#pragma once
#include "furi/core/event_loop.h"

namespace furi {

class EventLoop {
public:
    FuriEventLoop* ptr;
    EventLoop() {
        ptr = furi_event_loop_alloc();
    }
    EventLoop(EventLoop&& other) {
        ptr = other.ptr;
    }
    ~EventLoop() {
        furi_event_loop_free(ptr);
    }

    void stop() {
        furi_event_loop_stop(ptr);
    }
};
}
