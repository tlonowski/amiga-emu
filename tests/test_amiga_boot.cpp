#include "amiga/machine.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
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

void test_aga_32bit_chip_dma_contention() {
    amiga::Amiga500 a1200;
    a1200.set_model(amiga::AmigaModel::A1200);
    a1200.reset();

    // Turn off OVL so $000000..$1FFFFF is Chip RAM
    a1200.bus().write8(0x00BFE201u, 0x03u);
    a1200.bus().write8(0x00BFE001u, 0x00u);

    // Move beam into active display window (line 100, hpos=0x40 CCK = 0x80 CPU cycles)
    for (int i = 0; i < 100 * 227; ++i) {
        bool idx = false;
        a1200.custom().tick_cpu_cycles(2, false, false, idx);
    }
    // Advance to CCK 0x40 within DDFSTRT ($38)..DDFSTOP ($D0)
    for (int i = 0; i < 0x40; ++i) {
        bool idx = false;
        a1200.custom().tick_cpu_cycles(2, false, false, idx);
    }

    // 1. DMA disabled -> 0 wait cycles
    a1200.custom().write16(0x096u, 0x7FFFu);  // clear DMACON
    EXPECT_EQ(a1200.custom().cpu_chip_wait_cycles(true), 0u);

    // 2. 8-bitplane HIRES with FMODE=0 (16-bit fetch) -> heavy contention (>0 wait)
    a1200.custom().write16(0x100u, 0x8011u);  // HIRES + 8 bitplanes (BPU=0, BPU3=1)
    a1200.custom().write16(0x1FCu, 0x0000u);  // FMODE=0
    a1200.custom().write16(0x096u, 0x8300u);  // DMAEN | BPLEN
    const uint32_t wait_fmode0 = a1200.custom().cpu_chip_wait_cycles(true);
    EXPECT_EQ(wait_fmode0 > 0u, true);

    // 3. Same 8-bitplane HIRES with FMODE=3 (64-bit fetch) at CCK 0x46 -> free slot!
    for (int i = 0; i < 6; ++i) {
        bool idx = false;
        a1200.custom().tick_cpu_cycles(2, false, false, idx);
    }
    a1200.custom().write16(0x1FCu, 0x0003u);  // FMODE=3 (4x fewer slots)
    EXPECT_EQ(a1200.custom().cpu_chip_wait_cycles(true), 0u);

    // 4. Fast RAM access has 0 wait even during heavy Chip DMA
    a1200.set_fast_ram_mb(8);
    a1200.bus().write8(0xE80048u, 0x20u);  // map Fast RAM at $200000
    (void)a1200.bus().take_wait_cycles();
    (void)a1200.bus().read32(0x200000u);
    EXPECT_EQ(a1200.bus().take_wait_cycles(), 0u);
}

