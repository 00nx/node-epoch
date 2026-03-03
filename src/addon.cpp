#include <napi.h>
#include <windows.h>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <string>
#include <sstream>
#include <iostream>
#include <iomanip>

namespace {

////////////////////////////////////////////////////////////////////////////////
// Types & Globals
////////////////////////////////////////////////////////////////////////////////

struct TimerState {
    Napi::ThreadSafeFunction tsfn;
    HANDLE timer_handle = nullptr;

    explicit TimerState(Napi::ThreadSafeFunction&& func)
        : tsfn(std::move(func)) {}

    ~TimerState() {
        if (tsfn) {
            tsfn.Release();
        }
    }

    TimerState(const TimerState&) = delete;
    TimerState& operator=(const TimerState&) = delete;
};

// Protected map of active timers
static std::unordered_map<HANDLE, std::unique_ptr<TimerState>> activeTimers;
static std::mutex timersMutex;

////////////////////////////////////////////////////////////////////////////////
// Helpers
////////////////////////////////////////////////////////////////////////////////

int64_t current_epoch_ms() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER uli{ft.dwLowDateTime, ft.dwHighDateTime};
    // 100-ns ticks since 1601 → ms since 1970
    return static_cast<int64_t>((uli.QuadPart - 116444736000000000ULL) / 10000ULL);
}

int64_t normalize_interval(const std::string& unit, double value) {
    if (unit == "s")  return static_cast<int64_t>(value * 1000.0 + 0.5);
    if (unit == "ms") return static_cast<int64_t>(value + 0.5);
    if (unit == "us") return static_cast<int64_t>(value / 1000.0 + 0.5);
    if (unit == "ns") return static_cast<int64_t>(value / 1'000'000.0 + 0.5);
    return -1;
}

std::string format_log(const char* level, const std::string& msg, uintptr_t handle = 0) {
    std::ostringstream oss;
    oss << "[epoch-timer " << level << "] "
        << current_epoch_ms() << " ms  ";
    if (handle != 0) {
        oss << "T:" << std::hex << std::setw(16) << std::setfill('0') << handle << "  ";
    }
    oss << msg << "\n";
    return oss.str();
}

#define LOG_INFO(msg, ...)   do { std::cerr << format_log("INFO",  msg, ##__VA_ARGS__); } while(0)
#define LOG_ERROR(msg, ...)  do { std::cerr << format_log("ERROR", msg, ##__VA_ARGS__); } while(0)

////////////////////////////////////////////////////////////////////////////////
// Timer Callback (runs on thread-pool thread)
////////////////////////////////////////////////////////////////////////////////
VOID CALLBACK TimerCallback(PVOID param, BOOLEAN /*TimerOrWaitFired*/) {
    auto* state = static_cast<TimerState*>(param);
    if (!state || !state->timer_handle) {
        LOG_ERROR("TimerCallback received invalid state");
        return;
    }

    const HANDLE timer_h = state->timer_handle;
    uintptr_t h = reinterpret_cast<uintptr_t>(timer_h);

    napi_status status = state->tsfn.NonBlockingCall([](Napi::Env env, Napi::Function js_cb) {
        js_cb.Call({});
    });

    if (status == napi_closing) {
        LOG_INFO("TSFN already closing during callback", h);
    } else if (status != napi_ok) {
        LOG_ERROR("TSFN NonBlockingCall failed: " + std::to_string(status), h);
    }

    // Cleanup map entry
    {
        std::lock_guard<std::mutex> lock(timersMutex);
        activeTimers.erase(timer_h);
    }

    // Explicitly release so finalizer can run
    state->tsfn.Release();
    state->tsfn = nullptr;           // ← prevents double-release in ~TimerState()

    if (!DeleteTimerQueueTimer(nullptr, timer_h, nullptr)) {
        DWORD err = GetLastError();
        if (err != ERROR_IO_PENDING && err != ERROR_SUCCESS) {
            LOG_ERROR("DeleteTimerQueueTimer failed: " + std::to_string(err), h);
        }
    }

    LOG_INFO("Timer completed & cleaned up", h);
}

////////////////////////////////////////////////////////////////////////////////
// Exported function: setEpochTimer(unit: string, value: number, callback: () => void)
////////////////////////////////////////////////////////////////////////////////

Napi::Value SetEpochTimer(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() != 3 ||
        !info[0].IsString() ||
        !info[1].IsNumber() ||
        !info[2].IsFunction()) {
        Napi::TypeError::New(env,
            "Usage: setEpochTimer(unit: 's'|'ms'|'us'|'ns', value: number, callback: () => void)")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }

    std::string unit  = info[0].As<Napi::String>().Utf8Value();
    double      value = info[1].As<Napi::Number>().DoubleValue();
    Napi::Function cb = info[2].As<Napi::Function>();

    int64_t target_ms = normalize_interval(unit, value);
    if (target_ms <= 0) {
        Napi::TypeError::New(env, "Invalid unit or value ≤ 0").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    int64_t now_ms   = current_epoch_ms();
    int64_t delay_ms = target_ms - now_ms;

    if (delay_ms <= 0) {
        LOG_INFO("Immediate execution (target already passed): " + std::to_string(target_ms));
        cb.Call({});
        return env.Undefined();
    }

    // Windows CreateTimerQueueTimer uses DWORD → max ~49.7 days
    if (delay_ms > 0xFFFFFFFFLL) {
        Napi::RangeError::New(env, "Delay too large (maximum supported ~49.7 days)")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }

    LOG_INFO("Scheduling to epoch " + std::to_string(target_ms) +
             " ms (in " + std::to_string(delay_ms) + " ms)");

    // Create ThreadSafeFunction
    Napi::ThreadSafeFunction tsfn = Napi::ThreadSafeFunction::New(
        env,
        cb,
        "EpochTimer",
        0,      // unlimited queue
        1,      // ref count
        [](Napi::Env) { LOG_INFO("ThreadSafeFunction finalizer called"); }
    );

    auto state = std::make_unique<TimerState>(std::move(tsfn));

    HANDLE timer_handle = nullptr;
    BOOL success = CreateTimerQueueTimer(
        &timer_handle,
        nullptr,                        // default queue
        TimerCallback,
        state.get(),
        static_cast<DWORD>(delay_ms),
        0,                              // one-shot
        WT_EXECUTEONLYONCE |
        WT_EXECUTEINTIMERTHREAD |
        WT_EXECUTELONGFUNCTION
    );

    if (!success) {
        DWORD err = GetLastError();
        std::string msg = "CreateTimerQueueTimer failed: " + std::to_string(err);
        LOG_ERROR(msg);
        Napi::Error::New(env, msg).ThrowAsJavaScriptException();
        return env.Undefined();
    }

    state->timer_handle = timer_handle;

    {
        std::lock_guard<std::mutex> lock(timersMutex);
        activeTimers[timer_handle] = std::move(state);
    }

    LOG_INFO("Timer created  delay=" + std::to_string(delay_ms) + " ms",
             reinterpret_cast<uintptr_t>(timer_handle));

    return env.Undefined();
}

} // anonymous namespace

////////////////////////////////////////////////////////////////////////////////
// Module initialization
////////////////////////////////////////////////////////////////////////////////

Napi::Object Init(Napi::Env env, Napi::Object exports) {
    exports.Set("setEpochTimer", Napi::Function::New(env, SetEpochTimer, "setEpochTimer"));
    return exports;
}

NODE_API_MODULE(epoch_timer, Init)
