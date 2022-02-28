#pragma once

#include "../../utility/bounding_box.hpp"
#include "../../math/vectors.hpp"
#include "../../datastructures/string.hpp"

struct Rendering_Core;
struct Timer;
struct Compiler;
struct Text_Editor;
struct Input;
struct Text_Renderer;

struct Code_Editor
{
    Text_Editor* text_editor;
    Compiler* compiler;

    bool show_context_info;
    String context_info;
    vec2 context_info_pos;
};

Code_Editor code_editor_create(Text_Renderer* text_renderer, Rendering_Core* core, Timer* timer);
void code_editor_destroy(Code_Editor* editor);
void code_editor_update(Code_Editor* editor, Input* input, double time);
void code_editor_render(Code_Editor* editor, Rendering_Core* core, Bounding_Box2 editor_box);