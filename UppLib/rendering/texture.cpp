#include "texture.hpp"

#include "shader_program.hpp"
#include "texture_bitmap.hpp"
#include "rendering_core.hpp"
#include "../utility/utils.hpp"

Texture* texture_create_empty(Texture_Type type, int width, int height)
{
    Texture* result = new Texture();
    result->is_renderbuffer = false;
    result->width = width;
    result->height = height;
    result->type = type;

    glGenTextures(1, &result->texture_id);
    opengl_state_bind_texture(Texture_Binding_Type::TEXTURE_2D, result->texture_id);
    glTexImage2D(
        (GLenum) Texture_Binding_Type::TEXTURE_2D, 
        0,
        (GLenum) type,
        width, 
        height,
        0, // Must be 0
        GL_RED, // Dummy
        GL_UNSIGNED_BYTE,
        0
    );
    result->has_mipmap = false;

    return result;
}

Texture* texture_create_renderbuffer(Texture_Type type, int width, int height)
{
    Texture* result = new Texture();
    result->type = type;
    result->is_renderbuffer = true;
    result->has_mipmap = false;
    result->width = width;
    result->height = height;
    glGenRenderbuffers(1, &result->texture_id);
    glBindRenderbuffer(GL_RENDERBUFFER, result->texture_id);
    glRenderbufferStorage(GL_RENDERBUFFER, (GLenum)type, width, height);
    return result;
}

Texture* texture_create_from_bytes(Texture_Type type, Array<byte> data, int width, int height, bool create_mipmap)
{
    Texture* result = texture_create_empty(type, width, height);
    texture_update_texture_data(result, data, create_mipmap);
    return result;
}

Texture* texture_create_from_texture_bitmap(Texture_Bitmap* texture_data, bool create_mipmap)
{
    Texture_Type result_type;
    switch (texture_data->channel_count) 
    {
    case 1: result_type = Texture_Type::RED_U8; break;
    case 2: result_type = Texture_Type::RED_GREEN_U8; break;
    case 3: result_type = Texture_Type::RED_GREEN_BLUE_U8; break;
    case 4: result_type = Texture_Type::RED_GREEN_BLUE_ALPHA_U8; break;
    default: panic("Should not happen");
    }
    return texture_create_from_bytes(result_type, texture_data->data, texture_data->width, texture_data->height, create_mipmap);
}

void texture_destroy(Texture* texture) {
    glDeleteTextures(1, &texture->texture_id);
    delete texture;
}

void texture_update_texture_data(Texture* texture, Array<byte> data, bool create_mipmap)
{
    if (texture->is_renderbuffer) {
        panic("Cannot update a renderbuffer!");
    }
    if (texture->type == Texture_Type::DEPTH || texture->type == Texture_Type::DEPTH_STENCIL) {
        panic("Unsupported types for data upload, is definitly possible, but I have to look into that\n");
    }
    if (data.size != texture_type_pixel_byte_size(texture->type) * texture->width * texture->height) {
        panic("Data size doesn't match texture!");
    }


    GLenum cpu_data_type;
    if (texture_type_is_float(texture->type)) {
        cpu_data_type = GL_FLOAT;
    }
    else {
        cpu_data_type = GL_UNSIGNED_BYTE;
    }

    int channel_count = texture_type_channel_count(texture->type);
    if (channel_count < 4) {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
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

    opengl_state_bind_texture(Texture_Binding_Type::TEXTURE_2D, texture->texture_id);
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

void texture_resize(Texture* texture, int width, int height, bool create_mipmap)
{
    if (texture->is_renderbuffer) 
    {
        texture->width = width;
        texture->height = height;
        glBindRenderbuffer(GL_RENDERBUFFER, texture->texture_id);
        glRenderbufferStorage(GL_RENDERBUFFER, (GLenum) texture->type, width, height);
        return;
    }

    opengl_state_bind_texture(Texture_Binding_Type::TEXTURE_2D, texture->texture_id);
    glTexImage2D(
        (GLenum) Texture_Binding_Type::TEXTURE_2D, 
        0,
        (GLenum) texture->type,
        width, 
        height,
        0, // Must be 0
        GL_RED, // Dummy
        GL_UNSIGNED_BYTE,
        0
    );
    texture->width = width;
    texture->height = height;
    texture->has_mipmap = create_mipmap;
    if (create_mipmap) {
        glGenerateMipmap((GLenum)Texture_Binding_Type::TEXTURE_2D);
    }
}

void texture_bind(Texture* texture) {
    opengl_state_bind_texture(Texture_Binding_Type::TEXTURE_2D, texture->texture_id);
}

GLint texture_bind_to_next_free_unit(Texture* texture, Sampling_Mode sample_mode)
{
    if (texture->is_renderbuffer) {
        panic("Cannot bind a renderbuffer, since they are write_only");
    }
    if (!texture->has_mipmap && sample_mode.minification == Texture_Minification_Mode::TRILINEAR_INTERPOLATION) {
        panic("Tried to set to trilinear, but texture has no mipmap!\n");
    }
    return opengl_state_bind_texture_to_next_free_unit(Texture_Binding_Type::TEXTURE_2D, texture->texture_id, sample_mode);
}

