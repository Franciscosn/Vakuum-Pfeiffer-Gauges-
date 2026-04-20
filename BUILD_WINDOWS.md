## Windows Build Workflow

Diese Anleitung beschreibt den vorgesehenen Windows-Workflow fuer das native C++-Frontend in der Parallels-VM.

### Voraussetzungen

- Windows 11 VM
- Visual Studio Community 2022 mit `Desktop development with C++`
- alternativ oder zusaetzlich: Visual Studio Build Tools 2022
- Repo im Gast unter:
  `C:\BuildWork\Vakuum-Pfeiffer-Gauges-`

### Visual-Studio-Workflow

1. Visual Studio Community 2022 starten.
2. Die Solution oeffnen:
   `C:\BuildWork\Vakuum-Pfeiffer-Gauges-\windows\VisualStudio\CDTPressureLoggerWin.sln`
3. Oben `Release` und `x64` auswaehlen.
4. `Build > Build Solution`.
5. Starten mit `Ctrl+F5` oder `Debug > Start Without Debugging`.

### Build-Ausgabe

Die fertige EXE liegt nach einem Release-Build unter:

`C:\BuildWork\Vakuum-Pfeiffer-Gauges-\windows\VisualStudio\build\x64\Release\CDTPressureLoggerWin.exe`

### Kommandozeilen-Build

Falls ohne IDE gebaut werden soll:

```bat
call C:\BuildTools\VC\Auxiliary\Build\vcvars64.bat
C:\BuildTools\MSBuild\Current\Bin\MSBuild.exe C:\BuildWork\Vakuum-Pfeiffer-Gauges-\windows\VisualStudio\CDTPressureLoggerWin.sln /m /p:Configuration=Release /p:Platform=x64
```

Fuer einen Debug-Build:

```bat
call C:\BuildTools\VC\Auxiliary\Build\vcvars64.bat
C:\BuildTools\MSBuild\Current\Bin\MSBuild.exe C:\BuildWork\Vakuum-Pfeiffer-Gauges-\windows\VisualStudio\CDTPressureLoggerWin.sln /m /p:Configuration=Debug /p:Platform=x64
```

### Wichtige Projektstruktur

- `src`: gemeinsame Bibliotheken fuer Fehlerbehandlung, Serial und Pfeiffer-Protokoll
- `apps/shared`: gemeinsame App-Logik fuer Monitoring, Logging und Status
- `apps/windows`: Windows-Frontend
- `windows/VisualStudio`: Solution- und Projektdateien fuer Visual Studio

### Hinweise

- Die eigentlichen Quelltexte liegen absichtlich nicht direkt im Visual-Studio-Ordner, damit dieselbe Kernlogik auch vom macOS-Frontend genutzt werden kann.
- Wenn das Repo an einen anderen Ort in der VM kopiert wird, muss nur der Pfad zur `.sln` angepasst werden. Die Projektdatei verwendet relative Include-Pfade.
