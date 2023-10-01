#include "render_rework.hpp"

#include "../../utility/utils.hpp"
#include "../../win32/timing.hpp"
#include "../../win32/window.hpp"
#include "../../rendering/rendering_core.hpp"
#include "../../rendering/texture.hpp"
#include "../../rendering/texture_bitmap.hpp"
#include "../../rendering/camera_controllers.hpp"
#include "../../utility/random.hpp"
#include "../../rendering/framebuffer.hpp"
#include "../../rendering/text_renderer.hpp"
#include "../../rendering/renderer_2d.hpp"
#include "../../rendering/basic2D.hpp"
#include <algorithm>
#include <iostream>

/*
Things to consider:
 - Focus and overlapping windows will be a big part of all this.
 - There is a tradeoff between doing layout right while the gui is building up,
    and layout after all components of the window are know. When doing the layout straight awaw
    you can query if the component has been pressed at the current position instantly.
    If you don't do this, you can create nicer layouts which calculate sizes of e.g. tables dynamically based on the largest
    column or something like that. But for input you then have to match the current components that are displayed with the
    components of the last frame, and if you cannot do that, you probably lose something like focus and schtuff
 - This probably means that while generating a new gui I keep up with the components that correspond to the stuff that we had last frame,
    and for queries I check the information (last frame mouse position and last frame focused thing) when looking for input or others.
 - What happens when there is no match between previous component and new ones? (Note that I probably want some sort of hierarchical system,
    for lists and things that can be made smaller or larger. Also for tables and stuff, which I probably want to be able to sort by schtuff...)
 - Being able to graph things would also be nice, but again, these are things that will be more interesting in the future, and I don't want to
    ruin stuff for myself. But maybe if I can insert custom rendercalls for a specifc area this would be somewhat easy?
 - This would be even cooler if I had hot reloading, because then I could change code on the fly, but this will probably be possible
    when my language is finished lol.
*/
// What the fuck is even going on right now, I need to think how to do input I guess
// So I need to have some input where I can match previous inputs with current ones...
/*
Do I even need 2 datastructures? E.g. building a new one and matching the old one?
--> I guess not, I think it would be easier if we just modify the existing one when updating:

Stages of IM-GUI:
 1. Frame start
    -> Maybe do some resetting, e.g. queried_this_frame and stuff (For error detection)
 2. Drawing commands (Like whatever)
    -> These cannot access the layout because the layout is determined at the end of the frame,
       but we need to know if a user pressed something, or there was a mouse-over or something along those lines
    -> Here I feel like we also need to know if something is newly generated (Because this invalidates all previous inputs)
        or if it is already generated, so matching needs to be done in one way or another...
        -> Maybe a hierarchy doesn't invalidate as much
        -> New values will never be found..., so no invalid matches would be nice
        -> Code positions would also be nice to know, but I guess this is hard without macros
 3. Frame end
    -> Now we have all components so we can calculate Layout values (Like maybe scrolling things, cutting things off),
        also do the depth hierarchy and schtuff like that.
    -> Here we also need to do user input I guess --> Go through event history, move windows (Drag-and-drop), click buttons
        String editing and schtuff. This needs to be saved so that when things are queried next frame they get the new values.
*/

struct GUI_Interval {
    int min;
    int max;
};

int float_to_nearest_int(float value) {
    return value >= 0.0f ? ((int)value + 0.5f) : ((int)value - 0.5f);
}

struct GUI_Box {
    GUI_Box() {}
    GUI_Box(Bounding_Box2 bb) {
        intervals[0].min = float_to_nearest_int(bb.min.x);
        intervals[0].max = float_to_nearest_int(bb.max.x);
        intervals[1].min = float_to_nearest_int(bb.min.y);
        intervals[1].max = float_to_nearest_int(bb.max.y);
    }

    GUI_Interval intervals[2];
    GUI_Interval& operator[](int index) {
        assert(index >= 0 && index < 3, "!");
        return intervals[index];
    }

    Bounding_Box2 as_bounding_box() {
        return bounding_box_2_make_min_max(vec2(intervals[0].min, intervals[1].min), vec2(intervals[0].max, intervals[1].max));
    }

    Optional<GUI_Box> intersect(const GUI_Box& other) {
        GUI_Box result;
        for (int dim = 0; dim < 2; dim++) {
            auto& a = intervals[dim];
            auto& b = other.intervals[dim];
            if (a.min > b.max || b.min > a.max) {
                return optional_make_failure<GUI_Box>();
            }
            result.intervals[dim].min = math_maximum(a.min, b.min);
            result.intervals[dim].max = math_minimum(a.max, b.max);
        }
        return optional_make_success(result);
    }

    bool overlaps(const GUI_Box& other) {
        return this->intersect(other).available;
    }

    bool is_point_inside(vec2 point) {
        return point.x >= intervals[0].min && point.x <= intervals[0].max &&
            point.y >= intervals[1].min && point.y <= intervals[1].max;
    }
};

struct GUI_Size
{
    int min_size;
    bool fit_at_least_children; // If set, grows to accomodate min size of children...

    // Fill options;
    bool fill; // Fill up parent, if multiple children fill up parent, the available size is split
    bool inherit_fill_from_children; // If a child also wants to fill, this will propagate up
    int preferred_size; // Set to 0 to disable, will be ignored if < min_size
};

GUI_Size gui_size_make(int min_size, bool fit_at_least_children, bool fill, bool inherit_fill_from_children, int preferred_size) {
    GUI_Size size;
    size.min_size = min_size;
    size.fill = fill;
    size.fit_at_least_children = fit_at_least_children;
    size.inherit_fill_from_children = inherit_fill_from_children;
    size.preferred_size = preferred_size;
    return size;
}

GUI_Size gui_size_make_fit(bool inherit_fill = false) {
    return gui_size_make(0.0f, true, false, inherit_fill, 0);
}

GUI_Size gui_size_make_fixed(float value) {
    return gui_size_make(value, false, false, false, 0);
}

GUI_Size gui_size_make_fill(bool fit_at_least_children = true) {
    return gui_size_make(0, fit_at_least_children, true, false, 0);
}

GUI_Size gui_size_make_preferred(int preferred, int min = 0, bool fit_at_least_children = true) {
    return gui_size_make(min, fit_at_least_children, true, false, preferred);
}



enum class GUI_Alignment
{
    MIN, // in x this is left_aligned, in y bottom aligned
    MAX,
    CENTER,
};

struct GUI_Position
{
    bool auto_layout; // Is set by parent, otherwise position needs to be specified
    GUI_Alignment alignment; // Only used with auto layout, is inherited from parent by default
    bool relative_to_window; // Only used if auto layout is off, either relative to window or relative to parent 
    vec2 offset; // Will be rounded to nearest integer value...
    Anchor anchor;
};

enum class GUI_Stack_Direction {
    LEFT_TO_RIGHT,
    RIGHT_TO_LEFT,
    TOP_TO_BOTTOM,
    BOTTOM_TO_TOP,
};

struct GUI_Layout
{
    GUI_Stack_Direction stack_direction;
    int padding[2];
    bool pad_between_children;
    GUI_Alignment child_default_alignment;
};



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

GUI_Drawable gui_drawable_make_none() {
    GUI_Drawable drawable;
    drawable.type = GUI_Drawable_Type::NONE;
    drawable.color = vec4(0.0f);
    drawable.text = string_create_static("");
    return drawable;
}

GUI_Drawable gui_drawable_make_text(String text, vec4 color = vec4(0.0f, 0.0f, 0.0f, 1.0f)) {
    GUI_Drawable drawable;
    drawable.type = GUI_Drawable_Type::TEXT;
    drawable.color = color;
    drawable.text = string_copy(text);
    return drawable;
}

GUI_Drawable gui_drawable_make_rect(vec4 color, int border_thickness = 0, vec4 border_color = vec4(0), int edge_radius = 0) {
    GUI_Drawable drawable;
    drawable.type = GUI_Drawable_Type::RECTANGLE;
    drawable.color = color;
    drawable.text = string_create_static("");
    drawable.border_color = border_color;
    drawable.edge_radius = edge_radius;
    drawable.border_thickness = border_thickness;
    return drawable;
}

void gui_drawable_destroy(GUI_Drawable& drawable) {
    if (drawable.type == GUI_Drawable_Type::TEXT) {
        string_destroy(&drawable.text);
    }
}



// Matching Checkpoint
enum class GUI_Matching_Checkpoint_Type
{
    NONE,
    INT,
    VOID_POINTER,
    CONST_CHAR_POINTER
};

struct GUI_Matching_Checkpoint
{
    GUI_Matching_Checkpoint_Type type;
    union {
        const char* name;
        void* void_ptr;
        int int_value;
    } data;
};

bool imgui_checkpoint_equals(GUI_Matching_Checkpoint a, GUI_Matching_Checkpoint b) {
    if (a.type != b.type) {
        return false;
    }
    switch (a.type)
    {
    case GUI_Matching_Checkpoint_Type::CONST_CHAR_POINTER: return strcmp(a.data.name, b.data.name) == 0;
    case GUI_Matching_Checkpoint_Type::INT: return a.data.int_value == b.data.int_value;
    case GUI_Matching_Checkpoint_Type::VOID_POINTER: return a.data.void_ptr == b.data.void_ptr;
    case GUI_Matching_Checkpoint_Type::NONE: return false;
    default: panic("");
    }
    return false;
}



// GUI Hierarchy
typedef void (*gui_userdata_destroy_fn)(void* userdata);

struct GUI_Node
{
    GUI_Size size[2];
    GUI_Drawable drawable;
    GUI_Position position;   // Resets to default each frame
    GUI_Alignment alignment; // Resets to default each frame
    GUI_Layout layout;       // Resets to default each frame
    int z_index;             // Stays persistent over frames

    // Enable/Disable settings (Must be set each frame)
    bool input_enabled; // Element can receive mouse hover, default false
    bool hidden; // Element and all children will be hidden this frame, will not be drawn
    bool keep_children_even_if_not_referenced;

    // Input
    bool mouse_hover; // If the mouse is hovering over this exact element
    bool mouse_hovers_child; // If the mouse is hovering over a child node which has input enabled

    // Userdata
    void* userdata;
    gui_userdata_destroy_fn userdata_destroy_fn;

    // Stuff calculated during layout
    GUI_Box bounding_box;
    Optional<GUI_Box> clipped_box; // Hierarchical clipping from parent (Does not include overlaps)
    int min_size[2];
    int min_child_size[2];
    bool hidden_by_parent; // If some parent is hidden, this node is hidden this frame
    bool should_fill[2];
    int calculated_size[2];

    // Infos for matching
    bool referenced_this_frame; // Node and all child nodes will be removed at end of frame if not referenced 
    int traversal_next_child; // Reset each frame, used to match nodes
    GUI_Matching_Checkpoint checkpoint;

    // Tree navigation
    int index_parent;
    int index_next_node; // Next node on the same level
    int index_first_child;
    int index_last_child;
};

void gui_node_destroy(GUI_Node& node) {
    if (node.userdata != 0) {
        assert(node.userdata_destroy_fn != 0, "Since userdata should always be allocated dynamically, there should always be a destroy!");
        node.userdata_destroy_fn(node.userdata);
        node.userdata = 0;
        node.userdata_destroy_fn = 0;
        logg("Userdata destroy was called!\n");
    }
    gui_drawable_destroy(node.drawable);
}

struct GUI_Handle
{
    int index;
    bool mouse_hover;
    bool mouse_hovers_child;
    bool first_time_created;
    void* userdata;
};

struct IMGUI
{
    Dynamic_Array<GUI_Node> nodes;
    GUI_Matching_Checkpoint active_checkpoint;
    GUI_Handle root_handle;
    Cursor_Icon_Type cursor_type;
    
