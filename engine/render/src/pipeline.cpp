// engine/render/src/pipeline.cpp
//
// SPIR-V loading + graphics pipelines (dynamic rendering, no render pass
// objects). Two pipelines: the Week 4 triangle (kept for empty frames and
// the reload demo) and the Week 5 static-mesh pipeline with vertex input,
// depth test, and push-constant MVP. Shader files are untrusted at reload
// time, so every load goes through validate_spirv_bytes before the driver.

#include "vk_types.h"

#include "hue/core/memory.h"
#include "hue/render/spirv.h"

#include <cstddef>
#include <cstdio>

namespace hue::render {

namespace {

struct SpirvBlob {
    std::uint32_t* words = nullptr;
    std::size_t byte_size = 0;
};

void blob_release(SpirvBlob& blob) {
    if (blob.words != nullptr) {
        const auto freed = heap_free(blob.words);
        (void)freed;
        blob.words = nullptr;
        blob.byte_size = 0;
    }
}

[[nodiscard]] Result<void> blob_load(SpirvBlob& blob, const char* path) {
    std::FILE* file = std::fopen(path, "rb");
    if (file == nullptr) {
        HUE_LOG_ERROR("shader file not found: %s", path);
        return ErrorCode::kNotFound;
    }

    std::fseek(file, 0, SEEK_END);
    const long file_size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (file_size <= 0 || static_cast<std::size_t>(file_size) > kSpirvMaxBytes) {
        std::fclose(file);
        HUE_LOG_ERROR("shader file has unreasonable size (%ld): %s", file_size, path);
        return ErrorCode::kCorruptData;
    }

    const std::size_t byte_size = static_cast<std::size_t>(file_size);
    auto allocation = heap_allocate(byte_size, alignof(std::uint32_t), MemoryTag::kRender);
    if (!allocation) {
        std::fclose(file);
        return allocation.error();
    }
    blob.words = static_cast<std::uint32_t*>(allocation.value());
    blob.byte_size = byte_size;

    const std::size_t read = std::fread(blob.words, 1, byte_size, file);
    std::fclose(file);
    if (read != byte_size) {
        blob_release(blob);
        HUE_LOG_ERROR("short read on shader file: %s", path);
        return ErrorCode::kCorruptData;
    }

    const auto valid = validate_spirv_bytes(blob.words, blob.byte_size);
    if (!valid) {
        blob_release(blob);
        HUE_LOG_ERROR("shader failed SPIR-V validation: %s", path);
        return valid.error();
    }
    return {};
}

[[nodiscard]] Result<VkShaderModule> shader_module_create(const ContextState& context,
                                                          const char* directory,
                                                          const char* file_name) {
    char path[512];
    std::snprintf(path, sizeof(path), "%s/%s", directory, file_name);

    SpirvBlob blob;
    const auto loaded = blob_load(blob, path);
    if (!loaded) {
        return loaded.error();
    }

    VkShaderModuleCreateInfo module_info{};
    module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    module_info.codeSize = blob.byte_size;
    module_info.pCode = blob.words;

    VkShaderModule module = VK_NULL_HANDLE;
    const VkResult result = vkCreateShaderModule(context.device, &module_info, nullptr, &module);
    blob_release(blob);
    if (result != VK_SUCCESS) {
        HUE_LOG_ERROR("vkCreateShaderModule failed (VkResult %d) for %s",
                      static_cast<int>(result), path);
        return ErrorCode::kUnknown;
    }
    return module;
}

// Shared fixed-function blocks. Both pipelines render Y-up through a
// negative viewport, so front faces stay counter-clockwise.
struct FixedFunction {
    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    VkPipelineViewportStateCreateInfo viewport{};
    VkPipelineRasterizationStateCreateInfo rasterization{};
    VkPipelineMultisampleStateCreateInfo multisample{};
    VkPipelineColorBlendAttachmentState blend_attachment{};
    VkPipelineColorBlendStateCreateInfo blend{};
    VkDynamicState dynamic_states[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};

