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


enum class Primitive_2D_Type
{
    TEXT,
    RECTANGLE,
    // LINE
    // CIRCLE
    // IMAGE
    // CUSTOM (Either a callback or a custom renderpass...)
};

struct Primitive_2D
{
    Primitive_2D_Type type;
    vec4 color; // Note that everything may be transparent!
    struct {
        String text;
    } data;
    Bounding_Box2 bounding_box; // Bounding box calculation should be aligned to pixel grid!
    int layer; // If we have multiple items in the same layer then it is drawn based on order
};

struct Primitive_Renderer
{
    Dynamic_Array<Primitive_2D> primitives;
    Text_Renderer* text_renderer;
    int current_layer;
};

Primitive_Renderer* primitive_renderer_create(Text_Renderer* text_renderer) {
    Primitive_Renderer* renderer = new Primitive_Renderer();
    renderer->primitives = dynamic_array_create_empty<Primitive_2D>(1);
    renderer->text_renderer = text_renderer;
    renderer->current_layer = 0;
    return renderer;
}

void primitive_renderer_destroy(Primitive_Renderer* renderer) {
    assert(renderer->primitives.size == 0, "Must be 0");
    dynamic_array_destroy(&renderer->primitives);
    delete renderer;
}

void primitive_renderer_set_layer(Primitive_Renderer* renderer, int layer) {
    renderer->current_layer = layer;
}

void primitive_renderer_add_rectangle(Primitive_Renderer* renderer, Bounding_Box2 box, vec4 color) {
    Primitive_2D primitive;
    primitive.type = Primitive_2D_Type::RECTANGLE;
    primitive.bounding_box = box;
    primitive.color = color;
    primitive.layer = renderer->current_layer;
    dynamic_array_push_back(&renderer->primitives, primitive);
}

void primitive_renderer_add_text(Primitive_Renderer* renderer, String text, vec2 pos, Anchor anchor, float line_height, vec4 color) {
    Primitive_2D primitive;
    primitive.type = Primitive_2D_Type::TEXT;
    primitive.bounding_box = bounding_box_2_make_anchor(pos, vec2(text_renderer_line_width(renderer->text_renderer, line_height, text.size), line_height), anchor);
    primitive.data.text = string_copy(text);
    primitive.color = color;
    primitive.layer = renderer->current_layer;
    dynamic_array_push_back(&renderer->primitives, primitive);
}

class PrimitiveComparator {
public:
    bool operator()(const Primitive_2D& a, const Primitive_2D& b) {
        return a.layer < b.layer;
    }
};

