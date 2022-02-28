#pragma once

struct Texture_2D;
struct Rendering_Core;

#include "opengl_function_pointers.hpp"
#include "../datastructures/dynamic_array.hpp"

enum class Framebuffer_Depth_Stencil_State
{
    NO_DEPTH,
    DEPTH_32_NO_STENCIL,
    DEPTH_24_STENCIL_8,
    RENDERBUFFER_DEPTH_32_NO_STENCIL,
    RENDERBUFFER_DEPTH_24_STENCIL_8
};

struct Framebuffer_Attachment
{
    int attachment_index;
    Texture_2D* texture;
    bool destroy_texture;
};

struct Framebuffer
{
    GLuint framebuffer_id;
    Dynamic_Array<Framebuffer_Attachment> color_attachments;
    Framebuffer_Attachment depth_stencil_attachment;

    int width, height;
    bool resize_with_window;
    bool is_complete;
};

Framebuffer* framebuffer_create_fullscreen(Rendering_Core* core, Framebuffer_Depth_Stencil_State depth_stencil_state);
Framebuffer* framebuffer_create_width_height(Rendering_Core* core, Framebuffer_Depth_Stencil_State depth_stencil_state, int width, int height);
void framebuffer_destroy(Framebuffer* framebuffer, Rendering_Core* core);

void framebuffer_set_depth_attachment(Framebuffer* framebuffer, Rendering_Core* core, Texture_2D* texture, bool destroy_texture_with_framebuffer);
void framebuffer_add_color_attachment(Framebuffer* framebuffer, Rendering_Core* core, int attachment_index, Texture_2D* texture, bool destroy_texture_with_framebuffer);




