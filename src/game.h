#pragma once

#include "v2.h"
#include "collision.h"

typedef enum InputType {
    INPUT_NULL,

    INPUT_MOVE_LEFT,
    INPUT_MOVE_RIGHT,
    INPUT_MOVE_UP,
    INPUT_MOVE_DOWN,

    INPUT_MOVE_DODGE,

    INPUT_SHOOT,

    INPUT_QUIT,
    INPUT_LAST,
} InputType;

typedef struct Input {
    v2 aim;
    bool active[INPUT_LAST];
} Input;

typedef enum PlayerState {
    PLAYER_STATE_DEFAULT = 0,
    PLAYER_STATE_SLIDING,
} PlayerState;

typedef struct Player {
    bool occupied;

    v2 pos;
    v2 velocity;

    v2 dodge;
    v2 look;

    f32 time_left_in_dodge;
    f32 time_left_in_dodge_delay;
    f32 time_left_in_shoot_delay;

    f32 hue;

    f32 health;

    PlayerState state;
} Player;

typedef enum Tiles {
    TILE_INVALID = 0,
    TILE_GRASS = ' ',
    TILE_STONE = '#',
} Tiles;

typedef struct Map {
    const u8 *data;
    u32 width;
    u32 height;
    f32 tile_size;
    v2 origin;
} Map;

typedef struct Game {
    Map map;
    Player players[MAX_CLIENTS];
} Game;

static inline bool map_coord_in_bounds(const Map *map, i32 i, i32 j) {
    return i >= 0 && i <= map->width &&
           j >= 0 && j <= map->height;
}

static inline void map_coord(const Map *map, i32 *i, i32 *j, v2 at) {
    *i = (at.x - map->origin.x)/map->tile_size;
    *j = (at.y - map->origin.y)/map->tile_size;
}

static inline u8 map_at(const Map *map, v2 at) {
    i32 i = 0;
    i32 j = 0;
    map_coord(map, &i, &j, at);
    if (!map_coord_in_bounds(map, i, j))
        return TILE_INVALID;

    return map->data[j*map->width + i];
}

static Map map = {
    .data = (const u8 *) "################"
                         "#              #"
                         "# ####         #"
                         "# #            #"
                         "# #            #"
                         "# #            #"
                         "#              #"
                         "#              #"
                         "#              #"
                         "#              #"
                         "#              #"
                         "#              #"
                         "#        #     #"
                         "#              #"
                         "#              #"
                         "################",
    .width = 16,
    .height = 16,
    .tile_size = 1.0f,
    .origin = {-8.0f, -8.0f},
};

#include "draw.h"
#include "log.h"

static v2 raycast_grid(Game *game, v2 pos, v2 dir) {
    assert(f32_equal(v2len2(dir), 1.0f));

    i32 i;
    i32 j;
    map_coord(&game->map, &i, &j, pos);
    assert(map_coord_in_bounds(&game->map, i, j));

    const f32 sign_x = dir.x > 0.0f ? 1 : -1;
    const f32 sign_y = dir.y > 0.0f ? 1 : -1;

    const u32 tile_offset_x = dir.x > 0.0f ? 1 : 0;
    const u32 tile_offset_y = dir.y > 0.0f ? 1 : 0;

    f32 t = 0;

    f32 dtx = (((f32) i + tile_offset_x)*game->map.tile_size - pos.x + game->map.origin.x) / dir.x;
    f32 dty = (((f32) j + tile_offset_y)*game->map.tile_size - pos.y + game->map.origin.y) / dir.y;

    while (map_coord_in_bounds(&game->map, i, j)) {
        const u8 tile = game->map.data[j*game->map.width + i];
        if (tile == TILE_STONE)
            break;

        if (dtx < dty) {
            i += sign_x;
            const f32 dt = dtx;
            t += dt;
            dtx += (f32) sign_x * game->map.tile_size / dir.x - dt;
            dty -= dt;
        } else {
            j += sign_y;
            const f32 dt = dty;
            t += dt;
            dtx -= dt;
            dty += (f32) sign_y * game->map.tile_size / dir.y - dt;
        }
    }

    return v2scale(t, dir);
}

