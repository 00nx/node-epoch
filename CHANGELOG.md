# Changelog

All notable changes to node-epoch are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

---

## [2.0.0] — 2026-03

### Added
- **`clearEpochTimer(handle)`** — cancel a pending timer before it fires.
  `setEpochTimer` now returns a `BigInt` handle.
- **`getTime(unit?)`** — high-resolution current wall-clock via
  `GetSystemTimePreciseAsFileTime` (≈100 ns on Windows).
- **`setResolution(ms?)`** — tune `timeBeginPeriod` at runtime.
- **`diagnostics()`** — inspect native addon load state.
- **TypeScript declarations** (`index.d.ts`) bundled; no `@types` package needed.
- **Cross-platform fallback** — drift-corrected `setTimeout` loop on non-Windows
  so the same code works in CI (Linux/macOS) without conditional requires.
- **Comprehensive test suite** (`test/index.test.js`) covering fire accuracy,
  past-target, unit conversion, cancellation, ordering, stress test.
- **Benchmark script** (`test/bench.js`) for jitter distribution measurement.

### Changed
- Native addon rebuilt with **C++17**, `/O2 /GL /LTCG` on MSVC for maximum
  speed and smaller binary size.
- `ComputeDelayMs` now uses `GetSystemTimePreciseAsFileTime` instead of
  `GetSystemTimeAsFileTime` for more accurate delay calculation.
- Timer contexts stored in `std::unordered_map` for O(1) cancellation without
  touching the Windows Timer Queue.
- `package.json` updated: `exports` map, `engines` field, `types` field,
  updated `node-addon-api` to v8.
- `binding.gyp` adds `/fp:fast`, `/GS-`, `/Ob2`, `/Oi`, `/Ot`, and LTCG linker
  flags for release builds.

### Fixed
- Race condition between `TimerProc` and `clearEpochTimer` — atomic `fired`
  flag prevents double-dispatch when a timer is cancelled just as it fires.
- Memory leak on `CreateTimerQueueTimer` failure — context is now cleaned up
  on error path.
- `ThreadSafeFunction` finalizer correctly releases handle on addon unload.

---

## [1.x] — 2024–2025

Initial release. Core `setEpochTimer(unit, value, callback)` using Windows
Timer Queue + TSFN. Windows only, no cancellation, no TypeScript types.
