#include "bachelor.hpp"

#include <iostream>

#include "../../utility/utils.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../rendering/rendering_core.hpp"
#include "../../win32/window.hpp"
#include "../../win32/timing.hpp"
#include "../../utility/gui.hpp"
#include "../../rendering/text_renderer.hpp"
#include "../../utility/file_io.hpp"

struct Shader_Stage_Code
{
    String definitions;
    String code;
};

enum class Shader_Variable_Type
{
    UNIFORM,
    VERTEX_ATTRIBUTE,
    VARIABLE,
};

struct Shader_Variable
{
    String base_name; 
    Shader_Datatype datatype;
    Shader_Variable_Type variable_type;
    Shader_Stage defined_in_stage;
    bool used_in_stage[(int)Shader_Stage::SHADER_STAGE_COUNT];
    bool is_initialized;
    bool writable;
};

struct Shader_Generator
{
    Shader_Stage current_stage;
    Shader_Stage_Code stages[(int)Shader_Stage::SHADER_STAGE_COUNT];
    bool geometry_shader_enabled;
    Dynamic_Array<Shader_Variable*> variables;
    Dynamic_Array<Shader*> allocated_shaders;
};

Shader_Generator shader_generator;

void shader_generator_start_shader();

void shader_generator_initialize() {
    for (int i = 0; i < (int)Shader_Stage::SHADER_STAGE_COUNT; i++) {
        shader_generator.stages[i].code = string_create_empty(32);
        shader_generator.stages[i].definitions = string_create_empty(32);
    }
    shader_generator.variables = dynamic_array_create<Shader_Variable*>(1);
    shader_generator.allocated_shaders = dynamic_array_create<Shader*>(1);
    shader_generator_start_shader();
}

void shader_generator_destroy() 
{
    for (int i = 0; i < (int)Shader_Stage::SHADER_STAGE_COUNT; i++) {
        string_destroy(&shader_generator.stages[i].code);
        string_destroy(&shader_generator.stages[i].definitions);
    }
    for (int i = 0; i < shader_generator.variables.size; i++) {
        delete shader_generator.variables[i];
    }
    dynamic_array_destroy(&shader_generator.variables);
    for (int i = 0; i < shader_generator.allocated_shaders.size; i++) {
        shader_destroy(shader_generator.allocated_shaders[i]);
    }
    dynamic_array_destroy(&shader_generator.allocated_shaders);
}

// For Uniforms and Vertex-Attributes, writable and is_initialized are overwritten
Shader_Variable* shader_generator_allocate_variable(
    String name, Shader_Datatype datatype, Shader_Variable_Type variable_type, bool is_initialized, bool writable)
{
    Shader_Variable* variable = new Shader_Variable;
    variable->base_name = name;
    variable->datatype = datatype;
    variable->variable_type = variable_type;

    variable->defined_in_stage = shader_generator.current_stage;
    if (variable_type == Shader_Variable_Type::VERTEX_ATTRIBUTE) {
        variable->defined_in_stage = Shader_Stage::VERTEX;
    }

    variable->is_initialized = is_initialized;
    variable->writable = writable;
    if (variable_type != Shader_Variable_Type::VARIABLE) {
        variable->is_initialized = true;
        variable->writable = false;
    }
    for (int i = 0; i < (int)Shader_Stage::SHADER_STAGE_COUNT; i++) {
        variable->used_in_stage[i] = false;
    }
    dynamic_array_push_back(&shader_generator.variables, variable);
    return variable;
}

void shader_variable_generate_access(Shader_Variable* variable, String* append_to, bool is_write_access)
{
    if (is_write_access) {
        assert(variable->writable, "");
        variable->is_initialized = true;
    }
    else {
        assert(variable->is_initialized, "");
    }
    assert((int)variable->defined_in_stage <= (int)shader_generator.current_stage, "");
    variable->used_in_stage[(int)shader_generator.current_stage] = true;

    // Append variable/stage prefix
    switch (variable->variable_type)
    {
    case Shader_Variable_Type::UNIFORM: string_append_formated(append_to, "u_"); break;
    case Shader_Variable_Type::VARIABLE: {
        // Variables only get prefixes if they are passed between stages
        if (variable->defined_in_stage != shader_generator.current_stage) {
            switch (shader_generator.current_stage) {
            case Shader_Stage::VERTEX: panic("Otherwise it's defined in another stage!"); break;
            case Shader_Stage::GEOMETRY: string_append_formated(append_to, "v_"); break; // Geometry shader always uses vertex shader output
            case Shader_Stage::FRAGMENT: {
                if (shader_generator.geometry_shader_enabled) {
                    string_append_formated(append_to, "g_");
                }
                else {
                    string_append_formated(append_to, "v_");
                }
                break;
            }
            default: panic("");
            }
        }
        break;
    }
    case Shader_Variable_Type::VERTEX_ATTRIBUTE: {
        if (variable->defined_in_stage == shader_generator.current_stage) {
            string_append_formated(append_to, "a_");
        }
        else {
            switch (shader_generator.current_stage) {
            case Shader_Stage::VERTEX: panic("Otherwise it's defined in another stage!"); break;
            case Shader_Stage::GEOMETRY: string_append_formated(append_to, "v_"); break; // Geometry shader always uses vertex shader output
            case Shader_Stage::FRAGMENT: {
                if (shader_generator.geometry_shader_enabled) {
                    string_append_formated(append_to, "g_");
                }
                else {
                    string_append_formated(append_to, "v_");
                }
            }
            default: panic("");
            }
        }
        break;
    }
    }

    string_append_formated(append_to, variable->base_name.characters);
}

void shader_generator_start_shader()
{
    auto& generator = shader_generator;
    generator.current_stage = Shader_Stage::VERTEX;
    generator.geometry_shader_enabled = false;
    for (int i = 0; i < (int)Shader_Stage::SHADER_STAGE_COUNT; i++) {
        string_reset(&shader_generator.stages[i].code);
        string_reset(&shader_generator.stages[i].definitions);
    }

    for (int i = 0; i < shader_generator.variables.size; i++) {
        delete shader_generator.variables[i];
    }
    dynamic_array_reset(&shader_generator.variables);
}

Shader_Variable* shader_generator_make_variable_from_attribute(Vertex_Attribute_Base* attribute, const char* base_name_str) 
{
    String base_name = string_create_static(base_name_str);
    auto variable = shader_generator_allocate_variable(base_name, attribute->type, Shader_Variable_Type::VERTEX_ATTRIBUTE, true, false);
    variable->defined_in_stage = Shader_Stage::VERTEX;
    String datatype_as_string = shader_datatype_as_string(attribute->type);
    string_append_formated(
        &shader_generator.stages[(int)Shader_Stage::VERTEX].definitions,
        "layout(location = %d) in %s a_%s;\n",
        attribute->binding_location, datatype_as_string.characters, base_name.characters
    );
    return variable;
}

Shader_Variable* shader_generator_make_variable_uniform(const char* base_name_str, Shader_Datatype type) {
    String base_name = string_create_static(base_name_str);
    return shader_generator_allocate_variable(base_name, type, Shader_Variable_Type::UNIFORM, true, false);
}

Shader_Variable* shader_generator_make_variable_uninitialized(const char* base_name_str, Shader_Datatype type) {
    String base_name = string_create_static(base_name_str);
    String datatype = shader_datatype_as_string(type);
    string_append_formated(
        &shader_generator.stages[(int)shader_generator.current_stage].code,
        "%s %s;\n", datatype.characters, base_name.characters
    );
    return shader_generator_allocate_variable(base_name, type, Shader_Variable_Type::VARIABLE, false, true);
}

// Returns color output variable
void shader_generator_set_shader_stage_to_fragment_shader() {
    shader_generator.current_stage = Shader_Stage::FRAGMENT;
}

void shader_generator_set_variable_to_expression_string(Shader_Variable* variable, const char* expression_string)
{
    auto code = &shader_generator.stages[(int)shader_generator.current_stage].code;
    shader_variable_generate_access(variable, code, true);
    string_append_formated(code, " = %s;\n", expression_string);
}

void shader_generator_make_assignment(Shader_Variable* to, Shader_Variable* from)
{
    assert(to->datatype == from->datatype, "");
    auto code = &shader_generator.stages[(int)shader_generator.current_stage].code;
    shader_variable_generate_access(to, code, true);
    string_append_formated(code, " = ");
    shader_variable_generate_access(from, code, false);
    string_append_formated(code, ";\n");
}

