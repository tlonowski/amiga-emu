#include "amiga/floppy.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>

namespace amiga {

void FloppyDrive::reset() {
    selected_ = false;
    motor_on_ = false;
    id_mode_ = false;
    cylinder_ = 0;
    side_ = 0;
    prev_prb_ = 0xFF;
    track_pos_ = 0;
    cached_track_ = 0xFF;
    disk_changed_ = false;
    pending_step_sound_ = false;
}

bool FloppyDrive::insert_adf(const uint8_t* data, size_t size) {
    if (!data || size != ADF_SIZE) {
        return false;
    }
    adf_data_.assign(data, data + size);
    is_scp_ = false;
    for (auto& t : scp_tracks_) t.clear();
    for (auto& c : scp_byte_cycles_) c.clear();
    track_byte_cycles_.clear();
    last_byte_cpu_cycles_ = 114u;
    disk_path_.clear();
    write_buf_.clear();
    has_disk_ = true;
    disk_changed_ = false;
    cached_track_ = 0xFF;
    track_pos_ = 0;
    return true;
}

bool FloppyDrive::insert_scp(const uint8_t* data, size_t size) {
    if (!data || size < 0x2B0u || data[0] != 'S' || data[1] != 'C' || data[2] != 'P') {
        return false;
    }
    const uint8_t num_revs = data[5];
    if (num_revs == 0) return false;

    auto read_le32 = [&](size_t off) -> uint32_t {
        if (off + 4u > size) return 0u;
        return static_cast<uint32_t>(data[off]) |
               (static_cast<uint32_t>(data[off + 1u]) << 8) |
               (static_cast<uint32_t>(data[off + 2u]) << 16) |
               (static_cast<uint32_t>(data[off + 3u]) << 24);
    };

    for (auto& t : scp_tracks_) t.clear();
    for (auto& c : scp_byte_cycles_) c.clear();
    bool any_track = false;

    for (size_t trk = 0; trk < MAX_SCP_TRACKS; ++trk) {
        const uint32_t trk_off = read_le32(0x10u + trk * 4u);
        if (trk_off == 0 || trk_off + 16u > size) continue;
        if (data[trk_off] != 'T' || data[trk_off + 1u] != 'R' || data[trk_off + 2u] != 'K') continue;

        // Prefer revolution 1 if multiple revolutions exist (avoids splice jitter at index 0), else revolution 0
        const size_t rev_idx = (num_revs > 1u) ? 1u : 0u;
        const size_t rev_hdr = trk_off + 4u + rev_idx * 12u;
        if (rev_hdr + 12u > size) continue;
        const uint32_t dur_ticks = read_le32(rev_hdr);
        const uint32_t num_cells = read_le32(rev_hdr + 4u);
        const uint32_t data_rel = read_le32(rev_hdr + 8u);
        const size_t cell_off = trk_off + data_rel;
        if (num_cells == 0 || cell_off + static_cast<size_t>(num_cells) * 2u > size) continue;

        // Estimate nominal bitcell period in 25ns ticks (~80 ticks = 2.0 us for DD 300 RPM)
        double bitcell_ticks = 80.0;
        if (dur_ticks > 7000000u && dur_ticks < 9000000u) {
            bitcell_ticks = static_cast<double>(dur_ticks) / 100000.0;
        }

        std::vector<uint8_t> bits;
        std::vector<uint16_t> bit_ticks;
        bits.reserve(static_cast<size_t>(num_cells) * 3u);
        bit_ticks.reserve(static_cast<size_t>(num_cells) * 3u);
        uint32_t acc = 0;
        double local_bitcell = bitcell_ticks;
        for (uint32_t c = 0; c < num_cells; ++c) {
            const uint16_t raw = static_cast<uint16_t>(
                (static_cast<uint16_t>(data[cell_off + c * 2u]) << 8) |
                data[cell_off + c * 2u + 1u]);
            if (raw == 0) {
                acc += 65536u;
                continue;
            }
            const uint32_t dt = acc + raw;
            acc = 0;
            int cells_total = static_cast<int>((static_cast<double>(dt) / local_bitcell) + 0.5);
            if (cells_total < 2) cells_total = 2;
            if (cells_total > 4) cells_total = 4;
            const double est_cell = static_cast<double>(dt) / static_cast<double>(cells_total);
            if (est_cell >= 55.0 && est_cell <= 105.0) {
                local_bitcell = local_bitcell * 0.96 + est_cell * 0.04;
            }
            const uint16_t per_bit_tick = static_cast<uint16_t>(
                std::clamp<uint32_t>(dt / static_cast<uint32_t>(cells_total), 40u, 160u));
            const int nzeros = cells_total - 1;
            for (int z = 0; z < nzeros; ++z) {
                bits.push_back(0u);
                bit_ticks.push_back(per_bit_tick);
            }
            bits.push_back(1u);
            bit_ticks.push_back(per_bit_tick);
        }

        if (bits.size() < 32u) continue;

        // Pack into 16-bit MFM words aligned to the first sync marker ($4489 or $A245) if present,
        // while also keeping per-sync realignment in sync_to_word().
        size_t align_bit = 0;
        uint16_t sr = 0;
        for (size_t b = 0; b < bits.size(); ++b) {
            sr = static_cast<uint16_t>((sr << 1) | bits[b]);
            if (b >= 15u && (sr == 0x4489u || sr == 0xA245u)) {
                align_bit = (b - 15u) & 15u;
                break;
            }
        }

        const size_t total_bits = bits.size();
        const size_t num_words = (total_bits - align_bit) / 16u;
        if (num_words == 0) continue;

        auto& out_words = scp_tracks_[trk];
        auto& out_cycles = scp_byte_cycles_[trk];
        out_words.resize(num_words);
        out_cycles.resize(num_words);
        for (size_t w = 0; w < num_words; ++w) {
            uint16_t word = 0;
            uint32_t ticks_sum = 0;
            const size_t base = align_bit + w * 16u;
            for (size_t k = 0; k < 16u; ++k) {
                word = static_cast<uint16_t>((word << 1) | bits[base + k]);
                ticks_sum += bit_ticks[base + k];
            }
            out_words[w] = word;
            // ticks_sum is the duration of 16 MFM bits (2 bytes) in 25ns units.
            // 1 byte duration = (ticks_sum / 2) * 25ns. At 7.09379 MHz (140.968 ns/cycle),
            // CPU cycles per byte = ticks_sum * 12.5 / 140.96836 ~= ticks_sum * 0.088672.
            const uint32_t cyc = std::clamp<uint32_t>(
                static_cast<uint32_t>((static_cast<double>(ticks_sum) * 0.088672) + 0.5), 80u, 180u);
            out_cycles[w] = static_cast<uint8_t>(cyc);
        }
        any_track = true;
    }

    if (!any_track) return false;
    adf_data_.clear();
    is_scp_ = true;
    disk_path_.clear();
    write_buf_.clear();
    has_disk_ = true;
    disk_changed_ = false;
    cached_track_ = 0xFF;
    track_pos_ = 0;
    return true;
}

bool FloppyDrive::insert_adf_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    const auto sz = f.tellg();
    if (sz <= 0) return false;
    std::vector<uint8_t> buf(static_cast<size_t>(sz));
    f.seekg(0);
    if (!f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()))) {
        return false;
    }
    if (buf.size() >= 16u && buf[0] == 'S' && buf[1] == 'C' && buf[2] == 'P') {
        if (!insert_scp(buf.data(), buf.size())) return false;
        disk_path_ = path;
        return true;
    }
    if (buf.size() != ADF_SIZE) return false;
    if (!insert_adf(buf.data(), buf.size())) return false;
    disk_path_ = path;
    return true;
}

