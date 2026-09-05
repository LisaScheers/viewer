/**
 * @file llrendervulkanuploadtransfer_test.cpp
 * @brief Tests for one-shot Vulkan upload transfers.
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

#include "llrendervulkanuploaddestination.h"
#include "llrendervulkanuploadtransfer.h"
#include "lltut.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
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

struct CommandPoolRecord
{
    VkDevice                 mDevice        = VK_NULL_HANDLE;
    VkStructureType          mStructureType = VK_STRUCTURE_TYPE_MAX_ENUM;
    const void*              mNext          = nullptr;
    VkCommandPoolCreateFlags mFlags         = 0;
    std::uint32_t            mQueueFamily   = VK_QUEUE_FAMILY_IGNORED;
    bool                     mAllocatorNull = false;
};

struct CommandBufferRecord
{
    VkDevice             mDevice        = VK_NULL_HANDLE;
    VkStructureType      mStructureType = VK_STRUCTURE_TYPE_MAX_ENUM;
    const void*          mNext          = nullptr;
    VkCommandPool        mPool          = VK_NULL_HANDLE;
    VkCommandBufferLevel mLevel         = VK_COMMAND_BUFFER_LEVEL_MAX_ENUM;
    std::uint32_t        mCount         = 0;
};

struct FenceRecord
{
    VkDevice           mDevice        = VK_NULL_HANDLE;
    VkStructureType    mStructureType = VK_STRUCTURE_TYPE_MAX_ENUM;
    const void*        mNext          = nullptr;
    VkFenceCreateFlags mFlags         = 0;
    bool               mAllocatorNull = false;
};

struct BeginRecord
{
    VkCommandBuffer                       mCommandBuffer = VK_NULL_HANDLE;
    VkStructureType                       mStructureType = VK_STRUCTURE_TYPE_MAX_ENUM;
    const void*                           mNext          = nullptr;
    VkCommandBufferUsageFlags             mFlags         = 0;
    const VkCommandBufferInheritanceInfo* mInheritance   = nullptr;
};

struct BarrierRecord
{
    VkCommandBuffer       mCommandBuffer      = VK_NULL_HANDLE;
    VkPipelineStageFlags  mSourceStage        = 0;
    VkPipelineStageFlags  mDestinationStage   = 0;
    VkDependencyFlags     mDependencyFlags    = 0;
    std::uint32_t         mMemoryBarrierCount = 0;
    std::uint32_t         mBufferBarrierCount = 0;
    std::uint32_t         mImageBarrierCount  = 0;
    VkBufferMemoryBarrier mBufferBarrier{};
};

struct CopyRecord
{
    VkCommandBuffer mCommandBuffer = VK_NULL_HANDLE;
    VkBuffer        mSource        = VK_NULL_HANDLE;
    VkBuffer        mDestination   = VK_NULL_HANDLE;
    std::uint32_t   mRegionCount   = 0;
    VkBufferCopy    mRegion{};
};

struct SubmitRecord
{
    VkQueue         mQueue                = VK_NULL_HANDLE;
    std::uint32_t   mSubmitCount          = 0;
    VkStructureType mStructureType        = VK_STRUCTURE_TYPE_MAX_ENUM;
    const void*     mNext                 = nullptr;
    std::uint32_t   mWaitSemaphoreCount   = 0;
    bool            mWaitSemaphoresNull   = false;
    bool            mWaitStagesNull       = false;
    std::uint32_t   mCommandBufferCount   = 0;
    VkCommandBuffer mCommandBuffer        = VK_NULL_HANDLE;
    std::uint32_t   mSignalSemaphoreCount = 0;
    bool            mSignalSemaphoresNull = false;
    VkFence         mFence                = VK_NULL_HANDLE;
};

struct WaitRecord
{
    VkDevice      mDevice     = VK_NULL_HANDLE;
    std::uint32_t mFenceCount = 0;
    VkFence       mFence      = VK_NULL_HANDLE;
    VkBool32      mWaitAll    = VK_FALSE;
    std::uint64_t mTimeout    = 0;
};

struct FakeState
{
    VkInstance       mInstance          = fakeHandle<VkInstance>(0x1000);
    VkSurfaceKHR     mSurface           = fakeHandle<VkSurfaceKHR>(0x2000);
    VkPhysicalDevice mPhysicalDevice    = fakeHandle<VkPhysicalDevice>(0x3000);
    VkDevice         mDevice            = fakeHandle<VkDevice>(0x4000);
    VkQueue          mQueue             = fakeHandle<VkQueue>(0x5000);
    VkBuffer         mSourceBuffer      = fakeHandle<VkBuffer>(0x6000);
    VkBuffer         mDestinationBuffer = fakeHandle<VkBuffer>(0x6100);
    VkDeviceMemory   mSourceMemory      = fakeHandle<VkDeviceMemory>(0x7000);
    VkDeviceMemory   mDestinationMemory = fakeHandle<VkDeviceMemory>(0x7100);
    VkDeviceMemory   mExtraSourceMemory = fakeHandle<VkDeviceMemory>(0x7200);
    VkCommandPool    mCommandPool       = fakeHandle<VkCommandPool>(0x8000);
    VkCommandBuffer  mCommandBuffer     = fakeHandle<VkCommandBuffer>(0x8100);
    VkFence          mFence             = fakeHandle<VkFence>(0x8200);
    std::uint32_t    mQueueFamily       = 2;

    VkPhysicalDeviceMemoryProperties mMemoryProperties{};
    VkMemoryRequirements             mRequirements{ 64, 16, 0x3 };
    std::array<std::uint8_t, 64>     mMappedStorage{};
    std::uint32_t                    mAllocationCount = 0;

    std::string mMissingCommand;
    std::string mInvalidateAt;

    VkResult              mCreateCommandPoolResult     = VK_SUCCESS;
    VkCommandPool         mCreateCommandPoolOutput     = mCommandPool;
    VkResult              mAllocateCommandBufferResult = VK_SUCCESS;
    VkCommandBuffer       mAllocateCommandBufferOutput = mCommandBuffer;
    VkResult              mCreateFenceResult           = VK_SUCCESS;
    VkFence               mCreateFenceOutput           = mFence;
    VkResult              mBeginResult                 = VK_SUCCESS;
    VkResult              mEndResult                   = VK_SUCCESS;
    VkResult              mSubmitResult                = VK_SUCCESS;
    std::vector<VkResult> mWaitResults{ VK_SUCCESS };
    std::size_t           mNextWaitResult = 0;

    std::vector<std::string>         mInstanceLookups;
    std::vector<std::string>         mDeviceLookups;
    bool                             mAllCommandsResolvedBeforeMutation = false;
    std::vector<std::string>         mCalls;
    std::vector<CommandPoolRecord>   mCommandPoolRecords;
    std::vector<CommandBufferRecord> mCommandBufferRecords;
    std::vector<FenceRecord>         mFenceRecords;
    std::vector<BeginRecord>         mBeginRecords;
    std::vector<BarrierRecord>       mBarrierRecords;
    std::vector<CopyRecord>          mCopyRecords;
    std::vector<VkCommandBuffer>     mEndedCommandBuffers;
    std::vector<SubmitRecord>        mSubmitRecords;
    std::vector<WaitRecord>          mWaitRecords;
    std::vector<VkFence>             mDestroyedFences;
    std::vector<VkDevice>            mDestroyFenceDevices;
    std::vector<VkCommandPool>       mDestroyedCommandPools;
    std::vector<VkDevice>            mDestroyCommandPoolDevices;
    std::vector<VkBuffer>            mDestroyedBuffers;
    std::vector<VkDeviceMemory>      mFreedMemories;
    std::optional<VkBuffer>          mIgnoreNextDestroyedBuffer;
    std::size_t                      mIgnoredBufferDestructionCount = 0;
    std::size_t                      mDestroyDeviceCalls            = 0;
    bool                             mChildDestroyedWithWrongDevice = false;

    VulkanLogicalDeviceGeneration*                   mLogicalToMove     = nullptr;
    VulkanUploadSourceGeneration*                    mSourceToMove      = nullptr;
    VulkanUploadDestinationGeneration*               mDestinationToMove = nullptr;
    std::optional<VulkanPhysicalDeviceGeneration>    mMovedPhysical;
    std::optional<VulkanLogicalDeviceGeneration>     mMovedLogical;
    std::optional<VulkanUploadSourceGeneration>      mMovedSource;
    std::optional<VulkanUploadDestinationGeneration> mMovedDestination;
    VulkanUploadSourceDescription*                   mDescriptionToMutate = nullptr;

    FakeState()
    {
        mMappedStorage.fill(0xa5);
        mMemoryProperties.memoryHeapCount              = 2;
        mMemoryProperties.memoryHeaps[0].size          = 1ULL << 29;
        mMemoryProperties.memoryHeaps[1].size          = 1ULL << 30;
        mMemoryProperties.memoryHeaps[1].flags         = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;
        mMemoryProperties.memoryTypeCount              = 2;
        mMemoryProperties.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        mMemoryProperties.memoryTypes[0].heapIndex     = 0;
        mMemoryProperties.memoryTypes[1].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        mMemoryProperties.memoryTypes[1].heapIndex     = 1;
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

void invalidateAt(std::string_view point) noexcept
{
    if (!gFakeState || gFakeState->mInvalidateAt != point)
    {
        return;
    }
    if (gFakeState->mLogicalToMove)
    {
        gFakeState->mMovedLogical.emplace(std::move(*gFakeState->mLogicalToMove));
        gFakeState->mLogicalToMove = nullptr;
    }
    if (gFakeState->mSourceToMove)
    {
        gFakeState->mMovedSource.emplace(std::move(*gFakeState->mSourceToMove));
        gFakeState->mSourceToMove = nullptr;
    }
    if (gFakeState->mDestinationToMove)
    {
        gFakeState->mMovedDestination.emplace(std::move(*gFakeState->mDestinationToMove));
        gFakeState->mDestinationToMove = nullptr;
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
    std::strncpy(properties->deviceName, "upload-transfer-fake", VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 1);
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
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateBuffer(VkDevice                  device,
                                                const VkBufferCreateInfo* create_info,
                                                const VkAllocationCallbacks*,
                                                VkBuffer* buffer) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || !create_info || !buffer)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (create_info->usage == VK_BUFFER_USAGE_TRANSFER_SRC_BIT)
    {
        *buffer = gFakeState->mSourceBuffer;
    }
    else if (create_info->usage == (VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT))
    {
        *buffer = gFakeState->mDestinationBuffer;
    }
    else
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks*) noexcept
{
    if (gFakeState)
    {
        if (gFakeState->mIgnoreNextDestroyedBuffer && buffer == *gFakeState->mIgnoreNextDestroyedBuffer)
        {
            gFakeState->mIgnoreNextDestroyedBuffer.reset();
            ++gFakeState->mIgnoredBufferDestructionCount;
            return;
        }
        gFakeState->mChildDestroyedWithWrongDevice |= device != gFakeState->mDevice;
        gFakeState->mDestroyedBuffers.push_back(buffer);
    }
}

VKAPI_ATTR void VKAPI_CALL fakeGetBufferMemoryRequirements(VkDevice, VkBuffer, VkMemoryRequirements* requirements) noexcept
{
    if (gFakeState && requirements)
    {
        *requirements = gFakeState->mRequirements;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeAllocateMemory(VkDevice,
                                                  const VkMemoryAllocateInfo*,
                                                  const VkAllocationCallbacks*,
                                                  VkDeviceMemory* memory) noexcept
{
    if (!gFakeState || !memory)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    switch (gFakeState->mAllocationCount++)
    {
        case 0:
            *memory = gFakeState->mSourceMemory;
            break;
        case 1:
            *memory = gFakeState->mDestinationMemory;
            break;
        default:
            *memory = gFakeState->mExtraSourceMemory;
            break;
    }
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeFreeMemory(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks*) noexcept
{
    if (gFakeState)
    {
        gFakeState->mChildDestroyedWithWrongDevice |= device != gFakeState->mDevice;
        gFakeState->mFreedMemories.push_back(memory);
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeBindBufferMemory(VkDevice, VkBuffer, VkDeviceMemory, VkDeviceSize) noexcept
{
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeMapMemory(VkDevice, VkDeviceMemory, VkDeviceSize, VkDeviceSize, VkMemoryMapFlags, void** data) noexcept
{
    if (!gFakeState || !data)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    *data = gFakeState->mMappedStorage.data();
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeUnmapMemory(VkDevice, VkDeviceMemory) noexcept
{
}

VKAPI_ATTR VkResult VKAPI_CALL fakeFlushMappedMemoryRanges(VkDevice, std::uint32_t, const VkMappedMemoryRange*) noexcept
{
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateCommandPool(VkDevice                       device,
                                                     const VkCommandPoolCreateInfo* create_info,
                                                     const VkAllocationCallbacks*   allocator,
                                                     VkCommandPool*                 command_pool) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || !create_info || !command_pool)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    const std::vector<std::string> expected{ "vkCreateCommandPool", "vkDestroyCommandPool", "vkAllocateCommandBuffers", "vkCreateFence",
                                             "vkDestroyFence",      "vkBeginCommandBuffer", "vkCmdPipelineBarrier",     "vkCmdCopyBuffer",
                                             "vkEndCommandBuffer",  "vkQueueSubmit",        "vkWaitForFences" };
    gFakeState->mAllCommandsResolvedBeforeMutation = gFakeState->mDeviceLookups == expected;
    gFakeState->mCommandPoolRecords.push_back(
        { device, create_info->sType, create_info->pNext, create_info->flags, create_info->queueFamilyIndex, allocator == nullptr });
    gFakeState->mCalls.emplace_back("create-pool");
    *command_pool = gFakeState->mCreateCommandPoolOutput;
    if (gFakeState->mDescriptionToMutate)
    {
        gFakeState->mDescriptionToMutate->mHandle = { 99, 99 };
        gFakeState->mDescriptionToMutate->mBytes.fill(0xee);
    }
    invalidateAt("create-pool");
    return gFakeState->mCreateCommandPoolResult;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyCommandPool(VkDevice device, VkCommandPool command_pool, const VkAllocationCallbacks*) noexcept
{
    if (gFakeState)
    {
        gFakeState->mChildDestroyedWithWrongDevice |= device != gFakeState->mDevice;
        gFakeState->mDestroyedCommandPools.push_back(command_pool);
        gFakeState->mDestroyCommandPoolDevices.push_back(device);
        gFakeState->mCalls.emplace_back("destroy-pool");
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeAllocateCommandBuffers(VkDevice                           device,
                                                          const VkCommandBufferAllocateInfo* allocate_info,
                                                          VkCommandBuffer*                   command_buffer) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || !allocate_info || !command_buffer)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    gFakeState->mCommandBufferRecords.push_back({ device, allocate_info->sType, allocate_info->pNext, allocate_info->commandPool,
                                                  allocate_info->level, allocate_info->commandBufferCount });
    gFakeState->mCalls.emplace_back("allocate-command-buffer");
    *command_buffer = gFakeState->mAllocateCommandBufferOutput;
    invalidateAt("allocate-command-buffer");
    return gFakeState->mAllocateCommandBufferResult;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateFence(VkDevice                     device,
                                               const VkFenceCreateInfo*     create_info,
                                               const VkAllocationCallbacks* allocator,
                                               VkFence*                     fence) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || !create_info || !fence)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    gFakeState->mFenceRecords.push_back({ device, create_info->sType, create_info->pNext, create_info->flags, allocator == nullptr });
    gFakeState->mCalls.emplace_back("create-fence");
    *fence = gFakeState->mCreateFenceOutput;
    invalidateAt("create-fence");
    return gFakeState->mCreateFenceResult;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyFence(VkDevice device, VkFence fence, const VkAllocationCallbacks*) noexcept
{
    if (gFakeState)
    {
        gFakeState->mChildDestroyedWithWrongDevice |= device != gFakeState->mDevice;
        gFakeState->mDestroyedFences.push_back(fence);
        gFakeState->mDestroyFenceDevices.push_back(device);
        gFakeState->mCalls.emplace_back("destroy-fence");
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeBeginCommandBuffer(VkCommandBuffer command_buffer, const VkCommandBufferBeginInfo* begin_info) noexcept
{
    if (!gFakeState || !begin_info)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    gFakeState->mBeginRecords.push_back(
        { command_buffer, begin_info->sType, begin_info->pNext, begin_info->flags, begin_info->pInheritanceInfo });
    gFakeState->mCalls.emplace_back("begin");
    invalidateAt("begin");
    return gFakeState->mBeginResult;
}

VKAPI_ATTR void VKAPI_CALL fakeCmdPipelineBarrier(VkCommandBuffer      command_buffer,
                                                  VkPipelineStageFlags source_stage,
                                                  VkPipelineStageFlags destination_stage,
                                                  VkDependencyFlags    dependency_flags,
                                                  std::uint32_t        memory_barrier_count,
                                                  const VkMemoryBarrier*,
                                                  std::uint32_t                buffer_barrier_count,
                                                  const VkBufferMemoryBarrier* buffer_barriers,
                                                  std::uint32_t                image_barrier_count,
                                                  const VkImageMemoryBarrier*) noexcept
{
    if (!gFakeState || buffer_barrier_count != 1 || !buffer_barriers)
    {
        return;
    }
    gFakeState->mBarrierRecords.push_back({ command_buffer, source_stage, destination_stage, dependency_flags, memory_barrier_count,
                                            buffer_barrier_count, image_barrier_count, buffer_barriers[0] });
    const bool source = gFakeState->mBarrierRecords.size() == 1;
    gFakeState->mCalls.emplace_back(source ? "source-barrier" : "destination-barrier");
    invalidateAt(source ? "source-barrier" : "destination-barrier");
}

VKAPI_ATTR void VKAPI_CALL fakeCmdCopyBuffer(VkCommandBuffer     command_buffer,
                                             VkBuffer            source,
                                             VkBuffer            destination,
                                             std::uint32_t       region_count,
                                             const VkBufferCopy* regions) noexcept
{
    if (!gFakeState || region_count == 0 || !regions)
    {
        return;
    }
    gFakeState->mCopyRecords.push_back({ command_buffer, source, destination, region_count, regions[0] });
    gFakeState->mCalls.emplace_back("copy");
    invalidateAt("copy");
}

VKAPI_ATTR VkResult VKAPI_CALL fakeEndCommandBuffer(VkCommandBuffer command_buffer) noexcept
{
    if (!gFakeState)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    gFakeState->mEndedCommandBuffers.push_back(command_buffer);
    gFakeState->mCalls.emplace_back("end");
    invalidateAt("end");
    return gFakeState->mEndResult;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeQueueSubmit(VkQueue             queue,
                                               std::uint32_t       submit_count,
                                               const VkSubmitInfo* submits,
                                               VkFence             fence) noexcept
{
    if (!gFakeState || submit_count == 0 || !submits)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    const VkSubmitInfo& submit = submits[0];
    gFakeState->mSubmitRecords.push_back(
        { queue, submit_count, submit.sType, submit.pNext, submit.waitSemaphoreCount, submit.pWaitSemaphores == nullptr,
          submit.pWaitDstStageMask == nullptr, submit.commandBufferCount,
          submit.commandBufferCount == 1 && submit.pCommandBuffers ? submit.pCommandBuffers[0] : VK_NULL_HANDLE,
          submit.signalSemaphoreCount, submit.pSignalSemaphores == nullptr, fence });
    gFakeState->mCalls.emplace_back("submit");
    invalidateAt("submit");
    return gFakeState->mSubmitResult;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeWaitForFences(VkDevice device, std::uint32_t fence_count, const VkFence* fences, VkBool32 wait_all,
                                                 std::uint64_t timeout) noexcept
{
    if (!gFakeState || fence_count == 0 || !fences)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    gFakeState->mWaitRecords.push_back({ device, fence_count, fences[0], wait_all, timeout });
    gFakeState->mCalls.emplace_back("wait");
    invalidateAt("wait");
    if (gFakeState->mNextWaitResult >= gFakeState->mWaitResults.size())
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    return gFakeState->mWaitResults[gFakeState->mNextWaitResult++];
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL fakeGetDeviceProcAddr(VkDevice device, const char* name) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || !name)
    {
        return nullptr;
    }
    gFakeState->mDeviceLookups.emplace_back(name);
    invalidateAt(std::string("resolve:") + name);
    if (gFakeState->mMissingCommand == name)
    {
        return nullptr;
    }
    if (std::strcmp(name, "vkCreateBuffer") == 0)
        return eraseFunctionType(fakeCreateBuffer);
    if (std::strcmp(name, "vkDestroyBuffer") == 0)
        return eraseFunctionType(fakeDestroyBuffer);
    if (std::strcmp(name, "vkGetBufferMemoryRequirements") == 0)
        return eraseFunctionType(fakeGetBufferMemoryRequirements);
    if (std::strcmp(name, "vkAllocateMemory") == 0)
        return eraseFunctionType(fakeAllocateMemory);
    if (std::strcmp(name, "vkFreeMemory") == 0)
        return eraseFunctionType(fakeFreeMemory);
    if (std::strcmp(name, "vkBindBufferMemory") == 0)
        return eraseFunctionType(fakeBindBufferMemory);
    if (std::strcmp(name, "vkMapMemory") == 0)
        return eraseFunctionType(fakeMapMemory);
    if (std::strcmp(name, "vkUnmapMemory") == 0)
        return eraseFunctionType(fakeUnmapMemory);
    if (std::strcmp(name, "vkFlushMappedMemoryRanges") == 0)
        return eraseFunctionType(fakeFlushMappedMemoryRanges);
    if (std::strcmp(name, "vkCreateCommandPool") == 0)
        return eraseFunctionType(fakeCreateCommandPool);
    if (std::strcmp(name, "vkDestroyCommandPool") == 0)
        return eraseFunctionType(fakeDestroyCommandPool);
    if (std::strcmp(name, "vkAllocateCommandBuffers") == 0)
        return eraseFunctionType(fakeAllocateCommandBuffers);
    if (std::strcmp(name, "vkCreateFence") == 0)
        return eraseFunctionType(fakeCreateFence);
    if (std::strcmp(name, "vkDestroyFence") == 0)
        return eraseFunctionType(fakeDestroyFence);
    if (std::strcmp(name, "vkBeginCommandBuffer") == 0)
        return eraseFunctionType(fakeBeginCommandBuffer);
    if (std::strcmp(name, "vkCmdPipelineBarrier") == 0)
        return eraseFunctionType(fakeCmdPipelineBarrier);
    if (std::strcmp(name, "vkCmdCopyBuffer") == 0)
        return eraseFunctionType(fakeCmdCopyBuffer);
    if (std::strcmp(name, "vkEndCommandBuffer") == 0)
        return eraseFunctionType(fakeEndCommandBuffer);
    if (std::strcmp(name, "vkQueueSubmit") == 0)
        return eraseFunctionType(fakeQueueSubmit);
    if (std::strcmp(name, "vkWaitForFences") == 0)
        return eraseFunctionType(fakeWaitForFences);
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
    if (gFakeState->mMissingCommand == name)
    {
        return nullptr;
    }
    if (std::strcmp(name, "vkGetPhysicalDeviceMemoryProperties") == 0)
        return eraseFunctionType(fakeGetPhysicalDeviceMemoryProperties);
    if (std::strcmp(name, "vkGetDeviceProcAddr") == 0)
        return eraseFunctionType(fakeGetDeviceProcAddr);
    return nullptr;
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

struct Resources
{
    explicit Resources(FakeState& state);

    VulkanPhysicalDeviceGeneration    mPhysical;
    VulkanLogicalDeviceGeneration     mLogical;
    VulkanUploadSourceGeneration      mSource;
    VulkanUploadDestinationGeneration mDestination;
};

VulkanPhysicalDeviceGeneration makePhysical(FakeState& state)
{
    auto physical_result = resolveVulkanPhysicalDeviceGeneration({ fakeGetInstanceProcAddr, state.mInstance, state.mSurface });
    tut::ensure("the physical-device fixture resolves", std::holds_alternative<VulkanPhysicalDeviceGeneration>(physical_result));
    return std::get<VulkanPhysicalDeviceGeneration>(std::move(physical_result));
}

VulkanLogicalDeviceGeneration makeLogical(VulkanPhysicalDeviceGeneration& physical)
{
    auto logical_result = resolveVulkanLogicalDeviceGeneration(physical);
    tut::ensure("the logical-device fixture resolves", std::holds_alternative<VulkanLogicalDeviceGeneration>(logical_result));
    return std::get<VulkanLogicalDeviceGeneration>(std::move(logical_result));
}

VulkanUploadSourceGeneration makeSource(VulkanPhysicalDeviceGeneration& physical, VulkanLogicalDeviceGeneration& logical)
{
    const auto request       = description();
    auto       source_result = resolveVulkanUploadSourceGeneration(physical, logical, request);
    tut::ensure("the upload-source fixture resolves", std::holds_alternative<VulkanUploadSourceGeneration>(source_result));
    return std::get<VulkanUploadSourceGeneration>(std::move(source_result));
}

VulkanUploadDestinationGeneration makeDestination(VulkanPhysicalDeviceGeneration& physical,
                                                  VulkanLogicalDeviceGeneration&  logical,
                                                  VulkanUploadSourceGeneration&   source)
{
    const auto request            = description();
    auto       destination_result = resolveVulkanUploadDestinationGeneration(physical, logical, source, request);
    tut::ensure("the upload-destination fixture resolves", std::holds_alternative<VulkanUploadDestinationGeneration>(destination_result));
    return std::get<VulkanUploadDestinationGeneration>(std::move(destination_result));
}

Resources::Resources(FakeState& state) :
    mPhysical(makePhysical(state)),
    mLogical(makeLogical(mPhysical)),
    mSource(makeSource(mPhysical, mLogical)),
    mDestination(makeDestination(mPhysical, mLogical, mSource))
{
    state.mInstanceLookups.clear();
    state.mDeviceLookups.clear();
    state.mCalls.clear();
    state.mDestroyedFences.clear();
    state.mDestroyFenceDevices.clear();
    state.mDestroyedCommandPools.clear();
    state.mDestroyCommandPoolDevices.clear();
}

Resources makeResources(FakeState& state)
{
    return Resources(state);
}

void resetMovedUploadResources(FakeState& state)
{
    if (state.mMovedDestination)
    {
        state.mMovedDestination->reset();
    }
    if (state.mMovedSource)
    {
        state.mMovedSource->reset();
    }
    tut::ensure("moved upload resources retire before their logical device",
                state.mDestroyDeviceCalls == 0 && !state.mChildDestroyedWithWrongDevice);
}

const VulkanUploadTransferResolutionError& requireResolutionError(const VulkanUploadTransferResolutionResult& result)
{
    const auto* error = std::get_if<VulkanUploadTransferResolutionError>(&result);
    tut::ensure("transfer resolution returns an error", error != nullptr);
    return *error;
}

void ensureResolutionError(const VulkanUploadTransferResolutionResult& result,
                           VulkanUploadTransferResolutionCode          code,
                           std::optional<VulkanUploadTransferCommand>  command       = std::nullopt,
                           VkResult                                    native_result = VK_SUCCESS)
{
    const auto& error = requireResolutionError(result);
    tut::ensure("the exact transfer resolution code is reported", error.mCode == code);
    tut::ensure("the exact transfer resolution command is reported", error.mCommand == command);
    tut::ensure("the exact transfer resolution result is reported", error.mResult == native_result);
}

VulkanUploadTransferGeneration takeTransfer(VulkanUploadTransferResolutionResult&& result)
{
    tut::ensure("transfer resolution returns a generation", std::holds_alternative<VulkanUploadTransferGeneration>(result));
    return std::get<VulkanUploadTransferGeneration>(std::move(result));
}

const VulkanUploadTransferOperationError& requireOperationError(const VulkanUploadTransferOperationResult& result)
{
    const auto* error = std::get_if<VulkanUploadTransferOperationError>(&result);
    tut::ensure("the transfer operation returns an error", error != nullptr);
    return *error;
}

void ensureOperationError(const VulkanUploadTransferOperationResult& result,
                          VulkanUploadTransferOperationCode          code,
                          VulkanUploadTransferDisposition            disposition,
                          std::optional<VulkanUploadTransferCommand> command       = std::nullopt,
                          VkResult                                   native_result = VK_SUCCESS)
{
    const auto& error = requireOperationError(result);
    tut::ensure("the exact transfer operation code is reported", error.mCode == code);
    tut::ensure("the exact transfer operation command is reported", error.mCommand == command);
    tut::ensure("the exact transfer operation result is reported", error.mResult == native_result);
    tut::ensure("the exact transfer operation disposition is reported", error.mDisposition == disposition);
}

void ensureDisposition(const VulkanUploadTransferOperationResult& result, VulkanUploadTransferDisposition expected)
{
    const auto* disposition = std::get_if<VulkanUploadTransferDisposition>(&result);
    tut::ensure("the transfer operation returns a disposition", disposition != nullptr);
    tut::ensure("the exact transfer disposition is reported", *disposition == expected);
}

} // namespace

namespace tut
{

struct render_vulkan_upload_transfer_test
{
};

using render_vulkan_upload_transfer_group  = test_group<render_vulkan_upload_transfer_test>;
using render_vulkan_upload_transfer_object = render_vulkan_upload_transfer_group::object;
render_vulkan_upload_transfer_group render_vulkan_upload_transfer_tests("render Vulkan upload transfer");

template<>
template<>
void render_vulkan_upload_transfer_object::test<1>()
{
    static_assert(!std::is_default_constructible_v<VulkanUploadTransferGeneration>);
    static_assert(!std::is_copy_constructible_v<VulkanUploadTransferGeneration>);
    static_assert(!std::is_copy_assignable_v<VulkanUploadTransferGeneration>);
    static_assert(std::is_nothrow_move_constructible_v<VulkanUploadTransferGeneration>);
    static_assert(!std::is_move_assignable_v<VulkanUploadTransferGeneration>);
    static_assert(noexcept(resolveVulkanUploadTransferGeneration(std::declval<const VulkanPhysicalDeviceGeneration&>(),
                                                                 std::declval<const VulkanLogicalDeviceGeneration&>(),
                                                                 std::declval<const VulkanUploadSourceDescription&>(),
                                                                 std::declval<const VulkanUploadSourceGeneration&>(),
                                                                 std::declval<VulkanUploadDestinationGeneration&>())));

    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            resources = makeResources(state);
        state.mMovedPhysical.emplace(std::move(resources.mPhysical));
        ensureResolutionError(resolveVulkanUploadTransferGeneration(resources.mPhysical,
                                                                    resources.mLogical,
                                                                    description(),
                                                                    resources.mSource,
                                                                    resources.mDestination),
                              VulkanUploadTransferResolutionCode::InvalidPhysicalDeviceGeneration);
        ensure("an invalid physical parent resolves no transfer command", state.mDeviceLookups.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            resources = makeResources(state);
        state.mMovedLogical.emplace(std::move(resources.mLogical));
        ensureResolutionError(resolveVulkanUploadTransferGeneration(resources.mPhysical,
                                                                    resources.mLogical,
                                                                    description(),
                                                                    resources.mSource,
                                                                    resources.mDestination),
                              VulkanUploadTransferResolutionCode::InvalidLogicalDeviceGeneration);
        ensure("an invalid logical parent resolves no transfer command", state.mDeviceLookups.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            resources = makeResources(state);
        auto            invalid   = description();
        invalid.mHandle           = {};
        ensureResolutionError(resolveVulkanUploadTransferGeneration(resources.mPhysical,
                                                                    resources.mLogical,
                                                                    invalid,
                                                                    resources.mSource,
                                                                    resources.mDestination),
                              VulkanUploadTransferResolutionCode::InvalidDescription);
        ensure("an invalid description resolves no transfer command", state.mDeviceLookups.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            resources    = makeResources(state);
        auto            moved_source = std::move(resources.mSource);
        ensureResolutionError(resolveVulkanUploadTransferGeneration(resources.mPhysical,
                                                                    resources.mLogical,
                                                                    description(),
                                                                    resources.mSource,
                                                                    resources.mDestination),
                              VulkanUploadTransferResolutionCode::InvalidUploadSourceGeneration);
        ensure("a moved source resolves no transfer command", state.mDeviceLookups.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            resources         = makeResources(state);
        auto            moved_destination = std::move(resources.mDestination);
        ensureResolutionError(resolveVulkanUploadTransferGeneration(resources.mPhysical,
                                                                    resources.mLogical,
                                                                    description(),
                                                                    resources.mSource,
                                                                    resources.mDestination),
                              VulkanUploadTransferResolutionCode::InvalidUploadDestinationGeneration);
        ensure("a moved destination resolves no transfer command", state.mDeviceLookups.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            resources = makeResources(state);
        auto            mismatch  = description();
        mismatch.mBytes[0] ^= 0xff;
        ensureResolutionError(resolveVulkanUploadTransferGeneration(resources.mPhysical,
                                                                    resources.mLogical,
                                                                    mismatch,
                                                                    resources.mSource,
                                                                    resources.mDestination),
                              VulkanUploadTransferResolutionCode::InvalidUploadSourceGeneration);
        ensure("a mismatched value description resolves no transfer command", state.mDeviceLookups.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            resources              = makeResources(state);
        const VkBuffer  original_source_buffer = state.mSourceBuffer;
        state.mSourceBuffer                    = state.mDestinationBuffer;
        auto alias_source_result = resolveVulkanUploadSourceGeneration(resources.mPhysical, resources.mLogical, description());
        ensure("the alias fixture resolves a second source generation",
               std::holds_alternative<VulkanUploadSourceGeneration>(alias_source_result));
        auto alias_source = std::get<VulkanUploadSourceGeneration>(std::move(alias_source_result));
        state.mInstanceLookups.clear();
        state.mDeviceLookups.clear();
        ensureResolutionError(resolveVulkanUploadTransferGeneration(resources.mPhysical,
                                                                    resources.mLogical,
                                                                    description(),
                                                                    alias_source,
                                                                    resources.mDestination),
                              VulkanUploadTransferResolutionCode::SourceDestinationBufferAlias);
        ensure("an aliased source and destination resolve no transfer command", state.mDeviceLookups.empty());

        // The adversarial source borrows the destination's fake buffer handle.
        // Suppress that one fake destroy so the destination remains its sole
        // teardown owner; the source's distinct allocation is still freed.
        state.mIgnoreNextDestroyedBuffer = state.mDestinationBuffer;
        alias_source.reset();
        state.mSourceBuffer = original_source_buffer;
        ensure("the alias fixture leaves one owner for the shared fake handle",
               state.mIgnoredBufferDestructionCount == 1 && !state.mIgnoreNextDestroyedBuffer &&
                   std::find(state.mFreedMemories.begin(), state.mFreedMemories.end(), state.mExtraSourceMemory) !=
                       state.mFreedMemories.end());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            resources = makeResources(state);
        auto            transfer  = takeTransfer(resolveVulkanUploadTransferGeneration(resources.mPhysical,
                                                                                       resources.mLogical,
                                                                                       description(),
                                                                                       resources.mSource,
                                                                                       resources.mDestination));
        ensureDisposition(transfer.execute(), VulkanUploadTransferDisposition::Complete);
        state.mDeviceLookups.clear();
        ensureResolutionError(resolveVulkanUploadTransferGeneration(resources.mPhysical,
                                                                    resources.mLogical,
                                                                    description(),
                                                                    resources.mSource,
                                                                    resources.mDestination),
                              VulkanUploadTransferResolutionCode::DestinationAlreadyResident);
        ensure("a resident destination resolves no second transfer command", state.mDeviceLookups.empty());
    }
}

template<>
template<>
void render_vulkan_upload_transfer_object::test<2>()
{
    struct MissingCase
    {
        const char*                 mName;
        VulkanUploadTransferCommand mCommand;
    };
    constexpr std::array cases{
        MissingCase{ "vkGetDeviceProcAddr", VulkanUploadTransferCommand::GetDeviceProcAddr },
        MissingCase{ "vkCreateCommandPool", VulkanUploadTransferCommand::CreateCommandPool },
        MissingCase{ "vkDestroyCommandPool", VulkanUploadTransferCommand::DestroyCommandPool },
        MissingCase{ "vkAllocateCommandBuffers", VulkanUploadTransferCommand::AllocateCommandBuffers },
        MissingCase{ "vkCreateFence", VulkanUploadTransferCommand::CreateFence },
        MissingCase{ "vkDestroyFence", VulkanUploadTransferCommand::DestroyFence },
        MissingCase{ "vkBeginCommandBuffer", VulkanUploadTransferCommand::BeginCommandBuffer },
        MissingCase{ "vkCmdPipelineBarrier", VulkanUploadTransferCommand::CmdPipelineBarrier },
        MissingCase{ "vkCmdCopyBuffer", VulkanUploadTransferCommand::CmdCopyBuffer },
        MissingCase{ "vkEndCommandBuffer", VulkanUploadTransferCommand::EndCommandBuffer },
        MissingCase{ "vkQueueSubmit", VulkanUploadTransferCommand::QueueSubmit },
        MissingCase{ "vkWaitForFences", VulkanUploadTransferCommand::WaitForFences },
    };

    for (const MissingCase& test_case : cases)
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            resources = makeResources(state);
        state.mMissingCommand     = test_case.mName;
        const auto result         = resolveVulkanUploadTransferGeneration(resources.mPhysical,
                                                                          resources.mLogical,
                                                                          description(),
                                                                          resources.mSource,
                                                                          resources.mDestination);
        ensureResolutionError(result, VulkanUploadTransferResolutionCode::MissingRequiredCommand, test_case.mCommand);
        ensure("every missing command fails before command-pool creation", state.mCommandPoolRecords.empty());
    }
}

template<>
template<>
void render_vulkan_upload_transfer_object::test<3>()
{
    FakeState       state;
    ScopedFakeState scope(state);
    auto            resources  = makeResources(state);
    auto            request    = description();
    const auto      original   = request;
    state.mDescriptionToMutate = &request;

    auto transfer = takeTransfer(
        resolveVulkanUploadTransferGeneration(resources.mPhysical, resources.mLogical, request, resources.mSource, resources.mDestination));
    ensure("the native callback mutated only the caller's description", request != original);
    ensure("resolution owns the exact description before native work", transfer.matchesDescription(original));
    ensure("all creation and execution commands resolve before mutation", state.mAllCommandsResolvedBeforeMutation);
    ensure("resolution resolves no semaphore or swapchain command",
           std::none_of(state.mDeviceLookups.begin(), state.mDeviceLookups.end(), [](const std::string& name)
                        { return name.find("Semaphore") != std::string::npos || name.find("Swapchain") != std::string::npos; }));
    ensure("one exact command pool is created", state.mCommandPoolRecords.size() == 1);
    const auto& pool = state.mCommandPoolRecords.front();
    ensure("the command pool uses the exact device and unified queue family",
           pool.mDevice == state.mDevice && pool.mStructureType == VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO && pool.mNext == nullptr &&
               pool.mFlags == 0 && pool.mQueueFamily == state.mQueueFamily && pool.mAllocatorNull);
    ensure("one primary command buffer is allocated from the pool", state.mCommandBufferRecords.size() == 1);
    const auto& command_buffer = state.mCommandBufferRecords.front();
    ensure("the command-buffer allocation is exact",
           command_buffer.mDevice == state.mDevice && command_buffer.mStructureType == VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO &&
               command_buffer.mNext == nullptr && command_buffer.mPool == state.mCommandPool &&
               command_buffer.mLevel == VK_COMMAND_BUFFER_LEVEL_PRIMARY && command_buffer.mCount == 1);
    ensure("one unsignaled fence is created", state.mFenceRecords.size() == 1);
    const auto& fence = state.mFenceRecords.front();
    ensure("the fence create info is exact",
           fence.mDevice == state.mDevice && fence.mStructureType == VK_STRUCTURE_TYPE_FENCE_CREATE_INFO && fence.mNext == nullptr &&
               fence.mFlags == 0 && fence.mAllocatorNull);
    ensure("resolution publishes exact ownership and queue provenance",
           transfer.resourceHandle() == original.mHandle && transfer.contentIdentity() == resources.mSource.contentIdentity() &&
               transfer.sourceBuffer() == state.mSourceBuffer && transfer.destinationBuffer() == state.mDestinationBuffer &&
               transfer.queue() == state.mQueue && transfer.queueFamilyIndex() == state.mQueueFamily && transfer.queueIndex() == 0 &&
               transfer.commandPool() == state.mCommandPool && transfer.commandBuffer() == state.mCommandBuffer &&
               transfer.fence() == state.mFence && transfer.disposition() == VulkanUploadTransferDisposition::Ready &&
               transfer.submissionAttemptCount() == 0 && transfer.completionWaitCount() == 0 &&
               transfer.createdFor(resources.mPhysical, resources.mLogical) &&
               transfer.retainsUploadResources(resources.mSource, resources.mDestination));
}

template<>
template<>
void render_vulkan_upload_transfer_object::test<4>()
{
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            resources      = makeResources(state);
        state.mCreateCommandPoolResult = VK_ERROR_OUT_OF_HOST_MEMORY;
        state.mCreateCommandPoolOutput = fakeHandle<VkCommandPool>(0xdead);
        ensureResolutionError(resolveVulkanUploadTransferGeneration(resources.mPhysical,
                                                                    resources.mLogical,
                                                                    description(),
                                                                    resources.mSource,
                                                                    resources.mDestination),
                              VulkanUploadTransferResolutionCode::CommandPoolCreationFailure,
                              VulkanUploadTransferCommand::CreateCommandPool,
                              VK_ERROR_OUT_OF_HOST_MEMORY);
        ensure("a poisoned failed pool output is never destroyed", state.mDestroyedCommandPools.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            resources      = makeResources(state);
        state.mCreateCommandPoolOutput = VK_NULL_HANDLE;
        ensureResolutionError(resolveVulkanUploadTransferGeneration(resources.mPhysical,
                                                                    resources.mLogical,
                                                                    description(),
                                                                    resources.mSource,
                                                                    resources.mDestination),
                              VulkanUploadTransferResolutionCode::NullCommandPoolOnSuccess,
                              VulkanUploadTransferCommand::CreateCommandPool);
        ensure("a null successful pool output is not destroyed", state.mDestroyedCommandPools.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            resources          = makeResources(state);
        state.mAllocateCommandBufferResult = VK_ERROR_OUT_OF_HOST_MEMORY;
        state.mAllocateCommandBufferOutput = fakeHandle<VkCommandBuffer>(0xdead);
        ensureResolutionError(resolveVulkanUploadTransferGeneration(resources.mPhysical,
                                                                    resources.mLogical,
                                                                    description(),
                                                                    resources.mSource,
                                                                    resources.mDestination),
                              VulkanUploadTransferResolutionCode::CommandBufferAllocationFailure,
                              VulkanUploadTransferCommand::AllocateCommandBuffers,
                              VK_ERROR_OUT_OF_HOST_MEMORY);
        ensure("command-buffer failure rolls back only the pool",
               state.mDestroyedCommandPools == std::vector<VkCommandPool>{ state.mCommandPool } && state.mDestroyedFences.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            resources          = makeResources(state);
        state.mAllocateCommandBufferOutput = VK_NULL_HANDLE;
        ensureResolutionError(resolveVulkanUploadTransferGeneration(resources.mPhysical,
                                                                    resources.mLogical,
                                                                    description(),
                                                                    resources.mSource,
                                                                    resources.mDestination),
                              VulkanUploadTransferResolutionCode::NullCommandBufferOnSuccess,
                              VulkanUploadTransferCommand::AllocateCommandBuffers);
        ensure("a null command buffer rolls back the pool",
               state.mDestroyedCommandPools == std::vector<VkCommandPool>{ state.mCommandPool });
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            resources = makeResources(state);
        state.mCreateFenceResult  = VK_ERROR_OUT_OF_DEVICE_MEMORY;
        state.mCreateFenceOutput  = fakeHandle<VkFence>(0xdead);
        ensureResolutionError(resolveVulkanUploadTransferGeneration(resources.mPhysical,
                                                                    resources.mLogical,
                                                                    description(),
                                                                    resources.mSource,
                                                                    resources.mDestination),
                              VulkanUploadTransferResolutionCode::FenceCreationFailure,
                              VulkanUploadTransferCommand::CreateFence,
                              VK_ERROR_OUT_OF_DEVICE_MEMORY);
        ensure("a poisoned failed fence is never destroyed and the pool rolls back",
               state.mDestroyedFences.empty() && state.mDestroyedCommandPools == std::vector<VkCommandPool>{ state.mCommandPool });
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            resources = makeResources(state);
        state.mCreateFenceOutput  = VK_NULL_HANDLE;
        ensureResolutionError(resolveVulkanUploadTransferGeneration(resources.mPhysical,
                                                                    resources.mLogical,
                                                                    description(),
                                                                    resources.mSource,
                                                                    resources.mDestination),
                              VulkanUploadTransferResolutionCode::NullFenceOnSuccess,
                              VulkanUploadTransferCommand::CreateFence);
        ensure("a null successful fence rolls back only the pool",
               state.mDestroyedFences.empty() && state.mDestroyedCommandPools == std::vector<VkCommandPool>{ state.mCommandPool });
    }

    for (const std::string event : { "resolve:vkWaitForFences", "create-pool", "allocate-command-buffer", "create-fence" })
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            resources = makeResources(state);
        state.mInvalidateAt       = event;
        state.mSourceToMove       = &resources.mSource;
        const auto result         = resolveVulkanUploadTransferGeneration(resources.mPhysical,
                                                                          resources.mLogical,
                                                                          description(),
                                                                          resources.mSource,
                                                                          resources.mDestination);
        ensureResolutionError(result, VulkanUploadTransferResolutionCode::InvalidUploadSourceGeneration);
        ensure("a reentrant source move never publishes a transfer generation",
               !std::holds_alternative<VulkanUploadTransferGeneration>(result));
        if (event == "create-fence")
        {
            ensure("post-fence invalidation rolls back fence before pool",
                   state.mCalls.size() >= 2 && state.mCalls[state.mCalls.size() - 2] == "destroy-fence" &&
                       state.mCalls.back() == "destroy-pool");
        }
        ensure("stale-parent rollback uses the device that created each native object",
               std::all_of(state.mDestroyFenceDevices.begin(), state.mDestroyFenceDevices.end(),
                           [&](VkDevice device) { return device == state.mDevice; }) &&
                   std::all_of(state.mDestroyCommandPoolDevices.begin(), state.mDestroyCommandPoolDevices.end(),
                               [&](VkDevice device) { return device == state.mDevice; }));
        resetMovedUploadResources(state);
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            resources = makeResources(state);
        state.mInvalidateAt       = "resolve:vkCreateFence";
        state.mLogicalToMove      = &resources.mLogical;
        ensureResolutionError(resolveVulkanUploadTransferGeneration(resources.mPhysical,
                                                                    resources.mLogical,
                                                                    description(),
                                                                    resources.mSource,
                                                                    resources.mDestination),
                              VulkanUploadTransferResolutionCode::InvalidLogicalDeviceGeneration);
        ensure("dispatch resolution retains the original device without native mutation",
               state.mCommandPoolRecords.empty() && state.mDestroyedCommandPools.empty() && state.mDestroyedFences.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            resources = makeResources(state);
        state.mInvalidateAt       = "create-fence";
        state.mLogicalToMove      = &resources.mLogical;
        ensureResolutionError(resolveVulkanUploadTransferGeneration(resources.mPhysical,
                                                                    resources.mLogical,
                                                                    description(),
                                                                    resources.mSource,
                                                                    resources.mDestination),
                              VulkanUploadTransferResolutionCode::InvalidLogicalDeviceGeneration);
        ensure("logical-device ABA rollback keeps the original device for fence and pool destruction",
               state.mDestroyFenceDevices == std::vector<VkDevice>{ state.mDevice } &&
                   state.mDestroyCommandPoolDevices == std::vector<VkDevice>{ state.mDevice });
    }
}

template<>
template<>
void render_vulkan_upload_transfer_object::test<5>()
{
    FakeState       state;
    ScopedFakeState scope(state);
    auto            resources = makeResources(state);
    auto            transfer  = takeTransfer(resolveVulkanUploadTransferGeneration(resources.mPhysical,
                                                                                   resources.mLogical,
                                                                                   description(),
                                                                                   resources.mSource,
                                                                                   resources.mDestination));
    state.mCalls.clear();

    ensureDisposition(transfer.execute(), VulkanUploadTransferDisposition::Complete);
    ensure("execution records, submits, and waits in exact order",
           state.mCalls == std::vector<std::string>{ "begin", "source-barrier", "copy", "destination-barrier", "end", "submit", "wait" });
    ensure("the fresh primary command buffer begins for one-time submission", state.mBeginRecords.size() == 1);
    const auto& begin = state.mBeginRecords.front();
    ensure("the begin info is exact",
           begin.mCommandBuffer == state.mCommandBuffer && begin.mStructureType == VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO &&
               begin.mNext == nullptr && begin.mFlags == VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT && begin.mInheritance == nullptr);
    ensure("exactly two buffer barriers are recorded", state.mBarrierRecords.size() == 2);
    const auto& source_barrier = state.mBarrierRecords[0];
    ensure("the source barrier orders host writes before transfer reads",
           source_barrier.mCommandBuffer == state.mCommandBuffer && source_barrier.mSourceStage == VK_PIPELINE_STAGE_HOST_BIT &&
               source_barrier.mDestinationStage == VK_PIPELINE_STAGE_TRANSFER_BIT && source_barrier.mDependencyFlags == 0 &&
               source_barrier.mMemoryBarrierCount == 0 && source_barrier.mBufferBarrierCount == 1 &&
               source_barrier.mImageBarrierCount == 0 && source_barrier.mBufferBarrier.sType == VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER &&
               source_barrier.mBufferBarrier.pNext == nullptr && source_barrier.mBufferBarrier.srcAccessMask == VK_ACCESS_HOST_WRITE_BIT &&
               source_barrier.mBufferBarrier.dstAccessMask == VK_ACCESS_TRANSFER_READ_BIT &&
               source_barrier.mBufferBarrier.srcQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED &&
               source_barrier.mBufferBarrier.dstQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED &&
               source_barrier.mBufferBarrier.buffer == state.mSourceBuffer && source_barrier.mBufferBarrier.offset == 0 &&
               source_barrier.mBufferBarrier.size == VULKAN_UPLOAD_SOURCE_BYTE_COUNT);
    ensure("exactly one copy region is recorded", state.mCopyRecords.size() == 1);
    const auto& copy = state.mCopyRecords.front();
    ensure("the copy uses distinct exact buffers and 48 offset-zero bytes",
           copy.mCommandBuffer == state.mCommandBuffer && copy.mSource == state.mSourceBuffer &&
               copy.mDestination == state.mDestinationBuffer && copy.mRegionCount == 1 && copy.mRegion.srcOffset == 0 &&
               copy.mRegion.dstOffset == 0 && copy.mRegion.size == VULKAN_UPLOAD_SOURCE_BYTE_COUNT);
    const auto& destination_barrier = state.mBarrierRecords[1];
    ensure("the destination barrier orders transfer writes before vertex reads",
           destination_barrier.mCommandBuffer == state.mCommandBuffer &&
               destination_barrier.mSourceStage == VK_PIPELINE_STAGE_TRANSFER_BIT &&
               destination_barrier.mDestinationStage == VK_PIPELINE_STAGE_VERTEX_INPUT_BIT && destination_barrier.mDependencyFlags == 0 &&
               destination_barrier.mMemoryBarrierCount == 0 && destination_barrier.mBufferBarrierCount == 1 &&
               destination_barrier.mImageBarrierCount == 0 &&
               destination_barrier.mBufferBarrier.sType == VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER &&
               destination_barrier.mBufferBarrier.pNext == nullptr &&
               destination_barrier.mBufferBarrier.srcAccessMask == VK_ACCESS_TRANSFER_WRITE_BIT &&
               destination_barrier.mBufferBarrier.dstAccessMask == VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT &&
               destination_barrier.mBufferBarrier.srcQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED &&
               destination_barrier.mBufferBarrier.dstQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED &&
               destination_barrier.mBufferBarrier.buffer == state.mDestinationBuffer && destination_barrier.mBufferBarrier.offset == 0 &&
               destination_barrier.mBufferBarrier.size == VULKAN_UPLOAD_SOURCE_BYTE_COUNT);
    ensure("the exact command buffer ends once", state.mEndedCommandBuffers == std::vector<VkCommandBuffer>{ state.mCommandBuffer });
    ensure("one submission is recorded", state.mSubmitRecords.size() == 1);
    const auto& submit = state.mSubmitRecords.front();
    ensure("submission contains one command buffer and no semaphores",
           submit.mQueue == state.mQueue && submit.mSubmitCount == 1 && submit.mStructureType == VK_STRUCTURE_TYPE_SUBMIT_INFO &&
               submit.mNext == nullptr && submit.mWaitSemaphoreCount == 0 && submit.mWaitSemaphoresNull && submit.mWaitStagesNull &&
               submit.mCommandBufferCount == 1 && submit.mCommandBuffer == state.mCommandBuffer && submit.mSignalSemaphoreCount == 0 &&
               submit.mSignalSemaphoresNull && submit.mFence == state.mFence);
    ensure("one infinite all-fence completion wait is recorded", state.mWaitRecords.size() == 1);
    const auto& wait = state.mWaitRecords.front();
    ensure("the fence wait is exact",
           wait.mDevice == state.mDevice && wait.mFenceCount == 1 && wait.mFence == state.mFence && wait.mWaitAll == VK_TRUE &&
               wait.mTimeout == std::numeric_limits<std::uint64_t>::max());
    ensure("ordinary fence success alone publishes resident identity",
           resources.mDestination.isResident() && resources.mDestination.residentContentIdentity() == resources.mSource.contentIdentity());
    ensure("completion releases temporary resource dependencies and retains diagnostics",
           transfer.disposition() == VulkanUploadTransferDisposition::Complete &&
               !transfer.retainsUploadSourceGeneration(resources.mSource) &&
               !transfer.retainsUploadDestinationGeneration(resources.mDestination) && transfer.matchesDescription(description()) &&
               transfer.createdFor(resources.mPhysical, resources.mLogical) && transfer.submissionAttemptCount() == 1 &&
               transfer.completionWaitCount() == 1);

    const std::size_t call_count = state.mCalls.size();
    ensureOperationError(transfer.execute(),
                         VulkanUploadTransferOperationCode::InvalidDisposition,
                         VulkanUploadTransferDisposition::Complete);
    ensureOperationError(transfer.retryCompletion(),
                         VulkanUploadTransferOperationCode::InvalidDisposition,
                         VulkanUploadTransferDisposition::Complete);
    ensure("terminal operations make no native call and never resubmit",
           state.mCalls.size() == call_count && transfer.submissionAttemptCount() == 1 && transfer.completionWaitCount() == 1);
}

template<>
template<>
void render_vulkan_upload_transfer_object::test<6>()
{
    for (const auto& [command, result, expected] :
         std::array{ std::tuple{ VulkanUploadTransferCommand::BeginCommandBuffer, VK_ERROR_OUT_OF_HOST_MEMORY,
                                 VulkanUploadTransferDisposition::ResetRequired },
                     std::tuple{ VulkanUploadTransferCommand::BeginCommandBuffer, VK_ERROR_DEVICE_LOST,
                                 VulkanUploadTransferDisposition::DeviceLost },
                     std::tuple{ VulkanUploadTransferCommand::EndCommandBuffer, VK_ERROR_OUT_OF_HOST_MEMORY,
                                 VulkanUploadTransferDisposition::ResetRequired },
                     std::tuple{ VulkanUploadTransferCommand::EndCommandBuffer, VK_ERROR_DEVICE_LOST,
                                 VulkanUploadTransferDisposition::DeviceLost } })
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            resources = makeResources(state);
        if (command == VulkanUploadTransferCommand::BeginCommandBuffer)
        {
            state.mBeginResult = result;
        }
        else
        {
            state.mEndResult = result;
        }
        auto transfer = takeTransfer(resolveVulkanUploadTransferGeneration(resources.mPhysical,
                                                                           resources.mLogical,
                                                                           description(),
                                                                           resources.mSource,
                                                                           resources.mDestination));
        ensureOperationError(transfer.execute(), VulkanUploadTransferOperationCode::CommandFailure, expected, command, result);
        ensure("recording failure never submits or waits",
               state.mSubmitRecords.empty() && state.mWaitRecords.empty() && transfer.submissionAttemptCount() == 0 &&
                   transfer.completionWaitCount() == 0 && !resources.mDestination.isResident());
        ensure("device loss releases temporary resource pointers",
               expected != VulkanUploadTransferDisposition::DeviceLost ||
                   !transfer.retainsUploadResources(resources.mSource, resources.mDestination));
    }

    struct InvalidationCase
    {
        const char*                       mEvent;
        bool                              mMoveDestination;
        VulkanUploadTransferOperationCode mExpected;
    };
    constexpr std::array invalidations{
        InvalidationCase{ "begin", false, VulkanUploadTransferOperationCode::InvalidUploadSourceGeneration },
        InvalidationCase{ "source-barrier", true, VulkanUploadTransferOperationCode::InvalidUploadDestinationGeneration },
        InvalidationCase{ "copy", false, VulkanUploadTransferOperationCode::InvalidUploadSourceGeneration },
        InvalidationCase{ "destination-barrier", true, VulkanUploadTransferOperationCode::InvalidUploadDestinationGeneration },
        InvalidationCase{ "end", false, VulkanUploadTransferOperationCode::InvalidUploadSourceGeneration },
        InvalidationCase{ "wait", true, VulkanUploadTransferOperationCode::InvalidUploadDestinationGeneration },
    };
    for (const auto& test_case : invalidations)
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            resources = makeResources(state);
        state.mInvalidateAt       = test_case.mEvent;
        if (test_case.mMoveDestination)
        {
            state.mDestinationToMove = &resources.mDestination;
        }
        else
        {
            state.mSourceToMove = &resources.mSource;
        }
        auto transfer = takeTransfer(resolveVulkanUploadTransferGeneration(resources.mPhysical,
                                                                           resources.mLogical,
                                                                           description(),
                                                                           resources.mSource,
                                                                           resources.mDestination));
        ensureOperationError(transfer.execute(), test_case.mExpected, VulkanUploadTransferDisposition::ResetRequired);
        ensure("reentrant resource replacement never publishes resident content",
               !resources.mDestination.isResident() && (!state.mMovedDestination || !state.mMovedDestination->isResident()));
        if (std::string_view(test_case.mEvent) != "wait")
        {
            ensure("pre-submit reentrancy never reaches queue submit", state.mSubmitRecords.empty());
        }
        else
        {
            ensure("post-submit ABA detection occurs only after retirement",
                   state.mSubmitRecords.size() == 1 && state.mWaitRecords.size() == 1);
        }
        ensure("retired reentrant transfer state resets before moved buffers", transfer.reset());
        resetMovedUploadResources(state);
    }
}

template<>
template<>
void render_vulkan_upload_transfer_object::test<7>()
{
    FakeState       state;
    ScopedFakeState scope(state);
    auto            resources = makeResources(state);
    state.mSubmitResult       = VK_ERROR_OUT_OF_HOST_MEMORY;
    auto transfer             = takeTransfer(resolveVulkanUploadTransferGeneration(resources.mPhysical,
                                                                                   resources.mLogical,
                                                                                   description(),
                                                                                   resources.mSource,
                                                                                   resources.mDestination));
    ensureOperationError(transfer.execute(),
                         VulkanUploadTransferOperationCode::CommandFailure,
                         VulkanUploadTransferDisposition::ResetRequired,
                         VulkanUploadTransferCommand::QueueSubmit,
                         VK_ERROR_OUT_OF_HOST_MEMORY);
    ensure("an ordinary submit error requires reset and does not wait or publish",
           transfer.submissionAttemptCount() == 1 && transfer.completionWaitCount() == 0 && state.mWaitRecords.empty() &&
               !resources.mDestination.isResident() && transfer.reset());
}

template<>
template<>
void render_vulkan_upload_transfer_object::test<8>()
{
    FakeState       state;
    ScopedFakeState scope(state);
    auto            resources = makeResources(state);
    state.mWaitResults        = { VK_TIMEOUT, VK_SUCCESS };
    auto transfer             = takeTransfer(resolveVulkanUploadTransferGeneration(resources.mPhysical,
                                                                                   resources.mLogical,
                                                                                   description(),
                                                                                   resources.mSource,
                                                                                   resources.mDestination));
    ensureOperationError(transfer.execute(),
                         VulkanUploadTransferOperationCode::CommandFailure,
                         VulkanUploadTransferDisposition::Pending,
                         VulkanUploadTransferCommand::WaitForFences,
                         VK_TIMEOUT);
    ensure("an inconclusive wait retains both resources without publishing",
           transfer.retainsUploadResources(resources.mSource, resources.mDestination) && transfer.submissionAttemptCount() == 1 &&
               transfer.completionWaitCount() == 1 && !resources.mDestination.isResident());
    const auto teardown_before = state.mCalls;
    ensure("pending reset is refused", !transfer.reset());
    ensure("refused reset changes no native ownership", state.mCalls == teardown_before);

    const std::size_t calls_before_retry = state.mCalls.size();
    ensureDisposition(transfer.retryCompletion(), VulkanUploadTransferDisposition::Complete);
    ensure("completion retry issues only one additional fence wait",
           state.mCalls.size() == calls_before_retry + 1 && state.mCalls.back() == "wait" && transfer.submissionAttemptCount() == 1 &&
               transfer.completionWaitCount() == 2 && state.mSubmitRecords.size() == 1 && state.mWaitRecords.size() == 2);
    ensure("retry success publishes once and releases both temporary pointers",
           resources.mDestination.isResident() && resources.mDestination.residentContentIdentity() == resources.mSource.contentIdentity() &&
               !transfer.retainsUploadResources(resources.mSource, resources.mDestination));
}

template<>
template<>
void render_vulkan_upload_transfer_object::test<9>()
{
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            resources = makeResources(state);
        state.mWaitResults        = { VK_ERROR_DEVICE_LOST };
        auto transfer             = takeTransfer(resolveVulkanUploadTransferGeneration(resources.mPhysical,
                                                                                       resources.mLogical,
                                                                                       description(),
                                                                                       resources.mSource,
                                                                                       resources.mDestination));
        ensureOperationError(transfer.execute(),
                             VulkanUploadTransferOperationCode::CommandFailure,
                             VulkanUploadTransferDisposition::DeviceLost,
                             VulkanUploadTransferCommand::WaitForFences,
                             VK_ERROR_DEVICE_LOST);
        ensure("wait device loss retires the pending lifetime without false publication",
               transfer.submissionAttemptCount() == 1 && transfer.completionWaitCount() == 1 &&
                   !transfer.retainsUploadResources(resources.mSource, resources.mDestination) && !resources.mDestination.isResident() &&
                   transfer.reset());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            resources = makeResources(state);
        state.mSubmitResult       = VK_ERROR_DEVICE_LOST;
        state.mWaitResults        = { VK_TIMEOUT, VK_SUCCESS };
        auto transfer             = takeTransfer(resolveVulkanUploadTransferGeneration(resources.mPhysical,
                                                                                       resources.mLogical,
                                                                                       description(),
                                                                                       resources.mSource,
                                                                                       resources.mDestination));
        ensureOperationError(transfer.execute(),
                             VulkanUploadTransferOperationCode::CommandFailure,
                             VulkanUploadTransferDisposition::Pending,
                             VulkanUploadTransferCommand::WaitForFences,
                             VK_TIMEOUT);
        ensure("submit device loss remains pending until a wait retires possible use",
               transfer.retainsUploadResources(resources.mSource, resources.mDestination) && !transfer.reset());
        ensureOperationError(transfer.retryCompletion(),
                             VulkanUploadTransferOperationCode::CommandFailure,
                             VulkanUploadTransferDisposition::DeviceLost,
                             VulkanUploadTransferCommand::QueueSubmit,
                             VK_ERROR_DEVICE_LOST);
        ensure("retired submit device loss never publishes and never resubmits",
               transfer.submissionAttemptCount() == 1 && transfer.completionWaitCount() == 2 && state.mSubmitRecords.size() == 1 &&
                   !resources.mDestination.isResident() && !transfer.retainsUploadResources(resources.mSource, resources.mDestination));
    }
}

template<>
template<>
void render_vulkan_upload_transfer_object::test<10>()
{
    FakeState       state;
    ScopedFakeState scope(state);
    auto            resources = makeResources(state);
    state.mWaitResults        = { VK_TIMEOUT, VK_SUCCESS };
    auto transfer             = takeTransfer(resolveVulkanUploadTransferGeneration(resources.mPhysical,
                                                                                   resources.mLogical,
                                                                                   description(),
                                                                                   resources.mSource,
                                                                                   resources.mDestination));
    ensureOperationError(transfer.execute(),
                         VulkanUploadTransferOperationCode::CommandFailure,
                         VulkanUploadTransferDisposition::Pending,
                         VulkanUploadTransferCommand::WaitForFences,
                         VK_TIMEOUT);

    auto moved = std::move(transfer);
    ensure("a move makes every ownership getter on the source object inert",
           !transfer.resourceHandle() && transfer.contentIdentity() == 0 && transfer.sourceBuffer() == VK_NULL_HANDLE &&
               transfer.destinationBuffer() == VK_NULL_HANDLE && transfer.queue() == VK_NULL_HANDLE &&
               transfer.queueFamilyIndex() == VK_QUEUE_FAMILY_IGNORED && transfer.commandPool() == VK_NULL_HANDLE &&
               transfer.commandBuffer() == VK_NULL_HANDLE && transfer.fence() == VK_NULL_HANDLE &&
               transfer.disposition() == VulkanUploadTransferDisposition::ResetRequired && transfer.submissionAttemptCount() == 0 &&
               transfer.completionWaitCount() == 0 && transfer.reset());
    ensure("a move preserves pending ownership, counters, and exact resource retention",
           moved.commandPool() == state.mCommandPool && moved.commandBuffer() == state.mCommandBuffer && moved.fence() == state.mFence &&
               moved.disposition() == VulkanUploadTransferDisposition::Pending && moved.submissionAttemptCount() == 1 &&
               moved.completionWaitCount() == 1 && moved.retainsUploadResources(resources.mSource, resources.mDestination) &&
               !moved.reset());
    ensureDisposition(moved.retryCompletion(), VulkanUploadTransferDisposition::Complete);

    state.mCalls.clear();
    ensure("completed transfer reset succeeds", moved.reset());
    ensure("reset destroys the fence before the owning command pool",
           state.mCalls == std::vector<std::string>{ "destroy-fence", "destroy-pool" } && state.mDestroyDeviceCalls == 0 &&
               !state.mChildDestroyedWithWrongDevice);
    ensure("reset clears ownership, provenance, counters, and description",
           !moved.resourceHandle() && moved.contentIdentity() == 0 && !moved.matchesDescription(description()) &&
               moved.sourceBuffer() == VK_NULL_HANDLE && moved.destinationBuffer() == VK_NULL_HANDLE && moved.queue() == VK_NULL_HANDLE &&
               moved.queueFamilyIndex() == VK_QUEUE_FAMILY_IGNORED && moved.commandPool() == VK_NULL_HANDLE &&
               moved.commandBuffer() == VK_NULL_HANDLE && moved.fence() == VK_NULL_HANDLE &&
               moved.disposition() == VulkanUploadTransferDisposition::ResetRequired && moved.submissionAttemptCount() == 0 &&
               moved.completionWaitCount() == 0 && !moved.createdFor(resources.mPhysical, resources.mLogical));
    ensure("reset is idempotent", moved.reset() && state.mCalls.size() == 2);
}

} // namespace tut
