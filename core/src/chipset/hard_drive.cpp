#include "amiga/hard_drive.hpp"
#include "amiga/bus.hpp"

#include <algorithm>
#include <cstring>

namespace amiga {

namespace {

uint32_t be32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}

uint64_t be64(const uint8_t* p) {
    return (static_cast<uint64_t>(be32(p)) << 32) | be32(p + 4);
}

void emit16(std::vector<uint8_t>& buf, size_t off, uint16_t v) {
    buf[off] = static_cast<uint8_t>(v >> 8);
    buf[off + 1] = static_cast<uint8_t>(v & 0xFFu);
}

void emit32(std::vector<uint8_t>& buf, size_t off, uint32_t v) {
    buf[off] = static_cast<uint8_t>((v >> 24) & 0xFFu);
    buf[off + 1] = static_cast<uint8_t>((v >> 16) & 0xFFu);
    buf[off + 2] = static_cast<uint8_t>((v >> 8) & 0xFFu);
    buf[off + 3] = static_cast<uint8_t>(v & 0xFFu);
}

class CodeEmitter {
public:
    CodeEmitter(std::vector<uint8_t>& rom, size_t start_off)
        : rom_(rom), pos_(start_off) {}

    size_t pos() const { return pos_; }
    void w16(uint16_t v) {
        emit16(rom_, pos_, v);
        pos_ += 2;
    }
    void w32(uint32_t v) {
        emit32(rom_, pos_, v);
        pos_ += 4;
    }
    void str(const char* s) {
        while (*s) {
            rom_[pos_++] = static_cast<uint8_t>(*s++);
        }
        rom_[pos_++] = 0;
        if (pos_ & 1u) rom_[pos_++] = 0;
    }

private:
    std::vector<uint8_t>& rom_;
    size_t pos_;
};

uint32_t read_be32_bus(Bus& bus, uint32_t addr) {
    return bus.read32(addr);
}

void write_be32_bus(Bus& bus, uint32_t addr, uint32_t val) {
    bus.write32(addr, val);
}

uint32_t compute_hunk_alloc_size(const std::vector<uint8_t>& bin) {
    if (bin.size() < 24 || be32(bin.data()) != 0x000003F3u) return 0;
    uint32_t pos = 8;  // skip HUNK_HEADER + 0
    if (pos + 12 > bin.size()) return 0;
    pos += 4;  // table_size
    const uint32_t first_h = be32(bin.data() + pos);
    const uint32_t last_h = be32(bin.data() + pos + 4);
    pos += 8;
    if (last_h < first_h) return 0;
    const uint32_t count = last_h - first_h + 1u;
    if (pos + count * 4u > bin.size()) return 0;
    uint32_t total = 64u;  // FileSysEntry (62 bytes, aligned to 64)
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t sz_longs = be32(bin.data() + pos + i * 4u) & 0x3FFFFFFFu;
        total += 8u + sz_longs * 4u;  // 4-byte size + 4-byte BPTR next + payload
    }
    return total;
}

}  // namespace

void HardDriveController::reset() {
    configured_ = false;
    shut_up_ = false;
    base_addr_ = 0;
    selected_unit_ = 0;
    name_target_addr_ = 0;
    if (rom_.empty()) {
        build_rom();
    }
}

void HardDriveController::clear_units() {
    units_.clear();
}

bool HardDriveController::read_virtual(Unit& u, uint64_t off, uint8_t* dst, uint32_t len) {
    if (off + len > u.size_bytes) return false;
    if (!u.is_vhd) {
        u.file.clear();
        u.file.seekg(static_cast<std::streamoff>(off), std::ios::beg);
        u.file.read(reinterpret_cast<char*>(dst), static_cast<std::streamsize>(len));
        return static_cast<bool>(u.file);
    }

    uint32_t rem = len;
    uint64_t cur = off;
    uint8_t* out = dst;
    while (rem > 0) {
        const uint64_t lba = cur / 512ull;
        const uint32_t in_sec = static_cast<uint32_t>(cur % 512ull);
        const uint32_t chunk = std::min<uint32_t>(rem, 512u - in_sec);
        const uint32_t bi = static_cast<uint32_t>(lba / u.vhd_sec_per_blk);
        const uint32_t so = static_cast<uint32_t>(lba % u.vhd_sec_per_blk);
        if (bi >= u.vhd_bat.size() || u.vhd_bat[bi] == 0xFFFFFFFFu) {
            std::memset(out, 0, chunk);
        } else {
            const uint64_t phys = (static_cast<uint64_t>(u.vhd_bat[bi]) + u.vhd_bm_secs + so) * 512ull + in_sec;
            u.file.clear();
            u.file.seekg(static_cast<std::streamoff>(phys), std::ios::beg);
            u.file.read(reinterpret_cast<char*>(out), static_cast<std::streamsize>(chunk));
            if (!u.file) return false;
        }
        cur += chunk;
        out += chunk;
        rem -= chunk;
    }
    return true;
}