Shader* shader_generator_finish(Shader_Variable* position_output, Shader_Variable* color_output)
{
    // Set color output (Must be done before pass-through, since variable is then used later)
    {
        color_output->used_in_stage[(int)Shader_Stage::FRAGMENT] = true;
        assert(position_output->is_initialized, "");

        shader_generator.current_stage = Shader_Stage::FRAGMENT;
        Shader_Datatype color_output_type = color_output->datatype;
        assert(color_output_type == Shader_Datatype::VEC2 || color_output_type == Shader_Datatype::VEC3 || color_output_type == Shader_Datatype::VEC4, "");
        auto datatype_string = shader_datatype_as_string(color_output_type);
        string_append_formated(
            &shader_generator.stages[(int)Shader_Stage::FRAGMENT].definitions,
            "out %s o_color;\n", datatype_string.characters
        );

        auto code = &shader_generator.stages[(int)Shader_Stage::FRAGMENT].code;
        string_append_formated(code, "o_color = ");
        shader_variable_generate_access(color_output, code, false);
        string_append_formated(code, ";\n");
    }

    // Create variable pass_throughs
    for (int i = 0; i < shader_generator.variables.size; i++)
    {
        auto variable = shader_generator.variables[i];
        String datatype_string = shader_datatype_as_string(variable->datatype);

        if (variable->variable_type == Shader_Variable_Type::UNIFORM)
        {
            // Generate the uniform access in each used stage
            for (int j = 0; j < (int)Shader_Stage::SHADER_STAGE_COUNT; j++) {
                auto& stage = shader_generator.stages[j];
                if (!variable->used_in_stage[j]) {
                    continue;
                }
                string_append_formated(&stage.definitions, "uniform u_%s;\n", variable->base_name.characters);
            }
            continue;
        }

        // Check if variable is used in further stages
        int last_used_stage = -1;
        for (int j = (int)variable->defined_in_stage + 1; j < (int)Shader_Stage::SHADER_STAGE_COUNT; j++) {
            if (variable->used_in_stage[j]) {
                last_used_stage = j;
            }
        }

        // Generate pass-throughs for variable to last_used stage
        for (int j = (int)variable->defined_in_stage; j <= last_used_stage; j++)
        {
            auto& stage = shader_generator.stages[j];
            const char* stage_input_prefix = "";
            const char* stage_output_prefix = "";
            switch ((Shader_Stage)j)
            {
            case Shader_Stage::VERTEX: stage_input_prefix = "a_"; stage_output_prefix = "v_"; break;
            case Shader_Stage::GEOMETRY: stage_input_prefix = "v_"; stage_output_prefix = "g_";  break;
            case Shader_Stage::FRAGMENT: 
                stage_input_prefix = (shader_generator.geometry_shader_enabled ? "g_" : "v_"); 
                stage_output_prefix = "o_";  
                break;
            default: panic("");
            }

            if (variable->variable_type == Shader_Variable_Type::VARIABLE && (int)variable->defined_in_stage == j) {
                stage_input_prefix = "";
            }

            // Generate input
            if (j != (int)variable->defined_in_stage) {
                string_append_formated(&stage.definitions, "in %s %s%s;\n", datatype_string.characters, stage_input_prefix, variable->base_name.characters);
            }

            // Generate output
            if (j != last_used_stage) {
                string_append_formated(
                    &stage.definitions, "out %s %s%s;\n", 
                    datatype_string.characters, stage_output_prefix, variable->base_name.characters
                );
                string_append_formated(
                    &stage.code, "%s%s = %s%s;\n", 
                    stage_output_prefix, variable->base_name.characters,
                    stage_input_prefix, variable->base_name.characters
                );
            }
        }
    }

    // Set position output (Is done after pass-through for aesthetic reasons)
    {
        position_output->used_in_stage[(int)Shader_Stage::VERTEX] = true;
        assert(position_output->defined_in_stage == Shader_Stage::VERTEX, "");
        assert(position_output->is_initialized, "");

        shader_generator.current_stage = Shader_Stage::VERTEX;
        auto str = &shader_generator.stages[(int)Shader_Stage::VERTEX].code;
        switch (position_output->datatype)
        {
        case Shader_Datatype::VEC2: 
            string_append_formated(str, "gl_Position = vec4(");
            shader_variable_generate_access(position_output, str, false);
            string_append_formated(str, ".x, ");
            shader_variable_generate_access(position_output, str, false);
            string_append_formated(str, ".y, 0.0f, 1.0f);\n");
            break;
        case Shader_Datatype::VEC3:
            string_append_formated(str, "gl_Position = vec4(");
            shader_variable_generate_access(position_output, str, false);
            string_append_formated(str, ".x, ");
            shader_variable_generate_access(position_output, str, false);
            string_append_formated(str, ".y, ");
            shader_variable_generate_access(position_output, str, false);
            string_append_formated(str, ".z, 1.0f);\n");
            break;
        case Shader_Datatype::VEC4:
            string_append_formated(str, "gl_Position = ");
            shader_variable_generate_access(position_output, str, false);
            string_append_formated(str, ";\n");
            break;
        default: panic("Can only set vertex position with above types!");
        }
    }


    // Create final code
    String stage_source = string_create_empty(256);
    SCOPE_EXIT(string_destroy(&stage_source));
    Shader* shader = shader_create_empty();
    for (int i = 0; i < (int)Shader_Stage::SHADER_STAGE_COUNT; i++)
    {
        auto& stage = shader_generator.stages[i];
        auto src = &stage_source;
        string_reset(src);

        if (i == (int)Shader_Stage::GEOMETRY && !shader_generator.geometry_shader_enabled) {
            continue;
        }
        
        string_append_formated(src, "#version 430 core\n\n");
        string_append_formated(src, stage.definitions.characters);
        string_append_formated(src, "\n\nvoid main()\n{\n");

        Array<String> lines = string_split(stage.code, '\n');
        SCOPE_EXIT(string_split_destroy(lines));
        for (int i = 0; i < lines.size; i++) {
            string_append_formated(src, "    ");
            string_append_string(src, &lines[i]);
            string_append_formated(src, "\n");
        }
        string_append_formated(src, "}\n");

        printf("Stage %d:\n---------\n%s\n\n", i, src->characters);
        shader_add_shader_stage(shader, (Shader_Stage)i, stage_source);
    }

    shader_compile(shader);
    dynamic_array_push_back(&shader_generator.allocated_shaders, shader);
    return shader;
}




struct Line_Renderer
{
    Shader* shader;
    Mesh* mesh;
    Vertex_Attribute<vec4>* attribute_line_start_end;
    Vertex_Attribute<float>* attribute_width;
    // For batched rendering
    int last_draw_element_count;
    int element_count;
};

void line_renderer_on_frame_start(void* userdata)
{
    Line_Renderer* renderer = (Line_Renderer*) userdata;
    renderer->last_draw_element_count = 0;
}

Line_Renderer* line_renderer_create()
{
    auto& predefined = rendering_core.predefined;

    Line_Renderer* renderer = new Line_Renderer();
    renderer->attribute_line_start_end = vertex_attribute_make<vec4>("Line_Start_End");
    renderer->attribute_width = vertex_attribute_make<float>("Line_Width");
    renderer->mesh = rendering_core_query_mesh(
        "line_mesh",
        vertex_description_create({
            predefined.position2D, predefined.index, predefined.color4,
            renderer->attribute_line_start_end, renderer->attribute_width
        }),
        true
    );
    renderer->shader = rendering_core_query_shader("test.glsl");
    renderer->last_draw_element_count = 0;
    rendering_core_add_render_event_listener(Render_Event::FRAME_START, line_renderer_on_frame_start, renderer);
    return renderer;
}

void line_renderer_destroy(Line_Renderer* renderer)
{
    rendering_core_remove_render_event_listener(Render_Event::FRAME_START, line_renderer_on_frame_start, renderer);
    delete renderer;
}

