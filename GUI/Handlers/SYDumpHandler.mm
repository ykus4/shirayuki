#import "SYDumpHandler.h"

#import "SYInput.h"
#import "SYResultCell.h"
#import "SYTheme.h"
#import "SYToast.h"
#import "ShirayukiConfig.hpp"
#import "ShirayukiMemory.hpp"
#import "ShirayukiViewController.h"

#include <cctype>
#include <vector>

using namespace Shirayuki;

/// Row dictionary keys.
static NSString *const kRowKind = @"kind";
static NSString *const kRowTitle = @"title";
static NSString *const kRowDetail = @"detail";
static NSString *const kRowText = @"text";
static NSString *const kRowAddress = @"address";
static NSString *const kRowMnemonic = @"mnemonic";

static NSString *const kRowKindHex = @"hex";
static NSString *const kRowKindAsm = @"asm";

static const CGFloat kDumpRowHeight = 36;
static const CGFloat kCellIconSize = 12;

/// Default hex dump length, and the default number of instructions for `asm`.
static const NSInteger kDefaultHexDumpLength = 64;
static const NSInteger kDefaultDisasmCount = 16;

/// One ARM64 instruction is four bytes, so a disassembly request is bounded by
/// the same byte budget as the largest hex dump.
static const NSInteger kMaxDisasmCount = (NSInteger)(kMaxHexDumpLength / 4);

/// Bytes shown per table row. Half a `kHexDumpBytesPerLine` line: the full 16
/// bytes plus their ASCII do not fit the panel width in the cell's monospace
/// font, and a truncated hex line is worse than two whole ones.
static const size_t kDumpBytesPerRow = kHexDumpBytesPerLine / 2;

/// `asm` sub-command keyword.
static NSString *const kDisasmKeyword = @"asm";

typedef NS_ENUM(NSInteger, SYDumpMode) { SYDumpModeHex = 0, SYDumpModeDisasm };

/// std::string to NSString, never nil: the C++ layer can hand back bytes that are
/// not valid UTF-8, and `@(...)` boxing yields nil for those, which would throw
/// when inserted into a dictionary literal.
static NSString *SYDumpString(const std::string &value) {
    return [NSString stringWithUTF8String:value.c_str()] ?: @"<unprintable>";
}

/// Mnemonics that transfer control and return: rendered as calls.
static NSSet<NSString *> *SYCallMnemonics(void) {
    static NSSet<NSString *> *set = nil;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        set = [NSSet setWithArray:@[ @"bl", @"blr", @"blraa", @"blrab" ]];
    });
    return set;
}

/// Mnemonics that transfer control without returning, plus the return forms.
/// Exact matches, not a `hasPrefix:@"b"` test — that test claimed `bfi`, `bic`
/// and `brk` were branches, and made the `bl` branch of the old if/else chain
/// unreachable because "bl" also has the prefix "b".
static NSSet<NSString *> *SYBranchMnemonics(void) {
    static NSSet<NSString *> *set = nil;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        set = [NSSet setWithArray:@[
            @"b", @"br", @"braa", @"brab", @"cbz", @"cbnz", @"tbz", @"tbnz", @"ret", @"retaa",
            @"retab"
        ]];
    });
    return set;
}

@implementation SYDumpHandler {
    SYDumpMode _mode;
    /// Address of the last dump, and the length (bytes) or instruction count that
    /// was *requested* for it. The requested count is what a refresh re-uses —
    /// deriving it from the row count made a dump truncated by an unreadable page
    /// shrink again on every refresh.
    uintptr_t _lastAddress;
    NSInteger _lastRequestedCount;
    /// Whole last dump as text, for the copy-everything long press.
    NSString *_lastDumpText;
}

+ (NSDictionary<NSString *, NSString *> *)tabDescriptor {
    return @{
        @"title" : @"Dump",
        @"icon" : @"doc.text",
        @"placeholder" : @"0xADDR [len | asm [count]]",
        @"typeLabel" : @"raw",
        @"actionIcon" : @"eye.fill"
    };
}

