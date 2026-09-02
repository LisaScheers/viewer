/**
 * @file llrendervulkanswapchain_test.cpp
 * @brief Tests for loader-neutral Vulkan swapchain ownership.
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

#include "llrendervulkanswapchain.h"
#include "lltut.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
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
    GetDeviceProcAddr,
    CreateSwapchain,
    DestroySwapchain
};

enum class SwapchainEvent : std::uint8_t
{
    Create,
    Destroy
};

struct FakeState
{
    VkInstance       mInstance        = fakeHandle<VkInstance>(0x1000);
    VkSurfaceKHR     mSurface         = fakeHandle<VkSurfaceKHR>(0x2000);
    VkPhysicalDevice mPhysicalDevice  = fakeHandle<VkPhysicalDevice>(0x3000);
    VkDevice         mDevice          = fakeHandle<VkDevice>(0x4000);
    VkQueue          mQueue           = fakeHandle<VkQueue>(0x5000);
    VkSwapchainKHR   mSwapchainOutput = fakeHandle<VkSwapchainKHR>(0x6000);
    std::uint32_t    mQueueFamily     = 2;

    VkSurfaceCapabilitiesKHR          mCapabilities{ 2,
                                            0,
                                                     { std::numeric_limits<std::uint32_t>::max(), std::numeric_limits<std::uint32_t>::max() },
                                                     { 64, 64 },
                                                     { 4096, 2160 },
                                            1,
                                            VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR,
                                            VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR,
                                            VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                                VK_IMAGE_USAGE_TRANSFER_SRC_BIT };
    std::array<VkSurfaceFormatKHR, 2> mFormats{ VkSurfaceFormatKHR{ VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR },
                                                VkSurfaceFormatKHR{ VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR } };
    std::array<VkPresentModeKHR, 2>   mPresentModes{ VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_FIFO_KHR };

    MissingCommand              mMissingCommand = MissingCommand::None;
    VkResult                    mCreateResult   = VK_SUCCESS;
    std::vector<std::string>    mInstanceLookups;
    std::vector<std::string>    mDeviceLookups;
    std::vector<SwapchainEvent> mSwapchainEvents;

    std::size_t              mCreateCalls  = 0;
    VkDevice                 mCreateDevice = VK_NULL_HANDLE;
    VkSwapchainCreateInfoKHR mCapturedCreateInfo{};
    bool                     mCreateAllocatorNull     = false;
    bool                     mAllResolvedBeforeCreate = false;

    std::size_t    mDestroySwapchainCalls = 0;
    VkDevice       mDestroyDevice         = VK_NULL_HANDLE;
    VkSwapchainKHR mDestroyedSwapchain    = VK_NULL_HANDLE;
    bool           mDestroyAllocatorNull  = false;
    std::size_t    mDestroyDeviceCalls    = 0;
};

FakeState* gFakeState = nullptr;

class ScopedFakeState
{
public:
    explicit ScopedFakeState(FakeState& state) noexcept : mPrevious(gFakeState) { gFakeState = &state; }
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
    properties->limits.maxFramebufferWidth      = 4096;
    properties->limits.maxFramebufferHeight     = 2160;
    properties->limits.maxViewportDimensions[0] = 4096;
    properties->limits.maxViewportDimensions[1] = 4096;
    properties->limits.viewportBoundsRange[0]   = -8192.0f;
    properties->limits.viewportBoundsRange[1]   = 8191.0f;
    std::strncpy(properties->deviceName, "swapchain-fake", VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 1);
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
        ++gFakeState->mDestroyDeviceCalls;
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
    if (!gFakeState || physical_device != gFakeState->mPhysicalDevice || surface != gFakeState->mSurface || !capabilities)
    {
        return VK_ERROR_SURFACE_LOST_KHR;
    }
    *capabilities = gFakeState->mCapabilities;
    return VK_SUCCESS;
}

template<typename Value, std::size_t Size>
VkResult enumerate(const std::array<Value, Size>& values, std::uint32_t* count, Value* output) noexcept
{
    if (!count)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!output)
    {
        *count = static_cast<std::uint32_t>(values.size());
        return VK_SUCCESS;
    }
    const std::uint32_t written = std::min<std::uint32_t>(*count, static_cast<std::uint32_t>(values.size()));
    std::copy_n(values.begin(), written, output);
    *count = written;
    return written == values.size() ? VK_SUCCESS : VK_INCOMPLETE;
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
    return enumerate(gFakeState->mFormats, count, formats);
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
    return enumerate(gFakeState->mPresentModes, count, modes);
}

VKAPI_ATTR void VKAPI_CALL fakeGetFormatProperties(VkPhysicalDevice physical_device,
                                                   VkFormat,
                                                   VkFormatProperties* properties) noexcept
{
    if (gFakeState && physical_device == gFakeState->mPhysicalDevice && properties)
    {
        *properties                        = {};
        properties->optimalTilingFeatures =
            VK_FORMAT_FEATURE_TRANSFER_DST_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateSwapchain(VkDevice                        device,
                                                   const VkSwapchainCreateInfoKHR* create_info,
                                                   const VkAllocationCallbacks*    allocator,
                                                   VkSwapchainKHR*                 swapchain) noexcept
{
    if (!gFakeState || !swapchain)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    ++gFakeState->mCreateCalls;
    gFakeState->mSwapchainEvents.push_back(SwapchainEvent::Create);
    gFakeState->mCreateDevice            = device;
    gFakeState->mCreateAllocatorNull     = allocator == nullptr;
    gFakeState->mAllResolvedBeforeCreate = gFakeState->mInstanceLookups.size() == 1 && gFakeState->mDeviceLookups.size() == 2;
    if (create_info)
    {
        gFakeState->mCapturedCreateInfo = *create_info;
    }
    *swapchain = gFakeState->mSwapchainOutput;
    return gFakeState->mCreateResult;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroySwapchain(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* allocator) noexcept
{
    if (!gFakeState)
    {
        return;
    }
    ++gFakeState->mDestroySwapchainCalls;
    gFakeState->mSwapchainEvents.push_back(SwapchainEvent::Destroy);
    gFakeState->mDestroyDevice        = device;
    gFakeState->mDestroyedSwapchain   = swapchain;
    gFakeState->mDestroyAllocatorNull = allocator == nullptr;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL fakeGetDeviceProcAddr(VkDevice device, const char* name) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || !name)
    {
        return nullptr;
    }
    gFakeState->mDeviceLookups.emplace_back(name);
    if (std::strcmp(name, "vkCreateSwapchainKHR") == 0)
    {
        return gFakeState->mMissingCommand == MissingCommand::CreateSwapchain ? nullptr : eraseFunctionType(fakeCreateSwapchain);
    }
    if (std::strcmp(name, "vkDestroySwapchainKHR") == 0)
    {
        return gFakeState->mMissingCommand == MissingCommand::DestroySwapchain ? nullptr : eraseFunctionType(fakeDestroySwapchain);
    }
    return nullptr;
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
    if (std::strcmp(name, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR") == 0)
        return eraseFunctionType(fakeGetSurfaceCapabilities);
    if (std::strcmp(name, "vkGetPhysicalDeviceSurfaceFormatsKHR") == 0)
        return eraseFunctionType(fakeGetSurfaceFormats);
    if (std::strcmp(name, "vkGetPhysicalDeviceSurfacePresentModesKHR") == 0)
        return eraseFunctionType(fakeGetSurfacePresentModes);
    if (std::strcmp(name, "vkGetPhysicalDeviceFormatProperties") == 0)
        return eraseFunctionType(fakeGetFormatProperties);

    gFakeState->mInstanceLookups.emplace_back(name);
    if (std::strcmp(name, "vkGetDeviceProcAddr") == 0)
    {
        return gFakeState->mMissingCommand == MissingCommand::GetDeviceProcAddr ? nullptr : eraseFunctionType(fakeGetDeviceProcAddr);
    }
    return nullptr;
}

struct Parents
{
    VulkanPhysicalDeviceGeneration         mPhysical;
    VulkanLogicalDeviceGeneration          mLogical;
    VulkanSwapchainConfigurationGeneration mConfiguration;
};

Parents makeParents(FakeState& state, VkExtent2D drawable_extent = { 1280, 720 })
{
    VulkanPhysicalDeviceResolutionResult physical_result =
        resolveVulkanPhysicalDeviceGeneration(VulkanPhysicalDeviceRequest{ fakeGetInstanceProcAddr, state.mInstance, state.mSurface });
    tut::ensure("the physical-device fixture resolves", std::holds_alternative<VulkanPhysicalDeviceGeneration>(physical_result));
    auto physical = std::get<VulkanPhysicalDeviceGeneration>(std::move(physical_result));

    VulkanLogicalDeviceResolutionResult logical_result = resolveVulkanLogicalDeviceGeneration(physical);
    tut::ensure("the logical-device fixture resolves", std::holds_alternative<VulkanLogicalDeviceGeneration>(logical_result));
    auto logical = std::get<VulkanLogicalDeviceGeneration>(std::move(logical_result));

    VulkanSwapchainConfigurationResolutionResult configuration_result =
        resolveVulkanSwapchainConfigurationGeneration(physical, logical, drawable_extent);
    tut::ensure("the swapchain-configuration fixture resolves",
                std::holds_alternative<VulkanSwapchainConfigurationGeneration>(configuration_result));
    auto configuration = std::get<VulkanSwapchainConfigurationGeneration>(std::move(configuration_result));

    state.mInstanceLookups.clear();
    state.mDeviceLookups.clear();
    return { std::move(physical), std::move(logical), std::move(configuration) };
}

const VulkanSwapchainResolutionError& requireError(const VulkanSwapchainResolutionResult& result)
{
    const auto* error = std::get_if<VulkanSwapchainResolutionError>(&result);
    tut::ensure("swapchain resolution returns an error", error != nullptr);
    return *error;
}

void ensureCode(const VulkanSwapchainResolutionResult& result, VulkanSwapchainResolutionCode code)
{
    tut::ensure("the exact swapchain error is reported", requireError(result).mCode == code);
}

VulkanSwapchainGeneration takeGeneration(VulkanSwapchainResolutionResult&& result)
{
    tut::ensure("swapchain resolution returns a generation", std::holds_alternative<VulkanSwapchainGeneration>(result));
    return std::get<VulkanSwapchainGeneration>(std::move(result));
}

void ensureExactCreateInfo(const VkSwapchainCreateInfoKHR& info, const Parents& parents)
{
    const auto& configuration = parents.mConfiguration;
    const auto  format        = configuration.surfaceFormat();
    const auto  extent        = configuration.imageExtent();
    tut::ensure("the swapchain create record has exact structure invariants",
                info.sType == VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR && info.pNext == nullptr && info.flags == 0);
    tut::ensure("the swapchain create record copies exact surface and image policy",
                info.surface == configuration.surface() && info.minImageCount == configuration.imageCount() &&
                    info.imageFormat == format.format && info.imageColorSpace == format.colorSpace &&
                    info.imageExtent.width == extent.width && info.imageExtent.height == extent.height &&
                    info.imageArrayLayers == configuration.imageArrayLayers() && info.imageUsage == configuration.imageUsage() &&
                    info.imageUsage == (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT));
    tut::ensure("exclusive sharing supplies no queue-family array",
                info.imageSharingMode == VK_SHARING_MODE_EXCLUSIVE && info.queueFamilyIndexCount == 0 &&
                    info.pQueueFamilyIndices == nullptr);
    tut::ensure("the swapchain create record copies exact presentation policy",
                info.preTransform == configuration.preTransform() && info.compositeAlpha == configuration.compositeAlpha() &&
                    info.presentMode == configuration.presentMode() && info.clipped == configuration.clipped() &&
                    info.oldSwapchain == VK_NULL_HANDLE);
}

} // namespace

namespace tut
{

struct render_vulkan_swapchain_test
{
};

using render_vulkan_swapchain_group  = test_group<render_vulkan_swapchain_test>;
using render_vulkan_swapchain_object = render_vulkan_swapchain_group::object;
render_vulkan_swapchain_group render_vulkan_swapchain_tests("render Vulkan swapchain");

template<>
template<>
void render_vulkan_swapchain_object::test<1>()
{
    static_assert(!std::is_default_constructible_v<VulkanSwapchainGeneration>);
    static_assert(!std::is_copy_constructible_v<VulkanSwapchainGeneration>);
    static_assert(!std::is_copy_assignable_v<VulkanSwapchainGeneration>);
    static_assert(std::is_nothrow_move_constructible_v<VulkanSwapchainGeneration>);
    static_assert(!std::is_move_assignable_v<VulkanSwapchainGeneration>);
    static_assert(std::is_nothrow_destructible_v<VulkanSwapchainGeneration>);
    static_assert(std::variant_size_v<VulkanSwapchainResolutionResult> == 2);
    static_assert(noexcept(resolveVulkanSwapchainGeneration(std::declval<const VulkanLogicalDeviceGeneration&>(),
                                                            std::declval<const VulkanSwapchainConfigurationGeneration&>())));

    const VulkanSwapchainResolutionError value{ VulkanSwapchainResolutionCode::SwapchainCreationFailure,
                                                VulkanSwapchainCommand::CreateSwapchain, VK_ERROR_SURFACE_LOST_KHR };
    ensure("identical typed errors compare equal", value == value);

    FakeState       state;
    ScopedFakeState scope(state);
    auto            parents      = makeParents(state);
    auto            live_logical = std::move(parents.mLogical);
    ensureCode(resolveVulkanSwapchainGeneration(parents.mLogical, parents.mConfiguration),
               VulkanSwapchainResolutionCode::InvalidLogicalDeviceGeneration);
    ensure("invalid logical provenance resolves no swapchain command",
           state.mInstanceLookups.empty() && state.mDeviceLookups.empty() && state.mCreateCalls == 0);

    auto live_configuration = std::move(parents.mConfiguration);
    ensureCode(resolveVulkanSwapchainGeneration(live_logical, parents.mConfiguration),
               VulkanSwapchainResolutionCode::InvalidSwapchainConfigurationGeneration);
    ensure("invalid configuration provenance resolves no swapchain command",
           state.mInstanceLookups.empty() && state.mDeviceLookups.empty() && state.mCreateCalls == 0);
    ensure("moved parents remain live", live_logical.device() == state.mDevice && live_configuration.device() == state.mDevice);

    FakeState distinct_state;
    distinct_state.mInstance        = fakeHandle<VkInstance>(0x1100);
    distinct_state.mSurface         = fakeHandle<VkSurfaceKHR>(0x2200);
    distinct_state.mPhysicalDevice  = fakeHandle<VkPhysicalDevice>(0x3300);
    distinct_state.mDevice          = fakeHandle<VkDevice>(0x4400);
    distinct_state.mQueue           = fakeHandle<VkQueue>(0x5500);
    distinct_state.mSwapchainOutput = fakeHandle<VkSwapchainKHR>(0x6600);
    {
        ScopedFakeState distinct_scope(distinct_state);
        auto            distinct_parents = makeParents(distinct_state);

        ensureCode(resolveVulkanSwapchainGeneration(distinct_parents.mLogical, live_configuration),
                   VulkanSwapchainResolutionCode::InvalidSwapchainConfigurationGeneration);
        ensure("distinct live parent provenance fails before swapchain dispatch or mutation",
               distinct_state.mInstanceLookups.empty() && distinct_state.mDeviceLookups.empty() && distinct_state.mCreateCalls == 0 &&
                   distinct_state.mDestroySwapchainCalls == 0);
    }
}

template<>
template<>
void render_vulkan_swapchain_object::test<2>()
{
    constexpr std::array cases{ std::pair{ MissingCommand::GetDeviceProcAddr, VulkanSwapchainCommand::GetDeviceProcAddr },
                                std::pair{ MissingCommand::CreateSwapchain, VulkanSwapchainCommand::CreateSwapchain },
                                std::pair{ MissingCommand::DestroySwapchain, VulkanSwapchainCommand::DestroySwapchain } };

    for (std::size_t index = 0; index < cases.size(); ++index)
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        state.mMissingCommand   = cases[index].first;

        const VulkanSwapchainResolutionResult result = resolveVulkanSwapchainGeneration(parents.mLogical, parents.mConfiguration);
        const VulkanSwapchainResolutionError& error  = requireError(result);
        ensure("a missing dispatch command uses the required-command code",
               error.mCode == VulkanSwapchainResolutionCode::MissingRequiredCommand);
        ensure("a missing dispatch command preserves exact identity", error.mCommand == cases[index].second);
        ensure("instance dispatch resolution has exact cutoff", state.mInstanceLookups.size() == 1);
        ensure("device dispatch resolution has exact cutoff", state.mDeviceLookups.size() == index);
        ensure("incomplete dispatch creates and destroys nothing", state.mCreateCalls == 0 && state.mDestroySwapchainCalls == 0);
    }
}

template<>
template<>
void render_vulkan_swapchain_object::test<3>()
{
    FakeState       state;
    ScopedFakeState scope(state);
    auto            parents    = makeParents(state, { 2560, 200 });
    auto            generation = takeGeneration(resolveVulkanSwapchainGeneration(parents.mLogical, parents.mConfiguration));

    ensure("device dispatch resolves through exact parent handles and in exact order",
           state.mInstanceLookups == std::vector<std::string>{ "vkGetDeviceProcAddr" } &&
               state.mDeviceLookups == std::vector<std::string>{ "vkCreateSwapchainKHR", "vkDestroySwapchainKHR" } &&
               state.mAllResolvedBeforeCreate);
    ensure("creation uses the exact logical device and null allocation callbacks",
           state.mCreateCalls == 1 && state.mCreateDevice == state.mDevice && state.mCreateAllocatorNull);
    ensureExactCreateInfo(state.mCapturedCreateInfo, parents);
    ensureExactCreateInfo(generation.createInfo(), parents);
    ensure("the owner retains and authenticates exact parent provenance",
           generation.getInstanceProcAddr() == fakeGetInstanceProcAddr && generation.instance() == state.mInstance &&
               generation.surface() == state.mSurface && generation.physicalDevice() == state.mPhysicalDevice &&
               generation.device() == state.mDevice && generation.queueFamilyIndex() == state.mQueueFamily &&
               generation.drawableExtent().width == 2560 && generation.drawableExtent().height == 200 &&
               generation.swapchain() == state.mSwapchainOutput && generation.createdFor(parents.mLogical, parents.mConfiguration));

    generation.reset();
    ensure("reset destroys the exact swapchain on the exact device with null callbacks",
           state.mDestroySwapchainCalls == 1 && state.mDestroyDevice == state.mDevice &&
               state.mDestroyedSwapchain == state.mSwapchainOutput && state.mDestroyAllocatorNull &&
               state.mSwapchainEvents == std::vector<SwapchainEvent>{ SwapchainEvent::Create, SwapchainEvent::Destroy } &&
               generation.swapchain() == VK_NULL_HANDLE && !generation.createdFor(parents.mLogical, parents.mConfiguration));
    generation.reset();
    ensure("reset is idempotent", state.mDestroySwapchainCalls == 1);
}

template<>
template<>
void render_vulkan_swapchain_object::test<4>()
{
    FakeState       state;
    ScopedFakeState scope(state);
    auto            parents     = makeParents(state);
    state.mCreateResult         = VK_ERROR_NATIVE_WINDOW_IN_USE_KHR;
    const VkSwapchainKHR broken = state.mSwapchainOutput;

    const VulkanSwapchainResolutionResult result = resolveVulkanSwapchainGeneration(parents.mLogical, parents.mConfiguration);
    const VulkanSwapchainResolutionError& error  = requireError(result);
    ensure("creation failure preserves the exact command and VkResult",
           error.mCode == VulkanSwapchainResolutionCode::SwapchainCreationFailure &&
               error.mCommand == VulkanSwapchainCommand::CreateSwapchain && error.mResult == VK_ERROR_NATIVE_WINDOW_IN_USE_KHR);
    ensure("an undefined non-null failed output is never inspected or destroyed",
           broken != VK_NULL_HANDLE && state.mCreateCalls == 1 && state.mDestroySwapchainCalls == 0 &&
               state.mSwapchainEvents == std::vector<SwapchainEvent>{ SwapchainEvent::Create });
}

template<>
template<>
void render_vulkan_swapchain_object::test<5>()
{
    FakeState       state;
    ScopedFakeState scope(state);
    auto            parents = makeParents(state);
    state.mSwapchainOutput  = VK_NULL_HANDLE;

    const VulkanSwapchainResolutionResult result = resolveVulkanSwapchainGeneration(parents.mLogical, parents.mConfiguration);
    const VulkanSwapchainResolutionError& error  = requireError(result);
    ensure("success with a null swapchain is typed",
           error.mCode == VulkanSwapchainResolutionCode::NullSwapchainOnSuccess &&
               error.mCommand == VulkanSwapchainCommand::CreateSwapchain && error.mResult == VK_SUCCESS);
    ensure("a null success output is not destroyed", state.mCreateCalls == 1 && state.mDestroySwapchainCalls == 0);

    state.mSwapchainOutput = fakeHandle<VkSwapchainKHR>(0x7000);
    auto retried           = takeGeneration(resolveVulkanSwapchainGeneration(parents.mLogical, parents.mConfiguration));
    ensure("unchanged parents are reusable after null output",
           retried.swapchain() == state.mSwapchainOutput && retried.createdFor(parents.mLogical, parents.mConfiguration));
    retried.reset();
    ensure("retry owns exactly one valid swapchain", state.mDestroySwapchainCalls == 1);
}

template<>
template<>
void render_vulkan_swapchain_object::test<6>()
{
    FakeState       state;
    ScopedFakeState scope(state);
    auto            parents    = makeParents(state);
    auto            generation = takeGeneration(resolveVulkanSwapchainGeneration(parents.mLogical, parents.mConfiguration));
    auto            moved      = std::move(generation);

    ensure("move transfers exact ownership and disarms the source",
           moved.swapchain() == state.mSwapchainOutput && moved.createdFor(parents.mLogical, parents.mConfiguration) &&
               generation.swapchain() == VK_NULL_HANDLE && !generation.createdFor(parents.mLogical, parents.mConfiguration));
    moved.reset();
    ensure("only the moved owner destroys the swapchain", state.mDestroySwapchainCalls == 1);
}

} // namespace tut
