/**
 * vk_draw.c – SDF body + real separable Gaussian soft shadows
 */

#include "vk_draw.h"
#include "text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define CGUI_TEXT_CACHE_CAPACITY 256u

typedef struct VkTextTexture {
    char *utf8;
    uint32_t pixel_size;
    uint32_t style_flags;
    uint32_t width;
    uint32_t height;
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    VkDescriptorSet descriptor;
} VkTextTexture;

/* Mask pipeline (offscreen SDF) – set in vk_draw_create */
VkPipeline cgui_mask_pipeline = VK_NULL_HANDLE;


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
} BlurPush;

typedef struct BlitPush {
    float rect[4];
    float color[4];
    float uv_rect[4];
    float screen[2];
} BlitPush;

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

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

static uint32_t find_memory_type(VkPhysicalDevice phys, uint32_t type_bits, VkMemoryPropertyFlags flags)
{
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(phys, &props);
    for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
        if ((type_bits & (1u << i)) && (props.memoryTypes[i].propertyFlags & flags) == flags) {
            return i;
        }
    }
    return 0;
}

static bool find_memory_type_checked(VkPhysicalDevice phys, uint32_t type_bits,
                                     VkMemoryPropertyFlags flags, uint32_t *index_out)
{
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(phys, &props);
    for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
        if ((type_bits & (1u << i)) &&
            (props.memoryTypes[i].propertyFlags & flags) == flags) {
            *index_out = i;
            return true;
        }
    }
    return false;
}

static void destroy_text_texture(VkDevice dev, VkTextTexture *texture)
{
    if (!texture) {
        return;
    }
    if (texture->view) vkDestroyImageView(dev, texture->view, NULL);
    if (texture->image) vkDestroyImage(dev, texture->image, NULL);
    if (texture->memory) vkFreeMemory(dev, texture->memory, NULL);
    free(texture->utf8);
    memset(texture, 0, sizeof(*texture));
}

