#include "amiga/m68k.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>

namespace amiga {

namespace {

constexpr uint32_t size_mask(Size s) {
    switch (s) {
        case Size::Byte: return 0x000000FFu;
        case Size::Word: return 0x0000FFFFu;
        case Size::Long: return 0xFFFFFFFFu;
    }
    return 0xFFFFFFFFu;
}

constexpr uint32_t size_sign_bit(Size s) {
    switch (s) {
        case Size::Byte: return 0x00000080u;
        case Size::Word: return 0x00008000u;
        case Size::Long: return 0x80000000u;
    }
    return 0x80000000u;
}

constexpr uint32_t sign_extend_to_32(uint32_t val, Size s) {
    switch (s) {
        case Size::Byte: return static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(val)));
        case Size::Word: return static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(val)));
        case Size::Long: return val;
    }
    return val;
}

constexpr uint32_t addr_increment(uint8_t reg, Size s) {
    if (s == Size::Byte && reg == 7) {
        return 2;  // A7 stays word-aligned on byte stack operations.
    }
    return static_cast<uint32_t>(s);
}

constexpr Size decode_size_00_01_10(uint8_t bits) {
    switch (bits & 0x3u) {
        case 0: return Size::Byte;
        case 1: return Size::Word;
        default: return Size::Long;
    }
}

uint32_t read_sized(Bus& bus, uint32_t addr, Size s) {
    switch (s) {
        case Size::Byte: return bus.read8(addr);
        case Size::Word: return bus.read16(addr);
        case Size::Long: return bus.read32(addr);
    }
    return 0;
}

void write_sized(Bus& bus, uint32_t addr, Size s, uint32_t val) {
    switch (s) {
        case Size::Byte: bus.write8(addr, static_cast<uint8_t>(val)); break;
        case Size::Word: bus.write16(addr, static_cast<uint16_t>(val)); break;
        case Size::Long: bus.write32(addr, val); break;
    }
}

}  // namespace

M68k::M68k(Bus& bus) : bus_(bus) {}

void M68k::reset() {
    d_.fill(0);
    a_.fill(0);
    vbr_ = 0;
    cacr_ = 0;
    sfc_ = 0;
    dfc_ = 0;
    fp_.fill(0.0);
    fpcr_ = 0;
    fpsr_ = 0;
    fpiar_ = 0;
    usp_ = 0;
    ssp_ = 0;
    sr_ = 0x2700;
    halted_ = false;
    pending_ipl_ = 0;
    total_cycles_ = 0;

    ssp_ = bus_.read32(0x000000u);
    a_[7] = ssp_;
    pc_ = bus_.read32(0x000004u);
}

void M68k::set_sr(uint16_t new_sr) {
    new_sr &= SR_MASK;
    const bool was_sup = is_supervisor();
    const bool now_sup = (new_sr & FLAG_S) != 0;
    if (was_sup != now_sup) {
        if (was_sup) {
            ssp_ = a_[7];
            a_[7] = usp_;
        } else {
            usp_ = a_[7];
            a_[7] = ssp_;
        }
    }
    sr_ = new_sr;
}

void M68k::set_usp(uint32_t val) {
    if (is_supervisor()) {
        usp_ = val;
    } else {
        a_[7] = val;
    }
}

void M68k::set_ssp(uint32_t val) {
    if (is_supervisor()) {
        a_[7] = val;
    } else {
        ssp_ = val;
    }
}

uint16_t M68k::fetch16() {
    const uint16_t val = bus_.read16(pc_);
    pc_ += 2;
    return val;
}

uint32_t M68k::fetch32() {
    const uint32_t val = bus_.read32(pc_);
    pc_ += 4;
    return val;
}

void M68k::push16(uint16_t val) {
    a_[7] -= 2;
    bus_.write16(a_[7], val);
}

void M68k::push32(uint32_t val) {
    a_[7] -= 4;
    bus_.write32(a_[7], val);
}

uint16_t M68k::pop16() {
    const uint16_t val = bus_.read16(a_[7]);
    a_[7] += 2;
    return val;
}

uint32_t M68k::pop32() {
    const uint32_t val = bus_.read32(a_[7]);
    a_[7] += 4;
    return val;
}

void M68k::raise_exception(uint8_t vector) {
    exception_taken_ = true;
    if (vector == 4 || vector == 8 || vector == 10 || vector == 11) {
        pc_ = instr_pc_;
    }
    const uint16_t old_sr = sr_;
    set_sr(static_cast<uint16_t>((sr_ | FLAG_S) & ~FLAG_T));
    if (is_020()) {
        push16(static_cast<uint16_t>(vector) * 4u);  // Format $0 + vector offset
    }
    push32(pc_);
    push16(old_sr);
    pc_ = bus_.read32(vbr_ + static_cast<uint32_t>(vector) * 4u);
    total_cycles_ += 34;
}

M68k::ResolvedEA M68k::resolve_indexed_ea(uint32_t base_addr, bool /*base_is_pc*/) {
    ResolvedEA ea{};
    ea.kind = ResolvedEA::Kind::Memory;
    const uint16_t ext = fetch16();
    const uint8_t xreg = (ext >> 12) & 0x7u;
    const uint32_t raw_x = (ext & 0x8000u) ? a_[xreg] : d_[xreg];
    const int32_t x_val = (ext & 0x0800u)
                              ? static_cast<int32_t>(raw_x)
                              : static_cast<int32_t>(static_cast<int16_t>(raw_x & 0xFFFFu));
    const uint32_t scale = is_020() ? ((ext >> 9) & 0x3u) : 0u;
    const int64_t x_scaled = static_cast<int64_t>(x_val) << scale;

    if (!is_020() || (ext & 0x0100u) == 0) {
        const auto disp = static_cast<int8_t>(ext & 0xFFu);
        ea.addr = static_cast<uint32_t>(static_cast<int64_t>(base_addr) + disp + x_scaled);
        ea.cycles = 10;
        return ea;
    }

    const bool bs = (ext & 0x0080u) != 0;
    const bool is = (ext & 0x0040u) != 0;
    const uint8_t bd_size = (ext >> 4) & 0x3u;
    const uint8_t iis = ext & 0x7u;

    int32_t bd = 0;
    if (bd_size == 2) bd = static_cast<int32_t>(static_cast<int16_t>(fetch16()));
    else if (bd_size == 3) bd = static_cast<int32_t>(fetch32());

    const uint32_t base = bs ? 0u : base_addr;
    const int64_t idx = is ? 0 : x_scaled;

    if (iis == 0) {
        ea.addr = static_cast<uint32_t>(static_cast<int64_t>(base) + bd + idx);
        ea.cycles = 12;
        return ea;
    }

    const uint8_t od_size = iis & 0x3u;
    int32_t od = 0;
    if (od_size == 2) od = static_cast<int32_t>(static_cast<int16_t>(fetch16()));
    else if (od_size == 3) od = static_cast<int32_t>(fetch32());

    const bool post = (iis & 0x4u) != 0;
    const uint32_t ptr_addr = post
                                  ? static_cast<uint32_t>(static_cast<int64_t>(base) + bd)
                                  : static_cast<uint32_t>(static_cast<int64_t>(base) + bd + idx);
    const uint32_t ptr = bus_.read32(ptr_addr);
    ea.addr = post
                  ? static_cast<uint32_t>(static_cast<int64_t>(ptr) + idx + od)
                  : static_cast<uint32_t>(static_cast<int64_t>(ptr) + od);
    ea.cycles = 18;
    return ea;
}

M68k::ResolvedEA M68k::resolve_ea(uint8_t mode, uint8_t reg, Size size) {
    ResolvedEA ea{};
    const uint32_t word_penalty = (size == Size::Long) ? 4u : 0u;

    switch (mode & 0x7u) {
        case 0:  // Dn
            ea.kind = ResolvedEA::Kind::DataReg;
            ea.reg = reg;
            return ea;
        case 1:  // An
            ea.kind = ResolvedEA::Kind::AddrReg;
            ea.reg = reg;
            return ea;
        case 2:  // (An)
            ea.kind = ResolvedEA::Kind::Memory;
            ea.addr = a_[reg];
            ea.cycles = 4 + word_penalty;
            return ea;
        case 3:  // (An)+
            ea.kind = ResolvedEA::Kind::Memory;
            ea.addr = a_[reg];
            a_[reg] += addr_increment(reg, size);
            ea.cycles = 4 + word_penalty;
            return ea;
        case 4:  // -(An)
            a_[reg] -= addr_increment(reg, size);
            ea.kind = ResolvedEA::Kind::Memory;
            ea.addr = a_[reg];
            ea.cycles = 6 + word_penalty;
            return ea;
        case 5: {  // (d16, An)
            const auto disp = static_cast<int16_t>(fetch16());
            ea.kind = ResolvedEA::Kind::Memory;
            ea.addr = static_cast<uint32_t>(static_cast<int32_t>(a_[reg]) + disp);
            ea.cycles = 8 + word_penalty;
            return ea;
        }
        case 6: {  // (d8, An, Xn) / 68020 full ext
            ResolvedEA res = resolve_indexed_ea(a_[reg], false);
            res.cycles += word_penalty;
            return res;
        }
        case 7:
            switch (reg & 0x7u) {
                case 0:  // (xxx).W
                    ea.kind = ResolvedEA::Kind::Memory;
                    ea.addr = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(fetch16())));
                    ea.cycles = 8 + word_penalty;
                    return ea;
                case 1:  // (xxx).L
                    ea.kind = ResolvedEA::Kind::Memory;
                    ea.addr = fetch32();
                    ea.cycles = 12 + word_penalty;
                    return ea;
                case 2: {  // (d16, PC)
                    const uint32_t base = pc_;
                    const auto disp = static_cast<int16_t>(fetch16());
                    ea.kind = ResolvedEA::Kind::Memory;
                    ea.addr = static_cast<uint32_t>(static_cast<int32_t>(base) + disp);
                    ea.cycles = 8 + word_penalty;
                    return ea;
                }
                case 3: {  // (d8, PC, Xn) / 68020 full ext
                    const uint32_t base = pc_;
                    ResolvedEA res = resolve_indexed_ea(base, true);
                    res.cycles += word_penalty;
                    return res;
                }
                case 4:  // #imm
                    ea.kind = ResolvedEA::Kind::Immediate;
                    if (size == Size::Long) {
                        ea.imm = fetch32();
                        ea.cycles = 8;
                    } else {
                        ea.imm = fetch16() & size_mask(size);
                        ea.cycles = 4;
                    }
                    return ea;
                default:
                    break;
            }
            break;
    }
    return ea;
}

uint32_t M68k::read_ea(const ResolvedEA& ea, Size size) {
    const uint32_t mask = size_mask(size);
    switch (ea.kind) {
        case ResolvedEA::Kind::DataReg: return d_[ea.reg] & mask;
        case ResolvedEA::Kind::AddrReg: return a_[ea.reg] & mask;
        case ResolvedEA::Kind::Memory: return read_sized(bus_, ea.addr, size) & mask;
        case ResolvedEA::Kind::Immediate: return ea.imm & mask;
    }
    return 0;
}

void M68k::write_ea(const ResolvedEA& ea, Size size, uint32_t val) {
    const uint32_t mask = size_mask(size);
    switch (ea.kind) {
        case ResolvedEA::Kind::DataReg:
            d_[ea.reg] = (d_[ea.reg] & ~mask) | (val & mask);
            break;
        case ResolvedEA::Kind::AddrReg:
            a_[ea.reg] = (size == Size::Word) ? sign_extend_to_32(val, Size::Word) : val;
            break;
        case ResolvedEA::Kind::Memory:
            write_sized(bus_, ea.addr, size, val & mask);
            break;
        case ResolvedEA::Kind::Immediate:
            break;
    }
}

