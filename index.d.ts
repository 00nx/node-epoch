/**
 * node-epoch — High-precision absolute epoch timer for Node.js on Windows.
 * TypeScript declarations.
 */

/** Time unit accepted by the API. */
export type TimeUnit = 's' | 'ms' | 'us' | 'ns';

/**
 * Schedule a callback at an **absolute** epoch timestamp.
 *
 * @param unit     Time unit of `value`.
 * @param value    Absolute Unix timestamp expressed in `unit`.
 * @param callback Invoked when the target time is reached.
 * @returns        An opaque handle usable with `clearEpochTimer`.
 *
 * @example
 * // Fire 5 seconds from now
 * const h = setEpochTimer('ms', Date.now() + 5000, () => {
 *   console.log('tick');
 * });
 */
export function setEpochTimer(
  unit: TimeUnit,
  value: number,
  callback: () => void,
): bigint;

/**
 * Cancel a pending timer.
 * Safe to call after the timer has already fired.
 *
 * @param handle The value returned by `setEpochTimer`.
 */
export function clearEpochTimer(handle: bigint): void;

/**
 * Return the current wall-clock time in the requested unit using the
 * highest-resolution source available on the current platform.
 *
 * @param unit Defaults to `"ms"`.
 */
export function getTime(unit?: TimeUnit): number;

/**
 * Adjust the Windows system timer resolution (Windows only).
 * Lower values give tighter jitter at the cost of CPU wake frequency.
 *
 * @param ms Resolution in milliseconds, clamped to [1, 16]. Defaults to 1.
 */
export function setResolution(ms?: number): void;

/** Diagnostic snapshot about the current addon state. */
export interface Diagnostics {
  /** Current OS platform string (e.g. `"win32"`). */
  platform: string;
  /** Whether the native Windows addon was loaded successfully. */
  nativeLoaded: boolean;
  /** Error message if the native addon failed to load, otherwise `null`. */
  nativeError: string | null;
}

/**
 * Returns diagnostic information about the addon.
 */
export function diagnostics(): Diagnostics;
