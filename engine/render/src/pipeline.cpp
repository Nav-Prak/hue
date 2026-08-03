// engine/render/src/pipeline.cpp
//
// SPIR-V loading + the triangle graphics pipeline (dynamic rendering, no
// render pass objects). Shader files are untrusted at reload time, so
// every load goes through validate_spirv_bytes before reaching the driver.

#include "vk_types.h"

#include "hue/core/memory.h"
#include "hue/render/spirv.h"

#include <cstdio>
#include <cstring>

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

} // namespace

Result<void> pipeline_create(PipelineState& pipeline, const ContextState& context,
                             VkFormat color_format, const char* shader_directory) {
    auto vertex_module = shader_module_create(context, shader_directory, "triangle.vert.spv");
    if (!vertex_module) {
        return vertex_module.error();
    }
    auto fragment_module = shader_module_create(context, shader_directory, "triangle.frag.spv");
    if (!fragment_module) {
        vkDestroyShaderModule(context.device, vertex_module.value(), nullptr);
        return fragment_module.error();
    }

    // Cleanup that runs on every exit path below.
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

    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterization{};
    rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterization.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterization.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blend_attachment{};
    blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &blend_attachment;

    const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                             VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = 2;
    dynamic_state.pDynamicStates = dynamic_states;

    if (pipeline.layout == VK_NULL_HANDLE) {
        VkPipelineLayoutCreateInfo layout_info{};
        layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        HUE_VK_TRY(vkCreatePipelineLayout(context.device, &layout_info, nullptr,
                                          &pipeline.layout));
    }

    VkPipelineRenderingCreateInfo rendering_info{};
    rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering_info.colorAttachmentCount = 1;
    rendering_info.pColorAttachmentFormats = &color_format;

    VkGraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.pNext = &rendering_info;
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = stages;
    pipeline_info.pVertexInputState = &vertex_input;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterization;
    pipeline_info.pMultisampleState = &multisample;
    pipeline_info.pColorBlendState = &blend;
    pipeline_info.pDynamicState = &dynamic_state;
    pipeline_info.layout = pipeline.layout;

    HUE_VK_TRY(vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &pipeline_info,
                                         nullptr, &pipeline.pipeline));
    return {};
}

void pipeline_destroy(PipelineState& pipeline, const ContextState& context) {
    if (pipeline.pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(context.device, pipeline.pipeline, nullptr);
        pipeline.pipeline = VK_NULL_HANDLE;
    }
    if (pipeline.layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(context.device, pipeline.layout, nullptr);
        pipeline.layout = VK_NULL_HANDLE;
    }
}

} // namespace hue::render
