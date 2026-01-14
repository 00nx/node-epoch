# Changelog

All notable changes to this project will be documented in this file.


## [1.0.2] - 2026-01-15

### Added
- Timestamped logging with current epoch milliseconds for better debugging
- Finalizer callback logging for ThreadSafeFunction (helps detect resource leaks)
- `WT_EXECUTELONGFUNCTION` flag to `CreateTimerQueueTimer` for better handling of potentially long-running JS callbacks

### Changed
- Improved logging messages — more detailed and informative
- Reordered cleanup sequence in `TimerCallback` (delete timer → release tsfn → delete state) → safer & follows Windows recommendations
- More defensive activeTimers map lookup during cleanup

### Fixed
- Potential resource leak when `CreateTimerQueueTimer` fails (properly release ThreadSafeFunction)
- Incorrect error checking for `DeleteTimerQueueTimer` (now ignores expected `ERROR_IO_PENDING`)
- Added missing `timer_handle` preservation before state deletion

### Improved
- Better error messages including Windows GetLastError() codes
- More context in logs (target epoch time, calculated delay, timer handle)
- Immediate execution case now properly logged

