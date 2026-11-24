#include "amiga/bus.hpp"
#include "amiga/m68k.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

namespace {

class TestBus final : public amiga::Bus {
public:
    std::array<uint8_t, 64 * 1024> ram{};

    uint8_t read8(uint32_t addr) override {
        return ram[addr & 0xFFFFu];
    }

    uint16_t read16(uint32_t addr) override {
        const uint16_t hi = read8(addr);
        const uint16_t lo = read8(addr + 1);
        return static_cast<uint16_t>((hi << 8) | lo);
    }

    uint32_t read32(uint32_t addr) override {
        const uint32_t hi = read16(addr);
        const uint32_t lo = read16(addr + 2);
        return (hi << 16) | lo;
    }

    void write8(uint32_t addr, uint8_t val) override {
        ram[addr & 0xFFFFu] = val;
    }

    void write16(uint32_t addr, uint16_t val) override {
        write8(addr, static_cast<uint8_t>(val >> 8));
        write8(addr + 1, static_cast<uint8_t>(val & 0xFFu));
    }

    void write32(uint32_t addr, uint32_t val) override {
        write16(addr, static_cast<uint16_t>(val >> 16));
        write16(addr + 2, static_cast<uint16_t>(val & 0xFFFFu));
    }

    void load_program(uint32_t addr, std::initializer_list<uint16_t> words) {
        uint32_t p = addr;
        for (uint16_t w : words) {
            write16(p, w);
            p += 2;
        }
    }

