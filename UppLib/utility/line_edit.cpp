#include "line_edit.hpp"
#include "character_info.hpp"

Line_Editor line_editor_make()
{
    Line_Editor editor;
    editor.pos = 0;
    editor.select_start = 0;
    return editor;
}

bool line_editor_feed_key_message(Line_Editor& editor, String* text, Key_Message msg)
{
    if (!msg.key_down) {
        return false;
    }

    // Sanitize positions
    auto& pos = editor.pos;
    auto& select_start = editor.select_start;
    pos = math_clamp(pos, 0, text->size);
    select_start = math_clamp(select_start, 0, text->size);

    auto move_next_word = [](String* text, int pos, bool forwards) -> int
    {
        if (forwards) {
            if (pos >= text->size) return pos;
        }
        else {
            if (pos - 1 < 0) return 0;
        }

        auto char_type_equals = [](char a, char b) -> bool {
            if (char_is_valid_identifier(a)) {
                return char_is_valid_identifier(b);
            }
            else if (a == ' ') {
                return b == ' ';
            }
            else {
                return b != ' ' && !char_is_valid_identifier(b);
            }
        };

        int i = pos;
        if (!forwards) {
            i = i - 1;
        }
        char c = text->characters[i];

        // Goto next non-valid identifier
        while (i < text->size && i >= 0) 
        {
            char ic = text->characters[i];
            if (char_is_valid_identifier(c)) {
                if (!char_is_valid_identifier(ic)) {
                    break;
                }
            }
            else if (c == ' ') {
                if (ic != ' ') {
                    break;
                }
            }
            else {
                if (ic == ' ' || char_is_valid_identifier(ic)) {
                    break;
                }
            }
            i += forwards ? 1 : -1;
        }

        if (forwards) {
            return i;
        }
        else {
            if (i < 0) {
                return 0;
            }
            return i + 1;
        }
    };

    // Handle ctrl-messages
    if (msg.ctrl_down)
    {
        switch (msg.key_code)
        {
        case Key_Code::U: {
            if (pos <= 0) return false;
            string_remove_substring(text, 0, math_maximum(pos, select_start));
            pos = 0;
            select_start = 0;
            return true;
        }
        case Key_Code::BACKSPACE: 
        case Key_Code::W:
        {
            int i = move_next_word(text, pos, false);
            if (i != pos) {
                string_remove_substring(text, i, pos);
                pos = i;
                select_start = i;
                return true;
            }
            return false;
        }
        case Key_Code::A: {
            select_start = 0;
            pos = text->size;
            return false;
        }
        }
    }

    // Handle editor messages
    switch (msg.key_code)
    {
    case Key_Code::ARROW_RIGHT:
    case Key_Code::ARROW_LEFT: 
    {
        int new_pos = pos;
        if (msg.ctrl_down) {
            new_pos = move_next_word(text, pos, msg.key_code == Key_Code::ARROW_RIGHT);
        }
        else {
            new_pos = pos + (msg.key_code == Key_Code::ARROW_RIGHT ? 1 : -1);
        }
        new_pos = math_clamp(new_pos, 0, text->size);

        if (msg.shift_down) {
            pos = new_pos;
        }
        else 
        {
            // If we have a selection goto end of selection
            if (pos != select_start) {
                int min = math_minimum(pos, select_start);
                int max = math_maximum(pos, select_start);
                if (msg.key_code == Key_Code::ARROW_RIGHT) {
                    new_pos = max;
                }
                else {
                    new_pos = min;
                }
            }

            pos = new_pos;
            select_start = pos;
        }
        return false;
    }
    case Key_Code::BACKSPACE: 
    {
        if (text->size == 0) return false;

        bool changed = false;
        if (select_start == pos) {
            if (pos > 0) {
                string_remove_character(text, pos - 1);
                pos -= 1;
                select_start -= 1;
                changed = true;
            }
        }
        else {
            int start = math_minimum(pos, select_start);
            int end = math_maximum(pos, select_start);
            string_remove_substring(text, start, end);
            pos = start;
            select_start = start;
            changed = true;
        }
        return changed;
    }
    }

    if (msg.character >= ' ')
    {
        bool changed = false;
        // Delete selection if existing
        if (select_start != pos) {
            int start = math_minimum(pos, select_start);
            int end = math_maximum(pos, select_start);
            string_remove_substring(text, start, end);
            pos = start;
            select_start = start;
            changed = true;
        }
        if (msg.character == '\t') {
            string_insert_character_before(text, ' ', pos);
            string_insert_character_before(text, ' ', pos);
            string_insert_character_before(text, ' ', pos);
            string_insert_character_before(text, ' ', pos);
            pos += 4;
            select_start += 4;
            changed = true;
            return changed;
        }
        else if (msg.character == '\n' || msg.character == '\r') {
            return false;
        }

        string_insert_character_before(text, msg.character, pos);
        changed = true;
        pos += 1;
        select_start += 1;

        return changed;
    }

    return false;
}

