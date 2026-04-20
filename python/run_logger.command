#!/bin/zsh

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

if [ -f "$ROOT_DIR/.venv/bin/activate" ]; then
	source "$ROOT_DIR/.venv/bin/activate"
elif [ -f "$HOME/venvs/cdt_pressure_logger_v9/bin/activate" ]; then
	source "$HOME/venvs/cdt_pressure_logger_v9/bin/activate"
fi

cd "$ROOT_DIR"

if ! python3 - <<'PY' >/dev/null 2>&1
import matplotlib  # noqa: F401
import serial  # noqa: F401
PY
then
	echo "Python-Abhaengigkeiten fehlen."
	echo "Bitte einmal ausfuehren:"
	echo "  python3 -m pip install -r python/requirements.txt"
	read -r "REPLY?Weiter mit Enter beenden..."
	exit 1
fi

exec python3 "$SCRIPT_DIR/cdt_pressure_logger_v9.py"
