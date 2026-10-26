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
- Busse: Haupt-Ein-/Ausgänge, die das Plugin mono anlegt, werden vor dem Aktivieren per `setBusArrangements`
  auf Stereo angefragt (JUCE-Plugins mit flexiblem Layout, z. B. Signalizer, starten sonst mono); lehnt das
  Plugin ab, bleibt sein Layout. Geprüft mit `ADXTEST_FLEX=1` im Test-Instrument.
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

**CLAP-Host** (`plugin_clap.cpp`, Header in `third_party/clap`, MIT-Lizenz)
- `clap_entry` → Plugin-Factory; Bibliotheken werden einmal initialisiert und nie entladen. Im Projekt steht
  ein 32-bit-FNV-Hash der CLAP-ID (wie bei den VST3-Klassen-IDs).
- Threads: „Main-Thread“ ist der Aodix-GUI-Thread, „Audio-Thread“ der DSP-Thread, der die Instanz gerade
  rechnet (`thread-check`-Erweiterung). `start_processing`/`stop_processing` laufen im Audio-Thread;
  beim Deaktivieren ohne laufendes Audio übernimmt der Main-Thread das Stoppen.
- Audio-Ports werden zu Aodix' flachen Kanallisten zusammengefasst. Noten gehen als CLAP-Noten-Ereignisse
  an das Plugin, übrige MIDI-Nachrichten als `CLAP_EVENT_MIDI` (falls der Notenport MIDI unterstützt);
  „All Notes Off“ als Note-Off mit Platzhalter-Taste. Noten- und MIDI-Ausgaben des Plugins → Aodix-MIDI-Kabel.
- Parameter: normalisiert ↔ Wertebereich des Plugins (gestufte Parameter gerundet), Anzeige über
  `value_to_text`. Änderungen aus Aodix werden als Ereignisse im nächsten `process()` zugestellt (ohne
  laufendes Audio über `params.flush`); Änderungen im Plugin-Editor kommen als Ausgabe-Ereignisse und werden
  im GUI-Thread an Aodix (Automation) weitergegeben.
- Transport (`clap_event_transport`) aus Aodix' Zeitinformation. Zustand: Chunk `"ADXCLAP1"` + `state.save`.
- Editor: X11-API, eingebettet ins Instanzfenster; `timer-support` und `posix-fd-support` laufen in der
  Aodix-Nachrichtenschleife; `request_resize` und Größenänderungen durch den Benutzer werden weitergereicht.
- Host-Anfragen (`request_restart`, `request_callback`, Parameter-Rescan) werden im Leerlauf des GUI-Threads
  erledigt.
- Konfiguration: vier CLAP-Ordner (`clap_path` am Ende von `Aodix.cfg`; ältere Dateien enden davor und
  bekommen die Standardpfade). Pfade dürfen mit `~` beginnen; der Ordner-Dialog schreibt Pfade im
  Home-Ordner in dieser Form.
- Regeln, die Plugins (z. B. von u-he über clap-helpers) streng prüfen und bei Verstoß das Programm beenden:
  nach `request_resize` ruft der Host kein `set_size()` auf (bei nicht skalierbaren Editoren verboten);
  `set_size()` gibt es nur bei echter Größenänderung durch den Benutzer – nicht als Folge von Fenstergrößen,
  die Aodix selbst gesetzt hat (manche Plugins melden per `get_size` eine andere, unskalierte Größe als die
  angeforderte) – und nur, wenn das Plugin die Größe per `adjust_size` akzeptiert;
  `request_resize` und `closed` dürfen aus jedem Thread kommen und werden ggf. im GUI-Thread erledigt
  (bei `closed` ruft der Host `destroy()` auf); `params.flush` läuft bei aktivem Plugin als „Audio-Thread“;
  `activate`/`deactivate` nur im Main-Thread (Aufrufe z. B. aus dem JACK-Samplerate-Callback werden in den
  GUI-Thread verschoben). Der Host erzwingt keine GUI-Skalierung (`set_scale` wird nicht aufgerufen).
