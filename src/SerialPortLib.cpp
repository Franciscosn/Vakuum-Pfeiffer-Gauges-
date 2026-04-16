///////////////////////////////////////////////////////////////////////////////////////////////////
//
// SerialPortLib.cpp: implementation of the CSerialPort class.
//
///////////////////////////////////////////////////////////////////////////////////////////////////


#include "SerialPortLib.h"

#include <algorithm>
#include <cstring>
#include <sstream>
#include <thread>

#ifndef MS_WIN
	#include <cerrno>
	#include <fcntl.h>
	#include <glob.h>
	#include <sys/select.h>
	#include <sys/stat.h>
	#include <sys/types.h>
	#include <unistd.h>
#endif

using namespace std;


CSerialPort::CSerialPort()
{
	#ifdef MS_WIN
		hPort = INVALID_HANDLE_VALUE;
	#else
		nFileDescriptor = -1;
	#endif

	sPort = "";
	dwTimeoutMs = 200;
}


CSerialPort::~CSerialPort()
{
	Close();
}


//-------------------------------------------------------------------------------------------------
/// Open one serial transport in 8N1 mode.
///
DWORD CSerialPort::Open( const string& i_Port, const DWORD i_BaudRate, const DWORD i_TimeoutMs )
{
	if ( i_Port.length() == 0 )
		return CSerialPort_Open | ES_OutOfRange;

	if ( GetOpen() )
		if ( const DWORD error = Close() )
			return error;

	#ifdef MS_WIN
		const string normalized_port = NormalizeWindowsPort( i_Port );
		hPort = CreateFileA( normalized_port.c_str(),
							 GENERIC_READ | GENERIC_WRITE,
							 0,
							 0,
							 OPEN_EXISTING,
							 0,
							 0 );
		if ( hPort == INVALID_HANDLE_VALUE )
			return CSerialPort_Open | EH_Init;

		DCB dcb;
		memset( &dcb, 0, sizeof(dcb) );
		dcb.DCBlength = sizeof(dcb);
		if ( !GetCommState( hPort, &dcb ) )
			return CSerialPort_Open | EH_Init;

		dcb.BaudRate = i_BaudRate;
		dcb.ByteSize = 8;
		dcb.Parity = NOPARITY;
		dcb.StopBits = ONESTOPBIT;
		dcb.fBinary = TRUE;
		dcb.fParity = FALSE;
		dcb.fOutxCtsFlow = FALSE;
		dcb.fOutxDsrFlow = FALSE;
		dcb.fDtrControl = DTR_CONTROL_DISABLE;
		dcb.fRtsControl = RTS_CONTROL_DISABLE;
		dcb.fOutX = FALSE;
		dcb.fInX = FALSE;

		if ( !SetCommState( hPort, &dcb ) )
			return CSerialPort_Open | EH_Init;

		SetupComm( hPort, 4096, 4096 );
		dwTimeoutMs = i_TimeoutMs;
		if ( const DWORD error = ApplyReadTimeout( dwTimeoutMs ) )
			return error;

		PurgeComm( hPort, PURGE_RXCLEAR | PURGE_TXCLEAR );
	#else
		nFileDescriptor = open( i_Port.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK );
		if ( nFileDescriptor < 0 )
			return CSerialPort_Open | EH_Init;

		struct termios options;
		memset( &options, 0, sizeof(options) );
		if ( tcgetattr( nFileDescriptor, &options ) != 0 )
			return CSerialPort_Open | EH_Init;

		cfmakeraw( &options );
		const speed_t baud = MapBaudRate( i_BaudRate );
		if ( cfsetispeed( &options, baud ) != 0 )
			return CSerialPort_Open | EH_Init;
		if ( cfsetospeed( &options, baud ) != 0 )
			return CSerialPort_Open | EH_Init;

		options.c_cflag |= (CLOCAL | CREAD);
		options.c_cflag &= ~PARENB;
		options.c_cflag &= ~CSTOPB;
		options.c_cflag &= ~CSIZE;
		options.c_cflag |= CS8;
		options.c_cc[VMIN] = 0;
		options.c_cc[VTIME] = 0;

		if ( tcsetattr( nFileDescriptor, TCSANOW, &options ) != 0 )
			return CSerialPort_Open | EH_Init;

		tcflush( nFileDescriptor, TCIOFLUSH );
		dwTimeoutMs = i_TimeoutMs;
	#endif

	sPort = i_Port;
	return EC_OK;
}