#pragma mark - Actions

- (void)performAction:(NSString *)input {
    SYCommand *cmd = [SYCommand parse:input minArgs:0];
    if (!cmd.ok) {
        [self failWithMessage:cmd.error];
        return;
    }

    NSString *lengthArg = [cmd argAt:0];
    if (lengthArg.length && [lengthArg caseInsensitiveCompare:kDisasmKeyword] == NSOrderedSame) {
        const NSInteger count = [cmd integerArgAt:1
                                         fallback:kDefaultDisasmCount
                                              min:1
                                              max:kMaxDisasmCount];
        [self showDisassemblyAt:cmd.address count:(size_t)count];
        return;
    }

    // Rejected rather than quietly falling back: "0x100 sixteen" used to reach
    // -integerValue, produce a length of 0 and dump nothing at all.
    if (lengthArg.length && ![self isDecimal:lengthArg]) {
        [self failWithMessage:[NSString stringWithFormat:@"Expected a byte count or '%@'",
                                                         kDisasmKeyword]];
        return;
    }

    const NSInteger length = [cmd integerArgAt:0
                                      fallback:kDefaultHexDumpLength
                                           min:1
                                           max:(NSInteger)kMaxHexDumpLength];
    [self showHexDumpAt:cmd.address length:(size_t)length];
}

- (BOOL)isDecimal:(NSString *)text {
    if (!text.length)
        return NO;
    NSCharacterSet *nonDigits = [[NSCharacterSet decimalDigitCharacterSet] invertedSet];
    return [text rangeOfCharacterFromSet:nonDigits].location == NSNotFound;
}

#pragma mark - Presentation

- (void)showHexDumpAt:(uintptr_t)addr length:(size_t)len {
    NSArray<NSDictionary *> *rows = [self hexRowsAt:addr length:len];
    if (!rows) {
        [self failWithMessage:[NSString stringWithFormat:@"Cannot read %zu bytes at %@", len,
                                                         SYFormatAddress(addr)]];
        return;
    }

    _mode = SYDumpModeHex;
    _lastAddress = addr;
    _lastRequestedCount = (NSInteger)len;
    _lastDumpText = [[rows valueForKey:kRowText] componentsJoinedByString:@"\n"];

    [self adoptRows:rows
            message:[NSString stringWithFormat:@"%zu bytes at %@", len, SYFormatAddress(addr)]
               type:SYToastSuccess];
}

- (void)showDisassemblyAt:(uintptr_t)addr count:(size_t)count {
    NSArray<NSDictionary *> *rows = [self disasmRowsAt:addr count:count];

    _mode = SYDumpModeDisasm;
    _lastAddress = addr;
    _lastRequestedCount = (NSInteger)count;
    _lastDumpText = [[rows valueForKey:kRowText] componentsJoinedByString:@"\n"];

    if (rows.count == 0) {
        [self adoptRows:rows
                message:[NSString stringWithFormat:@"Nothing readable at %@", SYFormatAddress(addr)]
                   type:SYToastWarning];
        return;
    }

    // A dump cut short by an unreadable page is reported rather than presented as
    // if the whole request had been decoded.
    NSString *message = [NSString stringWithFormat:@"%lu instructions", (unsigned long)rows.count];
    SYToastType type = SYToastSuccess;
    if (rows.count < count) {
        message = [NSString stringWithFormat:@"%lu of %zu instructions (read stopped)",
                                             (unsigned long)rows.count, count];
        type = SYToastWarning;
    }
    [self adoptRows:rows message:message type:type];
}

/// Install a finished row set and report it. The only place the table model is
/// replaced.
- (void)adoptRows:(NSArray<NSDictionary *> *)rows
          message:(NSString *)message
             type:(SYToastType)type {
    [self.entries setArray:rows ?: @[]];
    [self finishWithMessage:message type:type];
}

