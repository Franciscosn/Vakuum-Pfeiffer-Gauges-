///////////////////////////////////////////////////////////////////////////////////////////////////
//
// PfeifferGaugeLib.cpp: implementation of the shared Pfeiffer gauge protocol layer.
//
///////////////////////////////////////////////////////////////////////////////////////////////////


#include "PfeifferGaugeLib.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <iomanip>
#include <limits>
#include <sstream>
#include <thread>

using namespace std;


namespace
{
	string _Trim( const string& i_Text )
	{
		size_t begin = 0;
		size_t end = i_Text.length();

		while ( (begin < end) && isspace( static_cast<unsigned char>( i_Text[begin] ) ) )
			begin++;
		while ( (end > begin) && isspace( static_cast<unsigned char>( i_Text[end - 1] ) ) )
			end--;

		return i_Text.substr( begin, end - begin );
	}


	vector<string> _SplitCsv( const string& i_Text )
	{
		vector<string> values;
		stringstream stream( i_Text );
		string token;

		while ( getline( stream, token, ',' ) )
			values.push_back( _Trim( token ) );

		return values;
	}


	DWORD _ParseInts( const string& i_Text, vector<int> *pValues )
	{
		if ( pValues == 0 )
			return ES_NotAvailable;

		pValues->clear();
		const vector<string> tokens = _SplitCsv( i_Text );
		for ( size_t i = 0; i < tokens.size(); i++ )
		{
			try
			{
				pValues->push_back( stoi( tokens[i] ) );
			}
			catch ( ... )
			{
				return ES_SyntaxError;
			}
		}

		return EC_OK;
	}


	DWORD _ParseDoubles( const string& i_Text, vector<double> *pValues )
	{
		if ( pValues == 0 )
			return ES_NotAvailable;

		pValues->clear();
		const vector<string> tokens = _SplitCsv( i_Text );
		for ( size_t i = 0; i < tokens.size(); i++ )
		{
			try
			{
				pValues->push_back( stod( tokens[i] ) );
			}
			catch ( ... )
			{
				return ES_SyntaxError;
			}
		}

		return EC_OK;
	}


	string _FormatIntVector( const vector<int>& i_Values )
	{
		stringstream stream;
		stream << "[";
		for ( size_t i = 0; i < i_Values.size(); i++ )
		{
			if ( i > 0 )
				stream << ", ";
			stream << i_Values[i];
		}
		stream << "]";
		return stream.str();
	}


	string _FormatDoubleVector( const vector<double>& i_Values, const int i_Precision )
	{
		stringstream stream;
		stream.setf( ios::fixed );
		stream << "[";
		for ( size_t i = 0; i < i_Values.size(); i++ )
		{
			if ( i > 0 )
				stream << ", ";
			stream << setprecision( i_Precision ) << i_Values[i];
		}
		stream << "]";
		return stream.str();
	}


	string _FormatStringVector( const vector<string>& i_Values )
	{
		stringstream stream;
		stream << "[";
		for ( size_t i = 0; i < i_Values.size(); i++ )
		{
			if ( i > 0 )
				stream << ", ";
			stream << i_Values[i];
		}
		stream << "]";
		return stream.str();
	}


	string _SanitizeChannelName( const string& i_Name )
	{
		string sanitized;
		for ( size_t i = 0; i < i_Name.length(); i++ )
		{
			const unsigned char character = static_cast<unsigned char>( i_Name[i] );
			if ( isalnum( character ) )
				sanitized.push_back( static_cast<char>( toupper( character ) ) );
		}

		sanitized = sanitized.substr( 0, 4 );
		while ( sanitized.length() < 4 )
			sanitized.push_back( ' ' );

		return sanitized;
	}
}


CPfeifferSerialHelper::CPfeifferSerialHelper( CSerialPort *pPort )
{
	this->pPort = pPort;
}


DWORD CPfeifferSerialHelper::Drain( const double i_Seconds )
{
	if ( pPort == 0 )
		return ES_NotAvailable;

	string data;
	return pPort->Drain( &data, i_Seconds );
}


DWORD CPfeifferSerialHelper::ReadLine( string *pLine, const double i_TimeoutSeconds )
{
	if ( pLine == 0 )
		return ES_NotAvailable;
	if ( pPort == 0 )
		return ES_NotAvailable;

	pLine->clear();
	const auto end_time = chrono::steady_clock::now() + chrono::duration<double>( i_TimeoutSeconds );
	while ( chrono::steady_clock::now() < end_time )
	{
		char character = 0;
		size_t bytes_read = 0;
		if ( const DWORD error = pPort->Read( &character, 1, &bytes_read, 50 ) )
			return error;
		if ( bytes_read == 0 )
			continue;

		pLine->push_back( character );
		if ( character == '\n' )
			break;
	}

	*pLine = _Trim( *pLine );
	return EC_OK;
}


DWORD CPfeifferSerialHelper::ReadUntilIdle( string *pData, const double i_IdleSeconds, const double i_MaxSeconds )
{
	if ( pData == 0 )
		return ES_NotAvailable;
	if ( pPort == 0 )
		return ES_NotAvailable;

	pData->clear();
	const auto end_time = chrono::steady_clock::now() + chrono::duration<double>( i_MaxSeconds );
	auto last_rx = chrono::steady_clock::now();

	while ( chrono::steady_clock::now() < end_time )
	{
		char buffer[256];
		size_t bytes_read = 0;
		if ( const DWORD error = pPort->Read( buffer, sizeof(buffer), &bytes_read, 20 ) )
			return error;
		if ( bytes_read > 0 )
		{
			pData->append( buffer, bytes_read );
			last_rx = chrono::steady_clock::now();
		}
		else
		{
			if ( (!pData->empty()) && (chrono::duration<double>( chrono::steady_clock::now() - last_rx ).count() >= i_IdleSeconds) )
				break;
			this_thread::sleep_for( chrono::milliseconds( 10 ) );
		}
	}

	*pData = _Trim( *pData );
	return EC_OK;
}


