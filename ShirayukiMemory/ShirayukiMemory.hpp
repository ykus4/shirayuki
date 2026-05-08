#ifndef SHIRAYUKI_MEMORY_HPP
#define SHIRAYUKI_MEMORY_HPP

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
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
    uintptr_t start = 0;
    size_t size = 0;
    vm_prot_t protection = VM_PROT_NONE;

    bool isReadable() const { return protection & VM_PROT_READ; }
    bool isWritable() const { return protection & VM_PROT_WRITE; }
    bool isExecutable() const { return protection & VM_PROT_EXECUTE; }
};

// --- Core memory operations ---
namespace Memory {
    // Read bytes from address
    Status read(uintptr_t address, void *buffer, size_t len);

    // Write bytes to address (handles page protection)
    Status write(uintptr_t address, const void *buffer, size_t len);

    // Typed read
    template <typename T>
    T readValue(uintptr_t address) {
        T val{};
        read(address, &val, sizeof(T));
        return val;
    }

    // Typed write
    template <typename T>
    Status writeValue(uintptr_t address, T value) {
        return write(address, &value, sizeof(T));
    }

    // Get page-aligned protection info
    RegionInfo getRegionInfo(uintptr_t address);

    // Set memory protection
    Status protect(uintptr_t address, size_t len, vm_prot_t prot);

    // List all memory regions for current task
    std::vector<RegionInfo> listRegions(vm_prot_t requiredProt = VM_PROT_NONE);
}

// --- Image / module utilities ---
namespace Image {
    // Get info for loaded binary by name
    ImageInfo find(const std::string &imageName);

    // Get base image (main executable)
    ImageInfo getBase();

    // List all loaded images
    std::vector<ImageInfo> listAll();

    // Calculate absolute address from image + offset
    uintptr_t absoluteAddress(const ImageInfo &img, uintptr_t offset);
    uintptr_t absoluteAddress(const std::string &imageName, uintptr_t offset);
}

// --- Memory scanner ---
namespace Scanner {
    // IDA-style pattern (e.g. "FF 00 ?? 01 AB")
    std::vector<uintptr_t> findPattern(uintptr_t start, size_t len,
                                       const std::string &pattern);

    // Find first match only
    uintptr_t findPatternFirst(uintptr_t start, size_t len,
                               const std::string &pattern);

    // Scan all readable regions of an image
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

    // String search (UTF-8)
    std::vector<uintptr_t> findString(uintptr_t start, size_t len,
                                      const std::string &str);
}

// --- Memory patch (apply/restore) ---
class Patch {
public:
    // Create from raw bytes
    static Patch createWithBytes(uintptr_t address, const void *bytes, size_t len);

    // Create from hex string (e.g. "90 90 90 90")
    static Patch createWithHex(uintptr_t address, const std::string &hex);

    // Create NOP sled
    static Patch createNop(uintptr_t address, size_t count);

    bool isValid() const { return m_address != 0 && !m_patchBytes.empty(); }
    uintptr_t address() const { return m_address; }
    size_t size() const { return m_patchBytes.size(); }

    // Apply patch
    bool apply();

    // Restore original bytes
    bool restore();

    // Check if currently patched
    bool isApplied() const;

    // Inspect
    std::vector<uint8_t> originalBytes() const { return m_origBytes; }
    std::vector<uint8_t> patchBytes() const { return m_patchBytes; }
    std::string currentHex() const;

private:
    uintptr_t m_address = 0;
    std::vector<uint8_t> m_origBytes;
    std::vector<uint8_t> m_patchBytes;
    bool m_applied = false;
};

// --- Hex utilities ---
namespace Hex {
    std::vector<uint8_t> toBytes(const std::string &hex);
    std::string fromBytes(const void *data, size_t len);
    std::string dump(uintptr_t address, size_t len, size_t bytesPerLine = 16);
    bool isValid(const std::string &hex);
}

} // namespace Shirayuki

#endif // SHIRAYUKI_MEMORY_HPP
