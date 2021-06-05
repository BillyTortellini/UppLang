#include "framebuffer.hpp"

#include "rendering_core.hpp"
#include "texture_2D.hpp"

Framebuffer* framebuffer_create_width_height(Rendering_Core* core, Framebuffer_Depth_Stencil_State depth_stencil_state, int width, int height)
{
    Framebuffer* result = new Framebuffer();
    result->is_complete = false;
    result->width = width;
    result->height = height;
    result->color_attachments = dynamic_array_create_empty<Framebuffer_Attachment>(2);
    result->resize_with_window = false;

    glGenFramebuffers(1, &result->framebuffer_id);
    opengl_state_bind_framebuffer(&core->opengl_state, result->framebuffer_id);

    switch (depth_stencil_state)
    {
    case Framebuffer_Depth_Stencil_State::NO_DEPTH: 
        result->depth_stencil_attachment.attachment_index = 0;
        result->depth_stencil_attachment.texture = 0;
        result->depth_stencil_attachment.destroy_texture = false;
        break;
    case Framebuffer_Depth_Stencil_State::DEPTH_32_NO_STENCIL: {
        result->depth_stencil_attachment.attachment_index = 0;
        result->depth_stencil_attachment.texture = texture_2D_create_empty(core, Texture_2D_Type::DEPTH, width, height, texture_sampling_mode_make_nearest());
        result->depth_stencil_attachment.destroy_texture = true;
        texture_2D_bind_to_next_free_unit(result->depth_stencil_attachment.texture, core);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, result->depth_stencil_attachment.texture->texture_id, 0);
        result->is_complete = true;
        break;
    }
    case Framebuffer_Depth_Stencil_State::DEPTH_24_STENCIL_8:
        result->depth_stencil_attachment.attachment_index = 0;
        result->depth_stencil_attachment.texture = texture_2D_create_empty(core, Texture_2D_Type::DEPTH_STENCIL, width, height, texture_sampling_mode_make_nearest());
        result->depth_stencil_attachment.destroy_texture = true;
        texture_2D_bind_to_next_free_unit(result->depth_stencil_attachment.texture, core);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, result->depth_stencil_attachment.texture->texture_id, 0);
        result->is_complete = true;
        break;
    case Framebuffer_Depth_Stencil_State::RENDERBUFFER_DEPTH_32_NO_STENCIL:
        result->depth_stencil_attachment.attachment_index = 0;
        result->depth_stencil_attachment.texture = texture_2D_create_renderbuffer(core, Texture_2D_Type::DEPTH, width, height);
        result->depth_stencil_attachment.destroy_texture = true;
        glBindRenderbuffer(GL_RENDERBUFFER, result->depth_stencil_attachment.texture->texture_id);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, result->depth_stencil_attachment.texture->texture_id);
        result->is_complete = true;
        break;
    case Framebuffer_Depth_Stencil_State::RENDERBUFFER_DEPTH_24_STENCIL_8:
        result->depth_stencil_attachment.attachment_index = 0;
        result->depth_stencil_attachment.texture = texture_2D_create_renderbuffer(core, Texture_2D_Type::DEPTH_STENCIL, width, height);
        result->depth_stencil_attachment.destroy_texture = true;
        glBindRenderbuffer(GL_RENDERBUFFER, result->depth_stencil_attachment.texture->texture_id);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, result->depth_stencil_attachment.texture->texture_id);
        result->is_complete = true;
        break;
    }

    if (result->is_complete) {
        result->is_complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    }

    return result;
}

void framebuffer_window_resize_callback(void* userdata, Rendering_Core* core)
{
    Framebuffer* framebuffer = (Framebuffer*)userdata;
    framebuffer->width = core->render_information.window_width;
    framebuffer->height = core->render_information.window_height;
    if (framebuffer->depth_stencil_attachment.texture != 0) {
        texture_2D_resize(framebuffer->depth_stencil_attachment.texture, core, framebuffer->width, framebuffer->height, false);
    }
    for (int i = 0; i < framebuffer->color_attachments.size; i++) {
        Framebuffer_Attachment* attachment = &framebuffer->color_attachments[i];
        texture_2D_resize(attachment->texture, core, framebuffer->width, framebuffer->height, false);
    }
}