static bool create_text_image(VkDraw *d, const CguiTextBitmap *bitmap,
                              VkImage *image_out, VkDeviceMemory *memory_out,
                              VkImageView *view_out)
{
    VkContext *ctx = d->ctx;
    VkDevice dev = ctx->device;
    size_t pixel_count = (size_t)bitmap->width * bitmap->height;
    if (!bitmap->alpha || !bitmap->width || !bitmap->height ||
        pixel_count > SIZE_MAX / 4u) {
        return false;
    }

    uint8_t *rgba = malloc(pixel_count * 4u);
    if (!rgba) {
        return false;
    }
    for (size_t i = 0; i < pixel_count; ++i) {
        rgba[i * 4u + 0u] = 255u;
        rgba[i * 4u + 1u] = 255u;
        rgba[i * 4u + 2u] = 255u;
        rgba[i * 4u + 3u] = bitmap->alpha[i];
    }

    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory staging_memory = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    bool ok = false;
    size_t byte_count = pixel_count * 4u;

    VkBufferCreateInfo bci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = byte_count,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    if (vkCreateBuffer(dev, &bci, NULL, &staging) != VK_SUCCESS) {
        goto cleanup;
    }

    VkMemoryRequirements staging_req;
    vkGetBufferMemoryRequirements(dev, staging, &staging_req);
    uint32_t staging_type;
    if (!find_memory_type_checked(ctx->physical_device, staging_req.memoryTypeBits,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                  &staging_type)) {
        fprintf(stderr, "cgui text: no coherent host-visible Vulkan memory type\n");
        goto cleanup;
    }
    VkMemoryAllocateInfo staging_ai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = staging_req.size,
        .memoryTypeIndex = staging_type,
    };
    if (vkAllocateMemory(dev, &staging_ai, NULL, &staging_memory) != VK_SUCCESS ||
        vkBindBufferMemory(dev, staging, staging_memory, 0) != VK_SUCCESS) {
        goto cleanup;
    }
    void *mapped = NULL;
    if (vkMapMemory(dev, staging_memory, 0, byte_count, 0, &mapped) != VK_SUCCESS) {
        goto cleanup;
    }
    memcpy(mapped, rgba, byte_count);
    vkUnmapMemory(dev, staging_memory);

    VkImageCreateInfo ici = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = { bitmap->width, bitmap->height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (vkCreateImage(dev, &ici, NULL, image_out) != VK_SUCCESS) {
        goto cleanup;
    }

    VkMemoryRequirements image_req;
    vkGetImageMemoryRequirements(dev, *image_out, &image_req);
    uint32_t image_type;
    if (!find_memory_type_checked(ctx->physical_device, image_req.memoryTypeBits,
                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &image_type) &&
        !find_memory_type_checked(ctx->physical_device, image_req.memoryTypeBits, 0,
                                  &image_type)) {
        fprintf(stderr, "cgui text: no compatible Vulkan image memory type\n");
        goto cleanup;
    }
    VkMemoryAllocateInfo image_ai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = image_req.size,
        .memoryTypeIndex = image_type,
    };
    if (vkAllocateMemory(dev, &image_ai, NULL, memory_out) != VK_SUCCESS ||
        vkBindImageMemory(dev, *image_out, *memory_out, 0) != VK_SUCCESS) {
        goto cleanup;
    }

    VkImageViewCreateInfo view_ci = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = *image_out,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1,
            .layerCount = 1,
        },
    };
    if (vkCreateImageView(dev, &view_ci, NULL, view_out) != VK_SUCCESS) {
        goto cleanup;
    }

    VkCommandBufferAllocateInfo command_ai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = ctx->command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    if (vkAllocateCommandBuffers(dev, &command_ai, &cmd) != VK_SUCCESS) {
        goto cleanup;
    }
    VkCommandBufferBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    if (vkBeginCommandBuffer(cmd, &begin) != VK_SUCCESS) {
        goto cleanup;
    }

    VkImageMemoryBarrier to_transfer = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = *image_out,
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
        .imageExtent = { bitmap->width, bitmap->height, 1 },
    };
    vkCmdCopyBufferToImage(cmd, staging, *image_out,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    VkImageMemoryBarrier to_shader = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = *image_out,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1,
            .layerCount = 1,
        },
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                         0, NULL, 0, NULL, 1, &to_shader);

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        goto cleanup;
    }
    VkSubmitInfo submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
    };
    if (vkQueueSubmit(ctx->graphics_queue, 1, &submit, VK_NULL_HANDLE) != VK_SUCCESS ||
        vkQueueWaitIdle(ctx->graphics_queue) != VK_SUCCESS) {
        goto cleanup;
    }
    ok = true;

cleanup:
    if (cmd) {
        vkFreeCommandBuffers(dev, ctx->command_pool, 1, &cmd);
    }
    if (staging) vkDestroyBuffer(dev, staging, NULL);
    if (staging_memory) vkFreeMemory(dev, staging_memory, NULL);
    free(rgba);
    if (!ok) {
        if (*view_out) vkDestroyImageView(dev, *view_out, NULL);
        if (*image_out) vkDestroyImage(dev, *image_out, NULL);
        if (*memory_out) vkFreeMemory(dev, *memory_out, NULL);
        *view_out = VK_NULL_HANDLE;
        *image_out = VK_NULL_HANDLE;
        *memory_out = VK_NULL_HANDLE;
    }
    return ok;
}

