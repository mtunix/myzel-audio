# Shared code (`common/`)

Everything both plugins use: the UI kit, the two-view editor, presets and A/B, session state, a
few DSP blocks and the test helpers. A plugin keeps only what is its own: DSP, parameters,
layout, colours, its displays (Umber's VU meter, Cerulean's tail display) and its texts.

Includes read `common/<area>/<File>.h`. Namespaces: `myzel::ui`, `myzel::plugin`, `myzel::dsp`,
`myzel::testing`. Each plugin's `ui/Theme.h` imports `myzel::ui`, so plugin UI code uses the
kit unqualified.

## Build

All targets are source-only (CMake `INTERFACE` libraries): each plugin compiles them with its
own JUCE configuration, exactly like its own sources, so JUCE is never linked twice.

| Target | Contents |
|---|---|
| `MyzelDsp` | `dsp/`: header-only blocks (TPT filters) |
| `MyzelPlugin` | `plugin/`: undo, presets, A/B, UI state keys, session state |
| `MyzelUiAssets` | the Inter typeface (SIL OFL 1.1, `ui/assets/fonts/OFL.txt`) |
| `MyzelUi` | `ui/`: theme, widgets, header, two-view editor, studio controls |

`cmake/MyzelPlugin.cmake` defines `myzel_add_plugin()`, which turns a `plugins/<name>/`
folder into `<Name>JuceConfig`, `<Name>Dsp`, `<Name>Shared` and the plugin (VST3, LV2, CLAP,
Standalone) with the vendor IDs from the root `CMakeLists.txt`, and adds its `tools/` and
`tests/`. A plugin's `CMakeLists.txt` is its `project()` plus one call.

## Theme: one kit, each plugin's colours

`ui/Theme.h` declares what the kit draws with; each plugin **defines** it in its `ui/Theme.cpp`
(one plugin per binary, so there is exactly one definition):

- `Theme::palette()`: the kit colours (`Palette`). One accent (`accentCore/accent/accentDeep`),
  reserved for things that are on. Caution (`caution`, `cautionDeep`) only for Bypass; it never
  means "on" and must differ from the accent (Cerulean: amber on ice blue; Umber: signal red on amber).
- `Theme::look()`: the panel finish (`PanelLook`): light falloff, grain, vignette, tick shadow
  (printed ink casts a shadow below, an engraved cut a dark edge above), hover ring strength,
  and the LED/label positions of tall keys.
- `Theme::studio()`: the studio view's colours (`StudioPalette`, `ui/StudioTheme.h`).

Colours of a plugin's own displays stay in the plugin (`Theme::extras()`). Drawing code defines
no colour literals except neutral black/white shading alphas.

`metrics` holds what every plugin shares (header height, scale range, shadow direction, type
sizes, knob sweep, fade times); each plugin adds its base size and layout grid.

## Widgets

| File | Role |
|---|---|
| `Drawing` | cached layers, the 4-unit grid snap, legends, screws, recesses, grooves, shadows, grain, glow |
| `PanelArtwork` | the static front panel from a spec (knob scales, wells, grooves, legends) |
| `Knob` | rotary control: drag/fine/wheel/keys/typed entry, re-bindable, accessibility |
| `LampButton` | console key with an LED bar (square, wide, compact; latching or momentary) |
| `ValueBubble` | the value read-out while hovering or dragging (above large knobs) |
| `HeaderButton`, `PresetDisplay`, `HeaderBar` | the header rail (`HeaderConfig` per plugin) |
| `InlineDialog` | in-editor prompts (save, rename, delete) |
| `KitLookAndFeel` | popup menus, tooltips, text fields |
| `LockBadge` | the padlock after Mix while the mix lock is on |

The header is the same in every plugin, and the editor shows one header over both views: name
and version, preset browser (SAVE appears in the field with unsaved changes), Bypass, A/B,
undo/redo, PANEL | STUDIO (plugins with two views) and ⚙. The ⚙ menu holds the plugin's own
settings (`HeaderConfig::addSettings`, item IDs from `firstPluginItemId`: JUCE runs an item's
action only if it has an ID), the mix lock and the window size. `HeaderConfig` also carries the
mix lock's texts (menu label, padlock tooltip).

