# Eidolon

A cross-platform handwritten-notes app: vector ink saved as SVG, with WebDAV sync.

Eidolon is a maintained fork of [Write](https://github.com/styluslabs/Write) by Stylus
Labs (C++/SDL, AGPL-3.0). 

Documents are ordinary SVG (gzipped, `.svgz`) — pages, strokes, images, and text are
all plain SVG elements, viewable in any browser and greppable forever.

## What this fork adds

- **WebDAV sync** — sync documents to any WebDAV server (static curl + mbedTLS on
  Android). The device copy is treated as a cache; the server copy is the durable one.
- **Text boxes** — a Text tool for placing and editing styled text (font family, size,
  color, bold/italic) as first-class SVG `<text>` elements.
- **Current Android platform work** — target SDK 36 (Android 16), scoped storage,
  edge-to-edge insets, predictive-back handling, 16 KB page-size support, SDL upgraded
  2.0.9 → 2.32.10, larger touch targets on phone-density screens, and fixes for several
  suspend/resume lifecycle bugs (black screen / dead input on re-open).

Android is the primary target and the only platform currently exercised regularly.
The desktop (Linux, macOS, Windows) and iOS builds are inherited from upstream and
should still work, but get far less testing.

## Building

Clone with submodules:

```
git clone --recurse-submodules https://github.com/heluca/Write.git eidolon
```

Note that two submodules point at patched forks and must be on the right branch
(`--recurse-submodules` handles this): `SDL` on `write-android` (the Android Makefile
checks the branch *name*) and `ugui` on `textbox`.

### Android

```
cd syncscribble/android
./gww assembleRelease        # or installRelease to build + install via adb
```

`gww` is a gradlew wrapper that can also bootstrap the toolchain: `./gww --install-sdk`.
Requirements: JDK 21, Android SDK with build-tools 36.0.0, **NDK r27** (pinned by
`ndkVersion`; the committed curl/mbedTLS prebuilts were built with it — don't mix NDK
versions). Release output is unsigned; sign for sideloading with your own key, e.g.

```
apksigner sign --ks ~/.android/debug.keystore --ks-pass pass:android \
    --ks-key-alias androiddebugkey app/build/outputs/apk/release/app-release-unsigned.apk
```

### Linux

```
cd syncscribble && make USE_SYSTEM_SDL=1
```

On Debian/Ubuntu: `apt install build-essential libsdl2-dev`. Copy the fonts from
`scribbleres/fonts` into `syncscribble/Release` before running.

### macOS

```
cd syncscribble && make MACOS=1
```

### iOS

Build SDL first (`cd SDL && make -f ../scribbleres/SDL-Makefile.ios`), then use the
[Xcode project](xcode/) or `cd syncscribble && make`. See the
[nanovgXC readme](https://github.com/styluslabs/nanovgXC#example-app) for setup.

### Windows

Install Visual Studio (Community is fine) and
[GNU make for Windows](http://www.equation.com/servlet/equation.cmd?fa=make); set
`DEPENDBASE` in `syncscribble/Makefile`; build SDL with
`make -f ../scribbleres/SDL-Makefile.msvc` (upstream used the `write-win` SDL branch);
then run `make` from a Visual Studio command prompt in `syncscribble`.

## Status and contributing

This is a personal fork maintained for daily use, with roughly one platform-maintenance
pass per year around Android's target-SDK deadlines. Release history and per-platform
build availability are tracked in [CHANGELOG.md](CHANGELOG.md). Issues and patches are welcome —
especially reports from the desktop and iOS builds — but expect a small-scale project,
not a product.

For the original app, documentation, and help: [styluslabs.com](http://styluslabs.com)
| [Help](http://styluslabs.com/write/Help.html) | [FAQ](http://styluslabs.com/faq).

## License

[AGPL-3.0](LICENSE), same as upstream Write. If you distribute builds or run a modified
version as a network service, the AGPL requires you to make the corresponding source
available.
