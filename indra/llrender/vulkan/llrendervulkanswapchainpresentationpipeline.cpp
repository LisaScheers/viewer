/**
 * @file llrendervulkanswapchainpresentationpipeline.cpp
 * @brief Loader-neutral Vulkan swapchain presentation-pipeline ownership.
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

#include "llrendervulkanswapchainpresentationpipeline.h"

#include <array>
#include <utility>

namespace LLRenderVulkan
{

namespace
{
    // Generated from shaders/presentation.vert.glsl with glslang 16.4.0:
    // glslangValidator -V --target-env vulkan1.1 -S vert -Os -g0
    // 736 bytes; SHA-256 c22f211633f950d6f10a87934e705a32cb76f29d17b58853a5122c2a100c27c1.
    constexpr std::array<std::uint32_t, 184> PRESENTATION_VERTEX_SHADER{
        0x07230203, 0x00010300, 0x0008000b, 0x00000022, 0x00000000, 0x00020011, 0x00000001, 0x0006000b, 0x00000001, 0x4c534c47, 0x6474732e,
        0x3035342e, 0x00000000, 0x0003000e, 0x00000000, 0x00000001, 0x0007000f, 0x00000000, 0x00000004, 0x6e69616d, 0x00000000, 0x0000000d,
        0x00000011, 0x00030047, 0x0000000b, 0x00000002, 0x00050048, 0x0000000b, 0x00000000, 0x0000000b, 0x00000000, 0x00050048, 0x0000000b,
        0x00000001, 0x0000000b, 0x00000001, 0x00050048, 0x0000000b, 0x00000002, 0x0000000b, 0x00000003, 0x00050048, 0x0000000b, 0x00000003,
        0x0000000b, 0x00000004, 0x00040047, 0x00000011, 0x0000000b, 0x0000002a, 0x00020013, 0x00000002, 0x00030021, 0x00000003, 0x00000002,
        0x00030016, 0x00000006, 0x00000020, 0x00040017, 0x00000007, 0x00000006, 0x00000004, 0x00040015, 0x00000008, 0x00000020, 0x00000000,
        0x0004002b, 0x00000008, 0x00000009, 0x00000001, 0x0004001c, 0x0000000a, 0x00000006, 0x00000009, 0x0006001e, 0x0000000b, 0x00000007,
        0x00000006, 0x0000000a, 0x0000000a, 0x00040020, 0x0000000c, 0x00000003, 0x0000000b, 0x0004003b, 0x0000000c, 0x0000000d, 0x00000003,
        0x00040015, 0x0000000e, 0x00000020, 0x00000001, 0x0004002b, 0x0000000e, 0x0000000f, 0x00000000, 0x00040020, 0x00000010, 0x00000001,
        0x0000000e, 0x0004003b, 0x00000010, 0x00000011, 0x00000001, 0x0004002b, 0x0000000e, 0x00000013, 0x00000001, 0x00020014, 0x00000014,
        0x0004002b, 0x00000006, 0x00000016, 0x40400000, 0x0004002b, 0x00000006, 0x00000017, 0xbf800000, 0x0004002b, 0x0000000e, 0x0000001a,
        0x00000002, 0x0004002b, 0x00000006, 0x0000001d, 0x00000000, 0x0004002b, 0x00000006, 0x0000001e, 0x3f800000, 0x00040020, 0x00000020,
        0x00000003, 0x00000007, 0x00050036, 0x00000002, 0x00000004, 0x00000000, 0x00000003, 0x000200f8, 0x00000005, 0x0004003d, 0x0000000e,
        0x00000012, 0x00000011, 0x000500aa, 0x00000014, 0x00000015, 0x00000012, 0x00000013, 0x000600a9, 0x00000006, 0x00000018, 0x00000015,
        0x00000016, 0x00000017, 0x000500aa, 0x00000014, 0x0000001b, 0x00000012, 0x0000001a, 0x000600a9, 0x00000006, 0x0000001c, 0x0000001b,
        0x00000016, 0x00000017, 0x00070050, 0x00000007, 0x0000001f, 0x00000018, 0x0000001c, 0x0000001d, 0x0000001e, 0x00050041, 0x00000020,
        0x00000021, 0x0000000d, 0x0000000f, 0x0003003e, 0x00000021, 0x0000001f, 0x000100fd, 0x00010038
    };

    // Generated from shaders/presentation.frag.glsl with glslang 16.4.0:
    // glslangValidator -V --target-env vulkan1.1 -S frag -Os -g0
    // 304 bytes; SHA-256 784c8df9710a1b546fa9873249b463567449f4e48b37c656c6d2ac3584e897cc.
    constexpr std::array<std::uint32_t, 76> PRESENTATION_FRAGMENT_SHADER{
        0x07230203, 0x00010300, 0x0008000b, 0x0000000d, 0x00000000, 0x00020011, 0x00000001, 0x0006000b, 0x00000001, 0x4c534c47, 0x6474732e,
        0x3035342e, 0x00000000, 0x0003000e, 0x00000000, 0x00000001, 0x0006000f, 0x00000004, 0x00000004, 0x6e69616d, 0x00000000, 0x00000009,
        0x00030010, 0x00000004, 0x00000007, 0x00040047, 0x00000009, 0x0000001e, 0x00000000, 0x00020013, 0x00000002, 0x00030021, 0x00000003,
        0x00000002, 0x00030016, 0x00000006, 0x00000020, 0x00040017, 0x00000007, 0x00000006, 0x00000004, 0x00040020, 0x00000008, 0x00000003,
        0x00000007, 0x0004003b, 0x00000008, 0x00000009, 0x00000003, 0x0004002b, 0x00000006, 0x0000000a, 0x00000000, 0x0004002b, 0x00000006,
        0x0000000b, 0x3f800000, 0x0007002c, 0x00000007, 0x0000000c, 0x0000000a, 0x0000000b, 0x0000000a, 0x0000000b, 0x00050036, 0x00000002,
        0x00000004, 0x00000000, 0x00000003, 0x000200f8, 0x00000005, 0x0003003e, 0x00000009, 0x0000000c, 0x000100fd, 0x00010038
    };

    static_assert(PRESENTATION_VERTEX_SHADER.size() * sizeof(std::uint32_t) == 736);
    static_assert(PRESENTATION_FRAGMENT_SHADER.size() * sizeof(std::uint32_t) == 304);
    static_assert(PRESENTATION_VERTEX_SHADER.front() == 0x07230203);
    static_assert(PRESENTATION_FRAGMENT_SHADER.front() == 0x07230203);

    struct PresentationPipelineDispatch
    {
        PFN_vkGetDeviceProcAddr       mGetDeviceProcAddr       = nullptr;
        PFN_vkCreateShaderModule      mCreateShaderModule      = nullptr;
        PFN_vkDestroyShaderModule     mDestroyShaderModule     = nullptr;
        PFN_vkCreatePipelineLayout    mCreatePipelineLayout    = nullptr;
        PFN_vkDestroyPipelineLayout   mDestroyPipelineLayout   = nullptr;
        PFN_vkCreateGraphicsPipelines mCreateGraphicsPipelines = nullptr;
        PFN_vkDestroyPipeline         mDestroyPipeline         = nullptr;
    };

    VulkanSwapchainPresentationPipelineResolutionError failure(
        VulkanSwapchainPresentationPipelineResolutionCode         code,
        std::optional<VulkanSwapchainPresentationPipelineCommand> command = std::nullopt,
        VkResult                                                  result  = VK_SUCCESS) noexcept
    {
        return { code, command, result };
    }

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

    bool valid(const VulkanLogicalDeviceGeneration& generation) noexcept
    {
        return generation.getInstanceProcAddr() != nullptr && generation.instance() != VK_NULL_HANDLE &&
               generation.surface() != VK_NULL_HANDLE && generation.physicalDevice() != VK_NULL_HANDLE &&
               generation.device() != VK_NULL_HANDLE && generation.queue() != VK_NULL_HANDLE &&
               generation.queueFamilyIndex() != VK_QUEUE_FAMILY_IGNORED;
    }

    bool belongsTo(const VulkanSwapchainConfigurationGeneration& configuration,
                   const VulkanLogicalDeviceGeneration&          logical_device) noexcept
    {
        const VkExtent2D drawable_extent = configuration.drawableExtent();
        const VkExtent2D image_extent    = configuration.imageExtent();
        return configuration.getInstanceProcAddr() == logical_device.getInstanceProcAddr() &&
               configuration.instance() == logical_device.instance() && configuration.surface() == logical_device.surface() &&
               configuration.physicalDevice() == logical_device.physicalDevice() &&
               configuration.physicalDeviceIndex() == logical_device.physicalDeviceIndex() &&
               configuration.device() == logical_device.device() && configuration.queueFamilyIndex() == logical_device.queueFamilyIndex() &&
               drawable_extent.width != 0 && drawable_extent.height != 0 && image_extent.width != 0 && image_extent.height != 0 &&
               configuration.imageCount() != 0 && configuration.surfaceFormat().format != VK_FORMAT_UNDEFINED;
    }

    std::optional<VulkanSwapchainPresentationPipelineResolutionError> resolveDispatch(const VulkanLogicalDeviceGeneration& logical_device,
                                                                                      PresentationPipelineDispatch& dispatch) noexcept
    {
        dispatch.mGetDeviceProcAddr = resolveInstance<PFN_vkGetDeviceProcAddr>(logical_device.getInstanceProcAddr(),
                                                                               logical_device.instance(), "vkGetDeviceProcAddr");
        if (!dispatch.mGetDeviceProcAddr)
        {
            return failure(VulkanSwapchainPresentationPipelineResolutionCode::MissingRequiredCommand,
                           VulkanSwapchainPresentationPipelineCommand::GetDeviceProcAddr);
        }

#define LL_RESOLVE_PRESENTATION_PIPELINE_COMMAND(member, type, name, command)                               \
    dispatch.member = resolveDevice<type>(dispatch.mGetDeviceProcAddr, logical_device.device(), name);      \
    if (!dispatch.member)                                                                                   \
    {                                                                                                       \
        return failure(VulkanSwapchainPresentationPipelineResolutionCode::MissingRequiredCommand, command); \
    }

        LL_RESOLVE_PRESENTATION_PIPELINE_COMMAND(mCreateShaderModule, PFN_vkCreateShaderModule, "vkCreateShaderModule",
                                                 VulkanSwapchainPresentationPipelineCommand::CreateShaderModule)
        LL_RESOLVE_PRESENTATION_PIPELINE_COMMAND(mDestroyShaderModule, PFN_vkDestroyShaderModule, "vkDestroyShaderModule",
                                                 VulkanSwapchainPresentationPipelineCommand::DestroyShaderModule)
        LL_RESOLVE_PRESENTATION_PIPELINE_COMMAND(mCreatePipelineLayout, PFN_vkCreatePipelineLayout, "vkCreatePipelineLayout",
                                                 VulkanSwapchainPresentationPipelineCommand::CreatePipelineLayout)
        LL_RESOLVE_PRESENTATION_PIPELINE_COMMAND(mDestroyPipelineLayout, PFN_vkDestroyPipelineLayout, "vkDestroyPipelineLayout",
                                                 VulkanSwapchainPresentationPipelineCommand::DestroyPipelineLayout)
        LL_RESOLVE_PRESENTATION_PIPELINE_COMMAND(mCreateGraphicsPipelines, PFN_vkCreateGraphicsPipelines, "vkCreateGraphicsPipelines",
                                                 VulkanSwapchainPresentationPipelineCommand::CreateGraphicsPipelines)
        LL_RESOLVE_PRESENTATION_PIPELINE_COMMAND(mDestroyPipeline, PFN_vkDestroyPipeline, "vkDestroyPipeline",
                                                 VulkanSwapchainPresentationPipelineCommand::DestroyPipeline)

#undef LL_RESOLVE_PRESENTATION_PIPELINE_COMMAND
        return std::nullopt;
    }

    bool parentsAreCurrent(const VulkanLogicalDeviceGeneration&               logical_device_generation,
                           const VulkanSwapchainConfigurationGeneration&      configuration_generation,
                           const VulkanSwapchainGeneration&                   swapchain_generation,
                           const VulkanSwapchainImagesGeneration&             images_generation,
                           const VulkanSwapchainPresentationTargetGeneration& presentation_target_generation) noexcept
    {
        return valid(logical_device_generation) && belongsTo(configuration_generation, logical_device_generation) &&
               swapchain_generation.createdFor(logical_device_generation, configuration_generation) &&
               images_generation.createdFor(logical_device_generation, configuration_generation, swapchain_generation) &&
               presentation_target_generation.createdFor(logical_device_generation, configuration_generation, swapchain_generation,
                                                         images_generation);
    }

    void destroyShaderModules(VkDevice                  device,
                              PFN_vkDestroyShaderModule destroy_shader_module,
                              VkShaderModule&           vertex_shader,
                              VkShaderModule&           fragment_shader) noexcept
    {
        if (fragment_shader != VK_NULL_HANDLE)
        {
            destroy_shader_module(device, fragment_shader, nullptr);
            fragment_shader = VK_NULL_HANDLE;
        }
        if (vertex_shader != VK_NULL_HANDLE)
        {
            destroy_shader_module(device, vertex_shader, nullptr);
            vertex_shader = VK_NULL_HANDLE;
        }
    }

    void rollback(VkDevice                            device,
                  const PresentationPipelineDispatch& dispatch,
                  VkPipeline&                         pipeline,
                  VkShaderModule&                     vertex_shader,
                  VkShaderModule&                     fragment_shader,
                  VkPipelineLayout&                   pipeline_layout) noexcept
    {
        if (pipeline != VK_NULL_HANDLE)
        {
            dispatch.mDestroyPipeline(device, pipeline, nullptr);
            pipeline = VK_NULL_HANDLE;
        }
        destroyShaderModules(device, dispatch.mDestroyShaderModule, vertex_shader, fragment_shader);
        if (pipeline_layout != VK_NULL_HANDLE)
        {
            dispatch.mDestroyPipelineLayout(device, pipeline_layout, nullptr);
            pipeline_layout = VK_NULL_HANDLE;
        }
    }

} // namespace

struct VulkanSwapchainPresentationPipelineGenerationFactory
{
    static VulkanSwapchainPresentationPipelineGeneration create(
        const VulkanLogicalDeviceGeneration&               logical_device_generation,
        const VulkanSwapchainConfigurationGeneration&      configuration_generation,
        const VulkanSwapchainGeneration&                   swapchain_generation,
        const VulkanSwapchainImagesGeneration&             images_generation,
        const VulkanSwapchainPresentationTargetGeneration& presentation_target_generation,
        VkPipelineLayout                                   pipeline_layout,
        VkPipeline                                         pipeline,
        PFN_vkDestroyPipelineLayout                        destroy_pipeline_layout,
        PFN_vkDestroyPipeline                              destroy_pipeline) noexcept
    {
        return VulkanSwapchainPresentationPipelineGeneration(logical_device_generation, configuration_generation, swapchain_generation,
                                                             images_generation, presentation_target_generation, pipeline_layout, pipeline,
                                                             destroy_pipeline_layout, destroy_pipeline);
    }
};

VulkanSwapchainPresentationPipelineGeneration::VulkanSwapchainPresentationPipelineGeneration(
    const VulkanLogicalDeviceGeneration&               logical_device_generation,
    const VulkanSwapchainConfigurationGeneration&      configuration_generation,
    const VulkanSwapchainGeneration&                   swapchain_generation,
    const VulkanSwapchainImagesGeneration&             images_generation,
    const VulkanSwapchainPresentationTargetGeneration& presentation_target_generation,
    VkPipelineLayout                                   pipeline_layout,
    VkPipeline                                         pipeline,
    PFN_vkDestroyPipelineLayout                        destroy_pipeline_layout,
    PFN_vkDestroyPipeline                              destroy_pipeline) noexcept :
    mLogicalDeviceGeneration(&logical_device_generation),
    mConfigurationGeneration(&configuration_generation),
    mSwapchainGeneration(&swapchain_generation),
    mImagesGeneration(&images_generation),
    mPresentationTargetGeneration(&presentation_target_generation),
    mGetInstanceProcAddr(logical_device_generation.getInstanceProcAddr()),
    mInstance(logical_device_generation.instance()),
    mSurface(logical_device_generation.surface()),
    mPhysicalDevice(logical_device_generation.physicalDevice()),
    mPhysicalDeviceIndex(logical_device_generation.physicalDeviceIndex()),
    mDevice(logical_device_generation.device()),
    mQueue(logical_device_generation.queue()),
    mQueueFamilyIndex(logical_device_generation.queueFamilyIndex()),
    mQueueIndex(logical_device_generation.queueIndex()),
    mDrawableExtent(configuration_generation.drawableExtent()),
    mSwapchain(swapchain_generation.swapchain()),
    mImageFormat(images_generation.imageFormat()),
    mImageExtent(configuration_generation.imageExtent()),
    mImageCount(images_generation.imageCount()),
    mRenderPass(presentation_target_generation.renderPass()),
    mPipelineLayout(pipeline_layout),
    mPipeline(pipeline),
    mDestroyPipelineLayout(destroy_pipeline_layout),
    mDestroyPipeline(destroy_pipeline)
{
}

VulkanSwapchainPresentationPipelineGeneration::~VulkanSwapchainPresentationPipelineGeneration() noexcept
{
    reset();
}

VulkanSwapchainPresentationPipelineGeneration::VulkanSwapchainPresentationPipelineGeneration(
    VulkanSwapchainPresentationPipelineGeneration&& other) noexcept :
    mLogicalDeviceGeneration(std::exchange(other.mLogicalDeviceGeneration, nullptr)),
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
    mDrawableExtent(std::exchange(other.mDrawableExtent, {})),
    mSwapchain(std::exchange(other.mSwapchain, VK_NULL_HANDLE)),
    mImageFormat(std::exchange(other.mImageFormat, VK_FORMAT_UNDEFINED)),
    mImageExtent(std::exchange(other.mImageExtent, {})),
    mImageCount(std::exchange(other.mImageCount, 0)),
    mRenderPass(std::exchange(other.mRenderPass, VK_NULL_HANDLE)),
    mPipelineLayout(std::exchange(other.mPipelineLayout, VK_NULL_HANDLE)),
    mPipeline(std::exchange(other.mPipeline, VK_NULL_HANDLE)),
    mDestroyPipelineLayout(std::exchange(other.mDestroyPipelineLayout, nullptr)),
    mDestroyPipeline(std::exchange(other.mDestroyPipeline, nullptr))
{
}

bool VulkanSwapchainPresentationPipelineGeneration::createdFor(
    const VulkanLogicalDeviceGeneration&               logical_device_generation,
    const VulkanSwapchainConfigurationGeneration&      configuration_generation,
    const VulkanSwapchainGeneration&                   swapchain_generation,
    const VulkanSwapchainImagesGeneration&             images_generation,
    const VulkanSwapchainPresentationTargetGeneration& presentation_target_generation) const noexcept
{
    const VkExtent2D drawable_extent = configuration_generation.drawableExtent();
    const VkExtent2D image_extent    = configuration_generation.imageExtent();
    return mPipelineLayout != VK_NULL_HANDLE && mPipeline != VK_NULL_HANDLE && mLogicalDeviceGeneration == &logical_device_generation &&
           mConfigurationGeneration == &configuration_generation && mSwapchainGeneration == &swapchain_generation &&
           mImagesGeneration == &images_generation && mPresentationTargetGeneration == &presentation_target_generation &&
           presentation_target_generation.createdFor(logical_device_generation, configuration_generation, swapchain_generation,
                                                     images_generation) &&
           mGetInstanceProcAddr == logical_device_generation.getInstanceProcAddr() && mInstance == logical_device_generation.instance() &&
           mSurface == logical_device_generation.surface() && mPhysicalDevice == logical_device_generation.physicalDevice() &&
           mPhysicalDeviceIndex == logical_device_generation.physicalDeviceIndex() && mDevice == logical_device_generation.device() &&
           mQueue == logical_device_generation.queue() && mQueueFamilyIndex == logical_device_generation.queueFamilyIndex() &&
           mQueueIndex == logical_device_generation.queueIndex() && mDrawableExtent.width == drawable_extent.width &&
           mDrawableExtent.height == drawable_extent.height && mSwapchain == swapchain_generation.swapchain() &&
           mImageFormat == configuration_generation.surfaceFormat().format && mImageFormat == images_generation.imageFormat() &&
           mImageExtent.width == image_extent.width && mImageExtent.height == image_extent.height &&
           mImageCount == images_generation.imageCount() && mRenderPass == presentation_target_generation.renderPass();
}

void VulkanSwapchainPresentationPipelineGeneration::reset() noexcept
{
    if (mPipeline != VK_NULL_HANDLE && mDestroyPipeline)
    {
        mDestroyPipeline(mDevice, mPipeline, nullptr);
    }
    mPipeline = VK_NULL_HANDLE;
    if (mPipelineLayout != VK_NULL_HANDLE && mDestroyPipelineLayout)
    {
        mDestroyPipelineLayout(mDevice, mPipelineLayout, nullptr);
    }
    mPipelineLayout               = VK_NULL_HANDLE;
    mLogicalDeviceGeneration      = nullptr;
    mConfigurationGeneration      = nullptr;
    mSwapchainGeneration          = nullptr;
    mImagesGeneration             = nullptr;
    mPresentationTargetGeneration = nullptr;
    mGetInstanceProcAddr          = nullptr;
    mInstance                     = VK_NULL_HANDLE;
    mSurface                      = VK_NULL_HANDLE;
    mPhysicalDevice               = VK_NULL_HANDLE;
    mPhysicalDeviceIndex          = 0;
    mDevice                       = VK_NULL_HANDLE;
    mQueue                        = VK_NULL_HANDLE;
    mQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    mQueueIndex                   = 0;
    mDrawableExtent               = {};
    mSwapchain                    = VK_NULL_HANDLE;
    mImageFormat                  = VK_FORMAT_UNDEFINED;
    mImageExtent                  = {};
    mImageCount                   = 0;
    mRenderPass                   = VK_NULL_HANDLE;
    mDestroyPipelineLayout        = nullptr;
    mDestroyPipeline              = nullptr;
}

VulkanSwapchainPresentationPipelineResolutionResult resolveVulkanSwapchainPresentationPipelineGeneration(
    const VulkanLogicalDeviceGeneration&               logical_device_generation,
    const VulkanSwapchainConfigurationGeneration&      configuration_generation,
    const VulkanSwapchainGeneration&                   swapchain_generation,
    const VulkanSwapchainImagesGeneration&             images_generation,
    const VulkanSwapchainPresentationTargetGeneration& presentation_target_generation) noexcept
{
    if (!valid(logical_device_generation))
    {
        return failure(VulkanSwapchainPresentationPipelineResolutionCode::InvalidLogicalDeviceGeneration);
    }
    if (!belongsTo(configuration_generation, logical_device_generation))
    {
        return failure(VulkanSwapchainPresentationPipelineResolutionCode::InvalidSwapchainConfigurationGeneration);
    }
    if (!swapchain_generation.createdFor(logical_device_generation, configuration_generation))
    {
        return failure(VulkanSwapchainPresentationPipelineResolutionCode::InvalidSwapchainGeneration);
    }
    if (!images_generation.createdFor(logical_device_generation, configuration_generation, swapchain_generation))
    {
        return failure(VulkanSwapchainPresentationPipelineResolutionCode::InvalidSwapchainImagesGeneration);
    }
    if (!presentation_target_generation.createdFor(logical_device_generation, configuration_generation, swapchain_generation,
                                                   images_generation))
    {
        return failure(VulkanSwapchainPresentationPipelineResolutionCode::InvalidSwapchainPresentationTargetGeneration);
    }

    PresentationPipelineDispatch dispatch;
    if (auto error = resolveDispatch(logical_device_generation, dispatch))
    {
        return *error;
    }

    const VkDevice device = logical_device_generation.device();

    VkPipelineLayoutCreateInfo pipeline_layout_info{};
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

    VkPipelineLayout pipeline_layout        = VK_NULL_HANDLE;
    const VkResult   pipeline_layout_result = dispatch.mCreatePipelineLayout(device, &pipeline_layout_info, nullptr, &pipeline_layout);
    if (pipeline_layout_result != VK_SUCCESS)
    {
        return failure(VulkanSwapchainPresentationPipelineResolutionCode::PipelineLayoutCreationFailure,
                       VulkanSwapchainPresentationPipelineCommand::CreatePipelineLayout, pipeline_layout_result);
    }
    if (pipeline_layout == VK_NULL_HANDLE)
    {
        return failure(VulkanSwapchainPresentationPipelineResolutionCode::NullPipelineLayoutOnSuccess,
                       VulkanSwapchainPresentationPipelineCommand::CreatePipelineLayout);
    }

    VkShaderModuleCreateInfo vertex_shader_info{};
    vertex_shader_info.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vertex_shader_info.codeSize = PRESENTATION_VERTEX_SHADER.size() * sizeof(std::uint32_t);
    vertex_shader_info.pCode    = PRESENTATION_VERTEX_SHADER.data();

    VkShaderModule vertex_shader        = VK_NULL_HANDLE;
    const VkResult vertex_shader_result = dispatch.mCreateShaderModule(device, &vertex_shader_info, nullptr, &vertex_shader);
    if (vertex_shader_result != VK_SUCCESS)
    {
        dispatch.mDestroyPipelineLayout(device, pipeline_layout, nullptr);
        return failure(VulkanSwapchainPresentationPipelineResolutionCode::VertexShaderModuleCreationFailure,
                       VulkanSwapchainPresentationPipelineCommand::CreateShaderModule, vertex_shader_result);
    }
    if (vertex_shader == VK_NULL_HANDLE)
    {
        dispatch.mDestroyPipelineLayout(device, pipeline_layout, nullptr);
        return failure(VulkanSwapchainPresentationPipelineResolutionCode::NullVertexShaderModuleOnSuccess,
                       VulkanSwapchainPresentationPipelineCommand::CreateShaderModule);
    }

    VkShaderModuleCreateInfo fragment_shader_info{};
    fragment_shader_info.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    fragment_shader_info.codeSize = PRESENTATION_FRAGMENT_SHADER.size() * sizeof(std::uint32_t);
    fragment_shader_info.pCode    = PRESENTATION_FRAGMENT_SHADER.data();

    VkShaderModule fragment_shader        = VK_NULL_HANDLE;
    const VkResult fragment_shader_result = dispatch.mCreateShaderModule(device, &fragment_shader_info, nullptr, &fragment_shader);
    if (fragment_shader_result != VK_SUCCESS)
    {
        dispatch.mDestroyShaderModule(device, vertex_shader, nullptr);
        dispatch.mDestroyPipelineLayout(device, pipeline_layout, nullptr);
        return failure(VulkanSwapchainPresentationPipelineResolutionCode::FragmentShaderModuleCreationFailure,
                       VulkanSwapchainPresentationPipelineCommand::CreateShaderModule, fragment_shader_result);
    }
    if (fragment_shader == VK_NULL_HANDLE)
    {
        dispatch.mDestroyShaderModule(device, vertex_shader, nullptr);
        dispatch.mDestroyPipelineLayout(device, pipeline_layout, nullptr);
        return failure(VulkanSwapchainPresentationPipelineResolutionCode::NullFragmentShaderModuleOnSuccess,
                       VulkanSwapchainPresentationPipelineCommand::CreateShaderModule);
    }

    const std::array<VkPipelineShaderStageCreateInfo, 2> shader_stages{
        VkPipelineShaderStageCreateInfo{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT,
                                         vertex_shader, "main", nullptr },
        VkPipelineShaderStageCreateInfo{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT,
                                         fragment_shader, "main", nullptr }
    };

    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

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
    rasterization.lineWidth               = 1.0f;

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
    pipeline_info.renderPass          = presentation_target_generation.renderPass();
    pipeline_info.subpass             = 0;
    pipeline_info.basePipelineHandle  = VK_NULL_HANDLE;
    pipeline_info.basePipelineIndex   = -1;

    VkPipeline     pipeline        = VK_NULL_HANDLE;
    const VkResult pipeline_result = dispatch.mCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline);
    if (pipeline_result != VK_SUCCESS)
    {
        // Unlike single-object creation, Vulkan permits aggregate pipeline
        // creation to return non-null partial handles on overall failure.
        rollback(device, dispatch, pipeline, vertex_shader, fragment_shader, pipeline_layout);
        return failure(VulkanSwapchainPresentationPipelineResolutionCode::GraphicsPipelineCreationFailure,
                       VulkanSwapchainPresentationPipelineCommand::CreateGraphicsPipelines, pipeline_result);
    }
    if (pipeline == VK_NULL_HANDLE)
    {
        rollback(device, dispatch, pipeline, vertex_shader, fragment_shader, pipeline_layout);
        return failure(VulkanSwapchainPresentationPipelineResolutionCode::NullGraphicsPipelineOnSuccess,
                       VulkanSwapchainPresentationPipelineCommand::CreateGraphicsPipelines);
    }

    destroyShaderModules(device, dispatch.mDestroyShaderModule, vertex_shader, fragment_shader);

    if (!parentsAreCurrent(logical_device_generation, configuration_generation, swapchain_generation, images_generation,
                           presentation_target_generation))
    {
        dispatch.mDestroyPipeline(device, pipeline, nullptr);
        dispatch.mDestroyPipelineLayout(device, pipeline_layout, nullptr);
        return failure(VulkanSwapchainPresentationPipelineResolutionCode::ParentGenerationChanged);
    }

    return VulkanSwapchainPresentationPipelineGenerationFactory::create(
        logical_device_generation, configuration_generation, swapchain_generation, images_generation, presentation_target_generation,
        pipeline_layout, pipeline, dispatch.mDestroyPipelineLayout, dispatch.mDestroyPipeline);
}

} // namespace LLRenderVulkan
