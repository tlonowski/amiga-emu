# AmigaEmu

**Version:** `1.4.29`

Native Amiga emulator for macOS (Apple Silicon & Intel) written in **C++20** (`AmigaCore`) and **Swift / AppKit / Metal** (`AmigaEmuApp`), licensed under the **GNU General Public License v3.0 (GPL-3.0)**.

## Features

- **CPU & Memory**
  - Motorola **68000** (Amiga 500 / 600), **68EC020** (Amiga 1200), and **68030 + 68882 FPU** (Amiga 4000)
  - **68020/68030 Instruction Cache** (256 B I-Cache + IBE burst), **68030 Data Cache** (256 B D-Cache + DBE burst + hardware `/CIIN`), and **68030 MMU** (`TC`, `CRP`, `SRP`, `TT0`, `TT1`, `MMUSR`, `PMOVE`, `PTEST`, `PFLUSH`)
  - **MC68882 FPU** with `FSAVE`/`FRESTORE` state frames, trig/exponential ops, and packed-decimal BCD
  - Configurable Chip RAM: **512 KB / 1 MB** (A500 rev 5/6a), **1 MB / 2 MB** (A600 / A601), **2 MB** (A1200 / A4000), Slow RAM (A500), 24-bit AutoConfig Fast RAM (0 / 2 / 4 / 8 MB), and **32-bit Zorro III Fast RAM** (0 / 16 / 64 / 128 MB)
  - Kickstart ROM loader (1.2, 1.3, 2.04, 2.05, 3.0, 3.1, 3.X) with automatic OVL boot mapping and `*-ext.rom` Extended ROM support (`$E00000`)

- **Chipset (OCS / ECS / AGA)**
  - **Custom chips (Agnus / Denise / Lisa / Paula)**: Copper, **cycle-stepped Blitter** (desc, fill, line mode), full DMA slot contention, hardware sprites (16/32/64-bit, attached 16-color, `BPLCON2` priority, `BPLCON4` sprite banks, 35 ns `SH1:SH0` positioning)
  - **ECS & AGA identification**: `Super Agnus 8375` / `Super Denise 8373` (A600) and `Alice` / `Lisa` (A1200 / A4000), plus A4000 motherboard registers (`Fat Gary`, `Ramsey rev 7`, `Super Buster rev 11`)
  - **Video modes**: Lores, Hires, Super-Hires (`SHRES` with 35 ns `PFxH0` scroll and 2× sprite supersampling), Interlace (`LACE`), Dual Playfield (OCS/ECS/AGA), `HAM-6` & `HAM-8`, `EHB`, `BRDRBLNK`, 64-bit AGA bitplane fetch (`FMODE`), and mid-scanline Copper & CPU palette/background updates
  - **Audio**: 4-channel Paula DMA audio streamed via `AVFoundation` with optional synthesized floppy drive step/motor sound
  - **Storage, RTC & I/O**: `DF0:`–`DF3:` hot-detectable floppy drives (`.adf`), RDB/OFS/FFS/SFS & Dynamic VHD hard drives (`.hdf`) via AutoConfig HD and native **Gayle / A4000 IDE** (`$DA2000` / `$DD2000`), RTC (`$DC0000` MSM6242B / RP5C01), parallel Centronics & serial UART ports, CIA-A/CIA-B timers, mouse, keyboard, and joystick

- **Native macOS UI**
  - **Metal** hardware-accelerated rendering with Sharp Bilinear filtering, 4:3 aspect-ratio preservation, borderless fullscreen (`Cmd+F`), and optional widescreen stretch (`Cmd+Shift+F`)
  - Built-in machine profiles: **Amiga 500**, **Amiga 600**, **Amiga 1200**, and **Amiga 4000**
  - Authentic A1200-style `POWER` / `F. DISK` / `H. DISK` LED bar and per-drive activity indicators
  - Folder-tree `.adf` disk browser with automatic multi-disk mounting (`DF0:`–`DF3:` for folders with 1–4 `.adf` files)
  - **Amiga-to-macOS printing** (`Cmd+P` or auto-print from `PRT:` / `PAR:` / `SER:` with ANSI & Epson `ESC/P` formatting to native macOS Print dialog / PDF / AirPrint)
  - Monokai dark theme and **English / Polish** UI localization

## Building & Running

### Requirements
- macOS 13.0+
- Xcode Command Line Tools (`clang++`, `swift`)

### Run unit tests
```sh
make test
```

### Build `.app` bundle
```sh
make app
open build/AmigaEmu.app
```

On first launch, AmigaEmu automatically creates the directory structure in your home folder (`~/AmigaEmu/Kickstart`, `~/AmigaEmu/adf`, and `~/AmigaEmu/Hard Drives`), or you can select ROMs, `.adf`, and `.hdf` files directly in the UI.

## Project Structure

- `core/` — C++20 emulation core (`m68k`, `custom`, `cia`, `floppy`, `hard_drive`, `amiga_bus`, `rom_loader`) and C API (`amiga_c_api.h`)
- `macos/Sources/` — Native macOS AppKit & Metal frontend (`MetalView`, `Shaders.metal`, `AppIcon`, `MonokaiTheme`, `L10n`, sidebar & disk browser)
- `tests/` — CPU (`test_m68k`) and Amiga boot/chipset (`test_amiga_boot`) tests
- `tools/` — `build_app.sh` app bundler and `kickstart_probe` diagnostic tool

## Versioning

Version numbers follow the `1.<machines>.<fixes>` convention (stored in [`VERSION`](VERSION)):
- **`1`** — major release line
- **`<machines>`** — number of supported Amiga machine profiles (currently **4**: Amiga 500, Amiga 600, Amiga 1200, Amiga 4000)
- **`<fixes>`** — code fixes, compatibility improvements, and bugfix releases

## Author & License

Created by **Tomasz Lonowski**.

This program is free software: you can redistribute it and/or modify it under the terms of the **GNU General Public License** as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version. See [LICENSE](LICENSE) for details.
