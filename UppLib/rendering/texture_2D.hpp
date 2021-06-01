#pragma once

#include "opengl_function_pointers.hpp"
#include "../datastructures/array.hpp"

struct Rendering_Core;
struct Shader_Program;
struct Texture_Bitmap;

enum class Texture_Minification_Mode
{
    NEAREST_PIXEL_VALUE = GL_NEAREST,
    BILINEAR_INTERPOLATION = GL_LINEAR,
    TRILINEAR_INTERPOLATION = GL_LINEAR_MIPMAP_LINEAR, // Requires mipmap
};

enum class Texture_Magnification_Mode
{
    NEAREST_PIXEL_VALUE = GL_NEAREST,
    BILINEAR_INTERPOLATION = GL_LINEAR,
};

enum class Texture_Wrapping_Mode
{
    CLAMP_TO_EDGE = GL_CLAMP_TO_EDGE,
    REPEAT = GL_REPEAT,
    MIRROR_REPEAT = GL_MIRRORED_REPEAT,
    //CLAMP_TO_BORDER_COLOR = GL_CLAMP_TO_BORDER, // Maybe support later, color needs to be saved somewhere
};

struct Texture_Sampling_Mode
{
    Texture_Minification_Mode minification_mode;
    Texture_Magnification_Mode magnification_mode;
    Texture_Wrapping_Mode u_wrapping_mode;
    Texture_Wrapping_Mode v_wrapping_mode;
};

Texture_Sampling_Mode texture_sampling_mode_make(Texture_Minification_Mode min_mode, Texture_Magnification_Mode mag_mode,
    Texture_Wrapping_Mode u_wrapping, Texture_Wrapping_Mode v_wrapping);
Texture_Sampling_Mode texture_sampling_mode_make_nearest();
Texture_Sampling_Mode texture_sampling_mode_make_bilinear();
Texture_Sampling_Mode texture_sampling_mode_make_trilinear();

enum class Texture_2D_Type
{
    DEPTH = GL_DEPTH_COMPONENT,
    DEPTH_STENCIL = GL_DEPTH_STENCIL,
    RED_U8 = GL_RED,
    RED_GREEN_U8 = GL_RG,
    RED_GREEN_BLUE_U8 = GL_RGB,
    RED_GREEN_BLUE_ALPHA_U8 = GL_RGBA,
    RED_F16 = GL_R16F,
    RED_GREEN_F16 = GL_RG16F,
    RED_GREEN_BLUE_F16 = GL_RGB16F,
    RED_GREEN_BLUE_ALPHA_F16 = GL_RGBA16F,
    RED_F32 = GL_R32F,
    RED_GREEN_F32 = GL_RG32F,
    RED_GREEN_BLUE_F32 = GL_RGB32F,
    RED_GREEN_BLUE_ALPHA_F32 = GL_RGBA32F,
};

bool texture_2D_type_is_float(Texture_2D_Type type);
bool texture_2D_type_is_int(Texture_2D_Type type);
int texture_2D_type_channel_count(Texture_2D_Type type);
int texture_2D_type_pixel_byte_size(Texture_2D_Type type);

struct Texture_2D
{
    Texture_2D_Type type;
    GLuint texture_id;
    int width;
    int height;
    Texture_Sampling_Mode sampling_mode;
    bool has_mipmap;

    GLint internal_gpu_format; // GL_RED, GL_RG, GL_RGB, GL_RGBA, GL_DEPTH_COMPONENT, GL_DEPTH_STENCIL, or other (e.g. compressed) formats
};

Texture_2D texture_2D_create_empty(Rendering_Core* core, Texture_2D_Type type, int width, int height, Texture_Sampling_Mode sample_mode);
Texture_2D texture_2D_create_from_bytes(Rendering_Core* core, Texture_2D_Type type, Array<byte> data, int width, int height, Texture_Sampling_Mode sample_mode);
Texture_2D texture_2D_create_from_texture_bitmap(Rendering_Core* core, Texture_Bitmap* texture_data, Texture_Sampling_Mode sample_mode);
void texture_2D_destroy(Texture_2D* texture);

void texture_2D_update_texture_data(Texture_2D* texture, Rendering_Core* core, Array<byte> data, bool create_mipmap);
void texture_2D_resize(Texture_2D* texture, Rendering_Core* core, int width, int height, bool create_mipmap);
GLint texture_2D_bind_to_next_free_unit(Texture_2D* texture, Rendering_Core* core);
void texture_2D_set_sampling_mode(Texture_2D* texture, Texture_Sampling_Mode sample_mode, Rendering_Core* core);
