#include <napi.h>
#include <windows.h>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <iostream>
#include <sstream>

#define LOG_INFO(msg) do { \
    std::ostringstream oss; \
    oss << "[epoch-timer " << current_epoch_ms() << "ms] " << msg << std::endl; \
    std::cerr << oss.str(); \
} while(0)


struct TimerState {
    Napi::ThreadSafeFunction tsfn;
    HANDLE timer_handle = nullptr;
};

static std::unordered_map<HANDLE, std::unique_ptr<TimerState>> activeTimers;
static std::mutex timersMutex;



static VOID CALLBACK TimerCallback(PVOID lpParameter, BOOLEAN /*TimerOrWaitFired*/) {
    auto* state = static_cast<TimerState*>(lpParameter);
    if (!state) return;


    napi_status status = state->tsfn.BlockingCall([](Napi::Env env, Napi::Function callback) {
        callback.Call({});
    });

    if (status != napi_ok) {

    }

    // Cleanup
    {
        std::lock_guard<std::mutex> lock(timersMutex);
        activeTimers.erase(state->timer_handle);
    }

    DeleteTimerQueueTimer(nullptr, state->timer_handle, nullptr);
    state->tsfn.Release();
    delete state;
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
        Napi::TypeError::New(env, "Invalid unit or value <= 0").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    int64_t now_ms = current_epoch_ms();
    int64_t delay_ms = target_ms - now_ms;

    if (delay_ms <= 0) {
        callback.Call({});
        return env.Undefined();
    }

    auto state = std::make_unique<TimerState>();

    state->tsfn = Napi::ThreadSafeFunction::New(
        env,
        callback,
        "EpochTimer",
        0,   
        1     
    );

    BOOL ok = CreateTimerQueueTimer(
        &state->timer_handle,
        nullptr,                     
        TimerCallback,
        state.get(),                   // pass ownership via raw pointer
        static_cast<DWORD>(delay_ms),
        0,                             // one shot
        WT_EXECUTEONLYONCE | WT_EXECUTEINTIMERTHREAD
    );

    if (!ok) {
        state->tsfn.Release();
        Napi::Error::New(env, "CreateTimerQueueTimer failed").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    {
        std::lock_guard<std::mutex> lock(timersMutex);
        activeTimers[state->timer_handle] = std::move(state);
    }

    return env.Undefined();
}

Napi::Object Init(Napi::Env env, Napi::Object exports) {
    exports.Set("setEpochTimer", Napi::Function::New(env, SetEpochTimer));
    return exports;
}

NODE_API_MODULE(epoch_timer, Init)
