#pragma once

#include "amiga/floppy.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace amiga {

/// Agnus / Denise / Paula custom chip register bank ($DFF000-$DFF1FE) + scanline video renderer.
class CustomChips {
public:
    static constexpr uint16_t PAL_LINES = 312;
    static constexpr uint32_t CPU_CYCLES_PER_LINE = 454;
    static constexpr uint16_t FB_WIDTH = 704;       // PAL overscan (352 lores px: $71..$1D1)
    static constexpr uint16_t VISIBLE_LINES = 284;  // PAL overscan (lines 28..311)
    static constexpr uint16_t FB_HEIGHT = VISIBLE_LINES * 2;  // 568p deinterlaced (flicker-free LACE)
    static constexpr uint16_t VISIBLE_VSTART = 28;  // $1C
    static constexpr uint16_t VISIBLE_HSTART = 0x71; // 113 lores px

    void set_chip_ram(uint8_t* chip_ram, size_t size);
    void set_floppy(FloppyDrive* floppy) { floppies_[0] = floppy; }
    void set_floppy(size_t unit, FloppyDrive* floppy) {
        if (unit < floppies_.size()) floppies_[unit] = floppy;
    }
    void set_floppy_sound_enabled(bool enabled) { floppy_sound_enabled_ = enabled; }
    bool floppy_sound_enabled() const { return floppy_sound_enabled_; }
    void set_joy1(bool up, bool down, bool left, bool right);
    void add_mouse_delta(int32_t dx, int32_t dy);
    void set_mouse_right(bool right);
    void reset();

    uint16_t read16(uint16_t reg_offset);
    void write16(uint16_t reg_offset, uint16_t val);

    /// Advances beam position, Copper, Disk DMA, and scanline rendering. Returns true on frame end.
    bool tick_cpu_cycles(uint32_t cycles, bool cia_a_irq, bool cia_b_irq, bool& out_disk_index);

    /// Computes the current Paula interrupt priority level (0..6) for the 68000.
    uint8_t current_ipl() const;

    /// Returns wait cycles (in 7.09 MHz bus clocks) for a CPU access to Chip RAM / Custom registers.
    uint32_t cpu_chip_wait_cycles(bool is_32bit_access) const;

    uint16_t color(unsigned index) const { return colors_[index & 31u]; }
    uint16_t dmacon() const { return dmacon_; }
    uint16_t intena() const { return intena_; }
    uint16_t intreq() const { return intreq_; }
    uint16_t vpos() const { return vpos_; }
    uint8_t hpos() const { return hpos_; }
    uint16_t bplcon0() const { return bplcon0_; }
    uint16_t fmode() const { return fmode_; }
    uint16_t ddfstrt() const { return ddfstrt_; }
    uint16_t ddfstop() const { return ddfstop_; }
    uint32_t cop1lc() const { return cop1lc_; }
    uint32_t cop2lc() const { return cop2lc_; }
    int16_t bpl1mod() const { return bpl1mod_; }
    int16_t bpl2mod() const { return bpl2mod_; }
    uint32_t blt_desc_count() const { return blt_desc_count_; }
    uint32_t blt_fill_count() const { return blt_fill_count_; }
    uint32_t blt_line_count() const { return blt_line_count_; }

    // Paula Serial UART (SERDAT / SERPER / SERDATR)
    uint16_t serper() const { return serper_; }
    uint16_t serdat() const { return serdat_; }
    const std::vector<uint8_t>& serial_tx_buffer() const { return serial_tx_buf_; }
    size_t drain_serial_tx(uint8_t* dst, size_t max_len);
    void clear_serial_tx() { serial_tx_buf_.clear(); }
    void receive_serial_byte(uint8_t val);

    /// BGRA8 (0xAARRGGBB little-endian = B,G,R,A bytes) framebuffer of size FB_WIDTH * FB_HEIGHT.
    const uint32_t* framebuffer() const { return framebuffer_.data(); }

