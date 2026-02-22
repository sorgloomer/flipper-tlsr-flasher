#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct Buffer {
    uint8_t* ptr;
    uint32_t size;
} Buffer;

/**
    A fixed size circular buffer with only non-blocking operations.
    Not thread-safe.
 */
typedef struct RingBuffer {
    Buffer buffer;
    uint32_t head;
    uint32_t tail;
    uint32_t size;
} RingBuffer;

RingBuffer* ringbuffer_alloc(uint32_t capacity);
void ringbuffer_free(RingBuffer* self);
void ringbuffer_init(RingBuffer* self, uint32_t capacity);
void ringbuffer_deinit(RingBuffer* self);

uint32_t ringbuffer_get_total_capacity(const RingBuffer* self);
uint32_t ringbuffer_get_empty_space(const RingBuffer* self);
uint32_t ringbuffer_get_current_length(const RingBuffer* self);

Buffer ringbuffer_get_continuous_write_buffer(const RingBuffer* self);
Buffer ringbuffer_get_continuous_write_buffer_head(const RingBuffer* self);
void ringbuffer_advance_write_tail(RingBuffer* self, uint32_t amount);
void ringbuffer_advance_write_head(RingBuffer* self, uint32_t amount);

/**
    Writes the specified bytes into the end of the buffer. Does not block.
    Always writes as many bytes as possible based on the available capacity and
    input buffer length.

    @return Returns the byte count written to the buffer. Returns 0 if and only
            if the buffer is full.
 */
uint32_t ringbuffer_write(RingBuffer* self, uint8_t* buffer, uint32_t length);

/**
    Similar to ringbuffer_write, but this one prepends the contents to the
    beginning.
 */
uint32_t ringbuffer_write_to_start(RingBuffer* self, uint8_t* buffer, uint32_t length);

/**
    Reads the specified bytes from the head of the buffer. Does not block.
    Always reads as many bytes as possible based on the available size and
    input buffer length.

    @return The number of bytes read. Returns 0 if and only if the buffer is empty.
 */
uint32_t ringbuffer_read(RingBuffer* self, uint8_t* buffer, uint32_t length);

/**
    Similar to ringbuffer_read, but reads the bytes from the end.
 */
uint32_t ringbuffer_read_from_end(RingBuffer* self, uint8_t* buffer, uint32_t length);

uint32_t ringbuffer_resize(RingBuffer* self, uint32_t new_capacity);