static VkTextTexture *get_text_texture(VkDraw *d, const char *utf8,
                                       uint32_t pixel_size, uint32_t style_flags)
{
    VkTextTexture *cache = d->text_cache;
    for (size_t i = 0; i < d->text_cache_count; ++i) {
        if (cache[i].pixel_size == pixel_size &&
            cache[i].style_flags == style_flags &&
            strcmp(cache[i].utf8, utf8) == 0) {
            return &cache[i];
        }
    }

    if (d->text_cache_count >= CGUI_TEXT_CACHE_CAPACITY) {
        fprintf(stderr,
                "cgui text: texture cache is full (%u unique labels); "
                "destroy and recreate the draw context to clear it\n",
                CGUI_TEXT_CACHE_CAPACITY);
        return NULL;
    }

    CguiTextBitmap bitmap = {0};
    if (!cgui_text_render_styled(d->text_engine, utf8, pixel_size, style_flags, &bitmap)) {
        return NULL;
    }

    VkTextTexture texture = {
        .pixel_size = pixel_size,
        .style_flags = style_flags,
        .width = bitmap.width,
        .height = bitmap.height,
    };
    size_t text_length = strlen(utf8);
    texture.utf8 = malloc(text_length + 1u);
    if (!texture.utf8) {
        cgui_text_bitmap_destroy(&bitmap);
        return NULL;
    }
    memcpy(texture.utf8, utf8, text_length + 1u);

    bool resume_pass = d->ctx->frame_open;
    if (resume_pass) {
        vk_context_suspend_pass(d->ctx);
    }
    bool uploaded = create_text_image(d, &bitmap, &texture.image,
                                      &texture.memory, &texture.view);
    if (resume_pass) {
        vk_context_resume_pass(d->ctx);
    }
    cgui_text_bitmap_destroy(&bitmap);
    if (!uploaded) {
        destroy_text_texture(d->ctx->device, &texture);
        fprintf(stderr, "cgui text: Vulkan texture upload failed\n");
        return NULL;
    }

    VkDescriptorSetAllocateInfo alloc = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = d->blur_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &d->blit_set_layout,
    };
    if (vkAllocateDescriptorSets(d->ctx->device, &alloc,
                                 &texture.descriptor) != VK_SUCCESS) {
        destroy_text_texture(d->ctx->device, &texture);
        fprintf(stderr, "cgui text: failed to allocate a texture descriptor\n");
        return NULL;
    }

    VkDescriptorImageInfo image_info = {
        .sampler = d->sampler,
        .imageView = texture.view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = texture.descriptor,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &image_info,
    };
    vkUpdateDescriptorSets(d->ctx->device, 1, &write, 0, NULL);

    cache[d->text_cache_count] = texture;
    return &cache[d->text_cache_count++];
}

