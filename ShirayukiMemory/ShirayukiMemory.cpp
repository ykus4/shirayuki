#include "ShirayukiMemory.hpp"
#include <mach/mach_vm.h>
#include <mach/vm_map.h>
#include <libkern/OSCacheControl.h>
#include <dlfcn.h>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <algorithm>

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
        sys_icache_invalidate((void *)address, len);
        return Status::Success;
    }

    // Save original protection before modifying
    mach_vm_address_t pageStart = address & ~(vm_page_size - 1);
    mach_vm_size_t pageLen = (address + len - pageStart + vm_page_size - 1) & ~(vm_page_size - 1);

    // Query current protection
    mach_vm_address_t regionAddr = pageStart;
    mach_vm_size_t regionSize = 0;
    uint32_t depth = 0;
    vm_region_submap_short_info_data_64_t info;
    mach_msg_type_number_t count = VM_REGION_SUBMAP_SHORT_INFO_COUNT_64;
    vm_prot_t origProt = VM_PROT_READ | VM_PROT_EXECUTE; // fallback

    kern_return_t infoKr = mach_vm_region_recurse(
        mach_task_self(), &regionAddr, &regionSize, &depth,
        (vm_region_recurse_info_t)&info, &count
    );
    if (infoKr == KERN_SUCCESS) {
        origProt = info.protection;
    }

    // Set writable
    kr = mach_vm_protect(
        mach_task_self(), pageStart, pageLen, false,
        VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY
    );
    if (kr != KERN_SUCCESS) return Status::ProtectionFailed;

    kr = mach_vm_write(
        mach_task_self(),
        (mach_vm_address_t)address,
        (vm_offset_t)buffer,
        (mach_msg_type_number_t)len
    );

    // Restore original protection
    mach_vm_protect(mach_task_self(), pageStart, pageLen, false, origProt);

    if (kr == KERN_SUCCESS) {
        sys_icache_invalidate((void *)address, len);
        return Status::Success;
    }

    return Status::Failed;
}

RegionInfo Memory::getRegionInfo(uintptr_t address) {
    RegionInfo ri{};
    mach_vm_address_t addr = (mach_vm_address_t)address;
    mach_vm_size_t size = 0;
    uint32_t depth = 0;
    vm_region_submap_short_info_data_64_t info;
    mach_msg_type_number_t count = VM_REGION_SUBMAP_SHORT_INFO_COUNT_64;

    kern_return_t kr = mach_vm_region_recurse(
        mach_task_self(), &addr, &size, &depth,
        (vm_region_recurse_info_t)&info, &count
    );

    if (kr == KERN_SUCCESS) {
        ri.start = (uintptr_t)addr;
        ri.size = (size_t)size;
        ri.protection = info.protection;
    }

    return ri;
}

Status Memory::protect(uintptr_t address, size_t len, vm_prot_t prot) {
    kern_return_t kr = mach_vm_protect(
        mach_task_self(), (mach_vm_address_t)address,
        (mach_vm_size_t)len, false, prot
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
            mach_task_self(), &addr, &size, &depth,
            (vm_region_recurse_info_t)&info, &count
        );
        if (kr != KERN_SUCCESS) break;

        if (requiredProt == VM_PROT_NONE || (info.protection & requiredProt) == requiredProt) {
            RegionInfo ri;
            ri.start = (uintptr_t)addr;
            ri.size = (size_t)size;
            ri.protection = info.protection;
            regions.push_back(ri);
        }

        addr += size;
    }

    return regions;
}

