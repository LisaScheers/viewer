/**
 * @file llrendervulkantextureuploadsamplepipeline.cpp
 * @brief Loader-neutral ownership of one sampled streamed-texture pipeline.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2026, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the license only.
 * $/LicenseInfo$
 */

#include "llrendervulkantextureuploadsamplepipeline.h"

#include <array>
#include <utility>

namespace LLRenderVulkan
{
namespace
{
    // Generated from shaders/textureupload.vert.glsl with glslang 16.4.0:
    // glslangValidator -V --target-env vulkan1.1 -S vert
    // 1112 bytes; SHA-256 139f3d06e998cdd95ad6ae751dd97cf7ecaeb9c210efca379a7b1ee73270789c.
    constexpr std::array<std::uint32_t, 278> TEXTURE_UPLOAD_SAMPLE_VERTEX_SHADER{
        0x07230203, 0x00010300, 0x0008000b, 0x00000024, 0x00000000, 0x00020011, 0x00000001, 0x0006000b, 0x00000001, 0x4c534c47, 0x6474732e,
        0x3035342e, 0x00000000, 0x0003000e, 0x00000000, 0x00000001, 0x0008000f, 0x00000000, 0x00000004, 0x6e69616d, 0x00000000, 0x00000009,
        0x0000000c, 0x00000019, 0x00030003, 0x00000002, 0x000001c2, 0x000a0004, 0x475f4c47, 0x4c474f4f, 0x70635f45, 0x74735f70, 0x5f656c79,
        0x656e696c, 0x7269645f, 0x69746365, 0x00006576, 0x00080004, 0x475f4c47, 0x4c474f4f, 0x6e695f45, 0x64756c63, 0x69645f65, 0x74636572,
        0x00657669, 0x00040005, 0x00000004, 0x6e69616d, 0x00000000, 0x00030005, 0x00000009, 0x00006374, 0x00050005, 0x0000000c, 0x69736f70,
        0x6e6f6974, 0x00000000, 0x00060005, 0x00000017, 0x505f6c67, 0x65567265, 0x78657472, 0x00000000, 0x00060006, 0x00000017, 0x00000000,
        0x505f6c67, 0x7469736f, 0x006e6f69, 0x00070006, 0x00000017, 0x00000001, 0x505f6c67, 0x746e696f, 0x657a6953, 0x00000000, 0x00070006,
        0x00000017, 0x00000002, 0x435f6c67, 0x4470696c, 0x61747369, 0x0065636e, 0x00070006, 0x00000017, 0x00000003, 0x435f6c67, 0x446c6c75,
        0x61747369, 0x0065636e, 0x00030005, 0x00000019, 0x00000000, 0x00040047, 0x00000009, 0x0000001e, 0x00000000, 0x00040047, 0x0000000c,
        0x0000001e, 0x00000000, 0x00030047, 0x00000017, 0x00000002, 0x00050048, 0x00000017, 0x00000000, 0x0000000b, 0x00000000, 0x00050048,
        0x00000017, 0x00000001, 0x0000000b, 0x00000001, 0x00050048, 0x00000017, 0x00000002, 0x0000000b, 0x00000003, 0x00050048, 0x00000017,
        0x00000003, 0x0000000b, 0x00000004, 0x00020013, 0x00000002, 0x00030021, 0x00000003, 0x00000002, 0x00030016, 0x00000006, 0x00000020,
        0x00040017, 0x00000007, 0x00000006, 0x00000002, 0x00040020, 0x00000008, 0x00000003, 0x00000007, 0x0004003b, 0x00000008, 0x00000009,
        0x00000003, 0x00040017, 0x0000000a, 0x00000006, 0x00000003, 0x00040020, 0x0000000b, 0x00000001, 0x0000000a, 0x0004003b, 0x0000000b,
        0x0000000c, 0x00000001, 0x0004002b, 0x00000006, 0x0000000f, 0x3f000000, 0x00040017, 0x00000013, 0x00000006, 0x00000004, 0x00040015,
        0x00000014, 0x00000020, 0x00000000, 0x0004002b, 0x00000014, 0x00000015, 0x00000001, 0x0004001c, 0x00000016, 0x00000006, 0x00000015,
        0x0006001e, 0x00000017, 0x00000013, 0x00000006, 0x00000016, 0x00000016, 0x00040020, 0x00000018, 0x00000003, 0x00000017, 0x0004003b,
        0x00000018, 0x00000019, 0x00000003, 0x00040015, 0x0000001a, 0x00000020, 0x00000001, 0x0004002b, 0x0000001a, 0x0000001b, 0x00000000,
        0x0004002b, 0x00000006, 0x0000001d, 0x3f800000, 0x00040020, 0x00000022, 0x00000003, 0x00000013, 0x00050036, 0x00000002, 0x00000004,
        0x00000000, 0x00000003, 0x000200f8, 0x00000005, 0x0004003d, 0x0000000a, 0x0000000d, 0x0000000c, 0x0007004f, 0x00000007, 0x0000000e,
        0x0000000d, 0x0000000d, 0x00000000, 0x00000001, 0x0005008e, 0x00000007, 0x00000010, 0x0000000e, 0x0000000f, 0x00050050, 0x00000007,
        0x00000011, 0x0000000f, 0x0000000f, 0x00050081, 0x00000007, 0x00000012, 0x00000010, 0x00000011, 0x0003003e, 0x00000009, 0x00000012,
        0x0004003d, 0x0000000a, 0x0000001c, 0x0000000c, 0x00050051, 0x00000006, 0x0000001e, 0x0000001c, 0x00000000, 0x00050051, 0x00000006,
        0x0000001f, 0x0000001c, 0x00000001, 0x00050051, 0x00000006, 0x00000020, 0x0000001c, 0x00000002, 0x00070050, 0x00000013, 0x00000021,
        0x0000001e, 0x0000001f, 0x00000020, 0x0000001d, 0x00050041, 0x00000022, 0x00000023, 0x00000019, 0x0000001b, 0x0003003e, 0x00000023,
        0x00000021, 0x000100fd, 0x00010038
    };

