#ifndef SHIRAYUKI_MEMORY_HPP
#define SHIRAYUKI_MEMORY_HPP

#include "ShirayukiConfig.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <mach-o/dyld.h>
#include <mach-o/getsect.h>
#include <mach/mach.h>
#include <optional>
#include <regex>
#include <string>
#include <sys/mman.h>
#include <vector>

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

    bool isValid() const {
        return base != 0;
    }
};

// --- Memory region info ---
struct RegionInfo {
    std::string label; // human-readable kind: "MALLOC", "STACK", "__TEXT", ...
    uintptr_t start = 0;
    size_t size = 0;
    vm_prot_t protection = VM_PROT_NONE;
    // Kernel VM_MEMORY_* allocator tag. This, not the address range, is what
    // reliably distinguishes heap from stack: on arm64 the entire user address
    // space sits above 4 GB, so address-threshold heuristics classify nothing.
    unsigned int userTag = 0;

    bool isReadable() const {
        return protection & VM_PROT_READ;
    }
    bool isWritable() const {
        return protection & VM_PROT_WRITE;
    }
    bool isExecutable() const {
        return protection & VM_PROT_EXECUTE;
    }
    bool isHeap() const;
    bool isStack() const;
};

// --- Region filter for scans ---
enum class RegionFilter {
    All = 0,
    HeapOnly,   // rw-, anonymous
    DataOnly,   // __DATA segments
    StackOnly,  // stack regions
    ReadWrite,  // any rw-
    Executable, // r-x
};

// --- Core memory operations ---
namespace Memory {
Status read(uintptr_t address, void *buffer, size_t len);
Status write(uintptr_t address, const void *buffer, size_t len);

// Read a typed value, reporting failure. An unreadable address is otherwise
// indistinguishable from one that legitimately holds 0 — and a caller that
// displays that 0 in an editable field will happily write it back, turning a
// failed read into real memory corruption.
template <typename T> Status readValue(uintptr_t address, T &out) {
    return read(address, &out, sizeof(T));
}

// Convenience form for callers that have already validated the address, or for
// which a zero on failure is genuinely harmless. Prefer the two-argument form.
template <typename T> std::optional<T> tryReadValue(uintptr_t address) {
    T val{};
    if (read(address, &val, sizeof(T)) != Status::Success)
        return std::nullopt;
    return val;
}

template <typename T> Status writeValue(uintptr_t address, T value) {
    return write(address, &value, sizeof(T));
}

RegionInfo getRegionInfo(uintptr_t address);
Status protect(uintptr_t address, size_t len, vm_prot_t prot);
std::vector<RegionInfo> listRegions(vm_prot_t requiredProt = VM_PROT_NONE);
std::vector<RegionInfo> listRegionsFiltered(RegionFilter filter);
} // namespace Memory

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
} // namespace Image

// --- Search value type ---
// Enumerator order is load-bearing: ValueType.cpp indexes its descriptor table
// by static_cast<size_t>(type) and static_asserts the count.
enum class ValueType { Int8, UInt8, Int16, UInt16, Int32, UInt32, Int64, UInt64, Float32, Float64 };

size_t valueTypeSize(ValueType type);

// Compact UI label ("i32", "f64"). For a stable identifier use ValueFormat::toTag.
std::string valueTypeLabel(ValueType type);

// Typed three-way compare of two same-type raw values: -1, 0 or 1.
// Comparing the bytes directly instead (memcmp) is wrong on little-endian for
// every multi-byte type, and for all signed and floating point values.
int compareValues(const uint8_t *a, const uint8_t *b, ValueType type);

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
std::vector<uintptr_t> findPattern(uintptr_t start, size_t len, const std::string &pattern);
uintptr_t findPatternFirst(uintptr_t start, size_t len, const std::string &pattern);
std::vector<uintptr_t> findPatternInImage(const ImageInfo &img, const std::string &pattern);

// Find every occurrence of `width` needle bytes in [start, start+len).
//
// `stride` is the step between candidate offsets; it defaults to `width`, i.e.
// only naturally-aligned values are reported, which is what a typed scan wants.
// Pass 1 for a byte-granular search.
//
// Reads a copy of the range through Memory::read rather than dereferencing it.
// Region lists come from a vm_region snapshot, so by the time a scan runs the
// host app may already have freed or unmapped the range — a direct load would
// fault the whole process, where a failed read merely skips that chunk.
std::vector<uintptr_t> findValueBytes(uintptr_t start, size_t len, const uint8_t *needle,
                                      size_t width, size_t stride = 0);