- Geprüft mit einem eigenen Test-Instrument (`tests/clap_test_synth`, Xlib-Editor mit Timer und
  Datei-Deskriptor) und einem streng prüfenden Instrument auf Basis von clap-helpers
  (`tests/clap_strict`, beendet sich bei jedem Regelverstoß des Hosts): Laden, Noten, Parameter in beide
  Richtungen (mit und ohne laufendes Audio), Editor mit Größenwunsch, Zustand speichern/laden, Reihenfolge
  beim Beenden, AddressSanitizer.

**LV2-Host** (`plugin_lv2.cpp`)
- Die RDF-Beschreibungen liest **lilv**, das wie libjack erst zur Laufzeit geladen wird (`dlopen("liblilv-0.so.0")`,
  eigene Deklarationen der benötigten Funktionen; keine Build-Abhängigkeit). Die LV2-Header (ISC-Lizenz) liegen
  in `third_party/lv2`. Die lilv-Welt wird beim ersten Aufbau der Bibliothek im GUI-Thread angelegt
  (`lilv_world_load_all`: `$LV2_PATH` bzw. die Standardorte) und bis zum Programmende behalten.
- `plugin_registry.cpp` erkennt LV2 am Pfad (`*.lv2`, das Bundle-Verzeichnis). Als Plugin-ID dient ein
  32-bit-FNV-Hash der Plugin-URI; geladen wird das Plugin mit diesem Hash, egal wo sein Bundle liegt
  (deshalb überspringt `import_localize_vst_dll` die Suche für `.lv2`-Pfade).
- Host-Features: `urid:map`/`unmap` (global, mit Mutex), `options` (Blocklängen, `sequenceSize`, Samplerate,
  `ui:updateRate`), `bufsz:boundedBlockLength`, `worker:schedule` (ein gemeinsamer Worker-Thread; Anfragen und
  Antworten über lockfreie Ringpuffer, `work_response` und `end_run` im Audio-Thread), `log`. Plugins, die ein
  anderes Pflicht-Feature verlangen, werden mit Meldung abgelehnt.
- Ports: Audio-Ports pro Block an Aodix' Puffer angeschlossen; Steuer-Eingänge → Aodix-Parameter (Bereich,
  `toggled`, `integer`/`enumeration`, `logarithmic`, `sampleRate`; `freeWheeling`-Ports ausgeblendet), Werte
  atomar wie bei LADSPA; Atom-Eingang für MIDI (Ereignisse mit Sample-Offset) und `time:Position` (gesendet bei
  Start/Stopp, Tempowechsel und Sprung); Atom-Ausgänge: MIDI an Aodix (`plugin_midi_out`), alle Ereignisse an die
  Oberfläche. CV-Ports werden mit Stille bzw. einem Puffer verbunden.
- Zustand: Chunk `"ADXLV2S1"` + lilv-Zustand als Turtle (`lilv_state_new_from_instance` / `lilv_state_to_string`,
  Port-Werte und der eigene Zustand des Plugins); Laden mit `lilv_state_new_from_string` + `lilv_state_restore`.
  Bei geänderter Samplerate wird das Plugin mit seinem Zustand neu instanziiert.
- Oberflächen: `ui:X11UI` mit `ui:parent`, `ui:resize` (in beide Richtungen), `ui:portMap`, `instance-access`,
  `data-access`, `options`; das Idle-Interface wird per Timer (~30 Hz) aufgerufen. Steuerwerte gehen bei Änderung
  per `port_event` an die Oberfläche, Bedienungen in der Oberfläche als Parameter (und Automation) an Aodix bzw.
  als Atom-Nachricht (`atom:eventTransfer`) an das Plugin. Größen, die Aodix dem Fenster selbst gegeben hat,
  werden nicht als Benutzer-Größenänderung an die Oberfläche zurückgegeben (wie bei CLAP).
