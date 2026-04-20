///////////////////////////////////////////////////////////////////////////////////////////////////
//
// PressureLoggerAppEngine.cpp: implementation of the CPressureLoggerAppEngine class.
//
///////////////////////////////////////////////////////////////////////////////////////////////////


#include "PressureLoggerAppEngine.h"

#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <limits>
#include <sstream>

using namespace std;


namespace
{
	const size_t PRESSURE_LOGGER_HISTORY_LIMIT = 1200;
	const size_t PRESSURE_LOGGER_LOG_LIMIT = 400;
	const char *PRESSURE_LOGGER_CONFIG_FILENAME = ".cdt_pressure_logger_config.ini";


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


	string _TimestampNow()
	{
		const auto current_time = chrono::system_clock::now();
		time_t raw_time = chrono::system_clock::to_time_t( current_time );
		struct tm time_data;

		#ifdef MS_WIN
			localtime_s( &time_data, &raw_time );
		#else
			localtime_r( &raw_time, &time_data );
		#endif

		stringstream stream;
		stream << put_time( &time_data, "%Y-%m-%d %H:%M:%S" );
		return stream.str();
	}
}


CPressureLoggerAppEngine::CPressureLoggerAppEngine()
{
	bStopRequested = false;
	LastDeviceType = PressureLoggerDevice_TPG262;
	EnsureDefaultDisplayNames();
	LoadUserConfig();

	lock_guard<mutex> lock( StateMutex );
	State.Setup.DeviceType = LastDeviceType;
	UpdateActiveDisplayLabelsLocked();
}


CPressureLoggerAppEngine::~CPressureLoggerAppEngine()
{
	Disconnect();
}


DWORD CPressureLoggerAppEngine::Connect( const PressureLoggerConnectionSetup i_Setup )
{
	if ( i_Setup.sPort.length() == 0 )
		return PressureLoggerAppEngine_Connect | ES_OutOfRange;

	if ( const DWORD error = Disconnect() )
		return error;

	if ( i_Setup.DeviceType == PressureLoggerDevice_TPG262 )
		pDriver.reset( new CTPG262Driver );
	else
		pDriver.reset( new CMaxiGaugeDriver );

	if ( const DWORD error = pDriver->Init( i_Setup ) )
	{
		pDriver.reset();
		return error;
	}

	Setup = i_Setup;
	SetLastSelection( Setup.DeviceType, Setup.sPort );
	MonitorStartSteady = chrono::steady_clock::now();

	{
		lock_guard<mutex> lock( StateMutex );
		State = PressureLoggerStateSnapshot();
		State.bConnected = true;
		State.bMonitoring = false;
		State.bFaulted = false;
		State.Setup = Setup;
		State.sLastErrorText = "";
		UpdateActiveDisplayLabelsLocked();
	}

	AppendLog( "[INFO] Connected to " + Setup.sPort + " using " + pDriver->GetDeviceName() );
	{
		stringstream stream;
		stream << "[INFO] Setup: device=" << pDriver->GetDeviceName()
			   << ", poll=" << fixed << setprecision( 3 ) << Setup.dPollingSeconds << " s"
			   << ", long_term=" << (Setup.bTPG262LongTermMode ? "on" : "off");
		AppendLog( stream.str() );
	}

	if ( const DWORD error = StartMonitorThread() )
	{
		pDriver->Close();
		pDriver.reset();
		return error;
	}

	return EC_OK;
}


DWORD CPressureLoggerAppEngine::Disconnect()
{
	StopLogging();
	StopMonitorThread();

	if ( pDriver.get() != 0 )
	{
		pDriver->Close();
		pDriver.reset();
	}

	lock_guard<mutex> lock( StateMutex );
	State.bConnected = false;
	State.bMonitoring = false;
	State.bFaulted = false;
	State.LastChannels.clear();
	State.Setup = PressureLoggerConnectionSetup();
	State.Setup.DeviceType = LastDeviceType;
	State.sLastErrorText = "";
	UpdateActiveDisplayLabelsLocked();
	return EC_OK;
}


bool CPressureLoggerAppEngine::GetConnected() const
{
	lock_guard<mutex> lock( StateMutex );
	return State.bConnected;
}


struct PressureLoggerConnectionSetup CPressureLoggerAppEngine::GetConnectionSetup() const
{
	lock_guard<mutex> lock( StateMutex );
	return State.Setup;
}


void CPressureLoggerAppEngine::GetStateSnapshot( PressureLoggerStateSnapshot *pSnapshot ) const
{
	if ( pSnapshot == 0 )
		return;

	lock_guard<mutex> lock( StateMutex );
	*pSnapshot = State;
}


string CPressureLoggerAppEngine::GetLastErrorText( const DWORD i_ErrorCode )
{
	return GetErrorText( i_ErrorCode );
}


