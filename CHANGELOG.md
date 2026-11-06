# Changelog

## 4.2.1.7 (2026-11-06)

Editing
- Copies by dragging: Ctrl+drag on an event drags a copy; on an event of the selection, a copy of the whole
  selection. Dragging an event of the selection without Ctrl moves the whole selection. Holding Ctrl when
  dropping places it off the grid, releasing Ctrl before dropping snaps it to the grid. Ctrl+drag on a plugin
  in the routing view drags a copy of it (same plugin and settings, no wires).
- The middle mouse button drags the sequencer (vertically, faster when zoomed out, and by whole tracks) and the
  routing view (anywhere in the view).
- Note pre-release is a percentage of the quantization (0–95%, default 25%, per pattern): 0% = an entered note
  is one quantization step long. Projects stay compatible with Aodix on Windows.
- New shortcuts: Ctrl+L (start/end marker on the selection, cycle on), Shift+D/A (next/previous pattern),
  Shift+W/S (16 patterns forward/back), Shift+Q/E (zoom out/in), Ctrl+7/8 (pre-release −/+5%), Ctrl+9/0
  (previous/next instance), Ctrl+. (solo track). Ctrl+A now selects exactly the events of the pattern.
- Help → Keyboard Shortcuts... opens an overview of all keyboard and mouse shortcuts.

Playback
- Events flash when they start playing: their block lights up in a lighter version of its color and fades back
  (pattern calls for every event of the called pattern). Flash time in *File → Configuration → Flash Events*
  (default 400 ms, or no flash).
- The play position line moves evenly and no longer flickers (it is drawn as an overlay over the window image,
  its position is interpolated between audio blocks).
- A bypassed plugin is no longer processed, so it costs no cpu. Automation and other parameter changes still
  reach it and take effect when the bypass is switched off.
- Live mode pages to the next view when the play position reaches 95% of the visible height.

Configuration and look
- Project folder setting (*File → Configuration → Project Path*, default: the documents folder); Open and Save
  As start there.
- File dialogs with zenity 4 open in the right folder.
- Color themes: new keys for the routing view (wire, wire_arrow, midi_wire, wire_drag), the track separator
  lines (track_line_dark, track_line_light), the play line (play_line) and the flash colors (flash_note,
  flash_automation, flash_block). On dark skins the track separator lines were almost white; fixed.
- Impulse skin: black stripes in the master input/output modules and the transport buttons removed; the
  Impulse theme has wire colors that are readable on its dark background.

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