    FixedFunction() {
        input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport.viewportCount = 1;
        viewport.scissorCount = 1;
        rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterization.polygonMode = VK_POLYGON_MODE_FILL;
        rasterization.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterization.lineWidth = 1.0f;
        multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        blend.attachmentCount = 1;
        blend.pAttachments = &blend_attachment;
        dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = dynamic_states;
    }
};

[[nodiscard]] Result<VkPipeline>
graphics_pipeline_create(const ContextState& context, const char* shader_directory,
                         const char* vert_name, const char* frag_name, VkPipelineLayout layout,
                         VkFormat color_format, VkFormat depth_format,
                         const VkPipelineVertexInputStateCreateInfo& vertex_input,
                         bool depth_test, bool alpha_blend = false, bool cull_none = false) {
    auto vertex_module = shader_module_create(context, shader_directory, vert_name);
    if (!vertex_module) {
        return vertex_module.error();
    }
    auto fragment_module = shader_module_create(context, shader_directory, frag_name);
    if (!fragment_module) {
        vkDestroyShaderModule(context.device, vertex_module.value(), nullptr);
        return fragment_module.error();
    }

    struct ModuleGuard {
        VkDevice device;
        VkShaderModule vertex;
        VkShaderModule fragment;
        ~ModuleGuard() {
            vkDestroyShaderModule(device, vertex, nullptr);
            vkDestroyShaderModule(device, fragment, nullptr);
        }
    } guard{context.device, vertex_module.value(), fragment_module.value()};

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = guard.vertex;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = guard.fragment;
    stages[1].pName = "main";

    FixedFunction fixed;
    if (cull_none) {
        fixed.rasterization.cullMode = VK_CULL_MODE_NONE;
    }
    if (alpha_blend) {
        fixed.blend_attachment.blendEnable = VK_TRUE;
        fixed.blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        fixed.blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        fixed.blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
        fixed.blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        fixed.blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        fixed.blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    }

    VkPipelineDepthStencilStateCreateInfo depth_stencil{};
    depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil.depthTestEnable = depth_test ? VK_TRUE : VK_FALSE;
    depth_stencil.depthWriteEnable = depth_test ? VK_TRUE : VK_FALSE;
    depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineRenderingCreateInfo rendering_info{};
    rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering_info.colorAttachmentCount = 1;
    rendering_info.pColorAttachmentFormats = &color_format;
    rendering_info.depthAttachmentFormat = depth_format;

    VkGraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.pNext = &rendering_info;
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = stages;
    pipeline_info.pVertexInputState = &vertex_input;
    pipeline_info.pInputAssemblyState = &fixed.input_assembly;
    pipeline_info.pViewportState = &fixed.viewport;
    pipeline_info.pRasterizationState = &fixed.rasterization;
    pipeline_info.pMultisampleState = &fixed.multisample;
    pipeline_info.pDepthStencilState = &depth_stencil;
    pipeline_info.pColorBlendState = &fixed.blend;
    pipeline_info.pDynamicState = &fixed.dynamic;
    pipeline_info.layout = layout;

    VkPipeline pipeline = VK_NULL_HANDLE;
    const VkResult result = vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1,
                                                      &pipeline_info, nullptr, &pipeline);
    if (result != VK_SUCCESS) {
        HUE_LOG_ERROR("vkCreateGraphicsPipelines failed (VkResult %d) for %s",
                      static_cast<int>(result), vert_name);
        return ErrorCode::kUnknown;
    }
    return pipeline;
}

} // namespace