    /// Reads up to `max_frames` interleaved stereo float samples (48 kHz) from the audio ring buffer.
    size_t read_audio(float* out_stereo, size_t max_frames);

    void set_led_filter(bool enabled) { led_filter_ = enabled; }
    void set_ecs(bool enabled) { ecs_enabled_ = enabled; }
    bool is_ecs() const { return ecs_enabled_; }
    void set_aga(bool enabled) {
        aga_enabled_ = enabled;
        sync_palette_snapshot();
    }
    bool is_aga() const { return aga_enabled_; }

    struct AudioChannel {
        uint32_t lc = 0;
        uint16_t len = 0;
        uint16_t per = 0;
        uint16_t vol = 0;
        uint32_t pt = 0;
        uint16_t words_left = 0;
        uint16_t sample_pair = 0;
        int32_t per_counter = 0;
        int8_t current_sample = 0;
        uint8_t byte_idx = 0;
        bool dma_running = false;
    };

    struct SpriteState {
        uint32_t pt = 0;
        uint16_t vstart = 0;
        uint16_t vstop = 0;
        uint16_t hstart = 0;  // in hires pixels (pos_x * 2 + ctl_bit0)
        uint8_t hfine = 0;    // AGA SH1:SH0 (SPRxCTL bits 4:3), 70 ns / 35 ns sub-pixel offset
        bool attached = false;
        bool armed = false;
        bool done = false;
    };

    struct StateSnapshot {
        uint32_t dskpt = 0;
        uint16_t dsklen = 0;
        uint16_t dsklen_prev = 0;
        uint16_t dsksync = 0x4489;
        uint16_t dskbytr = 0;
        uint16_t dsk_last_word = 0xAAAA;
        uint8_t dsk_byte_phase = 0;
        uint32_t dsk_byte_cycle_acc = 0;
        uint32_t dsk_wordequal_hold = 0;
        uint8_t dsk_dma_on = 0;
        uint8_t dsk_dma_active = 0;
        uint8_t dsk_write = 0;
        uint8_t dsk_synced = 0;
        uint16_t dmacon = 0;
        uint16_t intena = 0;
        uint16_t intreq = 0;
        uint16_t adkcon = 0;
        uint16_t joy1dat = 0;
        uint8_t mouse_x = 0;
        uint8_t mouse_y = 0;
        uint16_t vpos = 0;
        uint8_t hpos = 0;
        uint32_t line_cycle_acc = 0;
        uint8_t lof = 0;
        uint8_t blt_zero = 1;
        uint32_t blt_busy_cycles = 0;
        uint8_t blt_active = 0;
        uint8_t blt_line_mode = 0;
        uint16_t blt_width = 0;
        uint16_t blt_height = 0;
        uint16_t blt_cur_x = 0;
        uint16_t blt_cur_y = 0;
        uint16_t blt_a_prev = 0;
        uint16_t blt_b_prev = 0;
        uint8_t blt_fill_carry = 0;
        int16_t blt_line_err = 0;
        uint16_t blt_line_bit_x = 0;
        uint16_t blt_line_pat_bit = 0;
        uint8_t blt_line_first = 1;
        uint8_t blt_line_sign = 0;
        uint32_t blt_cycles_per_step = 4;
        uint32_t blt_cycle_acc = 0;
        uint32_t cop1lc = 0;
        uint32_t cop2lc = 0;
        uint32_t cop_pc = 0;
        uint16_t copcon = 0;
        uint8_t cop_waiting = 0;
        int32_t cop_hpos = 0;
        uint16_t cop_wait_v = 0;
        uint16_t cop_wait_h = 0;
        uint16_t cop_mask_v = 0x7Fu;
        uint16_t cop_mask_h = 0x7Fu;
        uint16_t diwstrt = 0x2C81;
        uint16_t diwstop = 0x2CC1;
        uint16_t diwhigh = 0;
        uint8_t diwhigh_valid = 0;
        uint16_t beamcon0 = 0;
        uint16_t ddfstrt = 0x0038;
        uint16_t ddfstop = 0x00D0;
        uint16_t bplcon0 = 0;
        uint16_t bplcon1 = 0;
        uint16_t bplcon2 = 0;
        uint16_t bplcon3 = 0x0C00;
        uint16_t bplcon4 = 0x0011;
        uint16_t fmode = 0;
        uint8_t ecs_enabled = 0;
        uint8_t aga_enabled = 0;
        int16_t bpl1mod = 0;
        int16_t bpl2mod = 0;
        std::array<uint32_t, 8> bplpt{};
        uint16_t bltcon0 = 0;
        uint16_t bltcon1 = 0;
        uint16_t bltsizv = 0;
        uint16_t bltafwm = 0xFFFF;
        uint16_t bltalwm = 0xFFFF;
        uint32_t bltapt = 0;
        uint32_t bltbpt = 0;
        uint32_t bltcpt = 0;
        uint32_t bltdpt = 0;
        int16_t bltamod = 0;
        int16_t bltbmod = 0;
        int16_t bltcmod = 0;
        int16_t bltdmod = 0;
        uint16_t bltadat = 0;
        uint16_t bltbdat = 0;
        uint16_t bltcdat = 0;
        std::array<uint16_t, 32> colors{};
        std::array<uint32_t, 256> colors_aga{};
        std::array<SpriteState, 8> sprites{};
        std::array<AudioChannel, 4> audio{};
        uint8_t led_filter = 0;
        uint16_t serper = 372u;
        uint16_t serdat = 0u;
        uint8_t ser_rx_data = 0u;
        uint8_t ser_rx_ready = 0u;
    };

