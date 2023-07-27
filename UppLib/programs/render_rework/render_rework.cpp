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

    /* So what coordinates do we have here:
        * pixel coordinates             (0 - bb_width)
            + Integer precision
            + Absolute
            - Resolution dependent (E.g. no scaling)
        * normalized screen coordinates (-1.0f - 1.0f)
            o Required for rendering
            - Introduces stretching on non 1:1 aspect rations
            + Resolution Independent
        * Aspect-Ratio normalized coordinates (4 - 3 --> 4/3 - 1)
            o Normalized to either height, width, max or min of dimensions
            + No stretching
            - Arbitrary boundaries on the side of windows (e.g. at 1.34343 is the left boundary)
        * Pixel coordinates are almost fine except that they are integers.
        * I guess depending on the use case you want other sizes...
        * Conversion of widths and heights is different when we have different aspect ratios
        Some things we may want:
            - Size dependent on Window-Size (E.g. text scaling with Screen size for large Text and UI-elements in Games)
            - Size dependent on Screen-Size (For things that should always be e.g. readable, like Text in information UIs)
            - Fixed pixel size:
                Not flexible on high resolution devices, everything will be smaller, and on low-res monitors the size is too large.

       When sending the data to the gpu we want to have normalized screen coordinates
     */

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


struct GUI_Size
{
    bool is_absolute; // Size is given by bounding_rectangle, not calculated by parent
    Bounding_Box2 absolute_box;
    // If size is not absolute
    float min_size[2]; // Minimum size in x/y
    bool fill[2];
};

GUI_Size gui_size_make_absolute(Bounding_Box2 absolute_box) {
    GUI_Size size;
    size.is_absolute = true;
    size.absolute_box = absolute_box;
    // Initialize unused with something
    size.min_size[0] = 0.0f;
    size.min_size[1] = 0.0f;
    size.fill[0] = false;
    size.fill[1] = false;
    return size;
}

GUI_Size gui_size_make_min(vec2 min_size) {
    GUI_Size size;
    size.is_absolute = false;
    size.min_size[0] = min_size.x;
    size.min_size[1] = min_size.y;
    size.fill[0] = false;
    size.fill[1] = false;
    // Initialize unused with something
    size.absolute_box = bounding_box_2_make_min_max(vec2(0.0f), vec2(0.0f));
    return size;
}

GUI_Size gui_size_make_fill(bool fill_x, bool fill_y) {
    GUI_Size size;
    size.is_absolute = false;
    size.fill[0] = fill_x;
    size.fill[1] = fill_y;
    // Initialize unused with something
    size.absolute_box = bounding_box_2_make_min_max(vec2(0.0f), vec2(0.0f));
    size.min_size[0] = 0.0f;
    size.min_size[1] = 0.0f;
    return size;
}



enum class GUI_Align
{
    MIN, // in x this is left_aligned, in y bottom aligned
    MAX,
    CENTER,
};

struct GUI_Layout
{
    int stack_dimension; // either 0 or 1
    GUI_Align child_alignment;
    float padding[2];
};

GUI_Layout gui_layout_make(bool stack_vertical = false, GUI_Align align = GUI_Align::MIN, vec2 padding = vec2(0.0f))
{
    GUI_Layout layout;
    layout.child_alignment = align;
    layout.padding[0] = padding.x;
    layout.padding[1] = padding.y;
    layout.stack_dimension = stack_vertical ? 0 : 1;
    return layout;
}



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

GUI_Drawable gui_drawable_make_rect(vec4 color) {
    GUI_Drawable drawable;
    drawable.type = GUI_Drawable_Type::RECTANGLE;
    drawable.color = color;
    drawable.text = string_create_static("");
    return drawable;
}

void gui_drawable_destroy(GUI_Drawable& drawable) {
    if (drawable.type == GUI_Drawable_Type::TEXT) {
        string_destroy(&drawable.text);
    }
}


typedef void (*gui_userdata_destroy_fn)(void* userdata);

// GUI Hierarchy
struct GUI_Node
{
    GUI_Size size;
    GUI_Layout layout;
    GUI_Drawable drawable;

    // Input
    bool receives_input;
    bool mouse_hover;

    // Userdata
    void* userdata;
    gui_userdata_destroy_fn userdata_destroy_fn;

    // Stuff calculated during layout
    Bounding_Box2 bounding_box;
    Optional<Bounding_Box2> clipped_box; // Hierarchical clipping from parent
    float min_size_with_children[2];

    // Infos for matching
    bool referenced_this_frame; // Node and all child nodes will be removed at end of frame if not referenced 
    int traversal_next_child; // Reset each frame, used to match nodes

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
    void* userdata;
};

struct GUI_Renderer
{
    Text_Renderer* text_renderer;
    Dynamic_Array<GUI_Node> nodes;
    GUI_Handle root_handle;
};

GUI_Renderer gui_renderer_initialize(Text_Renderer* text_renderer)
{
    auto& pre = rendering_core.predefined;

    GUI_Renderer result;
    result.text_renderer = text_renderer;
    result.nodes = dynamic_array_create_empty<GUI_Node>(1);
    result.root_handle.index = 0;
    result.root_handle.mouse_hover = false;
    
    // Push root node
    GUI_Node root;
    root.bounding_box = bounding_box_2_convert(bounding_box_2_make_anchor(vec2(0.0f), vec2(2.0f), Anchor::CENTER_CENTER), Unit::NORMALIZED_SCREEN);
    root.referenced_this_frame = true;
    root.index_first_child = -1;
    root.index_last_child = -1;
    root.index_parent = -1;
    root.index_next_node = -1;
    root.traversal_next_child = -1;
    root.drawable = gui_drawable_make_none();
    root.size = gui_size_make_absolute(root.bounding_box);
    root.layout = gui_layout_make();
    root.receives_input = false;
    dynamic_array_push_back(&result.nodes, root);

    return result;
}

