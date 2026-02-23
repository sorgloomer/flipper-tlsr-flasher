#pragma once
#include <functional>
#include <memory>
#include <vector>
#include <furi.h>
#include "src/furi/duration.hpp"

class TimerHandle;
class TimerPool;

namespace __timerpool {

using callback_t = std::function<void()>;
using handle_t = std::shared_ptr<TimerHandle>;

void invoke_callback(void* context);
void cancel(TimerHandle* handle);
void invoke(TimerHandle* handle);

handle_t submit(
    TimerPool* pool,
    furi::u32ms timeout,
    FuriEventLoopTimerType type,
    callback_t&& callback);

FuriEventLoop*& access_event_loop(TimerPool* pool);
std::vector<handle_t>& access_timers(TimerPool* pool);

}

class TimerPool {
public:
    using handle_t = std::shared_ptr<TimerHandle>;
    using callback_t = std::function<void()>;

private:
    FuriEventLoop* event_loop;
    TimerHandle* currently_running = nullptr;
    std::vector<handle_t> timers;
    std::vector<handle_t> destruct_queue;

    void remove_timer(TimerHandle* timer);

public:
    TimerPool(FuriEventLoop* event_loop)
        : event_loop(event_loop) {};

    template <typename F>
    void submit(furi::u32ms timeout, FuriEventLoopTimerType type, F&& callback) {
        __timerpool::submit(this, timeout, type, callback_t(std::move(callback)));
    }

    FuriEventLoop* get_raw_event_loop() {
        return event_loop;
    }

    friend std::shared_ptr<TimerHandle> __timerpool::submit(
        TimerPool* pool,
        furi::u32ms timeout,
        FuriEventLoopTimerType type,
        callback_t&& callback);
    friend void __timerpool::invoke(TimerHandle* handle);
    friend void __timerpool::cancel(TimerHandle* handle);
    friend FuriEventLoop*& __timerpool::access_event_loop(TimerPool* pool);
    friend std::vector<handle_t>& __timerpool::access_timers(TimerPool* pool);
};

class TimerHandle {
    using callback_t = std::function<void()>;
    TimerPool* pool;
    FuriEventLoopTimerType type;
    FuriEventLoopTimer* timer;
    callback_t callback;
    bool currently_running = false;

public:
    /** for internal use only! */
    TimerHandle(
        TimerPool* pool,
        FuriEventLoopTimerType type,
        FuriEventLoopTimer* timer,
        callback_t&& callback)
        : pool(pool)
        , type(type)
        , timer(timer)
        , callback(std::move(callback)) {
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
        auto old_currently_running = currently_running;
        currently_running = true;
        callback();
        currently_running = old_currently_running;
    }
    friend void __timerpool::cancel(TimerHandle* handle);
    friend void __timerpool::invoke(TimerHandle* handle);
    friend std::shared_ptr<TimerHandle> __timerpool::submit(
        TimerPool* pool,
        furi::u32ms timeout,
        FuriEventLoopTimerType type,
        callback_t&& callback);

    ~TimerHandle();
};
