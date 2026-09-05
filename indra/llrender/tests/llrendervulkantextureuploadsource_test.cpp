/**
 * @file llrendervulkantextureuploadsource_test.cpp
 * @brief Tests for immutable Vulkan texture-upload-source ownership.
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

#include "llrendervulkantextureuploadsource.h"
#include "lltextureuploaddiagnostic.h"
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

namespace LLRenderVulkan
{

struct VulkanTextureUploadSourceGenerationTestAccess
{
    static void forceContentIdentity(VulkanTextureUploadSourceGeneration& source, std::uint64_t content_identity) noexcept
    {
        source.mContentIdentity = content_identity;
    }
};

} // namespace LLRenderVulkan

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
    CommandLookup,
    MemoryProperties,
    CreateBuffer,
    MemoryRequirements,
    AllocateMemory,
    BindBufferMemory,
    MapMemory,
    FlushMemory,
    UnmapMemory
};

enum class TeardownAt : std::uint8_t
{
    None,
    DestroyBuffer,
    FreeMemory
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
    VkMemoryRequirements             mRequirements{ 256, 16, 0xf };
    VkResult                         mCreateResult   = VK_SUCCESS;
    VkBuffer                         mCreateOutput   = mBuffer;
    VkResult                         mAllocateResult = VK_SUCCESS;
    VkDeviceMemory                   mAllocateOutput = mMemory;
    VkResult                         mBindResult     = VK_SUCCESS;
    VkResult                         mMapResult      = VK_SUCCESS;
    VkResult                         mFlushResult    = VK_SUCCESS;
    std::array<std::uint8_t, 256>    mMappedStorage{};
    std::array<std::uint8_t, 256>    mFailedMapPoison{};
    void*                            mMapOutput = mMappedStorage.data();

    std::vector<std::string> mInstanceLookups;
    std::vector<std::string> mDeviceLookups;
    bool                     mAllCommandsResolvedBeforeMutation = false;

    std::vector<BufferRecord>                  mBufferRecords;
    std::vector<VkBuffer>                      mCreateIncomingOutputs;
    std::vector<VkDevice>                      mCreateDevices;
    std::vector<bool>                          mCreateAllocatorNull;
    std::vector<VkBuffer>                      mRequirementBuffers;
    std::vector<VkMemoryAllocateInfo>          mAllocateInfos;
    std::vector<VkDeviceMemory>                mAllocateIncomingOutputs;
    std::vector<VkDevice>                      mAllocateDevices;
    std::vector<bool>                          mAllocateAllocatorNull;
    std::vector<VkBuffer>                      mBoundBuffers;
    std::vector<VkDeviceMemory>                mBoundMemories;
    std::vector<VkDeviceSize>                  mBindOffsets;
    std::vector<VkDeviceMemory>                mMappedMemories;
    std::vector<VkDeviceSize>                  mMapOffsets;
    std::vector<VkDeviceSize>                  mMapSizes;
    std::vector<VkMemoryMapFlags>              mMapFlags;
    std::vector<void*>                         mMapIncomingOutputs;
    std::vector<FlushRecord>                   mFlushRecords;
    std::vector<std::array<std::uint8_t, 256>> mFlushMappedSnapshots;
    std::vector<std::array<std::uint8_t, 256>> mUnmapMappedSnapshots;
    std::vector<std::string>                   mHostCallbackOrder;
    std::vector<VkDeviceMemory>                mUnmappedMemories;
    std::vector<VkBuffer>                      mDestroyedBuffers;
    std::vector<VkDeviceMemory>                mFreedMemories;
    std::vector<std::string>                   mTeardownOrder;
    std::size_t                                mDestroyDeviceCalls = 0;

    std::size_t                                   mCommandLookupCalls      = 0;
    std::size_t                                   mInvalidateOwnerLookupAt = 0;
    VulkanPhysicalDeviceGeneration*               mPhysicalToInvalidate    = nullptr;
    VulkanLogicalDeviceGeneration*                mLogicalToInvalidate     = nullptr;
    std::optional<VulkanPhysicalDeviceGeneration> mMovedPhysical;
    std::optional<VulkanLogicalDeviceGeneration>  mMovedLogical;
    VulkanTextureUploadSourceDescription*         mDescriptionToMutate = nullptr;
    InvalidateAt                                  mMutateDescriptionAt = InvalidateAt::None;
    VulkanTextureUploadSourceGeneration*          mResetToReenter      = nullptr;
    const VulkanPhysicalDeviceGeneration*         mResetPhysical       = nullptr;
    const VulkanLogicalDeviceGeneration*          mResetLogical        = nullptr;
    VulkanTextureUploadSourceDescription          mResetDescription;
    TeardownAt                                    mReenterResetAt            = TeardownAt::None;
    bool                                          mResetReentryAttempted     = false;
    bool                                          mResetReentryObservedInert = false;

    FakeState()
    {
        mMappedStorage.fill(0xa5);
        mFailedMapPoison.fill(0x5c);
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

void adversarialCallback(InvalidateAt point) noexcept
{
    if (!gFakeState)
    {
        return;
    }
    if (gFakeState->mMutateDescriptionAt == point && gFakeState->mDescriptionToMutate)
    {
        gFakeState->mMutateDescriptionAt          = InvalidateAt::None;
        gFakeState->mDescriptionToMutate->mHandle = { 99, 99 };
        ++gFakeState->mDescriptionToMutate->mExpectedRevision;
        gFakeState->mDescriptionToMutate->mBytes.fill(0xee);
    }
    if (gFakeState->mInvalidateAt != point)
    {
        return;
    }
    gFakeState->mInvalidateAt = InvalidateAt::None;
    if (gFakeState->mPhysicalToInvalidate)
    {
        gFakeState->mMovedPhysical.emplace(std::move(*gFakeState->mPhysicalToInvalidate));
        gFakeState->mPhysicalToInvalidate = nullptr;
    }
    else if (gFakeState->mLogicalToInvalidate)
    {
        gFakeState->mMovedLogical.emplace(std::move(*gFakeState->mLogicalToInvalidate));
        gFakeState->mLogicalToInvalidate = nullptr;
    }
}

void commandLookupCallback() noexcept
{
    if (!gFakeState)
    {
        return;
    }
    ++gFakeState->mCommandLookupCalls;
    if (gFakeState->mInvalidateOwnerLookupAt == 0 || gFakeState->mCommandLookupCalls == gFakeState->mInvalidateOwnerLookupAt)
    {
        adversarialCallback(InvalidateAt::CommandLookup);
    }
}

void reenterReset(TeardownAt point) noexcept
{
    if (gFakeState && gFakeState->mReenterResetAt == point && gFakeState->mResetToReenter)
    {
        gFakeState->mReenterResetAt                       = TeardownAt::None;
        gFakeState->mResetReentryAttempted                = true;
        const VulkanTextureUploadSourceGeneration& source = *gFakeState->mResetToReenter;
        const LLRenderContract::Extent2D           extent = source.residentExtent();
        gFakeState->mResetReentryObservedInert =
            !source.resourceHandle() && source.expectedRevision() == 0 && extent.mWidth == 0 && extent.mHeight == 0 &&
            source.pixelFormat() == LLRenderContract::PixelFormat::RGBA8Unorm && source.rowPitch() == 0 &&
            source.rowOrigin() == LLRenderContract::RowOrigin::TopLeft && source.contentIdentity() == 0 &&
            !source.matchesDescription(gFakeState->mResetDescription) && source.flags() == 0 && source.usage() == 0 &&
            source.sharingMode() == VK_SHARING_MODE_MAX_ENUM && source.buffer() == VK_NULL_HANDLE && source.memory() == VK_NULL_HANDLE &&
            source.byteCount() == 0 && source.allocationSize() == 0 && source.memoryTypeIndex() == 0 && source.memoryPropertyFlags() == 0 &&
            !source.isCoherent() && gFakeState->mResetPhysical && gFakeState->mResetLogical &&
            !source.createdFor(*gFakeState->mResetPhysical, *gFakeState->mResetLogical);
        gFakeState->mResetToReenter->reset();
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
    std::strncpy(properties->deviceName, "texture-upload-source-fake", VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 1);
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
        adversarialCallback(InvalidateAt::MemoryProperties);
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
    gFakeState->mCreateIncomingOutputs.push_back(*buffer);
    *buffer = gFakeState->mCreateOutput;
    adversarialCallback(InvalidateAt::CreateBuffer);
    return gFakeState->mCreateResult;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyBuffer(VkDevice, VkBuffer buffer, const VkAllocationCallbacks*) noexcept
{
    if (gFakeState)
    {
        gFakeState->mDestroyedBuffers.push_back(buffer);
        gFakeState->mTeardownOrder.emplace_back("destroy-buffer");
        reenterReset(TeardownAt::DestroyBuffer);
    }
}

VKAPI_ATTR void VKAPI_CALL fakeGetBufferMemoryRequirements(VkDevice, VkBuffer buffer, VkMemoryRequirements* requirements) noexcept
{
    if (gFakeState && requirements)
    {
        gFakeState->mRequirementBuffers.push_back(buffer);
        *requirements = gFakeState->mRequirements;
        adversarialCallback(InvalidateAt::MemoryRequirements);
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
    gFakeState->mAllocateIncomingOutputs.push_back(*memory);
    *memory = gFakeState->mAllocateOutput;
    adversarialCallback(InvalidateAt::AllocateMemory);
    return gFakeState->mAllocateResult;
}

VKAPI_ATTR void VKAPI_CALL fakeFreeMemory(VkDevice, VkDeviceMemory memory, const VkAllocationCallbacks*) noexcept
{
    if (gFakeState)
    {
        gFakeState->mFreedMemories.push_back(memory);
        gFakeState->mTeardownOrder.emplace_back("free-memory");
        reenterReset(TeardownAt::FreeMemory);
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
    adversarialCallback(InvalidateAt::BindBufferMemory);
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
    gFakeState->mMapIncomingOutputs.push_back(*data);
    *data = gFakeState->mMapOutput;
    adversarialCallback(InvalidateAt::MapMemory);
    return gFakeState->mMapResult;
}

VKAPI_ATTR void VKAPI_CALL fakeUnmapMemory(VkDevice, VkDeviceMemory memory) noexcept
{
    if (gFakeState)
    {
        gFakeState->mUnmapMappedSnapshots.push_back(gFakeState->mMappedStorage);
        gFakeState->mHostCallbackOrder.emplace_back("unmap");
        gFakeState->mUnmappedMemories.push_back(memory);
        gFakeState->mTeardownOrder.emplace_back("unmap");
        adversarialCallback(InvalidateAt::UnmapMemory);
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
    gFakeState->mFlushMappedSnapshots.push_back(gFakeState->mMappedStorage);
    gFakeState->mHostCallbackOrder.emplace_back("flush");
    adversarialCallback(InvalidateAt::FlushMemory);
    return gFakeState->mFlushResult;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL fakeGetDeviceProcAddr(VkDevice device, const char* name) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || !name)
    {
        return nullptr;
    }
    gFakeState->mDeviceLookups.emplace_back(name);
    commandLookupCallback();
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
    commandLookupCallback();
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
    state.mCommandLookupCalls = 0;
    return { std::move(physical), std::move(logical) };
}

VulkanTextureUploadSourceDescription description()
{
    VulkanTextureUploadSourceBytes bytes;
    for (std::size_t index = 0; index < bytes.size(); ++index)
    {
        bytes[index] = static_cast<std::uint8_t>(index * 3 + 1);
    }
    return vulkanTextureUploadSourceDescription(bytes);
}

VulkanTextureUploadSourceDescription diagnosticDescription()
{
    const LLRenderContract::TextureUploadCase upload_case = LLRenderContract::makeTextureUploadCase();
    const auto                                decoded     = LLRenderContract::decodeStreamingUploadFrame(upload_case.mFrame);
    tut::ensure("the texture-upload fixture frame decodes", decoded.has_value());
    tut::ensure("the decoded fixture has exactly 144 source bytes", decoded->mPixels.size() == VULKAN_TEXTURE_UPLOAD_SOURCE_BYTE_COUNT);

    VulkanTextureUploadSourceBytes bytes;
    std::copy(decoded->mPixels.begin(), decoded->mPixels.end(), bytes.begin());
    return vulkanTextureUploadSourceDescription(bytes);
}

bool snapshotContainsOnlyPacket(const std::array<std::uint8_t, 256>& snapshot, const VulkanTextureUploadSourceDescription& description)
{
    return std::equal(description.mBytes.begin(), description.mBytes.end(), snapshot.begin()) &&
           std::all_of(snapshot.begin() + VULKAN_TEXTURE_UPLOAD_SOURCE_BYTE_COUNT,
                       snapshot.end(),
                       [](std::uint8_t byte) { return byte == 0xa5; });
}

const VulkanTextureUploadSourceResolutionError& requireError(const VulkanTextureUploadSourceResolutionResult& result)
{
    const auto* error = std::get_if<VulkanTextureUploadSourceResolutionError>(&result);
    tut::ensure("texture-upload-source resolution returns an error", error != nullptr);
    return *error;
}

void ensureError(const VulkanTextureUploadSourceResolutionResult& result,
                 VulkanTextureUploadSourceResolutionCode          code,
                 std::optional<VulkanTextureUploadSourceCommand>  command       = std::nullopt,
                 VkResult                                         native_result = VK_SUCCESS)
{
    const auto& error = requireError(result);
    tut::ensure("the exact texture-upload-source error code is reported", error.mCode == code);
    tut::ensure("the exact texture-upload-source command is reported", error.mCommand == command);
    tut::ensure("the exact native result is reported", error.mResult == native_result);
}

VulkanTextureUploadSourceGeneration takeGeneration(VulkanTextureUploadSourceResolutionResult&& result)
{
    tut::ensure("texture-upload-source resolution returns a generation",
                std::holds_alternative<VulkanTextureUploadSourceGeneration>(result));
    return std::get<VulkanTextureUploadSourceGeneration>(std::move(result));
}

} // namespace

namespace tut
{

struct render_vulkan_texture_upload_source_test
{
};

using render_vulkan_texture_upload_source_group  = test_group<render_vulkan_texture_upload_source_test>;
using render_vulkan_texture_upload_source_object = render_vulkan_texture_upload_source_group::object;
render_vulkan_texture_upload_source_group render_vulkan_texture_upload_source_tests("render Vulkan texture upload source");

template<>
template<>
void render_vulkan_texture_upload_source_object::test<1>()
{
    static_assert(VULKAN_TEXTURE_UPLOAD_SOURCE_BYTE_COUNT == 144);
    static_assert(std::tuple_size_v<VulkanTextureUploadSourceBytes> == 144);
    static_assert(!std::is_default_constructible_v<VulkanTextureUploadSourceGeneration>);
    static_assert(!std::is_copy_constructible_v<VulkanTextureUploadSourceGeneration>);
    static_assert(!std::is_copy_assignable_v<VulkanTextureUploadSourceGeneration>);
    static_assert(std::is_nothrow_move_constructible_v<VulkanTextureUploadSourceGeneration>);
    static_assert(!std::is_move_assignable_v<VulkanTextureUploadSourceGeneration>);
    static_assert(noexcept(resolveVulkanTextureUploadSourceGeneration(std::declval<const VulkanPhysicalDeviceGeneration&>(),
                                                                      std::declval<const VulkanLogicalDeviceGeneration&>(),
                                                                      std::declval<const VulkanTextureUploadSourceDescription&>())));

    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents        = makeParents(state);
        auto            moved_physical = std::move(parents.mPhysical);
        ensureError(resolveVulkanTextureUploadSourceGeneration(parents.mPhysical, parents.mLogical, description()),
                    VulkanTextureUploadSourceResolutionCode::InvalidPhysicalDeviceGeneration);
        ensure("an invalid physical parent performs no upload lookup", state.mInstanceLookups.empty() && state.mDeviceLookups.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents       = makeParents(state);
        auto            moved_logical = std::move(parents.mLogical);
        ensureError(resolveVulkanTextureUploadSourceGeneration(parents.mPhysical, parents.mLogical, description()),
                    VulkanTextureUploadSourceResolutionCode::InvalidLogicalDeviceGeneration);
        ensure("an invalid logical parent performs no upload lookup", state.mInstanceLookups.empty() && state.mDeviceLookups.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        auto            invalid = description();
        invalid.mHandle         = {};
        ensureError(resolveVulkanTextureUploadSourceGeneration(parents.mPhysical, parents.mLogical, invalid),
                    VulkanTextureUploadSourceResolutionCode::InvalidDescription);
        ensure("an invalid description performs no upload lookup", state.mInstanceLookups.empty() && state.mDeviceLookups.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        auto            invalid = description();
        ++invalid.mHandle.mGeneration;
        ensureError(resolveVulkanTextureUploadSourceGeneration(parents.mPhysical, parents.mLogical, invalid),
                    VulkanTextureUploadSourceResolutionCode::InvalidDescription);
        ensure("a noncanonical image handle performs no upload lookup", state.mInstanceLookups.empty() && state.mDeviceLookups.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        auto            invalid = description();
        --invalid.mExpectedRevision;
        ensureError(resolveVulkanTextureUploadSourceGeneration(parents.mPhysical, parents.mLogical, invalid),
                    VulkanTextureUploadSourceResolutionCode::InvalidDescription);
        ensure("a stale revision performs no upload lookup", state.mInstanceLookups.empty() && state.mDeviceLookups.empty());
    }
}

template<>
template<>
void render_vulkan_texture_upload_source_object::test<2>()
{
    struct MissingCase
    {
        MissingCommand                   mMissing;
        VulkanTextureUploadSourceCommand mExpected;
    };
    constexpr std::array cases{
        MissingCase{ MissingCommand::GetPhysicalDeviceMemoryProperties,
                     VulkanTextureUploadSourceCommand::GetPhysicalDeviceMemoryProperties },
        MissingCase{ MissingCommand::GetDeviceProcAddr, VulkanTextureUploadSourceCommand::GetDeviceProcAddr },
        MissingCase{ MissingCommand::CreateBuffer, VulkanTextureUploadSourceCommand::CreateBuffer },
        MissingCase{ MissingCommand::DestroyBuffer, VulkanTextureUploadSourceCommand::DestroyBuffer },
        MissingCase{ MissingCommand::GetBufferMemoryRequirements, VulkanTextureUploadSourceCommand::GetBufferMemoryRequirements },
        MissingCase{ MissingCommand::AllocateMemory, VulkanTextureUploadSourceCommand::AllocateMemory },
        MissingCase{ MissingCommand::FreeMemory, VulkanTextureUploadSourceCommand::FreeMemory },
        MissingCase{ MissingCommand::BindBufferMemory, VulkanTextureUploadSourceCommand::BindBufferMemory },
        MissingCase{ MissingCommand::MapMemory, VulkanTextureUploadSourceCommand::MapMemory },
        MissingCase{ MissingCommand::UnmapMemory, VulkanTextureUploadSourceCommand::UnmapMemory },
        MissingCase{ MissingCommand::FlushMappedMemoryRanges, VulkanTextureUploadSourceCommand::FlushMappedMemoryRanges },
    };

    for (const MissingCase& test_case : cases)
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        state.mMissingCommand   = test_case.mMissing;
        const auto result       = resolveVulkanTextureUploadSourceGeneration(parents.mPhysical, parents.mLogical, description());
        ensureError(result, VulkanTextureUploadSourceResolutionCode::MissingRequiredCommand, test_case.mExpected);
        ensure("command resolution fails before the first buffer mutation", state.mBufferRecords.empty());
    }
}

template<>
template<>
void render_vulkan_texture_upload_source_object::test<3>()
{
    FakeState       state;
    ScopedFakeState scope(state);
    auto            parents = makeParents(state);
    const auto      request = description();

    auto generation = takeGeneration(resolveVulkanTextureUploadSourceGeneration(parents.mPhysical, parents.mLogical, request));
    ensure("all commands resolve before buffer creation", state.mAllCommandsResolvedBeforeMutation);
    ensure("one exact transfer-source buffer is created",
           state.mBufferRecords.size() == 1 && state.mBufferRecords[0].mStructureType == VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO &&
               state.mBufferRecords[0].mNext == nullptr && state.mBufferRecords[0].mFlags == 0 &&
               state.mBufferRecords[0].mSize == VULKAN_TEXTURE_UPLOAD_SOURCE_BYTE_COUNT &&
               state.mBufferRecords[0].mUsage == VK_BUFFER_USAGE_TRANSFER_SRC_BIT &&
               state.mBufferRecords[0].mSharingMode == VK_SHARING_MODE_EXCLUSIVE && state.mBufferRecords[0].mQueueFamilyIndexCount == 0 &&
               state.mBufferRecords[0].mQueueFamilyIndices == nullptr && state.mCreateDevices == std::vector<VkDevice>{ state.mDevice } &&
               state.mCreateAllocatorNull == std::vector<bool>{ true } &&
               state.mCreateIncomingOutputs == std::vector<VkBuffer>{ VK_NULL_HANDLE });
    ensure("the dedicated allocation uses the coherent preference",
           state.mAllocateInfos.size() == 1 && state.mAllocateInfos[0].sType == VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO &&
               state.mAllocateInfos[0].pNext == nullptr && state.mAllocateInfos[0].allocationSize == state.mRequirements.size &&
               state.mAllocateInfos[0].memoryTypeIndex == 2 && state.mAllocateDevices == std::vector<VkDevice>{ state.mDevice } &&
               state.mAllocateAllocatorNull == std::vector<bool>{ true } &&
               state.mAllocateIncomingOutputs == std::vector<VkDeviceMemory>{ VK_NULL_HANDLE });
    ensure("the allocation is bound at zero",
           state.mBoundBuffers == std::vector<VkBuffer>{ state.mBuffer } &&
               state.mBoundMemories == std::vector<VkDeviceMemory>{ state.mMemory } &&
               state.mBindOffsets == std::vector<VkDeviceSize>{ 0 });
    ensure("the whole allocation is mapped and unmapped before publication",
           state.mMappedMemories == std::vector<VkDeviceMemory>{ state.mMemory } && state.mMapOffsets == std::vector<VkDeviceSize>{ 0 } &&
               state.mMapSizes == std::vector<VkDeviceSize>{ VK_WHOLE_SIZE } && state.mMapFlags == std::vector<VkMemoryMapFlags>{ 0 } &&
               state.mMapIncomingOutputs == std::vector<void*>{ nullptr } &&
               state.mUnmappedMemories == std::vector<VkDeviceMemory>{ state.mMemory } && state.mFlushRecords.empty() &&
               state.mFlushMappedSnapshots.empty() && state.mUnmapMappedSnapshots.size() == 1 &&
               state.mHostCallbackOrder == std::vector<std::string>{ "unmap" });
    ensure("exactly 144 immutable bytes are copied",
           snapshotContainsOnlyPacket(state.mUnmapMappedSnapshots.front(), request) &&
               state.mUnmapMappedSnapshots.front() == state.mMappedStorage);
    const LLRenderContract::Extent2D extent = generation.residentExtent();
    ensure("the generation publishes exact metadata and deterministic identity",
           generation.resourceHandle() == request.mHandle && generation.expectedRevision() == request.mExpectedRevision &&
               extent.mWidth == LLRenderContract::TEXTURE_UPLOAD_RESIDENT_WIDTH &&
               extent.mHeight == LLRenderContract::TEXTURE_UPLOAD_RESIDENT_HEIGHT &&
               generation.pixelFormat() == LLRenderContract::PixelFormat::RGBA8Unorm &&
               generation.rowPitch() == LLRenderContract::TEXTURE_UPLOAD_ROW_PITCH &&
               generation.rowOrigin() == LLRenderContract::RowOrigin::TopLeft && generation.flags() == 0 &&
               generation.usage() == VK_BUFFER_USAGE_TRANSFER_SRC_BIT && generation.sharingMode() == VK_SHARING_MODE_EXCLUSIVE &&
               generation.contentIdentity() == LLRenderContract::stableByteContentIdentity(request.mBytes) &&
               generation.matchesDescription(request) && generation.buffer() == state.mBuffer && generation.memory() == state.mMemory &&
               generation.byteCount() == VULKAN_TEXTURE_UPLOAD_SOURCE_BYTE_COUNT &&
               generation.allocationSize() == state.mRequirements.size && generation.memoryTypeIndex() == 2 &&
               generation.memoryPropertyFlags() == state.mMemoryProperties.memoryTypes[2].propertyFlags && generation.isCoherent() &&
               generation.createdFor(parents.mPhysical, parents.mLogical));

    auto wrong_bytes = request;
    wrong_bytes.mBytes[5] ^= 0xff;
    auto wrong_handle = request;
    ++wrong_handle.mHandle.mGeneration;
    auto wrong_revision = request;
    --wrong_revision.mExpectedRevision;
    ensure("description matching authenticates the handle, revision, and every byte",
           !generation.matchesDescription(wrong_bytes) && !generation.matchesDescription(wrong_handle) &&
               !generation.matchesDescription(wrong_revision));
}

template<>
template<>
void render_vulkan_texture_upload_source_object::test<4>()
{
    FakeState       state;
    ScopedFakeState scope(state);
    auto            parents = makeParents(state);
    const auto      request = description();

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

    auto generation = takeGeneration(resolveVulkanTextureUploadSourceGeneration(parents.mPhysical, parents.mLogical, request));
    ensure("each forbidden memory-type flag is skipped before ordinary host-visible fallback",
           generation.memoryTypeIndex() == 3 && generation.memoryPropertyFlags() == VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT &&
               !generation.isCoherent());
    ensure("noncoherent bytes exist before an exact flush and remain intact through unmap",
           state.mFlushRecords.size() == 1 && state.mFlushRecords[0].mDevice == state.mDevice && state.mFlushRecords[0].mRangeCount == 1 &&
               state.mFlushRecords[0].mRange.sType == VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE &&
               state.mFlushRecords[0].mRange.pNext == nullptr && state.mFlushRecords[0].mRange.memory == state.mMemory &&
               state.mFlushRecords[0].mRange.offset == 0 && state.mFlushRecords[0].mRange.size == VK_WHOLE_SIZE &&
               state.mUnmappedMemories == std::vector<VkDeviceMemory>{ state.mMemory } && state.mFlushMappedSnapshots.size() == 1 &&
               state.mUnmapMappedSnapshots.size() == 1 && snapshotContainsOnlyPacket(state.mFlushMappedSnapshots.front(), request) &&
               state.mFlushMappedSnapshots.front() == state.mUnmapMappedSnapshots.front() &&
               state.mHostCallbackOrder == std::vector<std::string>{ "flush", "unmap" });

    {
        FakeState       tile_state;
        ScopedFakeState tile_scope(tile_state);
        auto            tile_parents                = makeParents(tile_state);
        const auto      tile_request                = description();
        tile_state.mMemoryProperties.memoryTypes[0] = {
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            0,
        };
        tile_state.mMemoryProperties.memoryTypes[1] = { VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, 1 };
        tile_state.mMemoryProperties.memoryTypes[2] = {
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_PROTECTED_BIT,
            1,
        };
        tile_state.mMemoryProperties.memoryTypes[3] = tile_state.mMemoryProperties.memoryTypes[2];
        tile_state.mMemoryProperties.memoryHeaps[0].flags |= VK_MEMORY_HEAP_TILE_MEMORY_BIT_QCOM;

        auto tile_generation =
            takeGeneration(resolveVulkanTextureUploadSourceGeneration(tile_parents.mPhysical, tile_parents.mLogical, tile_request));
        ensure("a QCOM tile heap is independently skipped before noncoherent fallback",
               tile_generation.memoryTypeIndex() == 1 && !tile_generation.isCoherent() && tile_state.mFlushMappedSnapshots.size() == 1 &&
                   tile_state.mUnmapMappedSnapshots.size() == 1 &&
                   snapshotContainsOnlyPacket(tile_state.mFlushMappedSnapshots.front(), tile_request) &&
                   tile_state.mFlushMappedSnapshots.front() == tile_state.mUnmapMappedSnapshots.front() &&
                   tile_state.mHostCallbackOrder == std::vector<std::string>{ "flush", "unmap" });
    }

    {
        FakeState       unified_state;
        ScopedFakeState unified_scope(unified_state);
        auto            unified_parents                = makeParents(unified_state);
        unified_state.mMemoryProperties.memoryTypes[0] = {
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            0,
        };
        auto unified =
            takeGeneration(resolveVulkanTextureUploadSourceGeneration(unified_parents.mPhysical, unified_parents.mLogical, description()));
        ensure("unified device-local host-visible memory remains eligible",
               unified.memoryTypeIndex() == 0 && unified.isCoherent() && unified_state.mFlushRecords.empty());
    }

    {
        FakeState       highest_state;
        ScopedFakeState highest_scope(highest_state);
        auto            highest_parents                 = makeParents(highest_state);
        highest_state.mMemoryProperties.memoryHeapCount = 1;
        highest_state.mMemoryProperties.memoryTypeCount = VK_MAX_MEMORY_TYPES;
        for (std::uint32_t index = 0; index < VK_MAX_MEMORY_TYPES; ++index)
        {
            highest_state.mMemoryProperties.memoryTypes[index] = {
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_PROTECTED_BIT,
                0,
            };
        }
        highest_state.mMemoryProperties.memoryTypes[VK_MAX_MEMORY_TYPES - 1].propertyFlags =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        highest_state.mRequirements.memoryTypeBits = std::uint32_t{ 1 } << (VK_MAX_MEMORY_TYPES - 1);

        auto highest =
            takeGeneration(resolveVulkanTextureUploadSourceGeneration(highest_parents.mPhysical, highest_parents.mLogical, description()));
        ensure("the highest legal memory-type bit is selected without an overflowing shift",
               highest.memoryTypeIndex() == VK_MAX_MEMORY_TYPES - 1);
    }
}

template<>
template<>
void render_vulkan_texture_upload_source_object::test<5>()
{
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents                 = makeParents(state);
        state.mMemoryProperties.memoryTypeCount = 0;
        ensureError(resolveVulkanTextureUploadSourceGeneration(parents.mPhysical, parents.mLogical, description()),
                    VulkanTextureUploadSourceResolutionCode::InvalidPhysicalDeviceMemoryProperties,
                    VulkanTextureUploadSourceCommand::GetPhysicalDeviceMemoryProperties);
        ensure("an invalid memory table creates no buffer", state.mBufferRecords.empty());
    }
    for (const auto counts : std::array{
             std::pair{ VK_MAX_MEMORY_TYPES + 1U, 1U },
             std::pair{ 1U, 0U },
             std::pair{ 1U, VK_MAX_MEMORY_HEAPS + 1U },
         })
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents                 = makeParents(state);
        state.mMemoryProperties.memoryTypeCount = counts.first;
        state.mMemoryProperties.memoryHeapCount = counts.second;
        ensureError(resolveVulkanTextureUploadSourceGeneration(parents.mPhysical, parents.mLogical, description()),
                    VulkanTextureUploadSourceResolutionCode::InvalidPhysicalDeviceMemoryProperties,
                    VulkanTextureUploadSourceCommand::GetPhysicalDeviceMemoryProperties);
        ensure("out-of-range memory table counts create no buffer", state.mBufferRecords.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents                          = makeParents(state);
        state.mMemoryProperties.memoryTypes[1].heapIndex = state.mMemoryProperties.memoryHeapCount;
        ensureError(resolveVulkanTextureUploadSourceGeneration(parents.mPhysical, parents.mLogical, description()),
                    VulkanTextureUploadSourceResolutionCode::InvalidPhysicalDeviceMemoryProperties,
                    VulkanTextureUploadSourceCommand::GetPhysicalDeviceMemoryProperties);
    }
    for (const VkMemoryRequirements requirements : std::array{ VkMemoryRequirements{ 143, 16, 1 }, VkMemoryRequirements{ 144, 0, 1 },
                                                               VkMemoryRequirements{ 144, 3, 1 }, VkMemoryRequirements{ 144, 16, 0 } })
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        state.mRequirements     = requirements;
        const auto result       = resolveVulkanTextureUploadSourceGeneration(parents.mPhysical, parents.mLogical, description());
        ensureError(result,
                    VulkanTextureUploadSourceResolutionCode::InvalidBufferMemoryRequirements,
                    VulkanTextureUploadSourceCommand::GetBufferMemoryRequirements);
        ensure("invalid requirements destroy only the created buffer",
               state.mDestroyedBuffers == std::vector<VkBuffer>{ state.mBuffer } && state.mFreedMemories.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents            = makeParents(state);
        state.mRequirements.memoryTypeBits = 1;
        ensureError(resolveVulkanTextureUploadSourceGeneration(parents.mPhysical, parents.mLogical, description()),
                    VulkanTextureUploadSourceResolutionCode::NoCompatibleMemoryType);
        ensure("an incompatible memory table destroys only the created buffer",
               state.mDestroyedBuffers == std::vector<VkBuffer>{ state.mBuffer } && state.mFreedMemories.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents                     = makeParents(state);
        state.mMemoryProperties.memoryHeaps[1].size = state.mRequirements.size - 1;
        state.mRequirements.memoryTypeBits          = std::uint32_t{ 1 } << 2;
        ensureError(resolveVulkanTextureUploadSourceGeneration(parents.mPhysical, parents.mLogical, description()),
                    VulkanTextureUploadSourceResolutionCode::NoCompatibleMemoryType);
    }
}

template<>
template<>
void render_vulkan_texture_upload_source_object::test<6>()
{
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        state.mCreateResult     = VK_ERROR_OUT_OF_HOST_MEMORY;
        state.mCreateOutput     = fakeHandle<VkBuffer>(0xdead);
        ensureError(resolveVulkanTextureUploadSourceGeneration(parents.mPhysical, parents.mLogical, description()),
                    VulkanTextureUploadSourceResolutionCode::BufferCreationFailure,
                    VulkanTextureUploadSourceCommand::CreateBuffer,
                    VK_ERROR_OUT_OF_HOST_MEMORY);
        ensure("undefined failed-create output is never destroyed", state.mDestroyedBuffers.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        state.mCreateOutput     = VK_NULL_HANDLE;
        ensureError(resolveVulkanTextureUploadSourceGeneration(parents.mPhysical, parents.mLogical, description()),
                    VulkanTextureUploadSourceResolutionCode::NullBufferOnSuccess,
                    VulkanTextureUploadSourceCommand::CreateBuffer);
        ensure("a null successful buffer output is not destroyed", state.mDestroyedBuffers.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        state.mAllocateResult   = VK_ERROR_OUT_OF_DEVICE_MEMORY;
        state.mAllocateOutput   = fakeHandle<VkDeviceMemory>(0xdead);
        ensureError(resolveVulkanTextureUploadSourceGeneration(parents.mPhysical, parents.mLogical, description()),
                    VulkanTextureUploadSourceResolutionCode::MemoryAllocationFailure,
                    VulkanTextureUploadSourceCommand::AllocateMemory,
                    VK_ERROR_OUT_OF_DEVICE_MEMORY);
        ensure("undefined failed-allocation output is never freed",
               state.mDestroyedBuffers == std::vector<VkBuffer>{ state.mBuffer } && state.mFreedMemories.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        state.mAllocateOutput   = VK_NULL_HANDLE;
        ensureError(resolveVulkanTextureUploadSourceGeneration(parents.mPhysical, parents.mLogical, description()),
                    VulkanTextureUploadSourceResolutionCode::NullMemoryOnSuccess,
                    VulkanTextureUploadSourceCommand::AllocateMemory);
        ensure("a null successful allocation rolls back the buffer only",
               state.mDestroyedBuffers == std::vector<VkBuffer>{ state.mBuffer } && state.mFreedMemories.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        state.mBindResult       = VK_ERROR_MEMORY_MAP_FAILED;
        ensureError(resolveVulkanTextureUploadSourceGeneration(parents.mPhysical, parents.mLogical, description()),
                    VulkanTextureUploadSourceResolutionCode::BufferMemoryBindFailure,
                    VulkanTextureUploadSourceCommand::BindBufferMemory,
                    VK_ERROR_MEMORY_MAP_FAILED);
        ensure("bind failure rolls back buffer before memory",
               state.mTeardownOrder == std::vector<std::string>{ "destroy-buffer", "free-memory" });
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        state.mMapResult        = VK_ERROR_MEMORY_MAP_FAILED;
        state.mMapOutput        = state.mFailedMapPoison.data();
        const auto poison       = state.mFailedMapPoison;
        ensureError(resolveVulkanTextureUploadSourceGeneration(parents.mPhysical, parents.mLogical, description()),
                    VulkanTextureUploadSourceResolutionCode::MemoryMapFailure,
                    VulkanTextureUploadSourceCommand::MapMemory,
                    VK_ERROR_MEMORY_MAP_FAILED);
        ensure("failed mapping is not unmapped and rolls back ownership",
               state.mUnmappedMemories.empty() && state.mFailedMapPoison == poison &&
                   state.mTeardownOrder == std::vector<std::string>{ "destroy-buffer", "free-memory" });
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        state.mMapOutput        = nullptr;
        ensureError(resolveVulkanTextureUploadSourceGeneration(parents.mPhysical, parents.mLogical, description()),
                    VulkanTextureUploadSourceResolutionCode::NullMappedDataOnSuccess,
                    VulkanTextureUploadSourceCommand::MapMemory);
        ensure("a null successful mapping is unmapped before ownership rollback",
               state.mTeardownOrder == std::vector<std::string>{ "unmap", "destroy-buffer", "free-memory" });
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents            = makeParents(state);
        state.mRequirements.memoryTypeBits = std::uint32_t{ 1 } << 1;
        state.mFlushResult                 = VK_ERROR_MEMORY_MAP_FAILED;
        ensureError(resolveVulkanTextureUploadSourceGeneration(parents.mPhysical, parents.mLogical, description()),
                    VulkanTextureUploadSourceResolutionCode::MemoryFlushFailure,
                    VulkanTextureUploadSourceCommand::FlushMappedMemoryRanges,
                    VK_ERROR_MEMORY_MAP_FAILED);
        ensure("flush failure unmaps before ownership rollback",
               state.mTeardownOrder == std::vector<std::string>{ "unmap", "destroy-buffer", "free-memory" });
    }
}

template<>
template<>
void render_vulkan_texture_upload_source_object::test<7>()
{
    FakeState       state;
    ScopedFakeState scope(state);
    auto            parents    = makeParents(state);
    const auto      request    = description();
    auto            generation = takeGeneration(resolveVulkanTextureUploadSourceGeneration(parents.mPhysical, parents.mLogical, request));

    auto same_native = makeParents(state);
    ensure("same-looking parent generations fail the owner's address and provenance check",
           generation.createdFor(parents.mPhysical, parents.mLogical) &&
               !generation.createdFor(same_native.mPhysical, same_native.mLogical));

    auto moved = std::move(generation);
    ensure("a move makes every ownership accessor on the source inert",
           !generation.resourceHandle() && generation.contentIdentity() == 0 && !generation.matchesDescription(request) &&
               generation.residentExtent().mWidth == 0 && generation.residentExtent().mHeight == 0 && generation.rowPitch() == 0 &&
               generation.usage() == 0 && generation.sharingMode() == VK_SHARING_MODE_MAX_ENUM && generation.buffer() == VK_NULL_HANDLE &&
               generation.memory() == VK_NULL_HANDLE && generation.byteCount() == 0 && generation.allocationSize() == 0 &&
               generation.memoryTypeIndex() == 0 && generation.memoryPropertyFlags() == 0 && !generation.isCoherent() &&
               !generation.createdFor(parents.mPhysical, parents.mLogical));
    ensure("a move preserves exact ownership and provenance on the destination",
           moved.resourceHandle() == request.mHandle && moved.expectedRevision() == request.mExpectedRevision &&
               moved.contentIdentity() == LLRenderContract::stableByteContentIdentity(request.mBytes) &&
               moved.matchesDescription(request) && moved.buffer() == state.mBuffer && moved.memory() == state.mMemory &&
               moved.byteCount() == VULKAN_TEXTURE_UPLOAD_SOURCE_BYTE_COUNT && moved.createdFor(parents.mPhysical, parents.mLogical));

    state.mTeardownOrder.clear();
    moved.reset();
    ensure("reset destroys the buffer before freeing its dedicated allocation",
           state.mTeardownOrder == std::vector<std::string>{ "destroy-buffer", "free-memory" });
    ensure("reset makes every ownership accessor inert",
           !moved.resourceHandle() && moved.contentIdentity() == 0 && !moved.matchesDescription(request) &&
               moved.residentExtent().mWidth == 0 && moved.residentExtent().mHeight == 0 && moved.rowPitch() == 0 && moved.usage() == 0 &&
               moved.sharingMode() == VK_SHARING_MODE_MAX_ENUM && moved.buffer() == VK_NULL_HANDLE && moved.memory() == VK_NULL_HANDLE &&
               moved.byteCount() == 0 && moved.allocationSize() == 0 && moved.memoryTypeIndex() == 0 && moved.memoryPropertyFlags() == 0 &&
               !moved.isCoherent() && !moved.createdFor(parents.mPhysical, parents.mLogical));
    moved.reset();
    ensure("reset is idempotent", state.mTeardownOrder.size() == 2);

    for (TeardownAt point : { TeardownAt::DestroyBuffer, TeardownAt::FreeMemory })
    {
        FakeState       reentry_state;
        ScopedFakeState reentry_scope(reentry_state);
        auto            reentry_parents = makeParents(reentry_state);
        auto            reentry_generation =
            takeGeneration(resolveVulkanTextureUploadSourceGeneration(reentry_parents.mPhysical, reentry_parents.mLogical, description()));
        reentry_state.mTeardownOrder.clear();
        reentry_state.mResetToReenter   = &reentry_generation;
        reentry_state.mResetPhysical    = &reentry_parents.mPhysical;
        reentry_state.mResetLogical     = &reentry_parents.mLogical;
        reentry_state.mResetDescription = description();
        reentry_state.mReenterResetAt   = point;

        reentry_generation.reset();
        ensure("every public accessor observes an inert generation before either teardown callback can re-enter",
               reentry_state.mResetReentryAttempted && reentry_state.mResetReentryObservedInert &&
                   reentry_state.mTeardownOrder == std::vector<std::string>{ "destroy-buffer", "free-memory" });
        reentry_generation.reset();
        ensure("reentrant reset retains exactly one teardown occurrence per object", reentry_state.mTeardownOrder.size() == 2);
    }

    {
        FakeState       collision_state;
        ScopedFakeState collision_scope(collision_state);
        collision_state.mMemory         = fakeHandle<VkDeviceMemory>(0x6000);
        collision_state.mAllocateOutput = collision_state.mMemory;
        auto collision_parents          = makeParents(collision_state);
        auto first                      = takeGeneration(
            resolveVulkanTextureUploadSourceGeneration(collision_parents.mPhysical, collision_parents.mLogical, description()));
        auto second = takeGeneration(
            resolveVulkanTextureUploadSourceGeneration(collision_parents.mPhysical, collision_parents.mLogical, description()));
        collision_state.mTeardownOrder.clear();

        first.reset();
        second.reset();
        ensure("equal raw handle bits remain four independent successful ownership occurrences",
               collision_state.mDestroyedBuffers == std::vector<VkBuffer>{ collision_state.mBuffer, collision_state.mBuffer } &&
                   collision_state.mFreedMemories == std::vector<VkDeviceMemory>{ collision_state.mMemory, collision_state.mMemory } &&
                   collision_state.mTeardownOrder ==
                       std::vector<std::string>{ "destroy-buffer", "free-memory", "destroy-buffer", "free-memory" });
    }
}

template<>
template<>
void render_vulkan_texture_upload_source_object::test<8>()
{
    constexpr std::array callback_points{
        InvalidateAt::CommandLookup,      InvalidateAt::MemoryProperties, InvalidateAt::CreateBuffer,
        InvalidateAt::MemoryRequirements, InvalidateAt::AllocateMemory,   InvalidateAt::BindBufferMemory,
        InvalidateAt::MapMemory,          InvalidateAt::FlushMemory,      InvalidateAt::UnmapMemory,
    };
    for (InvalidateAt point : callback_points)
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents    = makeParents(state);
        auto            request    = description();
        const auto      original   = request;
        state.mDescriptionToMutate = &request;
        state.mMutateDescriptionAt = point;
        if (point == InvalidateAt::FlushMemory)
        {
            state.mRequirements.memoryTypeBits = std::uint32_t{ 1 } << 1;
        }

        auto generation = takeGeneration(resolveVulkanTextureUploadSourceGeneration(parents.mPhysical, parents.mLogical, request));
        ensure("resolution snapshots the caller description before every resolver and native callback",
               request != original && generation.matchesDescription(original) && !generation.matchesDescription(request) &&
                   generation.resourceHandle() == original.mHandle && generation.expectedRevision() == original.mExpectedRevision &&
                   generation.contentIdentity() == LLRenderContract::stableByteContentIdentity(original.mBytes) &&
                   std::equal(original.mBytes.begin(), original.mBytes.end(), state.mMappedStorage.begin()));
    }

    for (std::size_t lookup = 1; lookup <= 11; ++lookup)
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents        = makeParents(state);
        state.mInvalidateAt            = InvalidateAt::CommandLookup;
        state.mInvalidateOwnerLookupAt = lookup;
        state.mLogicalToInvalidate     = &parents.mLogical;

        ensureError(resolveVulkanTextureUploadSourceGeneration(parents.mPhysical, parents.mLogical, description()),
                    VulkanTextureUploadSourceResolutionCode::InvalidLogicalDeviceGeneration);
        ensure("every resolver callback is followed by parent revalidation",
               state.mCommandLookupCalls == lookup && state.mBufferRecords.empty() && state.mDestroyedBuffers.empty() &&
                   state.mFreedMemories.empty());
    }

    for (InvalidateAt point : { InvalidateAt::CreateBuffer, InvalidateAt::AllocateMemory, InvalidateAt::MapMemory })
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents      = makeParents(state);
        state.mInvalidateAt          = point;
        state.mLogicalToInvalidate   = &parents.mLogical;
        const auto failed_map_poison = state.mFailedMapPoison;
        if (point == InvalidateAt::CreateBuffer)
        {
            state.mCreateResult = VK_ERROR_OUT_OF_HOST_MEMORY;
            state.mCreateOutput = fakeHandle<VkBuffer>(0xdead);
        }
        else if (point == InvalidateAt::AllocateMemory)
        {
            state.mAllocateResult = VK_ERROR_OUT_OF_DEVICE_MEMORY;
            state.mAllocateOutput = fakeHandle<VkDeviceMemory>(0xdead);
        }
        else
        {
            state.mMapResult = VK_ERROR_MEMORY_MAP_FAILED;
            state.mMapOutput = state.mFailedMapPoison.data();
        }

        ensureError(resolveVulkanTextureUploadSourceGeneration(parents.mPhysical, parents.mLogical, description()),
                    VulkanTextureUploadSourceResolutionCode::InvalidLogicalDeviceGeneration);
        if (point == InvalidateAt::CreateBuffer)
        {
            ensure("simultaneous parent invalidation never adopts a failed create output", state.mTeardownOrder.empty());
        }
        else if (point == InvalidateAt::AllocateMemory)
        {
            ensure("simultaneous parent invalidation never adopts a failed allocation output",
                   state.mTeardownOrder == std::vector<std::string>{ "destroy-buffer" } && state.mFreedMemories.empty());
        }
        else
        {
            ensure("simultaneous parent invalidation never consumes or unmaps a failed map output",
                   state.mFailedMapPoison == failed_map_poison && state.mUnmappedMemories.empty() &&
                       state.mTeardownOrder == std::vector<std::string>{ "destroy-buffer", "free-memory" });
        }
    }
}

template<>
template<>
void render_vulkan_texture_upload_source_object::test<9>()
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

        const auto result = resolveVulkanTextureUploadSourceGeneration(parents.mPhysical, parents.mLogical, description());
        ensureError(result, VulkanTextureUploadSourceResolutionCode::InvalidLogicalDeviceGeneration);
        ensure("a stale parent never publishes a generation", !std::holds_alternative<VulkanTextureUploadSourceGeneration>(result));

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

    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents     = makeParents(state);
        state.mInvalidateAt         = InvalidateAt::MemoryProperties;
        state.mPhysicalToInvalidate = &parents.mPhysical;

        ensureError(resolveVulkanTextureUploadSourceGeneration(parents.mPhysical, parents.mLogical, description()),
                    VulkanTextureUploadSourceResolutionCode::InvalidPhysicalDeviceGeneration);
        ensure("physical-parent invalidation after a native callback publishes and owns nothing",
               state.mBufferRecords.empty() && state.mDestroyedBuffers.empty() && state.mFreedMemories.empty());
    }
}

template<>
template<>
void render_vulkan_texture_upload_source_object::test<10>()
{
    FakeState       state;
    ScopedFakeState scope(state);
    auto            parents    = makeParents(state);
    const auto      canonical  = diagnosticDescription();
    auto            generation = takeGeneration(resolveVulkanTextureUploadSourceGeneration(parents.mPhysical, parents.mLogical, canonical));
    const LLRenderContract::TextureUploadCase    upload_case = LLRenderContract::makeTextureUploadCase();
    const LLRenderContract::TextureUploadFixture fixture     = LLRenderContract::makeTextureUploadFixture();

    ensure("the decoded diagnostic request carries the replacement image, revision, and every fixture byte",
           canonical.mHandle == upload_case.mInputs.mHandles.mReplacementImage &&
               canonical.mExpectedRevision == upload_case.mInputs.mRevision && canonical.mBytes == fixture.mSourceRGBA8 &&
               state.mFlushMappedSnapshots.empty() && state.mUnmapMappedSnapshots.size() == 1 &&
               snapshotContainsOnlyPacket(state.mUnmapMappedSnapshots.front(), canonical) &&
               state.mHostCallbackOrder == std::vector<std::string>{ "unmap" });
    for (std::size_t row = 0; row < LLRenderContract::TEXTURE_UPLOAD_RESIDENT_HEIGHT; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            const std::size_t offset = row * LLRenderContract::TEXTURE_UPLOAD_ROW_PITCH + 32 + column;
            ensure_equals("each row's poison padding is present when unmap closes the coherent write",
                          state.mUnmapMappedSnapshots.front()[offset],
                          static_cast<std::uint8_t>(0xf0 + row * 4 + column));
        }
    }
    ensure("the diagnostic upload source publishes the stable fixture FNV identity",
           generation.contentIdentity() == 0xf3f139b7e2014eb5ULL && generation.matchesDescription(canonical));

    generation.reset();
    auto arbitrary            = description();
    auto arbitrary_generation = takeGeneration(resolveVulkanTextureUploadSourceGeneration(parents.mPhysical, parents.mLogical, arbitrary));
    ensure("the fixed texture shape accepts arbitrary 144-byte payloads",
           arbitrary_generation.matchesDescription(arbitrary) &&
               arbitrary_generation.contentIdentity() == LLRenderContract::stableByteContentIdentity(arbitrary.mBytes));

    auto different = arbitrary;
    different.mBytes[17] ^= 0x5a;
    VulkanTextureUploadSourceGenerationTestAccess::forceContentIdentity(arbitrary_generation,
                                                                        LLRenderContract::stableByteContentIdentity(different.mBytes));
    ensure("an artificial FNV collision cannot replace exact typed and byte matching", !arbitrary_generation.matchesDescription(different));
}

} // namespace tut