uint32_t M68k::read_modify_write_ea(uint8_t mode, uint8_t reg, Size size, ResolvedEA& out_ea) {
    out_ea = resolve_ea(mode, reg, size);
    return read_ea(out_ea, size);
}

void M68k::set_ccr(uint8_t ccr) {
    sr_ = static_cast<uint16_t>((sr_ & 0xFFE0u) | (ccr & 0x1Fu));
}

void M68k::update_nz_flags(uint32_t result, Size size, bool clear_vc) {
    const uint32_t masked = result & size_mask(size);
    uint16_t clear_mask = FLAG_Z | FLAG_N;
    if (clear_vc) {
        clear_mask |= FLAG_V | FLAG_C;
    }
    sr_ &= static_cast<uint16_t>(~clear_mask);
    if (masked == 0) {
        sr_ |= FLAG_Z;
    }
    if ((masked & size_sign_bit(size)) != 0) {
        sr_ |= FLAG_N;
    }
}

uint32_t M68k::add_with_flags(uint32_t dst, uint32_t src, Size size, bool with_x, bool set_x) {
    const uint32_t mask = size_mask(size);
    const uint32_t sbit = size_sign_bit(size);
    const uint32_t d = dst & mask;
    const uint32_t s = src & mask;
    const uint32_t x = (with_x && (sr_ & FLAG_X)) ? 1u : 0u;

    const uint64_t sum = static_cast<uint64_t>(d) + static_cast<uint64_t>(s) + x;
    const uint32_t res = static_cast<uint32_t>(sum) & mask;

    const bool carry = sum > mask;
    const bool overflow = ((~(d ^ s)) & (d ^ res) & sbit) != 0;

    uint16_t clear_mask = FLAG_C | FLAG_V | FLAG_Z | FLAG_N | (set_x ? FLAG_X : 0u);
    sr_ &= static_cast<uint16_t>(~clear_mask);
    if (carry) {
        sr_ |= FLAG_C;
        if (set_x) sr_ |= FLAG_X;
    }
    if (overflow) sr_ |= FLAG_V;
    if (res == 0) {
        // Note: for ADDX, Z is only cleared when non-zero; for regular ADD it's set when zero.
        if (!with_x) sr_ |= FLAG_Z;
    }
    if ((res & sbit) != 0) sr_ |= FLAG_N;

    return res;
}

uint32_t M68k::sub_with_flags(uint32_t dst, uint32_t src, Size size, bool with_x, bool set_x) {
    const uint32_t mask = size_mask(size);
    const uint32_t sbit = size_sign_bit(size);
    const uint32_t d = dst & mask;
    const uint32_t s = src & mask;
    const uint32_t x = (with_x && (sr_ & FLAG_X)) ? 1u : 0u;

    const uint64_t diff = static_cast<uint64_t>(d) - static_cast<uint64_t>(s) - x;
    const uint32_t res = static_cast<uint32_t>(diff) & mask;

    const bool borrow = static_cast<uint64_t>(d) < (static_cast<uint64_t>(s) + x);
    const bool overflow = ((d ^ s) & (d ^ res) & sbit) != 0;

    uint16_t clear_mask = FLAG_C | FLAG_V | FLAG_Z | FLAG_N | (set_x ? FLAG_X : 0u);
    sr_ &= static_cast<uint16_t>(~clear_mask);
    if (borrow) {
        sr_ |= FLAG_C;
        if (set_x) sr_ |= FLAG_X;
    }
    if (overflow) sr_ |= FLAG_V;
    if (res == 0) {
        if (!with_x) sr_ |= FLAG_Z;
    }
    if ((res & sbit) != 0) sr_ |= FLAG_N;

    return res;
}

bool M68k::test_condition(uint8_t cc) const {
    const bool c = (sr_ & FLAG_C) != 0;
    const bool v = (sr_ & FLAG_V) != 0;
    const bool z = (sr_ & FLAG_Z) != 0;
    const bool n = (sr_ & FLAG_N) != 0;

    switch (cc & 0xFu) {
        case 0x0: return true;                   // T
        case 0x1: return false;                  // F
        case 0x2: return !c && !z;               // HI
        case 0x3: return c || z;                 // LS
        case 0x4: return !c;                     // CC / HS
        case 0x5: return c;                      // CS / LO
        case 0x6: return !z;                     // NE
        case 0x7: return z;                      // EQ
        case 0x8: return !v;                     // VC
        case 0x9: return v;                      // VS
        case 0xA: return !n;                     // PL
        case 0xB: return n;                      // MI
        case 0xC: return n == v;                 // GE
        case 0xD: return n != v;                 // LT
        case 0xE: return !z && (n == v);         // GT
        case 0xF: return z || (n != v);          // LE
    }
    return false;
}

uint32_t M68k::step() {
    const uint8_t cur_ipl = static_cast<uint8_t>((sr_ >> 8) & 0x7u);
    if (pending_ipl_ != 0 && (pending_ipl_ == 7 || pending_ipl_ > cur_ipl)) {
        halted_ = false;
        const uint8_t level = pending_ipl_;
        if (level == 7) {
            pending_ipl_ = 0;  // NMI is edge-triggered
        }
        const uint16_t old_sr = sr_;
        const uint16_t new_sr = static_cast<uint16_t>(((sr_ | FLAG_S) & ~FLAG_T & ~FLAG_IPL_MASK) |
                                                      (static_cast<uint16_t>(level) << 8));
        set_sr(new_sr);
        if (is_020()) {
            push16(static_cast<uint16_t>((24u + level) * 4u));
        }
        push32(pc_);
        push16(old_sr);
        pc_ = bus_.read32(vbr_ + static_cast<uint32_t>(24u + level) * 4u);
        total_cycles_ += 44;
        return 44;
    }

    if (halted_) {
        total_cycles_ += 4;
        return 4;
    }

    instr_pc_ = pc_;
    const bool trace = (sr_ & FLAG_T) != 0;
    exception_taken_ = false;
    const uint16_t op = fetch16();
    const uint32_t cycles = execute_opcode(op);
    total_cycles_ += cycles;
    if (trace && !exception_taken_ && !halted_) {
        raise_exception(9);
        return cycles + 34;
    }
    return cycles;
}

uint32_t M68k::execute_opcode(uint16_t op) {
    const uint8_t group = (op >> 12) & 0xFu;
    switch (group) {
        case 0x0: return exec_group0(op);
        case 0x1: return exec_move(op, Size::Byte);
        case 0x2: return exec_move(op, Size::Long);
        case 0x3: return exec_move(op, Size::Word);
        case 0x4: return exec_group4(op);
        case 0x5: return exec_group5(op);
        case 0x6: return exec_group6(op);
        case 0x7: {  // MOVEQ
            if ((op & 0x0100u) == 0) {
                const uint8_t reg = (op >> 9) & 0x7u;
                const uint32_t val = sign_extend_to_32(op & 0xFFu, Size::Byte);
                d_[reg] = val;
                update_nz_flags(val, Size::Long);
                return 4;
            }
            break;
        }
        case 0x8: return exec_group8(op);
        case 0x9: return exec_group9_or_d(op, false);
        case 0xA:
            raise_exception(10);
            return 34;
        case 0xB: return exec_group_b(op);
        case 0xC: return exec_group_c(op);
        case 0xD: return exec_group9_or_d(op, true);
        case 0xE: return exec_group_e(op);
        case 0xF:
            if (is_020() && (op & 0x0E00u) == 0x0200u) {
                return exec_fpu(op);
            }
            raise_exception(11);
            return 34;
        default: break;
    }

    raise_exception(4);  // Illegal instruction
    return 34;
}

uint32_t M68k::exec_bit_op(uint8_t bit_op, uint8_t bit_num, uint8_t ea_mode, uint8_t ea_reg, uint32_t extra_cycles) {
    const bool is_reg = (ea_mode == 0);
    const Size size = is_reg ? Size::Long : Size::Byte;
    const uint32_t bit = 1u << (bit_num & (is_reg ? 31u : 7u));

    ResolvedEA ea{};
    const uint32_t val = read_modify_write_ea(ea_mode, ea_reg, size, ea);
    if ((val & bit) == 0) {
        sr_ |= FLAG_Z;
    } else {
        sr_ &= static_cast<uint16_t>(~FLAG_Z);
    }

    uint32_t res = val;
    switch (bit_op & 0x3u) {
        case 0: return 4 + extra_cycles + ea.cycles;  // BTST
        case 1: res = val ^ bit; break;               // BCHG
        case 2: res = val & ~bit; break;              // BCLR
        case 3: res = val | bit; break;               // BSET
    }
    write_ea(ea, size, res);
    return 8 + extra_cycles + ea.cycles;
}

uint32_t M68k::exec_group0(uint16_t op) {
    const uint8_t ea_mode = (op >> 3) & 0x7u;
    const uint8_t ea_reg = op & 0x7u;
    const uint8_t size_bits = (op >> 6) & 0x3u;

    // MOVEP Dn, (d16, An) / (d16, An), Dn
    if ((op & 0x0138u) == 0x0108u) {
        const uint8_t dn = (op >> 9) & 0x7u;
        const auto disp = static_cast<int16_t>(fetch16());
        uint32_t addr = static_cast<uint32_t>(static_cast<int32_t>(a_[ea_reg]) + disp);
        const bool is_long = (op & 0x0040u) != 0;
        const bool to_mem = (op & 0x0080u) != 0;
        const int count = is_long ? 4 : 2;
        if (to_mem) {
            for (int i = count - 1; i >= 0; --i) {
                bus_.write8(addr, static_cast<uint8_t>((d_[dn] >> (i * 8)) & 0xFFu));
                addr += 2;
            }
        } else {
            uint32_t res = 0;
            for (int i = 0; i < count; ++i) {
                res = (res << 8) | bus_.read8(addr);
                addr += 2;
            }
            const uint32_t mask = is_long ? 0xFFFFFFFFu : 0xFFFFu;
            d_[dn] = (d_[dn] & ~mask) | (res & mask);
        }
        return is_long ? 24 : 16;
    }

    // Dynamic bit operations: BTST/BCHG/BCLR/BSET Dn, <ea>
    if ((op & 0x0100u) != 0) {
        const uint8_t dn = (op >> 9) & 0x7u;
        return exec_bit_op(size_bits, static_cast<uint8_t>(d_[dn]), ea_mode, ea_reg, 0);
    }

    // Static bit operations: BTST/BCHG/BCLR/BSET #imm, <ea>
    if ((op & 0xFF00u) == 0x0800u) {
        const uint8_t bit_num = static_cast<uint8_t>(fetch16() & 0xFFu);
        return exec_bit_op(size_bits, bit_num, ea_mode, ea_reg, 4);
    }

    // Immediate ALU operations: ORI (0000), ANDI (0200), SUBI (0400), ADDI (0600), EORI (0A00), CMPI (0C00)
    if (size_bits < 3) {
        const Size size = decode_size_00_01_10(size_bits);
        const uint8_t subop = (op >> 9) & 0x7u;

        // Special forms to CCR / SR (ea_mode == 7, ea_reg == 4)
        if (ea_mode == 7 && ea_reg == 4 && (subop == 0 || subop == 1 || subop == 5)) {
            const uint16_t imm = fetch16();
            if (size == Size::Word && !is_supervisor()) {
                raise_exception(8);  // Privilege violation
                return 34;
            }
            const uint16_t cur = sr_;
            uint16_t next = cur;
            if (subop == 0) next = cur | imm;
            else if (subop == 1) next = cur & imm;
            else next = cur ^ imm;

            if (size == Size::Byte) {
                set_ccr(static_cast<uint8_t>(next));
            } else {
                set_sr(next);
            }
            return 20;
        }

        const uint32_t imm = (size == Size::Long) ? fetch32() : (fetch16() & size_mask(size));
        ResolvedEA dst_ea{};
        const uint32_t dst = read_modify_write_ea(ea_mode, ea_reg, size, dst_ea);
        uint32_t res = dst;

        switch (subop) {
            case 0:  // ORI
                res = dst | imm;
                update_nz_flags(res, size);
                write_ea(dst_ea, size, res);
                return 8 + dst_ea.cycles;
            case 1:  // ANDI
                res = dst & imm;
                update_nz_flags(res, size);
                write_ea(dst_ea, size, res);
                return 8 + dst_ea.cycles;
            case 2:  // SUBI
                res = sub_with_flags(dst, imm, size, false, true);
                write_ea(dst_ea, size, res);
                return 8 + dst_ea.cycles;
            case 3:  // ADDI
                res = add_with_flags(dst, imm, size, false, true);
                write_ea(dst_ea, size, res);
                return 8 + dst_ea.cycles;
            case 5:  // EORI
                res = dst ^ imm;
                update_nz_flags(res, size);
                write_ea(dst_ea, size, res);
                return 8 + dst_ea.cycles;
            case 6:  // CMPI
                (void)sub_with_flags(dst, imm, size, false, false);
                return 8 + dst_ea.cycles;
            default:
                break;
        }
    }

    raise_exception(4);
    return 34;
}