DWORD CPressureLoggerAppEngine::LoadUserConfig()
{
	EnsureDefaultDisplayNames();

	ifstream input( GetConfigPath().c_str() );
	if ( !input.is_open() )
		return EC_OK;

	string line;
	while ( getline( input, line ) )
	{
		const size_t separator = line.find( '=' );
		if ( separator == string::npos )
			continue;

		const string key = _Trim( line.substr( 0, separator ) );
		const string value = _Trim( line.substr( separator + 1 ) );

		if ( key == "last_device" )
			LastDeviceType = (value == "MaxiGauge") ? PressureLoggerDevice_MaxiGauge : PressureLoggerDevice_TPG262;
		else if ( key == "last_port" )
			sLastPort = value;
		else if ( key.rfind( "tpg262_name_", 0 ) == 0 )
		{
			try
			{
				const int index = stoi( key.substr( 12 ) );
				if ( (index >= 1) && (index <= static_cast<int>( Tpg262DisplayNames.size() )) )
					Tpg262DisplayNames[index - 1] = NormalizeDisplayChannelName( value, static_cast<BYTE>( index ) );
			}
			catch ( ... )
			{
			}
		}
		else if ( key.rfind( "maxigauge_name_", 0 ) == 0 )
		{
			try
			{
				const int index = stoi( key.substr( 15 ) );
				if ( (index >= 1) && (index <= static_cast<int>( MaxiGaugeDisplayNames.size() )) )
					MaxiGaugeDisplayNames[index - 1] = NormalizeDisplayChannelName( value, static_cast<BYTE>( index ) );
			}
			catch ( ... )
			{
			}
		}
	}

	return EC_OK;
}


DWORD CPressureLoggerAppEngine::SaveUserConfig() const
{
	ofstream output( GetConfigPath().c_str(), ios::out | ios::trunc );
	if ( !output.is_open() )
		return ES_Failure;

	output << "last_device=" << ((LastDeviceType == PressureLoggerDevice_MaxiGauge) ? "MaxiGauge" : "TPG262") << "\n";
	output << "last_port=" << sLastPort << "\n";
	for ( size_t i = 0; i < Tpg262DisplayNames.size(); i++ )
		output << "tpg262_name_" << (i + 1) << "=" << Tpg262DisplayNames[i] << "\n";
	for ( size_t i = 0; i < MaxiGaugeDisplayNames.size(); i++ )
		output << "maxigauge_name_" << (i + 1) << "=" << MaxiGaugeDisplayNames[i] << "\n";

	return EC_OK;
}


string CPressureLoggerAppEngine::GetConfigPath() const
{
	const char *home = getenv( "HOME" );
	#ifdef MS_WIN
		if ( home == 0 )
			home = getenv( "USERPROFILE" );
	#endif

	if ( (home != 0) && (strlen( home ) > 0) )
		return (filesystem::path( home ) / PRESSURE_LOGGER_CONFIG_FILENAME).string();

	return (filesystem::current_path() / PRESSURE_LOGGER_CONFIG_FILENAME).string();
}


string CPressureLoggerAppEngine::GetLastPort() const
{
	return sLastPort;
}


enum PressureLoggerDeviceType CPressureLoggerAppEngine::GetLastDeviceType() const
{
	return LastDeviceType;
}


void CPressureLoggerAppEngine::SetLastSelection( const enum PressureLoggerDeviceType i_DeviceType, const string& i_Port )
{
	LastDeviceType = i_DeviceType;
	sLastPort = i_Port;
	SaveUserConfig();
}


vector<string> CPressureLoggerAppEngine::GetDisplayChannelNames( const enum PressureLoggerDeviceType i_DeviceType ) const
{
	return (i_DeviceType == PressureLoggerDevice_MaxiGauge) ? MaxiGaugeDisplayNames : Tpg262DisplayNames;
}


string CPressureLoggerAppEngine::GetDisplayChannelName( const enum PressureLoggerDeviceType i_DeviceType, const BYTE i_Channel ) const
{
	const vector<string> names = GetDisplayChannelNames( i_DeviceType );
	if ( (i_Channel >= 1) && (i_Channel <= names.size()) )
		return names[i_Channel - 1];

	stringstream stream;
	stream << "Kanal " << static_cast<unsigned>( i_Channel );
	return stream.str();
}


string CPressureLoggerAppEngine::FormatCombinedChannelLabel( const enum PressureLoggerDeviceType i_DeviceType, const BYTE i_Channel ) const
{
	stringstream stream;
	stream << "Kanal " << static_cast<unsigned>( i_Channel ) << " – " << GetDisplayChannelName( i_DeviceType, i_Channel );
	return stream.str();
}


double CPressureLoggerAppEngine::StatusPlotValue( const PressureLoggerStateSnapshot& i_Snapshot, const BYTE i_Channel, const int i_StatusCode, const double i_Value ) const
{
	if ( (i_StatusCode == 5) || (i_StatusCode == 6) )
		return numeric_limits<double>::quiet_NaN();
	if ( std::isfinite( i_Value ) && (i_Value > 0.0) )
		return i_Value;

	double last_positive = numeric_limits<double>::quiet_NaN();
	for ( size_t sample_index = i_Snapshot.History.size(); sample_index > 0; sample_index-- )
	{
		const PressureSample& sample = i_Snapshot.History[sample_index - 1];
		for ( size_t reading_index = 0; reading_index < sample.ChannelValues.size(); reading_index++ )
		{
			const PressureChannelReading& reading = sample.ChannelValues[reading_index];
			if ( (reading.byChannel == i_Channel) && std::isfinite( reading.dPressure ) && (reading.dPressure > 0.0) )
			{
				last_positive = reading.dPressure;
				break;
			}
		}
		if ( std::isfinite( last_positive ) )
			break;
	}

	if ( i_StatusCode == 1 )
		return std::isfinite( last_positive ) ? max( last_positive / 3.0, 1e-12 ) : 1e-12;
	if ( i_StatusCode == 2 )
		return std::isfinite( last_positive ) ? max( last_positive, 1e-12 ) : 1e3;

	return numeric_limits<double>::quiet_NaN();
}


