/**
 * vk_draw.c – Gaussian shadows, rounded shapes, and batched atlas text
 */

#include "vk_draw.h"
#include "text.h"
#include "glyph_atlas.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#define NANOSVG_IMPLEMENTATION
#include "third_party/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "third_party/nanosvgrast.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#define SHADOW_ATLAS_SIZE 2048U
#define ICON_SVG_MAX_SIZE 512U
#define ICON_SVG_MAX_BYTES (1024U * 1024U)

/* ------------------------------------------------------------------ */
/* Push-constant layouts (must match shaders)                          */
/* ------------------------------------------------------------------ */

typedef struct RRPush {
    float rect[4];
    float color[4];
    float params[4];   /* radius, soft, screen_w, screen_h */
} RRPush;

typedef struct BlurPush {
    float direction[2];
    float sigma;
    float _pad;
    float uv_bounds[4];
} BlurPush;

typedef struct BlitPush {
    float rect[4];
    float color[4];
    float uv_rect[4];
    float screen[2];
} BlitPush;

/* Text atlas push constants (must match text.vert) */
typedef struct TextPush {
    float color[4];    /* RGBA [0,1] */
    float screen[2];   /* (screen_w, screen_h) */
} TextPush;

typedef struct TextVertex {
    float position[2];
    float uv[2];
} TextVertex;

typedef struct TextVertexChunk {
    VkBuffer buffer;
    VkDeviceMemory memory;
    void *mapped;
    VkDeviceSize capacity;
    VkDeviceSize used;
} TextVertexChunk;

typedef struct TextVertexFrame {
    TextVertexChunk *chunks;
    uint32_t count;
    uint32_t capacity;
    uint64_t frame_number;
} TextVertexFrame;

typedef struct ShadowRecord {
    float x, y, w, h;
    float radius;
    float sigma;
    float padding;
    float offset_y;
    uint8_t color[4];
    bool has_shadow;
    VkRect2D tile;
} ShadowRecord;

typedef struct ShadowBatch {
    ShadowRecord *records;
    size_t count;
    size_t capacity;
    size_t playback_index;
    uint32_t shelf_x;
    uint32_t shelf_y;
    uint32_t shelf_height;
    uint32_t used_width;
    uint32_t used_height;
    bool collecting;
    bool ready;
    bool failed;
} ShadowBatch;

struct VkDrawIcon {
    VkDraw *owner;
    char *path;
    uint32_t width;
    uint32_t height;
    uint32_t raster_size;
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    VkDescriptorPool pool;
    VkDescriptorSet set;
    struct VkDrawIcon *next;
};

struct VkDrawFont {
    VkDraw *owner;
    char *family;
    CguiTextEngine *engine;
    CguiGlyphAtlas *atlas;
    VkDescriptorPool pool;
    VkDescriptorSet set;
    struct VkDrawFont *next;
};

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static ShadowBatch *get_shadow_batch(VkDraw *d, bool create)
{
    if (!d->shadow_batch && create) {
        d->shadow_batch = calloc(1, sizeof(ShadowBatch));
    }
    return d->shadow_batch;
}

static bool allocate_shadow_tile(const VkDraw *d, ShadowBatch *batch,
                                 ShadowRecord *record)
{
    uint32_t atlas_width = d->offscreen_w;
    uint32_t atlas_height = d->offscreen_h;
    double width = ceil((double)record->w + 2.0 * record->padding);
    double height = ceil((double)record->h + 2.0 * record->padding);
    if (!isfinite(width) || !isfinite(height) ||
        width <= 0.0 || height <= 0.0 ||
        width > atlas_width || height > atlas_height) {
        return false;
    }
    uint32_t tile_w = (uint32_t)width;
    uint32_t tile_h = (uint32_t)height;
    if (batch->shelf_x + tile_w > atlas_width) {
        batch->shelf_y += batch->shelf_height;
        batch->shelf_x = 0;
        batch->shelf_height = 0;
    }
    if (batch->shelf_y + tile_h > atlas_height) return false;

    record->tile = (VkRect2D) {
        .offset = { (int32_t)batch->shelf_x, (int32_t)batch->shelf_y },
        .extent = { tile_w, tile_h },
    };
    batch->shelf_x += tile_w;
    if (tile_h > batch->shelf_height) batch->shelf_height = tile_h;
    if (batch->shelf_x > batch->used_width) batch->used_width = batch->shelf_x;
    uint32_t tile_bottom = (uint32_t)record->tile.offset.y + tile_h;
    if (tile_bottom > batch->used_height) batch->used_height = tile_bottom;
    return true;
}

static bool append_shadow_record(VkDraw *d, const Style *style,
                                 float x, float y, float w, float h)
{
    ShadowBatch *batch = get_shadow_batch(d, true);
    if (!batch) return false;
    if (batch->count == batch->capacity) {
        size_t capacity = batch->capacity ? batch->capacity * 2 : 32;
        if (capacity < batch->capacity ||
            capacity > SIZE_MAX / sizeof(*batch->records)) {
            return false;
        }
        ShadowRecord *records = realloc(batch->records,
                                        capacity * sizeof(*records));
        if (!records) return false;
        batch->records = records;
        batch->capacity = capacity;
    }

    ShadowRecord record = {
        .x = x, .y = y, .w = w, .h = h,
        .radius = style->corner_radius,
        .sigma = fmaxf(style->shadow_blur * 0.5f, 1.0f),
        .offset_y = style->shadow_offset_y,
        .has_shadow = style->shadow_color[3] > 0 &&
                      (style->shadow_blur > 0.0f ||
                       style->shadow_offset_y != 0.0f) &&
                      w > 0.0f && h > 0.0f,
    };
    memcpy(record.color, style->shadow_color, sizeof(record.color));
    if (record.has_shadow) {
        record.padding = ceilf(record.sigma * 3.0f + 2.0f);
        if (!isfinite(record.sigma) || !isfinite(record.padding) ||
            !isfinite(record.offset_y) ||
            !allocate_shadow_tile(d, batch, &record)) {
            return false;
        }
    }
    batch->records[batch->count++] = record;
    return true;
}

static VkShaderModule create_module(VkDevice dev, const uint32_t *code, size_t words)
{
    VkShaderModuleCreateInfo ci = {
        .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = words * sizeof(uint32_t),
        .pCode    = code,
    };
    VkShaderModule m = VK_NULL_HANDLE;
    if (vkCreateShaderModule(dev, &ci, NULL, &m) != VK_SUCCESS) {
        fprintf(stderr, "vkCreateShaderModule failed\n");
    }
    return m;
}

static bool find_memory_type(VkPhysicalDevice phys, uint32_t type_bits,
                             VkMemoryPropertyFlags flags, uint32_t *index_out)
{
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(phys, &props);
    for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
        if ((type_bits & (1u << i)) && (props.memoryTypes[i].propertyFlags & flags) == flags) {
            *index_out = i;
            return true;
        }
    }
    return false;
}

static bool allocate_text_vertex_chunk(VkDraw *d, VkDeviceSize size,
                                       TextVertexChunk *chunk)
{
    VkDevice dev = d->ctx->device;
    VkBufferCreateInfo bci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    if (vkCreateBuffer(dev, &bci, NULL, &chunk->buffer) != VK_SUCCESS) {
        fprintf(stderr, "cgui text: failed to create vertex buffer\n");
        return false;
    }

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(dev, chunk->buffer, &req);
    uint32_t memory_type;
    if (!find_memory_type(d->ctx->physical_device, req.memoryTypeBits,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          &memory_type)) {
        fprintf(stderr, "cgui text: no host-coherent vertex memory type\n");
        vkDestroyBuffer(dev, chunk->buffer, NULL);
        chunk->buffer = VK_NULL_HANDLE;
        return false;
    }
    VkMemoryAllocateInfo mai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size,
        .memoryTypeIndex = memory_type,
    };
    if (vkAllocateMemory(dev, &mai, NULL, &chunk->memory) != VK_SUCCESS) {
        fprintf(stderr, "cgui text: failed to allocate vertex buffer memory\n");
        vkDestroyBuffer(dev, chunk->buffer, NULL);
        chunk->buffer = VK_NULL_HANDLE;
        return false;
    }
    if (vkBindBufferMemory(dev, chunk->buffer, chunk->memory, 0) != VK_SUCCESS) {
        fprintf(stderr, "cgui text: failed to bind vertex buffer memory\n");
        vkFreeMemory(dev, chunk->memory, NULL);
        vkDestroyBuffer(dev, chunk->buffer, NULL);
        chunk->memory = VK_NULL_HANDLE;
        chunk->buffer = VK_NULL_HANDLE;
        return false;
    }

    if (vkMapMemory(dev, chunk->memory, 0, VK_WHOLE_SIZE, 0,
                    &chunk->mapped) != VK_SUCCESS) {
        fprintf(stderr, "cgui text: failed to map vertex buffer memory\n");
        vkFreeMemory(dev, chunk->memory, NULL);
        vkDestroyBuffer(dev, chunk->buffer, NULL);
        chunk->memory = VK_NULL_HANDLE;
        chunk->buffer = VK_NULL_HANDLE;
        return false;
    }
    chunk->capacity = req.size;
    return true;
}

