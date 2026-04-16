///////////////////////////////////////////////////////////////////////////////////////////////////
//
// CDTPressureLoggerMac.mm: native macOS frontend for the portable Pfeiffer pressure logger.
//
// ------------------------------------------------------------------------------------------------
//
// Description:
/// This frontend mirrors the Python pressure logger layout more closely: connection block,
/// measurement status, channel cards, plot selection, message area, raw command box and a
/// collapsible control section. The shared 'CPressureLoggerAppEngine' still owns all protocol,
/// monitoring and CSV logic so that the UI remains a native shell around one common application
/// layer.
//
// Please announce changes and hints to support@n-cdt.com
// Copyright (c) 2026 CDT GmbH
// All rights reserved.
//
///////////////////////////////////////////////////////////////////////////////////////////////////


#import <Cocoa/Cocoa.h>

#include "PressureLoggerAppEngine.h"

#include <fstream>
#include <iomanip>
#include <sstream>


namespace
{
	NSString *_ToNSString( const std::string& i_Text )
	{
		return [NSString stringWithUTF8String: i_Text.length() ? i_Text.c_str() : ""];
	}


	std::string _ToStdString( NSString *i_Text )
	{
		if ( i_Text == nil )
			return "";
		const char *raw_text = [i_Text UTF8String];
		return std::string( raw_text ? raw_text : "" );
	}


	NSFont *_MainFont()
	{
		NSFont *font = [NSFont fontWithName:@"Avenir Next" size:13.0];
		if ( font == nil )
			font = [NSFont systemFontOfSize:13.0];
		return font;
	}


	NSFont *_SmallBoldFont()
	{
		NSFont *font = [NSFont fontWithName:@"Avenir Next Demi Bold" size:12.0];
		if ( font == nil )
			font = [NSFont boldSystemFontOfSize:12.0];
		return font;
	}


	NSFont *_ValueFont()
	{
		NSFont *font = [NSFont fontWithName:@"Avenir Next Demi Bold" size:26.0];
		if ( font == nil )
			font = [NSFont boldSystemFontOfSize:26.0];
		return font;
	}


	NSFont *_MonoFont()
	{
		NSFont *font = [NSFont fontWithName:@"Menlo" size:12.0];
		if ( font == nil )
			font = [NSFont monospacedSystemFontOfSize:12.0 weight:NSFontWeightRegular];
		return font;
	}


	NSColor *_LineColors( const int index )
	{
		switch ( index )
		{
			case 0: return [NSColor colorWithCalibratedRed:0.10 green:0.45 blue:0.80 alpha:1.0];
			case 1: return [NSColor colorWithCalibratedRed:0.94 green:0.52 blue:0.06 alpha:1.0];
			case 2: return [NSColor colorWithCalibratedRed:0.20 green:0.63 blue:0.17 alpha:1.0];
			case 3: return [NSColor colorWithCalibratedRed:0.82 green:0.24 blue:0.20 alpha:1.0];
			case 4: return [NSColor colorWithCalibratedRed:0.49 green:0.35 blue:0.21 alpha:1.0];
			default:return [NSColor colorWithCalibratedRed:0.43 green:0.32 blue:0.67 alpha:1.0];
		}
	}
}


@interface PressureIndicatorView : NSView
- (void)setIndicatorColor:(NSColor *)color;
@end


@implementation PressureIndicatorView
{
	NSColor *_indicatorColor;
}


- (instancetype)initWithFrame:(NSRect)frame
{
	self = [super initWithFrame:frame];
	if ( self )
		_indicatorColor = [NSColor colorWithCalibratedWhite:0.70 alpha:1.0];
	return self;
}


- (void)setIndicatorColor:(NSColor *)color
{
	_indicatorColor = color;
	[self setNeedsDisplay:YES];
}


- (void)drawRect:(NSRect)dirtyRect
{
	(void) dirtyRect;

	NSRect circleRect = NSInsetRect( [self bounds], 2.0, 2.0 );
	NSBezierPath *path = [NSBezierPath bezierPathWithOvalInRect:circleRect];
	[_indicatorColor setFill];
	[path fill];
	[[NSColor colorWithCalibratedWhite:0.45 alpha:1.0] setStroke];
	[path setLineWidth:1.0];
	[path stroke];
}

@end


@interface PressureChannelCardView : NSBox
- (void)setCombinedLabel:(NSString *)title;
- (void)updatePressure:(double)value statusCode:(int)statusCode statusText:(NSString *)statusText;
@end


@implementation PressureChannelCardView
{
	NSTextField *_titleLabel;
	NSTextField *_valueLabel;
	NSTextField *_statusLabel;
	PressureIndicatorView *_okIndicator;
	PressureIndicatorView *_offIndicator;
	PressureIndicatorView *_orIndicator;
}


- (instancetype)initWithFrame:(NSRect)frame
{
	self = [super initWithFrame:frame];
	if ( self )
	{
		[self setBoxType:NSBoxCustom];
		[self setBorderColor:[NSColor colorWithCalibratedWhite:0.86 alpha:1.0]];
		[self setCornerRadius:8.0];
		[self setFillColor:[NSColor colorWithCalibratedWhite:0.98 alpha:1.0]];
		[self setTitlePosition:NSNoTitle];

		NSView *content = [self contentView];

		_titleLabel = [[NSTextField alloc] initWithFrame:NSMakeRect( 16, frame.size.height - 34, frame.size.width - 32, 20 )];
		[_titleLabel setEditable:NO];
		[_titleLabel setBezeled:NO];
		[_titleLabel setBordered:NO];
		[_titleLabel setDrawsBackground:NO];
		[_titleLabel setSelectable:NO];
		[_titleLabel setFont:_SmallBoldFont()];
		[content addSubview:_titleLabel];

		_valueLabel = [[NSTextField alloc] initWithFrame:NSMakeRect( 16, frame.size.height - 86, frame.size.width - 32, 34 )];
		[_valueLabel setEditable:NO];
		[_valueLabel setBezeled:NO];
		[_valueLabel setBordered:NO];
		[_valueLabel setDrawsBackground:NO];
		[_valueLabel setSelectable:NO];
		[_valueLabel setFont:_ValueFont()];
		[_valueLabel setStringValue:@"—"];
		[content addSubview:_valueLabel];

		_statusLabel = [[NSTextField alloc] initWithFrame:NSMakeRect( 16, frame.size.height - 118, frame.size.width - 32, 22 )];
		[_statusLabel setEditable:NO];
		[_statusLabel setBezeled:NO];
		[_statusLabel setBordered:NO];
		[_statusLabel setDrawsBackground:NO];
		[_statusLabel setSelectable:NO];
		[_statusLabel setFont:_MainFont()];
		[_statusLabel setStringValue:@"—"];
		[content addSubview:_statusLabel];

		_okIndicator = [[PressureIndicatorView alloc] initWithFrame:NSMakeRect( frame.size.width - 190, 16, 18, 18 )];
		_offIndicator = [[PressureIndicatorView alloc] initWithFrame:NSMakeRect( frame.size.width - 110, 16, 18, 18 )];
		_orIndicator = [[PressureIndicatorView alloc] initWithFrame:NSMakeRect( frame.size.width - 40, 16, 18, 18 )];
		[content addSubview:_okIndicator];
		[content addSubview:_offIndicator];
		[content addSubview:_orIndicator];

		NSArray<NSString *> *labels = @[@"OK", @"AUS", @"OR"];
		NSArray<NSNumber *> *xValues = @[@(frame.size.width - 212), @(frame.size.width - 133), @(frame.size.width - 63)];
		for ( NSInteger i = 0; i < labels.count; i++ )
		{
			NSTextField *label = [[NSTextField alloc] initWithFrame:NSMakeRect( [xValues[i] doubleValue], 12, 28, 22 )];
			[label setEditable:NO];
			[label setBezeled:NO];
			[label setBordered:NO];
			[label setDrawsBackground:NO];
			[label setSelectable:NO];
			[label setAlignment:NSTextAlignmentRight];
			[label setFont:_MainFont()];
			[label setStringValue:labels[i]];
			[content addSubview:label];
		}
	}
	return self;
}


