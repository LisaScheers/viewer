/**
 * @file llrendervulkanswapchainimages.cpp
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

#include "llrendervulkanswapchainimages.h"

#include <algorithm>
#include <new>
#include <utility>

namespace LLRenderVulkan
{

namespace
{
    struct SwapchainImagesDispatch
    {
        PFN_vkGetDeviceProcAddr     mGetDeviceProcAddr  = nullptr;
        PFN_vkGetSwapchainImagesKHR mGetSwapchainImages = nullptr;
        PFN_vkCreateImageView       mCreateImageView    = nullptr;
        PFN_vkDestroyImageView      mDestroyImageView   = nullptr;
    };

    VulkanSwapchainImagesResolutionError failure(VulkanSwapchainImagesResolutionCode         code,
                                                 std::optional<VulkanSwapchainImagesCommand> command = std::nullopt,
                                                 VkResult                                    result  = VK_SUCCESS) noexcept
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
        return configuration.getInstanceProcAddr() == logical_device.getInstanceProcAddr() &&
               configuration.instance() == logical_device.instance() && configuration.surface() == logical_device.surface() &&
               configuration.physicalDevice() == logical_device.physicalDevice() &&
               configuration.physicalDeviceIndex() == logical_device.physicalDeviceIndex() &&
               configuration.device() == logical_device.device() && configuration.queueFamilyIndex() == logical_device.queueFamilyIndex() &&
               drawable_extent.width != 0 && drawable_extent.height != 0 && configuration.imageCount() != 0 &&
               configuration.surfaceFormat().format != VK_FORMAT_UNDEFINED;
    }

    std::optional<VulkanSwapchainImagesResolutionError> resolveDispatch(const VulkanLogicalDeviceGeneration& logical_device,
                                                                        SwapchainImagesDispatch&             dispatch) noexcept
    {
        dispatch.mGetDeviceProcAddr = resolveInstance<PFN_vkGetDeviceProcAddr>(logical_device.getInstanceProcAddr(),
                                                                               logical_device.instance(), "vkGetDeviceProcAddr");
        if (!dispatch.mGetDeviceProcAddr)
        {
            return failure(VulkanSwapchainImagesResolutionCode::MissingRequiredCommand, VulkanSwapchainImagesCommand::GetDeviceProcAddr);
        }

        dispatch.mGetSwapchainImages =
            resolveDevice<PFN_vkGetSwapchainImagesKHR>(dispatch.mGetDeviceProcAddr, logical_device.device(), "vkGetSwapchainImagesKHR");
        if (!dispatch.mGetSwapchainImages)
        {
            return failure(VulkanSwapchainImagesResolutionCode::MissingRequiredCommand, VulkanSwapchainImagesCommand::GetSwapchainImages);
        }
        dispatch.mCreateImageView =
            resolveDevice<PFN_vkCreateImageView>(dispatch.mGetDeviceProcAddr, logical_device.device(), "vkCreateImageView");
        if (!dispatch.mCreateImageView)
        {
            return failure(VulkanSwapchainImagesResolutionCode::MissingRequiredCommand, VulkanSwapchainImagesCommand::CreateImageView);
        }
        dispatch.mDestroyImageView =
            resolveDevice<PFN_vkDestroyImageView>(dispatch.mGetDeviceProcAddr, logical_device.device(), "vkDestroyImageView");
        if (!dispatch.mDestroyImageView)
        {
            return failure(VulkanSwapchainImagesResolutionCode::MissingRequiredCommand, VulkanSwapchainImagesCommand::DestroyImageView);
        }
        return std::nullopt;
    }

    std::optional<VulkanSwapchainImagesResolutionError> allocationFailure(
        VulkanSwapchainImagesDetail::AllocationCheckpoint allocation_checkpoint,
        std::uint32_t                                     observed_count,
        std::uint32_t                                     attempt) noexcept
    {
        if (!allocation_checkpoint)
        {
            return std::nullopt;
        }
        try
        {
            allocation_checkpoint();
        }
        catch (const std::bad_alloc&)
        {
            auto error =
                failure(VulkanSwapchainImagesResolutionCode::ScratchAllocationFailure, VulkanSwapchainImagesCommand::GetSwapchainImages);
            error.mObservedCount      = observed_count;
            error.mEnumerationAttempt = attempt;
            return error;
        }
        return std::nullopt;
    }

    std::variant<VulkanSwapchainImagesResolutionError, std::unique_ptr<VkImage[]>> enumerateImages(
        const SwapchainImagesDispatch&                    dispatch,
        const VulkanLogicalDeviceGeneration&              logical_device,
        const VulkanSwapchainConfigurationGeneration&     configuration,
        const VulkanSwapchainGeneration&                  swapchain,
        VulkanSwapchainImagesDetail::AllocationCheckpoint allocation_checkpoint,
        std::uint32_t&                                    image_count) noexcept
    {
        std::uint32_t last_observed_count = 0;
        for (std::uint32_t attempt = 1; attempt <= VULKAN_SWAPCHAIN_IMAGE_ENUMERATION_ATTEMPTS; ++attempt)
        {
            std::uint32_t  count        = 0;
            const VkResult count_result = dispatch.mGetSwapchainImages(logical_device.device(), swapchain.swapchain(), &count, nullptr);
            if (count_result != VK_SUCCESS && count_result != VK_INCOMPLETE)
            {
                auto error                = failure(VulkanSwapchainImagesResolutionCode::SwapchainImageEnumerationFailure,
                                                    VulkanSwapchainImagesCommand::GetSwapchainImages, count_result);
                error.mEnumerationAttempt = attempt;
                return error;
            }
            last_observed_count = count;
            if (count_result == VK_INCOMPLETE)
            {
                continue;
            }
            if (count == 0 || count < configuration.imageCount())
            {
                auto error                = failure(VulkanSwapchainImagesResolutionCode::InvalidSwapchainImageEnumerationOutput,
                                                    VulkanSwapchainImagesCommand::GetSwapchainImages);
                error.mObservedCount      = count;
                error.mEnumerationAttempt = attempt;
                return error;
            }
            if (count > VULKAN_SWAPCHAIN_MAX_IMAGES)
            {
                auto error                = failure(VulkanSwapchainImagesResolutionCode::SwapchainImageCountExceeded,
                                                    VulkanSwapchainImagesCommand::GetSwapchainImages);
                error.mObservedCount      = count;
                error.mEnumerationAttempt = attempt;
                return error;
            }

            if (auto error = allocationFailure(allocation_checkpoint, count, attempt))
            {
                return *error;
            }
            std::unique_ptr<VkImage[]> images(new (std::nothrow) VkImage[count]{});
            if (!images)
            {
                auto error                = failure(VulkanSwapchainImagesResolutionCode::ScratchAllocationFailure,
                                                    VulkanSwapchainImagesCommand::GetSwapchainImages);
                error.mObservedCount      = count;
                error.mEnumerationAttempt = attempt;
                return error;
            }

            const std::uint32_t capacity = count;
            const VkResult list_result = dispatch.mGetSwapchainImages(logical_device.device(), swapchain.swapchain(), &count, images.get());
            if (list_result != VK_SUCCESS && list_result != VK_INCOMPLETE)
            {
                auto error                = failure(VulkanSwapchainImagesResolutionCode::SwapchainImageEnumerationFailure,
                                                    VulkanSwapchainImagesCommand::GetSwapchainImages, list_result);
                error.mEnumerationAttempt = attempt;
                return error;
            }
            last_observed_count = count;
            if (count > VULKAN_SWAPCHAIN_MAX_IMAGES)
            {
                auto error                = failure(VulkanSwapchainImagesResolutionCode::SwapchainImageCountExceeded,
                                                    VulkanSwapchainImagesCommand::GetSwapchainImages, list_result);
                error.mObservedCount      = count;
                error.mEnumerationAttempt = attempt;
                return error;
            }
            if (count > capacity)
            {
                auto error                = failure(VulkanSwapchainImagesResolutionCode::InvalidSwapchainImageEnumerationOutput,
                                                    VulkanSwapchainImagesCommand::GetSwapchainImages, list_result);
                error.mObservedCount      = count;
                error.mEnumerationAttempt = attempt;
                return error;
            }
            if (list_result == VK_INCOMPLETE)
            {
                continue;
            }
            if (count == 0 || count < configuration.imageCount())
            {
                auto error                = failure(VulkanSwapchainImagesResolutionCode::InvalidSwapchainImageEnumerationOutput,
                                                    VulkanSwapchainImagesCommand::GetSwapchainImages);
                error.mObservedCount      = count;
                error.mEnumerationAttempt = attempt;
                return error;
            }

            for (std::uint32_t index = 0; index < count; ++index)
            {
                if (images[index] == VK_NULL_HANDLE || std::find(images.get(), images.get() + index, images[index]) != images.get() + index)
                {
                    auto error                = failure(VulkanSwapchainImagesResolutionCode::InvalidSwapchainImageEnumerationOutput,
                                                        VulkanSwapchainImagesCommand::GetSwapchainImages);
                    error.mObservedCount      = count;
                    error.mEnumerationAttempt = attempt;
                    error.mImageIndex         = index;
                    return error;
                }
            }

            image_count = count;
            return images;
        }

        auto error                = failure(VulkanSwapchainImagesResolutionCode::SwapchainImageEnumerationRetryLimitExceeded,
                                            VulkanSwapchainImagesCommand::GetSwapchainImages, VK_INCOMPLETE);
        error.mObservedCount      = last_observed_count;
        error.mEnumerationAttempt = VULKAN_SWAPCHAIN_IMAGE_ENUMERATION_ATTEMPTS;
        return error;
    }

    VkImageViewCreateInfo imageViewCreateInfo(VkImage image, VkFormat format) noexcept
    {
        VkImageViewCreateInfo info{};
        info.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        info.image                           = image;
        info.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        info.format                          = format;
        info.components.r                    = VK_COMPONENT_SWIZZLE_IDENTITY;
        info.components.g                    = VK_COMPONENT_SWIZZLE_IDENTITY;
        info.components.b                    = VK_COMPONENT_SWIZZLE_IDENTITY;
        info.components.a                    = VK_COMPONENT_SWIZZLE_IDENTITY;
        info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        info.subresourceRange.baseMipLevel   = 0;
        info.subresourceRange.levelCount     = 1;
        info.subresourceRange.baseArrayLayer = 0;
        info.subresourceRange.layerCount     = 1;
        return info;
    }

    void destroyViewsReverse(VkDevice               device,
                             PFN_vkDestroyImageView destroy_image_view,
                             VkImageView*           image_views,
                             std::uint32_t          count) noexcept
    {
        while (count != 0)
        {
            --count;
            if (image_views[count] != VK_NULL_HANDLE)
            {
                destroy_image_view(device, image_views[count], nullptr);
                image_views[count] = VK_NULL_HANDLE;
            }
        }
    }

} // namespace

struct VulkanSwapchainImagesGenerationFactory
{
    static VulkanSwapchainImagesGeneration create(const VulkanLogicalDeviceGeneration&          logical_device_generation,
                                                  const VulkanSwapchainConfigurationGeneration& configuration_generation,
                                                  const VulkanSwapchainGeneration&              swapchain_generation,
                                                  std::uint32_t                                 image_count,
                                                  std::unique_ptr<VkImage[]>
                                                      images,
                                                  std::unique_ptr<VkImageView[]>
                                                                         image_views,
                                                  PFN_vkDestroyImageView destroy_image_view) noexcept
    {
        return VulkanSwapchainImagesGeneration(logical_device_generation, configuration_generation, swapchain_generation, image_count,
                                               std::move(images), std::move(image_views), destroy_image_view);
    }
};

VulkanSwapchainImagesGeneration::VulkanSwapchainImagesGeneration(const VulkanLogicalDeviceGeneration&          logical_device_generation,
                                                                 const VulkanSwapchainConfigurationGeneration& configuration_generation,
                                                                 const VulkanSwapchainGeneration&              swapchain_generation,
                                                                 std::uint32_t                                 image_count,
                                                                 std::unique_ptr<VkImage[]>
                                                                     images,
                                                                 std::unique_ptr<VkImageView[]>
                                                                                        image_views,
                                                                 PFN_vkDestroyImageView destroy_image_view) noexcept :
    mGetInstanceProcAddr(logical_device_generation.getInstanceProcAddr()),
    mInstance(logical_device_generation.instance()),
    mPhysicalDevice(logical_device_generation.physicalDevice()),
    mPhysicalDeviceIndex(logical_device_generation.physicalDeviceIndex()),
    mDevice(logical_device_generation.device()),
    mQueueFamilyIndex(logical_device_generation.queueFamilyIndex()),
    mDrawableExtent(configuration_generation.drawableExtent()),
    mSwapchain(swapchain_generation.swapchain()),
    mImageFormat(configuration_generation.surfaceFormat().format),
    mImageCount(image_count),
    mImages(std::move(images)),
    mImageViews(std::move(image_views)),
    mDestroyImageView(destroy_image_view)
{
}

VulkanSwapchainImagesGeneration::~VulkanSwapchainImagesGeneration() noexcept
{
    reset();
}

VulkanSwapchainImagesGeneration::VulkanSwapchainImagesGeneration(VulkanSwapchainImagesGeneration&& other) noexcept :
    mGetInstanceProcAddr(std::exchange(other.mGetInstanceProcAddr, nullptr)),
    mInstance(std::exchange(other.mInstance, VK_NULL_HANDLE)),
    mPhysicalDevice(std::exchange(other.mPhysicalDevice, VK_NULL_HANDLE)),
    mPhysicalDeviceIndex(std::exchange(other.mPhysicalDeviceIndex, 0)),
    mDevice(std::exchange(other.mDevice, VK_NULL_HANDLE)),
    mQueueFamilyIndex(std::exchange(other.mQueueFamilyIndex, VK_QUEUE_FAMILY_IGNORED)),
    mDrawableExtent(std::exchange(other.mDrawableExtent, {})),
    mSwapchain(std::exchange(other.mSwapchain, VK_NULL_HANDLE)),
    mImageFormat(std::exchange(other.mImageFormat, VK_FORMAT_UNDEFINED)),
    mImageCount(std::exchange(other.mImageCount, 0)),
    mImages(std::move(other.mImages)),
    mImageViews(std::move(other.mImageViews)),
    mDestroyImageView(std::exchange(other.mDestroyImageView, nullptr))
{
}

VkImage VulkanSwapchainImagesGeneration::image(std::uint32_t index) const noexcept
{
    return index < mImageCount && mImages ? mImages[index] : VK_NULL_HANDLE;
}

VkImageView VulkanSwapchainImagesGeneration::imageView(std::uint32_t index) const noexcept
{
    return index < mImageCount && mImageViews ? mImageViews[index] : VK_NULL_HANDLE;
}

bool VulkanSwapchainImagesGeneration::createdFor(const VulkanLogicalDeviceGeneration&          logical_device_generation,
                                                 const VulkanSwapchainConfigurationGeneration& configuration_generation,
                                                 const VulkanSwapchainGeneration&              swapchain_generation) const noexcept
{
    const VkExtent2D drawable_extent = configuration_generation.drawableExtent();
    return mImageCount != 0 && mImages && mImageViews &&
           swapchain_generation.createdFor(logical_device_generation, configuration_generation) &&
           mGetInstanceProcAddr == logical_device_generation.getInstanceProcAddr() && mInstance == logical_device_generation.instance() &&
           mPhysicalDevice == logical_device_generation.physicalDevice() &&
           mPhysicalDeviceIndex == logical_device_generation.physicalDeviceIndex() && mDevice == logical_device_generation.device() &&
           mQueueFamilyIndex == logical_device_generation.queueFamilyIndex() && mDrawableExtent.width == drawable_extent.width &&
           mDrawableExtent.height == drawable_extent.height && mSwapchain == swapchain_generation.swapchain() &&
           mImageFormat == configuration_generation.surfaceFormat().format;
}

void VulkanSwapchainImagesGeneration::reset() noexcept
{
    if (mImageViews && mDestroyImageView)
    {
        destroyViewsReverse(mDevice, mDestroyImageView, mImageViews.get(), mImageCount);
    }
    mImageViews.reset();
    mImages.reset();
    mImageCount = 0;
}

namespace
{
    VulkanSwapchainImagesResolutionResult resolveSwapchainImages(
        const VulkanLogicalDeviceGeneration&              logical_device_generation,
        const VulkanSwapchainConfigurationGeneration&     configuration_generation,
        const VulkanSwapchainGeneration&                  swapchain_generation,
        VulkanSwapchainImagesDetail::AllocationCheckpoint allocation_checkpoint) noexcept
    {
        if (!valid(logical_device_generation))
        {
            return failure(VulkanSwapchainImagesResolutionCode::InvalidLogicalDeviceGeneration);
        }
        if (!belongsTo(configuration_generation, logical_device_generation))
        {
            return failure(VulkanSwapchainImagesResolutionCode::InvalidSwapchainConfigurationGeneration);
        }
        if (!swapchain_generation.createdFor(logical_device_generation, configuration_generation))
        {
            return failure(VulkanSwapchainImagesResolutionCode::InvalidSwapchainGeneration);
        }

        SwapchainImagesDispatch dispatch;
        if (auto error = resolveDispatch(logical_device_generation, dispatch))
        {
            return *error;
        }

        std::uint32_t image_count   = 0;
        auto          images_result = enumerateImages(dispatch, logical_device_generation, configuration_generation, swapchain_generation,
                                                      allocation_checkpoint, image_count);
        if (const auto* error = std::get_if<VulkanSwapchainImagesResolutionError>(&images_result))
        {
            return *error;
        }
        auto images = std::get<std::unique_ptr<VkImage[]>>(std::move(images_result));

        if (auto error = allocationFailure(allocation_checkpoint, image_count, 0))
        {
            error->mCommand = VulkanSwapchainImagesCommand::CreateImageView;
            return *error;
        }
        std::unique_ptr<VkImageView[]> image_views(new (std::nothrow) VkImageView[image_count]{});
        if (!image_views)
        {
            auto error =
                failure(VulkanSwapchainImagesResolutionCode::ScratchAllocationFailure, VulkanSwapchainImagesCommand::CreateImageView);
            error.mObservedCount = image_count;
            return error;
        }

        const VkFormat format = configuration_generation.surfaceFormat().format;
        for (std::uint32_t index = 0; index < image_count; ++index)
        {
            const VkImageViewCreateInfo create_info = imageViewCreateInfo(images[index], format);
            VkImageView                 image_view;
            const VkResult result = dispatch.mCreateImageView(logical_device_generation.device(), &create_info, nullptr, &image_view);
            if (result != VK_SUCCESS)
            {
                // Vulkan leaves the output undefined on failure. Roll back only
                // the views from earlier successful calls.
                destroyViewsReverse(logical_device_generation.device(), dispatch.mDestroyImageView, image_views.get(), index);
                auto error           = failure(VulkanSwapchainImagesResolutionCode::ImageViewCreationFailure,
                                               VulkanSwapchainImagesCommand::CreateImageView, result);
                error.mObservedCount = image_count;
                error.mImageIndex    = index;
                return error;
            }
            if (image_view == VK_NULL_HANDLE)
            {
                destroyViewsReverse(logical_device_generation.device(), dispatch.mDestroyImageView, image_views.get(), index);
                auto error =
                    failure(VulkanSwapchainImagesResolutionCode::NullImageViewOnSuccess, VulkanSwapchainImagesCommand::CreateImageView);
                error.mObservedCount = image_count;
                error.mImageIndex    = index;
                return error;
            }
            image_views[index] = image_view;
        }

        return VulkanSwapchainImagesGenerationFactory::create(logical_device_generation, configuration_generation, swapchain_generation,
                                                              image_count, std::move(images), std::move(image_views),
                                                              dispatch.mDestroyImageView);
    }

} // namespace

VulkanSwapchainImagesResolutionResult resolveVulkanSwapchainImagesGeneration(
    const VulkanLogicalDeviceGeneration&          logical_device_generation,
    const VulkanSwapchainConfigurationGeneration& configuration_generation,
    const VulkanSwapchainGeneration&              swapchain_generation) noexcept
{
    return resolveSwapchainImages(logical_device_generation, configuration_generation, swapchain_generation, nullptr);
}

namespace VulkanSwapchainImagesDetail
{

    VulkanSwapchainImagesResolutionResult resolve(const VulkanLogicalDeviceGeneration&          logical_device_generation,
                                                  const VulkanSwapchainConfigurationGeneration& configuration_generation,
                                                  const VulkanSwapchainGeneration&              swapchain_generation,
                                                  AllocationCheckpoint                          allocation_checkpoint) noexcept
    {
        return resolveSwapchainImages(logical_device_generation, configuration_generation, swapchain_generation, allocation_checkpoint);
    }

} // namespace VulkanSwapchainImagesDetail

} // namespace LLRenderVulkan
