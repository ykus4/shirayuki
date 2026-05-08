#import "ShirayukiViewController.h"
#import "ShirayukiWindow.h"
#import "SYTheme.h"
#import "SYResultCell.h"
#import "ShirayukiMemory.hpp"
#import "Freeze.hpp"
#import "PointerScan.hpp"

using namespace Shirayuki;

typedef NS_ENUM(NSInteger, SYTab) {
    SYTabSearch = 0,
    SYTabPatches,
    SYTabFreeze,
    SYTabPointerScan,
    SYTabDump
};

static NSString *const kCellID = @"SYCell";

@interface ShirayukiViewController () <UITableViewDelegate, UITableViewDataSource, UITextFieldDelegate>
// Header
@property (nonatomic, strong) UIView *headerView;
@property (nonatomic, strong) UILabel *titleLabel;
@property (nonatomic, strong) UIButton *closeButton;
@property (nonatomic, strong) UIButton *minimizeButton;

// Tabs
@property (nonatomic, strong) UIScrollView *tabBar;
@property (nonatomic, strong) NSArray<UIButton *> *tabButtons;
@property (nonatomic, strong) UIView *tabIndicator;

// Input area
@property (nonatomic, strong) UIView *inputContainer;
@property (nonatomic, strong) UITextField *inputField;
@property (nonatomic, strong) UIButton *actionButton;
@property (nonatomic, strong) UIButton *typeButton; // search type selector

// Status
@property (nonatomic, strong) UIView *statusBar;
@property (nonatomic, strong) UIImageView *statusIcon;
@property (nonatomic, strong) UILabel *statusLabel;
@property (nonatomic, strong) UIActivityIndicatorView *spinner;

// Content
@property (nonatomic, strong) UITableView *tableView;

// State
@property (nonatomic, assign) SYTab currentTab;
@property (nonatomic, strong) NSMutableArray<NSNumber *> *searchResults;
@property (nonatomic, strong) NSMutableArray<NSDictionary *> *patchEntries;
@property (nonatomic, strong) NSMutableArray<NSMutableDictionary *> *freezeEntries;
@property (nonatomic, strong) NSMutableArray<NSDictionary *> *pointerResults;
@property (nonatomic, strong) NSString *searchType;
@property (nonatomic, assign) BOOL isSearching;
@end

@implementation ShirayukiViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = [SYTheme bgPrimary];
    self.view.layer.cornerRadius = [SYTheme radiusLarge];
    self.view.clipsToBounds = YES;

    // Add subtle border
    self.view.layer.borderColor = [SYTheme accentDim].CGColor;
    self.view.layer.borderWidth = 0.5;

    _searchResults = [NSMutableArray new];
    _patchEntries = [NSMutableArray new];
    _freezeEntries = [NSMutableArray new];
    _pointerResults = [NSMutableArray new];
    _currentTab = SYTabSearch;
    _searchType = @"int32";

    [self buildHeader];
    [self buildTabBar];
    [self buildInputArea];
    [self buildStatusBar];
    [self buildTableView];
    [self setupDrag];
    [self updateTabSelection:NO];
}

#pragma mark - Build UI

