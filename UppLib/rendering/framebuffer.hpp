#pragma once

struct Rendering_Core;
struct Texture;

#include <initializer_list>

#include "../datastructures/dynamic_array.hpp"
#include "opengl_state.hpp"

// FRAMEBUFFER
struct Framebuffer
{
    GLuint framebuffer_id;
    Dynamic_Array<Texture*> attachments;
    Texture* color_texture;

    int width, height;
    bool resize_with_window;
};

Framebuffer* framebuffer_create(Texture_Type type, Depth_Type depth_type, bool fullscreen, int width, int height);
void framebuffer_resize(Framebuffer* framebuffer, int width, int height);
void framebuffer_destroy(Framebuffer* framebuffer);