void test_gayle_ide_controller() {
    const std::string tmp_path = "/tmp/amiga_gayle_ide_test.hdf";
    {
        std::ofstream f(tmp_path, std::ios::binary | std::ios::trunc);
        std::vector<uint8_t> zeros(512u * 64u, 0u);
        f.write(reinterpret_cast<const char*>(zeros.data()), static_cast<std::streamsize>(zeros.size()));
    }

    amiga::Amiga500 a1200;
    a1200.set_model(amiga::AmigaModel::A1200);
    a1200.reset();
    EXPECT_EQ(a1200.gayle_ide().attach_hdf(tmp_path), true);

    // 1. IDENTIFY DEVICE (0xEC)
    a1200.bus().write8(0x00DA2018u, 0xA0u);
    a1200.bus().write8(0x00DA201Cu, 0xECu);
    EXPECT_EQ((a1200.bus().read8(0x00DA9000u) & 0x80u) != 0u, true);
    EXPECT_EQ(a1200.bus().read8(0x00DA201Cu), 0x58u);  // DRDY | DSC | DRQ (clears IRQ)
    EXPECT_EQ((a1200.bus().read8(0x00DA9000u) & 0x80u) == 0u, true);

    std::vector<uint16_t> id(256, 0);
    for (int i = 0; i < 256; ++i) {
        id[i] = a1200.bus().read16(0x00DA2000u);
    }
    EXPECT_EQ(id[0], 0x0040u);
    EXPECT_EQ(id[49], 0x0200u);  // LBA supported
    EXPECT_EQ(id[60], 64u);      // 64 sectors total

    // 2. WRITE SECTORS (0x30) at LBA 5
    a1200.bus().write8(0x00DA2008u, 1u);     // sector count = 1
    a1200.bus().write8(0x00DA200Cu, 5u);     // LBA 0..7 = 5
    a1200.bus().write8(0x00DA2010u, 0u);     // LBA 8..15
    a1200.bus().write8(0x00DA2014u, 0u);     // LBA 16..23
    a1200.bus().write8(0x00DA2018u, 0xE0u);  // LBA mode + master
    a1200.bus().write8(0x00DA201Cu, 0x30u);  // WRITE SECTORS
    for (uint16_t i = 0; i < 256; ++i) {
        a1200.bus().write16(0x00DA2000u, static_cast<uint16_t>(0xA500u + i));
    }
    EXPECT_EQ(a1200.bus().read8(0x00DA201Cu), 0x50u);  // DRDY | DSC

    // 3. READ SECTORS (0x20) at LBA 5 and verify round-trip
    a1200.bus().write8(0x00DA2008u, 1u);
    a1200.bus().write8(0x00DA200Cu, 5u);
    a1200.bus().write8(0x00DA2010u, 0u);
    a1200.bus().write8(0x00DA2014u, 0u);
    a1200.bus().write8(0x00DA2018u, 0xE0u);
    a1200.bus().write8(0x00DA201Cu, 0x20u);  // READ SECTORS
    for (uint16_t i = 0; i < 256; ++i) {
        EXPECT_EQ(a1200.bus().read16(0x00DA2000u), static_cast<uint16_t>(0xA500u + i));
    }

    a1200.gayle_ide().detach();
    std::remove(tmp_path.c_str());
}

void test_midline_copper_palette_change() {
    amiga::Amiga500 a500;
    a500.reset();
    a500.bus().write8(0x00BFE201u, 0x03u);
    a500.bus().write8(0x00BFE001u, 0x00u);  // OVL off

    // One lores bitplane, all pixels set -> COLOR01 everywhere.
    for (uint32_t addr = 0x20000u; addr < 0x2A000u; addr += 2u) {
        a500.bus().write16(addr, 0xFFFFu);
    }

    // Copper: COLOR00=black, COLOR01=red; WAIT line $64, HP $48; COLOR01=blue; end.
    const uint16_t list[] = {0x0180, 0x0000, 0x0182, 0x0F00, 0x6491,
                             0xFFFE, 0x0182, 0x000F, 0xFFFF, 0xFFFE};
    for (size_t i = 0; i < sizeof(list) / sizeof(list[0]); ++i) {
        a500.bus().write16(0x10000u + static_cast<uint32_t>(i) * 2u, list[i]);
    }

    auto& c = a500.custom();
    c.write16(0x080, 0x0001);  // COP1LCH
    c.write16(0x082, 0x0000);  // COP1LCL
    c.write16(0x08E, 0x2C81);  // DIWSTRT
    c.write16(0x090, 0xF4C1);  // DIWSTOP
    c.write16(0x092, 0x0038);  // DDFSTRT
    c.write16(0x094, 0x00D0);  // DDFSTOP
    c.write16(0x100, 0x1200);  // 1 bitplane, COLOR
    c.write16(0x108, 0x0000);
    c.write16(0x10A, 0x0000);
    c.write16(0x096, 0x8380);  // DMAEN | BPLEN | COPEN

    auto tick = [&]() {
        bool idx = false;
        c.tick_cpu_cycles(8, false, false, idx);
    };

    // Finish the current frame so the Copper restarts from COP1LC at vblank.
    uint16_t prev = c.vpos();
    for (;;) {
        tick();
        const uint16_t now = c.vpos();
        if (now < prev) break;
        prev = now;
    }
    c.write16(0x0E0, 0x0002);  // BPL1PTH
    c.write16(0x0E2, 0x0000);  // BPL1PTL
    while (c.vpos() < 112) tick();

    const uint32_t* fb = c.framebuffer();
    const size_t w = amiga::CustomChips::FB_WIDTH;
    auto px = [&](int line, size_t x) { return fb[static_cast<size_t>(line - 28) * 2u * w + x]; };

    EXPECT_EQ(px(90, 300), px(90, 500));            // uniform red before the WAIT line
    EXPECT_EQ(px(100, 300), px(90, 300));           // left of the split is still red
    EXPECT_EQ(px(100, 500), px(110, 500));          // right of the split is already blue
    EXPECT_EQ(px(100, 300) != px(100, 500), true);  // colour changed in the middle of the line
}

