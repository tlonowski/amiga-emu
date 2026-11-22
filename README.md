# AmigaEmu

**Version:** `1.0.0`

Native Amiga emulator for macOS (Apple Silicon & Intel) written in **C++20** (`AmigaCore`) and **Swift / AppKit / Metal** (`AmigaEmuApp`), licensed under the **GNU General Public License v3.0 (GPL-3.0)**.

## Features

- **CPU & Memory**
  - Motorola **68000** (Amiga 500 / 600) and **68EC020** + **68881/68882 FPU** (Amiga 1200)
  - Chip RAM (512 KB – 2 MB), Slow RAM, and **Fast RAM** (0 / 2 / 4 / 8 MB, 24-bit AutoConfig)
  - Kickstart ROM loader (1.3, 2.05, 3.1) with automatic OVL boot mapping

- **Chipset (OCS / ECS / AGA)**
  - **Custom chips (Agnus / Denise / Lisa / Paula)**: Copper, Blitter (desc, fill, line mode), hardware sprites (16/32/64-bit, attached 16-color, `BPLCON2` priority, `BPLCON4` sprite banks)
  - **Video modes**: Lores, Hires, Super-Hires (`SHRES`), Interlace (`LACE`) with flicker-free field merging, Dual Playfield (OCS/ECS/AGA), `HAM-6` & `HAM-8`, `EHB`, and 64-bit AGA bitplane fetch (`FMODE`)
  - **Audio**: 4-channel Paula DMA audio streamed via `AVFoundation`
  - **Storage & I/O**: `DF0:`–`DF3:` floppy drives (`.adf`), RDB/OFS/FFS/SFS & Dynamic VHD hard drive image support (`.hdf`), CIA-A / CIA-B timers, mouse, keyboard, and joystick emulation

- **Native macOS UI**
  - **Metal** hardware-accelerated rendering with Sharp Bilinear filtering, 4:3 aspect-ratio preservation, borderless fullscreen (`Cmd+F`), and optional widescreen stretch (`Cmd+Shift+F`)
  - Built-in machine profiles: **Amiga 500**, **Amiga 600**, and **Amiga 1200**
  - Folder-tree ADF disk browser (double-click to mount in `DF0:`)
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
- **`<machines>`** — number of supported Amiga machine profiles (currently **3**: Amiga 500, Amiga 600, Amiga 1200)
- **`<fixes>`** — code fixes, compatibility improvements, and bugfix releases

## Author & License

Created by **Tomasz Lonowski**.

This program is free software: you can redistribute it and/or modify it under the terms of the **GNU General Public License** as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version. See [LICENSE](LICENSE) for details.