    int focused_node; // 0 (E.g. root) When no node has focus

    Text_Renderer* text_renderer;
    Window* window;
    Input* input;
};

IMGUI imgui;



// GUI_Node Getters and setters
void gui_node_set_position_fixed(GUI_Handle handle, vec2 offset, Anchor anchor, bool relative_to_window = false) {
    imgui.nodes[handle.index].position.auto_layout = false;
    imgui.nodes[handle.index].position.offset = offset;
    imgui.nodes[handle.index].position.anchor = anchor;
    imgui.nodes[handle.index].position.relative_to_window = relative_to_window;
}

void gui_node_set_alignment(GUI_Handle handle, GUI_Alignment alignment = GUI_Alignment::MIN) {
    imgui.nodes[handle.index].position.auto_layout = true;
    imgui.nodes[handle.index].position.alignment = alignment;
}

void gui_node_set_layout(
    GUI_Handle handle, GUI_Stack_Direction stack_direction = GUI_Stack_Direction::TOP_TO_BOTTOM, GUI_Alignment child_default_alignment = GUI_Alignment::MIN)
{
    imgui.nodes[handle.index].layout.stack_direction = stack_direction;
    imgui.nodes[handle.index].layout.child_default_alignment = child_default_alignment;
}

void gui_node_set_padding(GUI_Handle handle, int x_padding, int y_padding, bool pad_between_children = false) {
    imgui.nodes[handle.index].layout.padding[0] = x_padding;
    imgui.nodes[handle.index].layout.padding[1] = y_padding;
    imgui.nodes[handle.index].layout.pad_between_children = pad_between_children;
}

void gui_node_update_drawable(GUI_Handle handle, GUI_Drawable drawable) {
    // Special handling for text drawable, to avoid memory allocations
    auto& node = imgui.nodes[handle.index];
    gui_drawable_destroy(node.drawable);
    node.drawable = drawable;
}

void gui_node_update_size(GUI_Handle handle, GUI_Size size_x, GUI_Size size_y) {
    imgui.nodes[handle.index].size[0] = size_x;
    imgui.nodes[handle.index].size[1] = size_y;
}

void gui_node_set_z_index(GUI_Handle handle, int z_index) {
    imgui.nodes[handle.index].z_index = z_index;
}

void gui_node_set_z_index_to_highest(GUI_Handle handle)
{
    auto& nodes = imgui.nodes;
    auto& node = nodes[handle.index];
    if (node.index_parent == -1) {
        return;
    }
    auto& parent_node = nodes[node.index_parent];

    // Figure out if the node is already the highest
    int min_z_index = 100000;
    int max_z_index = -100000;
    bool found_other = false;
    int child_index = parent_node.index_first_child;
    while (child_index != -1) {
        auto& child_node = nodes[child_index];
        SCOPE_EXIT(child_index = child_node.index_next_node);
        if (child_index == handle.index) {
            continue;
        }
        min_z_index = math_minimum(min_z_index, child_node.z_index);
        max_z_index = math_maximum(max_z_index, child_node.z_index);
        found_other = true;
    }

    if (!found_other) {
        node.z_index = 0;
        return;
    }

    // Set lowest z-index to 0, and adjust all others
    int new_highest = max_z_index - min_z_index;
    node.z_index = new_highest + 1;

    // Adjust all other z-indices
    child_index = parent_node.index_first_child;
    while (child_index != -1) {
        auto& child_node = nodes[child_index];
        SCOPE_EXIT(child_index = child_node.index_next_node);
        if (child_index == handle.index) {
            continue;
        }
        child_node.z_index -= min_z_index;
    }
}

Bounding_Box2 gui_node_get_previous_frame_size(GUI_Handle handle) {
    return imgui.nodes[handle.index].bounding_box.as_bounding_box();
}

void gui_node_enable_input(GUI_Handle handle) {
    imgui.nodes[handle.index].input_enabled = true;
}

void gui_node_set_focus(GUI_Handle handle) {
    imgui.focused_node = handle.index;
}

// Does nothing is node is not in focus
void gui_node_remove_focus(GUI_Handle handle) {
    if (imgui.focused_node == handle.index) {
        imgui.focused_node = 0;
    }
}

bool gui_node_has_focus(GUI_Handle handle) {
    return imgui.focused_node == handle.index;
}

void gui_node_hide(GUI_Handle handle) {
    imgui.nodes[handle.index].hidden = true;
}

void gui_node_keep_children_even_if_not_referenced(GUI_Handle handle) {
    imgui.nodes[handle.index].keep_children_even_if_not_referenced = true;
}

void gui_node_set_userdata(GUI_Handle& handle, void* userdata, gui_userdata_destroy_fn destroy_fn) {
    if (handle.index == 0) {
        panic("Cannot update root node!");
    }
    auto& node = imgui.nodes[handle.index];
    if (node.userdata != 0) {
        assert(node.userdata_destroy_fn != 0, "Since userdata should always be allocated dynamically, there should always be a destroy!");
        node.userdata_destroy_fn(node.userdata);
    }
    node.userdata = userdata;
    node.userdata_destroy_fn = destroy_fn;
    handle.userdata = userdata;
}



// Initializers and destroy for gui
void imgui_initialize(Text_Renderer* text_renderer, Window* window, Input* input)
{
    auto& pre = rendering_core.predefined;

    imgui.text_renderer = text_renderer;
    imgui.nodes = dynamic_array_create_empty<GUI_Node>(1);
    imgui.window = window;
    imgui.input = input;
    imgui.cursor_type = Cursor_Icon_Type::ARROW;
    imgui.active_checkpoint.type = GUI_Matching_Checkpoint_Type::NONE;
    imgui.focused_node = 0;

    // Note: We cannot add the root node with add_node, because root doesnt have a parent...
    imgui.root_handle.index = 0;
    imgui.root_handle.mouse_hover = false;
    imgui.root_handle.mouse_hovers_child = false;
    imgui.root_handle.userdata = nullptr;

    // Push root node
    GUI_Node root;
    root.bounding_box = bounding_box_2_convert(bounding_box_2_make_anchor(vec2(0.0f), vec2(2.0f), Anchor::CENTER_CENTER), Unit::NORMALIZED_SCREEN);
    root.referenced_this_frame = true;
    root.index_first_child = -1;
    root.index_last_child = -1;
    root.index_parent = -1;
    root.index_next_node = -1;
    root.traversal_next_child = -1;
    root.z_index = 0;
    root.userdata = nullptr;
    root.userdata_destroy_fn = nullptr;
    root.drawable = gui_drawable_make_none();
    auto& info = rendering_core.render_information;
    root.size[0] = gui_size_make(info.backbuffer_width, false, false, false, 0);
    root.size[1] = gui_size_make(info.backbuffer_height, false, false, false, 0);
    root.input_enabled = false;
    root.hidden = false;
    root.keep_children_even_if_not_referenced = false;
    dynamic_array_push_back(&imgui.nodes, root);

    gui_node_set_position_fixed(imgui.root_handle, vec2(0.0f), Anchor::BOTTOM_LEFT, false);
    gui_node_set_layout(imgui.root_handle);
    gui_node_set_padding(imgui.root_handle, 0, 0, false);
}

void imgui_destroy() {
    for (int i = 0; i < imgui.nodes.size; i++) {
        gui_node_destroy(imgui.nodes[i]);
    }
    dynamic_array_destroy(&imgui.nodes);
}



// Matching and node adding
void gui_matching_add_checkpoint_int(int value) {
    imgui.active_checkpoint.type = GUI_Matching_Checkpoint_Type::INT;
    imgui.active_checkpoint.data.int_value = value;
}

void gui_matching_add_checkpoint_void_ptr(void* ptr) {
    imgui.active_checkpoint.type = GUI_Matching_Checkpoint_Type::VOID_POINTER;
    imgui.active_checkpoint.data.void_ptr = ptr;
}

void gui_matching_add_checkpoint_name(const char* name) {
    imgui.active_checkpoint.type = GUI_Matching_Checkpoint_Type::CONST_CHAR_POINTER;
    imgui.active_checkpoint.data.name = name;
}

GUI_Handle gui_add_node(GUI_Handle parent_handle, GUI_Size size_x, GUI_Size size_y, GUI_Drawable drawable)
{
    auto& nodes = imgui.nodes;

    // Node-Matching (Create new node or reuse node from last frame)
    bool create_new_node;
    int node_index = nodes[parent_handle.index].traversal_next_child;
    if (imgui.active_checkpoint.type != GUI_Matching_Checkpoint_Type::NONE) {
        // Find checkpoint
        if (node_index != -1 && imgui_checkpoint_equals(imgui.active_checkpoint, nodes[node_index].checkpoint)) {
            create_new_node = false;
        }
        else {
            create_new_node = true;
            int child_index = nodes[parent_handle.index].index_first_child;
            while (child_index != -1) {
                auto& child_node = nodes[child_index];
                SCOPE_EXIT(child_index = child_node.index_next_node);
                if (imgui_checkpoint_equals(imgui.active_checkpoint, child_node.checkpoint)) {
                    node_index = child_index;
                    create_new_node = false;
                    break;
                }
            }
        }
    }
    else {
        if (node_index == -1) {
            create_new_node = true;
        }
        else {
            if (nodes[node_index].checkpoint.type != GUI_Matching_Checkpoint_Type::NONE) {
                create_new_node = true;
            }
            else {
                create_new_node = false;
            }
        }
    }

    if (create_new_node) { // No match found from previous frame, create new node
        GUI_Node node;
        node.index_parent = parent_handle.index;
        node.index_first_child = -1;
        node.index_last_child = -1;
        node.index_next_node = -1;
        node.traversal_next_child = -1;
        node.mouse_hover = false;
        node.mouse_hovers_child = false;
        node.userdata = 0;
        node.userdata_destroy_fn = 0;
        node.z_index = 0;
        node.hidden = false;
        node.keep_children_even_if_not_referenced = false;
        node.input_enabled = false;
        node.drawable.type = GUI_Drawable_Type::NONE; // Note: This is required because drawable actually has ownership of memory
        dynamic_array_push_back(&imgui.nodes, node);
        node_index = imgui.nodes.size - 1;

        // Create links between parent and children
        auto& parent_node = nodes[parent_handle.index];
        parent_node = nodes[parent_handle.index];
        if (parent_node.index_first_child == -1) {
            assert(parent_node.index_last_child == -1, "If one is -1, both indices must be");
            parent_node.index_first_child = node_index;
            parent_node.index_last_child = node_index;
        }
        else {
            assert(nodes[parent_node.index_last_child].index_next_node, "Last child must always have -1!");
            nodes[parent_node.index_last_child].index_next_node = node_index;
            parent_node.index_last_child = node_index;
        }
    }

    // Update next traversal
    nodes[parent_handle.index].traversal_next_child = imgui.nodes[node_index].index_next_node;

    // Create handle
    GUI_Handle handle;
    handle.index = node_index;
    handle.mouse_hover = nodes[node_index].mouse_hover;
    handle.mouse_hovers_child = nodes[node_index].mouse_hovers_child;
    handle.first_time_created = create_new_node;
    handle.userdata = nodes[node_index].userdata;

    // Update node data
    auto& node = imgui.nodes[node_index];
    node.referenced_this_frame = true;
    gui_node_set_layout(handle);
    gui_node_set_alignment(handle, imgui.nodes[parent_handle.index].layout.child_default_alignment);
    gui_node_update_drawable(handle, drawable);
    gui_node_set_padding(handle, 0, 0);
    node.size[0] = size_x;
    node.size[1] = size_y;
    node.checkpoint = imgui.active_checkpoint;
    imgui.active_checkpoint.type = GUI_Matching_Checkpoint_Type::NONE;

    return handle;
}



