#pragma once

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

class TimerHandle;
class TimerPool;

namespace __timerpool {

using callback_t = std::function<void()>;
using handle_t = std::shared_ptr<TimerHandle>;
#if SW_TIMERPOOL_USE_MAP
using timer_collection_t = std::unordered_map<TimerHandle*, handle_t>;
#else
using timer_collection_t = std::vector<handle_t>;
#endif

void invoke_callback(void* context);
void cancel(TimerHandle* handle);
void invoke(TimerHandle* handle);

handle_t submit(
    TimerPool* pool,
    furi::u32ms timeout,
    FuriEventLoopTimerType type,
    callback_t&& callback,
    const char* debug);

FuriEventLoop*& access_event_loop(TimerPool* pool);
timer_collection_t& access_timers(TimerPool* pool);

}

class TimerPool {
public:
    using handle_t = std::shared_ptr<TimerHandle>;
    using callback_t = std::function<void()>;

private:
    FuriEventLoop* event_loop;
    __timerpool::timer_collection_t timers;

    void remove_timer(TimerHandle* timer);

public:
    TimerPool(FuriEventLoop* event_loop)
        : event_loop(event_loop) {};

    template <typename F>
    handle_t
        submit(furi::u32ms timeout, FuriEventLoopTimerType type, F&& callback, const char* debug) {
        return __timerpool::submit(this, timeout, type, callback_t(std::move(callback)), debug);
    }

    FuriEventLoop* get_raw_event_loop() {
        return event_loop;
    }

    friend std::shared_ptr<TimerHandle> __timerpool::submit(
        TimerPool* pool,
        furi::u32ms timeout,
        FuriEventLoopTimerType type,
        callback_t&& callback,
        const char* debug);
    friend void __timerpool::invoke(TimerHandle* handle);
    friend void __timerpool::cancel(TimerHandle* handle);
    friend FuriEventLoop*& __timerpool::access_event_loop(TimerPool* pool);
    friend __timerpool::timer_collection_t& __timerpool::access_timers(TimerPool* pool);
};

class TimerHandle {
    using callback_t = std::function<void()>;
    TimerPool* pool;
    FuriEventLoopTimerType type;
    FuriEventLoopTimer* timer;
    callback_t callback;
    const char* debug;

public:
    /** for internal use only! */
    TimerHandle(
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

    void cancel() {
        __timerpool::cancel(this);
    }
    void internal_destroy();

    void invoke() {
        if(callback == nullptr) {
            SW_DEBUG_TRACE("TimerHandle::invoke callback == nullptr %s", debug);
            return;
        }
        callback();
    }
    friend void __timerpool::cancel(TimerHandle* handle);
    friend void __timerpool::invoke(TimerHandle* handle);
    friend std::shared_ptr<TimerHandle> __timerpool::submit(
        TimerPool* pool,
        furi::u32ms timeout,
        FuriEventLoopTimerType type,
        callback_t&& callback,
        const char* debug);

    ~TimerHandle();
};
