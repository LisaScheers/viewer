/**
 * @file llrendervulkanswapchainconfiguration.cpp
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

#include "llrendervulkanswapchainconfiguration.h"

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <new>
#include <utility>

namespace LLRenderVulkan
{

namespace
{
    constexpr std::uint32_t ENUMERATION_ATTEMPTS = 4;

    struct SwapchainConfigurationDispatch
    {
        PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR mGetSurfaceCapabilities = nullptr;
        PFN_vkGetPhysicalDeviceSurfaceFormatsKHR      mGetSurfaceFormats      = nullptr;
        PFN_vkGetPhysicalDeviceSurfacePresentModesKHR mGetSurfacePresentModes = nullptr;
        PFN_vkGetPhysicalDeviceFormatProperties       mGetFormatProperties    = nullptr;
    };

    VulkanSwapchainConfigurationResolutionError failure(VulkanSwapchainConfigurationResolutionCode         code,
                                                        std::optional<VulkanSwapchainConfigurationCommand> command = std::nullopt,
                                                        VkResult                                           result  = VK_SUCCESS) noexcept
    {
        return { code, command, result, 0, 0 };
    }

    template<typename Function>
    Function resolve(PFN_vkGetInstanceProcAddr resolver, VkInstance instance, const char* name) noexcept
    {
        return reinterpret_cast<Function>(resolver(instance, name));
    }

    bool valid(const VulkanPhysicalDeviceGeneration& physical_device_generation) noexcept
    {
        const VkQueueFamilyProperties& queue_family = physical_device_generation.queueFamilyProperties();
        return physical_device_generation.getInstanceProcAddr() && physical_device_generation.instance() != VK_NULL_HANDLE &&
               physical_device_generation.surface() != VK_NULL_HANDLE && physical_device_generation.physicalDevice() != VK_NULL_HANDLE &&
               physical_device_generation.queueFamilyIndex() != VK_QUEUE_FAMILY_IGNORED && queue_family.queueCount > 0 &&
               (queue_family.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
    }

    std::optional<VulkanSwapchainConfigurationResolutionError> resolveDispatch(
        const VulkanPhysicalDeviceGeneration& physical_device_generation,
        SwapchainConfigurationDispatch&       dispatch) noexcept
    {
        const PFN_vkGetInstanceProcAddr resolver = physical_device_generation.getInstanceProcAddr();
        const VkInstance                instance = physical_device_generation.instance();

        dispatch.mGetSurfaceCapabilities =
            resolve<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(resolver, instance, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
        if (!dispatch.mGetSurfaceCapabilities)
        {
            return failure(VulkanSwapchainConfigurationResolutionCode::MissingRequiredCommand,
                           VulkanSwapchainConfigurationCommand::GetPhysicalDeviceSurfaceCapabilities);
        }

        dispatch.mGetSurfaceFormats =
            resolve<PFN_vkGetPhysicalDeviceSurfaceFormatsKHR>(resolver, instance, "vkGetPhysicalDeviceSurfaceFormatsKHR");
        if (!dispatch.mGetSurfaceFormats)
        {
            return failure(VulkanSwapchainConfigurationResolutionCode::MissingRequiredCommand,
                           VulkanSwapchainConfigurationCommand::GetPhysicalDeviceSurfaceFormats);
        }

        dispatch.mGetSurfacePresentModes =
            resolve<PFN_vkGetPhysicalDeviceSurfacePresentModesKHR>(resolver, instance, "vkGetPhysicalDeviceSurfacePresentModesKHR");
        if (!dispatch.mGetSurfacePresentModes)
        {
            return failure(VulkanSwapchainConfigurationResolutionCode::MissingRequiredCommand,
                           VulkanSwapchainConfigurationCommand::GetPhysicalDeviceSurfacePresentModes);
        }

        dispatch.mGetFormatProperties =
            resolve<PFN_vkGetPhysicalDeviceFormatProperties>(resolver, instance, "vkGetPhysicalDeviceFormatProperties");
        if (!dispatch.mGetFormatProperties)
        {
            return failure(VulkanSwapchainConfigurationResolutionCode::MissingRequiredCommand,
                           VulkanSwapchainConfigurationCommand::GetPhysicalDeviceFormatProperties);
        }
        return std::nullopt;
    }

    std::optional<VulkanSwapchainConfigurationResolutionError> validateCapabilities(const VkSurfaceCapabilitiesKHR& capabilities) noexcept
    {
        if (capabilities.minImageCount == 0 || (capabilities.maxImageCount != 0 && capabilities.maxImageCount < capabilities.minImageCount))
        {
            return failure(VulkanSwapchainConfigurationResolutionCode::InvalidImageCountRange,
                           VulkanSwapchainConfigurationCommand::GetPhysicalDeviceSurfaceCapabilities);
        }
        if (capabilities.minImageExtent.width > capabilities.maxImageExtent.width ||
            capabilities.minImageExtent.height > capabilities.maxImageExtent.height)
        {
            return failure(VulkanSwapchainConfigurationResolutionCode::InvalidExtentRange,
                           VulkanSwapchainConfigurationCommand::GetPhysicalDeviceSurfaceCapabilities);
        }

        const bool width_variable  = capabilities.currentExtent.width == std::numeric_limits<std::uint32_t>::max();
        const bool height_variable = capabilities.currentExtent.height == std::numeric_limits<std::uint32_t>::max();
        if (width_variable != height_variable)
        {
            return failure(VulkanSwapchainConfigurationResolutionCode::InvalidExtentRange,
                           VulkanSwapchainConfigurationCommand::GetPhysicalDeviceSurfaceCapabilities);
        }
        if (!width_variable && (capabilities.currentExtent.width == 0 || capabilities.currentExtent.height == 0 ||
                                capabilities.currentExtent.width < capabilities.minImageExtent.width ||
                                capabilities.currentExtent.height < capabilities.minImageExtent.height ||
                                capabilities.currentExtent.width > capabilities.maxImageExtent.width ||
                                capabilities.currentExtent.height > capabilities.maxImageExtent.height))
        {
            return failure(VulkanSwapchainConfigurationResolutionCode::SurfaceUnavailable,
                           VulkanSwapchainConfigurationCommand::GetPhysicalDeviceSurfaceCapabilities);
        }
        if (width_variable && (capabilities.maxImageExtent.width == 0 || capabilities.maxImageExtent.height == 0))
        {
            return failure(VulkanSwapchainConfigurationResolutionCode::SurfaceUnavailable,
                           VulkanSwapchainConfigurationCommand::GetPhysicalDeviceSurfaceCapabilities);
        }
        if (capabilities.maxImageArrayLayers < 1)
        {
            return failure(VulkanSwapchainConfigurationResolutionCode::InvalidArrayLayerCount,
                           VulkanSwapchainConfigurationCommand::GetPhysicalDeviceSurfaceCapabilities);
        }

        const auto current_transform = static_cast<VkSurfaceTransformFlagsKHR>(capabilities.currentTransform);
        if (current_transform == 0 || (current_transform & (current_transform - 1)) != 0 ||
            (capabilities.supportedTransforms & current_transform) == 0)
        {
            return failure(VulkanSwapchainConfigurationResolutionCode::InvalidCurrentTransform,
                           VulkanSwapchainConfigurationCommand::GetPhysicalDeviceSurfaceCapabilities);
        }
        if (capabilities.supportedCompositeAlpha == 0)
        {
            return failure(VulkanSwapchainConfigurationResolutionCode::MissingCompositeAlpha,
                           VulkanSwapchainConfigurationCommand::GetPhysicalDeviceSurfaceCapabilities);
        }
        if ((capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0)
        {
            return failure(VulkanSwapchainConfigurationResolutionCode::ColorAttachmentUsageUnsupported,
                           VulkanSwapchainConfigurationCommand::GetPhysicalDeviceSurfaceCapabilities);
        }
        if ((capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0)
        {
            return failure(VulkanSwapchainConfigurationResolutionCode::SurfaceTransferDestinationUsageUnsupported,
                           VulkanSwapchainConfigurationCommand::GetPhysicalDeviceSurfaceCapabilities);
        }
        return std::nullopt;
    }

    std::variant<VulkanSwapchainConfigurationResolutionError, VkSurfaceFormatKHR> selectSurfaceFormat(
        const SwapchainConfigurationDispatch&                    dispatch,
        VkPhysicalDevice                                         physical_device,
        VkSurfaceKHR                                             surface,
        VulkanSwapchainConfigurationDetail::AllocationCheckpoint allocation_checkpoint) noexcept
    {
        for (std::uint32_t attempt = 1; attempt <= ENUMERATION_ATTEMPTS; ++attempt)
        {
            std::uint32_t  count        = 0;
            const VkResult count_result = dispatch.mGetSurfaceFormats(physical_device, surface, &count, nullptr);
            if (count_result == VK_INCOMPLETE)
            {
                continue;
            }
            if (count_result != VK_SUCCESS)
            {
                auto error                = failure(VulkanSwapchainConfigurationResolutionCode::SurfaceFormatEnumerationFailure,
                                                    VulkanSwapchainConfigurationCommand::GetPhysicalDeviceSurfaceFormats, count_result);
                error.mEnumerationAttempt = attempt;
                return error;
            }
            if (count == 0 || count > VULKAN_SWAPCHAIN_MAX_SURFACE_FORMATS)
            {
                auto error = failure(count == 0 ? VulkanSwapchainConfigurationResolutionCode::InvalidSurfaceFormatEnumerationOutput
                                                : VulkanSwapchainConfigurationResolutionCode::SurfaceFormatCountExceeded,
                                     VulkanSwapchainConfigurationCommand::GetPhysicalDeviceSurfaceFormats);
                error.mObservedCount      = count;
                error.mEnumerationAttempt = attempt;
                return error;
            }

            if (allocation_checkpoint)
            {
                try
                {
                    allocation_checkpoint();
                }
                catch (const std::bad_alloc&)
                {
                    auto error                = failure(VulkanSwapchainConfigurationResolutionCode::ScratchAllocationFailure,
                                                        VulkanSwapchainConfigurationCommand::GetPhysicalDeviceSurfaceFormats);
                    error.mObservedCount      = count;
                    error.mEnumerationAttempt = attempt;
                    return error;
                }
            }
            std::unique_ptr<VkSurfaceFormatKHR[]> formats(new (std::nothrow) VkSurfaceFormatKHR[count]{});
            if (!formats)
            {
                auto error                = failure(VulkanSwapchainConfigurationResolutionCode::ScratchAllocationFailure,
                                                    VulkanSwapchainConfigurationCommand::GetPhysicalDeviceSurfaceFormats);
                error.mObservedCount      = count;
                error.mEnumerationAttempt = attempt;
                return error;
            }

            const std::uint32_t capacity    = count;
            const VkResult      list_result = dispatch.mGetSurfaceFormats(physical_device, surface, &count, formats.get());
            if (count > capacity)
            {
                auto error                = failure(VulkanSwapchainConfigurationResolutionCode::InvalidSurfaceFormatEnumerationOutput,
                                                    VulkanSwapchainConfigurationCommand::GetPhysicalDeviceSurfaceFormats, list_result);
                error.mObservedCount      = count;
                error.mEnumerationAttempt = attempt;
                return error;
            }
            if (list_result == VK_INCOMPLETE)
            {
                continue;
            }
            if (list_result != VK_SUCCESS)
            {
                auto error                = failure(VulkanSwapchainConfigurationResolutionCode::SurfaceFormatEnumerationFailure,
                                                    VulkanSwapchainConfigurationCommand::GetPhysicalDeviceSurfaceFormats, list_result);
                error.mEnumerationAttempt = attempt;
                return error;
            }
            if (count == 0)
            {
                auto error                = failure(VulkanSwapchainConfigurationResolutionCode::InvalidSurfaceFormatEnumerationOutput,
                                                    VulkanSwapchainConfigurationCommand::GetPhysicalDeviceSurfaceFormats);
                error.mEnumerationAttempt = attempt;
                return error;
            }

            constexpr std::array<VkFormat, 2> priorities{ VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM };
            for (VkFormat priority : priorities)
            {
                for (std::uint32_t index = 0; index < count; ++index)
                {
                    if (formats[index].format == priority && formats[index].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
                    {
                        return formats[index];
                    }
                }
            }
            auto error                = failure(VulkanSwapchainConfigurationResolutionCode::NoCompatibleSurfaceFormat,
                                                VulkanSwapchainConfigurationCommand::GetPhysicalDeviceSurfaceFormats);
            error.mObservedCount      = count;
            error.mEnumerationAttempt = attempt;
            return error;
        }

        auto error                = failure(VulkanSwapchainConfigurationResolutionCode::SurfaceFormatEnumerationRetryLimitExceeded,
                                            VulkanSwapchainConfigurationCommand::GetPhysicalDeviceSurfaceFormats, VK_INCOMPLETE);
        error.mEnumerationAttempt = ENUMERATION_ATTEMPTS;
        return error;
    }

    std::variant<VulkanSwapchainConfigurationResolutionError, VkPresentModeKHR> selectPresentMode(
        const SwapchainConfigurationDispatch&                    dispatch,
        VkPhysicalDevice                                         physical_device,
        VkSurfaceKHR                                             surface,
        VulkanSwapchainConfigurationDetail::AllocationCheckpoint allocation_checkpoint) noexcept
    {
        for (std::uint32_t attempt = 1; attempt <= ENUMERATION_ATTEMPTS; ++attempt)
        {
            std::uint32_t  count        = 0;
            const VkResult count_result = dispatch.mGetSurfacePresentModes(physical_device, surface, &count, nullptr);
            if (count_result == VK_INCOMPLETE)
            {
                continue;
            }
            if (count_result != VK_SUCCESS)
            {
                auto error                = failure(VulkanSwapchainConfigurationResolutionCode::PresentModeEnumerationFailure,
                                                    VulkanSwapchainConfigurationCommand::GetPhysicalDeviceSurfacePresentModes, count_result);
                error.mEnumerationAttempt = attempt;
                return error;
            }
            if (count == 0 || count > VULKAN_SWAPCHAIN_MAX_PRESENT_MODES)
            {
                auto error = failure(count == 0 ? VulkanSwapchainConfigurationResolutionCode::InvalidPresentModeEnumerationOutput
                                                : VulkanSwapchainConfigurationResolutionCode::PresentModeCountExceeded,
                                     VulkanSwapchainConfigurationCommand::GetPhysicalDeviceSurfacePresentModes);
                error.mObservedCount      = count;
                error.mEnumerationAttempt = attempt;
                return error;
            }

            if (allocation_checkpoint)
            {
                try
                {
                    allocation_checkpoint();
                }
                catch (const std::bad_alloc&)
                {
                    auto error                = failure(VulkanSwapchainConfigurationResolutionCode::ScratchAllocationFailure,
                                                        VulkanSwapchainConfigurationCommand::GetPhysicalDeviceSurfacePresentModes);
                    error.mObservedCount      = count;
                    error.mEnumerationAttempt = attempt;
                    return error;
                }
            }
            std::unique_ptr<VkPresentModeKHR[]> modes(new (std::nothrow) VkPresentModeKHR[count]{});
            if (!modes)
            {
                auto error                = failure(VulkanSwapchainConfigurationResolutionCode::ScratchAllocationFailure,
                                                    VulkanSwapchainConfigurationCommand::GetPhysicalDeviceSurfacePresentModes);
                error.mObservedCount      = count;
                error.mEnumerationAttempt = attempt;
                return error;
            }

            const std::uint32_t capacity    = count;
            const VkResult      list_result = dispatch.mGetSurfacePresentModes(physical_device, surface, &count, modes.get());
            if (count > capacity)
            {
                auto error                = failure(VulkanSwapchainConfigurationResolutionCode::InvalidPresentModeEnumerationOutput,
                                                    VulkanSwapchainConfigurationCommand::GetPhysicalDeviceSurfacePresentModes, list_result);
                error.mObservedCount      = count;
                error.mEnumerationAttempt = attempt;
                return error;
            }
            if (list_result == VK_INCOMPLETE)
            {
                continue;
            }
            if (list_result != VK_SUCCESS)
            {
                auto error                = failure(VulkanSwapchainConfigurationResolutionCode::PresentModeEnumerationFailure,
                                                    VulkanSwapchainConfigurationCommand::GetPhysicalDeviceSurfacePresentModes, list_result);
                error.mEnumerationAttempt = attempt;
                return error;
            }
            if (count == 0)
            {
                auto error                = failure(VulkanSwapchainConfigurationResolutionCode::InvalidPresentModeEnumerationOutput,
                                                    VulkanSwapchainConfigurationCommand::GetPhysicalDeviceSurfacePresentModes);
                error.mEnumerationAttempt = attempt;
                return error;
            }
            if (std::find(modes.get(), modes.get() + count, VK_PRESENT_MODE_FIFO_KHR) != modes.get() + count)
            {
                return VK_PRESENT_MODE_FIFO_KHR;
            }

            auto error                = failure(VulkanSwapchainConfigurationResolutionCode::FifoPresentModeUnsupported,
                                                VulkanSwapchainConfigurationCommand::GetPhysicalDeviceSurfacePresentModes);
            error.mObservedCount      = count;
            error.mEnumerationAttempt = attempt;
            return error;
        }

        auto error                = failure(VulkanSwapchainConfigurationResolutionCode::PresentModeEnumerationRetryLimitExceeded,
                                            VulkanSwapchainConfigurationCommand::GetPhysicalDeviceSurfacePresentModes, VK_INCOMPLETE);
        error.mEnumerationAttempt = ENUMERATION_ATTEMPTS;
        return error;
    }

    VkExtent2D selectExtent(const VkSurfaceCapabilitiesKHR& capabilities, VkExtent2D drawable_extent) noexcept
    {
        if (capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max())
        {
            return capabilities.currentExtent;
        }
        return { std::clamp(drawable_extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
                 std::clamp(drawable_extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height) };
    }

    std::uint32_t selectImageCount(const VkSurfaceCapabilitiesKHR& capabilities) noexcept
    {
        std::uint32_t image_count = capabilities.minImageCount;
        if (image_count != std::numeric_limits<std::uint32_t>::max() &&
            (capabilities.maxImageCount == 0 || image_count < capabilities.maxImageCount))
        {
            ++image_count;
        }
        return image_count;
    }

    VkCompositeAlphaFlagBitsKHR selectCompositeAlpha(VkCompositeAlphaFlagsKHR supported) noexcept
    {
        constexpr std::array<VkCompositeAlphaFlagBitsKHR, 4> priorities{ VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
                                                                         VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                                                                         VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
                                                                         VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR };
        for (VkCompositeAlphaFlagBitsKHR priority : priorities)
        {
            if ((supported & priority) != 0)
            {
                return priority;
            }
        }
        return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    }

} // namespace

struct VulkanSwapchainConfigurationGenerationFactory
{
    static VulkanSwapchainConfigurationGeneration create(const VulkanPhysicalDeviceGeneration& physical_device_generation,
                                                         const VulkanLogicalDeviceGeneration&  logical_device_generation,
                                                         VkExtent2D                            drawable_extent,
                                                         const VkSurfaceCapabilitiesKHR&       surface_capabilities,
                                                         VkSurfaceFormatKHR                    surface_format,
                                                         VkPresentModeKHR                      present_mode,
                                                         std::uint32_t                         image_count,
                                                         VkExtent2D                            image_extent,
                                                         VkSurfaceTransformFlagBitsKHR         pre_transform,
                                                         VkCompositeAlphaFlagBitsKHR           composite_alpha) noexcept
    {
        return VulkanSwapchainConfigurationGeneration(physical_device_generation, logical_device_generation, drawable_extent,
                                                      surface_capabilities, surface_format, present_mode, image_count, image_extent,
                                                      pre_transform, composite_alpha);
    }
};

VulkanSwapchainConfigurationGeneration::VulkanSwapchainConfigurationGeneration(
    const VulkanPhysicalDeviceGeneration& physical_device_generation,
    const VulkanLogicalDeviceGeneration&  logical_device_generation,
    VkExtent2D                            drawable_extent,
    const VkSurfaceCapabilitiesKHR&       surface_capabilities,
    VkSurfaceFormatKHR                    surface_format,
    VkPresentModeKHR                      present_mode,
    std::uint32_t                         image_count,
    VkExtent2D                            image_extent,
    VkSurfaceTransformFlagBitsKHR         pre_transform,
    VkCompositeAlphaFlagBitsKHR           composite_alpha) noexcept :
    mGetInstanceProcAddr(physical_device_generation.getInstanceProcAddr()),
    mInstance(physical_device_generation.instance()),
    mSurface(physical_device_generation.surface()),
    mPhysicalDevice(physical_device_generation.physicalDevice()),
    mPhysicalDeviceIndex(physical_device_generation.physicalDeviceIndex()),
    mDevice(logical_device_generation.device()),
    mQueueFamilyIndex(physical_device_generation.queueFamilyIndex()),
    mDrawableExtent(drawable_extent),
    mSurfaceCapabilities(surface_capabilities),
    mSurfaceFormat(surface_format),
    mPresentMode(present_mode),
    mImageCount(image_count),
    mImageExtent(image_extent),
    mPreTransform(pre_transform),
    mCompositeAlpha(composite_alpha)
{
}

VulkanSwapchainConfigurationGeneration::VulkanSwapchainConfigurationGeneration(VulkanSwapchainConfigurationGeneration&& other) noexcept :
    mGetInstanceProcAddr(std::exchange(other.mGetInstanceProcAddr, nullptr)),
    mInstance(std::exchange(other.mInstance, VK_NULL_HANDLE)),
    mSurface(std::exchange(other.mSurface, VK_NULL_HANDLE)),
    mPhysicalDevice(std::exchange(other.mPhysicalDevice, VK_NULL_HANDLE)),
    mPhysicalDeviceIndex(std::exchange(other.mPhysicalDeviceIndex, 0)),
    mDevice(std::exchange(other.mDevice, VK_NULL_HANDLE)),
    mQueueFamilyIndex(std::exchange(other.mQueueFamilyIndex, VK_QUEUE_FAMILY_IGNORED)),
    mDrawableExtent(std::exchange(other.mDrawableExtent, {})),
    mSurfaceCapabilities(std::exchange(other.mSurfaceCapabilities, {})),
    mSurfaceFormat(std::exchange(other.mSurfaceFormat, {})),
    mPresentMode(std::exchange(other.mPresentMode, VK_PRESENT_MODE_FIFO_KHR)),
    mImageCount(std::exchange(other.mImageCount, 0)),
    mImageExtent(std::exchange(other.mImageExtent, {})),
    mPreTransform(std::exchange(other.mPreTransform, VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)),
    mCompositeAlpha(std::exchange(other.mCompositeAlpha, VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR))
{
}

bool VulkanSwapchainConfigurationGeneration::createdFor(const VulkanPhysicalDeviceGeneration& physical_device_generation,
                                                        const VulkanLogicalDeviceGeneration&  logical_device_generation,
                                                        VkExtent2D                            drawable_extent) const noexcept
{
    return mGetInstanceProcAddr == physical_device_generation.getInstanceProcAddr() && mInstance == physical_device_generation.instance() &&
           mSurface == physical_device_generation.surface() && mPhysicalDevice == physical_device_generation.physicalDevice() &&
           mPhysicalDeviceIndex == physical_device_generation.physicalDeviceIndex() && mDevice != VK_NULL_HANDLE &&
           mDevice == logical_device_generation.device() && mQueueFamilyIndex == physical_device_generation.queueFamilyIndex() &&
           logical_device_generation.createdFor(physical_device_generation) && mDrawableExtent.width == drawable_extent.width &&
           mDrawableExtent.height == drawable_extent.height;
}

VulkanSwapchainConfigurationResolutionResult resolveSwapchainConfiguration(
    const VulkanPhysicalDeviceGeneration&                    physical_device_generation,
    const VulkanLogicalDeviceGeneration&                     logical_device_generation,
    VkExtent2D                                               drawable_extent,
    VulkanSwapchainConfigurationDetail::AllocationCheckpoint allocation_checkpoint) noexcept
{
    if (!valid(physical_device_generation))
    {
        return failure(VulkanSwapchainConfigurationResolutionCode::InvalidPhysicalDeviceGeneration);
    }
    if (!logical_device_generation.createdFor(physical_device_generation) || logical_device_generation.device() == VK_NULL_HANDLE ||
        logical_device_generation.queue() == VK_NULL_HANDLE)
    {
        return failure(VulkanSwapchainConfigurationResolutionCode::InvalidLogicalDeviceGeneration);
    }
    if (drawable_extent.width == 0 || drawable_extent.height == 0)
    {
        return failure(VulkanSwapchainConfigurationResolutionCode::InvalidDrawableExtent);
    }

    SwapchainConfigurationDispatch dispatch;
    if (std::optional<VulkanSwapchainConfigurationResolutionError> error = resolveDispatch(physical_device_generation, dispatch))
    {
        return *error;
    }

    VkSurfaceCapabilitiesKHR capabilities{};
    const VkResult           capabilities_result =
        dispatch.mGetSurfaceCapabilities(physical_device_generation.physicalDevice(), physical_device_generation.surface(), &capabilities);
    if (capabilities_result != VK_SUCCESS)
    {
        return failure(VulkanSwapchainConfigurationResolutionCode::SurfaceCapabilitiesQueryFailure,
                       VulkanSwapchainConfigurationCommand::GetPhysicalDeviceSurfaceCapabilities, capabilities_result);
    }
    if (std::optional<VulkanSwapchainConfigurationResolutionError> error = validateCapabilities(capabilities))
    {
        return *error;
    }

    const VkExtent2D image_extent = selectExtent(capabilities, drawable_extent);
    const VkPhysicalDeviceLimits& limits = physical_device_generation.properties().limits;
    if (image_extent.width > limits.maxFramebufferWidth || image_extent.height > limits.maxFramebufferHeight)
    {
        return failure(VulkanSwapchainConfigurationResolutionCode::SelectedImageExtentExceedsFramebufferLimits);
    }

    auto format_result = selectSurfaceFormat(dispatch, physical_device_generation.physicalDevice(), physical_device_generation.surface(),
                                             allocation_checkpoint);
    if (const auto* error = std::get_if<VulkanSwapchainConfigurationResolutionError>(&format_result))
    {
        return *error;
    }

    const VkSurfaceFormatKHR selected_format = std::get<VkSurfaceFormatKHR>(format_result);
    VkFormatProperties       format_properties{};
    dispatch.mGetFormatProperties(physical_device_generation.physicalDevice(), selected_format.format, &format_properties);
    if ((format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_TRANSFER_DST_BIT) == 0)
    {
        return failure(VulkanSwapchainConfigurationResolutionCode::SelectedFormatTransferDestinationUnsupported,
                       VulkanSwapchainConfigurationCommand::GetPhysicalDeviceFormatProperties);
    }
    if ((format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) == 0)
    {
        return failure(VulkanSwapchainConfigurationResolutionCode::SelectedFormatColorAttachmentUnsupported,
                       VulkanSwapchainConfigurationCommand::GetPhysicalDeviceFormatProperties);
    }

    auto present_mode_result = selectPresentMode(dispatch, physical_device_generation.physicalDevice(),
                                                 physical_device_generation.surface(), allocation_checkpoint);
    if (const auto* error = std::get_if<VulkanSwapchainConfigurationResolutionError>(&present_mode_result))
    {
        return *error;
    }

    return VulkanSwapchainConfigurationGenerationFactory::create(
        physical_device_generation, logical_device_generation, drawable_extent, capabilities, selected_format,
        std::get<VkPresentModeKHR>(present_mode_result), selectImageCount(capabilities), image_extent,
        capabilities.currentTransform, selectCompositeAlpha(capabilities.supportedCompositeAlpha));
}

VulkanSwapchainConfigurationResolutionResult resolveVulkanSwapchainConfigurationGeneration(
    const VulkanPhysicalDeviceGeneration& physical_device_generation,
    const VulkanLogicalDeviceGeneration&  logical_device_generation,
    VkExtent2D                            drawable_extent) noexcept
{
    return resolveSwapchainConfiguration(physical_device_generation, logical_device_generation, drawable_extent, nullptr);
}

namespace VulkanSwapchainConfigurationDetail
{

    VulkanSwapchainConfigurationResolutionResult resolve(const VulkanPhysicalDeviceGeneration& physical_device_generation,
                                                         const VulkanLogicalDeviceGeneration&  logical_device_generation,
                                                         VkExtent2D                            drawable_extent,
                                                         AllocationCheckpoint                  allocation_checkpoint) noexcept
    {
        return resolveSwapchainConfiguration(physical_device_generation, logical_device_generation, drawable_extent, allocation_checkpoint);
    }

} // namespace VulkanSwapchainConfigurationDetail

} // namespace LLRenderVulkan
