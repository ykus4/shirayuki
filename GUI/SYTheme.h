#import <UIKit/UIKit.h>

// Shirayuki color theme — dark + cyan accent (snow/ice inspired)
@interface SYTheme : NSObject

// Background
+ (UIColor *)bgPrimary;    // main panel bg
+ (UIColor *)bgSecondary;  // card/cell bg
+ (UIColor *)bgTertiary;   // input fields

// Accent
+ (UIColor *)accent;       // cyan
+ (UIColor *)accentDim;    // muted cyan
+ (UIColor *)accentGlow;   // bright glow for active states

// Semantic
+ (UIColor *)success;
+ (UIColor *)warning;
+ (UIColor *)danger;
+ (UIColor *)info;

// Text
+ (UIColor *)textPrimary;
+ (UIColor *)textSecondary;
+ (UIColor *)textMuted;

// Fonts
+ (UIFont *)monoSmall;
+ (UIFont *)monoMedium;
+ (UIFont *)monoBold;
+ (UIFont *)titleFont;
+ (UIFont *)captionFont;

// Corner radius
+ (CGFloat)radiusSmall;
+ (CGFloat)radiusMedium;
+ (CGFloat)radiusLarge;

// SF Symbol helpers
+ (UIImage *)icon:(NSString *)name;
+ (UIImage *)icon:(NSString *)name size:(CGFloat)size;
+ (UIImage *)icon:(NSString *)name size:(CGFloat)size color:(UIColor *)color;

@end
