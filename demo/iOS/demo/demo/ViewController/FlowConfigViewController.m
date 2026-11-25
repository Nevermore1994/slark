//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#import "FlowConfigViewController.h"

#import "Masonry.h"
#import "SlarkFlowConfig.h"

#include <math.h>
#include <stdlib.h>

static NSString* const kSourceLow = @"sourceLow";
static NSString* const kSourceHigh = @"sourceHigh";
static NSString* const kBufferingStart = @"bufferingStart";
static NSString* const kBufferingEnter = @"bufferingEnter";
static NSString* const kBufferingExit = @"bufferingExit";
static NSString* const kDnsTtl = @"dnsTtl";
static NSString* const kNotifyProgress = @"notifyProgress";
static NSString* const kNotifyCache = @"notifyCache";

static NSArray<NSDictionary<NSString*, NSString*>*>* FieldSpecs(void) {
    return @[
        @{@"group" : @"Source", @"key" : kSourceLow, @"label" : @"lowWatermarkSec"},
        @{@"group" : @"Source", @"key" : kSourceHigh, @"label" : @"highWatermarkSec"},
        @{@"group" : @"Buffering", @"key" : kBufferingStart, @"label" : @"startBufferSec"},
        @{@"group" : @"Buffering", @"key" : kBufferingEnter, @"label" : @"rebufferEnterSec"},
        @{@"group" : @"Buffering", @"key" : kBufferingExit, @"label" : @"rebufferExitSec"},
        @{@"group" : @"DNS", @"key" : kDnsTtl, @"label" : @"cacheTtlSec"},
        @{@"group" : @"Notification", @"key" : kNotifyProgress, @"label" : @"progressStepSec"},
        @{@"group" : @"Notification", @"key" : kNotifyCache, @"label" : @"cacheIntervalSec"},
    ];
}

static NSString* FormatDouble(double value) {
    return [NSString stringWithFormat:@"%.6g", value];
}

static BOOL ParseDouble(NSString* text, double* out) {
    NSString* trimmed =
        [text stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if (trimmed.length == 0) {
        return NO;
    }
    const char* cstr = [trimmed UTF8String];
    char* end = NULL;
    double value = strtod(cstr, &end);
    if (end == cstr || *end != '\0' || !isfinite(value)) {
        return NO;
    }
    *out = value;
    return YES;
}

static BOOL ConfigIsValid(SlarkFlowConfig* config) {
    return config.source.lowWatermarkSec >= 0.0 &&
           config.source.lowWatermarkSec < config.source.highWatermarkSec &&
           config.buffering.startBufferSec >= 0.0 && config.buffering.rebufferEnterSec >= 0.0 &&
           config.buffering.rebufferExitSec >= config.buffering.rebufferEnterSec &&
           config.buffering.rebufferExitSec < config.source.highWatermarkSec &&
           config.dns.cacheTtlSec > 0.0 && config.notify.progressStepSec > 0.0 &&
           config.notify.cacheIntervalSec > 0.0;
}

@implementation DemoFlowConfig

+ (instancetype)shared {
    static DemoFlowConfig* instance;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        instance = [DemoFlowConfig new];
    });
    return instance;
}

- (SlarkFlowConfig*)current {
    return self.config ?: [SlarkFlowConfig new];
}

@end

@interface FlowConfigViewController ()
@property (nonatomic, strong) UIScrollView* scrollView;
@property (nonatomic, strong) UIStackView* stack;
@property (nonatomic, strong) UITextView* urlsView;
@property (nonatomic, strong) UILabel* errorLabel;
@property (nonatomic, strong) NSMutableDictionary<NSString*, UITextField*>* fields;
@end

@implementation FlowConfigViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = [UIColor systemBackgroundColor];
    self.fields = [NSMutableDictionary dictionary];

    [self setupBar];
    [self setupForm];
    [self loadConfig:[DemoFlowConfig shared].current];

    UITapGestureRecognizer* tap =
        [[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(endEditing)];
    [self.view addGestureRecognizer:tap];
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(keyboardChanged:)
                                                 name:UIKeyboardWillShowNotification
                                               object:nil];
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(keyboardChanged:)
                                                 name:UIKeyboardWillHideNotification
                                               object:nil];
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
}

- (void)endEditing {
    [self.view endEditing:YES];
}

