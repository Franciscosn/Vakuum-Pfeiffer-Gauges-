///////////////////////////////////////////////////////////////////////////////////////////////////
//
// PressureLoggerAppEngine.h: interface for the CPressureLoggerAppEngine class.
//
// ------------------------------------------------------------------------------------------------
//
// Description:
///                                                                     \class CPressureLoggerAppEngine
/// 'CPressureLoggerAppEngine' bundles the shared Pfeiffer protocol layer into one application
/// engine that can be reused by the native Windows and macOS frontends. The engine owns the
/// serial connection, the monitoring thread, CSV logging and the current UI state snapshot.
//
// Please announce changes and hints to support@n-cdt.com
// Copyright (c) 2026 CDT GmbH
// All rights reserved.
//
///////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef PRESSURELOGGERAPPENGINE_H
#define PRESSURELOGGERAPPENGINE_H


#include "PfeifferGaugeLib.h"

#include <atomic>
#include <chrono>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>


struct HL_API PressureLoggerStateSnapshot
{
	bool bConnected;
	bool bMonitoring;
	bool bLogging;
	bool bFaulted;
	DWORD dwSampleCount;
	struct PressureLoggerConnectionSetup Setup;
	std::string sCsvPath;
	std::string sLastErrorText;
	std::vector<std::string> DisplayChannelNames;
	std::vector<std::string> CombinedChannelLabels;
	std::vector<PressureChannelReading> LastChannels;
	std::vector<PressureSample> History;
	std::vector<std::string> LogLines;

	#ifndef HL_EXTERN_C
		PressureLoggerStateSnapshot()
		{
			bConnected = false;
			bMonitoring = false;
			bLogging = false;
			bFaulted = false;
			dwSampleCount = 0;
		}
	#endif
};


// error codes
const DWORD EC_CPressureLoggerAppEngine            = 0x53000000;
const DWORD PressureLoggerAppEngine_Connect        = 0x53010000;
const DWORD PressureLoggerAppEngine_Disconnect     = 0x53020000;
const DWORD PressureLoggerAppEngine_StartLogging   = 0x53030000;
const DWORD PressureLoggerAppEngine_StopLogging    = 0x53040000;
const DWORD PressureLoggerAppEngine_ClearHistory   = 0x53050000;
const DWORD PressureLoggerAppEngine_GetState       = 0x53060000;
const DWORD PressureLoggerAppEngine_CollectPorts   = 0x53070000;
const DWORD PressureLoggerAppEngine_Command        = 0x53080000;


class HL_API CPressureLoggerAppEngine : virtual public CErrorLib
{

public:

	CPressureLoggerAppEngine();
	virtual ~CPressureLoggerAppEngine();

	DWORD Connect( const PressureLoggerConnectionSetup i_Setup );
	DWORD Disconnect();

	bool GetConnected() const;
	struct PressureLoggerConnectionSetup GetConnectionSetup() const;
	void GetStateSnapshot( PressureLoggerStateSnapshot *pSnapshot ) const;
	std::string GetLastErrorText( const DWORD i_ErrorCode );
	DWORD LoadUserConfig();
	DWORD SaveUserConfig() const;
	std::string GetConfigPath() const;
	std::string GetLastPort() const;
	enum PressureLoggerDeviceType GetLastDeviceType() const;
	void SetLastSelection( const enum PressureLoggerDeviceType i_DeviceType, const std::string& i_Port );
	std::vector<std::string> GetDisplayChannelNames( const enum PressureLoggerDeviceType i_DeviceType ) const;
	std::string GetDisplayChannelName( const enum PressureLoggerDeviceType i_DeviceType, const BYTE i_Channel ) const;
	std::string FormatCombinedChannelLabel( const enum PressureLoggerDeviceType i_DeviceType, const BYTE i_Channel ) const;
	double StatusPlotValue( const PressureLoggerStateSnapshot& i_Snapshot, const BYTE i_Channel, const int i_StatusCode, const double i_Value ) const;
	void BuildPlotSeries( const PressureLoggerStateSnapshot& i_Snapshot, const BYTE i_Channel, std::vector<double> *pTimes, std::vector<double> *pValues ) const;
	std::string MakeDefaultCsvPath( const enum PressureLoggerDeviceType i_DeviceType ) const;
	std::string GetHelpText( const std::string& i_Key ) const;
	std::string FormatLatestValues( const PressureLoggerStateSnapshot& i_Snapshot ) const;
	std::string FormatRecentSamples( const PressureLoggerStateSnapshot& i_Snapshot, const size_t i_MaxSamples ) const;

