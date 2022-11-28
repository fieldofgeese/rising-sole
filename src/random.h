#pragma once

#include "common.h"

typedef u32 u32_4x __attribute__((vector_size(4*sizeof(u32))));

#define random_default_seed(series)                                     \
    _Generic((series),                                                  \
             struct random_series_xors *: random_default_seed_xors,     \
             struct random_series_pcg *:  random_default_seed_pcg       \
             )(series)

#define random_next_u32(series)                                 \
    _Generic((series),                                          \
             struct random_series_xors *: random_next_u32_xors, \
             struct random_series_pcg *:  random_next_u32_pcg   \
             )(series)

#define random_bilateral(series)                                 \
    _Generic((series),                                           \
             struct random_series_xors *: random_bilateral_xors, \
             struct random_series_pcg *:  random_bilateral_pcg   \
             )(series)

#define random_unilateral(series)                                 \
    _Generic((series),                                            \
             struct random_series_xors *: random_unilateral_xors, \
             struct random_series_pcg *:  random_unilateral_pcg   \
             )(series)

#define random_f32(series, min, max)                       \
    _Generic((series),                                     \
             struct random_series_xors *: random_f32_xors, \
             struct random_series_pcg *:  random_f32_pcg   \
             )(series, min, max)

// XORS

struct random_series_xors {
    u32_4x state;
};

static inline void random_default_seed_xors(struct random_series_xors *series) {
    series-> state = (u32_4x) {78953890, 235498, 893456, 93453080};
}

static inline u32_4x random_next_u32_4x_xors(struct random_series_xors *series) {
    u32_4x result = series->state;
    result ^= result << 13;
    result ^= result >> 17;
    result ^= result << 5;
    series->state = result;

    return result;
}

static inline u32 random_next_u32_xors(struct random_series_xors *series) {
    return random_next_u32_4x_xors(series)[0];
}

static inline f32 random_unilateral_xors(struct random_series_xors *series) {
    f32 divisor = 1.0f / (f32) UINT32_MAX;
    f32 result = divisor * (f32) random_next_u32_xors(series);
    return result;
}

static inline f32 random_bilateral_xors(struct random_series_xors *series) {
    return 2.0f*random_unilateral_xors(series) - 1.0f;
}

static inline f32 random_f32_xors(struct random_series_xors *series, f32 min, f32 max) {
    f32 divisor = 1.0f / (f32) UINT32_MAX;
    return min + (max-min) * (divisor * (f32) random_next_u32_xors(series));
}

// PCG

struct random_series_pcg {
    u64 state;
    u64 selector;
};

static inline void random_seed_pcg(struct random_series_pcg *series, u64 state, u64 selector) {
    series->state = state;
    series->selector = (selector << 1) | 1;
}

static inline void random_default_seed_pcg(struct random_series_pcg *series) {
    random_seed_pcg(series, 7123915, 912305);
}

static inline u32 rotate_right(u32 value, i32 amount) {
    amount &= 31;
    return (value >> amount) | (value << (32 - amount));
}

static inline u32 random_next_u32_pcg(struct random_series_pcg *series) {
    u64 state = series->state;
    state = state * 6364136223846793005ULL + series->selector;
    series->state = state;

    u32 pre_rotate = (u32) ((state ^ (state >> 18)) >> 27);
    u32 result = rotate_right(pre_rotate, (i32) (state >> 59));

    return result;
}

static inline f32 random_unilateral_pcg(struct random_series_pcg *series) {
    f32 divisor = 1.0f / (f32) UINT32_MAX;
    return divisor * (f32) random_next_u32_pcg(series);
}

static inline f32 random_bilateral_pcg(struct random_series_pcg *series) {
    return 2.0f*random_unilateral_pcg(series) - 1.0f;
}

static inline f32 random_f32_pcg(struct random_series_pcg *series, f32 min, f32 max) {
    f32 divisor = 1.0f / (f32) UINT32_MAX;
    return min + (max-min) * (divisor * (f32) random_next_u32_pcg(series));
}
