#include "code_history.hpp"

#include "source_code.hpp"



// Helpers
void code_change_destroy(Code_Change* change)
{
    if (change->type == Code_Change_Type::TEXT_INSERT) {
        string_destroy(&change->options.text_insert.text);
    }
}

Block_Index code_history_add_block(Code_History* history, Block_Index parent, int line_index)
{
    Block_Index new_index;
    if (history->free_blocks.size > 0) {
        new_index =  dynamic_array_remove_last(&history->free_blocks);
    }
    else {
        new_index = block_index_make(history->code, dynamic_array_push_back_dummy(&history->code->blocks));
    }

    auto block = index_value(new_index);
    block->children = dynamic_array_create_empty<Block_Index>(1);
    block->lines = dynamic_array_create_empty<Source_Line>(1);
    block->line_index = line_index;
    block->parent = parent;

    // Insert ordered into parent
    auto parent_block = index_value(parent);
    int i = 0;
    while (i < parent_block->children.size)
    {
        auto child_block = index_value(parent_block->children[i]);
        if (line_index <= child_block->line_index) {
            break;
        }
        i++;
    }
    dynamic_array_insert_ordered(&parent_block->children, new_index , i);
    return new_index;
}

void code_history_remove_block_from_hierarchy(Code_History* history, Block_Index index)
{
    auto block = index_value(index);
    assert(index.block != 0, "Cannot free root block!");
    assert(block->children.size == 0 && block->lines.size == 0, "Cannot free non-empty block");
    source_block_destroy(block);
    dynamic_array_push_back(&history->free_blocks, index);
    // Remove from parent
    auto parent_block = index_value(block->parent);
    bool found = false;
    for (int i = 0; i < parent_block->children.size; i++) {
        if (parent_block->children[i].block == index.block) {
            dynamic_array_remove_ordered(&parent_block->children, i);
            found = true;
            break;
        }
    }
    assert(found, "");
}



// Code History
Code_History code_history_create(Source_Code* code)
{
    Code_History result;
    result.nodes = dynamic_array_create_empty<History_Node>(1);
    result.free_blocks = dynamic_array_create_empty<Block_Index>(1);
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
        root.cursor_index = optional_make_success(text_index_make(line_index_make(block_index_make_root(history->code), 0), 0));
        dynamic_array_push_back(&history->nodes, root);
    }
    dynamic_array_reset(&history->free_blocks);
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