    StateSnapshot capture_state() const {
        StateSnapshot s;
        s.dskpt = dskpt_;
        s.dsklen = dsklen_;
        s.dsklen_prev = dsklen_prev_;
        s.dsksync = dsksync_;
        s.dskbytr = dskbytr_;
        s.dsk_last_word = dsk_last_word_;
        s.dsk_byte_phase = dsk_byte_phase_;
        s.dsk_byte_cycle_acc = dsk_byte_cycle_acc_;
        s.dsk_wordequal_hold = dsk_wordequal_hold_;
        s.dsk_dma_on = dsk_dma_on_ ? 1 : 0;
        s.dsk_dma_active = dsk_dma_active_ ? 1 : 0;
        s.dsk_write = dsk_write_ ? 1 : 0;
        s.dsk_synced = dsk_synced_ ? 1 : 0;
        s.dmacon = dmacon_;
        s.intena = intena_;
        s.intreq = intreq_;
        s.adkcon = adkcon_;
        s.joy1dat = joy1dat_;
        s.mouse_x = mouse_x_;
        s.mouse_y = mouse_y_;
        s.vpos = vpos_;
        s.hpos = hpos_;
        s.line_cycle_acc = line_cycle_acc_;
        s.lof = lof_ ? 1 : 0;
        s.blt_zero = blt_zero_ ? 1 : 0;
        s.blt_busy_cycles = blt_busy_cycles_;
        s.blt_active = blt_active_ ? 1 : 0;
        s.blt_line_mode = blt_line_mode_ ? 1 : 0;
        s.blt_width = blt_width_;
        s.blt_height = blt_height_;
        s.blt_cur_x = blt_cur_x_;
        s.blt_cur_y = blt_cur_y_;
        s.blt_a_prev = blt_a_prev_;
        s.blt_b_prev = blt_b_prev_;
        s.blt_fill_carry = blt_fill_carry_ ? 1 : 0;
        s.blt_line_err = blt_line_err_;
        s.blt_line_bit_x = blt_line_bit_x_;
        s.blt_line_pat_bit = blt_line_pat_bit_;
        s.blt_line_first = blt_line_first_ ? 1 : 0;
        s.blt_line_sign = blt_line_sign_ ? 1 : 0;
        s.blt_cycles_per_step = blt_cycles_per_step_;
        s.blt_cycle_acc = blt_cycle_acc_;
        s.cop1lc = cop1lc_;
        s.cop2lc = cop2lc_;
        s.cop_pc = cop_pc_;
        s.copcon = copcon_;
        s.cop_waiting = cop_waiting_ ? 1 : 0;
        s.cop_hpos = cop_hpos_;
        s.cop_wait_v = cop_wait_v_;
        s.cop_wait_h = cop_wait_h_;
        s.cop_mask_v = cop_mask_v_;
        s.cop_mask_h = cop_mask_h_;
        s.diwstrt = diwstrt_;
        s.diwstop = diwstop_;
        s.diwhigh = diwhigh_;
        s.diwhigh_valid = diwhigh_valid_ ? 1 : 0;
        s.beamcon0 = beamcon0_;
        s.ddfstrt = ddfstrt_;
        s.ddfstop = ddfstop_;
        s.bplcon0 = bplcon0_;
        s.bplcon1 = bplcon1_;
        s.bplcon2 = bplcon2_;
        s.bplcon3 = bplcon3_;
        s.bplcon4 = bplcon4_;
        s.fmode = fmode_;
        s.ecs_enabled = ecs_enabled_ ? 1 : 0;
        s.aga_enabled = aga_enabled_ ? 1 : 0;
        s.bpl1mod = bpl1mod_;
        s.bpl2mod = bpl2mod_;
        s.bplpt = bplpt_;
        s.bltcon0 = bltcon0_;
        s.bltcon1 = bltcon1_;
        s.bltsizv = bltsizv_;
        s.bltafwm = bltafwm_;
        s.bltalwm = bltalwm_;
        s.bltapt = bltapt_;
        s.bltbpt = bltbpt_;
        s.bltcpt = bltcpt_;
        s.bltdpt = bltdpt_;
        s.bltamod = bltamod_;
        s.bltbmod = bltbmod_;
        s.bltcmod = bltcmod_;
        s.bltdmod = bltdmod_;
        s.bltadat = bltadat_;
        s.bltbdat = bltbdat_;
        s.bltcdat = bltcdat_;
        s.colors = colors_;
        s.colors_aga = colors_aga_;
        s.sprites = sprites_;
        s.audio = audio_;
        s.led_filter = led_filter_ ? 1 : 0;
        s.serper = serper_;
        s.serdat = serdat_;
        s.ser_rx_data = ser_rx_data_;
        s.ser_rx_ready = ser_rx_ready_ ? 1 : 0;
        return s;
    }

