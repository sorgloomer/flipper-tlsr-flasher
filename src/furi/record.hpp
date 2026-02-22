#pragma once
#include <utility>
#include "furi/core/record.h"

namespace furi {

template <typename TContent = void>
class Record {
    TContent* ptr;
    const char* name;

public:
    Record(const char* name)
        : ptr(nullptr)
        , name(name) {
        if(name != nullptr) {
            this->ptr = (TContent*)furi_record_open(name);
        }
    }
    Record()
        : Record(nullptr) {
    }
    Record(Record&& other) {
        *this = std::move(other);
    }
    Record& operator=(Record&& other) {
        this->destroy();
        this->ptr = other.ptr;
        this->name = other.name;
        other.ptr = nullptr;
        other.name = nullptr;
        return *this;
    }
    ~Record() {
        this->destroy();
    }
    TContent* get() {
        return this->ptr;
    }

    void destroy() {
        if(this->name != nullptr) {
            furi_record_close(this->name);
        }
        this->name = nullptr;
        this->ptr = nullptr;
    }
};

}
