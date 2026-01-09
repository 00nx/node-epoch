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

# or after publishing to npm:
# npm install node-epoch
