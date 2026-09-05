/**
 * @file llrendervulkanswapchainimages.h
 * @brief Loader-neutral Vulkan swapchain image-view ownership.
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

#ifndef LL_LLRENDERVULKANSWAPCHAINIMAGES_H
#define LL_LLRENDERVULKANSWAPCHAINIMAGES_H

#include "llrendervulkanswapchain.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <variant>

namespace LLRenderVulkan
{

inline constexpr std::uint32_t VULKAN_SWAPCHAIN_MAX_IMAGES                 = 4096;
inline constexpr std::uint32_t VULKAN_SWAPCHAIN_IMAGE_ENUMERATION_ATTEMPTS = 4;

enum class VulkanSwapchainImagesCommand : std::uint8_t
{
    GetDeviceProcAddr,
    GetSwapchainImages,
    CreateImageView,
    DestroyImageView
};

enum class VulkanSwapchainImagesResolutionCode : std::uint8_t
{
    InvalidLogicalDeviceGeneration,
    InvalidSwapchainConfigurationGeneration,
    InvalidSwapchainGeneration,
    MissingRequiredCommand,
    SwapchainImageEnumerationFailure,
    SwapchainImageCountExceeded,
    SwapchainImageEnumerationRetryLimitExceeded,
    InvalidSwapchainImageEnumerationOutput,
    ImageViewCreationFailure,
    NullImageViewOnSuccess,
    ScratchAllocationFailure
};

struct VulkanSwapchainImagesResolutionError
{
    VulkanSwapchainImagesResolutionCode         mCode = VulkanSwapchainImagesResolutionCode::InvalidLogicalDeviceGeneration;
    std::optional<VulkanSwapchainImagesCommand> mCommand;
    VkResult                                    mResult             = VK_SUCCESS;
    std::uint32_t                               mObservedCount      = 0;
    std::uint32_t                               mEnumerationAttempt = 0;
    std::uint32_t                               mImageIndex         = 0;

    friend constexpr bool operator==(const VulkanSwapchainImagesResolutionError&, const VulkanSwapchainImagesResolutionError&) = default;
};

// Swapchain images are borrowed. This generation owns exactly one 2D color
// image view for every image and must be reset before its swapchain parent.
class VulkanSwapchainImagesGeneration
{
public:
    ~VulkanSwapchainImagesGeneration() noexcept;

    VulkanSwapchainImagesGeneration(const VulkanSwapchainImagesGeneration&)            = delete;
    VulkanSwapchainImagesGeneration& operator=(const VulkanSwapchainImagesGeneration&) = delete;
    VulkanSwapchainImagesGeneration(VulkanSwapchainImagesGeneration&& other) noexcept;
    VulkanSwapchainImagesGeneration& operator=(VulkanSwapchainImagesGeneration&&) = delete;

    std::uint32_t imageCount() const noexcept { return mImageCount; }
    VkImage       image(std::uint32_t index) const noexcept;
    VkImageView   imageView(std::uint32_t index) const noexcept;
    VkFormat      imageFormat() const noexcept { return mImageFormat; }

    bool createdFor(const VulkanLogicalDeviceGeneration&          logical_device_generation,
                    const VulkanSwapchainConfigurationGeneration& configuration_generation,
                    const VulkanSwapchainGeneration&              swapchain_generation) const noexcept;

    void reset() noexcept;

private:
    friend struct VulkanSwapchainImagesGenerationFactory;

    VulkanSwapchainImagesGeneration(const VulkanLogicalDeviceGeneration&          logical_device_generation,
                                    const VulkanSwapchainConfigurationGeneration& configuration_generation,
                                    const VulkanSwapchainGeneration&              swapchain_generation,
                                    std::uint32_t                                 image_count,
                                    std::unique_ptr<VkImage[]>
                                        images,
                                    std::unique_ptr<VkImageView[]>
                                                           image_views,
                                    PFN_vkDestroyImageView destroy_image_view) noexcept;

    PFN_vkGetInstanceProcAddr      mGetInstanceProcAddr = nullptr;
    VkInstance                     mInstance            = VK_NULL_HANDLE;
    VkPhysicalDevice               mPhysicalDevice      = VK_NULL_HANDLE;
    std::uint32_t                  mPhysicalDeviceIndex = 0;
    VkDevice                       mDevice              = VK_NULL_HANDLE;
    std::uint32_t                  mQueueFamilyIndex    = VK_QUEUE_FAMILY_IGNORED;
    VkExtent2D                     mDrawableExtent{};
    VkSwapchainKHR                 mSwapchain   = VK_NULL_HANDLE;
    VkFormat                       mImageFormat = VK_FORMAT_UNDEFINED;
    std::uint32_t                  mImageCount  = 0;
    std::unique_ptr<VkImage[]>     mImages;
    std::unique_ptr<VkImageView[]> mImageViews;
    PFN_vkDestroyImageView         mDestroyImageView = nullptr;
};

using VulkanSwapchainImagesResolutionResult = std::variant<VulkanSwapchainImagesResolutionError, VulkanSwapchainImagesGeneration>;

VulkanSwapchainImagesResolutionResult resolveVulkanSwapchainImagesGeneration(
    const VulkanLogicalDeviceGeneration&          logical_device_generation,
    const VulkanSwapchainConfigurationGeneration& configuration_generation,
    const VulkanSwapchainGeneration&              swapchain_generation) noexcept;

namespace VulkanSwapchainImagesDetail
{

    using AllocationCheckpoint = void (*)();

    VulkanSwapchainImagesResolutionResult resolve(const VulkanLogicalDeviceGeneration&          logical_device_generation,
                                                  const VulkanSwapchainConfigurationGeneration& configuration_generation,
                                                  const VulkanSwapchainGeneration&              swapchain_generation,
                                                  AllocationCheckpoint                          allocation_checkpoint) noexcept;

} // namespace VulkanSwapchainImagesDetail

} // namespace LLRenderVulkan

#endif // LL_LLRENDERVULKANSWAPCHAINIMAGES_H
