#include "ShirayukiMemory.hpp"
#include <libkern/OSCacheControl.h>
#include <mach-o/getsect.h>
#include <mach/vm_map.h>

namespace Shirayuki {

// Allocator tags the kernel assigns to malloc-family mappings. The tag, not the
// address range, is what reliably identifies the heap: on arm64 the whole user
// address space sits above 4 GB, so an address threshold classifies nothing.
bool RegionInfo::isHeap() const {
    switch (userTag) {
        case VM_MEMORY_MALLOC:
        case VM_MEMORY_MALLOC_SMALL:
        case VM_MEMORY_MALLOC_LARGE:
        case VM_MEMORY_MALLOC_HUGE:
        case VM_MEMORY_REALLOC:
        case VM_MEMORY_MALLOC_TINY:
        case VM_MEMORY_MALLOC_LARGE_REUSABLE:
        case VM_MEMORY_MALLOC_LARGE_REUSED:
        case VM_MEMORY_MALLOC_NANO:
            return true;
        default:
            return false;
    }
}

bool RegionInfo::isStack() const {
    return userTag == VM_MEMORY_STACK;
}

namespace {

// Short human-readable name for a region, used in the dump/region UI. The
// `label` field was previously declared but never assigned by any code path.
std::string labelForTag(unsigned int tag, vm_prot_t prot) {
    switch (tag) {
        case VM_MEMORY_MALLOC:
        case VM_MEMORY_MALLOC_SMALL:
        case VM_MEMORY_MALLOC_LARGE:
        case VM_MEMORY_MALLOC_HUGE:
        case VM_MEMORY_REALLOC:
        case VM_MEMORY_MALLOC_TINY:
        case VM_MEMORY_MALLOC_LARGE_REUSABLE:
        case VM_MEMORY_MALLOC_LARGE_REUSED:
        case VM_MEMORY_MALLOC_NANO:
            return "MALLOC";
        case VM_MEMORY_STACK:
            return "STACK";
        case VM_MEMORY_DYLIB:
            return "DYLIB";
        case VM_MEMORY_SHARED_PMAP:
            return "SHARED";
        case VM_MEMORY_OS_ALLOC_ONCE:
            return "OS_ONCE";
        case 0:
            // Untagged: file-backed image mappings and the like. Fall back to
            // naming it by protection, which is what the user actually cares
            // about when picking a scan target.
            if (prot & VM_PROT_EXECUTE)
                return "__TEXT";
            if (prot & VM_PROT_WRITE)
                return "__DATA";
            return "__RODATA";
        default:
            return "OTHER";
    }
}

} // namespace

Status Memory::read(uintptr_t address, void *buffer, size_t len) {
    if (!address)
        return Status::InvalidAddress;
    if (!buffer)
        return Status::InvalidBuffer;
    if (!len)
        return Status::InvalidLength;

    vm_size_t outSize = 0;
    kern_return_t kr = vm_read_overwrite(mach_task_self(), (vm_address_t)address, (vm_size_t)len,
                                         (vm_address_t)buffer, &outSize);

    return (kr == KERN_SUCCESS) ? Status::Success : Status::Failed;
}

Status Memory::write(uintptr_t address, const void *buffer, size_t len) {
    if (!address)
        return Status::InvalidAddress;
    if (!buffer)
        return Status::InvalidBuffer;
    if (!len)
        return Status::InvalidLength;

    kern_return_t kr = vm_write(mach_task_self(), (vm_address_t)address, (vm_offset_t)buffer,
                                (mach_msg_type_number_t)len);

    if (kr == KERN_SUCCESS) {
        sys_icache_invalidate((void *)address, len);
        return Status::Success;
    }

    vm_address_t pageStart = address & ~(vm_page_size - 1);
    vm_size_t pageLen = (address + len - pageStart + vm_page_size - 1) & ~(vm_page_size - 1);

    vm_address_t regionAddr = pageStart;
    vm_size_t regionSize = 0;
    uint32_t depth = 0;
    vm_region_submap_short_info_data_64_t info;
    mach_msg_type_number_t count = VM_REGION_SUBMAP_SHORT_INFO_COUNT_64;
    vm_prot_t origProt = VM_PROT_READ | VM_PROT_EXECUTE;

    kern_return_t infoKr = vm_region_recurse_64(mach_task_self(), &regionAddr, &regionSize, &depth,
                                                (vm_region_recurse_info_t)&info, &count);
    if (infoKr == KERN_SUCCESS) {
        origProt = info.protection;
    }

    kr = vm_protect(mach_task_self(), pageStart, pageLen, false,
                    VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY);
    if (kr != KERN_SUCCESS)
        return Status::ProtectionFailed;

    kr = vm_write(mach_task_self(), (vm_address_t)address, (vm_offset_t)buffer,
                  (mach_msg_type_number_t)len);

    vm_protect(mach_task_self(), pageStart, pageLen, false, origProt);

    if (kr == KERN_SUCCESS) {
        sys_icache_invalidate((void *)address, len);
        return Status::Success;
    }

    return Status::Failed;
}

RegionInfo Memory::getRegionInfo(uintptr_t address) {
    RegionInfo ri{};
    vm_address_t addr = (vm_address_t)address;
    vm_size_t size = 0;
    uint32_t depth = 0;
    vm_region_submap_short_info_data_64_t info;
    mach_msg_type_number_t count = VM_REGION_SUBMAP_SHORT_INFO_COUNT_64;

    kern_return_t kr = vm_region_recurse_64(mach_task_self(), &addr, &size, &depth,
                                            (vm_region_recurse_info_t)&info, &count);

    // vm_region_recurse_64 returns the next region at or after `address`, so a
    // hit that starts beyond the requested address does not actually contain it.
    if (kr == KERN_SUCCESS && address >= (uintptr_t)addr &&
        address < (uintptr_t)addr + (size_t)size) {
        ri.start = (uintptr_t)addr;
        ri.size = (size_t)size;
        ri.protection = info.protection;
        ri.userTag = info.user_tag;
        ri.label = labelForTag(info.user_tag, info.protection);
    }

    return ri;
}

Status Memory::protect(uintptr_t address, size_t len, vm_prot_t prot) {
    kern_return_t kr =
        vm_protect(mach_task_self(), (vm_address_t)address, (vm_size_t)len, false, prot);
    return (kr == KERN_SUCCESS) ? Status::Success : Status::ProtectionFailed;
}

std::vector<RegionInfo> Memory::listRegions(vm_prot_t requiredProt) {
    std::vector<RegionInfo> regions;
    vm_address_t addr = 0;
    vm_size_t size = 0;

    while (true) {
        uint32_t depth = 0;
        vm_region_submap_short_info_data_64_t info;
        mach_msg_type_number_t count = VM_REGION_SUBMAP_SHORT_INFO_COUNT_64;

        kern_return_t kr = vm_region_recurse_64(mach_task_self(), &addr, &size, &depth,
                                                (vm_region_recurse_info_t)&info, &count);
        if (kr != KERN_SUCCESS)
            break;

        if (requiredProt == VM_PROT_NONE || (info.protection & requiredProt) == requiredProt) {
            RegionInfo ri;
            ri.start = (uintptr_t)addr;
            ri.size = (size_t)size;
            ri.protection = info.protection;
            ri.userTag = info.user_tag;
            ri.label = labelForTag(info.user_tag, info.protection);
            regions.push_back(ri);
        }

        // A zero-size region would leave `addr` unchanged and spin here forever,
        // appending the same entry until memory runs out. Likewise a region at
        // the very top of the address space would wrap `addr` back to 0 and
        // restart the walk.
        if (size == 0)
            break;
        const vm_address_t next = addr + size;
        if (next <= addr)
            break;
        addr = next;
    }

    return regions;
}

namespace {

struct AddressSpan {
    uintptr_t start = 0;
    uintptr_t end = 0;
};

// Exact bounds of every writable data segment across all loaded images.
//
// Testing "within kModuleMaxSize of an image base" instead does not work: that
// 256 MB window routinely swallows malloc zones allocated near the main binary,
// so heap regions were also reported as image data.
std::vector<AddressSpan> writableSegmentSpans() {
    static const char *kSegments[] = {SEG_DATA, "__DATA_CONST", "__DATA_DIRTY"};

    std::vector<AddressSpan> spans;
    const uint32_t imageCount = _dyld_image_count();
    for (uint32_t i = 0; i < imageCount; i++) {
        // _dyld_get_image_header returns mach_header* even on LP64, where the
        // real layout — and what getsegmentdata expects — is mach_header_64.
        const auto *header =
            reinterpret_cast<const struct mach_header_64 *>(_dyld_get_image_header(i));
        if (!header)
            continue;
        for (const char *segment : kSegments) {
            unsigned long size = 0;
            uint8_t *data = getsegmentdata(header, segment, &size);
            if (!data || size == 0)
                continue;
            const uintptr_t start = reinterpret_cast<uintptr_t>(data);
            spans.push_back({start, start + size});
        }
    }
    return spans;
}

bool overlapsAnySpan(const std::vector<AddressSpan> &spans, const RegionInfo &r) {
    const uintptr_t regionEnd = r.start + r.size;
    for (const auto &s : spans) {
        if (r.start < s.end && s.start < regionEnd)
            return true;
    }
    return false;
}

} // namespace

std::vector<RegionInfo> Memory::listRegionsFiltered(RegionFilter filter) {
    auto all = listRegions(VM_PROT_NONE);
    if (filter == RegionFilter::All)
        return all;

    // Only DataOnly needs the segment list; building it per region would make
    // the filter O(regions x images x segments).
    std::vector<AddressSpan> dataSpans;
    if (filter == RegionFilter::DataOnly)
        dataSpans = writableSegmentSpans();

    std::vector<RegionInfo> filtered;
    filtered.reserve(all.size());
    for (auto &r : all) {
        bool keep = false;
        switch (filter) {
            case RegionFilter::All:
                keep = true;
                break;
            // Malloc-tagged mappings. HeapOnly and DataOnly previously shared
            // one predicate, so the two filters returned identical results.
            case RegionFilter::HeapOnly:
                keep = r.isReadable() && r.isWritable() && r.isHeap();
                break;
            // rw- mappings overlapping a __DATA* segment of a loaded image.
            case RegionFilter::DataOnly:
                keep = r.isReadable() && r.isWritable() && !r.isExecutable() &&
                       overlapsAnySpan(dataSpans, r);
                break;
            case RegionFilter::StackOnly:
                keep = r.isReadable() && r.isWritable() && r.isStack();
                break;
            case RegionFilter::ReadWrite:
                keep = r.isReadable() && r.isWritable();
                break;
            case RegionFilter::Executable:
                keep = r.isExecutable();
                break;
        }
        if (keep)
            filtered.push_back(r);
    }
    return filtered;
}

} // namespace Shirayuki
