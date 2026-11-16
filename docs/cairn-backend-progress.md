# `tarn_ui` Cairn Backend Progress — Milestone B1

Date: 2026-11-16  
Branch: `macos-appkit` (`ui` repository)  
Compiler: `tarn 0.5.3` (`1b6102e`, 2026-11-15)  
Reference Plan: `cairn/docs/design/tarn-ui-cairn-backend-plan.md` & `cairn/docs/design/desktop-spec.md`

---

## 1. What Was Built in Milestone B1

Milestone B1 establishes the third `tarn_ui` backend (`backends/cairn/backend/`) alongside `backends/linux/backend` (GTK4) and `backends/macos/backend` (AppKit), keeping the platform-neutral `ABI 14` seam and existing backend tests completely intact.

### 1.1 Subsystems & Files Added

| Path | Responsibility |
|---|---|
| `backends/cairn/tarn.toml` | Package manifest (`tarn_ui_cairn`, links `objc`, `CoreFoundation`, `CoreGraphics`). |
| `backends/cairn/README.md` | Overview and commands for testing and running the Cairn backend. |
| `backends/cairn/backend/damage.tarn` | `Rect` geometry and bounded `DamageRegion` tracker (up to 16 coalesced Axis-Aligned Bounding Boxes aligned to `32×32` pixel tiles before minimum-area-increase merging). |
| `backends/cairn/backend/render.tarn` | Pure-Tarn 2D tile rasterizer: premultiplied BGRA8 `Canvas`, 64-bit SWAR span blending (2 pixels per `u64` multiply), sRGB `color-mix`, `ThemePalette` (`themeVars(prefs, dark)` from `desktop-spec.md` §1.3), analytical anti-aliased SDF rounded rectangles with per-corner radii $(r_{tl}, r_{tr}, r_{br}, r_{bl})$, `1px` inner/outer borders (`--edge`, `--line`), `1px` top inner specular highlight (`--hi`), linear/radial gradients, precomputed 9-slice drop shadows (`NineSliceShadow`, zero per-frame blur cost per ADR 0009 §9), and the procedural `Wallpaper("lago")` generator (`desktop-spec.md` §1.10). |
| `backends/cairn/backend/png.tarn` | Pure-Tarn PNG encoder (RFC 2083 + RFC 1950/1951 uncompressed Deflate blocks + Adler-32 + CRC-32 + FIPS 180-4 SHA-256) for headless CI verification and `snapshot_png` without needing a display server or screen-recording permission. |
| `backends/cairn/backend/objc.tarn` | Objective-C (`objc_msgSend`), `CoreFoundation`, `CoreGraphics` (`CGDataProvider`, `CGImage`), and `clock_gettime_nsec_np` FFI declarations in pure Tarn. |
| `backends/cairn/backend/surface_macos.tarn` | macOS host surface (`NSWindow` + layer-backed `NSView` / `CALayer` + `CGImage` presentation). Skips all CoreGraphics work when `DamageRegion.is_empty()` is true (`0` redraws and `0` presents at idle). |
| `backends/cairn/backend/surface_linux.tarn` | Documented compile-time stub for the Linux Wayland (`wl_shm` + `wl_surface.damage_buffer` + `wl_surface.frame`) surface presenter alongside the working headless buffer mode. |
| `backends/cairn/backend/scene_b1.tarn` | Retained B1 scene: cached `Wallpaper("lago")` layer, cached `.glass.window` backdrop layer (`26px` radius, `1px` `--edge` border, `1px` `--hi` inner specular highlight, traffic lights, 9-slice window drop shadow), Cairn 3-stone vector mark (`brand.mjs`), 6 accent swatches (`ACCENTS`), interactive `.card` / `.card.is-on` radio cards, and `.btn-soft` (theme toggle) / `.btn-primary` (accent cycle) buttons with hover and active states. |
| `backends/cairn/backend/native.tarn` | Full `native.Native` (`ABI 14`) session implementation over `surface_macos` and `scene_b1`, including lifecycle, event queue, pointer/key dispatch, dynamic list/tree reconciliation, `watch_fd`, `watch_interval`, and `snapshot_png`. |
| `backends/cairn/backend/devtools_off.tarn` | No-op devtools transport stub for B1 builds (matching `backends/macos/backend/devtools_off.tarn`). |
| `backends/cairn/backend/b1_test.tarn` | Automated test suite verifying `DamageRegion` tile alignment/coalescing, idle zero-work invariants, deterministic headless PNG hash (`685fdc263df05dc73a8abb9e50c69744e17ef2ecfdc73f32cee0e11fce34aa59`), 1080p hover damage budget, and live macOS host window + `ABI 14` contract. |
| `examples/cairn_b1_rects/` | Interactive B1 demo application (`main.tarn`, `README.md`) supporting live window mode, `--headless [path]`, `--bench`, and `--idle-check`. |
| `scripts/build-cairn-b1.sh`, `scripts/run-cairn-b1.sh`, `scripts/test-cairn.sh` | Build, run, and test scripts for Milestone B1. |