- (void)buildHeader {
    _headerView = [[UIView alloc] init];
    _headerView.backgroundColor = [SYTheme bgSecondary];
    _headerView.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:_headerView];

    // Snowflake icon + title
    UIImageView *logo = [[UIImageView alloc] initWithImage:[SYTheme icon:@"snowflake" size:16 color:[SYTheme accent]]];
    logo.translatesAutoresizingMaskIntoConstraints = NO;
    [_headerView addSubview:logo];

    _titleLabel = [[UILabel alloc] init];
    _titleLabel.text = @"Shirayuki";
    _titleLabel.font = [SYTheme titleFont];
    _titleLabel.textColor = [SYTheme textPrimary];
    _titleLabel.translatesAutoresizingMaskIntoConstraints = NO;
    [_headerView addSubview:_titleLabel];

    _minimizeButton = [UIButton buttonWithType:UIButtonTypeSystem];
    [_minimizeButton setImage:[SYTheme icon:@"minus" size:12 color:[SYTheme textSecondary]] forState:UIControlStateNormal];
    _minimizeButton.translatesAutoresizingMaskIntoConstraints = NO;
    [_minimizeButton addTarget:self action:@selector(minimizeTapped) forControlEvents:UIControlEventTouchUpInside];
    [_headerView addSubview:_minimizeButton];

    _closeButton = [UIButton buttonWithType:UIButtonTypeSystem];
    [_closeButton setImage:[SYTheme icon:@"xmark" size:12 color:[SYTheme danger]] forState:UIControlStateNormal];
    _closeButton.translatesAutoresizingMaskIntoConstraints = NO;
    [_closeButton addTarget:self action:@selector(closeTapped) forControlEvents:UIControlEventTouchUpInside];
    [_headerView addSubview:_closeButton];

    [NSLayoutConstraint activateConstraints:@[
        [_headerView.topAnchor constraintEqualToAnchor:self.view.topAnchor],
        [_headerView.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
        [_headerView.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
        [_headerView.heightAnchor constraintEqualToConstant:36],

        [logo.leadingAnchor constraintEqualToAnchor:_headerView.leadingAnchor constant:12],
        [logo.centerYAnchor constraintEqualToAnchor:_headerView.centerYAnchor],
        [logo.widthAnchor constraintEqualToConstant:16],
        [logo.heightAnchor constraintEqualToConstant:16],

        [_titleLabel.leadingAnchor constraintEqualToAnchor:logo.trailingAnchor constant:6],
        [_titleLabel.centerYAnchor constraintEqualToAnchor:_headerView.centerYAnchor],

        [_closeButton.trailingAnchor constraintEqualToAnchor:_headerView.trailingAnchor constant:-8],
        [_closeButton.centerYAnchor constraintEqualToAnchor:_headerView.centerYAnchor],
        [_closeButton.widthAnchor constraintEqualToConstant:28],
        [_closeButton.heightAnchor constraintEqualToConstant:28],

        [_minimizeButton.trailingAnchor constraintEqualToAnchor:_closeButton.leadingAnchor constant:-4],
        [_minimizeButton.centerYAnchor constraintEqualToAnchor:_headerView.centerYAnchor],
        [_minimizeButton.widthAnchor constraintEqualToConstant:28],
        [_minimizeButton.heightAnchor constraintEqualToConstant:28],
    ]];
}

- (void)buildTabBar {
    _tabBar = [[UIScrollView alloc] init];
    _tabBar.showsHorizontalScrollIndicator = NO;
    _tabBar.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:_tabBar];

    NSArray *tabDefs = @[
        @{@"icon": @"magnifyingglass", @"title": @"Search"},
        @{@"icon": @"wrench.and.screwdriver", @"title": @"Patch"},
        @{@"icon": @"lock.fill", @"title": @"Freeze"},
        @{@"icon": @"arrow.triangle.branch", @"title": @"Ptr"},
        @{@"icon": @"doc.text", @"title": @"Dump"},
    ];

    NSMutableArray *buttons = [NSMutableArray new];
    CGFloat x = 8;

    for (NSDictionary *def in tabDefs) {
        UIButton *btn = [UIButton buttonWithType:UIButtonTypeSystem];
        UIImage *icon = [SYTheme icon:def[@"icon"] size:12 color:[SYTheme textMuted]];
        [btn setImage:icon forState:UIControlStateNormal];
        [btn setTitle:[NSString stringWithFormat:@" %@", def[@"title"]] forState:UIControlStateNormal];
        [btn setTitleColor:[SYTheme textMuted] forState:UIControlStateNormal];
        btn.titleLabel.font = [SYTheme captionFont];
        btn.tag = buttons.count;
        [btn sizeToFit];
        btn.frame = CGRectMake(x, 4, btn.frame.size.width + 16, 26);
        [btn addTarget:self action:@selector(tabTapped:) forControlEvents:UIControlEventTouchUpInside];
        [_tabBar addSubview:btn];
        [buttons addObject:btn];
        x += btn.frame.size.width + 4;
    }
    _tabBar.contentSize = CGSizeMake(x, 34);
    _tabButtons = buttons;

    // Tab indicator line
    _tabIndicator = [[UIView alloc] initWithFrame:CGRectMake(8, 30, 50, 2)];
    _tabIndicator.backgroundColor = [SYTheme accent];
    _tabIndicator.layer.cornerRadius = 1;
    [_tabBar addSubview:_tabIndicator];

    [NSLayoutConstraint activateConstraints:@[
        [_tabBar.topAnchor constraintEqualToAnchor:_headerView.bottomAnchor],
        [_tabBar.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
        [_tabBar.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
        [_tabBar.heightAnchor constraintEqualToConstant:34],
    ]];
}

- (void)buildInputArea {
    _inputContainer = [[UIView alloc] init];
    _inputContainer.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:_inputContainer];

    // Type selector button
    _typeButton = [UIButton buttonWithType:UIButtonTypeSystem];
    [_typeButton setTitle:@"i32" forState:UIControlStateNormal];
    [_typeButton setTitleColor:[SYTheme accent] forState:UIControlStateNormal];
    _typeButton.titleLabel.font = [SYTheme captionFont];
    _typeButton.backgroundColor = [SYTheme bgTertiary];
    _typeButton.layer.cornerRadius = [SYTheme radiusSmall];
    _typeButton.translatesAutoresizingMaskIntoConstraints = NO;
    [_typeButton addTarget:self action:@selector(typeTapped) forControlEvents:UIControlEventTouchUpInside];
    [_inputContainer addSubview:_typeButton];

    // Input field
    _inputField = [[UITextField alloc] init];
    _inputField.backgroundColor = [SYTheme bgTertiary];
    _inputField.textColor = [SYTheme textPrimary];
    _inputField.font = [SYTheme monoMedium];
    _inputField.layer.cornerRadius = [SYTheme radiusSmall];
    _inputField.leftView = [[UIView alloc] initWithFrame:CGRectMake(0, 0, 10, 0)];
    _inputField.leftViewMode = UITextFieldViewModeAlways;
    _inputField.attributedPlaceholder = [[NSAttributedString alloc]
        initWithString:@"Value or pattern..."
        attributes:@{NSForegroundColorAttributeName: [SYTheme textMuted]}];
    _inputField.autocorrectionType = UITextAutocorrectionTypeNo;
    _inputField.autocapitalizationType = UITextAutocapitalizationTypeNone;
    _inputField.returnKeyType = UIReturnKeySearch;
    _inputField.delegate = self;
    _inputField.translatesAutoresizingMaskIntoConstraints = NO;
    [_inputContainer addSubview:_inputField];

    // Action button
    _actionButton = [UIButton buttonWithType:UIButtonTypeSystem];
    _actionButton.backgroundColor = [SYTheme accent];
    _actionButton.layer.cornerRadius = [SYTheme radiusSmall];
    _actionButton.translatesAutoresizingMaskIntoConstraints = NO;
    [_actionButton setImage:[SYTheme icon:@"play.fill" size:14 color:[UIColor blackColor]] forState:UIControlStateNormal];
    [_actionButton addTarget:self action:@selector(actionTapped) forControlEvents:UIControlEventTouchUpInside];
    [_inputContainer addSubview:_actionButton];

    [NSLayoutConstraint activateConstraints:@[
        [_inputContainer.topAnchor constraintEqualToAnchor:_tabBar.bottomAnchor constant:6],
        [_inputContainer.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor constant:8],
        [_inputContainer.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor constant:-8],
        [_inputContainer.heightAnchor constraintEqualToConstant:34],

        [_typeButton.leadingAnchor constraintEqualToAnchor:_inputContainer.leadingAnchor],
        [_typeButton.centerYAnchor constraintEqualToAnchor:_inputContainer.centerYAnchor],
        [_typeButton.widthAnchor constraintEqualToConstant:36],
        [_typeButton.heightAnchor constraintEqualToConstant:30],

        [_inputField.leadingAnchor constraintEqualToAnchor:_typeButton.trailingAnchor constant:6],
        [_inputField.centerYAnchor constraintEqualToAnchor:_inputContainer.centerYAnchor],
        [_inputField.trailingAnchor constraintEqualToAnchor:_actionButton.leadingAnchor constant:-6],
        [_inputField.heightAnchor constraintEqualToConstant:30],

        [_actionButton.trailingAnchor constraintEqualToAnchor:_inputContainer.trailingAnchor],
        [_actionButton.centerYAnchor constraintEqualToAnchor:_inputContainer.centerYAnchor],
        [_actionButton.widthAnchor constraintEqualToConstant:36],
        [_actionButton.heightAnchor constraintEqualToConstant:30],
    ]];
}

- (void)buildStatusBar {
    _statusBar = [[UIView alloc] init];
    _statusBar.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:_statusBar];

    _statusIcon = [[UIImageView alloc] initWithImage:[SYTheme icon:@"checkmark.circle.fill" size:10 color:[SYTheme success]]];
    _statusIcon.translatesAutoresizingMaskIntoConstraints = NO;
    [_statusBar addSubview:_statusIcon];

    _statusLabel = [[UILabel alloc] init];
    _statusLabel.text = @"Ready";
    _statusLabel.font = [SYTheme captionFont];
    _statusLabel.textColor = [SYTheme textMuted];
    _statusLabel.translatesAutoresizingMaskIntoConstraints = NO;
    [_statusBar addSubview:_statusLabel];

    _spinner = [[UIActivityIndicatorView alloc] initWithActivityIndicatorStyle:UIActivityIndicatorViewStyleMedium];
    _spinner.color = [SYTheme accent];
    _spinner.hidesWhenStopped = YES;
    _spinner.transform = CGAffineTransformMakeScale(0.6, 0.6);
    _spinner.translatesAutoresizingMaskIntoConstraints = NO;
    [_statusBar addSubview:_spinner];

    [NSLayoutConstraint activateConstraints:@[
        [_statusBar.topAnchor constraintEqualToAnchor:_inputContainer.bottomAnchor constant:4],
        [_statusBar.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor constant:12],
        [_statusBar.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor constant:-12],
        [_statusBar.heightAnchor constraintEqualToConstant:16],

        [_statusIcon.leadingAnchor constraintEqualToAnchor:_statusBar.leadingAnchor],
        [_statusIcon.centerYAnchor constraintEqualToAnchor:_statusBar.centerYAnchor],
        [_statusIcon.widthAnchor constraintEqualToConstant:10],

        [_statusLabel.leadingAnchor constraintEqualToAnchor:_statusIcon.trailingAnchor constant:4],
        [_statusLabel.centerYAnchor constraintEqualToAnchor:_statusBar.centerYAnchor],

        [_spinner.trailingAnchor constraintEqualToAnchor:_statusBar.trailingAnchor],
        [_spinner.centerYAnchor constraintEqualToAnchor:_statusBar.centerYAnchor],
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
        [_tableView.topAnchor constraintEqualToAnchor:_statusBar.bottomAnchor constant:4],
        [_tableView.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
        [_tableView.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
        [_tableView.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor],
    ]];
}

- (void)setupDrag {
    UIPanGestureRecognizer *pan = [[UIPanGestureRecognizer alloc] initWithTarget:self action:@selector(handleDrag:)];
    [_headerView addGestureRecognizer:pan];
}

#pragma mark - Drag

- (void)handleDrag:(UIPanGestureRecognizer *)gesture {
    UIWindow *window = [ShirayukiWindow shared];
    CGPoint translation = [gesture translationInView:window.superview];
    window.center = CGPointMake(window.center.x + translation.x,
                                window.center.y + translation.y);
    [gesture setTranslation:CGPointZero inView:window.superview];
}

#pragma mark - Tab Management

- (void)tabTapped:(UIButton *)sender {
    _currentTab = (SYTab)sender.tag;
    [self updateTabSelection:YES];
    [self updateInputPlaceholder];
    [_tableView reloadData];
}

- (void)updateTabSelection:(BOOL)animated {
    NSArray *icons = @[@"magnifyingglass", @"wrench.and.screwdriver", @"lock.fill",
                       @"arrow.triangle.branch", @"doc.text"];

    for (NSUInteger i = 0; i < _tabButtons.count; i++) {
        UIButton *btn = _tabButtons[i];
        BOOL selected = (i == (NSUInteger)_currentTab);
        UIColor *color = selected ? [SYTheme accent] : [SYTheme textMuted];
        [btn setImage:[SYTheme icon:icons[i] size:12 color:color] forState:UIControlStateNormal];
        [btn setTitleColor:color forState:UIControlStateNormal];
    }

    // Animate indicator
    UIButton *selectedBtn = _tabButtons[_currentTab];
    CGFloat duration = animated ? 0.25 : 0;
    [UIView animateWithDuration:duration delay:0 usingSpringWithDamping:0.8
          initialSpringVelocity:0 options:0 animations:^{
        self.tabIndicator.frame = CGRectMake(
            selectedBtn.frame.origin.x,
            30,
            selectedBtn.frame.size.width,
            2
        );
    } completion:nil];
}

- (void)updateInputPlaceholder {
    NSArray *placeholders = @[
        @"Value, hex pattern, or string...",
        @"0xADDR HEXBYTES",
        @"0xADDR VALUE",
        @"0xTARGET_ADDR",
        @"0xADDR [length]"
    ];
    NSArray *typeLabels = @[@"i32", @"hex", @"i32", @"ptr", @"raw"];

    _inputField.attributedPlaceholder = [[NSAttributedString alloc]
        initWithString:placeholders[_currentTab]
        attributes:@{NSForegroundColorAttributeName: [SYTheme textMuted]}];
    [_typeButton setTitle:typeLabels[_currentTab] forState:UIControlStateNormal];
}

#pragma mark - Actions

- (void)closeTapped {
    [UIView animateWithDuration:0.2 animations:^{
        [ShirayukiWindow shared].transform = CGAffineTransformMakeScale(0.9, 0.9);
        [ShirayukiWindow shared].alpha = 0;
    } completion:^(BOOL finished) {
        [[ShirayukiWindow shared] hide];
        [ShirayukiWindow shared].transform = CGAffineTransformIdentity;
        [ShirayukiWindow shared].alpha = 1;
    }];
}

- (void)minimizeTapped {
    // Animate to smaller size
    [[ShirayukiWindow shared] hide];
}

- (void)typeTapped {
    // Cycle search types
    NSArray *types = @[@"int32", @"float", @"hex", @"string"];
    NSArray *labels = @[@"i32", @"f32", @"hex", @"str"];
    NSUInteger idx = [types indexOfObject:_searchType];
    idx = (idx + 1) % types.count;
    _searchType = types[idx];
    [_typeButton setTitle:labels[idx] forState:UIControlStateNormal];

    // Animate
    [UIView animateWithDuration:0.15 animations:^{
        self.typeButton.transform = CGAffineTransformMakeScale(1.2, 1.2);
    } completion:^(BOOL finished) {
        [UIView animateWithDuration:0.1 animations:^{
            self.typeButton.transform = CGAffineTransformIdentity;
        }];
    }];
}

- (void)actionTapped {
    NSString *input = _inputField.text;
    if (!input.length) return;

    [_inputField resignFirstResponder];

    switch (_currentTab) {
        case SYTabSearch: [self performSearch:input]; break;
        case SYTabPatches: [self performPatch:input]; break;
        case SYTabFreeze: [self performFreeze:input]; break;
        case SYTabPointerScan: [self performPointerScan:input]; break;
        case SYTabDump: [self performDump:input]; break;
    }
}

#pragma mark - Status Updates

- (void)setStatus:(NSString *)text icon:(NSString *)iconName color:(UIColor *)color loading:(BOOL)loading {
    dispatch_async(dispatch_get_main_queue(), ^{
        self.statusLabel.text = text;
        self.statusLabel.textColor = color ?: [SYTheme textMuted];
        self.statusIcon.image = [SYTheme icon:iconName size:10 color:color ?: [SYTheme textMuted]];
        self.statusIcon.hidden = loading;
        if (loading) [self.spinner startAnimating];
        else [self.spinner stopAnimating];
    });
}

#pragma mark - Search

- (void)performSearch:(NSString *)input {
    if (_isSearching) return;
    _isSearching = YES;
    [_searchResults removeAllObjects];
    [self setStatus:@"Scanning memory..." icon:@"" color:[SYTheme accent] loading:YES];

    // Pulse animation on action button
    [UIView animateWithDuration:0.3 delay:0 options:UIViewAnimationOptionAutoreverse | UIViewAnimationOptionRepeat
                     animations:^{
        self.actionButton.alpha = 0.5;
    } completion:nil];

    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        auto regions = Memory::listRegions(VM_PROT_READ | VM_PROT_WRITE);
        size_t totalHits = 0;

        for (auto &region : regions) {
            if (region.size > 100 * 1024 * 1024) continue;

            std::vector<uintptr_t> hits;

            if ([self.searchType isEqualToString:@"int32"]) {
                int32_t val = [input intValue];
                hits = Scanner::findValue<int32_t>(region.start, region.size, val);
            } else if ([self.searchType isEqualToString:@"float"]) {
                float val = [input floatValue];
                hits = Scanner::findValue<float>(region.start, region.size, val);
            } else if ([self.searchType isEqualToString:@"hex"]) {
                hits = Scanner::findPattern(region.start, region.size, [input UTF8String]);
            } else {
                hits = Scanner::findString(region.start, region.size, [input UTF8String]);
            }

            for (auto addr : hits) {
                if (totalHits < 500) {
                    [self.searchResults addObject:@(addr)];
                }
                totalHits++;
            }
            if (totalHits >= 500) break;
        }

        dispatch_async(dispatch_get_main_queue(), ^{
            self.isSearching = NO;
            [self.actionButton.layer removeAllAnimations];
            self.actionButton.alpha = 1.0;

            NSString *msg = [NSString stringWithFormat:@"%zu results found", totalHits];
            UIColor *color = totalHits > 0 ? [SYTheme success] : [SYTheme warning];
            NSString *icon = totalHits > 0 ? @"checkmark.circle.fill" : @"exclamationmark.triangle.fill";
            [self setStatus:msg icon:icon color:color loading:NO];

            [self.tableView reloadData];

            // Animate rows appearing
            [self animateTableAppear];
        });
    });
}