- Bibliothek: eigenes Untermenü „LV2“ (alphabetisch, Bundles mit mehreren Plugins als Untermenü), keine
  Ordner-Einstellung.
- Geprüft mit einem eigenen Test-Instrument (`tests/lv2_test_synth`: alle Port-Arten, Worker, Zustand, MIDI-Echo,
  `time:Position`, X11-Oberfläche mit Idle-Interface und Größenänderung) und den LV2-Beispiel-Plugins
  (eg-amp, eg-metro, eg-midigate, eg-fifths, eg-params): Laden, Noten, Parameter in beide Richtungen,
  Zustand speichern/laden (auch mit verschobenem Bundle), Beenden mit offenem Editor, AddressSanitizer,
  ThreadSanitizer.

**LADSPA-Host** (`plugin_ladspa.cpp`)
- Eigene Schnittstellendefinitionen nach der öffentlichen LADSPA-1.1-Spezifikation (ABI-kompatibel zu
  `ladspa.h`, damit das Projekt vollständig MIT-lizenziert bleibt). `plugin_registry.cpp` verteilt `scan`/`load`
  nach Dateityp (`.vst3` → VST3, `.so` → LADSPA).
- Bibliotheken werden einmal geöffnet und nie entladen. Plugins ohne Audio-Ausgang werden ausgelassen.
- Steuer-Eingänge → Aodix-Parameter: Bereich aus den Range-Hints (unbegrenzte Seiten werden ergänzt,
  `SAMPLE_RATE`-Hints mit der aktuellen Samplerate multipliziert), logarithmische, ganzzahlige und
  Schalter-Parameter, Standardwerte nach Spezifikation. Steuer-Ausgänge (Anzeigen) werden angeschlossen, aber
  nicht dargestellt.
- Thread-Sicherheit: Aodix schreibt Zielwerte (atomar); der Audio-Thread kopiert sie zu Beginn jedes Blocks in
  den Port-Speicher, den das Plugin liest. Audio-Ports werden pro Block an Aodix' Puffer angeschlossen.
  Neuinstanziierung bei geänderter Samplerate (LADSPA kennt die Samplerate nur beim Instanziieren); der
  Audio-Thread gibt währenddessen Stille aus.
- Zustand im Projekt: Chunk `"ADXLADS1"` + Anzahl + Reglerwerte (in Plugin-Einheiten). Programm: ein festes
  „Default“-Programm. Die 32-bit-LADSPA-ID wird direkt als Plugin-ID gespeichert.
- Bibliothek: eigenes Untermenü „LADSPA“, Suche in den LADSPA-Standardordnern; fehlende Bibliotheken eines
  Projekts werden dort nach Dateinamen gesucht, sonst per Dateiauswahl.
- Geprüft mit swh-plugins (Steve Harris): Laden, Parameterbereiche und Standardwerte, Signalkette mit
  Hallfahne, Zustand speichern/laden (bitgleiche Werte), AddressSanitizer.

**Mehrkern-Verarbeitung** (`AodixCoreDspGraph.cpp`, `AodixDspPool.cpp`)
- Das Original berechnet die Instanzen in „Bubble“-Durchläufen nacheinander: Eine Instanz arbeitet, sobald
  alle Instanzen mit Audio- oder MIDI-Kabeln zu ihr gearbeitet haben; direkt nach ihrem `process()` werden die
  Ausgänge in die Eingänge der Ziele gemischt und die MIDI-Ausgabe in deren Warteschlangen gehängt.
- Pro Block wird zuerst genau diese Reihenfolge bestimmt (dieselben Durchläufe, ohne zu rechnen). Daraus
  entsteht der Abhängigkeitsgraph. Unabhängige Instanzen laufen parallel in einem kleinen Echtzeit-Threadpool;
  der JACK-Thread arbeitet mit. Instanzen in Rückkopplungsschleifen arbeiten wie im Original nicht.
