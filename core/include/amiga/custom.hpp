#pragma once

#include "amiga/floppy.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

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

    /// BGRA8 (0xAARRGGBB little-endian = B,G,R,A bytes) framebuffer of size FB_WIDTH * FB_HEIGHT.
    const uint32_t* framebuffer() const { return framebuffer_.data(); }

    /// Reads up to `max_frames` interleaved stereo float samples (48 kHz) from the audio ring buffer.
    size_t read_audio(float* out_stereo, size_t max_frames);

    void set_led_filter(bool enabled) { led_filter_ = enabled; }
    void set_aga(bool enabled) { aga_enabled_ = enabled; }
    bool is_aga() const { return aga_enabled_; }

private:
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

    static uint16_t apply_set_clr(uint16_t current, uint16_t val);
    uint16_t read_chip16(uint32_t addr) const;
    void write_chip16(uint32_t addr, uint16_t val);

    void run_copper_line(uint16_t line);
    void run_disk_line(bool& out_disk_index);
    void tick_audio(uint32_t cpu_cycles);
    void render_scanline(uint16_t line);
    void run_blitter(uint16_t bltsize);
    void run_blitter_wh(uint16_t width, uint16_t height);

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
    struct SpriteState {
        uint32_t pt = 0;
        uint16_t vstart = 0;
        uint16_t vstop = 0;
        uint16_t hstart = 0;  // in hires pixels (pos_x * 2 + ctl_bit0)
        bool attached = false;
        bool armed = false;
        bool done = false;
    };
    std::array<SpriteState, 8> sprites_{};

    std::array<uint32_t, FB_WIDTH * FB_HEIGHT> framebuffer_{};

    // Paula Audio state
    static constexpr size_t AUDIO_RING_FRAMES = 8192;
    std::array<AudioChannel, 4> audio_{};
    std::array<float, AUDIO_RING_FRAMES * 2> audio_ring_{};
    std::atomic<size_t> audio_write_pos_{0};
    std::atomic<size_t> audio_read_pos_{0};
    uint32_t audio_sample_acc_ = 0;
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
};

}  // namespace amiga
