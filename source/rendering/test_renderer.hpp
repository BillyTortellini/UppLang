#pragma once

#include "shader_program.hpp"
#include "gpu_buffers.hpp"
#include "../math/umath.hpp"
#include "rendering_core.hpp"
#include "../utility/bounding_box.hpp"

/*
    This class support the following Primitives:
        * Rectangles
        * Circles
        * Lines
        * Curves
        * Textures
        * Triangles?

    But it is not concerned about how these elements are layed out on the screen,
    the coordinates given are in pixel coordinates of the window size
*/

struct Mesh_CPU_Buffer_Dynamic
{
    Mesh_GPU_Buffer mesh;
    int vertex_count;
    Dynamic_Array<byte> vertex_buffer;
    Dynamic_Array<u32> index_buffer;
};

Mesh_CPU_Buffer_Dynamic mesh_cpu_buffer_dynamic_create(Rendering_Core* core, int expected_face_count, int vertex_byte_size, Array<Vertex_Attribute> attributes);
void mesh_cpu_buffer_dynamic_dynamic_destroy(Mesh_CPU_Buffer_Dynamic* buffer);
void mesh_cpu_buffer_dynamic_add_face(Mesh_CPU_Buffer_Dynamic* buffer, int index_offset_0, int index_offset_1, int index_offset_2);
void mesh_cpu_buffer_dynamic_upload_data(Mesh_CPU_Buffer_Dynamic* buffer, Rendering_Core* core);

template<typename T>
void mesh_cpu_buffer_dynamic_add_vertex(Mesh_CPU_Buffer_Dynamic* buffer, T vertex)
{
    byte* b = (byte*) &vertex;
    for (int i = 0; i < sizeof(T); i++) {
        dynamic_array_push_back(&buffer->vertex_buffer, *b);
        b = b + 1;
    }
    buffer->vertex_count++;
}



enum class Anchor_2D 
{
    TOP_LEFT, TOP_CENTER, TOP_RIGHT,
    BOTTOM_LEFT, BOTTOM_CENTER, BOTTOM_RIGHT,
    CENTER_LEFT, CENTER_CENTER, CENTER_RIGHT
};
vec2 anchor_to_direction(Anchor_2D anchor);

struct Vertex_Rectangle
{
    vec3 position;
    vec3 color;
};
Vertex_Rectangle vertex_rectangle_make(vec3 pos, vec3 color);

struct Vertex_Circle
{
    vec3 position;
    vec3 color;
    vec2 uvs;
    float radius;
};
Vertex_Circle vertex_circle_make(vec3 pos, vec3 color, vec2 uvs, float radius);

struct Vertex_Line
{
    vec3 position;
    vec3 color;
    vec2 uv;
    float thickness;
    float length;
};
Vertex_Line vertex_line_make(vec3 pos, vec3 color, vec2 uv, float thickness, float length);

struct Line_Train_Point
{
    vec2 position;
    float thickness;
};

enum struct Line_Cap
{
    FLAT,
    SQUARE,
    ROUND
};

enum struct Line_Join
{
    Round,
    Bevel,
    Miter
};

struct Primitive_Renderer_2D
{
    Render_Information* render_info;
    Shader_Program* shader_rectangles;
    Shader_Program* shader_circles;
    Shader_Program* shader_lines;
    Mesh_CPU_Buffer_Dynamic mesh_rectangles;
    Mesh_CPU_Buffer_Dynamic mesh_circles;
    Mesh_CPU_Buffer_Dynamic mesh_lines;

    Dynamic_Array<Line_Train_Point> line_train_points;
    float line_train_depth;
    vec3 line_train_color;
};

Primitive_Renderer_2D* primitive_renderer_2D_create(Rendering_Core* core);
void primitive_renderer_2D_destroy(Primitive_Renderer_2D* renderer, Rendering_Core* core);
void primitive_renderer_2D_render(Primitive_Renderer_2D* renderer, Rendering_Core* core);

void primitive_renderer_2D_add_rectangle(Primitive_Renderer_2D* renderer, vec2 anchor_pos, vec2 size, float depth, Anchor_2D anchor, vec3 color);
void primitive_renderer_2D_add_circle(Primitive_Renderer_2D* renderer, vec2 center, float radius, float depth, vec3 color);
void primitive_renderer_2D_add_line(Primitive_Renderer_2D* renderer, vec2 start, vec2 end, Line_Cap start_cap, Line_Cap end_cap, float thickness, float depth, vec3 color);
void primitive_renderer_2D_start_line_train(Primitive_Renderer_2D* renderer, vec3 color, float depth);
void primitive_renderer_2D_add_line_train_point(Primitive_Renderer_2D* renderer, vec2 position, float thickness);
void primitive_renderer_2D_end_line_train(Primitive_Renderer_2D* renderer);


