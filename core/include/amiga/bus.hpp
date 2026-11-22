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
};

}  // namespace amiga