static bool create_color_target(VkDraw *d, uint32_t w, uint32_t h,
                                VkImage *image, VkDeviceMemory *memory,
                                VkImageView *view, VkFramebuffer *fb)
{
    VkContext *ctx = d->ctx;
    VkDevice dev = ctx->device;

    VkImageCreateInfo ici = {
        .sType     = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format    = VK_FORMAT_R8G8B8A8_UNORM,
        .extent    = { w, h, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples   = VK_SAMPLE_COUNT_1_BIT,
        .tiling    = VK_IMAGE_TILING_OPTIMAL,
        .usage     = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (vkCreateImage(dev, &ici, NULL, image) != VK_SUCCESS) return false;

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(dev, *image, &req);
    VkMemoryAllocateInfo mai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size,
        .memoryTypeIndex = find_memory_type(ctx->physical_device, req.memoryTypeBits,
                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    };
    if (vkAllocateMemory(dev, &mai, NULL, memory) != VK_SUCCESS) return false;
    vkBindImageMemory(dev, *image, *memory, 0);

    VkImageViewCreateInfo vci = {
        .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image    = *image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format   = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1,
            .layerCount = 1,
        },
    };
    if (vkCreateImageView(dev, &vci, NULL, view) != VK_SUCCESS) return false;

    VkFramebufferCreateInfo fci = {
        .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass      = d->offscreen_pass,
        .attachmentCount = 1,
        .pAttachments    = view,
        .width           = w,
        .height          = h,
        .layers          = 1,
    };
    if (vkCreateFramebuffer(dev, &fci, NULL, fb) != VK_SUCCESS) return false;
    return true;
}

static void destroy_color_target(VkDevice dev, VkImage *image, VkDeviceMemory *memory,
                                 VkImageView *view, VkFramebuffer *fb)
{
    if (*fb)     vkDestroyFramebuffer(dev, *fb, NULL);
    if (*view)   vkDestroyImageView(dev, *view, NULL);
    if (*image)  vkDestroyImage(dev, *image, NULL);
    if (*memory) vkFreeMemory(dev, *memory, NULL);
    *fb = VK_NULL_HANDLE; *view = VK_NULL_HANDLE;
    *image = VK_NULL_HANDLE; *memory = VK_NULL_HANDLE;
}

/* ------------------------------------------------------------------ */
/* Offscreen resize                                                    */
/* ------------------------------------------------------------------ */

bool vk_draw_resize(VkDraw *d)
{
    if (!d || !d->ctx) return false;
    VkContext *ctx = d->ctx;
    uint32_t w = ctx->swapchain_extent.width;
    uint32_t h = ctx->swapchain_extent.height;
    if (w == 0 || h == 0) return false;
    if (w == d->offscreen_w && h == d->offscreen_h && d->mask_image) {
        return true;
    }

    vkDeviceWaitIdle(ctx->device);
    destroy_color_target(ctx->device, &d->mask_image, &d->mask_memory, &d->mask_view, &d->mask_fb);
    destroy_color_target(ctx->device, &d->temp_image, &d->temp_memory, &d->temp_view, &d->temp_fb);

    if (!create_color_target(d, w, h, &d->mask_image, &d->mask_memory, &d->mask_view, &d->mask_fb)) {
        return false;
    }
    if (!create_color_target(d, w, h, &d->temp_image, &d->temp_memory, &d->temp_view, &d->temp_fb)) {
        return false;
    }

    d->offscreen_w = w;
    d->offscreen_h = h;

    /* Update descriptor sets to point at the new views */
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
            .pImageInfo = &mask_info, /* final blur lives in mask after V pass */
        },
    };
    vkUpdateDescriptorSets(ctx->device, 3, writes, 0, NULL);
    return true;
}

/* ------------------------------------------------------------------ */
/* Create / destroy                                                    */
/* ------------------------------------------------------------------ */