Framebuffer* framebuffer_create_fullscreen(Rendering_Core* core, Framebuffer_Depth_Stencil_State depth_stencil_state)
{
    Framebuffer* result = framebuffer_create_width_height(core, depth_stencil_state, core->render_information.window_width, core->render_information.window_height);
    result->resize_with_window = true;
    rendering_core_add_window_size_listener(core, &framebuffer_window_resize_callback, result);
    return result;
}

void framebuffer_set_depth_attachment(Framebuffer* framebuffer, Rendering_Core* core, Texture_2D* texture, bool destroy_texture_with_framebuffer)
{
    if (framebuffer->depth_stencil_attachment.destroy_texture) {
        texture_2D_destroy(framebuffer->depth_stencil_attachment.texture);
    }

    framebuffer->depth_stencil_attachment.attachment_index = 0;
    framebuffer->depth_stencil_attachment.texture = texture;
    framebuffer->depth_stencil_attachment.destroy_texture = destroy_texture_with_framebuffer;

    opengl_state_bind_framebuffer(&core->opengl_state, framebuffer->framebuffer_id);
    switch (texture->type)
    {
    case Texture_2D_Type::DEPTH:
        if (texture->is_renderbuffer) {
            glBindRenderbuffer(GL_RENDERBUFFER, texture->texture_id);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, texture->texture_id); 
        }
        else {
            texture_2D_bind_to_next_free_unit(texture, core);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, texture->texture_id, 0);
        }
        break;
    case Texture_2D_Type::DEPTH_STENCIL:
        if (texture->is_renderbuffer) {
            glBindRenderbuffer(GL_RENDERBUFFER, texture->texture_id);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, texture->texture_id); 
        }
        else {
            texture_2D_bind_to_next_free_unit(texture, core);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, texture->texture_id, 0);
        }
        break;
    default: panic("Depth attachment texture type must be either depth or depth_stencil!");
    }

    framebuffer->is_complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
}

void framebuffer_add_color_attachment(Framebuffer* framebuffer, Rendering_Core* core, int attachment_index, Texture_2D* texture, bool destroy_texture_afterwards)
{
    if (texture->type == Texture_2D_Type::DEPTH || texture->type == Texture_2D_Type::DEPTH_STENCIL) {
        panic("Should not happen, color attachment texture cannot be depth component!");
    }

    if (texture->width != framebuffer->width && texture->height != framebuffer->height) {
        panic("Should not happen, Color buffer needs the same size as framebuffer");
    }

    Framebuffer_Attachment* attachment = nullptr;
    for (int i = 0; i < framebuffer->color_attachments.size; i++) {
        if (framebuffer->color_attachments[i].attachment_index == attachment_index) {
            attachment = &framebuffer->color_attachments[i];
            if (attachment->destroy_texture) {
                texture_2D_destroy(attachment->texture);
            }
            break;
        }
    }
    if (attachment == nullptr) {
        Framebuffer_Attachment empty_attachment = {};
        dynamic_array_push_back(&framebuffer->color_attachments, empty_attachment);
        attachment = &framebuffer->color_attachments[framebuffer->color_attachments.size - 1];
    }
    attachment->attachment_index = attachment_index;
    attachment->destroy_texture = destroy_texture_afterwards;
    attachment->texture = texture;

    opengl_state_bind_framebuffer(&core->opengl_state, framebuffer->framebuffer_id);
    if (texture->is_renderbuffer) {
        glBindRenderbuffer(GL_RENDERBUFFER, texture->texture_id);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + attachment_index, GL_RENDERBUFFER, texture->texture_id);
    }
    else {
        texture_2D_bind_to_next_free_unit(texture, core);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + attachment_index, GL_TEXTURE_2D, texture->texture_id, 0);
    }
}

void framebuffer_destroy(Framebuffer* framebuffer, Rendering_Core* core)
{
    for (int i = 0; i < framebuffer->color_attachments.size; i++) {
        Framebuffer_Attachment* attachment = &framebuffer->color_attachments[i];
        if (attachment->destroy_texture) {
            texture_2D_destroy(attachment->texture);
        }
    }
    if (framebuffer->depth_stencil_attachment.destroy_texture) {
        texture_2D_destroy(framebuffer->depth_stencil_attachment.texture);
    }
    dynamic_array_destroy(&framebuffer->color_attachments);
    if (framebuffer->resize_with_window) {
        rendering_core_remove_window_size_listener(core, framebuffer);
    }
    glDeleteFramebuffers(1, &framebuffer->framebuffer_id);
    delete framebuffer;
}

