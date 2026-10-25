# ADX Test LV2 Synth

Minimal LV2 instrument used to test the Aodix LV2 host: sine/square/saw/triangle voice with a low-pass, control
ports of every kind (linear, logarithmic, toggled, integer, control output), MIDI and `time:Position` on an atom
input, a MIDI echo (notes +12) on the atom output, worker (one job per note), state interface, and an X11 UI
(`ui:X11UI`) drawn with plain Xlib, driven by the idle interface. Left click in the UI sets the gain, right click
asks the host for a larger editor (`ui:resize`). `ADXTEST_LOG=/path/to/file` writes a log of the callbacks.

```bash
B=~/.lv2/adxtest.lv2; mkdir -p $B; cp adxtest.lv2/*.ttl $B/
gcc -O2 -fPIC -shared -I../../third_party/lv2/include adxtest_lv2.c -o $B/adxtest_lv2.so -lm
gcc -O2 -fPIC -shared -I../../third_party/lv2/include adxtest_lv2_ui.c -o $B/adxtest_lv2_ui.so -lX11
```