DWORD CPfeifferSerialHelper::SendAscii( const string& i_Command )
{
	if ( pPort == 0 )
		return ES_NotAvailable;

	const string payload = i_Command + "\r";
	return pPort->Write( payload.c_str(), payload.length() );
}


DWORD CPfeifferSerialHelper::SendETX()
{
	if ( pPort == 0 )
		return ES_NotAvailable;

	const char etx = 0x03;
	return pPort->Write( &etx, 1 );
}


DWORD CPfeifferSerialHelper::SendENQ()
{
	if ( pPort == 0 )
		return ES_NotAvailable;

	const char enq = 0x05;
	return pPort->Write( &enq, 1 );
}


bool CPfeifferSerialHelper::AckOk( const string& i_RawResponse ) const
{
	return (i_RawResponse.find( '\x06' ) != string::npos) && (i_RawResponse.find( '\x15' ) == string::npos);
}


DWORD CPfeifferSerialHelper::ExpectAck( const double i_TimeoutSeconds )
{
	string raw;
	if ( const DWORD error = ReadUntilIdle( &raw, 0.03, i_TimeoutSeconds ) )
		return error;

	if ( AckOk( raw ) )
		return EC_OK;
	if ( raw.find( '\x15' ) != string::npos )
		return EH_NoAcknowledge;
	return EH_InvalidResponse;
}


DWORD CPfeifferSerialHelper::RequestResponse( const string& i_Command, string *pResponse, const double i_TimeoutSeconds )
{
	if ( pResponse == 0 )
		return ES_NotAvailable;

	if ( const DWORD error = SendAscii( i_Command ) )
		return error;
	if ( const DWORD error = ExpectAck( i_TimeoutSeconds ) )
		return error;
	if ( const DWORD error = SendENQ() )
		return error;
	if ( const DWORD error = ReadUntilIdle( pResponse, 0.05, max( 1.0, i_TimeoutSeconds ) ) )
		return error;
	if ( pResponse->length() == 0 )
		return EH_InvalidResponse;

	return EC_OK;
}


DWORD CPfeifferSerialHelper::WriteOnly( const string& i_Command, const double i_TimeoutSeconds )
{
	if ( const DWORD error = SendAscii( i_Command ) )
		return error;
	return ExpectAck( i_TimeoutSeconds );
}


CPfeifferGaugeDriver::CPfeifferGaugeDriver()
	: Helper( &Port )
{
	bInitialized = false;
}


CPfeifferGaugeDriver::~CPfeifferGaugeDriver()
{
	Close();
}


DWORD CPfeifferGaugeDriver::Init( const PressureLoggerConnectionSetup i_Setup )
{
	lock_guard<recursive_mutex> lock( Mutex );

	if ( bInitialized )
		if ( const DWORD error = Close() )
			return error;

	Setup = i_Setup;
	if ( const DWORD error = Port.Open( Setup.sPort, Setup.dwBaudRate, Setup.dwTimeoutMs ) )
		return error;

	bInitialized = true;
	return EC_OK;
}


DWORD CPfeifferGaugeDriver::Close()
{
	lock_guard<recursive_mutex> lock( Mutex );

	DWORD error = EC_OK;
	if ( Port.GetOpen() )
	{
		FinishMonitoringSession();
		error = Port.Close();
	}

	bInitialized = false;
	return error;
}


bool CPfeifferGaugeDriver::GetInit() const
{
	return (bInitialized && Port.GetOpen());
}


struct PressureLoggerConnectionSetup CPfeifferGaugeDriver::GetConnectionSetup() const
{
	return Setup;
}


recursive_mutex& CPfeifferGaugeDriver::GetMutex()
{
	return Mutex;
}


DWORD CPfeifferGaugeDriver::StartMonitoringSession()
{
	lock_guard<recursive_mutex> lock( Mutex );
	if ( !GetInit() )
		return PfeifferGaugeDriver_Init | ES_NotInitialized;
	return PrepareMonitoringSession();
}


DWORD CPfeifferGaugeDriver::StopMonitoringSession()
{
	lock_guard<recursive_mutex> lock( Mutex );
	if ( !GetInit() )
		return EC_OK;
	return FinishMonitoringSession();
}


double CPfeifferGaugeDriver::GetSuggestedLoopDelaySeconds() const
{
	return Setup.dPollingSeconds;
}


DWORD CPfeifferGaugeDriver::SetChannelName( const BYTE i_Channel, const string& i_Name )
{
	(void) i_Channel;
	(void) i_Name;
	return ES_NotAvailable;
}


DWORD CPfeifferGaugeDriver::SetDigits( const int i_Value )
{
	(void) i_Value;
	return ES_NotAvailable;
}


DWORD CPfeifferGaugeDriver::SetContrast( const int i_Value )
{
	(void) i_Value;
	return ES_NotAvailable;
}


DWORD CPfeifferGaugeDriver::SetScreensave( const int i_Value )
{
	(void) i_Value;
	return ES_NotAvailable;
}


DWORD CPfeifferGaugeDriver::CollectSuggestedPorts( vector<string> *pPorts ) const
{
	return Port.CollectSuggestedPorts( pPorts );
}


string CPfeifferGaugeDriver::StatusText( const int i_StatusCode )
{
	switch ( i_StatusCode )
	{
		case 0: return "ok";
		case 1: return "underrange";
		case 2: return "overrange";
		case 3: return "sensor error";
		case 4: return "sensor off";
		case 5: return "no sensor / not identified";
		case 6: return "identification error";
		default:
		{
			stringstream stream;
			stream << "unknown (" << i_StatusCode << ")";
			return stream.str();
		}
	}
}


