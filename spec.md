# Project instructions

## Repository orientation

- Read [README.md](README.md) for setup and the user workflow. Consult
  [spec/architecture.md](spec/architecture.md) for component boundaries,
  [spec/modules.md](spec/modules.md) for gameplay APIs, and the audio/texture
  guides when changing asset handling. Current source and validators take
  precedence over documentation summaries.
- The desktop editor is C11 with GTK4, JSON-GLib, Epoxy and libpng. Keep UI text
  and documentation in English, preserving the Windows 95/98 visual style.
- This repository contains PSX Studio and the standalone `psx-lib/` framework.
  One root `AGENTS.md` and this specification govern both; do not add nested
  instruction/spec.md files. See [framework reference](spec/framework.md) for
  C runtime, resource and SDK pipeline contracts.
- `editor/build.c` exports application C89 and invokes included PSX Lib.
  SDK compilation, resource-bank conversion, caching and CD verification belong
  to `psx-lib/`. Never send .psx files to the framework. It supports only C source
  applications and resource lists; do not restore document loading/game generation.
- Keep framework code independent of GTK, Epoxy, the editor and sibling checkouts.
  Host tools are C11 with GLib/GIO, JSON-GLib and libpng. Generated JSON manifests
  and cache receipts are build metadata, not game authoring schemas.
- Do not add a Python dependency to running, packaging or building games with
  PSX Studio. Python is only used by optional asset generators and developer
  pipeline tests. WAV-to-VAG and PNG-to-TIM conversion are native.
- Psy-Q, DOSBox-X, emulator and BIOS remain external. Settings configures only
  SDK and emulator; Studio locates its bundled psx-lib automatically. Ignore old
  engine_path project settings. The CLI --engine override is for development.
  PSX Lib resolves dosbox-x from PATH. Do not hardcode machine paths or commit
  credentials, SDKs, generated output or BIOS. Preserve the current output paths:
  `build/engine/ps1/NAME-HASH/` and `build/studio-projects/NAME-HASH/source/`.
- `examples/` is ignored and distributed separately through GitHub Releases.
  It is not required to compile or run the editor. Keep generic tests independent
  of example assets; example integration tests must skip when unavailable.
  Windows packages also exclude game examples. `psx-lib/examples/c-starter/` is
  a small tracked C starter independent of the optional game archive.

## Build and validation

- Inspect the working tree before editing and preserve unrelated user changes.
- `make` builds framework and editor; `make studio` / `make lib` build either.
  `make -C psx-lib` and `make -C psx-lib test` must work independently, without
  GTK or the parent project. Framework tests use self-contained fixtures and
  never discover sibling games/editors. `make test` runs both suites;
  `make test-studio` / `make test-lib` run them separately.
- On Linux, use `./build/psx-studio` to launch. On Windows,
  use `.\build.ps1` with MSYS2 UCRT64; the distributable folder is
  `build/windows/psx-studio/`. Keep its libraries and resources together.
- Use `make test` on Linux or `.\build.ps1 test` on Windows for host tests;
  these include Python pipeline tests. Run checks appropriate to the change and
  report unavailable dependencies, skipped checks and failures explicitly.
- For changes affecting console code or export, also build a game with Psy-Q
  and inspect it in an emulator when those external tools are available. A host
  test does not prove console compatibility or performance.
- Keep reusable implementation guidance in `spec/` and update it when contracts
  change. Validation procedures and known limitations belong in
  `spec/game-development.md`; do not maintain a per-task activity log. Report
  checks, skips and failures in the task or PR, distinguishing code review,
  host tests, real SDK builds and emulator observations.

## Continuing or creating games

- Before editing a `.psx` game, its assets or C modules, read
  [spec/game-development.md](spec/game-development.md) and the game-specific guide
  when present (for Shadow Ops, `examples/shadow-ops.md` in the separate archive).
- If a requested game is absent, obtain its separate example archive or project
  files before editing it; do not recreate a missing game from documentation.
- Treat that playbook as navigation and workflow guidance; current source and
  validators define the implementation. Keep the relevant guide updated when
  changing project data contracts, build procedures or gameplay assumptions.

## PlayStation runtime arithmetic

- Use integer or fixed-point arithmetic in all code executed by the PS1 game,
  including transforms, projection, clipping, collisions and gameplay modules.
- Do not introduce `float`, `double`, floating-point literals or calls to floating-
  point math functions into console execution paths. The PS1 CPU has no FPU;
  software floating-point operations are too costly for frame rendering.
- `editor/camera.c` contains desktop-only floating-point viewport helpers.
  Console arithmetic and rendering belong to psx-lib; generated
  application code and modules must use its integer/fixed-point APIs.
- Document each fixed-point format. Existing runtime scales and camera zoom use
  4096 = 1.0; runtime rotations use 4096 units per turn. Check intermediate
  arithmetic for overflow on the PS1's 32-bit `long`, not just the host's type.
- Keep gameplay increments and timers tied to fixed simulation steps, not drawn
  frames. The PAL loop uses 50 steps/second; keep GPU/video polling nonblocking
  and service fresh input/logic between rendering batches. Render snapshots must
  stay coherent while live objects advance. Keep game callbacks out of interrupts.
  Reset timing after blocking Scene loads. Test equivalent gameplay at different
  render cadences; do not add floating-point delta time to console modules.
- Keep exported code, console runtime and example C modules strict ANSI C89,
  compatible with the project's Psy-Q toolchain. Validate arithmetic changes with
  host reference tests and a real Psy-Q build; measure performance in the emulator.

## Low-poly PS1 assets

- Always create low-poly models and maps for PS1. Avoid unnecessary geometry;
  preserve the intended silhouette, texture mapping and necessary depth ordering.
- Do not impose an arbitrary fixed triangle maximum on models or demo scenes.
  Choose detail according to visual needs and measured performance.
- Treat importer/exporter capacity limits as technical ceilings, not recommended
  asset complexity. Verify FPS with gameplay input and camera movement.
- Preserve texture UVs and gameplay/collider dimensions when reducing geometry.
  Rebuild with Psy-Q and inspect the result in the emulator.

## Automated GUI verification

- On Linux, use an isolated Xvfb display for editor, emulator and DOSBox checks. Explicitly
  unset `WAYLAND_DISPLAY`/`WAYLAND_SOCKET`, set `GDK_BACKEND=x11` and
  `SDL_VIDEODRIVER=x11`; Xvfb alone does not stop SDL from selecting the real
  Wayland desktop. Stop test processes when verification is finished.
- For headless DOSBox Staging checks, use a temporary configuration with
  `output=texture` and `texture_renderer=software`, without the user's primary
  configuration. Deprecated `output=surface` can fall back to OpenGL and fail
  under Xvfb.
- On Windows, use a dedicated test VM/session for automated GUI checks; Xvfb
  instructions apply only to Linux. Avoid interfering with the user's desktop
  and clean up processes started for verification on either platform.
