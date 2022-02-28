#include "texture_2D.hpp"

#include "shader_program.hpp"
#include "texture_bitmap.hpp"
#include "rendering_core.hpp"
#include "../utility/utils.hpp"

Texture_Sampling_Mode texture_sampling_mode_make(Texture_Minification_Mode min_mode, Texture_Magnification_Mode mag_mode,
    Texture_Wrapping_Mode u_wrapping, Texture_Wrapping_Mode v_wrapping)
{
    Texture_Sampling_Mode result;
    result.magnification_mode = mag_mode;
    result.minification_mode = min_mode;
    result.u_wrapping_mode = u_wrapping;
    result.v_wrapping_mode = v_wrapping;
    return result;
}

Texture_Sampling_Mode texture_sampling_mode_make_nearest()
{
    return texture_sampling_mode_make(
        Texture_Minification_Mode::NEAREST_PIXEL_VALUE,
        Texture_Magnification_Mode::NEAREST_PIXEL_VALUE,
        Texture_Wrapping_Mode::CLAMP_TO_EDGE,
        Texture_Wrapping_Mode::CLAMP_TO_EDGE
    );
}

Texture_Sampling_Mode texture_sampling_mode_make_bilinear()
{
    return texture_sampling_mode_make(
        Texture_Minification_Mode::BILINEAR_INTERPOLATION,
        Texture_Magnification_Mode::BILINEAR_INTERPOLATION,
        Texture_Wrapping_Mode::CLAMP_TO_EDGE,
        Texture_Wrapping_Mode::CLAMP_TO_EDGE
    );
}

Texture_Sampling_Mode texture_sampling_mode_make_trilinear()
{
    return texture_sampling_mode_make(
        Texture_Minification_Mode::TRILINEAR_INTERPOLATION,
        Texture_Magnification_Mode::BILINEAR_INTERPOLATION,
        Texture_Wrapping_Mode::CLAMP_TO_EDGE,
        Texture_Wrapping_Mode::CLAMP_TO_EDGE
    );
}

bool texture_2D_type_is_float(Texture_2D_Type type)
{
    switch (type)
    {
    case Texture_2D_Type::DEPTH:
    case Texture_2D_Type::DEPTH_STENCIL:
    case Texture_2D_Type::RED_F16:
    case Texture_2D_Type::RED_GREEN_F16:
    case Texture_2D_Type::RED_GREEN_BLUE_F16:
    case Texture_2D_Type::RED_GREEN_BLUE_ALPHA_F16:
    case Texture_2D_Type::RED_F32:
    case Texture_2D_Type::RED_GREEN_F32:
    case Texture_2D_Type::RED_GREEN_BLUE_F32:
    case Texture_2D_Type::RED_GREEN_BLUE_ALPHA_F32:
        return true;
    case Texture_2D_Type::RED_U8:
    case Texture_2D_Type::RED_GREEN_U8:
    case Texture_2D_Type::RED_GREEN_BLUE_U8:
    case Texture_2D_Type::RED_GREEN_BLUE_ALPHA_U8:
        return false;
    }
    panic("Should not happen!");
    return false;
}

bool texture_2D_type_is_int(Texture_2D_Type type) {
    return !texture_2D_type_is_float(type);
}

int texture_2D_type_channel_count(Texture_2D_Type type) 
{
    switch (type)
    {
    case Texture_2D_Type::RED_U8:
    case Texture_2D_Type::RED_F16:
    case Texture_2D_Type::RED_F32:
    case Texture_2D_Type::DEPTH:
    case Texture_2D_Type::DEPTH_STENCIL:
        return 1;
    case Texture_2D_Type::RED_GREEN_U8:
    case Texture_2D_Type::RED_GREEN_F16:
    case Texture_2D_Type::RED_GREEN_F32:
        return 2;
    case Texture_2D_Type::RED_GREEN_BLUE_U8:
    case Texture_2D_Type::RED_GREEN_BLUE_F16:
    case Texture_2D_Type::RED_GREEN_BLUE_F32:
        return 3;
    case Texture_2D_Type::RED_GREEN_BLUE_ALPHA_U8:
    case Texture_2D_Type::RED_GREEN_BLUE_ALPHA_F16:
    case Texture_2D_Type::RED_GREEN_BLUE_ALPHA_F32:
        return 4;
    }
    panic("Should not happen!");
    return 1;
}

