#include <furi.h>
#include "./timerpool.hpp"
#include "src/furi/duration.hpp"
#include "src/buildconf.hpp"

__timerpool::handle_t __timerpool::submit(
    TimerPool* pool,
    furi::u32ms interval,
    FuriEventLoopTimerType type,
    __timerpool::callback_t&& callback,
    const char* debug) {
    auto handle = std::make_shared<TimerHandle>(pool, type, std::move(callback), debug);
    SW_DEBUG_TRACE("__timerpool::submit %s %lx", debug, reinterpret_cast<uint32_t>(handle.get()));
    auto event_loop = __timerpool::access_event_loop(pool);
    auto timer =
        furi_event_loop_timer_alloc(event_loop, __timerpool::invoke_callback, type, handle.get());
    handle->timer = timer;
    auto& timers = __timerpool::access_timers(pool);

#if SW_TIMERPOOL_USE_MAP
    timers.insert_or_assign(handle.get(), handle);
#else
    timers.insert(timers.end(), handle);
#endif

    furi_event_loop_timer_start(timer, interval.count());
    return handle;
}

void TimerPool::remove_timer(TimerHandle* timer) {
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

void __timerpool::invoke(TimerHandle* handle) {
    SW_DEBUG_TRACE(
        "__timerpool::invoke 1 - %d %lx %s",
        static_cast<int>(handle->callback != nullptr),
        reinterpret_cast<uint32_t>(handle),
        handle->debug);

    handle->invoke();
    if(handle->type == FuriEventLoopTimerTypeOnce) {
        __timerpool::cancel(handle);
    }
}

void __timerpool::invoke_callback(void* context) {
    __timerpool::invoke(static_cast<TimerHandle*>(context));
}

void __timerpool::cancel(TimerHandle* handle) {
    SW_DEBUG_TRACE(
        "__timerpool::cancel enter %s %lx", handle->debug, reinterpret_cast<uint32_t>(handle));
    handle->internal_destroy();
    SW_DEBUG_TRACE("__timerpool::cancel mid 1 %lx", reinterpret_cast<uint32_t>(handle));
    handle->pool->remove_timer(handle);
    SW_DEBUG_TRACE("__timerpool::cancel exit %lx", reinterpret_cast<uint32_t>(handle));
}

void TimerHandle::internal_destroy() {
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

FuriEventLoop*& __timerpool::access_event_loop(TimerPool* pool) {
    return pool->event_loop;
}
__timerpool::timer_collection_t& __timerpool::access_timers(TimerPool* pool) {
    return pool->timers;
}

TimerHandle::~TimerHandle() {
    SW_DEBUG_TRACE(
        "TimerHandle::~TimerHandle() enter %s %lx", this->debug, reinterpret_cast<uint32_t>(this));
    this->internal_destroy();
    SW_DEBUG_TRACE(
        "TimerHandle::~TimerHandle() exit %s %lx", this->debug, reinterpret_cast<uint32_t>(this));
}
