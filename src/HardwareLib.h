///////////////////////////////////////////////////////////////////////////////////////////////////
//
// HardwareLib.h: shared platform definitions for the CDT pressure logger port.
//
// ------------------------------------------------------------------------------------------------
//
// Description:
///                                                                              \file HardwareLib.h
/// This header provides the minimal type aliases and export helpers used by the new
/// cross-platform pressure logger implementation. It intentionally mirrors the style of the CDT
/// hardware code base without pulling in the historic library dependencies.
//
// Please announce changes and hints to support@n-cdt.com
// Copyright (c) 2026 CDT GmbH
// All rights reserved.
//
///////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef HARDWARELIB_H
#define HARDWARELIB_H


#include <cstdint>


#if defined(_WIN32) || defined(_WIN64)
	#define MS_WIN
#endif

#if defined(__APPLE__)
	#define MAC_OS
#endif

#define HL_API

typedef std::uint32_t DWORD;
typedef std::uint8_t BYTE;


#endif  // HARDWARELIB_H