#pragma mark - Patch

- (void)performPatch:(NSString *)input {
    NSArray *parts = [input componentsSeparatedByString:@" "];
    if (parts.count < 2) {
        [self setStatus:@"Format: 0xADDR HEXBYTES" icon:@"exclamationmark.triangle.fill" color:[SYTheme warning] loading:NO];
        return;
    }

    unsigned long long addr = strtoull([parts[0] UTF8String], NULL, 16);
    NSMutableString *hexStr = [NSMutableString new];
    for (NSUInteger i = 1; i < parts.count; i++) {
        [hexStr appendString:parts[i]];
        if (i < parts.count - 1) [hexStr appendString:@" "];
    }

    auto patch = Patch::createWithHex((uintptr_t)addr, [hexStr UTF8String]);
    if (patch.isValid() && patch.apply()) {
        NSMutableDictionary *entry = [@{
            @"address": @(addr),
            @"hex": hexStr,
            @"original": @(Hex::fromBytes(patch.originalBytes().data(),
                                          patch.originalBytes().size()).c_str()),
            @"applied": @YES
        } mutableCopy];
        [_patchEntries addObject:entry];
        [self setStatus:[NSString stringWithFormat:@"Patched 0x%llX", addr]
                   icon:@"checkmark.circle.fill" color:[SYTheme success] loading:NO];
    } else {
        [self setStatus:@"Patch failed — invalid address or protection"
                   icon:@"xmark.circle.fill" color:[SYTheme danger] loading:NO];
    }
    [_tableView reloadData];
}