#pragma mark - Data production

/// Read `len` bytes and format them as table rows, or nil when the range cannot
/// be read.
///
/// The bytes are read here rather than through Hex::dump because that function
/// reports failure in-band, returning the literal strings "<read failed>" and
/// "<length too large>" that the previous alert happily displayed as if they were
/// a dump.
- (NSArray<NSDictionary *> *)hexRowsAt:(uintptr_t)addr length:(size_t)len {
    if (len == 0 || len > kMaxHexDumpLength)
        return nil;

    std::vector<uint8_t> buf(len);
    if (Memory::read(addr, buf.data(), len) != Status::Success)
        return nil;

    NSMutableArray<NSDictionary *> *rows = [NSMutableArray new];
    for (size_t off = 0; off < len; off += kDumpBytesPerRow) {
        const size_t lineLen = (len - off < kDumpBytesPerRow) ? (len - off) : kDumpBytesPerRow;
        const uintptr_t lineAddr = addr + off;

        NSString *hex = SYDumpString(Hex::fromBytes(buf.data() + off, lineLen));
        NSMutableString *ascii = [NSMutableString stringWithCapacity:lineLen];
        for (size_t j = 0; j < lineLen; j++) {
            const unsigned char c = buf[off + j];
            [ascii appendFormat:@"%c", std::isprint(c) ? (char)c : '.'];
        }

        NSString *addressText = SYFormatAddress(lineAddr);
        [rows addObject:@{
            kRowKind : kRowKindHex,
            kRowTitle : hex,
            kRowDetail : [NSString stringWithFormat:@"%@  |%@|", addressText, ascii],
            kRowAddress : @(lineAddr),
            kRowText : [NSString stringWithFormat:@"%@  %@  |%@|", addressText, hex, ascii]
        }];
    }
    return rows;
}

/// Disassemble `count` instructions into table rows. Never nil; an unreadable
/// address simply yields no rows.
- (NSArray<NSDictionary *> *)disasmRowsAt:(uintptr_t)addr count:(size_t)count {
    NSMutableArray<NSDictionary *> *rows = [NSMutableArray new];

    auto insns = Disasm::disassemble(addr, count);
    for (auto &insn : insns) {
        NSString *mnemonic = SYDumpString(insn.mnemonic);
        NSString *operands = SYDumpString(insn.operands);
        NSString *title =
            operands.length ? [NSString stringWithFormat:@"%@ %@", mnemonic, operands] : mnemonic;
        [rows addObject:@{
            kRowKind : kRowKindAsm,
            kRowTitle : title,
            kRowDetail :
                [NSString stringWithFormat:@"%@  %08X", SYFormatAddress(insn.address), insn.opcode],
            kRowAddress : @(insn.address),
            kRowMnemonic : mnemonic,
            kRowText : SYDumpString(Disasm::formatInstruction(insn))
        }];
    }
    return rows;
}

#pragma mark - Table

- (CGFloat)rowHeight {
    return kDumpRowHeight;
}

- (void)configureCell:(SYResultCell *)cell forRow:(NSInteger)row {
    NSDictionary *entry = self.entries[(NSUInteger)row];

    NSString *iconName = @"number";
    UIColor *iconColor = [SYTheme textSecondary];

    if ([entry[kRowKind] isEqualToString:kRowKindAsm]) {
        NSString *mnemonic = entry[kRowMnemonic] ?: @"";
        iconName = @"chevron.right";
        // Ordered so the more specific class wins, and conditional branches
        // ("b.eq") are matched by their own prefix instead of by "b".
        if ([SYCallMnemonics() containsObject:mnemonic]) {
            iconName = @"arrow.right.circle";
            iconColor = [SYTheme info];
        } else if ([SYBranchMnemonics() containsObject:mnemonic] || [mnemonic hasPrefix:@"b."]) {
            iconName = @"arrow.turn.down.right";
            iconColor = [SYTheme warning];
        } else if ([mnemonic isEqualToString:@"nop"]) {
            iconColor = [SYTheme textMuted];
        }
    }

    [cell configureWithIcon:[SYTheme icon:iconName size:kCellIconSize color:iconColor]
                      title:entry[kRowTitle]
                     detail:entry[kRowDetail]
                      badge:nil
                 badgeColor:nil];
}