    void restore_state(const StateSnapshot& s) {
        dskpt_ = s.dskpt;
        dsklen_ = s.dsklen;
        dsklen_prev_ = s.dsklen_prev;
        dsksync_ = s.dsksync;
        dskbytr_ = s.dskbytr;
        dsk_last_word_ = s.dsk_last_word;
        dsk_byte_phase_ = s.dsk_byte_phase;
        dsk_byte_cycle_acc_ = s.dsk_byte_cycle_acc;
        dsk_wordequal_hold_ = s.dsk_wordequal_hold;
        dsk_dma_on_ = s.dsk_dma_on != 0;
        dsk_dma_active_ = s.dsk_dma_active != 0;
        dsk_write_ = s.dsk_write != 0;
        dsk_synced_ = s.dsk_synced != 0;
        dmacon_ = s.dmacon;
        intena_ = s.intena;
        intreq_ = s.intreq;
        adkcon_ = s.adkcon;
        joy1dat_ = s.joy1dat;
        mouse_x_ = s.mouse_x;
        mouse_y_ = s.mouse_y;
        vpos_ = s.vpos;
        hpos_ = s.hpos;
        line_cycle_acc_ = s.line_cycle_acc;
        lof_ = s.lof != 0;
        blt_zero_ = s.blt_zero != 0;
        blt_busy_cycles_ = s.blt_busy_cycles;
        blt_active_ = s.blt_active != 0;
        blt_line_mode_ = s.blt_line_mode != 0;
        blt_width_ = s.blt_width;
        blt_height_ = s.blt_height;
        blt_cur_x_ = s.blt_cur_x;
        blt_cur_y_ = s.blt_cur_y;
        blt_a_prev_ = s.blt_a_prev;
        blt_b_prev_ = s.blt_b_prev;
        blt_fill_carry_ = s.blt_fill_carry != 0;
        blt_line_err_ = s.blt_line_err;
        blt_line_bit_x_ = s.blt_line_bit_x;
        blt_line_pat_bit_ = s.blt_line_pat_bit;
        blt_line_first_ = s.blt_line_first != 0;
        blt_line_sign_ = s.blt_line_sign != 0;
        blt_cycles_per_step_ = s.blt_cycles_per_step;
        blt_cycle_acc_ = s.blt_cycle_acc;
        cop1lc_ = s.cop1lc;
        cop2lc_ = s.cop2lc;
        cop_pc_ = s.cop_pc;
        copcon_ = s.copcon;
        cop_waiting_ = s.cop_waiting != 0;
        cop_hpos_ = s.cop_hpos;
        cop_wait_v_ = s.cop_wait_v;
        cop_wait_h_ = s.cop_wait_h;
        cop_mask_v_ = s.cop_mask_v;
        cop_mask_h_ = s.cop_mask_h;
        diwstrt_ = s.diwstrt;
        diwstop_ = s.diwstop;
        diwhigh_ = s.diwhigh;
        diwhigh_valid_ = s.diwhigh_valid != 0;
        beamcon0_ = s.beamcon0;
        ddfstrt_ = s.ddfstrt;
        ddfstop_ = s.ddfstop;
        bplcon0_ = s.bplcon0;
        bplcon1_ = s.bplcon1;
        bplcon2_ = s.bplcon2;
        bplcon3_ = s.bplcon3;
        bplcon4_ = s.bplcon4;
        fmode_ = s.fmode;
        ecs_enabled_ = s.ecs_enabled != 0;
        aga_enabled_ = s.aga_enabled != 0;
        bpl1mod_ = s.bpl1mod;
        bpl2mod_ = s.bpl2mod;
        bplpt_ = s.bplpt;
        bltcon0_ = s.bltcon0;
        bltcon1_ = s.bltcon1;
        bltsizv_ = s.bltsizv;
        bltafwm_ = s.bltafwm;
        bltalwm_ = s.bltalwm;
        bltapt_ = s.bltapt;
        bltbpt_ = s.bltbpt;
        bltcpt_ = s.bltcpt;
        bltdpt_ = s.bltdpt;
        bltamod_ = s.bltamod;
        bltbmod_ = s.bltbmod;
        bltcmod_ = s.bltcmod;
        bltdmod_ = s.bltdmod;
        bltadat_ = s.bltadat;
        bltbdat_ = s.bltbdat;
        bltcdat_ = s.bltcdat;
        colors_ = s.colors;
        colors_aga_ = s.colors_aga;
        sprites_ = s.sprites;
        audio_ = s.audio;
        led_filter_ = s.led_filter != 0;
        serper_ = s.serper;
        serdat_ = s.serdat;
        ser_rx_data_ = s.ser_rx_data;
        ser_rx_ready_ = s.ser_rx_ready != 0;
        sync_palette_snapshot();
    }