bool HardDriveController::write_virtual(Unit& u, uint64_t off, const uint8_t* src, uint32_t len) {
    if (off + len > u.size_bytes) return false;
    if (!u.is_vhd) {
        u.file.clear();
        u.file.seekp(static_cast<std::streamoff>(off), std::ios::beg);
        u.file.write(reinterpret_cast<const char*>(src), static_cast<std::streamsize>(len));
        u.file.flush();
        return static_cast<bool>(u.file);
    }

    uint32_t rem = len;
    uint64_t cur = off;
    const uint8_t* in = src;
    while (rem > 0) {
        const uint64_t lba = cur / 512ull;
        const uint32_t in_sec = static_cast<uint32_t>(cur % 512ull);
        const uint32_t chunk = std::min<uint32_t>(rem, 512u - in_sec);
        const uint32_t bi = static_cast<uint32_t>(lba / u.vhd_sec_per_blk);
        const uint32_t so = static_cast<uint32_t>(lba % u.vhd_sec_per_blk);
        if (bi >= u.vhd_bat.size()) return false;

        if (u.vhd_bat[bi] == 0xFFFFFFFFu) {
            u.file.clear();
            u.file.seekg(0, std::ios::end);
            const uint64_t fsz = static_cast<uint64_t>(u.file.tellg());
            const uint64_t blk_off = (fsz >= 512ull) ? (fsz - 512ull) : fsz;
            const uint32_t new_sec = static_cast<uint32_t>(blk_off / 512ull);
            std::vector<uint8_t> zeros(static_cast<size_t>(u.vhd_bm_secs) * 512u + u.vhd_block_size, 0);
            u.file.seekp(static_cast<std::streamoff>(blk_off), std::ios::beg);
            u.file.write(reinterpret_cast<const char*>(zeros.data()), static_cast<std::streamsize>(zeros.size()));
            u.file.write(reinterpret_cast<const char*>(u.vhd_footer.data()), 512);
            u.vhd_bat[bi] = new_sec;
            uint8_t bat_be[4]{};
            bat_be[0] = static_cast<uint8_t>((new_sec >> 24) & 0xFFu);
            bat_be[1] = static_cast<uint8_t>((new_sec >> 16) & 0xFFu);
            bat_be[2] = static_cast<uint8_t>((new_sec >> 8) & 0xFFu);
            bat_be[3] = static_cast<uint8_t>(new_sec & 0xFFu);
            u.file.seekp(static_cast<std::streamoff>(u.vhd_bat_off + static_cast<uint64_t>(bi) * 4ull), std::ios::beg);
            u.file.write(reinterpret_cast<const char*>(bat_be), 4);
        }

        const uint64_t phys = (static_cast<uint64_t>(u.vhd_bat[bi]) + u.vhd_bm_secs + so) * 512ull + in_sec;
        u.file.clear();
        u.file.seekp(static_cast<std::streamoff>(phys), std::ios::beg);
        u.file.write(reinterpret_cast<const char*>(in), static_cast<std::streamsize>(chunk));

        // Mark sector valid in block bitmap
        const uint64_t bm_byte_off = static_cast<uint64_t>(u.vhd_bat[bi]) * 512ull + (so >> 3);
        uint8_t bm = 0;
        u.file.seekg(static_cast<std::streamoff>(bm_byte_off), std::ios::beg);
        u.file.read(reinterpret_cast<char*>(&bm), 1);
        bm |= static_cast<uint8_t>(0x80u >> (so & 7u));
        u.file.seekp(static_cast<std::streamoff>(bm_byte_off), std::ios::beg);
        u.file.write(reinterpret_cast<const char*>(&bm), 1);
        if (!u.file) return false;

        cur += chunk;
        in += chunk;
        rem -= chunk;
    }
    u.file.flush();
    return true;
}