bool CPfeifferGaugeDriver::GetClassAndMethod( const DWORD i_MethodId, string *pClassAndMethodName )
{
	if ( pClassAndMethodName == 0 )
		return false;

	if ( (i_MethodId & EC_Mask) == EC_CPfeifferGaugeDriver )
	{
		switch ( i_MethodId )
		{
			case PfeifferGaugeDriver_Init:		*pClassAndMethodName = "PfeifferGaugeDriver.Init(): "; break;
			case PfeifferGaugeDriver_Close:		*pClassAndMethodName = "PfeifferGaugeDriver.Close(): "; break;
			case PfeifferGaugeDriver_ReadSample:*pClassAndMethodName = "PfeifferGaugeDriver.ReadSample(): "; break;
			case PfeifferGaugeDriver_Query:		*pClassAndMethodName = "PfeifferGaugeDriver.Query(): "; break;
			case PfeifferGaugeDriver_Write:		*pClassAndMethodName = "PfeifferGaugeDriver.Write(): "; break;
			case PfeifferGaugeDriver_CollectInfo:*pClassAndMethodName = "PfeifferGaugeDriver.CollectDeviceInfo(): "; break;
			default:							*pClassAndMethodName = "PfeifferGaugeDriver.UnknownMethod(): "; break;
		}
		return true;
	}

	return CErrorLib::GetClassAndMethod( i_MethodId, pClassAndMethodName );
}


DWORD CPfeifferGaugeDriver::PrepareMonitoringSession()
{
	return EC_OK;
}


DWORD CPfeifferGaugeDriver::FinishMonitoringSession()
{
	return EC_OK;
}


CTPG262Driver::CTPG262Driver()
{
}


CTPG262Driver::~CTPG262Driver()
{
}


enum PressureLoggerDeviceType CTPG262Driver::GetDeviceType() const
{
	return PressureLoggerDevice_TPG262;
}


BYTE CTPG262Driver::GetChannelCount() const
{
	return 2;
}


string CTPG262Driver::GetDeviceName() const
{
	return "TPG 262";
}


DWORD CTPG262Driver::PrepareMonitoringSession()
{
	if ( const DWORD error = Helper.Drain( 0.2 ) )
		return error;
	if ( const DWORD error = Helper.SendETX() )
		return error;
	this_thread::sleep_for( chrono::milliseconds( 100 ) );
	if ( const DWORD error = Helper.Drain( 0.2 ) )
		return error;

	if ( !Setup.bTPG262LongTermMode )
	{
		stringstream command;
		command << "COM," << Setup.dwTPG262ContinuousMode;
		if ( const DWORD error = Helper.WriteOnly( command.str(), 2.0 ) )
			return PfeifferGaugeDriver_Init | error;
	}

	return EC_OK;
}


DWORD CTPG262Driver::FinishMonitoringSession()
{
	Helper.SendETX();
	this_thread::sleep_for( chrono::milliseconds( 50 ) );
	return EC_OK;
}


double CTPG262Driver::GetSuggestedLoopDelaySeconds() const
{
	return Setup.bTPG262LongTermMode ? Setup.dPollingSeconds : 0.0;
}


DWORD CTPG262Driver::Query( const string& i_Command, string *pResponse )
{
	lock_guard<recursive_mutex> lock( Mutex );
	if ( !GetInit() )
		return PfeifferGaugeDriver_Query | ES_NotInitialized;

	Helper.SendETX();
	this_thread::sleep_for( chrono::milliseconds( 50 ) );
	Helper.Drain( 0.05 );

	const DWORD error = Helper.RequestResponse( i_Command, pResponse, 1.2 );
	return (error == EC_OK) ? EC_OK : (PfeifferGaugeDriver_Query | error);
}


DWORD CTPG262Driver::Write( const string& i_Command )
{
	lock_guard<recursive_mutex> lock( Mutex );
	if ( !GetInit() )
		return PfeifferGaugeDriver_Write | ES_NotInitialized;

	Helper.SendETX();
	this_thread::sleep_for( chrono::milliseconds( 50 ) );
	Helper.Drain( 0.05 );

	const DWORD error = Helper.WriteOnly( i_Command, 1.2 );
	return (error == EC_OK) ? EC_OK : (PfeifferGaugeDriver_Write | error);
}


DWORD CTPG262Driver::ReadPressureResponse( const BYTE i_Channel, PressureChannelReading *pReading )
{
	if ( pReading == 0 )
		return PfeifferGaugeDriver_ReadSample | ES_NotAvailable;
	if ( (i_Channel < 1) || (i_Channel > 2) )
		return PfeifferGaugeDriver_ReadSample | ES_OutOfRange;

	string response;
	stringstream command;
	command << "PR" << static_cast<unsigned>( i_Channel );
	if ( const DWORD error = Query( command.str(), &response ) )
		return error;

	const vector<string> tokens = _SplitCsv( response );
	if ( tokens.size() < 2 )
		return PfeifferGaugeDriver_ReadSample | EH_InvalidResponse;

	try
	{
		pReading->byChannel = i_Channel;
		pReading->nStatusCode = stoi( tokens[0] );
		pReading->dPressure = stod( tokens[1] );
		pReading->sStatusText = StatusText( pReading->nStatusCode );
	}
	catch ( ... )
	{
		return PfeifferGaugeDriver_ReadSample | EH_InvalidResponse;
	}

	return EC_OK;
}


