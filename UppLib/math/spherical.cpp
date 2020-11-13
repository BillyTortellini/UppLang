#include "spherical.hpp"

#include "scalars.hpp"

vec3 math_coordinates_spherical_to_euclidean(const vec2& s)
{
    // The s vector is made up of (azimuth angle, polar angle (measured from zenith pointing up))
    float y = math_sine(s.y);
    float len_xz = math_cosine(s.y);
    float z = -math_cosine(s.x) * len_xz;
    float x = -math_sine(s.x) * len_xz;
    return vec3(x, y, z);
}

vec3 math_coordinates_spherical_to_euclidean(const vec3& s)
{
    // The s vector is made up of (azimuth angle, polar angle, radius)
    return math_coordinates_spherical_to_euclidean(vec2(s.x, s.y)) * s.z;
}

// Puts the azimuth in [-PI, PI) range and the polar angle in [PI/2, PI/2]
vec2 math_normalize_spherical(const vec2& s) 
{
    vec2 r;
    r.x = math_modulo_in_interval(s.x, -PI, PI);
    r.y = math_clamp(s.y, -PI/2.0f + 0.001f, PI/2.0f - 0.001f);
    return r;
}
