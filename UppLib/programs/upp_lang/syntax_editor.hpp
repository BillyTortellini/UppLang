#pragma once

/*
Plan: Rework Analyser

Semantic Analyser v2:
 - Uses Global Variable
 - Attaches Information to AST
 - Workload Discovery Phase
 - Get rid of Modtree and IR-Code

Parser TODO:
 - Integration/Rewrite of Semantic Analyser
 - Editor Error Display
 - Parser Error reporting
*/

/*
    Features I want:
     - Auto-Formating (Whitespaces + Auto-Wrapping, Auto Folding...)
     - Editing-Freedom (Delete wherever I want, insert wherever I want)
     - Code-Completion
     - Better Parser Errors (Display missing Gaps, possible continuations)
     - Level-Of-Detail (Folding)
     - Indentation-Based Navigation (+ other Code-Based navigations)
     - Code-Views (Dion-Slices, only display/edit a Slice of Code, e.g. only 1 Module)
     - Better Rendering (Smooth-Scrolling, Cursor-Movement Traces, Good Selection, Animated Editing)
     - Less input (Less Keystrokes for Space, Semicolon, {})
     - Transformative Copy-Paste (Requires Selection of valid source code)
     - Single Project-Files

    How I want to achieve this:
    By combining Editor, Lexer and Parser into one Entity

    Current Design:
        Blocks have Hierarchical-Representation
        Lines have Text-Representation
        Text gets auto-formated after each edit
        Formating can be done independent from Representation
*/

struct Rendering_Core;
struct Input;
struct Renderer_2D;
struct Text_Renderer;
struct Timer;

void syntax_editor_create(Rendering_Core* rendering_core, Text_Renderer* text_renderer, Renderer_2D* renderer_2D, Input* input, Timer* timer);
void syntax_editor_destroy();
void syntax_editor_update();
void syntax_editor_render();