#pragma mark - Freeze

- (void)performFreeze:(NSString *)input {
    NSArray *parts = [input componentsSeparatedByString:@" "];
    if (parts.count < 2) {
        [self setStatus:@"Format: 0xADDR VALUE" icon:@"exclamationmark.triangle.fill" color:[SYTheme warning] loading:NO];
        return;
    }

    unsigned long long addr = strtoull([parts[0] UTF8String], NULL, 16);
    int32_t value = [parts[1] intValue];

    auto &fm = FreezeManager::shared();
    uint64_t fid = fm.addValue<int32_t>((uintptr_t)addr, value, "");
    if (!fm.isRunning()) fm.start(16);

    NSMutableDictionary *entry = [@{
        @"id": @(fid),
        @"address": @(addr),
        @"value": @(value),
        @"active": @YES
    } mutableCopy];
    [_freezeEntries addObject:entry];

    [self setStatus:[NSString stringWithFormat:@"Frozen 0x%llX = %d", addr, value]
               icon:@"lock.fill" color:[SYTheme accent] loading:NO];
    [_tableView reloadData];
}

#pragma mark - Pointer Scan

- (void)performPointerScan:(NSString *)input {
    unsigned long long addr = strtoull([input UTF8String], NULL, 16);
    if (!addr) {
        [self setStatus:@"Invalid address" icon:@"xmark.circle.fill" color:[SYTheme danger] loading:NO];
        return;
    }

    [_pointerResults removeAllObjects];
    [self setStatus:@"Scanning pointer chains..." icon:@"" color:[SYTheme accent] loading:YES];

    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        PointerScanConfig config;
        config.targetAddress = (uintptr_t)addr;
        config.maxDepth = 3;
        config.maxOffset = 0x1000;
        config.maxResults = 50;

        auto chains = PointerScanner::scan(config);

        dispatch_async(dispatch_get_main_queue(), ^{
            for (auto &chain : chains) {
                NSMutableString *desc = [NSMutableString stringWithFormat:@"%s+0x%lX",
                    chain.moduleName.c_str(), chain.moduleOffset];
                for (auto off : chain.offsets) {
                    [desc appendFormat:@" -> [+0x%llX]", (unsigned long long)off];
                }
                uintptr_t resolved = chain.resolve();
                BOOL valid = (resolved == (uintptr_t)addr);

                [self.pointerResults addObject:@{
                    @"desc": desc,
                    @"valid": @(valid),
                    @"depth": @(chain.offsets.size())
                }];
            }

            NSString *msg = [NSString stringWithFormat:@"%zu pointer chains found", chains.size()];
            [self setStatus:msg icon:@"checkmark.circle.fill" color:[SYTheme success] loading:NO];
            [self.tableView reloadData];
            [self animateTableAppear];
        });
    });
}

