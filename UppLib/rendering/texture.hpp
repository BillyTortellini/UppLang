#pragma once

#include "../utility/datatypes.hpp"
#include "../datastructures/array.hpp"
#include "opengl_state.hpp"
#include "opengl_function_pointers.hpp"
#include "shader_program.hpp"
#include "../utility/binary_parser.hpp"

struct Texture_Bitmap
{
    int width;
    int height;
    int channel_count;
    Array<byte> data;
};

Texture_Bitmap texture_bitmap_create_from_data(int width, int height, int channel_count, byte* data);
Texture_Bitmap texture_bitmap_create_from_data_with_pitch(int width, int height, int pitch, byte* data);
Texture_Bitmap texture_bitmap_create_from_bitmap_with_pitch(int width, int height, int pitch, byte* data);
Texture_Bitmap texture_bitmap_create_empty(int width, int height, int channel_count);
Texture_Bitmap texture_bitmap_create_empty_mono(int width, int height, byte fill_value);
Texture_Bitmap texture_bitmap_create_test_bitmap(int size);
void texture_bitmap_inpaint_complete(Texture_Bitmap* destination, Texture_Bitmap* source, int position_x, int position_y);
void texture_bitmap_destroy(Texture_Bitmap* texture_data);

void texture_bitmap_binary_parser_write(Texture_Bitmap* bitmap, BinaryParser* parser);
Texture_Bitmap texture_bitmap_binary_parser_read(BinaryParser* parser);
Array<float> texture_bitmap_create_distance_field(Texture_Bitmap* source);
Array<float> texture_bitmap_create_distance_field_bad(Texture_Bitmap* source);
void texture_bitmap_print_distance_field(Array<float> data, int width);


struct Texture_Filtermode
{
    GLenum minification_mode; // GL_NEAREST, GL_LINEAR, GL_LINEAR_MIMAP_LINEAR, ...
    GLenum magnification_mode; // GL_NEAREST, GL_LINEAR
    GLenum u_axis_wrapping; // GL_CLAMP_TO_EDGE, GL_CLAMP_TO_BORDER (With specified color), GL_REPEAT, GL_MIRRORED_REPEAT
    GLenum v_axis_wrapping; // Same as above
};

Texture_Filtermode texture_filtermode_make(GLenum minification_mode, GLenum magnification_mode,
    GLenum u_axis_wrapping, GLenum v_axis_wrapping);
Texture_Filtermode texture_filtermode_make_nearest();
Texture_Filtermode texture_filtermode_make_linear();
Texture_Filtermode texture_filtermode_make_mipmap();

struct Texture
{
    GLuint texture_id;
    int width;
    int height;
    // The following variables determine how GL stores the colors internal, in the shaders we always get
    // floats after querying textures
    GLint internal_gpu_format; // GL_RED, GL_RG, GL_RGB, GL_RGBA, GL_DEPTH_COMPONENT, GL_DEPTH_STENCIL, or other compressed formats
    GLenum cpu_data_format; // Same as above
    GLenum sampler_type;
    Texture_Filtermode filtermode;
    bool has_mipmap;
};

Texture texture_create_from_bytes(byte* data, int width, int height, int channel_count, GLenum data_format, Texture_Filtermode filtermode, OpenGLState* state);
Texture texture_create_from_bytes(byte* data, int width, int height, int channel_count, Texture_Filtermode filtermode, OpenGLState* state);
Texture texture_create_from_texture_bitmap(Texture_Bitmap* texture_data, Texture_Filtermode filtermode, OpenGLState* state);
void texture_destroy(Texture* texture);

GLint texture_bind_to_unit(Texture* texture, OpenGLState* state);
void texture_set_texture_filtermode(Texture* texture, Texture_Filtermode filtermode, OpenGLState* state);
Texture texture_create_test_texture(OpenGLState* state);
