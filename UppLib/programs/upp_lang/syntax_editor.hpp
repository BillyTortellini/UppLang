#pragma once

#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/string.hpp"
#include "../../math/vectors.hpp"

struct Rendering_Core;
struct Input;
struct Text_Renderer;
struct Renderer_2D;
struct Window;

// Functions
void syntax_editor_initialize(Text_Renderer* text_renderer, Renderer_2D* renderer_2D, Window* window, Input* input);
void syntax_editor_destroy();
void syntax_editor_update(bool& animations_running);
void syntax_editor_render();

void syntax_editor_save_state(String file_path);
void syntax_editor_load_state(String file_path);
















