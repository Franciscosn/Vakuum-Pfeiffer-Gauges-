///////////////////////////////////////////////////////////////////////////////////////////////////
//
// CDTPressureLoggerWin.cpp: native Win32 frontend for the portable Pfeiffer pressure logger.
//
// ------------------------------------------------------------------------------------------------
//
// Description:
/// This frontend mirrors the Python pressure logger layout more closely: connection block, channel
/// cards, live plot, message area, raw command box and a collapsible control section. All device
/// communication remains inside 'CPressureLoggerAppEngine' so that the Windows shell can later be
/// embedded into the larger CDT Coater application structure.
//
// Please announce changes and hints to support@n-cdt.com
// Copyright (c) 2026 CDT GmbH
// All rights reserved.
//
///////////////////////////////////////////////////////////////////////////////////////////////////


#include <windows.h>
#include <commdlg.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "Comdlg32.lib")

#include "PressureLoggerAppEngine.h"


using namespace std;


namespace
{
	HMENU _ControlMenu( const int controlId )
	{
		return reinterpret_cast<HMENU>( static_cast<INT_PTR>( controlId ) );
	}


	wstring _ToWide( const string& i_Text )
	{
		if ( i_Text.empty() )
			return L"";

		const int length = MultiByteToWideChar( CP_UTF8, 0, i_Text.c_str(), -1, 0, 0 );
		if ( length <= 0 )
			return L"";

		wstring result( length, L'\0' );
		MultiByteToWideChar( CP_UTF8, 0, i_Text.c_str(), -1, &result[0], length );
		if ( !result.empty() && (result.back() == L'\0') )
			result.pop_back();
		return result;
	}


	string _ToUtf8( const wstring& i_Text )
	{
		if ( i_Text.empty() )
			return "";

		const int length = WideCharToMultiByte( CP_UTF8, 0, i_Text.c_str(), -1, 0, 0, 0, 0 );
		if ( length <= 0 )
			return "";

		string result( length, '\0' );
		WideCharToMultiByte( CP_UTF8, 0, i_Text.c_str(), -1, &result[0], length, 0, 0 );
		if ( !result.empty() && (result.back() == '\0') )
			result.pop_back();
		return result;
	}


	wstring _TrimmedWindowText( HWND hControl )
	{
		const int length = GetWindowTextLengthW( hControl );
		if ( length <= 0 )
			return L"";

		wstring text( length + 1, L'\0' );
		GetWindowTextW( hControl, &text[0], length + 1 );
		text.resize( length );
		return text;
	}


	string _NormalizeDecimalInput( const string& i_Text )
	{
		string normalized = i_Text;
		replace( normalized.begin(), normalized.end(), ',', '.' );
		return normalized;
	}


	bool _IsChecked( HWND hControl )
	{
		return SendMessageW( hControl, BM_GETCHECK, 0, 0 ) == BST_CHECKED;
	}


	void _SetChecked( HWND hControl, const bool i_Checked )
	{
		SendMessageW( hControl, BM_SETCHECK, i_Checked ? BST_CHECKED : BST_UNCHECKED, 0 );
	}


	wstring _FormatScientific( const double i_Value, const int i_Precision )
	{
		if ( !std::isfinite( i_Value ) )
			return L"--";

		wstringstream stream;
		stream.setf( ios::scientific );
		stream << setprecision( i_Precision ) << i_Value;
		return stream.str();
	}


	wstring _FormatFixed( const double i_Value, const int i_Precision )
	{
		wstringstream stream;
		stream.setf( ios::fixed );
		stream << setprecision( i_Precision ) << i_Value;
		return stream.str();
	}


	wstring _FormatAxisLabel( const double i_Value )
	{
		wstringstream stream;
		stream.setf( ios::fixed );
		stream << setprecision( fabs( i_Value ) < 1.0 ? 3 : 2 ) << i_Value;
		return stream.str();
	}


	COLORREF _PlotLineColor( const int i_Index )
	{
		switch ( i_Index )
		{
			case 0: return RGB(  26, 115, 204 );
			case 1: return RGB( 240, 133,  15 );
			case 2: return RGB(  51, 160,  44 );
			case 3: return RGB( 198,  60,  52 );
			case 4: return RGB( 117,  83,  41 );
			default:return RGB( 112,  84, 172 );
		}
	}


	COLORREF _IndicatorGray()
	{
		return RGB( 186, 186, 186 );
	}


	COLORREF _StatusIndicatorColor( const int i_StatusCode, const int i_IndicatorIndex )
	{
		if ( (i_StatusCode == 0) && (i_IndicatorIndex == 0) )
			return RGB(  46, 125,  50 );
		if ( (i_StatusCode == 4) && (i_IndicatorIndex == 1) )
			return RGB( 250, 177,  35 );
		if ( (i_StatusCode == 2) && (i_IndicatorIndex == 2) )
			return RGB( 190,  40,  35 );
		return _IndicatorGray();
	}


	void _FillSolidRect( HDC hdc, const RECT& i_Rect, const COLORREF i_Color )
	{
		HBRUSH hBrush = CreateSolidBrush( i_Color );
		FillRect( hdc, &i_Rect, hBrush );
		DeleteObject( hBrush );
	}


	void _DrawTextRect( HDC hdc, const RECT& i_Rect, const wstring& i_Text, const UINT i_Flags, const HFONT hFont, const COLORREF i_Color )
	{
		HGDIOBJ hOldFont = SelectObject( hdc, hFont );
		SetBkMode( hdc, TRANSPARENT );
		SetTextColor( hdc, i_Color );
		RECT text_rect = i_Rect;
		DrawTextW( hdc, i_Text.c_str(), -1, &text_rect, i_Flags );
		SelectObject( hdc, hOldFont );
	}


	void _SelectComboByText( HWND hCombo, const wstring& i_Text )
	{
		const int index = static_cast<int>( SendMessageW( hCombo, CB_FINDSTRINGEXACT, static_cast<WPARAM>( -1 ), reinterpret_cast<LPARAM>( i_Text.c_str() ) ) );
		if ( index >= 0 )
			SendMessageW( hCombo, CB_SETCURSEL, static_cast<WPARAM>( index ), 0 );
	}


	wstring _OpenFileDialog( HWND hOwner, const bool i_SaveDialog, const wstring& i_InitialPath )
	{
		wchar_t buffer[MAX_PATH * 4] = {0};
		if ( !i_InitialPath.empty() )
			wcsncpy_s( buffer, i_InitialPath.c_str(), _TRUNCATE );

		OPENFILENAMEW ofn;
		ZeroMemory( &ofn, sizeof(ofn) );
		ofn.lStructSize = sizeof(ofn);
		ofn.hwndOwner = hOwner;
		ofn.lpstrFile = buffer;
		ofn.nMaxFile = static_cast<DWORD>( _countof( buffer ) );
		ofn.lpstrFilter = L"CSV-Dateien (*.csv)\0*.csv\0Alle Dateien (*.*)\0*.*\0";
		ofn.nFilterIndex = 1;
		ofn.Flags = OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;

		if ( i_SaveDialog )
		{
			ofn.Flags |= OFN_OVERWRITEPROMPT;
			ofn.lpstrDefExt = L"csv";
			return GetSaveFileNameW( &ofn ) ? wstring( buffer ) : L"";
		}

		ofn.Flags |= OFN_FILEMUSTEXIST;
		return GetOpenFileNameW( &ofn ) ? wstring( buffer ) : L"";
	}
}


class CIndicatorWindow
{

public:

	CIndicatorWindow()
	{
		hWnd = 0;
		Color = _IndicatorGray();
	}

	bool Create( HINSTANCE hInstance, HWND hParent, const RECT& i_Rect )
	{
		RegisterClass( hInstance );
		hWnd = CreateWindowExW( 0,
								L"CDTPressureIndicatorWindow",
								L"",
								WS_CHILD | WS_VISIBLE,
								i_Rect.left,
								i_Rect.top,
								i_Rect.right - i_Rect.left,
								i_Rect.bottom - i_Rect.top,
								hParent,
								0,
								hInstance,
								this );
		return (hWnd != 0);
	}

	void SetColor( const COLORREF i_Color )
	{
		Color = i_Color;
		if ( hWnd != 0 )
			InvalidateRect( hWnd, 0, TRUE );
	}

	HWND Window() const
	{
		return hWnd;
	}

private:

	static void RegisterClass( HINSTANCE hInstance )
	{
		static bool bRegistered = false;
		if ( bRegistered )
			return;

		WNDCLASSEXW window_class;
		ZeroMemory( &window_class, sizeof(window_class) );
		window_class.cbSize = sizeof(window_class);
		window_class.lpfnWndProc = WindowProc;
		window_class.hInstance = hInstance;
		window_class.lpszClassName = L"CDTPressureIndicatorWindow";
		window_class.hCursor = LoadCursorW( 0, IDC_ARROW );
		window_class.hbrBackground = reinterpret_cast<HBRUSH>( COLOR_WINDOW + 1 );
		RegisterClassExW( &window_class );
		bRegistered = true;
	}

	static LRESULT CALLBACK WindowProc( HWND i_hWnd, UINT i_Message, WPARAM i_wParam, LPARAM i_lParam )
	{
		CIndicatorWindow *pWindow = reinterpret_cast<CIndicatorWindow*>( GetWindowLongPtrW( i_hWnd, GWLP_USERDATA ) );

		if ( i_Message == WM_NCCREATE )
		{
			CREATESTRUCTW *create_struct = reinterpret_cast<CREATESTRUCTW*>( i_lParam );
			pWindow = reinterpret_cast<CIndicatorWindow*>( create_struct->lpCreateParams );
			SetWindowLongPtrW( i_hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>( pWindow ) );
			pWindow->hWnd = i_hWnd;
		}

		if ( (pWindow != 0) && (i_Message == WM_PAINT) )
		{
			PAINTSTRUCT ps;
			HDC hdc = BeginPaint( i_hWnd, &ps );

			RECT client_rect;
			GetClientRect( i_hWnd, &client_rect );
			_FillSolidRect( hdc, client_rect, RGB( 255, 255, 255 ) );

			HBRUSH hBrush = CreateSolidBrush( pWindow->Color );
			HBRUSH hOldBrush = reinterpret_cast<HBRUSH>( SelectObject( hdc, hBrush ) );
			HPEN hPen = CreatePen( PS_SOLID, 1, RGB( 120, 120, 120 ) );
			HPEN hOldPen = reinterpret_cast<HPEN>( SelectObject( hdc, hPen ) );

			Ellipse( hdc, client_rect.left + 1, client_rect.top + 1, client_rect.right - 1, client_rect.bottom - 1 );

			SelectObject( hdc, hOldBrush );
			SelectObject( hdc, hOldPen );
			DeleteObject( hBrush );
			DeleteObject( hPen );
			EndPaint( i_hWnd, &ps );
			return 0;
		}

		return DefWindowProcW( i_hWnd, i_Message, i_wParam, i_lParam );
	}

private:

	HWND hWnd;
	COLORREF Color;
};


class CPressureCardWindow
{

public:

	CPressureCardWindow()
	{
		hWnd = 0;
		hMainFont = 0;
		hBoldFont = 0;
		hValueFont = 0;
		StatusCode = 6;
		Value = numeric_limits<double>::quiet_NaN();
	}

	bool Create( HINSTANCE hInstance, HWND hParent, const RECT& i_Rect )
	{
		RegisterClass( hInstance );
		hWnd = CreateWindowExW( 0,
								L"CDTPressureCardWindow",
								L"",
								WS_CHILD | WS_VISIBLE,
								i_Rect.left,
								i_Rect.top,
								i_Rect.right - i_Rect.left,
								i_Rect.bottom - i_Rect.top,
								hParent,
								0,
								hInstance,
								this );
		return (hWnd != 0);
	}

	void SetFonts( const HFONT i_MainFont, const HFONT i_BoldFont, const HFONT i_ValueFont )
	{
		hMainFont = i_MainFont;
		hBoldFont = i_BoldFont;
		hValueFont = i_ValueFont;
		if ( hWnd != 0 )
			InvalidateRect( hWnd, 0, TRUE );
	}

	void SetGeometry( const RECT& i_Rect )
	{
		if ( hWnd != 0 )
			MoveWindow( hWnd, i_Rect.left, i_Rect.top, i_Rect.right - i_Rect.left, i_Rect.bottom - i_Rect.top, TRUE );
	}

	void SetVisible( const bool i_Visible )
	{
		if ( hWnd != 0 )
			ShowWindow( hWnd, i_Visible ? SW_SHOW : SW_HIDE );
	}

	void Update( const string& i_Label, const double i_Value, const int i_StatusCode, const string& i_StatusText )
	{
		Label = i_Label;
		Value = i_Value;
		StatusCode = i_StatusCode;
		StatusText = i_StatusText;
		if ( hWnd != 0 )
			InvalidateRect( hWnd, 0, TRUE );
	}

private:

	static void RegisterClass( HINSTANCE hInstance )
	{
		static bool bRegistered = false;
		if ( bRegistered )
			return;

		WNDCLASSEXW window_class;
		ZeroMemory( &window_class, sizeof(window_class) );
		window_class.cbSize = sizeof(window_class);
		window_class.lpfnWndProc = WindowProc;
		window_class.hInstance = hInstance;
		window_class.lpszClassName = L"CDTPressureCardWindow";
		window_class.hCursor = LoadCursorW( 0, IDC_ARROW );
		window_class.hbrBackground = reinterpret_cast<HBRUSH>( COLOR_WINDOW + 1 );
		RegisterClassExW( &window_class );
		bRegistered = true;
	}

	static LRESULT CALLBACK WindowProc( HWND i_hWnd, UINT i_Message, WPARAM i_wParam, LPARAM i_lParam )
	{
		CPressureCardWindow *pWindow = reinterpret_cast<CPressureCardWindow*>( GetWindowLongPtrW( i_hWnd, GWLP_USERDATA ) );

		if ( i_Message == WM_NCCREATE )
		{
			CREATESTRUCTW *create_struct = reinterpret_cast<CREATESTRUCTW*>( i_lParam );
			pWindow = reinterpret_cast<CPressureCardWindow*>( create_struct->lpCreateParams );
			SetWindowLongPtrW( i_hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>( pWindow ) );
			pWindow->hWnd = i_hWnd;
		}

		if ( (pWindow != 0) && (i_Message == WM_PAINT) )
		{
			pWindow->OnPaint();
			return 0;
		}

		return DefWindowProcW( i_hWnd, i_Message, i_wParam, i_lParam );
	}

	void OnPaint()
	{
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint( hWnd, &ps );

		RECT client_rect;
		GetClientRect( hWnd, &client_rect );

		_FillSolidRect( hdc, client_rect, RGB( 248, 248, 248 ) );
		HPEN hBorderPen = CreatePen( PS_SOLID, 1, RGB( 222, 222, 222 ) );
		HBRUSH hFillBrush = CreateSolidBrush( RGB( 248, 248, 248 ) );
		HPEN hOldPen = reinterpret_cast<HPEN>( SelectObject( hdc, hBorderPen ) );
		HBRUSH hOldBrush = reinterpret_cast<HBRUSH>( SelectObject( hdc, hFillBrush ) );
		RoundRect( hdc, client_rect.left, client_rect.top, client_rect.right, client_rect.bottom, 12, 12 );
		SelectObject( hdc, hOldPen );
		SelectObject( hdc, hOldBrush );
		DeleteObject( hBorderPen );
		DeleteObject( hFillBrush );

		RECT title_rect = {16, 10, client_rect.right - 16, 34};
		_DrawTextRect( hdc, title_rect, _ToWide( Label ), DT_LEFT | DT_VCENTER | DT_SINGLELINE, hBoldFont ? hBoldFont : reinterpret_cast<HFONT>( GetStockObject( DEFAULT_GUI_FONT ) ), RGB( 30, 30, 30 ) );

		RECT value_rect = {16, 42, client_rect.right - 16, 82};
		_DrawTextRect( hdc, value_rect, _FormatScientific( Value, 4 ), DT_LEFT | DT_VCENTER | DT_SINGLELINE, hValueFont ? hValueFont : reinterpret_cast<HFONT>( GetStockObject( DEFAULT_GUI_FONT ) ), RGB( 25, 25, 25 ) );

		RECT status_rect = {16, 86, client_rect.right - 16, 108};
		_DrawTextRect( hdc, status_rect, _ToWide( StatusText.empty() ? string("--") : StatusText ), DT_LEFT | DT_VCENTER | DT_SINGLELINE, hMainFont ? hMainFont : reinterpret_cast<HFONT>( GetStockObject( DEFAULT_GUI_FONT ) ), RGB( 45, 45, 45 ) );

		const int label_y = client_rect.bottom - 26;
		const int indicator_y = client_rect.bottom - 30;
		const int indicator_size = 16;
		const int x_ok = client_rect.right - 182;
		const int x_off = client_rect.right - 100;
		const int x_or = client_rect.right - 36;

		for ( int i = 0; i < 3; i++ )
		{
			int x = x_ok;
			wstring label = L"OK";
			if ( i == 1 )
			{
				x = x_off;
				label = L"AUS";
			}
			else if ( i == 2 )
			{
				x = x_or;
				label = L"OR";
			}

			HBRUSH hBrush = CreateSolidBrush( _StatusIndicatorColor( StatusCode, i ) );
			HBRUSH hOld = reinterpret_cast<HBRUSH>( SelectObject( hdc, hBrush ) );
			HPEN hPen = CreatePen( PS_SOLID, 1, RGB( 120, 120, 120 ) );
			HPEN hOldCardPen = reinterpret_cast<HPEN>( SelectObject( hdc, hPen ) );
			Ellipse( hdc, x, indicator_y, x + indicator_size, indicator_y + indicator_size );
			SelectObject( hdc, hOld );
			SelectObject( hdc, hOldCardPen );
			DeleteObject( hBrush );
			DeleteObject( hPen );

			RECT label_rect = {x - 36, label_y - 2, x - 4, label_y + 18};
			_DrawTextRect( hdc, label_rect, label, DT_RIGHT | DT_VCENTER | DT_SINGLELINE, hMainFont ? hMainFont : reinterpret_cast<HFONT>( GetStockObject( DEFAULT_GUI_FONT ) ), RGB( 45, 45, 45 ) );
		}

		EndPaint( hWnd, &ps );
	}

private:

	HWND hWnd;
	HFONT hMainFont;
	HFONT hBoldFont;
	HFONT hValueFont;
	string Label;
	string StatusText;
	int StatusCode;
	double Value;
};


class CPressurePlotWindow
{

public:

	CPressurePlotWindow()
	{
		hWnd = 0;
		hMainFont = 0;
		hMonoFont = 0;
		pEngine = 0;
		ChannelCount = 2;
		for ( size_t i = 0; i < VisibleChannels.size(); i++ )
			VisibleChannels[i] = (i < 2);
	}

	bool Create( HINSTANCE hInstance, HWND hParent, const RECT& i_Rect )
	{
		RegisterClass( hInstance );
		hWnd = CreateWindowExW( WS_EX_CLIENTEDGE,
								L"CDTPressurePlotWindow",
								L"",
								WS_CHILD | WS_VISIBLE,
								i_Rect.left,
								i_Rect.top,
								i_Rect.right - i_Rect.left,
								i_Rect.bottom - i_Rect.top,
								hParent,
								0,
								hInstance,
								this );
		return (hWnd != 0);
	}

	void SetFonts( const HFONT i_MainFont, const HFONT i_MonoFont )
	{
		hMainFont = i_MainFont;
		hMonoFont = i_MonoFont;
		if ( hWnd != 0 )
			InvalidateRect( hWnd, 0, TRUE );
	}

	void SetGeometry( const RECT& i_Rect )
	{
		if ( hWnd != 0 )
			MoveWindow( hWnd, i_Rect.left, i_Rect.top, i_Rect.right - i_Rect.left, i_Rect.bottom - i_Rect.top, TRUE );
	}

	void UpdateData( const PressureLoggerStateSnapshot& i_Snapshot,
					 CPressureLoggerAppEngine *i_pEngine,
					 const bool *i_pVisibleChannels,
					 const int i_ChannelCount )
	{
		Snapshot = i_Snapshot;
		pEngine = i_pEngine;
		ChannelCount = max( 1, min( 6, i_ChannelCount ) );
		for ( int i = 0; i < 6; i++ )
			VisibleChannels[i] = (i_pVisibleChannels != 0) ? i_pVisibleChannels[i] : (i < 2);

		if ( hWnd != 0 )
			InvalidateRect( hWnd, 0, TRUE );
	}

	HWND Window() const
	{
		return hWnd;
	}

private:

	static void RegisterClass( HINSTANCE hInstance )
	{
		static bool bRegistered = false;
		if ( bRegistered )
			return;

		WNDCLASSEXW window_class;
		ZeroMemory( &window_class, sizeof(window_class) );
		window_class.cbSize = sizeof(window_class);
		window_class.lpfnWndProc = WindowProc;
		window_class.hInstance = hInstance;
		window_class.lpszClassName = L"CDTPressurePlotWindow";
		window_class.hCursor = LoadCursorW( 0, IDC_ARROW );
		window_class.hbrBackground = reinterpret_cast<HBRUSH>( COLOR_WINDOW + 1 );
		RegisterClassExW( &window_class );
		bRegistered = true;
	}

	static LRESULT CALLBACK WindowProc( HWND i_hWnd, UINT i_Message, WPARAM i_wParam, LPARAM i_lParam )
	{
		CPressurePlotWindow *pWindow = reinterpret_cast<CPressurePlotWindow*>( GetWindowLongPtrW( i_hWnd, GWLP_USERDATA ) );

		if ( i_Message == WM_NCCREATE )
		{
			CREATESTRUCTW *create_struct = reinterpret_cast<CREATESTRUCTW*>( i_lParam );
			pWindow = reinterpret_cast<CPressurePlotWindow*>( create_struct->lpCreateParams );
			SetWindowLongPtrW( i_hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>( pWindow ) );
			pWindow->hWnd = i_hWnd;
		}

		if ( (pWindow != 0) && (i_Message == WM_PAINT) )
		{
			pWindow->OnPaint();
			return 0;
		}

		return DefWindowProcW( i_hWnd, i_Message, i_wParam, i_lParam );
	}

	void DrawPolylineSegment( HDC hdc, const vector<POINT>& i_Points, const COLORREF i_Color )
	{
		if ( i_Points.size() < 2 )
			return;

		HPEN hPen = CreatePen( PS_SOLID, 2, i_Color );
		HPEN hOldPen = reinterpret_cast<HPEN>( SelectObject( hdc, hPen ) );
		Polyline( hdc, &i_Points[0], static_cast<int>( i_Points.size() ) );
		SelectObject( hdc, hOldPen );
		DeleteObject( hPen );
	}

