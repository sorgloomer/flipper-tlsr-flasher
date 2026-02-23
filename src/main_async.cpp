#include <coroutine>
#include <furi.h>
#include "src/utils/timerpool.hpp"
#include "src/buildconf.hpp"
#include "./main_async.hpp"

using namespace std::chrono_literals;

auto delay(Implicits* implicits, furi::u32ms delay) {
    struct awaitable {
        TimerPool* timerpool;
        furi::u32ms delay;
        std::coroutine_handle<> handle = std::coroutine_handle<>(nullptr);
        bool await_ready() {
            return false;
        }
        void await_suspend(std::coroutine_handle<> h) {
            this->handle = h;
            timerpool->submit(
                delay,
                FuriEventLoopTimerTypeOnce,
                [this]() { this->handle.resume(); },
                "delay await_suspend");
        }
        void await_resume() {
        }
        ~awaitable() {
            SW_DEBUG_TRACE("main_async awaitable ~awaitable");
        }
    };
    SW_DEBUG_TRACE("main_async awaitable create");
    return awaitable{.timerpool = implicits->timers, .delay = delay};
}

coroutine main_async(Implicits* implicits) {
    co_await delay(implicits, 1000ms);

#if 0
    {
        FURI_LOG_I(TAG, "Beta submitting");
        auto t1 = implicits->app->timers->submit(
            1000ms, FuriEventLoopTimerTypeOnce, []() { FURI_LOG_I(TAG, "Beta fired"); }, "beta");
        FURI_LOG_I(TAG, "Beta submitted");
        co_await delay(implicits, 500ms);
        FURI_LOG_I(TAG, "Beta delay");
        t1->cancel();
        FURI_LOG_I(TAG, "Beta canceled");
    }
    FURI_LOG_I(TAG, "Beta destroyed");
#endif

    for(int i = 0;; i++) {
        FURI_LOG_I(TAG, "from async counter %d", i);
        co_await delay(implicits, 1000ms);
    }
}