- Bitgenaue Ergebnisse: Statt dass eine Quelle in die Eingänge ihrer Ziele mischt („push“), sammelt jede
  Instanz unmittelbar vor ihrer Verarbeitung ihre Eingänge selbst ein („pull“), in exakt der Reihenfolge des
  Originals (Master-Eingang zuerst, dann die Quellen in Verarbeitungsreihenfolge, Ausgang für Ausgang, Kabel
  für Kabel). Die Master-Ausgänge werden am Ende in Verarbeitungsreihenfolge gemischt. MIDI-Ausgaben eines
  Plugins werden während `process()` pro Instanz zwischengespeichert und genauso zugestellt.
- Kein Speicheranlegen und keine blockierenden Systemaufrufe im Audio-Thread; leere Worker warten kurz aktiv und
  schlafen dann auf einem Semaphor. Gleicher Gleitkomma-Modus (Denormals) und Echtzeit-Priorität wie der
  JACK-Thread (`jack_client_real_time_priority`).
- Parallel wird nur gerechnet, wenn es sich lohnt: Die Rechenzeit jeder Instanz wird geglättet gemessen;
  aus Gesamtzeit, kritischem Pfad und geschätzten Kosten fürs Aufwecken (40 µs) wird pro Block entschieden.
  War der letzte Block parallel, werden die Worker gleich zu Beginn von `dsp_work()` geweckt.
- Feste Threads (seit 4.2.1.3): Jede Instanz wird immer im selben Thread gerechnet („Slot“: 0 = JACK-Thread,
  1…n = Worker, eigene Warteschlange und Semaphor pro Worker). Neue Instanzen kommen in den Thread mit der
  geringsten gemessenen Last. Grund: u-he-Plugins mit eingeschalteter „Multicore“-Option (Bazille) blieben hängen,
  wenn der aufrufende Thread von Block zu Block wechselte. Der Wechsel zwischen serieller und paralleler
  Verarbeitung (der Instanzen in andere Threads verschiebt) erfolgt erst, wenn die andere Betriebsart eine halbe
  Sekunde lang gewünscht ist. Echtzeit-Priorität für die Worker über `jack_acquire_real_time_scheduling`
  (PipeWire: RTKit), sonst `pthread_setschedparam`. Geprüft: bitgleiche Bounces 1/2/4 Threads, gleicher Gewinn
  (6,3 s → 3,6 s auf 2 Kernen), höchstens ein Thread-Wechsel pro Instanz, ThreadSanitizer ohne Befund.
- Einstellung `dsp_threads` in `Aodix.cfg` (0 = automatisch), Auswahl „DSP Threads“ im Konfigurationsdialog
  (wirkt sofort, der Dialog ist dafür eine Zeile höher). Testweise: `AODIX_DSP_THREADS=n`.
- Geprüft: gerenderte WAV-Dateien von Original und neuem Code mit 1, 2, 3, 4 und 8 Threads sind bitgleich
  (Projekte mit parallelen Zweigen, Ketten, Zusammenführungen und MIDI-Kabeln), ThreadSanitizer und
  AddressSanitizer ohne Befund im neuen Code. Einzige beobachtete Abweichung: das erste Sample von
  „mda Overdrive“ – das Plugin liest nicht initialisierten Speicher (`filt1`/`filt2`), das tritt mit
  veränderter Speicherbelegung auch im Original auf.

**HiDPI-Skalierung** (100/150/200 %)
- Wird komplett in der Plattformschicht erledigt: Aodix arbeitet weiter in seinen ursprünglichen
  (logischen) Pixeln. Erst bei der Übergabe an X11 werden Bildausgabe, Fenstergrößen, Positionen und
  Mauskoordinaten umgerechnet. Der Original-Code und die Skins bleiben unverändert.
- 200 %: Pixelverdopplung. 150 %: 2×2 → 3×3 mit exakter Mittelung der Zwischenpixel.
- Fenster mit `WS_EX_ADX_NATIVE_PIXELS` (Plugin-Editoren) werden nicht skaliert; ihre Größe ist in echten
  Pixeln, nur ihre Position wird umgerechnet (das Fenster bleibt über Aodix zentriert).
