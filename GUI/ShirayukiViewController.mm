#import "ShirayukiViewController.h"
#import "Freeze.hpp"
#import "Handlers/SYDumpHandler.h"
#import "Handlers/SYFreezeHandler.h"
#import "Handlers/SYPatchHandler.h"
#import "Handlers/SYPointerHandler.h"
#import "Handlers/SYSearchHandler.h"
#import "Handlers/SYWatchHandler.h"
#import "SYInput.h"
#import "SYResultCell.h"
#import "SYTabHandler.h"
#import "SYTheme.h"
#import "SYToast.h"
#import "SYValueTypeUtil.h"
#import "Session.hpp"
#import "ShirayukiMemory.hpp"
#import "ShirayukiWindow.h"

using namespace Shirayuki;

static NSString *const kCellID = @"SYCell";

static NSString *const kHistoryCellID = @"SYHistoryCell";

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
@property (nonatomic, strong) UITableView *historyDropdown;
@property (nonatomic, strong) UIView *historyOverlay;

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

    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(appDidEnterBackground)
                                                 name:UIApplicationDidEnterBackgroundNotification
                                               object:nil];
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
}

- (void)appDidEnterBackground {
    [self autoSaveSession];
}

- (void)autoSaveSession {
    // Collect current state into a Session and save
    Shirayuki::Session session;

    // Bundle ID of host app
    NSString *bundleId = [[NSBundle mainBundle] bundleIdentifier] ?: @"unknown";
    session.targetBundle = [bundleId UTF8String];
    session.name = "autosave";

    // Freeze entries
    session.freezeEntries = Shirayuki::FreezeManager::shared().entries();

    // Patches from handler
    for (NSDictionary *p in self.patchHandler.allPatches) {
        Shirayuki::Session::PatchRecord pr;
        pr.address = [p[@"address"] unsignedLongLongValue];
        pr.patchHex = [p[@"hex"] UTF8String] ?: "";
        pr.originalHex = [p[@"original"] UTF8String] ?: "";
        pr.label = [p[@"label"] UTF8String] ?: "";
        pr.autoApply = NO;
        session.patches.push_back(pr);
    }

    // Search history
    for (NSString *s in [self.searchHandler searchHistory]) {
        session.searchHistory.push_back([s UTF8String]);
    }

    std::string path = Shirayuki::SessionManager::autoSavePath(session.targetBundle);
    Shirayuki::SessionManager::save(session, path);
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

/// Look a handler up by class rather than by position. The index-based accessors
/// this replaces were commented "safe against reordering" while being exactly
/// the opposite: reordering `_handlers` silently repointed every one of them.
- (id)handlerOfClass:(Class)cls {
    for (id<SYTabHandler> handler in _handlers) {
        if ([handler isKindOfClass:cls])
            return handler;
    }
    return nil;
}

- (SYSearchHandler *)searchHandler {
    return [self handlerOfClass:[SYSearchHandler class]];
}
- (SYPatchHandler *)patchHandler {
    return [self handlerOfClass:[SYPatchHandler class]];
}
- (SYFreezeHandler *)freezeHandler {
    return [self handlerOfClass:[SYFreezeHandler class]];
}
- (SYWatchHandler *)watchHandler {
    return [self handlerOfClass:[SYWatchHandler class]];
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
    UILongPressGestureRecognizer *actionLongPress =
        [[UILongPressGestureRecognizer alloc] initWithTarget:self
                                                      action:@selector(actionLongPressed:)];
    actionLongPress.minimumPressDuration = 0.5;
    [_actionButton addGestureRecognizer:actionLongPress];
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

    NSArray<NSString *> *titles = [self narrowButtonTitles];
    CGFloat x = 0;

    for (NSUInteger i = 0; i < titles.count; i++) {
        UIButton *btn = [UIButton buttonWithType:UIButtonTypeSystem];
        [btn setTitle:titles[i] forState:UIControlStateNormal];
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

    BOOL showNarrow = NO;
    if ([h respondsToSelector:@selector(showsNarrowBar)])
        showNarrow = [h showsNarrowBar];
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
    id<SYTabHandler> h = [self currentHandler];
    if (![h respondsToSelector:@selector(cycleType)])
        return;

    [h cycleType];
    [_typeButton setTitle:[h typeLabel] forState:UIControlStateNormal];
    // The type change can also change the placeholder and narrow-bar visibility.
    [self updateForCurrentTab:NO];
    [_tableView reloadData];

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

- (void)actionTapped {
    NSString *input = _inputField.text;
    [_inputField resignFirstResponder];

    UIImpactFeedbackGenerator *haptic =
        [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleLight];
    [haptic impactOccurred];

    [[self currentHandler] performAction:input ?: @""];
}

- (void)actionLongPressed:(UILongPressGestureRecognizer *)gesture {
    if (gesture.state != UIGestureRecognizerStateBegan)
        return;

    id<SYTabHandler> h = [self currentHandler];
    if (![h respondsToSelector:@selector(actionButtonMenuActions)])
        return;

    [self presentActions:[h actionButtonMenuActions]
                   title:[NSString stringWithFormat:@"%@ Actions", [h tabTitle]]];
}

/// Present handler-supplied actions as an action sheet, adding Cancel. Keeps the
/// per-tab menu contents with the tab instead of in a chain of index checks here.
- (void)presentActions:(NSArray<UIAlertAction *> *)actions title:(NSString *)title {
    if (!actions.count)
        return;

    UIAlertController *sheet =
        [UIAlertController alertControllerWithTitle:title
                                            message:nil
                                     preferredStyle:UIAlertControllerStyleActionSheet];
    for (UIAlertAction *action in actions)
        [sheet addAction:action];
    [sheet addAction:[UIAlertAction actionWithTitle:@"Cancel"
                                              style:UIAlertActionStyleCancel
                                            handler:nil]];
    [self presentViewController:sheet animated:YES completion:nil];
}

- (void)showBatchModifyAlert {
    UIAlertController *alert =
        [UIAlertController alertControllerWithTitle:@"Batch Modify"
                                            message:@"Set all results to value"
                                     preferredStyle:UIAlertControllerStyleAlert];
    [alert addTextFieldWithConfigurationHandler:^(UITextField *tf) {
        tf.placeholder = @"value";
        tf.keyboardType = UIKeyboardTypeDecimalPad;
        tf.font = [SYTheme monoMedium];
    }];
    [alert addAction:[UIAlertAction actionWithTitle:@"Apply"
                                              style:UIAlertActionStyleDefault
                                            handler:^(UIAlertAction *a) {
                                                NSString *val = alert.textFields.firstObject.text;
                                                if (val.length)
                                                    [self.searchHandler batchModify:val];
                                            }]];
    [alert addAction:[UIAlertAction actionWithTitle:@"Cancel"
                                              style:UIAlertActionStyleCancel
                                            handler:nil]];
    [self presentViewController:alert animated:YES completion:nil];
}

/// Narrow-bar buttons, in tag order. Titles and the filters they map to used to
/// live in two separate arrays in two separate methods, coupled only by index and
/// by strings that had to match what the handler compared against.
+ (NSArray<NSDictionary *> *)narrowButtons {
    static NSArray<NSDictionary *> *buttons = nil;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        buttons = @[
            @{@"title" : @"Changed",
              @"filter" : @(SYNarrowChanged)},
            @{@"title" : @"Unchanged",
              @"filter" : @(SYNarrowUnchanged)},
            @{@"title" : @"Inc",
              @"filter" : @(SYNarrowIncreased)},
            @{@"title" : @"Dec",
              @"filter" : @(SYNarrowDecreased)},
            @{@"title" : @">",
              @"filter" : @(SYNarrowGreaterThan)},
            @{@"title" : @"<",
              @"filter" : @(SYNarrowLessThan)},
            @{@"title" : @"Reset"}, // no filter — resets the search
        ];
    });
    return buttons;
}

- (NSArray<NSString *> *)narrowButtonTitles {
    NSMutableArray<NSString *> *titles = [NSMutableArray array];
    for (NSDictionary *b in [[self class] narrowButtons])
        [titles addObject:b[@"title"]];
    return titles;
}

- (void)confirmAction:(NSString *)title
              message:(NSString *)message
          confirmVerb:(NSString *)verb
             onAccept:(void (^)(void))onAccept {
    if (!onAccept)
        return;

    UIAlertController *alert =
        [UIAlertController alertControllerWithTitle:title
                                            message:message
                                     preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:verb.length ? verb : @"Confirm"
                                              style:UIAlertActionStyleDestructive
                                            handler:^(UIAlertAction *a) {
                                                onAccept();
                                            }]];
    [alert addAction:[UIAlertAction actionWithTitle:@"Cancel"
                                              style:UIAlertActionStyleCancel
                                            handler:nil]];
    [self presentViewController:alert animated:YES completion:nil];
}

