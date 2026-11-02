# Rez recomp module

Rez (United Game Artists, 2001), recompiled for the TrimUI Brick. Deadcast plays it in place of its
emulator when the game's Core is set to it.

## Supported Formats

| Region | Format | SHA-1 |
|---|---|---|
| Japan | CHD | `9711a0d0c21af3685672b9a42c72abf999fc7150` |
| Europe | CHD | `01b7c1cc29a840b141a4ae713037eb1224fac233` |

The SHA-1 is the CHD header's, shown by `chdman info` as **SHA1**. Other images, including GDI, CDI
and BIN/CUE, are currently not supported.

## Features

- **60 fps:** the stages are drawn at 60 frames a second, where the original draws them at 30.
- **Resolution:** 640x480 or 1024x768 (the Brick's screen), set in Recomp Options.

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
sdk/ui/toolchain/toolchain.sh
sdk/ui/toolchain/devlibs.sh <adb serial>
sdk/tools/build-module.sh "/path/to/Rez (Europe) (En,Ja,Fr,De,Es,It).chd"
sdk/tools/pack-module.sh
```

## Credits

Owes much to [dream-recomp](https://github.com/phobos665/dream-recomp), [dreamcomp](https://github.com/cherryduck/dreamcomp)
and [flycast](https://github.com/flyinghead/flycast).

## Licence

GPL-2.0 (`LICENSE`).
