#pragma once
#include <memory>
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
        this->destroy();
    }
    void destroy() {
        if(ptr_owned && ptr != nullptr) {
            furi_event_loop_free(ptr);
        }
        ptr = nullptr;
        ptr_owned = false;
    }
    EventLoop get_view() {
        return EventLoop(ptr);
    }
    void run() {
        if(ptr != nullptr) {
            furi_event_loop_run(ptr);
        }
    }
    void stop() {
        if(ptr != nullptr) {
            furi_event_loop_stop(ptr);
        }
    }

    EventLoop(EventLoop&& other) {
        *this = std::move(other);
    }
    EventLoop& operator=(EventLoop&& other) {
        this->destroy();
        this->ptr = other.ptr;
        this->ptr_owned = other.ptr_owned;
        other.ptr = nullptr;
        other.ptr_owned = false;
        return *this;
    }
};
}
