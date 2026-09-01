/**
 * @file llrendervulkanlogicaldevice.cpp
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

#include "llrendervulkanlogicaldevice.h"

#include <algorithm>
#include <array>
#include <string_view>
#include <utility>

namespace LLRenderVulkan
{

namespace
{
    constexpr std::uint32_t LOGICAL_DEVICE_QUEUE_INDEX     = 0;
    constexpr float         LOGICAL_DEVICE_QUEUE_PRIORITY  = 1.0f;
    constexpr char          PORTABILITY_SUBSET_EXTENSION[] = "VK_KHR_portability_subset";

    struct LogicalDeviceDispatch
    {
        PFN_vkGetPhysicalDeviceFeatures mGetPhysicalDeviceFeatures = nullptr;
        PFN_vkCreateDevice              mCreateDevice              = nullptr;
        PFN_vkDestroyDevice             mDestroyDevice             = nullptr;
        PFN_vkGetDeviceQueue            mGetDeviceQueue            = nullptr;
    };

    VulkanLogicalDeviceResolutionError failure(VulkanLogicalDeviceResolutionCode         code,
                                               std::optional<VulkanLogicalDeviceCommand> command = std::nullopt,
                                               VkResult                                  result  = VK_SUCCESS) noexcept
    {
        return { code, command, result };
    }

    template<typename Function>
    Function resolve(PFN_vkGetInstanceProcAddr resolver, VkInstance instance, const char* name) noexcept
    {
        return reinterpret_cast<Function>(resolver(instance, name));
    }

    bool hasExactExtensionPolicy(const VulkanPhysicalDeviceGeneration& generation) noexcept
    {
        const std::span<const std::string_view> extensions = generation.requiredDeviceExtensions();
        if (extensions.size() < 2 || extensions.size() > 3 || extensions[0] != VK_KHR_SWAPCHAIN_EXTENSION_NAME ||
            extensions[1] != VK_KHR_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME)
        {
            return false;
        }

        if (generation.portabilitySubsetRequired())
        {
            return extensions.size() == 3 && extensions[2] == PORTABILITY_SUBSET_EXTENSION;
        }
        return extensions.size() == 2;
    }

    bool valid(const VulkanPhysicalDeviceGeneration& generation) noexcept
    {
        const VkQueueFamilyProperties& queue_family = generation.queueFamilyProperties();
        return generation.getInstanceProcAddr() != nullptr && generation.instance() != VK_NULL_HANDLE &&
               generation.surface() != VK_NULL_HANDLE && generation.physicalDevice() != VK_NULL_HANDLE &&
               generation.queueFamilyIndex() != VK_QUEUE_FAMILY_IGNORED && queue_family.queueCount > 0 &&
               (queue_family.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0 && generation.swapchainMaintenance1Supported() &&
               hasExactExtensionPolicy(generation);
    }

    std::optional<VulkanLogicalDeviceResolutionError> resolveDispatch(const VulkanPhysicalDeviceGeneration& generation,
                                                                      LogicalDeviceDispatch&                dispatch) noexcept
    {
        const PFN_vkGetInstanceProcAddr resolver = generation.getInstanceProcAddr();
        const VkInstance                instance = generation.instance();

        dispatch.mGetPhysicalDeviceFeatures = resolve<PFN_vkGetPhysicalDeviceFeatures>(resolver, instance, "vkGetPhysicalDeviceFeatures");
        if (!dispatch.mGetPhysicalDeviceFeatures)
        {
            return failure(VulkanLogicalDeviceResolutionCode::MissingRequiredCommand,
                           VulkanLogicalDeviceCommand::GetPhysicalDeviceFeatures);
        }

        dispatch.mCreateDevice = resolve<PFN_vkCreateDevice>(resolver, instance, "vkCreateDevice");
        if (!dispatch.mCreateDevice)
        {
            return failure(VulkanLogicalDeviceResolutionCode::MissingRequiredCommand, VulkanLogicalDeviceCommand::CreateDevice);
        }

        dispatch.mDestroyDevice = resolve<PFN_vkDestroyDevice>(resolver, instance, "vkDestroyDevice");
        if (!dispatch.mDestroyDevice)
        {
            return failure(VulkanLogicalDeviceResolutionCode::MissingRequiredCommand, VulkanLogicalDeviceCommand::DestroyDevice);
        }

        dispatch.mGetDeviceQueue = resolve<PFN_vkGetDeviceQueue>(resolver, instance, "vkGetDeviceQueue");
        if (!dispatch.mGetDeviceQueue)
        {
            return failure(VulkanLogicalDeviceResolutionCode::MissingRequiredCommand, VulkanLogicalDeviceCommand::GetDeviceQueue);
        }

        return std::nullopt;
    }

} // namespace

struct VulkanLogicalDeviceGenerationFactory
{
    static VulkanLogicalDeviceGeneration create(const VulkanPhysicalDeviceGeneration& physical_device_generation,
                                                VkDevice                              device,
                                                VkQueue                               queue,
                                                PFN_vkDestroyDevice                   destroy_device) noexcept
    {
        return VulkanLogicalDeviceGeneration(physical_device_generation, device, queue, destroy_device);
    }
};

VulkanLogicalDeviceGeneration::VulkanLogicalDeviceGeneration(const VulkanPhysicalDeviceGeneration& physical_device_generation,
                                                             VkDevice                              device,
                                                             VkQueue                               queue,
                                                             PFN_vkDestroyDevice                   destroy_device) noexcept :
    mGetInstanceProcAddr(physical_device_generation.getInstanceProcAddr()),
    mInstance(physical_device_generation.instance()),
    mSurface(physical_device_generation.surface()),
    mPhysicalDevice(physical_device_generation.physicalDevice()),
    mPhysicalDeviceIndex(physical_device_generation.physicalDeviceIndex()),
    mDevice(device),
    mQueue(queue),
    mQueueFamilyIndex(physical_device_generation.queueFamilyIndex()),
    mEnabledDeviceExtensions{ VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME,
                              physical_device_generation.portabilitySubsetRequired() ? std::string_view(PORTABILITY_SUBSET_EXTENSION)
                                                                                     : std::string_view{} },
    mEnabledDeviceExtensionCount(physical_device_generation.portabilitySubsetRequired() ? 3 : 2),
    mDestroyDevice(destroy_device)
{
    mEnabledFeatures.independentBlend                           = VK_TRUE;
    mEnabledSwapchainMaintenance1Features.sType                 = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_KHR;
    mEnabledSwapchainMaintenance1Features.swapchainMaintenance1 = VK_TRUE;
}

VulkanLogicalDeviceGeneration::~VulkanLogicalDeviceGeneration() noexcept
{
    reset();
}

VulkanLogicalDeviceGeneration::VulkanLogicalDeviceGeneration(VulkanLogicalDeviceGeneration&& other) noexcept :
    mGetInstanceProcAddr(std::exchange(other.mGetInstanceProcAddr, nullptr)),
    mInstance(std::exchange(other.mInstance, VK_NULL_HANDLE)),
    mSurface(std::exchange(other.mSurface, VK_NULL_HANDLE)),
    mPhysicalDevice(std::exchange(other.mPhysicalDevice, VK_NULL_HANDLE)),
    mPhysicalDeviceIndex(std::exchange(other.mPhysicalDeviceIndex, 0)),
    mDevice(std::exchange(other.mDevice, VK_NULL_HANDLE)),
    mQueue(std::exchange(other.mQueue, VK_NULL_HANDLE)),
    mQueueFamilyIndex(std::exchange(other.mQueueFamilyIndex, VK_QUEUE_FAMILY_IGNORED)),
    mQueueIndex(std::exchange(other.mQueueIndex, 0)),
    mEnabledFeatures(std::exchange(other.mEnabledFeatures, {})),
    mEnabledSwapchainMaintenance1Features(std::exchange(other.mEnabledSwapchainMaintenance1Features, {})),
    mEnabledDeviceExtensions(std::exchange(other.mEnabledDeviceExtensions, {})),
    mEnabledDeviceExtensionCount(std::exchange(other.mEnabledDeviceExtensionCount, 0)),
    mDestroyDevice(std::exchange(other.mDestroyDevice, nullptr))
{
}

bool VulkanLogicalDeviceGeneration::createdFor(const VulkanPhysicalDeviceGeneration& physical_device_generation) const noexcept
{
    if (mDevice == VK_NULL_HANDLE || mQueue == VK_NULL_HANDLE || mGetInstanceProcAddr != physical_device_generation.getInstanceProcAddr() ||
        mInstance != physical_device_generation.instance() || mSurface != physical_device_generation.surface() ||
        mPhysicalDevice != physical_device_generation.physicalDevice() ||
        mPhysicalDeviceIndex != physical_device_generation.physicalDeviceIndex() ||
        mQueueFamilyIndex != physical_device_generation.queueFamilyIndex() ||
        !physical_device_generation.swapchainMaintenance1Supported() || !swapchainMaintenance1Enabled() ||
        mEnabledSwapchainMaintenance1Features.sType != VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_KHR ||
        mEnabledSwapchainMaintenance1Features.pNext != nullptr)
    {
        return false;
    }

    const std::span<const std::string_view> required = physical_device_generation.requiredDeviceExtensions();
    return required.size() == mEnabledDeviceExtensionCount &&
           std::equal(required.begin(), required.end(), mEnabledDeviceExtensions.begin());
}

void VulkanLogicalDeviceGeneration::reset() noexcept
{
    if (mDevice != VK_NULL_HANDLE && mDestroyDevice)
    {
        mDestroyDevice(mDevice, nullptr);
    }
    mDevice = VK_NULL_HANDLE;
    mQueue  = VK_NULL_HANDLE;
}

VulkanLogicalDeviceResolutionResult resolveVulkanLogicalDeviceGeneration(
    const VulkanPhysicalDeviceGeneration& physical_device_generation) noexcept
{
    if (!valid(physical_device_generation))
    {
        return failure(VulkanLogicalDeviceResolutionCode::InvalidPhysicalDeviceGeneration);
    }

    LogicalDeviceDispatch dispatch;
    if (std::optional<VulkanLogicalDeviceResolutionError> dispatch_error = resolveDispatch(physical_device_generation, dispatch))
    {
        return *dispatch_error;
    }

    VkPhysicalDeviceFeatures supported_features{};
    dispatch.mGetPhysicalDeviceFeatures(physical_device_generation.physicalDevice(), &supported_features);
    if (supported_features.independentBlend != VK_TRUE)
    {
        return failure(VulkanLogicalDeviceResolutionCode::IndependentBlendUnsupported,
                       VulkanLogicalDeviceCommand::GetPhysicalDeviceFeatures);
    }

    const float queue_priority = LOGICAL_DEVICE_QUEUE_PRIORITY;

    VkDeviceQueueCreateInfo queue_info{};
    queue_info.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = physical_device_generation.queueFamilyIndex();
    queue_info.queueCount       = 1;
    queue_info.pQueuePriorities = &queue_priority;

    VkPhysicalDeviceFeatures enabled_features{};
    enabled_features.independentBlend = VK_TRUE;

    const std::uint32_t              extension_count = physical_device_generation.portabilitySubsetRequired() ? 3 : 2;
    const std::array<const char*, 3> extension_names{ VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME,
                                                      PORTABILITY_SUBSET_EXTENSION };

    VkPhysicalDeviceSwapchainMaintenance1FeaturesKHR swapchain_maintenance_features{};
    swapchain_maintenance_features.sType                 = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_KHR;
    swapchain_maintenance_features.swapchainMaintenance1 = VK_TRUE;

    VkDeviceCreateInfo create_info{};
    create_info.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    create_info.pNext                   = &swapchain_maintenance_features;
    create_info.queueCreateInfoCount    = 1;
    create_info.pQueueCreateInfos       = &queue_info;
    create_info.enabledExtensionCount   = extension_count;
    create_info.ppEnabledExtensionNames = extension_names.data();
    create_info.pEnabledFeatures        = &enabled_features;

    VkDevice       pending_device = VK_NULL_HANDLE;
    const VkResult create_result =
        dispatch.mCreateDevice(physical_device_generation.physicalDevice(), &create_info, nullptr, &pending_device);
    if (create_result != VK_SUCCESS)
    {
        // The output is undefined on failure and therefore cannot be inspected
        // or passed to vkDestroyDevice, even when a broken implementation wrote
        // a non-null bit pattern into it.
        return failure(VulkanLogicalDeviceResolutionCode::DeviceCreationFailure, VulkanLogicalDeviceCommand::CreateDevice, create_result);
    }
    if (pending_device == VK_NULL_HANDLE)
    {
        return failure(VulkanLogicalDeviceResolutionCode::NullDeviceOnSuccess, VulkanLogicalDeviceCommand::CreateDevice);
    }

    VkQueue queue = VK_NULL_HANDLE;
    dispatch.mGetDeviceQueue(pending_device, physical_device_generation.queueFamilyIndex(), LOGICAL_DEVICE_QUEUE_INDEX, &queue);
    if (queue == VK_NULL_HANDLE)
    {
        dispatch.mDestroyDevice(pending_device, nullptr);
        return failure(VulkanLogicalDeviceResolutionCode::NullQueueOnSuccess, VulkanLogicalDeviceCommand::GetDeviceQueue);
    }

    return VulkanLogicalDeviceGenerationFactory::create(physical_device_generation, pending_device, queue, dispatch.mDestroyDevice);
}

} // namespace LLRenderVulkan
