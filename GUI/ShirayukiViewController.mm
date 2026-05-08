#import "ShirayukiViewController.h"
#import "Freeze.hpp"
#import "Handlers/SYDumpHandler.h"
#import "Handlers/SYFreezeHandler.h"
#import "Handlers/SYPatchHandler.h"
#import "Handlers/SYPointerHandler.h"
#import "Handlers/SYSearchHandler.h"
#import "Handlers/SYWatchHandler.h"
#import "SYResultCell.h"
#import "SYTabHandler.h"
#import "SYTheme.h"
#import "SYToast.h"
#import "ShirayukiMemory.hpp"
#import "ShirayukiWindow.h"

using namespace Shirayuki;

static NSString *const kCellID = @"SYCell";

@interface ShirayukiViewController () <UITableViewDelegate, UITableViewDataSource,
                                       UITextFieldDelegate>
@property (nonatomic, strong) UIView *headerView;
@property (nonatomic, strong) UIScrollView *tabBar;
@property (nonatomic, strong) NSArray<UIButton *> *tabButtons;
@property (nonatomic, strong) UIView *tabIndicator;
@property (nonatomic, strong) UIView *inputContainer;
@property (nonatomic, strong) UITextField *inputField;
@property (nonatomic, strong) UIButton *actionButton;
@property (nonatomic, strong) UIButton *typeButton;
@property (nonatomic, strong) UIView *narrowBar; // for search narrowing buttons
@property (nonatomic, strong) UILabel *statusLabel;
@property (nonatomic, strong) UITableView *tableView;

@property (nonatomic, strong) NSArray<id<SYTabHandler>> *handlers;
@property (nonatomic, assign) NSInteger currentTabIndex;

// Typed handler accessors — safe against reordering
@property (nonatomic, readonly) SYSearchHandler *searchHandler;
@property (nonatomic, readonly) SYPatchHandler *patchHandler;
@property (nonatomic, readonly) SYFreezeHandler *freezeHandler;
@property (nonatomic, readonly) SYWatchHandler *watchHandler;
@end

@implementation ShirayukiViewController

- (void)viewDidLoad {
    [super viewDidLoad];

    self.view.backgroundColor = [SYTheme bgPrimary];
    self.view.layer.cornerRadius = [SYTheme radiusLarge];
    self.view.clipsToBounds = YES;
    self.view.layer.borderColor = [SYTheme accentDim].CGColor;
    self.view.layer.borderWidth = 0.5;

    [self setupHandlers];
    [self buildHeader];
    [self buildTabBar];
    [self buildInputArea];
    [self buildNarrowBar];
    [self buildTableView];
    [self setupGestures];
    [self updateForCurrentTab:NO];
}

- (void)setupHandlers {
    SYSearchHandler *search = [SYSearchHandler new];
    search.viewController = self;
    SYPatchHandler *patch = [SYPatchHandler new];
    patch.viewController = self;
    SYFreezeHandler *freeze = [SYFreezeHandler new];
    freeze.viewController = self;
    SYWatchHandler *watch = [SYWatchHandler new];
    watch.viewController = self;
    SYPointerHandler *ptr = [SYPointerHandler new];
    ptr.viewController = self;
    SYDumpHandler *dump = [SYDumpHandler new];
    dump.viewController = self;

    _handlers = @[ search, patch, freeze, watch, ptr, dump ];
    _currentTabIndex = 0;
}

- (id<SYTabHandler>)currentHandler {
    return _handlers[_currentTabIndex];
}

- (SYSearchHandler *)searchHandler {
    return (SYSearchHandler *)_handlers[0];
}
- (SYPatchHandler *)patchHandler {
    return (SYPatchHandler *)_handlers[1];
}
- (SYFreezeHandler *)freezeHandler {
    return (SYFreezeHandler *)_handlers[2];
}
- (SYWatchHandler *)watchHandler {
    return (SYWatchHandler *)_handlers[3];
}

#pragma mark - Build UI