- Einstellung `ui_scale` in `Aodix.cfg` (aus einem reservierten Byte, Format bleibt kompatibel), neue Auswahl
  „UI Scaling *“ im Konfigurationsdialog; die MIDI-Geräteliste darunter ist dafür etwas kürzer.

**Komprimierte Projektdateien (`.adxl`)**
- `.adxl` = gzip-komprimierte `.adx`-Datei (zlib, Stufe 6), Inhalt byte-identisch. Der Originalcode liest und
  schreibt weiter über `FILE*` mit `fseek`/`ftell`: Beim Laden wird eine gzip-Datei (erkannt an den Magic-Bytes
  `1F 8B`, nicht an der Endung) in eine anonyme temporäre Datei (`tmpfile()`) entpackt; beim Speichern einer
  `.adxl` schreibt Aodix in eine temporäre Datei und komprimiert sie am Ende nach `<name>.tmp`, das dann die
  Zieldatei ersetzt (ein vorhandenes Projekt geht bei einem Schreibfehler nicht verloren). Funktionen in
  `AodixLinux.cpp` (`adx_project_open_read/open_write/close_write`).
- „Save As“ schlägt `.adxl` vor, `.adx` bleibt wählbar; „Save“ behält die Endung der geöffneten Datei.
  Ein fehlgeschlagenes Speichern wird jetzt gemeldet (das Original speicherte stumm).
- Dateitypen (`resources/aodix-mime.xml`, shared-mime-info): `application/x-aodix-project` (`*.adx`, Magic
  `4XDA` = Header-ID `'ADX4'` als Little-Endian-int32) und `application/x-aodix-project-compressed` (`*.adxl`,
  Unterklasse von `application/gzip`, damit Dateimanager die Datei nicht als Archiv behandeln). Der
  Desktop-Eintrag meldet Aodix für beide an. Geprüft mit `gio info` (GIO, wie Thunar).
- Geprüft: `.adx` laden → als `.adxl` und `.adx` speichern → `.adxl` laden → als `.adx` speichern; entpackte
  `.adxl`, direkt gespeicherte und neu gespeicherte `.adx` sind byte-identisch (2,9 MB → 10 KB).

**Version und Credits**
- Programmversion 4.2.1.x (`project()` in `CMakeLists.txt`, als `AODIX_VERSION_STRING` im Code), Fenstertitel
  „Aodix 4.2.1.3 (Linux)“. In die Projektdateien schreibt Aodix weiter die Formatversion 4203 (`aodix_version`),
  denn das Format ist unverändert und Aodix 4.2.0.3 unter Windows lehnt Dateien ab 4300 ab.
- About-Dialog (breiter, eine Zeile mehr): Arguelles Rius & Kantar (bis 4.2.0.1), J. van't Hoog (bis 4.2.0.3),
  Linux-Version und Weiterentwicklung: Marcus Honnacker via Claude Opus 5.5.

**Zwischenablage**
- Textfelder (Dialoge, Dateidialog und die Umbenennen-Felder im Hauptfenster) unterstützen Strg+C/X/V sowie
  Strg+Einfg, Umschalt+Entf und Umschalt+Einfg. Nach X11-Art wird markierter Text zusätzlich als PRIMARY
  angeboten und mit der mittleren Maustaste eingefügt. Eingefügte Zeilenumbrüche/Tabs werden zu Leerzeichen.
- Umsetzung in `win32_window.cpp`: ein verstecktes Fenster besitzt CLIPBOARD/PRIMARY und beantwortet
  `SelectionRequest` (TARGETS, UTF8_STRING, TEXT, STRING); Lesen per `XConvertSelection` mit bis zu 1 s
  Wartezeit (UTF8_STRING, sonst STRING). Sehr große Inhalte (INCR) werden nicht gelesen. Die Event-Zwischenablage
  des Sequencers ist davon unabhängig (intern, wie im Original).