void line_renderer_push_line(Line_Renderer* line_renderer, Array<vec2> points, float width, vec4 color)
{
    if (points.size < 2) return;

    auto mesh = line_renderer->mesh;
    auto& predefined = rendering_core.predefined;
    auto mesh_push_quad_indices = [](Mesh* mesh, uint32 i0, uint32 i1, uint32 i2, uint32 i3) {
        mesh_push_indices(mesh, {i0, i1, i3, i1, i2, i3}, false);
    };

    const float smoothing_radius = 1.0f;
    float buffer = smoothing_radius * 1.5f;
    float h = buffer + width;

    const uint32 vertex_count_start = mesh->vertex_count;
    // Generate Start points
    {
        vec2 start = points[0];
        vec2 end = points[1];
        vec2 d = vector_normalize_safe(end - start);
        vec2 n = vector_rotate_90_degree_counter_clockwise(d);

        mesh_push_attribute(mesh, predefined.position2D, { start - h * d - h * n, start - h * d + n * h });
        vec4 start_end = vec4(start.x, start.y, end.x, end.y);
        mesh_push_attribute(mesh, line_renderer->attribute_line_start_end, { start_end, start_end } );
    }

    // Generate intermediate points
    uint32 last_bot_index = vertex_count_start;
    uint32 last_top_index = vertex_count_start + 1;
    float full_length = 0;
    vec2 last_point = points[0];
    for (int i = 1; i < points.size - 1; i++) 
    {
        // Get points
        vec2 start = last_point;
        vec2 mid = points[i];
        vec2 end = points[i + 1];

        // Get angle infos
        vec2 start_to_mid = vector_normalize_safe(mid - start);
        vec2 mid_to_end = vector_normalize_safe(end - mid);
        bool right_turn = vector_dot(vector_rotate_90_degree_clockwise(-mid_to_end), start_to_mid) > 0.0f;
        bool angle_higher_90 = vector_dot(start_to_mid, mid_to_end) < 0.0f;

        // Get normals
        vec2 segment_normal = vector_rotate_90_degree_counter_clockwise(start_to_mid); // Normal of line segment start -> mid
        vec2 segment_normal_2 = vector_rotate_90_degree_counter_clockwise(mid_to_end); // Normal of line segment mid -> end
        vec2 normal = vector_normalize_safe(segment_normal + segment_normal_2); // Normal of intersection (Half angle of other normals)

        {
            // Make sure normals turn outwards
            if (!right_turn) {
                segment_normal = segment_normal * -1.0f;
                segment_normal_2 = segment_normal_2 * -1.0f;
                normal = normal * -1.0f;
            }

            float mitter_length = h / vector_dot(normal, segment_normal);

            // Calculate new points
            vec2 inner_point = mid - normal * mitter_length;
            vec2 bevel_normal = vector_normalize_safe(segment_normal + normal);
            vec2 bevel_start = bevel_normal * (h / vector_dot(bevel_normal, normal)) ;
            vec2 bevel_end = 2.0f * normal * vector_dot(bevel_start, normal) - bevel_start; // Mirror around normal to get the end

            // Add to middle
            bevel_start = mid + bevel_start;
            bevel_end = mid + bevel_end;
            vec2 bevel_center = mid + normal * h;

            uint32 index_inner_point_start  = mesh->vertex_count;
            uint32 index_inner_point_end    = mesh->vertex_count + 1;
            uint32 index_bevel_start        = mesh->vertex_count + 2;
            uint32 index_bevel_center_start = mesh->vertex_count + 3;
            uint32 index_bevel_center_end   = mesh->vertex_count + 4;
            uint32 index_bevel_end          = mesh->vertex_count + 5;
            mesh_push_attribute(
                mesh, predefined.position2D, { inner_point, inner_point, bevel_start, bevel_center, bevel_center, bevel_end }
            );
            vec4 start_mid = vec4(start.x, start.y, mid.x, mid.y);
            vec4 mid_end = vec4(mid.x, mid.y, end.x, end.y);
            mesh_push_attribute(
                mesh, line_renderer->attribute_line_start_end, { start_mid, mid_end, start_mid, start_mid, mid_end, mid_end} 
            );
             
            if (right_turn) {
                mesh_push_quad_indices(mesh, last_bot_index, index_inner_point_start, index_bevel_start, last_top_index);
                mesh_push_indices(mesh, { index_inner_point_start, index_bevel_center_start, index_bevel_start, index_inner_point_end, index_bevel_end, index_bevel_center_end }, false);
                last_bot_index = index_inner_point_end;
                last_top_index = index_bevel_end;
            }
            else {
                mesh_push_quad_indices(mesh, last_bot_index, index_bevel_start, index_inner_point_start, last_top_index);
                mesh_push_indices(mesh, { index_inner_point_start, index_bevel_start, index_bevel_center_start, index_inner_point_end, index_bevel_center_end, index_bevel_end }, false);
                last_bot_index = index_bevel_end;
                last_top_index = index_inner_point_end;
            }
        }

        last_point = mid;
    }

    // Generate end point
    {
        vec2 start = last_point;
        vec2 end = points[points.size - 1];
        vec2 d = vector_normalize_safe(end - start);
        vec2 n = vector_rotate_90_degree_counter_clockwise(d);

        uint32 index_bot = mesh->vertex_count;
        uint32 index_top = mesh->vertex_count + 1;
        mesh_push_attribute(mesh, predefined.position2D, { end + h * d - n * h, end + h * d + n * h });
        vec4 start_end = vec4(start.x, start.y, end.x, end.y);
        mesh_push_attribute(mesh, line_renderer->attribute_line_start_end, { start_end, start_end } );
        mesh_push_quad_indices(mesh, last_bot_index, index_bot, index_top, last_top_index);
    }

    // Push width and color
    {
        int added_vertex_count = mesh->vertex_count - vertex_count_start;
        Array<float> widths = mesh_push_attribute_slice(mesh, line_renderer->attribute_width, added_vertex_count);
        Array<vec4> colors = mesh_push_attribute_slice(mesh, predefined.color4, added_vertex_count);
        for (int i = 0; i < added_vertex_count; i++) {
            widths[i] = width;
            colors[i] = color;
        }
    }
}

void line_renderer_push_line(Line_Renderer* line_renderer, vec2 start, vec2 end, float width, vec4 color)
{
    vec2 points[2] = { start, end };
    line_renderer_push_line(line_renderer, array_create_static<vec2>(points, 2), width, color);
}

void line_renderer_push_circle(Line_Renderer* line_renderer, vec2 point, float radius, vec4 color)
{
    auto mesh = line_renderer->mesh;
    auto& predefined = rendering_core.predefined;

    const float smoothing_radius = 1.0f;
    float buffer = smoothing_radius * 1.5f;
    float h = buffer + radius;

    mesh_push_indices(mesh, { 0, 1, 2, 1, 3, 2 }, true);
    mesh_push_attribute(mesh, predefined.position2D, {point + vec2(-h, -h), point + vec2(h, -h), point + vec2(-h, h), point + vec2(h, h)});
    vec4 start_end = vec4(point.x, point.y, point.x, point.y);
    mesh_push_attribute(mesh, line_renderer->attribute_line_start_end, { start_end, start_end, start_end, start_end} );
    mesh_push_attribute(mesh, line_renderer->attribute_width, { radius, radius, radius, radius } );
    mesh_push_attribute(mesh, predefined.color4, { color, color, color, color } );
}

void line_renderer_draw(Line_Renderer* renderer, Render_Pass* render_pass)
{
    int index_count = renderer->mesh->index_count;
    if (renderer->last_draw_element_count == index_count) {
        return;
    }

    render_pass_draw_count(
        render_pass, renderer->shader, renderer->mesh, Mesh_Topology::TRIANGLES, {}, renderer->last_draw_element_count, index_count - renderer->last_draw_element_count
    );
    renderer->last_draw_element_count = index_count;
}




struct Edge {
    int a;
    int b;
};

Edge edge_make(int index_a, int index_b) {
    Edge result = { index_a, index_b };
    return result;
}

struct Frame {
    Dynamic_Array<Edge> edges;
};

struct Layer
{
    int current_frame;
    Dynamic_Array<Frame> frames;
    bool hidden;
    bool collisions_enabled;
    vec4 color;
};

enum class Edit_Mode
{
    NORMAL,
    VERTEX_ADD,
    EDGE_ADD,
    VERTEX_REMOVE,
    EDGE_REMOVE,
    EDGE_INCREMENT,
};

struct Vertex {
    vec2 pos;
    vec4 color;
};

struct Graph_Editor
{
    String filename;

    // Graph
    Dynamic_Array<Vertex> vertices;
    Dynamic_Array<Layer> layers;

    // Current mode
    int current_layer;
    Edit_Mode edit_mode;
    int edge_add_start_index; // -1 if no start was selected yet
    int edge_increment_index; // -1 if no edge was selected yet

    bool option_edge_no_cycles;
    bool option_draw_edges_side_by_side;
    bool option_new_frame_for_operations;

    // Camera
    vec2 camera_center;
    bool drag_start;
    vec2 drag_start_mouse_pos;
    vec2 drag_start_camera_center;
    float mouse_wheel_pos;

    // Rendering
    Line_Renderer* line_renderer;
};

Graph_Editor graph_editor;

void graph_editor_add_layer()
{
    Layer layer;
    layer.current_frame = 0;
    layer.frames = dynamic_array_create<Frame>(1);
    layer.hidden = false;
    layer.color = vec4(0.5f, 0.5f, 0.5f, 1.0f);
    layer.collisions_enabled = true;
    Frame frame;
    frame.edges = dynamic_array_create<Edge>(1);
    dynamic_array_push_back(&layer.frames, frame);
    dynamic_array_push_back(&graph_editor.layers, layer);
    graph_editor.current_layer = graph_editor.layers.size - 1;
}

bool graph_editor_load_file(const char* filepath);

void graph_editor_initialize()
{
    graph_editor.vertices = dynamic_array_create<Vertex>(1);
    graph_editor.layers = dynamic_array_create<Layer>(1);
    graph_editor.line_renderer = line_renderer_create();

    graph_editor.option_draw_edges_side_by_side = false;
    graph_editor.option_new_frame_for_operations = false;
    graph_editor.option_edge_no_cycles = false;

    graph_editor_add_layer();

    graph_editor.edit_mode = Edit_Mode::NORMAL;
    graph_editor.camera_center = vec2(0.0f);
    graph_editor.mouse_wheel_pos = 0.0f;
    graph_editor.drag_start = false;
    graph_editor.edge_add_start_index = -1;

    graph_editor.filename = string_create_empty(1);
    graph_editor_load_file("graphs/default.txt");
}

