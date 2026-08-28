/**
 * @file llrendervulkanswapchainconfiguration.h
 * @brief Loader-neutral Vulkan swapchain configuration selection.
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

#ifndef LL_LLRENDERVULKANSWAPCHAINCONFIGURATION_H
#define LL_LLRENDERVULKANSWAPCHAINCONFIGURATION_H

#include "llrendervulkanlogicaldevice.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>
#include <variant>

namespace LLRenderVulkan
{

inline constexpr std::uint32_t VULKAN_SWAPCHAIN_MAX_SURFACE_FORMATS = 4096;
inline constexpr std::uint32_t VULKAN_SWAPCHAIN_MAX_PRESENT_MODES   = 256;

enum class VulkanSwapchainConfigurationCommand : std::uint8_t
{
    GetPhysicalDeviceSurfaceCapabilities,
    GetPhysicalDeviceSurfaceFormats,
    GetPhysicalDeviceSurfacePresentModes
};

enum class VulkanSwapchainConfigurationResolutionCode : std::uint8_t
{
    InvalidPhysicalDeviceGeneration,
    InvalidLogicalDeviceGeneration,
    InvalidDrawableExtent,
    MissingRequiredCommand,
    SurfaceCapabilitiesQueryFailure,
    InvalidImageCountRange,
    InvalidExtentRange,
    SurfaceUnavailable,
    InvalidArrayLayerCount,
    InvalidCurrentTransform,
    MissingCompositeAlpha,
    ColorAttachmentUsageUnsupported,
    SurfaceFormatEnumerationFailure,
    SurfaceFormatCountExceeded,
    SurfaceFormatEnumerationRetryLimitExceeded,
    InvalidSurfaceFormatEnumerationOutput,
    NoCompatibleSurfaceFormat,
    PresentModeEnumerationFailure,
    PresentModeCountExceeded,
    PresentModeEnumerationRetryLimitExceeded,
    InvalidPresentModeEnumerationOutput,
    FifoPresentModeUnsupported,
    ScratchAllocationFailure
};

struct VulkanSwapchainConfigurationResolutionError
{
    VulkanSwapchainConfigurationResolutionCode         mCode = VulkanSwapchainConfigurationResolutionCode::InvalidPhysicalDeviceGeneration;
    std::optional<VulkanSwapchainConfigurationCommand> mCommand;
    VkResult                                           mResult             = VK_SUCCESS;
    std::uint32_t                                      mObservedCount      = 0;
    std::uint32_t                                      mEnumerationAttempt = 0;

    friend constexpr bool operator==(const VulkanSwapchainConfigurationResolutionError&,
                                     const VulkanSwapchainConfigurationResolutionError&) = default;
};

// This immutable generation owns no Vulkan object. It records one create-ready
// configuration for the exact physical device, logical device, surface, and
// drawable extent. Those parents must outlive it.
class VulkanSwapchainConfigurationGeneration
{
public:
    VulkanSwapchainConfigurationGeneration(const VulkanSwapchainConfigurationGeneration&)            = delete;
    VulkanSwapchainConfigurationGeneration& operator=(const VulkanSwapchainConfigurationGeneration&) = delete;
    VulkanSwapchainConfigurationGeneration(VulkanSwapchainConfigurationGeneration&& other) noexcept;
    VulkanSwapchainConfigurationGeneration& operator=(VulkanSwapchainConfigurationGeneration&&) = delete;

    PFN_vkGetInstanceProcAddr       getInstanceProcAddr() const noexcept { return mGetInstanceProcAddr; }
    VkInstance                      instance() const noexcept { return mInstance; }
    VkSurfaceKHR                    surface() const noexcept { return mSurface; }
    VkPhysicalDevice                physicalDevice() const noexcept { return mPhysicalDevice; }
    std::uint32_t                   physicalDeviceIndex() const noexcept { return mPhysicalDeviceIndex; }
    VkDevice                        device() const noexcept { return mDevice; }
    std::uint32_t                   queueFamilyIndex() const noexcept { return mQueueFamilyIndex; }
    VkExtent2D                      drawableExtent() const noexcept { return mDrawableExtent; }
    const VkSurfaceCapabilitiesKHR& surfaceCapabilities() const noexcept { return mSurfaceCapabilities; }
    VkSurfaceFormatKHR              surfaceFormat() const noexcept { return mSurfaceFormat; }
    VkPresentModeKHR                presentMode() const noexcept { return mPresentMode; }
    std::uint32_t                   imageCount() const noexcept { return mImageCount; }
    VkExtent2D                      imageExtent() const noexcept { return mImageExtent; }
    std::uint32_t                   imageArrayLayers() const noexcept { return 1; }
    VkImageUsageFlags               imageUsage() const noexcept { return VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; }
    VkSharingMode                   imageSharingMode() const noexcept { return VK_SHARING_MODE_EXCLUSIVE; }
    VkSurfaceTransformFlagBitsKHR   preTransform() const noexcept { return mPreTransform; }
    VkCompositeAlphaFlagBitsKHR     compositeAlpha() const noexcept { return mCompositeAlpha; }
    VkBool32                        clipped() const noexcept { return VK_TRUE; }

    bool createdFor(const VulkanPhysicalDeviceGeneration& physical_device_generation,
                    const VulkanLogicalDeviceGeneration&  logical_device_generation,
                    VkExtent2D                            drawable_extent) const noexcept;

private:
    friend struct VulkanSwapchainConfigurationGenerationFactory;

    VulkanSwapchainConfigurationGeneration(const VulkanPhysicalDeviceGeneration& physical_device_generation,
                                           const VulkanLogicalDeviceGeneration&  logical_device_generation,
                                           VkExtent2D                            drawable_extent,
                                           const VkSurfaceCapabilitiesKHR&       surface_capabilities,
                                           VkSurfaceFormatKHR                    surface_format,
                                           VkPresentModeKHR                      present_mode,
                                           std::uint32_t                         image_count,
                                           VkExtent2D                            image_extent,
                                           VkSurfaceTransformFlagBitsKHR         pre_transform,
                                           VkCompositeAlphaFlagBitsKHR           composite_alpha) noexcept;

    PFN_vkGetInstanceProcAddr     mGetInstanceProcAddr = nullptr;
    VkInstance                    mInstance            = VK_NULL_HANDLE;
    VkSurfaceKHR                  mSurface             = VK_NULL_HANDLE;
    VkPhysicalDevice              mPhysicalDevice      = VK_NULL_HANDLE;
    std::uint32_t                 mPhysicalDeviceIndex = 0;
    VkDevice                      mDevice              = VK_NULL_HANDLE;
    std::uint32_t                 mQueueFamilyIndex    = VK_QUEUE_FAMILY_IGNORED;
    VkExtent2D                    mDrawableExtent{};
    VkSurfaceCapabilitiesKHR      mSurfaceCapabilities{};
    VkSurfaceFormatKHR            mSurfaceFormat{};
    VkPresentModeKHR              mPresentMode = VK_PRESENT_MODE_FIFO_KHR;
    std::uint32_t                 mImageCount  = 0;
    VkExtent2D                    mImageExtent{};
    VkSurfaceTransformFlagBitsKHR mPreTransform   = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    VkCompositeAlphaFlagBitsKHR   mCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
};

using VulkanSwapchainConfigurationResolutionResult =
    std::variant<VulkanSwapchainConfigurationResolutionError, VulkanSwapchainConfigurationGeneration>;

VulkanSwapchainConfigurationResolutionResult resolveVulkanSwapchainConfigurationGeneration(
    const VulkanPhysicalDeviceGeneration& physical_device_generation,
    const VulkanLogicalDeviceGeneration&  logical_device_generation,
    VkExtent2D                            drawable_extent) noexcept;

namespace VulkanSwapchainConfigurationDetail
{

    using AllocationCheckpoint = void (*)();

    VulkanSwapchainConfigurationResolutionResult resolve(const VulkanPhysicalDeviceGeneration& physical_device_generation,
                                                         const VulkanLogicalDeviceGeneration&  logical_device_generation,
                                                         VkExtent2D                            drawable_extent,
                                                         AllocationCheckpoint                  allocation_checkpoint) noexcept;

} // namespace VulkanSwapchainConfigurationDetail

} // namespace LLRenderVulkan

#endif // LL_LLRENDERVULKANSWAPCHAINCONFIGURATION_H
