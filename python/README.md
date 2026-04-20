# Python Pressure Logger

Die lauffaehige Python-Referenz liegt jetzt bewusst getrennt vom nativen C++-Port in diesem Ordner.

Inhalt:

- `cdt_pressure_logger_v9.py`: originale Tkinter- und Matplotlib-App
- `run_logger.command`: Mac-Startskript fuer die Python-App
- `run_logger.bat`: Windows-Startskript fuer die Python-App

Start:

```bash
./python/run_logger.command
```

Unter Windows:

```bat
python\run_logger.bat
```

Bequemer Repo-Root-Starter:

```bash
./run_logger.command
```

Hinweise:

- Die Hilfetexte werden aus `../texts/` gelesen.
- Die Python-Konfiguration liegt neben dem Skript in `python/.cdt_pressure_logger_config.json`.