size_t HardDriveController::add_hdf(const std::string& path) {
    auto u = std::make_unique<Unit>();
    u->path = path;
    u->file.open(path, std::ios::in | std::ios::out | std::ios::binary);
    if (!u->file.is_open()) {
        u->file.open(path, std::ios::in | std::ios::binary);
    }
    if (!u->file.is_open()) {
        return static_cast<size_t>(-1);
    }
    u->file.seekg(0, std::ios::end);
    const uint64_t raw_size = static_cast<uint64_t>(u->file.tellg());
    if (raw_size < 512u * 32u) {
        return static_cast<size_t>(-1);
    }

    uint8_t hdr[512]{};
    u->file.seekg(0, std::ios::beg);
    u->file.read(reinterpret_cast<char*>(hdr), 512);

    if (std::memcmp(hdr, "conectix", 8) == 0 && be32(hdr + 60) == 3u) {
        u->is_vhd = true;
        std::memcpy(u->vhd_footer.data(), hdr, 512);
        const uint64_t data_off = be64(hdr + 16);
        u->size_bytes = be64(hdr + 48);
        uint8_t dyn[1024]{};
        u->file.seekg(static_cast<std::streamoff>(data_off), std::ios::beg);
        u->file.read(reinterpret_cast<char*>(dyn), 1024);
        if (std::memcmp(dyn, "cxsparse", 8) != 0) {
            return static_cast<size_t>(-1);
        }
        u->vhd_bat_off = be64(dyn + 16);
        const uint32_t max_entries = be32(dyn + 28);
        u->vhd_block_size = be32(dyn + 32);
        if (u->vhd_block_size < 512u || max_entries == 0) {
            return static_cast<size_t>(-1);
        }
        u->vhd_sec_per_blk = u->vhd_block_size / 512u;
        u->vhd_bm_secs = ((u->vhd_sec_per_blk + 7u) / 8u + 511u) / 512u;
        std::vector<uint8_t> bat_raw(static_cast<size_t>(max_entries) * 4u);
        u->file.seekg(static_cast<std::streamoff>(u->vhd_bat_off), std::ios::beg);
        u->file.read(reinterpret_cast<char*>(bat_raw.data()), static_cast<std::streamsize>(bat_raw.size()));
        u->vhd_bat.resize(max_entries);
        for (uint32_t i = 0; i < max_entries; ++i) {
            u->vhd_bat[i] = be32(bat_raw.data() + i * 4u);
        }
    } else {
        u->size_bytes = raw_size;
    }

    // Check for RDB (RDSK) in virtual sectors 0..15
    bool found_rdb = false;
    uint8_t sec[512]{};
    for (uint32_t s = 0; s < 16; ++s) {
        if (!read_virtual(*u, static_cast<uint64_t>(s) * 512ull, sec, 512)) break;
        if (std::memcmp(sec, "RDSK", 4) == 0) {
            found_rdb = true;
            break;
        }
    }

    const size_t idx = units_.size();
    u->dos_name = "DH" + std::to_string(idx);
    u->boot_pri = (idx == 0) ? 0 : -128;

    if (found_rdb) {
        const uint32_t part_head = be32(sec + 28);
        const uint32_t fshd_head = be32(sec + 32);
        u->cylinders = be32(sec + 64);

        if (part_head != 0xFFFFFFFFu) {
            uint8_t pb[512]{};
            if (read_virtual(*u, static_cast<uint64_t>(part_head) * 512ull, pb, 512) &&
                std::memcmp(pb, "PART", 4) == 0) {
                const uint8_t nlen = pb[36];
                if (nlen > 0 && nlen <= 31) {
                    u->dos_name.assign(reinterpret_cast<const char*>(pb + 37), nlen);
                }
                u->surfaces = be32(pb + 128 + 3 * 4);
                u->sectors_per_block = be32(pb + 128 + 4 * 4);
                u->blocks_per_track = be32(pb + 128 + 5 * 4);
                u->reserved = be32(pb + 128 + 6 * 4);
                u->low_cyl = be32(pb + 128 + 9 * 4);
                u->high_cyl = be32(pb + 128 + 10 * 4);
                u->num_buffers = be32(pb + 128 + 11 * 4);
                u->max_transfer = be32(pb + 128 + 13 * 4);
                u->mask = be32(pb + 128 + 14 * 4);
                u->boot_pri = static_cast<int32_t>(be32(pb + 128 + 15 * 4));
                u->dostype = be32(pb + 128 + 16 * 4);
            }
        }

        uint32_t fblk = fshd_head;
        while (fblk != 0xFFFFFFFFu) {
            uint8_t fb[512]{};
            if (!read_virtual(*u, static_cast<uint64_t>(fblk) * 512ull, fb, 512)) break;
            if (std::memcmp(fb, "FSHD", 4) != 0) break;
            const uint32_t fdt = be32(fb + 32);
            if (fdt == u->dostype) {
                u->fs_version = be32(fb + 36);
                uint32_t cur = be32(fb + 72);
                while (cur != 0xFFFFFFFFu) {
                    uint8_t lb[512]{};
                    if (!read_virtual(*u, static_cast<uint64_t>(cur) * 512ull, lb, 512)) break;
                    if (std::memcmp(lb, "LSEG", 4) != 0) break;
                    const uint32_t sl = be32(lb + 4);
                    if (sl > 5u && sl <= 128u) {
                        const size_t bytes = static_cast<size_t>(sl - 5u) * 4u;
                        u->fs_binary.insert(u->fs_binary.end(), lb + 20, lb + 20 + bytes);
                    }
                    cur = be32(lb + 16);
                }
                u->fs_alloc_size = compute_hunk_alloc_size(u->fs_binary);
                break;
            }
            fblk = be32(fb + 16);
        }
    } else {
        if (hdr[0] == 'D' && hdr[1] == 'O' && hdr[2] == 'S' && hdr[3] <= 7) {
            u->dostype = be32(hdr);
        } else {
            u->dostype = 0x444F5301u;  // DOS\1
        }
        u->cylinders = static_cast<uint32_t>(u->size_bytes / (32ull * 512ull));
        u->high_cyl = (u->cylinders > 0) ? (u->cylinders - 1u) : 0u;
    }

    units_.push_back(std::move(u));
    return idx;
}

void HardDriveController::remove_hdf(size_t index) {
    if (index >= units_.size()) return;
    units_.erase(units_.begin() + static_cast<std::ptrdiff_t>(index));
    for (size_t i = 0; i < units_.size(); ++i) {
        if (units_[i]->fs_binary.empty() && units_[i]->low_cyl == 0) {
            units_[i]->dos_name = "DH" + std::to_string(i);
            units_[i]->boot_pri = (i == 0) ? 0 : -128;
        }
    }
}