// GUI UPDATE
void gui_remove_unreferenced_nodes_recursive(Array<int> new_node_indices, int node_index, int& next_free_node_index, bool dont_delete_children, bool hidden_by_parent)
{
    auto& nodes = imgui.nodes;
    auto& node = nodes[node_index];

    // Check if node is parent
    if (node_index == 0) {
        new_node_indices[0] = 0;
        next_free_node_index = 1;
    }
    // Check if node should be deleted
    else if ((!node.referenced_this_frame && !dont_delete_children) || new_node_indices[node.index_parent] == -1) {
        gui_node_destroy(node);
        new_node_indices[node_index] = -1;
    }
    // Otherwise update node index
    else {
        new_node_indices[node_index] = next_free_node_index;
        next_free_node_index += 1;
        // Also update parent index
        node.index_parent = new_node_indices[node.index_parent];
        assert(node.index_parent != -1, "Parent cannot be deleted, other");
    }

    // Update child indices
    {
        node.hidden_by_parent = node.hidden || hidden_by_parent;
        int child_index = node.index_first_child;
        while (child_index != -1) {
            auto& child = nodes[child_index];
            SCOPE_EXIT(child_index = child.index_next_node);
            gui_remove_unreferenced_nodes_recursive(
                new_node_indices, child_index, next_free_node_index,
                dont_delete_children || node.keep_children_even_if_not_referenced,
                node.hidden_by_parent
            );
        }

        // Update next connections of children
        child_index = node.index_first_child;
        node.index_first_child = -1;
        node.index_last_child = -1;
        int last_valid_child = -1; // To update next pointer
        while (child_index != -1) {
            auto& child = nodes[child_index];
            SCOPE_EXIT(child_index = child.index_next_node);
            if (new_node_indices[child_index] == -1) { // Child will be deleted
                continue;
            }

            if (node.index_first_child == -1) {
                node.index_first_child = new_node_indices[child_index];
            }
            node.index_last_child = new_node_indices[child_index];
            if (last_valid_child != -1) {
                nodes[last_valid_child].index_next_node = new_node_indices[child_index];
            }
            last_valid_child = child_index;
        }
        if (last_valid_child != -1) {
            nodes[last_valid_child].index_next_node = -1;
        }
    }
}

bool gui_in_stacking_dimension(GUI_Stack_Direction direction, int dim) {
    if (dim == 0) {
        return direction == GUI_Stack_Direction::LEFT_TO_RIGHT || direction == GUI_Stack_Direction::RIGHT_TO_LEFT;
    }
    else if (dim == 1) {
        return direction == GUI_Stack_Direction::TOP_TO_BOTTOM || direction == GUI_Stack_Direction::BOTTOM_TO_TOP;
    }
    else {
        panic("");
    }
    return false;
}

void gui_layout_calculate_min_size(int node_index, int dim)
{
    auto& nodes = imgui.nodes;
    auto& node = nodes[node_index];

    node.min_child_size[dim] = 0.0f;
    const bool in_stacking_dimension = gui_in_stacking_dimension(node.layout.stack_direction, dim);

    // Loop over children
    bool child_with_fill_exists = false;
    int child_count = 0;
    int child_index = node.index_first_child;
    while (child_index != -1) {
        auto& child_node = nodes[child_index];
        SCOPE_EXIT(child_index = child_node.index_next_node);
        gui_layout_calculate_min_size(child_index, dim);

        if (!child_node.position.auto_layout || child_node.hidden_by_parent) {
            continue;
        }
        child_count++;
        if (child_node.size[dim].fill) {
            child_with_fill_exists = true;
        }
        if (in_stacking_dimension) {
            node.min_child_size[dim] += child_node.min_size[dim];
        }
        else {
            node.min_child_size[dim] = math_maximum(node.min_child_size[dim], child_node.min_size[dim]);
        }
    }

    // Check inherited fill
    if (node.size[dim].fill) {
        node.should_fill[dim] = true;
    }
    else if (node.size[dim].inherit_fill_from_children) {
        node.should_fill[dim] = child_with_fill_exists;
    }
    else {
        node.should_fill[dim] = false;
    }

    // Calculate min size
    if (node.size[dim].fit_at_least_children) {
        int padding_size = node.layout.padding[dim] * 2;
        if (node.layout.pad_between_children) {
            padding_size += math_maximum(0, child_count - 1) * node.layout.padding[dim];
        }
        node.min_size[dim] = math_maximum(node.size[dim].min_size, node.min_child_size[dim] + padding_size);
    }
    else {
        node.min_size[dim] = node.size[dim].min_size;
    }
}

struct GUI_Constraint {
    GUI_Constraint() {}
    GUI_Constraint(int node_index, int value, bool is_max_constraint) : node_index(node_index), value(value), is_max_constraint(is_max_constraint) {}
    bool is_max_constraint;
    int value;
    int node_index;
};

struct GUI_Constraint_Comparator {
    bool operator()(const GUI_Constraint& a, const GUI_Constraint& b) {
        return a.value < b.value;
    }
};

void gui_layout_layout_children(int node_index, int dim)
{
    auto& nodes = imgui.nodes;
    auto& node = nodes[node_index];
    auto& layout = node.layout;

    // Size is set by parent at this point
    const int node_size = node.calculated_size[dim];
    const int node_pos = node.bounding_box[dim].min;
    const bool in_stack_dimension = gui_in_stacking_dimension(node.layout.stack_direction, dim);

    // Calculated clipped bounding box
    if (dim == 1) {
        // Note: This is currently a little hacky, since we can only do the "full" calculation once we know the 
        //      values of both dimension, but this function is called twice
        if (node.index_parent != -1) {
            auto& parent_node = nodes[node.index_parent];
            if (parent_node.clipped_box.available) {
                // Adjust parent box for padding
                auto parent_box = parent_node.clipped_box.value;
                parent_box[0].min += parent_node.layout.padding[0];
                parent_box[0].max -= parent_node.layout.padding[0];
                parent_box[1].min += parent_node.layout.padding[1];
                parent_box[1].max -= parent_node.layout.padding[1];
                if (parent_box[0].max > parent_box[0].min && parent_box[1].max > parent_box[1].min) {
                    node.clipped_box = parent_box.intersect(node.bounding_box);
                }
                else {
                    node.clipped_box = optional_make_failure<GUI_Box>();
                }
            }
            else {
                node.clipped_box = optional_make_failure<GUI_Box>();
            }
        }
        else {
            node.clipped_box = optional_make_success(node.bounding_box);
        }
    }

    // Set sizes for all non-fill children
    if (in_stack_dimension)
    {
        // Collect fill children and calculate space
        Dynamic_Array<GUI_Constraint> preferred_constraints = dynamic_array_create_empty<GUI_Constraint>(1);
        Dynamic_Array<GUI_Constraint> fill_constraints = dynamic_array_create_empty<GUI_Constraint>(1);
        SCOPE_EXIT(dynamic_array_destroy(&preferred_constraints));
        SCOPE_EXIT(dynamic_array_destroy(&fill_constraints));
        int space_without_padding = node_size - node.layout.padding[dim];
        int space_min_preferred = 0;
        int space_full_preferred = 0;
        int space_min_fill = 0;
        {
            int child_index = node.index_first_child;
            while (child_index != -1)
            {
                auto& child_node = nodes[child_index];
                SCOPE_EXIT(child_index = child_node.index_next_node);

                // Ignore hidden and non auto-layout nodes
                if (child_node.hidden_by_parent) {
                    child_node.calculated_size[dim] = 0;
                    continue;
                }
                if (!child_node.position.auto_layout) {
                    if (child_node.should_fill[dim]) {
                        child_node.calculated_size[dim] = math_maximum(child_node.min_size[dim], node_size - node.layout.padding[dim] * 2);
                    }
                    else {
                        child_node.calculated_size[dim] = child_node.min_size[dim];
                    }
                    continue;
                }

                // Set size to minimum and adjust free space
                child_node.calculated_size[dim] = child_node.min_size[dim];
                if (node.layout.pad_between_children && child_index != node.index_first_child) {
                    space_without_padding -= node.layout.padding[dim];
                }

                // Add node indices to corresponding indices
                if (child_node.should_fill[dim]) {
                    if (child_node.size[dim].preferred_size > child_node.min_size[dim]) {
                        dynamic_array_push_back(&preferred_constraints, GUI_Constraint(child_index, child_node.min_size[dim], false));
                        dynamic_array_push_back(&preferred_constraints, GUI_Constraint(child_index, child_node.size[dim].preferred_size, true));
                        space_min_preferred += child_node.min_size[dim];
                        space_full_preferred += child_node.size[dim].preferred_size;
                    }
                    else {
                        dynamic_array_push_back(&fill_constraints, GUI_Constraint(child_index, child_node.min_size[dim], false));
                        space_min_fill += child_node.min_size[dim];
                    }
                }
                else {
                    space_without_padding -= child_node.min_size[dim];
                }
            }
        }

        auto solve_constraints_fn = [](Dynamic_Array<GUI_Constraint>& constraints, int space_available, int dim) -> void 
        {
            if (constraints.size == 0) {
                return;
            }
            dynamic_array_sort(&constraints, GUI_Constraint_Comparator());
            
            // Find suitable size_threshhold
            int size_threshold = 0.0f;
            int pixel_remainder = 0; // Because of int division some pixels need to the first few items which can take more

            int active_count = 0;
            for (int i = 0; i < constraints.size; i++)
            {
                auto& constraint = constraints[i];
                if (!constraint.is_max_constraint) {
                    active_count += 1;
                    space_available += constraint.value;
                }
                else {
                    assert(active_count > 0, "Must not happen when sorting intervals with min/max");
                    active_count -= 1;
                    space_available -= constraint.value;
                }

                if (constraint.value * active_count <= space_available) {
                    if (active_count == 0) {
                        size_threshold = constraint.value;
                        pixel_remainder = 0;
                    }
                    else {
                        size_threshold = (float)space_available / active_count;
                        pixel_remainder = space_available % active_count;
                    }
                }
                else {
                    break;
                }
            }

            // Set sizes depending on threshold
            for (int i = 0; i < constraints.size; i++) {
                auto& constraint = constraints[i];
                if (constraint.is_max_constraint) { // Since min constraints handle setting the size we can skip max constraints
                    continue;
                }
                auto& node = imgui.nodes[constraint.node_index];
                if (size_threshold < node.min_size[dim]) {
                    node.calculated_size[dim] = node.min_size[dim];
                }
                else if (size_threshold > node.size[dim].preferred_size && node.size[dim].preferred_size > node.min_size[dim]) {
                    node.calculated_size[dim] = node.size[dim].preferred_size;
                }
                else {
                    node.calculated_size[dim] = size_threshold;
                    if (pixel_remainder > 0) {
                        node.calculated_size[dim] += 1;
                        pixel_remainder -= 1;
                    }
                }
            }
        };

        if (space_without_padding < space_min_fill + space_min_preferred) {
            // Everything is set to minimum, nothing more can be done
        }
        else if (space_without_padding < space_min_fill + space_full_preferred) {
            // Fill out preferred contraints, fill_nodes stay at minimum
            solve_constraints_fn(preferred_constraints, space_without_padding - space_min_preferred - space_min_fill, dim);
        }
        else {
            // All preferred nodes can be fully set, fill_nodes need to be calculated
            for (int i = 0; i < preferred_constraints.size; i++) {
                auto& constraint = preferred_constraints[i];
                auto& node = nodes[constraint.node_index];
                node.calculated_size[dim] = node.size[dim].preferred_size;
            }
            solve_constraints_fn(fill_constraints, space_without_padding - space_min_fill - space_full_preferred, dim);
        }
    }
    else
    {
        int child_index = node.index_first_child;
        while (child_index != -1) {
            auto& child_node = nodes[child_index];
            SCOPE_EXIT(child_index = child_node.index_next_node);
            if (child_node.hidden_by_parent) {
                child_node.calculated_size[dim] = 0;
                continue;
            }
            if (child_node.should_fill[dim]) {
                int padding = node.layout.padding[dim];
                if (!child_node.position.auto_layout) {
                    padding = 0;
                }
                child_node.calculated_size[dim] = math_maximum(node_size - padding * 2, child_node.min_size[dim]);
                if (child_node.size[dim].preferred_size > child_node.min_size[dim]) {
                    child_node.calculated_size[dim] = math_minimum(child_node.calculated_size[dim], child_node.size[dim].preferred_size);
                }
            }
            else {
                child_node.calculated_size[dim] = child_node.min_size[dim];
            }
        }
    }

    // Loop over all children and set their bounding boxes
    {
        // Setup stack cursor 
        int stack_sign;
        int stack_cursor;
        switch (node.layout.stack_direction) {
        case GUI_Stack_Direction::TOP_TO_BOTTOM: stack_sign = -1; stack_cursor = node.bounding_box[1].max; break;
        case GUI_Stack_Direction::BOTTOM_TO_TOP: stack_sign = 1; stack_cursor = node.bounding_box[1].min; break;
        case GUI_Stack_Direction::LEFT_TO_RIGHT: stack_sign = 1; stack_cursor = node.bounding_box[0].min; break;
        case GUI_Stack_Direction::RIGHT_TO_LEFT: stack_sign = -1; stack_cursor = node.bounding_box[0].max; break;
        default: panic("");
        }
        stack_cursor += layout.padding[dim] * stack_sign;

        int child_index = node.index_first_child;
        while (child_index != -1)
        {
            auto& child_node = nodes[child_index];
            SCOPE_EXIT(child_index = child_node.index_next_node);
            if (child_node.hidden_by_parent) {
                continue;
            }

            // Alignment info if child should be aligned
            bool align_child = false;
            GUI_Alignment final_align = GUI_Alignment::MIN;
            int rel_pos = node_pos;
            int padding = node.layout.padding[dim];
            int rel_size = node_size;
            int offset = 0;

            // Check how position should be calculated
            int child_pos = 0;
            if (child_node.position.auto_layout)
            {
                if (in_stack_dimension) {
                    child_pos = stack_cursor;
                    if (stack_sign < 0.0f) {
                        child_pos -= child_node.calculated_size[dim];
                    }
                    int padding = node.layout.pad_between_children ? node.layout.padding[dim] : 0;
                    stack_cursor += (child_node.calculated_size[dim] + padding) * stack_sign;
                }
                else {
                    align_child = true;
                    final_align = child_node.position.alignment;
                }
            }
            else
            {
                align_child = true;
                padding = false;
                if (child_node.position.relative_to_window) {
                    rel_pos = 0;
                    auto& info = rendering_core.render_information;
                    rel_size = dim == 0 ? info.backbuffer_width : info.backbuffer_height;
                }

                offset = (dim == 0 ? child_node.position.offset.x : child_node.position.offset.y);
                vec2 anchor_dir = anchor_to_direction(child_node.position.anchor);
                float offset_dir = dim == 0 ? anchor_dir.x : anchor_dir.y;
                if (offset_dir < -0.1f) {
                    final_align = GUI_Alignment::MIN;
                }
                else if (offset_dir > 0.1f) {
                    final_align = GUI_Alignment::MAX;
                }
                else {
                    final_align = GUI_Alignment::CENTER;
                }
            }

            // Do alignment if requested
            if (align_child)
            {
                switch (final_align)
                {
                case GUI_Alignment::MIN: child_pos = rel_pos + padding + offset; break;
                case GUI_Alignment::MAX: child_pos = rel_pos + rel_size - child_node.calculated_size[dim] - padding + offset; break;
                case GUI_Alignment::CENTER: child_pos = (rel_pos + rel_size / 2.0f) - child_node.calculated_size[dim] / 2.0f + offset;
                }
            }

            // Set child bounding box from position and size
            child_node.bounding_box[dim].min = child_pos;
            child_node.bounding_box[dim].max = child_pos + child_node.calculated_size[dim];

            // Recurse to all children
            gui_layout_layout_children(child_index, dim);
        }
    }
}

