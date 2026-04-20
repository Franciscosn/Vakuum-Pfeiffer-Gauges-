# Vakuum-Pfeiffer-Gauges

Neu aufgebauter Stand fuer die Pfeiffer-Vakuumsteuerung mit zwei Schichten:

- die Python-Referenz liegt jetzt getrennt in `python/`.
- der neue native C++-Port liegt in `src/`, `apps/shared/`, `apps/windows/` und `apps/macos/`.

Unterstuetzte Geraetetypen:

- `Pfeiffer TPG 262`
- `Pfeiffer MaxiGauge / TPG 256 A`

## Ziel des C++-Ports

Der neue Port ist bewusst so strukturiert, dass er sich spaeter sauber in den CDT-Coater
integrieren laesst:

- `src/` enthaelt die kommentierte Serial-, Protokoll- und Geraeteschicht.
- `apps/shared/` enthaelt die gemeinsame App-Engine fuer beide Plattformen.
- `apps/windows/` enthaelt das native Win32-Frontend.
- `apps/macos/` enthaelt das native Cocoa-Frontend.
- `windows/VisualStudio/` enthaelt das Windows-Build-Projekt.

Die gemeinsame Engine uebernimmt:

- serielle Verbindung und Port-Vorschlaege
- Monitoring-Thread
- CSV-Logging
- Rohkommandos
- Diagnose- und Servicebefehle
- TPG-262- und MaxiGauge-spezifische Parameterbefehle

## Funktionsumfang des nativen Ports

Die C++-Variante deckt den kommunikations- und steuerungsrelevanten Kern der Python-App ab:

- Verbinden und Trennen
- Live-Monitoring
- CSV-Logging
- Einzelabfrage `PRx`
- Diagnose
- Aktivieren und Verifizieren eines Kanals
- Werkreset
- Einheit, Gauge EIN/AUS, Degas, Filter, CAL, FSR, OFC
- MaxiGauge-spezifisch: Kanalname, Digits, Contrast, Screensave
- Rohkommando-Modus

Wichtige Abweichung zur Python-Variante:

- Die Python-App besitzt einen Matplotlib-Live-Plot.
- Der native C++-Port zeigt stattdessen aktuelle Werte, Sample-Historie und Kommunikationslog
  in nativen Views an.

Das ist absichtlich so gehalten, damit die Geraetelogik zuerst sauber portabel und integrierbar
ist, statt direkt wieder an ein Python-Plot-Stack gekoppelt zu werden.

## Projektaufbau

```text
.
├── .github/workflows/
│   ├── macos-build.yml
│   └── windows-build.yml
├── apps/
│   ├── macos/
│   │   ├── CDTPressureLoggerMac.mm
│   │   ├── Info.plist
│   │   └── build_macos_app.sh
│   ├── shared/
│   │   ├── PressureLoggerAppEngine.cpp
│   │   └── PressureLoggerAppEngine.h
│   └── windows/
│       └── CDTPressureLoggerWin.cpp
├── src/
│   ├── ErrorLib.cpp
│   ├── ErrorLib.h
│   ├── HardwareLib.h
│   ├── PfeifferGaugeLib.cpp
│   ├── PfeifferGaugeLib.h
│   ├── SerialPortLib.cpp
│   └── SerialPortLib.h
├── python/
│   ├── cdt_pressure_logger_v9.py
│   ├── README.md
│   ├── requirements.txt
│   ├── run_logger.bat
│   └── run_logger.command
├── texts/
│   └── ...
├── windows/VisualStudio/
│   ├── CDTPressureLoggerWin.sln
│   └── CDTPressureLoggerWin/
│       ├── CDTPressureLoggerWin.vcxproj
│       └── CDTPressureLoggerWin.vcxproj.filters
├── Run CDT Pressure Logger.bat
├── Run CDT Pressure Logger.command
```

## Build und Start unter macOS

- Doppelklick auf `Run CDT Pressure Logger.command`
- oder im Terminal:

```bash
./Run\ CDT\ Pressure\ Logger.command
```

Ergebnis:

- `build/macos/CDT Pressure Logger.app`

Direkt oeffnen nach dem Build:

```bash
open "build/macos/CDT Pressure Logger.app"
```

Voraussetzung:

- Apple Command Line Tools oder Xcode

## Build und Start unter Windows

- `windows/VisualStudio/CDTPressureLoggerWin.sln` in Visual Studio 2022 oeffnen
- Konfiguration `x64 Release` bauen
- danach `Run CDT Pressure Logger.bat` verwenden
- oder direkt
  `windows\VisualStudio\build\x64\Release\CDTPressureLoggerWin.exe` starten

## Windows-Build ueber GitHub Actions

Workflow:

- `.github/workflows/windows-build.yml`

Ablauf:

1. Repository nach GitHub pushen
2. im Reiter `Actions` den Workflow `Build Windows App` starten
3. nach erfolgreichem Lauf das Artifact `CDTPressureLoggerWin-x64-Release` herunterladen

Das ZIP enthaelt:

- `CDTPressureLoggerWin.exe`
- `Run CDT Pressure Logger.bat`
- `README.md`

## macOS-Build ueber GitHub Actions

Workflow:

- `.github/workflows/macos-build.yml`

Artifact:

- `CDTPressureLogger-macOS`

Das ZIP enthaelt die gebaute App:

- `CDT Pressure Logger.app`

## Python-Referenz

Die vorhandene Python-App bleibt absichtlich im Repository, jetzt aber getrennt vom nativen Port:

- `python/cdt_pressure_logger_v9.py`
- `python/run_logger.command`
- `python/run_logger.bat`

Sie ist weiterhin die vollstaendige Labor-Referenz mit der bisherigen Tkinter- und Matplotlib-UI.
Der neue C++-Port wurde daran funktional ausgerichtet, aber architektonisch in eine spaeter besser
integrierbare Form ueberfuehrt.

Die Hilfetexte in `texts/` werden von Python und C++ gemeinsam genutzt.
