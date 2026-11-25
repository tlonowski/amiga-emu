#include "amiga/custom.hpp"

#include <algorithm>
#include <functional>

namespace amiga {

namespace {

constexpr uint32_t amiga_rgb12_to_bgra32(uint16_t c12) {
  const uint32_t r4 = (c12 >> 8) & 0xFu;
  const uint32_t g4 = (c12 >> 4) & 0xFu;
  const uint32_t b4 = c12 & 0xFu;
  const uint32_t r8 = (r4 << 4) | r4;
  const uint32_t g8 = (g4 << 4) | g4;
  const uint32_t b8 = (b4 << 4) | b4;
  return 0xFF000000u | (r8 << 16) | (g8 << 8) | b8;
}

} // namespace

void CustomChips::set_chip_ram(uint8_t *chip_ram, size_t size) {
  chip_ram_ = chip_ram;
  chip_ram_mask_ = (size > 0) ? (size - 1u) : 0u;
}

void CustomChips::reset() {
  dmacon_ = 0x0000;
  intena_ = 0x0000;
  intreq_ = 0x0000;
  adkcon_ = 0x0000;
  vpos_ = 0;
  hpos_ = 0;
  line_cycle_acc_ = 0;
  lof_ = true;
  blt_zero_ = true;
  blt_busy_cycles_ = 0;

  dskpt_ = 0;
  dsklen_ = 0;
  dsklen_prev_ = 0;
  dsksync_ = 0x4489;
  dskbytr_ = 0;
  dsk_dma_active_ = false;
  dsk_write_ = false;
  dsk_synced_ = false;

  cop1lc_ = 0;
  cop2lc_ = 0;
  cop_pc_ = 0;
  copcon_ = 0;
  cop_waiting_ = false;
  diwhigh_ = 0;
  diwhigh_valid_ = false;
  beamcon0_ = aga_enabled_ ? 0x0020u : 0x0000u;
  bplcon3_ = 0x0C00;
  bplcon4_ = 0x0011;
  fmode_ = 0;
  colors_.fill(0);
  colors_aga_.fill(0xFF000000u);
  cop_hpos_ = 0;
  mouse_x_ = 0;
  mouse_y_ = 0;
  pending_mouse_dx_ = 0;
  pending_mouse_dy_ = 0;
  mouse_right_ = false;

  diwstrt_ = 0x2C81;
  diwstop_ = 0x2CC1;
  ddfstrt_ = 0x0038;
  ddfstop_ = 0x00D0;
  bplcon0_ = 0;
  bplcon1_ = 0;
  bplcon2_ = 0;
  bpl1mod_ = 0;
  bpl2mod_ = 0;
  bplpt_.fill(0);
  sprites_.fill(SpriteState{});

  bltcon0_ = 0;
  bltcon1_ = 0;
  bltafwm_ = 0xFFFF;
  bltalwm_ = 0xFFFF;
  bltapt_ = bltbpt_ = bltcpt_ = bltdpt_ = 0;
  bltamod_ = bltbmod_ = bltcmod_ = bltdmod_ = 0;
  bltadat_ = bltbdat_ = bltcdat_ = 0;
  blt_active_ = false;
  blt_line_mode_ = false;
  blt_width_ = blt_height_ = 0;
  blt_cur_x_ = blt_cur_y_ = 0;
  blt_a_prev_ = blt_b_prev_ = 0;
  blt_fill_carry_ = false;
  blt_line_err_ = 0;
  blt_line_bit_x_ = blt_line_pat_bit_ = 0;
  blt_line_first_ = true;
  blt_line_sign_ = false;
  blt_cycles_per_step_ = 4;
  blt_cycle_acc_ = 0;
  blt_busy_cycles_ = 0;

  colors_.fill(0);
  framebuffer_.fill(0xFF000000u);

  for (auto &ch : audio_) {
    ch = AudioChannel{};
  }
  audio_ring_.fill(0.0f);
  audio_write_pos_.store(0, std::memory_order_relaxed);
  audio_read_pos_.store(0, std::memory_order_relaxed);
  audio_sample_acc_ = 0;
  audio_cck_rem_ = 0;
  copper_writing_ = false;
  sync_palette_snapshot();
  filter_l_ = 0.0f;
  filter_r_ = 0.0f;
  led_filter_ = false;
  floppy_step_env_ = 0.0f;
  floppy_step_phase_ = 0.0f;
  floppy_step_freq_ = 165.0f;
  floppy_step_gain_ = 0.14f;
  floppy_motor_env_ = 0.0f;
  floppy_motor_phase_ = 0.0f;
  floppy_noise_rng_ = 0x12345678u;
  serper_ = 372u;
  serdat_ = 0u;
  ser_rx_data_ = 0u;
  ser_rx_ready_ = false;
  ser_tx_cycles_ = 0u;
}

uint16_t CustomChips::apply_set_clr(uint16_t current, uint16_t val) {
  const uint16_t bits = val & 0x7FFFu;
  if ((val & 0x8000u) != 0) {
    return static_cast<uint16_t>(current | bits);
  }
  return static_cast<uint16_t>(current & ~bits);
}

uint16_t CustomChips::read_chip16(uint32_t addr) const {
  if (!chip_ram_ || chip_ram_mask_ == 0)
    return 0;
  const uint32_t a = addr & chip_ram_mask_ & ~1u;
  return static_cast<uint16_t>((static_cast<uint16_t>(chip_ram_[a]) << 8) |
                               chip_ram_[a + 1u]);
}

void CustomChips::write_chip16(uint32_t addr, uint16_t val) {
  if (!chip_ram_ || chip_ram_mask_ == 0)
    return;
  const uint32_t a = addr & chip_ram_mask_ & ~1u;
  chip_ram_[a] = static_cast<uint8_t>(val >> 8);
  chip_ram_[a + 1u] = static_cast<uint8_t>(val & 0xFFu);
}

void CustomChips::set_joy1(bool up, bool down, bool left, bool right) {
  uint16_t v = 0;
  if (right)
    v |= 0x0002u;
  if (left)
    v |= 0x0200u;
  if (down != right)
    v |= 0x0001u;
  if (up != left)
    v |= 0x0100u;
  joy1dat_ = v;
}

void CustomChips::add_mouse_delta(int32_t dx, int32_t dy) {
  pending_mouse_dx_ += dx;
  pending_mouse_dy_ += dy;
}

void CustomChips::set_mouse_right(bool right) { mouse_right_ = right; }

uint16_t CustomChips::read16(uint16_t reg_offset) {
  const uint16_t reg = reg_offset & 0x1FEu;
  switch (reg) {
  case 0x002:
    return static_cast<uint16_t>(
        dmacon_ |
        ((blt_active_ || blt_busy_cycles_ > 0) ? 0x4000u : 0x0000u) |
        (blt_zero_ ? 0x2000u : 0x0000u)); // DMACONR
  case 0x004: {                                                    // VPOSR
    const uint16_t lof_bit = lof_ ? 0x8000u : 0x0000u;
    const uint16_t chip_id =
        aga_enabled_ ? 0x2300u
                     : (ecs_enabled_ ? 0x2000u : 0x0000u); // Alice=$23, ECS 2MB Super Agnus=$20
    return static_cast<uint16_t>(lof_bit | chip_id | ((vpos_ >> 8) & 0x0001u));
  }
  case 0x006:
    return static_cast<uint16_t>(((vpos_ & 0xFFu) << 8) | hpos_); // VHPOSR
  case 0x07C:
    return aga_enabled_
               ? 0x00F8u
               : (ecs_enabled_ ? 0x00FCu : 0xFFFFu); // LISAID=$00F8, ECS Super Denise=$00FC
  case 0x00A:
    return static_cast<uint16_t>((static_cast<uint16_t>(mouse_y_) << 8) |
                                 mouse_x_); // JOY0DAT
  case 0x00C:
    return joy1dat_; // JOY1DAT
  case 0x010:
    return adkcon_; // ADKCONR
  case 0x016:
    return mouse_right_ ? 0xFB00u : 0xFF00u; // POTGOR (bit 10 = /RMB)
  case 0x018: {                              // SERDATR
    const uint16_t rbf =
        (ser_rx_ready_ || ((intreq_ & (1u << 11)) != 0u)) ? 0x4000u : 0x0000u;
    const uint16_t tbe_tsre = (ser_tx_cycles_ == 0u) ? 0x3000u : 0x0000u;
    return static_cast<uint16_t>(rbf | tbe_tsre | 0x0B00u | ser_rx_data_);
  }
  case 0x01A:
    return dskbytr_; // DSKBYTR
  case 0x01C:
    return intena_; // INTENAR
  case 0x01E:
    return intreq_; // INTREQR
  case 0x088:       // COPJMP1
    cop_pc_ = cop1lc_;
    cop_waiting_ = false;
    return 0xFFFF;
  case 0x08A: // COPJMP2
    cop_pc_ = cop2lc_;
    cop_waiting_ = false;
    return 0xFFFF;
  default:
    return 0xFFFF;
  }
}

void CustomChips::apply_color_write(unsigned idx, unsigned bank, bool loct, uint16_t val) {
  if (aga_enabled_) {
    const unsigned aga_idx = bank * 32u + idx;
    const uint32_t r4 = (val >> 8) & 0xFu;
    const uint32_t g4 = (val >> 4) & 0xFu;
    const uint32_t b4 = val & 0xFu;
    uint32_t cur = colors_aga_[aga_idx];
    if (loct) {
      cur = (cur & 0xFFF0F0F0u) | (r4 << 16) | (g4 << 8) | b4;
    } else {
      cur = 0xFF000000u | (r4 << 20) | (r4 << 16) | (g4 << 12) | (g4 << 8) |
            (b4 << 4) | b4;
      if (bank == 0) {
        colors_[idx] = val & 0x0FFFu;
      }
    }
    colors_aga_[aga_idx] = cur;
  } else {
    colors_[idx] = val & 0x0FFFu;
  }
}

void CustomChips::write16(uint16_t reg_offset, uint16_t val) {
  const uint16_t reg = reg_offset & 0x1FEu;
  if (reg >= 0x180 && reg <= 0x1BE) {
    const unsigned idx = (reg - 0x180u) >> 1;
    const unsigned bank = (bplcon3_ >> 13) & 0x7u;
    const bool loct = (bplcon3_ & 0x0200u) != 0;
    if (color_events_.size() < 1024u) {
      // Copper writes carry their own beam position; CPU writes land at the
      // position the beam has reached in the line being accumulated.
      const int32_t ex =
          copper_writing_
              ? copper_write_x_
              : (static_cast<int32_t>(line_cycle_acc_) -
                 static_cast<int32_t>(VISIBLE_HSTART)) * 2;
      color_events_.push_back({ex, static_cast<uint16_t>(idx),
                               static_cast<uint16_t>(bank), loct, val});
    } else {
      color_events_overflow_ = true;
    }
    apply_color_write(idx, bank, loct, val);
    return;
  }

  // Paula Audio registers ($0A0..$0DF: 4 channels * 16 bytes)
  if (reg >= 0x0A0 && reg <= 0x0DE) {
    const unsigned ch_idx = (reg - 0x0A0u) >> 4;
    auto &ch = audio_[ch_idx];
    switch (reg & 0x0Eu) {
    case 0x0:
      ch.lc =
          (ch.lc & 0x0000FFFFu) | (static_cast<uint32_t>(val & 0x1Fu) << 16);
      break;
    case 0x2:
      ch.lc = (ch.lc & 0xFFFF0000u) | (val & 0xFFFEu);
      break;
    case 0x4:
      ch.len = val;
      break;
    case 0x6:
      ch.per = val;
      break;
    case 0x8:
      ch.vol = std::min<uint16_t>(val & 0x7Fu, 64u);
      break;
    case 0xA:
      ch.sample_pair = val;
      ch.current_sample = static_cast<int8_t>(val >> 8);
      break;
    default:
      break;
    }
    return;
  }

  // Sprite pointers SPR0PTH..SPR7PTL ($120..$13E)
  if (reg >= 0x120 && reg <= 0x13E) {
    const unsigned idx = (reg - 0x120u) >> 2;
    if ((reg & 2u) == 0) {
      sprites_[idx].pt = (sprites_[idx].pt & 0x0000FFFFu) |
                         (static_cast<uint32_t>(val & 0x1Fu) << 16);
    } else {
      sprites_[idx].pt = (sprites_[idx].pt & 0xFFFF0000u) | (val & 0xFFFEu);
    }
    sprites_[idx].armed = false;
    sprites_[idx].done = false;
    return;
  }

  // Bitplane pointers BPL1PTH..BPL8PTL ($0E0..$0FE)
  if (reg >= 0x0E0 && reg <= 0x0FE) {
    const unsigned plane = (reg - 0x0E0u) >> 2;
    if ((reg & 2u) == 0) {
      bplpt_[plane] = (bplpt_[plane] & 0x0000FFFFu) |
                      (static_cast<uint32_t>(val & 0x1Fu) << 16);
    } else {
      bplpt_[plane] = (bplpt_[plane] & 0xFFFF0000u) | (val & 0xFFFEu);
    }
    return;
  }

  if (reg >= 0x040 && reg <= 0x074 && blt_active_) {
    flush_blitter();
  }

  switch (reg) {
  case 0x020:
    dskpt_ =
        (dskpt_ & 0x0000FFFFu) | (static_cast<uint32_t>(val & 0x1Fu) << 16);
    break;
  case 0x022:
    dskpt_ = (dskpt_ & 0xFFFF0000u) | (val & 0xFFFEu);
    break;
  case 0x024: { // DSKLEN
    const bool dma_en = (val & 0x8000u) != 0;
    if (!dma_en) {
      dsk_dma_active_ = false;
      dsk_write_ = false;
      dsk_synced_ = false;
      dsklen_ = 0;
    } else if (val == dsklen_prev_) {
      dsklen_ = val & 0x3FFFu;
      dsk_write_ = (val & 0x4000u) != 0;
      dsk_dma_active_ = (dsklen_ > 0);
      dsk_synced_ = dsk_write_ || ((adkcon_ & 0x0400u) == 0);
    }
    dsklen_prev_ = val;
    break;
  }
  case 0x02E:
    copcon_ = val;
    break;
  case 0x030: // SERDAT
    serdat_ = val;
    if (val != 0u) {
      if (serial_tx_buf_.size() < 1024u * 1024u) {
        serial_tx_buf_.push_back(static_cast<uint8_t>(val & 0xFFu));
      }
      ser_tx_cycles_ = 32u;
    }
    break;
  case 0x032: // SERPER
    serper_ = val;
    break;
  case 0x040:
    bltcon0_ = val;
    break;
  case 0x042:
    bltcon1_ = val;
    break;
  case 0x044:
    bltafwm_ = val;
    break;
  case 0x046:
    bltalwm_ = val;
    break;
  case 0x048:
    bltcpt_ =
        (bltcpt_ & 0x0000FFFFu) | (static_cast<uint32_t>(val & 0x1Fu) << 16);
    break;
  case 0x04A:
    bltcpt_ = (bltcpt_ & 0xFFFF0000u) | (val & 0xFFFEu);
    break;
  case 0x04C:
    bltbpt_ =
        (bltbpt_ & 0x0000FFFFu) | (static_cast<uint32_t>(val & 0x1Fu) << 16);
    break;
  case 0x04E:
    bltbpt_ = (bltbpt_ & 0xFFFF0000u) | (val & 0xFFFEu);
    break;
  case 0x050:
    bltapt_ =
        (bltapt_ & 0x0000FFFFu) | (static_cast<uint32_t>(val & 0x1Fu) << 16);
    break;
  case 0x052:
    bltapt_ = (bltapt_ & 0xFFFF0000u) | (val & 0xFFFEu);
    break;
  case 0x054:
    bltdpt_ =
        (bltdpt_ & 0x0000FFFFu) | (static_cast<uint32_t>(val & 0x1Fu) << 16);
    break;
  case 0x056:
    bltdpt_ = (bltdpt_ & 0xFFFF0000u) | (val & 0xFFFEu);
    break;
  case 0x058:
    run_blitter(val);
    break;
  case 0x05A:
    bltcon0_ = (bltcon0_ & 0xFF00u) | (val & 0x00FFu);
    break; // BLTCON0L
  case 0x05C:
    bltsizv_ = val & 0x7FFFu;
    break;
  case 0x05E: {
    const uint16_t w = (val & 0x07FFu) ? (val & 0x07FFu) : 2048u;
    const uint16_t h = (bltsizv_ & 0x7FFFu) ? (bltsizv_ & 0x7FFFu) : 32768u;
    run_blitter_wh(w, h);
    break;
  }
  case 0x060:
    bltcmod_ = static_cast<int16_t>(val & 0xFFFEu);
    break;
  case 0x062:
    bltbmod_ = static_cast<int16_t>(val & 0xFFFEu);
    break;
  case 0x064:
    bltamod_ = static_cast<int16_t>(val & 0xFFFEu);
    break;
  case 0x066:
    bltdmod_ = static_cast<int16_t>(val & 0xFFFEu);
    break;
  case 0x070:
    bltcdat_ = val;
    break;
  case 0x072:
    bltbdat_ = val;
    break;
  case 0x074:
    bltadat_ = val;
    break;
  case 0x07E:
    dsksync_ = val;
    break;
  case 0x080:
    cop1lc_ =
        (cop1lc_ & 0x0000FFFFu) | (static_cast<uint32_t>(val & 0x1Fu) << 16);
    break;
  case 0x082:
    cop1lc_ = (cop1lc_ & 0xFFFF0000u) | (val & 0xFFFEu);
    break;
  case 0x084:
    cop2lc_ =
        (cop2lc_ & 0x0000FFFFu) | (static_cast<uint32_t>(val & 0x1Fu) << 16);
    break;
  case 0x086:
    cop2lc_ = (cop2lc_ & 0xFFFF0000u) | (val & 0xFFFEu);
    break;
  case 0x088:
    cop_pc_ = cop1lc_;
    cop_waiting_ = false;
    break;
  case 0x08A:
    cop_pc_ = cop2lc_;
    cop_waiting_ = false;
    break;
  case 0x02A:
    lof_ = (val & 0x8000u) != 0;
    break; // VPOSW
  case 0x08E:
    diwstrt_ = val;
    diwhigh_valid_ = false;
    break;
  case 0x090:
    diwstop_ = val;
    diwhigh_valid_ = false;
    break;
  case 0x092:
    ddfstrt_ = val;
    break;
  case 0x094:
    ddfstop_ = val;
    break;
  case 0x096:
    dmacon_ = apply_set_clr(dmacon_, val);
    break;
  case 0x09A:
    intena_ = apply_set_clr(intena_, val);
    break;
  case 0x09C:
    intreq_ = apply_set_clr(intreq_, val);
    if ((val & 0x8000u) == 0u && (val & (1u << 11)) != 0u) {
      ser_rx_ready_ = false;
    }
    break;
  case 0x09E:
    adkcon_ = apply_set_clr(adkcon_, val);
    break;
  case 0x100:
    bplcon0_ = val;
    break;
  case 0x102:
    bplcon1_ = val;
    break;
  case 0x104:
    bplcon2_ = val;
    break;
  case 0x106:
    bplcon3_ = val;
    break;
  case 0x108:
    bpl1mod_ = static_cast<int16_t>(val & 0xFFFEu);
    break;
  case 0x10A:
    bpl2mod_ = static_cast<int16_t>(val & 0xFFFEu);
    break;
  case 0x10C:
    bplcon4_ = val;
    break;
  case 0x1DC:
    beamcon0_ = val;
    break;
  case 0x1E4:
    diwhigh_ = val;
    diwhigh_valid_ = true;
    break;
  case 0x1FC:
    fmode_ = val;
    break;
  default:
    break;
  }
}

void CustomChips::run_blitter(uint16_t bltsize) {
  const uint16_t height = (bltsize >> 6) ? (bltsize >> 6) : 1024u;
  const uint16_t width = (bltsize & 0x3Fu) ? (bltsize & 0x3Fu) : 64u;
  run_blitter_wh(width, height);
}

void CustomChips::run_blitter_wh(uint16_t width, uint16_t height) {
  if (blt_active_) {
    flush_blitter();
  }

  const bool use_a = (bltcon0_ & 0x0800u) != 0;
  const bool use_b = (bltcon0_ & 0x0400u) != 0;
  const bool use_c = (bltcon0_ & 0x0200u) != 0;
  const bool use_d = (bltcon0_ & 0x0100u) != 0;
  const uint8_t ash = static_cast<uint8_t>((bltcon0_ >> 12) & 0xFu);
  const uint8_t bsh = static_cast<uint8_t>((bltcon1_ >> 12) & 0xFu);
  const bool desc = (bltcon1_ & 0x0002u) != 0;
  const bool line_mode = (bltcon1_ & 0x0001u) != 0;
  const bool fill_ife = !line_mode && (bltcon1_ & 0x0008u) != 0;
  const bool fill_efe = !line_mode && (bltcon1_ & 0x0010u) != 0;
  const bool fill_mode = fill_ife || fill_efe;
  if (desc && !line_mode)
    ++blt_desc_count_;
  if (fill_mode)
    ++blt_fill_count_;
  if (line_mode)
    ++blt_line_count_;

  blt_active_ = true;
  blt_line_mode_ = line_mode;
  blt_width_ = width;
  blt_height_ = height;
  blt_cur_x_ = 0;
  blt_cur_y_ = 0;
  blt_a_prev_ = 0;
  blt_b_prev_ = 0;
  blt_fill_carry_ = (bltcon1_ & 0x0004u) != 0;
  blt_zero_ = true;
  blt_cycle_acc_ = 0;

  if (line_mode) {
    blt_line_err_ = static_cast<int16_t>(bltapt_ & 0xFFFFu);
    blt_line_bit_x_ = ash;
    blt_line_pat_bit_ = bsh;
    blt_line_first_ = true;
    blt_line_sign_ = (bltcon1_ & 0x0040u) != 0;
    blt_cycles_per_step_ = 8u;
    blt_busy_cycles_ =
        std::max<uint32_t>(8u, static_cast<uint32_t>(height) * 8u);
  } else {
    const uint32_t active_ch =
        static_cast<uint32_t>(use_a) + static_cast<uint32_t>(use_b) +
        static_cast<uint32_t>(use_c) + static_cast<uint32_t>(use_d);
    blt_cycles_per_step_ = std::max<uint32_t>(active_ch, 2u) * 2u;
    blt_busy_cycles_ =
        static_cast<uint32_t>(width) * height * blt_cycles_per_step_;
  }
}

void CustomChips::step_blitter_once() {
  if (!blt_active_)
    return;

  const bool use_a = (bltcon0_ & 0x0800u) != 0;
  const bool use_b = (bltcon0_ & 0x0400u) != 0;
  const bool use_c = (bltcon0_ & 0x0200u) != 0;
  const bool use_d = (bltcon0_ & 0x0100u) != 0;
  const uint8_t lf = static_cast<uint8_t>(bltcon0_ & 0xFFu);
  const uint8_t ash = static_cast<uint8_t>((bltcon0_ >> 12) & 0xFu);
  const uint8_t bsh = static_cast<uint8_t>((bltcon1_ >> 12) & 0xFu);
  const bool desc = (bltcon1_ & 0x0002u) != 0;

  if (blt_line_mode_) {
    const bool sing = (bltcon1_ & 0x0002u) != 0;
    const bool aul = (bltcon1_ & 0x0004u) != 0;
    const bool sul = (bltcon1_ & 0x0008u) != 0;
    const bool sud = (bltcon1_ & 0x0010u) != 0;

    auto step_x = [&](bool neg) {
      if (!neg) {
        if (blt_line_bit_x_ == 15u) {
          blt_line_bit_x_ = 0;
          bltcpt_ += 2u;
        } else {
          ++blt_line_bit_x_;
        }
      } else {
        if (blt_line_bit_x_ == 0) {
          blt_line_bit_x_ = 15u;
          bltcpt_ -= 2u;
        } else {
          --blt_line_bit_x_;
        }
      }
    };

    auto step_y = [&](bool neg) {
      bltcpt_ = static_cast<uint32_t>(static_cast<int32_t>(bltcpt_) +
                                      (neg ? -bltcmod_ : bltcmod_));
      blt_line_first_ = true;
    };

    const uint16_t c_word = use_c ? read_chip16(bltcpt_) : bltcdat_;
    const uint16_t mask = static_cast<uint16_t>(0x8000u >> blt_line_bit_x_);
    const uint16_t pat_mask =
        ((bltbdat_ >> blt_line_pat_bit_) & 1u) ? mask : 0u;
    blt_line_pat_bit_ = (blt_line_pat_bit_ + 15u) & 15u;

    uint16_t out = c_word;
    if (!sing || blt_line_first_) {
      uint16_t new_bit = 0;
      const uint8_t idx = static_cast<uint8_t>(
          ((mask != 0) ? 4u : 0u) | ((pat_mask != 0) ? 2u : 0u) |
          (((c_word & mask) != 0) ? 1u : 0u));
      if ((lf & (1u << idx)) != 0) {
        new_bit = mask;
      }
      out = static_cast<uint16_t>((c_word & ~mask) | new_bit);
      blt_line_first_ = false;
    }
    if (out != 0)
      blt_zero_ = false;
    if (use_d || use_c) {
      write_chip16(bltdpt_, out);
    }

    if (sud) {
      step_x(aul);
      if (!blt_line_sign_)
        step_y(sul);
    } else {
      step_y(aul);
      if (!blt_line_sign_)
        step_x(sul);
    }
    bltdpt_ = bltcpt_;

    if (blt_line_sign_) {
      blt_line_err_ = static_cast<int16_t>(blt_line_err_ + bltbmod_);
    } else {
      blt_line_err_ = static_cast<int16_t>(blt_line_err_ + bltamod_);
    }
    blt_line_sign_ = (blt_line_err_ < 0);
    bltapt_ = (bltapt_ & 0xFFFF0000u) | static_cast<uint16_t>(blt_line_err_);

    ++blt_cur_y_;
    if (blt_cur_y_ >= blt_height_) {
      blt_active_ = false;
      blt_busy_cycles_ = 0;
      blt_cycle_acc_ = 0;
      intreq_ |= (1u << 6); // BLIT interrupt
    }
    return;
  }

  const bool fill_ife = (bltcon1_ & 0x0008u) != 0;
  const bool fill_efe = (bltcon1_ & 0x0010u) != 0;
  const bool fill_mode = fill_ife || fill_efe;
  const int32_t step = desc ? -2 : 2;
  const bool fci = (bltcon1_ & 0x0004u) != 0;

  if (blt_cur_x_ == 0) {
    blt_a_prev_ = 0;
    blt_b_prev_ = 0;
    blt_fill_carry_ = fci;
  }

  if (use_a) {
    bltadat_ = read_chip16(bltapt_);
    bltapt_ = static_cast<uint32_t>(static_cast<int32_t>(bltapt_) + step);
  }
  if (use_b) {
    bltbdat_ = read_chip16(bltbpt_);
    bltbpt_ = static_cast<uint32_t>(static_cast<int32_t>(bltbpt_) + step);
  }
  if (use_c) {
    bltcdat_ = read_chip16(bltcpt_);
    bltcpt_ = static_cast<uint32_t>(static_cast<int32_t>(bltcpt_) + step);
  }

  uint16_t a_masked = bltadat_;
  if (blt_cur_x_ == 0)
    a_masked &= bltafwm_;
  if (blt_cur_x_ == blt_width_ - 1u)
    a_masked &= bltalwm_;

  const uint16_t a_val =
      desc ? static_cast<uint16_t>((a_masked << ash) |
                                   (ash ? (blt_a_prev_ >> (16u - ash)) : 0u))
           : static_cast<uint16_t>((a_masked >> ash) |
                                   (ash ? (blt_a_prev_ << (16u - ash)) : 0u));
  blt_a_prev_ = a_masked;

  const uint16_t b_val =
      desc ? static_cast<uint16_t>((bltbdat_ << bsh) |
                                   (bsh ? (blt_b_prev_ >> (16u - bsh)) : 0u))
           : static_cast<uint16_t>((bltbdat_ >> bsh) |
                                   (bsh ? (blt_b_prev_ << (16u - bsh)) : 0u));
  blt_b_prev_ = bltbdat_;

  uint16_t out = 0;
  for (int bit = 0; bit < 16; ++bit) {
    const uint8_t idx = static_cast<uint8_t>((((a_val >> bit) & 1u) << 2) |
                                             (((b_val >> bit) & 1u) << 1) |
                                             ((bltcdat_ >> bit) & 1u));
    if ((lf & (1u << idx)) != 0) {
      out |= static_cast<uint16_t>(1u << bit);
    }
  }

  if (fill_mode) {
    uint16_t filled = 0;
    for (int bit = 0; bit < 16; ++bit) {
      const bool bit_in = ((out >> bit) & 1u) != 0;
      const bool bit_out =
          fill_ife ? (bit_in || blt_fill_carry_) : (bit_in != blt_fill_carry_);
      if (bit_out)
        filled |= static_cast<uint16_t>(1u << bit);
      if (bit_in)
        blt_fill_carry_ = !blt_fill_carry_;
    }
    out = filled;
  }

  if (out != 0)
    blt_zero_ = false;
  if (use_d) {
    write_chip16(bltdpt_, out);
    bltdpt_ = static_cast<uint32_t>(static_cast<int32_t>(bltdpt_) + step);
  }

  ++blt_cur_x_;
  if (blt_cur_x_ >= blt_width_) {
    blt_cur_x_ = 0;
    if (use_a)
      bltapt_ = static_cast<uint32_t>(static_cast<int32_t>(bltapt_) +
                                      (desc ? -bltamod_ : bltamod_));
    if (use_b)
      bltbpt_ = static_cast<uint32_t>(static_cast<int32_t>(bltbpt_) +
                                      (desc ? -bltbmod_ : bltbmod_));
    if (use_c)
      bltcpt_ = static_cast<uint32_t>(static_cast<int32_t>(bltcpt_) +
                                      (desc ? -bltcmod_ : bltcmod_));
    if (use_d)
      bltdpt_ = static_cast<uint32_t>(static_cast<int32_t>(bltdpt_) +
                                      (desc ? -bltdmod_ : bltdmod_));
    ++blt_cur_y_;
    if (blt_cur_y_ >= blt_height_) {
      blt_active_ = false;
      blt_busy_cycles_ = 0;
      blt_cycle_acc_ = 0;
      intreq_ |= (1u << 6); // BLIT interrupt
    }
  }
}

void CustomChips::tick_blitter(uint32_t cpu_cycles) {
  if (!blt_active_) {
    if (blt_busy_cycles_ > 0) {
      if (cpu_cycles >= blt_busy_cycles_) {
        blt_busy_cycles_ = 0;
        intreq_ |= (1u << 6);
      } else {
        blt_busy_cycles_ -= cpu_cycles;
      }
    }
    return;
  }
  if ((dmacon_ & 0x0240u) != 0x0240u) { // Requires DMAEN (bit 9) + BLTEN (bit 6)
    return;
  }

  blt_cycle_acc_ += cpu_cycles;
  while (blt_active_ && blt_cycle_acc_ >= blt_cycles_per_step_) {
    blt_cycle_acc_ -= blt_cycles_per_step_;
    if (blt_busy_cycles_ > blt_cycles_per_step_) {
      blt_busy_cycles_ -= blt_cycles_per_step_;
    } else {
      blt_busy_cycles_ = 1u;
    }
    step_blitter_once();
  }
}

void CustomChips::flush_blitter() {
  while (blt_active_) {
    step_blitter_once();
  }
  blt_busy_cycles_ = 0;
  blt_cycle_acc_ = 0;
}

void CustomChips::run_copper_line(uint16_t line) {
  if ((dmacon_ & 0x0280u) != 0x0280u) { // DMAEN (bit 9) + COPEN (bit 7)
    return;
  }

  int hpos = cop_hpos_;
  cop_hpos_ = 0;
  if (hpos >= 227) {
    cop_hpos_ = hpos - 227;
    return;
  }

  if (cop_waiting_) {
    if (cop_wait_v_ == 0xFFFFu) {
      return;
    }
    const uint16_t v_masked = (line & 0xFFu) & cop_mask_v_;
    const uint16_t wv_masked = cop_wait_v_ & cop_mask_v_;
    if (v_masked < wv_masked ||
        (cop_wait_h_ > 0x71u && v_masked == wv_masked)) {
      return;
    }
    if (v_masked == wv_masked) {
      const int target_h = static_cast<int>(cop_wait_h_) * 2;
      if (hpos < target_h) {
        hpos = target_h;
      }
    }
    cop_waiting_ = false;
    hpos += 4;
    if (hpos >= 227 || (cop_wait_v_ == 0xFFu && cop_wait_h_ >= 0x6Eu &&
                        (line & 0xFFu) == 0xFFu)) {
      cop_hpos_ = (hpos >= 227) ? (hpos - 227) : 0;
      return;
    }
  }

  bool copper_started_blit = false;
  for (int guard = 0; guard < 256; ++guard) {
    const uint16_t ir1 = read_chip16(cop_pc_);
    const uint16_t ir2 = read_chip16(cop_pc_ + 2u);

    if ((ir1 & 1u) != 0 && hpos >= 227) {
      cop_hpos_ = hpos - 227;
      return;
    }

    cop_pc_ += 4u;
    hpos += 4;
    if (copper_started_blit && blt_active_) {
      tick_blitter(8u);
    }

    if ((ir1 & 1u) == 0) { // MOVE
      const uint16_t reg = ir1 & 0x01FEu;
      const uint16_t min_reg = (copcon_ & 0x0002u) ? 0x0040u : 0x0080u;
      if (reg < min_reg) {
        cop_waiting_ = true;
        cop_wait_v_ = 0xFFFFu;
        return;
      }
      copper_write_x_ =
          (hpos * 2 - static_cast<int32_t>(VISIBLE_HSTART)) * 2;
      copper_writing_ = true;
      write16(reg, ir2);
      copper_writing_ = false;
      if (reg == 0x058u || reg == 0x05Eu) {
        copper_started_blit = true;
      }
    } else {
      const uint16_t wv = (ir1 >> 8) & 0xFFu;
      const uint16_t wh = (ir1 >> 1) & 0x7Fu;
      const uint16_t mv = ((ir2 >> 8) & 0x7Fu) | 0x80u;
      const uint16_t mh = (ir2 >> 1) & 0x7Fu;

      if ((ir2 & 1u) == 0) { // WAIT
        if ((ir2 & 0x8000u) == 0u && blt_active_ &&
            (dmacon_ & 0x0240u) == 0x0240u) {
          flush_blitter(); // BFD=0: wait for Blitter finished
        }
        if (ir1 == 0xFFFFu && ir2 == 0xFFFEu) {
          if (copper_started_blit && blt_active_ && hpos < 227) {
            tick_blitter(static_cast<uint32_t>(227 - hpos) * 2u);
          }
          cop_waiting_ = true;
          cop_wait_v_ = 0xFFFFu;
          return;
        }
        const uint16_t v_masked = (line & 0xFFu) & mv;
        const uint16_t wv_masked = wv & mv;
        if (v_masked < wv_masked || (wh > 0x71u && v_masked == wv_masked)) {
          if (copper_started_blit && blt_active_ && hpos < 227) {
            tick_blitter(static_cast<uint32_t>(227 - hpos) * 2u);
          }
          cop_waiting_ = true;
          cop_wait_v_ = wv;
          cop_wait_h_ = wh;
          cop_mask_v_ = mv;
          cop_mask_h_ = mh;
          return;
        }
        if (v_masked == wv_masked) {
          const int target_h = static_cast<int>(wh) * 2 + 4;
          if (hpos < target_h) {
            hpos = target_h;
          }
        }
        if (wv == 0xFFu && wh >= 0x6Eu && (line & 0xFFu) == 0xFFu) {
          return;
        }
      } else { // SKIP
        const uint16_t v_masked = (line & 0xFFu) & mv;
        const uint16_t wv_masked = wv & mv;
        const int target_h = static_cast<int>(wh) * 2;
        if (v_masked > wv_masked ||
            (v_masked == wv_masked && wh <= 0x71u && hpos >= target_h)) {
          cop_pc_ += 4u;
        }
      }
    }
  }
}

void CustomChips::render_scanline(uint16_t line) {
  const bool in_fb =
      (line >= VISIBLE_VSTART && line < VISIBLE_VSTART + VISIBLE_LINES);
  const bool lace = (bplcon0_ & 0x0004u) != 0;
  const size_t fb_row = in_fb
                            ? static_cast<size_t>(line - VISIBLE_VSTART) * 2u +
                                  ((lace && !lof_) ? 1u : 0u)
                            : 0u;
  uint32_t *dst = in_fb ? &framebuffer_[fb_row * FB_WIDTH] : nullptr;
  auto mirror_non_lace = [&]() {
    if (!lace && dst) {
      std::copy_n(dst, FB_WIDTH, dst + FB_WIDTH);
    }
  };

  // Copper palette writes made during this line are replayed lazily from the
  // palette as it was at line start, so changes land at the right pixel.
  size_t ev_i = 0;
  if (!color_events_.empty()) {
    if (color_events_overflow_) {
      ev_i = color_events_.size();  // too many writes: keep end-of-line palette
    } else {
      std::stable_sort(color_events_.begin(), color_events_.end(),
                       [](const ColorEvent &a, const ColorEvent &b) { return a.x < b.x; });
      colors_ = colors_snap_;
      colors_aga_ = colors_aga_snap_;
    }
  }
  auto apply_events = [&](int32_t x) {
    while (ev_i < color_events_.size() && color_events_[ev_i].x <= x) {
      const ColorEvent &e = color_events_[ev_i++];
      apply_color_write(e.idx, e.bank, e.loct, e.val);
    }
  };
  struct EventFlush {
    std::function<void()> fn;
    ~EventFlush() { fn(); }
  } event_flush{[&]() { apply_events(INT32_MAX); }};

  const bool has_midline_events =
      !color_events_.empty() && !color_events_overflow_;
  auto rewind_events = [&]() {
    if (has_midline_events) {
      ev_i = 0;
      colors_ = colors_snap_;
      colors_aga_ = colors_aga_snap_;
    }
  };

  const bool brdrblnk = (aga_enabled_ || ecs_enabled_) && ((bplcon3_ & 0x0020u) != 0);
  if (dst) {
    if (brdrblnk) {
      std::fill_n(dst, FB_WIDTH, 0xFF000000u);
    } else if (has_midline_events) {
      rewind_events();
      for (int32_t x = 0; x < static_cast<int32_t>(FB_WIDTH); ++x) {
        apply_events(x);
        dst[x] = aga_enabled_ ? colors_aga_[0]
                              : amiga_rgb12_to_bgra32(colors_[0]);
      }
      rewind_events();
    } else {
      const uint32_t bg =
          aga_enabled_ ? colors_aga_[0] : amiga_rgb12_to_bgra32(colors_[0]);
      std::fill_n(dst, FB_WIDTH, bg);
    }
  }

  const bool bpl_dma = (dmacon_ & 0x0300u) == 0x0300u; // DMAEN + BPLEN
  uint8_t bpu = static_cast<uint8_t>((bplcon0_ >> 12) & 0x7u);
  if (aga_enabled_ && (bplcon0_ & 0x0010u) != 0) {
    bpu |= 8u;
  }
  const uint8_t max_bpu = aga_enabled_ ? 8u : 6u;

  uint16_t v_start = (diwstrt_ >> 8) & 0xFFu;
  uint16_t v_stop = (diwstop_ >> 8) & 0xFFu;
  if ((aga_enabled_ || ecs_enabled_) && diwhigh_valid_) {
    v_start |= static_cast<uint16_t>((diwhigh_ & 0x0007u) << 8);
    v_stop |= static_cast<uint16_t>(((diwhigh_ >> 8) & 0x0007u) << 8);
  } else {
    if ((v_stop & 0x80u) == 0)
      v_stop |= 0x100u;
  }

  if (!bpl_dma || bpu == 0 || bpu > max_bpu || line < v_start ||
      line >= v_stop) {
    mirror_non_lace();
    return;
  }

  const bool shres = (bplcon0_ & 0x0040u) != 0;
  const bool hires = !shres && ((bplcon0_ & 0x8000u) != 0);
  const uint16_t ddf_start = ddfstrt_ & 0x00FCu;
  const uint16_t ddf_stop = ddfstop_ & 0x00FCu;
  const uint16_t ddf_diff =
      (ddf_stop >= ddf_start) ? (ddf_stop - ddf_start) : 0u;
  const uint8_t bpl_fmode =
      aga_enabled_ ? static_cast<uint8_t>(fmode_ & 0x03u) : 0u;
  const uint16_t fetch_words =
      shres
          ? 4u
          : ((bpl_fmode == 3u) ? 4u
                               : ((bpl_fmode != 0u) ? 2u : (hires ? 2u : 1u)));
  const uint16_t fetch_step =
      shres
          ? 8u
          : (hires
                 ? ((bpl_fmode == 3u) ? 16u : 8u)
                 : ((bpl_fmode == 3u) ? 32u : ((bpl_fmode != 0u) ? 16u : 8u)));
  const uint16_t num_blocks =
      static_cast<uint16_t>(((ddf_diff + fetch_step - 1u) / fetch_step) + 1u);
  const uint16_t words_per_line =
      static_cast<uint16_t>(num_blocks * fetch_words);

  const bool bscan2 = aga_enabled_ && ((fmode_ & 0x4000u) != 0u);
  auto line_bpl_mod = [&](uint8_t p) -> int16_t {
    if (bscan2) {
      return ((line & 1u) == (v_start & 1u)) ? bpl1mod_ : bpl2mod_;
    }
    return ((p & 1u) == 0u) ? bpl1mod_ : bpl2mod_;
  };

  if (!dst) {
    for (uint8_t p = 0; p < bpu; ++p) {
      const int16_t mod = line_bpl_mod(p);
      bplpt_[p] =
          static_cast<uint32_t>(static_cast<int32_t>(bplpt_[p]) +
                                static_cast<int32_t>(words_per_line) * 2 + mod);
    }
    return;
  }

  int32_t h_start = static_cast<int32_t>(diwstrt_ & 0xFFu);
  int32_t h_stop = static_cast<int32_t>(diwstop_ & 0xFFu);
  if ((aga_enabled_ || ecs_enabled_) && diwhigh_valid_) {
    h_start |= static_cast<int32_t>((diwhigh_ & 0x0020u) << 3);
    h_stop |= static_cast<int32_t>((diwhigh_ & 0x2000u) >> 5);
  } else {
    h_stop |= 0x100;
  }
  const int32_t clip_x0 = std::clamp<int32_t>(
      (h_start - static_cast<int32_t>(VISIBLE_HSTART)) * 2, 0, FB_WIDTH);
  const int32_t clip_x1 = std::clamp<int32_t>(
      (h_stop - static_cast<int32_t>(VISIBLE_HSTART)) * 2, 0, FB_WIDTH);

  const int32_t ddf_delay =
      shres ? ((bpl_fmode == 3u) ? 17 : ((bpl_fmode != 0u) ? 9 : 5))
            : ((hires && bpl_fmode == 0u) ? 9 : 17);
  const int32_t ddf_h_lores = static_cast<int32_t>(ddf_start * 2u) + ddf_delay;
  const int32_t base_x =
      (ddf_h_lores - static_cast<int32_t>(VISIBLE_HSTART)) * 2;
  int32_t pf1_scroll = static_cast<int32_t>(bplcon1_ & 0x0Fu) * 2;
  int32_t pf2_scroll = static_cast<int32_t>((bplcon1_ >> 4) & 0x0Fu) * 2;
  if (aga_enabled_) {
    pf1_scroll += static_cast<int32_t>((bplcon1_ >> 10) & 0x03u) * 32 +
                  static_cast<int32_t>((bplcon1_ >> 9) & 0x01u);
    pf2_scroll += static_cast<int32_t>((bplcon1_ >> 14) & 0x03u) * 32 +
                  static_cast<int32_t>((bplcon1_ >> 13) & 0x01u);
  }

  const int32_t pf1_h0 = aga_enabled_ ? ((bplcon1_ >> 8) & 1) : 0;   // PF1H0 (35 ns)
  const int32_t pf2_h0 = aga_enabled_ ? ((bplcon1_ >> 12) & 1) : 0;  // PF2H0 (35 ns)

  std::array<uint8_t, FB_WIDTH> line_idx{};
  if (shres) {
    std::array<uint8_t, FB_WIDTH * 2> shres_idx{};
    const int32_t sclip_x0 = clip_x0 * 2;
    const int32_t sclip_x1 = clip_x1 * 2;
    for (uint16_t w = 0; w < words_per_line; ++w) {
      const int32_t word_base = base_x * 2 + static_cast<int32_t>(w) * 16;
      for (uint8_t p = 0; p < bpu; ++p) {
        const uint16_t word = read_chip16(bplpt_[p]);
        bplpt_[p] += 2u;
        if (word == 0)
          continue;
        const uint8_t bit_mask = static_cast<uint8_t>(1u << p);
        const int32_t scroll =
            (((p & 1u) == 0) ? pf1_scroll : pf2_scroll) * 2 +
            (((p & 1u) == 0) ? pf1_h0 : pf2_h0);
        const int32_t px_start = word_base + scroll;
        for (int bit = 15; bit >= 0; --bit) {
          if ((word & (1u << bit)) != 0) {
            const int32_t sx = px_start + (15 - bit);
            if (sx >= sclip_x0 && sx < sclip_x1) {
              shres_idx[static_cast<size_t>(sx)] |= bit_mask;
            }
          }
        }
      }
    }
    const uint8_t bplam =
        aga_enabled_ ? static_cast<uint8_t>((bplcon4_ >> 8) & 0xFFu) : 0u;
    const uint32_t bg_px =
        aga_enabled_ ? colors_aga_[0] : amiga_rgb12_to_bgra32(colors_[0]);
    if (brdrblnk && clip_x1 > clip_x0) {
      std::fill(dst + clip_x0, dst + clip_x1, bg_px);
    }
    auto shres_color = [&](uint8_t idx) -> uint32_t {
      if (idx == 0)
        return bg_px;
      if (aga_enabled_)
        return colors_aga_[static_cast<uint8_t>(idx ^ bplam)];
      return amiga_rgb12_to_bgra32(colors_[idx & 31u]);
    };
    for (int32_t x = clip_x0; x < clip_x1; ++x) {
      apply_events(x);
      const uint8_t i0 = shres_idx[static_cast<size_t>(x * 2)];
      const uint8_t i1 = shres_idx[static_cast<size_t>(x * 2 + 1)];
      line_idx[static_cast<size_t>(x)] = static_cast<uint8_t>(i0 | i1);
      if (i0 == 0 && i1 == 0)
        continue;
      const uint32_t c0 = shres_color(i0);
      if (i0 == i1) {
        dst[x] = c0;
      } else {
        const uint32_t c1 = shres_color(i1);
        dst[x] = 0xFF000000u | (((c0 & 0x00FEFEFEu) + (c1 & 0x00FEFEFEu)) >> 1);
      }
    }
  } else {
    for (uint16_t w = 0; w < words_per_line; ++w) {
      const int32_t word_base =
          base_x + static_cast<int32_t>(w) * (hires ? 16 : 32);
      for (uint8_t p = 0; p < bpu; ++p) {
        const uint16_t word = read_chip16(bplpt_[p]);
        bplpt_[p] += 2u;
        if (word == 0)
          continue;
        const uint8_t bit_mask = static_cast<uint8_t>(1u << p);
        const int32_t scroll = ((p & 1u) == 0) ? pf1_scroll : pf2_scroll;
        const int32_t px_start = word_base + scroll;
        if (hires) {
          for (int bit = 15; bit >= 0; --bit) {
            if ((word & (1u << bit)) != 0) {
              const int32_t x = px_start + (15 - bit);
              if (x >= clip_x0 && x < clip_x1) {
                line_idx[static_cast<size_t>(x)] |= bit_mask;
              }
            }
          }
        } else {
          for (int bit = 15; bit >= 0; --bit) {
            if ((word & (1u << bit)) != 0) {
              const int32_t x = px_start + (15 - bit) * 2;
              if (x >= clip_x0 && x < clip_x1) {
                line_idx[static_cast<size_t>(x)] |= bit_mask;
              }
              if (x + 1 >= clip_x0 && x + 1 < clip_x1) {
                line_idx[static_cast<size_t>(x + 1)] |= bit_mask;
              }
            }
          }
        }
      }
    }
  }

  const bool ham = (bplcon0_ & 0x0800u) != 0;
  const bool ehb = (bpu == 6 && (bplcon0_ & 0x0C00u) == 0);
  const bool dpf = (bplcon0_ & 0x0400u) != 0 && !ham;
  if (shres) {
    // Already rendered with 2x horizontal supersampling above.
  } else if (aga_enabled_) {
    const uint8_t bplam = static_cast<uint8_t>((bplcon4_ >> 8) & 0xFFu);
    const bool pf2pri = (bplcon2_ & 0x0040u) != 0;
    const uint8_t pf2of_bits = static_cast<uint8_t>((bplcon3_ >> 10) & 0x07u);
    static constexpr std::array<uint8_t, 8> kPf2OfTable = {0,  2,  4,  8,
                                                           16, 32, 64, 128};
    const uint8_t pf2_offset =
        (pf2of_bits != 0) ? kPf2OfTable[pf2of_bits] : ((bpu > 6) ? 16u : 8u);
    const uint32_t hold_init = aga_enabled_ ? colors_aga_[0] : 0u;
    if (brdrblnk && clip_x1 > clip_x0) {
      std::fill(dst + clip_x0, dst + clip_x1, hold_init);
    }
    uint32_t hold = hold_init;
    for (int32_t x = clip_x0; x < clip_x1; ++x) {
      apply_events(x);
      const uint8_t color_idx = line_idx[static_cast<size_t>(x)];
      if (ham && bpu == 8) {
        const uint8_t ctrl = (color_idx >> 6) & 0x3u;
        const uint8_t val6 = color_idx & 0x3Fu;
        const uint32_t val8 = static_cast<uint32_t>((val6 << 2) | (val6 >> 4));
        if (ctrl == 0)
          hold = colors_aga_[val6];
        else if (ctrl == 1)
          hold = (hold & 0xFFFFFF00u) | val8;
        else if (ctrl == 2)
          hold = (hold & 0xFF00FFFFu) | (val8 << 16);
        else
          hold = (hold & 0xFFFF00FFu) | (val8 << 8);
        dst[x] = hold;
      } else if (ham && (bpu == 6 || bpu == 5)) {
        const uint8_t ctrl = (color_idx >> 4) & 0x3u;
        const uint8_t val4 = color_idx & 0x0Fu;
        const uint32_t val8 = static_cast<uint32_t>((val4 << 4) | val4);
        if (ctrl == 0)
          hold = colors_aga_[val4];
        else if (ctrl == 1)
          hold = (hold & 0xFFFFFF00u) | val8;
        else if (ctrl == 2)
          hold = (hold & 0xFF00FFFFu) | (val8 << 16);
        else
          hold = (hold & 0xFFFF00FFu) | (val8 << 8);
        dst[x] = hold;
      } else if (dpf) {
        const uint8_t pf1 = static_cast<uint8_t>(
            ((color_idx >> 0) & 1u) | (((color_idx >> 2) & 1u) << 1) |
            (((color_idx >> 4) & 1u) << 2) | (((color_idx >> 6) & 1u) << 3));
        const uint8_t pf2 = static_cast<uint8_t>(
            ((color_idx >> 1) & 1u) | (((color_idx >> 3) & 1u) << 1) |
            (((color_idx >> 5) & 1u) << 2) | (((color_idx >> 7) & 1u) << 3));
        if (pf1 != 0 || pf2 != 0) {
          const uint8_t final_idx =
              (pf2pri && pf2 != 0) || (pf1 == 0)
                  ? static_cast<uint8_t>((pf2 + pf2_offset) ^ bplam)
                  : static_cast<uint8_t>(pf1 ^ bplam);
          dst[x] = colors_aga_[final_idx];
        }
      } else if (color_idx != 0) {
        uint32_t c = colors_aga_[static_cast<uint8_t>(color_idx ^ bplam)];
        if (ehb && (color_idx & 32u) != 0) {
          const uint32_t r = ((c >> 16) & 0xFFu) >> 1;
          const uint32_t g = ((c >> 8) & 0xFFu) >> 1;
          const uint32_t b = (c & 0xFFu) >> 1;
          c = 0xFF000000u | (r << 16) | (g << 8) | b;
        }
        dst[x] = c;
      }
    }
  } else {
    const bool pf2pri = (bplcon2_ & 0x0040u) != 0;
    uint16_t hold12 = colors_[0] & 0x0FFFu;
    for (int32_t x = clip_x0; x < clip_x1; ++x) {
      apply_events(x);
      const uint8_t color_idx = line_idx[static_cast<size_t>(x)];
      if (ham && (bpu == 6 || bpu == 5)) {
        const uint8_t ctrl = (color_idx >> 4) & 0x3u;
        const uint16_t val4 = static_cast<uint16_t>(color_idx & 0x0Fu);
        if (ctrl == 0)
          hold12 = colors_[val4] & 0x0FFFu;
        else if (ctrl == 1)
          hold12 = static_cast<uint16_t>((hold12 & 0x0FF0u) | val4);
        else if (ctrl == 2)
          hold12 = static_cast<uint16_t>((hold12 & 0x00FFu) | (val4 << 8));
        else
          hold12 = static_cast<uint16_t>((hold12 & 0x0F0Fu) | (val4 << 4));
        dst[x] = amiga_rgb12_to_bgra32(hold12);
      } else if (dpf) {
        const uint8_t pf1 = static_cast<uint8_t>(
            ((color_idx >> 0) & 1u) | (((color_idx >> 2) & 1u) << 1) |
            (((color_idx >> 4) & 1u) << 2));
        const uint8_t pf2 = static_cast<uint8_t>(
            ((color_idx >> 1) & 1u) | (((color_idx >> 3) & 1u) << 1) |
            (((color_idx >> 5) & 1u) << 2));
        if (pf1 != 0 || pf2 != 0) {
          const uint8_t idx = (pf2pri && pf2 != 0) || (pf1 == 0)
                                  ? static_cast<uint8_t>(8u + pf2)
                                  : pf1;
          dst[x] = amiga_rgb12_to_bgra32(colors_[idx]);
        }
      } else if (color_idx != 0) {
        uint16_t rgb12 = colors_[color_idx & 31u];
        if (ehb && (color_idx & 32u) != 0) {
          rgb12 = static_cast<uint16_t>((rgb12 >> 1) & 0x0777u);
        }
        dst[x] = amiga_rgb12_to_bgra32(rgb12);
      }
    }
  }

  for (uint8_t p = 0; p < bpu; ++p) {
    const int16_t mod = line_bpl_mod(p);
    bplpt_[p] = static_cast<uint32_t>(static_cast<int32_t>(bplpt_[p]) + mod);
  }

  apply_events(INT32_MAX);

  // Hardware Sprites DMA (SPR0..SPR7, including 16-color attached, vertical
  // multiplexing, and AGA 32/64-bit)
  const bool spr_dma = (dmacon_ & 0x0220u) == 0x0220u; // DMAEN + SPREN
  if (spr_dma && line >= 20) {
    const uint8_t spr_fmode =
        aga_enabled_ ? static_cast<uint8_t>((fmode_ >> 2) & 0x03u) : 0u;
    const int spr_width =
        (spr_fmode == 3u) ? 64 : ((spr_fmode != 0u) ? 32 : 16);
    const uint32_t spr_stride = static_cast<uint32_t>(spr_width / 8) *
                                2u; // bytes for both A+B channels
    const uint8_t spres =
        aga_enabled_ ? static_cast<uint8_t>((bplcon3_ >> 6) & 0x03u) : 0u;
    // Sprite pixel width in 35 ns units: SPRES 00/01 = lores (140 ns),
    // 10 = hires (70 ns), 11 = SuperHires (35 ns). One framebuffer pixel = 70 ns.
    const int32_t spr_unit35 = (spres == 3u) ? 1 : ((spres == 2u) ? 2 : 4);

    std::array<uint64_t, 8> spr_a{};
    std::array<uint64_t, 8> spr_b{};
    std::array<bool, 8> spr_vis{};

    auto read_spr_chan = [&](uint32_t addr) -> uint64_t {
      if (spr_width == 16)
        return read_chip16(addr);
      if (spr_width == 32) {
        return (static_cast<uint64_t>(read_chip16(addr)) << 16) |
               read_chip16(addr + 2u);
      }
      uint64_t v = 0;
      for (int w = 0; w < 4; ++w) {
        v = (v << 16) | read_chip16(addr + static_cast<uint32_t>(w * 2));
      }
      return v;
    };

    auto fetch_ctrl = [&](SpriteState &sp) {
      const uint16_t pos = read_chip16(sp.pt);
      const uint16_t ctl = read_chip16(sp.pt + (spr_stride / 2u));
      sp.pt += spr_stride;
      if (pos == 0 && ctl == 0) {
        sp.done = true;
        sp.armed = true;
        return;
      }
      sp.vstart = static_cast<uint16_t>(((pos >> 8) & 0xFFu) |
                                        ((ctl & 0x04u) ? 0x100u : 0u));
      sp.vstop = static_cast<uint16_t>(((ctl >> 8) & 0xFFu) |
                                       ((ctl & 0x02u) ? 0x100u : 0u));
      sp.hstart = static_cast<uint16_t>(((pos & 0xFFu) << 1) | (ctl & 0x01u));
      sp.hfine = aga_enabled_ ? static_cast<uint8_t>((ctl >> 3) & 0x03u) : 0u;
      sp.attached = (ctl & 0x80u) != 0;
      sp.armed = true;
      sp.done = false;
    };

    for (unsigned s = 0; s < 8; ++s) {
      auto &sp = sprites_[s];
      if (sp.pt == 0)
        continue;
      if (!sp.armed) {
        fetch_ctrl(sp);
      }
      int guard = 0;
      while (!sp.done && line >= sp.vstop && guard++ < 32) {
        fetch_ctrl(sp);
      }
      if (!sp.done && line >= sp.vstart && line < sp.vstop) {
        spr_a[s] = read_spr_chan(sp.pt);
        spr_b[s] = read_spr_chan(sp.pt + (spr_stride / 2u));
        const bool sscan2 = aga_enabled_ && ((fmode_ & 0x8000u) != 0u);
        if (!sscan2 || ((line & 1u) != (sp.vstart & 1u))) {
          sp.pt += spr_stride;
        }
        spr_vis[s] = true;
      }
    }

    auto spr_color_px = [&](unsigned s, uint8_t c,
                            bool is_attached) -> uint32_t {
      if (!aga_enabled_) {
        const unsigned idx =
            is_attached ? (16u + c) : (16u + (s / 2u) * 4u + c);
        return amiga_rgb12_to_bgra32(colors_[idx]);
      }
      const unsigned bank_nibble = (is_attached || (s & 1u))
                                       ? (bplcon4_ & 0x0Fu)
                                       : ((bplcon4_ >> 4) & 0x0Fu);
      const unsigned base = is_attached ? (bank_nibble * 16u)
                                        : (bank_nibble * 16u + (s / 2u) * 4u);
      return colors_aga_[(base + c) & 0xFFu];
    };

    const unsigned pf1p = bplcon2_ & 0x07u;
    const unsigned pf2p = (bplcon2_ >> 3) & 0x07u;
    auto pf_covers_sprite = [&](int32_t x, int pair) -> bool {
      const uint8_t idx = line_idx[static_cast<size_t>(x)];
      if (idx == 0)
        return false;
      const unsigned p = static_cast<unsigned>(pair);
      if (dpf) {
        const bool pf1_on = (idx & 0x55u) != 0;
        const bool pf2_on = (idx & 0xAAu) != 0;
        return (pf1_on && pf1p <= p) || (pf2_on && pf2p <= p);
      }
      return pf2p <= p;
    };

    auto draw_spr_px = [&](int32_t p35, uint32_t px, int pair) {
      if (!dst)
        return;
      for (int32_t s35 = p35; s35 < p35 + spr_unit35; ++s35) {
        const int32_t px_x = s35 >> 1;
        if ((s35 & 1) == 0 && (s35 + 1) < (p35 + spr_unit35)) {
          if (px_x >= clip_x0 && px_x < clip_x1 &&
              !pf_covers_sprite(px_x, pair)) {
            dst[px_x] = px;
          }
          ++s35;
        } else {
          if (px_x >= clip_x0 && px_x < clip_x1 &&
              !pf_covers_sprite(px_x, pair)) {
            dst[px_x] = 0xFF000000u |
                        (((dst[px_x] & 0x00FEFEFEu) + (px & 0x00FEFEFEu)) >> 1);
          }
        }
      }
    };

    for (int pair = 3; pair >= 0; --pair) {
      const unsigned s0 = static_cast<unsigned>(pair * 2);
      const unsigned s1 = s0 + 1u;
      if ((spr_vis[s0] || spr_vis[s1]) && sprites_[s1].attached &&
          sprites_[s1].pt != 0) {
        const int32_t base35 = (static_cast<int32_t>(sprites_[s0].hstart) -
                                static_cast<int32_t>(VISIBLE_HSTART)) *
                                   4 +
                               static_cast<int32_t>(sprites_[s0].hfine);
        const uint64_t a0 = spr_vis[s0] ? spr_a[s0] : 0ull;
        const uint64_t b0 = spr_vis[s0] ? spr_b[s0] : 0ull;
        const uint64_t a1 = spr_vis[s1] ? spr_a[s1] : 0ull;
        const uint64_t b1 = spr_vis[s1] ? spr_b[s1] : 0ull;
        for (int bit = spr_width - 1; bit >= 0; --bit) {
          const uint8_t c = static_cast<uint8_t>(
              ((a0 >> bit) & 1ull) | (((b0 >> bit) & 1ull) << 1) |
              (((a1 >> bit) & 1ull) << 2) | (((b1 >> bit) & 1ull) << 3));
          if (c == 0)
            continue;
          const uint32_t px = spr_color_px(s0, c, true);
          const int32_t p35 = base35 + (spr_width - 1 - bit) * spr_unit35;
          draw_spr_px(p35, px, pair);
        }
      } else {
        for (int k = 1; k >= 0; --k) {
          const unsigned s = s0 + static_cast<unsigned>(k);
          if (!spr_vis[s])
            continue;
          const int32_t base35 = (static_cast<int32_t>(sprites_[s].hstart) -
                                  static_cast<int32_t>(VISIBLE_HSTART)) *
                                     4 +
                                 static_cast<int32_t>(sprites_[s].hfine);
          const uint64_t a = spr_a[s];
          const uint64_t b = spr_b[s];
          for (int bit = spr_width - 1; bit >= 0; --bit) {
            const uint8_t c = static_cast<uint8_t>(((a >> bit) & 1ull) |
                                                   (((b >> bit) & 1ull) << 1));
            if (c == 0)
              continue;
            const uint32_t px = spr_color_px(s, c, false);
            const int32_t p35 = base35 + (spr_width - 1 - bit) * spr_unit35;
            draw_spr_px(p35, px, pair);
          }
        }
      }
    }
  }
  mirror_non_lace();
}

FloppyDrive *CustomChips::active_floppy() const {
  for (FloppyDrive *f : floppies_) {
    if (f && f->enabled() && f->selected() && f->motor_on()) {
      return f;
    }
  }
  for (FloppyDrive *f : floppies_) {
    if (f && f->enabled() && f->selected()) {
      return f;
    }
  }
  for (FloppyDrive *f : floppies_) {
    if (f && f->enabled() && f->motor_on()) {
      return f;
    }
  }
  return floppies_[0];
}

void CustomChips::run_disk_line(bool &out_disk_index) {
  FloppyDrive *floppy = active_floppy();
  if (!floppy || !floppy->motor_on()) {
    return;
  }

  const bool dma_enabled =
      (dmacon_ & 0x0210u) == 0x0210u; // DMAEN (bit 9) + DSKEN (bit 4)
  if (!dsk_dma_active_ || !dma_enabled) {
    // Still rotate the disk slowly (1 word per scanline -> index pulse every
    // ~20 frames)
    bool idx = false;
    (void)floppy->read_mfm_word(idx);
    if (idx)
      out_disk_index = true;
    return;
  }

  const bool wordsync = (adkcon_ & 0x0400u) != 0;
  if (wordsync && !dsk_synced_) {
    // Scan up to 128 words per line to catch the sync marker cleanly
    for (int i = 0; i < 128; ++i) {
      bool idx = false;
      const uint16_t w = floppy->read_mfm_word(idx);
      if (idx)
        out_disk_index = true;
      if (w == dsksync_) {
        dsk_synced_ = true;
        intreq_ |= (1u << 12); // DSKSYN
        break;
      }
    }
    if (!dsk_synced_)
      return;
  }

  // Transfer up to 4 MFM words per scanline
  for (int i = 0; i < 4 && dsklen_ > 0; ++i) {
    if (dsk_write_) {
      const uint16_t w = read_chip16(dskpt_);
      floppy->write_mfm_word(w);
    } else {
      bool idx = false;
      const uint16_t w = floppy->read_mfm_word(idx);
      if (idx)
        out_disk_index = true;
      write_chip16(dskpt_, w);
    }
    dskpt_ += 2u;
    --dsklen_;
    if (dsklen_ == 0) {
      if (dsk_write_) {
        floppy->finish_write_track();
      }
      dsk_dma_active_ = false;
      dsk_write_ = false;
      dsk_synced_ = false;
      intreq_ |= (1u << 1); // DSKBLK
      break;
    }
  }
}

void CustomChips::tick_audio(uint32_t cpu_cycles) {
  audio_cck_rem_ += cpu_cycles;
  const uint32_t cck = audio_cck_rem_ >> 1;
  audio_cck_rem_ &= 1u;

  if (cck > 0) {
    const bool master_dma = (dmacon_ & 0x0200u) != 0;
    for (unsigned i = 0; i < 4; ++i) {
      auto &ch = audio_[i];
      const bool ch_dma = master_dma && ((dmacon_ & (1u << i)) != 0);

      if (ch_dma && !ch.dma_running) {
        ch.dma_running = true;
        ch.pt = ch.lc;
        ch.words_left = ch.len;
        ch.sample_pair = read_chip16(ch.pt);
        ch.pt += 2u;
        if (ch.words_left > 0)
          --ch.words_left;
        ch.byte_idx = 0;
        ch.current_sample = static_cast<int8_t>(ch.sample_pair >> 8);
        ch.per_counter = static_cast<int32_t>(ch.per ? ch.per : 65536u);
        intreq_ |= static_cast<uint16_t>(1u << (7u + i));
      } else if (!ch_dma) {
        ch.dma_running = false;
        continue;
      }

      const int32_t period = static_cast<int32_t>(
          std::max<uint16_t>(ch.per ? ch.per : 65535u, 60u));
      ch.per_counter -= static_cast<int32_t>(cck);
      while (ch.per_counter <= 0) {
        ch.per_counter += period;
        ch.byte_idx ^= 1u;
        if (ch.byte_idx == 1u) {
          ch.current_sample = static_cast<int8_t>(ch.sample_pair & 0xFFu);
        } else {
          if (ch.words_left == 0) {
            ch.pt = ch.lc;
            ch.words_left = ch.len;
            intreq_ |= static_cast<uint16_t>(1u << (7u + i));
          }
          ch.sample_pair = read_chip16(ch.pt);
          ch.pt += 2u;
          if (ch.words_left > 0)
            --ch.words_left;
          ch.current_sample = static_cast<int8_t>(ch.sample_pair >> 8);
        }
      }
    }
  }

  // Resample at 48,000 Hz from 7.09379 MHz CPU clock (~147.787 CPU cycles per
  // sample) Fixed-point accumulator: 48000 / 7093790
  audio_sample_acc_ += cpu_cycles * 4800u;
  constexpr uint32_t kCpuDiv = 709379u;
  while (audio_sample_acc_ >= kCpuDiv) {
    audio_sample_acc_ -= kCpuDiv;

    auto ch_out = [&](unsigned idx) -> float {
      const auto &c = audio_[idx];
      if (!c.dma_running)
        return 0.0f;
      return static_cast<float>(static_cast<int32_t>(c.current_sample) *
                                static_cast<int32_t>(c.vol)) /
             8192.0f;
    };

    const float raw_l = (ch_out(0) + ch_out(3)) * 0.5f;
    const float raw_r = (ch_out(1) + ch_out(2)) * 0.5f;
    // Slight stereo crossfeed so headphones sound natural
    const float mix_l = raw_l * 0.85f + raw_r * 0.15f;
    const float mix_r = raw_r * 0.85f + raw_l * 0.15f;

    const float alpha = led_filter_ ? 0.35f : 0.85f;
    filter_l_ += alpha * (mix_l - filter_l_);
    filter_r_ += alpha * (mix_r - filter_r_);

    float floppy_sample = 0.0f;
    if (!floppy_sound_enabled_) {
      for (FloppyDrive *f : floppies_) {
        if (f && f->enabled()) {
          (void)f->consume_step_sound();
        }
      }
      floppy_step_env_ = 0.0f;
      floppy_motor_env_ = 0.0f;
    } else {
      bool any_step = false;
      uint8_t step_cyl = 0;
      bool step_has_disk = false;
      bool any_motor_with_disk = false;
      for (FloppyDrive *f : floppies_) {
        if (!f || !f->enabled())
          continue;
        if (f->consume_step_sound()) {
          any_step = true;
          step_cyl = f->cylinder();
          step_has_disk = f->has_disk();
        }
        if (f->motor_on() && f->has_disk()) {
          any_motor_with_disk = true;
        }
      }
      if (any_step) {
        floppy_step_env_ = 1.0f;
        floppy_step_phase_ = 0.0f;
        floppy_step_freq_ = 155.0f + static_cast<float>(step_cyl & 15u) * 2.5f;
        floppy_step_gain_ = step_has_disk ? 0.14f : 0.07f;
      }
      const float target_motor = any_motor_with_disk ? 1.0f : 0.0f;
      floppy_motor_env_ += (target_motor - floppy_motor_env_) * 0.004f;

      floppy_noise_rng_ = floppy_noise_rng_ * 1664525u + 1013904223u;
      const float noise =
          static_cast<float>(static_cast<int32_t>(floppy_noise_rng_ >> 16) -
                             32768) /
          32768.0f;

      if (floppy_step_env_ > 0.001f) {
        floppy_step_phase_ += floppy_step_freq_ / 48000.0f;
        if (floppy_step_phase_ >= 1.0f)
          floppy_step_phase_ -= 1.0f;
        const float tri = (floppy_step_phase_ < 0.5f)
                              ? (4.0f * floppy_step_phase_ - 1.0f)
                              : (3.0f - 4.0f * floppy_step_phase_);
        const float click =
            (floppy_step_env_ > 0.72f) ? (noise * 0.55f) : (noise * 0.12f);
        floppy_sample +=
            (tri * 0.75f + click) * floppy_step_env_ * floppy_step_gain_;
        floppy_step_env_ *= 0.9915f;
      }

      if (floppy_motor_env_ > 0.001f) {
        floppy_motor_phase_ += 55.0f / 48000.0f;
        if (floppy_motor_phase_ >= 1.0f)
          floppy_motor_phase_ -= 1.0f;
        const float hum = (floppy_motor_phase_ < 0.5f)
                              ? (4.0f * floppy_motor_phase_ - 1.0f)
                              : (3.0f - 4.0f * floppy_motor_phase_);
        floppy_sample += (hum * 0.006f + noise * 0.007f) * floppy_motor_env_;
      }
    }

    const float out_l = std::clamp(filter_l_ + floppy_sample, -1.0f, 1.0f);
    const float out_r = std::clamp(filter_r_ + floppy_sample, -1.0f, 1.0f);

    const size_t wpos = audio_write_pos_.load(std::memory_order_relaxed);
    const size_t next_w = (wpos + 1u) % AUDIO_RING_FRAMES;
    if (next_w != audio_read_pos_.load(std::memory_order_acquire)) {
      audio_ring_[wpos * 2u] = out_l;
      audio_ring_[wpos * 2u + 1u] = out_r;
      audio_write_pos_.store(next_w, std::memory_order_release);
    }
  }
}

size_t CustomChips::read_audio(float *out_stereo, size_t max_frames) {
  if (!out_stereo || max_frames == 0)
    return 0;
  size_t rpos = audio_read_pos_.load(std::memory_order_relaxed);
  const size_t wpos = audio_write_pos_.load(std::memory_order_acquire);
  size_t count = 0;
  while (count < max_frames && rpos != wpos) {
    out_stereo[count * 2u] = audio_ring_[rpos * 2u];
    out_stereo[count * 2u + 1u] = audio_ring_[rpos * 2u + 1u];
    rpos = (rpos + 1u) % AUDIO_RING_FRAMES;
    ++count;
  }
  audio_read_pos_.store(rpos, std::memory_order_release);
  return count;
}

bool CustomChips::tick_cpu_cycles(uint32_t cycles, bool cia_a_irq,
                                  bool cia_b_irq, bool &out_disk_index) {
  out_disk_index = false;
  if (cia_a_irq)
    intreq_ |= (1u << 3);
  if (cia_b_irq)
    intreq_ |= (1u << 13);

  if (ser_tx_cycles_ > 0u) {
    if (cycles >= ser_tx_cycles_) {
      ser_tx_cycles_ = 0u;
      intreq_ |= 0x0001u; // TBE (Serial Transmit Buffer Empty)
    } else {
      ser_tx_cycles_ -= cycles;
    }
  }

  tick_blitter(cycles);
  tick_audio(cycles);

  bool frame_done = false;
  line_cycle_acc_ += cycles;
  while (line_cycle_acc_ >= CPU_CYCLES_PER_LINE) {
    line_cycle_acc_ -= CPU_CYCLES_PER_LINE;
    run_copper_line(vpos_);
    run_disk_line(out_disk_index);
    render_scanline(vpos_);
    sync_palette_snapshot();

    ++vpos_;
    if (vpos_ >= PAL_LINES) {
      vpos_ = 0;
      if ((bplcon0_ & 0x0004u) != 0) {
        lof_ = !lof_;
      } else {
        lof_ = true;
      }
      intreq_ |= (1u << 5); // VERTB
      cop_pc_ = cop1lc_;
      cop_waiting_ = false;
      cop_hpos_ = 0;
      for (auto &sp : sprites_) {
        sp.armed = false;
        sp.done = false;
      }
      const int32_t step_x = std::clamp(pending_mouse_dx_, -60, 60);
      const int32_t step_y = std::clamp(pending_mouse_dy_, -60, 60);
      mouse_x_ = static_cast<uint8_t>(static_cast<int32_t>(mouse_x_) + step_x);
      mouse_y_ = static_cast<uint8_t>(static_cast<int32_t>(mouse_y_) + step_y);
      pending_mouse_dx_ -= step_x;
      pending_mouse_dy_ -= step_y;
      frame_done = true;
    }
  }
  hpos_ = static_cast<uint8_t>((line_cycle_acc_ * 227u) / CPU_CYCLES_PER_LINE);
  return frame_done;
}

uint8_t CustomChips::current_ipl() const {
  if ((intena_ & 0x4000u) == 0)
    return 0;
  const uint16_t active = intena_ & intreq_ & 0x3FFFu;
  if (active == 0)
    return 0;
  if ((active & (1u << 13)) != 0)
    return 6;
  if ((active & ((1u << 12) | (1u << 11))) != 0)
    return 5;
  if ((active & 0x0780u) != 0)
    return 4;
  if ((active & 0x0070u) != 0)
    return 3;
  if ((active & (1u << 3)) != 0)
    return 2;
  return 1;
}

uint32_t CustomChips::cpu_chip_wait_cycles(bool is_32bit_access) const {
    const uint32_t cck = (line_cycle_acc_ >> 1) % 227u;
    uint32_t wait = 0u;

    // 1. DRAM refresh slots (even CCKs 0, 2, 4)
    if (cck <= 4u && (cck & 1u) == 0u) {
        wait += 2u;
    }

    if ((dmacon_ & 0x0200u) != 0u) { // Master DMAEN
        // 2. Disk DMA slots (CCKs 7, 9, 11)
        if (dsk_dma_active_ && (dmacon_ & 0x0010u) != 0u &&
            (cck == 7u || cck == 9u || cck == 11u)) {
            wait += 2u;
        }

        // 3. Audio DMA slots (CCKs 13, 15, 17, 19 for channels 0..3)
        if ((dmacon_ & 0x000Fu) != 0u && cck >= 13u && cck <= 19u && (cck & 1u) != 0u) {
            const uint32_t aud_ch = (cck - 13u) >> 1;
            if ((dmacon_ & (1u << aud_ch)) != 0u) {
                wait += 2u;
            }
        }

        // 4. Sprite DMA slots (odd CCKs 21..51 on visible lines)
        if ((dmacon_ & 0x0020u) != 0u && vpos_ >= 25u && vpos_ < 312u &&
            cck >= 21u && cck <= 51u && (cck & 1u) != 0u) {
            const uint32_t spr_idx = (cck - 21u) >> 2;
            if (spr_idx < 8u && vpos_ >= sprites_[spr_idx].vstart && vpos_ < sprites_[spr_idx].vstop) {
                wait += 2u;
            }
        }

        // 5. Bitplane DMA slots inside DDFSTRT..DDFSTOP window
        if ((dmacon_ & 0x0100u) != 0u && vpos_ >= VISIBLE_VSTART && vpos_ < 312u) {
            uint8_t bpu = static_cast<uint8_t>((bplcon0_ >> 12) & 0x7u);
            if (aga_enabled_ && (bplcon0_ & 0x0010u) != 0u && bpu == 0u) {
                bpu = 8u;
            }
            if (bpu > 0u) {
                const uint32_t ddf_s = std::max<uint32_t>(ddfstrt_ & 0xFEu, 0x18u);
                const uint32_t ddf_e = std::min<uint32_t>(ddfstop_ & 0xFEu, 0xE0u);
                if (cck >= ddf_s && cck <= ddf_e) {
                    const bool shres = aga_enabled_ && ((bplcon0_ & 0x0040u) != 0u);
                    const bool hires = !shres && ((bplcon0_ & 0x8000u) != 0u);
                    const uint32_t res_mult = shres ? 4u : (hires ? 2u : 1u);
                    uint32_t fmode_div = 1u;
                    if (aga_enabled_) {
                        const uint16_t bpl_fmode = fmode_ & 0x0003u;
                        if (bpl_fmode == 3u) fmode_div = 4u;
                        else if (bpl_fmode != 0u) fmode_div = 2u;
                    }
                    const uint32_t slots_per_8 = std::min<uint32_t>(
                        8u, (static_cast<uint32_t>(bpu) * res_mult + fmode_div - 1u) / fmode_div);
                    if ((cck & 7u) < slots_per_8) {
                        wait += (slots_per_8 >= 8u) ? 4u : 2u;
                    }
                }
            }
        }

        // 6. Copper DMA (even CCK slots while Copper is actively executing)
        if ((dmacon_ & 0x0080u) != 0u && !cop_waiting_ && (cck & 1u) == 0u) {
            wait += 2u;
        }

        // 7. Blitter DMA contention (BLTPRI/nasty vs shared cycle stealing)
        if ((blt_active_ || blt_busy_cycles_ > 0u) && (dmacon_ & 0x0040u) != 0u) {
            if ((dmacon_ & 0x0400u) != 0u) {
                wait += 4u; // BLTPRI (Blitter Nasty): Blitter steals every free bus cycle
            } else if ((cck & 1u) == 0u) {
                wait += 2u; // Shared bus mode: Blitter steals alternating free cycles
            }
        }
    }

    if (!aga_enabled_ && is_32bit_access) {
        wait *= 2u;
    }
    return wait;
}

size_t CustomChips::drain_serial_tx(uint8_t* dst, size_t max_len) {
    if (serial_tx_buf_.empty()) return 0;
    if (!dst || max_len == 0) return serial_tx_buf_.size();
    const size_t n = std::min(max_len, serial_tx_buf_.size());
    std::copy_n(serial_tx_buf_.data(), n, dst);
    serial_tx_buf_.erase(serial_tx_buf_.begin(),
                         serial_tx_buf_.begin() + static_cast<std::ptrdiff_t>(n));
    return n;
}

void CustomChips::receive_serial_byte(uint8_t val) {
    ser_rx_data_ = val;
    ser_rx_ready_ = true;
    intreq_ |= (1u << 11); // RBF (Serial Receive Buffer Full)
}

} // namespace amiga
