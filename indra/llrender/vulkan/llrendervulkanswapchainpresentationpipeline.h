/**
 * @file llrendervulkanswapchainpresentationpipeline.h
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

#ifndef LL_LLRENDERVULKANSWAPCHAINPRESENTATIONPIPELINE_H
#define LL_LLRENDERVULKANSWAPCHAINPRESENTATIONPIPELINE_H

#include "llrendervulkanswapchainpresentationtarget.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>
#include <variant>

namespace LLRenderVulkan
{

enum class VulkanSwapchainPresentationPipelineCommand : std::uint8_t
{
    GetDeviceProcAddr,
    CreateShaderModule,
    DestroyShaderModule,
    CreatePipelineLayout,
    DestroyPipelineLayout,
    CreateGraphicsPipelines,
    DestroyPipeline
};

enum class VulkanSwapchainPresentationPipelineResolutionCode : std::uint8_t
{
    InvalidLogicalDeviceGeneration,
    InvalidSwapchainConfigurationGeneration,
    InvalidSwapchainGeneration,
    InvalidSwapchainImagesGeneration,
    InvalidSwapchainPresentationTargetGeneration,
    MissingRequiredCommand,
    VertexShaderModuleCreationFailure,
    NullVertexShaderModuleOnSuccess,
    FragmentShaderModuleCreationFailure,
    NullFragmentShaderModuleOnSuccess,
    PipelineLayoutCreationFailure,
    NullPipelineLayoutOnSuccess,
    GraphicsPipelineCreationFailure,
    NullGraphicsPipelineOnSuccess,
    ParentGenerationChanged
};

struct VulkanSwapchainPresentationPipelineResolutionError
{
    VulkanSwapchainPresentationPipelineResolutionCode mCode =
        VulkanSwapchainPresentationPipelineResolutionCode::InvalidLogicalDeviceGeneration;
    std::optional<VulkanSwapchainPresentationPipelineCommand> mCommand;
    VkResult                                                  mResult = VK_SUCCESS;

    friend constexpr bool operator==(const VulkanSwapchainPresentationPipelineResolutionError&,
                                     const VulkanSwapchainPresentationPipelineResolutionError&) = default;
};

// This generation owns one empty pipeline layout and one graphics pipeline for
// its exact presentation target. It must be reset before that target or any of
// its parents. Submitted users must finish first. Host access is externally
// synchronized.
class VulkanSwapchainPresentationPipelineGeneration
{
public:
    ~VulkanSwapchainPresentationPipelineGeneration() noexcept;

    VulkanSwapchainPresentationPipelineGeneration(const VulkanSwapchainPresentationPipelineGeneration&)            = delete;
    VulkanSwapchainPresentationPipelineGeneration& operator=(const VulkanSwapchainPresentationPipelineGeneration&) = delete;
    VulkanSwapchainPresentationPipelineGeneration(VulkanSwapchainPresentationPipelineGeneration&& other) noexcept;
    VulkanSwapchainPresentationPipelineGeneration& operator=(VulkanSwapchainPresentationPipelineGeneration&&) = delete;

    VkPipelineLayout pipelineLayout() const noexcept { return mPipelineLayout; }
    VkPipeline       pipeline() const noexcept { return mPipeline; }

    bool createdFor(const VulkanLogicalDeviceGeneration&               logical_device_generation,
                    const VulkanSwapchainConfigurationGeneration&      configuration_generation,
                    const VulkanSwapchainGeneration&                   swapchain_generation,
                    const VulkanSwapchainImagesGeneration&             images_generation,
                    const VulkanSwapchainPresentationTargetGeneration& presentation_target_generation) const noexcept;

    void reset() noexcept;

private:
    friend struct VulkanSwapchainPresentationPipelineGenerationFactory;

    VulkanSwapchainPresentationPipelineGeneration(const VulkanLogicalDeviceGeneration&               logical_device_generation,
                                                  const VulkanSwapchainConfigurationGeneration&      configuration_generation,
                                                  const VulkanSwapchainGeneration&                   swapchain_generation,
                                                  const VulkanSwapchainImagesGeneration&             images_generation,
                                                  const VulkanSwapchainPresentationTargetGeneration& presentation_target_generation,
                                                  VkPipelineLayout                                   pipeline_layout,
                                                  VkPipeline                                         pipeline,
                                                  PFN_vkDestroyPipelineLayout                        destroy_pipeline_layout,
                                                  PFN_vkDestroyPipeline                              destroy_pipeline) noexcept;

    const VulkanLogicalDeviceGeneration*               mLogicalDeviceGeneration      = nullptr;
    const VulkanSwapchainConfigurationGeneration*      mConfigurationGeneration      = nullptr;
    const VulkanSwapchainGeneration*                   mSwapchainGeneration          = nullptr;
    const VulkanSwapchainImagesGeneration*             mImagesGeneration             = nullptr;
    const VulkanSwapchainPresentationTargetGeneration* mPresentationTargetGeneration = nullptr;
    PFN_vkGetInstanceProcAddr                          mGetInstanceProcAddr          = nullptr;
    VkInstance                                         mInstance                     = VK_NULL_HANDLE;
    VkSurfaceKHR                                       mSurface                      = VK_NULL_HANDLE;
    VkPhysicalDevice                                   mPhysicalDevice               = VK_NULL_HANDLE;
    std::uint32_t                                      mPhysicalDeviceIndex          = 0;
    VkDevice                                           mDevice                       = VK_NULL_HANDLE;
    VkQueue                                            mQueue                        = VK_NULL_HANDLE;
    std::uint32_t                                      mQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    std::uint32_t                                      mQueueIndex                   = 0;
    VkExtent2D                                         mDrawableExtent{};
    VkSwapchainKHR                                     mSwapchain   = VK_NULL_HANDLE;
    VkFormat                                           mImageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D                                         mImageExtent{};
    std::uint32_t                                      mImageCount            = 0;
    VkRenderPass                                       mRenderPass            = VK_NULL_HANDLE;
    VkPipelineLayout                                   mPipelineLayout        = VK_NULL_HANDLE;
    VkPipeline                                         mPipeline              = VK_NULL_HANDLE;
    PFN_vkDestroyPipelineLayout                        mDestroyPipelineLayout = nullptr;
    PFN_vkDestroyPipeline                              mDestroyPipeline       = nullptr;
};

using VulkanSwapchainPresentationPipelineResolutionResult =
    std::variant<VulkanSwapchainPresentationPipelineResolutionError, VulkanSwapchainPresentationPipelineGeneration>;

VulkanSwapchainPresentationPipelineResolutionResult resolveVulkanSwapchainPresentationPipelineGeneration(
    const VulkanLogicalDeviceGeneration&               logical_device_generation,
    const VulkanSwapchainConfigurationGeneration&      configuration_generation,
    const VulkanSwapchainGeneration&                   swapchain_generation,
    const VulkanSwapchainImagesGeneration&             images_generation,
    const VulkanSwapchainPresentationTargetGeneration& presentation_target_generation) noexcept;

} // namespace LLRenderVulkan

#endif // LL_LLRENDERVULKANSWAPCHAINPRESENTATIONPIPELINE_H
