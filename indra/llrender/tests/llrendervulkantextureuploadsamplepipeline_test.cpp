/**
 * @file llrendervulkantextureuploadsamplepipeline_test.cpp
 * @brief Focused tests for sampled streamed-texture pipeline ownership.
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

#include "llrendervulkantextureuploadsamplepipeline.h"
#include "lltut.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
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
        constexpr VkFormatFeatureFlags format_features = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
                                                         VK_FORMAT_FEATURE_TRANSFER_DST_BIT | VK_FORMAT_FEATURE_BLIT_SRC_BIT |
                                                         VK_FORMAT_FEATURE_BLIT_DST_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        VkImageFormatProperties image_properties{};
        image_properties.maxExtent       = { 64, 64, 1 };
        image_properties.maxMipLevels    = 8;
        image_properties.maxArrayLayers  = 4;
        image_properties.sampleCounts    = VK_SAMPLE_COUNT_1_BIT;
        image_properties.maxResourceSize = 1ULL << 20;
        const VkMemoryRequirements memory_requirements{ 4096, 256, 1 };
        return VulkanTextureUploadDestinationGeneration(physical,
                                                        logical,
                                                        description,
                                                        format_features,
                                                        image_properties,
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

    static bool markResident(VulkanTextureUploadDestinationGeneration& destination,
                             std::uint64_t                             revision,
                             std::uint64_t                             content_identity) noexcept
    {
        return destination.markResident(revision, content_identity, LLRenderContract::ImageState::ShaderRead);
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

struct ShaderModuleRecord
{
    VkStructureType            mStructureType = VK_STRUCTURE_TYPE_MAX_ENUM;
    const void*                mNext          = nullptr;
    VkShaderModuleCreateFlags  mFlags         = 0;
    std::vector<std::uint32_t> mCode;
};

struct GraphicsPipelineRecord
{
    VkPipelineCache                                mCache           = VK_NULL_HANDLE;
    std::uint32_t                                  mCreateInfoCount = 0;
    VkStructureType                                mStructureType   = VK_STRUCTURE_TYPE_MAX_ENUM;
    const void*                                    mNext            = nullptr;
    VkPipelineCreateFlags                          mFlags           = 0;
    std::uint32_t                                  mStageCount      = 0;
    std::array<VkPipelineShaderStageCreateInfo, 2> mStages{};
    std::array<std::string, 2>                     mEntryPoints;
    VkPipelineVertexInputStateCreateInfo           mVertexInput{};
    std::vector<VkVertexInputBindingDescription>   mVertexBindings;
    std::vector<VkVertexInputAttributeDescription> mVertexAttributes;
    VkPipelineInputAssemblyStateCreateInfo         mInputAssembly{};
    bool                                           mTessellationNull = false;
    VkPipelineViewportStateCreateInfo              mViewport{};
    VkPipelineRasterizationStateCreateInfo         mRasterization{};
    VkPipelineMultisampleStateCreateInfo           mMultisample{};
    bool                                           mDepthStencilNull = false;
    VkPipelineColorBlendStateCreateInfo            mColorBlend{};
    VkPipelineColorBlendAttachmentState            mColorAttachment{};
    VkPipelineDynamicStateCreateInfo               mDynamic{};
    std::array<VkDynamicState, 2>                  mDynamicStates{};
    VkPipelineLayout                               mLayout            = VK_NULL_HANDLE;
    VkRenderPass                                   mRenderPass        = VK_NULL_HANDLE;
    std::uint32_t                                  mSubpass           = 0;
    VkPipeline                                     mBasePipeline      = VK_NULL_HANDLE;
    std::int32_t                                   mBasePipelineIndex = 0;
    bool                                           mAllocatorNull     = false;
};

struct FakeState
{
    VkInstance            mInstance            = fakeHandle<VkInstance>(0x1000);
    VkSurfaceKHR          mSurface             = fakeHandle<VkSurfaceKHR>(0x2000);
    VkPhysicalDevice      mPhysicalDevice      = fakeHandle<VkPhysicalDevice>(0x3000);
    VkDevice              mDevice              = fakeHandle<VkDevice>(0x4000);
    VkQueue               mQueue               = fakeHandle<VkQueue>(0x5000);
    VkSwapchainKHR        mSwapchain           = fakeHandle<VkSwapchainKHR>(0x6000);
    VkImage               mDestinationImage    = fakeHandle<VkImage>(0x7000);
    VkDeviceMemory        mDestinationMemory   = fakeHandle<VkDeviceMemory>(0x7100);
    VkImageView           mDestinationView     = fakeHandle<VkImageView>(0x7200);
    VkSampler             mSampler             = fakeHandle<VkSampler>(0x7300);
    VkDescriptorSetLayout mDescriptorSetLayout = fakeHandle<VkDescriptorSetLayout>(0x7400);
    VkPipelineLayout      mPipelineLayout      = fakeHandle<VkPipelineLayout>(0x7500);
    VkDescriptorPool      mDescriptorPool      = fakeHandle<VkDescriptorPool>(0x7600);
    VkDescriptorSet       mDescriptorSet       = fakeHandle<VkDescriptorSet>(0x7700);
    VkRenderPass          mRenderPass          = fakeHandle<VkRenderPass>(0x8000);
    VkPipeline            mPipeline            = fakeHandle<VkPipeline>(0x9000);
    std::uint32_t         mQueueFamily         = 2;
    std::uint64_t         mContentIdentity     = 0x123456789abcdef0ULL;

    VkSurfaceCapabilitiesKHR          mCapabilities{ 2,
                                            0,
                                                     { std::numeric_limits<std::uint32_t>::max(), std::numeric_limits<std::uint32_t>::max() },
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
    std::array<VkImage, 3> mSwapchainImages{ fakeHandle<VkImage>(0xa100), fakeHandle<VkImage>(0xa200), fakeHandle<VkImage>(0xa300) };

    bool                     mOwnerPhase = false;
    std::string              mMissingCommand;
    std::string              mInvalidateAt;
    std::vector<std::string> mInstanceLookups;
    std::vector<std::string> mDeviceLookups;
    std::vector<std::string> mMutationOrder;
    std::vector<std::string> mDestroyOrder;
    bool                     mAllCommandsResolvedBeforeMutation = false;
    bool                     mDestroyArgumentsExact             = true;

    std::vector<VkImageView>        mCreatedSwapchainViews;
    std::size_t                     mFramebufferCreateCount     = 0;
    std::size_t                     mPipelineLayoutCreateCount  = 0;
    std::size_t                     mPipelineLayoutDestroyCount = 0;
    std::vector<VkResult>           mShaderResults;
    std::vector<VkShaderModule>     mShaderOutputs;
    std::vector<ShaderModuleRecord> mShaderRecords;
    VkResult                        mPipelineResult = VK_SUCCESS;
    VkPipeline                      mPipelineOutput = mPipeline;
    GraphicsPipelineRecord          mPipelineRecord;
    std::vector<VkShaderModule>     mDestroyedShaders;
    std::vector<VkPipeline>         mDestroyedPipelines;

    VulkanTextureUploadDestinationDescription*         mDestinationDescriptionToMutate = nullptr;
    VulkanTextureUploadSampleBindingDescription*       mBindingDescriptionToMutate     = nullptr;
    VulkanTextureUploadSamplePipelineDescription*      mPipelineDescriptionToMutate    = nullptr;
    VulkanTextureUploadSampleBindingGeneration*        mBindingToReset                 = nullptr;
    VulkanSwapchainPresentationTargetGeneration*       mTargetToReset                  = nullptr;
    VulkanTextureUploadSamplePipelineGeneration*       mPipelineToReenter              = nullptr;
    const VulkanPhysicalDeviceGeneration*              mPhysicalForAccess              = nullptr;
    const VulkanLogicalDeviceGeneration*               mLogicalForAccess               = nullptr;
    const VulkanTextureUploadDestinationGeneration*    mDestinationForAccess           = nullptr;
    const VulkanTextureUploadSampleBindingGeneration*  mBindingForAccess               = nullptr;
    const VulkanSwapchainConfigurationGeneration*      mConfigurationForAccess         = nullptr;
    const VulkanSwapchainGeneration*                   mSwapchainForAccess             = nullptr;
    const VulkanSwapchainImagesGeneration*             mImagesForAccess                = nullptr;
    const VulkanSwapchainPresentationTargetGeneration* mTargetForAccess                = nullptr;
    bool                                               mReenterDestroy                 = false;
    std::size_t                                        mReentryAttempts                = 0;
    bool                                               mReentryObservedInert           = true;

    std::array<VkShaderModule, 2> defaultShaderModules() const noexcept
    {
        return { fakeHandle<VkShaderModule>(0x9100), fakeHandle<VkShaderModule>(0x9200) };
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

bool pipelineAccessorsInert(const VulkanTextureUploadSamplePipelineGeneration& pipeline) noexcept
{
    bool inert = !pipeline.pipelineResourceHandle() && pipeline.pipelineLayout() == VK_NULL_HANDLE &&
                 pipeline.pipeline() == VK_NULL_HANDLE && !pipeline.matchesDescription(vulkanTextureUploadSamplePipelineDescription());
    if (gFakeState && gFakeState->mPhysicalForAccess && gFakeState->mLogicalForAccess && gFakeState->mDestinationForAccess &&
        gFakeState->mBindingForAccess && gFakeState->mConfigurationForAccess && gFakeState->mSwapchainForAccess &&
        gFakeState->mImagesForAccess && gFakeState->mTargetForAccess)
    {
        inert &= !pipeline.createdFor(*gFakeState->mPhysicalForAccess,
                                      *gFakeState->mLogicalForAccess,
                                      *gFakeState->mDestinationForAccess,
                                      *gFakeState->mBindingForAccess,
                                      *gFakeState->mConfigurationForAccess,
                                      *gFakeState->mSwapchainForAccess,
                                      *gFakeState->mImagesForAccess,
                                      *gFakeState->mTargetForAccess) &&
                 !pipeline.retainsTextureUploadSampleBindingGeneration(*gFakeState->mBindingForAccess);
    }
    return inert;
}

void attemptResetReentry() noexcept
{
    if (!gFakeState || !gFakeState->mReenterDestroy || !gFakeState->mPipelineToReenter)
    {
        return;
    }
    ++gFakeState->mReentryAttempts;
    gFakeState->mReentryObservedInert &= pipelineAccessorsInert(*gFakeState->mPipelineToReenter);
    gFakeState->mPipelineToReenter->reset();
    gFakeState->mReentryObservedInert &= pipelineAccessorsInert(*gFakeState->mPipelineToReenter);
}

void invalidateAt(std::string_view point) noexcept
{
    if (!gFakeState || gFakeState->mInvalidateAt != point)
    {
        return;
    }
    gFakeState->mInvalidateAt.clear();
    if (gFakeState->mDestinationDescriptionToMutate)
    {
        ++gFakeState->mDestinationDescriptionToMutate->mExpectedRevision;
        gFakeState->mDestinationDescriptionToMutate = nullptr;
    }
    if (gFakeState->mBindingDescriptionToMutate)
    {
        ++gFakeState->mBindingDescriptionToMutate->mBinding;
        gFakeState->mBindingDescriptionToMutate = nullptr;
    }
    if (gFakeState->mPipelineDescriptionToMutate)
    {
        ++gFakeState->mPipelineDescriptionToMutate->mHandle.mGeneration;
        gFakeState->mPipelineDescriptionToMutate = nullptr;
    }
    if (gFakeState->mBindingToReset)
    {
        gFakeState->mBindingToReset->reset();
        gFakeState->mBindingToReset = nullptr;
    }
    if (gFakeState->mTargetToReset)
    {
        gFakeState->mTargetToReset->reset();
        gFakeState->mTargetToReset = nullptr;
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
    *properties                                 = {};
    properties->apiVersion                      = VK_API_VERSION_1_1;
    properties->limits.maxFramebufferWidth      = 4096;
    properties->limits.maxFramebufferHeight     = 2160;
    properties->limits.maxFramebufferLayers     = 1;
    properties->limits.maxViewportDimensions[0] = 4096;
    properties->limits.maxViewportDimensions[1] = 4096;
    properties->limits.viewportBoundsRange[0]   = -8192.f;
    properties->limits.viewportBoundsRange[1]   = 8191.f;
    std::strncpy(properties->deviceName, "sample-pipeline-fake", VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 1);
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

VKAPI_ATTR VkResult VKAPI_CALL fakeGetSurfaceFormats(VkPhysicalDevice    physical_device,
                                                     VkSurfaceKHR        surface,
                                                     std::uint32_t*      count,
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

VKAPI_ATTR VkResult VKAPI_CALL fakeGetSurfacePresentModes(VkPhysicalDevice  physical_device,
                                                          VkSurfaceKHR      surface,
                                                          std::uint32_t*    count,
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
    if (!images)
    {
        *count = static_cast<std::uint32_t>(gFakeState->mSwapchainImages.size());
        return VK_SUCCESS;
    }
    const std::uint32_t written = std::min(*count, static_cast<std::uint32_t>(gFakeState->mSwapchainImages.size()));
    std::copy_n(gFakeState->mSwapchainImages.begin(), written, images);
    *count = written;
    return written == gFakeState->mSwapchainImages.size() ? VK_SUCCESS : VK_INCOMPLETE;
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
    *image_view = fakeHandle<VkImageView>(0xb100 + gFakeState->mCreatedSwapchainViews.size() * 0x100);
    gFakeState->mCreatedSwapchainViews.push_back(*image_view);
    return VK_SUCCESS;
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

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateRenderPass(VkDevice device,
                                                    const VkRenderPassCreateInfo*,
                                                    const VkAllocationCallbacks*,
                                                    VkRenderPass* render_pass) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || !render_pass)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    *render_pass = gFakeState->mRenderPass;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyRenderPass(VkDevice, VkRenderPass, const VkAllocationCallbacks*) noexcept
{
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateFramebuffer(VkDevice device,
                                                     const VkFramebufferCreateInfo*,
                                                     const VkAllocationCallbacks*,
                                                     VkFramebuffer* framebuffer) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || !framebuffer)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    *framebuffer = fakeHandle<VkFramebuffer>(0xc100 + gFakeState->mFramebufferCreateCount * 0x100);
    ++gFakeState->mFramebufferCreateCount;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyFramebuffer(VkDevice, VkFramebuffer, const VkAllocationCallbacks*) noexcept
{
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateSampler(VkDevice device,
                                                 const VkSamplerCreateInfo*,
                                                 const VkAllocationCallbacks*,
                                                 VkSampler* sampler) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || !sampler)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    *sampler = gFakeState->mSampler;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroySampler(VkDevice, VkSampler, const VkAllocationCallbacks*) noexcept
{
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateDescriptorSetLayout(VkDevice device,
                                                             const VkDescriptorSetLayoutCreateInfo*,
                                                             const VkAllocationCallbacks*,
                                                             VkDescriptorSetLayout* layout) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || !layout)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    *layout = gFakeState->mDescriptorSetLayout;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyDescriptorSetLayout(VkDevice, VkDescriptorSetLayout, const VkAllocationCallbacks*) noexcept
{
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreatePipelineLayout(VkDevice device,
                                                        const VkPipelineLayoutCreateInfo*,
                                                        const VkAllocationCallbacks*,
                                                        VkPipelineLayout* layout) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || !layout)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    ++gFakeState->mPipelineLayoutCreateCount;
    *layout = gFakeState->mPipelineLayout;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyPipelineLayout(VkDevice, VkPipelineLayout, const VkAllocationCallbacks*) noexcept
{
    if (gFakeState)
    {
        ++gFakeState->mPipelineLayoutDestroyCount;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateDescriptorPool(VkDevice device,
                                                        const VkDescriptorPoolCreateInfo*,
                                                        const VkAllocationCallbacks*,
                                                        VkDescriptorPool* pool) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || !pool)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    *pool = gFakeState->mDescriptorPool;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyDescriptorPool(VkDevice, VkDescriptorPool, const VkAllocationCallbacks*) noexcept
{
}

VKAPI_ATTR VkResult VKAPI_CALL fakeAllocateDescriptorSets(VkDevice device,
                                                          const VkDescriptorSetAllocateInfo*,
                                                          VkDescriptorSet* descriptor_set) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || !descriptor_set)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    *descriptor_set = gFakeState->mDescriptorSet;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
    fakeUpdateDescriptorSets(VkDevice, std::uint32_t, const VkWriteDescriptorSet*, std::uint32_t, const VkCopyDescriptorSet*) noexcept
{
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateShaderModule(VkDevice                        device,
                                                      const VkShaderModuleCreateInfo* create_info,
                                                      const VkAllocationCallbacks*    allocator,
                                                      VkShaderModule*                 shader_module) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || !create_info || !shader_module || !create_info->pCode ||
        create_info->codeSize % sizeof(std::uint32_t) != 0)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    gFakeState->mAllCommandsResolvedBeforeMutation =
        gFakeState->mInstanceLookups == std::vector<std::string>{ "vkGetDeviceProcAddr" } &&
        gFakeState->mDeviceLookups ==
            std::vector<std::string>{ "vkCreateShaderModule", "vkDestroyShaderModule", "vkCreateGraphicsPipelines", "vkDestroyPipeline" };

    ShaderModuleRecord record;
    record.mStructureType = create_info->sType;
    record.mNext          = create_info->pNext;
    record.mFlags         = create_info->flags;
    record.mCode.assign(create_info->pCode, create_info->pCode + create_info->codeSize / sizeof(std::uint32_t));
    gFakeState->mShaderRecords.push_back(std::move(record));
    const std::size_t index = gFakeState->mShaderRecords.size() - 1;
    gFakeState->mMutationOrder.emplace_back(index == 0 ? "vertex" : "fragment");
    gFakeState->mDestroyArgumentsExact &= allocator == nullptr;
    const auto default_modules = gFakeState->defaultShaderModules();
    *shader_module             = index < gFakeState->mShaderOutputs.size() ? gFakeState->mShaderOutputs[index] : default_modules[index % 2];
    invalidateAt(index == 0 ? "create-vertex" : "create-fragment");
    return index < gFakeState->mShaderResults.size() ? gFakeState->mShaderResults[index] : VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyShaderModule(VkDevice device, VkShaderModule shader, const VkAllocationCallbacks* allocator) noexcept
{
    if (!gFakeState)
    {
        return;
    }
    gFakeState->mDestroyedShaders.push_back(shader);
    gFakeState->mDestroyOrder.emplace_back("shader");
    gFakeState->mDestroyArgumentsExact &= device == gFakeState->mDevice && allocator == nullptr;
    invalidateAt("destroy-shader");
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateGraphicsPipelines(VkDevice                            device,
                                                           VkPipelineCache                     cache,
                                                           std::uint32_t                       create_info_count,
                                                           const VkGraphicsPipelineCreateInfo* create_infos,
                                                           const VkAllocationCallbacks*        allocator,
                                                           VkPipeline*                         pipelines) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || create_info_count != 1 || !create_infos || !pipelines)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    const VkGraphicsPipelineCreateInfo& info   = create_infos[0];
    GraphicsPipelineRecord&             record = gFakeState->mPipelineRecord;
    record.mCache                              = cache;
    record.mCreateInfoCount                    = create_info_count;
    record.mStructureType                      = info.sType;
    record.mNext                               = info.pNext;
    record.mFlags                              = info.flags;
    record.mStageCount                         = info.stageCount;
    record.mAllocatorNull                      = allocator == nullptr;
    if (info.stageCount == 2 && info.pStages)
    {
        std::copy_n(info.pStages, 2, record.mStages.begin());
        record.mEntryPoints[0] = info.pStages[0].pName ? info.pStages[0].pName : "";
        record.mEntryPoints[1] = info.pStages[1].pName ? info.pStages[1].pName : "";
    }
    if (info.pVertexInputState)
    {
        record.mVertexInput = *info.pVertexInputState;
        if (info.pVertexInputState->vertexBindingDescriptionCount != 0 && info.pVertexInputState->pVertexBindingDescriptions)
        {
            record.mVertexBindings.assign(info.pVertexInputState->pVertexBindingDescriptions,
                                          info.pVertexInputState->pVertexBindingDescriptions +
                                              info.pVertexInputState->vertexBindingDescriptionCount);
            record.mVertexInput.pVertexBindingDescriptions = record.mVertexBindings.data();
        }
        if (info.pVertexInputState->vertexAttributeDescriptionCount != 0 && info.pVertexInputState->pVertexAttributeDescriptions)
        {
            record.mVertexAttributes.assign(info.pVertexInputState->pVertexAttributeDescriptions,
                                            info.pVertexInputState->pVertexAttributeDescriptions +
                                                info.pVertexInputState->vertexAttributeDescriptionCount);
            record.mVertexInput.pVertexAttributeDescriptions = record.mVertexAttributes.data();
        }
    }
    if (info.pInputAssemblyState)
    {
        record.mInputAssembly = *info.pInputAssemblyState;
    }
    record.mTessellationNull = info.pTessellationState == nullptr;
    if (info.pViewportState)
    {
        record.mViewport = *info.pViewportState;
    }
    if (info.pRasterizationState)
    {
        record.mRasterization = *info.pRasterizationState;
    }
    if (info.pMultisampleState)
    {
        record.mMultisample = *info.pMultisampleState;
    }
    record.mDepthStencilNull = info.pDepthStencilState == nullptr;
    if (info.pColorBlendState)
    {
        record.mColorBlend = *info.pColorBlendState;
        if (info.pColorBlendState->attachmentCount == 1 && info.pColorBlendState->pAttachments)
        {
            record.mColorAttachment = info.pColorBlendState->pAttachments[0];
        }
    }
    if (info.pDynamicState)
    {
        record.mDynamic = *info.pDynamicState;
        if (info.pDynamicState->dynamicStateCount == 2 && info.pDynamicState->pDynamicStates)
        {
            std::copy_n(info.pDynamicState->pDynamicStates, 2, record.mDynamicStates.begin());
        }
    }
    record.mLayout            = info.layout;
    record.mRenderPass        = info.renderPass;
    record.mSubpass           = info.subpass;
    record.mBasePipeline      = info.basePipelineHandle;
    record.mBasePipelineIndex = info.basePipelineIndex;
    gFakeState->mMutationOrder.emplace_back("pipeline");
    gFakeState->mDestroyArgumentsExact &= allocator == nullptr;
    *pipelines = gFakeState->mPipelineOutput;
    invalidateAt("create-pipeline");
    return gFakeState->mPipelineResult;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyPipeline(VkDevice device, VkPipeline pipeline, const VkAllocationCallbacks* allocator) noexcept
{
    if (!gFakeState)
    {
        return;
    }
    gFakeState->mDestroyedPipelines.push_back(pipeline);
    gFakeState->mDestroyOrder.emplace_back("pipeline");
    gFakeState->mDestroyArgumentsExact &= device == gFakeState->mDevice && allocator == nullptr;
    attemptResetReentry();
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL fakeGetDeviceProcAddr(VkDevice device, const char* name) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || !name)
    {
        return nullptr;
    }
    if (gFakeState->mOwnerPhase)
    {
        gFakeState->mDeviceLookups.emplace_back(name);
        invalidateAt(std::string("resolve:") + name);
        if (gFakeState->mMissingCommand == name)
        {
            return nullptr;
        }
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
    if (std::strcmp(name, "vkCreateRenderPass") == 0)
        return eraseFunctionType(fakeCreateRenderPass);
    if (std::strcmp(name, "vkDestroyRenderPass") == 0)
        return eraseFunctionType(fakeDestroyRenderPass);
    if (std::strcmp(name, "vkCreateFramebuffer") == 0)
        return eraseFunctionType(fakeCreateFramebuffer);
    if (std::strcmp(name, "vkDestroyFramebuffer") == 0)
        return eraseFunctionType(fakeDestroyFramebuffer);
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
    if (std::strcmp(name, "vkCreateShaderModule") == 0)
        return eraseFunctionType(fakeCreateShaderModule);
    if (std::strcmp(name, "vkDestroyShaderModule") == 0)
        return eraseFunctionType(fakeDestroyShaderModule);
    if (std::strcmp(name, "vkCreateGraphicsPipelines") == 0)
        return eraseFunctionType(fakeCreateGraphicsPipelines);
    if (std::strcmp(name, "vkDestroyPipeline") == 0)
        return eraseFunctionType(fakeDestroyPipeline);
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

    if (std::strcmp(name, "vkGetDeviceProcAddr") == 0)
    {
        if (gFakeState->mOwnerPhase)
        {
            gFakeState->mInstanceLookups.emplace_back(name);
            invalidateAt("resolve:vkGetDeviceProcAddr");
            if (gFakeState->mMissingCommand == name)
            {
                return nullptr;
            }
        }
        return eraseFunctionType(fakeGetDeviceProcAddr);
    }
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

VulkanTextureUploadDestinationGeneration makeDestination(FakeState&                      state,
                                                         VulkanPhysicalDeviceGeneration& physical,
                                                         VulkanLogicalDeviceGeneration&  logical)
{
    auto destination = VulkanTextureUploadDestinationGenerationTestAccess::create(physical,
                                                                                  logical,
                                                                                  vulkanTextureUploadDestinationDescription(),
                                                                                  state.mDestinationImage,
                                                                                  state.mDestinationMemory,
                                                                                  state.mDestinationView,
                                                                                  fakeDestroyImageView,
                                                                                  fakeDestroyImage,
                                                                                  fakeFreeMemory);
    tut::ensure("the destination fixture becomes resident",
                VulkanTextureUploadDestinationGenerationTestAccess::markResident(destination,
                                                                                 LLRenderContract::TEXTURE_UPLOAD_REVISION,
                                                                                 state.mContentIdentity));
    return destination;
}

VulkanTextureUploadSampleBindingGeneration makeBinding(FakeState&                                state,
                                                       VulkanPhysicalDeviceGeneration&           physical,
                                                       VulkanLogicalDeviceGeneration&            logical,
                                                       VulkanTextureUploadDestinationGeneration& destination)
{
    auto result = resolveVulkanTextureUploadSampleBindingGeneration(physical,
                                                                    logical,
                                                                    vulkanTextureUploadDestinationDescription(),
                                                                    vulkanTextureUploadSampleBindingDescription(),
                                                                    destination);
    tut::ensure("the sampled-binding fixture resolves", std::holds_alternative<VulkanTextureUploadSampleBindingGeneration>(result));
    auto binding = std::get<VulkanTextureUploadSampleBindingGeneration>(std::move(result));
    tut::ensure("the sampled-binding fixture uses the expected layout and set",
                binding.pipelineLayout() == state.mPipelineLayout && binding.descriptorSet() == state.mDescriptorSet);
    return binding;
}

VulkanSwapchainConfigurationGeneration makeConfiguration(VulkanPhysicalDeviceGeneration& physical, VulkanLogicalDeviceGeneration& logical)
{
    auto result = resolveVulkanSwapchainConfigurationGeneration(physical, logical, { 1280, 720 });
    tut::ensure("the swapchain-configuration fixture resolves", std::holds_alternative<VulkanSwapchainConfigurationGeneration>(result));
    return std::get<VulkanSwapchainConfigurationGeneration>(std::move(result));
}

VulkanSwapchainGeneration makeSwapchain(VulkanLogicalDeviceGeneration& logical, VulkanSwapchainConfigurationGeneration& configuration)
{
    auto result = resolveVulkanSwapchainGeneration(logical, configuration);
    tut::ensure("the swapchain fixture resolves", std::holds_alternative<VulkanSwapchainGeneration>(result));
    return std::get<VulkanSwapchainGeneration>(std::move(result));
}

VulkanSwapchainImagesGeneration makeImages(VulkanLogicalDeviceGeneration&          logical,
                                           VulkanSwapchainConfigurationGeneration& configuration,
                                           VulkanSwapchainGeneration&              swapchain)
{
    auto result = resolveVulkanSwapchainImagesGeneration(logical, configuration, swapchain);
    tut::ensure("the swapchain-images fixture resolves", std::holds_alternative<VulkanSwapchainImagesGeneration>(result));
    return std::get<VulkanSwapchainImagesGeneration>(std::move(result));
}

VulkanSwapchainPresentationTargetGeneration makeTarget(VulkanLogicalDeviceGeneration&          logical,
                                                       VulkanSwapchainConfigurationGeneration& configuration,
                                                       VulkanSwapchainGeneration&              swapchain,
                                                       VulkanSwapchainImagesGeneration&        images)
{
    auto result = resolveVulkanSwapchainPresentationTargetGeneration(logical, configuration, swapchain, images);
    tut::ensure("the presentation-target fixture resolves", std::holds_alternative<VulkanSwapchainPresentationTargetGeneration>(result));
    return std::get<VulkanSwapchainPresentationTargetGeneration>(std::move(result));
}

struct Resources
{
    explicit Resources(FakeState& state) :
        mPhysical(makePhysical(state)),
        mLogical(makeLogical(mPhysical)),
        mDestination(makeDestination(state, mPhysical, mLogical)),
        mBinding(makeBinding(state, mPhysical, mLogical, mDestination)),
        mConfiguration(makeConfiguration(mPhysical, mLogical)),
        mSwapchain(makeSwapchain(mLogical, mConfiguration)),
        mImages(makeImages(mLogical, mConfiguration, mSwapchain)),
        mTarget(makeTarget(mLogical, mConfiguration, mSwapchain, mImages))
    {
        state.mInstanceLookups.clear();
        state.mDeviceLookups.clear();
        state.mMutationOrder.clear();
        state.mDestroyOrder.clear();
    }

    VulkanPhysicalDeviceGeneration              mPhysical;
    VulkanLogicalDeviceGeneration               mLogical;
    VulkanTextureUploadDestinationGeneration    mDestination;
    VulkanTextureUploadSampleBindingGeneration  mBinding;
    VulkanSwapchainConfigurationGeneration      mConfiguration;
    VulkanSwapchainGeneration                   mSwapchain;
    VulkanSwapchainImagesGeneration             mImages;
    VulkanSwapchainPresentationTargetGeneration mTarget;
};

VulkanTextureUploadSamplePipelineResolutionResult resolvePipeline(
    FakeState&                                          state,
    Resources&                                          resources,
    const VulkanTextureUploadSamplePipelineDescription& pipeline_description    = vulkanTextureUploadSamplePipelineDescription(),
    const VulkanTextureUploadDestinationDescription&    destination_description = vulkanTextureUploadDestinationDescription(),
    const VulkanTextureUploadSampleBindingDescription&  binding_description     = vulkanTextureUploadSampleBindingDescription())
{
    state.mOwnerPhase = true;
    return resolveVulkanTextureUploadSamplePipelineGeneration(resources.mPhysical,
                                                              resources.mLogical,
                                                              destination_description,
                                                              binding_description,
                                                              pipeline_description,
                                                              resources.mDestination,
                                                              resources.mBinding,
                                                              resources.mConfiguration,
                                                              resources.mSwapchain,
                                                              resources.mImages,
                                                              resources.mTarget);
}

const VulkanTextureUploadSamplePipelineResolutionError& requireError(const VulkanTextureUploadSamplePipelineResolutionResult& result)
{
    const auto* error = std::get_if<VulkanTextureUploadSamplePipelineResolutionError>(&result);
    tut::ensure("sample-pipeline resolution returns an error", error != nullptr);
    return *error;
}

void ensureCode(const VulkanTextureUploadSamplePipelineResolutionResult& result, VulkanTextureUploadSamplePipelineResolutionCode code)
{
    tut::ensure("the exact sampled-pipeline error is reported", requireError(result).mCode == code);
}

VulkanTextureUploadSamplePipelineGeneration takePipeline(VulkanTextureUploadSamplePipelineResolutionResult&& result)
{
    if (const auto* error = std::get_if<VulkanTextureUploadSamplePipelineResolutionError>(&result))
    {
        tut::ensure("sample-pipeline resolution returns a generation; error code " +
                        std::to_string(static_cast<unsigned int>(error->mCode)),
                    false);
    }
    return std::get<VulkanTextureUploadSamplePipelineGeneration>(std::move(result));
}

void ensureBorrowedPipelineLayoutPreserved(const FakeState& state, const Resources& resources, std::size_t destroy_count)
{
    tut::ensure("rollback uses exact destroy arguments and does not destroy the borrowed Stage 60 pipeline layout",
                state.mDestroyArgumentsExact && state.mPipelineLayoutDestroyCount == destroy_count &&
                    resources.mBinding.pipelineLayout() == state.mPipelineLayout);
}

std::uint64_t shaderChecksum(const std::vector<std::uint32_t>& words) noexcept
{
    std::uint64_t checksum = UINT64_C(14695981039346656037);
    for (std::uint32_t word : words)
    {
        for (unsigned int shift = 0; shift != 32; shift += 8)
        {
            checksum ^= (word >> shift) & 0xffU;
            checksum *= UINT64_C(1099511628211);
        }
    }
    return checksum;
}

} // namespace

namespace tut
{

struct render_vulkan_texture_upload_sample_pipeline_test
{
};

using render_vulkan_texture_upload_sample_pipeline_group  = test_group<render_vulkan_texture_upload_sample_pipeline_test>;
using render_vulkan_texture_upload_sample_pipeline_object = render_vulkan_texture_upload_sample_pipeline_group::object;
render_vulkan_texture_upload_sample_pipeline_group render_vulkan_texture_upload_sample_pipeline_tests(
    "render Vulkan texture upload sample pipeline");

template<>
template<>
void render_vulkan_texture_upload_sample_pipeline_object::test<1>()
{
    set_test_name("the neutral description and owner type form a strict move-only contract");

    static_assert(!std::is_default_constructible_v<VulkanTextureUploadSamplePipelineGeneration>);
    static_assert(!std::is_copy_constructible_v<VulkanTextureUploadSamplePipelineGeneration>);
    static_assert(!std::is_copy_assignable_v<VulkanTextureUploadSamplePipelineGeneration>);
    static_assert(std::is_nothrow_move_constructible_v<VulkanTextureUploadSamplePipelineGeneration>);
    static_assert(!std::is_move_assignable_v<VulkanTextureUploadSamplePipelineGeneration>);
    static_assert(std::is_nothrow_destructible_v<VulkanTextureUploadSamplePipelineGeneration>);
    static_assert(std::variant_size_v<VulkanTextureUploadSamplePipelineResolutionResult> == 2);
    static_assert(
        noexcept(resolveVulkanTextureUploadSamplePipelineGeneration(std::declval<const VulkanPhysicalDeviceGeneration&>(),
                                                                    std::declval<const VulkanLogicalDeviceGeneration&>(),
                                                                    std::declval<const VulkanTextureUploadDestinationDescription&>(),
                                                                    std::declval<const VulkanTextureUploadSampleBindingDescription&>(),
                                                                    std::declval<const VulkanTextureUploadSamplePipelineDescription&>(),
                                                                    std::declval<const VulkanTextureUploadDestinationGeneration&>(),
                                                                    std::declval<const VulkanTextureUploadSampleBindingGeneration&>(),
                                                                    std::declval<const VulkanSwapchainConfigurationGeneration&>(),
                                                                    std::declval<const VulkanSwapchainGeneration&>(),
                                                                    std::declval<const VulkanSwapchainImagesGeneration&>(),
                                                                    std::declval<const VulkanSwapchainPresentationTargetGeneration&>())));

    const auto description = vulkanTextureUploadSamplePipelineDescription();
    ensure("the only canonical description field is the neutral pipeline handle",
           description.mHandle == LLRenderContract::PipelineHandle{ 1, 1 } &&
               description.mHandle == LLRenderContract::StreamingUploadHandles{}.mPipeline);
    const VulkanTextureUploadSamplePipelineResolutionError error{
        VulkanTextureUploadSamplePipelineResolutionCode::GraphicsPipelineCreationFailure,
        VulkanTextureUploadSamplePipelineCommand::CreateGraphicsPipelines, VK_ERROR_OUT_OF_DEVICE_MEMORY
    };
    ensure("identical typed errors compare equal", error == error);

    FakeState       state;
    ScopedFakeState scope(state);
    Resources       resources(state);
    auto            invalid = description;
    ++invalid.mHandle.mGeneration;
    ensureCode(resolvePipeline(state, resources, invalid), VulkanTextureUploadSamplePipelineResolutionCode::InvalidDescription);
    ensure("invalid description stops before dispatch or native mutation",
           state.mInstanceLookups.empty() && state.mDeviceLookups.empty() && state.mShaderRecords.empty());
}

template<>
template<>
void render_vulkan_texture_upload_sample_pipeline_object::test<2>()
{
    set_test_name("invalid exact parents stop at their typed boundary before dispatch");

    {
        FakeState       state;
        ScopedFakeState scope(state);
        Resources       resources(state);
        auto            moved = std::move(resources.mPhysical);
        ensureCode(resolvePipeline(state, resources), VulkanTextureUploadSamplePipelineResolutionCode::InvalidPhysicalDeviceGeneration);
        ensure("invalid physical stops before dispatch", state.mInstanceLookups.empty() && state.mDeviceLookups.empty());
        resources.mTarget.reset();
        resources.mImages.reset();
        resources.mSwapchain.reset();
        resources.mBinding.reset();
        resources.mDestination.reset();
        resources.mLogical.reset();
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        Resources       resources(state);
        auto            moved = std::move(resources.mLogical);
        ensureCode(resolvePipeline(state, resources), VulkanTextureUploadSamplePipelineResolutionCode::InvalidLogicalDeviceGeneration);
        ensure("invalid logical stops before dispatch", state.mInstanceLookups.empty() && state.mDeviceLookups.empty());
        resources.mTarget.reset();
        resources.mImages.reset();
        resources.mSwapchain.reset();
        resources.mBinding.reset();
        resources.mDestination.reset();
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        Resources       resources(state);
        auto            moved = std::move(resources.mDestination);
        ensureCode(resolvePipeline(state, resources),
                   VulkanTextureUploadSamplePipelineResolutionCode::InvalidTextureUploadDestinationGeneration);
        ensure("invalid destination stops before dispatch", state.mInstanceLookups.empty() && state.mDeviceLookups.empty());
        resources.mBinding.reset();
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        Resources       resources(state);
        auto            moved = std::move(resources.mBinding);
        ensureCode(resolvePipeline(state, resources),
                   VulkanTextureUploadSamplePipelineResolutionCode::InvalidTextureUploadSampleBindingGeneration);
        ensure("invalid binding stops before dispatch", state.mInstanceLookups.empty() && state.mDeviceLookups.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        Resources       resources(state);
        auto            moved = std::move(resources.mConfiguration);
        ensureCode(resolvePipeline(state, resources),
                   VulkanTextureUploadSamplePipelineResolutionCode::InvalidSwapchainConfigurationGeneration);
        ensure("invalid configuration stops before dispatch", state.mInstanceLookups.empty() && state.mDeviceLookups.empty());
        resources.mTarget.reset();
        resources.mImages.reset();
        resources.mSwapchain.reset();
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        Resources       resources(state);
        auto            moved = std::move(resources.mSwapchain);
        ensureCode(resolvePipeline(state, resources), VulkanTextureUploadSamplePipelineResolutionCode::InvalidSwapchainGeneration);
        ensure("invalid swapchain stops before dispatch", state.mInstanceLookups.empty() && state.mDeviceLookups.empty());
        resources.mTarget.reset();
        resources.mImages.reset();
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        Resources       resources(state);
        auto            moved = std::move(resources.mImages);
        ensureCode(resolvePipeline(state, resources), VulkanTextureUploadSamplePipelineResolutionCode::InvalidSwapchainImagesGeneration);
        ensure("invalid images stop before dispatch", state.mInstanceLookups.empty() && state.mDeviceLookups.empty());
        resources.mTarget.reset();
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        Resources       resources(state);
        auto            moved = std::move(resources.mTarget);
        ensureCode(resolvePipeline(state, resources),
                   VulkanTextureUploadSamplePipelineResolutionCode::InvalidSwapchainPresentationTargetGeneration);
        ensure("invalid target stops before dispatch", state.mInstanceLookups.empty() && state.mDeviceLookups.empty());
    }
}

template<>
template<>
void render_vulkan_texture_upload_sample_pipeline_object::test<3>()
{
    set_test_name("all required loader-neutral commands resolve before native mutation");

    constexpr std::array cases{ std::pair{ "vkGetDeviceProcAddr", VulkanTextureUploadSamplePipelineCommand::GetDeviceProcAddr },
                                std::pair{ "vkCreateShaderModule", VulkanTextureUploadSamplePipelineCommand::CreateShaderModule },
                                std::pair{ "vkDestroyShaderModule", VulkanTextureUploadSamplePipelineCommand::DestroyShaderModule },
                                std::pair{ "vkCreateGraphicsPipelines", VulkanTextureUploadSamplePipelineCommand::CreateGraphicsPipelines },
                                std::pair{ "vkDestroyPipeline", VulkanTextureUploadSamplePipelineCommand::DestroyPipeline } };

    for (std::size_t index = 0; index < cases.size(); ++index)
    {
        FakeState       state;
        ScopedFakeState scope(state);
        Resources       resources(state);
        state.mMissingCommand = cases[index].first;
        const auto  result    = resolvePipeline(state, resources);
        const auto& error     = requireError(result);
        ensure("missing dispatch reports its exact typed command and neutral result",
               error.mCode == VulkanTextureUploadSamplePipelineResolutionCode::MissingRequiredCommand &&
                   error.mCommand == cases[index].second && error.mResult == VK_SUCCESS);
        ensure("missing dispatch stops at the exact lookup cutoff",
               state.mInstanceLookups == std::vector<std::string>{ "vkGetDeviceProcAddr" } && state.mDeviceLookups.size() == index &&
                   state.mShaderRecords.empty() && state.mMutationOrder.empty() && state.mDestroyedShaders.empty() &&
                   state.mDestroyedPipelines.empty());
    }
}

template<>
template<>
void render_vulkan_texture_upload_sample_pipeline_object::test<4>()
{
    set_test_name("success uses exact sampled shaders, borrowed layout, and fixed graphics state");

    FakeState       state;
    ScopedFakeState scope(state);
    Resources       resources(state);
    const auto      layout_create_count = state.mPipelineLayoutCreateCount;
    auto            pipeline            = takePipeline(resolvePipeline(state, resources));
    const auto      modules             = state.defaultShaderModules();

    ensure("all commands precede vertex-fragment-pipeline mutation and no new layout is created",
           state.mAllCommandsResolvedBeforeMutation &&
               state.mMutationOrder == std::vector<std::string>{ "vertex", "fragment", "pipeline" } &&
               state.mPipelineLayoutCreateCount == layout_create_count);
    ensure("the generation publishes its canonical handle, pipeline, borrowed layout, and exact parent chain",
           pipeline.pipelineResourceHandle() == LLRenderContract::StreamingUploadHandles{}.mPipeline &&
               pipeline.pipelineLayout() == resources.mBinding.pipelineLayout() && pipeline.pipeline() == state.mPipeline &&
               pipeline.matchesDescription(vulkanTextureUploadSamplePipelineDescription()) &&
               pipeline.retainsTextureUploadSampleBindingGeneration(resources.mBinding) &&
               pipeline.createdFor(resources.mPhysical,
                                   resources.mLogical,
                                   resources.mDestination,
                                   resources.mBinding,
                                   resources.mConfiguration,
                                   resources.mSwapchain,
                                   resources.mImages,
                                   resources.mTarget));

    ensure("the exact accepted texture-upload SPIR-V payloads are deep-copied",
           state.mShaderRecords.size() == 2 && state.mShaderRecords[0].mStructureType == VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO &&
               !state.mShaderRecords[0].mNext && state.mShaderRecords[0].mFlags == 0 && state.mShaderRecords[0].mCode.size() == 278 &&
               state.mShaderRecords[0].mCode.front() == 0x07230203 &&
               shaderChecksum(state.mShaderRecords[0].mCode) == UINT64_C(0xcb961d4b78e7dc03) &&
               state.mShaderRecords[1].mStructureType == VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO && !state.mShaderRecords[1].mNext &&
               state.mShaderRecords[1].mFlags == 0 && state.mShaderRecords[1].mCode.size() == 157 &&
               state.mShaderRecords[1].mCode.front() == 0x07230203 &&
               shaderChecksum(state.mShaderRecords[1].mCode) == UINT64_C(0xd79939740e024c50));

    const auto& record = state.mPipelineRecord;
    ensure("the pipeline root uses two main stages, the borrowed layout, exact pass, and no cache or derivative",
           record.mCache == VK_NULL_HANDLE && record.mCreateInfoCount == 1 &&
               record.mStructureType == VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO && !record.mNext && record.mFlags == 0 &&
               record.mStageCount == 2 && record.mStages[0].sType == VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO &&
               !record.mStages[0].pNext && record.mStages[0].flags == 0 && record.mStages[0].stage == VK_SHADER_STAGE_VERTEX_BIT &&
               record.mStages[0].module == modules[0] && record.mEntryPoints[0] == "main" && !record.mStages[0].pSpecializationInfo &&
               record.mStages[1].sType == VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO && !record.mStages[1].pNext &&
               record.mStages[1].flags == 0 && record.mStages[1].stage == VK_SHADER_STAGE_FRAGMENT_BIT &&
               record.mStages[1].module == modules[1] && record.mEntryPoints[1] == "main" && !record.mStages[1].pSpecializationInfo &&
               record.mLayout == resources.mBinding.pipelineLayout() && record.mRenderPass == resources.mTarget.renderPass() &&
               record.mSubpass == 0 && record.mBasePipeline == VK_NULL_HANDLE && record.mBasePipelineIndex == -1 && record.mAllocatorNull);
    ensure("vertex input, assembly, viewport, tessellation, and dynamic state are exact",
           record.mVertexInput.sType == VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO && !record.mVertexInput.pNext &&
               record.mVertexInput.flags == 0 && record.mVertexBindings.size() == 1 && record.mVertexBindings[0].binding == 0 &&
               record.mVertexBindings[0].stride == 16 && record.mVertexBindings[0].inputRate == VK_VERTEX_INPUT_RATE_VERTEX &&
               record.mVertexAttributes.size() == 1 && record.mVertexAttributes[0].location == 0 &&
               record.mVertexAttributes[0].binding == 0 && record.mVertexAttributes[0].format == VK_FORMAT_R32G32B32_SFLOAT &&
               record.mVertexAttributes[0].offset == 0 &&
               record.mInputAssembly.sType == VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO && !record.mInputAssembly.pNext &&
               record.mInputAssembly.flags == 0 && record.mInputAssembly.topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST &&
               record.mInputAssembly.primitiveRestartEnable == VK_FALSE && record.mTessellationNull &&
               record.mViewport.sType == VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO && !record.mViewport.pNext &&
               record.mViewport.flags == 0 && record.mViewport.viewportCount == 1 && !record.mViewport.pViewports &&
               record.mViewport.scissorCount == 1 && !record.mViewport.pScissors &&
               record.mDynamic.sType == VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO && !record.mDynamic.pNext &&
               record.mDynamic.flags == 0 && record.mDynamic.dynamicStateCount == 2 &&
               record.mDynamicStates == std::array<VkDynamicState, 2>{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR });
    ensure("rasterization, multisampling, depth, and color replacement state are exact",
           record.mRasterization.sType == VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO && !record.mRasterization.pNext &&
               record.mRasterization.flags == 0 && record.mRasterization.depthClampEnable == VK_FALSE &&
               record.mRasterization.rasterizerDiscardEnable == VK_FALSE && record.mRasterization.polygonMode == VK_POLYGON_MODE_FILL &&
               record.mRasterization.cullMode == VK_CULL_MODE_NONE && record.mRasterization.frontFace == VK_FRONT_FACE_COUNTER_CLOCKWISE &&
               record.mRasterization.depthBiasEnable == VK_FALSE && record.mRasterization.lineWidth == 1.f &&
               record.mMultisample.sType == VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO && !record.mMultisample.pNext &&
               record.mMultisample.flags == 0 && record.mMultisample.rasterizationSamples == VK_SAMPLE_COUNT_1_BIT &&
               record.mMultisample.sampleShadingEnable == VK_FALSE && !record.mMultisample.pSampleMask &&
               record.mMultisample.alphaToCoverageEnable == VK_FALSE && record.mMultisample.alphaToOneEnable == VK_FALSE &&
               record.mDepthStencilNull && record.mColorBlend.sType == VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO &&
               !record.mColorBlend.pNext && record.mColorBlend.flags == 0 && record.mColorBlend.logicOpEnable == VK_FALSE &&
               record.mColorBlend.logicOp == VK_LOGIC_OP_COPY && record.mColorBlend.attachmentCount == 1 &&
               record.mColorAttachment.blendEnable == VK_FALSE && record.mColorAttachment.srcColorBlendFactor == VK_BLEND_FACTOR_ONE &&
               record.mColorAttachment.dstColorBlendFactor == VK_BLEND_FACTOR_ZERO &&
               record.mColorAttachment.colorBlendOp == VK_BLEND_OP_ADD &&
               record.mColorAttachment.srcAlphaBlendFactor == VK_BLEND_FACTOR_ONE &&
               record.mColorAttachment.dstAlphaBlendFactor == VK_BLEND_FACTOR_ZERO &&
               record.mColorAttachment.alphaBlendOp == VK_BLEND_OP_ADD &&
               record.mColorAttachment.colorWriteMask ==
                   (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT));
    ensure("successful publication destroys transient fragment then vertex modules",
           state.mDestroyedShaders == std::vector<VkShaderModule>{ modules[1], modules[0] } &&
               state.mDestroyOrder == std::vector<std::string>{ "shader", "shader" });

    pipeline.reset();
    ensure("reset destroys only the owned pipeline and leaves the borrowed Stage 60 layout live",
           pipelineAccessorsInert(pipeline) && state.mDestroyedPipelines == std::vector<VkPipeline>{ state.mPipeline } &&
               state.mPipelineLayoutDestroyCount == 0 &&
               state.mDestroyOrder == std::vector<std::string>{ "shader", "shader", "pipeline" } && state.mDestroyArgumentsExact &&
               resources.mBinding.pipelineLayout() == state.mPipelineLayout);
    pipeline.reset();
    ensure("reset is idempotent", state.mDestroyOrder.size() == 3);
}

template<>
template<>
void render_vulkan_texture_upload_sample_pipeline_object::test<5>()
{
    set_test_name("creation failures preserve exact results and roll back only acquired occurrences in reverse order");

    {
        FakeState       state;
        ScopedFakeState scope(state);
        Resources       resources(state);
        const auto      layout_destroy_count = state.mPipelineLayoutDestroyCount;
        const auto      poison               = fakeHandle<VkShaderModule>(0xdead);
        state.mShaderResults                 = { VK_ERROR_OUT_OF_DEVICE_MEMORY };
        state.mShaderOutputs                 = { poison };
        const auto  result                   = resolvePipeline(state, resources);
        const auto& error                    = requireError(result);
        ensure("failed vertex creation ignores its poisoned output",
               error.mCode == VulkanTextureUploadSamplePipelineResolutionCode::VertexShaderModuleCreationFailure &&
                   error.mCommand == VulkanTextureUploadSamplePipelineCommand::CreateShaderModule &&
                   error.mResult == VK_ERROR_OUT_OF_DEVICE_MEMORY && state.mDestroyedShaders.empty() && state.mDestroyedPipelines.empty());
        ensureBorrowedPipelineLayoutPreserved(state, resources, layout_destroy_count);
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        Resources       resources(state);
        const auto      layout_destroy_count = state.mPipelineLayoutDestroyCount;
        state.mShaderOutputs                 = { VK_NULL_HANDLE };
        ensureCode(resolvePipeline(state, resources), VulkanTextureUploadSamplePipelineResolutionCode::NullVertexShaderModuleOnSuccess);
        ensure("null vertex success has no acquired obligation", state.mDestroyedShaders.empty() && state.mDestroyOrder.empty());
        ensureBorrowedPipelineLayoutPreserved(state, resources, layout_destroy_count);
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        Resources       resources(state);
        const auto      layout_destroy_count = state.mPipelineLayoutDestroyCount;
        const auto      modules              = state.defaultShaderModules();
        const auto      poison               = fakeHandle<VkShaderModule>(0xdead);
        state.mShaderResults                 = { VK_SUCCESS, VK_ERROR_OUT_OF_DEVICE_MEMORY };
        state.mShaderOutputs                 = { modules[0], poison };
        const auto  result                   = resolvePipeline(state, resources);
        const auto& error                    = requireError(result);
        ensure("failed fragment creation ignores poison and retires only vertex",
               error.mCode == VulkanTextureUploadSamplePipelineResolutionCode::FragmentShaderModuleCreationFailure &&
                   error.mResult == VK_ERROR_OUT_OF_DEVICE_MEMORY && state.mDestroyedShaders == std::vector<VkShaderModule>{ modules[0] } &&
                   state.mDestroyOrder == std::vector<std::string>{ "shader" });
        ensureBorrowedPipelineLayoutPreserved(state, resources, layout_destroy_count);
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        Resources       resources(state);
        const auto      layout_destroy_count = state.mPipelineLayoutDestroyCount;
        const auto      modules              = state.defaultShaderModules();
        state.mShaderOutputs                 = { modules[0], VK_NULL_HANDLE };
        ensureCode(resolvePipeline(state, resources), VulkanTextureUploadSamplePipelineResolutionCode::NullFragmentShaderModuleOnSuccess);
        ensure("null fragment success retires only vertex",
               state.mDestroyedShaders == std::vector<VkShaderModule>{ modules[0] } &&
                   state.mDestroyOrder == std::vector<std::string>{ "shader" });
        ensureBorrowedPipelineLayoutPreserved(state, resources, layout_destroy_count);
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        Resources       resources(state);
        const auto      layout_destroy_count = state.mPipelineLayoutDestroyCount;
        const auto      modules              = state.defaultShaderModules();
        const auto      partial              = fakeHandle<VkPipeline>(0xdead);
        state.mPipelineResult                = VK_ERROR_OUT_OF_DEVICE_MEMORY;
        state.mPipelineOutput                = partial;
        const auto  result                   = resolvePipeline(state, resources);
        const auto& error                    = requireError(result);
        ensure("aggregate pipeline failure destroys its donated partial result before both module occurrences",
               error.mCode == VulkanTextureUploadSamplePipelineResolutionCode::GraphicsPipelineCreationFailure &&
                   error.mCommand == VulkanTextureUploadSamplePipelineCommand::CreateGraphicsPipelines &&
                   error.mResult == VK_ERROR_OUT_OF_DEVICE_MEMORY && state.mDestroyedPipelines == std::vector<VkPipeline>{ partial } &&
                   state.mDestroyedShaders == std::vector<VkShaderModule>{ modules[1], modules[0] } &&
                   state.mDestroyOrder == std::vector<std::string>{ "pipeline", "shader", "shader" });
        ensureBorrowedPipelineLayoutPreserved(state, resources, layout_destroy_count);
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        Resources       resources(state);
        const auto      layout_destroy_count = state.mPipelineLayoutDestroyCount;
        const auto      modules              = state.defaultShaderModules();
        state.mPipelineOutput                = VK_NULL_HANDLE;
        ensureCode(resolvePipeline(state, resources), VulkanTextureUploadSamplePipelineResolutionCode::NullGraphicsPipelineOnSuccess);
        ensure("null pipeline success retires both transient modules without a pipeline destroy",
               state.mDestroyedPipelines.empty() && state.mDestroyedShaders == std::vector<VkShaderModule>{ modules[1], modules[0] } &&
                   state.mDestroyOrder == std::vector<std::string>{ "shader", "shader" });
        ensureBorrowedPipelineLayoutPreserved(state, resources, layout_destroy_count);
    }
}

template<>
template<>
void render_vulkan_texture_upload_sample_pipeline_object::test<6>()
{
    set_test_name("every resolver and native callback boundary rejects changed parents transactionally");

    enum class CallerDescription
    {
        Destination,
        Binding,
        Pipeline
    };
    struct DescriptionCase
    {
        const char*       mName;
        CallerDescription mDescription;
    };
    struct CallbackCase
    {
        const char*              mPoint;
        std::size_t              mDestroyedShaderCount;
        std::size_t              mDestroyedPipelineCount;
        std::vector<std::string> mDestroyOrder;
    };

    constexpr std::array description_cases{
        DescriptionCase{ "destination", CallerDescription::Destination },
        DescriptionCase{ "binding", CallerDescription::Binding },
        DescriptionCase{ "pipeline", CallerDescription::Pipeline },
    };
    const std::array callback_cases{
        CallbackCase{ "resolve:vkGetDeviceProcAddr", 0, 0, {} },
        CallbackCase{ "resolve:vkCreateShaderModule", 0, 0, {} },
        CallbackCase{ "resolve:vkDestroyShaderModule", 0, 0, {} },
        CallbackCase{ "resolve:vkCreateGraphicsPipelines", 0, 0, {} },
        CallbackCase{ "resolve:vkDestroyPipeline", 0, 0, {} },
        CallbackCase{ "create-vertex", 1, 0, { "shader" } },
        CallbackCase{ "create-fragment", 2, 0, { "shader", "shader" } },
        CallbackCase{ "create-pipeline", 2, 1, { "pipeline", "shader", "shader" } },
        CallbackCase{ "destroy-shader", 2, 1, { "shader", "shader", "pipeline" } },
    };

    for (const auto& description_case : description_cases)
    {
        for (const auto& callback_case : callback_cases)
        {
            FakeState       state;
            ScopedFakeState scope(state);
            Resources       resources(state);
            auto            destination_description = vulkanTextureUploadDestinationDescription();
            auto            binding_description     = vulkanTextureUploadSampleBindingDescription();
            auto            pipeline_description    = vulkanTextureUploadSamplePipelineDescription();
            const auto      original_destination    = destination_description;
            const auto      original_binding        = binding_description;
            const auto      original_pipeline       = pipeline_description;
            const auto      layout_destroy_count    = state.mPipelineLayoutDestroyCount;
            state.mInvalidateAt                     = callback_case.mPoint;

            switch (description_case.mDescription)
            {
                case CallerDescription::Destination:
                    state.mDestinationDescriptionToMutate = &destination_description;
                    break;
                case CallerDescription::Binding:
                    state.mBindingDescriptionToMutate = &binding_description;
                    break;
                case CallerDescription::Pipeline:
                    state.mPipelineDescriptionToMutate = &pipeline_description;
                    break;
            }

            ensureCode(resolvePipeline(state, resources, pipeline_description, destination_description, binding_description),
                       VulkanTextureUploadSamplePipelineResolutionCode::ParentGenerationChanged);

            const bool                  destination_changed = destination_description != original_destination;
            const bool                  binding_changed     = binding_description != original_binding;
            const bool                  pipeline_changed    = pipeline_description != original_pipeline;
            const auto                  modules             = state.defaultShaderModules();
            std::vector<VkShaderModule> expected_shaders;
            if (callback_case.mDestroyedShaderCount == 1)
            {
                expected_shaders = { modules[0] };
            }
            else if (callback_case.mDestroyedShaderCount == 2)
            {
                expected_shaders = { modules[1], modules[0] };
            }
            const std::vector<VkPipeline> expected_pipelines =
                callback_case.mDestroyedPipelineCount == 1 ? std::vector<VkPipeline>{ state.mPipeline } : std::vector<VkPipeline>{};
            const std::string context = std::string(description_case.mName) + " at " + callback_case.mPoint;
            ensure("only the selected caller-owned description changes: " + context,
                   state.mInvalidateAt.empty() &&
                       destination_changed == (description_case.mDescription == CallerDescription::Destination) &&
                       binding_changed == (description_case.mDescription == CallerDescription::Binding) &&
                       pipeline_changed == (description_case.mDescription == CallerDescription::Pipeline));
            ensure("changed-parent rollback retires exactly the acquired native occurrences: " + context,
                   state.mDestroyedShaders == expected_shaders && state.mDestroyedPipelines == expected_pipelines &&
                       state.mDestroyOrder == callback_case.mDestroyOrder && state.mDestroyArgumentsExact);
            ensureBorrowedPipelineLayoutPreserved(state, resources, layout_destroy_count);
        }
    }

    {
        FakeState       state;
        ScopedFakeState scope(state);
        Resources       resources(state);
        state.mInvalidateAt   = "create-fragment";
        state.mBindingToReset = &resources.mBinding;
        ensureCode(resolvePipeline(state, resources), VulkanTextureUploadSamplePipelineResolutionCode::ParentGenerationChanged);
        const auto modules = state.defaultShaderModules();
        ensure("binding invalidation retires fragment then vertex and never publishes a pipeline",
               resources.mBinding.pipelineLayout() == VK_NULL_HANDLE &&
                   state.mDestroyedShaders == std::vector<VkShaderModule>{ modules[1], modules[0] } && state.mDestroyedPipelines.empty() &&
                   state.mDestroyOrder == std::vector<std::string>{ "shader", "shader" });
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        Resources       resources(state);
        state.mInvalidateAt  = "create-pipeline";
        state.mTargetToReset = &resources.mTarget;
        ensureCode(resolvePipeline(state, resources), VulkanTextureUploadSamplePipelineResolutionCode::ParentGenerationChanged);
        const auto modules = state.defaultShaderModules();
        ensure("target invalidation retires pipeline then both modules in reverse acquisition order",
               resources.mTarget.renderPass() == VK_NULL_HANDLE &&
                   state.mDestroyedPipelines == std::vector<VkPipeline>{ state.mPipeline } &&
                   state.mDestroyedShaders == std::vector<VkShaderModule>{ modules[1], modules[0] } &&
                   state.mDestroyOrder == std::vector<std::string>{ "pipeline", "shader", "shader" });
    }
}

template<>
template<>
void render_vulkan_texture_upload_sample_pipeline_object::test<7>()
{
    set_test_name("move, parent identity, and reentrant idempotent reset preserve the single owned obligation");

    FakeState       state;
    ScopedFakeState scope(state);
    Resources       resources(state);
    auto            original = takePipeline(resolvePipeline(state, resources));
    auto            moved    = VulkanTextureUploadSamplePipelineGeneration(std::move(original));
    ensure("move transfers the exact pipeline and provenance while disarming the source",
           pipelineAccessorsInert(original) && moved.pipeline() == state.mPipeline &&
               moved.pipelineLayout() == resources.mBinding.pipelineLayout() &&
               moved.createdFor(resources.mPhysical,
                                resources.mLogical,
                                resources.mDestination,
                                resources.mBinding,
                                resources.mConfiguration,
                                resources.mSwapchain,
                                resources.mImages,
                                resources.mTarget));

    const std::size_t layout_destroy_count = state.mPipelineLayoutDestroyCount;
    state.mPipelineToReenter               = &moved;
    state.mPhysicalForAccess               = &resources.mPhysical;
    state.mLogicalForAccess                = &resources.mLogical;
    state.mDestinationForAccess            = &resources.mDestination;
    state.mBindingForAccess                = &resources.mBinding;
    state.mConfigurationForAccess          = &resources.mConfiguration;
    state.mSwapchainForAccess              = &resources.mSwapchain;
    state.mImagesForAccess                 = &resources.mImages;
    state.mTargetForAccess                 = &resources.mTarget;
    state.mReenterDestroy                  = true;
    moved.reset();
    moved.reset();
    ensure("reset disarms all public ownership state before its reentrant callback and destroys only the pipeline once",
           pipelineAccessorsInert(moved) && state.mReentryAttempts == 1 && state.mReentryObservedInert &&
               state.mDestroyedPipelines == std::vector<VkPipeline>{ state.mPipeline } &&
               state.mPipelineLayoutDestroyCount == layout_destroy_count && resources.mBinding.pipelineLayout() == state.mPipelineLayout &&
               state.mDestroyArgumentsExact);
}

template<>
template<>
void render_vulkan_texture_upload_sample_pipeline_object::test<8>()
{
    set_test_name("equal shader-module handle bits remain two transient ownership occurrences");

    FakeState       state;
    ScopedFakeState scope(state);
    Resources       resources(state);
    const auto      equal = fakeHandle<VkShaderModule>(0xeeee);
    state.mShaderOutputs  = { equal, equal };
    auto pipeline         = takePipeline(resolvePipeline(state, resources));
    ensure("fragment and vertex occurrences are each destroyed even when their handle bits match",
           state.mDestroyedShaders == std::vector<VkShaderModule>{ equal, equal } &&
               state.mDestroyOrder == std::vector<std::string>{ "shader", "shader" });
    pipeline.reset();
    ensure("the published pipeline remains one independent ownership occurrence",
           state.mDestroyedPipelines == std::vector<VkPipeline>{ state.mPipeline } &&
               state.mDestroyOrder == std::vector<std::string>{ "shader", "shader", "pipeline" });
}

template<>
template<>
void render_vulkan_texture_upload_sample_pipeline_object::test<9>()
{
    set_test_name("scope destruction follows the idempotent reset path without touching the borrowed layout");

    FakeState         state;
    ScopedFakeState   scope(state);
    Resources         resources(state);
    const std::size_t layout_destroy_count = state.mPipelineLayoutDestroyCount;
    {
        auto pipeline = takePipeline(resolvePipeline(state, resources));
        ensure("the scoped owner is live", pipeline.pipeline() == state.mPipeline);
    }
    ensure("scope exit destroys exactly the owned pipeline",
           state.mDestroyedPipelines == std::vector<VkPipeline>{ state.mPipeline } &&
               state.mPipelineLayoutDestroyCount == layout_destroy_count && resources.mBinding.pipelineLayout() == state.mPipelineLayout);
}

template<>
template<>
void render_vulkan_texture_upload_sample_pipeline_object::test<10>()
{
    set_test_name("the pipeline retains only the exact binding object and detects illegal parent detachment");

    FakeState       state;
    ScopedFakeState scope(state);
    Resources       resources(state);
    auto            pipeline             = takePipeline(resolvePipeline(state, resources));
    auto            detached_binding     = std::move(resources.mBinding);
    const auto      layout_destroy_count = state.mPipelineLayoutDestroyCount;
    ensure("detaching the retained binding invalidates authentication without discarding the owned pipeline",
           !pipeline.createdFor(resources.mPhysical,
                                resources.mLogical,
                                resources.mDestination,
                                resources.mBinding,
                                resources.mConfiguration,
                                resources.mSwapchain,
                                resources.mImages,
                                resources.mTarget) &&
               pipeline.retainsTextureUploadSampleBindingGeneration(resources.mBinding) &&
               !pipeline.retainsTextureUploadSampleBindingGeneration(detached_binding) && pipeline.pipeline() == state.mPipeline &&
               pipeline.pipelineLayout() == state.mPipelineLayout && detached_binding.pipelineLayout() == state.mPipelineLayout);
    pipeline.reset();
    ensure("the sampled pipeline retires independently after binding detachment",
           pipelineAccessorsInert(pipeline) && state.mDestroyedPipelines == std::vector<VkPipeline>{ state.mPipeline } &&
               state.mPipelineLayoutDestroyCount == layout_destroy_count && detached_binding.pipelineLayout() == state.mPipelineLayout &&
               state.mDestroyArgumentsExact);
}

template<>
template<>
void render_vulkan_texture_upload_sample_pipeline_object::test<11>()
{
    set_test_name("exact-looking distinct binding and target objects cannot authenticate the pipeline");

    FakeState       state;
    ScopedFakeState scope(state);
    Resources       resources(state);
    auto            pipeline = takePipeline(resolvePipeline(state, resources));
    state.mOwnerPhase        = false;
    auto alternate_binding   = makeBinding(state, resources.mPhysical, resources.mLogical, resources.mDestination);
    auto alternate_target    = makeTarget(resources.mLogical, resources.mConfiguration, resources.mSwapchain, resources.mImages);
    ensure("the distinct parents deliberately expose the same pipeline-visible native state",
           alternate_binding.pipelineLayout() == resources.mBinding.pipelineLayout() &&
               alternate_binding.descriptorSet() == resources.mBinding.descriptorSet() &&
               alternate_binding.matchesDescription(vulkanTextureUploadSampleBindingDescription()) &&
               alternate_target.renderPass() == resources.mTarget.renderPass() &&
               alternate_target.imageFormat() == resources.mTarget.imageFormat() &&
               alternate_target.imageExtent().width == resources.mTarget.imageExtent().width &&
               alternate_target.imageExtent().height == resources.mTarget.imageExtent().height &&
               alternate_target.framebufferCount() == resources.mTarget.framebufferCount());
    ensure("an exact-looking but distinct binding identity is rejected",
           !pipeline.createdFor(resources.mPhysical,
                                resources.mLogical,
                                resources.mDestination,
                                alternate_binding,
                                resources.mConfiguration,
                                resources.mSwapchain,
                                resources.mImages,
                                resources.mTarget) &&
               !pipeline.retainsTextureUploadSampleBindingGeneration(alternate_binding) &&
               pipeline.retainsTextureUploadSampleBindingGeneration(resources.mBinding));
    ensure("an exact-looking but distinct target identity is rejected",
           !pipeline.createdFor(resources.mPhysical,
                                resources.mLogical,
                                resources.mDestination,
                                resources.mBinding,
                                resources.mConfiguration,
                                resources.mSwapchain,
                                resources.mImages,
                                alternate_target));

    const auto layout_destroy_count = state.mPipelineLayoutDestroyCount;
    pipeline.reset();
    ensure("pipeline reset destroys only its owned pipeline while both exact-looking parents remain live",
           state.mDestroyedPipelines == std::vector<VkPipeline>{ state.mPipeline } && state.mDestroyArgumentsExact &&
               state.mPipelineLayoutDestroyCount == layout_destroy_count && resources.mBinding.pipelineLayout() == state.mPipelineLayout &&
               alternate_binding.pipelineLayout() == state.mPipelineLayout && alternate_target.renderPass() == state.mRenderPass);
}

} // namespace tut