static bool get_text_vertex_region(VkDraw *d, uint32_t vertex_count,
                                   TextVertexChunk **chunk_out,
                                   VkDeviceSize *offset_out,
                                   TextVertex **vertices_out)
{
    size_t byte_size;
    if (__builtin_mul_overflow((size_t)vertex_count, sizeof(TextVertex),
                               &byte_size)) {
        return false;
    }
    VkDeviceSize required = (VkDeviceSize)byte_size;
    uint32_t frame_index = d->ctx->current_frame;
    TextVertexFrame *frame = d->txt_vertex_frames[frame_index];
    if (!frame) {
        frame = calloc(1, sizeof(*frame));
        if (!frame) return false;
        d->txt_vertex_frames[frame_index] = frame;
    }
    if (frame->frame_number != d->ctx->frame_number) {
        for (uint32_t i = 0; i < frame->count; ++i) {
            frame->chunks[i].used = 0;
        }
        frame->frame_number = d->ctx->frame_number;
    }

    TextVertexChunk *chunk = NULL;
    for (uint32_t i = 0; i < frame->count; ++i) {
        if (frame->chunks[i].capacity - frame->chunks[i].used >= required) {
            chunk = &frame->chunks[i];
            break;
        }
    }
    if (!chunk) {
        if (frame->count == frame->capacity) {
            uint32_t capacity = frame->capacity ? frame->capacity * 2u : 4u;
            size_t allocation_size;
            if (capacity < frame->capacity ||
                __builtin_mul_overflow((size_t)capacity,
                                       sizeof(*frame->chunks),
                                       &allocation_size)) return false;
            TextVertexChunk *chunks = realloc(
                frame->chunks, allocation_size);
            if (!chunks) return false;
            frame->chunks = chunks;
            frame->capacity = capacity;
        }

        VkDeviceSize capacity = required < 65536u ? 65536u : required;
        chunk = &frame->chunks[frame->count];
        memset(chunk, 0, sizeof(*chunk));
        if (!allocate_text_vertex_chunk(d, capacity, chunk)) return false;
        ++frame->count;
    }

    *chunk_out = chunk;
    *offset_out = chunk->used;
    *vertices_out = (TextVertex *)((uint8_t *)chunk->mapped + chunk->used);
    chunk->used += required;
    return true;
}

static void destroy_text_vertex_frames(VkDraw *d)
{
    VkDevice dev = d->ctx->device;
    for (uint32_t i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; ++i) {
        TextVertexFrame *frame = d->txt_vertex_frames[i];
        if (!frame) continue;
        for (uint32_t j = 0; j < frame->count; ++j) {
            TextVertexChunk *chunk = &frame->chunks[j];
            if (chunk->mapped) vkUnmapMemory(dev, chunk->memory);
            if (chunk->buffer) vkDestroyBuffer(dev, chunk->buffer, NULL);
            if (chunk->memory) vkFreeMemory(dev, chunk->memory, NULL);
        }
        free(frame->chunks);
        free(frame);
    }
}

static bool create_color_target(VkDraw *d, uint32_t width, uint32_t height,
                                VkImage *image, VkDeviceMemory *memory,
                                VkImageView *view, VkFramebuffer *framebuffer)
{
    VkDevice dev = d->ctx->device;
    VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = { width, height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (vkCreateImage(dev, &image_info, NULL, image) != VK_SUCCESS) return false;

    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(dev, *image, &requirements);
    uint32_t memory_type;
    if (!find_memory_type(d->ctx->physical_device, requirements.memoryTypeBits,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &memory_type)) {
        fprintf(stderr, "cgui shadow: no device-local image memory type\n");
        goto fail;
    }
    VkMemoryAllocateInfo allocation = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = memory_type,
    };
    if (vkAllocateMemory(dev, &allocation, NULL, memory) != VK_SUCCESS) goto fail;
    if (vkBindImageMemory(dev, *image, *memory, 0) != VK_SUCCESS) goto fail;

    VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = *image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1,
            .layerCount = 1,
        },
    };
    if (vkCreateImageView(dev, &view_info, NULL, view) != VK_SUCCESS) goto fail;

    VkFramebufferCreateInfo framebuffer_info = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = d->offscreen_pass,
        .attachmentCount = 1,
        .pAttachments = view,
        .width = width,
        .height = height,
        .layers = 1,
    };
    if (vkCreateFramebuffer(dev, &framebuffer_info, NULL, framebuffer) != VK_SUCCESS) {
        goto fail;
    }
    return true;

fail:
    if (*framebuffer) vkDestroyFramebuffer(dev, *framebuffer, NULL);
    if (*view) vkDestroyImageView(dev, *view, NULL);
    if (*memory) vkFreeMemory(dev, *memory, NULL);
    if (*image) vkDestroyImage(dev, *image, NULL);
    *framebuffer = VK_NULL_HANDLE;
    *view = VK_NULL_HANDLE;
    *memory = VK_NULL_HANDLE;
    *image = VK_NULL_HANDLE;
    return false;
}

static void destroy_color_target(VkDevice dev, VkImage *image,
                                 VkDeviceMemory *memory, VkImageView *view,
                                 VkFramebuffer *framebuffer)
{
    if (*framebuffer) vkDestroyFramebuffer(dev, *framebuffer, NULL);
    if (*view) vkDestroyImageView(dev, *view, NULL);
    if (*image) vkDestroyImage(dev, *image, NULL);
    if (*memory) vkFreeMemory(dev, *memory, NULL);
    *framebuffer = VK_NULL_HANDLE;
    *view = VK_NULL_HANDLE;
    *image = VK_NULL_HANDLE;
    *memory = VK_NULL_HANDLE;
}

static void destroy_draw_icon(VkDevice dev, VkDrawIcon *icon)
{
    if (icon->pool) vkDestroyDescriptorPool(dev, icon->pool, NULL);
    if (icon->view) vkDestroyImageView(dev, icon->view, NULL);
    if (icon->image) vkDestroyImage(dev, icon->image, NULL);
    if (icon->memory) vkFreeMemory(dev, icon->memory, NULL);
    free(icon->path);
    free(icon);
}

static void destroy_draw_icons(VkDevice dev, VkDraw *d)
{
    VkDrawIcon *icon = d->icons;
    while (icon) {
        VkDrawIcon *next = icon->next;
        destroy_draw_icon(dev, icon);
        icon = next;
    }
    d->icons = NULL;
}

static void destroy_draw_fonts(VkDevice dev, VkDraw *d)
{
    VkDrawFont *font = d->text_fonts;
    while (font) {
        VkDrawFont *next = font->next;
        if (font->atlas) cgui_atlas_destroy(font->atlas);
        if (font->engine) cgui_text_destroy(font->engine);
        if (font->pool) vkDestroyDescriptorPool(dev, font->pool, NULL);
        free(font->family);
        free(font);
        font = next;
    }
    d->text_fonts = NULL;
}

static bool upload_icon_pixels(VkDraw *d, VkDrawIcon *icon,
                               const uint8_t *pixels, size_t byte_count)
{
    VkDevice dev = d->ctx->device;
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory staging_memory = VK_NULL_HANDLE;
    void *mapped = NULL;
    bool ok = false;

    VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = { icon->width, icon->height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (vkCreateImage(dev, &image_info, NULL, &icon->image) != VK_SUCCESS) {
        fprintf(stderr, "cgui icon: failed to create image\n");
        goto cleanup;
    }
    VkMemoryRequirements image_req;
    vkGetImageMemoryRequirements(dev, icon->image, &image_req);
    uint32_t memory_type;
    if (!find_memory_type(d->ctx->physical_device, image_req.memoryTypeBits,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &memory_type)) {
        fprintf(stderr, "cgui icon: no device-local image memory type\n");
        goto cleanup;
    }
    VkMemoryAllocateInfo image_alloc = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = image_req.size,
        .memoryTypeIndex = memory_type,
    };
    if (vkAllocateMemory(dev, &image_alloc, NULL, &icon->memory) != VK_SUCCESS ||
        vkBindImageMemory(dev, icon->image, icon->memory, 0) != VK_SUCCESS) {
        fprintf(stderr, "cgui icon: failed to allocate image memory\n");
        goto cleanup;
    }

    VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = icon->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1,
            .layerCount = 1,
        },
    };
    if (vkCreateImageView(dev, &view_info, NULL, &icon->view) != VK_SUCCESS) {
        fprintf(stderr, "cgui icon: failed to create image view\n");
        goto cleanup;
    }

    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = byte_count,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    if (vkCreateBuffer(dev, &buffer_info, NULL, &staging) != VK_SUCCESS) {
        fprintf(stderr, "cgui icon: failed to create staging buffer\n");
        goto cleanup;
    }
    VkMemoryRequirements buffer_req;
    vkGetBufferMemoryRequirements(dev, staging, &buffer_req);
    if (!find_memory_type(d->ctx->physical_device, buffer_req.memoryTypeBits,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &memory_type)) {
        fprintf(stderr, "cgui icon: no host-coherent staging memory type\n");
        goto cleanup;
    }
    VkMemoryAllocateInfo buffer_alloc = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = buffer_req.size,
        .memoryTypeIndex = memory_type,
    };
    if (vkAllocateMemory(dev, &buffer_alloc, NULL, &staging_memory) != VK_SUCCESS ||
        vkBindBufferMemory(dev, staging, staging_memory, 0) != VK_SUCCESS ||
        vkMapMemory(dev, staging_memory, 0, byte_count, 0, &mapped) != VK_SUCCESS) {
        fprintf(stderr, "cgui icon: failed to prepare staging buffer\n");
        goto cleanup;
    }
    memcpy(mapped, pixels, byte_count);
    vkUnmapMemory(dev, staging_memory);
    mapped = NULL;

    VkCommandBuffer cmd = vk_context_begin_one_shot(d->ctx);
    if (cmd == VK_NULL_HANDLE) {
        fprintf(stderr, "cgui icon: failed to begin upload commands\n");
        goto cleanup;
    }
    VkImageMemoryBarrier to_transfer = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = icon->image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1,
            .layerCount = 1,
        },
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                         0, NULL, 0, NULL, 1, &to_transfer);

    VkBufferImageCopy copy = {
        .imageSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .layerCount = 1,
        },
        .imageExtent = { icon->width, icon->height, 1 },
    };
    vkCmdCopyBufferToImage(cmd, staging, icon->image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    VkImageMemoryBarrier to_shader = to_transfer;
    to_shader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    to_shader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    to_shader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_shader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                         0, NULL, 0, NULL, 1, &to_shader);
    if (!vk_context_end_one_shot(d->ctx, cmd)) {
        fprintf(stderr, "cgui icon: image upload failed\n");
        goto cleanup;
    }

    VkDescriptorPoolSize pool_size = {
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
    };
    VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1,
        .poolSizeCount = 1,
        .pPoolSizes = &pool_size,
    };
    if (vkCreateDescriptorPool(dev, &pool_info, NULL, &icon->pool) != VK_SUCCESS) {
        fprintf(stderr, "cgui icon: failed to create descriptor pool\n");
        goto cleanup;
    }
    VkDescriptorSetAllocateInfo set_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = icon->pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &d->blit_set_layout,
    };
    if (vkAllocateDescriptorSets(dev, &set_info, &icon->set) != VK_SUCCESS) {
        fprintf(stderr, "cgui icon: failed to allocate descriptor set\n");
        goto cleanup;
    }
    VkDescriptorImageInfo image = {
        .sampler = d->sampler,
        .imageView = icon->view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = icon->set,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &image,
    };
    vkUpdateDescriptorSets(dev, 1, &write, 0, NULL);
    ok = true;

