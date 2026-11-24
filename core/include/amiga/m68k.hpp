#pragma once

#include "amiga/bus.hpp"

#include <array>
#include <cstdint>

namespace amiga {

/// Operand size for 68000 instructions.
enum class Size : uint8_t {
    Byte = 1,
    Word = 2,
    Long = 4,
};

/// Motorola 68000 / 68EC020 CPU core.
class M68k {
public:
    enum class CpuModel : uint8_t {
        M68000 = 0,
        M68EC020 = 1,
        M68030 = 2,
    };

    /// Status register flag bit masks.
    static constexpr uint16_t FLAG_C = 1u << 0;
    static constexpr uint16_t FLAG_V = 1u << 1;
    static constexpr uint16_t FLAG_Z = 1u << 2;
    static constexpr uint16_t FLAG_N = 1u << 3;
    static constexpr uint16_t FLAG_X = 1u << 4;
    static constexpr uint16_t FLAG_IPL_MASK = 0x0700u;
    static constexpr uint16_t FLAG_S = 1u << 13;
    static constexpr uint16_t FLAG_T = 1u << 15;
    static constexpr uint16_t SR_MASK = 0xA71Fu;

    explicit M68k(Bus& bus);

    void set_cpu_model(CpuModel model) { cpu_model_ = model; }
    CpuModel cpu_model() const { return cpu_model_; }
    bool is_020() const { return cpu_model_ == CpuModel::M68EC020 || cpu_model_ == CpuModel::M68030; }
    bool is_32bit() const { return cpu_model_ == CpuModel::M68030; }

    /// Performs a hardware reset: loads SSP from $000000 and PC from $000004.
    void reset();

    /// Executes one instruction and returns the number of CPU clock cycles consumed.
    uint32_t step();

    /// Raises an exception for the given vector number (0..255).
    void raise_exception(uint8_t vector);

    /// Sets the current external interrupt priority level (0 = none, 1..7 = IPL1..IPL7).
    void set_ipl(uint8_t level) { pending_ipl_ = level & 0x7u; }
    uint8_t ipl() const { return pending_ipl_; }

    /// Updates Status Register while keeping USP/SSP synchronized on S-bit transitions.
    void set_sr(uint16_t new_sr);
    uint16_t sr() const { return sr_; }
    bool is_supervisor() const { return (sr_ & FLAG_S) != 0; }

    // Register accessors for testing and debugging.
    uint32_t d(unsigned i) const { return d_[i]; }
    void set_d(unsigned i, uint32_t val) { d_[i] = val; }

    uint32_t a(unsigned i) const { return a_[i]; }
    void set_a(unsigned i, uint32_t val) { a_[i] = val; }

    uint32_t pc() const { return pc_; }
    void set_pc(uint32_t val) { pc_ = val; }

    uint32_t usp() const { return is_supervisor() ? usp_ : a_[7]; }
    uint32_t ssp() const { return is_supervisor() ? a_[7] : ssp_; }
    void set_usp(uint32_t val);
    void set_ssp(uint32_t val);

    uint64_t total_cycles() const { return total_cycles_; }
    bool halted() const { return halted_; }

private:
    struct ResolvedEA {
        enum class Kind : uint8_t { DataReg, AddrReg, Memory, Immediate };
        Kind kind = Kind::Memory;
        uint8_t reg = 0;
        uint32_t addr = 0;
        uint32_t imm = 0;
        uint32_t cycles = 0;
    };

    uint16_t fetch16();
    uint32_t fetch32();

    ResolvedEA resolve_ea(uint8_t mode, uint8_t reg, Size size);
    ResolvedEA resolve_indexed_ea(uint32_t base_addr, bool base_is_pc);
    uint32_t read_ea(const ResolvedEA& ea, Size size);
    void write_ea(const ResolvedEA& ea, Size size, uint32_t val);
    uint32_t read_modify_write_ea(uint8_t mode, uint8_t reg, Size size, ResolvedEA& out_ea);

    void push16(uint16_t val);
    void push32(uint32_t val);
    uint16_t pop16();
    uint32_t pop32();