void FloppyDrive::eject() {
    has_disk_ = false;
    is_scp_ = false;
    for (auto& t : scp_tracks_) t.clear();
    for (auto& c : scp_byte_cycles_) c.clear();
    track_byte_cycles_.clear();
    last_byte_cpu_cycles_ = 114u;
    disk_changed_ = true;
    disk_path_.clear();
    write_buf_.clear();
    adf_data_.clear();
    track_mfm_.assign(TRACK_WORDS, 0xAAAAu);
    cached_track_ = 0xFF;
}

void FloppyDrive::write_cia_b_prb(uint8_t prb) {
    const bool now_selected = (prb & sel_bit_mask_) == 0;  // /SELx
    const bool mtr_low = (prb & 0x80u) == 0;               // /MTR active low

    if (now_selected && !selected_) {
        // Falling edge of /SELx
        if (mtr_low) {
            motor_on_ = true;
            id_mode_ = false;
        } else {
            motor_on_ = false;
            id_mode_ = true;
        }
    } else if (now_selected) {
        if (mtr_low) {
            motor_on_ = true;
            id_mode_ = false;
        } else {
            motor_on_ = false;
        }
    }
    selected_ = now_selected;

    if (selected_) {
        side_ = ((prb & 0x04u) == 0) ? 1u : 0u;  // /SIDE: 1=lower(0), 0=upper(1)

        // /STEP falling edge (bit 0: 1 -> 0)
        if ((prev_prb_ & 0x01u) != 0 && (prb & 0x01u) == 0) {
            const bool dir_out = (prb & 0x02u) != 0;  // DIR: 1 = toward track 0
            if (dir_out) {
                if (cylinder_ > 0) --cylinder_;
            } else {
                if (cylinder_ < 79) ++cylinder_;
            }
            if (enabled_ && has_disk_) {
                disk_changed_ = false;  // Stepping clears /CHNG latch when disk is present
            }
            if (enabled_) {
                pending_step_sound_ = true;
            }
        }
    }
    prev_prb_ = prb;
}