	void OnPaint()
	{
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint( hWnd, &ps );

		RECT client_rect;
		GetClientRect( hWnd, &client_rect );
		_FillSolidRect( hdc, client_rect, RGB( 255, 255, 255 ) );

		const int left = 84;
		const int right = 26;
		const int top = 26;
		const int bottom = 72;
		RECT plot_rect = {left, top, client_rect.right - right, client_rect.bottom - bottom};
		if ( plot_rect.right <= plot_rect.left || plot_rect.bottom <= plot_rect.top )
		{
			EndPaint( hWnd, &ps );
			return;
		}

		vector<vector<double>> times_by_channel( ChannelCount );
		vector<vector<double>> values_by_channel( ChannelCount );

		double x_min = -0.05;
		double x_max = 0.05;
		double y_min = 1.0;
		double y_max = 10.0;
		bool has_data = false;

		if ( pEngine != 0 )
		{
			for ( int channel = 1; channel <= ChannelCount; channel++ )
			{
				if ( !VisibleChannels[channel - 1] )
					continue;

				pEngine->BuildPlotSeries( Snapshot, static_cast<BYTE>( channel ), &times_by_channel[channel - 1], &values_by_channel[channel - 1] );
				for ( size_t i = 0; i < times_by_channel[channel - 1].size(); i++ )
				{
					const double value = values_by_channel[channel - 1][i];
					x_min = has_data ? min( x_min, times_by_channel[channel - 1][i] ) : times_by_channel[channel - 1][i];
					x_max = has_data ? max( x_max, times_by_channel[channel - 1][i] ) : max( times_by_channel[channel - 1][i], 0.05 );
					if ( std::isfinite( value ) && (value > 0.0) )
					{
						y_min = has_data ? min( y_min, value ) : value;
						y_max = has_data ? max( y_max, value ) : value;
						has_data = true;
					}
				}
			}
		}

		if ( !has_data )
		{
			x_min = -0.05;
			x_max = 0.05;
			y_min = 1.0;
			y_max = 10.0;
		}
		else
		{
			if ( fabs( x_max - x_min ) < 1e-9 )
			{
				x_min -= 0.5;
				x_max += 0.5;
			}
			const double log_min = floor( log10( max( y_min, 1e-12 ) ) );
			const double log_max = ceil( log10( max( y_max, y_min * 1.01 ) ) );
			y_min = pow( 10.0, log_min );
			y_max = pow( 10.0, max( log_max, log_min + 1.0 ) );
		}

		HPEN hBorderPen = CreatePen( PS_SOLID, 1, RGB( 80, 80, 80 ) );
		HPEN hOldPen = reinterpret_cast<HPEN>( SelectObject( hdc, hBorderPen ) );
		HBRUSH hNullBrush = reinterpret_cast<HBRUSH>( GetStockObject( NULL_BRUSH ) );
		HBRUSH hOldBrush = reinterpret_cast<HBRUSH>( SelectObject( hdc, hNullBrush ) );
		Rectangle( hdc, plot_rect.left, plot_rect.top, plot_rect.right, plot_rect.bottom );
		SelectObject( hdc, hOldPen );
		SelectObject( hdc, hOldBrush );
		DeleteObject( hBorderPen );

		HPEN hGridPen = CreatePen( PS_SOLID, 1, RGB( 214, 214, 214 ) );
		hOldPen = reinterpret_cast<HPEN>( SelectObject( hdc, hGridPen ) );
		for ( int i = 0; i <= 5; i++ )
		{
			const int x = plot_rect.left + (plot_rect.right - plot_rect.left) * i / 5;
			MoveToEx( hdc, x, plot_rect.top, 0 );
			LineTo( hdc, x, plot_rect.bottom );

			const double x_value = x_min + (x_max - x_min) * static_cast<double>( i ) / 5.0;
			RECT x_label_rect = {x - 36, plot_rect.bottom + 6, x + 36, plot_rect.bottom + 24};
			_DrawTextRect( hdc, x_label_rect, _FormatAxisLabel( x_value ), DT_CENTER | DT_TOP | DT_SINGLELINE, hMonoFont ? hMonoFont : reinterpret_cast<HFONT>( GetStockObject( ANSI_FIXED_FONT ) ), RGB( 30, 30, 30 ) );
		}

		for ( int i = 0; i <= 4; i++ )
		{
			const int y = plot_rect.top + (plot_rect.bottom - plot_rect.top) * i / 4;
			MoveToEx( hdc, plot_rect.left, y, 0 );
			LineTo( hdc, plot_rect.right, y );

			const double log_value = log10( y_max ) - (log10( y_max ) - log10( y_min )) * static_cast<double>( i ) / 4.0;
			const double axis_value = pow( 10.0, log_value );
			RECT y_label_rect = {8, y - 10, left - 10, y + 10};
			_DrawTextRect( hdc, y_label_rect, _FormatScientific( axis_value, 1 ), DT_RIGHT | DT_VCENTER | DT_SINGLELINE, hMonoFont ? hMonoFont : reinterpret_cast<HFONT>( GetStockObject( ANSI_FIXED_FONT ) ), RGB( 30, 30, 30 ) );
		}
		SelectObject( hdc, hOldPen );
		DeleteObject( hGridPen );

		RECT x_axis_label_rect = {plot_rect.left, client_rect.bottom - 34, plot_rect.right, client_rect.bottom - 10};
		_DrawTextRect( hdc, x_axis_label_rect, L"Zeit seit Messstart [s]", DT_CENTER | DT_VCENTER | DT_SINGLELINE, hMainFont ? hMainFont : reinterpret_cast<HFONT>( GetStockObject( DEFAULT_GUI_FONT ) ), RGB( 30, 30, 30 ) );

		LOGFONTW vertical_font;
		ZeroMemory( &vertical_font, sizeof(vertical_font) );
		vertical_font.lfHeight = -18;
		vertical_font.lfEscapement = 900;
		vertical_font.lfOrientation = 900;
		wcscpy_s( vertical_font.lfFaceName, L"Segoe UI" );
		HFONT hVerticalFont = CreateFontIndirectW( &vertical_font );
		RECT y_axis_label_rect = {8, plot_rect.top + 100, 36, plot_rect.bottom - 100};
		_DrawTextRect( hdc, y_axis_label_rect, L"Druck", DT_CENTER | DT_VCENTER | DT_SINGLELINE, hVerticalFont, RGB( 30, 30, 30 ) );
		DeleteObject( hVerticalFont );

		for ( int channel = 1; channel <= ChannelCount; channel++ )
		{
			if ( !VisibleChannels[channel - 1] )
				continue;

			vector<POINT> segment;
			for ( size_t i = 0; i < times_by_channel[channel - 1].size(); i++ )
			{
				const double value = values_by_channel[channel - 1][i];
				if ( !std::isfinite( value ) || (value <= 0.0) )
				{
					DrawPolylineSegment( hdc, segment, _PlotLineColor( channel - 1 ) );
					segment.clear();
					continue;
				}

				const double normalized_x = (times_by_channel[channel - 1][i] - x_min) / (x_max - x_min);
				const double normalized_y = (log10( value ) - log10( y_min )) / (log10( y_max ) - log10( y_min ));

				POINT point;
				point.x = plot_rect.left + static_cast<int>( normalized_x * (plot_rect.right - plot_rect.left) );
				point.y = plot_rect.bottom - static_cast<int>( normalized_y * (plot_rect.bottom - plot_rect.top) );
				segment.push_back( point );
			}
			DrawPolylineSegment( hdc, segment, _PlotLineColor( channel - 1 ) );
		}

		int visible_count = 0;
		for ( int i = 0; i < ChannelCount; i++ )
			if ( VisibleChannels[i] )
				visible_count++;

		if ( visible_count > 0 )
		{
			RECT legend_rect = {plot_rect.right - 260, plot_rect.top + 10, plot_rect.right - 10, plot_rect.top + 20 + visible_count * 24};
			_FillSolidRect( hdc, legend_rect, RGB( 255, 255, 255 ) );
			HPEN hLegendPen = CreatePen( PS_SOLID, 1, RGB( 212, 212, 212 ) );
			hOldPen = reinterpret_cast<HPEN>( SelectObject( hdc, hLegendPen ) );
			SelectObject( hdc, GetStockObject( NULL_BRUSH ) );
			RoundRect( hdc, legend_rect.left, legend_rect.top, legend_rect.right, legend_rect.bottom, 10, 10 );
			SelectObject( hdc, hOldPen );
			DeleteObject( hLegendPen );

			int legend_y = legend_rect.top + 10;
			for ( int channel = 1; channel <= ChannelCount; channel++ )
			{
				if ( !VisibleChannels[channel - 1] )
					continue;

				HPEN hLinePen = CreatePen( PS_SOLID, 2, _PlotLineColor( channel - 1 ) );
				hOldPen = reinterpret_cast<HPEN>( SelectObject( hdc, hLinePen ) );
				MoveToEx( hdc, legend_rect.left + 14, legend_y + 8, 0 );
				LineTo( hdc, legend_rect.left + 44, legend_y + 8 );
				SelectObject( hdc, hOldPen );
				DeleteObject( hLinePen );

				string label = (channel >= 1 && channel <= static_cast<int>( Snapshot.CombinedChannelLabels.size() ))
					? Snapshot.CombinedChannelLabels[channel - 1]
					: ((pEngine != 0) ? pEngine->FormatCombinedChannelLabel( Snapshot.Setup.DeviceType, static_cast<BYTE>( channel ) ) : ("Kanal " + to_string( channel )));
				RECT label_rect = {legend_rect.left + 52, legend_y - 2, legend_rect.right - 10, legend_y + 18};
				_DrawTextRect( hdc, label_rect, _ToWide( label ), DT_LEFT | DT_VCENTER | DT_SINGLELINE, hMainFont ? hMainFont : reinterpret_cast<HFONT>( GetStockObject( DEFAULT_GUI_FONT ) ), RGB( 20, 20, 20 ) );
				legend_y += 24;
			}
		}

		EndPaint( hWnd, &ps );
	}

private:

	HWND hWnd;
	HFONT hMainFont;
	HFONT hMonoFont;
	CPressureLoggerAppEngine *pEngine;
	PressureLoggerStateSnapshot Snapshot;
	array<bool, 6> VisibleChannels;
	int ChannelCount;
};


class CStandalonePlotWindow
{

public:

	CStandalonePlotWindow()
	{
		hWnd = 0;
		hMainFont = 0;
		hMonoFont = 0;
		hInstance = 0;
	}

	void SetFonts( const HFONT i_MainFont, const HFONT i_MonoFont )
	{
		hMainFont = i_MainFont;
		hMonoFont = i_MonoFont;
		Plot.SetFonts( hMainFont, hMonoFont );
	}

	bool CreateIfNeeded( HINSTANCE i_hInstance, const wchar_t *i_Title )
	{
		hInstance = i_hInstance;
		if ( hWnd != 0 )
		{
			ShowWindow( hWnd, SW_SHOW );
			SetForegroundWindow( hWnd );
			return true;
		}

		RegisterClass( i_hInstance );
		hWnd = CreateWindowExW( 0,
								L"CDTPressureStandalonePlotWindow",
								i_Title,
								WS_OVERLAPPEDWINDOW | WS_VISIBLE,
								CW_USEDEFAULT,
								CW_USEDEFAULT,
								1100,
								760,
								0,
								0,
								i_hInstance,
								this );
		return (hWnd != 0);
	}

	void UpdateData( const PressureLoggerStateSnapshot& i_Snapshot,
					 CPressureLoggerAppEngine *i_pEngine,
					 const bool *i_pVisibleChannels,
					 const int i_ChannelCount )
	{
		if ( hWnd != 0 )
			Plot.UpdateData( i_Snapshot, i_pEngine, i_pVisibleChannels, i_ChannelCount );
	}

	HWND Window() const
	{
		return hWnd;
	}

private:

	static void RegisterClass( HINSTANCE hInstance )
	{
		static bool bRegistered = false;
		if ( bRegistered )
			return;

		WNDCLASSEXW window_class;
		ZeroMemory( &window_class, sizeof(window_class) );
		window_class.cbSize = sizeof(window_class);
		window_class.lpfnWndProc = WindowProc;
		window_class.hInstance = hInstance;
		window_class.lpszClassName = L"CDTPressureStandalonePlotWindow";
		window_class.hCursor = LoadCursorW( 0, IDC_ARROW );
		window_class.hbrBackground = reinterpret_cast<HBRUSH>( COLOR_WINDOW + 1 );
		RegisterClassExW( &window_class );
		bRegistered = true;
	}

	static LRESULT CALLBACK WindowProc( HWND i_hWnd, UINT i_Message, WPARAM i_wParam, LPARAM i_lParam )
	{
		CStandalonePlotWindow *pWindow = reinterpret_cast<CStandalonePlotWindow*>( GetWindowLongPtrW( i_hWnd, GWLP_USERDATA ) );

		if ( i_Message == WM_NCCREATE )
		{
			CREATESTRUCTW *create_struct = reinterpret_cast<CREATESTRUCTW*>( i_lParam );
			pWindow = reinterpret_cast<CStandalonePlotWindow*>( create_struct->lpCreateParams );
			SetWindowLongPtrW( i_hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>( pWindow ) );
			pWindow->hWnd = i_hWnd;
		}

		if ( pWindow != 0 )
		{
			switch ( i_Message )
			{
				case WM_CREATE:
				{
					RECT client_rect;
					GetClientRect( i_hWnd, &client_rect );
					pWindow->Plot.Create( pWindow->hInstance, i_hWnd, client_rect );
					pWindow->Plot.SetFonts( pWindow->hMainFont, pWindow->hMonoFont );
					return 0;
				}

				case WM_SIZE:
				{
					RECT client_rect;
					GetClientRect( i_hWnd, &client_rect );
					pWindow->Plot.SetGeometry( client_rect );
					return 0;
				}

				case WM_CLOSE:
					DestroyWindow( i_hWnd );
					return 0;

				case WM_NCDESTROY:
					pWindow->hWnd = 0;
					SetWindowLongPtrW( i_hWnd, GWLP_USERDATA, 0 );
					return 0;
			}
		}

		return DefWindowProcW( i_hWnd, i_Message, i_wParam, i_lParam );
	}

private:

	HWND hWnd;
	HINSTANCE hInstance;
	HFONT hMainFont;
	HFONT hMonoFont;
	CPressurePlotWindow Plot;
};


class CTextDisplayWindow
{

public:

	explicit CTextDisplayWindow( const wchar_t *i_Title, const wstring& i_Text )
	{
		Title = i_Title ? i_Title : L"Text";
		Text = i_Text;
		hWnd = 0;
		hEdit = 0;
	}

	void Show( HINSTANCE i_hInstance, HWND hOwner )
	{
		RegisterClass( i_hInstance );
		CreateWindowExW( 0,
						 L"CDTPressureTextDisplayWindow",
						 Title.c_str(),
						 WS_OVERLAPPEDWINDOW | WS_VISIBLE,
						 CW_USEDEFAULT,
						 CW_USEDEFAULT,
						 820,
						 680,
						 hOwner,
						 0,
						 i_hInstance,
						 this );
	}

private:

	static void RegisterClass( HINSTANCE hInstance )
	{
		static bool bRegistered = false;
		if ( bRegistered )
			return;

		WNDCLASSEXW window_class;
		ZeroMemory( &window_class, sizeof(window_class) );
		window_class.cbSize = sizeof(window_class);
		window_class.lpfnWndProc = WindowProc;
		window_class.hInstance = hInstance;
		window_class.lpszClassName = L"CDTPressureTextDisplayWindow";
		window_class.hCursor = LoadCursorW( 0, IDC_ARROW );
		window_class.hbrBackground = reinterpret_cast<HBRUSH>( COLOR_WINDOW + 1 );
		RegisterClassExW( &window_class );
		bRegistered = true;
	}

