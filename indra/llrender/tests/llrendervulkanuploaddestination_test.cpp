/**
 * @file llrendervulkanuploaddestination_test.cpp
 * @brief Tests for Vulkan upload-destination ownership.
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
#include "lltut.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
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
    BindBufferMemory
};

enum class InvalidateSourceAt : std::uint8_t
{
    None,
    MemoryProperties,
    CreateBuffer,
    MemoryRequirements,
    AllocateMemory,
    BindBufferMemory
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
    VkBuffer         mSourceBuffer   = fakeHandle<VkBuffer>(0x6100);
    VkDeviceMemory   mSourceMemory   = fakeHandle<VkDeviceMemory>(0x7100);
    VkBuffer         mBuffer         = fakeHandle<VkBuffer>(0x6200);
    VkDeviceMemory   mMemory         = fakeHandle<VkDeviceMemory>(0x7200);
    std::uint32_t    mQueueFamily    = 2;

    MissingCommand     mMissingCommand     = MissingCommand::None;
    InvalidateSourceAt mInvalidateSourceAt = InvalidateSourceAt::None;
    bool               mDestinationPhase   = false;

    VkPhysicalDeviceMemoryProperties mMemoryProperties{};
    VkMemoryRequirements             mRequirements{ 64, 16, 0xf };
    VkResult                         mCreateResult   = VK_SUCCESS;
    VkBuffer                         mCreateOutput   = mSourceBuffer;
    VkResult                         mAllocateResult = VK_SUCCESS;
    VkDeviceMemory                   mAllocateOutput = mSourceMemory;
    VkResult                         mBindResult     = VK_SUCCESS;
    std::array<std::uint8_t, 64>     mMappedStorage{};

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
    std::vector<VkBuffer>             mDestroyedBuffers;
    std::vector<VkDeviceMemory>       mFreedMemories;
    std::vector<std::string>          mTeardownOrder;

    VulkanUploadSourceGeneration*               mSourceToInvalidate = nullptr;
    std::optional<VulkanUploadSourceGeneration> mMovedSource;
    VulkanUploadSourceDescription*              mDescriptionToMutate = nullptr;

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

void invalidateSource(InvalidateSourceAt point) noexcept
{
    if (gFakeState && gFakeState->mDestinationPhase && gFakeState->mInvalidateSourceAt == point && gFakeState->mSourceToInvalidate)
    {
        gFakeState->mMovedSource.emplace(std::move(*gFakeState->mSourceToInvalidate));
        gFakeState->mSourceToInvalidate = nullptr;
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
    if (gFakeState && physical_device == gFakeState->mPhysicalDevice && properties)
    {
        *properties            = {};
        properties->apiVersion = VK_API_VERSION_1_1;
        std::strncpy(properties->deviceName, "upload-destination-fake", VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 1);
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

VKAPI_ATTR void VKAPI_CALL fakeGetPhysicalDeviceMemoryProperties(VkPhysicalDevice                  physical_device,
                                                                 VkPhysicalDeviceMemoryProperties* properties) noexcept
{
    if (gFakeState && physical_device == gFakeState->mPhysicalDevice && properties)
    {
        *properties = gFakeState->mMemoryProperties;
        invalidateSource(InvalidateSourceAt::MemoryProperties);
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
    if (gFakeState->mDestinationPhase)
    {
        gFakeState->mAllCommandsResolvedBeforeMutation =
            gFakeState->mInstanceLookups == std::vector<std::string>{ "vkGetPhysicalDeviceMemoryProperties", "vkGetDeviceProcAddr" } &&
            gFakeState->mDeviceLookups == std::vector<std::string>{ "vkCreateBuffer",   "vkDestroyBuffer", "vkGetBufferMemoryRequirements",
                                                                    "vkAllocateMemory", "vkFreeMemory",    "vkBindBufferMemory" };
    }
    gFakeState->mBufferRecords.push_back({ create_info->sType, create_info->pNext, create_info->flags, create_info->size,
                                           create_info->usage, create_info->sharingMode, create_info->queueFamilyIndexCount,
                                           create_info->pQueueFamilyIndices });
    gFakeState->mCreateDevices.push_back(device);
    gFakeState->mCreateAllocatorNull.push_back(allocator == nullptr);
    *buffer = gFakeState->mCreateOutput;
    if (gFakeState->mDestinationPhase && gFakeState->mDescriptionToMutate)
    {
        gFakeState->mDescriptionToMutate->mHandle = { 99, 99 };
        gFakeState->mDescriptionToMutate->mBytes.fill(0xee);
    }
    invalidateSource(InvalidateSourceAt::CreateBuffer);
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
        invalidateSource(InvalidateSourceAt::MemoryRequirements);
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
    invalidateSource(InvalidateSourceAt::AllocateMemory);
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
    invalidateSource(InvalidateSourceAt::BindBufferMemory);
    return gFakeState->mBindResult;
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
        return eraseFunctionType(fakeMapMemory);
    if (std::strcmp(name, "vkUnmapMemory") == 0)
        return eraseFunctionType(fakeUnmapMemory);
    if (std::strcmp(name, "vkFlushMappedMemoryRanges") == 0)
        return eraseFunctionType(fakeFlushMappedMemoryRanges);
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

VulkanUploadSourceGeneration takeSource(VulkanUploadSourceResolutionResult&& result)
{
    tut::ensure("upload-source fixture resolution succeeds", std::holds_alternative<VulkanUploadSourceGeneration>(result));
    return std::get<VulkanUploadSourceGeneration>(std::move(result));
}

void beginDestinationPhase(FakeState& state)
{
    state.mDestinationPhase = true;
    state.mCreateOutput     = state.mBuffer;
    state.mAllocateOutput   = state.mMemory;
    state.mInstanceLookups.clear();
    state.mDeviceLookups.clear();
    state.mBufferRecords.clear();
    state.mCreateDevices.clear();
    state.mCreateAllocatorNull.clear();
    state.mRequirementBuffers.clear();
    state.mAllocateInfos.clear();
    state.mAllocateDevices.clear();
    state.mAllocateAllocatorNull.clear();
    state.mBoundBuffers.clear();
    state.mBoundMemories.clear();
    state.mBindOffsets.clear();
    state.mDestroyedBuffers.clear();
    state.mFreedMemories.clear();
    state.mTeardownOrder.clear();
    state.mAllCommandsResolvedBeforeMutation = false;
}

struct Fixture
{
    Parents                       mParents;
    VulkanUploadSourceDescription mDescription;
    VulkanUploadSourceGeneration  mSource;

    explicit Fixture(FakeState& state) :
        mParents(makeParents(state)),
        mDescription(description()),
        mSource(takeSource(resolveVulkanUploadSourceGeneration(mParents.mPhysical, mParents.mLogical, mDescription)))
    {
        beginDestinationPhase(state);
    }
};

Fixture makeFixture(FakeState& state)
{
    return Fixture(state);
}

const VulkanUploadDestinationResolutionError& requireError(const VulkanUploadDestinationResolutionResult& result)
{
    const auto* error = std::get_if<VulkanUploadDestinationResolutionError>(&result);
    tut::ensure("upload-destination resolution returns an error", error != nullptr);
    return *error;
}

void ensureError(const VulkanUploadDestinationResolutionResult& result,
                 VulkanUploadDestinationResolutionCode          code,
                 std::optional<VulkanUploadDestinationCommand>  command       = std::nullopt,
                 VkResult                                       native_result = VK_SUCCESS)
{
    const auto& error = requireError(result);
    tut::ensure("the exact upload-destination error code is reported", error.mCode == code);
    tut::ensure("the exact upload-destination command is reported", error.mCommand == command);
    tut::ensure("the exact native result is reported", error.mResult == native_result);
}

VulkanUploadDestinationGeneration takeDestination(VulkanUploadDestinationResolutionResult&& result)
{
    tut::ensure("upload-destination resolution returns a generation", std::holds_alternative<VulkanUploadDestinationGeneration>(result));
    return std::get<VulkanUploadDestinationGeneration>(std::move(result));
}

VulkanUploadDestinationResolutionResult resolve(const Fixture& fixture)
{
    return resolveVulkanUploadDestinationGeneration(fixture.mParents.mPhysical,
                                                    fixture.mParents.mLogical,
                                                    fixture.mSource,
                                                    fixture.mDescription);
}

} // namespace

namespace tut
{

struct render_vulkan_upload_destination_test
{
};

using render_vulkan_upload_destination_group  = test_group<render_vulkan_upload_destination_test>;
using render_vulkan_upload_destination_object = render_vulkan_upload_destination_group::object;
render_vulkan_upload_destination_group render_vulkan_upload_destination_tests("render Vulkan upload destination");

template<>
template<>
void render_vulkan_upload_destination_object::test<1>()
{
    static_assert(!std::is_default_constructible_v<VulkanUploadDestinationGeneration>);
    static_assert(!std::is_copy_constructible_v<VulkanUploadDestinationGeneration>);
    static_assert(!std::is_copy_assignable_v<VulkanUploadDestinationGeneration>);
    static_assert(std::is_nothrow_move_constructible_v<VulkanUploadDestinationGeneration>);
    static_assert(!std::is_move_assignable_v<VulkanUploadDestinationGeneration>);
    static_assert(noexcept(resolveVulkanUploadDestinationGeneration(std::declval<const VulkanPhysicalDeviceGeneration&>(),
                                                                    std::declval<const VulkanLogicalDeviceGeneration&>(),
                                                                    std::declval<const VulkanUploadSourceGeneration&>(),
                                                                    std::declval<const VulkanUploadSourceDescription&>())));

    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            fixture        = makeFixture(state);
        auto            moved_physical = std::move(fixture.mParents.mPhysical);
        ensureError(resolve(fixture), VulkanUploadDestinationResolutionCode::InvalidPhysicalDeviceGeneration);
        ensure("an invalid physical parent performs no destination lookup",
               state.mInstanceLookups.empty() && state.mDeviceLookups.empty() && state.mBufferRecords.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            fixture       = makeFixture(state);
        auto            moved_logical = std::move(fixture.mParents.mLogical);
        ensureError(resolve(fixture), VulkanUploadDestinationResolutionCode::InvalidLogicalDeviceGeneration);
        ensure("an invalid logical parent performs no destination lookup",
               state.mInstanceLookups.empty() && state.mDeviceLookups.empty() && state.mBufferRecords.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            fixture      = makeFixture(state);
        fixture.mDescription.mHandle = {};
        ensureError(resolve(fixture), VulkanUploadDestinationResolutionCode::InvalidDescription);
        ensure("an invalid description performs no destination lookup",
               state.mInstanceLookups.empty() && state.mDeviceLookups.empty() && state.mBufferRecords.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            fixture = makeFixture(state);
        fixture.mSource.reset();
        beginDestinationPhase(state);
        ensureError(resolve(fixture), VulkanUploadDestinationResolutionCode::InvalidUploadSourceGeneration);
        ensure("an invalid source performs no destination lookup",
               state.mInstanceLookups.empty() && state.mDeviceLookups.empty() && state.mBufferRecords.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            fixture = makeFixture(state);
        fixture.mDescription.mBytes[4] ^= 0xff;
        ensureError(resolve(fixture), VulkanUploadDestinationResolutionCode::UploadSourceDescriptionMismatch);
        ensure("a source-description mismatch performs no destination lookup",
               state.mInstanceLookups.empty() && state.mDeviceLookups.empty() && state.mBufferRecords.empty());
    }
}

template<>
template<>
void render_vulkan_upload_destination_object::test<2>()
{
    struct MissingCase
    {
        MissingCommand                 mMissing;
        VulkanUploadDestinationCommand mExpected;
    };
    constexpr std::array cases{
        MissingCase{ MissingCommand::GetPhysicalDeviceMemoryProperties, VulkanUploadDestinationCommand::GetPhysicalDeviceMemoryProperties },
        MissingCase{ MissingCommand::GetDeviceProcAddr, VulkanUploadDestinationCommand::GetDeviceProcAddr },
        MissingCase{ MissingCommand::CreateBuffer, VulkanUploadDestinationCommand::CreateBuffer },
        MissingCase{ MissingCommand::DestroyBuffer, VulkanUploadDestinationCommand::DestroyBuffer },
        MissingCase{ MissingCommand::GetBufferMemoryRequirements, VulkanUploadDestinationCommand::GetBufferMemoryRequirements },
        MissingCase{ MissingCommand::AllocateMemory, VulkanUploadDestinationCommand::AllocateMemory },
        MissingCase{ MissingCommand::FreeMemory, VulkanUploadDestinationCommand::FreeMemory },
        MissingCase{ MissingCommand::BindBufferMemory, VulkanUploadDestinationCommand::BindBufferMemory },
    };

    for (const MissingCase& test_case : cases)
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            fixture = makeFixture(state);
        state.mMissingCommand   = test_case.mMissing;
        const auto result       = resolve(fixture);
        ensureError(result, VulkanUploadDestinationResolutionCode::MissingRequiredCommand, test_case.mExpected);
        ensure("command resolution fails before the first destination mutation", state.mBufferRecords.empty());
    }
}

template<>
template<>
void render_vulkan_upload_destination_object::test<3>()
{
    FakeState       state;
    ScopedFakeState scope(state);
    auto            fixture = makeFixture(state);

    auto destination = takeDestination(resolve(fixture));
    ensure("all destination commands resolve before buffer creation", state.mAllCommandsResolvedBeforeMutation);
    ensure("one distinct exact transfer-destination vertex buffer is created",
           state.mBuffer != fixture.mSource.buffer() && state.mBufferRecords.size() == 1 &&
               state.mBufferRecords[0].mStructureType == VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO && state.mBufferRecords[0].mNext == nullptr &&
               state.mBufferRecords[0].mFlags == 0 && state.mBufferRecords[0].mSize == VULKAN_UPLOAD_SOURCE_BYTE_COUNT &&
               state.mBufferRecords[0].mUsage == (VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) &&
               state.mBufferRecords[0].mSharingMode == VK_SHARING_MODE_EXCLUSIVE && state.mBufferRecords[0].mQueueFamilyIndexCount == 0 &&
               state.mBufferRecords[0].mQueueFamilyIndices == nullptr && state.mCreateDevices == std::vector<VkDevice>{ state.mDevice } &&
               state.mCreateAllocatorNull == std::vector<bool>{ true });
    ensure("the lowest compatible pure device-local type backs one dedicated allocation",
           state.mAllocateInfos.size() == 1 && state.mAllocateInfos[0].sType == VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO &&
               state.mAllocateInfos[0].pNext == nullptr && state.mAllocateInfos[0].allocationSize == state.mRequirements.size &&
               state.mAllocateInfos[0].memoryTypeIndex == 0 && state.mAllocateDevices == std::vector<VkDevice>{ state.mDevice } &&
               state.mAllocateAllocatorNull == std::vector<bool>{ true });
    ensure("the dedicated allocation is bound at offset zero and never mapped",
           state.mRequirementBuffers == std::vector<VkBuffer>{ state.mBuffer } &&
               state.mBoundBuffers == std::vector<VkBuffer>{ state.mBuffer } &&
               state.mBoundMemories == std::vector<VkDeviceMemory>{ state.mMemory } &&
               state.mBindOffsets == std::vector<VkDeviceSize>{ 0 } &&
               std::find(state.mDeviceLookups.begin(), state.mDeviceLookups.end(), "vkMapMemory") == state.mDeviceLookups.end());
    ensure("the destination publishes exact nonresident metadata and provenance",
           destination.resourceHandle() == fixture.mDescription.mHandle &&
               destination.expectedContentIdentity() == fixture.mSource.contentIdentity() &&
               destination.byteCount() == VULKAN_UPLOAD_SOURCE_BYTE_COUNT &&
               destination.usage() == (VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) &&
               destination.buffer() == state.mBuffer && destination.memory() == state.mMemory &&
               destination.allocationSize() == state.mRequirements.size && destination.memoryTypeIndex() == 0 &&
               destination.memoryPropertyFlags() == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT && destination.isDeviceLocal() &&
               !destination.isMapped() && destination.createdFor(fixture.mParents.mPhysical, fixture.mParents.mLogical) &&
               destination.matchesDescription(fixture.mDescription) && destination.matchesUploadSource(fixture.mSource) &&
               !destination.isResident() && destination.residentContentIdentity() == 0);

    auto wrong_bytes = fixture.mDescription;
    wrong_bytes.mBytes[3] ^= 0xff;
    auto wrong_handle = fixture.mDescription;
    ++wrong_handle.mHandle.mGeneration;
    ensure("description matching authenticates the handle and all source bytes",
           !destination.matchesDescription(wrong_bytes) && !destination.matchesDescription(wrong_handle));
}

template<>
template<>
void render_vulkan_upload_destination_object::test<4>()
{
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            fixture                              = makeFixture(state);
        state.mMemoryProperties.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        auto destination                                     = takeDestination(resolve(fixture));
        ensure("host-only types are skipped and the lowest UMA device-local type is accepted without mapping",
               destination.memoryTypeIndex() == 3 && destination.isDeviceLocal() &&
                   (destination.memoryPropertyFlags() & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0 &&
                   std::find(state.mDeviceLookups.begin(), state.mDeviceLookups.end(), "vkMapMemory") == state.mDeviceLookups.end());
    }
    for (const VkMemoryPropertyFlags forbidden_flag :
         { VK_MEMORY_PROPERTY_PROTECTED_BIT, VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT, VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD })
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            fixture                              = makeFixture(state);
        state.mMemoryProperties.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | forbidden_flag;
        state.mRequirements.memoryTypeBits                   = (std::uint32_t{ 1 } << 0) | (std::uint32_t{ 1 } << 3);

        auto destination = takeDestination(resolve(fixture));
        ensure("each forbidden device-local property is skipped independently",
               destination.memoryTypeIndex() == 3 &&
                   destination.memoryPropertyFlags() ==
                       (VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            fixture = makeFixture(state);
        state.mMemoryProperties.memoryHeaps[0].flags |= VK_MEMORY_HEAP_TILE_MEMORY_BIT_QCOM;
        state.mMemoryProperties.memoryTypes[3].heapIndex = 1;
        state.mRequirements.memoryTypeBits               = (std::uint32_t{ 1 } << 0) | (std::uint32_t{ 1 } << 3);

        auto destination = takeDestination(resolve(fixture));
        ensure("a tile-backed device-local type is skipped independently",
               destination.memoryTypeIndex() == 3 &&
                   destination.memoryPropertyFlags() ==
                       (VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            fixture = makeFixture(state);
        for (std::uint32_t index = 0; index < state.mMemoryProperties.memoryTypeCount; ++index)
        {
            state.mMemoryProperties.memoryTypes[index].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        }
        ensureError(resolve(fixture), VulkanUploadDestinationResolutionCode::NoCompatibleMemoryType);
        ensure("host-only memory cannot back a resident destination",
               state.mDestroyedBuffers == std::vector<VkBuffer>{ state.mBuffer } && state.mFreedMemories.empty());
    }
}

template<>
template<>
void render_vulkan_upload_destination_object::test<5>()
{
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            fixture                 = makeFixture(state);
        state.mMemoryProperties.memoryTypeCount = 0;
        ensureError(resolve(fixture),
                    VulkanUploadDestinationResolutionCode::InvalidPhysicalDeviceMemoryProperties,
                    VulkanUploadDestinationCommand::GetPhysicalDeviceMemoryProperties);
        ensure("an invalid memory table creates no destination buffer", state.mBufferRecords.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            fixture                          = makeFixture(state);
        state.mMemoryProperties.memoryTypes[0].heapIndex = state.mMemoryProperties.memoryHeapCount;
        ensureError(resolve(fixture),
                    VulkanUploadDestinationResolutionCode::InvalidPhysicalDeviceMemoryProperties,
                    VulkanUploadDestinationCommand::GetPhysicalDeviceMemoryProperties);
    }
    for (const VkMemoryRequirements requirements :
         std::array{ VkMemoryRequirements{ 47, 16, 1 }, VkMemoryRequirements{ 64, 3, 1 }, VkMemoryRequirements{ 64, 16, 0 } })
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            fixture = makeFixture(state);
        state.mRequirements     = requirements;
        const auto result       = resolve(fixture);
        ensureError(result,
                    VulkanUploadDestinationResolutionCode::InvalidBufferMemoryRequirements,
                    VulkanUploadDestinationCommand::GetBufferMemoryRequirements);
        ensure("invalid requirements destroy only the created destination buffer",
               state.mDestroyedBuffers == std::vector<VkBuffer>{ state.mBuffer } && state.mFreedMemories.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            fixture                     = makeFixture(state);
        state.mMemoryProperties.memoryHeaps[0].size = state.mRequirements.size - 1;
        state.mRequirements.memoryTypeBits          = 1;
        ensureError(resolve(fixture), VulkanUploadDestinationResolutionCode::NoCompatibleMemoryType);
        ensure("an undersized compatible heap is rejected", state.mDestroyedBuffers == std::vector<VkBuffer>{ state.mBuffer });
    }
}

template<>
template<>
void render_vulkan_upload_destination_object::test<6>()
{
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            fixture = makeFixture(state);
        state.mCreateResult     = VK_ERROR_OUT_OF_HOST_MEMORY;
        state.mCreateOutput     = fakeHandle<VkBuffer>(0xdead);
        ensureError(resolve(fixture),
                    VulkanUploadDestinationResolutionCode::BufferCreationFailure,
                    VulkanUploadDestinationCommand::CreateBuffer,
                    VK_ERROR_OUT_OF_HOST_MEMORY);
        ensure("a failed create output is poisoned and never inspected or destroyed", state.mDestroyedBuffers.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            fixture = makeFixture(state);
        state.mCreateOutput     = VK_NULL_HANDLE;
        ensureError(resolve(fixture),
                    VulkanUploadDestinationResolutionCode::NullBufferOnSuccess,
                    VulkanUploadDestinationCommand::CreateBuffer);
        ensure("a null successful buffer output owns nothing", state.mDestroyedBuffers.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        {
            auto fixture              = makeFixture(state);
            state.mCreateOutput       = state.mSourceBuffer;
            state.mInvalidateSourceAt = InvalidateSourceAt::CreateBuffer;
            state.mSourceToInvalidate = &fixture.mSource;
            ensureError(resolve(fixture),
                        VulkanUploadDestinationResolutionCode::SourceDestinationBufferAlias,
                        VulkanUploadDestinationCommand::CreateBuffer);
            ensure("a successful source-buffer alias is neither adopted nor destroyed",
                   state.mDestroyedBuffers.empty() && state.mFreedMemories.empty() && state.mRequirementBuffers.empty() &&
                       state.mAllocateInfos.empty() && state.mBoundBuffers.empty() && state.mMovedSource &&
                       state.mMovedSource->createdFor(fixture.mParents.mPhysical, fixture.mParents.mLogical));
            state.mMovedSource->reset();
        }
        ensure("a rejected source-buffer alias leaves one source teardown obligation",
               state.mDestroyedBuffers == std::vector<VkBuffer>{ state.mSourceBuffer } &&
                   state.mFreedMemories == std::vector<VkDeviceMemory>{ state.mSourceMemory });
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            fixture = makeFixture(state);
        state.mAllocateResult   = VK_ERROR_OUT_OF_DEVICE_MEMORY;
        state.mAllocateOutput   = fakeHandle<VkDeviceMemory>(0xdead);
        ensureError(resolve(fixture),
                    VulkanUploadDestinationResolutionCode::MemoryAllocationFailure,
                    VulkanUploadDestinationCommand::AllocateMemory,
                    VK_ERROR_OUT_OF_DEVICE_MEMORY);
        ensure("a failed allocation output is poisoned and never freed",
               state.mDestroyedBuffers == std::vector<VkBuffer>{ state.mBuffer } && state.mFreedMemories.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            fixture = makeFixture(state);
        state.mAllocateOutput   = VK_NULL_HANDLE;
        ensureError(resolve(fixture),
                    VulkanUploadDestinationResolutionCode::NullMemoryOnSuccess,
                    VulkanUploadDestinationCommand::AllocateMemory);
        ensure("a null successful allocation rolls back only the buffer",
               state.mDestroyedBuffers == std::vector<VkBuffer>{ state.mBuffer } && state.mFreedMemories.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        {
            auto fixture              = makeFixture(state);
            state.mAllocateOutput     = state.mSourceMemory;
            state.mInvalidateSourceAt = InvalidateSourceAt::AllocateMemory;
            state.mSourceToInvalidate = &fixture.mSource;
            ensureError(resolve(fixture),
                        VulkanUploadDestinationResolutionCode::SourceDestinationMemoryAlias,
                        VulkanUploadDestinationCommand::AllocateMemory);
            ensure("a successful source-memory alias rolls back only the distinct destination buffer",
                   state.mDestroyedBuffers == std::vector<VkBuffer>{ state.mBuffer } && state.mFreedMemories.empty() &&
                       state.mBoundBuffers.empty() && state.mMovedSource &&
                       state.mMovedSource->createdFor(fixture.mParents.mPhysical, fixture.mParents.mLogical));
            state.mMovedSource->reset();
        }
        ensure("a rejected source-memory alias is freed once by its source owner",
               state.mDestroyedBuffers == std::vector<VkBuffer>{ state.mBuffer, state.mSourceBuffer } &&
                   state.mFreedMemories == std::vector<VkDeviceMemory>{ state.mSourceMemory });
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            fixture = makeFixture(state);
        state.mBindResult       = VK_ERROR_MEMORY_MAP_FAILED;
        ensureError(resolve(fixture),
                    VulkanUploadDestinationResolutionCode::BufferMemoryBindFailure,
                    VulkanUploadDestinationCommand::BindBufferMemory,
                    VK_ERROR_MEMORY_MAP_FAILED);
        ensure("bind failure destroys the destination buffer before freeing its allocation",
               state.mTeardownOrder == std::vector<std::string>{ "destroy-buffer", "free-memory" });
    }
}

template<>
template<>
void render_vulkan_upload_destination_object::test<7>()
{
    FakeState       state;
    ScopedFakeState scope(state);
    auto            fixture     = makeFixture(state);
    auto            destination = takeDestination(resolve(fixture));

    auto moved = std::move(destination);
    ensure("a move makes every destination accessor on the source inert",
           !destination.resourceHandle() && destination.expectedContentIdentity() == 0 && destination.byteCount() == 0 &&
               destination.usage() == 0 && destination.buffer() == VK_NULL_HANDLE && destination.memory() == VK_NULL_HANDLE &&
               destination.allocationSize() == 0 && destination.memoryTypeIndex() == 0 && destination.memoryPropertyFlags() == 0 &&
               !destination.isDeviceLocal() && !destination.isMapped() &&
               !destination.createdFor(fixture.mParents.mPhysical, fixture.mParents.mLogical) &&
               !destination.matchesDescription(fixture.mDescription) && !destination.matchesUploadSource(fixture.mSource) &&
               !destination.isResident() && destination.residentContentIdentity() == 0);
    ensure("a move preserves exact destination ownership and nonresident provenance",
           moved.resourceHandle() == fixture.mDescription.mHandle && moved.expectedContentIdentity() == fixture.mSource.contentIdentity() &&
               moved.byteCount() == VULKAN_UPLOAD_SOURCE_BYTE_COUNT &&
               moved.usage() == (VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) && moved.buffer() == state.mBuffer &&
               moved.memory() == state.mMemory && moved.matchesUploadSource(fixture.mSource) && !moved.isResident() && !moved.isMapped() &&
               moved.residentContentIdentity() == 0);

    state.mTeardownOrder.clear();
    moved.reset();
    ensure("reset destroys the destination buffer before freeing its dedicated allocation",
           state.mTeardownOrder == std::vector<std::string>{ "destroy-buffer", "free-memory" });
    ensure("reset makes every destination accessor inert",
           !moved.resourceHandle() && moved.expectedContentIdentity() == 0 && moved.byteCount() == 0 && moved.buffer() == VK_NULL_HANDLE &&
               moved.usage() == 0 && moved.memory() == VK_NULL_HANDLE && moved.allocationSize() == 0 && moved.memoryTypeIndex() == 0 &&
               moved.memoryPropertyFlags() == 0 && !moved.isDeviceLocal() && !moved.isMapped() &&
               !moved.createdFor(fixture.mParents.mPhysical, fixture.mParents.mLogical) &&
               !moved.matchesDescription(fixture.mDescription) && !moved.matchesUploadSource(fixture.mSource) && !moved.isResident() &&
               moved.residentContentIdentity() == 0);
    moved.reset();
    ensure("destination reset is idempotent", state.mTeardownOrder.size() == 2);
}

template<>
template<>
void render_vulkan_upload_destination_object::test<8>()
{
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            fixture    = makeFixture(state);
        auto            request    = fixture.mDescription;
        const auto      original   = request;
        state.mDescriptionToMutate = &request;

        auto destination = takeDestination(
            resolveVulkanUploadDestinationGeneration(fixture.mParents.mPhysical, fixture.mParents.mLogical, fixture.mSource, request));
        ensure("an adversarial native callback mutates the caller-owned description", request != original);
        ensure("resolution owns the source description before its first native callback",
               destination.matchesDescription(original) && !destination.matchesDescription(request) &&
                   destination.resourceHandle() == original.mHandle &&
                   destination.expectedContentIdentity() == fixture.mSource.contentIdentity());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            fixture      = makeFixture(state);
        auto            destination  = takeDestination(resolve(fixture));
        auto            moved_source = std::move(fixture.mSource);

        ensure("destination ownership does not retain or depend on the source object address",
               destination.createdFor(fixture.mParents.mPhysical, fixture.mParents.mLogical) &&
                   destination.matchesDescription(fixture.mDescription) && !destination.matchesUploadSource(fixture.mSource) &&
                   destination.matchesUploadSource(moved_source));
        moved_source.reset();
        ensure("a stale upload source no longer authenticates the independent destination",
               destination.createdFor(fixture.mParents.mPhysical, fixture.mParents.mLogical) &&
                   !destination.matchesUploadSource(moved_source));
    }
}

template<>
template<>
void render_vulkan_upload_destination_object::test<9>()
{
    constexpr std::array points{ InvalidateSourceAt::MemoryProperties, InvalidateSourceAt::CreateBuffer,
                                 InvalidateSourceAt::MemoryRequirements, InvalidateSourceAt::AllocateMemory,
                                 InvalidateSourceAt::BindBufferMemory };

    for (InvalidateSourceAt point : points)
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            fixture   = makeFixture(state);
        state.mInvalidateSourceAt = point;
        state.mSourceToInvalidate = &fixture.mSource;

        const auto result = resolve(fixture);
        ensureError(result, VulkanUploadDestinationResolutionCode::InvalidUploadSourceGeneration);
        ensure("a stale upload source never publishes a destination generation",
               !std::holds_alternative<VulkanUploadDestinationGeneration>(result));

        if (point == InvalidateSourceAt::MemoryProperties)
        {
            ensure("pre-mutation source invalidation owns no destination resource",
                   state.mDestroyedBuffers.empty() && state.mFreedMemories.empty());
        }
        else
        {
            ensure("post-creation source invalidation destroys the destination buffer",
                   state.mDestroyedBuffers == std::vector<VkBuffer>{ state.mBuffer });
        }
        if (point == InvalidateSourceAt::AllocateMemory || point == InvalidateSourceAt::BindBufferMemory)
        {
            ensure("post-allocation source invalidation frees the destination allocation",
                   state.mFreedMemories == std::vector<VkDeviceMemory>{ state.mMemory });
        }
        else
        {
            ensure("pre-allocation source invalidation frees no destination memory", state.mFreedMemories.empty());
        }
        if (point == InvalidateSourceAt::BindBufferMemory)
        {
            ensure("stale-source rollback preserves destination teardown order",
                   state.mTeardownOrder == std::vector<std::string>{ "destroy-buffer", "free-memory" });
        }
    }
}

} // namespace tut