    const std::array<uint32_t, FB_WIDTH * FB_HEIGHT>& framebuffer_array() const { return framebuffer_; }
    std::array<uint32_t, FB_WIDTH * FB_HEIGHT>& framebuffer_array_mut() { return framebuffer_; }

private:

    static uint16_t apply_set_clr(uint16_t current, uint16_t val);
    uint16_t read_chip16(uint32_t addr) const;
    void write_chip16(uint32_t addr, uint16_t val);

    void run_copper_line(uint16_t line);
    void run_disk_line(bool& out_disk_index);
    void tick_audio(uint32_t cpu_cycles);
    void render_scanline(uint16_t line);
    void apply_color_write(unsigned idx, unsigned bank, bool loct, uint16_t val);
    void sync_palette_snapshot() {
        colors_snap_ = colors_;
        colors_aga_snap_ = colors_aga_;
        color_events_.clear();
        color_events_overflow_ = false;
    }
    void run_blitter(uint16_t bltsize);
    void run_blitter_wh(uint16_t width, uint16_t height);
    void step_blitter_once();
    void tick_blitter(uint32_t cpu_cycles);
    void flush_blitter();

    uint8_t* chip_ram_ = nullptr;
    size_t chip_ram_mask_ = 0;
    std::array<FloppyDrive*, 4> floppies_{};
    FloppyDrive* active_floppy() const;

