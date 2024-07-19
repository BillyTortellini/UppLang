#pragma once

#include "../math/umath.hpp"
#include "../datastructures/string.hpp"
#include "../rendering/basic2D.hpp"


/*
TODO:
 * Drop-Down Menu/Pop-Up window -> Probably just add a new element to root thingy with topmost z-index which disappears after focus lost
 * Integer/Float input
 * Toggle-able seperator (Section seperator)

Extra Features that I may want to have sometime in the future:
 * Line-Rendering for graphs
 * Fill-Weight so that components keep a specific ratio to one another
 * Tab-able input focus (E.g. move from input to input with tab), so it's easier to add new things
 * Copy and paste for text/number input
 * Better text handling, e.g. cursor movement, mouse selection, typical shortcuts, copy and paste, undo/redo
 * Text area for large text, multiple lines/line-wrapping
 * UI-Style which determines colors/sizes of predefined objects like windows, buttons...
 * Images and Icon rendering
 * Simple Plotting (bar-charts, line-plots, scatter-plot)
 * More padding options (E.g. border option so that cut-off can handle padding correctly...)
*/

// Types and forward definitions
struct Text_Renderer;
struct Window;
struct GUI_Size;
struct GUI_Drawable;
struct Render_Pass;

enum class GUI_Stack_Direction {
    LEFT_TO_RIGHT,
    RIGHT_TO_LEFT,
    TOP_TO_BOTTOM,
    BOTTOM_TO_TOP,
};

enum class GUI_Alignment
{
    MIN, // in x this is left_aligned, in y bottom aligned
    MAX,
    CENTER,
};

typedef void (*gui_userdata_destroy_fn)(void* userdata);



void gui_initialize(Text_Renderer* text_renderer, Window* window);
void gui_destroy();
void gui_update_and_render(Render_Pass* render_pass); // Note: Requires alpha blended pipeline state!

struct GUI_Handle
{
    int index;

    bool first_time_created;
    void* userdata;

    bool mouse_hover;
    bool mouse_hovers_child;
    bool has_mouse_wheel_input;
};

GUI_Handle gui_add_node(GUI_Handle parent_handle, GUI_Size size_x, GUI_Size size_y, GUI_Drawable drawable);
void gui_matching_add_checkpoint_int(int value);
void gui_matching_add_checkpoint_void_ptr(void* ptr);
void gui_matching_add_checkpoint_name(const char* name);

GUI_Handle gui_root_handle();
void gui_node_set_position_fixed(GUI_Handle handle, vec2 offset, Anchor anchor, bool relative_to_window = false);
void gui_node_set_alignment(GUI_Handle handle, GUI_Alignment alignment = GUI_Alignment::MIN);
void gui_node_set_layout(
    GUI_Handle handle, 
    GUI_Stack_Direction stack_direction = GUI_Stack_Direction::TOP_TO_BOTTOM, 
    GUI_Alignment child_default_alignment = GUI_Alignment::MIN,
    vec2 child_offset = vec2(0,0));
void gui_node_set_layout_child_offset(GUI_Handle handle, vec2 child_offset);
void gui_node_set_padding(GUI_Handle handle, int x_padding, int y_padding, bool pad_between_children = false);
void gui_node_update_drawable(GUI_Handle handle, GUI_Drawable drawable);
void gui_node_update_size(GUI_Handle handle, GUI_Size size_x, GUI_Size size_y);
void gui_node_set_z_index(GUI_Handle handle, int z_index);
void gui_node_set_z_index_to_highest(GUI_Handle handle);
void gui_node_enable_input(GUI_Handle handle);
void gui_node_set_focus(GUI_Handle handle);
void gui_node_remove_focus(GUI_Handle handle);
bool gui_node_has_focus(GUI_Handle handle);
void gui_node_hide(GUI_Handle handle);
void gui_node_keep_children_even_if_not_referenced(GUI_Handle handle);
void gui_node_set_userdata(GUI_Handle& handle, void* userdata, gui_userdata_destroy_fn destroy_fn);
Bounding_Box2 gui_node_get_previous_frame_box(GUI_Handle handle);
vec2 gui_node_get_previous_frame_min_child_size(GUI_Handle handle);



struct GUI_Size
{
    int min_size;
    bool fit_at_least_children; // If set, grows to accomodate min size of children...

    // Fill options;
    bool fill; // Fill up parent, if multiple children fill up parent, the available size is split
    bool inherit_fill_from_children; // If a child also wants to fill, this will propagate up
    int preferred_size; // Set to 0 to disable, will be ignored if < min_size
};

GUI_Size gui_size_make(int min_size, bool fit_at_least_children, bool fill, bool inherit_fill_from_children, int preferred_size);
GUI_Size gui_size_make_fit(bool inherit_fill = false);
GUI_Size gui_size_make_fixed(float value);
GUI_Size gui_size_make_fill(bool fit_at_least_children = true);
GUI_Size gui_size_make_preferred(int preferred, int min = 0, bool fit_at_least_children = true);

enum class GUI_Drawable_Type
{
    RECTANGLE,
    TEXT,
    NONE, // Just a container for other items (Usefull for layout)
};

struct GUI_Drawable
{
    GUI_Drawable_Type type;
    String text;
    vec4 color;

    // Rectangle Options
    vec4 border_color;
    int border_thickness;
    int edge_radius;
};

GUI_Drawable gui_drawable_make_none();
GUI_Drawable gui_drawable_make_text(String text, vec4 color = vec4(0.0f, 0.0f, 0.0f, 1.0f));
GUI_Drawable gui_drawable_make_rect(vec4 color, int border_thickness = 0, vec4 border_color = vec4(0, 0, 0, 1), int edge_radius = 0);
void gui_drawable_destroy(GUI_Drawable& drawable);


// Helper methods
template<typename T>
T* gui_store_primitive(GUI_Handle parent_handle, T default_value) {
    auto node_handle = gui_add_node(parent_handle, gui_size_make_fixed(0.0f), gui_size_make_fixed(0.0f), gui_drawable_make_none());
    gui_node_hide(node_handle);
    if (node_handle.userdata == 0) {
        T* new_value = new T;
        *new_value = default_value;
        gui_node_set_userdata(
            node_handle, (void*)new_value,
            [](void* data) -> void {
                T* typed_data = (T*)data;
                delete typed_data;
            }
        );
        return new_value;
    }
    return (T*)node_handle.userdata;
}

String* gui_store_string(GUI_Handle parent_handle, const char* initial_string);



// PREDEFINED OBJECTS
GUI_Handle gui_push_text(GUI_Handle parent_handle, String text, float text_height_cm = .4f, vec4 color = vec4(0.0f, 0.0f, 0.0f, 1.0f));
GUI_Handle gui_push_scroll_area(GUI_Handle parent_handle, GUI_Size size_x, GUI_Size size_y);
GUI_Handle gui_push_window(GUI_Handle parent_handle, const char* name);
bool gui_push_button(GUI_Handle parent_handle, String text);
void gui_push_text_edit(GUI_Handle parent_handle, String* string, float text_height_cm = 0.4f);
bool gui_push_toggle(GUI_Handle parent_handle, bool* value);
GUI_Handle gui_push_text_description(GUI_Handle parent_handle, const char* text);

void gui_push_example_gui();