	static LRESULT CALLBACK WindowProc( HWND i_hWnd, UINT i_Message, WPARAM i_wParam, LPARAM i_lParam )
	{
		CTextDisplayWindow *pWindow = reinterpret_cast<CTextDisplayWindow*>( GetWindowLongPtrW( i_hWnd, GWLP_USERDATA ) );

		if ( i_Message == WM_NCCREATE )
		{
			CREATESTRUCTW *create_struct = reinterpret_cast<CREATESTRUCTW*>( i_lParam );
			pWindow = reinterpret_cast<CTextDisplayWindow*>( create_struct->lpCreateParams );
			SetWindowLongPtrW( i_hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>( pWindow ) );
			pWindow->hWnd = i_hWnd;
		}

		if ( pWindow != 0 )
		{
			switch ( i_Message )
			{
				case WM_CREATE:
				{
					pWindow->hEdit = CreateWindowExW( WS_EX_CLIENTEDGE,
												   L"EDIT",
												   pWindow->Text.c_str(),
												   WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_READONLY | WS_VSCROLL | WS_HSCROLL,
												   10,
												   10,
												   760,
												   600,
												   i_hWnd,
												   0,
												   GetModuleHandleW( 0 ),
												   0 );

					HFONT hFont = CreateFontW( -17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
												  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
												  FIXED_PITCH | FF_MODERN, L"Consolas" );
					SendMessageW( pWindow->hEdit, WM_SETFONT, reinterpret_cast<WPARAM>( hFont ), TRUE );
					SetWindowLongPtrW( pWindow->hEdit, GWLP_USERDATA, reinterpret_cast<LONG_PTR>( hFont ) );
					return 0;
				}

				case WM_SIZE:
					if ( pWindow->hEdit != 0 )
						MoveWindow( pWindow->hEdit, 10, 10, LOWORD( i_lParam ) - 20, HIWORD( i_lParam ) - 20, TRUE );
					return 0;

				case WM_NCDESTROY:
				{
					if ( pWindow->hEdit != 0 )
					{
						HFONT hFont = reinterpret_cast<HFONT>( GetWindowLongPtrW( pWindow->hEdit, GWLP_USERDATA ) );
						if ( hFont != 0 )
							DeleteObject( hFont );
					}
					SetWindowLongPtrW( i_hWnd, GWLP_USERDATA, 0 );
					delete pWindow;
					return 0;
				}
			}
		}

		return DefWindowProcW( i_hWnd, i_Message, i_wParam, i_lParam );
	}

private:

	wstring Title;
	wstring Text;
	HWND hWnd;
	HWND hEdit;
};


class CMainWindow
{

public:

	CMainWindow();
	~CMainWindow();

	bool Create( HINSTANCE hInstance );
	HWND Window() const { return hWnd; }

	static LRESULT CALLBACK WindowProc( HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam );

private:

	enum ControlId
	{
		ID_COMBO_DEVICE = 1000,
		ID_COMBO_PORT,
		ID_BUTTON_REFRESH_PORTS,
		ID_BUTTON_CONNECT,
		ID_BUTTON_DISCONNECT,
		ID_BUTTON_NEW_MEASUREMENT,
		ID_BUTTON_FACTORY_RESET,
		ID_BUTTON_START_LOGGING,
		ID_BUTTON_STOP_LOGGING,
		ID_CHECK_LIVE_ONLY,
		ID_COMBO_INTERVAL,
		ID_CHECK_LONG_TERM,
		ID_EDIT_LONG_TERM,
		ID_EDIT_CSV,
		ID_BUTTON_BROWSE_CSV,
		ID_BUTTON_BROWSE_CSV_ALT,
		ID_CHECK_PLOT_1,
		ID_CHECK_PLOT_2,
		ID_CHECK_PLOT_3,
		ID_CHECK_PLOT_4,
		ID_CHECK_PLOT_5,
		ID_CHECK_PLOT_6,
		ID_EDIT_MESSAGES,
		ID_EDIT_RAW,
		ID_BUTTON_SEND_RAW,
		ID_BUTTON_RAW_HELP,
		ID_BUTTON_TOGGLE_CONTROL,
		ID_COMBO_CONTROL_CHANNEL,
		ID_BUTTON_GAUGE_ON,
		ID_BUTTON_GAUGE_OFF,
		ID_BUTTON_READ_NOW,
		ID_BUTTON_ACTIVATE_VERIFY,
		ID_BUTTON_DIAGNOSE,
		ID_COMBO_UNIT,
		ID_BUTTON_SET_UNIT,
		ID_BUTTON_DEGAS_ON,
		ID_BUTTON_DEGAS_OFF,
		ID_COMBO_FILTER,
		ID_BUTTON_SET_FILTER,
		ID_EDIT_CALIBRATION,
		ID_BUTTON_SET_CALIBRATION,
		ID_COMBO_FSR,
		ID_BUTTON_SET_FSR,
		ID_COMBO_OFC,
		ID_BUTTON_SET_OFC,
		ID_EDIT_DISPLAY_NAME,
		ID_BUTTON_SET_DISPLAY_NAME,
		ID_COMBO_DIGITS,
		ID_BUTTON_SET_DIGITS,
		ID_EDIT_CONTRAST,
		ID_BUTTON_SET_CONTRAST,
		ID_EDIT_SCREENSAVE,
		ID_BUTTON_SET_SCREENSAVE,
		ID_BUTTON_EXTERNAL_PLOT,
		ID_BUTTON_PLOT_CSV
	};

private:

	LRESULT HandleMessage( UINT message, WPARAM wParam, LPARAM lParam );

	void CreateControls();
	void ApplyFonts();
	void ApplyDefaultValues();
	void LayoutChildren();
	void RefreshPorts();
	void UpdateDeviceProfile();
	void UpdateUiFromState();
	void SyncDisplayNameField();
	void ShowError( const DWORD i_ErrorCode );
	void ShowTextWindow( const wchar_t *i_Title, const string& i_Text );

	HWND CreateGroup( int x, int y, int width, int height, const wchar_t *text );
	HWND CreateLabel( int x, int y, int width, int height, const wchar_t *text, const bool i_Bold = false );
	HWND CreateEdit( int x, int y, int width, int height, int controlId, const wchar_t *text, DWORD extraStyle = 0 );
	HWND CreateButton( int x, int y, int width, int height, int controlId, const wchar_t *text, DWORD style = BS_PUSHBUTTON );
	HWND CreateCheckbox( int x, int y, int width, int height, int controlId, const wchar_t *text );
	HWND CreateCombo( int x, int y, int width, int height, int controlId, const bool i_Editable = false );
	void FillCombo( HWND hCombo, const vector<wstring>& i_Items, const int i_SelectedIndex = 0 );
	void SetWindowTextUtf8( HWND hControl, const string& i_Text ) const;
	string ReadControlTextUtf8( HWND hControl ) const;
	int ReadInt( HWND hControl, const wchar_t *name, bool *pOk ) const;
	double ReadDouble( HWND hControl, const wchar_t *name, bool *pOk ) const;
	BYTE SelectedChannel( bool *pOk ) const;
	PressureLoggerDeviceType SelectedDeviceType() const;
	PressureLoggerDeviceType ActiveDeviceTypeForUi() const;
	int ActiveChannelCount() const;
	int SelectedUnitCode() const;
	int SelectedFilterCode() const;
	int SelectedFsrCode() const;
	int SelectedOfcCode() const;
	int SelectedDigits() const;
	PressureLoggerConnectionSetup BuildSetup( bool *pOk ) const;
	void SetPlotCheckboxDefaults();
	void UpdatePlotCheckboxAvailability();
	void UpdateControlButtonsAvailability( const bool i_Connected );
	bool LoadCsvSnapshot( const wstring& i_Path, PressureLoggerStateSnapshot *pSnapshot );
	void OpenExternalPlot();
	void OpenCsvPlot();

	void OnConnect();
	void OnDisconnect();
	void OnNewMeasurement();
	void OnStartLogging();
	void OnStopLogging();
	void OnFactoryReset();
	void OnSetUnit();
	void OnGauge( const bool i_On );
	void OnReadNow();
	void OnDiagnose();
	void OnActivateVerify();
	void OnDegas( const bool i_On );
	void OnSetFilter();
	void OnSetCalibration();
	void OnSetFsr();
	void OnSetOfc();
	void OnSetDisplayName();
	void OnSetDigits();
	void OnSetContrast();
	void OnSetScreensave();
	void OnSendRaw();
	void OnBrowseCsv( const bool i_SaveDialog );
	void OnPlotCsv();
	void OnToggleControlPanel();

private:

	HINSTANCE hInstance;
	HWND hWnd;
	HFONT hMainFont;
	HFONT hBoldFont;
	HFONT hMonoFont;
	HFONT hValueFont;

	HWND hTitleLabel;
	HWND hConnectionGroup;
	HWND hDeviceCombo;
	HWND hPortCombo;
	HWND hConnectButton;
	HWND hDisconnectButton;
	HWND hRefreshPortsButton;
	HWND hFactoryResetButton;
	HWND hMeasurementStatusLabel;
	HWND hSamplesLabel;
	HWND hCsvStatusLabel;
	HWND hIntervalCombo;
	HWND hLongTermCheck;
	HWND hLongTermEdit;
	HWND hLiveOnlyCheck;
	HWND hCsvEdit;
	HWND hStartLoggingButton;
	HWND hStopLoggingButton;
	HWND hNewMeasurementButton;

	CIndicatorWindow ConnectionIndicator;
	CIndicatorWindow MeasurementIndicator;
	CIndicatorWindow CsvIndicator;

	array<CPressureCardWindow, 6> ChannelCards;
	array<HWND, 6> PlotChecks;
	array<bool, 6> PlotVisible;

	HWND hMessagesLabel;
	HWND hMessagesEdit;
	HWND hRawGroup;
	HWND hRawEdit;
	HWND hSendRawButton;
	HWND hRawHelpButton;
	HWND hToggleControlButton;

	HWND hControlGroup;
	vector<HWND> ControlPanelWindows;
	HWND hControlChannelCombo;
	HWND hUnitCombo;
	HWND hFilterCombo;
	HWND hCalibrationEdit;
	HWND hFsrCombo;
	HWND hOfcCombo;
	HWND hDisplayNameEdit;
	HWND hDigitsCombo;
	HWND hContrastEdit;
	HWND hScreensaveEdit;
	HWND hGaugeOnButton;
	HWND hGaugeOffButton;
	HWND hReadNowButton;
	HWND hActivateVerifyButton;
	HWND hDiagnoseButton;
	HWND hSetUnitButton;
	HWND hDegasOnButton;
	HWND hDegasOffButton;
	HWND hSetFilterButton;
	HWND hSetCalibrationButton;
	HWND hSetFsrButton;
	HWND hSetOfcButton;
	HWND hSetDisplayNameButton;
	HWND hSetDigitsButton;
	HWND hSetContrastButton;
	HWND hSetScreensaveButton;

	HWND hExternalPlotButton;
	HWND hPlotCsvButton;
	CPressurePlotWindow MainPlot;
	CStandalonePlotWindow ExternalPlotWindow;
	CStandalonePlotWindow CsvPlotWindow;
	PressureLoggerStateSnapshot CsvSnapshot;

	bool bControlVisible;
	PressureLoggerStateSnapshot CurrentSnapshot;
	CPressureLoggerAppEngine Engine;
};


CMainWindow::CMainWindow()
{
	hInstance = 0;
	hWnd = 0;
	hMainFont = 0;
	hBoldFont = 0;
	hMonoFont = 0;
	hValueFont = 0;
	hTitleLabel = 0;
	hConnectionGroup = 0;
	hDeviceCombo = 0;
	hPortCombo = 0;
	hConnectButton = 0;
	hDisconnectButton = 0;
	hRefreshPortsButton = 0;
	hFactoryResetButton = 0;
	hMeasurementStatusLabel = 0;
	hSamplesLabel = 0;
	hCsvStatusLabel = 0;
	hIntervalCombo = 0;
	hLongTermCheck = 0;
	hLongTermEdit = 0;
	hLiveOnlyCheck = 0;
	hCsvEdit = 0;
	hStartLoggingButton = 0;
	hStopLoggingButton = 0;
	hNewMeasurementButton = 0;
	hMessagesLabel = 0;
	hMessagesEdit = 0;
	hRawGroup = 0;
	hRawEdit = 0;
	hSendRawButton = 0;
	hRawHelpButton = 0;
	hToggleControlButton = 0;
	hControlGroup = 0;
	hControlChannelCombo = 0;
	hUnitCombo = 0;
	hFilterCombo = 0;
	hCalibrationEdit = 0;
	hFsrCombo = 0;
	hOfcCombo = 0;
	hDisplayNameEdit = 0;
	hDigitsCombo = 0;
	hContrastEdit = 0;
	hScreensaveEdit = 0;
	hGaugeOnButton = 0;
	hGaugeOffButton = 0;
	hReadNowButton = 0;
	hActivateVerifyButton = 0;
	hDiagnoseButton = 0;
	hSetUnitButton = 0;
	hDegasOnButton = 0;
	hDegasOffButton = 0;
	hSetFilterButton = 0;
	hSetCalibrationButton = 0;
	hSetFsrButton = 0;
	hSetOfcButton = 0;
	hSetDisplayNameButton = 0;
	hSetDigitsButton = 0;
	hSetContrastButton = 0;
	hSetScreensaveButton = 0;
	hExternalPlotButton = 0;
	hPlotCsvButton = 0;
	bControlVisible = false;

	for ( size_t i = 0; i < PlotChecks.size(); i++ )
	{
		PlotChecks[i] = 0;
		PlotVisible[i] = (i < 2);
	}
}