**Fensterposition**
- Das Hauptfenster merkt sich Lage, Größe und Maximiert-Zustand (das Original startete immer maximiert).
  `win_state`/`win_geom` in `Aodix.cfg` (aus reservierten Bytes; physische Pixel, damit es nach einer Änderung
  der Skalierung passt). Die Plattformschicht merkt sich bei jedem `ConfigureNotify` die letzte nicht maximierte
  Geometrie (Rahmenposition über `_NET_FRAME_EXTENTS`, Zustand über `_NET_WM_STATE`); gespeichert wird beim
  Schließen. Liegt die gespeicherte Position außerhalb des Bildschirms, wird das Fenster zentriert.

**Kleinere Korrekturen**
- Die Konfiguration wird beim Übernehmen sofort gespeichert (nicht erst beim Beenden) – sie überlebt so
  einen abstürzenden Plugin oder eine beendete Sitzung.
- „Save“ kopierte den Dateinamen auf sich selbst (undefiniertes Verhalten, von ASan gefunden).

**Audio / MIDI (JACK)**
- `AodixCoreAsio.cpp` → `AodixCoreAudio.cpp`: gleiche Struktur wie das Original (Audio-Callback füllt die
  Eingänge, ruft `dsp_work()` in der Critical Section, kopiert die Ausgänge mit Begrenzung auf ±1).
- libjack wird per `dlopen` geladen: keine Build-Abhängigkeit, funktioniert mit jack2 und PipeWire-JACK.
- PipeWire ohne `pw-jack`: Manche Distributionen (Debian, MX Linux, …) legen PipeWires libjack in einen
  eigenen Ordner (`…/pipewire-0.3/jack/`), den nur `pw-jack` per `LD_LIBRARY_PATH` einbindet. Kann die normale
  libjack keinen Server erreichen, lädt Aodix diese Bibliothek selbst (die erste bleibt geladen, beide
  `RTLD_LOCAL`) und öffnet den Client erneut. Ob PipeWire benutzt wird, erkennt Aodix am Pfad der geladenen
  Bibliothek (`dlinfo`); der Treibername lautet dann „PipeWire (48 kHz, 256 Samples)“.
- Puffergröße/Samplerate für PipeWire (`pw_buffer_size`, `pw_sample_rate` aus reservierten Bytes von
  `Aodix.cfg`; 0 = 256 Samples / 48000 Hz) werden vor `jack_client_open` als `PIPEWIRE_LATENCY` und
  `PIPEWIRE_RATE` gesetzt, ohne vorhandene Umgebungsvariablen zu überschreiben. jack2 ignoriert sie.
  Neue Zeile im Konfigurationsdialog (Audio-Gruppe, Dialog 16 Einheiten höher).
- Geprüft mit einer Ersatz-libjack ohne Server und der echten libjack am PipeWire-Pfad: Rückfall, Werte der
  Konfiguration bzw. der Umgebung, Anzeige; außerdem jack2 direkt und die Fehlermeldung ohne jeden Server.
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

**Paketierung**
- `cmake/Packaging.cmake`: CPack-DEB, Präfix `/usr`, Abhängigkeiten per `dpkg-shlibdeps` (libc, libstdc++,
  libX11, libXext, zlib); per `dlopen` geladene Bibliotheken als „Recommends“ (libjack/pipewire-jack, liblilv),
  zenity/kdialog und JACK-Server als „Suggests“. Keine Maintainer-Skripte: Desktop-, MIME- und Icon-Caches
  aktualisieren die dpkg-Trigger. Icons 32/64/128/256 px (die größeren pixelgenau aus dem 32-px-Original
  vergrößert), Lizenzen unter `/usr/share/doc/aodix`. Maintainer-Feld über `-DAODIX_PACKAGE_MAINTAINER=...`.
