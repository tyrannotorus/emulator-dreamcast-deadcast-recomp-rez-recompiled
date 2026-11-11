# Rez recomp module

Rez (United Game Artists, 2001), recompiled for the TrimUI Brick. Deadcast 1.2.0 or newer hosts it when the game's
Core is set to it: Deadcast's menu, Options and Recomp Store stay Deadcast's, and this module is the game.

## Supported Formats

| Region | Format | SHA-1 |
|---|---|---|
| Japan | CHD | `9711a0d0c21af3685672b9a42c72abf999fc7150` |
| Europe | CHD | `01b7c1cc29a840b141a4ae713037eb1224fac233` |
| Europe | CHD | `b02e62788394f59c34fcd07d19a7dd80b47fb76e` |
| Europe | CHD | `66bfa7cf95348746f61e36a50c77050d380ca3aa` |

The SHA-1 is the CHD header's, shown by `chdman info` as **SHA1**. The European disc's three CHDs hold the same
game data; they differ only in where the audio warning track starts. Other images, including GDI, CDI and BIN/CUE,
are currently not supported.

## Features

- **60 fps:** the stages are drawn at 60 frames a second, where the original draws them at 30.
- **Resolution:** 1024x768 (the Brick's screen) or 640x480, set in Options.
- **Vibration:** Off, Low, Medium or High, set in Options.
- **Controls:** every button can be remapped, set in Options. `module.cfg` declares them; the SDK's README explains how.

## Files

```
module.cfg              the manifest
store.json              what its page in the Recomp Store says
game/rez.toml           the game: its discs, boot file, relocations, hooks, functions
game/rez-pipelines.txt  the renderer's pipelines Rez uses
game/CMakeLists.txt     the module's build
src/                    native code replacing the game's libraries, and the port's hooks (rez.cpp)
sdk/                    the engine and port layer
```

## Building

From your own disc:

```
git submodule update --init --recursive
sdk/tools/toolchain/toolchain.sh
sdk/tools/toolchain/devlibs.sh <adb serial>
sdk/tools/build-module.sh "/path/to/Rez (Europe) (En,Ja,Fr,De,Es,It).chd"
sdk/tools/pack-module.sh
```

## Credits

Owes much to [dream-recomp](https://github.com/phobos665/dream-recomp), [dreamcomp](https://github.com/cherryduck/dreamcomp)
and [flycast](https://github.com/flyinghead/flycast).

## Licence

GPL-2.0 (`LICENSE`).
