# node-epoch

**High-precision absolute-time epoch timer for Node.js on Windows**

[![License: GPL-3.0](https://img.shields.io/badge/License-GPL--3.0-blue.svg)](LICENSE)
[![Node.js ≥14](https://img.shields.io/badge/node-%3E%3D14-brightgreen)](https://nodejs.org)
[![Windows only](https://img.shields.io/badge/platform-Windows-lightgrey)]()

Ultra-low jitter timer that fires at an **absolute Unix epoch timestamp** — not
a relative delay. Built on the Windows Timer Queue (`CreateTimerQueueTimer`) +
`timeBeginPeriod(1)` for 1 ms system resolution, dispatching callbacks back
onto the Node.js event loop via N-API `ThreadSafeFunction`.

Typical jitter: **1–5 ms** under load (vs. 10–50 ms with plain `setTimeout`).

---

## Table of Contents

- [Features](#features)
- [Requirements](#requirements)
- [Installation](#installation)
- [Quick Start](#quick-start)
- [API Reference](#api-reference)
  - [setEpochTimer](#setepochtimerunit-value-callback--bigint)
  - [clearEpochTimer](#clearepochtimerhandle)
  - [getTime](#gettimeunit--number)
  - [setResolution](#setresolutionms)
  - [diagnostics](#diagnostics--diagnostics)
- [Architecture](#architecture)
- [Benchmarking](#benchmarking)
- [Build from Source](#build-from-source)
- [TypeScript](#typescript)
- [Fallback Mode](#fallback-mode)
- [Migration from v1](#migration-from-v1)
- [FAQ](#faq)

---

## Features

- Fire callbacks at **absolute epoch timestamps** (seconds, ms, µs, or ns)
- **Cancellable** — cancel a pending timer any time before it fires
- `getTime()` — current wall-clock via `GetSystemTimePreciseAsFileTime` (≈100 ns resolution)
- `setResolution()` — tune the Windows timer period (default 1 ms)
- Full **TypeScript declarations** included
- Graceful **cross-platform fallback** for non-Windows environments
- Zero dependencies at runtime beyond the compiled `.node` addon

---

## Requirements

| | |
|---|---|
| **OS** | Windows 10 / Server 2016 or newer |
| **Node.js** | ≥ 14.0.0 |
| **Build tools** | Visual Studio Build Tools 2019+ (for `npm install` compilation) |

---

## Installation

```sh
# From GitHub (recommended)
npm install github:00nx/node-epoch

# After cloning
npm install && npm run build
```

> **Note:** `npm install` automatically compiles the native addon via `node-gyp`.
> You need the Visual Studio C++ build tools installed. Run
> `npm install -g windows-build-tools` if you don't have them.

---

## Quick Start

```js
const { setEpochTimer, clearEpochTimer, getTime } = require('node-epoch');

// Fire exactly 3 seconds from now
setEpochTimer('ms', Date.now() + 3000, () => {
  console.log('fired at', new Date().toISOString());
});

// Fire at a specific Unix timestamp (seconds precision)
const targetUnixSec = Math.floor(Date.now() / 1000) + 5;
setEpochTimer('s', targetUnixSec, () => {
  console.log('second-precision timer fired');
});

// Cancel a timer
const handle = setEpochTimer('ms', Date.now() + 10000, () => {
  console.log('this will never print');
});
clearEpochTimer(handle);

// Nested precision example (pre-arm inner timer from outer)
const now = Date.now();
setEpochTimer('ms', now + 5000, () => {
  console.log('outer fired — arming inner...');
  setEpochTimer('ms', now + 5500, () => {
    console.log('inner fired 500 ms later');
  });
});
```

---

## API Reference

### `setEpochTimer(unit, value, callback)` → `bigint`

Schedule `callback` to be called when the wall clock reaches `value` in `unit`.

| Parameter  | Type       | Description |
|------------|------------|-------------|
| `unit`     | `string`   | Time unit: `"s"`, `"ms"`, `"us"`, or `"ns"` |
| `value`    | `number`   | Absolute Unix timestamp in the given unit |
| `callback` | `Function` | Invoked with no arguments when the target time is reached |

**Returns** a `bigint` handle that can be passed to `clearEpochTimer`.

If `value` is in the past or present, the callback fires on the **next tick**
(equivalent to `setImmediate`).

```js
// Microsecond-precision example
setEpochTimer('us', (Date.now() + 500) * 1000, () => {
  console.log('fired at ~500 ms from now');
});
```

---

### `clearEpochTimer(handle)`

Cancel a timer before it fires. Safe to call after the timer has already fired
(no-op). Safe to call multiple times with the same handle.

```js
const h = setEpochTimer('ms', Date.now() + 5000, doWork);
// Changed our mind
clearEpochTimer(h);
```

---

### `getTime([unit])` → `number`

Return the current high-resolution wall-clock time.

On Windows, uses `GetSystemTimePreciseAsFileTime` (≈100 ns resolution).
On other platforms, falls back to `Date.now()` in the requested unit.

| Parameter | Type     | Default  | Description |
|-----------|----------|----------|-------------|
| `unit`    | `string` | `"ms"`   | `"s"`, `"ms"`, `"us"`, or `"ns"` |

```js
const nowUs = getTime('us');  // current time in microseconds
```

---

### `setResolution([ms])`

Tune the Windows system timer resolution. Lower values give tighter jitter
but increase CPU wake rate. Has no effect on non-Windows platforms.

| Parameter | Type     | Default | Range  |
|-----------|----------|---------|--------|
| `ms`      | `number` | `1`     | 1 – 16 |

```js
// Already default — 1 ms is the minimum Windows allows
setResolution(1);
```

---

### `diagnostics()` → `Diagnostics`

Returns an object useful for debugging:

```js
{
  platform:     'win32',
  nativeLoaded: true,
  nativeError:  null
}
```

---

## Architecture

```
Node.js JS thread
┌───────────────────────────────────────────────┐
│  setEpochTimer('ms', target, cb)              │
│    → validates args                           │
│    → ComputeDelayMs(target - now)             │
│    → CreateTimerQueueTimer(delayMs)  ─────────┼──► Windows thread pool
│    → returns BigInt handle                    │         │
└───────────────────────────────────────────────┘         │ (on expire)
                                                          ▼
                                                    TimerProc()
                                                    tsfn.NonBlockingCall()
                                                          │
                ┌─────────────────────────────────────────┘
                ▼  (libuv loop — JS thread)
           callback()   ← your function runs here
```

Key design decisions:

- **`timeBeginPeriod(1)`** is called once at module load to request 1 ms timer
  resolution from Windows. This is the tightest the Timer Queue can reliably
  achieve without busy-waiting.
- **`GetSystemTimePreciseAsFileTime`** is used instead of `GetSystemTimeAsFileTime`
  for the current-time reads, giving ≈100 ns accuracy on modern hardware.
- **`ThreadSafeFunction`** ensures callbacks are always dispatched on the V8
  thread, making the addon safe with worker threads and during graceful shutdown.
- Timers are stored in an `unordered_map` behind a mutex, enabling O(1) lookup
  for cancellation without touching the Windows Timer Queue.

---

## Benchmarking

```sh
# 200 timers spread over 1 second (default)
node test/bench.js

# Custom: 500 samples over 2 seconds
node test/bench.js 500 2000
```

Example output on a modern Windows machine:

```
node-epoch benchmark — 200 samples over 1000 ms spread

  samples : 200
  min     : 0.200 ms
  mean    : 1.841 ms
  p50     : 1.512 ms
  p95     : 4.203 ms
  p99     : 6.841 ms
  max     : 8.102 ms
```

---

## Build from Source

```sh
git clone https://github.com/00nx/node-epoch.git
cd node-epoch
npm install
npm run build        # release build
npm run build:debug  # debug symbols
npm test             # run test suite
```

Requirements: Node.js ≥14, npm, Python 3, Visual Studio Build Tools 2019+.

---

## TypeScript

TypeScript declarations are bundled (`index.d.ts`). No `@types` package needed.

```ts
import { setEpochTimer, clearEpochTimer, getTime, TimeUnit } from 'node-epoch';

const unit: TimeUnit = 'ms';
const handle: bigint = setEpochTimer(unit, Date.now() + 1000, () => {
  console.log('typed timer fired');
});
```

---

## Fallback Mode

On non-Windows platforms (Linux, macOS) or when the native addon fails to
compile, `node-epoch` automatically falls back to a **drift-corrected
`setTimeout` loop**. Jitter in fallback mode is typically 5–20 ms.

Check whether the native addon loaded:

```js
const { diagnostics } = require('node-epoch');
const d = diagnostics();
console.log(d.nativeLoaded); // false on non-Windows
```

---

## Migration from v1

| v1 | v2 |
|---|---|
| `setEpochTimer(unit, value, cb)` — no return value | Now returns a `bigint` handle |
| No cancellation | `clearEpochTimer(handle)` |
| No `getTime()` | `getTime(unit?)` added |
| Windows only (throws on other OSes) | Graceful fallback on non-Windows |
| No TypeScript types | `index.d.ts` bundled |

---

## FAQ

**Why not `setTimeout` with a calculated delay?**

`setTimeout` uses a relative delay computed once at call time. Under system
load the callback can be deferred by 10–50 ms. node-epoch recalculates the
delay from the current time at the last possible moment and uses the Windows
Timer Queue, which is woken by a dedicated kernel timer (not the event loop
tick) — resulting in consistently lower jitter.

**Can I use this in a Worker Thread?**

Yes. Each Worker has its own event loop, and `ThreadSafeFunction` is
per-thread. Timers created in a Worker fire on that Worker's event loop.

**What happens if I set thousands of timers?**

The Windows Timer Queue handles thousands of concurrent timers efficiently.
The JS side stores handles in an `unordered_map` — O(1) insert and lookup.
Memory per timer is approximately 200 bytes.

**Is the 1 ms resolution guaranteed?**

`timeBeginPeriod(1)` *requests* 1 ms resolution from Windows, but the OS may
not honour it on all hardware or under all power plans. On battery, Windows
may coarsen timers to save power. Use a fixed power plan for production.

---

## License

[GPL-3.0](LICENSE) © 00nx