std::vector<RegionInfo> Memory::listRegionsFiltered(RegionFilter filter) {
    auto all = listRegions(VM_PROT_NONE);
    std::vector<RegionInfo> filtered;

    for (auto &r : all) {
        switch (filter) {
            case RegionFilter::All:
                filtered.push_back(r);
                break;
            case RegionFilter::HeapOnly:
                if (r.isReadable() && r.isWritable() && !r.isExecutable())
                    filtered.push_back(r);
                break;
            case RegionFilter::DataOnly:
                if (r.isReadable() && r.isWritable() && !r.isExecutable())
                    filtered.push_back(r);
                break;
            case RegionFilter::StackOnly:
                // Stack regions are typically at high addresses and rw-
                if (r.isReadable() && r.isWritable() && r.start > 0x100000000ULL)
                    filtered.push_back(r);
                break;
            case RegionFilter::ReadWrite:
                if (r.isReadable() && r.isWritable())
                    filtered.push_back(r);
                break;
            case RegionFilter::Executable:
                if (r.isExecutable())
                    filtered.push_back(r);
                break;
        }
    }

    return filtered;
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
        if (path == imageName || path.find(imageName) != std::string::npos) {
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
    images.reserve(count);
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

uintptr_t Image::findSymbol(const std::string &imageName, const std::string &symbolName) {
    void *handle = dlopen(imageName.c_str(), RTLD_NOLOAD);
    if (!handle) return 0;
    void *sym = dlsym(handle, symbolName.c_str());
    dlclose(handle);
    return (uintptr_t)sym;
}

uintptr_t Image::findSymbol(const ImageInfo &img, const std::string &symbolName) {
    return findSymbol(img.name, symbolName);
}

// =============================================================================
// ValueType helpers
// =============================================================================

size_t valueTypeSize(ValueType type) {
    switch (type) {
        case ValueType::Int8: case ValueType::UInt8: return 1;
        case ValueType::Int16: case ValueType::UInt16: return 2;
        case ValueType::Int32: case ValueType::UInt32: case ValueType::Float32: return 4;
        case ValueType::Int64: case ValueType::UInt64: case ValueType::Float64: return 8;
    }
    return 4;
}

std::string valueTypeLabel(ValueType type) {
    switch (type) {
        case ValueType::Int8: return "i8";
        case ValueType::UInt8: return "u8";
        case ValueType::Int16: return "i16";
        case ValueType::UInt16: return "u16";
        case ValueType::Int32: return "i32";
        case ValueType::UInt32: return "u32";
        case ValueType::Int64: return "i64";
        case ValueType::UInt64: return "u64";
        case ValueType::Float32: return "f32";
        case ValueType::Float64: return "f64";
    }
    return "?";
}

// =============================================================================
// Scanner
// =============================================================================

// Parse IDA-style pattern, build skip table for Boyer-Moore-like speedup
struct PatternData {
    std::vector<uint8_t> bytes;
    std::vector<bool> mask;
    size_t firstSolidByte = 0; // index of first non-wildcard for skip
};

static bool parseIdaPattern(const std::string &pattern, PatternData &out) {
    out.bytes.clear();
    out.mask.clear();
    out.firstSolidByte = 0;

    std::istringstream ss(pattern);
    std::string token;
    bool foundFirst = false;

    while (ss >> token) {
        if (token == "?" || token == "??") {
            out.bytes.push_back(0);
            out.mask.push_back(false);
        } else {
            unsigned int val;
            std::istringstream hexSS(token);
            if (!(hexSS >> std::hex >> val) || val > 0xFF) return false;
            out.bytes.push_back(static_cast<uint8_t>(val));
            out.mask.push_back(true);
            if (!foundFirst) {
                out.firstSolidByte = out.bytes.size() - 1;
                foundFirst = true;
            }
        }
    }

    return !out.bytes.empty();
}

std::vector<uintptr_t> Scanner::findPattern(uintptr_t start, size_t len,
                                            const std::string &pattern) {
    std::vector<uintptr_t> results;
    PatternData pat;
    if (!parseIdaPattern(pattern, pat)) return results;

    const uint8_t *buf = reinterpret_cast<const uint8_t *>(start);
    size_t patLen = pat.bytes.size();
    if (len < patLen) return results;

    // Build skip table based on first solid byte
    // If the first solid byte doesn't match, we can skip
    uint8_t anchor = pat.bytes[pat.firstSolidByte];

    for (size_t i = 0; i <= len - patLen; ) {
        // Quick check on anchor byte
        if (buf[i + pat.firstSolidByte] != anchor) {
            i++;
            continue;
        }

        bool found = true;
        for (size_t j = 0; j < patLen; j++) {
            if (pat.mask[j] && buf[i + j] != pat.bytes[j]) {
                found = false;
                break;
            }
        }
        if (found) {
            results.push_back(start + i);
        }
        i++;
    }

    return results;
}

uintptr_t Scanner::findPatternFirst(uintptr_t start, size_t len,
                                    const std::string &pattern) {
    PatternData pat;
    if (!parseIdaPattern(pattern, pat)) return 0;

    const uint8_t *buf = reinterpret_cast<const uint8_t *>(start);
    size_t patLen = pat.bytes.size();
    if (len < patLen) return 0;

    uint8_t anchor = pat.bytes[pat.firstSolidByte];

    for (size_t i = 0; i <= len - patLen; ) {
        if (buf[i + pat.firstSolidByte] != anchor) { i++; continue; }

        bool found = true;
        for (size_t j = 0; j < patLen; j++) {
            if (pat.mask[j] && buf[i + j] != pat.bytes[j]) {
                found = false;
                break;
            }
        }
        if (found) return start + i;
        i++;
    }

    return 0;
}

std::vector<uintptr_t> Scanner::findPatternInImage(const ImageInfo &img,
                                                   const std::string &pattern) {
    std::vector<uintptr_t> allResults;
    if (!img.isValid()) return allResults;

    auto regions = Memory::listRegions(VM_PROT_READ);
    for (auto &region : regions) {
        if (region.start >= img.base && region.start < img.base + 0x10000000) {
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

// --- Narrowing ---

static int compareBytes(const uint8_t *a, const uint8_t *b, ValueType type) {
    switch (type) {
        case ValueType::Int8: {
            int8_t va, vb; memcpy(&va, a, 1); memcpy(&vb, b, 1);
            return (va > vb) - (va < vb);
        }
        case ValueType::UInt8: {
            uint8_t va, vb; memcpy(&va, a, 1); memcpy(&vb, b, 1);
            return (va > vb) - (va < vb);
        }
        case ValueType::Int16: {
            int16_t va, vb; memcpy(&va, a, 2); memcpy(&vb, b, 2);
            return (va > vb) - (va < vb);
        }
        case ValueType::UInt16: {
            uint16_t va, vb; memcpy(&va, a, 2); memcpy(&vb, b, 2);
            return (va > vb) - (va < vb);
        }
        case ValueType::Int32: {
            int32_t va, vb; memcpy(&va, a, 4); memcpy(&vb, b, 4);
            return (va > vb) - (va < vb);
        }
        case ValueType::UInt32: {
            uint32_t va, vb; memcpy(&va, a, 4); memcpy(&vb, b, 4);
            return (va > vb) - (va < vb);
        }
        case ValueType::Int64: {
            int64_t va, vb; memcpy(&va, a, 8); memcpy(&vb, b, 8);
            return (va > vb) - (va < vb);
        }
        case ValueType::UInt64: {
            uint64_t va, vb; memcpy(&va, a, 8); memcpy(&vb, b, 8);
            return (va > vb) - (va < vb);
        }
        case ValueType::Float32: {
            float va, vb; memcpy(&va, a, 4); memcpy(&vb, b, 4);
            return (va > vb) - (va < vb);
        }
        case ValueType::Float64: {
            double va, vb; memcpy(&va, a, 8); memcpy(&vb, b, 8);
            return (va > vb) - (va < vb);
        }
    }
    return 0;
}

std::vector<Scanner::Candidate> Scanner::narrowResults(
    const std::vector<Candidate> &candidates, ValueType type,
    CompareMode mode, const void *compareValue) {

    std::vector<Candidate> filtered;
    size_t sz = valueTypeSize(type);

    for (auto &c : candidates) {
        uint8_t currentBuf[8] = {};
        if (Memory::read(c.address, currentBuf, sz) != Status::Success) continue;

        bool keep = false;

        switch (mode) {
            case CompareMode::Exact:
                if (compareValue) {
                    keep = (memcmp(currentBuf, compareValue, sz) == 0);
                }
                break;
            case CompareMode::Changed:
                keep = (memcmp(currentBuf, c.snapshotValue.data(), sz) != 0);
                break;
            case CompareMode::Unchanged:
                keep = (memcmp(currentBuf, c.snapshotValue.data(), sz) == 0);
                break;
            case CompareMode::Increased:
                keep = (compareBytes(currentBuf, c.snapshotValue.data(), type) > 0);
                break;
            case CompareMode::Decreased:
                keep = (compareBytes(currentBuf, c.snapshotValue.data(), type) < 0);
                break;
            case CompareMode::GreaterThan:
                if (compareValue) keep = (compareBytes(currentBuf, (const uint8_t *)compareValue, type) > 0);
                break;
            case CompareMode::LessThan:
                if (compareValue) keep = (compareBytes(currentBuf, (const uint8_t *)compareValue, type) < 0);
                break;
        }

        if (keep) {
            Candidate newCandidate;
            newCandidate.address = c.address;
            newCandidate.snapshotValue.assign(currentBuf, currentBuf + sz);
            filtered.push_back(newCandidate);
        }
    }

    return filtered;
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

    p.m_origBytes.resize(len);
    if (Memory::read(address, p.m_origBytes.data(), len) != Status::Success) {
        return Patch{};
    }

    return p;
}

Patch Patch::createWithHex(uintptr_t address, const std::string &hex) {
    auto bytes = Hex::toBytes(hex);
    if (bytes.empty()) return Patch{};
    return createWithBytes(address, bytes.data(), bytes.size());
}

Patch Patch::createNop(uintptr_t address, size_t count) {
    // ARM64 NOP = 0xD503201F (little-endian: 1F 20 03 D5)
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

    if (Memory::write(m_address, m_patchBytes.data(), m_patchBytes.size()) == Status::Success) {
        m_applied = true;
        return true;
    }
    return false;
}

bool Patch::restore() {
    if (!isValid()) return false;
    if (!m_applied) return true;

    if (Memory::write(m_address, m_origBytes.data(), m_origBytes.size()) == Status::Success) {
        m_applied = false;
        return true;
    }
    return false;
}

bool Patch::isApplied() const {
    if (!isValid()) return false;
    std::vector<uint8_t> current(m_patchBytes.size());
    if (Memory::read(m_address, current.data(), current.size()) != Status::Success) return false;
    return current == m_patchBytes;
}

std::string Patch::currentHex() const {
    if (!isValid()) return "";
    std::vector<uint8_t> current(m_patchBytes.size());
    Memory::read(m_address, current.data(), current.size());
    return Hex::fromBytes(current.data(), current.size());
}

std::string Patch::originalHex() const {
    return Hex::fromBytes(m_origBytes);
}

std::string Patch::patchHex() const {
    return Hex::fromBytes(m_patchBytes);
}

// =============================================================================
// Hex
// =============================================================================

std::vector<uint8_t> Hex::toBytes(const std::string &hex) {
    std::vector<uint8_t> bytes;

    // Try space-separated first
    if (hex.find(' ') != std::string::npos || hex.find('?') != std::string::npos) {
        std::istringstream ss(hex);
        std::string token;
        while (ss >> token) {
            if (token == "?" || token == "??") {
                bytes.push_back(0);
                continue;
            }
            unsigned int val;
            std::istringstream hexSS(token);
            if (!(hexSS >> std::hex >> val) || val > 0xFF) return {};
            bytes.push_back(static_cast<uint8_t>(val));
        }
        return bytes;
    }

    // Contiguous hex string
    if (hex.length() >= 2) {
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

std::string Hex::fromBytes(const std::vector<uint8_t> &data) {
    return fromBytes(data.data(), data.size());
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
            if (j == 7) ss << ' '; // extra space at midpoint
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
    if (hex.empty()) return false;
    for (char c : hex) {
        if (c == ' ' || c == '?') continue;
        if (!isxdigit(c)) return false;
    }
    return true;
}

// =============================================================================
// Disasm (simplified ARM64 decoder)
// =============================================================================

namespace Disasm {

static std::string decodeARM64(uint32_t op, uintptr_t pc) {
    // NOP
    if (op == 0xD503201F) return "nop";

    // RET
    if ((op & 0xFFFFFC1F) == 0xD65F0000) {
        int rn = (op >> 5) & 0x1F;
        if (rn == 30) return "ret";
        return "ret x" + std::to_string(rn);
    }

    // B (unconditional branch)
    if ((op & 0xFC000000) == 0x14000000) {
        int32_t imm = (op & 0x03FFFFFF);
        if (imm & 0x02000000) imm |= 0xFC000000; // sign extend
        uintptr_t target = pc + (imm << 2);
        std::ostringstream ss;
        ss << "b 0x" << std::hex << target;
        return ss.str();
    }

    // BL
    if ((op & 0xFC000000) == 0x94000000) {
        int32_t imm = (op & 0x03FFFFFF);
        if (imm & 0x02000000) imm |= 0xFC000000;
        uintptr_t target = pc + (imm << 2);
        std::ostringstream ss;
        ss << "bl 0x" << std::hex << target;
        return ss.str();
    }

    // B.cond
    if ((op & 0xFF000010) == 0x54000000) {
        static const char *conds[] = {
            "eq","ne","cs","cc","mi","pl","vs","vc",
            "hi","ls","ge","lt","gt","le","al","nv"
        };
        int cond = op & 0xF;
        int32_t imm = ((op >> 5) & 0x7FFFF);
        if (imm & 0x40000) imm |= 0xFFF80000;
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
        if (imm7 & 0x40) imm7 |= 0xFFFFFF80;
        int sf = (op >> 31) & 1;
        std::ostringstream ss;
        ss << (isLoad ? "ldp " : "stp ");
        ss << (sf ? "x" : "w") << rt << ", " << (sf ? "x" : "w") << rt2;
        ss << ", [x" << rn;
        if (imm7) ss << ", #" << (imm7 * (sf ? 8 : 4));
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
        if (Memory::read(pc, &opcode, 4) != Status::Success) break;

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
    if (!insn.operands.empty()) ss << " " << insn.operands;
    return ss.str();
}

} // namespace Disasm

} // namespace Shirayuki
