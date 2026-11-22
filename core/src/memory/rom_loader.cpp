#include "amiga/rom_loader.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>

namespace amiga {

namespace {

bool read_file_bytes(const std::filesystem::path& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    const auto sz = f.tellg();
    if (sz <= 0) return false;
    out.resize(static_cast<size_t>(sz));
    f.seekg(0);
    return static_cast<bool>(f.read(reinterpret_cast<char*>(out.data()), sz));
}

}  // namespace

bool load_kickstart_file(const std::string& rom_path, std::vector<uint8_t>& out_rom, std::string* out_error) {
    std::vector<uint8_t> raw;
    if (!read_file_bytes(rom_path, raw)) {
        if (out_error) *out_error = "Cannot read ROM file: " + rom_path;
        return false;
    }

    constexpr char kMagic[] = "AMIROMTYPE1";
    constexpr size_t kMagicLen = 11;
    if (raw.size() > kMagicLen && std::memcmp(raw.data(), kMagic, kMagicLen) == 0) {
        const auto key_path = std::filesystem::path(rom_path).parent_path() / "rom.key";
        std::vector<uint8_t> key;
        if (!read_file_bytes(key_path, key)) {
            if (out_error) *out_error = "Encrypted ROM requires rom.key at: " + key_path.string();
            return false;
        }
        const size_t rom_size = raw.size() - kMagicLen;
        out_rom.resize(rom_size);
        for (size_t i = 0; i < rom_size; ++i) {
            out_rom[i] = raw[kMagicLen + i] ^ key[i % key.size()];
        }
        return true;
    }

    out_rom = std::move(raw);
    return true;
}

}  // namespace amiga
