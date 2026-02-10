# Changelog

All notable changes to this project will be documented in this file.


## [2.0.0] - 10-02-2026

### Major Features & Improvements

- **Complete rewrite & modernization** of the native addon
  - Fully RAII-style resource management using `std::unique_ptr`
  - Proper cleanup of `Napi::ThreadSafeFunction` in `TimerState` destructor
  - Safer and more consistent handling of timer handles and state

- **Improved code structure and readability**
  - Separated concerns (types, helpers, logging, callback)
  - Clearer variable names (`ms_to_wait`, `timer_handle`, etc.)
  - Added visual section separators in critical callback logic
  - More modern C++ style (move semantics, `const`, better naming)

- **Enhanced logging**
  - Consistent, grep-friendly log format with levels `[INFO]` / `[ERROR]`
  - Added flush to stderr output
  - More descriptive messages (callback failures, long timers, etc.)

- **Better input validation**
  - Early rejection of non-positive values
  - Treat `0` as invalid instead of allowing it
  - Clearer error messages for invalid units or values

- **Immediate execution improvements**
  - Cleaner handling when target time has already passed
  - No timer is created in this case (performance & resource win)

### Bug Fixes

- Fixed potential resource leaks on timer creation failure
- Prevented double-free / use-after-free in cleanup path
- Corrected error handling for `DeleteTimerQueueTimer` (ignore expected `ERROR_IO_PENDING`)
- Fixed logging format inconsistencies
