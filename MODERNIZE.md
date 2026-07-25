# Modernizing the Eidolon UI #

Findings from an investigation into how feasible it is to modernize the inherited
Write UI's appearance *without* changing how it behaves.

Short version: **very feasible**, and considerably more so than a typical "restyle
the app" task. The UI is not built from native widgets — it is SVG rendered through
a CSS engine, so appearance and behavior are already separated. Most of the visual
work is editing one 377-line file.


## How the UI is actually built ##

Three layers matter:

| Layer | Where | What it controls |
| --- | --- | --- |
| Colors | `ugui/theme.cpp` (`defaultColorsCSS`) | 14 CSS variables |
| Widget styling | `ugui/theme.cpp` (`defaultStyleCSS`) | Fills, fonts, margins per widget class |
| Widget geometry | `ugui/theme.cpp` (`defaultWidgetSVG`) | The SVG prototype for each widget |
| Screen layout | `syncscribble/res_ui.cpp` | How widgets compose into windows |
| Icons | `scribbleres/icons/` | 79 hand-authored SVG files |

Rendering goes through `usvg` (SVG parser/DOM) and `nanovgXC` (GPU rasterizer).

### The escape hatch ###

`createStylesheet()` in `syncscribble/resources.cpp:166-181` reads a `guiCSS` config
key. If set, it loads a stylesheet **from disk** instead of the built-in one:

```cpp
std::string guiCSS = ScribbleApp::cfg->String("guiCSS");
if(!guiCSS.empty() && guiCSS.find('\n') == std::string::npos)
  guiCSS = readFile(guiCSS.c_str());
```

`guiSVG` does the same for widget geometry (`resources.cpp:158-163`). Both default
to empty in `syncscribble/scribbleconfig.cpp:266-267`.

**This means you can iterate on the entire look by editing a file and restarting —
no recompile.** Fold the result back into `theme.cpp` once it is settled. This is
the single most useful fact for anyone doing this work.

Caveat: when `guiCSS` is set it *replaces* both default stylesheets rather than
cascading over them, so an override file must be complete, not a patch. Easiest
path is to copy the two string literals out of `theme.cpp` as a starting point.


## Why it looks dated ##

Three independent causes. They can be tackled separately and in any order.

### 1. Flat mid-gray palette ###

`ugui/theme.cpp:3-39`. The dark theme:

```css
--dark: #101010;    /* toolbar */
--window: #303030;  /* menu, dialog */
--light: #505050;   /* separator */
--base: #202020;    /* list, inputbox */
--button: #555555;
--title: #2EA3CF;
```

Evenly-spaced pure grays with zero hue, so no surface recedes or advances — the
depth cue modern dark themes rely on is absent. The single accent `#2EA3CF` is a
2010-era cyan. A light theme exists at `theme.cpp:23-39` with the same issue.

Every rule references these via `var()`, so they are genuinely centralized.

### 2. Everything is a hard-cornered rectangle ###

Every widget background is a plain `<rect>` with no `rx`/`ry` — only 2 occurrences
of `rx=` in the whole file, and one is the scroll handle. There is a revealing
leftover on the pushbutton at `theme.cpp:260`:

```
<rect class="background pushbtn-bg" box-anchor="hfill" width="36" height="36"/>  <!-- rx="8" ry="8" -->
```

Someone already started this and backed it out.

Compounding it, `theme.cpp:154` sets `shape-rendering: crispEdges` on all
backgrounds, explicitly disabling antialiasing.

Good news: `usvg` supports rounded rects natively, including **per-corner radii**
(`usvg/svgnode.h:473-479`), which is beyond what SVG proper offers.

### 3. Icons ###

79 files in `scribbleres/icons/`, hand-authored at 96×96 in a heavy 2011 Android
idiom — dense paths, thick strokes, boxy forms. See `ic_menu_copy.svg`. They all
carry `class="icon"` so they inherit color from CSS, but the *forms* are the dated
part and CSS cannot fix that.


## Effort estimate ##

Increasing cost, decreasing confidence:

**Palette rework — hours, near-zero risk.**
Rewrite 14 variables. Highest visual return per unit effort by a wide margin.
Pure paint; cannot affect layout or behavior. Do this first, and via `guiCSS` so
it needs no build.

**Rounded corners, shadows, antialiasing — 1-2 days, low risk.**
Add `rx`/`ry` to widget prototypes, drop `crispEdges`. Watch out: `margin` values
feed `ugui`'s flex layout (`ugui/layout.h`), so retuning spacing shifts geometry
rather than only paint. Corners and antialiasing alone are safe; spacing is not
quite.

**Typography — hours, low risk.**
Roboto, San Francisco (Display + Text) and DroidSansFallback are already bundled in
`scribbleres/fonts/`. Base size is set once at `theme.cpp:64` rather than
per-element, so global adjustment is a one-line change.

**Icon redesign — days to weeks, separable.**
The real work, and the only part that is genuinely laborious. Options: redraw in
place, or map to an existing open icon set re-exported at the same 96×96 viewBox
so nothing else has to change. Can be deferred entirely, or done incrementally
icon-by-icon — nothing else depends on it.


## The caveat on "without changing how it works" ##

That constraint holds cleanly for **colors, corners, antialiasing and typography** —
all pure paint.

It gets softer for **sizing**. Touch targets are tuned for stylus and finger input:
36px menu items and toolbar buttons recur 18 times through `theme.cpp`. Modern
designs trend toward more padding and whitespace, but growing these changes how
much fits on screen — on a phone or tablet that is a UX change, not a visual one,
and this app is used on both.

Recommendation: **hold the current metrics fixed in a first pass.** Treat spacing
as a deliberate, separate decision made against a real device, not something that
rides along with the restyle.


## Open questions ##

- **Renderer CSS coverage.** `box-shadow` is used at `theme.cpp:157` so some effects
  work, and gradients are parsed (`usvg/svgparser.cpp:616-641`). But parsed is not
  the same as rendered — worth a quick probe before committing to a design that
  leans on gradients or layered opacity.
- **Light theme parity.** Both themes need doing; only the dark one was examined
  closely here.
- **Per-platform divergence.** Not investigated whether iOS/Android override any of
  this (note the `ios-statusbar-bg` element in `res_ui.cpp`).


## Suggested first step ##

Draft a replacement palette as a standalone `guiCSS` file and point the config at
it. No build required, fully reversible, and it answers the only question that
really matters up front: how much of the dated feel is *just* the colors.

---

Repo note: reading `theme.cpp` requires the `ugui` and `usvg` submodules, which are
not populated by default — `git submodule update --init ugui usvg`.
