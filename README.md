# Lurker Desktop

A native Linux desktop client for [Lurker](https://github.com/amiantos/lurker) in the mIRC
tradition: MDI windows, treebar, switchbar, nicklists, and (planned) the mIRC scripting language.
C++20 and Qt 6. Clean-room: no mIRC code or assets. See [docs/DESIGN.md](docs/DESIGN.md).

**Status:** Phase 0 of [docs/PLAN.md](docs/PLAN.md) is done: the Qt-free core (IRC formatting
codes, slash commands, IRC message parsing) is tested and the window shell runs. Progress toward
mIRC parity is tracked in [docs/PARITY.md](docs/PARITY.md). Next: Phase 1, the Lurker backends.

## Building

Needs CMake 3.21+, Ninja, a C++20 compiler, and Qt 6.4+ (Widgets, Network, WebSockets).

```bash
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=$HOME/Qt/6.11.3/gcc_64
cmake --build build
ctest --test-dir build --output-on-failure
./build/src/gui/lurker-desktop
```

Without Qt, `-DLURKER_BUILD_GUI=OFF` builds and tests just the core.

A Qt without system packages can be installed into `~/Qt` with
[aqtinstall](https://github.com/miurahr/aqtinstall):

```bash
uvx --from aqtinstall aqt install-qt linux desktop 6.11.3 linux_gcc_64 -O ~/Qt -m qtwebsockets qtnetworkauth qtmultimedia qtimageformats
```

Developer flags: `--smoke-test` starts and exits; `--screenshot=out.png` saves an image of the
main window and exits (works with `QT_QPA_PLATFORM=offscreen`).

## License

[MPL-2.0](LICENSE), same as Lurker.
