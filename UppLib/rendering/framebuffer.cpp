#include "framebuffer.hpp"

#include "rendering_core.hpp"
#include "texture.hpp"

void framebuffer_window_resize_callback(void* userdata);

Framebuffer* framebuffer_create(Texture_Type type, Depth_Type depth_type, bool fullscreen, int width, int height)
{
    Framebuffer* result = new Framebuffer();
    result->width = width;
    result->height = height;
    result->attachments = dynamic_array_create_empty<Texture*>(1);
    result->resize_with_window = fullscreen;
    if (fullscreen) {
        rendering_core_add_render_event_listener(Render_Event::WINDOW_SIZE_CHANGED, &framebuffer_window_resize_callback, result);
    }

    glGenFramebuffers(1, &result->framebuffer_id);
    opengl_state_bind_framebuffer(result->framebuffer_id);

    // Create depth stencil attachment if requested
    if (depth_type != Depth_Type::NO_DEPTH)
    {
        bool is_renderbuffer = false;
        Texture_Type texture_type = Texture_Type::DEPTH;
        switch (depth_type) {
        case Depth_Type::DEPTH_32_NO_STENCIL:              texture_type = Texture_Type::DEPTH; is_renderbuffer = false; break;
        case Depth_Type::DEPTH_24_STENCIL_8:               texture_type = Texture_Type::DEPTH_STENCIL; is_renderbuffer = false; break;
        case Depth_Type::RENDERBUFFER_DEPTH_32_NO_STENCIL: texture_type = Texture_Type::DEPTH; is_renderbuffer = true; break;
        case Depth_Type::RENDERBUFFER_DEPTH_24_STENCIL_8:  texture_type = Texture_Type::DEPTH_STENCIL; is_renderbuffer = true; break;
        default: panic("");
        }
        GLenum attachment_type = texture_type == Texture_Type::DEPTH ? GL_DEPTH_ATTACHMENT : GL_DEPTH_STENCIL_ATTACHMENT;

        Texture* depth;
        if (is_renderbuffer) {
            depth = texture_create_renderbuffer(texture_type, width, height);
            glBindRenderbuffer(GL_RENDERBUFFER, depth->texture_id);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, attachment_type, GL_RENDERBUFFER, depth->texture_id);
        }
        else {
            depth = texture_create_empty(texture_type, width, height);
            texture_bind(depth);
            glFramebufferTexture2D(GL_FRAMEBUFFER, attachment_type, GL_TEXTURE_2D, depth->texture_id, 0);
        }
        dynamic_array_push_back(&result->attachments, depth);
    }

    // Create color attachment
    {
        Texture* color = texture_create_empty(type, width, height);
        texture_bind(color);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color->texture_id, 0);
        dynamic_array_push_back(&result->attachments, color);
        result->color_texture = color;
    }

    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "Framebuffer must be complete by now!");

    return result;
}

void framebuffer_resize(Framebuffer* framebuffer, int width, int height) {
    framebuffer->width = width;
    framebuffer->height = height;
    for (int i = 0; i < framebuffer->attachments.size; i++) {
        auto attachment = framebuffer->attachments[i];
        texture_resize(attachment, framebuffer->width, framebuffer->height, false);
    }
}

void framebuffer_window_resize_callback(void* userdata)
{
    auto& info = rendering_core.render_information;
    framebuffer_resize((Framebuffer*)userdata, info.backbuffer_width, info.backbuffer_height);
}

void framebuffer_destroy(Framebuffer* framebuffer)
{
    for (int i = 0; i < framebuffer->attachments.size; i++) {
        texture_destroy(framebuffer->attachments[i]);
    }
    dynamic_array_destroy(&framebuffer->attachments);
    if (framebuffer->resize_with_window) {
        rendering_core_remove_render_event_listener(Render_Event::WINDOW_SIZE_CHANGED, &framebuffer_window_resize_callback, framebuffer);
    }
    glDeleteFramebuffers(1, &framebuffer->framebuffer_id);
    delete framebuffer;
}

