/**
 * @file llrendervulkanlogicaldevice.h
 * @brief Loader-neutral Vulkan logical-device ownership.
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

#ifndef LL_LLRENDERVULKANLOGICALDEVICE_H
#define LL_LLRENDERVULKANLOGICALDEVICE_H

#include "llrendervulkanphysicaldevice.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <variant>

namespace LLRenderVulkan
{

enum class VulkanLogicalDeviceCommand : std::uint8_t
{
    GetPhysicalDeviceFeatures,
    CreateDevice,
    DestroyDevice,
    GetDeviceQueue
};

enum class VulkanLogicalDeviceResolutionCode : std::uint8_t
{
    InvalidPhysicalDeviceGeneration,
    MissingRequiredCommand,
    IndependentBlendUnsupported,
    DeviceCreationFailure,
    NullDeviceOnSuccess,
    NullQueueOnSuccess
};

struct VulkanLogicalDeviceResolutionError
{
    VulkanLogicalDeviceResolutionCode         mCode = VulkanLogicalDeviceResolutionCode::InvalidPhysicalDeviceGeneration;
    std::optional<VulkanLogicalDeviceCommand> mCommand;
    VkResult                                  mResult = VK_SUCCESS;

    friend constexpr bool operator==(const VulkanLogicalDeviceResolutionError&, const VulkanLogicalDeviceResolutionError&) = default;
};

// This immutable generation owns one VkDevice and borrows its queue. The
// originating physical-device generation, surface, instance, resolver, and
// loader must outlive it. reset() is the sole explicit ownership transition.
class VulkanLogicalDeviceGeneration
{
public:
    ~VulkanLogicalDeviceGeneration() noexcept;

    VulkanLogicalDeviceGeneration(const VulkanLogicalDeviceGeneration&)            = delete;
    VulkanLogicalDeviceGeneration& operator=(const VulkanLogicalDeviceGeneration&) = delete;
    VulkanLogicalDeviceGeneration(VulkanLogicalDeviceGeneration&& other) noexcept;
    VulkanLogicalDeviceGeneration& operator=(VulkanLogicalDeviceGeneration&&) = delete;

    PFN_vkGetInstanceProcAddr       getInstanceProcAddr() const noexcept { return mGetInstanceProcAddr; }
    VkInstance                      instance() const noexcept { return mInstance; }
    VkSurfaceKHR                    surface() const noexcept { return mSurface; }
    VkPhysicalDevice                physicalDevice() const noexcept { return mPhysicalDevice; }
    std::uint32_t                   physicalDeviceIndex() const noexcept { return mPhysicalDeviceIndex; }
    VkDevice                        device() const noexcept { return mDevice; }
    VkQueue                         queue() const noexcept { return mQueue; }
    std::uint32_t                   queueFamilyIndex() const noexcept { return mQueueFamilyIndex; }
    std::uint32_t                   queueIndex() const noexcept { return mQueueIndex; }
    const VkPhysicalDeviceFeatures& enabledFeatures() const noexcept { return mEnabledFeatures; }
    bool                            independentBlendEnabled() const noexcept { return mEnabledFeatures.independentBlend == VK_TRUE; }
    bool swapchainMaintenance1Enabled() const noexcept { return mEnabledSwapchainMaintenance1Features.swapchainMaintenance1 == VK_TRUE; }
    std::span<const std::string_view> enabledDeviceExtensions() const noexcept
    {
        return std::span<const std::string_view>(mEnabledDeviceExtensions.data(), mEnabledDeviceExtensionCount);
    }
    bool portabilitySubsetEnabled() const noexcept { return mEnabledDeviceExtensionCount == 3; }
    bool createdFor(const VulkanPhysicalDeviceGeneration& physical_device_generation) const noexcept;

    void reset() noexcept;

private:
    friend struct VulkanLogicalDeviceGenerationFactory;

    VulkanLogicalDeviceGeneration(const VulkanPhysicalDeviceGeneration& physical_device_generation,
                                  VkDevice                              device,
                                  VkQueue                               queue,
                                  PFN_vkDestroyDevice                   destroy_device) noexcept;

    PFN_vkGetInstanceProcAddr                        mGetInstanceProcAddr = nullptr;
    VkInstance                                       mInstance            = VK_NULL_HANDLE;
    VkSurfaceKHR                                     mSurface             = VK_NULL_HANDLE;
    VkPhysicalDevice                                 mPhysicalDevice      = VK_NULL_HANDLE;
    std::uint32_t                                    mPhysicalDeviceIndex = 0;
    VkDevice                                         mDevice              = VK_NULL_HANDLE;
    VkQueue                                          mQueue               = VK_NULL_HANDLE;
    std::uint32_t                                    mQueueFamilyIndex    = VK_QUEUE_FAMILY_IGNORED;
    std::uint32_t                                    mQueueIndex          = 0;
    VkPhysicalDeviceFeatures                         mEnabledFeatures{};
    VkPhysicalDeviceSwapchainMaintenance1FeaturesKHR mEnabledSwapchainMaintenance1Features{};
    std::array<std::string_view, 3>                  mEnabledDeviceExtensions{};
    std::size_t                                      mEnabledDeviceExtensionCount = 0;
    PFN_vkDestroyDevice                              mDestroyDevice               = nullptr;
};

using VulkanLogicalDeviceResolutionResult = std::variant<VulkanLogicalDeviceResolutionError, VulkanLogicalDeviceGeneration>;

VulkanLogicalDeviceResolutionResult resolveVulkanLogicalDeviceGeneration(
    const VulkanPhysicalDeviceGeneration& physical_device_generation) noexcept;

} // namespace LLRenderVulkan

#endif // LL_LLRENDERVULKANLOGICALDEVICE_H
