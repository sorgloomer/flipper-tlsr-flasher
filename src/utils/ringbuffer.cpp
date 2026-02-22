#include <furi.h>
#include <string.h>
#include "./ringbuffer.hpp"

#define _INLINE __attribute__((always_inline)) inline

_INLINE static uint32_t min_u32(uint32_t a, uint32_t b) {
    return (a < b) ? a : b;
}

void ringbuffer_init(struct RingBuffer* self, uint32_t capacity) {
    self->buffer.size = capacity;
    self->buffer.ptr = (uint8_t*)malloc(capacity);
    furi_check(self->buffer.ptr);
    self->head = 0;
    self->tail = 0;
    self->size = 0;
}

void ringbuffer_deinit(RingBuffer* self) {
    free(self->buffer.ptr);
}

RingBuffer* ringbuffer_alloc(uint32_t capacity) {
    RingBuffer* self = (RingBuffer*)malloc(sizeof(RingBuffer));
    furi_check(self);
    ringbuffer_init(self, capacity);
    return self;
}

void ringbuffer_free(RingBuffer* self) {
    if(self == NULL) return;
    ringbuffer_deinit(self);
    free(self);
}

/* ============================================================
   WRITE (append at tail)
   ============================================================ */
uint32_t ringbuffer_write(RingBuffer* self, uint8_t* buffer, uint32_t length) {
    if(self->size == self->buffer.size) return 0;

    uint32_t writable = min_u32(length, self->buffer.size - self->size);

    uint32_t first_part = min_u32(writable, self->buffer.size - self->tail);

    memcpy(self->buffer.ptr + self->tail, buffer, first_part);

    uint32_t second_part = writable - first_part;
    if(second_part > 0) {
        memcpy(self->buffer.ptr, buffer + first_part, second_part);
    }

    self->tail = (self->tail + writable) % self->buffer.size;
    self->size += writable;

    return writable;
}

/* ============================================================
   WRITE AT START (prepend before head)
   ============================================================ */
uint32_t ringbuffer_write_to_start(RingBuffer* self, uint8_t* buffer, uint32_t length) {
    if(self->size == self->buffer.size) return 0;

    uint32_t writable = min_u32(length, self->buffer.size - self->size);

    /* new head position */
    uint32_t new_head = (self->head + self->buffer.size - writable) % self->buffer.size;

    uint32_t first_part = min_u32(writable, self->buffer.size - new_head);

    memcpy(self->buffer.ptr + new_head, buffer, first_part);

    uint32_t second_part = writable - first_part;
    if(second_part > 0) {
        memcpy(self->buffer.ptr, buffer + first_part, second_part);
    }

    self->head = new_head;
    self->size += writable;

    return writable;
}

/* ============================================================
   READ FROM HEAD
   ============================================================ */
uint32_t ringbuffer_read(RingBuffer* self, uint8_t* buffer, uint32_t length) {
    if(self->size == 0) return 0;

    uint32_t readable = min_u32(length, self->size);

    uint32_t first_part = min_u32(readable, self->buffer.size - self->head);

    memcpy(buffer, self->buffer.ptr + self->head, first_part);

    uint32_t second_part = readable - first_part;
    if(second_part > 0) {
        memcpy(buffer + first_part, self->buffer.ptr, second_part);
    }

    self->head = (self->head + readable) % self->buffer.size;
    self->size -= readable;

    return readable;
}

/* ============================================================
   READ FROM END (tail side)
   ============================================================ */
uint32_t ringbuffer_read_from_end(RingBuffer* self, uint8_t* buffer, uint32_t length) {
    if(self->size == 0) return 0;

    uint32_t readable = min_u32(length, self->size);

    uint32_t start = (self->tail + self->buffer.size - readable) % self->buffer.size;

    uint32_t first_part = min_u32(readable, self->buffer.size - start);

    memcpy(buffer, self->buffer.ptr + start, first_part);

    uint32_t second_part = readable - first_part;
    if(second_part > 0) {
        memcpy(buffer + first_part, self->buffer.ptr, second_part);
    }

    self->tail = start;
    self->size -= readable;

    return readable;
}

/* ============================================================
   INFO FUNCTIONS
   ============================================================ */
uint32_t ringbuffer_get_total_capacity(const RingBuffer* self) {
    return self->buffer.size;
}

uint32_t ringbuffer_get_empty_space(const RingBuffer* self) {
    return self->buffer.size - self->size;
}

uint32_t ringbuffer_get_current_length(const RingBuffer* self) {
    return self->size;
}

/* ============================================================
   RESIZE
   Preserves existing data order.
   ============================================================ */
uint32_t ringbuffer_resize(RingBuffer* self, uint32_t new_capacity) {
    if(new_capacity == 0) return 0;

    uint8_t* new_buffer = (uint8_t*)malloc(new_capacity);
    if(!new_buffer) return 0;

    uint32_t to_copy = min_u32(self->size, new_capacity);

    /* copy data in logical order starting from head */
    uint32_t first_part = min_u32(to_copy, self->buffer.size - self->head);

    memcpy(new_buffer, self->buffer.ptr + self->head, first_part);

    uint32_t second_part = to_copy - first_part;
    if(second_part > 0) {
        memcpy(new_buffer + first_part, self->buffer.ptr, second_part);
    }

    free(self->buffer.ptr);

    self->buffer.ptr = new_buffer;
    self->buffer.size = new_capacity;
    self->head = 0;
    self->size = to_copy;
    self->tail = to_copy % new_capacity;

    return to_copy;
}

Buffer ringbuffer_get_continuous_write_buffer(const RingBuffer* self) {
    if(self->size == self->buffer.size) {
        return ({
            Buffer result = {
                .ptr = NULL,
                .size = 0,
            };
            result;
        });
    }
    uint32_t end_index = self->head > self->tail ? self->head : self->buffer.size;
    return ({
        Buffer result = {
            .ptr = self->buffer.ptr + self->tail,
            .size = end_index - self->tail,
        };
        result;
    });
}

Buffer ringbuffer_get_continuous_write_buffer_head(const RingBuffer* self) {
    Buffer output = {
        .ptr = NULL,
        .size = 0,
    };
    if(self->size == self->buffer.size) return output;
    uint32_t start_index = self->head > self->tail ? self->tail : 0;
    output.size = self->head - start_index;
    output.ptr = self->buffer.ptr + start_index;
    return output;
}

void ringbuffer_advance_write_tail(RingBuffer* self, uint32_t amount) {
    self->size += amount;
    self->tail = (self->tail + amount) % self->buffer.size;
}

void ringbuffer_advance_write_head(RingBuffer* self, uint32_t amount) {
    self->size += amount;
    self->head = (self->head + self->buffer.size - amount) % self->buffer.size;
}