void CPressureLoggerAppEngine::BuildPlotSeries( const PressureLoggerStateSnapshot& i_Snapshot, const BYTE i_Channel, vector<double> *pTimes, vector<double> *pValues ) const
{
	if ( (pTimes == 0) || (pValues == 0) )
		return;

	pTimes->clear();
	pValues->clear();

	for ( size_t sample_index = 0; sample_index < i_Snapshot.History.size(); sample_index++ )
	{
		const PressureSample& sample = i_Snapshot.History[sample_index];
		for ( size_t reading_index = 0; reading_index < sample.ChannelValues.size(); reading_index++ )
		{
			const PressureChannelReading& reading = sample.ChannelValues[reading_index];
			if ( reading.byChannel != i_Channel )
				continue;

			pTimes->push_back( sample.dSecondsSinceStart );
			pValues->push_back( StatusPlotValue( i_Snapshot, i_Channel, reading.nStatusCode, reading.dPressure ) );
			break;
		}
	}
}


string CPressureLoggerAppEngine::MakeDefaultCsvPath( const enum PressureLoggerDeviceType i_DeviceType ) const
{
	const auto current_time = chrono::system_clock::now();
	time_t raw_time = chrono::system_clock::to_time_t( current_time );
	struct tm time_data;

	#ifdef MS_WIN
		localtime_s( &time_data, &raw_time );
	#else
		localtime_r( &raw_time, &time_data );
	#endif

	string slug = (i_DeviceType == PressureLoggerDevice_MaxiGauge) ? "maxigauge" : "tpg_262";
	const string directory = filesystem::current_path().string();

	stringstream stream;
	stream << directory;
	if ( (!directory.empty()) && (directory.back() != filesystem::path::preferred_separator) )
		stream << filesystem::path::preferred_separator;
	stream << slug << "_log_" << put_time( &time_data, "%Y%m%d_%H%M%S" ) << ".csv";
	return stream.str();
}


string CPressureLoggerAppEngine::GetHelpText( const string& i_Key ) const
{
	string filename;
	if ( i_Key == "raw" ) filename = "rohkommandos_pfeiffer_vollstaendig.txt";
	else if ( i_Key == "diagnose" ) filename = "diagnose_lesen_hilfe_vollstaendig.txt";
	else if ( i_Key == "unit" ) filename = "hilfe_einheit.txt";
	else if ( i_Key == "sensor" ) filename = "hilfe_gauge_ein_aus.txt";
	else if ( i_Key == "read_now" ) filename = "hilfe_messwert_jetzt_lesen.txt";
	else if ( i_Key == "degas" ) filename = "hilfe_degas.txt";
	else if ( i_Key == "activate" ) filename = "hilfe_gauge_aktivieren_pruefen.txt";
	else if ( i_Key == "filter" ) filename = "hilfe_filter.txt";
	else if ( i_Key == "calibration" ) filename = "hilfe_kalibrierfaktor.txt";
	else if ( i_Key == "fsr" ) filename = "hilfe_fsr.txt";
	else if ( i_Key == "ofc" ) filename = "hilfe_ofc.txt";
	else if ( i_Key == "channel_name" ) filename = "hilfe_kanalname.txt";
	else if ( i_Key == "digits" ) filename = "hilfe_digits.txt";
	else if ( i_Key == "contrast" ) filename = "hilfe_contrast.txt";
	else if ( i_Key == "screensave" ) filename = "hilfe_screensave.txt";
	else return "Kein Hilfetext fuer diesen Bereich vorhanden.";

	const filesystem::path path = filesystem::path( GetTextsDirectory() ) / filename;
	ifstream input( path.c_str() );
	if ( !input.is_open() )
		return "Die Hilfedatei konnte nicht gelesen werden:\n" + path.string();

	stringstream content;
	content << input.rdbuf();
	return content.str();
}