---

## 2. Measurements vs. Milestone B1 Budget Gate

Measured on macOS (`x86_64`, Darwin 25.6.0) under `nice -n 10` resource limits while background workloads were active:

| Metric | Plan / ADR 0009 Budget Gate | Measured Result (`cairn_b1_rects`) | Status |
|---|---|---|---|
| **First frame time (window resolution `960×600`)** | `< 50 ms` (`50,000 µs`) | **`8.07 ms`** (`8,070 µs` live macOS host surface + first frame) / **`17.5–18.3 ms`** cold headless including LCG wallpaper generation | **PASS** (~6× under budget) |
| **First frame time (`1920×1080` cold start)** | `< 50 ms` B1 target / `< 100 ms` ADR 0009 app start | **`39.5–41.7 ms`** (`39,551 µs` best / `41,682 µs` typical cold 1080p wallpaper + shadow LUT + glass window render) | **PASS** |
| **Single-card hover damage redraw at `1920×1080`** | `< 1.5 ms` (`1,500 µs`) | **`0.66 ms`** (`655–756 µs` min, **`827–869 µs`** with live window damage flush, `1` dirty tile-aligned rect) | **PASS** (~2× under budget) |
| **Idle redraws when pointer stops** | `0 redraws` | **`0 redraws`**, **`0 CALayer presents`**, `0` dirty rects when pointer is stationary or moves without changing hovered target | **PASS** |
| **Idle CPU cost (steady state)** | `~0% CPU` | **`0.4%` thread CPU** (`6.09 ms` over `1,501 ms` wall clock while blocked in `nextEventMatchingMask:`, `0` frames rendered) | **PASS** |
| **Headless `snapshot_png` determinism** | Verified deterministic SHA-256 | **`685fdc263df05dc73a8abb9e50c69744e17ef2ecfdc73f32cee0e11fce34aa59`** (`960×600` RGBA PNG, verified across repeated runs in `test-cairn.sh`) | **PASS** |

---

## 3. Deviations & Design Notes

1. **Linux Surface (`surface_linux.tarn`)**: As permitted by the B1 task specification on this macOS host, `surface_linux.tarn` is a documented compile-time stub for the interactive Wayland `wl_shm` presenter while providing full headless buffer rendering for CI snapshots.
2. **Hierarchical Layer Caching (`wallpaper` + `window_base`)**: To achieve `< 0.9 ms` single-card hover redraws at `1920×1080` in pure CPU Tarn without SIMD intrinsics, `SceneB1` caches both the static `Wallpaper("lago")` canvas and the static `.glass.window` backdrop canvas (`window_base`, invalidated only when theme mode, accent color, or transparency changes). Hover/active card transitions blit only the damaged `32×32`-aligned card bounding box from `window_base` and rasterize the affected card's 9-slice shadow and SDF geometry.
3. **SWAR 64-bit Blending**: Interior horizontal spans of translucent rounded rectangles and 9-slice shadow edges are blended two pixels at a time using 64-bit SIMD-Within-A-Register (`u64` parallel RB and GA channel multiplication), while opaque spans use libc `memset_pattern4`.

---

## 4. Observed Tarn Language & Stdlib Gaps (With Minimal Reproducers)

Per `cairn/docs/design/tarn-ui-cairn-backend-plan.md` §6 and the project rules, the compiler (`../tarn`) was not modified. The following language/stdlib gaps were encountered and worked around cleanly in pure Tarn:

### Gap 1: `if` is a Statement, Not an Expression (`E1002`)
- **Context**: Selecting between two colors or scalar values inline (e.g. inside a struct literal field initializer).
- **Minimal Reproducer**:
  ```tarn
  fn pick(flag bool) i32 {
      x := if flag { 1 } else { 2 } // error[E1002]: expected expression, found `if`
      return x
  }
  ```
