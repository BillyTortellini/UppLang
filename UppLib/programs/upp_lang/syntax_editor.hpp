#pragma once

#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/string.hpp"
#include "../../math/vectors.hpp"

struct Rendering_Core;
struct Input;
struct Text_Renderer;
struct Timer;
struct Renderer_2D;

// Tokens
// Functions
void syntax_editor_initialize(Rendering_Core* rendering_core, Text_Renderer* text_renderer, Renderer_2D* renderer_2D, Input* input, Timer* timer);
void syntax_editor_destroy();
void syntax_editor_update();
void syntax_editor_render();
