- (void)setCombinedLabel:(NSString *)title
{
	[_titleLabel setStringValue:title ?: @""];
}


- (void)updatePressure:(double)value statusCode:(int)statusCode statusText:(NSString *)statusText
{
	if ( std::isfinite( value ) )
		[_valueLabel setStringValue:_ToNSString( ([&]() -> std::string { std::stringstream stream; stream.setf( std::ios::scientific ); stream << std::setprecision( 4 ) << value; return stream.str(); })() )];
	else
		[_valueLabel setStringValue:@"—"];

	[_statusLabel setStringValue:statusText ?: @"—"];

	[_okIndicator setIndicatorColor:[NSColor colorWithCalibratedWhite:0.75 alpha:1.0]];
	[_offIndicator setIndicatorColor:[NSColor colorWithCalibratedWhite:0.75 alpha:1.0]];
	[_orIndicator setIndicatorColor:[NSColor colorWithCalibratedWhite:0.75 alpha:1.0]];

	if ( statusCode == 0 )
		[_okIndicator setIndicatorColor:[NSColor colorWithCalibratedRed:0.18 green:0.49 blue:0.20 alpha:1.0]];
	else if ( statusCode == 4 )
		[_offIndicator setIndicatorColor:[NSColor colorWithCalibratedRed:0.98 green:0.66 blue:0.14 alpha:1.0]];
	else if ( statusCode == 2 )
		[_orIndicator setIndicatorColor:[NSColor colorWithCalibratedRed:0.78 green:0.16 blue:0.16 alpha:1.0]];
}

@end


@interface PressurePlotView : NSView
- (void)updateWithSnapshot:(const PressureLoggerStateSnapshot &)snapshot engine:(CPressureLoggerAppEngine *)engine visibleChannels:(const bool *)visibleChannels channelCount:(NSInteger)channelCount;
@end


@implementation PressurePlotView
{
	PressureLoggerStateSnapshot _snapshot;
	CPressureLoggerAppEngine *_engine;
	bool _visibleChannels[6];
	NSInteger _channelCount;
}


- (instancetype)initWithFrame:(NSRect)frame
{
	self = [super initWithFrame:frame];
	if ( self )
	{
		for ( int i = 0; i < 6; i++ )
			_visibleChannels[i] = (i < 2);
		_channelCount = 2;
		[self setWantsLayer:YES];
	}
	return self;
}


- (void)updateWithSnapshot:(const PressureLoggerStateSnapshot &)snapshot engine:(CPressureLoggerAppEngine *)engine visibleChannels:(const bool *)visibleChannels channelCount:(NSInteger)channelCount
{
	_snapshot = snapshot;
	_engine = engine;
	_channelCount = channelCount;
	for ( int i = 0; i < 6; i++ )
		_visibleChannels[i] = visibleChannels ? visibleChannels[i] : (i < 2);
	[self setNeedsDisplay:YES];
}


- (void)drawRect:(NSRect)dirtyRect
{
	(void) dirtyRect;

	NSRect bounds = [self bounds];
	[[NSColor whiteColor] setFill];
	NSRectFill( bounds );

	const CGFloat left = 80.0;
	const CGFloat right = 24.0;
	const CGFloat top = 28.0;
	const CGFloat bottom = 70.0;
	NSRect plotRect = NSMakeRect( left, bottom, bounds.size.width - left - right, bounds.size.height - top - bottom );

	[[NSColor colorWithCalibratedWhite:0.20 alpha:1.0] setStroke];
	NSBezierPath *border = [NSBezierPath bezierPathWithRect:plotRect];
	[border setLineWidth:1.0];
	[border stroke];

	double xMin = -0.05;
	double xMax = 0.05;
	double yMin = 1.0;
	double yMax = 10.0;
	bool hasData = false;

	std::vector<std::vector<double>> timesByChannel;
	std::vector<std::vector<double>> valuesByChannel;
	timesByChannel.resize( _channelCount );
	valuesByChannel.resize( _channelCount );

	if ( _engine != 0 )
	{
		for ( NSInteger channel = 1; channel <= _channelCount; channel++ )
		{
			if ( !_visibleChannels[channel - 1] )
				continue;

			_engine->BuildPlotSeries( _snapshot, static_cast<BYTE>( channel ), &timesByChannel[channel - 1], &valuesByChannel[channel - 1] );
			for ( size_t i = 0; i < timesByChannel[channel - 1].size(); i++ )
			{
				xMin = hasData ? std::min( xMin, timesByChannel[channel - 1][i] ) : timesByChannel[channel - 1][i];
				xMax = hasData ? std::max( xMax, timesByChannel[channel - 1][i] ) : std::max( timesByChannel[channel - 1][i], 0.05 );
				const double value = valuesByChannel[channel - 1][i];
				if ( std::isfinite( value ) && (value > 0.0) )
				{
					yMin = hasData ? std::min( yMin, value ) : value;
					yMax = hasData ? std::max( yMax, value ) : value;
					hasData = true;
				}
				else if ( !hasData )
				{
					hasData = false;
				}
			}
		}
	}

	if ( !hasData )
	{
		xMin = -0.05;
		xMax = 0.05;
		yMin = 1.0;
		yMax = 10.0;
	}
	else
	{
		if ( fabs( xMax - xMin ) < 1e-9 )
		{
			xMin -= 0.5;
			xMax += 0.5;
		}
		const double logMin = floor( log10( std::max( yMin, 1e-12 ) ) );
		const double logMax = ceil( log10( std::max( yMax, yMin * 1.01 ) ) );
		yMin = pow( 10.0, logMin );
		yMax = pow( 10.0, std::max( logMax, logMin + 1.0 ) );
	}

	[[NSColor colorWithCalibratedWhite:0.80 alpha:1.0] setStroke];
	for ( int i = 0; i <= 5; i++ )
	{
		const CGFloat x = plotRect.origin.x + plotRect.size.width * static_cast<CGFloat>( i ) / 5.0;
		NSBezierPath *vertical = [NSBezierPath bezierPath];
		[vertical moveToPoint:NSMakePoint( x, plotRect.origin.y )];
		[vertical lineToPoint:NSMakePoint( x, plotRect.origin.y + plotRect.size.height )];
		[vertical stroke];
	}

	for ( int i = 0; i <= 4; i++ )
	{
		const double logValue = log10( yMin ) + (log10( yMax ) - log10( yMin )) * static_cast<double>( i ) / 4.0;
		const CGFloat y = plotRect.origin.y + plotRect.size.height * static_cast<CGFloat>( i ) / 4.0;
		NSBezierPath *horizontal = [NSBezierPath bezierPath];
		[horizontal moveToPoint:NSMakePoint( plotRect.origin.x, y )];
		[horizontal lineToPoint:NSMakePoint( plotRect.origin.x + plotRect.size.width, y )];
		[horizontal stroke];

		const double labelValue = pow( 10.0, logValue );
		NSString *label = _ToNSString( ([&]() -> std::string { std::stringstream stream; stream.setf( std::ios::scientific ); stream << std::setprecision( 1 ) << labelValue; return stream.str(); })() );
		[label drawAtPoint:NSMakePoint( 10, y - 8 ) withAttributes:@{NSFontAttributeName:_MonoFont(), NSForegroundColorAttributeName:[NSColor blackColor]}];
	}

	NSString *xLabel = @"Zeit seit Messstart [s]";
	[xLabel drawAtPoint:NSMakePoint( plotRect.origin.x + plotRect.size.width / 2.0 - 70, 18 ) withAttributes:@{NSFontAttributeName:_MainFont()}];

	NSString *yLabel = @"Druck";
	[NSGraphicsContext saveGraphicsState];
	NSAffineTransform *transform = [NSAffineTransform transform];
	[transform translateXBy:24 yBy:plotRect.origin.y + plotRect.size.height / 2.0];
	[transform rotateByDegrees:90.0];
	[transform concat];
	[yLabel drawAtPoint:NSMakePoint( 0, 0 ) withAttributes:@{NSFontAttributeName:_MainFont()}];
	[NSGraphicsContext restoreGraphicsState];

	for ( NSInteger channel = 1; channel <= _channelCount; channel++ )
	{
		if ( !_visibleChannels[channel - 1] )
			continue;

		const std::vector<double>& times = timesByChannel[channel - 1];
		const std::vector<double>& values = valuesByChannel[channel - 1];
		if ( times.empty() )
			continue;

		NSBezierPath *path = [NSBezierPath bezierPath];
		[path setLineWidth:2.0];
		[_LineColors( static_cast<int>( channel - 1 ) ) setStroke];

		bool pathStarted = false;
		for ( size_t i = 0; i < times.size(); i++ )
		{
			const double value = values[i];
			if ( !std::isfinite( value ) || (value <= 0.0) )
			{
				pathStarted = false;
				continue;
			}

			const CGFloat x = plotRect.origin.x + static_cast<CGFloat>( (times[i] - xMin) / (xMax - xMin) ) * plotRect.size.width;
			const double normalizedY = (log10( value ) - log10( yMin )) / (log10( yMax ) - log10( yMin ));
			const CGFloat y = plotRect.origin.y + static_cast<CGFloat>( normalizedY ) * plotRect.size.height;
			if ( !pathStarted )
			{
				[path moveToPoint:NSMakePoint( x, y )];
				pathStarted = true;
			}
			else
			{
				[path lineToPoint:NSMakePoint( x, y )];
			}
		}
		[path stroke];
	}

	NSRect legendRect = NSMakeRect( plotRect.origin.x + plotRect.size.width - 250, plotRect.origin.y + plotRect.size.height - 70, 230, 52 + 20 * _channelCount );
	[[NSColor colorWithCalibratedWhite:1.0 alpha:0.95] setFill];
	[[NSBezierPath bezierPathWithRoundedRect:legendRect xRadius:6 yRadius:6] fill];
	[[NSColor colorWithCalibratedWhite:0.85 alpha:1.0] setStroke];
	[[NSBezierPath bezierPathWithRoundedRect:legendRect xRadius:6 yRadius:6] stroke];

	CGFloat legendY = legendRect.origin.y + legendRect.size.height - 24;
	for ( NSInteger channel = 1; channel <= _channelCount; channel++ )
	{
		if ( !_visibleChannels[channel - 1] )
			continue;

		[_LineColors( static_cast<int>( channel - 1 ) ) setStroke];
		NSBezierPath *legendLine = [NSBezierPath bezierPath];
		[legendLine moveToPoint:NSMakePoint( legendRect.origin.x + 12, legendY + 8 )];
		[legendLine lineToPoint:NSMakePoint( legendRect.origin.x + 42, legendY + 8 )];
		[legendLine setLineWidth:2.0];
		[legendLine stroke];

		NSString *label = _ToNSString( (channel >= 1 && channel <= _snapshot.CombinedChannelLabels.size())
									   ? _snapshot.CombinedChannelLabels[channel - 1]
									   : _engine->FormatCombinedChannelLabel( _snapshot.Setup.DeviceType, static_cast<BYTE>( channel ) ) );
		[label drawAtPoint:NSMakePoint( legendRect.origin.x + 54, legendY ) withAttributes:@{NSFontAttributeName:_MainFont()}];
		legendY -= 22;
	}
}