string CPressureLoggerAppEngine::FormatLatestValues( const PressureLoggerStateSnapshot& i_Snapshot ) const
{
	stringstream stream;
	stream.setf( ios::scientific );

	stream << "Connection: " << (i_Snapshot.bConnected ? "connected" : "disconnected") << "\n";
	stream << "Monitoring: " << (i_Snapshot.bMonitoring ? "running" : "stopped") << "\n";
	stream << "Logging: " << (i_Snapshot.bLogging ? "active" : "off") << "\n";
	stream << "Fault: " << (i_Snapshot.bFaulted ? "yes" : "no") << "\n";
	stream << "Samples: " << i_Snapshot.dwSampleCount << "\n";
	if ( !i_Snapshot.sCsvPath.empty() )
		stream << "CSV: " << i_Snapshot.sCsvPath << "\n";
	if ( !i_Snapshot.sLastErrorText.empty() )
		stream << "Last error: " << i_Snapshot.sLastErrorText << "\n";

	if ( i_Snapshot.LastChannels.empty() )
	{
		stream << "\nNo values available yet.";
		return stream.str();
	}

	stream << "\nLatest values\n";
	for ( size_t i = 0; i < i_Snapshot.LastChannels.size(); i++ )
	{
		const PressureChannelReading& reading = i_Snapshot.LastChannels[i];
		const string label = (reading.byChannel >= 1 && reading.byChannel <= i_Snapshot.CombinedChannelLabels.size())
			? i_Snapshot.CombinedChannelLabels[reading.byChannel - 1]
			: FormatCombinedChannelLabel( i_Snapshot.Setup.DeviceType, reading.byChannel );
		stream << label << "  ";
		stream << reading.sStatusText << "  ";
		stream << setprecision( 4 ) << reading.dPressure << "\n";
	}

	return stream.str();
}


string CPressureLoggerAppEngine::FormatRecentSamples( const PressureLoggerStateSnapshot& i_Snapshot, const size_t i_MaxSamples ) const
{
	stringstream stream;
	stream.setf( ios::scientific );

	if ( i_Snapshot.History.empty() )
	{
		stream << "No sample history available.";
		return stream.str();
	}

	const size_t begin_index = (i_Snapshot.History.size() > i_MaxSamples) ? (i_Snapshot.History.size() - i_MaxSamples) : 0;
	for ( size_t sample_index = begin_index; sample_index < i_Snapshot.History.size(); sample_index++ )
	{
		const PressureSample& sample = i_Snapshot.History[sample_index];
		stream << fixed << setprecision( 2 ) << sample.dSecondsSinceStart << " s";

		for ( size_t channel_index = 0; channel_index < sample.ChannelValues.size(); channel_index++ )
		{
			const PressureChannelReading& reading = sample.ChannelValues[channel_index];
			const string label = (reading.byChannel >= 1 && reading.byChannel <= i_Snapshot.CombinedChannelLabels.size())
				? i_Snapshot.CombinedChannelLabels[reading.byChannel - 1]
				: FormatCombinedChannelLabel( i_Snapshot.Setup.DeviceType, reading.byChannel );
			stream << " | " << label << ":";
			stream << setprecision( 3 ) << scientific << reading.dPressure;
			stream << " (" << reading.sStatusText << ")";
		}
		stream << "\n";
	}

	return stream.str();
}


DWORD CPressureLoggerAppEngine::CollectSuggestedPorts( vector<string> *pPorts ) const
{
	CSerialPort port;
	const DWORD error = port.CollectSuggestedPorts( pPorts );
	return (error == EC_OK) ? EC_OK : (PressureLoggerAppEngine_CollectPorts | error);
}


DWORD CPressureLoggerAppEngine::StartLogging( const string& i_CsvPath )
{
	if ( pDriver.get() == 0 )
		return PressureLoggerAppEngine_StartLogging | ES_NotInitialized;

	string csv_path = i_CsvPath;
	if ( csv_path.length() == 0 )
		csv_path = MakeDefaultCsvPath( Setup.DeviceType );

	if ( CsvFile.is_open() )
		StopLogging();

	try
	{
		const filesystem::path path( csv_path );
		if ( path.has_parent_path() )
			filesystem::create_directories( path.parent_path() );
	}
	catch ( ... )
	{
		return PressureLoggerAppEngine_StartLogging | ES_Failure;
	}

	CsvFile.open( csv_path.c_str(), ios::out | ios::trunc );
	if ( !CsvFile.is_open() )
		return PressureLoggerAppEngine_StartLogging | ES_Failure;

	LoggingStartSystem = chrono::system_clock::now();
	{
		lock_guard<mutex> lock( StateMutex );
		State.bLogging = true;
		State.sCsvPath = csv_path;
		State.bFaulted = false;
		State.sLastErrorText = "";
	}

	WriteCsvHeader();
	AppendLog( "[INFO] Logging started: " + csv_path );
	return EC_OK;
}


DWORD CPressureLoggerAppEngine::StopLogging()
{
	const bool was_logging = CsvFile.is_open();
	if ( CsvFile.is_open() )
		CsvFile.close();

	{
		lock_guard<mutex> lock( StateMutex );
		State.bLogging = false;
		State.sCsvPath = "";
	}

	if ( was_logging )
		AppendLog( "[INFO] Logging stopped." );
	return EC_OK;
}


DWORD CPressureLoggerAppEngine::ClearHistory()
{
	{
		lock_guard<mutex> lock( StateMutex );
		State.dwSampleCount = 0;
		State.LastChannels.clear();
		State.History.clear();
	}
	AppendLog( "[INFO] Cleared plot and sample history." );
	return EC_OK;
}


DWORD CPressureLoggerAppEngine::ResetMeasurementTimeline()
{
	MonitorStartSteady = chrono::steady_clock::now();
	return ClearHistory();
}