int texture_2D_type_pixel_byte_size(Texture_2D_Type type)
{
    switch (type)
    {
    case Texture_2D_Type::DEPTH:
    case Texture_2D_Type::DEPTH_STENCIL: return 4;
    case Texture_2D_Type::RED_F16: return 2 * 1;
    case Texture_2D_Type::RED_GREEN_F16: return 2 * 2;
    case Texture_2D_Type::RED_GREEN_BLUE_F16: return 2 * 3;
    case Texture_2D_Type::RED_GREEN_BLUE_ALPHA_F16: return 2 * 4;
    case Texture_2D_Type::RED_F32: return 4 * 1;
    case Texture_2D_Type::RED_GREEN_F32: return 4 * 2;
    case Texture_2D_Type::RED_GREEN_BLUE_F32: return 4 * 3;
    case Texture_2D_Type::RED_GREEN_BLUE_ALPHA_F32: return 4 * 4;
    case Texture_2D_Type::RED_U8: return 1;
    case Texture_2D_Type::RED_GREEN_U8: return 2;
    case Texture_2D_Type::RED_GREEN_BLUE_U8: return 3;
    case Texture_2D_Type::RED_GREEN_BLUE_ALPHA_U8: return 4;
    }
    panic("Should not happen!");
    return false;
}

Texture_2D* texture_2D_create_empty(Rendering_Core* core, Texture_2D_Type type, int width, int height, Texture_Sampling_Mode sample_mode)
{
    Texture_2D* result = new Texture_2D();
    result->is_renderbuffer = false;
    result->width = width;
    result->height = height;
    result->type = type;
    result->sampling_mode = sample_mode;

    glGenTextures(1, &result->texture_id);
    opengl_state_bind_texture_to_next_free_unit(&core->opengl_state, Texture_Binding_Type::TEXTURE_2D, result->texture_id);
    glTexImage2D(
        (GLenum) Texture_Binding_Type::TEXTURE_2D, 
        0,
        (GLenum) type,
        width, 
        height,
        0, // Must be 0
        GL_RED, // Dummy
        GL_BYTE,
        0
    );
    result->has_mipmap = false;
    texture_2D_set_sampling_mode(result, sample_mode, core);

    return result;
}

Texture_2D* texture_2D_create_renderbuffer(Rendering_Core* core, Texture_2D_Type type, int width, int height)
{
    Texture_2D* result = new Texture_2D();
    result->type = type;
    result->is_renderbuffer = true;
    result->has_mipmap = false;
    result->width = width;
    result->height = height;
    result->sampling_mode = texture_sampling_mode_make_nearest();
    glGenRenderbuffers(1, &result->texture_id);
    glBindRenderbuffer(GL_RENDERBUFFER, result->texture_id);
    glRenderbufferStorage(GL_RENDERBUFFER, (GLenum)type, width, height);
    return result;
}

Texture_2D* texture_2D_create_from_bytes(Rendering_Core* core, Texture_2D_Type type, 
    Array<byte> data, int width, int height, Texture_Sampling_Mode sample_mode)
{
    Texture_2D* result = texture_2D_create_empty(core, type, width, height, sample_mode);
    texture_2D_update_texture_data(result, core, data, sample_mode.minification_mode == Texture_Minification_Mode::TRILINEAR_INTERPOLATION);
    return result;
}

Texture_2D* texture_2D_create_from_texture_bitmap(Rendering_Core* core, Texture_Bitmap* texture_data, Texture_Sampling_Mode sample_mode)
{
    Texture_2D_Type result_type;
    switch (texture_data->channel_count) 
    {
    case 1: result_type = Texture_2D_Type::RED_U8; break;
    case 2: result_type = Texture_2D_Type::RED_GREEN_U8; break;
    case 3: result_type = Texture_2D_Type::RED_GREEN_BLUE_U8; break;
    case 4: result_type = Texture_2D_Type::RED_GREEN_BLUE_ALPHA_U8; break;
    default: panic("Should not happen");
    }
    return texture_2D_create_from_bytes(core, result_type, texture_data->data, texture_data->width, texture_data->height, sample_mode);
}