void primitive_renderer_render(Primitive_Renderer* renderer)
{
    // Stable sort items by layer
    {
        if (renderer->primitives.size == 0) {
            return;
        }
        PrimitiveComparator comp;
        std::sort(&renderer->primitives[0], &renderer->primitives[renderer->primitives.size - 1], comp);
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

    // Generate batches until we have nothing left to draw
    Dynamic_Array<int> draw_batch = dynamic_array_create_empty<int>(1);
    SCOPE_EXIT(dynamic_array_destroy(&draw_batch));
    int batch_index = 0;
    logg("\n\nPrimitive render render:\n");
    while (renderer->primitives.size != 0) 
    {
        // Generate batch
        dynamic_array_reset(&draw_batch);
        for (int i = 0; i < renderer->primitives.size; i++) 
        {
            auto& primitive = renderer->primitives[i];
            // Check for overlapps
            bool no_overlap = true;
            for (int j = 0; j < i; j++) {
                const auto& other = renderer->primitives[j];
                if (bounding_box_2_overlap(primitive.bounding_box, other.bounding_box)) {
                    no_overlap = false;
                    break;
                }
            }
            if (no_overlap) {
                dynamic_array_push_back(&draw_batch, i);
            }
        }

        logg("Draw batch #%d, item count: #%d\n", batch_index, draw_batch.size);
        batch_index += 1;
        // Draw batch
        {
            int quad_vertex_count = rect_mesh->vertex_count;
            for (int i = 0; i < draw_batch.size; i++)
            {
                auto& primitive = renderer->primitives[draw_batch[i]];
                switch (primitive.type) {
                case Primitive_2D_Type::RECTANGLE: {
                    float d = 0.0f;
                    auto& bb = primitive.bounding_box;
                    logg("    Rectangle: %4.1f/%4.1f, %4.1f %4.1f\n", bb.min.x, bb.min.y, bb.max.x - bb.min.x, bb.max.y - bb.min.y);
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
                    vec4 c = primitive.color;
                    mesh_push_attribute(rect_mesh, pre.color4, { c, c, c, c, c, c });
                    break;
                }
                case Primitive_2D_Type::TEXT: {
                    logg("    Text \"%s\"\n", primitive.data.text.characters);
                    text_renderer_add_text(
                        renderer->text_renderer, primitive.data.text, primitive.bounding_box.min, Anchor::BOTTOM_LEFT,
                        primitive.bounding_box.max.y - primitive.bounding_box.min.y, vec3(primitive.color.x, primitive.color.y, primitive.color.z)
                    );
                    break;
                }
                default: panic("");
                }
            }

            int new_quad_vertex_count = rect_mesh->vertex_count;
            if (new_quad_vertex_count > quad_vertex_count) {
                render_pass_draw_count(pass_2D, rect_shader, rect_mesh, Mesh_Topology::TRIANGLES, {}, quad_vertex_count, new_quad_vertex_count - quad_vertex_count);
            }
            text_renderer_draw(renderer->text_renderer, pass_2D);
        }

        // Remove drawn items
        {
            int next_compacted = 0;
            int primitive_index = 0;
            int draw_index = 0;
            while (primitive_index < renderer->primitives.size)
            {
                bool item_was_drawn = false;
                if (draw_index < draw_batch.size) {
                    if (draw_batch[draw_index] == primitive_index) { // Item was drawn
                        item_was_drawn = true;
                    }
                }

                if (item_was_drawn) {
                    // Destroy primitive
                    auto& primitive = renderer->primitives[draw_batch[draw_index]];
                    if (primitive.type == Primitive_2D_Type::TEXT) {
                        string_destroy(&primitive.data.text);
                    }
                    draw_index += 1;
                }
                else {
                    renderer->primitives[next_compacted] = renderer->primitives[primitive_index];
                    next_compacted += 1;
                }
                primitive_index += 1;
            }
            renderer->primitives.size = next_compacted;
        }
    }

    assert(renderer->primitives.size == 0, "All primitives must be removed by now!");
}



struct GUI_Node
{
    // Position
    Bounding_Box2 bounding_box;
    vec4 color;
    bool referenced_this_frame; // Node and all child nodes will be removed at end of frame if not referenced 

    // Tree navigation
    int index_parent;
    int index_next_node; // Next node on the same level
    int index_first_child;
    int index_last_child;
};

struct GUI_Position
{
    int node_index; // May be -1!
    int parent_node;
};

struct GUI_Renderer
{
    Primitive_Renderer* primitive_renderer;
    Dynamic_Array<GUI_Node> nodes;
    Dynamic_Array<GUI_Position> traversal;
};

GUI_Renderer gui_renderer_initialize(Text_Renderer* text_renderer)
{
    auto& pre = rendering_core.predefined;

    GUI_Renderer result;
    result.primitive_renderer = primitive_renderer_create(text_renderer);
    result.nodes = dynamic_array_create_empty<GUI_Node>(1);
    result.traversal = dynamic_array_create_empty<GUI_Position>(1);
    
    // Push root node
    GUI_Node root;
    root.bounding_box = bounding_box_2_convert(bounding_box_2_make_anchor(vec2(0.0f), vec2(2.0f), Anchor::CENTER_CENTER), Unit::NORMALIZED_SCREEN);
    root.color = vec4(0.0f);
    root.referenced_this_frame = true;
    root.index_first_child = -1;
    root.index_last_child = -1;
    root.index_parent = -1;
    root.index_next_node = -1;
    dynamic_array_push_back(&result.nodes, root);

    GUI_Position pos;
    pos.parent_node = 0;
    pos.node_index = -1;
    dynamic_array_push_back(&result.traversal, pos);
    
    return result;
}

void gui_renderer_destroy(GUI_Renderer* renderer) {
    dynamic_array_destroy(&renderer->nodes);
    dynamic_array_destroy(&renderer->traversal);
}

void gui_push_node(GUI_Renderer* renderer, Bounding_Box2 bounding_box, vec4 color) 
{
    auto& nodes = renderer->nodes;

    // Matching (Create new node or reuse node from last frame)
    int node_index;
    {
        assert(renderer->traversal.size > 0, "Must be, otherwise we went back to root?");
        auto pos = renderer->traversal[renderer->traversal.size - 1];
        if (pos.node_index == -1) { // New node is required
            GUI_Node node;
            node.index_parent = pos.parent_node;
            node.index_first_child = -1;
            node.index_last_child = -1;
            node.index_next_node = -1;
            dynamic_array_push_back(&renderer->nodes, node);
            node_index = renderer->nodes.size - 1;

            // Create links between parents and stuff
            auto& parent_node = nodes[pos.parent_node];
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
        else { // Found matching
            node_index = pos.node_index;
        }
    }
    
    // Update node data
    auto& node = renderer->nodes[node_index];
    node.bounding_box = bounding_box;
    node.color = color;
    node.referenced_this_frame = true;

    // Add node to traversal
    GUI_Position next_pos;
    next_pos.node_index = node.index_first_child;
    next_pos.parent_node = node_index;
    dynamic_array_push_back(&renderer->traversal, next_pos);
}

void gui_pop_node(GUI_Renderer* renderer) {
    // Go out one step of the traversal
    auto& traversal = renderer->traversal;
    dynamic_array_rollback_to_size(&traversal, traversal.size - 1);
    assert(traversal.size > 0, "Cannot go back to root, error must have happened when creating the GUI");

    // Step to next child
    auto& pos = traversal[traversal.size - 1];
    if (pos.node_index != -1) {
        pos.node_index = renderer->nodes[pos.node_index].index_next_node;
    }
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

    // Update root size
    renderer->nodes[0].bounding_box = bounding_box_2_make_anchor(vec2(0.0f), convertSize(vec2(2.0f), Unit::NORMALIZED_SCREEN), Anchor::BOTTOM_LEFT);

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

        gui_push_node(
            renderer,
            bounding_box_2_make_anchor(vec2(50, 50), vec2(400, 300), Anchor::BOTTOM_LEFT),
            red
        );
        gui_push_node(
            renderer,
            bounding_box_2_make_anchor(vec2(250, 50), vec2(70, 40), Anchor::BOTTOM_LEFT),
            blue
        );
        gui_pop_node(renderer);
        gui_pop_node(renderer);
        gui_push_node(
            renderer,
            bounding_box_2_make_anchor(vec2(300, 50), vec2(400, 300), Anchor::BOTTOM_LEFT),
            vec4(0.0f, 1.0f, 0.0f, 0.5f)
        );
        gui_pop_node(renderer);

        // gui_new_window(renderer, "Longer Test1");
        // gui_push_label(renderer, string_create_static("Hello there"));
        // gui_push_label(renderer, string_create_static("General Kenobi!"));
        // gui_new_window(renderer, "Faster Test2");

        // vec2 pos = convertPoint(vec2(0.0f), Unit::NORMALIZED_SCREEN);
        // vec2 size = convertSize(vec2(1.0f), Unit::CENTIMETER);

        // primitive_renderer_add_rectangle(
        //     renderer->primitive_renderer, bounding_box_2_make_anchor(pos, size, Anchor::BOTTOM_LEFT), red
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

        // Update traversal
        assert(renderer->traversal.size == 1, "Traversal must always start and stop at node 0!");
        renderer->traversal[0].node_index = renderer->nodes[0].index_first_child;

        // Update root node
        nodes[0].referenced_this_frame = true;
    }

    // Layout UI (FUTURE)

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
            // text_renderer_draw(renderer->primitive_renderer->text_renderer, pass_2D);
        }
    }
}


void gui_draw_text_cutoff(GUI_Renderer* renderer, String text, vec2 pos, Anchor anchor, vec2 size, vec3 text_color)
{
    // Trim text if too long
    float char_width = text_renderer_line_width(renderer->primitive_renderer->text_renderer, size.y, 1);
    if (char_width * text.size > size.x) {
        text.size = size.x / char_width; // Note: Integer division rounding down
    }
    primitive_renderer_add_text(renderer->primitive_renderer, text, pos, anchor, size.y, vec4(text_color, 1.0f));
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
            if (!window_handle_messages(window, true)) {
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