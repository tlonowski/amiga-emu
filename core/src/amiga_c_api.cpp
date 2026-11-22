#include "amiga/amiga_c_api.h"
#include "amiga/machine.hpp"
#include "amiga/rom_loader.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <vector>

struct AmigaMachine {
    amiga::Amiga500 a500;
};

AmigaMachine* amiga_create(void) {
    return new AmigaMachine();
}

void amiga_destroy(AmigaMachine* machine) {
    delete machine;
}

void amiga_set_model(AmigaMachine* machine, int model) {
    if (!machine) return;
    if (model == 3) machine->a500.set_model(amiga::AmigaModel::A4000);
    else if (model == 2) machine->a500.set_model(amiga::AmigaModel::A1200);
    else if (model == 1) machine->a500.set_model(amiga::AmigaModel::A600);
    else machine->a500.set_model(amiga::AmigaModel::A500);
}

void amiga_set_fast_ram_mb(AmigaMachine* machine, uint32_t mb) {
    if (!machine) return;
    machine->a500.set_fast_ram_mb(mb);
}

int amiga_load_rom(AmigaMachine* machine, const uint8_t* data, size_t size) {
    if (!machine) return 0;
    return machine->a500.load_rom(data, size) ? 1 : 0;
}

int amiga_load_kickstart_path(AmigaMachine* machine, const char* path) {
    if (!machine || !path) return 0;
    std::vector<uint8_t> rom;
    if (!amiga::load_kickstart_file(path, rom)) {
        return 0;
    }
    return machine->a500.load_rom(rom.data(), rom.size()) ? 1 : 0;
}

int amiga_insert_adf_path(AmigaMachine* machine, const char* path) {
    return amiga_insert_adf_unit(machine, 0, path);
}

int amiga_insert_adf_unit(AmigaMachine* machine, int unit, const char* path) {
    if (!machine || !path || unit < 0 || unit >= 4) return 0;
    return machine->a500.insert_adf(static_cast<size_t>(unit), path) ? 1 : 0;
}

void amiga_eject_adf(AmigaMachine* machine) {
    amiga_eject_adf_unit(machine, 0);
}

void amiga_eject_adf_unit(AmigaMachine* machine, int unit) {
    if (machine && unit >= 0 && unit < 4) {
        machine->a500.eject_adf(static_cast<size_t>(unit));
    }
}

void amiga_set_floppy_enabled(AmigaMachine* machine, int unit, int enabled) {
    if (machine && unit >= 0 && unit < 4) {
        machine->a500.set_floppy_enabled(static_cast<size_t>(unit), enabled != 0);
    }
}

void amiga_set_floppy_write_protect(AmigaMachine* machine, int unit, int read_only) {
    if (machine && unit >= 0 && unit < 4) {
        machine->a500.floppy(static_cast<size_t>(unit)).set_write_protected(read_only != 0);
    }
}

void amiga_clear_hdfs(AmigaMachine* machine) {
    if (machine) {
        machine->a500.clear_hdfs();
    }
}

int amiga_add_hdf_path(AmigaMachine* machine, const char* path) {
    if (!machine || !path) return 0;
    return machine->a500.add_hdf(path) != static_cast<size_t>(-1) ? 1 : 0;
}

namespace {
void write_be32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>((v >> 24) & 0xFFu);
    p[1] = static_cast<uint8_t>((v >> 16) & 0xFFu);
    p[2] = static_cast<uint8_t>((v >> 8) & 0xFFu);
    p[3] = static_cast<uint8_t>(v & 0xFFu);
}

uint32_t read_be32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}
}  // namespace

int amiga_create_blank_adf_path(const char* path, const char* volume_name) {
    if (!path) return 0;
    constexpr size_t kAdfSize = 80 * 2 * 11 * 512;  // 901,120 bytes
    std::vector<uint8_t> adf(kAdfSize, 0);

    // Bootblock (sectors 0..1): DOS\0 OFS
    adf[0] = 'D'; adf[1] = 'O'; adf[2] = 'S'; adf[3] = 0;
    write_be32(&adf[8], 880);  // Rootblock sector

    uint32_t boot_sum = 0;
    for (size_t i = 0; i < 1024; i += 4) {
        if (i == 4) continue;
        const uint32_t w = read_be32(&adf[i]);
        const uint32_t prev = boot_sum;
        boot_sum += w;
        if (boot_sum < prev) ++boot_sum;
    }
    write_be32(&adf[4], ~boot_sum);

    // Rootblock at sector 880
    uint8_t* root = &adf[880u * 512u];
    write_be32(&root[0], 2);           // T_HEADER
    write_be32(&root[12], 72);         // ht_size = 72 longs
    write_be32(&root[312], 0xFFFFFFFFu); // bm_flag = valid
    write_be32(&root[316], 881);       // bm_pages[0] = sector 881

    const char* vol = (volume_name && volume_name[0] != '\0') ? volume_name : "Empty";
    const size_t vlen = std::min<size_t>(std::strlen(vol), 30);
    root[432] = static_cast<uint8_t>(vlen);
    std::memcpy(&root[433], vol, vlen);
    write_be32(&root[504], 0);         // extension
    write_be32(&root[508], 1);         // ST_ROOT

    uint32_t root_sum = 0;
    for (size_t i = 0; i < 512; i += 4) {
        if (i == 20) continue;
        root_sum += read_be32(&root[i]);
    }
    write_be32(&root[20], static_cast<uint32_t>(-static_cast<int32_t>(root_sum)));

    // Bitmap block at sector 881 (1758 data blocks starting at sector 2)
    uint8_t* bm = &adf[881u * 512u];
    for (size_t b = 2; b < 1760; ++b) {
        if (b == 880 || b == 881) continue;  // mark root and bitmap as used (0)
        const size_t bit_idx = b - 2;
        const size_t long_idx = 1 + (bit_idx / 32);
        const uint32_t bit = 1u << (bit_idx % 32);
        const uint32_t cur = read_be32(&bm[long_idx * 4]);
        write_be32(&bm[long_idx * 4], cur | bit);
    }
    uint32_t bm_sum = 0;
    for (size_t i = 4; i < 512; i += 4) {
        bm_sum += read_be32(&bm[i]);
    }
    write_be32(&bm[0], static_cast<uint32_t>(-static_cast<int32_t>(bm_sum)));

    std::ofstream out(path, std::ios::binary);
    if (!out) return 0;
    out.write(reinterpret_cast<const char*>(adf.data()), static_cast<std::streamsize>(adf.size()));
    return out.good() ? 1 : 0;
}

