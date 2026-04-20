@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "ROOT_DIR=%SCRIPT_DIR%.."

if exist "%ROOT_DIR%\.venv\Scripts\activate.bat" (
	call "%ROOT_DIR%\.venv\Scripts\activate.bat"
)

where py >nul 2>nul
if %errorlevel%==0 (
	py -3 "%SCRIPT_DIR%cdt_pressure_logger_v9.py"
) else (
	python "%SCRIPT_DIR%cdt_pressure_logger_v9.py"
)
