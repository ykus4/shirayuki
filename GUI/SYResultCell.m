#import "SYResultCell.h"
#import "SYTheme.h"

@implementation SYResultCell

- (instancetype)initWithStyle:(UITableViewCellStyle)style
              reuseIdentifier:(NSString *)reuseIdentifier {
    self = [super initWithStyle:style reuseIdentifier:reuseIdentifier];
    if (self) {
        self.backgroundColor = [UIColor clearColor];
        self.selectionStyle = UITableViewCellSelectionStyleNone;

        _cardView = [[UIView alloc] init];
        _cardView.backgroundColor = [SYTheme bgSecondary];
        _cardView.layer.cornerRadius = [SYTheme radiusSmall];
        _cardView.translatesAutoresizingMaskIntoConstraints = NO;
        [self.contentView addSubview:_cardView];

        _iconView = [[UIImageView alloc] init];
        _iconView.contentMode = UIViewContentModeScaleAspectFit;
        _iconView.translatesAutoresizingMaskIntoConstraints = NO;
        [_cardView addSubview:_iconView];

        _titleLabel = [[UILabel alloc] init];
        _titleLabel.font = [SYTheme monoMedium];
        _titleLabel.textColor = [SYTheme textPrimary];
        _titleLabel.translatesAutoresizingMaskIntoConstraints = NO;
        [_cardView addSubview:_titleLabel];

        _detailLabel = [[UILabel alloc] init];
        _detailLabel.font = [SYTheme monoSmall];
        _detailLabel.textColor = [SYTheme textSecondary];
        _detailLabel.translatesAutoresizingMaskIntoConstraints = NO;
        [_cardView addSubview:_detailLabel];

        _badgeLabel = [[UILabel alloc] init];
        _badgeLabel.font = [SYTheme captionFont];
        _badgeLabel.textColor = [UIColor whiteColor];
        _badgeLabel.textAlignment = NSTextAlignmentCenter;
        _badgeLabel.layer.cornerRadius = 4;
        _badgeLabel.clipsToBounds = YES;
        _badgeLabel.translatesAutoresizingMaskIntoConstraints = NO;
        [_cardView addSubview:_badgeLabel];

        [NSLayoutConstraint activateConstraints:@[
            // Card
            [_cardView.topAnchor constraintEqualToAnchor:self.contentView.topAnchor constant:2],
            [_cardView.bottomAnchor constraintEqualToAnchor:self.contentView.bottomAnchor
                                                   constant:-2],
            [_cardView.leadingAnchor constraintEqualToAnchor:self.contentView.leadingAnchor
                                                    constant:8],
            [_cardView.trailingAnchor constraintEqualToAnchor:self.contentView.trailingAnchor
                                                     constant:-8],

            // Icon
            [_iconView.leadingAnchor constraintEqualToAnchor:_cardView.leadingAnchor constant:10],
            [_iconView.centerYAnchor constraintEqualToAnchor:_cardView.centerYAnchor],
            [_iconView.widthAnchor constraintEqualToConstant:20],
            [_iconView.heightAnchor constraintEqualToConstant:20],

            // Title
            [_titleLabel.leadingAnchor constraintEqualToAnchor:_iconView.trailingAnchor
                                                      constant:10],
            [_titleLabel.topAnchor constraintEqualToAnchor:_cardView.topAnchor constant:8],
            [_titleLabel.trailingAnchor constraintEqualToAnchor:_badgeLabel.leadingAnchor
                                                       constant:-8],

            // Detail
            [_detailLabel.leadingAnchor constraintEqualToAnchor:_titleLabel.leadingAnchor],
            [_detailLabel.topAnchor constraintEqualToAnchor:_titleLabel.bottomAnchor constant:2],
            [_detailLabel.trailingAnchor constraintEqualToAnchor:_titleLabel.trailingAnchor],
            [_detailLabel.bottomAnchor constraintLessThanOrEqualToAnchor:_cardView.bottomAnchor
                                                                constant:-8],

            // Badge
            [_badgeLabel.trailingAnchor constraintEqualToAnchor:_cardView.trailingAnchor
                                                       constant:-10],
            [_badgeLabel.centerYAnchor constraintEqualToAnchor:_cardView.centerYAnchor],
            [_badgeLabel.widthAnchor constraintGreaterThanOrEqualToConstant:32],
            [_badgeLabel.heightAnchor constraintEqualToConstant:18],
        ]];
    }
    return self;
}

- (void)configureWithIcon:(UIImage *)icon
                    title:(NSString *)title
                   detail:(NSString *)detail
                    badge:(NSString *)badge
               badgeColor:(UIColor *)badgeColor {
    _iconView.image = icon;
    _titleLabel.text = title;
    _detailLabel.text = detail;

    if (badge) {
        _badgeLabel.hidden = NO;
        _badgeLabel.text = [NSString stringWithFormat:@" %@ ", badge];
        _badgeLabel.backgroundColor = badgeColor ?: [SYTheme accentDim];
    } else {
        _badgeLabel.hidden = YES;
    }
}

- (void)setHighlighted:(BOOL)highlighted animated:(BOOL)animated {
    [super setHighlighted:highlighted animated:animated];
    [UIView animateWithDuration:0.15
                     animations:^{
                         self.cardView.backgroundColor =
                             highlighted ? [SYTheme bgTertiary] : [SYTheme bgSecondary];
                         self.cardView.transform = highlighted
                                                       ? CGAffineTransformMakeScale(0.98, 0.98)
                                                       : CGAffineTransformIdentity;
                     }];
}

@end