cleanup:
    if (mapped) vkUnmapMemory(dev, staging_memory);
    if (staging) vkDestroyBuffer(dev, staging, NULL);
    if (staging_memory) vkFreeMemory(dev, staging_memory, NULL);
    return ok;
}

static bool create_offscreen_pass(VkDraw *d)
{
    VkAttachmentDescription color = {
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    VkAttachmentReference reference = {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &reference,
    };
    VkSubpassDependency dependencies[2] = {
        {
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0,
            .srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        },
        {
            .srcSubpass = 0,
            .dstSubpass = VK_SUBPASS_EXTERNAL,
            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        },
    };
    VkRenderPassCreateInfo pass_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &color,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 2,
        .pDependencies = dependencies,
    };
    return vkCreateRenderPass(d->ctx->device, &pass_info, NULL,
                              &d->offscreen_pass) == VK_SUCCESS;
}

bool vk_draw_resize(VkDraw *d)
{
    if (!d || !d->ctx) return false;
    uint32_t width = d->ctx->swapchain_extent.width;
    uint32_t height = d->ctx->swapchain_extent.height;
    if (width < SHADOW_ATLAS_SIZE) width = SHADOW_ATLAS_SIZE;
    if (height < SHADOW_ATLAS_SIZE) height = SHADOW_ATLAS_SIZE;
    if (d->mask_image && d->temp_image &&
        d->offscreen_w >= width && d->offscreen_h >= height) {
        return true;
    }

    vkDeviceWaitIdle(d->ctx->device);
    destroy_color_target(d->ctx->device, &d->mask_image, &d->mask_memory,
                         &d->mask_view, &d->mask_fb);
    destroy_color_target(d->ctx->device, &d->temp_image, &d->temp_memory,
                         &d->temp_view, &d->temp_fb);
    if (!create_color_target(d, width, height,
                             &d->mask_image, &d->mask_memory,
                             &d->mask_view, &d->mask_fb) ||
        !create_color_target(d, width, height,
                             &d->temp_image, &d->temp_memory,
                             &d->temp_view, &d->temp_fb)) {
        return false;
    }

    d->offscreen_w = width;
    d->offscreen_h = height;
    VkDescriptorImageInfo mask_info = {
        .sampler = d->sampler,
        .imageView = d->mask_view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    VkDescriptorImageInfo temp_info = {
        .sampler = d->sampler,
        .imageView = d->temp_view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    VkWriteDescriptorSet writes[3] = {
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = d->blur_set_a,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &mask_info,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = d->blur_set_b,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &temp_info,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = d->blit_set,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &mask_info,
        },
    };
    vkUpdateDescriptorSets(d->ctx->device, 3, writes, 0, NULL);
    return true;
}



static VkPipeline create_graphics_pipeline(
    VkDevice dev, VkRenderPass rp, VkPipelineLayout layout,
    VkShaderModule vert, VkShaderModule frag,
    bool blend, bool dynamic_viewport)
{
    VkPipelineShaderStageCreateInfo stages[2] = {
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vert, .pName = "main" },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = frag, .pName = "main" },
    };
    VkPipelineVertexInputStateCreateInfo vi = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };
    VkPipelineInputAssemblyStateCreateInfo ia = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    VkPipelineViewportStateCreateInfo vp = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };
    VkPipelineRasterizationStateCreateInfo rs = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    VkPipelineMultisampleStateCreateInfo ms = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    VkPipelineColorBlendAttachmentState ba = {
        .blendEnable = blend ? VK_TRUE : VK_FALSE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = 0xF,
    };
    if (!blend) {
        ba.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        ba.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    }
    VkPipelineColorBlendStateCreateInfo cb = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &ba,
    };
    VkDynamicState dyn_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
    };
    VkPipelineDynamicStateCreateInfo dyn = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = dynamic_viewport ? 2u : 0u,
        .pDynamicStates = dyn_states,
    };
    VkGraphicsPipelineCreateInfo gp = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = stages,
        .pVertexInputState = &vi,
        .pInputAssemblyState = &ia,
        .pViewportState = &vp,
        .pRasterizationState = &rs,
        .pMultisampleState = &ms,
        .pColorBlendState = &cb,
        .pDynamicState = dynamic_viewport ? &dyn : NULL,
        .layout = layout,
        .renderPass = rp,
        .subpass = 0,
    };
    VkPipeline pipe = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gp, NULL, &pipe) != VK_SUCCESS) {
        fprintf(stderr, "vkCreateGraphicsPipelines failed\n");
    }
    return pipe;
}

static bool decode_svg_icon(const char *path, uint8_t **pixels_out,
                            uint32_t raster_size, uint32_t *width_out,
                            uint32_t *height_out)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "cgui icon: failed to open %s\n", path);
        return false;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fprintf(stderr, "cgui icon: failed to seek %s\n", path);
        fclose(file);
        return false;
    }
    long file_size = ftell(file);
    if (file_size <= 0 || (unsigned long)file_size > ICON_SVG_MAX_BYTES ||
        fseek(file, 0, SEEK_SET) != 0) {
        fprintf(stderr, "cgui icon: invalid or oversized SVG: %s\n", path);
        fclose(file);
        return false;
    }

    size_t input_size = (size_t)file_size;
    char *input = malloc(input_size + 1);
    if (!input) {
        fprintf(stderr, "cgui icon: out of memory reading %s\n", path);
        fclose(file);
        return false;
    }
    if (fread(input, 1, input_size, file) != input_size || ferror(file)) {
        fprintf(stderr, "cgui icon: failed to read %s\n", path);
        free(input);
        fclose(file);
        return false;
    }
    fclose(file);
    input[input_size] = '\0';

    char *current_color;
    while ((current_color = strstr(input, "currentColor")) != NULL) {
        size_t tail_size = strlen(current_color + 12);
        memmove(current_color + 4, current_color + 12, tail_size + 1);
        memcpy(current_color, "#fff", 4);
    }

    NSVGimage *svg = nsvgParse(input, "px", 96.0f);
    free(input);
    if (!svg || !isfinite(svg->width) || !isfinite(svg->height) ||
        svg->width <= 0.0f || svg->height <= 0.0f) {
        fprintf(stderr, "cgui icon: failed to parse SVG %s\n", path);
        if (svg) nsvgDelete(svg);
        return false;
    }

    double scale_value = (double)raster_size /
                         fmax((double)svg->width, (double)svg->height);
    if (!isfinite(scale_value) || scale_value > FLT_MAX) {
        fprintf(stderr, "cgui icon: unsupported SVG dimensions in %s\n", path);
        nsvgDelete(svg);
        return false;
    }
    float scale = (float)scale_value;
    uint32_t width = (uint32_t)ceilf(svg->width * scale);
    uint32_t height = (uint32_t)ceilf(svg->height * scale);
    if (width == 0 || height == 0 ||
        width > raster_size || height > raster_size) {
        fprintf(stderr, "cgui icon: invalid dimensions in %s\n", path);
        nsvgDelete(svg);
        return false;
    }

    size_t byte_count = (size_t)width * height * 4;
    uint8_t *pixels = malloc(byte_count);
    NSVGrasterizer *rasterizer = nsvgCreateRasterizer();
    if (!pixels || !rasterizer) {
        fprintf(stderr, "cgui icon: failed to allocate SVG rasterizer for %s\n",
                path);
        free(pixels);
        if (rasterizer) nsvgDeleteRasterizer(rasterizer);
        nsvgDelete(svg);
        return false;
    }
    nsvgRasterize(rasterizer, svg, 0.0f, 0.0f, scale, pixels,
                  (int)width, (int)height, (int)(width * 4));
    nsvgDeleteRasterizer(rasterizer);
    nsvgDelete(svg);

    *pixels_out = pixels;
    *width_out = width;
    *height_out = height;
    return true;
}