    // Generated from shaders/textureupload.frag.glsl with glslang 16.4.0:
    // glslangValidator -V --target-env vulkan1.1 -S frag
    // 628 bytes; SHA-256 2d07ec80932a25934493be1d4f8bdfb3ca3d2bac0cf3ffa9cbb7d7520bdaafb1.
    constexpr std::array<std::uint32_t, 157> TEXTURE_UPLOAD_SAMPLE_FRAGMENT_SHADER{
        0x07230203, 0x00010300, 0x0008000b, 0x00000014, 0x00000000, 0x00020011, 0x00000001, 0x0006000b, 0x00000001, 0x4c534c47, 0x6474732e,
        0x3035342e, 0x00000000, 0x0003000e, 0x00000000, 0x00000001, 0x0007000f, 0x00000004, 0x00000004, 0x6e69616d, 0x00000000, 0x00000009,
        0x00000011, 0x00030010, 0x00000004, 0x00000007, 0x00030003, 0x00000002, 0x000001c2, 0x000a0004, 0x475f4c47, 0x4c474f4f, 0x70635f45,
        0x74735f70, 0x5f656c79, 0x656e696c, 0x7269645f, 0x69746365, 0x00006576, 0x00080004, 0x475f4c47, 0x4c474f4f, 0x6e695f45, 0x64756c63,
        0x69645f65, 0x74636572, 0x00657669, 0x00040005, 0x00000004, 0x6e69616d, 0x00000000, 0x00050005, 0x00000009, 0x67617266, 0x6c6f635f,
        0x0000726f, 0x00050005, 0x0000000d, 0x66666964, 0x4d657375, 0x00007061, 0x00030005, 0x00000011, 0x00006374, 0x00040047, 0x00000009,
        0x0000001e, 0x00000000, 0x00040047, 0x0000000d, 0x00000021, 0x00000000, 0x00040047, 0x0000000d, 0x00000022, 0x00000000, 0x00040047,
        0x00000011, 0x0000001e, 0x00000000, 0x00020013, 0x00000002, 0x00030021, 0x00000003, 0x00000002, 0x00030016, 0x00000006, 0x00000020,
        0x00040017, 0x00000007, 0x00000006, 0x00000004, 0x00040020, 0x00000008, 0x00000003, 0x00000007, 0x0004003b, 0x00000008, 0x00000009,
        0x00000003, 0x00090019, 0x0000000a, 0x00000006, 0x00000001, 0x00000000, 0x00000000, 0x00000000, 0x00000001, 0x00000000, 0x0003001b,
        0x0000000b, 0x0000000a, 0x00040020, 0x0000000c, 0x00000000, 0x0000000b, 0x0004003b, 0x0000000c, 0x0000000d, 0x00000000, 0x00040017,
        0x0000000f, 0x00000006, 0x00000002, 0x00040020, 0x00000010, 0x00000001, 0x0000000f, 0x0004003b, 0x00000010, 0x00000011, 0x00000001,
        0x00050036, 0x00000002, 0x00000004, 0x00000000, 0x00000003, 0x000200f8, 0x00000005, 0x0004003d, 0x0000000b, 0x0000000e, 0x0000000d,
        0x0004003d, 0x0000000f, 0x00000012, 0x00000011, 0x00050057, 0x00000007, 0x00000013, 0x0000000e, 0x00000012, 0x0003003e, 0x00000009,
        0x00000013, 0x000100fd, 0x00010038
    };

    static_assert(TEXTURE_UPLOAD_SAMPLE_VERTEX_SHADER.size() * sizeof(std::uint32_t) == 1112);
    static_assert(TEXTURE_UPLOAD_SAMPLE_FRAGMENT_SHADER.size() * sizeof(std::uint32_t) == 628);
    static_assert(TEXTURE_UPLOAD_SAMPLE_VERTEX_SHADER.front() == 0x07230203);
    static_assert(TEXTURE_UPLOAD_SAMPLE_FRAGMENT_SHADER.front() == 0x07230203);

    struct Dispatch
    {
        PFN_vkGetDeviceProcAddr       mGetDeviceProcAddr       = nullptr;
        PFN_vkCreateShaderModule      mCreateShaderModule      = nullptr;
        PFN_vkDestroyShaderModule     mDestroyShaderModule     = nullptr;
        PFN_vkCreateGraphicsPipelines mCreateGraphicsPipelines = nullptr;
        PFN_vkDestroyPipeline         mDestroyPipeline         = nullptr;
    };

    template<typename Function>
    Function resolveInstance(PFN_vkGetInstanceProcAddr resolver, VkInstance instance, const char* name) noexcept
    {
        return reinterpret_cast<Function>(resolver(instance, name));
    }

    template<typename Function>
    Function resolveDevice(PFN_vkGetDeviceProcAddr resolver, VkDevice device, const char* name) noexcept
    {
        return reinterpret_cast<Function>(resolver(device, name));
    }

    VulkanTextureUploadSamplePipelineResolutionError failure(VulkanTextureUploadSamplePipelineResolutionCode         code,
                                                             std::optional<VulkanTextureUploadSamplePipelineCommand> command = std::nullopt,
                                                             VkResult result = VK_SUCCESS) noexcept
    {
        return { code, command, result };
    }