// Code Changes
void code_change_apply(Code_History* history, Code_Change* change, bool forwards)
{
    bool apply_change_forward = forwards;
    if (change->reverse_effect) {
        apply_change_forward = !apply_change_forward;
    }

    auto code = history->code;
    switch (change->type)
    {
    case Code_Change_Type::LINE_INSERT:
    {
        auto& insert = change->options.line_insert;
        auto block = index_value(insert.block);
        if (apply_change_forward) {
            assert(insert.line >= 0 && insert.line <= block->lines.size, "");
            source_line_insert_empty(insert);
        }
        else {
            assert(insert.line >= 0 && insert.line < block->lines.size, "");
            source_line_destroy(&block->lines[insert.line]);
            dynamic_array_remove_ordered(&block->lines, insert.line);
        }

        // Move all follow blocks in block
        for (int i = 0; i < block->children.size; i++) 
        {
            auto child_block = index_value(block->children[i]);
            if (child_block->line_index > insert.line) {
                child_block->line_index += apply_change_forward ? 1 : -1;
            }
        }
        break;
    }
    case Code_Change_Type::BLOCK_CREATE:
    {
        auto& create = change->options.block_create;
        if (apply_change_forward) {
            create.new_block_index = code_history_add_block(history, create.parent, create.line_index);
        }
        else {
            auto block = index_value(create.new_block_index);
            create.line_index = block->line_index;
            create.parent = block->parent;
            code_history_remove_block_from_hierarchy(history, create.new_block_index);
        }
        break;
    }
    case Code_Change_Type::BLOCK_MERGE:
    {
        auto& merge = change->options.block_merge;
        if (apply_change_forward)
        {
            auto into_block = index_value(merge.index);
            auto parent_block = index_value(into_block->parent);

            // Find other block
            int merge_child_index = -1;
            for (int i = 0; i < parent_block->children.size; i++) {
                auto child_index = parent_block->children[i];
                if (child_index.block == merge.index.block) {
                    merge_child_index = i + 1;
                    break;
                }
            }
            assert(merge_child_index != -1, "");
            assert(merge_child_index < parent_block->children.size, "Next must be valid");
            merge.merge_other = parent_block->children[merge_child_index];
            auto other_block = index_value(merge.merge_other);
            assert(into_block->line_index == other_block->line_index, "Merge blocks must occupy the same line");

            // Save split old_line_index for reversing this change
            merge.split_index = into_block->lines.size;
            // Update block indices
            for (int i = 0; i < other_block->children.size; i++) {
                auto child_block = index_value(other_block->children[i]);
                child_block->line_index += into_block->lines.size;
            }
            // Move lines from one block to the other
            dynamic_array_append_other(&into_block->lines, &other_block->lines);
            dynamic_array_reset(&other_block->lines);
            // Move child blocks from one block to the other
            dynamic_array_append_other(&into_block->children, &other_block->children);
            dynamic_array_reset(&other_block->children);
            // Remove other block
            code_history_remove_block_from_hierarchy(history, merge.merge_other);
        }
        else
        {
            // Block split
            auto split_block = index_value(merge.index);
            merge.merge_other = code_history_add_block(history, split_block->parent, split_block->line_index);
            split_block = index_value(merge.index); // Refresh pointer, since adding can invalidate pointers to blocks
            auto other_block = index_value(merge.merge_other);

            // Copy blocks after split_index into new block
            assert(merge.split_index >= 0 && merge.split_index < split_block->lines.size, "");
            int block_cutoff = 0;
            for (int i = 0; i < split_block->children.size; i++) {
                auto child_block = index_value(split_block->children[i]);
                if (child_block->line_index >= merge.split_index) {
                    block_cutoff = i;
                    break;
                }
            }
            for (int i = block_cutoff; i < split_block->children.size; i++) {
                dynamic_array_push_back(&other_block->children, split_block->children[i]);
            }
            dynamic_array_rollback_to_size(&other_block->children, block_cutoff);
            // Copy lines to other block
            for (int i = merge.split_index; i < split_block->lines.size; i++) {
                dynamic_array_push_back(&other_block->lines, split_block->lines[i]);
            }
            dynamic_array_rollback_to_size(&other_block->lines, merge.split_index);
        }
        break;
    }
    case Code_Change_Type::BLOCK_INDEX_CHANGED:
    {
        auto& index_change = change->options.block_index_change;
        auto block = index_value(index_change.index);
        if (apply_change_forward) {
            index_change.old_line_index = block->line_index;
            block->line_index = index_change.new_line_index;
        }
        else {
            index_change.new_line_index = block->line_index;
            block->line_index = index_change.old_line_index;
        }
        break;
    }
    case Code_Change_Type::TEXT_INSERT:
    {
        auto& insert = change->options.text_insert;
        auto& text = index_value(insert.index.line)->text;
        if (apply_change_forward) {
            string_insert_string(&text, &insert.text, insert.index.pos);
        }
        else {
            string_remove_substring(&text, insert.index.pos, insert.index.pos + insert.text.size);
        }
        break;
    }
    default: panic("");
    }
}

int history_insert_and_apply_change(Code_History* history, Code_Change change)
{
    int change_index = history->nodes.size;
    History_Node node;
    node.change = change;
    node.next_change = -1;
    node.prev_change = history->current;
    node.alt_change = -1;
    node.type = History_Node_Type::NORMAL;
    node.complex_partner = -1;
    node.cursor_index = optional_make_failure<Text_Index>();

    auto& current_node = history->nodes[history->current];
    if (current_node.next_change != -1) {
        node.alt_change = current_node.next_change;
    }
    current_node.next_change = change_index;

    history->current = history->nodes.size;
    dynamic_array_push_back(&history->nodes, node);
    code_change_apply(history, &history->nodes[change_index].change, true);
    return change_index;
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

    source_code_sanity_check(history->code);
}

void history_set_cursor_pos(Code_History* history, Text_Index cursor)
{
    auto node = history->nodes[history->current];
    node.cursor_index = optional_make_success(cursor);
}

Optional<Text_Index> history_get_cursor_pos(Code_History* history)
{
    auto node = history->nodes[history->current];
    return node.cursor_index;
}




// Change helpers
// TEXT
Code_Change code_change_create_empty(Code_Change_Type type, bool reverse_effect)
{
    Code_Change result;
    result.type = type;
    result.reverse_effect = reverse_effect;
    return result;
}

void history_insert_text(Code_History* history, Text_Index index, String string)
{
    source_code_sanity_check(history->code);
    SCOPE_EXIT(source_code_sanity_check(history->code));

    auto line = index_value(index.line);
    auto change = code_change_create_empty(Code_Change_Type::TEXT_INSERT, false);
    change.options.text_insert.index = index;
    change.options.text_insert.text = string_copy(string);
    history_insert_and_apply_change(history, change);
}

