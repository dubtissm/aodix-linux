# Aodix für Linux (64-bit)

Portierung des Musik-Sequencers **Aodix 4.2.0.3** von Arguru Software auf Linux, basierend auf
[Aodix Enhanced](https://github.com/vanjac/aodix-enhanced). Das Programm soll so originalgetreu wie
möglich bleiben: gleiche Oberfläche, gleiche Bedienung, gleiches Projektformat (`.adx`).

Ziele der Portierung:

- natives 64-bit-Linux-Programm (X11, unter Wayland über XWayland)
- **JACK** statt ASIO für Audio und MIDI
- **VST3**-Plugins (64-bit Linux), später eventuell CLAP
- alte `.adx`-Projekte bleiben lesbar und werden im selben Format gespeichert

![Aodix unter Linux](docs/screenshot-stage1.png)

## Stand

| Etappe | Inhalt | Status |
|---|---|---|
| 1 | 64-bit-Linux-Build, X11-Oberfläche, Projekte laden/speichern | ✅ fertig |
| 2 | JACK-Audio und JACK-MIDI | offen |
| 3 | Feinschliff der Oberfläche (Zwischenablage, Tastatur-Layouts, HiDPI) | offen |
| 4 | VST3-Hosting inklusive Plugin-Editoren | offen |
| 5 | Installation, Paketierung, CLAP (optional) | offen |

In Etappe 1 läuft die komplette Oberfläche (Sequencer, Routing, Menüs, Dialoge), Projekte werden geladen
und gespeichert. Es gibt noch **keinen Ton** (JACK folgt in Etappe 2) und es können noch **keine Plugins**
geladen werden (VST3 folgt in Etappe 4).

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

## Wo liegt was?

- Einstellungen: `~/.config/aodix/Aodix.cfg` (gleiches Binärformat wie unter Windows)
- Skins: `Skins/Blue`, `Skins/Night` neben dem Programm (oder `share/aodix/Skins`)
- Hilfe: Menü *Help → Content* öffnet die HTML-Hilfe im Browser

## Aufbau des Quellcodes

```
src/aodix/      Original-Programmcode von Aodix (nur dort geändert, wo die Portierung es erfordert)
src/argulib/    Arguru-Hilfsbibliothek (Original)
src/platform/   Win32-Kompatibilitätsschicht auf X11 (Zeichnen, Fenster, Menüs, Dialoge)
src/plugin/     Plugin-Schnittstelle (AdxPlugin), später VST3-/CLAP-Implementierung
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
gerendert (freie Bitstream-Vera-/DejaVu-Lizenz).