void graph_editor_shutdown()
{

}

void gui_push_color_selector(GUI_Handle parent, vec4* color, Input* input)
{
    const int SIZE = 22;

    auto button = gui_add_node(parent, gui_size_make_fixed(SIZE), gui_size_make_fixed(SIZE), gui_drawable_make_rect(*color, 2));
    gui_node_enable_input(button);
    bool* already_open = gui_store_primitive<bool>(button, false);
    bool mouse_pressed = input->mouse_pressed[(int)Mouse_Key_Code::LEFT];

    // Open picker if we click on button
    bool opened_this_frame = false;
    if (!*already_open) {
        if (mouse_pressed && button.mouse_hover) {
            *already_open = true;
            opened_this_frame = true;
        }
        else {
            return;
        }
    }

    // Show picker
    auto picker = gui_add_node(gui_root_handle(), gui_size_make_fit(), gui_size_make_fit(), gui_drawable_make_rect(vec4(1.0f), 2, vec4(0.0f, 0.0f, 0.3f, 1.0f)));
    gui_node_set_layout(picker, GUI_Stack_Direction::LEFT_TO_RIGHT, GUI_Alignment::CENTER);
    gui_node_set_padding(picker, 2, 2);
    if (opened_this_frame) {
        gui_node_set_z_index_to_highest(picker);
    }

    // Add colors to picker
    const int COLOR_COUNT = 8;
    {
        vec4 colors[COLOR_COUNT] = {
            vec4(1.0f, 1.0f, 1.0f, 1.0f),
            vec4(0.0f, 0.0f, 0.0f, 1.0f),
            vec4(1.0f, 0.0f, 0.0f, 1.0f),
            vec4(0.0f, 1.0f, 0.0f, 1.0f),
            vec4(0.0f, 0.0f, 1.0f, 1.0f),
            vec4(0.0f, 1.0f, 1.0f, 1.0f),
            vec4(1.0f, 0.0f, 1.0f, 1.0f),
            vec4(1.0f, 1.0f, 0.0f, 1.0f)
        };

        for (int i = 0; i < COLOR_COUNT; i++) {
            auto button = gui_add_node(picker, gui_size_make_fixed(SIZE), gui_size_make_fixed(SIZE), gui_drawable_make_rect(colors[i], 2, vec4(1.0f)));
            gui_node_enable_input(button);
            // Change border color on hover
            if (button.mouse_hover) {
                gui_node_update_drawable(button, gui_drawable_make_rect(colors[i], 2, vec4(0.5f, 0.5f, 0.5f, 1.0f)));
            }

            if (mouse_pressed && button.mouse_hover) {
                *color = colors[i];
                *already_open = false; // Close picker
            }
        }
    }

    // Set position of color picker
    {
        auto button_bb = gui_node_get_previous_frame_box(button);
        vec2 button_center = (button_bb.max + button_bb.min) / 2.0f;
        vec2 button_size = (button_bb.max - button_bb.min) / 2.0f;
        vec2 screen_size = vec2(rendering_core.render_information.backbuffer_width, rendering_core.render_information.backbuffer_height);

        vec2 size = vec2(COLOR_COUNT * SIZE + (COLOR_COUNT + 1) * 2, SIZE + 4);
        vec2 position = vec2(0.0f);
        // Determine position x
        position.x = button_center.x - size.x / 2.0f;
        if (position.x <= 0.0f) {
            position.x = 0.0f;
        }
        else if (position.x + size.x >= screen_size.x) {
            position.x = screen_size.x - size.x;
        }
        // Determine position y
        position.y = button_bb.max.y;
        if (button_center.y + size.y > screen_size.y) {
            // Position at bottom of button in this case
            position.y = button_bb.min.y - size.y;
        }
        gui_node_set_position_fixed(picker, position, Anchor::BOTTOM_LEFT, true);
    }


    // Close picker if we press somewhere else (or Right-Click)
    if ((mouse_pressed && !(button.mouse_hover || picker.mouse_hover || picker.mouse_hovers_child)) || input->mouse_pressed[(int)Mouse_Key_Code::RIGHT]) {
        *already_open = false;
    }
}

float distance_edge_to_point(vec2 edge_start, vec2 edge_end, vec2 point)
{
    vec2 p = point;
    vec2 a = edge_start;
    vec2 b = edge_end;

    vec2 ab = b - a;
    float t = vector_dot(p - a, ab) / math_maximum(0.00001f, ab.x * ab.x + ab.y * ab.y);
    t = math_minimum(1.0f, math_maximum(0.0f, t));
    vec2 closest = (a + ab * t);

    return vector_distance_between(closest, p);
}

void graph_editor_save_to_file()
{
    String buffer = string_create_empty(1024);
    SCOPE_EXIT(string_destroy(&buffer));

    // Format: Vertices: list of vertices
    auto& editor = graph_editor;
    for (int i = 0; i < editor.vertices.size; i++) {
        auto& v = editor.vertices[i];
        string_append_formated(&buffer, "VERTEX %f %f\n", v.pos.x, v.pos.y);
    }
    string_append_formated(
        &buffer, "OPTIONS %f %f %f %d %d %d %d\n", 
        editor.camera_center.x, editor.camera_center.y, editor.mouse_wheel_pos, 
        editor.current_layer,
        (editor.option_draw_edges_side_by_side ? 1 : 0),
        (editor.option_edge_no_cycles ? 1 : 0),
        (editor.option_new_frame_for_operations ? 1 : 0)
    );

    // Layers
    for (int i = 0; i < editor.layers.size; i++) {
        auto& layer = editor.layers[i];
        string_append_formated(&buffer, "LAYER %f %f %f %f %d %d %d\n",
            layer.color.x, layer.color.y, layer.color.z, layer.color.w, layer.current_frame, 
            (layer.hidden ? 1 : 0), (layer.collisions_enabled ? 1 : 0)
        );
        for (int j = 0; j < layer.frames.size; j++) {
            auto& frame = layer.frames[j];
            string_append_formated(&buffer, "FRAME\n");
            for (int k = 0; k < frame.edges.size; k++) {
                auto& edge = frame.edges[k];
                string_append_formated(&buffer, "EDGE %d %d\n", edge.a, edge.b);
            }
        }
    }

    file_io_write_file(editor.filename.characters, array_create_static<byte>((byte*)buffer.characters, buffer.size));
    logg("Stored to file: %s\n", editor.filename.characters);
}

bool graph_editor_load_file(const char* filepath) 
{
    auto& editor = graph_editor;
    string_reset(&editor.filename);
    string_append_formated(&editor.filename, filepath);

    auto file_opt = file_io_load_text_file(filepath);
    SCOPE_EXIT(file_io_unload_text_file(&file_opt));
    if (!file_opt.available) {
        return false;
    }

    // Reset all data of current graph editor
    for (int i = 0; i < editor.layers.size; i++) {
        auto& layer = editor.layers[i];
        for (int j = 0; j < layer.frames.size; j++) {
            auto& frame = layer.frames[j];
            dynamic_array_destroy(&frame.edges);
        }
        dynamic_array_destroy(&layer.frames);
    }
    dynamic_array_reset(&editor.layers);
    dynamic_array_reset(&editor.vertices);

    // Load data
    auto text = file_opt.value;
    auto lines = string_split(text, '\n');
    SCOPE_EXIT(string_split_destroy(lines));
    String prefix_vertex = string_create_static("VERTEX");
    String prefix_layer = string_create_static("LAYER");
    String prefix_frame = string_create_static("FRAME");
    String prefix_edge = string_create_static("EDGE");
    String prefix_options = string_create_static("OPTIONS");
    for (int i = 0; i < lines.size; i++)
    {
        auto line = lines[i];
        auto words = string_split(line, ' ');
        SCOPE_EXIT(string_split_destroy(words));
        if (words.size == 0) continue;

        if (string_equals(&words[0], &prefix_vertex)) {
            assert(words.size == 3, "");
            Vertex v;
            v.pos.x = optional_unwrap(string_parse_float(&words[1]));
            v.pos.y = optional_unwrap(string_parse_float(&words[2]));
            dynamic_array_push_back(&editor.vertices, v);
        }
        else if (string_equals(&words[0], &prefix_layer)) {
            Layer layer;
            layer.color.x = optional_unwrap(string_parse_float(&words[1]));
            layer.color.y = optional_unwrap(string_parse_float(&words[2]));
            layer.color.z = optional_unwrap(string_parse_float(&words[3]));
            layer.color.w = optional_unwrap(string_parse_float(&words[4]));
            layer.current_frame = optional_unwrap(string_parse_float(&words[5]));
            layer.hidden = optional_unwrap(string_parse_float(&words[6])) == 1 ? true : false;
            layer.collisions_enabled = optional_unwrap(string_parse_float(&words[7])) == 1 ? true : false;
            layer.frames = dynamic_array_create<Frame>(1);
            dynamic_array_push_back(&editor.layers, layer);
        }
        else if (string_equals(&words[0], &prefix_frame)) {
            Frame f;
            f.edges = dynamic_array_create<Edge>(1);
            dynamic_array_push_back(&editor.layers[editor.layers.size - 1].frames, f);
        }
        else if (string_equals(&words[0], &prefix_edge)) {
            auto& layer = editor.layers[editor.layers.size - 1];
            auto& frame = layer.frames[layer.frames.size - 1];
            Edge e;
            e.a = optional_unwrap(string_parse_int(&words[1]));
            e.b = optional_unwrap(string_parse_int(&words[2]));
            dynamic_array_push_back(&frame.edges, e);
        }
        else if (string_equals(&words[0], &prefix_options)) {
            editor.camera_center.x = optional_unwrap(string_parse_float(&words[1]));
            editor.camera_center.y = optional_unwrap(string_parse_float(&words[2]));
            editor.mouse_wheel_pos = optional_unwrap(string_parse_float(&words[3]));
            editor.current_layer = optional_unwrap(string_parse_int(&words[4]));
            editor.option_draw_edges_side_by_side = optional_unwrap(string_parse_float(&words[5])) == 1 ? true : false;
            editor.option_edge_no_cycles = optional_unwrap(string_parse_float(&words[6])) == 1 ? true : false;
            editor.option_new_frame_for_operations = optional_unwrap(string_parse_float(&words[7])) == 1 ? true : false;
        }
        else {
            panic("Invalid word on line start!\n");
        }
    }

    return true;
}

