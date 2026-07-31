#import "SYInput.h"

#import <errno.h>
#import <stdlib.h>

NSString *SYFormatAddress(uintptr_t address) {
    return [NSString stringWithFormat:@"0x%llX", (unsigned long long)address];
}

BOOL SYParseAddress(NSString *text, uintptr_t *outAddress) {
    if (!outAddress)
        return NO;
    *outAddress = 0;

    NSString *trimmed =
        [text stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if (trimmed.length == 0)
        return NO;

    const char *cstr = [trimmed UTF8String];
    if (!cstr || !*cstr)
        return NO;

    // A leading sign would be accepted by strtoull and wrap around; addresses are
    // unsigned, so reject it outright rather than parsing "-1" as 0xFFFF... .
    if (*cstr == '-' || *cstr == '+')
        return NO;

    errno = 0;
    char *end = NULL;
    unsigned long long value = strtoull(cstr, &end, 16);

    // The end pointer is what makes failure detectable at all: without it,
    // strtoull returns 0 for "garbage" and 0x1 for "0x1zzz", neither
    // distinguishable from a legitimate parse.
    if (end == cstr || (end && *end != '\0'))
        return NO;
    if (errno == ERANGE)
        return NO;

    *outAddress = (uintptr_t)value;
    return YES;
}

@implementation SYCommand

+ (instancetype)parse:(NSString *)input minArgs:(NSUInteger)minArgs {
    SYCommand *cmd = [[SYCommand alloc] init];
    cmd->_ok = NO;
    cmd->_address = 0;
    cmd->_args = @[];
    cmd->_error = nil;

    NSString *trimmed =
        [input stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if (trimmed.length == 0) {
        cmd->_error = @"Enter an address";
        return cmd;
    }

    // Split on any run of whitespace, dropping empties, so "0x100  5" yields one
    // argument rather than an empty string followed by "5".
    NSMutableArray<NSString *> *tokens = [NSMutableArray array];
    for (NSString *part in
         [trimmed componentsSeparatedByCharactersInSet:[NSCharacterSet whitespaceCharacterSet]]) {
        if (part.length > 0)
            [tokens addObject:part];
    }
    if (tokens.count == 0) {
        cmd->_error = @"Enter an address";
        return cmd;
    }

    uintptr_t address = 0;
    if (!SYParseAddress(tokens[0], &address)) {
        cmd->_error = [NSString stringWithFormat:@"Invalid address: %@", tokens[0]];
        return cmd;
    }
    // Address 0 is never a valid target, but it is now distinguishable from a
    // parse failure and gets its own message.
    if (address == 0) {
        cmd->_error = @"Address cannot be zero";
        return cmd;
    }

    NSArray<NSString *> *args = [tokens subarrayWithRange:NSMakeRange(1, tokens.count - 1)];
    if (args.count < minArgs) {
        cmd->_error = @"Expected: <address> <value>";
        return cmd;
    }

    cmd->_ok = YES;
    cmd->_address = address;
    cmd->_args = args;
    return cmd;
}

- (NSString *)argAt:(NSUInteger)index {
    if (index >= self.args.count)
        return nil;
    return self.args[index];
}

- (NSString *)argAt:(NSUInteger)index or:(NSString *)fallback {
    NSString *v = [self argAt:index];
    return (v.length > 0) ? v : fallback;
}

- (NSInteger)integerArgAt:(NSUInteger)index
                 fallback:(NSInteger)fallback
                      min:(NSInteger)minValue
                      max:(NSInteger)maxValue {
    NSString *raw = [self argAt:index];
    NSInteger value = fallback;

    if (raw.length > 0) {
        // NSScanner rejects trailing garbage, unlike -integerValue which returns
        // 0 for "abc" and silently truncates "12abc" to 12.
        NSScanner *scanner = [NSScanner scannerWithString:raw];
        NSInteger scanned = 0;
        if ([scanner scanInteger:&scanned] && scanner.isAtEnd)
            value = scanned;
    }

    if (value < minValue)
        value = minValue;
    if (value > maxValue)
        value = maxValue;
    return value;
}

@end
