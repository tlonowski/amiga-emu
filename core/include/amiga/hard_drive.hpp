#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace amiga {

class Bus;

/// Zorro II Autoconfig hard drive controller + ROM driver ("amigahd.device")
/// supporting raw HDFs, Dynamic VHDs (conectix/cxsparse), and RDB (RDSK/PART/FSHD/LSEG).
class HardDriveController {
public:
    struct Unit {
        std::string path;
        std::fstream file;
        uint64_t size_bytes = 0;
        uint32_t cylinders = 0;
        uint32_t surfaces = 1;
        uint32_t sectors_per_block = 1;
        uint32_t blocks_per_track = 32;
        uint32_t reserved = 2;
        uint32_t low_cyl = 0;
        uint32_t high_cyl = 0;
        uint32_t num_buffers = 30;
        uint32_t max_transfer = 0x00100000u;
        uint32_t mask = 0x7FFFFFFEu;
        uint32_t dostype = 0x444F5301u;  // DOS\1
        int32_t boot_pri = 0;
        std::string dos_name;

        // Dynamic VHD support
        bool is_vhd = false;
        uint32_t vhd_block_size = 0;
        uint32_t vhd_sec_per_blk = 0;
        uint32_t vhd_bm_secs = 0;
        uint64_t vhd_bat_off = 0;
        std::vector<uint32_t> vhd_bat;
        std::array<uint8_t, 512> vhd_footer{};

        // RDB filesystem driver binary (FSHD/LSEG)
        std::vector<uint8_t> fs_binary;
        uint32_t fs_version = 0;
        uint32_t fs_alloc_size = 0;
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
    bool read_virtual(Unit& u, uint64_t off, uint8_t* dst, uint32_t len);
    bool write_virtual(Unit& u, uint64_t off, const uint8_t* src, uint32_t len);
    bool install_filesystem(const Unit& u, uint32_t mem_addr, Bus& bus) const;
    void fill_parmpkt(const Unit& u, uint32_t pkt_addr, uint32_t dos_name_addr, Bus& bus) const;

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
