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



box2 box2_convert(const box2& bb, Unit unit) {
    box2 result;
    result.min = convertPoint(bb.min, unit);
    result.max = convertPoint(bb.max, unit);
    return result;
}