uint8_t FloppyDrive::cia_a_status_bits() const {
    if (!selected_) {
        return 0x3Cu;  // All active-low lines high (inactive)
    }
    const bool disk_present = enabled_ && has_disk_;
    uint8_t bits = 0;
    // Bit 5: /RDY (0 = ready when disk inserted and motor on, OR 3.5" DD Drive ID = 0 in id_mode)
    if (!id_mode_ && (!disk_present || !motor_on_)) {
        bits |= 0x20u;
    }
    // Bit 4: /TK0 (0 = head on cylinder 0)
    if (cylinder_ != 0) {
        bits |= 0x10u;
    }
    // Bit 3: /WPRO (0 = write protected, 1 = writable)
    if (disk_present && !write_protected_) {
        bits |= 0x08u;
    }
    // Bit 2: /CHNG (0 = no disk or disk just changed, 1 = disk present & acknowledged)
    if (disk_present && !disk_changed_) {
        bits |= 0x04u;
    }
    return bits;
}

uint8_t FloppyDrive::mfm_encode_nibble_pair(uint8_t data_bits, uint8_t& prev_bit) {
    // data_bits holds 4 data bits in positions 3..0.
    // Output byte places data bits at 6,4,2,0 and clock bits at 7,5,3,1.
    uint8_t out = 0;
    for (int i = 3; i >= 0; --i) {
        const uint8_t d = (data_bits >> i) & 1u;
        const uint8_t c = (prev_bit == 0 && d == 0) ? 1u : 0u;
        out = static_cast<uint8_t>((out << 2) | (c << 1) | d);
        prev_bit = d;
    }
    return out;
}

void FloppyDrive::mfm_encode_block(const uint8_t* even_src, const uint8_t* odd_src,
                                   size_t len, uint8_t* mfm_dst, uint8_t& prev_bit) {
    // Amiga MFM encodes odd bits first, then even bits!
    // (Amiga HRM: first `len` bytes hold odd bits (b7,b5,b3,b1), next `len` bytes hold even bits (b6,b4,b2,b0)).
    for (size_t i = 0; i < len; ++i) {
        const uint8_t b = odd_src[i];
        const uint8_t odd_hi = static_cast<uint8_t>(((b >> 7) & 1u) << 3 | ((b >> 5) & 1u) << 2 |
                                                    ((b >> 3) & 1u) << 1 | ((b >> 1) & 1u));
        mfm_dst[i] = mfm_encode_nibble_pair(odd_hi, prev_bit);
    }
    for (size_t i = 0; i < len; ++i) {
        const uint8_t b = even_src[i];
        const uint8_t even_lo = static_cast<uint8_t>(((b >> 6) & 1u) << 3 | ((b >> 4) & 1u) << 2 |
                                                     ((b >> 2) & 1u) << 1 | (b & 1u));
        mfm_dst[len + i] = mfm_encode_nibble_pair(even_lo, prev_bit);
    }
}