bool HardDriveController::install_filesystem(const Unit& u, uint32_t mem_addr, Bus& bus) const {
    const auto& bin = u.fs_binary;
    if (bin.size() < 24 || mem_addr == 0 || be32(bin.data()) != 0x000003F3u) return false;

    uint32_t pos = 12;
    const uint32_t first_h = be32(bin.data() + pos);
    const uint32_t last_h = be32(bin.data() + pos + 4);
    pos += 8;
    const uint32_t count = last_h - first_h + 1u;

    std::vector<uint32_t> hunk_addrs(count, 0);
    uint32_t cur_addr = mem_addr + 64u;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t sz_longs = be32(bin.data() + pos + i * 4u) & 0x3FFFFFFFu;
        const uint32_t seg_bytes = 8u + sz_longs * 4u;
        write_be32_bus(bus, cur_addr, seg_bytes);
        hunk_addrs[i] = cur_addr + 4u;  // points to BPTR next_seg
        if (i > 0) {
            write_be32_bus(bus, hunk_addrs[i - 1], hunk_addrs[i] >> 2);
        }
        cur_addr += seg_bytes;
    }
    pos += count * 4u;

    uint32_t cur_hunk = 0;
    while (pos + 4u <= bin.size() && cur_hunk < count) {
        const uint32_t ht = be32(bin.data() + pos) & 0x3FFFFFFFu;
        pos += 4u;
        if (ht == 0x3E9u || ht == 0x3EAu) {  // HUNK_CODE / HUNK_DATA
            const uint32_t n = be32(bin.data() + pos);
            pos += 4u;
            const uint32_t dst = hunk_addrs[cur_hunk] + 4u;
            for (uint32_t i = 0; i < n * 4u && pos + i < bin.size(); ++i) {
                bus.write8(dst + i, bin[pos + i]);
            }
            pos += n * 4u;
        } else if (ht == 0x3EBu) {  // HUNK_BSS
            pos += 4u;
        } else if (ht == 0x3ECu) {  // HUNK_RELOC32
            while (pos + 4u <= bin.size()) {
                const uint32_t rcount = be32(bin.data() + pos);
                pos += 4u;
                if (rcount == 0) break;
                const uint32_t target_hunk = be32(bin.data() + pos);
                pos += 4u;
                const uint32_t target_base = (target_hunk < count) ? (hunk_addrs[target_hunk] + 4u) : 0u;
                const uint32_t src_base = hunk_addrs[cur_hunk] + 4u;
                for (uint32_t r = 0; r < rcount && pos + 4u <= bin.size(); ++r) {
                    const uint32_t off = be32(bin.data() + pos);
                    pos += 4u;
                    const uint32_t cur_val = read_be32_bus(bus, src_base + off);
                    write_be32_bus(bus, src_base + off, cur_val + target_base);
                }
            }
        } else if (ht == 0x3F0u) {  // HUNK_SYMBOL
            while (pos + 4u <= bin.size()) {
                const uint32_t nl = be32(bin.data() + pos);
                pos += 4u;
                if (nl == 0) break;
                pos += nl * 4u + 4u;
            }
        } else if (ht == 0x3F1u) {  // HUNK_DEBUG
            const uint32_t n = be32(bin.data() + pos);
            pos += 4u + n * 4u;
        } else if (ht == 0x3F2u) {  // HUNK_END
            ++cur_hunk;
        } else {
            break;
        }
    }

    // Fill struct FileSysEntry at mem_addr
    write_be32_bus(bus, mem_addr + 0x0Eu, u.dostype);
    write_be32_bus(bus, mem_addr + 0x12u, u.fs_version);
    write_be32_bus(bus, mem_addr + 0x16u, 0x00000180u);  // PatchFlags: SegList + GlobalVec
    write_be32_bus(bus, mem_addr + 0x36u, hunk_addrs[0] >> 2);
    write_be32_bus(bus, mem_addr + 0x3Au, 0xFFFFFFFFu);  // GlobalVec = -1
    return true;
}

void HardDriveController::fill_parmpkt(const Unit& u, uint32_t pkt_addr, uint32_t dos_name_addr, Bus& bus) const {
    if (dos_name_addr != 0) {
        for (size_t i = 0; i < u.dos_name.size() && i < 15; ++i) {
            bus.write8(dos_name_addr + static_cast<uint32_t>(i), static_cast<uint8_t>(u.dos_name[i]));
        }
        bus.write8(dos_name_addr + static_cast<uint32_t>(std::min<size_t>(u.dos_name.size(), 15)), 0);
    }
    write_be32_bus(bus, pkt_addr + 0, dos_name_addr);
    write_be32_bus(bus, pkt_addr + 4, base_addr_ + 0x0500u);  // "amigahd.device"
    write_be32_bus(bus, pkt_addr + 8, selected_unit_);
    write_be32_bus(bus, pkt_addr + 12, 0);
    write_be32_bus(bus, pkt_addr + 16, 16);   // de_TableSize
    write_be32_bus(bus, pkt_addr + 20, 128);  // de_SizeBlock
    write_be32_bus(bus, pkt_addr + 24, 0);    // de_SecOrg
    write_be32_bus(bus, pkt_addr + 28, u.surfaces);
    write_be32_bus(bus, pkt_addr + 32, u.sectors_per_block);
    write_be32_bus(bus, pkt_addr + 36, u.blocks_per_track);
    write_be32_bus(bus, pkt_addr + 40, u.reserved);
    write_be32_bus(bus, pkt_addr + 44, 0);    // de_PreAlloc
    write_be32_bus(bus, pkt_addr + 48, 0);    // de_Interleave
    write_be32_bus(bus, pkt_addr + 52, u.low_cyl);
    write_be32_bus(bus, pkt_addr + 56, u.high_cyl);
    write_be32_bus(bus, pkt_addr + 60, u.num_buffers);
    write_be32_bus(bus, pkt_addr + 64, 0);    // de_BufMemType
    write_be32_bus(bus, pkt_addr + 68, u.max_transfer);
    write_be32_bus(bus, pkt_addr + 72, u.mask);
    write_be32_bus(bus, pkt_addr + 76, static_cast<uint32_t>(u.boot_pri));
    write_be32_bus(bus, pkt_addr + 80, u.dostype);
}

