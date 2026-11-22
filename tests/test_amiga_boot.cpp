#include "amiga/machine.hpp"

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

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

void write_be16(std::vector<uint8_t>& buf, uint32_t off, uint16_t val) {
    buf[off] = static_cast<uint8_t>(val >> 8);
    buf[off + 1] = static_cast<uint8_t>(val & 0xFFu);
}

void write_be32(std::vector<uint8_t>& buf, uint32_t off, uint32_t val) {
    write_be16(buf, off, static_cast<uint16_t>(val >> 16));
    write_be16(buf, off + 2, static_cast<uint16_t>(val & 0xFFFFu));
}

void test_ovl_boot_and_cia_irq() {
    // Build a minimal 256 KB Kickstart-like ROM mapped at $FC0000-$FFFFFF (mirrored at $F80000).
    std::vector<uint8_t> rom(256 * 1024, 0xFF);

    // Reset vectors at ROM offset 0 (visible at $000000 while OVL=1):
    // Initial SSP = $0007F000, Initial PC = $00FC0100.
    write_be32(rom, 0x000000u, 0x0007F000u);
    write_be32(rom, 0x000004u, 0x00FC0100u);

    // Boot code at ROM offset $0100 ($FC0100):
    uint32_t p = 0x0100u;
    auto emit16 = [&](uint16_t w) {
        write_be16(rom, p, w);
        p += 2;
    };
    auto emit32 = [&](uint32_t l) {
        write_be32(rom, p, l);
        p += 4;
    };

    // 1. Set CIA-A DDRA bit 0 as output and clear PRA bit 0 (turn off OVL):
    emit16(0x13FCu); emit16(0x0003u); emit32(0x00BFE201u);  // MOVE.B #$03, $BFE201 (DDRA)
    emit16(0x13FCu); emit16(0x0000u); emit32(0x00BFE001u);  // MOVE.B #$00, $BFE001 (PRA: OVL=0)

    // 2. Write COLOR00 = $0F00 (red) to $DFF180:
    emit16(0x33FCu); emit16(0x0F00u); emit32(0x00DFF180u);  // MOVE.W #$0F00, $DFF180

    // 3. Install Level 2 (PORTS / CIA-A) autovector at $000068 (now Chip RAM!):
    emit16(0x23FCu); emit32(0x00FC0200u); emit32(0x00000068u);  // MOVE.L #$FC0200, ($0068).L

    // 4. Configure CIA-A Timer A = 10 E-clocks, enable Timer A IRQ in CIA-A ICR, start one-shot:
    emit16(0x13FCu); emit16(0x000Au); emit32(0x00BFE401u);  // MOVE.B #$0A, $BFE401 (TALO)
    emit16(0x13FCu); emit16(0x0000u); emit32(0x00BFE501u);  // MOVE.B #$00, $BFE501 (TAHI)
    emit16(0x13FCu); emit16(0x0081u); emit32(0x00BFED01u);  // MOVE.B #$81, $BFED01 (ICR: enable TA)
    emit16(0x13FCu); emit16(0x0019u); emit32(0x00BFEE01u);  // MOVE.B #$19, $BFEE01 (CRA: start+load+oneshot)

    // 5. Enable PORTS + INTEN in Paula INTENA ($DFF09A): $8000 | $4000 | $0008 = $C008
    emit16(0x33FCu); emit16(0xC008u); emit32(0x00DFF09Au);  // MOVE.W #$C008, $DFF09A

    // 6. STOP #$2000 (enable interrupts, wait for CIA-A Timer A)
    emit16(0x4E72u); emit16(0x2000u);

    // 7. After RTE, set COLOR00 = $00F0 (green) and loop:
    emit16(0x33FCu); emit16(0x00F0u); emit32(0x00DFF180u);  // MOVE.W #$00F0, $DFF180
    emit16(0x60FEu);                                        // BRA.S *

    // Level 2 IRQ handler at ROM offset $0200 ($FC0200):
    p = 0x0200u;
    emit16(0x4A39u); emit32(0x00BFED01u);                   // TST.B $BFED01 (ack CIA-A ICR)
    emit16(0x33FCu); emit16(0x0008u); emit32(0x00DFF09Cu);  // MOVE.W #$0008, $DFF09C (clear PORTS in INTREQ)
    emit16(0x7055u);                                        // MOVEQ #$55, D0
    emit16(0x4E73u);                                        // RTE

    amiga::Amiga500 a500;
    EXPECT_EQ(a500.load_rom(rom.data(), rom.size()), true);
    a500.reset();

    EXPECT_EQ(a500.bus().ovl_active(), true);
    EXPECT_EQ(a500.cpu().pc(), 0x00FC0100u);

    for (int i = 0; i < 64; ++i) {
        a500.step_instruction();
        if (a500.custom().color(0) == 0x00F0u && a500.cpu().d(0) == 0x55u) {
            break;
        }
    }

    EXPECT_EQ(a500.bus().ovl_active(), false);
    EXPECT_EQ(a500.custom().color(0), 0x00F0u);
    EXPECT_EQ(a500.cpu().d(0), 0x55u);
}

