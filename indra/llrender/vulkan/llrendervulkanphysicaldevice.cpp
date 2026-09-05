/**
 * @file llrendervulkanphysicaldevice.cpp
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

#include "llrendervulkanphysicaldevice.h"

#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <string_view>
#include <utility>

namespace LLRenderVulkan
{

struct VulkanPhysicalDeviceGenerationFactory
{
    static VulkanPhysicalDeviceGeneration create(PFN_vkGetInstanceProcAddr         get_instance_proc_addr,
                                                 VkInstance                        instance,
                                                 VkSurfaceKHR                      surface,
                                                 VkPhysicalDevice                  physical_device,
                                                 std::uint32_t                     physical_device_index,
                                                 const VkPhysicalDeviceProperties& properties,
                                                 std::uint32_t                     queue_family_index,
                                                 const VkQueueFamilyProperties&    queue_family_properties,
                                                 bool                              swapchain_maintenance_1_supported,
                                                 bool                              portability_subset_advertised) noexcept
    {
        return VulkanPhysicalDeviceGeneration(get_instance_proc_addr, instance, surface, physical_device, physical_device_index, properties,
                                              queue_family_index, queue_family_properties, swapchain_maintenance_1_supported,
                                              portability_subset_advertised);
    }
};

VulkanPhysicalDeviceGeneration::VulkanPhysicalDeviceGeneration(PFN_vkGetInstanceProcAddr         get_instance_proc_addr,
                                                               VkInstance                        instance,
                                                               VkSurfaceKHR                      surface,
                                                               VkPhysicalDevice                  physical_device,
                                                               std::uint32_t                     physical_device_index,
                                                               const VkPhysicalDeviceProperties& properties,
                                                               std::uint32_t                     queue_family_index,
                                                               const VkQueueFamilyProperties&    queue_family_properties,
                                                               bool                              swapchain_maintenance_1_supported,
                                                               bool                              portability_subset_advertised) noexcept :
    mGetInstanceProcAddr(get_instance_proc_addr),
    mInstance(instance),
    mSurface(surface),
    mPhysicalDevice(physical_device),
    mPhysicalDeviceIndex(physical_device_index),
    mProperties(properties),
    mQueueFamilyIndex(queue_family_index),
    mQueueFamilyProperties(queue_family_properties),
    mSwapchainMaintenance1Supported(swapchain_maintenance_1_supported),
    mPortabilitySubsetAdvertised(portability_subset_advertised),
    mRequiredDeviceExtensions{ VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME,
                               portability_subset_advertised ? std::string_view("VK_KHR_portability_subset") : std::string_view{} },
    mRequiredDeviceExtensionCount(portability_subset_advertised ? 3 : 2)
{
}

VulkanPhysicalDeviceGeneration::VulkanPhysicalDeviceGeneration(VulkanPhysicalDeviceGeneration&& other) noexcept :
    mGetInstanceProcAddr(std::exchange(other.mGetInstanceProcAddr, nullptr)),
    mInstance(std::exchange(other.mInstance, VK_NULL_HANDLE)),
    mSurface(std::exchange(other.mSurface, VK_NULL_HANDLE)),
    mPhysicalDevice(std::exchange(other.mPhysicalDevice, VK_NULL_HANDLE)),
    mPhysicalDeviceIndex(std::exchange(other.mPhysicalDeviceIndex, 0)),
    mProperties(std::exchange(other.mProperties, {})),
    mQueueFamilyIndex(std::exchange(other.mQueueFamilyIndex, VK_QUEUE_FAMILY_IGNORED)),
    mQueueFamilyProperties(std::exchange(other.mQueueFamilyProperties, {})),
    mSwapchainMaintenance1Supported(std::exchange(other.mSwapchainMaintenance1Supported, false)),
    mPortabilitySubsetAdvertised(std::exchange(other.mPortabilitySubsetAdvertised, false)),
    mRequiredDeviceExtensions(std::exchange(other.mRequiredDeviceExtensions, {})),
    mRequiredDeviceExtensionCount(std::exchange(other.mRequiredDeviceExtensionCount, 0))
{
}

namespace
{

    constexpr std::uint32_t ENUMERATION_ATTEMPTS           = 4;
    constexpr char          PORTABILITY_SUBSET_EXTENSION[] = "VK_KHR_portability_subset";

    struct PresentationDispatch
    {
        PFN_vkEnumeratePhysicalDevices               mEnumeratePhysicalDevices               = nullptr;
        PFN_vkGetPhysicalDeviceProperties            mGetPhysicalDeviceProperties            = nullptr;
        PFN_vkGetPhysicalDeviceQueueFamilyProperties mGetPhysicalDeviceQueueFamilyProperties = nullptr;
        PFN_vkGetPhysicalDeviceSurfaceSupportKHR     mGetPhysicalDeviceSurfaceSupport        = nullptr;
        PFN_vkEnumerateDeviceExtensionProperties     mEnumerateDeviceExtensionProperties     = nullptr;
        PFN_vkGetPhysicalDeviceFeatures2             mGetPhysicalDeviceFeatures2             = nullptr;
    };

    VulkanPhysicalDeviceResolutionError failure(VulkanPhysicalDeviceResolutionCode         code,
                                                std::optional<VulkanPhysicalDeviceCommand> command = std::nullopt,
                                                VkResult                                   result  = VK_SUCCESS) noexcept
    {
        VulkanPhysicalDeviceResolutionError error;
        error.mCode    = code;
        error.mCommand = command;
        error.mResult  = result;
        return error;
    }

    VulkanPhysicalDeviceResolutionError candidateFailure(VulkanPhysicalDeviceResolutionCode code,
                                                         VulkanPhysicalDeviceCommand        command,
                                                         std::uint32_t                      physical_device_index,
                                                         VkResult                           result = VK_SUCCESS) noexcept
    {
        VulkanPhysicalDeviceResolutionError error = failure(code, command, result);
        error.mPhysicalDeviceIndex                = physical_device_index;
        return error;
    }

    template<typename Function>
    Function resolve(PFN_vkGetInstanceProcAddr resolver, VkInstance instance, const char* name) noexcept
    {
        return reinterpret_cast<Function>(resolver(instance, name));
    }

    std::optional<VulkanPhysicalDeviceResolutionError> resolveDispatch(const VulkanPhysicalDeviceRequest& request,
                                                                       PresentationDispatch&              dispatch) noexcept
    {
        dispatch.mEnumeratePhysicalDevices =
            resolve<PFN_vkEnumeratePhysicalDevices>(request.mGetInstanceProcAddr, request.mInstance, "vkEnumeratePhysicalDevices");
        if (!dispatch.mEnumeratePhysicalDevices)
        {
            return failure(VulkanPhysicalDeviceResolutionCode::MissingRequiredCommand,
                           VulkanPhysicalDeviceCommand::EnumeratePhysicalDevices);
        }

        dispatch.mGetPhysicalDeviceProperties =
            resolve<PFN_vkGetPhysicalDeviceProperties>(request.mGetInstanceProcAddr, request.mInstance, "vkGetPhysicalDeviceProperties");
        if (!dispatch.mGetPhysicalDeviceProperties)
        {
            return failure(VulkanPhysicalDeviceResolutionCode::MissingRequiredCommand,
                           VulkanPhysicalDeviceCommand::GetPhysicalDeviceProperties);
        }

        dispatch.mGetPhysicalDeviceQueueFamilyProperties = resolve<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(
            request.mGetInstanceProcAddr, request.mInstance, "vkGetPhysicalDeviceQueueFamilyProperties");
        if (!dispatch.mGetPhysicalDeviceQueueFamilyProperties)
        {
            return failure(VulkanPhysicalDeviceResolutionCode::MissingRequiredCommand,
                           VulkanPhysicalDeviceCommand::GetPhysicalDeviceQueueFamilyProperties);
        }

        dispatch.mGetPhysicalDeviceSurfaceSupport = resolve<PFN_vkGetPhysicalDeviceSurfaceSupportKHR>(
            request.mGetInstanceProcAddr, request.mInstance, "vkGetPhysicalDeviceSurfaceSupportKHR");
        if (!dispatch.mGetPhysicalDeviceSurfaceSupport)
        {
            return failure(VulkanPhysicalDeviceResolutionCode::MissingRequiredCommand,
                           VulkanPhysicalDeviceCommand::GetPhysicalDeviceSurfaceSupport);
        }

        dispatch.mEnumerateDeviceExtensionProperties = resolve<PFN_vkEnumerateDeviceExtensionProperties>(
            request.mGetInstanceProcAddr, request.mInstance, "vkEnumerateDeviceExtensionProperties");
        if (!dispatch.mEnumerateDeviceExtensionProperties)
        {
            return failure(VulkanPhysicalDeviceResolutionCode::MissingRequiredCommand,
                           VulkanPhysicalDeviceCommand::EnumerateDeviceExtensionProperties);
        }

        dispatch.mGetPhysicalDeviceFeatures2 =
            resolve<PFN_vkGetPhysicalDeviceFeatures2>(request.mGetInstanceProcAddr, request.mInstance, "vkGetPhysicalDeviceFeatures2");
        if (!dispatch.mGetPhysicalDeviceFeatures2)
        {
            return failure(VulkanPhysicalDeviceResolutionCode::MissingRequiredCommand,
                           VulkanPhysicalDeviceCommand::GetPhysicalDeviceFeatures2);
        }

        return std::nullopt;
    }

    template<std::size_t Size>
    std::optional<std::string_view> boundedName(const char (&name)[Size]) noexcept
    {
        const void* terminator = std::memchr(name, '\0', Size);
        if (!terminator)
        {
            return std::nullopt;
        }
        const auto* end = static_cast<const char*>(terminator);
        return std::string_view(name, static_cast<std::size_t>(end - name));
    }

    enum class ExtensionResolution : std::uint8_t
    {
        MissingSwapchain,
        MissingSwapchainMaintenance1,
        Available,
        Failed
    };

    ExtensionResolution resolveDeviceExtensions(const PresentationDispatch&          dispatch,
                                                VkPhysicalDevice                     physical_device,
                                                std::uint32_t                        physical_device_index,
                                                bool&                                portability_subset_advertised,
                                                VulkanPhysicalDeviceResolutionError& error) noexcept
    {
        for (std::uint32_t attempt = 1; attempt <= ENUMERATION_ATTEMPTS; ++attempt)
        {
            std::uint32_t  count        = 0;
            const VkResult count_result = dispatch.mEnumerateDeviceExtensionProperties(physical_device, nullptr, &count, nullptr);
            if (count_result == VK_INCOMPLETE)
            {
                continue;
            }
            if (count_result != VK_SUCCESS)
            {
                error =
                    candidateFailure(VulkanPhysicalDeviceResolutionCode::DeviceExtensionEnumerationFailure,
                                     VulkanPhysicalDeviceCommand::EnumerateDeviceExtensionProperties, physical_device_index, count_result);
                error.mEnumerationAttempt = attempt;
                return ExtensionResolution::Failed;
            }
            if (count > VULKAN_PRESENTATION_MAX_DEVICE_EXTENSIONS)
            {
                error                     = candidateFailure(VulkanPhysicalDeviceResolutionCode::DeviceExtensionCountExceeded,
                                                             VulkanPhysicalDeviceCommand::EnumerateDeviceExtensionProperties,
                                                             physical_device_index);
                error.mObservedCount      = count;
                error.mEnumerationAttempt = attempt;
                return ExtensionResolution::Failed;
            }
            if (count == 0)
            {
                return ExtensionResolution::MissingSwapchain;
            }

            std::unique_ptr<VkExtensionProperties[]> properties(new (std::nothrow) VkExtensionProperties[count]{});
            if (!properties)
            {
                error                     = candidateFailure(VulkanPhysicalDeviceResolutionCode::ScratchAllocationFailure,
                                                             VulkanPhysicalDeviceCommand::EnumerateDeviceExtensionProperties,
                                                             physical_device_index);
                error.mObservedCount      = count;
                error.mEnumerationAttempt = attempt;
                return ExtensionResolution::Failed;
            }

            const std::uint32_t capacity = count;
            const VkResult list_result   = dispatch.mEnumerateDeviceExtensionProperties(physical_device, nullptr, &count, properties.get());
            if (count > capacity)
            {
                error =
                    candidateFailure(VulkanPhysicalDeviceResolutionCode::InvalidDeviceExtensionEnumerationOutput,
                                     VulkanPhysicalDeviceCommand::EnumerateDeviceExtensionProperties, physical_device_index, list_result);
                error.mObservedCount      = count;
                error.mEnumerationAttempt = attempt;
                return ExtensionResolution::Failed;
            }
            if (list_result == VK_INCOMPLETE)
            {
                continue;
            }
            if (list_result != VK_SUCCESS)
            {
                error =
                    candidateFailure(VulkanPhysicalDeviceResolutionCode::DeviceExtensionEnumerationFailure,
                                     VulkanPhysicalDeviceCommand::EnumerateDeviceExtensionProperties, physical_device_index, list_result);
                error.mEnumerationAttempt = attempt;
                return ExtensionResolution::Failed;
            }

            bool swapchain_advertised             = false;
            bool swapchain_maintenance_advertised = false;
            portability_subset_advertised         = false;
            for (std::uint32_t index = 0; index < count; ++index)
            {
                const std::optional<std::string_view> name = boundedName(properties[index].extensionName);
                if (!name || name->empty())
                {
                    error                     = candidateFailure(VulkanPhysicalDeviceResolutionCode::MalformedDeviceExtensionProperty,
                                                                 VulkanPhysicalDeviceCommand::EnumerateDeviceExtensionProperties,
                                                                 physical_device_index);
                    error.mPropertyIndex      = index;
                    error.mEnumerationAttempt = attempt;
                    return ExtensionResolution::Failed;
                }
                swapchain_advertised |= *name == VK_KHR_SWAPCHAIN_EXTENSION_NAME;
                swapchain_maintenance_advertised |= *name == VK_KHR_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME;
                portability_subset_advertised |= *name == PORTABILITY_SUBSET_EXTENSION;
            }
            if (!swapchain_advertised)
            {
                return ExtensionResolution::MissingSwapchain;
            }
            return swapchain_maintenance_advertised ? ExtensionResolution::Available : ExtensionResolution::MissingSwapchainMaintenance1;
        }

        error                     = candidateFailure(VulkanPhysicalDeviceResolutionCode::DeviceExtensionEnumerationRetryLimitExceeded,
                                                     VulkanPhysicalDeviceCommand::EnumerateDeviceExtensionProperties, physical_device_index, VK_INCOMPLETE);
        error.mEnumerationAttempt = ENUMERATION_ATTEMPTS;
        return ExtensionResolution::Failed;
    }

    enum class QueueResolution : std::uint8_t
    {
        Missing,
        Available,
        Failed
    };

    QueueResolution resolveQueueFamily(const VulkanPhysicalDeviceRequest&   request,
                                       const PresentationDispatch&          dispatch,
                                       VkPhysicalDevice                     physical_device,
                                       std::uint32_t                        physical_device_index,
                                       std::uint32_t&                       queue_family_index,
                                       VkQueueFamilyProperties&             queue_family_properties,
                                       VulkanPhysicalDeviceResolutionError& error) noexcept
    {
        std::uint32_t count = 0;
        dispatch.mGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, nullptr);
        if (count > VULKAN_PRESENTATION_MAX_QUEUE_FAMILIES)
        {
            error                = candidateFailure(VulkanPhysicalDeviceResolutionCode::QueueFamilyCountExceeded,
                                                    VulkanPhysicalDeviceCommand::GetPhysicalDeviceQueueFamilyProperties,
                                                    physical_device_index);
            error.mObservedCount = count;
            return QueueResolution::Failed;
        }
        if (count == 0)
        {
            return QueueResolution::Missing;
        }

        std::unique_ptr<VkQueueFamilyProperties[]> properties(new (std::nothrow) VkQueueFamilyProperties[count]{});
        if (!properties)
        {
            error                = candidateFailure(VulkanPhysicalDeviceResolutionCode::ScratchAllocationFailure,
                                                    VulkanPhysicalDeviceCommand::GetPhysicalDeviceQueueFamilyProperties,
                                                    physical_device_index);
            error.mObservedCount = count;
            return QueueResolution::Failed;
        }

        const std::uint32_t capacity = count;
        dispatch.mGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, properties.get());
        if (count > capacity)
        {
            error                = candidateFailure(VulkanPhysicalDeviceResolutionCode::InvalidQueueFamilyEnumerationOutput,
                                                    VulkanPhysicalDeviceCommand::GetPhysicalDeviceQueueFamilyProperties,
                                                    physical_device_index);
            error.mObservedCount = count;
            return QueueResolution::Failed;
        }

        for (std::uint32_t index = 0; index < count; ++index)
        {
            if (properties[index].queueCount == 0 || (properties[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0)
            {
                continue;
            }

            VkBool32       support        = VK_FALSE;
            const VkResult support_result = dispatch.mGetPhysicalDeviceSurfaceSupport(physical_device, index, request.mSurface, &support);
            if (support_result != VK_SUCCESS)
            {
                error =
                    candidateFailure(VulkanPhysicalDeviceResolutionCode::SurfaceSupportQueryFailure,
                                     VulkanPhysicalDeviceCommand::GetPhysicalDeviceSurfaceSupport, physical_device_index, support_result);
                error.mQueueFamilyIndex = index;
                return QueueResolution::Failed;
            }
            if (support == VK_TRUE)
            {
                queue_family_index      = index;
                queue_family_properties = properties[index];
                return QueueResolution::Available;
            }
        }
        return QueueResolution::Missing;
    }

} // namespace

VulkanPhysicalDeviceResolutionResult resolveVulkanPhysicalDeviceGeneration(const VulkanPhysicalDeviceRequest& request) noexcept
{
    if (!request.mGetInstanceProcAddr)
    {
        return failure(VulkanPhysicalDeviceResolutionCode::InvalidGetInstanceProcAddr);
    }
    if (request.mInstance == VK_NULL_HANDLE)
    {
        return failure(VulkanPhysicalDeviceResolutionCode::InvalidInstance);
    }
    if (request.mSurface == VK_NULL_HANDLE)
    {
        return failure(VulkanPhysicalDeviceResolutionCode::InvalidSurface);
    }

    PresentationDispatch dispatch;
    if (std::optional<VulkanPhysicalDeviceResolutionError> error = resolveDispatch(request, dispatch))
    {
        return *error;
    }

    for (std::uint32_t attempt = 1; attempt <= ENUMERATION_ATTEMPTS; ++attempt)
    {
        std::uint32_t  count        = 0;
        const VkResult count_result = dispatch.mEnumeratePhysicalDevices(request.mInstance, &count, nullptr);
        if (count_result == VK_INCOMPLETE)
        {
            continue;
        }
        if (count_result != VK_SUCCESS)
        {
            auto error                = failure(VulkanPhysicalDeviceResolutionCode::PhysicalDeviceEnumerationFailure,
                                                VulkanPhysicalDeviceCommand::EnumeratePhysicalDevices, count_result);
            error.mEnumerationAttempt = attempt;
            return error;
        }
        if (count > VULKAN_PRESENTATION_MAX_PHYSICAL_DEVICES)
        {
            auto error                = failure(VulkanPhysicalDeviceResolutionCode::PhysicalDeviceCountExceeded,
                                                VulkanPhysicalDeviceCommand::EnumeratePhysicalDevices);
            error.mObservedCount      = count;
            error.mEnumerationAttempt = attempt;
            return error;
        }
        if (count == 0)
        {
            auto error                = failure(VulkanPhysicalDeviceResolutionCode::NoSuitablePhysicalDevice,
                                                VulkanPhysicalDeviceCommand::EnumeratePhysicalDevices);
            error.mEnumerationAttempt = attempt;
            return error;
        }

        std::unique_ptr<VkPhysicalDevice[]> physical_devices(new (std::nothrow) VkPhysicalDevice[count]{});
        if (!physical_devices)
        {
            auto error                = failure(VulkanPhysicalDeviceResolutionCode::ScratchAllocationFailure,
                                                VulkanPhysicalDeviceCommand::EnumeratePhysicalDevices);
            error.mObservedCount      = count;
            error.mEnumerationAttempt = attempt;
            return error;
        }

        const std::uint32_t capacity    = count;
        const VkResult      list_result = dispatch.mEnumeratePhysicalDevices(request.mInstance, &count, physical_devices.get());
        if (count > capacity)
        {
            auto error                = failure(VulkanPhysicalDeviceResolutionCode::InvalidPhysicalDeviceEnumerationOutput,
                                                VulkanPhysicalDeviceCommand::EnumeratePhysicalDevices, list_result);
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
            auto error                = failure(VulkanPhysicalDeviceResolutionCode::PhysicalDeviceEnumerationFailure,
                                                VulkanPhysicalDeviceCommand::EnumeratePhysicalDevices, list_result);
            error.mEnumerationAttempt = attempt;
            return error;
        }

        std::optional<VulkanPhysicalDeviceRejection> last_rejection;
        std::optional<std::uint32_t>                 last_rejected_index;
        for (std::uint32_t index = 0; index < count; ++index)
        {
            const VkPhysicalDevice physical_device = physical_devices[index];
            last_rejected_index                    = index;
            if (physical_device == VK_NULL_HANDLE)
            {
                last_rejection = VulkanPhysicalDeviceRejection::NullPhysicalDevice;
                continue;
            }

            VkPhysicalDeviceProperties properties{};
            dispatch.mGetPhysicalDeviceProperties(physical_device, &properties);
            if (VK_API_VERSION_VARIANT(properties.apiVersion) != 0)
            {
                last_rejection = VulkanPhysicalDeviceRejection::UnsupportedApiVariant;
                continue;
            }
            if (properties.apiVersion < VK_API_VERSION_1_1)
            {
                last_rejection = VulkanPhysicalDeviceRejection::InsufficientApiVersion;
                continue;
            }

            bool                                portability_subset_advertised = false;
            VulkanPhysicalDeviceResolutionError query_error;
            const ExtensionResolution           extension_resolution =
                resolveDeviceExtensions(dispatch, physical_device, index, portability_subset_advertised, query_error);
            if (extension_resolution == ExtensionResolution::Failed)
            {
                return query_error;
            }
            if (extension_resolution == ExtensionResolution::MissingSwapchain)
            {
                last_rejection = VulkanPhysicalDeviceRejection::MissingSwapchainExtension;
                continue;
            }
            if (extension_resolution == ExtensionResolution::MissingSwapchainMaintenance1)
            {
                last_rejection = VulkanPhysicalDeviceRejection::MissingSwapchainMaintenance1Extension;
                continue;
            }

            VkPhysicalDeviceSwapchainMaintenance1FeaturesKHR swapchain_maintenance_features{};
            swapchain_maintenance_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_KHR;

            VkPhysicalDeviceFeatures2 features{};
            features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
            features.pNext = &swapchain_maintenance_features;
            dispatch.mGetPhysicalDeviceFeatures2(physical_device, &features);
            if (swapchain_maintenance_features.swapchainMaintenance1 != VK_TRUE)
            {
                last_rejection = VulkanPhysicalDeviceRejection::SwapchainMaintenance1FeatureUnsupported;
                continue;
            }

            std::uint32_t           queue_family_index = VK_QUEUE_FAMILY_IGNORED;
            VkQueueFamilyProperties queue_family_properties{};
            const QueueResolution   queue_resolution =
                resolveQueueFamily(request, dispatch, physical_device, index, queue_family_index, queue_family_properties, query_error);
            if (queue_resolution == QueueResolution::Failed)
            {
                return query_error;
            }
            if (queue_resolution == QueueResolution::Missing)
            {
                last_rejection = VulkanPhysicalDeviceRejection::MissingUnifiedGraphicsPresentQueueFamily;
                continue;
            }

            return VulkanPhysicalDeviceGenerationFactory::create(request.mGetInstanceProcAddr, request.mInstance, request.mSurface,
                                                                 physical_device, index, properties, queue_family_index,
                                                                 queue_family_properties, true, portability_subset_advertised);
        }

        auto error =
            failure(VulkanPhysicalDeviceResolutionCode::NoSuitablePhysicalDevice, VulkanPhysicalDeviceCommand::EnumeratePhysicalDevices);
        error.mLastRejection       = last_rejection;
        error.mPhysicalDeviceIndex = last_rejected_index;
        error.mObservedCount       = count;
        error.mEnumerationAttempt  = attempt;
        return error;
    }

    auto error                = failure(VulkanPhysicalDeviceResolutionCode::PhysicalDeviceEnumerationRetryLimitExceeded,
                                        VulkanPhysicalDeviceCommand::EnumeratePhysicalDevices, VK_INCOMPLETE);
    error.mEnumerationAttempt = ENUMERATION_ATTEMPTS;
    return error;
}

} // namespace LLRenderVulkan
