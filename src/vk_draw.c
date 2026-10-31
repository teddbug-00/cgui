/**
 * vk_draw.c – SDF body + real separable Gaussian soft shadows
 */

#include "vk_draw.h"
#include "text.h"
#include "glyph_atlas.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

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

/* Text atlas push constants (must match text.vert / text.frag) */
typedef struct TextPush {
    float rect[4];     /* (x, y, w, h) in screen pixels */
    float color[4];    /* RGBA [0,1] */
    float uv_rect[4];  /* (u0, v0, u_w, v_h) into atlas */
    float screen[2];   /* (screen_w, screen_h) */
} TextPush;

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
        .descriptorCount = 3u,
    };
    VkDescriptorPoolCreateInfo dpci = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 3u,
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
        /* Reuse blit_set_layout for the atlas (same binding: combined-image-sampler at 0) */
        out->txt_set_layout = out->blit_set_layout;
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

    /* Text push-constant layout (rect, color, uv_rect, screen) */
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
        VkPipelineVertexInputStateCreateInfo txt_vi = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
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

    printf("UI draw ready (SDF body + Gaussian shadow + GPU glyph atlas)\n");
    return true;
}

void vk_draw_destroy(VkDraw *d)
{
    if (!d || !d->ctx) return;
    VkDevice dev = d->ctx->device;
    if (!dev) { memset(d, 0, sizeof(*d)); return; }

    vkDeviceWaitIdle(dev);
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

    if (d->glyph_atlas) {
        cgui_atlas_destroy((CguiGlyphAtlas *)d->glyph_atlas);
        d->glyph_atlas = NULL;
    }
    if (d->txt_pipeline) vkDestroyPipeline(dev, d->txt_pipeline, NULL);
    if (d->txt_layout) vkDestroyPipelineLayout(dev, d->txt_layout, NULL);
    if (d->txt_pool) vkDestroyDescriptorPool(dev, d->txt_pool, NULL);
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

    CguiGlyphAtlas *atlas = (CguiGlyphAtlas *)d->glyph_atlas;
    if (!atlas) {
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

    /* If new glyphs were rasterized into atlas, flush to GPU */
    if (d->ctx->frame_open) {
        vk_context_suspend_pass(d->ctx);
    }
    cgui_atlas_flush(atlas);
    if (!d->ctx->frame_open) {
        vk_context_resume_pass(d->ctx);
    }

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

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, d->txt_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            d->txt_layout, 0, 1, &d->txt_set, 0, NULL);

    float screen_w = (float)ctx->swapchain_extent.width;
    float screen_h = (float)ctx->swapchain_extent.height;
    float cr = rgba[0] / 255.0f;
    float cg = rgba[1] / 255.0f;
    float cb = rgba[2] / 255.0f;
    float ca = rgba[3] / 255.0f;

    for (uint32_t i = 0; i < count; ++i) {
        const CguiGlyphQuad *q = &quads[i];
        TextPush push = {
            .rect = {
                floorf(base_x + q->x + 0.5f),
                floorf(base_y + q->y + 0.5f),
                q->w,
                q->h,
            },
            .color = { cr, cg, cb, ca },
            .uv_rect = {
                q->u0,
                q->v0,
                q->u1 - q->u0,
                q->v1 - q->v0,
            },
            .screen = { screen_w, screen_h },
        };
        vkCmdPushConstants(cmd, d->txt_layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(push), &push);
        vkCmdDraw(cmd, 6, 1, 0, 0);
    }

    if (quads != stack_quads) {
        free(quads);
    }
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
    CguiGlyphAtlas *atlas = (CguiGlyphAtlas *)d->glyph_atlas;
    if (!atlas) {
        return false;
    }
    return cgui_atlas_shape_quads(atlas, utf8, 0.0f, 0.0f, raster_size, style_flags,
                                  NULL, 0, NULL, w_out, h_out);
}

/* Mask pipeline global set from create */