void texture_2D_destroy(Texture_2D* texture) {
    glDeleteTextures(1, &texture->texture_id);
    delete texture;
}

void texture_2D_update_texture_data(Texture_2D* texture, Rendering_Core* core, Array<byte> data, bool create_mipmap)
{
    if (texture->is_renderbuffer) {
        panic("Cannot update a renderbuffer!");
    }
    if (texture->type == Texture_2D_Type::DEPTH || texture->type == Texture_2D_Type::DEPTH_STENCIL) {
        panic("Unsupported types for data upload, is definitly possible, but I have to look into that\n");
    }

    int channel_count = texture_2D_type_channel_count(texture->type);
    if (channel_count < 4) {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    }

    GLenum cpu_data_type;
    if (texture_2D_type_is_float(texture->type)) {
        cpu_data_type = GL_FLOAT;
    }
    else {
        cpu_data_type = GL_BYTE;
    }

    GLenum cpu_data_format;
    switch (channel_count)
    {
    case 1: cpu_data_format = GL_RED; break;
    case 2: cpu_data_format = GL_RG; break;
    case 3: cpu_data_format = GL_RGB; break;
    case 4: cpu_data_format = GL_RGBA; break;
    default: panic("Should not happen!"); cpu_data_format = GL_RED; break;
    }

    if (channel_count < 4) {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    }

    if (data.size < texture_2D_type_pixel_byte_size(texture->type) * texture->width * texture->height) {
        panic("Data is to small for texture to upload!");
    }

    opengl_state_bind_texture_to_next_free_unit(&core->opengl_state, Texture_Binding_Type::TEXTURE_2D, texture->texture_id);
    glTexSubImage2D(
        (GLenum) Texture_Binding_Type::TEXTURE_2D,
        0,
        0,
        0,
        texture->width,
        texture->height,
        cpu_data_format,
        cpu_data_type,
        data.data
    );

    if (create_mipmap) {
        glGenerateMipmap((GLenum) Texture_Binding_Type::TEXTURE_2D);
        texture->has_mipmap = true;
    }
}

void texture_2D_resize(Texture_2D* texture, Rendering_Core* core, int width, int height, bool create_mipmap)
{
    if (texture->is_renderbuffer) 
    {
        texture->width = width;
        texture->height = height;
        glBindRenderbuffer(GL_RENDERBUFFER, texture->texture_id);
        glRenderbufferStorage(GL_RENDERBUFFER, (GLenum) texture->type, width, height);
        return;
    }

    opengl_state_bind_texture_to_next_free_unit(&core->opengl_state, Texture_Binding_Type::TEXTURE_2D, texture->texture_id);
    glTexImage2D(
        (GLenum) Texture_Binding_Type::TEXTURE_2D, 
        0,
        (GLenum) texture->type,
        width, 
        height,
        0, // Must be 0
        GL_RED, // Dummy
        GL_BYTE,
        0
    );
    texture->width = width;
    texture->height = height;
    texture->has_mipmap = create_mipmap;
    if (create_mipmap) {
        glGenerateMipmap((GLenum)Texture_Binding_Type::TEXTURE_2D);
    }
}

GLint texture_2D_bind_to_next_free_unit(Texture_2D* texture, Rendering_Core* core) {
    if (texture->is_renderbuffer) {
        panic("Cannot bind a renderbuffer, since they are write_only");
    }
    return opengl_state_bind_texture_to_next_free_unit(&core->opengl_state, Texture_Binding_Type::TEXTURE_2D, texture->texture_id);
}

void texture_2D_set_sampling_mode(Texture_2D* texture, Texture_Sampling_Mode sample_mode, Rendering_Core* core)
{
    opengl_state_bind_texture_to_next_free_unit(&core->opengl_state, Texture_Binding_Type::TEXTURE_2D, texture->texture_id);
    texture->sampling_mode = sample_mode;

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLenum) sample_mode.minification_mode);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLenum) sample_mode.magnification_mode);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (GLenum) sample_mode.u_wrapping_mode);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (GLenum) sample_mode.v_wrapping_mode);
    if (!texture->has_mipmap && sample_mode.minification_mode == Texture_Minification_Mode::TRILINEAR_INTERPOLATION) {
        panic("Tried to set to trilinear, but texture has no mipmap!\n");
    }
}