static bool create_offscreen_pass(VkDraw *d)
{
    VkAttachmentDescription color = {
        .format         = VK_FORMAT_R8G8B8A8_UNORM,
        .samples        = VK_SAMPLE_COUNT_1_BIT,
        .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    VkAttachmentReference ref = {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    VkSubpassDescription sub = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &ref,
    };
    VkSubpassDependency deps[2] = {
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
    VkRenderPassCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &color,
        .subpassCount = 1,
        .pSubpasses = &sub,
        .dependencyCount = 2,
        .pDependencies = deps,
    };
    return vkCreateRenderPass(d->ctx->device, &ci, NULL, &d->offscreen_pass) == VK_SUCCESS;
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

bool vk_draw_create(VkDraw *out, VkContext *ctx, const VkDrawShaders *sh)
{
    memset(out, 0, sizeof(*out));
    out->ctx = ctx;
    VkDevice dev = ctx->device;

    if (!create_offscreen_pass(out)) return false;

    out->text_engine = cgui_text_create();
    out->text_cache = calloc(CGUI_TEXT_CACHE_CAPACITY, sizeof(VkTextTexture));
    if (!out->text_engine || !out->text_cache) {
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
    if (!out->rr_vert || !out->rr_frag || !out->blur_vert || !out->blur_frag ||
        !out->blit_vert || !out->blit_frag) {
        vk_draw_destroy(out);
        return false;
    }

    /* Sampler */
    VkSamplerCreateInfo sci = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    };
    vkCreateSampler(dev, &sci, NULL, &out->sampler);

    /* Descriptor set layouts (one combined image sampler) */
    VkDescriptorSetLayoutBinding binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    VkDescriptorSetLayoutCreateInfo dslci = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &binding,
    };
    vkCreateDescriptorSetLayout(dev, &dslci, NULL, &out->blur_set_layout);
    vkCreateDescriptorSetLayout(dev, &dslci, NULL, &out->blit_set_layout);

    VkDescriptorPoolSize pool_size = {
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = CGUI_TEXT_CACHE_CAPACITY + 3u,
    };
    VkDescriptorPoolCreateInfo dpci = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = CGUI_TEXT_CACHE_CAPACITY + 3u,
        .poolSizeCount = 1,
        .pPoolSizes = &pool_size,
    };
    vkCreateDescriptorPool(dev, &dpci, NULL, &out->blur_pool);

    VkDescriptorSetLayout layouts[3] = {
        out->blur_set_layout, out->blur_set_layout, out->blit_set_layout,
    };
    VkDescriptorSetAllocateInfo dsai = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = out->blur_pool,
        .descriptorSetCount = 3,
        .pSetLayouts = layouts,
    };
    VkDescriptorSet sets[3];
    vkAllocateDescriptorSets(dev, &dsai, sets);
    out->blur_set_a = sets[0];
    out->blur_set_b = sets[1];
    out->blit_set   = sets[2];

    /* Pipeline layouts */
    VkPushConstantRange rr_pcr = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .size = sizeof(RRPush),
    };
    VkPipelineLayoutCreateInfo rr_lci = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &rr_pcr,
    };
    vkCreatePipelineLayout(dev, &rr_lci, NULL, &out->rr_layout);

    VkPushConstantRange blur_pcr = {
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .size = sizeof(BlurPush),
    };
    VkPipelineLayoutCreateInfo blur_lci = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &out->blur_set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &blur_pcr,
    };
    vkCreatePipelineLayout(dev, &blur_lci, NULL, &out->blur_layout);

    VkPushConstantRange blit_pcr = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .size = sizeof(BlitPush),
    };
    VkPipelineLayoutCreateInfo blit_lci = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &out->blit_set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &blit_pcr,
    };
    vkCreatePipelineLayout(dev, &blit_lci, NULL, &out->blit_layout);

    /* Pipelines – RR works on both main and offscreen (compatible attachment layouts)
       We create RR against main render pass; for offscreen we need same format compatibility.
       Using main render_pass for body; for mask we create a second RR pipeline on offscreen_pass. */
    out->rr_pipeline = create_graphics_pipeline(
        dev, ctx->render_pass, out->rr_layout, out->rr_vert, out->rr_frag, true, true);

    cgui_mask_pipeline = create_graphics_pipeline(
        dev, out->offscreen_pass, out->rr_layout, out->rr_vert, out->rr_frag, false, true);

    out->blur_pipeline = create_graphics_pipeline(
        dev, out->offscreen_pass, out->blur_layout, out->blur_vert, out->blur_frag, false, true);

    out->blit_pipeline = create_graphics_pipeline(
        dev, ctx->render_pass, out->blit_layout, out->blit_vert, out->blit_frag, true, true);

    if (!out->rr_pipeline || !cgui_mask_pipeline || !out->blur_pipeline || !out->blit_pipeline) {
        vk_draw_destroy(out);
        return false;
    }

    if (!vk_draw_resize(out)) {
        vk_draw_destroy(out);
        return false;
    }

    printf("UI draw ready (SDF body + Gaussian shadow blur)\n");
    return true;
}

