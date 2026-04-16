///////////////////////////////////////////////////////////////////////////////////////////////////
//
// PfeifferGaugeLib.h: interfaces for the shared Pfeiffer gauge protocol layer.
//
// ------------------------------------------------------------------------------------------------
//
// Description:
///                                                                          \file PfeifferGaugeLib.h
/// This file bundles the shared data structures, the ASCII protocol helper and the two concrete
/// device drivers for Pfeiffer TPG 262 and MaxiGauge controllers. The goal is to keep all
/// protocol-specific communication outside the platform frontends.
//
// Please announce changes and hints to support@n-cdt.com
// Copyright (c) 2026 CDT GmbH
// All rights reserved.
//
///////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef PFEIFFERGAUGELIB_H
#define PFEIFFERGAUGELIB_H


#include "SerialPortLib.h"

#include <mutex>
#include <string>
#include <vector>


enum PressureLoggerDeviceType
{
	PressureLoggerDevice_TPG262 = 0,
	PressureLoggerDevice_MaxiGauge = 1
};


struct HL_API PressureChannelReading
{
	BYTE byChannel;
	int nStatusCode;
	double dPressure;
	std::string sStatusText;

	#ifndef HL_EXTERN_C
		PressureChannelReading()
		{
			byChannel = 0;
			nStatusCode = 6;
			dPressure = 0.0;
			sStatusText = "no sensor / not identified";
		}
	#endif
};


struct HL_API PressureSample
{
	double dSecondsSinceStart;
	double dCapturedAtUnixSeconds;
	std::vector<PressureChannelReading> ChannelValues;

	#ifndef HL_EXTERN_C
		PressureSample()
		{
			dSecondsSinceStart = 0.0;
			dCapturedAtUnixSeconds = 0.0;
		}
	#endif
};


struct HL_API PressureLoggerConnectionSetup
{
	enum PressureLoggerDeviceType DeviceType;
	std::string sPort;
	DWORD dwBaudRate;
	DWORD dwTimeoutMs;
	double dPollingSeconds;
	DWORD dwTPG262ContinuousMode;
	bool bTPG262LongTermMode;

	#ifndef HL_EXTERN_C
		PressureLoggerConnectionSetup()
		{
			DeviceType = PressureLoggerDevice_TPG262;
			sPort = "";
			dwBaudRate = 9600;
			dwTimeoutMs = 200;
			dPollingSeconds = 1.0;
			dwTPG262ContinuousMode = 1;
			bTPG262LongTermMode = false;
		}
	#endif
};


// error codes
const DWORD EC_CPfeifferGaugeDriver         = 0x52000000;
const DWORD PfeifferGaugeDriver_Init        = 0x52010000;
const DWORD PfeifferGaugeDriver_Close       = 0x52020000;
const DWORD PfeifferGaugeDriver_ReadSample  = 0x52030000;
const DWORD PfeifferGaugeDriver_Query       = 0x52040000;
const DWORD PfeifferGaugeDriver_Write       = 0x52050000;
const DWORD PfeifferGaugeDriver_CollectInfo = 0x52060000;


class HL_API CPfeifferSerialHelper
{

public:

	explicit CPfeifferSerialHelper( CSerialPort *pPort );

	DWORD Drain( const double i_Seconds );
	DWORD ReadLine( std::string *pLine, const double i_TimeoutSeconds );
	DWORD ReadUntilIdle( std::string *pData, const double i_IdleSeconds, const double i_MaxSeconds );

	DWORD SendAscii( const std::string& i_Command );
	DWORD SendETX();
	DWORD SendENQ();
	bool AckOk( const std::string& i_RawResponse ) const;
	DWORD ExpectAck( const double i_TimeoutSeconds );
	DWORD RequestResponse( const std::string& i_Command, std::string *pResponse, const double i_TimeoutSeconds );
	DWORD WriteOnly( const std::string& i_Command, const double i_TimeoutSeconds );

private:

	CSerialPort *pPort;
};


class HL_API CPfeifferGaugeDriver : virtual public CErrorLib
{

public:

	CPfeifferGaugeDriver();
	virtual ~CPfeifferGaugeDriver();

	DWORD Init( const PressureLoggerConnectionSetup i_Setup );
	DWORD Close();

	bool GetInit() const;
	struct PressureLoggerConnectionSetup GetConnectionSetup() const;
	std::recursive_mutex& GetMutex();
	DWORD StartMonitoringSession();
	DWORD StopMonitoringSession();
	virtual double GetSuggestedLoopDelaySeconds() const;

	virtual enum PressureLoggerDeviceType GetDeviceType() const = 0;
	virtual BYTE GetChannelCount() const = 0;
	virtual std::string GetDeviceName() const = 0;

	virtual DWORD ReadSample( PressureSample *pSample ) = 0;
	virtual DWORD ReadSingleChannel( const BYTE i_Channel, PressureChannelReading *pReading ) = 0;
	virtual DWORD ExecuteRaw( const std::string& i_Command, const bool i_WriteOnly, std::string *pResponse ) = 0;
	virtual DWORD CollectDeviceInfo( std::vector<std::string> *pLines ) = 0;
	virtual DWORD ActivateAndVerify( const BYTE i_Channel, std::vector<std::string> *pLines ) = 0;
	virtual DWORD FactoryReset() = 0;
	virtual DWORD SetUnit( const int i_UnitCode ) = 0;
	virtual DWORD SetSensorState( const BYTE i_Channel, const bool i_TurnOn ) = 0;
	virtual DWORD SetDegas( const BYTE i_Channel, const bool i_On ) = 0;
	virtual DWORD SetFilter( const BYTE i_Channel, const int i_Value ) = 0;
	virtual DWORD SetCalibration( const BYTE i_Channel, const double i_Value ) = 0;
	virtual DWORD SetFsr( const BYTE i_Channel, const int i_Value ) = 0;
	virtual DWORD SetOfc( const BYTE i_Channel, const int i_Value ) = 0;
	virtual DWORD SetChannelName( const BYTE i_Channel, const std::string& i_Name );
	virtual DWORD SetDigits( const int i_Value );
	virtual DWORD SetContrast( const int i_Value );
	virtual DWORD SetScreensave( const int i_Value );

	DWORD CollectSuggestedPorts( std::vector<std::string> *pPorts ) const;
	static std::string StatusText( const int i_StatusCode );

	virtual bool GetClassAndMethod( const DWORD i_MethodId, std::string *pClassAndMethodName );

protected:

	virtual DWORD PrepareMonitoringSession();
	virtual DWORD FinishMonitoringSession();

protected:

	CSerialPort Port;
	CPfeifferSerialHelper Helper;
	PressureLoggerConnectionSetup Setup;
	bool bInitialized;
	std::recursive_mutex Mutex;
};


class HL_API CTPG262Driver : public CPfeifferGaugeDriver
{

public:

	CTPG262Driver();
	virtual ~CTPG262Driver();

	virtual enum PressureLoggerDeviceType GetDeviceType() const;
	virtual BYTE GetChannelCount() const;
	virtual std::string GetDeviceName() const;
	virtual double GetSuggestedLoopDelaySeconds() const;

	virtual DWORD ReadSample( PressureSample *pSample );
	virtual DWORD ReadSingleChannel( const BYTE i_Channel, PressureChannelReading *pReading );
	virtual DWORD ExecuteRaw( const std::string& i_Command, const bool i_WriteOnly, std::string *pResponse );
	virtual DWORD CollectDeviceInfo( std::vector<std::string> *pLines );
	virtual DWORD ActivateAndVerify( const BYTE i_Channel, std::vector<std::string> *pLines );
	virtual DWORD FactoryReset();
	virtual DWORD SetUnit( const int i_UnitCode );
	virtual DWORD SetSensorState( const BYTE i_Channel, const bool i_TurnOn );
	virtual DWORD SetDegas( const BYTE i_Channel, const bool i_On );
	virtual DWORD SetFilter( const BYTE i_Channel, const int i_Value );
	virtual DWORD SetCalibration( const BYTE i_Channel, const double i_Value );
	virtual DWORD SetFsr( const BYTE i_Channel, const int i_Value );
	virtual DWORD SetOfc( const BYTE i_Channel, const int i_Value );

