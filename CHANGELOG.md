# Changelog

## 4.2.1.4 (2026-10-27)

- Fixed: projects that were loaded and saved again lost the leading `/` of their plugin paths, so the plugins had
  to be located again on the next load (with garbled file names in the message). Such projects are repaired
  automatically when they are opened.

## 4.2.1.3 (2026-10-26)

- The main window remembers its size, position and maximized state (the original always started maximized).
- Multi-core processing: every plugin instance is now always processed on the same thread. Plugins with their own
  helper threads (u-he "Multicore", e.g. Bazille) could hang when the calling thread changed from block to block.
  Switching between serial and parallel processing now happens at most every half second.
- The DSP worker threads get real-time priority through the audio system (under PipeWire via RTKit), so the
  warning about missing rtprio limits no longer appears on most systems.

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
