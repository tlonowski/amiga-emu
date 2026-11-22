#include "amiga/amiga_bus.hpp"

#include <algorithm>

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
    const size_t chip_sz = (model == AmigaModel::A1200) ? A1200_CHIP_RAM_SIZE : CHIP_RAM_SIZE;
    const size_t slow_sz = (model == AmigaModel::A1200) ? 0u : SLOW_RAM_SIZE;
    chip_ram_.assign(chip_sz, 0);
    slow_ram_.assign(slow_sz, 0);
    custom_.set_chip_ram(chip_ram_.data(), chip_ram_.size());
    gayle_id_shift_ = 0;
    gayle_intreq_ = 0;
    gayle_intena_ = 0;
    gayle_cfg_ = 0;
}

void AmigaBus::set_fast_ram_mb(uint32_t mb) {
    uint32_t clamped = 0;
    if (mb >= 8u) clamped = 8u;
    else if (mb >= 4u) clamped = 4u;
    else if (mb >= 2u) clamped = 2u;
    fast_ram_.assign(static_cast<size_t>(clamped) * 1024u * 1024u, 0);
    fast_ram_base_ = 0;
    fast_ram_configured_ = false;
    fast_ram_shut_up_ = false;
}

void AmigaBus::reset() {
    std::fill(chip_ram_.begin(), chip_ram_.end(), 0);
    std::fill(slow_ram_.begin(), slow_ram_.end(), 0);
    std::fill(fast_ram_.begin(), fast_ram_.end(), 0);
    fast_ram_base_ = 0;
    fast_ram_configured_ = false;
    fast_ram_shut_up_ = false;
    gayle_id_shift_ = 0;
    gayle_intreq_ = 0;
    gayle_intena_ = 0;
    gayle_cfg_ = 0;
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

uint8_t AmigaBus::read_rom8(uint32_t offset) const {
    return rom_[offset % MAX_ROM_SIZE];
}

uint8_t AmigaBus::read8(uint32_t addr) {
    const uint32_t a = addr & 0x00FFFFFFu;

    if (a < chip_ram_.size()) {
        if (ovl_active() && a < MAX_ROM_SIZE) {
            return read_rom8(a);
        }
        return chip_ram_[a];
    }

    if (fast_ram_configured_ && a >= fast_ram_base_ && a < fast_ram_base_ + fast_ram_.size()) {
        return fast_ram_[a - fast_ram_base_];
    }

    if (!slow_ram_.empty() && a >= 0xC00000u && a < 0xC00000u + slow_ram_.size()) {
        return slow_ram_[a - 0xC00000u];
    }

    if (a >= 0xF80000u) {
        return read_rom8(a - 0xF80000u);
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

    if (fast_ram_autoconfig_active() && a >= 0xE80000u && a < 0xE90000u) {
        return read_fast_autoconfig8(a - 0xE80000u);
    }

    if (hd_ && hd_->handles_read(a)) {
        return hd_->read8(a);
    }

    if (model_ == AmigaModel::A1200 || model_ == AmigaModel::A600) {
        if (a >= 0xDE1000u && a < 0xDE2000u) {
            const uint8_t bit = static_cast<uint8_t>(((0xD0u << (gayle_id_shift_ & 7u)) & 0x80u) ? 0x80u : 0x00u);
            gayle_id_shift_ = static_cast<uint8_t>((gayle_id_shift_ + 1u) & 7u);
            return bit;
        }
        if (a >= 0xDA8000u && a < 0xDAC000u) {
            if (a == 0xDA9000u) return gayle_intreq_;
            if (a == 0xDAA000u) return gayle_intena_;
            if (a == 0xDAB000u) return gayle_cfg_;
            return 0x00;
        }
        if (a >= 0xDA2000u && a < 0xDA4000u) {
            // No IDE drive attached: status without BSY/DRDY.
            return 0xFF;
        }
    }

    return 0xFF;
}

uint16_t AmigaBus::read16(uint32_t addr) {
    const uint32_t a = addr & 0x00FFFFFFu;
    if (a >= 0xDFF000u && a < 0xE00000u) {
        return custom_.read16(static_cast<uint16_t>(a & 0x1FEu));
    }
    const uint16_t hi = read8(a);
    const uint16_t lo = read8(a + 1u);
    return static_cast<uint16_t>((hi << 8) | lo);
}

uint32_t AmigaBus::read32(uint32_t addr) {
    const uint32_t hi = read16(addr);
    const uint32_t lo = read16(addr + 2u);
    return (hi << 16) | lo;
}

void AmigaBus::write8(uint32_t addr, uint8_t val) {
    const uint32_t a = addr & 0x00FFFFFFu;

    if (a < chip_ram_.size()) {
        chip_ram_[a] = val;
        return;
    }

    if (fast_ram_configured_ && a >= fast_ram_base_ && a < fast_ram_base_ + fast_ram_.size()) {
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

    if (fast_ram_autoconfig_active() && a >= 0xE80000u && a < 0xE90000u) {
        const uint32_t off = a - 0xE80000u;
        if (off == 0x48u) {
            fast_ram_base_ = static_cast<uint32_t>(val) << 16;
            fast_ram_configured_ = true;
        } else if (off == 0x4Cu) {
            fast_ram_shut_up_ = true;
        }
        return;
    }

    if (hd_ && hd_->handles_write(a)) {
        hd_->write8(a, val, *this);
        return;
    }

    if (model_ == AmigaModel::A1200 || model_ == AmigaModel::A600) {
        if (a >= 0xDE1000u && a < 0xDE2000u) {
            gayle_id_shift_ = 0;
        } else if (a == 0xDA9000u) {
            gayle_intreq_ = val;
        } else if (a == 0xDAA000u) {
            gayle_intena_ = val;
        } else if (a == 0xDAB000u) {
            gayle_cfg_ = val;
        }
    }
}

void AmigaBus::write16(uint32_t addr, uint16_t val) {
    const uint32_t a = addr & 0x00FFFFFFu;
    if (a >= 0xDFF000u && a < 0xE00000u) {
        custom_.write16(static_cast<uint16_t>(a & 0x1FEu), val);
        return;
    }
    write8(a, static_cast<uint8_t>(val >> 8));
    write8(a + 1u, static_cast<uint8_t>(val & 0xFFu));
}

void AmigaBus::write32(uint32_t addr, uint32_t val) {
    const uint32_t a = addr & 0x00FFFFFFu;
    if (hd_ && hd_->handles_write(a)) {
        hd_->write32(a, val, *this);
        return;
    }
    write16(a, static_cast<uint16_t>(val >> 16));
    write16(a + 2u, static_cast<uint16_t>(val & 0xFFFFu));
}

}  // namespace amiga
