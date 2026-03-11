/**
 * node-epoch — High-precision absolute-time timer for Node.js on Windows
 *
 * Uses Windows Timer Queue (CreateTimerQueueTimer) combined with
 * timeBeginPeriod(1) to achieve 1 ms system timer resolution.
 * Callbacks are dispatched back onto the Node.js event loop via
 * Napi::ThreadSafeFunction so the addon is fully thread-safe.
 *
 * Exported functions:
 *   setEpochTimer(unit, value, callback) → timerHandle (BigInt)
 *   clearEpochTimer(timerHandle)
 *   getTime(unit) → number  (current wall-clock in requested unit)
 *   setResolution(ms)       (call timeBeginPeriod; default 1)
 */

#define NAPI_VERSION 6
#include <napi.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>   // timeBeginPeriod / timeEndPeriod
#include <timeapi.h>

#include <cstdint>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <memory>
#include <string>

#pragma comment(lib, "winmm.lib")

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

// All live timer contexts indexed by an auto-increment id.
static std::mutex                                        g_mapMtx;
static std::unordered_map<uint64_t, struct TimerContext*> g_timers;
static std::atomic<uint64_t>                             g_nextId{1};
static std::atomic<UINT>                                 g_currentPeriod{0};
static HANDLE                                            g_timerQueue{nullptr};
static std::once_flag                                    g_queueOnce;

// ---------------------------------------------------------------------------
// TimerContext — one allocation per pending timer
// ---------------------------------------------------------------------------

struct TimerContext {
    uint64_t                   id;
    Napi::ThreadSafeFunction   tsfn;
    HANDLE                     timerHandle{nullptr};
    std::atomic<bool>          fired{false};   // prevent double-fire on cancel race
    std::atomic<bool>          cancelled{false};

    explicit TimerContext(uint64_t _id, Napi::ThreadSafeFunction _tsfn)
        : id(_id), tsfn(std::move(_tsfn)) {}

    ~TimerContext() {
        tsfn.Release();
    }
};

// ---------------------------------------------------------------------------
// Timer queue initialisation (lazy, once)
// ---------------------------------------------------------------------------

static void EnsureTimerQueue() {
    std::call_once(g_queueOnce, []() {
        g_timerQueue = CreateTimerQueue();
        // Best-effort 1 ms timer resolution
        if (timeBeginPeriod(1) == TIMERR_NOERROR) {
            g_currentPeriod.store(1);
        }
    });
}

// ---------------------------------------------------------------------------
// Timer callback — runs on Windows thread-pool
// ---------------------------------------------------------------------------

static VOID CALLBACK TimerProc(PVOID lpParam, BOOLEAN /*TimerOrWaitFired*/) {
    auto* ctx = reinterpret_cast<TimerContext*>(lpParam);

    bool expected = false;
    if (!ctx->fired.compare_exchange_strong(expected, true)) {
        // Already fired or cancelled — skip
        return;
    }

    // Dispatch callback onto the Node.js event loop
    napi_status status = ctx->tsfn.NonBlockingCall();
    if (status != napi_ok) {
        // env is shutting down; nothing we can do
    }
}

// ---------------------------------------------------------------------------
// Cleanup helper — called after the TSFN fires (on JS thread) or on cancel
// ---------------------------------------------------------------------------

static void CleanupContext(uint64_t id) {
    std::lock_guard<std::mutex> lock(g_mapMtx);
    auto it = g_timers.find(id);
    if (it != g_timers.end()) {
        // DeleteTimerQueueTimer with INVALID_HANDLE_VALUE blocks until the
        // callback completes; use NULL to allow async cleanup instead.
        if (it->second->timerHandle) {
            DeleteTimerQueueTimer(g_timerQueue, it->second->timerHandle, nullptr);
        }
        delete it->second;
        g_timers.erase(it);
    }
}

// ---------------------------------------------------------------------------
// Compute delay in milliseconds from an absolute epoch value
// Returns 0 if the target is in the past (fire immediately via next-tick).
// ---------------------------------------------------------------------------

