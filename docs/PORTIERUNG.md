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
  `AdxPlugin` (`src/plugin/AdxPlugin.h`). Die VST3-Implementierung steht in `src/plugin/plugin_vst3.cpp`,
  CLAP kann später daneben gestellt werden.
- Die VST-2-Callback-Funktion (`audioMaster`) wurde durch `AdxPluginHost` ersetzt
  (Automation, MIDI-Ausgang, Anzeige aktualisieren, Editorgröße, Transportinformation).
- Die Plugin-Bibliothek sucht nach `.vst3`-Bundles; ein Bundle mit mehreren Plugins erscheint als Untermenü.
  Die Plugin-Klasse wird über die im Projekt gespeicherte 32-bit-ID ausgewählt.
- MIDI-Warteschlangen-Überlauf erzeugt keine MessageBox mehr aus dem Audio-Thread (Absturzgefahr).
- Fehlende Plugins werden wie im Original in den VST-Pfaden gesucht – jetzt nach `.vst3`-Bundles; der
  „Browse“-Dialog wählt einen Bundle-Ordner. Voreingestellte Suchpfade: `~/.vst3`, `/usr/lib/vst3`,
  `/usr/local/lib/vst3`.

**VST3-Host** (`plugin_vst3.cpp`, SDK-Teilmenge in `third_party/vst3sdk`, MIT-Lizenz)
- Module werden über `VST3::Hosting::Module` geladen und nie entladen (manche Plugins vertragen das nicht).
  Die Klassenliste kommt aus `moduleinfo.json`, sonst aus der Factory.
- 32-bit-ID für das Projekt: FNV-1a-Hash der 16-Byte-Klassen-ID (das `.adx`-Format hat nur 32 bit Platz).
- Prozessor und Controller getrennt oder als Einzelkomponente, verbunden über `ConnectionProxy`.
  Alle Audio-Busse werden aktiviert und zu einer flachen Kanalliste zusammengefasst (Aodix-Pins).
- Parameter: Aodix arbeitet mit Index 0…n, VST3 mit IDs – Abbildung beim Laden. Änderungen aus Aodix gehen
  über eine Warteschlange an den Prozessor (Audio-Thread, `IParameterChanges`) und an den Controller (GUI-Thread).
  Änderungen aus dem Plugin-Editor (`performEdit`) landen in `plugin_automate` – wie `audioMasterAutomate`.
- MIDI: Note an/aus → VST3-Noten-Ereignisse; CC, Pitchbend, Aftertouch, Programmwechsel → Parameter über
  `IMidiMapping`. CC 120/123 beenden alle hängenden Noten. MIDI-Ausgabe (Legacy-MIDI-CC-Ereignisse) zurück nach Aodix.
- Programme: Programmliste über `IUnitInfo` und den Parameter mit `kIsProgramChange`.
- Zustand: Chunk `"ADX3VST3"` + Komponenten-Zustand + Controller-Zustand.
- Transport: `ProcessContext` aus Aodix' Zeitinformation (Tempo, PPQ, Takt, Taktart, Cycle).
- Editor: `IPlugView` mit `X11EmbedWindowID` im Aodix-Instanzfenster. `IPlugFrame` mit `Linux::IRunLoop`;
  Dateideskriptoren und Timer der Plugins laufen in Aodix' Nachrichtenschleife (GUI-Thread). Größenwünsche
  des Plugins (`resizeView`) und Größenänderungen durch den Benutzer (bei skalierbaren Editoren) werden
  weitergereicht; nicht skalierbare Editorfenster haben eine feste Größe.

**Kleinere Korrekturen**
- Die Konfiguration wird beim Übernehmen sofort gespeichert (nicht erst beim Beenden) – sie überlebt so
  einen abstürzenden Plugin oder eine beendete Sitzung.
- „Save“ kopierte den Dateinamen auf sich selbst (undefiniertes Verhalten, von ASan gefunden).

