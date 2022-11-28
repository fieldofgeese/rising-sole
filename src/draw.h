#pragma once

#include "common.h"
#include "v2.h"
#include <raylib.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

typedef struct Graph {
    f32 *data;
    f32 max;
    f32 min;
    u32 size;
    u32 top;
} Graph;

static inline Graph graph_new(u32 size) {
    Graph g = {
        .data = malloc(size * sizeof(f32)),
        .max = 1.0f,
        .min = 0.0f,
        .size = size,
        .top = 0,
    };
    memset(g.data, 0, size * sizeof(f32));
    return g;
}

static inline void graph_free(Graph *g) {
    free(g->data);
    g->size = 0;
    g->data = NULL;
}

static inline void graph_append(Graph *g, f32 y) {
    g->data[g->top] = y;
    g->top = (g->top + 1) % g->size;
}

typedef struct Player Player;
typedef struct Map Map;
typedef struct Game Game;

f32 world_to_screen_length(f32 len);
Vector2 world_to_screen(v2 v);
Vector2 screen_to_world(Vector2 v);

void debug_v2(v2 pos, v2 v, f32 scale, Color color);
void draw_all_debug_v2s();

void draw_centered_line(v2 start, v2 end, f32 thickness, Color color);
void draw_v2(v2 pos, v2 v, f32 scale, Color color);
void draw_tile(f32 x, f32 y, f32 border_thickness, Color light, Color dark);
void draw_water(f32 t);
void draw_map(const Map *map);
//void draw_projectiles(const Game *game);
void draw_player(const Player *p);
void draw_game(const Game *game, const f32 t);
void draw_graph(Graph *g, v2 pos, v2 size, v2 margin);