uint32_t M68k::exec_move(uint16_t op, Size size) {
    const uint8_t src_mode = (op >> 3) & 0x7u;
    const uint8_t src_reg = op & 0x7u;
    const uint8_t dst_reg = (op >> 9) & 0x7u;
    const uint8_t dst_mode = (op >> 6) & 0x7u;

    const ResolvedEA src_ea = resolve_ea(src_mode, src_reg, size);
    const uint32_t val = read_ea(src_ea, size);

    const ResolvedEA dst_ea = resolve_ea(dst_mode, dst_reg, size);
    write_ea(dst_ea, size, val);

    // MOVEA does not alter CCR flags.
    if (dst_mode != 1) {
        update_nz_flags(val, size);
    }

    return 4 + src_ea.cycles + dst_ea.cycles;
}

uint32_t M68k::exec_group4(uint16_t op) {
    if (op == 0x4E71u) {  // NOP
        return 4;
    }
    if (op == 0x4AFCu) {  // ILLEGAL
        raise_exception(4);
        return 34;
    }
    if (op == 0x4E72u) {  // STOP #imm
        if (!is_supervisor()) {
            raise_exception(8);
            return 34;
        }
        set_sr(fetch16());
        halted_ = true;
        return 4;
    }
    if (op == 0x4E75u) {  // RTS
        pc_ = pop32();
        return 16;
    }
    if (op == 0x4E77u) {  // RTR
        set_ccr(static_cast<uint8_t>(pop16()));
        pc_ = pop32();
        return 20;
    }
    if (op == 0x4E73u) {  // RTE
        if (!is_supervisor()) {
            raise_exception(8);
            return 34;
        }
        const uint16_t new_sr = pop16();
        const uint32_t new_pc = pop32();
        if (is_020()) {
            const uint16_t fmt = pop16();
            if ((fmt >> 12) == 2u) {
                (void)pop32();
            }
        }
        set_sr(new_sr);
        pc_ = new_pc;
        return 20;
    }
    if (op == 0x4E74u && is_020()) {  // RTD #disp16
        const auto disp = static_cast<int16_t>(fetch16());
        pc_ = pop32();
        a_[7] = static_cast<uint32_t>(static_cast<int32_t>(a_[7]) + disp);
        return 16;
    }
    if ((op == 0x4E7Au || op == 0x4E7Bu) && is_020()) {  // MOVEC
        if (!is_supervisor()) {
            raise_exception(8);
            return 34;
        }
        const uint16_t ext = fetch16();
        const uint8_t rreg = (ext >> 12) & 0x7u;
        const bool is_addr = (ext & 0x8000u) != 0;
        const uint16_t ctrl = ext & 0x0FFFu;
        const bool to_ctrl = (op == 0x4E7Bu);
        uint32_t& gp = is_addr ? a_[rreg] : d_[rreg];

        auto access_ctrl = [&](uint32_t val, bool write) -> std::pair<uint32_t, bool> {
            switch (ctrl) {
                case 0x000: if (write) sfc_ = val & 0x7u; return {sfc_, true};
                case 0x001: if (write) dfc_ = val & 0x7u; return {dfc_, true};
                case 0x002: if (write) cacr_ = val & 0x0003u; return {cacr_, true};
                case 0x800: if (write) usp_ = val; return {usp_, true};
                case 0x801: if (write) vbr_ = val; return {vbr_, true};
                case 0x802: return {0, true};  // CAAR (stub)
                case 0x803: case 0x804: return {a_[7], true};  // MSP/ISP
                default: return {0, false};
            }
        };
        if (to_ctrl) {
            if (!access_ctrl(gp, true).second) {
                raise_exception(4);
                return 34;
            }
        } else {
            const auto [v, ok] = access_ctrl(0, false);
            if (!ok) {
                raise_exception(4);
                return 34;
            }
            gp = v;
        }
        return 12;
    }
    if (op == 0x4E70u) {  // RESET
        if (!is_supervisor()) {
            raise_exception(8);
            return 34;
        }
        bus_.reset_peripherals();
        return 132;
    }
    if ((op & 0xFFF0u) == 0x4E40u) {  // TRAP #vector
        const uint8_t vec = 32u + (op & 0xFu);
        raise_exception(vec);
        return 34;
    }
    if ((op & 0xFFF8u) == 0x4E50u) {  // LINK An, #disp16
        const uint8_t reg = op & 0x7u;
        const auto disp = static_cast<int16_t>(fetch16());
        push32(a_[reg]);
        a_[reg] = a_[7];
        a_[7] = static_cast<uint32_t>(static_cast<int32_t>(a_[7]) + disp);
        return 16;
    }
    if ((op & 0xFFF8u) == 0x4E58u) {  // UNLK An
        const uint8_t reg = op & 0x7u;
        a_[7] = a_[reg];
        a_[reg] = pop32();
        return 12;
    }
    if ((op & 0xFFF0u) == 0x4E60u) {  // MOVE USP
        if (!is_supervisor()) {
            raise_exception(8);
            return 34;
        }
        const uint8_t reg = op & 0x7u;
        if ((op & 0x0008u) == 0) {
            usp_ = a_[reg];
        } else {
            a_[reg] = usp_;
        }
        return 4;
    }

    const uint8_t ea_mode = (op >> 3) & 0x7u;
    const uint8_t ea_reg = op & 0x7u;

    // EXTB.L Dn (68020) — must precede LEA since 0x49C0..0x49C7 matches (op & 0x01C0) == 0x01C0 with ea_mode == 0
    if ((op & 0xFFF8u) == 0x49C0u && is_020()) {
        const uint8_t reg = op & 0x7u;
        d_[reg] = sign_extend_to_32(d_[reg] & 0xFFu, Size::Byte);
        update_nz_flags(d_[reg], Size::Long);
        return 4;
    }

    // LEA <ea>, An (control addressing modes only)
    if ((op & 0x01C0u) == 0x01C0u &&
        (ea_mode == 2 || ea_mode == 5 || ea_mode == 6 || (ea_mode == 7 && ea_reg <= 3))) {
        const uint8_t reg = (op >> 9) & 0x7u;
        const ResolvedEA ea = resolve_ea(ea_mode, ea_reg, Size::Long);
        a_[reg] = ea.addr;
        return ea.cycles;
    }

    // CHK.W <ea>, Dn
    if ((op & 0x01C0u) == 0x0180u) {
        const uint8_t reg = (op >> 9) & 0x7u;
        const ResolvedEA ea = resolve_ea(ea_mode, ea_reg, Size::Word);
        const int16_t bound = static_cast<int16_t>(read_ea(ea, Size::Word));
        const int16_t val = static_cast<int16_t>(d_[reg] & 0xFFFFu);
        if (val < 0) {
            sr_ |= FLAG_N;
            raise_exception(6);
            return 40 + ea.cycles;
        }
        if (val > bound) {
            sr_ &= static_cast<uint16_t>(~FLAG_N);
            raise_exception(6);
            return 40 + ea.cycles;
        }
        return 10 + ea.cycles;
    }

    // MOVE from SR (0x40C0)
    if ((op & 0xFFC0u) == 0x40C0u) {
        const ResolvedEA ea = resolve_ea(ea_mode, ea_reg, Size::Word);
        write_ea(ea, Size::Word, sr_);
        return 6 + ea.cycles;
    }

    // MOVE from CCR (0x42C0, 68010/020)
    if ((op & 0xFFC0u) == 0x42C0u && is_020()) {
        const ResolvedEA ea = resolve_ea(ea_mode, ea_reg, Size::Word);
        write_ea(ea, Size::Word, sr_ & 0x001Fu);
        return 6 + ea.cycles;
    }

    // MOVE to CCR (0x44C0)
    if ((op & 0xFFC0u) == 0x44C0u) {
        const ResolvedEA ea = resolve_ea(ea_mode, ea_reg, Size::Word);
        set_ccr(static_cast<uint8_t>(read_ea(ea, Size::Word)));
        return 12 + ea.cycles;
    }

    // MOVE to SR (0x46C0)
    if ((op & 0xFFC0u) == 0x46C0u) {
        if (!is_supervisor()) {
            raise_exception(8);
            return 34;
        }
        const ResolvedEA ea = resolve_ea(ea_mode, ea_reg, Size::Word);
        set_sr(static_cast<uint16_t>(read_ea(ea, Size::Word)));
        return 12 + ea.cycles;
    }

    // PEA <ea>
    if ((op & 0xFFC0u) == 0x4840u && ea_mode != 0) {
        const ResolvedEA ea = resolve_ea(ea_mode, ea_reg, Size::Long);
        push32(ea.addr);
        return 8 + ea.cycles;
    }

    // SWAP Dn
    if ((op & 0xFFF8u) == 0x4840u) {
        const uint8_t reg = op & 0x7u;
        d_[reg] = (d_[reg] >> 16) | (d_[reg] << 16);
        update_nz_flags(d_[reg], Size::Long);
        return 4;
    }

    // LINK.L An, #disp32 (68020)
    if ((op & 0xFFF8u) == 0x4808u && is_020()) {
        const uint8_t reg = op & 0x7u;
        const auto disp = static_cast<int32_t>(fetch32());
        push32(a_[reg]);
        a_[reg] = a_[7];
        a_[7] = static_cast<uint32_t>(static_cast<int32_t>(a_[7]) + disp);
        return 20;
    }

    // MULS.L / MULU.L (0x4C00..0x4C3F, 68020)
    if ((op & 0xFFC0u) == 0x4C00u && is_020()) {
        const uint16_t ext = fetch16();
        const ResolvedEA ea = resolve_ea(ea_mode, ea_reg, Size::Long);
        const uint32_t src = read_ea(ea, Size::Long);
        const uint8_t dl = (ext >> 12) & 0x7u;
        const uint8_t dh = ext & 0x7u;
        const bool is_signed = (ext & 0x0800u) != 0;
        const bool is_64 = (ext & 0x0400u) != 0;

        sr_ &= static_cast<uint16_t>(~(FLAG_C | FLAG_V | FLAG_Z | FLAG_N));
        if (is_signed) {
            const int64_t res = static_cast<int64_t>(static_cast<int32_t>(src)) *
                                static_cast<int64_t>(static_cast<int32_t>(d_[dl]));
            d_[dl] = static_cast<uint32_t>(res);
            if (is_64) {
                d_[dh] = static_cast<uint32_t>(static_cast<uint64_t>(res) >> 32);
                if (res == 0) sr_ |= FLAG_Z;
                if (res < 0) sr_ |= FLAG_N;
            } else {
                update_nz_flags(d_[dl], Size::Long, false);
                if (res != static_cast<int64_t>(static_cast<int32_t>(d_[dl]))) sr_ |= FLAG_V;
            }
        } else {
            const uint64_t res = static_cast<uint64_t>(src) * static_cast<uint64_t>(d_[dl]);
            d_[dl] = static_cast<uint32_t>(res);
            if (is_64) {
                d_[dh] = static_cast<uint32_t>(res >> 32);
                if (res == 0) sr_ |= FLAG_Z;
                if ((res >> 63) != 0) sr_ |= FLAG_N;
            } else {
                update_nz_flags(d_[dl], Size::Long, false);
                if ((res >> 32) != 0) sr_ |= FLAG_V;
            }
        }
        return 28 + ea.cycles;
    }

    // DIVS.L / DIVU.L / DIVSL.L / DIVUL.L (0x4C40..0x4C7F, 68020)
    if ((op & 0xFFC0u) == 0x4C40u && is_020()) {
        const uint16_t ext = fetch16();
        const ResolvedEA ea = resolve_ea(ea_mode, ea_reg, Size::Long);
        const uint32_t src = read_ea(ea, Size::Long);
        if (src == 0) {
            raise_exception(5);
            return 40 + ea.cycles;
        }
        const uint8_t dq = (ext >> 12) & 0x7u;
        const uint8_t dr = ext & 0x7u;
        const bool is_signed = (ext & 0x0800u) != 0;
        const bool is_64 = (ext & 0x0400u) != 0;

        sr_ &= static_cast<uint16_t>(~(FLAG_C | FLAG_V));
        bool overflow = false;
        uint32_t quot = 0;
        uint32_t rem = 0;
        if (is_signed) {
            const int64_t dividend = is_64
                ? static_cast<int64_t>((static_cast<uint64_t>(d_[dr]) << 32) | d_[dq])
                : static_cast<int64_t>(static_cast<int32_t>(d_[dq]));
            const int64_t divisor = static_cast<int32_t>(src);
            const int64_t q = dividend / divisor;
            const int64_t r = dividend % divisor;
            if (q < INT32_MIN || q > INT32_MAX) {
                overflow = true;
            } else {
                quot = static_cast<uint32_t>(static_cast<int32_t>(q));
                rem = static_cast<uint32_t>(static_cast<int32_t>(r));
            }
        } else {
            const uint64_t dividend = is_64
                ? ((static_cast<uint64_t>(d_[dr]) << 32) | d_[dq])
                : static_cast<uint64_t>(d_[dq]);
            const uint64_t divisor = src;
            const uint64_t q = dividend / divisor;
            const uint64_t r = dividend % divisor;
            if (q > 0xFFFFFFFFull) {
                overflow = true;
            } else {
                quot = static_cast<uint32_t>(q);
                rem = static_cast<uint32_t>(r);
            }
        }
        if (overflow) {
            sr_ |= FLAG_V;
        } else {
            if (dr != dq || is_64) {
                d_[dr] = rem;
            }
            d_[dq] = quot;
            update_nz_flags(quot, Size::Long, false);
        }
        return 44 + ea.cycles;
    }

    // NBCD <ea>
    if ((op & 0xFFC0u) == 0x4800u) {
        ResolvedEA ea{};
        const uint8_t dst = static_cast<uint8_t>(read_modify_write_ea(ea_mode, ea_reg, Size::Byte, ea));
        const uint32_t res = exec_bcd_sub(0, dst);
        write_ea(ea, Size::Byte, res);
        return 6 + ea.cycles;
    }

    // EXT.W / EXT.L Dn
    if ((op & 0xFFB8u) == 0x4880u && ea_mode == 0) {
        const uint8_t reg = op & 0x7u;
        if ((op & 0x0040u) == 0) {
            const uint32_t res = sign_extend_to_32(d_[reg] & 0xFFu, Size::Byte) & 0xFFFFu;
            d_[reg] = (d_[reg] & 0xFFFF0000u) | res;
            update_nz_flags(res, Size::Word);
        } else {
            d_[reg] = sign_extend_to_32(d_[reg] & 0xFFFFu, Size::Word);
            update_nz_flags(d_[reg], Size::Long);
        }
        return 4;
    }

    // MOVEM
    if ((op & 0xFB80u) == 0x4880u) {
        const bool mem_to_reg = (op & 0x0400u) != 0;
        const Size size = ((op & 0x0040u) != 0) ? Size::Long : Size::Word;
        const uint32_t step_bytes = static_cast<uint32_t>(size);
        const uint16_t mask = fetch16();
        uint32_t cycles = 8;

        auto read_reg = [&](int idx) -> uint32_t {
            return (idx < 8) ? d_[idx] : a_[idx - 8];
        };
        auto write_reg = [&](int idx, uint32_t val) {
            if (idx < 8) d_[idx] = val;
            else a_[idx - 8] = val;
        };

        if (!mem_to_reg) {
            if (ea_mode == 4) {  // -(An): mask bit 0 = A7, bit 15 = D0
                uint32_t addr = a_[ea_reg];
                for (int i = 0; i < 16; ++i) {
                    if ((mask & (1u << i)) != 0) {
                        addr -= step_bytes;
                        write_sized(bus_, addr, size, read_reg(15 - i));
                        cycles += (size == Size::Long) ? 8 : 4;
                    }
                }
                a_[ea_reg] = addr;
            } else {
                const ResolvedEA ea = resolve_ea(ea_mode, ea_reg, size);
                uint32_t addr = ea.addr;
                for (int i = 0; i < 16; ++i) {
                    if ((mask & (1u << i)) != 0) {
                        write_sized(bus_, addr, size, read_reg(i));
                        addr += step_bytes;
                        cycles += (size == Size::Long) ? 8 : 4;
                    }
                }
                cycles += ea.cycles;
            }
        } else {
            uint32_t addr = 0;
            if (ea_mode == 3) {  // (An)+
                addr = a_[ea_reg];
            } else {
                const ResolvedEA ea = resolve_ea(ea_mode, ea_reg, size);
                addr = ea.addr;
                cycles += ea.cycles;
            }
            for (int i = 0; i < 16; ++i) {
                if ((mask & (1u << i)) != 0) {
                    uint32_t val = read_sized(bus_, addr, size);
                    if (size == Size::Word) {
                        val = sign_extend_to_32(val, Size::Word);
                    }
                    write_reg(i, val);
                    addr += step_bytes;
                    cycles += (size == Size::Long) ? 8 : 4;
                }
            }
            if (ea_mode == 3) {
                a_[ea_reg] = addr;
            }
        }
        return cycles;
    }

    // JSR / JMP
    if ((op & 0xFF80u) == 0x4E80u) {
        const ResolvedEA ea = resolve_ea(ea_mode, ea_reg, Size::Long);
        if ((op & 0x0040u) == 0) {
            push32(pc_);
        }
        pc_ = ea.addr;
        return 8 + ea.cycles;
    }

    // TAS <ea>
    if ((op & 0xFFC0u) == 0x4AC0u) {
        ResolvedEA ea{};
        const uint8_t val = static_cast<uint8_t>(read_modify_write_ea(ea_mode, ea_reg, Size::Byte, ea));
        update_nz_flags(val, Size::Byte);
        write_ea(ea, Size::Byte, val | 0x80u);
        return 4 + ea.cycles;
    }

    // NEGX (0x4000), CLR (0x4200), NEG (0x4400), NOT (0x4600), TST (0x4A00)
    const uint8_t size_bits = (op >> 6) & 0x3u;
    if (size_bits < 3) {
        const Size size = decode_size_00_01_10(size_bits);
        const uint16_t prefix = op & 0xFF00u;
        if (prefix == 0x4000u) {  // NEGX
            ResolvedEA ea{};
            const uint32_t dst = read_modify_write_ea(ea_mode, ea_reg, size, ea);
            const uint32_t res = sub_with_flags(0, dst, size, true, true);
            write_ea(ea, size, res);
            return 4 + ea.cycles;
        }
        if (prefix == 0x4200u) {  // CLR
            ResolvedEA ea{};
            (void)read_modify_write_ea(ea_mode, ea_reg, size, ea);
            write_ea(ea, size, 0);
            update_nz_flags(0, size);
            return 4 + ea.cycles;
        }
        if (prefix == 0x4400u) {  // NEG
            ResolvedEA ea{};
            const uint32_t dst = read_modify_write_ea(ea_mode, ea_reg, size, ea);
            const uint32_t res = sub_with_flags(0, dst, size, false, true);
            write_ea(ea, size, res);
            return 4 + ea.cycles;
        }
        if (prefix == 0x4600u) {  // NOT
            ResolvedEA ea{};
            const uint32_t dst = read_modify_write_ea(ea_mode, ea_reg, size, ea);
            const uint32_t res = (~dst) & size_mask(size);
            write_ea(ea, size, res);
            update_nz_flags(res, size);
            return 4 + ea.cycles;
        }
        if (prefix == 0x4A00u) {  // TST
            const ResolvedEA ea = resolve_ea(ea_mode, ea_reg, size);
            const uint32_t val = read_ea(ea, size);
            update_nz_flags(val, size);
            return 4 + ea.cycles;
        }
    }

    raise_exception(4);
    return 34;
}

