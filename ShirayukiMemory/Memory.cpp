#include "ShirayukiMemory.hpp"
#include <libkern/OSCacheControl.h>
#include <mach/vm_map.h>

namespace Shirayuki {

// ARM64 stack region heuristic — regions above this address that are RW-only
// are treated as stack. Kept private to Memory.cpp.
static constexpr uintptr_t kStackRegionMinAddress = 0x100000000ULL;

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

    if (kr == KERN_SUCCESS) {
        ri.start = (uintptr_t)addr;
        ri.size = (size_t)size;
        ri.protection = info.protection;
    }

    return ri;
}

Status Memory::protect(uintptr_t address, size_t len, vm_prot_t prot) {
    kern_return_t kr =
        vm_protect(mach_task_self(), (vm_address_t)address, (vm_size_t)len, false, prot);
    return (kr == KERN_SUCCESS) ? Status::Success : Status::ProtectionFailed;
}

static bool regionMatches(const RegionInfo &r, RegionFilter filter) {
    switch (filter) {
        case RegionFilter::All:
            return true;
        case RegionFilter::HeapOnly:
        case RegionFilter::DataOnly:
            return r.isReadable() && r.isWritable() && !r.isExecutable();
        case RegionFilter::StackOnly:
            return r.isReadable() && r.isWritable() && r.start > kStackRegionMinAddress;
        case RegionFilter::ReadWrite:
            return r.isReadable() && r.isWritable();
        case RegionFilter::Executable:
            return r.isExecutable();
    }
    return false;
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
            regions.push_back(ri);
        }

        addr += size;
    }

    return regions;
}

std::vector<RegionInfo> Memory::listRegionsFiltered(RegionFilter filter) {
    auto all = listRegions(VM_PROT_NONE);
    if (filter == RegionFilter::All)
        return all;

    std::vector<RegionInfo> filtered;
    filtered.reserve(all.size());
    for (auto &r : all) {
        if (regionMatches(r, filter))
            filtered.push_back(r);
    }
    return filtered;
}

} // namespace Shirayuki