CMainWindow::~CMainWindow()
{
	if ( hWnd != 0 )
		KillTimer( hWnd, 1 );

	Engine.Disconnect();

	if ( hMainFont ) DeleteObject( hMainFont );
	if ( hBoldFont ) DeleteObject( hBoldFont );
	if ( hMonoFont ) DeleteObject( hMonoFont );
	if ( hValueFont ) DeleteObject( hValueFont );
}


bool CMainWindow::Create( HINSTANCE i_hInstance )
{
	hInstance = i_hInstance;

	WNDCLASSEXW window_class;
	ZeroMemory( &window_class, sizeof(window_class) );

	window_class.cbSize = sizeof(window_class);
	window_class.lpfnWndProc = CMainWindow::WindowProc;
	window_class.hInstance = i_hInstance;
	window_class.lpszClassName = L"CDTPressureLoggerMainWindow";
	window_class.hCursor = LoadCursorW( 0, IDC_ARROW );
	window_class.hbrBackground = reinterpret_cast<HBRUSH>( COLOR_WINDOW + 1 );

	if ( !RegisterClassExW( &window_class ) )
		return false;

	hWnd = CreateWindowExW( 0,
							window_class.lpszClassName,
							L"CDT pressure logger",
							WS_OVERLAPPEDWINDOW | WS_VISIBLE,
							CW_USEDEFAULT,
							CW_USEDEFAULT,
							1860,
							1280,
							0,
							0,
							i_hInstance,
							this );

	return (hWnd != 0);
}


LRESULT CALLBACK CMainWindow::WindowProc( HWND i_hWnd, UINT i_Message, WPARAM i_wParam, LPARAM i_lParam )
{
	CMainWindow *pWindow = reinterpret_cast<CMainWindow*>( GetWindowLongPtrW( i_hWnd, GWLP_USERDATA ) );

	if ( i_Message == WM_NCCREATE )
	{
		CREATESTRUCTW *create_struct = reinterpret_cast<CREATESTRUCTW*>( i_lParam );
		pWindow = reinterpret_cast<CMainWindow*>( create_struct->lpCreateParams );
		SetWindowLongPtrW( i_hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>( pWindow ) );
		pWindow->hWnd = i_hWnd;
	}

	if ( pWindow != 0 )
		return pWindow->HandleMessage( i_Message, i_wParam, i_lParam );

	return DefWindowProcW( i_hWnd, i_Message, i_wParam, i_lParam );
}


LRESULT CMainWindow::HandleMessage( UINT i_Message, WPARAM i_wParam, LPARAM i_lParam )
{
	switch ( i_Message )
	{
		case WM_CREATE:
			CreateControls();
			ApplyFonts();
			ApplyDefaultValues();
			RefreshPorts();
			UpdateDeviceProfile();
			LayoutChildren();
			UpdateUiFromState();
			SetTimer( hWnd, 1, 250, 0 );
			return 0;

		case WM_SIZE:
			LayoutChildren();
			return 0;

		case WM_TIMER:
			UpdateUiFromState();
			return 0;

		case WM_COMMAND:
		{
			switch ( LOWORD( i_wParam ) )
			{
				case ID_BUTTON_REFRESH_PORTS:	RefreshPorts(); return 0;
				case ID_BUTTON_CONNECT:			OnConnect(); return 0;
				case ID_BUTTON_DISCONNECT:		OnDisconnect(); return 0;
				case ID_BUTTON_NEW_MEASUREMENT:	OnNewMeasurement(); return 0;
				case ID_BUTTON_FACTORY_RESET:	OnFactoryReset(); return 0;
				case ID_BUTTON_START_LOGGING:	OnStartLogging(); return 0;
				case ID_BUTTON_STOP_LOGGING:	OnStopLogging(); return 0;
				case ID_BUTTON_SEND_RAW:		OnSendRaw(); return 0;
				case ID_BUTTON_RAW_HELP:		ShowTextWindow( L"Hilfe: Rohkommandos", Engine.GetHelpText( "raw" ) ); return 0;
				case ID_BUTTON_TOGGLE_CONTROL:	OnToggleControlPanel(); return 0;
				case ID_BUTTON_GAUGE_ON:		OnGauge( true ); return 0;
				case ID_BUTTON_GAUGE_OFF:		OnGauge( false ); return 0;
				case ID_BUTTON_READ_NOW:		OnReadNow(); return 0;
				case ID_BUTTON_ACTIVATE_VERIFY:	OnActivateVerify(); return 0;
				case ID_BUTTON_DIAGNOSE:		OnDiagnose(); return 0;
				case ID_BUTTON_SET_UNIT:		OnSetUnit(); return 0;
				case ID_BUTTON_DEGAS_ON:		OnDegas( true ); return 0;
				case ID_BUTTON_DEGAS_OFF:		OnDegas( false ); return 0;
				case ID_BUTTON_SET_FILTER:		OnSetFilter(); return 0;
				case ID_BUTTON_SET_CALIBRATION:	OnSetCalibration(); return 0;
				case ID_BUTTON_SET_FSR:			OnSetFsr(); return 0;
				case ID_BUTTON_SET_OFC:			OnSetOfc(); return 0;
				case ID_BUTTON_SET_DISPLAY_NAME:OnSetDisplayName(); return 0;
				case ID_BUTTON_SET_DIGITS:		OnSetDigits(); return 0;
				case ID_BUTTON_SET_CONTRAST:	OnSetContrast(); return 0;
				case ID_BUTTON_SET_SCREENSAVE:	OnSetScreensave(); return 0;
				case ID_BUTTON_BROWSE_CSV:
				case ID_BUTTON_BROWSE_CSV_ALT:	OnBrowseCsv( true ); return 0;
				case ID_BUTTON_EXTERNAL_PLOT:	OpenExternalPlot(); return 0;
				case ID_BUTTON_PLOT_CSV:		OnPlotCsv(); return 0;
			}

			if ( (LOWORD( i_wParam ) >= ID_CHECK_PLOT_1) && (LOWORD( i_wParam ) <= ID_CHECK_PLOT_6) )
			{
				SetPlotCheckboxDefaults();
				UpdateUiFromState();
				return 0;
			}

			if ( HIWORD( i_wParam ) == CBN_SELCHANGE )
			{
				if ( LOWORD( i_wParam ) == ID_COMBO_DEVICE )
				{
					UpdateDeviceProfile();
					return 0;
				}
				if ( LOWORD( i_wParam ) == ID_COMBO_PORT )
				{
					Engine.SetLastSelection( SelectedDeviceType(), ReadControlTextUtf8( hPortCombo ) );
					return 0;
				}
				if ( LOWORD( i_wParam ) == ID_COMBO_CONTROL_CHANNEL )
				{
					SyncDisplayNameField();
					return 0;
				}
			}

			if ( HIWORD( i_wParam ) == CBN_EDITCHANGE && LOWORD( i_wParam ) == ID_COMBO_PORT )
			{
				Engine.SetLastSelection( SelectedDeviceType(), ReadControlTextUtf8( hPortCombo ) );
				return 0;
			}

			return 0;
		}

		case WM_DESTROY:
			PostQuitMessage( 0 );
			return 0;
	}

	return DefWindowProcW( hWnd, i_Message, i_wParam, i_lParam );
}


HWND CMainWindow::CreateGroup( int x, int y, int width, int height, const wchar_t *text )
{
	return CreateWindowExW( 0,
							L"BUTTON",
							text,
							WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
							x,
							y,
							width,
							height,
							hWnd,
							0,
							hInstance,
							0 );
}


HWND CMainWindow::CreateLabel( int x, int y, int width, int height, const wchar_t *text, const bool i_Bold )
{
	HWND hLabel = CreateWindowExW( 0,
								   L"STATIC",
								   text,
								   WS_CHILD | WS_VISIBLE,
								   x,
								   y,
								   width,
								   height,
								   hWnd,
								   0,
								   hInstance,
								   0 );
	if ( i_Bold )
		SetWindowLongPtrW( hLabel, GWLP_USERDATA, 1 );
	return hLabel;
}


HWND CMainWindow::CreateEdit( int x, int y, int width, int height, int controlId, const wchar_t *text, DWORD extraStyle )
{
	return CreateWindowExW( WS_EX_CLIENTEDGE,
							L"EDIT",
							text,
							WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_LEFT | extraStyle,
							x,
							y,
							width,
							height,
							hWnd,
							_ControlMenu( controlId ),
							hInstance,
							0 );
}


HWND CMainWindow::CreateButton( int x, int y, int width, int height, int controlId, const wchar_t *text, DWORD style )
{
	return CreateWindowExW( 0,
							L"BUTTON",
							text,
							WS_CHILD | WS_VISIBLE | WS_TABSTOP | style,
							x,
							y,
							width,
							height,
							hWnd,
							_ControlMenu( controlId ),
							hInstance,
							0 );
}


HWND CMainWindow::CreateCheckbox( int x, int y, int width, int height, int controlId, const wchar_t *text )
{
	return CreateButton( x, y, width, height, controlId, text, BS_AUTOCHECKBOX );
}


HWND CMainWindow::CreateCombo( int x, int y, int width, int height, int controlId, const bool i_Editable )
{
	return CreateWindowExW( 0,
							L"COMBOBOX",
							L"",
							WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | (i_Editable ? CBS_DROPDOWN : CBS_DROPDOWNLIST),
							x,
							y,
							width,
							height,
							hWnd,
							_ControlMenu( controlId ),
							hInstance,
							0 );
}


