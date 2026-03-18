#pragma once

#include "../math/vectors.hpp"
#include "../utility/utils.hpp"

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

box2 box2_convert(const box2& bb, Unit unit);
