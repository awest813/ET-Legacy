# WebAssembly port status

Updated October 8, 2026 (local time).

The working source is nested inside the downloaded directory. It originated as
an extracted archive; the integrated source is now prepared on the repository's
initial `main` branch. Vendored dependencies include their browser fixes as
regular source files, rather than unresolved Git links. The browser build runs the ET: Legacy engine,
game modules, and Omni-bot AI inside one WebAssembly module. The local HTTP
process serves files; it does not run the game simulation or bots.

## Build and runtime

The Emscripten Release target `etl` builds in `build_wasm`. Its output comprises
`etl.html`, `etl.js`, `etl.wasm`, and `etl.data`. The build uses the SDL2, Ogg,
Vorbis, JPEG, FreeType, PNG, and zlib Emscripten ports. `cmake/ETLOmnibotWasm.cmake`
defines the static bot target, linked with exception support.

Original game assets are installed at
`C:\Users\allen\Downloads\etlegacy-wasm\assets`. The required files are
`pak0.pk3`, `pak1.pk3`, `pak2.pk3`, and `etloose.pk3`. The loose pack contains the
repository's `etmain` assets; the browser mounts it under `/legacy`, with the
original packs under `/etmain`. The generated `ui/version_generated.h` is also
preloaded under `/legacy/ui`, fixing menu version macro parsing errors.

Omni-bot sources are the official jswigart/omni-bot repository at commit
`b3f10f334de625765100bbfeacc5eac95c098607`, under
`vendor/omni-bot-runtime/0.83/Omnibot`. Despite that directory name, the runtime
reports version 0.93. Its GameMonkey scripting dependency is at commit
`2d24f6e500c327aa2e99f3572e8e2f4b285e37f4`. Boost 1.83 Filesystem and Regex are
built with the runtime. Browser data under `vendor/omni-bot-browser` comes from
the official ET: Legacy Omni-bot archive and is preloaded at `/omni-bot`.
Native DLLs are excluded; `omnibot_et.wasm` there is a path placeholder for the
runtime statically linked into the engine.

The vendored Boost Filesystem revision is
`e65ddb6ef21697970f7d1438f7a46c5233940059`; Boost Regex is
`4cbcd3078e6ae10d05124379623a1bf03fcb9350`.

For a fresh checkout, activate Emscripten with `emsdk_env.sh` (or set `EMSDK`),
then run `./build_wasm.sh configure` followed by `./build_wasm.sh etl`.
Local `web-server.json`, downloaded packs, build output, Python environments
and signing keystores are excluded from version control.

Port corrections include GameMonkey member-offset handling for the Wasm ABI,
the noreturn specialization, PhysicsFS executable path handling, and disabling
native interprocess discovery in the browser. The engine interface has private
ownership inside the game module.

On this machine, invoke Ninja with Emscripten on PATH and
`EMSDK_PYTHON=C:\Python314\python.exe`:

```text
ninja -C build_wasm etl -j 8
```

Existing output files have owner-only ACLs. Builds and the preview server were
run through approved processes with access to those files; ACLs were unchanged.

## Renderer corrections

The fixed-function OpenGL shim renders through WebGL2. Earlier black-canvas and
opaque-texture problems were corrected by preserving unsigned vertex alpha,
uploading vertices before indexed draws, updating shader uniforms for all draw
paths, and normalizing sized RGB/RGBA texture formats. ARB programs bind before
uniform updates. Renderer restart recreates private buffers, shaders, matrices,
and texture mappings when SDL creates a new WebGL context. Frames are presented
through requestAnimationFrame.

The deeper audit also fixed alpha comparisons: GLSL expects comparison codes
1 through 8, whereas OpenGL supplies enums starting at 0x0200. Plants, trees,
and fences now render clean cutouts. Clip-plane enable state is tracked, and
client texture-unit queries report the client unit. Deleted texture slots,
shaders, and programs are reclaimed. Shader and program handles have separate
ranges, and the private fixed-function shaders are released after linking.

Render statistics require `ETWEBGL_DEBUG_STATS`; shader and program failures
remain visible. Evidence of corrected cutouts:
`build_wasm/audit-goldrush.jpg` before and `build_wasm/audit-cutouts-fixed.jpg` after.

## Offline match launcher

The launcher offers Oasis, Gold Rush, Battery, Fuel Dump, Radar, and Rail Gun;
0–12 bots; and Easy, Normal, or Hard difficulty. Default play uses four bots,
balanced across both teams. Difficulty values are 2, 4, and 6, with combat
movement skill set to half that value. Both population limits equal the selected
count, so zero bots permits exploration. The engine allows 16 clients.

Settings persist in localStorage. Query parameters `map`, `bots`, and `difficulty`
override valid saved selections, with invalid values falling back to defaults.
Before starting the map, the launcher writes the bot configuration under
`/omni-bot/et/user/omni-bot.cfg`.

The launcher scrolls in short windows and uses one column below 361 pixels.
It includes a keyboard guide, visible focus indicators, and accessible progress.
Default and 360×480 viewports were checked. Evidence:
`build_wasm/audited-launcher.jpg` and `build_wasm/offline-match-settings.jpg`.

## Assets and loading lifecycle

`python misc/web/audit_assets.py` validates all 5,162 ZIP entries in the four
installed packs, totaling 241,663,372 bytes. It checks CRC errors, duplicate and
unsafe paths, loose-pack freshness against repository files, six BSP/navigation/
script bundles, font coverage, and accidental native bot binaries. All checks
passed. Details: `build_wasm/asset-integrity-audit.json`.

The preview serves `/assets/manifest.json`, containing complete pack sizes,
SHA-256 hashes, and CRC32 checksums after ZIP entry validation. The manifest is
cached until file size or modification time changes. Missing or invalid source
packs return HTTP 503. The browser checks complete file size and CRC32 to detect
accidental corruption; it does not authenticate assets cryptographically.

Pack verification reads 8 MiB chunks, updates progress, and yields between
chunks. Changed or corrupted cached packs are downloaded again; corrupted
downloads are rejected and removed from the cache. Verified packs are linked
into the engine filesystem, avoiding a second 228 MB copy of pak0. Unchanged
assets do not cause another complete IndexedDB write. A verified saved manifest
can support play when the local asset server becomes unavailable.

Cache mount/read/quota failures permit in-memory play. Cache operations have a
15-second deadline. If a cache read stalls, downloads use an isolated in-memory
directory so its late completion cannot overwrite active assets. Manifest and
pack requests also have deadlines. Loading states and attempt epochs reject late
callbacks after failure and duplicate runtime callbacks. Failures display a
retry action; startup cannot continue behind the error screen.

## Restart and bot state

Static Wasm game/UI globals persist across map changes and restarts. Cgame and
UI initialization reset their display context and loading panels, preventing
calls through freed font callbacks. Loading-panel widescreen offsets are undone
before setup, and limbo/debrief panels apply only the change in offset.

Failed browser bot initialization releases partial manager state before the
engine interface is freed. Shutdown clears the bot manager pointer and resets
the bot filesystem's initialized state. Native DLL unload cannot provide this
cleanup for a statically linked browser runtime.

Recovery was exercised live by selecting a missing bot path and restarting
Gold Rush. The game continued after the initialization error. Restoring
`/omni-bot` and restarting loaded the runtime again with four bots, two on each
team. Evidence: `build_wasm/audit-bot-failure.jpg` and `audit-bot-recovery.jpg`.

Live checks covered Oasis → Battery → Fuel Dump → Rail Gun → Gold Rush → Radar
with four bots, plus UI and renderer/context restart on Rail Gun. Bots loaded
waypoints and scripts for all six maps. Battery bots constructed the beach MG
nest. Earlier checks verified six Hard bots on Gold Rush and Radar, restarting
Radar with its population intact, and zero-bot Easy exploration on Gold Rush.
The bots captured the Forward Bunker and Old City, built command posts and MG
nests, and fought during the running matches. The human player joined Allies
and spawned in first-person gameplay with a textured scene and HUD.

Evidence: `build_wasm/offline-bots-gameplay.jpg`, `six-bots-hard-verified.jpg`,
`bot-map-change-verified.jpg`, `bot-map-restart-verified.jpg`,
`zero-bots-verified.jpg`, `team-menu-before.jpg`, `team-menu-after-map-change.jpg`,
`audit-battery.jpg`, `audit-fueldump.jpg`, `audit-railgun.jpg`, and `audit-radar.jpg`.

## Browser mouse focus and sound

The top bar shows menu, console, gameplay capture, and audio state. Click the
canvas or **Capture mouse** for normal mouse-look; requests only occur during
that gesture. Menus and the console release capture. Menu pointing follows the
actual canvas position, including CSS scaling and widescreen layouts. Team and
Medic selection were verified by clicking their displayed buttons on Oasis.

If the browser denies pointer lock, hold the **right mouse button** and drag to
aim, with **left click** to fire. Releasing the right button, leaving the canvas,
opening menus, or losing browser focus clears held actions. This fallback is
bounded by the canvas, so a desktop browser with pointer lock gives more freedom.
Escape releases capture; Tab retains the normal scoreboard binding.

PLAY primes SDL's shared Web Audio context before asynchronous asset loading.
The top bar provides mute/unmute and a saved volume slider. Sound fades to zero
while the canvas lacks focus or the page is hidden, then returns when focused.
The output gain reconnects after sound-device restarts without doubling output.
Permission rejection remains recoverable through **Enable sound**. A fatal
startup closes the primed context. No new audio dependency is required.

Live browser checks verified the SDL stereo/44.1 kHz backend, running Web Audio
state, mute/unmute, 0% and 65% volume, saved volume after reload, real
`snd_restart` recovery, and menu pointing. The embedded browser
still denies pointer lock; its denial path remains playable through drag aim.
The fault suite covers capture denial/timeouts, late permission callbacks,
drag flags/release, focus loss, audio
resume rejection, single output wiring, volume ramps, and sound-device restart.
Normal desktop pointer lock and audible output need a manual hardware check.
Build log: `build_wasm/browser-input-audio-build.log`.

## Regression checks

```text
node misc/web/test_launcher.cjs
python misc/web/test_serve.py
python misc/web/audit_assets.py
emcc misc/web/test_resources.c -Isrc/webgl -O2 -sENVIRONMENT=node -sWASM_ASYNC_COMPILATION=0 -o build_wasm/test_resources.cjs
node build_wasm/test_resources.cjs
```

The HTTP suite has seven checks, including real ZIP corruption, manifest refresh,
route containment, and missing files. Launcher cases cover same-size final-byte
corruption, bad downloads/manifests, read failures, cache faults/timeouts,
late requests after failure, offline manifest fallback, duplicate initialization,
retry behavior, option validation, and engine errors.

The resource harness includes the actual shim and mocks only native WebGL calls.
It passed 24,573 texture allocations, 100 shader/program lifecycles, all eight
alpha comparisons, invalid alpha enums, reference clamping, and clip enable state.
Final build log: `build_wasm/asset-state-audit-verified-build.log`.

## Local preview and remaining verification

Run `python misc/web/serve.py 8081` from this source directory, then open
<http://localhost:8081/>. Set `ETWASM_ASSETS` to override the pack directory.
The server binds to `127.0.0.1` by default; `ETWASM_BIND` can select another
interface when intentionally needed. Routes expose engine output, the four
packs, and their manifest. Directory listings, traversal, and arbitrary logs
are rejected.

- The embedded browser denies pointer lock. Drag aim is available; normal
  captured mouse-look should be checked in a desktop browser.
- Audible output and profile persistence require further checks. Browser online
  transport and retry are verified; a full match against a compatible ET server
  still needs verification (see the online networking section below).
- Optional GeoIP and platform-manifest files are absent. Some optional alternate
  crosshair textures are absent from the supplied packs.
- C90 declaration warnings, vendor template warnings, and the deprecated
  WASM_BIGINT linker setting remain; the Release build completes successfully.

Internet access is unnecessary during gameplay once the local engine files,
bot data, and original game packs are loaded.

## Candidate projects and other game ports

Research checked October 1, 2026. These are candidates for evaluation; this
research did not add runtime dependencies or change the working game build.

