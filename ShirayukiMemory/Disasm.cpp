#include "ShirayukiMemory.hpp"
#include <iomanip>
#include <sstream>
#include <string>

namespace Shirayuki {
namespace Disasm {

// --- ARM64 encoding constants (Item #3) ---
// Each pair is (mask, match) — an instruction I encodes as (I & mask) == match.
// Layout follows the ARM Architecture Reference Manual.
namespace arm64 {
static constexpr uint32_t kNopOpcode = 0xD503201Fu;

// RET Xn   (encoded as BR variant; low 5 bits = Rn, upper bits fixed)
static constexpr uint32_t kRetMask = 0xFFFFFC1Fu;
static constexpr uint32_t kRetMatch = 0xD65F0000u;

// Unconditional branch immediate: B (op-code bits 31..26 == 000101)
static constexpr uint32_t kBranchImmMask = 0xFC000000u;
static constexpr uint32_t kBUncondMatch = 0x14000000u;
static constexpr uint32_t kBlMatch = 0x94000000u;
static constexpr uint32_t kBrImm26Mask = 0x03FFFFFFu;
static constexpr uint32_t kBrImm26SignBit = 0x02000000u;
static constexpr uint32_t kBrImm26SignExt = 0xFC000000u;

// B.cond: bits 31..24 == 01010100, bit 4 == 0
static constexpr uint32_t kBCondMask = 0xFF000010u;
static constexpr uint32_t kBCondMatch = 0x54000000u;
static constexpr uint32_t kBCondImm19SignBit = 0x40000u;
static constexpr uint32_t kBCondImm19SignExt = 0xFFF80000u;

// MOVZ: bits 30..23 == 10100101, bit 31 = sf
static constexpr uint32_t kMovzMask = 0x7F800000u;
static constexpr uint32_t kMovzMatch = 0x52800000u;

// STP/LDP (signed offset, 32/64-bit register)
static constexpr uint32_t kLdstPairMask = 0x7FC00000u;
static constexpr uint32_t kStpMatch = 0x29000000u;
static constexpr uint32_t kLdpMatch = 0x29400000u;

static constexpr uint32_t kReg5Mask = 0x1Fu;
} // namespace arm64

static std::string decodeARM64(uint32_t op, uintptr_t pc) {
    using namespace arm64;

    if (op == kNopOpcode)
        return "nop";

    if ((op & kRetMask) == kRetMatch) {
        int rn = (op >> 5) & kReg5Mask;
        if (rn == 30)
            return "ret";
        return "ret x" + std::to_string(rn);
    }

    if ((op & kBranchImmMask) == kBUncondMatch) {
        int32_t imm = (op & kBrImm26Mask);
        if (imm & kBrImm26SignBit)
            imm |= kBrImm26SignExt;
        uintptr_t target = pc + (imm << 2);
        std::ostringstream ss;
        ss << "b 0x" << std::hex << target;
        return ss.str();
    }

    if ((op & kBranchImmMask) == kBlMatch) {
        int32_t imm = (op & kBrImm26Mask);
        if (imm & kBrImm26SignBit)
            imm |= kBrImm26SignExt;
        uintptr_t target = pc + (imm << 2);
        std::ostringstream ss;
        ss << "bl 0x" << std::hex << target;
        return ss.str();
    }

    if ((op & kBCondMask) == kBCondMatch) {
        static const char *conds[] = {"eq", "ne", "cs", "cc", "mi", "pl", "vs", "vc",
                                      "hi", "ls", "ge", "lt", "gt", "le", "al", "nv"};
        int cond = op & 0xF;
        int32_t imm = ((op >> 5) & 0x7FFFF);
        if (imm & kBCondImm19SignBit)
            imm |= kBCondImm19SignExt;
        uintptr_t target = pc + (imm << 2);
        std::ostringstream ss;
        ss << "b." << conds[cond] << " 0x" << std::hex << target;
        return ss.str();
    }

    if ((op & kMovzMask) == kMovzMatch) {
        int sf = (op >> 31) & 1;
        int rd = op & kReg5Mask;
        int hw = (op >> 21) & 0x3;
        uint16_t imm16 = (op >> 5) & 0xFFFF;
        uint64_t val = (uint64_t)imm16 << (hw * 16);
        std::ostringstream ss;
        ss << "mov " << (sf ? "x" : "w") << rd << ", #" << val;
        return ss.str();
    }

    if ((op & kLdstPairMask) == kStpMatch || (op & kLdstPairMask) == kLdpMatch) {
        bool isLoad = (op >> 22) & 1;
        int rt = op & kReg5Mask;
        int rt2 = (op >> 10) & kReg5Mask;
        int rn = (op >> 5) & kReg5Mask;
        int imm7 = (op >> 15) & 0x7F;
        if (imm7 & 0x40)
            imm7 |= 0xFFFFFF80;
        int sf = (op >> 31) & 1;
        std::ostringstream ss;
        ss << (isLoad ? "ldp " : "stp ");
        ss << (sf ? "x" : "w") << rt << ", " << (sf ? "x" : "w") << rt2;
        ss << ", [x" << rn;
        if (imm7)
            ss << ", #" << (imm7 * (sf ? 8 : 4));
        ss << "]";
        return ss.str();
    }

    std::ostringstream ss;
    ss << ".word 0x" << std::hex << std::setfill('0') << std::setw(8) << op;
    return ss.str();
}

std::vector<Instruction> disassemble(uintptr_t address, size_t count) {
    std::vector<Instruction> insns;
    insns.reserve(count);

    for (size_t i = 0; i < count; i++) {
        uintptr_t pc = address + i * 4;
        uint32_t opcode = 0;
        if (Memory::read(pc, &opcode, 4) != Status::Success)
            break;

        Instruction insn;
        insn.address = pc;
        insn.opcode = opcode;

        std::string decoded = decodeARM64(opcode, pc);
        size_t spacePos = decoded.find(' ');
        if (spacePos != std::string::npos) {
            insn.mnemonic = decoded.substr(0, spacePos);
            insn.operands = decoded.substr(spacePos + 1);
        } else {
            insn.mnemonic = decoded;
        }

        insns.push_back(insn);
    }

    return insns;
}

std::string formatInstruction(const Instruction &insn) {
    std::ostringstream ss;
    ss << std::hex << std::setfill('0') << std::setw(12) << insn.address << "  ";
    ss << std::setw(8) << insn.opcode << "  ";
    ss << insn.mnemonic;
    if (!insn.operands.empty())
        ss << " " << insn.operands;
    return ss.str();
}

} // namespace Disasm
} // namespace Shirayuki