static DWORD ComputeDelayMs(const std::string& unit, double value) {
    // Obtain current time in milliseconds (high precision)
    FILETIME ft;
    GetSystemTimePreciseAsFileTime(&ft);
    // FILETIME is 100-ns intervals since 1601-01-01
    ULARGE_INTEGER ui;
    ui.LowPart  = ft.dwLowDateTime;
    ui.HighPart = ft.dwHighDateTime;
    // Convert to Unix epoch ms: subtract 116444736000000000 (100-ns ticks from
    // 1601-01-01 to 1970-01-01), then divide by 10000 to get ms.
    constexpr uint64_t EPOCH_DIFF_100NS = 116444736000000000ULL;
    double nowMs = static_cast<double>((ui.QuadPart - EPOCH_DIFF_100NS) / 10000ULL);

    double targetMs;
    if      (unit == "s" )  targetMs = value * 1000.0;
    else if (unit == "ms")  targetMs = value;
    else if (unit == "us")  targetMs = value / 1000.0;
    else if (unit == "ns")  targetMs = value / 1000000.0;
    else                    targetMs = value;   // fallback: treat as ms

    double diffMs = targetMs - nowMs;
    if (diffMs <= 0.0) return 0;
    if (diffMs > static_cast<double>(MAXDWORD)) return MAXDWORD;
    return static_cast<DWORD>(diffMs + 0.5);   // round to nearest ms
}

// ---------------------------------------------------------------------------
// setEpochTimer(unit, value, callback) → BigInt handle
// ---------------------------------------------------------------------------

static Napi::Value SetEpochTimer(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 3) {
        Napi::TypeError::New(env, "setEpochTimer requires 3 arguments: unit, value, callback")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }

    if (!info[0].IsString()) {
        Napi::TypeError::New(env, "Argument 0 (unit) must be a string: \"s\", \"ms\", \"us\", or \"ns\"")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }
    if (!info[1].IsNumber()) {
        Napi::TypeError::New(env, "Argument 1 (value) must be a number")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }
    if (!info[2].IsFunction()) {
        Napi::TypeError::New(env, "Argument 2 (callback) must be a function")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }

    std::string unit  = info[0].As<Napi::String>().Utf8Value();
    double      value = info[1].As<Napi::Number>().DoubleValue();
    Napi::Function cb = info[2].As<Napi::Function>();

    EnsureTimerQueue();

    uint64_t id = g_nextId.fetch_add(1, std::memory_order_relaxed);

    // Create TSFN — resource name used for debugging only
    auto tsfn = Napi::ThreadSafeFunction::New(
        env,
        cb,
        "node-epoch-timer",
        0,   // maxQueueSize: unlimited
        1,   // initial thread count
        [id](Napi::Env) {
            // Finalizer — runs on JS thread after Release()
            // Context may already be erased; safe to be a no-op here.
            (void)id;
        }
    );

    auto* ctx = new TimerContext(id, std::move(tsfn));

    // Register callback that the TSFN will invoke on the JS thread
    ctx->tsfn.SetContext(ctx);

    // Wrap tsfn call so we execute the JS callback and then clean up
    // We use a typed lambda via Napi's non-blocking call pattern:
    // The "data" parameter is unused; we capture id via closure in Finalizer.
    // Re-set the JS callback by registering a custom call:
    {
        // Override the TSFN callback by recreating with a proper JS-thread callback
        // Teardown the one we made and redo with the proper form:
        ctx->tsfn.Release();   // release the one created above

        ctx->tsfn = Napi::ThreadSafeFunction::New(
            env,
            cb,
            "node-epoch-timer",
            0,
            1,
            ctx,   // context pointer
            [](Napi::Env, void* /*finalizeData*/, TimerContext* /*ctx*/) {
                // Finalizer — no-op; cleanup handled in JS callback
            },
            ctx   // finalizeData (same as context here)
        );
    }

    DWORD delayMs = ComputeDelayMs(unit, value);

    {
        std::lock_guard<std::mutex> lock(g_mapMtx);
        g_timers[id] = ctx;
    }

    if (delayMs == 0) {
        // Target is in the past — fire immediately via setImmediate equivalent
        // Mark fired so the WinTimer callback (if it somehow fires) is ignored
        ctx->fired.store(true);
        napi_status st = ctx->tsfn.NonBlockingCall(
            nullptr,
            [](Napi::Env /*env*/, Napi::Function jsCallback, TimerContext* context, void* /*data*/) {
                jsCallback.Call({});
                // Cleanup after call
                uint64_t tid = context->id;
                CleanupContext(tid);
            }
        );
        if (st != napi_ok) {
            CleanupContext(id);
        }
    } else {
        // Schedule via Windows Timer Queue
        HANDLE timerHandle = nullptr;
        BOOL ok = CreateTimerQueueTimer(
            &timerHandle,
            g_timerQueue,
            TimerProc,
            ctx,
            delayMs,
            0,           // period 0 = one-shot
            WT_EXECUTEDEFAULT
        );

        if (!ok) {
            CleanupContext(id);
            Napi::Error::New(env, "CreateTimerQueueTimer failed")
                .ThrowAsJavaScriptException();
            return env.Undefined();
        }

        ctx->timerHandle = timerHandle;

        // Register the actual JS-thread callback (replaces default empty call)
        // We need to store the callback separately since TSFN was created with cb.
        // The NonBlockingCall in TimerProc will use the default JS callback (cb).
        // Nothing more to do — TimerProc calls tsfn.NonBlockingCall() and
        // the TSFN invokes cb() automatically.
    }

    // Return handle as BigInt for clearEpochTimer
    return Napi::BigInt::New(env, id);
}

