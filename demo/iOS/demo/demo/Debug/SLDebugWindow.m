//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#import "SLDebugWindow.h"

#import "SlarkPlayer.h"

#include <math.h>

static NSString* const kTitles[] = {
    @"Source", @"Video", @"Audio", @"Playback", @"Sync",
    @"Buffer", @"Queue", @"Render", @"Network", @"System"
};
static NSArray<NSString*>* GroupTitles(void) {
    return @[kTitles[0], kTitles[1], kTitles[2], kTitles[3], kTitles[4],
             kTitles[5], kTitles[6], kTitles[7], kTitles[8], kTitles[9]];
}
static NSArray<NSArray<NSString*>*>* TabGroups(void) {
    return @[
        @[@"Playback", @"Sync"],
        @[@"Source", @"Video", @"Audio"],
        @[@"Buffer", @"Queue", @"Render"],
        @[@"Network"],
        @[@"System"]
    ];
}
static NSArray<NSString*>* TabNames(void) {
    return @[@"Overview", @"Media", @"Pipeline", @"Network", @"System"];
}

static UIColor* BgColor(void) { return [UIColor colorWithWhite:0.055 alpha:1.0]; }
static UIColor* CardColor(void) { return [UIColor colorWithWhite:0.09 alpha:1.0]; }
static UIColor* CpuColor(void) { return [UIColor colorWithRed:0.31 green:0.76 blue:0.97 alpha:1.0]; }
static UIColor* MemoryColor(void) { return [UIColor colorWithRed:0.51 green:0.78 blue:0.52 alpha:1.0]; }

@interface SLDebugPoint : NSObject
@property (nonatomic, assign) int64_t timestampUs;
@property (nonatomic, assign) double cpuPercent;
@property (nonatomic, assign) int64_t rssKB;
@end

@implementation SLDebugPoint
@end

@interface SLDebugChartView : UIView
@property (nonatomic, copy) NSArray<NSNumber*>* values;
@property (nonatomic, strong) UIColor* lineColor;
@property (nonatomic, assign) BOOL zeroBased;
@end

@implementation SLDebugChartView

- (instancetype)initWithFrame:(CGRect)frame {
    if (self = [super initWithFrame:frame]) {
        self.backgroundColor = CardColor();
        self.layer.cornerRadius = 8;
        self.clipsToBounds = YES;
        _values = @[];
        _zeroBased = YES;
    }
    return self;
}

- (void)drawRect:(CGRect)rect {
    CGContextRef ctx = UIGraphicsGetCurrentContext();
    if (!ctx) return;
    const CGFloat w = CGRectGetWidth(rect);
    const CGFloat h = CGRectGetHeight(rect);
    if (w <= 0 || h <= 0) return;

    CGContextSetStrokeColorWithColor(ctx, [UIColor colorWithWhite:1.0 alpha:0.08].CGColor);
    CGContextSetLineWidth(ctx, 1.0);
    for (int i = 0; i <= 4; ++i) {
        CGFloat y = h * i / 4.0;
        CGContextMoveToPoint(ctx, 0, y);
        CGContextAddLineToPoint(ctx, w, y);
        CGFloat x = w * i / 4.0;
        CGContextMoveToPoint(ctx, x, 0);
        CGContextAddLineToPoint(ctx, x, h);
    }
    CGContextStrokePath(ctx);
    if (self.values.count == 0) return;

    double high = 0.0;
    double low = self.zeroBased ? 0.0 : self.values.firstObject.doubleValue;
    for (NSNumber* value in self.values) {
        high = fmax(high, value.doubleValue);
        if (!self.zeroBased) low = fmin(low, value.doubleValue);
    }
    const double padding = self.zeroBased ? fmax(1.0, high * 0.15) : fmax(0.5, (high - low) * 0.15);
    const double min = self.zeroBased ? 0.0 : low - padding;
    const double max = high + padding;
    const double span = fmax(1.0, max - min);
    const CGFloat step = self.values.count > 1 ? w / (self.values.count - 1) : w;
    CGMutablePathRef line = CGPathCreateMutable();
    CGMutablePathRef fill = CGPathCreateMutable();
    CGPathMoveToPoint(fill, NULL, 0, h);
    [self.values enumerateObjectsUsingBlock:^(NSNumber* value, NSUInteger idx, BOOL* stop) {
        CGFloat x = idx * step;
        CGFloat y = h - (CGFloat)((value.doubleValue - min) / span) * (h - 4.0) - 2.0;
        if (idx == 0) CGPathMoveToPoint(line, NULL, x, y);
        else CGPathAddLineToPoint(line, NULL, x, y);
        CGPathAddLineToPoint(fill, NULL, x, y);
    }];
    CGPathAddLineToPoint(fill, NULL, (self.values.count - 1) * step, h);
    CGPathCloseSubpath(fill);

    CGContextAddPath(ctx, fill);
    CGContextSetFillColorWithColor(ctx, [self.lineColor colorWithAlphaComponent:0.16].CGColor);
    CGContextFillPath(ctx);
    CGContextAddPath(ctx, line);
    CGContextSetStrokeColorWithColor(ctx, self.lineColor.CGColor);
    CGContextSetLineWidth(ctx, 2.0);
    CGContextStrokePath(ctx);
    CGPathRelease(line);
    CGPathRelease(fill);
}