void FloppyDrive::mfm_encode_checksum(uint32_t sum_bits, uint8_t* mfm_dst8, uint8_t& prev_bit) {
    // First 4 MFM bytes represent the odd bits of the checksum (always 0).
    for (int i = 0; i < 4; ++i) {
        mfm_dst8[i] = mfm_encode_nibble_pair(0, prev_bit);
    }
    // Next 4 MFM bytes hold the 16 data bits at positions 6,4,2,0 of each byte.
    for (int i = 0; i < 4; ++i) {
        const uint8_t b = static_cast<uint8_t>((sum_bits >> (24 - i * 8)) & 0xFFu);
        const uint8_t nibble = static_cast<uint8_t>(((b >> 6) & 1u) << 3 | ((b >> 4) & 1u) << 2 |
                                                    ((b >> 2) & 1u) << 1 | (b & 1u));
        mfm_dst8[4 + i] = mfm_encode_nibble_pair(nibble, prev_bit);
    }
}

void FloppyDrive::encode_track(uint8_t cyl, uint8_t side) {
    const uint8_t linear_track = static_cast<uint8_t>(cyl * 2u + side);
    if (cached_track_ == linear_track) {
        return;
    }
    cached_track_ = linear_track;

    std::array<uint8_t, TRACK_WORDS * 2> raw{};
    raw.fill(0xAAu);

    if (!has_disk_) {
        track_mfm_.assign(TRACK_WORDS, 0xAAAAu);
        return;
    }

    if (is_scp_) {
        if (linear_track < scp_tracks_.size() && !scp_tracks_[linear_track].empty()) {
            track_mfm_ = scp_tracks_[linear_track];
            track_byte_cycles_ = scp_byte_cycles_[linear_track];
        } else {
            track_mfm_.assign(TRACK_WORDS, 0xAAAAu);
            track_byte_cycles_.clear();
        }
        if (track_pos_ >= track_mfm_.size()) {
            track_pos_ = 0;
        }
        return;
    }

    track_byte_cycles_.clear();
    last_byte_cpu_cycles_ = 114u;
    track_mfm_.resize(TRACK_WORDS);

    size_t pos = 0;
    uint8_t prev_bit = 0;

    // Initial track gap
    pos += 140;

    for (uint8_t sec = 0; sec < 11; ++sec) {
        // 2 sync-gap bytes (0xAA) + 2 sync words (0x4489 0x4489)
        raw[pos++] = 0xAA;
        raw[pos++] = 0xAA;
        raw[pos++] = 0x44;
        raw[pos++] = 0x89;
        raw[pos++] = 0x44;
        raw[pos++] = 0x89;
        prev_bit = 1;  // 0x4489 ends with bit 0 = 1

        // Sector info (4 bytes) + label (16 zero bytes)
        std::array<uint8_t, 20> hdr{};
        hdr[0] = 0xFF;
        hdr[1] = linear_track;
        hdr[2] = sec;
        hdr[3] = static_cast<uint8_t>(11u - sec);

        // Encode 4 info bytes -> 8 MFM bytes, and 16 label bytes -> 32 MFM bytes
        uint8_t* hdr_mfm = &raw[pos];
        mfm_encode_block(hdr.data(), hdr.data(), 4, hdr_mfm, prev_bit);
        mfm_encode_block(hdr.data() + 4, hdr.data() + 4, 16, hdr_mfm + 8, prev_bit);
        pos += 40;

        // Header checksum: XOR of all 10 32-bit MFM longs (40 bytes) masked with 0x55555555
        uint32_t hdr_sum = 0;
        for (size_t i = 0; i < 40; i += 4) {
            const uint32_t w = (static_cast<uint32_t>(hdr_mfm[i]) << 24) |
                               (static_cast<uint32_t>(hdr_mfm[i + 1]) << 16) |
                               (static_cast<uint32_t>(hdr_mfm[i + 2]) << 8) |
                               static_cast<uint32_t>(hdr_mfm[i + 3]);
            hdr_sum ^= w;
        }
        hdr_sum &= 0x55555555u;
        mfm_encode_checksum(hdr_sum, &raw[pos], prev_bit);
        pos += 8;

        // Data block (512 bytes) -> 1024 MFM bytes
        const size_t adf_off = (static_cast<size_t>(linear_track) * 11u + sec) * 512u;
        const uint8_t* sec_data = &adf_data_[adf_off];
        uint8_t* data_mfm = &raw[pos + 8];
        uint8_t data_prev = 0;
        mfm_encode_block(sec_data, sec_data, 512, data_mfm, data_prev);

        // Data checksum
        uint32_t data_sum = 0;
        for (size_t i = 0; i < 1024; i += 4) {
            const uint32_t w = (static_cast<uint32_t>(data_mfm[i]) << 24) |
                               (static_cast<uint32_t>(data_mfm[i + 1]) << 16) |
                               (static_cast<uint32_t>(data_mfm[i + 2]) << 8) |
                               static_cast<uint32_t>(data_mfm[i + 3]);
            data_sum ^= w;
        }
        data_sum &= 0x55555555u;
        mfm_encode_checksum(data_sum, &raw[pos], prev_bit);
        // Re-encode data block with the exact prev_bit from data checksum
        mfm_encode_block(sec_data, sec_data, 512, data_mfm, prev_bit);
        pos += 8 + 1024;

        // Inter-sector gap (2 bytes to keep 0x4489 word-aligned)
        raw[pos++] = (prev_bit == 0) ? 0xAAu : 0x2Au;
        raw[pos++] = 0xAAu;
        prev_bit = 0;
    }

    for (size_t i = 0; i < TRACK_WORDS; ++i) {
        track_mfm_[i] = static_cast<uint16_t>((static_cast<uint16_t>(raw[i * 2]) << 8) | raw[i * 2 + 1]);
    }
}

