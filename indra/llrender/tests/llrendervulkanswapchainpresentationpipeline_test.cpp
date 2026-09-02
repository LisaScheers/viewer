/**
 * @file llrendervulkanswapchainpresentationpipeline_test.cpp
 * @brief Tests for Vulkan swapchain presentation-pipeline ownership.
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

#include "llrendervulkanswapchainpresentationpipeline.h"
#include "lltut.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
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
    CreateRenderPass,
    DestroyRenderPass,
    CreateFramebuffer,
    DestroyFramebuffer,
    CreateShaderModule,
    DestroyShaderModule,
    CreatePipelineLayout,
    DestroyPipelineLayout,
    CreateGraphicsPipelines,
    DestroyPipeline
};

struct RenderPassRecord
{
    VkStructureType         mStructureType   = VK_STRUCTURE_TYPE_MAX_ENUM;
    const void*             mNext            = nullptr;
    VkRenderPassCreateFlags mFlags           = 0;
    std::uint32_t           mAttachmentCount = 0;
    std::uint32_t           mSubpassCount    = 0;
    std::uint32_t           mDependencyCount = 0;
    bool                    mHasAttachments  = false;
    bool                    mHasSubpasses    = false;
    bool                    mHasDependencies = false;
    VkAttachmentDescription mAttachment{};
    VkSubpassDescription    mSubpass{};
    VkAttachmentReference   mColorAttachment{};
    bool                    mHasColorAttachment = false;
};

struct FramebufferRecord
{
    VkStructureType          mStructureType   = VK_STRUCTURE_TYPE_MAX_ENUM;
    const void*              mNext            = nullptr;
    VkFramebufferCreateFlags mFlags           = 0;
    VkRenderPass             mRenderPass      = VK_NULL_HANDLE;
    std::uint32_t            mAttachmentCount = 0;
    VkImageView              mAttachment      = VK_NULL_HANDLE;
    std::uint32_t            mWidth           = 0;
    std::uint32_t            mHeight          = 0;
    std::uint32_t            mLayers          = 0;
};

struct ShaderModuleRecord
{
    VkStructureType            mStructureType = VK_STRUCTURE_TYPE_MAX_ENUM;
    const void*                mNext          = nullptr;
    VkShaderModuleCreateFlags  mFlags         = 0;
    std::vector<std::uint32_t> mCode;
};

struct PipelineLayoutRecord
{
    VkStructureType             mStructureType          = VK_STRUCTURE_TYPE_MAX_ENUM;
    const void*                 mNext                   = nullptr;
    VkPipelineLayoutCreateFlags mFlags                  = 0;
    std::uint32_t               mSetLayoutCount         = 0;
    std::uint32_t               mPushConstantRangeCount = 0;
    bool                        mSetLayoutsNull         = false;
    bool                        mPushConstantsNull      = false;
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
    VkInstance       mInstance       = fakeHandle<VkInstance>(0x1000);
    VkSurfaceKHR     mSurface        = fakeHandle<VkSurfaceKHR>(0x2000);
    VkPhysicalDevice mPhysicalDevice = fakeHandle<VkPhysicalDevice>(0x3000);
    VkDevice         mDevice         = fakeHandle<VkDevice>(0x4000);
    VkQueue          mQueue          = fakeHandle<VkQueue>(0x5000);
    VkSwapchainKHR   mSwapchain      = fakeHandle<VkSwapchainKHR>(0x6000);
    std::uint32_t    mQueueFamily    = 2;

    VkSurfaceCapabilitiesKHR          mCapabilities{ 2,
                                            0,
                                                     { std::numeric_limits<std::uint32_t>::max(), std::numeric_limits<std::uint32_t>::max() },
                                                     { 64, 64 },
                                                     { 4096, 2160 },
                                            1,
                                            VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
                                            VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
                                            VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
                                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT };
    std::array<VkSurfaceFormatKHR, 1> mFormats{ VkSurfaceFormatKHR{ VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR } };
    std::array<VkPresentModeKHR, 1>   mPresentModes{ VK_PRESENT_MODE_FIFO_KHR };
    std::array<VkImage, 3>            mImages{ fakeHandle<VkImage>(0x7100), fakeHandle<VkImage>(0x7200), fakeHandle<VkImage>(0x7300) };

    MissingCommand           mMissingCommand = MissingCommand::None;
    std::vector<std::string> mInstanceLookups;
    std::vector<std::string> mDeviceLookups;

    std::vector<VkImageView> mCreatedImageViews;
    std::size_t              mDestroyImageViewCalls = 0;
    std::size_t              mDestroySwapchainCalls = 0;
    std::size_t              mDestroyDeviceCalls    = 0;

    VkResult                      mRenderPassResult = VK_SUCCESS;
    VkRenderPass                  mRenderPassOutput = fakeHandle<VkRenderPass>(0x9000);
    std::vector<RenderPassRecord> mRenderPassRecords;
    std::vector<VkDevice>         mRenderPassDevices;
    std::vector<bool>             mRenderPassAllocatorNull;

    std::vector<VkResult>          mFramebufferResults;
    std::vector<VkFramebuffer>     mFramebufferOutputs;
    std::vector<FramebufferRecord> mFramebufferRecords;
    std::vector<VkDevice>          mFramebufferDevices;
    std::vector<bool>              mFramebufferAllocatorNull;

    std::vector<VkFramebuffer> mDestroyedFramebuffers;
    std::vector<VkDevice>      mFramebufferDestroyDevices;
    std::vector<bool>          mFramebufferDestroyAllocatorNull;
    std::vector<VkRenderPass>  mDestroyedRenderPasses;
    std::vector<VkDevice>      mRenderPassDestroyDevices;
    std::vector<bool>          mRenderPassDestroyAllocatorNull;
    std::vector<std::string>   mTargetDestroyOrder;
    bool                       mAllCommandsResolvedBeforeMutation = false;
    bool                       mAllocationCompletedBeforeMutation = false;

    VkResult                                     mPipelineLayoutResult = VK_SUCCESS;
    VkPipelineLayout                             mPipelineLayoutOutput = fakeHandle<VkPipelineLayout>(0xb000);
    PipelineLayoutRecord                         mPipelineLayoutRecord;
    std::vector<VkResult>                        mShaderModuleResults;
    std::vector<VkShaderModule>                  mShaderModuleOutputs;
    std::vector<ShaderModuleRecord>              mShaderModuleRecords;
    VkResult                                     mGraphicsPipelineResult = VK_SUCCESS;
    VkPipeline                                   mGraphicsPipelineOutput = fakeHandle<VkPipeline>(0xb300);
    GraphicsPipelineRecord                       mGraphicsPipelineRecord;
    std::vector<VkShaderModule>                  mDestroyedShaderModules;
    std::vector<VkPipeline>                      mDestroyedPipelines;
    std::vector<VkPipelineLayout>                mDestroyedPipelineLayouts;
    std::vector<std::string>                     mPipelineMutationOrder;
    std::vector<std::string>                     mPipelineDestroyOrder;
    bool                                         mAllPipelineCommandsResolvedBeforeMutation = false;
    bool                                         mPipelineDestroyArgumentsExact             = true;
    VulkanSwapchainPresentationTargetGeneration* mTargetToResetAfterPipelineCreation        = nullptr;

    std::vector<VkShaderModule> defaultShaderModules() const
    {
        return { fakeHandle<VkShaderModule>(0xb100), fakeHandle<VkShaderModule>(0xb200) };
    }

    std::vector<VkFramebuffer> defaultFramebuffers() const
    {
        return { fakeHandle<VkFramebuffer>(0xa100), fakeHandle<VkFramebuffer>(0xa200), fakeHandle<VkFramebuffer>(0xa300) };
    }
};

FakeState*  gFakeState          = nullptr;
std::size_t gAllocationCalls    = 0;
std::size_t gFailAllocationCall = 0;

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
    if (!gFakeState || physical_device != gFakeState->mPhysicalDevice || !properties)
    {
        return;
    }
    *properties                             = {};
    properties->apiVersion                  = VK_API_VERSION_1_1;
    properties->limits.maxFramebufferWidth  = 4096;
    properties->limits.maxFramebufferHeight = 2160;
    properties->limits.maxFramebufferLayers = 1;
    std::strncpy(properties->deviceName, "presentation-target-fake", VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 1);
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
    if (!gFakeState || physical_device != gFakeState->mPhysicalDevice || !create_info || !device ||
        create_info->enabledExtensionCount != 2 || !create_info->ppEnabledExtensionNames || !create_info->ppEnabledExtensionNames[0] ||
        !create_info->ppEnabledExtensionNames[1] ||
        std::strcmp(create_info->ppEnabledExtensionNames[0], VK_KHR_SWAPCHAIN_EXTENSION_NAME) != 0 ||
        std::strcmp(create_info->ppEnabledExtensionNames[1], VK_KHR_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME) != 0)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    const auto* maintenance = static_cast<const VkPhysicalDeviceSwapchainMaintenance1FeaturesKHR*>(create_info->pNext);
    if (!maintenance || maintenance->sType != VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_KHR ||
        maintenance->swapchainMaintenance1 != VK_TRUE || maintenance->pNext)
    {
        return VK_ERROR_FEATURE_NOT_PRESENT;
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
        *properties                       = {};
        properties->optimalTilingFeatures = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
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

VKAPI_ATTR void VKAPI_CALL fakeDestroySwapchain(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks*) noexcept
{
    if (gFakeState && device == gFakeState->mDevice && swapchain == gFakeState->mSwapchain)
    {
        ++gFakeState->mDestroySwapchainCalls;
    }
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
    if (!gFakeState || device != gFakeState->mDevice || !image_view)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    *image_view = fakeHandle<VkImageView>(0x8100 + gFakeState->mCreatedImageViews.size() * 0x100);
    gFakeState->mCreatedImageViews.push_back(*image_view);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyImageView(VkDevice device, VkImageView, const VkAllocationCallbacks*) noexcept
{
    if (gFakeState && device == gFakeState->mDevice)
    {
        ++gFakeState->mDestroyImageViewCalls;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateRenderPass(VkDevice                      device,
                                                    const VkRenderPassCreateInfo* create_info,
                                                    const VkAllocationCallbacks*  allocator,
                                                    VkRenderPass*                 render_pass) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || !create_info || !render_pass)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    RenderPassRecord record;
    record.mStructureType   = create_info->sType;
    record.mNext            = create_info->pNext;
    record.mFlags           = create_info->flags;
    record.mAttachmentCount = create_info->attachmentCount;
    record.mSubpassCount    = create_info->subpassCount;
    record.mDependencyCount = create_info->dependencyCount;
    record.mHasAttachments  = create_info->pAttachments != nullptr;
    record.mHasSubpasses    = create_info->pSubpasses != nullptr;
    record.mHasDependencies = create_info->pDependencies != nullptr;
    if (create_info->attachmentCount == 1 && create_info->pAttachments)
    {
        record.mAttachment = create_info->pAttachments[0];
    }
    if (create_info->subpassCount == 1 && create_info->pSubpasses)
    {
        record.mSubpass = create_info->pSubpasses[0];
        if (record.mSubpass.colorAttachmentCount == 1 && record.mSubpass.pColorAttachments)
        {
            record.mColorAttachment    = record.mSubpass.pColorAttachments[0];
            record.mHasColorAttachment = true;
        }
    }
    gFakeState->mRenderPassRecords.push_back(record);
    gFakeState->mRenderPassDevices.push_back(device);
    gFakeState->mRenderPassAllocatorNull.push_back(allocator == nullptr);
    gFakeState->mAllCommandsResolvedBeforeMutation =
        gFakeState->mInstanceLookups == std::vector<std::string>{ "vkGetDeviceProcAddr" } &&
        gFakeState->mDeviceLookups ==
            std::vector<std::string>{ "vkCreateRenderPass", "vkDestroyRenderPass", "vkCreateFramebuffer", "vkDestroyFramebuffer" };
    gFakeState->mAllocationCompletedBeforeMutation = gAllocationCalls == 1;

    *render_pass = gFakeState->mRenderPassOutput;
    return gFakeState->mRenderPassResult;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyRenderPass(VkDevice device, VkRenderPass render_pass, const VkAllocationCallbacks* allocator) noexcept
{
    if (!gFakeState)
    {
        return;
    }
    gFakeState->mDestroyedRenderPasses.push_back(render_pass);
    gFakeState->mRenderPassDestroyDevices.push_back(device);
    gFakeState->mRenderPassDestroyAllocatorNull.push_back(allocator == nullptr);
    gFakeState->mTargetDestroyOrder.emplace_back("render-pass");
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateFramebuffer(VkDevice                       device,
                                                     const VkFramebufferCreateInfo* create_info,
                                                     const VkAllocationCallbacks*   allocator,
                                                     VkFramebuffer*                 framebuffer) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || !create_info || !framebuffer)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    FramebufferRecord record;
    record.mStructureType   = create_info->sType;
    record.mNext            = create_info->pNext;
    record.mFlags           = create_info->flags;
    record.mRenderPass      = create_info->renderPass;
    record.mAttachmentCount = create_info->attachmentCount;
    if (create_info->attachmentCount == 1 && create_info->pAttachments)
    {
        record.mAttachment = create_info->pAttachments[0];
    }
    record.mWidth  = create_info->width;
    record.mHeight = create_info->height;
    record.mLayers = create_info->layers;
    gFakeState->mFramebufferRecords.push_back(record);
    gFakeState->mFramebufferDevices.push_back(device);
    gFakeState->mFramebufferAllocatorNull.push_back(allocator == nullptr);

    const std::size_t index    = gFakeState->mFramebufferRecords.size() - 1;
    const auto        defaults = gFakeState->defaultFramebuffers();
    *framebuffer =
        index < gFakeState->mFramebufferOutputs.size() ? gFakeState->mFramebufferOutputs[index] : defaults[index % defaults.size()];
    return index < gFakeState->mFramebufferResults.size() ? gFakeState->mFramebufferResults[index] : VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyFramebuffer(VkDevice                     device,
                                                  VkFramebuffer                framebuffer,
                                                  const VkAllocationCallbacks* allocator) noexcept
{
    if (!gFakeState)
    {
        return;
    }
    gFakeState->mDestroyedFramebuffers.push_back(framebuffer);
    gFakeState->mFramebufferDestroyDevices.push_back(device);
    gFakeState->mFramebufferDestroyAllocatorNull.push_back(allocator == nullptr);
    gFakeState->mTargetDestroyOrder.emplace_back("framebuffer");
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreatePipelineLayout(VkDevice                          device,
                                                        const VkPipelineLayoutCreateInfo* create_info,
                                                        const VkAllocationCallbacks*      allocator,
                                                        VkPipelineLayout*                 pipeline_layout) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || !create_info || !pipeline_layout)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    auto& record                   = gFakeState->mPipelineLayoutRecord;
    record.mStructureType          = create_info->sType;
    record.mNext                   = create_info->pNext;
    record.mFlags                  = create_info->flags;
    record.mSetLayoutCount         = create_info->setLayoutCount;
    record.mPushConstantRangeCount = create_info->pushConstantRangeCount;
    record.mSetLayoutsNull         = create_info->pSetLayouts == nullptr;
    record.mPushConstantsNull      = create_info->pPushConstantRanges == nullptr;
    gFakeState->mPipelineMutationOrder.emplace_back("layout");
    gFakeState->mAllPipelineCommandsResolvedBeforeMutation =
        gFakeState->mInstanceLookups == std::vector<std::string>{ "vkGetDeviceProcAddr" } &&
        gFakeState->mDeviceLookups == std::vector<std::string>{ "vkCreateShaderModule",      "vkDestroyShaderModule",
                                                                "vkCreatePipelineLayout",    "vkDestroyPipelineLayout",
                                                                "vkCreateGraphicsPipelines", "vkDestroyPipeline" };
    gFakeState->mPipelineDestroyArgumentsExact &= allocator == nullptr;
    *pipeline_layout = gFakeState->mPipelineLayoutOutput;
    return gFakeState->mPipelineLayoutResult;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyPipelineLayout(VkDevice                     device,
                                                     VkPipelineLayout             pipeline_layout,
                                                     const VkAllocationCallbacks* allocator) noexcept
{
    if (!gFakeState)
    {
        return;
    }
    gFakeState->mDestroyedPipelineLayouts.push_back(pipeline_layout);
    gFakeState->mPipelineDestroyOrder.emplace_back("layout");
    gFakeState->mPipelineDestroyArgumentsExact &= device == gFakeState->mDevice && allocator == nullptr;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateShaderModule(VkDevice                        device,
                                                      const VkShaderModuleCreateInfo* create_info,
                                                      const VkAllocationCallbacks*    allocator,
                                                      VkShaderModule*                 shader_module) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || !create_info || !shader_module ||
        create_info->codeSize % sizeof(std::uint32_t) != 0 || !create_info->pCode)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    ShaderModuleRecord record;
    record.mStructureType = create_info->sType;
    record.mNext          = create_info->pNext;
    record.mFlags         = create_info->flags;
    record.mCode.assign(create_info->pCode, create_info->pCode + create_info->codeSize / sizeof(std::uint32_t));
    gFakeState->mShaderModuleRecords.push_back(std::move(record));
    gFakeState->mPipelineMutationOrder.emplace_back(gFakeState->mShaderModuleRecords.size() == 1 ? "vertex" : "fragment");
    gFakeState->mPipelineDestroyArgumentsExact &= allocator == nullptr;

    const std::size_t index    = gFakeState->mShaderModuleRecords.size() - 1;
    const auto        defaults = gFakeState->defaultShaderModules();
    *shader_module =
        index < gFakeState->mShaderModuleOutputs.size() ? gFakeState->mShaderModuleOutputs[index] : defaults[index % defaults.size()];
    return index < gFakeState->mShaderModuleResults.size() ? gFakeState->mShaderModuleResults[index] : VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyShaderModule(VkDevice                     device,
                                                   VkShaderModule               shader_module,
                                                   const VkAllocationCallbacks* allocator) noexcept
{
    if (!gFakeState)
    {
        return;
    }
    gFakeState->mDestroyedShaderModules.push_back(shader_module);
    gFakeState->mPipelineDestroyOrder.emplace_back("shader");
    gFakeState->mPipelineDestroyArgumentsExact &= device == gFakeState->mDevice && allocator == nullptr;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateGraphicsPipelines(VkDevice                            device,
                                                           VkPipelineCache                     pipeline_cache,
                                                           std::uint32_t                       create_info_count,
                                                           const VkGraphicsPipelineCreateInfo* create_infos,
                                                           const VkAllocationCallbacks*        allocator,
                                                           VkPipeline*                         pipelines) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || create_info_count != 1 || !create_infos || !pipelines)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    const auto& info        = create_infos[0];
    auto&       record      = gFakeState->mGraphicsPipelineRecord;
    record.mCache           = pipeline_cache;
    record.mCreateInfoCount = create_info_count;
    record.mStructureType   = info.sType;
    record.mNext            = info.pNext;
    record.mFlags           = info.flags;
    record.mStageCount      = info.stageCount;
    record.mAllocatorNull   = allocator == nullptr;
    if (info.stageCount == 2 && info.pStages)
    {
        std::copy_n(info.pStages, 2, record.mStages.begin());
        record.mEntryPoints[0] = info.pStages[0].pName ? info.pStages[0].pName : "";
        record.mEntryPoints[1] = info.pStages[1].pName ? info.pStages[1].pName : "";
    }
    if (info.pVertexInputState)
        record.mVertexInput = *info.pVertexInputState;
    if (info.pInputAssemblyState)
        record.mInputAssembly = *info.pInputAssemblyState;
    record.mTessellationNull = info.pTessellationState == nullptr;
    if (info.pViewportState)
        record.mViewport = *info.pViewportState;
    if (info.pRasterizationState)
        record.mRasterization = *info.pRasterizationState;
    if (info.pMultisampleState)
        record.mMultisample = *info.pMultisampleState;
    record.mDepthStencilNull = info.pDepthStencilState == nullptr;
    if (info.pColorBlendState)
    {
        record.mColorBlend = *info.pColorBlendState;
        if (info.pColorBlendState->attachmentCount == 1 && info.pColorBlendState->pAttachments)
            record.mColorAttachment = info.pColorBlendState->pAttachments[0];
    }
    if (info.pDynamicState)
    {
        record.mDynamic = *info.pDynamicState;
        if (info.pDynamicState->dynamicStateCount == 2 && info.pDynamicState->pDynamicStates)
            std::copy_n(info.pDynamicState->pDynamicStates, 2, record.mDynamicStates.begin());
    }
    record.mLayout            = info.layout;
    record.mRenderPass        = info.renderPass;
    record.mSubpass           = info.subpass;
    record.mBasePipeline      = info.basePipelineHandle;
    record.mBasePipelineIndex = info.basePipelineIndex;
    gFakeState->mPipelineMutationOrder.emplace_back("pipeline");
    gFakeState->mPipelineDestroyArgumentsExact &= allocator == nullptr;
    *pipelines = gFakeState->mGraphicsPipelineOutput;
    if (gFakeState->mTargetToResetAfterPipelineCreation)
    {
        gFakeState->mTargetToResetAfterPipelineCreation->reset();
        gFakeState->mTargetToResetAfterPipelineCreation = nullptr;
    }
    return gFakeState->mGraphicsPipelineResult;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyPipeline(VkDevice device, VkPipeline pipeline, const VkAllocationCallbacks* allocator) noexcept
{
    if (!gFakeState)
    {
        return;
    }
    gFakeState->mDestroyedPipelines.push_back(pipeline);
    gFakeState->mPipelineDestroyOrder.emplace_back("pipeline");
    gFakeState->mPipelineDestroyArgumentsExact &= device == gFakeState->mDevice && allocator == nullptr;
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
    if (std::strcmp(name, "vkCreateRenderPass") == 0)
        return gFakeState->mMissingCommand == MissingCommand::CreateRenderPass ? nullptr : eraseFunctionType(fakeCreateRenderPass);
    if (std::strcmp(name, "vkDestroyRenderPass") == 0)
        return gFakeState->mMissingCommand == MissingCommand::DestroyRenderPass ? nullptr : eraseFunctionType(fakeDestroyRenderPass);
    if (std::strcmp(name, "vkCreateFramebuffer") == 0)
        return gFakeState->mMissingCommand == MissingCommand::CreateFramebuffer ? nullptr : eraseFunctionType(fakeCreateFramebuffer);
    if (std::strcmp(name, "vkDestroyFramebuffer") == 0)
        return gFakeState->mMissingCommand == MissingCommand::DestroyFramebuffer ? nullptr : eraseFunctionType(fakeDestroyFramebuffer);
    if (std::strcmp(name, "vkCreateShaderModule") == 0)
        return gFakeState->mMissingCommand == MissingCommand::CreateShaderModule ? nullptr : eraseFunctionType(fakeCreateShaderModule);
    if (std::strcmp(name, "vkDestroyShaderModule") == 0)
        return gFakeState->mMissingCommand == MissingCommand::DestroyShaderModule ? nullptr : eraseFunctionType(fakeDestroyShaderModule);
    if (std::strcmp(name, "vkCreatePipelineLayout") == 0)
        return gFakeState->mMissingCommand == MissingCommand::CreatePipelineLayout ? nullptr : eraseFunctionType(fakeCreatePipelineLayout);
    if (std::strcmp(name, "vkDestroyPipelineLayout") == 0)
        return gFakeState->mMissingCommand == MissingCommand::DestroyPipelineLayout ? nullptr
                                                                                    : eraseFunctionType(fakeDestroyPipelineLayout);
    if (std::strcmp(name, "vkCreateGraphicsPipelines") == 0)
        return gFakeState->mMissingCommand == MissingCommand::CreateGraphicsPipelines ? nullptr
                                                                                      : eraseFunctionType(fakeCreateGraphicsPipelines);
    if (std::strcmp(name, "vkDestroyPipeline") == 0)
        return gFakeState->mMissingCommand == MissingCommand::DestroyPipeline ? nullptr : eraseFunctionType(fakeDestroyPipeline);
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

    gFakeState->mInstanceLookups.emplace_back(name);
    if (std::strcmp(name, "vkGetDeviceProcAddr") == 0)
    {
        return gFakeState->mMissingCommand == MissingCommand::GetDeviceProcAddr ? nullptr : eraseFunctionType(fakeGetDeviceProcAddr);
    }
    return nullptr;
}

struct ParentBase
{
    VulkanPhysicalDeviceGeneration         mPhysical;
    VulkanLogicalDeviceGeneration          mLogical;
    VulkanSwapchainConfigurationGeneration mConfiguration;
    VulkanSwapchainGeneration              mSwapchain;
    VulkanSwapchainImagesGeneration        mImages;
};

VulkanSwapchainPresentationTargetGeneration makeTarget(ParentBase& parents)
{
    auto result =
        resolveVulkanSwapchainPresentationTargetGeneration(parents.mLogical, parents.mConfiguration, parents.mSwapchain, parents.mImages);
    tut::ensure("the presentation-target fixture resolves", std::holds_alternative<VulkanSwapchainPresentationTargetGeneration>(result));
    return std::get<VulkanSwapchainPresentationTargetGeneration>(std::move(result));
}

struct Parents : ParentBase
{
    Parents(ParentBase&& base, FakeState& state) : ParentBase(std::move(base)), mTarget(makeTarget(*this))
    {
        state.mInstanceLookups.clear();
        state.mDeviceLookups.clear();
        state.mTargetDestroyOrder.clear();
        state.mDestroyedFramebuffers.clear();
        state.mDestroyedRenderPasses.clear();
    }

    VulkanSwapchainPresentationTargetGeneration mTarget;
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

    return Parents(ParentBase{ std::move(physical), std::move(logical), std::move(configuration), std::move(swapchain), std::move(images) },
                   state);
}

const VulkanSwapchainPresentationPipelineResolutionError& requireError(const VulkanSwapchainPresentationPipelineResolutionResult& result)
{
    const auto* error = std::get_if<VulkanSwapchainPresentationPipelineResolutionError>(&result);
    tut::ensure("presentation-pipeline resolution returns an error", error != nullptr);
    return *error;
}

void ensureCode(const VulkanSwapchainPresentationPipelineResolutionResult& result, VulkanSwapchainPresentationPipelineResolutionCode code)
{
    tut::ensure("the exact presentation-pipeline error is reported", requireError(result).mCode == code);
}

VulkanSwapchainPresentationPipelineGeneration takePipeline(VulkanSwapchainPresentationPipelineResolutionResult&& result)
{
    if (const auto* error = std::get_if<VulkanSwapchainPresentationPipelineResolutionError>(&result))
    {
        tut::ensure("presentation-pipeline resolution returns a generation; error code " +
                        std::to_string(static_cast<unsigned int>(error->mCode)),
                    false);
    }
    return std::get<VulkanSwapchainPresentationPipelineGeneration>(std::move(result));
}

VulkanSwapchainPresentationPipelineResolutionResult resolvePipeline(Parents& parents)
{
    return resolveVulkanSwapchainPresentationPipelineGeneration(parents.mLogical, parents.mConfiguration, parents.mSwapchain,
                                                                parents.mImages, parents.mTarget);
}

std::uint64_t shaderChecksum(const std::vector<std::uint32_t>& words) noexcept
{
    std::uint64_t checksum = UINT64_C(14695981039346656037);
    for (const std::uint32_t word : words)
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

struct render_vulkan_swapchain_presentation_pipeline_test
{
};

using render_vulkan_swapchain_presentation_pipeline_group  = test_group<render_vulkan_swapchain_presentation_pipeline_test>;
using render_vulkan_swapchain_presentation_pipeline_object = render_vulkan_swapchain_presentation_pipeline_group::object;
render_vulkan_swapchain_presentation_pipeline_group render_vulkan_swapchain_presentation_pipeline_tests(
    "render Vulkan swapchain presentation pipeline");

template<>
template<>
void render_vulkan_swapchain_presentation_pipeline_object::test<1>()
{
    static_assert(!std::is_default_constructible_v<VulkanSwapchainPresentationPipelineGeneration>);
    static_assert(!std::is_copy_constructible_v<VulkanSwapchainPresentationPipelineGeneration>);
    static_assert(!std::is_copy_assignable_v<VulkanSwapchainPresentationPipelineGeneration>);
    static_assert(std::is_nothrow_move_constructible_v<VulkanSwapchainPresentationPipelineGeneration>);
    static_assert(!std::is_move_assignable_v<VulkanSwapchainPresentationPipelineGeneration>);
    static_assert(std::is_nothrow_destructible_v<VulkanSwapchainPresentationPipelineGeneration>);
    static_assert(std::variant_size_v<VulkanSwapchainPresentationPipelineResolutionResult> == 2);
    static_assert(
        noexcept(resolveVulkanSwapchainPresentationPipelineGeneration(std::declval<const VulkanLogicalDeviceGeneration&>(),
                                                                      std::declval<const VulkanSwapchainConfigurationGeneration&>(),
                                                                      std::declval<const VulkanSwapchainGeneration&>(),
                                                                      std::declval<const VulkanSwapchainImagesGeneration&>(),
                                                                      std::declval<const VulkanSwapchainPresentationTargetGeneration&>())));

    const VulkanSwapchainPresentationPipelineResolutionError value{
        VulkanSwapchainPresentationPipelineResolutionCode::GraphicsPipelineCreationFailure,
        VulkanSwapchainPresentationPipelineCommand::CreateGraphicsPipelines, VK_ERROR_OUT_OF_DEVICE_MEMORY
    };
    ensure("identical typed errors compare equal", value == value);

    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        auto            logical = std::move(parents.mLogical);
        ensureCode(resolvePipeline(parents), VulkanSwapchainPresentationPipelineResolutionCode::InvalidLogicalDeviceGeneration);
        ensure("an invalid logical parent stops before dispatch", state.mInstanceLookups.empty() && state.mDeviceLookups.empty());
        parents.mTarget.reset();
        parents.mImages.reset();
        parents.mSwapchain.reset();
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents       = makeParents(state);
        auto            configuration = std::move(parents.mConfiguration);
        ensureCode(resolvePipeline(parents), VulkanSwapchainPresentationPipelineResolutionCode::InvalidSwapchainConfigurationGeneration);
        ensure("an invalid configuration parent stops before dispatch", state.mInstanceLookups.empty() && state.mDeviceLookups.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents   = makeParents(state);
        auto            swapchain = std::move(parents.mSwapchain);
        ensureCode(resolvePipeline(parents), VulkanSwapchainPresentationPipelineResolutionCode::InvalidSwapchainGeneration);
        ensure("an invalid swapchain parent stops before dispatch", state.mInstanceLookups.empty() && state.mDeviceLookups.empty());
        parents.mTarget.reset();
        parents.mImages.reset();
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        auto            images  = std::move(parents.mImages);
        ensureCode(resolvePipeline(parents), VulkanSwapchainPresentationPipelineResolutionCode::InvalidSwapchainImagesGeneration);
        ensure("an invalid images parent stops before dispatch", state.mInstanceLookups.empty() && state.mDeviceLookups.empty());
        parents.mTarget.reset();
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        auto            target  = std::move(parents.mTarget);
        ensureCode(resolvePipeline(parents),
                   VulkanSwapchainPresentationPipelineResolutionCode::InvalidSwapchainPresentationTargetGeneration);
        ensure("an invalid target parent stops before dispatch", state.mInstanceLookups.empty() && state.mDeviceLookups.empty());
    }
}

template<>
template<>
void render_vulkan_swapchain_presentation_pipeline_object::test<2>()
{
    constexpr std::array cases{
        std::pair{ MissingCommand::GetDeviceProcAddr, VulkanSwapchainPresentationPipelineCommand::GetDeviceProcAddr },
        std::pair{ MissingCommand::CreateShaderModule, VulkanSwapchainPresentationPipelineCommand::CreateShaderModule },
        std::pair{ MissingCommand::DestroyShaderModule, VulkanSwapchainPresentationPipelineCommand::DestroyShaderModule },
        std::pair{ MissingCommand::CreatePipelineLayout, VulkanSwapchainPresentationPipelineCommand::CreatePipelineLayout },
        std::pair{ MissingCommand::DestroyPipelineLayout, VulkanSwapchainPresentationPipelineCommand::DestroyPipelineLayout },
        std::pair{ MissingCommand::CreateGraphicsPipelines, VulkanSwapchainPresentationPipelineCommand::CreateGraphicsPipelines },
        std::pair{ MissingCommand::DestroyPipeline, VulkanSwapchainPresentationPipelineCommand::DestroyPipeline }
    };

    for (std::size_t index = 0; index < cases.size(); ++index)
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents = makeParents(state);
        state.mMissingCommand   = cases[index].first;

        const auto  result = resolvePipeline(parents);
        const auto& error  = requireError(result);
        ensure("missing dispatch reports its exact typed command",
               error.mCode == VulkanSwapchainPresentationPipelineResolutionCode::MissingRequiredCommand &&
                   error.mCommand == cases[index].second);
        ensure("missing dispatch has the exact instance cutoff", state.mInstanceLookups.size() == 1);
        ensure("missing dispatch has the exact device cutoff", state.mDeviceLookups.size() == index);
        ensure("all dispatch resolves before native mutation",
               state.mPipelineMutationOrder.empty() && state.mDestroyedShaderModules.empty() && state.mDestroyedPipelines.empty() &&
                   state.mDestroyedPipelineLayouts.empty());
    }
}

template<>
template<>
void render_vulkan_swapchain_presentation_pipeline_object::test<3>()
{
    FakeState       state;
    ScopedFakeState scope(state);
    auto            parents  = makeParents(state);
    auto            pipeline = takePipeline(resolvePipeline(parents));

    const auto shader_modules = state.defaultShaderModules();
    ensure("all commands resolve before the exact layout-module-module-pipeline mutation order",
           state.mAllPipelineCommandsResolvedBeforeMutation &&
               state.mPipelineMutationOrder == std::vector<std::string>{ "layout", "vertex", "fragment", "pipeline" });
    ensure("the owner publishes the exact layout, pipeline, and live parent chain",
           pipeline.pipelineLayout() == state.mPipelineLayoutOutput && pipeline.pipeline() == state.mGraphicsPipelineOutput &&
               pipeline.createdFor(parents.mLogical, parents.mConfiguration, parents.mSwapchain, parents.mImages, parents.mTarget));

    const auto& layout = state.mPipelineLayoutRecord;
    ensure("the pipeline layout is empty and uses no extension chain or custom allocator",
           layout.mStructureType == VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO && !layout.mNext && layout.mFlags == 0 &&
               layout.mSetLayoutCount == 0 && layout.mPushConstantRangeCount == 0 && layout.mSetLayoutsNull && layout.mPushConstantsNull);

    ensure("the exact deterministic vertex and fragment SPIR-V payloads are deep-copied",
           state.mShaderModuleRecords.size() == 2 &&
               state.mShaderModuleRecords[0].mStructureType == VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO &&
               !state.mShaderModuleRecords[0].mNext && state.mShaderModuleRecords[0].mFlags == 0 &&
               state.mShaderModuleRecords[0].mCode.size() == 184 && state.mShaderModuleRecords[0].mCode[0] == 0x07230203 &&
               shaderChecksum(state.mShaderModuleRecords[0].mCode) == UINT64_C(0x3f7b5ef9a923c49a) &&
               state.mShaderModuleRecords[1].mStructureType == VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO &&
               !state.mShaderModuleRecords[1].mNext && state.mShaderModuleRecords[1].mFlags == 0 &&
               state.mShaderModuleRecords[1].mCode.size() == 76 && state.mShaderModuleRecords[1].mCode[0] == 0x07230203 &&
               shaderChecksum(state.mShaderModuleRecords[1].mCode) == UINT64_C(0xed4aea126bcc6067));

    const auto& record = state.mGraphicsPipelineRecord;
    ensure("the pipeline root uses two main stages, the exact target pass, subpass zero, and no cache or derivative",
           record.mCache == VK_NULL_HANDLE && record.mCreateInfoCount == 1 &&
               record.mStructureType == VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO && !record.mNext && record.mFlags == 0 &&
               record.mStageCount == 2 && record.mStages[0].sType == VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO &&
               !record.mStages[0].pNext && record.mStages[0].flags == 0 && record.mStages[0].stage == VK_SHADER_STAGE_VERTEX_BIT &&
               record.mStages[0].module == shader_modules[0] && record.mEntryPoints[0] == "main" &&
               !record.mStages[0].pSpecializationInfo && record.mStages[1].sType == VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO &&
               !record.mStages[1].pNext && record.mStages[1].flags == 0 && record.mStages[1].stage == VK_SHADER_STAGE_FRAGMENT_BIT &&
               record.mStages[1].module == shader_modules[1] && record.mEntryPoints[1] == "main" &&
               !record.mStages[1].pSpecializationInfo && record.mLayout == state.mPipelineLayoutOutput &&
               record.mRenderPass == parents.mTarget.renderPass() && record.mSubpass == 0 && record.mBasePipeline == VK_NULL_HANDLE &&
               record.mBasePipelineIndex == -1 && record.mAllocatorNull);
    ensure("vertex input, assembly, viewport, tessellation, and dynamic state are exact",
           record.mVertexInput.sType == VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO && !record.mVertexInput.pNext &&
               record.mVertexInput.flags == 0 && record.mVertexInput.vertexBindingDescriptionCount == 0 &&
               !record.mVertexInput.pVertexBindingDescriptions && record.mVertexInput.vertexAttributeDescriptionCount == 0 &&
               !record.mVertexInput.pVertexAttributeDescriptions &&
               record.mInputAssembly.sType == VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO && !record.mInputAssembly.pNext &&
               record.mInputAssembly.flags == 0 && record.mInputAssembly.topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST &&
               record.mInputAssembly.primitiveRestartEnable == VK_FALSE && record.mTessellationNull &&
               record.mViewport.sType == VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO && !record.mViewport.pNext &&
               record.mViewport.flags == 0 && record.mViewport.viewportCount == 1 && !record.mViewport.pViewports &&
               record.mViewport.scissorCount == 1 && !record.mViewport.pScissors &&
               record.mDynamic.sType == VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO && !record.mDynamic.pNext &&
               record.mDynamic.flags == 0 && record.mDynamic.dynamicStateCount == 2 &&
               record.mDynamicStates == std::array<VkDynamicState, 2>{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR });
    ensure("rasterization and multisampling use only fixed Vulkan 1.1 core state",
           record.mRasterization.sType == VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO && !record.mRasterization.pNext &&
               record.mRasterization.flags == 0 && record.mRasterization.depthClampEnable == VK_FALSE &&
               record.mRasterization.rasterizerDiscardEnable == VK_FALSE && record.mRasterization.polygonMode == VK_POLYGON_MODE_FILL &&
               record.mRasterization.cullMode == VK_CULL_MODE_NONE && record.mRasterization.frontFace == VK_FRONT_FACE_COUNTER_CLOCKWISE &&
               record.mRasterization.depthBiasEnable == VK_FALSE && record.mRasterization.depthBiasConstantFactor == 0.0f &&
               record.mRasterization.depthBiasClamp == 0.0f && record.mRasterization.depthBiasSlopeFactor == 0.0f &&
               record.mRasterization.lineWidth == 1.0f &&
               record.mMultisample.sType == VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO && !record.mMultisample.pNext &&
               record.mMultisample.flags == 0 && record.mMultisample.rasterizationSamples == VK_SAMPLE_COUNT_1_BIT &&
               record.mMultisample.sampleShadingEnable == VK_FALSE && record.mMultisample.minSampleShading == 0.0f &&
               !record.mMultisample.pSampleMask && record.mMultisample.alphaToCoverageEnable == VK_FALSE &&
               record.mMultisample.alphaToOneEnable == VK_FALSE && record.mDepthStencilNull);
    ensure("the single color attachment disables blending with explicit replacement factors and writes RGBA",
           record.mColorBlend.sType == VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO && !record.mColorBlend.pNext &&
               record.mColorBlend.flags == 0 && record.mColorBlend.logicOpEnable == VK_FALSE &&
               record.mColorBlend.logicOp == VK_LOGIC_OP_COPY && record.mColorBlend.attachmentCount == 1 &&
               record.mColorAttachment.blendEnable == VK_FALSE && record.mColorAttachment.srcColorBlendFactor == VK_BLEND_FACTOR_ONE &&
               record.mColorAttachment.dstColorBlendFactor == VK_BLEND_FACTOR_ZERO &&
               record.mColorAttachment.colorBlendOp == VK_BLEND_OP_ADD &&
               record.mColorAttachment.srcAlphaBlendFactor == VK_BLEND_FACTOR_ONE &&
               record.mColorAttachment.dstAlphaBlendFactor == VK_BLEND_FACTOR_ZERO &&
               record.mColorAttachment.alphaBlendOp == VK_BLEND_OP_ADD &&
               record.mColorAttachment.colorWriteMask ==
                   (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT) &&
               record.mColorBlend.blendConstants[0] == 0.0f && record.mColorBlend.blendConstants[1] == 0.0f &&
               record.mColorBlend.blendConstants[2] == 0.0f && record.mColorBlend.blendConstants[3] == 0.0f);

    ensure("successful publication destroys transient fragment then vertex modules",
           state.mDestroyedShaderModules == std::vector<VkShaderModule>{ shader_modules[1], shader_modules[0] } &&
               state.mPipelineDestroyOrder == std::vector<std::string>{ "shader", "shader" });
    pipeline.reset();
    ensure("reset destroys pipeline before layout, clears provenance, and uses exact allocator arguments",
           state.mDestroyedPipelines == std::vector<VkPipeline>{ state.mGraphicsPipelineOutput } &&
               state.mDestroyedPipelineLayouts == std::vector<VkPipelineLayout>{ state.mPipelineLayoutOutput } &&
               state.mPipelineDestroyOrder == std::vector<std::string>{ "shader", "shader", "pipeline", "layout" } &&
               state.mPipelineDestroyArgumentsExact && pipeline.pipeline() == VK_NULL_HANDLE &&
               pipeline.pipelineLayout() == VK_NULL_HANDLE &&
               !pipeline.createdFor(parents.mLogical, parents.mConfiguration, parents.mSwapchain, parents.mImages, parents.mTarget));
    pipeline.reset();
    ensure("reset is idempotent", state.mPipelineDestroyOrder.size() == 4);
}

template<>
template<>
void render_vulkan_swapchain_presentation_pipeline_object::test<4>()
{
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents     = makeParents(state);
        const auto      poison      = fakeHandle<VkPipelineLayout>(0xdead);
        state.mPipelineLayoutResult = VK_ERROR_OUT_OF_DEVICE_MEMORY;
        state.mPipelineLayoutOutput = poison;
        const auto  result          = resolvePipeline(parents);
        const auto& error           = requireError(result);
        ensure("failed layout creation preserves the result and ignores its poisoned output",
               error.mCode == VulkanSwapchainPresentationPipelineResolutionCode::PipelineLayoutCreationFailure &&
                   error.mCommand == VulkanSwapchainPresentationPipelineCommand::CreatePipelineLayout &&
                   error.mResult == VK_ERROR_OUT_OF_DEVICE_MEMORY && state.mDestroyedPipelineLayouts.empty() &&
                   state.mShaderModuleRecords.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents     = makeParents(state);
        state.mPipelineLayoutOutput = VK_NULL_HANDLE;
        ensureCode(resolvePipeline(parents), VulkanSwapchainPresentationPipelineResolutionCode::NullPipelineLayoutOnSuccess);
        ensure("null layout success creates nothing else and needs no destroy",
               state.mDestroyedPipelineLayouts.empty() && state.mShaderModuleRecords.empty());
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents    = makeParents(state);
        const auto      poison     = fakeHandle<VkShaderModule>(0xdead);
        state.mShaderModuleResults = { VK_ERROR_OUT_OF_DEVICE_MEMORY };
        state.mShaderModuleOutputs = { poison };
        const auto  result         = resolvePipeline(parents);
        const auto& error          = requireError(result);
        ensure("failed vertex-module creation preserves the result and ignores its poisoned output",
               error.mCode == VulkanSwapchainPresentationPipelineResolutionCode::VertexShaderModuleCreationFailure &&
                   error.mCommand == VulkanSwapchainPresentationPipelineCommand::CreateShaderModule &&
                   error.mResult == VK_ERROR_OUT_OF_DEVICE_MEMORY && state.mDestroyedShaderModules.empty() &&
                   state.mDestroyedPipelineLayouts == std::vector<VkPipelineLayout>{ state.mPipelineLayoutOutput } &&
                   state.mPipelineDestroyOrder == std::vector<std::string>{ "layout" });
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents    = makeParents(state);
        state.mShaderModuleOutputs = { VK_NULL_HANDLE };
        ensureCode(resolvePipeline(parents), VulkanSwapchainPresentationPipelineResolutionCode::NullVertexShaderModuleOnSuccess);
        ensure("null vertex success rolls back only the layout",
               state.mDestroyedShaderModules.empty() && state.mPipelineDestroyOrder == std::vector<std::string>{ "layout" });
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents    = makeParents(state);
        const auto      modules    = state.defaultShaderModules();
        const auto      poison     = fakeHandle<VkShaderModule>(0xdead);
        state.mShaderModuleResults = { VK_SUCCESS, VK_ERROR_OUT_OF_DEVICE_MEMORY };
        state.mShaderModuleOutputs = { modules[0], poison };
        const auto  result         = resolvePipeline(parents);
        const auto& error          = requireError(result);
        ensure("failed fragment-module creation ignores its poison and rolls back vertex then layout",
               error.mCode == VulkanSwapchainPresentationPipelineResolutionCode::FragmentShaderModuleCreationFailure &&
                   error.mResult == VK_ERROR_OUT_OF_DEVICE_MEMORY &&
                   state.mDestroyedShaderModules == std::vector<VkShaderModule>{ modules[0] } &&
                   std::find(state.mDestroyedShaderModules.begin(), state.mDestroyedShaderModules.end(), poison) ==
                       state.mDestroyedShaderModules.end() &&
                   state.mPipelineDestroyOrder == std::vector<std::string>{ "shader", "layout" });
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents    = makeParents(state);
        const auto      modules    = state.defaultShaderModules();
        state.mShaderModuleOutputs = { modules[0], VK_NULL_HANDLE };
        ensureCode(resolvePipeline(parents), VulkanSwapchainPresentationPipelineResolutionCode::NullFragmentShaderModuleOnSuccess);
        ensure("null fragment success rolls back vertex then layout",
               state.mDestroyedShaderModules == std::vector<VkShaderModule>{ modules[0] } &&
                   state.mPipelineDestroyOrder == std::vector<std::string>{ "shader", "layout" });
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents       = makeParents(state);
        const auto      modules       = state.defaultShaderModules();
        const auto      partial       = fakeHandle<VkPipeline>(0xdead);
        state.mGraphicsPipelineResult = VK_ERROR_OUT_OF_DEVICE_MEMORY;
        state.mGraphicsPipelineOutput = partial;
        const auto  result            = resolvePipeline(parents);
        const auto& error             = requireError(result);
        ensure("aggregate pipeline failure preserves its result and destroys the non-null partial output first",
               error.mCode == VulkanSwapchainPresentationPipelineResolutionCode::GraphicsPipelineCreationFailure &&
                   error.mCommand == VulkanSwapchainPresentationPipelineCommand::CreateGraphicsPipelines &&
                   error.mResult == VK_ERROR_OUT_OF_DEVICE_MEMORY && state.mDestroyedPipelines == std::vector<VkPipeline>{ partial } &&
                   state.mDestroyedShaderModules == std::vector<VkShaderModule>{ modules[1], modules[0] } &&
                   state.mDestroyedPipelineLayouts == std::vector<VkPipelineLayout>{ state.mPipelineLayoutOutput } &&
                   state.mPipelineDestroyOrder == std::vector<std::string>{ "pipeline", "shader", "shader", "layout" });
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents       = makeParents(state);
        const auto      modules       = state.defaultShaderModules();
        state.mGraphicsPipelineOutput = VK_NULL_HANDLE;
        ensureCode(resolvePipeline(parents), VulkanSwapchainPresentationPipelineResolutionCode::NullGraphicsPipelineOnSuccess);
        ensure("null pipeline success destroys modules and layout but no pipeline",
               state.mDestroyedPipelines.empty() &&
                   state.mDestroyedShaderModules == std::vector<VkShaderModule>{ modules[1], modules[0] } &&
                   state.mPipelineDestroyOrder == std::vector<std::string>{ "shader", "shader", "layout" });
    }
}

template<>
template<>
void render_vulkan_swapchain_presentation_pipeline_object::test<5>()
{
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents  = makeParents(state);
        auto            pipeline = takePipeline(resolvePipeline(parents));
        auto            moved    = std::move(pipeline);
        ensure("move transfers handles and exact provenance while disarming the source",
               moved.pipelineLayout() == state.mPipelineLayoutOutput && moved.pipeline() == state.mGraphicsPipelineOutput &&
                   moved.createdFor(parents.mLogical, parents.mConfiguration, parents.mSwapchain, parents.mImages, parents.mTarget) &&
                   pipeline.pipelineLayout() == VK_NULL_HANDLE && pipeline.pipeline() == VK_NULL_HANDLE &&
                   !pipeline.createdFor(parents.mLogical, parents.mConfiguration, parents.mSwapchain, parents.mImages, parents.mTarget));

        state.mInstanceLookups.clear();
        state.mDeviceLookups.clear();
        auto second_result = resolveVulkanSwapchainPresentationTargetGeneration(parents.mLogical, parents.mConfiguration,
                                                                                parents.mSwapchain, parents.mImages);
        ensure("a second target fixture resolves", std::holds_alternative<VulkanSwapchainPresentationTargetGeneration>(second_result));
        auto second_target = std::get<VulkanSwapchainPresentationTargetGeneration>(std::move(second_result));
        ensure("an exact-looking but distinct target identity is rejected",
               !moved.createdFor(parents.mLogical, parents.mConfiguration, parents.mSwapchain, parents.mImages, second_target));
        second_target.reset();
        moved.reset();
        ensure("only the moved owner destroys the pipeline and layout once",
               state.mDestroyedPipelines.size() == 1 && state.mDestroyedPipelineLayouts.size() == 1);
    }
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            parents                   = makeParents(state);
        state.mTargetToResetAfterPipelineCreation = &parents.mTarget;
        const auto result                         = resolvePipeline(parents);
        ensureCode(result, VulkanSwapchainPresentationPipelineResolutionCode::ParentGenerationChanged);
        ensure("a parent change before publication rolls back the complete native child",
               parents.mTarget.renderPass() == VK_NULL_HANDLE &&
                   state.mDestroyedPipelines == std::vector<VkPipeline>{ state.mGraphicsPipelineOutput } &&
                   state.mDestroyedPipelineLayouts == std::vector<VkPipelineLayout>{ state.mPipelineLayoutOutput } &&
                   state.mPipelineDestroyOrder == std::vector<std::string>{ "shader", "shader", "pipeline", "layout" });
    }
}

} // namespace tut