DWORD CTPG262Driver::ReadSample( PressureSample *pSample )
{
	if ( pSample == 0 )
		return PfeifferGaugeDriver_ReadSample | ES_NotAvailable;

	pSample->ChannelValues.clear();

	if ( Setup.bTPG262LongTermMode )
	{
		for ( BYTE channel = 1; channel <= 2; channel++ )
		{
			PressureChannelReading reading;
			if ( const DWORD error = ReadPressureResponse( channel, &reading ) )
				return error;
			pSample->ChannelValues.push_back( reading );
		}
		return EC_OK;
	}

	lock_guard<recursive_mutex> lock( Mutex );
	string line;
	if ( const DWORD error = Helper.ReadLine( &line, 1.5 ) )
		return PfeifferGaugeDriver_ReadSample | error;
	if ( line.length() == 0 )
		return PfeifferGaugeDriver_ReadSample | EH_TimeOut;

	const vector<string> tokens = _SplitCsv( line );
	if ( tokens.size() < 4 )
		return PfeifferGaugeDriver_ReadSample | EH_InvalidResponse;

	try
	{
		PressureChannelReading reading_a;
		reading_a.byChannel = 1;
		reading_a.nStatusCode = stoi( tokens[0] );
		reading_a.dPressure = stod( tokens[1] );
		reading_a.sStatusText = StatusText( reading_a.nStatusCode );
		pSample->ChannelValues.push_back( reading_a );

		PressureChannelReading reading_b;
		reading_b.byChannel = 2;
		reading_b.nStatusCode = stoi( tokens[2] );
		reading_b.dPressure = stod( tokens[3] );
		reading_b.sStatusText = StatusText( reading_b.nStatusCode );
		pSample->ChannelValues.push_back( reading_b );
	}
	catch ( ... )
	{
		return PfeifferGaugeDriver_ReadSample | EH_InvalidResponse;
	}

	return EC_OK;
}


DWORD CTPG262Driver::ReadSingleChannel( const BYTE i_Channel, PressureChannelReading *pReading )
{
	return ReadPressureResponse( i_Channel, pReading );
}


DWORD CTPG262Driver::ExecuteRaw( const string& i_Command, const bool i_WriteOnly, string *pResponse )
{
	string command = _Trim( i_Command );
	if ( command.length() == 0 )
		return PfeifferGaugeDriver_Query | ES_OutOfRange;

	if ( command[0] == '!' )
		command = _Trim( command.substr( 1 ) );

	if ( i_WriteOnly )
		return Write( command );
	return Query( command, pResponse );
}


DWORD CTPG262Driver::GetSensorStatusFlags( vector<int> *pFlags )
{
	string response;
	if ( const DWORD error = Query( "SEN", &response ) )
		return error;
	if ( const DWORD error = _ParseInts( response, pFlags ) )
		return PfeifferGaugeDriver_Query | error;
	if ( pFlags->size() != 2 )
		return PfeifferGaugeDriver_Query | EH_InvalidResponse;
	return EC_OK;
}


DWORD CTPG262Driver::GetDegas( vector<int> *pValues )
{
	string response;
	if ( const DWORD error = Query( "DGS", &response ) )
		return error;
	if ( const DWORD error = _ParseInts( response, pValues ) )
		return PfeifferGaugeDriver_Query | error;
	if ( pValues->size() != 2 )
		return PfeifferGaugeDriver_Query | EH_InvalidResponse;
	return EC_OK;
}


DWORD CTPG262Driver::GetFilter( vector<int> *pValues )
{
	string response;
	if ( const DWORD error = Query( "FIL", &response ) )
		return error;
	if ( const DWORD error = _ParseInts( response, pValues ) )
		return PfeifferGaugeDriver_Query | error;
	if ( pValues->size() != 2 )
		return PfeifferGaugeDriver_Query | EH_InvalidResponse;
	return EC_OK;
}


DWORD CTPG262Driver::GetCalibration( vector<double> *pValues )
{
	string response;
	if ( const DWORD error = Query( "CAL", &response ) )
		return error;
	if ( const DWORD error = _ParseDoubles( response, pValues ) )
		return PfeifferGaugeDriver_Query | error;
	if ( pValues->size() != 2 )
		return PfeifferGaugeDriver_Query | EH_InvalidResponse;
	return EC_OK;
}


DWORD CTPG262Driver::GetFsr( vector<int> *pValues )
{
	string response;
	if ( const DWORD error = Query( "FSR", &response ) )
		return error;
	if ( const DWORD error = _ParseInts( response, pValues ) )
		return PfeifferGaugeDriver_Query | error;
	if ( pValues->size() != 2 )
		return PfeifferGaugeDriver_Query | EH_InvalidResponse;
	return EC_OK;
}


DWORD CTPG262Driver::GetOfc( vector<int> *pValues )
{
	string response;
	if ( const DWORD error = Query( "OFC", &response ) )
		return error;
	if ( const DWORD error = _ParseInts( response, pValues ) )
		return PfeifferGaugeDriver_Query | error;
	if ( pValues->size() != 2 )
		return PfeifferGaugeDriver_Query | EH_InvalidResponse;
	return EC_OK;
}


DWORD CTPG262Driver::GetIdent( string *pIdent )
{
	return Query( "TID", pIdent );
}


DWORD CTPG262Driver::GetErrorStatus( string *pErrorText )
{
	return Query( "ERR", pErrorText );
}


DWORD CTPG262Driver::ResetErrors( string *pResponse )
{
	return Query( "RES,1", pResponse );
}


