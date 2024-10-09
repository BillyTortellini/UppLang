#pragma once

#include "../../datastructures/string.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashset.hpp"

#include "source_code.hpp"

// Forward Declarations
struct Source_Code;

// Code History
enum class Code_Change_Type
{
    LINE_INSERT,
    CHAR_INSERT,
    TEXT_INSERT, // Note: Text insert on a single line, so no \n
    LINE_INDENTATION_CHANGE,
};

struct Code_Change
{
    Code_Change_Type type;
    bool apply_forwards;
    union
    {
        struct {
            int line_index;
            int indentation;
        } line_insert;
        struct {
            int line_index;
            int old_indentation;
            int new_indentation;
        } indentation_change;
        struct {
            Text_Index index;
            char c;
        } char_insert;
        struct {
            Text_Index index;
            String text;
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

    // Cursor info
    Optional<Text_Index> cursor_index;
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

void history_undo(Code_History* history); // Returns appropriate cursor position
void history_redo(Code_History* history);

void history_start_complex_command(Code_History* history);
void history_stop_complex_command(Code_History* history);

void history_set_cursor_pos(Code_History* history, Text_Index cursor);
Optional<Text_Index> history_get_cursor_pos(Code_History* history);

// Change Interface
void history_insert_text(Code_History* history, Text_Index index, String string);
void history_delete_text(Code_History* history, Text_Index index, int char_end);
void history_insert_char(Code_History* history, Text_Index index, char c);
void history_delete_char(Code_History* history, Text_Index index);

void history_insert_line(Code_History* history, int line_index, int indentation);
void history_insert_line_with_text(Code_History* history, int line_index, int indentation, String string);
void history_remove_line(Code_History* history, int line_index);
void history_change_indent(Code_History* history, int line_index, int new_indent);



// Timestamps
struct History_Timestamp
{
    int node_index;
};

History_Timestamp history_get_timestamp(Code_History* history);
void history_get_changes_between(Code_History* history, History_Timestamp start, History_Timestamp end, Dynamic_Array<Code_Change>* changes);