void test_cpu_midline_palette_and_35ns_sprite() {
    amiga::Amiga500 a1200;
    a1200.set_model(amiga::AmigaModel::A1200);
    a1200.reset();
    a1200.bus().write8(0x00BFE201u, 0x03u);
    a1200.bus().write8(0x00BFE001u, 0x00u);  // OVL off

    auto& c = a1200.custom();
    c.write16(0x08E, 0x2C81);  // DIWSTRT
    c.write16(0x090, 0xF4C1);  // DIWSTOP

    // Advance to line 60, hpos = 0
    while (c.vpos() < 60) {
        bool idx = false;
        c.tick_cpu_cycles(454, false, false, idx);
    }

    // Set COLOR00 = red ($0F00) at start of line 60, advance halfway, then CPU writes green ($00F0)
    c.write16(0x180, 0x0F00);
    bool idx = false;
    c.tick_cpu_cycles(240, false, false, idx);
    c.write16(0x180, 0x00F0);
    c.tick_cpu_cycles(454 - 240, false, false, idx);  // finish line 60

    const uint32_t* fb = c.framebuffer();
    const size_t w = amiga::CustomChips::FB_WIDTH;
    const size_t row60 = static_cast<size_t>(60 - 28) * 2u * w;
    EXPECT_EQ(fb[row60 + 20] != fb[row60 + 400], true);  // CPU mid-line COLOR00 split
}

void test_cycle_stepped_blitter_and_dma_contention() {
    amiga::Amiga500 a500;
    a500.set_model(amiga::AmigaModel::A500);
    a500.reset();
    a500.bus().write8(0x00BFE201u, 0x03u);
    a500.bus().write8(0x00BFE001u, 0x00u);  // OVL off

    // Prepare 4 words in Chip RAM at $10000 and zero destination at $20000
    for (uint32_t i = 0; i < 4; ++i) {
        a500.bus().write16(0x010000u + i * 2u, static_cast<uint16_t>(0xA001u + i));
        a500.bus().write16(0x020000u + i * 2u, 0x0000u);
    }

    auto& c = a500.custom();
    c.write16(0x096u, 0x8240u);  // DMAEN | BLTEN
    c.write16(0x040u, 0x09F0u);  // BLTCON0: USEA | USED, D=A
    c.write16(0x042u, 0x0000u);  // BLTCON1
    c.write16(0x044u, 0xFFFFu);  // BLTAFWM
    c.write16(0x046u, 0xFFFFu);  // BLTALWM
    c.write16(0x064u, 0x0000u);  // BLTAMOD
    c.write16(0x066u, 0x0000u);  // BLTDMOD
    c.write16(0x050u, 0x0001u);  // BLTAPTH
    c.write16(0x052u, 0x0000u);  // BLTAPTL ($10000)
    c.write16(0x054u, 0x0002u);  // BLTDPTH
    c.write16(0x056u, 0x0000u);  // BLTDPTL ($20000)
    c.write16(0x09Cu, 0x0040u);  // Clear INTREQ BLIT

    // Start 1x4 word blit (BLTSIZE: height=1, width=4 -> (1 << 6) | 4 = 0x0044)
    c.write16(0x058u, 0x0044u);

    // Immediately after writing BLTSIZE, BBUSY ($4000) is set and destination words are not finished yet
    EXPECT_EQ((c.read16(0x002u) & 0x4000u) != 0u, true);
    EXPECT_EQ(a500.bus().read16(0x020006u), 0x0000u);
    EXPECT_EQ((c.read16(0x01Eu) & 0x0040u) == 0u, true);

    // Enable BLTPRI (Blitter Nasty, $0400) and verify CPU wait cycles on Chip RAM contention
    c.write16(0x096u, 0x8400u);  // Set BLTPRI
    EXPECT_EQ(c.cpu_chip_wait_cycles(false) >= 4u, true);

    // Step 1 word (4 CPU cycles for A+D channels) -> word 0 copied, word 3 still 0
    bool idx = false;
    c.tick_cpu_cycles(4u, false, false, idx);
    EXPECT_EQ(a500.bus().read16(0x020000u), 0xA001u);
    EXPECT_EQ(a500.bus().read16(0x020006u), 0x0000u);
    EXPECT_EQ((c.read16(0x002u) & 0x4000u) != 0u, true);

    // Step remaining 3 words (12 CPU cycles) -> all 4 words copied, BBUSY cleared, BLIT IRQ set
    c.tick_cpu_cycles(12u, false, false, idx);
    EXPECT_EQ(a500.bus().read16(0x020002u), 0xA002u);
    EXPECT_EQ(a500.bus().read16(0x020004u), 0xA003u);
    EXPECT_EQ(a500.bus().read16(0x020006u), 0xA004u);
    EXPECT_EQ((c.read16(0x002u) & 0x4000u) == 0u, true);
    EXPECT_EQ((c.read16(0x01Eu) & 0x0040u) != 0u, true);
}