bool gui_handle_input(Input* input, int node_index)
{
    auto& nodes = imgui.nodes;
    auto& node = nodes[node_index];

    // Check which child has the highest z-index and overlapps
    int highest_z_node_index = -1;
    int highest_z = 0;
    int child_index = node.index_first_child;
    while (child_index != -1) {
        auto& child_node = nodes[child_index];
        SCOPE_EXIT(child_index = child_node.index_next_node);

        if (!child_node.clipped_box.available || child_node.hidden_by_parent) {
            continue;
        }
        bool overlaps_mouse = child_node.clipped_box.value.is_point_inside(
            vec2(input->mouse_x, (int)(rendering_core.render_information.backbuffer_height - input->mouse_y)));
        if (!overlaps_mouse) {
            continue;
        }

        // Initialize highest z if not set
        if (highest_z_node_index == -1) {
            highest_z = child_node.z_index;
            highest_z_node_index = child_index;
        }
        if (child_node.z_index >= highest_z) {
            highest_z = child_node.z_index;
            highest_z_node_index = child_index;
        }
    }

    // Loop over children
    bool child_took_input = false;
    if (highest_z_node_index != -1) {
        child_took_input = gui_handle_input(input, highest_z_node_index);
    }

    node.mouse_hover = !child_took_input && node.input_enabled;
    node.mouse_hovers_child = !node.mouse_hover;
    return node.mouse_hover || child_took_input;
}

void gui_append_to_string(String* append_to, int indentation_level, int node_index)
{
    auto& nodes = imgui.nodes;
    auto& node = nodes[node_index];

    for (int i = 0; i < indentation_level; i++) {
        string_append_formated(append_to, "  ");
    }

    bool print_bb = true;
    bool print_layout = true;

    string_append_formated(append_to, "#%d: ", node_index);
    if (print_bb) {
        auto bb = node.bounding_box;
        if (!node.clipped_box.available) {
            string_append_formated(append_to, "CLIPPED");
        }
        else if (node.hidden_by_parent) {
            string_append_formated(append_to, "Hidden");
        }
        else {
            string_append_formated(append_to, "(%d, %d)", bb[0].max - bb[0].min, bb[1].max - bb[1].min);
        }
    }

    string_append(append_to, "\n");
    {
        int child_index = node.index_first_child;
        while (child_index != -1) {
            auto& child_node = nodes[child_index];
            SCOPE_EXIT(child_index = child_node.index_next_node);
            gui_append_to_string(append_to, indentation_level + 1, child_index);
        }
    }
}

struct GUI_Dependency {
    int dependency_count;
    Dynamic_Array<int> dependents;
};

void gui_add_dependency(Dynamic_Array<GUI_Node>& nodes, Array<GUI_Dependency> dependencies, int node_index, int depends_on_index, bool parent_child_dependency)
{
    auto& node = nodes[node_index];
    auto& other = nodes[depends_on_index];

    if (other.hidden_by_parent) {
        return;
    }

    if (other.drawable.type == GUI_Drawable_Type::NONE) {
        if (parent_child_dependency) {
            if (other.index_parent == -1) {
                return;
            }
            gui_add_dependency(nodes, dependencies, node_index, other.index_parent, parent_child_dependency);
        }
        else {
            return;
        }
    }
    else {
        dependencies[node_index].dependency_count += 1;
        dynamic_array_push_back(&dependencies[depends_on_index].dependents, node_index);
    }
}

bool check_overlap_dependency(Dynamic_Array<GUI_Node>& nodes, Array<GUI_Dependency> dependencies, int node_index, int other_index, bool check_z_index)
{
    auto* node = &nodes[node_index];
    auto* other = &nodes[other_index];

    if (!node->bounding_box.overlaps(other->bounding_box)) {
        return false;
    }

    // Other would need to wait on me, except if the z-index is higher
    if (other->z_index > node->z_index && check_z_index) {
        GUI_Node* swap = other;
        other = node;
        node = swap;
        int swapi = other_index;
        other_index = node_index;
        node_index = swapi;
    }

    if (node->drawable.type == GUI_Drawable_Type::NONE) {
        // Overlap all my children with the other
        int child_index = node->index_first_child;
        bool overlapped_any = false;
        while (child_index != -1) {
            auto& child_node = nodes[child_index];
            SCOPE_EXIT(child_index = child_node.index_next_node);
            if (child_node.hidden_by_parent) {
                continue;
            }
            if (check_overlap_dependency(nodes, dependencies, child_index, other_index, false)) {
                overlapped_any = true;
            };
        }
        return overlapped_any;
    }

    // Check if we overlap with any child, if so we don't need to add any additional dependencies
    bool overlapped_any = false;
    int child_index = other->index_first_child;
    while (child_index != -1) {
        auto& child_node = nodes[child_index];
        SCOPE_EXIT(child_index = child_node.index_next_node);
        if (child_node.hidden_by_parent) {
            continue;
        }
        if (check_overlap_dependency(nodes, dependencies, node_index, child_index, false)) {
            overlapped_any = true;
        };
    }
    if (!overlapped_any) {
        if (other->drawable.type == GUI_Drawable_Type::NONE) {
            return false;
        }
        gui_add_dependency(nodes, dependencies, node_index, other_index, false);
    }

    return true;
}

