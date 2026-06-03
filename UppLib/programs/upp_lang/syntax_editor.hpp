#pragma once

#include "../../datastructures/allocators.hpp"
#include "../../datastructures/string.hpp"
#include "../../math/vectors.hpp"
#include "code_history.hpp"

struct Rendering_Core;
struct Input;
struct Text_Renderer;
struct Renderer_2D;
struct Window;
struct Source_Code;
struct Code_Change;
struct Editor_Tab;
struct Source_Breakpoint;

namespace Text_Editing
{
    void update_editor_data_after_line_insert(Editor_Tab* tab, int line_index);
    void update_editor_data_after_line_delete(Editor_Tab* tab, int line_index);
}

struct Code_Fold
{
    ibox1 interval;
	int child_folds_start; // -1 If no child
    int next_index;

    static Code_Fold make(ibox1 interval);
    bool contains(int line_index);
};

struct Line_Breakpoint
{
    int line_number;
    Source_Breakpoint* src_breakpoint; 
    bool enabled; // This is toggle via UI, but I'm not sure if it's already implemented
};

struct Editor_Tab
{
	String filepath;
    Source_Code* code; // Note: This is different than the code in the compilation unit
    Code_History history;
	Arena arena;

    History_Timestamp last_code_info_synch;
    History_Timestamp last_compiler_synchronized;
    int last_code_completion_info_index;
    Text_Index last_code_completion_query_pos;
    bool requires_recompile; // E.g. when loaded from a file, or otherwise modified

	// Folds:
	//  * 'Active' Folds are sorted by line-starts in the folds array
	//  * Folds can be hierarchical, and child folds are stored in the child-folds array
	//  * Folds cannot overlap and they cannot be directly next to one another
    DynArray<Code_Fold> folds;
    DynArray<Code_Fold> child_folds;
	int next_free_child_fold; // Free-list of child folds in the child-folds array, if none available -1

    DynArray<Line_Breakpoint> breakpoints;

    // Cursor and camera
    Text_Index cursor;
    int cam_start;
    bool move_cursor_horizontal_on_vertical_movement;
    bool move_to_horizontal_start;

    History_Timestamp last_render_timestamp;
    Text_Index last_render_cursor_pos;

	// Note: Jumps are mostly for line-jumping, but we also store the cursor character at the time of the jump,
	//		but this character does not change if line changes are made, so the index should be sanitized before being used
    DynArray<Text_Index> jump_list;
    int last_jump_index;
};




// Functions
void syntax_editor_initialize(Text_Renderer* text_renderer, Renderer_2D* renderer_2D, Window* window, Input* input);
void syntax_editor_destroy();
void syntax_editor_update(bool& animations_running);
void syntax_editor_render();

void syntax_editor_save_state(String file_path);
void syntax_editor_load_state(String file_path);
