@end

@interface SLDebugGroupView : UIView
@property (nonatomic, strong) UILabel* titleLabel;
@property (nonatomic, strong) UILabel* bodyLabel;
@end

@implementation SLDebugGroupView

- (instancetype)initWithFrame:(CGRect)frame {
    if (self = [super initWithFrame:frame]) {
        self.backgroundColor = CardColor();
        self.layer.cornerRadius = 8;
        _titleLabel = [UILabel new];
        _titleLabel.font = [UIFont monospacedSystemFontOfSize:10 weight:UIFontWeightSemibold];
        _titleLabel.textColor = CpuColor();
        [self addSubview:_titleLabel];
        _bodyLabel = [UILabel new];
        _bodyLabel.font = [UIFont monospacedSystemFontOfSize:10 weight:UIFontWeightRegular];
        _bodyLabel.textColor = [UIColor whiteColor];
        _bodyLabel.numberOfLines = 0;
        [self addSubview:_bodyLabel];
    }
    return self;
}

- (void)layoutSubviews {
    [super layoutSubviews];
    const CGFloat w = CGRectGetWidth(self.bounds);
    self.titleLabel.frame = CGRectMake(10, 8, w - 20, 14);
    CGSize body = [self.bodyLabel sizeThatFits:CGSizeMake(w - 20, CGFLOAT_MAX)];
    self.bodyLabel.frame = CGRectMake(10, 26, w - 20, body.height);
}

@end

@interface SLDebugRootView : UIView
@end

@implementation SLDebugRootView

- (UIView*)hitTest:(CGPoint)point withEvent:(UIEvent*)event {
    UIView* hit = [super hitTest:point withEvent:event];
    return hit == self ? nil : hit;
}

@end

@interface SLDebugOverlayWindow : UIWindow
@end

@implementation SLDebugOverlayWindow

- (UIView*)hitTest:(CGPoint)point withEvent:(UIEvent*)event {
    UIView* hit = [super hitTest:point withEvent:event];
    return hit == self ? nil : hit;
}

@end