// Check if segments ab and cd intersect
// Idea from https://www.youtube.com/watch?v=bvlIYX9cgls 
bool line_segment_intersection(vec2 a, vec2 b, vec2 c, vec2 d, vec2* crossing = nullptr)
{
    float pa = (d.x - c.x) * (c.y - a.y) - (d.y - c.y) * (c.x - a.x);
    float pb = (d.x - c.x) * (b.y - a.y) - (d.y - c.y) * (b.x - a.x);
    float pc = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);

    float epsilon = 0.000001f;
    if (math_absolute(pb) < epsilon) {
        // Line is parallel
        return false;
    }

    float alpha = pa / pb;
    float beta = pc / pb;

    if (alpha >= 0.0f && alpha <= 1.0f && beta >= 0.0f && beta <= 1.0f) 
    {
        if (crossing != nullptr) {
            *crossing = a + alpha * (b - a);
        }
        return true;
    }

    return false;
}

bool graph_editor_edges_intersect(int a, int b, int c, int d)
{
    if (a == c || a == d || b == c || b == d) {
        return false;
    }
    auto& v = graph_editor.vertices;
    return line_segment_intersection(v[a].pos, v[b].pos, v[c].pos, v[d].pos);
}

bool graph_editor_new_edge_intersects_any(int a, int b)
{
    auto& editor = graph_editor;
    auto& vertices = editor.vertices;
    for (int i = 0; i < editor.layers.size; i++)
    {
        auto& layer = editor.layers[i];
        if (!layer.collisions_enabled) {
            continue;
        }

        auto& edges = layer.frames[layer.current_frame].edges;
        for (int j = 0; j < edges.size; j++)
        {
            auto& edge = edges[j];
            // If edges share an endpoint, then they don't cross
            if (edge.a == a || edge.b == b || edge.a == b || edge.b == a) {
                continue;
            }

            // Otherwise check if edge would be overlapping
            vec2 pos_a = vertices[edge.a].pos;
            vec2 pos_b = vertices[edge.b].pos;
            vec2 pos_c = vertices[a].pos;
            vec2 pos_d = vertices[b].pos;
            if (line_segment_intersection(pos_a, pos_b, pos_c, pos_d)) {
                return true;
            }
        }
    }

    return false;
}

void graph_editor_copy_current_frame()
{
    auto& editor = graph_editor;
    auto& layer = editor.layers[editor.current_layer];

    Frame& frame = layer.frames[layer.current_frame];
    Frame new_frame;
    new_frame.edges = dynamic_array_create_copy(frame.edges.data, frame.edges.size);
    
    // Delete all frames after this frame
    for (int i = layer.current_frame + 1; i < layer.frames.size; i += 1) {
        dynamic_array_destroy(&layer.frames[i].edges);
    }
    dynamic_array_rollback_to_size(&layer.frames, layer.current_frame + 1);
    dynamic_array_push_back(&layer.frames, new_frame);
    layer.current_frame = layer.frames.size - 1;
}