VkDrawIcon *vk_draw_load_icon(VkDraw *d, const char *path,
                              uint32_t raster_size)
{
    if (!d || !d->ctx || !path || path[0] == '\0' ||
        raster_size == 0 || raster_size > ICON_SVG_MAX_SIZE ||
        d->ctx->frame_open || !d->sampler || !d->blit_set_layout) {
        fprintf(stderr, "cgui icon: load requires a path, size 1-%u, and no open frame\n",
                ICON_SVG_MAX_SIZE);
        return NULL;
    }
    for (VkDrawIcon *cached = d->icons; cached; cached = cached->next) {
        if (cached->raster_size == raster_size &&
            strcmp(cached->path, path) == 0) return cached;
    }

    uint8_t *pixels = NULL;
    uint32_t width = 0;
    uint32_t height = 0;
    if (!decode_svg_icon(path, &pixels, raster_size, &width, &height)) return NULL;
    size_t pixel_bytes = (size_t)width * height * 4;

    VkDrawIcon *icon = calloc(1, sizeof(*icon));
    if (!icon) {
        fprintf(stderr, "cgui icon: out of memory allocating icon handle\n");
        free(pixels);
        return NULL;
    }
    icon->path = strdup(path);
    icon->owner = d;
    icon->width = width;
    icon->height = height;
    icon->raster_size = raster_size;
    bool uploaded = icon->path &&
                    upload_icon_pixels(d, icon, pixels, pixel_bytes);
    free(pixels);
    if (!uploaded) {
        if (!icon->path) {
            fprintf(stderr, "cgui icon: out of memory copying icon path\n");
        }
        destroy_draw_icon(d->ctx->device, icon);
        return NULL;
    }
    icon->next = d->icons;
    d->icons = icon;
    return icon;
}

void vk_draw_icon(VkDraw *d, const VkDrawIcon *icon,
                  float x, float y, float w, float h,
                  const uint8_t tint[4])
{
    if (!d || !icon || icon->owner != d || !tint ||
        !d->ctx || !d->ctx->frame_open ||
        !isfinite(x) || !isfinite(y) || !isfinite(w) || !isfinite(h) ||
        w <= 0.0f || h <= 0.0f) {
        return;
    }
    ShadowBatch *batch = get_shadow_batch(d, false);
    if (batch && batch->collecting) return;
    VkCommandBuffer cmd = d->ctx->active_cmd;
    VkViewport viewport = {
        .width = (float)d->ctx->swapchain_extent.width,
        .height = (float)d->ctx->swapchain_extent.height,
        .maxDepth = 1.0f,
    };
    VkRect2D scissor = { .extent = d->ctx->swapchain_extent };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, d->icon_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            d->blit_layout, 0, 1, &icon->set, 0, NULL);
    BlitPush push = {
        .rect = { x, y, w, h },
        .color = {
            tint[0] / 255.0f, tint[1] / 255.0f,
            tint[2] / 255.0f, tint[3] / 255.0f,
        },
        .uv_rect = { 0.0f, 0.0f, 1.0f, 1.0f },
        .screen = {
            (float)d->ctx->swapchain_extent.width,
            (float)d->ctx->swapchain_extent.height,
        },
    };
    vkCmdPushConstants(cmd, d->blit_layout,
                       VK_SHADER_STAGE_VERTEX_BIT |
                       VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(push), &push);
    vkCmdDraw(cmd, 6, 1, 0, 0);
}