static void game_update(Game *game,
                        Player *p,
                        Input *input,
                        const f32 dt,
                        bool replaying) {
    const f32 move_acceleration = 0.5f/dt;
    const f32 max_move_speed = 5.0f;

    const f32 dodge_acceleration = 1.0f/dt;
    const f32 dodge_deceleration = 0.10f/dt;
    const f32 max_dodge_speed = 10.0f;
    const f32 dodge_time = 0.10f;
    const f32 dodge_delay_time = 1.0f;

    p->look = v2normalize(input->aim);

    //bool can_shoot = p->time_left_in_shoot_delay == 0.0f;
    //if (input->active[INPUT_SHOOT] && can_shoot) {
    //    struct projectile *projectile = NULL;
    //    for (u32 i = 0; i < MAX_PROJECTILES; ++i) {
    //        projectile = &game->projectiles[i];
    //        if (!projectile->alive)
    //            break;
    //    }

    //    if (projectile != NULL) {
    //        projectile->pos = p->pos;
    //        projectile->velocity = v2scale(5.0f, p->look);
    //        projectile->time_left = 5.0f;
    //        projectile->times_bounced = 0;
    //        projectile->alive = true;
    //        p->time_left_in_shoot_delay = 0.5f;
    //    }
    //}

    if (p->time_left_in_shoot_delay > 0.0f) {
        p->time_left_in_shoot_delay -= dt;
        if (p->time_left_in_shoot_delay <= 0.0f)
            p->time_left_in_shoot_delay = 0.0f;
    }

    if (p->time_left_in_dodge_delay > 0.0f) {
        p->time_left_in_dodge_delay -= dt;
        if (p->time_left_in_dodge_delay <= 0.0f)
            p->time_left_in_dodge_delay = 0.0f;
    }

    // Dodge state
    bool in_dodge = p->state == PLAYER_STATE_SLIDING;
    bool in_dodge_delay = p->time_left_in_dodge_delay > 0.0f;
    if (!in_dodge_delay && !in_dodge && input->active[INPUT_MOVE_DODGE]) {
        p->dodge = p->look;
        p->time_left_in_dodge = dodge_time;
        p->state = PLAYER_STATE_SLIDING;

        // If we have a velocity in any other direction than the dodge dir,
        // redirect it in the dodge dir
        const f32 speed = v2len(p->velocity);
        p->velocity = v2scale(speed, p->dodge);
    }

    bool has_moved = false;

    // Dodge movement
    if (p->state == PLAYER_STATE_SLIDING) {
        if (p->time_left_in_dodge > 0.0f) {
            p->velocity = v2add(p->velocity, v2scale(dt*dodge_acceleration, p->dodge));
            const f32 speed = v2len(p->velocity);
            if (speed > max_dodge_speed) {
                p->velocity = v2scale(max_dodge_speed, v2normalize(p->velocity));
            }

            has_moved = true;

            p->time_left_in_dodge -= dt;
            if (p->time_left_in_dodge <= 0.0f) {
                p->time_left_in_dodge = 0.0f;
            }
        } else {
            const v2 slowdown_dir = v2neg(v2normalize(p->velocity));
            const f32 speed = v2len(p->velocity);
            if (speed > 0.0f) {
                const f32 slowdown = fminf(speed, dt*dodge_deceleration);
                if (speed < dt*dodge_deceleration) {
                    p->state = PLAYER_STATE_DEFAULT;
                    p->time_left_in_dodge_delay = dodge_delay_time;
                }
                p->velocity = v2add(p->velocity, v2scale(slowdown, slowdown_dir));
            }
        }
    }

    // Process player inputs
    const f32 dx = 1.0f*(f32) input->active[INPUT_MOVE_RIGHT] - 1.0f*(f32) input->active[INPUT_MOVE_LEFT];
    const f32 dy = 1.0f*(f32) input->active[INPUT_MOVE_DOWN]  - 1.0f*(f32) input->active[INPUT_MOVE_UP];
    const v2 dv = {dx, dy};
    const f32 len2 = v2len2(dv);

    if (p->state == PLAYER_STATE_SLIDING && p->time_left_in_dodge == 0.0f) {
        const f32 speed = v2len(p->velocity);
        if (speed <= max_move_speed && len2 > 0.0f) {
            p->state = PLAYER_STATE_DEFAULT;
            p->time_left_in_dodge_delay = dodge_delay_time;
        }
    }

    // Player movement
    if (p->state != PLAYER_STATE_SLIDING) {
        if (len2 > 0.0f) {
            const f32 len = sqrtf(len2);

            p->velocity = v2add(p->velocity, v2scale(dt*move_acceleration/len, dv));
            const f32 speed = v2len(p->velocity);
            if (speed > max_move_speed) {
                p->velocity = v2scale(max_move_speed, v2normalize(p->velocity));
            }
        } else {
            const v2 slowdown_dir = v2neg(v2normalize(p->velocity));
            const f32 speed = v2len(p->velocity);
            if (speed > 0.0f) {
                const f32 slowdown = fminf(speed, dt*move_acceleration);
                p->velocity = v2add(p->velocity, v2scale(slowdown, slowdown_dir));
            }
        }
    }

    // "Integrate" velocity
    if (!v2iszero(p->velocity)) {
        p->pos = v2add(p->pos, v2scale(dt, p->velocity));
        has_moved = true;
    }

    if (!has_moved) {
        return;
    }

    // Handle collisions by checking collision against the
    // eight tiles around the player
    const v2 tile_offsets[8] = {
        {+1,  0},
        {+1, -1},
        { 0, -1},
        {-1, -1},
        {-1,  0},
        {-1, +1},
        { 0, +1},
        {+1, +1},
    };

    for (i32 i = 0; i < ARRLEN(tile_offsets); ++i) {
        const v2 at = v2add(p->pos, v2scale(game->map.tile_size, tile_offsets[i]));
        const u8 tile = map_at(&game->map, at);
        if (tile != TILE_STONE)
            continue;

        const f32 radius = 0.25f;
        Collision result = collide_aabb_circle((AABB) {
                                                   .pos = {floorf(at.x), floorf(at.y)},
                                                   .width = game->map.tile_size,
                                                   .height = game->map.tile_size,
                                               },
                                               (Circle) {
                                                   .pos = p->pos,
                                                   .radius = radius,
                                               });

        if (!result.colliding || v2iszero(result.resolve))
            continue;

        p->pos = v2add(p->pos, result.resolve);

        if (in_dodge) {
            f32 dot = v2dot(p->dodge, v2normalize(result.resolve));
            // resolve and dodge should be pointing in opposite directions.
            // If the dot product is <= -0.5f the relative direction between
            // the vectors should >= 90+45 deg, we choose -0.6f to be a bit
            // more lenient, feels a bit better.
            if (dot <= -0.6f) {
                p->state = PLAYER_STATE_DEFAULT;
                p->time_left_in_dodge = 0.0f;
                p->time_left_in_dodge_delay = dodge_delay_time;
            }
        }
    }
}
