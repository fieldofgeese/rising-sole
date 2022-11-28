#pragma once

#include "common.h"
#include "v2.h"

typedef struct Collision {
    bool colliding;
    v2 resolve;
} Collision;

typedef struct AABB {
    v2 pos;
    f32 width;
    f32 height;
} AABB;

typedef struct Circle {
    v2 pos;
    f32 radius;
} Circle;

static Collision collide_circle_circle(Circle c0, Circle c1) {
    const f32 radius_sum = c0.radius + c1.radius;
    const v2 center_diff = v2sub(c1.pos, c0.pos);
    const f32 center_diff_len2 = v2len2(center_diff);

    Collision result = {
        .colliding = false,
    };

    if (center_diff_len2 > radius_sum*radius_sum)
        return result;

    const f32 center_diff_len = sqrtf(center_diff_len2);
    const f32 overlap = radius_sum - center_diff_len;

    result.colliding = true;
    result.resolve = v2scale(overlap/center_diff_len, center_diff);

    return result;
}

static Collision collide_aabb_circle(AABB rect, Circle circle) {
    v2 nearest = {
        .x = clamp(circle.pos.x, rect.pos.x, rect.pos.x + rect.width),
        .y = clamp(circle.pos.y, rect.pos.y, rect.pos.y + rect.height),
    };

    nearest = v2sub(nearest, circle.pos);
    const f32 dist2 = v2len2(nearest);

    Collision result = {
        .colliding = false,
    };
    if (circle.radius*circle.radius < dist2)
        return result;

    const f32 dist = sqrtf(dist2);
    result.colliding = true;
    result.resolve = v2scale(-(circle.radius-dist)/dist, nearest);

    return result;
}

static bool collide_ray_aabb(v2 pos, v2 dir, AABB rect, v2 *res) {
    const f32 x0 = rect.pos.x;
    const f32 x1 = rect.pos.x + rect.width;
    const f32 y0 = rect.pos.y;
    const f32 y1 = rect.pos.y + rect.height;

    //printf("pos: {%f, %f}\n", pos.x, pos.y);
    //printf("dir: {%f, %f}\n", dir.x, dir.y);

    const f32 dist_x0 = fabsf(x0 - pos.x);
    const f32 dist_x1 = fabsf(x1 - pos.x);
    const f32 dist_y0 = fabsf(y0 - pos.y);
    const f32 dist_y1 = fabsf(y1 - pos.y);

    //printf("%f,%f,%f,%f\n", x0, x1, y0, y1);
    //printf("%f,%f,%f,%f\n", dist_x0, dist_x1, dist_y0, dist_y1);

    const f32 x = (dist_x0 < dist_x1) ? x0 : x1;
    const f32 y = (dist_y0 < dist_y1) ? y0 : y1;

    //printf("%f, %f\n", x, y);

    if (v2dot((v2) {x-pos.x,y-pos.y}, dir) <= 0.0f)
        return false;

    const f32 hit_line_y = pos.y + (dir.y/dir.x)*fabsf(x - pos.x);
    const f32 hit_line_x = pos.x + (dir.x/dir.y)*fabsf(y - pos.y);

    const i32 hit_x = hit_line_x <= x1 && hit_line_x >= x0;
    const i32 hit_y = hit_line_y <= y1 && hit_line_y >= y0;
    const i32 hit = hit_x | hit_y;

    //printf("checking collide %d, %d\n", hit_x, hit_y);
    //printf("%f, %f\n", hit_line_x, hit_line_y);

    if (!hit)
        return false;

    //printf("hit\n");

    if (hit_x)
        *res = (v2) {hit_line_x, y};
    else if (hit_y)
        *res = (v2) {x, hit_line_y};

    return true;
}
