///////////////////////////////////////////////////////////////////////////////////////////////////
//
// ErrorLib.h: interface for the CErrorLib class.
//
// ------------------------------------------------------------------------------------------------
//
// Description:
///                                                                                 \class CErrorLib
/// 'CErrorLib' provides the error code layout and the text formatting used by the new pressure
/// logger code. The implementation is intentionally compact, but it follows the CDT convention of
/// returning one 32-bit error code that contains class id, method id and error category.
//
// Please announce changes and hints to support@n-cdt.com
// Copyright (c) 2026 CDT GmbH
// All rights reserved.
//
///////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef ERRORLIB_H
#define ERRORLIB_H


#include "HardwareLib.h"

#include <string>


// default masks
const DWORD EC_Mask               = 0xff000000;
const DWORD EM_Mask               = 0x00ff0000;
const DWORD EH_Mask               = 0x0000ff00;
const DWORD ES_Mask               = 0x000000ff;

// class and method ids
const DWORD EC_CErrorLib          = 0x00000000;
const DWORD CErrorLib_GetError    = 0x00010000;

// success
const DWORD EC_OK                 = 0x00000000;

// hardware error codes
const DWORD EH_Init               = 0x00000100;
const DWORD EH_Close              = 0x00000200;
const DWORD EH_Read               = 0x00000300;
const DWORD EH_Write              = 0x00000400;
const DWORD EH_NotInitialized     = 0x00000500;
const DWORD EH_TimeOut            = 0x00000600;
const DWORD EH_InvalidResponse    = 0x00000700;
const DWORD EH_NotAvailable       = 0x00000800;
const DWORD EH_NoAcknowledge      = 0x00000900;

// software error codes
const DWORD ES_OutOfRange         = 0x00000001;
const DWORD ES_NotInitialized     = 0x00000002;
const DWORD ES_NotAvailable       = 0x00000003;
const DWORD ES_TimeOut            = 0x00000004;
const DWORD ES_Failure            = 0x00000005;
const DWORD ES_SyntaxError        = 0x00000006;
const DWORD ES_AlreadyInitialized = 0x00000007;
const DWORD ES_UnknownMethod      = 0x00000008;


class HL_API CErrorLib
{

public:

	CErrorLib();
	virtual ~CErrorLib();

	std::string GetErrorText( const DWORD i_ErrorCode, const DWORD i_OSErrorCode = 0 );
	virtual bool GetClassAndMethod( const DWORD i_MethodId, std::string *pClassAndMethodName );

protected:

	std::string GetMethodName( const DWORD i_MethodId ) const;
	std::string GetHardwareErrorText( const DWORD i_ErrorCode ) const;
	std::string GetSoftwareErrorText( const DWORD i_ErrorCode ) const;
	std::string GetOSErrorText( const DWORD i_OSErrorCode ) const;
};


#endif  // ERRORLIB_H