@end


@interface CDTPressureLoggerDelegate : NSObject <NSApplicationDelegate>
@end


@implementation CDTPressureLoggerDelegate
{
	NSWindow *_window;
	NSScrollView *_leftScrollView;
	NSView *_leftContentView;

	NSPopUpButton *_devicePopup;
	NSPopUpButton *_portPopup;
	PressureIndicatorView *_connectionIndicator;
	PressureIndicatorView *_measurementIndicator;
	PressureIndicatorView *_fileIndicator;
	NSTextField *_measurementStatusLabel;
	NSTextField *_samplesStatusLabel;
	NSTextField *_fileStatusLabel;
	NSPopUpButton *_intervalPopup;
	NSButton *_longTermCheck;
	NSTextField *_longTermField;
	NSButton *_liveOnlyCheck;
	NSTextField *_csvField;

	NSButton *_plotChecks[6];
	PressureChannelCardView *_channelCards[6];

	NSTextView *_messagesView;
	NSTextField *_rawField;

	NSButton *_toggleControlButton;
	NSBox *_controlBox;
	NSPopUpButton *_controlChannelPopup;
	NSPopUpButton *_unitPopup;
	NSPopUpButton *_filterPopup;
	NSTextField *_calibrationField;
	NSPopUpButton *_fsrPopup;
	NSPopUpButton *_ofcPopup;
	NSTextField *_displayNameField;
	NSPopUpButton *_digitsPopup;
	NSTextField *_contrastField;
	NSTextField *_screensaveField;

	PressurePlotView *_plotView;
	NSWindow *_externalPlotWindow;
	PressurePlotView *_externalPlotView;
	NSWindow *_csvPlotWindow;
	PressurePlotView *_csvPlotView;

	NSTimer *_pollTimer;
	CPressureLoggerAppEngine _engine;
	PressureLoggerStateSnapshot _csvSnapshot;
	BOOL _controlVisible;
	BOOL _plotVisible[6];
}


- (void)applicationDidFinishLaunching:(NSNotification *)notification
{
	(void) notification;

	for ( int i = 0; i < 6; i++ )
		_plotVisible[i] = (i < 2);

	[self buildMenu];
	[self buildWindow];
	[self applyDefaults];
	[self refreshPorts:nil];
	[self updateDeviceProfile];
	[self refreshUi:nil];

	_pollTimer = [NSTimer scheduledTimerWithTimeInterval:0.25 target:self selector:@selector(refreshUi:) userInfo:nil repeats:YES];

	[_window makeKeyAndOrderFront:nil];
	[NSApp activateIgnoringOtherApps:YES];
}


- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender
{
	(void) sender;
	return YES;
}


- (void)applicationWillTerminate:(NSNotification *)notification
{
	(void) notification;
	[_pollTimer invalidate];
	_engine.Disconnect();
}


- (void)buildMenu
{
	NSMenu *mainMenu = [[NSMenu alloc] initWithTitle:@"MainMenu"];
	NSMenuItem *appMenuItem = [[NSMenuItem alloc] initWithTitle:@"CDT pressure logger" action:nil keyEquivalent:@""];
	[mainMenu addItem:appMenuItem];

	NSMenu *appMenu = [[NSMenu alloc] initWithTitle:@"CDT pressure logger"];
	[appMenu addItem:[[NSMenuItem alloc] initWithTitle:@"Quit CDT pressure logger" action:@selector(terminate:) keyEquivalent:@"q"]];
	[appMenuItem setSubmenu:appMenu];
	[NSApp setMainMenu:mainMenu];
}


- (NSBox *)createSectionBox:(NSRect)frame title:(NSString *)title inView:(NSView *)view
{
	NSBox *box = [[NSBox alloc] initWithFrame:frame];
	[box setTitle:title];
	[box setBoxType:NSBoxCustom];
	[box setBorderColor:[NSColor colorWithCalibratedWhite:0.88 alpha:1.0]];
	[box setCornerRadius:8.0];
	[box setFillColor:[NSColor colorWithCalibratedWhite:0.98 alpha:1.0]];
	[box setTitleFont:[NSFont fontWithName:@"Avenir Next Demi Bold" size:15.0] ?: [NSFont boldSystemFontOfSize:15.0]];
	[view addSubview:box];
	return box;
}


- (NSTextField *)createLabel:(NSRect)frame text:(NSString *)text inView:(NSView *)view bold:(BOOL)bold
{
	NSTextField *label = [[NSTextField alloc] initWithFrame:frame];
	[label setStringValue:text];
	[label setEditable:NO];
	[label setBezeled:NO];
	[label setBordered:NO];
	[label setDrawsBackground:NO];
	[label setSelectable:NO];
	[label setFont:bold ? _SmallBoldFont() : _MainFont()];
	[view addSubview:label];
	return label;
}


- (NSTextField *)createField:(NSRect)frame text:(NSString *)text inView:(NSView *)view
{
	NSTextField *field = [[NSTextField alloc] initWithFrame:frame];
	[field setStringValue:text];
	[field setFont:_MainFont()];
	[view addSubview:field];
	return field;
}


