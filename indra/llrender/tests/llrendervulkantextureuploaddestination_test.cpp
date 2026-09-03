/**
 * @file llrendervulkantextureuploaddestination_test.cpp
 * @brief Tests for Vulkan streamed-texture image ownership.
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

#include "llrendervulkantextureuploaddestination.h"
#include "lltextureuploaddiagnostic.h"
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

constexpr VkFormatFeatureFlags REQUIRED_FORMAT_FEATURES =
    VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT |
    VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
constexpr VkImageUsageFlags IMAGE_USAGE = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

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

template<typename Handle>
std::uintptr_t handleBits(Handle handle) noexcept
{
    if constexpr (std::is_pointer_v<Handle>)
    {
        return reinterpret_cast<std::uintptr_t>(handle);
    }
    else
    {
        return static_cast<std::uintptr_t>(handle);
    }
}

enum class MissingCommand : std::uint8_t
{
    None,
    GetPhysicalDeviceFormatProperties,
    GetPhysicalDeviceImageFormatProperties,
    GetPhysicalDeviceMemoryProperties,
    GetDeviceProcAddr,
    CreateImage,
    DestroyImage,
    GetImageMemoryRequirements2,
    AllocateMemory,
    FreeMemory,
    BindImageMemory,
    CreateImageView,
    DestroyImageView
};

enum class CallbackPoint : std::uint8_t
{
    None,
    CommandLookup,
    FormatProperties,
    ImageFormatProperties,
    MemoryProperties,
    CreateImage,
    MemoryRequirements,
    AllocateMemory,
    BindImageMemory,
    CreateImageView
};

enum class TeardownPoint : std::uint8_t
{
    None,
    DestroyImageView,
    DestroyImage,
    FreeMemory
};

struct ImageFormatQueryRecord
{
    VkPhysicalDevice   mPhysicalDevice = VK_NULL_HANDLE;
    VkFormat           mFormat         = VK_FORMAT_UNDEFINED;
    VkImageType        mType           = VK_IMAGE_TYPE_MAX_ENUM;
    VkImageTiling      mTiling         = VK_IMAGE_TILING_MAX_ENUM;
    VkImageUsageFlags  mUsage          = 0;
    VkImageCreateFlags mFlags          = 0;
};

struct MemoryRequirementsRecord
{
    VkDevice        mDevice                 = VK_NULL_HANDLE;
    VkStructureType mInfoStructureType      = VK_STRUCTURE_TYPE_MAX_ENUM;
    bool            mInfoNextNull           = false;
    VkImage         mImage                  = VK_NULL_HANDLE;
    VkStructureType mOutputStructureType    = VK_STRUCTURE_TYPE_MAX_ENUM;
    VkStructureType mDedicatedStructureType = VK_STRUCTURE_TYPE_MAX_ENUM;
    bool            mDedicatedNextNull      = false;
};

struct AllocationRecord
{
    VkStructureType mStructureType          = VK_STRUCTURE_TYPE_MAX_ENUM;
    VkDeviceSize    mAllocationSize         = 0;
    std::uint32_t   mMemoryTypeIndex        = 0;
    VkStructureType mDedicatedStructureType = VK_STRUCTURE_TYPE_MAX_ENUM;
    bool            mDedicatedNextNull      = false;
    VkImage         mDedicatedImage         = VK_NULL_HANDLE;
    VkBuffer        mDedicatedBuffer        = VK_NULL_HANDLE;
};

struct FakeState
{
    VkInstance       mInstance       = fakeHandle<VkInstance>(0x1000);
    VkSurfaceKHR     mSurface        = fakeHandle<VkSurfaceKHR>(0x2000);
    VkPhysicalDevice mPhysicalDevice = fakeHandle<VkPhysicalDevice>(0x3000);
    VkDevice         mDevice         = fakeHandle<VkDevice>(0x4000);
    VkQueue          mQueue          = fakeHandle<VkQueue>(0x5000);
    VkImage          mImageOutput    = fakeHandle<VkImage>(0x6100);
    VkDeviceMemory   mMemoryOutput   = fakeHandle<VkDeviceMemory>(0x7100);
    VkImageView      mViewOutput     = fakeHandle<VkImageView>(0x8100);
    std::uint32_t    mQueueFamily    = 2;

    bool           mOwnerPhase              = false;
    MissingCommand mMissing                 = MissingCommand::None;
    CallbackPoint  mInvalidateAt            = CallbackPoint::None;
    bool           mInvalidatePhysical      = false;
    CallbackPoint  mMutateDescriptionAt     = CallbackPoint::None;
    bool           mReenterCreateImage      = false;
    bool           mReentryAttempted        = false;
    bool           mReentrySucceeded        = false;
    std::size_t    mInvalidateOwnerLookupAt = 0;
    std::size_t    mOwnerLookupCalls        = 0;

    VkFormatProperties               mFormatProperties{};
    VkImageFormatProperties          mImageFormatProperties{};
    VkPhysicalDeviceMemoryProperties mMemoryProperties{};
    VkMemoryRequirements             mMemoryRequirements{ 4096, 256, 0xf };
    VkBool32                         mPrefersDedicated  = VK_TRUE;
    VkBool32                         mRequiresDedicated = VK_FALSE;
    VkResult                         mImageFormatResult = VK_SUCCESS;
    VkResult                         mCreateImageResult = VK_SUCCESS;
    VkResult                         mAllocateResult    = VK_SUCCESS;
    VkResult                         mBindResult        = VK_SUCCESS;
    VkResult                         mCreateViewResult  = VK_SUCCESS;

    std::vector<std::string> mInstanceLookups;
    std::vector<std::string> mDeviceLookups;
    bool                     mAllCommandsResolvedBeforeCapability = false;
    bool                     mAllCommandsResolvedBeforeCreate     = false;

    std::size_t                           mFormatQueryCalls          = 0;
    VkPhysicalDevice                      mFormatQueryPhysicalDevice = VK_NULL_HANDLE;
    VkFormat                              mFormatQueryFormat         = VK_FORMAT_UNDEFINED;
    std::vector<ImageFormatQueryRecord>   mImageFormatQueries;
    std::size_t                           mMemoryPropertyCalls = 0;
    std::vector<VkImageCreateInfo>        mImageCreateInfos;
    std::vector<VkDevice>                 mImageCreateDevices;
    std::vector<bool>                     mImageCreateAllocatorNull;
    std::vector<MemoryRequirementsRecord> mMemoryRequirementRecords;
    std::vector<AllocationRecord>         mAllocationRecords;
    std::vector<VkDevice>                 mAllocationDevices;
    std::vector<bool>                     mAllocationAllocatorNull;
    std::vector<VkImage>                  mBoundImages;
    std::vector<VkDeviceMemory>           mBoundMemories;
    std::vector<VkDeviceSize>             mBindOffsets;
    std::vector<VkDevice>                 mBindDevices;
    std::vector<VkImageViewCreateInfo>    mViewCreateInfos;
    std::vector<VkDevice>                 mViewCreateDevices;
    std::vector<bool>                     mViewCreateAllocatorNull;
    std::vector<VkImageView>              mDestroyedViews;
    std::vector<VkImage>                  mDestroyedImages;
    std::vector<VkDeviceMemory>           mFreedMemories;
    std::vector<VkDevice>                 mDestroyViewDevices;
    std::vector<VkDevice>                 mDestroyImageDevices;
    std::vector<VkDevice>                 mFreeMemoryDevices;
    std::vector<bool>                     mDestroyViewAllocatorNull;
    std::vector<bool>                     mDestroyImageAllocatorNull;
    std::vector<bool>                     mFreeMemoryAllocatorNull;
    std::vector<std::string>              mTeardownOrder;

    VulkanPhysicalDeviceGeneration*                         mPhysicalToInvalidate = nullptr;
    VulkanLogicalDeviceGeneration*                          mLogicalToInvalidate  = nullptr;
    std::optional<VulkanPhysicalDeviceGeneration>           mMovedPhysical;
    std::optional<VulkanLogicalDeviceGeneration>            mMovedLogical;
    VulkanTextureUploadDestinationDescription*              mDescriptionToMutate = nullptr;
    const VulkanPhysicalDeviceGeneration*                   mReentryPhysical     = nullptr;
    const VulkanLogicalDeviceGeneration*                    mReentryLogical      = nullptr;
    std::optional<VulkanTextureUploadDestinationGeneration> mReenteredGeneration;
    VulkanTextureUploadDestinationGeneration*               mResetToReenter        = nullptr;
    TeardownPoint                                           mReenterResetAt        = TeardownPoint::None;
    bool                                                    mResetReentryAttempted = false;

    FakeState()
    {
        mFormatProperties.optimalTilingFeatures = REQUIRED_FORMAT_FEATURES;
        mImageFormatProperties.maxExtent        = { 64, 64, 1 };
        mImageFormatProperties.maxMipLevels     = 8;
        mImageFormatProperties.maxArrayLayers   = 4;
        mImageFormatProperties.sampleCounts     = VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_4_BIT;
        mImageFormatProperties.maxResourceSize  = 1;

        mMemoryProperties.memoryHeapCount              = 2;
        mMemoryProperties.memoryHeaps[0].size          = 1ULL << 30;
        mMemoryProperties.memoryHeaps[0].flags         = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;
        mMemoryProperties.memoryHeaps[1].size          = 1ULL << 29;
        mMemoryProperties.memoryTypeCount              = 4;
        mMemoryProperties.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        mMemoryProperties.memoryTypes[0].heapIndex     = 0;
        mMemoryProperties.memoryTypes[1].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        mMemoryProperties.memoryTypes[1].heapIndex     = 1;
        mMemoryProperties.memoryTypes[2].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_PROTECTED_BIT;
        mMemoryProperties.memoryTypes[2].heapIndex     = 0;
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

void adversarialCallback(CallbackPoint point) noexcept
{
    if (!gFakeState)
    {
        return;
    }
    if (gFakeState->mMutateDescriptionAt == point && gFakeState->mDescriptionToMutate)
    {
        gFakeState->mMutateDescriptionAt = CallbackPoint::None;
        ++gFakeState->mDescriptionToMutate->mExpectedRevision;
    }
    if (gFakeState->mInvalidateAt != point)
    {
        return;
    }

    gFakeState->mInvalidateAt = CallbackPoint::None;
    if (gFakeState->mInvalidatePhysical && gFakeState->mPhysicalToInvalidate)
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
        std::strncpy(properties->deviceName, "texture-upload-destination-fake", VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 1);
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

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateDevice(VkPhysicalDevice,
                                                const VkDeviceCreateInfo*,
                                                const VkAllocationCallbacks*,
                                                VkDevice* device) noexcept
{
    if (!gFakeState || !device)
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

VKAPI_ATTR void VKAPI_CALL fakeGetPhysicalDeviceFormatProperties(VkPhysicalDevice    physical_device,
                                                                 VkFormat            format,
                                                                 VkFormatProperties* properties) noexcept
{
    if (gFakeState && physical_device == gFakeState->mPhysicalDevice && properties)
    {
        gFakeState->mAllCommandsResolvedBeforeCapability =
            gFakeState->mInstanceLookups == std::vector<std::string>{ "vkGetPhysicalDeviceFormatProperties",
                                                                      "vkGetPhysicalDeviceImageFormatProperties",
                                                                      "vkGetPhysicalDeviceMemoryProperties", "vkGetDeviceProcAddr" } &&
            gFakeState->mDeviceLookups ==
                std::vector<std::string>{ "vkCreateImage",     "vkDestroyImage",    "vkGetImageMemoryRequirements2",
                                          "vkAllocateMemory",  "vkFreeMemory",      "vkBindImageMemory",
                                          "vkCreateImageView", "vkDestroyImageView" };
        ++gFakeState->mFormatQueryCalls;
        gFakeState->mFormatQueryPhysicalDevice = physical_device;
        gFakeState->mFormatQueryFormat         = format;
        *properties                            = gFakeState->mFormatProperties;
        adversarialCallback(CallbackPoint::FormatProperties);
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeGetPhysicalDeviceImageFormatProperties(VkPhysicalDevice         physical_device,
                                                                          VkFormat                 format,
                                                                          VkImageType              type,
                                                                          VkImageTiling            tiling,
                                                                          VkImageUsageFlags        usage,
                                                                          VkImageCreateFlags       flags,
                                                                          VkImageFormatProperties* properties) noexcept
{
    if (!gFakeState || physical_device != gFakeState->mPhysicalDevice || !properties)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    gFakeState->mImageFormatQueries.push_back({ physical_device, format, type, tiling, usage, flags });
    *properties = gFakeState->mImageFormatProperties;
    adversarialCallback(CallbackPoint::ImageFormatProperties);
    return gFakeState->mImageFormatResult;
}

VKAPI_ATTR void VKAPI_CALL fakeGetPhysicalDeviceMemoryProperties(VkPhysicalDevice                  physical_device,
                                                                 VkPhysicalDeviceMemoryProperties* properties) noexcept
{
    if (gFakeState && physical_device == gFakeState->mPhysicalDevice && properties)
    {
        ++gFakeState->mMemoryPropertyCalls;
        *properties = gFakeState->mMemoryProperties;
        adversarialCallback(CallbackPoint::MemoryProperties);
    }
}

void maybeReenterCreateImage() noexcept
{
    if (!gFakeState || !gFakeState->mReenterCreateImage || gFakeState->mReentryAttempted || !gFakeState->mReentryPhysical ||
        !gFakeState->mReentryLogical)
    {
        return;
    }
    gFakeState->mReentryAttempted = true;
    auto result                   = resolveVulkanTextureUploadDestinationGeneration(*gFakeState->mReentryPhysical,
                                                                                    *gFakeState->mReentryLogical,
                                                                                    vulkanTextureUploadDestinationDescription());
    if (auto* generation = std::get_if<VulkanTextureUploadDestinationGeneration>(&result))
    {
        gFakeState->mReenteredGeneration.emplace(std::move(*generation));
        gFakeState->mReentrySucceeded = true;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateImage(VkDevice                     device,
                                               const VkImageCreateInfo*     create_info,
                                               const VkAllocationCallbacks* allocation_callbacks,
                                               VkImage*                     image) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || !create_info || !image)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (gFakeState->mImageCreateInfos.empty())
    {
        gFakeState->mAllCommandsResolvedBeforeCreate =
            gFakeState->mInstanceLookups == std::vector<std::string>{ "vkGetPhysicalDeviceFormatProperties",
                                                                      "vkGetPhysicalDeviceImageFormatProperties",
                                                                      "vkGetPhysicalDeviceMemoryProperties", "vkGetDeviceProcAddr" } &&
            gFakeState->mDeviceLookups ==
                std::vector<std::string>{ "vkCreateImage",     "vkDestroyImage",    "vkGetImageMemoryRequirements2",
                                          "vkAllocateMemory",  "vkFreeMemory",      "vkBindImageMemory",
                                          "vkCreateImageView", "vkDestroyImageView" };
    }
    gFakeState->mImageCreateInfos.push_back(*create_info);
    gFakeState->mImageCreateDevices.push_back(device);
    gFakeState->mImageCreateAllocatorNull.push_back(allocation_callbacks == nullptr);
    maybeReenterCreateImage();
    *image = gFakeState->mImageOutput;
    adversarialCallback(CallbackPoint::CreateImage);
    return gFakeState->mCreateImageResult;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks* allocation_callbacks) noexcept
{
    if (gFakeState)
    {
        gFakeState->mDestroyedImages.push_back(image);
        gFakeState->mDestroyImageDevices.push_back(device);
        gFakeState->mDestroyImageAllocatorNull.push_back(allocation_callbacks == nullptr);
        gFakeState->mTeardownOrder.emplace_back("destroy-image");
        if (gFakeState->mReenterResetAt == TeardownPoint::DestroyImage && !gFakeState->mResetReentryAttempted &&
            gFakeState->mResetToReenter)
        {
            gFakeState->mResetReentryAttempted = true;
            gFakeState->mResetToReenter->reset();
        }
    }
}

VKAPI_ATTR void VKAPI_CALL fakeGetImageMemoryRequirements2(VkDevice                              device,
                                                           const VkImageMemoryRequirementsInfo2* info,
                                                           VkMemoryRequirements2*                requirements) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || !info || !requirements)
    {
        return;
    }
    auto* dedicated = static_cast<VkMemoryDedicatedRequirements*>(requirements->pNext);
    gFakeState->mMemoryRequirementRecords.push_back({ device, info->sType, info->pNext == nullptr, info->image, requirements->sType,
                                                      dedicated ? dedicated->sType : VK_STRUCTURE_TYPE_MAX_ENUM,
                                                      dedicated && dedicated->pNext == nullptr });
    requirements->memoryRequirements = gFakeState->mMemoryRequirements;
    if (dedicated)
    {
        dedicated->prefersDedicatedAllocation  = gFakeState->mPrefersDedicated;
        dedicated->requiresDedicatedAllocation = gFakeState->mRequiresDedicated;
    }
    adversarialCallback(CallbackPoint::MemoryRequirements);
}

VKAPI_ATTR VkResult VKAPI_CALL fakeAllocateMemory(VkDevice                     device,
                                                  const VkMemoryAllocateInfo*  allocate_info,
                                                  const VkAllocationCallbacks* allocation_callbacks,
                                                  VkDeviceMemory*              memory) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || !allocate_info || !memory)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    const auto* dedicated = static_cast<const VkMemoryDedicatedAllocateInfo*>(allocate_info->pNext);
    gFakeState->mAllocationRecords.push_back({ allocate_info->sType, allocate_info->allocationSize, allocate_info->memoryTypeIndex,
                                               dedicated ? dedicated->sType : VK_STRUCTURE_TYPE_MAX_ENUM,
                                               dedicated && dedicated->pNext == nullptr, dedicated ? dedicated->image : VK_NULL_HANDLE,
                                               dedicated ? dedicated->buffer : VK_NULL_HANDLE });
    gFakeState->mAllocationDevices.push_back(device);
    gFakeState->mAllocationAllocatorNull.push_back(allocation_callbacks == nullptr);
    *memory = gFakeState->mMemoryOutput;
    adversarialCallback(CallbackPoint::AllocateMemory);
    return gFakeState->mAllocateResult;
}

VKAPI_ATTR void VKAPI_CALL fakeFreeMemory(VkDevice                     device,
                                          VkDeviceMemory               memory,
                                          const VkAllocationCallbacks* allocation_callbacks) noexcept
{
    if (gFakeState)
    {
        gFakeState->mFreedMemories.push_back(memory);
        gFakeState->mFreeMemoryDevices.push_back(device);
        gFakeState->mFreeMemoryAllocatorNull.push_back(allocation_callbacks == nullptr);
        gFakeState->mTeardownOrder.emplace_back("free-memory");
        if (gFakeState->mReenterResetAt == TeardownPoint::FreeMemory && !gFakeState->mResetReentryAttempted && gFakeState->mResetToReenter)
        {
            gFakeState->mResetReentryAttempted = true;
            gFakeState->mResetToReenter->reset();
        }
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeBindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize offset) noexcept
{
    if (!gFakeState)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    gFakeState->mBoundImages.push_back(image);
    gFakeState->mBoundMemories.push_back(memory);
    gFakeState->mBindOffsets.push_back(offset);
    gFakeState->mBindDevices.push_back(device);
    adversarialCallback(CallbackPoint::BindImageMemory);
    return gFakeState->mBindResult;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateImageView(VkDevice                     device,
                                                   const VkImageViewCreateInfo* create_info,
                                                   const VkAllocationCallbacks* allocation_callbacks,
                                                   VkImageView*                 image_view) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || !create_info || !image_view)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    gFakeState->mViewCreateInfos.push_back(*create_info);
    gFakeState->mViewCreateDevices.push_back(device);
    gFakeState->mViewCreateAllocatorNull.push_back(allocation_callbacks == nullptr);
    *image_view = gFakeState->mViewOutput;
    adversarialCallback(CallbackPoint::CreateImageView);
    return gFakeState->mCreateViewResult;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyImageView(VkDevice                     device,
                                                VkImageView                  image_view,
                                                const VkAllocationCallbacks* allocation_callbacks) noexcept
{
    if (gFakeState)
    {
        gFakeState->mDestroyedViews.push_back(image_view);
        gFakeState->mDestroyViewDevices.push_back(device);
        gFakeState->mDestroyViewAllocatorNull.push_back(allocation_callbacks == nullptr);
        gFakeState->mTeardownOrder.emplace_back("destroy-view");
        if (gFakeState->mReenterResetAt == TeardownPoint::DestroyImageView && !gFakeState->mResetReentryAttempted &&
            gFakeState->mResetToReenter)
        {
            gFakeState->mResetReentryAttempted = true;
            gFakeState->mResetToReenter->reset();
        }
    }
}

PFN_vkVoidFunction ownerLookupResult(PFN_vkVoidFunction result) noexcept
{
    if (gFakeState && gFakeState->mOwnerPhase)
    {
        ++gFakeState->mOwnerLookupCalls;
        if (gFakeState->mInvalidateOwnerLookupAt == gFakeState->mOwnerLookupCalls)
        {
            adversarialCallback(CallbackPoint::CommandLookup);
        }
    }
    return result;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL fakeGetDeviceProcAddr(VkDevice device, const char* name) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || !name)
    {
        return nullptr;
    }
    gFakeState->mDeviceLookups.emplace_back(name);
    if (std::strcmp(name, "vkCreateImage") == 0)
        return ownerLookupResult(gFakeState->mMissing == MissingCommand::CreateImage ? nullptr : eraseFunctionType(fakeCreateImage));
    if (std::strcmp(name, "vkDestroyImage") == 0)
        return ownerLookupResult(gFakeState->mMissing == MissingCommand::DestroyImage ? nullptr : eraseFunctionType(fakeDestroyImage));
    if (std::strcmp(name, "vkGetImageMemoryRequirements2") == 0)
        return ownerLookupResult(gFakeState->mMissing == MissingCommand::GetImageMemoryRequirements2
                                     ? nullptr
                                     : eraseFunctionType(fakeGetImageMemoryRequirements2));
    if (std::strcmp(name, "vkAllocateMemory") == 0)
        return ownerLookupResult(gFakeState->mMissing == MissingCommand::AllocateMemory ? nullptr : eraseFunctionType(fakeAllocateMemory));
    if (std::strcmp(name, "vkFreeMemory") == 0)
        return ownerLookupResult(gFakeState->mMissing == MissingCommand::FreeMemory ? nullptr : eraseFunctionType(fakeFreeMemory));
    if (std::strcmp(name, "vkBindImageMemory") == 0)
        return ownerLookupResult(gFakeState->mMissing == MissingCommand::BindImageMemory ? nullptr
                                                                                         : eraseFunctionType(fakeBindImageMemory));
    if (std::strcmp(name, "vkCreateImageView") == 0)
        return ownerLookupResult(gFakeState->mMissing == MissingCommand::CreateImageView ? nullptr
                                                                                         : eraseFunctionType(fakeCreateImageView));
    if (std::strcmp(name, "vkDestroyImageView") == 0)
        return ownerLookupResult(gFakeState->mMissing == MissingCommand::DestroyImageView ? nullptr
                                                                                          : eraseFunctionType(fakeDestroyImageView));
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

    if (gFakeState->mOwnerPhase)
    {
        gFakeState->mInstanceLookups.emplace_back(name);
    }
    if (std::strcmp(name, "vkGetPhysicalDeviceFormatProperties") == 0)
        return ownerLookupResult(gFakeState->mMissing == MissingCommand::GetPhysicalDeviceFormatProperties
                                     ? nullptr
                                     : eraseFunctionType(fakeGetPhysicalDeviceFormatProperties));
    if (std::strcmp(name, "vkGetPhysicalDeviceImageFormatProperties") == 0)
        return ownerLookupResult(gFakeState->mMissing == MissingCommand::GetPhysicalDeviceImageFormatProperties
                                     ? nullptr
                                     : eraseFunctionType(fakeGetPhysicalDeviceImageFormatProperties));
    if (std::strcmp(name, "vkGetPhysicalDeviceMemoryProperties") == 0)
        return ownerLookupResult(gFakeState->mMissing == MissingCommand::GetPhysicalDeviceMemoryProperties
                                     ? nullptr
                                     : eraseFunctionType(fakeGetPhysicalDeviceMemoryProperties));
    if (std::strcmp(name, "vkGetDeviceProcAddr") == 0)
        return ownerLookupResult(gFakeState->mMissing == MissingCommand::GetDeviceProcAddr ? nullptr
                                                                                           : eraseFunctionType(fakeGetDeviceProcAddr));
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

    state.mOwnerPhase = true;
    state.mInstanceLookups.clear();
    state.mDeviceLookups.clear();
    state.mOwnerLookupCalls = 0;
    return { std::move(physical), std::move(logical) };
}

const VulkanTextureUploadDestinationResolutionError& requireError(const VulkanTextureUploadDestinationResolutionResult& result)
{
    const auto* error = std::get_if<VulkanTextureUploadDestinationResolutionError>(&result);
    tut::ensure("texture-upload destination resolution returns an error", error != nullptr);
    return *error;
}

void ensureError(const VulkanTextureUploadDestinationResolutionResult&   result,
                 VulkanTextureUploadDestinationResolutionCode            code,
                 std::optional<VulkanTextureUploadDestinationCommand>    command         = std::nullopt,
                 VkResult                                                native_result   = VK_SUCCESS,
                 std::optional<VulkanTextureUploadDestinationCapability> capability      = std::nullopt,
                 std::uint64_t                                           required_value  = 0,
                 std::uint64_t                                           available_value = 0)
{
    const auto& error = requireError(result);
    tut::ensure("the exact texture-upload destination error code is reported", error.mCode == code);
    tut::ensure("the exact texture-upload destination command is reported", error.mCommand == command);
    tut::ensure("the exact texture-upload destination native result is reported", error.mResult == native_result);
    tut::ensure("the exact texture-upload destination capability is reported", error.mCapability == capability);
    tut::ensure("the exact required capability value is reported", error.mRequiredValue == required_value);
    tut::ensure("the exact available capability value is reported", error.mAvailableValue == available_value);
}

VulkanTextureUploadDestinationGeneration takeGeneration(VulkanTextureUploadDestinationResolutionResult&& result)
{
    tut::ensure("texture-upload destination resolution returns a generation",
                std::holds_alternative<VulkanTextureUploadDestinationGeneration>(result));
    return std::get<VulkanTextureUploadDestinationGeneration>(std::move(result));
}

VulkanTextureUploadDestinationResolutionResult resolve(
    const Parents&                                   parents,
    const VulkanTextureUploadDestinationDescription& description = vulkanTextureUploadDestinationDescription())
{
    return resolveVulkanTextureUploadDestinationGeneration(parents.mPhysical, parents.mLogical, description);
}

bool sameExtent(VkExtent3D left, VkExtent3D right) noexcept
{
    return left.width == right.width && left.height == right.height && left.depth == right.depth;
}

bool sameRange(VkImageSubresourceRange left, VkImageSubresourceRange right) noexcept
{
    return left.aspectMask == right.aspectMask && left.baseMipLevel == right.baseMipLevel && left.levelCount == right.levelCount &&
           left.baseArrayLayer == right.baseArrayLayer && left.layerCount == right.layerCount;
}

} // namespace

namespace tut
{

struct render_vulkan_texture_upload_destination_test
{
};

using render_vulkan_texture_upload_destination_group  = test_group<render_vulkan_texture_upload_destination_test>;
using render_vulkan_texture_upload_destination_object = render_vulkan_texture_upload_destination_group::object;
render_vulkan_texture_upload_destination_group render_vulkan_texture_upload_destination_tests("render Vulkan texture upload destination");

template<>
template<>
void render_vulkan_texture_upload_destination_object::test<1>()
{
    static_assert(!std::is_default_constructible_v<VulkanTextureUploadDestinationGeneration>);
    static_assert(!std::is_copy_constructible_v<VulkanTextureUploadDestinationGeneration>);
    static_assert(!std::is_copy_assignable_v<VulkanTextureUploadDestinationGeneration>);
    static_assert(std::is_nothrow_move_constructible_v<VulkanTextureUploadDestinationGeneration>);
    static_assert(!std::is_move_assignable_v<VulkanTextureUploadDestinationGeneration>);
    static_assert(std::is_nothrow_destructible_v<VulkanTextureUploadDestinationGeneration>);
    static_assert(
        noexcept(resolveVulkanTextureUploadDestinationGeneration(std::declval<const VulkanPhysicalDeviceGeneration&>(),
                                                                 std::declval<const VulkanLogicalDeviceGeneration&>(),
                                                                 std::declval<const VulkanTextureUploadDestinationDescription&>())));

    constexpr auto canonical = vulkanTextureUploadDestinationDescription();
    static_assert(canonical.mHandle == LLRenderContract::StreamingUploadHandles{}.mReplacementImage);
    static_assert(canonical.mExpectedRevision == LLRenderContract::TEXTURE_UPLOAD_REVISION);
    static_assert(canonical.mResidentExtent.mWidth == LLRenderContract::TEXTURE_UPLOAD_RESIDENT_WIDTH);
    static_assert(canonical.mResidentExtent.mHeight == LLRenderContract::TEXTURE_UPLOAD_RESIDENT_HEIGHT);
    static_assert(canonical.mLogicalExtent.mWidth == LLRenderContract::TEXTURE_UPLOAD_LOGICAL_WIDTH);
    static_assert(canonical.mLogicalExtent.mHeight == LLRenderContract::TEXTURE_UPLOAD_LOGICAL_HEIGHT);
    static_assert(canonical.mResidentDiscard == LLRenderContract::TEXTURE_UPLOAD_RESIDENT_DISCARD);
    static_assert(canonical.mFormat == LLRenderContract::PixelFormat::RGBA8Unorm);
    static_assert(canonical.mMipLevels == LLRenderContract::TEXTURE_UPLOAD_MIP_LEVELS);
    static_assert(canonical.mArrayLayers == 1);
    static_assert(canonical.mSamples == 1);
    static_assert(canonical.mInitialState == LLRenderContract::ImageState::Undefined);

    const LLRenderContract::TextureUploadCase diagnostic_case = LLRenderContract::makeTextureUploadCase();
    ensure("the owner description maps the diagnostic case without a second shape definition",
           canonical.mHandle == diagnostic_case.mInputs.mHandles.mReplacementImage &&
               canonical.mExpectedRevision == diagnostic_case.mInputs.mRevision &&
               canonical.mResidentExtent.mWidth == diagnostic_case.mInputs.mExtent.mWidth &&
               canonical.mResidentExtent.mHeight == diagnostic_case.mInputs.mExtent.mHeight &&
               canonical.mLogicalExtent.mWidth == diagnostic_case.mInputs.mLogicalExtent.mWidth &&
               canonical.mLogicalExtent.mHeight == diagnostic_case.mInputs.mLogicalExtent.mHeight &&
               canonical.mResidentDiscard == diagnostic_case.mInputs.mResidentDiscard &&
               canonical.mFormat == diagnostic_case.mInputs.mSourceFormat && canonical.mInitialState == diagnostic_case.mInputs.mBefore);

    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents        = makeParents(state);
        auto            moved_physical = std::move(parents.mPhysical);
        (void)moved_physical;
        ensureError(resolve(parents), VulkanTextureUploadDestinationResolutionCode::InvalidPhysicalDeviceGeneration);
        ensure("an invalid physical parent performs no owner lookup",
               state.mInstanceLookups.empty() && state.mDeviceLookups.empty() && state.mImageCreateInfos.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents       = makeParents(state);
        auto            moved_logical = std::move(parents.mLogical);
        (void)moved_logical;
        ensureError(resolve(parents), VulkanTextureUploadDestinationResolutionCode::InvalidLogicalDeviceGeneration);
        ensure("an invalid logical parent performs no owner lookup",
               state.mInstanceLookups.empty() && state.mDeviceLookups.empty() && state.mImageCreateInfos.empty());
    }
    {
        std::array<VulkanTextureUploadDestinationDescription, 13> invalid{};
        invalid.fill(canonical);
        invalid[0].mHandle = {};
        ++invalid[1].mExpectedRevision;
        ++invalid[2].mResidentExtent.mWidth;
        ++invalid[3].mResidentExtent.mHeight;
        ++invalid[4].mLogicalExtent.mWidth;
        ++invalid[5].mLogicalExtent.mHeight;
        ++invalid[6].mResidentDiscard;
        invalid[7].mFormat = LLRenderContract::PixelFormat::RGBA8Srgb;
        ++invalid[8].mMipLevels;
        ++invalid[9].mArrayLayers;
        ++invalid[10].mSamples;
        invalid[11].mInitialState = LLRenderContract::ImageState::ShaderRead;
        ++invalid[12].mHandle.mGeneration;

        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        for (const auto& description : invalid)
        {
            ensureError(resolve(parents, description), VulkanTextureUploadDestinationResolutionCode::InvalidDescription);
        }
        ensure("every noncanonical field is rejected before command lookup or mutation",
               state.mInstanceLookups.empty() && state.mDeviceLookups.empty() && state.mFormatQueryCalls == 0 &&
                   state.mImageCreateInfos.empty());
    }
}

template<>
template<>
void render_vulkan_texture_upload_destination_object::test<2>()
{
    struct MissingCase
    {
        MissingCommand                        mMissing;
        VulkanTextureUploadDestinationCommand mExpected;
    };
    constexpr std::array cases{
        MissingCase{ MissingCommand::GetPhysicalDeviceFormatProperties,
                     VulkanTextureUploadDestinationCommand::GetPhysicalDeviceFormatProperties },
        MissingCase{ MissingCommand::GetPhysicalDeviceImageFormatProperties,
                     VulkanTextureUploadDestinationCommand::GetPhysicalDeviceImageFormatProperties },
        MissingCase{ MissingCommand::GetPhysicalDeviceMemoryProperties,
                     VulkanTextureUploadDestinationCommand::GetPhysicalDeviceMemoryProperties },
        MissingCase{ MissingCommand::GetDeviceProcAddr, VulkanTextureUploadDestinationCommand::GetDeviceProcAddr },
        MissingCase{ MissingCommand::CreateImage, VulkanTextureUploadDestinationCommand::CreateImage },
        MissingCase{ MissingCommand::DestroyImage, VulkanTextureUploadDestinationCommand::DestroyImage },
        MissingCase{ MissingCommand::GetImageMemoryRequirements2, VulkanTextureUploadDestinationCommand::GetImageMemoryRequirements2 },
        MissingCase{ MissingCommand::AllocateMemory, VulkanTextureUploadDestinationCommand::AllocateMemory },
        MissingCase{ MissingCommand::FreeMemory, VulkanTextureUploadDestinationCommand::FreeMemory },
        MissingCase{ MissingCommand::BindImageMemory, VulkanTextureUploadDestinationCommand::BindImageMemory },
        MissingCase{ MissingCommand::CreateImageView, VulkanTextureUploadDestinationCommand::CreateImageView },
        MissingCase{ MissingCommand::DestroyImageView, VulkanTextureUploadDestinationCommand::DestroyImageView },
    };

    for (const MissingCase& test_case : cases)
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        state.mMissing          = test_case.mMissing;

        ensureError(resolve(parents), VulkanTextureUploadDestinationResolutionCode::MissingRequiredCommand, test_case.mExpected);
        ensure("missing commands are found before capability queries or native object creation",
               state.mFormatQueryCalls == 0 && state.mImageFormatQueries.empty() && state.mMemoryPropertyCalls == 0 &&
                   state.mImageCreateInfos.empty() && state.mAllocationRecords.empty() && state.mViewCreateInfos.empty());
    }
}

template<>
template<>
void render_vulkan_texture_upload_destination_object::test<3>()
{
    FakeState       state;
    ScopedFakeState scope(state);
    auto            parents  = makeParents(state);
    state.mPrefersDedicated  = VK_FALSE;
    state.mRequiresDedicated = VK_FALSE;

    auto       generation  = takeGeneration(resolve(parents));
    const auto description = vulkanTextureUploadDestinationDescription();

    const std::vector<std::string> expected_instance_lookups{
        "vkGetPhysicalDeviceFormatProperties",
        "vkGetPhysicalDeviceImageFormatProperties",
        "vkGetPhysicalDeviceMemoryProperties",
        "vkGetDeviceProcAddr",
    };
    const std::vector<std::string> expected_device_lookups{
        "vkCreateImage",     "vkDestroyImage",     "vkGetImageMemoryRequirements2", "vkAllocateMemory", "vkFreeMemory", "vkBindImageMemory",
        "vkCreateImageView", "vkDestroyImageView",
    };
    ensure("the owner resolves the exact command set before its first capability query or native mutation",
           state.mAllCommandsResolvedBeforeCapability && state.mAllCommandsResolvedBeforeCreate &&
               state.mInstanceLookups == expected_instance_lookups && state.mDeviceLookups == expected_device_lookups);
    ensure("the format capability query is exact",
           state.mFormatQueryCalls == 1 && state.mFormatQueryPhysicalDevice == state.mPhysicalDevice &&
               state.mFormatQueryFormat == VK_FORMAT_R8G8B8A8_UNORM);
    ensure("the image-format capability query is exact", state.mImageFormatQueries.size() == 1);
    const auto& format_query = state.mImageFormatQueries.front();
    ensure("the image-format tuple has no inferred or mutable fields",
           format_query.mPhysicalDevice == state.mPhysicalDevice && format_query.mFormat == VK_FORMAT_R8G8B8A8_UNORM &&
               format_query.mType == VK_IMAGE_TYPE_2D && format_query.mTiling == VK_IMAGE_TILING_OPTIMAL &&
               format_query.mUsage == IMAGE_USAGE && format_query.mFlags == 0 && state.mMemoryPropertyCalls == 1);

    ensure("one image is created with the logical device and default allocator",
           state.mImageCreateInfos.size() == 1 && state.mImageCreateDevices == std::vector<VkDevice>{ state.mDevice } &&
               state.mImageCreateAllocatorNull == std::vector<bool>{ true });
    const VkImageCreateInfo& image_info = state.mImageCreateInfos.front();
    ensure("the image create structure is exact",
           image_info.sType == VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO && image_info.pNext == nullptr && image_info.flags == 0 &&
               image_info.imageType == VK_IMAGE_TYPE_2D && image_info.format == VK_FORMAT_R8G8B8A8_UNORM &&
               sameExtent(image_info.extent, { description.mResidentExtent.mWidth, description.mResidentExtent.mHeight, 1 }) &&
               image_info.mipLevels == description.mMipLevels && image_info.arrayLayers == description.mArrayLayers &&
               image_info.samples == VK_SAMPLE_COUNT_1_BIT && image_info.tiling == VK_IMAGE_TILING_OPTIMAL &&
               image_info.usage == IMAGE_USAGE && image_info.sharingMode == VK_SHARING_MODE_EXCLUSIVE &&
               image_info.queueFamilyIndexCount == 0 && image_info.pQueueFamilyIndices == nullptr &&
               image_info.initialLayout == VK_IMAGE_LAYOUT_UNDEFINED);

    ensure("memory requirements use the image-specific v2 query and dedicated-requirements chain",
           state.mMemoryRequirementRecords.size() == 1);
    const auto& requirements = state.mMemoryRequirementRecords.front();
    ensure("the memory-requirements structures are exact",
           requirements.mDevice == state.mDevice && requirements.mInfoStructureType == VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2 &&
               requirements.mInfoNextNull && requirements.mImage == state.mImageOutput &&
               requirements.mOutputStructureType == VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2 &&
               requirements.mDedicatedStructureType == VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS && requirements.mDedicatedNextNull);

    ensure("one exact formally dedicated allocation is made",
           state.mAllocationRecords.size() == 1 && state.mAllocationDevices == std::vector<VkDevice>{ state.mDevice } &&
               state.mAllocationAllocatorNull == std::vector<bool>{ true });
    const AllocationRecord& allocation = state.mAllocationRecords.front();
    ensure("the allocation chain was deep-copied and retains the exact dedicated image",
           allocation.mStructureType == VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO &&
               allocation.mAllocationSize == state.mMemoryRequirements.size && allocation.mMemoryTypeIndex == 0 &&
               allocation.mDedicatedStructureType == VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO && allocation.mDedicatedNextNull &&
               allocation.mDedicatedImage == state.mImageOutput && allocation.mDedicatedBuffer == VK_NULL_HANDLE);
    ensure("the image is bound once at offset zero",
           state.mBindDevices == std::vector<VkDevice>{ state.mDevice } &&
               state.mBoundImages == std::vector<VkImage>{ state.mImageOutput } &&
               state.mBoundMemories == std::vector<VkDeviceMemory>{ state.mMemoryOutput } &&
               state.mBindOffsets == std::vector<VkDeviceSize>{ 0 });

    ensure("one image view is created with the logical device and default allocator",
           state.mViewCreateInfos.size() == 1 && state.mViewCreateDevices == std::vector<VkDevice>{ state.mDevice } &&
               state.mViewCreateAllocatorNull == std::vector<bool>{ true });
    const VkImageViewCreateInfo& view_info = state.mViewCreateInfos.front();
    ensure(
        "the image-view create structure is exact",
        view_info.sType == VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO && view_info.pNext == nullptr && view_info.flags == 0 &&
            view_info.image == state.mImageOutput && view_info.viewType == VK_IMAGE_VIEW_TYPE_2D &&
            view_info.format == VK_FORMAT_R8G8B8A8_UNORM && view_info.components.r == VK_COMPONENT_SWIZZLE_IDENTITY &&
            view_info.components.g == VK_COMPONENT_SWIZZLE_IDENTITY && view_info.components.b == VK_COMPONENT_SWIZZLE_IDENTITY &&
            view_info.components.a == VK_COMPONENT_SWIZZLE_IDENTITY &&
            sameRange(view_info.subresourceRange, { VK_IMAGE_ASPECT_COLOR_BIT, 0, description.mMipLevels, 0, description.mArrayLayers }));

    const VkImageFormatProperties& retained_limits       = generation.imageFormatProperties();
    const VkMemoryRequirements&    retained_requirements = generation.memoryRequirements();
    ensure("the generation publishes the exact canonical image description and retained capability metadata",
           generation.resourceHandle() == description.mHandle && generation.expectedRevision() == description.mExpectedRevision &&
               sameExtent(generation.residentExtent(), { description.mResidentExtent.mWidth, description.mResidentExtent.mHeight, 1 }) &&
               generation.logicalExtent().mWidth == description.mLogicalExtent.mWidth &&
               generation.logicalExtent().mHeight == description.mLogicalExtent.mHeight &&
               generation.residentDiscard() == description.mResidentDiscard && generation.pixelFormat() == description.mFormat &&
               generation.initialState() == description.mInitialState && generation.flags() == 0 &&
               generation.imageType() == VK_IMAGE_TYPE_2D && generation.format() == VK_FORMAT_R8G8B8A8_UNORM &&
               generation.mipLevels() == description.mMipLevels && generation.arrayLayers() == description.mArrayLayers &&
               generation.samples() == VK_SAMPLE_COUNT_1_BIT && generation.tiling() == VK_IMAGE_TILING_OPTIMAL &&
               generation.usage() == IMAGE_USAGE && generation.sharingMode() == VK_SHARING_MODE_EXCLUSIVE &&
               generation.initialLayout() == VK_IMAGE_LAYOUT_UNDEFINED && generation.formatFeatures() == REQUIRED_FORMAT_FEATURES &&
               sameExtent(retained_limits.maxExtent, state.mImageFormatProperties.maxExtent) &&
               retained_limits.maxMipLevels == state.mImageFormatProperties.maxMipLevels &&
               retained_limits.maxArrayLayers == state.mImageFormatProperties.maxArrayLayers &&
               retained_limits.sampleCounts == state.mImageFormatProperties.sampleCounts &&
               retained_limits.maxResourceSize == state.mImageFormatProperties.maxResourceSize);
    ensure("the generation publishes exact allocation, view, and parent provenance",
           generation.image() == state.mImageOutput && generation.memory() == state.mMemoryOutput &&
               retained_requirements.size == state.mMemoryRequirements.size &&
               retained_requirements.alignment == state.mMemoryRequirements.alignment &&
               retained_requirements.memoryTypeBits == state.mMemoryRequirements.memoryTypeBits &&
               generation.allocationSize() == state.mMemoryRequirements.size &&
               generation.allocationAlignment() == state.mMemoryRequirements.alignment &&
               generation.compatibleMemoryTypeBits() == state.mMemoryRequirements.memoryTypeBits && generation.memoryTypeIndex() == 0 &&
               generation.memoryPropertyFlags() == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT && generation.isDeviceLocal() &&
               !generation.prefersDedicatedAllocation() && !generation.requiresDedicatedAllocation() &&
               generation.imageView() == state.mViewOutput && generation.imageViewType() == VK_IMAGE_VIEW_TYPE_2D &&
               sameRange(generation.viewRange(), { VK_IMAGE_ASPECT_COLOR_BIT, 0, description.mMipLevels, 0, description.mArrayLayers }) &&
               generation.createdFor(parents.mPhysical, parents.mLogical) && generation.matchesDescription(description));
}

template<>
template<>
void render_vulkan_texture_upload_destination_object::test<4>()
{
    constexpr std::array<VkFormatFeatureFlags, 6> required_bits{
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT, VK_FORMAT_FEATURE_TRANSFER_SRC_BIT, VK_FORMAT_FEATURE_TRANSFER_DST_BIT,
        VK_FORMAT_FEATURE_BLIT_SRC_BIT,      VK_FORMAT_FEATURE_BLIT_DST_BIT,     VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT,
    };
    for (VkFormatFeatureFlags missing_bit : required_bits)
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        state.mFormatProperties.optimalTilingFeatures &= ~missing_bit;
        state.mFormatProperties.linearTilingFeatures = REQUIRED_FORMAT_FEATURES;

        ensureError(resolve(parents),
                    VulkanTextureUploadDestinationResolutionCode::UnsupportedFormatFeatures,
                    VulkanTextureUploadDestinationCommand::GetPhysicalDeviceFormatProperties,
                    VK_SUCCESS,
                    VulkanTextureUploadDestinationCapability::FormatFeatures,
                    REQUIRED_FORMAT_FEATURES,
                    state.mFormatProperties.optimalTilingFeatures);
        ensure("every required optimal-tiling feature is enforced independently",
               state.mFormatQueryCalls == 1 && state.mImageFormatQueries.empty() && state.mImageCreateInfos.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents      = makeParents(state);
        state.mImageFormatResult     = VK_ERROR_FORMAT_NOT_SUPPORTED;
        state.mImageFormatProperties = {};

        ensureError(resolve(parents),
                    VulkanTextureUploadDestinationResolutionCode::ImageFormatQueryFailure,
                    VulkanTextureUploadDestinationCommand::GetPhysicalDeviceImageFormatProperties,
                    VK_ERROR_FORMAT_NOT_SUPPORTED);
        ensure("a failed image-format query does not inspect its output or create an image", state.mImageCreateInfos.empty());
    }

    for (std::uint32_t limit_case = 0; limit_case < 6; ++limit_case)
    {
        FakeState                                state;
        ScopedFakeState                          scope(state);
        auto                                     parents         = makeParents(state);
        VulkanTextureUploadDestinationCapability capability      = VulkanTextureUploadDestinationCapability::ExtentWidth;
        std::uint64_t                            required_value  = 0;
        std::uint64_t                            available_value = 0;
        switch (limit_case)
        {
            case 0:
                state.mImageFormatProperties.maxExtent.width = LLRenderContract::TEXTURE_UPLOAD_RESIDENT_WIDTH - 1;
                capability                                   = VulkanTextureUploadDestinationCapability::ExtentWidth;
                required_value                               = LLRenderContract::TEXTURE_UPLOAD_RESIDENT_WIDTH;
                available_value                              = state.mImageFormatProperties.maxExtent.width;
                break;
            case 1:
                state.mImageFormatProperties.maxExtent.height = LLRenderContract::TEXTURE_UPLOAD_RESIDENT_HEIGHT - 1;
                capability                                    = VulkanTextureUploadDestinationCapability::ExtentHeight;
                required_value                                = LLRenderContract::TEXTURE_UPLOAD_RESIDENT_HEIGHT;
                available_value                               = state.mImageFormatProperties.maxExtent.height;
                break;
            case 2:
                state.mImageFormatProperties.maxExtent.depth = 0;
                capability                                   = VulkanTextureUploadDestinationCapability::ExtentDepth;
                required_value                               = 1;
                break;
            case 3:
                state.mImageFormatProperties.maxMipLevels = LLRenderContract::TEXTURE_UPLOAD_MIP_LEVELS - 1;
                capability                                = VulkanTextureUploadDestinationCapability::MipLevels;
                required_value                            = LLRenderContract::TEXTURE_UPLOAD_MIP_LEVELS;
                available_value                           = state.mImageFormatProperties.maxMipLevels;
                break;
            case 4:
                state.mImageFormatProperties.maxArrayLayers = 0;
                capability                                  = VulkanTextureUploadDestinationCapability::ArrayLayers;
                required_value                              = 1;
                break;
            case 5:
                state.mImageFormatProperties.sampleCounts = VK_SAMPLE_COUNT_4_BIT;
                capability                                = VulkanTextureUploadDestinationCapability::SampleCountOne;
                required_value                            = VK_SAMPLE_COUNT_1_BIT;
                available_value                           = state.mImageFormatProperties.sampleCounts;
                break;
        }

        ensureError(resolve(parents),
                    VulkanTextureUploadDestinationResolutionCode::UnsupportedImageFormatLimits,
                    VulkanTextureUploadDestinationCommand::GetPhysicalDeviceImageFormatProperties,
                    VK_SUCCESS,
                    capability,
                    required_value,
                    available_value);
        ensure("each required image-format limit is enforced before image creation", state.mImageCreateInfos.empty());
    }

    for (VkDeviceSize max_resource_size : std::array<VkDeviceSize, 3>{ 0, 1, 1ULL << 40 })
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents                      = makeParents(state);
        state.mImageFormatProperties.maxResourceSize = max_resource_size;

        auto generation = takeGeneration(resolve(parents));
        ensure("maxResourceSize is evidence from the successful driver query, not a host payload-size gate",
               generation.imageFormatProperties().maxResourceSize == max_resource_size);
    }
    {
        FakeState                      state;
        ScopedFakeState                scope(state);
        auto                           parents       = makeParents(state);
        constexpr VkFormatFeatureFlags extra_feature = VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
        state.mFormatProperties.optimalTilingFeatures |= extra_feature;

        auto generation = takeGeneration(resolve(parents));
        ensure("successful format evidence retains supported features beyond the required mask",
               generation.formatFeatures() == (REQUIRED_FORMAT_FEATURES | extra_feature));
    }
}

template<>
template<>
void render_vulkan_texture_upload_destination_object::test<5>()
{
    for (std::uint32_t invalid_case = 0; invalid_case < 5; ++invalid_case)
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        switch (invalid_case)
        {
            case 0:
                state.mMemoryProperties.memoryTypeCount = 0;
                break;
            case 1:
                state.mMemoryProperties.memoryTypeCount = VK_MAX_MEMORY_TYPES + 1;
                break;
            case 2:
                state.mMemoryProperties.memoryHeapCount = 0;
                break;
            case 3:
                state.mMemoryProperties.memoryHeapCount = VK_MAX_MEMORY_HEAPS + 1;
                break;
            case 4:
                state.mMemoryProperties.memoryTypes[0].heapIndex = state.mMemoryProperties.memoryHeapCount;
                break;
        }

        ensureError(resolve(parents),
                    VulkanTextureUploadDestinationResolutionCode::InvalidPhysicalDeviceMemoryProperties,
                    VulkanTextureUploadDestinationCommand::GetPhysicalDeviceMemoryProperties);
        ensure("invalid physical memory tables are rejected before image creation", state.mImageCreateInfos.empty());
    }

    constexpr std::array invalid_requirements{
        VkMemoryRequirements{ 0, 16, 1 },
        VkMemoryRequirements{ 64, 0, 1 },
        VkMemoryRequirements{ 64, 3, 1 },
        VkMemoryRequirements{ 64, 16, 0 },
    };
    for (const VkMemoryRequirements& requirements : invalid_requirements)
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents   = makeParents(state);
        state.mMemoryRequirements = requirements;

        ensureError(resolve(parents),
                    VulkanTextureUploadDestinationResolutionCode::InvalidImageMemoryRequirements,
                    VulkanTextureUploadDestinationCommand::GetImageMemoryRequirements2);
        ensure("invalid image requirements roll back only the acquired image occurrence",
               state.mDestroyedImages == std::vector<VkImage>{ state.mImageOutput } && state.mFreedMemories.empty() &&
                   state.mDestroyedViews.empty() && state.mTeardownOrder == std::vector<std::string>{ "destroy-image" });
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        for (std::uint32_t index = 0; index < state.mMemoryProperties.memoryTypeCount; ++index)
        {
            state.mMemoryProperties.memoryTypes[index].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        }

        ensureError(resolve(parents), VulkanTextureUploadDestinationResolutionCode::NoCompatibleMemoryType);
        ensure("host-only memory cannot back the destination image",
               state.mDestroyedImages == std::vector<VkImage>{ state.mImageOutput } && state.mFreedMemories.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents                              = makeParents(state);
        state.mMemoryProperties.memoryHeapCount              = 1;
        state.mMemoryProperties.memoryTypeCount              = 1;
        state.mMemoryProperties.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        state.mMemoryProperties.memoryTypes[0].heapIndex     = 0;
        state.mMemoryProperties.memoryHeaps[0].size          = state.mMemoryRequirements.size - 1;
        state.mMemoryRequirements.memoryTypeBits             = 1;

        ensureError(resolve(parents), VulkanTextureUploadDestinationResolutionCode::NoCompatibleMemoryType);
        ensure("an undersized otherwise-compatible heap is rejected",
               state.mDestroyedImages == std::vector<VkImage>{ state.mImageOutput } && state.mFreedMemories.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents                 = makeParents(state);
        state.mMemoryProperties.memoryHeapCount = 3;
        state.mMemoryProperties.memoryHeaps[0]  = { 1ULL << 30, VK_MEMORY_HEAP_DEVICE_LOCAL_BIT };
        state.mMemoryProperties.memoryHeaps[1]  = {
            1ULL << 30,
            VK_MEMORY_HEAP_DEVICE_LOCAL_BIT | VK_MEMORY_HEAP_TILE_MEMORY_BIT_QCOM,
        };
        state.mMemoryProperties.memoryHeaps[2]  = { state.mMemoryRequirements.size - 1, VK_MEMORY_HEAP_DEVICE_LOCAL_BIT };
        state.mMemoryProperties.memoryTypeCount = 6;
        state.mMemoryProperties.memoryTypes[0]  = {
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD,
            0,
        };
        state.mMemoryProperties.memoryTypes[1] = {
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_PROTECTED_BIT,
            0,
        };
        state.mMemoryProperties.memoryTypes[2] = {
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT,
            0,
        };
        state.mMemoryProperties.memoryTypes[3] = { VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 1 };
        state.mMemoryProperties.memoryTypes[4] = { VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 2 };
        state.mMemoryProperties.memoryTypes[5] = {
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
            0,
        };
        state.mMemoryRequirements.memoryTypeBits = 0x3f;

        auto generation = takeGeneration(resolve(parents));
        ensure("selection skips every forbidden type and heap property and takes the lowest remaining index",
               generation.memoryTypeIndex() == 5 &&
                   generation.memoryPropertyFlags() == (VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT));
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents                     = makeParents(state);
        state.mMemoryRequirements                   = { 64, 16, 1 };
        state.mMemoryProperties.memoryHeaps[0].size = 64;
        state.mPrefersDedicated                     = VK_TRUE;
        state.mRequiresDedicated                    = VK_TRUE;

        auto generation = takeGeneration(resolve(parents));
        ensure("a tight valid requirement below the diagnostic texel count remains driver-authoritative",
               generation.allocationSize() == 64 && generation.prefersDedicatedAllocation() && generation.requiresDedicatedAllocation() &&
                   state.mAllocationRecords.front().mAllocationSize == 64 &&
                   state.mAllocationRecords.front().mDedicatedImage == state.mImageOutput);
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents  = makeParents(state);
        state.mPrefersDedicated  = VK_TRUE;
        state.mRequiresDedicated = VK_FALSE;

        auto generation = takeGeneration(resolve(parents));
        ensure("a preferred-only requirement is retained and still uses the unconditional dedicated allocation chain",
               generation.prefersDedicatedAllocation() && !generation.requiresDedicatedAllocation() &&
                   state.mAllocationRecords.front().mDedicatedStructureType == VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO &&
                   state.mAllocationRecords.front().mDedicatedImage == state.mImageOutput &&
                   state.mAllocationRecords.front().mDedicatedBuffer == VK_NULL_HANDLE);
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents                 = makeParents(state);
        state.mMemoryProperties.memoryTypeCount = VK_MAX_MEMORY_TYPES;
        for (std::uint32_t index = 0; index < VK_MAX_MEMORY_TYPES; ++index)
        {
            state.mMemoryProperties.memoryTypes[index].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
            state.mMemoryProperties.memoryTypes[index].heapIndex     = 0;
        }
        state.mMemoryProperties.memoryTypes[VK_MAX_MEMORY_TYPES - 1].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        state.mMemoryRequirements.memoryTypeBits                                   = std::uint32_t{ 1 } << (VK_MAX_MEMORY_TYPES - 1);

        auto generation = takeGeneration(resolve(parents));
        ensure("the highest legal memory-type bit is selected without an overflowing shift",
               generation.memoryTypeIndex() == VK_MAX_MEMORY_TYPES - 1);
    }
}

template<>
template<>
void render_vulkan_texture_upload_destination_object::test<6>()
{
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents  = makeParents(state);
        state.mCreateImageResult = VK_ERROR_OUT_OF_HOST_MEMORY;
        state.mImageOutput       = fakeHandle<VkImage>(0xdead);

        ensureError(resolve(parents),
                    VulkanTextureUploadDestinationResolutionCode::ImageCreationFailure,
                    VulkanTextureUploadDestinationCommand::CreateImage,
                    VK_ERROR_OUT_OF_HOST_MEMORY);
        ensure("a failed image output is poisoned and never inspected or destroyed",
               state.mDestroyedImages.empty() && state.mMemoryRequirementRecords.empty() && state.mAllocationRecords.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        state.mImageOutput      = VK_NULL_HANDLE;

        ensureError(resolve(parents),
                    VulkanTextureUploadDestinationResolutionCode::NullImageOnSuccess,
                    VulkanTextureUploadDestinationCommand::CreateImage);
        ensure("a null successful image output creates no ownership occurrence", state.mDestroyedImages.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        state.mAllocateResult   = VK_ERROR_OUT_OF_DEVICE_MEMORY;
        state.mMemoryOutput     = fakeHandle<VkDeviceMemory>(0xdead);

        ensureError(resolve(parents),
                    VulkanTextureUploadDestinationResolutionCode::MemoryAllocationFailure,
                    VulkanTextureUploadDestinationCommand::AllocateMemory,
                    VK_ERROR_OUT_OF_DEVICE_MEMORY);
        ensure("a failed allocation output is poisoned, never freed, and leaves only the image rollback obligation",
               state.mDestroyedImages == std::vector<VkImage>{ state.mImageOutput } && state.mFreedMemories.empty() &&
                   state.mBoundMemories.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        state.mMemoryOutput     = VK_NULL_HANDLE;

        ensureError(resolve(parents),
                    VulkanTextureUploadDestinationResolutionCode::NullMemoryOnSuccess,
                    VulkanTextureUploadDestinationCommand::AllocateMemory);
        ensure("a null successful allocation rolls back only the image",
               state.mDestroyedImages == std::vector<VkImage>{ state.mImageOutput } && state.mFreedMemories.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        state.mBindResult       = VK_ERROR_MEMORY_MAP_FAILED;

        ensureError(resolve(parents),
                    VulkanTextureUploadDestinationResolutionCode::ImageMemoryBindFailure,
                    VulkanTextureUploadDestinationCommand::BindImageMemory,
                    VK_ERROR_MEMORY_MAP_FAILED);
        ensure("bind failure destroys the image before freeing its allocation",
               state.mTeardownOrder == std::vector<std::string>{ "destroy-image", "free-memory" } && state.mDestroyedViews.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        state.mCreateViewResult = VK_ERROR_OUT_OF_HOST_MEMORY;
        state.mViewOutput       = fakeHandle<VkImageView>(0xdead);

        ensureError(resolve(parents),
                    VulkanTextureUploadDestinationResolutionCode::ImageViewCreationFailure,
                    VulkanTextureUploadDestinationCommand::CreateImageView,
                    VK_ERROR_OUT_OF_HOST_MEMORY);
        ensure("a failed view output is poisoned and never destroyed",
               state.mDestroyedViews.empty() && state.mTeardownOrder == std::vector<std::string>{ "destroy-image", "free-memory" });
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        state.mViewOutput       = VK_NULL_HANDLE;

        ensureError(resolve(parents),
                    VulkanTextureUploadDestinationResolutionCode::NullImageViewOnSuccess,
                    VulkanTextureUploadDestinationCommand::CreateImageView);
        ensure("a null successful view output rolls back image then memory without a view teardown",
               state.mDestroyedViews.empty() && state.mTeardownOrder == std::vector<std::string>{ "destroy-image", "free-memory" });
    }
}

template<>
template<>
void render_vulkan_texture_upload_destination_object::test<7>()
{
    {
        FakeState                state;
        ScopedFakeState          scope(state);
        auto                     parents     = makeParents(state);
        constexpr std::uintptr_t shared_bits = 0xd00d;
        state.mImageOutput                   = fakeHandle<VkImage>(shared_bits);
        state.mMemoryOutput                  = fakeHandle<VkDeviceMemory>(shared_bits);
        state.mViewOutput                    = fakeHandle<VkImageView>(shared_bits);
        state.mReenterCreateImage            = true;
        state.mReentryPhysical               = &parents.mPhysical;
        state.mReentryLogical                = &parents.mLogical;

        auto outer = takeGeneration(resolve(parents));
        ensure("image creation can re-enter owner resolution without corrupting either transaction",
               state.mReentryAttempted && state.mReentrySucceeded && state.mReenteredGeneration && state.mImageCreateInfos.size() == 2 &&
                   state.mAllocationRecords.size() == 2 && state.mViewCreateInfos.size() == 2);
        ensure("typed image, memory, and view ownership remains distinct when the driver reuses raw handle bits",
               handleBits(outer.image()) == shared_bits && handleBits(outer.memory()) == shared_bits &&
                   handleBits(outer.imageView()) == shared_bits && handleBits(state.mReenteredGeneration->image()) == shared_bits &&
                   handleBits(state.mReenteredGeneration->memory()) == shared_bits &&
                   handleBits(state.mReenteredGeneration->imageView()) == shared_bits);

        outer.reset();
        state.mReenteredGeneration->reset();
        ensure("each successful owner contributes one teardown occurrence regardless of duplicate handle values",
               state.mDestroyedViews.size() == 2 && state.mDestroyedImages.size() == 2 && state.mFreedMemories.size() == 2 &&
                   state.mTeardownOrder == std::vector<std::string>{ "destroy-view", "destroy-image", "free-memory", "destroy-view",
                                                                     "destroy-image", "free-memory" });
    }

    constexpr std::array teardown_points{
        TeardownPoint::DestroyImageView,
        TeardownPoint::DestroyImage,
        TeardownPoint::FreeMemory,
    };
    for (TeardownPoint point : teardown_points)
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents    = makeParents(state);
        auto            generation = takeGeneration(resolve(parents));
        state.mResetToReenter      = &generation;
        state.mReenterResetAt      = point;

        generation.reset();
        ensure("reset is inert before any teardown callback can re-enter it",
               state.mResetReentryAttempted && state.mDestroyedViews == std::vector<VkImageView>{ state.mViewOutput } &&
                   state.mDestroyedImages == std::vector<VkImage>{ state.mImageOutput } &&
                   state.mFreedMemories == std::vector<VkDeviceMemory>{ state.mMemoryOutput } &&
                   state.mTeardownOrder == std::vector<std::string>{ "destroy-view", "destroy-image", "free-memory" });
        generation.reset();
        ensure("a second reset adds no teardown occurrence", state.mTeardownOrder.size() == 3);
    }
}

template<>
template<>
void render_vulkan_texture_upload_destination_object::test<8>()
{
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents        = makeParents(state);
        auto            description    = vulkanTextureUploadDestinationDescription();
        const auto      original       = description;
        state.mDescriptionToMutate     = &description;
        state.mMutateDescriptionAt     = CallbackPoint::CommandLookup;
        state.mInvalidateOwnerLookupAt = 1;

        auto generation = takeGeneration(resolve(parents, description));
        ensure("resolution copies the caller description before its first command resolver callback",
               description != original && generation.matchesDescription(original) && !generation.matchesDescription(description));
    }

    constexpr std::array callback_points{
        CallbackPoint::FormatProperties, CallbackPoint::ImageFormatProperties, CallbackPoint::MemoryProperties,
        CallbackPoint::CreateImage,      CallbackPoint::MemoryRequirements,    CallbackPoint::AllocateMemory,
        CallbackPoint::BindImageMemory,  CallbackPoint::CreateImageView,
    };
    for (CallbackPoint point : callback_points)
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents     = makeParents(state);
        auto            description = vulkanTextureUploadDestinationDescription();
        const auto      original    = description;
        state.mDescriptionToMutate  = &description;
        state.mMutateDescriptionAt  = point;

        auto generation = takeGeneration(resolve(parents, description));
        ensure("resolution copies the caller description before every native callback",
               description != original && generation.matchesDescription(original) && !generation.matchesDescription(description) &&
                   generation.expectedRevision() == original.mExpectedRevision);
    }

    for (std::size_t lookup = 1; lookup <= 12; ++lookup)
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents        = makeParents(state);
        state.mInvalidateAt            = CallbackPoint::CommandLookup;
        state.mInvalidateOwnerLookupAt = lookup;
        state.mLogicalToInvalidate     = &parents.mLogical;

        ensureError(resolve(parents), VulkanTextureUploadDestinationResolutionCode::InvalidLogicalDeviceGeneration);
        ensure("each command resolver callback is followed by parent revalidation",
               state.mOwnerLookupCalls == lookup && state.mFormatQueryCalls == 0 && state.mImageCreateInfos.empty() &&
                   state.mDestroyedImages.empty() && state.mFreedMemories.empty() && state.mDestroyedViews.empty());
    }

    for (CallbackPoint point : callback_points)
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents    = makeParents(state);
        state.mInvalidateAt        = point;
        state.mLogicalToInvalidate = &parents.mLogical;

        ensureError(resolve(parents), VulkanTextureUploadDestinationResolutionCode::InvalidLogicalDeviceGeneration);
        ensure("a stale logical parent never publishes an image owner",
               !state.mMovedLogical || !parents.mLogical.createdFor(parents.mPhysical));

        std::vector<std::string> expected_order;
        if (point == CallbackPoint::CreateImage || point == CallbackPoint::MemoryRequirements)
        {
            expected_order = { "destroy-image" };
        }
        else if (point == CallbackPoint::AllocateMemory || point == CallbackPoint::BindImageMemory)
        {
            expected_order = { "destroy-image", "free-memory" };
        }
        else if (point == CallbackPoint::CreateImageView)
        {
            expected_order = { "destroy-view", "destroy-image", "free-memory" };
        }
        ensure("stale-parent rollback releases exactly the ownership occurrences acquired before the callback",
               state.mTeardownOrder == expected_order);
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents     = makeParents(state);
        state.mInvalidateAt         = CallbackPoint::MemoryProperties;
        state.mInvalidatePhysical   = true;
        state.mPhysicalToInvalidate = &parents.mPhysical;

        ensureError(resolve(parents), VulkanTextureUploadDestinationResolutionCode::InvalidPhysicalDeviceGeneration);
        ensure("physical-parent invalidation owns no object before image creation", state.mTeardownOrder.empty());
    }

    constexpr std::array failure_points{
        CallbackPoint::ImageFormatProperties, CallbackPoint::CreateImage,     CallbackPoint::AllocateMemory,
        CallbackPoint::BindImageMemory,       CallbackPoint::CreateImageView,
    };
    for (CallbackPoint point : failure_points)
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents    = makeParents(state);
        state.mInvalidateAt        = point;
        state.mLogicalToInvalidate = &parents.mLogical;
        if (point == CallbackPoint::ImageFormatProperties)
        {
            state.mImageFormatResult     = VK_ERROR_FORMAT_NOT_SUPPORTED;
            state.mImageFormatProperties = {};
        }
        else if (point == CallbackPoint::CreateImage)
        {
            state.mCreateImageResult = VK_ERROR_OUT_OF_HOST_MEMORY;
            state.mImageOutput       = fakeHandle<VkImage>(0xdead);
        }
        else if (point == CallbackPoint::AllocateMemory)
        {
            state.mAllocateResult = VK_ERROR_OUT_OF_DEVICE_MEMORY;
            state.mMemoryOutput   = fakeHandle<VkDeviceMemory>(0xdead);
        }
        else if (point == CallbackPoint::BindImageMemory)
        {
            state.mBindResult = VK_ERROR_MEMORY_MAP_FAILED;
        }
        else
        {
            state.mCreateViewResult = VK_ERROR_OUT_OF_HOST_MEMORY;
            state.mViewOutput       = fakeHandle<VkImageView>(0xdead);
        }

        ensureError(resolve(parents), VulkanTextureUploadDestinationResolutionCode::InvalidLogicalDeviceGeneration);
        if (point == CallbackPoint::ImageFormatProperties || point == CallbackPoint::CreateImage)
        {
            ensure("stale-parent precedence never adopts a failed capability or image output", state.mTeardownOrder.empty());
        }
        else if (point == CallbackPoint::AllocateMemory)
        {
            ensure("a failed allocation output remains unowned under simultaneous parent invalidation",
                   state.mTeardownOrder == std::vector<std::string>{ "destroy-image" } && state.mFreedMemories.empty());
        }
        else
        {
            ensure("simultaneous bind or view failure rolls back only previously acquired occurrences",
                   state.mDestroyedViews.empty() && state.mTeardownOrder == std::vector<std::string>{ "destroy-image", "free-memory" });
        }
    }
}

template<>
template<>
void render_vulkan_texture_upload_destination_object::test<9>()
{
    FakeState       state;
    ScopedFakeState scope(state);
    auto            parents     = makeParents(state);
    auto            generation  = takeGeneration(resolve(parents));
    auto            same_native = makeParents(state);
    const auto      description = vulkanTextureUploadDestinationDescription();

    ensure("same-looking parent generations do not pass the owner's address and provenance check",
           generation.createdFor(parents.mPhysical, parents.mLogical) &&
               !generation.createdFor(same_native.mPhysical, same_native.mLogical));

    auto                           moved              = std::move(generation);
    const VkImageFormatProperties& inert_limits       = generation.imageFormatProperties();
    const VkMemoryRequirements&    inert_requirements = generation.memoryRequirements();
    ensure("move construction makes every source ownership and description accessor inert",
           !generation.resourceHandle() && generation.expectedRevision() == 0 && sameExtent(generation.residentExtent(), { 0, 0, 0 }) &&
               generation.logicalExtent().mWidth == 0 && generation.logicalExtent().mHeight == 0 && generation.residentDiscard() == 0 &&
               generation.pixelFormat() == LLRenderContract::PixelFormat::RGBA8Unorm &&
               generation.initialState() == LLRenderContract::ImageState::Undefined && generation.flags() == 0 &&
               generation.imageType() == VK_IMAGE_TYPE_MAX_ENUM && generation.format() == VK_FORMAT_UNDEFINED &&
               generation.mipLevels() == 0 && generation.arrayLayers() == 0 && generation.samples() == VK_SAMPLE_COUNT_FLAG_BITS_MAX_ENUM &&
               generation.tiling() == VK_IMAGE_TILING_MAX_ENUM && generation.usage() == 0 &&
               generation.sharingMode() == VK_SHARING_MODE_MAX_ENUM && generation.initialLayout() == VK_IMAGE_LAYOUT_MAX_ENUM &&
               generation.formatFeatures() == 0 && sameExtent(inert_limits.maxExtent, { 0, 0, 0 }) && inert_limits.maxMipLevels == 0 &&
               inert_limits.maxArrayLayers == 0 && inert_limits.sampleCounts == 0 && inert_limits.maxResourceSize == 0 &&
               generation.image() == VK_NULL_HANDLE && generation.memory() == VK_NULL_HANDLE && inert_requirements.size == 0 &&
               inert_requirements.alignment == 0 && inert_requirements.memoryTypeBits == 0 && generation.allocationSize() == 0 &&
               generation.allocationAlignment() == 0 && generation.compatibleMemoryTypeBits() == 0 && generation.memoryTypeIndex() == 0 &&
               generation.memoryPropertyFlags() == 0 && !generation.isDeviceLocal() && !generation.prefersDedicatedAllocation() &&
               !generation.requiresDedicatedAllocation() && generation.imageView() == VK_NULL_HANDLE &&
               generation.imageViewType() == VK_IMAGE_VIEW_TYPE_MAX_ENUM && sameRange(generation.viewRange(), {}) &&
               !generation.createdFor(parents.mPhysical, parents.mLogical) && !generation.matchesDescription(description));
    ensure("move construction transfers the complete image owner",
           moved.resourceHandle() == description.mHandle && moved.expectedRevision() == description.mExpectedRevision &&
               moved.image() == state.mImageOutput && moved.memory() == state.mMemoryOutput && moved.imageView() == state.mViewOutput &&
               moved.imageViewType() == VK_IMAGE_VIEW_TYPE_2D && moved.createdFor(parents.mPhysical, parents.mLogical) &&
               moved.matchesDescription(description));

    state.mTeardownOrder.clear();
    moved.reset();
    ensure("reset destroys view, image, and allocation in dependency order",
           state.mTeardownOrder == std::vector<std::string>{ "destroy-view", "destroy-image", "free-memory" } &&
               state.mDestroyViewDevices == std::vector<VkDevice>{ state.mDevice } &&
               state.mDestroyImageDevices == std::vector<VkDevice>{ state.mDevice } &&
               state.mFreeMemoryDevices == std::vector<VkDevice>{ state.mDevice } &&
               state.mDestroyViewAllocatorNull == std::vector<bool>{ true } &&
               state.mDestroyImageAllocatorNull == std::vector<bool>{ true } &&
               state.mFreeMemoryAllocatorNull == std::vector<bool>{ true });
    const VkMemoryRequirements& reset_requirements = moved.memoryRequirements();
    ensure("reset makes the moved owner fully inert",
           !moved.resourceHandle() && moved.image() == VK_NULL_HANDLE && moved.memory() == VK_NULL_HANDLE &&
               moved.imageView() == VK_NULL_HANDLE && moved.imageViewType() == VK_IMAGE_VIEW_TYPE_MAX_ENUM &&
               reset_requirements.size == 0 && reset_requirements.alignment == 0 && reset_requirements.memoryTypeBits == 0 &&
               sameRange(moved.viewRange(), {}) && !moved.createdFor(parents.mPhysical, parents.mLogical) &&
               !moved.matchesDescription(description));
    moved.reset();
    ensure("reset is idempotent", state.mTeardownOrder.size() == 3);

    state.mTeardownOrder.clear();
    state.mDestroyedViews.clear();
    state.mDestroyedImages.clear();
    state.mFreedMemories.clear();
    {
        auto automatic = takeGeneration(resolve(parents));
        ensure("the automatic owner is live until its lexical lifetime ends", automatic.createdFor(parents.mPhysical, parents.mLogical));
    }
    ensure("destruction releases each remaining native ownership occurrence exactly once",
           state.mDestroyedViews == std::vector<VkImageView>{ state.mViewOutput } &&
               state.mDestroyedImages == std::vector<VkImage>{ state.mImageOutput } &&
               state.mFreedMemories == std::vector<VkDeviceMemory>{ state.mMemoryOutput } &&
               state.mTeardownOrder == std::vector<std::string>{ "destroy-view", "destroy-image", "free-memory" });
}

} // namespace tut
