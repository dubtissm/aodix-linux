# ADX Test Synth (VST3)

Minimal VST3 instrument used to test the Aodix VST3 host without a GUI toolkit:
sine voice, one parameter ("Gain"), an editor drawn with plain Xlib.

The editor exercises the host side of the VST3 Linux editor API:
X11 embedding (`X11EmbedWindowID`), `Linux::IRunLoop` (timer + X connection fd),
`IPlugFrame::resizeView` (right click grows the editor) and parameter edits from the
editor (left click sets the gain, which Aodix receives like a knob movement / automation).
With `ADXTEST_LOG=/path/to/file` the plugin writes a log of these callbacks.
With `ADXTEST_LOAD_US=<n>` every process call takes at least n microseconds (artificial cpu load for
performance tests of the parallel dsp processing).

Build (needs the complete VST3 SDK, built once with CMake):

```bash
SDK=/path/to/vst3sdk; L=$SDK/build/lib/Release
O=~/.vst3/adxtest.vst3/Contents/x86_64-linux; mkdir -p $O
g++ -std=c++17 -O1 -fPIC -shared -DRELEASE=1 -I$SDK adxtest.cpp $SDK/public.sdk/source/main/linuxmain.cpp \
    -o $O/adxtest.so -Wl,--whole-archive $L/libsdk.a -Wl,--no-whole-archive \
    $L/libsdk_common.a $L/libbase.a $L/libpluginterfaces.a -lX11 -lpthread -ldl
```
