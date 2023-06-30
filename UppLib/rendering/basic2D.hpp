#pragma once

#include "../math/vectors.hpp"

enum class Unit
{
    PIXELS,  // Note: 0 in the y axis is bottom of window...
    NORMALIZED_SCREEN, // -1.0f - 1.0f
    CENTIMETER, // Note: this uses DPI given by windows, which may not be correct on some monitors...
};

// Converting between 2D units
vec2 convertSizeFromTo(vec2 value, Unit from, Unit to);
float convertHeightFromTo(float value, Unit from, Unit to);
float convertWidthFromTo(float value, Unit from, Unit to);
vec2 convertPointFromTo(vec2 value, Unit from, Unit to);
float convertYFromTo(float value, Unit from, Unit to);
float convertXFromTo(float value, Unit from, Unit to);

// Converting to standard global unit
vec2 convertSize(vec2 value, Unit unit);
float convertWidth(float value, Unit unit);
float convertHeight(float value, Unit unit);
vec2 convertPoint(vec2 value, Unit unit);

constexpr Unit GLOBAL_INTERNAL_UNIT = Unit::PIXELS;



enum class Anchor
{
    TOP_LEFT,
    TOP_CENTER,
    TOP_RIGHT,

    CENTER_LEFT,
    CENTER_CENTER,
    CENTER_RIGHT,
    
    BOTTOM_LEFT,
    BOTTOM_CENTER,
    BOTTOM_RIGHT
};

vec2 anchor_to_direction(Anchor anchor);
vec2 anchor_switch(vec2 position, vec2 size, Anchor from, Anchor to); // Returns a point that results in the same rectangle with the new anchor



struct Bounding_Box2
{
    vec2 min;
    vec2 max;
};

Bounding_Box2 bounding_box_2_make_min_max(vec2 min, vec2 max);
Bounding_Box2 bounding_box_2_make_anchor(vec2 pos, vec2 size, Anchor anchor);
bool bounding_box_2_is_point_inside(const Bounding_Box2& bb, const vec2& p);
bool bounding_box_2_is_other_box_inside(const Bounding_Box2& bb, const Bounding_Box2& inside);
Bounding_Box2 bounding_box_2_combine(Bounding_Box2 bb1, Bounding_Box2 bb2);
