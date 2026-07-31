#import "SYPointerHandler.h"

#import "PointerScan.hpp"
#import "SYInput.h"
#import "SYResultCell.h"
#import "SYTheme.h"
#import "SYToast.h"
#import "ShirayukiConfig.hpp"
#import "ShirayukiViewController.h"

using namespace Shirayuki;

/// Named so no `std::vector<...>` spelling is needed in an ObjC method signature
/// or inside a dispatch block (see the C++/ObjC boundary rule in CLAUDE.md).
typedef std::vector<PointerChain> SYPointerChainList;

static const CGFloat kCellIconSize = 14;

/// Upper bound on the caller-supplied offset window. `findPointersTo` sweeps
/// every readable region looking for pointers that land in
/// [target - maxOffset, target + maxOffset], so this value multiplies the cost of
/// an already expensive scan. 1 MB is far wider than any real object.
static const uintptr_t kPointerScanMaxOffsetWindow = 0x100000;

@implementation SYPointerHandler {
    /// Chains from the last scan, kept as C++ values rather than flattened into
    /// dictionaries so a row can be re-resolved on demand.
    SYPointerChainList _chains;
    /// Target of the last scan: what a chain must resolve to to still be valid.
    uintptr_t _target;
}

+ (NSDictionary<NSString *, NSString *> *)tabDescriptor {
    return @{
        @"title" : @"Ptr",
        @"icon" : @"arrow.triangle.branch",
        @"placeholder" : @"0xTARGET [depth] [0xMAXOFFSET]",
        @"typeLabel" : @"ptr",
        @"actionIcon" : @"magnifyingglass.circle.fill"
    };
}

#pragma mark - Actions

- (void)performAction:(NSString *)input {
    SYCommand *cmd = [SYCommand parse:input minArgs:0];
    if (!cmd.ok) {
        [self failWithMessage:cmd.error];
        return;
    }
    if (cmd.address == 0) {
        [self failWithMessage:@"Enter the target address to find pointers to"];
        return;
    }

    // Clamped against the scanner's own limit: the depth used to come straight
    // from -intValue, so "0x100 99" asked for a 99-level scan that would never
    // finish.
    const NSInteger depth = [cmd integerArgAt:0
                                     fallback:kPointerScanDefaultDepth
                                          min:1
                                          max:kPointerScanMaxDepth];

    // Parsed as an unsigned hex window, then clamped before it becomes the signed
    // maxOffset. The previous `int64_t maxOff = strtoull(...)` let a large window
    // arrive negative, which makes findPointersTo compute an inverted — i.e.
    // empty — scan range and silently report zero chains.
    uintptr_t maxOffset = kPointerScanDefaultMaxOffset;
    NSString *offsetArg = [cmd argAt:1];
    if (offsetArg.length) {
        if (!SYParseAddress(offsetArg, &maxOffset)) {
            [self failWithMessage:@"Max offset must be hexadecimal"];
            return;
        }
        if (maxOffset == 0)
            maxOffset = kPointerScanDefaultMaxOffset;
        if (maxOffset > kPointerScanMaxOffsetWindow)
            maxOffset = kPointerScanMaxOffsetWindow;
    }

    // Checked here as well as in -runInBackground: so the heap allocation below,
    // which is only released by the completion block, is never made for a scan
    // that the base class refuses to start.
    if (self.busy) {
        [SYToast show:@"A pointer scan is already running" type:SYToastWarning];
        return;
    }

    const uintptr_t target = cmd.address;
    const uint32_t maxDepth = (uint32_t)depth;
    const int64_t offsetWindow = (int64_t)maxOffset;
    _target = target;

    // Heap-allocated so the result survives the hop back to the main queue
    // without copying, and so the background block never names a template type.
    auto *found = new SYPointerChainList();

    [SYToast show:[NSString stringWithFormat:@"Scanning pointers to %@ (depth %ld)…",
                                             SYFormatAddress(target), (long)depth]
             type:SYToastInfo];

    __weak __typeof__(self) weakSelf = self;
    [self
        runInBackground:^{
            PointerScanConfig config;
            config.targetAddress = target;
            config.maxDepth = maxDepth;
            config.maxOffset = offsetWindow;
            config.maxResults = kPointerScanDefaultMaxResults;
            *found = PointerScanner::scan(config);
        }
        completion:^{
            __strong __typeof__(weakSelf) strongSelf = weakSelf;
            if (strongSelf)
                [strongSelf adoptChains:*found];
            delete found;
        }];
}