// Typed convenience wrapper over findValueBytes.
template <typename T> std::vector<uintptr_t> findValue(uintptr_t start, size_t len, T value) {
    uint8_t needle[sizeof(T)];
    memcpy(needle, &value, sizeof(T));
    return findValueBytes(start, len, needle, sizeof(T));
}

// String search
std::vector<uintptr_t> findString(uintptr_t start, size_t len, const std::string &str);

// Regex search (matches against null-terminated strings in the region)
std::vector<uintptr_t> findRegex(uintptr_t start, size_t len, const std::string &pattern);

// Narrowing: filter candidates by comparing current vs snapshot
struct Candidate {
    uintptr_t address;
    std::vector<uint8_t> snapshotValue; // value at time of initial scan
};

std::vector<Candidate> narrowResults(const std::vector<Candidate> &candidates, ValueType type,
                                     CompareMode mode, const void *compareValue = nullptr);
} // namespace Scanner

// --- Memory patch (apply/restore) ---
class Patch {
  public:
    static Patch createWithBytes(uintptr_t address, const void *bytes, size_t len);
    static Patch createWithHex(uintptr_t address, const std::string &hex);
    static Patch createNop(uintptr_t address, size_t count);

    bool isValid() const {
        return m_address != 0 && !m_patchBytes.empty();
    }
    uintptr_t address() const {
        return m_address;
    }
    size_t size() const {
        return m_patchBytes.size();
    }
    std::string label() const {
        return m_label;
    }
    void setLabel(const std::string &l) {
        m_label = l;
    }

    bool apply();
    bool restore();
    bool isApplied() const;

    std::vector<uint8_t> originalBytes() const {
        return m_origBytes;
    }
    std::vector<uint8_t> patchBytes() const {
        return m_patchBytes;
    }
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
} // namespace Hex

// --- Disassembly (ARM64) ---
namespace Disasm {
struct Instruction {
    uintptr_t address;
    uint32_t opcode;
    std::string mnemonic; // simplified
    std::string operands;
};

// Disassemble ARM64 instructions at address
std::vector<Instruction> disassemble(uintptr_t address, size_t count);
std::string formatInstruction(const Instruction &insn);
} // namespace Disasm

// --- Value formatting and parsing ---
namespace ValueFormat {
// Format raw bytes as a plain value string, with no decoration. Guaranteed
// round-trippable: parse(format(buf, t), t, buf2) reproduces buf exactly.
// Use this for edit fields, session files and JSON export.
std::string format(const uint8_t *buf, ValueType type);

// Format raw bytes for display in a table cell: integers gain a hex annotation
// ("42 (0x2A)"), floats use fixed notation. Not parseable — display only.
std::string formatDisplay(const uint8_t *buf, ValueType type);

// Parse a decimal or 0x-prefixed hexadecimal string into raw bytes for the
// given type. Returns the number of bytes written, or 0 if the input is empty,
// malformed, has trailing characters, or is out of range for the type; `buf` is
// zeroed either way. Never throws — callers pass raw text field contents.
size_t parse(const std::string &input, ValueType type, uint8_t buf[kMaxValueSize]);

// Resolve a type tag. Both the canonical tag ("int32", "float") and the compact
// label ("i32", "f32") are accepted, as are the "float32"/"float64" aliases.
// fromTag falls back to Int32 for unknown tags; use tryFromTag when an
// unrecognised tag should be reported to the user instead of silently coerced.
bool tryFromTag(const std::string &tag, ValueType &out);
ValueType fromTag(const std::string &tag);

// Canonical, stable string tag for a type ("int32"). Round-trips through
// fromTag. For the compact UI label use valueTypeLabel.
std::string toTag(ValueType type);

// Canonical tags of every ValueType, in enumerator order.
std::vector<std::string> allTags();
} // namespace ValueFormat

} // namespace Shirayuki

#endif // SHIRAYUKI_MEMORY_HPP