    void setup_reset_vectors(uint32_t ssp = 0x008000u, uint32_t pc = 0x001000u) {
        write32(0x000000u, ssp);
        write32(0x000004u, pc);
    }
};

int g_failures = 0;

#define EXPECT_EQ(actual, expected)                                                               \
    do {                                                                                          \
        const auto a_val = (actual);                                                              \
        const auto e_val = (expected);                                                            \
        if (a_val != e_val) {                                                                     \
            std::fprintf(stderr, "FAIL %s:%d: %s == 0x%llx, expected 0x%llx\n", __FILE__,         \
                         __LINE__, #actual, static_cast<unsigned long long>(a_val),               \
                         static_cast<unsigned long long>(e_val));                                 \
            ++g_failures;                                                                         \
        }                                                                                         \
    } while (0)

void test_reset_and_moveq() {
    TestBus bus;
    bus.setup_reset_vectors(0x008000u, 0x001000u);
    bus.load_program(0x001000u, {
        0x70FFu,  // MOVEQ #-1, D0
        0x7200u,  // MOVEQ #0, D1
    });

    amiga::M68k cpu(bus);
    cpu.reset();
    EXPECT_EQ(cpu.ssp(), 0x008000u);
    EXPECT_EQ(cpu.pc(), 0x001000u);
    EXPECT_EQ(cpu.sr(), 0x2700u);

    cpu.step();
    EXPECT_EQ(cpu.d(0), 0xFFFFFFFFu);
    EXPECT_EQ((cpu.sr() & amiga::M68k::FLAG_N) != 0, true);
    EXPECT_EQ((cpu.sr() & amiga::M68k::FLAG_Z) != 0, false);

    cpu.step();
    EXPECT_EQ(cpu.d(1), 0x00000000u);
    EXPECT_EQ((cpu.sr() & amiga::M68k::FLAG_Z) != 0, true);
    EXPECT_EQ((cpu.sr() & amiga::M68k::FLAG_N) != 0, false);
}

void test_ea_modes_and_move() {
    TestBus bus;
    bus.setup_reset_vectors();
    bus.write32(0x002000u, 0x11223344u);
    bus.load_program(0x001000u, {
        0x41F8u, 0x2000u,  // LEA ($2000).W, A0
        0x2018u,           // MOVE.L (A0)+, D0
        0x1020u,           // MOVE.B -(A0), D0 (byte from $2003 = 0x44, A0 becomes $2003; wait A0 was $2004)
        0x43E8u, 0x0010u,  // LEA ($10, A0), A1
    });

    amiga::M68k cpu(bus);
    cpu.reset();
    cpu.step();
    EXPECT_EQ(cpu.a(0), 0x00002000u);

    cpu.step();
    EXPECT_EQ(cpu.d(0), 0x11223344u);
    EXPECT_EQ(cpu.a(0), 0x00002004u);

    cpu.step();
    EXPECT_EQ(cpu.a(0), 0x00002003u);
    EXPECT_EQ(cpu.d(0), 0x11223344u);

    cpu.step();
    EXPECT_EQ(cpu.a(1), 0x00002013u);
}

void test_arithmetic_flags() {
    TestBus bus;
    bus.setup_reset_vectors();
    bus.load_program(0x001000u, {
        0x707Fu,           // MOVEQ #127, D0
        0x5200u,           // ADDQ.B #1, D0 -> 0x80 (N=1, V=1, C=0, X=0)
        0x7201u,           // MOVEQ #1, D1
        0x5501u,           // SUBQ.B #2, D1 -> 0xFF (N=1, C=1, X=1, V=0)
        0xB001u,           // CMP.B D1, D0 -> 0x80 - 0xFF = 0x81 (N=1, C=1, V=0, Z=0)
    });

    amiga::M68k cpu(bus);
    cpu.reset();

    cpu.step();
    cpu.step();
    EXPECT_EQ(cpu.d(0) & 0xFFu, 0x80u);
    EXPECT_EQ((cpu.sr() & amiga::M68k::FLAG_N) != 0, true);
    EXPECT_EQ((cpu.sr() & amiga::M68k::FLAG_V) != 0, true);
    EXPECT_EQ((cpu.sr() & amiga::M68k::FLAG_C) != 0, false);

    cpu.step();
    cpu.step();
    EXPECT_EQ(cpu.d(1) & 0xFFu, 0xFFu);
    EXPECT_EQ((cpu.sr() & amiga::M68k::FLAG_C) != 0, true);
    EXPECT_EQ((cpu.sr() & amiga::M68k::FLAG_X) != 0, true);

    cpu.step();
    EXPECT_EQ((cpu.sr() & amiga::M68k::FLAG_C) != 0, true);
    EXPECT_EQ((cpu.sr() & amiga::M68k::FLAG_N) != 0, true);
    EXPECT_EQ((cpu.sr() & amiga::M68k::FLAG_Z) != 0, false);
}

void test_branch_bsr_rts_dbra() {
    TestBus bus;
    bus.setup_reset_vectors();
    // Sum 1..4 using DBRA loop inside a subroutine.
    bus.load_program(0x001000u, {
        0x7000u,           // MOVEQ #0, D0
        0x7203u,           // MOVEQ #3, D1 (4 iterations)
        0x6102u,           // BSR.S +2 -> $1008
        0x4E71u,           // NOP ($1006)
        // $1008:
        0xD041u,           // ADD.W D1, D0
        0x51C9u, 0xFFFCu,  // DBRA D1, $1008 (-4 from $100C)
        0x4E75u,           // RTS
    });

    amiga::M68k cpu(bus);
    cpu.reset();
    for (int i = 0; i < 16; ++i) {
        cpu.step();
        if (cpu.pc() == 0x001006u) break;
    }
    EXPECT_EQ(cpu.pc(), 0x001006u);
    EXPECT_EQ(cpu.d(0), 6u);  // 3 + 2 + 1 + 0
    EXPECT_EQ(cpu.a(7), 0x008000u);
}

void test_supervisor_user_and_trap() {
    TestBus bus;
    bus.setup_reset_vectors(0x008000u, 0x001000u);
    bus.write32(32u * 4u, 0x002000u);  // TRAP #0 vector

    bus.load_program(0x001000u, {
        0x41F8u, 0x7000u,  // LEA ($7000).W, A0
        0x4E60u,           // MOVE A0, USP
        0x027Cu, 0xDFFFu,  // ANDI #$DFFF, SR (drop to user mode)
        0x4E40u,           // TRAP #0
        0x4E71u,           // NOP ($100C)
    });
    bus.load_program(0x002000u, {
        0x702Au,           // MOVEQ #42, D0
        0x4E73u,           // RTE
    });

    amiga::M68k cpu(bus);
    cpu.reset();
    cpu.step();  // LEA
    cpu.step();  // MOVE A0, USP
    cpu.step();  // ANDI SR -> User mode
    EXPECT_EQ(cpu.is_supervisor(), false);
    EXPECT_EQ(cpu.a(7), 0x007000u);

    cpu.step();  // TRAP #0
    EXPECT_EQ(cpu.is_supervisor(), true);
    EXPECT_EQ(cpu.pc(), 0x002000u);

    cpu.step();  // MOVEQ #42, D0
    cpu.step();  // RTE
    EXPECT_EQ(cpu.is_supervisor(), false);
    EXPECT_EQ(cpu.a(7), 0x007000u);
    EXPECT_EQ(cpu.pc(), 0x00100Cu);
    EXPECT_EQ(cpu.d(0), 42u);
}

void test_movem_link_unlk() {
    TestBus bus;
    bus.setup_reset_vectors(0x008000u, 0x001000u);
    bus.load_program(0x001000u, {
        0x7011u,           // MOVEQ #$11, D0
        0x7222u,           // MOVEQ #$22, D1
        0x48E7u, 0xC000u,  // MOVEM.L D0-D1, -(A7) (bit15=D0, bit14=D1)
        0x7000u,           // MOVEQ #0, D0
        0x7200u,           // MOVEQ #0, D1
        0x4CDFu, 0x0003u,  // MOVEM.L (A7)+, D0-D1 (bit0=D0, bit1=D1)
        0x4E56u, 0xFFF8u,  // LINK A6, #-8
        0x4E5Eu,           // UNLK A6
    });

    amiga::M68k cpu(bus);
    cpu.reset();
    cpu.step();
    cpu.step();
    cpu.step();  // MOVEM.L -(A7)
    EXPECT_EQ(cpu.a(7), 0x007FF8u);
    EXPECT_EQ(bus.read32(0x007FF8u), 0x11u);
    EXPECT_EQ(bus.read32(0x007FFCu), 0x22u);

    cpu.step();
    cpu.step();
    cpu.step();  // MOVEM.L (A7)+
    EXPECT_EQ(cpu.a(7), 0x008000u);
    EXPECT_EQ(cpu.d(0), 0x11u);
    EXPECT_EQ(cpu.d(1), 0x22u);

    cpu.step();  // LINK A6, #-8
    EXPECT_EQ(cpu.a(6), 0x007FFCu);
    EXPECT_EQ(cpu.a(7), 0x007FF4u);

    cpu.step();  // UNLK A6
    EXPECT_EQ(cpu.a(7), 0x008000u);
    EXPECT_EQ(cpu.a(6), 0x000000u);
}

void test_mul_div_bit_shift() {
    TestBus bus;
    bus.setup_reset_vectors(0x008000u, 0x001000u);
    bus.write32(5u * 4u, 0x003000u);  // Zero Divide vector
    bus.load_program(0x001000u, {
        0x700Au,           // MOVEQ #10, D0
        0xC0FCu, 0x0014u,  // MULU #20, D0 -> 200
        0x80FCu, 0x0003u,  // DIVU #3, D0 -> quot=66 (0x42), rem=2 (0x00020042)
        0x08C0u, 0x0010u,  // BSET #16, D0 -> sets bit 16 (D0=0x00030042), Z=0 (was 0 -> Z=1!)
        0xE388u,           // LSL.L #1, D0 -> 0x00060084
        0x80FCu, 0x0000u,  // DIVU #0, D0 -> Zero Divide exception
    });

    amiga::M68k cpu(bus);
    cpu.reset();
    cpu.step();  // MOVEQ
    cpu.step();  // MULU
    EXPECT_EQ(cpu.d(0), 200u);

    cpu.step();  // DIVU #3
    EXPECT_EQ(cpu.d(0), 0x00020042u);

    cpu.step();  // BSET #16, D0 (bit 16 was 0 -> Z=1, then set to 1)
    EXPECT_EQ((cpu.sr() & amiga::M68k::FLAG_Z) != 0, true);
    EXPECT_EQ(cpu.d(0), 0x00030042u);

    cpu.step();  // LSL.L #1, D0
    EXPECT_EQ(cpu.d(0), 0x00060084u);

    cpu.step();  // DIVU #0
    EXPECT_EQ(cpu.pc(), 0x003000u);
}

void test_stop_and_ipl_interrupt() {
    TestBus bus;
    bus.setup_reset_vectors(0x008000u, 0x001000u);
    bus.write32((24u + 3u) * 4u, 0x002500u);  // Level 3 autovector (VBLANK)
    bus.load_program(0x001000u, {
        0x4E72u, 0x2000u,  // STOP #$2000 (enable all IPLs, halt CPU)
        0x7055u,           // MOVEQ #$55, D0 ($1004)
    });
    bus.load_program(0x002500u, {
        0x7277u,           // MOVEQ #$77, D1
        0x4E73u,           // RTE
    });

    amiga::M68k cpu(bus);
    cpu.reset();
    cpu.step();  // STOP #$2000
    EXPECT_EQ(cpu.halted(), true);
    EXPECT_EQ(cpu.sr(), 0x2000u);

    cpu.set_ipl(3);
    cpu.step();  // Takes IPL3 interrupt, wakes up
    EXPECT_EQ(cpu.halted(), false);
    EXPECT_EQ(cpu.pc(), 0x002500u);
    EXPECT_EQ((cpu.sr() & amiga::M68k::FLAG_IPL_MASK) >> 8, 3u);

    cpu.set_ipl(0);  // Clear interrupt line before RTE
    cpu.step();      // MOVEQ #$77, D1
    cpu.step();      // RTE
    EXPECT_EQ(cpu.pc(), 0x001004u);
    EXPECT_EQ(cpu.d(1), 0x77u);

    cpu.step();  // MOVEQ #$55, D0
    EXPECT_EQ(cpu.d(0), 0x55u);
}

void test_icache_020() {
    TestBus bus;
    amiga::M68k cpu(bus);
    cpu.set_cpu_model(amiga::M68k::CpuModel::M68EC020);
    bus.write32(0x000000, 0x00008000);
    bus.write32(0x000004, 0x00001000);
    cpu.reset();

    // $1000: MOVEQ #1, D0 (0x7001); MOVEC D0, CACR (0x4E7B 0x0002) -> enable I-Cache
    // $1006: MOVEQ #$11, D2 (0x7411)
    // $1008: NOP (0x4E71)
    bus.write16(0x1000, 0x7001);
    bus.write16(0x1002, 0x4E7B);
    bus.write16(0x1004, 0x0002);
    bus.write16(0x1006, 0x7411);
    bus.write16(0x1008, 0x4E71);

    cpu.step();  // MOVEQ #1, D0
    cpu.step();  // MOVEC D0, CACR
    const uint32_t miss_cycles = cpu.step();  // MOVEQ #$11, D2 (cache miss)
    EXPECT_EQ(cpu.d(2), 0x11u);

    // Overwrite $1006 in memory with MOVEQ #$22, D2 without clearing I-Cache
    bus.write16(0x1006, 0x7422);
    cpu.set_pc(0x1006);
    const uint32_t hit_cycles = cpu.step();
    EXPECT_EQ(cpu.d(2), 0x11u);  // Still sees cached opcode
    EXPECT_EQ(hit_cycles < miss_cycles, true);

    // Clear single entry at $1006 via CAAR + CACR bit 2 (CE = 0x04 | E = 0x01)
    cpu.set_d(0, 0x1004);  // $1004..$1007 shares same 4-byte cache line
    cpu.set_pc(0x1010);
    bus.write16(0x1010, 0x4E7B);  // MOVEC D0, CAAR
    bus.write16(0x1012, 0x0802);
    bus.write16(0x1014, 0x7005);  // MOVEQ #5, D0 (E | CE)
    bus.write16(0x1016, 0x4E7B);  // MOVEC D0, CACR
    bus.write16(0x1018, 0x0002);
    cpu.step();
    cpu.step();
    cpu.step();

    cpu.set_pc(0x1006);
    cpu.step();
    EXPECT_EQ(cpu.d(2), 0x22u);  // Refetched new opcode after entry invalidation

    // Overwrite $1006 again and test full cache clear (CACR bit 3 = 0x08 | E = 0x01)
    bus.write16(0x1006, 0x7433);
    cpu.set_pc(0x1006);
    cpu.step();
    EXPECT_EQ(cpu.d(2), 0x22u);  // Still cached

    cpu.set_pc(0x1020);
    bus.write16(0x1020, 0x7009);  // MOVEQ #9, D0 (E | C)
    bus.write16(0x1022, 0x4E7B);  // MOVEC D0, CACR
    bus.write16(0x1024, 0x0002);
    cpu.step();
    cpu.step();

    cpu.set_pc(0x1006);
    cpu.step();
    EXPECT_EQ(cpu.d(2), 0x33u);  // Refetched after full cache clear
}

void test_dcache_030() {
    TestBus bus;
    amiga::M68k cpu(bus);
    cpu.set_cpu_model(amiga::M68k::CpuModel::M68030);
    bus.write32(0x000000, 0x00008000);
    bus.write32(0x000004, 0x00001000);
    cpu.reset();

    // Seed 16-byte burst line at $2000..$200F
    bus.write32(0x2000, 0x11112222u);
    bus.write32(0x2004, 0x33334444u);
    bus.write32(0x2008, 0x55556666u);
    bus.write32(0x200C, 0x77778888u);

    // Enable 68030 Data Cache + Data Burst + Write Allocate (ED=0x0100 | DBE=0x1000 | WA=0x2000 = 0x3100)
    cpu.set_d(0, 0x00003100u);
    bus.write16(0x1000, 0x4E7B);  // MOVEC D0, CACR
    bus.write16(0x1002, 0x0002);
    bus.write16(0x1004, 0x2238);  // MOVE.L ($2000).W, D1 (miss -> bursts $2000..$200F into D-Cache)
    bus.write16(0x1006, 0x2000);
    cpu.step();
    cpu.step();
    EXPECT_EQ(cpu.d(1), 0x11112222u);

    // Modify $2000 and $200C directly on the bus behind the CPU cache (simulating external DMA)
    bus.write32(0x2000, 0xAAAAAAAAu);
    bus.write32(0x200C, 0xBBBBBBBBu);

    // Because $2000..$200F was burst-filled into D-Cache, reading $2000 and $200C hits the D-Cache!
    bus.write16(0x1008, 0x2438);  // MOVE.L ($2000).W, D2
    bus.write16(0x100A, 0x2000);
    bus.write16(0x100C, 0x2638);  // MOVE.L ($200C).W, D3
    bus.write16(0x100E, 0x200C);
    cpu.step();
    cpu.step();
    EXPECT_EQ(cpu.d(2), 0x11112222u);
    EXPECT_EQ(cpu.d(3), 0x77778888u);

    // Clear Data Cache entry $200C via CAAR + CED (0x0400 | 0x3100 = 0x3500)
    cpu.set_d(0, 0x0000200Cu);
    bus.write16(0x1010, 0x4E7B);  // MOVEC D0, CAAR
    bus.write16(0x1012, 0x0802);
    cpu.step();
    cpu.set_d(0, 0x00003500u);
    bus.write16(0x1014, 0x4E7B);  // MOVEC D0, CACR
    bus.write16(0x1016, 0x0002);
    bus.write16(0x1018, 0x2638);  // MOVE.L ($200C).W, D3
    bus.write16(0x101A, 0x200C);
    cpu.step();
    cpu.step();
    EXPECT_EQ(cpu.d(3), 0xBBBBBBBBu);  // Refetched from memory after CED!
}

void test_mmu_030() {
    TestBus bus;
    amiga::M68k cpu(bus);
    cpu.set_cpu_model(amiga::M68k::CpuModel::M68030);
    bus.write32(0x000000, 0x00008000);
    bus.write32(0x000004, 0x00001000);
    cpu.reset();

    // 1. Setup 1-level 68030 page table at $3000 (16 entries of 4-byte short page descriptors, TIA=4, IS=16 -> 4 KB pages)
    // Logical $00005xxx (index 5) -> Physical $00009xxx (descriptor = $00009001, DT=1 page descriptor)
    // Logical $00006xxx (index 6) -> Physical $00006xxx with Cache-Inhibit + Write-Protect ($00006045)
    for (uint32_t i = 0; i < 16; ++i) {
        bus.write32(0x3000 + i * 4u, (i << 12) | 0x01u);
    }
    bus.write32(0x3000 + 5u * 4u, 0x00009001u);
    bus.write32(0x3000 + 6u * 4u, 0x00006045u);  // DT=1 | WP(0x04) | CI(0x40)

    // Store CRP (64-bit: $80000002_00003000) at $2100, TC ($800C4000: E=1, PS=12, IS=16, TIA=4) at $2108,
    // and TT0 ($07008407: base=$07, mask=$00, E=1, CI=1) at $210C
    bus.write32(0x2100, 0x80000002u);
    bus.write32(0x2104, 0x00003000u);
    bus.write32(0x2108, 0x80CF4000u);  // E=1, PS=12, IS=16 (0xF+1? no: IS=16 isn't 4-bit, use IS=12, TIA=8 -> 256 entries)
    // With IS=12, TIA=8 (4 KB pages in lower 16 MB: 12+8+12 = 32 bits!): TC = 0x80CC8000 (E=1, PS=12, IS=12, TIA=8)
    bus.write32(0x2108, 0x80CC8000u);
    bus.write32(0x210C, 0x07008507u);  // TT0: base=$07, mask=$00, E=1 (0x8000), CI=1 (0x0400), RWM=1 (0x0100)

    // Seed physical $9120 with 0xCAFEBABE
    bus.write32(0x9120, 0xCAFEBABEu);

    // $1000: PMOVE ($2100).W, CRP   -> 0xF038, ext 0x4C00, 0x2100
    // $1006: PMOVE ($2108).W, TC    -> 0xF038, ext 0x4000, 0x2108
    // $100C: PMOVE ($210C).W, TT0   -> 0xF038, ext 0x0800, 0x210C
    // $1012: MOVE.L ($5120).W, D1   -> 0x2238, 0x5120 (translates logical $5120 -> physical $9120!)
    // $1016: PTESTR #1, ($6000).W, #7, A2 -> 0xF038, ext 0x9F41, 0x6000
    // $101C: PMOVE MMUSR, ($2110).W -> 0xF038, ext 0x6200, 0x2110
    bus.write16(0x1000, 0xF038); bus.write16(0x1002, 0x4C00); bus.write16(0x1004, 0x2100);
    bus.write16(0x1006, 0xF038); bus.write16(0x1008, 0x4000); bus.write16(0x100A, 0x2108);
    bus.write16(0x100C, 0xF038); bus.write16(0x100E, 0x0800); bus.write16(0x1010, 0x210C);
    bus.write16(0x1012, 0x2238); bus.write16(0x1014, 0x5120);
    bus.write16(0x1016, 0xF038); bus.write16(0x1018, 0x9F41); bus.write16(0x101A, 0x6000);
    bus.write16(0x101C, 0xF038); bus.write16(0x101E, 0x6200); bus.write16(0x1020, 0x2110);

    cpu.step();  // PMOVE CRP
    cpu.step();  // PMOVE TC
    cpu.step();  // PMOVE TT0
    cpu.step();  // MOVE.L ($5120).W, D1 -> reads physical $9120!
    EXPECT_EQ(cpu.d(1), 0xCAFEBABEu);

    cpu.step();  // PTESTR ($6000).W -> checks entry 6 ($3018: CI=1, WP=1, level=1)
    cpu.step();  // PMOVE MMUSR, ($2110).W
    const uint16_t mmusr = bus.read16(0x2110);
    EXPECT_EQ(cpu.a(2), 0x00003018u);            // Descriptor address loaded into A2
    EXPECT_EQ((mmusr & 0x0028u), 0x0028u);       // CI (0x20) | WP (0x08) set
    EXPECT_EQ((mmusr & 0x0007u), 1u);            // 1 level searched
}

void test_fpu_68882() {
    TestBus bus;
    amiga::M68k cpu(bus);
    cpu.set_cpu_model(amiga::M68k::CpuModel::M68030);
    bus.write32(0x000000, 0x00008000);
    bus.write32(0x000004, 0x00001000);
    cpu.reset();

    // 1. Before any FPU instruction, FSAVE -(A7) writes a 4-byte Null state frame ($00000000)
    bus.write16(0x1000, 0xF327);  // FSAVE -(A7)
    cpu.step();
    EXPECT_EQ(cpu.a(7), 0x7FFCu);
    EXPECT_EQ(bus.read32(0x7FFCu), 0x00000000u);

    // 2. Execute FMOVECR #$00, FP0 (pi, 0xF200 0x5C00), FSINCOS.X FP0, FP2:FP1 (0xF200 0x0032),
    //    FMOVE.L FP2, D0 (cos(pi) == -1 -> 0xF200 0x6100)
    bus.write16(0x1002, 0xF200); bus.write16(0x1004, 0x5C00);  // FMOVECR #pi, FP0
    bus.write16(0x1006, 0xF200); bus.write16(0x1008, 0x0032);  // FSINCOS.X FP0, FP2:FP1
    bus.write16(0x100A, 0xF200); bus.write16(0x100C, 0x6100);  // FMOVE.L FP2, D0
    cpu.step();
    cpu.step();
    cpu.step();
    EXPECT_EQ(cpu.d(0), 0xFFFFFFFFu);  // cos(pi) = -1

    // 3. Now FSAVE -(A7) writes the 60-byte MC68882 Idle frame ($1F380000) and FRESTORE (A7)+ pops all 60 bytes
    bus.write16(0x100E, 0xF327);  // FSAVE -(A7)
    bus.write16(0x1010, 0xF35F);  // FRESTORE (A7)+
    cpu.step();
    EXPECT_EQ(cpu.a(7), 0x7FFCu - 60u);
    EXPECT_EQ(bus.read32(cpu.a(7)), 0x1F380000u);
    cpu.step();
    EXPECT_EQ(cpu.a(7), 0x7FFCu);
}

}  // namespace

int main() {
    test_reset_and_moveq();
    test_ea_modes_and_move();
    test_arithmetic_flags();
    test_branch_bsr_rts_dbra();
    test_supervisor_user_and_trap();
    test_movem_link_unlk();
    test_mul_div_bit_shift();
    test_stop_and_ipl_interrupt();
    test_icache_020();
    test_dcache_030();
    test_mmu_030();
    test_fpu_68882();

    if (g_failures == 0) {
        std::printf("All m68k tests passed.\n");
        return EXIT_SUCCESS;
    }
    std::fprintf(stderr, "%d test failure(s).\n", g_failures);
    return EXIT_FAILURE;
}