DWORD CPressureLoggerAppEngine::ExecuteRawCommand( const string& i_Command )
{
	string result_line;
	const DWORD error = ExecuteDriverCommand(
		PressureLoggerAppEngine_Command,
		"",
		true,
		[&]( CPfeifferGaugeDriver *pGaugeDriver )
		{
			const bool write_only = (!i_Command.empty() && (i_Command[0] == '!'));
			string response;
			const DWORD current_error = pGaugeDriver->ExecuteRaw( i_Command, write_only, &response );
			if ( current_error != EC_OK )
				return current_error;

			string command = i_Command;
			if ( write_only && (!command.empty()) && (command[0] == '!') )
				command = command.substr( 1 );

			result_line = write_only ? ("[RAW] " + command + " -> ACK") : ("[RAW] " + command + " -> " + response);
			return EC_OK;
		}
	);

	if ( (error == EC_OK) && (!result_line.empty()) )
		AppendLog( result_line );
	return error;
}


DWORD CPressureLoggerAppEngine::ReadSingleChannelNow( const BYTE i_Channel )
{
	string result_line;
	const DWORD error = ExecuteDriverCommand(
		PressureLoggerAppEngine_Command,
		"",
		true,
		[&]( CPfeifferGaugeDriver *pGaugeDriver )
		{
			PressureChannelReading reading;
			const DWORD current_error = pGaugeDriver->ReadSingleChannel( i_Channel, &reading );
			if ( current_error != EC_OK )
				return current_error;

			stringstream stream;
			stream.setf( ios::scientific );
			stream << "[INFO] PR" << static_cast<unsigned>( i_Channel ) << " -> " << reading.nStatusCode << ", " << reading.dPressure;
			result_line = stream.str();
			return EC_OK;
		}
	);

	if ( (error == EC_OK) && (!result_line.empty()) )
		AppendLog( result_line );
	return error;
}


DWORD CPressureLoggerAppEngine::ReadDeviceInfo()
{
	vector<string> lines;
	const DWORD error = ExecuteDriverCommand(
		PressureLoggerAppEngine_Command,
		"",
		true,
		[&]( CPfeifferGaugeDriver *pGaugeDriver )
		{
			return pGaugeDriver->CollectDeviceInfo( &lines );
		}
	);

	if ( error == EC_OK )
		for ( size_t i = 0; i < lines.size(); i++ )
			AppendLog( "[INFO] " + lines[i] );

	return error;
}


DWORD CPressureLoggerAppEngine::ActivateAndVerify( const BYTE i_Channel )
{
	vector<string> lines;
	const DWORD error = ExecuteDriverCommand(
		PressureLoggerAppEngine_Command,
		"",
		true,
		[&]( CPfeifferGaugeDriver *pGaugeDriver )
		{
			return pGaugeDriver->ActivateAndVerify( i_Channel, &lines );
		}
	);

	if ( error == EC_OK )
		for ( size_t i = 0; i < lines.size(); i++ )
			AppendLog( "[INFO] " + lines[i] );

	return error;
}


DWORD CPressureLoggerAppEngine::FactoryResetDevice()
{
	return ExecuteDriverCommand( PressureLoggerAppEngine_Command,
								 "[INFO] Factory reset requested.",
								 true,
								 []( CPfeifferGaugeDriver *pGaugeDriver ) { return pGaugeDriver->FactoryReset(); } );
}


DWORD CPressureLoggerAppEngine::SetUnit( const int i_UnitCode )
{
	return ExecuteDriverCommand( PressureLoggerAppEngine_Command,
								 "[INFO] Unit changed.",
								 true,
								 [i_UnitCode]( CPfeifferGaugeDriver *pGaugeDriver ) { return pGaugeDriver->SetUnit( i_UnitCode ); } );
}


DWORD CPressureLoggerAppEngine::SetSensorState( const BYTE i_Channel, const bool i_TurnOn )
{
	return ExecuteDriverCommand( PressureLoggerAppEngine_Command,
								 i_TurnOn ? "[INFO] Gauge enabled." : "[INFO] Gauge disabled.",
								 true,
								 [i_Channel, i_TurnOn]( CPfeifferGaugeDriver *pGaugeDriver ) { return pGaugeDriver->SetSensorState( i_Channel, i_TurnOn ); } );
}


DWORD CPressureLoggerAppEngine::SetDegas( const BYTE i_Channel, const bool i_On )
{
	return ExecuteDriverCommand( PressureLoggerAppEngine_Command,
								 i_On ? "[INFO] Degas enabled." : "[INFO] Degas disabled.",
								 true,
								 [i_Channel, i_On]( CPfeifferGaugeDriver *pGaugeDriver ) { return pGaugeDriver->SetDegas( i_Channel, i_On ); } );
}


DWORD CPressureLoggerAppEngine::SetFilter( const BYTE i_Channel, const int i_Value )
{
	return ExecuteDriverCommand( PressureLoggerAppEngine_Command,
								 "[INFO] Filter changed.",
								 true,
								 [i_Channel, i_Value]( CPfeifferGaugeDriver *pGaugeDriver ) { return pGaugeDriver->SetFilter( i_Channel, i_Value ); } );
}


DWORD CPressureLoggerAppEngine::SetCalibration( const BYTE i_Channel, const double i_Value )
{
	return ExecuteDriverCommand( PressureLoggerAppEngine_Command,
								 "[INFO] Calibration factor changed.",
								 true,
								 [i_Channel, i_Value]( CPfeifferGaugeDriver *pGaugeDriver ) { return pGaugeDriver->SetCalibration( i_Channel, i_Value ); } );
}