void HardDriveController::build_rom() {
    rom_.assign(0x2000u, 0);

    // DiagArea at $0100 (relative to board base)
    rom_[0x0100] = 0x90;  // DAC_WORDWIDE | DAC_CONFIGTIME
    rom_[0x0101] = 0x00;  // da_Flags
    emit16(rom_, 0x0102, 0x0800);  // da_Size
    emit16(rom_, 0x0104, 0x0020);  // da_DiagPoint -> $0120
    emit16(rom_, 0x0106, 0x0300);  // da_BootPoint -> $0400 (BootEntry)

    const size_t off_dev_name = 0x0500;
    const size_t off_exp_name = 0x0520;
    const size_t off_nsd_cmds = 0x0540;
    {
        CodeEmitter se(rom_, off_dev_name);
        se.str("amigahd.device");
        CodeEmitter se2(rom_, off_exp_name);
        se2.str("expansion.library");

        static constexpr uint16_t kNsdCmds[] = {
            2, 3, 9, 10, 11, 13, 14, 15, 18, 20, 21, 22, 24, 25, 27,
            0x4000, 0xC000, 0xC001, 0xC002, 0
        };
        for (size_t i = 0; i < sizeof(kNsdCmds) / sizeof(kNsdCmds[0]); ++i) {
            emit16(rom_, off_nsd_cmds + i * 2u, kNsdCmds[i]);
        }
    }

    // $0400: BootEntry() -> A6 = SysBase, FindResident("dos.library") + InitResident
    {
        CodeEmitter e(rom_, 0x0400);
        e.w16(0x2C78); e.w16(0x0004);  // MOVEA.L $4.W, A6
        e.w16(0x43FA); e.w16(0x0016);  // LEA dosname(PC), A1
        e.w16(0x4EAE); e.w16(0xFFA0);  // JSR -$60(A6)  (FindResident)
        e.w16(0x4A80);                 // TST.L D0
        e.w16(0x6708);                 // BEQ.S +8
        e.w16(0x2240);                 // MOVEA.L D0, A1  (resident in A1!)
        e.w16(0x7200);                 // MOVEQ #0, D1
        e.w16(0x4EAE); e.w16(0xFF9A);  // JSR -$66(A6)  (InitResident)
        e.w16(0x4E75);                 // RTS
        e.w16(0x4E71);                 // NOP
        e.str("dos.library");
    }

    // $0440: DevOpen
    {
        CodeEmitter e(rom_, 0x0440);
        e.w16(0x2340); e.w16(0x0018);  // MOVE.L D0, $18(A1)  (io_Unit = unit)
        e.w16(0x526E); e.w16(0x0020);  // ADDQ.W #1, $20(A6)  (LIB_OPENCNT)
        e.w16(0x4229); e.w16(0x001F);  // CLR.B $1F(A1)       (io_Error = 0)
        e.w16(0x7000);                 // MOVEQ #0, D0
        e.w16(0x4E75);                 // RTS
    }

    // $0460: DevClose
    {
        CodeEmitter e(rom_, 0x0460);
        e.w16(0x536E); e.w16(0x0020);  // SUBQ.W #1, $20(A6)
        e.w16(0x7000);                 // MOVEQ #0, D0
        e.w16(0x4E75);                 // RTS
    }

    // $0470: DevNull (Expunge / ExtFunc / AbortIO)
    {
        CodeEmitter e(rom_, 0x0470);
        e.w16(0x7000);                 // MOVEQ #0, D0
        e.w16(0x4E75);                 // RTS
    }

    // $0480: DevBeginIO(A1=io, A6=dev)
    {
        CodeEmitter e(rom_, 0x0480);
        e.w16(0x41FA); e.w16(static_cast<uint16_t>(0x10000 - 0x0482));  // LEA -$0482(PC), A0 -> boardBase
        e.w16(0xD1FC); e.w32(0x0000F004u);                              // ADDA.L #$F004, A0
        e.w16(0x2089);                                                  // MOVE.L A1, (A0)
        e.w16(0x0829); e.w16(0x0000); e.w16(0x001E);                    // BTST #0, $1E(A1)
        e.w16(0x6608);                                                  // BNE.S +8
        e.w16(0x2C78); e.w16(0x0004);                                   // MOVEA.L $4.W, A6
        e.w16(0x4EAE); e.w16(0xFE86);                                   // JSR -$17A(A6) (ReplyMsg)
        e.w16(0x4E75);                                                  // RTS
    }

    // $0120: DiagEntry(A0=boardBase, A2=diagArea, A3=configDev, A6=SysBase)
    {
        CodeEmitter e(rom_, 0x0120);
        e.w16(0x48E7); e.w16(0x3F3E);  // MOVEM.L D2-D7/A2-A6, -(A7)
        e.w16(0x2A48);                 // MOVEA.L A0, A5  (A5 = boardBase)
        e.w16(0x254D); e.w16(0x0010);  // MOVE.L A5, $10(A2) (save boardBase in RAM diagArea+$10 for BootEntry)
        e.w16(0x284B);                 // MOVEA.L A3, A4  (A4 = configDev)

        // Check unit count at $F000(A5) -> if 0, skip mounting
        e.w16(0x204D);                 // MOVEA.L A5, A0
        e.w16(0xD1FC); e.w32(0x0000F000u);
        e.w16(0x2810);                 // MOVE.L (A0), D4  (D4 = unit count)
        e.w16(0x6700);
        const size_t patch_beq_done = e.pos();
        e.w16(0x0000);

        // Fill FuncTable in RAM on stack (7 longs = 28 bytes)
        e.w16(0x4FEF); e.w16(static_cast<uint16_t>(-28));
        e.w16(0x204F);                 // MOVEA.L A7, A0
        auto push_vec = [&](uint32_t fn_off) {
            e.w16(0x200D);             // MOVE.L A5, D0
            e.w16(0x0680); e.w32(fn_off);
            e.w16(0x20C0);             // MOVE.L D0, (A0)+
        };
        push_vec(0x0440);  // Open
        push_vec(0x0460);  // Close
        push_vec(0x0470);  // Expunge
        push_vec(0x0470);  // ExtFunc
        push_vec(0x0480);  // BeginIO
        push_vec(0x0470);  // AbortIO
        e.w16(0x20FC); e.w32(0xFFFFFFFFu);

        // MakeLibrary(A0=funcTable, A1=0, A2=0, D0=36, D1=0)
        e.w16(0x204F);
        e.w16(0x93C9);                 // SUBA.L A1, A1
        e.w16(0x95CA);                 // SUBA.L A2, A2
        e.w16(0x7024);                 // MOVEQ #36, D0
        e.w16(0x7200);                 // MOVEQ #0, D1
        e.w16(0x4EAE); e.w16(0xFFAC);  // JSR -$54(A6)
        e.w16(0x4FEF); e.w16(28);
        e.w16(0x4A80);
        e.w16(0x6700);
        const size_t patch_beq_done2 = e.pos();
        e.w16(0x0000);

        e.w16(0x2240);                 // MOVEA.L D0, A1
        e.w16(0x137C); e.w16(0x0003); e.w16(0x0008);  // MOVE.B #3, $08(A1)
        e.w16(0x200D);
        e.w16(0x0680); e.w32(static_cast<uint32_t>(off_dev_name));
        e.w16(0x2340); e.w16(0x000A);
        e.w16(0x2340); e.w16(0x0018);
        e.w16(0x4EAE); e.w16(0xFE50);  // AddDevice(A1)

        // Open expansion.library
        e.w16(0x224D);
        e.w16(0xD3FC); e.w32(static_cast<uint32_t>(off_exp_name));
        e.w16(0x4EAE); e.w16(0xFE68);  // OldOpenLibrary
        e.w16(0x4A80);
        e.w16(0x6700);
        const size_t patch_beq_done3 = e.pos();
        e.w16(0x0000);
        e.w16(0x2640);                 // MOVEA.L D0, A3  (A3 = ExpansionBase)

        // Loop D5 = 0 .. D4 - 1
        e.w16(0x7A00);                 // MOVEQ #0, D5
        const size_t loop_top = e.pos();

        // Select unit D5 in MMIO: MOVE.L D5, $F008(A5)
        e.w16(0x204D);
        e.w16(0xD1FC); e.w32(0x0000F008u);
        e.w16(0x2085);                 // MOVE.L D5, (A0)

        // Allocate 104 bytes on stack: 84 bytes parmpkt + 20 bytes dos_name buffer
        e.w16(0x4FEF); e.w16(static_cast<uint16_t>(-104));
        e.w16(0x41EF); e.w16(84);      // LEA 84(A7), A0
        e.w16(0x224D);
        e.w16(0xD3FC); e.w32(0x0000F018u);
        e.w16(0x2288);                 // MOVE.L A0, (A1) (name_target_addr_)

        // Fill entire parmpkt via MMIO $F01C(A5) <- A7
        e.w16(0x204F);                 // MOVEA.L A7, A0
        e.w16(0x224D);
        e.w16(0xD3FC); e.w32(0x0000F01Cu);
        e.w16(0x2288);                 // MOVE.L A0, (A1)

        // D7 = parmpkt[76] (de_BootPri)
        e.w16(0x2E2F); e.w16(76);      // MOVE.L 76(A7), D7

        // Save SysBase (A6), switch A6 = ExpansionBase (A3)
        e.w16(0x2F0E);                 // MOVE.L A6, -(A7)
        e.w16(0x2C4B);                 // MOVEA.L A3, A6
        e.w16(0x41EF); e.w16(4);       // LEA 4(A7), A0
        e.w16(0x4EAE); e.w16(0xFF70);  // JSR -$90(A6)   (MakeDosNode)
        e.w16(0x2C5F);                 // MOVEA.L (A7)+, A6 (restore SysBase)
        e.w16(0x4A80);                 // TST.L D0
        e.w16(0x6700);                 // BEQ.W skip_add
        const size_t patch_skip_add = e.pos();
        e.w16(0x0000);
        e.w16(0x2440);                 // MOVEA.L D0, A2 (save devNode in A2)

        // If unit has RDB filesystem ($F020(A5) != 0), AllocMem + relocate + patch dn_SegList/dn_GlobalVec
        e.w16(0x204D);
        e.w16(0xD1FC); e.w32(0x0000F020u);
        e.w16(0x2010);                 // MOVE.L (A0), D0
        e.w16(0x6700);
        const size_t patch_nofs1 = e.pos();
        e.w16(0x0000);
        e.w16(0x223C); e.w32(0x00010001u);  // MEMF_PUBLIC | MEMF_CLEAR
        e.w16(0x4EAE); e.w16(0xFF3A);  // JSR -$C6(A6) (AllocMem)
        e.w16(0x4A80);
        e.w16(0x6700);
        const size_t patch_nofs2 = e.pos();
        e.w16(0x0000);
        e.w16(0x204D);
        e.w16(0xD1FC); e.w32(0x0000F024u);
        e.w16(0x2080);                 // MOVE.L D0, (A0) -> install_filesystem
        e.w16(0x2240);                 // MOVEA.L D0, A1 (FileSysEntry)
        e.w16(0x2569); e.w16(0x0036); e.w16(0x0020);  // MOVE.L $36(A1), $20(A2) (dn_SegList = fse_SegList)
        e.w16(0x257C); e.w32(0xFFFFFFFFu); e.w16(0x0024);  // MOVE.L #-1, $24(A2) (dn_GlobalVec = -1)

        const size_t nofs_pos = e.pos();
        emit16(rom_, patch_nofs1, static_cast<uint16_t>(nofs_pos - patch_nofs1));
        emit16(rom_, patch_nofs2, static_cast<uint16_t>(nofs_pos - patch_nofs2));

        // Call AddBootNode / AddDosNode via A6 = ExpansionBase (A3)
        e.w16(0x2F0E);                 // MOVE.L A6, -(A7)
        e.w16(0x2C4B);                 // MOVEA.L A3, A6
        e.w16(0x204A);                 // MOVEA.L A2, A0 (devNode)
        e.w16(0x4A85);                 // TST.L D5       (unit == 0?)
        e.w16(0x660C);                 // BNE.S add_dos (+12)
        // AddBootNode(D0=bootPri, D1=0, A0=devNode, A1=configDev)
        e.w16(0x2007);                 // MOVE.L D7, D0
        e.w16(0x7200);                 // MOVEQ #0, D1
        e.w16(0x224C);                 // MOVEA.L A4, A1
        e.w16(0x4EAE); e.w16(0xFFDC);  // JSR -$24(A6)
        e.w16(0x6008);                 // BRA.S pop_sys
        // add_dos: AddDosNode(D0=bootPri, D1=0, A0=devNode)
        e.w16(0x2007);
        e.w16(0x7200);
        e.w16(0x4EAE); e.w16(0xFF6A);  // JSR -$96(A6)
        e.w16(0x2C5F);                 // MOVEA.L (A7)+, A6

        const size_t skip_add_pos = e.pos();
        emit16(rom_, patch_skip_add, static_cast<uint16_t>(skip_add_pos - patch_skip_add));
        e.w16(0x4FEF); e.w16(104);     // LEA 104(A7), A7

        e.w16(0x5285);                 // ADDQ.L #1, D5
        e.w16(0xB885);                 // CMP.L D5, D4
        const size_t branch_off = e.pos();
        e.w16(0x6E00);                 // BGT.W loop_top
        e.w16(static_cast<uint16_t>(static_cast<int16_t>(loop_top - (branch_off + 2))));

        // CloseLibrary(A1 = A3)
        e.w16(0x224B);
        e.w16(0x4EAE); e.w16(0xFE62);

        const size_t done_pos = e.pos();
        emit16(rom_, patch_beq_done, static_cast<uint16_t>(done_pos - patch_beq_done));
        emit16(rom_, patch_beq_done2, static_cast<uint16_t>(done_pos - patch_beq_done2));
        emit16(rom_, patch_beq_done3, static_cast<uint16_t>(done_pos - patch_beq_done3));

        e.w16(0x4CDF); e.w16(0x7CFC);  // MOVEM.L (A7)+, D2-D7/A2-A6
        e.w16(0x7001);                 // MOVEQ #1, D0
        e.w16(0x4E75);                 // RTS
    }
}

