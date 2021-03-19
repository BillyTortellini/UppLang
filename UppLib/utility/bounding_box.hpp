#pragma once

#include "../math/vectors.hpp"

struct BoundingBox2
{
    vec2 min;
    vec2 max;
};

BoundingBox2 bounding_box_2_make_min_max(vec2 min, vec2 max);
BoundingBox2 bounding_box_2_make_center_size(vec2 center, vec2 size);
bool bounding_box_2_is_point_inside(const BoundingBox2& bb, const vec2& p);
BoundingBox2 bounding_box_2_combine(BoundingBox2 bb1, BoundingBox2 bb2);
