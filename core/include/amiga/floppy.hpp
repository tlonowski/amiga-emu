#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace amiga {

/// Amiga 3.5" DD floppy drive (DF0:) with ADF-to-MFM track encoding.
class FloppyDrive {
public:
    static constexpr size_t ADF_SIZE = 80 * 2 * 11 * 512;  // 901,120 bytes
    static constexpr size_t TRACK_WORDS = 6334;             // ~12,668 bytes per MFM track

    void reset();
    void set_enabled(bool enabled) { enabled_ = enabled; }
    bool enabled() const { return enabled_; }
    void set_select_mask(uint8_t sel_bit_mask) { sel_bit_mask_ = sel_bit_mask; }

    bool insert_adf(const uint8_t* data, size_t size);
    bool insert_adf_file(const std::string& path);
    void eject();
    void set_write_protected(bool ro) { write_protected_ = ro; }
    bool write_protected() const { return write_protected_; }

    bool has_disk() const { return has_disk_; }
    uint8_t cylinder() const { return cylinder_; }
    uint8_t side() const { return side_; }
    bool motor_on() const { return motor_on_; }
    bool selected() const { return selected_; }
    bool consume_step_sound() {
        const bool v = pending_step_sound_;
        pending_step_sound_ = false;
        return v;
    }

    /// Updates drive control lines from CIA-B PRB ($BFD100, active-low).
    void write_cia_b_prb(uint8_t prb);

    /// Returns CIA-A PRA bits 5..2 (/RDY, /TK0, /WPRO, /CHNG) for DF0:.
    uint8_t cia_a_status_bits() const;

    /// Reads next 16-bit MFM word from the current track and advances head position.
    /// Sets out_index=true when the track wraps around.
    uint16_t read_mfm_word(bool& out_index);
    void write_mfm_word(uint16_t word);
    void finish_write_track();

private:
    void encode_track(uint8_t cyl, uint8_t side);
    static void mfm_encode_block(const uint8_t* even_src, const uint8_t* odd_src,
                                 size_t len, uint8_t* mfm_dst, uint8_t& prev_bit);
    static void mfm_encode_checksum(uint32_t sum_bits, uint8_t* mfm_dst8, uint8_t& prev_bit);
    static uint8_t mfm_encode_nibble_pair(uint8_t data_bits, uint8_t& prev_bit);

    std::vector<uint8_t> adf_data_;
    std::string disk_path_;
    std::array<uint16_t, TRACK_WORDS> track_mfm_{};
    std::vector<uint16_t> write_buf_;
    size_t track_pos_ = 0;

    bool has_disk_ = false;
    bool write_protected_ = false;
    bool disk_changed_ = false;  // /CHNG is active-low (0 = changed/no disk, 1 = present)
    bool enabled_ = true;
    uint8_t sel_bit_mask_ = 0x08u;  // 0x08=DF0, 0x10=DF1, 0x20=DF2, 0x40=DF3
    bool id_mode_ = false;
    bool selected_ = false;
    bool motor_on_ = false;
    uint8_t cylinder_ = 0;
    uint8_t side_ = 0;
    uint8_t prev_prb_ = 0xFF;
    uint8_t cached_track_ = 0xFF;
    bool pending_step_sound_ = false;
};

}  // namespace amiga
