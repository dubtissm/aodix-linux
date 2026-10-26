# Changelog

## Unreleased

- The main window remembers its size, position and maximized state (the original always started maximized).

## 4.2.1.2 (2026-10-25)

- VST3: plugins with a flexible channel layout that start with mono busses (for example JUCE plugins such as
  Signalizer) are asked for stereo main inputs and outputs, as other hosts do. Plugins that only support mono
  keep their layout.

## 4.2.1.1 (2026-10-25)

- PipeWire without `pw-jack`: if no JACK server is reachable, Aodix loads PipeWire's JACK library itself, so
  starting Aodix from the application menu works on Debian, MX Linux and similar systems.
- New settings *Buffer Size* and *Sample Rate* (File → Configuration, default 256 samples / 48000 Hz), passed to
  PipeWire like `PIPEWIRE_LATENCY` / `PIPEWIRE_RATE`. Values set on the command line take precedence.
- The driver name shows whether PipeWire is used, e.g. "PipeWire (48 kHz, 256 Samples)".
- Help menu: "Website..." is now "Development..." and opens the GitHub page https://github.com/dubtissm/aodix-linux.

## 4.2.1.0 (2026-10-25)

First release of Aodix for Linux: 64-bit X11 port of Aodix 4.2.0.3 with JACK audio and MIDI, VST3, CLAP, LV2
and LADSPA plugins, multi-core processing, UI scaling, keyboard layouts, clipboard, compressed `.adxl`
projects and a Debian package.