bool vk_draw_create(VkDraw *out, VkContext *ctx, const VkDrawShaders *sh)
{
    memset(out, 0, sizeof(*out));
    out->ctx = ctx;
    VkDevice dev = ctx->device;

    if (!create_offscreen_pass(out)) {
        fprintf(stderr, "cgui shadow: failed to create offscreen render pass\n");
        vk_draw_destroy(out);
        return false;
    }

    out->text_engine = cgui_text_create();
    if (!out->text_engine) {
        fprintf(stderr, "cgui text: failed to initialize the text renderer\n");
        vk_draw_destroy(out);
        return false;
    }

    out->rr_vert   = create_module(dev, sh->rr_vert, sh->rr_vert_n);
    out->rr_frag   = create_module(dev, sh->rr_frag, sh->rr_frag_n);
    out->blur_vert = create_module(dev, sh->blur_vert, sh->blur_vert_n);
    out->blur_frag = create_module(dev, sh->blur_frag, sh->blur_frag_n);
    out->blit_vert = create_module(dev, sh->blit_vert, sh->blit_vert_n);
    out->blit_frag = create_module(dev, sh->blit_frag, sh->blit_frag_n);
    out->icon_frag = create_module(dev, sh->icon_frag, sh->icon_frag_n);
    if (!out->rr_vert || !out->rr_frag || !out->blur_vert ||
        !out->blur_frag || !out->blit_vert || !out->blit_frag ||
        !out->icon_frag) {
        vk_draw_destroy(out);
        return false;
    }

    VkSamplerCreateInfo shadow_sampler_info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    };
    if (vkCreateSampler(dev, &shadow_sampler_info, NULL, &out->sampler) != VK_SUCCESS) {
        vk_draw_destroy(out);
        return false;
    }

    VkDescriptorSetLayoutBinding shadow_binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    VkDescriptorSetLayoutCreateInfo shadow_layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &shadow_binding,
    };
    if (vkCreateDescriptorSetLayout(dev, &shadow_layout_info, NULL,
                                    &out->blur_set_layout) != VK_SUCCESS ||
        vkCreateDescriptorSetLayout(dev, &shadow_layout_info, NULL,
                                    &out->blit_set_layout) != VK_SUCCESS) {
        vk_draw_destroy(out);
        return false;
    }

    VkDescriptorPoolSize shadow_pool_size = {
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 3,
    };
    VkDescriptorPoolCreateInfo shadow_pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 3,
        .poolSizeCount = 1,
        .pPoolSizes = &shadow_pool_size,
    };
    if (vkCreateDescriptorPool(dev, &shadow_pool_info, NULL,
                               &out->shadow_pool) != VK_SUCCESS) {
        vk_draw_destroy(out);
        return false;
    }

    VkDescriptorSetLayout shadow_layouts[] = {
        out->blur_set_layout, out->blur_set_layout, out->blit_set_layout,
    };
    VkDescriptorSet shadow_sets[3];
    VkDescriptorSetAllocateInfo shadow_set_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = out->shadow_pool,
        .descriptorSetCount = 3,
        .pSetLayouts = shadow_layouts,
    };
    if (vkAllocateDescriptorSets(dev, &shadow_set_info, shadow_sets) != VK_SUCCESS) {
        vk_draw_destroy(out);
        return false;
    }
    out->blur_set_a = shadow_sets[0];
    out->blur_set_b = shadow_sets[1];
    out->blit_set = shadow_sets[2];

    VkPushConstantRange rr_pcr = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .size = sizeof(RRPush),
    };
    VkPipelineLayoutCreateInfo rr_lci = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &rr_pcr,
    };
    if (vkCreatePipelineLayout(dev, &rr_lci, NULL, &out->rr_layout) != VK_SUCCESS) {
        vk_draw_destroy(out);
        return false;
    }

    out->rr_pipeline = create_graphics_pipeline(
        dev, ctx->render_pass, out->rr_layout, out->rr_vert, out->rr_frag, true, true);
    if (!out->rr_pipeline) {
        vk_draw_destroy(out);
        return false;
    }

    VkPushConstantRange blur_pcr = {
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .size = sizeof(BlurPush),
    };
    VkPipelineLayoutCreateInfo blur_layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &out->blur_set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &blur_pcr,
    };
    VkPushConstantRange blit_pcr = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .size = sizeof(BlitPush),
    };
    VkPipelineLayoutCreateInfo blit_layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &out->blit_set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &blit_pcr,
    };
    if (vkCreatePipelineLayout(dev, &blur_layout_info, NULL,
                               &out->blur_layout) != VK_SUCCESS ||
        vkCreatePipelineLayout(dev, &blit_layout_info, NULL,
                               &out->blit_layout) != VK_SUCCESS) {
        vk_draw_destroy(out);
        return false;
    }
    out->mask_pipeline = create_graphics_pipeline(
        dev, out->offscreen_pass, out->rr_layout, out->rr_vert, out->rr_frag,
        false, true);
    out->blur_pipeline = create_graphics_pipeline(
        dev, out->offscreen_pass, out->blur_layout, out->blur_vert,
        out->blur_frag, false, true);
    out->blit_pipeline = create_graphics_pipeline(
        dev, ctx->render_pass, out->blit_layout, out->blit_vert,
        out->blit_frag, true, true);
    out->icon_pipeline = create_graphics_pipeline(
        dev, ctx->render_pass, out->blit_layout, out->blit_vert,
        out->icon_frag, true, true);
    if (!out->mask_pipeline || !out->blur_pipeline || !out->blit_pipeline ||
        !out->icon_pipeline ||
        !vk_draw_resize(out)) {
        vk_draw_destroy(out);
        return false;
    }

    VkDescriptorSetLayoutBinding txt_binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    VkDescriptorSetLayoutCreateInfo txt_dslci = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &txt_binding,
    };
    if (vkCreateDescriptorSetLayout(dev, &txt_dslci, NULL,
                                    &out->txt_set_layout) != VK_SUCCESS) {
        vk_draw_destroy(out);
        return false;
    }

    /* ---- Glyph atlas pipeline ---- */
    /* Allocate a separate pool for the atlas descriptor (1 set) */
    {
        VkDescriptorPoolSize atlas_pool_size = {
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
        };
        VkDescriptorPoolCreateInfo atlas_dpci = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = 1,
            .poolSizeCount = 1,
            .pPoolSizes = &atlas_pool_size,
        };
        if (vkCreateDescriptorPool(dev, &atlas_dpci, NULL, &out->txt_pool) != VK_SUCCESS) {
            vk_draw_destroy(out); return false;
        }
        VkDescriptorSetAllocateInfo atlas_dsai = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = out->txt_pool,
            .descriptorSetCount = 1,
            .pSetLayouts = &out->txt_set_layout,
        };
        if (vkAllocateDescriptorSets(dev, &atlas_dsai, &out->txt_set) != VK_SUCCESS) {
            vk_draw_destroy(out); return false;
        }
    }

    /* Nearest sampler for atlas glyphs (already AA'd by FreeType) */
    {
        VkSamplerCreateInfo txt_sci = {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = VK_FILTER_NEAREST,
            .minFilter = VK_FILTER_NEAREST,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        };
        if (vkCreateSampler(dev, &txt_sci, NULL, &out->txt_sampler) != VK_SUCCESS) {
            vk_draw_destroy(out); return false;
        }
    }

    /* Text push-constant layout (color, screen) */
    {
        VkPushConstantRange txt_pcr = {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            .size = sizeof(TextPush),
        };
        VkPipelineLayoutCreateInfo txt_lci = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1,
            .pSetLayouts = &out->txt_set_layout,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &txt_pcr,
        };
        if (vkCreatePipelineLayout(dev, &txt_lci, NULL, &out->txt_layout) != VK_SUCCESS) {
            vk_draw_destroy(out); return false;
        }
    }

    out->txt_vert = create_module(dev, sh->txt_vert, sh->txt_vert_n);
    out->txt_frag = create_module(dev, sh->txt_frag, sh->txt_frag_n);
    if (!out->txt_vert || !out->txt_frag) {
        vk_draw_destroy(out); return false;
    }

    /* Text pipeline: premultiplied-alpha blend */
    {
        VkPipelineColorBlendAttachmentState txt_ba = {
            .blendEnable         = VK_TRUE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .colorBlendOp        = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .alphaBlendOp        = VK_BLEND_OP_ADD,
            .colorWriteMask      = 0xF,
        };
        VkPipelineColorBlendStateCreateInfo txt_cb = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .attachmentCount = 1,
            .pAttachments = &txt_ba,
        };
        VkPipelineShaderStageCreateInfo txt_stages[2] = {
            { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
              .stage = VK_SHADER_STAGE_VERTEX_BIT,   .module = out->txt_vert, .pName = "main" },
            { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
              .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = out->txt_frag, .pName = "main" },
        };
        VkVertexInputBindingDescription txt_binding = {
            .binding = 0,
            .stride = sizeof(TextVertex),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        };
        VkVertexInputAttributeDescription txt_attributes[] = {
            { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT,
              .offset = offsetof(TextVertex, position) },
            { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT,
              .offset = offsetof(TextVertex, uv) },
        };
        VkPipelineVertexInputStateCreateInfo txt_vi = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .vertexBindingDescriptionCount = 1,
            .pVertexBindingDescriptions = &txt_binding,
            .vertexAttributeDescriptionCount = 2,
            .pVertexAttributeDescriptions = txt_attributes,
        };
        VkPipelineInputAssemblyStateCreateInfo txt_ia = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST };
        VkPipelineViewportStateCreateInfo txt_vp = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1, .scissorCount = 1 };
        VkPipelineRasterizationStateCreateInfo txt_rs = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE,
            .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1.0f };
        VkPipelineMultisampleStateCreateInfo txt_ms = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT };
        VkDynamicState txt_dyn_arr[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo txt_dyn = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .dynamicStateCount = 2, .pDynamicStates = txt_dyn_arr };
        VkGraphicsPipelineCreateInfo txt_gp = {
            .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .stageCount          = 2, .pStages      = txt_stages,
            .pVertexInputState   = &txt_vi,
            .pInputAssemblyState = &txt_ia,
            .pViewportState      = &txt_vp,
            .pRasterizationState = &txt_rs,
            .pMultisampleState   = &txt_ms,
            .pColorBlendState    = &txt_cb,
            .pDynamicState       = &txt_dyn,
            .layout              = out->txt_layout,
            .renderPass          = ctx->render_pass,
        };
        if (vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &txt_gp, NULL,
                                       &out->txt_pipeline) != VK_SUCCESS) {
            fprintf(stderr, "cgui text: failed to create atlas text pipeline\n");
            vk_draw_destroy(out); return false;
        }
    }

    /* Create the glyph atlas and bind its texture to the descriptor set */
    out->glyph_atlas = cgui_atlas_create(ctx, out->text_engine);
    if (!out->glyph_atlas) {
        fprintf(stderr, "cgui text: failed to create glyph atlas\n");
        vk_draw_destroy(out); return false;
    }
    {
        VkDescriptorImageInfo atlas_img = {
            .sampler     = out->txt_sampler,
            .imageView   = cgui_atlas_image_view(out->glyph_atlas),
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
        VkWriteDescriptorSet atlas_write = {
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = out->txt_set,
            .dstBinding      = 0,
            .descriptorCount = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo      = &atlas_img,
        };
        vkUpdateDescriptorSets(dev, 1, &atlas_write, 0, NULL);
    }

    printf("UI draw ready (Gaussian shadows + rounded shapes + batched text)\n");
    return true;
}

void vk_draw_destroy(VkDraw *d)
{
    if (!d || !d->ctx) return;
    VkDevice dev = d->ctx->device;
    if (!dev) { memset(d, 0, sizeof(*d)); return; }

    vkDeviceWaitIdle(dev);
    destroy_draw_fonts(dev, d);
    cgui_text_destroy(d->text_engine);
    d->text_engine = NULL;

    destroy_color_target(dev, &d->mask_image, &d->mask_memory,
                         &d->mask_view, &d->mask_fb);
    destroy_color_target(dev, &d->temp_image, &d->temp_memory,
                         &d->temp_view, &d->temp_fb);
    destroy_draw_icons(dev, d);

    if (d->rr_pipeline) vkDestroyPipeline(dev, d->rr_pipeline, NULL);
    if (d->mask_pipeline) vkDestroyPipeline(dev, d->mask_pipeline, NULL);
    if (d->blur_pipeline) vkDestroyPipeline(dev, d->blur_pipeline, NULL);
    if (d->blit_pipeline) vkDestroyPipeline(dev, d->blit_pipeline, NULL);
    if (d->icon_pipeline) vkDestroyPipeline(dev, d->icon_pipeline, NULL);
    if (d->rr_layout) vkDestroyPipelineLayout(dev, d->rr_layout, NULL);
    if (d->blur_layout) vkDestroyPipelineLayout(dev, d->blur_layout, NULL);
    if (d->blit_layout) vkDestroyPipelineLayout(dev, d->blit_layout, NULL);
    if (d->shadow_pool) vkDestroyDescriptorPool(dev, d->shadow_pool, NULL);
    if (d->blur_set_layout) vkDestroyDescriptorSetLayout(dev, d->blur_set_layout, NULL);
    if (d->blit_set_layout) vkDestroyDescriptorSetLayout(dev, d->blit_set_layout, NULL);
    if (d->sampler) vkDestroySampler(dev, d->sampler, NULL);
    if (d->offscreen_pass) vkDestroyRenderPass(dev, d->offscreen_pass, NULL);
    if (d->rr_vert) vkDestroyShaderModule(dev, d->rr_vert, NULL);
    if (d->rr_frag) vkDestroyShaderModule(dev, d->rr_frag, NULL);
    if (d->blur_vert) vkDestroyShaderModule(dev, d->blur_vert, NULL);
    if (d->blur_frag) vkDestroyShaderModule(dev, d->blur_frag, NULL);
    if (d->blit_vert) vkDestroyShaderModule(dev, d->blit_vert, NULL);
    if (d->blit_frag) vkDestroyShaderModule(dev, d->blit_frag, NULL);
    if (d->icon_frag) vkDestroyShaderModule(dev, d->icon_frag, NULL);

    if (d->glyph_atlas) {
        cgui_atlas_destroy((CguiGlyphAtlas *)d->glyph_atlas);
        d->glyph_atlas = NULL;
    }
    ShadowBatch *shadow_batch = get_shadow_batch(d, false);
    if (shadow_batch) {
        free(shadow_batch->records);
        free(shadow_batch);
        d->shadow_batch = NULL;
    }
    destroy_text_vertex_frames(d);
    if (d->txt_pipeline) vkDestroyPipeline(dev, d->txt_pipeline, NULL);
    if (d->txt_layout) vkDestroyPipelineLayout(dev, d->txt_layout, NULL);
    if (d->txt_pool) vkDestroyDescriptorPool(dev, d->txt_pool, NULL);
    if (d->txt_set_layout) vkDestroyDescriptorSetLayout(dev, d->txt_set_layout, NULL);
    if (d->txt_sampler) vkDestroySampler(dev, d->txt_sampler, NULL);
    if (d->txt_vert) vkDestroyShaderModule(dev, d->txt_vert, NULL);
    if (d->txt_frag) vkDestroyShaderModule(dev, d->txt_frag, NULL);

    memset(d, 0, sizeof(*d));
}