- (void)setupBar {
    UIView* bar = [UIView new];
    bar.backgroundColor = [UIColor secondarySystemBackgroundColor];
    [self.view addSubview:bar];
    [bar mas_makeConstraints:^(MASConstraintMaker* make) {
        make.top.equalTo(self.view.mas_safeAreaLayoutGuideTop);
        make.left.right.equalTo(self.view);
        make.height.mas_equalTo(56);
    }];

    UIButton* cancel = [UIButton buttonWithType:UIButtonTypeSystem];
    [cancel setTitle:@"Cancel" forState:UIControlStateNormal];
    [cancel addTarget:self action:@selector(cancel) forControlEvents:UIControlEventTouchUpInside];
    [bar addSubview:cancel];
    [cancel mas_makeConstraints:^(MASConstraintMaker* make) {
        make.left.mas_equalTo(12);
        make.centerY.equalTo(bar);
        make.width.mas_equalTo(70);
    }];

    UIButton* reset = [UIButton buttonWithType:UIButtonTypeSystem];
    [reset setTitle:@"Reset" forState:UIControlStateNormal];
    [reset addTarget:self action:@selector(reset) forControlEvents:UIControlEventTouchUpInside];
    [bar addSubview:reset];
    [reset mas_makeConstraints:^(MASConstraintMaker* make) {
        make.left.equalTo(cancel.mas_right).offset(4);
        make.centerY.equalTo(bar);
        make.width.mas_equalTo(60);
    }];

    UILabel* title = [UILabel new];
    title.text = @"Playback Config";
    title.font = [UIFont systemFontOfSize:15 weight:UIFontWeightSemibold];
    title.textAlignment = NSTextAlignmentCenter;
    [bar addSubview:title];
    [title mas_makeConstraints:^(MASConstraintMaker* make) {
        make.center.equalTo(bar);
    }];

    UIButton* apply = [UIButton buttonWithType:UIButtonTypeSystem];
    [apply setTitle:@"Apply" forState:UIControlStateNormal];
    apply.titleLabel.font = [UIFont systemFontOfSize:16 weight:UIFontWeightSemibold];
    [apply addTarget:self action:@selector(apply) forControlEvents:UIControlEventTouchUpInside];
    [bar addSubview:apply];
    [apply mas_makeConstraints:^(MASConstraintMaker* make) {
        make.right.mas_equalTo(-12);
        make.centerY.equalTo(bar);
        make.width.mas_equalTo(60);
    }];
}

- (void)setupForm {
    self.scrollView = [UIScrollView new];
    self.scrollView.keyboardDismissMode = UIScrollViewKeyboardDismissModeOnDrag;
    [self.view addSubview:self.scrollView];
    [self.scrollView mas_makeConstraints:^(MASConstraintMaker* make) {
        make.top.equalTo(self.view.mas_safeAreaLayoutGuideTop).offset(56);
        make.left.right.bottom.equalTo(self.view);
    }];

    self.stack = [UIStackView new];
    self.stack.axis = UILayoutConstraintAxisVertical;
    self.stack.spacing = 8;
    self.stack.layoutMargins = UIEdgeInsetsMake(12, 16, 16, 16);
    self.stack.layoutMarginsRelativeArrangement = YES;
    [self.scrollView addSubview:self.stack];
    [self.stack mas_makeConstraints:^(MASConstraintMaker* make) {
        make.edges.equalTo(self.scrollView);
        make.width.equalTo(self.scrollView);
    }];

    NSString* group = nil;
    for (NSDictionary<NSString*, NSString*>* spec in FieldSpecs()) {
        if (![spec[@"group"] isEqualToString:group]) {
            group = spec[@"group"];
            [self.stack addArrangedSubview:[self makeGroupLabel:group]];
        }
        [self.stack addArrangedSubview:[self makeFieldRow:spec[@"label"] key:spec[@"key"]]];
        if ([spec[@"key"] isEqualToString:kDnsTtl]) {
            [self.stack addArrangedSubview:[self makeLabel:@"preResolveUrls (one per line)"]];
            self.urlsView = [UITextView new];
            self.urlsView.font = [UIFont systemFontOfSize:13];
            self.urlsView.autocorrectionType = UITextAutocorrectionTypeNo;
            self.urlsView.autocapitalizationType = UITextAutocapitalizationTypeNone;
            self.urlsView.layer.borderColor = [UIColor separatorColor].CGColor;
            self.urlsView.layer.borderWidth = 1.0;
            self.urlsView.layer.cornerRadius = 6;
            [self.urlsView mas_makeConstraints:^(MASConstraintMaker* make) {
                make.height.mas_equalTo(80);
            }];
            [self.stack addArrangedSubview:self.urlsView];
        }
    }

    self.errorLabel = [UILabel new];
    self.errorLabel.textColor = [UIColor systemRedColor];
    self.errorLabel.font = [UIFont systemFontOfSize:12];
    self.errorLabel.numberOfLines = 0;
    self.errorLabel.hidden = YES;
    [self.stack addArrangedSubview:self.errorLabel];

    UILabel* hint = [UILabel new];
    hint.text = @"Applying rebuilds the current playback; values are read when the player is created.";
    hint.textColor = [UIColor secondaryLabelColor];
    hint.font = [UIFont systemFontOfSize:12];
    hint.numberOfLines = 0;
    [self.stack addArrangedSubview:hint];
}

- (UILabel*)makeGroupLabel:(NSString*)text {
    UILabel* label = [UILabel new];
    label.text = text;
    label.font = [UIFont systemFontOfSize:15 weight:UIFontWeightSemibold];
    label.textColor = [UIColor systemBlueColor];
    [label mas_makeConstraints:^(MASConstraintMaker* make) {
        make.height.mas_equalTo(28);
    }];
    return label;
}

- (UILabel*)makeLabel:(NSString*)text {
    UILabel* label = [UILabel new];
    label.text = text;
    label.font = [UIFont systemFontOfSize:13];
    return label;
}

