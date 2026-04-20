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

#include <algorithm>
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
	NSTextField *_okLabel;
	NSTextField *_offLabel;
	NSTextField *_orLabel;
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
		[_titleLabel setLineBreakMode:NSLineBreakByTruncatingTail];
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
		[_statusLabel setLineBreakMode:NSLineBreakByTruncatingTail];
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
			if ( i == 0 ) _okLabel = label;
			else if ( i == 1 ) _offLabel = label;
			else _orLabel = label;
		}
	}
	return self;
}


- (void)layout
{
	[super layout];

	NSRect bounds = [self bounds];
	const CGFloat width = bounds.size.width;
	const CGFloat height = bounds.size.height;
	const CGFloat leftPadding = 16.0;
	const CGFloat rightPadding = 16.0;
	const CGFloat titleHeight = 20.0;
	const CGFloat valueHeight = 34.0;
	const CGFloat statusHeight = 22.0;
	const CGFloat bottomPadding = 14.0;
	const CGFloat indicatorSize = 18.0;
	const CGFloat labelGap = 6.0;
	const CGFloat pairGap = 18.0;

	[_titleLabel setFrame:NSMakeRect( leftPadding, height - 32.0, width - leftPadding - rightPadding, titleHeight )];
	[_valueLabel setFrame:NSMakeRect( leftPadding, height - 76.0, width - leftPadding - rightPadding, valueHeight )];
	[_statusLabel setFrame:NSMakeRect( leftPadding, height - 108.0, width - leftPadding - rightPadding, statusHeight )];

	CGFloat cursor = width - rightPadding;
	const CGFloat orLabelWidth = 24.0;
	const CGFloat offLabelWidth = 28.0;
	const CGFloat okLabelWidth = 24.0;

	const CGFloat orIndicatorX = cursor - indicatorSize;
	const CGFloat orLabelX = orIndicatorX - labelGap - orLabelWidth;
	cursor = orLabelX - pairGap;
	const CGFloat offIndicatorX = cursor - indicatorSize;
	const CGFloat offLabelX = offIndicatorX - labelGap - offLabelWidth;
	cursor = offLabelX - pairGap;
	const CGFloat okIndicatorX = cursor - indicatorSize;
	const CGFloat okLabelX = okIndicatorX - labelGap - okLabelWidth;

	[_okIndicator setFrame:NSMakeRect( okIndicatorX, bottomPadding, indicatorSize, indicatorSize )];
	[_offIndicator setFrame:NSMakeRect( offIndicatorX, bottomPadding, indicatorSize, indicatorSize )];
	[_orIndicator setFrame:NSMakeRect( orIndicatorX, bottomPadding, indicatorSize, indicatorSize )];

	[_okLabel setFrame:NSMakeRect( okLabelX, bottomPadding - 4.0, okLabelWidth, 22.0 )];
	[_offLabel setFrame:NSMakeRect( offLabelX, bottomPadding - 4.0, offLabelWidth, 22.0 )];
	[_orLabel setFrame:NSMakeRect( orLabelX, bottomPadding - 4.0, orLabelWidth, 22.0 )];
}