void gui_renderer_destroy(GUI_Renderer* renderer) {
    for (int i = 0; i < renderer->nodes.size; i++) {
        gui_node_destroy(renderer->nodes[i]);
    }
    dynamic_array_destroy(&renderer->nodes);
}

GUI_Handle gui_add_node(GUI_Renderer* renderer, GUI_Handle parent_handle, GUI_Layout layout, GUI_Size size, GUI_Drawable drawable, bool receives_input) 
{
    auto& nodes = renderer->nodes;

    // Node-Matching (Create new node or reuse node from last frame)
    int node_index = nodes[parent_handle.index].traversal_next_child;
    bool create_new_node = node_index == -1;
    if (create_new_node) { // No match found from previous frame, create new node
        GUI_Node node;
        node.index_parent = parent_handle.index;
        node.index_first_child = -1;
        node.index_last_child = -1;
        node.index_next_node = -1;
        node.traversal_next_child = -1;
        node.mouse_hover = false;
        node.userdata = 0;
        node.userdata_destroy_fn = 0;
        dynamic_array_push_back(&renderer->nodes, node);
        node_index = renderer->nodes.size - 1;

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

    // Update node data
    {
        auto& node = renderer->nodes[node_index];
        node.referenced_this_frame = true;
        node.layout = layout;
        node.size = size;
        node.receives_input = receives_input;

        // Special handeling for text drawable, to avoid memory allocations
        if (!create_new_node && node.drawable.type == GUI_Drawable_Type::TEXT) {
            if (drawable.type == GUI_Drawable_Type::TEXT) {
                string_set_characters(&node.drawable.text, drawable.text.characters);
                node.drawable.color = drawable.color;
            }
            else {
                gui_drawable_destroy(node.drawable);
                node.drawable = drawable;
            }
        }
        else {
            node.drawable = drawable;
        }
    }

    // Update next traversal
    nodes[parent_handle.index].traversal_next_child = renderer->nodes[node_index].index_next_node;

    GUI_Handle handle;
    handle.index = node_index;
    handle.mouse_hover = nodes[node_index].mouse_hover;
    handle.userdata = nodes[node_index].userdata;
    return handle;
}

void gui_set_drawable(GUI_Renderer* renderer, GUI_Handle handle, GUI_Drawable drawable) {
    if (handle.index == 0) {
        panic("Cannot update root node!");
    }
    renderer->nodes[handle.index].drawable = drawable;
}

void gui_set_size(GUI_Renderer* renderer, GUI_Handle handle, GUI_Size size) {
    if (handle.index == 0) {
        panic("Cannot update root node!");
    }
    renderer->nodes[handle.index].size = size;
}

void gui_set_userdata(GUI_Renderer* renderer, GUI_Handle& handle, void* userdata, gui_userdata_destroy_fn destroy_fn) {
    if (handle.index == 0) {
        panic("Cannot update root node!");
    }
    auto& node = renderer->nodes[handle.index];
    if (node.userdata != 0) {
        assert(node.userdata_destroy_fn != 0, "Since userdata should always be allocated dynamically, there should always be a destroy!");
        node.userdata_destroy_fn(node.userdata);
    }
    node.userdata = userdata;
    node.userdata_destroy_fn = destroy_fn;
    handle.userdata = userdata;
}

void gui_update_nodes_recursive(GUI_Renderer* renderer, Array<int> new_node_indices, int node_index, int& next_free_node_index)
{
    auto& nodes = renderer->nodes;
    auto& node = nodes[node_index];

    // Check if node is parent
    if (node_index == 0) {
        new_node_indices[0] = 0;
        next_free_node_index = 1;
    }
    // Check if node should be deleted
    else if (!node.referenced_this_frame || new_node_indices[node.index_parent] == -1) {
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
        int child_index = node.index_first_child;
        while (child_index != -1) {
            auto& child = nodes[child_index];
            SCOPE_EXIT(child_index = child.index_next_node);
            gui_update_nodes_recursive(renderer, new_node_indices, child_index, next_free_node_index);
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

    // Reset node data
    node.referenced_this_frame = false;
    node.traversal_next_child = node.index_first_child;
    node.mouse_hover = false;
}

void gui_layout_calculate_min_size(GUI_Renderer* renderer, int node_index)
{
    auto& nodes = renderer->nodes;
    auto& node = nodes[node_index];

    // Calculate min-sizes of all children
    for (int dim = 0; dim < 2; dim++) {
        node.min_size_with_children[dim] = 0.0f;
    }
    int child_index = node.index_first_child;
    while (child_index != -1) {
        auto& child_node = nodes[child_index];
        SCOPE_EXIT(child_index = child_node.index_next_node);
        gui_layout_calculate_min_size(renderer, child_index);
        for (int dim = 0; dim < 2; dim++) {
            if (dim == node.layout.stack_dimension) {
                node.min_size_with_children[dim] += child_node.min_size_with_children[dim];
            }
            else {
                node.min_size_with_children[dim] = math_maximum(node.min_size_with_children[dim], child_node.min_size_with_children[dim]);
            }
        }
    }

    // Add padding and own minimum size
    for (int dim = 0; dim < 2; dim++) {
        node.min_size_with_children[dim] = math_maximum(
            node.min_size_with_children[dim] + node.layout.padding[dim] * 2.0f,
            node.size.min_size[dim]
        );
    }
}

void gui_layout_layout_children(GUI_Renderer* renderer, int node_index)
{
    auto& nodes = renderer->nodes;
    auto& node = nodes[node_index];
    auto& layout = node.layout;

    // Set my own size if i'm an absolute unit, otherwise it was set by parent (Note: This is slightly confusing because root also needs to work)
    if (node.size.is_absolute) {
        node.bounding_box = node.size.absolute_box;
    }
    const float my_size[2] = { node.bounding_box.max.x - node.bounding_box.min.x, node.bounding_box.max.y - node.bounding_box.min.y };

    // Calculated clipped bounding box
    if (node.index_parent != -1) {
        const auto& parent_node = nodes[node.index_parent];
        if (parent_node.clipped_box.available) {
            node.clipped_box = bounding_box_2_union(node.bounding_box, parent_node.clipped_box.value);
        }
        else {
            node.clipped_box.available = false;
        }
    }
    else {
        node.clipped_box = optional_make_success(node.bounding_box);
    }

    // Calculate additional size for all fill children
    bool size_for_fill_available = false;
    float size_for_fill = 0.0f;
    {
        // Get number of children who want to fill in stacking direction
        int fill_child_count = 0;
        int child_index = node.index_first_child;
        float non_fill_size = 0.0f;
        while (child_index != -1) {
            auto& child_node = nodes[child_index];
            SCOPE_EXIT(child_index = child_node.index_next_node);
            if (child_node.size.is_absolute) {
                continue;
            }
            if (child_node.size.fill[layout.stack_dimension]) {
                fill_child_count += 1;
            }
            else {
                non_fill_size += child_node.min_size_with_children[layout.stack_dimension];
            }
        }

        if (my_size[layout.stack_dimension] - node.min_size_with_children[layout.stack_dimension] > 0) {
            size_for_fill_available = true;
            size_for_fill = (my_size[layout.stack_dimension] - non_fill_size) / fill_child_count;
        }
    }

    // Set bounding_boxes for all children
    const float stack_sign = layout.stack_dimension == 0 ? +1.0f : -1.0f; // Stack downward if we stack in y
    float stack_cursor = layout.stack_dimension == 0 ? node.bounding_box.min.x : node.bounding_box.max.y;
    stack_cursor += layout.padding[layout.stack_dimension] * stack_sign;
    const float min[2] = { node.bounding_box.min.x, node.bounding_box.min.y };
    const float max[2] = { node.bounding_box.max.x, node.bounding_box.max.y };

    int child_index = node.index_first_child;
    while (child_index != -1)
    {
        auto& child_node = nodes[child_index];
        SCOPE_EXIT(child_index = child_node.index_next_node);

        // Ignore children with absolute position
        if (child_node.size.is_absolute) {
            gui_layout_layout_children(renderer, child_index);
            continue;
        }

        // Calculate child size and position
        float child_size[2];
        float pos[2];
        for (int dim = 0; dim < 2; dim++) {
            // Calculate size
            child_size[dim] = child_node.min_size_with_children[dim];
            if (child_node.size.fill[dim] && size_for_fill_available) {
                if (dim == layout.stack_dimension) {
                    child_size[dim] = size_for_fill;
                }
                else {
                    child_size[dim] = math_maximum(child_size[dim], my_size[dim] - layout.padding[dim] * 2.0f);
                }
            }

            // Set position
            if (dim == layout.stack_dimension) {
                pos[dim] = stack_cursor;
                if (stack_sign < 0) {
                    pos[dim] -= child_size[dim];
                }
                stack_cursor += child_size[dim] * stack_sign;
            }
            else {
                if (layout.child_alignment == GUI_Align::MIN) {
                    pos[dim] = min[dim] + layout.padding[dim];
                }
                else if (layout.child_alignment == GUI_Align::CENTER) {
                    pos[dim] = (min[dim] + max[dim]) / 2.0f - child_size[dim] / 2.0f;
                }
                else if (layout.child_alignment == GUI_Align::MAX) {
                    pos[dim] = max[dim] - layout.padding[dim] - child_size[dim];
                }
                else {
                    panic("Hey");
                }
            }
        }

        // Position + size to bounding_box
        child_node.bounding_box.min.x = pos[0];
        child_node.bounding_box.min.y = pos[1];
        child_node.bounding_box.max.x = pos[0] + child_size[0];
        child_node.bounding_box.max.y = pos[1] + child_size[1];

        // Recurse to all children
        gui_layout_layout_children(renderer, child_index);
    }
}

bool gui_handle_input(GUI_Renderer* renderer, Input* input, int node_index)
{
    auto& nodes = renderer->nodes;
    auto& node = nodes[node_index];

    // Check if mouse is over this node
    bool mouse_over = false;
    if (node.clipped_box.available) {
        mouse_over = bounding_box_2_is_point_inside(
            node.clipped_box.value, vec2(input->mouse_x, (int)(rendering_core.render_information.backbuffer_height - input->mouse_y)));
    }
    if (!mouse_over) {
        return false;
    }

    // Loop over children
    bool child_took_input = false;
    {
        int child_index = node.index_first_child;
        while (child_index != -1) {
            auto& child_node = nodes[child_index];
            SCOPE_EXIT(child_index = child_node.index_next_node);
            if (gui_handle_input(renderer, input, child_index)) {
                child_took_input = true;
                break;
            }
        }
    }

    if (node.receives_input) {
        node.mouse_hover = true; // I guess we always set hover even if child takes input
    }
    return node.receives_input && mouse_over;
}



template<typename T>
T* gui_store_primitive(GUI_Renderer* renderer, GUI_Handle parent_handle, T default_value) {
    auto node_handle = gui_push_dummy(renderer, parent_handle);
    if (node_handle.userdata == 0) {
        T* new_value = new T;
        *new_value = default_value;
        gui_set_userdata(
            renderer, node_handle, (void*)new_value,
            [](void* data) -> void {
                T* typed_data = (T*)data;
                delete typed_data;
            }
        );
        return new_value;
    }
    return (T*)node_handle.userdata;
}

void gui_push_text(GUI_Renderer* renderer, GUI_Handle parent_handle, String text, float text_height_cm = .5f, vec4 color = vec4(0.0f, 0.0f, 0.0f, 1.0f))
{
    const float char_height = convertHeight(text_height_cm, Unit::CENTIMETER);
    const float char_width = text_renderer_line_width(renderer->text_renderer, char_height, 1) + 0.01f;
    gui_add_node(
        renderer, parent_handle,
        gui_layout_make(),
        gui_size_make_min(vec2(char_width * text.size, char_height)),
        gui_drawable_make_text(text, color),
        false
    );
}

struct GUI_Window_Info
{
    vec2 pos;
    bool drag_started;
    vec2 prev_mouse;
};

GUI_Handle gui_push_window(GUI_Renderer* renderer, GUI_Handle parent_handle, Input* input, vec2 size, Anchor anchor, const char* name, vec2 initial_pos)
{
    // Get window info
    GUI_Window_Info* info = 0;
    {
        GUI_Window_Info initial_info;
        initial_info.drag_started = false;
        initial_info.pos = initial_pos;
        info = gui_store_primitive<GUI_Window_Info>(renderer, parent_handle, initial_info);
    }

    // Create gui nodes
    auto window_handle = gui_add_node(
        renderer, parent_handle, gui_layout_make(), 
        gui_size_make_absolute(bounding_box_2_make_anchor(info->pos, size, anchor)),
        gui_drawable_make_none(), false);
    auto header_handle = gui_add_node(
        renderer, window_handle,
        gui_layout_make(false, GUI_Align::MIN, vec2(3)),
        gui_size_make_fill(true, false),
        gui_drawable_make_rect(vec4(0.3f, 0.3f, 1.0f, 1.0f)),
        true
    );
    gui_push_text(renderer, header_handle, string_create_static(name));
    auto client_area = gui_add_node(renderer, window_handle, gui_layout_make(), gui_size_make_fill(true, true), gui_drawable_make_rect(vec4(1.0f)), false);

    // Handle drag
    bool mouse_down = input->mouse_down[(int)Mouse_Key_Code::LEFT];
    vec2 mouse_pos = vec2(input->mouse_x, input->mouse_y);
    if (info->drag_started && mouse_down) {
        vec2 diff = mouse_pos - info->prev_mouse;
        diff.y *= -1;
        info->prev_mouse = mouse_pos;
        // Update window pos
        info->pos += diff;
        gui_set_size(renderer, window_handle, gui_size_make_absolute(bounding_box_2_make_anchor(info->pos, size, anchor)));
    }
    else {
        if (header_handle.mouse_hover && mouse_down) {
            info->drag_started = true;
            info->prev_mouse = mouse_pos;
        }
        else {
            info->drag_started = false;
        }
    }

    return client_area;
}

bool gui_push_button(GUI_Renderer* renderer, GUI_Handle parent_handle, Input* input, String text)
{
    const vec4 border_color = vec4(vec3(0.2f), 1.0f); // Some shade of gray
    const vec4 normal_color = vec4(vec3(0.8f), 1.0f); // Some shade of gray
    const vec4 hover_color = vec4(vec3(0.5f), 1.0f); // Some shade of gray
    auto border = gui_add_node(
        renderer, parent_handle, gui_layout_make(false, GUI_Align::MIN, vec2(1.2f)),
        gui_size_make_min(vec2(0.0f)), gui_drawable_make_rect(border_color), false
    );
    auto button = gui_add_node(
        renderer, border, gui_layout_make(false, GUI_Align::CENTER, vec2(1.0f)), gui_size_make_min(vec2(convertWidth(1.0f, Unit::CENTIMETER), 0.0f)),
        gui_drawable_make_rect(normal_color), true
    );
    if (button.mouse_hover) {
        gui_set_drawable(renderer, button, gui_drawable_make_rect(hover_color));
    }
    gui_push_text(renderer, button, text);
    return button.mouse_hover && input->mouse_pressed[(int)Mouse_Key_Code::LEFT];
}

GUI_Handle gui_push_dummy(GUI_Renderer* renderer, GUI_Handle parent_handle) {
    return gui_add_node(
        renderer, parent_handle, gui_layout_make(),
        gui_size_make_absolute(bounding_box_2_make_min_max(vec2(-10.0f), vec2(-10.0f))),
        gui_drawable_make_none(), false);
}

// Returns true if the value was toggled
bool gui_push_toggle(GUI_Renderer* renderer, GUI_Handle parent_handle, Input* input, bool* value)
{
    const vec4 border_color = vec4(vec3(0.1f), 1.0f); // Some shade of gray
    const vec4 normal_color = vec4(vec3(0.8f), 1.0f); // Some shade of gray
    const vec4 hover_color = vec4(vec3(0.5f), 1.0f); // Some shade of gray
    float height = convertHeight(.4f, Unit::CENTIMETER);
    auto border = gui_add_node(
        renderer, parent_handle, gui_layout_make(false, GUI_Align::CENTER, vec2(1.5f)),
        gui_size_make_min(vec2(0.0f)), gui_drawable_make_rect(border_color), true
    );
    auto center = gui_add_node(
        renderer, border, gui_layout_make(false, GUI_Align::CENTER, vec2(0.0f)),
        gui_size_make_min(vec2(height)),
        gui_drawable_make_rect(hover_color), false
    );
    bool pressed = false;
    if (border.mouse_hover) {
        gui_set_drawable(renderer, center, gui_drawable_make_rect(hover_color));
        pressed = input->mouse_pressed[(int)Mouse_Key_Code::LEFT];
    }
    if (pressed) {
        *value = !*value;
    }
    if (*value) {
        gui_push_text(renderer, center, string_create_static("x"), .4f, vec4(1.0f, 0.0f, 0.0f, 1.0f));
    }
    return pressed;
}



struct GUI_Dependency
{
    int dependency_count;
    int waiting_for_child_finish_count;
    Dynamic_Array<int> dependents_waiting_on_draw;
    Dynamic_Array<int> dependents_waiting_on_child_finish;
};

void gui_update(GUI_Renderer* renderer, Input* input)
{
    auto& core = rendering_core;
    auto& pre = core.predefined;

    if (false)
    {
        static bool toggle = false;
        if (input->key_pressed[(int)Key_Code::T]) {
            toggle = !toggle;
            logg("Toggle switched to: %s\n", toggle ? "true" : "false");
        }
        // auto window = gui_add_node(renderer, renderer->root_handle, gui_layout_make(),
        //     gui_size_make_absolute(bounding_box_2_make_anchor(convertPoint(vec2(0.0f), Unit::NORMALIZED_SCREEN), vec2(400), Anchor::CENTER_CENTER)),
        //     gui_drawable_make_rect(vec4(1.0f, 0.0f, 1.0f, 1.0f)),
        //     false
        // );
        auto window = gui_push_window(
            renderer, renderer->root_handle, input, vec2(400), Anchor::CENTER_CENTER,
            "Window", convertPoint(vec2(0.0f), Unit::NORMALIZED_SCREEN)
        );
        if (toggle) {
            gui_push_text(renderer, window, string_create_static("Hello"));
            gui_add_node(renderer, window, gui_layout_make(),
                gui_size_make_absolute(bounding_box_2_make_anchor(convertPoint(vec2(0.0f), Unit::NORMALIZED_SCREEN), vec2(200), Anchor::CENTER_CENTER)),
                gui_drawable_make_rect(vec4(0.0f, 1.0f, 1.0f, 1.0f)),
                false
            );
        }
    }

    // Generating UI (User code mockup, this will be somewhere else later)
    if (true)
    {
        // Make the rectangle a constant pixel size:
        int pixel_width = 100;
        int pixel_height = 100;

        const vec4 white = vec4(1.0f);
        const vec4 black = vec4(vec3(0.0f), 1.0f);
        const vec4 red = vec4(vec3(1, 0, 0), 1.0f);
        const vec4 green = vec4(vec3(0, 1, 0), 1.0f);
        const vec4 blue = vec4(vec3(0, 0, 1), 1.0f);
        const vec4 cyan = vec4(vec3(0, 1, 1), 1.0f);
        const vec4 yellow = vec4(vec3(1, 1, 0), 1.0f);
        const vec4 magenta = vec4(vec3(1, 0, 1), 1.0f);
        const vec4 gray = vec4(vec3(0.3f), 1.0f);

        auto window = gui_push_window(
            renderer, renderer->root_handle, input,
            vec2(400, 600), Anchor::CENTER_CENTER, "Test window", convertPoint(vec2(0.0f), Unit::NORMALIZED_SCREEN)
        );
        auto space = gui_add_node(renderer, window, gui_layout_make(false, GUI_Align::CENTER), gui_size_make_fill(true, true), gui_drawable_make_rect(cyan), false);
        bool* value = gui_store_primitive<bool>(renderer, space, false);
        gui_push_toggle(renderer, space, input, value);
        if (*value) {
            bool pressed = gui_push_button(renderer, space, input, string_create_static("Press me!"));
            int* value = gui_store_primitive<int>(renderer, space, 0);
            if (pressed) {
                *value += 1;
            }
            String tmp = string_create_formated("%d", *value);
            SCOPE_EXIT(string_destroy(&tmp));
            gui_push_text(renderer, space, tmp);
        }

        auto right_align = gui_add_node(renderer, window, gui_layout_make(false, GUI_Align::MAX), gui_size_make_fill(true, false), gui_drawable_make_none(), false);
        gui_push_text(renderer, right_align, string_create_static("Dis is da dext"));
        auto vertical = gui_add_node(renderer, window, gui_layout_make(true), gui_size_make_fill(true, true), gui_drawable_make_none(), false);
        gui_add_node(renderer, vertical, gui_layout_make(), gui_size_make_fill(true, true), gui_drawable_make_rect(gray), false);
        gui_add_node(renderer, vertical, gui_layout_make(), gui_size_make_fill(true, true), gui_drawable_make_rect(yellow), false);
        auto center = gui_add_node(renderer, window, gui_layout_make(false, GUI_Align::CENTER), gui_size_make_fill(true, false), gui_drawable_make_none(), false);
        gui_push_text(renderer, center, string_create_static("Da degst 2"));
        gui_add_node(renderer, window, gui_layout_make(), gui_size_make_fill(true, true), gui_drawable_make_rect(magenta), false);
        gui_push_text(renderer, window, string_create_static("Da degst 3"));


        // Add centered text
        // gui_push_text(renderer, window, string_create_static("Hello there"));
        // auto tmp = string_create_static("Long text that, lorem ipsum");
        // auto center = gui_add_node(renderer, window, gui_layout_make(false, GUI_Align::CENTER), gui_size_make_fill(true, false), gui_drawable_make_none());
        // gui_push_text(renderer, center, tmp, 1.3f);

        // gui_push_text(renderer, window, string_create_static("Hello there"), 0.4f, gray);
        // gui_push_text(renderer, window, string_create_static("This is a new item"), 0.4f, gray);
        // auto container = gui_push_container(renderer, window, true);
        // gui_push_text(renderer, container, string_create_static("Where"), 0.4f, gray);
        // gui_push_text(renderer, container, string_create_static("Am I"), 0.4f, green);

        // {
        //     float t = rendering_core.render_information.current_time_in_seconds;
        //     t += 20.0f;
        //     if (t < 1.0f) {
        //         t = 0;
        //     }
        //     else {
        //         t = t - 1;
        //     }

        //     float remaining = 10 - t;
        //     gui_push_text(renderer, window, string_create_static(""), 0.4f, gray); // Placeholder
        //     String tmp = string_create_formated("%3.1f", remaining);
        //     SCOPE_EXIT(string_destroy(&tmp));

        //     vec4 color = red;
        //     if (remaining < 0.0f) {
        //         string_set_characters(&tmp, "Get Pranked, lol");
        //         color = magenta;
        //     }

        //     auto layout = gui_layout_default(false, Anchor::CENTER_CENTER);
        //     auto center = gui_add_node(renderer, window, layout, white, GUI_Draw_Type::NONE, string_create_static(""));
        //     gui_push_text(renderer, center, tmp, 1.3f, color);
        // }

        // gui_new_window(renderer, "Longer Test1");
        // gui_push_label(renderer, string_create_static("Hello there"));
        // gui_push_label(renderer, string_create_static("General Kenobi!"));
        // gui_new_window(renderer, "Faster Test2");

        // vec2 pos = convertPoint(vec2(0.0f), Unit::NORMALIZED_SCREEN);
        // vec2 size = convertSize(vec2(1.0f), Unit::CENTIMETER);

        // primitive_renderer_add_rectangle(
        //   , renderer->primitive_renderer, bounding_box_2_make_anchor(pos, size, Anchor::BOTTOM_LEFT), red
        // );
        // gui_draw_text_cutoff(renderer, string_create_static("Hellow orld"), pos, Anchor::BOTTOM_LEFT, size, vec3(1.0f));
        // primitive_renderer_add_text(renderer->primitive_renderer, string_create_static("Asdfasdf"), pos, Anchor::CENTER_CENTER, size.y * 0.8f, magenta);
        // primitive_renderer_add_rectangle(
        //     renderer->primitive_renderer, bounding_box_2_make_anchor(pos, size, Anchor::CENTER_CENTER), blue
        // );


        // pos.x += size.x * 2.0f;
        // primitive_renderer_add_rectangle(
        //     renderer->primitive_renderer, bounding_box_2_make_anchor(pos, size, Anchor::BOTTOM_LEFT), green
        // );

    }

    // Remove nodes from last frame
    {
        auto& nodes = renderer->nodes;
        // Generate new node positions
        Array<int> new_node_indices = array_create_empty<int>(nodes.size);
        SCOPE_EXIT(array_destroy(&new_node_indices));
        int next_free_index = 0;
        gui_update_nodes_recursive(renderer, new_node_indices, 0, next_free_index);

        // Do compaction
        Dynamic_Array<GUI_Node> new_nodes = dynamic_array_create_empty<GUI_Node>(next_free_index + 1);
        new_nodes.size = next_free_index;
        for (int i = 0; i < nodes.size; i++) {
            int new_index = new_node_indices[i];
            if (new_index != -1) {
                new_nodes[new_index] = nodes[i];
            }
        }
        auto old = renderer->nodes;
        renderer->nodes = new_nodes;
        dynamic_array_destroy(&old);

        // Update root node
        nodes[0].referenced_this_frame = true;
    }

    // Layout UI
    {
        auto& nodes = renderer->nodes;
        assert(nodes[0].size.is_absolute, "Root must be absolute!");
        nodes[0].size.absolute_box = bounding_box_2_make_anchor(vec2(0.0f), convertSize(vec2(2.0f), Unit::NORMALIZED_SCREEN), Anchor::BOTTOM_LEFT);

        gui_layout_calculate_min_size(renderer, 0);
        gui_layout_layout_children(renderer, 0);
    }

    // Handle input
    gui_handle_input(renderer, input, 0);

    // Render UI
    {
        auto& nodes = renderer->nodes;

        // Generate draw batches
        Array<int> execution_order = array_create_empty<int>(nodes.size);
        Dynamic_Array<int> batch_start_indices = dynamic_array_create_empty<int>(nodes.size);
        SCOPE_EXIT(array_destroy(&execution_order));
        SCOPE_EXIT(dynamic_array_destroy(&batch_start_indices));
        {
            int next_free_in_order = 0;

            // Initialize dependency graph structure
            Array<GUI_Dependency> dependencies = array_create_empty<GUI_Dependency>(nodes.size);
            for (int i = 0; i < dependencies.size; i++) {
                auto& dependency = dependencies[i];
                dependency.dependency_count = 0;
                dependency.waiting_for_child_finish_count = 0;
                dependency.dependents_waiting_on_child_finish = dynamic_array_create_empty<int>(1);
                dependency.dependents_waiting_on_draw = dynamic_array_create_empty<int>(1);
            }
            SCOPE_EXIT(
                for (int i = 0; i < dependencies.size; i++) {
                    auto& dependency = dependencies[i];
                    dynamic_array_destroy(&dependency.dependents_waiting_on_child_finish);
                    dynamic_array_destroy(&dependency.dependents_waiting_on_draw);
                }
            array_destroy(&dependencies);
            );

            // Generate dependencies
            for (int i = 0; i < nodes.size; i++)
            {
                auto& node = nodes[i];
                auto& dependency = dependencies[i];

                // Generate child finish dependencies
                auto child_index = node.index_first_child;
                while (child_index != -1) {
                    auto& child_dependency = dependencies[child_index];
                    auto& child_node = nodes[child_index];
                    SCOPE_EXIT(child_index = child_node.index_next_node);
                    // Add child finish dependency for each child
                    dependency.waiting_for_child_finish_count += 1;
                    child_dependency.dependency_count += 1;
                    dynamic_array_push_back(&dependency.dependents_waiting_on_draw, child_index);
                }

                // Generate overlap/neighbor dependencies
                auto next_index = node.index_next_node;
                while (next_index != -1) {
                    auto& next_dependency = dependencies[next_index];
                    auto& next_node = nodes[next_index];
                    SCOPE_EXIT(next_index = next_node.index_next_node);
                    // Add overlap dependency
                    if (bounding_box_2_overlap(next_node.bounding_box, node.bounding_box)) {
                        next_dependency.dependency_count += 1;
                        if (node.index_first_child == -1) { // Normal dependency if this node doesn't have any children
                            dynamic_array_push_back(&dependency.dependents_waiting_on_draw, next_index);
                        }
                        else {
                            dynamic_array_push_back(&dependency.dependents_waiting_on_child_finish, next_index);
                        }
                    }
                }
            }

            // Generate first batch by looking for all things that are runnable
            dynamic_array_push_back(&batch_start_indices, 0);
            for (int i = 0; i < dependencies.size; i++) {
                auto& dependency = dependencies[i];
                if (dependency.dependency_count == 0) {
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
                    auto& dependency = dependencies[i];
                    for (int j = 0; j < dependency.dependents_waiting_on_draw.size; j++)
                    {
                        int waiting_index = dependency.dependents_waiting_on_draw[j];
                        auto& waiting_dependency = dependencies[waiting_index];
                        assert(waiting_dependency.dependency_count > 0, "Must not happen!");
                        waiting_dependency.dependency_count -= 1;
                        // Add to next batch if the workload can be drawn
                        if (waiting_dependency.dependency_count == 0) {
                            execution_order[next_free_in_order] = waiting_index;
                            next_free_in_order += 1;
                        }
                    }

                    // Check parent dependencies
                    auto& node = nodes[i];
                    if (node.index_parent != -1)
                    {
                        auto& parent_dependency = dependencies[node.index_parent];
                        assert(parent_dependency.waiting_for_child_finish_count > 0, "Must not happen!");
                        parent_dependency.waiting_for_child_finish_count -= 1;
                        if (parent_dependency.waiting_for_child_finish_count == 0)
                        {
                            for (int j = 0; j < parent_dependency.dependents_waiting_on_child_finish.size; j++)
                            {
                                int waiting_index = parent_dependency.dependents_waiting_on_child_finish[j];
                                auto& waiting_dependency = dependencies[waiting_index];
                                assert(waiting_dependency.dependency_count > 0, "Must not happen!");
                                waiting_dependency.dependency_count -= 1;
                                // Add to next batch if the workload can be drawn
                                if (waiting_dependency.dependency_count == 0) {
                                    execution_order[next_free_in_order] = waiting_index;
                                    next_free_in_order += 1;
                                }
                            }
                        }
                    }
                }

                if (next_free_in_order == batch_end) {
                    assert(next_free_in_order == nodes.size, "Deadlock must not happen!");
                    break;
                }
                dynamic_array_push_back(&batch_start_indices, next_free_in_order); // Push last index
            }
        }

        // Query render primitives
        auto& pre = rendering_core.predefined;
        Mesh* rect_mesh = rendering_core_query_mesh("gui_rect", vertex_description_create({ pre.position2D, pre.color4 }), true);
        Shader* rect_shader = rendering_core_query_shader("gui_rect.glsl");

        auto render_state_2D = pipeline_state_make_default();
        render_state_2D.blending_state.blending_enabled = true;
        render_state_2D.blending_state.source = Blend_Operand::SOURCE_ALPHA;
        render_state_2D.blending_state.destination = Blend_Operand::ONE_MINUS_SOURCE_ALPHA;
        render_state_2D.blending_state.equation = Blend_Equation::ADDITION;
        render_state_2D.depth_state.test_type = Depth_Test_Type::IGNORE_DEPTH;
        Render_Pass* pass_2D = rendering_core_query_renderpass("2D pass", render_state_2D, 0);
        render_pass_add_dependency(pass_2D, rendering_core.predefined.main_pass);

        // Draw batches in order
        for (int batch = 0; batch < batch_start_indices.size - 1; batch++)
        {
            int batch_start = batch_start_indices[batch];
            int batch_end = batch_start_indices[batch + 1];
            int quad_vertex_count = rect_mesh->vertex_count;
            for (int node_indirect_index = batch_start; node_indirect_index < batch_end; node_indirect_index++)
            {
                auto& node = nodes[execution_order[node_indirect_index]];
                if (!node.clipped_box.available) {
                    continue;
                }
                switch (node.drawable.type)
                {
                case GUI_Drawable_Type::RECTANGLE: {
                    auto bb = node.clipped_box.value;
                    bb.min = convertPointFromTo(bb.min, Unit::PIXELS, Unit::NORMALIZED_SCREEN);
                    bb.max = convertPointFromTo(bb.max, Unit::PIXELS, Unit::NORMALIZED_SCREEN);
                    auto& pre = rendering_core.predefined;
                    mesh_push_attribute(
                        rect_mesh, pre.position2D,
                        {
                            vec2(bb.min.x, bb.min.y),
                            vec2(bb.max.x, bb.min.y),
                            vec2(bb.max.x, bb.max.y),

                            vec2(bb.min.x, bb.min.y),
                            vec2(bb.max.x, bb.max.y),
                            vec2(bb.min.x, bb.max.y),
                        }
                    );
                    vec4 c = node.drawable.color;
                    mesh_push_attribute(rect_mesh, pre.color4, { c, c, c, c, c, c });
                    break;
                }
                case GUI_Drawable_Type::TEXT: {
                    float height = node.bounding_box.max.y - node.bounding_box.min.y;
                    float char_width = text_renderer_line_width(renderer->text_renderer, height, 1);
                    String text = node.drawable.text; // Local copy so size can be changed without affecting original code
                    vec4 c = node.drawable.color;
                    text_renderer_add_text(
                        renderer->text_renderer, text, node.bounding_box.min, Anchor::BOTTOM_LEFT, height, vec3(c.x, c.y, c.z), node.clipped_box
                    );
                    break;
                }
                case GUI_Drawable_Type::NONE: break;
                default: panic("");
                }

                // logg("    Text \"%s\"\n", primitive.data.text.characters);
                // text_renderer_add_text(
                //     renderer->text_renderer, primitive.data.text, primitive.bounding_box.min, Anchor::BOTTOM_LEFT,
                //     primitive.bounding_box.max.y - primitive.bounding_box.min.y, vec3(primitive.color.x, primitive.color.y, primitive.color.z)
                // );
                // break;
            }

            // Add draw command for batch
            int new_quad_vertex_count = rect_mesh->vertex_count;
            if (new_quad_vertex_count > quad_vertex_count) {
                render_pass_draw_count(pass_2D, rect_shader, rect_mesh, Mesh_Topology::TRIANGLES, {}, quad_vertex_count, new_quad_vertex_count - quad_vertex_count);
            }
            text_renderer_draw(renderer->text_renderer, pass_2D);
        }
    }
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
        window_set_vsync(window, true);
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

    GUI_Renderer gui_renderer = gui_renderer_initialize(text_renderer);

    // Window Loop
    double time_last_update_start = timer_current_time_in_seconds(&timer);
    while (true)
    {
        double time_frame_start = timer_current_time_in_seconds(&timer);
        float time_since_last_update = (float)(time_frame_start - time_last_update_start);
        time_last_update_start = time_frame_start;

        Input* input = window_get_input(window);
        SCOPE_EXIT(input_reset(input));
        {
            // Input and Logic
            if (!window_handle_messages(window, false)) {
                break;
            }
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

        double time_input_end = timer_current_time_in_seconds(&timer);

        // Rendering
        {
            rendering_core_prepare_frame(timer_current_time_in_seconds(&timer), window_state->width, window_state->height);
            SCOPE_EXIT(
                renderer_2D_reset(renderer_2D);
            text_renderer_reset(text_renderer);
            rendering_core_render(camera, Framebuffer_Clear_Type::COLOR_AND_DEPTH);
            window_swap_buffers(window);
            );

            gui_update(&gui_renderer, input);

            // text_renderer_add_text(text_renderer, &string_create_static("H"), vec2(0.0f), 0.1f, 0.0f);
            // text_renderer_draw(text_renderer, text_pass);
            // text_renderer_add_text(text_renderer, &string_create_static("e"), vec2(0.0f, -0.3f), 0.03f, 0.0f);
            // text_renderer_draw(text_renderer, text_pass);


            // auto main_pass = core.predefined.main_pass;
            // auto circle1 = rendering_core_query_mesh(
            //     "circleMesh1", vertex_description_create({ core.predefined.position2D }), true
            // );
            // auto circle2 = rendering_core_query_mesh(
            //     "circleMesh2", vertex_description_create({ core.predefined.position2D, core.predefined.index, core.predefined.texture_coordinates }), true
            // );

            // // Generate circle data
            // {
            //     float radius = 0.3f;
            //     int division = 16;
            //     auto time = rendering_core.render_information.current_time_in_seconds;
            //     vec2 offset = vec2(math_cosine(time), math_sine(time));
            //     for (int i = 0; i < division; i++) {
            //         mesh_push_attribute(
            //             circle1, core.predefined.position2D, {
            //                 offset + vec2(0),
            //                 offset + vec2(math_cosine(2 * PI / division * i), math_sine(2 * PI / division * i)) * radius,
            //                 offset + vec2(math_cosine(2 * PI / division * (i + 1)), math_sine(2 * PI / division * (i + 1))) * radius,
            //             }
            //         );
            //     }
            //     
            //     time += PI;
            //     offset = vec2(math_cosine(time), math_sine(time));

            //     auto indices = mesh_push_attribute_slice(circle2, core.predefined.index, division * 3);
            //     auto positions = mesh_push_attribute_slice(circle2, core.predefined.position2D, division * 3);
            //     auto uvs = mesh_push_attribute_slice(circle2, core.predefined.texture_coordinates, division * 3);
            //     for (int i = 0; i < division; i++) {
            //         int k = i * 3;
            //         indices[k + 0] = k + 0;
            //         indices[k + 1] = k + 1;
            //         indices[k + 2] = k + 2;
            //         positions[k + 0] = offset + vec2(0);
            //         positions[k + 1] = offset + vec2(math_cosine(2 * PI / division * k), math_sine(2 * PI / division * k)) * radius;
            //         positions[k + 2] = offset + vec2(math_cosine(2 * PI / division * (k + 1)), math_sine(2 * PI / division * (k + 1))) * radius;
            //         uvs[k + 0] = vec2(0);
            //         uvs[k + 1] = vec2(math_cosine(2 * PI / division * k), math_sine(2 * PI / division * k)) * radius;
            //         uvs[k + 2] = vec2(math_cosine(2 * PI / division * (k + 1)), math_sine(2 * PI / division * (k + 1))) * radius;
            //     }

            //     logg("What\n");
            // }

            // auto const_color_2d = rendering_core_query_shader("const_color_2D.glsl");
            // //render_pass_draw(main_pass, const_color_2d, circle1, Mesh_Topology::TRIANGLES, { uniform_make("u_color", vec4(1.0f)) });
            // render_pass_draw(main_pass, const_color_2d, circle2, Mesh_Topology::TRIANGLES, { uniform_make("u_color", vec4(1.0f, 0.0f, 0.0f, 1.0f)) });
            //auto color_shader_2d = shader_generator_make({ predefined.position2D, predefined.uniform_color }, );

            //auto bg_buffer = rendering_core_query_framebuffer("bg_buffer", Texture_Type::RED_GREEN_BLUE_U8, Depth_Type::NO_DEPTH, 512, 512);
            //{
            //    auto bg_pass = rendering_core_query_renderpass("bg_pass", pipeline_state_make_default(), bg_buffer);
            //    render_pass_add_dependency(main_pass, bg_pass);
            //    auto bg_shader = rendering_core_query_shader("upp_lang/background.glsl");
            //    render_pass_draw(bg_pass, bg_shader, quad_mesh, Mesh_Topology::TRIANGLES, {});
            //}

            //{
            //    auto shader = rendering_core_query_shader("test.glsl");
            //    render_pass_set_uniforms(main_pass, shader, { uniform_make("image", bg_buffer->color_texture, sampling_mode_bilinear()) });
            //    render_pass_draw(main_pass, shader, mesh, Mesh_Topology::TRIANGLES, { uniform_make("offset", vec2(0)), uniform_make("scale", 1.0f) });
            //}
            // renderer_2D_add_rectangle(renderer_2D, vec2(0.0f), vec2(0.2, 0.3f), vec3(1.0f), 0.0f);
            // renderer_2D_draw(renderer_2D, text_pass);
        }

        double time_render_end = timer_current_time_in_seconds(&timer);

        // Sleep
        {
            double time_calculations = timer_current_time_in_seconds(&timer) - time_frame_start;
            /*
            logg("FRAME_TIMING:\n---------------\n");
            logg("input        ... %3.2fms\n", 1000.0f * (float)(time_input_end - time_frame_start));
            logg("render       ... %3.2fms\n", 1000.0f * (float)(time_render_end - time_input_end));
            logg("TSLF: %3.2fms, calculation time: %3.2fms\n", time_since_last_update*1000, time_calculations*1000);
            */

            // Sleep
            const int TARGET_FPS = 60;
            const double SECONDS_PER_FRAME = 1.0 / TARGET_FPS;
            timer_sleep_until(&timer, time_frame_start + SECONDS_PER_FRAME);
        }
    }
}