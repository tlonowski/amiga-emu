#include "amiga/gayle_ide.hpp"

#include <algorithm>
#include <cstring>

namespace amiga {

namespace {
constexpr uint8_t ATA_SR_ERR  = 0x01u;
constexpr uint8_t ATA_SR_DRQ  = 0x08u;
constexpr uint8_t ATA_SR_DSC  = 0x10u;
constexpr uint8_t ATA_SR_DRDY = 0x40u;

void set_ata_word(std::array<uint8_t, 512>& buf, size_t word_idx, uint16_t val) {
    // Gayle maps ATA D0..D15 directly to 68k big-endian 16-bit data words.
    buf[word_idx * 2u] = static_cast<uint8_t>(val >> 8);
    buf[word_idx * 2u + 1u] = static_cast<uint8_t>(val & 0xFFu);
}

void set_ata_string(std::array<uint8_t, 512>& buf, size_t start_word, size_t num_words, const char* s) {
    const size_t len = std::strlen(s);
    for (size_t w = 0; w < num_words; ++w) {
        const size_t i0 = w * 2u;
        const size_t i1 = i0 + 1u;
        const char c0 = (i0 < len) ? s[i0] : ' ';
        const char c1 = (i1 < len) ? s[i1] : ' ';
        set_ata_word(buf, start_word + w, static_cast<uint16_t>((static_cast<uint8_t>(c0) << 8) | static_cast<uint8_t>(c1)));
    }
}
}  // namespace

bool GayleIde::attach_hdf(const std::string& path) {
    detach();
    std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
    if (!f.is_open()) {
        return false;
    }
    f.seekg(0, std::ios::end);
    const std::streamoff sz = f.tellg();
    if (sz < 512) {
        return false;
    }
    file_ = std::move(f);
    path_ = path;
    total_sectors_ = static_cast<uint64_t>(sz) / 512ull;

    geom_heads_ = 16;
    geom_spt_ = 63;
    const uint64_t cyls = std::max<uint64_t>(1ull, total_sectors_ / (static_cast<uint64_t>(geom_heads_) * geom_spt_));
    geom_cyls_ = static_cast<uint16_t>(std::min<uint64_t>(cyls, 16383ull));
    reset();
    return true;
}

void GayleIde::detach() {
    if (file_.is_open()) {
        file_.close();
    }
    path_.clear();
    total_sectors_ = 0;
    reset();
}

void GayleIde::reset() {
    error_ = 0x01u;
    features_ = 0;
    sector_count_ = 1;
    sector_num_ = 1;
    cyl_low_ = 0;
    cyl_high_ = 0;
    drive_head_ = 0xA0u;
    status_ = attached() ? (ATA_SR_DRDY | ATA_SR_DSC) : 0xFFu;
    dev_ctrl_ = 0;
    irq_pending_ = false;
    buf_pos_ = 0;
    sectors_left_ = 0;
    cur_lba_ = 0;
    writing_sector_ = false;
}

uint64_t GayleIde::current_lba() const {
    if ((drive_head_ & 0x40u) != 0u) {
        return (static_cast<uint64_t>(drive_head_ & 0x0Fu) << 24) |
               (static_cast<uint64_t>(cyl_high_) << 16) |
               (static_cast<uint64_t>(cyl_low_) << 8) |
               static_cast<uint64_t>(sector_num_);
    }
    const uint32_t cyl = (static_cast<uint32_t>(cyl_high_) << 8) | cyl_low_;
    const uint32_t head = drive_head_ & 0x0Fu;
    const uint32_t sec = (sector_num_ > 0u) ? (sector_num_ - 1u) : 0u;
    return (static_cast<uint64_t>(cyl) * geom_heads_ + head) * geom_spt_ + sec;
}

void GayleIde::prepare_identify() {
    sector_buf_.fill(0);
    set_ata_word(sector_buf_, 0, 0x0040u);  // Fixed disk
    set_ata_word(sector_buf_, 1, geom_cyls_);
    set_ata_word(sector_buf_, 3, geom_heads_);
    set_ata_word(sector_buf_, 6, geom_spt_);
    set_ata_string(sector_buf_, 10, 10, "AE000001");
    set_ata_string(sector_buf_, 23, 4, "1.0");
    set_ata_string(sector_buf_, 27, 20, "AmigaEmu Gayle IDE");
    set_ata_word(sector_buf_, 47, 0x8001u);  // Max 1 sector per DRQ multiple
    set_ata_word(sector_buf_, 49, 0x0200u);  // LBA supported
    const uint32_t lba28 = static_cast<uint32_t>(std::min<uint64_t>(total_sectors_, 0x0FFFFFFFull));
    set_ata_word(sector_buf_, 60, static_cast<uint16_t>(lba28 & 0xFFFFu));
    set_ata_word(sector_buf_, 61, static_cast<uint16_t>(lba28 >> 16));
    buf_pos_ = 0;
    sectors_left_ = 1;
    writing_sector_ = false;
    status_ = ATA_SR_DRDY | ATA_SR_DSC | ATA_SR_DRQ;
    irq_pending_ = true;
}

bool GayleIde::load_next_sector() {
    if (!attached() || cur_lba_ >= total_sectors_) {
        error_ = 0x10u;  // IDNF
        status_ = ATA_SR_DRDY | ATA_SR_ERR;
        sectors_left_ = 0;
        return false;
    }
    file_.clear();
    file_.seekg(static_cast<std::streamoff>(cur_lba_ * 512ull), std::ios::beg);
    file_.read(reinterpret_cast<char*>(sector_buf_.data()), 512);
    if (!file_) {
        error_ = 0x40u;  // UNC
        status_ = ATA_SR_DRDY | ATA_SR_ERR;
        sectors_left_ = 0;
        return false;
    }
    buf_pos_ = 0;
    writing_sector_ = false;
    status_ = ATA_SR_DRDY | ATA_SR_DSC | ATA_SR_DRQ;
    irq_pending_ = true;
    return true;
}

bool GayleIde::commit_current_sector() {
    if (!attached() || cur_lba_ >= total_sectors_) {
        error_ = 0x10u;
        status_ = ATA_SR_DRDY | ATA_SR_ERR;
        sectors_left_ = 0;
        return false;
    }
    file_.clear();
    file_.seekp(static_cast<std::streamoff>(cur_lba_ * 512ull), std::ios::beg);
    file_.write(reinterpret_cast<const char*>(sector_buf_.data()), 512);
    file_.flush();
    if (!file_) {
        error_ = 0x40u;
        status_ = ATA_SR_DRDY | ATA_SR_ERR;
        sectors_left_ = 0;
        return false;
    }
    return true;
}

void GayleIde::execute_command(uint8_t cmd) {
    if (!attached()) {
        status_ = 0xFFu;
        return;
    }
    if ((drive_head_ & 0x10u) != 0u) {
        // Only primary master (unit 0) is attached.
        error_ = 0x04u;  // ABRT
        status_ = ATA_SR_DRDY | ATA_SR_ERR;
        return;
    }

    error_ = 0;
    switch (cmd) {
        case 0xECu:  // IDENTIFY DEVICE
            prepare_identify();
            break;
        case 0x20u:  // READ SECTORS
        case 0x21u:
        case 0xC4u:  // READ MULTIPLE
            cur_lba_ = current_lba();
            sectors_left_ = (sector_count_ == 0u) ? 256u : sector_count_;
            load_next_sector();
            break;
        case 0x30u:  // WRITE SECTORS
        case 0x31u:
        case 0xC5u:  // WRITE MULTIPLE
            cur_lba_ = current_lba();
            sectors_left_ = (sector_count_ == 0u) ? 256u : sector_count_;
            buf_pos_ = 0;
            writing_sector_ = true;
            status_ = ATA_SR_DRDY | ATA_SR_DSC | ATA_SR_DRQ;
            break;
        case 0x90u:  // EXECUTE DEVICE DIAGNOSTIC
            error_ = 0x01u;
            status_ = ATA_SR_DRDY | ATA_SR_DSC;
            irq_pending_ = true;
            break;
        case 0x91u:  // INITIALIZE DRIVE PARAMETERS
            geom_heads_ = static_cast<uint8_t>((drive_head_ & 0x0Fu) + 1u);
            geom_spt_ = (sector_count_ == 0u) ? 63u : sector_count_;
            status_ = ATA_SR_DRDY | ATA_SR_DSC;
            irq_pending_ = true;
            break;
        case 0x10u:  // RECALIBRATE
        case 0xC6u:  // SET MULTIPLE MODE
        case 0xE7u:  // FLUSH CACHE
            status_ = ATA_SR_DRDY | ATA_SR_DSC;
            irq_pending_ = true;
            break;
        default:
            error_ = 0x04u;  // ABRT
            status_ = ATA_SR_DRDY | ATA_SR_ERR;
            irq_pending_ = true;
            break;
    }
}

uint16_t GayleIde::read16(uint32_t addr24) {
    if (!attached()) return 0xFFFFu;
    if ((addr24 & 0x0FFFu) == 0x003Au) {
        const uint8_t irq = irq_pending_ ? 0x80u : 0x00u;
        return static_cast<uint16_t>((irq << 8) | irq);
    }
    const uint32_t off = addr24 & 0x101Eu;
    if (off == 0x0000u) {
        if ((status_ & ATA_SR_DRQ) == 0u || writing_sector_) {
            return 0xFFFFu;
        }
        const uint16_t word = static_cast<uint16_t>((sector_buf_[buf_pos_] << 8) | sector_buf_[buf_pos_ + 1u]);
        buf_pos_ += 2u;
        if (buf_pos_ >= 512u) {
            if (sectors_left_ > 0u) --sectors_left_;
            if (sectors_left_ > 0u) {
                ++cur_lba_;
                load_next_sector();
            } else {
                status_ = ATA_SR_DRDY | ATA_SR_DSC;
            }
        }
        return word;
    }
    const uint8_t b = read8(addr24);
    return static_cast<uint16_t>((b << 8) | b);
}

uint8_t GayleIde::read8(uint32_t addr24) {
    if ((addr24 & 0x0FFFu) == 0x003Au) {
        return (attached() && irq_pending_) ? 0x80u : 0x00u;  // A4000 IDE IRQ status ($DD203A / $DD303A)
    }
    if (!attached()) return 0xFFu;
    if ((addr24 & 0x101Fu) == 0x1018u) {
        return status_;  // Alternate Status ($DA3018 / $DD3018 / $DD3038, does not clear IRQ)
    }
    const uint32_t reg = (addr24 >> 2) & 0x07u;
    switch (reg) {
        case 0: return static_cast<uint8_t>(read16(addr24) >> 8);
        case 1: return error_;
        case 2: return sector_count_;
        case 3: return sector_num_;
        case 4: return cyl_low_;
        case 5: return cyl_high_;
        case 6: return drive_head_;
        case 7:
            irq_pending_ = false;
            return status_;
        default: return 0xFFu;
    }
}

void GayleIde::write16(uint32_t addr24, uint16_t val) {
    if (!attached()) return;
    if ((addr24 & 0x0FFFu) == 0x003Au) return;
    const uint32_t off = addr24 & 0x101Eu;
    if (off == 0x0000u) {
        if ((status_ & ATA_SR_DRQ) == 0u || !writing_sector_) {
            return;
        }
        sector_buf_[buf_pos_] = static_cast<uint8_t>(val >> 8);
        sector_buf_[buf_pos_ + 1u] = static_cast<uint8_t>(val & 0xFFu);
        buf_pos_ += 2u;
        if (buf_pos_ >= 512u) {
            if (!commit_current_sector()) return;
            if (sectors_left_ > 0u) --sectors_left_;
            if (sectors_left_ > 0u) {
                ++cur_lba_;
                buf_pos_ = 0;
                status_ = ATA_SR_DRDY | ATA_SR_DSC | ATA_SR_DRQ;
                irq_pending_ = true;
            } else {
                writing_sector_ = false;
                status_ = ATA_SR_DRDY | ATA_SR_DSC;
                irq_pending_ = true;
            }
        }
        return;
    }
    write8(addr24, static_cast<uint8_t>(val >> 8));
}

void GayleIde::write8(uint32_t addr24, uint8_t val) {
    if (!attached()) return;
    if ((addr24 & 0x0FFFu) == 0x003Au) return;
    if ((addr24 & 0x101Fu) == 0x1018u) {
        const bool prev_srst = (dev_ctrl_ & 0x04u) != 0u;
        dev_ctrl_ = val;
        if (prev_srst && (val & 0x04u) == 0u) {
            reset();
        }
        return;
    }
    const uint32_t reg = (addr24 >> 2) & 0x07u;
    switch (reg) {
        case 0: write16(addr24, static_cast<uint16_t>(val) << 8); break;
        case 1: features_ = val; break;
        case 2: sector_count_ = val; break;
        case 3: sector_num_ = val; break;
        case 4: cyl_low_ = val; break;
        case 5: cyl_high_ = val; break;
        case 6: drive_head_ = val; break;
        case 7: execute_command(val); break;
        default: break;
    }
}

}  // namespace amiga
