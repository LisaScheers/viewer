/**
 * @file llrendervulkanswapchainimages_test.cpp
 * @brief Tests for loader-neutral Vulkan swapchain image-view ownership.
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

#include "llrendervulkanswapchainimages.h"
#include "lltut.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <string>
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
    GetSwapchainImages,
    CreateImageView,
    DestroyImageView
};

struct EnumerationStep
{
    VkResult             mCountResult     = VK_SUCCESS;
    std::uint32_t        mAdvertisedCount = 3;
    VkResult             mListResult      = VK_SUCCESS;
    std::uint32_t        mReturnedCount   = 3;
    std::vector<VkImage> mImages;
};

struct FakeState
{
    VkInstance       mInstance       = fakeHandle<VkInstance>(0x1000);
    VkSurfaceKHR     mSurface        = fakeHandle<VkSurfaceKHR>(0x2000);
    VkPhysicalDevice mPhysicalDevice = fakeHandle<VkPhysicalDevice>(0x3000);
    VkDevice         mDevice         = fakeHandle<VkDevice>(0x4000);
    VkQueue          mQueue          = fakeHandle<VkQueue>(0x5000);
    VkSwapchainKHR   mSwapchain      = fakeHandle<VkSwapchainKHR>(0x6000);
    std::uint32_t    mQueueFamily    = 2;

    VkSurfaceCapabilitiesKHR          mCapabilities{ 2,
                                            0,
                                                     { std::numeric_limits<std::uint32_t>::max(), std::numeric_limits<std::uint32_t>::max() },
                                                     { 64, 64 },
                                                     { 4096, 2160 },
                                            1,
                                            VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
                                            VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
                                            VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
                                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT };
    std::array<VkSurfaceFormatKHR, 1> mFormats{ VkSurfaceFormatKHR{ VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR } };
    std::array<VkPresentModeKHR, 1>   mPresentModes{ VK_PRESENT_MODE_FIFO_KHR };

    MissingCommand               mMissingCommand = MissingCommand::None;
    std::vector<std::string>     mInstanceLookups;
    std::vector<std::string>     mDeviceLookups;
    std::vector<EnumerationStep> mEnumerationSteps;
    std::size_t                  mEnumerationStep = 0;
    std::size_t                  mCountCalls      = 0;
    std::size_t                  mListCalls       = 0;
    std::vector<VkDevice>        mEnumerationDevices;
    std::vector<VkSwapchainKHR>  mEnumerationSwapchains;

    std::vector<VkResult>              mCreateResults;
    std::vector<VkImageView>           mCreateOutputs;
    std::vector<VkImageViewCreateInfo> mCreateInfos;
    std::vector<VkDevice>              mCreateDevices;
    std::vector<bool>                  mCreateAllocatorNull;
    bool                               mAllResolvedBeforeMutation = false;

    std::vector<VkImageView> mDestroyedViews;
    std::vector<VkDevice>    mDestroyDevices;
    std::vector<bool>        mDestroyAllocatorNull;
    std::size_t              mDestroySwapchainCalls = 0;
    std::size_t              mDestroyDeviceCalls    = 0;

    std::vector<VkImage> defaultImages() const
    {
        return { fakeHandle<VkImage>(0x7100), fakeHandle<VkImage>(0x7200), fakeHandle<VkImage>(0x7300) };
    }

    std::vector<VkImageView> defaultViews() const
    {
        return { fakeHandle<VkImageView>(0x8100), fakeHandle<VkImageView>(0x8200), fakeHandle<VkImageView>(0x8300) };
    }
};

FakeState*  gFakeState          = nullptr;
std::size_t gAllocationCalls    = 0;
std::size_t gFailAllocationCall = 0;

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
    std::strncpy(properties->deviceName, "swapchain-images-fake", VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 1);
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
        *count = 1;
        return VK_SUCCESS;
    }
    if (*count == 0)
    {
        return VK_INCOMPLETE;
    }
    properties[0] = {};
    std::strncpy(properties[0].extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_MAX_EXTENSION_NAME_SIZE - 1);
    *count = 1;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeGetPhysicalDeviceFeatures(VkPhysicalDevice physical_device, VkPhysicalDeviceFeatures* features) noexcept
{
    if (gFakeState && physical_device == gFakeState->mPhysicalDevice && features)
    {
        *features                  = {};
        features->independentBlend = VK_TRUE;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateDevice(VkPhysicalDevice physical_device,
                                                const VkDeviceCreateInfo*,
                                                const VkAllocationCallbacks*,
                                                VkDevice* device) noexcept
{
    if (!gFakeState || physical_device != gFakeState->mPhysicalDevice || !device)
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
    const std::uint32_t written = std::min(*count, static_cast<std::uint32_t>(values.size()));
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

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateSwapchain(VkDevice device,
                                                   const VkSwapchainCreateInfoKHR*,
                                                   const VkAllocationCallbacks*,
                                                   VkSwapchainKHR* swapchain) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || !swapchain)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    *swapchain = gFakeState->mSwapchain;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroySwapchain(VkDevice, VkSwapchainKHR, const VkAllocationCallbacks*) noexcept
{
    if (gFakeState)
    {
        ++gFakeState->mDestroySwapchainCalls;
    }
}

const EnumerationStep& enumerationStep() noexcept
{
    static const EnumerationStep fallback;
    if (!gFakeState || gFakeState->mEnumerationSteps.empty())
    {
        return fallback;
    }
    return gFakeState->mEnumerationSteps[std::min(gFakeState->mEnumerationStep, gFakeState->mEnumerationSteps.size() - 1)];
}

VKAPI_ATTR VkResult VKAPI_CALL fakeGetSwapchainImages(VkDevice       device,
                                                      VkSwapchainKHR swapchain,
                                                      std::uint32_t* count,
                                                      VkImage*       images) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || swapchain != gFakeState->mSwapchain || !count)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    gFakeState->mEnumerationDevices.push_back(device);
    gFakeState->mEnumerationSwapchains.push_back(swapchain);
    if (!images)
    {
        if (!gFakeState->mEnumerationSteps.empty())
        {
            gFakeState->mEnumerationStep = std::min(gFakeState->mCountCalls, gFakeState->mEnumerationSteps.size() - 1);
        }
        ++gFakeState->mCountCalls;
        const auto& step = enumerationStep();
        *count           = step.mAdvertisedCount;
        return step.mCountResult;
    }

    ++gFakeState->mListCalls;
    const auto&         step     = enumerationStep();
    const auto          values   = step.mImages.empty() ? gFakeState->defaultImages() : step.mImages;
    const std::uint32_t capacity = *count;
    const std::uint32_t copied   = std::min<std::uint32_t>(capacity, static_cast<std::uint32_t>(values.size()));
    std::copy_n(values.begin(), copied, images);
    *count = step.mReturnedCount;
    return step.mListResult;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateImageView(VkDevice                     device,
                                                   const VkImageViewCreateInfo* create_info,
                                                   const VkAllocationCallbacks* allocator,
                                                   VkImageView*                 image_view) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || !create_info || !image_view)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    const std::size_t index = gFakeState->mCreateInfos.size();
    gFakeState->mAllResolvedBeforeMutation =
        gFakeState->mInstanceLookups == std::vector<std::string>{ "vkGetDeviceProcAddr" } &&
        gFakeState->mDeviceLookups == std::vector<std::string>{ "vkGetSwapchainImagesKHR", "vkCreateImageView", "vkDestroyImageView" };
    gFakeState->mCreateInfos.push_back(*create_info);
    gFakeState->mCreateDevices.push_back(device);
    gFakeState->mCreateAllocatorNull.push_back(allocator == nullptr);

    const VkImageView output =
        index < gFakeState->mCreateOutputs.size() ? gFakeState->mCreateOutputs[index] : fakeHandle<VkImageView>(0x8100 + index * 0x100);
    *image_view = output;
    return index < gFakeState->mCreateResults.size() ? gFakeState->mCreateResults[index] : VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyImageView(VkDevice device, VkImageView image_view, const VkAllocationCallbacks* allocator) noexcept
{
    if (!gFakeState)
    {
        return;
    }
    gFakeState->mDestroyDevices.push_back(device);
    gFakeState->mDestroyedViews.push_back(image_view);
    gFakeState->mDestroyAllocatorNull.push_back(allocator == nullptr);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL fakeGetDeviceProcAddr(VkDevice device, const char* name) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || !name)
    {
        return nullptr;
    }
    if (std::strcmp(name, "vkCreateSwapchainKHR") == 0)
    {
        return eraseFunctionType(fakeCreateSwapchain);
    }
    if (std::strcmp(name, "vkDestroySwapchainKHR") == 0)
    {
        return eraseFunctionType(fakeDestroySwapchain);
    }

    gFakeState->mDeviceLookups.emplace_back(name);
    if (std::strcmp(name, "vkGetSwapchainImagesKHR") == 0)
    {
        return gFakeState->mMissingCommand == MissingCommand::GetSwapchainImages ? nullptr : eraseFunctionType(fakeGetSwapchainImages);
    }
    if (std::strcmp(name, "vkCreateImageView") == 0)
    {
        return gFakeState->mMissingCommand == MissingCommand::CreateImageView ? nullptr : eraseFunctionType(fakeCreateImageView);
    }
    if (std::strcmp(name, "vkDestroyImageView") == 0)
    {
        return gFakeState->mMissingCommand == MissingCommand::DestroyImageView ? nullptr : eraseFunctionType(fakeDestroyImageView);
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
    VulkanSwapchainGeneration              mSwapchain;
};

Parents makeParents(FakeState& state)
{
    auto physical_result =
        resolveVulkanPhysicalDeviceGeneration(VulkanPhysicalDeviceRequest{ fakeGetInstanceProcAddr, state.mInstance, state.mSurface });
    tut::ensure("the physical-device fixture resolves", std::holds_alternative<VulkanPhysicalDeviceGeneration>(physical_result));
    auto physical = std::get<VulkanPhysicalDeviceGeneration>(std::move(physical_result));

    auto logical_result = resolveVulkanLogicalDeviceGeneration(physical);
    tut::ensure("the logical-device fixture resolves", std::holds_alternative<VulkanLogicalDeviceGeneration>(logical_result));
    auto logical = std::get<VulkanLogicalDeviceGeneration>(std::move(logical_result));

    auto configuration_result = resolveVulkanSwapchainConfigurationGeneration(physical, logical, { 1280, 720 });
    tut::ensure("the swapchain-configuration fixture resolves",
                std::holds_alternative<VulkanSwapchainConfigurationGeneration>(configuration_result));
    auto configuration = std::get<VulkanSwapchainConfigurationGeneration>(std::move(configuration_result));

    auto swapchain_result = resolveVulkanSwapchainGeneration(logical, configuration);
    tut::ensure("the swapchain fixture resolves", std::holds_alternative<VulkanSwapchainGeneration>(swapchain_result));
    auto swapchain = std::get<VulkanSwapchainGeneration>(std::move(swapchain_result));

    state.mInstanceLookups.clear();
    state.mDeviceLookups.clear();
    state.mEnumerationStep = 0;
    state.mCountCalls      = 0;
    state.mListCalls       = 0;
    return { std::move(physical), std::move(logical), std::move(configuration), std::move(swapchain) };
}

const VulkanSwapchainImagesResolutionError& requireError(const VulkanSwapchainImagesResolutionResult& result)
{
    const auto* error = std::get_if<VulkanSwapchainImagesResolutionError>(&result);
    tut::ensure("swapchain-image resolution returns an error", error != nullptr);
    return *error;
}

void ensureCode(const VulkanSwapchainImagesResolutionResult& result, VulkanSwapchainImagesResolutionCode code)
{
    tut::ensure("the exact swapchain-image error is reported", requireError(result).mCode == code);
}

VulkanSwapchainImagesGeneration takeGeneration(VulkanSwapchainImagesResolutionResult&& result)
{
    tut::ensure("swapchain-image resolution returns a generation", std::holds_alternative<VulkanSwapchainImagesGeneration>(result));
    return std::get<VulkanSwapchainImagesGeneration>(std::move(result));
}

EnumerationStep successfulStep(const FakeState& state, std::uint32_t count = 3)
{
    EnumerationStep step;
    step.mAdvertisedCount = count;
    step.mReturnedCount   = count;
    step.mImages          = state.defaultImages();
    while (step.mImages.size() < count)
    {
        step.mImages.push_back(fakeHandle<VkImage>(0x7100 + step.mImages.size() * 0x100));
    }
    return step;
}

void ensureExactCreateInfo(const VkImageViewCreateInfo& info, VkImage image, VkFormat format)
{
    tut::ensure("the image-view create record has exact structure invariants",
                info.sType == VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO && info.pNext == nullptr && info.flags == 0);
    tut::ensure("the image-view create record selects the exact borrowed image, 2D type, and format",
                info.image == image && info.viewType == VK_IMAGE_VIEW_TYPE_2D && info.format == format);
    tut::ensure("the image-view create record uses identity components",
                info.components.r == VK_COMPONENT_SWIZZLE_IDENTITY && info.components.g == VK_COMPONENT_SWIZZLE_IDENTITY &&
                    info.components.b == VK_COMPONENT_SWIZZLE_IDENTITY && info.components.a == VK_COMPONENT_SWIZZLE_IDENTITY);
    tut::ensure("the image-view create record selects one color mip and one array layer",
                info.subresourceRange.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT && info.subresourceRange.baseMipLevel == 0 &&
                    info.subresourceRange.levelCount == 1 && info.subresourceRange.baseArrayLayer == 0 &&
                    info.subresourceRange.layerCount == 1);
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

struct render_vulkan_swapchain_images_test
{
};

using render_vulkan_swapchain_images_group  = test_group<render_vulkan_swapchain_images_test>;
using render_vulkan_swapchain_images_object = render_vulkan_swapchain_images_group::object;
render_vulkan_swapchain_images_group render_vulkan_swapchain_images_tests("render Vulkan swapchain images");

template<>
template<>
void render_vulkan_swapchain_images_object::test<1>()
{
    static_assert(!std::is_default_constructible_v<VulkanSwapchainImagesGeneration>);
    static_assert(!std::is_copy_constructible_v<VulkanSwapchainImagesGeneration>);
    static_assert(!std::is_copy_assignable_v<VulkanSwapchainImagesGeneration>);
    static_assert(std::is_nothrow_move_constructible_v<VulkanSwapchainImagesGeneration>);
    static_assert(!std::is_move_assignable_v<VulkanSwapchainImagesGeneration>);
    static_assert(std::is_nothrow_destructible_v<VulkanSwapchainImagesGeneration>);
    static_assert(std::variant_size_v<VulkanSwapchainImagesResolutionResult> == 2);
    static_assert(noexcept(resolveVulkanSwapchainImagesGeneration(std::declval<const VulkanLogicalDeviceGeneration&>(),
                                                                  std::declval<const VulkanSwapchainConfigurationGeneration&>(),
                                                                  std::declval<const VulkanSwapchainGeneration&>())));

    const VulkanSwapchainImagesResolutionError value{ VulkanSwapchainImagesResolutionCode::ImageViewCreationFailure,
                                                      VulkanSwapchainImagesCommand::CreateImageView,
                                                      VK_ERROR_OUT_OF_DEVICE_MEMORY,
                                                      3,
                                                      0,
                                                      1 };
    ensure("identical typed errors compare equal", value == value);

    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        auto            logical = std::move(parents.mLogical);
        ensureCode(resolveVulkanSwapchainImagesGeneration(parents.mLogical, parents.mConfiguration, parents.mSwapchain),
                   VulkanSwapchainImagesResolutionCode::InvalidLogicalDeviceGeneration);
        ensure("an invalid logical parent stops before dispatch", state.mInstanceLookups.empty() && state.mDeviceLookups.empty());
        ensure("the moved logical parent stays live", logical.device() == state.mDevice);
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents       = makeParents(state);
        auto            configuration = std::move(parents.mConfiguration);
        ensureCode(resolveVulkanSwapchainImagesGeneration(parents.mLogical, parents.mConfiguration, parents.mSwapchain),
                   VulkanSwapchainImagesResolutionCode::InvalidSwapchainConfigurationGeneration);
        ensure("an invalid configuration parent stops before dispatch", state.mInstanceLookups.empty() && state.mDeviceLookups.empty());
        ensure("the moved configuration parent stays live", configuration.device() == state.mDevice);
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents   = makeParents(state);
        auto            swapchain = std::move(parents.mSwapchain);
        ensureCode(resolveVulkanSwapchainImagesGeneration(parents.mLogical, parents.mConfiguration, parents.mSwapchain),
                   VulkanSwapchainImagesResolutionCode::InvalidSwapchainGeneration);
        ensure("an invalid swapchain parent stops before dispatch", state.mInstanceLookups.empty() && state.mDeviceLookups.empty());
        ensure("the moved swapchain parent stays live", swapchain.swapchain() == state.mSwapchain);
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);

        FakeState distinct_state;
        distinct_state.mInstance       = fakeHandle<VkInstance>(0x1100);
        distinct_state.mSurface        = fakeHandle<VkSurfaceKHR>(0x2200);
        distinct_state.mPhysicalDevice = fakeHandle<VkPhysicalDevice>(0x3300);
        distinct_state.mDevice         = fakeHandle<VkDevice>(0x4400);
        distinct_state.mQueue          = fakeHandle<VkQueue>(0x5500);
        distinct_state.mSwapchain      = fakeHandle<VkSwapchainKHR>(0x6600);
        ScopedFakeState distinct_scope(distinct_state);
        auto            distinct_parents = makeParents(distinct_state);

        ensureCode(resolveVulkanSwapchainImagesGeneration(distinct_parents.mLogical, parents.mConfiguration, parents.mSwapchain),
                   VulkanSwapchainImagesResolutionCode::InvalidSwapchainConfigurationGeneration);
        ensureCode(resolveVulkanSwapchainImagesGeneration(distinct_parents.mLogical, distinct_parents.mConfiguration, parents.mSwapchain),
                   VulkanSwapchainImagesResolutionCode::InvalidSwapchainGeneration);
        ensure("mismatched distinct live parents stop before dispatch or mutation",
               distinct_state.mInstanceLookups.empty() && distinct_state.mDeviceLookups.empty() && distinct_state.mCountCalls == 0 &&
                   distinct_state.mCreateInfos.empty() && distinct_state.mDestroyedViews.empty());
    }
}

template<>
template<>
void render_vulkan_swapchain_images_object::test<2>()
{
    constexpr std::array cases{ std::pair{ MissingCommand::GetDeviceProcAddr, VulkanSwapchainImagesCommand::GetDeviceProcAddr },
                                std::pair{ MissingCommand::GetSwapchainImages, VulkanSwapchainImagesCommand::GetSwapchainImages },
                                std::pair{ MissingCommand::CreateImageView, VulkanSwapchainImagesCommand::CreateImageView },
                                std::pair{ MissingCommand::DestroyImageView, VulkanSwapchainImagesCommand::DestroyImageView } };

    for (std::size_t index = 0; index < cases.size(); ++index)
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        state.mMissingCommand   = cases[index].first;

        const auto  result = resolveVulkanSwapchainImagesGeneration(parents.mLogical, parents.mConfiguration, parents.mSwapchain);
        const auto& error  = requireError(result);
        ensure("missing dispatch reports its exact typed command",
               error.mCode == VulkanSwapchainImagesResolutionCode::MissingRequiredCommand && error.mCommand == cases[index].second);
        ensure("missing dispatch has the exact instance cutoff", state.mInstanceLookups.size() == 1);
        ensure("missing dispatch has the exact device cutoff", state.mDeviceLookups.size() == index);
        ensure("all commands resolve before query or mutation",
               state.mCountCalls == 0 && state.mListCalls == 0 && state.mCreateInfos.empty() && state.mDestroyedViews.empty());
    }
}

template<>
template<>
void render_vulkan_swapchain_images_object::test<3>()
{
    FakeState       state;
    ScopedFakeState scope(state);
    auto            parents = makeParents(state);
    auto generation = takeGeneration(resolveVulkanSwapchainImagesGeneration(parents.mLogical, parents.mConfiguration, parents.mSwapchain));

    const auto images = state.defaultImages();
    const auto views  = state.defaultViews();
    ensure("dispatch resolves through exact parent handles and in exact order",
           state.mInstanceLookups == std::vector<std::string>{ "vkGetDeviceProcAddr" } &&
               state.mDeviceLookups == std::vector<std::string>{ "vkGetSwapchainImagesKHR", "vkCreateImageView", "vkDestroyImageView" } &&
               state.mAllResolvedBeforeMutation);
    ensure("enumeration uses the exact logical device and swapchain",
           state.mCountCalls == 1 && state.mListCalls == 1 && state.mEnumerationDevices.size() == 2 &&
               std::all_of(state.mEnumerationDevices.begin(), state.mEnumerationDevices.end(),
                           [&](VkDevice device) { return device == state.mDevice; }) &&
               std::all_of(state.mEnumerationSwapchains.begin(), state.mEnumerationSwapchains.end(),
                           [&](VkSwapchainKHR swapchain) { return swapchain == state.mSwapchain; }));
    ensure("the generation exposes every borrowed image and owned view",
           generation.imageCount() == 3 && generation.imageFormat() == VK_FORMAT_B8G8R8A8_UNORM && generation.image(0) == images[0] &&
               generation.image(2) == images[2] && generation.imageView(0) == views[0] && generation.imageView(2) == views[2] &&
               generation.image(3) == VK_NULL_HANDLE && generation.imageView(3) == VK_NULL_HANDLE);
    ensure("the generation authenticates its exact parents",
           generation.createdFor(parents.mLogical, parents.mConfiguration, parents.mSwapchain));
    ensure("one view is created per image on the exact device with null callbacks",
           state.mCreateInfos.size() == 3 && state.mCreateDevices == std::vector<VkDevice>(3, state.mDevice) &&
               state.mCreateAllocatorNull == std::vector<bool>(3, true));
    for (std::uint32_t index = 0; index < 3; ++index)
    {
        ensureExactCreateInfo(state.mCreateInfos[index], images[index], VK_FORMAT_B8G8R8A8_UNORM);
    }

    generation.reset();
    ensure("reset destroys all views in reverse order with exact device and null callbacks",
           state.mDestroyedViews == std::vector<VkImageView>{ views[2], views[1], views[0] } &&
               state.mDestroyDevices == std::vector<VkDevice>(3, state.mDevice) &&
               state.mDestroyAllocatorNull == std::vector<bool>(3, true));
    ensure("reset discards borrowed image records without destroying the parent swapchain",
           generation.imageCount() == 0 && generation.image(0) == VK_NULL_HANDLE && generation.imageView(0) == VK_NULL_HANDLE &&
               !generation.createdFor(parents.mLogical, parents.mConfiguration, parents.mSwapchain) && state.mDestroySwapchainCalls == 0);
    generation.reset();
    ensure("reset is idempotent", state.mDestroyedViews.size() == 3);
}

template<>
template<>
void render_vulkan_swapchain_images_object::test<4>()
{
    for (bool fail_count_query : { true, false })
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        auto            step    = successfulStep(state);
        if (fail_count_query)
        {
            step.mCountResult     = VK_ERROR_OUT_OF_HOST_MEMORY;
            step.mAdvertisedCount = std::numeric_limits<std::uint32_t>::max();
        }
        else
        {
            step.mListResult    = VK_ERROR_OUT_OF_DEVICE_MEMORY;
            step.mReturnedCount = std::numeric_limits<std::uint32_t>::max();
        }
        state.mEnumerationSteps = { step };

        const auto  result = resolveVulkanSwapchainImagesGeneration(parents.mLogical, parents.mConfiguration, parents.mSwapchain);
        const auto& error  = requireError(result);
        ensure("enumeration failure preserves exact command and VkResult",
               error.mCode == VulkanSwapchainImagesResolutionCode::SwapchainImageEnumerationFailure &&
                   error.mCommand == VulkanSwapchainImagesCommand::GetSwapchainImages &&
                   error.mResult == (fail_count_query ? VK_ERROR_OUT_OF_HOST_MEMORY : VK_ERROR_OUT_OF_DEVICE_MEMORY) &&
                   error.mEnumerationAttempt == 1 && error.mObservedCount == 0);
        ensure("undefined failed enumeration output is never classified as a count error",
               error.mCode != VulkanSwapchainImagesResolutionCode::SwapchainImageCountExceeded &&
                   error.mCode != VulkanSwapchainImagesResolutionCode::InvalidSwapchainImageEnumerationOutput);
        ensure("enumeration failure creates and destroys no view", state.mCreateInfos.empty() && state.mDestroyedViews.empty());
    }
}

template<>
template<>
void render_vulkan_swapchain_images_object::test<5>()
{
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        auto            first   = successfulStep(state);
        auto            second  = successfulStep(state, 4);
        first.mImages           = second.mImages;
        first.mListResult       = VK_INCOMPLETE;
        first.mReturnedCount    = first.mAdvertisedCount;
        state.mEnumerationSteps = { first, second };

        auto generation =
            takeGeneration(resolveVulkanSwapchainImagesGeneration(parents.mLogical, parents.mConfiguration, parents.mSwapchain));
        ensure("VK_INCOMPLETE retries a fresh count-then-array attempt and accepts growth",
               state.mCountCalls == 2 && state.mListCalls == 2 && generation.imageCount() == 4 &&
                   generation.image(3) == second.mImages[3] && state.mCreateInfos.size() == 4);
        generation.reset();
        ensure("the grown set owns four unique views and destroys each once in reverse order",
               state.mDestroyedViews == std::vector<VkImageView>{ fakeHandle<VkImageView>(0x8400), fakeHandle<VkImageView>(0x8300),
                                                                  fakeHandle<VkImageView>(0x8200), fakeHandle<VkImageView>(0x8100) });
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        auto            step    = successfulStep(state);
        step.mImages            = successfulStep(state, 4).mImages;
        step.mListResult        = VK_INCOMPLETE;
        step.mReturnedCount     = step.mAdvertisedCount;
        state.mEnumerationSteps.assign(VULKAN_SWAPCHAIN_IMAGE_ENUMERATION_ATTEMPTS, step);

        const auto  result = resolveVulkanSwapchainImagesGeneration(parents.mLogical, parents.mConfiguration, parents.mSwapchain);
        const auto& error  = requireError(result);
        ensure("persistent VK_INCOMPLETE has a bounded typed retry failure",
               error.mCode == VulkanSwapchainImagesResolutionCode::SwapchainImageEnumerationRetryLimitExceeded &&
                   error.mCommand == VulkanSwapchainImagesCommand::GetSwapchainImages && error.mResult == VK_INCOMPLETE &&
                   error.mEnumerationAttempt == VULKAN_SWAPCHAIN_IMAGE_ENUMERATION_ATTEMPTS &&
                   state.mCountCalls == VULKAN_SWAPCHAIN_IMAGE_ENUMERATION_ATTEMPTS &&
                   state.mListCalls == VULKAN_SWAPCHAIN_IMAGE_ENUMERATION_ATTEMPTS && state.mCreateInfos.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        auto            step    = successfulStep(state);
        // VK_INCOMPLETE is not valid for a count-only query. Keep this as a
        // defensive protocol-violation case so even malformed drivers remain bounded.
        step.mCountResult = VK_INCOMPLETE;
        state.mEnumerationSteps.assign(VULKAN_SWAPCHAIN_IMAGE_ENUMERATION_ATTEMPTS, step);
        ensureCode(resolveVulkanSwapchainImagesGeneration(parents.mLogical, parents.mConfiguration, parents.mSwapchain),
                   VulkanSwapchainImagesResolutionCode::SwapchainImageEnumerationRetryLimitExceeded);
        ensure("protocol-violating count-query incompleteness is bounded without array allocation",
               state.mCountCalls == VULKAN_SWAPCHAIN_IMAGE_ENUMERATION_ATTEMPTS && state.mListCalls == 0);
    }
}

template<>
template<>
void render_vulkan_swapchain_images_object::test<6>()
{
    struct Case
    {
        std::uint32_t                       mAdvertised;
        std::uint32_t                       mReturned;
        VulkanSwapchainImagesResolutionCode mCode;
    };
    constexpr std::array cases{ Case{ 0, 0, VulkanSwapchainImagesResolutionCode::InvalidSwapchainImageEnumerationOutput },
                                Case{ 2, 2, VulkanSwapchainImagesResolutionCode::InvalidSwapchainImageEnumerationOutput },
                                Case{ VULKAN_SWAPCHAIN_MAX_IMAGES + 1, VULKAN_SWAPCHAIN_MAX_IMAGES + 1,
                                      VulkanSwapchainImagesResolutionCode::SwapchainImageCountExceeded },
                                Case{ 3, 4, VulkanSwapchainImagesResolutionCode::InvalidSwapchainImageEnumerationOutput },
                                Case{ 3, 0, VulkanSwapchainImagesResolutionCode::InvalidSwapchainImageEnumerationOutput },
                                Case{ 3, 2, VulkanSwapchainImagesResolutionCode::InvalidSwapchainImageEnumerationOutput } };

    for (std::size_t index = 0; index < cases.size(); ++index)
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        auto            step    = successfulStep(state);
        step.mAdvertisedCount   = cases[index].mAdvertised;
        step.mReturnedCount     = cases[index].mReturned;
        state.mEnumerationSteps = { step };

        const auto  result = resolveVulkanSwapchainImagesGeneration(parents.mLogical, parents.mConfiguration, parents.mSwapchain);
        const auto& error  = requireError(result);
        ensure("invalid and excessive counts have exact typed failures", error.mCode == cases[index].mCode);
        ensure("malformed enumeration creates and destroys no view", state.mCreateInfos.empty() && state.mDestroyedViews.empty());
    }
}

template<>
template<>
void render_vulkan_swapchain_images_object::test<7>()
{
    for (bool duplicate : { false, true })
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        auto            step    = successfulStep(state);
        if (duplicate)
        {
            step.mImages[2] = step.mImages[0];
        }
        else
        {
            step.mImages[1] = VK_NULL_HANDLE;
        }
        state.mEnumerationSteps = { step };

        const auto  result = resolveVulkanSwapchainImagesGeneration(parents.mLogical, parents.mConfiguration, parents.mSwapchain);
        const auto& error  = requireError(result);
        ensure("null and duplicate borrowed images are rejected with their exact index",
               error.mCode == VulkanSwapchainImagesResolutionCode::InvalidSwapchainImageEnumerationOutput &&
                   error.mCommand == VulkanSwapchainImagesCommand::GetSwapchainImages && error.mImageIndex == (duplicate ? 2 : 1));
        ensure("invalid borrowed images create no view", state.mCreateInfos.empty() && state.mDestroyedViews.empty());
    }
}

template<>
template<>
void render_vulkan_swapchain_images_object::test<8>()
{
    FakeState         state;
    ScopedFakeState   scope(state);
    auto              parents                 = makeParents(state);
    const auto        views                   = state.defaultViews();
    const VkImageView undefined_failed_output = fakeHandle<VkImageView>(0xdead);
    state.mCreateResults                      = { VK_SUCCESS, VK_SUCCESS, VK_ERROR_OUT_OF_DEVICE_MEMORY };
    state.mCreateOutputs                      = { views[0], views[1], undefined_failed_output };

    const auto  result = resolveVulkanSwapchainImagesGeneration(parents.mLogical, parents.mConfiguration, parents.mSwapchain);
    const auto& error  = requireError(result);
    ensure("partial creation failure preserves exact command, result, count, and index",
           error.mCode == VulkanSwapchainImagesResolutionCode::ImageViewCreationFailure &&
               error.mCommand == VulkanSwapchainImagesCommand::CreateImageView && error.mResult == VK_ERROR_OUT_OF_DEVICE_MEMORY &&
               error.mObservedCount == 3 && error.mImageIndex == 2);
    ensure("partial creation rolls back only successful views in reverse order",
           state.mDestroyedViews == std::vector<VkImageView>{ views[1], views[0] });
    ensure("the undefined non-null failed output is never inspected or destroyed",
           undefined_failed_output != VK_NULL_HANDLE && std::find(state.mDestroyedViews.begin(), state.mDestroyedViews.end(),
                                                                  undefined_failed_output) == state.mDestroyedViews.end());
}

template<>
template<>
void render_vulkan_swapchain_images_object::test<9>()
{
    FakeState       state;
    ScopedFakeState scope(state);
    auto            parents = makeParents(state);
    const auto      views   = state.defaultViews();
    state.mCreateOutputs    = { views[0], VK_NULL_HANDLE };

    const auto  result = resolveVulkanSwapchainImagesGeneration(parents.mLogical, parents.mConfiguration, parents.mSwapchain);
    const auto& error  = requireError(result);
    ensure("success with a null view is typed at the exact index",
           error.mCode == VulkanSwapchainImagesResolutionCode::NullImageViewOnSuccess &&
               error.mCommand == VulkanSwapchainImagesCommand::CreateImageView && error.mResult == VK_SUCCESS && error.mImageIndex == 1);
    ensure("null success rolls back only the earlier valid view", state.mDestroyedViews == std::vector<VkImageView>{ views[0] });
}

template<>
template<>
void render_vulkan_swapchain_images_object::test<10>()
{
    FakeState       state;
    ScopedFakeState scope(state);
    auto            parents = makeParents(state);
    auto generation = takeGeneration(resolveVulkanSwapchainImagesGeneration(parents.mLogical, parents.mConfiguration, parents.mSwapchain));
    auto moved      = std::move(generation);

    ensure("move transfers all view ownership and disarms the source",
           moved.imageCount() == 3 && moved.createdFor(parents.mLogical, parents.mConfiguration, parents.mSwapchain) &&
               generation.imageCount() == 0 && generation.image(0) == VK_NULL_HANDLE && generation.imageView(0) == VK_NULL_HANDLE &&
               !generation.createdFor(parents.mLogical, parents.mConfiguration, parents.mSwapchain));

    FakeState distinct_state;
    distinct_state.mInstance       = fakeHandle<VkInstance>(0x1100);
    distinct_state.mSurface        = fakeHandle<VkSurfaceKHR>(0x2200);
    distinct_state.mPhysicalDevice = fakeHandle<VkPhysicalDevice>(0x3300);
    distinct_state.mDevice         = fakeHandle<VkDevice>(0x4400);
    distinct_state.mQueue          = fakeHandle<VkQueue>(0x5500);
    distinct_state.mSwapchain      = fakeHandle<VkSwapchainKHR>(0x6600);
    {
        ScopedFakeState distinct_scope(distinct_state);
        auto            distinct_parents = makeParents(distinct_state);
        ensure("distinct live parent provenance is never accepted",
               !moved.createdFor(distinct_parents.mLogical, distinct_parents.mConfiguration, distinct_parents.mSwapchain));
    }

    moved.reset();
    ensure("only the moved owner destroys each view once", state.mDestroyedViews.size() == 3);
}

template<>
template<>
void render_vulkan_swapchain_images_object::test<11>()
{
    gAllocationCalls    = 0;
    gFailAllocationCall = 1;
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        const auto      result =
            VulkanSwapchainImagesDetail::resolve(parents.mLogical, parents.mConfiguration, parents.mSwapchain, allocationCheckpoint);
        const auto& error = requireError(result);
        ensure("image-array allocation failure is typed before array enumeration",
               error.mCode == VulkanSwapchainImagesResolutionCode::ScratchAllocationFailure &&
                   error.mCommand == VulkanSwapchainImagesCommand::GetSwapchainImages && error.mObservedCount == 3 &&
                   error.mEnumerationAttempt == 1 && gAllocationCalls == 1 && state.mListCalls == 0 && state.mCreateInfos.empty());
    }

    gAllocationCalls    = 0;
    gFailAllocationCall = 2;
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        const auto      result =
            VulkanSwapchainImagesDetail::resolve(parents.mLogical, parents.mConfiguration, parents.mSwapchain, allocationCheckpoint);
        const auto& error = requireError(result);
        ensure("view-array allocation failure is typed before Vulkan mutation",
               error.mCode == VulkanSwapchainImagesResolutionCode::ScratchAllocationFailure &&
                   error.mCommand == VulkanSwapchainImagesCommand::CreateImageView && error.mObservedCount == 3 && gAllocationCalls == 2 &&
                   state.mCountCalls == 1 && state.mListCalls == 1 && state.mCreateInfos.empty() && state.mDestroyedViews.empty());

        gFailAllocationCall = 0;
        auto retried        = takeGeneration(
            VulkanSwapchainImagesDetail::resolve(parents.mLogical, parents.mConfiguration, parents.mSwapchain, allocationCheckpoint));
        ensure("unchanged parents remain reusable after allocation failure",
               retried.createdFor(parents.mLogical, parents.mConfiguration, parents.mSwapchain));
    }

    gAllocationCalls    = 0;
    gFailAllocationCall = 2;
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        auto            first   = successfulStep(state);
        first.mImages           = successfulStep(state, 4).mImages;
        first.mListResult       = VK_INCOMPLETE;
        first.mReturnedCount    = first.mAdvertisedCount;
        state.mEnumerationSteps = { first, successfulStep(state) };
        const auto result =
            VulkanSwapchainImagesDetail::resolve(parents.mLogical, parents.mConfiguration, parents.mSwapchain, allocationCheckpoint);
        const auto& error = requireError(result);
        ensure("each enumeration retry has its own deterministic allocation checkpoint",
               error.mCode == VulkanSwapchainImagesResolutionCode::ScratchAllocationFailure &&
                   error.mCommand == VulkanSwapchainImagesCommand::GetSwapchainImages && error.mEnumerationAttempt == 2 &&
                   gAllocationCalls == 2 && state.mCountCalls == 2 && state.mListCalls == 1 && state.mCreateInfos.empty());
    }
    gFailAllocationCall = 0;
}

} // namespace tut