- (void)narrowTapped:(UIButton *)sender {
    NSArray<NSDictionary *> *buttons = [[self class] narrowButtons];
    if (sender.tag < 0 || (NSUInteger)sender.tag >= buttons.count)
        return;

    NSNumber *filter = buttons[(NSUInteger)sender.tag][@"filter"];
    if (filter) {
        // The comparison filters need a value; the input field supplies it.
        [self.searchHandler narrowWithFilter:(SYNarrowFilter)filter.integerValue
                                       input:self.inputField.text];
    } else {
        [self.searchHandler resetSearch];
    }
    [self updateForCurrentTab:NO];
}

#pragma mark - Search history dropdown

/// Inputs the current tab offers as history, or nil when it offers none.
- (NSArray<NSString *> *)currentInputHistory {
    id<SYTabHandler> h = [self currentHandler];
    if (![h respondsToSelector:@selector(showsInputHistory)] || ![h showsInputHistory])
        return nil;
    if (![h respondsToSelector:@selector(inputHistory)])
        return nil;
    return [h inputHistory];
}

- (void)showSearchHistory {
    NSArray<NSString *> *history = [self currentInputHistory];
    if (!history.count)
        return;

    if (_historyDropdown) {
        [_historyDropdown reloadData];
        _historyOverlay.hidden = NO;
        _historyDropdown.hidden = NO;
        return;
    }

    _historyOverlay = [[UIView alloc] init];
    _historyOverlay.translatesAutoresizingMaskIntoConstraints = NO;
    _historyOverlay.backgroundColor = [UIColor clearColor];
    UITapGestureRecognizer *tap =
        [[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(dismissHistory)];
    [_historyOverlay addGestureRecognizer:tap];
    [self.view addSubview:_historyOverlay];

    _historyDropdown = [[UITableView alloc] initWithFrame:CGRectZero style:UITableViewStylePlain];
    _historyDropdown.backgroundColor = [SYTheme bgSecondary];
    _historyDropdown.layer.cornerRadius = [SYTheme radiusSmall];
    _historyDropdown.layer.borderColor = [SYTheme accentDim].CGColor;
    _historyDropdown.layer.borderWidth = 0.5;
    _historyDropdown.separatorStyle = UITableViewCellSeparatorStyleSingleLine;
    _historyDropdown.separatorColor = [SYTheme accentDim];
    _historyDropdown.rowHeight = 30;
    _historyDropdown.translatesAutoresizingMaskIntoConstraints = NO;
    _historyDropdown.delegate = self;
    _historyDropdown.dataSource = self;
    [_historyDropdown registerClass:[UITableViewCell class] forCellReuseIdentifier:kHistoryCellID];
    [self.view addSubview:_historyDropdown];

    NSUInteger visibleRows = MIN((NSUInteger)5, history.count);
    CGFloat dropHeight = visibleRows * 30.0;

    [NSLayoutConstraint activateConstraints:@[
        [_historyOverlay.topAnchor constraintEqualToAnchor:self.view.topAnchor],
        [_historyOverlay.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
        [_historyOverlay.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
        [_historyOverlay.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor],
        [_historyDropdown.topAnchor constraintEqualToAnchor:_inputContainer.bottomAnchor
                                                   constant:2],
        [_historyDropdown.leadingAnchor constraintEqualToAnchor:_inputField.leadingAnchor],
        [_historyDropdown.trailingAnchor constraintEqualToAnchor:_inputField.trailingAnchor],
        [_historyDropdown.heightAnchor constraintEqualToConstant:dropHeight],
    ]];

    _historyDropdown.alpha = 0;
    [UIView animateWithDuration:0.15
                     animations:^{
                         self.historyDropdown.alpha = 1;
                     }];
}

- (void)dismissHistory {
    [UIView animateWithDuration:0.1
        animations:^{
            self.historyDropdown.alpha = 0;
        }
        completion:^(BOOL f) {
            self.historyOverlay.hidden = YES;
            self.historyDropdown.hidden = YES;
        }];
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

    // A tab with a per-row menu supplies the actions itself; the view controller
    // no longer needs to know that row menus are a freeze-tab thing.
    if ([h respondsToSelector:@selector(contextActionsForRow:)]) {
        NSArray<UIAlertAction *> *actions = [h contextActionsForRow:indexPath.row];
        if (actions.count) {
            [self presentActions:actions
                           title:[NSString stringWithFormat:@"%@ Options", [h tabTitle]]];
            return;
        }
    }

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
    const size_t valSize = SYValueTypeUtil::sizeOfTag(type);

    // Read first and bail out if it fails. Displaying a zero-filled buffer for an
    // unreadable address would prefill the edit field with "0", and accepting
    // that prefill writes a real zero into memory — a failed read turning into
    // actual corruption.
    uint8_t buf[kMaxValueSize] = {};
    if (Memory::read(addr, buf, valSize) != Status::Success) {
        [SYToast show:[NSString stringWithFormat:@"Cannot read 0x%llX", (unsigned long long)addr]
                 type:SYToastError];
        return;
    }

    // Plain (re-parseable) form for the edit field, annotated form for the label.
    NSString *editStr = SYValueTypeUtil::formatValue(buf, type);
    NSString *displayStr = SYValueTypeUtil::displayValue(buf, type);

    UIAlertController *alert = [UIAlertController
        alertControllerWithTitle:SYFormatAddress(addr)
                         message:[NSString stringWithFormat:@"Current: %@ (%@)", displayStr, type]
                  preferredStyle:UIAlertControllerStyleAlert];

    [alert addTextFieldWithConfigurationHandler:^(UITextField *tf) {
        tf.text = editStr;
        tf.font = [SYTheme monoMedium];
        // Not DecimalPad: negative values, hex entry and exponents all need the
        // full keyboard, and every type is parsed through ValueFormat anyway.
        tf.keyboardType = UIKeyboardTypeASCIICapable;
        tf.autocorrectionType = UITextAutocorrectionTypeNo;
        tf.autocapitalizationType = UITextAutocapitalizationTypeNone;
        tf.clearButtonMode = UITextFieldViewModeAlways;
    }];

    [alert
        addAction:[UIAlertAction
                      actionWithTitle:@"Write"
                                style:UIAlertActionStyleDefault
                              handler:^(UIAlertAction *a) {
                                  NSString *val = alert.textFields.firstObject.text;
                                  uint8_t out[kMaxValueSize] = {};
                                  const size_t n = SYValueTypeUtil::parseValue(val, type, out);
                                  if (n == 0) {
                                      [SYToast
                                          show:[NSString stringWithFormat:@"Invalid %@ value", type]
                                          type:SYToastError];
                                      return;
                                  }
                                  if (Memory::write(addr, out, n) != Status::Success) {
                                      [SYToast show:@"Write failed" type:SYToastError];
                                      return;
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
    if (tableView == _historyDropdown) {
        return (NSInteger)[self currentInputHistory].count;
    }
    return [[self currentHandler] numberOfRows];
}

- (UITableViewCell *)tableView:(UITableView *)tableView
         cellForRowAtIndexPath:(NSIndexPath *)indexPath {
    if (tableView == _historyDropdown) {
        UITableViewCell *cell = [tableView dequeueReusableCellWithIdentifier:kHistoryCellID
                                                                forIndexPath:indexPath];
        NSArray<NSString *> *history = [self currentInputHistory];
        cell.textLabel.text =
            (indexPath.row < (NSInteger)history.count) ? history[indexPath.row] : @"";
        cell.textLabel.font = [SYTheme monoMedium];
        cell.textLabel.textColor = [SYTheme textPrimary];
        cell.backgroundColor = [UIColor clearColor];
        return cell;
    }
    return [[self currentHandler] cellForRow:indexPath.row inTableView:tableView];
}

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath {
    if (tableView == _historyDropdown) {
        NSArray<NSString *> *history = [self currentInputHistory];
        if (indexPath.row < (NSInteger)history.count)
            _inputField.text = history[indexPath.row];
        [self dismissHistory];
        [_inputField resignFirstResponder];
        return;
    }
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
    [self dismissHistory];
    [self actionTapped];
    return YES;
}

- (void)textFieldDidBeginEditing:(UITextField *)textField {
    [self showSearchHistory];
}

- (void)textFieldDidEndEditing:(UITextField *)textField {
    [self dismissHistory];
}

@end
