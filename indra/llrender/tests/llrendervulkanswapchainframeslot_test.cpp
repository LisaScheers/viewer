/**
 * @file llrendervulkanswapchainframeslot_test.cpp
 * @brief Tests for loader-neutral Vulkan swapchain frame-slot ownership.
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

#include "llrendervulkanswapchainframeslot.h"
#include "lltut.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
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
    CreateCommandPool,
    DestroyCommandPool,
    AllocateCommandBuffers,
    CreateSemaphore,
    DestroySemaphore,
    CreateFence,
    DestroyFence,
    WaitForFences,
    ResetCommandBuffer,
    BeginCommandBuffer,
    EndCommandBuffer,
    ResetFences,
    QueueSubmit
};

struct FenceWaitRecord
{
    VkDevice      mDevice     = VK_NULL_HANDLE;
    std::uint32_t mFenceCount = 0;
    VkFence       mFence      = VK_NULL_HANDLE;
    VkBool32      mWaitAll    = VK_FALSE;
    std::uint64_t mTimeout    = 0;
};

struct CommandBufferResetRecord
{
    VkCommandBuffer           mCommandBuffer = VK_NULL_HANDLE;
    VkCommandBufferResetFlags mFlags         = 0;
};

struct FenceResetRecord
{
    VkDevice      mDevice     = VK_NULL_HANDLE;
    std::uint32_t mFenceCount = 0;
    VkFence       mFence      = VK_NULL_HANDLE;
};

struct QueueSubmitRecord
{
    VkQueue         mQueue                = VK_NULL_HANDLE;
    std::uint32_t   mSubmitCount          = 0;
    VkFence         mFence                = VK_NULL_HANDLE;
    VkStructureType mStructureType        = VK_STRUCTURE_TYPE_MAX_ENUM;
    bool            mNextNull             = false;
    std::uint32_t   mWaitSemaphoreCount   = 0;
    bool            mWaitSemaphoresNull   = false;
    bool            mWaitStageMasksNull   = false;
    std::uint32_t   mCommandBufferCount   = 0;
    VkCommandBuffer mCommandBuffer        = VK_NULL_HANDLE;
    std::uint32_t   mSignalSemaphoreCount = 0;
    bool            mSignalSemaphoresNull = false;
};

struct FakeState
{
    FakeState()
    {
        mCapabilities.minImageCount           = 2;
        mCapabilities.maxImageCount           = 0;
        mCapabilities.currentExtent           = { std::numeric_limits<std::uint32_t>::max(), std::numeric_limits<std::uint32_t>::max() };
        mCapabilities.minImageExtent          = { 64, 64 };
        mCapabilities.maxImageExtent          = { 4096, 2160 };
        mCapabilities.maxImageArrayLayers     = 1;
        mCapabilities.supportedTransforms     = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        mCapabilities.currentTransform        = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        mCapabilities.supportedCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        mCapabilities.supportedUsageFlags     = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }

    VkInstance       mInstance       = fakeHandle<VkInstance>(0x1000);
    VkSurfaceKHR     mSurface        = fakeHandle<VkSurfaceKHR>(0x2000);
    VkPhysicalDevice mPhysicalDevice = fakeHandle<VkPhysicalDevice>(0x3000);
    VkDevice         mDevice         = fakeHandle<VkDevice>(0x4000);
    VkQueue          mQueue          = fakeHandle<VkQueue>(0x5000);
    VkSwapchainKHR   mSwapchain      = fakeHandle<VkSwapchainKHR>(0x6000);
    std::uint32_t    mQueueFamily    = 2;

    VkSurfaceCapabilitiesKHR          mCapabilities{};
    std::array<VkSurfaceFormatKHR, 1> mFormats{ VkSurfaceFormatKHR{ VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR } };
    std::array<VkPresentModeKHR, 1>   mPresentModes{ VK_PRESENT_MODE_FIFO_KHR };

    MissingCommand           mMissingCommand = MissingCommand::None;
    std::vector<std::string> mInstanceLookups;
    std::vector<std::string> mDeviceLookups;
    bool                     mAllResolvedBeforeMutation = false;

    VkResult        mCommandPoolResult    = VK_SUCCESS;
    VkCommandPool   mCommandPoolOutput    = fakeHandle<VkCommandPool>(0x9000);
    VkResult        mCommandBufferResult  = VK_SUCCESS;
    VkCommandBuffer mCommandBufferOutput  = fakeHandle<VkCommandBuffer>(0x9100);
    VkCommandBuffer mCommandBufferWritten = VK_NULL_HANDLE;
    VkResult        mSemaphoreResult      = VK_SUCCESS;
    VkSemaphore     mSemaphoreOutput      = fakeHandle<VkSemaphore>(0x9200);
    VkResult        mFenceResult          = VK_SUCCESS;
    VkFence         mFenceOutput          = fakeHandle<VkFence>(0x9300);

    std::vector<VkResult> mWaitResults;
    std::size_t           mNextWaitResult           = 0;
    VkResult              mResetCommandBufferResult = VK_SUCCESS;
    VkResult              mBeginCommandBufferResult = VK_SUCCESS;
    VkResult              mEndCommandBufferResult   = VK_SUCCESS;
    VkResult              mResetFencesResult        = VK_SUCCESS;
    VkResult              mQueueSubmitResult        = VK_SUCCESS;

    std::vector<std::string>                 mEvents;
    std::vector<VkDevice>                    mMutationDevices;
    std::vector<VkCommandPoolCreateInfo>     mCommandPoolInfos;
    std::vector<VkCommandBufferAllocateInfo> mCommandBufferInfos;
    std::vector<VkSemaphoreCreateInfo>       mSemaphoreInfos;
    std::vector<VkFenceCreateInfo>           mFenceInfos;
    std::vector<bool>                        mAllocatorNull;
    std::vector<VkCommandPool>               mDestroyedCommandPools;
    std::vector<VkSemaphore>                 mDestroyedSemaphores;
    std::vector<VkFence>                     mDestroyedFences;
    std::vector<FenceWaitRecord>             mFenceWaits;
    std::vector<CommandBufferResetRecord>    mCommandBufferResets;
    std::vector<VkCommandBufferBeginInfo>    mCommandBufferBeginInfos;
    std::vector<VkCommandBuffer>             mEndedCommandBuffers;
    std::vector<FenceResetRecord>            mFenceResets;
    std::vector<QueueSubmitRecord>           mQueueSubmits;

    std::size_t mNextImageView = 0;

    std::array<VkImage, 3> images() const noexcept
    {
        return { fakeHandle<VkImage>(0x7100), fakeHandle<VkImage>(0x7200), fakeHandle<VkImage>(0x7300) };
    }

    void clearFrameRecords()
    {
        mInstanceLookups.clear();
        mDeviceLookups.clear();
        mAllResolvedBeforeMutation = false;
        mEvents.clear();
        mMutationDevices.clear();
        mCommandPoolInfos.clear();
        mCommandBufferInfos.clear();
        mCommandBufferWritten = VK_NULL_HANDLE;
        mSemaphoreInfos.clear();
        mFenceInfos.clear();
        mAllocatorNull.clear();
        mDestroyedCommandPools.clear();
        mDestroyedSemaphores.clear();
        mDestroyedFences.clear();
        mFenceWaits.clear();
        mCommandBufferResets.clear();
        mCommandBufferBeginInfos.clear();
        mEndedCommandBuffers.clear();
        mFenceResets.clear();
        mQueueSubmits.clear();
        mNextWaitResult = 0;
    }
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
    if (gFakeState && physical_device == gFakeState->mPhysicalDevice && properties)
    {
        *properties            = {};
        properties->apiVersion = VK_API_VERSION_1_1;
        std::strncpy(properties->deviceName, "frame-slot-fake", VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 1);
    }
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

VKAPI_ATTR void VKAPI_CALL fakeDestroyDevice(VkDevice, const VkAllocationCallbacks*) noexcept
{
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
    const auto values = gFakeState->images();
    if (!images)
    {
        *count = static_cast<std::uint32_t>(values.size());
        return VK_SUCCESS;
    }
    const std::uint32_t written = std::min(*count, static_cast<std::uint32_t>(values.size()));
    std::copy_n(values.begin(), written, images);
    *count = written;
    return written == values.size() ? VK_SUCCESS : VK_INCOMPLETE;
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
    *image_view = fakeHandle<VkImageView>(0x8100 + gFakeState->mNextImageView * 0x100);
    ++gFakeState->mNextImageView;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyImageView(VkDevice, VkImageView, const VkAllocationCallbacks*) noexcept
{
}

const std::vector<std::string>& expectedDeviceLookups()
{
    static const std::vector<std::string> names{ "vkCreateCommandPool", "vkDestroyCommandPool", "vkAllocateCommandBuffers",
                                                 "vkCreateSemaphore",   "vkDestroySemaphore",   "vkCreateFence",
                                                 "vkDestroyFence" };
    return names;
}

const std::vector<std::string>& expectedExecutionLookups()
{
    static const std::vector<std::string> names{ "vkWaitForFences",    "vkResetCommandBuffer", "vkBeginCommandBuffer",
                                                 "vkEndCommandBuffer", "vkResetFences",        "vkQueueSubmit" };
    return names;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateCommandPool(VkDevice                       device,
                                                     const VkCommandPoolCreateInfo* create_info,
                                                     const VkAllocationCallbacks*   allocator,
                                                     VkCommandPool*                 command_pool) noexcept
{
    if (!gFakeState || !create_info || !command_pool)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    gFakeState->mAllResolvedBeforeMutation = gFakeState->mInstanceLookups == std::vector<std::string>{ "vkGetDeviceProcAddr" } &&
                                             gFakeState->mDeviceLookups == expectedDeviceLookups();
    gFakeState->mEvents.emplace_back("create pool");
    gFakeState->mMutationDevices.push_back(device);
    gFakeState->mCommandPoolInfos.push_back(*create_info);
    gFakeState->mAllocatorNull.push_back(allocator == nullptr);
    *command_pool = gFakeState->mCommandPoolOutput;
    return gFakeState->mCommandPoolResult;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyCommandPool(VkDevice                     device,
                                                  VkCommandPool                command_pool,
                                                  const VkAllocationCallbacks* allocator) noexcept
{
    if (!gFakeState)
    {
        return;
    }
    gFakeState->mEvents.emplace_back("destroy pool");
    gFakeState->mMutationDevices.push_back(device);
    gFakeState->mDestroyedCommandPools.push_back(command_pool);
    gFakeState->mAllocatorNull.push_back(allocator == nullptr);
}

VKAPI_ATTR VkResult VKAPI_CALL fakeAllocateCommandBuffers(VkDevice                           device,
                                                          const VkCommandBufferAllocateInfo* allocate_info,
                                                          VkCommandBuffer*                   command_buffer) noexcept
{
    if (!gFakeState || !allocate_info || !command_buffer)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    gFakeState->mEvents.emplace_back("allocate buffer");
    gFakeState->mMutationDevices.push_back(device);
    gFakeState->mCommandBufferInfos.push_back(*allocate_info);
    *command_buffer                   = gFakeState->mCommandBufferResult == VK_SUCCESS ? gFakeState->mCommandBufferOutput : VK_NULL_HANDLE;
    gFakeState->mCommandBufferWritten = *command_buffer;
    return gFakeState->mCommandBufferResult;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateSemaphore(VkDevice                     device,
                                                   const VkSemaphoreCreateInfo* create_info,
                                                   const VkAllocationCallbacks* allocator,
                                                   VkSemaphore*                 semaphore) noexcept
{
    if (!gFakeState || !create_info || !semaphore)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    gFakeState->mEvents.emplace_back("create semaphore");
    gFakeState->mMutationDevices.push_back(device);
    gFakeState->mSemaphoreInfos.push_back(*create_info);
    gFakeState->mAllocatorNull.push_back(allocator == nullptr);
    *semaphore = gFakeState->mSemaphoreOutput;
    return gFakeState->mSemaphoreResult;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroySemaphore(VkDevice device, VkSemaphore semaphore, const VkAllocationCallbacks* allocator) noexcept
{
    if (!gFakeState)
    {
        return;
    }
    gFakeState->mEvents.emplace_back("destroy semaphore");
    gFakeState->mMutationDevices.push_back(device);
    gFakeState->mDestroyedSemaphores.push_back(semaphore);
    gFakeState->mAllocatorNull.push_back(allocator == nullptr);
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateFence(VkDevice                     device,
                                               const VkFenceCreateInfo*     create_info,
                                               const VkAllocationCallbacks* allocator,
                                               VkFence*                     fence) noexcept
{
    if (!gFakeState || !create_info || !fence)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    gFakeState->mEvents.emplace_back("create fence");
    gFakeState->mMutationDevices.push_back(device);
    gFakeState->mFenceInfos.push_back(*create_info);
    gFakeState->mAllocatorNull.push_back(allocator == nullptr);
    *fence = gFakeState->mFenceOutput;
    return gFakeState->mFenceResult;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyFence(VkDevice device, VkFence fence, const VkAllocationCallbacks* allocator) noexcept
{
    if (!gFakeState)
    {
        return;
    }
    gFakeState->mEvents.emplace_back("destroy fence");
    gFakeState->mMutationDevices.push_back(device);
    gFakeState->mDestroyedFences.push_back(fence);
    gFakeState->mAllocatorNull.push_back(allocator == nullptr);
}

VKAPI_ATTR VkResult VKAPI_CALL fakeWaitForFences(VkDevice device, std::uint32_t fence_count, const VkFence* fences, VkBool32 wait_all,
                                                 std::uint64_t timeout) noexcept
{
    if (!gFakeState || !fences || fence_count == 0)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    gFakeState->mEvents.emplace_back("wait fence");
    gFakeState->mFenceWaits.push_back({ device, fence_count, fences[0], wait_all, timeout });
    if (gFakeState->mNextWaitResult < gFakeState->mWaitResults.size())
    {
        return gFakeState->mWaitResults[gFakeState->mNextWaitResult++];
    }
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeResetCommandBuffer(VkCommandBuffer command_buffer, VkCommandBufferResetFlags flags) noexcept
{
    if (!gFakeState)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    gFakeState->mEvents.emplace_back("reset buffer");
    gFakeState->mCommandBufferResets.push_back({ command_buffer, flags });
    return gFakeState->mResetCommandBufferResult;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeBeginCommandBuffer(VkCommandBuffer command_buffer, const VkCommandBufferBeginInfo* begin_info) noexcept
{
    if (!gFakeState || !begin_info)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    gFakeState->mEvents.emplace_back("begin buffer");
    gFakeState->mCommandBufferBeginInfos.push_back(*begin_info);
    if (command_buffer != gFakeState->mCommandBufferOutput)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    return gFakeState->mBeginCommandBufferResult;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeEndCommandBuffer(VkCommandBuffer command_buffer) noexcept
{
    if (!gFakeState)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    gFakeState->mEvents.emplace_back("end buffer");
    gFakeState->mEndedCommandBuffers.push_back(command_buffer);
    return gFakeState->mEndCommandBufferResult;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeResetFences(VkDevice device, std::uint32_t fence_count, const VkFence* fences) noexcept
{
    if (!gFakeState || !fences || fence_count == 0)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    gFakeState->mEvents.emplace_back("reset fence");
    gFakeState->mFenceResets.push_back({ device, fence_count, fences[0] });
    return gFakeState->mResetFencesResult;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeQueueSubmit(VkQueue             queue,
                                               std::uint32_t       submit_count,
                                               const VkSubmitInfo* submits,
                                               VkFence             fence) noexcept
{
    if (!gFakeState || !submits || submit_count == 0)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    const VkSubmitInfo& submit = submits[0];
    gFakeState->mEvents.emplace_back("queue submit");
    gFakeState->mQueueSubmits.push_back(
        { queue, submit_count, fence, submit.sType, submit.pNext == nullptr, submit.waitSemaphoreCount, submit.pWaitSemaphores == nullptr,
          submit.pWaitDstStageMask == nullptr, submit.commandBufferCount,
          submit.commandBufferCount != 0 && submit.pCommandBuffers ? submit.pCommandBuffers[0] : VK_NULL_HANDLE,
          submit.signalSemaphoreCount, submit.pSignalSemaphores == nullptr });
    return gFakeState->mQueueSubmitResult;
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
    if (std::strcmp(name, "vkCreateCommandPool") == 0)
        return gFakeState->mMissingCommand == MissingCommand::CreateCommandPool ? nullptr : eraseFunctionType(fakeCreateCommandPool);
    if (std::strcmp(name, "vkDestroyCommandPool") == 0)
        return gFakeState->mMissingCommand == MissingCommand::DestroyCommandPool ? nullptr : eraseFunctionType(fakeDestroyCommandPool);
    if (std::strcmp(name, "vkAllocateCommandBuffers") == 0)
        return gFakeState->mMissingCommand == MissingCommand::AllocateCommandBuffers ? nullptr
                                                                                     : eraseFunctionType(fakeAllocateCommandBuffers);
    if (std::strcmp(name, "vkCreateSemaphore") == 0)
        return gFakeState->mMissingCommand == MissingCommand::CreateSemaphore ? nullptr : eraseFunctionType(fakeCreateSemaphore);
    if (std::strcmp(name, "vkDestroySemaphore") == 0)
        return gFakeState->mMissingCommand == MissingCommand::DestroySemaphore ? nullptr : eraseFunctionType(fakeDestroySemaphore);
    if (std::strcmp(name, "vkCreateFence") == 0)
        return gFakeState->mMissingCommand == MissingCommand::CreateFence ? nullptr : eraseFunctionType(fakeCreateFence);
    if (std::strcmp(name, "vkDestroyFence") == 0)
        return gFakeState->mMissingCommand == MissingCommand::DestroyFence ? nullptr : eraseFunctionType(fakeDestroyFence);
    if (std::strcmp(name, "vkWaitForFences") == 0)
        return gFakeState->mMissingCommand == MissingCommand::WaitForFences ? nullptr : eraseFunctionType(fakeWaitForFences);
    if (std::strcmp(name, "vkResetCommandBuffer") == 0)
        return gFakeState->mMissingCommand == MissingCommand::ResetCommandBuffer ? nullptr : eraseFunctionType(fakeResetCommandBuffer);
    if (std::strcmp(name, "vkBeginCommandBuffer") == 0)
        return gFakeState->mMissingCommand == MissingCommand::BeginCommandBuffer ? nullptr : eraseFunctionType(fakeBeginCommandBuffer);
    if (std::strcmp(name, "vkEndCommandBuffer") == 0)
        return gFakeState->mMissingCommand == MissingCommand::EndCommandBuffer ? nullptr : eraseFunctionType(fakeEndCommandBuffer);
    if (std::strcmp(name, "vkResetFences") == 0)
        return gFakeState->mMissingCommand == MissingCommand::ResetFences ? nullptr : eraseFunctionType(fakeResetFences);
    if (std::strcmp(name, "vkQueueSubmit") == 0)
        return gFakeState->mMissingCommand == MissingCommand::QueueSubmit ? nullptr : eraseFunctionType(fakeQueueSubmit);
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

    state.clearFrameRecords();
    return { std::move(physical), std::move(logical), std::move(configuration), std::move(swapchain), std::move(images) };
}

const VulkanSwapchainFrameSlotResolutionError& requireError(const VulkanSwapchainFrameSlotResolutionResult& result)
{
    const auto* error = std::get_if<VulkanSwapchainFrameSlotResolutionError>(&result);
    tut::ensure("frame-slot resolution returns an error", error != nullptr);
    return *error;
}

void ensureCode(const VulkanSwapchainFrameSlotResolutionResult& result, VulkanSwapchainFrameSlotResolutionCode code)
{
    tut::ensure("the exact frame-slot error is reported", requireError(result).mCode == code);
}

VulkanSwapchainFrameSlotGeneration takeGeneration(VulkanSwapchainFrameSlotResolutionResult&& result)
{
    tut::ensure("frame-slot resolution returns a generation", std::holds_alternative<VulkanSwapchainFrameSlotGeneration>(result));
    return std::get<VulkanSwapchainFrameSlotGeneration>(std::move(result));
}

VulkanSwapchainFrameSlotResolutionResult resolveSlot(Parents& parents)
{
    return resolveVulkanSwapchainFrameSlotGeneration(parents.mLogical, parents.mConfiguration, parents.mSwapchain, parents.mImages);
}

void ensureOnlyExactDevice(const FakeState& state)
{
    tut::ensure("every frame-slot mutation uses the exact logical device",
                std::all_of(state.mMutationDevices.begin(), state.mMutationDevices.end(),
                            [&](VkDevice device) { return device == state.mDevice; }));
}

const VulkanSwapchainFrameSlotOperationError& requireOperationError(const VulkanSwapchainFrameSlotOperationResult& result)
{
    const auto* error = std::get_if<VulkanSwapchainFrameSlotOperationError>(&result);
    tut::ensure("frame-slot operation returns an error", error != nullptr);
    return *error;
}

void ensureOperationSuccess(const VulkanSwapchainFrameSlotOperationResult& result, VulkanSwapchainFrameSlotDisposition disposition)
{
    const auto* value = std::get_if<VulkanSwapchainFrameSlotDisposition>(&result);
    tut::ensure("frame-slot operation returns its exact disposition", value && *value == disposition);
}

void ensureOperationError(const VulkanSwapchainFrameSlotOperationResult& result,
                          VulkanSwapchainFrameSlotOperationCode          code,
                          VulkanSwapchainFrameSlotDisposition            disposition,
                          std::optional<VulkanSwapchainFrameSlotCommand> command   = std::nullopt,
                          VkResult                                       vk_result = VK_SUCCESS)
{
    const auto& error = requireOperationError(result);
    tut::ensure("frame-slot operation reports its exact typed failure",
                error.mCode == code && error.mCommand == command && error.mResult == vk_result && error.mDisposition == disposition);
}

VulkanSwapchainFrameSlotOperationResult resolveExecution(VulkanSwapchainFrameSlotGeneration& generation, Parents& parents)
{
    return generation.resolveEmptySubmissionDispatch(parents.mLogical, parents.mConfiguration, parents.mSwapchain, parents.mImages);
}

} // namespace

namespace tut
{

struct render_vulkan_swapchain_frame_slot_test
{
};

using render_vulkan_swapchain_frame_slot_group  = test_group<render_vulkan_swapchain_frame_slot_test>;
using render_vulkan_swapchain_frame_slot_object = render_vulkan_swapchain_frame_slot_group::object;
render_vulkan_swapchain_frame_slot_group render_vulkan_swapchain_frame_slot_tests("render Vulkan swapchain frame slot");

template<>
template<>
void render_vulkan_swapchain_frame_slot_object::test<1>()
{
    static_assert(!std::is_default_constructible_v<VulkanSwapchainFrameSlotGeneration>);
    static_assert(!std::is_copy_constructible_v<VulkanSwapchainFrameSlotGeneration>);
    static_assert(!std::is_copy_assignable_v<VulkanSwapchainFrameSlotGeneration>);
    static_assert(std::is_nothrow_move_constructible_v<VulkanSwapchainFrameSlotGeneration>);
    static_assert(!std::is_move_assignable_v<VulkanSwapchainFrameSlotGeneration>);
    static_assert(std::is_nothrow_destructible_v<VulkanSwapchainFrameSlotGeneration>);
    static_assert(std::variant_size_v<VulkanSwapchainFrameSlotResolutionResult> == 2);
    static_assert(std::variant_size_v<VulkanSwapchainFrameSlotOperationResult> == 2);
    static_assert(noexcept(resolveVulkanSwapchainFrameSlotGeneration(
        std::declval<const VulkanLogicalDeviceGeneration&>(), std::declval<const VulkanSwapchainConfigurationGeneration&>(),
        std::declval<const VulkanSwapchainGeneration&>(), std::declval<const VulkanSwapchainImagesGeneration&>())));
    static_assert(noexcept(std::declval<VulkanSwapchainFrameSlotGeneration&>().resolveEmptySubmissionDispatch(
        std::declval<const VulkanLogicalDeviceGeneration&>(), std::declval<const VulkanSwapchainConfigurationGeneration&>(),
        std::declval<const VulkanSwapchainGeneration&>(), std::declval<const VulkanSwapchainImagesGeneration&>())));
    static_assert(noexcept(std::declval<VulkanSwapchainFrameSlotGeneration&>().executeEmptySubmission()));
    static_assert(noexcept(std::declval<VulkanSwapchainFrameSlotGeneration&>().retryEmptySubmissionCompletion()));

    const VulkanSwapchainFrameSlotResolutionError value{ VulkanSwapchainFrameSlotResolutionCode::SubmissionFenceCreationFailure,
                                                         VulkanSwapchainFrameSlotCommand::CreateFence, VK_ERROR_OUT_OF_DEVICE_MEMORY };
    ensure("identical typed errors compare equal", value == value);
    const VulkanSwapchainFrameSlotOperationError operation_value{ VulkanSwapchainFrameSlotOperationCode::CommandFailure,
                                                                  VulkanSwapchainFrameSlotCommand::QueueSubmit, VK_ERROR_UNKNOWN,
                                                                  VulkanSwapchainFrameSlotDisposition::ResetRequired };
    ensure("identical operation errors compare equal", operation_value == operation_value);

    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        auto            logical = std::move(parents.mLogical);
        ensureCode(resolveSlot(parents), VulkanSwapchainFrameSlotResolutionCode::InvalidLogicalDeviceGeneration);
        ensure("invalid logical provenance stops before dispatch", state.mInstanceLookups.empty() && state.mDeviceLookups.empty());
        parents.mImages.reset();
        parents.mSwapchain.reset();
        ensure("the moved logical generation remains live", logical.device() == state.mDevice);
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents       = makeParents(state);
        auto            configuration = std::move(parents.mConfiguration);
        ensureCode(resolveSlot(parents), VulkanSwapchainFrameSlotResolutionCode::InvalidSwapchainConfigurationGeneration);
        ensure("invalid configuration provenance stops before dispatch", state.mInstanceLookups.empty() && state.mDeviceLookups.empty());
        parents.mImages.reset();
        parents.mSwapchain.reset();
        ensure("the moved configuration generation remains live", configuration.device() == state.mDevice);
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents   = makeParents(state);
        auto            swapchain = std::move(parents.mSwapchain);
        ensureCode(resolveSlot(parents), VulkanSwapchainFrameSlotResolutionCode::InvalidSwapchainGeneration);
        ensure("invalid swapchain provenance stops before dispatch", state.mInstanceLookups.empty() && state.mDeviceLookups.empty());
        parents.mImages.reset();
        ensure("the moved swapchain generation remains live", swapchain.swapchain() == state.mSwapchain);
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        auto            images  = std::move(parents.mImages);
        ensureCode(resolveSlot(parents), VulkanSwapchainFrameSlotResolutionCode::InvalidSwapchainImagesGeneration);
        ensure("invalid images provenance stops before dispatch", state.mInstanceLookups.empty() && state.mDeviceLookups.empty());
        ensure("the moved images generation remains live", images.imageCount() == 3);
    }
}

template<>
template<>
void render_vulkan_swapchain_frame_slot_object::test<2>()
{
    constexpr std::array cases{ std::pair{ MissingCommand::GetDeviceProcAddr, VulkanSwapchainFrameSlotCommand::GetDeviceProcAddr },
                                std::pair{ MissingCommand::CreateCommandPool, VulkanSwapchainFrameSlotCommand::CreateCommandPool },
                                std::pair{ MissingCommand::DestroyCommandPool, VulkanSwapchainFrameSlotCommand::DestroyCommandPool },
                                std::pair{ MissingCommand::AllocateCommandBuffers,
                                           VulkanSwapchainFrameSlotCommand::AllocateCommandBuffers },
                                std::pair{ MissingCommand::CreateSemaphore, VulkanSwapchainFrameSlotCommand::CreateSemaphore },
                                std::pair{ MissingCommand::DestroySemaphore, VulkanSwapchainFrameSlotCommand::DestroySemaphore },
                                std::pair{ MissingCommand::CreateFence, VulkanSwapchainFrameSlotCommand::CreateFence },
                                std::pair{ MissingCommand::DestroyFence, VulkanSwapchainFrameSlotCommand::DestroyFence } };

    for (std::size_t index = 0; index < cases.size(); ++index)
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        state.mMissingCommand   = cases[index].first;
        const auto  result      = resolveSlot(parents);
        const auto& error       = requireError(result);
        ensure("missing dispatch reports its exact typed command",
               error.mCode == VulkanSwapchainFrameSlotResolutionCode::MissingRequiredCommand && error.mCommand == cases[index].second &&
                   error.mResult == VK_SUCCESS);
        ensure_equals("missing dispatch has the exact instance cutoff", state.mInstanceLookups.size(), std::size_t{ 1 });
        ensure_equals("missing dispatch has the exact device cutoff", state.mDeviceLookups.size(), index);
        ensure("all dispatch resolves before mutation", state.mEvents.empty());
    }
}

template<>
template<>
void render_vulkan_swapchain_frame_slot_object::test<3>()
{
    FakeState       state;
    ScopedFakeState scope(state);
    auto            parents    = makeParents(state);
    auto            generation = takeGeneration(resolveSlot(parents));

    ensure("dispatch resolves through exact parent handles and in exact order",
           state.mInstanceLookups == std::vector<std::string>{ "vkGetDeviceProcAddr" } && state.mDeviceLookups == expectedDeviceLookups() &&
               state.mAllResolvedBeforeMutation);
    ensure("resources are created in dependency order",
           state.mEvents == std::vector<std::string>{ "create pool", "allocate buffer", "create semaphore", "create fence" });
    ensureOnlyExactDevice(state);
    ensure("creation exposes all four exact owned handles",
           generation.commandPool() == state.mCommandPoolOutput && generation.commandBuffer() == state.mCommandBufferOutput &&
               generation.imageAvailableSemaphore() == state.mSemaphoreOutput && generation.submissionFence() == state.mFenceOutput);
    ensure("the generation authenticates its exact four parents",
           generation.createdFor(parents.mLogical, parents.mConfiguration, parents.mSwapchain, parents.mImages));
    ensure("command pool creation is zero-linked, resettable, and bound to the exact queue family",
           state.mCommandPoolInfos.size() == 1 && state.mCommandPoolInfos[0].sType == VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO &&
               state.mCommandPoolInfos[0].pNext == nullptr &&
               state.mCommandPoolInfos[0].flags == VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT &&
               state.mCommandPoolInfos[0].queueFamilyIndex == state.mQueueFamily);
    ensure("exactly one primary command buffer is allocated from the owned pool",
           state.mCommandBufferInfos.size() == 1 && state.mCommandBufferInfos[0].sType == VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO &&
               state.mCommandBufferInfos[0].pNext == nullptr && state.mCommandBufferInfos[0].commandPool == state.mCommandPoolOutput &&
               state.mCommandBufferInfos[0].level == VK_COMMAND_BUFFER_LEVEL_PRIMARY &&
               state.mCommandBufferInfos[0].commandBufferCount == 1);
    ensure("the image-available semaphore is a zero-flag binary semaphore",
           state.mSemaphoreInfos.size() == 1 && state.mSemaphoreInfos[0].sType == VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO &&
               state.mSemaphoreInfos[0].pNext == nullptr && state.mSemaphoreInfos[0].flags == 0);
    ensure("the submission fence starts signaled",
           state.mFenceInfos.size() == 1 && state.mFenceInfos[0].sType == VK_STRUCTURE_TYPE_FENCE_CREATE_INFO &&
               state.mFenceInfos[0].pNext == nullptr && state.mFenceInfos[0].flags == VK_FENCE_CREATE_SIGNALED_BIT);
    ensure("all create and destroy-capable calls use null allocation callbacks",
           state.mAllocatorNull == std::vector<bool>{ true, true, true });

    generation.reset();
    ensure("reset destroys in reverse dependency order and pool destruction implicitly frees the buffer",
           state.mEvents == std::vector<std::string>{ "create pool", "allocate buffer", "create semaphore", "create fence", "destroy fence",
                                                      "destroy semaphore", "destroy pool" } &&
               state.mDestroyedFences == std::vector<VkFence>{ state.mFenceOutput } &&
               state.mDestroyedSemaphores == std::vector<VkSemaphore>{ state.mSemaphoreOutput } &&
               state.mDestroyedCommandPools == std::vector<VkCommandPool>{ state.mCommandPoolOutput });
    ensureOnlyExactDevice(state);
    ensure("reset clears ownership and provenance",
           generation.commandPool() == VK_NULL_HANDLE && generation.commandBuffer() == VK_NULL_HANDLE &&
               generation.imageAvailableSemaphore() == VK_NULL_HANDLE && generation.submissionFence() == VK_NULL_HANDLE &&
               !generation.createdFor(parents.mLogical, parents.mConfiguration, parents.mSwapchain, parents.mImages));
    generation.reset();
    ensure_equals("reset is idempotent", state.mEvents.size(), std::size_t{ 7 });
}

template<>
template<>
void render_vulkan_swapchain_frame_slot_object::test<4>()
{
    for (bool success_null : { false, true })
    {
        FakeState           state;
        ScopedFakeState     scope(state);
        auto                parents  = makeParents(state);
        const VkCommandPool poisoned = fakeHandle<VkCommandPool>(0xdead);
        state.mCommandPoolResult     = success_null ? VK_SUCCESS : VK_ERROR_OUT_OF_HOST_MEMORY;
        state.mCommandPoolOutput     = success_null ? VK_NULL_HANDLE : poisoned;

        const auto  result = resolveSlot(parents);
        const auto& error  = requireError(result);
        ensure("pool failure and success-null have distinct exact types",
               error.mCode == (success_null ? VulkanSwapchainFrameSlotResolutionCode::NullCommandPoolOnSuccess
                                            : VulkanSwapchainFrameSlotResolutionCode::CommandPoolCreationFailure) &&
                   error.mCommand == VulkanSwapchainFrameSlotCommand::CreateCommandPool &&
                   error.mResult == (success_null ? VK_SUCCESS : VK_ERROR_OUT_OF_HOST_MEMORY));
        ensure("failed or null pool creation owns and destroys no output",
               state.mEvents == std::vector<std::string>{ "create pool" } && state.mDestroyedCommandPools.empty() &&
                   state.mDestroyedSemaphores.empty() && state.mDestroyedFences.empty() && (success_null || poisoned != VK_NULL_HANDLE));
    }
}

template<>
template<>
void render_vulkan_swapchain_frame_slot_object::test<5>()
{
    for (bool success_null : { false, true })
    {
        FakeState             state;
        ScopedFakeState       scope(state);
        auto                  parents  = makeParents(state);
        const VkCommandBuffer poisoned = fakeHandle<VkCommandBuffer>(0xdead);
        state.mCommandBufferResult     = success_null ? VK_SUCCESS : VK_ERROR_OUT_OF_DEVICE_MEMORY;
        state.mCommandBufferOutput     = success_null ? VK_NULL_HANDLE : poisoned;

        const auto  result = resolveSlot(parents);
        const auto& error  = requireError(result);
        ensure("buffer failure and success-null have distinct exact types",
               error.mCode == (success_null ? VulkanSwapchainFrameSlotResolutionCode::NullCommandBufferOnSuccess
                                            : VulkanSwapchainFrameSlotResolutionCode::CommandBufferAllocationFailure) &&
                   error.mCommand == VulkanSwapchainFrameSlotCommand::AllocateCommandBuffers &&
                   error.mResult == (success_null ? VK_SUCCESS : VK_ERROR_OUT_OF_DEVICE_MEMORY));
        ensure("buffer failure writes a null output and rolls back only its successful pool",
               state.mEvents == std::vector<std::string>{ "create pool", "allocate buffer", "destroy pool" } &&
                   state.mDestroyedCommandPools == std::vector<VkCommandPool>{ state.mCommandPoolOutput } &&
                   state.mDestroyedSemaphores.empty() && state.mDestroyedFences.empty() && state.mCommandBufferWritten == VK_NULL_HANDLE &&
                   (success_null || poisoned != VK_NULL_HANDLE));
    }
}

template<>
template<>
void render_vulkan_swapchain_frame_slot_object::test<6>()
{
    for (bool success_null : { false, true })
    {
        FakeState         state;
        ScopedFakeState   scope(state);
        auto              parents  = makeParents(state);
        const VkSemaphore poisoned = fakeHandle<VkSemaphore>(0xdead);
        state.mSemaphoreResult     = success_null ? VK_SUCCESS : VK_ERROR_OUT_OF_HOST_MEMORY;
        state.mSemaphoreOutput     = success_null ? VK_NULL_HANDLE : poisoned;

        const auto  result = resolveSlot(parents);
        const auto& error  = requireError(result);
        ensure("semaphore failure and success-null have distinct exact types",
               error.mCode == (success_null ? VulkanSwapchainFrameSlotResolutionCode::NullImageAvailableSemaphoreOnSuccess
                                            : VulkanSwapchainFrameSlotResolutionCode::ImageAvailableSemaphoreCreationFailure) &&
                   error.mCommand == VulkanSwapchainFrameSlotCommand::CreateSemaphore &&
                   error.mResult == (success_null ? VK_SUCCESS : VK_ERROR_OUT_OF_HOST_MEMORY));
        ensure("semaphore failure never destroys undefined output and rolls back only the pool",
               state.mEvents == std::vector<std::string>{ "create pool", "allocate buffer", "create semaphore", "destroy pool" } &&
                   state.mDestroyedCommandPools == std::vector<VkCommandPool>{ state.mCommandPoolOutput } &&
                   state.mDestroyedSemaphores.empty() && state.mDestroyedFences.empty() && (success_null || poisoned != VK_NULL_HANDLE));
    }
}

template<>
template<>
void render_vulkan_swapchain_frame_slot_object::test<7>()
{
    for (bool success_null : { false, true })
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents  = makeParents(state);
        const VkFence   poisoned = fakeHandle<VkFence>(0xdead);
        state.mFenceResult       = success_null ? VK_SUCCESS : VK_ERROR_OUT_OF_DEVICE_MEMORY;
        state.mFenceOutput       = success_null ? VK_NULL_HANDLE : poisoned;

        const auto  result = resolveSlot(parents);
        const auto& error  = requireError(result);
        ensure("fence failure and success-null have distinct exact types",
               error.mCode == (success_null ? VulkanSwapchainFrameSlotResolutionCode::NullSubmissionFenceOnSuccess
                                            : VulkanSwapchainFrameSlotResolutionCode::SubmissionFenceCreationFailure) &&
                   error.mCommand == VulkanSwapchainFrameSlotCommand::CreateFence &&
                   error.mResult == (success_null ? VK_SUCCESS : VK_ERROR_OUT_OF_DEVICE_MEMORY));
        ensure("fence failure never destroys undefined output and rolls back semaphore then pool",
               state.mEvents == std::vector<std::string>{ "create pool", "allocate buffer", "create semaphore", "create fence",
                                                          "destroy semaphore", "destroy pool" } &&
                   state.mDestroyedCommandPools == std::vector<VkCommandPool>{ state.mCommandPoolOutput } &&
                   state.mDestroyedSemaphores == std::vector<VkSemaphore>{ state.mSemaphoreOutput } && state.mDestroyedFences.empty() &&
                   (success_null || poisoned != VK_NULL_HANDLE));
    }
}

template<>
template<>
void render_vulkan_swapchain_frame_slot_object::test<8>()
{
    FakeState       state;
    ScopedFakeState scope(state);
    auto            parents    = makeParents(state);
    auto            generation = takeGeneration(resolveSlot(parents));
    auto            moved      = std::move(generation);

    ensure("move transfers all ownership and disarms the source",
           moved.createdFor(parents.mLogical, parents.mConfiguration, parents.mSwapchain, parents.mImages) &&
               generation.commandPool() == VK_NULL_HANDLE && generation.commandBuffer() == VK_NULL_HANDLE &&
               generation.imageAvailableSemaphore() == VK_NULL_HANDLE && generation.submissionFence() == VK_NULL_HANDLE &&
               !generation.createdFor(parents.mLogical, parents.mConfiguration, parents.mSwapchain, parents.mImages));

    auto second_images_result = resolveVulkanSwapchainImagesGeneration(parents.mLogical, parents.mConfiguration, parents.mSwapchain);
    ensure("a replacement images fixture resolves", std::holds_alternative<VulkanSwapchainImagesGeneration>(second_images_result));
    auto second_images = std::get<VulkanSwapchainImagesGeneration>(std::move(second_images_result));
    ensure("equal image values from a distinct Stage42 generation do not authenticate",
           second_images.imageCount() == parents.mImages.imageCount() &&
               !moved.createdFor(parents.mLogical, parents.mConfiguration, parents.mSwapchain, second_images));

    state.clearFrameRecords();
    moved.reset();
    moved.reset();
    generation.reset();
    ensure("only the moved owner destroys each resource once",
           state.mEvents == std::vector<std::string>{ "destroy fence", "destroy semaphore", "destroy pool" } &&
               state.mDestroyedFences.size() == 1 && state.mDestroyedSemaphores.size() == 1 && state.mDestroyedCommandPools.size() == 1);
}

template<>
template<>
void render_vulkan_swapchain_frame_slot_object::test<9>()
{
    constexpr std::array cases{
        std::pair{ MissingCommand::GetDeviceProcAddr, VulkanSwapchainFrameSlotCommand::GetDeviceProcAddr },
        std::pair{ MissingCommand::WaitForFences, VulkanSwapchainFrameSlotCommand::WaitForFences },
        std::pair{ MissingCommand::ResetCommandBuffer, VulkanSwapchainFrameSlotCommand::ResetCommandBuffer },
        std::pair{ MissingCommand::BeginCommandBuffer, VulkanSwapchainFrameSlotCommand::BeginCommandBuffer },
        std::pair{ MissingCommand::EndCommandBuffer, VulkanSwapchainFrameSlotCommand::EndCommandBuffer },
        std::pair{ MissingCommand::ResetFences, VulkanSwapchainFrameSlotCommand::ResetFences },
        std::pair{ MissingCommand::QueueSubmit, VulkanSwapchainFrameSlotCommand::QueueSubmit },
    };

    for (std::size_t index = 0; index < cases.size(); ++index)
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents    = makeParents(state);
        auto            generation = takeGeneration(resolveSlot(parents));
        state.clearFrameRecords();
        state.mMissingCommand = cases[index].first;

        ensureOperationError(resolveExecution(generation, parents), VulkanSwapchainFrameSlotOperationCode::MissingRequiredCommand,
                             VulkanSwapchainFrameSlotDisposition::Reusable, cases[index].second);
        ensure("failed execution dispatch resolution has the exact lookup cutoff",
               state.mInstanceLookups == std::vector<std::string>{ "vkGetDeviceProcAddr" } && state.mDeviceLookups.size() == index);
        ensure("dispatch resolution mutates no Vulkan object", state.mEvents.empty());
        ensureOperationError(generation.executeEmptySubmission(), VulkanSwapchainFrameSlotOperationCode::MissingRequiredCommand,
                             VulkanSwapchainFrameSlotDisposition::Reusable, VulkanSwapchainFrameSlotCommand::WaitForFences);
        ensure("failed resolution publishes none of the execution dispatch", state.mEvents.empty());

        state.mMissingCommand = MissingCommand::None;
        state.mInstanceLookups.clear();
        state.mDeviceLookups.clear();
        ensureOperationSuccess(resolveExecution(generation, parents), VulkanSwapchainFrameSlotDisposition::Reusable);
        ensure("successful execution dispatch resolution uses the exact retained parents and command order",
               state.mInstanceLookups == std::vector<std::string>{ "vkGetDeviceProcAddr" } &&
                   state.mDeviceLookups == expectedExecutionLookups());
        const auto instance_lookups = state.mInstanceLookups;
        const auto device_lookups   = state.mDeviceLookups;
        state.mMissingCommand       = MissingCommand::WaitForFences;
        ensureOperationSuccess(resolveExecution(generation, parents), VulkanSwapchainFrameSlotDisposition::Reusable);
        ensure("published execution dispatch is idempotent without another lookup",
               state.mInstanceLookups == instance_lookups && state.mDeviceLookups == device_lookups);
    }
}

template<>
template<>
void render_vulkan_swapchain_frame_slot_object::test<10>()
{
    FakeState         state;
    ScopedFakeState   scope(state);
    auto              parents             = makeParents(state);
    auto              generation          = takeGeneration(resolveSlot(parents));
    const VkSemaphore untouched_semaphore = generation.imageAvailableSemaphore();
    state.clearFrameRecords();
    ensureOperationSuccess(resolveExecution(generation, parents), VulkanSwapchainFrameSlotDisposition::Reusable);
    state.clearFrameRecords();

    ensureOperationSuccess(generation.executeEmptySubmission(), VulkanSwapchainFrameSlotDisposition::Reusable);
    ensureOperationSuccess(generation.executeEmptySubmission(), VulkanSwapchainFrameSlotDisposition::Reusable);

    const std::vector<std::string> one_cycle{ "wait fence",  "reset buffer", "begin buffer", "end buffer",
                                              "reset fence", "queue submit", "wait fence" };
    auto                           expected_events = one_cycle;
    expected_events.insert(expected_events.end(), one_cycle.begin(), one_cycle.end());
    ensure("two cycles use the exact command order and no create, destroy, or semaphore call", state.mEvents == expected_events);
    ensure("two cycles leave the slot reusable and the Stage 43 semaphore untouched",
           generation.disposition() == VulkanSwapchainFrameSlotDisposition::Reusable &&
               generation.imageAvailableSemaphore() == untouched_semaphore);

    ensure_equals("two cycles issue four waits", state.mFenceWaits.size(), std::size_t{ 4 });
    ensure("every wait uses the exact device, fence, wait-all value, and infinite timeout",
           std::all_of(state.mFenceWaits.begin(), state.mFenceWaits.end(),
                       [&](const FenceWaitRecord& record)
                       {
                           return record.mDevice == state.mDevice && record.mFenceCount == 1 && record.mFence == state.mFenceOutput &&
                                  record.mWaitAll == VK_TRUE && record.mTimeout == std::numeric_limits<std::uint64_t>::max();
                       }));
    ensure("each reset targets the one primary buffer with zero flags",
           state.mCommandBufferResets.size() == 2 &&
               std::all_of(state.mCommandBufferResets.begin(), state.mCommandBufferResets.end(), [&](const CommandBufferResetRecord& record)
                           { return record.mCommandBuffer == state.mCommandBufferOutput && record.mFlags == 0; }));
    ensure("each begin uses an exact zeroed primary-buffer begin contract",
           state.mCommandBufferBeginInfos.size() == 2 &&
               std::all_of(state.mCommandBufferBeginInfos.begin(), state.mCommandBufferBeginInfos.end(),
                           [](const VkCommandBufferBeginInfo& info)
                           {
                               return info.sType == VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO && info.pNext == nullptr &&
                                      info.flags == 0 && info.pInheritanceInfo == nullptr;
                           }));
    ensure("each empty recording ends the exact command buffer",
           state.mEndedCommandBuffers == std::vector<VkCommandBuffer>{ state.mCommandBufferOutput, state.mCommandBufferOutput });
    ensure("each fence reset targets only the exact submission fence",
           state.mFenceResets.size() == 2 &&
               std::all_of(state.mFenceResets.begin(), state.mFenceResets.end(), [&](const FenceResetRecord& record)
                           { return record.mDevice == state.mDevice && record.mFenceCount == 1 && record.mFence == state.mFenceOutput; }));
    ensure("each queue submission contains one buffer and no semaphore or stage-mask input",
           state.mQueueSubmits.size() == 2 &&
               std::all_of(state.mQueueSubmits.begin(), state.mQueueSubmits.end(),
                           [&](const QueueSubmitRecord& record)
                           {
                               return record.mQueue == state.mQueue && record.mSubmitCount == 1 && record.mFence == state.mFenceOutput &&
                                      record.mStructureType == VK_STRUCTURE_TYPE_SUBMIT_INFO && record.mNextNull &&
                                      record.mWaitSemaphoreCount == 0 && record.mWaitSemaphoresNull && record.mWaitStageMasksNull &&
                                      record.mCommandBufferCount == 1 && record.mCommandBuffer == state.mCommandBufferOutput &&
                                      record.mSignalSemaphoreCount == 0 && record.mSignalSemaphoresNull;
                           }));
    ensure("execution performs no allocation-callback operation", state.mAllocatorNull.empty());
}

template<>
template<>
void render_vulkan_swapchain_frame_slot_object::test<11>()
{
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents    = makeParents(state);
        auto            generation = takeGeneration(resolveSlot(parents));
        ensureOperationSuccess(resolveExecution(generation, parents), VulkanSwapchainFrameSlotDisposition::Reusable);
        auto logical = std::move(parents.mLogical);
        state.clearFrameRecords();
        ensureOperationError(resolveExecution(generation, parents),
                             VulkanSwapchainFrameSlotOperationCode::InvalidLogicalDeviceGeneration,
                             VulkanSwapchainFrameSlotDisposition::Reusable);
        ensure("stale logical provenance stops before execution dispatch", state.mInstanceLookups.empty() && state.mEvents.empty());
        generation.reset();
        parents.mImages.reset();
        parents.mSwapchain.reset();
        ensure("the moved logical generation remains live", logical.device() == state.mDevice);
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents    = makeParents(state);
        auto            generation = takeGeneration(resolveSlot(parents));
        ensureOperationSuccess(resolveExecution(generation, parents), VulkanSwapchainFrameSlotDisposition::Reusable);
        auto configuration = std::move(parents.mConfiguration);
        state.clearFrameRecords();
        ensureOperationError(resolveExecution(generation, parents),
                             VulkanSwapchainFrameSlotOperationCode::InvalidSwapchainConfigurationGeneration,
                             VulkanSwapchainFrameSlotDisposition::Reusable);
        ensure("stale configuration provenance stops before execution dispatch", state.mInstanceLookups.empty() && state.mEvents.empty());
        generation.reset();
        parents.mImages.reset();
        parents.mSwapchain.reset();
        ensure("the moved configuration generation remains live", configuration.device() == state.mDevice);
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents    = makeParents(state);
        auto            generation = takeGeneration(resolveSlot(parents));
        ensureOperationSuccess(resolveExecution(generation, parents), VulkanSwapchainFrameSlotDisposition::Reusable);
        auto swapchain = std::move(parents.mSwapchain);
        state.clearFrameRecords();
        ensureOperationError(resolveExecution(generation, parents), VulkanSwapchainFrameSlotOperationCode::InvalidSwapchainGeneration,
                             VulkanSwapchainFrameSlotDisposition::Reusable);
        ensure("stale swapchain provenance stops before execution dispatch", state.mInstanceLookups.empty() && state.mEvents.empty());
        generation.reset();
        parents.mImages.reset();
        ensure("the moved swapchain generation remains live", swapchain.swapchain() == state.mSwapchain);
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents    = makeParents(state);
        auto            generation = takeGeneration(resolveSlot(parents));
        ensureOperationSuccess(resolveExecution(generation, parents), VulkanSwapchainFrameSlotDisposition::Reusable);
        auto images = std::move(parents.mImages);
        state.clearFrameRecords();
        ensureOperationError(resolveExecution(generation, parents),
                             VulkanSwapchainFrameSlotOperationCode::InvalidSwapchainImagesGeneration,
                             VulkanSwapchainFrameSlotDisposition::Reusable);
        ensure("stale image provenance stops before execution dispatch", state.mInstanceLookups.empty() && state.mEvents.empty());
        generation.reset();
        ensure("the moved image generation remains live", images.imageCount() == 3);
    }
}

template<>
template<>
void render_vulkan_swapchain_frame_slot_object::test<12>()
{
    enum class FailurePoint : std::uint8_t
    {
        InitialWait,
        ResetCommandBuffer,
        BeginCommandBuffer,
        EndCommandBuffer,
        ResetFence,
        QueueSubmit,
        FinalWait
    };
    struct Case
    {
        FailurePoint                        mPoint;
        VulkanSwapchainFrameSlotCommand     mCommand;
        VkResult                            mResult;
        VulkanSwapchainFrameSlotDisposition mDisposition;
        std::size_t                         mEventCount;
    };
    constexpr std::array cases{
        Case{ FailurePoint::InitialWait, VulkanSwapchainFrameSlotCommand::WaitForFences, VK_TIMEOUT,
              VulkanSwapchainFrameSlotDisposition::Reusable, 1 },
        Case{ FailurePoint::ResetCommandBuffer, VulkanSwapchainFrameSlotCommand::ResetCommandBuffer, VK_ERROR_OUT_OF_HOST_MEMORY,
              VulkanSwapchainFrameSlotDisposition::ResetRequired, 2 },
        Case{ FailurePoint::BeginCommandBuffer, VulkanSwapchainFrameSlotCommand::BeginCommandBuffer, VK_ERROR_OUT_OF_DEVICE_MEMORY,
              VulkanSwapchainFrameSlotDisposition::ResetRequired, 3 },
        Case{ FailurePoint::EndCommandBuffer, VulkanSwapchainFrameSlotCommand::EndCommandBuffer, VK_ERROR_UNKNOWN,
              VulkanSwapchainFrameSlotDisposition::ResetRequired, 4 },
        Case{ FailurePoint::ResetFence, VulkanSwapchainFrameSlotCommand::ResetFences, VK_ERROR_OUT_OF_HOST_MEMORY,
              VulkanSwapchainFrameSlotDisposition::ResetRequired, 5 },
        Case{ FailurePoint::QueueSubmit, VulkanSwapchainFrameSlotCommand::QueueSubmit, VK_ERROR_UNKNOWN,
              VulkanSwapchainFrameSlotDisposition::ResetRequired, 6 },
        Case{ FailurePoint::FinalWait, VulkanSwapchainFrameSlotCommand::WaitForFences, VK_TIMEOUT,
              VulkanSwapchainFrameSlotDisposition::Pending, 7 },
    };

    for (const Case& test_case : cases)
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents    = makeParents(state);
        auto            generation = takeGeneration(resolveSlot(parents));
        state.clearFrameRecords();
        ensureOperationSuccess(resolveExecution(generation, parents), VulkanSwapchainFrameSlotDisposition::Reusable);
        state.clearFrameRecords();

        switch (test_case.mPoint)
        {
            case FailurePoint::InitialWait:
                state.mWaitResults = { test_case.mResult };
                break;
            case FailurePoint::ResetCommandBuffer:
                state.mResetCommandBufferResult = test_case.mResult;
                break;
            case FailurePoint::BeginCommandBuffer:
                state.mBeginCommandBufferResult = test_case.mResult;
                break;
            case FailurePoint::EndCommandBuffer:
                state.mEndCommandBufferResult = test_case.mResult;
                break;
            case FailurePoint::ResetFence:
                state.mResetFencesResult = test_case.mResult;
                break;
            case FailurePoint::QueueSubmit:
                state.mQueueSubmitResult = test_case.mResult;
                break;
            case FailurePoint::FinalWait:
                state.mWaitResults = { VK_SUCCESS, test_case.mResult };
                break;
        }

        ensureOperationError(generation.executeEmptySubmission(), VulkanSwapchainFrameSlotOperationCode::CommandFailure,
                             test_case.mDisposition, test_case.mCommand, test_case.mResult);
        ensure("failure stops at its exact command and invokes no destroy callback",
               state.mEvents.size() == test_case.mEventCount && state.mDestroyedCommandPools.empty() &&
                   state.mDestroyedSemaphores.empty() && state.mDestroyedFences.empty());
        ensure("the failure leaves the durable exact disposition", generation.disposition() == test_case.mDisposition);

        if (test_case.mDisposition == VulkanSwapchainFrameSlotDisposition::Pending)
        {
            ensureOperationSuccess(generation.retryEmptySubmissionCompletion(), VulkanSwapchainFrameSlotDisposition::Reusable);
        }
        else if (test_case.mDisposition == VulkanSwapchainFrameSlotDisposition::ResetRequired)
        {
            const std::size_t event_count = state.mEvents.size();
            ensureOperationError(generation.executeEmptySubmission(), VulkanSwapchainFrameSlotOperationCode::InvalidDisposition,
                                 VulkanSwapchainFrameSlotDisposition::ResetRequired);
            ensureOperationError(generation.retryEmptySubmissionCompletion(),
                                 VulkanSwapchainFrameSlotOperationCode::InvalidDisposition,
                                 VulkanSwapchainFrameSlotDisposition::ResetRequired);
            ensure_equals("reset-required rejects execution and completion retry without a Vulkan call", state.mEvents.size(), event_count);
        }
    }
}

template<>
template<>
void render_vulkan_swapchain_frame_slot_object::test<13>()
{
    enum class DeviceLossPoint : std::uint8_t
    {
        InitialWait,
        QueueSubmitThenWaitSuccess,
        QueueSubmitThenWaitDeviceLost,
        FinalWait
    };
    struct Case
    {
        DeviceLossPoint                 mPoint;
        VulkanSwapchainFrameSlotCommand mCommand;
    };
    constexpr std::array cases{
        Case{ DeviceLossPoint::InitialWait, VulkanSwapchainFrameSlotCommand::WaitForFences },
        Case{ DeviceLossPoint::QueueSubmitThenWaitSuccess, VulkanSwapchainFrameSlotCommand::QueueSubmit },
        Case{ DeviceLossPoint::QueueSubmitThenWaitDeviceLost, VulkanSwapchainFrameSlotCommand::QueueSubmit },
        Case{ DeviceLossPoint::FinalWait, VulkanSwapchainFrameSlotCommand::WaitForFences },
    };

    for (const Case& test_case : cases)
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents    = makeParents(state);
        auto            generation = takeGeneration(resolveSlot(parents));
        state.clearFrameRecords();
        ensureOperationSuccess(resolveExecution(generation, parents), VulkanSwapchainFrameSlotDisposition::Reusable);
        state.clearFrameRecords();

        if (test_case.mPoint == DeviceLossPoint::InitialWait)
        {
            state.mWaitResults = { VK_ERROR_DEVICE_LOST };
        }
        else if (test_case.mPoint == DeviceLossPoint::QueueSubmitThenWaitSuccess)
        {
            state.mQueueSubmitResult = VK_ERROR_DEVICE_LOST;
            state.mWaitResults       = { VK_SUCCESS, VK_SUCCESS };
        }
        else if (test_case.mPoint == DeviceLossPoint::QueueSubmitThenWaitDeviceLost)
        {
            state.mQueueSubmitResult = VK_ERROR_DEVICE_LOST;
            state.mWaitResults       = { VK_SUCCESS, VK_ERROR_DEVICE_LOST };
        }
        else
        {
            state.mWaitResults = { VK_SUCCESS, VK_ERROR_DEVICE_LOST };
        }

        const bool queue_submit_loss = test_case.mPoint == DeviceLossPoint::QueueSubmitThenWaitSuccess ||
                                       test_case.mPoint == DeviceLossPoint::QueueSubmitThenWaitDeviceLost;
        ensureOperationError(generation.executeEmptySubmission(), VulkanSwapchainFrameSlotOperationCode::CommandFailure,
                             queue_submit_loss ? VulkanSwapchainFrameSlotDisposition::Pending
                                               : VulkanSwapchainFrameSlotDisposition::DeviceLost,
                             test_case.mCommand, VK_ERROR_DEVICE_LOST);

        if (queue_submit_loss)
        {
            const std::size_t pending_event_count = state.mEvents.size();
            generation.reset();
            ensure("submit device loss remains pending and blocks every destroy callback",
                   generation.disposition() == VulkanSwapchainFrameSlotDisposition::Pending &&
                       state.mEvents.size() == pending_event_count && state.mDestroyedFences.empty() &&
                       state.mDestroyedSemaphores.empty() && state.mDestroyedCommandPools.empty());
            ensureOperationError(generation.executeEmptySubmission(), VulkanSwapchainFrameSlotOperationCode::InvalidDisposition,
                                 VulkanSwapchainFrameSlotDisposition::Pending);
            if (test_case.mPoint == DeviceLossPoint::QueueSubmitThenWaitSuccess)
            {
                ensureOperationSuccess(generation.retryEmptySubmissionCompletion(), VulkanSwapchainFrameSlotDisposition::DeviceLost);
            }
            else
            {
                ensureOperationError(generation.retryEmptySubmissionCompletion(), VulkanSwapchainFrameSlotOperationCode::CommandFailure,
                                     VulkanSwapchainFrameSlotDisposition::DeviceLost, VulkanSwapchainFrameSlotCommand::WaitForFences,
                                     VK_ERROR_DEVICE_LOST);
            }
        }

        const std::size_t event_count = state.mEvents.size();
        ensureOperationError(generation.executeEmptySubmission(), VulkanSwapchainFrameSlotOperationCode::InvalidDisposition,
                             VulkanSwapchainFrameSlotDisposition::DeviceLost);
        ensureOperationError(generation.retryEmptySubmissionCompletion(), VulkanSwapchainFrameSlotOperationCode::InvalidDisposition,
                             VulkanSwapchainFrameSlotDisposition::DeviceLost);
        ensure_equals("device-lost rejects execution and retry without another Vulkan call", state.mEvents.size(), event_count);
        generation.reset();
        ensure("completed device-loss accounting permits exactly one child-first teardown",
               state.mEvents.size() == event_count + 3 && state.mDestroyedFences.size() == 1 && state.mDestroyedSemaphores.size() == 1 &&
                   state.mDestroyedCommandPools.size() == 1);
    }
}

template<>
template<>
void render_vulkan_swapchain_frame_slot_object::test<14>()
{
    FakeState       state;
    ScopedFakeState scope(state);
    auto            parents    = makeParents(state);
    auto            generation = takeGeneration(resolveSlot(parents));
    state.clearFrameRecords();
    ensureOperationSuccess(resolveExecution(generation, parents), VulkanSwapchainFrameSlotDisposition::Reusable);
    state.clearFrameRecords();
    state.mWaitResults = { VK_SUCCESS, VK_TIMEOUT, VK_TIMEOUT, VK_SUCCESS };

    ensureOperationError(generation.executeEmptySubmission(), VulkanSwapchainFrameSlotOperationCode::CommandFailure,
                         VulkanSwapchainFrameSlotDisposition::Pending, VulkanSwapchainFrameSlotCommand::WaitForFences, VK_TIMEOUT);
    const std::size_t pending_event_count = state.mEvents.size();
    ensureOperationError(generation.executeEmptySubmission(), VulkanSwapchainFrameSlotOperationCode::InvalidDisposition,
                         VulkanSwapchainFrameSlotDisposition::Pending);
    ensureOperationSuccess(resolveExecution(generation, parents), VulkanSwapchainFrameSlotDisposition::Pending);
    ensure("pending dispatch resolution is idempotent and a second execution performs no Vulkan call",
           state.mInstanceLookups.empty() && state.mDeviceLookups.empty() && state.mEvents.size() == pending_event_count);

    generation.reset();
    ensure("the pending reset guard invokes no destroy callback and retains ownership for completion retry",
           state.mEvents.size() == pending_event_count && state.mDestroyedFences.empty() && state.mDestroyedSemaphores.empty() &&
               state.mDestroyedCommandPools.empty() && generation.submissionFence() == state.mFenceOutput &&
               generation.disposition() == VulkanSwapchainFrameSlotDisposition::Pending);

    ensureOperationError(generation.retryEmptySubmissionCompletion(), VulkanSwapchainFrameSlotOperationCode::CommandFailure,
                         VulkanSwapchainFrameSlotDisposition::Pending, VulkanSwapchainFrameSlotCommand::WaitForFences, VK_TIMEOUT);
    ensureOperationSuccess(generation.retryEmptySubmissionCompletion(), VulkanSwapchainFrameSlotDisposition::Reusable);
    const std::size_t completed_event_count = state.mEvents.size();
    ensureOperationError(generation.retryEmptySubmissionCompletion(), VulkanSwapchainFrameSlotOperationCode::InvalidDisposition,
                         VulkanSwapchainFrameSlotDisposition::Reusable);
    ensure_equals("completion retry is rejected after the slot becomes reusable", state.mEvents.size(), completed_event_count);

    generation.reset();
    ensure("recovered pending ownership is destroyed exactly once in child-first order",
           state.mEvents.size() == completed_event_count + 3 && state.mDestroyedFences.size() == 1 &&
               state.mDestroyedSemaphores.size() == 1 && state.mDestroyedCommandPools.size() == 1);
}

template<>
template<>
void render_vulkan_swapchain_frame_slot_object::test<15>()
{
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents    = makeParents(state);
        auto            generation = takeGeneration(resolveSlot(parents));
        state.clearFrameRecords();
        ensureOperationSuccess(resolveExecution(generation, parents), VulkanSwapchainFrameSlotDisposition::Reusable);
        auto moved = std::move(generation);

        ensure("move transfers retained dispatch, disposition, and ownership while disarming the source",
               moved.disposition() == VulkanSwapchainFrameSlotDisposition::Reusable &&
                   moved.createdFor(parents.mLogical, parents.mConfiguration, parents.mSwapchain, parents.mImages) &&
                   generation.commandPool() == VK_NULL_HANDLE && generation.disposition() == VulkanSwapchainFrameSlotDisposition::Reusable);
        ensureOperationError(generation.executeEmptySubmission(), VulkanSwapchainFrameSlotOperationCode::MissingRequiredCommand,
                             VulkanSwapchainFrameSlotDisposition::Reusable, VulkanSwapchainFrameSlotCommand::WaitForFences);
        state.clearFrameRecords();
        ensureOperationSuccess(moved.executeEmptySubmission(), VulkanSwapchainFrameSlotDisposition::Reusable);
        moved.reset();
        moved.reset();
        generation.reset();
        ensure("only the moved execution-capable owner destroys each resource once",
               state.mDestroyedFences.size() == 1 && state.mDestroyedSemaphores.size() == 1 && state.mDestroyedCommandPools.size() == 1);
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents    = makeParents(state);
        auto            generation = takeGeneration(resolveSlot(parents));
        state.clearFrameRecords();
        ensureOperationSuccess(resolveExecution(generation, parents), VulkanSwapchainFrameSlotDisposition::Reusable);
        state.clearFrameRecords();
        state.mQueueSubmitResult = VK_ERROR_DEVICE_LOST;
        state.mWaitResults       = { VK_SUCCESS, VK_SUCCESS };
        ensureOperationError(generation.executeEmptySubmission(), VulkanSwapchainFrameSlotOperationCode::CommandFailure,
                             VulkanSwapchainFrameSlotDisposition::Pending, VulkanSwapchainFrameSlotCommand::QueueSubmit,
                             VK_ERROR_DEVICE_LOST);

        auto moved = std::move(generation);
        ensure("move transfers pending device-loss accounting and disarms the source",
               moved.disposition() == VulkanSwapchainFrameSlotDisposition::Pending &&
                   generation.disposition() == VulkanSwapchainFrameSlotDisposition::Reusable && generation.commandPool() == VK_NULL_HANDLE);
        const std::size_t pending_event_count = state.mEvents.size();
        generation.reset();
        ensure_equals("the moved-from source owns nothing and invokes no destroy callback", state.mEvents.size(), pending_event_count);
        ensureOperationSuccess(moved.retryEmptySubmissionCompletion(), VulkanSwapchainFrameSlotDisposition::DeviceLost);
        moved.reset();
        moved.reset();
        ensure("the moved pending owner retires work before exactly one teardown",
               state.mDestroyedFences.size() == 1 && state.mDestroyedSemaphores.size() == 1 && state.mDestroyedCommandPools.size() == 1 &&
                   moved.disposition() == VulkanSwapchainFrameSlotDisposition::Reusable);
    }
}

} // namespace tut
