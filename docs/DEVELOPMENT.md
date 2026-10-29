# Development

Notes for building, packaging and working on Aodix for Linux. The detailed technical notes on the port are in
German: [PORTIERUNG.md](PORTIERUNG.md). The `.adx` file format is described in [adx-spec.txt](adx-spec.txt).

## Building

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/aodix [project.adxl]
```

Skins, themes and help are copied next to the executable, so Aodix runs straight from the build folder.
`cmake --install build` installs the program, skins, help, desktop entry, icons and the MIME types for
`.adx`/`.adxl` (`resources/aodix-mime.xml`).

Runtime libraries loaded with `dlopen` (no build dependency): **libjack** (jack2, or PipeWire's libjack – if the
regular libjack cannot reach a server, Aodix loads `…/pipewire-0.3/jack/libjack.so.0` itself) and **liblilv**
(LV2). Optional: `zenity` or `kdialog` for native file dialogs, otherwise a built-in dialog is used.

## Debian package and releases

```bash
cmake -B build-release -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build-release -j
cd build-release && cpack -G DEB          # -> aodix_<version>_amd64.deb (needs dpkg-dev)
```

The package configuration is in `cmake/Packaging.cmake` (dependencies from `dpkg-shlibdeps`, JACK and lilv as
recommends; maintainer field: `-DAODIX_PACKAGE_MAINTAINER="Name <mail>"`). A package runs on the distribution
it was built on and newer ones.

GitHub Actions:
- `.github/workflows/build.yml` builds every push and pull request.
- `.github/workflows/release.yml` builds the `.deb` on Ubuntu 22.04 (runs on Ubuntu/Mint 22.04+, Debian 12+)
  for every tag `v*` and attaches it to the release:

```bash
# version in CMakeLists.txt (project(... VERSION x.y.z.w)), entry in CHANGELOG.md, then
git tag v4.2.1.2
git push origin v4.2.1.2
```

## Source layout

```
src/aodix/      original Aodix code, changed only where the port requires it
src/argulib/    Arguru helper library (original)
src/platform/   Win32 compatibility layer on X11 (drawing, windows, menus, dialogs, clipboard, scaling)
src/plugin/     plugin interface (AdxPlugin.h) and hosts: plugin_vst3/clap/lv2/ladspa.cpp
                multi-core processing: src/aodix/AodixCoreDspGraph.cpp, AodixDspPool.cpp
                audio/MIDI (JACK/PipeWire): src/aodix/AodixCoreAudio.cpp
third_party/    parts of the VST3 SDK 3.8 (MIT), CLAP headers 1.2.2 (MIT), LV2 headers (ISC)
tests/          test plugins for the VST3, CLAP and LV2 hosts
tools/          test project generator, project rewiring, font generators, Impulse skin generator
resources/      skins, color themes, help, icons, desktop entry, MIME types
```

The history starts with the unchanged original source code (tag `original`):

```bash
git diff original -- src/aodix     # every change made to the original Aodix code
```

## Testing

- **Test plugins** (`tests/`): a VST3 instrument with an Xlib editor (`vst3_test_synth`, `ADXTEST_FLEX=1` for
  mono busses that accept stereo), CLAP instruments (`clap_test_synth`, and `clap_strict`, which terminates on
  every host rule violation like u-he plugins), and an LV2 instrument with an X11 UI (`lv2_test_synth`). Each
  writes a callback log to `$ADXTEST_LOG`; build instructions are in their READMEs.
- **Without audio hardware:** run a JACK dummy server (`jackd -d dummy`) or PipeWire.
  `tools/make_test_adx.py` writes a test project (`--thru` wires master input 1/2 to output 1/2);
  `tools/adx_rewire.py` rewrites routing, notes and cue range of a saved project.
- **Multi-core:** render with `AODIX_DSP_THREADS=1` and `=n` and compare the WAV files – they must be identical.
- **Environment overrides:** `AODIX_UI_SCALE=150`, `AODIX_DSP_THREADS=n`, `AODIX_BUILTIN_FILE_DIALOG=1`.
- AddressSanitizer / ThreadSanitizer builds: `-DCMAKE_CXX_FLAGS=-fsanitize=address` (or `thread`).

## Multi-core processing in short

The original processes instances one after another in "bubble" passes. Aodix computes this order per block,
builds a dependency graph from audio and MIDI wires and runs independent instances in a small real-time thread
pool. Every instance pulls its inputs in exactly the original order, so the output is bit-identical. Parallel
processing is only used when the measured plugin load makes it worthwhile. Each instance has a fixed thread
(plugins with own helper threads, like u-he's Multicore, hang when the calling thread keeps changing); switching
between serial and parallel processing is held back for half a second.

## UI scaling in short

Aodix keeps drawing in its original (logical) pixels; scaling happens at the X11 boundary only. 200 % doubles
pixels, 150 % maps 2×2 blocks to 3×3 with averaged in-between pixels. Plugin editor windows are never scaled.
