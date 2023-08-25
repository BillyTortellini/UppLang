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


enum class GUI_Size_Type
{
    FIXED,
    FILL_WITH_MIN, 
    MIN
};

struct GUI_Size
{
    float min_size;
    bool fit_at_least_children;
    bool fill;
};

GUI_Size gui_size_make(float min_size, bool fit_at_least_children, bool fill) {
    GUI_Size size;
    size.min_size = min_size;
    size.fill = fill;
    size.fit_at_least_children = fit_at_least_children;
    return size;
}

GUI_Size gui_size_make_fit() {
    return gui_size_make(0.0f, true, false);
}

GUI_Size gui_size_make_fixed(float value) {
    return gui_size_make(value, false, false);
}

GUI_Size gui_size_make_fill(bool fit_children = false, float min_size = 0.0f) {
    return gui_size_make(min_size, fit_children, true);
}



enum class GUI_Position_Type
{
    RELATIVE_TO_WINDOW,
    RELATIVE_TO_PARENT,
    USE_PARENT_LAYOUT
};

struct GUI_Position
{
    GUI_Position_Type type;
    vec2 offset;
    Anchor anchor;
    int z_index;
};

GUI_Position gui_position_make_parent_layout(int z_index = 0) {
    GUI_Position result;
    memory_zero(&result);
    result.type = GUI_Position_Type::USE_PARENT_LAYOUT;
    result.z_index = z_index;
    return result;
}

GUI_Position gui_position_make_relative(vec2 offset, Anchor anchor, int z_index = 0, bool relative_to_parent = true) {
    GUI_Position result;
    result.type = relative_to_parent ? GUI_Position_Type::RELATIVE_TO_PARENT : GUI_Position_Type::RELATIVE_TO_WINDOW;
    result.offset = offset;
    result.anchor = anchor;
    result.z_index = z_index;
    return result;
}



enum class GUI_Align
{
    MIN, // in x this is left_aligned, in y bottom aligned
    MAX,
    CENTER,
};

enum class GUI_Layout_Type {
    STACK_HORIZONTAL,
    STACK_VERTICAL,
    LAYERED
};

struct GUI_Layout
{
    GUI_Layout_Type layout_type;
    GUI_Align child_alignment; 
    float padding[2];
};

GUI_Layout gui_layout_make_stacked(bool stack_vertical = true, GUI_Align align = GUI_Align::MIN, vec2 padding = vec2(0.0f))
{
    GUI_Layout layout;
    layout.child_alignment = align;
    layout.padding[0] = padding.x;
    layout.padding[1] = padding.y;
    layout.layout_type = stack_vertical ? GUI_Layout_Type::STACK_VERTICAL : GUI_Layout_Type::STACK_HORIZONTAL;
    return layout;
}