@interface SLDebugWindow () <UIScrollViewDelegate>
@property (nonatomic, strong) UIWindow* window;
@property (nonatomic, strong) UIView* panel;
@property (nonatomic, strong) UILabel* headerLabel;
@property (nonatomic, strong) UISegmentedControl* tabs;
@property (nonatomic, strong) UIScrollView* scroll;
@property (nonatomic, strong) UIView* chartsContainer;
@property (nonatomic, strong) SLDebugChartView* cpuChart;
@property (nonatomic, strong) SLDebugChartView* memoryChart;
@property (nonatomic, strong) UILabel* cpuValue;
@property (nonatomic, strong) UILabel* memoryValue;
@property (nonatomic, strong) UILabel* chartStats;
@property (nonatomic, strong) NSMutableArray<SLDebugGroupView*>* groupViews;
@property (nonatomic, strong) NSTimer* timer;
@property (nonatomic, strong) NSArray<SLDebugPoint*>* points;
@property (nonatomic, strong) NSDictionary<NSString*, NSArray<NSString*>*>* groups;
@property (nonatomic, assign) BOOL collapsed;
@property (nonatomic, assign) BOOL fullscreen;
@property (nonatomic, assign) CGPoint panelOrigin;
@end

@implementation SLDebugWindow

+ (instancetype)shared {
    static SLDebugWindow* instance;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        instance = [SLDebugWindow new];
    });
    return instance;
}

- (instancetype)init {
    if (self = [super init]) {
        _points = @[];
        _groups = @{};
        _collapsed = YES;
        _groupViews = [NSMutableArray array];
    }
    return self;
}

- (void)start {
    if (self.timer) return;
    [self setupWindow];
    __weak typeof(self) weakSelf = self;
    self.timer = [NSTimer scheduledTimerWithTimeInterval:1.0
                                                 repeats:YES
                                                   block:^(NSTimer* timer) {
                                                       [weakSelf tick];
                                                   }];
    [self tick];
}

- (void)setupWindow {
    UIWindowScene* scene = nil;
    for (UIScene* candidate in UIApplication.sharedApplication.connectedScenes) {
        if ([candidate isKindOfClass:[UIWindowScene class]] &&
            candidate.activationState == UISceneActivationStateForegroundActive) {
            scene = (UIWindowScene*)candidate;
            break;
        }
    }
    if (scene) {
        self.window = [[SLDebugOverlayWindow alloc] initWithWindowScene:scene];
        self.window.frame = [UIScreen mainScreen].bounds;
    } else {
        self.window = [[SLDebugOverlayWindow alloc] initWithFrame:[UIScreen mainScreen].bounds];
    }
    self.window.windowLevel = UIWindowLevelStatusBar + 100;
    self.window.backgroundColor = [UIColor clearColor];

    UIViewController* root = [UIViewController new];
    root.view = [[SLDebugRootView alloc] initWithFrame:self.window.bounds];
    root.view.backgroundColor = [UIColor clearColor];
    self.window.rootViewController = root;

    self.panel = [UIView new];
    self.panel.backgroundColor = [BgColor() colorWithAlphaComponent:0.94];
    self.panel.layer.cornerRadius = 12;
    self.panel.layer.shadowColor = [UIColor blackColor].CGColor;
    self.panel.layer.shadowOpacity = 0.5;
    self.panel.layer.shadowRadius = 8;
    self.panel.layer.shadowOffset = CGSizeMake(0, 2);
    [root.view addSubview:self.panel];

    [self setupHeader];
    [self setupTabs];
    [self setupScroll];
    [self layoutPanel];

    UIPanGestureRecognizer* pan =
        [[UIPanGestureRecognizer alloc] initWithTarget:self action:@selector(handlePan:)];
    [self.headerLabel addGestureRecognizer:pan];
    self.headerLabel.userInteractionEnabled = YES;

    [self.window setHidden:NO];
}