DWORD CTPG262Driver::CollectDeviceInfo( vector<string> *pLines )
{
	if ( pLines == 0 )
		return PfeifferGaugeDriver_CollectInfo | ES_NotAvailable;

	pLines->clear();
	string ident;
	vector<int> values_int;
	vector<double> values_double;

	if ( GetIdent( &ident ) == EC_OK )
		pLines->push_back( "TPG 262 IDs: " + ident );
	if ( GetSensorStatusFlags( &values_int ) == EC_OK )
		pLines->push_back( "TPG 262 Sensor on/off: " + _FormatIntVector( values_int ) );
	if ( GetFilter( &values_int ) == EC_OK )
		pLines->push_back( "TPG 262 Filter: " + _FormatIntVector( values_int ) );
	if ( GetCalibration( &values_double ) == EC_OK )
		pLines->push_back( "TPG 262 CAL: " + _FormatDoubleVector( values_double, 3 ) );
	if ( GetFsr( &values_int ) == EC_OK )
		pLines->push_back( "TPG 262 FSR: " + _FormatIntVector( values_int ) );
	if ( GetOfc( &values_int ) == EC_OK )
		pLines->push_back( "TPG 262 OFC: " + _FormatIntVector( values_int ) );
	if ( GetDegas( &values_int ) == EC_OK )
		pLines->push_back( "TPG 262 DGS: " + _FormatIntVector( values_int ) );

	string error_text;
	if ( GetErrorStatus( &error_text ) == EC_OK )
		pLines->push_back( "TPG 262 ERR: " + error_text );

	return EC_OK;
}


DWORD CTPG262Driver::ActivateAndVerify( const BYTE i_Channel, vector<string> *pLines )
{
	if ( pLines == 0 )
		return PfeifferGaugeDriver_CollectInfo | ES_NotAvailable;

	pLines->clear();

	string ident;
	if ( GetIdent( &ident ) == EC_OK )
		pLines->push_back( "TID=" + ident );

	vector<int> before_flags;
	if ( GetSensorStatusFlags( &before_flags ) == EC_OK )
		pLines->push_back( "SEN before=" + _FormatIntVector( before_flags ) );

	const DWORD set_error = SetSensorState( i_Channel, true );
	if ( set_error != EC_OK )
		pLines->push_back( "Activation failed: " + GetErrorText( set_error ) );

	this_thread::sleep_for( chrono::milliseconds( 400 ) );

	vector<int> after_flags;
	if ( GetSensorStatusFlags( &after_flags ) == EC_OK )
		pLines->push_back( "SEN after=" + _FormatIntVector( after_flags ) );

	string error_text;
	if ( GetErrorStatus( &error_text ) == EC_OK )
		pLines->push_back( "ERR=" + error_text );

	PressureChannelReading reading;
	if ( ReadSingleChannel( i_Channel, &reading ) == EC_OK )
	{
		stringstream stream;
		stream.setf( ios::scientific );
		stream << "PR" << static_cast<unsigned>( i_Channel ) << ": " << reading.nStatusCode << ", " << reading.dPressure;
		pLines->push_back( stream.str() );
	}

	return EC_OK;
}


DWORD CTPG262Driver::FactoryReset()
{
	return Write( "SAV,0" );
}


DWORD CTPG262Driver::SetUnit( const int i_UnitCode )
{
	stringstream stream;
	stream << "UNI," << i_UnitCode;
	return Write( stream.str() );
}


DWORD CTPG262Driver::SetSensorState( const BYTE i_Channel, const bool i_TurnOn )
{
	if ( (i_Channel < 1) || (i_Channel > 2) )
		return PfeifferGaugeDriver_Write | ES_OutOfRange;

	vector<int> current;
	if ( const DWORD error = GetSensorStatusFlags( &current ) )
		return error;

	if ( i_TurnOn && (current[i_Channel - 1] == 2) )
		return EC_OK;
	if ( (!i_TurnOn) && (current[i_Channel - 1] == 1) )
		return EC_OK;

	vector<int> values( 2, 0 );
	values[i_Channel - 1] = i_TurnOn ? 2 : 1;

	stringstream command;
	command << "SEN," << values[0] << "," << values[1];
	DWORD error = Write( command.str() );
	if ( (error != EC_OK) && i_TurnOn )
	{
		string reset_response;
		ResetErrors( &reset_response );
		this_thread::sleep_for( chrono::milliseconds( 150 ) );
		error = Write( command.str() );
	}

	return error;
}


DWORD CTPG262Driver::SetDegas( const BYTE i_Channel, const bool i_On )
{
	if ( (i_Channel < 1) || (i_Channel > 2) )
		return PfeifferGaugeDriver_Write | ES_OutOfRange;

	vector<int> values( 2, 0 );
	values[i_Channel - 1] = i_On ? 1 : 0;

	stringstream command;
	command << "DGS," << values[0] << "," << values[1];
	return Write( command.str() );
}


DWORD CTPG262Driver::SetFilter( const BYTE i_Channel, const int i_Value )
{
	if ( (i_Channel < 1) || (i_Channel > 2) )
		return PfeifferGaugeDriver_Write | ES_OutOfRange;

	vector<int> values( 2, 0 );
	values[i_Channel - 1] = i_Value;

	stringstream command;
	command << "FIL," << values[0] << "," << values[1];
	return Write( command.str() );
}


DWORD CTPG262Driver::SetCalibration( const BYTE i_Channel, const double i_Value )
{
	if ( (i_Channel < 1) || (i_Channel > 2) )
		return PfeifferGaugeDriver_Write | ES_OutOfRange;

	vector<double> values( 2, 1.0 );
	values[i_Channel - 1] = i_Value;

	stringstream command;
	command.setf( ios::fixed );
	command << "CAL," << setprecision( 3 ) << values[0] << "," << values[1];
	return Write( command.str() );
}


DWORD CTPG262Driver::SetFsr( const BYTE i_Channel, const int i_Value )
{
	if ( (i_Channel < 1) || (i_Channel > 2) )
		return PfeifferGaugeDriver_Write | ES_OutOfRange;

	vector<int> values( 2, 5 );
	values[i_Channel - 1] = i_Value;

	stringstream command;
	command << "FSR," << values[0] << "," << values[1];
	return Write( command.str() );
}


