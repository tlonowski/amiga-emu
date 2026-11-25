#include "amiga/machine.hpp"

namespace amiga {

Amiga500::Amiga500()
    : cia_a_(0x01u, 0xFDu),  // OVL high, buttons high, floppy bits dynamically updated
      cia_b_(0xC4u, 0xC4u),  // Parallel printer ready (BUSY=0, POUT=0, SEL=1) + serial CTS/DSR/CD ready (0)
      bus_(cia_a_, cia_b_, custom_),
      cpu_(bus_) {
    cia_a_.set_parallel_handshake(true);
    for (size_t i = 0; i < 4; ++i) {
        floppies_[i].set_select_mask(static_cast<uint8_t>(0x08u << i));
        floppies_[i].set_enabled(i == 0);
        custom_.set_floppy(i, &floppies_[i]);
    }
    bus_.set_hard_drive(&hd_);
    bus_.set_gayle_ide(&gayle_ide_);
}

void Amiga500::set_model(AmigaModel model) {
    model_ = model;
    bus_.set_model(model);
    custom_.set_ecs(model == AmigaModel::A600 || model == AmigaModel::A1200 || model == AmigaModel::A4000);
    custom_.set_aga(model == AmigaModel::A1200 || model == AmigaModel::A4000);
    if (model == AmigaModel::A4000) {
        cpu_.set_cpu_model(M68k::CpuModel::M68030);
    } else if (model == AmigaModel::A1200) {
        cpu_.set_cpu_model(M68k::CpuModel::M68EC020);
    } else {
        cpu_.set_cpu_model(M68k::CpuModel::M68000);
    }
}

bool Amiga500::load_rom(const uint8_t* data, size_t size) {
    return bus_.load_rom(data, size);
}

bool Amiga500::load_ext_rom(const uint8_t* data, size_t size) {
    return bus_.load_ext_rom(data, size);
}

void Amiga500::clear_ext_rom() {
    bus_.clear_ext_rom();
}

bool Amiga500::insert_adf(const std::string& path) {
    return insert_adf(0, path);
}

bool Amiga500::insert_adf(size_t unit, const std::string& path) {
    if (unit >= floppies_.size()) return false;
    floppies_[unit].set_enabled(true);
    return floppies_[unit].insert_adf_file(path);
}

void Amiga500::eject_adf() {
    eject_adf(0);
}

void Amiga500::eject_adf(size_t unit) {
    if (unit < floppies_.size()) {
        floppies_[unit].eject();
    }
}

void Amiga500::set_floppy_enabled(size_t unit, bool enabled) {
    if (unit < floppies_.size()) {
        floppies_[unit].set_enabled(unit == 0 ? true : enabled);
    }
}

void Amiga500::clear_hdfs() {
    hd_.clear_units();
}

size_t Amiga500::add_hdf(const std::string& path) {
    return hd_.add_hdf(path);
}

void Amiga500::remove_hdf(size_t index) {
    hd_.remove_hdf(index);
}

void Amiga500::reset() {
    cia_a_.reset();
    cia_b_.reset();
    custom_.reset();
    for (auto& f : floppies_) {
        f.reset();
    }
    hd_.reset();
    gayle_ide_.reset();
    bus_.reset();
    cpu_.reset();
    e_clock_rem_ = 0;
    key_queue_.clear();
    key_cooldown_cycles_ = 0;
    joy_fire_ = false;
    mouse_left_ = false;
    mouse_left_physical_ = false;
    mouse_left_hold_ = 0;
    mouse_right_physical_ = false;
    mouse_right_hold_ = 0;
    custom_.set_joy1(false, false, false, false);
}

void Amiga500::key_event(uint8_t rawkey, bool pressed) {
    const uint8_t code = static_cast<uint8_t>(((rawkey & 0x7Fu) << 1) | (pressed ? 0u : 1u));
    const uint8_t sdr = static_cast<uint8_t>(~code);
    if (key_queue_.size() < 64) {
        key_queue_.push_back(sdr);
    }
}

void Amiga500::set_joystick(bool up, bool down, bool left, bool right, bool fire) {
    joy_fire_ = fire;
    custom_.set_joy1(up, down, left, right);
}

void Amiga500::mouse_move(int32_t dx, int32_t dy) {
    custom_.add_mouse_delta(dx, dy);
}

void Amiga500::set_mouse_buttons(bool left, bool right) {
    if (left && !mouse_left_physical_) {
        mouse_left_hold_ = 3;
    }
    mouse_left_physical_ = left;
    mouse_left_ = left || (mouse_left_hold_ > 0);

    if (right && !mouse_right_physical_) {
        mouse_right_hold_ = 3;
    }
    mouse_right_physical_ = right;
    custom_.set_mouse_right(right || (mouse_right_hold_ > 0));
}

uint32_t Amiga500::step_instruction() {
    uint8_t button_bits = 0xC1u;
    if (mouse_left_) button_bits &= static_cast<uint8_t>(~0x40u);  // /FIR0
    if (joy_fire_) button_bits &= static_cast<uint8_t>(~0x80u);    // /FIR1
    auto update_floppies = [&]() {
        const uint8_t prb = cia_b_.port_b();
        uint8_t status = 0x3Cu;
        for (auto& f : floppies_) {
            f.write_cia_b_prb(prb);
            status &= f.cia_a_status_bits();
        }
        cia_a_.set_pra_input_mask(static_cast<uint8_t>(button_bits | status));
    };

    update_floppies();
    const uint32_t cpu_cycles = cpu_.step();
    uint32_t cycles = cpu_cycles;
    if (model_ == AmigaModel::A1200) {
        cycles = std::max<uint32_t>(1u, cpu_cycles / 3u);
    } else if (model_ == AmigaModel::A4000) {
        cycles = std::max<uint32_t>(1u, cpu_cycles / 4u);
    }
    update_floppies();

    if (key_cooldown_cycles_ > cycles) {
        key_cooldown_cycles_ -= cycles;
    } else {
        key_cooldown_cycles_ = 0;
    }
    if (!key_queue_.empty() && key_cooldown_cycles_ == 0 &&
        cia_a_.serial_input_mode() && !cia_a_.sp_pending()) {
        const uint8_t sdr = key_queue_.front();
        key_queue_.pop_front();
        cia_a_.send_serial_byte(sdr);
        key_cooldown_cycles_ = 4000;
    }

    e_clock_rem_ += cycles;
    const uint32_t e_clocks = e_clock_rem_ / 10u;
    e_clock_rem_ %= 10u;
    if (e_clocks > 0) {
        cia_a_.tick_e_clocks(e_clocks);
        cia_b_.tick_e_clocks(e_clocks);
    }

    const uint16_t prev_vpos = custom_.vpos();
    custom_.set_led_filter((cia_a_.port_a() & 0x02u) == 0);
    bool disk_index = false;
    const bool frame_done = custom_.tick_cpu_cycles(cycles, cia_a_.irq_active(), cia_b_.irq_active(), disk_index);
    if (disk_index) {
        cia_b_.raise_flag_irq();
    }
    if (custom_.vpos() != prev_vpos) {
        cia_b_.tick_tod();  // HSYNC
    }
    if (frame_done) {
        cia_a_.tick_tod();  // VSYNC
        hd_.tick_frame();
        if (mouse_left_hold_ > 0) {
            --mouse_left_hold_;
            if (mouse_left_hold_ == 0 && !mouse_left_physical_) {
                mouse_left_ = false;
            }
        }
        if (mouse_right_hold_ > 0) {
            --mouse_right_hold_;
            if (mouse_right_hold_ == 0 && !mouse_right_physical_) {
                custom_.set_mouse_right(false);
            }
        }
    }

    cpu_.set_ipl(custom_.current_ipl());
    return cycles;
}

void Amiga500::step_frame() {
    uint64_t guard = 0;
    const uint16_t start_vpos = custom_.vpos();
    bool left_start = false;
    while (guard++ < 200000) {
        step_instruction();
        const uint16_t v = custom_.vpos();
        if (v != start_vpos) {
            left_start = true;
        } else if (left_start) {
            break;
        }
    }
}

uint32_t Amiga500::led_state() const {
    uint32_t state = 0x01u;  // Bit 0: Power LED always on
    if ((cia_a_.port_a() & 0x02u) == 0) {
        state |= 0x08u;      // Bit 3: /LED filter active (bright)
    }
    uint8_t active_cyl = 0xFFu;
    uint32_t sel_motor_mask = 0u;
    uint32_t any_motor_mask = 0u;
    for (size_t i = 0; i < floppies_.size(); ++i) {
        const auto& f = floppies_[i];
        if (f.enabled() && f.motor_on()) {
            state |= 0x02u;  // Bit 1: Floppy LED
            any_motor_mask |= (0x10u << i);
            if (f.selected()) {
                sel_motor_mask |= (0x10u << i);
                active_cyl = f.cylinder();
            } else if (active_cyl == 0xFFu) {
                active_cyl = f.cylinder();
            }
        }
    }
    state |= (sel_motor_mask != 0u) ? sel_motor_mask : any_motor_mask;
    if (hd_.active()) {
        state |= 0x04u;      // Bit 2: HD LED
    }
    state |= (static_cast<uint32_t>(active_cyl) << 8);
    return state;
}

}  // namespace amiga