- (void)setFrameSize:(NSSize)newSize
{
	[super setFrameSize:newSize];
	[self layout];
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
- (void)resetZoom;
- (void)zoomIn;
- (void)zoomOut;
@end


@implementation PressurePlotView
{
	PressureLoggerStateSnapshot _snapshot;
	CPressureLoggerAppEngine *_engine;
	bool _visibleChannels[6];
	NSInteger _channelCount;
	BOOL _hasManualViewport;
	double _manualXMin;
	double _manualXMax;
	double _manualYMin;
	double _manualYMax;
	double _autoXMin;
	double _autoXMax;
	double _autoYMin;
	double _autoYMax;
	NSRect _lastPlotRect;
}


- (instancetype)initWithFrame:(NSRect)frame
{
	self = [super initWithFrame:frame];
	if ( self )
	{
		for ( int i = 0; i < 6; i++ )
			_visibleChannels[i] = (i < 2);
		_channelCount = 2;
		_hasManualViewport = NO;
		_autoXMin = -0.05;
		_autoXMax = 0.05;
		_autoYMin = 1.0;
		_autoYMax = 10.0;
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


- (BOOL)isFlipped
{
	return NO;
}


- (BOOL)acceptsFirstResponder
{
	return YES;
}


- (void)resetZoom
{
	_hasManualViewport = NO;
	[self setNeedsDisplay:YES];
}


- (void)zoomIn
{
	[self zoomAroundNormalizedX:0.5 normalizedY:0.5 factor:0.78];
}


- (void)zoomOut
{
	[self zoomAroundNormalizedX:0.5 normalizedY:0.5 factor:1.28];
}


- (void)zoomAroundNormalizedX:(double)normalizedX normalizedY:(double)normalizedY factor:(double)factor
{
	const double xMin = _hasManualViewport ? _manualXMin : _autoXMin;
	const double xMax = _hasManualViewport ? _manualXMax : _autoXMax;
	const double yMin = _hasManualViewport ? _manualYMin : _autoYMin;
	const double yMax = _hasManualViewport ? _manualYMax : _autoYMax;
	if ( !(xMax > xMin) || !(yMax > yMin) )
		return;

	const double clampedX = std::max( 0.0, std::min( 1.0, normalizedX ) );
	const double clampedY = std::max( 0.0, std::min( 1.0, normalizedY ) );
	const double anchorX = xMin + (xMax - xMin) * clampedX;
	const double newXMin = anchorX - (anchorX - xMin) * factor;
	const double newXMax = anchorX + (xMax - anchorX) * factor;
	if ( (newXMax - newXMin) < 1e-6 )
		return;

	const double logYMin = log10( std::max( yMin, 1e-12 ) );
	const double logYMax = log10( std::max( yMax, yMin * 1.01 ) );
	const double anchorLogY = logYMin + (logYMax - logYMin) * clampedY;
	const double newLogYMin = anchorLogY - (anchorLogY - logYMin) * factor;
	const double newLogYMax = anchorLogY + (logYMax - anchorLogY) * factor;
	if ( (newLogYMax - newLogYMin) < 0.05 )
		return;

	_manualXMin = newXMin;
	_manualXMax = newXMax;
	_manualYMin = pow( 10.0, newLogYMin );
	_manualYMax = pow( 10.0, newLogYMax );
	_hasManualViewport = YES;
	[self setNeedsDisplay:YES];
}


- (void)scrollWheel:(NSEvent *)event
{
	const NSPoint localPoint = [self convertPoint:[event locationInWindow] fromView:nil];
	if ( !NSPointInRect( localPoint, _lastPlotRect ) )
	{
		[super scrollWheel:event];
		return;
	}

	const double delta = [event hasPreciseScrollingDeltas] ? [event scrollingDeltaY] : [event deltaY];
	if ( fabs( delta ) < 0.01 )
		return;

	const double normalizedX = (localPoint.x - _lastPlotRect.origin.x) / _lastPlotRect.size.width;
	const double normalizedY = (localPoint.y - _lastPlotRect.origin.y) / _lastPlotRect.size.height;
	const double factor = (delta > 0.0) ? 0.88 : 1.14;
	[self zoomAroundNormalizedX:normalizedX normalizedY:normalizedY factor:factor];
}


- (void)magnifyWithEvent:(NSEvent *)event
{
	double factor = 1.0 - [event magnification];
	factor = std::max( 0.65, std::min( 1.35, factor ) );
	[self zoomAroundNormalizedX:0.5 normalizedY:0.5 factor:factor];
}


- (void)mouseDown:(NSEvent *)event
{
	if ( [event clickCount] >= 2 )
	{
		[self resetZoom];
		return;
	}
	[super mouseDown:event];
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
	_lastPlotRect = plotRect;

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

	_autoXMin = xMin;
	_autoXMax = xMax;
	_autoYMin = yMin;
	_autoYMax = yMax;
	if ( !_hasManualViewport )
	{
		_manualXMin = xMin;
		_manualXMax = xMax;
		_manualYMin = yMin;
		_manualYMax = yMax;
	}

	const double viewXMin = _hasManualViewport ? _manualXMin : _autoXMin;
	const double viewXMax = _hasManualViewport ? _manualXMax : _autoXMax;
	const double viewYMin = _hasManualViewport ? _manualYMin : _autoYMin;
	const double viewYMax = _hasManualViewport ? _manualYMax : _autoYMax;

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
		const double logValue = log10( viewYMin ) + (log10( viewYMax ) - log10( viewYMin )) * static_cast<double>( i ) / 4.0;
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

			if ( (times[i] < viewXMin) || (times[i] > viewXMax) || (value < viewYMin) || (value > viewYMax) )
			{
				pathStarted = false;
				continue;
			}

			const CGFloat x = plotRect.origin.x + static_cast<CGFloat>( (times[i] - viewXMin) / (viewXMax - viewXMin) ) * plotRect.size.width;
			const double normalizedY = (log10( value ) - log10( viewYMin )) / (log10( viewYMax ) - log10( viewYMin ));
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

	NSInteger visibleCount = 0;
	for ( NSInteger channel = 1; channel <= _channelCount; channel++ )
		if ( _visibleChannels[channel - 1] )
			visibleCount++;

	NSRect legendRect = NSMakeRect( plotRect.origin.x + plotRect.size.width - 250, plotRect.origin.y + plotRect.size.height - 22.0 - 24.0 * std::max<NSInteger>( visibleCount, 1 ), 230, 18.0 + 24.0 * std::max<NSInteger>( visibleCount, 1 ) );
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

	if ( _hasManualViewport )
	{
		NSString *zoomBadge = @"Zoom aktiv";
		NSDictionary *attributes = @{ NSFontAttributeName : _SmallBoldFont(),
									  NSForegroundColorAttributeName : [NSColor colorWithCalibratedWhite:0.18 alpha:1.0] };
		[zoomBadge drawAtPoint:NSMakePoint( plotRect.origin.x + 10, plotRect.origin.y + plotRect.size.height - 22 ) withAttributes:attributes];
	}
}

@end


@interface PressureFlippedView : NSView
@end


@implementation PressureFlippedView
- (BOOL)isFlipped
{
	return YES;
}
@end


@interface CDTPressureLoggerDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate>
@end


@implementation CDTPressureLoggerDelegate
{
	NSWindow *_window;
	NSScrollView *_leftScrollView;
	NSView *_leftContentView;
	NSView *_rightView;
	NSBox *_connectionBox;
	NSBox *_rawBox;
	NSView *_plotFooterView;
	NSTextField *_plotSelectionLabel;
	NSTextField *_messagesLabel;
	NSButton *_renameChannelsButton;
	NSButton *_debugInfoButton;
	NSTextField *_deviceLabel;
	NSTextField *_portLabel;
	NSTextField *_measurementLabel;
	NSTextField *_csvLabel;
	NSTextField *_longTermSuffixLabel;
	NSButton *_connectButton;
	NSButton *_disconnectButton;
	NSButton *_refreshPortsButton;
	NSButton *_diagnoseButton;
	NSButton *_factoryResetButton;
	NSButton *_startLoggingButton;
	NSButton *_newMeasurementButton;
	NSButton *_stopLoggingButton;
	NSButton *_csvBrowseButton;
	NSButton *_browseCsvButton;
	NSButton *_sendRawButton;
	NSButton *_rawHelpButton;

	NSPopUpButton *_devicePopup;
	NSPopUpButton *_portPopup;
	PressureIndicatorView *_connectionIndicator;
	PressureIndicatorView *_measurementIndicator;
	PressureIndicatorView *_fileIndicator;
	NSTextField *_measurementStatusLabel;
	NSTextField *_samplesStatusLabel;
	NSTextField *_fileStatusLabel;
	NSTextField *_intervalTitleLabel;
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
	NSTextField *_channelNameField;
	NSTextField *_displayNameLabel;
	NSTextField *_displayNameField;
	NSButton *_displayNameButton;
	NSPopUpButton *_digitsPopup;
	NSTextField *_contrastField;
	NSTextField *_screensaveField;
	NSArray<NSView *> *_maxiOnlyViews;

	PressurePlotView *_plotView;
	NSButton *_plotHomeButton;
	NSButton *_plotZoomOutButton;
	NSButton *_plotZoomInButton;
	NSButton *_clearPlotButton;
	NSButton *_externalPlotButton;
	NSButton *_plotCsvButton;
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
	PressureFlippedView *contentView = [[PressureFlippedView alloc] initWithFrame:[[box contentView] frame]];
	[contentView setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
	[box setContentView:contentView];
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


- (NSButton *)createInfoButton:(NSRect)frame key:(NSString *)key title:(NSString *)title inView:(NSView *)view
{
	NSButton *button = [self createButton:frame title:@"i" target:self action:@selector(showHelpAction:) inView:view];
	[button setFont:_MainFont()];
	[button setIdentifier:key];
	[button setToolTip:title];
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
	[textView setHorizontallyResizable:NO];
	[textView setVerticallyResizable:YES];
	[textView setMinSize:NSMakeSize( 0, 0 )];
	[textView setMaxSize:NSMakeSize( CGFLOAT_MAX, CGFLOAT_MAX )];
	[textView setAutoresizingMask:NSViewWidthSizable];
	[[textView textContainer] setWidthTracksTextView:YES];
	[scrollView setDocumentView:textView];
	[view addSubview:scrollView];
	return textView;
}


- (void)buildWindow
{
	NSRect frame = NSMakeRect( 0, 0, 1700, 980 );
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
	[_window setDelegate:self];
	[_window setMinSize:NSMakeSize( 1380.0, 860.0 )];

	NSView *contentView = [_window contentView];

	_leftScrollView = [[NSScrollView alloc] initWithFrame:NSMakeRect( 0, 0, 770, frame.size.height )];
	[_leftScrollView setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
	[_leftScrollView setHasVerticalScroller:YES];
	[_leftScrollView setBorderType:NSNoBorder];
	_leftContentView = [[PressureFlippedView alloc] initWithFrame:NSMakeRect( 0, 0, 760, 1120 )];
	[_leftScrollView setDocumentView:_leftContentView];
	[contentView addSubview:_leftScrollView];

	_rightView = [[PressureFlippedView alloc] initWithFrame:NSMakeRect( 780, 0, frame.size.width - 780, frame.size.height )];
	[_rightView setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
	[contentView addSubview:_rightView];

	[self createLabel:NSMakeRect( 16, 12, 340, 34 ) text:@"CDT pressure logger" inView:_leftContentView bold:YES];

	_connectionBox = [self createSectionBox:NSMakeRect( 10, 56, 740, 304 ) title:@"Verbindung / Messung / Status" inView:_leftContentView];
	NSView *connectionView = [_connectionBox contentView];

	_deviceLabel = [self createLabel:NSMakeRect( 16, 12, 48, 24 ) text:@"Gerät:" inView:connectionView bold:NO];
	_connectionIndicator = [[PressureIndicatorView alloc] initWithFrame:NSMakeRect( 210, 12, 18, 18 )];
	[connectionView addSubview:_connectionIndicator];
	_devicePopup = [self createPopup:NSMakeRect( 260, 8, 210, 28 ) titles:@[@"TPG 262", @"MaxiGauge"] inView:connectionView];
	[_devicePopup setTarget:self];
	[_devicePopup setAction:@selector(deviceChanged:)];
	_portLabel = [self createLabel:NSMakeRect( 500, 12, 35, 24 ) text:@"Port" inView:connectionView bold:NO];
	_portPopup = [self createPopup:NSMakeRect( 546, 8, 172, 28 ) titles:@[@""] inView:connectionView];
	[_portPopup setTarget:self];
	[_portPopup setAction:@selector(selectionChanged:)];

	_connectButton = [self createButton:NSMakeRect( 16, 46, 118, 30 ) title:@"Verbinden" target:self action:@selector(connectAction:) inView:connectionView];
	_disconnectButton = [self createButton:NSMakeRect( 142, 46, 118, 30 ) title:@"Trennen" target:self action:@selector(disconnectAction:) inView:connectionView];
	_refreshPortsButton = [self createButton:NSMakeRect( 268, 46, 126, 30 ) title:@"Aktualisieren" target:self action:@selector(refreshPorts:) inView:connectionView];
	_diagnoseButton = [self createButton:NSMakeRect( 402, 46, 118, 30 ) title:@"Diagnose" target:self action:@selector(diagnoseAction:) inView:connectionView];
	_factoryResetButton = [self createButton:NSMakeRect( 528, 46, 118, 30 ) title:@"Werkreset" target:self action:@selector(factoryResetAction:) inView:connectionView];

	_measurementLabel = [self createLabel:NSMakeRect( 16, 82, 68, 24 ) text:@"Messung:" inView:connectionView bold:NO];
	_measurementIndicator = [[PressureIndicatorView alloc] initWithFrame:NSMakeRect( 210, 82, 18, 18 )];
	[connectionView addSubview:_measurementIndicator];
	_measurementStatusLabel = [self createLabel:NSMakeRect( 260, 82, 220, 24 ) text:@"Nicht verbunden" inView:connectionView bold:NO];
	_samplesStatusLabel = [self createLabel:NSMakeRect( 500, 82, 140, 24 ) text:@"Sam 0" inView:connectionView bold:NO];

	_startLoggingButton = [self createButton:NSMakeRect( 16, 118, 140, 30 ) title:@"Logging starten" target:self action:@selector(startLoggingAction:) inView:connectionView];
	_newMeasurementButton = [self createButton:NSMakeRect( 164, 118, 162, 30 ) title:@"Neue Datei + Start" target:self action:@selector(startNewMeasurementAction:) inView:connectionView];
	_stopLoggingButton = [self createButton:NSMakeRect( 334, 118, 146, 30 ) title:@"Logging stoppen" target:self action:@selector(stopLoggingAction:) inView:connectionView];
	_liveOnlyCheck = [self createCheckbox:NSMakeRect( 490, 120, 236, 26 ) title:@"nur live anzeigen, nicht speichern" target:self action:@selector(refreshUi:) inView:connectionView];

	_intervalTitleLabel = [self createLabel:NSMakeRect( 16, 154, 136, 24 ) text:@"Continuous Mode" inView:connectionView bold:NO];
	_intervalPopup = [self createPopup:NSMakeRect( 164, 150, 100, 28 ) titles:@[@"1 s"] inView:connectionView];
	_longTermCheck = [self createCheckbox:NSMakeRect( 274, 152, 130, 26 ) title:@"Langzeitmodus" target:self action:@selector(deviceChanged:) inView:connectionView];
	_longTermField = [self createField:NSMakeRect( 462, 150, 56, 28 ) text:@"60" inView:connectionView];
	_longTermSuffixLabel = [self createLabel:NSMakeRect( 526, 154, 118, 24 ) text:@"s (Standard 60)" inView:connectionView bold:NO];

	_csvLabel = [self createLabel:NSMakeRect( 16, 190, 36, 24 ) text:@"CSV" inView:connectionView bold:NO];
	_csvField = [self createField:NSMakeRect( 56, 186, 626, 28 ) text:@"" inView:connectionView];
	_csvBrowseButton = [self createButton:NSMakeRect( 690, 186, 30, 28 ) title:@"…" target:self action:@selector(chooseCsvPathAction:) inView:connectionView];

	_browseCsvButton = [self createButton:NSMakeRect( 16, 226, 118, 30 ) title:@"Durchsuchen" target:self action:@selector(chooseCsvPathAction:) inView:connectionView];
	_fileIndicator = [[PressureIndicatorView alloc] initWithFrame:NSMakeRect( 142, 232, 18, 18 )];
	[connectionView addSubview:_fileIndicator];
	_fileStatusLabel = [self createLabel:NSMakeRect( 174, 228, 540, 24 ) text:@"Datei: Keine Datei offen" inView:connectionView bold:NO];
	[_fileStatusLabel setLineBreakMode:NSLineBreakByTruncatingMiddle];

	for ( int i = 0; i < 6; i++ )
	{
		_channelCards[i] = [[PressureChannelCardView alloc] initWithFrame:NSMakeRect( 10, 400, 360, 128 )];
		[_leftContentView addSubview:_channelCards[i]];
	}

	_plotSelectionLabel = [self createLabel:NSMakeRect( 16, 0, 120, 24 ) text:@"Im Plot anzeigen:" inView:_leftContentView bold:NO];
	for ( int i = 0; i < 6; i++ )
	{
		_plotChecks[i] = [self createCheckbox:NSMakeRect( 145 + i * 44, 0, 42, 24 ) title:[NSString stringWithFormat:@"%d", i + 1] target:self action:@selector(plotVisibilityChanged:) inView:_leftContentView];
		[_plotChecks[i] setState:(i < 2) ? NSControlStateValueOn : NSControlStateValueOff];
	}
	_renameChannelsButton = [self createButton:NSMakeRect( 430, 0, 132, 28 ) title:@"Kanalnamen..." target:self action:@selector(editChannelNamesAction:) inView:_leftContentView];
	_debugInfoButton = [self createButton:NSMakeRect( 572, 0, 148, 28 ) title:@"Debug-Info" target:self action:@selector(showDebugInfoAction:) inView:_leftContentView];

	_messagesLabel = [self createLabel:NSMakeRect( 16, 0, 90, 24 ) text:@"Meldungen" inView:_leftContentView bold:NO];
	_messagesView = [self createTextView:NSMakeRect( 10, 0, 740, 180 ) inView:_leftContentView];

	_rawBox = [self createSectionBox:NSMakeRect( 10, 0, 740, 64 ) title:@"Rohkommando" inView:_leftContentView];
	NSView *rawView = [_rawBox contentView];
	_rawField = [self createField:NSMakeRect( 14, 10, 500, 28 ) text:@"" inView:rawView];
	_sendRawButton = [self createButton:NSMakeRect( 528, 8, 150, 30 ) title:@"Senden" target:self action:@selector(sendRawAction:) inView:rawView];
	_rawHelpButton = [self createInfoButton:NSMakeRect( 686, 8, 34, 30 ) key:@"raw" title:@"Hilfe: Rohkommandos" inView:rawView];

	_toggleControlButton = [self createButton:NSMakeRect( 10, 0, 740, 30 ) title:@"Steuerung / Parameter einblenden" target:self action:@selector(toggleControlAction:) inView:_leftContentView];
	_controlBox = [self createSectionBox:NSMakeRect( 10, 0, 740, 516 ) title:@"Steuerung / Parameter" inView:_leftContentView];
	NSView *controlView = [_controlBox contentView];

	[self createLabel:NSMakeRect( 16, 10, 40, 24 ) text:@"Kanal" inView:controlView bold:NO];
	_controlChannelPopup = [self createPopup:NSMakeRect( 70, 8, 90, 28 ) titles:@[@"1", @"2"] inView:controlView];
	[_controlChannelPopup setTarget:self];
	[_controlChannelPopup setAction:@selector(controlChannelChanged:)];
	[self createButton:NSMakeRect( 264, 8, 126, 30 ) title:@"Gauge EIN" target:self action:@selector(gaugeOnAction:) inView:controlView];
	[self createButton:NSMakeRect( 398, 8, 126, 30 ) title:@"Gauge AUS" target:self action:@selector(gaugeOffAction:) inView:controlView];
	[self createInfoButton:NSMakeRect( 706, 8, 24, 30 ) key:@"sensor" title:@"Hilfe: Gauge ein/aus" inView:controlView];

	[self createLabel:NSMakeRect( 16, 46, 50, 24 ) text:@"Einheit" inView:controlView bold:NO];
	_unitPopup = [self createPopup:NSMakeRect( 70, 44, 120, 28 ) titles:@[@"mbar", @"Torr", @"Pa"] inView:controlView];
	[self createButton:NSMakeRect( 264, 44, 138, 30 ) title:@"Einheit setzen" target:self action:@selector(setUnitAction:) inView:controlView];
	[self createInfoButton:NSMakeRect( 706, 44, 24, 30 ) key:@"unit" title:@"Hilfe: Einheit" inView:controlView];

	[self createButton:NSMakeRect( 264, 80, 178, 30 ) title:@"Messwert jetzt lesen" target:self action:@selector(readNowAction:) inView:controlView];
	[self createInfoButton:NSMakeRect( 446, 80, 24, 30 ) key:@"read_now" title:@"Hilfe: Messwert jetzt lesen" inView:controlView];
	[self createButton:NSMakeRect( 482, 80, 174, 30 ) title:@"Aktivieren + prüfen" target:self action:@selector(activateVerifyAction:) inView:controlView];
	[self createInfoButton:NSMakeRect( 706, 80, 24, 30 ) key:@"activate" title:@"Hilfe: Gauge aktivieren + prüfen" inView:controlView];

	[self createButton:NSMakeRect( 264, 116, 126, 30 ) title:@"Degas EIN" target:self action:@selector(degasOnAction:) inView:controlView];
	[self createButton:NSMakeRect( 398, 116, 126, 30 ) title:@"Degas AUS" target:self action:@selector(degasOffAction:) inView:controlView];
	[self createInfoButton:NSMakeRect( 706, 116, 24, 30 ) key:@"degas" title:@"Hilfe: Degas" inView:controlView];

	[self createLabel:NSMakeRect( 16, 154, 40, 24 ) text:@"Filter" inView:controlView bold:NO];
	_filterPopup = [self createPopup:NSMakeRect( 70, 152, 120, 28 ) titles:@[@"fast", @"standard", @"slow"] inView:controlView];
	[_filterPopup selectItemAtIndex:1];
	[self createButton:NSMakeRect( 264, 152, 138, 30 ) title:@"Filter setzen" target:self action:@selector(setFilterAction:) inView:controlView];
	[self createInfoButton:NSMakeRect( 706, 152, 24, 30 ) key:@"filter" title:@"Hilfe: Filter" inView:controlView];

	[self createLabel:NSMakeRect( 16, 190, 104, 24 ) text:@"Kalibrierfaktor" inView:controlView bold:NO];
	_calibrationField = [self createField:NSMakeRect( 128, 188, 90, 28 ) text:@"1.000" inView:controlView];
	[self createButton:NSMakeRect( 264, 188, 118, 30 ) title:@"CAL setzen" target:self action:@selector(setCalibrationAction:) inView:controlView];
	[self createInfoButton:NSMakeRect( 706, 188, 24, 30 ) key:@"calibration" title:@"Hilfe: Kalibrierfaktor" inView:controlView];

	[self createLabel:NSMakeRect( 16, 226, 64, 24 ) text:@"Full Scale" inView:controlView bold:NO];
	_fsrPopup = [self createPopup:NSMakeRect( 128, 224, 126, 28 ) titles:@[@"1000 mbar"] inView:controlView];
	[self createButton:NSMakeRect( 264, 224, 118, 30 ) title:@"FSR setzen" target:self action:@selector(setFsrAction:) inView:controlView];
	[self createInfoButton:NSMakeRect( 706, 224, 24, 30 ) key:@"fsr" title:@"Hilfe: Full Scale / FSR" inView:controlView];

	[self createLabel:NSMakeRect( 16, 262, 76, 24 ) text:@"Offset-Korr." inView:controlView bold:NO];
	_ofcPopup = [self createPopup:NSMakeRect( 128, 260, 126, 28 ) titles:@[@"off", @"on", @"auto"] inView:controlView];
	[self createButton:NSMakeRect( 264, 260, 118, 30 ) title:@"OFC setzen" target:self action:@selector(setOfcAction:) inView:controlView];
	[self createInfoButton:NSMakeRect( 706, 260, 24, 30 ) key:@"ofc" title:@"Hilfe: Offset-Korrektur" inView:controlView];

	NSTextField *channelNameLabel = [self createLabel:NSMakeRect( 16, 298, 122, 24 ) text:@"Kanalname (Gerät)" inView:controlView bold:NO];
	_channelNameField = [self createField:NSMakeRect( 144, 296, 112, 28 ) text:@"Kanal 1" inView:controlView];
	NSButton *channelNameButton = [self createButton:NSMakeRect( 264, 296, 118, 30 ) title:@"Name setzen" target:self action:@selector(setChannelNameAction:) inView:controlView];
	NSButton *channelNameInfo = [self createInfoButton:NSMakeRect( 706, 296, 24, 30 ) key:@"channel_name" title:@"Hilfe: Kanalname" inView:controlView];

	NSTextField *digitsLabel = [self createLabel:NSMakeRect( 16, 334, 40, 24 ) text:@"Digits" inView:controlView bold:NO];
	_digitsPopup = [self createPopup:NSMakeRect( 144, 332, 82, 28 ) titles:@[@"2", @"3"] inView:controlView];
	[_digitsPopup selectItemAtIndex:1];
	NSButton *digitsButton = [self createButton:NSMakeRect( 264, 332, 118, 30 ) title:@"Digits setzen" target:self action:@selector(setDigitsAction:) inView:controlView];
	NSButton *digitsInfo = [self createInfoButton:NSMakeRect( 706, 332, 24, 30 ) key:@"digits" title:@"Hilfe: Digits" inView:controlView];

	NSTextField *contrastLabel = [self createLabel:NSMakeRect( 16, 370, 60, 24 ) text:@"Contrast" inView:controlView bold:NO];
	_contrastField = [self createField:NSMakeRect( 144, 368, 82, 28 ) text:@"10" inView:controlView];
	NSButton *contrastButton = [self createButton:NSMakeRect( 264, 368, 118, 30 ) title:@"Contrast setzen" target:self action:@selector(setContrastAction:) inView:controlView];
	NSButton *contrastInfo = [self createInfoButton:NSMakeRect( 706, 368, 24, 30 ) key:@"contrast" title:@"Hilfe: Contrast" inView:controlView];
	NSTextField *screensaveLabel = [self createLabel:NSMakeRect( 16, 406, 92, 24 ) text:@"Screensave [h]" inView:controlView bold:NO];
	_screensaveField = [self createField:NSMakeRect( 144, 404, 82, 28 ) text:@"0" inView:controlView];
	NSButton *screensaveButton = [self createButton:NSMakeRect( 264, 404, 132, 30 ) title:@"Screensave setzen" target:self action:@selector(setScreensaveAction:) inView:controlView];
	NSButton *screensaveInfo = [self createInfoButton:NSMakeRect( 706, 404, 24, 30 ) key:@"screensave" title:@"Hilfe: Screensave" inView:controlView];

	_displayNameLabel = [self createLabel:NSMakeRect( 16, 442, 92, 24 ) text:@"Anzeigename" inView:controlView bold:NO];
	_displayNameField = [self createField:NSMakeRect( 144, 440, 112, 28 ) text:@"Kanal 1" inView:controlView];
	_displayNameButton = [self createButton:NSMakeRect( 264, 440, 138, 30 ) title:@"Namen speichern" target:self action:@selector(setDisplayNameAction:) inView:controlView];

	_maxiOnlyViews = @[ channelNameLabel, _channelNameField, channelNameButton, channelNameInfo,
						digitsLabel, _digitsPopup, digitsButton, digitsInfo,
						contrastLabel, _contrastField, contrastButton, contrastInfo,
						screensaveLabel, _screensaveField, screensaveButton, screensaveInfo ];

	[_controlBox setHidden:YES];
	_controlVisible = NO;

	_plotView = [[PressurePlotView alloc] initWithFrame:NSMakeRect( 20, 20, _rightView.frame.size.width - 40, _rightView.frame.size.height - 88 )];
	[_rightView addSubview:_plotView];

	_plotFooterView = [[PressureFlippedView alloc] initWithFrame:NSMakeRect( 0, _rightView.frame.size.height - 58, _rightView.frame.size.width, 58 )];
	[_rightView addSubview:_plotFooterView];
	_plotHomeButton = [self createButton:NSMakeRect( 0, 14, 82, 30 ) title:@"Home" target:self action:@selector(resetPlotZoomAction:) inView:_plotFooterView];
	_plotZoomOutButton = [self createButton:NSMakeRect( 0, 14, 44, 30 ) title:@"-" target:self action:@selector(plotZoomOutAction:) inView:_plotFooterView];
	_plotZoomInButton = [self createButton:NSMakeRect( 0, 14, 44, 30 ) title:@"+" target:self action:@selector(plotZoomInAction:) inView:_plotFooterView];
	_clearPlotButton = [self createButton:NSMakeRect( 0, 14, 118, 30 ) title:@"Plot leeren" target:self action:@selector(clearPlotAction:) inView:_plotFooterView];
	_externalPlotButton = [self createButton:NSMakeRect( 0, 14, 128, 30 ) title:@"Externer Plot" target:self action:@selector(openExternalPlotAction:) inView:_plotFooterView];
	_plotCsvButton = [self createButton:NSMakeRect( 0, 14, 118, 30 ) title:@"CSV plotten" target:self action:@selector(plotCsvAction:) inView:_plotFooterView];

	[self layoutInterface];
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
	[_intervalTitleLabel setStringValue:maxi ? @"Polling-Intervall" : @"Continuous Mode"];

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
	for ( NSView *view in _maxiOnlyViews )
		[view setHidden:!maxi];

	const CGFloat displayNameY = maxi ? 442.0 : 298.0;
	[_displayNameLabel setFrame:NSMakeRect( 16, displayNameY, 92, 24 )];
	[_displayNameField setFrame:NSMakeRect( 144, displayNameY - 2.0, 112, 28 )];
	[_displayNameButton setFrame:NSMakeRect( 264, displayNameY - 2.0, 138, 30 )];

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
	[self layoutInterface];
}


- (void)deviceChanged:(id)sender
{
	(void) sender;
	[self updateDeviceProfile];
}


- (void)layoutInterface
{
	NSView *contentView = [_window contentView];
	const CGFloat totalWidth = std::max( 1380.0, contentView.bounds.size.width );
	const CGFloat totalHeight = std::max( 860.0, contentView.bounds.size.height );
	const CGFloat splitterGap = 12.0;
	CGFloat leftWidth = floor( totalWidth * 0.48 );
	leftWidth = std::max( 760.0, std::min( leftWidth, totalWidth - 560.0 ) );
	const CGFloat rightX = leftWidth + splitterGap;
	const CGFloat rightWidth = std::max( 548.0, totalWidth - rightX );

	[_leftScrollView setFrame:NSMakeRect( 0, 0, leftWidth, totalHeight )];
	[_rightView setFrame:NSMakeRect( rightX, 0, rightWidth, totalHeight )];

	const CGFloat outerPadding = 12.0;
	const CGFloat sectionWidth = leftWidth - outerPadding * 2.0;
	const CGFloat cardGap = 12.0;
	const CGFloat cardHeight = 112.0;
	const CGFloat cardSpacing = 12.0;
	const CGFloat sectionStartY = 56.0;

	[_connectionBox setFrame:NSMakeRect( outerPadding, sectionStartY, sectionWidth, 274.0 )];
	NSView *connectionView = [_connectionBox contentView];
	const CGFloat innerLeft = 16.0;
	const CGFloat innerRight = 16.0;
	const CGFloat row1Y = 10.0;
	const CGFloat row2Y = 46.0;
	const CGFloat row3Y = 82.0;
	const CGFloat row4Y = 118.0;
	const CGFloat row5Y = 154.0;
	const CGFloat row6Y = 190.0;
	const CGFloat row7Y = 226.0;
	const CGFloat availableWidth = connectionView.bounds.size.width - innerLeft - innerRight;

	const CGFloat portPopupWidth = std::max( 184.0, std::min( 248.0, availableWidth * 0.30 ) );
	const CGFloat portLabelWidth = 35.0;
	const CGFloat portPopupX = connectionView.bounds.size.width - innerRight - portPopupWidth;
	const CGFloat portLabelX = portPopupX - 42.0;
	const CGFloat devicePopupX = 258.0;
	const CGFloat indicatorX = 210.0;
	const CGFloat devicePopupWidth = std::max( 170.0, portLabelX - 16.0 - devicePopupX );

	[_deviceLabel setFrame:NSMakeRect( innerLeft, row1Y + 4.0, 48.0, 24.0 )];
	[_connectionIndicator setFrame:NSMakeRect( indicatorX, row1Y + 2.0, 18.0, 18.0 )];
	[_devicePopup setFrame:NSMakeRect( devicePopupX, row1Y, devicePopupWidth, 28.0 )];
	[_portLabel setFrame:NSMakeRect( portLabelX, row1Y + 4.0, portLabelWidth, 24.0 )];
	[_portPopup setFrame:NSMakeRect( portPopupX, row1Y, portPopupWidth, 28.0 )];

	const CGFloat commandGap = 8.0;
	const CGFloat commandButtonWidth = floor( (availableWidth - commandGap * 4.0) / 5.0 );
	NSArray<NSButton *> *connectionButtons = @[ _connectButton, _disconnectButton, _refreshPortsButton, _diagnoseButton, _factoryResetButton ];
	for ( NSInteger index = 0; index < connectionButtons.count; index++ )
	{
		NSButton *button = connectionButtons[index];
		const CGFloat buttonX = innerLeft + index * (commandButtonWidth + commandGap);
		[button setFrame:NSMakeRect( buttonX, row2Y, commandButtonWidth, 30.0 )];
	}

	[_measurementLabel setFrame:NSMakeRect( innerLeft, row3Y + 4.0, 68.0, 24.0 )];
	[_measurementIndicator setFrame:NSMakeRect( indicatorX, row3Y + 2.0, 18.0, 18.0 )];
	[_samplesStatusLabel setFrame:NSMakeRect( connectionView.bounds.size.width - innerRight - 122.0, row3Y + 4.0, 122.0, 24.0 )];
	[_measurementStatusLabel setFrame:NSMakeRect( devicePopupX, row3Y + 4.0, CGRectGetMinX( [_samplesStatusLabel frame] ) - 14.0 - devicePopupX, 24.0 )];

	const CGFloat measurementButtonsWidth = availableWidth * 0.62;
	const CGFloat measurementButtonWidth = floor( (measurementButtonsWidth - commandGap * 2.0) / 3.0 );
	[_startLoggingButton setFrame:NSMakeRect( innerLeft, row4Y, measurementButtonWidth, 30.0 )];
	[_newMeasurementButton setFrame:NSMakeRect( innerLeft + measurementButtonWidth + commandGap, row4Y, measurementButtonWidth, 30.0 )];
	[_stopLoggingButton setFrame:NSMakeRect( innerLeft + (measurementButtonWidth + commandGap) * 2.0, row4Y, measurementButtonWidth, 30.0 )];
	const CGFloat liveOnlyX = innerLeft + measurementButtonWidth * 3.0 + commandGap * 2.0 + 12.0;
	[_liveOnlyCheck setFrame:NSMakeRect( liveOnlyX, row4Y + 2.0, connectionView.bounds.size.width - innerRight - liveOnlyX, 26.0 )];

	[_intervalTitleLabel setFrame:NSMakeRect( innerLeft, row5Y + 4.0, 136.0, 24.0 )];
	[_intervalPopup setFrame:NSMakeRect( 164.0, row5Y, 104.0, 28.0 )];
	[_longTermCheck setFrame:NSMakeRect( 276.0, row5Y + 2.0, 136.0, 26.0 )];
	[_longTermField setFrame:NSMakeRect( connectionView.bounds.size.width - innerRight - 214.0, row5Y, 58.0, 28.0 )];
	[_longTermSuffixLabel setFrame:NSMakeRect( connectionView.bounds.size.width - innerRight - 148.0, row5Y + 4.0, 132.0, 24.0 )];

	[_csvLabel setFrame:NSMakeRect( innerLeft, row6Y + 4.0, 36.0, 24.0 )];
	[_csvBrowseButton setFrame:NSMakeRect( connectionView.bounds.size.width - innerRight - 30.0, row6Y, 30.0, 28.0 )];
	[_csvField setFrame:NSMakeRect( 56.0, row6Y, CGRectGetMinX( [_csvBrowseButton frame] ) - 8.0 - 56.0, 28.0 )];

	[_browseCsvButton setFrame:NSMakeRect( innerLeft, row7Y, 118.0, 30.0 )];
	[_fileIndicator setFrame:NSMakeRect( 144.0, row7Y + 6.0, 18.0, 18.0 )];
	[_fileStatusLabel setFrame:NSMakeRect( 176.0, row7Y + 4.0, connectionView.bounds.size.width - innerRight - 176.0, 24.0 )];

	const CGFloat cardsTop = CGRectGetMaxY( [_connectionBox frame] ) + 12.0;
	const NSInteger cardRows = ([self activeChannelCount] + 1) / 2;
	CGFloat currentY = cardsTop;
	const CGFloat cardWidth = (leftWidth - outerPadding * 2.0 - cardGap) / 2.0;
	for ( NSInteger i = 0; i < 6; i++ )
	{
		const NSInteger row = i / 2;
		const NSInteger col = i % 2;
		[_channelCards[i] setFrame:NSMakeRect( outerPadding + col * (cardWidth + cardGap),
											  currentY + row * (cardHeight + cardSpacing),
											  cardWidth,
											  cardHeight )];
	}
	currentY += cardRows * (cardHeight + cardSpacing) - 2.0;

	[_plotSelectionLabel setFrame:NSMakeRect( 16.0, currentY, 120.0, 24.0 )];
	for ( int i = 0; i < 6; i++ )
		[_plotChecks[i] setFrame:NSMakeRect( 145.0 + i * 38.0, currentY - 1.0, 36.0, 24.0 )];
	const CGFloat debugWidth = 148.0;
	const CGFloat renameWidth = 132.0;
	const CGFloat actionGap = 10.0;
	const CGFloat debugX = leftWidth - outerPadding - debugWidth;
	const CGFloat renameX = debugX - actionGap - renameWidth;
	[_renameChannelsButton setFrame:NSMakeRect( renameX, currentY - 2.0, renameWidth, 28.0 )];
	[_debugInfoButton setFrame:NSMakeRect( debugX, currentY - 2.0, debugWidth, 28.0 )];

	currentY += 32.0;
	[_messagesLabel setFrame:NSMakeRect( 16.0, currentY, 90.0, 24.0 )];
	NSScrollView *messagesScroll = [_messagesView enclosingScrollView];
	const CGFloat messagesHeight = std::max( 150.0, std::min( 220.0, floor( _leftScrollView.bounds.size.height * 0.18 ) ) );
	[messagesScroll setFrame:NSMakeRect( outerPadding, currentY + 26.0, sectionWidth, messagesHeight )];

	currentY += messagesHeight + 34.0;
	[_rawBox setFrame:NSMakeRect( outerPadding, currentY, sectionWidth, 64.0 )];
	NSView *rawView = [_rawBox contentView];
	[_rawHelpButton setFrame:NSMakeRect( rawView.bounds.size.width - 38.0, 8.0, 34.0, 30.0 )];
	[_sendRawButton setFrame:NSMakeRect( rawView.bounds.size.width - 192.0, 8.0, 150.0, 30.0 )];
	[_rawField setFrame:NSMakeRect( 14.0, 10.0, CGRectGetMinX( [_sendRawButton frame] ) - 18.0, 28.0 )];

	currentY += 74.0;
	[_toggleControlButton setFrame:NSMakeRect( outerPadding, currentY, sectionWidth, 30.0 )];
	currentY += 40.0;

	if ( _controlVisible )
	{
		[_controlBox setHidden:NO];
		const CGFloat controlHeight = ([self selectedDeviceType] == PressureLoggerDevice_MaxiGauge) ? 516.0 : 374.0;
		[_controlBox setFrame:NSMakeRect( outerPadding, currentY, sectionWidth, controlHeight )];
		currentY += controlHeight + 10.0;
	}
	else
	{
		[_controlBox setHidden:YES];
	}

	const CGFloat contentHeight = std::max( currentY + 16.0, _leftScrollView.bounds.size.height );
	[_leftContentView setFrame:NSMakeRect( 0, 0, leftWidth, contentHeight )];

	const CGFloat rightHeight = _rightView.bounds.size.height;
	const CGFloat footerHeight = 58.0;
	[_plotView setFrame:NSMakeRect( 20.0, 20.0, rightWidth - 40.0, rightHeight - footerHeight - 30.0 )];
	[_plotFooterView setFrame:NSMakeRect( 0, rightHeight - footerHeight, rightWidth, footerHeight )];

	[_plotHomeButton setFrame:NSMakeRect( 20.0, 14.0, 82.0, 30.0 )];
	[_plotZoomOutButton setFrame:NSMakeRect( 110.0, 14.0, 44.0, 30.0 )];
	[_plotZoomInButton setFrame:NSMakeRect( 162.0, 14.0, 44.0, 30.0 )];

	CGFloat buttonRight = rightWidth - 20.0;
	[_plotCsvButton setFrame:NSMakeRect( buttonRight - 118.0, 14.0, 118.0, 30.0 )];
	buttonRight -= 128.0;
	[_externalPlotButton setFrame:NSMakeRect( buttonRight - 128.0, 14.0, 128.0, 30.0 )];
	buttonRight -= 138.0;
	[_clearPlotButton setFrame:NSMakeRect( buttonRight - 118.0, 14.0, 118.0, 30.0 )];
}


- (void)showHelpAction:(id)sender
{
	NSButton *button = (NSButton *) sender;
	NSString *key = [button identifier];
	NSString *title = [button toolTip] ?: @"Hilfe";
	if ( key != nil )
		[self showHelpWindowWithTitle:title key:_ToStdString( key )];
}


- (void)showTextWindowWithTitle:(NSString *)title content:(NSString *)content
{
	NSWindow *textWindow = [[NSWindow alloc] initWithContentRect:NSMakeRect( 0, 0, 820, 680 )
													 styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable)
													   backing:NSBackingStoreBuffered
														 defer:NO];
	[textWindow setTitle:(title ?: @"Text")];
	[textWindow center];

	NSTextView *textView = [self createTextView:NSMakeRect( 10, 10, 800, 660 ) inView:[textWindow contentView]];
	[[textView textStorage] setAttributedString:[[NSAttributedString alloc] initWithString:(content ?: @"") attributes:@{NSFontAttributeName:_MonoFont()}]];
	[textWindow makeKeyAndOrderFront:nil];
}


- (void)editChannelNamesAction:(id)sender
{
	(void) sender;
	const NSInteger channelCount = [self activeChannelCount];
	NSView *accessoryView = [[NSView alloc] initWithFrame:NSMakeRect( 0, 0, 360, 26 + channelCount * 34 )];
	NSMutableArray<NSTextField *> *fields = [NSMutableArray arrayWithCapacity:channelCount];

	for ( NSInteger i = 0; i < channelCount; i++ )
	{
		const CGFloat rowY = 8.0 + i * 34.0;
		NSTextField *label = [self createLabel:NSMakeRect( 0, rowY + 4.0, 76, 24 ) text:[NSString stringWithFormat:@"Kanal %ld", static_cast<long>( i + 1 )] inView:accessoryView bold:NO];
		[label setAlignment:NSTextAlignmentRight];
		NSTextField *field = [self createField:NSMakeRect( 88, rowY, 254, 28 ) text:_ToNSString( _engine.GetDisplayChannelName( [self selectedDeviceType], static_cast<BYTE>( i + 1 ) ) ) inView:accessoryView];
		[fields addObject:field];
	}

	NSAlert *alert = [[NSAlert alloc] init];
	[alert setAlertStyle:NSAlertStyleInformational];
	[alert setMessageText:@"Anzeigenamen bearbeiten"];
	[alert setInformativeText:@"Die Namen werden lokal gespeichert und direkt in Karten und Plot-Legende verwendet."];
	[alert addButtonWithTitle:@"Speichern"];
	[alert addButtonWithTitle:@"Abbrechen"];
	[alert setAccessoryView:accessoryView];
	if ( [alert runModal] != NSAlertFirstButtonReturn )
		return;

	for ( NSInteger i = 0; i < channelCount; i++ )
	{
		const DWORD error = _engine.SetDisplayChannelName( [self selectedDeviceType], static_cast<BYTE>( i + 1 ), _ToStdString( [[fields objectAtIndex:i] stringValue] ) );
		if ( error != EC_OK )
		{
			[self showError:error];
			return;
		}
	}

	[self controlChannelChanged:nil];
	[self refreshUi:nil];
}


- (void)showDebugInfoAction:(id)sender
{
	(void) sender;
	PressureLoggerStateSnapshot snapshot;
	_engine.GetStateSnapshot( &snapshot );

	std::stringstream report;
	report << "Selected device: " << _ToStdString( [_devicePopup titleOfSelectedItem] ) << "\n";
	report << "Selected port: " << _ToStdString( [_portPopup titleOfSelectedItem] ) << "\n";
	report << "Live only: " << (([_liveOnlyCheck state] == NSControlStateValueOn) ? "yes" : "no") << "\n";
	report << "Long term: " << (([_longTermCheck state] == NSControlStateValueOn) ? "yes" : "no") << "\n\n";
	report << _engine.FormatLatestValues( snapshot ) << "\n\n";
	report << "Recent samples\n";
	report << _engine.FormatRecentSamples( snapshot, 8 );

	[self showTextWindowWithTitle:@"Debug-Info" content:_ToNSString( report.str() )];
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
	NSString *name = _ToNSString( _engine.GetDisplayChannelName( [self selectedDeviceType], static_cast<BYTE>( channel ) ) );
	_displayNameField.stringValue = name;
	if ( _channelNameField != nil )
		_channelNameField.stringValue = name;
}


- (void)toggleControlAction:(id)sender
{
	(void) sender;
	_controlVisible = !_controlVisible;
	[_controlBox setHidden:!_controlVisible];
	[_toggleControlButton setTitle:_controlVisible ? @"Steuerung / Parameter ausblenden" : @"Steuerung / Parameter einblenden"];
	[self layoutInterface];
}


- (void)resetPlotZoomAction:(id)sender
{
	(void) sender;
	[_plotView resetZoom];
	if ( _externalPlotView != nil )
		[_externalPlotView resetZoom];
	if ( _csvPlotView != nil )
		[_csvPlotView resetZoom];
}


- (void)plotZoomInAction:(id)sender
{
	(void) sender;
	[_plotView zoomIn];
	if ( _externalPlotView != nil )
		[_externalPlotView zoomIn];
	if ( _csvPlotView != nil )
		[_csvPlotView zoomIn];
}


- (void)plotZoomOutAction:(id)sender
{
	(void) sender;
	[_plotView zoomOut];
	if ( _externalPlotView != nil )
		[_externalPlotView zoomOut];
	if ( _csvPlotView != nil )
		[_csvPlotView zoomOut];
}


- (void)windowDidResize:(NSNotification *)notification
{
	(void) notification;
	[self layoutInterface];
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


- (BOOL)buildSetup:(PressureLoggerConnectionSetup *)setupOut
{
	if ( setupOut == nil )
		return NO;

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
			if ( ![self readDoubleField:_longTermField value:&value label:@"Langzeitmodus"] )
				return NO;
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
			if ( ![self readDoubleField:_longTermField value:&value label:@"Langzeitmodus"] )
				return NO;
			setup.dPollingSeconds = std::max( 1.0, value );
		}
	}

	*setupOut = setup;
	return YES;
}


- (void)connectAction:(id)sender
{
	(void) sender;
	PressureLoggerConnectionSetup setup;
	if ( ![self buildSetup:&setup] )
		return;

	const DWORD error = _engine.Connect( setup );
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
	{
		[_csvField setStringValue:[[panel URL] path]];
		if ( [_liveOnlyCheck state] != NSControlStateValueOn )
			[_fileStatusLabel setStringValue:[NSString stringWithFormat:@"Datei: %@", [_csvField stringValue]]];
	}
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


- (void)clearPlotAction:(id)sender
{
	(void) sender;
	const DWORD error = _engine.ClearHistory();
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
	const DWORD error = _engine.SetDisplayChannelName( [self selectedDeviceType], [self selectedChannel], _ToStdString( [_displayNameField stringValue] ) );
	if ( error != EC_OK )
		[self showError:error];
	else
	{
		[self controlChannelChanged:nil];
		[self refreshUi:nil];
	}
}


- (void)setChannelNameAction:(id)sender
{
	(void) sender;
	const DWORD error = _engine.SetChannelName( [self selectedChannel], _ToStdString( [_channelNameField stringValue] ) );
	if ( error != EC_OK )
		[self showError:error];
	else
	{
		[self controlChannelChanged:nil];
		[self refreshUi:nil];
	}
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
	[self showTextWindowWithTitle:title content:_ToNSString( _engine.GetHelpText( key ) )];
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

	PressureLoggerStateSnapshot snapshot;
	_engine.GetStateSnapshot( &snapshot );
	[_externalPlotView updateWithSnapshot:snapshot engine:&_engine visibleChannels:_plotVisible channelCount:[self activeChannelCount]];
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

		try
		{
			PressureSample sample;
			std::replace( values[0].begin(), values[0].end(), ',', '.' );
			sample.dSecondsSinceStart = std::stod( values[0] );
			for ( size_t channel = 1; channel < headers.size() / 2 + 1; channel++ )
			{
				PressureChannelReading reading;
				reading.byChannel = static_cast<BYTE>( channel );
				reading.nStatusCode = std::stoi( values[1 + (channel - 1) * 2] );
				std::string pressure_text = values[2 + (channel - 1) * 2];
				std::replace( pressure_text.begin(), pressure_text.end(), ',', '.' );
				reading.dPressure = std::stod( pressure_text );
				reading.sStatusText = CPfeifferGaugeDriver::StatusText( reading.nStatusCode );
				sample.ChannelValues.push_back( reading );
			}

			_csvSnapshot.History.push_back( sample );
		}
		catch ( ... )
		{
			continue;
		}
	}

	return YES;
}


- (void)plotCsvAction:(id)sender
{
	(void) sender;
	NSOpenPanel *panel = [NSOpenPanel openPanel];
	#pragma clang diagnostic push
	#pragma clang diagnostic ignored "-Wdeprecated-declarations"
		[panel setAllowedFileTypes:@[@"csv"]];
	#pragma clang diagnostic pop
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
	if ( snapshot.bFaulted )
		[_measurementIndicator setIndicatorColor:[NSColor colorWithCalibratedRed:0.78 green:0.22 blue:0.18 alpha:1.0]];
	else
		[_measurementIndicator setIndicatorColor:snapshot.bMonitoring ? [NSColor colorWithCalibratedRed:0.18 green:0.49 blue:0.20 alpha:1.0] : [NSColor colorWithCalibratedWhite:0.70 alpha:1.0]];
	if ( snapshot.bFaulted && !snapshot.sCsvPath.empty() )
		[_fileIndicator setIndicatorColor:[NSColor colorWithCalibratedRed:0.78 green:0.22 blue:0.18 alpha:1.0]];
	else
		[_fileIndicator setIndicatorColor:snapshot.bLogging ? [NSColor colorWithCalibratedRed:0.18 green:0.49 blue:0.20 alpha:1.0] : [NSColor colorWithCalibratedWhite:0.70 alpha:1.0]];

	NSString *measurementText = @"Nicht verbunden";
	if ( snapshot.bConnected )
		measurementText = snapshot.bFaulted ? @"Monitoringfehler" : (snapshot.bLogging ? @"Logging läuft" : (snapshot.bMonitoring ? @"Monitoring läuft" : @"Bereit"));
	[_measurementStatusLabel setStringValue:measurementText];
	[_samplesStatusLabel setStringValue:[NSString stringWithFormat:@"Sam %u", static_cast<unsigned>( snapshot.dwSampleCount )]];
	NSString *fileText = @"Datei: Keine Datei offen";
	if ( snapshot.bLogging )
		fileText = [NSString stringWithFormat:@"Datei: %@", _ToNSString( snapshot.sCsvPath )];
	else if ( snapshot.bFaulted && !snapshot.sCsvPath.empty() )
		fileText = [NSString stringWithFormat:@"Datei: %@ (nach Fehler geschlossen)", _ToNSString( snapshot.sCsvPath )];
	else if ( snapshot.bMonitoring && ([_liveOnlyCheck state] == NSControlStateValueOn) )
		fileText = @"Datei: Monitoring ohne Dateispeicherung";
	[_fileStatusLabel setStringValue:fileText];

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
