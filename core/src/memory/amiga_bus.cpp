#include "amiga/amiga_bus.hpp"

#include <algorithm>
#include <ctime>

namespace amiga {

AmigaBus::AmigaBus(Cia8520& cia_a, Cia8520& cia_b, CustomChips& custom)
    : cia_a_(cia_a),
      cia_b_(cia_b),
      custom_(custom),
      chip_ram_(CHIP_RAM_SIZE, 0),
      slow_ram_(SLOW_RAM_SIZE, 0),
      rom_(MAX_ROM_SIZE, 0xFF) {
    custom_.set_chip_ram(chip_ram_.data(), chip_ram_.size());
}

void AmigaBus::set_model(AmigaModel model) {
    model_ = model;
    const size_t chip_sz = (model == AmigaModel::A1200 || model == AmigaModel::A4000 ||
                            (model == AmigaModel::A600 && a600_chip_2mb_))
                               ? A1200_CHIP_RAM_SIZE
                               : ((model == AmigaModel::A500 && a500_chip_512k_)
                                      ? (512u * 1024u)
                                      : CHIP_RAM_SIZE);
    const size_t slow_sz = (model == AmigaModel::A500) ? SLOW_RAM_SIZE : 0u;
    const size_t mb_fast_sz = (model == AmigaModel::A4000) ? (16u * 1024u * 1024u) : 0u;
    chip_ram_.assign(chip_sz, 0);
    slow_ram_.assign(slow_sz, 0);
    mb_fast_ram_.assign(mb_fast_sz, 0);
    custom_.set_chip_ram(chip_ram_.data(), chip_ram_.size());
    gayle_id_shift_ = 0;
    gayle_status_ = 0;
    gayle_intreq_ = 0;
    gayle_intena_ = 0;
    gayle_cfg_ = 0;
    ramsey_ctrl_ = 0x08u;
    gary_coldstart_ = 0x80u;
    reset_rtc();
}

void AmigaBus::set_a500_chip_ram_512k(bool enabled) {
    a500_chip_512k_ = enabled;
    if (model_ == AmigaModel::A500) {
        const size_t chip_sz = enabled ? (512u * 1024u) : CHIP_RAM_SIZE;
        chip_ram_.assign(chip_sz, 0);
        custom_.set_chip_ram(chip_ram_.data(), chip_ram_.size());
    }
}

void AmigaBus::set_a600_chip_ram_2mb(bool enabled) {
    a600_chip_2mb_ = enabled;
    if (model_ == AmigaModel::A600) {
        const size_t chip_sz = enabled ? A1200_CHIP_RAM_SIZE : CHIP_RAM_SIZE;
        chip_ram_.assign(chip_sz, 0);
        custom_.set_chip_ram(chip_ram_.data(), chip_ram_.size());
    }
}

void AmigaBus::set_fast_ram_mb(uint32_t mb) {
    uint32_t clamped = 0;
    if (model_ == AmigaModel::A4000) {
        if (mb >= 128u) clamped = 128u;
        else if (mb >= 64u) clamped = 64u;
        else if (mb >= 32u) clamped = 32u;
        else if (mb >= 16u) clamped = 16u;
        else if (mb >= 8u) clamped = 8u;
        else if (mb >= 4u) clamped = 4u;
        else if (mb >= 2u) clamped = 2u;
    } else {
        if (mb >= 8u) clamped = 8u;
        else if (mb >= 4u) clamped = 4u;
        else if (mb >= 2u) clamped = 2u;
    }
    fast_ram_.assign(static_cast<size_t>(clamped) * 1024u * 1024u, 0);
    fast_ram_base_ = 0;
    fast_ram_configured_ = false;
    fast_ram_shut_up_ = false;
}

void AmigaBus::reset() {
    std::fill(chip_ram_.begin(), chip_ram_.end(), 0);
    std::fill(slow_ram_.begin(), slow_ram_.end(), 0);
    std::fill(mb_fast_ram_.begin(), mb_fast_ram_.end(), 0);
    std::fill(fast_ram_.begin(), fast_ram_.end(), 0);
    fast_ram_base_ = 0;
    fast_ram_configured_ = false;
    fast_ram_shut_up_ = false;
    gayle_id_shift_ = 0;
    gayle_intreq_ = 0;
    gayle_intena_ = 0;
    gayle_cfg_ = 0;
    ramsey_ctrl_ = 0x08u;
    gary_coldstart_ = 0x80u;
    reset_rtc();
}

void AmigaBus::reset_peripherals() {
    cia_a_.reset();
    cia_b_.reset();
    custom_.reset();
    if (hd_) {
        hd_->reset();
    }
    fast_ram_base_ = 0;
    fast_ram_configured_ = false;
    fast_ram_shut_up_ = false;
    gayle_id_shift_ = 0;
    gayle_intreq_ = 0;
    gayle_intena_ = 0;
    gayle_cfg_ = 0;
    ramsey_ctrl_ = 0x08u;
}

uint8_t AmigaBus::read_fast_autoconfig8(uint32_t offset) const {
    const uint32_t mb = fast_ram_mb();
    const uint8_t size_code = (mb == 8u) ? 0u : ((mb == 4u) ? 7u : 6u);
    const uint8_t er_type = static_cast<uint8_t>(0xE0u | size_code);  // Zorro II + MEMLIST
    uint8_t val = 0x00u;
    switch (offset & 0xFEu) {
        case 0x00: return static_cast<uint8_t>(er_type & 0xF0u);
        case 0x02: return static_cast<uint8_t>((er_type & 0x0Fu) << 4);
        case 0x04: val = 0x01u; break;  // er_Product hi
        case 0x06: val = 0x01u; break;  // er_Product lo
        case 0x10: val = 0x07u; break;  // Manufacturer $07DB (2011)
        case 0x12: val = 0x07u; break;
        case 0x14: val = 0xDBu; break;
        case 0x16: val = 0xDBu; break;
        case 0x24: val = 0x01u; break;  // Serial #1
        case 0x26: val = 0x01u; break;
        default:   val = 0x00u; break;
    }
    const bool is_low_nibble = (offset & 0x02u) != 0;
    const uint8_t nibble = is_low_nibble ? static_cast<uint8_t>((val & 0x0Fu) << 4) : static_cast<uint8_t>(val & 0xF0u);
    return static_cast<uint8_t>((~nibble) & 0xF0u);
}

uint8_t AmigaBus::read_z3_autoconfig8(uint32_t offset) const {
    // Zorro III AutoConfig at $FF000000:
    // Hi nibble at offset + $00, Lo nibble at offset + $100 (for reg = 0x00, 0x04, 0x08, ...)
    const uint32_t mb = fast_ram_mb();
    uint8_t size_code = 0u;  // 16 MB (with ERTF_EXTENDED)
    if (mb >= 128u) size_code = 3u;
    else if (mb >= 64u) size_code = 2u;
    else if (mb >= 32u) size_code = 1u;

    const uint8_t er_type = static_cast<uint8_t>(0xA0u | size_code);  // ERT_ZORROIII (0x80) | ERTF_MEMLIST (0x20)
    const uint8_t reg = static_cast<uint8_t>(offset & 0xFFu);
    const bool is_low = (offset & 0x100u) != 0;

    uint8_t val = 0x00u;
    switch (reg) {
        case 0x00: return is_low ? static_cast<uint8_t>((er_type & 0x0Fu) << 4)
                                 : static_cast<uint8_t>(er_type & 0xF0u);
        case 0x04: val = 0x02u; break;  // er_Product
        case 0x08: val = 0x30u; break;  // er_Flags: ERTF_EXTENDED (0x20) | ERTF_Z3_MEMSIZE (0x10)? Wait: Z3 MEMSPACE=0x20, EXTENDED=0x20? Let's use 0x30 (MEMSPACE | EXTENDED)
        case 0x10: val = 0x07u; break;  // Manufacturer hi ($07DB)
        case 0x14: val = 0xDBu; break;  // Manufacturer lo
        case 0x24: val = 0x02u; break;  // Serial #2
        default:   val = 0x00u; break;
    }
    const uint8_t nibble = is_low ? static_cast<uint8_t>((val & 0x0Fu) << 4)
                                  : static_cast<uint8_t>(val & 0xF0u);
    return static_cast<uint8_t>((~nibble) & 0xF0u);
}

bool AmigaBus::load_rom(const uint8_t* data, size_t size) {
    if (!data || (size != 256 * 1024 && size != 512 * 1024)) {
        return false;
    }
    if (size == 256 * 1024) {
        // Mirror 256 KB Kickstart across both $F80000-$FBFFFF and $FC0000-$FFFFFF.
        std::copy_n(data, size, rom_.begin());
        std::copy_n(data, size, rom_.begin() + size);
    } else {
        std::copy_n(data, size, rom_.begin());
    }
    return true;
}

bool AmigaBus::load_ext_rom(const uint8_t* data, size_t size) {
    if (!data || (size != 256 * 1024 && size != 512 * 1024)) {
        return false;
    }
    ext_rom_.assign(data, data + size);
    return true;
}

void AmigaBus::clear_ext_rom() {
    ext_rom_.clear();
}

uint8_t AmigaBus::read_rom8(uint32_t offset) const {
    return rom_[offset % MAX_ROM_SIZE];
}

uint32_t AmigaBus::take_wait_cycles() {
    const uint32_t w = pending_wait_cycles_;
    pending_wait_cycles_ = 0;
    return w;
}

bool AmigaBus::is_data_cacheable(uint32_t addr) const {
    const uint32_t a = (model_ == AmigaModel::A4000) ? addr : (addr & 0x00FFFFFFu);
    // Fat Gary / Ramsey asserts hardware /CIIN (Cache Inhibit In) for Chip RAM ($000000..$1FFFFF)
    // and I/O + Autoconfig ranges ($BF0000..$EFFFFF, $FF000000..$FF00FFFF) because Agnus/Paula DMA
    // modifies Chip RAM without CPU bus snooping. Fast RAM and ROM are data-cacheable.
    if (a < 0x00200000u) return false;
    if (a >= 0x00BF0000u && a < 0x00F00000u) return false;
    if (a >= 0xFF000000u && a < 0xFF010000u) return false;
    return true;
}

void AmigaBus::account_chip_wait(uint32_t a, bool is_32bit) {
    if (suppress_wait_) return;
    if ((a < chip_ram_.size() && !ovl_active()) || (a >= 0xDFF000u && a < 0xE00000u)) {
        pending_wait_cycles_ += custom_.cpu_chip_wait_cycles(is_32bit);
    }
}

uint8_t AmigaBus::read8(uint32_t addr) {
    const uint32_t a = (model_ == AmigaModel::A4000) ? addr : (addr & 0x00FFFFFFu);
    account_chip_wait(a, false);

    if (a < chip_ram_.size()) {
        if (ovl_active() && a < MAX_ROM_SIZE) {
            return read_rom8(a);
        }
        return chip_ram_[a];
    }

    if (!mb_fast_ram_.empty() && a >= 0x07000000u && a < 0x07000000u + mb_fast_ram_.size()) {
        return mb_fast_ram_[a - 0x07000000u];
    }

    if (fast_ram_mapped() && a >= fast_ram_base_ && a < fast_ram_base_ + fast_ram_.size()) {
        return fast_ram_[a - fast_ram_base_];
    }

    if (!slow_ram_.empty() && a >= 0xC00000u && a < 0xC00000u + slow_ram_.size()) {
        return slow_ram_[a - 0xC00000u];
    }

    if (a >= 0x00F80000u && a <= 0x00FFFFFFu) {
        return read_rom8(a - 0x00F80000u);
    }

    if (!ext_rom_.empty() && a >= 0x00E00000u && a < 0x00E80000u) {
        return ext_rom_[(a - 0x00E00000u) % ext_rom_.size()];
    }

    if (a >= 0xBFE000u && a < 0xBFF000u) {
        return cia_a_.read(static_cast<uint8_t>((a >> 8) & 0x0Fu));
    }

    if (a >= 0xBFD000u && a < 0xBFE000u) {
        return cia_b_.read(static_cast<uint8_t>((a >> 8) & 0x0Fu));
    }

    if (a >= 0xDFF000u && a < 0xE00000u) {
        const uint16_t w = custom_.read16(static_cast<uint16_t>(a & 0x1FEu));
        return static_cast<uint8_t>((a & 1u) ? (w & 0xFFu) : (w >> 8));
    }

    if (fast_ram_autoconfig_active() && !is_zorro3_fast_ram() && a >= 0xE80000u && a < 0xE90000u) {
        return read_fast_autoconfig8(a - 0xE80000u);
    }

    if (fast_ram_autoconfig_active() && is_zorro3_fast_ram() && a >= 0xFF000000u && a < 0xFF010000u) {
        return read_z3_autoconfig8(a - 0xFF000000u);
    }

    if (hd_ && hd_->handles_read(a)) {
        return hd_->read8(a);
    }

    if (a >= 0x00DC0000u && a < 0x00DD0000u) {
        return read_rtc8(a);
    }

    if (model_ == AmigaModel::A4000) {
        if (a == 0x00DE0000u) return gary_coldstart_ & 0x80u;
        if (a == 0x00DE0001u) return gary_toenb_ & 0x80u;
        if (a == 0x00DE0002u) return gary_timeout_ & 0x80u;
        if (a == 0x00DE0003u) return ramsey_ctrl_ & 0x7Fu;
        if (a == 0x00DE0006u || a == 0x00DE0018u) return static_cast<uint8_t>((buster_ctrl_ & 0xF0u) | 0x0Bu);  // Super Buster rev 11
        if (a == 0x00DE0043u) return 0x0Fu;  // Ramsey rev 7 ($0F)
        if (a >= 0x00DE1000u && a < 0x00DE2000u) return 0x00u;  // Fat Gary ID (no Gayle chip)
        if (a >= 0x00DD2000u && a < 0x00DD4000u) {
            if (gayle_ide_ && gayle_ide_->attached()) {
                return gayle_ide_->read8(a);
            }
            if ((a & 0x0FFFu) == 0x003Au) return 0x00u;  // No IDE IRQ pending
            return 0xFF;
        }
    } else if (model_ == AmigaModel::A1200 || model_ == AmigaModel::A600) {
        if (a >= 0xDE1000u && a < 0xDE2000u) {
            const uint8_t bit = static_cast<uint8_t>(((0xD0u << (gayle_id_shift_ & 7u)) & 0x80u) ? 0x80u : 0x00u);
            gayle_id_shift_ = static_cast<uint8_t>((gayle_id_shift_ + 1u) & 7u);
            return bit;
        }
        if (a >= 0xDA8000u && a < 0xDAC000u) {
            const uint8_t ide_irq = (gayle_ide_ && gayle_ide_->irq_pending()) ? 0x80u : 0x00u;
            if (a == 0xDA8000u) return static_cast<uint8_t>((gayle_status_ & 0x03u) | ide_irq);
            if (a == 0xDA9000u) return static_cast<uint8_t>(gayle_intreq_ | ide_irq);
            if (a == 0xDAA000u) return gayle_intena_;
            if (a == 0xDAB000u) return gayle_cfg_;
            return 0x00;
        }
        if (a >= 0xDA2000u && a < 0xDA4000u) {
            if (gayle_ide_ && gayle_ide_->attached()) {
                return gayle_ide_->read8(a);
            }
            return 0xFF;
        }
    }

    return 0xFF;
}

uint16_t AmigaBus::read16(uint32_t addr) {
    const uint32_t a = (model_ == AmigaModel::A4000) ? addr : (addr & 0x00FFFFFFu);
    account_chip_wait(a, false);
    if (a >= 0xDFF000u && a < 0xE00000u) {
        return custom_.read16(static_cast<uint16_t>(a & 0x1FEu));
    }
    if (((model_ == AmigaModel::A1200 || model_ == AmigaModel::A600) &&
         a >= 0xDA2000u && a < 0xDA4000u) ||
        (model_ == AmigaModel::A4000 && a >= 0x00DD2000u && a < 0x00DD4000u)) {
        if (gayle_ide_ && gayle_ide_->attached()) {
            return gayle_ide_->read16(a);
        }
    }
    const bool prev = suppress_wait_;
    suppress_wait_ = true;
    const uint16_t hi = read8(a);
    const uint16_t lo = read8(a + 1u);
    suppress_wait_ = prev;
    return static_cast<uint16_t>((hi << 8) | lo);
}

uint32_t AmigaBus::read32(uint32_t addr) {
    const uint32_t a = (model_ == AmigaModel::A4000) ? addr : (addr & 0x00FFFFFFu);
    account_chip_wait(a, true);
    const bool prev = suppress_wait_;
    suppress_wait_ = true;
    const uint32_t hi = read16(addr);
    const uint32_t lo = read16(addr + 2u);
    suppress_wait_ = prev;
    return (hi << 16) | lo;
}

void AmigaBus::write8(uint32_t addr, uint8_t val) {
    const uint32_t a = (model_ == AmigaModel::A4000) ? addr : (addr & 0x00FFFFFFu);
    account_chip_wait(a, false);

    if (a < chip_ram_.size()) {
        chip_ram_[a] = val;
        return;
    }

    if (!mb_fast_ram_.empty() && a >= 0x07000000u && a < 0x07000000u + mb_fast_ram_.size()) {
        mb_fast_ram_[a - 0x07000000u] = val;
        return;
    }

    if (fast_ram_mapped() && a >= fast_ram_base_ && a < fast_ram_base_ + fast_ram_.size()) {
        fast_ram_[a - fast_ram_base_] = val;
        return;
    }

    if (!slow_ram_.empty() && a >= 0xC00000u && a < 0xC00000u + slow_ram_.size()) {
        slow_ram_[a - 0xC00000u] = val;
        return;
    }

    if (a >= 0xBFE000u && a < 0xBFF000u) {
        cia_a_.write(static_cast<uint8_t>((a >> 8) & 0x0Fu), val);
        return;
    }

    if (a >= 0xBFD000u && a < 0xBFE000u) {
        cia_b_.write(static_cast<uint8_t>((a >> 8) & 0x0Fu), val);
        return;
    }

    if (a >= 0xDFF000u && a < 0xE00000u) {
        custom_.write16(static_cast<uint16_t>(a & 0x1FEu), static_cast<uint16_t>(val));
        return;
    }

    if (fast_ram_autoconfig_active() && !is_zorro3_fast_ram() && a >= 0xE80000u && a < 0xE90000u) {
        const uint32_t off = a - 0xE80000u;
        if (off == 0x48u) {
            fast_ram_base_ = static_cast<uint32_t>(val) << 16;
            fast_ram_configured_ = true;
        } else if (off == 0x4Cu) {
            fast_ram_shut_up_ = true;
        }
        return;
    }

    if (fast_ram_autoconfig_active() && is_zorro3_fast_ram() && a >= 0xFF000000u && a < 0xFF010000u) {
        const uint32_t off = a - 0xFF000000u;
        if (off == 0x44u) {
            fast_ram_base_ = (fast_ram_base_ & 0x00FF0000u) | (static_cast<uint32_t>(val) << 24);
            fast_ram_configured_ = true;
        } else if (off == 0x45u) {
            fast_ram_base_ = (fast_ram_base_ & 0xFF000000u) | (static_cast<uint32_t>(val) << 16);
        } else if (off == 0x4Cu) {
            fast_ram_shut_up_ = true;
        }
        return;
    }

    if (hd_ && hd_->handles_write(a)) {
        hd_->write8(a, val, *this);
        return;
    }

    if (a >= 0x00DC0000u && a < 0x00DD0000u) {
        write_rtc8(a, val);
        return;
    }

    if (model_ == AmigaModel::A4000) {
        if (a == 0x00DE0000u) {
            gary_coldstart_ = val & 0x80u;
        } else if (a == 0x00DE0001u) {
            gary_toenb_ = val & 0x80u;
        } else if (a == 0x00DE0002u) {
            gary_timeout_ = val & 0x80u;
        } else if (a == 0x00DE0003u) {
            ramsey_ctrl_ = val & 0x7Fu;
        } else if (a == 0x00DE0006u || a == 0x00DE0018u) {
            buster_ctrl_ = static_cast<uint8_t>((val & 0xF0u) | 0x0Bu);
        } else if (a >= 0x00DD2000u && a < 0x00DD4000u && gayle_ide_ && gayle_ide_->attached()) {
            gayle_ide_->write8(a, val);
        }
    } else if (model_ == AmigaModel::A1200 || model_ == AmigaModel::A600) {
        if (a >= 0xDE1000u && a < 0xDE2000u) {
            gayle_id_shift_ = 0;
        } else if (a == 0xDA9000u) {
            gayle_intreq_ = val;
        } else if (a == 0xDAA000u) {
            gayle_intena_ = val;
        } else if (a == 0xDAB000u) {
            gayle_cfg_ = val;
        } else if (a >= 0xDA2000u && a < 0xDA4000u && gayle_ide_ && gayle_ide_->attached()) {
            gayle_ide_->write8(a, val);
        }
    }
}

void AmigaBus::write16(uint32_t addr, uint16_t val) {
    const uint32_t a = (model_ == AmigaModel::A4000) ? addr : (addr & 0x00FFFFFFu);
    account_chip_wait(a, false);
    if (a >= 0xDFF000u && a < 0xE00000u) {
        custom_.write16(static_cast<uint16_t>(a & 0x1FEu), val);
        return;
    }
    if (((model_ == AmigaModel::A1200 || model_ == AmigaModel::A600) &&
         a >= 0xDA2000u && a < 0xDA4000u) ||
        (model_ == AmigaModel::A4000 && a >= 0x00DD2000u && a < 0x00DD4000u)) {
        if (gayle_ide_ && gayle_ide_->attached()) {
            gayle_ide_->write16(a, val);
            return;
        }
    }
    if (fast_ram_autoconfig_active() && is_zorro3_fast_ram() && a == 0xFF000044u) {
        fast_ram_base_ = static_cast<uint32_t>(val) << 16;
        fast_ram_configured_ = true;
        return;
    }
    const bool prev = suppress_wait_;
    suppress_wait_ = true;
    write8(a, static_cast<uint8_t>(val >> 8));
    write8(a + 1u, static_cast<uint8_t>(val & 0xFFu));
    suppress_wait_ = prev;
}

void AmigaBus::write32(uint32_t addr, uint32_t val) {
    const uint32_t a = (model_ == AmigaModel::A4000) ? addr : (addr & 0x00FFFFFFu);
    account_chip_wait(a, true);
    if (hd_ && hd_->handles_write(a)) {
        hd_->write32(a, val, *this);
        return;
    }
    const bool prev = suppress_wait_;
    suppress_wait_ = true;
    write16(a, static_cast<uint16_t>(val >> 16));
    write16(a + 2u, static_cast<uint16_t>(val & 0xFFFFu));
    suppress_wait_ = prev;
}

void AmigaBus::reset_rtc() {
    rtc_ctrl_d_ = (model_ == AmigaModel::A4000) ? 0x08u : 0x00u;
    rtc_ctrl_e_ = 0x00u;
    rtc_ctrl_f_ = 0x04u;  // 24-hour mode
    rtc_24h_ = true;
    rtc_bank1_.fill(0);
    rtc_bank1_[0xA] = 0x01u;  // RP5C01 24-hour flag
}

uint8_t AmigaBus::read_rtc8(uint32_t addr) const {
    // Both MSM6242B (A500/A501, A600, A1200) and Ricoh RP5C01 (A4000) expose
    // 16 4-bit registers on the lower data lines (odd addresses $DC0003 + reg*4).
    if ((addr & 1u) == 0u) {
        return 0x00u;
    }
    const uint8_t reg = static_cast<uint8_t>((addr >> 2) & 0x0Fu);

    std::time_t now = std::time(nullptr);
    std::tm tm_val{};
    if (const std::tm* lt = std::localtime(&now)) {
        tm_val = *lt;
    }
    const uint8_t sec = static_cast<uint8_t>(std::clamp(tm_val.tm_sec, 0, 59));
    const uint8_t min = static_cast<uint8_t>(std::clamp(tm_val.tm_min, 0, 59));
    const uint8_t hour = static_cast<uint8_t>(std::clamp(tm_val.tm_hour, 0, 23));
    const uint8_t mday = static_cast<uint8_t>(std::clamp(tm_val.tm_mday, 1, 31));
    const uint8_t mon = static_cast<uint8_t>(std::clamp(tm_val.tm_mon + 1, 1, 12));
    const uint8_t year = static_cast<uint8_t>((tm_val.tm_year + 1900) % 100);
    const uint8_t wday = static_cast<uint8_t>(std::clamp(tm_val.tm_wday, 0, 6));

    uint8_t nib = 0u;
    if (model_ == AmigaModel::A4000) {
        // Ricoh RP5C01 (4 banks selected by Mode register 0xD bits 1:0)
        if (reg == 0xDu) {
            nib = rtc_ctrl_d_ & 0x0Fu;
        } else if (reg == 0xEu) {
            nib = rtc_ctrl_e_ & 0x0Fu;
        } else if (reg == 0xFu) {
            nib = 0x00u;
        } else {
            const uint8_t bank = rtc_ctrl_d_ & 0x03u;
            if (bank == 0u) {
                switch (reg) {
                    case 0x0: nib = sec % 10u; break;
                    case 0x1: nib = sec / 10u; break;
                    case 0x2: nib = min % 10u; break;
                    case 0x3: nib = min / 10u; break;
                    case 0x4: nib = hour % 10u; break;
                    case 0x5: nib = hour / 10u; break;
                    case 0x6: nib = wday; break;
                    case 0x7: nib = mday % 10u; break;
                    case 0x8: nib = mday / 10u; break;
                    case 0x9: nib = mon % 10u; break;
                    case 0xA: nib = mon / 10u; break;
                    case 0xB: nib = year % 10u; break;
                    case 0xC: nib = year / 10u; break;
                    default: break;
                }
            } else if (bank == 1u) {
                if (reg == 0xAu) {
                    nib = rtc_24h_ ? 0x01u : 0x00u;
                } else if (reg == 0xBu) {
                    nib = static_cast<uint8_t>((tm_val.tm_year + 1900) & 3);
                } else {
                    nib = rtc_bank1_[reg] & 0x0Fu;
                }
            } else {
                nib = rtc_nvram_[(bank - 2u) & 1u][reg] & 0x0Fu;
            }
        }
    } else {
        // OKI MSM6242B (A500/A501, A600, A1200)
        switch (reg) {
            case 0x0: nib = sec % 10u; break;
            case 0x1: nib = sec / 10u; break;
            case 0x2: nib = min % 10u; break;
            case 0x3: nib = min / 10u; break;
            case 0x4: nib = hour % 10u; break;
            case 0x5: nib = hour / 10u; break;
            case 0x6: nib = mday % 10u; break;
            case 0x7: nib = mday / 10u; break;
            case 0x8: nib = mon % 10u; break;
            case 0x9: nib = mon / 10u; break;
            case 0xA: nib = year % 10u; break;
            case 0xB: nib = year / 10u; break;
            case 0xC: nib = wday; break;
            case 0xD: nib = rtc_ctrl_d_ & 0x0Du; break;  // bit 1 (BUSY) always 0
            case 0xE: nib = rtc_ctrl_e_ & 0x0Fu; break;
            case 0xF: nib = rtc_ctrl_f_ & 0x0Fu; break;
            default: break;
        }
    }
    return static_cast<uint8_t>(0xF0u | (nib & 0x0Fu));
}

void AmigaBus::write_rtc8(uint32_t addr, uint8_t val) {
    if ((addr & 1u) == 0u) {
        return;
    }
    const uint8_t reg = static_cast<uint8_t>((addr >> 2) & 0x0Fu);
    const uint8_t nib = val & 0x0Fu;

    if (model_ == AmigaModel::A4000) {
        if (reg == 0xDu) {
            rtc_ctrl_d_ = nib;
        } else if (reg == 0xEu) {
            rtc_ctrl_e_ = nib;
        } else if (reg == 0xFu) {
            if ((nib & 0x01u) != 0u) {
                rtc_bank1_.fill(0);
            }
        } else {
            const uint8_t bank = rtc_ctrl_d_ & 0x03u;
            if (bank == 1u) {
                if (reg == 0xAu) {
                    rtc_24h_ = (nib & 0x01u) != 0u;
                }
                rtc_bank1_[reg] = nib;
            } else if (bank >= 2u) {
                rtc_nvram_[(bank - 2u) & 1u][reg] = nib;
            }
        }
    } else {
        if (reg == 0xDu) {
            rtc_ctrl_d_ = nib & 0x0Du;
        } else if (reg == 0xEu) {
            rtc_ctrl_e_ = nib;
        } else if (reg == 0xFu) {
            rtc_ctrl_f_ = nib;
            rtc_24h_ = (nib & 0x04u) != 0u;
        }
    }
}

}  // namespace amiga