void test_rtc_clock_dc0000() {
    // 1. A500 (MSM6242B at $DC0000)
    amiga::Amiga500 a500;
    a500.set_model(amiga::AmigaModel::A500);
    a500.reset();

    // Control F ($DC003F) defaults to 24-hour mode (bit 2 = 1 -> 0x4), BUSY bit in Control D ($DC0037) is 0
    EXPECT_EQ(a500.bus().read8(0x00DC003Fu) & 0x0Fu, 0x04u);
    EXPECT_EQ(a500.bus().read8(0x00DC0037u) & 0x02u, 0x00u);

    // Month (regs 8 & 9: $DC0023 and $DC0027) must be valid BCD in 1..12
    const uint8_t m1 = a500.bus().read8(0x00DC0023u) & 0x0Fu;
    const uint8_t m10 = a500.bus().read8(0x00DC0027u) & 0x0Fu;
    const uint8_t month = static_cast<uint8_t>(m10 * 10u + m1);
    EXPECT_EQ(month >= 1u && month <= 12u, true);

    // 2. A4000 (Ricoh RP5C01 at $DC0000 with 4 banks + NVRAM in banks 2 & 3)
    amiga::Amiga500 a4000;
    a4000.set_model(amiga::AmigaModel::A4000);
    a4000.reset();

    // Bank 0 month (regs 9 & 10: $DC0027 and $DC002B) must match valid 1..12
    const uint8_t rm1 = a4000.bus().read8(0x00DC0027u) & 0x0Fu;
    const uint8_t rm10 = a4000.bus().read8(0x00DC002Bu) & 0x0Fu;
    const uint8_t rmonth = static_cast<uint8_t>(rm10 * 10u + rm1);
    EXPECT_EQ(rmonth, month);

    // Select Bank 2 (Mode reg $DC0037 = 0x0A) and test battery-backed NVRAM nibbles
    a4000.bus().write8(0x00DC0037u, 0x0Au);
    a4000.bus().write8(0x00DC0007u, 0x05u);
    a4000.bus().write8(0x00DC000Bu, 0x0Cu);
    EXPECT_EQ(a4000.bus().read8(0x00DC0007u) & 0x0Fu, 0x05u);
    EXPECT_EQ(a4000.bus().read8(0x00DC000Bu) & 0x0Fu, 0x0Cu);
}

