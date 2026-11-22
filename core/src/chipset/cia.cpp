#include "amiga/cia.hpp"

namespace amiga {

Cia8520::Cia8520(uint8_t pra_reset, uint8_t pra_input_mask)
    : pra_reset_(pra_reset), pra_input_mask_(pra_input_mask) {
    reset();
}

void Cia8520::reset() {
    pra_ = pra_reset_;
    prb_ = 0xFF;
    ddra_ = 0x00;
    ddrb_ = 0x00;
    ta_counter_ = 0xFFFF;
    ta_latch_ = 0xFFFF;
    tb_counter_ = 0xFFFF;
    tb_latch_ = 0xFFFF;
    tod_ = 0;
    tod_alarm_ = 0;
    tod_latch_ = 0;
    tod_latched_ = false;
    tod_running_ = true;
    sdr_ = 0;
    icr_data_ = 0;
    icr_mask_ = 0;
    cra_ = 0;
    crb_ = 0;
}

uint8_t Cia8520::port_a() const {
    return static_cast<uint8_t>((pra_ & ddra_) | (pra_input_mask_ & ~ddra_));
}

uint8_t Cia8520::port_b() const {
    return static_cast<uint8_t>((prb_ & ddrb_) | (0xFFu & ~ddrb_));
}

void Cia8520::raise_icr(uint8_t bits) {
    icr_data_ = static_cast<uint8_t>(icr_data_ | (bits & 0x1Fu));
}

uint8_t Cia8520::read(uint8_t reg) {
    switch (reg & 0x0Fu) {
        case 0x0: return port_a();
        case 0x1: return port_b();
        case 0x2: return ddra_;
        case 0x3: return ddrb_;
        case 0x4: return static_cast<uint8_t>(ta_counter_ & 0xFFu);
        case 0x5: return static_cast<uint8_t>(ta_counter_ >> 8);
        case 0x6: return static_cast<uint8_t>(tb_counter_ & 0xFFu);
        case 0x7: return static_cast<uint8_t>(tb_counter_ >> 8);
        case 0x8: {
            const uint32_t v = tod_latched_ ? tod_latch_ : tod_;
            tod_latched_ = false;
            return static_cast<uint8_t>(v & 0xFFu);
        }
        case 0x9: {
            const uint32_t v = tod_latched_ ? tod_latch_ : tod_;
            return static_cast<uint8_t>((v >> 8) & 0xFFu);
        }
        case 0xA:
            if (!tod_latched_) {
                tod_latch_ = tod_;
                tod_latched_ = true;
            }
            return static_cast<uint8_t>((tod_latch_ >> 16) & 0xFFu);
        case 0xC: return sdr_;
        case 0xD: {
            const uint8_t ret = static_cast<uint8_t>((irq_active() ? 0x80u : 0x00u) | (icr_data_ & 0x1Fu));
            icr_data_ = 0;
            return ret;
        }
        case 0xE: return cra_;
        case 0xF: return crb_;
        default: return 0xFF;
    }
}

void Cia8520::write(uint8_t reg, uint8_t val) {
    switch (reg & 0x0Fu) {
        case 0x0: pra_ = val; break;
        case 0x1: prb_ = val; break;
        case 0x2: ddra_ = val; break;
        case 0x3: ddrb_ = val; break;
        case 0x4: ta_latch_ = static_cast<uint16_t>((ta_latch_ & 0xFF00u) | val); break;
        case 0x5:
            ta_latch_ = static_cast<uint16_t>((ta_latch_ & 0x00FFu) | (static_cast<uint16_t>(val) << 8));
            if ((cra_ & 0x01u) == 0) {
                ta_counter_ = ta_latch_;
            }
            if ((cra_ & 0x08u) != 0) {  // One-shot: writing TAHI loads & starts timer
                ta_counter_ = ta_latch_;
                cra_ |= 0x01u;
            }
            break;
        case 0x6: tb_latch_ = static_cast<uint16_t>((tb_latch_ & 0xFF00u) | val); break;
        case 0x7:
            tb_latch_ = static_cast<uint16_t>((tb_latch_ & 0x00FFu) | (static_cast<uint16_t>(val) << 8));
            if ((crb_ & 0x01u) == 0) {
                tb_counter_ = tb_latch_;
            }
            if ((crb_ & 0x08u) != 0) {  // One-shot: writing TBHI loads & starts timer
                tb_counter_ = tb_latch_;
                crb_ |= 0x01u;
            }
            break;
        case 0x8:
            if ((crb_ & 0x80u) != 0) {
                tod_alarm_ = (tod_alarm_ & 0xFFFF00u) | val;
            } else {
                tod_ = (tod_ & 0xFFFF00u) | val;
                tod_running_ = true;
            }
            break;
        case 0x9:
            if ((crb_ & 0x80u) != 0) {
                tod_alarm_ = (tod_alarm_ & 0xFF00FFu) | (static_cast<uint32_t>(val) << 8);
            } else {
                tod_ = (tod_ & 0xFF00FFu) | (static_cast<uint32_t>(val) << 8);
            }
            break;
        case 0xA:
            if ((crb_ & 0x80u) != 0) {
                tod_alarm_ = (tod_alarm_ & 0x00FFFFu) | (static_cast<uint32_t>(val) << 16);
            } else {
                tod_ = (tod_ & 0x00FFFFu) | (static_cast<uint32_t>(val) << 16);
                tod_running_ = false;
            }
            break;
        case 0xC:
            sdr_ = val;
            raise_icr(0x08u);
            break;
        case 0xD:
            if ((val & 0x80u) != 0) {
                icr_mask_ = static_cast<uint8_t>(icr_mask_ | (val & 0x1Fu));
            } else {
                icr_mask_ = static_cast<uint8_t>(icr_mask_ & ~(val & 0x1Fu));
            }
            break;
        case 0xE:
            cra_ = static_cast<uint8_t>(val & 0xEFu);
            if ((val & 0x10u) != 0) {  // Force load
                ta_counter_ = ta_latch_;
            }
            break;
        case 0xF:
            crb_ = static_cast<uint8_t>(val & 0xEFu);
            if ((val & 0x10u) != 0) {  // Force load
                tb_counter_ = tb_latch_;
            }
            break;
        default: break;
    }
}

void Cia8520::tick_e_clocks(uint32_t e_clocks) {
    if (e_clocks == 0) return;

    auto step_timer = [](uint16_t& counter, uint16_t latch, uint8_t& cr, uint32_t ticks) -> bool {
        if ((cr & 0x01u) == 0) return false;
        if (ticks < counter) {
            counter = static_cast<uint16_t>(counter - ticks);
            return false;
        }
        counter = latch;
        if ((cr & 0x08u) != 0) {  // One-shot mode
            cr = static_cast<uint8_t>(cr & ~0x01u);
        }
        return true;
    };

    if ((cra_ & 0x20u) == 0 && step_timer(ta_counter_, ta_latch_, cra_, e_clocks)) {
        raise_icr(0x01u);
    }
    if ((crb_ & 0x60u) == 0 && step_timer(tb_counter_, tb_latch_, crb_, e_clocks)) {
        raise_icr(0x02u);
    }
}

void Cia8520::tick_tod() {
    if (!tod_running_) return;
    tod_ = (tod_ + 1u) & 0xFFFFFFu;
    if (tod_ == tod_alarm_) {
        raise_icr(0x04u);  // TOD Alarm interrupt
    }
}

}  // namespace amiga
