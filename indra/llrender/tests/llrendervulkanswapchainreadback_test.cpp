/**
 * @file llrendervulkanswapchainreadback_test.cpp
 * @brief Tests for Vulkan swapchain readback-buffer ownership.
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

#include "llrendervulkanswapchainreadback.h"
#include "lltut.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
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
    GetPhysicalDeviceMemoryProperties,
    GetDeviceProcAddr,
    CreateBuffer,
    DestroyBuffer,
    GetBufferMemoryRequirements,
    AllocateMemory,
    FreeMemory,
    BindBufferMemory,
    MapMemory,
    UnmapMemory
};

struct BufferRecord
{
    VkStructureType      mStructureType         = VK_STRUCTURE_TYPE_MAX_ENUM;
    const void*          mNext                  = nullptr;
    VkBufferCreateFlags  mFlags                 = 0;
    VkDeviceSize         mSize                  = 0;
    VkBufferUsageFlags   mUsage                 = 0;
    VkSharingMode        mSharingMode           = VK_SHARING_MODE_MAX_ENUM;
    std::uint32_t        mQueueFamilyIndexCount = 0;
    const std::uint32_t* mQueueFamilyIndices    = nullptr;
};

struct FakeState
{
    VkInstance       mInstance       = fakeHandle<VkInstance>(0x1000);
    VkSurfaceKHR     mSurface        = fakeHandle<VkSurfaceKHR>(0x2000);
    VkPhysicalDevice mPhysicalDevice = fakeHandle<VkPhysicalDevice>(0x3000);
    VkDevice         mDevice         = fakeHandle<VkDevice>(0x4000);
    VkQueue          mQueue          = fakeHandle<VkQueue>(0x5000);
    VkSwapchainKHR   mSwapchain      = fakeHandle<VkSwapchainKHR>(0x6000);
    VkBuffer         mBuffer         = fakeHandle<VkBuffer>(0x9000);
    VkDeviceMemory   mMemory         = fakeHandle<VkDeviceMemory>(0xa000);
    void*            mMappedData     = reinterpret_cast<void*>(0xb000);
    std::uint32_t    mQueueFamily    = 2;

    VkSurfaceCapabilitiesKHR          mCapabilities{ 2,
                                            0,
                                                     { std::numeric_limits<std::uint32_t>::max(),
                                                       std::numeric_limits<std::uint32_t>::max() },
                                                     { 64, 64 },
                                                     { 4096, 2160 },
                                            1,
                                            VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
                                            VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
                                            VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
                                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                                VK_IMAGE_USAGE_TRANSFER_SRC_BIT };
    std::array<VkSurfaceFormatKHR, 1> mFormats{ VkSurfaceFormatKHR{ VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR } };
    std::array<VkPresentModeKHR, 1>   mPresentModes{ VK_PRESENT_MODE_FIFO_KHR };
    std::array<VkImage, 3>            mImages{ fakeHandle<VkImage>(0x7100), fakeHandle<VkImage>(0x7200), fakeHandle<VkImage>(0x7300) };

    MissingCommand           mMissingCommand = MissingCommand::None;
    std::vector<std::string> mInstanceLookups;
    std::vector<std::string> mDeviceLookups;
    bool                     mAllCommandsResolvedBeforeMutation = false;

    VkPhysicalDeviceMemoryProperties mMemoryProperties{};
    VkMemoryRequirements             mRequirements{ 1280 * 720 * 4, 256, 0xf };
    VkResult                         mCreateResult   = VK_SUCCESS;
    VkBuffer                         mCreateOutput   = mBuffer;
    VkResult                         mAllocateResult = VK_SUCCESS;
    VkDeviceMemory                   mAllocateOutput = mMemory;
    VkResult                         mBindResult     = VK_SUCCESS;
    VkResult                         mMapResult      = VK_SUCCESS;
    void*                            mMapOutput      = mMappedData;

    std::vector<BufferRecord>         mBufferRecords;
    std::vector<VkDevice>             mCreateDevices;
    std::vector<bool>                 mCreateAllocatorNull;
    std::vector<VkBuffer>             mRequirementBuffers;
    std::vector<VkMemoryAllocateInfo> mAllocateInfos;
    std::vector<VkDevice>             mAllocateDevices;
    std::vector<bool>                 mAllocateAllocatorNull;
    std::vector<VkBuffer>             mBoundBuffers;
    std::vector<VkDeviceMemory>       mBoundMemories;
    std::vector<VkDeviceSize>         mBindOffsets;
    std::vector<VkDeviceMemory>       mMappedMemories;
    std::vector<VkDeviceSize>         mMapOffsets;
    std::vector<VkDeviceSize>         mMapSizes;
    std::vector<VkMemoryMapFlags>     mMapFlags;
    std::vector<VkDeviceMemory>       mUnmappedMemories;
    std::vector<VkBuffer>             mDestroyedBuffers;
    std::vector<VkDeviceMemory>       mFreedMemories;
    std::vector<std::string>          mReadbackTeardownOrder;

    std::size_t mDestroyImageViewCalls = 0;
    std::size_t mDestroySwapchainCalls = 0;
    std::size_t mDestroyDeviceCalls    = 0;

    VulkanSwapchainImagesGeneration*               mImagesToInvalidate = nullptr;
    std::optional<VulkanSwapchainImagesGeneration> mMovedImages;

    FakeState()
    {
        mMemoryProperties.memoryHeapCount              = 2;
        mMemoryProperties.memoryHeaps[0].size          = 1ULL << 30;
        mMemoryProperties.memoryHeaps[0].flags         = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;
        mMemoryProperties.memoryHeaps[1].size          = 1ULL << 29;
        mMemoryProperties.memoryTypeCount              = 4;
        mMemoryProperties.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        mMemoryProperties.memoryTypes[0].heapIndex     = 0;
        mMemoryProperties.memoryTypes[1].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        mMemoryProperties.memoryTypes[1].heapIndex     = 1;
        mMemoryProperties.memoryTypes[2].propertyFlags =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        mMemoryProperties.memoryTypes[2].heapIndex = 1;
        mMemoryProperties.memoryTypes[3].propertyFlags =
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        mMemoryProperties.memoryTypes[3].heapIndex = 0;
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
        return VK_ERROR_INITIALIZATION_FAILED;
    if (!devices)
    {
        *count = 1;
        return VK_SUCCESS;
    }
    if (*count == 0)
        return VK_INCOMPLETE;
    devices[0] = gFakeState->mPhysicalDevice;
    *count     = 1;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeGetPhysicalDeviceProperties(VkPhysicalDevice            physical_device,
                                                           VkPhysicalDeviceProperties* properties) noexcept
{
    if (!gFakeState || physical_device != gFakeState->mPhysicalDevice || !properties)
        return;
    *properties                                 = {};
    properties->apiVersion                      = VK_API_VERSION_1_1;
    properties->limits.maxFramebufferWidth      = 4096;
    properties->limits.maxFramebufferHeight     = 2160;
    properties->limits.maxFramebufferLayers     = 1;
    properties->limits.maxViewportDimensions[0] = 4096;
    properties->limits.maxViewportDimensions[1] = 4096;
    properties->limits.viewportBoundsRange[0]   = -8192.0f;
    properties->limits.viewportBoundsRange[1]   = 8191.0f;
    std::strncpy(properties->deviceName, "readback-fake", VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 1);
}

VKAPI_ATTR void VKAPI_CALL fakeGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice         physical_device,
                                                                      std::uint32_t*           count,
                                                                      VkQueueFamilyProperties* properties) noexcept
{
    if (!gFakeState || physical_device != gFakeState->mPhysicalDevice || !count)
        return;
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
        return VK_ERROR_SURFACE_LOST_KHR;
    *supported = queue_family == gFakeState->mQueueFamily ? VK_TRUE : VK_FALSE;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeEnumerateDeviceExtensionProperties(VkPhysicalDevice       physical_device,
                                                                      const char*            layer_name,
                                                                      std::uint32_t*         count,
                                                                      VkExtensionProperties* properties) noexcept
{
    if (!gFakeState || physical_device != gFakeState->mPhysicalDevice || layer_name || !count)
        return VK_ERROR_INITIALIZATION_FAILED;
    if (!properties)
    {
        *count = 2;
        return VK_SUCCESS;
    }
    if (*count < 2)
        return VK_INCOMPLETE;
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
        return;
    auto* maintenance = static_cast<VkPhysicalDeviceSwapchainMaintenance1FeaturesKHR*>(features->pNext);
    if (features->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 && maintenance &&
        maintenance->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_KHR && !maintenance->pNext)
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
    if (!gFakeState || physical_device != gFakeState->mPhysicalDevice || !create_info || !device)
        return VK_ERROR_INITIALIZATION_FAILED;
    *device = gFakeState->mDevice;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyDevice(VkDevice device, const VkAllocationCallbacks*) noexcept
{
    if (gFakeState && device == gFakeState->mDevice)
        ++gFakeState->mDestroyDeviceCalls;
}

VKAPI_ATTR void VKAPI_CALL fakeGetDeviceQueue(VkDevice      device,
                                              std::uint32_t queue_family,
                                              std::uint32_t queue_index,
                                              VkQueue*      queue) noexcept
{
    if (gFakeState && device == gFakeState->mDevice && queue_family == gFakeState->mQueueFamily && queue_index == 0 && queue)
        *queue = gFakeState->mQueue;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeGetSurfaceCapabilities(VkPhysicalDevice          physical_device,
                                                          VkSurfaceKHR              surface,
                                                          VkSurfaceCapabilitiesKHR* capabilities) noexcept
{
    if (!gFakeState || physical_device != gFakeState->mPhysicalDevice || surface != gFakeState->mSurface || !capabilities)
        return VK_ERROR_SURFACE_LOST_KHR;
    *capabilities = gFakeState->mCapabilities;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeGetSurfaceFormats(VkPhysicalDevice    physical_device,
                                                     VkSurfaceKHR        surface,
                                                     std::uint32_t*      count,
                                                     VkSurfaceFormatKHR* formats) noexcept
{
    if (!gFakeState || physical_device != gFakeState->mPhysicalDevice || surface != gFakeState->mSurface || !count)
        return VK_ERROR_SURFACE_LOST_KHR;
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

VKAPI_ATTR VkResult VKAPI_CALL fakeGetSurfacePresentModes(VkPhysicalDevice  physical_device,
                                                          VkSurfaceKHR      surface,
                                                          std::uint32_t*    count,
                                                          VkPresentModeKHR* modes) noexcept
{
    if (!gFakeState || physical_device != gFakeState->mPhysicalDevice || surface != gFakeState->mSurface || !count)
        return VK_ERROR_SURFACE_LOST_KHR;
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
        *properties = {};
        properties->optimalTilingFeatures =
            VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateSwapchain(VkDevice device,
                                                   const VkSwapchainCreateInfoKHR*,
                                                   const VkAllocationCallbacks*,
                                                   VkSwapchainKHR* swapchain) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || !swapchain)
        return VK_ERROR_INITIALIZATION_FAILED;
    *swapchain = gFakeState->mSwapchain;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroySwapchain(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks*) noexcept
{
    if (gFakeState && device == gFakeState->mDevice && swapchain == gFakeState->mSwapchain)
        ++gFakeState->mDestroySwapchainCalls;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeGetSwapchainImages(VkDevice       device,
                                                      VkSwapchainKHR swapchain,
                                                      std::uint32_t* count,
                                                      VkImage*       images) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || swapchain != gFakeState->mSwapchain || !count)
        return VK_ERROR_INITIALIZATION_FAILED;
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
    static std::uintptr_t next_view = 0x8100;
    if (!gFakeState || device != gFakeState->mDevice || !image_view)
        return VK_ERROR_INITIALIZATION_FAILED;
    *image_view = fakeHandle<VkImageView>(next_view);
    next_view += 0x100;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyImageView(VkDevice device, VkImageView, const VkAllocationCallbacks*) noexcept
{
    if (gFakeState && device == gFakeState->mDevice)
        ++gFakeState->mDestroyImageViewCalls;
}

VKAPI_ATTR void VKAPI_CALL fakeGetPhysicalDeviceMemoryProperties(VkPhysicalDevice                  physical_device,
                                                                 VkPhysicalDeviceMemoryProperties* properties) noexcept
{
    if (gFakeState && physical_device == gFakeState->mPhysicalDevice && properties)
        *properties = gFakeState->mMemoryProperties;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateBuffer(VkDevice                     device,
                                                const VkBufferCreateInfo*    create_info,
                                                const VkAllocationCallbacks* allocator,
                                                VkBuffer*                    buffer) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || !create_info || !buffer)
        return VK_ERROR_INITIALIZATION_FAILED;
    gFakeState->mAllCommandsResolvedBeforeMutation =
        gFakeState->mInstanceLookups == std::vector<std::string>{ "vkGetPhysicalDeviceMemoryProperties", "vkGetDeviceProcAddr" } &&
        gFakeState->mDeviceLookups == std::vector<std::string>{ "vkCreateBuffer",   "vkDestroyBuffer", "vkGetBufferMemoryRequirements",
                                                                "vkAllocateMemory", "vkFreeMemory",    "vkBindBufferMemory",
                                                                "vkMapMemory",      "vkUnmapMemory" };
    gFakeState->mBufferRecords.push_back({ create_info->sType, create_info->pNext, create_info->flags, create_info->size,
                                           create_info->usage, create_info->sharingMode, create_info->queueFamilyIndexCount,
                                           create_info->pQueueFamilyIndices });
    gFakeState->mCreateDevices.push_back(device);
    gFakeState->mCreateAllocatorNull.push_back(allocator == nullptr);
    *buffer = gFakeState->mCreateOutput;
    return gFakeState->mCreateResult;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyBuffer(VkDevice, VkBuffer buffer, const VkAllocationCallbacks*) noexcept
{
    if (!gFakeState)
        return;
    gFakeState->mDestroyedBuffers.push_back(buffer);
    gFakeState->mReadbackTeardownOrder.emplace_back("destroy-buffer");
}

VKAPI_ATTR void VKAPI_CALL fakeGetBufferMemoryRequirements(VkDevice, VkBuffer buffer, VkMemoryRequirements* requirements) noexcept
{
    if (!gFakeState || !requirements)
        return;
    gFakeState->mRequirementBuffers.push_back(buffer);
    *requirements = gFakeState->mRequirements;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeAllocateMemory(VkDevice                     device,
                                                  const VkMemoryAllocateInfo*  allocate_info,
                                                  const VkAllocationCallbacks* allocator,
                                                  VkDeviceMemory*              memory) noexcept
{
    if (!gFakeState || !allocate_info || !memory)
        return VK_ERROR_INITIALIZATION_FAILED;
    gFakeState->mAllocateInfos.push_back(*allocate_info);
    gFakeState->mAllocateDevices.push_back(device);
    gFakeState->mAllocateAllocatorNull.push_back(allocator == nullptr);
    *memory = gFakeState->mAllocateOutput;
    return gFakeState->mAllocateResult;
}

VKAPI_ATTR void VKAPI_CALL fakeFreeMemory(VkDevice, VkDeviceMemory memory, const VkAllocationCallbacks*) noexcept
{
    if (!gFakeState)
        return;
    gFakeState->mFreedMemories.push_back(memory);
    gFakeState->mReadbackTeardownOrder.emplace_back("free-memory");
}

VKAPI_ATTR VkResult VKAPI_CALL fakeBindBufferMemory(VkDevice, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize offset) noexcept
{
    if (!gFakeState)
        return VK_ERROR_INITIALIZATION_FAILED;
    gFakeState->mBoundBuffers.push_back(buffer);
    gFakeState->mBoundMemories.push_back(memory);
    gFakeState->mBindOffsets.push_back(offset);
    return gFakeState->mBindResult;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeMapMemory(VkDevice, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size,
                                             VkMemoryMapFlags flags, void** data) noexcept
{
    if (!gFakeState || !data)
        return VK_ERROR_INITIALIZATION_FAILED;
    gFakeState->mMappedMemories.push_back(memory);
    gFakeState->mMapOffsets.push_back(offset);
    gFakeState->mMapSizes.push_back(size);
    gFakeState->mMapFlags.push_back(flags);
    *data = gFakeState->mMapOutput;
    if (gFakeState->mImagesToInvalidate)
    {
        gFakeState->mMovedImages.emplace(std::move(*gFakeState->mImagesToInvalidate));
        gFakeState->mImagesToInvalidate = nullptr;
    }
    return gFakeState->mMapResult;
}

VKAPI_ATTR void VKAPI_CALL fakeUnmapMemory(VkDevice, VkDeviceMemory memory) noexcept
{
    if (!gFakeState)
        return;
    gFakeState->mUnmappedMemories.push_back(memory);
    gFakeState->mReadbackTeardownOrder.emplace_back("unmap");
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL fakeGetDeviceProcAddr(VkDevice device, const char* name) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || !name)
        return nullptr;
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
    if (std::strcmp(name, "vkCreateBuffer") == 0)
        return gFakeState->mMissingCommand == MissingCommand::CreateBuffer ? nullptr : eraseFunctionType(fakeCreateBuffer);
    if (std::strcmp(name, "vkDestroyBuffer") == 0)
        return gFakeState->mMissingCommand == MissingCommand::DestroyBuffer ? nullptr : eraseFunctionType(fakeDestroyBuffer);
    if (std::strcmp(name, "vkGetBufferMemoryRequirements") == 0)
        return gFakeState->mMissingCommand == MissingCommand::GetBufferMemoryRequirements
                   ? nullptr
                   : eraseFunctionType(fakeGetBufferMemoryRequirements);
    if (std::strcmp(name, "vkAllocateMemory") == 0)
        return gFakeState->mMissingCommand == MissingCommand::AllocateMemory ? nullptr : eraseFunctionType(fakeAllocateMemory);
    if (std::strcmp(name, "vkFreeMemory") == 0)
        return gFakeState->mMissingCommand == MissingCommand::FreeMemory ? nullptr : eraseFunctionType(fakeFreeMemory);
    if (std::strcmp(name, "vkBindBufferMemory") == 0)
        return gFakeState->mMissingCommand == MissingCommand::BindBufferMemory ? nullptr : eraseFunctionType(fakeBindBufferMemory);
    if (std::strcmp(name, "vkMapMemory") == 0)
        return gFakeState->mMissingCommand == MissingCommand::MapMemory ? nullptr : eraseFunctionType(fakeMapMemory);
    if (std::strcmp(name, "vkUnmapMemory") == 0)
        return gFakeState->mMissingCommand == MissingCommand::UnmapMemory ? nullptr : eraseFunctionType(fakeUnmapMemory);
    return nullptr;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL fakeGetInstanceProcAddr(VkInstance instance, const char* name) noexcept
{
    if (!gFakeState || instance != gFakeState->mInstance || !name)
        return nullptr;
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
    if (std::strcmp(name, "vkGetPhysicalDeviceMemoryProperties") == 0)
        return gFakeState->mMissingCommand == MissingCommand::GetPhysicalDeviceMemoryProperties
                   ? nullptr
                   : eraseFunctionType(fakeGetPhysicalDeviceMemoryProperties);
    if (std::strcmp(name, "vkGetDeviceProcAddr") == 0)
        return gFakeState->mMissingCommand == MissingCommand::GetDeviceProcAddr ? nullptr : eraseFunctionType(fakeGetDeviceProcAddr);
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
    auto physical_result = resolveVulkanPhysicalDeviceGeneration({ fakeGetInstanceProcAddr, state.mInstance, state.mSurface });
    tut::ensure("the physical-device fixture resolves", std::holds_alternative<VulkanPhysicalDeviceGeneration>(physical_result));
    auto physical = std::get<VulkanPhysicalDeviceGeneration>(std::move(physical_result));

    auto logical_result = resolveVulkanLogicalDeviceGeneration(physical);
    tut::ensure("the logical-device fixture resolves", std::holds_alternative<VulkanLogicalDeviceGeneration>(logical_result));
    auto logical = std::get<VulkanLogicalDeviceGeneration>(std::move(logical_result));

    auto configuration_result = resolveVulkanSwapchainConfigurationGeneration(physical, logical, { 1280, 720 });
    tut::ensure("the configuration fixture resolves", std::holds_alternative<VulkanSwapchainConfigurationGeneration>(configuration_result));
    auto configuration = std::get<VulkanSwapchainConfigurationGeneration>(std::move(configuration_result));

    auto swapchain_result = resolveVulkanSwapchainGeneration(logical, configuration);
    tut::ensure("the swapchain fixture resolves", std::holds_alternative<VulkanSwapchainGeneration>(swapchain_result));
    auto swapchain = std::get<VulkanSwapchainGeneration>(std::move(swapchain_result));

    auto images_result = resolveVulkanSwapchainImagesGeneration(logical, configuration, swapchain);
    tut::ensure("the images fixture resolves", std::holds_alternative<VulkanSwapchainImagesGeneration>(images_result));
    auto images = std::get<VulkanSwapchainImagesGeneration>(std::move(images_result));

    state.mInstanceLookups.clear();
    state.mDeviceLookups.clear();
    return { std::move(physical), std::move(logical), std::move(configuration), std::move(swapchain), std::move(images) };
}

const VulkanSwapchainReadbackResolutionError& requireError(const VulkanSwapchainReadbackResolutionResult& result)
{
    const auto* error = std::get_if<VulkanSwapchainReadbackResolutionError>(&result);
    tut::ensure("readback resolution returns an error", error != nullptr);
    return *error;
}

void ensureCode(const VulkanSwapchainReadbackResolutionResult& result, VulkanSwapchainReadbackResolutionCode code)
{
    tut::ensure("the exact readback error is reported", requireError(result).mCode == code);
}

VulkanSwapchainReadbackGeneration takeReadback(VulkanSwapchainReadbackResolutionResult&& result)
{
    tut::ensure("readback resolution returns a generation", std::holds_alternative<VulkanSwapchainReadbackGeneration>(result));
    return std::get<VulkanSwapchainReadbackGeneration>(std::move(result));
}

VulkanSwapchainImagesGeneration takeImages(VulkanSwapchainImagesResolutionResult&& result)
{
    tut::ensure("images resolution returns a generation", std::holds_alternative<VulkanSwapchainImagesGeneration>(result));
    return std::get<VulkanSwapchainImagesGeneration>(std::move(result));
}

} // namespace

namespace tut
{

struct render_vulkan_swapchain_readback_test
{
};

using render_vulkan_swapchain_readback_group  = test_group<render_vulkan_swapchain_readback_test>;
using render_vulkan_swapchain_readback_object = render_vulkan_swapchain_readback_group::object;
render_vulkan_swapchain_readback_group render_vulkan_swapchain_readback_tests("render Vulkan swapchain readback");

template<>
template<>
void render_vulkan_swapchain_readback_object::test<1>()
{
    static_assert(!std::is_default_constructible_v<VulkanSwapchainReadbackGeneration>);
    static_assert(!std::is_copy_constructible_v<VulkanSwapchainReadbackGeneration>);
    static_assert(!std::is_copy_assignable_v<VulkanSwapchainReadbackGeneration>);
    static_assert(std::is_nothrow_move_constructible_v<VulkanSwapchainReadbackGeneration>);
    static_assert(!std::is_move_assignable_v<VulkanSwapchainReadbackGeneration>);
    static_assert(std::is_nothrow_destructible_v<VulkanSwapchainReadbackGeneration>);
    static_assert(noexcept(resolveVulkanSwapchainReadbackGeneration(
        std::declval<const VulkanPhysicalDeviceGeneration&>(), std::declval<const VulkanLogicalDeviceGeneration&>(),
        std::declval<const VulkanSwapchainConfigurationGeneration&>(), std::declval<const VulkanSwapchainGeneration&>(),
        std::declval<const VulkanSwapchainImagesGeneration&>())));

    const auto layout = VulkanSwapchainReadbackDetail::checkedByteLayout(1280, 720);
    ensure("ordinary byte layout is exact",
           std::get<VulkanSwapchainReadbackByteLayout>(layout) == VulkanSwapchainReadbackByteLayout{ 1280 * 4, 1280 * 720 * 4 });
    const auto row_overflow = VulkanSwapchainReadbackDetail::checkedByteLayout(std::numeric_limits<VkDeviceSize>::max() / 4 + 1, 1);
    ensure("row-byte overflow is typed",
           std::get<VulkanSwapchainReadbackResolutionError>(row_overflow).mCode == VulkanSwapchainReadbackResolutionCode::RowBytesOverflow);
    const auto byte_overflow = VulkanSwapchainReadbackDetail::checkedByteLayout(std::numeric_limits<VkDeviceSize>::max() / 4, 2);
    ensure("total-byte overflow is typed",
           std::get<VulkanSwapchainReadbackResolutionError>(byte_overflow).mCode ==
               VulkanSwapchainReadbackResolutionCode::ByteCountOverflow);

    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents  = makeParents(state);
        auto            physical = std::move(parents.mPhysical);
        ensureCode(resolveVulkanSwapchainReadbackGeneration(parents.mPhysical, parents.mLogical, parents.mConfiguration, parents.mSwapchain,
                                                            parents.mImages),
                   VulkanSwapchainReadbackResolutionCode::InvalidPhysicalDeviceGeneration);
        ensure("invalid physical stops before dispatch", state.mInstanceLookups.empty() && state.mDeviceLookups.empty());
        ensure("moved physical remains live", physical.physicalDevice() == state.mPhysicalDevice);
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        auto            logical = std::move(parents.mLogical);
        ensureCode(resolveVulkanSwapchainReadbackGeneration(parents.mPhysical, parents.mLogical, parents.mConfiguration, parents.mSwapchain,
                                                            parents.mImages),
                   VulkanSwapchainReadbackResolutionCode::InvalidLogicalDeviceGeneration);
        ensure("invalid logical stops before dispatch", state.mInstanceLookups.empty() && state.mDeviceLookups.empty());
        ensure("moved logical remains live", logical.device() == state.mDevice);
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents       = makeParents(state);
        auto            configuration = std::move(parents.mConfiguration);
        ensureCode(resolveVulkanSwapchainReadbackGeneration(parents.mPhysical, parents.mLogical, parents.mConfiguration, parents.mSwapchain,
                                                            parents.mImages),
                   VulkanSwapchainReadbackResolutionCode::InvalidSwapchainConfigurationGeneration);
        ensure("invalid configuration stops before dispatch", state.mInstanceLookups.empty() && state.mDeviceLookups.empty());
        ensure("moved configuration remains live", configuration.device() == state.mDevice);
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents   = makeParents(state);
        auto            swapchain = std::move(parents.mSwapchain);
        ensureCode(resolveVulkanSwapchainReadbackGeneration(parents.mPhysical, parents.mLogical, parents.mConfiguration, parents.mSwapchain,
                                                            parents.mImages),
                   VulkanSwapchainReadbackResolutionCode::InvalidSwapchainGeneration);
        ensure("invalid swapchain stops before dispatch", state.mInstanceLookups.empty() && state.mDeviceLookups.empty());
        ensure("moved swapchain remains live", swapchain.swapchain() == state.mSwapchain);
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        auto            images  = std::move(parents.mImages);
        ensureCode(resolveVulkanSwapchainReadbackGeneration(parents.mPhysical, parents.mLogical, parents.mConfiguration, parents.mSwapchain,
                                                            parents.mImages),
                   VulkanSwapchainReadbackResolutionCode::InvalidSwapchainImagesGeneration);
        ensure("invalid images stops before dispatch", state.mInstanceLookups.empty() && state.mDeviceLookups.empty());
        ensure("moved images remain live", images.imageCount() == 3);
    }
}

template<>
template<>
void render_vulkan_swapchain_readback_object::test<2>()
{
    constexpr std::array cases{
        std::pair{ MissingCommand::GetPhysicalDeviceMemoryProperties, VulkanSwapchainReadbackCommand::GetPhysicalDeviceMemoryProperties },
        std::pair{ MissingCommand::GetDeviceProcAddr, VulkanSwapchainReadbackCommand::GetDeviceProcAddr },
        std::pair{ MissingCommand::CreateBuffer, VulkanSwapchainReadbackCommand::CreateBuffer },
        std::pair{ MissingCommand::DestroyBuffer, VulkanSwapchainReadbackCommand::DestroyBuffer },
        std::pair{ MissingCommand::GetBufferMemoryRequirements, VulkanSwapchainReadbackCommand::GetBufferMemoryRequirements },
        std::pair{ MissingCommand::AllocateMemory, VulkanSwapchainReadbackCommand::AllocateMemory },
        std::pair{ MissingCommand::FreeMemory, VulkanSwapchainReadbackCommand::FreeMemory },
        std::pair{ MissingCommand::BindBufferMemory, VulkanSwapchainReadbackCommand::BindBufferMemory },
        std::pair{ MissingCommand::MapMemory, VulkanSwapchainReadbackCommand::MapMemory },
        std::pair{ MissingCommand::UnmapMemory, VulkanSwapchainReadbackCommand::UnmapMemory }
    };

    for (std::size_t index = 0; index < cases.size(); ++index)
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        state.mMissingCommand   = cases[index].first;
        const auto  result      = resolveVulkanSwapchainReadbackGeneration(parents.mPhysical, parents.mLogical, parents.mConfiguration,
                                                                           parents.mSwapchain, parents.mImages);
        const auto& error       = requireError(result);
        ensure("missing dispatch identifies its exact command",
               error.mCode == VulkanSwapchainReadbackResolutionCode::MissingRequiredCommand && error.mCommand == cases[index].second);
        ensure("instance dispatch has an exact cutoff", state.mInstanceLookups.size() == std::min<std::size_t>(index + 1, 2));
        ensure("device dispatch has an exact cutoff", state.mDeviceLookups.size() == (index < 2 ? 0 : index - 1));
        ensure("dispatch failure performs no mutation", state.mBufferRecords.empty() && state.mReadbackTeardownOrder.empty());
    }
}

template<>
template<>
void render_vulkan_swapchain_readback_object::test<3>()
{
    FakeState       state;
    ScopedFakeState scope(state);
    auto            parents = makeParents(state);
    auto readback = takeReadback(resolveVulkanSwapchainReadbackGeneration(parents.mPhysical, parents.mLogical, parents.mConfiguration,
                                                                          parents.mSwapchain, parents.mImages));

    constexpr VkMemoryPropertyFlags expected_flags =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    ensure("all commands resolve before the first mutation", state.mAllCommandsResolvedBeforeMutation);
    ensure("owner exposes exact handles, mapping, and image metadata",
           readback.buffer() == state.mBuffer && readback.memory() == state.mMemory && readback.isMapped() &&
               readback.imageFormat() == VK_FORMAT_B8G8R8A8_UNORM && readback.imageExtent().width == 1280 &&
               readback.imageExtent().height == 720 && readback.imageCount() == 3 && readback.rowBytes() == 5120 &&
               readback.byteCount() == 1280 * 720 * 4 && readback.allocationSize() == state.mRequirements.size &&
               readback.memoryTypeIndex() == 2 && readback.memoryPropertyFlags() == expected_flags);
    ensure("owner authenticates all exact parents",
           readback.createdFor(parents.mPhysical, parents.mLogical, parents.mConfiguration, parents.mSwapchain, parents.mImages));

    ensure("buffer creation is exact and uses no custom allocator",
           state.mBufferRecords.size() == 1 && state.mCreateDevices == std::vector<VkDevice>{ state.mDevice } &&
               state.mCreateAllocatorNull == std::vector<bool>{ true });
    const auto& buffer = state.mBufferRecords[0];
    ensure("buffer is an exact exclusive transfer destination",
           buffer.mStructureType == VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO && !buffer.mNext && buffer.mFlags == 0 &&
               buffer.mSize == 1280 * 720 * 4 && buffer.mUsage == VK_BUFFER_USAGE_TRANSFER_DST_BIT &&
               buffer.mSharingMode == VK_SHARING_MODE_EXCLUSIVE && buffer.mQueueFamilyIndexCount == 0 && !buffer.mQueueFamilyIndices);
    ensure("requirements are queried for the exact buffer", state.mRequirementBuffers == std::vector<VkBuffer>{ state.mBuffer });
    ensure("one separate exact-size allocation uses the lowest compatible type",
           state.mAllocateInfos.size() == 1 && state.mAllocateInfos[0].sType == VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO &&
               !state.mAllocateInfos[0].pNext && state.mAllocateInfos[0].allocationSize == state.mRequirements.size &&
               state.mAllocateInfos[0].memoryTypeIndex == 2 && state.mAllocateDevices == std::vector<VkDevice>{ state.mDevice } &&
               state.mAllocateAllocatorNull == std::vector<bool>{ true });
    ensure(
        "binding and whole-allocation persistent mapping are exact",
        state.mBoundBuffers == std::vector<VkBuffer>{ state.mBuffer } &&
            state.mBoundMemories == std::vector<VkDeviceMemory>{ state.mMemory } && state.mBindOffsets == std::vector<VkDeviceSize>{ 0 } &&
            state.mMappedMemories == std::vector<VkDeviceMemory>{ state.mMemory } && state.mMapOffsets == std::vector<VkDeviceSize>{ 0 } &&
            state.mMapSizes == std::vector<VkDeviceSize>{ VK_WHOLE_SIZE } && state.mMapFlags == std::vector<VkMemoryMapFlags>{ 0 });

    auto moved = std::move(readback);
    ensure("move transfers ownership and provenance while disarming the source",
           moved.buffer() == state.mBuffer && moved.memory() == state.mMemory && moved.isMapped() &&
               moved.createdFor(parents.mPhysical, parents.mLogical, parents.mConfiguration, parents.mSwapchain, parents.mImages) &&
               readback.buffer() == VK_NULL_HANDLE && readback.memory() == VK_NULL_HANDLE && !readback.isMapped());
    moved.reset();
    ensure("reset follows unmap, destroy-buffer, free-memory exactly",
           state.mReadbackTeardownOrder == std::vector<std::string>{ "unmap", "destroy-buffer", "free-memory" } &&
               state.mUnmappedMemories == std::vector<VkDeviceMemory>{ state.mMemory } &&
               state.mDestroyedBuffers == std::vector<VkBuffer>{ state.mBuffer } &&
               state.mFreedMemories == std::vector<VkDeviceMemory>{ state.mMemory });
    ensure("reset disarms the owner and is idempotent",
           moved.buffer() == VK_NULL_HANDLE && moved.memory() == VK_NULL_HANDLE && !moved.isMapped() &&
               moved.imageFormat() == VK_FORMAT_UNDEFINED && moved.imageExtent().width == 0 && moved.imageExtent().height == 0 &&
               moved.imageCount() == 0 && moved.rowBytes() == 0 && moved.byteCount() == 0 && moved.allocationSize() == 0 &&
               moved.memoryTypeIndex() == 0 && moved.memoryPropertyFlags() == 0 &&
               !moved.createdFor(parents.mPhysical, parents.mLogical, parents.mConfiguration, parents.mSwapchain, parents.mImages));
    moved.reset();
    ensure("a second reset performs no work", state.mReadbackTeardownOrder.size() == 3);
}

template<>
template<>
void render_vulkan_swapchain_readback_object::test<4>()
{
    {
        FakeState state;
        state.mFormats[0].format = VK_FORMAT_R8G8B8A8_UNORM;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        auto readback = takeReadback(resolveVulkanSwapchainReadbackGeneration(parents.mPhysical, parents.mLogical, parents.mConfiguration,
                                                                              parents.mSwapchain, parents.mImages));
        ensure("RGBA8 uses the same exact four-byte layout",
               readback.imageFormat() == VK_FORMAT_R8G8B8A8_UNORM && readback.rowBytes() == 5120 && readback.byteCount() == 1280 * 720 * 4);
        readback.reset();
    }
    {
        FakeState state;
        state.mRequirements.memoryTypeBits = (1U << 3);
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        auto readback = takeReadback(resolveVulkanSwapchainReadbackGeneration(parents.mPhysical, parents.mLogical, parents.mConfiguration,
                                                                              parents.mSwapchain, parents.mImages));
        const auto expected =
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        ensure("memoryTypeBits filtering selects the lowest allowed coherent visible type",
               readback.memoryTypeIndex() == 3 && readback.memoryPropertyFlags() == expected);
        readback.reset();
    }
    {
        FakeState state;
        state.mMemoryProperties.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        auto readback = takeReadback(resolveVulkanSwapchainReadbackGeneration(parents.mPhysical, parents.mLogical, parents.mConfiguration,
                                                                              parents.mSwapchain, parents.mImages));
        ensure("selection is deterministically the lowest compatible index", readback.memoryTypeIndex() == 0);
        readback.reset();
    }
    {
        FakeState state;
        state.mMemoryProperties.memoryHeaps[0].size = 0;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        auto readback = takeReadback(resolveVulkanSwapchainReadbackGeneration(parents.mPhysical, parents.mLogical, parents.mConfiguration,
                                                                              parents.mSwapchain, parents.mImages));
        ensure("an unused zero-sized heap does not reject a compatible type on another heap", readback.memoryTypeIndex() == 2);
        readback.reset();
    }
    {
        FakeState state;
        state.mMemoryProperties.memoryTypes[2].propertyFlags =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        auto readback = takeReadback(resolveVulkanSwapchainReadbackGeneration(parents.mPhysical, parents.mLogical, parents.mConfiguration,
                                                                              parents.mSwapchain, parents.mImages));
        ensure("a disabled AMD device-coherent type is skipped for the next compatible type", readback.memoryTypeIndex() == 3);
        readback.reset();
    }
    {
        FakeState state;
        state.mMemoryProperties.memoryHeaps[1].flags = VK_MEMORY_HEAP_TILE_MEMORY_BIT_QCOM;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        auto readback = takeReadback(resolveVulkanSwapchainReadbackGeneration(parents.mPhysical, parents.mLogical, parents.mConfiguration,
                                                                              parents.mSwapchain, parents.mImages));
        ensure("a disabled QCOM tile-memory heap is skipped for the next compatible type", readback.memoryTypeIndex() == 3);
        readback.reset();
    }
}

template<>
template<>
void render_vulkan_swapchain_readback_object::test<5>()
{
    const auto run_memory_case = [](auto mutate)
    {
        FakeState state;
        mutate(state);
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        const auto      result  = resolveVulkanSwapchainReadbackGeneration(parents.mPhysical, parents.mLogical, parents.mConfiguration,
                                                                           parents.mSwapchain, parents.mImages);
        ensureCode(result, VulkanSwapchainReadbackResolutionCode::InvalidPhysicalDeviceMemoryProperties);
        ensure("invalid memory properties do not create a buffer", state.mBufferRecords.empty() && state.mReadbackTeardownOrder.empty());
    };
    run_memory_case([](FakeState& state) { state.mMemoryProperties.memoryTypeCount = 0; });
    run_memory_case([](FakeState& state) { state.mMemoryProperties.memoryTypeCount = VK_MAX_MEMORY_TYPES + 1; });
    run_memory_case([](FakeState& state) { state.mMemoryProperties.memoryHeapCount = 0; });
    run_memory_case([](FakeState& state) { state.mMemoryProperties.memoryHeapCount = VK_MAX_MEMORY_HEAPS + 1; });
    run_memory_case([](FakeState& state) { state.mMemoryProperties.memoryTypes[0].heapIndex = 2; });

    const auto run_requirements_case = [](const VkMemoryRequirements& requirements, VulkanSwapchainReadbackResolutionCode code)
    {
        FakeState state;
        state.mRequirements = requirements;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        ensureCode(resolveVulkanSwapchainReadbackGeneration(parents.mPhysical, parents.mLogical, parents.mConfiguration, parents.mSwapchain,
                                                            parents.mImages),
                   code);
        ensure("requirements failure destroys only the created buffer",
               state.mReadbackTeardownOrder == std::vector<std::string>{ "destroy-buffer" });
    };
    const VkDeviceSize bytes = 1280 * 720 * 4;
    run_requirements_case({ 0, 256, 0xf }, VulkanSwapchainReadbackResolutionCode::InvalidBufferMemoryRequirements);
    run_requirements_case({ bytes - 1, 256, 0xf }, VulkanSwapchainReadbackResolutionCode::InvalidBufferMemoryRequirements);
    run_requirements_case({ bytes, 0, 0xf }, VulkanSwapchainReadbackResolutionCode::InvalidBufferMemoryRequirements);
    run_requirements_case({ bytes, 3, 0xf }, VulkanSwapchainReadbackResolutionCode::InvalidBufferMemoryRequirements);
    run_requirements_case({ bytes, 256, 0 }, VulkanSwapchainReadbackResolutionCode::InvalidBufferMemoryRequirements);
    run_requirements_case({ bytes, 256, 0x3 }, VulkanSwapchainReadbackResolutionCode::NoCompatibleMemoryType);

    {
        FakeState state;
        state.mMemoryProperties.memoryTypes[2].propertyFlags =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD;
        state.mRequirements.memoryTypeBits = (1U << 2);
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        ensureCode(resolveVulkanSwapchainReadbackGeneration(parents.mPhysical, parents.mLogical, parents.mConfiguration, parents.mSwapchain,
                                                            parents.mImages),
                   VulkanSwapchainReadbackResolutionCode::NoCompatibleMemoryType);
        ensure("a disabled AMD device-coherent type is never allocated",
               state.mAllocateInfos.empty() && state.mReadbackTeardownOrder == std::vector<std::string>{ "destroy-buffer" });
    }
    {
        FakeState state;
        state.mMemoryProperties.memoryHeaps[1].flags = VK_MEMORY_HEAP_TILE_MEMORY_BIT_QCOM;
        state.mRequirements.memoryTypeBits           = (1U << 2);
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        ensureCode(resolveVulkanSwapchainReadbackGeneration(parents.mPhysical, parents.mLogical, parents.mConfiguration, parents.mSwapchain,
                                                            parents.mImages),
                   VulkanSwapchainReadbackResolutionCode::NoCompatibleMemoryType);
        ensure("a disabled QCOM tile-memory heap is never allocated",
               state.mAllocateInfos.empty() && state.mReadbackTeardownOrder == std::vector<std::string>{ "destroy-buffer" });
    }
}

template<>
template<>
void render_vulkan_swapchain_readback_object::test<6>()
{
    {
        FakeState state;
        state.mCreateResult = VK_ERROR_OUT_OF_DEVICE_MEMORY;
        state.mCreateOutput = fakeHandle<VkBuffer>(0xdead);
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        const auto      result  = resolveVulkanSwapchainReadbackGeneration(parents.mPhysical, parents.mLogical, parents.mConfiguration,
                                                                           parents.mSwapchain, parents.mImages);
        const auto&     error   = requireError(result);
        ensure("buffer failure preserves the exact native result",
               error.mCode == VulkanSwapchainReadbackResolutionCode::BufferCreationFailure &&
                   error.mCommand == VulkanSwapchainReadbackCommand::CreateBuffer && error.mResult == VK_ERROR_OUT_OF_DEVICE_MEMORY);
        ensure("poisoned failed buffer output is ignored", state.mReadbackTeardownOrder.empty());
    }
    {
        FakeState state;
        state.mCreateOutput = VK_NULL_HANDLE;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        ensureCode(resolveVulkanSwapchainReadbackGeneration(parents.mPhysical, parents.mLogical, parents.mConfiguration, parents.mSwapchain,
                                                            parents.mImages),
                   VulkanSwapchainReadbackResolutionCode::NullBufferOnSuccess);
        ensure("null successful buffer needs no destroy", state.mReadbackTeardownOrder.empty());
    }
    {
        FakeState state;
        state.mAllocateResult = VK_ERROR_OUT_OF_DEVICE_MEMORY;
        state.mAllocateOutput = fakeHandle<VkDeviceMemory>(0xdead);
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        const auto      result  = resolveVulkanSwapchainReadbackGeneration(parents.mPhysical, parents.mLogical, parents.mConfiguration,
                                                                           parents.mSwapchain, parents.mImages);
        const auto&     error   = requireError(result);
        ensure("allocation failure preserves the exact native result",
               error.mCode == VulkanSwapchainReadbackResolutionCode::MemoryAllocationFailure &&
                   error.mCommand == VulkanSwapchainReadbackCommand::AllocateMemory && error.mResult == VK_ERROR_OUT_OF_DEVICE_MEMORY);
        ensure("poisoned failed allocation is ignored while the buffer rolls back",
               state.mReadbackTeardownOrder == std::vector<std::string>{ "destroy-buffer" } && state.mFreedMemories.empty());
    }
    {
        FakeState state;
        state.mAllocateOutput = VK_NULL_HANDLE;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        ensureCode(resolveVulkanSwapchainReadbackGeneration(parents.mPhysical, parents.mLogical, parents.mConfiguration, parents.mSwapchain,
                                                            parents.mImages),
                   VulkanSwapchainReadbackResolutionCode::NullMemoryOnSuccess);
        ensure("null successful allocation rolls back only the buffer",
               state.mReadbackTeardownOrder == std::vector<std::string>{ "destroy-buffer" });
    }
}

template<>
template<>
void render_vulkan_swapchain_readback_object::test<7>()
{
    {
        FakeState state;
        state.mBindResult = VK_ERROR_MEMORY_MAP_FAILED;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        const auto      result  = resolveVulkanSwapchainReadbackGeneration(parents.mPhysical, parents.mLogical, parents.mConfiguration,
                                                                           parents.mSwapchain, parents.mImages);
        const auto&     error   = requireError(result);
        ensure("bind failure is typed with the native result",
               error.mCode == VulkanSwapchainReadbackResolutionCode::BufferMemoryBindFailure &&
                   error.mCommand == VulkanSwapchainReadbackCommand::BindBufferMemory && error.mResult == VK_ERROR_MEMORY_MAP_FAILED);
        ensure("bind failure destroys the buffer before freeing memory",
               state.mReadbackTeardownOrder == std::vector<std::string>{ "destroy-buffer", "free-memory" });
    }
    {
        FakeState state;
        state.mMapResult = VK_ERROR_MEMORY_MAP_FAILED;
        state.mMapOutput = reinterpret_cast<void*>(0xdead);
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        const auto      result  = resolveVulkanSwapchainReadbackGeneration(parents.mPhysical, parents.mLogical, parents.mConfiguration,
                                                                           parents.mSwapchain, parents.mImages);
        const auto&     error   = requireError(result);
        ensure("map failure is typed with the native result",
               error.mCode == VulkanSwapchainReadbackResolutionCode::MemoryMapFailure &&
                   error.mCommand == VulkanSwapchainReadbackCommand::MapMemory && error.mResult == VK_ERROR_MEMORY_MAP_FAILED);
        ensure("poisoned failed map output is not unmapped",
               state.mReadbackTeardownOrder == std::vector<std::string>{ "destroy-buffer", "free-memory" } &&
                   state.mUnmappedMemories.empty());
    }
    {
        FakeState state;
        state.mMapOutput = nullptr;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        ensureCode(resolveVulkanSwapchainReadbackGeneration(parents.mPhysical, parents.mLogical, parents.mConfiguration, parents.mSwapchain,
                                                            parents.mImages),
                   VulkanSwapchainReadbackResolutionCode::NullMappedDataOnSuccess);
        ensure("successful null mapping rolls back the live mapping first",
               state.mReadbackTeardownOrder == std::vector<std::string>{ "unmap", "destroy-buffer", "free-memory" });
    }
}

template<>
template<>
void render_vulkan_swapchain_readback_object::test<8>()
{
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents   = makeParents(state);
        state.mImagesToInvalidate = &parents.mImages;
        ensureCode(resolveVulkanSwapchainReadbackGeneration(parents.mPhysical, parents.mLogical, parents.mConfiguration, parents.mSwapchain,
                                                            parents.mImages),
                   VulkanSwapchainReadbackResolutionCode::InvalidSwapchainImagesGeneration);
        ensure("post-map reauthentication rolls back in exact order",
               state.mReadbackTeardownOrder == std::vector<std::string>{ "unmap", "destroy-buffer", "free-memory" });
        state.mMovedImages.reset();
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        auto readback = takeReadback(resolveVulkanSwapchainReadbackGeneration(parents.mPhysical, parents.mLogical, parents.mConfiguration,
                                                                              parents.mSwapchain, parents.mImages));
        state.mInstanceLookups.clear();
        state.mDeviceLookups.clear();
        auto second_images =
            takeImages(resolveVulkanSwapchainImagesGeneration(parents.mLogical, parents.mConfiguration, parents.mSwapchain));
        ensure("a distinct images generation with identical native values is rejected",
               !readback.createdFor(parents.mPhysical, parents.mLogical, parents.mConfiguration, parents.mSwapchain, second_images));
        readback.reset();
    }
}

} // namespace tut
