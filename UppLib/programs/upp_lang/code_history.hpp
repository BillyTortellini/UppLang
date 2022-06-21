#pragma once

#include "../../datastructures/string.hpp"
#include "../../datastructures/dynamic_array.hpp"

// Forward Declarations
struct Source_Code;

// Code History
enum class Code_Change_Type
{
    LINE_INSERT,
    INDENTATION_CHANGE,
    TEXT_INSERT,
};

struct Code_Change
{
    Code_Change_Type type;
    bool reverse_effect;
    int line_index;
    union
    {
        struct {
            int indentation;
        } line_insert;
        struct {
            int old_indentation;
            int new_indentation;
        } indentation_change;
        struct {
            String text;
            int char_start;
        } text_insert;
    } options;
};



enum class History_Node_Type
{
    NORMAL,
    COMPLEX_START,
    COMPLEX_END,
};

struct History_Node
{
    History_Node_Type type;

    // Payload
    Code_Change change;

    // Linkage to other nodes
    int next_change;
    int alt_change; // For 'other' history-path
    int prev_change;

    // Complex info
    int complex_partner;
};

struct Code_History
{
    Source_Code* code;
    Dynamic_Array<History_Node> nodes;
    int current;

    int complex_level;
    int complex_start;
};

Code_History code_history_create(Source_Code* code);
void code_history_reset(Code_History* history);
void code_history_destroy(Code_History* history);

void history_undo(Code_History* history);
void history_redo(Code_History* history);

void history_insert_line(Code_History* history, int line_index, int indentation);
void history_remove_line(Code_History* history, int line_index);
void history_insert_text(Code_History* history, int line_index, int char_index, String string);
void history_delete_text(Code_History* history, int line_index, int char_start, int char_end);
void history_insert_char(Code_History* history, int line_index, int char_index, char c);
void history_delete_char(Code_History* history, int line_index, int char_index);
void history_change_indentation(Code_History* history, int line_index, int new_indentation);
void history_start_complex_command(Code_History* history);
void history_stop_complex_command(Code_History* history);

void history_reconstruct_line_from_tokens(Code_History* history, int line_index);


