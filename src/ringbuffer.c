#include <furi.h>
#include <string.h>
#include "ringbuffer.h"

#define _INLINE __attribute__((always_inline)) inline

_INLINE static uint32_t min_u32(uint32_t a, uint32_t b) {
    return (a < b) ? a : b;
}

void ringbuffer_init(struct RingBuffer* self, uint32_t capacity) {
    self->capacity = capacity;
    self->buffer = malloc(capacity);
    furi_check(self->buffer);
    self->head = 0;
    self->tail = 0;
    self->size = 0;
}

void ringbuffer_deinit(RingBuffer* self) {
    free(self->buffer);
}

RingBuffer* ringbuffer_alloc(uint32_t capacity) {
    RingBuffer* self = malloc(sizeof(RingBuffer));
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
    if(self->size == self->capacity) return 0;

    uint32_t writable = min_u32(length, self->capacity - self->size);

    uint32_t first_part = min_u32(writable, self->capacity - self->tail);

    memcpy(self->buffer + self->tail, buffer, first_part);

    uint32_t second_part = writable - first_part;
    if(second_part > 0) {
        memcpy(self->buffer, buffer + first_part, second_part);
    }

    self->tail = (self->tail + writable) % self->capacity;
    self->size += writable;

    return writable;
}

/* ============================================================
   WRITE AT START (prepend before head)
   ============================================================ */
uint32_t ringbuffer_write_to_start(RingBuffer* self, uint8_t* buffer, uint32_t length) {
    if(self->size == self->capacity) return 0;

    uint32_t writable = min_u32(length, self->capacity - self->size);

    /* new head position */
    uint32_t new_head = (self->head + self->capacity - writable) % self->capacity;

    uint32_t first_part = min_u32(writable, self->capacity - new_head);

    memcpy(self->buffer + new_head, buffer, first_part);

    uint32_t second_part = writable - first_part;
    if(second_part > 0) {
        memcpy(self->buffer, buffer + first_part, second_part);
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

    uint32_t first_part = min_u32(readable, self->capacity - self->head);

    memcpy(buffer, self->buffer + self->head, first_part);

    uint32_t second_part = readable - first_part;
    if(second_part > 0) {
        memcpy(buffer + first_part, self->buffer, second_part);
    }

    self->head = (self->head + readable) % self->capacity;
    self->size -= readable;

    return readable;
}

/* ============================================================
   READ FROM END (tail side)
   ============================================================ */
uint32_t ringbuffer_read_from_end(RingBuffer* self, uint8_t* buffer, uint32_t length) {
    if(self->size == 0) return 0;

    uint32_t readable = min_u32(length, self->size);

    uint32_t start = (self->tail + self->capacity - readable) % self->capacity;

    uint32_t first_part = min_u32(readable, self->capacity - start);

    memcpy(buffer, self->buffer + start, first_part);

    uint32_t second_part = readable - first_part;
    if(second_part > 0) {
        memcpy(buffer + first_part, self->buffer, second_part);
    }

    self->tail = start;
    self->size -= readable;

    return readable;
}

/* ============================================================
   INFO FUNCTIONS
   ============================================================ */
uint32_t ringbuffer_get_total_capacity(RingBuffer* self) {
    return self->capacity;
}

uint32_t ringbuffer_get_empty_space(RingBuffer* self) {
    return self->capacity - self->size;
}

uint32_t ringbuffer_get_current_length(RingBuffer* self) {
    return self->size;
}

/* ============================================================
   RESIZE
   Preserves existing data order.
   ============================================================ */
uint32_t ringbuffer_resize(RingBuffer* self, uint32_t new_capacity) {
    if(new_capacity == 0) return 0;

    uint8_t* new_buffer = malloc(new_capacity);
    if(!new_buffer) return 0;

    uint32_t to_copy = min_u32(self->size, new_capacity);

    /* copy data in logical order starting from head */
    uint32_t first_part = min_u32(to_copy, self->capacity - self->head);

    memcpy(new_buffer, self->buffer + self->head, first_part);

    uint32_t second_part = to_copy - first_part;
    if(second_part > 0) {
        memcpy(new_buffer + first_part, self->buffer, second_part);
    }

    free(self->buffer);

    self->buffer = new_buffer;
    self->capacity = new_capacity;
    self->head = 0;
    self->size = to_copy;
    self->tail = to_copy % new_capacity;

    return to_copy;
}

bool ringbuffer_get_continuous_write_buffer(RingBuffer* self, Buffer* output) {
    if(self->size == self->capacity) return true;
    uint32_t end_index = self->head > self->tail ? self->head : self->capacity;
    output->size = end_index - self->tail;
    output->ptr = self->buffer + self->tail;
    return false;
}

bool ringbuffer_get_continuous_write_buffer_head(RingBuffer* self, Buffer* output) {
    if(self->size == self->capacity) return true;
    uint32_t start_index = self->head > self->tail ? self->tail : 0;
    output->size = self->head - start_index;
    output->ptr = self->buffer + start_index;
    return false;
}

void ringbuffer_advance_write_tail(RingBuffer* self, uint32_t amount) {
    self->size += amount;
    self->tail = (self->tail + amount) % self->capacity;
}

void ringbuffer_advance_write_head(RingBuffer* self, uint32_t amount) {
    self->size += amount;
    self->head = (self->head + self->capacity - amount) % self->capacity;
}
