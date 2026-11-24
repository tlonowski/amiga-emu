#pragma once

#include <array>
#include <cstdint>
#include <fstream>
#include <string>

namespace amiga {

/// Native Amiga 600 / 1200 Gayle ($DA2000-$DA3FFF) and Amiga 4000 ($DD2000-$DD3FFF) ATA/IDE controller.
class GayleIde {
public:
    bool attach_hdf(const std::string& path);
    void detach();
    bool attached() const { return file_.is_open(); }

    void reset();

    uint8_t read8(uint32_t addr24);
    uint16_t read16(uint32_t addr24);
    void write8(uint32_t addr24, uint8_t val);
    void write16(uint32_t addr24, uint16_t val);

    bool irq_pending() const { return irq_pending_; }

private:
    void execute_command(uint8_t cmd);
    void prepare_identify();
    uint64_t current_lba() const;
    bool load_next_sector();
    bool commit_current_sector();

    std::string path_;
    std::fstream file_;
    uint64_t total_sectors_ = 0;
    uint16_t geom_cyls_ = 0;
    uint8_t geom_heads_ = 16;
    uint8_t geom_spt_ = 63;

    uint8_t error_ = 0x01;
    uint8_t features_ = 0;
    uint8_t sector_count_ = 1;
    uint8_t sector_num_ = 1;
    uint8_t cyl_low_ = 0;
    uint8_t cyl_high_ = 0;
    uint8_t drive_head_ = 0xA0;
    uint8_t status_ = 0x50;  // DRDY | DSC
    uint8_t dev_ctrl_ = 0;
    bool irq_pending_ = false;

    std::array<uint8_t, 512> sector_buf_{};
    uint32_t buf_pos_ = 0;
    uint32_t sectors_left_ = 0;
    uint64_t cur_lba_ = 0;
    bool writing_sector_ = false;
};

}  // namespace amiga
