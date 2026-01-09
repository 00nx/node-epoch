# node-epoch

**High-precision absolute epoch timer for Node.js on Windows**  
Ultra-low jitter timer using Windows Timer Queue — perfect for sniping, trading  and real-time applications where `setTimeout` is too slow or drifts too much.

Current typical jitter: **1–10 ms** (often better than native `setTimeout` under load)

**Windows only** — uses `CreateTimerQueueTimer` + `ThreadSafeFunction`

## Features

- Trigger callbacks at **absolute epoch timestamps** (not relative delays)
- Non-blocking (uses Windows thread pool)
- Supports units: seconds, milliseconds, microseconds, nanoseconds
- Very low overhead compared to busy-wait approaches
- Designed for latency-sensitive use-cases (Discord vanity sniping, trading, automation)

## Installation

```bash
# Recommended: from GitHub (latest version)
npm install github:00nx/node-epoch
```


## quickstart

```js
const { setEpochTimer } = require('node-epoch');

console.log("Arming precise epoch timers...");

// 1. Fire exactly 5 seconds from now (seconds precision)
setEpochTimer("s", Math.floor(Date.now() / 1000) + 5, () => {
  console.log("→ 5-second epoch timer fired!", new Date().toISOString());
});

// 2. Most common: millisecond precision
setEpochTimer("ms", Date.now() + 3500, () => {
  console.log("→ 3.5 second precise timer!", new Date().toISOString());
});

// 3. Warmup + ultra-low latency claim example (vanity/discord style)
const now = Date.now();
setEpochTimer("ms", now + 5000, () => {
  console.log("[warmup] 5s warmup complete");
  setEpochTimer("ms", now + 5500, () => {
    console.log("[claim] ULTRA LOW LATENCY CLAIM FIRED", new Date().toISOString());
  });
});```
