#pragma once

#include <cstdint>

namespace amiga {

/// Abstract memory and I/O bus interface (big-endian as seen by the 68000).
class Bus {
public:
    virtual ~Bus() = default;

    virtual uint8_t read8(uint32_t addr) = 0;
    virtual uint16_t read16(uint32_t addr) = 0;
    virtual uint32_t read32(uint32_t addr) = 0;

    virtual void write8(uint32_t addr, uint8_t val) = 0;
    virtual void write16(uint32_t addr, uint16_t val) = 0;
    virtual void write32(uint32_t addr, uint32_t val) = 0;

    /// Resets external peripherals and Autoconfig state on CPU RESET (0x4E70) without clearing RAM.
    virtual void reset_peripherals() {}

    /// Consumes accumulated bus wait cycles (in 7.09 MHz bus clocks) from DMA contention.
    virtual uint32_t take_wait_cycles() { return 0; }

    /// Returns true if the address is data-cacheable by the 68030 D-Cache (false when hardware /CIIN is asserted).
    virtual bool is_data_cacheable(uint32_t /*addr*/) const { return true; }
};

}  // namespace amiga
