#!/bin/zsh

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
"$SCRIPT_DIR/apps/macos/build_macos_app.sh"
open "$SCRIPT_DIR/build/macos/CDT Pressure Logger.app"
