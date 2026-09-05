/**
 * @file llrendervulkanswapchainpresentationtarget.h
 * @brief Loader-neutral Vulkan swapchain presentation-target ownership.
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

#ifndef LL_LLRENDERVULKANSWAPCHAINPRESENTATIONTARGET_H
#define LL_LLRENDERVULKANSWAPCHAINPRESENTATIONTARGET_H

#include "llrendervulkanswapchainimages.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <variant>

namespace LLRenderVulkan
{

enum class VulkanSwapchainPresentationTargetCommand : std::uint8_t
{
    GetDeviceProcAddr,
    CreateRenderPass,
    DestroyRenderPass,
    CreateFramebuffer,
    DestroyFramebuffer
};

enum class VulkanSwapchainPresentationTargetResolutionCode : std::uint8_t
{
    InvalidLogicalDeviceGeneration,
    InvalidSwapchainConfigurationGeneration,
    InvalidSwapchainGeneration,
    InvalidSwapchainImagesGeneration,
    InvalidSwapchainImageView,
    MissingRequiredCommand,
    ScratchAllocationFailure,
    RenderPassCreationFailure,
    NullRenderPassOnSuccess,
    FramebufferCreationFailure,
    NullFramebufferOnSuccess
};

struct VulkanSwapchainPresentationTargetResolutionError
{
    VulkanSwapchainPresentationTargetResolutionCode         mCode =
        VulkanSwapchainPresentationTargetResolutionCode::InvalidLogicalDeviceGeneration;
    std::optional<VulkanSwapchainPresentationTargetCommand> mCommand;
    VkResult                                                mResult     = VK_SUCCESS;
    std::uint32_t                                           mImageCount = 0;
    std::uint32_t                                           mImageIndex = 0;

    friend constexpr bool operator==(const VulkanSwapchainPresentationTargetResolutionError&,
                                     const VulkanSwapchainPresentationTargetResolutionError&) = default;
};

// This generation owns one render pass and one framebuffer for every image
// view in its exact swapchain-images parent. It must be reset before that
// parent destroys or moves any image view. Submitted users must finish before
// reset or destruction. Host access is externally synchronized.
class VulkanSwapchainPresentationTargetGeneration
{
public:
    ~VulkanSwapchainPresentationTargetGeneration() noexcept;

    VulkanSwapchainPresentationTargetGeneration(const VulkanSwapchainPresentationTargetGeneration&) = delete;
    VulkanSwapchainPresentationTargetGeneration& operator=(const VulkanSwapchainPresentationTargetGeneration&) = delete;
    VulkanSwapchainPresentationTargetGeneration(VulkanSwapchainPresentationTargetGeneration&& other) noexcept;
    VulkanSwapchainPresentationTargetGeneration& operator=(VulkanSwapchainPresentationTargetGeneration&&) = delete;

    VkRenderPass   renderPass() const noexcept { return mRenderPass; }
    std::uint32_t  framebufferCount() const noexcept { return mImageCount; }
    VkFramebuffer framebuffer(std::uint32_t index) const noexcept;
    VkFormat      imageFormat() const noexcept { return mImageFormat; }
    VkExtent2D    imageExtent() const noexcept { return mImageExtent; }

    bool createdFor(const VulkanLogicalDeviceGeneration&          logical_device_generation,
                    const VulkanSwapchainConfigurationGeneration& configuration_generation,
                    const VulkanSwapchainGeneration&              swapchain_generation,
                    const VulkanSwapchainImagesGeneration&        images_generation) const noexcept;

    void reset() noexcept;

private:
    friend struct VulkanSwapchainPresentationTargetGenerationFactory;

    VulkanSwapchainPresentationTargetGeneration(const VulkanLogicalDeviceGeneration&          logical_device_generation,
                                                const VulkanSwapchainConfigurationGeneration& configuration_generation,
                                                const VulkanSwapchainGeneration&              swapchain_generation,
                                                const VulkanSwapchainImagesGeneration&        images_generation,
                                                VkRenderPass                                  render_pass,
                                                std::unique_ptr<VkFramebuffer[]>               framebuffers,
                                                PFN_vkDestroyRenderPass                       destroy_render_pass,
                                                PFN_vkDestroyFramebuffer                      destroy_framebuffer) noexcept;

    const VulkanLogicalDeviceGeneration*          mLogicalDeviceGeneration = nullptr;
    const VulkanSwapchainConfigurationGeneration* mConfigurationGeneration = nullptr;
    const VulkanSwapchainGeneration*              mSwapchainGeneration     = nullptr;
    const VulkanSwapchainImagesGeneration*        mImagesGeneration        = nullptr;
    PFN_vkGetInstanceProcAddr                     mGetInstanceProcAddr      = nullptr;
    VkInstance                                    mInstance                 = VK_NULL_HANDLE;
    VkSurfaceKHR                                  mSurface                  = VK_NULL_HANDLE;
    VkPhysicalDevice                              mPhysicalDevice           = VK_NULL_HANDLE;
    std::uint32_t                                 mPhysicalDeviceIndex      = 0;
    VkDevice                                      mDevice                   = VK_NULL_HANDLE;
    VkQueue                                       mQueue                    = VK_NULL_HANDLE;
    std::uint32_t                                 mQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
    std::uint32_t                                 mQueueIndex               = 0;
    VkExtent2D                                    mDrawableExtent{};
    VkSwapchainKHR                                mSwapchain   = VK_NULL_HANDLE;
    VkFormat                                      mImageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D                                    mImageExtent{};
    std::uint32_t                                 mImageCount = 0;
    VkRenderPass                                  mRenderPass = VK_NULL_HANDLE;
    std::unique_ptr<VkFramebuffer[]>              mFramebuffers;
    PFN_vkDestroyRenderPass                       mDestroyRenderPass  = nullptr;
    PFN_vkDestroyFramebuffer                      mDestroyFramebuffer = nullptr;
};

using VulkanSwapchainPresentationTargetResolutionResult =
    std::variant<VulkanSwapchainPresentationTargetResolutionError, VulkanSwapchainPresentationTargetGeneration>;

VulkanSwapchainPresentationTargetResolutionResult resolveVulkanSwapchainPresentationTargetGeneration(
    const VulkanLogicalDeviceGeneration&          logical_device_generation,
    const VulkanSwapchainConfigurationGeneration& configuration_generation,
    const VulkanSwapchainGeneration&              swapchain_generation,
    const VulkanSwapchainImagesGeneration&        images_generation) noexcept;

namespace VulkanSwapchainPresentationTargetDetail
{

    using AllocationCheckpoint = void (*)();

    VulkanSwapchainPresentationTargetResolutionResult resolve(
        const VulkanLogicalDeviceGeneration&          logical_device_generation,
        const VulkanSwapchainConfigurationGeneration& configuration_generation,
        const VulkanSwapchainGeneration&              swapchain_generation,
        const VulkanSwapchainImagesGeneration&        images_generation,
        AllocationCheckpoint                          allocation_checkpoint) noexcept;

} // namespace VulkanSwapchainPresentationTargetDetail

} // namespace LLRenderVulkan

#endif // LL_LLRENDERVULKANSWAPCHAINPRESENTATIONTARGET_H
