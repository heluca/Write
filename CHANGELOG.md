# Changelog

Notable changes to Eidolon, at the level a user of the app would care about.
Format follows [Keep a Changelog](https://keepachangelog.com); versions are semver
(Android `versionCode` = MAJOR\*10000 + MINOR\*100 + PATCH). The **Builds** line under
each version records which platforms that version actually shipped on — it is amended
if a platform's build is produced later. Release process: every release gets a git tag
(`vX.Y.Z`) and a Gitea release. Gitea CI builds both the Android and Linux packages on
every push, so both are available for any release; minor releases (x.y.0) are also pushed
to GitHub, which additionally builds Windows. Patch and interim releases stay on Gitea only.

## [Unreleased]

**Builds:** Android (AAB + sideload APK), Linux (x86-64 tarball, needs glibc 2.34+ —
Ubuntu 22.04+/Debian 12+, and libsdl2 which the bundled `setup.sh` installs)

### Added
- Text tool: place styled text boxes (font family, size, color, bold/italic) as SVG
  `<text>` elements — they move, scale, erase, and undo like any other stroke, and
  survive in the saved SVG as real text. Tapping an existing text box with the tool
  edits it (this also works on touch-only phones, where double-tap-to-edit isn't
  reachable because a single-finger tap draws). Uses the system fonts on Android
  (Roboto, Noto Serif, mono). The multi-line editor shows a correctly sized caret that
  tracks the line it is on, with its drag handle attached, and the dialog's style
  controls line up in two columns that fit a phone screen.

## [0.1.0] — 2026-07-25

First release of the fork. Baseline is Stylus Labs Write with the rebrand to Eidolon
(`ca.helu.eidolon`) plus everything below.

**Builds:** Android (AAB + sideload APK — Play internal testing, versionCode 100)

### Added
- WebDAV sync: documents sync to any WebDAV server (static curl + mbedTLS on Android).
  The server copy is the durable one; the on-device copy lives in app-specific storage
  and is removed on uninstall.

### Changed
- Android: target SDK 36 / Android 16 — scoped storage, edge-to-edge insets (toolbar no
  longer renders behind the status bar), and the back key still returns to the document
  list under Android 16's predictive back.
- Android: toolbar buttons ~40% larger on high-density phone screens (lowest-priority
  toolbar items move into the overflow menu to make room).
- SDL upgraded 2.0.9 → 2.32.10; native libraries aligned for 16 KB page-size devices.

### Fixed
- Saved documents now put explicit width/height attributes on the top-level `<svg>`
  element (upstream Write doesn't), so `.svg`/`.svgz` files display at their real size
  in renderers that ignore the embedded CSS sizing — iOS QuickLook, JupyterLab, KDE
  previews — instead of collapsing to SVG's 300×150 default.
- Android: app froze when backgrounded mid-save and came back to a black screen with
  dead input (save-thread deadlock).
- Android: screen stayed black after returning to the app until something forced a
  redraw (stale swapchain after the surface was recreated).
