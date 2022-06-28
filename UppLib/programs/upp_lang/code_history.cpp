#include "code_history.hpp"

#include "source_code.hpp"

// Code History
void code_change_destroy(Code_Change* change)
{
    if (change->type == Code_Change_Type::TEXT_INSERT) {
        string_destroy(&change->options.text_insert.text);
    }
}

Code_History code_history_create(Source_Code* code)
{
    Code_History result;
    result.nodes = dynamic_array_create_empty<History_Node>(1);
    result.code = code;
    code_history_reset(&result);
    return result;
}

void code_history_reset(Code_History* history)
{
    dynamic_array_reset(&history->nodes);
    {
        History_Node root;
        root.type = History_Node_Type::NORMAL;
        root.prev_change = -1;
        root.next_change = -1;
        root.alt_change = -1;
        root.complex_partner = -1;
        dynamic_array_push_back(&history->nodes, root);
    }
    history->current = 0;
    history->complex_level = 0;
    history->complex_start = -1;
}

void code_history_destroy(Code_History* history)
{
    for (int i = 0; i < history->nodes.size; i++) {
        code_change_destroy(&history->nodes[i].change);
    }
    dynamic_array_destroy(&history->nodes);
}

void source_code_destroy_line(Source_Line* line) {
    string_destroy(&line->text);
    dynamic_array_destroy(&line->tokens);
    dynamic_array_destroy(&line->infos);
}

void code_change_apply(Code_History* history, Code_Change* change, bool forwards)
{
    bool apply_change_forward = forwards;
    if (change->reverse_effect) {
        apply_change_forward = !apply_change_forward;
    }

    auto code = history->code;
    auto& lines = code->lines;
    int line_index = change->line_index;
    assert(line_index >= 0 && line_index <= lines.size, "");

    switch (change->type)
    {
    case Code_Change_Type::LINE_INSERT: 
    {
        auto& insert = change->options.line_insert;
        if (apply_change_forward) 
        {
            Source_Line line;
            line.indentation = insert.indentation;
            line.text = string_create_empty(4);
            line.tokens = dynamic_array_create_empty<Token>(1);
            line.infos = dynamic_array_create_empty<Render_Info>(1);
            dynamic_array_insert_ordered(&lines, line, line_index);
        }
        else
        {
            auto& line = lines[line_index];
            source_code_destroy_line(&line);
            dynamic_array_remove_ordered(&lines, line_index);
        }
        break;
    }
    case Code_Change_Type::INDENTATION_CHANGE:
    {
        auto& indent_change = change->options.indentation_change;
        lines[line_index].indentation = apply_change_forward ? indent_change.new_indentation : indent_change.old_indentation;
        break;
    }
    case Code_Change_Type::TEXT_INSERT:
    {
        auto& insert = change->options.text_insert;
        auto& line = lines[line_index];
        if (apply_change_forward) {
            string_insert_string(&line.text, &insert.text, insert.char_start);
        }
        else  {
            string_remove_substring(&line.text, insert.char_start, insert.char_start + insert.text.size);
        }
        break;
    }
    default: panic("");
    }
}

void history_insert_and_apply_change(Code_History* history, Code_Change change)
{
    int change_index = history->nodes.size;
    History_Node node;
    node.change = change;
    node.next_change = -1;
    node.prev_change = history->current;
    node.alt_change = -1;
    node.type = History_Node_Type::NORMAL;
    node.complex_partner = -1;

    auto& current_node = history->nodes[history->current];
    if (current_node.next_change != -1) {
        node.alt_change = current_node.next_change;
    }
    current_node.next_change = change_index;

    history->current = history->nodes.size;
    dynamic_array_push_back(&history->nodes, node);
    code_change_apply(history, &node.change, true);
}