/* ------------------------------------------------------------------ */
/* Drawing                                                             */
/* ------------------------------------------------------------------ */

void vk_draw_rounded_rect(VkDraw *d,
                          float x, float y, float w, float h,
                          float radius, float soft,
                          const uint8_t rgba[4])
{
    if (!d || !d->ctx || !d->ctx->frame_open || w <= 0.0f || h <= 0.0f) return;
    ShadowBatch *batch = get_shadow_batch(d, false);
    if (batch && batch->collecting) return;

    VkContext *ctx = d->ctx;
    VkCommandBuffer cmd = ctx->active_cmd;

    VkViewport viewport = {
        .width = (float)ctx->swapchain_extent.width,
        .height = (float)ctx->swapchain_extent.height,
        .maxDepth = 1.0f,
    };
    VkRect2D scissor = { .extent = ctx->swapchain_extent };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, d->rr_pipeline);

    RRPush pc = {
        .rect = { x, y, w, h },
        .color = { rgba[0]/255.f, rgba[1]/255.f, rgba[2]/255.f, rgba[3]/255.f },
        .params = { radius, soft,
                    (float)ctx->swapchain_extent.width,
                    (float)ctx->swapchain_extent.height },
    };
    vkCmdPushConstants(cmd, d->rr_layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(pc), &pc);
    vkCmdDraw(cmd, 6, 1, 0, 0);
}

static void draw_shadow_mask(VkDraw *d, float x, float y, float width,
                             float height, float radius, const VkRect2D *scissor)
{
    VkCommandBuffer cmd = d->ctx->active_cmd;
    VkClearValue clear = { .color = {{ 0, 0, 0, 0 }} };
    VkRenderPassBeginInfo pass_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = d->offscreen_pass,
        .framebuffer = d->mask_fb,
        .renderArea = *scissor,
        .clearValueCount = 1,
        .pClearValues = &clear,
    };
    vkCmdBeginRenderPass(cmd, &pass_info, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport = {
        .width = (float)d->offscreen_w,
        .height = (float)d->offscreen_h,
        .maxDepth = 1.0f,
    };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, scissor);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, d->mask_pipeline);

    RRPush push = {
        .rect = { x, y, width, height },
        .color = { 1, 1, 1, 1 },
        .params = { radius, 1.25f, (float)d->offscreen_w,
                    (float)d->offscreen_h },
    };
    vkCmdPushConstants(cmd, d->rr_layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(push), &push);
    vkCmdDraw(cmd, 6, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
}

static void draw_shadow_masks_batched(VkDraw *d, const ShadowBatch *batch)
{
    VkCommandBuffer cmd = d->ctx->active_cmd;
    VkClearValue clear = { .color = {{ 0, 0, 0, 0 }} };
    VkRect2D area = { .extent = { batch->used_width, batch->used_height } };
    VkRenderPassBeginInfo pass_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = d->offscreen_pass,
        .framebuffer = d->mask_fb,
        .renderArea = area,
        .clearValueCount = 1,
        .pClearValues = &clear,
    };
    vkCmdBeginRenderPass(cmd, &pass_info, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport viewport = {
        .width = (float)d->offscreen_w,
        .height = (float)d->offscreen_h,
        .maxDepth = 1.0f,
    };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, d->mask_pipeline);

    for (size_t i = 0; i < batch->count; ++i) {
        const ShadowRecord *record = &batch->records[i];
        if (!record->has_shadow) continue;
        vkCmdSetScissor(cmd, 0, 1, &record->tile);
        RRPush push = {
            .rect = {
                (float)record->tile.offset.x + record->padding,
                (float)record->tile.offset.y + record->padding,
                record->w, record->h,
            },
            .color = { 1, 1, 1, 1 },
            .params = {
                record->radius, 1.25f,
                (float)d->offscreen_w, (float)d->offscreen_h,
            },
        };
        vkCmdPushConstants(cmd, d->rr_layout,
                           VK_SHADER_STAGE_VERTEX_BIT |
                           VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(push), &push);
        vkCmdDraw(cmd, 6, 1, 0, 0);
    }
    vkCmdEndRenderPass(cmd);
}

static void draw_blur_pass(VkDraw *d, VkFramebuffer framebuffer,
                           VkDescriptorSet source, float direction_x,
                           float direction_y, float sigma,
                           const VkRect2D *scissor)
{
    VkCommandBuffer cmd = d->ctx->active_cmd;
    VkClearValue clear = { .color = {{ 0, 0, 0, 0 }} };
    VkRenderPassBeginInfo pass_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = d->offscreen_pass,
        .framebuffer = framebuffer,
        .renderArea = *scissor,
        .clearValueCount = 1,
        .pClearValues = &clear,
    };
    vkCmdBeginRenderPass(cmd, &pass_info, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport = {
        .width = (float)d->offscreen_w,
        .height = (float)d->offscreen_h,
        .maxDepth = 1.0f,
    };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, scissor);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, d->blur_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            d->blur_layout, 0, 1, &source, 0, NULL);

    BlurPush push = {
        .direction = { direction_x, direction_y },
        .sigma = sigma,
        .uv_bounds = {
            0.5f / (float)d->offscreen_w,
            0.5f / (float)d->offscreen_h,
            1.0f - 0.5f / (float)d->offscreen_w,
            1.0f - 0.5f / (float)d->offscreen_h,
        },
    };
    vkCmdPushConstants(cmd, d->blur_layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(push), &push);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
}

static void draw_blur_pass_batched(VkDraw *d, const ShadowBatch *batch,
                                   VkFramebuffer framebuffer,
                                   VkDescriptorSet source, float direction_x,
                                   float direction_y)
{
    VkCommandBuffer cmd = d->ctx->active_cmd;
    VkClearValue clear = { .color = {{ 0, 0, 0, 0 }} };
    VkRect2D area = { .extent = { batch->used_width, batch->used_height } };
    VkRenderPassBeginInfo pass_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = d->offscreen_pass,
        .framebuffer = framebuffer,
        .renderArea = area,
        .clearValueCount = 1,
        .pClearValues = &clear,
    };
    vkCmdBeginRenderPass(cmd, &pass_info, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport viewport = {
        .width = (float)d->offscreen_w,
        .height = (float)d->offscreen_h,
        .maxDepth = 1.0f,
    };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, d->blur_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            d->blur_layout, 0, 1, &source, 0, NULL);

    for (size_t i = 0; i < batch->count; ++i) {
        const ShadowRecord *record = &batch->records[i];
        if (!record->has_shadow) continue;
        vkCmdSetScissor(cmd, 0, 1, &record->tile);
        float min_u = ((float)record->tile.offset.x + 0.5f) /
                      (float)d->offscreen_w;
        float min_v = ((float)record->tile.offset.y + 0.5f) /
                      (float)d->offscreen_h;
        float max_u = ((float)record->tile.offset.x +
                       (float)record->tile.extent.width - 0.5f) /
                      (float)d->offscreen_w;
        float max_v = ((float)record->tile.offset.y +
                       (float)record->tile.extent.height - 0.5f) /
                      (float)d->offscreen_h;
        BlurPush push = {
            .direction = { direction_x, direction_y },
            .sigma = record->sigma,
            .uv_bounds = { min_u, min_v, max_u, max_v },
        };
        vkCmdPushConstants(cmd, d->blur_layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(push), &push);
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }
    vkCmdEndRenderPass(cmd);
}

