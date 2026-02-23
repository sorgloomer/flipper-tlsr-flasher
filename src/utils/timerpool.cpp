#include <furi.h>
#include "./timerpool.hpp"
#include "src/furi/duration.hpp"
#include "src/buildconf.hpp"

__timerpool::handle_t __timerpool::submit(
    TimerPool* pool,
    furi::u32ms interval,
    FuriEventLoopTimerType type,
    __timerpool::callback_t&& callback) {
    auto handle = std::make_shared<TimerHandle>(pool, type, nullptr, std::move(callback));

    auto event_loop = __timerpool::access_event_loop(pool);
    auto timer =
        furi_event_loop_timer_alloc(event_loop, __timerpool::invoke_callback, type, handle.get());
    handle->timer = timer;
    auto& timers = __timerpool::access_timers(pool);
    timers.insert(timers.end(), handle);
    furi_event_loop_timer_start(timer, interval.count());
    return handle;
}

void TimerPool::remove_timer(TimerHandle* timer) {
    handle_t ptimer(timer);
    auto& timers = this->timers;
    auto begin = timers.begin(), end = timers.end();
    auto result = std::find(begin, end, ptimer);
    if(result != end) {
        timers.erase(result);
    }
}

void __timerpool::invoke(TimerHandle* handle) {
    SW_DEBUG_TRACE(
        "__timerpool::invoke 1 - %d %lx", (int)(handle->callback != nullptr), (uint32_t)handle);
    auto pool = handle->pool;
    pool->currently_running = handle;

    handle->invoke();
    if(handle->type == FuriEventLoopTimerTypeOnce) {
        __timerpool::cancel(handle);
    }
    pool->currently_running = nullptr;
}

void __timerpool::invoke_callback(void* context) {
    __timerpool::invoke(static_cast<TimerHandle*>(context));
}

void __timerpool::cancel(TimerHandle* handle) {
    auto pool = handle->pool;
    if(handle == pool->currently_running) {
        // TODO: EventLoopTimers have a bug, removing it inside its own callback
        //   causes the next scheduled timer to skip
    }
    handle->internal_destroy();
    handle->pool->remove_timer(handle);
}

void TimerHandle::internal_destroy() {
    SW_DEBUG_TRACE("TimerHandle::internal_destroy 1");
    if(this->timer != nullptr) {
        SW_DEBUG_TRACE("TimerHandle::internal_destroy 2");
        furi_event_loop_timer_free(this->timer);
    }
    this->timer = nullptr;
    this->callback = nullptr;
}

FuriEventLoop*& __timerpool::access_event_loop(TimerPool* pool) {
    return pool->event_loop;
}
std::vector<__timerpool::handle_t>& __timerpool::access_timers(TimerPool* pool) {
    return pool->timers;
}

TimerHandle::~TimerHandle() {
    this->internal_destroy();
}