void history_undo(Code_History* history)
{
    assert(history->complex_level == 0, "Cannot undo/redo inside a complex command");
    if (history->current == 0) return; // Base node

    auto node = &history->nodes[history->current];
    switch (node->type)
    {
    case History_Node_Type::COMPLEX_START: panic("Should not happen");
    case History_Node_Type::NORMAL: {
        code_change_apply(history, &node->change, false);
        history->current = node->prev_change;
        break;
    }
    case History_Node_Type::COMPLEX_END: 
    {
        // Apply all commands in reverse until we get to the start
        assert(node->complex_partner > 0, "Complex must be finished here");
        int goto_index = node->complex_partner;
        while (history->current != goto_index) 
        {
            assert(history->current != 0, "");
            node = &history->nodes[history->current];
            code_change_apply(history, &node->change, false);
            history->current = node->prev_change;
        }

        assert(history->current != 0, "Complex command cannot start with the base node!");
        node = &history->nodes[history->current];
        code_change_apply(history, &node->change, false);
        history->current = node->prev_change;
        break;
    }
    default:panic("");
    }
}

void history_redo(Code_History* history)
{
    assert(history->complex_level == 0, "Cannot undo/redo inside a complex command");

    auto node = &history->nodes[history->current];
    if (node->next_change == -1) return;
    history->current = node->next_change;
    node = &history->nodes[history->current];

    switch (node->type)
    {
    case History_Node_Type::COMPLEX_END: panic("Shouldn't happen");
    case History_Node_Type::NORMAL: {
        code_change_apply(history, &node->change, true);
        break;
    }
    case History_Node_Type::COMPLEX_START:
    {
        // Apply all commands until we get to the end
        assert(node->complex_partner != -1, "Complex must be finished here");
        int goto_index = node->complex_partner;
        while (history->current != goto_index)
        {
            assert(history->current != 0, "");
            code_change_apply(history, &node->change, true);
            history->current = node->next_change;
            node = &history->nodes[history->current];
        }
        // Apply the latest change
        code_change_apply(history, &node->change, true);
        break;
    }
    default:panic("");
    }
}

void history_start_complex_command(Code_History* history)
{
    assert(history->complex_level >= 0, "");
    if (history->complex_level == 0) {
        history->complex_start = history->current;
    }
    history->complex_level += 1;
}

void history_stop_complex_command(Code_History* history)
{
    assert(history->complex_level > 0, "");
    history->complex_level -= 1;
    if (history->complex_level > 0) return;

    int start_node_index = history->nodes[history->complex_start].next_change;
    // Recorded complex commands with 0 or 1 entries should be ignored
    if (start_node_index == -1 || start_node_index == history->current) return;

    auto& node_start = history->nodes[start_node_index];
    auto& node_end = history->nodes[history->current];

    node_start.type = History_Node_Type::COMPLEX_START;
    node_start.complex_partner = history->current;
    node_end.type = History_Node_Type::COMPLEX_END;
    node_end.complex_partner = start_node_index;
}



// Change helpers
Code_Change code_change_create_empty(Code_Change_Type type, int line_index, bool reverse_effect)
{
    Code_Change result;
    result.type = type;
    result.line_index = line_index;
    result.reverse_effect = reverse_effect;
    return result;
}

void history_insert_line(Code_History* code, int line_index, int indentation)
{
    auto change = code_change_create_empty(Code_Change_Type::LINE_INSERT, line_index, false);
    change.options.line_insert.indentation = indentation;
    history_insert_and_apply_change(code, change);
}

void history_remove_line(Code_History* history, int line_index)
{
    auto& code = history->code;
    assert(line_index >= 0 && line_index < code->lines.size, "");
    auto& line = code->lines[line_index];

    history_start_complex_command(history);
    if (line.text.size != 0) {
        history_delete_text(history, line_index, 0, line.text.size);
    }
    auto change = code_change_create_empty(Code_Change_Type::LINE_INSERT, line_index, true);
    change.options.line_insert.indentation = history->code->lines[line_index].indentation;
    history_insert_and_apply_change(history, change);
    history_stop_complex_command(history);
}

void history_change_indentation(Code_History* history, int line_index, int new_indentation)
{
    auto& code = history->code;
    auto change = code_change_create_empty(Code_Change_Type::INDENTATION_CHANGE, line_index, false);
    change.options.indentation_change.new_indentation = new_indentation;
    change.options.indentation_change.old_indentation = code->lines[line_index].indentation;
    history_insert_and_apply_change(history, change);
}

