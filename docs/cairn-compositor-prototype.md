# Cairn Compositor Prototype — Architecture & Phase 11 Mapping

Status: Prototype (pure Tarn on `backends/cairn`, hosted inside a macOS window)  
References: `cairn/docs/adr/0009-desktop-compositor.md`, `cairn/docs/design/desktop-spec.md`

---

## 1. Overview

The **Cairn Compositor Prototype** (`backends/cairn/backend/compositor.tarn`, `examples/cairn_compositor/`) runs the Cairn desktop compositor inside a single macOS host window (`"Cairn Compositor · Prototype"`), hosting multiple independent `tarn_ui` client applications (`Counter`, `Widgets`, and `B5 Onboarding`) behind an explicit **client/compositor protocol boundary**.

Every compositor responsibility from **ADR 0009** runs in pure Tarn:
- **Buffer ownership transfer** (no copies between client commit and compositor scanout/composition).
- **Per-workspace window placement policies**: **Floating** (free move/resize, z-stacking, minimize/maximize, traffic lights) and **Tiling** (Hyprland/Omarchy **dwindle** BSP with configurable gaps and active focus borders), with spring-animated (`340/30`) policy transitions.
- **Workspaces & compositor-owned shell surfaces**: 4 workspaces with animated switching, floating `.glass` Topbar, magnified Dock (`170/14, mass 0.1`), and `Ctrl+K` / `Super+Space` Universal Launcher.
- **Strict input isolation**: pointer and keyboard events are dispatched **only** to the focused client in client-local coordinates; unfocused clients never see each other's input.
- **End-to-end damage aggregation**: client damage rects + compositor animation damage coalesce into a single `DamageRegion`, returning `0` redraws and `0%` CPU when idle.

---

## 2. Client / Compositor Protocol Boundary & Phase 11 IPC Mapping

In the prototype, clients run in-process for portability on macOS, but interact with `Compositor` strictly through **owned buffer handoff** (`SurfaceBuffer` moved by value, never shared mutably):

| Prototype Boundary (`compositor.tarn`) | Phase 11 Cairn IPC (`cairn` Microkernel) |
|---|---|
| `create_surface(id, title, w, h) -> SurfaceBuffer` | `ipc_call(COMPOSITOR_EP, CREATE_SURFACE, ...)` returning an owned SHM capability |
| `commit_surface(id, buf: SurfaceBuffer, dmg: DamageRegion)` | `ipc_send(COMPOSITOR_EP, COMMIT, move(shm_cap), dmg_rects[0..16])` (ADR 0002 ownership transfer: client loses capability on send, preventing use-after-present) |
| `acquire_surface_buffer(id) -> Option<SurfaceBuffer>` | Compositor returns released `shm_cap` on `FRAME_DONE` / vblank event |
| `poll_client_input(id) -> Vec<ClientInputEvent>` | Compositor pushes `Pointer` / `Key` / `Focus` events only to the focused client's input endpoint |

Because Tarn's ownership rules enforce move-only transfer of `SurfaceBuffer` into `commit_surface`, **use-after-present** is prevented at compile time in both the prototype and the Phase 11 IPC service.

---

## 3. Window Policies & Springs

Each `Workspace` (`0..3`) stores its own `Policy`:
- **`POLICY_FLOATING`**: Windows retain user-dragged `(x, y, w, h)` with traffic lights, 9-slice drop shadows (`NineSliceShadow`), and `22px` rounded corners.
- **`POLICY_TILING` (Dwindle BSP)**: Available work area (inset by Topbar, Dock, and outer gaps) is recursively split:
  - Window 0 takes the left half (or full area if 1 window); window 1 takes top-right; window 2 takes bottom-right-left, etc., alternating horizontal and vertical splits with `10px` inner gaps and `2px` accent focus borders.
- **Live Policy & Workspace Transitions**: Switching `Floating ↔ Tiling` or changing workspaces recomputes target rects and retargets each window's `LayoutSpring` (`stiffness: 340, damping: 30`) from its current position/velocity without jumping.

---

## 4. Mapping to Phase 11–12 GPU Driver Capabilities

When Cairn OS boots on **QEMU `virtio-gpu`** (Phase 11) and **Intel Iris Xe** (`anv` Vulkan / `i915`, Phase 12):
1. **Buffer Backing**: `SurfaceBuffer.canvas` (`Vec<u8>` BGRA8) maps to a `virtio-gpu` dumb buffer / GEM handle (Phase 11) or `dmabuf` / `VkImage` with `VK_KHR_external_memory_fd` (Phase 12).
2. **Composition Pass**: `Compositor.draw_tile(tile)` maps to a `VkRenderPass` drawing:
   - Cached `Wallpaper` background + Dual-Kawase `.glass` offscreen pyramid (`vkCmdDispatch` / fragment pass).
   - Per-window quads clipped to `rounded_rect` SDF in the fragment shader + 9-slice shadow instancing.
   - Hardware cursor plane (`DRM_PLANE_TYPE_CURSOR`) for zero-composition cursor motion.
3. **Damage & Pacing**: `DamageRegion` rects map directly to `VK_KHR_incremental_present` (`VkPresentRegionKHR`) and DRM/KMS `FB_DAMAGE_CLIPS` driven by vblank page-flip events.