	DWORD CollectSuggestedPorts( std::vector<std::string> *pPorts ) const;
	DWORD StartLogging( const std::string& i_CsvPath );
	DWORD StopLogging();
	DWORD ClearHistory();
	DWORD ResetMeasurementTimeline();

	DWORD ExecuteRawCommand( const std::string& i_Command );
	DWORD ReadSingleChannelNow( const BYTE i_Channel );
	DWORD ReadDeviceInfo();
	DWORD ActivateAndVerify( const BYTE i_Channel );
	DWORD FactoryResetDevice();
	DWORD SetUnit( const int i_UnitCode );
	DWORD SetSensorState( const BYTE i_Channel, const bool i_TurnOn );
	DWORD SetDegas( const BYTE i_Channel, const bool i_On );
	DWORD SetFilter( const BYTE i_Channel, const int i_Value );
	DWORD SetCalibration( const BYTE i_Channel, const double i_Value );
	DWORD SetFsr( const BYTE i_Channel, const int i_Value );
	DWORD SetOfc( const BYTE i_Channel, const int i_Value );
	DWORD SetDisplayChannelName( const enum PressureLoggerDeviceType i_DeviceType, const BYTE i_Channel, const std::string& i_Name );
	DWORD SetChannelName( const BYTE i_Channel, const std::string& i_Name );
	DWORD SetDigits( const int i_Value );
	DWORD SetContrast( const int i_Value );
	DWORD SetScreensave( const int i_Value );

	virtual bool GetClassAndMethod( const DWORD i_MethodId, std::string *pClassAndMethodName );

private:

	DWORD StartMonitorThread();
	DWORD StopMonitorThread();
	DWORD ExecuteDriverCommand( const DWORD i_MethodCode,
								const std::string& i_SuccessPrefix,
								const bool i_RestartMonitoring,
								const std::function<DWORD(CPfeifferGaugeDriver*)>& i_Command );
	void MonitorLoop();
	void AppendLog( const std::string& i_Line );
	void WriteCsvHeader();
	void WriteCsvRow( const PressureSample& i_Sample );
	size_t ChannelCountForDevice( const enum PressureLoggerDeviceType i_DeviceType ) const;
	DWORD StoreDisplayChannelName( const enum PressureLoggerDeviceType i_DeviceType, const BYTE i_Channel, const std::string& i_Name, const bool i_AppendLog );
	void EnsureDefaultDisplayNames();
	void UpdateActiveDisplayLabelsLocked();
	std::string NormalizeDisplayChannelName( const std::string& i_Name, const BYTE i_Channel ) const;
	std::string GetTextsDirectory() const;

private:

	std::unique_ptr<CPfeifferGaugeDriver> pDriver;
	PressureLoggerConnectionSetup Setup;
	std::vector<std::string> Tpg262DisplayNames;
	std::vector<std::string> MaxiGaugeDisplayNames;
	std::string sLastPort;
	enum PressureLoggerDeviceType LastDeviceType;

	mutable std::mutex StateMutex;
	PressureLoggerStateSnapshot State;

	std::thread MonitorThread;
	std::atomic<bool> bStopRequested;

	std::ofstream CsvFile;
	std::chrono::steady_clock::time_point MonitorStartSteady;
	std::chrono::system_clock::time_point LoggingStartSystem;
};


#endif  // PRESSURELOGGERAPPENGINE_H
