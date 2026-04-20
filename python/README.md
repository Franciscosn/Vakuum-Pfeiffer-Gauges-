# Python Pressure Logger

Die lauffaehige Python-Referenz liegt jetzt bewusst getrennt vom nativen C++-Port in diesem Ordner.

Inhalt:

- `cdt_pressure_logger_v9.py`: originale Tkinter- und Matplotlib-App
- `requirements.txt`: Python-Abhaengigkeiten fuer die Referenz-App
- `run_logger.command`: Mac-Startskript fuer die Python-App
- `run_logger.bat`: Windows-Startskript fuer die Python-App

Abhaengigkeiten einmalig installieren:

```bash
python3 -m pip install -r python/requirements.txt
```

Start:

```bash
./python/run_logger.command
```

Unter Windows:

```bat
python\run_logger.bat
```

Hinweise:

- Die Hilfetexte werden aus `../texts/` gelesen.
- Die Python-Konfiguration liegt neben dem Skript in `python/.cdt_pressure_logger_config.json`.
- Wenn `ModuleNotFoundError: No module named 'serial'` erscheint, fehlt `pyserial`; die Installation
  ueber `requirements.txt` behebt das.