- **Workaround Used**: Declare `var x = 2` followed by `if flag { x = 1 }` before the struct literal or return statement.

### Gap 2: `collections.Map.get` Returns `&V` (Aborts on Missing Key) Instead of `Option<&V>` (`E3020`)
- **Context**: Looking up a non-`Copy` widget `Part` struct in `parts: collections.Map<string, Part>`. Because Tarn forbids storing references inside `Option` (`E4203`), `Map.get(&key)` returns `&V` directly and panics if absent, while `collections.get_copy` only works for `V: Copy`.
- **Minimal Reproducer**:
  ```tarn
  import "collections"
  struct Node { name string }
  fn check(m &collections.Map<string, Node>, k &string) {
      match m.get(k) { // error[E3020]: a variant pattern cannot match a value of type `Node`
          Some(n) => {}
          None => {}
      }
  }
  ```
- **Workaround Used**: Guard lookups with `if self.parts.contains(key)` before calling `self.parts.get(key)` or `self.parts.get_mut(key)`.

### Gap 3: Portable SIMD Intrinsics (`u8x16` / `i32x4` / `f32x4`) Not Yet in Stdlib
- **Context**: Alpha-blending premultiplied BGRA8 spans and evaluating per-pixel SDF coverage at 1080p on the CPU.
- **Workaround Used**: 64-bit SWAR (`u64` two-pixel parallel channel multiply) in `backends/cairn/backend/render.tarn` (`blend_span`) plus analytical span splitting so `sqrt()` is only evaluated on rounded corner arcs (`4 × r²` pixels per rect) and skipped along straight edges and interior fills.

### Gap 4: Struct-Returning `objc_msgSend` (e.g. `NSPoint` / `NSRect` Return Values)
- **Context**: Calling `[NSEvent locationInWindow]` returns an `NSPoint` (`{CGFloat x, CGFloat y}`) in registers `xmm0`/`xmm1` (or `d0`/`d1`), whereas the single `objc_msgSend` declaration returns `*mut u8` in `rax`/`x0`.
- **Workaround Used**: Parse `location=(x,y)` from `[event description]` in `backends/cairn/backend/native.tarn` (`parse_event_point`), converting AppKit bottom-left window coordinates to top-left scene coordinates without needing a second colliding `extern "C" fn objc_msgSend` symbol declaration.

### Gap 5: `ffi.read` Supports Only Scalars and Raw Pointers (Not Structs)
- **Context**: Reading a cached `AtlasGlyph` struct directly from a `*AtlasGlyph` pointer in the `GlyphAtlas` fast-path lookup (`draw_shaped_run` in `backends/cairn/backend/text.tarn`).
- **Minimal Reproducer**:
  ```tarn
  import "ffi"
  struct Entry { x i32, y i32 }
  fn read_entry(v &Vec<Entry>, idx usize) Entry {
      p := ffi.vec(v)
      unsafe { return ffi.read(ffi.offset(p, idx)) } // error: backend not implemented: ffi.read supports only scalars and raw pointers
  }
  ```
- **Workaround Used**: Check the `u64` key via `ffi.read(ffi.offset(fk_ptr, slot))` and, on hit, read the `AtlasGlyph` struct via `atlas.fast_vals.as_slice()[slot]`.

---

## 5. What Was Built in Milestone B2

Milestone B2 adds pure-Tarn typography, Lucide vector icons, Cairn brand marks, and the Onboarding Welcome + BrandSheet demo over the B1 procedural `Wallpaper("lago")` and `.glass.window` backdrop.

### 5.1 Subsystems & Files Added / Updated

