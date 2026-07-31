#include "SYScanHelper.hpp"

#include <cstring>

using namespace Shirayuki;

namespace SYScan {
namespace {

// The non-numeric search modes. Anything else is resolved as a ValueType tag.
constexpr const char *kHexMode = "hex";
constexpr const char *kRegexMode = "regex";
constexpr const char *kStringMode = "string";

struct Plan {
    bool typed = false;
    ValueType type = ValueType::Int32;
    size_t valueSize = 0;
    // Parsed needle for typed scans.
    uint8_t value[kMaxValueSize] = {};
    Error error = Error::None;
};

// Resolve the request once, up front. Previously the type string was re-parsed
// per region through a 50-line if/else chain, and `outValSize` was assigned
// inside the region loop — so the width the caller saw depended on which region
// happened to be scanned last, and was left at a hardcoded 4 when no region
// qualified.
Plan makePlan(const Request &request) {
    Plan plan;

    if (request.typeTag == kHexMode || request.typeTag == kRegexMode ||
        request.typeTag == kStringMode) {
        if (request.input.empty())
            plan.error = Error::EmptyPattern;
        return plan;
    }

    if (!ValueFormat::tryFromTag(request.typeTag, plan.type)) {
        plan.error = Error::UnknownType;
        return plan;
    }

    plan.typed = true;
    plan.valueSize = valueTypeSize(plan.type);
    if (ValueFormat::parse(request.input, plan.type, plan.value) == 0) {
        plan.error = Error::InvalidValue;
        plan.typed = false;
        plan.valueSize = 0;
    }
    return plan;
}

// Typed scan dispatch. One template instead of the previous five copy-pasted
// branches, which is also why int8/uint8/uint16/uint32/uint64 were unreachable
// from the search tab: they simply were not listed, and fell through to the
// trailing else that treated "uint32" as a *string* search.
std::vector<uintptr_t> findTyped(uintptr_t start, size_t len, const Plan &plan) {
    switch (plan.type) {
        case ValueType::Int8:
            return Scanner::findValueBytes(start, len, plan.value, 1);
        case ValueType::UInt8:
            return Scanner::findValueBytes(start, len, plan.value, 1);
        case ValueType::Int16:
        case ValueType::UInt16:
            return Scanner::findValueBytes(start, len, plan.value, 2);
        case ValueType::Int32:
        case ValueType::UInt32:
        case ValueType::Float32:
            return Scanner::findValueBytes(start, len, plan.value, 4);
        case ValueType::Int64:
        case ValueType::UInt64:
        case ValueType::Float64:
            return Scanner::findValueBytes(start, len, plan.value, 8);
    }
    return {};
}

std::vector<uintptr_t> findUntyped(uintptr_t start, size_t len, const Request &request) {
    if (request.typeTag == kHexMode)
        return Scanner::findPattern(start, len, request.input);
    if (request.typeTag == kRegexMode)
        return Scanner::findRegex(start, len, request.input);
    return Scanner::findString(start, len, request.input);
}

// Capture the current value at each hit so the result can be narrowed later.
void captureSnapshots(Result &result) {
    if (result.valueSize == 0 || result.addresses.empty())
        return;

    result.snapshots.assign(result.addresses.size() * result.valueSize, 0);

    // A hit whose value can no longer be read is dropped rather than kept with a
    // zeroed snapshot, which would make it look "changed" on the next pass.
    size_t kept = 0;
    for (size_t i = 0; i < result.addresses.size(); i++) {
        uint8_t *dst = result.snapshots.data() + kept * result.valueSize;
        if (Memory::read(result.addresses[i], dst, result.valueSize) != Status::Success)
            continue;
        result.addresses[kept] = result.addresses[i];
        kept++;
    }
    result.addresses.resize(kept);
    result.snapshots.resize(kept * result.valueSize);
}

} // namespace

std::string describe(Error error) {
    switch (error) {
        case Error::None:
            return "";
        case Error::UnknownType:
            return "Unknown value type";
        case Error::InvalidValue:
            return "Invalid value for this type";
        case Error::EmptyPattern:
            return "Enter a pattern to search for";
        case Error::BadRegex:
            return "Invalid regular expression";
    }
    return "Scan failed";
}

Result scanRegion(uintptr_t start, size_t len, const Request &request) {
    Result result;
    const Plan plan = makePlan(request);
    if (plan.error != Error::None) {
        result.error = plan.error;
        return result;
    }

    result.valueSize = plan.valueSize;
    result.addresses = plan.typed ? findTyped(start, len, plan) : findUntyped(start, len, request);
    if (result.addresses.size() > request.maxResults)
        result.addresses.resize(request.maxResults);
    captureSnapshots(result);
    return result;
}

Result scanAll(const Request &request) {
    Result result;
    const Plan plan = makePlan(request);
    if (plan.error != Error::None) {
        result.error = plan.error;
        return result;
    }

    result.valueSize = plan.valueSize;

    const std::vector<RegionInfo> regions = Memory::listRegionsFiltered(RegionFilter::ReadWrite);

    for (const auto &region : regions) {
        if (result.addresses.size() >= request.maxResults)
            break;
        if (region.size > request.maxRegionSize)
            continue;

        std::vector<uintptr_t> hits = plan.typed ? findTyped(region.start, region.size, plan)
                                                 : findUntyped(region.start, region.size, request);

        const size_t room = request.maxResults - result.addresses.size();
        if (hits.size() > room)
            hits.resize(room);
        result.addresses.insert(result.addresses.end(), hits.begin(), hits.end());
    }

    captureSnapshots(result);
    return result;
}

Result narrow(const NarrowRequest &request) {
    Result result;

    ValueType type = ValueType::Int32;
    if (!ValueFormat::tryFromTag(request.typeTag, type)) {
        result.error = Error::UnknownType;
        return result;
    }

    const size_t valueSize = valueTypeSize(type);
    if (request.valueSize != valueSize || valueSize == 0) {
        // Snapshots were taken for a different type; narrowing them would
        // compare unrelated bytes.
        result.error = Error::UnknownType;
        return result;
    }

    // Exact/GreaterThan/LessThan compare against a value the user typed.
    uint8_t target[kMaxValueSize] = {};
    const bool needsTarget =
        (request.mode == CompareMode::Exact || request.mode == CompareMode::GreaterThan ||
         request.mode == CompareMode::LessThan);
    if (needsTarget && ValueFormat::parse(request.compareInput, type, target) == 0) {
        result.error = Error::InvalidValue;
        return result;
    }

    // Hand the whole set to the core, which owns the comparison semantics.
    std::vector<Scanner::Candidate> candidates;
    candidates.reserve(request.addresses.size());
    for (size_t i = 0; i < request.addresses.size(); i++) {
        const size_t offset = i * valueSize;
        if (offset + valueSize > request.snapshots.size())
            break;
        Scanner::Candidate c;
        c.address = request.addresses[i];
        c.snapshotValue.assign(request.snapshots.begin() + offset,
                               request.snapshots.begin() + offset + valueSize);
        candidates.push_back(std::move(c));
    }

    const std::vector<Scanner::Candidate> kept =
        Scanner::narrowResults(candidates, type, request.mode, needsTarget ? target : nullptr);

    result.valueSize = valueSize;
    result.addresses.reserve(kept.size());
    result.snapshots.reserve(kept.size() * valueSize);
    for (const auto &c : kept) {
        result.addresses.push_back(c.address);
        result.snapshots.insert(result.snapshots.end(), c.snapshotValue.begin(),
                                c.snapshotValue.end());
    }
    return result;
}

size_t writeAll(const std::vector<uintptr_t> &addresses, const std::string &typeTag,
                const std::string &input, Error &outError) {
    outError = Error::None;

    ValueType type = ValueType::Int32;
    if (!ValueFormat::tryFromTag(typeTag, type)) {
        outError = Error::UnknownType;
        return 0;
    }

    uint8_t value[kMaxValueSize] = {};
    const size_t width = ValueFormat::parse(input, type, value);
    if (width == 0) {
        // Reporting this matters: the previous code discarded the parse result
        // and wrote the zeroed buffer to every address in the result set.
        outError = Error::InvalidValue;
        return 0;
    }

    size_t written = 0;
    for (uintptr_t address : addresses) {
        if (Memory::write(address, value, width) == Status::Success)
            written++;
    }
    return written;
}

} // namespace SYScan