DWORD CPressureLoggerAppEngine::SetFsr( const BYTE i_Channel, const int i_Value )
{
	return ExecuteDriverCommand( PressureLoggerAppEngine_Command,
								 "[INFO] FSR changed.",
								 true,
								 [i_Channel, i_Value]( CPfeifferGaugeDriver *pGaugeDriver ) { return pGaugeDriver->SetFsr( i_Channel, i_Value ); } );
}


DWORD CPressureLoggerAppEngine::SetOfc( const BYTE i_Channel, const int i_Value )
{
	return ExecuteDriverCommand( PressureLoggerAppEngine_Command,
								 "[INFO] OFC changed.",
								 true,
								 [i_Channel, i_Value]( CPfeifferGaugeDriver *pGaugeDriver ) { return pGaugeDriver->SetOfc( i_Channel, i_Value ); } );
}


DWORD CPressureLoggerAppEngine::SetDisplayChannelName( const enum PressureLoggerDeviceType i_DeviceType, const BYTE i_Channel, const string& i_Name )
{
	return StoreDisplayChannelName( i_DeviceType, i_Channel, i_Name, true );
}


DWORD CPressureLoggerAppEngine::SetChannelName( const BYTE i_Channel, const string& i_Name )
{
	if ( pDriver.get() == 0 )
		return PressureLoggerAppEngine_Command | ES_NotInitialized;

	const enum PressureLoggerDeviceType device_type = Setup.DeviceType;
	const string normalized_name = NormalizeDisplayChannelName( i_Name, i_Channel );
	const DWORD error = ExecuteDriverCommand(
		PressureLoggerAppEngine_Command,
		string( "[INFO] Geraetename gesetzt: Kanal " ) + to_string( static_cast<unsigned>( i_Channel ) ) + " -> " + normalized_name,
		true,
		[i_Channel, normalized_name]( CPfeifferGaugeDriver *pGaugeDriver ) { return pGaugeDriver->SetChannelName( i_Channel, normalized_name ); }
	);
	if ( error != EC_OK )
		return error;

	return StoreDisplayChannelName( device_type, i_Channel, normalized_name, false );
}


DWORD CPressureLoggerAppEngine::SetDigits( const int i_Value )
{
	return ExecuteDriverCommand( PressureLoggerAppEngine_Command,
								 "[INFO] Digits changed.",
								 true,
								 [i_Value]( CPfeifferGaugeDriver *pGaugeDriver ) { return pGaugeDriver->SetDigits( i_Value ); } );
}


DWORD CPressureLoggerAppEngine::SetContrast( const int i_Value )
{
	return ExecuteDriverCommand( PressureLoggerAppEngine_Command,
								 "[INFO] Contrast changed.",
								 true,
								 [i_Value]( CPfeifferGaugeDriver *pGaugeDriver ) { return pGaugeDriver->SetContrast( i_Value ); } );
}


DWORD CPressureLoggerAppEngine::SetScreensave( const int i_Value )
{
	return ExecuteDriverCommand( PressureLoggerAppEngine_Command,
								 "[INFO] Screensave changed.",
								 true,
								 [i_Value]( CPfeifferGaugeDriver *pGaugeDriver ) { return pGaugeDriver->SetScreensave( i_Value ); } );
}


bool CPressureLoggerAppEngine::GetClassAndMethod( const DWORD i_MethodId, string *pClassAndMethodName )
{
	if ( pClassAndMethodName == 0 )
		return false;

	if ( (i_MethodId & EC_Mask) == EC_CPressureLoggerAppEngine )
	{
		switch ( i_MethodId )
		{
			case PressureLoggerAppEngine_Connect:		*pClassAndMethodName = "PressureLoggerAppEngine.Connect(): "; break;
			case PressureLoggerAppEngine_Disconnect:	*pClassAndMethodName = "PressureLoggerAppEngine.Disconnect(): "; break;
			case PressureLoggerAppEngine_StartLogging:	*pClassAndMethodName = "PressureLoggerAppEngine.StartLogging(): "; break;
			case PressureLoggerAppEngine_StopLogging:	*pClassAndMethodName = "PressureLoggerAppEngine.StopLogging(): "; break;
			case PressureLoggerAppEngine_ClearHistory:	*pClassAndMethodName = "PressureLoggerAppEngine.ClearHistory(): "; break;
			case PressureLoggerAppEngine_GetState:		*pClassAndMethodName = "PressureLoggerAppEngine.GetStateSnapshot(): "; break;
			case PressureLoggerAppEngine_CollectPorts:	*pClassAndMethodName = "PressureLoggerAppEngine.CollectSuggestedPorts(): "; break;
			case PressureLoggerAppEngine_Command:		*pClassAndMethodName = "PressureLoggerAppEngine.Command(): "; break;
			default:									*pClassAndMethodName = "PressureLoggerAppEngine.UnknownMethod(): "; break;
		}
		return true;
	}

	return CErrorLib::GetClassAndMethod( i_MethodId, pClassAndMethodName );
}