- (UIView*)makeFieldRow:(NSString*)label key:(NSString*)key {
    UIStackView* row = [UIStackView new];
    row.axis = UILayoutConstraintAxisHorizontal;
    row.spacing = 8;
    row.alignment = UIStackViewAlignmentCenter;

    UILabel* name = [self makeLabel:label];
    [name mas_makeConstraints:^(MASConstraintMaker* make) {
        make.width.mas_equalTo(170);
    }];
    [row addArrangedSubview:name];

    UITextField* field = [UITextField new];
    field.borderStyle = UITextBorderStyleRoundedRect;
    field.keyboardType = UIKeyboardTypeDecimalPad;
    field.font = [UIFont systemFontOfSize:13];
    field.autocorrectionType = UITextAutocorrectionTypeNo;
    [row addArrangedSubview:field];
    self.fields[key] = field;
    [row mas_makeConstraints:^(MASConstraintMaker* make) {
        make.height.mas_equalTo(36);
    }];
    return row;
}

- (void)loadConfig:(SlarkFlowConfig*)config {
    NSDictionary<NSString*, NSNumber*>* values = @{
        kSourceLow : @(config.source.lowWatermarkSec),
        kSourceHigh : @(config.source.highWatermarkSec),
        kBufferingStart : @(config.buffering.startBufferSec),
        kBufferingEnter : @(config.buffering.rebufferEnterSec),
        kBufferingExit : @(config.buffering.rebufferExitSec),
        kDnsTtl : @(config.dns.cacheTtlSec),
        kNotifyProgress : @(config.notify.progressStepSec),
        kNotifyCache : @(config.notify.cacheIntervalSec),
    };
    for (NSString* key in values) {
        self.fields[key].text = FormatDouble(values[key].doubleValue);
    }
    self.urlsView.text = [config.dns.preResolveUrls componentsJoinedByString:@"\n"];
    self.errorLabel.hidden = YES;
}

- (void)showError:(NSString*)message {
    self.errorLabel.text = message;
    self.errorLabel.hidden = NO;
}

- (void)cancel {
    if (self.onCancel) {
        self.onCancel();
    }
}

- (void)reset {
    [self loadConfig:[SlarkFlowConfig new]];
}

- (void)apply {
    NSMutableDictionary<NSString*, NSNumber*>* numbers = [NSMutableDictionary dictionary];
    for (NSDictionary<NSString*, NSString*>* spec in FieldSpecs()) {
        double value = 0.0;
        if (!ParseDouble(self.fields[spec[@"key"]].text, &value)) {
            [self showError:[NSString stringWithFormat:@"Invalid value for %@", spec[@"label"]]];
            return;
        }
        numbers[spec[@"key"]] = @(value);
    }

    NSMutableCharacterSet* separators = [NSMutableCharacterSet newlineCharacterSet];
    [separators addCharactersInString:@","];
    NSArray<NSString*>* urls = [self.urlsView.text componentsSeparatedByCharactersInSet:separators];
    NSMutableArray<NSString*>* preResolveUrls = [NSMutableArray array];
    for (NSString* url in urls) {
        NSString* trimmed =
            [url stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
        if (trimmed.length > 0) {
            [preResolveUrls addObject:trimmed];
        }
    }

    SlarkFlowConfig* config = [SlarkFlowConfig new];
    config.source.lowWatermarkSec = numbers[kSourceLow].doubleValue;
    config.source.highWatermarkSec = numbers[kSourceHigh].doubleValue;
    config.buffering.startBufferSec = numbers[kBufferingStart].doubleValue;
    config.buffering.rebufferEnterSec = numbers[kBufferingEnter].doubleValue;
    config.buffering.rebufferExitSec = numbers[kBufferingExit].doubleValue;
    config.dns.cacheTtlSec = numbers[kDnsTtl].doubleValue;
    config.dns.preResolveUrls = preResolveUrls;
    config.notify.progressStepSec = numbers[kNotifyProgress].doubleValue;
    config.notify.cacheIntervalSec = numbers[kNotifyCache].doubleValue;

    if (!ConfigIsValid(config)) {
        [self showError:@"Invalid values: check all fields and the watermark / buffering ordering."];
        return;
    }

    [self endEditing];
    [DemoFlowConfig shared].config = config;
    if (self.onApply) {
        self.onApply();
    }
}

- (void)keyboardChanged:(NSNotification*)notification {
    CGRect keyboardFrame = [notification.userInfo[UIKeyboardFrameEndUserInfoKey] CGRectValue];
    CGRect scrollFrame = [self.scrollView convertRect:self.scrollView.bounds
                                               toView:self.scrollView.window];
    CGFloat bottomInset =
        [notification.name isEqualToString:UIKeyboardWillHideNotification]
            ? 0.0
            : MAX(0.0, CGRectGetMaxY(scrollFrame) - CGRectGetMinY(keyboardFrame));
    UIEdgeInsets insets = self.scrollView.contentInset;
    insets.bottom = bottomInset;
    self.scrollView.contentInset = insets;
    self.scrollView.scrollIndicatorInsets = insets;
}

@end
