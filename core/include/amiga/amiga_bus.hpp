#pragma once

#include "amiga/bus.hpp"
#include "amiga/cia.hpp"
#include "amiga/custom.hpp"
#include "amiga/gayle_ide.hpp"
#include "amiga/hard_drive.hpp"
#include "amiga/model.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace amiga {

/// Amiga 500 24-bit memory map and I/O bus.
class AmigaBus final : public Bus {
public:
    static constexpr size_t CHIP_RAM_SIZE = 1024 * 1024;
    static constexpr size_t A1200_CHIP_RAM_SIZE = 2 * 1024 * 1024;
    static constexpr size_t SLOW_RAM_SIZE = 512 * 1024;
    static constexpr size_t MAX_ROM_SIZE = 512 * 1024;

    AmigaBus(Cia8520& cia_a, Cia8520& cia_b, CustomChips& custom);

    void set_model(AmigaModel model);
    AmigaModel model() const { return model_; }
    void set_a500_chip_ram_512k(bool enabled);
    bool a500_chip_ram_512k() const { return a500_chip_512k_; }
    void set_a600_chip_ram_2mb(bool enabled);
    bool a600_chip_ram_2mb() const { return a600_chip_2mb_; }
    size_t chip_ram_size() const { return chip_ram_.size(); }
    size_t slow_ram_size() const { return slow_ram_.size(); }
    void set_fast_ram_mb(uint32_t mb);
    uint32_t fast_ram_mb() const { return static_cast<uint32_t>(fast_ram_.size() / (1024u * 1024u)); }
    void reset();
    void reset_peripherals() override;
    uint32_t take_wait_cycles() override;
    bool is_data_cacheable(uint32_t addr) const override;
    bool load_rom(const uint8_t* data, size_t size);
    bool load_ext_rom(const uint8_t* data, size_t size);
    void clear_ext_rom();

    uint8_t read8(uint32_t addr) override;
    uint16_t read16(uint32_t addr) override;
    uint32_t read32(uint32_t addr) override;

    void write8(uint32_t addr, uint8_t val) override;
    void write16(uint32_t addr, uint16_t val) override;
    void write32(uint32_t addr, uint32_t val) override;

    bool ovl_active() const { return (cia_a_.port_a() & 0x01u) != 0; }
    const uint8_t* chip_ram() const { return chip_ram_.data(); }
    void set_hard_drive(HardDriveController* hd) { hd_ = hd; }
    void set_gayle_ide(GayleIde* ide) { gayle_ide_ = ide; }

    struct ConfigStateSnapshot {
        uint8_t model = 0;
        uint8_t a500_chip_512k = 0;
        uint8_t a600_chip_2mb = 0;
        uint8_t fast_ram_configured = 0;
        uint8_t fast_ram_shut_up = 0;
        uint32_t fast_ram_base = 0;
        uint8_t gayle_id_shift = 0;
        uint8_t gayle_status = 0;
        uint8_t gayle_intreq = 0;
        uint8_t gayle_intena = 0;
        uint8_t gayle_cfg = 0;
        uint8_t ramsey_ctrl = 0x08u;
        uint8_t gary_coldstart = 0x80u;
        uint8_t gary_toenb = 0x80u;
        uint8_t gary_timeout = 0x00u;
        uint8_t buster_ctrl = 0x0Bu;
        uint8_t rtc_ctrl_d = 0x08u;
        uint8_t rtc_ctrl_e = 0x00u;
        uint8_t rtc_ctrl_f = 0x04u;
        uint8_t rtc_24h = 1;
        std::array<uint8_t, 16> rtc_bank1{};
        std::array<std::array<uint8_t, 16>, 2> rtc_nvram{};
    };

    ConfigStateSnapshot capture_config_state() const {
        ConfigStateSnapshot s;
        s.model = static_cast<uint8_t>(model_);
        s.a500_chip_512k = a500_chip_512k_ ? 1 : 0;
        s.a600_chip_2mb = a600_chip_2mb_ ? 1 : 0;
        s.fast_ram_configured = fast_ram_configured_ ? 1 : 0;
        s.fast_ram_shut_up = fast_ram_shut_up_ ? 1 : 0;
        s.fast_ram_base = fast_ram_base_;
        s.gayle_id_shift = gayle_id_shift_;
        s.gayle_status = gayle_status_;
        s.gayle_intreq = gayle_intreq_;
        s.gayle_intena = gayle_intena_;
        s.gayle_cfg = gayle_cfg_;
        s.ramsey_ctrl = ramsey_ctrl_;
        s.gary_coldstart = gary_coldstart_;
        s.gary_toenb = gary_toenb_;
        s.gary_timeout = gary_timeout_;
        s.buster_ctrl = buster_ctrl_;
        s.rtc_ctrl_d = rtc_ctrl_d_;
        s.rtc_ctrl_e = rtc_ctrl_e_;
        s.rtc_ctrl_f = rtc_ctrl_f_;
        s.rtc_24h = rtc_24h_ ? 1 : 0;
        s.rtc_bank1 = rtc_bank1_;
        s.rtc_nvram = rtc_nvram_;
        return s;
    }

