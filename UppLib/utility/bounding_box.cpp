#include "bounding_box.hpp"

#include "../math/scalars.hpp"

BoundingBox2 bounding_box_2_make_min_max(vec2 min, vec2 max)
{
    BoundingBox2 bb;
    bb.min = min;
    bb.max = max;
    return bb;
}

BoundingBox2 bounding_box_2_make_center_size(vec2 center, vec2 size)
{
    BoundingBox2 bb;
    bb.min = center - size / 2;
    bb.max = center + size / 2;
    return bb;
}

bool bounding_box_2_is_point_inside(const BoundingBox2& bb, const vec2& p) {
    return p.x >= bb.min.x && p.y >= bb.min.y && p.x <= bb.max.x && p.y <= bb.max.y;
}

bool bounding_box_2_is_other_box_inside(const BoundingBox2& bb, const BoundingBox2& inside) {
    return bounding_box_2_is_point_inside(bb, inside.min) && bounding_box_2_is_point_inside(bb, inside.max);
}

BoundingBox2 bounding_box_2_combine(BoundingBox2 bb1, BoundingBox2 bb2) 
{
    BoundingBox2 result;
    result.min = vec2(
        math_minimum(bb1.min.x, bb2.min.x),
        math_minimum(bb1.min.y, bb2.min.y)
    );
    result.max = vec2(
        math_maximum(bb1.max.x, bb2.max.x),
        math_maximum(bb1.max.y, bb2.max.y)
    );
    return result;
}