DWORD CTPG262Driver::SetOfc( const BYTE i_Channel, const int i_Value )
{
	if ( (i_Channel < 1) || (i_Channel > 2) )
		return PfeifferGaugeDriver_Write | ES_OutOfRange;

	vector<int> values( 2, 0 );
	values[i_Channel - 1] = i_Value;

	stringstream command;
	command << "OFC," << values[0] << "," << values[1];
	return Write( command.str() );
}


bool CTPG262Driver::GetClassAndMethod( const DWORD i_MethodId, string *pClassAndMethodName )
{
	return CPfeifferGaugeDriver::GetClassAndMethod( i_MethodId, pClassAndMethodName );
}


CMaxiGaugeDriver::CMaxiGaugeDriver()
{
}


CMaxiGaugeDriver::~CMaxiGaugeDriver()
{
}


enum PressureLoggerDeviceType CMaxiGaugeDriver::GetDeviceType() const
{
	return PressureLoggerDevice_MaxiGauge;
}


BYTE CMaxiGaugeDriver::GetChannelCount() const
{
	return 6;
}


string CMaxiGaugeDriver::GetDeviceName() const
{
	return "MaxiGauge";
}


DWORD CMaxiGaugeDriver::PrepareMonitoringSession()
{
	if ( const DWORD error = Helper.Drain( 0.2 ) )
		return error;
	if ( const DWORD error = Helper.SendETX() )
		return error;
	this_thread::sleep_for( chrono::milliseconds( 100 ) );
	return Helper.Drain( 0.2 );
}


DWORD CMaxiGaugeDriver::FinishMonitoringSession()
{
	Helper.SendETX();
	this_thread::sleep_for( chrono::milliseconds( 50 ) );
	return EC_OK;
}


double CMaxiGaugeDriver::GetSuggestedLoopDelaySeconds() const
{
	return Setup.dPollingSeconds;
}


DWORD CMaxiGaugeDriver::Query( const string& i_Command, string *pResponse )
{
	lock_guard<recursive_mutex> lock( Mutex );
	if ( !GetInit() )
		return PfeifferGaugeDriver_Query | ES_NotInitialized;

	const DWORD error = Helper.RequestResponse( i_Command, pResponse, 1.2 );
	return (error == EC_OK) ? EC_OK : (PfeifferGaugeDriver_Query | error);
}


DWORD CMaxiGaugeDriver::Write( const string& i_Command )
{
	lock_guard<recursive_mutex> lock( Mutex );
	if ( !GetInit() )
		return PfeifferGaugeDriver_Write | ES_NotInitialized;

	const DWORD error = Helper.WriteOnly( i_Command, 1.2 );
	return (error == EC_OK) ? EC_OK : (PfeifferGaugeDriver_Write | error);
}


DWORD CMaxiGaugeDriver::ReadPressureResponse( const BYTE i_Channel, PressureChannelReading *pReading )
{
	if ( pReading == 0 )
		return PfeifferGaugeDriver_ReadSample | ES_NotAvailable;
	if ( (i_Channel < 1) || (i_Channel > 6) )
		return PfeifferGaugeDriver_ReadSample | ES_OutOfRange;

	stringstream command;
	command << "PR" << static_cast<unsigned>( i_Channel );
	string response;
	if ( const DWORD error = Query( command.str(), &response ) )
		return error;

	const vector<string> tokens = _SplitCsv( response );
	if ( tokens.size() < 2 )
		return PfeifferGaugeDriver_ReadSample | EH_InvalidResponse;

	try
	{
		pReading->byChannel = i_Channel;
		pReading->nStatusCode = stoi( tokens[0] );
		pReading->dPressure = stod( tokens[1] );
		pReading->sStatusText = StatusText( pReading->nStatusCode );
	}
	catch ( ... )
	{
		return PfeifferGaugeDriver_ReadSample | EH_InvalidResponse;
	}

	return EC_OK;
}


DWORD CMaxiGaugeDriver::ReadSample( PressureSample *pSample )
{
	if ( pSample == 0 )
		return PfeifferGaugeDriver_ReadSample | ES_NotAvailable;

	pSample->ChannelValues.clear();
	for ( BYTE channel = 1; channel <= 6; channel++ )
	{
		PressureChannelReading reading;
		DWORD error = ReadPressureResponse( channel, &reading );
		if ( error != EC_OK )
		{
			reading.byChannel = channel;
			reading.nStatusCode = 6;
			reading.dPressure = numeric_limits<double>::quiet_NaN();
			reading.sStatusText = StatusText( 6 );
		}
		pSample->ChannelValues.push_back( reading );
	}

	return EC_OK;
}


DWORD CMaxiGaugeDriver::ReadSingleChannel( const BYTE i_Channel, PressureChannelReading *pReading )
{
	return ReadPressureResponse( i_Channel, pReading );
}


DWORD CMaxiGaugeDriver::ExecuteRaw( const string& i_Command, const bool i_WriteOnly, string *pResponse )
{
	string command = _Trim( i_Command );
	if ( command.length() == 0 )
		return PfeifferGaugeDriver_Query | ES_OutOfRange;

	if ( command[0] == '!' )
		command = _Trim( command.substr( 1 ) );

	if ( i_WriteOnly )
		return Write( command );
	return Query( command, pResponse );
}


DWORD CMaxiGaugeDriver::GetSensorOnOff( vector<int> *pValues )
{
	string response;
	if ( const DWORD error = Query( "SEN", &response ) )
		return error;
	if ( const DWORD error = _ParseInts( response, pValues ) )
		return PfeifferGaugeDriver_Query | error;
	if ( pValues->size() != 6 )
		return PfeifferGaugeDriver_Query | EH_InvalidResponse;
	return EC_OK;
}


DWORD CMaxiGaugeDriver::GetDegas( vector<int> *pValues )
{
	string response;
	if ( const DWORD error = Query( "DGS", &response ) )
		return error;
	if ( const DWORD error = _ParseInts( response, pValues ) )
		return PfeifferGaugeDriver_Query | error;
	if ( pValues->size() != 6 )
		return PfeifferGaugeDriver_Query | EH_InvalidResponse;
	return EC_OK;
}