GUI_Layout gui_layout_make_layered(vec2 padding = vec2(0.0f))
{
    GUI_Layout layout;
    layout.layout_type = GUI_Layout_Type::LAYERED;
    layout.child_alignment = GUI_Align::MIN; // Doesnt matter
    layout.padding[0] = padding.x;
    layout.padding[1] = padding.y;
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



// GUI Hierarchy
typedef void (*gui_userdata_destroy_fn)(void* userdata);

struct GUI_Node
{
    GUI_Size size[2];
    GUI_Position position;
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
    float min_size[2];
    float min_child_size[2];

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

struct IMGUI
{
    Text_Renderer* text_renderer;
    Window* window;
    Dynamic_Array<GUI_Node> nodes;
    GUI_Handle root_handle;
    Cursor_Icon_Type cursor_type;
};

IMGUI imgui;

void imgui_initialize(Text_Renderer* text_renderer, Window* window)
{
    auto& pre = rendering_core.predefined;

    imgui.text_renderer = text_renderer;
    imgui.nodes = dynamic_array_create_empty<GUI_Node>(1);
    imgui.root_handle.index = 0;
    imgui.root_handle.mouse_hover = false;
    imgui.window = window;
    imgui.cursor_type = Cursor_Icon_Type::ARROW;
    
    // Push root node
    GUI_Node root;
    root.bounding_box = bounding_box_2_convert(bounding_box_2_make_anchor(vec2(0.0f), vec2(2.0f), Anchor::CENTER_CENTER), Unit::NORMALIZED_SCREEN);
    root.referenced_this_frame = true;
    root.index_first_child = -1;
    root.index_last_child = -1;
    root.index_parent = -1;
    root.index_next_node = -1;
    root.traversal_next_child = -1;
    root.userdata = nullptr;
    root.userdata_destroy_fn = nullptr;
    root.drawable = gui_drawable_make_none();
    auto& info = rendering_core.render_information;
    root.size[0] = gui_size_make(info.backbuffer_width, false, false);
    root.size[1] = gui_size_make(info.backbuffer_height, false, false);
    root.position = gui_position_make_relative(vec2(0.0f), Anchor::BOTTOM_LEFT, 0, false);
    root.layout = gui_layout_make_layered();
    root.receives_input = false;
    dynamic_array_push_back(&imgui.nodes, root);
}

void imgui_destroy() {
    for (int i = 0; i < imgui.nodes.size; i++) {
        gui_node_destroy(imgui.nodes[i]);
    }
    dynamic_array_destroy(&imgui.nodes);
}

GUI_Handle gui_add_node(
    GUI_Handle parent_handle, GUI_Size size_x, GUI_Size size_y, 
    GUI_Position position, GUI_Layout layout, GUI_Drawable drawable, bool receives_input
) 
{
    auto& nodes = imgui.nodes;

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

    // Update node data
    {
        auto& node = imgui.nodes[node_index];
        node.referenced_this_frame = true;
        node.layout = layout;
        node.size[0] = size_x;
        node.size[1] = size_y;
        node.position = position;
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
    nodes[parent_handle.index].traversal_next_child = imgui.nodes[node_index].index_next_node;

    GUI_Handle handle;
    handle.index = node_index;
    handle.mouse_hover = nodes[node_index].mouse_hover;
    handle.userdata = nodes[node_index].userdata;
    return handle;
}



// GUI UPDATE
void gui_update_nodes_recursive(Array<int> new_node_indices, int node_index, int& next_free_node_index)
{
    auto& nodes = imgui.nodes;
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
            gui_update_nodes_recursive(new_node_indices, child_index, next_free_node_index);
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

void gui_layout_calculate_min_size(int node_index, int dim)
{
    auto& nodes = imgui.nodes;
    auto& node = nodes[node_index];

    // Calculate all child min sizes
    node.min_child_size[dim] = 0.0f;
    bool in_stacking_dimension =
        (node.layout.layout_type != GUI_Layout_Type::LAYERED) &&
        ((node.layout.layout_type == GUI_Layout_Type::STACK_HORIZONTAL && dim == 0) ||
            (node.layout.layout_type == GUI_Layout_Type::STACK_VERTICAL && dim == 1));

    // Calculate min child size
    int child_index = node.index_first_child;
    while (child_index != -1) {
        auto& child_node = nodes[child_index];
        SCOPE_EXIT(child_index = child_node.index_next_node);
        gui_layout_calculate_min_size(child_index, dim);

        if (child_node.position.type != GUI_Position_Type::USE_PARENT_LAYOUT) {
            continue;
        }
        if (in_stacking_dimension) {
            node.min_child_size[dim] += child_node.min_size[dim];
        }
        else {
            node.min_child_size[dim] = math_maximum(node.min_child_size[dim], child_node.min_size[dim]);
        }
    }

    // Calculate min size
    if (node.size[dim].fit_at_least_children) {
        node.min_size[dim] = math_maximum(node.size[dim].min_size, node.min_child_size[dim] + node.layout.padding[dim] * 2.0f);
    }
    else {
        node.min_size[dim] = node.size[dim].min_size;
    }
}

void gui_layout_layout_children(int node_index, int dim)
{
    auto& nodes = imgui.nodes;
    auto& node = nodes[node_index];
    auto& layout = node.layout;

    // Size is set by parent at this point
    float node_size;
    float node_pos;
    if (dim == 0) {
        node_size = node.bounding_box.max.x - node.bounding_box.min.x;
        node_pos = node.bounding_box.min.x;
    }
    else {
        node_size = node.bounding_box.max.y - node.bounding_box.min.y;
        node_pos = node.bounding_box.min.y;
    }

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

    // Check if we need to calculate fill
    bool in_stack_dimension = false;
    if (node.layout.layout_type != GUI_Layout_Type::LAYERED) {
        int stack_dimension = node.layout.layout_type == GUI_Layout_Type::STACK_HORIZONTAL ? 0 : 1;
        in_stack_dimension = dim == stack_dimension;
    }

    // Calculate additional size for all fill children
    bool size_for_fill_available = false;
    float size_for_fill = 0.0f;
    const float available_size = node_size - 2.0f * node.layout.padding[dim];
    if (in_stack_dimension && available_size - node.min_child_size[dim] > 0) // Only calculate if there is actually space for filling
    {
        float non_fill_size = 0.0f;
        Dynamic_Array<int> fill_childs = dynamic_array_create_empty<int>(4);
        SCOPE_EXIT(dynamic_array_destroy(&fill_childs));

        // Get number of children who want to fill in stacking direction
        int child_index = node.index_first_child;
        while (child_index != -1) {
            auto& child_node = nodes[child_index];
            SCOPE_EXIT(child_index = child_node.index_next_node);
            if (child_node.position.type != GUI_Position_Type::USE_PARENT_LAYOUT) {
                continue;
            }

            if (child_node.size[dim].fill) {
                dynamic_array_push_back(&fill_childs, child_index);
            }
            else {
                non_fill_size += child_node.min_size[dim];
            }
        }

        // Calculate values for fill-children
        if (fill_childs.size > 0)
        {
            size_for_fill_available = true;
            size_for_fill = (available_size - non_fill_size) / fill_childs.size;

            // Loop over fill children until we have enough space for all
            float full_combined_size = 0.0f;
            float min_size_for_fill = 0.0f;
            int max_full_count = 0;
            int full_count = 0;
            for (int i = 0; i < fill_childs.size; i++)
            {
                auto& child_node = nodes[fill_childs[i]];
                float min_size = child_node.min_size[dim];
                if (min_size > size_for_fill) {
                    full_count += 1;
                    full_combined_size += min_size;
                }
                else {
                    min_size_for_fill = math_maximum(min_size_for_fill, min_size);
                }

                if (full_count > max_full_count) {
                    max_full_count = full_count;
                    size_for_fill = (available_size - non_fill_size - full_combined_size) / (fill_childs.size - full_count);

                    // Restart with new size_for_fill
                    if (size_for_fill < min_size_for_fill) {
                        i = -1; // Restart loop at 0;
                        full_count = 0;
                        full_combined_size = 0.0f;
                        min_size_for_fill = 0.0f;
                    }
                }
            }
        }
    }

    // Setup stack cursor 
    const float stack_sign = dim == 0 ? +1.0f : -1.0f; // Stack downward if we stack in y
    float stack_cursor = dim == 0 ? node.bounding_box.min.x : node.bounding_box.max.y;
    stack_cursor += layout.padding[dim] * stack_sign;

    // Loop over all children and set their position and size
    int child_index = node.index_first_child;
    while (child_index != -1)
    {
        auto& child_node = nodes[child_index];
        SCOPE_EXIT(child_index = child_node.index_next_node);

        float child_size = child_node.min_size[dim];
        if (child_node.size[dim].fill) {
            if (in_stack_dimension && child_node.position.type == GUI_Position_Type::USE_PARENT_LAYOUT) {
                if (size_for_fill_available && size_for_fill > child_size) {
                    child_size = size_for_fill;
                }
                else {
                    // In this case there is no more space to fill, so just take the minimum value
                }
            }
            else {
                child_size = math_maximum(child_size, node_size - node.layout.padding[dim] * 2.0f);
            }
        }

        // Alignment info if child should be aligned
        bool align_child = false;
        GUI_Align final_align = GUI_Align::MIN;
        float rel_pos = node_pos;
        float padding = node.layout.padding[dim];
        float rel_size = node_size;
        float offset = 0;

        // Check how position should be calculated
        float child_pos = 0.0f;
        switch (child_node.position.type)
        {
        case GUI_Position_Type::RELATIVE_TO_PARENT:
        case GUI_Position_Type::RELATIVE_TO_WINDOW: {
            align_child = true;
            if (child_node.position.type == GUI_Position_Type::RELATIVE_TO_WINDOW) {
                rel_pos = 0;
                auto& info = rendering_core.render_information;
                rel_size = dim == 0 ? info.backbuffer_width : info.backbuffer_height;
                padding = 0;
            }

            offset = (dim == 0 ? child_node.position.offset.x : child_node.position.offset.y);
            vec2 anchor_dir = anchor_to_direction(child_node.position.anchor);
            float offset_dir = dim == 0 ? anchor_dir.x : anchor_dir.y;
            if (offset_dir < -0.1f) {
                final_align = GUI_Align::MIN;
            }
            else if (offset_dir > 0.1f) {
                final_align = GUI_Align::MAX;
            }
            else {
                final_align = GUI_Align::CENTER;
            }
            break;
        }
        case GUI_Position_Type::USE_PARENT_LAYOUT: {
            if (in_stack_dimension) {
                child_pos = stack_cursor;
                if (stack_sign < 0.0f) {
                    child_pos -= child_size;
                }
                stack_cursor += child_size * stack_sign;
            }
            else {
                align_child = true;
                final_align = node.layout.child_alignment;
            }
            break;
        }
        default: panic("");
        }

        // Do alignment if requested
        if (align_child)
        {
            switch (final_align)
            {
            case GUI_Align::MIN: child_pos = rel_pos + padding + offset; break;
            case GUI_Align::MAX: child_pos = rel_pos + rel_size - child_size - padding + offset; break;
            case GUI_Align::CENTER: child_pos = (rel_pos + rel_size / 2.0f) - child_size / 2.0f + offset;
            }
        }

        // Set child pos
        if (dim == 0) {
            child_node.bounding_box.min.x = child_pos;
            child_node.bounding_box.max.x = child_pos + child_size;
        }
        else {
            child_node.bounding_box.min.y = child_pos;
            child_node.bounding_box.max.y = child_pos + child_size;
        }

        // Recurse to all children
        gui_layout_layout_children(child_index, dim);
    }
}

bool gui_handle_input(Input* input, int node_index)
{
    auto& nodes = imgui.nodes;
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
            if (gui_handle_input(input, child_index)) {
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
        else {
            string_append_formated(append_to, "(%4.0f, %4.0f)", bb.max.x - bb.min.x, bb.max.y - bb.min.y);
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

    // Custom overlap test
    {
        Bounding_Box2 a = node->bounding_box;
        Bounding_Box2 b = other->bounding_box;
        bool x_overlap = false;
        float max_overlap = 0;
        if (a.max.x > b.min.x && a.min.x < b.max.x) {
            float overlap = math_minimum(a.max.x - b.min.x, b.max.x - a.min.x);
            max_overlap = math_maximum(overlap, max_overlap);
            x_overlap = true;
        }
        bool y_overlap = false;
        if (a.max.y > b.min.y && a.min.y < b.max.y) {
            float overlap = math_minimum(a.max.y - b.min.y, b.max.y - a.min.y);
            max_overlap = math_maximum(overlap, max_overlap);
            y_overlap = true;
        }
        if (!y_overlap || !x_overlap) {
            return false;
        }
        if (max_overlap < 3.0f) {
            return false;
        }
    }

    // if (!bounding_box_2_overlap(node->bounding_box, other->bounding_box)) {
    //     return false;
    // }

    // Other would need to wait on me, except if the z-index is higher
    if (other->position.z_index > node->position.z_index && check_z_index) {
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
        while (child_index != -1) {
            auto& child_node = nodes[child_index];
            SCOPE_EXIT(child_index = child_node.index_next_node);
            check_overlap_dependency(nodes, dependencies, child_index, other_index, false);
        }
        return true;
    }

    // Check if we overlap with any child, if so we don't need to add any additional dependencies
    bool overlapped_any = false;
    int child_index = other->index_first_child;
    while (child_index != -1) {
        auto& child_node = nodes[child_index];
        SCOPE_EXIT(child_index = child_node.index_next_node);
        if (check_overlap_dependency(nodes, dependencies, node_index, child_index, false)) {
            overlapped_any = true;
        };
    }
    if (!overlapped_any) {
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

    // Remove nodes from last frame
    {
        // Generate new node positions
        Array<int> new_node_indices = array_create_empty<int>(nodes.size);
        SCOPE_EXIT(array_destroy(&new_node_indices));
        int next_free_index = 0;
        gui_update_nodes_recursive(new_node_indices, 0, next_free_index);

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
    }

    // Layout UI 
    {
        auto& nodes = imgui.nodes;

        // Set root to windows size
        nodes[0].size[0] = gui_size_make(info.backbuffer_width, false, false);
        nodes[0].size[1] = gui_size_make(info.backbuffer_height, false, false);
        nodes[0].bounding_box.min = vec2(0.0f);
        nodes[0].bounding_box.max = vec2(info.backbuffer_width, info.backbuffer_height);

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

    // Handle input
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

                    // Check for overlap-dependencies between children
                    int next_child_index = child_node.index_next_node;
                    while (next_child_index != -1) {
                        auto& next_child_node = nodes[next_child_index];
                        SCOPE_EXIT(next_child_index = next_child_node.index_next_node);
                        check_overlap_dependency(nodes, dependencies, next_child_index, child_index, true);
                    }
                }
            }

            int next_free_in_order = 0;
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
                    auto& dependency = dependencies[execution_order[i]];
                    for (int j = 0; j < dependency.dependents.size; j++)
                    {
                        int waiting_index = dependency.dependents[j];
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

        static int skip_batches = 0;
        if (input->key_pressed[(int)Key_Code::O]) {
            skip_batches += 1;
            logg("Skip batches: %\n", skip_batches);
        }
        else if (input->key_pressed[(int)Key_Code::P]) {
            skip_batches -= 1;
            logg("Skip batches: %\n", skip_batches);
        }
        skip_batches = math_clamp(skip_batches, 0, batch_start_indices.size - 1);

        // Draw batches in order
        for (int batch = 0; batch < batch_start_indices.size - 1 - skip_batches; batch++)
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
                    float char_width = text_renderer_line_width(imgui.text_renderer, height, 1);
                    String text = node.drawable.text; // Local copy so size can be changed without affecting original code
                    vec4 c = node.drawable.color;
                    text_renderer_add_text(
                        imgui.text_renderer, text, node.bounding_box.min, Anchor::BOTTOM_LEFT, height, vec3(c.x, c.y, c.z), node.clipped_box
                    );
                    break;
                }
                case GUI_Drawable_Type::NONE: break;
                default: panic("");
                }

                // logg("    Text \"%s\"\n", primitive.data.text.characters);
                // text_renderer_add_text(
                //     imgui.text_renderer, primitive.data.text, primitive.bounding_box.min, Anchor::BOTTOM_LEFT,
                //     primitive.bounding_box.max.y - primitive.bounding_box.min.y, vec3(primitive.color.x, primitive.color.y, primitive.color.z)
                // );
                // break;
            }

            // Add draw command for batch
            int new_quad_vertex_count = rect_mesh->vertex_count;
            if (new_quad_vertex_count > quad_vertex_count) {
                render_pass_draw_count(pass_2D, rect_shader, rect_mesh, Mesh_Topology::TRIANGLES, {}, quad_vertex_count, new_quad_vertex_count - quad_vertex_count);
            }
            text_renderer_draw(imgui.text_renderer, pass_2D);
        }
    }
}




// Setters and getters for outside input
void gui_set_userdata(GUI_Handle& handle, void* userdata, gui_userdata_destroy_fn destroy_fn) {
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

void gui_set_drawable(GUI_Handle handle, GUI_Drawable drawable) {
    if (handle.index == 0) {
        panic("Cannot update root node!");
    }
    imgui.nodes[handle.index].drawable = drawable;
}

void gui_set_size(GUI_Handle handle, GUI_Size size_x, GUI_Size size_y) {
    if (handle.index == 0) {
        panic("Cannot update root node!");
    }
    imgui.nodes[handle.index].size[0] = size_x;
    imgui.nodes[handle.index].size[1] = size_y;
}

void gui_set_position(GUI_Handle handle, GUI_Position pos) {
    if (handle.index == 0) {
        panic("Cannot update root node!");
    }
    imgui.nodes[handle.index].position = pos;
}

void gui_set_layout(GUI_Handle handle, GUI_Layout layout) {
    if (handle.index == 0) {
        panic("Cannot update root node!");
    }
    imgui.nodes[handle.index].layout = layout;
}

Bounding_Box2 gui_get_node_prev_size(GUI_Handle handle) {
    return imgui.nodes[handle.index].bounding_box;
}

template<typename T>
T* gui_store_primitive(GUI_Handle parent_handle, T default_value) {
    auto node_handle = gui_push_dummy(parent_handle);
    if (node_handle.userdata == 0) {
        T* new_value = new T;
        *new_value = default_value;
        gui_set_userdata(
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



// Predefined GUI objects
void gui_push_text(GUI_Handle parent_handle, String text, float text_height_cm = .5f, vec4 color = vec4(0.0f, 0.0f, 0.0f, 1.0f))
{
    const float char_height = convertHeight(text_height_cm, Unit::CENTIMETER);
    const float char_width = text_renderer_line_width(imgui.text_renderer, char_height, 1) + 0.01f;
    gui_add_node(
        parent_handle,
        gui_size_make_fixed(char_width * text.size),
        gui_size_make_fixed(char_height),
        gui_position_make_parent_layout(),
        gui_layout_make_layered(),
        gui_drawable_make_text(text, color),
        false
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

GUI_Handle gui_push_window(GUI_Handle parent_handle, Input* input, const char* name,
    vec2 initial_pos = convertPoint(vec2(0.0f), Unit::NORMALIZED_SCREEN), vec2 initial_size = vec2(300, 500), Anchor initial_anchor = Anchor::CENTER_CENTER)
{
    // Get window info
    GUI_Window_Info* info = 0;
    {
        GUI_Window_Info initial_info;
        initial_info.drag_started = false;
        initial_info.pos = anchor_switch(initial_pos, initial_size, initial_anchor, Anchor::BOTTOM_LEFT);
        initial_info.size = initial_size;
        initial_info.drag_started = false;
        initial_info.move = false;
        initial_info.resize_bottom = false;
        initial_info.resize_top = false;
        initial_info.resize_left = false;
        initial_info.resize_right = false;
        info = gui_store_primitive<GUI_Window_Info>(parent_handle, initial_info);
    }

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
    auto window_handle = gui_add_node(
        parent_handle,
        gui_size_make_fixed(info->size.x),
        gui_size_make_fixed(info->size.y),
        gui_position_make_relative(info->pos, Anchor::BOTTOM_LEFT),
        gui_layout_make_stacked(),
        gui_drawable_make_none(),
        true);
    auto header_handle = gui_add_node(
        window_handle,
        gui_size_make_fill(true, false),
        gui_size_make_fit(),
        gui_position_make_parent_layout(),
        gui_layout_make_stacked(true, GUI_Align::MIN, vec2(3)),
        gui_drawable_make_rect(vec4(0.3f, 0.3f, 1.0f, 1.0f)),
        true
    );
    gui_push_text(header_handle, string_create_static(name));
    auto client_area = gui_add_node(
        window_handle,
        gui_size_make_fill(),
        gui_size_make_fill(),
        gui_position_make_parent_layout(),
        gui_layout_make_stacked(),
        gui_drawable_make_rect(vec4(1.0f)),
        false
    );

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
    auto window_bb = gui_get_node_prev_size(window_handle);
    const float interaction_distance = 5;

    const bool right_border = math_absolute(mouse_pos.x - window_bb.max.x) < interaction_distance;
    const bool left_border = math_absolute(mouse_pos.x - window_bb.min.x) < interaction_distance && !right_border;
    const bool bottom_border = math_absolute(mouse_pos.y - window_bb.min.y) < interaction_distance;
    const bool top_border = math_absolute(mouse_pos.y - window_bb.max.y) < interaction_distance && !bottom_border;

    // Set cursor icon for scrolling
    if (window_handle.mouse_hover || header_handle.mouse_hover)
    {
        // Handle scrolling icon
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
        gui_set_position(window_handle, gui_position_make_relative(info->pos, Anchor::BOTTOM_LEFT));
        gui_set_size(window_handle, gui_size_make_fixed(info->size.x), gui_size_make_fixed(info->size.y));
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

    return client_area;
}

bool gui_push_button(GUI_Handle parent_handle, Input* input, String text)
{
    const vec4 border_color = vec4(vec3(0.2f), 1.0f); // Some shade of gray
    const vec4 normal_color = vec4(vec3(0.8f), 1.0f); // Some shade of gray
    const vec4 hover_color = vec4(vec3(0.5f), 1.0f); // Some shade of gray
    auto border = gui_add_node(
        parent_handle,
        gui_size_make_fit(),
        gui_size_make_fit(),
        gui_position_make_parent_layout(),
        gui_layout_make_stacked(true, GUI_Align::MIN, vec2(1.2f)),
        gui_drawable_make_rect(border_color), true
    );
    auto button = gui_add_node(
        border,
        gui_size_make(convertWidth(1.0f, Unit::CENTIMETER), true, false),
        gui_size_make_fit(),
        gui_position_make_parent_layout(),
        gui_layout_make_stacked(true, GUI_Align::CENTER, vec2(1.0f)),
        gui_drawable_make_rect(normal_color),
        false
    );
    if (border.mouse_hover) {
        gui_set_drawable(button, gui_drawable_make_rect(hover_color));
    }
    gui_push_text(button, text);
    return border.mouse_hover && input->mouse_pressed[(int)Mouse_Key_Code::LEFT];
}

GUI_Handle gui_push_dummy(GUI_Handle parent_handle) {
    return gui_add_node(
        parent_handle,
        gui_size_make_fixed(0.0f),
        gui_size_make_fixed(0.0f),
        gui_position_make_relative(vec2(0.0f), Anchor::BOTTOM_LEFT, 0, false),
        gui_layout_make_stacked(),
        gui_drawable_make_none(),
        false
    );
}

// Returns true if the value was toggled
bool gui_push_toggle(GUI_Handle parent_handle, Input* input, bool* value)
{
    const vec4 border_color = vec4(vec3(0.1f), 1.0f); // Some shade of gray
    const vec4 normal_color = vec4(vec3(0.8f), 1.0f); // Some shade of gray
    const vec4 hover_color = vec4(vec3(0.5f), 1.0f); // Some shade of gray
    float height = convertHeight(.4f, Unit::CENTIMETER);
    auto border = gui_add_node(
        parent_handle,
        gui_size_make_fit(),
        gui_size_make_fit(),
        gui_position_make_parent_layout(),
        gui_layout_make_stacked(true, GUI_Align::CENTER, vec2(1.5f)),
        gui_drawable_make_rect(border_color), true
    );
    auto center = gui_add_node(
        border,
        gui_size_make_fixed(height),
        gui_size_make_fixed(height),
        gui_position_make_parent_layout(),
        gui_layout_make_stacked(true, GUI_Align::CENTER, vec2(0.0f)),
        gui_drawable_make_rect(hover_color), false
    );
    bool pressed = false;
    if (border.mouse_hover) {
        gui_set_drawable(center, gui_drawable_make_rect(hover_color));
        pressed = input->mouse_pressed[(int)Mouse_Key_Code::LEFT];
    }
    if (pressed) {
        *value = !*value;
    }
    if (*value) {
        gui_push_text(center, string_create_static("x"), .4f, vec4(1.0f, 0.0f, 0.0f, 1.0f));
    }
    return pressed;
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

    // Z-Overlap Test
    if (true) {
        auto canvas = gui_add_node(imgui.root_handle, gui_size_make_fixed(250), gui_size_make_fixed(250),
            gui_position_make_relative(vec2(0), Anchor::CENTER_CENTER),
            gui_layout_make_stacked(),
            gui_drawable_make_rect(gray),
            false
        );

        gui_add_node(imgui.root_handle, gui_size_make_fixed(250), gui_size_make_fixed(250),
            gui_position_make_relative(vec2(-235, -20), Anchor::CENTER_CENTER),
            gui_layout_make_stacked(),
            gui_drawable_make_rect(yellow),
            false
        );



        gui_add_node(canvas, gui_size_make_fixed(50), gui_size_make_fixed(50),
            gui_position_make_relative(vec2(0), Anchor::CENTER_CENTER, 0),
            gui_layout_make_stacked(),
            gui_drawable_make_rect(vec4(0, 0, 1, 0.5f)),
            false
        );
        gui_add_node(canvas, gui_size_make_fixed(50), gui_size_make_fixed(50),
            gui_position_make_relative(vec2(15), Anchor::CENTER_CENTER, 0),
            gui_layout_make_stacked(),
            gui_drawable_make_rect(green),
            false
        );
        gui_add_node(canvas, gui_size_make_fixed(50), gui_size_make_fixed(50),
            gui_position_make_relative(vec2(-15, 4), Anchor::CENTER_CENTER, 0),
            gui_layout_make_stacked(),
            gui_drawable_make_rect(red),
            false
        );

        float offset = -90;
        gui_add_node(canvas, gui_size_make_fixed(50), gui_size_make_fixed(50),
            gui_position_make_relative(vec2(offset, 0.f), Anchor::CENTER_CENTER, 2),
            gui_layout_make_stacked(),
            gui_drawable_make_rect(vec4(0, 0, 1, 0.5f)),
            false
        );
        gui_add_node(canvas, gui_size_make_fixed(50), gui_size_make_fixed(50),
            gui_position_make_relative(vec2(15 + offset, 15.f), Anchor::CENTER_CENTER, 1),
            gui_layout_make_stacked(),
            gui_drawable_make_rect(green),
            false
        );
        gui_add_node(canvas, gui_size_make_fixed(50), gui_size_make_fixed(50),
            gui_position_make_relative(vec2(-15 + offset, 4.f), Anchor::CENTER_CENTER, 0),
            gui_layout_make_stacked(),
            gui_drawable_make_rect(red),
            false
        );
    }

    // Empty window
    if (true)
    {
        auto window = gui_push_window(imgui.root_handle, input, "Test");
        // gui_push_text(window, string_create_static("Text"), 2.0f);
        // gui_push_text(window, string_create_static("Take 2"), 2.0f);
    }

    if (false)
    {
        auto window = gui_add_node(
            imgui.root_handle,
            gui_size_make_fixed(300.0f),
            gui_size_make_fixed(300.0f),
            gui_position_make_relative(vec2(0.0f), Anchor::CENTER_CENTER),
            gui_layout_make_stacked(true, GUI_Align::MIN, vec2(5.0f)),
            gui_drawable_make_rect(white),
            false
        );

        // auto window = gui_push_window(imgui.root_handle, input, "Halp?");
        // gui_push_button(window, input, string_create_static("Something"));
        // gui_push_button(window, input, string_create_static("Other"));
        // gui_push_button(window, input, string_create_static("X"));

        auto horizontal = gui_add_node(
            window,
            gui_size_make_fill(),
            gui_size_make_fill(),
            gui_position_make_parent_layout(),
            gui_layout_make_stacked(false),
            gui_drawable_make_none(),
            false
        );
        gui_add_node(
            horizontal,
            gui_size_make_fill(),
            gui_size_make_fill(),
            gui_position_make_parent_layout(),
            gui_layout_make_stacked(),
            gui_drawable_make_rect(green),
            false
        );
        gui_add_node(
            horizontal,
            gui_size_make_fill(),
            gui_size_make_fill(),
            gui_position_make_parent_layout(),
            gui_layout_make_stacked(),
            gui_drawable_make_rect(yellow),
            false
        );

        gui_add_node(
            window,
            gui_size_make_fill(400.0f),
            gui_size_make_fill(100.0f),
            gui_position_make_parent_layout(),
            gui_layout_make_stacked(),
            gui_drawable_make_rect(cyan),
            false
        );
    }

    if (false)
    {
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
        auto window = gui_add_node(
            imgui.root_handle,
            // gui_size_make_fixed(200 * a),
            gui_size_make_fixed(60),
            gui_size_make_fixed(60),
            gui_position_make_relative(mouse_pos - vec2(30, 30), Anchor::BOTTOM_LEFT),
            gui_layout_make_layered(),
            gui_drawable_make_rect(vec4(1.0f, 0.0f, 1.0f, 1.0f)),
            false
        );
        auto bar = gui_add_node(
            window,
            gui_size_make_fill(),
            gui_size_make_fixed(30.0f),
            gui_position_make_parent_layout(),
            gui_layout_make_stacked(false, GUI_Align::MAX),
            gui_drawable_make_rect(vec4(0.3f, 0.3f, 1.0f, 1.0f)),
            false
        );

        gui_push_text(bar, string_create_static("HEllo!"));
        // auto window = gui_push_window(imgui.root_handle, input, "Window");
        // if (toggle) {
        //     gui_push_text(window, string_create_static("Hello"));
        //     gui_add_node(window, gui_layout_make(),
        //         gui_size_make_absolute(bounding_box_2_make_anchor(convertPoint(vec2(0.0f), Unit::NORMALIZED_SCREEN), vec2(200), Anchor::CENTER_CENTER)),
        //         gui_drawable_make_rect(vec4(0.0f, 1.0f, 1.0f, 1.0f)),
        //         false
        //     );
        // }
    }

    // Generating UI (User code mockup, this will be somewhere else later)
    if (true)
    {
        // Make the rectangle a constant pixel size:
        int pixel_width = 100;
        int pixel_height = 100;

        auto window = gui_push_window(imgui.root_handle, input, "Test window");

        auto space = gui_add_node(
            window,
            gui_size_make_fill(),
            gui_size_make_fill(),
            gui_position_make_parent_layout(),
            gui_layout_make_stacked(true, GUI_Align::CENTER),
            gui_drawable_make_rect(cyan),
            false
        );
        bool* value = gui_store_primitive<bool>(space, false);
        gui_push_toggle(space, input, value);
        if (*value) {
            bool pressed = gui_push_button(space, input, string_create_static("Press me!"));
            int* value = gui_store_primitive<int>(space, 0);
            if (pressed) {
                *value += 1;
            }
            String tmp = string_create_formated("%d", *value);
            SCOPE_EXIT(string_destroy(&tmp));
            gui_push_text(space, tmp);
        }

        auto right_align = gui_add_node(
            window,
            gui_size_make_fill(true),
            gui_size_make_fit(),
            gui_position_make_parent_layout(),
            gui_layout_make_stacked(true, GUI_Align::MAX),
            gui_drawable_make_none(),
            false);
        gui_push_text(right_align, string_create_static("Right"));

        auto horizontal = gui_add_node(
            window,
            gui_size_make_fill(true),
            gui_size_make_fill(true),
            gui_position_make_parent_layout(),
            gui_layout_make_stacked(false),
            gui_drawable_make_none(), false);
        gui_add_node(
            horizontal,
            gui_size_make_fill(true),
            gui_size_make_fill(true),
            gui_position_make_parent_layout(),
            gui_layout_make_stacked(),
            gui_drawable_make_rect(gray),
            false);
        auto horizontal2 = gui_add_node(
            horizontal,
            gui_size_make_fill(true),
            gui_size_make_fill(true),
            gui_position_make_parent_layout(),
            gui_layout_make_stacked(false),
            gui_drawable_make_none(),
            false);
        gui_add_node(
            horizontal2,
            gui_size_make_fill(true),
            gui_size_make_fill(true),
            gui_position_make_parent_layout(),
            gui_layout_make_stacked(),
            gui_drawable_make_rect(yellow),
            false);
        gui_add_node(
            horizontal2,
            gui_size_make_fill(true),
            gui_size_make_fill(true),
            gui_position_make_parent_layout(),
            gui_layout_make_stacked(),
            gui_drawable_make_rect(green),
            false);

        auto center = gui_add_node(
            window,
            gui_size_make_fill(false),
            gui_size_make_fit(),
            gui_position_make_parent_layout(),
            gui_layout_make_stacked(true, GUI_Align::CENTER),
            gui_drawable_make_none(),
            false);
        gui_push_text(center, string_create_static("Center with very long name that you shouldn't forget!"));
        gui_add_node(
            window,
            gui_size_make_fill(true),
            gui_size_make_fill(true),
            gui_position_make_parent_layout(),
            gui_layout_make_stacked(),
            gui_drawable_make_rect(magenta),
            false);
        gui_push_text(window, string_create_static("LEFT"));

        if (false)
        {
            auto window = gui_push_window(imgui.root_handle, input, "Contender");
        }


        // Add centered text
        // gui_push_text(window, string_create_static("Hello there"));
        // auto tmp = string_create_static("Long text that, lorem ipsum");
        // auto center = gui_add_node(window, gui_layout_make(false, GUI_Align::CENTER), gui_size_make_fill(true, false), gui_drawable_make_none());
        // gui_push_text(center, tmp, 1.3f);

        // gui_push_text(window, string_create_static("Hello there"), 0.4f, gray);
        // gui_push_text(window, string_create_static("This is a new item"), 0.4f, gray);
        // auto container = gui_push_container(window, true);
        // gui_push_text(container, string_create_static("Where"), 0.4f, gray);
        // gui_push_text(container, string_create_static("Am I"), 0.4f, green);

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
        //     gui_push_text(window, string_create_static(""), 0.4f, gray); // Placeholder
        //     String tmp = string_create_formated("%3.1f", remaining);
        //     SCOPE_EXIT(string_destroy(&tmp));

        //     vec4 color = red;
        //     if (remaining < 0.0f) {
        //         string_set_characters(&tmp, "Get Pranked, lol");
        //         color = magenta;
        //     }

        //     auto layout = gui_layout_default(false, Anchor::CENTER_CENTER);
        //     auto center = gui_add_node(window, layout, white, GUI_Draw_Type::NONE, string_create_static(""));
        //     gui_push_text(center, tmp, 1.3f, color);
        // }

        // gui_new_window("Longer Test1");
        // gui_push_label(string_create_static("Hello there"));
        // gui_push_label(string_create_static("General Kenobi!"));
        // gui_new_window("Faster Test2");

        // vec2 pos = convertPoint(vec2(0.0f), Unit::NORMALIZED_SCREEN);
        // vec2 size = convertSize(vec2(1.0f), Unit::CENTIMETER);

        // primitive_renderer_add_rectangle(
        //   , imgui.primitive_renderer, bounding_box_2_make_anchor(pos, size, Anchor::BOTTOM_LEFT), red
        // );
        // gui_draw_text_cutoff(renderer, string_create_static("Hellow orld"), pos, Anchor::BOTTOM_LEFT, size, vec3(1.0f));
        // primitive_renderer_add_text(imgui.primitive_renderer, string_create_static("Asdfasdf"), pos, Anchor::CENTER_CENTER, size.y * 0.8f, magenta);
        // primitive_renderer_add_rectangle(
        //     imgui.primitive_renderer, bounding_box_2_make_anchor(pos, size, Anchor::CENTER_CENTER), blue
        // );


        // pos.x += size.x * 2.0f;
        // primitive_renderer_add_rectangle(
        //     imgui.primitive_renderer, bounding_box_2_make_anchor(pos, size, Anchor::BOTTOM_LEFT), green
        // );

    }
}

GUI_Handle gui_push_text_description(GUI_Handle parent_handle, const char* text)
{
    auto main_container = gui_add_node(
        parent_handle,
        gui_size_make_fill(),
        gui_size_make_fit(),
        gui_position_make_parent_layout(),
        gui_layout_make_stacked(false),
        gui_drawable_make_none(), false
    );

    gui_push_text(main_container, string_create_static(text));

    return gui_add_node(
        main_container,
        gui_size_make_fill(),
        gui_size_make_fit(),
        gui_position_make_parent_layout(),
        gui_layout_make_stacked(true, GUI_Align::MAX),
        gui_drawable_make_none(), false
    );
}

void gui_push_int(GUI_Handle parent_handle, Input* input, const char* text, int& value)
{
    auto container = gui_add_node(
        parent_handle,
        gui_size_make_fill(),
        gui_size_make_fit(),
        gui_position_make_parent_layout(),
        gui_layout_make_stacked(false),
        gui_drawable_make_none(), false
    );
    gui_push_text(container, string_create_static(text));

    auto fill_container = gui_add_node(
        container,
        gui_size_make_fill(),
        gui_size_make_fit(),
        gui_position_make_parent_layout(),
        gui_layout_make_stacked(true, GUI_Align::MAX),
        gui_drawable_make_none(), false
    );
    auto h_container = gui_add_node(
        fill_container,
        gui_size_make_fit(),
        gui_size_make_fit(),
        gui_position_make_parent_layout(),
        gui_layout_make_stacked(false),
        gui_drawable_make_none(), false
    );

    if (gui_push_button(h_container, input, string_create_static("+"))) {
        value += 1;
    }
    if (gui_push_button(h_container, input, string_create_static("-"))) {
        value -= 1;
    }
    String tmp = string_create_formated("%d", value);
    SCOPE_EXIT(string_destroy(&tmp));
    gui_push_text(h_container, tmp);
}

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

    imgui_initialize(text_renderer, window);
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