- (void)setupHeader {
    self.headerLabel = [UILabel new];
    self.headerLabel.text = @"DEBUG";
    self.headerLabel.font = [UIFont monospacedSystemFontOfSize:11 weight:UIFontWeightBold];
    self.headerLabel.textColor = CpuColor();
    self.headerLabel.adjustsFontSizeToFitWidth = YES;
    self.headerLabel.minimumScaleFactor = 0.7;
    [self.panel addSubview:self.headerLabel];

    UIButton* collapse = [UIButton buttonWithType:UIButtonTypeSystem];
    [collapse setTitle:@"+" forState:UIControlStateNormal];
    [collapse addTarget:self action:@selector(toggleCollapse) forControlEvents:UIControlEventTouchUpInside];
    collapse.tag = 1;
    [self.panel addSubview:collapse];

    UIButton* expand = [UIButton buttonWithType:UIButtonTypeSystem];
    [expand setTitle:@"⤢" forState:UIControlStateNormal];
    [expand addTarget:self action:@selector(toggleFullscreen) forControlEvents:UIControlEventTouchUpInside];
    expand.tag = 2;
    [self.panel addSubview:expand];
}

- (void)setupTabs {
    self.tabs = [[UISegmentedControl alloc] initWithItems:TabNames()];
    self.tabs.selectedSegmentIndex = 4;
    self.tabs.apportionsSegmentWidthsByContent = YES;
    UIFont* tabFont = [UIFont systemFontOfSize:9 weight:UIFontWeightMedium];
    [self.tabs setTitleTextAttributes:@{NSFontAttributeName : tabFont,
                                        NSForegroundColorAttributeName : [UIColor colorWithWhite:0.65 alpha:1.0]}
                             forState:UIControlStateNormal];
    [self.tabs setTitleTextAttributes:@{NSFontAttributeName : tabFont,
                                        NSForegroundColorAttributeName : [UIColor whiteColor]}
                             forState:UIControlStateSelected];
    [self.tabs addTarget:self action:@selector(refreshContent) forControlEvents:UIControlEventValueChanged];
    [self.panel addSubview:self.tabs];
}

- (void)setupScroll {
    self.scroll = [UIScrollView new];
    self.scroll.showsVerticalScrollIndicator = YES;
    [self.panel addSubview:self.scroll];

    self.chartsContainer = [UIView new];
    [self.scroll addSubview:self.chartsContainer];

    self.cpuValue = [self makeValueLabel];
    self.memoryValue = [self makeValueLabel];
    self.chartStats = [UILabel new];
    self.chartStats.font = [UIFont monospacedSystemFontOfSize:9 weight:UIFontWeightRegular];
    self.chartStats.textColor = [UIColor colorWithWhite:0.6 alpha:1.0];
    self.cpuChart = [[SLDebugChartView alloc] initWithFrame:CGRectZero];
    self.cpuChart.lineColor = CpuColor();
    self.memoryChart = [[SLDebugChartView alloc] initWithFrame:CGRectZero];
    self.memoryChart.lineColor = MemoryColor();
    self.memoryChart.zeroBased = NO;
    [self.chartsContainer addSubview:self.cpuValue];
    [self.chartsContainer addSubview:self.memoryValue];
    [self.chartsContainer addSubview:self.cpuChart];
    [self.chartsContainer addSubview:self.memoryChart];
    [self.chartsContainer addSubview:self.chartStats];

    for (NSUInteger i = 0; i < 3; ++i) {
        SLDebugGroupView* view = [[SLDebugGroupView alloc] initWithFrame:CGRectZero];
        [self.groupViews addObject:view];
        [self.scroll addSubview:view];
    }
}

- (UILabel*)makeValueLabel {
    UILabel* label = [UILabel new];
    label.font = [UIFont monospacedSystemFontOfSize:16 weight:UIFontWeightSemibold];
    label.textColor = [UIColor whiteColor];
    return label;
}

