#include "renderer.hpp"

void renderer_set_viewport(Renderer* renderer, glm::ivec2 new_viewport)
{
    renderer->viewport = new_viewport;
}

void renderer_set_clear_color(Renderer* renderer, glm::vec4 new_clear_color)
{
    renderer->clear_color = new_clear_color;
}

void renderer_create(Renderer* renderer)
{
    glCreateFramebuffers(1, &renderer->framebuffer);

    glCreateTextures(GL_TEXTURE_2D, 1, &renderer->screen_texture);
    glCreateTextures(GL_TEXTURE_2D, 1, &renderer->depth_texture);

    glTextureStorage2D(renderer->screen_texture, 1, GL_RGBA8, renderer->viewport.x, renderer->viewport.y);
    glTextureStorage2D(renderer->depth_texture, 1, GL_DEPTH_COMPONENT32F, renderer->viewport.x, renderer->viewport.y);

    glTextureParameteri(renderer->depth_texture, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(renderer->depth_texture, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameteri(renderer->depth_texture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(renderer->depth_texture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTextureParameteri(renderer->screen_texture, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(renderer->screen_texture, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameteri(renderer->screen_texture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(renderer->screen_texture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glNamedFramebufferTexture(renderer->framebuffer, GL_COLOR_ATTACHMENT0, renderer->screen_texture, 0);
    glNamedFramebufferTexture(renderer->framebuffer, GL_DEPTH_ATTACHMENT, renderer->depth_texture, 0);

    if (glCheckNamedFramebufferStatus(renderer->framebuffer, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        printf("error: framebuffer not complete!\n");
    }

}