DWORD CPressureLoggerAppEngine::StartMonitorThread()
{
	if ( pDriver.get() == 0 )
		return PressureLoggerAppEngine_Connect | ES_NotInitialized;

	if ( MonitorThread.joinable() )
		MonitorThread.join();

	if ( const DWORD error = pDriver->StartMonitoringSession() )
		return error;

	bStopRequested = false;
	MonitorThread = thread( &CPressureLoggerAppEngine::MonitorLoop, this );

	lock_guard<mutex> lock( StateMutex );
	State.bMonitoring = true;
	State.bFaulted = false;
	State.sLastErrorText = "";
	return EC_OK;
}


DWORD CPressureLoggerAppEngine::StopMonitorThread()
{
	bStopRequested = true;

	if ( pDriver.get() != 0 )
		pDriver->StopMonitoringSession();
	if ( MonitorThread.joinable() )
		MonitorThread.join();

	lock_guard<mutex> lock( StateMutex );
	State.bMonitoring = false;
	return EC_OK;
}


DWORD CPressureLoggerAppEngine::ExecuteDriverCommand( const DWORD i_MethodCode,
													  const string& i_SuccessPrefix,
													  const bool i_RestartMonitoring,
													  const function<DWORD(CPfeifferGaugeDriver*)>& i_Command )
{
	if ( pDriver.get() == 0 )
		return i_MethodCode | ES_NotInitialized;

	bool was_monitoring = false;
	bool was_logging = false;
	{
		lock_guard<mutex> lock( StateMutex );
		was_monitoring = State.bMonitoring;
		was_logging = State.bLogging;
	}

	if ( was_monitoring )
		StopMonitorThread();

	const DWORD error = i_Command( pDriver.get() );
	DWORD restart_error = EC_OK;

	if ( i_RestartMonitoring && was_monitoring )
		restart_error = StartMonitorThread();

	if ( error != EC_OK )
	{
		const string error_text = GetLastErrorText( error );
		AppendLog( "[ERR] " + error_text );
		lock_guard<mutex> lock( StateMutex );
		State.sLastErrorText = error_text;
		return error;
	}
	if ( restart_error != EC_OK )
	{
		const string restart_text = GetLastErrorText( restart_error );
		if ( CsvFile.is_open() )
			CsvFile.close();
		AppendLog( "[ERR] Monitoring restart failed: " + restart_text );
		if ( was_logging )
			AppendLog( "[WARN] Logging stopped due to monitoring restart error." );
		lock_guard<mutex> lock( StateMutex );
		State.bMonitoring = false;
		State.bLogging = false;
		State.bFaulted = true;
		State.sLastErrorText = restart_text;
		return restart_error;
	}

	if ( i_SuccessPrefix.length() != 0 )
		AppendLog( i_SuccessPrefix );
	return EC_OK;
}


void CPressureLoggerAppEngine::MonitorLoop()
{
	while ( !bStopRequested )
	{
		if ( pDriver.get() == 0 )
			break;

		PressureSample sample;
		const DWORD error = pDriver->ReadSample( &sample );
		if ( error != EC_OK )
		{
			if ( bStopRequested )
				break;

			const string error_text = GetLastErrorText( error );
			const bool was_logging = CsvFile.is_open();
			if ( CsvFile.is_open() )
				CsvFile.close();

			AppendLog( "[ERR] " + error_text );
			if ( was_logging )
				AppendLog( "[WARN] Logging stopped due to monitoring error." );

			lock_guard<mutex> lock( StateMutex );
			State.bMonitoring = false;
			State.bLogging = false;
			State.bFaulted = true;
			State.sLastErrorText = error_text;
			return;
		}

		const auto steady_now = chrono::steady_clock::now();
		const auto system_now = chrono::system_clock::now();
		sample.dSecondsSinceStart = chrono::duration<double>( steady_now - MonitorStartSteady ).count();
		sample.dCapturedAtUnixSeconds = chrono::duration<double>( system_now.time_since_epoch() ).count();

		{
			lock_guard<mutex> lock( StateMutex );
			State.bMonitoring = true;
			State.LastChannels = sample.ChannelValues;
			State.History.push_back( sample );
			if ( State.History.size() > PRESSURE_LOGGER_HISTORY_LIMIT )
				State.History.erase( State.History.begin(), State.History.begin() + (State.History.size() - PRESSURE_LOGGER_HISTORY_LIMIT) );
			State.dwSampleCount++;
		}

		WriteCsvRow( sample );

		const double delay_seconds = max( 0.0, pDriver->GetSuggestedLoopDelaySeconds() );
		const auto end_time = chrono::steady_clock::now() + chrono::duration<double>( delay_seconds );
		while ( (!bStopRequested) && (delay_seconds > 0.0) && (chrono::steady_clock::now() < end_time) )
			this_thread::sleep_for( chrono::milliseconds( 25 ) );
	}
}


void CPressureLoggerAppEngine::AppendLog( const string& i_Line )
{
	lock_guard<mutex> lock( StateMutex );
	State.LogLines.push_back( _TimestampNow() + "  " + i_Line );
	if ( State.LogLines.size() > PRESSURE_LOGGER_LOG_LIMIT )
		State.LogLines.erase( State.LogLines.begin(), State.LogLines.begin() + (State.LogLines.size() - PRESSURE_LOGGER_LOG_LIMIT) );
}