	virtual bool GetClassAndMethod( const DWORD i_MethodId, std::string *pClassAndMethodName );

protected:

	virtual DWORD PrepareMonitoringSession();
	virtual DWORD FinishMonitoringSession();

private:

	DWORD Query( const std::string& i_Command, std::string *pResponse );
	DWORD Write( const std::string& i_Command );
	DWORD ReadPressureResponse( const BYTE i_Channel, PressureChannelReading *pReading );
	DWORD GetSensorStatusFlags( std::vector<int> *pFlags );
	DWORD GetDegas( std::vector<int> *pValues );
	DWORD GetFilter( std::vector<int> *pValues );
	DWORD GetCalibration( std::vector<double> *pValues );
	DWORD GetFsr( std::vector<int> *pValues );
	DWORD GetOfc( std::vector<int> *pValues );
	DWORD GetIdent( std::string *pIdent );
	DWORD GetErrorStatus( std::string *pErrorText );
	DWORD ResetErrors( std::string *pResponse );
};


class HL_API CMaxiGaugeDriver : public CPfeifferGaugeDriver
{

public:

	CMaxiGaugeDriver();
	virtual ~CMaxiGaugeDriver();

	virtual enum PressureLoggerDeviceType GetDeviceType() const;
	virtual BYTE GetChannelCount() const;
	virtual std::string GetDeviceName() const;
	virtual double GetSuggestedLoopDelaySeconds() const;

	virtual DWORD ReadSample( PressureSample *pSample );
	virtual DWORD ReadSingleChannel( const BYTE i_Channel, PressureChannelReading *pReading );
	virtual DWORD ExecuteRaw( const std::string& i_Command, const bool i_WriteOnly, std::string *pResponse );
	virtual DWORD CollectDeviceInfo( std::vector<std::string> *pLines );
	virtual DWORD ActivateAndVerify( const BYTE i_Channel, std::vector<std::string> *pLines );
	virtual DWORD FactoryReset();
	virtual DWORD SetUnit( const int i_UnitCode );
	virtual DWORD SetSensorState( const BYTE i_Channel, const bool i_TurnOn );
	virtual DWORD SetDegas( const BYTE i_Channel, const bool i_On );
	virtual DWORD SetFilter( const BYTE i_Channel, const int i_Value );
	virtual DWORD SetCalibration( const BYTE i_Channel, const double i_Value );
	virtual DWORD SetFsr( const BYTE i_Channel, const int i_Value );
	virtual DWORD SetOfc( const BYTE i_Channel, const int i_Value );
	virtual DWORD SetChannelName( const BYTE i_Channel, const std::string& i_Name );
	virtual DWORD SetDigits( const int i_Value );
	virtual DWORD SetContrast( const int i_Value );
	virtual DWORD SetScreensave( const int i_Value );

	virtual bool GetClassAndMethod( const DWORD i_MethodId, std::string *pClassAndMethodName );

protected:

	virtual DWORD PrepareMonitoringSession();
	virtual DWORD FinishMonitoringSession();

private:

	DWORD Query( const std::string& i_Command, std::string *pResponse );
	DWORD Write( const std::string& i_Command );
	DWORD ReadPressureResponse( const BYTE i_Channel, PressureChannelReading *pReading );
	DWORD GetSensorOnOff( std::vector<int> *pValues );
	DWORD GetDegas( std::vector<int> *pValues );
	DWORD GetFilter( std::vector<int> *pValues );
	DWORD GetOfc( std::vector<int> *pValues );
	DWORD GetFsr( std::vector<int> *pValues );
	DWORD GetChannelNames( std::vector<std::string> *pNames );
	DWORD GetDigits( int *pDigits );
	DWORD GetContrast( int *pContrast );
	DWORD GetScreensave( int *pScreensave );
};


#endif  // PFEIFFERGAUGELIB_H
