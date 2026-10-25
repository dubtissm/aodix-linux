# ADX Strict CLAP

CLAP test instrument built on [clap-helpers](https://github.com/free-audio/clap-helpers) with
`MisbehaviourHandler::Terminate` and `CheckingLevel::Maximal` – the same strict host checks that commercial
plugins (e.g. u-he) use: every violation of the CLAP rules by the host terminates the process. It has a
non-resizable Xlib editor that asks for a different size right after it is attached (like plugins applying
their own gui scale) and on right click.

Needs the current CLAP headers and clap-helpers (both MIT):

```bash
git clone --depth 1 https://github.com/free-audio/clap.git
git clone --depth 1 https://github.com/free-audio/clap-helpers.git
g++ -std=c++17 -O2 -fPIC -shared -fvisibility=hidden -Iclap/include -Iclap-helpers/include strict.cpp \
    -o ~/.clap/strict.clap -lX11
```

With `STRICT_UHE=1` it imitates what was seen with u-he Bazille: it claims to be resizable, reports its
unscaled size via `get_size` while asking the host for the scaled size, and terminates on any `set_size()`.

Test: load "ADX Strict CLAP", open the editor, right click into it, turn the gain knob with and without audio
running, save/load, quit. Aodix must survive all of it (a violation prints `[CLAP] …` and aborts).