static bool draw_shadow_batched(VkDraw *d, const ShadowRecord *record)
{
    VkCommandBuffer cmd = d->ctx->active_cmd;
    float dst_x = record->x - record->padding;
    float dst_y = record->y - record->padding + record->offset_y;
    float dst_w = (float)record->tile.extent.width;
    float dst_h = (float)record->tile.extent.height;
    int32_t x0 = (int32_t)floorf(dst_x);
    int32_t y0 = (int32_t)floorf(dst_y);
    int32_t x1 = (int32_t)ceilf(dst_x + dst_w);
    int32_t y1 = (int32_t)ceilf(dst_y + dst_h);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int32_t)d->ctx->swapchain_extent.width) {
        x1 = (int32_t)d->ctx->swapchain_extent.width;
    }
    if (y1 > (int32_t)d->ctx->swapchain_extent.height) {
        y1 = (int32_t)d->ctx->swapchain_extent.height;
    }
    if (x1 <= x0 || y1 <= y0) return true;

    VkRect2D scissor = {
        .offset = { x0, y0 },
        .extent = { (uint32_t)(x1 - x0), (uint32_t)(y1 - y0) },
    };
    VkViewport viewport = {
        .width = (float)d->ctx->swapchain_extent.width,
        .height = (float)d->ctx->swapchain_extent.height,
        .maxDepth = 1.0f,
    };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, d->blit_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            d->blit_layout, 0, 1, &d->blit_set, 0, NULL);
    BlitPush push = {
        .rect = { dst_x, dst_y, dst_w, dst_h },
        .color = {
            record->color[0] / 255.0f,
            record->color[1] / 255.0f,
            record->color[2] / 255.0f,
            record->color[3] / 255.0f,
        },
        .uv_rect = {
            (float)record->tile.offset.x / (float)d->offscreen_w,
            (float)record->tile.offset.y / (float)d->offscreen_h,
            (float)record->tile.extent.width / (float)d->offscreen_w,
            (float)record->tile.extent.height / (float)d->offscreen_h,
        },
        .screen = {
            (float)d->ctx->swapchain_extent.width,
            (float)d->ctx->swapchain_extent.height,
        },
    };
    vkCmdPushConstants(cmd, d->blit_layout,
                       VK_SHADER_STAGE_VERTEX_BIT |
                       VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(push), &push);
    vkCmdDraw(cmd, 6, 1, 0, 0);
    return true;
}

bool vk_draw_begin_shadow_batch(VkDraw *d)
{
    if (!d || !d->ctx || !d->ctx->frame_open) return false;
    ShadowBatch *batch = get_shadow_batch(d, true);
    if (!batch) {
        fprintf(stderr, "cgui shadow: failed to allocate shadow batch state\n");
        return false;
    }
    batch->count = 0;
    batch->playback_index = 0;
    batch->shelf_x = 0;
    batch->shelf_y = 0;
    batch->shelf_height = 0;
    batch->used_width = 0;
    batch->used_height = 0;
    batch->collecting = true;
    batch->ready = false;
    batch->failed = false;
    return true;
}

bool vk_draw_prepare_shadow_batch(VkDraw *d)
{
    ShadowBatch *batch = d ? get_shadow_batch(d, false) : NULL;
    if (!d || !d->ctx || !d->ctx->frame_open || !batch ||
        !batch->collecting) {
        fprintf(stderr, "cgui shadow: invalid shadow batch preparation state\n");
        if (batch) {
            batch->collecting = false;
            batch->ready = false;
        }
        return false;
    }
    batch->collecting = false;
    if (batch->failed) {
        fprintf(stderr, "cgui shadow: atlas capacity or memory exhausted; "
                        "falling back to per-box shadows\n");
        batch->ready = false;
        return false;
    }
    if (!vk_draw_resize(d)) {
        fprintf(stderr, "cgui shadow: failed to create shadow atlas\n");
        batch->ready = false;
        return false;
    }

    bool has_shadows = false;
    for (size_t i = 0; i < batch->count; ++i) {
        has_shadows |= batch->records[i].has_shadow;
    }
    if (has_shadows) {
        vk_context_suspend_pass(d->ctx);
        draw_shadow_masks_batched(d, batch);
        draw_blur_pass_batched(d, batch, d->temp_fb, d->blur_set_a,
                               1.0f / (float)d->offscreen_w, 0.0f);
        draw_blur_pass_batched(d, batch, d->mask_fb, d->blur_set_b,
                               0.0f, 1.0f / (float)d->offscreen_h);
        vk_context_resume_pass(d->ctx);
    }
    batch->playback_index = 0;
    batch->ready = true;
    return true;
}

void vk_draw_end_shadow_batch(VkDraw *d)
{
    ShadowBatch *batch = d ? get_shadow_batch(d, false) : NULL;
    if (!batch) return;
    if (batch->ready && batch->playback_index != batch->count) {
        fprintf(stderr, "cgui shadow: collected and rendered style-box counts differ\n");
    }
    batch->collecting = false;
    batch->ready = false;
}

static bool draw_shadow_direct(VkDraw *d, float x, float y, float w, float h,
                               const Style *s)
{
    if ((s->shadow_blur <= 0.0f && s->shadow_offset_y == 0.0f) ||
        s->shadow_color[3] == 0 || !d->ctx->frame_open) return true;
    if (!vk_draw_resize(d)) return false;

    float sigma = fmaxf(s->shadow_blur * 0.5f, 1.0f);
    float padding = ceilf(sigma * 3.0f + 2.0f);
    int32_t sx = (int32_t)floorf(x - padding);
    int32_t sy = (int32_t)floorf(y - padding);
    int32_t ex = (int32_t)ceilf(x + w + padding);
    int32_t ey = (int32_t)ceilf(y + h + padding);
    if (sx < 0) sx = 0;
    if (sy < 0) sy = 0;
    if (ex > (int32_t)d->offscreen_w) ex = (int32_t)d->offscreen_w;
    if (ey > (int32_t)d->offscreen_h) ey = (int32_t)d->offscreen_h;
    if (ex <= sx || ey <= sy) return true;

    VkRect2D scissor = {
        .offset = { sx, sy },
        .extent = { (uint32_t)(ex - sx), (uint32_t)(ey - sy) },
    };
    vk_context_suspend_pass(d->ctx);
    draw_shadow_mask(d, x, y, w, h, s->corner_radius, &scissor);
    draw_blur_pass(d, d->temp_fb, d->blur_set_a,
                   1.0f / (float)d->offscreen_w, 0.0f, sigma, &scissor);
    draw_blur_pass(d, d->mask_fb, d->blur_set_b,
                   0.0f, 1.0f / (float)d->offscreen_h, sigma, &scissor);
    vk_context_resume_pass(d->ctx);

    ShadowRecord record = {
        .x = (float)sx + padding,
        .y = (float)sy + padding,
        .sigma = sigma,
        .padding = padding,
        .offset_y = s->shadow_offset_y,
        .tile = scissor,
    };
    memcpy(record.color, s->shadow_color, sizeof(record.color));
    return draw_shadow_batched(d, &record);
}

void vk_draw_style_box(VkDraw *d, float x, float y, float w, float h,
                       const Style *s)
{
    if (!d || !s || !d->ctx) return;

    ShadowBatch *batch = get_shadow_batch(d, false);
    if (batch && batch->collecting) {
        if (!append_shadow_record(d, s, x, y, w, h)) batch->failed = true;
        return;
    }
    if (batch && batch->ready) {
        if (batch->playback_index < batch->count) {
            const ShadowRecord *record =
                &batch->records[batch->playback_index++];
            if (record->has_shadow) (void)draw_shadow_batched(d, record);
        } else {
            fprintf(stderr, "cgui shadow: style-box playback exceeded collected boxes\n");
            if (!draw_shadow_direct(d, x, y, w, h, s)) {
                fprintf(stderr, "cgui shadow: failed to draw fallback shadow\n");
            }
        }
    } else {
        if (!draw_shadow_direct(d, x, y, w, h, s)) {
            fprintf(stderr, "cgui shadow: failed to draw fallback shadow\n");
        }
    }

    if (s->border_width > 0.0f && s->border_color[3] > 0) {
        float bw = s->border_width;
        vk_draw_rounded_rect(d, x - bw*0.5f, y - bw*0.5f, w + bw, h + bw,
                             s->corner_radius + bw*0.5f, 1.0f, s->border_color);
    }

    vk_draw_rounded_rect(d, x, y, w, h, s->corner_radius, 1.25f, s->bg);
}

