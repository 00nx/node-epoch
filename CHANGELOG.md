# Node-Epoch
## 🆕 Changelog


# Node-Epoch
## 🆕 Changelog

### v2.3.0 — Observability & Robustness Release
**Quality-of-life improvements, better diagnostics, and production hardening**

#### Added
- New optional `return handle` behavior: `setEpochTimer(…)` now returns a string timer ID (hex handle) that can be used later for debugging or potential future cancellation
- Support for logging to file (optional): via new environment variable `EPOCH_TIMER_LOG_FILE=path/to/file.log`
- Timer age warning: logs `[WARN]` when a timer has been scheduled for > 24 hours
- Graceful degradation message when `napi_closing` is received during callback (helps diagnose unclean shutdowns)
- New log field: actual scheduled delay in human-readable form (e.g. "3h 14m 22s")
- Initial support for basic statistics collection (active timer count, total timers fired) — lo

- 
### v2.0.0
**Major rewrite & modernization**

#### Added
- Full RAII-style resource management using `std::unique_ptr<TimerState>`
- Destructor-based automatic cleanup of `Napi::ThreadSafeFunction`
- Visual section separators in `TimerCallback` for better code readability
- Early rejection of non-positive timer values
- Log warning hint for very long timers (> 24 hours)
- Support for more descriptive and grep-friendly log format with `[INFO]` / `[ERROR]` levels
- Explicit `std::flush` on log output
- `std::round` usage in time normalization for cleaner rounding

#### Changed
- Complete restructuring of the addon source code (separated types, helpers, logging, callback logic)
- Renamed variables for clarity (`delay_ms` → `ms_to_wait`, consistent `timer_handle` naming)
- Moved to modern C++ style (move semantics, better const usage, clearer naming)
- Improved logging system (consistent prefix, level indicators, more detailed messages)
- Reordered and clarified cleanup sequence in `TimerCallback`
- Immediate execution case now cleaner (no timer created when target already passed)

#### Fixed
- Potential resource leaks on timer creation failure paths
- Safer handling of `DeleteTimerQueueTimer` (properly ignores `ERROR_IO_PENDING` and `ERROR_SUCCESS`)
- Avoided use-after-free or double-free risks during cleanup
- Fixed logging format inconsistencies and missing context
- More robust map lookup and erasure in `activeTimers`

#### Improved
- Much better error messages with Windows error codes and context
- More readable and maintainable code structure
- Enhanced debuggability through improved log detail (target time, delay, handle values)
- Clearer input validation and early error throwing
- Consistent naming and documentation in code (supported units, etc.)

#### Breaking Changes
- None for JavaScript users — API remains identical (`setEpochTimer(unit, value, callback)`)
- Internal C++ implementation has been heavily refactored (should not affect existing usage)

---

### v1.0.2
#### Added
- Timestamped logging with current epoch milliseconds for better debugging
- Finalizer callback logging for ThreadSafeFunction (helps detect resource leaks)
- `WT_EXECUTELONGFUNCTION` flag to `CreateTimerQueueTimer` for better handling of potentially long-running JS callbacks

#### Changed
- Improved logging messages — more detailed and informative
- Reordered cleanup sequence in `TimerCallback` (delete timer → release tsfn → delete state) → safer & follows Windows recommendations
- More defensive activeTimers map lookup during cleanup

#### Fixed
- Potential resource leak when `CreateTimerQueueTimer` fails (properly release ThreadSafeFunction)
- Incorrect error checking for `DeleteTimerQueueTimer` (now ignores expected `ERROR_IO_PENDING`)
- Added missing `timer_handle` preservation before state deletion

#### Improved
- Better error messages including Windows GetLastError() codes
- More context in logs (target epoch time, calculated delay, timer handle)
- Immediate execution case now properly logged
