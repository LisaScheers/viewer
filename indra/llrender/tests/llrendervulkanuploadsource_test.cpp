/**
 * @file llrendervulkanuploadsource_test.cpp
 * @brief Tests for immutable Vulkan upload-source ownership.
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

#include "llrendervulkanuploadsource.h"
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
    UnmapMemory,
    FlushMappedMemoryRanges
};

enum class InvalidateAt : std::uint8_t
{
    None,
    MemoryProperties,
    CreateBuffer,
    MemoryRequirements,
    AllocateMemory,
    BindBufferMemory,
    MapMemory,
    FlushMemory,
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

struct FlushRecord
{
    VkDevice            mDevice     = VK_NULL_HANDLE;
    std::uint32_t       mRangeCount = 0;
    VkMappedMemoryRange mRange{};
};

struct FakeState
{
    VkInstance       mInstance       = fakeHandle<VkInstance>(0x1000);
    VkSurfaceKHR     mSurface        = fakeHandle<VkSurfaceKHR>(0x2000);
    VkPhysicalDevice mPhysicalDevice = fakeHandle<VkPhysicalDevice>(0x3000);
    VkDevice         mDevice         = fakeHandle<VkDevice>(0x4000);
    VkQueue          mQueue          = fakeHandle<VkQueue>(0x5000);
    VkBuffer         mBuffer         = fakeHandle<VkBuffer>(0x6000);
    VkDeviceMemory   mMemory         = fakeHandle<VkDeviceMemory>(0x7000);
    std::uint32_t    mQueueFamily    = 2;

    MissingCommand mMissingCommand = MissingCommand::None;
    InvalidateAt   mInvalidateAt   = InvalidateAt::None;

    VkPhysicalDeviceMemoryProperties mMemoryProperties{};
    VkMemoryRequirements             mRequirements{ 64, 16, 0xf };
    VkResult                         mCreateResult   = VK_SUCCESS;
    VkBuffer                         mCreateOutput   = mBuffer;
    VkResult                         mAllocateResult = VK_SUCCESS;
    VkDeviceMemory                   mAllocateOutput = mMemory;
    VkResult                         mBindResult     = VK_SUCCESS;
    VkResult                         mMapResult      = VK_SUCCESS;
    VkResult                         mFlushResult    = VK_SUCCESS;
    std::array<std::uint8_t, 64>     mMappedStorage{};
    void*                            mMapOutput = mMappedStorage.data();

    std::vector<std::string> mInstanceLookups;
    std::vector<std::string> mDeviceLookups;
    bool                     mAllCommandsResolvedBeforeMutation = false;

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
    std::vector<FlushRecord>          mFlushRecords;
    std::vector<VkDeviceMemory>       mUnmappedMemories;
    std::vector<VkBuffer>             mDestroyedBuffers;
    std::vector<VkDeviceMemory>       mFreedMemories;
    std::vector<std::string>          mTeardownOrder;
    std::size_t                       mDestroyDeviceCalls = 0;

    VulkanLogicalDeviceGeneration*               mLogicalToInvalidate = nullptr;
    std::optional<VulkanLogicalDeviceGeneration> mMovedLogical;
    VulkanUploadSourceDescription*               mDescriptionToMutate = nullptr;

    FakeState()
    {
        mMappedStorage.fill(0xa5);
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

void invalidateLogical(InvalidateAt point) noexcept
{
    if (gFakeState && gFakeState->mInvalidateAt == point && gFakeState->mLogicalToInvalidate)
    {
        gFakeState->mMovedLogical.emplace(std::move(*gFakeState->mLogicalToInvalidate));
        gFakeState->mLogicalToInvalidate = nullptr;
    }
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
    std::strncpy(properties->deviceName, "upload-source-fake", VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 1);
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
        maintenance->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_KHR)
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

VKAPI_ATTR void VKAPI_CALL fakeGetPhysicalDeviceMemoryProperties(VkPhysicalDevice                  physical_device,
                                                                 VkPhysicalDeviceMemoryProperties* properties) noexcept
{
    if (gFakeState && physical_device == gFakeState->mPhysicalDevice && properties)
    {
        *properties = gFakeState->mMemoryProperties;
        invalidateLogical(InvalidateAt::MemoryProperties);
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateBuffer(VkDevice                     device,
                                                const VkBufferCreateInfo*    create_info,
                                                const VkAllocationCallbacks* allocator,
                                                VkBuffer*                    buffer) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || !create_info || !buffer)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    gFakeState->mAllCommandsResolvedBeforeMutation =
        gFakeState->mInstanceLookups == std::vector<std::string>{ "vkGetPhysicalDeviceMemoryProperties", "vkGetDeviceProcAddr" } &&
        gFakeState->mDeviceLookups == std::vector<std::string>{ "vkCreateBuffer",   "vkDestroyBuffer", "vkGetBufferMemoryRequirements",
                                                                "vkAllocateMemory", "vkFreeMemory",    "vkBindBufferMemory",
                                                                "vkMapMemory",      "vkUnmapMemory",   "vkFlushMappedMemoryRanges" };
    gFakeState->mBufferRecords.push_back({ create_info->sType, create_info->pNext, create_info->flags, create_info->size,
                                           create_info->usage, create_info->sharingMode, create_info->queueFamilyIndexCount,
                                           create_info->pQueueFamilyIndices });
    gFakeState->mCreateDevices.push_back(device);
    gFakeState->mCreateAllocatorNull.push_back(allocator == nullptr);
    *buffer = gFakeState->mCreateOutput;
    if (gFakeState->mDescriptionToMutate)
    {
        gFakeState->mDescriptionToMutate->mHandle = { 99, 99 };
        gFakeState->mDescriptionToMutate->mBytes.fill(0xee);
    }
    invalidateLogical(InvalidateAt::CreateBuffer);
    return gFakeState->mCreateResult;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyBuffer(VkDevice, VkBuffer buffer, const VkAllocationCallbacks*) noexcept
{
    if (gFakeState)
    {
        gFakeState->mDestroyedBuffers.push_back(buffer);
        gFakeState->mTeardownOrder.emplace_back("destroy-buffer");
    }
}

VKAPI_ATTR void VKAPI_CALL fakeGetBufferMemoryRequirements(VkDevice, VkBuffer buffer, VkMemoryRequirements* requirements) noexcept
{
    if (gFakeState && requirements)
    {
        gFakeState->mRequirementBuffers.push_back(buffer);
        *requirements = gFakeState->mRequirements;
        invalidateLogical(InvalidateAt::MemoryRequirements);
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeAllocateMemory(VkDevice                     device,
                                                  const VkMemoryAllocateInfo*  allocate_info,
                                                  const VkAllocationCallbacks* allocator,
                                                  VkDeviceMemory*              memory) noexcept
{
    if (!gFakeState || !allocate_info || !memory)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    gFakeState->mAllocateInfos.push_back(*allocate_info);
    gFakeState->mAllocateDevices.push_back(device);
    gFakeState->mAllocateAllocatorNull.push_back(allocator == nullptr);
    *memory = gFakeState->mAllocateOutput;
    invalidateLogical(InvalidateAt::AllocateMemory);
    return gFakeState->mAllocateResult;
}

VKAPI_ATTR void VKAPI_CALL fakeFreeMemory(VkDevice, VkDeviceMemory memory, const VkAllocationCallbacks*) noexcept
{
    if (gFakeState)
    {
        gFakeState->mFreedMemories.push_back(memory);
        gFakeState->mTeardownOrder.emplace_back("free-memory");
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeBindBufferMemory(VkDevice, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize offset) noexcept
{
    if (!gFakeState)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    gFakeState->mBoundBuffers.push_back(buffer);
    gFakeState->mBoundMemories.push_back(memory);
    gFakeState->mBindOffsets.push_back(offset);
    invalidateLogical(InvalidateAt::BindBufferMemory);
    return gFakeState->mBindResult;
}

VKAPI_ATTR VkResult VKAPI_CALL
    fakeMapMemory(VkDevice, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, VkMemoryMapFlags flags, void** data) noexcept
{
    if (!gFakeState || !data)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    gFakeState->mMappedMemories.push_back(memory);
    gFakeState->mMapOffsets.push_back(offset);
    gFakeState->mMapSizes.push_back(size);
    gFakeState->mMapFlags.push_back(flags);
    *data = gFakeState->mMapOutput;
    invalidateLogical(InvalidateAt::MapMemory);
    return gFakeState->mMapResult;
}

VKAPI_ATTR void VKAPI_CALL fakeUnmapMemory(VkDevice, VkDeviceMemory memory) noexcept
{
    if (gFakeState)
    {
        gFakeState->mUnmappedMemories.push_back(memory);
        gFakeState->mTeardownOrder.emplace_back("unmap");
        invalidateLogical(InvalidateAt::UnmapMemory);
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeFlushMappedMemoryRanges(VkDevice                   device,
                                                           std::uint32_t              range_count,
                                                           const VkMappedMemoryRange* ranges) noexcept
{
    if (!gFakeState || range_count == 0 || !ranges)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    gFakeState->mFlushRecords.push_back({ device, range_count, ranges[0] });
    invalidateLogical(InvalidateAt::FlushMemory);
    return gFakeState->mFlushResult;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL fakeGetDeviceProcAddr(VkDevice device, const char* name) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || !name)
    {
        return nullptr;
    }
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
    if (std::strcmp(name, "vkFlushMappedMemoryRanges") == 0)
        return gFakeState->mMissingCommand == MissingCommand::FlushMappedMemoryRanges ? nullptr
                                                                                      : eraseFunctionType(fakeFlushMappedMemoryRanges);
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
    VulkanPhysicalDeviceGeneration mPhysical;
    VulkanLogicalDeviceGeneration  mLogical;
};

Parents makeParents(FakeState& state)
{
    auto physical_result = resolveVulkanPhysicalDeviceGeneration({ fakeGetInstanceProcAddr, state.mInstance, state.mSurface });
    tut::ensure("the physical-device fixture resolves", std::holds_alternative<VulkanPhysicalDeviceGeneration>(physical_result));
    auto physical = std::get<VulkanPhysicalDeviceGeneration>(std::move(physical_result));

    auto logical_result = resolveVulkanLogicalDeviceGeneration(physical);
    tut::ensure("the logical-device fixture resolves", std::holds_alternative<VulkanLogicalDeviceGeneration>(logical_result));
    auto logical = std::get<VulkanLogicalDeviceGeneration>(std::move(logical_result));

    state.mInstanceLookups.clear();
    state.mDeviceLookups.clear();
    return { std::move(physical), std::move(logical) };
}

VulkanUploadSourceDescription description()
{
    VulkanUploadSourceDescription result;
    result.mHandle = { 7, 3 };
    for (std::size_t index = 0; index < result.mBytes.size(); ++index)
    {
        result.mBytes[index] = static_cast<std::uint8_t>(index * 3 + 1);
    }
    return result;
}

const VulkanUploadSourceResolutionError& requireError(const VulkanUploadSourceResolutionResult& result)
{
    const auto* error = std::get_if<VulkanUploadSourceResolutionError>(&result);
    tut::ensure("upload-source resolution returns an error", error != nullptr);
    return *error;
}

void ensureError(const VulkanUploadSourceResolutionResult& result,
                 VulkanUploadSourceResolutionCode          code,
                 std::optional<VulkanUploadSourceCommand>  command       = std::nullopt,
                 VkResult                                  native_result = VK_SUCCESS)
{
    const auto& error = requireError(result);
    tut::ensure("the exact upload-source error code is reported", error.mCode == code);
    tut::ensure("the exact upload-source command is reported", error.mCommand == command);
    tut::ensure("the exact native result is reported", error.mResult == native_result);
}

VulkanUploadSourceGeneration takeGeneration(VulkanUploadSourceResolutionResult&& result)
{
    tut::ensure("upload-source resolution returns a generation", std::holds_alternative<VulkanUploadSourceGeneration>(result));
    return std::get<VulkanUploadSourceGeneration>(std::move(result));
}

} // namespace

namespace tut
{

struct render_vulkan_upload_source_test
{
};

using render_vulkan_upload_source_group  = test_group<render_vulkan_upload_source_test>;
using render_vulkan_upload_source_object = render_vulkan_upload_source_group::object;
render_vulkan_upload_source_group render_vulkan_upload_source_tests("render Vulkan upload source");

template<>
template<>
void render_vulkan_upload_source_object::test<1>()
{
    static_assert(VULKAN_UPLOAD_SOURCE_BYTE_COUNT == 48);
    static_assert(!std::is_default_constructible_v<VulkanUploadSourceGeneration>);
    static_assert(!std::is_copy_constructible_v<VulkanUploadSourceGeneration>);
    static_assert(!std::is_copy_assignable_v<VulkanUploadSourceGeneration>);
    static_assert(std::is_nothrow_move_constructible_v<VulkanUploadSourceGeneration>);
    static_assert(!std::is_move_assignable_v<VulkanUploadSourceGeneration>);
    static_assert(noexcept(resolveVulkanUploadSourceGeneration(std::declval<const VulkanPhysicalDeviceGeneration&>(),
                                                               std::declval<const VulkanLogicalDeviceGeneration&>(),
                                                               std::declval<const VulkanUploadSourceDescription&>())));

    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents        = makeParents(state);
        auto            moved_physical = std::move(parents.mPhysical);
        ensureError(resolveVulkanUploadSourceGeneration(parents.mPhysical, parents.mLogical, description()),
                    VulkanUploadSourceResolutionCode::InvalidPhysicalDeviceGeneration);
        ensure("an invalid physical parent performs no upload lookup", state.mInstanceLookups.empty() && state.mDeviceLookups.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents       = makeParents(state);
        auto            moved_logical = std::move(parents.mLogical);
        ensureError(resolveVulkanUploadSourceGeneration(parents.mPhysical, parents.mLogical, description()),
                    VulkanUploadSourceResolutionCode::InvalidLogicalDeviceGeneration);
        ensure("an invalid logical parent performs no upload lookup", state.mInstanceLookups.empty() && state.mDeviceLookups.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        auto            invalid = description();
        invalid.mHandle         = {};
        ensureError(resolveVulkanUploadSourceGeneration(parents.mPhysical, parents.mLogical, invalid),
                    VulkanUploadSourceResolutionCode::InvalidDescription);
        ensure("an invalid description performs no upload lookup", state.mInstanceLookups.empty() && state.mDeviceLookups.empty());
    }
}

template<>
template<>
void render_vulkan_upload_source_object::test<2>()
{
    struct MissingCase
    {
        MissingCommand            mMissing;
        VulkanUploadSourceCommand mExpected;
    };
    constexpr std::array cases{
        MissingCase{ MissingCommand::GetPhysicalDeviceMemoryProperties, VulkanUploadSourceCommand::GetPhysicalDeviceMemoryProperties },
        MissingCase{ MissingCommand::GetDeviceProcAddr, VulkanUploadSourceCommand::GetDeviceProcAddr },
        MissingCase{ MissingCommand::CreateBuffer, VulkanUploadSourceCommand::CreateBuffer },
        MissingCase{ MissingCommand::DestroyBuffer, VulkanUploadSourceCommand::DestroyBuffer },
        MissingCase{ MissingCommand::GetBufferMemoryRequirements, VulkanUploadSourceCommand::GetBufferMemoryRequirements },
        MissingCase{ MissingCommand::AllocateMemory, VulkanUploadSourceCommand::AllocateMemory },
        MissingCase{ MissingCommand::FreeMemory, VulkanUploadSourceCommand::FreeMemory },
        MissingCase{ MissingCommand::BindBufferMemory, VulkanUploadSourceCommand::BindBufferMemory },
        MissingCase{ MissingCommand::MapMemory, VulkanUploadSourceCommand::MapMemory },
        MissingCase{ MissingCommand::UnmapMemory, VulkanUploadSourceCommand::UnmapMemory },
        MissingCase{ MissingCommand::FlushMappedMemoryRanges, VulkanUploadSourceCommand::FlushMappedMemoryRanges },
    };

    for (const MissingCase& test_case : cases)
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        state.mMissingCommand   = test_case.mMissing;
        const auto result       = resolveVulkanUploadSourceGeneration(parents.mPhysical, parents.mLogical, description());
        ensureError(result, VulkanUploadSourceResolutionCode::MissingRequiredCommand, test_case.mExpected);
        ensure("command resolution fails before the first buffer mutation", state.mBufferRecords.empty());
    }
}

template<>
template<>
void render_vulkan_upload_source_object::test<3>()
{
    FakeState       state;
    ScopedFakeState scope(state);
    auto            parents = makeParents(state);
    const auto      request = description();

    auto generation = takeGeneration(resolveVulkanUploadSourceGeneration(parents.mPhysical, parents.mLogical, request));
    ensure("all commands resolve before buffer creation", state.mAllCommandsResolvedBeforeMutation);
    ensure("one exact transfer-source buffer is created",
           state.mBufferRecords.size() == 1 && state.mBufferRecords[0].mStructureType == VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO &&
               state.mBufferRecords[0].mNext == nullptr && state.mBufferRecords[0].mFlags == 0 &&
               state.mBufferRecords[0].mSize == VULKAN_UPLOAD_SOURCE_BYTE_COUNT &&
               state.mBufferRecords[0].mUsage == VK_BUFFER_USAGE_TRANSFER_SRC_BIT &&
               state.mBufferRecords[0].mSharingMode == VK_SHARING_MODE_EXCLUSIVE && state.mBufferRecords[0].mQueueFamilyIndexCount == 0 &&
               state.mBufferRecords[0].mQueueFamilyIndices == nullptr && state.mCreateDevices == std::vector<VkDevice>{ state.mDevice } &&
               state.mCreateAllocatorNull == std::vector<bool>{ true });
    ensure("the dedicated allocation uses the coherent preference",
           state.mAllocateInfos.size() == 1 && state.mAllocateInfos[0].sType == VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO &&
               state.mAllocateInfos[0].pNext == nullptr && state.mAllocateInfos[0].allocationSize == state.mRequirements.size &&
               state.mAllocateInfos[0].memoryTypeIndex == 2 && state.mAllocateDevices == std::vector<VkDevice>{ state.mDevice } &&
               state.mAllocateAllocatorNull == std::vector<bool>{ true });
    ensure("the allocation is bound at zero",
           state.mBoundBuffers == std::vector<VkBuffer>{ state.mBuffer } &&
               state.mBoundMemories == std::vector<VkDeviceMemory>{ state.mMemory } &&
               state.mBindOffsets == std::vector<VkDeviceSize>{ 0 });
    ensure("the whole allocation is mapped and unmapped before publication",
           state.mMappedMemories == std::vector<VkDeviceMemory>{ state.mMemory } && state.mMapOffsets == std::vector<VkDeviceSize>{ 0 } &&
               state.mMapSizes == std::vector<VkDeviceSize>{ VK_WHOLE_SIZE } && state.mMapFlags == std::vector<VkMemoryMapFlags>{ 0 } &&
               state.mUnmappedMemories == std::vector<VkDeviceMemory>{ state.mMemory } && state.mFlushRecords.empty());
    ensure("exactly 48 immutable bytes are copied",
           std::equal(request.mBytes.begin(), request.mBytes.end(), state.mMappedStorage.begin()) &&
               std::all_of(state.mMappedStorage.begin() + VULKAN_UPLOAD_SOURCE_BYTE_COUNT,
                           state.mMappedStorage.end(),
                           [](std::uint8_t byte) { return byte == 0xa5; }));
    ensure("the generation publishes exact metadata and deterministic identity",
           generation.resourceHandle() == request.mHandle && generation.contentIdentity() == 0x8716768a827af1a5ULL &&
               generation.matchesDescription(request) && generation.buffer() == state.mBuffer && generation.memory() == state.mMemory &&
               generation.byteCount() == VULKAN_UPLOAD_SOURCE_BYTE_COUNT && generation.allocationSize() == state.mRequirements.size &&
               generation.memoryTypeIndex() == 2 &&
               generation.memoryPropertyFlags() == state.mMemoryProperties.memoryTypes[2].propertyFlags && generation.isCoherent() &&
               generation.createdFor(parents.mPhysical, parents.mLogical));

    auto wrong_bytes = request;
    wrong_bytes.mBytes[5] ^= 0xff;
    auto wrong_handle = request;
    ++wrong_handle.mHandle.mGeneration;
    ensure("description matching authenticates both handle and exact bytes",
           !generation.matchesDescription(wrong_bytes) && !generation.matchesDescription(wrong_handle));
}

template<>
template<>
void render_vulkan_upload_source_object::test<4>()
{
    FakeState       state;
    ScopedFakeState scope(state);
    auto            parents = makeParents(state);

    state.mMemoryProperties.memoryTypes[0] = {
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_PROTECTED_BIT, 0
    };
    state.mMemoryProperties.memoryTypes[1] = {
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT, 0
    };
    state.mMemoryProperties.memoryTypes[2] = {
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD, 0
    };
    state.mMemoryProperties.memoryTypes[3] = { VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, 1 };
    state.mMemoryProperties.memoryHeaps[0].flags |= VK_MEMORY_HEAP_TILE_MEMORY_BIT_QCOM;

    auto generation = takeGeneration(resolveVulkanUploadSourceGeneration(parents.mPhysical, parents.mLogical, description()));
    ensure("malformed or unsupported coherent candidates fall back to ordinary host-visible memory",
           generation.memoryTypeIndex() == 3 && generation.memoryPropertyFlags() == VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT &&
               !generation.isCoherent());
    ensure("a noncoherent whole-allocation mapping receives one exact flush",
           state.mFlushRecords.size() == 1 && state.mFlushRecords[0].mDevice == state.mDevice && state.mFlushRecords[0].mRangeCount == 1 &&
               state.mFlushRecords[0].mRange.sType == VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE &&
               state.mFlushRecords[0].mRange.pNext == nullptr && state.mFlushRecords[0].mRange.memory == state.mMemory &&
               state.mFlushRecords[0].mRange.offset == 0 && state.mFlushRecords[0].mRange.size == VK_WHOLE_SIZE &&
               state.mUnmappedMemories == std::vector<VkDeviceMemory>{ state.mMemory });
}

template<>
template<>
void render_vulkan_upload_source_object::test<5>()
{
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents                 = makeParents(state);
        state.mMemoryProperties.memoryTypeCount = 0;
        ensureError(resolveVulkanUploadSourceGeneration(parents.mPhysical, parents.mLogical, description()),
                    VulkanUploadSourceResolutionCode::InvalidPhysicalDeviceMemoryProperties,
                    VulkanUploadSourceCommand::GetPhysicalDeviceMemoryProperties);
        ensure("an invalid memory table creates no buffer", state.mBufferRecords.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents                          = makeParents(state);
        state.mMemoryProperties.memoryTypes[1].heapIndex = state.mMemoryProperties.memoryHeapCount;
        ensureError(resolveVulkanUploadSourceGeneration(parents.mPhysical, parents.mLogical, description()),
                    VulkanUploadSourceResolutionCode::InvalidPhysicalDeviceMemoryProperties,
                    VulkanUploadSourceCommand::GetPhysicalDeviceMemoryProperties);
    }
    for (const VkMemoryRequirements requirements :
         std::array{ VkMemoryRequirements{ 47, 16, 1 }, VkMemoryRequirements{ 64, 3, 1 }, VkMemoryRequirements{ 64, 16, 0 } })
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        state.mRequirements     = requirements;
        const auto result       = resolveVulkanUploadSourceGeneration(parents.mPhysical, parents.mLogical, description());
        ensureError(result,
                    VulkanUploadSourceResolutionCode::InvalidBufferMemoryRequirements,
                    VulkanUploadSourceCommand::GetBufferMemoryRequirements);
        ensure("invalid requirements destroy only the created buffer",
               state.mDestroyedBuffers == std::vector<VkBuffer>{ state.mBuffer } && state.mFreedMemories.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents            = makeParents(state);
        state.mRequirements.memoryTypeBits = 1;
        ensureError(resolveVulkanUploadSourceGeneration(parents.mPhysical, parents.mLogical, description()),
                    VulkanUploadSourceResolutionCode::NoCompatibleMemoryType);
        ensure("an incompatible memory table destroys only the created buffer",
               state.mDestroyedBuffers == std::vector<VkBuffer>{ state.mBuffer } && state.mFreedMemories.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents                     = makeParents(state);
        state.mMemoryProperties.memoryHeaps[1].size = state.mRequirements.size - 1;
        state.mRequirements.memoryTypeBits          = std::uint32_t{ 1 } << 2;
        ensureError(resolveVulkanUploadSourceGeneration(parents.mPhysical, parents.mLogical, description()),
                    VulkanUploadSourceResolutionCode::NoCompatibleMemoryType);
    }
}

template<>
template<>
void render_vulkan_upload_source_object::test<6>()
{
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        state.mCreateResult     = VK_ERROR_OUT_OF_HOST_MEMORY;
        state.mCreateOutput     = fakeHandle<VkBuffer>(0xdead);
        ensureError(resolveVulkanUploadSourceGeneration(parents.mPhysical, parents.mLogical, description()),
                    VulkanUploadSourceResolutionCode::BufferCreationFailure,
                    VulkanUploadSourceCommand::CreateBuffer,
                    VK_ERROR_OUT_OF_HOST_MEMORY);
        ensure("undefined failed-create output is never destroyed", state.mDestroyedBuffers.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        state.mCreateOutput     = VK_NULL_HANDLE;
        ensureError(resolveVulkanUploadSourceGeneration(parents.mPhysical, parents.mLogical, description()),
                    VulkanUploadSourceResolutionCode::NullBufferOnSuccess,
                    VulkanUploadSourceCommand::CreateBuffer);
        ensure("a null successful buffer output is not destroyed", state.mDestroyedBuffers.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        state.mAllocateResult   = VK_ERROR_OUT_OF_DEVICE_MEMORY;
        state.mAllocateOutput   = fakeHandle<VkDeviceMemory>(0xdead);
        ensureError(resolveVulkanUploadSourceGeneration(parents.mPhysical, parents.mLogical, description()),
                    VulkanUploadSourceResolutionCode::MemoryAllocationFailure,
                    VulkanUploadSourceCommand::AllocateMemory,
                    VK_ERROR_OUT_OF_DEVICE_MEMORY);
        ensure("undefined failed-allocation output is never freed",
               state.mDestroyedBuffers == std::vector<VkBuffer>{ state.mBuffer } && state.mFreedMemories.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        state.mAllocateOutput   = VK_NULL_HANDLE;
        ensureError(resolveVulkanUploadSourceGeneration(parents.mPhysical, parents.mLogical, description()),
                    VulkanUploadSourceResolutionCode::NullMemoryOnSuccess,
                    VulkanUploadSourceCommand::AllocateMemory);
        ensure("a null successful allocation rolls back the buffer only",
               state.mDestroyedBuffers == std::vector<VkBuffer>{ state.mBuffer } && state.mFreedMemories.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        state.mBindResult       = VK_ERROR_MEMORY_MAP_FAILED;
        ensureError(resolveVulkanUploadSourceGeneration(parents.mPhysical, parents.mLogical, description()),
                    VulkanUploadSourceResolutionCode::BufferMemoryBindFailure,
                    VulkanUploadSourceCommand::BindBufferMemory,
                    VK_ERROR_MEMORY_MAP_FAILED);
        ensure("bind failure rolls back buffer before memory",
               state.mTeardownOrder == std::vector<std::string>{ "destroy-buffer", "free-memory" });
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        state.mMapResult        = VK_ERROR_MEMORY_MAP_FAILED;
        state.mMapOutput        = reinterpret_cast<void*>(0xdead);
        ensureError(resolveVulkanUploadSourceGeneration(parents.mPhysical, parents.mLogical, description()),
                    VulkanUploadSourceResolutionCode::MemoryMapFailure,
                    VulkanUploadSourceCommand::MapMemory,
                    VK_ERROR_MEMORY_MAP_FAILED);
        ensure("failed mapping is not unmapped and rolls back ownership",
               state.mUnmappedMemories.empty() && state.mTeardownOrder == std::vector<std::string>{ "destroy-buffer", "free-memory" });
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        state.mMapOutput        = nullptr;
        ensureError(resolveVulkanUploadSourceGeneration(parents.mPhysical, parents.mLogical, description()),
                    VulkanUploadSourceResolutionCode::NullMappedDataOnSuccess,
                    VulkanUploadSourceCommand::MapMemory);
        ensure("a null successful mapping is unmapped before ownership rollback",
               state.mTeardownOrder == std::vector<std::string>{ "unmap", "destroy-buffer", "free-memory" });
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents            = makeParents(state);
        state.mRequirements.memoryTypeBits = std::uint32_t{ 1 } << 1;
        state.mFlushResult                 = VK_ERROR_MEMORY_MAP_FAILED;
        ensureError(resolveVulkanUploadSourceGeneration(parents.mPhysical, parents.mLogical, description()),
                    VulkanUploadSourceResolutionCode::MemoryFlushFailure,
                    VulkanUploadSourceCommand::FlushMappedMemoryRanges,
                    VK_ERROR_MEMORY_MAP_FAILED);
        ensure("flush failure unmaps before ownership rollback",
               state.mTeardownOrder == std::vector<std::string>{ "unmap", "destroy-buffer", "free-memory" });
    }
}

template<>
template<>
void render_vulkan_upload_source_object::test<7>()
{
    FakeState       state;
    ScopedFakeState scope(state);
    auto            parents    = makeParents(state);
    const auto      request    = description();
    auto            generation = takeGeneration(resolveVulkanUploadSourceGeneration(parents.mPhysical, parents.mLogical, request));

    auto moved = std::move(generation);
    ensure("a move makes every ownership accessor on the source inert",
           !generation.resourceHandle() && generation.contentIdentity() == 0 && !generation.matchesDescription(request) &&
               generation.buffer() == VK_NULL_HANDLE && generation.memory() == VK_NULL_HANDLE && generation.byteCount() == 0 &&
               generation.allocationSize() == 0 && generation.memoryTypeIndex() == 0 && generation.memoryPropertyFlags() == 0 &&
               !generation.isCoherent() && !generation.createdFor(parents.mPhysical, parents.mLogical));
    ensure("a move preserves exact ownership and provenance on the destination",
           moved.resourceHandle() == request.mHandle && moved.contentIdentity() == 0x8716768a827af1a5ULL &&
               moved.matchesDescription(request) && moved.buffer() == state.mBuffer && moved.memory() == state.mMemory &&
               moved.byteCount() == VULKAN_UPLOAD_SOURCE_BYTE_COUNT && moved.createdFor(parents.mPhysical, parents.mLogical));

    state.mTeardownOrder.clear();
    moved.reset();
    ensure("reset destroys the buffer before freeing its dedicated allocation",
           state.mTeardownOrder == std::vector<std::string>{ "destroy-buffer", "free-memory" });
    ensure("reset makes every ownership accessor inert",
           !moved.resourceHandle() && moved.contentIdentity() == 0 && !moved.matchesDescription(request) &&
               moved.buffer() == VK_NULL_HANDLE && moved.memory() == VK_NULL_HANDLE && moved.byteCount() == 0 &&
               moved.allocationSize() == 0 && moved.memoryTypeIndex() == 0 && moved.memoryPropertyFlags() == 0 && !moved.isCoherent() &&
               !moved.createdFor(parents.mPhysical, parents.mLogical));
    moved.reset();
    ensure("reset is idempotent", state.mTeardownOrder.size() == 2);
}

template<>
template<>
void render_vulkan_upload_source_object::test<8>()
{
    FakeState       state;
    ScopedFakeState scope(state);
    auto            parents    = makeParents(state);
    auto            request    = description();
    const auto      original   = request;
    state.mDescriptionToMutate = &request;

    auto generation = takeGeneration(resolveVulkanUploadSourceGeneration(parents.mPhysical, parents.mLogical, request));
    ensure("the caller's request was mutated by the adversarial native callback", request != original);
    ensure("resolution owns its immutable description before the first native callback",
           generation.matchesDescription(original) && !generation.matchesDescription(request) &&
               generation.resourceHandle() == original.mHandle && generation.contentIdentity() == 0x8716768a827af1a5ULL &&
               std::equal(original.mBytes.begin(), original.mBytes.end(), state.mMappedStorage.begin()));
}

template<>
template<>
void render_vulkan_upload_source_object::test<9>()
{
    constexpr std::array invalidation_points{ InvalidateAt::MemoryProperties,   InvalidateAt::CreateBuffer,
                                              InvalidateAt::MemoryRequirements, InvalidateAt::AllocateMemory,
                                              InvalidateAt::BindBufferMemory,   InvalidateAt::MapMemory,
                                              InvalidateAt::FlushMemory,        InvalidateAt::UnmapMemory };

    for (InvalidateAt point : invalidation_points)
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents    = makeParents(state);
        state.mInvalidateAt        = point;
        state.mLogicalToInvalidate = &parents.mLogical;
        if (point == InvalidateAt::FlushMemory)
        {
            state.mRequirements.memoryTypeBits = std::uint32_t{ 1 } << 1;
        }

        const auto result = resolveVulkanUploadSourceGeneration(parents.mPhysical, parents.mLogical, description());
        ensureError(result, VulkanUploadSourceResolutionCode::InvalidLogicalDeviceGeneration);
        ensure("a stale parent never publishes a generation", !std::holds_alternative<VulkanUploadSourceGeneration>(result));

        if (point == InvalidateAt::MemoryProperties)
        {
            ensure("pre-mutation parent invalidation owns nothing", state.mDestroyedBuffers.empty() && state.mFreedMemories.empty());
        }
        else
        {
            ensure("post-creation parent invalidation destroys the buffer",
                   state.mDestroyedBuffers == std::vector<VkBuffer>{ state.mBuffer });
        }
        if (point == InvalidateAt::AllocateMemory || point == InvalidateAt::BindBufferMemory || point == InvalidateAt::MapMemory ||
            point == InvalidateAt::FlushMemory || point == InvalidateAt::UnmapMemory)
        {
            ensure("post-allocation parent invalidation frees the allocation",
                   state.mFreedMemories == std::vector<VkDeviceMemory>{ state.mMemory });
        }
        else
        {
            ensure("pre-allocation parent invalidation frees no memory", state.mFreedMemories.empty());
        }
        if (point == InvalidateAt::MapMemory || point == InvalidateAt::FlushMemory || point == InvalidateAt::UnmapMemory)
        {
            ensure("mapped stale-parent rollback performs exactly one unmap",
                   state.mUnmappedMemories == std::vector<VkDeviceMemory>{ state.mMemory });
        }
        else
        {
            ensure("unmapped stale-parent rollback does not unmap", state.mUnmappedMemories.empty());
        }
    }
}

} // namespace tut
