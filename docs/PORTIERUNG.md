# Portierungsnotizen

## Grundidee

Aodix ist direkt gegen die Win32-API geschrieben: Die gesamte Oberfläche wird mit GDI aus den
Skin-Bitmaps zusammengesetzt (`BitBlt`), dazu kommen Popup-Menüs, drei Dialoge und Fensternachrichten.
Statt diesen Code umzuschreiben, stellt `src/platform/` genau die Teilmenge der Win32-API bereit, die
Aodix benutzt – nativ auf X11 implementiert. Dadurch bleibt der Originalcode fast unverändert, und die
Oberfläche sieht pixelgleich aus.

Das ist **keine** allgemeine Windows-Emulation (wie Wine), sondern eine kleine, auf Aodix
zugeschnittene Schicht (rund 8000 Zeilen).

### Plattformschicht (`src/platform/`)

| Datei | Inhalt |
|---|---|
| `win32compat.h` | öffentliche Typen, Konstanten und Funktionen (`HWND`, `HDC`, `BitBlt`, `TrackPopupMenu`, …) |
| `win32_gdi.cpp` | Software-Rendering in 32-bit-Puffer: BitBlt, AlphaBlend, Linien, Polygone, Text, BMP-Laden |
| `win32_window.cpp` | X11-Anbindung: Fenster, Nachrichtenschleife, Timer, Maus/Tastatur, Cursor, MIT-SHM |
| `win32_menu.cpp` | Menüleiste und Popup-Menüs (mehrspaltig, Untermenüs, Häkchen) im klassischen Windows-Stil |
| `win32_controls.cpp` | Eingabefeld, Schaltflächen, Checkboxen, Combobox, Listbox, Fortschrittsbalken |
| `win32_dialog.cpp` | modale Dialoge aus den Original-Templates (`Aodix.rc`), MessageBox, eingebauter Dateidialog |
| `win32_system.cpp` | Verzeichnisse, Pfade, Critical Sections, Dateiauswahl über zenity/kdialog |

Schriften: Der „Terminal“-Font (6×8) wird aus dem Schriftstreifen des Skins erzeugt
(`tools/gen_terminal_font.py`), die Dialog-/Menüschrift ist aus DejaVu Sans Condensed gerendert
(`tools/gen_ui_font.py`). Es gibt dadurch keine Laufzeitabhängigkeit von Schriftarten oder Toolkits.
Dialog-Einheiten werden wie unter Windows aus der Dialogschrift berechnet, damit die Original-Layouts passen.

## Änderungen am Aodix-Code

Alle Änderungen sind in der Git-Historie gegen das Original nachvollziehbar. Im Wesentlichen:

**64-bit**
- `long` ist unter Linux 64 bit breit, unter Windows 32 bit. In allen Datei-Lese/Schreib-Stellen
  (`.adx`, `.fxp/.fxb`, MIDI-Import) wird jetzt `int32_t` verwendet – sonst wären alle Projektdateien
  unlesbar gewesen.
- Zeiger in `long`/`LONG` gespeichert (`SetWindowLong(GWL_USERDATA, …)`, Fenster-Subclassing) →
  `SetWindowLongPtr`.
- Inline-Assembler (`rdtsc`) für die CPU-Anzeige → monotone Uhr.
- `static_assert`s sichern das Speicherlayout der Strukturen, die roh in `.adx`-Dateien geschrieben werden.
- Getestet: ein nach der Spezifikation erzeugtes Projekt wird geladen und byte-identisch wieder gespeichert.

**Plugins**
- Statt `AEffect`/`dispatcher` (VST 2) spricht Aodix eine formatunabhängige Schnittstelle
  `AdxPlugin` (`src/plugin/AdxPlugin.h`). Die VST3-Implementierung folgt in Etappe 4, CLAP kann später
  daneben gestellt werden.
- Die VST-2-Callback-Funktion (`audioMaster`) wurde durch `AdxPluginHost` ersetzt
  (Automation, MIDI-Ausgang, Anzeige aktualisieren, Editorgröße, Transportinformation).
- Die Plugin-Bibliothek sucht nach `.vst3`-Bundles; ein Bundle mit mehreren Plugins erscheint als Untermenü.
  Die Plugin-Klasse wird über die im Projekt gespeicherte 32-bit-ID ausgewählt.
- MIDI-Warteschlangen-Überlauf erzeugt keine MessageBox mehr aus dem Audio-Thread (Absturzgefahr).

**Audio / MIDI**
- `AodixCoreAsio.cpp` → `AodixCoreAudio.cpp`: gleiche Struktur (Audio-Callback füllt die Eingänge,
  ruft `dsp_work()` in der Critical Section, kopiert die Ausgänge), Treiber wird JACK (Etappe 2).
- MIDI-Eingang über den Audiotreiber (JACK-MIDI) statt Windows-Multimedia.
- Im Konfigurationsdialog heißt die Gruppe „ASIO“ jetzt „Audio (JACK)“; Funktionen und IDs sind gleich.

**System**
- Konfiguration in `~/.config/aodix/Aodix.cfg`, Skins/Hilfe relativ zum Programm oder in `share/aodix`.
- HTML-Hilfe (statt `.chm`) und Website werden mit `xdg-open` geöffnet.
- Pfadtrenner `\` → `/`; Dateiendungen werden nicht mehr in Kleinbuchstaben umgeschrieben (Linux-Dateisysteme
  unterscheiden Groß-/Kleinschreibung).

## Bekannte Einschränkungen in Etappe 1

- Kein Ton und keine Wiedergabe-Animation (die Transportsteuerung läuft im Audio-Callback → Etappe 2).
- Keine Plugins ladbar (→ Etappe 4).
- Alte Projekte mit **Windows**-Plugins: Patterns, Events, Spuren und Marker werden geladen. Ältere Dateien
  (vor Version 4.2.0.3) enthalten keine Ersatzinformationen für fehlende Plugins; dort bricht Aodix – wie
  das Original – das Laden der Instanzen beim ersten fehlenden Plugin ab.
- Zwischenablage zwischen Aodix-Textfeldern und anderen Programmen fehlt noch.

## Testen ohne Audio-Hardware

Ein Testprojekt nach der Dateiformat-Spezifikation erzeugt `tools/make_test_adx.py`:

```bash
python3 tools/make_test_adx.py /tmp/test.adx
./build/aodix /tmp/test.adx
```

## Nächste Etappen

2. **JACK**: Client mit bis zu 32 Ein-/Ausgangsports (entspricht den 32 Master-Pins), JACK-MIDI-Eingang,
   Samplerate/Puffergröße vom Server, Wiederverbinden nach Serverneustart, Bounce ohne JACK-Unterbrechung.
3. **Oberfläche**: Zwischenablage, Tastatur-Layouts prüfen (QWERTZ/AZERTY), optionale Integer-Skalierung
   für HiDPI-Bildschirme.
4. **VST3**: Laden der Bundles (MIT-lizenziertes VST3-SDK 3.8), Prozessor/Controller, Parameter-Queues,
   Noten- und MIDI-CC-Abbildung, Zustand als Chunk im Projekt, Editor über X11-Einbettung mit `IRunLoop`.
5. **Paketierung**: Installationspaket, MIME-Typ für `.adx`, optional CLAP.