void test_serial_and_parallel_ports() {
    amiga::Amiga500 a500;
    a500.set_model(amiga::AmigaModel::A500);
    a500.reset();

    // 1. Parallel port (Centronics): CIA-B PRA ($BFD000) reports BUSY=0 (bit 0), POUT=0 (bit 1), SEL=1 (bit 2)
    const uint8_t ciab_pra = a500.bus().read8(0x00BFD000u);
    EXPECT_EQ(ciab_pra & 0x07u, 0x04u);

    // Configure CIA-A DDRB ($BFE301) for output and enable CIA-A FLAG interrupt (bit 4 = $10)
    a500.bus().write8(0x00BFE301u, 0xFFu);
    a500.bus().write8(0x00BFED01u, 0x90u);  // Set ICR mask for FLAG (/ACK)

    // Write "OK" across CIA-A PRB ($BFE101) and verify auto-/ACK pulse on CIA-A FLAG after E-clock tick
    a500.bus().write8(0x00BFE101u, 'O');
    a500.cia_a().tick_e_clocks(4u);
    EXPECT_EQ((a500.bus().read8(0x00BFED01u) & 0x10u) != 0u, true);

    a500.bus().write8(0x00BFE101u, 'K');
    a500.cia_a().tick_e_clocks(4u);
    EXPECT_EQ((a500.bus().read8(0x00BFED01u) & 0x10u) != 0u, true);

    uint8_t par_out[4] = {};
    EXPECT_EQ(a500.cia_a().drain_parallel_tx(par_out, sizeof(par_out)), 2u);
    EXPECT_EQ(par_out[0], static_cast<uint8_t>('O'));
    EXPECT_EQ(par_out[1], static_cast<uint8_t>('K'));

    // 2. Serial port (Paula UART): SERPER ($DFF032), SERDAT ($DFF030), SERDATR ($DFF018), TBE IRQ (INTREQ bit 0)
    auto& c = a500.custom();
    c.write16(0x032u, 372u);     // 9600 bps
    c.write16(0x09Cu, 0x0001u);  // Clear TBE in INTREQ
    c.write16(0x030u, 0x0100u | 'A');  // Stop bit + 'A'

    bool idx = false;
    c.tick_cpu_cycles(32u, false, false, idx);
    EXPECT_EQ((c.read16(0x018u) & 0x3000u), 0x3000u);  // TBE | TSRE set in SERDATR
    EXPECT_EQ((c.read16(0x01Eu) & 0x0001u) != 0u, true);  // TBE interrupt requested

    uint8_t ser_out[4] = {};
    EXPECT_EQ(c.drain_serial_tx(ser_out, sizeof(ser_out)), 1u);
    EXPECT_EQ(ser_out[0], static_cast<uint8_t>('A'));
}

void test_a500_512kb_chip_ram_switch() {
    amiga::Amiga500 a500;
    a500.set_model(amiga::AmigaModel::A500);
    a500.reset();
    a500.bus().write8(0x00BFE201u, 0x03u);
    a500.bus().write8(0x00BFE001u, 0x00u);  // OVL off

    // Default A500 rev 6a mode: 1 MB Chip RAM ($000000..$0FFFFF) + 512 KB Slow RAM ($C00000..$C7FFFF)
    EXPECT_EQ(a500.bus().chip_ram_size(), 1024u * 1024u);
    a500.bus().write16(0x080000u, 0x1234u);
    EXPECT_EQ(a500.bus().read16(0x080000u), 0x1234u);

    // Switch to classic A500 rev 5 mode: 512 KB Chip RAM ($000000..$07FFFF) + 512 KB Slow RAM ($C00000..$C7FFFF)
    a500.set_a500_chip_ram_512k(true);
    a500.bus().write8(0x00BFE201u, 0x03u);
    a500.bus().write8(0x00BFE001u, 0x00u);  // OVL off
    EXPECT_EQ(a500.bus().chip_ram_size(), 512u * 1024u);

    a500.bus().write16(0x07FFFEu, 0xCAFEu);
    EXPECT_EQ(a500.bus().read16(0x07FFFEu), 0xCAFEu);

    // $080000 is no longer mapped to Chip RAM, while $C00000 Slow RAM remains active
    a500.bus().write16(0x080000u, 0xBEEFu);
    EXPECT_EQ(a500.bus().read16(0x080000u), 0xFFFFu);
    a500.bus().write16(0xC00000u, 0xA501u);
    EXPECT_EQ(a500.bus().read16(0xC00000u), 0xA501u);
}

