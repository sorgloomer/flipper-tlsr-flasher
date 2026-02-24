#include <furi.h>
#include "./timerpool.hpp"
#include "src/furi/duration.hpp"
#include "src/buildconf.hpp"

void TimerPool::remove_timer(TimerPoolTimerHandle* timer) {
    SW_DEBUG_TRACE("TimerPool::remove_timer 1");
    auto& timers = this->timers;
    SW_DEBUG_TRACE("TimerPool::remove_timer 2");

#if SW_TIMERPOOL_USE_MAP
    auto result = timers.find(timer);
#else
    auto result = std::find_if(timers.begin(), timers.end(), [timer](handle_t sp) {
        return sp.get() == timer;
    });
#endif

    SW_DEBUG_TRACE("TimerPool::remove_timer 3");
    if(result != timers.end()) {
        SW_DEBUG_TRACE("TimerPool::remove_timer 4");
        timers.erase(result);
        SW_DEBUG_TRACE("TimerPool::remove_timer 5");
    }
    SW_DEBUG_TRACE("TimerPool::remove_timer 6");
}

void TimerPoolTimerHandle::invoke_and_release(TimerPoolTimerHandle* handle) {
    SW_DEBUG_TRACE(
        "__timerpool::invoke 1 - %d %lx %s",
        static_cast<int>(handle->callback != nullptr),
        reinterpret_cast<uint32_t>(handle),
        handle->debug);

    handle->invoke();
    if(handle->type == FuriEventLoopTimerTypeOnce) {
        handle->cancel();
    }
}

void TimerPoolTimerHandle::invoke_callback(void* context) {
    TimerPoolTimerHandle::invoke_and_release(static_cast<TimerPoolTimerHandle*>(context));
}

void TimerPoolTimerHandle::cancel() {
    SW_DEBUG_TRACE(
        "TimerHandle::cancel enter %s %lx", handle->debug, reinterpret_cast<uint32_t>(handle));
    this->nullify();
    SW_DEBUG_TRACE("TimerHandle::cancel mid 1 %lx", reinterpret_cast<uint32_t>(handle));
    this->pool->remove_timer(this);
    SW_DEBUG_TRACE("TimerHandle::cancel exit %lx", reinterpret_cast<uint32_t>(handle));
}

void TimerPoolTimerHandle::nullify() {
    SW_DEBUG_TRACE(
        "TimerHandle::internal_destroy 1 %s %lx", this->debug, reinterpret_cast<uint32_t>(this));
    if(this->timer != nullptr) {
        SW_DEBUG_TRACE(
            "TimerHandle::internal_destroy 2 %s %lx",
            this->debug,
            reinterpret_cast<uint32_t>(this));
        furi_event_loop_timer_free(this->timer);
    }
    this->timer = nullptr;
    this->callback = nullptr;
}

TimerPoolTimerHandle::~TimerPoolTimerHandle() {
    SW_DEBUG_TRACE(
        "TimerHandle::~TimerHandle() enter %s %lx", this->debug, reinterpret_cast<uint32_t>(this));
    this->nullify();
    SW_DEBUG_TRACE(
        "TimerHandle::~TimerHandle() exit %s %lx", this->debug, reinterpret_cast<uint32_t>(this));
}
