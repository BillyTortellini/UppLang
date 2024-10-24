#pragma once

#include "../datastructures/string.hpp"
#include "../datastructures/dynamic_array.hpp"
#include "../win32/input.hpp"

/*
Improvements:
 * History with Ctrl-Z + Shift-Ctrl-Z
 * Copy-Paste with Ctrl-C + Ctrl-V
 * Multiple line editing
*/

struct Line_Editor
{
    int pos;
    int select_start;
};

Line_Editor line_editor_make();
bool line_editor_feed_key_message(Line_Editor& editor, String* text, Key_Message msg);
