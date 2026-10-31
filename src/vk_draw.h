/**
 * vk_draw.h – 2D UI drawing with real Gaussian soft shadows
 *
 * Shadow path (Core Animation style):
 *   1. Render shape mask to offscreen texture
 *   2. Separable Gaussian blur (H then V)
 *   3. Composite blurred alpha tinted with shadow colour
 *   4. Draw solid rounded body on top
 */

#ifndef VK_DRAW_H
#define VK_DRAW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vk_context.h"
#include "style.h"

typedef struct VkDraw {
    VkContext *ctx;

    /* SDF rounded-rect pipeline (mask + body) */
    VkShaderModule   rr_vert;
    VkShaderModule   rr_frag;
    VkPipelineLayout rr_layout;
    VkPipeline       rr_pipeline;

    /* Separable Gaussian blur */
    VkShaderModule   blur_vert;
    VkShaderModule   blur_frag;
    VkPipelineLayout blur_layout;
    VkPipeline       blur_pipeline;
    VkDescriptorSetLayout blur_set_layout;
    VkDescriptorPool blur_pool;
    VkDescriptorSet  blur_set_a;   /* sample mask  */
    VkDescriptorSet  blur_set_b;   /* sample temp  */
    VkSampler        sampler;

    /* Blit (composite blurred shadow) */
    VkShaderModule   blit_vert;
    VkShaderModule   blit_frag;
    VkPipelineLayout blit_layout;
    VkPipeline       blit_pipeline;
    VkDescriptorSetLayout blit_set_layout;
    VkDescriptorSet  blit_set;

    /* Offscreen targets (swapchain-sized, R8G8B8A8) */
    VkImage        mask_image;
    VkDeviceMemory mask_memory;
    VkImageView    mask_view;
    VkFramebuffer  mask_fb;

    VkImage        temp_image;
    VkDeviceMemory temp_memory;
    VkImageView    temp_view;
    VkFramebuffer  temp_fb;

    VkRenderPass   offscreen_pass;  /* clear/load colour */
    uint32_t       offscreen_w;
    uint32_t       offscreen_h;

    /* Text rasterizer and bounded cache of uploaded alpha-mask textures. */
    void          *text_engine;
    void          *text_cache;
    size_t         text_cache_count;
} VkDraw;

typedef struct VkDrawShaders {
    const uint32_t *rr_vert;   size_t rr_vert_n;
    const uint32_t *rr_frag;   size_t rr_frag_n;
    const uint32_t *blur_vert; size_t blur_vert_n;
    const uint32_t *blur_frag; size_t blur_frag_n;
    const uint32_t *blit_vert; size_t blit_vert_n;
    const uint32_t *blit_frag; size_t blit_frag_n;
} VkDrawShaders;

bool vk_draw_create(VkDraw *out, VkContext *ctx, const VkDrawShaders *shaders);
void vk_draw_destroy(VkDraw *d);

/** Recreate offscreen images after swapchain resize. */
bool vk_draw_resize(VkDraw *d);

void vk_draw_rounded_rect(VkDraw *d,
                          float x, float y, float w, float h,
                          float radius, float soft,
                          const uint8_t rgba[4]);

void vk_draw_style_box(VkDraw *d, float x, float y, float w, float h,
                       const Style *s);

/**
 * Draw UTF-8 text from its top-left, using antialiased system-font glyphs.
 * A draw context caches up to 256 distinct text/size combinations.
 */
bool vk_draw_text(VkDraw *d, const char *utf8, float x, float y,
                  float pixel_size, const uint8_t rgba[4]);

/** Draw UTF-8 text centered within the supplied rectangle. */
bool vk_draw_text_centered(VkDraw *d, const char *utf8,
                           float x, float y, float w, float h,
                           float pixel_size, const uint8_t rgba[4]);

#endif /* VK_DRAW_H */
