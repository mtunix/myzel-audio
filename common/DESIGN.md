# Shared code (`common/`)

Everything the plugins share: the UI kit, the two-view editor, presets and A/B, session state,
DSP blocks and the test helpers. A plugin keeps only what is its own: DSP, parameters,
layout, colours, its displays (Umber's VU meter, Cerulean's tail display, Mulberry's echo map)
and its texts.

Includes read `common/<area>/<File>.h`. Namespaces: `myzel::ui`, `myzel::plugin`, `myzel::dsp`,
`myzel::testing`. Each plugin's `ui/Theme.h` imports `myzel::ui`, so plugin UI code uses the
kit unqualified.

## Build

All targets are source-only (CMake `INTERFACE` libraries): each plugin compiles them with its
own JUCE configuration, exactly like its own sources, so JUCE is never linked twice.

| Target | Contents |
|---|---|
| `MyzelDsp` | `dsp/`: header-only DSP blocks (below) |
| `MyzelPlugin` | `plugin/`: undo, presets, A/B, UI state keys, session state |
| `MyzelUiAssets` | the Inter typeface (SIL OFL 1.1, `ui/assets/fonts/OFL.txt`) |
| `MyzelUi` | `ui/`: theme, widgets, header, two-view editor, studio controls |

`cmake/MyzelPlugin.cmake` defines `myzel_add_plugin()`, which turns a `plugins/<name>/`
folder into `<Name>JuceConfig`, `<Name>Dsp`, `<Name>Shared` and the plugin (VST3, LV2, CLAP,
Standalone) with the vendor IDs from the root `CMakeLists.txt`, and adds its `tools/` and
`tests/`. A plugin's `CMakeLists.txt` is its `project()` plus one call. `GENERIC_EDITOR` builds a
plugin on JUCE's generic editor without `MyzelUi`, for a scaffold that has no `ui/Theme.cpp` yet
(Viridian until M4).

## DSP blocks

Header-only, no JUCE, namespace `myzel::dsp` (`analytic::` and `tubes::` group two families).
A block moves here from a plugin when a second plugin needs it; a move never changes a
sibling's sound (its golden files stay bit-identical).

| File | Contents | Used by |
|---|---|---|
| `Filters` | double TPT filters: SVF, one-pole, DC blocker, tilt, emphasis shelf | Umber, Mulberry, Inkcap (Glitch's Chew) |
| `Blocks` | float blocks: delay line (linear, cubic Lagrange), Schroeder allpass and allpass chain, one-pole and SVF, LFOs (sine, smooth random), smoothers and glides, envelope follower, PRNG | Cerulean, Mulberry, Inkcap (Room) |
| `ReverbBlocks` | three-band decay filter (Jot, second-order shelves, exactly transparent at gain 1), per-pass band gains from RT60, orthonormal Hadamard / Householder mixing | Cerulean; Inkcap (Room) |
| `EarlyReflections` | a seeded stereo early-reflection cluster: sparse multi-tap per channel, cross taps, gliding with Size | Cerulean (Hall); Inkcap (Room: Closet, Room, Hall) |
| `Fdn` | N-line feedback delay network with per-line decay filters, gliding lengths, freeze; its own LFOs or the caller's modulation per line (for a song-position-locked tank) | Cerulean (Hall, Rise, Choir, Halo, Echo, Billow); Inkcap (Room, Hall: 8 lines) |
| `DispersionCascade` | a stretched first-order allpass cascade, four lanes at once (a spring's chirp) | Cerulean (Spring); Inkcap (Room's Spring) |
| `GrainVoice` | one voice of the granular pitch shifter (WSOLA-aligned grains, power-normalised) | Cerulean (Halo), Mulberry (Clean's Shift) |
| `Interpolators` | 12-tap Kaiser-windowed sinc for fractional reads (every fraction the same DC gain, never above unity), a ring buffer with a mirrored guard so every window is contiguous | Mulberry (every machine's reads); Inkcap (`CaptureBuffer`: Glitch, Warble) |
| `JilesAtherton` | the Jiles–Atherton tape core, RK4 in field space (validated in Umber's `models/reel/NOTES.md`) | Umber (Reel); Mulberry (Loop, Reel) uses its equations, parameters and Langevin function with its own 8x solver (`TapeCore`) |
| `TapeCore` | the same Jiles–Atherton equations solved fast: tabulated Langevin, RK2 sub-steps of at most 0.2 in field | Mulberry (Loop, Reel at 8x); Inkcap (Crush's Tape and Cassette at 2x) |
| `TapeLosses` | a playback head's gap, spacing and thickness losses (Bertram) and contour bump as fitted low-order filters, matched (Vicanek) direct-form sections, the closed forms | Mulberry (Loop, Reel); Inkcap (Crush's head: its prototypes in TPT form, so Damage can move them) |
| `ClockedDomain` | Holters–Parker's clocked domain: modal filters, a device sampling the input filter's exact output at its own clock instants, its held staircase into the output filter; `setClockPhase` ties the clock to a timeline | Mulberry (Bucket, Rack); Inkcap (Bits) |
| `AnalyticFilters` | linear sections with exact responses: TPT one-pole and SVF, bilinear shelf with an exact inverse | Umber (Reel), Mulberry (Loop, Reel) |
| `Oversampler2x` | per-sample 2x polyphase IIR half-band pair (minimum phase, never gains), 4x/8x cascades, linear-phase half-band FIR | Mulberry (Loop, Reel, Clean); Viridian (the chain's 2x pair, `OpAmpClip`'s FIRs); Inkcap (Crush) |
| `NodalCircuit` | nodal DK solver for a netlist with any nonlinear device (`tubes::`): MNA to discrete state space, Newton on the device ports, adjustable bias supply, compile-time sparsity | Umber (Valve, Fuzz) |
| `TubeMath`, `KorenTriode` | tube currents from shared Hermite tables; Koren's triode with a smooth grid diode (`tubes::`) | Umber (Valve, Fuzz) |
| `TriodeStage` | a common-cathode triode gain stage on `NodalCircuit`, component values from the caller (`tubes::`; validated in Umber's `models/valve/NOTES.md`) | Umber (Valve); Viridian (planned); Inkcap (Crush's Valve) |
| `DkSolver` | nodal DK solver with built-in Ebers–Moll junction and saturating-core ports, state hand-over between rates | Umber (Class A) |
| `ClassACircuit` | single-ended class-A transistor stage into a saturating output transformer, switching-event sub-steps, component values from the caller (validated in Umber's `models/classa/NOTES.md`) | Umber (Class A); Viridian (planned) |
| `MagneticCore` | the Jiles–Atherton core of transformers and inductors, SI units, Langevin table (not the tape core above: different units and solver) | Umber (Desk); Viridian (planned) |
| `TransformerStage` | an audio transformer as a WDF with the nonlinear core at its root (validated in Umber's `models/desk/NOTES.md`) | Umber (Desk); Viridian (planned) |
| `LineAmp` | op-amp line stage: soft rail clip in the loop (exact, tabulated), slew limit, input offset | Umber (Desk) |
| `OpAmpClip` | an IC op-amp stage: LineAmp's loop at the chip's loop gain (exact, tabulated in units of the swing); at 4x the caller's rate, on a linear-phase path that runs only near the rails (the output the same to the bit), the input as an interpolating cubic spline, the rails' clip and the slew limit solved exactly on each segment and integrated under a cubic B-spline, the loop's knee under a triangle, the loop's smooth residue under a triangle near the rails (antiderivative antialiasing); `OpAmpClipLanes<2>` runs two channels in one loop, each the same to the bit as alone | Viridian (Console) |
| `FloatCompare` | `isExactly`: an exact comparison where one is meant (a value that did not move, an exact nominal point), without `-Wfloat-equal` | the solvers above |

The circuit stages take every component value from the caller (`ClassAComponents`,
`TriodeStageComponents`, `TransformerParams`, `LineAmpParams`): a plugin keeps its own voicing
(Umber's in its models' `standard…()` functions) and never forks the solver. The first two have
no values of their own: a field left out is NaN, `complete()` says whether all are set, and
`prepare()` asserts it.

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
  rename them. `stateProperties` adds sound that is not a host parameter (Inkcap's seed): a
  property of the APVTS state tree, stored in presets and A/B, set in the load's undo step, and
  kept while its own lock (a bool in the UI state, `isStateLocked`) is on. Empty for the other
  plugins, whose presets and sessions are unchanged by it.
- `PresetManager`: factory and user presets, one undo transaction and host gestures per load,
  the mix lock. `AbCompare`: two snapshots in the UI state.
- `ParameterUndoRecorder`: records user edits (gestures) only, never automation.
- `sessionState`: write/read/restore the APVTS tree with a version; restore also fixes raw host
  values on switches that `replaceState()` would skip, and gives a stored value that is not
  finite (a damaged session) the parameter's default.
- `UiState.h`: the shared UI state keys (scale, view, preset name, A/B, mix lock).

## Tests and tools

Each plugin's `tools/ui_snapshot` is a snapshot renderer and an editor self-test
(`--selftest`), built on header-only helpers in `testing/`:

| File | Role |
|---|---|
| `UiTestKit` | `check`, mouse events and drags, gesture recorder, parameter helpers, `saveSnapshot` |
| `ScratchSession` | redirects every file written for the user (user presets, the default-view setting) to a fresh temp folder, and deletes it afterwards. Create it before any processor: the `PresetManager` scans its folder when it is built |
| `SnapshotTool` | the tool's `main()` (`runSnapshotTool`): arguments, `--selftest` (exit 1 on a failure), the 8× `--leakcheck`, scenes and timing, all in a scratch session. The plugin brings its scenes, meter seeding and self-test |
| `EditorSelfTest` | the checks of shared editor code, which every self-test runs (`runEditorChecks`): user presets land in the scratch folder, A/B, the mix lock, and the two views (switching, header keys, session and default view, following a restored session) |

Checks of a plugin's own UI stay in its `SelfTest.cpp`. CI runs every self-test on Linux (a plugin
still on the generic editor has no snapshot tool, and CI skips the step for it). Inkcap's glass
face is not a `PluginEditor` (it uses `EditorController`, `InlineDialog`, `KitLookAndFeel` and
`SnapshotTool`), so its self-test checks presets, A/B, undo and the mix lock itself instead of
calling `runEditorChecks`.
`testing/PortableRandom.h` gives seeded uniform and Gaussian test noise that is the same on every
platform. The std distributions differ between libstdc++, libc++ and MSVC; this one follows
libstdc++ draw for draw, so a test signal measured on Linux is the one Windows and macOS see. A
test signal whose measurement sits near a margin uses it (Halite's signals do).

The root `CMakeLists.txt` builds with `-ffp-contract=off` on Clang and GCC: Apple's arm64 Clang
otherwise fuses `a * b + c`, which rounds differently from the Linux and Windows builds and breaks
the bit-exact identity tests.

Test case names are ASCII: ctest hands each name to Catch on the command line, and Windows
re-encodes anything else, so the test matches nothing. Function arguments that read and move the
same object are split into statements: Clang evaluates arguments left to right, GCC and MSVC
right to left.

`tools/ui_diff.py` renders a plugin's snapshots and compares them pixel by pixel with a recorded
baseline: run it before and after any change to shared UI code, for **every** plugin.
