# Aodix for Linux

[![Build](https://github.com/dubtissm/aodix-linux/actions/workflows/build.yml/badge.svg)](https://github.com/dubtissm/aodix-linux/actions/workflows/build.yml)
[![Release](https://img.shields.io/github/v/release/dubtissm/aodix-linux)](https://github.com/dubtissm/aodix-linux/releases/latest)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

**English** · [Deutsch](README.de.md)

**Aodix** is the tracker/sequencer hybrid by **Arguru Software**: a pattern based event editor combined with a
modular routing of plugin instances. This is a native 64-bit Linux version – same interface, same workflow and
the same project format as the original, with JACK/PipeWire audio and today's Linux plugin formats.

![Aodix on Linux](docs/screenshots/main-window.png)

## Features

- **Native 64-bit Linux** application (X11; runs on Wayland through XWayland)
- **JACK and PipeWire** for audio and MIDI – PipeWire works out of the box, no `pw-jack` needed
- **Plugins:** VST3, CLAP and LV2 instruments and effects with embedded editors, plus LADSPA effects
- **Multi-core processing:** independent plugin chains run in parallel, with bit-identical results
- **UI scaling** 100 / 150 / 200 % for high resolution screens, pixel-sharp like the original skins
- **Keyboard layouts** QWERTY, QWERTZ and AZERTY for note entry
- **Projects:** opens original `.adx` files; saves compressed `.adxl` (about 10 KB instead of 2.9 MB) or
  `.adx` for Aodix on Windows
- Clipboard in all text fields, Debian package with menu entry, icon and file types

| Routing | Plugin editor |
|---|---|
| ![Routing view](docs/screenshots/routing.png) | ![Plugin editor](docs/screenshots/plugin-editor.png) |

## Installation

**Debian, Ubuntu 22.04+, Linux Mint, MX Linux, Xubuntu …** – download the `.deb` from the
[latest release](https://github.com/dubtissm/aodix-linux/releases/latest) and install it:

```bash
sudo apt install ./aodix_*_amd64.deb
```

Aodix then appears in the application menu, and `.adx`/`.adxl` projects open with a double click.
For audio you need either PipeWire with JACK support (package `pipewire-jack`, installed on most current
systems) or a JACK server (jack2). LV2 plugins need `liblilv-0-0`.

**Other distributions:** build from source, see [Building](#building).

## Getting started

- **Audio:** Aodix connects to JACK or PipeWire on start. Buffer size and sample rate for PipeWire are set
  in *File → Configuration* (default 256 samples, 48000 Hz). *Current Driver* shows what is used, e.g.
  "PipeWire (48 kHz, 256 Samples)". If you start Aodix with `PIPEWIRE_LATENCY=… pw-jack aodix`, those values
  take precedence.
- **Plugins:** double-click an empty slot in the *VST Instance* list to open the plugin library – VST3 plugins
  at the top, CLAP, LV2 and LADSPA in their own sub menus. Double-click a loaded instance to open its editor.
- **Plugin folders:** VST3 is searched in `~/.vst3`, `/usr/lib/vst3`, `/usr/local/lib/vst3` and CLAP in
  `~/.clap`, `/usr/lib/clap`, `/usr/local/lib/clap` (more folders in *File → Configuration*). LV2 and LADSPA
  plugins are found in their standard locations.
- **Routing:** the *vst routing* button shows the modular view: wire audio and MIDI between instances and the
  master input/output, just like in the original.
- **MIDI input:** port `Aodix:midi_in`; with *Open MIDI In Devices* Aodix connects hardware MIDI sources.
- **Help:** *Help → Content* opens the original manual.

Settings are stored in `~/.config/aodix/Aodix.cfg`.

### Project files (.adx / .adxl)

An `.adx` file is a memory image of Aodix' project structure with all 256 patterns, even the empty ones – that
is why an empty project is 2.9 MB. `.adxl` is the same content, gzip compressed;
`gunzip -c project.adxl > project.adx` gives back an original `.adx`. *Save As* without an extension saves
`.adxl`, with the extension `.adx` uncompressed; *Save* keeps the format of the opened file.

### Real-time priority

For dropout-free audio, Aodix' processing threads run with real-time priority. Under PipeWire Aodix requests it
through RTKit automatically. If the terminal shows *"dsp worker threads could not get real-time priority"*, allow
real-time scheduling for your user, then log out and in again:

```bash
sudo usermod -aG audio $USER
printf '@audio - rtprio 95\n@audio - memlock unlimited\n' | sudo tee /etc/security/limits.d/95-audio.conf
```

On Debian-based systems with jack2, `sudo dpkg-reconfigure jackd2` (answer "yes" to real-time) sets up the same.
`ulimit -r` shows the allowed priority (0 = none). Aodix works without it, but may produce dropouts under load.

## Tested with

u-he Bazille (VST3 and CLAP, including editor and GUI scaling), u-he TetraOP, Signalizer, TAL plugins,
LoudMax (LADSPA), the swh LADSPA collection, LV2 effects, and the example plugins of the VST3 SDK.

## Known limitations

- Windows plugins (`.dll`), VST 2 and 32-bit plugins are not supported.
- LV2: only X11 plugin editors (GTK/Qt-only plugins are controlled through the parameter list); bundle presets
  are not listed; newly installed LV2 plugins appear after restarting Aodix.
- CLAP: no preset browser, MPE or note expressions.
- Old Windows projects: patterns, events, tracks and markers load; instances of Windows plugins stay empty –
  load a Linux version of the plugin instead (its settings have to be made again). Files older than 4.2.0.3
  carry no fallback data, so there Aodix stops loading instances at the first missing plugin, like the original.

## Building

Requirements: C++17 compiler, CMake ≥ 3.16, X11, Xext and zlib development packages.

```bash
# Debian / Ubuntu / Mint
sudo apt install build-essential cmake libx11-dev libxext-dev zlib1g-dev
# Fedora
sudo dnf install gcc-c++ cmake libX11-devel libXext-devel zlib-devel
# Arch / Manjaro
sudo pacman -S base-devel cmake libx11 libxext zlib

git clone https://github.com/dubtissm/aodix-linux.git
cd aodix-linux
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/aodix
```

`sudo cmake --install build` installs Aodix system wide. JACK and lilv are loaded at runtime, they are not
needed for building. Packaging, releases, source layout and tests: [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md).

## History and credits

Aodix was created by **Juan Antonio Arguelles Rius ("Arguru")** and **Zafer Kantar** (Arguru Software, up to
version 4.2.0.1), with graphics by **Paul Merchant** and **Marc De Haar**. **J. van't Hoog** fixed bugs and
continued it as [Aodix Enhanced](https://github.com/vanjac/aodix-enhanced) (4.2.0.3). This Linux version
(4.2.1.x) is developed by **Marcus Honnacker** together with Claude (Anthropic).
[Original Aodix website (archive)](https://web.archive.org/web/20070819041559/http://www.aodix.com/pageaodixv4.html).

The project history starts with the unchanged original source code; `git diff original -- src/aodix` shows
every change made for Linux. Technical notes on the port (in German): [docs/PORTIERUNG.md](docs/PORTIERUNG.md).

## License

MIT, like the original (see [LICENSE](LICENSE)). Included third-party code: parts of the Steinberg VST3 SDK
(MIT), the CLAP headers (MIT) and the LV2 headers (ISC); the UI font is rendered from DejaVu Sans Condensed.
VST is a trademark of Steinberg Media Technologies GmbH.
