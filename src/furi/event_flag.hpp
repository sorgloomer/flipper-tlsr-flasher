#pragma once
#include "furi/core/event_flag.h"
#include "src/furi/duration.hpp"

namespace furi {

template <typename TFlags = uint32_t>
class EventFlag {
public:
    FuriEventFlag* ptr;
    EventFlag() {
        ptr = furi_event_flag_alloc();
    }
    EventFlag(EventFlag&& other) {
        *this = std::move(other);
    }
    EventFlag& operator=(EventFlag&& other) {
        this->destroy();
        this->ptr = other.ptr;
        other.ptr = nullptr;
        return *this;
    }
    ~EventFlag() {
        this->destroy();
    }
    void destroy() {
        if(ptr != nullptr) {
            furi_event_flag_free(ptr);
        }
        ptr = nullptr;
    }

    TFlags set(TFlags flags) {
        return furi_event_flag_set(ptr, flags);
    }
    TFlags clear(TFlags flags) {
        return furi_event_flag_clear(ptr, flags);
    }
    TFlags get() {
        return furi_event_flag_get(ptr);
    }
    TFlags wait(TFlags flags, FuriFlag options, u32ms timeout) {
        return furi_event_flag_wait(ptr, flags, options, timeout);
    }
};
}
