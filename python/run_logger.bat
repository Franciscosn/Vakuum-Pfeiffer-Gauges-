@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "ROOT_DIR=%SCRIPT_DIR%.."

if exist "%ROOT_DIR%\.venv\Scripts\activate.bat" (
	call "%ROOT_DIR%\.venv\Scripts\activate.bat"
)

python -c "import matplotlib, serial" >nul 2>nul
if errorlevel 1 (
	echo Python-Abhaengigkeiten fehlen.
	echo Bitte einmal ausfuehren:
	echo   python -m pip install -r python\requirements.txt
	pause
	exit /b 1
)

where py >nul 2>nul
if %errorlevel%==0 (
	py -3 "%SCRIPT_DIR%cdt_pressure_logger_v9.py"
) else (
	python "%SCRIPT_DIR%cdt_pressure_logger_v9.py"
)
