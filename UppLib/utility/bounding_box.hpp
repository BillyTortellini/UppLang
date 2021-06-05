#pragma once

#include "../math/vectors.hpp"

struct Bounding_Box2
{
    vec2 min;
    vec2 max;
};

Bounding_Box2 bounding_box_2_make_min_max(vec2 min, vec2 max);
Bounding_Box2 bounding_box_2_make_center_size(vec2 center, vec2 size);
bool bounding_box_2_is_point_inside(const Bounding_Box2& bb, const vec2& p);
bool bounding_box_2_is_other_box_inside(const Bounding_Box2& bb, const Bounding_Box2& inside);
Bounding_Box2 bounding_box_2_combine(Bounding_Box2 bb1, Bounding_Box2 bb2);
