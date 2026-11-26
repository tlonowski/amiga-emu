#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AmigaMachine AmigaMachine;

AmigaMachine* amiga_create(void);
void amiga_destroy(AmigaMachine* machine);
void amiga_set_model(AmigaMachine* machine, int model);
void amiga_set_a500_chip_ram_512k(AmigaMachine* machine, int enabled);
void amiga_set_a600_chip_ram_2mb(AmigaMachine* machine, int enabled);
void amiga_set_fast_ram_mb(AmigaMachine* machine, uint32_t mb);
int amiga_load_rom(AmigaMachine* machine, const uint8_t* data, size_t size);
int amiga_load_kickstart_path(AmigaMachine* machine, const char* path);
int amiga_insert_adf_path(AmigaMachine* machine, const char* path);
int amiga_insert_adf_unit(AmigaMachine* machine, int unit, const char* path);
void amiga_eject_adf(AmigaMachine* machine);
void amiga_eject_adf_unit(AmigaMachine* machine, int unit);
void amiga_set_floppy_enabled(AmigaMachine* machine, int unit, int enabled);
void amiga_set_floppy_write_protect(AmigaMachine* machine, int unit, int read_only);
void amiga_set_floppy_sound_enabled(AmigaMachine* machine, int enabled);
void amiga_clear_hdfs(AmigaMachine* machine);
int amiga_add_hdf_path(AmigaMachine* machine, const char* path);
int amiga_attach_gayle_ide_path(AmigaMachine* machine, const char* path);
void amiga_detach_gayle_ide(AmigaMachine* machine);
int amiga_create_blank_adf_path(const char* path, const char* volume_name);
int amiga_create_blank_hdf_path(const char* path, uint32_t size_mb, const char* volume_name);
void amiga_reset(AmigaMachine* machine);
uint32_t amiga_step_cpu(AmigaMachine* machine);
void amiga_step_frame(AmigaMachine* machine);
uint32_t amiga_get_pc(const AmigaMachine* machine);
uint16_t amiga_get_color0(const AmigaMachine* machine);
uint32_t amiga_get_led_state(const AmigaMachine* machine);
const uint32_t* amiga_framebuffer(const AmigaMachine* machine);
uint16_t amiga_fb_width(void);
uint16_t amiga_fb_height(void);
size_t amiga_read_audio(AmigaMachine* machine, float* out_stereo, size_t max_frames);
void amiga_key_event(AmigaMachine* machine, uint8_t rawkey, int pressed);
void amiga_set_joystick(AmigaMachine* machine, int up, int down, int left, int right, int fire);
void amiga_mouse_move(AmigaMachine* machine, int32_t dx, int32_t dy);
void amiga_mouse_buttons(AmigaMachine* machine, int left, int right);
size_t amiga_read_parallel_output(AmigaMachine* machine, uint8_t* out_buf, size_t max_len);
size_t amiga_read_serial_output(AmigaMachine* machine, uint8_t* out_buf, size_t max_len);
int amiga_save_arf_path(const AmigaMachine* machine, const char* path);
int amiga_load_arf_path(AmigaMachine* machine, const char* path);
int amiga_get_model(const AmigaMachine* machine);
int amiga_get_a500_chip_ram_512k(const AmigaMachine* machine);
int amiga_get_a600_chip_ram_2mb(const AmigaMachine* machine);
uint32_t amiga_get_fast_ram_mb(const AmigaMachine* machine);
int amiga_get_floppy_enabled(const AmigaMachine* machine, int unit);
int amiga_get_floppy_write_protect(const AmigaMachine* machine, int unit);
const char* amiga_get_floppy_path(const AmigaMachine* machine, int unit);
uint8_t amiga_peek_ram8(const AmigaMachine* machine, uint32_t addr);
void amiga_poke_ram8(AmigaMachine* machine, uint32_t addr, uint8_t val);
uint32_t amiga_get_chip_ram_size(const AmigaMachine* machine);
uint32_t amiga_get_slow_ram_size(const AmigaMachine* machine);
uint32_t amiga_get_fast_ram_base(const AmigaMachine* machine);
uint32_t amiga_get_fast_ram_size(const AmigaMachine* machine);
uint32_t amiga_get_mb_fast_ram_size(const AmigaMachine* machine);
uint32_t amiga_get_d_reg(const AmigaMachine* machine, int reg);
void amiga_set_d_reg(AmigaMachine* machine, int reg, uint32_t val);
uint32_t amiga_get_a_reg(const AmigaMachine* machine, int reg);
void amiga_set_a_reg(AmigaMachine* machine, int reg, uint32_t val);
uint16_t amiga_get_sr(const AmigaMachine* machine);
void amiga_set_pc(AmigaMachine* machine, uint32_t val);

#ifdef __cplusplus
}
#endif