void history_delete_text(Code_History* history, Text_Index index, int char_end)
{
    source_code_sanity_check(history->code);
    SCOPE_EXIT(source_code_sanity_check(history->code));

    auto& text = index_value(index.line)->text;
    assert(index.pos >= 0 && index.pos <= text.size, "");
    assert(char_end >= 0 && char_end <= text.size, "");
    assert(index.pos < char_end, "");

    auto change = code_change_create_empty(Code_Change_Type::TEXT_INSERT, true);
    change.options.text_insert.index = index;
    change.options.text_insert.text = string_create_substring(&text, index.pos, char_end);
    history_insert_and_apply_change(history, change);
}

void history_insert_char(Code_History* history, Text_Index index, char c)
{
    source_code_sanity_check(history->code);
    SCOPE_EXIT(source_code_sanity_check(history->code));

    char buffer[2] = { c, '\0' };
    history_insert_text(history, index, string_create_static_with_size(buffer, 1));
}

void history_delete_char(Code_History* history, Text_Index index) {
    source_code_sanity_check(history->code);
    SCOPE_EXIT(source_code_sanity_check(history->code));

    history_delete_text(history, index, index.pos + 1);
}



// LINES
void history_insert_line(Code_History* history, Line_Index index)
{
    history_start_complex_command(history);
    SCOPE_EXIT(history_stop_complex_command(history));

    auto change = code_change_create_empty(Code_Change_Type::LINE_INSERT, false);
    change.options.line_insert = index;
    history_insert_and_apply_change(history, change);
}

void history_insert_line_with_text(Code_History* history, Line_Index line_index, String string)
{
    history_insert_line(history, line_index);
    history_insert_text(history, text_index_make(line_index, 0), string);
}

void history_remove_line(Code_History* history, Line_Index line_index)
{
    history_start_complex_command(history);
    SCOPE_EXIT(history_stop_complex_command(history));

    auto block = index_value(line_index.block);

    auto change = code_change_create_empty(Code_Change_Type::LINE_INSERT, true);
    change.options.line_insert = line_index;
    history_insert_and_apply_change(history, change);

    // Merge blocks if 2 fall together
    for (int i = 0; i + 1 < block->children.size; i++) 
    {
        auto child_index = block->children[i];
        auto child = index_value(child_index);
        auto next_index = block->children[i + 1];
        auto next_child = index_value(next_index);
        if (next_child->line_index == child->line_index) 
        {
            auto merge = code_change_create_empty(Code_Change_Type::BLOCK_MERGE, false);
            merge.options.block_merge.index = child_index;
            merge.options.block_merge.merge_other = next_index;
            merge.options.block_merge.split_index = child->lines.size;
            history_insert_and_apply_change(history, merge);
            break; // We break here because removing a line can only result in 1 merge
        }
    }

    // Recursively remove blocks
    auto block_index = line_index.block;
    while (block_index.block != 0)
    {
        block = index_value(block_index);
        if (block->lines.size == 0 && block->children.size == 0) {
            auto remove_change = code_change_create_empty(Code_Change_Type::BLOCK_CREATE, true);
            remove_change.options.block_create.line_index = block->line_index;
            remove_change.options.block_create.parent = block->parent;
            remove_change.options.block_create.new_block_index = block_index;
            history_insert_and_apply_change(history, change);
        }
        else {
            break;
        }
        block_index = block->parent;
    }

    // Add empty line to root block if we deleted the last root line
    auto root_index = block_index_make_root(history->code);
    auto root_block = index_value(root_index);
    if (root_block->children.size == 0 && root_block->lines.size == 0) {
        auto change = code_change_create_empty(Code_Change_Type::LINE_INSERT, false);
        change.options.line_insert = line_index_make(root_index, 0);
        history_insert_and_apply_change(history, change);
    }
}

