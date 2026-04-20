#!/bin/zsh

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

if [ -f "$ROOT_DIR/.venv/bin/activate" ]; then
	source "$ROOT_DIR/.venv/bin/activate"
elif [ -f "$HOME/venvs/cdt_pressure_logger_v9/bin/activate" ]; then
	source "$HOME/venvs/cdt_pressure_logger_v9/bin/activate"
fi

cd "$ROOT_DIR"
exec python3 "$SCRIPT_DIR/cdt_pressure_logger_v9.py"