    bool validPhysical(const VulkanPhysicalDeviceGeneration& physical) noexcept
    {
        const VkQueueFamilyProperties& queue_family = physical.queueFamilyProperties();
        return physical.getInstanceProcAddr() != nullptr && physical.instance() != VK_NULL_HANDLE && physical.surface() != VK_NULL_HANDLE &&
               physical.physicalDevice() != VK_NULL_HANDLE && physical.queueFamilyIndex() != VK_QUEUE_FAMILY_IGNORED &&
               queue_family.queueCount != 0 && (queue_family.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
    }

    bool validLogical(const VulkanPhysicalDeviceGeneration& physical, const VulkanLogicalDeviceGeneration& logical) noexcept
    {
        const VkQueueFamilyProperties& queue_family = physical.queueFamilyProperties();
        return logical.createdFor(physical) && logical.device() != VK_NULL_HANDLE && logical.queue() != VK_NULL_HANDLE &&
               logical.queueFamilyIndex() == physical.queueFamilyIndex() && queue_family.queueCount > logical.queueIndex() &&
               (queue_family.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
    }

    bool validDestination(const VulkanPhysicalDeviceGeneration&            physical,
                          const VulkanLogicalDeviceGeneration&             logical,
                          const VulkanTextureUploadDestinationDescription& description,
                          const VulkanTextureUploadDestinationGeneration&  destination) noexcept
    {
        const VkImageSubresourceRange range = destination.viewRange();
        return description == vulkanTextureUploadDestinationDescription() && destination.createdFor(physical, logical) &&
               destination.matchesDescription(description) && destination.resourceHandle() == description.mHandle &&
               destination.expectedRevision() == description.mExpectedRevision && destination.image() != VK_NULL_HANDLE &&
               destination.imageView() != VK_NULL_HANDLE && destination.format() == VK_FORMAT_R8G8B8A8_UNORM &&
               destination.mipLevels() == LLRenderContract::TEXTURE_UPLOAD_MIP_LEVELS && range.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT &&
               range.baseMipLevel == 0 && range.levelCount == LLRenderContract::TEXTURE_UPLOAD_MIP_LEVELS && range.baseArrayLayer == 0 &&
               range.layerCount == 1 && destination.isResident() && destination.residentRevision() == description.mExpectedRevision &&
               destination.residentContentIdentity() != 0 && destination.currentState() == LLRenderContract::ImageState::ShaderRead;
    }

    bool validBinding(const VulkanPhysicalDeviceGeneration&              physical,
                      const VulkanLogicalDeviceGeneration&               logical,
                      const VulkanTextureUploadDestinationDescription&   destination_description,
                      const VulkanTextureUploadSampleBindingDescription& binding_description,
                      const VulkanTextureUploadDestinationGeneration&    destination,
                      const VulkanTextureUploadSampleBindingGeneration&  binding) noexcept
    {
        return binding_description == vulkanTextureUploadSampleBindingDescription() &&
               validDestination(physical, logical, destination_description, destination) &&
               binding.createdFor(physical, logical, destination) && binding.matchesDescription(binding_description) &&
               binding.retainsTextureUploadDestinationGeneration(destination) &&
               binding.destinationResourceHandle() == destination_description.mHandle &&
               binding.expectedRevision() == destination_description.mExpectedRevision &&
               binding.residentRevision() == destination.residentRevision() &&
               binding.residentContentIdentity() == destination.residentContentIdentity() &&
               binding.destinationImageView() == destination.imageView() &&
               binding.destinationImageLayout() == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && binding.descriptorSetIndex() == 0 &&
               binding.binding() == 0 && binding.sampler() != VK_NULL_HANDLE && binding.descriptorSetLayout() != VK_NULL_HANDLE &&
               binding.pipelineLayout() != VK_NULL_HANDLE && binding.descriptorPool() != VK_NULL_HANDLE &&
               binding.descriptorSet() != VK_NULL_HANDLE;
    }

    bool validSwapchainParents(const VulkanPhysicalDeviceGeneration&              physical,
                               const VulkanLogicalDeviceGeneration&               logical,
                               const VulkanSwapchainConfigurationGeneration&      configuration,
                               const VulkanSwapchainGeneration&                   swapchain,
                               const VulkanSwapchainImagesGeneration&             images,
                               const VulkanSwapchainPresentationTargetGeneration& target) noexcept
    {
        const VkExtent2D drawable_extent = configuration.drawableExtent();
        const VkExtent2D image_extent    = configuration.imageExtent();
        return drawable_extent.width != 0 && drawable_extent.height != 0 && image_extent.width != 0 && image_extent.height != 0 &&
               configuration.createdFor(physical, logical, drawable_extent) && configuration.imageCount() != 0 &&
               configuration.surfaceFormat().format != VK_FORMAT_UNDEFINED && swapchain.createdFor(logical, configuration) &&
               swapchain.swapchain() != VK_NULL_HANDLE && images.createdFor(logical, configuration, swapchain) &&
               images.imageCount() != 0 && images.imageFormat() == configuration.surfaceFormat().format &&
               target.createdFor(logical, configuration, swapchain, images) && target.renderPass() != VK_NULL_HANDLE &&
               target.imageFormat() == images.imageFormat() && target.framebufferCount() == images.imageCount();
    }

    bool parentsAreCurrent(const VulkanPhysicalDeviceGeneration&               physical,
                           const VulkanLogicalDeviceGeneration&                logical,
                           const VulkanTextureUploadDestinationDescription&    destination_description,
                           const VulkanTextureUploadDestinationDescription&    owned_destination_description,
                           const VulkanTextureUploadSampleBindingDescription&  binding_description,
                           const VulkanTextureUploadSampleBindingDescription&  owned_binding_description,
                           const VulkanTextureUploadSamplePipelineDescription& description,
                           const VulkanTextureUploadSamplePipelineDescription& owned_description,
                           const VulkanTextureUploadDestinationGeneration&     destination,
                           const VulkanTextureUploadSampleBindingGeneration&   binding,
                           const VulkanSwapchainConfigurationGeneration&       configuration,
                           const VulkanSwapchainGeneration&                    swapchain,
                           const VulkanSwapchainImagesGeneration&              images,
                           const VulkanSwapchainPresentationTargetGeneration&  target,
                           std::uint64_t                                       resident_revision,
                           std::uint64_t                                       resident_content_identity,
                           VkImageView                                         destination_image_view,
                           VkDescriptorSet                                     descriptor_set,
                           VkPipelineLayout                                    pipeline_layout,
                           VkExtent2D                                          drawable_extent,
                           VkSwapchainKHR                                      swapchain_handle,
                           VkFormat                                            image_format,
                           VkExtent2D                                          image_extent,
                           std::uint32_t                                       image_count,
                           VkRenderPass                                        render_pass) noexcept
    {
        const VkExtent2D current_drawable_extent = configuration.drawableExtent();
        const VkExtent2D current_image_extent    = configuration.imageExtent();
        return destination_description == owned_destination_description && binding_description == owned_binding_description &&
               description == owned_description && owned_destination_description == vulkanTextureUploadDestinationDescription() &&
               owned_binding_description == vulkanTextureUploadSampleBindingDescription() &&
               owned_description == vulkanTextureUploadSamplePipelineDescription() && validPhysical(physical) &&
               validLogical(physical, logical) &&
               validBinding(physical, logical, owned_destination_description, owned_binding_description, destination, binding) &&
               validSwapchainParents(physical, logical, configuration, swapchain, images, target) &&
               destination.residentRevision() == resident_revision && destination.residentContentIdentity() == resident_content_identity &&
               destination.imageView() == destination_image_view && binding.residentRevision() == resident_revision &&
               binding.residentContentIdentity() == resident_content_identity && binding.destinationImageView() == destination_image_view &&
               binding.descriptorSet() == descriptor_set && binding.pipelineLayout() == pipeline_layout &&
               current_drawable_extent.width == drawable_extent.width && current_drawable_extent.height == drawable_extent.height &&
               swapchain.swapchain() == swapchain_handle && configuration.surfaceFormat().format == image_format &&
               images.imageFormat() == image_format && target.imageFormat() == image_format &&
               current_image_extent.width == image_extent.width && current_image_extent.height == image_extent.height &&
               target.imageExtent().width == image_extent.width && target.imageExtent().height == image_extent.height &&
               images.imageCount() == image_count && target.framebufferCount() == image_count && target.renderPass() == render_pass;
    }

    std::optional<VulkanTextureUploadSamplePipelineResolutionError> resolveDispatch(
        const VulkanPhysicalDeviceGeneration&               physical,
        const VulkanLogicalDeviceGeneration&                logical,
        const VulkanTextureUploadDestinationDescription&    destination_description,
        const VulkanTextureUploadDestinationDescription&    owned_destination_description,
        const VulkanTextureUploadSampleBindingDescription&  binding_description,
        const VulkanTextureUploadSampleBindingDescription&  owned_binding_description,
        const VulkanTextureUploadSamplePipelineDescription& description,
        const VulkanTextureUploadSamplePipelineDescription& owned_description,
        const VulkanTextureUploadDestinationGeneration&     destination,
        const VulkanTextureUploadSampleBindingGeneration&   binding,
        const VulkanSwapchainConfigurationGeneration&       configuration,
        const VulkanSwapchainGeneration&                    swapchain,
        const VulkanSwapchainImagesGeneration&              images,
        const VulkanSwapchainPresentationTargetGeneration&  target,
        std::uint64_t                                       resident_revision,
        std::uint64_t                                       resident_content_identity,
        VkImageView                                         destination_image_view,
        VkDescriptorSet                                     descriptor_set,
        VkPipelineLayout                                    pipeline_layout,
        VkExtent2D                                          drawable_extent,
        VkSwapchainKHR                                      swapchain_handle,
        VkFormat                                            image_format,
        VkExtent2D                                          image_extent,
        std::uint32_t                                       image_count,
        VkRenderPass                                        render_pass,
        Dispatch&                                           dispatch) noexcept
    {
        auto current = [&]() noexcept
        {
            return parentsAreCurrent(physical,
                                     logical,
                                     destination_description,
                                     owned_destination_description,
                                     binding_description,
                                     owned_binding_description,
                                     description,
                                     owned_description,
                                     destination,
                                     binding,
                                     configuration,
                                     swapchain,
                                     images,
                                     target,
                                     resident_revision,
                                     resident_content_identity,
                                     destination_image_view,
                                     descriptor_set,
                                     pipeline_layout,
                                     drawable_extent,
                                     swapchain_handle,
                                     image_format,
                                     image_extent,
                                     image_count,
                                     render_pass);
        };

        dispatch.mGetDeviceProcAddr =
            resolveInstance<PFN_vkGetDeviceProcAddr>(physical.getInstanceProcAddr(), physical.instance(), "vkGetDeviceProcAddr");
        if (!current())
        {
            return failure(VulkanTextureUploadSamplePipelineResolutionCode::ParentGenerationChanged);
        }
        if (!dispatch.mGetDeviceProcAddr)
        {
            return failure(VulkanTextureUploadSamplePipelineResolutionCode::MissingRequiredCommand,
                           VulkanTextureUploadSamplePipelineCommand::GetDeviceProcAddr);
        }

#define LL_RESOLVE_TEXTURE_SAMPLE_PIPELINE_COMMAND(member, type, name, command)                           \
    dispatch.member = resolveDevice<type>(dispatch.mGetDeviceProcAddr, logical.device(), name);           \
    if (!current())                                                                                       \
    {                                                                                                     \
        return failure(VulkanTextureUploadSamplePipelineResolutionCode::ParentGenerationChanged);         \
    }                                                                                                     \
    if (!dispatch.member)                                                                                 \
    {                                                                                                     \
        return failure(VulkanTextureUploadSamplePipelineResolutionCode::MissingRequiredCommand, command); \
    }

        LL_RESOLVE_TEXTURE_SAMPLE_PIPELINE_COMMAND(mCreateShaderModule,
                                                   PFN_vkCreateShaderModule,
                                                   "vkCreateShaderModule",
                                                   VulkanTextureUploadSamplePipelineCommand::CreateShaderModule)
        LL_RESOLVE_TEXTURE_SAMPLE_PIPELINE_COMMAND(mDestroyShaderModule,
                                                   PFN_vkDestroyShaderModule,
                                                   "vkDestroyShaderModule",
                                                   VulkanTextureUploadSamplePipelineCommand::DestroyShaderModule)
        LL_RESOLVE_TEXTURE_SAMPLE_PIPELINE_COMMAND(mCreateGraphicsPipelines,
                                                   PFN_vkCreateGraphicsPipelines,
                                                   "vkCreateGraphicsPipelines",
                                                   VulkanTextureUploadSamplePipelineCommand::CreateGraphicsPipelines)
        LL_RESOLVE_TEXTURE_SAMPLE_PIPELINE_COMMAND(mDestroyPipeline,
                                                   PFN_vkDestroyPipeline,
                                                   "vkDestroyPipeline",
                                                   VulkanTextureUploadSamplePipelineCommand::DestroyPipeline)

#undef LL_RESOLVE_TEXTURE_SAMPLE_PIPELINE_COMMAND

        return std::nullopt;
    }

    void destroyShaderModules(VkDevice                  device,
                              PFN_vkDestroyShaderModule destroy_shader_module,
                              VkShaderModule&           vertex_shader,
                              VkShaderModule&           fragment_shader) noexcept
    {
        if (fragment_shader != VK_NULL_HANDLE)
        {
            const VkShaderModule shader = std::exchange(fragment_shader, VK_NULL_HANDLE);
            destroy_shader_module(device, shader, nullptr);
        }
        if (vertex_shader != VK_NULL_HANDLE)
        {
            const VkShaderModule shader = std::exchange(vertex_shader, VK_NULL_HANDLE);
            destroy_shader_module(device, shader, nullptr);
        }
    }

    void rollback(const Dispatch& dispatch,
                  VkDevice        device,
                  VkPipeline&     pipeline,
                  VkShaderModule& vertex_shader,
                  VkShaderModule& fragment_shader) noexcept
    {
        if (pipeline != VK_NULL_HANDLE)
        {
            const VkPipeline owned_pipeline = std::exchange(pipeline, VK_NULL_HANDLE);
            dispatch.mDestroyPipeline(device, owned_pipeline, nullptr);
        }
        destroyShaderModules(device, dispatch.mDestroyShaderModule, vertex_shader, fragment_shader);
    }
} // namespace

struct VulkanTextureUploadSamplePipelineGenerationFactory
{
    static VulkanTextureUploadSamplePipelineGeneration create(const VulkanPhysicalDeviceGeneration&               physical,
                                                              const VulkanLogicalDeviceGeneration&                logical,
                                                              const VulkanTextureUploadDestinationDescription&    destination_description,
                                                              const VulkanTextureUploadSampleBindingDescription&  binding_description,
                                                              const VulkanTextureUploadSamplePipelineDescription& description,
                                                              const VulkanTextureUploadDestinationGeneration&     destination,
                                                              const VulkanTextureUploadSampleBindingGeneration&   binding,
                                                              const VulkanSwapchainConfigurationGeneration&       configuration,
                                                              const VulkanSwapchainGeneration&                    swapchain,
                                                              const VulkanSwapchainImagesGeneration&              images,
                                                              const VulkanSwapchainPresentationTargetGeneration&  target,
                                                              VkPipelineLayout                                    pipeline_layout,
                                                              VkPipeline                                          pipeline,
                                                              PFN_vkDestroyPipeline                               destroy_pipeline) noexcept
    {
        return VulkanTextureUploadSamplePipelineGeneration(physical,
                                                           logical,
                                                           destination_description,
                                                           binding_description,
                                                           description,
                                                           destination,
                                                           binding,
                                                           configuration,
                                                           swapchain,
                                                           images,
                                                           target,
                                                           pipeline_layout,
                                                           pipeline,
                                                           destroy_pipeline);
    }
};

VulkanTextureUploadSamplePipelineGeneration::VulkanTextureUploadSamplePipelineGeneration(
    const VulkanPhysicalDeviceGeneration&               physical,
    const VulkanLogicalDeviceGeneration&                logical,
    const VulkanTextureUploadDestinationDescription&    destination_description,
    const VulkanTextureUploadSampleBindingDescription&  binding_description,
    const VulkanTextureUploadSamplePipelineDescription& description,
    const VulkanTextureUploadDestinationGeneration&     destination,
    const VulkanTextureUploadSampleBindingGeneration&   binding,
    const VulkanSwapchainConfigurationGeneration&       configuration,
    const VulkanSwapchainGeneration&                    swapchain,
    const VulkanSwapchainImagesGeneration&              images,
    const VulkanSwapchainPresentationTargetGeneration&  target,
    VkPipelineLayout                                    pipeline_layout,
    VkPipeline                                          pipeline,
    PFN_vkDestroyPipeline                               destroy_pipeline) noexcept :
    mPhysicalDeviceGeneration(&physical),
    mLogicalDeviceGeneration(&logical),
    mDestinationGeneration(&destination),
    mSampleBindingGeneration(&binding),
    mConfigurationGeneration(&configuration),
    mSwapchainGeneration(&swapchain),
    mImagesGeneration(&images),
    mPresentationTargetGeneration(&target),
    mGetInstanceProcAddr(physical.getInstanceProcAddr()),
    mInstance(physical.instance()),
    mSurface(physical.surface()),
    mPhysicalDevice(physical.physicalDevice()),
    mPhysicalDeviceIndex(physical.physicalDeviceIndex()),
    mDevice(logical.device()),
    mQueue(logical.queue()),
    mQueueFamilyIndex(logical.queueFamilyIndex()),
    mQueueIndex(logical.queueIndex()),
    mDestinationDescription(destination_description),
    mSampleBindingDescription(binding_description),
    mDescription(description),
    mResidentRevision(destination.residentRevision()),
    mResidentContentIdentity(destination.residentContentIdentity()),
    mDestinationImageView(destination.imageView()),
    mDescriptorSet(binding.descriptorSet()),
    mDrawableExtent(configuration.drawableExtent()),
    mSwapchain(swapchain.swapchain()),
    mImageFormat(images.imageFormat()),
    mImageExtent(configuration.imageExtent()),
    mImageCount(images.imageCount()),
    mRenderPass(target.renderPass()),
    mPipelineLayout(pipeline_layout),
    mPipeline(pipeline),
    mDestroyPipeline(destroy_pipeline)
{
}

VulkanTextureUploadSamplePipelineGeneration::~VulkanTextureUploadSamplePipelineGeneration() noexcept
{
    reset();
}

VulkanTextureUploadSamplePipelineGeneration::VulkanTextureUploadSamplePipelineGeneration(
    VulkanTextureUploadSamplePipelineGeneration&& other) noexcept :
    mPhysicalDeviceGeneration(std::exchange(other.mPhysicalDeviceGeneration, nullptr)),
    mLogicalDeviceGeneration(std::exchange(other.mLogicalDeviceGeneration, nullptr)),
    mDestinationGeneration(std::exchange(other.mDestinationGeneration, nullptr)),
    mSampleBindingGeneration(std::exchange(other.mSampleBindingGeneration, nullptr)),
    mConfigurationGeneration(std::exchange(other.mConfigurationGeneration, nullptr)),
    mSwapchainGeneration(std::exchange(other.mSwapchainGeneration, nullptr)),
    mImagesGeneration(std::exchange(other.mImagesGeneration, nullptr)),
    mPresentationTargetGeneration(std::exchange(other.mPresentationTargetGeneration, nullptr)),
    mGetInstanceProcAddr(std::exchange(other.mGetInstanceProcAddr, nullptr)),
    mInstance(std::exchange(other.mInstance, VK_NULL_HANDLE)),
    mSurface(std::exchange(other.mSurface, VK_NULL_HANDLE)),
    mPhysicalDevice(std::exchange(other.mPhysicalDevice, VK_NULL_HANDLE)),
    mPhysicalDeviceIndex(std::exchange(other.mPhysicalDeviceIndex, 0)),
    mDevice(std::exchange(other.mDevice, VK_NULL_HANDLE)),
    mQueue(std::exchange(other.mQueue, VK_NULL_HANDLE)),
    mQueueFamilyIndex(std::exchange(other.mQueueFamilyIndex, VK_QUEUE_FAMILY_IGNORED)),
    mQueueIndex(std::exchange(other.mQueueIndex, 0)),
    mDestinationDescription(std::exchange(other.mDestinationDescription, {})),
    mSampleBindingDescription(std::exchange(other.mSampleBindingDescription, {})),
    mDescription(std::exchange(other.mDescription, {})),
    mResidentRevision(std::exchange(other.mResidentRevision, 0)),
    mResidentContentIdentity(std::exchange(other.mResidentContentIdentity, 0)),
    mDestinationImageView(std::exchange(other.mDestinationImageView, VK_NULL_HANDLE)),
    mDescriptorSet(std::exchange(other.mDescriptorSet, VK_NULL_HANDLE)),
    mDrawableExtent(std::exchange(other.mDrawableExtent, {})),
    mSwapchain(std::exchange(other.mSwapchain, VK_NULL_HANDLE)),
    mImageFormat(std::exchange(other.mImageFormat, VK_FORMAT_UNDEFINED)),
    mImageExtent(std::exchange(other.mImageExtent, {})),
    mImageCount(std::exchange(other.mImageCount, 0)),
    mRenderPass(std::exchange(other.mRenderPass, VK_NULL_HANDLE)),
    mPipelineLayout(std::exchange(other.mPipelineLayout, VK_NULL_HANDLE)),
    mPipeline(std::exchange(other.mPipeline, VK_NULL_HANDLE)),
    mDestroyPipeline(std::exchange(other.mDestroyPipeline, nullptr))
{
}

bool VulkanTextureUploadSamplePipelineGeneration::createdFor(const VulkanPhysicalDeviceGeneration&              physical,
                                                             const VulkanLogicalDeviceGeneration&               logical,
                                                             const VulkanTextureUploadDestinationGeneration&    destination,
                                                             const VulkanTextureUploadSampleBindingGeneration&  binding,
                                                             const VulkanSwapchainConfigurationGeneration&      configuration,
                                                             const VulkanSwapchainGeneration&                   swapchain,
                                                             const VulkanSwapchainImagesGeneration&             images,
                                                             const VulkanSwapchainPresentationTargetGeneration& target) const noexcept
{
    const VkExtent2D drawable_extent = configuration.drawableExtent();
    const VkExtent2D image_extent    = configuration.imageExtent();
    return mPipeline != VK_NULL_HANDLE && mPipelineLayout != VK_NULL_HANDLE && mDestroyPipeline != nullptr &&
           mPhysicalDeviceGeneration == &physical && mLogicalDeviceGeneration == &logical && mDestinationGeneration == &destination &&
           mSampleBindingGeneration == &binding && mConfigurationGeneration == &configuration && mSwapchainGeneration == &swapchain &&
           mImagesGeneration == &images && mPresentationTargetGeneration == &target && validPhysical(physical) &&
           validLogical(physical, logical) &&
           validBinding(physical, logical, mDestinationDescription, mSampleBindingDescription, destination, binding) &&
           validSwapchainParents(physical, logical, configuration, swapchain, images, target) &&
           mGetInstanceProcAddr == physical.getInstanceProcAddr() && mInstance == physical.instance() && mSurface == physical.surface() &&
           mPhysicalDevice == physical.physicalDevice() && mPhysicalDeviceIndex == physical.physicalDeviceIndex() &&
           mDevice == logical.device() && mQueue == logical.queue() && mQueueFamilyIndex == logical.queueFamilyIndex() &&
           mQueueIndex == logical.queueIndex() && mDestinationDescription == vulkanTextureUploadDestinationDescription() &&
           mSampleBindingDescription == vulkanTextureUploadSampleBindingDescription() &&
           mDescription == vulkanTextureUploadSamplePipelineDescription() && mResidentRevision == destination.residentRevision() &&
           mResidentContentIdentity == destination.residentContentIdentity() && mDestinationImageView == destination.imageView() &&
           mDescriptorSet == binding.descriptorSet() && mPipelineLayout == binding.pipelineLayout() &&
           mDrawableExtent.width == drawable_extent.width && mDrawableExtent.height == drawable_extent.height &&
           mSwapchain == swapchain.swapchain() && mImageFormat == configuration.surfaceFormat().format &&
           mImageFormat == images.imageFormat() && mImageFormat == target.imageFormat() && mImageExtent.width == image_extent.width &&
           mImageExtent.height == image_extent.height && mImageExtent.width == target.imageExtent().width &&
           mImageExtent.height == target.imageExtent().height && mImageCount == images.imageCount() &&
           mImageCount == target.framebufferCount() && mRenderPass == target.renderPass();
}

bool VulkanTextureUploadSamplePipelineGeneration::matchesDescription(
    const VulkanTextureUploadSamplePipelineDescription& description) const noexcept
{
    return mPipeline != VK_NULL_HANDLE && mDescription == description;
}

bool VulkanTextureUploadSamplePipelineGeneration::retainsTextureUploadSampleBindingGeneration(
    const VulkanTextureUploadSampleBindingGeneration& binding) const noexcept
{
    return mPipeline != VK_NULL_HANDLE && mPipelineLayout != VK_NULL_HANDLE && mSampleBindingGeneration == &binding;
}

void VulkanTextureUploadSamplePipelineGeneration::reset() noexcept
{
    const VkDevice              device           = std::exchange(mDevice, VK_NULL_HANDLE);
    const VkPipeline            pipeline         = std::exchange(mPipeline, VK_NULL_HANDLE);
    const PFN_vkDestroyPipeline destroy_pipeline = std::exchange(mDestroyPipeline, nullptr);

    mPhysicalDeviceGeneration     = nullptr;
    mLogicalDeviceGeneration      = nullptr;
    mDestinationGeneration        = nullptr;
    mSampleBindingGeneration      = nullptr;
    mConfigurationGeneration      = nullptr;
    mSwapchainGeneration          = nullptr;
    mImagesGeneration             = nullptr;
    mPresentationTargetGeneration = nullptr;
    mGetInstanceProcAddr          = nullptr;
    mInstance                     = VK_NULL_HANDLE;
    mSurface                      = VK_NULL_HANDLE;
    mPhysicalDevice               = VK_NULL_HANDLE;
    mPhysicalDeviceIndex          = 0;
    mQueue                        = VK_NULL_HANDLE;
    mQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    mQueueIndex                   = 0;
    mDestinationDescription       = {};
    mSampleBindingDescription     = {};
    mDescription                  = {};
    mResidentRevision             = 0;
    mResidentContentIdentity      = 0;
    mDestinationImageView         = VK_NULL_HANDLE;
    mDescriptorSet                = VK_NULL_HANDLE;
    mDrawableExtent               = {};
    mSwapchain                    = VK_NULL_HANDLE;
    mImageFormat                  = VK_FORMAT_UNDEFINED;
    mImageExtent                  = {};
    mImageCount                   = 0;
    mRenderPass                   = VK_NULL_HANDLE;
    mPipelineLayout               = VK_NULL_HANDLE;

    if (pipeline != VK_NULL_HANDLE && destroy_pipeline)
    {
        destroy_pipeline(device, pipeline, nullptr);
    }
}

VulkanTextureUploadSamplePipelineResolutionResult resolveVulkanTextureUploadSamplePipelineGeneration(
    const VulkanPhysicalDeviceGeneration&               physical,
    const VulkanLogicalDeviceGeneration&                logical,
    const VulkanTextureUploadDestinationDescription&    destination_description,
    const VulkanTextureUploadSampleBindingDescription&  binding_description,
    const VulkanTextureUploadSamplePipelineDescription& description,
    const VulkanTextureUploadDestinationGeneration&     destination,
    const VulkanTextureUploadSampleBindingGeneration&   binding,
    const VulkanSwapchainConfigurationGeneration&       configuration,
    const VulkanSwapchainGeneration&                    swapchain,
    const VulkanSwapchainImagesGeneration&              images,
    const VulkanSwapchainPresentationTargetGeneration&  target) noexcept
{
    const VulkanTextureUploadDestinationDescription    owned_destination_description = destination_description;
    const VulkanTextureUploadSampleBindingDescription  owned_binding_description     = binding_description;
    const VulkanTextureUploadSamplePipelineDescription owned_description             = description;

    if (!validPhysical(physical))
    {
        return failure(VulkanTextureUploadSamplePipelineResolutionCode::InvalidPhysicalDeviceGeneration);
    }
    if (!validLogical(physical, logical))
    {
        return failure(VulkanTextureUploadSamplePipelineResolutionCode::InvalidLogicalDeviceGeneration);
    }
    if (owned_destination_description != vulkanTextureUploadDestinationDescription() ||
        owned_binding_description != vulkanTextureUploadSampleBindingDescription() ||
        owned_description != vulkanTextureUploadSamplePipelineDescription())
    {
        return failure(VulkanTextureUploadSamplePipelineResolutionCode::InvalidDescription);
    }
    if (!validDestination(physical, logical, owned_destination_description, destination))
    {
        return failure(VulkanTextureUploadSamplePipelineResolutionCode::InvalidTextureUploadDestinationGeneration);
    }
    if (!validBinding(physical, logical, owned_destination_description, owned_binding_description, destination, binding))
    {
        return failure(VulkanTextureUploadSamplePipelineResolutionCode::InvalidTextureUploadSampleBindingGeneration);
    }
    const VkExtent2D drawable_extent = configuration.drawableExtent();
    if (!configuration.createdFor(physical, logical, drawable_extent) || drawable_extent.width == 0 || drawable_extent.height == 0 ||
        configuration.imageExtent().width == 0 || configuration.imageExtent().height == 0 || configuration.imageCount() == 0 ||
        configuration.surfaceFormat().format == VK_FORMAT_UNDEFINED)
    {
        return failure(VulkanTextureUploadSamplePipelineResolutionCode::InvalidSwapchainConfigurationGeneration);
    }
    if (!swapchain.createdFor(logical, configuration) || swapchain.swapchain() == VK_NULL_HANDLE)
    {
        return failure(VulkanTextureUploadSamplePipelineResolutionCode::InvalidSwapchainGeneration);
    }
    if (!images.createdFor(logical, configuration, swapchain) || images.imageCount() == 0 ||
        images.imageFormat() != configuration.surfaceFormat().format)
    {
        return failure(VulkanTextureUploadSamplePipelineResolutionCode::InvalidSwapchainImagesGeneration);
    }
    if (!target.createdFor(logical, configuration, swapchain, images) || target.renderPass() == VK_NULL_HANDLE ||
        target.imageFormat() != images.imageFormat() || target.framebufferCount() != images.imageCount())
    {
        return failure(VulkanTextureUploadSamplePipelineResolutionCode::InvalidSwapchainPresentationTargetGeneration);
    }

    const std::uint64_t    resident_revision         = destination.residentRevision();
    const std::uint64_t    resident_content_identity = destination.residentContentIdentity();
    const VkImageView      destination_image_view    = destination.imageView();
    const VkDescriptorSet  descriptor_set            = binding.descriptorSet();
    const VkPipelineLayout pipeline_layout           = binding.pipelineLayout();
    const VkSwapchainKHR   swapchain_handle          = swapchain.swapchain();
    const VkFormat         image_format              = images.imageFormat();
    const VkExtent2D       image_extent              = configuration.imageExtent();
    const std::uint32_t    image_count               = images.imageCount();
    const VkRenderPass     render_pass               = target.renderPass();

    auto current = [&]() noexcept
    {
        return parentsAreCurrent(physical,
                                 logical,
                                 destination_description,
                                 owned_destination_description,
                                 binding_description,
                                 owned_binding_description,
                                 description,
                                 owned_description,
                                 destination,
                                 binding,
                                 configuration,
                                 swapchain,
                                 images,
                                 target,
                                 resident_revision,
                                 resident_content_identity,
                                 destination_image_view,
                                 descriptor_set,
                                 pipeline_layout,
                                 drawable_extent,
                                 swapchain_handle,
                                 image_format,
                                 image_extent,
                                 image_count,
                                 render_pass);
    };

    Dispatch dispatch;
    if (auto error = resolveDispatch(physical,
                                     logical,
                                     destination_description,
                                     owned_destination_description,
                                     binding_description,
                                     owned_binding_description,
                                     description,
                                     owned_description,
                                     destination,
                                     binding,
                                     configuration,
                                     swapchain,
                                     images,
                                     target,
                                     resident_revision,
                                     resident_content_identity,
                                     destination_image_view,
                                     descriptor_set,
                                     pipeline_layout,
                                     drawable_extent,
                                     swapchain_handle,
                                     image_format,
                                     image_extent,
                                     image_count,
                                     render_pass,
                                     dispatch))
    {
        return *error;
    }

    const VkDevice device = logical.device();

    VkShaderModuleCreateInfo vertex_shader_info{};
    vertex_shader_info.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vertex_shader_info.codeSize = TEXTURE_UPLOAD_SAMPLE_VERTEX_SHADER.size() * sizeof(std::uint32_t);
    vertex_shader_info.pCode    = TEXTURE_UPLOAD_SAMPLE_VERTEX_SHADER.data();

    VkShaderModule vertex_shader        = VK_NULL_HANDLE;
    const VkResult vertex_shader_result = dispatch.mCreateShaderModule(device, &vertex_shader_info, nullptr, &vertex_shader);
    const bool     owns_vertex_shader   = vertex_shader_result == VK_SUCCESS && vertex_shader != VK_NULL_HANDLE;
    if (!current())
    {
        VkPipeline     no_pipeline  = VK_NULL_HANDLE;
        VkShaderModule no_fragment  = VK_NULL_HANDLE;
        VkShaderModule owned_vertex = owns_vertex_shader ? vertex_shader : VK_NULL_HANDLE;
        rollback(dispatch, device, no_pipeline, owned_vertex, no_fragment);
        return failure(VulkanTextureUploadSamplePipelineResolutionCode::ParentGenerationChanged);
    }
    if (vertex_shader_result != VK_SUCCESS)
    {
        return failure(VulkanTextureUploadSamplePipelineResolutionCode::VertexShaderModuleCreationFailure,
                       VulkanTextureUploadSamplePipelineCommand::CreateShaderModule,
                       vertex_shader_result);
    }
    if (vertex_shader == VK_NULL_HANDLE)
    {
        return failure(VulkanTextureUploadSamplePipelineResolutionCode::NullVertexShaderModuleOnSuccess,
                       VulkanTextureUploadSamplePipelineCommand::CreateShaderModule);
    }

    VkShaderModuleCreateInfo fragment_shader_info{};
    fragment_shader_info.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    fragment_shader_info.codeSize = TEXTURE_UPLOAD_SAMPLE_FRAGMENT_SHADER.size() * sizeof(std::uint32_t);
    fragment_shader_info.pCode    = TEXTURE_UPLOAD_SAMPLE_FRAGMENT_SHADER.data();

    VkShaderModule fragment_shader        = VK_NULL_HANDLE;
    const VkResult fragment_shader_result = dispatch.mCreateShaderModule(device, &fragment_shader_info, nullptr, &fragment_shader);
    const bool     owns_fragment_shader   = fragment_shader_result == VK_SUCCESS && fragment_shader != VK_NULL_HANDLE;
    if (!current())
    {
        VkPipeline no_pipeline = VK_NULL_HANDLE;
        if (!owns_fragment_shader)
        {
            fragment_shader = VK_NULL_HANDLE;
        }
        rollback(dispatch, device, no_pipeline, vertex_shader, fragment_shader);
        return failure(VulkanTextureUploadSamplePipelineResolutionCode::ParentGenerationChanged);
    }
    if (fragment_shader_result != VK_SUCCESS)
    {
        VkPipeline no_pipeline = VK_NULL_HANDLE;
        fragment_shader        = VK_NULL_HANDLE;
        rollback(dispatch, device, no_pipeline, vertex_shader, fragment_shader);
        return failure(VulkanTextureUploadSamplePipelineResolutionCode::FragmentShaderModuleCreationFailure,
                       VulkanTextureUploadSamplePipelineCommand::CreateShaderModule,
                       fragment_shader_result);
    }
    if (fragment_shader == VK_NULL_HANDLE)
    {
        VkPipeline no_pipeline = VK_NULL_HANDLE;
        rollback(dispatch, device, no_pipeline, vertex_shader, fragment_shader);
        return failure(VulkanTextureUploadSamplePipelineResolutionCode::NullFragmentShaderModuleOnSuccess,
                       VulkanTextureUploadSamplePipelineCommand::CreateShaderModule);
    }

    const std::array<VkPipelineShaderStageCreateInfo, 2> shader_stages{
        VkPipelineShaderStageCreateInfo{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT,
                                         vertex_shader, "main", nullptr },
        VkPipelineShaderStageCreateInfo{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT,
                                         fragment_shader, "main", nullptr }
    };

    const VkVertexInputBindingDescription   vertex_binding{ 0, 16, VK_VERTEX_INPUT_RATE_VERTEX };
    const VkVertexInputAttributeDescription vertex_attribute{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 };

    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount   = 1;
    vertex_input.pVertexBindingDescriptions      = &vertex_binding;
    vertex_input.vertexAttributeDescriptionCount = 1;
    vertex_input.pVertexAttributeDescriptions    = &vertex_attribute;

    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo rasterization{};
    rasterization.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization.depthClampEnable        = VK_FALSE;
    rasterization.rasterizerDiscardEnable = VK_FALSE;
    rasterization.polygonMode             = VK_POLYGON_MODE_FILL;
    rasterization.cullMode                = VK_CULL_MODE_NONE;
    rasterization.frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization.depthBiasEnable         = VK_FALSE;
    rasterization.lineWidth               = 1.f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisample.sampleShadingEnable  = VK_FALSE;

    VkPipelineColorBlendAttachmentState color_attachment{};
    color_attachment.blendEnable         = VK_FALSE;
    color_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    color_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    color_attachment.colorBlendOp        = VK_BLEND_OP_ADD;
    color_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    color_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    color_attachment.alphaBlendOp        = VK_BLEND_OP_ADD;
    color_attachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo color_blend{};
    color_blend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend.logicOpEnable   = VK_FALSE;
    color_blend.logicOp         = VK_LOGIC_OP_COPY;
    color_blend.attachmentCount = 1;
    color_blend.pAttachments    = &color_attachment;

    constexpr std::array<VkDynamicState, 2> dynamic_states{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo        dynamic_state{};
    dynamic_state.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = static_cast<std::uint32_t>(dynamic_states.size());
    dynamic_state.pDynamicStates    = dynamic_states.data();

    VkGraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount          = static_cast<std::uint32_t>(shader_stages.size());
    pipeline_info.pStages             = shader_stages.data();
    pipeline_info.pVertexInputState   = &vertex_input;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState      = &viewport_state;
    pipeline_info.pRasterizationState = &rasterization;
    pipeline_info.pMultisampleState   = &multisample;
    pipeline_info.pColorBlendState    = &color_blend;
    pipeline_info.pDynamicState       = &dynamic_state;
    pipeline_info.layout              = pipeline_layout;
    pipeline_info.renderPass          = render_pass;
    pipeline_info.subpass             = 0;
    pipeline_info.basePipelineHandle  = VK_NULL_HANDLE;
    pipeline_info.basePipelineIndex   = -1;

    VkPipeline     pipeline        = VK_NULL_HANDLE;
    const VkResult pipeline_result = dispatch.mCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline);
    if (!current())
    {
        rollback(dispatch, device, pipeline, vertex_shader, fragment_shader);
        return failure(VulkanTextureUploadSamplePipelineResolutionCode::ParentGenerationChanged);
    }
    if (pipeline_result != VK_SUCCESS)
    {
        rollback(dispatch, device, pipeline, vertex_shader, fragment_shader);
        return failure(VulkanTextureUploadSamplePipelineResolutionCode::GraphicsPipelineCreationFailure,
                       VulkanTextureUploadSamplePipelineCommand::CreateGraphicsPipelines,
                       pipeline_result);
    }
    if (pipeline == VK_NULL_HANDLE)
    {
        rollback(dispatch, device, pipeline, vertex_shader, fragment_shader);
        return failure(VulkanTextureUploadSamplePipelineResolutionCode::NullGraphicsPipelineOnSuccess,
                       VulkanTextureUploadSamplePipelineCommand::CreateGraphicsPipelines);
    }

    destroyShaderModules(device, dispatch.mDestroyShaderModule, vertex_shader, fragment_shader);
    if (!current())
    {
        dispatch.mDestroyPipeline(device, pipeline, nullptr);
        return failure(VulkanTextureUploadSamplePipelineResolutionCode::ParentGenerationChanged);
    }

    return VulkanTextureUploadSamplePipelineGenerationFactory::create(physical,
                                                                      logical,
                                                                      owned_destination_description,
                                                                      owned_binding_description,
                                                                      owned_description,
                                                                      destination,
                                                                      binding,
                                                                      configuration,
                                                                      swapchain,
                                                                      images,
                                                                      target,
                                                                      pipeline_layout,
                                                                      pipeline,
                                                                      dispatch.mDestroyPipeline);
}

} // namespace LLRenderVulkan