void test_a4000_native_ide_controller() {
    const std::string tmp_hdf = "/tmp/amiga_test_a4000_ide.hdf";
    {
        std::ofstream f(tmp_hdf, std::ios::binary | std::ios::trunc);
        std::vector<uint8_t> img(4096, 0);
        img[512] = 'A';
        img[513] = '4';
        img[514] = 'K';
        img[515] = '!';
        f.write(reinterpret_cast<const char*>(img.data()), static_cast<std::streamsize>(img.size()));
    }

    amiga::Amiga500 a4000;
    a4000.set_model(amiga::AmigaModel::A4000);
    a4000.reset();
    EXPECT_EQ(a4000.gayle_ide().attach_hdf(tmp_hdf), true);

    // 1. Issue ATA IDENTIFY DEVICE ($EC) at A4000 command register $00DD203C
    a4000.bus().write8(0x00DD2038u, 0xE0u);  // Master drive, LBA
    a4000.bus().write8(0x00DD203Cu, 0xECu);  // IDENTIFY DEVICE

    // Verify A4000 IDE interrupt status at $00DD203A (bit 7 = 1) and DRQ in Alternate Status ($00DD3038)
    EXPECT_EQ((a4000.bus().read8(0x00DD203Au) & 0x80u) != 0u, true);
    EXPECT_EQ((a4000.bus().read8(0x00DD3038u) & 0x08u) != 0u, true);

    // Reading primary Status ($00DD203C) clears the pending IDE interrupt at $00DD203A
    EXPECT_EQ((a4000.bus().read8(0x00DD203Cu) & 0x08u) != 0u, true);
    EXPECT_EQ((a4000.bus().read8(0x00DD203Au) & 0x80u) == 0u, true);

    // Drain 256 words of IDENTIFY data from $00DD2020
    uint16_t id0 = 0;
    for (int i = 0; i < 256; ++i) {
        const uint16_t w = a4000.bus().read16(0x00DD2020u);
        if (i == 0) id0 = w;
    }
    EXPECT_EQ(id0, 0x0040u);

    // 2. Read LBA 1 via A4000 taskfile registers ($00DD2028..$00DD203C) and data port ($00DD2020)
    a4000.bus().write8(0x00DD2028u, 1u);     // Sector count = 1
    a4000.bus().write8(0x00DD202Cu, 1u);     // LBA 7:0 = 1
    a4000.bus().write8(0x00DD2030u, 0u);     // LBA 15:8 = 0
    a4000.bus().write8(0x00DD2034u, 0u);     // LBA 23:16 = 0
    a4000.bus().write8(0x00DD2038u, 0xE0u);  // LBA mode
    a4000.bus().write8(0x00DD203Cu, 0x20u);  // READ SECTORS

    const uint16_t w0 = a4000.bus().read16(0x00DD2020u);
    const uint16_t w1 = a4000.bus().read16(0x00DD2020u);
    for (int i = 2; i < 256; ++i) {
        (void)a4000.bus().read16(0x00DD2020u);
    }
    EXPECT_EQ(w0, 0x4134u);  // "A4"
    EXPECT_EQ(w1, 0x4B21u);  // "K!"

    a4000.gayle_ide().detach();
    std::remove(tmp_hdf.c_str());
}

