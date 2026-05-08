#include "ShirayukiMemory.hpp"
#include <mach/mach_vm.h>
#include <mach/vm_map.h>
#include <libkern/OSCacheControl.h>
#include <dlfcn.h>
#include <cstring>
#include <sstream>
#include <iomanip>

namespace Shirayuki {

// =============================================================================
// Memory
// =============================================================================

Status Memory::read(uintptr_t address, void *buffer, size_t len) {
    if (!address) return Status::InvalidAddress;
    if (!buffer) return Status::InvalidBuffer;
    if (!len) return Status::InvalidLength;

    vm_size_t outSize = 0;
    kern_return_t kr = mach_vm_read_overwrite(
        mach_task_self(),
        (mach_vm_address_t)address,
        (mach_vm_size_t)len,
        (mach_vm_address_t)buffer,
        &outSize
    );

    return (kr == KERN_SUCCESS) ? Status::Success : Status::Failed;
}

Status Memory::write(uintptr_t address, const void *buffer, size_t len) {
    if (!address) return Status::InvalidAddress;
    if (!buffer) return Status::InvalidBuffer;
    if (!len) return Status::InvalidLength;

    // Try direct write first
    kern_return_t kr = mach_vm_write(
        mach_task_self(),
        (mach_vm_address_t)address,
        (vm_offset_t)buffer,
        (mach_msg_type_number_t)len
    );

    if (kr == KERN_SUCCESS) {
        // Flush instruction cache for code patches
        sys_icache_invalidate((void *)address, len);
        return Status::Success;
    }

    // If write failed, try changing protection first
    mach_vm_address_t pageStart = address & ~(vm_page_size - 1);
    mach_vm_size_t pageLen = (address + len - pageStart + vm_page_size - 1) & ~(vm_page_size - 1);

    kr = mach_vm_protect(
        mach_task_self(),
        pageStart,
        pageLen,
        false,
        VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY
    );

    if (kr != KERN_SUCCESS) return Status::ProtectionFailed;

    kr = mach_vm_write(
        mach_task_self(),
        (mach_vm_address_t)address,
        (vm_offset_t)buffer,
        (mach_msg_type_number_t)len
    );

    // Restore rx protection for code
    mach_vm_protect(
        mach_task_self(),
        pageStart,
        pageLen,
        false,
        VM_PROT_READ | VM_PROT_EXECUTE
    );

    if (kr == KERN_SUCCESS) {
        sys_icache_invalidate((void *)address, len);
        return Status::Success;
    }

    return Status::Failed;
}

RegionInfo Memory::getRegionInfo(uintptr_t address) {
    RegionInfo info{};
    mach_vm_address_t addr = (mach_vm_address_t)address;
    mach_vm_size_t size = 0;
    uint32_t depth = 0;
    vm_region_submap_short_info_data_64_t regionInfo;
    mach_msg_type_number_t count = VM_REGION_SUBMAP_SHORT_INFO_COUNT_64;

    kern_return_t kr = mach_vm_region_recurse(
        mach_task_self(),
        &addr, &size, &depth,
        (vm_region_recurse_info_t)&regionInfo, &count
    );

    if (kr == KERN_SUCCESS) {
        info.start = (uintptr_t)addr;
        info.size = (size_t)size;
        info.protection = regionInfo.protection;
    }

    return info;
}

Status Memory::protect(uintptr_t address, size_t len, vm_prot_t prot) {
    kern_return_t kr = mach_vm_protect(
        mach_task_self(),
        (mach_vm_address_t)address,
        (mach_vm_size_t)len,
        false,
        prot
    );
    return (kr == KERN_SUCCESS) ? Status::Success : Status::ProtectionFailed;
}

std::vector<RegionInfo> Memory::listRegions(vm_prot_t requiredProt) {
    std::vector<RegionInfo> regions;
    mach_vm_address_t addr = 0;
    mach_vm_size_t size = 0;

    while (true) {
        uint32_t depth = 0;
        vm_region_submap_short_info_data_64_t info;
        mach_msg_type_number_t count = VM_REGION_SUBMAP_SHORT_INFO_COUNT_64;

        kern_return_t kr = mach_vm_region_recurse(
            mach_task_self(),
            &addr, &size, &depth,
            (vm_region_recurse_info_t)&info, &count
        );

        if (kr != KERN_SUCCESS) break;

        if (requiredProt == VM_PROT_NONE || (info.protection & requiredProt) == requiredProt) {
            regions.push_back({(uintptr_t)addr, (size_t)size, info.protection});
        }

        addr += size;
    }

    return regions;
}

// =============================================================================
// Image
// =============================================================================

ImageInfo Image::find(const std::string &imageName) {
    uint32_t count = _dyld_image_count();
    for (uint32_t i = 0; i < count; i++) {
        const char *name = _dyld_get_image_name(i);
        if (!name) continue;

        std::string path(name);
        // Match by filename or full path
        if (path == imageName ||
            path.find(imageName) != std::string::npos) {
            ImageInfo info;
            info.name = path;
            info.base = (uintptr_t)_dyld_get_image_header(i);
            info.slide = _dyld_get_image_vmaddr_slide(i);
            return info;
        }
    }
    return {};
}

ImageInfo Image::getBase() {
    ImageInfo info;
    info.name = _dyld_get_image_name(0);
    info.base = (uintptr_t)_dyld_get_image_header(0);
    info.slide = _dyld_get_image_vmaddr_slide(0);
    return info;
}

std::vector<ImageInfo> Image::listAll() {
    std::vector<ImageInfo> images;
    uint32_t count = _dyld_image_count();
    for (uint32_t i = 0; i < count; i++) {
        ImageInfo info;
        info.name = _dyld_get_image_name(i) ?: "";
        info.base = (uintptr_t)_dyld_get_image_header(i);
        info.slide = _dyld_get_image_vmaddr_slide(i);
        images.push_back(info);
    }
    return images;
}

uintptr_t Image::absoluteAddress(const ImageInfo &img, uintptr_t offset) {
    if (!img.isValid()) return 0;
    return img.base + offset;
}

uintptr_t Image::absoluteAddress(const std::string &imageName, uintptr_t offset) {
    return absoluteAddress(find(imageName), offset);
}

// =============================================================================
// Scanner
// =============================================================================

// Parse IDA-style pattern into bytes + mask
static bool parseIdaPattern(const std::string &pattern,
                            std::vector<uint8_t> &bytes,
                            std::vector<bool> &mask) {
    bytes.clear();
    mask.clear();

    std::istringstream ss(pattern);
    std::string token;

    while (ss >> token) {
        if (token == "?" || token == "??") {
            bytes.push_back(0);
            mask.push_back(false);
        } else {
            unsigned int val;
            std::istringstream hexSS(token);
            if (!(hexSS >> std::hex >> val) || val > 0xFF) return false;
            bytes.push_back(static_cast<uint8_t>(val));
            mask.push_back(true);
        }
    }

    return !bytes.empty();
}

std::vector<uintptr_t> Scanner::findPattern(uintptr_t start, size_t len,
                                            const std::string &pattern) {
    std::vector<uintptr_t> results;
    std::vector<uint8_t> patBytes;
    std::vector<bool> patMask;

    if (!parseIdaPattern(pattern, patBytes, patMask)) return results;

    const uint8_t *buf = reinterpret_cast<const uint8_t *>(start);
    size_t patLen = patBytes.size();

    if (len < patLen) return results;

    for (size_t i = 0; i <= len - patLen; i++) {
        bool found = true;
        for (size_t j = 0; j < patLen; j++) {
            if (patMask[j] && buf[i + j] != patBytes[j]) {
                found = false;
                break;
            }
        }
        if (found) {
            results.push_back(start + i);
        }
    }

    return results;
}

uintptr_t Scanner::findPatternFirst(uintptr_t start, size_t len,
                                    const std::string &pattern) {
    std::vector<uint8_t> patBytes;
    std::vector<bool> patMask;

    if (!parseIdaPattern(pattern, patBytes, patMask)) return 0;

    const uint8_t *buf = reinterpret_cast<const uint8_t *>(start);
    size_t patLen = patBytes.size();

    if (len < patLen) return 0;

    for (size_t i = 0; i <= len - patLen; i++) {
        bool found = true;
        for (size_t j = 0; j < patLen; j++) {
            if (patMask[j] && buf[i + j] != patBytes[j]) {
                found = false;
                break;
            }
        }
        if (found) return start + i;
    }

    return 0;
}

std::vector<uintptr_t> Scanner::findPatternInImage(const ImageInfo &img,
                                                   const std::string &pattern) {
    std::vector<uintptr_t> allResults;
    if (!img.isValid()) return allResults;

    // Scan all readable regions that overlap with this image
    auto regions = Memory::listRegions(VM_PROT_READ);
    for (auto &region : regions) {
        // Simple heuristic: scan regions near the image base
        if (region.start >= img.base &&
            region.start < img.base + 0x10000000) { // 256MB max
            auto results = findPattern(region.start, region.size, pattern);
            allResults.insert(allResults.end(), results.begin(), results.end());
        }
    }

    return allResults;
}

std::vector<uintptr_t> Scanner::findString(uintptr_t start, size_t len,
                                           const std::string &str) {
    std::vector<uintptr_t> results;
    if (str.empty() || len < str.size()) return results;

    const uint8_t *buf = reinterpret_cast<const uint8_t *>(start);
    const uint8_t *needle = reinterpret_cast<const uint8_t *>(str.c_str());
    size_t needleLen = str.size();

    for (size_t i = 0; i <= len - needleLen; i++) {
        if (memcmp(buf + i, needle, needleLen) == 0) {
            results.push_back(start + i);
        }
    }

    return results;
}

// =============================================================================
// Patch
// =============================================================================

Patch Patch::createWithBytes(uintptr_t address, const void *bytes, size_t len) {
    Patch p;
    if (!address || !bytes || !len) return p;

    p.m_address = address;
    p.m_patchBytes.assign(
        reinterpret_cast<const uint8_t *>(bytes),
        reinterpret_cast<const uint8_t *>(bytes) + len
    );

    // Backup original bytes
    p.m_origBytes.resize(len);
    if (Memory::read(address, p.m_origBytes.data(), len) != Status::Success) {
        return Patch{}; // invalid
    }

    return p;
}

Patch Patch::createWithHex(uintptr_t address, const std::string &hex) {
    auto bytes = Hex::toBytes(hex);
    if (bytes.empty()) return Patch{};
    return createWithBytes(address, bytes.data(), bytes.size());
}

Patch Patch::createNop(uintptr_t address, size_t count) {
    // ARM64 NOP = 0x1F2003D5
    std::vector<uint8_t> nops(count * 4);
    for (size_t i = 0; i < count; i++) {
        nops[i * 4 + 0] = 0x1F;
        nops[i * 4 + 1] = 0x20;
        nops[i * 4 + 2] = 0x03;
        nops[i * 4 + 3] = 0xD5;
    }
    return createWithBytes(address, nops.data(), nops.size());
}

bool Patch::apply() {
    if (!isValid()) return false;
    if (m_applied) return true;

    Status st = Memory::write(m_address, m_patchBytes.data(), m_patchBytes.size());
    if (st == Status::Success) {
        m_applied = true;
        return true;
    }
    return false;
}

bool Patch::restore() {
    if (!isValid()) return false;
    if (!m_applied) return true;

    Status st = Memory::write(m_address, m_origBytes.data(), m_origBytes.size());
    if (st == Status::Success) {
        m_applied = false;
        return true;
    }
    return false;
}

bool Patch::isApplied() const {
    if (!isValid()) return false;

    std::vector<uint8_t> current(m_patchBytes.size());
    if (Memory::read(m_address, current.data(), current.size()) != Status::Success) {
        return false;
    }
    return current == m_patchBytes;
}

std::string Patch::currentHex() const {
    if (!isValid()) return "";
    std::vector<uint8_t> current(m_patchBytes.size());
    Memory::read(m_address, current.data(), current.size());
    return Hex::fromBytes(current.data(), current.size());
}

// =============================================================================
// Hex utilities
// =============================================================================

std::vector<uint8_t> Hex::toBytes(const std::string &hex) {
    std::vector<uint8_t> bytes;
    std::istringstream ss(hex);
    std::string token;

    while (ss >> token) {
        if (token == "?" || token == "??") {
            bytes.push_back(0);
            continue;
        }
        unsigned int val;
        std::istringstream hexSS(token);
        if (!(hexSS >> std::hex >> val) || val > 0xFF) {
            return {}; // invalid
        }
        bytes.push_back(static_cast<uint8_t>(val));
    }

    // Also try contiguous hex string (no spaces)
    if (bytes.empty() && hex.length() >= 2 && hex.find(' ') == std::string::npos) {
        for (size_t i = 0; i + 1 < hex.length(); i += 2) {
            unsigned int val;
            std::istringstream hexSS(hex.substr(i, 2));
            if (!(hexSS >> std::hex >> val)) return {};
            bytes.push_back(static_cast<uint8_t>(val));
        }
    }

    return bytes;
}

std::string Hex::fromBytes(const void *data, size_t len) {
    std::ostringstream ss;
    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(data);
    for (size_t i = 0; i < len; i++) {
        if (i > 0) ss << ' ';
        ss << std::uppercase << std::setfill('0') << std::setw(2) << std::hex
           << static_cast<unsigned>(bytes[i]);
    }
    return ss.str();
}

std::string Hex::dump(uintptr_t address, size_t len, size_t bytesPerLine) {
    std::ostringstream ss;
    std::vector<uint8_t> buf(len);

    if (Memory::read(address, buf.data(), len) != Status::Success) {
        return "<read failed>";
    }

    for (size_t i = 0; i < len; i += bytesPerLine) {
        ss << std::setfill('0') << std::setw(16) << std::hex << (address + i) << "  ";

        size_t lineLen = std::min(bytesPerLine, len - i);
        for (size_t j = 0; j < bytesPerLine; j++) {
            if (j < lineLen) {
                ss << std::setfill('0') << std::setw(2) << std::hex
                   << static_cast<unsigned>(buf[i + j]) << ' ';
            } else {
                ss << "   ";
            }
        }

        ss << " |";
        for (size_t j = 0; j < lineLen; j++) {
            char c = static_cast<char>(buf[i + j]);
            ss << (isprint(c) ? c : '.');
        }
        ss << "|\n";
    }

    return ss.str();
}

bool Hex::isValid(const std::string &hex) {
    for (char c : hex) {
        if (c == ' ' || c == '?') continue;
        if (!isxdigit(c)) return false;
    }
    return !hex.empty();
}

} // namespace Shirayuki
