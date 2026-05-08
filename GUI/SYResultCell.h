#import <UIKit/UIKit.h>

@interface SYResultCell : UITableViewCell
@property (nonatomic, strong) UIImageView *iconView;
@property (nonatomic, strong) UILabel *titleLabel;
@property (nonatomic, strong) UILabel *detailLabel;
@property (nonatomic, strong) UILabel *badgeLabel;
@property (nonatomic, strong) UIView *cardView;

- (void)configureWithIcon:(UIImage *)icon
                    title:(NSString *)title
                   detail:(NSString *)detail
                    badge:(NSString *)badge
               badgeColor:(UIColor *)badgeColor;
@end
