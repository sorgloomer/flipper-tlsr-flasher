#pragma once
#include <cstring>
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
        std::memmove(this, &other, sizeof(Record));
    }
    Record& operator=(Record&& other) {
        this->~Record();
        std::memmove(this, &other, sizeof(Record));
        return *this;
    }
    ~Record() {
        if(this->name != nullptr) {
            furi_record_close(this->name);
        }
    }
    TContent* get() {
        return this->ptr;
    }
};

}
