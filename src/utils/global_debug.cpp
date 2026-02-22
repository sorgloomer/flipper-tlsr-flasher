#include <memory>
#include <stdint.h>
#include "src/utils/str_printf.hpp"
#include "./global_debug.hpp"

std::unique_ptr<GlobalDebugInfo> global_debug_value(nullptr);

GlobalDebugInfo* global_debug() {
    return global_debug_value.get();
}

void global_debug_deinit() {
    global_debug_value = nullptr;
}

void global_debug_init() {
    global_debug_value = std::make_unique<GlobalDebugInfo>();
}

void global_debug_log(const char* format, ...) {
    // TODO this is not threadsafe but whatever
    GlobalDebugInfo* d = global_debug_value.get();
    std::string& tail = d->logs[d->log_capacity - 1];
    for(int32_t i = d->log_capacity - 1; i > 0; i--) {
        d->logs[i] = d->logs[i - 1];
    }
    d->logs[0] = tail;
    va_list args;
    va_start(args, format);
    str_vprintf(tail, format, args);
    va_end(args);
}