uint32_t M68k::exec_group5(uint16_t op) {
    const uint8_t ea_mode = (op >> 3) & 0x7u;
    const uint8_t ea_reg = op & 0x7u;
    const uint8_t size_bits = (op >> 6) & 0x3u;

    // DBcc Dn, <label>
    if (size_bits == 3 && ea_mode == 1) {
        const uint8_t cc = (op >> 8) & 0xFu;
        const uint32_t base = pc_;
        const auto disp = static_cast<int16_t>(fetch16());
        if (!test_condition(cc)) {
            const uint16_t count = static_cast<uint16_t>(d_[ea_reg] & 0xFFFFu) - 1u;
            d_[ea_reg] = (d_[ea_reg] & 0xFFFF0000u) | count;
            if (count != 0xFFFFu) {
                pc_ = static_cast<uint32_t>(static_cast<int32_t>(base) + disp);
                return 10;
            }
            return 14;
        }
        return 12;
    }

    // TRAPcc (68020)
    if (size_bits == 3 && ea_mode == 7 && (ea_reg == 2 || ea_reg == 3 || ea_reg == 4) && is_020()) {
        const uint8_t cc = (op >> 8) & 0xFu;
        if (ea_reg == 2) (void)fetch16();
        else if (ea_reg == 3) (void)fetch32();
        if (test_condition(cc)) {
            raise_exception(7);
            return 34;
        }
        return 6;
    }

    // Scc <ea>
    if (size_bits == 3) {
        const uint8_t cc = (op >> 8) & 0xFu;
        const ResolvedEA ea = resolve_ea(ea_mode, ea_reg, Size::Byte);
        write_ea(ea, Size::Byte, test_condition(cc) ? 0xFFu : 0x00u);
        return 4 + ea.cycles;
    }

    // ADDQ / SUBQ
    const Size size = decode_size_00_01_10(size_bits);
    uint32_t imm = (op >> 9) & 0x7u;
    if (imm == 0) imm = 8;
    const bool is_sub = (op & 0x0100u) != 0;

    if (ea_mode == 1) {  // An: always 32-bit, no flags modified
        a_[ea_reg] = is_sub ? (a_[ea_reg] - imm) : (a_[ea_reg] + imm);
        return 8;
    }

    ResolvedEA ea{};
    const uint32_t dst = read_modify_write_ea(ea_mode, ea_reg, size, ea);
    const uint32_t res = is_sub ? sub_with_flags(dst, imm, size, false, true)
                                : add_with_flags(dst, imm, size, false, true);
    write_ea(ea, size, res);
    return 4 + ea.cycles;
}

