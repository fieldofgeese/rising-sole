#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <assert.h>
#include <math.h>

#include <stdlib.h>
#include <string.h>

#define MAX_CLIENTS 128
#define FPS 60

#define NET_PER_SIM_TICKS 2

#define ARRLEN(arr) (sizeof(arr) / sizeof((arr)[0]))

//
// Type redefinitions
//

typedef int8_t   i8;
typedef uint8_t  u8;
typedef int16_t  i16;
typedef uint16_t u16;
typedef int32_t  i32;
typedef uint32_t u32;
typedef int64_t  i64;
typedef uint64_t u64;
typedef float    f32;
typedef double   f64;

//
// Float stuff
//

#define EPSILON (1e-4)

static inline f32 f32_abs(f32 f) {
    return (f < 0.0f) ? -f : f;
}

static inline bool f32_equal(f32 a, f32 b) {
    return f32_abs(a - b) < EPSILON;
}

static inline f32 clamp(f32 x, f32 a, f32 b) {
    return fminf(fmaxf(x, a), b);
}

//
// Time
//

#define NANOSECONDS(n) (1000000000ull*(n))

static inline u64 time_current() {
    struct timespec t;
    assert(clock_gettime(CLOCK_MONOTONIC, &t) == 0);
    return t.tv_sec * NANOSECONDS(1) + t.tv_nsec;
}

static inline void time_nanosleep(u64 t) {
    nanosleep(&(struct timespec) { .tv_nsec = t }, NULL);
}

//
// Byte stuff
//

#define extract_struct(bytes, type) \
    extract_struct_impl(bytes, sizeof(type))

static inline void *extract_struct_impl(const u8 **bytes, size_t size) {
    void *ptr = (void *) *bytes;
    *bytes += size;
    return ptr;
}

//
// Ciruclar buffer
//

#define CIRCULAR_BUFFER_APPEND(buf, element)                                    \
    do {                                                                        \
        assert((buf)->used < ARRLEN((buf)->data));                              \
        const size_t top = ((buf)->bottom + (buf)->used) % ARRLEN((buf)->data); \
        (buf)->data[top] = element;                                             \
        (buf)->used++;                                                          \
    } while (0)

#define CIRCULAR_BUFFER_POP(buf)                                                \
    do {                                                                        \
        assert((buf)->used > 0);                                                \
        (buf)->bottom = ((buf)->bottom + 1) % ARRLEN((buf)->data);              \
        (buf)->used--;                                                          \
    } while (0)

//
// ByteBuffer
//

typedef struct ByteBuffer {
    u8 *base;
    u8 *top;
    size_t size;
} ByteBuffer;

static inline ByteBuffer byte_buffer_init(void *ptr, size_t size) {
    return (ByteBuffer) {
        .base = ptr,
        .size = size,
        .top = ptr,
    };
}

static inline ByteBuffer byte_buffer_alloc(size_t size) {
    void *ptr = malloc(size);
    assert(ptr);
    return byte_buffer_init(ptr, size);
}

static inline void byte_buffer_free(ByteBuffer *b) {
    free(b->base);
    b->size = 0;
    b->top = NULL;
}

#define APPEND(buffer, data) \
    append(buffer, data, sizeof(*data))

static inline void append(ByteBuffer *buffer, void *data, size_t size) {
    assert(buffer->top + size <= buffer->base + buffer->size);
    memcpy(buffer->top, data, size);
    buffer->top += size;
}

#define POP(buffer, data) \
    pop(buffer, (void **) data, sizeof(**data))

static inline void pop(ByteBuffer *buffer, void **data, size_t size) {
    assert(buffer->top + size <= buffer->base + buffer->size);
    *data = buffer->top;
    buffer->top += size;
}