void gui_update(Input* input)
{
    auto& core = rendering_core;
    auto& pre = core.predefined;
    auto& info = core.render_information;
    auto& nodes = imgui.nodes;

    imgui.active_checkpoint.type = GUI_Matching_Checkpoint_Type::NONE;

    // Remove unreferenced nodes (And update indices accurdingly)
    {
        // Generate new node positions
        Array<int> new_node_indices = array_create_empty<int>(nodes.size);
        SCOPE_EXIT(array_destroy(&new_node_indices));
        int next_free_index = 0;
        gui_remove_unreferenced_nodes_recursive(new_node_indices, 0, next_free_index, false, false);

        // Do compaction
        Dynamic_Array<GUI_Node> new_nodes = dynamic_array_create_empty<GUI_Node>(next_free_index + 1);
        new_nodes.size = next_free_index;
        for (int i = 0; i < nodes.size; i++) {
            int new_index = new_node_indices[i];
            if (new_index != -1) {
                new_nodes[new_index] = nodes[i];
            }
        }
        auto old = imgui.nodes;
        imgui.nodes = new_nodes;
        dynamic_array_destroy(&old);

        // Update root node
        nodes[0].referenced_this_frame = true;
        // Update focused node
        imgui.focused_node = new_node_indices[imgui.focused_node];
        if (imgui.focused_node == -1) {
            imgui.focused_node = 0;
        }
    }

    // Reset node data
    for (int i = 0; i < imgui.nodes.size; i++) {
        auto& node = imgui.nodes[i];
        node.referenced_this_frame = false;
        node.traversal_next_child = node.index_first_child;
        node.mouse_hover = false;
        node.mouse_hovers_child = false;
        node.keep_children_even_if_not_referenced = false;
    }

    // Layout UI 
    {
        auto& nodes = imgui.nodes;

        // Set root to windows size
        nodes[0].size[0] = gui_size_make(info.backbuffer_width, false, false, false, 0);
        nodes[0].size[1] = gui_size_make(info.backbuffer_height, false, false, false, 0);
        nodes[0].calculated_size[0] = info.backbuffer_width;
        nodes[0].calculated_size[1] = info.backbuffer_height;
        nodes[0].bounding_box[0].min = 0;
        nodes[0].bounding_box[0].max = info.backbuffer_width;
        nodes[0].bounding_box[1].min = 0;
        nodes[0].bounding_box[1].max = info.backbuffer_height;

        // Calculate layout
        gui_layout_calculate_min_size(0, 0);
        gui_layout_calculate_min_size(0, 1);
        gui_layout_layout_children(0, 0);
        gui_layout_layout_children(0, 1);
    }

    // Print UI if requested
    if (input->key_pressed[(int)Key_Code::P]) {
        String str = string_create_empty(1);
        SCOPE_EXIT(string_destroy(&str));
        gui_append_to_string(&str, 0, 0);
        logg("%s\n\n", str.characters);
    }

    // Handle input (Do mouse collision testing)
    gui_handle_input(input, 0);

    // Handle cursor
    static Cursor_Icon_Type last_icon_type = Cursor_Icon_Type::ARROW;
    if (last_icon_type != imgui.cursor_type) {
        window_set_cursor_icon(imgui.window, imgui.cursor_type);
        last_icon_type = imgui.cursor_type;
    }
    imgui.cursor_type = Cursor_Icon_Type::ARROW; // Cursor needs to be set each frame, otherwise it defaults to Arrow

    // Render UI
    {
        auto& nodes = imgui.nodes;

        // Generate draw batches
        Array<int> execution_order = array_create_empty<int>(nodes.size);
        Dynamic_Array<int> batch_start_indices = dynamic_array_create_empty<int>(nodes.size);
        SCOPE_EXIT(array_destroy(&execution_order));
        SCOPE_EXIT(dynamic_array_destroy(&batch_start_indices));
        {
            // Initialize dependency graph structure
            Array<GUI_Dependency> dependencies = array_create_empty<GUI_Dependency>(nodes.size);
            for (int i = 0; i < dependencies.size; i++) {
                auto& dependency = dependencies[i];
                dependency.dependency_count = 0;
                dependency.dependents = dynamic_array_create_empty<int>(1);
            }
            SCOPE_EXIT(
                for (int i = 0; i < dependencies.size; i++) {
                    dynamic_array_destroy(&dependencies[i].dependents);
                }
            array_destroy(&dependencies);
            );

            // Generate dependencies
            for (int node_index = 0; node_index < nodes.size; node_index++)
            {
                auto& node = nodes[node_index];
                // Loop over all children
                auto child_index = node.index_first_child;
                while (child_index != -1) {
                    auto& child_node = nodes[child_index];
                    SCOPE_EXIT(child_index = child_node.index_next_node);

                    // Add parent-child dependency
                    gui_add_dependency(nodes, dependencies, child_index, node_index, true);

                    // If the node doesn't use auto-layout, we need to do collision checking
                    if (!child_node.position.auto_layout) {
                        // Check for overlap-dependencies between children
                        int other_child_index = node.index_first_child;
                        bool before_child = true;
                        while (other_child_index != -1) {
                            auto& other_child_node = nodes[other_child_index];
                            SCOPE_EXIT(other_child_index = other_child_node.index_next_node);
                            if (other_child_index == child_index) {
                                before_child = false;
                                continue; // Skip self intersection
                            }

                            if (other_child_node.position.auto_layout) {
                                // Collision with auto layout and non-auto layout node --> non-auto is drawn on top
                                check_overlap_dependency(nodes, dependencies, child_index, other_child_index, false);
                            }
                            else if (!before_child) {
                                // Remember that there is a symmetry here, and we don't want double checks
                                check_overlap_dependency(nodes, dependencies, other_child_index, child_index, true);
                            }
                        }
                    }
                }
            }

            int next_free_in_order = 0;
            int non_hidden_node_count = 0;
            // Generate first batch by looking for all things that are runnable
            dynamic_array_push_back(&batch_start_indices, 0);
            for (int i = 0; i < dependencies.size; i++) {
                if (!nodes[i].hidden_by_parent) {
                    non_hidden_node_count += 1;
                }
                auto& dependency = dependencies[i];
                if (dependency.dependency_count == 0 && !nodes[i].hidden_by_parent) {
                    execution_order[next_free_in_order] = i;
                    next_free_in_order += 1;
                }
            }
            dynamic_array_push_back(&batch_start_indices, next_free_in_order);

            // Run through dependency graph
            while (true)
            {
                const int batch_start = batch_start_indices[batch_start_indices.size - 2];
                const int batch_end = batch_start_indices[batch_start_indices.size - 1];
                if (batch_start == batch_end) {
                    panic("");
                    break;
                }

                // Remove all dependencies and queue next workloads
                for (int i = batch_start; i < batch_end; i++)
                {
                    auto& dependency = dependencies[execution_order[i]];
                    for (int j = 0; j < dependency.dependents.size; j++)
                    {
                        int waiting_index = dependency.dependents[j];
                        auto& waiting_dependency = dependencies[waiting_index];
                        assert(waiting_dependency.dependency_count > 0, "Must not happen!");
                        waiting_dependency.dependency_count -= 1;
                        // Add to next batch if the workload can be drawn
                        if (waiting_dependency.dependency_count == 0 && !nodes[waiting_index].hidden_by_parent) {
                            execution_order[next_free_in_order] = waiting_index;
                            next_free_in_order += 1;
                        }
                    }
                }

                if (next_free_in_order == batch_end) {
                    assert(next_free_in_order == non_hidden_node_count, "Deadlock must not happen!");
                    break;
                }
                dynamic_array_push_back(&batch_start_indices, next_free_in_order); // Push last index
            }
        }

        // Query render primitives
        auto& pre = rendering_core.predefined;
        auto posSizeCombined = vertex_attribute_make<vec4>("PosSize2D");
        auto clippedBoundingBox = vertex_attribute_make<vec4>("ClippedBoundingBox");
        auto borderThicknessEdgeRadius = vertex_attribute_make<vec2>("BorderThicknessEdgeRadius");
        auto borderColor = vertex_attribute_make<vec4>("BorderColor");
        auto rectangle_mesh = rendering_core_query_mesh(
            "gui_rectangle_mesh",
            vertex_description_create({
                posSizeCombined,
                clippedBoundingBox,
                rendering_core.predefined.color4,
                borderColor,
                borderThicknessEdgeRadius
                }),
            true
        );
        auto rectangle_shader = rendering_core_query_shader("gui_rect.glsl");

        auto render_state_2D = pipeline_state_make_default();
        render_state_2D.blending_state.blending_enabled = true;
        render_state_2D.blending_state.source = Blend_Operand::SOURCE_ALPHA;
        render_state_2D.blending_state.destination = Blend_Operand::ONE_MINUS_SOURCE_ALPHA;
        render_state_2D.blending_state.equation = Blend_Equation::ADDITION;
        render_state_2D.depth_state.test_type = Depth_Test_Type::IGNORE_DEPTH;
        Render_Pass* pass_2D = rendering_core_query_renderpass("2D pass", render_state_2D, 0);
        render_pass_add_dependency(pass_2D, rendering_core.predefined.main_pass);

        static int skip_batches = 0;
        if (input->key_pressed[(int)Key_Code::O] && imgui.focused_node == 0) {
            skip_batches += 1;
            logg("Skip batches: %\n", skip_batches);
        }
        else if (input->key_pressed[(int)Key_Code::P] && imgui.focused_node == 0) {
            skip_batches -= 1;
            logg("Skip batches: %\n", skip_batches);
        }
        skip_batches = math_clamp(skip_batches, 0, batch_start_indices.size - 1);

        // Draw batches in order
        for (int batch = 0; batch < batch_start_indices.size - 1 - skip_batches; batch++)
        {
            int batch_start = batch_start_indices[batch];
            int batch_end = batch_start_indices[batch + 1];
            const int before_rectangle_count = rectangle_mesh->vertex_count;
            for (int node_indirect_index = batch_start; node_indirect_index < batch_end; node_indirect_index++)
            {
                auto& node = nodes[execution_order[node_indirect_index]];
                if (!node.clipped_box.available) {
                    continue;
                }
                switch (node.drawable.type)
                {
                case GUI_Drawable_Type::RECTANGLE: {
                    // Rendering test 
                    auto clippedBB = node.clipped_box.value.as_bounding_box();
                    auto bb = node.bounding_box.as_bounding_box();
                    mesh_push_attribute(rectangle_mesh, posSizeCombined, { vec4(bb.min.x, bb.min.y, bb.max.x - bb.min.x, bb.max.y - bb.min.y) });
                    mesh_push_attribute(rectangle_mesh, clippedBoundingBox, { vec4(clippedBB.min.x, clippedBB.min.y, clippedBB.max.x, clippedBB.max.y) });
                    mesh_push_attribute(rectangle_mesh, rendering_core.predefined.color4, { node.drawable.color });
                    mesh_push_attribute(rectangle_mesh, borderColor, { node.drawable.border_color });
                    mesh_push_attribute(rectangle_mesh, borderThicknessEdgeRadius, { vec2(node.drawable.border_thickness, node.drawable.edge_radius) });
                    break;
                }
                case GUI_Drawable_Type::TEXT: {
                    auto bb = node.bounding_box.as_bounding_box();
                    float height = bb.max.y - bb.min.y;
                    float char_width = text_renderer_line_width(imgui.text_renderer, height, 1);
                    String text = node.drawable.text; // Local copy so size can be changed without affecting original code
                    vec4 c = node.drawable.color;
                    text_renderer_add_text(
                        imgui.text_renderer, text, bb.min, Anchor::BOTTOM_LEFT, height, vec3(c.x, c.y, c.z),
                        optional_make_success(node.clipped_box.value.as_bounding_box())
                    );
                    break;
                }
                case GUI_Drawable_Type::NONE: break;
                default: panic("");
                }
            }

            // Add draw commands for batch
            int after_rectangle_count = rectangle_mesh->vertex_count;
            if (after_rectangle_count > before_rectangle_count) {
                render_pass_draw_count(
                    pass_2D, rectangle_shader, rectangle_mesh, Mesh_Topology::POINTS, {},
                    before_rectangle_count, after_rectangle_count - before_rectangle_count
                );
            }
            text_renderer_draw(imgui.text_renderer, pass_2D);
        }
    }

    // Reset node data (Second time, first time is done while removing nodes)
    for (int i = 0; i < nodes.size; i++) {
        auto& node = nodes[i];
        node.hidden = false;
    }
}



// Predefined GUI objects
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

String* gui_store_string(GUI_Handle parent_handle, const char* initial_string) {
    auto node_handle = gui_add_node(parent_handle, gui_size_make_fixed(0.0f), gui_size_make_fixed(0.0f), gui_drawable_make_none());
    gui_node_hide(node_handle);
    if (node_handle.userdata == 0) {
        String* string = new String;
        *string = string_create(initial_string);
        gui_node_set_userdata(
            node_handle, (void*)string,
            [](void* data) -> void {
                String* str = (String*)data;
                string_destroy(str);
                delete str;
            }
        );
        return string;
    }
    return (String*)node_handle.userdata;
}

GUI_Handle gui_push_text(GUI_Handle parent_handle, String text, float text_height_cm = .4f, vec4 color = vec4(0.0f, 0.0f, 0.0f, 1.0f))
{
    const float char_height = convertHeight(text_height_cm, Unit::CENTIMETER);
    const float char_width = text_renderer_line_width(imgui.text_renderer, char_height, 1) + 0.01f;
    return gui_add_node(
        parent_handle,
        gui_size_make_fixed(char_width * text.size),
        gui_size_make_fixed(char_height),
        gui_drawable_make_text(text, color)
    );
}

struct GUI_Window_Info
{
    vec2 pos;
    vec2 size;

    bool drag_started;
    vec2 drag_start_mouse;
    vec2 drag_start_pos;
    vec2 drag_start_size;

    bool move;
    bool resize_right;
    bool resize_left;
    bool resize_top;
    bool resize_bottom;
};

