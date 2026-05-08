#ifndef SHIRAYUKI_MEMORY_HPP
#define SHIRAYUKI_MEMORY_HPP

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <deque>
#include <functional>
#include <mach/mach.h>
#include <mach-o/dyld.h>
#include <mach-o/getsect.h>
#include <sys/mman.h>

namespace Shirayuki {

// --- Status codes ---
enum class Status {
    Success = 0,
    Failed,
    InvalidAddress,
    InvalidLength,
    InvalidBuffer,
    ProtectionFailed,
    TaskFailed
};

// --- Image info (loaded binary) ---
struct ImageInfo {
    std::string name;
    uintptr_t base = 0;
    intptr_t slide = 0;

    bool isValid() const { return base != 0; }
};

// --- Memory region info ---
struct RegionInfo {
    std::string label;  // e.g. "__DATA", "MALLOC", "STACK"
    uintptr_t start = 0;
    size_t size = 0;
    vm_prot_t protection = VM_PROT_NONE;

    bool isReadable() const { return protection & VM_PROT_READ; }
    bool isWritable() const { return protection & VM_PROT_WRITE; }
    bool isExecutable() const { return protection & VM_PROT_EXECUTE; }
};

// --- Region filter for scans ---
enum class RegionFilter {
    All = 0,
    HeapOnly,       // rw-, anonymous
    DataOnly,       // __DATA segments
    StackOnly,      // stack regions
    ReadWrite,      // any rw-
    Executable,     // r-x
};

// --- Core memory operations ---
namespace Memory {
    Status read(uintptr_t address, void *buffer, size_t len);
    Status write(uintptr_t address, const void *buffer, size_t len);

    template <typename T>
    T readValue(uintptr_t address) {
        T val{};
        read(address, &val, sizeof(T));
        return val;
    }

    template <typename T>
    Status writeValue(uintptr_t address, T value) {
        return write(address, &value, sizeof(T));
    }

    RegionInfo getRegionInfo(uintptr_t address);
    Status protect(uintptr_t address, size_t len, vm_prot_t prot);
    std::vector<RegionInfo> listRegions(vm_prot_t requiredProt = VM_PROT_NONE);
    std::vector<RegionInfo> listRegionsFiltered(RegionFilter filter);
}

// --- Image / module utilities ---
namespace Image {
    ImageInfo find(const std::string &imageName);
    ImageInfo getBase();
    std::vector<ImageInfo> listAll();
    uintptr_t absoluteAddress(const ImageInfo &img, uintptr_t offset);
    uintptr_t absoluteAddress(const std::string &imageName, uintptr_t offset);

    // Symbol resolution
    uintptr_t findSymbol(const std::string &imageName, const std::string &symbolName);
    uintptr_t findSymbol(const ImageInfo &img, const std::string &symbolName);
}

// --- Search value type ---
enum class ValueType {
    Int8, UInt8,
    Int16, UInt16,
    Int32, UInt32,
    Int64, UInt64,
    Float32, Float64
};

size_t valueTypeSize(ValueType type);
std::string valueTypeLabel(ValueType type);

// --- Scanner compare mode (for narrowing) ---
enum class CompareMode {
    Exact,
    Changed,
    Unchanged,
    Increased,
    Decreased,
    GreaterThan,
    LessThan,
};

// --- Memory scanner ---
namespace Scanner {
    // IDA-style pattern (e.g. "FF 00 ?? 01 AB")
    std::vector<uintptr_t> findPattern(uintptr_t start, size_t len,
                                       const std::string &pattern);
    uintptr_t findPatternFirst(uintptr_t start, size_t len,
                               const std::string &pattern);
    std::vector<uintptr_t> findPatternInImage(const ImageInfo &img,
                                              const std::string &pattern);

    // Typed value search
    template <typename T>
    std::vector<uintptr_t> findValue(uintptr_t start, size_t len, T value) {
        std::vector<uintptr_t> results;
        const uint8_t *buf = reinterpret_cast<const uint8_t *>(start);
        for (size_t i = 0; i + sizeof(T) <= len; i += sizeof(T)) {
            if (*reinterpret_cast<const T *>(buf + i) == value) {
                results.push_back(start + i);
            }
        }
        return results;
    }

    // String search
    std::vector<uintptr_t> findString(uintptr_t start, size_t len,
                                      const std::string &str);

    // Narrowing: filter candidates by comparing current vs snapshot
    struct Candidate {
        uintptr_t address;
        std::vector<uint8_t> snapshotValue; // value at time of initial scan
    };

    std::vector<Candidate> narrowResults(const std::vector<Candidate> &candidates,
                                         ValueType type, CompareMode mode,
                                         const void *compareValue = nullptr);
}

// --- Memory patch (apply/restore) ---
class Patch {
public:
    static Patch createWithBytes(uintptr_t address, const void *bytes, size_t len);
    static Patch createWithHex(uintptr_t address, const std::string &hex);
    static Patch createNop(uintptr_t address, size_t count);

    bool isValid() const { return m_address != 0 && !m_patchBytes.empty(); }
    uintptr_t address() const { return m_address; }
    size_t size() const { return m_patchBytes.size(); }
    std::string label() const { return m_label; }
    void setLabel(const std::string &l) { m_label = l; }

    bool apply();
    bool restore();
    bool isApplied() const;

    std::vector<uint8_t> originalBytes() const { return m_origBytes; }
    std::vector<uint8_t> patchBytes() const { return m_patchBytes; }
    std::string currentHex() const;
    std::string originalHex() const;
    std::string patchHex() const;

private:
    uintptr_t m_address = 0;
    std::vector<uint8_t> m_origBytes;
    std::vector<uint8_t> m_patchBytes;
    std::string m_label;
    bool m_applied = false;
};

// --- Hex utilities ---
namespace Hex {
    std::vector<uint8_t> toBytes(const std::string &hex);
    std::string fromBytes(const void *data, size_t len);
    std::string fromBytes(const std::vector<uint8_t> &data);
    std::string dump(uintptr_t address, size_t len, size_t bytesPerLine = 16);
    bool isValid(const std::string &hex);
}

// --- Disassembly (ARM64) ---
namespace Disasm {
    struct Instruction {
        uintptr_t address;
        uint32_t opcode;
        std::string mnemonic;  // simplified
        std::string operands;
    };

    // Disassemble ARM64 instructions at address
    std::vector<Instruction> disassemble(uintptr_t address, size_t count);
    std::string formatInstruction(const Instruction &insn);
}

} // namespace Shirayuki

#endif // SHIRAYUKI_MEMORY_HPP