- (void)layoutPanel {
    const CGRect bounds = self.window.bounds;
    const CGFloat margin = 8;
    CGFloat width = self.fullscreen ? CGRectGetWidth(bounds) - margin * 2 : 260;
    CGFloat height = self.collapsed ? 40 : (self.fullscreen ? CGRectGetHeight(bounds) - 100 : 360);
    if (self.fullscreen) {
        self.panel.frame = CGRectMake(margin, 60, width, height);
        self.panelOrigin = CGPointMake(margin, 60);
    } else {
        CGFloat x = self.panelOrigin.x > 0 ? self.panelOrigin.x : CGRectGetWidth(bounds) - width - margin;
        CGFloat y = self.panelOrigin.y > 0 ? self.panelOrigin.y : 100;
        self.panel.frame = CGRectMake(x, y, width, height);
    }

    const CGFloat w = CGRectGetWidth(self.panel.bounds);
    self.headerLabel.frame = CGRectMake(12, 8, w - 84, 24);
    UIButton* collapse = (UIButton*)[self.panel viewWithTag:1];
    UIButton* expand = (UIButton*)[self.panel viewWithTag:2];
    collapse.frame = CGRectMake(w - 70, 4, 30, 32);
    expand.frame = CGRectMake(w - 38, 4, 30, 32);
    [collapse setTitle:self.collapsed ? @"+" : @"−" forState:UIControlStateNormal];

    const BOOL showBody = !self.collapsed;
    self.tabs.hidden = !showBody;
    self.scroll.hidden = !showBody;
    if (!showBody) return;

    self.tabs.frame = CGRectMake(8, 40, w - 16, 28);
    self.scroll.frame = CGRectMake(8, 74, w - 16, height - 82);

    const CGFloat sw = CGRectGetWidth(self.scroll.bounds);
    NSArray<NSString*>* names = TabGroups()[self.tabs.selectedSegmentIndex];
    const BOOL system = self.tabs.selectedSegmentIndex == 4;
    const CGFloat chartHeight = self.fullscreen ? 110 : 62;
    const CGFloat chartsHeight = system ? (chartHeight * 2 + 68) : 0;
    self.chartsContainer.hidden = !system;
    self.chartsContainer.frame = CGRectMake(0, 0, sw, chartsHeight);
    if (system) {
        self.cpuValue.frame = CGRectMake(8, 2, sw - 16, 20);
        self.cpuChart.frame = CGRectMake(0, 24, sw, chartHeight);
        self.memoryValue.frame = CGRectMake(8, 28 + chartHeight, sw - 16, 20);
        self.memoryChart.frame = CGRectMake(0, 50 + chartHeight, sw, chartHeight);
        self.chartStats.frame = CGRectMake(8, 54 + chartHeight * 2, sw - 16, 12);
    }

    CGFloat y = chartsHeight + (system ? 8 : 0);
    for (NSUInteger i = 0; i < self.groupViews.count; ++i) {
        SLDebugGroupView* view = self.groupViews[i];
        view.hidden = i >= names.count;
        if (view.hidden) continue;
        NSArray<NSString*>* rows = self.groups[names[i]] ?: @[@"n/a"];
        view.bodyLabel.text = [rows componentsJoinedByString:@"\n"];
        [view layoutIfNeeded];
        CGFloat textHeight = [view.bodyLabel sizeThatFits:CGSizeMake(sw - 20, CGFLOAT_MAX)].height;
        CGFloat cardHeight = textHeight + 34;
        view.frame = CGRectMake(0, y, sw, cardHeight);
        y += cardHeight + 8;
    }
    self.scroll.contentSize = CGSizeMake(sw, y);
}

- (void)toggleCollapse {
    self.collapsed = !self.collapsed;
    [self layoutPanel];
}

- (void)toggleFullscreen {
    self.fullscreen = !self.fullscreen;
    if (self.fullscreen) self.collapsed = NO;
    [self layoutPanel];
    [self refreshContent];
}

- (void)handlePan:(UIPanGestureRecognizer*)gesture {
    if (self.fullscreen) return;
    CGPoint translation = [gesture translationInView:self.window];
    [gesture setTranslation:CGPointZero inView:self.window];
    CGRect frame = self.panel.frame;
    frame.origin.x += translation.x;
    frame.origin.y += translation.y;
    self.panel.frame = frame;
    self.panelOrigin = frame.origin;
}

