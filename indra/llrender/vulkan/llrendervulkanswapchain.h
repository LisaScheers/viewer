/**
 * @file llrendervulkanswapchain.h
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

#ifndef LL_LLRENDERVULKANSWAPCHAIN_H
#define LL_LLRENDERVULKANSWAPCHAIN_H

#include "llrendervulkanswapchainconfiguration.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>
#include <variant>

namespace LLRenderVulkan
{

enum class VulkanSwapchainCommand : std::uint8_t
{
    GetDeviceProcAddr,
    CreateSwapchain,
    DestroySwapchain
};

enum class VulkanSwapchainResolutionCode : std::uint8_t
{
    InvalidLogicalDeviceGeneration,
    InvalidSwapchainConfigurationGeneration,
    MissingRequiredCommand,
    SwapchainCreationFailure,
    NullSwapchainOnSuccess
};

struct VulkanSwapchainResolutionError
{
    VulkanSwapchainResolutionCode         mCode = VulkanSwapchainResolutionCode::InvalidLogicalDeviceGeneration;
    std::optional<VulkanSwapchainCommand> mCommand;
    VkResult                              mResult = VK_SUCCESS;

    friend constexpr bool operator==(const VulkanSwapchainResolutionError&, const VulkanSwapchainResolutionError&) = default;
};

// This immutable generation owns one VkSwapchainKHR. Its exact logical device,
// configuration, surface, instance, resolver, and loader must outlive it.
// reset() is the sole explicit ownership transition.
class VulkanSwapchainGeneration
{
public:
    ~VulkanSwapchainGeneration() noexcept;

    VulkanSwapchainGeneration(const VulkanSwapchainGeneration&)            = delete;
    VulkanSwapchainGeneration& operator=(const VulkanSwapchainGeneration&) = delete;
    VulkanSwapchainGeneration(VulkanSwapchainGeneration&& other) noexcept;
    VulkanSwapchainGeneration& operator=(VulkanSwapchainGeneration&&) = delete;

    PFN_vkGetInstanceProcAddr       getInstanceProcAddr() const noexcept { return mGetInstanceProcAddr; }
    VkInstance                      instance() const noexcept { return mInstance; }
    VkSurfaceKHR                    surface() const noexcept { return mCreateInfo.surface; }
    VkPhysicalDevice                physicalDevice() const noexcept { return mPhysicalDevice; }
    std::uint32_t                   physicalDeviceIndex() const noexcept { return mPhysicalDeviceIndex; }
    VkDevice                        device() const noexcept { return mDevice; }
    std::uint32_t                   queueFamilyIndex() const noexcept { return mQueueFamilyIndex; }
    VkExtent2D                      drawableExtent() const noexcept { return mDrawableExtent; }
    VkSwapchainKHR                  swapchain() const noexcept { return mSwapchain; }
    const VkSwapchainCreateInfoKHR& createInfo() const noexcept { return mCreateInfo; }

    bool createdFor(const VulkanLogicalDeviceGeneration&          logical_device_generation,
                    const VulkanSwapchainConfigurationGeneration& configuration_generation) const noexcept;

    void reset() noexcept;

private:
    friend struct VulkanSwapchainGenerationFactory;

    VulkanSwapchainGeneration(const VulkanLogicalDeviceGeneration&          logical_device_generation,
                              const VulkanSwapchainConfigurationGeneration& configuration_generation,
                              const VkSwapchainCreateInfoKHR&               create_info,
                              VkSwapchainKHR                                swapchain,
                              PFN_vkDestroySwapchainKHR                     destroy_swapchain) noexcept;

    PFN_vkGetInstanceProcAddr mGetInstanceProcAddr = nullptr;
    VkInstance                mInstance            = VK_NULL_HANDLE;
    VkPhysicalDevice          mPhysicalDevice      = VK_NULL_HANDLE;
    std::uint32_t             mPhysicalDeviceIndex = 0;
    VkDevice                  mDevice              = VK_NULL_HANDLE;
    std::uint32_t             mQueueFamilyIndex    = VK_QUEUE_FAMILY_IGNORED;
    VkExtent2D                mDrawableExtent{};
    VkSwapchainCreateInfoKHR  mCreateInfo{};
    VkSwapchainKHR            mSwapchain        = VK_NULL_HANDLE;
    PFN_vkDestroySwapchainKHR mDestroySwapchain = nullptr;
};

using VulkanSwapchainResolutionResult = std::variant<VulkanSwapchainResolutionError, VulkanSwapchainGeneration>;

VulkanSwapchainResolutionResult resolveVulkanSwapchainGeneration(
    const VulkanLogicalDeviceGeneration&          logical_device_generation,
    const VulkanSwapchainConfigurationGeneration& configuration_generation) noexcept;

} // namespace LLRenderVulkan

#endif // LL_LLRENDERVULKANSWAPCHAIN_H
