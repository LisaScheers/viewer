/**
 * @file llrendervulkantextureuploadtransfer_test.cpp
 * @brief Focused contract tests for the Vulkan texture upload transfer.
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

#include "llrendervulkantextureuploadtransfer.h"
#include "lltextureuploaddiagnostic.h"
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

namespace LLRenderVulkan
{

struct VulkanTextureUploadSourceGenerationTestAccess
{
    static VulkanTextureUploadSourceGeneration create(const VulkanPhysicalDeviceGeneration&       physical,
                                                      const VulkanLogicalDeviceGeneration&        logical,
                                                      const VulkanTextureUploadSourceDescription& description,
                                                      VkBuffer                                    buffer,
                                                      VkDeviceMemory                              memory,
                                                      PFN_vkDestroyBuffer                         destroy_buffer,
                                                      PFN_vkFreeMemory                            free_memory) noexcept
    {
        return VulkanTextureUploadSourceGeneration(physical,
                                                   logical,
                                                   description,
                                                   buffer,
                                                   memory,
                                                   256,
                                                   0,
                                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                                   destroy_buffer,
                                                   free_memory);
    }
};

struct VulkanTextureUploadDestinationGenerationTestAccess
{
    static VulkanTextureUploadDestinationGeneration create(const VulkanPhysicalDeviceGeneration&            physical,
                                                           const VulkanLogicalDeviceGeneration&             logical,
                                                           const VulkanTextureUploadDestinationDescription& description,
                                                           VkImage                                          image,
                                                           VkDeviceMemory                                   memory,
                                                           VkImageView                                      image_view,
                                                           PFN_vkDestroyImageView                           destroy_image_view,
                                                           PFN_vkDestroyImage                               destroy_image,
                                                           PFN_vkFreeMemory                                 free_memory) noexcept
    {
        constexpr VkFormatFeatureFlags required_format_features =
            VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT |
            VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        VkImageFormatProperties image_format_properties{};
        image_format_properties.maxExtent       = { 64, 64, 1 };
        image_format_properties.maxMipLevels    = 8;
        image_format_properties.maxArrayLayers  = 4;
        image_format_properties.sampleCounts    = VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_4_BIT;
        image_format_properties.maxResourceSize = 1ULL << 20;
        const VkMemoryRequirements memory_requirements{ 4096, 256, 1 };
        return VulkanTextureUploadDestinationGeneration(physical,
                                                        logical,
                                                        description,
                                                        required_format_features,
                                                        image_format_properties,
                                                        image,
                                                        memory,
                                                        memory_requirements,
                                                        0,
                                                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                                        true,
                                                        true,
                                                        image_view,
                                                        destroy_image_view,
                                                        destroy_image,
                                                        free_memory);
    }

    static bool markResident(VulkanTextureUploadDestinationGeneration& generation, std::uint64_t revision, std::uint64_t identity) noexcept
    {
        return generation.markResident(revision, identity, LLRenderContract::ImageState::ShaderRead);
    }

    static void forcePartialPublication(VulkanTextureUploadDestinationGeneration& generation, std::uint64_t revision) noexcept
    {
        generation.mResidentRevision = revision;
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

struct CommandPoolRecord
{
    VkDevice                 mDevice        = VK_NULL_HANDLE;
    VkStructureType          mStructureType = VK_STRUCTURE_TYPE_MAX_ENUM;
    const void*              mNext          = nullptr;
    VkCommandPoolCreateFlags mFlags         = 0;
    std::uint32_t            mQueueFamily   = VK_QUEUE_FAMILY_IGNORED;
    bool                     mAllocatorNull = false;
    VkCommandPool            mIncoming      = VK_NULL_HANDLE;
};

struct CommandBufferRecord
{
    VkDevice             mDevice        = VK_NULL_HANDLE;
    VkStructureType      mStructureType = VK_STRUCTURE_TYPE_MAX_ENUM;
    const void*          mNext          = nullptr;
    VkCommandPool        mPool          = VK_NULL_HANDLE;
    VkCommandBufferLevel mLevel         = VK_COMMAND_BUFFER_LEVEL_MAX_ENUM;
    std::uint32_t        mCount         = 0;
    VkCommandBuffer      mIncoming      = VK_NULL_HANDLE;
};

struct FenceRecord
{
    VkDevice           mDevice        = VK_NULL_HANDLE;
    VkStructureType    mStructureType = VK_STRUCTURE_TYPE_MAX_ENUM;
    const void*        mNext          = nullptr;
    VkFenceCreateFlags mFlags         = 0;
    bool               mAllocatorNull = false;
    VkFence            mIncoming      = VK_NULL_HANDLE;
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
    VkCommandBuffer                    mCommandBuffer      = VK_NULL_HANDLE;
    VkPipelineStageFlags               mSourceStage        = 0;
    VkPipelineStageFlags               mDestinationStage   = 0;
    VkDependencyFlags                  mDependencyFlags    = 0;
    std::uint32_t                      mMemoryBarrierCount = 0;
    std::vector<VkBufferMemoryBarrier> mBufferBarriers;
    std::vector<VkImageMemoryBarrier>  mImageBarriers;
};

struct CopyRecord
{
    VkCommandBuffer                mCommandBuffer = VK_NULL_HANDLE;
    VkBuffer                       mSource        = VK_NULL_HANDLE;
    VkImage                        mDestination   = VK_NULL_HANDLE;
    VkImageLayout                  mLayout        = VK_IMAGE_LAYOUT_MAX_ENUM;
    std::vector<VkBufferImageCopy> mRegions;
};

struct BlitRecord
{
    VkCommandBuffer          mCommandBuffer     = VK_NULL_HANDLE;
    VkImage                  mSource            = VK_NULL_HANDLE;
    VkImageLayout            mSourceLayout      = VK_IMAGE_LAYOUT_MAX_ENUM;
    VkImage                  mDestination       = VK_NULL_HANDLE;
    VkImageLayout            mDestinationLayout = VK_IMAGE_LAYOUT_MAX_ENUM;
    std::vector<VkImageBlit> mRegions;
    VkFilter                 mFilter = VK_FILTER_MAX_ENUM;
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

enum class TeardownPoint : std::uint8_t
{
    None,
    Fence,
    Pool,
    Both
};

struct FakeState
{
    VkInstance       mInstance          = fakeHandle<VkInstance>(0x1000);
    VkSurfaceKHR     mSurface           = fakeHandle<VkSurfaceKHR>(0x2000);
    VkPhysicalDevice mPhysicalDevice    = fakeHandle<VkPhysicalDevice>(0x3000);
    VkDevice         mDevice            = fakeHandle<VkDevice>(0x4000);
    VkQueue          mQueue             = fakeHandle<VkQueue>(0x5000);
    VkBuffer         mSourceBuffer      = fakeHandle<VkBuffer>(0x6000);
    VkImage          mDestinationImage  = fakeHandle<VkImage>(0x6100);
    VkDeviceMemory   mSourceMemory      = fakeHandle<VkDeviceMemory>(0x7000);
    VkDeviceMemory   mDestinationMemory = fakeHandle<VkDeviceMemory>(0x7100);
    VkImageView      mDestinationView   = fakeHandle<VkImageView>(0x7200);
    VkCommandPool    mCommandPool       = fakeHandle<VkCommandPool>(0x8000);
    VkCommandBuffer  mCommandBuffer     = fakeHandle<VkCommandBuffer>(0x8100);
    VkFence          mFence             = fakeHandle<VkFence>(0x8200);
    std::uint32_t    mQueueFamily       = 2;
    std::uint32_t    mQueueCount        = 2;

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
    std::vector<BlitRecord>          mBlitRecords;
    std::vector<VkCommandBuffer>     mEndedCommandBuffers;
    std::vector<SubmitRecord>        mSubmitRecords;
    std::vector<WaitRecord>          mWaitRecords;
    std::vector<VkFence>             mDestroyedFences;
    std::vector<VkCommandPool>       mDestroyedCommandPools;
    std::vector<std::string>         mTransferTeardownOrder;
    std::vector<VkBuffer>            mDestroyedBuffers;
    std::vector<VkImageView>         mDestroyedImageViews;
    std::vector<VkImage>             mDestroyedImages;
    std::vector<VkDeviceMemory>      mFreedMemories;
    std::size_t                      mDestroyDeviceCalls               = 0;
    bool                             mTransferDestroyedWithWrongDevice = false;

    VulkanLogicalDeviceGeneration*                          mLogicalToMove     = nullptr;
    VulkanTextureUploadSourceGeneration*                    mSourceToMove      = nullptr;
    VulkanTextureUploadDestinationGeneration*               mDestinationToMove = nullptr;
    std::optional<VulkanLogicalDeviceGeneration>            mMovedLogical;
    std::optional<VulkanTextureUploadSourceGeneration>      mMovedSource;
    std::optional<VulkanTextureUploadDestinationGeneration> mMovedDestination;
    VulkanTextureUploadSourceDescription*                   mSourceDescriptionToMutate      = nullptr;
    VulkanTextureUploadDestinationDescription*              mDestinationDescriptionToMutate = nullptr;

    VulkanTextureUploadTransferGeneration* mTransferForOperationReentry = nullptr;
    bool                                   mExerciseOperationReentry    = false;
    std::size_t                            mOperationReentryAttempts    = 0;
    bool                                   mOperationResetRefused       = true;
    bool                                   mOperationMoveSafe           = true;
    bool                                   mRecursiveOperationsRefused  = true;

    VulkanTextureUploadTransferGeneration* mResetToReenter            = nullptr;
    TeardownPoint                          mReenterAt                 = TeardownPoint::None;
    std::size_t                            mResetReentryAttempts      = 0;
    bool                                   mResetReentryReturnedTrue  = true;
    bool                                   mResetReentryObservedInert = true;

    const VulkanPhysicalDeviceGeneration*           mPhysicalForAccess    = nullptr;
    const VulkanLogicalDeviceGeneration*            mLogicalForAccess     = nullptr;
    const VulkanTextureUploadSourceGeneration*      mSourceForAccess      = nullptr;
    const VulkanTextureUploadDestinationGeneration* mDestinationForAccess = nullptr;
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
    gFakeState->mInvalidateAt.clear();
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

VulkanTextureUploadSourceDescription      sourceDescription();
VulkanTextureUploadDestinationDescription destinationDescription();

bool transferAccessorsInert(const VulkanTextureUploadTransferGeneration& transfer) noexcept
{
    bool inert =
        !transfer.resourceHandle() && transfer.expectedRevision() == 0 && transfer.contentIdentity() == 0 &&
        transfer.sourceBuffer() == VK_NULL_HANDLE && transfer.destinationImage() == VK_NULL_HANDLE && transfer.queue() == VK_NULL_HANDLE &&
        transfer.queueFamilyIndex() == VK_QUEUE_FAMILY_IGNORED && transfer.queueIndex() == 0 && transfer.commandPool() == VK_NULL_HANDLE &&
        transfer.commandBuffer() == VK_NULL_HANDLE && transfer.fence() == VK_NULL_HANDLE && transfer.submissionAttemptCount() == 0 &&
        transfer.completionWaitCount() == 0 && transfer.disposition() == VulkanTextureUploadTransferDisposition::ResetRequired &&
        !transfer.matchesSourceDescription(sourceDescription()) && !transfer.matchesDestinationDescription(destinationDescription());
    if (gFakeState && gFakeState->mPhysicalForAccess && gFakeState->mLogicalForAccess && gFakeState->mSourceForAccess &&
        gFakeState->mDestinationForAccess)
    {
        inert &= !transfer.createdFor(*gFakeState->mPhysicalForAccess, *gFakeState->mLogicalForAccess) &&
                 !transfer.retainsTextureUploadSourceGeneration(*gFakeState->mSourceForAccess) &&
                 !transfer.retainsTextureUploadDestinationGeneration(*gFakeState->mDestinationForAccess) &&
                 !transfer.retainsTextureUploadResources(*gFakeState->mSourceForAccess, *gFakeState->mDestinationForAccess);
    }
    return inert;
}

void attemptOperationReentry() noexcept
{
    if (!gFakeState || !gFakeState->mExerciseOperationReentry || !gFakeState->mTransferForOperationReentry)
    {
        return;
    }
    auto& transfer = *gFakeState->mTransferForOperationReentry;
    ++gFakeState->mOperationReentryAttempts;

    const auto            handle       = transfer.resourceHandle();
    const std::uint64_t   revision     = transfer.expectedRevision();
    const std::uint64_t   identity     = transfer.contentIdentity();
    const VkBuffer        source       = transfer.sourceBuffer();
    const VkImage         destination  = transfer.destinationImage();
    const VkQueue         queue        = transfer.queue();
    const std::uint32_t   queue_family = transfer.queueFamilyIndex();
    const std::uint32_t   queue_index  = transfer.queueIndex();
    const VkCommandPool   pool         = transfer.commandPool();
    const VkCommandBuffer command      = transfer.commandBuffer();
    const VkFence         fence        = transfer.fence();
    const std::uint32_t   submissions  = transfer.submissionAttemptCount();
    const std::uint32_t   waits        = transfer.completionWaitCount();
    const auto            disposition  = transfer.disposition();

    gFakeState->mOperationResetRefused &= !transfer.reset();
    VulkanTextureUploadTransferGeneration attempted_move(std::move(transfer));
    gFakeState->mOperationMoveSafe &=
        transferAccessorsInert(attempted_move) && transfer.resourceHandle() == handle && transfer.expectedRevision() == revision &&
        transfer.contentIdentity() == identity && transfer.sourceBuffer() == source && transfer.destinationImage() == destination &&
        transfer.queue() == queue && transfer.queueFamilyIndex() == queue_family && transfer.queueIndex() == queue_index &&
        transfer.commandPool() == pool && transfer.commandBuffer() == command && transfer.fence() == fence &&
        transfer.submissionAttemptCount() == submissions && transfer.completionWaitCount() == waits &&
        transfer.disposition() == disposition;

    const std::size_t native_call_count = gFakeState->mCalls.size();
    const auto        nested_execute    = transfer.execute(1);
    const auto        nested_retry      = transfer.retryCompletion(1);
    const auto*       execute_error     = std::get_if<VulkanTextureUploadTransferOperationError>(&nested_execute);
    const auto*       retry_error       = std::get_if<VulkanTextureUploadTransferOperationError>(&nested_retry);
    gFakeState->mRecursiveOperationsRefused &=
        execute_error && retry_error && execute_error->mCode == VulkanTextureUploadTransferOperationCode::InvalidDisposition &&
        retry_error->mCode == VulkanTextureUploadTransferOperationCode::InvalidDisposition && execute_error->mDisposition == disposition &&
        retry_error->mDisposition == disposition && gFakeState->mCalls.size() == native_call_count;
}

void attemptResetReentry(TeardownPoint point) noexcept
{
    if (!gFakeState || !gFakeState->mResetToReenter || (gFakeState->mReenterAt != point && gFakeState->mReenterAt != TeardownPoint::Both))
    {
        return;
    }
    ++gFakeState->mResetReentryAttempts;
    gFakeState->mResetReentryObservedInert &= transferAccessorsInert(*gFakeState->mResetToReenter);
    gFakeState->mResetReentryReturnedTrue &= gFakeState->mResetToReenter->reset();
    gFakeState->mResetReentryObservedInert &= transferAccessorsInert(*gFakeState->mResetToReenter);
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
        std::strncpy(properties->deviceName, "texture-upload-transfer-fake", VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 1);
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
        properties[index].queueCount = index == gFakeState->mQueueFamily ? gFakeState->mQueueCount : 1;
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
    properties[1] = {};
    std::strncpy(properties[0].extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_MAX_EXTENSION_NAME_SIZE - 1);
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

VKAPI_ATTR void VKAPI_CALL fakeDestroyBuffer(VkDevice, VkBuffer buffer, const VkAllocationCallbacks*) noexcept
{
    if (gFakeState)
    {
        gFakeState->mDestroyedBuffers.push_back(buffer);
    }
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyImageView(VkDevice, VkImageView view, const VkAllocationCallbacks*) noexcept
{
    if (gFakeState)
    {
        gFakeState->mDestroyedImageViews.push_back(view);
    }
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyImage(VkDevice, VkImage image, const VkAllocationCallbacks*) noexcept
{
    if (gFakeState)
    {
        gFakeState->mDestroyedImages.push_back(image);
    }
}

VKAPI_ATTR void VKAPI_CALL fakeFreeMemory(VkDevice, VkDeviceMemory memory, const VkAllocationCallbacks*) noexcept
{
    if (gFakeState)
    {
        gFakeState->mFreedMemories.push_back(memory);
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateCommandPool(VkDevice                       device,
                                                     const VkCommandPoolCreateInfo* create_info,
                                                     const VkAllocationCallbacks*   allocator,
                                                     VkCommandPool*                 pool) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || !create_info || !pool)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    const std::vector<std::string> expected{ "vkCreateCommandPool",  "vkDestroyCommandPool",   "vkAllocateCommandBuffers",
                                             "vkCreateFence",        "vkDestroyFence",         "vkBeginCommandBuffer",
                                             "vkCmdPipelineBarrier", "vkCmdCopyBufferToImage", "vkCmdBlitImage",
                                             "vkEndCommandBuffer",   "vkQueueSubmit",          "vkWaitForFences" };
    gFakeState->mAllCommandsResolvedBeforeMutation = gFakeState->mDeviceLookups == expected;
    gFakeState->mCommandPoolRecords.push_back(
        { device, create_info->sType, create_info->pNext, create_info->flags, create_info->queueFamilyIndex, allocator == nullptr, *pool });
    gFakeState->mCalls.emplace_back("create-pool");
    *pool = gFakeState->mCreateCommandPoolOutput;
    if (gFakeState->mSourceDescriptionToMutate)
    {
        ++gFakeState->mSourceDescriptionToMutate->mExpectedRevision;
        gFakeState->mSourceDescriptionToMutate->mBytes.fill(0xee);
    }
    if (gFakeState->mDestinationDescriptionToMutate)
    {
        ++gFakeState->mDestinationDescriptionToMutate->mExpectedRevision;
    }
    invalidateAt("create-pool");
    return gFakeState->mCreateCommandPoolResult;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyCommandPool(VkDevice device, VkCommandPool pool, const VkAllocationCallbacks*) noexcept
{
    if (gFakeState)
    {
        gFakeState->mTransferDestroyedWithWrongDevice |= device != gFakeState->mDevice;
        gFakeState->mDestroyedCommandPools.push_back(pool);
        gFakeState->mTransferTeardownOrder.emplace_back("destroy-pool");
        attemptResetReentry(TeardownPoint::Pool);
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
                                                  allocate_info->level, allocate_info->commandBufferCount, *command_buffer });
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
    gFakeState->mFenceRecords.push_back(
        { device, create_info->sType, create_info->pNext, create_info->flags, allocator == nullptr, *fence });
    gFakeState->mCalls.emplace_back("create-fence");
    *fence = gFakeState->mCreateFenceOutput;
    invalidateAt("create-fence");
    return gFakeState->mCreateFenceResult;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyFence(VkDevice device, VkFence fence, const VkAllocationCallbacks*) noexcept
{
    if (gFakeState)
    {
        gFakeState->mTransferDestroyedWithWrongDevice |= device != gFakeState->mDevice;
        gFakeState->mDestroyedFences.push_back(fence);
        gFakeState->mTransferTeardownOrder.emplace_back("destroy-fence");
        attemptResetReentry(TeardownPoint::Fence);
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
    attemptOperationReentry();
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
                                                  const VkImageMemoryBarrier*  image_barriers) noexcept
{
    if (!gFakeState || (buffer_barrier_count != 0 && !buffer_barriers) || (image_barrier_count != 0 && !image_barriers))
    {
        return;
    }
    BarrierRecord record;
    record.mCommandBuffer      = command_buffer;
    record.mSourceStage        = source_stage;
    record.mDestinationStage   = destination_stage;
    record.mDependencyFlags    = dependency_flags;
    record.mMemoryBarrierCount = memory_barrier_count;
    if (buffer_barrier_count != 0)
    {
        record.mBufferBarriers.assign(buffer_barriers, buffer_barriers + buffer_barrier_count);
    }
    if (image_barrier_count != 0)
    {
        record.mImageBarriers.assign(image_barriers, image_barriers + image_barrier_count);
    }
    constexpr std::array names{ "source-barrier", "all-dst-barrier", "mip0-barrier", "mip1-barrier", "sampled-barrier" };
    const std::size_t    index = gFakeState->mBarrierRecords.size();
    gFakeState->mBarrierRecords.push_back(std::move(record));
    const std::string_view name = index < names.size() ? names[index] : "extra-barrier";
    gFakeState->mCalls.emplace_back(name);
    invalidateAt(name);
    attemptOperationReentry();
}

VKAPI_ATTR void VKAPI_CALL fakeCmdCopyBufferToImage(VkCommandBuffer          command_buffer,
                                                    VkBuffer                 source,
                                                    VkImage                  destination,
                                                    VkImageLayout            layout,
                                                    std::uint32_t            region_count,
                                                    const VkBufferImageCopy* regions) noexcept
{
    if (!gFakeState || region_count == 0 || !regions)
    {
        return;
    }
    gFakeState->mCopyRecords.push_back(
        { command_buffer, source, destination, layout, std::vector<VkBufferImageCopy>(regions, regions + region_count) });
    gFakeState->mCalls.emplace_back("copy");
    invalidateAt("copy");
    attemptOperationReentry();
}

VKAPI_ATTR void VKAPI_CALL fakeCmdBlitImage(VkCommandBuffer    command_buffer,
                                            VkImage            source,
                                            VkImageLayout      source_layout,
                                            VkImage            destination,
                                            VkImageLayout      destination_layout,
                                            std::uint32_t      region_count,
                                            const VkImageBlit* regions,
                                            VkFilter           filter) noexcept
{
    if (!gFakeState || region_count == 0 || !regions)
    {
        return;
    }
    const std::string name = gFakeState->mBlitRecords.empty() ? "blit0" : "blit1";
    gFakeState->mBlitRecords.push_back({ command_buffer, source, source_layout, destination, destination_layout,
                                         std::vector<VkImageBlit>(regions, regions + region_count), filter });
    gFakeState->mCalls.push_back(name);
    invalidateAt(name);
    attemptOperationReentry();
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
    attemptOperationReentry();
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
    attemptOperationReentry();
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
    attemptOperationReentry();
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
    if (std::strcmp(name, "vkCmdCopyBufferToImage") == 0)
        return eraseFunctionType(fakeCmdCopyBufferToImage);
    if (std::strcmp(name, "vkCmdBlitImage") == 0)
        return eraseFunctionType(fakeCmdBlitImage);
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
    invalidateAt(std::string("resolve:") + name);
    if (gFakeState->mMissingCommand == name)
    {
        return nullptr;
    }
    if (std::strcmp(name, "vkGetDeviceProcAddr") == 0)
        return eraseFunctionType(fakeGetDeviceProcAddr);
    return nullptr;
}

VulkanTextureUploadSourceDescription sourceDescription()
{
    return vulkanTextureUploadSourceDescription(LLRenderContract::makeTextureUploadFixture().mSourceRGBA8);
}

VulkanTextureUploadDestinationDescription destinationDescription()
{
    return vulkanTextureUploadDestinationDescription();
}

VulkanPhysicalDeviceGeneration makePhysical(FakeState& state)
{
    auto result = resolveVulkanPhysicalDeviceGeneration({ fakeGetInstanceProcAddr, state.mInstance, state.mSurface });
    tut::ensure("the physical-device fixture resolves", std::holds_alternative<VulkanPhysicalDeviceGeneration>(result));
    return std::get<VulkanPhysicalDeviceGeneration>(std::move(result));
}

VulkanLogicalDeviceGeneration makeLogical(VulkanPhysicalDeviceGeneration& physical)
{
    auto result = resolveVulkanLogicalDeviceGeneration(physical);
    tut::ensure("the logical-device fixture resolves", std::holds_alternative<VulkanLogicalDeviceGeneration>(result));
    return std::get<VulkanLogicalDeviceGeneration>(std::move(result));
}

struct Resources
{
    explicit Resources(FakeState& state) :
        mPhysical(makePhysical(state)),
        mLogical(makeLogical(mPhysical)),
        mSource(VulkanTextureUploadSourceGenerationTestAccess::create(mPhysical,
                                                                      mLogical,
                                                                      sourceDescription(),
                                                                      state.mSourceBuffer,
                                                                      state.mSourceMemory,
                                                                      fakeDestroyBuffer,
                                                                      fakeFreeMemory)),
        mDestination(VulkanTextureUploadDestinationGenerationTestAccess::create(mPhysical,
                                                                                mLogical,
                                                                                destinationDescription(),
                                                                                state.mDestinationImage,
                                                                                state.mDestinationMemory,
                                                                                state.mDestinationView,
                                                                                fakeDestroyImageView,
                                                                                fakeDestroyImage,
                                                                                fakeFreeMemory))
    {
        state.mInstanceLookups.clear();
        state.mDeviceLookups.clear();
        state.mCalls.clear();
        state.mTransferTeardownOrder.clear();
        state.mPhysicalForAccess    = &mPhysical;
        state.mLogicalForAccess     = &mLogical;
        state.mSourceForAccess      = &mSource;
        state.mDestinationForAccess = &mDestination;
    }

    VulkanPhysicalDeviceGeneration           mPhysical;
    VulkanLogicalDeviceGeneration            mLogical;
    VulkanTextureUploadSourceGeneration      mSource;
    VulkanTextureUploadDestinationGeneration mDestination;
};

VulkanTextureUploadTransferResolutionResult resolveTransfer(Resources& resources)
{
    return resolveVulkanTextureUploadTransferGeneration(resources.mPhysical,
                                                        resources.mLogical,
                                                        sourceDescription(),
                                                        destinationDescription(),
                                                        resources.mSource,
                                                        resources.mDestination);
}

VulkanTextureUploadTransferGeneration takeTransfer(VulkanTextureUploadTransferResolutionResult&& result)
{
    tut::ensure("texture transfer resolution returns a generation", std::holds_alternative<VulkanTextureUploadTransferGeneration>(result));
    return std::get<VulkanTextureUploadTransferGeneration>(std::move(result));
}

void ensureResolutionError(const VulkanTextureUploadTransferResolutionResult& result,
                           VulkanTextureUploadTransferResolutionCode          code,
                           std::optional<VulkanTextureUploadTransferCommand>  command       = std::nullopt,
                           VkResult                                           native_result = VK_SUCCESS)
{
    const auto* error = std::get_if<VulkanTextureUploadTransferResolutionError>(&result);
    tut::ensure("texture transfer resolution returns an error", error != nullptr);
    tut::ensure("the exact resolution error is reported",
                error->mCode == code && error->mCommand == command && error->mResult == native_result);
}

void ensureOperationError(const VulkanTextureUploadTransferOperationResult& result,
                          VulkanTextureUploadTransferOperationCode          code,
                          VulkanTextureUploadTransferDisposition            disposition,
                          std::optional<VulkanTextureUploadTransferCommand> command       = std::nullopt,
                          VkResult                                          native_result = VK_SUCCESS)
{
    const auto* error = std::get_if<VulkanTextureUploadTransferOperationError>(&result);
    tut::ensure("texture transfer operation returns an error", error != nullptr);
    tut::ensure("the exact operation error is reported",
                error->mCode == code && error->mCommand == command && error->mResult == native_result &&
                    error->mDisposition == disposition);
}

void ensureDisposition(const VulkanTextureUploadTransferOperationResult& result, VulkanTextureUploadTransferDisposition expected)
{
    const auto* disposition = std::get_if<VulkanTextureUploadTransferDisposition>(&result);
    tut::ensure("texture transfer operation returns a disposition", disposition != nullptr);
    tut::ensure("the exact transfer disposition is reported", *disposition == expected);
}

void resetMovedResources(FakeState& state)
{
    if (state.mMovedDestination)
    {
        state.mMovedDestination->reset();
    }
    if (state.mMovedSource)
    {
        state.mMovedSource->reset();
    }
}

bool exactImageBarrier(const VkImageMemoryBarrier& barrier,
                       VkImage                     image,
                       VkImageLayout               old_layout,
                       VkImageLayout               new_layout,
                       VkAccessFlags               source_access,
                       VkAccessFlags               destination_access,
                       std::uint32_t               base_mip,
                       std::uint32_t               mip_count) noexcept
{
    return barrier.sType == VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER && barrier.pNext == nullptr && barrier.srcAccessMask == source_access &&
           barrier.dstAccessMask == destination_access && barrier.oldLayout == old_layout && barrier.newLayout == new_layout &&
           barrier.srcQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED && barrier.dstQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED &&
           barrier.image == image && barrier.subresourceRange.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT &&
           barrier.subresourceRange.baseMipLevel == base_mip && barrier.subresourceRange.levelCount == mip_count &&
           barrier.subresourceRange.baseArrayLayer == 0 && barrier.subresourceRange.layerCount == 1;
}

bool exactBlit(const BlitRecord& record,
               VkCommandBuffer   command_buffer,
               VkImage           image,
               std::uint32_t     source_mip,
               VkOffset3D        source_end,
               VkOffset3D        destination_end) noexcept
{
    if (record.mCommandBuffer != command_buffer || record.mSource != image || record.mDestination != image ||
        record.mSourceLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL || record.mDestinationLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL ||
        record.mRegions.size() != 1 || record.mFilter != VK_FILTER_LINEAR)
    {
        return false;
    }
    const VkImageBlit& blit = record.mRegions.front();
    return blit.srcSubresource.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT && blit.srcSubresource.mipLevel == source_mip &&
           blit.srcSubresource.baseArrayLayer == 0 && blit.srcSubresource.layerCount == 1 && blit.srcOffsets[0].x == 0 &&
           blit.srcOffsets[0].y == 0 && blit.srcOffsets[0].z == 0 && blit.srcOffsets[1].x == source_end.x &&
           blit.srcOffsets[1].y == source_end.y && blit.srcOffsets[1].z == source_end.z &&
           blit.dstSubresource.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT && blit.dstSubresource.mipLevel == source_mip + 1 &&
           blit.dstSubresource.baseArrayLayer == 0 && blit.dstSubresource.layerCount == 1 && blit.dstOffsets[0].x == 0 &&
           blit.dstOffsets[0].y == 0 && blit.dstOffsets[0].z == 0 && blit.dstOffsets[1].x == destination_end.x &&
           blit.dstOffsets[1].y == destination_end.y && blit.dstOffsets[1].z == destination_end.z;
}

} // namespace

namespace tut
{

struct render_vulkan_texture_upload_transfer_test
{
};

using render_vulkan_texture_upload_transfer_group  = test_group<render_vulkan_texture_upload_transfer_test>;
using render_vulkan_texture_upload_transfer_object = render_vulkan_texture_upload_transfer_group::object;

render_vulkan_texture_upload_transfer_group render_vulkan_texture_upload_transfer("render Vulkan texture upload transfer");

template<>
template<>
void render_vulkan_texture_upload_transfer_object::test<1>()
{
    set_test_name("the one-shot owner accepts a graphics queue without an explicit transfer flag");

    static_assert(!std::is_default_constructible_v<VulkanTextureUploadTransferGeneration>);
    static_assert(!std::is_copy_constructible_v<VulkanTextureUploadTransferGeneration>);
    static_assert(!std::is_copy_assignable_v<VulkanTextureUploadTransferGeneration>);
    static_assert(std::is_nothrow_move_constructible_v<VulkanTextureUploadTransferGeneration>);
    static_assert(!std::is_move_assignable_v<VulkanTextureUploadTransferGeneration>);
    static_assert(std::is_nothrow_destructible_v<VulkanTextureUploadTransferGeneration>);
    static_assert(noexcept(resolveVulkanTextureUploadTransferGeneration(std::declval<const VulkanPhysicalDeviceGeneration&>(),
                                                                        std::declval<const VulkanLogicalDeviceGeneration&>(),
                                                                        std::declval<const VulkanTextureUploadSourceDescription&>(),
                                                                        std::declval<const VulkanTextureUploadDestinationDescription&>(),
                                                                        std::declval<const VulkanTextureUploadSourceGeneration&>(),
                                                                        std::declval<VulkanTextureUploadDestinationGeneration&>())));
    static_assert(noexcept(std::declval<VulkanTextureUploadTransferGeneration&>().execute(0)));
    static_assert(noexcept(std::declval<VulkanTextureUploadTransferGeneration&>().retryCompletion(0)));
    static_assert(noexcept(std::declval<VulkanTextureUploadTransferGeneration&>().reset()));

    FakeState       state;
    ScopedFakeState scope(state);
    Resources       resources(state);
    ensure("the retained queue family has two graphics queues and no explicit transfer bit",
           resources.mPhysical.queueFamilyProperties().queueCount == 2 &&
               resources.mPhysical.queueFamilyProperties().queueFlags == VK_QUEUE_GRAPHICS_BIT);
    auto transfer = takeTransfer(resolveTransfer(resources));
    ensure("the transfer selects the exact unified queue",
           transfer.queue() == state.mQueue && transfer.queueFamilyIndex() == state.mQueueFamily && transfer.queueIndex() == 0 &&
               transfer.createdFor(resources.mPhysical, resources.mLogical));
    ensure("the ready owner retains both immutable inputs",
           transfer.matchesSourceDescription(sourceDescription()) && transfer.matchesDestinationDescription(destinationDescription()) &&
               transfer.retainsTextureUploadResources(resources.mSource, resources.mDestination) &&
               transfer.resourceHandle() == sourceDescription().mHandle &&
               transfer.expectedRevision() == sourceDescription().mExpectedRevision &&
               transfer.contentIdentity() == resources.mSource.contentIdentity() && transfer.sourceBuffer() == state.mSourceBuffer &&
               transfer.destinationImage() == state.mDestinationImage &&
               transfer.disposition() == VulkanTextureUploadTransferDisposition::Ready && transfer.submissionAttemptCount() == 0 &&
               transfer.completionWaitCount() == 0);
}

template<>
template<>
void render_vulkan_texture_upload_transfer_object::test<2>()
{
    set_test_name("invalid parents, descriptions, resources, and published destinations fail before dispatch");

    {
        FakeState       state;
        ScopedFakeState scope(state);
        Resources       resources(state);
        auto            moved_physical = std::move(resources.mPhysical);
        ensureResolutionError(resolveTransfer(resources), VulkanTextureUploadTransferResolutionCode::InvalidPhysicalDeviceGeneration);
        ensure("an invalid physical parent resolves no transfer command", state.mInstanceLookups.empty() && state.mDeviceLookups.empty());
        resources.mDestination.reset();
        resources.mSource.reset();
        resources.mLogical.reset();
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        Resources       resources(state);
        auto            moved_logical = std::move(resources.mLogical);
        ensureResolutionError(resolveTransfer(resources), VulkanTextureUploadTransferResolutionCode::InvalidLogicalDeviceGeneration);
        ensure("an invalid logical parent resolves no transfer command", state.mInstanceLookups.empty() && state.mDeviceLookups.empty());
        resources.mDestination.reset();
        resources.mSource.reset();
        moved_logical.reset();
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        Resources       resources(state);
        auto            invalid_source = sourceDescription();
        invalid_source.mHandle         = {};
        ensureResolutionError(resolveVulkanTextureUploadTransferGeneration(resources.mPhysical,
                                                                           resources.mLogical,
                                                                           invalid_source,
                                                                           destinationDescription(),
                                                                           resources.mSource,
                                                                           resources.mDestination),
                              VulkanTextureUploadTransferResolutionCode::InvalidDescription);
        ensure("an invalid description resolves no transfer command", state.mInstanceLookups.empty() && state.mDeviceLookups.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        Resources       resources(state);
        auto            mismatch = sourceDescription();
        mismatch.mBytes[0] ^= 0xff;
        ensureResolutionError(resolveVulkanTextureUploadTransferGeneration(resources.mPhysical,
                                                                           resources.mLogical,
                                                                           mismatch,
                                                                           destinationDescription(),
                                                                           resources.mSource,
                                                                           resources.mDestination),
                              VulkanTextureUploadTransferResolutionCode::InvalidTextureUploadSourceGeneration);
        ensure("a mismatched source resolves no transfer command", state.mInstanceLookups.empty() && state.mDeviceLookups.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        Resources       resources(state);
        auto            moved_source = std::move(resources.mSource);
        ensureResolutionError(resolveTransfer(resources), VulkanTextureUploadTransferResolutionCode::InvalidTextureUploadSourceGeneration);
        ensure("a moved source resolves no transfer command", state.mInstanceLookups.empty() && state.mDeviceLookups.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        Resources       resources(state);
        auto            moved_destination = std::move(resources.mDestination);
        ensureResolutionError(resolveTransfer(resources),
                              VulkanTextureUploadTransferResolutionCode::InvalidTextureUploadDestinationGeneration);
        ensure("a moved destination resolves no transfer command", state.mInstanceLookups.empty() && state.mDeviceLookups.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        Resources       resources(state);
        ensure("the fixture destination accepts one completed publication",
               VulkanTextureUploadDestinationGenerationTestAccess::markResident(
                   resources.mDestination, sourceDescription().mExpectedRevision, resources.mSource.contentIdentity()));
        ensureResolutionError(resolveTransfer(resources), VulkanTextureUploadTransferResolutionCode::DestinationAlreadyResident);
        ensure("a resident destination resolves no transfer command", state.mInstanceLookups.empty() && state.mDeviceLookups.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        Resources       resources(state);
        VulkanTextureUploadDestinationGenerationTestAccess::forcePartialPublication(resources.mDestination,
                                                                                    sourceDescription().mExpectedRevision);
        ensureResolutionError(resolveTransfer(resources),
                              VulkanTextureUploadTransferResolutionCode::InvalidTextureUploadDestinationGeneration);
        ensure("partial publication is malformed rather than resident",
               !resources.mDestination.isResident() && state.mInstanceLookups.empty() && state.mDeviceLookups.empty());
    }
}

template<>
template<>
void render_vulkan_texture_upload_transfer_object::test<3>()
{
    set_test_name("every missing transfer command fails before native mutation");

    struct MissingCase
    {
        const char*                        mName;
        VulkanTextureUploadTransferCommand mCommand;
    };
    constexpr std::array cases{
        MissingCase{ "vkGetDeviceProcAddr", VulkanTextureUploadTransferCommand::GetDeviceProcAddr },
        MissingCase{ "vkCreateCommandPool", VulkanTextureUploadTransferCommand::CreateCommandPool },
        MissingCase{ "vkDestroyCommandPool", VulkanTextureUploadTransferCommand::DestroyCommandPool },
        MissingCase{ "vkAllocateCommandBuffers", VulkanTextureUploadTransferCommand::AllocateCommandBuffers },
        MissingCase{ "vkCreateFence", VulkanTextureUploadTransferCommand::CreateFence },
        MissingCase{ "vkDestroyFence", VulkanTextureUploadTransferCommand::DestroyFence },
        MissingCase{ "vkBeginCommandBuffer", VulkanTextureUploadTransferCommand::BeginCommandBuffer },
        MissingCase{ "vkCmdPipelineBarrier", VulkanTextureUploadTransferCommand::CmdPipelineBarrier },
        MissingCase{ "vkCmdCopyBufferToImage", VulkanTextureUploadTransferCommand::CmdCopyBufferToImage },
        MissingCase{ "vkCmdBlitImage", VulkanTextureUploadTransferCommand::CmdBlitImage },
        MissingCase{ "vkEndCommandBuffer", VulkanTextureUploadTransferCommand::EndCommandBuffer },
        MissingCase{ "vkQueueSubmit", VulkanTextureUploadTransferCommand::QueueSubmit },
        MissingCase{ "vkWaitForFences", VulkanTextureUploadTransferCommand::WaitForFences },
    };

    for (const MissingCase& test_case : cases)
    {
        FakeState       state;
        ScopedFakeState scope(state);
        Resources       resources(state);
        state.mMissingCommand = test_case.mName;
        ensureResolutionError(resolveTransfer(resources),
                              VulkanTextureUploadTransferResolutionCode::MissingRequiredCommand,
                              test_case.mCommand);
        ensure("missing dispatch mutates no Vulkan object and publishes nothing",
               state.mCommandPoolRecords.empty() && state.mCommandBufferRecords.empty() && state.mFenceRecords.empty() &&
                   state.mDestroyedCommandPools.empty() && state.mDestroyedFences.empty() && !resources.mDestination.isResident() &&
                   resources.mDestination.currentState() == LLRenderContract::ImageState::Undefined &&
                   resources.mDestination.residentRevision() == 0 && resources.mDestination.residentContentIdentity() == 0);
    }
}

template<>
template<>
void render_vulkan_texture_upload_transfer_object::test<4>()
{
    set_test_name("acquisition owns only successful outputs and rolls back fence before pool");

    enum class Step : std::uint8_t
    {
        Pool,
        CommandBuffer,
        Fence
    };
    struct FailureCase
    {
        Step                                      mStep;
        bool                                      mNullSuccess;
        VulkanTextureUploadTransferResolutionCode mCode;
        VulkanTextureUploadTransferCommand        mCommand;
    };
    constexpr std::array cases{
        FailureCase{ Step::Pool, false, VulkanTextureUploadTransferResolutionCode::CommandPoolCreationFailure,
                     VulkanTextureUploadTransferCommand::CreateCommandPool },
        FailureCase{ Step::Pool, true, VulkanTextureUploadTransferResolutionCode::NullCommandPoolOnSuccess,
                     VulkanTextureUploadTransferCommand::CreateCommandPool },
        FailureCase{ Step::CommandBuffer, false, VulkanTextureUploadTransferResolutionCode::CommandBufferAllocationFailure,
                     VulkanTextureUploadTransferCommand::AllocateCommandBuffers },
        FailureCase{ Step::CommandBuffer, true, VulkanTextureUploadTransferResolutionCode::NullCommandBufferOnSuccess,
                     VulkanTextureUploadTransferCommand::AllocateCommandBuffers },
        FailureCase{ Step::Fence, false, VulkanTextureUploadTransferResolutionCode::FenceCreationFailure,
                     VulkanTextureUploadTransferCommand::CreateFence },
        FailureCase{ Step::Fence, true, VulkanTextureUploadTransferResolutionCode::NullFenceOnSuccess,
                     VulkanTextureUploadTransferCommand::CreateFence },
    };

    for (const FailureCase& test_case : cases)
    {
        FakeState       state;
        ScopedFakeState scope(state);
        Resources       resources(state);
        if (test_case.mStep == Step::Pool)
        {
            state.mCreateCommandPoolResult = test_case.mNullSuccess ? VK_SUCCESS : VK_ERROR_OUT_OF_HOST_MEMORY;
            state.mCreateCommandPoolOutput = test_case.mNullSuccess ? VK_NULL_HANDLE : fakeHandle<VkCommandPool>(0xdead0001);
        }
        else if (test_case.mStep == Step::CommandBuffer)
        {
            state.mAllocateCommandBufferResult = test_case.mNullSuccess ? VK_SUCCESS : VK_ERROR_OUT_OF_HOST_MEMORY;
            state.mAllocateCommandBufferOutput = test_case.mNullSuccess ? VK_NULL_HANDLE : fakeHandle<VkCommandBuffer>(0xdead0002);
        }
        else
        {
            state.mCreateFenceResult = test_case.mNullSuccess ? VK_SUCCESS : VK_ERROR_OUT_OF_HOST_MEMORY;
            state.mCreateFenceOutput = test_case.mNullSuccess ? VK_NULL_HANDLE : fakeHandle<VkFence>(0xdead0003);
        }

        ensureResolutionError(resolveTransfer(resources),
                              test_case.mCode,
                              test_case.mCommand,
                              test_case.mNullSuccess ? VK_SUCCESS : VK_ERROR_OUT_OF_HOST_MEMORY);
        ensure("every output parameter starts null",
               !state.mCommandPoolRecords.empty() && state.mCommandPoolRecords.front().mIncoming == VK_NULL_HANDLE &&
                   (state.mCommandBufferRecords.empty() || state.mCommandBufferRecords.front().mIncoming == VK_NULL_HANDLE) &&
                   (state.mFenceRecords.empty() || state.mFenceRecords.front().mIncoming == VK_NULL_HANDLE));
        ensure("failed or null pool bits are never destroyed",
               test_case.mStep != Step::Pool ||
                   (state.mDestroyedCommandPools.empty() && state.mDestroyedFences.empty() && state.mTransferTeardownOrder.empty()));
        ensure("later acquisition failure rolls back the one successful pool",
               test_case.mStep == Step::Pool ||
                   (state.mDestroyedCommandPools == std::vector<VkCommandPool>{ state.mCommandPool } && state.mDestroyedFences.empty() &&
                    state.mTransferTeardownOrder == std::vector<std::string>{ "destroy-pool" }));
        ensure("a failed fence output is never treated as owned", test_case.mStep != Step::Fence || state.mDestroyedFences.empty());
        ensure("rollback uses the captured device and never publishes the destination",
               !state.mTransferDestroyedWithWrongDevice && !resources.mDestination.isResident());
    }
}

template<>
template<>
void render_vulkan_texture_upload_transfer_object::test<5>()
{
    set_test_name("resolution snapshots descriptions and revalidates after every callback");

    {
        FakeState       state;
        ScopedFakeState scope(state);
        Resources       resources(state);
        auto            source_request        = sourceDescription();
        auto            destination_request   = destinationDescription();
        const auto      original_source       = source_request;
        const auto      original_destination  = destination_request;
        state.mSourceDescriptionToMutate      = &source_request;
        state.mDestinationDescriptionToMutate = &destination_request;

        auto transfer = takeTransfer(resolveVulkanTextureUploadTransferGeneration(resources.mPhysical,
                                                                                  resources.mLogical,
                                                                                  source_request,
                                                                                  destination_request,
                                                                                  resources.mSource,
                                                                                  resources.mDestination));
        ensure("native creation mutates only the caller descriptions",
               source_request != original_source && destination_request != original_destination &&
                   transfer.matchesSourceDescription(original_source) && transfer.matchesDestinationDescription(original_destination));
        ensure("all transfer commands resolve before the first native mutation", state.mAllCommandsResolvedBeforeMutation);
        ensure("resolution asks for no semaphore or swapchain command",
               std::none_of(state.mDeviceLookups.begin(), state.mDeviceLookups.end(), [](const std::string& name)
                            { return name.find("Semaphore") != std::string::npos || name.find("Swapchain") != std::string::npos; }));
        ensure("one exact flags-zero command pool is created",
               state.mCommandPoolRecords.size() == 1 && state.mCommandPoolRecords.front().mDevice == state.mDevice &&
                   state.mCommandPoolRecords.front().mStructureType == VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO &&
                   state.mCommandPoolRecords.front().mNext == nullptr && state.mCommandPoolRecords.front().mFlags == 0 &&
                   state.mCommandPoolRecords.front().mQueueFamily == state.mQueueFamily &&
                   state.mCommandPoolRecords.front().mAllocatorNull && state.mCommandPoolRecords.front().mIncoming == VK_NULL_HANDLE);
        ensure("one primary command buffer is allocated from that pool",
               state.mCommandBufferRecords.size() == 1 && state.mCommandBufferRecords.front().mDevice == state.mDevice &&
                   state.mCommandBufferRecords.front().mStructureType == VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO &&
                   state.mCommandBufferRecords.front().mNext == nullptr &&
                   state.mCommandBufferRecords.front().mPool == state.mCommandPool &&
                   state.mCommandBufferRecords.front().mLevel == VK_COMMAND_BUFFER_LEVEL_PRIMARY &&
                   state.mCommandBufferRecords.front().mCount == 1 && state.mCommandBufferRecords.front().mIncoming == VK_NULL_HANDLE);
        ensure("one unsignaled flags-zero fence is created",
               state.mFenceRecords.size() == 1 && state.mFenceRecords.front().mDevice == state.mDevice &&
                   state.mFenceRecords.front().mStructureType == VK_STRUCTURE_TYPE_FENCE_CREATE_INFO &&
                   state.mFenceRecords.front().mNext == nullptr && state.mFenceRecords.front().mFlags == 0 &&
                   state.mFenceRecords.front().mAllocatorNull && state.mFenceRecords.front().mIncoming == VK_NULL_HANDLE);
        ensure("the owner publishes all acquired handles in Ready",
               transfer.commandPool() == state.mCommandPool && transfer.commandBuffer() == state.mCommandBuffer &&
                   transfer.fence() == state.mFence && transfer.disposition() == VulkanTextureUploadTransferDisposition::Ready);
    }

    struct ReentryCase
    {
        const char*                               mPoint;
        bool                                      mMoveLogical;
        bool                                      mMoveDestination;
        VkResult                                  mNativeResult;
        bool                                      mNullOutput;
        VulkanTextureUploadTransferResolutionCode mExpected;
        std::vector<std::string>                  mTeardown;
    };
    const std::array cases{
        ReentryCase{ "resolve:vkCmdBlitImage",
                     false,
                     false,
                     VK_SUCCESS,
                     false,
                     VulkanTextureUploadTransferResolutionCode::InvalidTextureUploadSourceGeneration,
                     {} },
        ReentryCase{ "create-pool",
                     false,
                     false,
                     VK_SUCCESS,
                     false,
                     VulkanTextureUploadTransferResolutionCode::InvalidTextureUploadSourceGeneration,
                     { "destroy-pool" } },
        ReentryCase{ "create-pool",
                     false,
                     false,
                     VK_ERROR_OUT_OF_HOST_MEMORY,
                     false,
                     VulkanTextureUploadTransferResolutionCode::InvalidTextureUploadSourceGeneration,
                     {} },
        ReentryCase{ "create-pool",
                     false,
                     false,
                     VK_SUCCESS,
                     true,
                     VulkanTextureUploadTransferResolutionCode::InvalidTextureUploadSourceGeneration,
                     {} },
        ReentryCase{ "allocate-command-buffer",
                     false,
                     true,
                     VK_ERROR_OUT_OF_HOST_MEMORY,
                     false,
                     VulkanTextureUploadTransferResolutionCode::InvalidTextureUploadDestinationGeneration,
                     { "destroy-pool" } },
        ReentryCase{ "create-fence",
                     true,
                     false,
                     VK_ERROR_OUT_OF_HOST_MEMORY,
                     false,
                     VulkanTextureUploadTransferResolutionCode::InvalidLogicalDeviceGeneration,
                     { "destroy-pool" } },
        ReentryCase{ "create-fence",
                     false,
                     false,
                     VK_SUCCESS,
                     false,
                     VulkanTextureUploadTransferResolutionCode::InvalidTextureUploadSourceGeneration,
                     { "destroy-fence", "destroy-pool" } },
    };

    for (const ReentryCase& test_case : cases)
    {
        FakeState       state;
        ScopedFakeState scope(state);
        Resources       resources(state);
        state.mInvalidateAt = test_case.mPoint;
        if (test_case.mMoveLogical)
        {
            state.mLogicalToMove = &resources.mLogical;
        }
        else if (test_case.mMoveDestination)
        {
            state.mDestinationToMove = &resources.mDestination;
        }
        else
        {
            state.mSourceToMove = &resources.mSource;
        }
        if (std::string_view(test_case.mPoint) == "create-pool")
        {
            state.mCreateCommandPoolResult = test_case.mNativeResult;
            if (test_case.mNullOutput)
                state.mCreateCommandPoolOutput = VK_NULL_HANDLE;
            else if (test_case.mNativeResult != VK_SUCCESS)
                state.mCreateCommandPoolOutput = fakeHandle<VkCommandPool>(0xdead1001);
        }
        else if (std::string_view(test_case.mPoint) == "allocate-command-buffer")
        {
            state.mAllocateCommandBufferResult = test_case.mNativeResult;
            if (test_case.mNativeResult != VK_SUCCESS)
                state.mAllocateCommandBufferOutput = fakeHandle<VkCommandBuffer>(0xdead1002);
        }
        else if (std::string_view(test_case.mPoint) == "create-fence")
        {
            state.mCreateFenceResult = test_case.mNativeResult;
            if (test_case.mNativeResult != VK_SUCCESS)
                state.mCreateFenceOutput = fakeHandle<VkFence>(0xdead1003);
        }

        ensureResolutionError(resolveTransfer(resources), test_case.mExpected);
        ensure("callback invalidation wins over simultaneous native failure or null output",
               state.mTransferTeardownOrder == test_case.mTeardown);
        ensure("rollback always uses the pre-callback device", !state.mTransferDestroyedWithWrongDevice);
        resetMovedResources(state);
        resources.mDestination.reset();
        resources.mSource.reset();
        if (state.mMovedLogical)
        {
            state.mMovedLogical->reset();
        }
    }
}

template<>
template<>
void render_vulkan_texture_upload_transfer_object::test<6>()
{
    set_test_name("execution records the exact upload, mip generation, submit, wait, and publication");

    FakeState       state;
    ScopedFakeState scope(state);
    Resources       resources(state);
    auto            transfer = takeTransfer(resolveTransfer(resources));
    state.mCalls.clear();
    ensure("resolution leaves the destination wholly unpublished",
           !resources.mDestination.isResident() && resources.mDestination.residentRevision() == 0 &&
               resources.mDestination.residentContentIdentity() == 0 &&
               resources.mDestination.currentState() == LLRenderContract::ImageState::Undefined);

    constexpr std::uint64_t timeout = 37;
    ensureDisposition(transfer.execute(timeout), VulkanTextureUploadTransferDisposition::Complete);
    const std::vector<std::string> expected_calls{ "begin",        "source-barrier", "all-dst-barrier", "copy", "mip0-barrier", "blit0",
                                                   "mip1-barrier", "blit1",          "sampled-barrier", "end",  "submit",       "wait" };
    ensure("commands execute once in the required order", state.mCalls == expected_calls);
    ensure("the primary command buffer begins once for one-shot submission",
           state.mBeginRecords.size() == 1 && state.mBeginRecords.front().mCommandBuffer == state.mCommandBuffer &&
               state.mBeginRecords.front().mStructureType == VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO &&
               state.mBeginRecords.front().mNext == nullptr &&
               state.mBeginRecords.front().mFlags == VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT &&
               state.mBeginRecords.front().mInheritance == nullptr);
    ensure("five exact barrier calls are recorded", state.mBarrierRecords.size() == 5);

    const auto& source_barrier = state.mBarrierRecords[0];
    ensure("the whole source range is ordered from host writes to transfer reads",
           source_barrier.mCommandBuffer == state.mCommandBuffer && source_barrier.mSourceStage == VK_PIPELINE_STAGE_HOST_BIT &&
               source_barrier.mDestinationStage == VK_PIPELINE_STAGE_TRANSFER_BIT && source_barrier.mDependencyFlags == 0 &&
               source_barrier.mMemoryBarrierCount == 0 && source_barrier.mBufferBarriers.size() == 1 &&
               source_barrier.mImageBarriers.empty() &&
               source_barrier.mBufferBarriers[0].sType == VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER &&
               source_barrier.mBufferBarriers[0].pNext == nullptr &&
               source_barrier.mBufferBarriers[0].srcAccessMask == VK_ACCESS_HOST_WRITE_BIT &&
               source_barrier.mBufferBarriers[0].dstAccessMask == VK_ACCESS_TRANSFER_READ_BIT &&
               source_barrier.mBufferBarriers[0].srcQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED &&
               source_barrier.mBufferBarriers[0].dstQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED &&
               source_barrier.mBufferBarriers[0].buffer == state.mSourceBuffer && source_barrier.mBufferBarriers[0].offset == 0 &&
               source_barrier.mBufferBarriers[0].size == VULKAN_TEXTURE_UPLOAD_SOURCE_BYTE_COUNT);

    const auto& all_destination = state.mBarrierRecords[1];
    ensure("all destination mips transition from undefined to transfer destination",
           all_destination.mCommandBuffer == state.mCommandBuffer && all_destination.mSourceStage == VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT &&
               all_destination.mDestinationStage == VK_PIPELINE_STAGE_TRANSFER_BIT && all_destination.mDependencyFlags == 0 &&
               all_destination.mMemoryBarrierCount == 0 && all_destination.mBufferBarriers.empty() &&
               all_destination.mImageBarriers.size() == 1 &&
               exactImageBarrier(all_destination.mImageBarriers[0],
                                 state.mDestinationImage,
                                 VK_IMAGE_LAYOUT_UNDEFINED,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                 0,
                                 VK_ACCESS_TRANSFER_WRITE_BIT,
                                 0,
                                 3));

    ensure("one four-row buffer-to-image copy is recorded", state.mCopyRecords.size() == 1);
    const auto& copy = state.mCopyRecords.front();
    ensure("the copy names the retained buffer, image, command buffer, and layout",
           copy.mCommandBuffer == state.mCommandBuffer && copy.mSource == state.mSourceBuffer &&
               copy.mDestination == state.mDestinationImage && copy.mLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
               copy.mRegions.size() == 4);
    constexpr std::array<VkDeviceSize, 4> offsets{ 108, 72, 36, 0 };
    for (std::uint32_t row = 0; row < copy.mRegions.size(); ++row)
    {
        const VkBufferImageCopy& region = copy.mRegions[row];
        ensure("each packed row reverses source origin without copying padding",
               region.bufferOffset == offsets[row] && region.bufferRowLength == 0 && region.bufferImageHeight == 0 &&
                   region.imageSubresource.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT && region.imageSubresource.mipLevel == 0 &&
                   region.imageSubresource.baseArrayLayer == 0 && region.imageSubresource.layerCount == 1 && region.imageOffset.x == 0 &&
                   region.imageOffset.y == static_cast<std::int32_t>(row) && region.imageOffset.z == 0 && region.imageExtent.width == 8 &&
                   region.imageExtent.height == 1 && region.imageExtent.depth == 1);
    }

    const auto& mip0 = state.mBarrierRecords[2];
    const auto& mip1 = state.mBarrierRecords[3];
    ensure("mip zero becomes the first blit source",
           mip0.mSourceStage == VK_PIPELINE_STAGE_TRANSFER_BIT && mip0.mDestinationStage == VK_PIPELINE_STAGE_TRANSFER_BIT &&
               mip0.mDependencyFlags == 0 && mip0.mMemoryBarrierCount == 0 && mip0.mBufferBarriers.empty() &&
               mip0.mImageBarriers.size() == 1 &&
               exactImageBarrier(mip0.mImageBarriers[0],
                                 state.mDestinationImage,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                 VK_ACCESS_TRANSFER_WRITE_BIT,
                                 VK_ACCESS_TRANSFER_READ_BIT,
                                 0,
                                 1));
    ensure("mip one becomes the second blit source",
           mip1.mSourceStage == VK_PIPELINE_STAGE_TRANSFER_BIT && mip1.mDestinationStage == VK_PIPELINE_STAGE_TRANSFER_BIT &&
               mip1.mDependencyFlags == 0 && mip1.mMemoryBarrierCount == 0 && mip1.mBufferBarriers.empty() &&
               mip1.mImageBarriers.size() == 1 &&
               exactImageBarrier(mip1.mImageBarriers[0],
                                 state.mDestinationImage,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                 VK_ACCESS_TRANSFER_WRITE_BIT,
                                 VK_ACCESS_TRANSFER_READ_BIT,
                                 1,
                                 1));
    ensure("two exact linear blits generate 4 by 2 and 2 by 1 mips",
           state.mBlitRecords.size() == 2 &&
               exactBlit(state.mBlitRecords[0], state.mCommandBuffer, state.mDestinationImage, 0, { 8, 4, 1 }, { 4, 2, 1 }) &&
               exactBlit(state.mBlitRecords[1], state.mCommandBuffer, state.mDestinationImage, 1, { 4, 2, 1 }, { 2, 1, 1 }));

    const auto& sampled = state.mBarrierRecords[4];
    ensure("all three generated mips become fragment-shader readable in one final barrier",
           sampled.mSourceStage == VK_PIPELINE_STAGE_TRANSFER_BIT && sampled.mDestinationStage == VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT &&
               sampled.mDependencyFlags == 0 && sampled.mMemoryBarrierCount == 0 && sampled.mBufferBarriers.empty() &&
               sampled.mImageBarriers.size() == 2 &&
               exactImageBarrier(sampled.mImageBarriers[0],
                                 state.mDestinationImage,
                                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                 VK_ACCESS_TRANSFER_READ_BIT,
                                 VK_ACCESS_SHADER_READ_BIT,
                                 0,
                                 2) &&
               exactImageBarrier(sampled.mImageBarriers[1],
                                 state.mDestinationImage,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                 VK_ACCESS_TRANSFER_WRITE_BIT,
                                 VK_ACCESS_SHADER_READ_BIT,
                                 2,
                                 1));

    ensure("the exact command buffer ends once", state.mEndedCommandBuffers == std::vector<VkCommandBuffer>{ state.mCommandBuffer });
    ensure("one semaphore-free submission uses the retained queue, command buffer, and fence",
           state.mSubmitRecords.size() == 1 && state.mSubmitRecords.front().mQueue == state.mQueue &&
               state.mSubmitRecords.front().mSubmitCount == 1 &&
               state.mSubmitRecords.front().mStructureType == VK_STRUCTURE_TYPE_SUBMIT_INFO &&
               state.mSubmitRecords.front().mNext == nullptr && state.mSubmitRecords.front().mWaitSemaphoreCount == 0 &&
               state.mSubmitRecords.front().mWaitSemaphoresNull && state.mSubmitRecords.front().mWaitStagesNull &&
               state.mSubmitRecords.front().mCommandBufferCount == 1 &&
               state.mSubmitRecords.front().mCommandBuffer == state.mCommandBuffer &&
               state.mSubmitRecords.front().mSignalSemaphoreCount == 0 && state.mSubmitRecords.front().mSignalSemaphoresNull &&
               state.mSubmitRecords.front().mFence == state.mFence);
    ensure("one finite all-fence wait forwards the exact timeout",
           state.mWaitRecords.size() == 1 && state.mWaitRecords.front().mDevice == state.mDevice &&
               state.mWaitRecords.front().mFenceCount == 1 && state.mWaitRecords.front().mFence == state.mFence &&
               state.mWaitRecords.front().mWaitAll == VK_TRUE && state.mWaitRecords.front().mTimeout == timeout);
    ensure("fence success publishes the exact revision, source identity, and shader-read state",
           resources.mDestination.isResident() && resources.mDestination.residentRevision() == sourceDescription().mExpectedRevision &&
               resources.mDestination.residentContentIdentity() == resources.mSource.contentIdentity() &&
               resources.mDestination.currentState() == LLRenderContract::ImageState::ShaderRead);
    ensure("completion releases temporary dependencies but preserves diagnostics",
           !transfer.retainsTextureUploadResources(resources.mSource, resources.mDestination) &&
               transfer.matchesSourceDescription(sourceDescription()) && transfer.matchesDestinationDescription(destinationDescription()) &&
               transfer.createdFor(resources.mPhysical, resources.mLogical) && transfer.submissionAttemptCount() == 1 &&
               transfer.completionWaitCount() == 1);
    const std::size_t calls = state.mCalls.size();
    ensureOperationError(transfer.execute(1),
                         VulkanTextureUploadTransferOperationCode::InvalidDisposition,
                         VulkanTextureUploadTransferDisposition::Complete);
    ensureOperationError(transfer.retryCompletion(1),
                         VulkanTextureUploadTransferOperationCode::InvalidDisposition,
                         VulkanTextureUploadTransferDisposition::Complete);
    ensure("terminal calls make no native call", state.mCalls.size() == calls);
}

template<>
template<>
void render_vulkan_texture_upload_transfer_object::test<7>()
{
    set_test_name("recording failures and callback invalidation never publish stale content");

    for (const auto& [command, result] :
         std::array{ std::tuple{ VulkanTextureUploadTransferCommand::BeginCommandBuffer, VK_ERROR_OUT_OF_HOST_MEMORY },
                     std::tuple{ VulkanTextureUploadTransferCommand::EndCommandBuffer, VK_ERROR_OUT_OF_DEVICE_MEMORY } })
    {
        FakeState       state;
        ScopedFakeState scope(state);
        Resources       resources(state);
        if (command == VulkanTextureUploadTransferCommand::BeginCommandBuffer)
            state.mBeginResult = result;
        else
            state.mEndResult = result;
        auto transfer = takeTransfer(resolveTransfer(resources));
        ensureOperationError(transfer.execute(5),
                             VulkanTextureUploadTransferOperationCode::CommandFailure,
                             VulkanTextureUploadTransferDisposition::ResetRequired,
                             command,
                             result);
        ensure("recording failure never submits, waits, or publishes",
               state.mSubmitRecords.empty() && state.mWaitRecords.empty() && transfer.submissionAttemptCount() == 0 &&
                   transfer.completionWaitCount() == 0 && !resources.mDestination.isResident());
    }

    {
        FakeState       state;
        ScopedFakeState scope(state);
        Resources       resources(state);
        state.mBeginResult  = VK_ERROR_OUT_OF_HOST_MEMORY;
        state.mInvalidateAt = "begin";
        state.mSourceToMove = &resources.mSource;
        auto transfer       = takeTransfer(resolveTransfer(resources));
        ensureOperationError(transfer.execute(5),
                             VulkanTextureUploadTransferOperationCode::InvalidTextureUploadSourceGeneration,
                             VulkanTextureUploadTransferDisposition::ResetRequired);
        ensure("begin callback invalidation wins over its simultaneous native failure",
               state.mSubmitRecords.empty() && state.mWaitRecords.empty() && transfer.submissionAttemptCount() == 0 &&
                   transfer.completionWaitCount() == 0 && !resources.mDestination.isResident() && transfer.reset());
        resetMovedResources(state);
    }

    {
        FakeState       state;
        ScopedFakeState scope(state);
        Resources       resources(state);
        state.mEndResult         = VK_ERROR_OUT_OF_DEVICE_MEMORY;
        state.mInvalidateAt      = "end";
        state.mDestinationToMove = &resources.mDestination;
        auto transfer            = takeTransfer(resolveTransfer(resources));
        ensureOperationError(transfer.execute(5),
                             VulkanTextureUploadTransferOperationCode::InvalidTextureUploadDestinationGeneration,
                             VulkanTextureUploadTransferDisposition::ResetRequired);
        ensure("end callback invalidation wins over its simultaneous native failure",
               state.mSubmitRecords.empty() && state.mWaitRecords.empty() && transfer.submissionAttemptCount() == 0 &&
                   transfer.completionWaitCount() == 0 && state.mMovedDestination && !state.mMovedDestination->isResident() &&
                   transfer.reset());
        resetMovedResources(state);
    }

    struct InvalidationCase
    {
        const char* mPoint;
        bool        mMoveDestination;
        bool        mAfterSubmit;
    };
    constexpr std::array cases{
        InvalidationCase{ "begin", false, false },           InvalidationCase{ "source-barrier", true, false },
        InvalidationCase{ "all-dst-barrier", false, false }, InvalidationCase{ "copy", true, false },
        InvalidationCase{ "mip0-barrier", false, false },    InvalidationCase{ "blit0", true, false },
        InvalidationCase{ "mip1-barrier", false, false },    InvalidationCase{ "blit1", true, false },
        InvalidationCase{ "sampled-barrier", false, false }, InvalidationCase{ "end", true, false },
        InvalidationCase{ "submit", false, true },           InvalidationCase{ "wait", true, true },
    };
    for (const InvalidationCase& test_case : cases)
    {
        FakeState       state;
        ScopedFakeState scope(state);
        Resources       resources(state);
        state.mInvalidateAt = test_case.mPoint;
        if (test_case.mMoveDestination)
            state.mDestinationToMove = &resources.mDestination;
        else
            state.mSourceToMove = &resources.mSource;
        auto transfer = takeTransfer(resolveTransfer(resources));
        ensureOperationError(transfer.execute(7),
                             test_case.mMoveDestination
                                 ? VulkanTextureUploadTransferOperationCode::InvalidTextureUploadDestinationGeneration
                                 : VulkanTextureUploadTransferOperationCode::InvalidTextureUploadSourceGeneration,
                             VulkanTextureUploadTransferDisposition::ResetRequired);
        ensure("stale resources never publish resident state",
               !resources.mDestination.isResident() && (!state.mMovedDestination || !state.mMovedDestination->isResident()));
        ensure("pre-submit invalidation records no submission or wait",
               test_case.mAfterSubmit || (state.mSubmitRecords.empty() && state.mWaitRecords.empty() &&
                                          transfer.submissionAttemptCount() == 0 && transfer.completionWaitCount() == 0));
        ensure("post-submit invalidation retires through one conclusive wait",
               !test_case.mAfterSubmit || (state.mSubmitRecords.size() == 1 && state.mWaitRecords.size() == 1 &&
                                           transfer.submissionAttemptCount() == 1 && transfer.completionWaitCount() == 1));
        ensure("a stale terminal owner resets before moved resource teardown", transfer.reset());
        resetMovedResources(state);
    }
}

template<>
template<>
void render_vulkan_texture_upload_transfer_object::test<8>()
{
    set_test_name("the operation guard rejects reset, move, execute, and retry from every Vulkan callback");

    FakeState       state;
    ScopedFakeState scope(state);
    Resources       resources(state);
    auto            transfer           = takeTransfer(resolveTransfer(resources));
    state.mTransferForOperationReentry = &transfer;
    state.mExerciseOperationReentry    = true;
    ensureDisposition(transfer.execute(11), VulkanTextureUploadTransferDisposition::Complete);
    ensure("all twelve callback boundaries exercised direct owner reentry", state.mOperationReentryAttempts == 12);
    ensure("every callback-time reset is refused", state.mOperationResetRefused);
    ensure("every callback-time move yields an inert destination and preserves the active source", state.mOperationMoveSafe);
    ensure("nested execute and retry calls return typed errors without native recursion", state.mRecursiveOperationsRefused);
    ensure("the guarded outer operation still submits once and publishes once",
           state.mSubmitRecords.size() == 1 && state.mWaitRecords.size() == 1 && resources.mDestination.isResident() &&
               transfer.disposition() == VulkanTextureUploadTransferDisposition::Complete);
}

template<>
template<>
void render_vulkan_texture_upload_transfer_object::test<9>()
{
    set_test_name("submit and wait failures preserve the exact safe disposition");

    {
        FakeState       state;
        ScopedFakeState scope(state);
        Resources       resources(state);
        state.mSubmitResult = VK_ERROR_OUT_OF_HOST_MEMORY;
        auto transfer       = takeTransfer(resolveTransfer(resources));
        ensureOperationError(transfer.execute(13),
                             VulkanTextureUploadTransferOperationCode::CommandFailure,
                             VulkanTextureUploadTransferDisposition::ResetRequired,
                             VulkanTextureUploadTransferCommand::QueueSubmit,
                             VK_ERROR_OUT_OF_HOST_MEMORY);
        ensure("ordinary submit failure does not wait or publish",
               transfer.submissionAttemptCount() == 1 && transfer.completionWaitCount() == 0 && state.mWaitRecords.empty() &&
                   !resources.mDestination.isResident() && transfer.reset());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        Resources       resources(state);
        state.mWaitResults = { VK_ERROR_DEVICE_LOST };
        auto transfer      = takeTransfer(resolveTransfer(resources));
        ensureOperationError(transfer.execute(17),
                             VulkanTextureUploadTransferOperationCode::CommandFailure,
                             VulkanTextureUploadTransferDisposition::DeviceLost,
                             VulkanTextureUploadTransferCommand::WaitForFences,
                             VK_ERROR_DEVICE_LOST);
        ensure("wait-side device loss retires dependencies without publication",
               transfer.submissionAttemptCount() == 1 && transfer.completionWaitCount() == 1 &&
                   !transfer.retainsTextureUploadResources(resources.mSource, resources.mDestination) &&
                   !resources.mDestination.isResident() && transfer.reset());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        Resources       resources(state);
        state.mSubmitResult = VK_ERROR_DEVICE_LOST;
        state.mWaitResults  = { VK_SUCCESS };
        auto transfer       = takeTransfer(resolveTransfer(resources));
        ensureOperationError(transfer.execute(19),
                             VulkanTextureUploadTransferOperationCode::CommandFailure,
                             VulkanTextureUploadTransferDisposition::DeviceLost,
                             VulkanTextureUploadTransferCommand::QueueSubmit,
                             VK_ERROR_DEVICE_LOST);
        ensure("submit-side device loss waits once, never publishes, and then retires dependencies",
               transfer.submissionAttemptCount() == 1 && transfer.completionWaitCount() == 1 && state.mSubmitRecords.size() == 1 &&
                   state.mWaitRecords.size() == 1 && !transfer.retainsTextureUploadResources(resources.mSource, resources.mDestination) &&
                   !resources.mDestination.isResident());
    }
}

template<>
template<>
void render_vulkan_texture_upload_transfer_object::test<10>()
{
    set_test_name("finite waits keep one pending submission retryable without resubmission");

    FakeState       state;
    ScopedFakeState scope(state);
    Resources       resources(state);
    state.mWaitResults             = { VK_TIMEOUT, VK_TIMEOUT, VK_SUCCESS };
    auto              transfer     = takeTransfer(resolveTransfer(resources));
    const std::size_t calls_before = state.mCalls.size();
    ensureOperationError(transfer.execute(std::numeric_limits<std::uint64_t>::max()),
                         VulkanTextureUploadTransferOperationCode::InvalidTimeout,
                         VulkanTextureUploadTransferDisposition::Ready);
    ensureOperationError(transfer.retryCompletion(std::numeric_limits<std::uint64_t>::max()),
                         VulkanTextureUploadTransferOperationCode::InvalidTimeout,
                         VulkanTextureUploadTransferDisposition::Ready);
    ensureOperationError(transfer.retryCompletion(1),
                         VulkanTextureUploadTransferOperationCode::InvalidDisposition,
                         VulkanTextureUploadTransferDisposition::Ready);
    ensure("invalid timeout and disposition make no native call", state.mCalls.size() == calls_before);

    ensureOperationError(transfer.execute(0),
                         VulkanTextureUploadTransferOperationCode::CommandFailure,
                         VulkanTextureUploadTransferDisposition::Pending,
                         VulkanTextureUploadTransferCommand::WaitForFences,
                         VK_TIMEOUT);
    ensure("zero is a finite poll that retains both resources without publication",
           transfer.retainsTextureUploadResources(resources.mSource, resources.mDestination) && transfer.submissionAttemptCount() == 1 &&
               transfer.completionWaitCount() == 1 && !resources.mDestination.isResident() && state.mWaitRecords.size() == 1 &&
               state.mWaitRecords[0].mTimeout == 0);
    const std::size_t pending_calls = state.mCalls.size();
    ensure("pending reset is refused without native teardown", !transfer.reset() && state.mCalls.size() == pending_calls);
    ensureOperationError(transfer.retryCompletion(std::numeric_limits<std::uint64_t>::max()),
                         VulkanTextureUploadTransferOperationCode::InvalidTimeout,
                         VulkanTextureUploadTransferDisposition::Pending);
    ensure("an unbounded retry issues no wait", state.mCalls.size() == pending_calls && state.mWaitRecords.size() == 1);

    ensureOperationError(transfer.retryCompletion(23),
                         VulkanTextureUploadTransferOperationCode::CommandFailure,
                         VulkanTextureUploadTransferDisposition::Pending,
                         VulkanTextureUploadTransferCommand::WaitForFences,
                         VK_TIMEOUT);
    ensure("a finite timeout retry waits only and keeps the same pending submission",
           state.mSubmitRecords.size() == 1 && state.mWaitRecords.size() == 2 && state.mWaitRecords[1].mTimeout == 23 &&
               state.mCalls.back() == "wait" && transfer.submissionAttemptCount() == 1 && transfer.completionWaitCount() == 2 &&
               !resources.mDestination.isResident());
    const std::size_t before_final_retry = state.mCalls.size();
    ensureDisposition(transfer.retryCompletion(29), VulkanTextureUploadTransferDisposition::Complete);
    ensure("the conclusive retry adds only one wait and never resubmits",
           state.mCalls.size() == before_final_retry + 1 && state.mCalls.back() == "wait" && state.mSubmitRecords.size() == 1 &&
               state.mWaitRecords.size() == 3 && state.mWaitRecords[2].mTimeout == 29 && transfer.submissionAttemptCount() == 1 &&
               transfer.completionWaitCount() == 3);
    ensure("only conclusive fence success publishes residency",
           resources.mDestination.isResident() && resources.mDestination.residentContentIdentity() == resources.mSource.contentIdentity() &&
               !transfer.retainsTextureUploadResources(resources.mSource, resources.mDestination));
}

template<>
template<>
void render_vulkan_texture_upload_transfer_object::test<11>()
{
    set_test_name("move and reset transfer typed duplicate handles with detached teardown callbacks");

    FakeState       state;
    ScopedFakeState scope(state);
    Resources       resources(state);
    state.mCreateFenceOutput = fakeHandle<VkFence>(handleBits(state.mCommandPool));
    state.mWaitResults       = { VK_TIMEOUT, VK_SUCCESS };
    auto transfer            = takeTransfer(resolveTransfer(resources));
    ensureOperationError(transfer.execute(31),
                         VulkanTextureUploadTransferOperationCode::CommandFailure,
                         VulkanTextureUploadTransferDisposition::Pending,
                         VulkanTextureUploadTransferCommand::WaitForFences,
                         VK_TIMEOUT);

    auto moved = std::move(transfer);
    ensure("the moved-from owner makes every public ownership query inert", transferAccessorsInert(transfer) && transfer.reset());
    ensure("the moved-to owner preserves the pending obligation and exact counters",
           moved.commandPool() == state.mCommandPool && handleBits(moved.fence()) == handleBits(state.mCommandPool) &&
               moved.commandBuffer() == state.mCommandBuffer && moved.disposition() == VulkanTextureUploadTransferDisposition::Pending &&
               moved.submissionAttemptCount() == 1 && moved.completionWaitCount() == 1 &&
               moved.retainsTextureUploadResources(resources.mSource, resources.mDestination) && !moved.reset());
    ensureDisposition(moved.retryCompletion(37), VulkanTextureUploadTransferDisposition::Complete);

    state.mTransferTeardownOrder.clear();
    state.mResetToReenter = &moved;
    state.mReenterAt      = TeardownPoint::Both;
    ensure("completed reset succeeds", moved.reset());
    ensure("typed duplicate bits destroy the fence before the pool",
           state.mDestroyedFences == std::vector<VkFence>{ state.mCreateFenceOutput } &&
               state.mDestroyedCommandPools == std::vector<VkCommandPool>{ state.mCommandPool } &&
               state.mTransferTeardownOrder == std::vector<std::string>{ "destroy-fence", "destroy-pool" } &&
               !state.mTransferDestroyedWithWrongDevice);
    ensure("both teardown callbacks observe detached state and nested reset is inert",
           state.mResetReentryAttempts == 2 && state.mResetReentryObservedInert && state.mResetReentryReturnedTrue &&
               transferAccessorsInert(moved));
    ensure("transfer teardown precedes logical-device teardown", state.mDestroyDeviceCalls == 0);
    const std::size_t teardown_count = state.mTransferTeardownOrder.size();
    ensure("reset is idempotent", moved.reset() && state.mTransferTeardownOrder.size() == teardown_count);
}

} // namespace tut