DWORD CMaxiGaugeDriver::GetFilter( vector<int> *pValues )
{
	string response;
	if ( const DWORD error = Query( "FIL", &response ) )
		return error;
	if ( const DWORD error = _ParseInts( response, pValues ) )
		return PfeifferGaugeDriver_Query | error;
	if ( pValues->size() != 6 )
		return PfeifferGaugeDriver_Query | EH_InvalidResponse;
	return EC_OK;
}


DWORD CMaxiGaugeDriver::GetOfc( vector<int> *pValues )
{
	string response;
	if ( const DWORD error = Query( "OFC", &response ) )
		return error;
	if ( const DWORD error = _ParseInts( response, pValues ) )
		return PfeifferGaugeDriver_Query | error;
	if ( pValues->size() != 6 )
		return PfeifferGaugeDriver_Query | EH_InvalidResponse;
	return EC_OK;
}


DWORD CMaxiGaugeDriver::GetFsr( vector<int> *pValues )
{
	string response;
	if ( const DWORD error = Query( "FSR", &response ) )
		return error;
	if ( const DWORD error = _ParseInts( response, pValues ) )
		return PfeifferGaugeDriver_Query | error;
	if ( pValues->size() != 6 )
		return PfeifferGaugeDriver_Query | EH_InvalidResponse;
	return EC_OK;
}


DWORD CMaxiGaugeDriver::GetChannelNames( vector<string> *pNames )
{
	if ( pNames == 0 )
		return PfeifferGaugeDriver_Query | ES_NotAvailable;

	string response;
	if ( const DWORD error = Query( "CID", &response ) )
		return error;
	*pNames = _SplitCsv( response );
	if ( pNames->size() != 6 )
		return PfeifferGaugeDriver_Query | EH_InvalidResponse;
	return EC_OK;
}


DWORD CMaxiGaugeDriver::GetDigits( int *pDigits )
{
	if ( pDigits == 0 )
		return PfeifferGaugeDriver_Query | ES_NotAvailable;

	string response;
	if ( const DWORD error = Query( "DCD", &response ) )
		return error;

	try
	{
		*pDigits = stoi( response );
	}
	catch ( ... )
	{
		return PfeifferGaugeDriver_Query | EH_InvalidResponse;
	}

	return EC_OK;
}


DWORD CMaxiGaugeDriver::GetContrast( int *pContrast )
{
	if ( pContrast == 0 )
		return PfeifferGaugeDriver_Query | ES_NotAvailable;

	string response;
	if ( const DWORD error = Query( "DCC", &response ) )
		return error;

	try
	{
		*pContrast = stoi( response );
	}
	catch ( ... )
	{
		return PfeifferGaugeDriver_Query | EH_InvalidResponse;
	}

	return EC_OK;
}


DWORD CMaxiGaugeDriver::GetScreensave( int *pScreensave )
{
	if ( pScreensave == 0 )
		return PfeifferGaugeDriver_Query | ES_NotAvailable;

	string response;
	if ( const DWORD error = Query( "DCS", &response ) )
		return error;

	try
	{
		*pScreensave = stoi( response );
	}
	catch ( ... )
	{
		return PfeifferGaugeDriver_Query | EH_InvalidResponse;
	}

	return EC_OK;
}


DWORD CMaxiGaugeDriver::CollectDeviceInfo( vector<string> *pLines )
{
	if ( pLines == 0 )
		return PfeifferGaugeDriver_CollectInfo | ES_NotAvailable;

	pLines->clear();
	vector<int> values_int;
	vector<string> values_string;

	if ( GetSensorOnOff( &values_int ) == EC_OK )
		pLines->push_back( "MaxiGauge Sensor on/off: " + _FormatIntVector( values_int ) );
	if ( GetFilter( &values_int ) == EC_OK )
		pLines->push_back( "MaxiGauge Filter: " + _FormatIntVector( values_int ) );
	if ( GetOfc( &values_int ) == EC_OK )
		pLines->push_back( "MaxiGauge OFC: " + _FormatIntVector( values_int ) );
	if ( GetFsr( &values_int ) == EC_OK )
		pLines->push_back( "MaxiGauge FSR: " + _FormatIntVector( values_int ) );
	if ( GetDegas( &values_int ) == EC_OK )
		pLines->push_back( "MaxiGauge DGS: " + _FormatIntVector( values_int ) );
	if ( GetChannelNames( &values_string ) == EC_OK )
		pLines->push_back( "MaxiGauge Names: " + _FormatStringVector( values_string ) );

	int single_value = 0;
	if ( GetDigits( &single_value ) == EC_OK )
	{
		stringstream stream;
		stream << "MaxiGauge Digits: " << single_value;
		pLines->push_back( stream.str() );
	}
	if ( GetContrast( &single_value ) == EC_OK )
	{
		stringstream stream;
		stream << "MaxiGauge Contrast: " << single_value;
		pLines->push_back( stream.str() );
	}
	if ( GetScreensave( &single_value ) == EC_OK )
	{
		stringstream stream;
		stream << "MaxiGauge Screensave: " << single_value;
		pLines->push_back( stream.str() );
	}

	return EC_OK;
}


