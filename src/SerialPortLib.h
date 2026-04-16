///////////////////////////////////////////////////////////////////////////////////////////////////
//
// SerialPortLib.h: interface for the CSerialPort class.
//
// ------------------------------------------------------------------------------------------------
//
// Description:
///                                                                               \class CSerialPort
/// 'CSerialPort' wraps the platform-specific serial transport used by the Pfeiffer gauges. The
/// implementation is deliberately conservative: 8N1 only, explicit timeout handling and a simple
/// helper for collecting likely serial port names.
//
// Please announce changes and hints to support@n-cdt.com
// Copyright (c) 2026 CDT GmbH
// All rights reserved.
//
///////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef SERIALPORTLIB_H
#define SERIALPORTLIB_H


#include "ErrorLib.h"

#include <string>
#include <vector>

#ifdef MS_WIN
	#include <windows.h>
#else
	#include <termios.h>
#endif


// error codes
const DWORD EC_CSerialPort            = 0x51000000;
const DWORD CSerialPort_Open          = 0x51010000;
const DWORD CSerialPort_Close         = 0x51020000;
const DWORD CSerialPort_Read          = 0x51030000;
const DWORD CSerialPort_Write         = 0x51040000;
const DWORD CSerialPort_Drain         = 0x51050000;
const DWORD CSerialPort_CollectPorts  = 0x51060000;


class HL_API CSerialPort : virtual public CErrorLib
{

public:

	CSerialPort();
	virtual ~CSerialPort();

	DWORD Open( const std::string& i_Port, const DWORD i_BaudRate, const DWORD i_TimeoutMs );
	DWORD Close();

	bool GetOpen() const;
	std::string GetPort() const;
	DWORD GetTimeoutMs() const;

	DWORD Read( char *pBuffer, const size_t i_MaxBytes, size_t *pBytesRead, const DWORD i_TimeoutMs );
	DWORD Write( const char *pBuffer, const size_t i_BytesToWrite );
	DWORD Drain( std::string *pData, const double i_Seconds );

	DWORD CollectSuggestedPorts( std::vector<std::string> *pPorts ) const;

	virtual bool GetClassAndMethod( const DWORD i_MethodId, std::string *pClassAndMethodName );

private:

	#ifdef MS_WIN
		DWORD ApplyReadTimeout( const DWORD i_TimeoutMs );
		std::string NormalizeWindowsPort( const std::string& i_Port ) const;
		HANDLE hPort;
	#else
		speed_t MapBaudRate( const DWORD i_BaudRate ) const;
		int nFileDescriptor;
	#endif

	std::string sPort;
	DWORD dwTimeoutMs;
};


#endif  // SERIALPORTLIB_H
