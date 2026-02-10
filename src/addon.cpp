#include <napi.h>
#include <windows.h>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <iostream>
#include <sstream>

struct TimerState {
    Napi::ThreadSafeFunction tsfn;
    HANDLE timer_handle = nullptr;
};

static std::unordered_map<HANDLE, std::unique_ptr<TimerState>> activeTimers;
static std::mutex timersMutex;

#define LOG_INFO(msg) do { \
    std::ostringstream oss; \
    oss << "[epoch-timer " << current_epoch_ms() << "ms] " << msg << std::endl; \
    std::cerr << oss.str() << std::flush; \
} while(0)

#define LOG_ERROR(msg) do { \
    std::ostringstream oss; \
    oss << "[epoch-timer ERROR " << current_epoch_ms() << "ms] " << msg << std::endl; \
    std::cerr << oss.str() << std::flush; \
} while(0)

static VOID CALLBACK TimerCallback(PVOID lpParameter, BOOLEAN /*TimerOrWaitFired*/) {
    auto* state = static_cast<TimerState*>(lpParameter);
    if (!state) {
        LOG_ERROR("TimerCallback called with null state pointer");
        return;
    }

    HANDLE timer_handle = state->timer_handle;  

    napi_status status = state->tsfn.BlockingCall([](Napi::Env env, Napi::Function jsCb) {
        jsCb.Call({});
    });

    if (status != napi_ok) {
        LOG_ERROR("ThreadSafeFunction.BlockingCall failed with status: " << status);
    }

    // Cleanup phase
    {
        std::lock_guard<std::mutex> lock(timersMutex);
        auto it = activeTimers.find(timer_handle);
        if (it != activeTimers.end()) {
            activeTimers.erase(it);
        } else {
            LOG_ERROR("Timer not found in activeTimers map during cleanup: " << timer_handle);
        }
    }

    // Delete the timer (wait-only mode is safe here)
    if (!DeleteTimerQueueTimer(nullptr, timer_handle, nullptr)) {
        DWORD err = GetLastError();
        if (err != ERROR_IO_PENDING && err != ERROR_SUCCESS) {
            LOG_ERROR("DeleteTimerQueueTimer failed with error: " << err);
        }
    }

    // Final resource cleanup
    state->tsfn.Release();
    delete state;

    LOG_INFO("Timer completed and cleaned up: " << timer_handle);
}

static int64_t normalize_to_ms(const std::string& unit, double value) {
    if (unit == "s")  return static_cast<int64_t>(value * 1000.0 + 0.5);
    if (unit == "ms") return static_cast<int64_t>(value + 0.5);
    if (unit == "us") return static_cast<int64_t>(value / 1000.0 + 0.5);
    if (unit == "ns") return static_cast<int64_t>(value / 1'000'000.0 + 0.5);
    return -1;
}

static int64_t current_epoch_ms() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER uli{ ft.dwLowDateTime, ft.dwHighDateTime };
    return static_cast<int64_t>((uli.QuadPart - 116444736000000000ULL) / 10000ULL);
}

Napi::Value SetEpochTimer(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() != 3 ||
        !info[0].IsString() ||
        !info[1].IsNumber() ||
        !info[2].IsFunction()) {
        Napi::TypeError::New(env, "Usage: setEpochTimer(unit: string, value: number, callback: function)")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }

    std::string unit = info[0].As<Napi::String>().Utf8Value();
    double value = info[1].As<Napi::Number>().DoubleValue();
    Napi::Function callback = info[2].As<Napi::Function>();

    int64_t target_ms = normalize_to_ms(unit, value);
    if (target_ms <= 0) {
        Napi::TypeError::New(env, "Invalid unit or value <= 0 (unit: " + unit + ")")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }

    int64_t now_ms = current_epoch_ms();
    int64_t delay_ms = target_ms - now_ms;

    if (delay_ms <= 0) {
        LOG_INFO("Target time already passed, executing callback immediately");
        callback.Call({});
        return env.Undefined();
    }

    LOG_INFO("Scheduling timer to epoch " << target_ms << " ms (in " << delay_ms << " ms)");

    auto state = std::make_unique<TimerState>();

    state->tsfn = Napi::ThreadSafeFunction::New(
        env,
        callback,
        "EpochTimer",
        0,      // unlimited queue
        1,      
        [](Napi::Env) { 
            LOG_INFO("ThreadSafeFunction finalizer invoked"); 
        }
    );

    HANDLE timer_handle = nullptr;
    BOOL success = CreateTimerQueueTimer(
        &timer_handle,
        nullptr,
        TimerCallback,
        state.get(),
        static_cast<DWORD>(delay_ms),
        0,      
        WT_EXECUTEONLYONCE | WT_EXECUTEINTIMERTHREAD | WT_EXECUTELONGFUNCTION
    );

    if (!success) {
        DWORD err = GetLastError();
        state->tsfn.Release();
        std::string msg = "CreateTimerQueueTimer failed with error code: " + std::to_string(err);
        LOG_ERROR(msg);
        Napi::Error::New(env, msg).ThrowAsJavaScriptException();
        return env.Undefined();
    }

    state->timer_handle = timer_handle;

    {
        std::lock_guard<std::mutex> lock(timersMutex);
        activeTimers[timer_handle] = std::move(state);
    }

    LOG_INFO("Timer created successfully: handle=" << timer_handle << " delay=" << delay_ms << "ms");

    return env.Undefined();
}

Napi::Object Init(Napi::Env env, Napi::Object exports) {
    exports.Set("setEpochTimer", Napi::Function::New(env, SetEpochTimer));
    return exports;
}

NODE_API_MODULE(epoch_timer, Init)
