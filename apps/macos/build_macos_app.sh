#!/bin/zsh

###################################################################################################
#
# build_macos_app.sh: build script for the native macOS CDT Pressure Logger frontend.
#
# This script intentionally avoids an Xcode project file so that the app can be built directly
# inside a checkout that only has the Apple Command Line Tools installed.
#
###################################################################################################

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$ROOT_DIR/build/macos"
APP_DIR="$BUILD_DIR/CDT Pressure Logger.app"
CONTENTS_DIR="$APP_DIR/Contents"
MACOS_DIR="$CONTENTS_DIR/MacOS"

cd "$ROOT_DIR"

mkdir -p "$MACOS_DIR"

clang++ \
	-std=c++17 \
	-fobjc-arc \
	-Isrc \
	-Iapps/shared \
	src/ErrorLib.cpp \
	src/SerialPortLib.cpp \
	src/PfeifferGaugeLib.cpp \
	apps/shared/PressureLoggerAppEngine.cpp \
	apps/macos/CDTPressureLoggerMac.mm \
	-framework Cocoa \
	-o "$MACOS_DIR/CDT Pressure Logger"

cp "$SCRIPT_DIR/Info.plist" "$CONTENTS_DIR/Info.plist"

echo "Built native app bundle at:"
echo "$APP_DIR"