uint32_t M68k::exec_group6(uint16_t op) {
    const uint8_t cc = (op >> 8) & 0xFu;
    const uint32_t base = pc_;
    int32_t disp = static_cast<int8_t>(op & 0xFFu);
    if (disp == 0) {
        disp = static_cast<int16_t>(fetch16());
    } else if (disp == -1 && is_020()) {
        disp = static_cast<int32_t>(fetch32());
    }

    if (cc == 0x1) {  // BSR
        push32(pc_);
        pc_ = static_cast<uint32_t>(static_cast<int32_t>(base) + disp);
        return 18;
    }

    if (test_condition(cc)) {
        pc_ = static_cast<uint32_t>(static_cast<int32_t>(base) + disp);
        return 10;
    }
    return 8;
}

uint32_t M68k::exec_bcd_add(uint8_t dst, uint8_t src) {
    const uint8_t x = (sr_ & FLAG_X) ? 1u : 0u;
    uint16_t lo = (dst & 0x0Fu) + (src & 0x0Fu) + x;
    uint16_t carry = 0;
    if (lo > 9u) {
        lo -= 10u;
        carry = 1u;
    }
    uint16_t hi = (dst >> 4) + (src >> 4) + carry;
    bool c_out = false;
    if (hi > 9u) {
        hi -= 10u;
        c_out = true;
    }
    const uint8_t res = static_cast<uint8_t>(((hi & 0x0Fu) << 4) | (lo & 0x0Fu));
    sr_ &= static_cast<uint16_t>(~(FLAG_C | FLAG_X));
    if (c_out) {
        sr_ |= FLAG_C | FLAG_X;
    }
    if (res != 0) {
        sr_ &= static_cast<uint16_t>(~FLAG_Z);
    }
    return res;
}

uint32_t M68k::exec_bcd_sub(uint8_t dst, uint8_t src) {
    const uint8_t x = (sr_ & FLAG_X) ? 1u : 0u;
    int16_t lo = static_cast<int16_t>(dst & 0x0Fu) - static_cast<int16_t>(src & 0x0Fu) - x;
    int16_t borrow = 0;
    if (lo < 0) {
        lo += 10;
        borrow = 1;
    }
    int16_t hi = static_cast<int16_t>(dst >> 4) - static_cast<int16_t>(src >> 4) - borrow;
    bool c_out = false;
    if (hi < 0) {
        hi += 10;
        c_out = true;
    }
    const uint8_t res = static_cast<uint8_t>(((hi & 0x0Fu) << 4) | (lo & 0x0Fu));
    sr_ &= static_cast<uint16_t>(~(FLAG_C | FLAG_X));
    if (c_out) {
        sr_ |= FLAG_C | FLAG_X;
    }
    if (res != 0) {
        sr_ &= static_cast<uint16_t>(~FLAG_Z);
    }
    return res;
}

uint32_t M68k::exec_group8(uint16_t op) {
    const uint8_t size_bits = (op >> 6) & 0x3u;
    const uint8_t reg = (op >> 9) & 0x7u;
    const uint8_t ea_mode = (op >> 3) & 0x7u;
    const uint8_t ea_reg = op & 0x7u;

    // SBCD
    if ((op & 0x01F0u) == 0x0100u) {
        if ((op & 0x0008u) == 0) {
            const uint8_t res = static_cast<uint8_t>(exec_bcd_sub(static_cast<uint8_t>(d_[reg]),
                                                                  static_cast<uint8_t>(d_[ea_reg])));
            d_[reg] = (d_[reg] & 0xFFFFFF00u) | res;
            return 6;
        }
        a_[ea_reg] -= addr_increment(ea_reg, Size::Byte);
        const uint8_t src = bus_.read8(a_[ea_reg]);
        a_[reg] -= addr_increment(reg, Size::Byte);
        const uint8_t dst = bus_.read8(a_[reg]);
        const uint8_t res = static_cast<uint8_t>(exec_bcd_sub(dst, src));
        bus_.write8(a_[reg], res);
        return 18;
    }

    // DIVU.W / DIVS.W
    if (size_bits == 3) {
        const bool is_signed = (op & 0x0100u) != 0;
        const ResolvedEA ea = resolve_ea(ea_mode, ea_reg, Size::Word);
        const uint16_t divisor_u16 = static_cast<uint16_t>(read_ea(ea, Size::Word));
        if (divisor_u16 == 0) {
            raise_exception(5);  // Zero Divide
            return 38 + ea.cycles;
        }

        sr_ &= static_cast<uint16_t>(~(FLAG_C | FLAG_V));
        if (!is_signed) {
            const uint32_t dividend = d_[reg];
            const uint32_t quotient = dividend / divisor_u16;
            const uint32_t rem = dividend % divisor_u16;
            if (quotient > 0xFFFFu) {
                sr_ |= FLAG_V;
            } else {
                d_[reg] = (rem << 16) | (quotient & 0xFFFFu);
                update_nz_flags(quotient, Size::Word);
            }
            return 140 + ea.cycles;
        }

        const int32_t dividend = static_cast<int32_t>(d_[reg]);
        const int16_t divisor = static_cast<int16_t>(divisor_u16);
        if (dividend == INT32_MIN && divisor == -1) {
            sr_ |= FLAG_V;
            return 158 + ea.cycles;
        }
        const int32_t quotient = dividend / divisor;
        const int32_t rem = dividend % divisor;
        if (quotient < -32768 || quotient > 32767) {
            sr_ |= FLAG_V;
        } else {
            d_[reg] = (static_cast<uint32_t>(static_cast<uint16_t>(rem)) << 16) |
                      static_cast<uint16_t>(quotient);
            update_nz_flags(static_cast<uint32_t>(quotient), Size::Word);
        }
        return 158 + ea.cycles;
    }

    // OR
    const Size size = decode_size_00_01_10(size_bits);
    if ((op & 0x0100u) == 0) {  // OR <ea>, Dn
        const ResolvedEA ea = resolve_ea(ea_mode, ea_reg, size);
        const uint32_t res = (d_[reg] | read_ea(ea, size)) & size_mask(size);
        d_[reg] = (d_[reg] & ~size_mask(size)) | res;
        update_nz_flags(res, size);
        return 4 + ea.cycles;
    }
    ResolvedEA ea{};
    const uint32_t dst = read_modify_write_ea(ea_mode, ea_reg, size, ea);
    const uint32_t res = (dst | d_[reg]) & size_mask(size);
    write_ea(ea, size, res);
    update_nz_flags(res, size);
    return 8 + ea.cycles;
}

uint32_t M68k::exec_group9_or_d(uint16_t op, bool is_add) {
    const uint8_t reg = (op >> 9) & 0x7u;
    const uint8_t opmode = (op >> 6) & 0x7u;
    const uint8_t ea_mode = (op >> 3) & 0x7u;
    const uint8_t ea_reg = op & 0x7u;

    // ADDA / SUBA
    if (opmode == 3 || opmode == 7) {
        const Size size = (opmode == 3) ? Size::Word : Size::Long;
        const ResolvedEA ea = resolve_ea(ea_mode, ea_reg, size);
        const uint32_t src = sign_extend_to_32(read_ea(ea, size), size);
        a_[reg] = is_add ? (a_[reg] + src) : (a_[reg] - src);
        return 6 + ea.cycles;
    }

    const Size size = decode_size_00_01_10(opmode & 0x3u);

    // ADDX / SUBX
    if ((op & 0x0130u) == 0x0100u) {
        const bool prev_z = (sr_ & FLAG_Z) != 0;
        if ((op & 0x0008u) == 0) {  // Dn, Dn
            const uint32_t res = is_add ? add_with_flags(d_[reg], d_[ea_reg], size, true, true)
                                        : sub_with_flags(d_[reg], d_[ea_reg], size, true, true);
            if (res == 0 && prev_z) sr_ |= FLAG_Z;
            d_[reg] = (d_[reg] & ~size_mask(size)) | res;
            return (size == Size::Long) ? 8 : 4;
        }
        // -(Ay), -(Ax)
        a_[ea_reg] -= addr_increment(ea_reg, size);
        const uint32_t src = read_sized(bus_, a_[ea_reg], size);
        a_[reg] -= addr_increment(reg, size);
        const uint32_t dst = read_sized(bus_, a_[reg], size);
        const uint32_t res = is_add ? add_with_flags(dst, src, size, true, true)
                                    : sub_with_flags(dst, src, size, true, true);
        if (res == 0 && prev_z) sr_ |= FLAG_Z;
        write_sized(bus_, a_[reg], size, res);
        return (size == Size::Long) ? 30 : 18;
    }

    if ((opmode & 0x4u) == 0) {  // <ea> op Dn -> Dn
        const ResolvedEA ea = resolve_ea(ea_mode, ea_reg, size);
        const uint32_t src = read_ea(ea, size);
        const uint32_t res = is_add ? add_with_flags(d_[reg], src, size, false, true)
                                    : sub_with_flags(d_[reg], src, size, false, true);
        d_[reg] = (d_[reg] & ~size_mask(size)) | res;
        return 4 + ea.cycles;
    }

    // Dn op <ea> -> <ea>
    ResolvedEA ea{};
    const uint32_t dst = read_modify_write_ea(ea_mode, ea_reg, size, ea);
    const uint32_t res = is_add ? add_with_flags(dst, d_[reg], size, false, true)
                                : sub_with_flags(dst, d_[reg], size, false, true);
    write_ea(ea, size, res);
    return 8 + ea.cycles;
}

uint32_t M68k::exec_group_b(uint16_t op) {
    const uint8_t reg = (op >> 9) & 0x7u;
    const uint8_t opmode = (op >> 6) & 0x7u;
    const uint8_t ea_mode = (op >> 3) & 0x7u;
    const uint8_t ea_reg = op & 0x7u;

    // CMPA
    if (opmode == 3 || opmode == 7) {
        const Size size = (opmode == 3) ? Size::Word : Size::Long;
        const ResolvedEA ea = resolve_ea(ea_mode, ea_reg, size);
        const uint32_t src = sign_extend_to_32(read_ea(ea, size), size);
        (void)sub_with_flags(a_[reg], src, Size::Long, false, false);
        return 6 + ea.cycles;
    }

    const Size size = decode_size_00_01_10(opmode & 0x3u);

    // CMPM (Ay)+, (Ax)+
    if ((op & 0x0138u) == 0x0108u) {
        const uint32_t src = read_sized(bus_, a_[ea_reg], size);
        a_[ea_reg] += addr_increment(ea_reg, size);
        const uint32_t dst = read_sized(bus_, a_[reg], size);
        a_[reg] += addr_increment(reg, size);
        (void)sub_with_flags(dst, src, size, false, false);
        return (size == Size::Long) ? 20 : 12;
    }

    if ((opmode & 0x4u) == 0) {  // CMP <ea>, Dn
        const ResolvedEA ea = resolve_ea(ea_mode, ea_reg, size);
        const uint32_t src = read_ea(ea, size);
        (void)sub_with_flags(d_[reg], src, size, false, false);
        return 4 + ea.cycles;
    }

    // EOR Dn, <ea>
    ResolvedEA ea{};
    const uint32_t dst = read_modify_write_ea(ea_mode, ea_reg, size, ea);
    const uint32_t res = (dst ^ d_[reg]) & size_mask(size);
    write_ea(ea, size, res);
    update_nz_flags(res, size);
    return 4 + ea.cycles;
}