uint8_t HardDriveController::read_autoconfig_byte(uint32_t offset) const {
    auto reg_byte = [](uint8_t idx) -> uint8_t {
        switch (idx) {
            case 0x00: return 0xD1;  // ERT_ZORROII | ERTF_DIAGVALID | 64KB (size=1)
            case 0x02: return 0x01;  // Product ID = 1
            case 0x04: return 0x00;  // Flags
            case 0x08: return 0x07;  // Manufacturer high
            case 0x0A: return 0xDB;  // Manufacturer low ($07DB)
            case 0x14: return 0x01;  // er_InitDiagVec high ($0100)
            case 0x16: return 0x00;  // er_InitDiagVec low
            default:   return 0x00;
        }
    };

    const uint8_t idx = static_cast<uint8_t>((offset >> 1) & 0xFEu);
    const uint8_t val = reg_byte(idx);
    const bool low_nibble = (offset & 2u) != 0;
    const uint8_t nib = low_nibble ? (val & 0x0Fu) : (val >> 4);
    const uint8_t encoded = static_cast<uint8_t>((offset < 4u) ? nib : (~nib & 0x0Fu));
    return static_cast<uint8_t>((encoded << 4) | 0x0Fu);
}

bool HardDriveController::handles_read(uint32_t addr24) const {
    if (units_.empty()) return false;
    if (!configured_ && !shut_up_ && addr24 >= 0xE80000u && addr24 < 0xE90000u) {
        return true;
    }
    if (configured_ && addr24 >= base_addr_ && addr24 < base_addr_ + 0x10000u) {
        return true;
    }
    return false;
}

