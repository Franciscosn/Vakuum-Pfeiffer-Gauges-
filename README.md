# Vakuum-Pfeiffer-Gauges

Ein wissenschaftlich orientiertes Desktop-Werkzeug zum **Steuern, Diagnostizieren und Aufzeichnen** von Druckwerten aus
Pfeiffer-Vakuummessgeräten über RS-232.

> Unterstützte Gerätetypen in dieser Version:
> - **Pfeiffer TPG 262** (Dual Gauge, 2 Kanäle)
> - **Pfeiffer MaxiGauge / TPG 256 A** (6 Kanäle)

---

## Inhaltsverzeichnis

1. [Use Case und Zielgruppe](#use-case-und-zielgruppe)
2. [Funktionsüberblick](#funktionsüberblick)
3. [Software-Architektur (leicht verständlich)](#software-architektur-leicht-verständlich)
4. [Messbetrieb: von Sensor bis CSV](#messbetrieb-von-sensor-bis-csv)
5. [Gerätesteuerung und Diagnose](#gerätesteuerung-und-diagnose)
6. [Installation](#installation)
7. [Programmstart](#programmstart)
8. [Bedienanleitung (Schritt für Schritt)](#bedienanleitung-schritt-für-schritt)
9. [CSV-Format](#csv-format)
10. [Hinweise zur Messinterpretation](#hinweise-zur-messinterpretation)
11. [Sicherheit und Verantwortung](#sicherheit-und-verantwortung)
12. [Projektstruktur](#projektstruktur)
13. [Roadmap (Open-Source-Ausbau)](#roadmap-open-source-ausbau)
14. [Lizenz und Beitrag](#lizenz-und-beitrag)

---

## Use Case und Zielgruppe

Diese Anwendung richtet sich an Labor- und Technik-Umgebungen, in denen Vakuumdrücke

- **live beobachtet**,
- **zuverlässig protokolliert**,
- und bei Bedarf **Geräteparameter direkt gesetzt**

werden sollen – ohne proprietäre Komplettsoftware.

Typische Nutzer:innen:

- Wissenschaftler:innen und Ingenieur:innen in Vakuumlaboren
- Anlagenbetreiber:innen mit Pfeiffer-Controllern
- Personen, die Diagnosen bei Sensor-/Kanalproblemen durchführen

Das Tool ist bewusst so aufgebaut, dass man

1. schnell messen kann,
2. trotzdem tief in Diagnose und Parametrierung einsteigen kann,
3. und dabei nachvollziehbare CSV-Daten für spätere Auswertung erhält.

---

## Funktionsüberblick

### 1) Verbindung und Geräteauswahl

- COM-Port-Erkennung
- Auswahl zwischen TPG 262 und MaxiGauge
- Verbindung/Trennung direkt aus der GUI

### 2) Messung und Logging

- Live-Monitoring mit Statusanzeige
- Logging in CSV-Dateien
- „Live-only“-Modus (Anzeige ohne Dateischreiben)
- „Langzeitmodus“ mit frei definierbarem Abtastintervall

### 3) Visualisierung

- Logarithmische Live-Druckkurve (matplotlib)
- Umschaltbare Kanalsichtbarkeit
- Externes Plot-Fenster
- Nachträgliches Plotten vorhandener CSV-Dateien

### 4) Gerätefunktionen

- Einheit setzen (mbar/Torr/Pa)
- Gauge EIN/AUS
- Degas EIN/AUS
- Filter, Kalibrierfaktor (CAL), Full-Scale (FSR), Offset-Korrektur (OFC)
- MaxiGauge-spezifisch: Kanalnamen, Digits, Contrast, Screensave
- Diagnose und Werkreset
- Rohkommando-Modus für direkte ASCII-Kommandos

---

## Software-Architektur (leicht verständlich)

Die Anwendung besteht aus klar getrennten Bausteinen:

1. **GUI-Schicht (`PressureLoggerApp`)**
   - zeigt Buttons, Felder, Anzeigen und Plots
   - nimmt Benutzeraktionen entgegen (z. B. „Logging starten“)

2. **Treiber-Schicht (`TPG262Driver`, `MaxiGaugeDriver`)**
   - kennt gerätespezifische Befehle (`PRx`, `SEN`, `UNI`, …)
   - kapselt Unterschiede zwischen den beiden Controllerfamilien

3. **Serielle Hilfsschicht (`SerialHelper`)**
   - implementiert das Pfeiffer-Protokollmuster:
     - Kommando senden
     - ACK prüfen
     - ggf. ENQ senden
     - Daten lesen

4. **Mess-Thread + Queue**
   - ein Hintergrundthread liest fortlaufend Messwerte
   - Messdaten werden thread-sicher in eine Queue gestellt
   - GUI verarbeitet die Queue periodisch, ohne einzufrieren

Diese Trennung erleichtert Wartung, Fehlersuche und spätere Erweiterungen.

---

## Messbetrieb: von Sensor bis CSV

1. Nutzer verbindet das Gerät über COM-Port.
2. Die App erstellt den passenden Treiber.
3. Ein Hintergrundthread liest Samples (Status + Messwert je Kanal).
4. Die GUI aktualisiert:
   - numerische Kanalanzeigen,
   - Statusampeln,
   - Live-Plot.
5. Bei aktivem Logging wird pro Sample eine CSV-Zeile geschrieben.

Wichtig: Für die physikalische Interpretation ist der **Statuscode** (ok, underrange, overrange, sensor off …)
oft genauso wichtig wie der numerische Wert.

---

## Gerätesteuerung und Diagnose

Das Tool unterstützt sowohl komfortable GUI-Buttons als auch Rohkommandos.

Empfohlene Diagnose-Reihenfolge bei Problemen eines Kanals:

1. Identifikation prüfen (`TID`/`CID`)
2. Schaltstatus lesen (`SEN`)
3. Druckwert lesen (`PRx`)
4. Fehlerstatus prüfen (`ERR`, relevant v. a. bei TPG 262)
5. ggf. Fehler quittieren (`RES,1`) und erneut prüfen

 Zusätzliche Hintergrundinformationen sind im Ordner `texts/` enthalten
(z. B. `texts/rohkommandos_pfeiffer_vollstaendig.txt`, `texts/diagnose_lesen_hilfe_vollstaendig.txt`).

---

## Installation

### Voraussetzungen

- Python **3.10+** (empfohlen)
- RS-232 / USB-zu-Serial Verbindung zum Pfeiffer-Controller

### Python-Abhängigkeiten

```bash
pip install pyserial matplotlib
```

> `tkinter` ist bei vielen Python-Installationen bereits enthalten.
> Unter Linux muss ggf. ein separates Paket installiert werden (z. B. `python3-tk`).

---

## Programmstart

```bash
python cdt_pressure_logger_v9.py
```

---

## Bedienanleitung (Schritt für Schritt)

1. **Gerät auswählen** (TPG 262 oder MaxiGauge).
2. **COM-Port auswählen** und auf **Verbinden** klicken.
3. Optional:
   - CSV-Zielpfad festlegen,
   - Messintervall wählen,
   - Langzeitmodus aktivieren.
4. **Logging starten** oder **Neue Datei + Start** wählen.
5. Während der Messung:
   - Kanalstatus beobachten,
   - Plotkanäle ein-/ausblenden,
   - bei Bedarf Parameter ändern.
6. Nach Abschluss:
   - Logging stoppen,
   - Verbindung trennen.

---

## CSV-Format

### TPG 262

Spalten:

- `t_s`
- `status_1`, `value_1`
- `status_2`, `value_2`

### MaxiGauge

Spalten:

- `t_s`
- `status_1`, `value_1`
- …
- `status_6`, `value_6`

Werte werden wissenschaftlich formatiert (`x.xxxxxxE±yy`) gespeichert.

---

## Hinweise zur Messinterpretation

- `status=0`: Messwert gültig (ok)
- `status=1`: underrange
- `status=2`: overrange
- `status=4`: Sensor aus
- `status=5/6`: Sensor fehlt oder nicht identifizierbar

Bei overrange/underrange kann ein numerischer Wert trotzdem vorliegen –
für die Bewertung ist der Status entscheidend.

---

## Sicherheit und Verantwortung

- Dieses Projekt bietet direkte Schreibzugriffe auf Geräteparameter.
- Änderungen an `SEN`, `DGS`, `FSR`, `OFC`, `CAL`, `SAV` sollten nur mit fachlichem Verständnis erfolgen.
- Bei kritischen Anlagen empfiehlt sich ein Testlauf an nicht produktiver Hardware.

Die Nutzung erfolgt auf eigene Verantwortung.

---

## Projektstruktur

```text
.
├── cdt_pressure_logger_v9.py
├── README.md
├── texts/
│   ├── rohkommandos_pfeiffer_vollstaendig.txt
│   ├── diagnose_lesen_hilfe_vollstaendig.txt
│   └── hilfe_*.txt
```

- `cdt_pressure_logger_v9.py`: Hauptanwendung (GUI + Treiber + Logging)
- `texts/hilfe_*.txt`: kontextbezogene Hilfe in der Oberfläche

---

## Roadmap (Open-Source-Ausbau)

- Modularisierung in mehrere Python-Dateien (`drivers/`, `ui/`, `io/`)
- Einheitliche englisch/deutsche Dokumentation
- Automatisierte Tests (Parser, CSV-Writer, Statuslogik)
- Packaging (z. B. `pyproject.toml` + konsolenfähiger Entry Point)
- Beispiel-Datensätze und reproduzierbare Analyse-Notebooks

---

## Lizenz und Beitrag

Aktuell ist noch keine explizite Lizenzdatei (`LICENSE`) enthalten.
Für einen vollständigen Open-Source-Standard wird empfohlen:

1. Lizenz hinzufügen (z. B. MIT, BSD-3, GPLv3),
2. `CONTRIBUTING.md` und `CODE_OF_CONDUCT.md` ergänzen,
3. ggf. Issue-/PR-Templates hinzufügen.

Beiträge sind willkommen.