- (NSPopUpButton *)createPopup:(NSRect)frame titles:(NSArray<NSString *> *)titles inView:(NSView *)view
{
	NSPopUpButton *popup = [[NSPopUpButton alloc] initWithFrame:frame pullsDown:NO];
	[popup addItemsWithTitles:titles];
	[popup setFont:_MainFont()];
	[view addSubview:popup];
	return popup;
}


- (NSButton *)createButton:(NSRect)frame title:(NSString *)title target:(id)target action:(SEL)action inView:(NSView *)view
{
	NSButton *button = [[NSButton alloc] initWithFrame:frame];
	[button setTitle:title];
	[button setFont:_MainFont()];
	[button setTarget:target];
	[button setAction:action];
	[button setBezelStyle:NSBezelStyleRounded];
	[view addSubview:button];
	return button;
}


- (NSButton *)createCheckbox:(NSRect)frame title:(NSString *)title target:(id)target action:(SEL)action inView:(NSView *)view
{
	NSButton *button = [[NSButton alloc] initWithFrame:frame];
	[button setTitle:title];
	[button setButtonType:NSButtonTypeSwitch];
	[button setFont:_MainFont()];
	[button setTarget:target];
	[button setAction:action];
	[view addSubview:button];
	return button;
}


- (NSTextView *)createTextView:(NSRect)frame inView:(NSView *)view
{
	NSScrollView *scrollView = [[NSScrollView alloc] initWithFrame:frame];
	[scrollView setHasVerticalScroller:YES];
	[scrollView setBorderType:NSBezelBorder];

	NSTextView *textView = [[NSTextView alloc] initWithFrame:NSMakeRect( 0, 0, frame.size.width, frame.size.height )];
	[textView setEditable:NO];
	[textView setFont:_MonoFont()];
	[textView setAutomaticQuoteSubstitutionEnabled:NO];
	[textView setAutomaticDashSubstitutionEnabled:NO];
	[scrollView setDocumentView:textView];
	[view addSubview:scrollView];
	return textView;
}