//-------------------------------------------------------------------------------------------------
/// Close the serial interface if it is open.
///
DWORD CSerialPort::Close()
{
	#ifdef MS_WIN
		if ( hPort != INVALID_HANDLE_VALUE )
		{
			if ( !CloseHandle( hPort ) )
				return CSerialPort_Close | EH_Close;
			hPort = INVALID_HANDLE_VALUE;
		}
	#else
		if ( nFileDescriptor >= 0 )
		{
			if ( close( nFileDescriptor ) != 0 )
				return CSerialPort_Close | EH_Close;
			nFileDescriptor = -1;
		}
	#endif

	sPort = "";
	return EC_OK;
}


bool CSerialPort::GetOpen() const
{
	#ifdef MS_WIN
		return (hPort != INVALID_HANDLE_VALUE);
	#else
		return (nFileDescriptor >= 0);
	#endif
}


string CSerialPort::GetPort() const
{
	return sPort;
}


DWORD CSerialPort::GetTimeoutMs() const
{
	return dwTimeoutMs;
}


//-------------------------------------------------------------------------------------------------
/// Read up to 'i_MaxBytes' bytes with one explicit timeout.
/// A timeout without new data is not treated as an error and returns zero bytes.
///
DWORD CSerialPort::Read( char *pBuffer, const size_t i_MaxBytes, size_t *pBytesRead, const DWORD i_TimeoutMs )
{
	if ( pBytesRead != 0 )
		*pBytesRead = 0;

	if ( !GetOpen() )
		return CSerialPort_Read | ES_NotInitialized;
	if ( (pBuffer == 0) || (i_MaxBytes == 0) )
		return CSerialPort_Read | ES_OutOfRange;

	#ifdef MS_WIN
		if ( const DWORD error = ApplyReadTimeout( i_TimeoutMs ) )
			return error;

		DWORD bytes_read = 0;
		if ( !ReadFile( hPort, pBuffer, static_cast<DWORD>( i_MaxBytes ), &bytes_read, 0 ) )
			return CSerialPort_Read | EH_Read;
		if ( pBytesRead != 0 )
			*pBytesRead = static_cast<size_t>( bytes_read );
	#else
		fd_set read_set;
		FD_ZERO( &read_set );
		FD_SET( nFileDescriptor, &read_set );

		struct timeval timeout;
		timeout.tv_sec = static_cast<long>( i_TimeoutMs / 1000 );
		timeout.tv_usec = static_cast<long>( (i_TimeoutMs % 1000) * 1000 );

		const int select_result = select( nFileDescriptor + 1, &read_set, 0, 0, &timeout );
		if ( select_result < 0 )
			return CSerialPort_Read | EH_Read;
		if ( select_result == 0 )
			return EC_OK;

		const ssize_t bytes_read = read( nFileDescriptor, pBuffer, i_MaxBytes );
		if ( bytes_read < 0 )
		{
			if ( (errno == EAGAIN) || (errno == EWOULDBLOCK) )
				return EC_OK;
			return CSerialPort_Read | EH_Read;
		}
		if ( pBytesRead != 0 )
			*pBytesRead = static_cast<size_t>( bytes_read );
	#endif

	return EC_OK;
}


//-------------------------------------------------------------------------------------------------
/// Write one byte sequence to the transport.
///
DWORD CSerialPort::Write( const char *pBuffer, const size_t i_BytesToWrite )
{
	if ( !GetOpen() )
		return CSerialPort_Write | ES_NotInitialized;
	if ( (pBuffer == 0) || (i_BytesToWrite == 0) )
		return CSerialPort_Write | ES_OutOfRange;

	#ifdef MS_WIN
		DWORD bytes_written = 0;
		if ( !WriteFile( hPort, pBuffer, static_cast<DWORD>( i_BytesToWrite ), &bytes_written, 0 ) )
			return CSerialPort_Write | EH_Write;
		if ( bytes_written != i_BytesToWrite )
			return CSerialPort_Write | EH_Write;
		FlushFileBuffers( hPort );
	#else
		size_t total_bytes = 0;
		while ( total_bytes < i_BytesToWrite )
		{
			const ssize_t current_bytes = write( nFileDescriptor, pBuffer + total_bytes, i_BytesToWrite - total_bytes );
			if ( current_bytes < 0 )
				return CSerialPort_Write | EH_Write;
			total_bytes += static_cast<size_t>( current_bytes );
		}
		tcdrain( nFileDescriptor );
	#endif

	return EC_OK;
}


//-------------------------------------------------------------------------------------------------
/// Drain any bytes that are currently available or arrive within the given idle window.
///
DWORD CSerialPort::Drain( string *pData, const double i_Seconds )
{
	if ( pData == 0 )
		return CSerialPort_Drain | ES_NotAvailable;
	if ( !GetOpen() )
		return CSerialPort_Drain | ES_NotInitialized;

	pData->clear();

	const auto end_time = chrono::steady_clock::now() + chrono::duration<double>( i_Seconds );
	while ( chrono::steady_clock::now() < end_time )
	{
		char buffer[256];
		size_t bytes_read = 0;
		if ( const DWORD error = Read( buffer, sizeof(buffer), &bytes_read, 20 ) )
			return error;
		if ( bytes_read > 0 )
			pData->append( buffer, bytes_read );
		else
			this_thread::sleep_for( chrono::milliseconds( 10 ) );
	}

	return EC_OK;
}


