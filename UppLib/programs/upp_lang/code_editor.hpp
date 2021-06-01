#pragma once

#include "text_editor.hpp"
#include "compiler.hpp"

struct Rendering_Core;

struct Code_Editor
{
    Text_Editor* text_editor;
    Compiler compiler;
};

Code_Editor code_editor_create(Text_Renderer* text_renderer, Rendering_Core* core);
void code_editor_destroy(Code_Editor* editor);
void code_editor_update(Code_Editor* editor, Input* input, double time);
void code_editor_render(Code_Editor* editor, Rendering_Core* core, BoundingBox2 editor_box);