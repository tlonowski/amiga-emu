#pragma once

#include <cstdint>

namespace amiga {

/// MOS 8520 Complex Interface Adapter (CIA-A or CIA-B).
class Cia8520 {
public:
    explicit Cia8520(uint8_t pra_reset = 0x00, uint8_t pra_input_mask = 0x00);

    void reset();
    uint8_t read(uint8_t reg);
    void write(uint8_t reg, uint8_t val);

    /// Advances CIA timers by the given number of E-clock cycles (~709 kHz).
    void tick_e_clocks(uint32_t e_clocks);

    /// Increments the 24-bit Time-Of-Day counter (VSYNC for CIA-A, HSYNC for CIA-B).
    void tick_tod();

    /// Effective output/input state of Port A (e.g. bit 0 of CIA-A is OVL).
    uint8_t port_a() const;
    uint8_t port_b() const;

    bool irq_active() const { return (icr_data_ & icr_mask_ & 0x1Fu) != 0; }
    void set_pra_input_mask(uint8_t mask) { pra_input_mask_ = mask; }
    void raise_flag_irq() { raise_icr(0x10u); }
    void send_serial_byte(uint8_t val) {
        sdr_ = val;
        raise_icr(0x08u);
    }
    bool serial_input_mode() const { return (cra_ & 0x40u) == 0; }
    bool sp_pending() const { return (icr_data_ & 0x08u) != 0; }
    uint8_t icr_mask() const { return icr_mask_; }
    uint8_t icr_data() const { return icr_data_; }
    uint32_t tod() const { return tod_; }
    uint32_t tod_alarm() const { return tod_alarm_; }

private:
    void raise_icr(uint8_t bits);

    uint8_t pra_reset_ = 0;
    uint8_t pra_input_mask_ = 0;

    uint8_t pra_ = 0;
    uint8_t prb_ = 0;
    uint8_t ddra_ = 0;
    uint8_t ddrb_ = 0;

    uint16_t ta_counter_ = 0xFFFF;
    uint16_t ta_latch_ = 0xFFFF;
    uint16_t tb_counter_ = 0xFFFF;
    uint16_t tb_latch_ = 0xFFFF;

    uint32_t tod_ = 0;
    uint32_t tod_alarm_ = 0;
    uint32_t tod_latch_ = 0;
    bool tod_latched_ = false;
    bool tod_running_ = true;

    uint8_t sdr_ = 0;
    uint8_t icr_data_ = 0;
    uint8_t icr_mask_ = 0;
    uint8_t cra_ = 0;
    uint8_t crb_ = 0;
};

}  // namespace amiga
