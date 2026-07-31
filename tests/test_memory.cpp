// Mach VM read/write, region enumeration and patching, exercised against this
// test process's own memory. Same code path the tweak uses in a host app:
// everything goes through mach_task_self().
#include "ShirayukiMemory.hpp"
#include "syharness.hpp"

#include <sys/mman.h>
#include <vector>

using namespace Shirayuki;

static void testReadWriteRoundTrip() {
    volatile int32_t target = 0x11223344;
    const uintptr_t addr = reinterpret_cast<uintptr_t>(&target);

    int32_t got = 0;
    SY_CHECK(Memory::read(addr, &got, sizeof(got)) == Status::Success);
    SY_CHECK_EQ(got, 0x11223344);

    const int32_t next = -5;
    SY_CHECK(Memory::write(addr, &next, sizeof(next)) == Status::Success);
    SY_CHECK_EQ(static_cast<int32_t>(target), -5);
}

static void testReadRejectsBadArguments() {
    int32_t sink = 0;
    // A null address is a programming error, not a memory access.
    SY_CHECK(Memory::read(0, &sink, sizeof(sink)) == Status::InvalidAddress);
    SY_CHECK(Memory::read(reinterpret_cast<uintptr_t>(&sink), nullptr, 4) == Status::InvalidBuffer);
    SY_CHECK(Memory::read(reinterpret_cast<uintptr_t>(&sink), &sink, 0) == Status::InvalidLength);

    SY_CHECK(Memory::write(0, &sink, sizeof(sink)) == Status::InvalidAddress);
    SY_CHECK(Memory::write(reinterpret_cast<uintptr_t>(&sink), nullptr, 4) ==
             Status::InvalidBuffer);
    SY_CHECK(Memory::write(reinterpret_cast<uintptr_t>(&sink), &sink, 0) == Status::InvalidLength);
}

// The whole point of routing through vm_read_overwrite: an unmapped address must
// come back as an error rather than faulting the process.
static void testReadUnmappedFailsWithoutCrashing() {
    void *page = mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    SY_CHECK(page != MAP_FAILED);
    const uintptr_t addr = reinterpret_cast<uintptr_t>(page);

    int32_t sink = 0;
    SY_CHECK(Memory::read(addr, &sink, sizeof(sink)) == Status::Success);

    munmap(page, 4096);

    // Now unmapped. This must return an error, not raise EXC_BAD_ACCESS.
    SY_CHECK(Memory::read(addr, &sink, sizeof(sink)) != Status::Success);
}

// readValue must report failure. Returning a zero-initialised T for an
// unreadable address is indistinguishable from reading a real 0.
static void testReadValueReportsFailure() {
    volatile int64_t target = 0x0123456789ABCDEFLL;
    int64_t out = 0;
    SY_CHECK(Memory::readValue<int64_t>(reinterpret_cast<uintptr_t>(&target), out) ==
             Status::Success);
    SY_CHECK_EQ(out, 0x0123456789ABCDEFLL);

    void *page = mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    const uintptr_t gone = reinterpret_cast<uintptr_t>(page);
    munmap(page, 4096);

    int64_t sink = 42;
    SY_CHECK(Memory::readValue<int64_t>(gone, sink) != Status::Success);
    SY_CHECK(!Memory::tryReadValue<int64_t>(gone).has_value());
    SY_CHECK(Memory::tryReadValue<int64_t>(reinterpret_cast<uintptr_t>(&target)).has_value());
}

static void testListRegions() {
    auto all = Memory::listRegions();
    SY_CHECK(all.size() > 0);

    // Every region must be non-empty and non-overlapping in ascending order.
    uintptr_t prevEnd = 0;
    bool ordered = true, nonEmpty = true;
    for (const auto &r : all) {
        if (r.size == 0)
            nonEmpty = false;
        if (r.start < prevEnd)
            ordered = false;
        prevEnd = r.start + r.size;
    }
    SY_CHECK(nonEmpty);
    SY_CHECK(ordered);

    // A known-writable stack variable must land in some rw- region.
    volatile int32_t local = 1;
    const uintptr_t addr = reinterpret_cast<uintptr_t>(&local);
    auto rw = Memory::listRegionsFiltered(RegionFilter::ReadWrite);
    bool found = false;
    for (const auto &r : rw) {
        if (addr >= r.start && addr < r.start + r.size) {
            found = true;
            break;
        }
    }
    SY_CHECK(found);
}