DWORD CMaxiGaugeDriver::ActivateAndVerify( const BYTE i_Channel, vector<string> *pLines )
{
	if ( pLines == 0 )
		return PfeifferGaugeDriver_CollectInfo | ES_NotAvailable;

	pLines->clear();

	vector<int> before_flags;
	if ( GetSensorOnOff( &before_flags ) == EC_OK )
		pLines->push_back( "SEN before=" + _FormatIntVector( before_flags ) );

	const DWORD set_error = SetSensorState( i_Channel, true );
	if ( set_error != EC_OK )
		pLines->push_back( "Activation failed: " + GetErrorText( set_error ) );

	this_thread::sleep_for( chrono::milliseconds( 400 ) );

	vector<int> after_flags;
	if ( GetSensorOnOff( &after_flags ) == EC_OK )
		pLines->push_back( "SEN after=" + _FormatIntVector( after_flags ) );

	PressureChannelReading reading;
	if ( ReadSingleChannel( i_Channel, &reading ) == EC_OK )
	{
		stringstream stream;
		stream.setf( ios::scientific );
		stream << "PR" << static_cast<unsigned>( i_Channel ) << ": " << reading.nStatusCode << ", " << reading.dPressure;
		pLines->push_back( stream.str() );
	}

	return EC_OK;
}


DWORD CMaxiGaugeDriver::FactoryReset()
{
	return Write( "SAV,1" );
}


DWORD CMaxiGaugeDriver::SetUnit( const int i_UnitCode )
{
	stringstream stream;
	stream << "UNI," << i_UnitCode;
	return Write( stream.str() );
}


DWORD CMaxiGaugeDriver::SetSensorState( const BYTE i_Channel, const bool i_TurnOn )
{
	if ( (i_Channel < 1) || (i_Channel > 6) )
		return PfeifferGaugeDriver_Write | ES_OutOfRange;

	vector<int> current;
	if ( const DWORD error = GetSensorOnOff( &current ) )
		return error;

	if ( i_TurnOn && (current[i_Channel - 1] == 2) )
		return EC_OK;
	if ( (!i_TurnOn) && (current[i_Channel - 1] == 1) )
		return EC_OK;

	vector<int> values( 6, 0 );
	values[i_Channel - 1] = i_TurnOn ? 2 : 1;

	stringstream command;
	command << "SEN";
	for ( size_t i = 0; i < values.size(); i++ )
			command << "," << values[i];
	return Write( command.str() );
}


DWORD CMaxiGaugeDriver::SetDegas( const BYTE i_Channel, const bool i_On )
{
	if ( (i_Channel < 4) || (i_Channel > 6) )
		return PfeifferGaugeDriver_Write | ES_OutOfRange;

	vector<int> values( 6, 0 );
	values[i_Channel - 1] = i_On ? 1 : 0;

	stringstream command;
	command << "DGS";
	for ( size_t i = 0; i < values.size(); i++ )
		command << "," << values[i];
	return Write( command.str() );
}


DWORD CMaxiGaugeDriver::SetFilter( const BYTE i_Channel, const int i_Value )
{
	if ( (i_Channel < 1) || (i_Channel > 6) )
		return PfeifferGaugeDriver_Write | ES_OutOfRange;

	vector<int> values( 6, 0 );
	values[i_Channel - 1] = i_Value;

	stringstream command;
	command << "FIL";
	for ( size_t i = 0; i < values.size(); i++ )
		command << "," << values[i];
	return Write( command.str() );
}


DWORD CMaxiGaugeDriver::SetCalibration( const BYTE i_Channel, const double i_Value )
{
	if ( (i_Channel < 1) || (i_Channel > 6) )
		return PfeifferGaugeDriver_Write | ES_OutOfRange;

	stringstream command;
	command.setf( ios::fixed );
	command << "CA" << static_cast<unsigned>( i_Channel ) << "," << setprecision( 3 ) << i_Value;
	return Write( command.str() );
}


DWORD CMaxiGaugeDriver::SetFsr( const BYTE i_Channel, const int i_Value )
{
	if ( (i_Channel < 1) || (i_Channel > 6) )
		return PfeifferGaugeDriver_Write | ES_OutOfRange;

	vector<int> values( 6, 3 );
	values[i_Channel - 1] = i_Value;

	stringstream command;
	command << "FSR";
	for ( size_t i = 0; i < values.size(); i++ )
		command << "," << values[i];
	return Write( command.str() );
}


DWORD CMaxiGaugeDriver::SetOfc( const BYTE i_Channel, const int i_Value )
{
	if ( (i_Channel < 1) || (i_Channel > 6) )
		return PfeifferGaugeDriver_Write | ES_OutOfRange;

	vector<int> values( 6, 0 );
	values[i_Channel - 1] = i_Value;

	stringstream command;
	command << "OFC";
	for ( size_t i = 0; i < values.size(); i++ )
		command << "," << values[i];
	return Write( command.str() );
}


DWORD CMaxiGaugeDriver::SetChannelName( const BYTE i_Channel, const string& i_Name )
{
	if ( (i_Channel < 1) || (i_Channel > 6) )
		return PfeifferGaugeDriver_Write | ES_OutOfRange;

	vector<string> names;
	if ( const DWORD error = GetChannelNames( &names ) )
		return error;

	names[i_Channel - 1] = _SanitizeChannelName( i_Name );
	stringstream command;
	command << "CID";
	for ( size_t i = 0; i < names.size(); i++ )
		command << "," << names[i];

	return Write( command.str() );
}


DWORD CMaxiGaugeDriver::SetDigits( const int i_Value )
{
	stringstream command;
	command << "DCD," << i_Value;
	return Write( command.str() );
}


DWORD CMaxiGaugeDriver::SetContrast( const int i_Value )
{
	stringstream command;
	command << "DCC," << i_Value;
	return Write( command.str() );
}


DWORD CMaxiGaugeDriver::SetScreensave( const int i_Value )
{
	stringstream command;
	command << "DCS," << i_Value;
	return Write( command.str() );
}


bool CMaxiGaugeDriver::GetClassAndMethod( const DWORD i_MethodId, string *pClassAndMethodName )
{
	return CPfeifferGaugeDriver::GetClassAndMethod( i_MethodId, pClassAndMethodName );
}