static bool draw_text_at(VkDraw *d, const char *utf8, float x, float y,
                         float pixel_size, uint32_t style_flags,
                         const uint8_t rgba[4], CguiTextAlign align,
                         bool align_box, float box_w, float box_h,
                         CguiGlyphAtlas *atlas, VkDescriptorSet text_set)
{
    ShadowBatch *batch = d ? get_shadow_batch(d, false) : NULL;
    if (batch && batch->collecting) return true;
    if (!d || !d->ctx || !d->ctx->frame_open || !utf8 || !rgba ||
        !isfinite(pixel_size) || pixel_size <= 0.0f || pixel_size > 512.0f) {
        return false;
    }
    if (utf8[0] == '\0') {
        return true;
    }

    uint32_t raster_size = (uint32_t)floorf(pixel_size + 0.5f);
    if (raster_size == 0) {
        return false;
    }

    if (!atlas || !text_set) {
        return false;
    }

    CguiGlyphQuad stack_quads[256];
    CguiGlyphQuad *quads = stack_quads;
    uint32_t cap = 256;
    uint32_t count = 0;
    float text_w = 0.0f;
    float text_h = 0.0f;

    if (!cgui_atlas_shape_quads(atlas, utf8, 0.0f, 0.0f, raster_size, style_flags,
                                quads, cap, &count, &text_w, &text_h)) {
        return false;
    }

    if (count > cap) {
        quads = malloc(count * sizeof(CguiGlyphQuad));
        if (!quads) return false;
        cap = count;
        if (!cgui_atlas_shape_quads(atlas, utf8, 0.0f, 0.0f, raster_size, style_flags,
                                    quads, cap, &count, &text_w, &text_h)) {
            free(quads);
            return false;
        }
    }

    if (count == 0) {
        if (quads != stack_quads) free(quads);
        return true;
    }

    float base_x = x;
    float base_y = y;

    if (align_box) {
        if (align == CGUI_ALIGN_CENTER) {
            base_x += (box_w - text_w) * 0.5f;
        } else if (align == CGUI_ALIGN_RIGHT) {
            base_x += (box_w - text_w);
        }
        base_y += (box_h - text_h) * 0.5f;
    }

    base_x = floorf(base_x + 0.5f);
    base_y = floorf(base_y + 0.5f);

    /* Upload new glyphs before drawing, in this frame's command buffer. */
    if (cgui_atlas_has_pending_upload(atlas)) {
        bool upload_ok = cgui_atlas_flush(atlas);
        if (!upload_ok) {
            if (quads != stack_quads) free(quads);
            return false;
        }
    }

    VkContext *ctx = d->ctx;
    VkCommandBuffer cmd = ctx->active_cmd;
    if (count > UINT32_MAX / 6u) {
        if (quads != stack_quads) free(quads);
        return false;
    }
    TextVertexChunk *vertex_chunk;
    VkDeviceSize vertex_offset;
    TextVertex *vertices;
    if (!get_text_vertex_region(d, count * 6u, &vertex_chunk,
                                &vertex_offset, &vertices)) {
        if (quads != stack_quads) free(quads);
        return false;
    }

    VkViewport viewport = {
        .width = (float)ctx->swapchain_extent.width,
        .height = (float)ctx->swapchain_extent.height,
        .maxDepth = 1.0f,
    };
    VkRect2D scissor = { .extent = ctx->swapchain_extent };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, d->txt_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            d->txt_layout, 0, 1, &text_set, 0, NULL);

    float cr = rgba[0] / 255.0f;
    float cg = rgba[1] / 255.0f;
    float cb = rgba[2] / 255.0f;
    float ca = rgba[3] / 255.0f;

    static const float corners[6][2] = {
        {0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f},
        {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f},
    };
    for (uint32_t i = 0; i < count; ++i) {
        const CguiGlyphQuad *q = &quads[i];
        float x0 = floorf(base_x + q->x + 0.5f);
        float y0 = floorf(base_y + q->y + 0.5f);
        float x1 = x0 + q->w;
        float y1 = y0 + q->h;
        float uv[4][2] = {
            { q->u0, q->v0 },
            { q->u1, q->v0 },
            { q->u0, q->v1 },
            { q->u1, q->v1 },
        };
        for (uint32_t vertex = 0; vertex < 6; ++vertex) {
            float cx = corners[vertex][0];
            float cy = corners[vertex][1];
            TextVertex *dst = &vertices[i * 6u + vertex];
            dst->position[0] = x0 + cx * (x1 - x0);
            dst->position[1] = y0 + cy * (y1 - y0);
            uint32_t uv_index = (cx > 0.5f ? 1u : 0u) +
                                (cy > 0.5f ? 2u : 0u);
            dst->uv[0] = uv[uv_index][0];
            dst->uv[1] = uv[uv_index][1];
        }
    }

    TextPush push = {
        .color = { cr, cg, cb, ca },
        .screen = {
            (float)ctx->swapchain_extent.width,
            (float)ctx->swapchain_extent.height,
        },
    };
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_chunk->buffer, &vertex_offset);
    vkCmdPushConstants(cmd, d->txt_layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(push), &push);
    vkCmdDraw(cmd, count * 6u, 1, 0, 0);

    if (quads != stack_quads) {
        free(quads);
    }
    return true;
}

VkDrawFont *vk_draw_load_font(VkDraw *d, const char *family)
{
    if (!d || !d->ctx || !family || !family[0] ||
        d->ctx->frame_open || !d->txt_set_layout) {
        fprintf(stderr, "cgui text: font loading requires a family and no open frame\n");
        return NULL;
    }
    for (VkDrawFont *cached = d->text_fonts; cached; cached = cached->next) {
        if (strcmp(cached->family, family) == 0) return cached;
    }

    VkDrawFont *font = calloc(1, sizeof(*font));
    if (!font) {
        fprintf(stderr, "cgui text: out of memory allocating font handle\n");
        return NULL;
    }
    font->owner = d;
    font->family = strdup(family);
    if (!font->family) goto fail;
    font->engine = cgui_text_create_family(family);
    if (!font->engine) goto fail;
    font->atlas = cgui_atlas_create(d->ctx, font->engine);
    if (!font->atlas) {
        fprintf(stderr, "cgui text: failed to create glyph atlas for '%s'\n", family);
        goto fail;
    }

    VkDescriptorPoolSize pool_size = {
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
    };
    VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1,
        .poolSizeCount = 1,
        .pPoolSizes = &pool_size,
    };
    if (vkCreateDescriptorPool(d->ctx->device, &pool_info, NULL,
                               &font->pool) != VK_SUCCESS) {
        fprintf(stderr, "cgui text: failed to create descriptor pool for '%s'\n",
                family);
        goto fail;
    }
    VkDescriptorSetAllocateInfo set_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = font->pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &d->txt_set_layout,
    };
    if (vkAllocateDescriptorSets(d->ctx->device, &set_info, &font->set) != VK_SUCCESS) {
        fprintf(stderr, "cgui text: failed to allocate descriptor set for '%s'\n",
                family);
        goto fail;
    }
    VkDescriptorImageInfo image = {
        .sampler = d->txt_sampler,
        .imageView = cgui_atlas_image_view(font->atlas),
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = font->set,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &image,
    };
    vkUpdateDescriptorSets(d->ctx->device, 1, &write, 0, NULL);

    font->next = d->text_fonts;
    d->text_fonts = font;
    return font;

fail:
    if (font->atlas) cgui_atlas_destroy(font->atlas);
    if (font->engine) cgui_text_destroy(font->engine);
    if (font->pool) vkDestroyDescriptorPool(d->ctx->device, font->pool, NULL);
    free(font->family);
    free(font);
    return NULL;
}

bool vk_draw_text_font_styled(VkDraw *d, const VkDrawFont *font,
                              const char *utf8, float x, float y,
                              float pixel_size, uint32_t style_flags,
                              const uint8_t rgba[4])
{
    if (!font || font->owner != d) return false;
    return draw_text_at(d, utf8, x, y, pixel_size, style_flags, rgba,
                        CGUI_ALIGN_LEFT, false, 0.0f, 0.0f,
                        font->atlas, font->set);
}

bool vk_draw_text(VkDraw *d, const char *utf8, float x, float y,
                  float pixel_size, const uint8_t rgba[4])
{
    return draw_text_at(d, utf8, x, y, pixel_size, 0, rgba,
                        CGUI_ALIGN_LEFT, false, 0.0f, 0.0f,
                        d ? (CguiGlyphAtlas *)d->glyph_atlas : NULL,
                        d ? d->txt_set : VK_NULL_HANDLE);
}

bool vk_draw_text_centered(VkDraw *d, const char *utf8,
                           float x, float y, float w, float h,
                           float pixel_size, const uint8_t rgba[4])
{
    return draw_text_at(d, utf8, x, y, pixel_size, 0, rgba,
                        CGUI_ALIGN_CENTER, true, w, h,
                        d ? (CguiGlyphAtlas *)d->glyph_atlas : NULL,
                        d ? d->txt_set : VK_NULL_HANDLE);
}

bool vk_draw_text_styled(VkDraw *d, const char *utf8, float x, float y,
                         float pixel_size, uint32_t style_flags,
                         const uint8_t rgba[4])
{
    return draw_text_at(d, utf8, x, y, pixel_size, style_flags, rgba,
                        CGUI_ALIGN_LEFT, false, 0.0f, 0.0f,
                        d ? (CguiGlyphAtlas *)d->glyph_atlas : NULL,
                        d ? d->txt_set : VK_NULL_HANDLE);
}

bool vk_draw_text_centered_styled(VkDraw *d, const char *utf8,
                                  float x, float y, float w, float h,
                                  float pixel_size, uint32_t style_flags,
                                  const uint8_t rgba[4])
{
    return draw_text_at(d, utf8, x, y, pixel_size, style_flags, rgba,
                        CGUI_ALIGN_CENTER, true, w, h,
                        d ? (CguiGlyphAtlas *)d->glyph_atlas : NULL,
                        d ? d->txt_set : VK_NULL_HANDLE);
}

bool vk_draw_text_aligned_styled(VkDraw *d, const char *utf8,
                                 float x, float y, float w, float h,
                                 float pixel_size, uint32_t style_flags,
                                 const uint8_t rgba[4], CguiTextAlign align)
{
    return draw_text_at(d, utf8, x, y, pixel_size, style_flags, rgba,
                        align, true, w, h,
                        d ? (CguiGlyphAtlas *)d->glyph_atlas : NULL,
                        d ? d->txt_set : VK_NULL_HANDLE);
}

bool vk_draw_measure_text(VkDraw *d, const char *utf8,
                          float pixel_size, uint32_t style_flags,
                          float *w_out, float *h_out)
{
    if (!d || !utf8 || pixel_size <= 0.0f || !w_out || !h_out) {
        return false;
    }
    uint32_t raster_size = (uint32_t)floorf(pixel_size + 0.5f);
    if (raster_size == 0) {
        return false;
    }
    CguiGlyphAtlas *atlas = (CguiGlyphAtlas *)d->glyph_atlas;
    if (!atlas) {
        return false;
    }
    return cgui_atlas_shape_quads(atlas, utf8, 0.0f, 0.0f, raster_size, style_flags,
                                  NULL, 0, NULL, w_out, h_out);
}

/* Mask pipeline global set from create */