void graph_editor_update(Input* input, Window_State* window_state, Render_Pass* pass_gui)
{
    auto& editor = graph_editor;

    // Draw GUI
    bool gui_has_focus = false;
    bool do_save_file = false;
    bool do_load_file = false;
    {
        auto window = gui_add_node(gui_root_handle(), gui_size_make_preferred(400), gui_size_make_fill(), gui_drawable_make_rect(vec4(1.0f, 1.0f, 1.0f, 0.7f)));
        auto area = gui_push_scroll_area(window, gui_size_make_fill(), gui_size_make_fill());
        gui_node_enable_input(area);
        if (window.mouse_hover || window.mouse_hovers_child || area.mouse_hover || area.mouse_hovers_child) {
            gui_has_focus = true;
        }

        auto push_section_header = [&](const char* name) {
            // Add Header
            gui_add_node(area, gui_size_make_fill(), gui_size_make_fixed(12.0f), gui_drawable_make_none()); // Padding
            gui_push_text(area, string_create_static(name), 0.5f);
            gui_add_node(area, gui_size_make_fill(), gui_size_make_fixed(2.0f), gui_drawable_make_none()); // Padding
            gui_add_node(area, gui_size_make_fill(), gui_size_make_fixed(2.0f), gui_drawable_make_rect(vec4(0.0f, 0.0f, 0.0f, 1.0f)));
            gui_add_node(area, gui_size_make_fill(), gui_size_make_fixed(2.0f), gui_drawable_make_none()); // Padding
        };

        push_section_header("Modes");
        {
            auto vertex_options = gui_add_node(area, gui_size_make_fit(), gui_size_make_fit(), gui_drawable_make_none());
            gui_node_set_layout(vertex_options, GUI_Stack_Direction::LEFT_TO_RIGHT);
            if (gui_push_button(vertex_options, string_create_static("Add Vertices"))) {
                editor.edit_mode = Edit_Mode::VERTEX_ADD;
            }
            if (gui_push_button(vertex_options, string_create_static("Remove Vertices"))) {
                editor.edit_mode = Edit_Mode::VERTEX_REMOVE;
            }

            gui_add_node(area, gui_size_make_fill(), gui_size_make_fixed(2), gui_drawable_make_none()); // Padding
            auto edge_options = gui_add_node(area, gui_size_make_fit(), gui_size_make_fit(), gui_drawable_make_none());
            gui_node_set_layout(edge_options, GUI_Stack_Direction::LEFT_TO_RIGHT);
            if (gui_push_button(edge_options, string_create_static("Add Edges"))) {
                editor.edit_mode = Edit_Mode::EDGE_ADD;
            }
            if (gui_push_button(edge_options, string_create_static("Remove Edges"))) {
                editor.edit_mode = Edit_Mode::EDGE_REMOVE;
            }

            gui_add_node(area, gui_size_make_fill(), gui_size_make_fixed(2), gui_drawable_make_none()); // Padding
            auto advanced_options = gui_add_node(area, gui_size_make_fit(), gui_size_make_fit(), gui_drawable_make_none());
            gui_node_set_layout(advanced_options, GUI_Stack_Direction::LEFT_TO_RIGHT);
            if (gui_push_button(advanced_options, string_create_static("Edge->Triangle"))) {
                editor.edit_mode = Edit_Mode::EDGE_INCREMENT;
            }
        }

        push_section_header("File");
        if (gui_push_button(area, string_create_static("Save"))) {
            do_save_file = true;
        }
        if (gui_push_button(area, string_create_static("Load"))) {
            do_load_file = true;
        }

        String buffer = string_create_empty(64);
        SCOPE_EXIT(string_destroy(&buffer));

        push_section_header("Layers");
        for (int i = 0; i < editor.layers.size; i++)
        {
            auto& layer = editor.layers[i];
            auto layer_node = gui_add_node(area, gui_size_make_fill(), gui_size_make_fit(), gui_drawable_make_rect(vec4(0.7f, 0.7f, 0.7f, 1.0f), 2.0f, vec4(0, 0, 0, 1), 3));
            if (editor.current_layer == i) {
                gui_node_update_drawable(layer_node, gui_drawable_make_rect(vec4(0.85f, 0.85f, 0.7f, 1.0f), 2.0f, vec4(0, 0, 0, 1), 3));
            }
            else if (layer_node.mouse_hover || layer_node.mouse_hovers_child) {
                gui_node_update_drawable(layer_node, gui_drawable_make_rect(vec4(0.80f, 0.80f, 0.8f, 1.0f), 2.0f, vec4(0, 0, 0, 1), 3));
            }

            gui_node_set_layout(layer_node, GUI_Stack_Direction::LEFT_TO_RIGHT, GUI_Alignment::CENTER);
            string_reset(&buffer);
            string_append_formated(&buffer, "#%d: %d/%d", i, layer.current_frame + 1, layer.frames.size);
            gui_push_text(layer_node, buffer);
            gui_node_set_padding(layer_node, 4, 4, true);
            gui_node_enable_input(layer_node);
            if (layer_node.mouse_hover && input->mouse_pressed[(int)Mouse_Key_Code::LEFT]) {
                printf("Pressed layer!\n");
                editor.current_layer = i;
            }

            if (gui_push_button(layer_node, string_create_static("Delete")) && editor.layers.size > 1) {
                auto layer = editor.layers[i]; // Not a reference!
                dynamic_array_remove_ordered(&editor.layers, i);
                for (int j = 0; j < layer.frames.size; j++) {
                    dynamic_array_destroy(&layer.frames);
                }
                dynamic_array_destroy(&layer.frames);
                editor.current_layer = 0;
            }
            gui_push_text(layer_node, string_create_static(" H:"));
            gui_push_toggle(layer_node, &layer.hidden);
            gui_push_text(layer_node, string_create_static(" C:"));
            gui_push_toggle(layer_node, &layer.collisions_enabled);
            gui_push_text(layer_node, string_create_static(" Color:"));
            gui_push_color_selector(layer_node, &layer.color, input);
        }

        if (gui_push_button(area, string_create_static("Add Layer"))) {
            graph_editor_add_layer();
        }

        // Frame control
        push_section_header("Frames");
        {
            auto& layer = editor.layers[editor.current_layer];

            auto frame_area = gui_add_node(area, gui_size_make_fill(), gui_size_make_fit(), gui_drawable_make_none());
            gui_node_set_layout(frame_area, GUI_Stack_Direction::LEFT_TO_RIGHT);
            if (gui_push_button(frame_area, string_create_static("<"))) {
                layer.current_frame = math_maximum(0, layer.current_frame - 1);
            }
            string_reset(&buffer);
            string_append_formated(&buffer, "%d", layer.current_frame + 1);
            gui_push_text(frame_area, buffer);
            if (gui_push_button(frame_area, string_create_static(">"))) {
                layer.current_frame = math_minimum(layer.frames.size - 1, layer.current_frame + 1);
            }
            if (gui_push_button(frame_area, string_create_static("+"))) {
                Frame frame;
                frame.edges = dynamic_array_create<Edge>(1);
                dynamic_array_push_back(&layer.frames, frame);
                layer.current_frame = layer.frames.size - 1;
            }
            if (gui_push_button(frame_area, string_create_static("-")) && layer.frames.size > 1) {
                auto frame = layer.frames[layer.current_frame];
                dynamic_array_remove_ordered(&layer.frames, layer.current_frame);
                dynamic_array_destroy(&frame.edges);
                layer.current_frame = math_minimum(layer.current_frame, layer.frames.size - 1);
            }

            // Keyboard shortcuts
            if (input->key_pressed[(int)Key_Code::ARROW_LEFT]) {
                layer.current_frame = math_maximum(0, layer.current_frame - 1);
            }
            else if (input->key_pressed[(int)Key_Code::ARROW_RIGHT]) {
                layer.current_frame = math_minimum(layer.frames.size - 1, layer.current_frame + 1);
            }
        }

        push_section_header("Options");
        {
            auto other = gui_push_text_description(area, "Only add to start/end");
            gui_push_toggle(other, &editor.option_edge_no_cycles);
            other = gui_push_text_description(area, "Draw edges side_by_side");
            gui_push_toggle(other, &editor.option_draw_edges_side_by_side);
            other = gui_push_text_description(area, "New frame for Edits");
            gui_push_toggle(other, &editor.option_new_frame_for_operations);
        }
    }

    // Query passes
    Render_Pass* pass_highlights = rendering_core_query_renderpass("Highlights", pipeline_state_make_alpha_blending(), nullptr);
    Render_Pass* pass_vertices = rendering_core_query_renderpass("Vertex pass", pipeline_state_make_alpha_blending(), nullptr);
    Render_Pass* pass_edges = rendering_core_query_renderpass("Edge pass", pipeline_state_make_alpha_blending(), nullptr);
    render_pass_add_dependency(pass_gui, pass_highlights);
    render_pass_add_dependency(pass_highlights, pass_vertices);
    render_pass_add_dependency(pass_vertices, pass_edges);

    // Handle file loading
    {
        if (input->key_pressed[(int)Key_Code::S] && input->key_down[(int)Key_Code::CTRL]) {
            do_save_file = true;
        }
        else if (input->key_pressed[(int)Key_Code::O] && input->key_down[(int)Key_Code::CTRL]) {
            do_load_file = true;
        }

        if (do_save_file) {
            do_load_file = false;
            graph_editor_save_to_file();
        }

        if (do_load_file)
        {
            String tmp = string_create();
            SCOPE_EXIT(string_destroy(&tmp));
            bool available = file_io_open_file_selection_dialog(&tmp);
            if (available) {
                graph_editor_load_file(tmp.characters);
            }
        }
    }

    // Camera controls
    float zoom_level = 0.0f;
    {
        // Mouse zoom
        if (!editor.drag_start) {
            editor.mouse_wheel_pos += input->mouse_wheel_delta;
            zoom_level = math_power(1.3f, editor.mouse_wheel_pos);
        }
        zoom_level = math_power(1.3f, editor.mouse_wheel_pos);

        // Camera drag and drop (Middle mouse button)
        vec2 mouse = vec2(input->mouse_x - window_state->width / 2.0f, window_state->height / 2.0f - input->mouse_y); // In screen coordinates
        if (editor.drag_start)
        {
            if (!input->mouse_down[(int)Mouse_Key_Code::MIDDLE]) {
                editor.drag_start = false;
            }
            else {
                vec2 offset = editor.drag_start_mouse_pos - mouse;
                editor.camera_center = editor.drag_start_camera_center + offset / zoom_level;
            }
        }
        else
        {
            if (input->mouse_down[(int)Mouse_Key_Code::MIDDLE]) {
                editor.drag_start = true;
                editor.drag_start_camera_center = editor.camera_center;
                editor.drag_start_mouse_pos = mouse;
            }
        }

    }

    auto world_pos_to_screen = [&](vec2 world_position) -> vec2 {
        return (world_position - editor.camera_center) * zoom_level + vec2(window_state->width, window_state->height) / 2.0f;
    };
    auto push_circle = [&](vec2 world_position, float radius, vec4 color) {
        line_renderer_push_circle(editor.line_renderer, world_pos_to_screen(world_position), radius, color);
    };
    auto push_line = [&](vec2 world_start, vec2 world_end, float width, vec4 color) {
        line_renderer_push_line(editor.line_renderer, world_pos_to_screen(world_start), world_pos_to_screen(world_end), width, color);
    };

    vec2 mouse_pos_world; // in world coordinates
    vec2 mouse_pos_screen;
    {
        mouse_pos_screen = vec2(input->mouse_x, window_state->height - input->mouse_y);
        vec2 relative_to_center = vec2(input->mouse_x - window_state->width / 2.0f, window_state->height / 2.0f - input->mouse_y);
        mouse_pos_world = editor.camera_center + relative_to_center / zoom_level;
    }
    int closest_vertex_to_mouse_index = -1;
    float distance_to_closest = 100000.0f;
    {
        for (int i = 0; i < editor.vertices.size; i++) {
            vec2 v = editor.vertices[i].pos;
            float l = vector_length(v - mouse_pos_world);
            if (l < distance_to_closest) {
                closest_vertex_to_mouse_index = i;
                distance_to_closest = l;
            }
        }
    }

    // Edit mode switches
    if (input->key_pressed[(int)Key_Code::A]) {
        editor.edit_mode = Edit_Mode::VERTEX_ADD;
    }
    else if (input->key_pressed[(int)Key_Code::S]) {
        editor.edit_mode = Edit_Mode::EDGE_ADD;
    }
    else if (input->key_pressed[(int)Key_Code::X]) {
        editor.edit_mode = Edit_Mode::EDGE_REMOVE;
    }
    else if (input->key_pressed[(int)Key_Code::E]) {
        editor.edit_mode = Edit_Mode::EDGE_INCREMENT;
    }
    if (input->key_pressed[(int)Key_Code::ESCAPE]) {
        editor.edit_mode = Edit_Mode::NORMAL;
    }
    if (editor.edit_mode != Edit_Mode::EDGE_ADD) {
        editor.edge_add_start_index = -1;
    }
    if (editor.edit_mode != Edit_Mode::EDGE_INCREMENT) {
        editor.edge_increment_index = -1;
    }


    const vec4 white = vec4(1.0f);
    const vec4 black = vec4(0.0f, 0.0f, 0.0f, 1.0f);
    const vec4 red = vec4(1.0f, 0.0f, 0.0f, 1.0f);
    const vec4 green = vec4(0.0f, 1.0f, 0.0f, 1.0f);
    const vec4 blue = vec4(0.0f, 0.0f, 1.0f, 1.0f);
    const vec4 orange = vec4(1.0f, 0.7f, 0.0f, 1.0f);
    const vec4 gray = vec4(0.7f, 0.7f, 0.7f, 1.0f);

    const vec4 vertex_color = white;
    const vec4 edge_color = gray;
    const vec4 highlight_color = orange;

    const float vertex_radius = 12.0f;
    const float edge_width = 3.0f;

    // Reset vertex color
    for (int i = 0; i < editor.vertices.size; i++) {
        editor.vertices[i].color = vertex_color;
    }

    // Calculate number of edges per vertex
    auto& layer = editor.layers[editor.current_layer];
    auto& frame = layer.frames[layer.current_frame];
    auto& edges = frame.edges;
    auto& vertices = editor.vertices;
    Array<int> vertex_edge_count = array_create<int>(vertices.size);
    SCOPE_EXIT(array_destroy(&vertex_edge_count));
    {
        for (int i = 0; i < vertex_edge_count.size; i++) {
            vertex_edge_count[i] = 0;
        }
        for (int i = 0; i < edges.size; i++) {
            auto& edge = edges[i];
            vertex_edge_count[edge.a] += 1;
            vertex_edge_count[edge.b] += 1;
        }
    }

    // Edit mode specifics
    if (!gui_has_focus)
    {
        switch (editor.edit_mode)
        {
        case Edit_Mode::NORMAL: {
            break;
        }
        case Edit_Mode::VERTEX_ADD:
        {
            // Check if we are too close to another point
            {
                bool too_close = false;
                const float cutoff_distance = 50.0f;
                if (closest_vertex_to_mouse_index != -1 && distance_to_closest <= cutoff_distance) {
                    too_close = true;
                }
                if (too_close) {
                    push_circle(editor.vertices[closest_vertex_to_mouse_index].pos, vertex_radius * 1.1f, red);
                    line_renderer_draw(editor.line_renderer, pass_highlights);
                    break;
                }
            }

            // Otherwise add preview point
            push_circle(mouse_pos_world, vertex_radius * 0.6f, highlight_color);
            line_renderer_draw(editor.line_renderer, pass_highlights);
            if (input->mouse_pressed[(int)Mouse_Key_Code::LEFT]) {
                Vertex vertex;
                vertex.pos = mouse_pos_world;
                vertex.color = vertex_color;
                dynamic_array_push_back(&editor.vertices, vertex);
            }
            break;
        }
        case Edit_Mode::VERTEX_REMOVE:
        {
            // Skip if graph is empty
            if (closest_vertex_to_mouse_index == -1) {
                editor.edit_mode = Edit_Mode::NORMAL;
                break;
            }

            // If we are too far away from any vertex, skip
            vec2 closest = world_pos_to_screen(editor.vertices[closest_vertex_to_mouse_index].pos);
            if (vector_length(mouse_pos_screen - closest) > 50.0f) {
                break;
            }

            // Highlight nearest vertex
            {
                const float X_SIZE = 12.5f;
                line_renderer_push_line(editor.line_renderer, closest + vec2(-X_SIZE), closest + vec2(X_SIZE), 3.0f, red);
                line_renderer_push_line(editor.line_renderer, closest + vec2(-X_SIZE, X_SIZE), closest + vec2(X_SIZE, -X_SIZE), 3.0f, red);
                line_renderer_draw(editor.line_renderer, pass_highlights);
            }

            // Remove if pressed
            if (input->mouse_pressed[(int)Mouse_Key_Code::LEFT])
            {
                int index = closest_vertex_to_mouse_index;
                dynamic_array_remove_ordered(&editor.vertices, index);
                for (int i = 0; i < editor.layers.size; i++) {
                    auto& layer = editor.layers[i];
                    for (int j = 0; j < layer.frames.size; j++) {
                        auto& frame = layer.frames[j];
                        for (int k = 0; k < frame.edges.size; k++) {
                            auto& edge = frame.edges[k];
                            if (edge.a == index || edge.b == index) {
                                dynamic_array_swap_remove(&frame.edges, k);
                                k = k - 1;
                                continue;
                            }
                            else {
                                if (edge.a > index) {
                                    edge.a -= 1;
                                }
                                if (edge.b > index) {
                                    edge.b -= 1;
                                }
                            }
                        }
                    }
                }
            }

            break;
        }
        case Edit_Mode::EDGE_ADD:
        {
            // Return if no vertices exist
            if (closest_vertex_to_mouse_index == -1) {
                break;
            }

            // Check which points are valid for edge add
            Dynamic_Array<int> valid_vertex_indices = dynamic_array_create<int>(vertices.size);
            SCOPE_EXIT(dynamic_array_destroy(&valid_vertex_indices));
            bool highlight_valid_vertices = false;
            if (editor.edge_add_start_index == -1)
            {
                if (editor.option_edge_no_cycles)
                {
                    highlight_valid_vertices = true;
                    // Only vertices with edge_count 0 or edge_count 1 are valid
                    for (int i = 0; i < vertices.size; i++) {
                        if (vertex_edge_count[i] < 2) {
                            dynamic_array_push_back(&valid_vertex_indices, i);
                        }
                    }
                }
                else {
                    // All points are valid otherwise
                    for (int i = 0; i < editor.vertices.size; i++) {
                        dynamic_array_push_back(&valid_vertex_indices, i);
                    }
                }
            }
            else
            {
                // Only add vertices which fulfill all criterias
                for (int i = 0; i < vertices.size; i++)
                {
                    auto& vertex = vertices[i];

                    // Don't connect to same edge
                    if (i == editor.edge_add_start_index) {
                        continue;
                    }
                    // Check for cycles
                    if (editor.option_edge_no_cycles && vertex_edge_count[i] > 1) {
                        continue;
                    }
                    // Check for crossings
                    if (graph_editor_new_edge_intersects_any(editor.edge_add_start_index, i)) {
                        continue;
                    }

                    dynamic_array_push_back(&valid_vertex_indices, i);
                }

                if (editor.option_edge_no_cycles) {
                    highlight_valid_vertices = true;
                }
            }

            if (highlight_valid_vertices)
            {
                for (int i = 0; i < valid_vertex_indices.size; i++) {
                    vertices[valid_vertex_indices[i]].color = highlight_color;
                }
            }

            // Try to snap mouse pos to closest point within radius
            vec2 snapped_pos = mouse_pos_world;
            bool invalid;
            bool too_far_away = false;
            {
                const float cutoff_distance = 100.0f;
                if (distance_to_closest >= cutoff_distance) {
                    invalid = true;
                    too_far_away = true;
                    snapped_pos = mouse_pos_world;
                }
                else {
                    snapped_pos = editor.vertices[closest_vertex_to_mouse_index].pos;
                    // Check if closest is valid
                    invalid = true;
                    for (int i = 0; i < valid_vertex_indices.size; i++) {
                        if (closest_vertex_to_mouse_index == valid_vertex_indices[i]) {
                            invalid = false;
                            break;
                        }
                    }
                }

                if (editor.edge_add_start_index == closest_vertex_to_mouse_index) {
                    invalid = true;
                }
            }

            const bool mouse_pressed = input->mouse_pressed[(int)Mouse_Key_Code::LEFT];
            if (editor.edge_add_start_index == -1)
            {
                if (mouse_pressed && !invalid) {
                    editor.edge_add_start_index = closest_vertex_to_mouse_index;
                }

                // Draw highlight around vertex
                if (invalid) {
                    push_circle(snapped_pos, vertex_radius * 1.1f, red);
                }
                else {
                    push_circle(snapped_pos, vertex_radius * 1.1f, green);
                }
                line_renderer_draw(editor.line_renderer, pass_highlights);
            }
            else
            {
                Edge edge;
                edge.a = closest_vertex_to_mouse_index;
                edge.b = editor.edge_add_start_index;
                if (edge.a < edge.b) {
                    int swap = edge.a;
                    edge.a = edge.b;
                    edge.b = swap;
                }

                // Check if line already exists
                bool already_exists = false;
                if (!too_far_away)
                {
                    for (int i = 0; i < frame.edges.size; i++) {
                        auto& other = frame.edges[i];
                        if ((edge.a == other.a && edge.b == other.b) || (edge.a == other.b && edge.b == other.a)) {
                            already_exists = true;
                            break;
                        }
                    }
                }

                // Draw preview line
                if (!already_exists) {
                    push_line(snapped_pos, editor.vertices[editor.edge_add_start_index].pos, edge_width * 1.1f, invalid ? red : highlight_color);
                    line_renderer_draw(editor.line_renderer, pass_highlights);
                }

                if (mouse_pressed && !invalid)
                {
                    if (!already_exists) {
                        if (editor.option_new_frame_for_operations) {
                            graph_editor_copy_current_frame();
                        }
                        dynamic_array_push_back(&layer.frames[layer.current_frame].edges, edge); 
                    }

                    if (input->key_down[(int)Key_Code::SHIFT]) {
                        editor.edge_add_start_index = -1;
                    }
                    else {
                        editor.edge_add_start_index = closest_vertex_to_mouse_index;
                    }
                }
            }
            break;
        }
        case Edit_Mode::EDGE_REMOVE:
        {
            auto& layer = editor.layers[editor.current_layer];
            auto& frame = layer.frames[layer.current_frame];

            if (frame.edges.size == 0) {
                editor.edit_mode = Edit_Mode::NORMAL;
                break;
            }

            // Find closest edge
            int closest_edge_index = -1;
            float closest_distance = 1000000.0f;
            for (int i = 0; i < frame.edges.size; i++) {
                auto& edge = frame.edges[i];
                float d = distance_edge_to_point(editor.vertices[edge.a].pos, editor.vertices[edge.b].pos, mouse_pos_world);
                if (d < closest_distance) {
                    closest_edge_index = i;
                    closest_distance = d;
                }
            }

            if (closest_edge_index == -1) {
                break;
            }

            // Highlight line
            push_line(
                editor.vertices[frame.edges[closest_edge_index].a].pos,
                editor.vertices[frame.edges[closest_edge_index].b].pos,
                edge_width * 1.1f, red
            );
            line_renderer_draw(editor.line_renderer, pass_highlights);

            if (input->mouse_pressed[(int)Mouse_Key_Code::LEFT]) {
                if (editor.option_new_frame_for_operations) {
                    graph_editor_copy_current_frame();
                }
                auto& edges = layer.frames[layer.current_frame].edges; // Update pointer after potential copy
                dynamic_array_swap_remove(&edges, closest_edge_index);
            }

            break;
        }
        case Edit_Mode::EDGE_INCREMENT:
        {
            auto& vertices = editor.vertices;
            auto& layer = editor.layers[editor.current_layer];
            auto& edges = layer.frames[layer.current_frame].edges;

            // Exit if there are no edges
            if (edges.size == 0) {
                editor.edit_mode = Edit_Mode::NORMAL;
                break;
            }

            // Find closest edge
            int closest_edge_index = -1;
            float closest_distance = 1000000.0f;
            for (int i = 0; i < edges.size; i++) {
                auto& edge = edges[i];
                float d = distance_edge_to_point(vertices[edge.a].pos, vertices[edge.b].pos, mouse_pos_world);
                if (d < closest_distance) {
                    closest_edge_index = i;
                    closest_distance = d;
                }
            }

            if (editor.edge_increment_index == -1)
            {
                // Highlight closest line
                push_line(
                    editor.vertices[edges[closest_edge_index].a].pos,
                    editor.vertices[edges[closest_edge_index].b].pos,
                    edge_width * 1.1f, highlight_color
                );
                line_renderer_draw(editor.line_renderer, pass_highlights);

                if (input->mouse_pressed[(int)Mouse_Key_Code::LEFT]) {
                    editor.edge_increment_index = closest_edge_index;
                }
            }
            else
            {
                // Highlight all vertices that are valid (e.g. 2 lines can connect without overlap)
                bool closest_to_mouse_valid = false;

                auto& edge = edges[editor.edge_increment_index];
                for (int i = 0; i < vertices.size; i++)
                {
                    auto v = vertices[i].pos;
                    if (vertex_edge_count[i] != 0) {
                        continue;
                    }

                    // Skip if there would be intersections
                    if (graph_editor_new_edge_intersects_any(edge.a, i) || graph_editor_new_edge_intersects_any(edge.b, i)) {
                        continue;
                    }

                    // Highlight vertex
                    vertices[i].color = highlight_color;
                    if (closest_vertex_to_mouse_index == i) {
                        closest_to_mouse_valid = true;
                    }
                }

                // Draw new lines
                {
                    push_line(vertices[edge.a].pos, vertices[closest_vertex_to_mouse_index].pos, edge_width, highlight_color);
                    push_line(vertices[edge.b].pos, vertices[closest_vertex_to_mouse_index].pos, edge_width, highlight_color);
                    line_renderer_draw(editor.line_renderer, pass_highlights);
                }

                if (input->mouse_pressed[(int)Mouse_Key_Code::LEFT] && closest_to_mouse_valid) 
                {
                    Edge edge = edges[editor.edge_increment_index];
                    if (editor.option_new_frame_for_operations) {
                        graph_editor_copy_current_frame();
                    }
                    auto& edges = layer.frames[layer.current_frame].edges; // Update pointer after potential copy
                    dynamic_array_swap_remove(&edges, editor.edge_increment_index);
                    Edge new1;
                    new1.a = edge.a;
                    new1.b = closest_vertex_to_mouse_index;
                    Edge new2;
                    new2.a = edge.b;
                    new2.b = closest_vertex_to_mouse_index;
                    dynamic_array_push_back(&edges, new1);
                    dynamic_array_push_back(&edges, new2);
                    editor.edge_increment_index = -1;
                }
            }


            break;
        }
        default: panic("");
        }
    }

    // Render graph
    {
        for (int i = 0; i < editor.vertices.size; i++) {
            auto& vertex = editor.vertices[i];
            push_circle(vertex.pos, vertex_radius, vertex.color);
        }
        line_renderer_draw(editor.line_renderer, pass_vertices);

        auto draw_layer = [&](int index)
        {
            auto& layer = editor.layers[index];
            if (layer.hidden) return;
            auto& frame = layer.frames[layer.current_frame];
            for (int j = 0; j < frame.edges.size; j++)
            {
                auto& edge = frame.edges[j];

                // Check if any other layer also has this edge
                int line_count = 1;
                int line_index = 0;
                if (editor.option_draw_edges_side_by_side)
                {
                    for (int k = 0; k < editor.layers.size; k++) {
                        if (k == index) { continue; }
                        auto& other_layer = editor.layers[k];
                        if (other_layer.hidden) { continue; }
                        auto& other_edges = other_layer.frames[other_layer.current_frame].edges;
                        for (int other_edge_index = 0; other_edge_index < other_edges.size; other_edge_index += 1) {
                            auto& other_edge = other_edges[other_edge_index];
                            if ((other_edge.a == edge.a && other_edge.b == edge.b) || (other_edge.a == edge.b && other_edge.b == edge.a)) {
                                line_count += 1;
                                if (k < index) {
                                    line_index += 1;
                                }
                            }
                        }
                    }
                }

                vec2 a = editor.vertices[edge.a].pos;
                vec2 b = editor.vertices[edge.b].pos;
                vec2 offset = vector_rotate_90_degree_clockwise(vector_normalize(b - a)) * edge_width * 2.0f / zoom_level * (line_index - line_count / 2);
                push_line(a + offset, b + offset, edge_width, layer.color);
            }
            line_renderer_draw(editor.line_renderer, pass_edges);
        };

        // Draw all layers in order (except selected one)
        for (int i = 0; i < editor.layers.size; i++) {
            if (i == editor.current_layer) continue;
            draw_layer(i);
        }

        // Draw current layer on top
        draw_layer(editor.current_layer);
    }
}


