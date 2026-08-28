/**
 * @file llrendervulkanswapchain.cpp
 * @brief Loader-neutral Vulkan swapchain ownership.
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

#include "llrendervulkanswapchain.h"

#include <utility>

namespace LLRenderVulkan
{

namespace
{
    struct SwapchainDispatch
    {
        PFN_vkGetDeviceProcAddr   mGetDeviceProcAddr = nullptr;
        PFN_vkCreateSwapchainKHR  mCreateSwapchain   = nullptr;
        PFN_vkDestroySwapchainKHR mDestroySwapchain  = nullptr;
    };

    VulkanSwapchainResolutionError failure(VulkanSwapchainResolutionCode         code,
                                           std::optional<VulkanSwapchainCommand> command = std::nullopt,
                                           VkResult                              result  = VK_SUCCESS) noexcept
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
               drawable_extent.width != 0 && drawable_extent.height != 0;
    }

    std::optional<VulkanSwapchainResolutionError> resolveDispatch(const VulkanLogicalDeviceGeneration& logical_device,
                                                                  SwapchainDispatch&                   dispatch) noexcept
    {
        dispatch.mGetDeviceProcAddr = resolveInstance<PFN_vkGetDeviceProcAddr>(logical_device.getInstanceProcAddr(),
                                                                               logical_device.instance(), "vkGetDeviceProcAddr");
        if (!dispatch.mGetDeviceProcAddr)
        {
            return failure(VulkanSwapchainResolutionCode::MissingRequiredCommand, VulkanSwapchainCommand::GetDeviceProcAddr);
        }

        dispatch.mCreateSwapchain =
            resolveDevice<PFN_vkCreateSwapchainKHR>(dispatch.mGetDeviceProcAddr, logical_device.device(), "vkCreateSwapchainKHR");
        if (!dispatch.mCreateSwapchain)
        {
            return failure(VulkanSwapchainResolutionCode::MissingRequiredCommand, VulkanSwapchainCommand::CreateSwapchain);
        }

        dispatch.mDestroySwapchain =
            resolveDevice<PFN_vkDestroySwapchainKHR>(dispatch.mGetDeviceProcAddr, logical_device.device(), "vkDestroySwapchainKHR");
        if (!dispatch.mDestroySwapchain)
        {
            return failure(VulkanSwapchainResolutionCode::MissingRequiredCommand, VulkanSwapchainCommand::DestroySwapchain);
        }
        return std::nullopt;
    }

    VkSwapchainCreateInfoKHR createInfo(const VulkanSwapchainConfigurationGeneration& configuration) noexcept
    {
        const VkSurfaceFormatKHR format = configuration.surfaceFormat();

        VkSwapchainCreateInfoKHR info{};
        info.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        info.surface          = configuration.surface();
        info.minImageCount    = configuration.imageCount();
        info.imageFormat      = format.format;
        info.imageColorSpace  = format.colorSpace;
        info.imageExtent      = configuration.imageExtent();
        info.imageArrayLayers = configuration.imageArrayLayers();
        info.imageUsage       = configuration.imageUsage();
        info.imageSharingMode = configuration.imageSharingMode();
        info.preTransform     = configuration.preTransform();
        info.compositeAlpha   = configuration.compositeAlpha();
        info.presentMode      = configuration.presentMode();
        info.clipped          = configuration.clipped();
        return info;
    }

    bool sameCreateContract(const VkSwapchainCreateInfoKHR& info, const VulkanSwapchainConfigurationGeneration& configuration) noexcept
    {
        const VkSurfaceFormatKHR format = configuration.surfaceFormat();
        const VkExtent2D         extent = configuration.imageExtent();
        return info.sType == VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR && info.pNext == nullptr && info.flags == 0 &&
               info.surface == configuration.surface() && info.minImageCount == configuration.imageCount() &&
               info.imageFormat == format.format && info.imageColorSpace == format.colorSpace && info.imageExtent.width == extent.width &&
               info.imageExtent.height == extent.height && info.imageArrayLayers == configuration.imageArrayLayers() &&
               info.imageUsage == configuration.imageUsage() && info.imageSharingMode == configuration.imageSharingMode() &&
               info.queueFamilyIndexCount == 0 && info.pQueueFamilyIndices == nullptr &&
               info.preTransform == configuration.preTransform() && info.compositeAlpha == configuration.compositeAlpha() &&
               info.presentMode == configuration.presentMode() && info.clipped == configuration.clipped() &&
               info.oldSwapchain == VK_NULL_HANDLE;
    }

} // namespace

struct VulkanSwapchainGenerationFactory
{
    static VulkanSwapchainGeneration create(const VulkanLogicalDeviceGeneration&          logical_device_generation,
                                            const VulkanSwapchainConfigurationGeneration& configuration_generation,
                                            const VkSwapchainCreateInfoKHR&               create_info,
                                            VkSwapchainKHR                                swapchain,
                                            PFN_vkDestroySwapchainKHR                     destroy_swapchain) noexcept
    {
        return VulkanSwapchainGeneration(logical_device_generation, configuration_generation, create_info, swapchain, destroy_swapchain);
    }
};

VulkanSwapchainGeneration::VulkanSwapchainGeneration(const VulkanLogicalDeviceGeneration&          logical_device_generation,
                                                     const VulkanSwapchainConfigurationGeneration& configuration_generation,
                                                     const VkSwapchainCreateInfoKHR&               create_info,
                                                     VkSwapchainKHR                                swapchain,
                                                     PFN_vkDestroySwapchainKHR                     destroy_swapchain) noexcept :
    mGetInstanceProcAddr(logical_device_generation.getInstanceProcAddr()),
    mInstance(logical_device_generation.instance()),
    mPhysicalDevice(logical_device_generation.physicalDevice()),
    mPhysicalDeviceIndex(logical_device_generation.physicalDeviceIndex()),
    mDevice(logical_device_generation.device()),
    mQueueFamilyIndex(logical_device_generation.queueFamilyIndex()),
    mDrawableExtent(configuration_generation.drawableExtent()),
    mCreateInfo(create_info),
    mSwapchain(swapchain),
    mDestroySwapchain(destroy_swapchain)
{
}

VulkanSwapchainGeneration::~VulkanSwapchainGeneration() noexcept
{
    reset();
}

VulkanSwapchainGeneration::VulkanSwapchainGeneration(VulkanSwapchainGeneration&& other) noexcept :
    mGetInstanceProcAddr(std::exchange(other.mGetInstanceProcAddr, nullptr)),
    mInstance(std::exchange(other.mInstance, VK_NULL_HANDLE)),
    mPhysicalDevice(std::exchange(other.mPhysicalDevice, VK_NULL_HANDLE)),
    mPhysicalDeviceIndex(std::exchange(other.mPhysicalDeviceIndex, 0)),
    mDevice(std::exchange(other.mDevice, VK_NULL_HANDLE)),
    mQueueFamilyIndex(std::exchange(other.mQueueFamilyIndex, VK_QUEUE_FAMILY_IGNORED)),
    mDrawableExtent(std::exchange(other.mDrawableExtent, {})),
    mCreateInfo(std::exchange(other.mCreateInfo, {})),
    mSwapchain(std::exchange(other.mSwapchain, VK_NULL_HANDLE)),
    mDestroySwapchain(std::exchange(other.mDestroySwapchain, nullptr))
{
}

bool VulkanSwapchainGeneration::createdFor(const VulkanLogicalDeviceGeneration&          logical_device_generation,
                                           const VulkanSwapchainConfigurationGeneration& configuration_generation) const noexcept
{
    const VkExtent2D drawable_extent = configuration_generation.drawableExtent();
    return mSwapchain != VK_NULL_HANDLE && valid(logical_device_generation) &&
           belongsTo(configuration_generation, logical_device_generation) &&
           mGetInstanceProcAddr == logical_device_generation.getInstanceProcAddr() && mInstance == logical_device_generation.instance() &&
           mPhysicalDevice == logical_device_generation.physicalDevice() &&
           mPhysicalDeviceIndex == logical_device_generation.physicalDeviceIndex() && mDevice == logical_device_generation.device() &&
           mQueueFamilyIndex == logical_device_generation.queueFamilyIndex() && mDrawableExtent.width == drawable_extent.width &&
           mDrawableExtent.height == drawable_extent.height && sameCreateContract(mCreateInfo, configuration_generation);
}

void VulkanSwapchainGeneration::reset() noexcept
{
    if (mSwapchain != VK_NULL_HANDLE && mDestroySwapchain)
    {
        mDestroySwapchain(mDevice, mSwapchain, nullptr);
    }
    mSwapchain = VK_NULL_HANDLE;
}

VulkanSwapchainResolutionResult resolveVulkanSwapchainGeneration(
    const VulkanLogicalDeviceGeneration&          logical_device_generation,
    const VulkanSwapchainConfigurationGeneration& configuration_generation) noexcept
{
    if (!valid(logical_device_generation))
    {
        return failure(VulkanSwapchainResolutionCode::InvalidLogicalDeviceGeneration);
    }
    if (!belongsTo(configuration_generation, logical_device_generation))
    {
        return failure(VulkanSwapchainResolutionCode::InvalidSwapchainConfigurationGeneration);
    }

    SwapchainDispatch dispatch;
    if (std::optional<VulkanSwapchainResolutionError> error = resolveDispatch(logical_device_generation, dispatch))
    {
        return *error;
    }

    const VkSwapchainCreateInfoKHR create_info = createInfo(configuration_generation);
    VkSwapchainKHR                 swapchain   = VK_NULL_HANDLE;
    const VkResult result = dispatch.mCreateSwapchain(logical_device_generation.device(), &create_info, nullptr, &swapchain);
    if (result != VK_SUCCESS)
    {
        // Vulkan leaves the output undefined on failure. Do not inspect it or
        // pass it to vkDestroySwapchainKHR.
        return failure(VulkanSwapchainResolutionCode::SwapchainCreationFailure, VulkanSwapchainCommand::CreateSwapchain, result);
    }
    if (swapchain == VK_NULL_HANDLE)
    {
        return failure(VulkanSwapchainResolutionCode::NullSwapchainOnSuccess, VulkanSwapchainCommand::CreateSwapchain);
    }

    return VulkanSwapchainGenerationFactory::create(logical_device_generation, configuration_generation, create_info, swapchain,
                                                    dispatch.mDestroySwapchain);
}

} // namespace LLRenderVulkan