void vk_draw_destroy(VkDraw *d)
{
    if (!d || !d->ctx) return;
    VkDevice dev = d->ctx->device;
    if (!dev) { memset(d, 0, sizeof(*d)); return; }

    vkDeviceWaitIdle(dev);
    VkTextTexture *text_cache = d->text_cache;
    for (size_t i = 0; i < d->text_cache_count; ++i) {
        destroy_text_texture(dev, &text_cache[i]);
    }
    free(d->text_cache);
    d->text_cache = NULL;
    d->text_cache_count = 0;
    cgui_text_destroy(d->text_engine);
    d->text_engine = NULL;

    destroy_color_target(dev, &d->mask_image, &d->mask_memory, &d->mask_view, &d->mask_fb);
    destroy_color_target(dev, &d->temp_image, &d->temp_memory, &d->temp_view, &d->temp_fb);

    if (d->rr_pipeline) vkDestroyPipeline(dev, d->rr_pipeline, NULL);
    if (cgui_mask_pipeline) { vkDestroyPipeline(dev, cgui_mask_pipeline, NULL); cgui_mask_pipeline = VK_NULL_HANDLE; }
    if (d->blur_pipeline) vkDestroyPipeline(dev, d->blur_pipeline, NULL);
    if (d->blit_pipeline) vkDestroyPipeline(dev, d->blit_pipeline, NULL);
    if (d->rr_layout) vkDestroyPipelineLayout(dev, d->rr_layout, NULL);
    if (d->blur_layout) vkDestroyPipelineLayout(dev, d->blur_layout, NULL);
    if (d->blit_layout) vkDestroyPipelineLayout(dev, d->blit_layout, NULL);
    if (d->blur_pool) vkDestroyDescriptorPool(dev, d->blur_pool, NULL);
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

static void draw_mask_offscreen(VkDraw *d, float x, float y, float w, float h,
                                float radius, VkPipeline mask_pipe,
                                const VkRect2D *scissor)
{
    VkContext *ctx = d->ctx;
    VkCommandBuffer cmd = ctx->active_cmd;

    VkClearValue clear = { .color = {{ 0, 0, 0, 0 }} };
    VkRenderPassBeginInfo rp = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = d->offscreen_pass,
        .framebuffer = d->mask_fb,
        .renderArea = *scissor,
        .clearValueCount = 1,
        .pClearValues = &clear,
    };
    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport = {
        .width = (float)d->offscreen_w,
        .height = (float)d->offscreen_h,
        .maxDepth = 1.0f,
    };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, scissor);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mask_pipe);

    RRPush pc = {
        .rect = { x, y, w, h },
        .color = { 1, 1, 1, 1 },
        .params = { radius, 1.25f, (float)d->offscreen_w, (float)d->offscreen_h },
    };
    vkCmdPushConstants(cmd, d->rr_layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(pc), &pc);
    vkCmdDraw(cmd, 6, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
}

static void blur_pass(VkDraw *d, VkFramebuffer fb, VkDescriptorSet src_set,
                      float dir_x, float dir_y, float sigma,
                      const VkRect2D *scissor)
{
    VkContext *ctx = d->ctx;
    VkCommandBuffer cmd = ctx->active_cmd;

    VkClearValue clear = { .color = {{ 0, 0, 0, 0 }} };
    VkRenderPassBeginInfo rp = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = d->offscreen_pass,
        .framebuffer = fb,
        .renderArea = *scissor,
        .clearValueCount = 1,
        .pClearValues = &clear,
    };
    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport = {
        .width = (float)d->offscreen_w,
        .height = (float)d->offscreen_h,
        .maxDepth = 1.0f,
    };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, d->blur_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            d->blur_layout, 0, 1, &src_set, 0, NULL);

    BlurPush bp = {
        .direction = { dir_x, dir_y },
        .sigma = sigma,
    };
    vkCmdPushConstants(cmd, d->blur_layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(bp), &bp);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
}