Result<void> pipeline_create(PipelineState& pipeline, const ContextState& context,
                             VkFormat color_format, VkFormat depth_format,
                             const char* shader_directory,
                             const DescriptorState& descriptors) {
    // ---- layouts (created once, survive shader reloads)
    if (pipeline.triangle_layout == VK_NULL_HANDLE) {
        VkPipelineLayoutCreateInfo layout_info{};
        layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        HUE_VK_TRY(vkCreatePipelineLayout(context.device, &layout_info, nullptr,
                                          &pipeline.triangle_layout));
    }
    if (pipeline.mesh_layout == VK_NULL_HANDLE) {
        // Set 0: frame UBO. Set 1: material textures. Push constants carry
        // the model matrix (vertex) and material factors (fragment).
        VkDescriptorSetLayout set_layouts[3] = {descriptors.frame_layout,
                                                descriptors.material_layout,
                                                descriptors.skin_layout};

        VkPushConstantRange push_range{};
        push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        push_range.size = sizeof(MeshPushConstants);

        VkPipelineLayoutCreateInfo layout_info{};
        layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_info.setLayoutCount = 3;
        layout_info.pSetLayouts = set_layouts;
        layout_info.pushConstantRangeCount = 1;
        layout_info.pPushConstantRanges = &push_range;
        HUE_VK_TRY(vkCreatePipelineLayout(context.device, &layout_info, nullptr,
                                          &pipeline.mesh_layout));
    }

    // ---- triangle: no vertex input, depth test off (always on top)
    VkPipelineVertexInputStateCreateInfo empty_input{};
    empty_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    auto triangle = graphics_pipeline_create(context, shader_directory, "triangle.vert.spv",
                                             "triangle.frag.spv", pipeline.triangle_layout,
                                             color_format, depth_format, empty_input, false);
    if (!triangle) {
        return triangle.error();
    }

    // ---- mesh: StaticVertex input, depth tested
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(asset::StaticVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attributes[3]{};
    attributes[0].location = 0;
    attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[0].offset = offsetof(asset::StaticVertex, position);
    attributes[1].location = 1;
    attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[1].offset = offsetof(asset::StaticVertex, normal);
    attributes[2].location = 2;
    attributes[2].format = VK_FORMAT_R32G32_SFLOAT;
    attributes[2].offset = offsetof(asset::StaticVertex, uv);

    VkPipelineVertexInputStateCreateInfo mesh_input{};
    mesh_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    mesh_input.vertexBindingDescriptionCount = 1;
    mesh_input.pVertexBindingDescriptions = &binding;
    mesh_input.vertexAttributeDescriptionCount = 3;
    mesh_input.pVertexAttributeDescriptions = attributes;

    auto mesh = graphics_pipeline_create(context, shader_directory, "mesh.vert.spv",
                                         "mesh.frag.spv", pipeline.mesh_layout, color_format,
                                         depth_format, mesh_input, true);
    if (!mesh) {
        vkDestroyPipeline(context.device, triangle.value(), nullptr);
        return mesh.error();
    }

    VkVertexInputBindingDescription skinned_binding{};
    skinned_binding.binding = 0;
    skinned_binding.stride = sizeof(asset::SkinnedVertex);
    skinned_binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription skinned_attributes[5]{};
    skinned_attributes[0] = attributes[0];
    skinned_attributes[0].offset = offsetof(asset::SkinnedVertex, position);
    skinned_attributes[1] = attributes[1];
    skinned_attributes[1].offset = offsetof(asset::SkinnedVertex, normal);
    skinned_attributes[2] = attributes[2];
    skinned_attributes[2].offset = offsetof(asset::SkinnedVertex, uv);
    skinned_attributes[3].location = 3;
    skinned_attributes[3].format = VK_FORMAT_R16G16B16A16_UINT;
    skinned_attributes[3].offset = offsetof(asset::SkinnedVertex, joints);
    skinned_attributes[4].location = 4;
    skinned_attributes[4].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    skinned_attributes[4].offset = offsetof(asset::SkinnedVertex, weights);
    VkPipelineVertexInputStateCreateInfo skinned_input{};
    skinned_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    skinned_input.vertexBindingDescriptionCount = 1;
    skinned_input.pVertexBindingDescriptions = &skinned_binding;
    skinned_input.vertexAttributeDescriptionCount = 5;
    skinned_input.pVertexAttributeDescriptions = skinned_attributes;
    auto skinned = graphics_pipeline_create(context, shader_directory, "skinned_mesh.vert.spv",
                                            "mesh.frag.spv", pipeline.mesh_layout, color_format,
                                            depth_format, skinned_input, true);
    if (!skinned) {
        vkDestroyPipeline(context.device, triangle.value(), nullptr);
        vkDestroyPipeline(context.device, mesh.value(), nullptr);
        return skinned.error();
    }

    if (pipeline.hud_layout == VK_NULL_HANDLE) {
        VkPushConstantRange push_range{};
        push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        push_range.size = sizeof(HudPushConstants);
        VkPipelineLayoutCreateInfo layout_info{};
        layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_info.pushConstantRangeCount = 1;
        layout_info.pPushConstantRanges = &push_range;
        HUE_VK_TRY(vkCreatePipelineLayout(context.device, &layout_info, nullptr,
                                          &pipeline.hud_layout));
    }

    auto hud = graphics_pipeline_create(context, shader_directory, "hud.vert.spv",
                                        "hud.frag.spv", pipeline.hud_layout, color_format,
                                        depth_format, empty_input, false, true, true);
    if (!hud) {
        vkDestroyPipeline(context.device, triangle.value(), nullptr);
        vkDestroyPipeline(context.device, mesh.value(), nullptr);
        vkDestroyPipeline(context.device, skinned.value(), nullptr);
        return hud.error();
    }

    pipeline.triangle = triangle.value();
    pipeline.mesh = mesh.value();
    pipeline.skinned_mesh = skinned.value();
    pipeline.hud = hud.value();
    return {};
}

void pipeline_destroy(PipelineState& pipeline, const ContextState& context) {
    if (pipeline.triangle != VK_NULL_HANDLE) {
        vkDestroyPipeline(context.device, pipeline.triangle, nullptr);
        pipeline.triangle = VK_NULL_HANDLE;
    }
    if (pipeline.mesh != VK_NULL_HANDLE) {
        vkDestroyPipeline(context.device, pipeline.mesh, nullptr);
        pipeline.mesh = VK_NULL_HANDLE;
    }
    if (pipeline.skinned_mesh != VK_NULL_HANDLE) {
        vkDestroyPipeline(context.device, pipeline.skinned_mesh, nullptr);
        pipeline.skinned_mesh = VK_NULL_HANDLE;
    }
    if (pipeline.hud != VK_NULL_HANDLE) {
        vkDestroyPipeline(context.device, pipeline.hud, nullptr);
        pipeline.hud = VK_NULL_HANDLE;
    }
    if (pipeline.triangle_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(context.device, pipeline.triangle_layout, nullptr);
        pipeline.triangle_layout = VK_NULL_HANDLE;
    }
    if (pipeline.mesh_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(context.device, pipeline.mesh_layout, nullptr);
        pipeline.mesh_layout = VK_NULL_HANDLE;
    }
    if (pipeline.hud_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(context.device, pipeline.hud_layout, nullptr);
        pipeline.hud_layout = VK_NULL_HANDLE;
    }
}

} // namespace hue::render
