#include "amiga/machine.hpp"
#include "amiga/rom_loader.hpp"

#include <cstdio>
#include <string>
#include <vector>

int main(int argc, char **argv) {
  const std::string path =
      (argc > 1) ? argv[1] : "kickstart.rom";

  std::vector<uint8_t> rom;
  std::string err;
  if (!amiga::load_kickstart_file(path, rom, &err)) {
    std::fprintf(stderr, "Error: %s\n", err.c_str());
    return 1;
  }

  std::printf("Loaded ROM: %s (%zu bytes), header=%02X %02X %02X %02X\n",
              path.c_str(), rom.size(), rom[0], rom[1], rom[2], rom[3]);

  amiga::Amiga500 a500;
  if (path.find("a1200") != std::string::npos ||
      path.find("A1200") != std::string::npos) {
    a500.set_model(amiga::AmigaModel::A1200);
    std::printf("Model: Amiga 1200 (AGA / 68EC020 / 2MB Chip)\n");
  }
  if (!a500.load_rom(rom.data(), rom.size())) {
    std::fprintf(stderr, "Unsupported ROM size: %zu\n", rom.size());
    return 1;
  }
  if (argc > 2) {
    const std::string p2 = argv[2];
    if (p2.size() >= 4 && p2.substr(p2.size() - 4) == ".hdf") {
      a500.add_hdf(p2);
      std::printf("Added HDF: %s\n", p2.c_str());
    } else if (a500.insert_adf(p2)) {
      std::printf("Inserted ADF: %s\n", p2.c_str());
    } else {
      std::fprintf(stderr, "Failed to insert ADF: %s\n", p2.c_str());
    }
  }
  if (argc > 3) {
    a500.set_floppy_enabled(1, true);
    a500.insert_adf(1, argv[3]);
    std::printf("Inserted DF1: %s\n", argv[3]);
  }
  if (argc > 4) {
    a500.set_floppy_enabled(2, true);
    a500.insert_adf(2, argv[4]);
    std::printf("Inserted DF2: %s\n", argv[4]);
  }

  a500.reset();
  std::printf("Reset: SSP=$%08X PC=$%08X OVL=%d\n", a500.cpu().ssp(),
              a500.cpu().pc(), a500.bus().ovl_active() ? 1 : 0);

  uint16_t prev_color0 = a500.custom().color(0);
  uint64_t vbl_count = 0, ports_count = 0, exter_count = 0;
  for (int frame = 1; frame <= 4000; ++frame) {
    for (int s = 0; s < 200000; ++s) {
      const uint16_t prev_v = a500.custom().vpos();
      a500.step_instruction();
      if (prev_v == 311 && a500.custom().vpos() == 0)
        break;
    }
    if (frame == 3699 || frame == 3760 || frame == 3950) {
      const uint32_t spr0 =
          (static_cast<uint32_t>(a500.bus().read16(0x000426u)) << 16) |
          a500.bus().read16(0x00042Au);
      std::printf("F%d: JOY0DAT=%04X PRA=%02X SPR0_POS=%04X\n", frame,
                  a500.custom().read16(0x00Au), a500.cia_a().read(0),
                  a500.bus().read16(spr0));
    }
    const uint16_t c0 = a500.custom().color(0);
    if (c0 != prev_color0) {
      std::printf("Frame %d: COLOR00 -> $%03X (PC=$%08X DMACON=$%04X)\n", frame,
                  c0, a500.cpu().pc(), a500.custom().dmacon());
      prev_color0 = c0;
    }
  }
  std::printf("IRQs: PORTS=%llu VERTB=%llu EXTER=%llu\n",
              static_cast<unsigned long long>(ports_count),
              static_cast<unsigned long long>(vbl_count),
              static_cast<unsigned long long>(exter_count));
  std::printf("CIA-A: PRA=%02X CRA=%02X CRB=%02X ICR_M=%02X ICR_D=%02X "
              "TOD=%06X ALM=%06X\n",
              a500.cia_a().read(0), a500.cia_a().read(0xE),
              a500.cia_a().read(0xF), a500.cia_a().icr_mask(),
              a500.cia_a().icr_data(), a500.cia_a().tod(),
              a500.cia_a().tod_alarm());
  std::printf("CIA-B: PRB=%02X CRA=%02X CRB=%02X ICR_M=%02X ICR_D=%02X "
              "TOD=%06X ALM=%06X\n",
              a500.cia_b().read(1), a500.cia_b().read(0xE),
              a500.cia_b().read(0xF), a500.cia_b().icr_mask(),
              a500.cia_b().icr_data(), a500.cia_b().tod(),
              a500.cia_b().tod_alarm());

  const uint32_t *fb = a500.custom().framebuffer();
  constexpr int kFbPixels =
      amiga::CustomChips::FB_WIDTH * amiga::CustomChips::FB_HEIGHT;
  uint32_t first_px = fb[0];
  int non_first = 0;
  for (int i = 0; i < kFbPixels; ++i) {
    if (fb[i] != first_px)
      ++non_first;
  }

  std::printf("After 4000 frames: PC=$%08X SR=$%04X DMACON=$%04X BPLCON0=$%04X "
              "FMODE=$%04X DDF=$%04X..%04X COP1LC=$%06X INTENA=$%04X "
              "INTREQ=$%04X COLOR00=$%03X non_first_px=%d\n",
              a500.cpu().pc(), a500.cpu().sr(), a500.custom().dmacon(),
              a500.custom().bplcon0(), a500.custom().fmode(),
              a500.custom().ddfstrt(), a500.custom().ddfstop(),
              a500.custom().cop1lc(), a500.custom().intena(),
              a500.custom().intreq(), a500.custom().color(0), non_first);
  const uint32_t cop = a500.custom().cop1lc();
  std::printf("Copper list at $%06X:\n", cop);
  for (uint32_t i = 0; i < 40; ++i) {
    const uint16_t ir1 = a500.bus().read16(cop + i * 4u);
    const uint16_t ir2 = a500.bus().read16(cop + i * 4u + 2u);
    std::printf("  %02u: %04X %04X\n", i, ir1, ir2);
  }
  const uint32_t cop2 = a500.custom().cop2lc();
  std::printf("COP2LC=$%06X BPL1MOD=%d BPL2MOD=%d COLORS=%03X,%03X,%03X,%03X "
              "BLT(desc=%u fill=%u line=%u)\n",
              cop2, a500.custom().bpl1mod(), a500.custom().bpl2mod(),
              a500.custom().color(0), a500.custom().color(1),
              a500.custom().color(2), a500.custom().color(3),
              a500.custom().blt_desc_count(), a500.custom().blt_fill_count(),
              a500.custom().blt_line_count());
  std::printf("Copper2 list at $%06X:\n", cop2);
  for (uint32_t i = 0; i < 24; ++i) {
    const uint16_t ir1 = a500.bus().read16(cop2 + i * 4u);
    const uint16_t ir2 = a500.bus().read16(cop2 + i * 4u + 2u);
    std::printf("  %02u: %04X %04X\n", i, ir1, ir2);
  }
  std::printf("ASCII preview y=40..220 (step 4):\n");
  for (int y = 40; y < 220; y += 4) {
    char buf[81]{};
    for (int x = 0; x < 80; ++x) {
      const uint32_t px = fb[y * amiga::CustomChips::FB_WIDTH + x * 8];
      if (px == fb[0])
        buf[x] = '.';
      else if (px == 0xFF000000u)
        buf[x] = '#';
      else if ((px & 0xFFu) > 0x80u)
        buf[x] = '+';
      else
        buf[x] = '*';
    }
    std::printf("%3d: %s\n", y, buf);
  }
  std::printf("Floppy: cyl=%u side=%u motor=%d sel=%d\n",
              a500.floppy().cylinder(), a500.floppy().side(),
              a500.floppy().motor_on() ? 1 : 0,
              a500.floppy().selected() ? 1 : 0);
  std::printf("Mem at $2064: %08X %08X %08X %08X\n", a500.bus().read32(0x2064u),
              a500.bus().read32(0x2068u), a500.bus().read32(0x206Cu),
              a500.bus().read32(0x2070u));
  int sync_off = -1;
  for (int off = 0; off < 14000; off += 2) {
    if (a500.bus().read16(0x2064u + static_cast<uint32_t>(off)) == 0x4489u) {
      sync_off = off;
      break;
    }
  }
  std::printf("First 0x4489 in DMA buf at offset=%d\n", sync_off);
  std::printf("Code $040C38..$040CE0:\n");
  for (uint32_t a = 0x040C38u; a < 0x040CE0u; a += 2) {
    std::printf("  $%06X: %04X\n", a, a500.bus().read16(a));
  }
  std::printf("Code $0401E0..$040240:\n");
  for (uint32_t a = 0x0401E0u; a < 0x040240u; a += 2) {
    std::printf("  $%06X: %04X\n", a, a500.bus().read16(a));
  }
  std::printf("Copper $040F36..$040FB0:\n");
  for (uint32_t a = 0x040F36u; a < 0x040FB0u; a += 4) {
    std::printf("  $%06X: %04X %04X\n", a, a500.bus().read16(a),
                a500.bus().read16(a + 2u));
  }
  std::printf(
      "Regs: D0=%08X D1=%08X D2=%08X D3=%08X D4=%08X D5=%08X D6=%08X D7=%08X\n",
      a500.cpu().d(0), a500.cpu().d(1), a500.cpu().d(2), a500.cpu().d(3),
      a500.cpu().d(4), a500.cpu().d(5), a500.cpu().d(6), a500.cpu().d(7));
  std::printf(
      "      A0=%08X A1=%08X A2=%08X A3=%08X A4=%08X A5=%08X A6=%08X A7=%08X\n",
      a500.cpu().a(0), a500.cpu().a(1), a500.cpu().a(2), a500.cpu().a(3),
      a500.cpu().a(4), a500.cpu().a(5), a500.cpu().a(6), a500.cpu().a(7));
  std::printf("Stack $0580..$05F0:\n");
  for (uint32_t a = 0x0580u; a < 0x05F0u; a += 4) {
    std::printf("  $%04X: %08X\n", a, a500.bus().read32(a));
  }
  std::printf("Code around $03F1C0..$03F250:\n");
  for (uint32_t a = 0x03F1C0u; a < 0x03F250u; a += 2) {
    std::printf("  $%06X: %04X\n", a, a500.bus().read16(a));
  }
  std::printf("Code around $FC30A0..$FC30F0:\n");
  for (uint32_t a = 0xFC30A0u; a < 0xFC30F0u; a += 2) {
    std::printf("  $%06X: %04X\n", a, a500.bus().read16(a));
  }
  std::printf("Code around $000180..$000260:\n");
  for (uint32_t a = 0x180u; a < 0x260u; a += 2) {
    std::printf("  $%06X: %04X\n", a, a500.bus().read16(a));
  }
  std::printf("Code around PC=$%06X (A7=$%08X -> ret=$%08X $%08X):\n",
              a500.cpu().pc(), a500.cpu().a(7),
              a500.bus().read32(a500.cpu().a(7)),
              a500.bus().read32(a500.cpu().a(7) + 4u));
  for (uint32_t a = (a500.cpu().pc() & ~1u) - 16u;
       a < (a500.cpu().pc() & ~1u) + 24u; a += 2) {
    std::printf("  $%06X: %04X\n", a, a500.bus().read16(a));
  }
  return 0;
}