    void restore_config_state(const ConfigStateSnapshot& s) {
        fast_ram_configured_ = s.fast_ram_configured != 0;
        fast_ram_shut_up_ = s.fast_ram_shut_up != 0;
        fast_ram_base_ = s.fast_ram_base;
        gayle_id_shift_ = s.gayle_id_shift;
        gayle_status_ = s.gayle_status;
        gayle_intreq_ = s.gayle_intreq;
        gayle_intena_ = s.gayle_intena;
        gayle_cfg_ = s.gayle_cfg;
        ramsey_ctrl_ = s.ramsey_ctrl;
        gary_coldstart_ = s.gary_coldstart;
        gary_toenb_ = s.gary_toenb;
        gary_timeout_ = s.gary_timeout;
        buster_ctrl_ = s.buster_ctrl;
        rtc_ctrl_d_ = s.rtc_ctrl_d;
        rtc_ctrl_e_ = s.rtc_ctrl_e;
        rtc_ctrl_f_ = s.rtc_ctrl_f;
        rtc_24h_ = s.rtc_24h != 0;
        rtc_bank1_ = s.rtc_bank1;
        rtc_nvram_ = s.rtc_nvram;
    }

    const std::vector<uint8_t>& chip_ram_vec() const { return chip_ram_; }
    const std::vector<uint8_t>& slow_ram_vec() const { return slow_ram_; }
    const std::vector<uint8_t>& mb_fast_ram_vec() const { return mb_fast_ram_; }
    const std::vector<uint8_t>& fast_ram_vec() const { return fast_ram_; }
    const std::vector<uint8_t>& rom_vec() const { return rom_; }
    const std::vector<uint8_t>& ext_rom_vec() const { return ext_rom_; }
    std::vector<uint8_t>& chip_ram_vec_mut() { return chip_ram_; }
    std::vector<uint8_t>& slow_ram_vec_mut() { return slow_ram_; }
    std::vector<uint8_t>& mb_fast_ram_vec_mut() { return mb_fast_ram_; }
    std::vector<uint8_t>& fast_ram_vec_mut() { return fast_ram_; }

private:
    uint8_t read_rom8(uint32_t offset) const;
    bool fast_ram_autoconfig_active() const {
        return !fast_ram_.empty() && !fast_ram_configured_ && !fast_ram_shut_up_;
    }
    bool fast_ram_mapped() const {
        return fast_ram_configured_;
    }
    bool is_zorro3_fast_ram() const {
        return model_ == AmigaModel::A4000 && fast_ram_mb() >= 16u;
    }
    uint8_t read_fast_autoconfig8(uint32_t offset) const;
    uint8_t read_z3_autoconfig8(uint32_t offset) const;
    uint8_t read_rtc8(uint32_t addr) const;
    void write_rtc8(uint32_t addr, uint8_t val);
    void reset_rtc();
    void account_chip_wait(uint32_t normalized_addr, bool is_32bit);

    Cia8520& cia_a_;
    Cia8520& cia_b_;
    CustomChips& custom_;
    HardDriveController* hd_ = nullptr;
    GayleIde* gayle_ide_ = nullptr;

    std::vector<uint8_t> chip_ram_;
    std::vector<uint8_t> slow_ram_;
    std::vector<uint8_t> mb_fast_ram_;  // A4000 Motherboard Fast RAM ($07000000-$07FFFFFF, 16 MB)
    std::vector<uint8_t> fast_ram_;
    std::vector<uint8_t> rom_;
    std::vector<uint8_t> ext_rom_;
    uint32_t fast_ram_base_ = 0;
    bool fast_ram_configured_ = false;
    bool fast_ram_shut_up_ = false;
    bool a500_chip_512k_ = false;
    bool a600_chip_2mb_ = false;
    AmigaModel model_ = AmigaModel::A500;
    uint8_t gayle_id_shift_ = 0;
    uint8_t gayle_status_ = 0;
    uint8_t gayle_intreq_ = 0;
    uint8_t gayle_intena_ = 0;
    uint8_t gayle_cfg_ = 0;
    uint8_t ramsey_ctrl_ = 0x08u;
    uint8_t gary_coldstart_ = 0x80u;
    uint8_t gary_toenb_ = 0x80u;
    uint8_t gary_timeout_ = 0x00u;
    uint8_t buster_ctrl_ = 0x0Bu;  // Super Buster rev 11
    uint8_t rtc_ctrl_d_ = 0x08u;
    uint8_t rtc_ctrl_e_ = 0x00u;
    uint8_t rtc_ctrl_f_ = 0x04u;
    bool rtc_24h_ = true;
    std::array<uint8_t, 16> rtc_bank1_{};
    std::array<std::array<uint8_t, 16>, 2> rtc_nvram_{};
    uint32_t pending_wait_cycles_ = 0;
    bool suppress_wait_ = false;
};

}  // namespace amiga