void CPressureLoggerAppEngine::WriteCsvHeader()
{
	if ( !CsvFile.is_open() )
		return;

	CsvFile << "t_s";
	const BYTE channel_count = static_cast<BYTE>( ChannelCountForDevice( Setup.DeviceType ) );
	for ( BYTE channel = 1; channel <= channel_count; channel++ )
		CsvFile << ",status_" << static_cast<unsigned>( channel ) << ",value_" << static_cast<unsigned>( channel );
	CsvFile << "\n";
	CsvFile.flush();
}


void CPressureLoggerAppEngine::WriteCsvRow( const PressureSample& i_Sample )
{
	if ( !CsvFile.is_open() )
		return;

	lock_guard<mutex> lock( StateMutex );
	if ( !State.bLogging )
		return;

	const double csv_seconds = chrono::duration<double>( chrono::system_clock::now() - LoggingStartSystem ).count();
	CsvFile.setf( ios::fixed );
	CsvFile << setprecision( 3 ) << csv_seconds;

	const BYTE channel_count = static_cast<BYTE>( ChannelCountForDevice( Setup.DeviceType ) );
	for ( BYTE channel = 1; channel <= channel_count; channel++ )
	{
		PressureChannelReading reading;
		bool found = false;
		for ( size_t i = 0; i < i_Sample.ChannelValues.size(); i++ )
		{
			if ( i_Sample.ChannelValues[i].byChannel == channel )
			{
				reading = i_Sample.ChannelValues[i];
				found = true;
				break;
			}
		}

		if ( !found )
		{
			reading.byChannel = channel;
			reading.nStatusCode = 6;
			reading.dPressure = numeric_limits<double>::quiet_NaN();
		}

		CsvFile << "," << reading.nStatusCode << ",";
		CsvFile.setf( ios::scientific );
		CsvFile << setprecision( 6 ) << reading.dPressure;
	}

	CsvFile << "\n";
	CsvFile.flush();
}


size_t CPressureLoggerAppEngine::ChannelCountForDevice( const enum PressureLoggerDeviceType i_DeviceType ) const
{
	return (i_DeviceType == PressureLoggerDevice_MaxiGauge) ? 6 : 2;
}


DWORD CPressureLoggerAppEngine::StoreDisplayChannelName( const enum PressureLoggerDeviceType i_DeviceType, const BYTE i_Channel, const string& i_Name, const bool i_AppendLog )
{
	if ( i_Channel < 1 )
		return PressureLoggerAppEngine_Command | ES_OutOfRange;

	vector<string> *pTargetNames = (i_DeviceType == PressureLoggerDevice_MaxiGauge) ? &MaxiGaugeDisplayNames : &Tpg262DisplayNames;
	if ( i_Channel > pTargetNames->size() )
		return PressureLoggerAppEngine_Command | ES_OutOfRange;

	(*pTargetNames)[i_Channel - 1] = NormalizeDisplayChannelName( i_Name, i_Channel );
	SaveUserConfig();

	{
		lock_guard<mutex> lock( StateMutex );
		UpdateActiveDisplayLabelsLocked();
	}

	if ( i_AppendLog )
		AppendLog( "[INFO] Anzeigename gesetzt: Kanal " + to_string( static_cast<unsigned>( i_Channel ) ) + " -> " + (*pTargetNames)[i_Channel - 1] );
	return EC_OK;
}


void CPressureLoggerAppEngine::EnsureDefaultDisplayNames()
{
	if ( Tpg262DisplayNames.size() != 2 )
		Tpg262DisplayNames.assign( 2, "" );
	if ( MaxiGaugeDisplayNames.size() != 6 )
		MaxiGaugeDisplayNames.assign( 6, "" );

	for ( size_t i = 0; i < Tpg262DisplayNames.size(); i++ )
		Tpg262DisplayNames[i] = NormalizeDisplayChannelName( Tpg262DisplayNames[i], static_cast<BYTE>( i + 1 ) );
	for ( size_t i = 0; i < MaxiGaugeDisplayNames.size(); i++ )
		MaxiGaugeDisplayNames[i] = NormalizeDisplayChannelName( MaxiGaugeDisplayNames[i], static_cast<BYTE>( i + 1 ) );
}


void CPressureLoggerAppEngine::UpdateActiveDisplayLabelsLocked()
{
	const enum PressureLoggerDeviceType device_type = State.Setup.DeviceType;
	State.DisplayChannelNames = GetDisplayChannelNames( device_type );
	State.CombinedChannelLabels.clear();

	for ( size_t i = 0; i < State.DisplayChannelNames.size(); i++ )
		State.CombinedChannelLabels.push_back( FormatCombinedChannelLabel( device_type, static_cast<BYTE>( i + 1 ) ) );
}


string CPressureLoggerAppEngine::NormalizeDisplayChannelName( const string& i_Name, const BYTE i_Channel ) const
{
	const string trimmed = _Trim( i_Name );
	if ( trimmed.length() != 0 )
		return trimmed;

	stringstream stream;
	stream << "Kanal " << static_cast<unsigned>( i_Channel );
	return stream.str();
}


string CPressureLoggerAppEngine::GetTextsDirectory() const
{
	return (filesystem::current_path() / "texts").string();
}