    // Paula Disk DMA state
    uint32_t dskpt_ = 0;
    uint16_t dsklen_ = 0;
    uint16_t dsklen_prev_ = 0;
    uint16_t dsksync_ = 0x4489;
    uint16_t dskbytr_ = 0;
    uint16_t dsk_last_word_ = 0xAAAA;
    uint8_t dsk_byte_phase_ = 0;
    uint32_t dsk_byte_cycle_acc_ = 0;
    uint32_t dsk_wordequal_hold_ = 0;
    bool dsk_dma_on_ = false;
    bool dsk_dma_active_ = false;
    bool dsk_write_ = false;
    bool dsk_synced_ = false;

    uint16_t dmacon_ = 0;
    uint16_t intena_ = 0;
    uint16_t intreq_ = 0;
    uint16_t adkcon_ = 0;
    uint16_t joy1dat_ = 0;
    uint8_t mouse_x_ = 0;
    uint8_t mouse_y_ = 0;
    int32_t pending_mouse_dx_ = 0;
    int32_t pending_mouse_dy_ = 0;
    bool mouse_right_ = false;

    uint16_t vpos_ = 0;
    uint8_t hpos_ = 0;
    uint32_t line_cycle_acc_ = 0;
    bool lof_ = false;
    bool blt_zero_ = true;
    uint32_t blt_busy_cycles_ = 0;
    bool blt_active_ = false;
    bool blt_line_mode_ = false;
    uint16_t blt_width_ = 0;
    uint16_t blt_height_ = 0;
    uint16_t blt_cur_x_ = 0;
    uint16_t blt_cur_y_ = 0;
    uint16_t blt_a_prev_ = 0;
    uint16_t blt_b_prev_ = 0;
    bool blt_fill_carry_ = false;
    int16_t blt_line_err_ = 0;
    uint16_t blt_line_bit_x_ = 0;
    uint16_t blt_line_pat_bit_ = 0;
    bool blt_line_first_ = true;
    bool blt_line_sign_ = false;
    uint32_t blt_cycles_per_step_ = 4;
    uint32_t blt_cycle_acc_ = 0;

    // Copper state
    uint32_t cop1lc_ = 0;
    uint32_t cop2lc_ = 0;
    uint32_t cop_pc_ = 0;
    uint16_t copcon_ = 0;
    bool cop_waiting_ = false;
    int cop_hpos_ = 0;
    uint16_t cop_wait_v_ = 0;
    uint16_t cop_wait_h_ = 0;
    uint16_t cop_mask_v_ = 0x7Fu;
    uint16_t cop_mask_h_ = 0x7Fu;

