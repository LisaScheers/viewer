/**
 * @file llrendervulkantextureuploadsamplebinding_test.cpp
 * @brief Focused contract tests for one sampled streamed-texture binding.
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

#include "llrendervulkantextureuploadsamplebinding.h"
#include "lltut.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace LLRenderVulkan
{

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

template<typename Function>
PFN_vkVoidFunction eraseFunctionType(Function function) noexcept
{
    return reinterpret_cast<PFN_vkVoidFunction>(function);
}

struct SamplerRecord
{
    VkDevice            mDevice = VK_NULL_HANDLE;
    VkSamplerCreateInfo mInfo{};
    bool                mAllocatorNull = false;
    VkSampler           mIncoming      = VK_NULL_HANDLE;
};

struct DescriptorSetLayoutRecord
{
    VkDevice                        mDevice = VK_NULL_HANDLE;
    VkDescriptorSetLayoutCreateInfo mInfo{};
    VkDescriptorSetLayoutBinding    mBinding{};
    bool                            mAllocatorNull = false;
    VkDescriptorSetLayout           mIncoming      = VK_NULL_HANDLE;
};

struct PipelineLayoutRecord
{
    VkDevice                   mDevice = VK_NULL_HANDLE;
    VkPipelineLayoutCreateInfo mInfo{};
    VkDescriptorSetLayout      mSetLayout     = VK_NULL_HANDLE;
    bool                       mAllocatorNull = false;
    VkPipelineLayout           mIncoming      = VK_NULL_HANDLE;
};

struct DescriptorPoolRecord
{
    VkDevice                   mDevice = VK_NULL_HANDLE;
    VkDescriptorPoolCreateInfo mInfo{};
    VkDescriptorPoolSize       mPoolSize{};
    bool                       mAllocatorNull = false;
    VkDescriptorPool           mIncoming      = VK_NULL_HANDLE;
};

struct DescriptorSetAllocationRecord
{
    VkDevice                    mDevice = VK_NULL_HANDLE;
    VkDescriptorSetAllocateInfo mInfo{};
    VkDescriptorSetLayout       mSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet             mIncoming  = VK_NULL_HANDLE;
};

struct DescriptorWriteRecord
{
    VkDevice              mDevice     = VK_NULL_HANDLE;
    std::uint32_t         mWriteCount = 0;
    VkWriteDescriptorSet  mWrite{};
    VkDescriptorImageInfo mImage{};
    std::uint32_t         mCopyCount  = 0;
    bool                  mCopiesNull = false;
};

struct FakeState
{
    VkInstance            mInstance            = fakeHandle<VkInstance>(0x1000);
    VkSurfaceKHR          mSurface             = fakeHandle<VkSurfaceKHR>(0x2000);
    VkPhysicalDevice      mPhysicalDevice      = fakeHandle<VkPhysicalDevice>(0x3000);
    VkDevice              mDevice              = fakeHandle<VkDevice>(0x4000);
    VkQueue               mQueue               = fakeHandle<VkQueue>(0x5000);
    VkImage               mImage               = fakeHandle<VkImage>(0x6000);
    VkDeviceMemory        mMemory              = fakeHandle<VkDeviceMemory>(0x7000);
    VkImageView           mImageView           = fakeHandle<VkImageView>(0x8000);
    VkSampler             mSampler             = fakeHandle<VkSampler>(0x9000);
    VkDescriptorSetLayout mDescriptorSetLayout = fakeHandle<VkDescriptorSetLayout>(0xa000);
    VkPipelineLayout      mPipelineLayout      = fakeHandle<VkPipelineLayout>(0xb000);
    VkDescriptorPool      mDescriptorPool      = fakeHandle<VkDescriptorPool>(0xc000);
    VkDescriptorSet       mDescriptorSet       = fakeHandle<VkDescriptorSet>(0xd000);
    std::uint32_t         mQueueFamily         = 2;
    std::uint32_t         mQueueCount          = 2;
    std::uint64_t         mContentIdentity     = 0x123456789abcdef0ULL;

    bool        mOwnerPhase = false;
    std::string mMissingCommand;
    std::string mInvalidateAt;

    VkResult              mCreateSamplerResult             = VK_SUCCESS;
    VkSampler             mCreateSamplerOutput             = mSampler;
    VkResult              mCreateDescriptorSetLayoutResult = VK_SUCCESS;
    VkDescriptorSetLayout mCreateDescriptorSetLayoutOutput = mDescriptorSetLayout;
    VkResult              mCreatePipelineLayoutResult      = VK_SUCCESS;
    VkPipelineLayout      mCreatePipelineLayoutOutput      = mPipelineLayout;
    VkResult              mCreateDescriptorPoolResult      = VK_SUCCESS;
    VkDescriptorPool      mCreateDescriptorPoolOutput      = mDescriptorPool;
    VkResult              mAllocateDescriptorSetResult     = VK_SUCCESS;
    VkDescriptorSet       mAllocateDescriptorSetOutput     = mDescriptorSet;

    std::vector<std::string> mInstanceLookups;
    std::vector<std::string> mDeviceLookups;
    std::vector<std::string> mCalls;
    bool                     mAllCommandsResolvedBeforeCreate = false;

    std::vector<SamplerRecord>                 mSamplerRecords;
    std::vector<DescriptorSetLayoutRecord>     mDescriptorSetLayoutRecords;
    std::vector<PipelineLayoutRecord>          mPipelineLayoutRecords;
    std::vector<DescriptorPoolRecord>          mDescriptorPoolRecords;
    std::vector<DescriptorSetAllocationRecord> mDescriptorSetAllocationRecords;
    std::vector<DescriptorWriteRecord>         mDescriptorWriteRecords;
    std::vector<VkSampler>                     mDestroyedSamplers;
    std::vector<VkDescriptorSetLayout>         mDestroyedDescriptorSetLayouts;
    std::vector<VkPipelineLayout>              mDestroyedPipelineLayouts;
    std::vector<VkDescriptorPool>              mDestroyedDescriptorPools;
    std::vector<std::string>                   mTeardownOrder;
    bool                                       mDestroyedWithWrongDevice = false;
    std::size_t                                mDestroyDeviceCalls       = 0;

    VulkanLogicalDeviceGeneration*                          mLogicalToMove     = nullptr;
    VulkanTextureUploadDestinationGeneration*               mDestinationToMove = nullptr;
    std::optional<VulkanLogicalDeviceGeneration>            mMovedLogical;
    std::optional<VulkanTextureUploadDestinationGeneration> mMovedDestination;
    VulkanTextureUploadDestinationDescription*              mDestinationDescriptionToMutate = nullptr;
    VulkanTextureUploadSampleBindingDescription*            mDescriptionToMutate            = nullptr;

    VulkanTextureUploadSampleBindingGeneration*     mBindingToReenter          = nullptr;
    bool                                            mReenterTeardown           = false;
    std::size_t                                     mResetReentryAttempts      = 0;
    bool                                            mResetReentryObservedInert = true;
    const VulkanPhysicalDeviceGeneration*           mPhysicalForAccess         = nullptr;
    const VulkanLogicalDeviceGeneration*            mLogicalForAccess          = nullptr;
    const VulkanTextureUploadDestinationGeneration* mDestinationForAccess      = nullptr;
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

VulkanTextureUploadSampleBindingDescription bindingDescription()
{
    return vulkanTextureUploadSampleBindingDescription();
}

bool bindingAccessorsInert(const VulkanTextureUploadSampleBindingGeneration& binding) noexcept
{
    bool inert = !binding.samplerResourceHandle() && !binding.destinationResourceHandle() && binding.expectedRevision() == 0 &&
                 binding.residentRevision() == 0 && binding.residentContentIdentity() == 0 &&
                 binding.destinationImageView() == VK_NULL_HANDLE && binding.destinationImageLayout() == VK_IMAGE_LAYOUT_MAX_ENUM &&
                 binding.descriptorSetIndex() == 0 && binding.binding() == 0 && binding.sampler() == VK_NULL_HANDLE &&
                 binding.descriptorSetLayout() == VK_NULL_HANDLE && binding.pipelineLayout() == VK_NULL_HANDLE &&
                 binding.descriptorPool() == VK_NULL_HANDLE && binding.descriptorSet() == VK_NULL_HANDLE &&
                 !binding.matchesDescription(bindingDescription());
    if (gFakeState && gFakeState->mPhysicalForAccess && gFakeState->mLogicalForAccess && gFakeState->mDestinationForAccess)
    {
        inert &= !binding.createdFor(*gFakeState->mPhysicalForAccess, *gFakeState->mLogicalForAccess, *gFakeState->mDestinationForAccess) &&
                 !binding.retainsTextureUploadDestinationGeneration(*gFakeState->mDestinationForAccess);
    }
    return inert;
}

void attemptResetReentry() noexcept
{
    if (!gFakeState || !gFakeState->mReenterTeardown || !gFakeState->mBindingToReenter)
    {
        return;
    }
    ++gFakeState->mResetReentryAttempts;
    gFakeState->mResetReentryObservedInert &= bindingAccessorsInert(*gFakeState->mBindingToReenter);
    gFakeState->mBindingToReenter->reset();
    gFakeState->mResetReentryObservedInert &= bindingAccessorsInert(*gFakeState->mBindingToReenter);
}

void invalidateAt(std::string_view point) noexcept
{
    if (!gFakeState || gFakeState->mInvalidateAt != point)
    {
        return;
    }
    gFakeState->mInvalidateAt.clear();
    if (gFakeState->mDescriptionToMutate)
    {
        gFakeState->mDescriptionToMutate->mBinding = 1;
        gFakeState->mDescriptionToMutate           = nullptr;
    }
    if (gFakeState->mDestinationDescriptionToMutate)
    {
        ++gFakeState->mDestinationDescriptionToMutate->mExpectedRevision;
        gFakeState->mDestinationDescriptionToMutate = nullptr;
    }
    if (gFakeState->mLogicalToMove)
    {
        gFakeState->mMovedLogical.emplace(std::move(*gFakeState->mLogicalToMove));
        gFakeState->mLogicalToMove = nullptr;
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
    if (gFakeState && physical_device == gFakeState->mPhysicalDevice && properties)
    {
        *properties            = {};
        properties->apiVersion = VK_API_VERSION_1_1;
        std::strncpy(properties->deviceName, "texture-upload-binding-fake", VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 1);
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

VKAPI_ATTR void VKAPI_CALL fakeDestroyImageView(VkDevice, VkImageView, const VkAllocationCallbacks*) noexcept
{
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyImage(VkDevice, VkImage, const VkAllocationCallbacks*) noexcept
{
}

VKAPI_ATTR void VKAPI_CALL fakeFreeMemory(VkDevice, VkDeviceMemory, const VkAllocationCallbacks*) noexcept
{
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateSampler(VkDevice                     device,
                                                 const VkSamplerCreateInfo*   info,
                                                 const VkAllocationCallbacks* allocator,
                                                 VkSampler*                   sampler) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || !info || !sampler)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    constexpr std::array expected{
        "vkCreateSampler",          "vkDestroySampler",        "vkCreateDescriptorSetLayout", "vkDestroyDescriptorSetLayout",
        "vkCreatePipelineLayout",   "vkDestroyPipelineLayout", "vkCreateDescriptorPool",      "vkDestroyDescriptorPool",
        "vkAllocateDescriptorSets", "vkUpdateDescriptorSets"
    };
    gFakeState->mAllCommandsResolvedBeforeCreate =
        std::equal(expected.begin(), expected.end(), gFakeState->mDeviceLookups.begin(), gFakeState->mDeviceLookups.end());
    gFakeState->mSamplerRecords.push_back({ device, *info, allocator == nullptr, *sampler });
    gFakeState->mCalls.emplace_back("create-sampler");
    *sampler = gFakeState->mCreateSamplerOutput;
    invalidateAt("create-sampler");
    return gFakeState->mCreateSamplerResult;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroySampler(VkDevice device, VkSampler sampler, const VkAllocationCallbacks*) noexcept
{
    if (gFakeState)
    {
        gFakeState->mDestroyedWithWrongDevice |= device != gFakeState->mDevice;
        gFakeState->mDestroyedSamplers.push_back(sampler);
        gFakeState->mTeardownOrder.emplace_back("destroy-sampler");
        attemptResetReentry();
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateDescriptorSetLayout(VkDevice                               device,
                                                             const VkDescriptorSetLayoutCreateInfo* info,
                                                             const VkAllocationCallbacks*           allocator,
                                                             VkDescriptorSetLayout*                 layout) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || !info || !layout || info->bindingCount != 1 || !info->pBindings)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    gFakeState->mDescriptorSetLayoutRecords.push_back({ device, *info, info->pBindings[0], allocator == nullptr, *layout });
    gFakeState->mCalls.emplace_back("create-layout");
    *layout = gFakeState->mCreateDescriptorSetLayoutOutput;
    invalidateAt("create-layout");
    return gFakeState->mCreateDescriptorSetLayoutResult;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyDescriptorSetLayout(VkDevice              device,
                                                          VkDescriptorSetLayout layout,
                                                          const VkAllocationCallbacks*) noexcept
{
    if (gFakeState)
    {
        gFakeState->mDestroyedWithWrongDevice |= device != gFakeState->mDevice;
        gFakeState->mDestroyedDescriptorSetLayouts.push_back(layout);
        gFakeState->mTeardownOrder.emplace_back("destroy-layout");
        attemptResetReentry();
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreatePipelineLayout(VkDevice                          device,
                                                        const VkPipelineLayoutCreateInfo* info,
                                                        const VkAllocationCallbacks*      allocator,
                                                        VkPipelineLayout*                 layout) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || !info || !layout || info->setLayoutCount != 1 || !info->pSetLayouts)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    gFakeState->mPipelineLayoutRecords.push_back({ device, *info, info->pSetLayouts[0], allocator == nullptr, *layout });
    gFakeState->mCalls.emplace_back("create-pipeline-layout");
    *layout = gFakeState->mCreatePipelineLayoutOutput;
    invalidateAt("create-pipeline-layout");
    return gFakeState->mCreatePipelineLayoutResult;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyPipelineLayout(VkDevice device, VkPipelineLayout layout, const VkAllocationCallbacks*) noexcept
{
    if (gFakeState)
    {
        gFakeState->mDestroyedWithWrongDevice |= device != gFakeState->mDevice;
        gFakeState->mDestroyedPipelineLayouts.push_back(layout);
        gFakeState->mTeardownOrder.emplace_back("destroy-pipeline-layout");
        attemptResetReentry();
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateDescriptorPool(VkDevice                          device,
                                                        const VkDescriptorPoolCreateInfo* info,
                                                        const VkAllocationCallbacks*      allocator,
                                                        VkDescriptorPool*                 pool) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || !info || !pool || info->poolSizeCount != 1 || !info->pPoolSizes)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    gFakeState->mDescriptorPoolRecords.push_back({ device, *info, info->pPoolSizes[0], allocator == nullptr, *pool });
    gFakeState->mCalls.emplace_back("create-pool");
    *pool = gFakeState->mCreateDescriptorPoolOutput;
    invalidateAt("create-pool");
    return gFakeState->mCreateDescriptorPoolResult;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyDescriptorPool(VkDevice device, VkDescriptorPool pool, const VkAllocationCallbacks*) noexcept
{
    if (gFakeState)
    {
        gFakeState->mDestroyedWithWrongDevice |= device != gFakeState->mDevice;
        gFakeState->mDestroyedDescriptorPools.push_back(pool);
        gFakeState->mTeardownOrder.emplace_back("destroy-pool");
        attemptResetReentry();
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeAllocateDescriptorSets(VkDevice                           device,
                                                          const VkDescriptorSetAllocateInfo* info,
                                                          VkDescriptorSet*                   descriptor_set) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || !info || !descriptor_set || info->descriptorSetCount != 1 || !info->pSetLayouts)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    gFakeState->mDescriptorSetAllocationRecords.push_back({ device, *info, info->pSetLayouts[0], *descriptor_set });
    gFakeState->mCalls.emplace_back("allocate-set");
    *descriptor_set = gFakeState->mAllocateDescriptorSetOutput;
    invalidateAt("allocate-set");
    return gFakeState->mAllocateDescriptorSetResult;
}

VKAPI_ATTR void VKAPI_CALL fakeUpdateDescriptorSets(VkDevice                    device,
                                                    std::uint32_t               write_count,
                                                    const VkWriteDescriptorSet* writes,
                                                    std::uint32_t               copy_count,
                                                    const VkCopyDescriptorSet*  copies) noexcept
{
    if (!gFakeState || !writes || write_count != 1 || !writes[0].pImageInfo)
    {
        return;
    }
    gFakeState->mDescriptorWriteRecords.push_back(
        { device, write_count, writes[0], writes[0].pImageInfo[0], copy_count, copies == nullptr });
    gFakeState->mCalls.emplace_back("update-set");
    invalidateAt("update-set");
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
    if (std::strcmp(name, "vkCreateSampler") == 0)
        return eraseFunctionType(fakeCreateSampler);
    if (std::strcmp(name, "vkDestroySampler") == 0)
        return eraseFunctionType(fakeDestroySampler);
    if (std::strcmp(name, "vkCreateDescriptorSetLayout") == 0)
        return eraseFunctionType(fakeCreateDescriptorSetLayout);
    if (std::strcmp(name, "vkDestroyDescriptorSetLayout") == 0)
        return eraseFunctionType(fakeDestroyDescriptorSetLayout);
    if (std::strcmp(name, "vkCreatePipelineLayout") == 0)
        return eraseFunctionType(fakeCreatePipelineLayout);
    if (std::strcmp(name, "vkDestroyPipelineLayout") == 0)
        return eraseFunctionType(fakeDestroyPipelineLayout);
    if (std::strcmp(name, "vkCreateDescriptorPool") == 0)
        return eraseFunctionType(fakeCreateDescriptorPool);
    if (std::strcmp(name, "vkDestroyDescriptorPool") == 0)
        return eraseFunctionType(fakeDestroyDescriptorPool);
    if (std::strcmp(name, "vkAllocateDescriptorSets") == 0)
        return eraseFunctionType(fakeAllocateDescriptorSets);
    if (std::strcmp(name, "vkUpdateDescriptorSets") == 0)
        return eraseFunctionType(fakeUpdateDescriptorSets);
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
        invalidateAt(std::string("resolve:") + name);
        if (gFakeState->mMissingCommand == name)
        {
            return nullptr;
        }
    }
    if (std::strcmp(name, "vkGetDeviceProcAddr") == 0)
        return eraseFunctionType(fakeGetDeviceProcAddr);
    return nullptr;
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
    explicit Resources(FakeState& state, bool resident = true) :
        mPhysical(makePhysical(state)),
        mLogical(makeLogical(mPhysical)),
        mDestination(VulkanTextureUploadDestinationGenerationTestAccess::create(mPhysical,
                                                                                mLogical,
                                                                                vulkanTextureUploadDestinationDescription(),
                                                                                state.mImage,
                                                                                state.mMemory,
                                                                                state.mImageView,
                                                                                fakeDestroyImageView,
                                                                                fakeDestroyImage,
                                                                                fakeFreeMemory))
    {
        if (resident)
        {
            tut::ensure("the destination fixture publishes",
                        VulkanTextureUploadDestinationGenerationTestAccess::markResident(mDestination,
                                                                                         LLRenderContract::TEXTURE_UPLOAD_REVISION,
                                                                                         state.mContentIdentity));
        }
        state.mOwnerPhase           = true;
        state.mPhysicalForAccess    = &mPhysical;
        state.mLogicalForAccess     = &mLogical;
        state.mDestinationForAccess = &mDestination;
        state.mInstanceLookups.clear();
        state.mDeviceLookups.clear();
        state.mCalls.clear();
    }

    VulkanPhysicalDeviceGeneration           mPhysical;
    VulkanLogicalDeviceGeneration            mLogical;
    VulkanTextureUploadDestinationGeneration mDestination;
};

VulkanTextureUploadSampleBindingResolutionResult resolveBinding(
    Resources&                                         resources,
    const VulkanTextureUploadSampleBindingDescription& description = vulkanTextureUploadSampleBindingDescription())
{
    return resolveVulkanTextureUploadSampleBindingGeneration(resources.mPhysical,
                                                             resources.mLogical,
                                                             vulkanTextureUploadDestinationDescription(),
                                                             description,
                                                             resources.mDestination);
}

VulkanTextureUploadSampleBindingGeneration takeBinding(VulkanTextureUploadSampleBindingResolutionResult&& result)
{
    tut::ensure("sample binding resolution returns a generation",
                std::holds_alternative<VulkanTextureUploadSampleBindingGeneration>(result));
    return std::get<VulkanTextureUploadSampleBindingGeneration>(std::move(result));
}

void ensureResolutionError(const VulkanTextureUploadSampleBindingResolutionResult& result,
                           VulkanTextureUploadSampleBindingResolutionCode          code,
                           std::optional<VulkanTextureUploadSampleBindingCommand>  command       = std::nullopt,
                           VkResult                                                native_result = VK_SUCCESS)
{
    const auto* error = std::get_if<VulkanTextureUploadSampleBindingResolutionError>(&result);
    tut::ensure("sample binding resolution returns an error", error != nullptr);
    tut::ensure("the exact sample binding resolution error is reported",
                error->mCode == code && error->mCommand == command && error->mResult == native_result);
}

bool exactSampler(const SamplerRecord& record, const FakeState& state) noexcept
{
    const VkSamplerCreateInfo& info = record.mInfo;
    return record.mDevice == state.mDevice && info.sType == VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO && info.pNext == nullptr &&
           info.flags == 0 && info.magFilter == VK_FILTER_LINEAR && info.minFilter == VK_FILTER_LINEAR &&
           info.mipmapMode == VK_SAMPLER_MIPMAP_MODE_LINEAR && info.addressModeU == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE &&
           info.addressModeV == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE && info.addressModeW == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE &&
           info.mipLodBias == 0.f && info.anisotropyEnable == VK_FALSE && info.maxAnisotropy == 1.f && info.compareEnable == VK_FALSE &&
           info.compareOp == VK_COMPARE_OP_ALWAYS && info.minLod == 0.f && info.maxLod == 2.f &&
           info.borderColor == VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK && info.unnormalizedCoordinates == VK_FALSE &&
           record.mAllocatorNull && record.mIncoming == VK_NULL_HANDLE;
}

bool exactDescriptorInterface(const FakeState& state) noexcept
{
    if (state.mDescriptorSetLayoutRecords.size() != 1 || state.mPipelineLayoutRecords.size() != 1 ||
        state.mDescriptorPoolRecords.size() != 1 || state.mDescriptorSetAllocationRecords.size() != 1 ||
        state.mDescriptorWriteRecords.size() != 1)
    {
        return false;
    }
    const auto& layout     = state.mDescriptorSetLayoutRecords.front();
    const auto& binding    = layout.mBinding;
    const auto& pipeline   = state.mPipelineLayoutRecords.front();
    const auto& pool       = state.mDescriptorPoolRecords.front();
    const auto& allocation = state.mDescriptorSetAllocationRecords.front();
    const auto& update     = state.mDescriptorWriteRecords.front();
    return layout.mDevice == state.mDevice && layout.mInfo.sType == VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO &&
           layout.mInfo.pNext == nullptr && layout.mInfo.flags == 0 && layout.mInfo.bindingCount == 1 && layout.mAllocatorNull &&
           layout.mIncoming == VK_NULL_HANDLE && binding.binding == 0 &&
           binding.descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER && binding.descriptorCount == 1 &&
           binding.stageFlags == VK_SHADER_STAGE_FRAGMENT_BIT && binding.pImmutableSamplers == nullptr &&
           pipeline.mDevice == state.mDevice && pipeline.mInfo.sType == VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO &&
           pipeline.mInfo.pNext == nullptr && pipeline.mInfo.flags == 0 && pipeline.mInfo.setLayoutCount == 1 &&
           pipeline.mSetLayout == state.mDescriptorSetLayout && pipeline.mInfo.pushConstantRangeCount == 0 &&
           pipeline.mInfo.pPushConstantRanges == nullptr && pipeline.mAllocatorNull && pipeline.mIncoming == VK_NULL_HANDLE &&
           pool.mDevice == state.mDevice && pool.mInfo.sType == VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO &&
           pool.mInfo.pNext == nullptr && pool.mInfo.flags == 0 && pool.mInfo.maxSets == 1 && pool.mInfo.poolSizeCount == 1 &&
           pool.mPoolSize.type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER && pool.mPoolSize.descriptorCount == 1 && pool.mAllocatorNull &&
           pool.mIncoming == VK_NULL_HANDLE && allocation.mDevice == state.mDevice &&
           allocation.mInfo.sType == VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO && allocation.mInfo.pNext == nullptr &&
           allocation.mInfo.descriptorPool == state.mDescriptorPool && allocation.mInfo.descriptorSetCount == 1 &&
           allocation.mSetLayout == state.mDescriptorSetLayout && allocation.mIncoming == VK_NULL_HANDLE &&
           update.mDevice == state.mDevice && update.mWriteCount == 1 && update.mWrite.sType == VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET &&
           update.mWrite.pNext == nullptr && update.mWrite.dstSet == state.mDescriptorSet && update.mWrite.dstBinding == 0 &&
           update.mWrite.dstArrayElement == 0 && update.mWrite.descriptorCount == 1 &&
           update.mWrite.descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER && update.mWrite.pBufferInfo == nullptr &&
           update.mWrite.pTexelBufferView == nullptr && update.mImage.sampler == state.mSampler &&
           update.mImage.imageView == state.mImageView && update.mImage.imageLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
           update.mCopyCount == 0 && update.mCopiesNull;
}

} // namespace

namespace tut
{

struct render_vulkan_texture_upload_sample_binding_test
{
};

using render_vulkan_texture_upload_sample_binding_group  = test_group<render_vulkan_texture_upload_sample_binding_test>;
using render_vulkan_texture_upload_sample_binding_object = render_vulkan_texture_upload_sample_binding_group::object;

render_vulkan_texture_upload_sample_binding_group render_vulkan_texture_upload_sample_binding(
    "render Vulkan texture upload sample binding");

template<>
template<>
void render_vulkan_texture_upload_sample_binding_object::test<1>()
{
    set_test_name("the immutable owner publishes the exact resident destination lineage");

    static_assert(!std::is_default_constructible_v<VulkanTextureUploadSampleBindingGeneration>);
    static_assert(!std::is_copy_constructible_v<VulkanTextureUploadSampleBindingGeneration>);
    static_assert(!std::is_copy_assignable_v<VulkanTextureUploadSampleBindingGeneration>);
    static_assert(std::is_nothrow_move_constructible_v<VulkanTextureUploadSampleBindingGeneration>);
    static_assert(!std::is_move_assignable_v<VulkanTextureUploadSampleBindingGeneration>);
    static_assert(std::is_nothrow_destructible_v<VulkanTextureUploadSampleBindingGeneration>);
    static_assert(
        noexcept(resolveVulkanTextureUploadSampleBindingGeneration(std::declval<const VulkanPhysicalDeviceGeneration&>(),
                                                                   std::declval<const VulkanLogicalDeviceGeneration&>(),
                                                                   std::declval<const VulkanTextureUploadDestinationDescription&>(),
                                                                   std::declval<const VulkanTextureUploadSampleBindingDescription&>(),
                                                                   std::declval<const VulkanTextureUploadDestinationGeneration&>())));
    static_assert(noexcept(std::declval<VulkanTextureUploadSampleBindingGeneration&>().reset()));

    const auto description = bindingDescription();
    ensure("the canonical binding is sampler 1:1 at set zero binding zero",
           description.mSampler.mHandle == LLRenderContract::SamplerHandle{ 1, 1 } && description.mDescriptorSetIndex == 0 &&
               description.mBinding == 0 && description.mSampler.mMinFilter == LLRenderContract::Filter::Linear &&
               description.mSampler.mMagFilter == LLRenderContract::Filter::Linear &&
               description.mSampler.mMipFilter == LLRenderContract::MipFilter::Linear &&
               description.mSampler.mAddressU == LLRenderContract::AddressMode::Clamp &&
               description.mSampler.mAddressV == LLRenderContract::AddressMode::Clamp && description.mSampler.mMaxAnisotropy == 1.f &&
               description.mSampler.mLifetime == LLRenderContract::ResourceLifetime::Persistent);

    FakeState       state;
    ScopedFakeState scope(state);
    Resources       resources(state);
    auto            binding = takeBinding(resolveBinding(resources));
    ensure("the owner records the fixed sampler and destination identity",
           binding.samplerResourceHandle() == description.mSampler.mHandle &&
               binding.destinationResourceHandle() == vulkanTextureUploadDestinationDescription().mHandle &&
               binding.expectedRevision() == LLRenderContract::TEXTURE_UPLOAD_REVISION &&
               binding.residentRevision() == LLRenderContract::TEXTURE_UPLOAD_REVISION &&
               binding.residentContentIdentity() == state.mContentIdentity && binding.destinationImageView() == state.mImageView &&
               binding.destinationImageLayout() == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && binding.descriptorSetIndex() == 0 &&
               binding.binding() == 0 && binding.sampler() == state.mSampler &&
               binding.descriptorSetLayout() == state.mDescriptorSetLayout && binding.pipelineLayout() == state.mPipelineLayout &&
               binding.descriptorPool() == state.mDescriptorPool && binding.descriptorSet() == state.mDescriptorSet &&
               binding.matchesDescription(description) &&
               binding.createdFor(resources.mPhysical, resources.mLogical, resources.mDestination) &&
               binding.retainsTextureUploadDestinationGeneration(resources.mDestination));
}

template<>
template<>
void render_vulkan_texture_upload_sample_binding_object::test<2>()
{
    set_test_name("invalid parents, descriptions, and destination states fail before dispatch");

    {
        FakeState       state;
        ScopedFakeState scope(state);
        Resources       resources(state);
        auto            moved_physical = std::move(resources.mPhysical);
        ensureResolutionError(resolveBinding(resources), VulkanTextureUploadSampleBindingResolutionCode::InvalidPhysicalDeviceGeneration);
        ensure("an invalid physical parent resolves no binding command", state.mInstanceLookups.empty());
        resources.mDestination.reset();
        resources.mLogical.reset();
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        Resources       resources(state);
        auto            moved_logical = std::move(resources.mLogical);
        ensureResolutionError(resolveBinding(resources), VulkanTextureUploadSampleBindingResolutionCode::InvalidLogicalDeviceGeneration);
        ensure("an invalid logical parent resolves no binding command", state.mInstanceLookups.empty());
        resources.mDestination.reset();
        moved_logical.reset();
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        Resources       resources(state);
        auto            invalid  = bindingDescription();
        invalid.mSampler.mHandle = {};
        ensureResolutionError(resolveBinding(resources, invalid), VulkanTextureUploadSampleBindingResolutionCode::InvalidDescription);
        ensure("an invalid binding description resolves no command", state.mInstanceLookups.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        Resources       resources(state);
        auto            invalid = vulkanTextureUploadDestinationDescription();
        ++invalid.mExpectedRevision;
        ensureResolutionError(resolveVulkanTextureUploadSampleBindingGeneration(resources.mPhysical,
                                                                                resources.mLogical,
                                                                                invalid,
                                                                                bindingDescription(),
                                                                                resources.mDestination),
                              VulkanTextureUploadSampleBindingResolutionCode::InvalidDescription);
        ensure("an invalid destination description resolves no command", state.mInstanceLookups.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        Resources       resources(state, false);
        ensureResolutionError(resolveBinding(resources), VulkanTextureUploadSampleBindingResolutionCode::DestinationNotResident);
        ensure("an unpublished destination resolves no binding command", state.mInstanceLookups.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        Resources       resources(state, false);
        VulkanTextureUploadDestinationGenerationTestAccess::forcePartialPublication(resources.mDestination,
                                                                                    LLRenderContract::TEXTURE_UPLOAD_REVISION);
        ensureResolutionError(resolveBinding(resources),
                              VulkanTextureUploadSampleBindingResolutionCode::InvalidTextureUploadDestinationGeneration);
        ensure("partial publication is malformed rather than resident", state.mInstanceLookups.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        Resources       resources(state);
        auto            moved_destination = std::move(resources.mDestination);
        ensureResolutionError(resolveBinding(resources),
                              VulkanTextureUploadSampleBindingResolutionCode::InvalidTextureUploadDestinationGeneration);
        ensure("a moved destination resolves no binding command", state.mInstanceLookups.empty());
    }
}

template<>
template<>
void render_vulkan_texture_upload_sample_binding_object::test<3>()
{
    set_test_name("every missing command fails before native object creation");

    struct MissingCase
    {
        const char*                             mName;
        VulkanTextureUploadSampleBindingCommand mCommand;
    };
    constexpr std::array cases{
        MissingCase{ "vkGetDeviceProcAddr", VulkanTextureUploadSampleBindingCommand::GetDeviceProcAddr },
        MissingCase{ "vkCreateSampler", VulkanTextureUploadSampleBindingCommand::CreateSampler },
        MissingCase{ "vkDestroySampler", VulkanTextureUploadSampleBindingCommand::DestroySampler },
        MissingCase{ "vkCreateDescriptorSetLayout", VulkanTextureUploadSampleBindingCommand::CreateDescriptorSetLayout },
        MissingCase{ "vkDestroyDescriptorSetLayout", VulkanTextureUploadSampleBindingCommand::DestroyDescriptorSetLayout },
        MissingCase{ "vkCreatePipelineLayout", VulkanTextureUploadSampleBindingCommand::CreatePipelineLayout },
        MissingCase{ "vkDestroyPipelineLayout", VulkanTextureUploadSampleBindingCommand::DestroyPipelineLayout },
        MissingCase{ "vkCreateDescriptorPool", VulkanTextureUploadSampleBindingCommand::CreateDescriptorPool },
        MissingCase{ "vkDestroyDescriptorPool", VulkanTextureUploadSampleBindingCommand::DestroyDescriptorPool },
        MissingCase{ "vkAllocateDescriptorSets", VulkanTextureUploadSampleBindingCommand::AllocateDescriptorSets },
        MissingCase{ "vkUpdateDescriptorSets", VulkanTextureUploadSampleBindingCommand::UpdateDescriptorSets },
    };
    for (const MissingCase& test_case : cases)
    {
        FakeState       state;
        ScopedFakeState scope(state);
        Resources       resources(state);
        state.mMissingCommand = test_case.mName;
        ensureResolutionError(resolveBinding(resources),
                              VulkanTextureUploadSampleBindingResolutionCode::MissingRequiredCommand,
                              test_case.mCommand);
        ensure("missing dispatch creates and destroys no binding object",
               state.mSamplerRecords.empty() && state.mCalls.empty() && state.mTeardownOrder.empty());
    }
}

template<>
template<>
void render_vulkan_texture_upload_sample_binding_object::test<4>()
{
    set_test_name("successful acquisition emits the exact sampler, layout, pool, set, and write");

    FakeState       state;
    ScopedFakeState scope(state);
    Resources       resources(state);
    auto            binding = takeBinding(resolveBinding(resources));
    ensure("all device commands resolve before the first native mutation", state.mAllCommandsResolvedBeforeCreate);
    ensure("one exact fixed sampler is created", state.mSamplerRecords.size() == 1 && exactSampler(state.mSamplerRecords.front(), state));
    ensure("one exact set-zero binding-zero interface is allocated and written", exactDescriptorInterface(state));
    const std::vector<std::string> expected_calls{ "create-sampler", "create-layout", "create-pipeline-layout",
                                                   "create-pool",    "allocate-set",  "update-set" };
    ensure("native creation and publication use the declared order", state.mCalls == expected_calls);
    ensure("the standalone owner resolves no shader, pipeline, draw, or readback command",
           std::none_of(state.mDeviceLookups.begin(), state.mDeviceLookups.end(),
                        [](const std::string& name)
                        {
                            return name.find("Shader") != std::string::npos || name.find("GraphicsPipeline") != std::string::npos ||
                                   name.find("Cmd") != std::string::npos || name.find("MapMemory") != std::string::npos;
                        }));
}

template<>
template<>
void render_vulkan_texture_upload_sample_binding_object::test<5>()
{
    set_test_name("failed and null create outputs roll back only successful obligations in reverse order");

    enum class Step : std::uint8_t
    {
        Sampler,
        DescriptorSetLayout,
        PipelineLayout,
        DescriptorPool,
        DescriptorSet
    };
    struct FailureCase
    {
        Step                                           mStep;
        bool                                           mNullSuccess;
        VulkanTextureUploadSampleBindingResolutionCode mCode;
        VulkanTextureUploadSampleBindingCommand        mCommand;
        std::vector<std::string>                       mTeardown;
    };
    const std::array cases{
        FailureCase{ Step::Sampler,
                     false,
                     VulkanTextureUploadSampleBindingResolutionCode::SamplerCreationFailure,
                     VulkanTextureUploadSampleBindingCommand::CreateSampler,
                     {} },
        FailureCase{ Step::Sampler,
                     true,
                     VulkanTextureUploadSampleBindingResolutionCode::NullSamplerOnSuccess,
                     VulkanTextureUploadSampleBindingCommand::CreateSampler,
                     {} },
        FailureCase{ Step::DescriptorSetLayout,
                     false,
                     VulkanTextureUploadSampleBindingResolutionCode::DescriptorSetLayoutCreationFailure,
                     VulkanTextureUploadSampleBindingCommand::CreateDescriptorSetLayout,
                     { "destroy-sampler" } },
        FailureCase{ Step::DescriptorSetLayout,
                     true,
                     VulkanTextureUploadSampleBindingResolutionCode::NullDescriptorSetLayoutOnSuccess,
                     VulkanTextureUploadSampleBindingCommand::CreateDescriptorSetLayout,
                     { "destroy-sampler" } },
        FailureCase{ Step::PipelineLayout,
                     false,
                     VulkanTextureUploadSampleBindingResolutionCode::PipelineLayoutCreationFailure,
                     VulkanTextureUploadSampleBindingCommand::CreatePipelineLayout,
                     { "destroy-layout", "destroy-sampler" } },
        FailureCase{ Step::PipelineLayout,
                     true,
                     VulkanTextureUploadSampleBindingResolutionCode::NullPipelineLayoutOnSuccess,
                     VulkanTextureUploadSampleBindingCommand::CreatePipelineLayout,
                     { "destroy-layout", "destroy-sampler" } },
        FailureCase{ Step::DescriptorPool,
                     false,
                     VulkanTextureUploadSampleBindingResolutionCode::DescriptorPoolCreationFailure,
                     VulkanTextureUploadSampleBindingCommand::CreateDescriptorPool,
                     { "destroy-pipeline-layout", "destroy-layout", "destroy-sampler" } },
        FailureCase{ Step::DescriptorPool,
                     true,
                     VulkanTextureUploadSampleBindingResolutionCode::NullDescriptorPoolOnSuccess,
                     VulkanTextureUploadSampleBindingCommand::CreateDescriptorPool,
                     { "destroy-pipeline-layout", "destroy-layout", "destroy-sampler" } },
        FailureCase{ Step::DescriptorSet,
                     false,
                     VulkanTextureUploadSampleBindingResolutionCode::DescriptorSetAllocationFailure,
                     VulkanTextureUploadSampleBindingCommand::AllocateDescriptorSets,
                     { "destroy-pool", "destroy-pipeline-layout", "destroy-layout", "destroy-sampler" } },
        FailureCase{ Step::DescriptorSet,
                     true,
                     VulkanTextureUploadSampleBindingResolutionCode::NullDescriptorSetOnSuccess,
                     VulkanTextureUploadSampleBindingCommand::AllocateDescriptorSets,
                     { "destroy-pool", "destroy-pipeline-layout", "destroy-layout", "destroy-sampler" } },
    };

    for (const FailureCase& test_case : cases)
    {
        FakeState       state;
        ScopedFakeState scope(state);
        Resources       resources(state);
        const VkResult  result = test_case.mNullSuccess ? VK_SUCCESS : VK_ERROR_OUT_OF_HOST_MEMORY;
        if (test_case.mStep == Step::Sampler)
        {
            state.mCreateSamplerResult = result;
            state.mCreateSamplerOutput = test_case.mNullSuccess ? VK_NULL_HANDLE : fakeHandle<VkSampler>(0xdead0001);
        }
        else if (test_case.mStep == Step::DescriptorSetLayout)
        {
            state.mCreateDescriptorSetLayoutResult = result;
            state.mCreateDescriptorSetLayoutOutput =
                test_case.mNullSuccess ? VK_NULL_HANDLE : fakeHandle<VkDescriptorSetLayout>(0xdead0002);
        }
        else if (test_case.mStep == Step::PipelineLayout)
        {
            state.mCreatePipelineLayoutResult = result;
            state.mCreatePipelineLayoutOutput = test_case.mNullSuccess ? VK_NULL_HANDLE : fakeHandle<VkPipelineLayout>(0xdead0003);
        }
        else if (test_case.mStep == Step::DescriptorPool)
        {
            state.mCreateDescriptorPoolResult = result;
            state.mCreateDescriptorPoolOutput = test_case.mNullSuccess ? VK_NULL_HANDLE : fakeHandle<VkDescriptorPool>(0xdead0004);
        }
        else
        {
            state.mAllocateDescriptorSetResult = result;
            state.mAllocateDescriptorSetOutput = test_case.mNullSuccess ? VK_NULL_HANDLE : fakeHandle<VkDescriptorSet>(0xdead0005);
        }

        ensureResolutionError(resolveBinding(resources),
                              test_case.mCode,
                              test_case.mCommand,
                              test_case.mNullSuccess ? VK_SUCCESS : VK_ERROR_OUT_OF_HOST_MEMORY);
        ensure("each failed or null step rolls back the prior successful obligations only",
               state.mTeardownOrder == test_case.mTeardown && !state.mDestroyedWithWrongDevice);
        ensure("poisoned failed output bits are never destroyed",
               std::none_of(state.mDestroyedSamplers.begin(), state.mDestroyedSamplers.end(),
                            [](VkSampler handle) { return handle == fakeHandle<VkSampler>(0xdead0001); }) &&
                   std::none_of(state.mDestroyedDescriptorSetLayouts.begin(),
                                state.mDestroyedDescriptorSetLayouts.end(),
                                [](VkDescriptorSetLayout handle) { return handle == fakeHandle<VkDescriptorSetLayout>(0xdead0002); }) &&
                   std::none_of(state.mDestroyedPipelineLayouts.begin(), state.mDestroyedPipelineLayouts.end(),
                                [](VkPipelineLayout handle) { return handle == fakeHandle<VkPipelineLayout>(0xdead0003); }) &&
                   std::none_of(state.mDestroyedDescriptorPools.begin(), state.mDestroyedDescriptorPools.end(),
                                [](VkDescriptorPool handle) { return handle == fakeHandle<VkDescriptorPool>(0xdead0004); }));
    }
}

template<>
template<>
void render_vulkan_texture_upload_sample_binding_object::test<6>()
{
    set_test_name("resolution revalidates the destination after every resolver and native callback");

    struct MutationCase
    {
        const char* mPoint;
        std::size_t mDestroyedObligationCount;
    };
    constexpr std::array           cases{ MutationCase{ "resolve:vkGetDeviceProcAddr", 0 },
                                MutationCase{ "resolve:vkCreateSampler", 0 },
                                MutationCase{ "resolve:vkDestroySampler", 0 },
                                MutationCase{ "resolve:vkCreateDescriptorSetLayout", 0 },
                                MutationCase{ "resolve:vkDestroyDescriptorSetLayout", 0 },
                                MutationCase{ "resolve:vkCreatePipelineLayout", 0 },
                                MutationCase{ "resolve:vkDestroyPipelineLayout", 0 },
                                MutationCase{ "resolve:vkCreateDescriptorPool", 0 },
                                MutationCase{ "resolve:vkDestroyDescriptorPool", 0 },
                                MutationCase{ "resolve:vkAllocateDescriptorSets", 0 },
                                MutationCase{ "resolve:vkUpdateDescriptorSets", 0 },
                                MutationCase{ "create-sampler", 1 },
                                MutationCase{ "create-layout", 2 },
                                MutationCase{ "create-pipeline-layout", 3 },
                                MutationCase{ "create-pool", 4 },
                                MutationCase{ "allocate-set", 4 },
                                MutationCase{ "update-set", 4 } };
    const std::vector<std::string> full_teardown{ "destroy-pool", "destroy-pipeline-layout", "destroy-layout", "destroy-sampler" };
    for (const MutationCase& test_case : cases)
    {
        FakeState       state;
        ScopedFakeState scope(state);
        Resources       resources(state);
        state.mInvalidateAt      = test_case.mPoint;
        state.mDestinationToMove = &resources.mDestination;
        ensureResolutionError(resolveBinding(resources), VulkanTextureUploadSampleBindingResolutionCode::ParentGenerationChanged);
        const auto expected_begin = full_teardown.end() - static_cast<std::ptrdiff_t>(test_case.mDestroyedObligationCount);
        const std::vector<std::string> expected_teardown(expected_begin, full_teardown.end());
        ensure("callback invalidation never leaves a binding owner published",
               state.mInvalidateAt.empty() && state.mDescriptorWriteRecords.size() <= 1 && state.mTeardownOrder == expected_teardown &&
                   state.mDestroyedDescriptorPools.size() == (test_case.mDestroyedObligationCount == 4 ? 1 : 0) &&
                   state.mDestroyedPipelineLayouts.size() == (test_case.mDestroyedObligationCount >= 3 ? 1 : 0) &&
                   state.mDestroyedDescriptorSetLayouts.size() == (test_case.mDestroyedObligationCount >= 2 ? 1 : 0) &&
                   state.mDestroyedSamplers.size() == (test_case.mDestroyedObligationCount >= 1 ? 1 : 0) &&
                   !state.mDestroyedWithWrongDevice);
    }

    {
        FakeState       state;
        ScopedFakeState scope(state);
        Resources       resources(state);
        state.mInvalidateAt  = "create-pool";
        state.mLogicalToMove = &resources.mLogical;
        ensureResolutionError(resolveBinding(resources), VulkanTextureUploadSampleBindingResolutionCode::ParentGenerationChanged);
        ensure("logical-parent mutation is detected and rolls back acquired children",
               state.mTeardownOrder ==
                       std::vector<std::string>{ "destroy-pool", "destroy-pipeline-layout", "destroy-layout", "destroy-sampler" } &&
                   !state.mDestroyedWithWrongDevice);
    }

    FakeState       state;
    ScopedFakeState scope(state);
    Resources       resources(state);
    auto            request    = bindingDescription();
    const auto      original   = request;
    state.mInvalidateAt        = "update-set";
    state.mDescriptionToMutate = &request;
    ensureResolutionError(resolveBinding(resources, request), VulkanTextureUploadSampleBindingResolutionCode::ParentGenerationChanged);
    ensure("void descriptor update is followed by a freshness check and complete rollback",
           request != original && state.mDescriptorWriteRecords.size() == 1 &&
               state.mTeardownOrder ==
                   std::vector<std::string>{ "destroy-pool", "destroy-pipeline-layout", "destroy-layout", "destroy-sampler" });

    FakeState       destination_state;
    ScopedFakeState destination_scope(destination_state);
    Resources       destination_resources(destination_state);
    auto            destination_request               = vulkanTextureUploadDestinationDescription();
    destination_state.mInvalidateAt                   = "update-set";
    destination_state.mDestinationDescriptionToMutate = &destination_request;
    ensureResolutionError(resolveVulkanTextureUploadSampleBindingGeneration(destination_resources.mPhysical,
                                                                            destination_resources.mLogical,
                                                                            destination_request,
                                                                            bindingDescription(),
                                                                            destination_resources.mDestination),
                          VulkanTextureUploadSampleBindingResolutionCode::ParentGenerationChanged);
    ensure("the destination request is also snapshotted across the void update",
           destination_request != vulkanTextureUploadDestinationDescription() &&
               destination_state.mTeardownOrder ==
                   std::vector<std::string>{ "destroy-pool", "destroy-pipeline-layout", "destroy-layout", "destroy-sampler" });
}

template<>
template<>
void render_vulkan_texture_upload_sample_binding_object::test<7>()
{
    set_test_name("move and idempotent reset transfer each successful obligation exactly once");

    FakeState       state;
    ScopedFakeState scope(state);
    Resources       resources(state);
    auto            first = takeBinding(resolveBinding(resources));
    auto            moved = VulkanTextureUploadSampleBindingGeneration(std::move(first));
    ensure("move leaves the source inert and preserves the destination retention",
           bindingAccessorsInert(first) && moved.createdFor(resources.mPhysical, resources.mLogical, resources.mDestination));
    state.mBindingToReenter = &moved;
    state.mReenterTeardown  = true;
    moved.reset();
    moved.reset();
    ensure("reset clears the public state before all reverse-order callbacks",
           bindingAccessorsInert(moved) && state.mResetReentryAttempts == 4 && state.mResetReentryObservedInert &&
               state.mTeardownOrder ==
                   std::vector<std::string>{ "destroy-pool", "destroy-pipeline-layout", "destroy-layout", "destroy-sampler" } &&
               state.mDestroyedDescriptorPools == std::vector<VkDescriptorPool>{ state.mDescriptorPool } &&
               state.mDestroyedPipelineLayouts == std::vector<VkPipelineLayout>{ state.mPipelineLayout } &&
               state.mDestroyedDescriptorSetLayouts == std::vector<VkDescriptorSetLayout>{ state.mDescriptorSetLayout } &&
               state.mDestroyedSamplers == std::vector<VkSampler>{ state.mSampler } && !state.mDestroyedWithWrongDevice);
}

template<>
template<>
void render_vulkan_texture_upload_sample_binding_object::test<8>()
{
    set_test_name("equal native handle bits remain four distinct ownership obligations");

    FakeState                state;
    ScopedFakeState          scope(state);
    Resources                resources(state);
    constexpr std::uintptr_t equal_bits    = 0xeeee;
    state.mCreateSamplerOutput             = fakeHandle<VkSampler>(equal_bits);
    state.mCreateDescriptorSetLayoutOutput = fakeHandle<VkDescriptorSetLayout>(equal_bits);
    state.mCreatePipelineLayoutOutput      = fakeHandle<VkPipelineLayout>(equal_bits);
    state.mCreateDescriptorPoolOutput      = fakeHandle<VkDescriptorPool>(equal_bits);
    state.mAllocateDescriptorSetOutput     = fakeHandle<VkDescriptorSet>(equal_bits);
    auto binding                           = takeBinding(resolveBinding(resources));
    binding.reset();
    ensure("each typed owner destroys once even when its handle bits match",
           state.mDestroyedSamplers.size() == 1 && state.mDestroyedDescriptorSetLayouts.size() == 1 &&
               state.mDestroyedPipelineLayouts.size() == 1 && state.mDestroyedDescriptorPools.size() == 1 &&
               state.mTeardownOrder ==
                   std::vector<std::string>{ "destroy-pool", "destroy-pipeline-layout", "destroy-layout", "destroy-sampler" });
}

template<>
template<>
void render_vulkan_texture_upload_sample_binding_object::test<9>()
{
    set_test_name("the binding retains only the destination and detects illegal parent detachment");

    FakeState       state;
    ScopedFakeState scope(state);
    Resources       resources(state);
    auto            binding           = takeBinding(resolveBinding(resources));
    auto            moved_destination = std::move(resources.mDestination);
    ensure("detaching the destination invalidates authentication without discarding owned children",
           !binding.createdFor(resources.mPhysical, resources.mLogical, resources.mDestination) &&
               binding.retainsTextureUploadDestinationGeneration(resources.mDestination) && binding.sampler() == state.mSampler &&
               binding.descriptorSet() == state.mDescriptorSet);
    binding.reset();
    ensure("the binding can retire independently after detachment",
           bindingAccessorsInert(binding) && state.mTeardownOrder == std::vector<std::string>{ "destroy-pool", "destroy-pipeline-layout",
                                                                                               "destroy-layout", "destroy-sampler" });
}

template<>
template<>
void render_vulkan_texture_upload_sample_binding_object::test<10>()
{
    set_test_name("destruction retires every native child through the reset path");

    FakeState       state;
    ScopedFakeState scope(state);
    Resources       resources(state);
    {
        auto binding = takeBinding(resolveBinding(resources));
        ensure("the scoped owner is live before destruction",
               binding.createdFor(resources.mPhysical, resources.mLogical, resources.mDestination));
    }
    ensure("scope exit destroys the pool, pipeline layout, set layout, and sampler once in reverse order",
           state.mDestroyedDescriptorPools == std::vector<VkDescriptorPool>{ state.mDescriptorPool } &&
               state.mDestroyedPipelineLayouts == std::vector<VkPipelineLayout>{ state.mPipelineLayout } &&
               state.mDestroyedDescriptorSetLayouts == std::vector<VkDescriptorSetLayout>{ state.mDescriptorSetLayout } &&
               state.mDestroyedSamplers == std::vector<VkSampler>{ state.mSampler } &&
               state.mTeardownOrder ==
                   std::vector<std::string>{ "destroy-pool", "destroy-pipeline-layout", "destroy-layout", "destroy-sampler" });
}

} // namespace tut
