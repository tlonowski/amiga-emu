#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

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

    /// Enables Centronics parallel-port auto-/ACK on PRB writes (used by CIA-A).
    void set_parallel_handshake(bool enabled) { parallel_handshake_ = enabled; }
    const std::vector<uint8_t>& parallel_tx_buffer() const { return prb_tx_buf_; }
    size_t drain_parallel_tx(uint8_t* dst, size_t max_len);
    void clear_parallel_tx() { prb_tx_buf_.clear(); }

    struct StateSnapshot {
        uint8_t pra = 0;
        uint8_t prb = 0;
        uint8_t ddra = 0;
        uint8_t ddrb = 0;
        uint16_t ta_counter = 0xFFFF;
        uint16_t ta_latch = 0xFFFF;
        uint16_t tb_counter = 0xFFFF;
        uint16_t tb_latch = 0xFFFF;
        uint32_t tod = 0;
        uint32_t tod_alarm = 0;
        uint32_t tod_latch = 0;
        uint8_t tod_latched = 0;
        uint8_t tod_running = 1;
        uint8_t sdr = 0;
        uint8_t icr_data = 0;
        uint8_t icr_mask = 0;
        uint8_t cra = 0;
        uint8_t crb = 0;
        uint32_t ack_pending_e_clocks = 0;
    };

    StateSnapshot capture_state() const {
        StateSnapshot s;
        s.pra = pra_;
        s.prb = prb_;
        s.ddra = ddra_;
        s.ddrb = ddrb_;
        s.ta_counter = ta_counter_;
        s.ta_latch = ta_latch_;
        s.tb_counter = tb_counter_;
        s.tb_latch = tb_latch_;
        s.tod = tod_;
        s.tod_alarm = tod_alarm_;
        s.tod_latch = tod_latch_;
        s.tod_latched = tod_latched_ ? 1 : 0;
        s.tod_running = tod_running_ ? 1 : 0;
        s.sdr = sdr_;
        s.icr_data = icr_data_;
        s.icr_mask = icr_mask_;
        s.cra = cra_;
        s.crb = crb_;
        s.ack_pending_e_clocks = ack_pending_e_clocks_;
        return s;
    }

    void restore_state(const StateSnapshot& s) {
        pra_ = s.pra;
        prb_ = s.prb;
        ddra_ = s.ddra;
        ddrb_ = s.ddrb;
        ta_counter_ = s.ta_counter;
        ta_latch_ = s.ta_latch;
        tb_counter_ = s.tb_counter;
        tb_latch_ = s.tb_latch;
        tod_ = s.tod;
        tod_alarm_ = s.tod_alarm;
        tod_latch_ = s.tod_latch;
        tod_latched_ = s.tod_latched != 0;
        tod_running_ = s.tod_running != 0;
        sdr_ = s.sdr;
        icr_data_ = s.icr_data;
        icr_mask_ = s.icr_mask;
        cra_ = s.cra;
        crb_ = s.crb;
        ack_pending_e_clocks_ = s.ack_pending_e_clocks;
    }

private:
    void raise_icr(uint8_t bits);

    uint8_t pra_reset_ = 0;
    uint8_t pra_input_mask_ = 0;
    bool parallel_handshake_ = false;
    uint32_t ack_pending_e_clocks_ = 0;
    std::vector<uint8_t> prb_tx_buf_;

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
