/**
 * glyph_atlas.c – GPU R8_UNORM glyph texture atlas with shelf packing.
 *
 * The atlas is a 2048×2048 VK_FORMAT_R8_UNORM device-local VkImage.
 * Glyphs are packed in horizontal shelves (rows).  When a new glyph is
 * requested that isn't already in the atlas, FreeType rasterizes it into a
 * CPU staging buffer (dirty_pixels), and the next cgui_atlas_flush() call
 * copies only the dirty rows to the GPU via vkCmdCopyBufferToImage.
 */

#include "glyph_atlas.h"
#include "text.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* Atlas texture dimensions */
#define ATLAS_W  CGUI_ATLAS_DIM
#define ATLAS_H  CGUI_ATLAS_DIM
#define MAX_GLYPHS CGUI_ATLAS_MAX_GLYPHS

/* Padding between glyphs in the atlas (prevents bilinear bleed) */
#define GLYPH_PAD 2u

typedef struct ShelfRow {
    uint16_t x;          /* current cursor X in this shelf */
    uint16_t y;          /* Y origin of this shelf */
    uint16_t height;     /* shelf height */
} ShelfRow;

#define MAX_SHELVES 256u

struct CguiGlyphAtlas {
    VkContext        *ctx;
    CguiTextEngine   *engine;

    /* GPU atlas texture (R8_UNORM) */
    VkImage           image;
    VkDeviceMemory    memory;
    VkImageView       view;

    /* CPU mirror – written by FreeType, uploaded to GPU on flush */
    uint8_t          *pixels;         /* ATLAS_W * ATLAS_H bytes */
    bool              dirty;

    /* Dirty rect tracking (full rows) for partial uploads */
    uint16_t          dirty_y0;
    uint16_t          dirty_y1;

    /* Shelf packer state */
    ShelfRow          shelves[MAX_SHELVES];
    uint32_t          shelf_count;
    uint16_t          next_shelf_y;   /* Y of the next free shelf */

    /* Glyph cache */
    CguiAtlasGlyph    glyphs[MAX_GLYPHS];
    uint32_t          glyph_count;
};

/* ------------------------------------------------------------------ */
/* Vulkan helpers                                                       */
/* ------------------------------------------------------------------ */

static uint32_t find_memory(VkPhysicalDevice phys, uint32_t bits,
                            VkMemoryPropertyFlags flags)
{
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(phys, &props);
    for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
        if ((bits & (1u << i)) &&
            (props.memoryTypes[i].propertyFlags & flags) == flags) {
            return i;
        }
    }
    return 0;
}

