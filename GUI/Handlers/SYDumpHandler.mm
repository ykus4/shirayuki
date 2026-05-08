#import "SYDumpHandler.h"
#import "SYResultCell.h"
#import "SYTheme.h"
#import "SYToast.h"
#import "ShirayukiMemory.hpp"
#import "ShirayukiViewController.h"

using namespace Shirayuki;

static NSString *const kCellID = @"SYCell";

@interface SYDumpHandler ()
@property (nonatomic, strong) NSMutableArray<NSDictionary *> *disasmLines;
@property (nonatomic, assign) uintptr_t lastDumpAddr;
@end

@implementation SYDumpHandler

- (instancetype)init {
    self = [super init];
    if (self) {
        _disasmLines = [NSMutableArray new];
    }
    return self;
}

- (NSString *)tabTitle {
    return @"Dump";
}
- (NSString *)tabIcon {
    return @"doc.text";
}
- (NSString *)placeholder {
    return @"0xADDR [len|asm] (asm = disassemble)";
}
- (NSString *)typeLabel {
    return @"raw";
}
- (NSString *)actionIcon {
    return @"eye.fill";
}

- (void)performAction:(NSString *)input {
    NSArray *parts = [input componentsSeparatedByString:@" "];
    unsigned long long addr = strtoull([parts[0] UTF8String], NULL, 16);
    if (!addr) {
        [SYToast show:@"Invalid address" type:SYToastError];
        return;
    }

    _lastDumpAddr = (uintptr_t)addr;

    // Check for "asm" mode
    BOOL isDisasm = NO;
    size_t len = 64;

    if (parts.count > 1) {
        if ([[parts[1] lowercaseString] isEqualToString:@"asm"]) {
            isDisasm = YES;
            len = parts.count > 2 ? [parts[2] integerValue] : 16;
        } else {
            len = [parts[1] integerValue];
        }
    }
    if (len > 4096)
        len = 4096;

    if (isDisasm) {
        [self showDisassembly:addr count:len];
    } else {
        [self showHexDump:addr length:len];
    }
}

- (void)showHexDump:(uintptr_t)addr length:(size_t)len {
    std::string dump = Hex::dump(addr, len);

    UIAlertController *alert = [UIAlertController
        alertControllerWithTitle:[NSString stringWithFormat:@"0x%lX (%zu bytes)", addr, len]
                         message:nil
                  preferredStyle:UIAlertControllerStyleAlert];

    NSMutableAttributedString *attrMsg = [[NSMutableAttributedString alloc]
        initWithString:[NSString stringWithUTF8String:dump.c_str()]
            attributes:@{
                NSFontAttributeName : [SYTheme monoSmall],
                NSForegroundColorAttributeName : [SYTheme textPrimary]
            }];
    [alert setValue:attrMsg forKey:@"attributedMessage"];

    [alert addAction:[UIAlertAction actionWithTitle:@"Copy"
                                              style:UIAlertActionStyleDefault
                                            handler:^(UIAlertAction *a) {
                                                [UIPasteboard generalPasteboard].string =
                                                    [NSString stringWithUTF8String:dump.c_str()];
                                                [SYToast show:@"Copied" type:SYToastSuccess];
                                            }]];
    [alert addAction:[UIAlertAction actionWithTitle:@"OK"
                                              style:UIAlertActionStyleCancel
                                            handler:nil]];

    [self.viewController presentViewController:alert animated:YES completion:nil];
}

- (void)showDisassembly:(uintptr_t)addr count:(size_t)count {
    [_disasmLines removeAllObjects];

    auto insns = Disasm::disassemble(addr, count);
    for (auto &insn : insns) {
        NSString *formatted =
            [NSString stringWithUTF8String:Disasm::formatInstruction(insn).c_str()];
        [_disasmLines addObject:@{
            @"text" : formatted,
            @"address" : @(insn.address),
            @"mnemonic" : @(insn.mnemonic.c_str())
        }];
    }

    [SYToast show:[NSString stringWithFormat:@"%zu instructions", insns.size()] type:SYToastInfo];
    [self.viewController reloadTable];
}

- (NSInteger)numberOfRows {
    return _disasmLines.count;
}

- (UITableViewCell *)cellForRow:(NSInteger)row inTableView:(UITableView *)tableView {
    SYResultCell *cell =
        [tableView dequeueReusableCellWithIdentifier:kCellID
                                        forIndexPath:[NSIndexPath indexPathForRow:row inSection:0]];

    NSDictionary *line = _disasmLines[row];
    NSString *mnemonic = line[@"mnemonic"];

    // Color branches differently
    UIColor *iconColor = [SYTheme textSecondary];
    NSString *iconName = @"chevron.right";
    if ([mnemonic hasPrefix:@"b"] || [mnemonic isEqualToString:@"ret"]) {
        iconColor = [SYTheme warning];
        iconName = @"arrow.turn.down.right";
    } else if ([mnemonic hasPrefix:@"bl"]) {
        iconColor = [SYTheme info];
        iconName = @"arrow.right.circle";
    } else if ([mnemonic isEqualToString:@"nop"]) {
        iconColor = [SYTheme textMuted];
    }

    [cell configureWithIcon:[SYTheme icon:iconName size:12 color:iconColor]
                      title:line[@"text"]
                     detail:nil
                      badge:nil
                 badgeColor:nil];
    return cell;
}

- (void)didSelectRow:(NSInteger)row {
    NSString *text = _disasmLines[row][@"text"];
    [UIPasteboard generalPasteboard].string = text;
    [SYToast show:@"Copied" type:SYToastInfo];
}

- (void)didLongPressRow:(NSInteger)row {
    // NOP this instruction
    uintptr_t addr = [_disasmLines[row][@"address"] unsignedLongLongValue];
    auto patch = Patch::createNop(addr, 1);
    if (patch.isValid() && patch.apply()) {
        [SYToast show:@"NOPed" type:SYToastSuccess];
        // Refresh disasm
        [self showDisassembly:_lastDumpAddr count:_disasmLines.count];
    }
}

- (CGFloat)rowHeight {
    return 36;
}

@end