## Two views

`PluginEditor` hosts one `EditorView` at a time and scales it (75–200 %, fixed aspect ratio).
It owns the `EditorController` (presets, A/B, undo, parameter access, the stored view), which
outlives the views; the plugin passes a factory that builds the panel or the studio view, and
its `HeaderConfig`. The editor also owns what both views show the same way, in one scaled
layer over the view:

- the `HeaderBar` over the view's top strip (the view still paints the header's shadow);
- the `LockBadge`, centred where the view's `getMixLockAnchor()` says and inked for the view
  (`legendDim` on the panel, `textDim` in the studio), visible while the mix lock is on;
- the `InlineDialog`, on top of everything;
- the frame clock: one `VBlankAttachment` that clamps the frame time (at most 0.1 s), calls the
  view's `animateFrame (dt)` and refreshes the header every 6 frames;
- `PresetManager::onChange` (header and padlock) and the session listener: a session restored
  by the host refreshes the header, calls the view's `sessionRestored()` and then follows the
  session's view.

A view keeps only its own controls and per-frame work; it calls `refreshHeader` when it changes
something the header shows. Switching is immediate (the header is not part of the view it
replaces), never resizes the window, and the header keys keep their places. The session stores
its view (`uistate::view`); a new session opens in the view chosen last (`ViewMode`,
`<plugin>.settings`).

The studio view is built from `StudioControls` (`StudioSlider` rows with typed entry,
`StudioToggle` chips/switches/blocks/segments, `SelectorRow` lists), `StudioMeters` (level bars
plus an optional gain-reduction bar, configured per plugin) and `studio::drawCard`. Card layout
is the plugin's; the grid (12 px gutters, selector column left, output column right) is shared.

## Presets, A/B, session

- `PresetHost`: what the processor offers (state, undo, UI state) and its hooks (preset applied,
  current preset changed, mix lock changed).
- `PresetFormat`: what a plugin's presets are (name, XML tag and version, parameter list,
  lockable parameters, factory snapshots). Tags and IDs are saved in files and sessions: never
  rename them.
- `PresetManager`: factory and user presets, one undo transaction and host gestures per load,
  the mix lock. `AbCompare`: two snapshots in the UI state.
- `ParameterUndoRecorder`: records user edits (gestures) only, never automation.
- `sessionState`: write/read/restore the APVTS tree with a version; restore also fixes raw host
  values on switches that `replaceState()` would skip.
- `UiState.h`: the shared UI state keys (scale, view, preset name, A/B, mix lock).

## Tests and tools

Each plugin's `tools/ui_snapshot` is a snapshot renderer and an editor self-test
(`--selftest`), built on header-only helpers in `testing/`:

| File | Role |
|---|---|
| `UiTestKit` | `check`, mouse events and drags, gesture recorder, parameter helpers, `saveSnapshot` |
| `ScratchSession` | redirects every file written for the user (user presets, the default-view setting) to a fresh temp folder, and deletes it afterwards. Create it before any processor: the `PresetManager` scans its folder when it is built |
| `SnapshotTool` | the tool's `main()` (`runSnapshotTool`): arguments, `--selftest` (exit 1 on a failure), the 8× `--leakcheck`, scenes and timing, all in a scratch session. The plugin brings its scenes, meter seeding and self-test |
| `EditorSelfTest` | the checks of shared editor code, which both self-tests run (`runEditorChecks`): user presets land in the scratch folder, A/B, the mix lock, and the two views (switching, header keys, session and default view, following a restored session) |

Checks of a plugin's own UI stay in its `SelfTest.cpp`. CI runs both self-tests on Linux.
`tools/ui_diff.py` renders a plugin's snapshots and compares them pixel by pixel with a recorded
baseline: run it before and after any change to shared UI code, for **both** plugins.