#pragma mark - Dump

- (void)performDump:(NSString *)input {
    NSArray *parts = [input componentsSeparatedByString:@" "];
    unsigned long long addr = strtoull([parts[0] UTF8String], NULL, 16);
    size_t len = (parts.count > 1) ? [parts[1] integerValue] : 64;
    if (len > 4096) len = 4096;

    std::string dump = Hex::dump((uintptr_t)addr, len);

    UIAlertController *alert = [UIAlertController
        alertControllerWithTitle:[NSString stringWithFormat:@"Dump @ 0x%llX", addr]
        message:[NSString stringWithUTF8String:dump.c_str()]
        preferredStyle:UIAlertControllerStyleAlert];

    // Use monospace font for the message
    NSMutableAttributedString *attrMsg = [[NSMutableAttributedString alloc]
        initWithString:[NSString stringWithUTF8String:dump.c_str()]
        attributes:@{
            NSFontAttributeName: [SYTheme monoSmall],
            NSForegroundColorAttributeName: [SYTheme textPrimary]
        }];
    [alert setValue:attrMsg forKey:@"attributedMessage"];

    [alert addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
    [alert addAction:[UIAlertAction actionWithTitle:@"Copy" style:UIAlertActionStyleDefault handler:^(UIAlertAction *a) {
        [UIPasteboard generalPasteboard].string = [NSString stringWithUTF8String:dump.c_str()];
    }]];
    [self presentViewController:alert animated:YES completion:nil];

    [self setStatus:[NSString stringWithFormat:@"Dumped %zu bytes", len]
               icon:@"doc.text.fill" color:[SYTheme info] loading:NO];
}

#pragma mark - Table Animations

- (void)animateTableAppear {
    NSArray *cells = [_tableView visibleCells];
    for (NSUInteger i = 0; i < cells.count; i++) {
        UITableViewCell *cell = cells[i];
        cell.alpha = 0;
        cell.transform = CGAffineTransformMakeTranslation(0, 20);
        [UIView animateWithDuration:0.3 delay:i * 0.03 usingSpringWithDamping:0.8
              initialSpringVelocity:0 options:0 animations:^{
            cell.alpha = 1;
            cell.transform = CGAffineTransformIdentity;
        } completion:nil];
    }
}

#pragma mark - UITableView

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section {
    switch (_currentTab) {
        case SYTabSearch: return _searchResults.count;
        case SYTabPatches: return _patchEntries.count;
        case SYTabFreeze: return _freezeEntries.count;
        case SYTabPointerScan: return _pointerResults.count;
        case SYTabDump: return 0;
    }
    return 0;
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath {
    SYResultCell *cell = [tableView dequeueReusableCellWithIdentifier:kCellID forIndexPath:indexPath];

    switch (_currentTab) {
        case SYTabSearch: {
            uintptr_t addr = [_searchResults[indexPath.row] unsignedLongLongValue];
            int32_t val = Memory::readValue<int32_t>(addr);
            [cell configureWithIcon:[SYTheme icon:@"memorychip" size:14]
                              title:[NSString stringWithFormat:@"0x%lX", addr]
                             detail:[NSString stringWithFormat:@"= %d (0x%X)", val, val]
                              badge:_searchType
                         badgeColor:[SYTheme accentDim]];
            break;
        }
        case SYTabPatches: {
            NSDictionary *entry = _patchEntries[indexPath.row];
            BOOL applied = [entry[@"applied"] boolValue];
            [cell configureWithIcon:[SYTheme icon:@"wrench.fill" size:14 color:applied ? [SYTheme success] : [SYTheme textMuted]]
                              title:[NSString stringWithFormat:@"0x%llX", [entry[@"address"] unsignedLongLongValue]]
                             detail:[NSString stringWithFormat:@"%@ (was: %@)", entry[@"hex"], entry[@"original"]]
                              badge:applied ? @"ON" : @"OFF"
                         badgeColor:applied ? [SYTheme success] : [SYTheme textMuted]];
            break;
        }
        case SYTabFreeze: {
            NSDictionary *entry = _freezeEntries[indexPath.row];
            BOOL active = [entry[@"active"] boolValue];
            [cell configureWithIcon:[SYTheme icon:active ? @"lock.fill" : @"lock.open" size:14
                                            color:active ? [SYTheme accent] : [SYTheme textMuted]]
                              title:[NSString stringWithFormat:@"0x%llX", [entry[@"address"] unsignedLongLongValue]]
                             detail:[NSString stringWithFormat:@"Locked = %@", entry[@"value"]]
                              badge:active ? @"FROZEN" : @"PAUSED"
                         badgeColor:active ? [SYTheme accent] : [SYTheme textMuted]];
            break;
        }
        case SYTabPointerScan: {
            NSDictionary *entry = _pointerResults[indexPath.row];
            BOOL valid = [entry[@"valid"] boolValue];
            NSInteger depth = [entry[@"depth"] integerValue];
            [cell configureWithIcon:[SYTheme icon:@"arrow.triangle.branch" size:14
                                            color:valid ? [SYTheme success] : [SYTheme warning]]
                              title:entry[@"desc"]
                             detail:[NSString stringWithFormat:@"Depth: %ld", (long)depth]
                              badge:valid ? @"OK" : @"??"
                         badgeColor:valid ? [SYTheme success] : [SYTheme warning]];
            break;
        }
        case SYTabDump:
            break;
    }

    return cell;
}

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath {
    switch (_currentTab) {
        case SYTabSearch: {
            uintptr_t addr = [_searchResults[indexPath.row] unsignedLongLongValue];
            [self showModifyAlert:addr];
            break;
        }
        case SYTabFreeze: {
            NSMutableDictionary *entry = _freezeEntries[indexPath.row];
            BOOL active = ![entry[@"active"] boolValue];
            entry[@"active"] = @(active);
            uint64_t fid = [entry[@"id"] unsignedLongLongValue];
            FreezeManager::shared().setActive(fid, active);
            [tableView reloadRowsAtIndexPaths:@[indexPath] withRowAnimation:UITableViewRowAnimationFade];
            break;
        }
        default: break;
    }
}

- (BOOL)tableView:(UITableView *)tableView canEditRowAtIndexPath:(NSIndexPath *)indexPath {
    return (_currentTab == SYTabFreeze || _currentTab == SYTabPatches);
}

- (void)tableView:(UITableView *)tableView commitEditingStyle:(UITableViewCellEditingStyle)editingStyle
    forRowAtIndexPath:(NSIndexPath *)indexPath {
    if (editingStyle != UITableViewCellEditingStyleDelete) return;

    if (_currentTab == SYTabFreeze) {
        uint64_t fid = [_freezeEntries[indexPath.row][@"id"] unsignedLongLongValue];
        FreezeManager::shared().remove(fid);
        [_freezeEntries removeObjectAtIndex:indexPath.row];
    } else if (_currentTab == SYTabPatches) {
        [_patchEntries removeObjectAtIndex:indexPath.row];
    }
    [tableView deleteRowsAtIndexPaths:@[indexPath] withRowAnimation:UITableViewRowAnimationFade];
}

#pragma mark - Modify Alert

- (void)showModifyAlert:(uintptr_t)addr {
    int32_t current = Memory::readValue<int32_t>(addr);

    UIAlertController *alert = [UIAlertController
        alertControllerWithTitle:[NSString stringWithFormat:@"0x%lX", addr]
        message:[NSString stringWithFormat:@"Current: %d (0x%X)", current, current]
        preferredStyle:UIAlertControllerStyleAlert];

    [alert addTextFieldWithConfigurationHandler:^(UITextField *tf) {
        tf.placeholder = @"New value";
        tf.text = [NSString stringWithFormat:@"%d", current];
        tf.keyboardType = UIKeyboardTypeNumbersAndPunctuation;
        tf.font = [SYTheme monoMedium];
    }];

    [alert addAction:[UIAlertAction actionWithTitle:@"Write" style:UIAlertActionStyleDefault
        handler:^(UIAlertAction *action) {
            int32_t newVal = [alert.textFields.firstObject.text intValue];
            Memory::writeValue<int32_t>(addr, newVal);
            [self setStatus:[NSString stringWithFormat:@"Wrote %d to 0x%lX", newVal, addr]
                       icon:@"pencil.circle.fill" color:[SYTheme success] loading:NO];
            [self.tableView reloadData];
        }]];

    [alert addAction:[UIAlertAction actionWithTitle:@"Freeze" style:UIAlertActionStyleDefault
        handler:^(UIAlertAction *action) {
            int32_t val = [alert.textFields.firstObject.text intValue];
            NSString *cmd = [NSString stringWithFormat:@"0x%lX %d", addr, val];
            [self performFreeze:cmd];
        }]];

    [alert addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
    [self presentViewController:alert animated:YES completion:nil];
}

#pragma mark - UITextField

- (BOOL)textFieldShouldReturn:(UITextField *)textField {
    [self actionTapped];
    return YES;
}

@end
