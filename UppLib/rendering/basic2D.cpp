#include "basic2D.hpp"

#include "rendering_core.hpp"

vec2 convertSizeFromTo(vec2 value, Unit from, Unit to) 
{
    auto& info = rendering_core.render_information;
    if (from == to) {
        return value;
    }

    // Do extra conversions so we don't have as much cases to code
    if (from == Unit::CENTIMETER && to == Unit::NORMALIZED_SCREEN) {
        value = convertSizeFromTo(value, from, Unit::PIXELS);
        from = Unit::PIXELS;
    }
    else if (from == Unit::NORMALIZED_SCREEN && to == Unit::CENTIMETER) {
        value = convertSizeFromTo(value, from, Unit::NORMALIZED_SCREEN);
        from = Unit::PIXELS;
    }

    // We only support PIXELS <-> NORMALIZED_SCREEN and PIXELS <-> CENTIMETER
    // all other cases are done in the previous code
    if (from == Unit::PIXELS && to == Unit::NORMALIZED_SCREEN) {
        return vec2(
            (value.x / (float)info.backbuffer_width) * 2.0f,
            (value.y / (float)info.backbuffer_height) * 2.0f
        );
    }
    else if (from == Unit::NORMALIZED_SCREEN && to == Unit::PIXELS) {
        return vec2(
            (value.x) / 2.0f * (float)info.backbuffer_width,
            (value.y) / 2.0f * (float)info.backbuffer_height
        );
    }

    if (from == Unit::PIXELS && to == Unit::CENTIMETER) {
        return vec2(
            value.x / (float)info.monitor_dpi * 2.54f,
            value.y / (float)info.monitor_dpi * 2.54f
        );
    }
    else if (from == Unit::CENTIMETER && to == Unit::PIXELS) {
        return vec2(
            value.x / 2.54f * (float)info.monitor_dpi,
            value.y / 2.54f * (float)info.monitor_dpi
        );
    }

    panic("All cases should be exhausted by now!");
    return value;
}

float convertHeightFromTo(float value, Unit from, Unit to) {
    return convertSizeFromTo(vec2(1.0f, value), from, to).y;
}

float convertWidthFromTo(float value, Unit from, Unit to) {
    return convertSizeFromTo(vec2(value, 1.0f), from, to).x;
}

vec2 convertPointFromTo(vec2 value, Unit from, Unit to) {
    auto& info = rendering_core.render_information;
    if (from == to) {
        return value;
    }

    if (to == Unit::NORMALIZED_SCREEN) {
        return convertSizeFromTo(value, from, to) - vec2(1.0f);
    }
    if (from == Unit::NORMALIZED_SCREEN) {
        return convertSizeFromTo(value + vec2(1.0f), from, to);
    }
    return convertSizeFromTo(value, from, to);
}

float convertYFromTo(float value, Unit from, Unit to) {
    return convertPointFromTo(vec2(0.0f, value), from, to).y;
}

float convertXFromTo(float value, Unit from, Unit to) {
    return convertPointFromTo(vec2(value, 0.0f), from, to).x;
}

vec2 convertSize(vec2 value, Unit unit) {
    return convertSizeFromTo(value, unit, GLOBAL_INTERNAL_UNIT);
}

float convertWidth(float value, Unit unit) {
    return convertWidthFromTo(value, unit, GLOBAL_INTERNAL_UNIT);
}

float convertHeight(float value, Unit unit) {
    return convertHeightFromTo(value, unit, GLOBAL_INTERNAL_UNIT);
}

vec2 convertPoint(vec2 value, Unit unit) {
    return convertPointFromTo(value, unit, GLOBAL_INTERNAL_UNIT);
}

vec2 anchor_to_direction(Anchor anchor)
{
    switch (anchor) {
    case Anchor::TOP_LEFT:       return vec2(-1.0f,  1.0f);
    case Anchor::TOP_CENTER:     return vec2( 0.0f,  1.0f);
    case Anchor::TOP_RIGHT:      return vec2( 1.0f,  1.0f);
    case Anchor::CENTER_LEFT:    return vec2(-1.0f,  0.0f);
    case Anchor::CENTER_CENTER:  return vec2( 0.0f,  0.0f);
    case Anchor::CENTER_RIGHT:   return vec2( 1.0f,  0.0f);
    case Anchor::BOTTOM_LEFT:    return vec2(-1.0f, -1.0f);
    case Anchor::BOTTOM_CENTER:  return vec2( 0.0f, -1.0f);
    case Anchor::BOTTOM_RIGHT:   return vec2( 1.0f, -1.0f);
    }
    panic("Shouldn't happen");
    return vec2(0.0f);
}

vec2 anchor_switch(vec2 position, vec2 size, Anchor from, Anchor to) {
    return position + size * (anchor_to_direction(to) - anchor_to_direction(from)) / 2.0f;
}



Bounding_Box2 bounding_box_2_make_min_max(vec2 min, vec2 max)
{
    Bounding_Box2 bb;
    bb.min = min;
    bb.max = max;
    return bb;
}

Bounding_Box2 bounding_box_2_make_anchor(vec2 pos, vec2 size, Anchor anchor) {
    Bounding_Box2 bb;
    bb.min = anchor_switch(pos, size, anchor, Anchor::BOTTOM_LEFT);
    bb.max = bb.min + size;
    return bb;
}

Bounding_Box2 bounding_box_2_make_center_size(vec2 center, vec2 size)
{
    Bounding_Box2 bb;
    bb.min = center - size / 2;
    bb.max = center + size / 2;
    return bb;
}

bool bounding_box_2_is_point_inside(const Bounding_Box2& bb, const vec2& p) {
    return p.x >= bb.min.x && p.y >= bb.min.y && p.x <= bb.max.x && p.y <= bb.max.y;
}

bool bounding_box_2_is_other_box_inside(const Bounding_Box2& bb, const Bounding_Box2& inside) {
    return bounding_box_2_is_point_inside(bb, inside.min) && bounding_box_2_is_point_inside(bb, inside.max);
}

Bounding_Box2 bounding_box_2_combine(Bounding_Box2 bb1, Bounding_Box2 bb2) 
{
    Bounding_Box2 result;
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