uint32_t M68k::exec_group_c(uint16_t op) {
    const uint8_t size_bits = (op >> 6) & 0x3u;
    const uint8_t reg = (op >> 9) & 0x7u;
    const uint8_t ea_mode = (op >> 3) & 0x7u;
    const uint8_t ea_reg = op & 0x7u;

    // ABCD
    if ((op & 0x01F0u) == 0x0100u) {
        if ((op & 0x0008u) == 0) {
            const uint8_t res = static_cast<uint8_t>(exec_bcd_add(static_cast<uint8_t>(d_[reg]),
                                                                  static_cast<uint8_t>(d_[ea_reg])));
            d_[reg] = (d_[reg] & 0xFFFFFF00u) | res;
            return 6;
        }
        a_[ea_reg] -= addr_increment(ea_reg, Size::Byte);
        const uint8_t src = bus_.read8(a_[ea_reg]);
        a_[reg] -= addr_increment(reg, Size::Byte);
        const uint8_t dst = bus_.read8(a_[reg]);
        const uint8_t res = static_cast<uint8_t>(exec_bcd_add(dst, src));
        bus_.write8(a_[reg], res);
        return 18;
    }

    // EXG
    if ((op & 0x0130u) == 0x0100u) {
        const uint8_t mode = (op >> 3) & 0x1Fu;
        if (mode == 0x08u) {  // Dx, Dy
            std::swap(d_[reg], d_[ea_reg]);
            return 6;
        }
        if (mode == 0x09u) {  // Ax, Ay
            std::swap(a_[reg], a_[ea_reg]);
            return 6;
        }
        if (mode == 0x11u) {  // Dx, Ay
            std::swap(d_[reg], a_[ea_reg]);
            return 6;
        }
    }

    // MULU.W / MULS.W
    if (size_bits == 3) {
        const bool is_signed = (op & 0x0100u) != 0;
        const ResolvedEA ea = resolve_ea(ea_mode, ea_reg, Size::Word);
        const uint16_t src = static_cast<uint16_t>(read_ea(ea, Size::Word));
        uint32_t res = 0;
        if (is_signed) {
            res = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(d_[reg] & 0xFFFFu)) *
                                        static_cast<int32_t>(static_cast<int16_t>(src)));
        } else {
            res = (d_[reg] & 0xFFFFu) * static_cast<uint32_t>(src);
        }
        d_[reg] = res;
        update_nz_flags(res, Size::Long);
        return 70 + ea.cycles;
    }

    // AND
    const Size size = decode_size_00_01_10(size_bits);
    if ((op & 0x0100u) == 0) {  // AND <ea>, Dn
        const ResolvedEA ea = resolve_ea(ea_mode, ea_reg, size);
        const uint32_t res = (d_[reg] & read_ea(ea, size)) & size_mask(size);
        d_[reg] = (d_[reg] & ~size_mask(size)) | res;
        update_nz_flags(res, size);
        return 4 + ea.cycles;
    }
    ResolvedEA ea{};
    const uint32_t dst = read_modify_write_ea(ea_mode, ea_reg, size, ea);
    const uint32_t res = (dst & d_[reg]) & size_mask(size);
    write_ea(ea, size, res);
    update_nz_flags(res, size);
    return 8 + ea.cycles;
}

uint32_t M68k::exec_shift(uint8_t type, bool left, uint32_t val, uint32_t count, Size size) {
    const uint32_t mask = size_mask(size);
    const uint32_t sbit = size_sign_bit(size);
    const uint8_t width = static_cast<uint8_t>(static_cast<uint8_t>(size) * 8u);
    uint32_t res = val & mask;

    sr_ &= static_cast<uint16_t>(~(FLAG_C | FLAG_V | FLAG_Z | FLAG_N));
    if (count == 0) {
        if (type == 2 && (sr_ & FLAG_X) != 0) {  // ROXL/ROXR with count 0 copies X to C
            sr_ |= FLAG_C;
        }
        if (res == 0) sr_ |= FLAG_Z;
        if ((res & sbit) != 0) sr_ |= FLAG_N;
        return res;
    }

    bool last_out = false;
    bool overflow = false;
    uint32_t x_bit = (sr_ & FLAG_X) ? 1u : 0u;

    for (uint32_t i = 0; i < count; ++i) {
        if (left) {
            last_out = (res & sbit) != 0;
            const uint32_t old_sign = res & sbit;
            res = (res << 1) & mask;
            if (type == 2) {  // ROXL
                res |= x_bit;
                x_bit = last_out ? 1u : 0u;
            } else if (type == 3) {  // ROL
                if (last_out) res |= 1u;
            } else if (type == 0) {  // ASL
                if ((res & sbit) != old_sign) overflow = true;
            }
        } else {
            last_out = (res & 1u) != 0;
            const uint32_t sign = res & sbit;
            res >>= 1;
            if (type == 0 && sign != 0) {  // ASR
                res |= sbit;
            } else if (type == 2) {  // ROXR
                if (x_bit != 0) res |= sbit;
                x_bit = last_out ? 1u : 0u;
            } else if (type == 3) {  // ROR
                if (last_out) res |= (1u << (width - 1u));
            }
        }
    }

    if (last_out) sr_ |= FLAG_C;
    if (type != 3) {  // ROL/ROR do not affect X
        if (last_out) sr_ |= FLAG_X;
        else sr_ &= static_cast<uint16_t>(~FLAG_X);
    }
    if (overflow) sr_ |= FLAG_V;
    if (res == 0) sr_ |= FLAG_Z;
    if ((res & sbit) != 0) sr_ |= FLAG_N;
    return res;
}

uint32_t M68k::exec_bitfield(uint16_t op) {
    const uint16_t ext = fetch16();
    const uint8_t ea_mode = (op >> 3) & 0x7u;
    const uint8_t ea_reg = op & 0x7u;
    const uint8_t kind = (op >> 8) & 0x7u;
    const uint8_t reg = (ext >> 12) & 0x7u;
    const int32_t offset = (ext & 0x0800u)
                               ? static_cast<int32_t>(d_[(ext >> 6) & 0x7u])
                               : static_cast<int32_t>((ext >> 6) & 0x1Fu);
    uint32_t width = (ext & 0x0020u) ? (d_[ext & 0x7u] & 0x1Fu) : (ext & 0x1Fu);
    if (width == 0) width = 32;

    const uint64_t field_mask = (width == 32) ? 0xFFFFFFFFull : ((1ull << width) - 1ull);
    uint64_t field_val = 0;
    ResolvedEA ea{};
    uint32_t base_byte = 0;
    uint32_t bit_off = 0;
    uint32_t num_bytes = 0;

    if (ea_mode == 0) {
        const uint32_t start_bit = static_cast<uint32_t>(offset) & 31u;
        const uint32_t v = d_[ea_reg];
        for (uint32_t i = 0; i < width; ++i) {
            const uint32_t b = 31u - ((start_bit + i) & 31u);
            field_val = (field_val << 1) | ((v >> b) & 1u);
        }
    } else {
        ea = resolve_ea(ea_mode, ea_reg, Size::Byte);
        const int32_t byte_delta = (offset >= 0) ? (offset >> 3) : -((-offset + 7) >> 3);
        base_byte = static_cast<uint32_t>(static_cast<int32_t>(ea.addr) + byte_delta);
        bit_off = static_cast<uint32_t>(offset - byte_delta * 8);
        num_bytes = (bit_off + width + 7u) / 8u;
        uint64_t raw = 0;
        for (uint32_t i = 0; i < num_bytes; ++i) {
            raw = (raw << 8) | bus_.read8(base_byte + i);
        }
        const uint32_t shift_down = num_bytes * 8u - (bit_off + width);
        field_val = (raw >> shift_down) & field_mask;
    }

    uint64_t new_field = field_val;
    bool modify = false;
    uint64_t flag_val = field_val;

    switch (kind) {
        case 0:  // BFTST
            break;
        case 1:  // BFEXTU
            d_[reg] = static_cast<uint32_t>(field_val);
            break;
        case 2:  // BFCHG
            new_field = field_val ^ field_mask;
            modify = true;
            break;
        case 3:  // BFEXTS
            if ((field_val & (1ull << (width - 1u))) != 0 && width < 32) {
                d_[reg] = static_cast<uint32_t>(field_val | (~field_mask & 0xFFFFFFFFull));
            } else {
                d_[reg] = static_cast<uint32_t>(field_val);
            }
            break;
        case 4:  // BFCLR
            new_field = 0;
            modify = true;
            break;
        case 5: {  // BFFFO
            uint32_t first_one = width;
            for (uint32_t i = 0; i < width; ++i) {
                if ((field_val & (1ull << (width - 1u - i))) != 0) {
                    first_one = i;
                    break;
                }
            }
            d_[reg] = static_cast<uint32_t>(offset + static_cast<int32_t>(first_one));
            break;
        }
        case 6:  // BFSET
            new_field = field_mask;
            modify = true;
            break;
        case 7:  // BFINS
            new_field = static_cast<uint64_t>(d_[reg]) & field_mask;
            flag_val = new_field;
            modify = true;
            break;
    }

    if (modify) {
        if (ea_mode == 0) {
            const uint32_t start_bit = static_cast<uint32_t>(offset) & 31u;
            uint32_t v = d_[ea_reg];
            for (uint32_t i = 0; i < width; ++i) {
                const uint32_t b = 31u - ((start_bit + i) & 31u);
                const uint32_t bit = static_cast<uint32_t>((new_field >> (width - 1u - i)) & 1u);
                v = (v & ~(1u << b)) | (bit << b);
            }
            d_[ea_reg] = v;
        } else {
            uint64_t raw = 0;
            for (uint32_t i = 0; i < num_bytes; ++i) {
                raw = (raw << 8) | bus_.read8(base_byte + i);
            }
            const uint32_t shift_down = num_bytes * 8u - (bit_off + width);
            raw = (raw & ~(field_mask << shift_down)) | (new_field << shift_down);
            for (uint32_t i = 0; i < num_bytes; ++i) {
                const uint8_t b = static_cast<uint8_t>((raw >> ((num_bytes - 1u - i) * 8u)) & 0xFFu);
                bus_.write8(base_byte + i, b);
            }
        }
    }

    sr_ &= static_cast<uint16_t>(~(FLAG_C | FLAG_V | FLAG_Z | FLAG_N));
    if (flag_val == 0) sr_ |= FLAG_Z;
    if ((flag_val & (1ull << (width - 1u))) != 0) sr_ |= FLAG_N;
    return 12 + ea.cycles;
}