int amiga_create_blank_hdf_path(const char* path, uint32_t size_mb, const char* volume_name) {
    if (!path || size_mb == 0 || size_mb > 1024) return 0;
    const uint64_t total_bytes = static_cast<uint64_t>(size_mb) * 1024ull * 1024ull;
    const uint32_t total_blocks = static_cast<uint32_t>(total_bytes / 512ull);
    const uint32_t root_block = total_blocks / 2u;
    const uint32_t bm_block = root_block + 1u;

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return 0;

    std::vector<uint8_t> chunk(1024u * 1024u, 0);
    for (uint32_t mb = 0; mb < size_mb; ++mb) {
        out.write(reinterpret_cast<const char*>(chunk.data()), static_cast<std::streamsize>(chunk.size()));
        if (!out) return 0;
    }

    // Write minimal DOS\1 (FFS) bootblock at sector 0
    std::vector<uint8_t> sec0(512, 0);
    sec0[0] = 'D'; sec0[1] = 'O'; sec0[2] = 'S'; sec0[3] = 1;
    write_be32(&sec0[8], root_block);
    out.seekp(0);
    out.write(reinterpret_cast<const char*>(sec0.data()), 512);

    // Write Rootblock at root_block
    std::vector<uint8_t> root(512, 0);
    write_be32(&root[0], 2);           // T_HEADER
    write_be32(&root[12], 72);         // ht_size
    write_be32(&root[312], 0xFFFFFFFFu);
    write_be32(&root[316], bm_block);
    const char* vol = (volume_name && volume_name[0] != '\0') ? volume_name : "Work";
    const size_t vlen = std::min<size_t>(std::strlen(vol), 30);
    root[432] = static_cast<uint8_t>(vlen);
    std::memcpy(&root[433], vol, vlen);
    write_be32(&root[508], 1);         // ST_ROOT
    uint32_t root_sum = 0;
    for (size_t i = 0; i < 512; i += 4) {
        if (i == 20) continue;
        root_sum += read_be32(&root[i]);
    }
    write_be32(&root[20], static_cast<uint32_t>(-static_cast<int32_t>(root_sum)));
    out.seekp(static_cast<std::streamoff>(root_block) * 512);
    out.write(reinterpret_cast<const char*>(root.data()), 512);

    return out.good() ? 1 : 0;
}

void amiga_reset(AmigaMachine* machine) {
    if (machine) {
        machine->a500.reset();
    }
}

uint32_t amiga_step_cpu(AmigaMachine* machine) {
    return machine ? machine->a500.step_instruction() : 0;
}

void amiga_step_frame(AmigaMachine* machine) {
    if (machine) {
        machine->a500.step_frame();
    }
}

uint32_t amiga_get_pc(const AmigaMachine* machine) {
    return machine ? machine->a500.cpu().pc() : 0;
}

uint16_t amiga_get_color0(const AmigaMachine* machine) {
    return machine ? machine->a500.custom().color(0) : 0;
}

uint32_t amiga_get_led_state(const AmigaMachine* machine) {
    return machine ? machine->a500.led_state() : 0;
}

const uint32_t* amiga_framebuffer(const AmigaMachine* machine) {
    return machine ? machine->a500.custom().framebuffer() : nullptr;
}

uint16_t amiga_fb_width(void) {
    return amiga::CustomChips::FB_WIDTH;
}

uint16_t amiga_fb_height(void) {
    return amiga::CustomChips::FB_HEIGHT;
}

size_t amiga_read_audio(AmigaMachine* machine, float* out_stereo, size_t max_frames) {
    return machine ? machine->a500.custom().read_audio(out_stereo, max_frames) : 0;
}

void amiga_key_event(AmigaMachine* machine, uint8_t rawkey, int pressed) {
    if (machine) {
        machine->a500.key_event(rawkey, pressed != 0);
    }
}

void amiga_set_joystick(AmigaMachine* machine, int up, int down, int left, int right, int fire) {
    if (machine) {
        machine->a500.set_joystick(up != 0, down != 0, left != 0, right != 0, fire != 0);
    }
}

void amiga_mouse_move(AmigaMachine* machine, int32_t dx, int32_t dy) {
    if (machine) {
        machine->a500.mouse_move(dx, dy);
    }
}

void amiga_mouse_buttons(AmigaMachine* machine, int left, int right) {
    if (machine) {
        machine->a500.set_mouse_buttons(left != 0, right != 0);
    }
}
