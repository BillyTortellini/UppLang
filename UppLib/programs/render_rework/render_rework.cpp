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

    /*
    How do I call this/How is the hierarchy generated?
    Do i need something like depth? I guess so...

    And I also guess that the most performant option would be to:
     - One batched call using depth buffer for opague objects and front to back (So lots of stuff will be called)
     - Then draw transparent objects back to front with batching (And also maybe probably do cpu culling)

    The most general case would be:
     - Lots of drawables with bounding-box + depth sort with a given depth order:
        Here I would do a depth sort, and then

    In the special case of a hierarchical GUI (meaning that we have something like a bvh), there is the additional property
    that child-items are fully contained in parent items, which means that depth sorting isn't really necessary
    --> On the CPU-side this is a little more performant because you don't have to depth sort that much
    --> But you need a special feature if you want to specify the drawing primitives in a specific order...

    I mean this is a design issue thingy, which is how I want to generate this...
    Possibilities:
     - Maybe retain current stack of last pushes
    */

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



enum class GUI_Draw_Type
{
    RECTANGLE,
    TEXT,
    NONE, // Just a container for other items (Maybe for layout or whatever)
};

struct GUI_Size
{
    float min_size;
    float padding;
    bool fill_parent;
    float fill_weight;
};

struct GUI_Layout
{
    // How size is calculated
    bool is_absolute; // Size is given by bounding_rectangle, not calculated by parent
    Bounding_Box2 absolute_size;
    GUI_Size size_x;
    GUI_Size size_y;

    // How child-items are placed
    bool stack_vertical; // Default is stack horizontal
    Anchor child_anchor; // Where to put children if they don't fill my size
};

struct GUI_Node
{
    // Position
    Bounding_Box2 bounding_box;
    bool referenced_this_frame; // Node and all child nodes will be removed at end of frame if not referenced 
    int traversal_next_child; // Reset each frame, used to match nodes

    // Infos for layout (Also only valid during layout, otherwise unspecified)
    float min_x;
    float min_y;

    // Drawing information
    GUI_Draw_Type draw_type;
    vec4 color;
    String text; // Text is fit into the bounding box
    GUI_Layout layout;

    // Tree navigation
    int index_parent;
    int index_next_node; // Next node on the same level
    int index_first_child;
    int index_last_child;
};

struct GUI_Handle
{
    int index;
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
    
    // Push root node
    GUI_Node root;
    root.bounding_box = bounding_box_2_convert(bounding_box_2_make_anchor(vec2(0.0f), vec2(2.0f), Anchor::CENTER_CENTER), Unit::NORMALIZED_SCREEN);
    root.draw_type = GUI_Draw_Type::NONE;
    root.text = string_create_empty(0);
    root.color = vec4(0.0f);
    root.referenced_this_frame = true;
    root.index_first_child = -1;
    root.index_last_child = -1;
    root.index_parent = -1;
    root.index_next_node = -1;
    root.traversal_next_child = -1;

    GUI_Layout layout;
    layout.is_absolute = true;
    layout.absolute_size = bounding_box_2_convert(bounding_box_2_make_anchor(vec2(0.0f), vec2(2.0f), Anchor::CENTER_CENTER), Unit::NORMALIZED_SCREEN);
    layout.stack_vertical = false;
    layout.child_anchor = Anchor::CENTER_CENTER;
    root.layout = layout;

    dynamic_array_push_back(&result.nodes, root);

    return result;
}

void gui_renderer_destroy(GUI_Renderer* renderer) {
    for (int i = 0; i < renderer->nodes.size; i++) {
        auto& node = renderer->nodes[i];
        string_destroy(&node.text);
    }
    dynamic_array_destroy(&renderer->nodes);
}