//-------------------------------------------------------------------------------------------------
/// Collect a conservative list of likely serial ports.
///
DWORD CSerialPort::CollectSuggestedPorts( vector<string> *pPorts ) const
{
	if ( pPorts == 0 )
		return CSerialPort_CollectPorts | ES_NotAvailable;

	pPorts->clear();

	#ifdef MS_WIN
		for ( int i = 1; i <= 32; i++ )
		{
			stringstream stream;
			stream << "COM" << i;
			pPorts->push_back( stream.str() );
		}
	#else
		glob_t glob_result;
		memset( &glob_result, 0, sizeof(glob_result) );

		if ( glob( "/dev/cu.*", 0, 0, &glob_result ) == 0 )
			for ( size_t i = 0; i < glob_result.gl_pathc; i++ )
				pPorts->push_back( glob_result.gl_pathv[i] );
		globfree( &glob_result );

		memset( &glob_result, 0, sizeof(glob_result) );
		if ( glob( "/dev/tty.*", 0, 0, &glob_result ) == 0 )
			for ( size_t i = 0; i < glob_result.gl_pathc; i++ )
				if ( find( pPorts->begin(), pPorts->end(), glob_result.gl_pathv[i] ) == pPorts->end() )
					pPorts->push_back( glob_result.gl_pathv[i] );
		globfree( &glob_result );

		sort( pPorts->begin(), pPorts->end() );
	#endif

	return EC_OK;
}


//-------------------------------------------------------------------------------------------------
/// Report the local class and method name for one serial port error code.
///
bool CSerialPort::GetClassAndMethod( const DWORD i_MethodId, string *pClassAndMethodName )
{
	if ( pClassAndMethodName == 0 )
		return false;

	if ( (i_MethodId & EC_Mask) == EC_CSerialPort )
	{
		switch ( i_MethodId )
		{
			case CSerialPort_Open:			*pClassAndMethodName = "SerialPort.Open(): "; break;
			case CSerialPort_Close:			*pClassAndMethodName = "SerialPort.Close(): "; break;
			case CSerialPort_Read:			*pClassAndMethodName = "SerialPort.Read(): "; break;
			case CSerialPort_Write:			*pClassAndMethodName = "SerialPort.Write(): "; break;
			case CSerialPort_Drain:			*pClassAndMethodName = "SerialPort.Drain(): "; break;
			case CSerialPort_CollectPorts:	*pClassAndMethodName = "SerialPort.CollectSuggestedPorts(): "; break;
			default:						*pClassAndMethodName = "SerialPort.UnknownMethod(): "; break;
		}
		return true;
	}

	return CErrorLib::GetClassAndMethod( i_MethodId, pClassAndMethodName );
}


#ifdef MS_WIN

DWORD CSerialPort::ApplyReadTimeout( const DWORD i_TimeoutMs )
{
	if ( hPort == INVALID_HANDLE_VALUE )
		return CSerialPort_Read | ES_NotInitialized;

	COMMTIMEOUTS timeouts;
	memset( &timeouts, 0, sizeof(timeouts) );
	timeouts.ReadIntervalTimeout = 20;
	timeouts.ReadTotalTimeoutConstant = i_TimeoutMs;
	timeouts.ReadTotalTimeoutMultiplier = 0;
	timeouts.WriteTotalTimeoutConstant = i_TimeoutMs;
	timeouts.WriteTotalTimeoutMultiplier = 0;

	if ( !SetCommTimeouts( hPort, &timeouts ) )
		return CSerialPort_Read | EH_Init;

	dwTimeoutMs = i_TimeoutMs;
	return EC_OK;
}


string CSerialPort::NormalizeWindowsPort( const string& i_Port ) const
{
	if ( i_Port.rfind( "\\\\.\\", 0 ) == 0 )
		return i_Port;

	if ( (i_Port.length() > 3) && (i_Port.substr( 0, 3 ) == "COM") )
		return "\\\\.\\" + i_Port;

	return i_Port;
}

#else

speed_t CSerialPort::MapBaudRate( const DWORD i_BaudRate ) const
{
	switch ( i_BaudRate )
	{
		case 1200:		return B1200;
		case 2400:		return B2400;
		case 4800:		return B4800;
		case 9600:		return B9600;
		case 19200:	return B19200;
		case 38400:	return B38400;
		case 57600:	return B57600;
		case 115200:	return B115200;
		default:		return B9600;
	}
}

#endif