static bool create_r8_image(VkContext *ctx,
                            VkImage *img, VkDeviceMemory *mem, VkImageView *view)
{
    VkDevice dev = ctx->device;

    VkImageCreateInfo ici = {
        .sType       = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType   = VK_IMAGE_TYPE_2D,
        .format      = VK_FORMAT_R8_UNORM,
        .extent      = { ATLAS_W, ATLAS_H, 1 },
        .mipLevels   = 1,
        .arrayLayers = 1,
        .samples     = VK_SAMPLE_COUNT_1_BIT,
        .tiling      = VK_IMAGE_TILING_OPTIMAL,
        .usage       = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                       VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (vkCreateImage(dev, &ici, NULL, img) != VK_SUCCESS) return false;

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(dev, *img, &req);
    VkMemoryAllocateInfo mai = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = req.size,
        .memoryTypeIndex = find_memory(ctx->physical_device, req.memoryTypeBits,
                                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    };
    if (vkAllocateMemory(dev, &mai, NULL, mem) != VK_SUCCESS) {
        vkDestroyImage(dev, *img, NULL);
        *img = VK_NULL_HANDLE;
        return false;
    }
    vkBindImageMemory(dev, *img, *mem, 0);

    VkImageViewCreateInfo vci = {
        .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image    = *img,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format   = VK_FORMAT_R8_UNORM,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1,
            .layerCount = 1,
        },
    };
    if (vkCreateImageView(dev, &vci, NULL, view) != VK_SUCCESS) {
        vkFreeMemory(dev, *mem, NULL);
        vkDestroyImage(dev, *img, NULL);
        *img = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

/* Transition atlas image layout via a one-shot command buffer */
static bool transition_atlas_layout(VkContext *ctx, VkImage img,
                                    VkImageLayout from, VkImageLayout to,
                                    VkAccessFlags src_acc, VkAccessFlags dst_acc,
                                    VkPipelineStageFlags src_stage,
                                    VkPipelineStageFlags dst_stage)
{
    VkCommandBuffer cmd = vk_context_begin_one_shot(ctx);
    if (cmd == VK_NULL_HANDLE) return false;

    VkImageMemoryBarrier barrier = {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask       = src_acc,
        .dstAccessMask       = dst_acc,
        .oldLayout           = from,
        .newLayout           = to,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = img,
        .subresourceRange    = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1,
            .layerCount = 1,
        },
    };
    vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0,
                         0, NULL, 0, NULL, 1, &barrier);
    return vk_context_end_one_shot(ctx, cmd);
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

CguiGlyphAtlas *cgui_atlas_create(VkContext *ctx, CguiTextEngine *engine)
{
    if (!ctx || !engine) return NULL;

    CguiGlyphAtlas *atlas = calloc(1, sizeof(*atlas));
    if (!atlas) return NULL;

    atlas->ctx    = ctx;
    atlas->engine = engine;

    atlas->pixels = calloc(ATLAS_W * ATLAS_H, 1);
    if (!atlas->pixels) { free(atlas); return NULL; }

    if (!create_r8_image(ctx, &atlas->image, &atlas->memory, &atlas->view)) {
        free(atlas->pixels);
        free(atlas);
        return NULL;
    }

    /* Transition atlas to SHADER_READ_ONLY_OPTIMAL at creation */
    transition_atlas_layout(ctx, atlas->image,
                            VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            0, VK_ACCESS_SHADER_READ_BIT,
                            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

    atlas->dirty_y0 = ATLAS_H;
    atlas->dirty_y1 = 0;
    return atlas;
}

void cgui_atlas_destroy(CguiGlyphAtlas *atlas)
{
    if (!atlas) return;
    VkDevice dev = atlas->ctx->device;
    if (atlas->view)   vkDestroyImageView(dev, atlas->view, NULL);
    if (atlas->memory) vkFreeMemory(dev, atlas->memory, NULL);
    if (atlas->image)  vkDestroyImage(dev, atlas->image, NULL);
    free(atlas->pixels);
    free(atlas);
}

VkImageView cgui_atlas_image_view(const CguiGlyphAtlas *atlas)
{
    return atlas ? atlas->view : VK_NULL_HANDLE;
}

VkImage cgui_atlas_image(const CguiGlyphAtlas *atlas)
{
    return atlas ? atlas->image : VK_NULL_HANDLE;
}

/* ------------------------------------------------------------------ */
/* Glyph lookup / insertion                                            */
/* ------------------------------------------------------------------ */

static CguiAtlasGlyph *find_glyph(CguiGlyphAtlas *atlas,
                                   uint32_t glyph_id, uint32_t pixel_size,
                                   uint32_t style_flags)
{
    for (uint32_t i = 0; i < atlas->glyph_count; ++i) {
        CguiAtlasGlyph *g = &atlas->glyphs[i];
        if (g->glyph_id    == glyph_id &&
            g->pixel_size  == pixel_size &&
            g->style_flags == style_flags) {
            return g;
        }
    }
    return NULL;
}

static CguiAtlasGlyph *insert_glyph(CguiGlyphAtlas *atlas,
                                     uint32_t glyph_id, uint32_t pixel_size,
                                     uint32_t style_flags)
{
    if (atlas->glyph_count >= MAX_GLYPHS) {
        fprintf(stderr, "cgui atlas: glyph table full (%u glyphs)\n", MAX_GLYPHS);
        return NULL;
    }

    /* Rasterize the glyph */
    CguiGlyphBitmap bm = {0};
    bool has_bitmap = cgui_text_render_glyph(atlas->engine,
                                             glyph_id, pixel_size, style_flags,
                                             &bm);

    uint32_t w = has_bitmap ? bm.width  + GLYPH_PAD : 0u;
    uint32_t h = has_bitmap ? bm.height + GLYPH_PAD : 0u;

    uint16_t ax = 0, ay = 0;

    if (has_bitmap && w > 0 && h > 0) {
        /* Find or create a shelf with enough width */
        ShelfRow *shelf = NULL;
        for (uint32_t i = 0; i < atlas->shelf_count; ++i) {
            ShelfRow *s = &atlas->shelves[i];
            if (s->height >= (uint16_t)h &&
                s->x + (uint16_t)w <= ATLAS_W) {
                shelf = s;
                break;
            }
        }

        if (!shelf) {
            /* Open a new shelf */
            if (atlas->shelf_count >= MAX_SHELVES ||
                atlas->next_shelf_y + (uint16_t)h > ATLAS_H) {
                fprintf(stderr, "cgui atlas: atlas texture is full\n");
                cgui_glyph_bitmap_destroy(&bm);
                return NULL;
            }
            shelf = &atlas->shelves[atlas->shelf_count++];
            shelf->x      = 0;
            shelf->y      = atlas->next_shelf_y;
            shelf->height = (uint16_t)h;
            atlas->next_shelf_y = (uint16_t)(atlas->next_shelf_y + h);
        }

        ax = shelf->x;
        ay = shelf->y;
        shelf->x = (uint16_t)(shelf->x + w);

        /* Blit into CPU mirror */
        for (uint32_t gy = 0; gy < bm.height; ++gy) {
            const uint8_t *src = bm.alpha + (size_t)gy * bm.width;
            uint8_t       *dst = atlas->pixels + ((size_t)(ay + gy) * ATLAS_W + ax);
            memcpy(dst, src, bm.width);
        }

        /* Expand dirty rect */
        if (!atlas->dirty || ay < atlas->dirty_y0) atlas->dirty_y0 = ay;
        uint16_t end_y = (uint16_t)(ay + bm.height);
        if (!atlas->dirty || end_y > atlas->dirty_y1) atlas->dirty_y1 = end_y;
        atlas->dirty = true;
    }

    cgui_glyph_bitmap_destroy(&bm);

    CguiAtlasGlyph *g = &atlas->glyphs[atlas->glyph_count++];
    g->glyph_id    = glyph_id;
    g->pixel_size  = pixel_size;
    g->style_flags = style_flags;
    g->atlas_x     = ax;
    g->atlas_y     = ay;
    g->atlas_w     = has_bitmap ? (uint16_t)bm.width  : 0;
    g->atlas_h     = has_bitmap ? (uint16_t)bm.height : 0;
    g->bearing_x   = (int16_t)bm.bearing_x;
    g->bearing_y   = (int16_t)bm.bearing_y;
    return g;
}

/* ------------------------------------------------------------------ */
/* cgui_atlas_flush – upload dirty CPU rows to GPU                    */
/* ------------------------------------------------------------------ */

bool cgui_atlas_flush(CguiGlyphAtlas *atlas)
{
    if (!atlas || !atlas->dirty) return true;

    VkContext *ctx = atlas->ctx;
    VkDevice   dev = ctx->device;

    uint32_t y0 = atlas->dirty_y0;
    uint32_t y1 = atlas->dirty_y1;
    if (y0 >= y1 || y1 > ATLAS_H) {
        atlas->dirty = false;
        return true;
    }

    uint32_t row_count  = y1 - y0;
    size_t   byte_count = (size_t)row_count * ATLAS_W;

    /* Create a staging buffer */
    VkBufferCreateInfo bci = {
        .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size        = byte_count,
        .usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VkBuffer staging = VK_NULL_HANDLE;
    if (vkCreateBuffer(dev, &bci, NULL, &staging) != VK_SUCCESS) return false;

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(dev, staging, &req);
    uint32_t mt = find_memory(ctx->physical_device, req.memoryTypeBits,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkMemoryAllocateInfo mai = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = req.size,
        .memoryTypeIndex = mt,
    };
    VkDeviceMemory staging_mem = VK_NULL_HANDLE;
    if (vkAllocateMemory(dev, &mai, NULL, &staging_mem) != VK_SUCCESS) {
        vkDestroyBuffer(dev, staging, NULL);
        return false;
    }
    vkBindBufferMemory(dev, staging, staging_mem, 0);

    void *mapped = NULL;
    vkMapMemory(dev, staging_mem, 0, byte_count, 0, &mapped);
    memcpy(mapped, atlas->pixels + (size_t)y0 * ATLAS_W, byte_count);
    vkUnmapMemory(dev, staging_mem);

    /* Upload via one-shot command buffer */
    VkCommandBuffer cmd = vk_context_begin_one_shot(ctx);
    if (cmd == VK_NULL_HANDLE) {
        vkFreeMemory(dev, staging_mem, NULL);
        vkDestroyBuffer(dev, staging, NULL);
        return false;
    }

    /* Transition SHADER_READ → TRANSFER_DST */
    VkImageMemoryBarrier b0 = {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask       = VK_ACCESS_SHADER_READ_BIT,
        .dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = atlas->image,
        .subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, NULL, 0, NULL, 1, &b0);

    VkBufferImageCopy region = {
        .bufferOffset      = 0,
        .bufferRowLength   = ATLAS_W,
        .bufferImageHeight = row_count,
        .imageSubresource  = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .imageOffset       = { 0, (int32_t)y0, 0 },
        .imageExtent       = { ATLAS_W, row_count, 1 },
    };
    vkCmdCopyBufferToImage(cmd, staging, atlas->image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    /* Transition back to SHADER_READ */
    VkImageMemoryBarrier b1 = b0;
    b1.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b1.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    b1.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b1.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, NULL, 0, NULL, 1, &b1);

    bool ok = vk_context_end_one_shot(ctx, cmd);

    vkFreeMemory(dev, staging_mem, NULL);
    vkDestroyBuffer(dev, staging, NULL);

    if (ok) {
        atlas->dirty   = false;
        atlas->dirty_y0 = ATLAS_H;
        atlas->dirty_y1 = 0;
    }
    return ok;
}

/* ------------------------------------------------------------------ */
/* cgui_atlas_shape_quads – shape string and return screen quads      */
/* ------------------------------------------------------------------ */

bool cgui_atlas_shape_quads(CguiGlyphAtlas *atlas,
                            const char *utf8, float x, float y,
                            uint32_t pixel_size, uint32_t style_flags,
                            CguiGlyphQuad *quads_out, uint32_t cap,
                            uint32_t *count_out,
                            float *total_w_out, float *total_h_out)
{
    if (!atlas || !utf8) return false;

    /* Shape the string */
    uint32_t n = 0;
    float asc = 0.0f, lh = 0.0f, total_w = 0.0f;

    /* First pass: count glyphs */
    cgui_text_shape(atlas->engine, utf8, pixel_size, style_flags,
                    NULL, 0, &n, NULL, NULL, NULL);
    if (n == 0) {
        if (count_out)   *count_out   = 0;
        if (total_w_out) *total_w_out = 0.0f;
        if (total_h_out) *total_h_out = (float)pixel_size;
        return true;
    }

    CguiShapedGlyph *shaped = malloc(n * sizeof(*shaped));
    if (!shaped) return false;

    cgui_text_shape(atlas->engine, utf8, pixel_size, style_flags,
                    shaped, n, NULL, &total_w, &asc, &lh);

    uint32_t out_n = 0;
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t gid = shaped[i].glyph_id;

        /* Look up or rasterize the glyph */
        CguiAtlasGlyph *ag = find_glyph(atlas, gid, pixel_size, style_flags);
        if (!ag) {
            ag = insert_glyph(atlas, gid, pixel_size, style_flags);
        }
        if (!ag || ag->atlas_w == 0 || ag->atlas_h == 0) {
            /* Whitespace or invisible glyph – skip but count */
            continue;
        }

        float qx = x + shaped[i].pen_x + (float)ag->bearing_x;
        float qy = y + asc - (float)ag->bearing_y;

        if (quads_out && out_n < cap) {
            CguiGlyphQuad *q = &quads_out[out_n];
            q->x  = qx;
            q->y  = qy;
            q->w  = (float)ag->atlas_w;
            q->h  = (float)ag->atlas_h;
            q->u0 = (float)ag->atlas_x / (float)ATLAS_W;
            q->v0 = (float)ag->atlas_y / (float)ATLAS_H;
            q->u1 = (float)(ag->atlas_x + ag->atlas_w) / (float)ATLAS_W;
            q->v1 = (float)(ag->atlas_y + ag->atlas_h) / (float)ATLAS_H;
        }
        ++out_n;
    }

    free(shaped);

    if (count_out)   *count_out   = out_n;
    if (total_w_out) *total_w_out = total_w;
    if (total_h_out) *total_h_out = lh;
    return true;
}
