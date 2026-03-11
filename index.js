'use strict';

/**
 * node-epoch — High-precision absolute epoch timer for Node.js on Windows.
 *
 * Falls back to a corrected setTimeout implementation on non-Windows
 * platforms so library consumers can target multiple OSes without
 * conditional requires.
 */

const os = require('os');

// ─── Native binding ────────────────────────────────────────────────────────

/** @type {import('./types').NativeBinding | null} */
let native = null;
let nativeLoadError = null;

try {
  native = require('bindings')('epoch');
} catch (err) {
  nativeLoadError = err;
}

const IS_WINDOWS = os.platform() === 'win32';

// ─── Unit → ms multiplier ──────────────────────────────────────────────────

const UNIT_TO_MS = {
  s:  1e3,
  ms: 1,
  us: 1e-3,
  ns: 1e-6,
};

const VALID_UNITS = new Set(Object.keys(UNIT_TO_MS));

/**
 * Validate & normalise call arguments, throwing TypeError on bad input.
 *
 * @param {string} unit
 * @param {number} value
 * @param {Function} callback
 */
function validateArgs(unit, value, callback) {
  if (!VALID_UNITS.has(unit)) {
    throw new TypeError(
      `setEpochTimer: invalid unit "${unit}". Must be one of: ${[...VALID_UNITS].join(', ')}`
    );
  }
  if (typeof value !== 'number' || !Number.isFinite(value) || value < 0) {
    throw new TypeError(
      `setEpochTimer: value must be a finite non-negative number (got ${value})`
    );
  }
  if (typeof callback !== 'function') {
    throw new TypeError('setEpochTimer: callback must be a function');
  }
}

// ─── Fallback timer (non-Windows or missing native build) ─────────────────

/**
 * Drift-corrected setTimeout fallback.
 * Schedules in 1 ms increments when very close to the target time to
 * compensate for OS timer coarseness.
 *
 * @param {string}   unit
 * @param {number}   value
 * @param {Function} callback
 * @returns {{ cancel: () => void }}
 */
function fallbackTimer(unit, value, callback) {
  const targetMs = value * UNIT_TO_MS[unit];
  let timeout = null;
  let cancelled = false;

  function schedule() {
    if (cancelled) return;
    const remaining = targetMs - Date.now();

    if (remaining <= 0) {
      // Fire on next tick to keep behaviour consistent with native
      setImmediate(callback);
      return;
    }

    // Within 50 ms: spin in 1 ms increments for better accuracy
    const delay = remaining > 50 ? remaining - 10 : 1;
    timeout = setTimeout(schedule, delay);
  }

  schedule();

  return {
    cancel() {
      cancelled = true;
      if (timeout !== null) clearTimeout(timeout);
    },
  };
}

// ─── Handle registry (for clearEpochTimer bookkeeping on fallback path) ───

const fallbackHandles = new Map(); // id → { cancel }
let nextFallbackId = BigInt(1);

// ─── Public API ────────────────────────────────────────────────────────────

/**
 * Schedule a callback at an absolute epoch timestamp.
 *
 * @param {"s"|"ms"|"us"|"ns"} unit  - Time unit of `value`.
 * @param {number}             value - Absolute Unix timestamp in `unit`.
 * @param {() => void}         callback - Invoked when the target time is reached.
 * @returns {bigint} An opaque handle that can be passed to `clearEpochTimer`.
 *
 * @example
 * const handle = setEpochTimer('ms', Date.now() + 3000, () => {
 *   console.log('fired after ~3 s');
 * });
 */
function setEpochTimer(unit, value, callback) {
  validateArgs(unit, value, callback);

  // Native path (Windows + compiled addon)
  if (IS_WINDOWS && native) {
    return native.setEpochTimer(unit, value, callback);
  }

  // Fallback path
  const id = nextFallbackId++;
  const handle = fallbackTimer(unit, value, () => {
    fallbackHandles.delete(id);
    callback();
  });
  fallbackHandles.set(id, handle);
  return id;
}

/**
 * Cancel a pending timer created with `setEpochTimer`.
 * Safe to call after the timer has already fired (no-op).
 *
 * @param {bigint} handle - The value returned by `setEpochTimer`.
 */
function clearEpochTimer(handle) {
  if (handle === undefined || handle === null) return;

  // Native path
  if (IS_WINDOWS && native) {
    if (typeof handle !== 'bigint') {
      throw new TypeError('clearEpochTimer: handle must be a BigInt');
    }
    native.clearEpochTimer(handle);
    return;
  }

  // Fallback path
  const entry = fallbackHandles.get(handle);
  if (entry) {
    entry.cancel();
    fallbackHandles.delete(handle);
  }
}

/**
 * Return the current wall-clock time in the requested unit using the
 * highest-resolution source available.
 *
 * On Windows this calls `GetSystemTimePreciseAsFileTime` (≈100 ns).
 * On other platforms it returns `Date.now()` converted to the unit.
 *
 * @param {"s"|"ms"|"us"|"ns"} [unit="ms"]
 * @returns {number}
 */
function getTime(unit = 'ms') {
  if (!VALID_UNITS.has(unit)) {
    throw new TypeError(`getTime: invalid unit "${unit}"`);
  }

  if (IS_WINDOWS && native) {
    return native.getTime(unit);
  }

  // Cross-platform fallback: use hrtime for sub-ms precision
  const [sec, nano] = process.hrtime();
  const epochOffsetNs = BigInt(Date.now()) * BigInt(1_000_000);
  const hrtimeNs = BigInt(sec) * BigInt(1_000_000_000) + BigInt(nano);
  // Approximation: hrtime doesn't give absolute epoch, use Date.now() instead
  const nowMs = Date.now();
  if (unit === 's')  return nowMs / 1000;
  if (unit === 'ms') return nowMs;
  if (unit === 'us') return nowMs * 1000;
  if (unit === 'ns') return nowMs * 1_000_000;
  return nowMs;
}

/**
 * Adjust the Windows system timer resolution.
 * Only has effect on Windows with the native addon loaded.
 * Lower values → tighter jitter but higher CPU wake rate.
 *
 * @param {number} [ms=1] - Resolution in milliseconds (1–16).
 */
function setResolution(ms = 1) {
  if (IS_WINDOWS && native) {
    native.setResolution(ms);
  }
}

/**
 * Returns diagnostic information about the current state of the addon.
 *
 * @returns {{ platform: string, nativeLoaded: boolean, nativeError: string|null }}
 */
function diagnostics() {
  return {
    platform:     os.platform(),
    nativeLoaded: native !== null,
    nativeError:  nativeLoadError ? nativeLoadError.message : null,
  };
}

// ─── Exports ────────────────────────────────────────────────────────────────

module.exports = {
  setEpochTimer,
  clearEpochTimer,
  getTime,
  setResolution,
  diagnostics,
};
