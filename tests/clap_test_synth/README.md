# ADX Test CLAP

Minimal CLAP instrument used to test the Aodix CLAP host: sine voice, one parameter ("Gain"), state, and an editor drawn
with plain Xlib that uses the host's timer and posix-fd support. Left click in the editor sets the gain (sent to the host
as a parameter output event), right click asks the host for a larger editor (`request_resize`).
`ADXTEST_LOG=/path/to/file` writes a log of the callbacks, `ADXTEST_LOAD_US=<n>` adds an artificial cpu load per block.

```bash
g++ -std=c++17 -O2 -fPIC -shared -fvisibility=hidden -I../../third_party/clap/include adxtest_clap.cpp \
    -o ~/.clap/adxtest.clap -lX11
```
