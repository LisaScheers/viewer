/**
 * @file llrendervulkanphysicaldevice.h
 * @brief Loader-neutral Vulkan presentation-device selection.
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

#ifndef LL_LLRENDERVULKANPHYSICALDEVICE_H
#define LL_LLRENDERVULKANPHYSICALDEVICE_H

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

inline constexpr std::uint32_t VULKAN_PRESENTATION_MAX_PHYSICAL_DEVICES  = 256;
inline constexpr std::uint32_t VULKAN_PRESENTATION_MAX_QUEUE_FAMILIES    = 256;
inline constexpr std::uint32_t VULKAN_PRESENTATION_MAX_DEVICE_EXTENSIONS = 4096;

struct VulkanPhysicalDeviceRequest
{
    PFN_vkGetInstanceProcAddr mGetInstanceProcAddr = nullptr;
    VkInstance                mInstance            = VK_NULL_HANDLE;
    VkSurfaceKHR              mSurface             = VK_NULL_HANDLE;
};

enum class VulkanPhysicalDeviceCommand : std::uint8_t
{
    EnumeratePhysicalDevices,
    GetPhysicalDeviceProperties,
    GetPhysicalDeviceQueueFamilyProperties,
    GetPhysicalDeviceSurfaceSupport,
    EnumerateDeviceExtensionProperties
};

enum class VulkanPhysicalDeviceRejection : std::uint8_t
{
    NullPhysicalDevice,
    UnsupportedApiVariant,
    InsufficientApiVersion,
    MissingSwapchainExtension,
    MissingUnifiedGraphicsPresentQueueFamily
};

enum class VulkanPhysicalDeviceResolutionCode : std::uint8_t
{
    InvalidGetInstanceProcAddr,
    InvalidInstance,
    InvalidSurface,
    MissingRequiredCommand,
    PhysicalDeviceEnumerationFailure,
    PhysicalDeviceCountExceeded,
    PhysicalDeviceEnumerationRetryLimitExceeded,
    InvalidPhysicalDeviceEnumerationOutput,
    DeviceExtensionEnumerationFailure,
    DeviceExtensionCountExceeded,
    DeviceExtensionEnumerationRetryLimitExceeded,
    InvalidDeviceExtensionEnumerationOutput,
    MalformedDeviceExtensionProperty,
    QueueFamilyCountExceeded,
    InvalidQueueFamilyEnumerationOutput,
    SurfaceSupportQueryFailure,
    ScratchAllocationFailure,
    NoSuitablePhysicalDevice
};

struct VulkanPhysicalDeviceResolutionError
{
    VulkanPhysicalDeviceResolutionCode           mCode = VulkanPhysicalDeviceResolutionCode::InvalidGetInstanceProcAddr;
    std::optional<VulkanPhysicalDeviceCommand>   mCommand;
    std::optional<VulkanPhysicalDeviceRejection> mLastRejection;
    VkResult                                     mResult = VK_SUCCESS;
    std::optional<std::uint32_t>                 mPhysicalDeviceIndex;
    std::optional<std::uint32_t>                 mQueueFamilyIndex;
    std::optional<std::uint32_t>                 mPropertyIndex;
    std::uint32_t                                mObservedCount      = 0;
    std::uint32_t                                mEnumerationAttempt = 0;

    friend constexpr bool operator==(const VulkanPhysicalDeviceResolutionError&, const VulkanPhysicalDeviceResolutionError&) = default;
};

// This immutable generation records a presentation-capable physical-device
// choice for one exact instance and surface. It owns no Vulkan object. The
// instance, surface, resolver, and loader remain owned by its parent and must
// outlive the generation.
class VulkanPhysicalDeviceGeneration
{
public:
    VulkanPhysicalDeviceGeneration(const VulkanPhysicalDeviceGeneration&)            = delete;
    VulkanPhysicalDeviceGeneration& operator=(const VulkanPhysicalDeviceGeneration&) = delete;
    VulkanPhysicalDeviceGeneration(VulkanPhysicalDeviceGeneration&& other) noexcept;
    VulkanPhysicalDeviceGeneration& operator=(VulkanPhysicalDeviceGeneration&&) = delete;

    PFN_vkGetInstanceProcAddr         getInstanceProcAddr() const noexcept { return mGetInstanceProcAddr; }
    VkInstance                        instance() const noexcept { return mInstance; }
    VkSurfaceKHR                      surface() const noexcept { return mSurface; }
    VkPhysicalDevice                  physicalDevice() const noexcept { return mPhysicalDevice; }
    std::uint32_t                     physicalDeviceIndex() const noexcept { return mPhysicalDeviceIndex; }
    const VkPhysicalDeviceProperties& properties() const noexcept { return mProperties; }
    std::uint32_t                     apiVersion() const noexcept { return mProperties.apiVersion; }
    std::uint32_t                     queueFamilyIndex() const noexcept { return mQueueFamilyIndex; }
    const VkQueueFamilyProperties&    queueFamilyProperties() const noexcept { return mQueueFamilyProperties; }
    bool                              portabilitySubsetAdvertised() const noexcept { return mPortabilitySubsetAdvertised; }
    bool                              portabilitySubsetRequired() const noexcept { return mPortabilitySubsetAdvertised; }
    std::span<const std::string_view> requiredDeviceExtensions() const noexcept
    {
        return std::span<const std::string_view>(mRequiredDeviceExtensions.data(), mRequiredDeviceExtensionCount);
    }
    bool selectedFor(VkInstance instance, VkSurfaceKHR surface) const noexcept { return mInstance == instance && mSurface == surface; }

private:
    friend struct VulkanPhysicalDeviceGenerationFactory;

    VulkanPhysicalDeviceGeneration(PFN_vkGetInstanceProcAddr         get_instance_proc_addr,
                                   VkInstance                        instance,
                                   VkSurfaceKHR                      surface,
                                   VkPhysicalDevice                  physical_device,
                                   std::uint32_t                     physical_device_index,
                                   const VkPhysicalDeviceProperties& properties,
                                   std::uint32_t                     queue_family_index,
                                   const VkQueueFamilyProperties&    queue_family_properties,
                                   bool                              portability_subset_advertised) noexcept;

    PFN_vkGetInstanceProcAddr       mGetInstanceProcAddr = nullptr;
    VkInstance                      mInstance            = VK_NULL_HANDLE;
    VkSurfaceKHR                    mSurface             = VK_NULL_HANDLE;
    VkPhysicalDevice                mPhysicalDevice      = VK_NULL_HANDLE;
    std::uint32_t                   mPhysicalDeviceIndex = 0;
    VkPhysicalDeviceProperties      mProperties{};
    std::uint32_t                   mQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    VkQueueFamilyProperties         mQueueFamilyProperties{};
    bool                            mPortabilitySubsetAdvertised = false;
    std::array<std::string_view, 2> mRequiredDeviceExtensions{};
    std::size_t                     mRequiredDeviceExtensionCount = 0;
};

using VulkanPhysicalDeviceResolutionResult = std::variant<VulkanPhysicalDeviceResolutionError, VulkanPhysicalDeviceGeneration>;

VulkanPhysicalDeviceResolutionResult resolveVulkanPhysicalDeviceGeneration(const VulkanPhysicalDeviceRequest& request) noexcept;

} // namespace LLRenderVulkan

#endif // LL_LLRENDERVULKANPHYSICALDEVICE_H
