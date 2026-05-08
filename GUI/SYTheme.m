#import "SYTheme.h"

@implementation SYTheme

// --- Background ---
+ (UIColor *)bgPrimary {
    return [UIColor colorWithRed:0.08 green:0.09 blue:0.12 alpha:0.96];
}
+ (UIColor *)bgSecondary {
    return [UIColor colorWithRed:0.12 green:0.13 blue:0.17 alpha:1.0];
}
+ (UIColor *)bgTertiary {
    return [UIColor colorWithRed:0.15 green:0.16 blue:0.21 alpha:1.0];
}

// --- Accent ---
+ (UIColor *)accent {
    return [UIColor colorWithRed:0.0 green:0.82 blue:0.95 alpha:1.0];
}
+ (UIColor *)accentDim {
    return [UIColor colorWithRed:0.0 green:0.55 blue:0.65 alpha:1.0];
}
+ (UIColor *)accentGlow {
    return [UIColor colorWithRed:0.4 green:0.95 blue:1.0 alpha:1.0];
}

// --- Semantic ---
+ (UIColor *)success {
    return [UIColor colorWithRed:0.2 green:0.9 blue:0.5 alpha:1.0];
}
+ (UIColor *)warning {
    return [UIColor colorWithRed:1.0 green:0.78 blue:0.2 alpha:1.0];
}
+ (UIColor *)danger {
    return [UIColor colorWithRed:1.0 green:0.3 blue:0.35 alpha:1.0];
}
+ (UIColor *)info {
    return [UIColor colorWithRed:0.55 green:0.6 blue:1.0 alpha:1.0];
}

// --- Text ---
+ (UIColor *)textPrimary {
    return [UIColor colorWithWhite:0.95 alpha:1.0];
}
+ (UIColor *)textSecondary {
    return [UIColor colorWithWhite:0.7 alpha:1.0];
}
+ (UIColor *)textMuted {
    return [UIColor colorWithWhite:0.45 alpha:1.0];
}

// --- Fonts ---
+ (UIFont *)monoSmall {
    return [UIFont monospacedSystemFontOfSize:11 weight:UIFontWeightRegular];
}
+ (UIFont *)monoMedium {
    return [UIFont monospacedSystemFontOfSize:13 weight:UIFontWeightRegular];
}
+ (UIFont *)monoBold {
    return [UIFont monospacedSystemFontOfSize:13 weight:UIFontWeightBold];
}
+ (UIFont *)titleFont {
    return [UIFont systemFontOfSize:15 weight:UIFontWeightSemibold];
}
+ (UIFont *)captionFont {
    return [UIFont systemFontOfSize:10 weight:UIFontWeightMedium];
}

// --- Radius ---
+ (CGFloat)radiusSmall { return 6.0; }
+ (CGFloat)radiusMedium { return 10.0; }
+ (CGFloat)radiusLarge { return 16.0; }

// --- SF Symbols ---
+ (UIImage *)icon:(NSString *)name {
    return [self icon:name size:16 color:[self accent]];
}

+ (UIImage *)icon:(NSString *)name size:(CGFloat)size {
    return [self icon:name size:size color:[self accent]];
}

+ (UIImage *)icon:(NSString *)name size:(CGFloat)size color:(UIColor *)color {
    UIImageSymbolConfiguration *config = [UIImageSymbolConfiguration
        configurationWithPointSize:size weight:UIImageSymbolWeightMedium];
    UIImage *img = [UIImage systemImageNamed:name withConfiguration:config];
    return [img imageWithTintColor:color renderingMode:UIImageRenderingModeAlwaysOriginal];
}

@end