- (void)buildHeader {
    _headerView = [[UIView alloc] init];
    _headerView.backgroundColor = [SYTheme bgSecondary];
    _headerView.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:_headerView];

    UIImageView *logo = [[UIImageView alloc] initWithImage:[SYTheme icon:@"snowflake"
                                                                    size:15
                                                                   color:[SYTheme accent]]];
    logo.translatesAutoresizingMaskIntoConstraints = NO;
    [_headerView addSubview:logo];

    UILabel *title = [[UILabel alloc] init];
    title.text = @"Shirayuki";
    title.font = [SYTheme titleFont];
    title.textColor = [SYTheme textPrimary];
    title.translatesAutoresizingMaskIntoConstraints = NO;
    [_headerView addSubview:title];

    UIButton *closeBtn = [UIButton buttonWithType:UIButtonTypeSystem];
    [closeBtn setImage:[SYTheme icon:@"xmark" size:11 color:[SYTheme danger]]
              forState:UIControlStateNormal];
    closeBtn.translatesAutoresizingMaskIntoConstraints = NO;
    [closeBtn addTarget:self
                  action:@selector(closeTapped)
        forControlEvents:UIControlEventTouchUpInside];
    [_headerView addSubview:closeBtn];

    [NSLayoutConstraint activateConstraints:@[
        [_headerView.topAnchor constraintEqualToAnchor:self.view.topAnchor],
        [_headerView.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
        [_headerView.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
        [_headerView.heightAnchor constraintEqualToConstant:34],
        [logo.leadingAnchor constraintEqualToAnchor:_headerView.leadingAnchor constant:12],
        [logo.centerYAnchor constraintEqualToAnchor:_headerView.centerYAnchor],
        [title.leadingAnchor constraintEqualToAnchor:logo.trailingAnchor constant:6],
        [title.centerYAnchor constraintEqualToAnchor:_headerView.centerYAnchor],
        [closeBtn.trailingAnchor constraintEqualToAnchor:_headerView.trailingAnchor constant:-8],
        [closeBtn.centerYAnchor constraintEqualToAnchor:_headerView.centerYAnchor],
        [closeBtn.widthAnchor constraintEqualToConstant:28],
        [closeBtn.heightAnchor constraintEqualToConstant:28],
    ]];
}

- (void)buildTabBar {
    _tabBar = [[UIScrollView alloc] init];
    _tabBar.showsHorizontalScrollIndicator = NO;
    _tabBar.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:_tabBar];

    NSMutableArray *buttons = [NSMutableArray new];
    CGFloat x = 6;

    for (NSUInteger i = 0; i < _handlers.count; i++) {
        id<SYTabHandler> h = _handlers[i];
        UIButton *btn = [UIButton buttonWithType:UIButtonTypeSystem];
        UIImage *icon = [SYTheme icon:[h tabIcon] size:11 color:[SYTheme textMuted]];
        [btn setImage:icon forState:UIControlStateNormal];
        [btn setTitle:[NSString stringWithFormat:@" %@", [h tabTitle]]
             forState:UIControlStateNormal];
        [btn setTitleColor:[SYTheme textMuted] forState:UIControlStateNormal];
        btn.titleLabel.font = [SYTheme captionFont];
        btn.tag = i;
        [btn sizeToFit];
        btn.frame = CGRectMake(x, 3, btn.frame.size.width + 14, 24);
        [btn addTarget:self
                      action:@selector(tabTapped:)
            forControlEvents:UIControlEventTouchUpInside];
        [_tabBar addSubview:btn];
        [buttons addObject:btn];
        x += btn.frame.size.width + 3;
    }
    _tabBar.contentSize = CGSizeMake(x, 30);
    _tabButtons = buttons;

    _tabIndicator = [[UIView alloc] initWithFrame:CGRectMake(6, 27, 40, 2)];
    _tabIndicator.backgroundColor = [SYTheme accent];
    _tabIndicator.layer.cornerRadius = 1;
    [_tabBar addSubview:_tabIndicator];

    [NSLayoutConstraint activateConstraints:@[
        [_tabBar.topAnchor constraintEqualToAnchor:_headerView.bottomAnchor],
        [_tabBar.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
        [_tabBar.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
        [_tabBar.heightAnchor constraintEqualToConstant:30],
    ]];
}

- (void)buildInputArea {
    _inputContainer = [[UIView alloc] init];
    _inputContainer.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:_inputContainer];

    _typeButton = [UIButton buttonWithType:UIButtonTypeSystem];
    [_typeButton setTitle:@"i32" forState:UIControlStateNormal];
    [_typeButton setTitleColor:[SYTheme accent] forState:UIControlStateNormal];
    _typeButton.titleLabel.font = [SYTheme captionFont];
    _typeButton.backgroundColor = [SYTheme bgTertiary];
    _typeButton.layer.cornerRadius = [SYTheme radiusSmall];
    _typeButton.translatesAutoresizingMaskIntoConstraints = NO;
    [_typeButton addTarget:self
                    action:@selector(typeTapped)
          forControlEvents:UIControlEventTouchUpInside];
    [_inputContainer addSubview:_typeButton];

    _inputField = [[UITextField alloc] init];
    _inputField.backgroundColor = [SYTheme bgTertiary];
    _inputField.textColor = [SYTheme textPrimary];
    _inputField.font = [SYTheme monoMedium];
    _inputField.layer.cornerRadius = [SYTheme radiusSmall];
    _inputField.leftView = [[UIView alloc] initWithFrame:CGRectMake(0, 0, 8, 0)];
    _inputField.leftViewMode = UITextFieldViewModeAlways;
    _inputField.autocorrectionType = UITextAutocorrectionTypeNo;
    _inputField.autocapitalizationType = UITextAutocapitalizationTypeNone;
    _inputField.returnKeyType = UIReturnKeyGo;
    _inputField.delegate = self;
    _inputField.translatesAutoresizingMaskIntoConstraints = NO;
    [_inputContainer addSubview:_inputField];

    _actionButton = [UIButton buttonWithType:UIButtonTypeSystem];
    _actionButton.backgroundColor = [SYTheme accent];
    _actionButton.layer.cornerRadius = [SYTheme radiusSmall];
    _actionButton.translatesAutoresizingMaskIntoConstraints = NO;
    [_actionButton addTarget:self
                      action:@selector(actionTapped)
            forControlEvents:UIControlEventTouchUpInside];
    [_inputContainer addSubview:_actionButton];

    [NSLayoutConstraint activateConstraints:@[
        [_inputContainer.topAnchor constraintEqualToAnchor:_tabBar.bottomAnchor constant:5],
        [_inputContainer.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor constant:8],
        [_inputContainer.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor
                                                       constant:-8],
        [_inputContainer.heightAnchor constraintEqualToConstant:32],
        [_typeButton.leadingAnchor constraintEqualToAnchor:_inputContainer.leadingAnchor],
        [_typeButton.centerYAnchor constraintEqualToAnchor:_inputContainer.centerYAnchor],
        [_typeButton.widthAnchor constraintEqualToConstant:34],
        [_typeButton.heightAnchor constraintEqualToConstant:28],
        [_inputField.leadingAnchor constraintEqualToAnchor:_typeButton.trailingAnchor constant:5],
        [_inputField.centerYAnchor constraintEqualToAnchor:_inputContainer.centerYAnchor],
        [_inputField.trailingAnchor constraintEqualToAnchor:_actionButton.leadingAnchor
                                                   constant:-5],
        [_inputField.heightAnchor constraintEqualToConstant:28],
        [_actionButton.trailingAnchor constraintEqualToAnchor:_inputContainer.trailingAnchor],
        [_actionButton.centerYAnchor constraintEqualToAnchor:_inputContainer.centerYAnchor],
        [_actionButton.widthAnchor constraintEqualToConstant:34],
        [_actionButton.heightAnchor constraintEqualToConstant:28],
    ]];
}

- (void)buildNarrowBar {
    _narrowBar = [[UIView alloc] init];
    _narrowBar.translatesAutoresizingMaskIntoConstraints = NO;
    _narrowBar.hidden = YES;
    [self.view addSubview:_narrowBar];

    NSArray *modes = @[ @"Changed", @"Unchanged", @"Inc", @"Dec", @"Reset" ];
    CGFloat x = 0;

    for (NSUInteger i = 0; i < modes.count; i++) {
        UIButton *btn = [UIButton buttonWithType:UIButtonTypeSystem];
        [btn setTitle:modes[i] forState:UIControlStateNormal];
        [btn setTitleColor:[SYTheme textSecondary] forState:UIControlStateNormal];
        btn.titleLabel.font = [UIFont systemFontOfSize:9 weight:UIFontWeightMedium];
        btn.backgroundColor = [SYTheme bgTertiary];
        btn.layer.cornerRadius = 4;
        btn.tag = i;
        [btn sizeToFit];
        btn.frame = CGRectMake(x, 0, btn.frame.size.width + 12, 22);
        [btn addTarget:self
                      action:@selector(narrowTapped:)
            forControlEvents:UIControlEventTouchUpInside];
        [_narrowBar addSubview:btn];
        x += btn.frame.size.width + 4;
    }

    [NSLayoutConstraint activateConstraints:@[
        [_narrowBar.topAnchor constraintEqualToAnchor:_inputContainer.bottomAnchor constant:4],
        [_narrowBar.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor constant:8],
        [_narrowBar.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor constant:-8],
        [_narrowBar.heightAnchor constraintEqualToConstant:22],
    ]];
}

- (void)buildTableView {
    _tableView = [[UITableView alloc] initWithFrame:CGRectZero style:UITableViewStylePlain];
    _tableView.backgroundColor = [UIColor clearColor];
    _tableView.separatorStyle = UITableViewCellSeparatorStyleNone;
    _tableView.delegate = self;
    _tableView.dataSource = self;
    _tableView.rowHeight = 52;
    _tableView.translatesAutoresizingMaskIntoConstraints = NO;
    [_tableView registerClass:[SYResultCell class] forCellReuseIdentifier:kCellID];
    [self.view addSubview:_tableView];

    [NSLayoutConstraint activateConstraints:@[
        [_tableView.topAnchor constraintEqualToAnchor:_narrowBar.bottomAnchor constant:4],
        [_tableView.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
        [_tableView.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
        [_tableView.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor],
    ]];
}

- (void)setupGestures {
    UIPanGestureRecognizer *drag =
        [[UIPanGestureRecognizer alloc] initWithTarget:self action:@selector(handleDrag:)];
    [_headerView addGestureRecognizer:drag];

    UILongPressGestureRecognizer *longPress =
        [[UILongPressGestureRecognizer alloc] initWithTarget:self
                                                      action:@selector(handleLongPress:)];
    longPress.minimumPressDuration = 0.5;
    [_tableView addGestureRecognizer:longPress];
}

#pragma mark - Tab

- (void)tabTapped:(UIButton *)sender {
    _currentTabIndex = sender.tag;
    [self updateForCurrentTab:YES];
    [_tableView reloadData];
}

- (void)updateForCurrentTab:(BOOL)animated {
    id<SYTabHandler> h = [self currentHandler];

    // Update tab indicator
    UIButton *btn = _tabButtons[_currentTabIndex];
    CGFloat dur = animated ? 0.25 : 0;
    [UIView animateWithDuration:dur
                          delay:0
         usingSpringWithDamping:0.8
          initialSpringVelocity:0
                        options:0
                     animations:^{
                         self.tabIndicator.frame =
                             CGRectMake(btn.frame.origin.x, 27, btn.frame.size.width, 2);
                     }
                     completion:nil];

    // Update button colors
    for (NSUInteger i = 0; i < _tabButtons.count; i++) {
        UIColor *c = (i == (NSUInteger)_currentTabIndex) ? [SYTheme accent] : [SYTheme textMuted];
        [_tabButtons[i] setImage:[SYTheme icon:[_handlers[i] tabIcon] size:11 color:c]
                        forState:UIControlStateNormal];
        [_tabButtons[i] setTitleColor:c forState:UIControlStateNormal];
    }

    // Update input
    _inputField.attributedPlaceholder = [[NSAttributedString alloc]
        initWithString:[h placeholder]
            attributes:@{NSForegroundColorAttributeName : [SYTheme textMuted]}];
    [_typeButton setTitle:[h typeLabel] forState:UIControlStateNormal];
    [_actionButton setImage:[SYTheme icon:[h actionIcon] size:13 color:[UIColor blackColor]]
                   forState:UIControlStateNormal];

    // Show narrow bar only for search in narrowing mode
    BOOL showNarrow = (_currentTabIndex == 0 && [self.searchHandler isNarrowing]);
    _narrowBar.hidden = !showNarrow;

    // Adjust row height
    if ([h respondsToSelector:@selector(rowHeight)]) {
        _tableView.rowHeight = [h rowHeight];
    } else {
        _tableView.rowHeight = 52;
    }
}

#pragma mark - Actions

- (void)closeTapped {
    [UIView animateWithDuration:0.2
        animations:^{
            [ShirayukiWindow shared].transform = CGAffineTransformMakeScale(0.9, 0.9);
            [ShirayukiWindow shared].alpha = 0;
        }
        completion:^(BOOL finished) {
            [[ShirayukiWindow shared] hide];
            [ShirayukiWindow shared].transform = CGAffineTransformIdentity;
            [ShirayukiWindow shared].alpha = 1;
        }];
}

- (void)typeTapped {
    if (_currentTabIndex == 0) {
        [self.searchHandler cycleType];
        [_typeButton setTitle:[self.searchHandler shortType] forState:UIControlStateNormal];
        [UIView animateWithDuration:0.12
            animations:^{
                self.typeButton.transform = CGAffineTransformMakeScale(1.2, 1.2);
            }
            completion:^(BOOL f) {
                [UIView animateWithDuration:0.08
                                 animations:^{
                                     self.typeButton.transform = CGAffineTransformIdentity;
                                 }];
            }];
    }
}

- (void)actionTapped {
    NSString *input = _inputField.text;
    [_inputField resignFirstResponder];

    UIImpactFeedbackGenerator *haptic =
        [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleLight];
    [haptic impactOccurred];

    [[self currentHandler] performAction:input ?: @""];
}

- (void)narrowTapped:(UIButton *)sender {
    static NSArray *modes = nil;
    if (!modes)
        modes = @[ @"changed", @"unchanged", @"increased", @"decreased" ];

    if (sender.tag < (NSInteger)modes.count) {
        [self.searchHandler narrow:modes[sender.tag]];
    } else {
        [self.searchHandler resetSearch];
    }
    [self updateForCurrentTab:NO];
}

#pragma mark - Drag

- (void)handleDrag:(UIPanGestureRecognizer *)gesture {
    UIWindow *window = [ShirayukiWindow shared];
    CGPoint translation = [gesture translationInView:window.superview];
    window.center = CGPointMake(window.center.x + translation.x, window.center.y + translation.y);
    [gesture setTranslation:CGPointZero inView:window.superview];
}

- (void)handleLongPress:(UILongPressGestureRecognizer *)gesture {
    if (gesture.state != UIGestureRecognizerStateBegan)
        return;

    CGPoint point = [gesture locationInView:_tableView];
    NSIndexPath *indexPath = [_tableView indexPathForRowAtPoint:point];
    if (!indexPath)
        return;

    id<SYTabHandler> h = [self currentHandler];
    if ([h respondsToSelector:@selector(didLongPressRow:)]) {
        [h didLongPressRow:indexPath.row];
    }
}

#pragma mark - Public

- (void)reloadTable {
    [_tableView reloadData];
    [self updateForCurrentTab:NO];
}

- (void)showModifyAlertForAddress:(uintptr_t)addr type:(NSString *)type {
    NSString *currentStr;
    if ([type isEqualToString:@"float"]) {
        float v = Memory::readValue<float>(addr);
        currentStr = [NSString stringWithFormat:@"%.3f", v];
    } else if ([type isEqualToString:@"double"]) {
        double v = Memory::readValue<double>(addr);
        currentStr = [NSString stringWithFormat:@"%.5f", v];
    } else if ([type isEqualToString:@"int64"]) {
        int64_t v = Memory::readValue<int64_t>(addr);
        currentStr = [NSString stringWithFormat:@"%lld", v];
    } else {
        int32_t v = Memory::readValue<int32_t>(addr);
        currentStr = [NSString stringWithFormat:@"%d", v];
    }

    UIAlertController *alert = [UIAlertController
        alertControllerWithTitle:[NSString stringWithFormat:@"0x%lX", addr]
                         message:[NSString stringWithFormat:@"Current: %@", currentStr]
                  preferredStyle:UIAlertControllerStyleAlert];

    [alert addTextFieldWithConfigurationHandler:^(UITextField *tf) {
        tf.text = currentStr;
        tf.font = [SYTheme monoMedium];
        tf.keyboardType = UIKeyboardTypeDecimalPad;
    }];

    [alert addAction:[UIAlertAction actionWithTitle:@"Write"
                                              style:UIAlertActionStyleDefault
                                            handler:^(UIAlertAction *a) {
                                                NSString *val = alert.textFields.firstObject.text;
                                                if ([type isEqualToString:@"float"]) {
                                                    float v = [val floatValue];
                                                    Memory::writeValue<float>(addr, v);
                                                } else if ([type isEqualToString:@"double"]) {
                                                    double v = [val doubleValue];
                                                    Memory::writeValue<double>(addr, v);
                                                } else if ([type isEqualToString:@"int64"]) {
                                                    int64_t v = [val longLongValue];
                                                    Memory::writeValue<int64_t>(addr, v);
                                                } else {
                                                    int32_t v = [val intValue];
                                                    Memory::writeValue<int32_t>(addr, v);
                                                }
                                                [SYToast show:@"Written" type:SYToastSuccess];
                                                [self reloadTable];
                                            }]];

    [alert addAction:[UIAlertAction actionWithTitle:@"Freeze"
                                              style:UIAlertActionStyleDefault
                                            handler:^(UIAlertAction *a) {
                                                NSString *val = alert.textFields.firstObject.text;
                                                NSString *cmd = [NSString
                                                    stringWithFormat:@"0x%lX %@", addr, val];
                                                [self.freezeHandler performAction:cmd];
                                            }]];

    [alert addAction:[UIAlertAction actionWithTitle:@"Watch"
                                              style:UIAlertActionStyleDefault
                                            handler:^(UIAlertAction *a) {
                                                NSString *cmd =
                                                    [NSString stringWithFormat:@"0x%lX", addr];
                                                [self.watchHandler performAction:cmd];
                                            }]];

    [alert addAction:[UIAlertAction actionWithTitle:@"Cancel"
                                              style:UIAlertActionStyleCancel
                                            handler:nil]];
    [self presentViewController:alert animated:YES completion:nil];
}

#pragma mark - UITableView

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section {
    return [[self currentHandler] numberOfRows];
}

- (UITableViewCell *)tableView:(UITableView *)tableView
         cellForRowAtIndexPath:(NSIndexPath *)indexPath {
    return [[self currentHandler] cellForRow:indexPath.row inTableView:tableView];
}

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath {
    id<SYTabHandler> h = [self currentHandler];
    if ([h respondsToSelector:@selector(didSelectRow:)]) {
        [h didSelectRow:indexPath.row];
    }
}

- (BOOL)tableView:(UITableView *)tableView canEditRowAtIndexPath:(NSIndexPath *)indexPath {
    id<SYTabHandler> h = [self currentHandler];
    if ([h respondsToSelector:@selector(canDeleteRow:)]) {
        return [h canDeleteRow:indexPath.row];
    }
    return NO;
}

- (void)tableView:(UITableView *)tableView
    commitEditingStyle:(UITableViewCellEditingStyle)style
     forRowAtIndexPath:(NSIndexPath *)indexPath {
    if (style != UITableViewCellEditingStyleDelete)
        return;
    id<SYTabHandler> h = [self currentHandler];
    if ([h respondsToSelector:@selector(deleteRow:)]) {
        [h deleteRow:indexPath.row];
        [tableView deleteRowsAtIndexPaths:@[ indexPath ]
                         withRowAnimation:UITableViewRowAnimationFade];
    }
}

#pragma mark - UITextField

- (BOOL)textFieldShouldReturn:(UITextField *)textField {
    [self actionTapped];
    return YES;
}

@end
