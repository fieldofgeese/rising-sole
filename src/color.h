#pragma once

#include <raylib.h>
#include <math.h>

typedef struct ColorHSL {
    f32 h, s, l;
} ColorHSL;

#define RGB(r,g,b) (Color) {255.0f*r, 255.0f*g, 255.0f*b, 255.0f}
#define HSL(h,s,l) (ColorHSL) {h, s, l}

static inline f32 hsl_f(ColorHSL col, int n) {
    f32 k = fmodf((n + col.h/30.0f), 12.0f);
    f32 a = col.s*fminf(col.l, 1.0f-col.l);
    return col.l - a*fmaxf(fminf(fminf(k-3.0f, 9.0f-k),1.0f),-1.0f);
}

static inline Color hsl_to_rgb(ColorHSL col) {
    return RGB(hsl_f(col, 0), hsl_f(col, 8), hsl_f(col, 4));
}