Line_Index history_add_line_indent(Code_History* history, Line_Index old_line_index)
{
    source_code_sanity_check(history->code);
    SCOPE_EXIT(source_code_sanity_check(history->code));

    history_start_complex_command(history);
    SCOPE_EXIT(history_stop_complex_command(history));

    // INFO: Adding an indent results in either a move into a previous/next block (+Eventual Merge) or a create block + move
    auto block = index_value(old_line_index.block);

    // Search if we are at the start/end of a block
    bool found_prev_block = false;
    bool found_next_block = false;
    Block_Index prev_block_index;
    Block_Index next_block_index;
    for (int i = 0; i < block->children.size; i++)
    {
        auto child_block = index_value(block->children[i]);
        if (old_line_index.line == child_block->line_index) {
            found_prev_block = true;
            prev_block_index = block->children[i];
        }
        else if (old_line_index.line + 1 == child_block->line_index) {
            found_next_block = true;
            next_block_index = block->children[i];
        }
    }

    Line_Index new_line_index;
    if (!found_prev_block && !found_next_block) {
        // Create new block
        auto change = code_change_create_empty(Code_Change_Type::BLOCK_CREATE, false);
        change.options.block_create.line_index = old_line_index.line;
        change.options.block_create.parent = old_line_index.block;
        int change_index = history_insert_and_apply_change(history, change);
        new_line_index.block = history->nodes[change_index].change.options.block_create.new_block_index;
        block = index_value(old_line_index.block); // Refresh because pointer may be invalid after inserting block
        new_line_index.line = 0;
    }
    else if (found_prev_block && found_next_block) {
        // Insert into prev block and merge blocks
        new_line_index.block = prev_block_index;
        new_line_index.line = index_value(next_block_index)->lines.size;
    }
    else {
        // Insert into the one block that was found
        if (found_next_block) {
            new_line_index.block = next_block_index;
            new_line_index.line = 0;
        }
        else if (found_prev_block) {
            new_line_index.block = prev_block_index;
            new_line_index.line = index_value(prev_block_index)->lines.size;
        }
        else {
            panic("Should be handled in case above, this is only to remove compiler warning");
        }
    }

    // Move line from one block to the other
    {
        history_insert_line_with_text(history, new_line_index, index_value(old_line_index)->text);
        history_remove_line(history, old_line_index);
    }

    if (found_prev_block && found_next_block) {
        auto merge = code_change_create_empty(Code_Change_Type::BLOCK_MERGE, false);
        merge.options.block_merge.index = new_line_index.block;
        history_insert_and_apply_change(history, merge);
    }

    return new_line_index;
}

Line_Index history_remove_line_indent(Code_History* history, Line_Index index)
{
    source_code_sanity_check(history->code);
    SCOPE_EXIT(source_code_sanity_check(history->code));

    // INFO: Remove indent results in either a move into a previous/next block (+Eventual Merge) or a Split block + move
    if (index.block.block == 0) return index; // Cannot remove line index in root block!
    auto block = index_value(index.block);

    // Search if we are at the start/end of the block
    bool at_block_start = false;
    bool at_block_end = false;
    if (index.line == 0) {
        at_block_start = true;
        if (block->children.size != 0 && index_value(block->children[0])->line_index == 0) {
            at_block_start = false;
        }
    }
    if (index.line == block->lines.size - 1) {
        at_block_end = true;
        if (block->children.size != 0 && index_value(dynamic_array_last(&block->children))->line_index == block->lines.size) {
            at_block_end = false;
        }
    }

    auto parent_block = index_value(block->parent);
    Line_Index new_line_pos = line_index_make(block->parent, block->line_index);
    bool move_block_after = false;
    Block_Index move_block_index;
    if (at_block_start) {
        move_block_after = true;
        move_block_index = index.block;
    }
    else if (at_block_end) {
        move_block_after = false;
        // Do nothing
    }
    else {
        new_line_pos = line_index_make(block->parent, block->line_index);

        // Split block at current line
        auto split = code_change_create_empty(Code_Change_Type::BLOCK_MERGE, true);
        split.options.block_merge.index = index.block;
        split.options.block_merge.split_index = index.line + 1;
        int change_index = history_insert_and_apply_change(history, split);

        move_block_after = true;
        move_block_index = history->nodes[change_index].change.options.block_merge.merge_other;

        // Update block pointers since split may invalidate references
        block = index_value(index.block);
        parent_block = index_value(block->parent);
    }

    // Move Line from block to block
    {
        history_insert_line_with_text(history, new_line_pos, index_value(index)->text);
        history_remove_line(history, index);
    }

    if (move_block_after)
    {
        auto move_block = index_value(move_block_index);
        auto move = code_change_create_empty(Code_Change_Type::BLOCK_INDEX_CHANGED, false);
        move.options.block_index_change.index = move_block_index;
        move.options.block_index_change.new_line_index = move_block->line_index + 1;
        move.options.block_index_change.new_line_index = move_block->line_index;
        history_insert_and_apply_change(history, move);
    }

    return new_line_pos;
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