bool HardDriveController::handles_write(uint32_t addr24) const {
    return handles_read(addr24);
}

uint8_t HardDriveController::read8(uint32_t addr24) {
    if (!configured_ && !shut_up_ && addr24 >= 0xE80000u && addr24 < 0xE90000u) {
        const uint32_t off = addr24 - 0xE80000u;
        if (off < 0x40u && (off & 1u) == 0) {
            return read_autoconfig_byte(off);
        }
        return 0xFF;
    }
    if (configured_ && addr24 >= base_addr_ && addr24 < base_addr_ + 0x10000u) {
        const uint32_t off = addr24 - base_addr_;
        if (off < rom_.size()) {
            return rom_[off];
        }
        if (off >= 0xF000u && off < 0xF030u) {
            uint32_t v = 0;
            const uint32_t reg = off & ~3u;
            if (reg == 0xF000u) {
                v = static_cast<uint32_t>(units_.size());
            } else if (selected_unit_ < units_.size()) {
                const auto& u = units_[selected_unit_];
                if (reg == 0xF00Cu) v = u->cylinders;
                else if (reg == 0xF010u) v = u->dostype;
                else if (reg == 0xF014u) v = static_cast<uint32_t>(u->boot_pri);
                else if (reg == 0xF020u) v = u->fs_alloc_size;
            }
            const int shift = static_cast<int>((3u - (off & 3u)) * 8u);
            return static_cast<uint8_t>((v >> shift) & 0xFFu);
        }
    }
    return 0xFF;
}

void HardDriveController::write8(uint32_t addr24, uint8_t val, Bus& /*bus*/) {
    if (!configured_ && !shut_up_ && addr24 >= 0xE80000u && addr24 < 0xE90000u) {
        const uint32_t off = addr24 - 0xE80000u;
        if (off == 0x48u) {
            base_addr_ = static_cast<uint32_t>(val) << 16;
            configured_ = true;
        } else if (off == 0x4Cu) {
            shut_up_ = true;
        }
    }
}