void CMainWindow::CreateControls()
{
	hTitleLabel = CreateLabel( 16, 12, 420, 32, L"CDT pressure logger", true );
	hConnectionGroup = CreateGroup( 10, 48, 830, 208, L"Verbindung / Messung / Status" );

	CreateLabel( 28, 82, 60, 22, L"Geraet:" );
	ConnectionIndicator.Create( hInstance, hWnd, RECT{230, 82, 248, 100} );
	hDeviceCombo = CreateCombo( 292, 78, 200, 240, ID_COMBO_DEVICE );
	CreateLabel( 516, 82, 38, 22, L"Port" );
	hPortCombo = CreateCombo( 560, 78, 222, 240, ID_COMBO_PORT, true );

	hConnectButton = CreateButton( 28, 122, 170, 30, ID_BUTTON_CONNECT, L"Verbinden" );
	hDisconnectButton = CreateButton( 206, 122, 110, 30, ID_BUTTON_DISCONNECT, L"Trennen" );
	hNewMeasurementButton = CreateButton( 324, 122, 140, 30, ID_BUTTON_NEW_MEASUREMENT, L"Neu + Start" );
	hRefreshPortsButton = CreateButton( 472, 122, 140, 30, ID_BUTTON_REFRESH_PORTS, L"Aktualisieren" );
	hFactoryResetButton = CreateButton( 620, 122, 162, 30, ID_BUTTON_FACTORY_RESET, L"Werkreset" );

	CreateLabel( 28, 166, 70, 22, L"Messung:" );
	MeasurementIndicator.Create( hInstance, hWnd, RECT{230, 166, 248, 184} );
	hMeasurementStatusLabel = CreateLabel( 292, 166, 220, 22, L"Nicht verbunden" );
	hSamplesLabel = CreateLabel( 516, 166, 180, 22, L"Sam 0" );

	hStartLoggingButton = CreateButton( 28, 200, 170, 30, ID_BUTTON_START_LOGGING, L"Logging starten" );
	hStopLoggingButton = CreateButton( 206, 200, 170, 30, ID_BUTTON_STOP_LOGGING, L"Logging stoppen" );
	hLiveOnlyCheck = CreateCheckbox( 396, 202, 300, 24, ID_CHECK_LIVE_ONLY, L"nur live anzeigen, nicht speichern" );

	CreateLabel( 28, 240, 128, 22, L"Continuous Mode" );
	hIntervalCombo = CreateCombo( 158, 236, 112, 200, ID_COMBO_INTERVAL );
	hLongTermCheck = CreateCheckbox( 284, 238, 130, 24, ID_CHECK_LONG_TERM, L"Langzeitmodus" );
	hLongTermEdit = CreateEdit( 520, 236, 56, 26, ID_EDIT_LONG_TERM, L"60" );
	CreateLabel( 584, 240, 160, 22, L"s (Standard 60)" );

	CreateLabel( 28, 274, 36, 22, L"CSV" );
	hCsvEdit = CreateEdit( 158, 270, 624, 26, ID_EDIT_CSV, L"" );
	CreateButton( 790, 270, 30, 26, ID_BUTTON_BROWSE_CSV, L"..." );

	CreateButton( 28, 306, 170, 30, ID_BUTTON_BROWSE_CSV_ALT, L"Durchsuchen" );
	CsvIndicator.Create( hInstance, hWnd, RECT{230, 310, 248, 328} );
	hCsvStatusLabel = CreateLabel( 292, 310, 500, 22, L"Datei: Keine Datei offen" );

	const int card_top = 366;
	for ( int i = 0; i < 6; i++ )
	{
		const int row = i / 2;
		const int col = i % 2;
		RECT card_rect;
		card_rect.left = 12 + col * 408;
		card_rect.top = card_top + row * 156;
		card_rect.right = card_rect.left + 396;
		card_rect.bottom = card_rect.top + 144;
		ChannelCards[i].Create( hInstance, hWnd, card_rect );
	}

	CreateLabel( 16, 842, 124, 22, L"Im Plot anzeigen:" );
	for ( int i = 0; i < 6; i++ )
	{
		const wstring label = to_wstring( i + 1 );
		PlotChecks[i] = CreateCheckbox( 150 + i * 44, 840, 40, 24, ID_CHECK_PLOT_1 + i, label.c_str() );
	}

	hMessagesLabel = CreateLabel( 16, 874, 100, 22, L"Meldungen" );
	hMessagesEdit = CreateEdit( 10, 900, 820, 200, ID_EDIT_MESSAGES, L"", ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL );

	hRawGroup = CreateGroup( 10, 1112, 820, 78, L"Rohkommando" );
	hRawEdit = CreateEdit( 28, 1140, 520, 26, ID_EDIT_RAW, L"" );
	hSendRawButton = CreateButton( 560, 1138, 160, 30, ID_BUTTON_SEND_RAW, L"Senden" );
	hRawHelpButton = CreateButton( 730, 1138, 54, 30, ID_BUTTON_RAW_HELP, L"i" );

	hToggleControlButton = CreateButton( 10, 1200, 820, 30, ID_BUTTON_TOGGLE_CONTROL, L"Steuerung / Parameter einblenden" );
	hControlGroup = CreateGroup( 10, 1238, 820, 332, L"Steuerung / Parameter" );

	HWND hControlLabelChannel = CreateLabel( 28, 1270, 46, 22, L"Kanal" );
	hControlChannelCombo = CreateCombo( 80, 1266, 90, 180, ID_COMBO_CONTROL_CHANNEL );
	hGaugeOnButton = CreateButton( 182, 1264, 118, 30, ID_BUTTON_GAUGE_ON, L"Gauge EIN" );
	hGaugeOffButton = CreateButton( 310, 1264, 118, 30, ID_BUTTON_GAUGE_OFF, L"Gauge AUS" );
	hReadNowButton = CreateButton( 438, 1264, 164, 30, ID_BUTTON_READ_NOW, L"Messwert jetzt lesen" );
	hActivateVerifyButton = CreateButton( 612, 1264, 178, 30, ID_BUTTON_ACTIVATE_VERIFY, L"Aktivieren + pruefen" );

	HWND hControlLabelUnit = CreateLabel( 28, 1310, 52, 22, L"Einheit" );
	hUnitCombo = CreateCombo( 84, 1306, 110, 160, ID_COMBO_UNIT );
	hSetUnitButton = CreateButton( 204, 1304, 110, 30, ID_BUTTON_SET_UNIT, L"Einheit setzen" );
	hDegasOnButton = CreateButton( 324, 1304, 118, 30, ID_BUTTON_DEGAS_ON, L"Degas EIN" );
	hDegasOffButton = CreateButton( 452, 1304, 118, 30, ID_BUTTON_DEGAS_OFF, L"Degas AUS" );
	hDiagnoseButton = CreateButton( 612, 1304, 178, 30, ID_BUTTON_DIAGNOSE, L"Diagnose" );

	HWND hControlLabelFilter = CreateLabel( 28, 1350, 42, 22, L"Filter" );
	hFilterCombo = CreateCombo( 84, 1346, 110, 160, ID_COMBO_FILTER );
	hSetFilterButton = CreateButton( 204, 1344, 110, 30, ID_BUTTON_SET_FILTER, L"Filter setzen" );
	HWND hControlLabelCalibration = CreateLabel( 328, 1350, 88, 22, L"Kalibrierfaktor" );
	hCalibrationEdit = CreateEdit( 420, 1346, 90, 26, ID_EDIT_CALIBRATION, L"1.000" );
	hSetCalibrationButton = CreateButton( 520, 1344, 110, 30, ID_BUTTON_SET_CALIBRATION, L"CAL setzen" );

	HWND hControlLabelFsr = CreateLabel( 28, 1390, 34, 22, L"FSR" );
	hFsrCombo = CreateCombo( 84, 1386, 150, 220, ID_COMBO_FSR );
	hSetFsrButton = CreateButton( 244, 1384, 110, 30, ID_BUTTON_SET_FSR, L"FSR setzen" );
	HWND hControlLabelOfc = CreateLabel( 372, 1390, 34, 22, L"OFC" );
	hOfcCombo = CreateCombo( 414, 1386, 110, 180, ID_COMBO_OFC );
	hSetOfcButton = CreateButton( 534, 1384, 110, 30, ID_BUTTON_SET_OFC, L"OFC setzen" );

	HWND hControlLabelDisplayName = CreateLabel( 28, 1430, 90, 22, L"Anzeigename" );
	hDisplayNameEdit = CreateEdit( 124, 1426, 140, 26, ID_EDIT_DISPLAY_NAME, L"Kanal 1" );
	hSetDisplayNameButton = CreateButton( 274, 1424, 128, 30, ID_BUTTON_SET_DISPLAY_NAME, L"Namen speichern" );
	HWND hControlLabelDigits = CreateLabel( 420, 1430, 40, 22, L"Digits" );
	hDigitsCombo = CreateCombo( 466, 1426, 90, 160, ID_COMBO_DIGITS );
	hSetDigitsButton = CreateButton( 566, 1424, 90, 30, ID_BUTTON_SET_DIGITS, L"Setzen" );

	HWND hControlLabelContrast = CreateLabel( 28, 1470, 54, 22, L"Contrast" );
	hContrastEdit = CreateEdit( 90, 1466, 70, 26, ID_EDIT_CONTRAST, L"10" );
	hSetContrastButton = CreateButton( 170, 1464, 110, 30, ID_BUTTON_SET_CONTRAST, L"Contrast" );
	HWND hControlLabelScreensave = CreateLabel( 300, 1470, 92, 22, L"Screensave [h]" );
	hScreensaveEdit = CreateEdit( 398, 1466, 70, 26, ID_EDIT_SCREENSAVE, L"0" );
	hSetScreensaveButton = CreateButton( 478, 1464, 122, 30, ID_BUTTON_SET_SCREENSAVE, L"Screensave" );

	ControlPanelWindows = {
		hControlGroup,
		hControlLabelChannel,
		hControlLabelUnit,
		hControlLabelFilter,
		hControlLabelCalibration,
		hControlLabelFsr,
		hControlLabelOfc,
		hControlLabelDisplayName,
		hControlLabelDigits,
		hControlLabelContrast,
		hControlLabelScreensave,
		hControlChannelCombo,
		hUnitCombo,
		hFilterCombo,
		hCalibrationEdit,
		hFsrCombo,
		hOfcCombo,
		hDisplayNameEdit,
		hDigitsCombo,
		hContrastEdit,
		hScreensaveEdit,
		hGaugeOnButton,
		hGaugeOffButton,
		hReadNowButton,
		hActivateVerifyButton,
		hDiagnoseButton,
		hSetUnitButton,
		hDegasOnButton,
		hDegasOffButton,
		hSetFilterButton,
		hSetCalibrationButton,
		hSetFsrButton,
		hSetOfcButton,
		hSetDisplayNameButton,
		hSetDigitsButton,
		hSetContrastButton,
		hSetScreensaveButton
	};

	RECT plot_rect = {860, 50, 1840, 1190};
	MainPlot.Create( hInstance, hWnd, plot_rect );
	hExternalPlotButton = CreateButton( 860, 1200, 140, 30, ID_BUTTON_EXTERNAL_PLOT, L"Externer Plot" );
	hPlotCsvButton = CreateButton( 1010, 1200, 120, 30, ID_BUTTON_PLOT_CSV, L"CSV plotten" );
}