void vk_draw_style_box(VkDraw *d, float x, float y, float w, float h,
                       const Style *s)
{
    if (!d || !s || !d->ctx) return;

    /* Ensure offscreen matches swapchain */
    if (d->offscreen_w != d->ctx->swapchain_extent.width ||
        d->offscreen_h != d->ctx->swapchain_extent.height) {
        vk_draw_resize(d);
    }

    if ((s->shadow_blur > 0.0f || s->shadow_offset_y != 0.0f) && s->shadow_color[3] > 0) {
        float sigma = fmaxf(s->shadow_blur * 0.5f, 1.0f);
        float pad = ceilf(sigma * 3.0f + 2.0f);

        int32_t sx = (int32_t)floorf(x - pad);
        int32_t sy = (int32_t)floorf(y - pad);
        int32_t ex = (int32_t)ceilf(x + w + pad);
        int32_t ey = (int32_t)ceilf(y + h + pad);

        if (sx < 0) sx = 0;
        if (sy < 0) sy = 0;
        if (ex > (int32_t)d->offscreen_w) ex = (int32_t)d->offscreen_w;
        if (ey > (int32_t)d->offscreen_h) ey = (int32_t)d->offscreen_h;

        if (ex > sx && ey > sy) {
            VkRect2D scissor = {
                .offset = { sx, sy },
                .extent = { (uint32_t)(ex - sx), (uint32_t)(ey - sy) },
            };

            vk_context_suspend_pass(d->ctx);

            /* 1. Mask (scissored strictly to shadow bounding box) */
            draw_mask_offscreen(d, x, y, w, h, s->corner_radius, cgui_mask_pipeline, &scissor);

            /* 2. H blur: mask → temp (scissored) */
            blur_pass(d, d->temp_fb, d->blur_set_a,
                      1.0f / (float)d->offscreen_w, 0.0f, sigma, &scissor);

            /* 3. V blur: temp → mask (scissored, final blur in mask) */
            blur_pass(d, d->mask_fb, d->blur_set_b,
                      0.0f, 1.0f / (float)d->offscreen_h, sigma, &scissor);

            vk_context_resume_pass(d->ctx);

            /* 4. Composite blurred mask as shadow onto main framebuffer */
            VkCommandBuffer cmd = d->ctx->active_cmd;
            VkViewport viewport = {
                .width = (float)d->ctx->swapchain_extent.width,
                .height = (float)d->ctx->swapchain_extent.height,
                .maxDepth = 1.0f,
            };
            vkCmdSetViewport(cmd, 0, 1, &viewport);

            float dst_x = (float)sx;
            float dst_y = (float)sy + s->shadow_offset_y;
            float dst_w = (float)scissor.extent.width;
            float dst_h = (float)scissor.extent.height;

            int32_t bsx = (int32_t)floorf(dst_x);
            int32_t bsy = (int32_t)floorf(dst_y);
            int32_t bex = (int32_t)ceilf(dst_x + dst_w);
            int32_t bey = (int32_t)ceilf(dst_y + dst_h);

            if (bsx < 0) bsx = 0;
            if (bsy < 0) bsy = 0;
            if (bex > (int32_t)d->ctx->swapchain_extent.width) bex = (int32_t)d->ctx->swapchain_extent.width;
            if (bey > (int32_t)d->ctx->swapchain_extent.height) bey = (int32_t)d->ctx->swapchain_extent.height;

            if (bex > bsx && bey > bsy) {
                VkRect2D blit_scissor = {
                    .offset = { bsx, bsy },
                    .extent = { (uint32_t)(bex - bsx), (uint32_t)(bey - bsy) },
                };
                vkCmdSetScissor(cmd, 0, 1, &blit_scissor);
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, d->blit_pipeline);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        d->blit_layout, 0, 1, &d->blit_set, 0, NULL);

                BlitPush bp = {
                    .rect = { dst_x, dst_y, dst_w, dst_h },
                    .color = {
                        s->shadow_color[0] / 255.0f,
                        s->shadow_color[1] / 255.0f,
                        s->shadow_color[2] / 255.0f,
                        s->shadow_color[3] / 255.0f,
                    },
                    .uv_rect = {
                        (float)sx / (float)d->offscreen_w,
                        (float)sy / (float)d->offscreen_h,
                        (float)scissor.extent.width / (float)d->offscreen_w,
                        (float)scissor.extent.height / (float)d->offscreen_h,
                    },
                    .screen = {
                        (float)d->ctx->swapchain_extent.width,
                        (float)d->ctx->swapchain_extent.height,
                    },
                };
                vkCmdPushConstants(cmd, d->blit_layout,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0, sizeof(bp), &bp);
                vkCmdDraw(cmd, 6, 1, 0, 0);
            }
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
                         bool align_box, float box_w, float box_h)
{
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
    VkTextTexture *texture = get_text_texture(d, utf8, raster_size, style_flags);
    if (!texture) {
        return false;
    }

    if (align_box) {
        if (align == CGUI_ALIGN_CENTER) {
            x += (box_w - (float)texture->width) * 0.5f;
        } else if (align == CGUI_ALIGN_RIGHT) {
            x += (box_w - (float)texture->width);
        }
        y += (box_h - (float)texture->height) * 0.5f;
    }

    /* Snap to integer physical pixels to guarantee 1:1 texel-to-pixel mapping */
    x = floorf(x + 0.5f);
    y = floorf(y + 0.5f);

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
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, d->blit_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            d->blit_layout, 0, 1, &texture->descriptor, 0, NULL);

    BlitPush push = {
        .rect = { x, y, (float)texture->width, (float)texture->height },
        .color = {
            rgba[0] / 255.0f,
            rgba[1] / 255.0f,
            rgba[2] / 255.0f,
            rgba[3] / 255.0f,
        },
        .uv_rect = { 0.0f, 0.0f, 1.0f, 1.0f },
        .screen = {
            (float)ctx->swapchain_extent.width,
            (float)ctx->swapchain_extent.height,
        },
    };
    vkCmdPushConstants(cmd, d->blit_layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(push), &push);
    vkCmdDraw(cmd, 6, 1, 0, 0);
    return true;
}