- Geprüft: Paket gebaut, installiert (Trigger laufen), `/usr/bin/aodix` startet mit frischer Konfiguration und
  findet Skins und Hilfe unter `/usr/share/aodix`; `.adxl` wird als Aodix-Projekt erkannt.
- `.github/workflows/build.yml` (Build bei jedem Push) und `release.yml` (Paket bei Tag `v*`, als Release-Datei)
  auf Ubuntu 22.04. Die Workflows laufen erst auf GitHub und sind hier nicht ausgeführt worden.

## Bekannte Einschränkungen

- VST3, CLAP, LV2 und LADSPA (64-bit Linux). Windows-Plugins, VST 2, DSSI und 32-bit-Plugins werden nicht geladen.
- LV2: nur X11-Oberflächen (keine GTK-/Qt-Oberflächen, kein suil); keine Presets aus dem Bundle im Programm-Menü,
  kein `patch:`-Parameter-Protokoll in der Parameterliste (Plugins wie eg-params sind über ihre eigene
  Oberfläche bedienbar); neue Plugins erscheinen nach einem Neustart.
- CLAP: keine Preset-Browser-, MPE- oder Note-Expression-Unterstützung; Plugin-Presets über die Plugin-Oberfläche.
- Alte Windows-Projekte mit VST-2-Plugins: Ein VST3-Nachfolger desselben Plugins hat eine andere ID und ein
  anderes Zustandsformat; solche Instanzen müssen neu geladen und eingestellt werden.
- Plugin-Editoren, die nur Wayland oder kein X11-Einbetten können, öffnen sich nicht (unter XWayland läuft X11).
- Alte Projekte mit **Windows**-Plugins: Patterns, Events, Spuren und Marker werden geladen. Ältere Dateien
  (vor Version 4.2.0.3) enthalten keine Ersatzinformationen für fehlende Plugins; dort bricht Aodix – wie
  das Original – das Laden der Instanzen beim ersten fehlenden Plugin ab.

## Testen ohne Audio-Hardware

Ein Testprojekt nach der Dateiformat-Spezifikation erzeugt `tools/make_test_adx.py`:

```bash
python3 tools/make_test_adx.py /tmp/test.adx
./build/aodix /tmp/test.adx
```

Mit `--thru` sind Master-Eingang 1/2 direkt mit Master-Ausgang 1/2 verbunden – ein Signal an
`Aodix:in_1` muss dann an `Aodix:out_1` herauskommen (Audio-Test ohne Plugins).

`tools/adx_rewire.py` schreibt Routing, Noten und Cue-Bereich in ein gespeichertes Projekt (für Tests der
Mehrkern-Verarbeitung: Bounce mit `AODIX_DSP_THREADS=1` und `=n` erzeugen und die WAV-Dateien vergleichen).

Für den VST3-Host gibt es ein kleines Test-Instrument mit Xlib-Editor in `tests/vst3_test_synth`
(protokolliert Einbettung, Timer, Klicks und Größenänderungen). Geprüft wurde außerdem mit den
Beispiel-Plugins des VST3-SDK und unter AddressSanitizer. Auf echter Hardware erfolgreich getestet:
u-he Bazille (Vollversion) mit eingebettetem Editor, MIDI-Steuerung aus Patterns und Parameteranzeige.

## Nächste Etappen

2. ~~**JACK**~~ erledigt (Audio bis 32 Ports, MIDI-Eingang, Bounce ohne Trennung).
3. ~~**Oberfläche**~~ erledigt: Skalierung 1,5× und 2×, Tastatur-Layouts (QWERTY/QWERTZ/AZERTY), Zwischenablage.
4. ~~**VST3**~~ erledigt (Laden, Parameter, Programme, MIDI, Zustand im Projekt, Editoren mit `IRunLoop`).
5. ~~**Paketierung**~~ erledigt: Debian-Paket über CPack (`cmake/Packaging.cmake`), GitHub-Workflows für Build und Release.
