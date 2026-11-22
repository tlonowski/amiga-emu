#pragma once

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace amiga {

class Bus;

/// Zorro II Autoconfig hard drive controller + ROM driver ("amigahd.device")
/// supporting an arbitrary number of HDF hardfiles (DH0:, DH1:, ...).
class HardDriveController {
public:
    struct Unit {
        std::string path;
        std::fstream file;
        uint64_t size_bytes = 0;
        uint32_t cylinders = 0;
        uint32_t dostype = 0x444F5301u;  // DOS\1
        int32_t boot_pri = 0;
        std::string dos_name;
    };

    void reset();
    void clear_units();
    size_t add_hdf(const std::string& path);
    void remove_hdf(size_t index);
    size_t unit_count() const { return units_.size(); }
    bool active() const { return activity_frames_ > 0; }
    void tick_frame() { if (activity_frames_ > 0) --activity_frames_; }

    bool handles_read(uint32_t addr24) const;
    bool handles_write(uint32_t addr24) const;
    uint8_t read8(uint32_t addr24);
    void write8(uint32_t addr24, uint8_t val, Bus& bus);
    void write32(uint32_t addr24, uint32_t val, Bus& bus);

private:
    void build_rom();
    void handle_begin_io(uint32_t io_addr, Bus& bus);
    uint8_t read_autoconfig_byte(uint32_t offset) const;

    std::vector<std::unique_ptr<Unit>> units_;
    std::vector<uint8_t> rom_;
    bool configured_ = false;
    bool shut_up_ = false;
    uint32_t base_addr_ = 0;
    uint32_t selected_unit_ = 0;
    uint32_t name_target_addr_ = 0;
    uint8_t activity_frames_ = 0;
};

}  // namespace amiga
