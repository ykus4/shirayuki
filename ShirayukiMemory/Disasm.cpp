#include "ShirayukiMemory.hpp"

#include <iomanip>
#include <sstream>

namespace Shirayuki {

// =============================================================================
// Disasm (simplified ARM64 decoder)
// =============================================================================

namespace Disasm {

static std::string decodeARM64(uint32_t op, uintptr_t pc) {
    // NOP
    if (op == 0xD503201F)
        return "nop";

    // RET
    if ((op & 0xFFFFFC1F) == 0xD65F0000) {
        int rn = (op >> 5) & 0x1F;
        if (rn == 30)
            return "ret";
        return "ret x" + std::to_string(rn);
    }

    // B (unconditional branch)
    if ((op & 0xFC000000) == 0x14000000) {
        int32_t imm = (op & 0x03FFFFFF);
        if (imm & 0x02000000)
            imm |= 0xFC000000; // sign extend
        uintptr_t target = pc + (imm << 2);
        std::ostringstream ss;
        ss << "b 0x" << std::hex << target;
        return ss.str();
    }

    // BL
    if ((op & 0xFC000000) == 0x94000000) {
        int32_t imm = (op & 0x03FFFFFF);
        if (imm & 0x02000000)
            imm |= 0xFC000000;
        uintptr_t target = pc + (imm << 2);
        std::ostringstream ss;
        ss << "bl 0x" << std::hex << target;
        return ss.str();
    }

    // B.cond
    if ((op & 0xFF000010) == 0x54000000) {
        static const char *conds[] = {"eq", "ne", "cs", "cc", "mi", "pl", "vs", "vc",
                                      "hi", "ls", "ge", "lt", "gt", "le", "al", "nv"};
        int cond = op & 0xF;
        int32_t imm = ((op >> 5) & 0x7FFFF);
        if (imm & 0x40000)
            imm |= 0xFFF80000;
        uintptr_t target = pc + (imm << 2);
        std::ostringstream ss;
        ss << "b." << conds[cond] << " 0x" << std::hex << target;
        return ss.str();
    }

    // MOV (wide immediate) — MOVZ
    if ((op & 0x7F800000) == 0x52800000) {
        int sf = (op >> 31) & 1;
        int rd = op & 0x1F;
        int hw = (op >> 21) & 0x3;
        uint16_t imm16 = (op >> 5) & 0xFFFF;
        uint64_t val = (uint64_t)imm16 << (hw * 16);
        std::ostringstream ss;
        ss << "mov " << (sf ? "x" : "w") << rd << ", #" << val;
        return ss.str();
    }

    // STP/LDP (common)
    if ((op & 0x7FC00000) == 0x29000000 || (op & 0x7FC00000) == 0x29400000) {
        bool isLoad = (op >> 22) & 1;
        int rt = op & 0x1F;
        int rt2 = (op >> 10) & 0x1F;
        int rn = (op >> 5) & 0x1F;
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

    // Fallback
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