GUI_Handle gui_push_window(GUI_Handle parent_handle, const char* name,
    vec2 initial_pos = convertPoint(vec2(0.0f), Unit::NORMALIZED_SCREEN), vec2 initial_size = vec2(300, 500), Anchor initial_anchor = Anchor::CENTER_CENTER)
{
    // Get window info
    GUI_Window_Info* info = 0;
    {
        GUI_Window_Info initial_info;
        initial_info.drag_started = false;
        initial_info.pos = anchor_switch(initial_pos, initial_size, initial_anchor, Anchor::BOTTOM_LEFT);
        initial_info.size = initial_size;
        initial_info.move = false;
        initial_info.resize_bottom = false;
        initial_info.resize_top = false;
        initial_info.resize_left = false;
        initial_info.resize_right = false;
        info = gui_store_primitive<GUI_Window_Info>(parent_handle, initial_info);
    }

    auto input = imgui.input;
    if (input->client_area_resized) {
        auto& pos = info->pos;
        auto& size = info->size;
        vec2 client_area = vec2(rendering_core.render_information.backbuffer_width, rendering_core.render_information.backbuffer_height);

        size.x = math_minimum(size.x, client_area.x);
        size.y = math_minimum(size.y, client_area.y);
        pos.x = math_clamp(pos.x, 0.0f, client_area.x - size.x);
        pos.y = math_clamp(pos.y, 0.0f, client_area.y - size.y);
    }

    // Create gui nodes
    auto window_handle = gui_add_node(parent_handle, gui_size_make_fixed(info->size.x), gui_size_make_fixed(info->size.y),
        gui_drawable_make_rect(vec4(1), 1, vec4(0, 0, 0, 1))
    );
    gui_node_set_position_fixed(window_handle, info->pos, Anchor::BOTTOM_LEFT, true);
    gui_node_set_layout(window_handle, GUI_Stack_Direction::TOP_TO_BOTTOM);
    gui_node_enable_input(window_handle);
    gui_node_set_padding(window_handle, 1, 1);

    auto header_handle = gui_add_node(window_handle, gui_size_make_fill(), gui_size_make_fit(false), gui_drawable_make_rect(vec4(0.3f, 0.3f, 1.0f, 1.0f)));
    gui_node_set_padding(header_handle, 1, 1);
    gui_node_enable_input(header_handle); // For dragging
    gui_push_text(header_handle, string_create_static(name), .43f);
    // Push header/client area seperator
    gui_add_node(window_handle, gui_size_make_fill(), gui_size_make_fixed(1), gui_drawable_make_rect(vec4(0, 0, 0, 1)));

    // Handle focusing of windows (e.g. window order)
    if (window_handle.first_time_created || ((window_handle.mouse_hovers_child || window_handle.mouse_hover) && input->mouse_pressed[(int)Mouse_Key_Code::LEFT])) {
        gui_node_set_z_index_to_highest(window_handle);
    }

    // Handle user interaction
    bool mouse_down = input->mouse_down[(int)Mouse_Key_Code::LEFT];
    bool mouse_pressed = input->mouse_pressed[(int)Mouse_Key_Code::LEFT];
    vec2 mouse_pos = vec2((float)input->mouse_x, rendering_core.render_information.backbuffer_height - input->mouse_y);

    if (!mouse_down && info->drag_started) {
        info->drag_started = false;
        info->move = false;
        info->resize_right = false;
        info->resize_left = false;
        info->resize_bottom = false;
        info->resize_top = false;
        window_set_cursor_constrain(imgui.window, false);
    }

    // Check for drag start
    auto window_bb = gui_node_get_previous_frame_size(window_handle);
    const float interaction_distance = 5;

    const bool right_border = math_absolute(mouse_pos.x - window_bb.max.x) < interaction_distance;
    const bool left_border = math_absolute(mouse_pos.x - window_bb.min.x) < interaction_distance && !right_border;
    const bool bottom_border = math_absolute(mouse_pos.y - window_bb.min.y) < interaction_distance;
    const bool top_border = math_absolute(mouse_pos.y - window_bb.max.y) < interaction_distance && !bottom_border;

    // Set cursor icon for resize
    if (window_handle.mouse_hover || header_handle.mouse_hover)
    {
        // Handle resize icon
        bool left = info->drag_started ? info->resize_left : left_border;
        bool right = info->drag_started ? info->resize_right : right_border;
        bool top = info->drag_started ? info->resize_top : top_border;
        bool bot = info->drag_started ? info->resize_bottom : bottom_border;

        if (bot) {
            if (left) {
                imgui.cursor_type = Cursor_Icon_Type::SIZE_NORTHEAST;
            }
            else if (right) {
                imgui.cursor_type = Cursor_Icon_Type::SIZE_SOUTHEAST;
            }
            else {
                imgui.cursor_type = Cursor_Icon_Type::SIZE_VERTICAL;
            }
        }
        else if (top) {
            if (left) {
                imgui.cursor_type = Cursor_Icon_Type::SIZE_SOUTHEAST;
            }
            else if (right) {
                imgui.cursor_type = Cursor_Icon_Type::SIZE_NORTHEAST;
            }
            else {
                imgui.cursor_type = Cursor_Icon_Type::SIZE_VERTICAL;
            }
        }
        else if (left || right) {
            imgui.cursor_type = Cursor_Icon_Type::SIZE_HORIZONTAL;
        }
    }

    // Check if drag-and-drop is happening
    if (info->drag_started)
    {
        bool tmp = input->key_down[(int)Key_Code::B];
        vec2 new_pos = info->pos;
        vec2 new_size = info->size;
        if (info->move) {
            new_pos = info->drag_start_pos + (mouse_pos - info->drag_start_mouse);
            // Restrict movement so we cant move windows out of the window
            auto info = rendering_core.render_information;
            new_pos.x = math_maximum(new_pos.x, 0.0f);
            new_pos.y = math_maximum(new_pos.y, 0.0f);
            new_pos.x = math_minimum(new_pos.x, info.backbuffer_width - new_size.x);
            new_pos.y = math_minimum(new_pos.y, info.backbuffer_height - new_size.y);
        }
        else
        {
            if (info->resize_right) {
                new_size.x = math_maximum(10.0f, info->drag_start_size.x + (mouse_pos.x - info->drag_start_mouse.x));
            }
            else if (info->resize_left) {
                new_size.x = math_maximum(10.0f, info->drag_start_size.x - (mouse_pos.x - info->drag_start_mouse.x));
                new_pos.x = info->drag_start_pos.x + (mouse_pos - info->drag_start_mouse).x;
                if (new_size.x != 10.0f) {
                    new_pos.x = info->drag_start_pos.x + (mouse_pos - info->drag_start_mouse).x;
                }
                else {
                    new_pos.x = info->drag_start_pos.x + info->drag_start_size.x - 10.0f;
                }
            }
            if (info->resize_top) {
                new_size.y = math_maximum(10.0f, info->drag_start_size.y + (mouse_pos.y - info->drag_start_mouse.y));
            }
            else if (info->resize_bottom) {
                new_size.y = math_maximum(10.0f, info->drag_start_size.y - (mouse_pos.y - info->drag_start_mouse.y));
                if (new_size.y != 10.0f) {
                    new_pos.y = info->drag_start_pos.y + (mouse_pos - info->drag_start_mouse).y;
                }
                else {
                    new_pos.y = info->drag_start_pos.y + info->drag_start_size.y - 10.0f;
                }
            }
        }
        info->pos = new_pos;
        info->size = new_size;
        gui_node_set_position_fixed(window_handle, info->pos, Anchor::BOTTOM_LEFT);
        gui_node_update_size(window_handle, gui_size_make_fixed(info->size.x), gui_size_make_fixed(info->size.y));
        window_set_cursor_constrain(imgui.window, true);
    }
    else if (mouse_pressed && (window_handle.mouse_hover || header_handle.mouse_hover))
    {
        if (right_border) {
            info->drag_started = true;
            info->resize_right = true;
        }
        else if (left_border) {
            info->drag_started = true;
            info->resize_left = true;
        }
        if (bottom_border) {
            info->drag_started = true;
            info->resize_bottom = true;
        }
        else if (top_border) {
            info->drag_started = true;
            info->resize_top = true;
        }

        if (!info->drag_started && header_handle.mouse_hover) {
            info->drag_started = true;
            info->move = true;
        }

        if (info->drag_started) {
            info->drag_start_pos = info->pos;
            info->drag_start_size = info->size;
            info->drag_start_mouse = mouse_pos;
        }
    }

    return window_handle;
}

bool gui_push_button(GUI_Handle parent_handle, String text)
{
    const vec4 border_color = vec4(vec3(0.2f), 1.0f); // Some shade of gray
    const vec4 normal_color = vec4(vec3(0.8f), 1.0f); // Some shade of gray
    const vec4 hover_color = vec4(vec3(0.5f), 1.0f); // Some shade of gray
    float width = convertWidth(1.0f, Unit::CENTIMETER);

    auto button = gui_add_node(
        parent_handle, gui_size_make_preferred(width * 3, width), gui_size_make_fit(), gui_drawable_make_rect(normal_color, 1, border_color, 5));
    gui_node_set_padding(button, 3, 3);
    gui_node_enable_input(button);

    if (button.mouse_hover) {
        gui_node_update_drawable(button, gui_drawable_make_rect(hover_color, 1, border_color, 5));
    }

    auto text_node = gui_push_text(button, text);
    gui_node_set_alignment(text_node, GUI_Alignment::CENTER);
    return button.mouse_hover && imgui.input->mouse_pressed[(int)Mouse_Key_Code::LEFT];
}

void gui_push_text_edit(GUI_Handle parent_handle, String* string, float text_height_cm = 0.4f)
{
    auto input = imgui.input;
    vec4 normal_bg_color = vec4(vec3(0.8f), 1);
    vec4 highlight_bg_color = vec4(vec3(0.88f), 1);
    vec4 border_color = vec4(0, 0, 0, 1);
    vec4 focus_border_color = vec4(0.7, 0.3, 0.3, 1.0);

    int text_height = convertHeight(text_height_cm, Unit::CENTIMETER);
    float char_width = text_renderer_line_width(imgui.text_renderer, text_height, 1);

    auto container = gui_add_node(parent_handle, gui_size_make_fill(), gui_size_make_fit(),
        gui_drawable_make_rect(normal_bg_color, 1, border_color, 3));
    gui_node_set_padding(container, 2, 2, false);
    gui_node_enable_input(container);
    gui_node_set_layout(container, GUI_Stack_Direction::LEFT_TO_RIGHT);

    if (container.mouse_hover && !gui_node_has_focus(container)) {
        gui_node_update_drawable(container, gui_drawable_make_rect(highlight_bg_color, 1, border_color, 3));
        if (input->mouse_pressed[(int)Mouse_Key_Code::LEFT]) {
            gui_node_set_focus(container);
        }
    }

    if (gui_node_has_focus(container) && !container.mouse_hover && input->mouse_pressed[(int)Mouse_Key_Code::LEFT]) {
        gui_node_remove_focus(container);
    }

    if (gui_node_has_focus(container)) {
        gui_node_update_drawable(container, gui_drawable_make_rect(highlight_bg_color, 2, focus_border_color, 3));
        for (int i = 0; i < input->key_messages.size; i++) {
            auto& msg = input->key_messages[i];
            if (msg.key_code == Key_Code::BACKSPACE && msg.key_down) {
                if (string->size > 0) {
                    string_remove_character(string, string->size - 1);
                }
            }
            if (msg.key_code == Key_Code::U && msg.key_down && msg.ctrl_down) {
                string_reset(string);
            }
            else if (msg.key_code == Key_Code::RETURN && msg.key_down) {
                gui_node_remove_focus(container);
                break;
            }
            else if (msg.character != 0) {
                string_append_character(string, msg.character);
            }
        }
    }
    auto text = gui_add_node(container, gui_size_make_fixed(string->size * char_width), gui_size_make_fixed(text_height), gui_drawable_make_text(*string));
    if (gui_node_has_focus(container)) {
        gui_add_node(container, gui_size_make_fixed(2), gui_size_make_fill(), gui_drawable_make_rect(vec4(0, 0, 0, 1)));
    }
}