GUI_Handle gui_add_node(GUI_Renderer* renderer, GUI_Handle parent_handle, GUI_Layout layout, vec4 color, GUI_Draw_Type draw_type, String text) 
{
    auto& nodes = renderer->nodes;

    // Matching (Create new node or reuse node from last frame)
    int node_index = nodes[parent_handle.index].traversal_next_child;
    if (node_index == -1) { // No match found from previous frame, create new node
        GUI_Node node;
        node.index_parent = parent_handle.index;
        node.index_first_child = -1;
        node.index_last_child = -1;
        node.index_next_node = -1;
        node.text = string_create_empty(1);
        node.traversal_next_child = -1;
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
    auto& node = renderer->nodes[node_index];
    node.layout = layout;
    node.bounding_box = bounding_box_2_make_min_max(vec2(0.0f), vec2(0.0f));
    node.referenced_this_frame = true;

    // Update next traversal
    nodes[parent_handle.index].traversal_next_child = node.index_next_node;

    // Store draw information
    string_set_characters(&node.text, text.characters);
    node.draw_type = draw_type;
    node.color = color;

    GUI_Handle handle;
    handle.index = node_index;
    return handle;
}

void gui_update_nodes_recursive(GUI_Renderer* renderer, Array<int> new_node_indices, int node_index, int& next_free_node_index)
{
    auto& nodes = renderer->nodes;
    auto& node = nodes[node_index];

    // Check if the node survived
    {
        bool node_should_be_deleted = !node.referenced_this_frame;
        if (!node_should_be_deleted && node.index_parent != -1) { // Check if parent was deleted
            node_should_be_deleted = new_node_indices[node.index_parent] == -1;
        }

        if (node_should_be_deleted) {
            // FUTURE: Here I can delete stuff if nodes hold memory at some point
            string_destroy(&node.text);
            new_node_indices[node_index] = -1;
        }
        else {
            new_node_indices[node_index] = next_free_node_index;
            next_free_node_index += 1;
        }
    }

    // Update indices
    if (node.index_parent != -1) {
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

    // Reset for next frame
    node.referenced_this_frame = false;
    node.traversal_next_child = node.index_first_child;
}

void gui_layout_calculate_min_size(GUI_Renderer* renderer, int node_index)
{
    auto& nodes = renderer->nodes;
    auto& node = nodes[node_index];

    // Calculate min-sizes of all children
    int child_index = node.index_first_child;
    node.min_x = 0.0f;
    node.min_y = 0.0f;
    while (child_index != -1) {
        auto& child_node = nodes[child_index];
        SCOPE_EXIT(child_index = child_node.index_next_node);
        gui_layout_calculate_min_size(renderer, child_index);
        node.min_x += child_node.min_x;
        node.min_y += child_node.min_y;
    }

    // Add padding
    node.min_x += node.layout.size_x.padding * 2.0f;
    node.min_y += node.layout.size_y.padding * 2.0f;

    // Add my own min size
    node.min_x = math_maximum(node.min_x, node.layout.size_x.min_size);
    node.min_y = math_maximum(node.min_y, node.layout.size_y.min_size);
}

void gui_layout_layout_children(GUI_Renderer* renderer, int node_index)
{
    auto& nodes = renderer->nodes;
    auto& node = nodes[node_index];
    auto& layout = node.layout;

    // Check if there are nodes which fill parent, and the absolute weight of all of them
    int child_index = node.index_first_child;
    bool has_fill_x = false;
    bool has_fill_y = false;
    float total_weight_x = 0.0f;
    float total_weight_y = 0.0f;
    float non_fill_x = 0.0f;
    float non_fill_y = 0.0f;
    while (child_index != -1) {
        auto& child_node = nodes[child_index];
        SCOPE_EXIT(child_index = child_node.index_next_node);
        if (child_node.layout.is_absolute) {
            continue;
        }
        if (child_node.layout.size_x.fill_parent) {
            has_fill_x = true;
            total_weight_x += child_node.layout.size_x.fill_weight;
        }
        else {
            non_fill_x += child_node.min_x;
        }
        if (child_node.layout.size_y.fill_parent) {
            has_fill_y = true;
            total_weight_y += child_node.layout.size_y.fill_weight;
        }
        else {
            non_fill_y += child_node.min_y;
        }
    }

    // Set my own size if i'm an absolute unit, otherwise size is already set by parent
    if (layout.is_absolute) {
        node.bounding_box = layout.absolute_size;
    }
    const float my_height = node.bounding_box.max.y - node.bounding_box.min.y;
    const float my_width = node.bounding_box.max.x - node.bounding_box.min.x;

    // Loop over all children and set their sizes appropriately
    const vec2 anchor_dir = anchor_to_direction(layout.child_anchor);
    float stack_cursor = 0.0f;
    if (layout.stack_vertical) {
        stack_cursor = node.bounding_box.min.x + layout.size_x.padding;
    }
    else {
        stack_cursor = node.bounding_box.max.y - layout.size_y.padding;
    }

    child_index = node.index_first_child;
    while (child_index != -1)
    {
        auto& child_node = nodes[child_index];
        SCOPE_EXIT(child_index = child_node.index_next_node);
        if (child_node.layout.is_absolute) {
            child_node.bounding_box = child_node.layout.absolute_size;
            gui_layout_layout_children(renderer, child_index);
            continue;
        }

        float width = 0;
        float height = 0;
        if (child_node.layout.size_x.fill_parent) {
            if (layout.stack_vertical) {
                width = (my_width - non_fill_x) / total_weight_x * child_node.layout.size_x.fill_weight;
            }
            else {
                width = my_width;
            }
        }
        width = math_maximum(width, child_node.min_x);
        if (child_node.layout.size_y.fill_parent && !layout.stack_vertical) {
            if (!layout.stack_vertical) {
                height = (my_height - non_fill_y) / total_weight_y * child_node.layout.size_y.fill_weight;
            }
            else {
                height = my_height;
            }
        }
        height = math_maximum(height, child_node.min_y);

        if (layout.stack_vertical)
        {
            child_node.bounding_box.min.x = stack_cursor;
            child_node.bounding_box.max.x = stack_cursor + width;
            stack_cursor += width;
            // Horizontal layout now depends on anchor thingy
            if (anchor_dir.y > 0.1f) {
                child_node.bounding_box.max.y = node.bounding_box.max.y - layout.size_y.padding;
                child_node.bounding_box.min.y = child_node.bounding_box.max.y - height;
            }
            else if (anchor_dir.y < -0.1f) {
                child_node.bounding_box.min.y = node.bounding_box.min.y + layout.size_y.padding;
                child_node.bounding_box.max.y = child_node.bounding_box.min.y + height;
            }
            else {
                float center = (node.bounding_box.min.y + node.bounding_box.max.y) / 2.0f;
                child_node.bounding_box.max.y = center + height / 2.0f;
                child_node.bounding_box.min.y = center - height / 2.0f;
            }
        }
        else {
            child_node.bounding_box.max.y = stack_cursor;
            child_node.bounding_box.min.y = stack_cursor - height;
            stack_cursor -= height;
            // Horizontal layout now depends on anchor thingy
            if (anchor_dir.x > 0.1f) {
                child_node.bounding_box.max.x = node.bounding_box.max.x - layout.size_x.padding;
                child_node.bounding_box.min.x = child_node.bounding_box.max.x - width;
            }
            else if (anchor_dir.x < -0.1f) {
                child_node.bounding_box.min.x = node.bounding_box.min.x + layout.size_x.padding;
                child_node.bounding_box.max.x = child_node.bounding_box.min.x + width;
            }
            else {
                float center = (node.bounding_box.min.x + node.bounding_box.max.x) / 2.0f;
                child_node.bounding_box.max.x = center + width / 2.0f;
                child_node.bounding_box.min.x = center - width / 2.0f;
            }
        }

        // Recurse to all children
        gui_layout_layout_children(renderer, child_index);
    }
}

struct GUI_Dependency
{
    int dependency_count;
    int waiting_for_child_finish_count;
    Dynamic_Array<int> dependents_waiting_on_draw;
    Dynamic_Array<int> dependents_waiting_on_child_finish;
};


void gui_layout_set_size_absolute(GUI_Layout& layout, Bounding_Box2 area) {
    layout.is_absolute = true;
    layout.absolute_size = area;
}

void gui_layout_set_size_x(GUI_Layout& layout, float minimum, bool fill_parent = false, float fill_weight = 1.0f) {
    layout.size_x.fill_parent = fill_parent;
    layout.size_x.fill_weight = fill_weight;
    layout.size_x.min_size = minimum;
}

void gui_layout_set_size_y(GUI_Layout& layout, float minimum, bool fill_parent = false, float fill_weight = 1.0f) {
    layout.size_y.fill_parent = fill_parent;
    layout.size_y.fill_weight = fill_weight;
    layout.size_y.min_size = minimum;
}

void gui_layout_set_padding(GUI_Layout& layout, float padding_x, float padding_y) {
    layout.size_x.padding = padding_x;
    layout.size_y.padding = padding_y;
}

GUI_Layout gui_layout_default(bool stack_vertical = false, Anchor child_anchor = Anchor::TOP_LEFT) {
    GUI_Layout result;
    result.is_absolute = false;
    gui_layout_set_size_x(result, 0.0f, true);
    gui_layout_set_size_y(result, 0.0f, true);
    gui_layout_set_padding(result, 0, 0);
    result.child_anchor = child_anchor;
    result.stack_vertical = stack_vertical;
    return result;
}

void gui_push_text(GUI_Renderer* renderer, GUI_Handle parent_handle, String text, float text_height_cm = .5f, vec4 color = vec4(0.0f, 0.0f, 0.0f, 1.0f))
{
    const float char_height = convertHeight(text_height_cm, Unit::CENTIMETER);
    const float char_width = text_renderer_line_width(renderer->text_renderer, char_height, 1);
    GUI_Layout layout = gui_layout_default();
    gui_layout_set_size_x(layout, char_width * text.size + 0.01f); // Note: Currently a bad hack because text gets clipped by bounding_box size
    gui_layout_set_size_y(layout, char_height);
    gui_add_node(renderer, parent_handle, layout, color, GUI_Draw_Type::TEXT, text);
}

GUI_Handle gui_push_window(GUI_Renderer* renderer, GUI_Handle parent_handle, Bounding_Box2 area, const char* name)
{
    GUI_Layout window_layout = gui_layout_default();
    gui_layout_set_size_absolute(window_layout, area);
    auto window_handle = gui_add_node(renderer, parent_handle, window_layout, vec4(1.0f), GUI_Draw_Type::NONE, string_create_static(""));

    GUI_Layout header_layout = gui_layout_default();
    gui_layout_set_size_x(header_layout, 0, true);
    gui_layout_set_size_y(header_layout, 0);
    auto header_handle = gui_add_node(renderer, window_handle, header_layout, vec4(0.3f, 0.3f, 1.0f, 1.0f), GUI_Draw_Type::RECTANGLE, string_create_static(""));

    gui_push_text(renderer, header_handle, string_create_static(name));

    return gui_add_node(renderer, window_handle, gui_layout_default(), vec4(1.0f), GUI_Draw_Type::RECTANGLE, string_create_static(""));
}

GUI_Handle gui_push_container(GUI_Renderer* renderer, GUI_Handle parent_handle, bool stack_vertical = false) {
    GUI_Layout layout = gui_layout_default(stack_vertical);
    gui_layout_set_size_x(layout, 0);
    gui_layout_set_size_y(layout, 0);
    return gui_add_node(renderer, parent_handle, layout, vec4(0.0f, 0.0f, 1.0f, 0.4f), GUI_Draw_Type::NONE, string_create_static(""));
}

void gui_update(GUI_Renderer* renderer, Input* input)
{
    auto& core = rendering_core;
    auto& pre = core.predefined;

    // Update root size

    // Generating UI (User code mockup, this will be somewhere else later)
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

        auto window = gui_push_window(renderer, renderer->root_handle, bounding_box_2_make_anchor(vec2(100, 100), vec2(400, 600), Anchor::BOTTOM_LEFT), "Test window");
        gui_push_text(renderer, window, string_create_static("Hello there"), 0.4f, gray);
        gui_push_text(renderer, window, string_create_static("This is a new item"), 0.4f, gray);
        auto container = gui_push_container(renderer, window, true);
        gui_push_text(renderer, container, string_create_static("Where"), 0.4f, gray);
        gui_push_text(renderer, container, string_create_static("Am I"), 0.4f, green);

        {
            float t = rendering_core.render_information.current_time_in_seconds;
            if (t < 1.0f) {
                t = 0;
            }
            else {
                t = t - 1;
            }

            float remaining = 10 - t;
            gui_push_text(renderer, window, string_create_static(""), 0.4f, gray); // Placeholder
            String tmp = string_create_formated("%3.1f", remaining);
            SCOPE_EXIT(string_destroy(&tmp));

            vec4 color = red;
            if (remaining < 0.0f) {
                string_set_characters(&tmp, "Get Pranked, lol");
                color = magenta;
            }

            auto layout = gui_layout_default(false, Anchor::CENTER_CENTER);
            auto center = gui_add_node(renderer, window, layout, white, GUI_Draw_Type::NONE, string_create_static(""));
            gui_push_text(renderer, center, tmp, 1.3f, color);
        }
        
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
        assert(nodes[0].layout.is_absolute, "Root must be absolute!");
        nodes[0].layout.absolute_size = bounding_box_2_make_anchor(vec2(0.0f), convertSize(vec2(2.0f), Unit::NORMALIZED_SCREEN), Anchor::BOTTOM_LEFT);

        gui_layout_calculate_min_size(renderer, 0);
        gui_layout_layout_children(renderer, 0);
    }

    // Render
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
                auto bb = node.bounding_box;
                switch (node.draw_type)
                {
                case GUI_Draw_Type::RECTANGLE: {
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
                    vec4 c = node.color;
                    mesh_push_attribute(rect_mesh, pre.color4, { c, c, c, c, c, c });
                    break;
                }
                case GUI_Draw_Type::TEXT: {
                    float height = bb.max.y - bb.min.y;
                    float width = bb.max.x - bb.min.x;
                    float char_width = text_renderer_line_width(renderer->text_renderer, height, 1);
                    String text = node.text; // Local copy so size can be changed without affecting original code
                    if (char_width * text.size > width) {
                        text.size = width / char_width; // Note: Integer division rounding down
                    }
                    text_renderer_add_text(
                        renderer->text_renderer, text, bb.min, Anchor::BOTTOM_LEFT, height, vec3(node.color.x, node.color.y, node.color.z)
                    );
                    break;
                }
                case GUI_Draw_Type::NONE: break;
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