uint16_t FloppyDrive::read_mfm_word(bool& out_index) {
    encode_track(cylinder_, side_);
    if (track_mfm_.empty()) {
        out_index = true;
        return 0xAAAAu;
    }
    if (track_pos_ >= track_mfm_.size()) {
        track_pos_ = 0;
    }
    const uint16_t w = track_mfm_[track_pos_];
    if (track_pos_ < track_byte_cycles_.size()) {
        last_byte_cpu_cycles_ = track_byte_cycles_[track_pos_];
    } else {
        last_byte_cpu_cycles_ = 114u;
    }
    ++track_pos_;
    out_index = false;
    if (track_pos_ >= track_mfm_.size()) {
        track_pos_ = 0;
        out_index = true;
    }
    return w;
}

bool FloppyDrive::sync_to_word(uint16_t sync_word, size_t max_words, bool& out_index) {
    encode_track(cylinder_, side_);
    const size_t n = track_mfm_.size();
    if (n == 0) {
        out_index = true;
        return false;
    }
    if (!is_scp_) {
        for (size_t i = 0; i < max_words; ++i) {
            bool idx = false;
            const uint16_t w = read_mfm_word(idx);
            if (idx) out_index = true;
            if (w == sync_word) return true;
        }
        return false;
    }

    // Bit-level Paula shift-register sync search across raw SCP MFM stream:
    // If sync_word is found at a non-zero bit offset, barrel-shift the circular track
    // by that bit offset so all subsequent 16-bit DMA words are bit-aligned to DSKSYNC.
    if (track_pos_ >= n) track_pos_ = 0;
    uint32_t sr = track_mfm_[(track_pos_ + n - 1u) % n];
    for (size_t step = 0; step < max_words; ++step) {
        const size_t cur_w = track_pos_;
        const uint16_t w = track_mfm_[cur_w];
        track_pos_ = (cur_w + 1u) % n;
        if (track_pos_ == 0) out_index = true;

        for (unsigned bit = 0; bit < 16u; ++bit) {
            sr = ((sr << 1) | ((w >> (15u - bit)) & 1u)) & 0xFFFFu;
            if (static_cast<uint16_t>(sr) == sync_word) {
                const unsigned shift = (bit + 1u) & 15u;
                if (shift != 0u) {
                    std::vector<uint16_t> shifted(n);
                    std::vector<uint8_t> shifted_cyc(track_byte_cycles_.size());
                    for (size_t k = 0; k < n; ++k) {
                        const uint32_t hi = track_mfm_[k];
                        const uint32_t lo = track_mfm_[(k + 1u) % n];
                        shifted[k] = static_cast<uint16_t>(((hi << shift) | (lo >> (16u - shift))) & 0xFFFFu);
                        if (k < track_byte_cycles_.size()) {
                            shifted_cyc[k] = track_byte_cycles_[k];
                        }
                    }
                    track_mfm_.swap(shifted);
                    track_byte_cycles_.swap(shifted_cyc);
                    const uint8_t linear_track = static_cast<uint8_t>(cylinder_ * 2u + side_);
                    if (linear_track < scp_tracks_.size()) {
                        scp_tracks_[linear_track] = track_mfm_;
                        scp_byte_cycles_[linear_track] = track_byte_cycles_;
                    }
                    track_pos_ = (cur_w + 1u) % n;
                }
                if (track_pos_ < track_byte_cycles_.size()) {
                    last_byte_cpu_cycles_ = track_byte_cycles_[track_pos_];
                }
                return true;
            }
        }
    }
    return false;
}

