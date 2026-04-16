@echo off
setlocal

set "ROOT_DIR=%~dp0"
set "EXE=%ROOT_DIR%windows\VisualStudio\build\x64\Release\CDTPressureLoggerWin.exe"

if exist "%EXE%" (
	start "" "%EXE%"
) else (
	echo The Windows executable was not found.
	echo Build the Visual Studio solution first:
	echo   windows\VisualStudio\CDTPressureLoggerWin.sln
	pause
)
