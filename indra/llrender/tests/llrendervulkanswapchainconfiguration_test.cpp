/**
 * @file llrendervulkanswapchainconfiguration_test.cpp
 * @brief Tests for loader-neutral Vulkan swapchain configuration selection.
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

#include "linden_common.h"

#include "llrendervulkanswapchainconfiguration.h"
#include "lltut.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace
{
using namespace LLRenderVulkan;

template<typename Handle>
Handle fakeHandle(std::uintptr_t value) noexcept
{
    if constexpr (std::is_pointer_v<Handle>)
    {
        return reinterpret_cast<Handle>(value);
    }
    else
    {
        return static_cast<Handle>(value);
    }
}

enum class MissingCommand : std::uint8_t
{
    None,
    GetSurfaceCapabilities,
    GetSurfaceFormats,
    GetSurfacePresentModes,
    GetFormatProperties
};

struct EnumerationBehavior
{
    VkResult      mCountResult          = VK_SUCCESS;
    VkResult      mListResult           = VK_SUCCESS;
    std::uint32_t mCountIncompleteCalls = 0;
    std::uint32_t mListIncompleteCalls  = 0;
    std::uint32_t mAdvertisedCount      = 0;
    bool          mListCountOverflow    = false;
};

struct FakeState
{
    VkInstance       mInstance       = fakeHandle<VkInstance>(0x1000);
    VkSurfaceKHR     mSurface        = fakeHandle<VkSurfaceKHR>(0x2000);
    VkPhysicalDevice mPhysicalDevice = fakeHandle<VkPhysicalDevice>(0x3000);
    VkDevice         mDevice         = fakeHandle<VkDevice>(0x4000);
    VkQueue          mQueue          = fakeHandle<VkQueue>(0x5000);
    std::uint32_t    mQueueFamily    = 2;

    MissingCommand                  mMissingCommand     = MissingCommand::None;
    VkResult                        mCapabilitiesResult = VK_SUCCESS;
    VkSurfaceCapabilitiesKHR        mCapabilities{ 2,
                                            0,
                                                   { std::numeric_limits<std::uint32_t>::max(), std::numeric_limits<std::uint32_t>::max() },
                                                   { 64, 64 },
                                                   { 4096, 2160 },
                                            1,
                                            VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
                                            VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
                                            VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR | VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR |
                                                VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR | VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
                                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                                VK_IMAGE_USAGE_TRANSFER_SRC_BIT };
    std::vector<VkSurfaceFormatKHR> mFormats{ { VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR },
                                              { VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR } };
    std::vector<VkPresentModeKHR>   mPresentModes{ VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_FIFO_KHR };
    EnumerationBehavior             mFormatBehavior;
    EnumerationBehavior             mPresentBehavior;
    VkFormatProperties mFormatProperties{ 0,
                                          VK_FORMAT_FEATURE_TRANSFER_DST_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT,
                                          0 };
    std::uint32_t                    mMaxFramebufferWidth  = 4096;
    std::uint32_t                    mMaxFramebufferHeight = 2160;

    std::vector<std::string> mConfigurationLookups;
    std::size_t              mCapabilitiesCalls      = 0;
    std::size_t              mFormatCountCalls       = 0;
    std::size_t              mFormatListCalls        = 0;
    std::size_t              mPresentCountCalls      = 0;
    std::size_t              mPresentListCalls       = 0;
    std::size_t              mFormatPropertiesCalls  = 0;
    bool                     mAllResolvedBeforeQuery = false;
    VkPhysicalDevice         mQueriedPhysicalDevice  = VK_NULL_HANDLE;
    VkSurfaceKHR             mQueriedSurface         = VK_NULL_HANDLE;
    VkFormat                 mQueriedFormat          = VK_FORMAT_UNDEFINED;
    std::size_t              mDestroyCalls           = 0;
};

FakeState*  gFakeState          = nullptr;
std::size_t gAllocationCalls    = 0;
std::size_t gFailAllocationCall = 0;

class ScopedFakeState
{
public:
    explicit ScopedFakeState(FakeState& state) noexcept : mPrevious(gFakeState)
    {
        gFakeState          = &state;
        gAllocationCalls    = 0;
        gFailAllocationCall = 0;
    }
    ~ScopedFakeState() noexcept { gFakeState = mPrevious; }

    ScopedFakeState(const ScopedFakeState&)            = delete;
    ScopedFakeState& operator=(const ScopedFakeState&) = delete;

private:
    FakeState* mPrevious = nullptr;
};

template<typename Function>
PFN_vkVoidFunction eraseFunctionType(Function function) noexcept
{
    return reinterpret_cast<PFN_vkVoidFunction>(function);
}

VKAPI_ATTR VkResult VKAPI_CALL fakeEnumeratePhysicalDevices(VkInstance instance, std::uint32_t* count, VkPhysicalDevice* devices) noexcept
{
    if (!gFakeState || instance != gFakeState->mInstance || !count)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!devices)
    {
        *count = 1;
        return VK_SUCCESS;
    }
    if (*count == 0)
    {
        return VK_INCOMPLETE;
    }
    devices[0] = gFakeState->mPhysicalDevice;
    *count     = 1;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeGetPhysicalDeviceProperties(VkPhysicalDevice            physical_device,
                                                           VkPhysicalDeviceProperties* properties) noexcept
{
    if (!gFakeState || physical_device != gFakeState->mPhysicalDevice || !properties)
    {
        return;
    }
    *properties            = {};
    properties->apiVersion = VK_API_VERSION_1_1;
    properties->limits.maxFramebufferWidth  = gFakeState->mMaxFramebufferWidth;
    properties->limits.maxFramebufferHeight = gFakeState->mMaxFramebufferHeight;
    std::strncpy(properties->deviceName, "swapchain-configuration-fake", VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 1);
}

VKAPI_ATTR void VKAPI_CALL fakeGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice         physical_device,
                                                                      std::uint32_t*           count,
                                                                      VkQueueFamilyProperties* properties) noexcept
{
    if (!gFakeState || physical_device != gFakeState->mPhysicalDevice || !count)
    {
        return;
    }
    const std::uint32_t required = gFakeState->mQueueFamily + 1;
    if (!properties)
    {
        *count = required;
        return;
    }
    const std::uint32_t written = std::min(*count, required);
    for (std::uint32_t index = 0; index < written; ++index)
    {
        properties[index]            = {};
        properties[index].queueCount = 1;
        properties[index].queueFlags = index == gFakeState->mQueueFamily ? VK_QUEUE_GRAPHICS_BIT : VK_QUEUE_COMPUTE_BIT;
    }
    *count = written;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeGetPhysicalDeviceSurfaceSupport(VkPhysicalDevice physical_device,
                                                                   std::uint32_t    queue_family,
                                                                   VkSurfaceKHR     surface,
                                                                   VkBool32*        supported) noexcept
{
    if (!gFakeState || physical_device != gFakeState->mPhysicalDevice || surface != gFakeState->mSurface || !supported)
    {
        return VK_ERROR_SURFACE_LOST_KHR;
    }
    *supported = queue_family == gFakeState->mQueueFamily ? VK_TRUE : VK_FALSE;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeEnumerateDeviceExtensionProperties(VkPhysicalDevice       physical_device,
                                                                      const char*            layer_name,
                                                                      std::uint32_t*         count,
                                                                      VkExtensionProperties* properties) noexcept
{
    if (!gFakeState || physical_device != gFakeState->mPhysicalDevice || layer_name || !count)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!properties)
    {
        *count = 2;
        return VK_SUCCESS;
    }
    if (*count < 2)
    {
        return VK_INCOMPLETE;
    }
    properties[0] = {};
    std::strncpy(properties[0].extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_MAX_EXTENSION_NAME_SIZE - 1);
    properties[1] = {};
    std::strncpy(properties[1].extensionName, VK_KHR_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME, VK_MAX_EXTENSION_NAME_SIZE - 1);
    *count = 2;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeGetPhysicalDeviceFeatures2(VkPhysicalDevice physical_device, VkPhysicalDeviceFeatures2* features) noexcept
{
    if (!gFakeState || physical_device != gFakeState->mPhysicalDevice || !features)
    {
        return;
    }
    auto* maintenance = static_cast<VkPhysicalDeviceSwapchainMaintenance1FeaturesKHR*>(features->pNext);
    if (features->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 && maintenance &&
        maintenance->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_KHR && maintenance->pNext == nullptr)
    {
        maintenance->swapchainMaintenance1 = VK_TRUE;
    }
}

VKAPI_ATTR void VKAPI_CALL fakeGetPhysicalDeviceFeatures(VkPhysicalDevice physical_device, VkPhysicalDeviceFeatures* features) noexcept
{
    if (gFakeState && physical_device == gFakeState->mPhysicalDevice && features)
    {
        *features                  = {};
        features->independentBlend = VK_TRUE;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateDevice(VkPhysicalDevice          physical_device,
                                                const VkDeviceCreateInfo* create_info,
                                                const VkAllocationCallbacks*,
                                                VkDevice* device) noexcept
{
    if (!gFakeState || physical_device != gFakeState->mPhysicalDevice || !create_info || !device ||
        create_info->enabledExtensionCount != 2 || !create_info->ppEnabledExtensionNames || !create_info->ppEnabledExtensionNames[0] ||
        !create_info->ppEnabledExtensionNames[1] ||
        std::strcmp(create_info->ppEnabledExtensionNames[0], VK_KHR_SWAPCHAIN_EXTENSION_NAME) != 0 ||
        std::strcmp(create_info->ppEnabledExtensionNames[1], VK_KHR_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME) != 0)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    const auto* maintenance = static_cast<const VkPhysicalDeviceSwapchainMaintenance1FeaturesKHR*>(create_info->pNext);
    if (!maintenance || maintenance->sType != VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_KHR ||
        maintenance->pNext != nullptr || maintenance->swapchainMaintenance1 != VK_TRUE)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    *device = gFakeState->mDevice;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyDevice(VkDevice device, const VkAllocationCallbacks*) noexcept
{
    if (gFakeState && device == gFakeState->mDevice)
    {
        ++gFakeState->mDestroyCalls;
    }
}

VKAPI_ATTR void VKAPI_CALL fakeGetDeviceQueue(VkDevice      device,
                                              std::uint32_t queue_family,
                                              std::uint32_t queue_index,
                                              VkQueue*      queue) noexcept
{
    if (gFakeState && device == gFakeState->mDevice && queue_family == gFakeState->mQueueFamily && queue_index == 0 && queue)
    {
        *queue = gFakeState->mQueue;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeGetSurfaceCapabilities(VkPhysicalDevice          physical_device,
                                                          VkSurfaceKHR              surface,
                                                          VkSurfaceCapabilitiesKHR* capabilities) noexcept
{
    if (!gFakeState || !capabilities)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    ++gFakeState->mCapabilitiesCalls;
    gFakeState->mQueriedPhysicalDevice  = physical_device;
    gFakeState->mQueriedSurface         = surface;
    gFakeState->mAllResolvedBeforeQuery = gFakeState->mConfigurationLookups.size() == 4;
    if (gFakeState->mCapabilitiesResult == VK_SUCCESS)
    {
        *capabilities = gFakeState->mCapabilities;
    }
    return gFakeState->mCapabilitiesResult;
}

template<typename Value>
VkResult enumerateValues(const std::vector<Value>& values,
                         EnumerationBehavior&      behavior,
                         std::size_t&              count_calls,
                         std::size_t&              list_calls,
                         std::uint32_t*            count,
                         Value*                    output) noexcept
{
    if (!count)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!output)
    {
        ++count_calls;
        *count = behavior.mAdvertisedCount != 0 ? behavior.mAdvertisedCount : static_cast<std::uint32_t>(values.size());
        if (behavior.mCountIncompleteCalls != 0)
        {
            --behavior.mCountIncompleteCalls;
            return VK_INCOMPLETE;
        }
        return behavior.mCountResult;
    }

    ++list_calls;
    const std::uint32_t capacity = *count;
    if (behavior.mListCountOverflow)
    {
        *count = capacity + 1;
        return behavior.mListResult;
    }
    const std::uint32_t written = std::min<std::uint32_t>(capacity, static_cast<std::uint32_t>(values.size()));
    std::copy_n(values.begin(), written, output);
    *count = written;
    if (behavior.mListIncompleteCalls != 0)
    {
        --behavior.mListIncompleteCalls;
        return VK_INCOMPLETE;
    }
    return behavior.mListResult;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeGetSurfaceFormats(VkPhysicalDevice    physical_device,
                                                     VkSurfaceKHR        surface,
                                                     std::uint32_t*      count,
                                                     VkSurfaceFormatKHR* formats) noexcept
{
    if (!gFakeState || physical_device != gFakeState->mPhysicalDevice || surface != gFakeState->mSurface)
    {
        return VK_ERROR_SURFACE_LOST_KHR;
    }
    return enumerateValues(gFakeState->mFormats, gFakeState->mFormatBehavior, gFakeState->mFormatCountCalls, gFakeState->mFormatListCalls,
                           count, formats);
}

VKAPI_ATTR VkResult VKAPI_CALL fakeGetSurfacePresentModes(VkPhysicalDevice  physical_device,
                                                          VkSurfaceKHR      surface,
                                                          std::uint32_t*    count,
                                                          VkPresentModeKHR* modes) noexcept
{
    if (!gFakeState || physical_device != gFakeState->mPhysicalDevice || surface != gFakeState->mSurface)
    {
        return VK_ERROR_SURFACE_LOST_KHR;
    }
    return enumerateValues(gFakeState->mPresentModes, gFakeState->mPresentBehavior, gFakeState->mPresentCountCalls,
                           gFakeState->mPresentListCalls, count, modes);
}

VKAPI_ATTR void VKAPI_CALL fakeGetFormatProperties(VkPhysicalDevice physical_device,
                                                   VkFormat         format,
                                                   VkFormatProperties* properties) noexcept
{
    if (!gFakeState || physical_device != gFakeState->mPhysicalDevice || !properties)
    {
        return;
    }
    ++gFakeState->mFormatPropertiesCalls;
    gFakeState->mQueriedFormat = format;
    *properties                = gFakeState->mFormatProperties;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL fakeGetInstanceProcAddr(VkInstance instance, const char* name) noexcept
{
    if (!gFakeState || instance != gFakeState->mInstance || !name)
    {
        return nullptr;
    }
    if (std::strcmp(name, "vkEnumeratePhysicalDevices") == 0)
        return eraseFunctionType(fakeEnumeratePhysicalDevices);
    if (std::strcmp(name, "vkGetPhysicalDeviceProperties") == 0)
        return eraseFunctionType(fakeGetPhysicalDeviceProperties);
    if (std::strcmp(name, "vkGetPhysicalDeviceQueueFamilyProperties") == 0)
        return eraseFunctionType(fakeGetPhysicalDeviceQueueFamilyProperties);
    if (std::strcmp(name, "vkGetPhysicalDeviceSurfaceSupportKHR") == 0)
        return eraseFunctionType(fakeGetPhysicalDeviceSurfaceSupport);
    if (std::strcmp(name, "vkEnumerateDeviceExtensionProperties") == 0)
        return eraseFunctionType(fakeEnumerateDeviceExtensionProperties);
    if (std::strcmp(name, "vkGetPhysicalDeviceFeatures2") == 0)
        return eraseFunctionType(fakeGetPhysicalDeviceFeatures2);
    if (std::strcmp(name, "vkGetPhysicalDeviceFeatures") == 0)
        return eraseFunctionType(fakeGetPhysicalDeviceFeatures);
    if (std::strcmp(name, "vkCreateDevice") == 0)
        return eraseFunctionType(fakeCreateDevice);
    if (std::strcmp(name, "vkDestroyDevice") == 0)
        return eraseFunctionType(fakeDestroyDevice);
    if (std::strcmp(name, "vkGetDeviceQueue") == 0)
        return eraseFunctionType(fakeGetDeviceQueue);

    gFakeState->mConfigurationLookups.emplace_back(name);
    if (std::strcmp(name, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR") == 0)
    {
        return gFakeState->mMissingCommand == MissingCommand::GetSurfaceCapabilities ? nullptr
                                                                                     : eraseFunctionType(fakeGetSurfaceCapabilities);
    }
    if (std::strcmp(name, "vkGetPhysicalDeviceSurfaceFormatsKHR") == 0)
    {
        return gFakeState->mMissingCommand == MissingCommand::GetSurfaceFormats ? nullptr : eraseFunctionType(fakeGetSurfaceFormats);
    }
    if (std::strcmp(name, "vkGetPhysicalDeviceSurfacePresentModesKHR") == 0)
    {
        return gFakeState->mMissingCommand == MissingCommand::GetSurfacePresentModes ? nullptr
                                                                                     : eraseFunctionType(fakeGetSurfacePresentModes);
    }
    if (std::strcmp(name, "vkGetPhysicalDeviceFormatProperties") == 0)
    {
        return gFakeState->mMissingCommand == MissingCommand::GetFormatProperties ? nullptr
                                                                                  : eraseFunctionType(fakeGetFormatProperties);
    }
    return nullptr;
}

struct Parents
{
    VulkanPhysicalDeviceGeneration mPhysical;
    VulkanLogicalDeviceGeneration  mLogical;
};

Parents makeParents(FakeState& state)
{
    VulkanPhysicalDeviceResolutionResult physical_result =
        resolveVulkanPhysicalDeviceGeneration(VulkanPhysicalDeviceRequest{ fakeGetInstanceProcAddr, state.mInstance, state.mSurface });
    tut::ensure("the physical-device fixture resolves", std::holds_alternative<VulkanPhysicalDeviceGeneration>(physical_result));
    auto physical = std::get<VulkanPhysicalDeviceGeneration>(std::move(physical_result));

    VulkanLogicalDeviceResolutionResult logical_result = resolveVulkanLogicalDeviceGeneration(physical);
    tut::ensure("the logical-device fixture resolves", std::holds_alternative<VulkanLogicalDeviceGeneration>(logical_result));
    auto logical = std::get<VulkanLogicalDeviceGeneration>(std::move(logical_result));
    state.mConfigurationLookups.clear();
    return { std::move(physical), std::move(logical) };
}

const VulkanSwapchainConfigurationResolutionError& requireError(const VulkanSwapchainConfigurationResolutionResult& result)
{
    const auto* error = std::get_if<VulkanSwapchainConfigurationResolutionError>(&result);
    tut::ensure("swapchain configuration resolution returns an error", error != nullptr);
    return *error;
}

void ensureCode(const VulkanSwapchainConfigurationResolutionResult& result, VulkanSwapchainConfigurationResolutionCode code)
{
    tut::ensure("the exact swapchain configuration error is reported", requireError(result).mCode == code);
}

VulkanSwapchainConfigurationGeneration takeGeneration(VulkanSwapchainConfigurationResolutionResult&& result)
{
    tut::ensure("swapchain configuration resolution returns a generation",
                std::holds_alternative<VulkanSwapchainConfigurationGeneration>(result));
    return std::get<VulkanSwapchainConfigurationGeneration>(std::move(result));
}

void allocationCheckpoint()
{
    ++gAllocationCalls;
    if (gAllocationCalls == gFailAllocationCall)
    {
        throw std::bad_alloc();
    }
}

} // namespace

namespace tut
{

struct render_vulkan_swapchain_configuration_test
{
};

using render_vulkan_swapchain_configuration_group  = test_group<render_vulkan_swapchain_configuration_test>;
using render_vulkan_swapchain_configuration_object = render_vulkan_swapchain_configuration_group::object;
render_vulkan_swapchain_configuration_group render_vulkan_swapchain_configuration_tests("render Vulkan swapchain configuration");

template<>
template<>
void render_vulkan_swapchain_configuration_object::test<1>()
{
    static_assert(!std::is_default_constructible_v<VulkanSwapchainConfigurationGeneration>);
    static_assert(!std::is_copy_constructible_v<VulkanSwapchainConfigurationGeneration>);
    static_assert(!std::is_copy_assignable_v<VulkanSwapchainConfigurationGeneration>);
    static_assert(std::is_nothrow_move_constructible_v<VulkanSwapchainConfigurationGeneration>);
    static_assert(!std::is_move_assignable_v<VulkanSwapchainConfigurationGeneration>);
    static_assert(std::variant_size_v<VulkanSwapchainConfigurationResolutionResult> == 2);
    static_assert(noexcept(resolveVulkanSwapchainConfigurationGeneration(std::declval<const VulkanPhysicalDeviceGeneration&>(),
                                                                         std::declval<const VulkanLogicalDeviceGeneration&>(), {})));
    static_assert(static_cast<std::uint8_t>(VulkanSwapchainConfigurationCommand::GetPhysicalDeviceSurfacePresentModes) == 2);
    static_assert(static_cast<std::uint8_t>(VulkanSwapchainConfigurationCommand::GetPhysicalDeviceFormatProperties) == 3);
    static_assert(static_cast<std::uint8_t>(VulkanSwapchainConfigurationResolutionCode::ScratchAllocationFailure) == 22);
    static_assert(static_cast<std::uint8_t>(
                      VulkanSwapchainConfigurationResolutionCode::SurfaceTransferDestinationUsageUnsupported) == 23);
    static_assert(static_cast<std::uint8_t>(
                      VulkanSwapchainConfigurationResolutionCode::SelectedFormatTransferDestinationUnsupported) == 24);
    static_assert(static_cast<std::uint8_t>(
                      VulkanSwapchainConfigurationResolutionCode::SelectedFormatColorAttachmentUnsupported) == 25);
    static_assert(static_cast<std::uint8_t>(
                      VulkanSwapchainConfigurationResolutionCode::SelectedImageExtentExceedsFramebufferLimits) == 26);

    FakeState       state;
    ScopedFakeState scope(state);
    auto            parents        = makeParents(state);
    auto            moved_physical = std::move(parents.mPhysical);

    ensureCode(resolveVulkanSwapchainConfigurationGeneration(parents.mPhysical, parents.mLogical, { 800, 600 }),
               VulkanSwapchainConfigurationResolutionCode::InvalidPhysicalDeviceGeneration);
    ensure("invalid physical provenance resolves no configuration command", state.mConfigurationLookups.empty());

    ensureCode(resolveVulkanSwapchainConfigurationGeneration(moved_physical, parents.mLogical, { 0, 600 }),
               VulkanSwapchainConfigurationResolutionCode::InvalidDrawableExtent);
    parents.mLogical.reset();
    ensureCode(resolveVulkanSwapchainConfigurationGeneration(moved_physical, parents.mLogical, { 800, 600 }),
               VulkanSwapchainConfigurationResolutionCode::InvalidLogicalDeviceGeneration);
}

template<>
template<>
void render_vulkan_swapchain_configuration_object::test<2>()
{
    constexpr std::array missing{
        std::pair{ MissingCommand::GetSurfaceCapabilities, VulkanSwapchainConfigurationCommand::GetPhysicalDeviceSurfaceCapabilities },
        std::pair{ MissingCommand::GetSurfaceFormats, VulkanSwapchainConfigurationCommand::GetPhysicalDeviceSurfaceFormats },
        std::pair{ MissingCommand::GetSurfacePresentModes, VulkanSwapchainConfigurationCommand::GetPhysicalDeviceSurfacePresentModes },
        std::pair{ MissingCommand::GetFormatProperties, VulkanSwapchainConfigurationCommand::GetPhysicalDeviceFormatProperties }
    };

    for (std::size_t index = 0; index < missing.size(); ++index)
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        state.mMissingCommand   = missing[index].first;

        const auto  result = resolveVulkanSwapchainConfigurationGeneration(parents.mPhysical, parents.mLogical, { 800, 600 });
        const auto& error  = requireError(result);
        ensure("missing dispatch reports exact code and command",
               error.mCode == VulkanSwapchainConfigurationResolutionCode::MissingRequiredCommand &&
                   error.mCommand == missing[index].second);
        ensure("resolution stops at the missing command", state.mConfigurationLookups.size() == index + 1);
        ensure("incomplete dispatch runs no surface query", state.mCapabilitiesCalls == 0);
    }
}

template<>
template<>
void render_vulkan_swapchain_configuration_object::test<3>()
{
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents   = makeParents(state);
        state.mCapabilitiesResult = VK_ERROR_SURFACE_LOST_KHR;
        const auto  result        = resolveVulkanSwapchainConfigurationGeneration(parents.mPhysical, parents.mLogical, { 800, 600 });
        const auto& error         = requireError(result);
        ensure("capability failure preserves command and result",
               error.mCode == VulkanSwapchainConfigurationResolutionCode::SurfaceCapabilitiesQueryFailure &&
                   error.mCommand == VulkanSwapchainConfigurationCommand::GetPhysicalDeviceSurfaceCapabilities &&
                   error.mResult == VK_ERROR_SURFACE_LOST_KHR);
        ensure("all commands resolve before querying the exact parent",
               state.mAllResolvedBeforeQuery && state.mQueriedPhysicalDevice == state.mPhysicalDevice &&
                   state.mQueriedSurface == state.mSurface);
    }

    constexpr std::array invalid_codes{ VulkanSwapchainConfigurationResolutionCode::InvalidImageCountRange,
                                        VulkanSwapchainConfigurationResolutionCode::InvalidExtentRange,
                                        VulkanSwapchainConfigurationResolutionCode::SurfaceUnavailable,
                                        VulkanSwapchainConfigurationResolutionCode::InvalidArrayLayerCount,
                                        VulkanSwapchainConfigurationResolutionCode::InvalidCurrentTransform,
                                        VulkanSwapchainConfigurationResolutionCode::MissingCompositeAlpha,
                                        VulkanSwapchainConfigurationResolutionCode::ColorAttachmentUsageUnsupported,
                                        VulkanSwapchainConfigurationResolutionCode::SurfaceTransferDestinationUsageUnsupported };
    for (std::size_t index = 0; index < invalid_codes.size(); ++index)
    {
        FakeState       state;
        ScopedFakeState scope(state);
        if (index == 0)
            state.mCapabilities.minImageCount = 0;
        if (index == 1)
            state.mCapabilities.minImageExtent.width = state.mCapabilities.maxImageExtent.width + 1;
        if (index == 2)
            state.mCapabilities.currentExtent = { 0, 0 };
        if (index == 3)
            state.mCapabilities.maxImageArrayLayers = 0;
        if (index == 4)
            state.mCapabilities.currentTransform = VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR;
        if (index == 5)
            state.mCapabilities.supportedCompositeAlpha = 0;
        if (index == 6)
            state.mCapabilities.supportedUsageFlags = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if (index == 7)
            state.mCapabilities.supportedUsageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        auto parents = makeParents(state);
        ensureCode(resolveVulkanSwapchainConfigurationGeneration(parents.mPhysical, parents.mLogical, { 800, 600 }), invalid_codes[index]);
        ensure("invalid capabilities stop before enumeration", state.mFormatCountCalls == 0 && state.mPresentCountCalls == 0);
    }
}

template<>
template<>
void render_vulkan_swapchain_configuration_object::test<4>()
{
    {
        FakeState       state;
        ScopedFakeState scope(state);
        state.mFormatBehavior.mCountResult = VK_ERROR_OUT_OF_HOST_MEMORY;
        auto        parents                = makeParents(state);
        const auto  result = resolveVulkanSwapchainConfigurationGeneration(parents.mPhysical, parents.mLogical, { 800, 600 });
        const auto& error  = requireError(result);
        ensure("format count failure is exact",
               error.mCode == VulkanSwapchainConfigurationResolutionCode::SurfaceFormatEnumerationFailure &&
                   error.mResult == VK_ERROR_OUT_OF_HOST_MEMORY && error.mEnumerationAttempt == 1);
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        state.mFormatBehavior.mAdvertisedCount = VULKAN_SWAPCHAIN_MAX_SURFACE_FORMATS + 1;
        auto        parents                    = makeParents(state);
        const auto  result = resolveVulkanSwapchainConfigurationGeneration(parents.mPhysical, parents.mLogical, { 800, 600 });
        const auto& error  = requireError(result);
        ensure("format count is bounded before allocation",
               error.mCode == VulkanSwapchainConfigurationResolutionCode::SurfaceFormatCountExceeded &&
                   error.mObservedCount == VULKAN_SWAPCHAIN_MAX_SURFACE_FORMATS + 1);
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        state.mFormatBehavior.mListCountOverflow = true;
        auto parents                             = makeParents(state);
        ensureCode(resolveVulkanSwapchainConfigurationGeneration(parents.mPhysical, parents.mLogical, { 800, 600 }),
                   VulkanSwapchainConfigurationResolutionCode::InvalidSurfaceFormatEnumerationOutput);
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        state.mFormatBehavior.mListIncompleteCalls = 1;
        auto parents                               = makeParents(state);
        auto generation = takeGeneration(resolveVulkanSwapchainConfigurationGeneration(parents.mPhysical, parents.mLogical, { 800, 600 }));
        ensure("one incomplete format list retries a full count and list cycle",
               state.mFormatCountCalls == 2 && state.mFormatListCalls == 2 &&
                   generation.surfaceFormat().format == VK_FORMAT_B8G8R8A8_UNORM);
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        state.mFormatBehavior.mCountIncompleteCalls = 5;
        auto        parents                         = makeParents(state);
        const auto  result = resolveVulkanSwapchainConfigurationGeneration(parents.mPhysical, parents.mLogical, { 800, 600 });
        const auto& error  = requireError(result);
        ensure("format retry exhaustion is typed and bounded",
               error.mCode == VulkanSwapchainConfigurationResolutionCode::SurfaceFormatEnumerationRetryLimitExceeded &&
                   error.mResult == VK_INCOMPLETE && error.mEnumerationAttempt == 4 && state.mFormatCountCalls == 4);
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        state.mFormats = { { VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR } };
        auto parents   = makeParents(state);
        ensureCode(resolveVulkanSwapchainConfigurationGeneration(parents.mPhysical, parents.mLogical, { 800, 600 }),
                   VulkanSwapchainConfigurationResolutionCode::NoCompatibleSurfaceFormat);
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        state.mFormatProperties.optimalTilingFeatures = 0;
        auto        parents = makeParents(state);
        const auto  result = resolveVulkanSwapchainConfigurationGeneration(parents.mPhysical, parents.mLogical, { 800, 600 });
        const auto& error  = requireError(result);
        ensure("selected-format transfer-destination rejection is exact",
               error.mCode == VulkanSwapchainConfigurationResolutionCode::SelectedFormatTransferDestinationUnsupported &&
                   error.mCommand == VulkanSwapchainConfigurationCommand::GetPhysicalDeviceFormatProperties &&
                   error.mResult == VK_SUCCESS);
        ensure("selected-format rejection queries the chosen BGRA format before present modes",
               state.mFormatPropertiesCalls == 1 && state.mQueriedFormat == VK_FORMAT_B8G8R8A8_UNORM &&
                   state.mPresentCountCalls == 0 && state.mPresentListCalls == 0);
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        state.mFormatProperties.optimalTilingFeatures = VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
        auto        parents = makeParents(state);
        const auto  result = resolveVulkanSwapchainConfigurationGeneration(parents.mPhysical, parents.mLogical, { 800, 600 });
        const auto& error  = requireError(result);
        ensure("selected-format color-attachment rejection is exact",
               error.mCode == VulkanSwapchainConfigurationResolutionCode::SelectedFormatColorAttachmentUnsupported &&
                   error.mCommand == VulkanSwapchainConfigurationCommand::GetPhysicalDeviceFormatProperties &&
                   error.mResult == VK_SUCCESS);
        ensure("color-attachment rejection follows the successful transfer gate and precedes present modes",
               state.mFormatPropertiesCalls == 1 && state.mQueriedFormat == VK_FORMAT_B8G8R8A8_UNORM &&
                   state.mPresentCountCalls == 0 && state.mPresentListCalls == 0);
    }
}

template<>
template<>
void render_vulkan_swapchain_configuration_object::test<5>()
{
    {
        FakeState       state;
        ScopedFakeState scope(state);
        state.mPresentBehavior.mListResult = VK_ERROR_OUT_OF_DEVICE_MEMORY;
        auto        parents                = makeParents(state);
        const auto  result = resolveVulkanSwapchainConfigurationGeneration(parents.mPhysical, parents.mLogical, { 800, 600 });
        const auto& error  = requireError(result);
        ensure("present-mode list failure is exact",
               error.mCode == VulkanSwapchainConfigurationResolutionCode::PresentModeEnumerationFailure &&
                   error.mResult == VK_ERROR_OUT_OF_DEVICE_MEMORY && error.mEnumerationAttempt == 1);
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        state.mPresentBehavior.mAdvertisedCount = VULKAN_SWAPCHAIN_MAX_PRESENT_MODES + 1;
        auto        parents                     = makeParents(state);
        const auto  result = resolveVulkanSwapchainConfigurationGeneration(parents.mPhysical, parents.mLogical, { 800, 600 });
        const auto& error  = requireError(result);
        ensure("present-mode count is bounded before allocation",
               error.mCode == VulkanSwapchainConfigurationResolutionCode::PresentModeCountExceeded &&
                   error.mObservedCount == VULKAN_SWAPCHAIN_MAX_PRESENT_MODES + 1);
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        state.mPresentBehavior.mListIncompleteCalls = 5;
        auto        parents                         = makeParents(state);
        const auto  result = resolveVulkanSwapchainConfigurationGeneration(parents.mPhysical, parents.mLogical, { 800, 600 });
        const auto& error  = requireError(result);
        ensure("present-mode retry exhaustion is typed and bounded",
               error.mCode == VulkanSwapchainConfigurationResolutionCode::PresentModeEnumerationRetryLimitExceeded &&
                   error.mResult == VK_INCOMPLETE && error.mEnumerationAttempt == 4 && state.mPresentCountCalls == 4 &&
                   state.mPresentListCalls == 4);
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        state.mPresentModes = { VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_MAILBOX_KHR };
        auto parents        = makeParents(state);
        ensureCode(resolveVulkanSwapchainConfigurationGeneration(parents.mPhysical, parents.mLogical, { 800, 600 }),
                   VulkanSwapchainConfigurationResolutionCode::FifoPresentModeUnsupported);
    }
}

template<>
template<>
void render_vulkan_swapchain_configuration_object::test<6>()
{
    FakeState       state;
    ScopedFakeState scope(state);
    state.mCapabilities.minImageExtent = { 320, 240 };
    state.mCapabilities.maxImageExtent = { 1920, 1080 };
    state.mMaxFramebufferWidth         = 1920;
    state.mMaxFramebufferHeight        = 240;
    auto parents                       = makeParents(state);
    auto generation = takeGeneration(resolveVulkanSwapchainConfigurationGeneration(parents.mPhysical, parents.mLogical, { 2560, 200 }));

    ensure("all four configuration commands resolve in exact order",
           state.mConfigurationLookups == std::vector<std::string>{ "vkGetPhysicalDeviceSurfaceCapabilitiesKHR",
                                                                    "vkGetPhysicalDeviceSurfaceFormatsKHR",
                                                                    "vkGetPhysicalDeviceSurfacePresentModesKHR",
                                                                    "vkGetPhysicalDeviceFormatProperties" });
    ensure("format priority selects BGRA8 UNORM nonlinear sRGB independent of driver order",
           generation.surfaceFormat().format == VK_FORMAT_B8G8R8A8_UNORM &&
               generation.surfaceFormat().colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);
    ensure("variable extent clamps authenticated drawable pixels",
           generation.drawableExtent().width == 2560 && generation.drawableExtent().height == 200 &&
               generation.imageExtent().width == 1920 && generation.imageExtent().height == 240);
    ensure("the conservative create policy is exact",
           generation.presentMode() == VK_PRESENT_MODE_FIFO_KHR && generation.imageCount() == 3 && generation.imageArrayLayers() == 1 &&
               generation.imageUsage() == (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT) &&
               generation.imageSharingMode() == VK_SHARING_MODE_EXCLUSIVE &&
               generation.preTransform() == VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR &&
               generation.compositeAlpha() == VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR && generation.clipped() == VK_TRUE);
    ensure("the configuration authenticates the exact parent chain and extent",
           generation.createdFor(parents.mPhysical, parents.mLogical, { 2560, 200 }) &&
               !generation.createdFor(parents.mPhysical, parents.mLogical, { 1920, 240 }) && generation.device() == state.mDevice &&
               generation.queueFamilyIndex() == state.mQueueFamily);
    ensure("the selected BGRA format is authenticated for optimal transfer destination and color attachment",
           state.mFormatPropertiesCalls == 1 && state.mQueriedFormat == VK_FORMAT_B8G8R8A8_UNORM);

    auto moved = std::move(generation);
    ensure("move transfers exact configuration provenance",
           moved.createdFor(parents.mPhysical, parents.mLogical, { 2560, 200 }) && generation.device() == VK_NULL_HANDLE);
}

template<>
template<>
void render_vulkan_swapchain_configuration_object::test<7>()
{
    constexpr std::array alpha_cases{
        std::pair{ VkCompositeAlphaFlagsKHR(VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR | VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR),
                   VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR },
        std::pair{ VkCompositeAlphaFlagsKHR(VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR | VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR),
                   VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR },
        std::pair{ VkCompositeAlphaFlagsKHR(VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR | VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR),
                   VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR },
        std::pair{ VkCompositeAlphaFlagsKHR(VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR), VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR }
    };

    for (const auto& [supported, expected] : alpha_cases)
    {
        FakeState       state;
        ScopedFakeState scope(state);
        state.mCapabilities.currentExtent           = { 1280, 720 };
        state.mCapabilities.maxImageCount           = 2;
        state.mCapabilities.supportedCompositeAlpha = supported;
        state.mFormats                              = { { VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR } };
        auto parents                                = makeParents(state);
        auto generation = takeGeneration(resolveVulkanSwapchainConfigurationGeneration(parents.mPhysical, parents.mLogical, { 640, 480 }));
        ensure("fixed surface extent overrides drawable request",
               generation.imageExtent().width == 1280 && generation.imageExtent().height == 720);
        ensure("bounded maximum caps min-plus-one image policy", generation.imageCount() == 2);
        ensure("RGBA8 is the exact second format fallback", generation.surfaceFormat().format == VK_FORMAT_R8G8B8A8_UNORM);
        ensure("composite-alpha priority is deterministic", generation.compositeAlpha() == expected);
    }
}

template<>
template<>
void render_vulkan_swapchain_configuration_object::test<8>()
{
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        gFailAllocationCall     = 1;
        const auto result =
            VulkanSwapchainConfigurationDetail::resolve(parents.mPhysical, parents.mLogical, { 800, 600 }, allocationCheckpoint);
        const auto& error = requireError(result);
        ensure("format scratch-allocation failure is typed",
               error.mCode == VulkanSwapchainConfigurationResolutionCode::ScratchAllocationFailure &&
                   error.mCommand == VulkanSwapchainConfigurationCommand::GetPhysicalDeviceSurfaceFormats && gAllocationCalls == 1 &&
                   state.mPresentCountCalls == 0);
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        gFailAllocationCall     = 2;
        const auto result =
            VulkanSwapchainConfigurationDetail::resolve(parents.mPhysical, parents.mLogical, { 800, 600 }, allocationCheckpoint);
        const auto& error = requireError(result);
        ensure("present-mode scratch-allocation failure is typed",
               error.mCode == VulkanSwapchainConfigurationResolutionCode::ScratchAllocationFailure &&
                   error.mCommand == VulkanSwapchainConfigurationCommand::GetPhysicalDeviceSurfacePresentModes && gAllocationCalls == 2);

        gFailAllocationCall = 0;
        auto retried        = takeGeneration(
            VulkanSwapchainConfigurationDetail::resolve(parents.mPhysical, parents.mLogical, { 800, 600 }, allocationCheckpoint));
        ensure("unchanged parents remain reusable after scratch failure",
               retried.createdFor(parents.mPhysical, parents.mLogical, { 800, 600 }));
    }
}

template<>
template<>
void render_vulkan_swapchain_configuration_object::test<9>()
{
    const auto ensure_invalid = [](const char* message, VulkanSwapchainConfigurationResolutionCode expected, auto&& mutate_capabilities)
    {
        FakeState       state;
        ScopedFakeState scope(state);
        mutate_capabilities(state.mCapabilities);
        auto parents = makeParents(state);
        ensureCode(resolveVulkanSwapchainConfigurationGeneration(parents.mPhysical, parents.mLogical, { 800, 600 }), expected);
        ensure(message, state.mFormatCountCalls == 0 && state.mPresentCountCalls == 0);
    };

    ensure_invalid("a maximum image count below the minimum stops before enumeration",
                   VulkanSwapchainConfigurationResolutionCode::InvalidImageCountRange,
                   [](VkSurfaceCapabilitiesKHR& capabilities) { capabilities.maxImageCount = capabilities.minImageCount - 1; });
    ensure_invalid("a half-variable current extent stops before enumeration",
                   VulkanSwapchainConfigurationResolutionCode::InvalidExtentRange,
                   [](VkSurfaceCapabilitiesKHR& capabilities) { capabilities.currentExtent.height = 720; });
    ensure_invalid("a fixed extent outside the supported range stops before enumeration",
                   VulkanSwapchainConfigurationResolutionCode::SurfaceUnavailable,
                   [](VkSurfaceCapabilitiesKHR& capabilities) { capabilities.currentExtent = { 32, 720 }; });
    ensure_invalid("a variable surface with a zero maximum extent stops before enumeration",
                   VulkanSwapchainConfigurationResolutionCode::SurfaceUnavailable,
                   [](VkSurfaceCapabilitiesKHR& capabilities)
                   {
                       capabilities.minImageExtent.width = 0;
                       capabilities.maxImageExtent.width = 0;
                   });
    ensure_invalid("a zero current transform stops before enumeration",
                   VulkanSwapchainConfigurationResolutionCode::InvalidCurrentTransform,
                   [](VkSurfaceCapabilitiesKHR& capabilities)
                   { capabilities.currentTransform = static_cast<VkSurfaceTransformFlagBitsKHR>(0); });
    ensure_invalid("a multi-bit current transform stops before enumeration",
                   VulkanSwapchainConfigurationResolutionCode::InvalidCurrentTransform,
                   [](VkSurfaceCapabilitiesKHR& capabilities)
                   {
                       capabilities.supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR | VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR;
                       capabilities.currentTransform    = static_cast<VkSurfaceTransformFlagBitsKHR>(capabilities.supportedTransforms);
                   });

    constexpr std::array framebuffer_limit_cases{
        std::pair{ std::uint32_t{ 799 }, std::uint32_t{ 2160 } },
        std::pair{ std::uint32_t{ 4096 }, std::uint32_t{ 599 } }
    };
    for (const auto& [max_width, max_height] : framebuffer_limit_cases)
    {
        FakeState       state;
        ScopedFakeState scope(state);
        state.mMaxFramebufferWidth  = max_width;
        state.mMaxFramebufferHeight = max_height;
        auto        parents = makeParents(state);
        const auto  result = resolveVulkanSwapchainConfigurationGeneration(parents.mPhysical, parents.mLogical, { 800, 600 });
        const auto& error  = requireError(result);
        ensure("a selected image extent beyond a physical framebuffer dimension is typed",
               error.mCode == VulkanSwapchainConfigurationResolutionCode::SelectedImageExtentExceedsFramebufferLimits &&
                   !error.mCommand && error.mResult == VK_SUCCESS);
        ensure("framebuffer limits reject the selected extent before surface-format or present-mode enumeration",
               state.mFormatCountCalls == 0 && state.mFormatListCalls == 0 && state.mFormatPropertiesCalls == 0 &&
                   state.mPresentCountCalls == 0 && state.mPresentListCalls == 0);
    }
}

template<>
template<>
void render_vulkan_swapchain_configuration_object::test<10>()
{
    {
        FakeState       state;
        ScopedFakeState scope(state);
        state.mFormats.clear();
        auto parents = makeParents(state);
        ensureCode(resolveVulkanSwapchainConfigurationGeneration(parents.mPhysical, parents.mLogical, { 800, 600 }),
                   VulkanSwapchainConfigurationResolutionCode::InvalidSurfaceFormatEnumerationOutput);
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        state.mFormats.clear();
        state.mFormatBehavior.mAdvertisedCount = 1;
        auto parents                           = makeParents(state);
        ensureCode(resolveVulkanSwapchainConfigurationGeneration(parents.mPhysical, parents.mLogical, { 800, 600 }),
                   VulkanSwapchainConfigurationResolutionCode::InvalidSurfaceFormatEnumerationOutput);
        ensure("a zero format list output follows one count and list call", state.mFormatCountCalls == 1 && state.mFormatListCalls == 1);
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        state.mFormatBehavior.mListResult = VK_ERROR_SURFACE_LOST_KHR;
        auto        parents               = makeParents(state);
        const auto  result = resolveVulkanSwapchainConfigurationGeneration(parents.mPhysical, parents.mLogical, { 800, 600 });
        const auto& error  = requireError(result);
        ensure("format list failure preserves the exact result and attempt",
               error.mCode == VulkanSwapchainConfigurationResolutionCode::SurfaceFormatEnumerationFailure &&
                   error.mResult == VK_ERROR_SURFACE_LOST_KHR && error.mEnumerationAttempt == 1);
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        state.mFormatBehavior.mCountIncompleteCalls = 1;
        auto parents                                = makeParents(state);
        auto generation = takeGeneration(resolveVulkanSwapchainConfigurationGeneration(parents.mPhysical, parents.mLogical, { 800, 600 }));
        ensure("one incomplete format count retries from the count query",
               state.mFormatCountCalls == 2 && state.mFormatListCalls == 1 &&
                   generation.surfaceFormat().format == VK_FORMAT_B8G8R8A8_UNORM);
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        state.mFormatBehavior.mListIncompleteCalls = 5;
        auto        parents                        = makeParents(state);
        const auto  result = resolveVulkanSwapchainConfigurationGeneration(parents.mPhysical, parents.mLogical, { 800, 600 });
        const auto& error  = requireError(result);
        ensure("format list retry exhaustion is typed and bounded",
               error.mCode == VulkanSwapchainConfigurationResolutionCode::SurfaceFormatEnumerationRetryLimitExceeded &&
                   error.mResult == VK_INCOMPLETE && error.mEnumerationAttempt == 4 && state.mFormatCountCalls == 4 &&
                   state.mFormatListCalls == 4);
    }
}

template<>
template<>
void render_vulkan_swapchain_configuration_object::test<11>()
{
    {
        FakeState       state;
        ScopedFakeState scope(state);
        state.mPresentModes.clear();
        auto parents = makeParents(state);
        ensureCode(resolveVulkanSwapchainConfigurationGeneration(parents.mPhysical, parents.mLogical, { 800, 600 }),
                   VulkanSwapchainConfigurationResolutionCode::InvalidPresentModeEnumerationOutput);
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        state.mPresentModes.clear();
        state.mPresentBehavior.mAdvertisedCount = 1;
        auto parents                            = makeParents(state);
        ensureCode(resolveVulkanSwapchainConfigurationGeneration(parents.mPhysical, parents.mLogical, { 800, 600 }),
                   VulkanSwapchainConfigurationResolutionCode::InvalidPresentModeEnumerationOutput);
        ensure("a zero present-mode list output follows one count and list call",
               state.mPresentCountCalls == 1 && state.mPresentListCalls == 1);
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        state.mPresentBehavior.mCountResult = VK_ERROR_SURFACE_LOST_KHR;
        auto        parents                 = makeParents(state);
        const auto  result = resolveVulkanSwapchainConfigurationGeneration(parents.mPhysical, parents.mLogical, { 800, 600 });
        const auto& error  = requireError(result);
        ensure("present-mode count failure preserves the exact result and attempt",
               error.mCode == VulkanSwapchainConfigurationResolutionCode::PresentModeEnumerationFailure &&
                   error.mResult == VK_ERROR_SURFACE_LOST_KHR && error.mEnumerationAttempt == 1);
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        state.mPresentBehavior.mListCountOverflow = true;
        auto parents                              = makeParents(state);
        ensureCode(resolveVulkanSwapchainConfigurationGeneration(parents.mPhysical, parents.mLogical, { 800, 600 }),
                   VulkanSwapchainConfigurationResolutionCode::InvalidPresentModeEnumerationOutput);
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        state.mPresentBehavior.mCountIncompleteCalls = 1;
        auto parents                                 = makeParents(state);
        auto generation = takeGeneration(resolveVulkanSwapchainConfigurationGeneration(parents.mPhysical, parents.mLogical, { 800, 600 }));
        ensure("one incomplete present-mode count retries from the count query",
               state.mPresentCountCalls == 2 && state.mPresentListCalls == 1 && generation.presentMode() == VK_PRESENT_MODE_FIFO_KHR);
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        state.mPresentBehavior.mListIncompleteCalls = 1;
        auto parents                                = makeParents(state);
        auto generation = takeGeneration(resolveVulkanSwapchainConfigurationGeneration(parents.mPhysical, parents.mLogical, { 800, 600 }));
        ensure("one incomplete present-mode list retries a full count and list cycle",
               state.mPresentCountCalls == 2 && state.mPresentListCalls == 2 && generation.presentMode() == VK_PRESENT_MODE_FIFO_KHR);
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        state.mPresentBehavior.mCountIncompleteCalls = 5;
        auto        parents                          = makeParents(state);
        const auto  result = resolveVulkanSwapchainConfigurationGeneration(parents.mPhysical, parents.mLogical, { 800, 600 });
        const auto& error  = requireError(result);
        ensure("present-mode count retry exhaustion is typed and bounded",
               error.mCode == VulkanSwapchainConfigurationResolutionCode::PresentModeEnumerationRetryLimitExceeded &&
                   error.mResult == VK_INCOMPLETE && error.mEnumerationAttempt == 4 && state.mPresentCountCalls == 4 &&
                   state.mPresentListCalls == 0);
    }
}

} // namespace tut
