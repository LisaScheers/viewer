/**
 * @file llrendervulkanswapchainpresentationtarget_test.cpp
 * @brief Tests for Vulkan swapchain presentation-target ownership.
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

#include "llrendervulkanswapchainpresentationtarget.h"
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
    CreateRenderPass,
    DestroyRenderPass,
    CreateFramebuffer,
    DestroyFramebuffer
};

struct RenderPassRecord
{
    VkStructureType         mStructureType = VK_STRUCTURE_TYPE_MAX_ENUM;
    const void*             mNext          = nullptr;
    VkRenderPassCreateFlags mFlags         = 0;
    std::uint32_t           mAttachmentCount = 0;
    std::uint32_t           mSubpassCount    = 0;
    std::uint32_t           mDependencyCount = 0;
    bool                    mHasAttachments   = false;
    bool                    mHasSubpasses     = false;
    bool                    mHasDependencies  = false;
    VkAttachmentDescription mAttachment{};
    VkSubpassDescription     mSubpass{};
    VkAttachmentReference    mColorAttachment{};
    bool                     mHasColorAttachment = false;
};

struct FramebufferRecord
{
    VkStructureType          mStructureType = VK_STRUCTURE_TYPE_MAX_ENUM;
    const void*              mNext          = nullptr;
    VkFramebufferCreateFlags mFlags         = 0;
    VkRenderPass             mRenderPass    = VK_NULL_HANDLE;
    std::uint32_t            mAttachmentCount = 0;
    VkImageView              mAttachment      = VK_NULL_HANDLE;
    std::uint32_t            mWidth            = 0;
    std::uint32_t            mHeight           = 0;
    std::uint32_t            mLayers           = 0;
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

    VkSurfaceCapabilitiesKHR mCapabilities{
        2,
        0,
        { std::numeric_limits<std::uint32_t>::max(), std::numeric_limits<std::uint32_t>::max() },
        { 64, 64 },
        { 4096, 2160 },
        1,
        VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
    };
    std::array<VkSurfaceFormatKHR, 1> mFormats{
        VkSurfaceFormatKHR{ VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR }
    };
    std::array<VkPresentModeKHR, 1> mPresentModes{ VK_PRESENT_MODE_FIFO_KHR };
    std::array<VkImage, 3>          mImages{ fakeHandle<VkImage>(0x7100), fakeHandle<VkImage>(0x7200),
                                             fakeHandle<VkImage>(0x7300) };

    MissingCommand           mMissingCommand = MissingCommand::None;
    std::vector<std::string> mInstanceLookups;
    std::vector<std::string> mDeviceLookups;

    std::vector<VkImageView> mCreatedImageViews;
    std::size_t              mDestroyImageViewCalls = 0;
    std::size_t              mDestroySwapchainCalls = 0;
    std::size_t              mDestroyDeviceCalls    = 0;

    VkResult     mRenderPassResult = VK_SUCCESS;
    VkRenderPass mRenderPassOutput = fakeHandle<VkRenderPass>(0x9000);
    std::vector<RenderPassRecord> mRenderPassRecords;
    std::vector<VkDevice>         mRenderPassDevices;
    std::vector<bool>             mRenderPassAllocatorNull;

    std::vector<VkResult>             mFramebufferResults;
    std::vector<VkFramebuffer>        mFramebufferOutputs;
    std::vector<FramebufferRecord>    mFramebufferRecords;
    std::vector<VkDevice>             mFramebufferDevices;
    std::vector<bool>                 mFramebufferAllocatorNull;

    std::vector<VkFramebuffer> mDestroyedFramebuffers;
    std::vector<VkDevice>      mFramebufferDestroyDevices;
    std::vector<bool>          mFramebufferDestroyAllocatorNull;
    std::vector<VkRenderPass>  mDestroyedRenderPasses;
    std::vector<VkDevice>      mRenderPassDestroyDevices;
    std::vector<bool>          mRenderPassDestroyAllocatorNull;
    std::vector<std::string>   mTargetDestroyOrder;
    bool                       mAllCommandsResolvedBeforeMutation = false;
    bool                       mAllocationCompletedBeforeMutation = false;

    std::vector<VkFramebuffer> defaultFramebuffers() const
    {
        return { fakeHandle<VkFramebuffer>(0xa100), fakeHandle<VkFramebuffer>(0xa200), fakeHandle<VkFramebuffer>(0xa300) };
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

VKAPI_ATTR VkResult VKAPI_CALL fakeEnumeratePhysicalDevices(VkInstance instance,
                                                            std::uint32_t* count,
                                                            VkPhysicalDevice* devices) noexcept
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
    properties->limits.maxFramebufferLayers     = 1;
    properties->limits.maxViewportDimensions[0] = 4096;
    properties->limits.maxViewportDimensions[1] = 4096;
    properties->limits.viewportBoundsRange[0]   = -8192.0f;
    properties->limits.viewportBoundsRange[1]   = 8191.0f;
    std::strncpy(properties->deviceName, "presentation-target-fake", VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 1);
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

VKAPI_ATTR void VKAPI_CALL fakeGetPhysicalDeviceFeatures2(VkPhysicalDevice physical_device,
                                                          VkPhysicalDeviceFeatures2* features) noexcept
{
    if (!gFakeState || physical_device != gFakeState->mPhysicalDevice || !features)
    {
        return;
    }
    auto* maintenance = static_cast<VkPhysicalDeviceSwapchainMaintenance1FeaturesKHR*>(features->pNext);
    if (features->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 && maintenance &&
        maintenance->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_KHR && !maintenance->pNext)
    {
        maintenance->swapchainMaintenance1 = VK_TRUE;
    }
}

VKAPI_ATTR void VKAPI_CALL fakeGetPhysicalDeviceFeatures(VkPhysicalDevice physical_device,
                                                         VkPhysicalDeviceFeatures* features) noexcept
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
        create_info->enabledExtensionCount != 2 || !create_info->ppEnabledExtensionNames ||
        !create_info->ppEnabledExtensionNames[0] || !create_info->ppEnabledExtensionNames[1] ||
        std::strcmp(create_info->ppEnabledExtensionNames[0], VK_KHR_SWAPCHAIN_EXTENSION_NAME) != 0 ||
        std::strcmp(create_info->ppEnabledExtensionNames[1], VK_KHR_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME) != 0)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    const auto* maintenance = static_cast<const VkPhysicalDeviceSwapchainMaintenance1FeaturesKHR*>(create_info->pNext);
    if (!maintenance || maintenance->sType != VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_KHR ||
        maintenance->swapchainMaintenance1 != VK_TRUE || maintenance->pNext)
    {
        return VK_ERROR_FEATURE_NOT_PRESENT;
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

VKAPI_ATTR void VKAPI_CALL fakeGetDeviceQueue(VkDevice device,
                                              std::uint32_t queue_family,
                                              std::uint32_t queue_index,
                                              VkQueue* queue) noexcept
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

VKAPI_ATTR VkResult VKAPI_CALL fakeGetSurfaceFormats(VkPhysicalDevice physical_device,
                                                     VkSurfaceKHR     surface,
                                                     std::uint32_t*   count,
                                                     VkSurfaceFormatKHR* formats) noexcept
{
    if (!gFakeState || physical_device != gFakeState->mPhysicalDevice || surface != gFakeState->mSurface || !count)
    {
        return VK_ERROR_SURFACE_LOST_KHR;
    }
    if (!formats)
    {
        *count = static_cast<std::uint32_t>(gFakeState->mFormats.size());
        return VK_SUCCESS;
    }
    const std::uint32_t written = std::min(*count, static_cast<std::uint32_t>(gFakeState->mFormats.size()));
    std::copy_n(gFakeState->mFormats.begin(), written, formats);
    *count = written;
    return written == gFakeState->mFormats.size() ? VK_SUCCESS : VK_INCOMPLETE;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeGetSurfacePresentModes(VkPhysicalDevice physical_device,
                                                          VkSurfaceKHR     surface,
                                                          std::uint32_t*   count,
                                                          VkPresentModeKHR* modes) noexcept
{
    if (!gFakeState || physical_device != gFakeState->mPhysicalDevice || surface != gFakeState->mSurface || !count)
    {
        return VK_ERROR_SURFACE_LOST_KHR;
    }
    if (!modes)
    {
        *count = static_cast<std::uint32_t>(gFakeState->mPresentModes.size());
        return VK_SUCCESS;
    }
    const std::uint32_t written = std::min(*count, static_cast<std::uint32_t>(gFakeState->mPresentModes.size()));
    std::copy_n(gFakeState->mPresentModes.begin(), written, modes);
    *count = written;
    return written == gFakeState->mPresentModes.size() ? VK_SUCCESS : VK_INCOMPLETE;
}

VKAPI_ATTR void VKAPI_CALL fakeGetPhysicalDeviceFormatProperties(VkPhysicalDevice    physical_device,
                                                                 VkFormat            format,
                                                                 VkFormatProperties* properties) noexcept
{
    if (gFakeState && physical_device == gFakeState->mPhysicalDevice && format == gFakeState->mFormats[0].format && properties)
    {
        *properties                       = {};
        properties->optimalTilingFeatures = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    }
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

VKAPI_ATTR void VKAPI_CALL fakeDestroySwapchain(VkDevice device,
                                                VkSwapchainKHR swapchain,
                                                const VkAllocationCallbacks*) noexcept
{
    if (gFakeState && device == gFakeState->mDevice && swapchain == gFakeState->mSwapchain)
    {
        ++gFakeState->mDestroySwapchainCalls;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeGetSwapchainImages(VkDevice device,
                                                      VkSwapchainKHR swapchain,
                                                      std::uint32_t* count,
                                                      VkImage* images) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || swapchain != gFakeState->mSwapchain || !count)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!images)
    {
        *count = static_cast<std::uint32_t>(gFakeState->mImages.size());
        return VK_SUCCESS;
    }
    const std::uint32_t written = std::min(*count, static_cast<std::uint32_t>(gFakeState->mImages.size()));
    std::copy_n(gFakeState->mImages.begin(), written, images);
    *count = written;
    return written == gFakeState->mImages.size() ? VK_SUCCESS : VK_INCOMPLETE;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateImageView(VkDevice device,
                                                   const VkImageViewCreateInfo*,
                                                   const VkAllocationCallbacks*,
                                                   VkImageView* image_view) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || !image_view)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    *image_view = fakeHandle<VkImageView>(0x8100 + gFakeState->mCreatedImageViews.size() * 0x100);
    gFakeState->mCreatedImageViews.push_back(*image_view);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyImageView(VkDevice device,
                                                VkImageView,
                                                const VkAllocationCallbacks*) noexcept
{
    if (gFakeState && device == gFakeState->mDevice)
    {
        ++gFakeState->mDestroyImageViewCalls;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateRenderPass(VkDevice device,
                                                    const VkRenderPassCreateInfo* create_info,
                                                    const VkAllocationCallbacks* allocator,
                                                    VkRenderPass* render_pass) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || !create_info || !render_pass)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    RenderPassRecord record;
    record.mStructureType   = create_info->sType;
    record.mNext            = create_info->pNext;
    record.mFlags           = create_info->flags;
    record.mAttachmentCount = create_info->attachmentCount;
    record.mSubpassCount    = create_info->subpassCount;
    record.mDependencyCount = create_info->dependencyCount;
    record.mHasAttachments  = create_info->pAttachments != nullptr;
    record.mHasSubpasses    = create_info->pSubpasses != nullptr;
    record.mHasDependencies = create_info->pDependencies != nullptr;
    if (create_info->attachmentCount == 1 && create_info->pAttachments)
    {
        record.mAttachment = create_info->pAttachments[0];
    }
    if (create_info->subpassCount == 1 && create_info->pSubpasses)
    {
        record.mSubpass = create_info->pSubpasses[0];
        if (record.mSubpass.colorAttachmentCount == 1 && record.mSubpass.pColorAttachments)
        {
            record.mColorAttachment    = record.mSubpass.pColorAttachments[0];
            record.mHasColorAttachment = true;
        }
    }
    gFakeState->mRenderPassRecords.push_back(record);
    gFakeState->mRenderPassDevices.push_back(device);
    gFakeState->mRenderPassAllocatorNull.push_back(allocator == nullptr);
    gFakeState->mAllCommandsResolvedBeforeMutation =
        gFakeState->mInstanceLookups == std::vector<std::string>{ "vkGetDeviceProcAddr" } &&
        gFakeState->mDeviceLookups == std::vector<std::string>{ "vkCreateRenderPass", "vkDestroyRenderPass",
                                                                "vkCreateFramebuffer", "vkDestroyFramebuffer" };
    gFakeState->mAllocationCompletedBeforeMutation = gAllocationCalls == 1;

    *render_pass = gFakeState->mRenderPassOutput;
    return gFakeState->mRenderPassResult;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyRenderPass(VkDevice device,
                                                 VkRenderPass render_pass,
                                                 const VkAllocationCallbacks* allocator) noexcept
{
    if (!gFakeState)
    {
        return;
    }
    gFakeState->mDestroyedRenderPasses.push_back(render_pass);
    gFakeState->mRenderPassDestroyDevices.push_back(device);
    gFakeState->mRenderPassDestroyAllocatorNull.push_back(allocator == nullptr);
    gFakeState->mTargetDestroyOrder.emplace_back("render-pass");
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateFramebuffer(VkDevice device,
                                                     const VkFramebufferCreateInfo* create_info,
                                                     const VkAllocationCallbacks* allocator,
                                                     VkFramebuffer* framebuffer) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || !create_info || !framebuffer)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    FramebufferRecord record;
    record.mStructureType   = create_info->sType;
    record.mNext            = create_info->pNext;
    record.mFlags           = create_info->flags;
    record.mRenderPass      = create_info->renderPass;
    record.mAttachmentCount = create_info->attachmentCount;
    if (create_info->attachmentCount == 1 && create_info->pAttachments)
    {
        record.mAttachment = create_info->pAttachments[0];
    }
    record.mWidth  = create_info->width;
    record.mHeight = create_info->height;
    record.mLayers = create_info->layers;
    gFakeState->mFramebufferRecords.push_back(record);
    gFakeState->mFramebufferDevices.push_back(device);
    gFakeState->mFramebufferAllocatorNull.push_back(allocator == nullptr);

    const std::size_t index = gFakeState->mFramebufferRecords.size() - 1;
    const auto defaults     = gFakeState->defaultFramebuffers();
    *framebuffer = index < gFakeState->mFramebufferOutputs.size() ? gFakeState->mFramebufferOutputs[index] : defaults[index];
    return index < gFakeState->mFramebufferResults.size() ? gFakeState->mFramebufferResults[index] : VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyFramebuffer(VkDevice device,
                                                  VkFramebuffer framebuffer,
                                                  const VkAllocationCallbacks* allocator) noexcept
{
    if (!gFakeState)
    {
        return;
    }
    gFakeState->mDestroyedFramebuffers.push_back(framebuffer);
    gFakeState->mFramebufferDestroyDevices.push_back(device);
    gFakeState->mFramebufferDestroyAllocatorNull.push_back(allocator == nullptr);
    gFakeState->mTargetDestroyOrder.emplace_back("framebuffer");
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL fakeGetDeviceProcAddr(VkDevice device, const char* name) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || !name)
    {
        return nullptr;
    }
    if (std::strcmp(name, "vkCreateSwapchainKHR") == 0)
        return eraseFunctionType(fakeCreateSwapchain);
    if (std::strcmp(name, "vkDestroySwapchainKHR") == 0)
        return eraseFunctionType(fakeDestroySwapchain);
    if (std::strcmp(name, "vkGetSwapchainImagesKHR") == 0)
        return eraseFunctionType(fakeGetSwapchainImages);
    if (std::strcmp(name, "vkCreateImageView") == 0)
        return eraseFunctionType(fakeCreateImageView);
    if (std::strcmp(name, "vkDestroyImageView") == 0)
        return eraseFunctionType(fakeDestroyImageView);

    gFakeState->mDeviceLookups.emplace_back(name);
    if (std::strcmp(name, "vkCreateRenderPass") == 0)
        return gFakeState->mMissingCommand == MissingCommand::CreateRenderPass ? nullptr : eraseFunctionType(fakeCreateRenderPass);
    if (std::strcmp(name, "vkDestroyRenderPass") == 0)
        return gFakeState->mMissingCommand == MissingCommand::DestroyRenderPass ? nullptr : eraseFunctionType(fakeDestroyRenderPass);
    if (std::strcmp(name, "vkCreateFramebuffer") == 0)
        return gFakeState->mMissingCommand == MissingCommand::CreateFramebuffer ? nullptr : eraseFunctionType(fakeCreateFramebuffer);
    if (std::strcmp(name, "vkDestroyFramebuffer") == 0)
        return gFakeState->mMissingCommand == MissingCommand::DestroyFramebuffer ? nullptr : eraseFunctionType(fakeDestroyFramebuffer);
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
        return eraseFunctionType(fakeGetPhysicalDeviceFormatProperties);

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
    VulkanSwapchainImagesGeneration        mImages;
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

    auto images_result = resolveVulkanSwapchainImagesGeneration(logical, configuration, swapchain);
    tut::ensure("the swapchain-images fixture resolves", std::holds_alternative<VulkanSwapchainImagesGeneration>(images_result));
    auto images = std::get<VulkanSwapchainImagesGeneration>(std::move(images_result));

    state.mInstanceLookups.clear();
    state.mDeviceLookups.clear();
    return { std::move(physical), std::move(logical), std::move(configuration), std::move(swapchain), std::move(images) };
}

const VulkanSwapchainPresentationTargetResolutionError& requireError(
    const VulkanSwapchainPresentationTargetResolutionResult& result)
{
    const auto* error = std::get_if<VulkanSwapchainPresentationTargetResolutionError>(&result);
    tut::ensure("presentation-target resolution returns an error", error != nullptr);
    return *error;
}

void ensureCode(const VulkanSwapchainPresentationTargetResolutionResult& result,
                VulkanSwapchainPresentationTargetResolutionCode code)
{
    tut::ensure("the exact presentation-target error is reported", requireError(result).mCode == code);
}

VulkanSwapchainPresentationTargetGeneration takeTarget(VulkanSwapchainPresentationTargetResolutionResult&& result)
{
    tut::ensure("presentation-target resolution returns a generation",
                std::holds_alternative<VulkanSwapchainPresentationTargetGeneration>(result));
    return std::get<VulkanSwapchainPresentationTargetGeneration>(std::move(result));
}

VulkanSwapchainImagesGeneration takeImages(VulkanSwapchainImagesResolutionResult&& result)
{
    tut::ensure("swapchain-images resolution returns a generation", std::holds_alternative<VulkanSwapchainImagesGeneration>(result));
    return std::get<VulkanSwapchainImagesGeneration>(std::move(result));
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

struct render_vulkan_swapchain_presentation_target_test
{
};

using render_vulkan_swapchain_presentation_target_group = test_group<render_vulkan_swapchain_presentation_target_test>;
using render_vulkan_swapchain_presentation_target_object = render_vulkan_swapchain_presentation_target_group::object;
render_vulkan_swapchain_presentation_target_group render_vulkan_swapchain_presentation_target_tests(
    "render Vulkan swapchain presentation target");

template<>
template<>
void render_vulkan_swapchain_presentation_target_object::test<1>()
{
    static_assert(!std::is_default_constructible_v<VulkanSwapchainPresentationTargetGeneration>);
    static_assert(!std::is_copy_constructible_v<VulkanSwapchainPresentationTargetGeneration>);
    static_assert(!std::is_copy_assignable_v<VulkanSwapchainPresentationTargetGeneration>);
    static_assert(std::is_nothrow_move_constructible_v<VulkanSwapchainPresentationTargetGeneration>);
    static_assert(!std::is_move_assignable_v<VulkanSwapchainPresentationTargetGeneration>);
    static_assert(std::is_nothrow_destructible_v<VulkanSwapchainPresentationTargetGeneration>);
    static_assert(std::variant_size_v<VulkanSwapchainPresentationTargetResolutionResult> == 2);
    static_assert(noexcept(resolveVulkanSwapchainPresentationTargetGeneration(
        std::declval<const VulkanLogicalDeviceGeneration&>(),
        std::declval<const VulkanSwapchainConfigurationGeneration&>(),
        std::declval<const VulkanSwapchainGeneration&>(),
        std::declval<const VulkanSwapchainImagesGeneration&>())));

    const VulkanSwapchainPresentationTargetResolutionError value{
        VulkanSwapchainPresentationTargetResolutionCode::FramebufferCreationFailure,
        VulkanSwapchainPresentationTargetCommand::CreateFramebuffer,
        VK_ERROR_OUT_OF_DEVICE_MEMORY,
        3,
        2
    };
    ensure("identical typed errors compare equal", value == value);

    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        auto            logical = std::move(parents.mLogical);
        ensureCode(resolveVulkanSwapchainPresentationTargetGeneration(parents.mLogical, parents.mConfiguration,
                                                                       parents.mSwapchain, parents.mImages),
                   VulkanSwapchainPresentationTargetResolutionCode::InvalidLogicalDeviceGeneration);
        ensure("an invalid logical parent stops before dispatch", state.mInstanceLookups.empty() && state.mDeviceLookups.empty());
        ensure("the moved logical parent remains live", logical.device() == state.mDevice);
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents       = makeParents(state);
        auto            configuration = std::move(parents.mConfiguration);
        ensureCode(resolveVulkanSwapchainPresentationTargetGeneration(parents.mLogical, parents.mConfiguration,
                                                                       parents.mSwapchain, parents.mImages),
                   VulkanSwapchainPresentationTargetResolutionCode::InvalidSwapchainConfigurationGeneration);
        ensure("an invalid configuration parent stops before dispatch", state.mInstanceLookups.empty() && state.mDeviceLookups.empty());
        ensure("the moved configuration parent remains live", configuration.device() == state.mDevice);
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents   = makeParents(state);
        auto            swapchain = std::move(parents.mSwapchain);
        ensureCode(resolveVulkanSwapchainPresentationTargetGeneration(parents.mLogical, parents.mConfiguration,
                                                                       parents.mSwapchain, parents.mImages),
                   VulkanSwapchainPresentationTargetResolutionCode::InvalidSwapchainGeneration);
        ensure("an invalid swapchain parent stops before dispatch", state.mInstanceLookups.empty() && state.mDeviceLookups.empty());
        ensure("the moved swapchain parent remains live", swapchain.swapchain() == state.mSwapchain);
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        auto            images  = std::move(parents.mImages);
        ensureCode(resolveVulkanSwapchainPresentationTargetGeneration(parents.mLogical, parents.mConfiguration,
                                                                       parents.mSwapchain, parents.mImages),
                   VulkanSwapchainPresentationTargetResolutionCode::InvalidSwapchainImagesGeneration);
        ensure("an invalid images parent stops before dispatch", state.mInstanceLookups.empty() && state.mDeviceLookups.empty());
        ensure("the moved images parent remains live", images.imageCount() == 3);
    }
}

template<>
template<>
void render_vulkan_swapchain_presentation_target_object::test<2>()
{
    constexpr std::array cases{
        std::pair{ MissingCommand::GetDeviceProcAddr, VulkanSwapchainPresentationTargetCommand::GetDeviceProcAddr },
        std::pair{ MissingCommand::CreateRenderPass, VulkanSwapchainPresentationTargetCommand::CreateRenderPass },
        std::pair{ MissingCommand::DestroyRenderPass, VulkanSwapchainPresentationTargetCommand::DestroyRenderPass },
        std::pair{ MissingCommand::CreateFramebuffer, VulkanSwapchainPresentationTargetCommand::CreateFramebuffer },
        std::pair{ MissingCommand::DestroyFramebuffer, VulkanSwapchainPresentationTargetCommand::DestroyFramebuffer }
    };

    for (std::size_t index = 0; index < cases.size(); ++index)
    {
        gAllocationCalls    = 0;
        gFailAllocationCall = 0;
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        state.mMissingCommand   = cases[index].first;

        const auto result = VulkanSwapchainPresentationTargetDetail::resolve(
            parents.mLogical, parents.mConfiguration, parents.mSwapchain, parents.mImages, allocationCheckpoint);
        const auto& error = requireError(result);
        ensure("missing dispatch reports its exact typed command",
               error.mCode == VulkanSwapchainPresentationTargetResolutionCode::MissingRequiredCommand &&
                   error.mCommand == cases[index].second);
        ensure("missing dispatch has the exact instance cutoff", state.mInstanceLookups.size() == 1);
        ensure("missing dispatch has the exact device cutoff", state.mDeviceLookups.size() == index);
        ensure("every command resolves before allocation or mutation",
               gAllocationCalls == 0 && state.mRenderPassRecords.empty() && state.mFramebufferRecords.empty() &&
                   state.mDestroyedFramebuffers.empty() && state.mDestroyedRenderPasses.empty());
    }
}

template<>
template<>
void render_vulkan_swapchain_presentation_target_object::test<3>()
{
    gAllocationCalls    = 0;
    gFailAllocationCall = 0;
    FakeState       state;
    ScopedFakeState scope(state);
    auto            parents = makeParents(state);
    auto target = takeTarget(VulkanSwapchainPresentationTargetDetail::resolve(
        parents.mLogical, parents.mConfiguration, parents.mSwapchain, parents.mImages, allocationCheckpoint));

    const auto expected_framebuffers = state.defaultFramebuffers();
    ensure("dispatch resolves in exact order before host allocation and Vulkan mutation",
           state.mAllCommandsResolvedBeforeMutation && state.mAllocationCompletedBeforeMutation && gAllocationCalls == 1);
    ensure("the owner exposes the exact render pass, framebuffers, format, and selected image extent",
           target.renderPass() == state.mRenderPassOutput && target.framebufferCount() == 3 &&
               target.framebuffer(0) == expected_framebuffers[0] && target.framebuffer(2) == expected_framebuffers[2] &&
               target.framebuffer(3) == VK_NULL_HANDLE && target.imageFormat() == VK_FORMAT_B8G8R8A8_UNORM &&
               target.imageExtent().width == 1280 && target.imageExtent().height == 720);
    ensure("the owner authenticates the exact live parents",
           target.createdFor(parents.mLogical, parents.mConfiguration, parents.mSwapchain, parents.mImages));

    ensure("one render pass is created on the exact device with no custom allocator",
           state.mRenderPassRecords.size() == 1 && state.mRenderPassDevices == std::vector<VkDevice>{ state.mDevice } &&
               state.mRenderPassAllocatorNull == std::vector<bool>{ true });
    const auto& render_pass = state.mRenderPassRecords[0];
    ensure("the render-pass root has one attachment, one subpass, and no dependencies",
           render_pass.mStructureType == VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO && !render_pass.mNext && render_pass.mFlags == 0 &&
               render_pass.mAttachmentCount == 1 && render_pass.mHasAttachments && render_pass.mSubpassCount == 1 &&
               render_pass.mHasSubpasses && render_pass.mDependencyCount == 0 && !render_pass.mHasDependencies);
    ensure("the presentation attachment has the exact fixed color contract",
           render_pass.mAttachment.flags == 0 && render_pass.mAttachment.format == VK_FORMAT_B8G8R8A8_UNORM &&
               render_pass.mAttachment.samples == VK_SAMPLE_COUNT_1_BIT &&
               render_pass.mAttachment.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR &&
               render_pass.mAttachment.storeOp == VK_ATTACHMENT_STORE_OP_STORE &&
               render_pass.mAttachment.stencilLoadOp == VK_ATTACHMENT_LOAD_OP_DONT_CARE &&
               render_pass.mAttachment.stencilStoreOp == VK_ATTACHMENT_STORE_OP_DONT_CARE &&
               render_pass.mAttachment.initialLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
               render_pass.mAttachment.finalLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    ensure("the graphics subpass binds only color attachment zero in attachment-optimal layout",
           render_pass.mSubpass.flags == 0 && render_pass.mSubpass.pipelineBindPoint == VK_PIPELINE_BIND_POINT_GRAPHICS &&
               render_pass.mSubpass.inputAttachmentCount == 0 && !render_pass.mSubpass.pInputAttachments &&
               render_pass.mSubpass.colorAttachmentCount == 1 && render_pass.mHasColorAttachment &&
               render_pass.mColorAttachment.attachment == 0 &&
               render_pass.mColorAttachment.layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
               !render_pass.mSubpass.pResolveAttachments && !render_pass.mSubpass.pDepthStencilAttachment &&
               render_pass.mSubpass.preserveAttachmentCount == 0 && !render_pass.mSubpass.pPreserveAttachments);

    ensure("one concrete framebuffer is created per actual image view",
           state.mFramebufferRecords.size() == 3 && state.mFramebufferDevices == std::vector<VkDevice>(3, state.mDevice) &&
               state.mFramebufferAllocatorNull == std::vector<bool>(3, true));
    for (std::uint32_t index = 0; index < 3; ++index)
    {
        const auto& framebuffer = state.mFramebufferRecords[index];
        ensure("each framebuffer has the exact render pass, image view, extent, and layer count",
               framebuffer.mStructureType == VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO && !framebuffer.mNext &&
                   framebuffer.mFlags == 0 && framebuffer.mRenderPass == state.mRenderPassOutput &&
                   framebuffer.mAttachmentCount == 1 && framebuffer.mAttachment == parents.mImages.imageView(index) &&
                   framebuffer.mWidth == 1280 && framebuffer.mHeight == 720 && framebuffer.mLayers == 1);
    }

    target.reset();
    ensure("reset destroys framebuffers in reverse order before the render pass",
           state.mDestroyedFramebuffers ==
                   std::vector<VkFramebuffer>{ expected_framebuffers[2], expected_framebuffers[1], expected_framebuffers[0] } &&
               state.mDestroyedRenderPasses == std::vector<VkRenderPass>{ state.mRenderPassOutput } &&
               state.mTargetDestroyOrder ==
                   std::vector<std::string>{ "framebuffer", "framebuffer", "framebuffer", "render-pass" });
    ensure("reset uses the exact device and no custom allocators",
           state.mFramebufferDestroyDevices == std::vector<VkDevice>(3, state.mDevice) &&
               state.mFramebufferDestroyAllocatorNull == std::vector<bool>(3, true) &&
               state.mRenderPassDestroyDevices == std::vector<VkDevice>{ state.mDevice } &&
               state.mRenderPassDestroyAllocatorNull == std::vector<bool>{ true });
    ensure("reset disarms the owner without touching parent image views",
           target.renderPass() == VK_NULL_HANDLE && target.framebufferCount() == 0 && target.framebuffer(0) == VK_NULL_HANDLE &&
               !target.createdFor(parents.mLogical, parents.mConfiguration, parents.mSwapchain, parents.mImages) &&
               state.mDestroyImageViewCalls == 0);
    target.reset();
    ensure("reset is idempotent", state.mTargetDestroyOrder.size() == 4);
}

template<>
template<>
void render_vulkan_swapchain_presentation_target_object::test<4>()
{
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents       = makeParents(state);
        const auto      failed_output = fakeHandle<VkRenderPass>(0xdead);
        state.mRenderPassResult       = VK_ERROR_OUT_OF_DEVICE_MEMORY;
        state.mRenderPassOutput       = failed_output;

        const auto result = resolveVulkanSwapchainPresentationTargetGeneration(
            parents.mLogical, parents.mConfiguration, parents.mSwapchain, parents.mImages);
        const auto& error = requireError(result);
        ensure("render-pass failure preserves the exact command and VkResult",
               error.mCode == VulkanSwapchainPresentationTargetResolutionCode::RenderPassCreationFailure &&
                   error.mCommand == VulkanSwapchainPresentationTargetCommand::CreateRenderPass &&
                   error.mResult == VK_ERROR_OUT_OF_DEVICE_MEMORY && error.mImageCount == 3);
        ensure("the undefined failed render-pass output is ignored",
               failed_output != VK_NULL_HANDLE && state.mDestroyedRenderPasses.empty() && state.mFramebufferRecords.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        state.mRenderPassOutput = VK_NULL_HANDLE;

        const auto result = resolveVulkanSwapchainPresentationTargetGeneration(
            parents.mLogical, parents.mConfiguration, parents.mSwapchain, parents.mImages);
        const auto& error = requireError(result);
        ensure("success with a null render pass has an exact typed failure",
               error.mCode == VulkanSwapchainPresentationTargetResolutionCode::NullRenderPassOnSuccess &&
                   error.mCommand == VulkanSwapchainPresentationTargetCommand::CreateRenderPass && error.mResult == VK_SUCCESS &&
                   error.mImageCount == 3);
        ensure("a null successful output creates no framebuffer and needs no rollback destroy",
               state.mFramebufferRecords.empty() && state.mDestroyedRenderPasses.empty());
    }
}

template<>
template<>
void render_vulkan_swapchain_presentation_target_object::test<5>()
{
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        const auto      framebuffers = state.defaultFramebuffers();
        const auto      failed_output = fakeHandle<VkFramebuffer>(0xdead);
        state.mFramebufferResults = { VK_SUCCESS, VK_SUCCESS, VK_ERROR_OUT_OF_DEVICE_MEMORY };
        state.mFramebufferOutputs = { framebuffers[0], framebuffers[1], failed_output };

        const auto result = resolveVulkanSwapchainPresentationTargetGeneration(
            parents.mLogical, parents.mConfiguration, parents.mSwapchain, parents.mImages);
        const auto& error = requireError(result);
        ensure("framebuffer failure preserves the exact command, VkResult, count, and index",
               error.mCode == VulkanSwapchainPresentationTargetResolutionCode::FramebufferCreationFailure &&
                   error.mCommand == VulkanSwapchainPresentationTargetCommand::CreateFramebuffer &&
                   error.mResult == VK_ERROR_OUT_OF_DEVICE_MEMORY && error.mImageCount == 3 && error.mImageIndex == 2);
        ensure("failure rolls back only successful framebuffers in reverse order, then the render pass",
               state.mDestroyedFramebuffers == std::vector<VkFramebuffer>{ framebuffers[1], framebuffers[0] } &&
                   state.mDestroyedRenderPasses == std::vector<VkRenderPass>{ state.mRenderPassOutput } &&
                   state.mTargetDestroyOrder == std::vector<std::string>{ "framebuffer", "framebuffer", "render-pass" });
        ensure("the undefined failed framebuffer output is ignored",
               std::find(state.mDestroyedFramebuffers.begin(), state.mDestroyedFramebuffers.end(), failed_output) ==
                   state.mDestroyedFramebuffers.end());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        const auto      framebuffers = state.defaultFramebuffers();
        state.mFramebufferOutputs = { framebuffers[0], VK_NULL_HANDLE };

        const auto result = resolveVulkanSwapchainPresentationTargetGeneration(
            parents.mLogical, parents.mConfiguration, parents.mSwapchain, parents.mImages);
        const auto& error = requireError(result);
        ensure("success with a null framebuffer is typed at its exact index",
               error.mCode == VulkanSwapchainPresentationTargetResolutionCode::NullFramebufferOnSuccess &&
                   error.mCommand == VulkanSwapchainPresentationTargetCommand::CreateFramebuffer && error.mResult == VK_SUCCESS &&
                   error.mImageCount == 3 && error.mImageIndex == 1);
        ensure("null success rolls back the earlier framebuffer, then the render pass",
               state.mDestroyedFramebuffers == std::vector<VkFramebuffer>{ framebuffers[0] } &&
                   state.mDestroyedRenderPasses == std::vector<VkRenderPass>{ state.mRenderPassOutput } &&
                   state.mTargetDestroyOrder == std::vector<std::string>{ "framebuffer", "render-pass" });
    }
}

template<>
template<>
void render_vulkan_swapchain_presentation_target_object::test<6>()
{
    gAllocationCalls    = 0;
    gFailAllocationCall = 1;
    FakeState       state;
    ScopedFakeState scope(state);
    auto            parents = makeParents(state);

    const auto result = VulkanSwapchainPresentationTargetDetail::resolve(
        parents.mLogical, parents.mConfiguration, parents.mSwapchain, parents.mImages, allocationCheckpoint);
    const auto& error = requireError(result);
    ensure("framebuffer-array allocation failure is typed with the exact command and count",
           error.mCode == VulkanSwapchainPresentationTargetResolutionCode::ScratchAllocationFailure &&
               error.mCommand == VulkanSwapchainPresentationTargetCommand::CreateFramebuffer && error.mImageCount == 3 &&
               gAllocationCalls == 1);
    ensure("all host allocation completes before any Vulkan object creation",
           state.mRenderPassRecords.empty() && state.mFramebufferRecords.empty() && state.mDestroyedFramebuffers.empty() &&
               state.mDestroyedRenderPasses.empty());

    gFailAllocationCall = 0;
    auto retried = takeTarget(VulkanSwapchainPresentationTargetDetail::resolve(
        parents.mLogical, parents.mConfiguration, parents.mSwapchain, parents.mImages, allocationCheckpoint));
    ensure("unchanged parents remain reusable after allocation failure",
           retried.createdFor(parents.mLogical, parents.mConfiguration, parents.mSwapchain, parents.mImages));
    retried.reset();
    gFailAllocationCall = 0;
}

template<>
template<>
void render_vulkan_swapchain_presentation_target_object::test<7>()
{
    FakeState       state;
    ScopedFakeState scope(state);
    auto            parents = makeParents(state);
    auto target = takeTarget(resolveVulkanSwapchainPresentationTargetGeneration(
        parents.mLogical, parents.mConfiguration, parents.mSwapchain, parents.mImages));
    auto moved = std::move(target);

    ensure("move transfers ownership and exact provenance while disarming the source",
           moved.renderPass() == state.mRenderPassOutput && moved.framebufferCount() == 3 &&
               moved.createdFor(parents.mLogical, parents.mConfiguration, parents.mSwapchain, parents.mImages) &&
               target.renderPass() == VK_NULL_HANDLE && target.framebufferCount() == 0 && target.framebuffer(0) == VK_NULL_HANDLE &&
               !target.createdFor(parents.mLogical, parents.mConfiguration, parents.mSwapchain, parents.mImages));

    state.mInstanceLookups.clear();
    state.mDeviceLookups.clear();
    auto second_images = takeImages(
        resolveVulkanSwapchainImagesGeneration(parents.mLogical, parents.mConfiguration, parents.mSwapchain));
    ensure("a distinct live image-view generation is not accepted as the target's parent",
           !moved.createdFor(parents.mLogical, parents.mConfiguration, parents.mSwapchain, second_images));

    moved.reset();
    ensure("only the moved owner destroys each framebuffer and the render pass once",
           state.mDestroyedFramebuffers.size() == 3 && state.mDestroyedRenderPasses.size() == 1);
}

} // namespace tut
