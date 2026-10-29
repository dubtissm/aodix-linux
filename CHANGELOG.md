# Changelog

## 4.2.1.6 (2026-10-29)

- Color themes: the colors of the event blocks (notes with their instrument colors, pattern, jump, MIDI,
  automation and tempo events) and of menus, dialogs and controls, which the original had hard coded, are now
  read from a text file (`*.theme`, one `name = #RRGGBB` per line). Choose it in *File → Configuration →
  Color Theme*; it is applied right away. Included: *Classic* (exactly the original colors), *Dark* and *Vivid*
  in the `Themes` folder.
- New skin *Impulse* with the matching theme *Impulse*: the colors and feel of Impulse Tracker 2 (tan panels with
  brown 3D edges, green and yellow text on black, a bolder DOS-like font, sample displays as background art) on
  the Aodix layout. Generated from the Blue skin by `tools/make_it2_skin.py`.
- The folder chooser for the skin path now starts in the current skin folder; skins and themes below the program
  folder are stored as relative paths (`Skins/Blue`, `Themes/Dark.theme`).

## 4.2.1.5 (2026-10-29)

- Fixed: the plugin libraries were released only by static destructors at program exit, after the plugins' own
  global objects had already been destroyed. VST3 `ModuleExit` could then crash (seen with dubtility); crash
  handlers of other plugins (Signalizer) reacted by writing files with garbage names into the current directory.
  Aodix now releases the libraries (VST3 `ModuleExit`, CLAP `deinit`) explicitly before it exits.
- A plugin that is not found at its stored path is now looked up by its unique ID in the plugin library before
  Aodix asks for it. This finds moved plugins and repairs projects whose plugin paths were damaged by earlier
  versions.

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
