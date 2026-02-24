#pragma once

// unordered_map seems to use double precision internally, and it does not compile
#define SW_TIMERPOOL_USE_MAP 0
#include <functional>
#include <memory>

#if SW_TIMERPOOL_USE_MAP
#include <unordered_map>
#else
#include <vector>
#endif

#include <furi.h>
#include "src/furi/duration.hpp"
#include "src/buildconf.hpp"

class TimerPool;
class TimerPoolTimerHandle;

class TimerPoolTimerHandle {
    using callback_t = std::function<void()>;
    TimerPool* pool;
    FuriEventLoopTimerType type;
    FuriEventLoopTimer* timer;
    callback_t callback;
    const char* debug;

    static void invoke_callback(void* context);
    static void invoke_and_release(TimerPoolTimerHandle* context);
    void nullify();

public:
    /** for internal use only! */
    TimerPoolTimerHandle(
        TimerPool* pool,
        FuriEventLoopTimerType type,
        callback_t&& callback,
        const char* debug)
        : pool(pool)
        , type(type)
        , timer(nullptr)
        , callback(std::move(callback))
        , debug(debug) {
    }
    FuriEventLoopTimer* get_raw_timer() {
        return timer;
    }
    FuriEventLoopTimerType get_type() {
        return type;
    }
    TimerPool* get_pool() {
        return pool;
    }

    void cancel();

    void invoke() {
        if(callback == nullptr) {
            SW_DEBUG_TRACE("TimerHandle::invoke callback == nullptr %s", debug);
            return;
        }
        callback();
    }
    ~TimerPoolTimerHandle();
    friend TimerPool;
};

class TimerPool {
public:
    using handle_t = std::shared_ptr<TimerPoolTimerHandle>;
    using callback_t = std::function<void()>;
#if SW_TIMERPOOL_USE_MAP
    using timer_collection_t = std::unordered_map<TimerHandle*, handle_t>;
#else
    using timer_collection_t = std::vector<handle_t>;
#endif

private:
    FuriEventLoop* event_loop;
    timer_collection_t timers;

    void remove_timer(TimerPoolTimerHandle* timer);

public:
    TimerPool(FuriEventLoop* event_loop)
        : event_loop(event_loop) {};

    template <typename F>
    handle_t
        submit(furi::u32ms interval, FuriEventLoopTimerType type, F&& callback, const char* debug) {
        auto handle =
            std::make_shared<TimerPoolTimerHandle>(this, type, std::move(callback), debug);
        SW_DEBUG_TRACE(
            "__timerpool::submit %s %lx", debug, reinterpret_cast<uint32_t>(handle.get()));
        auto timer = furi_event_loop_timer_alloc(
            event_loop, TimerPoolTimerHandle::invoke_callback, type, handle.get());
        handle->timer = timer;

#if SW_TIMERPOOL_USE_MAP
        timers.insert_or_assign(handle.get(), handle);
#else
        timers.insert(timers.end(), handle);
#endif

        furi_event_loop_timer_start(timer, interval.count());
        return handle;
    }

    FuriEventLoop* get_raw_event_loop() {
        return event_loop;
    }

    friend TimerPoolTimerHandle;
};