void history_insert_text(Code_History* history, int line_index, int char_index, String string)
{
    auto& code = history->code;
    assert(line_index >= 0 && line_index < code->lines.size, "");
    auto& line = code->lines[line_index];
    assert(char_index >= 0 && char_index <= line.text.size, "");

    auto change = code_change_create_empty(Code_Change_Type::TEXT_INSERT, line_index, false);
    change.options.text_insert.char_start = char_index;
    change.options.text_insert.text = string_copy(string);
    history_insert_and_apply_change(history, change);
}

void history_delete_text(Code_History* history, int line_index, int char_start, int char_end)
{
    auto& code = history->code;
    assert(line_index >= 0 && line_index < code->lines.size, "");
    auto& line = code->lines[line_index];
    assert(char_start >= 0 && char_start <= line.text.size, "");
    assert(char_end >= 0 && char_end <= line.text.size, "");
    assert(char_start < char_end, "");

    auto change = code_change_create_empty(Code_Change_Type::TEXT_INSERT, line_index, true);
    change.options.text_insert.char_start = char_start;
    change.options.text_insert.text = string_create_substring(&line.text, char_start, char_end);
    history_insert_and_apply_change(history, change);
}

void history_insert_char(Code_History* history, int line_index, int char_index, char c)
{
    char buffer[2] = { c, '\0' };
    history_insert_text(history, line_index, char_index, string_create_static_with_size(buffer, 1));
}

void history_delete_char(Code_History* history, int line_index, int char_index) {
    history_delete_text(history, line_index, char_index, char_index + 1);
}



// Timestamps
History_Timestamp history_get_timestamp(Code_History* history)
{
    History_Timestamp result;
    result.node_index = history->current;
    return result;
}

void history_get_changes_between(Code_History* history, History_Timestamp start_stamp, History_Timestamp end_stamp, Dynamic_Array<Code_Change>* changes)
{
    int start = start_stamp.node_index;
    int end = end_stamp.node_index;

    // Info: This is a modifier Breadth-First search, because of the tree structure we only visited each node from exactly one previous node
    //      Also, in this search, we search from the End to the start, so we don't have to reverse the path once we found it
    auto goto_index = array_create_empty<int>(history->nodes.size);
    SCOPE_EXIT(array_destroy(&goto_index));

    // Find path
    {
        auto layer_nodes = dynamic_array_create_empty<int>(history->nodes.size);
        SCOPE_EXIT(dynamic_array_destroy(&layer_nodes));
        int current_layer_start = 0;

        // Add inital node
        dynamic_array_push_back(&layer_nodes, end);
        goto_index[end] = end;

        // Start search
        bool found = false;
        while (!found)
        {
            int next_layer_start = layer_nodes.size;
            assert(current_layer_start != next_layer_start, "Each layer must have some nodes");
            for (int i = current_layer_start; i < layer_nodes.size; i++)
            {
                auto node_index = layer_nodes[i];
                int from_index = goto_index[node_index];
                auto& node = history->nodes[node_index];
                if (node_index == start) {
                    found = true;
                    break;
                }

                // Add neighbors to next layer
                if (node.prev_change != -1 && node.prev_change != from_index) {
                    dynamic_array_push_back(&layer_nodes, node.prev_change);
                    goto_index[node.prev_change] = node_index;
                }
                if (node.next_change != -1) 
                {
                    // Add all possible future paths
                    int future_path_index = node.next_change;
                    while (future_path_index != -1)
                    {
                        if (future_path_index != from_index) {
                            dynamic_array_push_back(&layer_nodes, future_path_index);
                            goto_index[future_path_index] = node_index;
                        }
                        future_path_index = history->nodes[future_path_index].alt_change;
                    }
                }
            }
            current_layer_start = next_layer_start;
        }
    }

    // Reconstruct Log (from goto-indices)
    dynamic_array_reset(changes);
    {
        int index = start;
        while (index != end)
        {
            int next = goto_index[index];
            auto& node = history->nodes[index];
            auto& next_node = history->nodes[next];
            if (next == node.prev_change) {
                // To go backwards i need to revert the current change
                Code_Change copy = node.change;
                copy.reverse_effect = !copy.reverse_effect;
                dynamic_array_push_back(changes, copy);
            }
            else {
                // To go forwards i need to apply the next change
                dynamic_array_push_back(changes, next_node.change);
            }
            index = next;
        }
    }
}