| Project | License | Potential use in this port |
| --- | --- | --- |
| [WASM Game Framework](https://github.com/BuiltByTed/wasm-game-framework/) | Repository declares MIT | Closest browser-shell reference: loading, save/config persistence, input capture, viewport handling, and PWA lifecycle. Its README identifies an Enemy Territory deployment. The framework supplies the browser lifecycle, while the engine supplies an adapter. |
| [GL4ES](https://github.com/ptitSeb/gl4es) | [MIT](https://github.com/ptitSeb/gl4es/blob/master/LICENSE) | Legacy OpenGL translation with an Emscripten build. Evaluate as an alternative to the custom fixed-function shim, especially state handling, shader translation, alpha tests, and texture lifetime. |
| [webgl-lint](https://github.com/greggman/webgl-lint) | [MIT](https://github.com/greggman/webgl-lint/blob/master/LICENSE.md) | Development-only checks for invalid GL calls, bad uniforms, unrenderable textures, attribute bounds, and shader failures. Best small first experiment for the current renderer. |
| [hash-wasm](https://github.com/Daninet/hash-wasm) | [MIT](https://github.com/Daninet/hash-wasm/blob/master/LICENSE) | Incremental Wasm CRC32 and SHA-256. Benchmark against the loader's JavaScript CRC loop using the installed 241 MB pack set; speed improvements are not yet measured. |
| [Workbox](https://github.com/GoogleChrome/workbox) | [MIT](https://github.com/GoogleChrome/workbox/blob/v7/LICENSE) | Versioned service-worker caching for the launcher and engine files. Keep one owner for PK3 caching rather than duplicating the existing IndexedDB pack cache. |
| [fflate](https://github.com/101arrowz/fflate) | [MIT](https://github.com/101arrowz/fflate/blob/master/LICENSE) | Streaming/worker ZIP handling for a future local PK3 import or archive inspector. Existing engine ZIP access does not require replacement. |
| [miniaudio](https://github.com/mackron/miniaudio) | MIT-0 or public domain | Emscripten/Web Audio backend reference for audio initialization and worklets. Verify the existing SDL audio path first; changing the engine audio backend is a larger task. |

Framework detail pages and its raw LICENSE were unavailable through the research
fetcher; MIT is the repository's displayed declaration. Verify the complete
license and adapter source at a pinned revision before importing its code.

Relevant engine ports:

- [wolfet-wasm](https://github.com/BuiltByTed/wolfet-wasm): GPL-3.0 ET: Legacy
  browser client with asset caching, graphics profiles, and input support.
  It connects to a native dedicated server with Omni-bot; our browser-local
  simulation and bot runtime should remain intact when adapting shell ideas.
- [Upstream ioquake3](https://github.com/ioquake/ioq3): GPLv2 engine with documented
  Emscripten support, CMake builds, configurable PK3 loading, and SDL2. Its related
  engine architecture makes it a strong reference for build and browser glue.
- [neortcw](https://github.com/klaussilveira/neortcw): an RTCW/ET port documenting
  an Emscripten ET client and Node server, both using WebSocket networking.
  Useful for ET-specific platform adaptation; component licenses need checking
  before copying individual files.
- [Qwasm2](https://github.com/GMH-Code/Qwasm2): GPLv2 Quake II engine with WebGL2,
  classic/software renderer options, and persistence of saves/settings. A useful
  reference for separating small user-state writes from large asset packs.
- [Qwasm](https://github.com/GMH-Code/Qwasm): Quake browser port with hardware and
  software rendering, mods, and a free browser-playable setup that includes bots.
  Engine/game content licensing must be checked separately before reuse.
- [Q3JS](https://github.com/lklacar/q3js): reference for browser game architecture,
  but its [component license](https://github.com/lklacar/q3js/blob/master/LICENSE)
  makes the ioquake3-derived engine GPL-2.0-only and its browser client, website,
  gateway/server wrapper, and other project components proprietary. Exclude those
  wrapper components from the code reuse plan without permission.

Suggested evaluation order: debug-only webgl-lint; framework input/persistence
patterns; a hash-wasm pack-verification benchmark; then an optional GL4ES renderer
build compared against the existing six-map and restart checks. Workbox follows
once engine-file update and cache invalidation rules are defined. These are
engineering recommendations from the source descriptions, not verified results
for this ET build. The existing engine remains governed by its GPLv3 license.

## Halo CE mobile port findings

Source review on October 2, 2026. The closest browser reference is
[OMG-Guest/Halo-Mobile](https://github.com/OMG-Guest/Halo-Mobile), inspected at
`dae64726943216e930321f96f8e3ea87e414dcd1`. The native Android touch reference is
[theLlamaNet/halo-ce-android](https://github.com/theLlamaNet/halo-ce-android),
inspected at `f30df41f6743812169457d983f05613ff68965db`. Selected files are stored
under `build_wasm/research/` as unexecuted references. This review adds no runtime
dependency. Halo-Mobile's LICENSE.md declares CC0-1.0, rather than MIT; review
component provenance and notices before importing code. The game data is separate.

| Area | Source finding | Application to ET |
| --- | --- | --- |
| Touch input | [input.js](https://github.com/OMG-Guest/Halo-Mobile/blob/dae64726943216e930321f96f8e3ea87e414dcd1/port/web/site/input.js) assigns touch identifiers to movement, look, or buttons. A fire-button touch can also provide swipe aiming. It retains short button taps until controller polling and hides touch controls while a physical controller is connected. | Add independent left-stick movement, swipe look, and action touches. Map actions through ET's existing input queue. Preserve quick taps and simultaneous movement/aim/fire. Mobile aiming should use touch deltas without requesting pointer lock. |
| Input cleanup and layout | Android [TouchControls.java](https://github.com/theLlamaNet/halo-ce-android/blob/f30df41f6743812169457d983f05613ff68965db/port/android/app/src/main/java/com/halo/decomp/TouchControls.java) resets touch owners, look state, and axes on cancellation/layout changes. [TouchLayout.java](https://github.com/theLlamaNet/halo-ce-android/blob/f30df41f6743812169457d983f05613ff68965db/port/android/app/src/main/java/com/halo/decomp/TouchLayout.java) validates dimensions and button scales. | Keep one cleanup operation for blur, hidden page, menu entry, orientation changes, and touch/pointer cancellation. Save normalized positions and bounded button sizes. Validate a complete imported layout before replacing current settings. |
| Audio interruption | Browser [app.js](https://github.com/OMG-Guest/Halo-Mobile/blob/dae64726943216e930321f96f8e3ea87e414dcd1/port/web/site/app.js) creates/resumes audio during Play and retries on touch, pointer, or keyboard interaction. It offers an iOS audio-session playback preference and a legacy silent-media fallback. | Our Play priming and resume control follow the same approach. Add direct touch/pointer gesture coverage for mobile. Treat playback on the phone's silent setting as an explicit preference and test interruption recovery on hardware. |
| Audio under load | [audio-worklet.js](https://github.com/OMG-Guest/Halo-Mobile/blob/dae64726943216e930321f96f8e3ea87e414dcd1/port/web/site/audio-worklet.js) consumes a shared stereo ring, emits silence on underruns, counts them, and converts the mixer rate to the output rate. | Measure SDL callback underruns and main-thread stalls before a backend change. If audio drops during bot/render work, evaluate a worklet bridge with bounded buffering, rate conversion, and restart cleanup. ET currently uses a growing, non-shared heap, so the shared-ring design requires platform/build work. |
| Persistent data and loading | [web README](https://github.com/OMG-Guest/Halo-Mobile/blob/dae64726943216e930321f96f8e3ea87e414dcd1/port/web/README.md) describes WasmFS/OPFS with saves under a separate directory and a completion marker written after import. The launcher checks available storage and exports saves. | Persist small ET profiles/configs separately from the large PK3 cache, with export/import and a validated completion state. Add storage estimates to the loader. OPFS synchronous disk access requires worker/backend adaptation; it cannot directly replace the current main-thread MEMFS links. |
| Offline engine updates | [sw.js](https://github.com/OMG-Guest/Halo-Mobile/blob/dae64726943216e930321f96f8e3ea87e414dcd1/port/web/site/sw.js) fills a version cache before switching its active version; a failed update leaves the old active cache. | Cache ET's shell, JS, Wasm, and bot data as one identified build, and activate only a complete verified set. Keep PK3s in their existing cache to avoid duplication. Our server already supplies isolation headers. |
| Rendering and diagnostics | [presenter.js](https://github.com/OMG-Guest/Halo-Mobile/blob/dae64726943216e930321f96f8e3ea87e414dcd1/port/web/site/presenter.js) closes transferred frame images and releases its frame gate in a finally block. It also has a reusable fixed pixel-buffer fallback. The launcher supports direct worker-canvas presentation. | ET already renders directly to its visible canvas. Keep that path; introduce a worker only after frame-time measurements justify it. Add optional frame-time, heap, and audio diagnostics to make mobile limits measurable. |

Adaptation priority: mobile touch routing and cancellation; small profile/config
persistence and storage visibility; complete offline engine caching; measured
audio/worker optimization. Halo's 2.1 GB fixed address-space requirement comes
from its engine layout and is not an ET memory target. Its standalone Android
renderer and SDL3 bridge are architectural references, rather than drop-in
replacements for ET's SDL2/WebGL shim and browser-local bot simulation.

One source-review caveat: the worklet's interpolating path reads a following
sample, while its available-frame check uses `ceil(position + frames * step)`.
At a 48 kHz mixer feeding a 96 kHz device (`step = 0.5`), 128 output frames can
require sample index 64 although the check accepts only 64 available samples
(indices 0-63). This is an inference from the code, not a measured Halo defect.
Any ET adaptation should test 44.1/48/96 kHz output and account for interpolation
lookahead explicitly.

## Browser UI audit and polish

October 2, 2026: the launcher now has a clearer match card, map mission briefs,
live match summary, and separate engine/files/match loading stages. Zero bots
disables the irrelevant difficulty control and shows solo exploration. Setup
controls, disabled states, keyboard focus rings, and muted text use consistent
styling. Short launchers scroll vertically; the narrowest layout stacks fields.

The toolbar participates in the page's flex layout, replacing an absolute overlay
and guessed canvas height. At 844 x 390 the old toolbar covered the first 25 pixels
of the game. The new toolbar ends at y=85 and the aspect-preserving canvas begins
at y=85. The desktop and narrow layouts also leave the game clear of the controls.
Volume includes a numeric percentage and an accessible value description.

Controls help is available during play in a modal dialog. Shift+Tab leaves the
canvas for the toolbar, while normal Tab retains the native scoreboard binding.
SDL's window keyboard listeners are isolated from HTML controls for keydown,
keypress, and keyup; native buttons and sliders keep their browser behavior.
Closing help returns focus to the game. The loading and failed canvas is inert
and hidden from accessibility, and fatal messages become alerts.

Verification: launcher fault/integrity/lifecycle/input/audio tests pass, including
new setup, dialog, keyboard isolation, and canvas accessibility checks. The Wasm
build succeeds (`build_wasm/ui-polish-build.log`). Browser checks cover 1254 x 884,
390 x 844, 320 x 568, and 844 x 390; no horizontal overflow was observed. The short
launcher's Play and help controls remain reachable by scrolling. Help fits the
narrow viewport, Escape/Close restore canvas focus, and keyboard volume/mute
updates the displayed state. Shift+Tab then Enter opens help after SDL starts,
and Enter activates Close. Native menu clicks select Allies/Medic and enter the
Oasis match correctly after the layout change. No browser console errors were
captured in this session. Screenshots are in `build_wasm/ui-polish-*.jpg`.

## Browser audio audit and polish

October 2, 2026: volume adjustments now retain an explicit mute. Unmute from
zero restores the last positive volume, including after a reload. Saved settings
are validated, and audio restarts and interruptions preserve mute intent.
The controls explain that sound pauses away from the focused game; click the
canvas after adjusting volume to hear the change. The sound status tooltip shows
the output device's sample rate.

The SDL processor has one controlled output through a master gain. Partial graph
or automation failures disconnect output and offer Retry sound; they no longer
reconnect directly to the speakers and bypass mute/volume. Gain automation is
cancelled before replacement, old nodes/listeners are released on restart, and
fatal shutdown cancels ramps and disconnects output. Resume requests are shared
while pending, with a four-second retry deadline for autoplay promises that never
settle; stale promises cannot overwrite the state of a restarted device.
Pointer, touch, and keyboard gestures recover suspended/interrupted audio. Focus
loss and pagehide silence immediately; context state changes update the controls
without waiting for the game loop.

The native DMA callback now handles any number of ring wraps without reading
beyond the buffer. Eight-bit startup/shutdown silence uses 128, rather than 0.
Custom callback sizes are bounded and rounded to powers of two; unsupported
channel counts fall back to stereo. Mix buffers retain at least two callbacks
of lookahead, with bounded allocation. Browser callbacks are at least 256 frames,
including low-rate settings that previously requested an invalid Web Audio size.

Verification: `node misc/web/test_launcher.cjs` includes audio graph/automation
failures, explicit mute and volume persistence, touch interruption recovery,
pending/stale resume requests, restart cleanup, focus/pagehide, and fatal shutdown.
`misc/web/test_audio.c` exercises the actual DMA callback at 8/16 bits with
exact-end, repeated-wrap, and output-boundary checks. Compile it with
`emcc misc/web/test_audio.c -O2 -sUSE_SDL=2 -sENVIRONMENT=node -sWASM_ASYNC_COMPILATION=0 -sASSERTIONS=2 -sSAFE_HEAP=1 -o build_wasm/test_audio.cjs`,
then run `node build_wasm/test_audio.cjs`. Both checks pass, and the game build
succeeds (`build_wasm/audio-audit-build.log`).

Live Oasis checks verified mouse/keyboard mute, muted volume adjustment, a
controlled 0% output while muted, and mute retained through `snd_restart`.
Unmute from zero restores the preceding 78% setting; closing help restores sound
and canvas focus. The expanded audio help fits 390 x 844 without scrolling.
Invalid settings (`s_channels 8`, `s_sdlDevSamps -1`, `s_sdlMixSamps 1`) combined
with 11 kHz/level 2 restart safely as stereo, 256-frame callbacks, and a 1024-sample
mix ring. Normal 44.1 kHz settings were restored; the browser output is 48 kHz
through SDL's rate conversion. Speaker audibility, mobile hardware interruption,
and underruns under prolonged load still need hardware measurements. The SDL
ScriptProcessor backend remains in use; this audit does not claim an AudioWorklet
migration or measured end-to-end latency reduction.

## Lighting and controls audit

October 2, 2026: browser rendering uses screen gamma without claiming a hardware
display ramp. Texture uploads skip baked gamma when that screen pass is active,
so gamma is applied once; the same path supports overbright without display-ramp
support. The existing software texture fallback and native hardware path remain
available. Intensity scaling still applies, and texture alpha is preserved.

The fixed-function WebGL shader now retains fragment alpha through fog and
multiplies alpha for additive RGBA texture stages. Portal clipping uses the signed
plane equation and transforms it by the inverse transpose of modelview at call
time, including translation, rotation, and nonuniform scale. These rules follow
the [OpenGL 1.5 specification](https://registry.khronos.org/OpenGL/specs/gl/glspec15.pdf)
(sections 2.12 and 3.10) and
[ARB_texture_env_add](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_texture_env_add.txt).

Mouse capture grants after timeout or Escape are released instead of taking over
the cursor. Canvas focus loss and pointer cancellation stop drag aim and clear
held input. The guide adds sprint/crouch, leaning and weapon selection, identifies
the bindings as defaults, and points to Options for brightness, mouse sensitivity
and rebinding.

SDL2's Emscripten mouse backend scales both pointer-lock and absolute-motion
deltas into window coordinates. Browser aiming now converts those deltas back
to CSS pixels so shrinking the canvas does not multiply sensitivity. Fractional
motion accumulates instead of being lost; focus, capture, and size changes reset
the remainder. Menu positions retain their existing absolute coordinate path.
`test_mouse.c` passes equivalent travel at 1280/640/320 widths, subpixel motion in
both directions, and focus/resize reset checks. Sustained pointer-lock/right-drag
aiming still requires manual validation in a browser that permits capture/input.

Checks: launcher/input/audio faults pass; `test_gamma.c` exercises the actual
renderer upload/mapping routines for hardware, screen-shader and software gamma.
`test_resources.c` also checks plane distance invariance under translated,
rotated and scaled modelview matrices. The real WebGL2 fixture (`test_render.c`)
passes pixel checks for diffuse/lightmap modulation, additive alpha, fog alpha,
and rejected/retained clip-plane regions. No GL or browser errors were reported
by that fixture. Build succeeds (`build_wasm/lighting-controls-audit-build.log`).

To repeat the standalone renderer checks:

```sh
emcc misc/web/test_gamma.c src/qcommon/q_math.c -Isrc/webgl -O2 -sENVIRONMENT=node -sWASM_ASYNC_COMPILATION=0 -o build_wasm/test_gamma.cjs
node build_wasm/test_gamma.cjs
emcc misc/web/test_mouse.c -O2 -sENVIRONMENT=node -sWASM_ASYNC_COMPILATION=0 -o build_wasm/test_mouse.cjs
node build_wasm/test_mouse.cjs
emcc misc/web/test_render.c -Isrc/webgl -O2 -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 --shell-file misc/web/test_render_shell.html -o build_wasm/test_render.html
python -m http.server 8085 --bind 127.0.0.1 --directory build_wasm
```

Open `http://localhost:8085/test_render.html`; the expanded page reports 16 checks and
zero failures. This temporary fixture server is separate from the game preview.

Live Oasis verification: changing gamma to 1.0 and running `vid_restart` produces
identical RGB pixels in the static stone region x=1030..1169, y=470..619 of the
1254 x 884 screenshots, confirming that a restart does not bake a second gamma
correction into that texture. Default gamma is 1.3 and overbright is 0. Screenshots
are saved as `build_wasm/lighting-audit-*.jpg` and `lighting-gamma-*.jpg`.

The final build also passes live Allies/Medic selection and deployment, movement
and jump input, knife/primary weapon switching, and Escape menu transitions.
Options exposes sensitivity/rebinding under Controls and gamma under System.
The help dialog fits at 390 x 844; at 320 x 568 it scrolls vertically without
horizontal overflow. Escape closes it and restores canvas focus and sound.
The final Oasis session has four offline bots, default gamma 1.3, overbright 0,
and no browser console errors. Screenshots: `build_wasm/controls-audit-*.jpg`
and `build_wasm/lighting-controls-ready.jpg`. The preview remains on port 8081;
the temporary GPU fixture server has been stopped.

### Rendering follow-up

October 3, 2026: GPU texture/renderbuffer size and multisample limits now come
from WebGL instead of hard-coded desktop values. Anisotropic filtering is enabled
only when the browser supports it, and its float limit is queried correctly;
the old shim returned zero for that float query. The extension list and fallback
limit agree with the detected support. This follows the
[Khronos WebGL extension specification](https://registry.khronos.org/webgl/extensions/EXT_texture_filter_anisotropic/).

A fresh renderer context resets current color/coordinates, texture matrices,
fog, depth range, viewport and error state. `glGetDoublev` now writes the queried
number of components, preserving the caller's adjacent memory for scalar and
color queries. Context loss exposes the launcher's Retry action and cancels held
input, audio and networking through the existing fatal-error cleanup.

The GPU fixture passes 16 checks with zero GL/browser errors, including alpha
cutouts, near/far depth, an offscreen framebuffer, hardware capability queries,
and rendering in a second actual WebGL context. Resource checks pass 24,573
texture allocations, 100 shader/program lifecycles, bounded double queries and
anisotropy support/fallback. Launcher tests cover context-loss recovery without
duplicate boot. Evidence: `build_wasm/rendering-polish-checks.jpg` and
`build_wasm/rendering-polish-build.log`.

The final build was also checked live on Radar and Oasis with four bots. Radar
returned to the same world view after `vid_restart`; changing to Oasis preserved
the running client and rendered its map, character, weapon and HUD. No browser
console errors were reported. At 390 x 844 and 844 x 390 the game retained its
16:9 canvas proportions, with no horizontal page overflow; portrait play remains
letterboxed and its native HUD is small. The viewport override was reset and
Oasis was left running on port 8081. Screenshots:
`build_wasm/rendering-polish-radar.jpg`, `build_wasm/rendering-polish-portrait.jpg`,
`build_wasm/rendering-polish-landscape.jpg`, and `build_wasm/rendering-polish-ready.jpg`.
Context-loss cleanup was fault-tested in the launcher harness, rather than by
forcing a hardware graphics reset during the live match. No frame-rate benchmark
or full online match is claimed by these checks.

## Online networking audit and setup

October 2, 2026: the previous browser build retained native UDP socket calls and
had no configured UDP relay. Linking `websocket.js` alone did not make those
sockets reach ET servers. The browser now uses an explicit binary WebSocket
transport for one operator-configured upstream. Native builds retain their UDP
backend; browser offline simulation retains the engine's loopback transport.
See [Emscripten's networking documentation](https://emscripten.org/docs/porting/networking.html).

`misc/web/relay.py` bridges each binary message to one UDP datagram, with a
separate connected UDP socket/source port per browser. The upstream address is
resolved from its command line at startup for the default route. The combined
launcher now also issues signed routes for compatible public master entries;
client packets cannot choose destinations. The relay accepts the `et-udp-v1`
subprotocol, `/relay` and signed `/relay/<ticket>` routes, and
explicit launcher origins. It binds to loopback by default. The WebSocket framing,
handshake, ping/pong, and close implementation comes from the MIT-licensed
[websockets package](https://pypi.org/project/websockets/), pinned in
`misc/web/requirements.txt`.

The preview advertises `/network/config.json`. With no configured relay, the
Online card explains the requirement, disables Join, and leaves offline play
available. The server label and relay URL come from preview environment variables.
The engine uses `192.0.2.1:27960` as a virtual peer identity inside the browser;
that address is never used as the relay's UDP destination. Console connects and
redirects to other addresses are rejected before disrupting an offline match.
Native LAN broadcast remains unavailable. The combined launcher queries the
public master through its HTTP catalog, outside the native client's transport.
Online servers must match the bundled Legacy module/protocol and
installed packs. Other mods are rejected rather than loading the wrong static
module. Automatic asset recovery uses only operator-approved HTTPS sources;
pure servers require matching pack checksums, including the Legacy pack.

Local setup in two PowerShell terminals, from the source root:

```powershell
# One-time relay dependency setup (already installed on this machine).
python -m venv .venv-web
.\.venv-web\Scripts\python.exe -m pip install -r misc/web/requirements.txt

# Relay terminal: replace the hostname with the chosen compatible ET server.
.\.venv-web\Scripts\python.exe misc/web/relay.py --server SERVER_HOST --udp-port 27960 --origin http://localhost:8081
```

```powershell
# Preview terminal: advertise the separately running relay, then choose Online.
$env:ETWASM_RELAY_URL = 'ws://127.0.0.1:8082/relay'
$env:ETWASM_SERVER_LABEL = 'My ET: Legacy server'
python misc/web/serve.py 8081
```

HTTP/HTTPS launchers require ws/wss respectively (HTTPS rejects insecure ws).
For a hosted setup, configure TLS and an exact hosted page origin at the relay;
the current local preview hasn't been published or given a public server target.
WebSocket uses TCP, so packet loss can delay subsequent packets; this relay does
not promise UDP latency characteristics.

Connection status distinguishes opening the relay, contacting/joining the ET
server, receiving game state, loading the map, and an active match. Active match
status uses the engine's ping. The dialog shows the server/engine error, packet
counts, retry, disconnect, and return to offline play. A connected relay alone
isn't shown as a joined match. Opening the relay has an 8-second deadline;
challenge phases use 15 seconds, and game-state/loading phases use 30 seconds.
The online client packet timeout is 20 seconds. Retry creates a new socket and
epoch; late callbacks and queued packets from the old socket cannot affect it.
Receive buffers are capped at 128 packets/1 MiB in the browser; outgoing browser
buffering is capped at 256 KiB. The relay caps packet size at 32 KiB, receive
queues at 128 packets, active clients at eight, and per-client packet/byte rates.
Congestion and invalid packet failures close the transport and expose recovery.

Verification commands:

```text
node misc/web/test_launcher.cjs
node misc/web/test_network.cjs
python misc/web/test_serve.py
.venv-web/Scripts/python.exe misc/web/test_relay.py
```

These cover config failure/timeout and late responses, online startup without a
local map/bot server, binary packet boundaries, queue limits, cancelled/late
socket events, secure URL validation, reconnect/disconnect, eight preview HTTP
checks, and six real loopback WebSocket/UDP checks including client isolation,
IPv6 upstream, rejection of injected UDP replies, and origin/protocol rejection.
A live Wasm browser session sent `getchallenge`
through the relay, received an ET `print` rejection from a controlled UDP test
endpoint, displayed that engine message with one sent/received packet, and
repeated the exchange with Retry. This proves packet transport and error/retry
integration, not a completed multiplayer match. Build evidence:
`build_wasm/network-audit-build.log`; screenshots: `build_wasm/network-*.jpg`.
Keyboard retry and the connection dialog were checked at 390 x 844 without
horizontal overflow. Back to offline restores the launcher. The normal preview
was restarted with no relay configured; its Online card explains that state.
Oasis still loads with four local bots (two per team), with no browser console
errors. The temporary HTTP/WebSocket/UDP audit endpoint has been stopped; the
normal loopback-only preview remains running on port 8081.

### Online recovery polish

October 2, 2026, follow-up audit: relay opening now exposes cancellation in the
launcher and disables repeated Retry in the connection dialog. Retry keeps track
of the running native client, disconnects its previous transport, and blocks old
match traffic/status until the new connect command reaches the handshake. A
15-second deadline also covers a native client that never starts that handshake.
Ping and packet-counter UI updates are limited to four per second; phase changes
remain immediate. Deadlines use the browser's monotonic clock.

Known relay close reasons identify a full relay, rate limits, unavailable UDP,
invalid packets, or protocol mismatch. Unknown close reasons use the generic
recovery message. Configuration requires the supported `/relay` route and uses a
nonempty server label. Protocol/game-name incompatibility now drops the browser
connection while preserving its engine for Retry. The actual compiled EM_JS
bridge preserves the first letter of plain errors and strips ET color markers;
its regex previously lost its escape during C-string generation.

Additional checks:

```sh
node misc/web/test_network.cjs
node misc/web/test_launcher.cjs
python misc/web/test_serve.py
emcc misc/web/test_net_bridge.c -O2 -sENVIRONMENT=node -sWASM_ASYNC_COMPILATION=0 -o build_wasm/test_net_bridge.cjs
node build_wasm/test_net_bridge.cjs
.venv-web/Scripts/python.exe misc/web/online_audit_fixture.py
```

The loopback fixture serves the build on `http://localhost:8083/`, relays binary
packets on port 8084, and answers UDP on port 27961. Join Online, then Retry twice
to exercise protocol mismatch, game-name mismatch, and an accepted handshake
followed by a deliberate rejection before gamestate. It deliberately does not
host a match and is separate from the normal preview. Build evidence:
`build_wasm/online-polish-build.log`.

Live verification preserved the engine across both compatibility failures, with
complete error messages and Retry. A third connection completed challenge,
server-info and connect exchange and sent native netchan packets; its missing
gamestate then exposed the native timeout/retry UI. A full online match remains
unverified because no gameplay server was configured. The local fixture was
stopped after the audit, and the main preview was restored on port 8081.

### Online and offline play follow-up

October 3, 2026: both play modes now expose **Match setup** in the browser toolbar.
Its dialog explains that returning ends the current match. **Keep playing** is
the initial keyboard action, and Escape returns focus to the game. **End match
and return** releases mouse/held input, silences audio and closes the relay before
reloading the launcher. Offline options and sound preferences are restored when
browser storage is available. Controls, connection and match dialogs replace one
another, and fatal cleanup closes the match dialog.

Verification passed `test_launcher.cjs`, `test_network.cjs`, all eight preview
HTTP tests and all six loopback WebSocket/UDP tests. Added launcher checks cover
cancelling the return dialog, keyboard focus, transport/audio cleanup, saved solo
setup, online failure while match setup is open, and fatal recovery. The rebuilt
browser game passed live Battery play with zero bots (native `status` listed only
the loopback player), returning to the saved setup, unavailable online config
followed by offline startup, and Oasis with four bots (two per team). The local
online fixture again exposed full protocol/game-name errors and recovered with
Retry without reloading the native engine; the match-return action then restored
its offline launcher. At 390 x 844 the new dialog fit without horizontal overflow,
and Escape resumed canvas focus. No browser console errors were reported.

Evidence: `build_wasm/play-mode-polish-build.log`,
`build_wasm/play-mode-polish-return.jpg`, `build_wasm/play-mode-polish-mobile.jpg`,
`build_wasm/play-mode-polish-online.jpg`, and `build_wasm/play-mode-polish-ready.jpg`.
The fixture was stopped and its temporary browser tab closed. The normal preview
remains on port 8081 with Oasis/four bots. A full online gameplay match remains
unverified until a compatible gameplay server is configured; the loopback endpoint
tests recovery and deliberately does not provide a gamestate.

### Bot audit and polish

The class manager now scans client-slot capacity, including humans and the last
slot, rather than the connected-player count. Departures leave holes in that
array; missing players could previously trigger unnecessary class changes.
Rejected bot connections release their allocated server slot, and bot console
commands reject more than 64 arguments before copying into the fixed array.

Queued team/class events now check that their goal and callback are still usable
after bot removal or reload. The documented `maxbots -1` setting also survives
minimum/maximum clamping, permitting manual bot management with `minbots 0`.
Positive limits continue to clamp against each other and client capacity.
Changes to preloaded bot scripts and navigation now trigger an `etl.data` rebuild;
previously a script-only edit could leave the browser using the old data bundle.

Run the regression checks after building the browser bot library:

```powershell
./misc/web/test_bots.ps1 -Compiler '<emsdk>/upstream/emscripten/em++.exe' -Node node
node misc/web/test_launcher.cjs
```

The bot checks use the actual GameMonkey interpreter from `libomnibot-wasm.a`.
They compile all six supported map scripts and exercise sparse/highest client
slots, humans, team isolation, departures, class selection, difficulty ordering,
normal and queued-after-teardown goal callbacks, and manual/positive bot limits.
The sparse-slot regression failed against the original class loop and passed
with the capacity scan. The browser build and launcher checks also passed.

Live checks verified twelve Hard bots split six per team, runtime difficulty
settings, bot reload, twenty rejected long-name additions followed by a valid
bot in slot 1, oversized-command rejection, bulk removal and replacement, and
four bots surviving Oasis-to-Gold-Rush-to-Radar map changes. On Radar they
constructed the road MG nest and Axis command post, exchanged the Forward
Bunker, and planted at the side entrance. No new goal callback errors appeared
after rebuilding the guards. Evidence includes
`build_wasm/bot-polish-build.log`, `bot-polish-twelve.jpg`,
`bot-polish-rejected.jpg`, `bot-polish-goldrush.jpg`, and `bot-polish-radar.jpg`.
The preview was restored to Oasis with four Normal bots; the runtime reported
four bots, maximum four, difficulty four and movement skill two. Final evidence:
`build_wasm/bot-polish-settings.jpg` and `bot-polish-ready.jpg`.

### Menus and settings follow-up

The browser now preloads four menu overrides from `misc/web/ui`: the main and
pause menus, Options, and System. Desktop menus remain unchanged. System keeps render size,
brightness, texture detail/filtering, lighting, shadows, sky, frame cap, mixer
volume/rate, Doppler, and packet settings. It omits desktop window management,
OpenAL selection, hardware gamma switches, unsupported MSAA controls, automatic
downloads and desktop home-folder access. Render size is described separately
from the browser's display size; the toolbar remains the master audio control.
The pause menu offers Resume and Match setup, which uses the existing return
dialog instead of terminating the native runtime. Menu edits trigger repackaging.
After a native disconnect, the main menu offers browser Match setup and settings
without requiring a desktop profile or presenting unsupported mod/server browsers.
Toolbar status and variable button labels reserve their space so a focus/audio
state change cannot move a button out from under a click. Browser mouse gestures
outside the canvas are isolated from SDL, including a release outside the canvas
that clears drag aiming.

Native archived settings and key bindings are saved independently of game packs
in the versioned `etl.engineSettings` localStorage entry. `Com_WriteConfiguration`
provides the current generated config; browser writes are coalesced for 250 ms
and flushed before returning to the launcher or leaving the page. The snapshot
is restored to `/browser/legacy/etconfig.cfg` before engine initialization, with
`fs_homepath` set to `/browser`. This saves the active settings, rather than the
entire native profile directory, demos, screenshots, HUD files or server cache.
Stored data is bounded, version/header checked, and rejected if malformed.
Storage or restore failures keep gameplay available and show the status under
the toolbar's Controls dialog. Browser-owned window/audio/download settings are
enforced at startup and when applying System settings.

`node misc/web/test_launcher.cjs` passes additional checks for native config
restoration, bindings/cvars, coalesced saves, return/page-exit flushing, malformed
data, unavailable storage, save retry, and filesystem restore failure. Build
evidence: `build_wasm/menus-settings-build.log`.

Live browser checks changed sensitivity from 5 to 4.272582 and added F8 as a
secondary scoreboard binding, then reloaded and verified both survived. The
original sensitivity and unbound F8 were restored afterward. System Apply
changed filtering to Bilinear and recovered after the renderer/audio restart;
Back discarded a staged change. Trilinear was restored through Apply. Native
disconnect opened the browser main menu, whose Match setup action returned to
the launcher with Oasis, four bots and Normal difficulty retained. A single
toolbar click opened Controls from the focused canvas, and the Controls dialog
fit a 390-by-844 viewport without horizontal overflow. Launcher and network
checks and the final WASM build passed. Screenshots:
`build_wasm/menus-settings-persisted.jpg`, `menus-settings-mobile.jpg`,
`menus-settings-system.jpg`, and `menus-settings-recovery.jpg`.

### Performance audit and polish

Browser frames no longer enter the desktop sleep/spin limiter. `Com_Frame`
samples input and services queued packets, then uses `web_frame.h` to return
immediately when the next simulation/render frame is not due. The clock carries
its deadline across animation callbacks, tolerates 500 microseconds of refresh
jitter, applies cap changes immediately, and resumes without catch-up bursts
after tab suspension. Focus/minimized caps retain their existing defaults and
zero-value fallback behavior; an uncapped main setting avoids division by zero.
Desktop frame limiting is unchanged. Hidden tabs remain subject to browser RAF
throttling, including their local offline simulation.

The WebGL shim uploads supported client indices directly. Ordinary indexed
draws previously allocated both index scratch and a separate resolved buffer,
copied every index, and expanded byte/short indices to 32 bits. Those draws now
use the original pointer and format without either index allocation/copy. Six
indices upload 24, 12 or 6 bytes for uint, ushort or byte respectively. Quad and
polygon conversion still uses its reusable scratch buffer. Invalid index types
and null client pointers are rejected before reading or uploading them.

The audit also checked the single-copy pack links, incremental full-byte asset
validation, bounded packet processing/queues, opt-in renderer diagnostics, and
bot update scheduling. These existing safeguards remain; bot sensory memory
already runs at 10 Hz and game/bot updates follow server simulation. Vertex
repacking and draw submission remain renderer costs. No GPU execution-time or
cross-hardware before/after FPS claim is inferred from CPU submission timing.

Validation: the final WASM build, launcher/network checks, resource lifecycle
checks, and 19 actual WebGL pixel/capability/context checks passed. The GPU suite
now renders each direct index format. `test_performance.c` exercises the real
shim upload path and deadline clock at 60/144 Hz, 1/10/30/60/76/125 FPS caps,
uncapped mode, jitter, cap changes and suspension recovery:

```text
emcc misc/web/test_performance.c -Isrc/webgl -O2 -sENVIRONMENT=node -sWASM_ASYNC_COMPILATION=0 -o build_wasm/test_performance.cjs
node build_wasm/test_performance.cjs
```

Live Oasis checks measured 30.03 FPS over 180 timing samples with four bots at a
30 FPS cap and 60.01 FPS over 240 samples with twelve bots at a 60 FPS cap. The
toolbar Controls dialog opened at a 1 FPS cap; bots continued fighting and
building the water pumps. These short embedded-browser checks validate pacing,
not general benchmark gains. Diagnostics were disabled, the 125 FPS cap was
restored, and the preview returned to four Normal bots. Evidence:
`build_wasm/performance-polish-build.log`, `performance-polish-timing.json`,
`performance-polish-gpu.jpg`, `performance-polish-low-cap.jpg`, and
`performance-polish-twelve.jpg`.

### Online play follow-up, October 4

An accepted `connectResponse` now starts the client's receive timeout at the
current engine time and clears its previous timeout count. A retry clears `clc`
but retains `cls.realtime`; previously an engine already running beyond its
20-second timeout could disconnect within a few frames of acceptance, before a
delayed gamestate or server error arrived. The reset occurs after state/address
validation. Foreign or duplicate acknowledgements cannot refresh the timer.
The fix also applies to native clients using the same connection path.

The relay now signals UDP transport loss so its waiting WebSocket jobs close
and release the client slot. Its incoming queue is limited by both packet count
and one MiB of stored bytes, matching the browser's byte budget, and consuming a
packet releases that capacity. Once the queue fails, later datagrams are ignored.
The browser recovery dialog keeps the configured server name visible across
failures and retries.

The compiled `test_connect_timeout.py` harness extracts the production
acceptance and timeout branches and uses a deterministic clock. It failed on
the original premature timeout and passes after the correction, including the
normal eventual timeout and rejected/duplicate acknowledgements. All nine real
WebSocket/UDP relay tests, eight HTTP preview tests, browser transport checks,
launcher checks, and the final WASM build pass. Relay tests include actual UDP
transport closure, WebSocket close/reason propagation, slot release, and byte
queue capacity/reuse. Run the additional connection check with:

```text
python misc/web/test_connect_timeout.py --compiler emcc --node node
```

Build evidence: `build_wasm/online-followup-build.log`.

The loopback handshake fixture reproduced the premature timeout on a late
accepted retry. With the correction, that retry reached “Receiving game state”
and displayed the fixture's full server message after its one-second delay.
Protocol and game-name errors remained recoverable through Retry without
reloading the engine. The recovery dialog also fit a 390-by-844 viewport without
horizontal overflow. Back to offline retained Oasis, four bots, and Normal
difficulty. The temporary fixture was stopped and its tab closed; the main
8081 preview was restored to an offline Allied Medic match on the final build.
Screenshots: `build_wasm/online-followup-retry-fixed.jpg` and
`build_wasm/online-followup-mobile.jpg`. This fixture deliberately stops before
gamestate and does not host a match; full online gameplay remains unverified
without a configured compatible gameplay server.

### Bot integration follow-up, October 4

The bots-versus-humans command `bot humanteam` now changes the human team rather
than overwriting the bot team. The ratio manager honors `maxbots -1` as an
unbounded population: it moves bots off the human team, adds up to the requested
ratio, and removes excess bots without treating every population as above the
negative limit. Positive caps still apply.

Automatic bot additions now use the total occupied client count for capacity.
Spectators can remain excluded from team balancing while still occupying slots;
a full server no longer repeatedly attempts impossible bot additions. Both the
minimum/maximum manager and ratio manager resume additions when space opens.

The actual GameMonkey regression harness reproduced the wrong-team command,
unbounded-ratio removal, and spectator capacity failures before their fixes.
The expanded checks cover team isolation, bot transfer, excess removal, positive
caps, both full-server population paths, and an available-slot recovery. The
existing six-map, class, difficulty, queued teardown, and manual-limit checks
also pass, as do the launcher suite and final WASM build. Build evidence:
`build_wasm/bot-integration-build.log`.

Live Oasis checks verified three Axis bots against one Allied human with
`maxbots -1`, the corrected human-team command, runtime reload, and map restart
with the ratio and population retained. The preview was restored to four Normal
bots, two per team, with bots-versus-humans disabled and the console closed.
Evidence: `build_wasm/bot-integration-ratio.jpg`, `bot-integration-reload.jpg`,
`bot-integration-restart.jpg`, `bot-integration-settings.jpg`, and
`bot-integration-ready.jpg`. Full-server spectator capacity is verified in the
interpreter harness; the browser match used one human rather than sixteen
independent connected clients.

### Controls and audio follow-up, October 4

Browser SDL window-leave events no longer clear held keys when the canvas still
has keyboard focus. Pointer hover and keyboard focus are distinct in the browser;
the shell's focus flags remain authoritative. Actual focus loss still clears
keys, and desktop SDL behavior is unchanged. Drag aiming retains its separate
mouseleave cancellation.

`test_input_focus.py` compiles the production SDL window-event switch for browser
and desktop policies. It reproduced the hover key-release bug before the fix
and passes hover exit, real focus loss, and desktop behavior afterward. Run it
with `python misc/web/test_input_focus.py --compiler emcc --node node`.
The actual audio DMA callback passes 8/16-bit ring boundaries, repeated wraps,
output bounds, and silence under safe-heap checks. The launcher suite also
passes capture cancellation, keyboard isolation, focus recovery, mute/volume
persistence, audio graph failures, stalled/interrupted resume, stale restart
promises, and shutdown. Existing audio safeguards remain in place.

A live muted `snd_restart` additionally exposed a divide-by-zero crash in SDL's
browser `HandleAudioProcess`. Its output node was disconnected on close but
retained the old native callback; a queued event could see the replacement
`SDL2.audio` and invoke the freed previous device. Browser audio shutdown now
detaches the node callback and cancels its silence timer before SDL closes and
frees the device. The shell simultaneously disconnects its gain, removes the
state listener, and invalidates pending resume requests. Saved mute and volume
remain available for the replacement device. The launcher regression executes
the production native teardown JavaScript and verifies callback/timer removal,
resource cleanup, and retained mute on replacement.

The final build passes live muted and unmuted `snd_restart` checks without new
browser errors; the replacement device reports running 48 kHz audio, a controlled
gain path, retained 80% volume, and the correct mute state. Shift+Tab/Enter opens
Controls, Escape restores game focus, opening the dialog reports zero output,
keyboard volume adjustment retains mute, and keyboard unmute restores sound.
The toolbar fits a 390-by-844 viewport without horizontal overflow. The normal
viewport and Oasis/four Normal bots were restored, with Allied Medic selected
and sound enabled at 80%. These checks validate runtime state and recovery;
physical speaker quality and sustained captured aiming are not measured here.
Evidence: `build_wasm/controls-audio-followup-build.log`,
`controls-audio-muted-restart.jpg`, `controls-audio-unmuted-restart.jpg`,
`controls-audio-mobile.jpg`, and `controls-audio-ready.jpg`.

### Mobile controls audit, October 4

The browser previously fit a phone viewport but required keyboard/mouse gameplay.
`misc/web/touch.js` now supplies an analog left move stick, right-side drag aiming,
and independent Fire, Jump, Crouch, Sprint, Reload and Use touches. Team, Menu and
Weapon buttons access native class selection, Escape menus and weapon cycling.
Gameplay controls hide in native menus; Weapon is disabled there. Native menu
selection retains SDL's touch-to-mouse handling. The default is automatic for
touch/coarse-pointer devices, with an explicit saved Touch on/off override.

Touch gameplay contributes directly to the native user command rather than
synthesizing held keyboard bindings. Physical keyboard/mouse input can coexist;
short action taps survive until the next command, and releasing one finger does
not release another. Movement uses a dead zone and bounded analog radius, respects
run/walk settings, and aim uses the existing sensitivity and zoom multiplier.
Pointer cancellation, lost capture, blur, hidden/page-exit, canvas focus loss,
resize, menu/loading changes and disabling Touch clear held input and queued look.
Touch gestures focus the canvas and use the existing audio resume path without
requesting mouse capture. Utility actions are restricted to fixed native commands.

Combat targets are 52 pixels, toolbar targets at least 44 pixels in touch mode,
and the move stick is 116 pixels. Landscape uses a compact toolbar; safe-area
insets, dynamic viewport height and disabled canvas pan/zoom protect the play area.
Portrait remains usable with controls below the letterboxed game; landscape gives
larger native menu targets. Controls and How to play explain both input modes.

Validation: `node misc/web/test_touch.cjs` covers simultaneous move/aim/fire,
multiple holders, short taps, cancellations, dead zone/radius bounds, menu/loading
gates, one-shot utility actions, preference/storage failures and lifecycle resets.
`misc/web/test_touch.c` compiles the production `web_touch.h` with real engine
types and checks movement/look/button flags, mixed keyboard input, run/walk,
clamping and non-finite rejection. Run with
`emcc misc/web/test_touch.c -O2 -sENVIRONMENT=node -sWASM_ASYNC_COMPILATION=0 -o build_wasm/test_touch.cjs`
then `node build_wasm/test_touch.cjs`. Launcher and compiled browser-focus
regressions also pass, as does the final `etl` build.

Live browser checks verify drag aim/movement, a Fire tap reducing ammunition,
weapon cycling, Team opening class selection, Menu opening/closing native menus,
and native Resume selection. Responsive checks cover 320-by-568 and 390-by-844 portrait and
844-by-390 landscape without horizontal overflow. Physical phone multitouch and
iOS/Android browser gesture behavior still need device testing; the browser
automation supplies single-pointer interaction and the test harness supplies
simultaneous pointer events. Evidence: `build_wasm/mobile-controls-build.log`,
`mobile-controls-landscape.jpg`, `mobile-controls-portrait.jpg` and
`mobile-controls-menu.jpg`.
The normal desktop viewport was restored with Touch off, Oasis/four Normal bots,
Allied Medic selected and sound enabled at 80%; the final runtime has no browser
error entries. `mobile-controls-ready.jpg` records this restored preview.

### PWA and fullscreen audit, October 4

The build now includes a scoped web app manifest, 192/512-pixel maskable icons,
theme/Apple home-screen metadata, and a generated service worker. The CMake
post-build step hashes the launcher, JavaScript, WASM, bundled mod/bot data,
manifest and icons. Their hashes also version the worker itself. Installation
verifies every response with SHA-256 before activating a complete app cache;
interrupted or changed downloads discard the partial new bundle and retain the
previous version. Cached engine requests never fall back to a different network
version. Original game packs remain in the existing verified IDBFS store, avoiding
a second pack copy. Relay configuration and game-pack requests bypass the worker.

App & offline shows saved-file readiness, browser installation guidance,
installation when a browser supplies its prompt, and explicit Reload to update.
A waiting update does not interrupt a match. Updates require other app windows
to close before activation, then reload the requesting window. Old caches are
removed on activation within the worker's own scope. Storage/network failures
remain recoverable: incomplete installation can retry, missing cached files can
be repaired, and a failed update leaves the usable old bundle available.

Fullscreen is controlled by the browser shell, available from both launcher and
game toolbar, and includes the whole app so audio, touch and menu controls remain
accessible. Enter/exit resets held input and mouse capture; completion restores
canvas focus. Unsupported APIs, rejection and timeout have visible recovery.
Escape releases a captured mouse first, then exits fullscreen once uncaptured;
Escape in an app dialog retains its normal dialog-close behavior. Native renderer
settings remain separate from browser fullscreen. Live verification caught and
fixed a Window timer receiver error before the final build; the regression now
checks browser timer binding as well as rejected and stalled fullscreen requests.

`node misc/web/test_pwa.cjs` checks fullscreen entry/exit/Escape, focus cleanup,
install dismissal, storage recovery and explicit updates. `node
misc/web/test_pwa_worker.cjs` checks complete verified installation, offline
navigation/code, preserved isolation headers, live-route exclusions, failed
install retention, version consistency, cache eviction/repair, scoped cleanup
and update guards. The nine `test_serve.py` HTTP checks pass, including the PWA
routes and MIME types; launcher and touch regressions also pass. All seven
generated bundle hashes and the manifest/icon dimensions were independently
verified, and the final WebAssembly link succeeds.

Live checks restart the browser and load Oasis with four bots while port 8081 is
stopped, then restore the preview server. A saved update remains waiting until
Reload to update is clicked. Fullscreen enters/exits from the launcher and game,
expands the game viewport with its toolbar visible, and the phone dialog and
landscape toolbar fit 390-by-844 and 844-by-390 views without horizontal overflow.
Native OS installation and physical iOS/Android fullscreen behavior still need
device testing; this browser provides the install-menu fallback rather than a
native installation prompt.

For hosting, publish the complete generated bundle including `sw.js`,
`manifest.webmanifest`, both icons and all four engine files. Keep correct WASM,
JavaScript and manifest MIME types, the existing COOP/COEP headers, and worker
revalidation. Service workers need HTTPS or localhost; a phone accessing a LAN
HTTP address can play while connected but cannot install this offline cache.
Rebuild through CMake when changing app files so their hashes stay coherent.
Evidence: `build_wasm/pwa-fullscreen-build.log`, `pwa-offline-play.jpg`,
`pwa-offline-ready.jpg`, `pwa-update-ready.jpg`, `pwa-fullscreen-launcher.jpg`,
`pwa-fullscreen-play.jpg` and `pwa-mobile-dialog.jpg`.
The update guard also passes with a second live browser tab: the current match
remains intact until that tab closes, then the saved update applies. Final
fullscreen Escape returns to the normal viewport and native menu without runtime
errors. The preview server is running again with the normal viewport, Touch off,
Oasis/four Normal bots, Allied Medic and sound at 80%. Additional evidence:
`pwa-update-tab-guard.jpg` and `pwa-fullscreen-ready.jpg`.

### Combined integration audit, October 4

The integration review covered the browser launcher, assets/loading, saved
settings, input/audio lifecycle, touch controls, PWA updates/recovery, network
transport, native bot scripts, and renderer/frame-pacing checks. This source is
an extracted archive without a root Git baseline; the review uses the current
browser implementation and its regression harnesses rather than a Git diff.

Browser dialogs now share dismissal and resume rules. A fatal error closes the
App dialog before exposing Retry; a network failure replaces it with connection
recovery. Queued dialog close events and fullscreen completion cannot move focus
away from another open dialog. Launcher Escape reaches fullscreen handling before
the window-level SDL keyboard filter stops propagation. Touch combat buttons now
accept keyboard/assistive activation as one tap, alongside independent held
pointer input; native menus still suppress combat actions.

Missing offline engine files now show a small recovery page on navigation,
including when the launcher HTML itself was evicted. Retry either repairs the
verified current bundle or explicitly activates a waiting update after other
app tabs close. It keeps saved pack storage, retains isolation headers, reports
storage failures, and rejects stale attempts after a deadline. Core code requests
still fail closed instead of combining incompatible engine versions. Cache
status failures now return an actionable message rather than remaining pending.

The renderer's `glIsEnabled` reads emulated alpha-test, fog, clip-plane, texture
and client-array state from the shim. Previously it forwarded those legacy enums
to WebGL. Tests cover active/client texture-unit isolation and native capability
forwarding. The real WebGL page passes 22 checks with zero failures/browser
errors, covering pixels, capabilities, index formats and a fresh context.

Validation passed: all five JavaScript suites; nine preview-route and nine relay
tests; fresh compiled resource, performance, mouse, touch, gamma, network-bridge,
safe-heap audio, input-focus and connection-timeout tests; the GameMonkey bot
checks for all six maps; and all 5,162 ZIP entries in the installed packs. The
Release build succeeds and all seven generated offline bundle hashes match.
Existing compiler/deprecation warnings remain.

Live verification covers explicit app updates, launcher fullscreen/Escape, a
complete offline reload/start with port 8081 confirmed closed, four Oasis bots,
and joining Allies as Medic. At 844×390, Enter on the touch Fire button reduces
ammo from 30 to 29 without holding fire. At 390×844, the App dialog is the only
open dialog and the document has no horizontal overflow. Regression fixtures
cover eviction recovery and dialog failures; deliberate eviction of the user's
live browser storage was not performed. Full multiplayer gameplay, physical
mobile devices, native app installation, audible hardware output and desktop
pointer lock remain outside these local checks.

Evidence: `build_wasm/all-changes-audit-build.log`,
`asset-integrity-audit.json`, `all-changes-gpu.jpg`,
`all-changes-offline-start.jpg`, `all-changes-mobile-fire.jpg` and
`all-changes-ready.jpg`. The normal local preview remains running with Oasis,
four Normal bots, Allied Medic, Touch off, fullscreen off and sound at 80%.

### Recovery bug follow-up, October 4

App update requests now have an eight-second deadline. A worker that stops
responding, becomes redundant, or rejects its message no longer leaves Reload
disabled indefinitely. The deadline also cancels automatic reload authorization;
a late controller change cannot interrupt the match after that failed attempt.
Successful controller replacement clears the timer and reloads once.

Offline repairs now have a 30-second deadline, catch failed worker messages,
and share one operation across repeated clicks. Old missing-file status replies
cannot cancel an active repair. Retry still checks for a newer deployment, and
repair results clear the timer and restore usable controls. Registration and
worker listeners are installed once per object, avoiding accumulation on retries.
Both synchronous and asynchronous install-prompt failures display recovery help.

Fullscreen Escape now defers to every open browser dialog, including Controls,
Match setup and connection recovery. Closing a dialog with Escape therefore
leaves fullscreen intact; Escape outside dialogs still exits it when the mouse
is not captured. The launcher and PWA tests cover these rules and the stalled,
late, throwing and duplicate-operation cases. All five JavaScript suites pass;
the final Release build is recorded in `build_wasm/bug-polish-build.log`.

Live checks explicitly applied the final saved update and started Oasis with
four Normal bots. Escape dismissed both Controls and Match setup while Exit
fullscreen remained active, then the exit button restored the desktop viewport.
The player returned to Allied Medic with Touch off and sound at 80%; no new
browser errors appeared during this check. All seven packaged hashes match.
Evidence: `build_wasm/bug-polish-fullscreen-dialog.jpg` and
`bug-polish-ready.jpg`. Timeout and worker-message faults are covered by the
isolated regression fixtures, without disrupting the user's live browser cache.

### Asset loading, rendering and audio follow-up, October 4

Downloaded packs now transfer their dedicated response buffer to MEMFS with
`canOwn`, avoiding an extra full-size copy of the 228 MB original pack. Size/CRC
verification, persistent caching and single-copy engine links remain in place.
`test_pack_storage.c` exercises the real Emscripten filesystem: buffer identity,
exact tail reads, and truncation when replacing a larger stale pack all pass.
The launcher fault suite also checks that pack downloads use this ownership path.
This removes a known allocation; it is not a cross-device peak-memory benchmark.

`glGetError` now returns actual WebGL errors when no shim error is pending. If
both exist, it reports the shim error first and preserves the backend error for
the next call. Previously backend errors were silently hidden. Resource tests
verify both queues; real WebGL tests inject an invalid backend enum and confirm
that it reaches the engine and is consumed once. All 24 pixel/capability/context
checks pass, including the existing indexed draws, cutouts, fog and framebuffer
paths, with zero unexpected browser errors.

Failed SDL audio initialization removes its device-list command. Open/allocation
failure uses normal shutdown cleanup, detaches browser callbacks before closing
an opened device, resets its handle and DMA state, and permits a clean retry.
The initialized flag is now set before unpausing, so the first callback sees a
ready ring; shutdown clears it before closing and never recloses an old handle.
`test_audio_init.c` compiles the real initialization/shutdown code with SDL and
allocator fault mocks. SDL/open/allocation failure, retry, first-callback state,
duplicate initialization and repeated shutdown pass under assertions/safe heap.
The existing 8/16-bit DMA boundary/silence and shell audio fault checks also pass.

Validation includes a clean Release build, frame-pacing/resource checks,
launcher and PWA checks, nine HTTP tests, all 5,162 pack entries and seven matching
offline bundle hashes. Live Oasis starts from cached packs with four Normal
bots. A muted `snd_restart` reopens the 48 kHz device with a ready output graph
at zero gain; Unmute restores the saved 80% setting. Display restart is checked
for native renderer/audio recovery. Audible hardware output still requires a
manual check. Evidence: `build_wasm/assets-render-audio-build.log`,
`assets-render-audio-gpu.jpg` and `assets-render-audio-ready.jpg`.
The live `vid_restart` completed with the scene/HUD intact and no new browser
or GL errors. Opening Controls reduced output gain to zero; closing it restored
the running 48 kHz graph to 80%. The preview remains on Oasis with four Normal
bots, Allied Medic, Touch off and fullscreen off.

### Grayscale texture follow-up, October 4

The next GPU audit reproduced rejected uploads for all six desktop luminance
formats selected by `r_greyScale`. The shim now converts the engine's RGBA-byte
uploads to valid RGBA8 storage, replicating red into RGB as desktop luminance
storage does. Opaque formats force alpha to 255; luminance-alpha preserves it.
Subimage updates retain this behavior per texture. Conversion honors unpack
row length, skips and alignment, then restores the caller's unpack state and
frees its temporary buffer. Ordinary color uploads keep their existing path.
Deletion, color redefinition and context recreation clear the conversion mode.

The optional `TEST_API_RENDERTOTEXTURE` limbo-panel example also reached a
renderer command using `GL_LINEAR` as a wrap mode and SGIS automatic mipmaps.
That command now clamps texture edges and explicitly generates mipmaps on the
WebAssembly path. The example is disabled in the normal game; this is API-path
repair, not a claim that the default limbo panel was broken.

The real GPU suite passes 50 checks with assertions and safe heap, including
all luminance formats, alpha, subimages, padded/skipped rows, empty allocations,
complete mip chains, color redefinition, copied framebuffer sampling and the
existing fog/cutout/indexed/framebuffer/context checks. The pre-fix reproduction
failed 19 of 43 checks. Texture resource cleanup and frame pacing checks pass,
the Release game build succeeds, and all seven offline bundle hashes match.
Evidence: `build_wasm/grayscale-render-gpu.jpg` and
`build_wasm/grayscale-render-build.log`.

Live verification activates the coherent offline update, loads saved Oasis
packs and joins Allied Medic with four Normal bots. `r_greyScale 1` followed
by `vid_restart` renders the world, weapon, HUD textures and transparent
foliage correctly. Restoring `r_greyScale 0` and restarting returns to color.
Both restarts retain a running, ready 48 kHz audio graph at the saved 80% volume
with no new browser or GL errors. The final preview keeps Touch and fullscreen
off. Evidence: `build_wasm/grayscale-render-live.jpg` and
`build_wasm/grayscale-render-ready.jpg`. Actual speaker output remains a manual
hardware check.

### Gameplay follow-up, October 4

After denied mouse capture, stationary left clicks previously retried permission
in the shell and were rejected again by the SDL button gate. Fallback now
forwards left clicks and explicitly permits only that gameplay button in SDL.
Right-drag aiming retains its separate motion gate; moving an unlocked pointer
without dragging cannot turn the camera. Unfocused presses remain blocked and
button releases still pass through. Capture mouse explicitly retries permission,
and help/toolbar guidance describes the updated behavior.

Offline startup now sets `g_gametype 2` before loading the selected map. The
engine's campaign default (`4`, confirmed in the live console) could otherwise
choose a campaign and advance to another map after a single-map selection.
Online startup leaves match mode to the remote server. Launcher regressions
cover all six offline map selections and online isolation.

Validation passes the launcher lifecycle/input/audio suite, touch pointer and
native usercmd checks, and the actual GameMonkey interpreter's six-map/class/
difficulty/team/limit checks. The SDL focus harness now compiles the production
mouse-button cases, checking fallback left shots, right-button isolation,
menu/capture/drag gates and releases after focus loss. The final Release build
succeeds and all seven offline bundle hashes match.

Live objective-mode Oasis shows four bots fighting and using landmines. A
shortened time limit and five-second intermission exercise normal round exit:
the next round remains Oasis with four bots, and the map restores its 30-minute
limit. Intermission time is restored to 60 seconds after this check. Evidence:
`build_wasm/gameplay-round-audit.json`, `gameplay-round-restart.jpg`, and
`gameplay-audit-build.log`.

In the final build, a stationary fallback left click changes ammunition from
30/60 to 29/60 without right-drag aim. Keyboard reload returns it to 30/59;
weapon keys select the pistol and primary weapon. Changing to Medic in the
native team menu and exercising death applies the selected class on the next
reinforcement wave. The preview remains in normal-color Oasis gameplay with
four Normal bots, Allied Medic, Touch/fullscreen off and the running audio
graph at 80%. No new browser errors appear. Evidence:
`build_wasm/gameplay-fallback-fire.jpg`, `gameplay-reload.jpg`, and
`gameplay-respawn.jpg`. Full remote multiplayer gameplay still requires a
compatible configured server; physical mouse capture remains browser-dependent.

### Offline and online UI follow-up, October 4

Online configuration checks now disable Check again until the request completes,
including late replies, failures and switching back to offline. The action labels
distinguish engine loading, connecting and game-file loading. Completed checks
restore focus lost when Check again disables itself, without stealing focus from
another control. A keyboard join
focuses Cancel connection; cancellation restores focus to Join server instead of
leaving it on the hidden cancel button.

Retry connection keeps the recovery dialog visible through the game-server
handshake, with Close focused while Retry is disabled. Only a successful Online
status finishes that retry and returns focus to the game. Ordinary status updates
cannot dismiss a dialog opened by the player, and finishing after Close cannot
replace another dialog. Failures retain their server context and recovery actions.

Offline bot-count errors expose aria-invalid and reference the match summary;
correcting the value clears the error. Play-mode and help-close buttons have a
44-pixel minimum height. Fatal recovery hides the irrelevant mode and match fields
and associates the focused Retry action with the error message.

The launcher and network regression suites pass, including duplicate checks,
loading labels, cancellation focus, retry progress, failure and unrelated-dialog
isolation. The Release browser build succeeds; all seven offline bundle hashes
match and the coherent update is activated. Browser verification covers portrait
390-by-844 setup, landscape 844-by-390 dialogs and launcher scrolling, and normal
desktop layout. Invalid and solo bot counts, unavailable-online fallback, saved
Oasis/four-bot offline startup, resume focus and keyboard match return work.

The loopback relay exercises protocol mismatch, game mismatch and an accepted
handshake followed by an intentional rejection before gamestate. Final keyboard
Retry keeps Close focused while Receiving game state is visible. A separate
temporary preview deliberately returns 503 for etl.js: its error screen focuses
Retry, and restoring the normal server lets Enter recover to a ready launcher.
Both temporary previews are stopped. Full remote multiplayer gameplay still
requires a compatible configured game server; this fixture does not host a match.
The final preview returns to the saved Oasis/four-Normal-bot launcher at normal
desktop size. Check again retains focus after a keyboard check completes, and no
browser errors are reported in this preview.
Evidence: `build_wasm/play-ui-audit-build.log`, `play-ui-mobile-offline.jpg`,
`play-ui-online-progress.jpg`, `play-ui-online-recovery.jpg`,
`play-ui-fatal-recovery.jpg`, `play-ui-online-check-focus.jpg`, and
`play-ui-ready.jpg`.

### Online server connection follow-up, October 4

The native Disconnect command now notifies the browser transport before the
engine performs its normal departure. The relay remains available for the final
game packets and closes when the native client reaches Disconnected. This reports
"Disconnected by you" without a failure dialog or redundant disconnect command.
Automatic cleanup during a failed connection or a replacement handshake cannot
mark the new attempt as a user departure. Offline and desktop behavior remain
unchanged.

Each handshake now has a 90-second overall limit in addition to the existing
15-second challenge/join and 30-second receiving/loading/waiting phase limits.
Changing handshake phases cannot extend the attempt indefinitely. Retry gets a
fresh budget; reaching Online ends it, so a later server map change gets a fresh
loading budget. Phase timeout messages identify where the connection stalled.

Validation passes the launcher and network suites, including cycling phases,
fresh retry/map-change budgets, each stalled phase, orderly native departure and
automatic cleanup isolation. The compiled production connect/timeout harness also
checks native Disconnect notification and disconnected/cinematic no-ops. The
compiled EM_JS bridge verifies the new notification. All 10 real WebSocket/UDP
relay tests pass, including final datagrams sent before an orderly close and
release of the client slot. The Release build succeeds and all seven offline
bundle hashes match.

Live loopback checks activate the coherent update, exercise protocol and game
mismatches, and reach Receiving game state after a valid handshake. Issuing the
native console Disconnect command yields the normal departure status and enables
Retry. Retrying starts a fresh handshake and retains the deliberate pre-gamestate
rejection reason. Back to offline returns to saved Oasis/four-Normal-bot setup.
No browser errors are reported. The temporary preview and relay are stopped; the
main preview keeps the updated launcher. The fixture's optional --reject-delay
15 argument leaves time to exercise native Disconnect before its rejection.
Full remote multiplayer gameplay still requires a compatible configured server;
the loopback fixture does not host a match.

Evidence: `build_wasm/server-connection-audit-build.log`,
`server-connection-native-disconnect.jpg`, `server-connection-retry-progress.jpg`,
and `server-connection-rejection.jpg`.

### Public server test, October 4

The official https://www.etlegacy.com/servers list supplies its Legacy entries
through https://www.etlegacy.com/servers/rest?mod=legacy (JSON/XHR headers).
Live getinfo queries identify the official development server at
104.248.140.165:27960: protocol 84, gamename et, mod legacy, no password and
Fuel Dump. Its reported engine is 2.86.0-34-g50cffc8, built October 3. Other
stock-map candidates found are |H*S|NO DOWNLOADS (115.70.0.200:27960, Fuel Dump)
and Classic Maps ET Legacy (144.76.234.46:27960, Battery). Server maps and
availability can change; these are the observed live replies, not guarantees.

The main loopback preview now advertises ET:Legacy DEV and uses a loopback-bound
relay targeting 104.248.140.165:27960. Reproduce its configuration with:

```powershell
# Relay terminal, using the existing virtual environment:
.\.venv-web\Scripts\python.exe misc/web/relay.py --server 104.248.140.165 --udp-port 27960 --port 8082 --origin http://localhost:8081

# Preview terminal:
$env:ETWASM_RELAY_URL = 'ws://127.0.0.1:8082/relay'
$env:ETWASM_SERVER_LABEL = 'ET:Legacy DEV · 104.248.140.165:27960'
.\.venv-web\Scripts\python.exe misc/web/serve.py 8081
```

The actual browser join accepts the public handshake, receives game state and
loads Fuel Dump entities. Full play then fails while parsing the knife weapon
configuration. The native log reports a pure server and the missing required
legacy/legacy_v2.86.0-34-g50cffc8.pk3; the local supplemental packs are absent
from its pure list. This verifies real relay/server communication and exposes
server-pack compatibility as the remaining blocker. Automatic map/mod downloads
remain disabled and no server packages are installed by this test.

Evidence: `build_wasm/official-server-list.json`, `compatible-server-probes.json`,
`public-server-test-log.json`, and `public-server-test.jpg`. The local relay and
configured preview remain running so the server can be selected from Online.

### Custom maps and asset packs, October 4

The offline launcher's **Custom maps & assets** disclosure imports an Enemy
Territory map `.pk3` from the device through the loopback preview server. The
imported map is selected after validation. Packs must have simple filenames, be
at most 256 MB each, and contain version-47 Enemy Territory BSPs with valid lump
bounds. Import checks every ZIP entry's CRC, rejects duplicate or unsafe paths,
encrypted/linked entries, executable mod modules, and startup/player configs.
It does not extract the archive or execute remote modules. Existing files are
never replaced. Import requires the exact localhost origin and a custom request
header and is unavailable when the preview binds to a non-loopback address.

Operators may also place packs directly in these directories, then choose
**Refresh map list** before starting the match:

```text
web-assets/etmain/<custom-map-or-shared-asset>.pk3
web-assets/legacy/<exact-server-Legacy-asset-pack>.pk3
```

Set `ETWASM_CUSTOM_ASSETS` before starting `misc/web/serve.py` to use another
directory. These files are game data and are ignored by Git. Preserve each map's
license and included author documentation. Maps follow the [official ET:Legacy
directory convention](https://github.com/etlegacy/etlegacy/wiki/Path-and-File-Structure).
At most 64 packs are listed. Invalid packs are skipped with a launcher notice and
an explicit server log reason; the original four required packs remain separate.

`/assets/custom.json` describes validated content and discovered maps. Pack URLs
include their full SHA-256 digest, so stale catalog URLs cannot fetch a different
version. Downloaded and cached custom packs are checked by size, complete-file
CRC32 and SHA-256 before the engine starts, and mounted through symlinks to avoid
a second retained pack copy. Bundled packs retain their existing complete-byte
CRC/manifest checks. Cached extra packs have a 512 MB budget; obsolete managed
versions and unused packs exceeding it are evicted before fetching new content.
Original packs, installed source packs and player settings are unaffected.

Offline startup loads the chosen map's packs and shared etmain asset packs.
Initial online startup loads installed catalog packs, including exact Legacy assets.
After recovery it loads only server-referenced extras by checksum. This build keeps
statically compiled Legacy game modules; an asset pack cannot install another
mod or guarantee protocol compatibility. Missing server references now stop
before cgame initialization with the required PK3 filenames and recovery
instructions instead of a misleading missing weapon error. Native downloads
remain disabled in the browser; arbitrary server download redirects are not
followed. The approved recovery path below fetches matching assets and rejoins;
manual installation remains available.

Maps with bundled `.way` and `.gm` navigation retain the bot controls. Maps without
navigation use solo exploration and explain why bots are disabled. Switching
back to a supported map restores the previous bot count. The selected custom
map persists across reloads. A saved catalog plus verified IDBFS bytes permits
cached custom-map startup when the asset server is unavailable; importing and
fetching a new map requires the local server. Browser storage eviction can
require another download.

Validation: 14 loopback preview/import tests; launcher custom catalog, delayed
discovery, navigation fallback, offline cache, corrupt CRC/SHA and cache-budget
checks; production required-pack gate compiled in web and desktop modes;
network and PWA suites; Release rebuild with coherent offline bundle. The
installed asset audit covers 5,162 original-pack entries and optional packs.

The real browser imported [ETL Warbell V3 from the official package
site](https://www.etlegacy.com/packages/etl_warbell_v3), loaded its BSP and
navigation, started four balanced browser-local bots, and joined the match as an
Allied medic. Bots progressed through the Book of Death objective. The imported
pack is 71,328,896 bytes, SHA-256
`27af7d99c3ec92a70f9d1edd804984712fd483c687acd4b612df65c3447c67aa`.
Optional missing raster alternate layers for crosshairs S/T are no longer
registered in the browser, avoiding fallback textures and startup warnings.

The final real-browser reload starts Warbell and all four bots with the preview
server completely stopped, using the saved catalog, verified base packs and
SHA-256-verified map cache. Portrait 390×844 import controls have no horizontal
overflow. Subsequent online work installed the exact
`legacy/legacy_v2.86.0-34-g50cffc8.pk3` and verified public-server recovery,
preserving the server's pure checksum requirements.

Evidence: `build_wasm/custom-assets-build.log`, `asset-integrity-audit.json`,
`custom-map-warbell-log.json`, and `custom-map-warbell-play.jpg`.
Additional evidence: `custom-map-mobile-import.jpg`,
`custom-map-offline-catalog.jpg`, `custom-map-offline-play.jpg`,
`custom-map-offline-log.json`, and `custom-assets-public-server-error.jpg`.

## Online server recovery (October 4, 2026)

Copy `misc/web/online-server.example.json` to `web-server.json` in the repository
root, then run `.\.venv-web\Scripts\python.exe misc/web/run_online.py`.
Open http://localhost:8081/, select **Online**, then **Join server**. The wrapper
runs the launcher, public discovery and UDP relay together on loopback; Ctrl+C
stops both services. Use Find a server and Server to choose a verified public
Legacy entry; Launcher default keeps the configured upstream available.
Configuration survives restarts. Service logs are `build_wasm/online-launcher.log`
and `online-relay.log`. The example selects the public Legacy development server;
its availability and rotation remain controlled by its operator. Other servers
must support the statically compiled Legacy 2.86 client and compatible packages.

`autoAssets: true` enables recovery when the native client identifies missing
PK3 references. `downloadBase` selects an operator-approved HTTPS mirror with
`etmain/` and `legacy/` directories. A mirror 404 falls back to the official
package or exact snapshot catalog. `assetSources` overrides a particular
`game/filename.pk3` with a published HTTPS PK3 or ZIP URL. The example uses the
[Hirntot map mirror](https://download.hirntot.org/etmain/) and an
[ETC source for Gold Rush GALS](https://et.clan-etc.de/etmain/).
Keep map licenses and author documentation with their packs.

Downloads use private staging, full ZIP validation and the engine's signed
MD4/XOR checksum over ordered nonempty-entry CRCs. Only exact server matches
are published, preserving original PK3 bytes. ZIP wrappers stream only the
exactly named pack and never extract other files. Browser loading additionally
checks complete-file CRC32 and SHA-256. Native server download redirects remain
blocked. Automatic requests require an opted-in configuration and a same-origin
custom header, with size and time bounds.

Verified recovery saves a ten-minute rejoin record for the same relay, reloads
to mount the required packs and reconnects. Cancellation invalidates late
callbacks; four consecutive recovery reloads are the limit. Failed downloads
leave retry and offline actions available. Unavailable browser storage requires
a manual reload. A successful join clears the retry record. Audio retains normal
browser gesture and focus requirements.

The connection dialog identifies the current pack and its position in the
required list. Downloads stream same-origin NDJSON progress through one XHR:
queue wait, installed-pack check, source lookup, byte transfer, extraction,
verification and publication. Known sizes show MB totals and percentage; unknown
sizes and validation show an indeterminate progress bar. Native connection
updates cannot overwrite active pack progress or the last actionable pack error.
Errors distinguish unavailable sources, timeout, wrong server version, invalid
archives and launcher limits without revealing operator URLs or local paths.

Cancel aborts the progress request and invalidates late browser callbacks. A
failed server progress write interrupts the installer, removes staging files and
releases the lock. Cancellation takes effect at the next progress write; an
upstream read can wait up to its 20-second socket timeout. Previously verified
packs stay installed. Queue waits and source fallback share a 210-second
preparation budget, checked between blocking operations; browser timeout is 240
seconds. HTTPS redirects must retain the configured host and port. Declared
response sizes are checked for truncation before archive validation.

Archive verification checks cancellation between entries and every 1 MB of
expanded data or hashing. Repeated progress stages are throttled to four writes
per second, retaining immediate stage changes and completion. Fatal engine
errors abort the active request and invalidate late responses. If browser storage
cannot save the rejoin record, **Reload to load packs** offers a manual recovery
action; native status updates preserve this message. Clearing a rejoin record
also clears its in-memory value when storage is unavailable.

The custom catalog caches validation per pack using resolved path, file identity,
size and modification/change times. New or replaced packs are fully revalidated;
unchanged verified or rejected files do not make refresh rescan the entire
library. Files changed during validation are rejected, and deleted files leave
the cache. Import and server-download publication share a short inventory lock
and recheck the 64-pack limit before publishing, including concurrent imports.

When automatic recovery is enabled, a first online handshake with a custom
library exceeding 512 MB loads a bounded subset, prioritizing Legacy and shared
assets. The server then identifies its exact requirements and normal recovery
mounts those packs. Unrelated maps cannot prevent the initial connection; actual
required packs remain subject to the 512 MB limit. Offline map selection retains
its existing pack selection and limits.

Local game-file loading displays the pack filename, transferred MB and manifest
size even without an HTTP content length. Its progress bar describes the current
download or verification stage. Custom-map imports distinguish upload progress
from server-side verification instead of leaving a completed upload looking
unfinished.

The native 28-byte player key is generated with browser randomness and saved
separately in local storage, so recovery reloads preserve player identity. It is
restored before native initialization. Browser disconnect and return-to-launcher
actions keep the relay open until native departure sends its final packets;
only then does transport close or the page reload. This prevents an immediate
retry from colliding with the old server session's key. A two-second fallback
prevents a stopped native engine from hanging the return action.

Recognized Legacy packages may retain published modules and `default.cfg` as
data. Embedded native/wasm binaries are never extracted or executed. Static
cgame/UI references use the package's published `*.mp.wasm32.so` entries only
when the normal server pure whitelist permits that pack. Feed-bound pure
checksums and server verification remain active. Arbitrary modules, startup or
player settings, conflicting headers and unsafe assets are rejected. A bounded
1,024-packet/8 MB receive queue preserves reliable fragments through map-loading
bursts while native registration yields to browser events.

Validation: launcher recovery/rejoin/cancellation/failure/loop-limit checks;
production missing-pack and pure-reference code compiled in web and desktop
modes; checksum vectors, atomic install and approved ZIP checks; preview routes;
network, relay and PWA suites. Public-server testing verified matching Legacy
acceptance, missing-map fetch/rejoin for Warbell and Bremen, online snapshots and
a server-controlled transition to Battery. The final build recovered Gold Rush
GALS, spawned an Allied medic, and verified movement with positions changing
from (4, 3675, 432) to (3, 3464, 448). Deliberate disconnect/retry was also
tested successfully after fixing the disconnect order, without changing the
player key. Final gameplay evidence is
`build_wasm/online-play-working.jpg` and `online-play-log.json`.

Download UX validation additionally covers partial/malformed progress frames,
unknown lengths, stale native status, truncated responses, actionable errors,
queue deadlines, client socket abort and installer cleanup. The Python asset
and route suites pass 33 tests. Browser testing removed a preserved Legacy pack,
downloaded it again from the approved snapshot archive, compared full SHA-256
bytes and verified automatic rejoin. A second live transfer was cancelled from
the mobile-sized dialog: staging disappeared and no incomplete pack was
published. The original verified pack was restored and temporary backups were
removed. Desktop/mobile proof is `build_wasm/asset-download-progress.jpg` and
`asset-download-mobile.jpg`; the seven-file PWA bundle hashes were checked.
The final retry also handled a live server rotation to `sp_delivery_te`,
automatically installing its 3,162,902-byte pack with native checksum -699467615.
The subsequent per-pack cache check returned six verified packs in 2.7–16.4 ms
over loopback (`build_wasm/asset-catalog-timing.json`). Additional fault checks
cover cancellation during ZIP validation, fatal-error cleanup, storage-failure
reload, oversized initial libraries, oversized exact server requirements and
publication when an import fills the final available pack slot.

## Launcher and recovery UX (October 4, 2026)

**Cancel loading** returns to match setup during saved-file lookup, manifest
loading, pack download or verification. It invalidates asynchronous callbacks,
aborts active requests and closes the relay before reloading. Repeated cancel or
return clicks cannot schedule multiple reloads. Native match startup remains a
separate stage without cancellation.

Connection recovery highlights the available retry, cancel-download or manual
reload action and hides unavailable actions. Failure focuses the recovery action;
intentional disconnect keeps focus on Close until native departure completes,
then focuses Retry if the player has not moved elsewhere. **Return to launcher**
describes the destination explicitly. Packet counts and ping are under collapsed
**Connection details**, while the connected live status stays stable. Receive
queue overflow distinguishes a stopped game loop from an overloaded active loop;
browser background throttling itself is unchanged.

Controls help opens the touch or keyboard section for the active input mode.
Touch help explains Team and Menu; mouse capture guidance is hidden in touch
mode. Focus/audio guidance is available in a collapsed section. The match-return
dialog emphasizes and initially focuses **Keep playing**.

Validation covers loading cancellation and late callbacks, return deduplication,
failure/disconnect focus, stable connected status, contextual help, network queue
errors, touch controls and PWA lifecycle. Desktop and 390 × 844 browser checks
verified loading cancellation, keyboard dismissal and compact help. Live server
recovery installed and rejoined `etl_braundorf_v2.pk3` (36,392,429 bytes, native
checksum -1072824284), followed by intentional disconnect and keyboard retry.
The build completed and all seven offline bundle hashes matched. Evidence:
`build_wasm/ux-mobile-pack-recovery.jpg`, `ux-touch-help.jpg`,
`ux-desktop-help.jpg`, `ux-online-connected.jpg` and `ux-retry-focus.jpg`.

## Map-request lifecycle bug fixes (October 4, 2026)

Fatal engine errors now invalidate and abort the active custom-map catalog or
import request. Previously, a late catalog response could disable the fatal
screen's **Retry** button. Import progress and completion also use the request
generation, so a timed-out import cannot overwrite a newer refresh, clear its
busy state or change the selected map. Fatal recovery prevents new catalog reads.

Map refresh restores keyboard focus lost when its button was temporarily
disabled. It does not move focus if the player selected another control or
switched to Online while the request was pending.

Regression checks reproduce the disabled-Retry bug and cover catalog/import
abort, late upload progress, late completion/timeout, newer-request isolation and
retry reload, focus restoration and mode changes. The launcher, network, touch
and PWA suites pass, as do all 33
Python asset and preview-route tests.

The rebuilt preview verified focus retention after a real catalog refresh and
started an offline Oasis match with bots. All seven PWA bundle hashes matched.
Evidence: `build_wasm/bug-map-refresh-verified.jpg` and
`bug-audit-offline-verified.jpg`.

## Touch gameplay completeness (October 4, 2026)

Touch gameplay now includes **Alt fire** and **Prone**. Alt fire uses the native
`weapalt` command for supported scopes, rifle grenades and deployed weapon modes.
Prone contributes `WBUTTON_PRONE` to the normal player command without modifying
keyboard state. Tap Prone to lie down or stand up; native stance delays and
collision restrictions still apply.

The combat grid uses four columns in landscape and three on narrow screens,
with 52-pixel buttons. Short portrait screens also use the compact toolbar and
place the game above the buttons, keeping its health/ammunition HUD visible.
Controls help describes both added actions.
Weapon actions are restricted to active gameplay, and menu, console, loading,
focus loss and touch-disable transitions clear pending touch input.

Validation includes touch pointer/keyboard activation and cancellation tests,
compiled native user-command and SDL utility-action tests, launcher/network/PWA
checks and the existing six-map bot class/difficulty/population suite. A live
offline Oasis match verified balanced bots, team/class selection, spawning,
FG42 scope activation/deactivation, a scoped shot (20/40 to 19/40), prone/stand
transitions and reload (19/40 to 20/39). The native menu hides combat controls
and disables weapon cycling. Browser layout checks cover 844 × 390, 390 × 844
and 320 × 568; physical-phone multitouch remains a device check.
The rebuilt browser bundle passed verification of all seven PWA asset hashes.

Evidence: `build_wasm/gameplay-touch-scope.jpg`,
`gameplay-touch-scoped-fire.jpg`, `gameplay-touch-prone.jpg`,
`gameplay-touch-portrait.jpg` and `gameplay-touch-small-portrait.jpg`.

## Live bot match tests (October 4, 2026)

Three short offline rounds were played through to the native result screen.
The test used a two-minute `timelimit` after warmup; map scripts restore their
normal limit when the next round starts.

| Map | Bots | Difficulty | Human class/team | Result |
| --- | --- | --- | --- | --- |
| Oasis | 4 | Normal | Allied Medic | Axis win; all four bots present |
| Gold Rush | 8 | Hard | Axis Engineer | Axis win; all eight bots present |
| Radar | 6 | Easy | Allied Engineer | Axis win; all six bots present |

Oasis bots built both water pumps and the command post and contested Old City.
The human fired at defending bots, was killed in combat, and separately verified
reinforcement deployment at the captured forward spawn. Gold Rush verified the
engineer loadout, movement, firing and MP40 reload (29/30 to 30/29). Radar bots
constructed the bunker MG nest, planted and defused dynamite at the side entrance
and contested the forward bunker; defending bots killed the human player.

Returning to the launcher between maps applied the new map, bot population and
difficulty. All three result screens hid combat controls and disabled weapon
cycling. No engine crash or bot script exception was observed. These short rounds
do not establish completion of the full attacking objective chains.
The logs include the existing optional GeoIP-database notice; bot waypoint/goal
initialization succeeds. The compiled six-map bot regression suite also passes.
The native death handler accepts the positive upward input generated by touch
Jump to enter the reinforcement queue; this was checked in source, separately
from live deployment checks. The original 60-second warmup, desktop input and
Oasis/4-bot/Normal launcher configuration were restored after testing.

Evidence: `build_wasm/bot-match-oasis-combat.jpg`,
`bot-match-oasis-respawn.jpg`, `bot-match-oasis-result.jpg`,
`bot-match-goldrush-result.jpg` and `bot-match-radar-result.jpg`.
Captured browser/native logs are `bot-match-goldrush-log.json` and
`bot-match-radar-log.json` in the same directory.

## Browser compatibility audit (October 4, 2026)

The launcher checks capabilities before inserting Emscripten's generated engine
script. WebAssembly, a working WebGL 2 context and native dialog menus are
required. Missing or blocked features show an actionable error with a Recheck
support button; the engine and game packs are not requested. The graphics probe
uses a disposable canvas and releases its context when the loss extension is
available. Browser names and user-agent versions do not determine admission.
Passing this check does not guarantee sufficient device memory or GPU capacity
for every custom map.

Custom maps and online server packs require Web Crypto SHA-256 verification.
The launcher detects unavailable hashing before loading packs, with guidance to
use localhost or HTTPS in an updated browser. Synchronous digest denial and
asynchronous rejection also produce verification errors. The strict integrity
check is retained. Bundled maps can still start without Web Crypto.
See [MDN SubtleCrypto](https://developer.mozilla.org/en-US/docs/Web/API/SubtleCrypto)
for the secure-context requirement.

Audio, mouse capture, fullscreen and app installation are optional capabilities.
Mouse capture failures retain right-button drag aiming. Web Audio supports the
prefixed constructor; a denied audio context does not prevent the match from
starting. Fullscreen supports both standard and WebKit methods and events.
Offline app setup tolerates a throwing service-worker property getter or a
synchronously denied registration. These failures leave connected play usable
and preserve a previously saved app; registration can be retried.
Browser restrictions and iframe policies may still deny these features; see
[MDN pointer lock](https://developer.mozilla.org/en-US/docs/Web/API/Element/requestPointerLock),
[fullscreen availability](https://developer.mozilla.org/en-US/docs/Web/API/Document/fullscreenEnabled)
and [service-worker registration](https://developer.mozilla.org/en-US/docs/Web/API/ServiceWorkerContainer/register).

| Coverage | Verification |
| --- | --- |
| In-app Chromium, localhost | Updated bundle reaches Play; Oasis/4-bot/Normal starts and renders; fullscreen enters/exits; sound toggles; controls menus open; app reports Offline ready; no captured runtime errors |
| Required capability failures | VM fault tests: missing/blocked WebAssembly, missing/throwing WebGL 2, missing dialog API, no engine insertion or asset requests, stable failure state and reload recovery |
| Optional API differences | Unit tests: unavailable/rejected/stalled pointer lock, prefixed audio/fullscreen, denied audio/storage, service-worker getter denial, synchronous/asynchronous registration failure, saved-app preservation and retry |
| Custom-pack verification | Tests: missing crypto/subtle/digest, synchronous digest denial, CRC/SHA-256 mismatch, cached and downloaded valid custom packs |
| Firefox, Safari, Android/iOS devices | Live device checks remain outstanding; API fault tests do not establish these browsers' rendering or performance |

The launcher, PWA UI, service worker, touch and network regression suites pass.
The rebuilt `etl` target succeeds, and all seven generated offline bundle hashes
match their files. Live testing applied the update through App & offline rather
than clearing browser storage. The local preview server was restarted after it
was found stopped; the previous saved app remained usable while it was down.
Mute, 80% volume, desktop touch-off mode and the Oasis/4-bot/Normal selection
were preserved or restored. Evidence: `build_wasm/browser-support-launcher.jpg`,
`browser-support-game.jpg` and `browser-support-offline-ready.jpg`.

## Configured server browser audit (October 4, 2026)

The online card now queries the operator-configured UDP server instead of
equating a valid relay URL with a ready game server. It displays the server name,
current map, occupied/public slots, human count when supplied, password status
and the relay host's query round-trip time. This response time is not the
browser's in-game ping. Full-server information is advisory: private slots and
occupancy can change before a join. No public master-server list or arbitrary
server selector is provided; the relay still targets one configured upstream.

`misc/web/server_browser.py` sends a random getinfo challenge over connected UDP
and accepts only a valid matching reply. Responses and text fields are bounded;
color/control codes are stripped and server text is rendered with textContent.
The cache lasts ten seconds and simultaneous refreshes share one probe. UDP waits
are bounded, DNS runs in a daemon probe thread, and HTTP waits at most 1.8 seconds.
A missing/blocked query permits Try joining server, with an explicit unverified
status. A known protocol/game/mod mismatch disables joining before assets load
and leaves offline play available. The API ignores client-supplied target query
parameters. Server details depend on `server` and `udpPort` in web-server.json;
these must identify the relay's actual upstream.

Password-protected entries expand a password field and require a value to join.
Passwords are transient, are cleared on successful startup or switching offline,
and are excluded from browser settings snapshots even if a native profile had
marked the variable archived. The engine reads a memory-only browser-connect.cfg;
passwords do not enter its command line, where plus signs and demo-like strings
have special meaning. Illegal ET info-string characters are rejected. Custom
packs cannot replace this generated config. Pack-recovery reloads request a
password again while preserving exact required-pack selection. A real protected
server was not available; password delivery, validation and recovery were tested
with the launcher fixture, and the native engine executed the transient config
during the public-server join.

Live Chromium testing queried ET:Legacy 2.90.0 DEV at 104.248.140.165:27960,
showed Baserace Desert with 16/32 occupied slots and no password, downloaded its
missing map from the configured approved source, reloaded and reached Connected
to the game server. The native map rendered with active server bots. Returning to
the launcher worked, and Check again later updated the live map to Radar.
At 320 × 568 the card had no horizontal overflow, its password field and Join
button retained 44-pixel heights, and all actions remained reachable by scrolling.
The viewport was reset; the desktop online card remains open with saved offline
Oasis/4-bot/Normal options preserved.

This audit also caught an HTML-minifier rewrite of a control-character regex that
broke emitted launcher JavaScript. Password validation now uses character checks.
Before packaging, Node parses both generated inline scripts, engine glue and the
worker; packaging fails on syntax errors. Node must be on the build PATH.
The shared recovery page is served independently at `/network/recovery` and
embedded in the worker for evicted-cache recovery. It can update a saved app whose
launcher cannot run without clearing IndexedDB packs or browser settings. Live
testing repaired the failed saved launcher through this page. The same page
handles update deadlines, late replies and other-tab activation restrictions.

Validation: seven server-query tests, 24 preview/asset-route tests, 11 server-pack
tests, and launcher, network, PWA UI and worker suites pass. The final build passes
generated-code parsing and all seven offline bundle hashes match. Firefox,
Safari and physical-phone checks remain outstanding.
Evidence: `build_wasm/server-browser-live.jpg`, `server-browser-connected.jpg`,
`server-browser-mobile.jpg` and `server-browser-join-log.json`.

## Gameplay and rendering follow-up (October 8, 2026)

The gameplay audit found that touch users could not answer native fireteam or
vote prompts requiring F1/F2. Touch menu controls now include Yes and No,
delivering the same native `vote yes` / `vote no` commands without depending on
keyboard bindings. Both require focused, active gameplay and are disabled in
menus, console and loading. Conflicting answers within one input frame cancel;
focus/visibility transitions discard queued answers. Each toolbar target is at
least 44 by 44 CSS pixels, including at 320 by 568 without horizontal overflow.
The touch help also explains Jump to join the reinforcement queue when wounded,
and waiting for a Medic if the player wants a revive instead.

Live testing accepted Create a Fireteam with Yes, answered Make Fireteam private
with No, and reached Response Sent with the new fireteam visible. A separate
disposable `devmap oasis` test used negative health damage to create a wounded
player, then touch Jump entered the reinforcement queue and the next wave
deployed the selected Medic. The longer bot round described below uses the
ordinary launcher, without gameplay cheats or objective assistance.

The touch and launcher suites pass, as does the compiled production SDL input
harness covering prompt commands, contradictory answers, gameplay gates and
focus loss. The final build parses generated launcher/engine/worker JavaScript;
all seven PWA asset hashes match. Both updates were applied through App & offline
without clearing saved game packs or settings. The viewport was reset after the
responsive check. Evidence: `gameplay-touch-fireteam-prompt.jpg`,
`gameplay-touch-fireteam-answered.jpg`, `gameplay-touch-wounded.jpg`,
`gameplay-touch-deployed.jpg` and `gameplay-touch-final-mobile.png` in build_wasm.

The production WebGL shim was rebuilt into the GPU harness and passed all 50
checks with zero failures: diffuse/lightmap and alpha combination, grayscale
uploads and mip chains, framebuffer copies, fog, portal clipping, cutouts,
depth, three index formats, offscreen rendering and clean context restart.
Results and screenshots are `gameplay-render-checks.json` and
`gameplay-render-checks.png`. The temporary harness server has exited.

A full thirty-minute Oasis round with twelve Hard bots, six per team and a human
spectator, completed with an Axis win at the time limit. Native logs verify both water
pumps, the Allied command post, Old City capture, planting and breaching the
Old City wall, and destruction of the South Anti-Tank Gun. The North gun and
final attacking victory remain unverified. Sampled
native HUD readings in overview and moving combat views range from 22 to 59 FPS
on this machine at a 1920 by 1080 render resolution. These samples are not a
whole-round performance benchmark or evidence for Chromebook hardware.
Progress logs are saved in `gameplay-12bots-log.json`; the final scoreboard is
`gameplay-12bots-result.png`. Builds and network checks ran alongside the round,
so these HUD samples do not isolate renderer cost from other CPU work.

The expanded port goal remains open. Its next checks and deliverables are:

| Area | Evidence still needed / remaining work |
| --- | --- |
| Rendering and play accuracy | Broader moving-scene and sustained frame-time checks; preserve the passing pixel checks while resolving any observed visual or gameplay defects |
| Bots | Finish live attacking objective chains and match transitions on the bundled maps; synthetic map-script/class tests alone do not establish this |
| Offline play and assets | Recheck saved-app cold starts with the preview unavailable after the final bundle and ensure custom-map recovery still preserves packs and settings |
| Online play | Play, respawn and verify match/map transitions on a compatible live server, beyond the earlier spectator connection and asset-recovery proof |
| PWA | Recheck offline cold start, update/recovery and fullscreen against the final integrated changes |
| Server browser | Public discovery, filtering, selection and pack recovery are live-tested; verify a native server with matching WebAssembly module packages and then a compatible public entry |
| Graphics and Chromebook settings | Add and verify browser-facing graphics controls and Chromebook presets after the preceding integration work, including persistence and safe renderer restart |

Physical multitouch and cross-browser/device rendering remain device checks;
the Chromium runtime and API-fault fixtures do not establish them.

## Public server discovery and selection (October 8, 2026)

The combined `run_online.py` launcher now queries `master.etlegacy.com:27950`
for public entries, probes them with random challenged `getinfo` requests and
shares one cached catalog across HTTP clients. `public_servers.py` parses native
IPv4/extended IPv6 wire records, rejects non-public destinations, merges split
packets and matches the native 4096-server capacity. The live master ends some
packets immediately after the final port; this boundary is covered by a regression.
Eight UDP workers run one scan with a 90-second probe budget; HTTP reads return
immediately with partial results. A stalled master DNS lookup cannot spawn more
workers or leave the UI polling indefinitely. This round of live master testing
used IPv4; extended IPv6 records have fixture coverage.

Online now provides name/map search, a public Server selector, discovery progress
and refresh. Filters retain the selected destination; background results defer
rebuilding a focused native picker until blur. Each browser control is at least
44 pixels high at 320 by 568, with no horizontal overflow. The configured launcher
default remains available. A live check caught and fixed a default query being
misread as an invalid public ID. Native passwords remain transient and clear on
server changes. Public IDs and the selected label survive map-pack reloads even
when their relay ticket changes. File-verification progress uses the pack name,
rather than its internal hash-named cache file.

The launcher issues HMAC-signed public relay routes only for recently probed
eligible IDs; a browser cannot supply a hostname or port. Tickets contain a
public numeric destination and a four-hour expiry. The shared signing key is
generated in the combined runner's environment, never returned to the browser or
written to disk. Each WebSocket retains its selected connected UDP peer and the
existing origin, packet, queue and client limits. Standalone services without a
shared key keep the configured-server flow. Deployments with separate launcher
and relay processes must give both the same private `ETWASM_PUBLIC_SECRET`
(64 lowercase hexadecimal characters) and restart them together when changing it.

Live testing exposed a compatibility limit hidden by protocol-only discovery:
native Legacy releases can speak protocol 84 but omit the published wasm32 cgame
and UI entries required by this static client's pure-package validation. Legacy
2.84.0 and stable 2.86.0 archives were inspected and do not contain those entries.
The native pure check remains unchanged. An initial engine-build allowlist proved
insufficient: the official server still reports `2.86.0-34-g50cffc8` while requiring
a different native-only `legacy_v2.86.1.pk3`. Public eligibility now requires
Legacy protocol 84, a 2.86-or-newer engine and either explicit non-pure status or
`wasmModules=1`. The patched native server emits that field only when its exact
Legacy WebAssembly module entries reside in the same pure packages as its native
cgame and UI modules. A separate companion PK3 cannot satisfy normal server pure
validation. The browser still checks the actual server-allowed entries before
loading cgame; a missing entry produces an explicit compatibility error.

Server operators must publish a matching Legacy PK3 containing native modules
and `cgame.mp.wasm32.so` / `ui.mp.wasm32.so`, built from the same module sources
as this static browser client. The capability declaration does not add support
for arbitrary executable mods. Standard native-only releases do not satisfy it.
Live deployment of this native server/package combination remains to be tested.

An initial 399-entry master query had 144 protocol-compatible replies. The final
411-entry query yielded one candidate under the earlier engine-build filter,
the official development server. These are snapshots, not permanent
availability counts. Earlier signed-route checks reached two distinct upstreams,
End of Existence (Gold Rush) and LinuxGSM (Radar); these establish routing only.
Both were subsequently excluded from the pure browser list after package review.

Asset recovery now also resolves exact stable Legacy archives from the
[official release page](https://www.etlegacy.com/download/release/2840), using
the mod-only download link without executing page JavaScript. Only the exact
required PK3 is extracted; archive checks, full-byte verification, the server's
pack checksum and atomic publication still apply. A live 2.84 pack recovered and
auto-rejoined the same public ID, after which its native-only pure package caused
rejection. A LinuxGSM attempt recovered 2.86.0, 2.85.0 and 2.83.2 packs but its
2.78.1 mod-only archive was unavailable through this resolver. That server is now
excluded from the compatible list; broad old-release recovery remains unverified.

The official-server attempt required `legacy_v2.86.1.pk3` with checksum
`1005389300`. The approved mirror and official release/package catalog did not
provide it. A new bounded metadata-only native request discovered the server's
exact source: `https://game.etlegacy.com/legacy/legacy_v2.86.1.pk3`. The pack was
downloaded through the production verifier and matched that checksum, all ZIP
entry CRCs and SHA-256
`dcd43143fa4412a80780eb142373996ee849bace5fbb230aa22ecf9cc71c4238`.
Its 34,333,739 bytes contain native modules but no wasm32 entries. A subsequent
real join was rejected by the server's normal pure validation. The old spectator
connection used a different package and does not establish current availability.
The exact verified URL is now in the operator example's approved asset sources.

Source discovery requests only the first missing safe PK3 name, waits at most
three seconds, reads the redirect metadata and stops the native download. It
never writes UDP file data, fetches a redirect, opens a fallback page or installs
native DLLs. Cancellation and a fresh gamestate clear pending discovery. Online
userinfo enables WWW redirect replies while `cl_allowDownload` remains zero.
Redirects appear in native diagnostic logs; automatic recovery still accepts
only operator-approved sources and verifies exact server checksums.

Evidence includes `public-official-pack-verification.json`,
`public-browser-native-only-pack.png` and its native log. The earlier missing-pack
failure remains recorded in `public-browser-missing-official-pack.png`.

Validation: 12 public discovery/catalog/ticket checks, nine server-info checks,
11 real relay checks (including selected-peer isolation), 25 HTTP route checks,
13 server-asset checks, and launcher/network suites pass. Generated scripts parse
and all seven final offline bundle hashes match. The mobile layout and PWA update
were tested in Chromium while preserving installed packs and offline settings.
Evidence includes `public-server-catalog-final.json`, `public-pure-support.json`,
`public-relay-live.json`, `public-browser-supported.png`, `public-browser-mobile.png`
and the public pack-recovery/pure-failure logs in build_wasm.

### Controlled pure-server verification (8 October 2026)

A Windows x64 dedicated server built from this source successfully accepted the
browser on loopback with `sv_pure 1`. Radar rendered, the player joined Allies,
moved, died and respawned. Native `ClientBegin` and the suicide event were recorded.
Evidence: `build_wasm/pure-online-gameplay.png`, `pure-online-respawn.png`,
`pure-online-gameplay-log.json`, and `build_native/fixture/info-proof.json`.
This verifies one client on a controlled server; public deployment, multiple
clients, map transitions and objective completion remain unverified. The latest
public scan checked 410 servers and found no compatible entries.

Build native `etlded`, `cgame`, `ui` and `qagame` and the browser targets from the
same source. Native dependencies can be supplied with the CMake cache path
`ETL_BUNDLED_LIBS_DIR`; its default remains the repository `libs` directory.
Run `python misc/web/build_server_pack.py --compiler <emcc> --node <node>` to
create a matching native/WebAssembly Legacy PK3. The builder checks actual WASM
exports and validates the resulting archive and checksum. Install this PK3 in
both the server's `legacy` directory and the launcher's approved asset directory.
The server also needs its loose native qagame DLL and the original stock packs.
Generated packages and stock game data are excluded from Git.

Native Git builds can advertise `ET Legacy 2.86-dirty` instead of a dotted patch
version with a leading `v`. Discovery accepts both forms while still requiring
the explicit matching-module declaration for pure servers.

### Two-client map transition and combat verification (9 October 2026)

Two Chromium clients with independent origin storage joined the native loopback
pure server. Both rendered the Radar-to-Oasis transition. Real firing reduced
the shooter's magazine from 30 to 27 and the other player's health from 100 to
46; further shots produced synchronized Thompson death messages. These damage
checks used the fixture's enabled friendly fire at a common spawn.

Rapid transitions exposed a loading-state bug during a fast map restart. The
client now identifies checksum reports by the gamestate that supplied their
feed. Full server map loads receive distinct IDs even within one engine frame.
Fast restarts resend the gamestate to pending clients and preserve loading until
their handshake finishes. Active clients still need valid pure authentication
before processing movement. Production-function tests cover pending, primed,
active, invalid and bot states in browser and desktop configurations.

The corrected native server and both clients survived the rapid Oasis-to-ETL
Supply transition, including a fast restart during loading. Its log records map
IDs 64142 and 64143 and subsequent accepted checksum reports and `ClientBegin`
for both players. Screenshots: `pure-multiplayer-damage-a.png`,
`pure-multiplayer-damage-b.png`, `pure-rapid-transition-a.png`, and
`pure-rapid-transition-b.png` under `build_wasm`. Native fixture diagnostics are
in `build_native/fixture/restart-loading-server.log`.

Run an isolated launcher using `python misc/web/run_online.py --config <file>`.
The configuration supplies separate web/relay ports and a server target; child
services share that exact file through `ETWASM_SERVER_CONFIG`. Service logs
include port numbers. The normal `web-server.json` stays available for the
regular launcher. Same-origin tabs share a saved player identity: duplicate-key
errors now explain that the other game should be disconnected before retrying.
Distinct players use distinct browser profiles.

The final seven bundle hashes matched both local files and HTTP responses. After
activating that saved update, the fixture's HTTP and relay processes were stopped
and ports 8083/8084 were independently confirmed unavailable. A new Chromium tab
then cold-started the saved app, verified cached packs, started Oasis with four
Normal bots, and spawned a movable Allied Medic. Evidence:
`build_wasm/cold-offline-bots.png`, `cold-offline-bots-log.json`, and
`build_native/fixture/offline-host-stopped.json`. This tests an unavailable local
host with existing browser storage; it does not prove storage survival after
device cleanup or physical ChromeOS behavior.

An update-saving failure during development exposed a retry issue. Retry now
calls the registration's explicit update check; a status reply from the previous
saved bundle cannot hide a failed update. PWA unit checks cover that recovery and
the existing repair/update/fullscreen flows. The launcher suite, 26 HTTP checks,
native dedicated-server build, browser build and compiled pure-state tests pass.
Public server deployment, full attacker objective completion and physical mobile
and ChromeOS testing remain outstanding before the whole port can be complete.

The subsequent pure-package audit corrected an exhausted allowlist search that
accepted unlisted client packages. Compiled tests now execute the complete
production verifier: valid packages (including the final list entry), unlisted
packages with correctly encoded checksums, duplicates, incorrect encoding,
incorrect module packages, an empty allowlist and outdated reports. The unlisted
package regression fails against the original condition and passes after the
correction. Both browser and desktop test configurations and engine builds pass.

## Hosted launcher and retry follow-up (October 9, 2026)

An explicit PWA retry now reports a service-worker registration failure even
when the old app is saved. The saved app remains usable, the retry button stays
available, and an old `APP_READY` reply cannot hide the error. Synchronous and
asynchronous registration failures and subsequent successful recovery pass the
PWA suite. The worker suite and generated bundle parsing also pass.

The combined runner now supports an exact HTTPS proxy origin:

```sh
python misc/web/run_online.py --config web-server.json --public-origin https://play.example.org
```

Use the deployment's real hostname in place of `play.example.org`. The runner
keeps both HTTP and WebSocket services bound to `127.0.0.1`, advertises
`wss://play.example.org/relay` to the browser, and allows only that HTTPS origin
at the relay. It generates and shares the public-discovery signing key between
its children in memory. Without this option the existing localhost launcher
and its two allowed local origins continue to work.

The HTTPS proxy must terminate TLS for that hostname and forward `/relay` and
`/relay/*` to the configured loopback relay port (8082 by default), preserving
the complete path/query, browser Origin and WebSocket upgrade. All other paths
go to the configured loopback web port (8081 by default), preserving response
headers and content types. Public signed server selections use the `/relay/*`
paths as well as the configured default `/relay`. Serve a complete browser
bundle from the application root and provide the verified stock/custom pack
directories used by `serve.py`.

For a browser-compatible native server, use the matching native/WASM module
package described above and the compiled native server. Its public UDP port
must be reachable and it must advertise to the ET master for public discovery.
The configured default can target the same server. Configure approved pack
download sources when players need packages beyond the bundled assets.

Runner tests cover local defaults, normalized default/non-default TLS ports,
IPv6 origins, configuration propagation, shared private signing keys and
invalid origins. A real isolated loopback run returned the expected WSS URL,
accepted the configured HTTPS Origin and rejected the local HTTP Origin with
403. Evidence is `build_native/fixture/hosted-origin-proof.json`. This verifies
the backends and runner wiring; actual TLS termination, public hosting and
public browser gameplay still require a deployment target and live testing.

A new ordinary Radar match with twelve Hard bots is running for objective-chain
verification. Initial logs show all twelve bots joining, bunker capture during
warmup, and a side-entrance dynamite plant and defensive defuse during the round.
Native HUD samples at 1280 by 720 showed 56 FPS outdoors and 58 FPS indoors with
no concurrent builds; these are individual samples, not a sustained benchmark.
The round is still in progress. Initial evidence is
`build_wasm/radar-objective-round-progress.png` and `radar-objective-round-log.json`.

## Public ticket renewal and sustained Radar test (October 9, 2026)

Public Join and in-game Retry now fetch a fresh configuration for the selected
public ID before opening the relay. This avoids reusing an expired four-hour
ticket or a ticket invalidated by a hosted-service signing-key restart. Renewal
preserves both the server ID and relay operator; a changed or unverified
destination returns an actionable error instead of reusing the old ticket.
Returning to the launcher or cancelling an initial join aborts the pending
request, and late replies cannot open a connection. Closing the status dialog
alone lets a requested reconnect finish without replacing a newer dialog.

Launcher checks cover initial and retry renewal, rotated tickets, HTTP/network
failure, timeout, malformed JSON, unavailable/incompatible selections, changed
server IDs and relay operators, repeated clicks and cancellation. The loopback
HTTP suite verifies that a fresh ticket resolves to the same destination after
key rotation while the old ticket is rejected. All 26 HTTP checks, the complete
launcher/network suites and the rebuilt browser bundle pass. These checks do
not replace the pending public deployment test.

The saved-app update was also applied through the normal App & offline dialog.
The launcher retained Fuel Dump, twelve Hard bots and all eight custom-map
choices; evidence is `build_wasm/pwa-saved-update-preferences.png`. The separate
Radar bot match advanced through both entrance breaches, securing the Forward
Bunker, and stealing the West Radar Parts. The completed round and automatic
restart are recorded below.

## Vertex packing performance (October 9, 2026)

A short 495-frame `com_speeds` sample during the Radar round measured median
CPU frame work of 10 ms, renderer backend work of 8 ms and game work of 0 ms
(integer-resolution timer); their 95th percentiles were 15, 12 and 2 ms.
This includes diagnostic logging and console transitions and excludes GPU and
compositor time. Raw and summarized evidence are
`build_wasm/radar-frame-cost-sample.json` and `radar-frame-cost-summary.json`.

The renderer shim now resolves the common float-position/UV and byte-color
array layout once per draw. Its existing component decoder handles other
formats. Nonzero starting vertices, padded/interleaved strides, disabled color
and texture arrays, normalized byte colors and short-position fallback are
covered by the compiled performance checks. In alternating Node/WASM CPU-only
runs, packing 12,288,000 vertices took about 367 ms with the original code and
34–35 ms with the fast path. This isolates packing and is not an in-game FPS
claim or a Chromebook measurement.

The expanded real WebGL2 suite passes all 52 checks with zero failures,
including new strided byte-color and diffuse/lightmap pixel checks. Existing
fog, portal, cutout, depth, index-format and fresh-context checks still pass.
Evidence: `build_wasm/vertex-pack-gpu-checks.png` and
`vertex-pack-gpu-checks.json`. The final browser build succeeds and parses the
generated scripts. Radar completed using its original engine; the optimized
engine was then verified in Oasis as recorded below.

The balanced twelve-Hard-bot Radar test subsequently completed with an Allied
victory. Bots breached both entrances, secured the Forward Bunker, stole both
radar components through multiple defensive returns, and delivered West at
native time 884100 and East at 1146300. The human remained a spectator and gave
no objective assistance. The scoreboard shows six bots on each team. The
ordinary automatic transition then restarted Radar and all twelve bots entered
the next warmup. Evidence: `build_wasm/radar-bots-allied-win.png`,
`radar-bots-allied-win-log.json`, `radar-bots-next-round.png` and
`radar-bots-next-round-log.json`; earlier entrance-breach records remain in
`radar-objective-round-log.json`. This proves one complete attacking chain and
round transition, not all bundled-map objective chains.

The optimized bundle was applied through App & offline's saved-update dialog.
All seven locally generated bundle hashes matched the files served over HTTP
(`build_wasm/vertex-pack-bundle-hashes.json`). A fresh Oasis game with twelve
Hard bots rendered correctly, accepted movement, and fired the Thompson from
30 to 27 rounds using the documented mouse-capture fallback. The visible FPS
counter read 59 in that scene; this is a spot check, not a sustained performance
benchmark. Evidence: `build_wasm/vertex-pack-live-oasis.png` and
`vertex-pack-live-oasis-log.json`. The human then returned to spectator so the
bot round could continue.

## Integrated saved-app cold start (October 9, 2026)

The latest optimized bundle was applied to the regular localhost launcher using
its saved-update dialog. Fuel Dump, twelve Hard bots and all eight custom-map
choices survived the update. The launcher and relay process was then stopped;
TCP checks confirmed ports 8081 and 8082 unavailable before and after gameplay.
An entirely new tab opened the cached launcher with ETL Supply, four Hard bots,
and the installed custom-map inventory while both services remained stopped.
The custom map rendered, bots joined, and an Allied medic moved and fired the
Thompson from 30 to 27 rounds using the capture fallback. App & offline reported
"Offline ready in this browser." Fullscreen entered and exited with the game
controls accessible. Ending the match returned to the cached launcher and
restored the custom-map selection and inventory.

Still with the services stopped, the same launcher then loaded stock Fuel Dump
with twelve Hard bots. Native `status` confirmed map `fueldump`, the spectator
and twelve bot clients; the snow scene rendered normally. The original Fuel
Dump/twelve-Hard-bot match preference was restored through this selection.
Evidence: `final-offline-stock-status.png`, `final-offline-stock-log.json` and
`final-offline-stock-render.png`. The temporary test tab was closed and the
regular launcher and relay were restarted afterward.

Evidence is `build_wasm/final-offline-services-stopped.json`,
`final-offline-services-stopped-after-play.json`, `final-offline-custom-play.png`,
`final-offline-custom-log.json`, `final-offline-fullscreen.png` and
`final-offline-return.png`. The PWA UI and worker regression suites also pass.
This verifies this browser's saved app and already-installed assets with the
local services unavailable; it does not establish offline downloads of new
maps, physical-device behavior or cross-browser support.

## Saved-pack verification performance (October 9, 2026)

The launcher now updates its streaming CRC four bytes at a time with a 4 KiB
lookup table, retaining the same CRC-32 result and chunked verification flow.
This replaces the byte-at-a-time inner loop and leaves SHA-256 checks for custom
and server packs in place. `node misc/web/test_pack_crc.cjs` runs the actual
launcher implementation against independently generated Python zlib fixtures,
unaligned views, partial buffers, varied chunk boundaries and corruption at
word and tail offsets. The complete launcher fault suite also passes.

`node misc/web/test_pack_crc.cjs --benchmark` checks 256 MiB using both loops.
Three warmed Node CPU samples measured 450.0–451.2 ms for the original loop and
214.9–219.5 ms for the four-byte loop, with identical checksums. This isolates
CRC work; it does not measure IndexedDB reads, total startup time or Chromebook
hardware. The browser build and generated-script parsing pass, and all seven
local/served bundle hashes match (`build_wasm/pack-crc-bundle-hashes.json`).

The saved update was applied through the regular localhost app dialog. Its
existing Fuel Dump/twelve-Hard-bot preference and custom-map inventory survived.
Fuel Dump then started and rendered after the updated loop verified the real
cached stock packs; the HTTP log showed a manifest request and no pack
redownload. Evidence: `build_wasm/pack-crc-live-fueldump.png` and
`pack-crc-live-fueldump-log.json`. This startup check does not establish total
load-time improvement or a complete Fuel Dump attacking round.

## Browser custom render-size Apply and Back (October 9, 2026)

Changing only Custom Width or Custom Height previously failed to request a
renderer restart when Custom resolution was already selected. The fields also
changed engine cvars before Apply, so Back did not discard the edits. Browser
System settings now stage both dimensions, read pending latched dimensions when
opened, discard staged edits on Back, and apply them with restart detection.
Desktop menu behavior is unchanged.

`misc/web/test_system_settings.c` tests the actual staging helper, including
width-only and height-only changes, Back, Apply, reopening, non-custom mode and
an already-latched width. Compile it with Emscripten and run the output in Node:

```sh
emcc misc/web/test_system_settings.c -O2 -sENVIRONMENT=node -sWASM_ASYNC_COMPILATION=0 -o build_wasm/test_system_settings.cjs
node build_wasm/test_system_settings.cjs
```

The regression passes. Browser and native UI/cgame builds pass, the local
native/WASM fixture module pack was regenerated, and all seven local/served
bundle hashes match (`build_wasm/custom-size-bundle-hashes.json`). The fixture
pack refresh is not a deployment or a new online-session verification.

In the in-app browser, a solo Oasis match started at 960x540. Editing to
1024x576 then pressing Back restored 960x540. Repeating the edits and confirming
Apply restarted the renderer and produced an actual 1024x576 canvas. Ending the
match through Match setup and starting another match preserved 1024x576 and
rendered normally. Evidence: `build_wasm/custom-size-back-discard.png`,
`custom-size-applied.png`, `custom-size-applied-log.json`,
`custom-size-persisted.png` and `custom-size-persisted.json`. Browser graphics
presets and physical Chromebook validation remain outstanding.

## Browser graphics and Chromebook presets (October 9, 2026)

In-game Options > System now offers three browser presets. These set editable
graphics values and wait for Apply; Back discards preset edits. Shadows, sky
detail, simple sky and the FPS cap also stage rather than changing immediately.
Audio, controls, networking and map brightness remain independent of presets.

| Preset | Render size | Texture detail | Filtering | Dynamic lights | Shadows / detailed sky | FPS cap |
| --- | --- | --- | --- | --- | --- | --- |
| Chromebook / Low power | 960x540 | Medium | Bilinear, no anisotropy | Disabled | Off / off, simple sky on | 60 |
| Balanced | 1280x720 | High | Trilinear, 4x anisotropy | Single-pass | On / on | 60 |
| Quality | 1920x1080 | Very High | Trilinear, 16x anisotropy | Multi-pass | On / on | 60 |

Anisotropy is limited by device support. The browser scales the render canvas
to fit the available display. The low-power preset reduces pixel count,
texture memory and effects; physical Chromebook performance is not established
by the desktop in-app-browser check.

The actual-helper regression now checks all three presets, staged effects,
Back/Apply/reopening and invalid preset rejection. Browser and native UI/cgame
builds pass; the local native/WASM module fixture pack was refreshed. All seven
local/served bundle hashes match (`build_wasm/graphics-presets-bundle-hashes.json`).
The new build was activated through App & offline's saved-update flow on the
isolated test origin, preserving its saved game files and settings.

Live System-menu clicks staged Chromebook values without resizing the game;
Back restored the previous size, effects and 125 FPS cap. Confirming Apply
produced a rendered 960x540 canvas and the intended settings on reopening.
Quality and Balanced each restarted successfully at actual 1920x1080 and
1280x720 canvas sizes. Evidence: `build_wasm/graphics-presets-back.png`,
`graphics-presets-chromebook.png` and `graphics-presets-quality.png`.
Ending the match through Match setup and starting another solo Oasis match
preserved Balanced's 1280x720 size, High textures, trilinear/4x filtering,
single-pass lights, shadows, detailed sky and 60 FPS cap. The game rendered
normally. Evidence: `graphics-presets-persisted.png` and
`graphics-presets-live-log.json`.

## Balanced Oasis full bot round (October 9, 2026)

The preserved optimized-renderer Oasis match completed its full 30-minute
time limit with twelve Hard bots, six per team. Axis won and the native
intermission scoreboard rendered with all twelve bots. End-of-round statistics
record 236 Axis kills and 151 Allied kills; the human remained a spectator with
zero kills, deaths, damage given and score. Evidence:
`build_wasm/oasis-balanced-full-round-win.png` and
`oasis-balanced-full-round-log.json`. This proves a complete balanced round
reached its normal result without human combat assistance, not that the Allies
completed Oasis's attacking objective chain. This preserved match predates the
graphics preset build, so it is not a full-round preset performance test.
The same match then left intermission automatically, rejoined all twelve bots
and reached active combat in the next round. Native log entries show the
Oasis restart at 1982150 and twelve bot entries at 1983000. Evidence:
`oasis-balanced-next-round.png` and `oasis-balanced-next-round-log.json`.

## Public discovery status and search (October 9, 2026)

Searching the public list previously replaced scan progress and discovery
errors with a bare matching-server count. The launcher now retains the latest
validated scan state across search edits. It distinguishes an ongoing scan,
no browser-compatible servers, a search with no matches, an unavailable master
with or without recent entries, and an HTTP refresh failure. Failed refreshes
retain the last verified list; a successful retry clears the error. Empty-list
guidance offers refresh/offline play rather than suggesting a launcher default
that may itself be incompatible. Selection and compatibility gates are intact.

The launcher regression suite covers each state, search during a scan or error,
stale-list retention and successful retry. The browser build and emitted-script
parsing pass. Live public scans checked 412/412 and then 414/414 servers, finding zero eligible
browser-compatible entries; searching for Oasis preserved the scan progress
and final empty-list explanation. The configured default also reported an
unverified native build and Join remained disabled. This is discovery and
compatibility-gate verification, not public online gameplay proof.
The final bundle's seven local/served hashes match
(`build_wasm/public-status-bundle-hashes.json`). After stopping only the
temporary test HTTP service, Refresh public list displayed a real network
failure; editing the search to Radar retained that error. The regular launcher,
relay and bot matches were left running. Evidence:
`build_wasm/public-status-live-empty.png`, `public-status-live-empty-ax.txt`,
`public-status-live-failure.png` and `public-status-live-failure-ax.txt`.

## Graphics selection before the first map (October 9, 2026)

The launcher now exposes Graphics & performance for both play modes. Players
can select Chromebook / Low power, Balanced or Quality before starting the
renderer and loading textures. Use saved game settings is the default on each
launcher visit, so later in-game customizations are not overwritten by an old
preset selection. Selected presets become ordinary native graphics settings
and use the existing settings snapshot/persistence flow after startup.

Preset startup arguments precede the first map or connection command. The
launcher regression suite checks all presets on both startup paths, compares
their actual values against the native System-menu implementation, preserves
saved custom graphics, rejects unknown preset names, prevents a cancelled late
load from starting, and covers unavailable storage. Browser build and emitted
script parsing pass. No native modules changed in this launcher-only addition.

Live selection of Chromebook / Low power started the first solo Oasis map at
an actual 960x540 canvas size, with Medium textures, bilinear filtering, no
anisotropy or dynamic lights/shadows, simple sky and a 60 FPS cap. No Apply or
renderer restart was needed. Evidence: `build_wasm/startup-graphics-first-map.png`
and `startup-graphics-first-map-log.json`.

Visual inspection caught and corrected an overly wide graphics panel. The
final panel aligns with the match form. At a 320x568 test viewport, document
scroll width was 320, panel width 273, summary height 45 and selector height 44
pixels. The viewport override was reset. Evidence:
`startup-graphics-final-launcher.png` and `startup-graphics-narrow.png`.
These are desktop browser viewport checks, not physical Chromebook or mobile
performance measurements.
After changing the native FPS cap from 60 to 76 and applying it, End match and
return preserved the settings. The final launcher build defaulted to Use saved
game settings and started another Oasis match with four Hard bots. All four
joined; the System menu retained 960x540, the low-power graphics values and the
custom 76 FPS cap. Evidence: `startup-graphics-customization.png` and
`startup-graphics-customization-log.json`. The final bundle's seven local and
served hashes match (`startup-graphics-bundle-hashes.json`). The temporary test
tab and HTTP service were cleaned up; the existing bot-round tabs were retained.

## Final graphics bundle offline validation (October 9, 2026)

A fresh browser tab cold-started the final saved launcher on
`http://127.0.0.1:8083/` with that origin's HTTP service stopped. Socket checks
before and after play confirmed 8083 unavailable; the separate regular preview
and relay on 8081/8082 remained running. This verifies an unavailable app origin,
not an entire device disconnected from the internet.

The cached custom catalog and ETL Supply loaded with four Hard bots. All four
joined and rejoined after warmup. A human joined Allies as Medic through Limbo,
moved and fired during the active round (Thompson ammunition 30 to 27). The
render canvas retained the saved low-power 960x540 dimensions. App & offline
reported offline readiness; fullscreen entered and exited with controls visible.
End match and return loaded the cached launcher and preserved the custom-map
inventory, four bots and Hard difficulty. Graphics defaulted to saved settings.

Selecting Balanced and stock Radar while the origin remained unavailable
started another cached match with four bots at actual 1280x720. The native
System menu confirmed High textures, trilinear/4x filtering, single-pass lights,
shadows and detailed sky enabled, low-quality sky disabled, and a 60 FPS cap.
All four Radar bots joined. This is startup and settings integration evidence;
it does not establish a complete Radar round with this final bundle.

Evidence in `build_wasm`: `final-graphics-offline-origin.json`,
`final-graphics-offline-after-play.json`, `final-graphics-offline-ready.png`,
`final-graphics-offline-fullscreen.png`,
`final-graphics-offline-custom-active.png`,
`final-graphics-offline-custom-active-log.json`,
`final-graphics-offline-return.png`,
`final-graphics-offline-stock-settings.png` and
`final-graphics-offline-stock-log.json`. Launcher, PWA UI and worker regression
suites pass against the current source. Public compatible hosting and physical
Chromebook/mobile/browser checks remain outstanding.

## Online graphics and loading restart recovery (October 9, 2026)

Two independent-origin Chromium clients joined the controlled native pure
server with the current matching native/WebAssembly module pack (checksum
504815435). Launcher-selected Chromebook and Balanced settings produced actual
960x540 and 1280x720 canvases respectively. Both players joined teams, moved
and fired (30 to 27 rounds). On the low-power client, the native System menu
confirmed the preset values; selecting Balanced and confirming Apply restarted
the renderer at 1280x720 while retaining the online connection. Movement and
firing continued (27 to 24 rounds). These are loopback integration checks,
not public internet or Chromebook hardware performance measurements.

A Radar-to-ETL Supply transition exposed a remaining fast-restart race. Both
clients finished loading but stayed at Waiting for match until the connection
timeout. The server repeatedly ignored their previous map IDs while they were
still `CS_PRIMED`. The restart-window shortcut now applies to active clients;
loading clients can use their message acknowledgements to request a replacement
gamestate. Stale movement still returns without entering the world, and current
pure authentication remains required. Download/nextdl handling is unchanged.

The regression compiles the production packet-state block in browser and
desktop configurations. It covers primed/connected recovery across multiple
restarts, acknowledgements before/after the last gamestate, current/active
clients, downloading clients and the earlier full-map boundary, alongside the
existing invalid-pure rejection checks. Both pass, as do native/browser builds,
launcher and PWA UI/worker suites.

On the corrected server, both stalled clients reconnected through Retry.
Repeating the custom-map transition and forcing a fast restart while both
clients visibly loaded recovered both without a timeout. Native logs record
new checksum reports and `ClientBegin` for each, followed by the normal warmup
restart. The server-side retest retained the existing client engines to isolate
the recovery fix. Evidence: `build_native/fixture/restart-recovery-server.log`,
`restart-recovery-transition.log`, `restart-recovery-fast-restart.log`, and
`build_wasm/restart-recovery-custom-a.png` / `restart-recovery-custom-b.png`.
The initial failure is preserved in
`build_native/fixture/final-graphics-online-server.log`.

Graphics evidence in `build_wasm`: `final-graphics-online-play-a.png`,
`final-graphics-online-play-b.png`, `final-graphics-online-low-power.png` and
`final-graphics-online-restart-play.png`. All seven rebuilt local/served bundle
hashes match (`restart-recovery-bundle-hashes.json`). Public compatible hosting
and physical-device validation remain open.

The rebuilt browser bundle was then activated through the explicit saved-update
flow on one origin. It rejoined the pure ETL Supply server, retained Balanced's
1280x720 size using saved settings, spawned through Limbo, moved and fired
(30 to 28 rounds). Evidence: `restart-recovery-updated-play.png`,
`restart-recovery-updated-a-log.json` and `restart-recovery-peer-b-log.json`.
Filtered native handshake events are `build_native/fixture/restart-recovery-events.json`.
Both temporary clients and the isolated HTTP/relay/server processes were stopped
after verification; the regular preview services were left running.

## Browser HUD editor pointer mapping (October 9, 2026)

The HUD editor shrinks its rendered canvas to leave room for side and bottom
controls. Browser absolute mouse coordinates now follow its expanded virtual
grid, and panel hit testing receives the updated cursor immediately. Ordinary
UI, Limbo and fullscreen editor coordinates retain their existing scale.
Previously, clicking Clone could select a world HUD component instead.

`misc/web/test_menu_mouse.py` compiles the production client/cgame coordinate
functions and covers side/bottom panels, immediate clicks, fullscreen mode,
three resolutions, UI priority and invalid-size/no-catcher gates. It passes,
along with the input-focus regression and browser/native cgame builds. All seven
local and served bundle hashes match the service-worker manifest
(`build_wasm/hud-mouse-bundle-hashes.json`). The matching controlled-server
module pack was rebuilt with checksum 168848376; it is a local test artifact.

Live Chromium testing activated the rebuilt bundle through App & offline.
At Balanced 1280x720, holding Clone created an editable HUD, selecting FPS in
the bottom list selected the intended component, and its side-panel Visible
checkbox enabled the counter. Fullscreen editor component selection and return
worked, followed by exiting the editor and clicking Cancel in ordinary Limbo.
Evidence: `build_wasm/hud-mouse-editor-fixed.png`. The displayed FPS is a brief
spot check, not a sustained performance benchmark.

The asset audit passed for 5,162 entries. A separate Gold Rush match with twelve
Hard bots, six per team, is still in progress with a human spectator. Bots
repaired/stole the tank and destroyed both tank barriers; complete gold theft,
truck escape and round completion are not yet established. Progress is saved
in `build_wasm/goldrush-round-progress.png` and its companion log JSON.
A short 499-frame CPU timing sample had median 4 ms, p95 5 ms and maximum 9 ms;
it excludes GPU/display time and overlapped asset auditing. It is not a full
round or physical-device benchmark. Raw sample and summary are in
`build_wasm/goldrush-balanced-frame-sample.json` and
`goldrush-balanced-frame-summary.json`. Public compatible hosting and physical
device/browser validation remain outstanding.

## Touchscreen laptop control defaults (October 9, 2026)

Touch-capable Chromebooks and laptops no longer default to the touch overlay
solely because `maxTouchPoints` is nonzero. The primary pointer's coarse/fine
media query now determines the initial controls. Touch capability remains a
fallback when media queries are unavailable or throw, and an explicit saved
Touch on/off choice takes priority. The toolbar still allows either mode.

The touch regression covers fine-pointer devices with ten touch points,
coarse-pointer defaults, both saved overrides and failed-query fallbacks,
alongside existing multi-pointer, cancellation, focus and native-menu gates.
It passes; the browser rebuild and generated JavaScript parsing also pass.
All seven local/served cache hashes match
(`build_wasm/touch-primary-pointer-hashes.json`). These simulated pointer
conditions do not establish physical Chromebook behavior.

The rebuilt bundle was activated through the saved-update flow on an isolated
origin. A twelve-Hard-bot Rail Gun match started with the human spectating.
At 320x568 portrait and 568x320 landscape, the touch controls and toolbar stayed
visible. Team opened native Limbo, gameplay actions disabled in that menu,
and Menu returned to gameplay. Touch off and the ordinary viewport were then
restored. Evidence: `build_wasm/touch-primary-portrait.png` and
`touch-primary-landscape.png`. This checks layout and single-pointer menu
integration, not physical multi-touch play or a completed Rail Gun round.
The match remains available for objective-chain testing.

## Protected server password recovery (October 9, 2026)

A live protected native server exposed a recovery gap: Invalid password offered
Retry, but the dialog could not correct the rejected credential. The connection
dialog now reveals and focuses a replacement password field after that native
failure. Retry validates it with the same rules as initial Join, writes the
existing memory-only connection config, and clears the field. The native retry
bridge executes that config before connecting. Closing the dialog discards
typed credentials; successful connection clears the recovery state. Passwords
remain excluded from saved native browser settings.

Launcher regressions cover empty/invalid/overlong replacements, exact transient
config delivery, focus, close/success cleanup and filesystem-write failure.
The compiled production network bridge verifies the config-before-connect
command order. Launcher, native bridge, network and PWA UI checks pass, as do
the browser build and generated JavaScript parsing. All seven local/served
bundle hashes match (`build_wasm/password-retry-hashes.json`).

Live Chromium testing used a loopback-only, password-protected `sv_pure 1`
Radar server and matching module pack checksum 168848376. The browser displayed
the password requirement and rejected an intentionally wrong test password.
After activating the rebuilt bundle through App & offline, repeating rejection
focused the new field. Correcting the password and clicking Retry reached the
rendered map without a page reload. The player joined Allies as Medic, moved
and fired (30 to 27 rounds). A subsequent Disconnect and ordinary Retry again
reached Online using the corrected memory-only password. Native logs record
the successful connection/ClientBegin events. This is controlled loopback
evidence, not public internet or TLS hosting validation.

Evidence: `build_wasm/password-retry-before.png`,
`password-retry-fixed-dialog.png`, `password-retry-online-play.png`,
`password-retry-normal-reconnect.png`, `password-retry-browser-log.json`, and
`build_native/fixture/password-retry-native-events.json`. The isolated native
server, launcher and relay are no longer running.

The browser environment subsequently restored the Gold Rush and Rail Gun tabs
to launcher pages. Saved logs retain tank theft/both tank-barrier destruction
for Gold Rush and depot/track-switch activity for Rail Gun. Neither interrupted
round proves a complete attacking objective chain or final round result; both
still require continuous full-round testing.

## Browser module identities for pure servers (October 9, 2026)

Checking only `cgame.mp.wasm32.so` and `ui.mp.wasm32.so` filenames could reference
a different build's package while executing the browser's statically linked
modules. The browser build now publishes both side modules from its cgame/UI
archives and compiles their ZIP CRC and byte length into the engine. Only
entries matching both fields may satisfy its pure-module references. The
existing download-stage gate rejects a missing or different build before map
loading, with update/server selection guidance. Normal server package allowlists
and pure checksum verification remain required; desktop loading is unchanged.

Patched servers advertise `wasmCgame` and `wasmUI` alongside `wasmModules` only
when their native and browser modules occupy the required checksum containers.
Discovery compares both identities with `build_wasm/web-modules/identity.json`.
Missing metadata, missing fields, and CRC/length mismatches cannot qualify a
pure server. Non-pure discovery keeps its existing protocol/mod/version checks.
CRC and length follow the engine's ZIP integrity scheme; they are not
cryptographic code attestation.

Build `etl` before running `misc/web/build_server_pack.py`. The pack builder uses
the exact published side modules and checks SHA-256 digests of archives and
outputs, refusing stale or modified artifacts. Deploy the matching
`web-modules/identity.json` with the launcher's build directory for server-side
discovery. The browser also checks its installed pack, including when an older
cached app is used.

Browser and native dedicated-server builds passed. Production-code fixtures pass
in web and desktop configurations, covering CRC/size mismatches, pure/non-pure
download gates, ordinary pure allowlists, and restart recovery. All 77 Python
catalog/relay/asset/publication tests and launcher checks passed. All seven
local/served bundle hashes match (`build_wasm/module-identity-hashes.json`). The
matching native/WASM package retains checksum 168848376.

Live Chromium verification used the final build and a loopback `sv_pure 1` Radar
server. Its matching advertisement qualified for Join; the client reached
Online, rendered Radar, joined Allies as a medic, moved through the spawn
building, and fired six rounds (30 to 24). An initial older local pack correctly
stopped at the required-pack gate. The final attempt used a dedicated matching
asset directory. Native logs record ClientConnect at 33950, ClientBegin at 40150
and 76750, and explicit disconnect at 180750. The isolated server, launcher,
relay, and tab were stopped after testing.

Evidence: `build_wasm/module-identity-online-play.png`,
`module-identity-browser-log.json`, `module-identity-build.log`,
`module-identity-tests.log`, `module-identity-launcher-tests.log`, and
`build_native/fixture/module-identity-info.json` /
`module-identity-native-events.json`. This verifies controlled local pure-server
play; it does not establish public hosting or physical Chromebook behavior.

## Console text insertion (October 9, 2026)

Opening the browser console and inserting `/bot rollcall` without a key event
lost its first slash, turning the diagnostic into chat. SDL had already removed
the console-key text, but the engine also discarded the next character event.
Browser character dispatch now clears that redundant suppression without
discarding inserted text. Existing console-key character filtering remains
active, and desktop dispatch retains its previous behavior.

Compiled production event-dispatch and character-handler fixtures pass for
browser and desktop configurations: slash prefixes, Unicode character events,
ordinary text, and filtered grave/tilde/UK console characters. The input-focus
fixture also passes. The browser build and all seven served bundle hashes pass.
Live Chromium reproduced the missing prefix in the previous Gold Rush build;
a separate fresh Radar session with the fixed build preserved the first slash
and executed `/echo console-text-probe` immediately after opening the console.
This checks browser text insertion; physical IME/device coverage remains open.

Evidence: `build_wasm/console-text-prefix-before.png`,
`console-text-prefix-fixed.png`, `console-text-browser-log.json`,
`console-text-build.log`, and `console-text-hashes.json`. The isolated Radar tab
and port 8085 preview were closed after verification.

## Continuous Gold Rush bot round (October 9, 2026)

A new continuous Gold Rush round completed with twelve Hard bots (six per team)
and the human spectating throughout the 30-minute map clock. Axis won. The
results list all twelve bots; detailed stats show 263 Axis kills and 178 Allied
kills. The human spectator has zero kills, deaths, shots, and XP, with no team
play. Spectator follow commands and read-only bot diagnostics did not change
the match's objectives or teams.

Captured objective events include tank repair at 130450, theft at 133550,
destruction of Tank Barrier #1 at 253400, and another tank repair at 567400.
Later diagnostics found Allied engineers selecting `BUILD_Tank` and Axis bots
defending the second barrier. Follow views showed movement and combat near the
damaged tank. This verifies sustained gameplay and a defensive full-round
outcome, not the complete Allied attack chain: the second barrier/bank/gold/
truck chain remains unproven in this run. The round used build `b8261da`; its
bot scripts, navigation, gameplay modules, and renderer are unchanged by the
subsequent pure-module and console-text fixes.

Evidence: `build_wasm/goldrush-oct9-full-round-result.png`,
`goldrush-oct9-full-round-stats.png`, and
`goldrush-oct9-objective-round-log.json` (merged bounded console snapshots from
initial startup through the result).
The automatic next-round load rendered Gold Rush again; all twelve bots emitted
new game-entry events at 1922000 and the next warmup appeared normally. Evidence:
`build_wasm/goldrush-oct9-next-round.png` and
`goldrush-oct9-next-round-log.json`.

## Match return from deep links (October 9, 2026)

Starting with a Gold Rush / twelve Hard bots deep link, choosing Battery / four
Normal bots and then returning to setup restored the original query choices.
Validated offline Play now replaces the URL's map, bots, and difficulty with
the selected match, preserving other parameters, the fragment, and history
state. It does not add a navigation entry. Storage and history failures remain
nonfatal; invalid forms and online joins do not rewrite offline choices.

The launcher and PWA regression suites pass, and all seven rebuilt bundle files
match their local and served manifest hashes. Live Chromium started Battery
with four Normal bots, then the isolated port 8085 host was stopped. End match
and return loaded the cached launcher with those same choices. Playing again
without editing the form rendered Battery and produced four bot-entry events.
This verifies cached startup with that host unavailable, not whole-device
Internet disconnection or a completed Battery round. The isolated tab was
closed afterward; the separate Rail Gun round remained running.

Evidence: `build_wasm/match-query-battery-start.png`,
`match-query-offline-return.png`, `match-query-offline-restart.png`,
`match-query-offline-log.json`, and `match-query-hashes.json`.

## Rail Gun switch state audit (October 9, 2026)

The stock and ETX Rail Gun bot scripts each read `norththofswitch` in two
lowered-switch decisions, although the declared state is `northofswitch`.
The actual GameMonkey interpreter reproduced the ignored north-side state:
an unloaded tug with the switch lowered selected Allies even when the north
flag was set. Both scripts now read the declared field.

The compiled bot fixture executes both production scripts across all sixteen
combinations of ammo load, switch height, and north-side state. Eight normal
south-region entries also verify that the previous side is cleared and the
resulting team assignment stays correct. Normal south-region callbacks already
clear the north flag, so this correction is not evidence that the ongoing
match's slow progress was caused by the typo. Existing class, difficulty,
queued-event, population, and team-balancing regressions remain passing.

The browser build passes; all seven served bundle hashes match, and `etl.data`
contains both exact corrected scripts. A separate Chromium preview applied the
app update, rendered Rail Gun, initialized Omni-bot, and recorded four bot-entry
events. That preview and its port 8085 host were stopped afterward. The earlier
continuous twelve-Normal-bot round remains on its original build, with depot
captures, switch activity, and tug riding observed; its full outcome is still
pending. Neither the short startup check nor the script fixture proves the
complete ammo transport and firing chain.

Evidence: `build_wasm/railgun-switch-before.log`, `railgun-switch-after.log`,
`railgun-switch-build.log`, `railgun-switch-hashes.json`,
`railgun-switch-startup.png`, and `railgun-switch-startup-log.json`.
