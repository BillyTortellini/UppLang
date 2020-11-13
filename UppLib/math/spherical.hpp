#pragma once

#include "vectors.hpp"

// -----------------------------
// --- SPHERICAL COORDINATES ---
// -----------------------------
// Contains functions for transfering spherical to euclidean coordinates

vec3 math_coordinates_spherical_to_euclidean(const vec2& s);
vec3 math_coordinates_spherical_to_euclidean(const vec3& s);
vec2 math_normalize_spherical(const vec2& s);
