#pragma once
#include <furi.h>
#include "src/furi/delay.hpp"

class TimerPool {
    struct TimerHandle* head;
    FuriEventLoop* event_loop;

public:
    TimerPool(FuriEventLoop* event_loop);
    ~TimerPool();

    void submit(
        furi::u32ms timeout,
        FuriEventLoopTimerType type,
        FuriEventLoopTimerCallback callback,
        void* context);

    FuriEventLoop* get_raw_event_loop() {
        return event_loop;
    }
};

struct TimerHandle {
    struct TimerHandle* prev;
    struct TimerHandle* next;
    TimerPool* pool;
    FuriEventLoopTimerType type;
    FuriEventLoopTimer* timer;
    FuriEventLoopTimerCallback callback;
    void* context;

public:
    FuriEventLoopTimer* get_raw_timer();
    void cancel();
};
