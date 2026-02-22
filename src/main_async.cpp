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
                [](void* h) { static_cast<std::coroutine_handle<>*>(h)->resume(); },
                (void*)&this->handle);
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

task main_async(Implicits* implicits) {
    for(int i = 0;; i++) {
        co_await delay(implicits, 5ms);
        FURI_LOG_I(TAG, "from async counter %d", i);
    }
}