// Returns true if the value was toggled
bool gui_push_toggle(GUI_Handle parent_handle, Input* input, bool* value)
{
    const vec4 border_color = vec4(vec3(0.1f), 1.0f); // Some shade of gray
    const vec4 normal_color = vec4(vec3(0.8f), 1.0f); // Some shade of gray
    const vec4 hover_color = vec4(vec3(0.5f), 1.0f); // Some shade of gray
    float height = convertHeight(.4f, Unit::CENTIMETER);
    auto border = gui_add_node(parent_handle, gui_size_make_fit(), gui_size_make_fit(), gui_drawable_make_rect(border_color));
    gui_node_set_padding(border, 1, 1);
    gui_node_enable_input(border);

    auto center = gui_add_node(border, gui_size_make_fixed(height), gui_size_make_fixed(height), gui_drawable_make_rect(hover_color));
    bool pressed = false;
    if (border.mouse_hover) {
        gui_node_update_drawable(center, gui_drawable_make_rect(hover_color));
        pressed = input->mouse_pressed[(int)Mouse_Key_Code::LEFT];
    }
    if (pressed) {
        *value = !*value;
    }
    if (*value) {
        auto text = gui_push_text(center, string_create_static("x"), .4f, vec4(1.0f, 0.0f, 0.0f, 1.0f));
        gui_node_set_alignment(text, GUI_Alignment::CENTER);
    }
    return pressed;
}

GUI_Handle gui_push_text_description(GUI_Handle parent_handle, const char* text)
{
    auto main_container = gui_add_node(parent_handle, gui_size_make_fill(), gui_size_make_fit(), gui_drawable_make_none());
    gui_node_set_layout(main_container, GUI_Stack_Direction::LEFT_TO_RIGHT);
    gui_push_text(main_container, string_create_static(text));

    auto container = gui_add_node(main_container, gui_size_make_fill(), gui_size_make_fit(), gui_drawable_make_none());
    gui_node_set_layout(container, GUI_Stack_Direction::RIGHT_TO_LEFT);
    return container;
}



void draw_example_gui(Input* input)
{
    const vec4 white = vec4(1.0f);
    const vec4 black = vec4(vec3(0.0f), 1.0f);
    const vec4 red = vec4(vec3(1, 0, 0), 1.0f);
    const vec4 green = vec4(vec3(0, 1, 0), 1.0f);
    const vec4 blue = vec4(vec3(0, 0, 1), 1.0f);
    const vec4 cyan = vec4(vec3(0, 1, 1), 1.0f);
    const vec4 yellow = vec4(vec3(1, 1, 0), 1.0f);
    const vec4 magenta = vec4(vec3(1, 0, 1), 1.0f);
    const vec4 gray = vec4(vec3(0.3f), 1.0f);

    static bool show_config_gui = false;
    if (input->key_pressed[(int)Key_Code::Z] && imgui.focused_node == 0) {
        show_config_gui = !show_config_gui;
    }

    static bool input_hover = false;
    static bool z_test = false;
    static bool empty_window = false;
    static bool stacking = false;
    static bool toggle_thing = false;
    static bool layout_window = false;
    static bool padding_test = false;
    static bool preferred_test = false;

    auto window = gui_push_window(imgui.root_handle, "Test");
    String* str = gui_store_string(window, "");
    gui_push_text_edit(window, str);

    if (show_config_gui) {
        gui_matching_add_checkpoint_name("Config GUI");
        auto window = gui_push_window(imgui.root_handle, "Config_Window");

        gui_push_toggle(gui_push_text_description(window, "Input hover"), input, &input_hover);
        gui_push_toggle(gui_push_text_description(window, "Z-Test"), input, &z_test);
        gui_push_toggle(gui_push_text_description(window, "Empty"), input, &empty_window);
        gui_push_toggle(gui_push_text_description(window, "Stacking"), input, &stacking);
        gui_push_toggle(gui_push_text_description(window, "Toggle-Thing"), input, &toggle_thing);
        gui_push_toggle(gui_push_text_description(window, "Layout"), input, &layout_window);
        gui_push_toggle(gui_push_text_description(window, "Padding"), input, &padding_test);
        gui_push_toggle(gui_push_text_description(window, "Preferred"), input, &preferred_test);
    }

    if (preferred_test) {
        gui_matching_add_checkpoint_name("Preferred GUI");
        auto window = gui_push_window(imgui.root_handle, "Preferred");
        auto container = gui_add_node(window, gui_size_make_fill(), gui_size_make_fill(), gui_drawable_make_rect(white));
        gui_node_set_layout(container, GUI_Stack_Direction::LEFT_TO_RIGHT);
        gui_node_set_padding(container, 2, 2, true);
        gui_add_node(container, gui_size_make_fixed(50), gui_size_make_fill(), gui_drawable_make_rect(gray, 1, black));
        gui_add_node(container, gui_size_make(60, true, true, false, 0), gui_size_make_fill(), gui_drawable_make_rect(red, 1, black));
        gui_add_node(container, gui_size_make(20, true, true, false, 0), gui_size_make_fill(), gui_drawable_make_rect(red, 1, black));
        gui_add_node(container, gui_size_make(60, true, true, false, 100), gui_size_make_fill(), gui_drawable_make_rect(green, 1, black));
        gui_add_node(container, gui_size_make(20, true, true, false, 100), gui_size_make_fill(), gui_drawable_make_rect(green, 1, black));
        gui_add_node(container, gui_size_make(20, true, true, false, 100), gui_size_make_fill(), gui_drawable_make_rect(green, 1, black));
    }

    if (padding_test) {
        gui_matching_add_checkpoint_name("Padding test");
        auto container = gui_add_node(imgui.root_handle, gui_size_make_fixed(400), gui_size_make_fixed(400), gui_drawable_make_rect(white));
        gui_node_set_padding(container, 2, 2, true);
        gui_node_set_position_fixed(container, vec2(0, 0), Anchor::CENTER_CENTER);

        gui_add_node(container, gui_size_make_fill(), gui_size_make_fixed(15), gui_drawable_make_rect(green));
        gui_add_node(container, gui_size_make_fill(), gui_size_make_fixed(15), gui_drawable_make_rect(red));
        gui_add_node(container, gui_size_make_fill(), gui_size_make_fixed(15), gui_drawable_make_rect(blue));
        gui_add_node(container, gui_size_make_fill(), gui_size_make_fixed(15), gui_drawable_make_rect(magenta));
        gui_add_node(container, gui_size_make_fill(), gui_size_make_fixed(15), gui_drawable_make_rect(yellow));
        gui_add_node(container, gui_size_make_fill(), gui_size_make_fill(), gui_drawable_make_rect(gray));
    }

    if (input_hover)
    {
        gui_matching_add_checkpoint_name("Input hover");

        auto canvas = gui_add_node(imgui.root_handle, gui_size_make_fixed(250), gui_size_make_fixed(250), gui_drawable_make_rect(red));
        gui_node_set_position_fixed(canvas, vec2(0), Anchor::CENTER_CENTER);
        gui_node_enable_input(canvas);
        if (canvas.mouse_hover) {
            gui_node_update_drawable(canvas, gui_drawable_make_rect(green));
        }
        else if (canvas.mouse_hovers_child) {
            gui_node_update_drawable(canvas, gui_drawable_make_rect(white));
        }

        auto c2 = gui_add_node(canvas, gui_size_make_fixed(50), gui_size_make_fixed(50), gui_drawable_make_rect(blue));
        gui_node_set_position_fixed(c2, vec2(0), Anchor::CENTER_CENTER);
        gui_node_enable_input(c2);
        if (c2.mouse_hover) {
            gui_node_update_drawable(c2, gui_drawable_make_rect(yellow));
        }

        auto c4 = gui_add_node(imgui.root_handle, gui_size_make_fixed(50), gui_size_make_fixed(50), gui_drawable_make_rect(blue));
        gui_node_set_position_fixed(c4, vec2(55, 0), Anchor::CENTER_CENTER),
            gui_node_enable_input(c4);
        if (c4.mouse_hover) {
            gui_node_update_drawable(c4, gui_drawable_make_rect(yellow));
        }

        auto time = rendering_core.render_information.current_time_in_seconds;
        auto c3 = gui_add_node(imgui.root_handle, gui_size_make_fixed(50), gui_size_make_fixed(50), gui_drawable_make_rect(magenta));
        gui_node_set_position_fixed(c3, vec2(math_sine(time) * 400, 0.f), Anchor::CENTER_CENTER),
            gui_node_enable_input(c3);
        if (c3.mouse_hover) {
            gui_node_update_drawable(c3, gui_drawable_make_rect(gray));
        }
    }

    if (z_test) {
        gui_matching_add_checkpoint_name("Z test ");
        auto canvas = gui_add_node(imgui.root_handle, gui_size_make_fixed(250), gui_size_make_fixed(250), gui_drawable_make_rect(gray));
        gui_node_set_position_fixed(canvas, vec2(0), Anchor::CENTER_CENTER);

        auto last = gui_add_node(imgui.root_handle, gui_size_make_fixed(250), gui_size_make_fixed(250), gui_drawable_make_rect(yellow));
        gui_node_set_position_fixed(last, vec2(-235, -20), Anchor::CENTER_CENTER);

        vec2 offset(0);
        last = gui_add_node(canvas, gui_size_make_fixed(50), gui_size_make_fixed(50), gui_drawable_make_rect(vec4(0, 0, 1, 0.5f)));
        gui_node_set_position_fixed(last, vec2(0) + offset, Anchor::CENTER_CENTER);
        last = gui_add_node(canvas, gui_size_make_fixed(50), gui_size_make_fixed(50), gui_drawable_make_rect(green));
        gui_node_set_position_fixed(last, vec2(15) + offset, Anchor::CENTER_CENTER);
        last = gui_add_node(canvas, gui_size_make_fixed(50), gui_size_make_fixed(50), gui_drawable_make_rect(red));
        gui_node_set_position_fixed(last, vec2(-15, 4) + offset, Anchor::CENTER_CENTER);

        offset = vec2(-95, 0);
        last = gui_add_node(canvas, gui_size_make_fixed(50), gui_size_make_fixed(50), gui_drawable_make_rect(vec4(0, 0, 1, 0.5f)));
        gui_node_set_position_fixed(last, vec2(0) + offset, Anchor::CENTER_CENTER);
        gui_node_set_z_index(last, 2);
        last = gui_add_node(canvas, gui_size_make_fixed(50), gui_size_make_fixed(50), gui_drawable_make_rect(green));
        gui_node_set_position_fixed(last, vec2(15) + offset, Anchor::CENTER_CENTER);
        gui_node_set_z_index(last, 1);
        last = gui_add_node(canvas, gui_size_make_fixed(50), gui_size_make_fixed(50), gui_drawable_make_rect(red));
        gui_node_set_position_fixed(last, vec2(-15, 4) + offset, Anchor::CENTER_CENTER);
        gui_node_set_z_index(last, 0);
    }

    if (empty_window)
    {
        gui_matching_add_checkpoint_name("Empty window");
        auto window = gui_push_window(imgui.root_handle, "Test");
        // gui_push_text(window, string_create_static("Text"), 2.0f);
        // gui_push_text(window, string_create_static("Take 2"), 2.0f);
    }

    if (stacking)
    {
        gui_matching_add_checkpoint_name("Stacking text");
        auto window = gui_add_node(imgui.root_handle, gui_size_make_fixed(300), gui_size_make_fixed(300), gui_drawable_make_none());
        gui_node_set_position_fixed(window, vec2(0), Anchor::CENTER_CENTER);
        gui_node_set_padding(window, 0, 3, true);

        auto top_row = gui_add_node(window, gui_size_make_fill(), gui_size_make_fill(), gui_drawable_make_none());
        gui_node_set_layout(top_row, GUI_Stack_Direction::LEFT_TO_RIGHT);
        gui_node_set_padding(top_row, 3, 0, true);
        auto bot_row = gui_add_node(window, gui_size_make_fill(), gui_size_make_fill(), gui_drawable_make_none());
        gui_node_set_layout(bot_row, GUI_Stack_Direction::LEFT_TO_RIGHT);
        gui_node_set_padding(bot_row, 3, 0, true);
        auto win0 = gui_add_node(top_row, gui_size_make_fill(), gui_size_make_fill(), gui_drawable_make_rect(white));
        gui_node_set_layout(win0, GUI_Stack_Direction::TOP_TO_BOTTOM, GUI_Alignment::CENTER);
        auto win1 = gui_add_node(top_row, gui_size_make_fill(), gui_size_make_fill(), gui_drawable_make_rect(white));
        gui_node_set_layout(win1, GUI_Stack_Direction::LEFT_TO_RIGHT, GUI_Alignment::CENTER);
        auto win2 = gui_add_node(bot_row, gui_size_make_fill(), gui_size_make_fill(), gui_drawable_make_rect(white));
        gui_node_set_layout(win2, GUI_Stack_Direction::RIGHT_TO_LEFT, GUI_Alignment::CENTER);
        auto win3 = gui_add_node(bot_row, gui_size_make_fill(), gui_size_make_fill(), gui_drawable_make_rect(white));
        gui_node_set_layout(win3, GUI_Stack_Direction::BOTTOM_TO_TOP, GUI_Alignment::CENTER);

        GUI_Handle handles[] = { win0, win1, win2, win3 };
        for (int i = 0; i < 4; i++) {
            gui_add_node(handles[i], gui_size_make_fixed(20), gui_size_make_fixed(20), gui_drawable_make_rect(red));
            gui_add_node(handles[i], gui_size_make_fixed(30), gui_size_make_fixed(30), gui_drawable_make_rect(green));
            gui_add_node(handles[i], gui_size_make_fixed(40), gui_size_make_fixed(40), gui_drawable_make_rect(blue));
        }
    }

    if (toggle_thing)
    {
        gui_matching_add_checkpoint_name("Toggle things");
        static bool toggle = false;
        if (input->key_pressed[(int)Key_Code::T]) {
            toggle = !toggle;
            logg("Toggle switched to: %s\n", toggle ? "true" : "false");
        }
        float t = rendering_core.render_information.current_time_in_seconds;
        auto& info = rendering_core.render_information;
        vec2 mouse_pos = vec2((float)input->mouse_x, info.backbuffer_height - input->mouse_y);
        float a = math_sine(t) * 0.5f + 0.5f;
        a = (float)input->mouse_x / rendering_core.render_information.backbuffer_width;
        auto window = gui_add_node(imgui.root_handle, gui_size_make_fixed(60), gui_size_make_fixed(60), gui_drawable_make_rect(vec4(1.0f, 0.0f, 1.0f, 1.0f)));
        gui_node_set_position_fixed(window, mouse_pos - vec2(30, 30), Anchor::BOTTOM_LEFT);

        auto bar = gui_add_node(window, gui_size_make_fill(), gui_size_make_fixed(30.0f), gui_drawable_make_rect(vec4(0.3f, 0.3f, 1.0f, 1.0f)));
        gui_push_text(bar, string_create_static("HEllo!"));
    }

    if (layout_window)
    {
        gui_matching_add_checkpoint_name("Layout test window");
        // Make the rectangle a constant pixel size:
        int pixel_width = 100;
        int pixel_height = 100;

        auto window = gui_push_window(imgui.root_handle, "Test window");

        // Add number couter with userdata
        auto space = gui_add_node(window, gui_size_make_fill(), gui_size_make_fill(), gui_drawable_make_rect(cyan));
        gui_node_set_layout(space, GUI_Stack_Direction::TOP_TO_BOTTOM, GUI_Alignment::CENTER);
        gui_node_set_padding(space, 1, 1, true);
        {
            bool* value = gui_store_primitive<bool>(space, false);
            gui_push_toggle(space, input, value);
            auto toggle_area = gui_add_node(space, gui_size_make_fill(), gui_size_make_fill(), gui_drawable_make_none());
            gui_node_set_layout(toggle_area, GUI_Stack_Direction::TOP_TO_BOTTOM, GUI_Alignment::CENTER);
            if (!*value) {
                gui_node_hide(toggle_area);
                gui_node_keep_children_even_if_not_referenced(toggle_area);
            }
            else
            {
                bool pressed = gui_push_button(toggle_area, string_create_static("Press me!"));
                int* value = gui_store_primitive<int>(toggle_area, 0);
                if (pressed) {
                    *value += 1;
                }
                String tmp = string_create_formated("%d", *value);
                SCOPE_EXIT(string_destroy(&tmp));
                gui_push_text(toggle_area, tmp);
            }
        }

        auto right = gui_push_text(window, string_create_static("Right"));
        gui_node_set_alignment(right, GUI_Alignment::MAX);

        auto horizontal = gui_add_node(window, gui_size_make_fill(true), gui_size_make_fill(true), gui_drawable_make_none());
        gui_node_set_layout(horizontal, GUI_Stack_Direction::LEFT_TO_RIGHT);
        gui_add_node(horizontal, gui_size_make_fill(true), gui_size_make_fill(true), gui_drawable_make_rect(gray));
        auto horizontal2 = gui_add_node(horizontal, gui_size_make_fill(true), gui_size_make_fill(true), gui_drawable_make_none());
        gui_node_set_layout(horizontal2, GUI_Stack_Direction::LEFT_TO_RIGHT);
        gui_add_node(horizontal2, gui_size_make_fill(true), gui_size_make_fill(true), gui_drawable_make_rect(yellow));
        gui_add_node(horizontal2, gui_size_make_fill(true), gui_size_make_fill(true), gui_drawable_make_rect(green));

        auto center = gui_push_text(window, string_create_static("Center with very long name that you shouldn't forget!"));
        gui_node_set_alignment(center, GUI_Alignment::CENTER);

        gui_add_node(window, gui_size_make_fill(true), gui_size_make_fill(true), gui_drawable_make_rect(magenta));
        gui_push_text(window, string_create_static("LEFT"));
    }
}