void HardDriveController::write32(uint32_t addr24, uint32_t val, Bus& bus) {
    if (!configured_ || addr24 < base_addr_ || addr24 >= base_addr_ + 0x10000u) return;
    const uint32_t off = addr24 - base_addr_;
    if (off == 0xF004u) {
        handle_begin_io(val, bus);
    } else if (off == 0xF008u) {
        selected_unit_ = val;
    } else if (off == 0xF018u) {
        name_target_addr_ = val;
    } else if (off == 0xF01Cu) {
        if (selected_unit_ < units_.size() && val != 0) {
            fill_parmpkt(*units_[selected_unit_], val, name_target_addr_, bus);
        }
    } else if (off == 0xF024u) {
        if (selected_unit_ < units_.size() && val != 0) {
            install_filesystem(*units_[selected_unit_], val, bus);
        }
    }
}

void HardDriveController::handle_begin_io(uint32_t io_addr, Bus& bus) {
    const uint32_t unit_idx = read_be32_bus(bus, io_addr + 0x18u);
    const uint16_t cmd = bus.read16(io_addr + 0x1Cu);
    const uint32_t length = read_be32_bus(bus, io_addr + 0x24u);
    const uint32_t data_ptr = read_be32_bus(bus, io_addr + 0x28u);
    const uint32_t offset32 = read_be32_bus(bus, io_addr + 0x2Cu);

    if (unit_idx >= units_.size()) {
        bus.write8(io_addr + 0x1Fu, 32);  // TDERR_BadUnitNum
        write_be32_bus(bus, io_addr + 0x20u, 0);
        return;
    }

    auto& u = *units_[unit_idx];
    uint8_t err = 0;
    uint32_t actual = 0;

    switch (cmd) {
        case 2:      // CMD_READ
        case 24:     // TD_READ64
        case 0xC000: // NSCMD_TD_READ64
        {
            uint64_t off64 = offset32;
            if (cmd == 24 || cmd == 0xC000) {
                const uint32_t hi = read_be32_bus(bus, io_addr + 0x20u);
                off64 |= (static_cast<uint64_t>(hi) << 32);
            }
            if (off64 + length <= u.size_bytes) {
                std::vector<uint8_t> buf(length);
                if (read_virtual(u, off64, buf.data(), length)) {
                    for (uint32_t i = 0; i < length; ++i) {
                        bus.write8(data_ptr + i, buf[i]);
                    }
                    actual = length;
                    activity_frames_ = 8;
                } else {
                    err = 20;
                }
            } else {
                err = 20;
            }
            break;
        }
        case 3:      // CMD_WRITE
        case 11:     // TD_FORMAT
        case 25:     // TD_WRITE64
        case 27:     // TD_FORMAT64
        case 0xC001: // NSCMD_TD_WRITE64
        case 0xC002: // NSCMD_TD_FORMAT64
        {
            uint64_t off64 = offset32;
            if (cmd == 25 || cmd == 27 || cmd == 0xC001 || cmd == 0xC002) {
                const uint32_t hi = read_be32_bus(bus, io_addr + 0x20u);
                off64 |= (static_cast<uint64_t>(hi) << 32);
            }
            if (off64 + length <= u.size_bytes) {
                std::vector<uint8_t> buf(length);
                for (uint32_t i = 0; i < length; ++i) {
                    buf[i] = bus.read8(data_ptr + i);
                }
                if (write_virtual(u, off64, buf.data(), length)) {
                    actual = length;
                    activity_frames_ = 8;
                } else {
                    err = 20;
                }
            } else {
                err = 20;
            }
            break;
        }
        case 9:   // TD_MOTOR
        case 10:  // TD_SEEK
        case 13:  // TD_CHANGENUM
        case 14:  // TD_CHANGESTATE (0 = disk present)
        case 15:  // TD_PROTSTATUS (0 = writable)
        case 20:  // TD_ADDCHANGEINT (keep pending; do not ReplyMsg)
            bus.write8(io_addr + 0x1Eu, bus.read8(io_addr + 0x1Eu) | 0x01u);
            actual = 0;
            break;
        case 21:  // TD_REMCHANGEINT
            actual = 0;
            break;
        case 18:  // TD_GETDRIVETYPE
            actual = 1;
            break;
        case 22:  // TD_GETGEOMETRY
            if (length >= 28 && data_ptr != 0) {
                write_be32_bus(bus, data_ptr + 0, 512);
                write_be32_bus(bus, data_ptr + 4, u.cylinders * u.surfaces * u.blocks_per_track);
                write_be32_bus(bus, data_ptr + 8, u.cylinders);
                write_be32_bus(bus, data_ptr + 12, u.surfaces);
                write_be32_bus(bus, data_ptr + 16, u.surfaces * u.blocks_per_track);
                write_be32_bus(bus, data_ptr + 20, u.blocks_per_track);
                write_be32_bus(bus, data_ptr + 24, 0);
                bus.write8(data_ptr + 28, 0);
                bus.write8(data_ptr + 29, 0);
                actual = 30;
            }
            break;
        case 0x4000:  // NSCMD_DEVICEQUERY
            if (length >= 16 && data_ptr != 0) {
                write_be32_bus(bus, data_ptr + 0, 0);
                write_be32_bus(bus, data_ptr + 4, 16);
                bus.write16(data_ptr + 8, 5);   // NSDEVTYPE_TRACKDISK
                bus.write16(data_ptr + 10, 0);
                write_be32_bus(bus, data_ptr + 12, base_addr_ + 0x0540u);
                actual = 16;
            }
            break;
        default:
            err = 0xFD;  // IOERR_NOCMD (-3)
            break;
    }

    bus.write8(io_addr + 0x1Fu, err);
    write_be32_bus(bus, io_addr + 0x20u, actual);
}

}  // namespace amiga