// ---------------------------------------------------------------------------
// clearEpochTimer(handle)
// ---------------------------------------------------------------------------

static Napi::Value ClearEpochTimer(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1) {
        Napi::TypeError::New(env, "clearEpochTimer requires 1 argument: timerHandle")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }

    if (!info[0].IsBigInt()) {
        Napi::TypeError::New(env, "Argument 0 must be a BigInt timer handle")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }

    bool lossless = true;
    uint64_t id = info[0].As<Napi::BigInt>().Uint64Value(&lossless);

    {
        std::lock_guard<std::mutex> lock(g_mapMtx);
        auto it = g_timers.find(id);
        if (it != g_timers.end()) {
            it->second->cancelled.store(true);
            it->second->fired.store(true);   // prevent TimerProc dispatch
        }
    }
    CleanupContext(id);

    return env.Undefined();
}

// ---------------------------------------------------------------------------
// getTime(unit) → number — current wall-clock in requested unit
// ---------------------------------------------------------------------------

static Napi::Value GetTime(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    std::string unit = "ms";
    if (info.Length() >= 1 && info[0].IsString()) {
        unit = info[0].As<Napi::String>().Utf8Value();
    }

    FILETIME ft;
    GetSystemTimePreciseAsFileTime(&ft);
    ULARGE_INTEGER ui;
    ui.LowPart  = ft.dwLowDateTime;
    ui.HighPart = ft.dwHighDateTime;

    constexpr uint64_t EPOCH_DIFF_100NS = 116444736000000000ULL;
    uint64_t ticks100ns = ui.QuadPart - EPOCH_DIFF_100NS;   // 100-ns since Unix epoch

    double result;
    if      (unit == "s" )  result = static_cast<double>(ticks100ns) / 1e7;
    else if (unit == "ms")  result = static_cast<double>(ticks100ns) / 1e4;
    else if (unit == "us")  result = static_cast<double>(ticks100ns) / 10.0;
    else if (unit == "ns")  result = static_cast<double>(ticks100ns) * 100.0;
    else                    result = static_cast<double>(ticks100ns) / 1e4;

    return Napi::Number::New(env, result);
}

// ---------------------------------------------------------------------------
// setResolution(ms) — adjust system timer resolution
// ---------------------------------------------------------------------------

static Napi::Value SetResolution(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    UINT ms = 1;
    if (info.Length() >= 1 && info[0].IsNumber()) {
        ms = static_cast<UINT>(info[0].As<Napi::Number>().Uint32Value());
        if (ms < 1) ms = 1;
        if (ms > 16) ms = 16;
    }

    UINT prev = g_currentPeriod.load();
    if (prev != 0) timeEndPeriod(prev);
    if (timeBeginPeriod(ms) == TIMERR_NOERROR) {
        g_currentPeriod.store(ms);
    }

    return env.Undefined();
}

// ---------------------------------------------------------------------------
// Module init
// ---------------------------------------------------------------------------

static Napi::Object Init(Napi::Env env, Napi::Object exports) {
    exports.Set("setEpochTimer",   Napi::Function::New(env, SetEpochTimer));
    exports.Set("clearEpochTimer", Napi::Function::New(env, ClearEpochTimer));
    exports.Set("getTime",         Napi::Function::New(env, GetTime));
    exports.Set("setResolution",   Napi::Function::New(env, SetResolution));
    return exports;
}

NODE_API_MODULE(epoch, Init)
