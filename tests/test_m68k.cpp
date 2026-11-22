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

    if (g_failures == 0) {
        std::printf("All m68k tests passed.\n");
        return EXIT_SUCCESS;
    }
    std::fprintf(stderr, "%d test failure(s).\n", g_failures);
    return EXIT_FAILURE;
}