void CMainWindow::ApplyFonts()
{
	hMainFont = CreateFontW( -18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
							 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
							 DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI" );
	hBoldFont = CreateFontW( -18, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
							 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
							 DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI" );
	hMonoFont = CreateFontW( -17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
							 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
							 FIXED_PITCH | FF_MODERN, L"Consolas" );
	hValueFont = CreateFontW( -34, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
							  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
							  DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI" );

	for ( HWND child = GetWindow( hWnd, GW_CHILD ); child != 0; child = GetWindow( child, GW_HWNDNEXT ) )
	{
		const LONG_PTR bold_flag = GetWindowLongPtrW( child, GWLP_USERDATA );
		SendMessageW( child, WM_SETFONT, reinterpret_cast<WPARAM>( bold_flag ? hBoldFont : hMainFont ), TRUE );
	}

	SendMessageW( hMessagesEdit, WM_SETFONT, reinterpret_cast<WPARAM>( hMonoFont ), TRUE );
	SendMessageW( hRawEdit, WM_SETFONT, reinterpret_cast<WPARAM>( hMonoFont ), TRUE );

	for ( size_t i = 0; i < ChannelCards.size(); i++ )
		ChannelCards[i].SetFonts( hMainFont, hBoldFont, hValueFont );

	MainPlot.SetFonts( hMainFont, hMonoFont );
	ExternalPlotWindow.SetFonts( hMainFont, hMonoFont );
	CsvPlotWindow.SetFonts( hMainFont, hMonoFont );
}


void CMainWindow::ApplyDefaultValues()
{
	FillCombo( hDeviceCombo, {L"TPG 262", L"MaxiGauge"}, (Engine.GetLastDeviceType() == PressureLoggerDevice_MaxiGauge) ? 1 : 0 );
	FillCombo( hUnitCombo, {L"mbar", L"Torr", L"Pa"}, 0 );
	FillCombo( hFilterCombo, {L"fast", L"standard", L"slow"}, 1 );
	FillCombo( hOfcCombo, {L"off", L"on", L"auto"}, 0 );
	FillCombo( hDigitsCombo, {L"2", L"3"}, 1 );
	_SetChecked( hLiveOnlyCheck, false );
	_SetChecked( hLongTermCheck, false );
	SetWindowTextUtf8( hCsvEdit, Engine.MakeDefaultCsvPath( SelectedDeviceType() ) );
	SetPlotCheckboxDefaults();
}


void CMainWindow::LayoutChildren()
{
	RECT client_rect;
	GetClientRect( hWnd, &client_rect );

	const int right_left = 860;
	const int right_margin = 20;
	const int bottom_buttons_y = client_rect.bottom - 44;
	RECT plot_rect = {right_left, 50, max( right_left + 400, client_rect.right - right_margin ), max( 200, bottom_buttons_y - 14 )};
	MainPlot.SetGeometry( plot_rect );
	MoveWindow( hExternalPlotButton, right_left, bottom_buttons_y, 140, 30, TRUE );
	MoveWindow( hPlotCsvButton, right_left + 150, bottom_buttons_y, 120, 30, TRUE );

	for ( size_t i = 0; i < ControlPanelWindows.size(); i++ )
		ShowWindow( ControlPanelWindows[i], bControlVisible ? SW_SHOW : SW_HIDE );
}


void CMainWindow::FillCombo( HWND hCombo, const vector<wstring>& i_Items, const int i_SelectedIndex )
{
	SendMessageW( hCombo, CB_RESETCONTENT, 0, 0 );
	for ( size_t i = 0; i < i_Items.size(); i++ )
		SendMessageW( hCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>( i_Items[i].c_str() ) );
	if ( !i_Items.empty() )
		SendMessageW( hCombo, CB_SETCURSEL, max( 0, min( static_cast<int>( i_Items.size() ) - 1, i_SelectedIndex ) ), 0 );
}


void CMainWindow::SetWindowTextUtf8( HWND hControl, const string& i_Text ) const
{
	SetWindowTextW( hControl, _ToWide( i_Text ).c_str() );
}


string CMainWindow::ReadControlTextUtf8( HWND hControl ) const
{
	return _ToUtf8( _TrimmedWindowText( hControl ) );
}


int CMainWindow::ReadInt( HWND hControl, const wchar_t *i_Name, bool *pOk ) const
{
	try
	{
		const int value = stoi( _ToUtf8( _TrimmedWindowText( hControl ) ) );
		if ( pOk != 0 ) *pOk = true;
		return value;
	}
	catch ( ... )
	{
		MessageBoxW( hWnd, (wstring( L"Ungueltiger Wert fuer " ) + i_Name + L".").c_str(), L"Eingabefehler", MB_ICONERROR | MB_OK );
		if ( pOk != 0 ) *pOk = false;
		return 0;
	}
}


double CMainWindow::ReadDouble( HWND hControl, const wchar_t *i_Name, bool *pOk ) const
{
	try
	{
		const double value = stod( _NormalizeDecimalInput( _ToUtf8( _TrimmedWindowText( hControl ) ) ) );
		if ( pOk != 0 ) *pOk = true;
		return value;
	}
	catch ( ... )
	{
		MessageBoxW( hWnd, (wstring( L"Ungueltiger Wert fuer " ) + i_Name + L".").c_str(), L"Eingabefehler", MB_ICONERROR | MB_OK );
		if ( pOk != 0 ) *pOk = false;
		return 0.0;
	}
}


BYTE CMainWindow::SelectedChannel( bool *pOk ) const
{
	const LRESULT index = SendMessageW( hControlChannelCombo, CB_GETCURSEL, 0, 0 );
	if ( index < 0 )
	{
		if ( pOk != 0 ) *pOk = false;
		MessageBoxW( hWnd, L"Kein Kanal ausgewaehlt.", L"Eingabefehler", MB_ICONERROR | MB_OK );
		return 0;
	}

	if ( pOk != 0 ) *pOk = true;
	return static_cast<BYTE>( index + 1 );
}


PressureLoggerDeviceType CMainWindow::SelectedDeviceType() const
{
	return (SendMessageW( hDeviceCombo, CB_GETCURSEL, 0, 0 ) == 1) ? PressureLoggerDevice_MaxiGauge : PressureLoggerDevice_TPG262;
}


PressureLoggerDeviceType CMainWindow::ActiveDeviceTypeForUi() const
{
	return CurrentSnapshot.bConnected ? CurrentSnapshot.Setup.DeviceType : SelectedDeviceType();
}


int CMainWindow::ActiveChannelCount() const
{
	return (ActiveDeviceTypeForUi() == PressureLoggerDevice_MaxiGauge) ? 6 : 2;
}


int CMainWindow::SelectedUnitCode() const
{
	return static_cast<int>( SendMessageW( hUnitCombo, CB_GETCURSEL, 0, 0 ) );
}


int CMainWindow::SelectedFilterCode() const
{
	const LRESULT index = SendMessageW( hFilterCombo, CB_GETCURSEL, 0, 0 );
	if ( index <= 0 )
		return 0;
	if ( index == 1 )
		return 1;
	return 2;
}


int CMainWindow::SelectedFsrCode() const
{
	return static_cast<int>( SendMessageW( hFsrCombo, CB_GETCURSEL, 0, 0 ) );
}


int CMainWindow::SelectedOfcCode() const
{
	return static_cast<int>( SendMessageW( hOfcCombo, CB_GETCURSEL, 0, 0 ) );
}


int CMainWindow::SelectedDigits() const
{
	return (SendMessageW( hDigitsCombo, CB_GETCURSEL, 0, 0 ) == 0) ? 2 : 3;
}


PressureLoggerConnectionSetup CMainWindow::BuildSetup( bool *pOk ) const
{
	if ( pOk != 0 )
		*pOk = false;

	PressureLoggerConnectionSetup setup;
	setup.DeviceType = SelectedDeviceType();
	setup.sPort = ReadControlTextUtf8( hPortCombo );
	setup.dwBaudRate = 9600;
	setup.dwTimeoutMs = 200;
	setup.bTPG262LongTermMode = _IsChecked( hLongTermCheck );
	setup.dwTPG262ContinuousMode = 1;
	setup.dPollingSeconds = 1.0;

	if ( setup.sPort.empty() )
	{
		MessageBoxW( hWnd, L"Bitte einen seriellen Port waehlen.", L"Eingabefehler", MB_ICONERROR | MB_OK );
		return setup;
	}

	if ( setup.DeviceType == PressureLoggerDevice_TPG262 )
	{
		setup.dwTPG262ContinuousMode = static_cast<DWORD>( max<LRESULT>( 0, SendMessageW( hIntervalCombo, CB_GETCURSEL, 0, 0 ) ) );
		if ( setup.bTPG262LongTermMode )
		{
			double seconds = 60.0;
			bool read_ok = false;
			seconds = ReadDouble( hLongTermEdit, L"Langzeitmodus", &read_ok );
			if ( !read_ok )
				return setup;
			setup.dPollingSeconds = max( 1.0, seconds );
		}
		else
		{
			setup.dPollingSeconds = 60.0;
		}
	}
	else
	{
		const double maxi_intervals[] = {0.2, 0.5, 1.0, 2.0, 5.0};
		const int interval_index = max<int>( 0, min<int>( 4, static_cast<int>( SendMessageW( hIntervalCombo, CB_GETCURSEL, 0, 0 ) ) ) );
		setup.dPollingSeconds = maxi_intervals[interval_index];
		if ( _IsChecked( hLongTermCheck ) )
		{
			double seconds = 60.0;
			bool read_ok = false;
			seconds = ReadDouble( hLongTermEdit, L"Langzeitmodus", &read_ok );
			if ( !read_ok )
				return setup;
			setup.dPollingSeconds = max( 1.0, seconds );
		}
	}

	if ( pOk != 0 )
		*pOk = true;
	return setup;
}


void CMainWindow::SetPlotCheckboxDefaults()
{
	for ( int i = 0; i < 6; i++ )
		PlotVisible[i] = _IsChecked( PlotChecks[i] );
}


void CMainWindow::UpdatePlotCheckboxAvailability()
{
	for ( int i = 0; i < 6; i++ )
	{
		const bool enabled = (i < ActiveChannelCount());
		EnableWindow( PlotChecks[i], enabled );
		if ( !enabled )
		{
			_SetChecked( PlotChecks[i], false );
			PlotVisible[i] = false;
		}
		else if ( !CurrentSnapshot.bConnected && (i < 2) && !_IsChecked( PlotChecks[i] ) )
		{
			_SetChecked( PlotChecks[i], true );
			PlotVisible[i] = true;
		}
	}
}


void CMainWindow::UpdateControlButtonsAvailability( const bool i_Connected )
{
	EnableWindow( hConnectButton, !i_Connected );
	EnableWindow( hDisconnectButton, i_Connected );
	EnableWindow( hDeviceCombo, !i_Connected );
	EnableWindow( hPortCombo, !i_Connected );
	EnableWindow( hRefreshPortsButton, !i_Connected );
	EnableWindow( hIntervalCombo, !i_Connected );
	EnableWindow( hLongTermCheck, !i_Connected );
	EnableWindow( hLongTermEdit, (!i_Connected) && _IsChecked( hLongTermCheck ) );
	EnableWindow( hFactoryResetButton, i_Connected );
	EnableWindow( hStartLoggingButton, i_Connected );
	EnableWindow( hStopLoggingButton, i_Connected );
	EnableWindow( hNewMeasurementButton, i_Connected );
	EnableWindow( hSendRawButton, i_Connected );

	for ( size_t i = 0; i < ControlPanelWindows.size(); i++ )
	{
		if ( ControlPanelWindows[i] != hControlGroup )
			EnableWindow( ControlPanelWindows[i], i_Connected );
	}

	EnableWindow( hControlChannelCombo, true );
	EnableWindow( hDisplayNameEdit, true );
	EnableWindow( hSetDisplayNameButton, true );

	const bool maxi = (ActiveDeviceTypeForUi() == PressureLoggerDevice_MaxiGauge);
	EnableWindow( hDigitsCombo, i_Connected && maxi );
	EnableWindow( hSetDigitsButton, i_Connected && maxi );
	EnableWindow( hContrastEdit, i_Connected && maxi );
	EnableWindow( hSetContrastButton, i_Connected && maxi );
	EnableWindow( hScreensaveEdit, i_Connected && maxi );
	EnableWindow( hSetScreensaveButton, i_Connected && maxi );
}


void CMainWindow::RefreshPorts()
{
	vector<string> ports;
	const DWORD error = Engine.CollectSuggestedPorts( &ports );
	if ( error != EC_OK )
	{
		ShowError( error );
		return;
	}

	vector<wstring> items;
	for ( size_t i = 0; i < ports.size(); i++ )
		items.push_back( _ToWide( ports[i] ) );
	if ( items.empty() )
		items.push_back( L"" );

	FillCombo( hPortCombo, items, 0 );

	const string last_port = Engine.GetLastPort();
	if ( !last_port.empty() )
	{
		_SelectComboByText( hPortCombo, _ToWide( last_port ) );
		SetWindowTextW( hPortCombo, _ToWide( last_port ).c_str() );
	}
}


void CMainWindow::UpdateDeviceProfile()
{
	const bool maxi = (SelectedDeviceType() == PressureLoggerDevice_MaxiGauge);

	if ( maxi )
		FillCombo( hIntervalCombo, {L"0.2 s", L"0.5 s", L"1 s", L"2 s", L"5 s"}, 2 );
	else
		FillCombo( hIntervalCombo, {L"100 ms", L"1 s", L"1 min"}, 1 );

	if ( maxi )
		FillCombo( hFsrCombo, {L"1 mbar", L"10 mbar", L"100 mbar", L"1000 mbar", L"2 bar", L"5 bar", L"10 bar", L"50 bar", L"0.1 mbar"}, 3 );
	else
		FillCombo( hFsrCombo, {L"0.01 mbar", L"0.1 mbar", L"1 mbar", L"10 mbar", L"100 mbar", L"1000 mbar", L"2 bar", L"5 bar", L"10 bar", L"50 bar"}, 5 );

	vector<wstring> channels;
	for ( int i = 1; i <= (maxi ? 6 : 2); i++ )
		channels.push_back( to_wstring( i ) );
	FillCombo( hControlChannelCombo, channels, 0 );

	for ( int i = 0; i < 6; i++ )
		ChannelCards[i].SetVisible( i < (maxi ? 6 : 2) );

	UpdatePlotCheckboxAvailability();
	SetWindowTextUtf8( hCsvEdit, Engine.MakeDefaultCsvPath( SelectedDeviceType() ) );
	Engine.SetLastSelection( SelectedDeviceType(), ReadControlTextUtf8( hPortCombo ) );
	SyncDisplayNameField();
	UpdateUiFromState();
}


void CMainWindow::SyncDisplayNameField()
{
	bool ok = false;
	const BYTE channel = SelectedChannel( &ok );
	if ( !ok )
		return;

	SetWindowTextUtf8( hDisplayNameEdit, Engine.GetDisplayChannelName( ActiveDeviceTypeForUi(), channel ) );
}


void CMainWindow::ShowError( const DWORD i_ErrorCode )
{
	MessageBoxW( hWnd, _ToWide( Engine.GetLastErrorText( i_ErrorCode ) ).c_str(), L"Pressure Logger Error", MB_ICONERROR | MB_OK );
}


void CMainWindow::ShowTextWindow( const wchar_t *i_Title, const string& i_Text )
{
	(new CTextDisplayWindow( i_Title, _ToWide( i_Text ) ))->Show( hInstance, hWnd );
}


bool CMainWindow::LoadCsvSnapshot( const wstring& i_Path, PressureLoggerStateSnapshot *pSnapshot )
{
	if ( pSnapshot == 0 )
		return false;

	ifstream input( _ToUtf8( i_Path ).c_str() );
	if ( !input.is_open() )
		return false;

	string header_line;
	if ( !getline( input, header_line ) )
		return false;

	vector<string> headers;
	string token;
	stringstream header_stream( header_line );
	while ( getline( header_stream, token, ',' ) )
		headers.push_back( token );

	if ( headers.size() < 3 )
		return false;

	PressureLoggerStateSnapshot snapshot;
	const size_t channel_count = (headers.size() - 1) / 2;
	snapshot.Setup.DeviceType = (channel_count > 2) ? PressureLoggerDevice_MaxiGauge : PressureLoggerDevice_TPG262;
	snapshot.DisplayChannelNames = Engine.GetDisplayChannelNames( snapshot.Setup.DeviceType );
	for ( size_t i = 0; i < snapshot.DisplayChannelNames.size(); i++ )
		snapshot.CombinedChannelLabels.push_back( Engine.FormatCombinedChannelLabel( snapshot.Setup.DeviceType, static_cast<BYTE>( i + 1 ) ) );

	string line;
	while ( getline( input, line ) )
	{
		stringstream line_stream( line );
		vector<string> values;
		while ( getline( line_stream, token, ',' ) )
			values.push_back( token );

		if ( values.size() != headers.size() )
			continue;

		try
		{
			PressureSample sample;
			sample.dSecondsSinceStart = stod( _NormalizeDecimalInput( values[0] ) );
			for ( size_t channel = 1; channel <= channel_count; channel++ )
			{
				PressureChannelReading reading;
				reading.byChannel = static_cast<BYTE>( channel );
				reading.nStatusCode = stoi( values[1 + (channel - 1) * 2] );
				reading.dPressure = stod( _NormalizeDecimalInput( values[2 + (channel - 1) * 2] ) );
				reading.sStatusText = CPfeifferGaugeDriver::StatusText( reading.nStatusCode );
				sample.ChannelValues.push_back( reading );
			}
			snapshot.History.push_back( sample );
		}
		catch ( ... )
		{
			continue;
		}
	}

	*pSnapshot = snapshot;
	return !snapshot.History.empty();
}


void CMainWindow::UpdateUiFromState()
{
	Engine.GetStateSnapshot( &CurrentSnapshot );
	SetPlotCheckboxDefaults();
	UpdatePlotCheckboxAvailability();
	UpdateControlButtonsAvailability( CurrentSnapshot.bConnected );

	ConnectionIndicator.SetColor( CurrentSnapshot.bConnected ? RGB(  46, 125,  50 ) : _IndicatorGray() );
	MeasurementIndicator.SetColor( CurrentSnapshot.bMonitoring ? RGB(  46, 125,  50 ) : _IndicatorGray() );
	CsvIndicator.SetColor( CurrentSnapshot.bLogging ? RGB(  46, 125,  50 ) : _IndicatorGray() );

	if ( !CurrentSnapshot.bConnected )
		SetWindowTextW( hMeasurementStatusLabel, L"Nicht verbunden" );
	else if ( CurrentSnapshot.bLogging )
		SetWindowTextW( hMeasurementStatusLabel, L"Logging laeuft" );
	else
		SetWindowTextW( hMeasurementStatusLabel, L"Monitoring laeuft" );

	SetWindowTextW( hSamplesLabel, (wstring( L"Sam " ) + to_wstring( CurrentSnapshot.dwSampleCount )).c_str() );
	SetWindowTextW( hCsvStatusLabel, CurrentSnapshot.bLogging ? (wstring( L"Datei: " ) + _ToWide( CurrentSnapshot.sCsvPath )).c_str() : L"Datei: Keine Datei offen" );

	for ( int i = 0; i < ActiveChannelCount(); i++ )
	{
		string label = (i < static_cast<int>( CurrentSnapshot.CombinedChannelLabels.size() ))
			? CurrentSnapshot.CombinedChannelLabels[i]
			: Engine.FormatCombinedChannelLabel( ActiveDeviceTypeForUi(), static_cast<BYTE>( i + 1 ) );

		bool found = false;
		for ( size_t j = 0; j < CurrentSnapshot.LastChannels.size(); j++ )
		{
			if ( CurrentSnapshot.LastChannels[j].byChannel == static_cast<BYTE>( i + 1 ) )
			{
				ChannelCards[i].Update( label,
										 CurrentSnapshot.LastChannels[j].dPressure,
										 CurrentSnapshot.LastChannels[j].nStatusCode,
										 CurrentSnapshot.LastChannels[j].sStatusText );
				found = true;
				break;
			}
		}

		if ( !found )
			ChannelCards[i].Update( label, numeric_limits<double>::quiet_NaN(), 6, "--" );
	}

	for ( int i = ActiveChannelCount(); i < 6; i++ )
		ChannelCards[i].SetVisible( false );

	stringstream log_stream;
	for ( size_t i = 0; i < CurrentSnapshot.LogLines.size(); i++ )
		log_stream << CurrentSnapshot.LogLines[i] << "\r\n";
	SetWindowTextW( hMessagesEdit, _ToWide( log_stream.str() ).c_str() );

	MainPlot.UpdateData( CurrentSnapshot, &Engine, PlotVisible.data(), ActiveChannelCount() );
	if ( ExternalPlotWindow.Window() != 0 )
		ExternalPlotWindow.UpdateData( CurrentSnapshot, &Engine, PlotVisible.data(), ActiveChannelCount() );
}


void CMainWindow::OpenExternalPlot()
{
	if ( ExternalPlotWindow.CreateIfNeeded( hInstance, L"Externer Plot" ) )
		ExternalPlotWindow.UpdateData( CurrentSnapshot, &Engine, PlotVisible.data(), ActiveChannelCount() );
}


void CMainWindow::OpenCsvPlot()
{
	if ( CsvPlotWindow.CreateIfNeeded( hInstance, L"CSV-Plot" ) )
		CsvPlotWindow.UpdateData( CsvSnapshot, &Engine, PlotVisible.data(), static_cast<int>( CsvSnapshot.CombinedChannelLabels.size() ) );
}


void CMainWindow::OnConnect()
{
	bool ok = false;
	const PressureLoggerConnectionSetup setup = BuildSetup( &ok );
	if ( !ok )
		return;

	const DWORD error = Engine.Connect( setup );
	if ( error != EC_OK )
	{
		ShowError( error );
		return;
	}

	UpdateUiFromState();
}


void CMainWindow::OnDisconnect()
{
	const DWORD error = Engine.Disconnect();
	if ( error != EC_OK )
		ShowError( error );
	UpdateUiFromState();
}


void CMainWindow::OnNewMeasurement()
{
	SetWindowTextUtf8( hCsvEdit, Engine.MakeDefaultCsvPath( ActiveDeviceTypeForUi() ) );
	const DWORD reset_error = Engine.ResetMeasurementTimeline();
	if ( reset_error != EC_OK )
	{
		ShowError( reset_error );
		return;
	}

	if ( !_IsChecked( hLiveOnlyCheck ) )
	{
		const DWORD start_error = Engine.StartLogging( ReadControlTextUtf8( hCsvEdit ) );
		if ( start_error != EC_OK )
			ShowError( start_error );
	}
}


void CMainWindow::OnStartLogging()
{
	if ( _IsChecked( hLiveOnlyCheck ) )
	{
		Engine.StopLogging();
		return;
	}

	string csv_path = ReadControlTextUtf8( hCsvEdit );
	if ( csv_path.empty() )
	{
		csv_path = Engine.MakeDefaultCsvPath( ActiveDeviceTypeForUi() );
		SetWindowTextUtf8( hCsvEdit, csv_path );
	}

	const DWORD error = Engine.StartLogging( csv_path );
	if ( error != EC_OK )
		ShowError( error );
}


void CMainWindow::OnStopLogging()
{
	const DWORD error = Engine.StopLogging();
	if ( error != EC_OK )
		ShowError( error );
}


void CMainWindow::OnFactoryReset()
{
	if ( MessageBoxW( hWnd, L"Werkseinstellungen fuer das angeschlossene Geraet laden?", L"Werkreset", MB_ICONQUESTION | MB_YESNO ) != IDYES )
		return;

	const DWORD error = Engine.FactoryResetDevice();
	if ( error != EC_OK )
		ShowError( error );
}


void CMainWindow::OnSetUnit()
{
	const DWORD error = Engine.SetUnit( SelectedUnitCode() );
	if ( error != EC_OK )
		ShowError( error );
}


void CMainWindow::OnGauge( const bool i_On )
{
	bool ok = false;
	const BYTE channel = SelectedChannel( &ok );
	if ( !ok )
		return;

	const DWORD error = Engine.SetSensorState( channel, i_On );
	if ( error != EC_OK )
		ShowError( error );
}


void CMainWindow::OnReadNow()
{
	bool ok = false;
	const BYTE channel = SelectedChannel( &ok );
	if ( !ok )
		return;

	const DWORD error = Engine.ReadSingleChannelNow( channel );
	if ( error != EC_OK )
		ShowError( error );
}


void CMainWindow::OnDiagnose()
{
	const DWORD error = Engine.ReadDeviceInfo();
	if ( error != EC_OK )
		ShowError( error );
}


void CMainWindow::OnActivateVerify()
{
	bool ok = false;
	const BYTE channel = SelectedChannel( &ok );
	if ( !ok )
		return;

	const DWORD error = Engine.ActivateAndVerify( channel );
	if ( error != EC_OK )
		ShowError( error );
}


void CMainWindow::OnDegas( const bool i_On )
{
	bool ok = false;
	const BYTE channel = SelectedChannel( &ok );
	if ( !ok )
		return;

	const DWORD error = Engine.SetDegas( channel, i_On );
	if ( error != EC_OK )
		ShowError( error );
}


void CMainWindow::OnSetFilter()
{
	bool ok = false;
	const BYTE channel = SelectedChannel( &ok );
	if ( !ok )
		return;

	const DWORD error = Engine.SetFilter( channel, SelectedFilterCode() );
	if ( error != EC_OK )
		ShowError( error );
}


void CMainWindow::OnSetCalibration()
{
	bool ok = false;
	const BYTE channel = SelectedChannel( &ok );
	if ( !ok )
		return;

	const double value = ReadDouble( hCalibrationEdit, L"Kalibrierfaktor", &ok );
	if ( !ok )
		return;

	const DWORD error = Engine.SetCalibration( channel, value );
	if ( error != EC_OK )
		ShowError( error );
}


void CMainWindow::OnSetFsr()
{
	bool ok = false;
	const BYTE channel = SelectedChannel( &ok );
	if ( !ok )
		return;

	const DWORD error = Engine.SetFsr( channel, SelectedFsrCode() );
	if ( error != EC_OK )
		ShowError( error );
}


void CMainWindow::OnSetOfc()
{
	bool ok = false;
	const BYTE channel = SelectedChannel( &ok );
	if ( !ok )
		return;

	const DWORD error = Engine.SetOfc( channel, SelectedOfcCode() );
	if ( error != EC_OK )
		ShowError( error );
}


void CMainWindow::OnSetDisplayName()
{
	bool ok = false;
	const BYTE channel = SelectedChannel( &ok );
	if ( !ok )
		return;

	const DWORD error = Engine.SetChannelName( channel, ReadControlTextUtf8( hDisplayNameEdit ) );
	if ( error != EC_OK )
	{
		ShowError( error );
		return;
	}

	SyncDisplayNameField();
}


void CMainWindow::OnSetDigits()
{
	const DWORD error = Engine.SetDigits( SelectedDigits() );
	if ( error != EC_OK )
		ShowError( error );
}


void CMainWindow::OnSetContrast()
{
	bool ok = false;
	const int value = ReadInt( hContrastEdit, L"Contrast", &ok );
	if ( !ok )
		return;

	const DWORD error = Engine.SetContrast( value );
	if ( error != EC_OK )
		ShowError( error );
}


void CMainWindow::OnSetScreensave()
{
	bool ok = false;
	const int value = ReadInt( hScreensaveEdit, L"Screensave", &ok );
	if ( !ok )
		return;

	const DWORD error = Engine.SetScreensave( value );
	if ( error != EC_OK )
		ShowError( error );
}


void CMainWindow::OnSendRaw()
{
	const DWORD error = Engine.ExecuteRawCommand( ReadControlTextUtf8( hRawEdit ) );
	if ( error != EC_OK )
		ShowError( error );
}


void CMainWindow::OnBrowseCsv( const bool i_SaveDialog )
{
	const wstring path = _OpenFileDialog( hWnd, i_SaveDialog, _TrimmedWindowText( hCsvEdit ) );
	if ( !path.empty() )
		SetWindowTextW( hCsvEdit, path.c_str() );
}


void CMainWindow::OnPlotCsv()
{
	const wstring path = _OpenFileDialog( hWnd, false, L"" );
	if ( path.empty() )
		return;

	if ( !LoadCsvSnapshot( path, &CsvSnapshot ) )
	{
		MessageBoxW( hWnd, L"Die CSV-Datei konnte nicht gelesen oder geparst werden.", L"CSV-Plot fehlgeschlagen", MB_ICONERROR | MB_OK );
		return;
	}

	OpenCsvPlot();
	if ( CsvPlotWindow.Window() != 0 )
	{
		const size_t separator = path.find_last_of( L"\\/" );
		const wstring filename = (separator == wstring::npos) ? path : path.substr( separator + 1 );
		SetWindowTextW( CsvPlotWindow.Window(), (wstring( L"CSV-Plot: " ) + filename).c_str() );
	}
}


void CMainWindow::OnToggleControlPanel()
{
	bControlVisible = !bControlVisible;
	SetWindowTextW( hToggleControlButton, bControlVisible ? L"Steuerung / Parameter ausblenden" : L"Steuerung / Parameter einblenden" );
	LayoutChildren();
}


int WINAPI wWinMain( HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow )
{
	CMainWindow window;
	if ( !window.Create( hInstance ) )
		return 0;

	ShowWindow( window.Window(), nCmdShow );
	UpdateWindow( window.Window() );

	MSG msg;
	while ( GetMessageW( &msg, 0, 0, 0 ) )
	{
		TranslateMessage( &msg );
		DispatchMessageW( &msg );
	}

	return 0;
}
