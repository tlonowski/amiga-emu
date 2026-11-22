#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace amiga {

/// Loads a Kickstart ROM file, transparently decrypting Amiga Forever AMIROMTYPE1 files using rom.key.
bool load_kickstart_file(const std::string& rom_path, std::vector<uint8_t>& out_rom, std::string* out_error = nullptr);

}  // namespace amiga