void test_zorro2_fast_ram_autoconfig() {
    amiga::Amiga500 a1200;
    a1200.set_model(amiga::AmigaModel::A1200);
    a1200.set_fast_ram_mb(8);
    a1200.reset();

    // AutoConfig er_Type for 8 MB Zorro II MEMLIST = 0xE0 (not inverted).
    EXPECT_EQ(a1200.bus().read8(0xE80000u), 0xE0u);
    EXPECT_EQ(a1200.bus().read8(0xE80002u), 0x00u);

    // Before configuration, $200000 is unmapped (reads 0xFF).
    EXPECT_EQ(a1200.bus().read8(0x200000u), 0xFFu);

    // Configure board at $200000.
    a1200.bus().write8(0xE80048u, 0x20u);

    // After configuration, $E80000 falls through (no HD attached -> 0xFF) and 8 MB Fast RAM is R/W.
    EXPECT_EQ(a1200.bus().read8(0xE80000u), 0xFFu);
    a1200.bus().write32(0x200000u, 0xCAFEBABEu);
    a1200.bus().write32(0x9FFFFCu, 0x12345678u);
    EXPECT_EQ(a1200.bus().read32(0x200000u), 0xCAFEBABEu);
    EXPECT_EQ(a1200.bus().read32(0x9FFFFCu), 0x12345678u);
}

void test_a4000_zorro3_fast_ram() {
    amiga::Amiga500 a4000;
    a4000.set_model(amiga::AmigaModel::A4000);
    a4000.set_fast_ram_mb(64);
    a4000.reset();

    // AutoConfig er_Type for 64 MB Zorro III MEMLIST = 0xA2 (0x80 Z3 | 0x20 MEMLIST | size 2).
    EXPECT_EQ(a4000.bus().read8(0xFF000000u), 0xA0u);
    EXPECT_EQ(a4000.bus().read8(0xFF000100u), 0x20u);

    // Configure Zorro III board at $40000000.
    a4000.bus().write16(0xFF000044u, 0x4000u);
    a4000.bus().write32(0x40000000u, 0xDEADBEEFu);
    a4000.bus().write32(0x43FFFFFCu, 0xCAFEBABEu);
    EXPECT_EQ(a4000.bus().read32(0x40000000u), 0xDEADBEEFu);
    EXPECT_EQ(a4000.bus().read32(0x43FFFFFCu), 0xCAFEBABEu);

    // Verify 32-bit ROM range does not mirror onto $40F80000.
    EXPECT_EQ(a4000.bus().read32(0x40F80000u), 0x00000000u);
}

}  // namespace

int main() {
    test_ovl_boot_and_cia_irq();
    test_zorro2_fast_ram_autoconfig();
    test_a4000_zorro3_fast_ram();

    if (g_failures == 0) {
        std::printf("All Amiga boot tests passed.\n");
        return EXIT_SUCCESS;
    }
    std::fprintf(stderr, "%d test failure(s).\n", g_failures);
    return EXIT_FAILURE;
}