/// The rows are a rendering of a memory range, not a user-managed list, so a
/// swipe must not remove one.
- (BOOL)canDeleteRow:(NSInteger)row {
    return NO;
}

- (void)didSelectRow:(NSInteger)row {
    if (row < 0 || (NSUInteger)row >= self.entries.count)
        return;
    NSString *text = self.entries[(NSUInteger)row][kRowText];
    if (!text.length)
        return;
    [UIPasteboard generalPasteboard].string = text;
    [SYToast show:@"Line copied" type:SYToastInfo];
}

- (void)didLongPressRow:(NSInteger)row {
    if (row < 0 || (NSUInteger)row >= self.entries.count)
        return;
    NSDictionary *entry = self.entries[(NSUInteger)row];

    // Hex rows: copy the whole dump. This replaces the "Copy" action of the alert
    // the dump used to be presented in.
    if (![entry[kRowKind] isEqualToString:kRowKindAsm]) {
        if (!_lastDumpText.length) {
            [self failWithMessage:@"Nothing to copy"];
            return;
        }
        [UIPasteboard generalPasteboard].string = _lastDumpText;
        [SYToast show:@"Dump copied" type:SYToastSuccess];
        return;
    }

    [self confirmNopAt:[entry[kRowAddress] unsignedLongLongValue]];
}

#pragma mark - NOP

/// A long press used to overwrite the instruction immediately, with no prompt and
/// no report when the write failed.
- (void)confirmNopAt:(uintptr_t)addr {
    ShirayukiViewController *vc = self.viewController;
    if (!vc)
        return;

    UIAlertController *alert = [UIAlertController
        alertControllerWithTitle:@"Replace with NOP?"
                         message:[NSString
                                     stringWithFormat:@"Overwrite the instruction at %@ with "
                                                      @"NOP.\n\nThis write is not recorded in "
                                                      @"the Patch tab, so it cannot be undone "
                                                      @"or restored from there.",
                                                      SYFormatAddress(addr)]
                  preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"Cancel"
                                              style:UIAlertActionStyleCancel
                                            handler:nil]];

    __weak __typeof__(self) weakSelf = self;
    [alert addAction:[UIAlertAction actionWithTitle:@"NOP"
                                              style:UIAlertActionStyleDestructive
                                            handler:^(UIAlertAction *action) {
                                                __strong __typeof__(weakSelf) strongSelf = weakSelf;
                                                [strongSelf applyNopAt:addr];
                                            }]];

    [vc presentViewController:alert animated:YES completion:nil];
}

- (void)applyNopAt:(uintptr_t)addr {
    // The Patch object is deliberately local, and discarding it means the original
    // bytes it captured are lost: a NOP made here cannot be undone, unlike one
    // made from the Patch tab. Undo support needs a patch registry shared between
    // the two tabs.
    auto patch = Patch::createNop(addr, 1);
    if (!patch.isValid() || !patch.apply()) {
        [self
            failWithMessage:[NSString stringWithFormat:@"NOP failed at %@", SYFormatAddress(addr)]];
        return;
    }

    // Re-disassemble the range that was originally requested, not one instruction
    // per existing row.
    if (_mode == SYDumpModeDisasm) {
        NSArray<NSDictionary *> *rows = [self disasmRowsAt:_lastAddress
                                                     count:(size_t)MAX(_lastRequestedCount, 1)];
        if (rows.count)
            [self.entries setArray:rows];
    }

    [self finishWithMessage:[NSString stringWithFormat:@"NOPed %@", SYFormatAddress(addr)]
                       type:SYToastSuccess];
}

@end