**Audio / MIDI (JACK)**
- `AodixCoreAsio.cpp` → `AodixCoreAudio.cpp`: gleiche Struktur wie das Original (Audio-Callback füllt die
  Eingänge, ruft `dsp_work()` in der Critical Section, kopiert die Ausgänge mit Begrenzung auf ±1).
- libjack wird per `dlopen` geladen: keine Build-Abhängigkeit, funktioniert mit jack2 und PipeWire-JACK.
- Der JACK-Client bleibt während der ganzen Sitzung offen. `audio_close()`/`audio_init()` (Konfiguration,
  Bounce) pausieren nur die Verarbeitung – so bleiben Verbindungen im Patchbay erhalten. Die Portanzahl wird
  nur beim Öffnen des Clients festgelegt (Ports werden nie entfernt, während der Callback läuft).
- JACK-Puffer größer als 2048 Samples werden in Teilblöcken verarbeitet.
- MIDI-Eingang über einen JACK-MIDI-Port. Der Audio-Thread legt die Nachrichten in eine lock-freie Queue,
  ein eigener Thread ruft `midi_in_process()` auf – wie der Windows-MIDI-Callback-Thread im Original
  (Aufnahme mit Undo-Kopie u. Ä. läuft so nicht im Echtzeit-Thread).
- Server-Shutdown wird erkannt und im GUI-Thread gemeldet.
- Im Konfigurationsdialog heißt die Gruppe „ASIO“ jetzt „Audio (JACK)“, die Samplerate-Auswahl ist zur
  Kanalanzahl geworden (die Samplerate bestimmt der JACK-Server).
- Fehlerkorrektur aus dem Original: „Render Cue Range“ im Bounce-Dialog erzeugte ohne Stop-Marker eine leere Datei.

**System**
- Konfiguration in `~/.config/aodix/Aodix.cfg`, Skins/Hilfe relativ zum Programm oder in `share/aodix`.
- HTML-Hilfe (statt `.chm`) und Website werden mit `xdg-open` geöffnet.
- Pfadtrenner `\` → `/`; Dateiendungen werden nicht mehr in Kleinbuchstaben umgeschrieben (Linux-Dateisysteme
  unterscheiden Groß-/Kleinschreibung).

## Bekannte Einschränkungen

- Nur VST3 (64-bit Linux). Windows-Plugins, VST 2 und 32-bit-Plugins werden nicht geladen.
- Alte Windows-Projekte mit VST-2-Plugins: Ein VST3-Nachfolger desselben Plugins hat eine andere ID und ein
  anderes Zustandsformat; solche Instanzen müssen neu geladen und eingestellt werden.
- Plugin-Editoren, die nur Wayland oder kein X11-Einbetten können, öffnen sich nicht (unter XWayland läuft X11).
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

Mit `--thru` sind Master-Eingang 1/2 direkt mit Master-Ausgang 1/2 verbunden – ein Signal an
`Aodix:in_1` muss dann an `Aodix:out_1` herauskommen (Audio-Test ohne Plugins).

Für den VST3-Host gibt es ein kleines Test-Instrument mit Xlib-Editor in `tests/vst3_test_synth`
(protokolliert Einbettung, Timer, Klicks und Größenänderungen). Geprüft wurde außerdem mit den
Beispiel-Plugins des VST3-SDK und unter AddressSanitizer. Auf echter Hardware erfolgreich getestet:
u-he Bazille (Vollversion) mit eingebettetem Editor, MIDI-Steuerung aus Patterns und Parameteranzeige.

## Nächste Etappen

2. ~~**JACK**~~ erledigt (Audio bis 32 Ports, MIDI-Eingang, Bounce ohne Trennung).
3. **Oberfläche**: Zwischenablage, Tastatur-Layouts prüfen (QWERTZ/AZERTY), Skalierung 1,5× und 2× für
   HiDPI-Bildschirme.
4. ~~**VST3**~~ erledigt (Laden, Parameter, Programme, MIDI, Zustand im Projekt, Editoren mit `IRunLoop`).
5. **Paketierung**: Installationspaket, MIME-Typ für `.adx`, optional CLAP.
