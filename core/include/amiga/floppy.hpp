#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace amiga {

/// Amiga 3.5" DD floppy drive (DF0:) with ADF-to-MFM track encoding.
class FloppyDrive {
public:
    static constexpr size_t ADF_SIZE = 80 * 2 * 11 * 512;  // 901,120 bytes
    static constexpr size_t TRACK_WORDS = 6334;             // ~12,668 bytes per MFM track
    static constexpr size_t MAX_SCP_TRACKS = 168;

    void reset();
    void set_enabled(bool enabled) { enabled_ = enabled; }
    bool enabled() const { return enabled_; }
    void set_select_mask(uint8_t sel_bit_mask) { sel_bit_mask_ = sel_bit_mask; }

    bool insert_adf(const uint8_t* data, size_t size);
    bool insert_scp(const uint8_t* data, size_t size);
    bool insert_adf_file(const std::string& path);
    void eject();
    void set_write_protected(bool ro) { write_protected_ = ro; }
    bool write_protected() const { return write_protected_; }

    bool has_disk() const { return has_disk_; }
    const std::string& disk_path() const { return disk_path_; }
    uint8_t cylinder() const { return cylinder_; }
    uint8_t side() const { return side_; }
    bool motor_on() const { return motor_on_; }
    bool selected() const { return selected_; }
    bool consume_step_sound() {
        const bool v = pending_step_sound_;
        pending_step_sound_ = false;
        return v;
    }

    struct DriveStateSnapshot {
        uint8_t enabled = 1;
        uint8_t write_protected = 0;
        uint8_t has_disk = 0;
        uint8_t disk_changed = 0;
        uint8_t id_mode = 0;
        uint8_t selected = 0;
        uint8_t motor_on = 0;
        uint8_t cylinder = 0;
        uint8_t side = 0;
        uint8_t prev_prb = 0xFF;
        uint32_t track_pos = 0;
    };

    DriveStateSnapshot capture_state() const {
        DriveStateSnapshot s;
        s.enabled = enabled_ ? 1 : 0;
        s.write_protected = write_protected_ ? 1 : 0;
        s.has_disk = has_disk_ ? 1 : 0;
        s.disk_changed = disk_changed_ ? 1 : 0;
        s.id_mode = id_mode_ ? 1 : 0;
        s.selected = selected_ ? 1 : 0;
        s.motor_on = motor_on_ ? 1 : 0;
        s.cylinder = cylinder_;
        s.side = side_;
        s.prev_prb = prev_prb_;
        s.track_pos = static_cast<uint32_t>(track_pos_);
        return s;
    }

    void restore_state(const DriveStateSnapshot& s) {
        enabled_ = s.enabled != 0;
        write_protected_ = s.write_protected != 0;
        if (has_disk_) {
            disk_changed_ = s.disk_changed != 0;
        }
        id_mode_ = s.id_mode != 0;
        selected_ = s.selected != 0;
        motor_on_ = s.motor_on != 0;
        cylinder_ = s.cylinder < 84 ? s.cylinder : 0;
        side_ = s.side & 1u;
        prev_prb_ = s.prev_prb;
        cached_track_ = 0xFF;
        track_pos_ = s.track_pos;
    }

    /// Updates drive control lines from CIA-B PRB ($BFD100, active-low).
    void write_cia_b_prb(uint8_t prb);

    /// Returns CIA-A PRA bits 5..2 (/RDY, /TK0, /WPRO, /CHNG) for DF0:.
    uint8_t cia_a_status_bits() const;

    /// Reads next 16-bit MFM word from the current track and advances head position.
    /// Sets out_index=true when the track wraps around.
    uint16_t read_mfm_word(bool& out_index);
    /// Returns the most recently read 16-bit MFM word without advancing head position.
    uint16_t current_mfm_word() const {
        if (track_mfm_.empty()) return 0xAAAAu;
        const size_t n = track_mfm_.size();
        return track_mfm_[(track_pos_ + n - 1u) % n];
    }
    /// Returns the CPU cycle duration (at 7.09379 MHz) per MFM byte at the current head position (~114 cycles nominal).
    uint32_t current_byte_cpu_cycles() const { return last_byte_cpu_cycles_; }
    /// Scans the track bit-by-bit (or word-by-word) up to `max_words` for `sync_word` (e.g. $4489)
    /// and aligns subsequent 16-bit MFM word reads to the bit immediately after the sync word.
    bool sync_to_word(uint16_t sync_word, size_t max_words, bool& out_index);
    void write_mfm_word(uint16_t word);
    void finish_write_track();

private:
    void encode_track(uint8_t cyl, uint8_t side);
    static void mfm_encode_block(const uint8_t* even_src, const uint8_t* odd_src,
                                 size_t len, uint8_t* mfm_dst, uint8_t& prev_bit);
    static void mfm_encode_checksum(uint32_t sum_bits, uint8_t* mfm_dst8, uint8_t& prev_bit);
    static uint8_t mfm_encode_nibble_pair(uint8_t data_bits, uint8_t& prev_bit);

    std::vector<uint8_t> adf_data_;
    bool is_scp_ = false;
    std::array<std::vector<uint16_t>, MAX_SCP_TRACKS> scp_tracks_{};
    std::array<std::vector<uint8_t>, MAX_SCP_TRACKS> scp_byte_cycles_{};
    std::string disk_path_;
    std::vector<uint16_t> track_mfm_{TRACK_WORDS, 0xAAAAu};
    std::vector<uint8_t> track_byte_cycles_;
    uint32_t last_byte_cpu_cycles_ = 114u;
    std::vector<uint16_t> write_buf_;
    size_t track_pos_ = 0;

    bool has_disk_ = false;
    bool write_protected_ = false;
    bool disk_changed_ = false;  // /CHNG is active-low (0 = changed/no disk, 1 = present)
    bool enabled_ = true;
    uint8_t sel_bit_mask_ = 0x08u;  // 0x08=DF0, 0x10=DF1, 0x20=DF2, 0x40=DF3
    bool id_mode_ = false;
    bool selected_ = false;
    bool motor_on_ = false;
    uint8_t cylinder_ = 0;
    uint8_t side_ = 0;
    uint8_t prev_prb_ = 0xFF;
    uint8_t cached_track_ = 0xFF;
    bool pending_step_sound_ = false;
};

}  // namespace amiga
