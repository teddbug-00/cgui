/**
 * vk_draw.h – 2D UI drawing with Gaussian soft shadows and batched text.
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
    VkRect2D clip_stack[32];
    uint32_t clip_depth;

    /* Rounded-rectangle body and offscreen shadow-mask pipelines */
    VkShaderModule   rr_vert;
    VkShaderModule   rr_frag;
    VkPipelineLayout rr_layout;
    VkPipeline       rr_pipeline;
    VkPipeline       mask_pipeline;

    VkShaderModule   blur_vert;
    VkShaderModule   blur_frag;
    VkPipelineLayout blur_layout;
    VkPipeline       blur_pipeline;
    VkDescriptorSetLayout blur_set_layout;
    VkDescriptorSet  blur_set_a;
    VkDescriptorSet  blur_set_b;
    VkSampler        sampler;

    VkShaderModule   blit_vert;
    VkShaderModule   blit_frag;
    VkShaderModule   icon_frag;
    VkPipelineLayout blit_layout;
    VkPipeline       blit_pipeline;
    VkPipeline       icon_pipeline;
    VkDescriptorSetLayout blit_set_layout;
    VkDescriptorSet  blit_set;
    VkDescriptorPool shadow_pool;

    VkImage        mask_image;
    VkDeviceMemory mask_memory;
    VkImageView    mask_view;
    VkFramebuffer  mask_fb;
    VkImage        temp_image;
    VkDeviceMemory temp_memory;
    VkImageView    temp_view;
    VkFramebuffer  temp_fb;
    VkRenderPass   offscreen_pass;
    uint32_t       offscreen_w;
    uint32_t       offscreen_h;

    /* Text layout and shaping engine */
    void          *text_engine;

    /* GPU glyph atlas (replaces per-string VkImage uploads for text) */
    void          *glyph_atlas;       /* CguiGlyphAtlas* */

    /* Atlas text pipeline */
    VkShaderModule        txt_vert;
    VkShaderModule        txt_frag;
    VkPipelineLayout      txt_layout;
    VkPipeline            txt_pipeline;
    VkDescriptorSetLayout txt_set_layout;
    VkDescriptorPool      txt_pool;
    VkDescriptorSet       txt_set;    /* bound to atlas image */
    VkSampler             txt_sampler;

    /* Per-frame host-visible text vertex chunks; frame fences protect reuse. */
    void                 *txt_vertex_frames[VK_MAX_FRAMES_IN_FLIGHT];

    /* Private state for the optional two-pass shadow batch. */
    void                 *shadow_batch;
    void                 *icons;
    void                 *text_fonts;
} VkDraw;

typedef struct VkDrawShaders {
    const uint32_t *rr_vert;   size_t rr_vert_n;
    const uint32_t *rr_frag;   size_t rr_frag_n;
    const uint32_t *blur_vert; size_t blur_vert_n;
    const uint32_t *blur_frag; size_t blur_frag_n;
    const uint32_t *blit_vert; size_t blit_vert_n;
    const uint32_t *blit_frag; size_t blit_frag_n;
    const uint32_t *icon_frag; size_t icon_frag_n;
    const uint32_t *txt_vert;  size_t txt_vert_n;
    const uint32_t *txt_frag;  size_t txt_frag_n;
} VkDrawShaders;

bool vk_draw_create(VkDraw *out, VkContext *ctx, const VkDrawShaders *shaders);
void vk_draw_destroy(VkDraw *d);

/** Recreate offscreen shadow images after swapchain resize. */
bool vk_draw_resize(VkDraw *d);

/** Intersect subsequent UI rendering with a nested viewport clip. */
bool vk_draw_clip_push(VkDraw *d, float x, float y, float w, float h);
void vk_draw_clip_pop(VkDraw *d);

typedef struct VkDrawIcon VkDrawIcon;
typedef struct VkDrawFont VkDrawFont;

/**
 * Load and cache an SVG icon rasterized to the requested maximum dimension
 * in pixels (1-512). Icons of the same path and size share a cached texture.
 * Call outside an open frame.
 */
VkDrawIcon *vk_draw_load_icon(VkDraw *d, const char *path,
                              uint32_t raster_size);

/** Draw a loaded image icon tinted by the supplied RGBA color. */
void vk_draw_icon(VkDraw *d, const VkDrawIcon *icon,
                  float x, float y, float w, float h,
                  const uint8_t tint[4]);

void vk_draw_rounded_rect(VkDraw *d,
                          float x, float y, float w, float h,
                          float radius, float soft,
                          const uint8_t rgba[4]);

void vk_draw_style_box(VkDraw *d, float x, float y, float w, float h,
                       const Style *s);

/**
 * Collect shadows during a render-only widget traversal, prepare shared
 * mask/blur passes, then traverse again to draw widget contents. Draw
 * callbacks must not mutate widget state or invoke application callbacks.
 * If preparation fails, draw once normally for per-box shadows.
 */
bool vk_draw_begin_shadow_batch(VkDraw *d);
bool vk_draw_prepare_shadow_batch(VkDraw *d);
void vk_draw_end_shadow_batch(VkDraw *d);

typedef enum CguiTextAlign {
    CGUI_ALIGN_LEFT   = 0,
    CGUI_ALIGN_CENTER = 1,
    CGUI_ALIGN_RIGHT  = 2,
} CguiTextAlign;

/**
 * Draw UTF-8 text from its top-left, using the GPU glyph atlas.
 * Each unique glyph is rasterized once into a 2048×2048 R8 atlas texture;
 * subsequent frames draw only textured quads from the atlas with no CPU
 * rasterization or Vulkan allocation overhead.
 */
bool vk_draw_text(VkDraw *d, const char *utf8, float x, float y,
                  float pixel_size, const uint8_t rgba[4]);

/** Draw UTF-8 text centered within the supplied rectangle. */
bool vk_draw_text_centered(VkDraw *d, const char *utf8,
                           float x, float y, float w, float h,
                           float pixel_size, const uint8_t rgba[4]);

bool vk_draw_text_styled(VkDraw *d, const char *utf8, float x, float y,
                         float pixel_size, uint32_t style_flags,
                         const uint8_t rgba[4]);

/** Load and cache a system font family for per-run text rendering. */
VkDrawFont *vk_draw_load_font(VkDraw *d, const char *family);

/** Draw text with a loaded font family and the regular/bold/italic style flags. */
bool vk_draw_text_font_styled(VkDraw *d, const VkDrawFont *font,
                              const char *utf8, float x, float y,
                              float pixel_size, uint32_t style_flags,
                              const uint8_t rgba[4]);

bool vk_draw_text_centered_styled(VkDraw *d, const char *utf8,
                                  float x, float y, float w, float h,
                                  float pixel_size, uint32_t style_flags,
                                  const uint8_t rgba[4]);

bool vk_draw_text_aligned_styled(VkDraw *d, const char *utf8,
                                 float x, float y, float w, float h,
                                 float pixel_size, uint32_t style_flags,
                                 const uint8_t rgba[4], CguiTextAlign align);

/** Query cached width and height of rendered text. */
bool vk_draw_measure_text(VkDraw *d, const char *utf8,
                          float pixel_size, uint32_t style_flags,
                          float *w_out, float *h_out);

#endif /* VK_DRAW_H */