// GUI END  




struct Ring_Buffer
{
    double values[120];
    int next_free;

    double average;
    double max;
    double min;
    double standard_deviation;
};

Ring_Buffer ring_buffer_make(double initial_value)
{
    Ring_Buffer result;
    for (int i = 0; i < 120; i++) {
        result.values[i] = 0;
    }
    result.next_free = 0;
    return result;
}

void ring_buffer_update_stats(Ring_Buffer& buffer)
{
    buffer.average = 0;
    buffer.max = -1000000.0;
    buffer.min = 1000000.0;
    for (int i = 0; i < 120; i++) {
        buffer.average += buffer.values[i];
        buffer.max = math_maximum(buffer.max, buffer.values[i]);
        buffer.min = math_minimum(buffer.max, buffer.values[i]);
    }
    buffer.average = buffer.average / 120.0;
    // Calculate variance
    buffer.standard_deviation = 0.0;
    for (int i = 0; i < 120; i++) {
        double diff = buffer.values[i] - buffer.average;
        buffer.standard_deviation += diff * diff;
    }
    buffer.standard_deviation = math_square_root(buffer.standard_deviation);
}

void ring_buffer_set_value(Ring_Buffer& buffer, double value) {
    buffer.values[buffer.next_free] = value;
    buffer.next_free = (buffer.next_free + 1) % 120;
}



void render_rework()
{
    Window* window = window_create("Test", 0);
    SCOPE_EXIT(window_destroy(window));
    Window_State* window_state = window_get_window_state(window);
    rendering_core_initialize(window_state->width, window_state->height, window_state->dpi);
    SCOPE_EXIT(rendering_core_destroy());

    Timer timer = timer_make();

    Camera_3D* camera = camera_3D_create(math_degree_to_radians(90), 0.1f, 100.0f);
    SCOPE_EXIT(camera_3D_destroy(camera));
    Camera_Controller_Arcball camera_controller_arcball;
    {
        window_set_cursor_constrain(window, false);
        window_set_cursor_visibility(window, true);
        window_set_cursor_reset_into_center(window, false);
        camera_controller_arcball = camera_controller_arcball_make(vec3(0.0f), 2.0f);
        camera->position = vec3(0, 0, 1.0f);
    }

    // Set Window/Rendering Options
    {
        window_load_position(window, "window_pos.set");
        window_set_vsync(window, false);
        opengl_state_set_clear_color(vec4(0.0f));
    }

    Texture_Bitmap bitmap = texture_bitmap_create_test_bitmap(64);
    Texture* texture = texture_create_from_texture_bitmap(&bitmap, false);
    Texture_Bitmap bitmap2 = texture_bitmap_create_empty(32, 32, 3);
    auto random = random_make_time_initalized();
    for (int i = 0; i < 32 * 32 * 3; i += 3) {
        bitmap2.data[i + 0] = (byte)random_next_u32(&random);
        bitmap2.data[i + 1] = (byte)random_next_u32(&random);
        bitmap2.data[i + 2] = (byte)random_next_u32(&random);
    }
    Texture* texture2 = texture_create_from_texture_bitmap(&bitmap2, false);

    Text_Renderer* text_renderer = text_renderer_create_from_font_atlas_file("resources/fonts/glyph_atlas.atlas");
    SCOPE_EXIT(text_renderer_destroy(text_renderer));

    Renderer_2D* renderer_2D = renderer_2D_create(text_renderer);
    SCOPE_EXIT(renderer_2D_destroy(renderer_2D));

    imgui_initialize(text_renderer, window, window_get_input(window));
    SCOPE_EXIT(imgui_destroy());

    // Window Loop
    double game_start_time = timer_current_time_in_seconds(&timer);
    double frame_start_time = game_start_time;
    while (true)
    {
        double now = timer_current_time_in_seconds(&timer);
        double tslf = now - frame_start_time;
        frame_start_time = now;

        // Handle input
        Input* input = window_get_input(window);
        double handle_message_time;
        SCOPE_EXIT(input_reset(input));
        {
            // Input and Logic
            double now = timer_current_time_in_seconds(&timer);
            int msg_count = 0;
            if (!window_handle_messages(window, false, &msg_count)) {
                break;
            }
            handle_message_time = timer_current_time_in_seconds(&timer) - now;
            if (input->close_request_issued || input->key_pressed[(int)Key_Code::ESCAPE]) {
                window_save_position(window, "window_pos.set");
                window_close(window);
                break;
            }
            if (input->key_pressed[(int)Key_Code::F11]) {
                Window_State* state = window_get_window_state(window);
                window_set_fullscreen(window, !state->fullscreen);
            }

            camera_controller_arcball_update(&camera_controller_arcball, camera, input, window_state->width, window_state->height);
        }

        // Rendering
        {
            rendering_core_prepare_frame(timer_current_time_in_seconds(&timer), window_state->width, window_state->height);
            now = timer_current_time_in_seconds(&timer);

            draw_example_gui(input);
            gui_update(input);

            renderer_2D_reset(renderer_2D);
            text_renderer_reset(text_renderer);
        }

        rendering_core_render(camera, Framebuffer_Clear_Type::COLOR_AND_DEPTH);
        window_swap_buffers(window);
        glFlush();

        // Sleep + Timing
        {
            // Sleep
            const int TARGET_FPS = 60;
            const double SECONDS_PER_FRAME = 1.0 / TARGET_FPS;
            timer_sleep_until(&timer, frame_start_time + SECONDS_PER_FRAME);
        }
    }
}