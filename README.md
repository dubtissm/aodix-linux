# Aodix für Linux (64-bit)

Portierung des Musik-Sequencers **Aodix 4.2.0.3** von Arguru Software auf Linux, basierend auf
[Aodix Enhanced](https://github.com/vanjac/aodix-enhanced). Das Programm soll so originalgetreu wie
möglich bleiben: gleiche Oberfläche, gleiche Bedienung, gleiches Projektformat (`.adx`).

Ziele der Portierung:

- natives 64-bit-Linux-Programm (X11, unter Wayland über XWayland)
- **JACK** statt ASIO für Audio und MIDI
- **VST3**-, **CLAP**- und **LV2**-Plugins (64-bit Linux) sowie **LADSPA**-Effekte
- alte `.adx`-Projekte bleiben lesbar und werden im selben Format gespeichert

![Aodix unter Linux](docs/screenshot-stage1.png)

## Stand

| Etappe | Inhalt | Status |
|---|---|---|
| 1 | 64-bit-Linux-Build, X11-Oberfläche, Projekte laden/speichern | ✅ fertig |
| 2 | JACK-Audio und JACK-MIDI | ✅ fertig |
| 3 | Feinschliff der Oberfläche (HiDPI-Skalierung ✅, Tastatur-Layouts ✅, Zwischenablage) | teilweise |
| 4 | VST3-Hosting inklusive Plugin-Editoren | ✅ fertig |
| 5 | Installation, Paketierung, CLAP (optional) | offen |

Die komplette Oberfläche läuft (Sequencer, Routing, Menüs, Dialoge), Projekte werden geladen und
gespeichert, Audio und MIDI laufen über JACK, Bounce schreibt WAV-Dateien, und 64-bit-**VST3**-Plugins
werden geladen – mit Parametern, Programmen, MIDI, Zustand im Projekt und eingebettetem Plugin-Editor.

## Audio und MIDI (JACK)

Aodix verbindet sich beim Start mit einem laufenden JACK-Server. Das funktioniert mit jack2 (z. B. über
QjackCtl gestartet) und mit PipeWire. Falls Aodix unter PipeWire keine Verbindung bekommt, mit
`pw-jack ./build/aodix` starten. Zum Bauen wird JACK nicht benötigt, die Bibliothek wird erst zur
Laufzeit geladen.

- **Ports:** `Aodix:out_1 …` und `Aodix:in_1 …` werden beim Start mit den Hardware-Ports verbunden.
  Die Anzahl (2/4/8/16/32) stellt man unter *File → Configuration → Channels* ein (wirkt nach Neustart).
  Welche JACK-Ports zu welchem Master-Pin gehören, wählt man wie im Original per Klick auf die Pins der
  Master-Input/Output-Module in der Routing-Ansicht.
- **Samplerate und Puffergröße** gibt der JACK-Server vor (Anzeige *block size* im Audio-Master).
- **MIDI:** Eingang `Aodix:midi_in`. Ist *Open MIDI In Devices* aktiv, verbindet Aodix alle Hardware-MIDI-
  Quellen automatisch (unter jack2 ggf. `a2jmidid -e` für ALSA-MIDI-Geräte starten).
- Wird der JACK-Server beendet, meldet Aodix das; nach dem Neustart von JACK den Treiber unter
  *File → Configuration* erneut übernehmen.

## VST3-Plugins

- **Suchpfade:** Voreingestellt sind die Standardorte `~/.vst3`, `/usr/lib/vst3` und `/usr/local/lib/vst3`
  (`~` steht für den eigenen Home-Ordner).
  Weitere Ordner lassen sich unter *File → Configuration → VST Plugins* eintragen (Unterordner werden
  durchsucht). Ein Bundle mit mehreren Plugins (z. B. mda-vst3) erscheint in der Bibliothek als Untermenü.
- **Laden:** wie im Original per Doppelklick auf einen leeren Instanz-Platz; Doppelklick auf eine geladene
  Instanz öffnet den Plugin-Editor. Ist *Instance To Master Autolink* aktiv, wird eine neue Instanz direkt mit
  dem Master-Ausgang verbunden.
- **MIDI:** Noten, Controller, Pitchbend, Aftertouch und Programmwechsel werden in VST3-Ereignisse bzw.
  Parameter übersetzt (über das `IMidiMapping` des Plugins). MIDI-Ausgaben eines Plugins kommen wie im
  Original im Routing an.
- **Parameter und Automation:** Knopfbewegungen im Plugin-Editor erscheinen in der Parameterliste und
  können aufgenommen werden. Programme kommen aus der Programmliste des Plugins (falls vorhanden).
- **Projekte:** Der vollständige Plugin-Zustand (Prozessor + Controller) wird im `.adx` gespeichert. Die Plugin-
  Klasse wird über eine aus der VST3-Klassen-ID abgeleitete 32-bit-ID wiedergefunden, das Bundle notfalls
  über seinen Namen in den Suchpfaden (Projekte lassen sich also auf einen anderen Rechner mitnehmen).
- **Editoren:** Die Plugin-Oberfläche wird in das Aodix-Instanzfenster eingebettet (X11). Plugins, die ihre
  Größe ändern wollen oder frei skalierbar sind, werden unterstützt.
- Windows-Plugins (`.dll`) und VST 2 werden nicht geladen.

Getestet mit u-he **Bazille** (Vollversion, VST3 Linux x64) inklusive Editor, den Beispiel-Plugins des VST3-SDK
(u. a. mda JX10, ADelay) und einem eigenen Test-Instrument mit Editor (`tests/vst3_test_synth`).

![u-he Bazille in Aodix unter Linux](docs/screenshot-vst3-bazille.png)

## Bauen

Benötigt: C++17-Compiler, CMake ≥ 3.16, X11- und Xext-Entwicklungspakete.

```bash
# Debian / Ubuntu / Mint
sudo apt install build-essential cmake libx11-dev libxext-dev

# Fedora
sudo dnf install gcc-c++ cmake libX11-devel libXext-devel

# Arch / Manjaro
sudo pacman -S base-devel cmake libx11 libxext
```

```bash
cd aodix_linux
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/aodix                  # oder: ./build/aodix projekt.adx
```

Die Skins und die Hilfe werden beim Bauen neben das Programm kopiert. Mit `cmake --install build` wird
Aodix systemweit installiert (Programm, Skins, Hilfe, Desktop-Eintrag).

Optional für native Dateidialoge: `zenity` (GNOME/GTK) oder `kdialog` (KDE). Ohne diese Programme
verwendet Aodix einen eingebauten Dateidialog.

## CLAP-Plugins

CLAP-Plugins (`*.clap`, z. B. die CLAP-Versionen von u-he Bazille oder Surge XT) erscheinen in der
Plugin-Bibliothek im Untermenü **CLAP**. Gesucht wird in den vier Ordnern unter
*File → Configuration → CLAP Plugins* – voreingestellt `~/.clap`, `/usr/lib/clap` und `/usr/local/lib/clap`.

- Instrumente und Effekte, Noten und MIDI, Parameter (mit der Werteanzeige des Plugins), Automation,
  Plugin-Oberfläche (eingebettet, mit Größenänderung), Zustand im Projekt, Mehrkern-Verarbeitung – wie bei VST3.
- Eine Datei mit mehreren Plugins bekommt ein eigenes Untermenü. Im Projekt wird das Plugin über seine
  CLAP-ID wiedergefunden.

Getestet mit u-he **Bazille** (CLAP) inklusive Oberfläche und Änderung der GUI-Skalierung im Plugin.

## LV2-Plugins

LV2-Plugins erscheinen in der Plugin-Bibliothek im Untermenü **LV2**, alphabetisch sortiert; Bundles mit
mehreren Plugins bekommen ein eigenes Untermenü. Eine Ordner-Einstellung gibt es nicht: Aodix findet die
Plugins wie andere LV2-Hosts über die Bibliothek **lilv** in den Standardorten (`$LV2_PATH`, sonst `~/.lv2`,
`/usr/local/lib/lv2`, `/usr/lib/lv2`).

- **Voraussetzung:** `liblilv-0.so.0` (Paket `liblilv-0-0` bzw. `lilv`, auf den meisten Systemen mit Audio-Software
  schon installiert). Aodix lädt die Bibliothek erst zur Laufzeit; fehlt sie, gibt es einfach kein LV2-Menü.
- Instrumente und Effekte, MIDI (auch MIDI-Ausgabe des Plugins), Parameter aus den Steuer-Ports (mit Bereich,
  logarithmisch, ganzzahlig, an/aus), Automation, Tempo und Transport (`time:Position`), Hintergrund-Arbeit der
  Plugins (Worker), Zustand im Projekt, Mehrkern-Verarbeitung.
- **Oberflächen:** X11-Oberflächen (`ui:X11UI`) werden eingebettet, mit Größenänderung. Plugins, die nur eine
  GTK- oder Qt-Oberfläche haben, werden über die Aodix-Parameterliste bedient.
- **Projekte:** Gespeichert werden Bundle-Pfad und URI des Plugins; liegt das Bundle später woanders, findet
  Aodix das Plugin über die URI wieder. Neu installierte LV2-Plugins erscheinen nach einem Neustart von Aodix.

## LADSPA-Plugins

Neben VST3 lädt Aodix auch LADSPA-Effekte (64-bit). Sie stehen in der Plugin-Bibliothek im Untermenü
**LADSPA**; Bibliotheken mit mehreren Plugins (z. B. `caps.so`, `phasers_1217.so`) bekommen ein eigenes
Untermenü.

- **Suchorte:** `$LADSPA_PATH`, `~/.ladspa`, `/usr/local/lib/ladspa`, `/usr/lib/ladspa`, `/usr/lib64/ladspa`.
  Die VST-Pfade aus der Konfiguration werden dafür nicht verwendet.
- **Parameter:** Die Regler des Plugins erscheinen in der Parameterliste – mit Wertebereich, Standardwert und
  passender Darstellung (logarithmisch, ganzzahlig, an/aus, abhängig von der Samplerate) – und lassen sich wie
  bei VST3 automatisieren und per MIDI-CC steuern.
- **Projekte:** Die Reglerwerte werden im `.adx` gespeichert; das Plugin wird über seine LADSPA-ID
  wiedergefunden.
- LADSPA kennt kein MIDI und keine eigene Oberfläche: Es gibt also nur Effekte (keine Instrumente), bedient
  über die Aodix-Parameterliste.

Getestet mit der Plugin-Sammlung von Steve Harris (swh-plugins, ~90 Plugins).

## Mehrkern-Verarbeitung

Aodix verteilt die Plugins auf mehrere CPU-Kerne. Instanzen, die im Routing nicht voneinander abhängen
(z. B. mehrere Synthesizer mit eigenen Effektketten), werden gleichzeitig berechnet; eine Instanz startet,
sobald alle Instanzen fertig sind, deren Audio- oder MIDI-Kabel in sie hineinführen.

- **Einstellung:** *File → Configuration → DSP Threads* – „Automatic“ (alle Kerne, höchstens 16), „1“
  (keine Parallelverarbeitung, wie das Original) oder eine feste Anzahl. Wirkt sofort.
- **Automatische Entscheidung:** Aodix misst laufend die Rechenzeit jeder Instanz und rechnet nur dann
  parallel, wenn es sich lohnt. Bei leichten Projekten bleibt alles auf einem Kern, weil das Aufwecken weiterer
  Threads mehr kosten würde als es bringt.
- **Gleicher Klang:** Die Eingänge jeder Instanz werden in genau der Reihenfolge gemischt wie im Original,
  MIDI-Ausgaben in derselben Reihenfolge weitergereicht. Das Ergebnis ist bitgenau dasselbe wie bei der
  Verarbeitung auf einem Kern (überprüft durch Vergleich gerenderter WAV-Dateien).
- **Echtzeit-Priorität:** Die zusätzlichen Threads bekommen dieselbe Echtzeit-Priorität wie der JACK-Thread,
  sofern der Benutzer das darf (bei JACK-Installationen meist über die Gruppe `audio` eingerichtet). Sonst
  erscheint ein Hinweis im Terminal; die Verarbeitung funktioniert trotzdem.
- Plugins mit eigener Mehrkern-Option (z. B. „Multicore“ in u-he-Plugins) können zusätzlich ihre Stimmen
  verteilen.

Gewinn in Zahlen (Test mit 6 rechenintensiven Instrumenten auf 2 Kernen): DSP-Last 69 % → 36 %. Den größten
Effekt hat es bei vielen unabhängigen, schweren Instrumenten; eine einzelne lange Effektkette kann nicht
schneller werden als ihre Glieder nacheinander.

## Skalierung für hochauflösende Bildschirme

Unter *File → Configuration → UI Scaling* lässt sich die Oberfläche auf **100 %, 150 % oder 200 %** stellen
(wirkt nach einem Neustart). Bei 200 % wird jedes Pixel verdoppelt und bleibt scharf; bei 150 % wird jeder
2×2-Block zu 3×3 Pixeln (Eckpixel bleiben erhalten, dazwischen wird gemittelt), damit es keine ungleich breiten
Pixel gibt. Plugin-Fenster werden **nicht** skaliert – sie zeichnen sich selbst und haben meist eine eigene
Größeneinstellung. Zum Ausprobieren ohne Konfiguration: `AODIX_UI_SCALE=150 ./build/aodix`.

## Wo liegt was?

- Einstellungen: `~/.config/aodix/Aodix.cfg` (gleiches Binärformat wie unter Windows)
- Skins: `Skins/Blue`, `Skins/Night` neben dem Programm (oder `share/aodix/Skins`)
- Hilfe: Menü *Help → Content* öffnet die HTML-Hilfe im Browser

## Aufbau des Quellcodes

```
src/aodix/      Original-Programmcode von Aodix (nur dort geändert, wo die Portierung es erfordert)
src/argulib/    Arguru-Hilfsbibliothek (Original)
src/platform/   Win32-Kompatibilitätsschicht auf X11 (Zeichnen, Fenster, Menüs, Dialoge)
src/plugin/     Plugin-Schnittstelle (AdxPlugin), Hosts für VST3, CLAP, LV2 und LADSPA (plugin_*.cpp)
                (Mehrkern-Verarbeitung: src/aodix/AodixCoreDspGraph.cpp, AodixDspPool.cpp)
third_party/    benötigter Teil des VST3-SDK 3.8, die CLAP-Header 1.2.2 (beide MIT) und die LV2-Header (ISC)
tests/          Test-Plugins für den VST3-, CLAP- und LV2-Host
resources/      Skins, Hilfe, Icon, Desktop-Eintrag
docs/           Dateiformat, Portierungsnotizen
```

Die Details zur Portierung stehen in [docs/PORTIERUNG.md](docs/PORTIERUNG.md).

## Git-Historie

Die Versionsgeschichte beginnt mit dem unveränderten Original-Quellcode, sodass jede Änderung der
Portierung nachvollziehbar ist. Sie liegt als Datei `aodix-git-history.bundle` bei. Einmalig einrichten:

```bash
cd aodix_linux
git init -b main
git fetch ./aodix-git-history.bundle main
git reset FETCH_HEAD
git log --oneline          # zeigt die Etappen
git diff 77e4cf3 -- src/aodix   # alle Änderungen am Original-Aodix-Code
```

## Lizenz

MIT-Lizenz wie das Original (siehe `LICENSE`). Aodix wurde von Juan Antonio Arguelles Rius („Arguru“)
und Zafer Kantar entwickelt, die Grafiken stammen von Paul Merchant und Marc De Haar; weitere Arbeiten von
J. van't Hoog (Aodix Enhanced). Die eingebettete Oberflächenschrift ist aus DejaVu Sans Condensed
gerendert (freie Bitstream-Vera-/DejaVu-Lizenz). Der mitgelieferte Teil des VST3-SDK von Steinberg steht
unter der MIT-Lizenz (`third_party/vst3sdk/LICENSE.txt`); VST ist eine Marke der Steinberg Media Technologies GmbH.