bool vk_draw_text(VkDraw *d, const char *utf8, float x, float y,
                  float pixel_size, const uint8_t rgba[4])
{
    return draw_text_at(d, utf8, x, y, pixel_size, 0, rgba, CGUI_ALIGN_LEFT, false, 0.0f, 0.0f);
}

bool vk_draw_text_centered(VkDraw *d, const char *utf8,
                           float x, float y, float w, float h,
                           float pixel_size, const uint8_t rgba[4])
{
    return draw_text_at(d, utf8, x, y, pixel_size, 0, rgba, CGUI_ALIGN_CENTER, true, w, h);
}

bool vk_draw_text_styled(VkDraw *d, const char *utf8, float x, float y,
                         float pixel_size, uint32_t style_flags,
                         const uint8_t rgba[4])
{
    return draw_text_at(d, utf8, x, y, pixel_size, style_flags, rgba, CGUI_ALIGN_LEFT, false, 0.0f, 0.0f);
}

bool vk_draw_text_centered_styled(VkDraw *d, const char *utf8,
                                  float x, float y, float w, float h,
                                  float pixel_size, uint32_t style_flags,
                                  const uint8_t rgba[4])
{
    return draw_text_at(d, utf8, x, y, pixel_size, style_flags, rgba, CGUI_ALIGN_CENTER, true, w, h);
}

bool vk_draw_text_aligned_styled(VkDraw *d, const char *utf8,
                                 float x, float y, float w, float h,
                                 float pixel_size, uint32_t style_flags,
                                 const uint8_t rgba[4], CguiTextAlign align)
{
    return draw_text_at(d, utf8, x, y, pixel_size, style_flags, rgba, align, true, w, h);
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
    VkTextTexture *texture = get_text_texture(d, utf8, raster_size, style_flags);
    if (!texture) {
        return false;
    }
    *w_out = (float)texture->width;
    *h_out = (float)texture->height;
    return true;
}

/* Mask pipeline global set from create */
