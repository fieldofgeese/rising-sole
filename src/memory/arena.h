#pragma once

//
// Implements a memory allocator where you can allocate a bunch of
// without having to worry about freeing, and then free everything
// at once. Perfect for data that exist during a single frame.
//

#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <stdlib.h>

#include "memory.h"

typedef struct ArenaAllocator {
    uint8_t *memory;
    size_t top;
    size_t size;
} ArenaAllocator;

static inline ArenaAllocator arena_allocator(size_t size) {
    void *memory = malloc(size);
    assert(memory);

    return (ArenaAllocator) {
        .memory = memory,
        .top = 0,
        .size = size,
    };
}

static inline void arena_free(ArenaAllocator *arena) {
    free(arena->memory);
    arena->memory = NULL;
    arena->top = 0;
    arena->size = 0;
}

static inline void *arena_allocate(ArenaAllocator *arena, size_t size) {
    assert(arena->top + size <= arena->size);
    void *ptr = (void *) (arena->memory + arena->top);
    arena->top += size;
    return ptr;
}

static inline void arena_clear(ArenaAllocator *arena) {
    arena->top = 0;
}