uint32_t M68k::exec_group_e(uint16_t op) {
    const uint8_t size_bits = (op >> 6) & 0x3u;
    const bool left = (op & 0x0100u) != 0;

    // 68020 bitfield instructions (0xE8C0..0xEFFF where bits 7..6 == 11 and bit 11 == 1)
    if (size_bits == 3 && (op & 0x0800u) != 0 && is_020()) {
        return exec_bitfield(op);
    }

    // Memory shift/rotate (always Word, count = 1)
    if (size_bits == 3) {
        const uint8_t type = (op >> 9) & 0x3u;
        const uint8_t ea_mode = (op >> 3) & 0x7u;
        const uint8_t ea_reg = op & 0x7u;
        ResolvedEA ea{};
        const uint32_t val = read_modify_write_ea(ea_mode, ea_reg, Size::Word, ea);
        const uint32_t res = exec_shift(type, left, val, 1, Size::Word);
        write_ea(ea, Size::Word, res);
        return 8 + ea.cycles;
    }

    // Register shift/rotate
    const Size size = decode_size_00_01_10(size_bits);
    const uint8_t type = (op >> 3) & 0x3u;
    const uint8_t reg = op & 0x7u;
    const uint8_t count_field = (op >> 9) & 0x7u;
    const uint32_t count = ((op & 0x0020u) != 0) ? (d_[count_field] & 63u)
                                                 : (count_field == 0 ? 8u : count_field);
    const uint32_t res = exec_shift(type, left, d_[reg], count, size);
    d_[reg] = (d_[reg] & ~size_mask(size)) | res;
    return ((size == Size::Long) ? 8u : 6u) + 2u * count;
}

namespace {

double fpu_ext96_to_double(uint32_t hi, uint64_t mant) {
    const bool neg = (hi & 0x80000000u) != 0;
    const int exp = static_cast<int>((hi >> 16) & 0x7FFFu);
    if (exp == 0 && mant == 0) return neg ? -0.0 : 0.0;
    if (exp == 0x7FFF) {
        if ((mant & 0x7FFFFFFFFFFFFFFFull) == 0) return neg ? -INFINITY : INFINITY;
        return NAN;
    }
    const double val = std::ldexp(static_cast<double>(mant), exp - 16383 - 63);
    return neg ? -val : val;
}

void fpu_double_to_ext96(double v, uint32_t& out_hi, uint64_t& out_mant) {
    uint64_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    const uint32_t sign = static_cast<uint32_t>((bits >> 63) & 1u) << 31;
    if (std::isnan(v)) {
        out_hi = sign | 0x7FFF0000u;
        out_mant = 0xFFFFFFFFFFFFFFFFull;
        return;
    }
    if (std::isinf(v)) {
        out_hi = sign | 0x7FFF0000u;
        out_mant = 0x8000000000000000ull;
        return;
    }
    if (v == 0.0) {
        out_hi = sign;
        out_mant = 0;
        return;
    }
    int exp2 = 0;
    const double m = std::frexp(std::fabs(v), &exp2);  // m in [0.5, 1.0)
    const int ext_exp = exp2 + 16382;
    if (ext_exp <= 0) {
        out_hi = sign;
        out_mant = 0;
        return;
    }
    if (ext_exp >= 0x7FFF) {
        out_hi = sign | 0x7FFF0000u;
        out_mant = 0x8000000000000000ull;
        return;
    }
    out_hi = sign | (static_cast<uint32_t>(ext_exp) << 16);
    out_mant = static_cast<uint64_t>(std::ldexp(m, 64));
}

bool fpu_test_cc(uint8_t cc, uint32_t fpsr) {
    const bool n = (fpsr & (1u << 27)) != 0;
    const bool z = (fpsr & (1u << 26)) != 0;
    const bool nan = (fpsr & (1u << 24)) != 0;
    switch (cc & 0x0Fu) {
        case 0x00: return false;              // F
        case 0x01: return z;                  // EQ
        case 0x02: return !nan && !z && !n;   // OGT
        case 0x03: return !nan && (z || !n);  // OGE
        case 0x04: return !nan && !z && n;    // OLT
        case 0x05: return !nan && (z || n);   // OLE
        case 0x06: return !nan && !z;         // OGL
        case 0x07: return !nan;               // OR
        case 0x08: return nan;                // UN
        case 0x09: return nan || z;           // UEQ
        case 0x0A: return nan || (!z && !n);  // UGT
        case 0x0B: return nan || z || !n;     // UGE
        case 0x0C: return nan || (!z && n);   // ULT
        case 0x0D: return nan || z || n;      // ULE
        case 0x0E: return !z;                 // NE
        case 0x0F: return true;               // T
    }
    return false;
}

}  // namespace

