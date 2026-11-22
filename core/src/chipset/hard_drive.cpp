#include "amiga/hard_drive.hpp"
#include "amiga/bus.hpp"

#include <algorithm>
#include <cstring>

namespace amiga {

namespace {

void emit16(std::vector<uint8_t>& buf, size_t off, uint16_t v) {
    buf[off] = static_cast<uint8_t>(v >> 8);
    buf[off + 1] = static_cast<uint8_t>(v & 0xFFu);
}

void emit32(std::vector<uint8_t>& buf, size_t off, uint32_t v) {
    buf[off] = static_cast<uint8_t>((v >> 24) & 0xFFu);
    buf[off + 1] = static_cast<uint8_t>((v >> 16) & 0xFFu);
    buf[off + 2] = static_cast<uint8_t>((v >> 8) & 0xFFu);
    buf[off + 3] = static_cast<uint8_t>(v & 0xFFu);
}

class CodeEmitter {
public:
    CodeEmitter(std::vector<uint8_t>& rom, size_t start_off)
        : rom_(rom), pos_(start_off) {}

    size_t pos() const { return pos_; }
    void w16(uint16_t v) {
        emit16(rom_, pos_, v);
        pos_ += 2;
    }
    void w32(uint32_t v) {
        emit32(rom_, pos_, v);
        pos_ += 4;
    }
    void str(const char* s) {
        while (*s) {
            rom_[pos_++] = static_cast<uint8_t>(*s++);
        }
        rom_[pos_++] = 0;
        if (pos_ & 1u) rom_[pos_++] = 0;
    }

private:
    std::vector<uint8_t>& rom_;
    size_t pos_;
};

uint32_t read_be32_bus(Bus& bus, uint32_t addr) {
    return bus.read32(addr);
}

void write_be32_bus(Bus& bus, uint32_t addr, uint32_t val) {
    bus.write32(addr, val);
}

}  // namespace

void HardDriveController::reset() {
    configured_ = false;
    shut_up_ = false;
    base_addr_ = 0;
    selected_unit_ = 0;
    if (rom_.empty()) {
        build_rom();
    }
}

void HardDriveController::clear_units() {
    units_.clear();
}

size_t HardDriveController::add_hdf(const std::string& path) {
    auto u = std::make_unique<Unit>();
    u->path = path;
    u->file.open(path, std::ios::in | std::ios::out | std::ios::binary);
    if (!u->file.is_open()) {
        u->file.open(path, std::ios::in | std::ios::binary);
    }
    if (!u->file.is_open()) {
        return static_cast<size_t>(-1);
    }
    u->file.seekg(0, std::ios::end);
    u->size_bytes = static_cast<uint64_t>(u->file.tellg());
    if (u->size_bytes < 512u * 32u) {
        return static_cast<size_t>(-1);
    }

    uint8_t hdr[4]{};
    u->file.seekg(0, std::ios::beg);
    u->file.read(reinterpret_cast<char*>(hdr), 4);
    if (hdr[0] == 'D' && hdr[1] == 'O' && hdr[2] == 'S' && hdr[3] <= 7) {
        u->dostype = (static_cast<uint32_t>(hdr[0]) << 24) |
                     (static_cast<uint32_t>(hdr[1]) << 16) |
                     (static_cast<uint32_t>(hdr[2]) << 8) |
                     static_cast<uint32_t>(hdr[3]);
    } else {
        u->dostype = 0x444F5301u;  // DOS\1
    }

    u->cylinders = static_cast<uint32_t>(u->size_bytes / (32ull * 512ull));
    const size_t idx = units_.size();
    u->dos_name = "DH" + std::to_string(idx);
    u->boot_pri = (idx == 0) ? 0 : -128;
    units_.push_back(std::move(u));
    return idx;
}

void HardDriveController::remove_hdf(size_t index) {
    if (index >= units_.size()) return;
    units_.erase(units_.begin() + static_cast<std::ptrdiff_t>(index));
    for (size_t i = 0; i < units_.size(); ++i) {
        units_[i]->dos_name = "DH" + std::to_string(i);
        units_[i]->boot_pri = (i == 0) ? 0 : -128;
    }
}

void HardDriveController::build_rom() {
    rom_.assign(0x2000u, 0);

    // DiagArea at $0100 (relative to board base)
    rom_[0x0100] = 0x90;  // DAC_WORDWIDE | DAC_CONFIGTIME
    rom_[0x0101] = 0x00;  // da_Flags
    emit16(rom_, 0x0102, 0x0800);  // da_Size
    emit16(rom_, 0x0104, 0x0020);  // da_DiagPoint -> $0120
    emit16(rom_, 0x0106, 0x0300);  // da_BootPoint -> $0400 (BootEntry)

    const size_t off_dev_name = 0x0500;
    const size_t off_exp_name = 0x0520;
    {
        CodeEmitter se(rom_, off_dev_name);
        se.str("amigahd.device");
        CodeEmitter se2(rom_, off_exp_name);
        se2.str("expansion.library");
    }

    // $0400: BootEntry() -> A6 = SysBase, FindResident("dos.library") + InitResident
    {
        CodeEmitter e(rom_, 0x0400);
        e.w16(0x2C78); e.w16(0x0004);  // MOVEA.L $4.W, A6
        e.w16(0x43FA); e.w16(0x0016);  // LEA dosname(PC), A1
        e.w16(0x4EAE); e.w16(0xFFA0);  // JSR -$60(A6)  (FindResident)
        e.w16(0x4A80);                 // TST.L D0
        e.w16(0x6708);                 // BEQ.S +8
        e.w16(0x2240);                 // MOVEA.L D0, A1  (resident in A1!)
        e.w16(0x7200);                 // MOVEQ #0, D1
        e.w16(0x4EAE); e.w16(0xFF9A);  // JSR -$66(A6)  (InitResident)
        e.w16(0x4E75);                 // RTS
        e.w16(0x4E71);                 // NOP
        e.str("dos.library");
    }

    // $0440: DevOpen
    {
        CodeEmitter e(rom_, 0x0440);
        e.w16(0x2340); e.w16(0x0018);  // MOVE.L D0, $18(A1)  (io_Unit = unit)
        e.w16(0x526E); e.w16(0x0020);  // ADDQ.W #1, $20(A6)  (LIB_OPENCNT)
        e.w16(0x4229); e.w16(0x001F);  // CLR.B $1F(A1)       (io_Error = 0)
        e.w16(0x7000);                 // MOVEQ #0, D0
        e.w16(0x4E75);                 // RTS
    }

    // $0460: DevClose
    {
        CodeEmitter e(rom_, 0x0460);
        e.w16(0x536E); e.w16(0x0020);  // SUBQ.W #1, $20(A6)
        e.w16(0x7000);                 // MOVEQ #0, D0
        e.w16(0x4E75);                 // RTS
    }

    // $0470: DevNull (Expunge / ExtFunc / AbortIO)
    {
        CodeEmitter e(rom_, 0x0470);
        e.w16(0x7000);                 // MOVEQ #0, D0
        e.w16(0x4E75);                 // RTS
    }

    // $0480: DevBeginIO(A1=io, A6=dev)
    {
        CodeEmitter e(rom_, 0x0480);
        e.w16(0x41FA); e.w16(static_cast<uint16_t>(0x10000 - 0x0482));  // LEA -$0482(PC), A0  -> A0 = boardBase
        e.w16(0xD1FC); e.w32(0x0000F004u);                              // ADDA.L #$F004, A0    -> A0 = boardBase + $F004
        e.w16(0x2089);                                                  // MOVE.L A1, (A0)      (trigger MMIO BeginIO!)
        e.w16(0x0829); e.w16(0x0000); e.w16(0x001E);                    // BTST #0, $1E(A1)     (IOF_QUICK?)
        e.w16(0x6608);                                                  // BNE.S +8 (skip ReplyMsg)
        e.w16(0x2C78); e.w16(0x0004);                                   // MOVEA.L $4.W, A6
        e.w16(0x4EAE); e.w16(0xFE86);                                   // JSR -$17A(A6)        (ReplyMsg(A1))
        e.w16(0x4E75);                                                  // RTS
    }

    // $0120: DiagEntry(A0=boardBase, A2=diagArea, A3=configDev, A6=SysBase)
    {
        CodeEmitter e(rom_, 0x0120);
        e.w16(0x48E7); e.w16(0x3F3E);  // MOVEM.L D2-D7/A2-A6, -(A7)
        e.w16(0x2A48);                 // MOVEA.L A0, A5  (A5 = boardBase)
        e.w16(0x284B);                 // MOVEA.L A3, A4  (A4 = configDev)

        // Check unit count at $F000(A5) -> if 0, skip mounting
        e.w16(0x204D);                 // MOVEA.L A5, A0
        e.w16(0xD1FC); e.w32(0x0000F000u);  // ADDA.L #$F000, A0
        e.w16(0x2810);                 // MOVE.L (A0), D4  (D4 = unit count)
        e.w16(0x6700);                 // BEQ.W done (patch later!)
        const size_t patch_beq_done = e.pos();
        e.w16(0x0000);

        // Fill FuncTable in RAM on stack (7 longs = 28 bytes)
        e.w16(0x4FEF); e.w16(static_cast<uint16_t>(-28));  // LEA -28(A7), A7
        e.w16(0x204F);                 // MOVEA.L A7, A0
        // Store 6 absolute pointers (boardBase + offset) + -1
        auto push_vec = [&](uint32_t fn_off) {
            e.w16(0x200D);             // MOVE.L A5, D0
            e.w16(0x0680); e.w32(fn_off);  // ADDI.L #fn_off, D0
            e.w16(0x20C0);             // MOVE.L D0, (A0)+
        };
        push_vec(0x0440);  // Open
        push_vec(0x0460);  // Close
        push_vec(0x0470);  // Expunge
        push_vec(0x0470);  // ExtFunc
        push_vec(0x0480);  // BeginIO
        push_vec(0x0470);  // AbortIO
        e.w16(0x20FC); e.w32(0xFFFFFFFFu);  // MOVE.L #-1, (A0)+

        // MakeLibrary(A0=funcTable, A1=0, A2=0, D0=36, D1=0)
        e.w16(0x204F);                 // MOVEA.L A7, A0
        e.w16(0x93C9);                 // SUBA.L A1, A1
        e.w16(0x95CA);                 // SUBA.L A2, A2
        e.w16(0x7024);                 // MOVEQ #36, D0
        e.w16(0x7200);                 // MOVEQ #0, D1
        e.w16(0x4EAE); e.w16(0xFFAC);  // JSR -$54(A6)  (MakeLibrary)
        e.w16(0x4FEF); e.w16(28);      // LEA 28(A7), A7
        e.w16(0x4A80);                 // TST.L D0
        e.w16(0x6700);                 // BEQ.W done
        const size_t patch_beq_done2 = e.pos();
        e.w16(0x0000);

        e.w16(0x2240);                 // MOVEA.L D0, A1  (A1 = Device*)
        e.w16(0x137C); e.w16(0x0003); e.w16(0x0008);  // MOVE.B #3, $08(A1) (NT_DEVICE)
        e.w16(0x200D);                 // MOVE.L A5, D0
        e.w16(0x0680); e.w32(static_cast<uint32_t>(off_dev_name));  // ADDI.L #off_dev_name, D0
        e.w16(0x2340); e.w16(0x000A);  // MOVE.L D0, $0A(A1) (LN_NAME)
        e.w16(0x2340); e.w16(0x0018);  // MOVE.L D0, $18(A1) (LIB_IDSTRING)
        e.w16(0x4EAE); e.w16(0xFE50);  // JSR -$1B0(A6)  (AddDevice(A1))

        // Open expansion.library: OldOpenLibrary(A1 = boardBase + off_exp_name)
        e.w16(0x224D);                 // MOVEA.L A5, A1
        e.w16(0xD3FC); e.w32(static_cast<uint32_t>(off_exp_name));  // ADDA.L #off_exp_name, A1
        e.w16(0x4EAE); e.w16(0xFE68);  // JSR -$198(A6)  (OldOpenLibrary)
        e.w16(0x4A80);                 // TST.L D0
        e.w16(0x6700);                 // BEQ.W done
        const size_t patch_beq_done3 = e.pos();
        e.w16(0x0000);
        e.w16(0x2640);                 // MOVEA.L D0, A3  (A3 = ExpansionBase)

        // Loop D5 = 0 .. D4 - 1
        e.w16(0x7A00);                 // MOVEQ #0, D5
        const size_t loop_top = e.pos();

        // Select unit D5 in MMIO: MOVE.L D5, $F008(A5)
        e.w16(0x204D);                 // MOVEA.L A5, A0
        e.w16(0xD1FC); e.w32(0x0000F008u);
        e.w16(0x2085);                 // MOVE.L D5, (A0)

        // Allocate 104 bytes on stack: 84 bytes parmpkt + 16 bytes dos_name buffer
        e.w16(0x4FEF); e.w16(static_cast<uint16_t>(-104));  // LEA -104(A7), A7
        // Ask MMIO $F018(A5) to write dos_name ("DH0\0") into 84(A7)
        e.w16(0x41EF); e.w16(84);      // LEA 84(A7), A0
        e.w16(0x224D);                 // MOVEA.L A5, A1
        e.w16(0xD3FC); e.w32(0x0000F018u);
        e.w16(0x2288);                 // MOVE.L A0, (A1)

        // Fill parmpkt at (A7)
        e.w16(0x224F);                 // MOVEA.L A7, A1
        e.w16(0x22C8);                 // MOVE.L A0, (A1)+         [0] dos_name ptr
        e.w16(0x200D);                 // MOVE.L A5, D0
        e.w16(0x0680); e.w32(static_cast<uint32_t>(off_dev_name));
        e.w16(0x22C0);                 // MOVE.L D0, (A1)+         [4] dev_name ptr
        e.w16(0x22C5);                 // MOVE.L D5, (A1)+         [8] unit
        e.w16(0x4299);                 // CLR.L (A1)+              [12] flags
        e.w16(0x22FC); e.w32(16);      // [16] envec size = 16
        e.w16(0x22FC); e.w32(128);     // [20] de_SizeBlock = 128
        e.w16(0x4299);                 // [24] de_SecOrg = 0
        e.w16(0x22FC); e.w32(1);       // [28] de_Surfaces = 1
        e.w16(0x22FC); e.w32(1);       // [32] de_SectorsPerBlock = 1
        e.w16(0x22FC); e.w32(32);      // [36] de_BlocksPerTrack = 32
        e.w16(0x22FC); e.w32(2);       // [40] de_Reserved = 2
        e.w16(0x4299);                 // [44] de_PreAlloc = 0
        e.w16(0x4299);                 // [48] de_Interleave = 0
        e.w16(0x4299);                 // [52] de_LowCyl = 0
        // [56] de_HighCyl = ($F00C(A5)) - 1
        e.w16(0x204D); e.w16(0xD1FC); e.w32(0x0000F00Cu);
        e.w16(0x2010);                 // MOVE.L (A0), D0
        e.w16(0x5380);                 // SUBQ.L #1, D0
        e.w16(0x22C0);                 // MOVE.L D0, (A1)+
        e.w16(0x22FC); e.w32(30);      // [60] de_NumBuffers = 30
        e.w16(0x4299);                 // [64] de_BufMemType = 0
        e.w16(0x22FC); e.w32(0x00100000u);  // [68] de_MaxTransfer = 1MB
        e.w16(0x22FC); e.w32(0x7FFFFFFEu);  // [72] de_Mask
        // [76] de_BootPri = $F014(A5)
        e.w16(0x204D); e.w16(0xD1FC); e.w32(0x0000F014u);
        e.w16(0x2E10);                 // MOVE.L (A0), D7 (keep BootPri in D7)
        e.w16(0x22C7);                 // MOVE.L D7, (A1)+
        // [80] de_DosType = $F010(A5)
        e.w16(0x204D); e.w16(0xD1FC); e.w32(0x0000F010u);
        e.w16(0x22D0);                 // MOVE.L (A0), (A1)+

        // Save SysBase (A6), switch A6 = ExpansionBase (A3)
        e.w16(0x2F0E);                 // MOVE.L A6, -(A7)
        e.w16(0x2C4B);                 // MOVEA.L A3, A6
        e.w16(0x41EF); e.w16(4);       // LEA 4(A7), A0  (parmpkt, +4 because SysBase pushed)
        e.w16(0x4EAE); e.w16(0xFF70);  // JSR -$90(A6)   (MakeDosNode(A0))
        e.w16(0x4A80);                 // TST.L D0
        e.w16(0x671A);                 // BEQ.S skip_add (26 bytes)
        e.w16(0x2040);                 // MOVEA.L D0, A0 (devNode)
        e.w16(0x4A85);                 // TST.L D5       (unit == 0?)
        e.w16(0x660C);                 // BNE.S add_dos (+12)
        // AddBootNode(D0=bootPri, D1=0, A0=devNode, A1=configDev)
        e.w16(0x2007);                 // MOVE.L D7, D0
        e.w16(0x7200);                 // MOVEQ #0, D1
        e.w16(0x224C);                 // MOVEA.L A4, A1
        e.w16(0x4EAE); e.w16(0xFFDC);  // JSR -$24(A6)   (AddBootNode)
        e.w16(0x6008);                 // BRA.S skip_add (+8)
        // add_dos: AddDosNode(D0=bootPri, D1=0, A0=devNode)
        e.w16(0x2007);                 // MOVE.L D7, D0
        e.w16(0x7200);                 // MOVEQ #0, D1
        e.w16(0x4EAE); e.w16(0xFF6A);  // JSR -$96(A6)   (AddDosNode)

        // skip_add:
        e.w16(0x2C5F);                 // MOVEA.L (A7)+, A6 (restore SysBase)
        e.w16(0x4FEF); e.w16(104);     // LEA 104(A7), A7

        e.w16(0x5285);                 // ADDQ.L #1, D5
        e.w16(0xB885);                 // CMP.L D5, D4  (D4 - D5)
        const size_t branch_off = e.pos();
        e.w16(0x6E00);                 // BGT.W loop_top
        e.w16(static_cast<uint16_t>(static_cast<int16_t>(loop_top - (branch_off + 2))));

        // CloseLibrary(A1 = A3)
        e.w16(0x224B);                 // MOVEA.L A3, A1
        e.w16(0x4EAE); e.w16(0xFE62);  // JSR -$19E(A6)

        const size_t done_pos = e.pos();
        emit16(rom_, patch_beq_done, static_cast<uint16_t>(done_pos - patch_beq_done));
        emit16(rom_, patch_beq_done2, static_cast<uint16_t>(done_pos - patch_beq_done2));
        emit16(rom_, patch_beq_done3, static_cast<uint16_t>(done_pos - patch_beq_done3));

        e.w16(0x4CDF); e.w16(0x7CFC);  // MOVEM.L (A7)+, D2-D7/A2-A6
        e.w16(0x7001);                 // MOVEQ #1, D0
        e.w16(0x4E75);                 // RTS
    }
}

uint8_t HardDriveController::read_autoconfig_byte(uint32_t offset) const {
    // Zorro II Autoconfig nibbles at even offsets $00..$3E (inverted for $02..$3E)
    auto reg_byte = [](uint8_t idx) -> uint8_t {
        switch (idx) {
            case 0x00: return 0xD1;  // ERT_ZORROII | ERTF_DIAGVALID | 64KB (size=1)
            case 0x02: return 0x01;  // Product ID = 1
            case 0x04: return 0x00;  // Flags
            case 0x08: return 0x07;  // Manufacturer high
            case 0x0A: return 0xDB;  // Manufacturer low ($07DB)
            case 0x14: return 0x01;  // er_InitDiagVec high ($0100)
            case 0x16: return 0x00;  // er_InitDiagVec low
            default:   return 0x00;
        }
    };

    const uint8_t idx = static_cast<uint8_t>((offset >> 1) & 0xFEu);
    const uint8_t val = reg_byte(idx);
    const bool low_nibble = (offset & 2u) != 0;
    const uint8_t nib = low_nibble ? (val & 0x0Fu) : (val >> 4);
    const uint8_t encoded = static_cast<uint8_t>((offset < 4u) ? nib : (~nib & 0x0Fu));
    return static_cast<uint8_t>((encoded << 4) | 0x0Fu);
}

bool HardDriveController::handles_read(uint32_t addr24) const {
    if (units_.empty()) return false;
    if (!configured_ && !shut_up_ && addr24 >= 0xE80000u && addr24 < 0xE90000u) {
        return true;
    }
    if (configured_ && addr24 >= base_addr_ && addr24 < base_addr_ + 0x10000u) {
        return true;
    }
    return false;
}

bool HardDriveController::handles_write(uint32_t addr24) const {
    return handles_read(addr24);
}

uint8_t HardDriveController::read8(uint32_t addr24) {
    if (!configured_ && !shut_up_ && addr24 >= 0xE80000u && addr24 < 0xE90000u) {
        const uint32_t off = addr24 - 0xE80000u;
        if (off < 0x40u && (off & 1u) == 0) {
            return read_autoconfig_byte(off);
        }
        return 0xFF;
    }
    if (configured_ && addr24 >= base_addr_ && addr24 < base_addr_ + 0x10000u) {
        const uint32_t off = addr24 - base_addr_;
        if (off < rom_.size()) {
            return rom_[off];
        }
        if (off >= 0xF000u && off < 0xF020u) {
            uint32_t v = 0;
            const uint32_t reg = off & ~3u;
            if (reg == 0xF000u) {
                v = static_cast<uint32_t>(units_.size());
            } else if (selected_unit_ < units_.size()) {
                const auto& u = units_[selected_unit_];
                if (reg == 0xF00Cu) v = u->cylinders;
                else if (reg == 0xF010u) v = u->dostype;
                else if (reg == 0xF014u) v = static_cast<uint32_t>(u->boot_pri);
            }
            const int shift = static_cast<int>((3u - (off & 3u)) * 8u);
            return static_cast<uint8_t>((v >> shift) & 0xFFu);
        }
    }
    return 0xFF;
}

void HardDriveController::write8(uint32_t addr24, uint8_t val, Bus& /*bus*/) {
    if (!configured_ && !shut_up_ && addr24 >= 0xE80000u && addr24 < 0xE90000u) {
        const uint32_t off = addr24 - 0xE80000u;
        if (off == 0x48u) {
            base_addr_ = static_cast<uint32_t>(val) << 16;
            configured_ = true;
        } else if (off == 0x4Cu) {
            shut_up_ = true;
        }
    }
}

void HardDriveController::write32(uint32_t addr24, uint32_t val, Bus& bus) {
    if (!configured_ || addr24 < base_addr_ || addr24 >= base_addr_ + 0x10000u) return;
    const uint32_t off = addr24 - base_addr_;
    if (off == 0xF004u) {
        handle_begin_io(val, bus);
    } else if (off == 0xF008u) {
        selected_unit_ = val;
    } else if (off == 0xF018u) {
        name_target_addr_ = val;
        if (selected_unit_ < units_.size() && name_target_addr_ != 0) {
            const std::string& nm = units_[selected_unit_]->dos_name;
            for (size_t i = 0; i < nm.size() && i < 15; ++i) {
                bus.write8(name_target_addr_ + static_cast<uint32_t>(i), static_cast<uint8_t>(nm[i]));
            }
            bus.write8(name_target_addr_ + static_cast<uint32_t>(std::min<size_t>(nm.size(), 15)), 0);
        }
    }
}

void HardDriveController::handle_begin_io(uint32_t io_addr, Bus& bus) {
    const uint32_t unit_idx = read_be32_bus(bus, io_addr + 0x18u);
    const uint16_t cmd = bus.read16(io_addr + 0x1Cu);
    const uint32_t length = read_be32_bus(bus, io_addr + 0x24u);
    const uint32_t data_ptr = read_be32_bus(bus, io_addr + 0x28u);
    const uint32_t offset32 = read_be32_bus(bus, io_addr + 0x2Cu);

    if (unit_idx >= units_.size()) {
        bus.write8(io_addr + 0x1Fu, 32);  // TDERR_BadUnitNum
        write_be32_bus(bus, io_addr + 0x20u, 0);
        return;
    }

    auto& u = *units_[unit_idx];
    uint8_t err = 0;
    uint32_t actual = 0;

    switch (cmd) {
        case 2:      // CMD_READ
        case 0xC000: // NSCMD_TD_READ64
        {
            uint64_t off64 = offset32;
            if (cmd == 0xC000) {
                const uint32_t hi = read_be32_bus(bus, io_addr + 0x20u);
                off64 |= (static_cast<uint64_t>(hi) << 32);
            }
            if (off64 + length <= u.size_bytes) {
                std::vector<uint8_t> buf(length);
                u.file.clear();
                u.file.seekg(static_cast<std::streamoff>(off64), std::ios::beg);
                u.file.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(length));
                if (u.file) {
                    for (uint32_t i = 0; i < length; ++i) {
                        bus.write8(data_ptr + i, buf[i]);
                    }
                    actual = length;
                    activity_frames_ = 8;
                } else {
                    err = 20;  // IOERR_BADLENGTH / read error
                }
            } else {
                err = 20;
            }
            break;
        }
        case 3:      // CMD_WRITE
        case 11:     // TD_FORMAT
        case 0xC001: // NSCMD_TD_WRITE64
        case 0xC002: // NSCMD_TD_FORMAT64
        {
            uint64_t off64 = offset32;
            if (cmd == 0xC001 || cmd == 0xC002) {
                const uint32_t hi = read_be32_bus(bus, io_addr + 0x20u);
                off64 |= (static_cast<uint64_t>(hi) << 32);
            }
            if (off64 + length <= u.size_bytes) {
                std::vector<uint8_t> buf(length);
                for (uint32_t i = 0; i < length; ++i) {
                    buf[i] = bus.read8(data_ptr + i);
                }
                u.file.clear();
                u.file.seekp(static_cast<std::streamoff>(off64), std::ios::beg);
                u.file.write(reinterpret_cast<const char*>(buf.data()), static_cast<std::streamsize>(length));
                u.file.flush();
                actual = length;
                activity_frames_ = 8;
            } else {
                err = 20;
            }
            break;
        }
        case 9:   // TD_MOTOR
        case 10:  // TD_SEEK
        case 13:  // TD_CHANGENUM
        case 14:  // TD_CHANGESTATE (0 = disk present)
        case 15:  // TD_PROTSTATUS (0 = writable)
        case 20:  // TD_ADDCHANGEINT
        case 21:  // TD_REMCHANGEINT
            actual = 0;
            break;
        case 18:  // TD_GETDRIVETYPE
            actual = 1;  // 3.5" / generic drive
            break;
        case 22:  // TD_GETGEOMETRY
            if (length >= 28 && data_ptr != 0) {
                write_be32_bus(bus, data_ptr + 0, 512);
                write_be32_bus(bus, data_ptr + 4, u.cylinders * 32u);
                write_be32_bus(bus, data_ptr + 8, u.cylinders);
                write_be32_bus(bus, data_ptr + 12, 1);
                write_be32_bus(bus, data_ptr + 16, 1);
                write_be32_bus(bus, data_ptr + 20, 32);
                write_be32_bus(bus, data_ptr + 24, 32);
                bus.write8(data_ptr + 28, 0);
                bus.write8(data_ptr + 29, 0);
                actual = 30;
            }
            break;
        default:
            err = 0xFD;  // IOERR_NOCMD (-3)
            break;
    }

    bus.write8(io_addr + 0x1Fu, err);
    write_be32_bus(bus, io_addr + 0x20u, actual);
}

}  // namespace amiga