    bool test_condition(uint8_t cc) const;
    void set_ccr(uint8_t ccr);
    void update_nz_flags(uint32_t result, Size size, bool clear_vc = true);
    uint32_t add_with_flags(uint32_t dst, uint32_t src, Size size, bool with_x, bool set_x);
    uint32_t sub_with_flags(uint32_t dst, uint32_t src, Size size, bool with_x, bool set_x);

    uint32_t execute_opcode(uint16_t op);
    uint32_t exec_group0(uint16_t op);
    uint32_t exec_move(uint16_t op, Size size);
    uint32_t exec_group4(uint16_t op);
    uint32_t exec_group5(uint16_t op);
    uint32_t exec_group6(uint16_t op);
    uint32_t exec_group8(uint16_t op);
    uint32_t exec_group9_or_d(uint16_t op, bool is_add);
    uint32_t exec_group_b(uint16_t op);
    uint32_t exec_group_c(uint16_t op);
    uint32_t exec_group_e(uint16_t op);
    uint32_t exec_bitfield(uint16_t op);
    uint32_t exec_mmu(uint16_t op);
    uint32_t exec_fpu(uint16_t op);

    uint32_t exec_bit_op(uint8_t bit_op, uint8_t bit_num, uint8_t ea_mode, uint8_t ea_reg, uint32_t extra_cycles);
    uint32_t exec_shift(uint8_t type, bool left, uint32_t val, uint32_t count, Size size);
    uint32_t exec_bcd_add(uint8_t dst, uint8_t src);
    uint32_t exec_bcd_sub(uint8_t dst, uint8_t src);

    void invalidate_icache();
    void invalidate_icache_entry(uint32_t addr);
    uint16_t icache_read16(uint32_t addr);
    void invalidate_dcache();
    void invalidate_dcache_entry(uint32_t addr);
    uint32_t translate_mmu_addr(uint32_t logical_addr, bool is_write, bool& out_cache_inhibit, uint16_t* out_mmusr = nullptr, uint32_t* out_desc_addr = nullptr) const;
    uint32_t dcache_read(uint32_t addr, Size s);
    void dcache_write(uint32_t addr, Size s, uint32_t val);

    struct ICacheEntry {
        uint32_t tag = 0;
        uint32_t data = 0;
        bool valid = false;
    };
    static constexpr size_t ICACHE_ENTRIES = 64;  // 64 x 4 bytes = 256 bytes (68020/68EC020/68030)

    Bus& bus_;
    CpuModel cpu_model_ = CpuModel::M68000;
    std::array<uint32_t, 8> d_{};
    std::array<uint32_t, 8> a_{};
    uint32_t usp_ = 0;
    uint32_t ssp_ = 0;
    uint32_t vbr_ = 0;
    uint32_t cacr_ = 0;
    uint32_t caar_ = 0;
    uint32_t sfc_ = 0;
    uint32_t dfc_ = 0;
    // 68030 PMMU registers
    uint32_t mmu_tc_ = 0;
    uint64_t mmu_crp_ = 0;
    uint64_t mmu_srp_ = 0;
    uint32_t mmu_tt0_ = 0;
    uint32_t mmu_tt1_ = 0;
    uint16_t mmu_sr_ = 0;
    std::array<ICacheEntry, ICACHE_ENTRIES> icache_{};
    std::array<ICacheEntry, ICACHE_ENTRIES> dcache_{};
    uint32_t icache_hits_in_step_ = 0;
    uint32_t dcache_hits_in_step_ = 0;
    std::array<double, 8> fp_{};
    uint32_t fpcr_ = 0;
    uint32_t fpsr_ = 0;
    uint32_t fpiar_ = 0;
    bool fpu_initialized_ = false;
    uint32_t pc_ = 0;
    uint32_t instr_pc_ = 0;
    uint16_t sr_ = 0x2700;
    uint64_t total_cycles_ = 0;
    uint8_t pending_ipl_ = 0;
    bool halted_ = false;
    bool exception_taken_ = false;
};

}  // namespace amiga