void bachelor_thesis()
{
    Window* window = window_create("Thesis", 0);
    SCOPE_EXIT(window_destroy(window));
    Input* input = window_get_input(window);
    Window_State* window_state = window_get_window_state(window);
    rendering_core_initialize(window_state->width, window_state->height, window_state->dpi);
    SCOPE_EXIT(rendering_core_destroy());
    Timer timer = timer_make();
    Camera_3D* camera = camera_3D_create(90.0f, 0.01f, 100.0f);
    SCOPE_EXIT(camera_3D_destroy(camera));
    shader_generator_initialize();
    SCOPE_EXIT(shader_generator_destroy());
    Text_Renderer* text_renderer = text_renderer_create_from_font_atlas_file("resources/fonts/glyph_atlas_new.atlas");
    SCOPE_EXIT(text_renderer_destroy(text_renderer));
    gui_initialize(text_renderer, window);
    SCOPE_EXIT(gui_destroy());
    graph_editor_initialize();
    SCOPE_EXIT(graph_editor_shutdown());

    auto pass_gui = rendering_core_query_renderpass("GUI_PASS", pipeline_state_make_alpha_blending(), nullptr);
    window_load_position(window, "window_pos.set");

    while (true)
    {
        // Window Handling
        input_reset(input);
        window_handle_messages(window, true);
        if (input->close_request_issued || (input->key_down[(int)Key_Code::CTRL] && input->key_down[(int)Key_Code::W])) {
            window_save_position(window, "window_pos.set");
            window_close(window);
            break;
        }
        rendering_core_prepare_frame(timer_current_time_in_seconds(&timer), window_state->width, window_state->height);
        graph_editor_update(input, window_state, pass_gui);
        gui_update_and_render(pass_gui);
        rendering_core_render(camera, Framebuffer_Clear_Type::COLOR_AND_DEPTH);
        window_swap_buffers(window);
        text_renderer_reset(text_renderer);
    }
}