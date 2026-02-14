#include <stdint.h>
#include <src/global_debug.h>

GlobalDebugInfo* global_debug_value = NULL;

GlobalDebugInfo* global_debug() {
    return global_debug_value;
}

void global_debug_deinit() {
    if(global_debug_value == NULL) return;
    for(uint32_t i = 0; i < global_debug_value->log_capacity; i++) {
        furi_string_free(global_debug_value->logs[i]);
    }
    free(global_debug_value->logs);
    free(global_debug_value);
    global_debug_value = NULL;
}

void global_debug_init() {
    global_debug_deinit();
    global_debug_value = malloc(sizeof(GlobalDebugInfo));
    memset(global_debug_value, 0, sizeof(GlobalDebugInfo));
    global_debug_value->log_capacity = 10;
    global_debug_value->logs = malloc(sizeof(FuriString*) * global_debug_value->log_capacity);
    for(uint32_t i = 0; i < global_debug_value->log_capacity; i++) {
        global_debug_value->logs[i] = furi_string_alloc();
    }
}

void global_debug_log(const char* format, ...) {
    // TODO this is not threadsafe but whatever
    GlobalDebugInfo* d = global_debug_value;
    FuriString* tail = d->logs[d->log_capacity - 1];
    for(int32_t i = d->log_capacity - 1; i > 0; i--) {
        d->logs[i] = d->logs[i - 1];
    }
    d->logs[0] = tail;
    va_list args;
    va_start(args, format);
    furi_string_vprintf(tail, format, args);
    va_end(args);
}
