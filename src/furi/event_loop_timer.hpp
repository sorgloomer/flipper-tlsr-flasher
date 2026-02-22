#pragma once
#include "src/furi/event_loop.hpp"
#include "src/furi/duration.hpp"
#include "furi/core/event_loop_timer.h"

namespace furi {

class EventLoopTimer {
private:
    template <typename T>
    static void closure_invoke(T* fn) {
        (*fn)();
    }

public:
    FuriEventLoopTimer* ptr;

    EventLoopTimer(
        EventLoop& event_loop,
        FuriEventLoopTimerCallback callback,
        FuriEventLoopTimerType type,
        void* context) {
        ptr = furi_event_loop_timer_alloc(event_loop.ptr, callback, type, context);
    }

    template <typename T>
    EventLoopTimer(EventLoop& event_loop, FuriEventLoopTimerType type, T* closure)
        : EventLoopTimer(event_loop, EventLoopTimer::closure_invoke<T>, type, closure) {
    }

    EventLoopTimer(EventLoopTimer&& other) {
        ptr = other.ptr;
    }

    ~EventLoopTimer() {
        furi_event_loop_timer_free(ptr);
    }

    void start(milli interval) {
        furi_event_loop_timer_start(ptr, interval.count());
    }
};
}
