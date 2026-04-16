///////////////////////////////////////////////////////////////////////////////////////////////////
//
// ErrorLib.cpp: implementation of the CErrorLib class.
//
///////////////////////////////////////////////////////////////////////////////////////////////////


#include "ErrorLib.h"

#include <sstream>

#ifdef MS_WIN
	#include <windows.h>
#else
	#include <cerrno>
	#include <cstring>
#endif

using namespace std;


CErrorLib::CErrorLib()
{
}


CErrorLib::~CErrorLib()
{
}


//-------------------------------------------------------------------------------------------------
/// Format one CDT-style error code into a readable text string.
///
string CErrorLib::GetErrorText( const DWORD i_ErrorCode, const DWORD i_OSErrorCode )
{
	if ( i_ErrorCode == EC_OK )
		return "OK";

	string class_and_method;
	if ( !GetClassAndMethod( i_ErrorCode & (EC_Mask | EM_Mask), &class_and_method ) )
		class_and_method = "UnknownClass." + GetMethodName( i_ErrorCode & (EC_Mask | EM_Mask) ) + ": ";

	const string hardware_text = GetHardwareErrorText( i_ErrorCode );
	const string software_text = GetSoftwareErrorText( i_ErrorCode );
	const string os_text = (i_OSErrorCode != 0) ? GetOSErrorText( i_OSErrorCode ) : "";

	stringstream stream;
	stream << class_and_method;

	if ( hardware_text.length() != 0 )
		stream << hardware_text;
	else if ( software_text.length() != 0 )
		stream << software_text;
	else
		stream << "unknown error";

	if ( os_text.length() != 0 )
		stream << " (" << os_text << ")";

	return stream.str();
}


//-------------------------------------------------------------------------------------------------
/// The base implementation only knows the error library itself.
///
bool CErrorLib::GetClassAndMethod( const DWORD i_MethodId, string *pClassAndMethodName )
{
	if ( pClassAndMethodName == 0 )
		return false;

	if ( (i_MethodId & EC_Mask) != EC_CErrorLib )
		return false;

	*pClassAndMethodName = "ErrorLib." + GetMethodName( i_MethodId ) + ": ";
	return true;
}


//-------------------------------------------------------------------------------------------------
/// Translate one method id into the readable method name.
///
string CErrorLib::GetMethodName( const DWORD i_MethodId ) const
{
	switch ( i_MethodId )
	{
		case CErrorLib_GetError:	return "GetErrorText()";
		default:					return "UnknownMethod()";
	}
}


//-------------------------------------------------------------------------------------------------
/// Translate the hardware error part of one error code.
///
string CErrorLib::GetHardwareErrorText( const DWORD i_ErrorCode ) const
{
	switch ( i_ErrorCode & EH_Mask )
	{
		case EH_Init:				return "hardware init failed";
		case EH_Close:				return "hardware close failed";
		case EH_Read:				return "hardware read failed";
		case EH_Write:				return "hardware write failed";
		case EH_NotInitialized:		return "hardware not initialized";
		case EH_TimeOut:			return "hardware timeout";
		case EH_InvalidResponse:	return "invalid device response";
		case EH_NotAvailable:		return "hardware not available";
		case EH_NoAcknowledge:		return "device returned NAK";
		default:					return "";
	}
}


//-------------------------------------------------------------------------------------------------
/// Translate the software error part of one error code.
///
string CErrorLib::GetSoftwareErrorText( const DWORD i_ErrorCode ) const
{
	switch ( i_ErrorCode & ES_Mask )
	{
		case ES_OutOfRange:			return "parameter out of range";
		case ES_NotInitialized:		return "object not initialized";
		case ES_NotAvailable:		return "requested object not available";
		case ES_TimeOut:			return "software timeout";
		case ES_Failure:			return "software failure";
		case ES_SyntaxError:		return "syntax error";
		case ES_AlreadyInitialized:	return "object already initialized";
		case ES_UnknownMethod:		return "unknown method";
		default:					return "";
	}
}


//-------------------------------------------------------------------------------------------------
/// Translate one operating system error code.
///
string CErrorLib::GetOSErrorText( const DWORD i_OSErrorCode ) const
{
	#ifdef MS_WIN
		char *buffer = 0;
		const DWORD length = FormatMessageA( FORMAT_MESSAGE_ALLOCATE_BUFFER |
											 FORMAT_MESSAGE_FROM_SYSTEM |
											 FORMAT_MESSAGE_IGNORE_INSERTS,
											 0,
											 i_OSErrorCode,
											 MAKELANGID( LANG_NEUTRAL, SUBLANG_DEFAULT ),
											 reinterpret_cast<LPSTR>( &buffer ),
											 0,
											 0 );
		string text;
		if ( (length > 0) && (buffer != 0) )
			text.assign( buffer, length );
		LocalFree( buffer );
		return text;
	#else
		return string( strerror( static_cast<int>( i_OSErrorCode ) ) );
	#endif
}
