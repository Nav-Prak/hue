// engine/render/src/descriptors.cpp
//
// Descriptor plumbing for the PBR pipeline. Set 0 is one uniform buffer of
// FrameUniforms per frame in flight (host-visible, persistently mapped:
// written once per frame before recording). Set 1 is a pair of combined
// image samplers per material, allocated at mesh upload from a fixed pool
// that lives until shutdown -- meshes are never destroyed individually, so
// sets are never freed one by one.

#include "vk_types.h"

namespace hue::render {

Result<void> descriptors_create(DescriptorState& descriptors, const ContextState& context) {
    // ---- set 0 layout: frame UBO
    VkDescriptorSetLayoutBinding ubo_binding{};
    ubo_binding.binding = 0;
    ubo_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ubo_binding.descriptorCount = 1;
    ubo_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo frame_layout_info{};
    frame_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    frame_layout_info.bindingCount = 1;
    frame_layout_info.pBindings = &ubo_binding;
    HUE_VK_TRY(vkCreateDescriptorSetLayout(context.device, &frame_layout_info, nullptr,
                                           &descriptors.frame_layout));

    // ---- set 1 layout: base color + metallic-roughness textures
    VkDescriptorSetLayoutBinding texture_bindings[2]{};
    for (std::uint32_t i = 0; i < 2; ++i) {
        texture_bindings[i].binding = i;
        texture_bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        texture_bindings[i].descriptorCount = 1;
        texture_bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    VkDescriptorSetLayoutCreateInfo material_layout_info{};
    material_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    material_layout_info.bindingCount = 2;
    material_layout_info.pBindings = texture_bindings;
    HUE_VK_TRY(vkCreateDescriptorSetLayout(context.device, &material_layout_info, nullptr,
                                           &descriptors.material_layout));

    // ---- pool: frame sets + up to kMaxMaterialSets material sets
    VkDescriptorPoolSize pool_sizes[2]{};
    pool_sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    pool_sizes[0].descriptorCount = kFramesInFlight;
    pool_sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_sizes[1].descriptorCount = kMaxMaterialSets * 2;

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.maxSets = kFramesInFlight + kMaxMaterialSets;
    pool_info.poolSizeCount = 2;
    pool_info.pPoolSizes = pool_sizes;
    HUE_VK_TRY(vkCreateDescriptorPool(context.device, &pool_info, nullptr, &descriptors.pool));

    // ---- frame UBOs + their sets
    for (std::uint32_t frame = 0; frame < kFramesInFlight; ++frame) {
        VkBufferCreateInfo buffer_info{};
        buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_info.size = sizeof(FrameUniforms);
        buffer_info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo alloc_info{};
        alloc_info.usage = VMA_MEMORY_USAGE_AUTO;
        alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                           VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo mapped{};
        HUE_VK_TRY(vmaCreateBuffer(context.allocator, &buffer_info, &alloc_info,
                                   &descriptors.frame_ubos[frame].buffer,
                                   &descriptors.frame_ubos[frame].allocation, &mapped));
        descriptors.frame_ubo_mapped[frame] = mapped.pMappedData;

        VkDescriptorSetAllocateInfo set_info{};
        set_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        set_info.descriptorPool = descriptors.pool;
        set_info.descriptorSetCount = 1;
        set_info.pSetLayouts = &descriptors.frame_layout;
        HUE_VK_TRY(vkAllocateDescriptorSets(context.device, &set_info,
                                            &descriptors.frame_sets[frame]));

        VkDescriptorBufferInfo buffer_write{};
        buffer_write.buffer = descriptors.frame_ubos[frame].buffer;
        buffer_write.range = sizeof(FrameUniforms);

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = descriptors.frame_sets[frame];
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.pBufferInfo = &buffer_write;
        vkUpdateDescriptorSets(context.device, 1, &write, 0, nullptr);
    }
    return {};
}

void descriptors_destroy(DescriptorState& descriptors, const ContextState& context) {
    for (std::uint32_t frame = 0; frame < kFramesInFlight; ++frame) {
        buffer_destroy(descriptors.frame_ubos[frame], context);
        descriptors.frame_ubo_mapped[frame] = nullptr;
        descriptors.frame_sets[frame] = VK_NULL_HANDLE;
    }
    if (descriptors.pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(context.device, descriptors.pool, nullptr);
        descriptors.pool = VK_NULL_HANDLE;
    }
    if (descriptors.material_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(context.device, descriptors.material_layout, nullptr);
        descriptors.material_layout = VK_NULL_HANDLE;
    }
    if (descriptors.frame_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(context.device, descriptors.frame_layout, nullptr);
        descriptors.frame_layout = VK_NULL_HANDLE;
    }
}

Result<VkDescriptorSet> material_set_allocate(DescriptorState& descriptors,
                                              const ContextState& context, VkImageView base_color,
                                              VkImageView metallic_roughness, VkSampler sampler) {
    VkDescriptorSetAllocateInfo set_info{};
    set_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    set_info.descriptorPool = descriptors.pool;
    set_info.descriptorSetCount = 1;
    set_info.pSetLayouts = &descriptors.material_layout;

    VkDescriptorSet set = VK_NULL_HANDLE;
    const VkResult allocated = vkAllocateDescriptorSets(context.device, &set_info, &set);
    if (allocated != VK_SUCCESS) {
        HUE_LOG_ERROR("material descriptor pool exhausted (VkResult %d)",
                      static_cast<int>(allocated));
        return ErrorCode::kOutOfMemory;
    }

    VkDescriptorImageInfo images[2]{};
    images[0].sampler = sampler;
    images[0].imageView = base_color;
    images[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    images[1].sampler = sampler;
    images[1].imageView = metallic_roughness;
    images[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet writes[2]{};
    for (std::uint32_t i = 0; i < 2; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = set;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo = &images[i];
    }
    vkUpdateDescriptorSets(context.device, 2, writes, 0, nullptr);
    return set;
}

} // namespace hue::render