- (void)buildWindow
{
	NSRect frame = NSMakeRect( 0, 0, 1780, 980 );
	_window = [[NSWindow alloc] initWithContentRect:frame
										  styleMask:(NSWindowStyleMaskTitled |
													 NSWindowStyleMaskClosable |
													 NSWindowStyleMaskMiniaturizable |
													 NSWindowStyleMaskResizable)
											backing:NSBackingStoreBuffered
											  defer:NO];
	[_window setTitle:@"CDT pressure logger"];
	[_window setBackgroundColor:[NSColor whiteColor]];
	[_window center];

	NSView *contentView = [_window contentView];

	_leftScrollView = [[NSScrollView alloc] initWithFrame:NSMakeRect( 0, 0, 840, frame.size.height )];
	[_leftScrollView setHasVerticalScroller:YES];
	[_leftScrollView setBorderType:NSNoBorder];
	_leftContentView = [[NSView alloc] initWithFrame:NSMakeRect( 0, 0, 830, 1240 )];
	[_leftScrollView setDocumentView:_leftContentView];
	[contentView addSubview:_leftScrollView];

	NSView *rightView = [[NSView alloc] initWithFrame:NSMakeRect( 850, 0, frame.size.width - 850, frame.size.height )];
	[rightView setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
	[contentView addSubview:rightView];

	[self createLabel:NSMakeRect( 16, 1175, 340, 34 ) text:@"CDT pressure logger" inView:_leftContentView bold:YES];

	NSBox *connectionBox = [self createSectionBox:NSMakeRect( 10, 990, 810, 180 ) title:@"Verbindung / Messung / Status" inView:_leftContentView];
	NSView *connectionView = [connectionBox contentView];

	[self createLabel:NSMakeRect( 16, 116, 48, 24 ) text:@"Gerät:" inView:connectionView bold:NO];
	_connectionIndicator = [[PressureIndicatorView alloc] initWithFrame:NSMakeRect( 218, 116, 18, 18 )];
	[connectionView addSubview:_connectionIndicator];
	_devicePopup = [self createPopup:NSMakeRect( 280, 110, 230, 28 ) titles:@[@"TPG 262", @"MaxiGauge"] inView:connectionView];
	[_devicePopup setTarget:self];
	[_devicePopup setAction:@selector(deviceChanged:)];
	[self createLabel:NSMakeRect( 530, 116, 35, 24 ) text:@"Port" inView:connectionView bold:NO];
	_portPopup = [self createPopup:NSMakeRect( 575, 110, 200, 28 ) titles:@[@""] inView:connectionView];
	[_portPopup setTarget:self];
	[_portPopup setAction:@selector(selectionChanged:)];

	[self createButton:NSMakeRect( 16, 72, 195, 30 ) title:@"Verbinden" target:self action:@selector(connectAction:) inView:connectionView];
	[self createButton:NSMakeRect( 220, 72, 56, 30 ) title:@"Neu + Start" target:self action:@selector(startNewMeasurementAction:) inView:connectionView];
	[self createButton:NSMakeRect( 280, 72, 240, 30 ) title:@"Aktualisieren" target:self action:@selector(refreshPorts:) inView:connectionView];
	[self createButton:NSMakeRect( 565, 72, 195, 30 ) title:@"Werkreset" target:self action:@selector(factoryResetAction:) inView:connectionView];

	[self createLabel:NSMakeRect( 16, 34, 68, 24 ) text:@"Messung:" inView:connectionView bold:NO];
	_measurementIndicator = [[PressureIndicatorView alloc] initWithFrame:NSMakeRect( 218, 34, 18, 18 )];
	[connectionView addSubview:_measurementIndicator];
	_measurementStatusLabel = [self createLabel:NSMakeRect( 280, 34, 220, 24 ) text:@"Nicht verbunden" inView:connectionView bold:NO];
	_samplesStatusLabel = [self createLabel:NSMakeRect( 530, 34, 140, 24 ) text:@"Sam 0" inView:connectionView bold:NO];

	[self createButton:NSMakeRect( 16, 0, 195, 30 ) title:@"Logging starten" target:self action:@selector(startLoggingAction:) inView:connectionView];
	[self createButton:NSMakeRect( 220, 0, 56, 30 ) title:@"|" target:self action:@selector(stopLoggingAction:) inView:connectionView];
	[self createButton:NSMakeRect( 280, 0, 240, 30 ) title:@"Logging stoppen" target:self action:@selector(stopLoggingAction:) inView:connectionView];
	_liveOnlyCheck = [self createCheckbox:NSMakeRect( 530, 0, 260, 26 ) title:@"nur live anzeigen, nicht speichern" target:self action:@selector(refreshUi:) inView:connectionView];

	[self createLabel:NSMakeRect( 16, -38, 150, 24 ) text:@"Continuous Mode" inView:connectionView bold:NO];
	_intervalPopup = [self createPopup:NSMakeRect( 220, -42, 110, 28 ) titles:@[@"1 s"] inView:connectionView];
	_longTermCheck = [self createCheckbox:NSMakeRect( 340, -40, 140, 26 ) title:@"Langzeitmodus" target:self action:@selector(deviceChanged:) inView:connectionView];
	_longTermField = [self createField:NSMakeRect( 530, -42, 54, 28 ) text:@"60" inView:connectionView];
	[self createLabel:NSMakeRect( 590, -38, 150, 24 ) text:@"s (Standard 60)" inView:connectionView bold:NO];

	[self createLabel:NSMakeRect( 16, -76, 36, 24 ) text:@"CSV" inView:connectionView bold:NO];
	_csvField = [self createField:NSMakeRect( 220, -80, 545, 28 ) text:@"" inView:connectionView];
	[self createButton:NSMakeRect( 772, -80, 24, 28 ) title:@"…" target:self action:@selector(chooseCsvPathAction:) inView:connectionView];

	[self createButton:NSMakeRect( 16, -118, 195, 30 ) title:@"Durchsuchen" target:self action:@selector(chooseCsvPathAction:) inView:connectionView];
	_fileIndicator = [[PressureIndicatorView alloc] initWithFrame:NSMakeRect( 218, -114, 18, 18 )];
	[connectionView addSubview:_fileIndicator];
	_fileStatusLabel = [self createLabel:NSMakeRect( 470, -112, 320, 24 ) text:@"Datei: Keine Datei offen" inView:connectionView bold:NO];

	CGFloat cardsTop = 790;
	for ( int i = 0; i < 6; i++ )
	{
		const int row = i / 2;
		const int col = i % 2;
		_channelCards[i] = [[PressureChannelCardView alloc] initWithFrame:NSMakeRect( 12 + col * 408, cardsTop - row * 164, 396, 150 )];
		[_leftContentView addSubview:_channelCards[i]];
	}

	[self createLabel:NSMakeRect( 16, 300, 110, 24 ) text:@"Im Plot anzeigen:" inView:_leftContentView bold:NO];
	for ( int i = 0; i < 6; i++ )
	{
		_plotChecks[i] = [self createCheckbox:NSMakeRect( 145 + i * 44, 300, 42, 24 ) title:[NSString stringWithFormat:@"%d", i + 1] target:self action:@selector(plotVisibilityChanged:) inView:_leftContentView];
		[_plotChecks[i] setState:(i < 2) ? NSControlStateValueOn : NSControlStateValueOff];
	}

	[self createLabel:NSMakeRect( 16, 266, 90, 24 ) text:@"Meldungen" inView:_leftContentView bold:NO];
	_messagesView = [self createTextView:NSMakeRect( 10, 42, 810, 220 ) inView:_leftContentView];

	NSBox *rawBox = [self createSectionBox:NSMakeRect( 10, -40, 810, 72 ) title:@"Rohkommando" inView:_leftContentView];
	NSView *rawView = [rawBox contentView];
	_rawField = [self createField:NSMakeRect( 14, 12, 520, 28 ) text:@"" inView:rawView];
	[self createButton:NSMakeRect( 548, 10, 170, 30 ) title:@"Senden" target:self action:@selector(sendRawAction:) inView:rawView];
	[self createButton:NSMakeRect( 726, 10, 40, 30 ) title:@"i" target:self action:@selector(showRawHelpAction:) inView:rawView];

	_toggleControlButton = [self createButton:NSMakeRect( 10, -86, 810, 30 ) title:@"Steuerung / Parameter einblenden" target:self action:@selector(toggleControlAction:) inView:_leftContentView];
	_controlBox = [self createSectionBox:NSMakeRect( 10, -414, 810, 320 ) title:@"Steuerung / Parameter" inView:_leftContentView];
	NSView *controlView = [_controlBox contentView];

	[self createLabel:NSMakeRect( 16, 260, 40, 24 ) text:@"Kanal" inView:controlView bold:NO];
	_controlChannelPopup = [self createPopup:NSMakeRect( 70, 256, 90, 28 ) titles:@[@"1", @"2"] inView:controlView];
	[_controlChannelPopup setTarget:self];
	[_controlChannelPopup setAction:@selector(controlChannelChanged:)];
	[self createButton:NSMakeRect( 180, 254, 120, 30 ) title:@"Gauge EIN" target:self action:@selector(gaugeOnAction:) inView:controlView];
	[self createButton:NSMakeRect( 310, 254, 120, 30 ) title:@"Gauge AUS" target:self action:@selector(gaugeOffAction:) inView:controlView];
	[self createButton:NSMakeRect( 440, 254, 170, 30 ) title:@"Messwert jetzt lesen" target:self action:@selector(readNowAction:) inView:controlView];
	[self createButton:NSMakeRect( 620, 254, 170, 30 ) title:@"Aktivieren + prüfen" target:self action:@selector(activateVerifyAction:) inView:controlView];

	[self createLabel:NSMakeRect( 16, 220, 50, 24 ) text:@"Einheit" inView:controlView bold:NO];
	_unitPopup = [self createPopup:NSMakeRect( 70, 216, 120, 28 ) titles:@[@"mbar", @"Torr", @"Pa"] inView:controlView];
	[self createButton:NSMakeRect( 200, 214, 100, 30 ) title:@"Einheit setzen" target:self action:@selector(setUnitAction:) inView:controlView];
	[self createButton:NSMakeRect( 310, 214, 120, 30 ) title:@"Degas EIN" target:self action:@selector(degasOnAction:) inView:controlView];
	[self createButton:NSMakeRect( 440, 214, 120, 30 ) title:@"Degas AUS" target:self action:@selector(degasOffAction:) inView:controlView];
	[self createButton:NSMakeRect( 620, 214, 170, 30 ) title:@"Diagnose" target:self action:@selector(diagnoseAction:) inView:controlView];

	[self createLabel:NSMakeRect( 16, 180, 40, 24 ) text:@"Filter" inView:controlView bold:NO];
	_filterPopup = [self createPopup:NSMakeRect( 70, 176, 120, 28 ) titles:@[@"fast", @"standard", @"slow"] inView:controlView];
	[_filterPopup selectItemAtIndex:1];
	[self createButton:NSMakeRect( 200, 174, 100, 30 ) title:@"Filter setzen" target:self action:@selector(setFilterAction:) inView:controlView];
	[self createLabel:NSMakeRect( 320, 180, 90, 24 ) text:@"Kalibrierfaktor" inView:controlView bold:NO];
	_calibrationField = [self createField:NSMakeRect( 420, 176, 90, 28 ) text:@"1.000" inView:controlView];
	[self createButton:NSMakeRect( 520, 174, 90, 30 ) title:@"CAL setzen" target:self action:@selector(setCalibrationAction:) inView:controlView];

	[self createLabel:NSMakeRect( 16, 140, 40, 24 ) text:@"FSR" inView:controlView bold:NO];
	_fsrPopup = [self createPopup:NSMakeRect( 70, 136, 160, 28 ) titles:@[@"1000 mbar"] inView:controlView];
	[self createButton:NSMakeRect( 240, 134, 100, 30 ) title:@"FSR setzen" target:self action:@selector(setFsrAction:) inView:controlView];
	[self createLabel:NSMakeRect( 360, 140, 40, 24 ) text:@"OFC" inView:controlView bold:NO];
	_ofcPopup = [self createPopup:NSMakeRect( 410, 136, 100, 28 ) titles:@[@"off", @"on", @"auto"] inView:controlView];
	[self createButton:NSMakeRect( 520, 134, 100, 30 ) title:@"OFC setzen" target:self action:@selector(setOfcAction:) inView:controlView];

	[self createLabel:NSMakeRect( 16, 100, 92, 24 ) text:@"Anzeigename" inView:controlView bold:NO];
	_displayNameField = [self createField:NSMakeRect( 112, 96, 140, 28 ) text:@"Kanal 1" inView:controlView];
	[self createButton:NSMakeRect( 262, 94, 120, 30 ) title:@"Namen speichern" target:self action:@selector(setDisplayNameAction:) inView:controlView];
	[self createLabel:NSMakeRect( 400, 100, 40, 24 ) text:@"Digits" inView:controlView bold:NO];
	_digitsPopup = [self createPopup:NSMakeRect( 446, 96, 90, 28 ) titles:@[@"2", @"3"] inView:controlView];
	[_digitsPopup selectItemAtIndex:1];
	[self createButton:NSMakeRect( 546, 94, 80, 30 ) title:@"Setzen" target:self action:@selector(setDigitsAction:) inView:controlView];

	[self createLabel:NSMakeRect( 16, 60, 60, 24 ) text:@"Contrast" inView:controlView bold:NO];
	_contrastField = [self createField:NSMakeRect( 84, 56, 60, 28 ) text:@"10" inView:controlView];
	[self createButton:NSMakeRect( 152, 54, 100, 30 ) title:@"Contrast" target:self action:@selector(setContrastAction:) inView:controlView];
	[self createLabel:NSMakeRect( 272, 60, 88, 24 ) text:@"Screensave [h]" inView:controlView bold:NO];
	_screensaveField = [self createField:NSMakeRect( 368, 56, 60, 28 ) text:@"0" inView:controlView];
	[self createButton:NSMakeRect( 436, 54, 120, 30 ) title:@"Screensave" target:self action:@selector(setScreensaveAction:) inView:controlView];

	[_controlBox setHidden:YES];
	_controlVisible = NO;

	_plotView = [[PressurePlotView alloc] initWithFrame:NSMakeRect( 20, 50, rightView.frame.size.width - 40, rightView.frame.size.height - 80 )];
	[_plotView setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
	[rightView addSubview:_plotView];
	[self createButton:NSMakeRect( 20, 12, 120, 30 ) title:@"Externer Plot" target:self action:@selector(openExternalPlotAction:) inView:rightView];
	[self createButton:NSMakeRect( 150, 12, 120, 30 ) title:@"CSV plotten" target:self action:@selector(plotCsvAction:) inView:rightView];
}


- (void)applyDefaults
{
	[_devicePopup selectItemAtIndex:(_engine.GetLastDeviceType() == PressureLoggerDevice_MaxiGauge) ? 1 : 0];
	[_unitPopup selectItemAtIndex:0];
	[_ofcPopup selectItemAtIndex:0];
	[_csvField setStringValue:_ToNSString( _engine.MakeDefaultCsvPath( _engine.GetLastDeviceType() ) )];
}


- (NSInteger)activeChannelCount
{
	return ([[_devicePopup titleOfSelectedItem] isEqualToString:@"MaxiGauge"]) ? 6 : 2;
}


- (PressureLoggerDeviceType)selectedDeviceType
{
	return ([[_devicePopup titleOfSelectedItem] isEqualToString:@"MaxiGauge"]) ? PressureLoggerDevice_MaxiGauge : PressureLoggerDevice_TPG262;
}


- (void)selectionChanged:(id)sender
{
	(void) sender;
	_engine.SetLastSelection( [self selectedDeviceType], _ToStdString( [_portPopup titleOfSelectedItem] ) );
}


- (void)refreshPorts:(id)sender
{
	(void) sender;

	std::vector<std::string> ports;
	const DWORD error = _engine.CollectSuggestedPorts( &ports );
	if ( error != EC_OK )
	{
		[self showError:error];
		return;
	}

	[_portPopup removeAllItems];
	if ( ports.empty() )
		[_portPopup addItemWithTitle:@""];
	else
		for ( size_t i = 0; i < ports.size(); i++ )
			[_portPopup addItemWithTitle:_ToNSString( ports[i] )];

	const std::string lastPort = _engine.GetLastPort();
	if ( !lastPort.empty() )
		[_portPopup selectItemWithTitle:_ToNSString( lastPort )];
}


- (void)updateDeviceProfile
{
	const BOOL maxi = ([self selectedDeviceType] == PressureLoggerDevice_MaxiGauge);

	[_intervalPopup removeAllItems];
	if ( maxi )
		[_intervalPopup addItemsWithTitles:@[@"0.2 s", @"0.5 s", @"1 s", @"2 s", @"5 s"]];
	else
		[_intervalPopup addItemsWithTitles:@[@"100 ms", @"1 s", @"1 min"]];
	[_intervalPopup selectItemAtIndex:maxi ? 2 : 1];

	[_fsrPopup removeAllItems];
	if ( maxi )
		[_fsrPopup addItemsWithTitles:@[@"1 mbar", @"10 mbar", @"100 mbar", @"1000 mbar", @"2 bar", @"5 bar", @"10 bar", @"50 bar", @"0.1 mbar"]];
	else
		[_fsrPopup addItemsWithTitles:@[@"0.01 mbar", @"0.1 mbar", @"1 mbar", @"10 mbar", @"100 mbar", @"1000 mbar", @"2 bar", @"5 bar", @"10 bar", @"50 bar"]];
	[_fsrPopup selectItemAtIndex:maxi ? 3 : 5];

	[_controlChannelPopup removeAllItems];
	for ( NSInteger i = 1; i <= [self activeChannelCount]; i++ )
		[_controlChannelPopup addItemWithTitle:[NSString stringWithFormat:@"%ld", static_cast<long>( i )]];
	[_controlChannelPopup selectItemAtIndex:0];

	[_digitsPopup setEnabled:maxi];
	[_contrastField setEnabled:maxi];
	[_screensaveField setEnabled:maxi];

	for ( NSInteger i = 0; i < 6; i++ )
	{
		[_channelCards[i] setHidden:(i >= [self activeChannelCount])];
		[_plotChecks[i] setEnabled:(i < [self activeChannelCount])];
		if ( i >= [self activeChannelCount] )
		{
			_plotVisible[i] = NO;
			[_plotChecks[i] setState:NSControlStateValueOff];
		}
	}

	_engine.SetLastSelection( [self selectedDeviceType], _ToStdString( [_portPopup titleOfSelectedItem] ) );
	[_csvField setStringValue:_ToNSString( _engine.MakeDefaultCsvPath( [self selectedDeviceType] ) )];
	[self controlChannelChanged:nil];
}


- (void)deviceChanged:(id)sender
{
	(void) sender;
	[self updateDeviceProfile];
}


- (void)plotVisibilityChanged:(id)sender
{
	for ( int i = 0; i < 6; i++ )
		_plotVisible[i] = ([_plotChecks[i] state] == NSControlStateValueOn);
	[self refreshUi:nil];
}


- (void)controlChannelChanged:(id)sender
{
	(void) sender;
	const NSInteger channel = [_controlChannelPopup indexOfSelectedItem] + 1;
	_displayNameField.stringValue = _ToNSString( _engine.GetDisplayChannelName( [self selectedDeviceType], static_cast<BYTE>( channel ) ) );
}


- (void)toggleControlAction:(id)sender
{
	(void) sender;
	_controlVisible = !_controlVisible;
	[_controlBox setHidden:!_controlVisible];
	[_toggleControlButton setTitle:_controlVisible ? @"Steuerung / Parameter ausblenden" : @"Steuerung / Parameter einblenden"];
}


- (BOOL)readIntField:(NSTextField *)field value:(int *)value label:(NSString *)label
{
	try
	{
		*value = std::stoi( _ToStdString( [field stringValue] ) );
		return YES;
	}
	catch ( ... )
	{
		NSAlert *alert = [[NSAlert alloc] init];
		[alert setAlertStyle:NSAlertStyleCritical];
		[alert setMessageText:@"Input Error"];
		[alert setInformativeText:[NSString stringWithFormat:@"Invalid value for %@.", label]];
		[alert runModal];
		return NO;
	}
}


- (BOOL)readDoubleField:(NSTextField *)field value:(double *)value label:(NSString *)label
{
	try
	{
		std::string text = _ToStdString( [field stringValue] );
		std::replace( text.begin(), text.end(), ',', '.' );
		*value = std::stod( text );
		return YES;
	}
	catch ( ... )
	{
		NSAlert *alert = [[NSAlert alloc] init];
		[alert setAlertStyle:NSAlertStyleCritical];
		[alert setMessageText:@"Input Error"];
		[alert setInformativeText:[NSString stringWithFormat:@"Invalid value for %@.", label]];
		[alert runModal];
		return NO;
	}
}


- (BYTE)selectedChannel
{
	return static_cast<BYTE>( [_controlChannelPopup indexOfSelectedItem] + 1 );
}


- (void)showError:(DWORD)error
{
	NSAlert *alert = [[NSAlert alloc] init];
	[alert setAlertStyle:NSAlertStyleCritical];
	[alert setMessageText:@"Pressure Logger Error"];
	[alert setInformativeText:_ToNSString( _engine.GetLastErrorText( error ) )];
	[alert runModal];
}


- (PressureLoggerConnectionSetup)buildSetup
{
	PressureLoggerConnectionSetup setup;
	setup.DeviceType = [self selectedDeviceType];
	setup.sPort = _ToStdString( [_portPopup titleOfSelectedItem] );
	setup.dwBaudRate = 9600;
	setup.dwTimeoutMs = 200;
	setup.bTPG262LongTermMode = ([_longTermCheck state] == NSControlStateValueOn);

	if ( setup.DeviceType == PressureLoggerDevice_TPG262 )
	{
		setup.dwTPG262ContinuousMode = static_cast<DWORD>( [_intervalPopup indexOfSelectedItem] );
		setup.dPollingSeconds = 60.0;
		if ( setup.bTPG262LongTermMode )
		{
			double value = 60.0;
			[self readDoubleField:_longTermField value:&value label:@"Langzeitmodus"];
			setup.dPollingSeconds = std::max( 1.0, value );
		}
	}
	else
	{
		const double intervals[] = {0.2, 0.5, 1.0, 2.0, 5.0};
		setup.dPollingSeconds = intervals[std::max<NSInteger>( 0, std::min<NSInteger>( [_intervalPopup indexOfSelectedItem], 4 ) )];
		if ( setup.bTPG262LongTermMode )
		{
			double value = 60.0;
			[self readDoubleField:_longTermField value:&value label:@"Langzeitmodus"];
			setup.dPollingSeconds = std::max( 1.0, value );
		}
	}

	return setup;
}


- (void)connectAction:(id)sender
{
	(void) sender;
	const DWORD error = _engine.Connect( [self buildSetup] );
	if ( error != EC_OK )
		[self showError:error];
}


- (void)disconnectAction:(id)sender
{
	(void) sender;
	const DWORD error = _engine.Disconnect();
	if ( error != EC_OK )
		[self showError:error];
}


- (void)chooseCsvPathAction:(id)sender
{
	(void) sender;
	NSSavePanel *panel = [NSSavePanel savePanel];
	[panel setNameFieldStringValue:[_csvField stringValue]];
	if ( [panel runModal] == NSModalResponseOK )
		[_csvField setStringValue:[[panel URL] path]];
}


- (void)startLoggingAction:(id)sender
{
	(void) sender;
	if ( [_liveOnlyCheck state] == NSControlStateValueOn )
	{
		_engine.StopLogging();
		return;
	}

	const DWORD error = _engine.StartLogging( _ToStdString( [_csvField stringValue] ) );
	if ( error != EC_OK )
		[self showError:error];
}


- (void)startNewMeasurementAction:(id)sender
{
	(void) sender;
	[_csvField setStringValue:_ToNSString( _engine.MakeDefaultCsvPath( [self selectedDeviceType] ) )];
	_engine.ResetMeasurementTimeline();
	if ( [_liveOnlyCheck state] != NSControlStateValueOn )
	{
		const DWORD error = _engine.StartLogging( _ToStdString( [_csvField stringValue] ) );
		if ( error != EC_OK )
			[self showError:error];
	}
}


- (void)stopLoggingAction:(id)sender
{
	(void) sender;
	const DWORD error = _engine.StopLogging();
	if ( error != EC_OK )
		[self showError:error];
}


- (void)readNowAction:(id)sender
{
	(void) sender;
	const DWORD error = _engine.ReadSingleChannelNow( [self selectedChannel] );
	if ( error != EC_OK )
		[self showError:error];
}


- (void)diagnoseAction:(id)sender
{
	(void) sender;
	const DWORD error = _engine.ReadDeviceInfo();
	if ( error != EC_OK )
		[self showError:error];
}


- (void)activateVerifyAction:(id)sender
{
	(void) sender;
	const DWORD error = _engine.ActivateAndVerify( [self selectedChannel] );
	if ( error != EC_OK )
		[self showError:error];
}


- (void)factoryResetAction:(id)sender
{
	(void) sender;
	NSAlert *alert = [[NSAlert alloc] init];
	[alert setAlertStyle:NSAlertStyleWarning];
	[alert setMessageText:@"Werkreset"];
	[alert setInformativeText:@"Werkseinstellungen fuer das angeschlossene Geraet laden?"];
	[alert addButtonWithTitle:@"Reset"];
	[alert addButtonWithTitle:@"Abbrechen"];
	if ( [alert runModal] != NSAlertFirstButtonReturn )
		return;

	const DWORD error = _engine.FactoryResetDevice();
	if ( error != EC_OK )
		[self showError:error];
}


- (void)setUnitAction:(id)sender
{
	(void) sender;
	const DWORD error = _engine.SetUnit( static_cast<int>( [_unitPopup indexOfSelectedItem] ) );
	if ( error != EC_OK )
		[self showError:error];
}


- (void)gaugeOnAction:(id)sender
{
	(void) sender;
	const DWORD error = _engine.SetSensorState( [self selectedChannel], true );
	if ( error != EC_OK )
		[self showError:error];
}


- (void)gaugeOffAction:(id)sender
{
	(void) sender;
	const DWORD error = _engine.SetSensorState( [self selectedChannel], false );
	if ( error != EC_OK )
		[self showError:error];
}


- (void)degasOnAction:(id)sender
{
	(void) sender;
	const DWORD error = _engine.SetDegas( [self selectedChannel], true );
	if ( error != EC_OK )
		[self showError:error];
}


- (void)degasOffAction:(id)sender
{
	(void) sender;
	const DWORD error = _engine.SetDegas( [self selectedChannel], false );
	if ( error != EC_OK )
		[self showError:error];
}


- (void)setFilterAction:(id)sender
{
	(void) sender;
	int filterValue = 0;
	if ( [_filterPopup indexOfSelectedItem] == 1 ) filterValue = 1;
	else if ( [_filterPopup indexOfSelectedItem] == 2 ) filterValue = 2;

	const DWORD error = _engine.SetFilter( [self selectedChannel], filterValue );
	if ( error != EC_OK )
		[self showError:error];
}


- (void)setCalibrationAction:(id)sender
{
	(void) sender;
	double value = 1.0;
	if ( ![self readDoubleField:_calibrationField value:&value label:@"CAL"] )
		return;

	const DWORD error = _engine.SetCalibration( [self selectedChannel], value );
	if ( error != EC_OK )
		[self showError:error];
}


- (void)setFsrAction:(id)sender
{
	(void) sender;
	const DWORD error = _engine.SetFsr( [self selectedChannel], static_cast<int>( [_fsrPopup indexOfSelectedItem] ) );
	if ( error != EC_OK )
		[self showError:error];
}


- (void)setOfcAction:(id)sender
{
	(void) sender;
	const DWORD error = _engine.SetOfc( [self selectedChannel], static_cast<int>( [_ofcPopup indexOfSelectedItem] ) );
	if ( error != EC_OK )
		[self showError:error];
}


- (void)setDisplayNameAction:(id)sender
{
	(void) sender;
	const DWORD error = _engine.SetChannelName( [self selectedChannel], _ToStdString( [_displayNameField stringValue] ) );
	if ( error != EC_OK )
		[self showError:error];
}


- (void)setDigitsAction:(id)sender
{
	(void) sender;
	const DWORD error = _engine.SetDigits( ([_digitsPopup indexOfSelectedItem] == 0) ? 2 : 3 );
	if ( error != EC_OK )
		[self showError:error];
}


- (void)setContrastAction:(id)sender
{
	(void) sender;
	int value = 10;
	if ( ![self readIntField:_contrastField value:&value label:@"Contrast"] )
		return;

	const DWORD error = _engine.SetContrast( value );
	if ( error != EC_OK )
		[self showError:error];
}


- (void)setScreensaveAction:(id)sender
{
	(void) sender;
	int value = 0;
	if ( ![self readIntField:_screensaveField value:&value label:@"Screensave"] )
		return;

	const DWORD error = _engine.SetScreensave( value );
	if ( error != EC_OK )
		[self showError:error];
}


- (void)sendRawAction:(id)sender
{
	(void) sender;
	const DWORD error = _engine.ExecuteRawCommand( _ToStdString( [_rawField stringValue] ) );
	if ( error != EC_OK )
		[self showError:error];
}


- (void)showHelpWindowWithTitle:(NSString *)title key:(const std::string &)key
{
	NSWindow *helpWindow = [[NSWindow alloc] initWithContentRect:NSMakeRect( 0, 0, 780, 620 )
													 styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable)
													   backing:NSBackingStoreBuffered
														 defer:NO];
	[helpWindow setTitle:title];
	[helpWindow center];

	NSTextView *helpText = [self createTextView:NSMakeRect( 10, 10, 760, 600 ) inView:[helpWindow contentView]];
	[[helpText textStorage] setAttributedString:[[NSAttributedString alloc] initWithString:_ToNSString( _engine.GetHelpText( key ) ) attributes:@{NSFontAttributeName:_MonoFont()}]];
	[helpWindow makeKeyAndOrderFront:nil];
}


- (void)showRawHelpAction:(id)sender
{
	(void) sender;
	[self showHelpWindowWithTitle:@"Hilfe: Rohkommandos" key:"raw"];
}


- (void)openExternalPlotAction:(id)sender
{
	(void) sender;
	if ( _externalPlotWindow == nil )
	{
		_externalPlotWindow = [[NSWindow alloc] initWithContentRect:NSMakeRect( 0, 0, 1100, 760 )
														 styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable)
														   backing:NSBackingStoreBuffered
															 defer:NO];
		[_externalPlotWindow setTitle:@"Externer Plot"];
		_externalPlotView = [[PressurePlotView alloc] initWithFrame:[[_externalPlotWindow contentView] bounds]];
		[_externalPlotView setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
		[[_externalPlotWindow contentView] addSubview:_externalPlotView];
	}

	[_externalPlotWindow makeKeyAndOrderFront:nil];
}


- (BOOL)loadCsvSnapshotFromPath:(NSString *)path
{
	std::ifstream input( _ToStdString( path ).c_str() );
	if ( !input.is_open() )
		return NO;

	std::string header_line;
	if ( !std::getline( input, header_line ) )
		return NO;

	std::vector<std::string> headers;
	std::stringstream header_stream( header_line );
	std::string header_token;
	while ( std::getline( header_stream, header_token, ',' ) )
		headers.push_back( header_token );

	_csvSnapshot = PressureLoggerStateSnapshot();
	_csvSnapshot.Setup.DeviceType = ((headers.size() - 1) / 2 > 2) ? PressureLoggerDevice_MaxiGauge : PressureLoggerDevice_TPG262;
	_csvSnapshot.DisplayChannelNames = _engine.GetDisplayChannelNames( _csvSnapshot.Setup.DeviceType );
	for ( size_t i = 0; i < _csvSnapshot.DisplayChannelNames.size(); i++ )
		_csvSnapshot.CombinedChannelLabels.push_back( _engine.FormatCombinedChannelLabel( _csvSnapshot.Setup.DeviceType, static_cast<BYTE>( i + 1 ) ) );

	std::string line;
	while ( std::getline( input, line ) )
	{
		std::stringstream line_stream( line );
		std::vector<std::string> values;
		std::string token;
		while ( std::getline( line_stream, token, ',' ) )
			values.push_back( token );

		if ( values.size() != headers.size() )
			continue;

		PressureSample sample;
		sample.dSecondsSinceStart = std::stod( values[0] );
		for ( size_t channel = 1; channel < headers.size() / 2 + 1; channel++ )
		{
			PressureChannelReading reading;
			reading.byChannel = static_cast<BYTE>( channel );
			reading.nStatusCode = std::stoi( values[1 + (channel - 1) * 2] );
			reading.dPressure = std::stod( values[2 + (channel - 1) * 2] );
			reading.sStatusText = CPfeifferGaugeDriver::StatusText( reading.nStatusCode );
			sample.ChannelValues.push_back( reading );
		}

		_csvSnapshot.History.push_back( sample );
	}

	return YES;
}


- (void)plotCsvAction:(id)sender
{
	(void) sender;
	NSOpenPanel *panel = [NSOpenPanel openPanel];
	[panel setAllowedFileTypes:@[@"csv"]];
	if ( [panel runModal] != NSModalResponseOK )
		return;

	if ( ![self loadCsvSnapshotFromPath:[[panel URL] path]] )
	{
		NSAlert *alert = [[NSAlert alloc] init];
		[alert setAlertStyle:NSAlertStyleCritical];
		[alert setMessageText:@"CSV-Plot fehlgeschlagen"];
		[alert setInformativeText:@"Die CSV-Datei konnte nicht gelesen oder geparst werden."];
		[alert runModal];
		return;
	}

	if ( _csvPlotWindow == nil )
	{
		_csvPlotWindow = [[NSWindow alloc] initWithContentRect:NSMakeRect( 0, 0, 1100, 760 )
													  styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable)
														backing:NSBackingStoreBuffered
														  defer:NO];
		_csvPlotView = [[PressurePlotView alloc] initWithFrame:[[_csvPlotWindow contentView] bounds]];
		[_csvPlotView setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
		[[_csvPlotWindow contentView] addSubview:_csvPlotView];
	}

	[_csvPlotWindow setTitle:[NSString stringWithFormat:@"CSV-Plot: %@", [[panel URL] lastPathComponent]]];
	[_csvPlotView updateWithSnapshot:_csvSnapshot engine:&_engine visibleChannels:_plotVisible channelCount:_csvSnapshot.CombinedChannelLabels.size()];
	[_csvPlotWindow makeKeyAndOrderFront:nil];
}


- (void)refreshUi:(id)sender
{
	(void) sender;

	PressureLoggerStateSnapshot snapshot;
	_engine.GetStateSnapshot( &snapshot );

	[_connectionIndicator setIndicatorColor:snapshot.bConnected ? [NSColor colorWithCalibratedRed:0.18 green:0.49 blue:0.20 alpha:1.0] : [NSColor colorWithCalibratedWhite:0.70 alpha:1.0]];
	[_measurementIndicator setIndicatorColor:snapshot.bMonitoring ? [NSColor colorWithCalibratedRed:0.18 green:0.49 blue:0.20 alpha:1.0] : [NSColor colorWithCalibratedWhite:0.70 alpha:1.0]];
	[_fileIndicator setIndicatorColor:snapshot.bLogging ? [NSColor colorWithCalibratedRed:0.18 green:0.49 blue:0.20 alpha:1.0] : [NSColor colorWithCalibratedWhite:0.70 alpha:1.0]];

	[_measurementStatusLabel setStringValue:snapshot.bConnected ? (snapshot.bLogging ? @"Logging läuft" : @"Monitoring läuft") : @"Nicht verbunden"];
	[_samplesStatusLabel setStringValue:[NSString stringWithFormat:@"Sam %u", static_cast<unsigned>( snapshot.dwSampleCount )]];
	[_fileStatusLabel setStringValue:snapshot.bLogging ? [NSString stringWithFormat:@"Datei: %@", _ToNSString( snapshot.sCsvPath )] : @"Datei: Keine Datei offen"];

	for ( NSInteger i = 0; i < [self activeChannelCount]; i++ )
	{
		NSString *label = _ToNSString( (i < snapshot.CombinedChannelLabels.size())
									   ? snapshot.CombinedChannelLabels[i]
									   : _engine.FormatCombinedChannelLabel( snapshot.Setup.DeviceType, static_cast<BYTE>( i + 1 ) ) );
		[_channelCards[i] setCombinedLabel:label];

		bool found = false;
		for ( size_t j = 0; j < snapshot.LastChannels.size(); j++ )
		{
			if ( snapshot.LastChannels[j].byChannel == static_cast<BYTE>( i + 1 ) )
			{
				[_channelCards[i] updatePressure:snapshot.LastChannels[j].dPressure
									   statusCode:snapshot.LastChannels[j].nStatusCode
									   statusText:_ToNSString( snapshot.LastChannels[j].sStatusText )];
				found = true;
				break;
			}
		}
		if ( !found )
			[_channelCards[i] updatePressure:nan("") statusCode:6 statusText:@"—"];
	}

	std::stringstream log_stream;
	for ( size_t i = 0; i < snapshot.LogLines.size(); i++ )
		log_stream << snapshot.LogLines[i] << "\n";
	[[_messagesView textStorage] setAttributedString:[[NSAttributedString alloc] initWithString:_ToNSString( log_stream.str() ) attributes:@{NSFontAttributeName:_MonoFont()}]];

	[_plotView updateWithSnapshot:snapshot engine:&_engine visibleChannels:_plotVisible channelCount:[self activeChannelCount]];
	if ( _externalPlotView != nil )
		[_externalPlotView updateWithSnapshot:snapshot engine:&_engine visibleChannels:_plotVisible channelCount:[self activeChannelCount]];
}

@end


int main( int argc, const char *argv[] )
{
	(void) argc;
	(void) argv;

	@autoreleasepool
	{
		NSApplication *app = [NSApplication sharedApplication];
		CDTPressureLoggerDelegate *delegate = [[CDTPressureLoggerDelegate alloc] init];
		[app setDelegate:delegate];
		[app run];
	}

	return 0;
}