uint32_t M68k::exec_fpu(uint16_t op) {
    const uint8_t type = (op >> 6) & 0x7u;
    const uint8_t ea_mode = (op >> 3) & 0x7u;
    const uint8_t ea_reg = op & 0x7u;

    auto update_fpcc = [&](double v) {
        fpsr_ &= ~0x0F000000u;
        if (std::isnan(v)) {
            fpsr_ |= (1u << 24);
        } else {
            if (std::signbit(v)) fpsr_ |= (1u << 27);
            if (v == 0.0) fpsr_ |= (1u << 26);
            if (std::isinf(v)) fpsr_ |= (1u << 25);
        }
    };

    if (type == 2) {  // FBcc.W (and FNOP when op==0xF280 && disp==0)
        const int16_t disp = static_cast<int16_t>(fetch16());
        const uint8_t cc = op & 0x3Fu;
        if (fpu_test_cc(cc, fpsr_)) {
            pc_ = static_cast<uint32_t>(static_cast<int32_t>(instr_pc_ + 2u) + disp);
            return 16;
        }
        return 12;
    }

    if (type == 3) {  // FBcc.L
        const int32_t disp = static_cast<int32_t>(fetch32());
        const uint8_t cc = op & 0x3Fu;
        if (fpu_test_cc(cc, fpsr_)) {
            pc_ = static_cast<uint32_t>(static_cast<int32_t>(instr_pc_ + 2u) + disp);
            return 16;
        }
        return 12;
    }

    if (type == 4) {  // FSAVE <ea> -> write 4-byte null state frame
        if (ea_mode == 4) {
            a_[ea_reg] -= 4u;
            bus_.write32(a_[ea_reg], 0u);
        } else {
            const ResolvedEA ea = resolve_ea(ea_mode, ea_reg, Size::Long);
            write_ea(ea, Size::Long, 0u);
        }
        return 16;
    }

    if (type == 5) {  // FRESTORE <ea>
        uint32_t fmt = 0;
        if (ea_mode == 3) {
            fmt = bus_.read32(a_[ea_reg]);
            a_[ea_reg] += 4u;
        } else {
            const ResolvedEA ea = resolve_ea(ea_mode, ea_reg, Size::Long);
            fmt = read_ea(ea, Size::Long);
        }
        const uint32_t frame_bytes = (fmt >> 16) & 0xFFu;
        if ((fmt & 0xFF000000u) == 0) {
            fp_.fill(0.0);
            fpsr_ = 0;
        } else if (ea_mode == 3 && frame_bytes > 0) {
            a_[ea_reg] += frame_bytes;
        }
        return 16;
    }

    if (type == 1) {  // FScc / FDBcc / FTRAPcc
        const uint16_t ext = fetch16();
        const bool cond = fpu_test_cc(static_cast<uint8_t>(ext & 0x3Fu), fpsr_);
        if (ea_mode == 1) {  // FDBcc Dn, disp16
            const int16_t disp = static_cast<int16_t>(fetch16());
            if (!cond) {
                const uint16_t cnt = static_cast<uint16_t>((d_[ea_reg] & 0xFFFFu) - 1u);
                d_[ea_reg] = (d_[ea_reg] & 0xFFFF0000u) | cnt;
                if (cnt != 0xFFFFu) {
                    pc_ = static_cast<uint32_t>(static_cast<int32_t>(instr_pc_ + 4u) + disp);
                }
            }
            return 14;
        }
        if (ea_mode == 7 && ea_reg >= 2) {  // FTRAPcc
            if (ea_reg == 2) (void)fetch16();
            else if (ea_reg == 3) (void)fetch32();
            if (cond) raise_exception(7);
            return 12;
        }
        const ResolvedEA ea = resolve_ea(ea_mode, ea_reg, Size::Byte);
        write_ea(ea, Size::Byte, cond ? 0xFFu : 0x00u);
        return 8 + ea.cycles;
    }

    if (type != 0) {
        raise_exception(11);
        return 34;
    }

    // type == 0: cpGEN
    const uint16_t cmd = fetch16();
    const uint8_t top = static_cast<uint8_t>((cmd >> 13) & 0x7u);

    auto read_ext96_ea = [&](uint8_t mode, uint8_t reg) -> double {
        uint32_t addr = 0;
        if (mode == 3) {
            addr = a_[reg];
            a_[reg] += 12u;
        } else if (mode == 4) {
            a_[reg] -= 12u;
            addr = a_[reg];
        } else {
            addr = resolve_ea(mode, reg, Size::Long).addr;
        }
        const uint32_t hi = bus_.read32(addr);
        const uint64_t mant = (static_cast<uint64_t>(bus_.read32(addr + 4u)) << 32) |
                              bus_.read32(addr + 8u);
        return fpu_ext96_to_double(hi, mant);
    };

    auto write_ext96_ea = [&](uint8_t mode, uint8_t reg, double val) {
        uint32_t addr = 0;
        if (mode == 3) {
            addr = a_[reg];
            a_[reg] += 12u;
        } else if (mode == 4) {
            a_[reg] -= 12u;
            addr = a_[reg];
        } else {
            addr = resolve_ea(mode, reg, Size::Long).addr;
        }
        uint32_t hi = 0;
        uint64_t mant = 0;
        fpu_double_to_ext96(val, hi, mant);
        bus_.write32(addr, hi);
        bus_.write32(addr + 4u, static_cast<uint32_t>(mant >> 32));
        bus_.write32(addr + 8u, static_cast<uint32_t>(mant));
    };

    auto read_src_fmt = [&](uint8_t fmt) -> double {
        switch (fmt) {
            case 0: {  // Long
                const ResolvedEA ea = resolve_ea(ea_mode, ea_reg, Size::Long);
                return static_cast<double>(static_cast<int32_t>(read_ea(ea, Size::Long)));
            }
            case 1: {  // Single
                const ResolvedEA ea = resolve_ea(ea_mode, ea_reg, Size::Long);
                const uint32_t bits = read_ea(ea, Size::Long);
                float f = 0.0f;
                std::memcpy(&f, &bits, sizeof(f));
                return static_cast<double>(f);
            }
            case 2:  // Extended (96-bit)
                return read_ext96_ea(ea_mode, ea_reg);
            case 4: {  // Word
                const ResolvedEA ea = resolve_ea(ea_mode, ea_reg, Size::Word);
                return static_cast<double>(static_cast<int16_t>(read_ea(ea, Size::Word)));
            }
            case 5: {  // Double (64-bit)
                uint32_t addr = 0;
                if (ea_mode == 3) { addr = a_[ea_reg]; a_[ea_reg] += 8u; }
                else if (ea_mode == 4) { a_[ea_reg] -= 8u; addr = a_[ea_reg]; }
                else addr = resolve_ea(ea_mode, ea_reg, Size::Long).addr;
                const uint64_t bits = (static_cast<uint64_t>(bus_.read32(addr)) << 32) | bus_.read32(addr + 4u);
                double d = 0.0;
                std::memcpy(&d, &bits, sizeof(d));
                return d;
            }
            case 6: {  // Byte
                const ResolvedEA ea = resolve_ea(ea_mode, ea_reg, Size::Byte);
                return static_cast<double>(static_cast<int8_t>(read_ea(ea, Size::Byte)));
            }
            default:
                return 0.0;
        }
    };

    // FMOVECR #rom_offset, FPn (cmd & 0xFC00 == 0x5C00)
    if ((cmd & 0xFC00u) == 0x5C00u) {
        const uint8_t dst = (cmd >> 7) & 0x7u;
        const uint8_t rom_off = cmd & 0x7Fu;
        double val = 0.0;
        switch (rom_off) {
            case 0x00: val = 3.14159265358979323846; break;
            case 0x0B: val = 0.30102999566398119521; break;  // log10(2)
            case 0x0C: val = 2.71828182845904523536; break;  // e
            case 0x0D: val = 1.44269504088896340736; break;  // log2(e)
            case 0x0E: val = 0.43429448190325182765; break;  // log10(e)
            case 0x0F: val = 0.0; break;
            case 0x30: val = 0.69314718055994530942; break;  // ln(2)
            case 0x31: val = 2.30258509299404568402; break;  // ln(10)
            case 0x32: val = 1.0; break;
            case 0x33: val = 10.0; break;
            case 0x34: val = 100.0; break;
            case 0x35: val = 1e4; break;
            case 0x36: val = 1e8; break;
            case 0x37: val = 1e16; break;
            case 0x38: val = 1e32; break;
            case 0x39: val = 1e64; break;
            case 0x3A: val = 1e128; break;
            case 0x3B: val = 1e256; break;
            default: val = 0.0; break;
        }
        fp_[dst] = val;
        update_fpcc(val);
        return 12;
    }

    // FMOVE FPn, <ea> (top == 3)
    if (top == 3) {
        const uint8_t src_fp = (cmd >> 7) & 0x7u;
        const uint8_t fmt = (cmd >> 10) & 0x7u;
        const double val = fp_[src_fp];
        switch (fmt) {
            case 0: {  // Long
                const ResolvedEA ea = resolve_ea(ea_mode, ea_reg, Size::Long);
                write_ea(ea, Size::Long, static_cast<uint32_t>(static_cast<int32_t>(std::llrint(val))));
                break;
            }
            case 1: {  // Single
                const float f = static_cast<float>(val);
                uint32_t bits = 0;
                std::memcpy(&bits, &f, sizeof(bits));
                const ResolvedEA ea = resolve_ea(ea_mode, ea_reg, Size::Long);
                write_ea(ea, Size::Long, bits);
                break;
            }
            case 2:  // Extended
                write_ext96_ea(ea_mode, ea_reg, val);
                break;
            case 4: {  // Word
                const ResolvedEA ea = resolve_ea(ea_mode, ea_reg, Size::Word);
                write_ea(ea, Size::Word, static_cast<uint16_t>(static_cast<int16_t>(std::llrint(val))));
                break;
            }
            case 5: {  // Double
                uint64_t bits = 0;
                std::memcpy(&bits, &val, sizeof(bits));
                uint32_t addr = 0;
                if (ea_mode == 3) { addr = a_[ea_reg]; a_[ea_reg] += 8u; }
                else if (ea_mode == 4) { a_[ea_reg] -= 8u; addr = a_[ea_reg]; }
                else addr = resolve_ea(ea_mode, ea_reg, Size::Long).addr;
                bus_.write32(addr, static_cast<uint32_t>(bits >> 32));
                bus_.write32(addr + 4u, static_cast<uint32_t>(bits));
                break;
            }
            case 6: {  // Byte
                const ResolvedEA ea = resolve_ea(ea_mode, ea_reg, Size::Byte);
                write_ea(ea, Size::Byte, static_cast<uint8_t>(static_cast<int8_t>(std::llrint(val))));
                break;
            }
            default:
                break;
        }
        return 16;
    }

    // FMOVE FPCR/FPSR/FPIAR <-> <ea> (top == 4 or 5)
    if (top == 4 || top == 5) {
        const bool to_mem = (top == 5);
        const uint8_t regsel = (cmd >> 10) & 0x7u;
        uint32_t* regs[3] = { &fpcr_, &fpsr_, &fpiar_ };
        if (ea_mode == 0 || ea_mode == 1) {
            const uint8_t idx = (regsel & 4u) ? 0 : ((regsel & 2u) ? 1 : 2);
            uint32_t& gp = (ea_mode == 0) ? d_[ea_reg] : a_[ea_reg];
            if (to_mem) gp = *regs[idx];
            else *regs[idx] = gp;
        } else {
            uint32_t count = 0;
            for (int b = 0; b < 3; ++b) if ((regsel & (4u >> b)) != 0) ++count;
            uint32_t addr = 0;
            if (ea_mode == 4) { a_[ea_reg] -= count * 4u; addr = a_[ea_reg]; }
            else if (ea_mode == 3) { addr = a_[ea_reg]; a_[ea_reg] += count * 4u; }
            else addr = resolve_ea(ea_mode, ea_reg, Size::Long).addr;
            for (int b = 0; b < 3; ++b) {
                if ((regsel & (4u >> b)) != 0) {
                    if (to_mem) bus_.write32(addr, *regs[b]);
                    else *regs[b] = bus_.read32(addr);
                    addr += 4u;
                }
            }
        }
        return 16;
    }

    // FMOVEM.X (top == 6 or 7)
    // mode (bits 12..11): 0=static predec, 1=dynamic predec, 2=static postinc/ctrl, 3=dynamic postinc/ctrl
    if (top == 6 || top == 7) {
        const bool to_mem = (top == 7);
        const uint8_t mode = (cmd >> 11) & 0x3u;
        const bool predec = (mode == 0 || mode == 1);
        const uint8_t mask = (mode == 1 || mode == 3)
                                 ? static_cast<uint8_t>(d_[(cmd >> 4) & 0x7u] & 0xFFu)
                                 : static_cast<uint8_t>(cmd & 0xFFu);
        uint32_t count = 0;
        for (int i = 0; i < 8; ++i) if ((mask & (1u << i)) != 0) ++count;
        uint32_t addr = 0;
        if (ea_mode == 4) {
            addr = a_[ea_reg];
        } else if (ea_mode == 3) {
            addr = a_[ea_reg];
            a_[ea_reg] += count * 12u;
        } else {
            addr = resolve_ea(ea_mode, ea_reg, Size::Long).addr;
        }
        if (predec && ea_mode == 4) {
            for (int fp = 7; fp >= 0; --fp) {
                if ((mask & (1u << fp)) != 0) {
                    addr -= 12u;
                    if (to_mem) {
                        uint32_t hi = 0;
                        uint64_t mant = 0;
                        fpu_double_to_ext96(fp_[fp], hi, mant);
                        bus_.write32(addr, hi);
                        bus_.write32(addr + 4u, static_cast<uint32_t>(mant >> 32));
                        bus_.write32(addr + 8u, static_cast<uint32_t>(mant));
                    } else {
                        const uint32_t hi = bus_.read32(addr);
                        const uint64_t mant = (static_cast<uint64_t>(bus_.read32(addr + 4u)) << 32) |
                                              bus_.read32(addr + 8u);
                        fp_[fp] = fpu_ext96_to_double(hi, mant);
                    }
                }
            }
            a_[ea_reg] = addr;
        } else {
            for (int fp = 0; fp < 8; ++fp) {
                if ((mask & (0x80u >> fp)) != 0) {
                    if (to_mem) {
                        uint32_t hi = 0;
                        uint64_t mant = 0;
                        fpu_double_to_ext96(fp_[fp], hi, mant);
                        bus_.write32(addr, hi);
                        bus_.write32(addr + 4u, static_cast<uint32_t>(mant >> 32));
                        bus_.write32(addr + 8u, static_cast<uint32_t>(mant));
                    } else {
                        const uint32_t hi = bus_.read32(addr);
                        const uint64_t mant = (static_cast<uint64_t>(bus_.read32(addr + 4u)) << 32) |
                                              bus_.read32(addr + 8u);
                        fp_[fp] = fpu_ext96_to_double(hi, mant);
                    }
                    addr += 12u;
                }
            }
        }
        return 16 + 8 * count;
    }

    // top == 0 (FPm -> FPn) or top == 2 (<ea> -> FPn)
    const bool rm = (cmd & 0x4000u) != 0;
    const uint8_t src_field = (cmd >> 10) & 0x7u;
    const uint8_t dst_fp = (cmd >> 7) & 0x7u;
    const uint8_t opmode = cmd & 0x7Fu;
    const double src = rm ? read_src_fmt(src_field) : fp_[src_field];
    double res = fp_[dst_fp];

    switch (opmode) {
        case 0x00: res = src; break;                              // FMOVE
        case 0x01: res = std::rint(src); break;                   // FINT
        case 0x02: res = std::sinh(src); break;                   // FSINH
        case 0x03: res = std::trunc(src); break;                  // FINTRZ
        case 0x04: res = std::sqrt(src); break;                   // FSQRT
        case 0x06: res = std::log1p(src); break;                  // FLOGNP1
        case 0x08: res = std::exp(src) - 1.0; break;              // FETOXM1
        case 0x09: res = std::tanh(src); break;                   // FTANH
        case 0x0A: res = std::atan(src); break;                   // FATAN
        case 0x0C: res = std::asin(src); break;                   // FASIN
        case 0x0D: res = std::atanh(src); break;                  // FATANH
        case 0x0E: res = std::sin(src); break;                    // FSIN
        case 0x0F: res = std::tan(src); break;                    // FTAN
        case 0x10: res = std::exp(src); break;                    // FETOX
        case 0x11: res = std::exp2(src); break;                   // FTWOTOX
        case 0x12: res = std::pow(10.0, src); break;              // FTENTOX
        case 0x14: res = std::log(src); break;                    // FLOGN
        case 0x15: res = std::log10(src); break;                  // FLOG10
        case 0x16: res = std::log2(src); break;                   // FLOG2
        case 0x18: res = std::fabs(src); break;                   // FABS
        case 0x19: res = std::cosh(src); break;                   // FCOSH
        case 0x1A: res = -src; break;                             // FNEG
        case 0x1C: res = std::acos(src); break;                   // FACOS
        case 0x1D: res = std::cos(src); break;                    // FCOS
        case 0x1E: { int e = 0; (void)std::frexp(src, &e); res = (src == 0.0) ? 0.0 : static_cast<double>(e - 1); break; }
        case 0x1F: { int e = 0; res = std::frexp(src, &e) * 2.0; break; }
        case 0x20: case 0x24: res = fp_[dst_fp] / src; break;     // FDIV / FSGLDIV
        case 0x21: res = std::fmod(fp_[dst_fp], src); break;      // FMOD
        case 0x22: res = fp_[dst_fp] + src; break;                // FADD
        case 0x23: case 0x27: res = fp_[dst_fp] * src; break;     // FMUL / FSGLMUL
        case 0x25: res = std::remainder(fp_[dst_fp], src); break; // FREM
        case 0x26: res = std::ldexp(fp_[dst_fp], static_cast<int>(src)); break;  // FSCALE
        case 0x28: res = fp_[dst_fp] - src; break;                // FSUB
        case 0x30: case 0x31: case 0x32: case 0x33:
        case 0x34: case 0x35: case 0x36: case 0x37:               // FSINCOS
            fp_[opmode & 0x7u] = std::cos(src);
            res = std::sin(src);
            break;
        case 0x38:                                                // FCMP
            update_fpcc(fp_[dst_fp] - src);
            return 16;
        case 0x3A:                                                // FTST
            update_fpcc(src);
            return 12;
        default:
            break;
    }

    fp_[dst_fp] = res;
    update_fpcc(res);
    return 16;
}

}  // namespace amiga
