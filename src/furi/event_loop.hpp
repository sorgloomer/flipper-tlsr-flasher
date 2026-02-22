#pragma once
#include <cstring>
#include "furi/core/event_loop.h"

namespace furi {

class EventLoop {
    FuriEventLoop* ptr;
    bool ptr_owned;

public:
    FuriEventLoop* get_raw_ptr() {
        return ptr;
    }
    bool get_ptr_owned() {
        return ptr_owned;
    }

    EventLoop() {
        this->ptr = furi_event_loop_alloc();
        this->ptr_owned = true;
    }
    EventLoop(FuriEventLoop* ptr) {
        this->ptr = ptr;
        this->ptr_owned = false;
    }
    ~EventLoop() {
        if(ptr_owned && ptr != nullptr) {
            furi_event_loop_free(ptr);
        }
    }
    EventLoop get_view() {
        return EventLoop(ptr);
    }
    void run() {
        furi_event_loop_run(ptr);
    }
    void stop() {
        furi_event_loop_stop(ptr);
    }

    EventLoop(EventLoop&& other) {
        std::memmove(this, &other, sizeof(EventLoop));
    }
    EventLoop& operator=(EventLoop&& other) {
        this->~EventLoop();
        std::memmove(this, &other, sizeof(EventLoop));
        return *this;
    }
};
}
