#pragma once

#include "amiga/amiga_bus.hpp"
#include "amiga/cia.hpp"
#include "amiga/custom.hpp"
#include "amiga/floppy.hpp"
#include "amiga/gayle_ide.hpp"
#include "amiga/hard_drive.hpp"
#include "amiga/m68k.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

namespace amiga {

/// Top-level Amiga 500 machine tying CPU, bus, CIA-A/B and custom chips together.
class Amiga500 {
public:
    Amiga500();

    void set_model(AmigaModel model);
    AmigaModel model() const { return model_; }
    void set_a500_chip_ram_512k(bool enabled) { bus_.set_a500_chip_ram_512k(enabled); }
    bool a500_chip_ram_512k() const { return bus_.a500_chip_ram_512k(); }
    void set_a600_chip_ram_2mb(bool enabled) { bus_.set_a600_chip_ram_2mb(enabled); }
    bool a600_chip_ram_2mb() const { return bus_.a600_chip_ram_2mb(); }
    void set_fast_ram_mb(uint32_t mb) { bus_.set_fast_ram_mb(mb); }
    bool load_rom(const uint8_t* data, size_t size);
    bool load_ext_rom(const uint8_t* data, size_t size);
    void clear_ext_rom();
    bool insert_adf(const std::string& path);
    bool insert_adf(size_t unit, const std::string& path);
    void eject_adf();
    void eject_adf(size_t unit);
    void set_floppy_enabled(size_t unit, bool enabled);
    void clear_hdfs();
    size_t add_hdf(const std::string& path);
    void remove_hdf(size_t index);
    void reset();
    void key_event(uint8_t rawkey, bool pressed);
    void set_joystick(bool up, bool down, bool left, bool right, bool fire);
    void mouse_move(int32_t dx, int32_t dy);
    void set_mouse_buttons(bool left, bool right);

    /// Executes one CPU instruction and advances CIA/custom chip clocks accordingly.
    uint32_t step_instruction();

    /// Runs the machine until one PAL video frame completes.
    void step_frame();

    M68k& cpu() { return cpu_; }
    const M68k& cpu() const { return cpu_; }
    AmigaBus& bus() { return bus_; }
    const AmigaBus& bus() const { return bus_; }
    Cia8520& cia_a() { return cia_a_; }
    Cia8520& cia_b() { return cia_b_; }
    CustomChips& custom() { return custom_; }
    const CustomChips& custom() const { return custom_; }
    FloppyDrive& floppy() { return floppies_[0]; }
    const FloppyDrive& floppy() const { return floppies_[0]; }
    FloppyDrive& floppy(size_t unit) { return floppies_[unit < 4 ? unit : 0]; }
    const FloppyDrive& floppy(size_t unit) const { return floppies_[unit < 4 ? unit : 0]; }
    GayleIde& gayle_ide() { return gayle_ide_; }
    const GayleIde& gayle_ide() const { return gayle_ide_; }
    uint32_t led_state() const;

private:
    AmigaModel model_ = AmigaModel::A500;
    Cia8520 cia_a_;
    Cia8520 cia_b_;
    CustomChips custom_;
    std::array<FloppyDrive, 4> floppies_;
    HardDriveController hd_;
    GayleIde gayle_ide_;
    AmigaBus bus_;
    M68k cpu_;
    uint32_t e_clock_rem_ = 0;
    std::deque<uint8_t> key_queue_;
    uint32_t key_cooldown_cycles_ = 0;
    bool joy_fire_ = false;
    bool mouse_left_ = false;
    bool mouse_left_physical_ = false;
    uint8_t mouse_left_hold_ = 0;
    bool mouse_right_physical_ = false;
    uint8_t mouse_right_hold_ = 0;
};

}  // namespace amiga
