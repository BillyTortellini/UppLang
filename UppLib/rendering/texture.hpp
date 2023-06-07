#pragma once

#include "opengl_state.hpp"
#include "../datastructures/array.hpp"

struct Texture_Bitmap;
typedef u32 GLuint;

struct Texture
{
    Texture_Type type;
    GLuint texture_id;
    int width;
    int height;
    bool has_mipmap;
    bool is_renderbuffer;
};

Texture* texture_create_empty(Texture_Type type, int width, int height);
Texture* texture_create_renderbuffer(Texture_Type type, int width, int height);
Texture* texture_create_from_bytes(Texture_Type type, Array<byte> data, int width, int height, bool create_mipmap);
Texture* texture_create_from_texture_bitmap(Texture_Bitmap* texture_data, bool create_mipmap);
void texture_destroy(Texture* texture);

void texture_update_texture_data(Texture* texture,  Array<byte> data, bool create_mipmap);
void texture_resize(Texture* texture,  int width, int height, bool create_mipmap);
void texture_bind(Texture* texture);
GLint texture_bind_to_next_free_unit(Texture* texture, Sampling_Mode sampling_mode);