| Path | Responsibility |
|---|---|
| `backends/cairn/fonts/` | `BricolageGrotesque.ttf` (`--font-display`) + `BricolageGrotesque-OFL.txt`, `Onest.ttf` (`--font-body`) + `Onest-OFL.txt`. |
| `backends/cairn/icons/Lucide-ISC.txt` | ISC license file for the Lucide vector icon set. |
| `backends/cairn/backend/text.tarn` | Pure-Tarn TrueType/OpenType parser (`head`, `hhea`, `maxp`, `hmtx`, `loca`, `glyf`, `cmap` formats 4/12, `kern`, `GPOS` PairPos formats 1/2, `GSUB` `tnum` lookup), UTF-8 decoder with Spanish accents (`á é í ó ú ñ ü ¡ ¿`), analytical scanline coverage glyph rasterizer with 4× horizontal subpixel phase bins, shelf-packed `GlyphAtlas` with 2048-slot direct-mapped fast cache, ellipsis truncation, and UAX #14 paragraph layout (`text-wrap: balance`). |
| `backends/cairn/backend/icons.tarn` | Pure-Tarn Lucide vector path/stroke SDF rasterizer cached in `GlyphAtlas`, plus `draw_cairn_mark` (3 rotated stones + sun dot), `draw_cairn_icon` (`96×96` squircle + ridge + radial glow), `draw_cairn_logo`, and `draw_app_icon` (`23%` squircle). |
| `backends/cairn/backend/scene_b2.tarn` | Retained B2 scene rendering the Onboarding `SetupWindow` + `Sidebar` (`Paso 1 de 8`, `Espacio estimado 5.2 GB` `StorageMeter`) + `StepWelcome` (`"Bienvenido a Cairn"`, `"Tu sistema. Tus reglas."`, 3 feature chips, `"Comenzar"` CTA) and interactive `BrandSheet` (`"Identidad de marca"`). |
| `backends/cairn/backend/native.tarn` | Wired `SceneB2` into `Native` when title starts with `"Cairn B2"`, avoiding unused `SceneB1` full-frame initialization. |
| `backends/cairn/backend/b2_test.tarn` | Automated B2 tests: font loading, Spanish UTF-8/cmap, `tnum`, 4× subpixel atlas caching, truncation, balanced paragraph layout, Lucide icon caching, deterministic PNG SHA-256 hashes, B2 budget gates, and live macOS host window check. |
| `examples/cairn_b2_welcome/` | Interactive B2 demo (`main.tarn`, `README.md`) supporting live window, `--brand`, `--headless`, `--headless-brand`, and `--bench`. |
| `scripts/build-cairn-b2.sh`, `scripts/run-cairn-b2.sh`, `scripts/build-cairn-b1.sh` | Build and run scripts for Milestone B2 (and updated B1 build copy list). |

---

## 6. Measurements vs. Milestone B2 Budget Gate

Measured on macOS under `nice -n 10` (`nice -n 10 bash scripts/run-cairn-b2.sh --bench`):

| Metric | Plan / ADR 0009 Budget Gate | Measured Result (`cairn_b2_welcome`) | Status |
|---|---|---|---|
| **Cold font load (`Onest` + `Bricolage Grotesque`)** | Part of `< 80 ms` cold start | **`1.06 ms`** (`1,057 µs` Welcome / `974 µs` BrandSheet) | **PASS** |
| **Text layout (`SceneB2` Welcome)** | Part of `< 80 ms` cold start | **`0.36 ms`** (`359 µs`) | **PASS** |
| **Cold glyph + icon rasterization** | Part of `< 80 ms` cold start | **`3.19 ms`** (`3,191 µs`) | **PASS** |
| **Cold font load + first frame (`960×600` headless)** | `< 80 ms` (`80,000 µs`) | **`17.61 ms`** (`17,606 µs`) | **PASS** (~4.5× under budget) |
| **Live macOS host window + first frame (`960×600`)** | `< 80 ms` (`80,000 µs`) | **`18.82 ms`** (`18,819 µs`) | **PASS** (~4.2× under budget) |
| **Cached 1,000+ glyph & icon draw (`1,080` items)** | `< 1 ms` (`1,000 µs`), `0` atlas allocs | **`0.70 ms`** (`697–742 µs`), `new_atlas_allocs = 0` | **PASS** |
| **CTA button hover damage redraw** | `< 1.5 ms` (`1,500 µs`) | **`0.10 ms`** (`102 µs` headless / `910 µs` live window, `1` dirty rect) | **PASS** |
| **Idle redraws / presents** | `0` redraws | **`0` redraws**, **`0` presents** | **PASS** |
| **Headless `snapshot_png` determinism** | Verified deterministic SHA-256 | Welcome: `58e36e299885c2bca88de779695d3ce66d97ca8f2ee5710892f8c01690a8ecba`<br>BrandSheet: `8d7cdb02798d7ea05f173c781926974fe922aa8f2dcd190403636fa978ba6fa5` | **PASS** |