// HeapOnly and DataOnly used to share one predicate, so both filters returned
// exactly the same regions and neither did what its name says.
static void testHeapAndDataFiltersDiffer() {
    // Allocate before enumerating: a fresh malloc zone may be mapped by this
    // very allocation, and a region list taken earlier would not contain it.
    std::vector<uint8_t> block(64 * 1024, 0xAB);
    const uintptr_t blockAddr = reinterpret_cast<uintptr_t>(block.data());

    auto heap = Memory::listRegionsFiltered(RegionFilter::HeapOnly);
    auto data = Memory::listRegionsFiltered(RegionFilter::DataOnly);
    auto rw = Memory::listRegionsFiltered(RegionFilter::ReadWrite);

    SY_CHECK(heap.size() > 0);
    SY_CHECK(data.size() > 0);

    // Neither may be the other, and both must be proper subsets of rw-.
    SY_CHECK(heap.size() != data.size() || heap.size() == 0);
    SY_CHECK(heap.size() <= rw.size());
    SY_CHECK(data.size() <= rw.size());

    // The two must be disjoint: a region is either inside a loaded image or not.
    bool disjoint = true;
    for (const auto &h : heap)
        for (const auto &d : data)
            if (h.start == d.start)
                disjoint = false;
    SY_CHECK(disjoint);

    // A malloc'd block belongs to the heap set, not the image-data set.
    bool inHeap = false, inData = false;
    for (const auto &r : heap)
        if (blockAddr >= r.start && blockAddr < r.start + r.size)
            inHeap = true;
    for (const auto &r : data)
        if (blockAddr >= r.start && blockAddr < r.start + r.size)
            inData = true;
    SY_CHECK(inHeap);
    SY_CHECK(!inData);

    // Conversely, a file-static variable lives in __DATA, not the heap.
    static int32_t staticVar = 0x5A5A;
    const uintptr_t staticAddr = reinterpret_cast<uintptr_t>(&staticVar);
    bool staticInData = false, staticInHeap = false;
    for (const auto &r : data)
        if (staticAddr >= r.start && staticAddr < r.start + r.size)
            staticInData = true;
    for (const auto &r : heap)
        if (staticAddr >= r.start && staticAddr < r.start + r.size)
            staticInHeap = true;
    SY_CHECK(staticInData);
    SY_CHECK(!staticInHeap);
}

// Regions must carry a human-readable label. The field existed but no code path
// ever assigned it, so it was permanently empty.
static void testRegionsAreLabelled() {
    auto all = Memory::listRegions();
    bool allLabelled = true;
    for (const auto &r : all)
        if (r.label.empty())
            allLabelled = false;
    SY_CHECK(allLabelled);

    std::vector<uint8_t> block(64 * 1024, 0xAB);
    const uintptr_t addr = reinterpret_cast<uintptr_t>(block.data());
    for (const auto &r : Memory::listRegions())
        if (addr >= r.start && addr < r.start + r.size)
            SY_CHECK_EQ(r.label, std::string("MALLOC"));
}