/// Install a finished scan result and report it. Zero chains is a warning, not
/// the success the old code always showed.
- (void)adoptChains:(const SYPointerChainList &)chains {
    _chains = chains;

    const size_t count = _chains.size();
    if (count == 0) {
        [self finishWithMessage:@"No pointer chains found — try a larger offset or depth"
                           type:SYToastWarning];
        return;
    }

    NSString *message = [NSString stringWithFormat:@"%zu chain%@", count, count == 1 ? @"" : @"s"];
    if (count >= kPointerScanDefaultMaxResults) {
        message = [NSString stringWithFormat:@"%zu chains (limit reached — some omitted)", count];
    }
    [self finishWithMessage:message type:SYToastSuccess];
}

#pragma mark - Table

- (NSInteger)numberOfRows {
    return (NSInteger)_chains.size();
}

/// The rows mirror `_chains`, so they are not individually removable: deleting a
/// row would desynchronise the two.
- (BOOL)canDeleteRow:(NSInteger)row {
    return NO;
}

- (void)configureCell:(SYResultCell *)cell forRow:(NSInteger)row {
    const PointerChain &chain = _chains[(size_t)row];

    // Re-resolved every time the cell is configured. The validity flag used to be
    // computed once on the scan thread, so a chain broken by a later
    // reallocation kept showing a green "OK" badge forever.
    const uintptr_t resolved = chain.resolve();
    const BOOL valid = (resolved != 0 && resolved == _target);

    NSString *detail = nil;
    if (valid) {
        detail = [NSString stringWithFormat:@"Depth %zu / resolves to %@", chain.offsets.size(),
                                            SYFormatAddress(resolved)];
    } else if (resolved != 0) {
        detail = [NSString stringWithFormat:@"Depth %zu / now points at %@", chain.offsets.size(),
                                            SYFormatAddress(resolved)];
    } else {
        detail = [NSString stringWithFormat:@"Depth %zu / does not resolve", chain.offsets.size()];
    }

    UIColor *color = valid ? [SYTheme success] : [SYTheme warning];
    [cell configureWithIcon:[SYTheme icon:@"arrow.triangle.branch" size:kCellIconSize color:color]
                      title:[self descriptionOfChain:chain]
                     detail:detail
                      badge:valid ? @"OK" : @"??"
                 badgeColor:color];
}

- (NSString *)descriptionOfChain:(const PointerChain &)chain {
    return [NSString stringWithUTF8String:chain.toString().c_str()] ?: @"<chain>";
}

- (void)didSelectRow:(NSInteger)row {
    if (row < 0 || (size_t)row >= _chains.size())
        return;
    NSString *desc = [self descriptionOfChain:_chains[(size_t)row]];
    [UIPasteboard generalPasteboard].string = desc;
    [SYToast show:@"Chain copied" type:SYToastInfo];
}

/// Distinct from a tap, which copies the chain notation: a long press copies the
/// address the chain currently resolves to. Previously it just called through to
/// -didSelectRow:, so the long-press affordance did nothing extra.
- (void)didLongPressRow:(NSInteger)row {
    if (row < 0 || (size_t)row >= _chains.size())
        return;
    const uintptr_t resolved = _chains[(size_t)row].resolve();
    if (resolved == 0) {
        [self failWithMessage:@"Chain no longer resolves"];
        return;
    }
    [self copyAddressToClipboard:resolved];
}

@end