- (void)tick {
    NSString* csv = [SlarkPlayer debugMetricsTick];
    self.points = [self parseCsv:csv];
    NSString* snapshot = self.snapshotProvider ? self.snapshotProvider() : @"";
    self.groups = [self parseSnapshot:snapshot];
    [self refreshContent];
}

- (NSArray<SLDebugPoint*>*)parseCsv:(NSString*)csv {
    NSMutableArray<SLDebugPoint*>* points = [NSMutableArray array];
    for (NSString* line in [csv componentsSeparatedByString:@"\n"]) {
        if (line.length == 0) continue;
        NSArray<NSString*>* parts = [line componentsSeparatedByString:@","];
        if (parts.count != 4) continue;
        SLDebugPoint* point = [SLDebugPoint new];
        point.timestampUs = [parts[0] longLongValue];
        point.cpuPercent = [parts[1] doubleValue];
        point.rssKB = [parts[2] longLongValue];
        [points addObject:point];
    }
    return points;
}

- (NSDictionary<NSString*, NSArray<NSString*>*>*)parseSnapshot:(NSString*)text {
    NSMutableDictionary<NSString*, NSMutableArray<NSString*>*>* result = [NSMutableDictionary dictionary];
    for (NSString* title in GroupTitles()) result[title] = [NSMutableArray array];
    NSString* current = nil;
    for (NSString* raw in [text componentsSeparatedByString:@"\n"]) {
        if (raw.length == 0) continue;
        if ([raw hasPrefix:@"["] && [raw hasSuffix:@"]"]) {
            current = [raw substringWithRange:NSMakeRange(1, raw.length - 2)];
            if (!result[current]) result[current] = [NSMutableArray array];
            continue;
        }
        if (!current) continue;
        NSRange separator = [raw rangeOfString:@": "];
        NSString* line = separator.location == NSNotFound
                             ? raw
                             : [NSString stringWithFormat:@"%@  %@",
                                                          [raw substringToIndex:separator.location],
                                                          [raw substringFromIndex:NSMaxRange(separator)]];
        [result[current] addObject:line];
    }
    return result;
}

- (void)refreshContent {
    if (!self.panel) return;
    NSMutableArray<NSNumber*>* cpu = [NSMutableArray array];
    NSMutableArray<NSNumber*>* rss = [NSMutableArray array];
    for (SLDebugPoint* point in self.points) {
        [cpu addObject:@(point.cpuPercent)];
        [rss addObject:@(point.rssKB / 1024.0)];
    }
    self.cpuChart.values = cpu;
    self.memoryChart.values = rss;
    [self.cpuChart setNeedsDisplay];
    [self.memoryChart setNeedsDisplay];
    self.cpuValue.text = [NSString stringWithFormat:@"CPU  %.1f %%", cpu.lastObject.doubleValue];
    self.memoryValue.text = [NSString stringWithFormat:@"RSS  %.1f MB", rss.lastObject.doubleValue];
    self.cpuValue.textColor = CpuColor();
    self.memoryValue.textColor = MemoryColor();
    if (cpu.count > 0) {
        double peak = [[cpu valueForKeyPath:@"@max.doubleValue"] doubleValue];
        double min = [[cpu valueForKeyPath:@"@min.doubleValue"] doubleValue];
        self.chartStats.text = [NSString
            stringWithFormat:@"%.0f pts  CPU peak %.1f%% min %.1f%%   1s · 2 min · per-core",
                             (double)cpu.count, peak, min];
    }
    self.headerLabel.text = self.collapsed
                                ? [NSString stringWithFormat:@"CPU %.0f%% · %.0f MB",
                                                             cpu.lastObject.doubleValue,
                                                             rss.lastObject.doubleValue]
                                : @"DEBUG";
    [self layoutPanel];
}

@end