void FloppyDrive::write_mfm_word(uint16_t word) {
    if (!has_disk_ || write_protected_) return;
    if (write_buf_.size() < TRACK_WORDS * 2u) {
        write_buf_.push_back(word);
    }
}

void FloppyDrive::finish_write_track() {
    if (!has_disk_ || write_protected_ || write_buf_.empty()) {
        write_buf_.clear();
        return;
    }

    std::vector<uint8_t> bytes;
    bytes.reserve(write_buf_.size() * 2u);
    for (uint16_t w : write_buf_) {
        bytes.push_back(static_cast<uint8_t>(w >> 8));
        bytes.push_back(static_cast<uint8_t>(w & 0xFFu));
    }
    write_buf_.clear();

    auto decode_mfm = [](const uint8_t* mfm, uint8_t* dst, size_t len) {
        for (size_t i = 0; i < len; ++i) {
            dst[i] = static_cast<uint8_t>(((mfm[i] & 0x55u) << 1) | (mfm[len + i] & 0x55u));
        }
    };

    const uint8_t expected_track = static_cast<uint8_t>(cylinder_ * 2u + side_);
    bool any_written = false;
    size_t i = 0;
    while (i + 1u < bytes.size()) {
        if (bytes[i] == 0x44u && bytes[i + 1u] == 0x89u) {
            i += 2u;
            while (i + 1u < bytes.size() && bytes[i] == 0x44u && bytes[i + 1u] == 0x89u) {
                i += 2u;
            }
            if (i + 1080u <= bytes.size()) {
                uint8_t info[4]{};
                decode_mfm(&bytes[i], info, 4);
                if (info[0] == 0xFFu && info[1] == expected_track && info[2] < 11u) {
                    const size_t adf_off = (static_cast<size_t>(expected_track) * 11u + info[2]) * 512u;
                    if (adf_off + 512u <= adf_data_.size()) {
                        decode_mfm(&bytes[i + 56u], &adf_data_[adf_off], 512u);
                        any_written = true;
                        i += 1080u;
                        continue;
                    }
                }
            }
        } else {
            ++i;
        }
    }

    if (any_written) {
        cached_track_ = 0xFF;
        if (!disk_path_.empty()) {
            std::fstream out(disk_path_, std::ios::in | std::ios::out | std::ios::binary);
            if (out) {
                const size_t track_off = static_cast<size_t>(expected_track) * 11u * 512u;
                out.seekp(static_cast<std::streamoff>(track_off), std::ios::beg);
                out.write(reinterpret_cast<const char*>(&adf_data_[track_off]), 11 * 512);
                out.flush();
            }
        }
    }
}

}  // namespace amiga