    // Display / Bitplane state
    uint16_t diwstrt_ = 0x2C81;
    uint16_t diwstop_ = 0x2CC1;
    uint16_t diwhigh_ = 0;
    bool diwhigh_valid_ = false;
    uint16_t beamcon0_ = 0;
    uint16_t ddfstrt_ = 0x0038;
    uint16_t ddfstop_ = 0x00D0;
    uint16_t bplcon0_ = 0;
    uint16_t bplcon1_ = 0;
    uint16_t bplcon2_ = 0;
    uint16_t bplcon3_ = 0x0C00;
    uint16_t bplcon4_ = 0x0011;
    uint16_t fmode_ = 0;
    bool ecs_enabled_ = false;
    bool aga_enabled_ = false;
    int16_t bpl1mod_ = 0;
    int16_t bpl2mod_ = 0;
    std::array<uint32_t, 8> bplpt_{};

    // Blitter state
    uint16_t bltcon0_ = 0;
    uint16_t bltcon1_ = 0;
    uint16_t bltsizv_ = 0;
    uint16_t bltafwm_ = 0xFFFF;
    uint16_t bltalwm_ = 0xFFFF;
    uint32_t bltapt_ = 0;
    uint32_t bltbpt_ = 0;
    uint32_t bltcpt_ = 0;
    uint32_t bltdpt_ = 0;
    int16_t bltamod_ = 0;
    int16_t bltbmod_ = 0;
    int16_t bltcmod_ = 0;
    int16_t bltdmod_ = 0;
    uint16_t bltadat_ = 0;
    uint16_t bltbdat_ = 0;
    uint16_t bltcdat_ = 0;
    uint32_t blt_desc_count_ = 0;
    uint32_t blt_fill_count_ = 0;
    uint32_t blt_line_count_ = 0;

    std::array<uint16_t, 32> colors_{};
    std::array<uint32_t, 256> colors_aga_{};
    std::array<SpriteState, 8> sprites_{};

    // Mid-scanline palette changes made by the Copper (applied lazily while rendering the line)
    struct ColorEvent {
        int32_t x;
        uint16_t idx;
        uint16_t bank;
        bool loct;
        uint16_t val;
    };
    std::vector<ColorEvent> color_events_;
    std::array<uint16_t, 32> colors_snap_{};
    std::array<uint32_t, 256> colors_aga_snap_{};
    bool copper_writing_ = false;
    bool color_events_overflow_ = false;
    int32_t copper_write_x_ = 0;

    std::array<uint32_t, FB_WIDTH * FB_HEIGHT> framebuffer_{};

    // Paula Audio state
    static constexpr size_t AUDIO_RING_FRAMES = 8192;
    std::array<AudioChannel, 4> audio_{};
    std::array<float, AUDIO_RING_FRAMES * 2> audio_ring_{};
    std::atomic<size_t> audio_write_pos_{0};
    std::atomic<size_t> audio_read_pos_{0};
    uint32_t audio_sample_acc_ = 0;
    uint32_t audio_cck_rem_ = 0;
    float filter_l_ = 0.0f;
    float filter_r_ = 0.0f;
    bool led_filter_ = false;
    float floppy_step_env_ = 0.0f;
    float floppy_step_phase_ = 0.0f;
    float floppy_step_freq_ = 165.0f;
    float floppy_step_gain_ = 0.14f;
    float floppy_motor_env_ = 0.0f;
    float floppy_motor_phase_ = 0.0f;
    uint32_t floppy_noise_rng_ = 0x12345678u;
    bool floppy_sound_enabled_ = true;

    // Paula Serial UART state
    uint16_t serper_ = 372u;  // 9600 bps default
    uint16_t serdat_ = 0u;
    uint8_t ser_rx_data_ = 0u;
    bool ser_rx_ready_ = false;
    uint32_t ser_tx_cycles_ = 0u;
    std::vector<uint8_t> serial_tx_buf_;
};

}  // namespace amiga