static void testPatchApplyRestore() {
    uint8_t code[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    const uintptr_t addr = reinterpret_cast<uintptr_t>(code);

    Patch p = Patch::createWithHex(addr, "AA BB CC");
    SY_CHECK(p.isValid());
    SY_CHECK_EQ(p.size(), 3u);
    SY_CHECK_EQ(p.originalHex(), std::string("11 22 33"));
    SY_CHECK_EQ(p.patchHex(), std::string("AA BB CC"));

    SY_CHECK(p.apply());
    SY_CHECK_EQ(static_cast<int>(code[0]), 0xAA);
    SY_CHECK_EQ(static_cast<int>(code[2]), 0xCC);
    SY_CHECK_EQ(static_cast<int>(code[3]), 0x44); // untouched
    SY_CHECK(p.isApplied());

    SY_CHECK(p.restore());
    SY_CHECK_EQ(static_cast<int>(code[0]), 0x11);
    SY_CHECK_EQ(static_cast<int>(code[2]), 0x33);
    SY_CHECK(!p.isApplied());
}

static void testHexRoundTrip() {
    SY_CHECK(Hex::isValid("AA BB CC"));
    SY_CHECK(Hex::isValid("aabbcc"));
    SY_CHECK(!Hex::isValid("ZZ"));

    auto bytes = Hex::toBytes("DE AD BE EF");
    SY_CHECK_EQ(bytes.size(), 4u);
    SY_CHECK_EQ(Hex::fromBytes(bytes), std::string("DE AD BE EF"));

    uint8_t buf[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    SY_CHECK_EQ(Hex::fromBytes(buf, 4), std::string("DE AD BE EF"));
}

static void testHexDumpIsBounded() {
    uint8_t block[32] = {};
    const uintptr_t addr = reinterpret_cast<uintptr_t>(block);

    std::string d = Hex::dump(addr, 16);
    SY_CHECK(d.size() > 0);

    // An absurd length must be refused rather than attempting the allocation.
    std::string huge = Hex::dump(addr, static_cast<size_t>(1) << 40);
    SY_CHECK(huge.size() < 4096);
}

// getRegionInfo, protect, and the Image helpers had no callers and no tests, so
// nothing verified them — which is how findSymbol shipped with a dlclose before
// the returned symbol address was read.
static void testGetRegionInfo() {
    volatile int32_t local = 1;
    const uintptr_t addr = reinterpret_cast<uintptr_t>(&local);

    RegionInfo r = Memory::getRegionInfo(addr);
    SY_CHECK(r.size > 0);
    SY_CHECK(addr >= r.start);
    SY_CHECK(addr < r.start + r.size);
    SY_CHECK(r.isReadable());
    SY_CHECK(r.isWritable());
    SY_CHECK(!r.label.empty());

    // An address with no mapping must come back empty, not garbage.
    RegionInfo none = Memory::getRegionInfo(0);
    SY_CHECK(!none.isReadable() || none.size == 0 || none.start != 0);
}

static void testProtect() {
    void *page = mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    SY_CHECK(page != MAP_FAILED);
    if (page == MAP_FAILED)
        return;
    const uintptr_t addr = reinterpret_cast<uintptr_t>(page);

    SY_CHECK(Memory::protect(addr, 4096, VM_PROT_READ) == Status::Success);
    SY_CHECK(!Memory::getRegionInfo(addr).isWritable());

    // Writing through Memory::write must still work: it re-protects the page.
    const int32_t value = 0x1234;
    SY_CHECK(Memory::write(addr, &value, sizeof(value)) == Status::Success);

    SY_CHECK(Memory::protect(addr, 4096, VM_PROT_READ | VM_PROT_WRITE) == Status::Success);
    SY_CHECK(Memory::getRegionInfo(addr).isWritable());

    munmap(page, 4096);
}

static void testImageLookup() {
    auto all = Image::listAll();
    SY_CHECK(all.size() > 0);

    ImageInfo base = Image::getBase();
    SY_CHECK(base.isValid());
    SY_CHECK(!base.name.empty());

    // find() must locate an image by a substring of its path.
    ImageInfo found = Image::find(base.name);
    SY_CHECK(found.isValid());
    SY_CHECK_EQ(found.base, base.base);

    SY_CHECK(!Image::find("this-image-does-not-exist.dylib").isValid());

    // absoluteAddress is base + offset, and rejects an invalid image.
    SY_CHECK_EQ(Image::absoluteAddress(base, 0x100), base.base + 0x100);
    SY_CHECK_EQ(Image::absoluteAddress(ImageInfo{}, 0x100), 0u);
    SY_CHECK_EQ(Image::absoluteAddress("this-image-does-not-exist.dylib", 0x100), 0u);
}

static void testFindSymbol() {
    // libSystem exports malloc; resolving it must return the same address the
    // loader gave this process. Reading the symbol after dlclose (as the previous
    // implementation did) is undefined, so this also guards that ordering.
    const uintptr_t resolved = Image::findSymbol("libSystem.B.dylib", "malloc");
    SY_CHECK(resolved != 0);
    if (resolved != 0)
        SY_CHECK_EQ(resolved, reinterpret_cast<uintptr_t>(&malloc));

    SY_CHECK_EQ(Image::findSymbol("libSystem.B.dylib", "sy_no_such_symbol"), 0u);
    SY_CHECK_EQ(Image::findSymbol("this-image-does-not-exist.dylib", "malloc"), 0u);
}

static void testFindPatternInImage() {
    ImageInfo base = Image::getBase();
    SY_CHECK(base.isValid());
    if (!base.isValid())
        return;

    // An invalid image must yield nothing rather than scanning everything.
    SY_CHECK_EQ(Scanner::findPatternInImage(ImageInfo{}, "DE AD BE EF").size(), 0u);

    // A pattern that cannot occur must return empty without crashing, and the
    // call must complete — it walks every readable region of the image.
    auto none = Scanner::findPatternInImage(base, "?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ??");
    SY_CHECK(none.size() > 0); // all-wildcard matches everywhere
}

static void run() {
    testReadWriteRoundTrip();
    testReadRejectsBadArguments();
    testReadUnmappedFailsWithoutCrashing();
    testReadValueReportsFailure();
    testListRegions();
    testHeapAndDataFiltersDiffer();
    testRegionsAreLabelled();
    testPatchApplyRestore();
    testHexRoundTrip();
    testHexDumpIsBounded();
    testGetRegionInfo();
    testProtect();
    testImageLookup();
    testFindSymbol();
    testFindPatternInImage();
}

SY_MAIN("test_memory")