void test_a4000_motherboard_registers() {
    amiga::Amiga500 a4000;
    a4000.set_model(amiga::AmigaModel::A4000);
    a4000.reset();

    // Fat Gary ($DE0000 coldstart = $80, $DE0001 timeout enable = $80, $DE0002 timeout flag = $00)
    EXPECT_EQ(a4000.bus().read8(0x00DE0000u), 0x80u);
    EXPECT_EQ(a4000.bus().read8(0x00DE0001u), 0x80u);
    EXPECT_EQ(a4000.bus().read8(0x00DE0002u), 0x00u);

    // Ramsey rev 7 ($DE0043 = $0F) and Ramsey Control ($DE0003: Page + Burst + 16MB RAMSIZE = $0B)
    EXPECT_EQ(a4000.bus().read8(0x00DE0043u), 0x0Fu);
    EXPECT_EQ(a4000.bus().read8(0x00DE0003u), 0x08u);
    a4000.bus().write8(0x00DE0003u, 0x0Bu);
    EXPECT_EQ(a4000.bus().read8(0x00DE0003u), 0x0Bu);

    // Super Buster rev 11 ($DE0006 / $DE0018 low nibble = $0B)
    EXPECT_EQ(a4000.bus().read8(0x00DE0006u) & 0x0Fu, 0x0Bu);
    EXPECT_EQ(a4000.bus().read8(0x00DE0018u) & 0x0Fu, 0x0Bu);
}

void test_a600_ecs_and_memory_map() {
    amiga::Amiga500 a600;
    a600.set_model(amiga::AmigaModel::A600);
    a600.reset();
    a600.bus().write8(0x00BFE201u, 0x03u);
    a600.bus().write8(0x00BFE001u, 0x00u);  // OVL off

    // 1. ECS Super Agnus 8375 PAL ID ($20 in VPOSR bits 14..8) and ECS Super Denise 8373 ($00FC in DENISEID)
    EXPECT_EQ((a600.custom().read16(0x004u) >> 8) & 0x7Fu, 0x20u);
    EXPECT_EQ(a600.custom().read16(0x07Cu), 0x00FCu);

    // 2. Authentic A600 memory map: no Slow RAM at $C00000, switchable 1 MB / 2 MB (A601) Chip RAM
    EXPECT_EQ(a600.bus().slow_ram_size(), 0u);
    EXPECT_EQ(a600.bus().chip_ram_size(), 1024u * 1024u);
    a600.bus().write16(0x100000u, 0xA601u);
    EXPECT_EQ(a600.bus().read16(0x100000u), 0xFFFFu);  // Unmapped when 1 MB Chip

    a600.set_a600_chip_ram_2mb(true);
    a600.bus().write8(0x00BFE201u, 0x03u);
    a600.bus().write8(0x00BFE001u, 0x00u);  // OVL off
    EXPECT_EQ(a600.bus().chip_ram_size(), 2u * 1024u * 1024u);
    a600.bus().write16(0x100000u, 0xA601u);
    EXPECT_EQ(a600.bus().read16(0x100000u), 0xA601u);  // Mapped at $100000 when 2 MB (A601) enabled

    // 3. Gayle PCMCIA status/control registers ($DA8000..$DAB000)
    a600.bus().write8(0x00DAA000u, 0x7Cu);  // GAYLE_INTENA
    a600.bus().write8(0x00DAB000u, 0x09u);  // GAYLE_CFG
    EXPECT_EQ(a600.bus().read8(0x00DAA000u), 0x7Cu);
    EXPECT_EQ(a600.bus().read8(0x00DAB000u), 0x09u);
}

}  // namespace

int main() {
    test_ovl_boot_and_cia_irq();
    test_zorro2_fast_ram_autoconfig();
    test_a4000_zorro3_fast_ram();
    test_aga_32bit_chip_dma_contention();
    test_gayle_ide_controller();
    test_midline_copper_palette_change();
    test_cpu_midline_palette_and_35ns_sprite();
    test_cycle_stepped_blitter_and_dma_contention();
    test_rtc_clock_dc0000();
    test_serial_and_parallel_ports();
    test_a500_512kb_chip_ram_switch();
    test_a4000_native_ide_controller();
    test_a4000_motherboard_registers();
    test_a600_ecs_and_memory_map();

    if (g_failures == 0) {
        std::printf("All Amiga boot tests passed.\n");
        return EXIT_SUCCESS;
    }
    std::fprintf(stderr, "%d test failure(s).\n", g_failures);
    return EXIT_FAILURE;
}
