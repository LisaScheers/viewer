/**
 * @file llwindowmacosxvulkan_test.cpp
 * @brief Tests for isolated macOS Vulkan window and loader ownership.
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

#include "lltextureuploaddiagnostic.h"
#include "llwindowmacosxvulkan-objc.h"
#include "llwindowmacosxvulkan.h"
#include "lltut.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{

#if defined(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME)
constexpr std::string_view SURFACE_CAPABILITIES_2_EXTENSION = VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME;
#else
constexpr std::string_view SURFACE_CAPABILITIES_2_EXTENSION = "VK_KHR_get_surface_capabilities2";
#endif
#if defined(VK_KHR_SURFACE_MAINTENANCE_1_EXTENSION_NAME)
constexpr std::string_view SURFACE_MAINTENANCE_EXTENSION = VK_KHR_SURFACE_MAINTENANCE_1_EXTENSION_NAME;
#else
constexpr std::string_view SURFACE_MAINTENANCE_EXTENSION = "VK_KHR_surface_maintenance1";
#endif

enum class Event
{
    OpenLoader,
    GetResolver,
    CreateNative,
    RefreshNative,
    ResizeNative,
    CreateInstance,
    CreateDebugMessenger,
    CreateSurface,
    DestroySurface,
    DestroyDebugMessenger,
    DestroyInstance,
    DestroyNative,
    CloseLoader
};

enum class RefreshMutation
{
    None,
    Token,
    Window,
    View,
    Layer,
    ZeroScale,
    InfiniteScale,
    ZeroWidth,
    ZeroHeight
};

struct FakeState
{
    FakeState()
    {
        mPhysicalProperties.apiVersion = VK_API_VERSION_1_1;
        mPhysicalProperties.deviceType = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
        mPhysicalProperties.limits.maxFramebufferWidth  = 4096;
        mPhysicalProperties.limits.maxFramebufferHeight = 2160;
        mPhysicalProperties.limits.maxViewportDimensions[0] = 4096;
        mPhysicalProperties.limits.maxViewportDimensions[1] = 4096;
        mPhysicalProperties.limits.viewportBoundsRange[0]   = -8192.0f;
        mPhysicalProperties.limits.viewportBoundsRange[1]   = 8191.0f;
        std::memcpy(mPhysicalProperties.deviceName, "macOS adapter fake", sizeof("macOS adapter fake"));
        mSurfaceCapabilities.minImageCount       = 2;
        mSurfaceCapabilities.maxImageCount       = 3;
        mSurfaceCapabilities.currentExtent       = { std::numeric_limits<std::uint32_t>::max(), std::numeric_limits<std::uint32_t>::max() };
        mSurfaceCapabilities.minImageExtent      = { 64, 64 };
        mSurfaceCapabilities.maxImageExtent      = { 4096, 2160 };
        mSurfaceCapabilities.maxImageArrayLayers = 1;
        mSurfaceCapabilities.supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        mSurfaceCapabilities.currentTransform    = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        mSurfaceCapabilities.supportedCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        mSurfaceCapabilities.supportedUsageFlags =
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        mFormatProperties.optimalTilingFeatures = VK_FORMAT_FEATURE_TRANSFER_DST_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
                                                  VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                                                  VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT |
                                                  VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        mMemoryProperties.memoryHeapCount              = 1;
        mMemoryProperties.memoryHeaps[0].size          = 64 * 1024 * 1024;
        mMemoryProperties.memoryTypeCount              = 1;
        mMemoryProperties.memoryTypes[0].propertyFlags =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        mMemoryProperties.memoryTypes[0].heapIndex     = 0;
    }

    std::array<Event, 96> mEvents{};
    std::size_t           mEventCount = 0;

    void* mLoader     = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x1010));
    void* mToken      = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x2020));
    void* mWindow     = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x3030));
    void* mView       = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x4040));
    void* mMetalLayer = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x5050));

    LLWindowMacOSXVulkanNativeWindow                    mNative{ mToken, mWindow, mView, mMetalLayer, 2.0, 1280, 720 };
    std::optional<LLWindowMacOSXVulkanNativeCreateCode> mNativeFailure;
    RefreshMutation                                     mRefreshMutation = RefreshMutation::None;
    bool                                                mRefreshSucceeds = true;
    F64                                                 mRefreshScale    = 2.0;
    U32                                                 mRefreshWidth    = 1280;
    U32                                                 mRefreshHeight   = 720;
    bool                                                mResizeSucceeds  = true;
    RefreshMutation                                     mResizeMutation  = RefreshMutation::None;
    std::size_t                                         mResizeCount     = 0;
    U32                                                 mResizeWidth     = 0;
    U32                                                 mResizeHeight    = 0;

    bool mLoaderOpens  = true;
    bool mResolverLive = true;
    bool mMainThread   = true;
    bool mLoaderLive   = false;
    bool mNativeLive   = false;

    const char*                           mOpenedPath     = nullptr;
    void*                                 mResolverLoader = nullptr;
    const LLWindowMacOSXVulkanCreateInfo* mCreateInfo     = nullptr;
    LLWindowMacOSXVulkanNativeWindow      mDestroyedNative;

    VkInstance               mInstance             = reinterpret_cast<VkInstance>(static_cast<std::uintptr_t>(0x6060));
    VkDebugUtilsMessengerEXT mDebugMessenger       = reinterpret_cast<VkDebugUtilsMessengerEXT>(static_cast<std::uintptr_t>(0x7070));
    VkSurfaceKHR             mSurface              = reinterpret_cast<VkSurfaceKHR>(static_cast<std::uintptr_t>(0x8080));
    VkResult                 mInstanceResult       = VK_SUCCESS;
    bool                     mNullInstance         = false;
    bool                     mExposeDestroySurface = true;

    bool     mSurfacePlatformFailure = false;
    VkResult mSurfaceResult          = VK_SUCCESS;
    bool     mNullSurface            = false;
    bool     mPoisonSurfaceOutput    = false;

    std::size_t                  mRefreshCount         = 0;
    std::size_t                  mCreateSurfaceCount   = 0;
    std::size_t                  mDestroySurfaceCount  = 0;
    std::size_t                  mDestroyDebugCount    = 0;
    std::size_t                  mDestroyInstanceCount = 0;
    LLWindowVulkanFunction       mSurfaceResolver      = nullptr;
    void*                        mSurfaceMetalLayer    = nullptr;
    VkInstance                   mSurfaceInstance      = VK_NULL_HANDLE;
    const VkAllocationCallbacks* mSurfaceAllocator     = reinterpret_cast<const VkAllocationCallbacks*>(1);

    const LLWindowMacOSXVulkan* mOwnerDuringDestroy                     = nullptr;
    bool                        mRequirementsLiveDuringSurfaceDestroy   = false;
    bool                        mInstanceLiveDuringSurfaceDestroy       = false;
    bool                        mNativeLiveDuringSurfaceDestroy         = false;
    bool                        mLoaderLiveDuringSurfaceDestroy         = false;
    bool                        mRequirementsLiveDuringMessengerDestroy = false;
    bool                        mSurfaceAbsentDuringMessengerDestroy    = false;
    bool                        mLoaderLiveDuringMessengerDestroy       = false;
    bool                        mRequirementsLiveDuringInstanceDestroy  = false;
    bool                        mSurfaceAbsentDuringInstanceDestroy     = false;
    bool                        mLoaderLiveDuringInstanceDestroy        = false;
    bool                        mRequirementsGoneBeforeNativeDestroy    = false;
    bool                        mInstanceGoneBeforeNativeDestroy        = false;
    bool                        mLoaderLiveDuringNativeDestroy          = false;
    bool                        mAllOwnersGoneBeforeLoaderClose         = false;
    bool                        mSurfaceCapabilities2Enabled            = false;
    bool                        mSurfaceMaintenanceEnabled              = false;

    VkPhysicalDevice           mPhysicalDevice = reinterpret_cast<VkPhysicalDevice>(static_cast<std::uintptr_t>(0x11110));
    VkPhysicalDeviceProperties mPhysicalProperties{};
    VkFormatProperties         mFormatProperties{};
    VkDevice                   mDevice    = reinterpret_cast<VkDevice>(static_cast<std::uintptr_t>(0x22220));
    VkQueue                    mQueue     = reinterpret_cast<VkQueue>(static_cast<std::uintptr_t>(0x33330));
    VkSwapchainKHR             mSwapchain = reinterpret_cast<VkSwapchainKHR>(static_cast<std::uintptr_t>(0x44440));
    VkSurfaceCapabilitiesKHR   mSurfaceCapabilities{};
    std::array<VkImage, 3>     mImages{ reinterpret_cast<VkImage>(static_cast<std::uintptr_t>(0x51000)),
                                    reinterpret_cast<VkImage>(static_cast<std::uintptr_t>(0x52000)),
                                    reinterpret_cast<VkImage>(static_cast<std::uintptr_t>(0x53000)) };
    std::array<VkImageView, 3> mImageViews{ reinterpret_cast<VkImageView>(static_cast<std::uintptr_t>(0x61000)),
                                            reinterpret_cast<VkImageView>(static_cast<std::uintptr_t>(0x62000)),
                                            reinterpret_cast<VkImageView>(static_cast<std::uintptr_t>(0x63000)) };
    std::size_t                mNextImageView              = 0;
    VkCommandPool              mCommandPool                = reinterpret_cast<VkCommandPool>(static_cast<std::uintptr_t>(0x71000));
    VkCommandBuffer            mCommandBuffer              = reinterpret_cast<VkCommandBuffer>(static_cast<std::uintptr_t>(0x72000));
    VkSemaphore                mImageAvailableSemaphore    = reinterpret_cast<VkSemaphore>(static_cast<std::uintptr_t>(0x73000));
    VkFence                    mSubmissionFence            = reinterpret_cast<VkFence>(static_cast<std::uintptr_t>(0x74000));
    VkSemaphore                mPresentationReadySemaphore = reinterpret_cast<VkSemaphore>(static_cast<std::uintptr_t>(0x75000));
    VkFence                    mPresentCompletionFence     = reinterpret_cast<VkFence>(static_cast<std::uintptr_t>(0x76000));
    std::size_t                mCreateSwapchainCount       = 0;
    std::size_t                mDestroySwapchainCount      = 0;
    VkSwapchainKHR             mLastOldSwapchain           = reinterpret_cast<VkSwapchainKHR>(static_cast<std::uintptr_t>(1));
    VkExtent2D                 mLastSwapchainExtent{};
    VkImageUsageFlags          mLastSwapchainUsage         = 0;
    std::size_t                mCreateImageViewCount       = 0;
    std::size_t                mDestroyImageViewCount      = 0;
    std::size_t                mCreateRenderPassCount      = 0;
    std::size_t                mDestroyRenderPassCount     = 0;
    std::size_t                      mPresentationRenderPassDestroyOrder = 0;
    std::size_t                mCreateFramebufferCount     = 0;
    std::size_t                mDestroyFramebufferCount    = 0;
    std::size_t                mCreateShaderModuleCount    = 0;
    std::size_t                mDestroyShaderModuleCount   = 0;
    std::size_t                mCreatePipelineLayoutCount  = 0;
    std::size_t                mDestroyPipelineLayoutCount = 0;
    std::size_t                mCreatePipelineCount        = 0;
    std::size_t                      mDestroyPipelineCount       = 0;
    VkPipelineLayout                 mLastPipelineLayout         = VK_NULL_HANDLE;
    VkPipeline                       mLastPipeline               = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties mMemoryProperties{};
    VkBuffer                         mReadbackBuffer          = reinterpret_cast<VkBuffer>(static_cast<std::uintptr_t>(0x81000));
    VkDeviceMemory                   mReadbackMemory          = reinterpret_cast<VkDeviceMemory>(static_cast<std::uintptr_t>(0x82000));
    VkBuffer                         mUploadSourceBuffer      = reinterpret_cast<VkBuffer>(static_cast<std::uintptr_t>(0x83000));
    VkDeviceMemory                   mUploadSourceMemory      = reinterpret_cast<VkDeviceMemory>(static_cast<std::uintptr_t>(0x84000));
    VkBuffer                         mUploadDestinationBuffer = reinterpret_cast<VkBuffer>(static_cast<std::uintptr_t>(0x85000));
    VkDeviceMemory                   mUploadDestinationMemory = reinterpret_cast<VkDeviceMemory>(static_cast<std::uintptr_t>(0x86000));
    VkBuffer                         mTextureUploadSourceBuffer = reinterpret_cast<VkBuffer>(static_cast<std::uintptr_t>(0x8a000));
    VkDeviceMemory                   mTextureUploadSourceMemory = reinterpret_cast<VkDeviceMemory>(static_cast<std::uintptr_t>(0x8b000));
    VkBuffer                         mLastCreatedBuffer       = VK_NULL_HANDLE;
    VkBufferCreateInfo               mTextureUploadSourceCreateInfo{};
    VkMemoryRequirements             mTextureUploadSourceMemoryRequirements{ 256, 64, 1 };
    VkImage                mTextureUploadDestinationImage = reinterpret_cast<VkImage>(static_cast<std::uintptr_t>(0x87000));
    VkDeviceMemory         mTextureUploadDestinationMemory = reinterpret_cast<VkDeviceMemory>(static_cast<std::uintptr_t>(0x88000));
    VkImageView            mTextureUploadDestinationImageView = reinterpret_cast<VkImageView>(static_cast<std::uintptr_t>(0x89000));
    VkMemoryRequirements   mTextureUploadDestinationMemoryRequirements{ 4096, 256, 1 };
    VkImageCreateInfo      mTextureUploadDestinationCreateInfo{};
    VkImageViewCreateInfo  mTextureUploadDestinationViewCreateInfo{};
    bool                   mTextureUploadDestinationDedicatedAllocationExact = false;
    std::size_t            mTextureUploadDestinationCreateCount              = 0;
    std::size_t            mTextureUploadDestinationDestroyCount             = 0;
    std::size_t            mTextureUploadDestinationRequirementsCount        = 0;
    std::size_t            mTextureUploadDestinationAllocateCount            = 0;
    std::size_t            mTextureUploadDestinationBindCount                = 0;
    std::size_t            mTextureUploadDestinationViewCreateCount          = 0;
    std::size_t            mTextureUploadDestinationViewDestroyCount         = 0;
    std::size_t            mTextureUploadDestinationFreeCount                = 0;
    std::size_t            mTextureUploadDestinationDestroySequence          = 0;
    std::size_t            mTextureUploadDestinationViewDestroyOrder         = 0;
    std::size_t            mTextureUploadDestinationImageDestroyOrder        = 0;
    std::size_t            mTextureUploadDestinationMemoryFreeOrder          = 0;
    std::array<std::uint8_t, LLRenderVulkan::VULKAN_UPLOAD_SOURCE_BYTE_COUNT> mUploadMappedBytes{};
    std::array<std::uint8_t, LLRenderContract::TEXTURE_UPLOAD_SOURCE_BYTE_COUNT> mTextureUploadSourceMappedBytes{};
    std::uint8_t                                                              mReadbackMappedByte            = 0;
    VkDeviceSize                                                              mReadbackBufferSize            = 0;
    std::size_t                                                               mCreateBufferCount             = 0;
    std::size_t                                                               mAllocateMemoryCount           = 0;
    std::size_t                                                               mMapMemoryCount                = 0;
    std::size_t                                                               mUnmapMemoryCount              = 0;
    std::size_t                                                               mDestroyBufferCount            = 0;
    std::size_t                                                               mFreeMemoryCount               = 0;
    std::size_t                                                               mUploadSourceDestroyCount      = 0;
    std::size_t                                                               mUploadSourceFreeCount         = 0;
    std::size_t                                                                  mTextureUploadSourceMapCount        = 0;
    std::size_t                                                                  mTextureUploadSourceFlushCount      = 0;
    std::size_t                                                                  mTextureUploadSourceUnmapCount      = 0;
    std::size_t                                                                  mTextureUploadSourceDestroyCount    = 0;
    std::size_t                                                                  mTextureUploadSourceFreeCount       = 0;
    std::size_t                                                                  mTextureUploadSourceDestroyOrder    = 0;
    std::size_t                                                                  mTextureUploadSourceMemoryFreeOrder = 0;
    std::size_t                                                                  mTextureUploadCopyCalls             = 0;
    std::size_t                                                                  mTextureUploadBlitCalls             = 0;
    std::size_t                                                                  mTextureUploadImageBarrierCount     = 0;
    VkSampler             mTextureUploadSampleBindingSampler = reinterpret_cast<VkSampler>(static_cast<std::uintptr_t>(0x8c000));
    VkDescriptorSetLayout mTextureUploadSampleBindingDescriptorSetLayout =
        reinterpret_cast<VkDescriptorSetLayout>(static_cast<std::uintptr_t>(0x8d000));
    VkPipelineLayout mTextureUploadSampleBindingPipelineLayout = reinterpret_cast<VkPipelineLayout>(static_cast<std::uintptr_t>(0x8e000));
    VkDescriptorPool mTextureUploadSampleBindingDescriptorPool = reinterpret_cast<VkDescriptorPool>(static_cast<std::uintptr_t>(0x8f000));
    VkDescriptorSet  mTextureUploadSampleBindingDescriptorSet  = reinterpret_cast<VkDescriptorSet>(static_cast<std::uintptr_t>(0x90000));
    VkPipeline       mTextureUploadSamplePipeline              = VK_NULL_HANDLE;
    VkPipelineLayout mTextureUploadSamplePipelineLayout        = VK_NULL_HANDLE;
    VkRenderPass     mTextureUploadSamplePipelineRenderPass    = VK_NULL_HANDLE;
    VkSamplerCreateInfo             mTextureUploadSampleBindingSamplerCreateInfo{};
    VkDescriptorSetLayoutCreateInfo mTextureUploadSampleBindingDescriptorSetLayoutCreateInfo{};
    VkDescriptorSetLayoutBinding    mTextureUploadSampleBindingDescriptorSetLayoutBinding{};
    VkPipelineLayoutCreateInfo      mTextureUploadSampleBindingPipelineLayoutCreateInfo{};
    VkDescriptorSetLayout           mTextureUploadSampleBindingPipelineSetLayout = VK_NULL_HANDLE;
    VkDescriptorPoolCreateInfo      mTextureUploadSampleBindingDescriptorPoolCreateInfo{};
    VkDescriptorPoolSize            mTextureUploadSampleBindingDescriptorPoolSize{};
    VkDescriptorSetAllocateInfo     mTextureUploadSampleBindingDescriptorSetAllocateInfo{};
    VkDescriptorSetLayout           mTextureUploadSampleBindingAllocatedSetLayout = VK_NULL_HANDLE;
    VkWriteDescriptorSet            mTextureUploadSampleBindingDescriptorWrite{};
    VkDescriptorImageInfo           mTextureUploadSampleBindingDescriptorImageInfo{};
    std::size_t                     mTextureUploadSampleBindingSamplerCreateCount                                    = 0;
    std::size_t                     mTextureUploadSampleBindingSamplerDestroyCount                                   = 0;
    std::size_t                     mTextureUploadSampleBindingDescriptorSetLayoutCreateCount                        = 0;
    std::size_t                     mTextureUploadSampleBindingDescriptorSetLayoutDestroyCount                       = 0;
    std::size_t                     mTextureUploadSampleBindingPipelineLayoutCreateCount                             = 0;
    std::size_t                     mTextureUploadSampleBindingPipelineLayoutDestroyCount                            = 0;
    std::size_t                     mTextureUploadSampleBindingDescriptorPoolCreateCount                             = 0;
    std::size_t                     mTextureUploadSampleBindingDescriptorPoolDestroyCount                            = 0;
    std::size_t                     mTextureUploadSampleBindingDescriptorSetAllocateCount                            = 0;
    std::size_t                     mTextureUploadSampleBindingDescriptorUpdateCount                                 = 0;
    std::size_t                     mTextureUploadSampleBindingDescriptorPoolDestroyOrder                            = 0;
    std::size_t                     mTextureUploadSampleBindingPipelineLayoutDestroyOrder                            = 0;
    std::size_t                     mTextureUploadSampleBindingDescriptorSetLayoutDestroyOrder                       = 0;
    std::size_t                     mTextureUploadSampleBindingSamplerDestroyOrder                                   = 0;
    std::size_t                     mTextureUploadSamplePipelineCreateCount                                          = 0;
    std::size_t                     mTextureUploadSamplePipelineDestroyCount                                         = 0;
    std::size_t                     mTextureUploadSamplePipelineDestroyOrder                                         = 0;
    std::size_t                                                                  mLogicalDeviceDestroyOrder          = 0;
    std::size_t                                                               mUploadDestinationDestroyCount = 0;
    std::size_t                                                               mUploadDestinationFreeCount    = 0;
    std::size_t                      mCreateCommandPoolCount  = 0;
    std::size_t                      mDestroyCommandPoolCount = 0;
    std::size_t                      mCreateSemaphoreCount    = 0;
    std::size_t                mDestroySemaphoreCount      = 0;
    std::size_t                mCreateFenceCount           = 0;
    std::size_t                mDestroyFenceCount          = 0;
    std::size_t                mWaitForFencesCount         = 0;
    std::size_t                mQueueSubmitCount           = 0;
    VkPipelineStageFlags       mSubmitWaitStage            = 0;
    std::size_t                mAcquireNextImageCount      = 0;
    std::uint32_t              mAcquiredImageIndex         = 0;
    std::size_t                mPipelineBarrierCount       = 0;
    std::size_t                                                               mCopyBufferCount               = 0;
    bool                                                                      mInvalidCopyBuffer             = false;
    std::size_t                mClearColorImageCount       = 0;
    std::size_t                mBeginRenderPassCount       = 0;
    std::size_t                mEndRenderPassCount         = 0;
    VkCommandBuffer            mRenderPassCommandBuffer    = VK_NULL_HANDLE;
    VkRenderPass               mRenderPass                 = VK_NULL_HANDLE;
    VkFramebuffer              mRenderPassFramebuffer      = VK_NULL_HANDLE;
    VkRect2D                   mRenderPassArea{};
    VkClearValue               mRenderPassClear{};
    VkSubpassContents          mRenderPassContents         = VK_SUBPASS_CONTENTS_MAX_ENUM;
    std::size_t                mBindPipelineCount          = 0;
    std::size_t                                                               mBindVertexBuffersCount     = 0;
    std::size_t                mSetViewportCount           = 0;
    std::size_t                mSetScissorCount            = 0;
    std::size_t                mDrawCount                  = 0;
    VkCommandBuffer            mDrawCommandBuffer          = VK_NULL_HANDLE;
    VkPipelineBindPoint        mPipelineBindPoint          = VK_PIPELINE_BIND_POINT_MAX_ENUM;
    VkPipeline                 mBoundPipeline              = VK_NULL_HANDLE;
    VkBuffer                                                                  mBoundVertexBuffer          = VK_NULL_HANDLE;
    VkDeviceSize                                                              mBoundVertexOffset = std::numeric_limits<VkDeviceSize>::max();
    std::uint32_t              mFirstVertexBinding         = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t              mVertexBindingCount         = 0;
    std::size_t                mCommandOrder               = 0;
    std::size_t                mBindPipelineOrder          = 0;
    std::size_t                mBindVertexBuffersOrder     = 0;
    std::size_t                mDrawOrder                  = 0;
    std::uint32_t              mFirstViewport              = std::numeric_limits<std::uint32_t>::max();
    VkViewport                 mViewport{};
    std::uint32_t              mFirstScissor = std::numeric_limits<std::uint32_t>::max();
    VkRect2D                   mScissor{};
    std::uint32_t              mDrawVertexCount            = 0;
    std::uint32_t              mDrawInstanceCount          = 0;
    std::uint32_t              mDrawFirstVertex            = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t              mDrawFirstInstance          = std::numeric_limits<std::uint32_t>::max();
    VkCommandBuffer            mClearCommandBuffer         = VK_NULL_HANDLE;
    VkImage                    mClearedImage               = VK_NULL_HANDLE;
    VkImageLayout              mClearImageLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
    VkClearColorValue          mClearColor{};
    VkImageSubresourceRange    mClearRange{};
    std::size_t                mQueuePresentCount           = 0;
    std::size_t                mReleaseSwapchainImagesCount = 0;

    void record(Event event) noexcept
    {
        if (mEventCount < mEvents.size())
        {
            mEvents[mEventCount++] = event;
        }
    }
};

FakeState* gState = nullptr;

class ScopedState
{
public:
    explicit ScopedState(FakeState& state) noexcept { gState = &state; }
    ~ScopedState() noexcept { gState = nullptr; }

    ScopedState(const ScopedState&)            = delete;
    ScopedState& operator=(const ScopedState&) = delete;

    void use(FakeState& state) noexcept { gState = &state; }
};

template<typename Function>
PFN_vkVoidFunction eraseFunctionType(Function function) noexcept
{
    return reinterpret_cast<PFN_vkVoidFunction>(function);
}

VKAPI_ATTR VkResult VKAPI_CALL fakeEnumerateInstanceVersion(std::uint32_t* version) noexcept
{
    if (!gState || !version)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    *version = VK_API_VERSION_1_1;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeEnumerateInstanceExtensionProperties(const char*,
                                                                        std::uint32_t*         count,
                                                                        VkExtensionProperties* properties) noexcept
{
    if (!gState || !count)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    constexpr std::array<const char*, 6> extensions{ VK_KHR_SURFACE_EXTENSION_NAME,      "VK_EXT_metal_surface",
                                                     VK_EXT_DEBUG_UTILS_EXTENSION_NAME,  "VK_KHR_portability_enumeration",
                                                     "VK_KHR_get_surface_capabilities2", "VK_KHR_surface_maintenance1" };
    if (!properties)
    {
        *count = static_cast<std::uint32_t>(extensions.size());
        return VK_SUCCESS;
    }

    const std::size_t copied = std::min<std::size_t>(*count, extensions.size());
    for (std::size_t index = 0; index < copied; ++index)
    {
        std::memcpy(properties[index].extensionName, extensions[index], std::strlen(extensions[index]) + 1);
    }
    *count = static_cast<std::uint32_t>(copied);
    return copied == extensions.size() ? VK_SUCCESS : VK_INCOMPLETE;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeEnumerateInstanceLayerProperties(std::uint32_t* count, VkLayerProperties* properties) noexcept
{
    if (!gState || !count)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!properties)
    {
        *count = 1;
        return VK_SUCCESS;
    }
    if (*count == 0)
    {
        return VK_INCOMPLETE;
    }
    constexpr char layer[] = "VK_LAYER_KHRONOS_validation";
    std::memcpy(properties[0].layerName, layer, sizeof(layer));
    *count = 1;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateInstance(const VkInstanceCreateInfo* create_info,
                                                  const VkAllocationCallbacks*,
                                                  VkInstance* instance) noexcept
{
    if (!gState || !create_info || !instance)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    gState->record(Event::CreateInstance);
    gState->mSurfaceCapabilities2Enabled = false;
    gState->mSurfaceMaintenanceEnabled   = false;
    for (std::uint32_t index = 0; index < create_info->enabledExtensionCount; ++index)
    {
        if (create_info->ppEnabledExtensionNames[index] == SURFACE_CAPABILITIES_2_EXTENSION)
        {
            gState->mSurfaceCapabilities2Enabled = true;
        }
        if (create_info->ppEnabledExtensionNames[index] == SURFACE_MAINTENANCE_EXTENSION)
        {
            gState->mSurfaceMaintenanceEnabled = true;
        }
    }
    *instance = gState->mNullInstance ? VK_NULL_HANDLE : gState->mInstance;
    return gState->mInstanceResult;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyInstance(VkInstance instance, const VkAllocationCallbacks*) noexcept
{
    if (!gState || instance != gState->mInstance)
    {
        return;
    }
    gState->record(Event::DestroyInstance);
    ++gState->mDestroyInstanceCount;
    if (gState->mOwnerDuringDestroy)
    {
        gState->mRequirementsLiveDuringInstanceDestroy = gState->mOwnerDuringDestroy->hasRequirements();
        gState->mSurfaceAbsentDuringInstanceDestroy    = gState->mDestroySurfaceCount == 1;
    }
    gState->mLoaderLiveDuringInstanceDestroy = gState->mLoaderLive;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateDebugUtilsMessenger(VkInstance instance,
                                                             const VkDebugUtilsMessengerCreateInfoEXT*,
                                                             const VkAllocationCallbacks*,
                                                             VkDebugUtilsMessengerEXT* messenger) noexcept
{
    if (!gState || instance != gState->mInstance || !messenger)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    gState->record(Event::CreateDebugMessenger);
    *messenger = gState->mDebugMessenger;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyDebugUtilsMessenger(VkInstance               instance,
                                                          VkDebugUtilsMessengerEXT messenger,
                                                          const VkAllocationCallbacks*) noexcept
{
    if (!gState || instance != gState->mInstance || messenger != gState->mDebugMessenger)
    {
        return;
    }
    gState->record(Event::DestroyDebugMessenger);
    ++gState->mDestroyDebugCount;
    if (gState->mOwnerDuringDestroy)
    {
        gState->mRequirementsLiveDuringMessengerDestroy = gState->mOwnerDuringDestroy->hasRequirements();
        gState->mSurfaceAbsentDuringMessengerDestroy    = gState->mDestroySurfaceCount == 1;
    }
    gState->mLoaderLiveDuringMessengerDestroy = gState->mLoaderLive;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroySurface(VkInstance instance, VkSurfaceKHR surface, const VkAllocationCallbacks* allocator) noexcept
{
    if (!gState || instance != gState->mInstance || surface != gState->mSurface || allocator)
    {
        return;
    }
    gState->record(Event::DestroySurface);
    ++gState->mDestroySurfaceCount;
    if (gState->mOwnerDuringDestroy)
    {
        gState->mRequirementsLiveDuringSurfaceDestroy = gState->mOwnerDuringDestroy->hasRequirements();
        gState->mInstanceLiveDuringSurfaceDestroy     = instance == gState->mInstance;
        gState->mNativeLiveDuringSurfaceDestroy       = gState->mOwnerDuringDestroy->hasNativeWindow();
    }
    gState->mLoaderLiveDuringSurfaceDestroy = gState->mLoaderLive;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeEnumeratePhysicalDevices(VkInstance instance,
                                                             std::uint32_t* count,
                                                             VkPhysicalDevice* devices) noexcept
{
    if (!gState || instance != gState->mInstance || !count)
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
    devices[0] = gState->mPhysicalDevice;
    *count     = 1;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeGetPhysicalDeviceProperties(VkPhysicalDevice device,
                                                            VkPhysicalDeviceProperties* properties) noexcept
{
    if (gState && device == gState->mPhysicalDevice && properties)
    {
        *properties = gState->mPhysicalProperties;
    }
}

VKAPI_ATTR void VKAPI_CALL fakeGetPhysicalDeviceFormatProperties(VkPhysicalDevice    device,
                                                                  VkFormat            format,
                                                                  VkFormatProperties* properties) noexcept
{
    if (gState && device == gState->mPhysicalDevice &&
        (format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_R8G8B8A8_UNORM) && properties)
    {
        *properties = gState->mFormatProperties;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeGetPhysicalDeviceImageFormatProperties(VkPhysicalDevice device, VkFormat format, VkImageType type,
                                                                          VkImageTiling tiling, VkImageUsageFlags usage,
                                                                          VkImageCreateFlags       flags,
                                                                          VkImageFormatProperties* properties) noexcept
{
    if (!gState || device != gState->mPhysicalDevice || format != VK_FORMAT_R8G8B8A8_UNORM || type != VK_IMAGE_TYPE_2D ||
        tiling != VK_IMAGE_TILING_OPTIMAL ||
        usage != (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT) || flags != 0 ||
        !properties)
    {
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    }
    *properties = { { 4096, 4096, 1 }, LLRenderContract::TEXTURE_UPLOAD_MIP_LEVELS, 1, VK_SAMPLE_COUNT_1_BIT,
                    64 * 1024 * 1024 };
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice         device,
                                                                      std::uint32_t*           count,
                                                                      VkQueueFamilyProperties* properties) noexcept
{
    if (!gState || device != gState->mPhysicalDevice || !count)
    {
        return;
    }
    if (!properties)
    {
        *count = 1;
        return;
    }
    if (*count != 0)
    {
        properties[0]            = {};
        properties[0].queueFlags = VK_QUEUE_GRAPHICS_BIT;
        properties[0].queueCount = 1;
        *count                   = 1;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeGetPhysicalDeviceSurfaceSupport(VkPhysicalDevice device,
                                                                   std::uint32_t    queue_family,
                                                                   VkSurfaceKHR     surface,
                                                                   VkBool32*        supported) noexcept
{
    if (!gState || device != gState->mPhysicalDevice || queue_family != 0 || surface != gState->mSurface || !supported)
    {
        return VK_ERROR_SURFACE_LOST_KHR;
    }
    *supported = VK_TRUE;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeEnumerateDeviceExtensionProperties(VkPhysicalDevice       device,
                                                                      const char*            layer,
                                                                      std::uint32_t*         count,
                                                                      VkExtensionProperties* properties) noexcept
{
    if (!gState || device != gState->mPhysicalDevice || layer || !count)
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
    std::memcpy(properties[0].extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME, sizeof(VK_KHR_SWAPCHAIN_EXTENSION_NAME));
    properties[1] = {};
    std::memcpy(properties[1].extensionName, VK_KHR_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME,
                sizeof(VK_KHR_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME));
    *count = 2;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeGetPhysicalDeviceFeatures2(VkPhysicalDevice physical_device,
                                                           VkPhysicalDeviceFeatures2* features) noexcept
{
    if (!gState || physical_device != gState->mPhysicalDevice || !features)
    {
        return;
    }
    for (VkBaseOutStructure* extension = static_cast<VkBaseOutStructure*>(features->pNext); extension; extension = extension->pNext)
    {
        if (extension->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_KHR)
        {
            reinterpret_cast<VkPhysicalDeviceSwapchainMaintenance1FeaturesKHR*>(extension)->swapchainMaintenance1 = VK_TRUE;
        }
    }
}

VKAPI_ATTR void VKAPI_CALL fakeGetPhysicalDeviceFeatures(VkPhysicalDevice device, VkPhysicalDeviceFeatures* features) noexcept
{
    if (gState && device == gState->mPhysicalDevice && features)
    {
        *features                  = {};
        features->independentBlend = VK_TRUE;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateDevice(VkPhysicalDevice device,
                                                const VkDeviceCreateInfo*,
                                                const VkAllocationCallbacks*,
                                                VkDevice* logical_device) noexcept
{
    if (!gState || device != gState->mPhysicalDevice || !logical_device)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    *logical_device = gState->mDevice;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyDevice(VkDevice device, const VkAllocationCallbacks*) noexcept
{
    if (gState && device == gState->mDevice)
    {
        gState->mLogicalDeviceDestroyOrder = ++gState->mTextureUploadDestinationDestroySequence;
    }
}

VKAPI_ATTR void VKAPI_CALL fakeGetDeviceQueue(VkDevice      device,
                                              std::uint32_t queue_family,
                                              std::uint32_t queue_index,
                                              VkQueue*      queue) noexcept
{
    if (gState && device == gState->mDevice && queue_family == 0 && queue_index == 0 && queue)
    {
        *queue = gState->mQueue;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeGetSurfaceCapabilities(VkPhysicalDevice          device,
                                                          VkSurfaceKHR              surface,
                                                          VkSurfaceCapabilitiesKHR* capabilities) noexcept
{
    if (!gState || device != gState->mPhysicalDevice || surface != gState->mSurface || !capabilities)
    {
        return VK_ERROR_SURFACE_LOST_KHR;
    }
    *capabilities = gState->mSurfaceCapabilities;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeGetSurfaceFormats(VkPhysicalDevice    device,
                                                     VkSurfaceKHR        surface,
                                                     std::uint32_t*      count,
                                                     VkSurfaceFormatKHR* formats) noexcept
{
    if (!gState || device != gState->mPhysicalDevice || surface != gState->mSurface || !count)
    {
        return VK_ERROR_SURFACE_LOST_KHR;
    }
    if (!formats)
    {
        *count = 1;
        return VK_SUCCESS;
    }
    if (*count == 0)
    {
        return VK_INCOMPLETE;
    }
    formats[0] = { VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
    *count     = 1;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeGetSurfacePresentModes(VkPhysicalDevice  device,
                                                          VkSurfaceKHR      surface,
                                                          std::uint32_t*    count,
                                                          VkPresentModeKHR* modes) noexcept
{
    if (!gState || device != gState->mPhysicalDevice || surface != gState->mSurface || !count)
    {
        return VK_ERROR_SURFACE_LOST_KHR;
    }
    if (!modes)
    {
        *count = 1;
        return VK_SUCCESS;
    }
    if (*count == 0)
    {
        return VK_INCOMPLETE;
    }
    modes[0] = VK_PRESENT_MODE_FIFO_KHR;
    *count   = 1;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateSwapchain(VkDevice device,
                                                   const VkSwapchainCreateInfoKHR* create_info,
                                                   const VkAllocationCallbacks*,
                                                   VkSwapchainKHR* swapchain) noexcept
{
    if (!gState || device != gState->mDevice || !create_info || !swapchain)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    ++gState->mCreateSwapchainCount;
    gState->mLastOldSwapchain    = create_info->oldSwapchain;
    gState->mLastSwapchainExtent = create_info->imageExtent;
    gState->mLastSwapchainUsage  = create_info->imageUsage;
    *swapchain                   = gState->mSwapchain;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroySwapchain(VkDevice device,
                                                VkSwapchainKHR swapchain,
                                                const VkAllocationCallbacks*) noexcept
{
    if (gState && device == gState->mDevice && swapchain == gState->mSwapchain)
    {
        ++gState->mDestroySwapchainCount;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeGetSwapchainImages(VkDevice       device,
                                                      VkSwapchainKHR swapchain,
                                                      std::uint32_t* count,
                                                      VkImage*       images) noexcept
{
    if (!gState || device != gState->mDevice || swapchain != gState->mSwapchain || !count)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!images)
    {
        *count = static_cast<std::uint32_t>(gState->mImages.size());
        return VK_SUCCESS;
    }
    const std::size_t written = std::min<std::size_t>(*count, gState->mImages.size());
    std::copy_n(gState->mImages.begin(), written, images);
    *count = static_cast<std::uint32_t>(written);
    return written == gState->mImages.size() ? VK_SUCCESS : VK_INCOMPLETE;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateImageView(VkDevice device,
                                                   const VkImageViewCreateInfo* create_info,
                                                   const VkAllocationCallbacks*,
                                                   VkImageView* image_view) noexcept
{
    if (!gState || device != gState->mDevice || !create_info || !image_view)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (create_info->image == gState->mTextureUploadDestinationImage)
    {
        gState->mTextureUploadDestinationViewCreateInfo = *create_info;
        *image_view                                     = gState->mTextureUploadDestinationImageView;
        ++gState->mTextureUploadDestinationViewCreateCount;
        return VK_SUCCESS;
    }
    *image_view = gState->mImageViews[gState->mNextImageView++ % gState->mImageViews.size()];
    ++gState->mCreateImageViewCount;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyImageView(VkDevice device,
                                                VkImageView image_view,
                                                const VkAllocationCallbacks*) noexcept
{
    if (gState && device == gState->mDevice && image_view == gState->mTextureUploadDestinationImageView)
    {
        ++gState->mTextureUploadDestinationViewDestroyCount;
        gState->mTextureUploadDestinationViewDestroyOrder = ++gState->mTextureUploadDestinationDestroySequence;
    }
    else if (gState && device == gState->mDevice && image_view != VK_NULL_HANDLE)
    {
        ++gState->mDestroyImageViewCount;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateImage(VkDevice device, const VkImageCreateInfo* create_info,
                                                const VkAllocationCallbacks*, VkImage* image) noexcept
{
    if (!gState || device != gState->mDevice || !create_info || !image)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    gState->mTextureUploadDestinationCreateInfo = *create_info;
    ++gState->mTextureUploadDestinationCreateCount;
    *image = gState->mTextureUploadDestinationImage;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks*) noexcept
{
    if (gState && device == gState->mDevice && image == gState->mTextureUploadDestinationImage)
    {
        ++gState->mTextureUploadDestinationDestroyCount;
        gState->mTextureUploadDestinationImageDestroyOrder = ++gState->mTextureUploadDestinationDestroySequence;
    }
}

VKAPI_ATTR void VKAPI_CALL fakeGetImageMemoryRequirements2(VkDevice device,
                                                            const VkImageMemoryRequirementsInfo2* info,
                                                            VkMemoryRequirements2* requirements) noexcept
{
    if (!gState || device != gState->mDevice || !info || info->sType != VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2 ||
        info->pNext != nullptr || info->image != gState->mTextureUploadDestinationImage || !requirements ||
        requirements->sType != VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2)
    {
        return;
    }
    auto* dedicated = static_cast<VkMemoryDedicatedRequirements*>(requirements->pNext);
    if (!dedicated || dedicated->sType != VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS || dedicated->pNext != nullptr)
    {
        return;
    }
    ++gState->mTextureUploadDestinationRequirementsCount;
    requirements->memoryRequirements           = gState->mTextureUploadDestinationMemoryRequirements;
    dedicated->prefersDedicatedAllocation      = VK_TRUE;
    dedicated->requiresDedicatedAllocation     = VK_FALSE;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeBindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory,
                                                    VkDeviceSize offset) noexcept
{
    if (!gState || device != gState->mDevice || image != gState->mTextureUploadDestinationImage ||
        memory != gState->mTextureUploadDestinationMemory || offset != 0)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    ++gState->mTextureUploadDestinationBindCount;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateRenderPass(VkDevice device,
                                                     const VkRenderPassCreateInfo*,
                                                     const VkAllocationCallbacks*,
                                                     VkRenderPass* render_pass) noexcept
{
    if (!gState || device != gState->mDevice || !render_pass)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    ++gState->mCreateRenderPassCount;
    *render_pass = reinterpret_cast<VkRenderPass>(static_cast<std::uintptr_t>(0x80000 + gState->mCreateRenderPassCount));
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyRenderPass(VkDevice device,
                                                  VkRenderPass render_pass,
                                                  const VkAllocationCallbacks*) noexcept
{
    if (gState && device == gState->mDevice && render_pass != VK_NULL_HANDLE)
    {
        ++gState->mDestroyRenderPassCount;
        gState->mPresentationRenderPassDestroyOrder = ++gState->mTextureUploadDestinationDestroySequence;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateFramebuffer(VkDevice device,
                                                      const VkFramebufferCreateInfo*,
                                                      const VkAllocationCallbacks*,
                                                      VkFramebuffer* framebuffer) noexcept
{
    if (!gState || device != gState->mDevice || !framebuffer)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    ++gState->mCreateFramebufferCount;
    *framebuffer = reinterpret_cast<VkFramebuffer>(static_cast<std::uintptr_t>(0x81000 + gState->mCreateFramebufferCount));
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyFramebuffer(VkDevice device,
                                                   VkFramebuffer framebuffer,
                                                   const VkAllocationCallbacks*) noexcept
{
    if (gState && device == gState->mDevice && framebuffer != VK_NULL_HANDLE)
    {
        ++gState->mDestroyFramebufferCount;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateShaderModule(VkDevice                        device,
                                                       const VkShaderModuleCreateInfo* create_info,
                                                       const VkAllocationCallbacks*,
                                                       VkShaderModule* shader_module) noexcept
{
    if (!gState || device != gState->mDevice || !create_info || !shader_module ||
        create_info->sType != VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO || create_info->codeSize == 0 ||
        create_info->codeSize % sizeof(std::uint32_t) != 0 || !create_info->pCode)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    ++gState->mCreateShaderModuleCount;
    *shader_module = reinterpret_cast<VkShaderModule>(
        static_cast<std::uintptr_t>(0x82000 + gState->mCreateShaderModuleCount));
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyShaderModule(VkDevice device,
                                                    VkShaderModule shader_module,
                                                    const VkAllocationCallbacks*) noexcept
{
    if (gState && device == gState->mDevice && shader_module != VK_NULL_HANDLE)
    {
        ++gState->mDestroyShaderModuleCount;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateSampler(VkDevice                     device,
                                                 const VkSamplerCreateInfo*   create_info,
                                                 const VkAllocationCallbacks* allocator,
                                                 VkSampler*                   sampler) noexcept
{
    if (!gState || device != gState->mDevice || !create_info || allocator || !sampler ||
        create_info->sType != VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO || create_info->pNext != nullptr || create_info->flags != 0 ||
        create_info->magFilter != VK_FILTER_LINEAR || create_info->minFilter != VK_FILTER_LINEAR ||
        create_info->mipmapMode != VK_SAMPLER_MIPMAP_MODE_LINEAR || create_info->addressModeU != VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE ||
        create_info->addressModeV != VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE ||
        create_info->addressModeW != VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE || create_info->mipLodBias != 0.f ||
        create_info->anisotropyEnable != VK_FALSE || create_info->maxAnisotropy != 1.f || create_info->compareEnable != VK_FALSE ||
        create_info->compareOp != VK_COMPARE_OP_ALWAYS || create_info->minLod != 0.f ||
        create_info->maxLod != static_cast<float>(LLRenderContract::TEXTURE_UPLOAD_MIP_LEVELS - 1) ||
        create_info->borderColor != VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK || create_info->unnormalizedCoordinates != VK_FALSE)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    gState->mTextureUploadSampleBindingSamplerCreateInfo = *create_info;
    ++gState->mTextureUploadSampleBindingSamplerCreateCount;
    *sampler = gState->mTextureUploadSampleBindingSampler;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroySampler(VkDevice device, VkSampler sampler, const VkAllocationCallbacks* allocator) noexcept
{
    if (gState && device == gState->mDevice && sampler == gState->mTextureUploadSampleBindingSampler && !allocator)
    {
        ++gState->mTextureUploadSampleBindingSamplerDestroyCount;
        gState->mTextureUploadSampleBindingSamplerDestroyOrder = ++gState->mTextureUploadDestinationDestroySequence;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateDescriptorSetLayout(VkDevice                               device,
                                                             const VkDescriptorSetLayoutCreateInfo* create_info,
                                                             const VkAllocationCallbacks*           allocator,
                                                             VkDescriptorSetLayout*                 descriptor_set_layout) noexcept
{
    if (!gState || device != gState->mDevice || !create_info || allocator || !descriptor_set_layout ||
        create_info->sType != VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO || create_info->pNext != nullptr ||
        create_info->flags != 0 || create_info->bindingCount != 1 || !create_info->pBindings)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    const VkDescriptorSetLayoutBinding& binding = create_info->pBindings[0];
    if (binding.binding != 0 || binding.descriptorType != VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER || binding.descriptorCount != 1 ||
        binding.stageFlags != VK_SHADER_STAGE_FRAGMENT_BIT || binding.pImmutableSamplers != nullptr)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    gState->mTextureUploadSampleBindingDescriptorSetLayoutBinding    = binding;
    gState->mTextureUploadSampleBindingDescriptorSetLayoutCreateInfo = *create_info;
    gState->mTextureUploadSampleBindingDescriptorSetLayoutCreateInfo.pBindings =
        &gState->mTextureUploadSampleBindingDescriptorSetLayoutBinding;
    ++gState->mTextureUploadSampleBindingDescriptorSetLayoutCreateCount;
    *descriptor_set_layout = gState->mTextureUploadSampleBindingDescriptorSetLayout;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyDescriptorSetLayout(VkDevice                     device,
                                                          VkDescriptorSetLayout        descriptor_set_layout,
                                                          const VkAllocationCallbacks* allocator) noexcept
{
    if (gState && device == gState->mDevice && descriptor_set_layout == gState->mTextureUploadSampleBindingDescriptorSetLayout &&
        !allocator)
    {
        ++gState->mTextureUploadSampleBindingDescriptorSetLayoutDestroyCount;
        gState->mTextureUploadSampleBindingDescriptorSetLayoutDestroyOrder = ++gState->mTextureUploadDestinationDestroySequence;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreatePipelineLayout(VkDevice                          device,
                                                        const VkPipelineLayoutCreateInfo* create_info,
                                                        const VkAllocationCallbacks*      allocator,
                                                        VkPipelineLayout*                 pipeline_layout) noexcept
{
    if (!gState || device != gState->mDevice || !create_info || allocator || !pipeline_layout ||
        create_info->sType != VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (create_info->pNext == nullptr && create_info->flags == 0 && create_info->setLayoutCount == 1 && create_info->pSetLayouts &&
        create_info->pSetLayouts[0] == gState->mTextureUploadSampleBindingDescriptorSetLayout && create_info->pushConstantRangeCount == 0 &&
        create_info->pPushConstantRanges == nullptr)
    {
        gState->mTextureUploadSampleBindingPipelineSetLayout                    = create_info->pSetLayouts[0];
        gState->mTextureUploadSampleBindingPipelineLayoutCreateInfo             = *create_info;
        gState->mTextureUploadSampleBindingPipelineLayoutCreateInfo.pSetLayouts = &gState->mTextureUploadSampleBindingPipelineSetLayout;
        ++gState->mTextureUploadSampleBindingPipelineLayoutCreateCount;
        *pipeline_layout = gState->mTextureUploadSampleBindingPipelineLayout;
        return VK_SUCCESS;
    }
    if (create_info->pNext != nullptr || create_info->flags != 0 || create_info->setLayoutCount != 0 ||
        create_info->pSetLayouts != nullptr || create_info->pushConstantRangeCount != 0 || create_info->pPushConstantRanges != nullptr)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    ++gState->mCreatePipelineLayoutCount;
    gState->mLastPipelineLayout = reinterpret_cast<VkPipelineLayout>(
        static_cast<std::uintptr_t>(0x83000 + gState->mCreatePipelineLayoutCount));
    *pipeline_layout = gState->mLastPipelineLayout;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyPipelineLayout(VkDevice                     device,
                                                     VkPipelineLayout             pipeline_layout,
                                                     const VkAllocationCallbacks* allocator) noexcept
{
    if (gState && device == gState->mDevice && pipeline_layout == gState->mTextureUploadSampleBindingPipelineLayout && !allocator)
    {
        ++gState->mTextureUploadSampleBindingPipelineLayoutDestroyCount;
        gState->mTextureUploadSampleBindingPipelineLayoutDestroyOrder = ++gState->mTextureUploadDestinationDestroySequence;
    }
    else if (gState && device == gState->mDevice && pipeline_layout != VK_NULL_HANDLE && !allocator)
    {
        ++gState->mDestroyPipelineLayoutCount;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateDescriptorPool(VkDevice                          device,
                                                        const VkDescriptorPoolCreateInfo* create_info,
                                                        const VkAllocationCallbacks*      allocator,
                                                        VkDescriptorPool*                 descriptor_pool) noexcept
{
    if (!gState || device != gState->mDevice || !create_info || allocator || !descriptor_pool ||
        create_info->sType != VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO || create_info->pNext != nullptr || create_info->flags != 0 ||
        create_info->maxSets != 1 || create_info->poolSizeCount != 1 || !create_info->pPoolSizes ||
        create_info->pPoolSizes[0].type != VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER || create_info->pPoolSizes[0].descriptorCount != 1)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    gState->mTextureUploadSampleBindingDescriptorPoolSize                  = create_info->pPoolSizes[0];
    gState->mTextureUploadSampleBindingDescriptorPoolCreateInfo            = *create_info;
    gState->mTextureUploadSampleBindingDescriptorPoolCreateInfo.pPoolSizes = &gState->mTextureUploadSampleBindingDescriptorPoolSize;
    ++gState->mTextureUploadSampleBindingDescriptorPoolCreateCount;
    *descriptor_pool = gState->mTextureUploadSampleBindingDescriptorPool;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyDescriptorPool(VkDevice                     device,
                                                     VkDescriptorPool             descriptor_pool,
                                                     const VkAllocationCallbacks* allocator) noexcept
{
    if (gState && device == gState->mDevice && descriptor_pool == gState->mTextureUploadSampleBindingDescriptorPool && !allocator)
    {
        ++gState->mTextureUploadSampleBindingDescriptorPoolDestroyCount;
        gState->mTextureUploadSampleBindingDescriptorPoolDestroyOrder = ++gState->mTextureUploadDestinationDestroySequence;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeAllocateDescriptorSets(VkDevice                           device,
                                                          const VkDescriptorSetAllocateInfo* allocate_info,
                                                          VkDescriptorSet*                   descriptor_sets) noexcept
{
    if (!gState || device != gState->mDevice || !allocate_info || !descriptor_sets ||
        allocate_info->sType != VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO || allocate_info->pNext != nullptr ||
        allocate_info->descriptorPool != gState->mTextureUploadSampleBindingDescriptorPool || allocate_info->descriptorSetCount != 1 ||
        !allocate_info->pSetLayouts || allocate_info->pSetLayouts[0] != gState->mTextureUploadSampleBindingDescriptorSetLayout)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    gState->mTextureUploadSampleBindingAllocatedSetLayout                    = allocate_info->pSetLayouts[0];
    gState->mTextureUploadSampleBindingDescriptorSetAllocateInfo             = *allocate_info;
    gState->mTextureUploadSampleBindingDescriptorSetAllocateInfo.pSetLayouts = &gState->mTextureUploadSampleBindingAllocatedSetLayout;
    ++gState->mTextureUploadSampleBindingDescriptorSetAllocateCount;
    descriptor_sets[0] = gState->mTextureUploadSampleBindingDescriptorSet;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeUpdateDescriptorSets(VkDevice                    device,
                                                    std::uint32_t               descriptor_write_count,
                                                    const VkWriteDescriptorSet* descriptor_writes,
                                                    std::uint32_t               descriptor_copy_count,
                                                    const VkCopyDescriptorSet*  descriptor_copies) noexcept
{
    if (!gState || device != gState->mDevice || descriptor_write_count != 1 || !descriptor_writes || descriptor_copy_count != 0 ||
        descriptor_copies != nullptr)
    {
        return;
    }
    const VkWriteDescriptorSet& write = descriptor_writes[0];
    if (write.sType != VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET || write.pNext != nullptr ||
        write.dstSet != gState->mTextureUploadSampleBindingDescriptorSet || write.dstBinding != 0 || write.dstArrayElement != 0 ||
        write.descriptorCount != 1 || write.descriptorType != VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER || !write.pImageInfo ||
        write.pBufferInfo != nullptr || write.pTexelBufferView != nullptr ||
        write.pImageInfo[0].sampler != gState->mTextureUploadSampleBindingSampler ||
        write.pImageInfo[0].imageView != gState->mTextureUploadDestinationImageView ||
        write.pImageInfo[0].imageLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        return;
    }
    gState->mTextureUploadSampleBindingDescriptorImageInfo        = write.pImageInfo[0];
    gState->mTextureUploadSampleBindingDescriptorWrite            = write;
    gState->mTextureUploadSampleBindingDescriptorWrite.pImageInfo = &gState->mTextureUploadSampleBindingDescriptorImageInfo;
    ++gState->mTextureUploadSampleBindingDescriptorUpdateCount;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateGraphicsPipelines(VkDevice device,
                                                           VkPipelineCache pipeline_cache,
                                                           std::uint32_t create_info_count,
                                                           const VkGraphicsPipelineCreateInfo* create_infos,
                                                           const VkAllocationCallbacks*,
                                                           VkPipeline* pipelines) noexcept
{
    if (!gState || device != gState->mDevice || pipeline_cache != VK_NULL_HANDLE || create_info_count != 1 || !create_infos || !pipelines ||
        create_infos[0].sType != VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO || create_infos[0].stageCount != 2 ||
        create_infos[0].renderPass == VK_NULL_HANDLE || create_infos[0].subpass != 0)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (create_infos[0].layout == gState->mTextureUploadSampleBindingPipelineLayout)
    {
        ++gState->mCreatePipelineCount;
        ++gState->mTextureUploadSamplePipelineCreateCount;
        gState->mTextureUploadSamplePipelineLayout     = create_infos[0].layout;
        gState->mTextureUploadSamplePipelineRenderPass = create_infos[0].renderPass;
        gState->mTextureUploadSamplePipeline =
            reinterpret_cast<VkPipeline>(static_cast<std::uintptr_t>(0x91000 + gState->mTextureUploadSamplePipelineCreateCount));
        *pipelines = gState->mTextureUploadSamplePipeline;
        return VK_SUCCESS;
    }
    if (create_infos[0].layout != gState->mLastPipelineLayout)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    ++gState->mCreatePipelineCount;
    gState->mLastPipeline = reinterpret_cast<VkPipeline>(static_cast<std::uintptr_t>(0x84000 + gState->mCreatePipelineCount));
    *pipelines = gState->mLastPipeline;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyPipeline(VkDevice device,
                                                VkPipeline pipeline,
                                                const VkAllocationCallbacks*) noexcept
{
    if (gState && device == gState->mDevice && pipeline != VK_NULL_HANDLE)
    {
        ++gState->mDestroyPipelineCount;
        if (pipeline == gState->mTextureUploadSamplePipeline)
        {
            ++gState->mTextureUploadSamplePipelineDestroyCount;
            gState->mTextureUploadSamplePipelineDestroyOrder = ++gState->mTextureUploadDestinationDestroySequence;
        }
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateCommandPool(VkDevice device,
                                                     const VkCommandPoolCreateInfo*,
                                                     const VkAllocationCallbacks*,
                                                     VkCommandPool* command_pool) noexcept
{
    if (!gState || device != gState->mDevice || !command_pool)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    ++gState->mCreateCommandPoolCount;
    *command_pool = gState->mCommandPool;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyCommandPool(VkDevice device,
                                                  VkCommandPool command_pool,
                                                  const VkAllocationCallbacks*) noexcept
{
    if (gState && device == gState->mDevice && command_pool == gState->mCommandPool)
    {
        ++gState->mDestroyCommandPoolCount;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeAllocateCommandBuffers(VkDevice device,
                                                          const VkCommandBufferAllocateInfo*,
                                                          VkCommandBuffer* command_buffer) noexcept
{
    if (!gState || device != gState->mDevice || !command_buffer)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    *command_buffer = gState->mCommandBuffer;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateSemaphore(VkDevice device,
                                                   const VkSemaphoreCreateInfo*,
                                                   const VkAllocationCallbacks*,
                                                   VkSemaphore* semaphore) noexcept
{
    if (!gState || device != gState->mDevice || !semaphore)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    *semaphore = gState->mCreateSemaphoreCount++ % 2 == 0 ? gState->mImageAvailableSemaphore
                                                          : gState->mPresentationReadySemaphore;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroySemaphore(VkDevice device,
                                                VkSemaphore semaphore,
                                                const VkAllocationCallbacks*) noexcept
{
    if (gState && device == gState->mDevice && semaphore != VK_NULL_HANDLE)
    {
        ++gState->mDestroySemaphoreCount;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateFence(VkDevice device,
                                               const VkFenceCreateInfo*,
                                               const VkAllocationCallbacks*,
                                               VkFence* fence) noexcept
{
    if (!gState || device != gState->mDevice || !fence)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    *fence = gState->mCreateFenceCount++ % 2 == 0 ? gState->mSubmissionFence : gState->mPresentCompletionFence;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyFence(VkDevice device, VkFence fence, const VkAllocationCallbacks*) noexcept
{
    if (gState && device == gState->mDevice && fence != VK_NULL_HANDLE)
    {
        ++gState->mDestroyFenceCount;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeWaitForFences(VkDevice       device,
                                                 std::uint32_t  fence_count,
                                                 const VkFence* fences,
                                                 VkBool32,
                                                 std::uint64_t) noexcept
{
    if (!gState || device != gState->mDevice || fence_count == 0 || fence_count > 2 || !fences)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    for (std::uint32_t index = 0; index < fence_count; ++index)
    {
        if (fences[index] != gState->mSubmissionFence && fences[index] != gState->mPresentCompletionFence)
        {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
    }
    ++gState->mWaitForFencesCount;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeResetCommandBuffer(VkCommandBuffer command_buffer, VkCommandBufferResetFlags flags) noexcept
{
    return gState && command_buffer == gState->mCommandBuffer && flags == 0 ? VK_SUCCESS : VK_ERROR_INITIALIZATION_FAILED;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeBeginCommandBuffer(VkCommandBuffer command_buffer,
                                                       const VkCommandBufferBeginInfo* begin_info) noexcept
{
    return gState && command_buffer == gState->mCommandBuffer && begin_info ? VK_SUCCESS : VK_ERROR_INITIALIZATION_FAILED;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeEndCommandBuffer(VkCommandBuffer command_buffer) noexcept
{
    return gState && command_buffer == gState->mCommandBuffer ? VK_SUCCESS : VK_ERROR_INITIALIZATION_FAILED;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeResetFences(VkDevice device, std::uint32_t fence_count, const VkFence* fences) noexcept
{
    if (!gState || device != gState->mDevice || fence_count == 0 || fence_count > 2 || !fences)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    for (std::uint32_t index = 0; index < fence_count; ++index)
    {
        if (fences[index] != gState->mSubmissionFence && fences[index] != gState->mPresentCompletionFence)
        {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
    }
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeQueueSubmit(VkQueue             queue,
                                               std::uint32_t       submit_count,
                                               const VkSubmitInfo* submits,
                                               VkFence             fence) noexcept
{
    if (!gState || queue != gState->mQueue || submit_count != 1 || !submits ||
        fence != gState->mSubmissionFence || submits[0].sType != VK_STRUCTURE_TYPE_SUBMIT_INFO ||
        submits[0].commandBufferCount != 1 || !submits[0].pCommandBuffers ||
        submits[0].pCommandBuffers[0] != gState->mCommandBuffer)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    const bool empty_submission = submits[0].waitSemaphoreCount == 0 && !submits[0].pWaitSemaphores &&
                                  !submits[0].pWaitDstStageMask && submits[0].signalSemaphoreCount == 0 &&
                                  !submits[0].pSignalSemaphores;
    const bool presentation_submission = submits[0].waitSemaphoreCount == 1 && submits[0].pWaitSemaphores &&
                                         submits[0].pWaitSemaphores[0] == gState->mImageAvailableSemaphore &&
                                         submits[0].pWaitDstStageMask && submits[0].signalSemaphoreCount == 1 &&
                                         submits[0].pSignalSemaphores &&
                                         submits[0].pSignalSemaphores[0] == gState->mPresentationReadySemaphore;
    if (!empty_submission && !presentation_submission)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    ++gState->mQueueSubmitCount;
    if (presentation_submission)
    {
        gState->mSubmitWaitStage = submits[0].pWaitDstStageMask[0];
    }
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeAcquireNextImage(VkDevice       device,
                                                    VkSwapchainKHR swapchain,
                                                    std::uint64_t  timeout,
                                                    VkSemaphore    semaphore,
                                                    VkFence        fence,
                                                    std::uint32_t* image_index) noexcept
{
    if (!gState || device != gState->mDevice || swapchain != gState->mSwapchain ||
        timeout != LLRenderVulkan::VULKAN_SWAPCHAIN_FRAME_ACQUIRE_TIMEOUT_NS || semaphore != gState->mImageAvailableSemaphore ||
        fence != VK_NULL_HANDLE || !image_index)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    ++gState->mAcquireNextImageCount;
    *image_index = gState->mAcquiredImageIndex;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeCmdPipelineBarrier(VkCommandBuffer      command_buffer,
                                                  VkPipelineStageFlags source_stage,
                                                  VkPipelineStageFlags destination_stage,
                                                  VkDependencyFlags,
                                                  std::uint32_t,
                                                  const VkMemoryBarrier*,
                                                  std::uint32_t,
                                                  const VkBufferMemoryBarrier*,
                                                  std::uint32_t               image_barrier_count,
                                                  const VkImageMemoryBarrier* image_barriers) noexcept
{
    if (gState && command_buffer == gState->mCommandBuffer && source_stage != 0 && destination_stage != 0 && image_barrier_count != 0 &&
        image_barriers)
    {
        if (image_barrier_count == 1)
        {
            ++gState->mPipelineBarrierCount;
        }
        for (std::uint32_t index = 0; index < image_barrier_count; ++index)
        {
            if (image_barriers[index].image == gState->mTextureUploadDestinationImage)
            {
                ++gState->mTextureUploadImageBarrierCount;
            }
        }
    }
}

VKAPI_ATTR void VKAPI_CALL fakeCmdCopyBufferToImage(VkCommandBuffer          command_buffer,
                                                    VkBuffer                 source,
                                                    VkImage                  destination,
                                                    VkImageLayout            layout,
                                                    std::uint32_t            region_count,
                                                    const VkBufferImageCopy* regions) noexcept
{
    if (gState && command_buffer == gState->mCommandBuffer && source == gState->mTextureUploadSourceBuffer &&
        destination == gState->mTextureUploadDestinationImage && layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && region_count == 4 &&
        regions)
    {
        ++gState->mTextureUploadCopyCalls;
    }
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
    if (gState && command_buffer == gState->mCommandBuffer && source == gState->mTextureUploadDestinationImage &&
        destination == gState->mTextureUploadDestinationImage && source_layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL &&
        destination_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && region_count == 1 && regions && filter == VK_FILTER_LINEAR)
    {
        ++gState->mTextureUploadBlitCalls;
    }
}

VKAPI_ATTR void VKAPI_CALL fakeCmdCopyBuffer(VkCommandBuffer     command_buffer,
                                             VkBuffer            source,
                                             VkBuffer            destination,
                                             std::uint32_t       region_count,
                                             const VkBufferCopy* regions) noexcept
{
    if (gState && command_buffer == gState->mCommandBuffer && source == gState->mUploadSourceBuffer &&
        destination == gState->mUploadDestinationBuffer && region_count == 1 && regions && regions[0].srcOffset == 0 &&
        regions[0].dstOffset == 0 && regions[0].size == LLRenderVulkan::VULKAN_UPLOAD_SOURCE_BYTE_COUNT)
    {
        ++gState->mCopyBufferCount;
    }
    else if (gState)
    {
        gState->mInvalidCopyBuffer = true;
    }
}

VKAPI_ATTR void VKAPI_CALL fakeCmdClearColorImage(VkCommandBuffer               command_buffer,
                                                   VkImage                       image,
                                                   VkImageLayout                 image_layout,
                                                   const VkClearColorValue*      color,
                                                   std::uint32_t                 range_count,
                                                   const VkImageSubresourceRange* ranges) noexcept
{
    if (gState && color && range_count == 1 && ranges)
    {
        ++gState->mClearColorImageCount;
        gState->mClearCommandBuffer = command_buffer;
        gState->mClearedImage       = image;
        gState->mClearImageLayout   = image_layout;
        gState->mClearColor         = *color;
        gState->mClearRange         = ranges[0];
    }
}

VKAPI_ATTR void VKAPI_CALL fakeCmdBeginRenderPass(VkCommandBuffer              command_buffer,
                                                   const VkRenderPassBeginInfo* begin_info,
                                                   VkSubpassContents            contents) noexcept
{
    if (gState && command_buffer == gState->mCommandBuffer && begin_info &&
        begin_info->sType == VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO && begin_info->clearValueCount == 1 &&
        begin_info->pClearValues)
    {
        ++gState->mBeginRenderPassCount;
        gState->mRenderPassCommandBuffer = command_buffer;
        gState->mRenderPass              = begin_info->renderPass;
        gState->mRenderPassFramebuffer   = begin_info->framebuffer;
        gState->mRenderPassArea          = begin_info->renderArea;
        gState->mRenderPassClear         = begin_info->pClearValues[0];
        gState->mRenderPassContents      = contents;
    }
}

VKAPI_ATTR void VKAPI_CALL fakeCmdEndRenderPass(VkCommandBuffer command_buffer) noexcept
{
    if (gState && command_buffer == gState->mCommandBuffer)
    {
        ++gState->mEndRenderPassCount;
    }
}

VKAPI_ATTR void VKAPI_CALL fakeCmdBindPipeline(VkCommandBuffer     command_buffer,
                                               VkPipelineBindPoint pipeline_bind_point,
                                               VkPipeline          pipeline) noexcept
{
    if (gState && command_buffer == gState->mCommandBuffer)
    {
        ++gState->mBindPipelineCount;
        gState->mDrawCommandBuffer = command_buffer;
        gState->mPipelineBindPoint = pipeline_bind_point;
        gState->mBoundPipeline     = pipeline;
        gState->mBindPipelineOrder = ++gState->mCommandOrder;
    }
}

VKAPI_ATTR void VKAPI_CALL fakeCmdBindVertexBuffers(VkCommandBuffer     command_buffer,
                                                    std::uint32_t       first_binding,
                                                    std::uint32_t       binding_count,
                                                    const VkBuffer*     buffers,
                                                    const VkDeviceSize* offsets) noexcept
{
    if (gState && command_buffer == gState->mCommandBuffer && binding_count == 1 && buffers && offsets)
    {
        ++gState->mBindVertexBuffersCount;
        gState->mDrawCommandBuffer      = command_buffer;
        gState->mFirstVertexBinding     = first_binding;
        gState->mVertexBindingCount     = binding_count;
        gState->mBoundVertexBuffer      = buffers[0];
        gState->mBoundVertexOffset      = offsets[0];
        gState->mBindVertexBuffersOrder = ++gState->mCommandOrder;
    }
}

VKAPI_ATTR void VKAPI_CALL fakeCmdSetViewport(VkCommandBuffer   command_buffer,
                                              std::uint32_t     first_viewport,
                                              std::uint32_t     viewport_count,
                                              const VkViewport* viewports) noexcept
{
    if (gState && command_buffer == gState->mCommandBuffer && viewport_count == 1 && viewports)
    {
        ++gState->mSetViewportCount;
        gState->mFirstViewport = first_viewport;
        gState->mViewport      = viewports[0];
    }
}

VKAPI_ATTR void VKAPI_CALL fakeCmdSetScissor(VkCommandBuffer command_buffer,
                                             std::uint32_t   first_scissor,
                                             std::uint32_t   scissor_count,
                                             const VkRect2D* scissors) noexcept
{
    if (gState && command_buffer == gState->mCommandBuffer && scissor_count == 1 && scissors)
    {
        ++gState->mSetScissorCount;
        gState->mFirstScissor = first_scissor;
        gState->mScissor      = scissors[0];
    }
}

VKAPI_ATTR void VKAPI_CALL fakeCmdDraw(VkCommandBuffer command_buffer,
                                       std::uint32_t   vertex_count,
                                       std::uint32_t   instance_count,
                                       std::uint32_t   first_vertex,
                                       std::uint32_t   first_instance) noexcept
{
    if (gState && command_buffer == gState->mCommandBuffer)
    {
        ++gState->mDrawCount;
        gState->mDrawOrder         = ++gState->mCommandOrder;
        gState->mDrawVertexCount   = vertex_count;
        gState->mDrawInstanceCount = instance_count;
        gState->mDrawFirstVertex   = first_vertex;
        gState->mDrawFirstInstance = first_instance;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeQueuePresent(VkQueue queue, const VkPresentInfoKHR* present_info) noexcept
{
    const auto* fence_info = present_info ? static_cast<const VkSwapchainPresentFenceInfoKHR*>(present_info->pNext) : nullptr;
    if (!gState || queue != gState->mQueue || !present_info || present_info->waitSemaphoreCount != 1 ||
        !present_info->pWaitSemaphores || present_info->pWaitSemaphores[0] != gState->mPresentationReadySemaphore ||
        present_info->swapchainCount != 1 || !present_info->pSwapchains || present_info->pSwapchains[0] != gState->mSwapchain ||
        !present_info->pImageIndices || present_info->pImageIndices[0] != gState->mAcquiredImageIndex || !fence_info ||
        fence_info->sType != VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_KHR || fence_info->swapchainCount != 1 ||
        !fence_info->pFences || fence_info->pFences[0] != gState->mPresentCompletionFence)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    ++gState->mQueuePresentCount;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeReleaseSwapchainImages(VkDevice device,
                                                          const VkReleaseSwapchainImagesInfoKHR* release_info) noexcept
{
    if (!gState || device != gState->mDevice || !release_info || release_info->swapchain != gState->mSwapchain ||
        release_info->imageIndexCount != 1 || !release_info->pImageIndices ||
        release_info->pImageIndices[0] != gState->mAcquiredImageIndex)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    ++gState->mReleaseSwapchainImagesCount;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeGetPhysicalDeviceMemoryProperties(VkPhysicalDevice                  physical_device,
                                                                 VkPhysicalDeviceMemoryProperties* properties) noexcept
{
    if (gState && physical_device == gState->mPhysicalDevice && properties)
    {
        *properties = gState->mMemoryProperties;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateBuffer(VkDevice,
                                                const VkBufferCreateInfo* create_info,
                                                const VkAllocationCallbacks*,
                                                VkBuffer* buffer) noexcept
{
    if (!gState || !create_info || !buffer)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    ++gState->mCreateBufferCount;
    gState->mReadbackBufferSize = create_info->size;
    if (create_info->usage == VK_BUFFER_USAGE_TRANSFER_SRC_BIT && create_info->size == LLRenderContract::TEXTURE_UPLOAD_SOURCE_BYTE_COUNT)
    {
        gState->mTextureUploadSourceCreateInfo = *create_info;
        *buffer                                = gState->mTextureUploadSourceBuffer;
    }
    else if (create_info->usage == VK_BUFFER_USAGE_TRANSFER_SRC_BIT && create_info->size == LLRenderVulkan::VULKAN_UPLOAD_SOURCE_BYTE_COUNT)
    {
        *buffer = gState->mUploadSourceBuffer;
    }
    else if (create_info->usage == VK_BUFFER_USAGE_TRANSFER_SRC_BIT)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    else if (create_info->usage == (VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT))
    {
        *buffer = gState->mUploadDestinationBuffer;
    }
    else
    {
        *buffer = gState->mReadbackBuffer;
    }
    gState->mLastCreatedBuffer = *buffer;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyBuffer(VkDevice, VkBuffer buffer, const VkAllocationCallbacks*) noexcept
{
    if (!gState)
    {
        return;
    }
    if (buffer == gState->mReadbackBuffer)
    {
        ++gState->mDestroyBufferCount;
    }
    else if (buffer == gState->mUploadSourceBuffer)
    {
        ++gState->mUploadSourceDestroyCount;
    }
    else if (buffer == gState->mTextureUploadSourceBuffer)
    {
        ++gState->mTextureUploadSourceDestroyCount;
        gState->mTextureUploadSourceDestroyOrder = ++gState->mTextureUploadDestinationDestroySequence;
    }
    else if (buffer == gState->mUploadDestinationBuffer)
    {
        ++gState->mUploadDestinationDestroyCount;
    }
}

VKAPI_ATTR void VKAPI_CALL fakeGetBufferMemoryRequirements(VkDevice, VkBuffer buffer, VkMemoryRequirements* requirements) noexcept
{
    if (gState && buffer == gState->mTextureUploadSourceBuffer && requirements)
    {
        *requirements = gState->mTextureUploadSourceMemoryRequirements;
    }
    else if (gState &&
             (buffer == gState->mReadbackBuffer || buffer == gState->mUploadSourceBuffer || buffer == gState->mUploadDestinationBuffer) &&
             requirements)
    {
        *requirements = { gState->mReadbackBufferSize, 256, 1 };
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeAllocateMemory(VkDevice device,
                                                  const VkMemoryAllocateInfo* allocate_info,
                                                  const VkAllocationCallbacks*,
                                                  VkDeviceMemory* memory) noexcept
{
    if (!gState || device != gState->mDevice || !allocate_info ||
        allocate_info->sType != VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO || !memory || allocate_info->memoryTypeIndex != 0)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    ++gState->mAllocateMemoryCount;
    if (allocate_info->pNext)
    {
        const auto* dedicated = static_cast<const VkMemoryDedicatedAllocateInfo*>(allocate_info->pNext);
        if (dedicated->sType != VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO || dedicated->pNext != nullptr ||
            dedicated->image != gState->mTextureUploadDestinationImage || dedicated->buffer != VK_NULL_HANDLE ||
            allocate_info->allocationSize != gState->mTextureUploadDestinationMemoryRequirements.size)
        {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        gState->mTextureUploadDestinationDedicatedAllocationExact = true;
        ++gState->mTextureUploadDestinationAllocateCount;
        *memory = gState->mTextureUploadDestinationMemory;
        return VK_SUCCESS;
    }
    if (allocate_info->allocationSize < gState->mReadbackBufferSize)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (gState->mLastCreatedBuffer == gState->mTextureUploadSourceBuffer &&
        allocate_info->allocationSize != gState->mTextureUploadSourceMemoryRequirements.size)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    *memory = gState->mLastCreatedBuffer == gState->mTextureUploadSourceBuffer ? gState->mTextureUploadSourceMemory
              : gState->mLastCreatedBuffer == gState->mUploadSourceBuffer      ? gState->mUploadSourceMemory
              : gState->mLastCreatedBuffer == gState->mUploadDestinationBuffer ? gState->mUploadDestinationMemory
                                                                               : gState->mReadbackMemory;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeFreeMemory(VkDevice, VkDeviceMemory memory, const VkAllocationCallbacks*) noexcept
{
    if (!gState)
    {
        return;
    }
    if (memory == gState->mReadbackMemory)
    {
        ++gState->mFreeMemoryCount;
    }
    else if (memory == gState->mUploadSourceMemory)
    {
        ++gState->mUploadSourceFreeCount;
    }
    else if (memory == gState->mTextureUploadSourceMemory)
    {
        ++gState->mTextureUploadSourceFreeCount;
        gState->mTextureUploadSourceMemoryFreeOrder = ++gState->mTextureUploadDestinationDestroySequence;
    }
    else if (memory == gState->mUploadDestinationMemory)
    {
        ++gState->mUploadDestinationFreeCount;
    }
    else if (memory == gState->mTextureUploadDestinationMemory)
    {
        ++gState->mTextureUploadDestinationFreeCount;
        gState->mTextureUploadDestinationMemoryFreeOrder = ++gState->mTextureUploadDestinationDestroySequence;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeBindBufferMemory(VkDevice, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize offset) noexcept
{
    const bool matching_pair = gState && ((buffer == gState->mReadbackBuffer && memory == gState->mReadbackMemory) ||
                                          (buffer == gState->mUploadSourceBuffer && memory == gState->mUploadSourceMemory) ||
                                          (buffer == gState->mTextureUploadSourceBuffer && memory == gState->mTextureUploadSourceMemory) ||
                                          (buffer == gState->mUploadDestinationBuffer && memory == gState->mUploadDestinationMemory));
    return matching_pair && offset == 0 ? VK_SUCCESS : VK_ERROR_INITIALIZATION_FAILED;
}

VKAPI_ATTR VkResult VKAPI_CALL
    fakeMapMemory(VkDevice, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, VkMemoryMapFlags flags, void** data) noexcept
{
    if (!gState ||
        (memory != gState->mReadbackMemory && memory != gState->mUploadSourceMemory && memory != gState->mTextureUploadSourceMemory) ||
        offset != 0 || size != VK_WHOLE_SIZE || flags != 0 || !data)
    {
        return VK_ERROR_MEMORY_MAP_FAILED;
    }
    ++gState->mMapMemoryCount;
    if (memory == gState->mTextureUploadSourceMemory)
    {
        ++gState->mTextureUploadSourceMapCount;
        *data = gState->mTextureUploadSourceMappedBytes.data();
    }
    else
    {
        *data = memory == gState->mUploadSourceMemory ? static_cast<void*>(gState->mUploadMappedBytes.data())
                                                      : static_cast<void*>(&gState->mReadbackMappedByte);
    }
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeFlushMappedMemoryRanges(VkDevice                   device,
                                                           std::uint32_t              range_count,
                                                           const VkMappedMemoryRange* ranges) noexcept
{
    if (!gState || device != gState->mDevice || range_count != 1 || !ranges || ranges[0].sType != VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE ||
        ranges[0].offset != 0 || ranges[0].size != VK_WHOLE_SIZE ||
        (ranges[0].memory != gState->mUploadSourceMemory && ranges[0].memory != gState->mTextureUploadSourceMemory))
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (ranges[0].memory == gState->mTextureUploadSourceMemory)
    {
        ++gState->mTextureUploadSourceFlushCount;
    }
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeUnmapMemory(VkDevice, VkDeviceMemory memory) noexcept
{
    if (gState &&
        (memory == gState->mReadbackMemory || memory == gState->mUploadSourceMemory || memory == gState->mTextureUploadSourceMemory))
    {
        ++gState->mUnmapMemoryCount;
        if (memory == gState->mTextureUploadSourceMemory)
        {
            ++gState->mTextureUploadSourceUnmapCount;
        }
    }
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL fakeGetDeviceProcAddr(VkDevice device, const char* name) noexcept
{
    if (!gState || device != gState->mDevice || !name)
    {
        return nullptr;
    }
#define LL_MACOS_VULKAN_DEVICE_COMMAND(command) \
    if (std::strcmp(name, "vk" #command) == 0)  \
    return eraseFunctionType(fake##command)
    if (std::strcmp(name, "vkCreateSwapchainKHR") == 0)
        return eraseFunctionType(fakeCreateSwapchain);
    if (std::strcmp(name, "vkDestroySwapchainKHR") == 0)
        return eraseFunctionType(fakeDestroySwapchain);
    if (std::strcmp(name, "vkGetSwapchainImagesKHR") == 0)
        return eraseFunctionType(fakeGetSwapchainImages);
    LL_MACOS_VULKAN_DEVICE_COMMAND(CreateImageView);
    LL_MACOS_VULKAN_DEVICE_COMMAND(DestroyImageView);
    LL_MACOS_VULKAN_DEVICE_COMMAND(CreateImage);
    LL_MACOS_VULKAN_DEVICE_COMMAND(DestroyImage);
    LL_MACOS_VULKAN_DEVICE_COMMAND(GetImageMemoryRequirements2);
    LL_MACOS_VULKAN_DEVICE_COMMAND(BindImageMemory);
    LL_MACOS_VULKAN_DEVICE_COMMAND(CreateRenderPass);
    LL_MACOS_VULKAN_DEVICE_COMMAND(DestroyRenderPass);
    LL_MACOS_VULKAN_DEVICE_COMMAND(CreateFramebuffer);
    LL_MACOS_VULKAN_DEVICE_COMMAND(DestroyFramebuffer);
    LL_MACOS_VULKAN_DEVICE_COMMAND(CreateShaderModule);
    LL_MACOS_VULKAN_DEVICE_COMMAND(DestroyShaderModule);
    LL_MACOS_VULKAN_DEVICE_COMMAND(CreateSampler);
    LL_MACOS_VULKAN_DEVICE_COMMAND(DestroySampler);
    LL_MACOS_VULKAN_DEVICE_COMMAND(CreateDescriptorSetLayout);
    LL_MACOS_VULKAN_DEVICE_COMMAND(DestroyDescriptorSetLayout);
    LL_MACOS_VULKAN_DEVICE_COMMAND(CreatePipelineLayout);
    LL_MACOS_VULKAN_DEVICE_COMMAND(DestroyPipelineLayout);
    LL_MACOS_VULKAN_DEVICE_COMMAND(CreateDescriptorPool);
    LL_MACOS_VULKAN_DEVICE_COMMAND(DestroyDescriptorPool);
    LL_MACOS_VULKAN_DEVICE_COMMAND(AllocateDescriptorSets);
    LL_MACOS_VULKAN_DEVICE_COMMAND(UpdateDescriptorSets);
    LL_MACOS_VULKAN_DEVICE_COMMAND(CreateGraphicsPipelines);
    LL_MACOS_VULKAN_DEVICE_COMMAND(DestroyPipeline);
    LL_MACOS_VULKAN_DEVICE_COMMAND(CreateCommandPool);
    LL_MACOS_VULKAN_DEVICE_COMMAND(DestroyCommandPool);
    LL_MACOS_VULKAN_DEVICE_COMMAND(AllocateCommandBuffers);
    LL_MACOS_VULKAN_DEVICE_COMMAND(CreateSemaphore);
    LL_MACOS_VULKAN_DEVICE_COMMAND(DestroySemaphore);
    LL_MACOS_VULKAN_DEVICE_COMMAND(CreateFence);
    LL_MACOS_VULKAN_DEVICE_COMMAND(DestroyFence);
    LL_MACOS_VULKAN_DEVICE_COMMAND(WaitForFences);
    LL_MACOS_VULKAN_DEVICE_COMMAND(ResetCommandBuffer);
    LL_MACOS_VULKAN_DEVICE_COMMAND(BeginCommandBuffer);
    LL_MACOS_VULKAN_DEVICE_COMMAND(EndCommandBuffer);
    LL_MACOS_VULKAN_DEVICE_COMMAND(ResetFences);
    LL_MACOS_VULKAN_DEVICE_COMMAND(QueueSubmit);
    if (std::strcmp(name, "vkAcquireNextImageKHR") == 0)
        return eraseFunctionType(fakeAcquireNextImage);
    LL_MACOS_VULKAN_DEVICE_COMMAND(CmdPipelineBarrier);
    LL_MACOS_VULKAN_DEVICE_COMMAND(CmdCopyBuffer);
    LL_MACOS_VULKAN_DEVICE_COMMAND(CmdCopyBufferToImage);
    LL_MACOS_VULKAN_DEVICE_COMMAND(CmdBlitImage);
    LL_MACOS_VULKAN_DEVICE_COMMAND(CmdClearColorImage);
    LL_MACOS_VULKAN_DEVICE_COMMAND(CmdBeginRenderPass);
    LL_MACOS_VULKAN_DEVICE_COMMAND(CmdEndRenderPass);
    LL_MACOS_VULKAN_DEVICE_COMMAND(CmdBindPipeline);
    LL_MACOS_VULKAN_DEVICE_COMMAND(CmdBindVertexBuffers);
    LL_MACOS_VULKAN_DEVICE_COMMAND(CmdSetViewport);
    LL_MACOS_VULKAN_DEVICE_COMMAND(CmdSetScissor);
    LL_MACOS_VULKAN_DEVICE_COMMAND(CmdDraw);
    if (std::strcmp(name, "vkQueuePresentKHR") == 0)
        return eraseFunctionType(fakeQueuePresent);
    if (std::strcmp(name, "vkReleaseSwapchainImagesKHR") == 0)
        return eraseFunctionType(fakeReleaseSwapchainImages);
    LL_MACOS_VULKAN_DEVICE_COMMAND(CreateBuffer);
    LL_MACOS_VULKAN_DEVICE_COMMAND(DestroyBuffer);
    LL_MACOS_VULKAN_DEVICE_COMMAND(GetBufferMemoryRequirements);
    LL_MACOS_VULKAN_DEVICE_COMMAND(AllocateMemory);
    LL_MACOS_VULKAN_DEVICE_COMMAND(FreeMemory);
    LL_MACOS_VULKAN_DEVICE_COMMAND(BindBufferMemory);
    LL_MACOS_VULKAN_DEVICE_COMMAND(MapMemory);
    LL_MACOS_VULKAN_DEVICE_COMMAND(FlushMappedMemoryRanges);
    LL_MACOS_VULKAN_DEVICE_COMMAND(UnmapMemory);
#undef LL_MACOS_VULKAN_DEVICE_COMMAND
    return nullptr;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL fakeGetInstanceProcAddr(VkInstance instance, const char* name) noexcept
{
    if (!gState || !name)
    {
        return nullptr;
    }
    if (std::strcmp(name, "vkCreateInstance") == 0)
    {
        return eraseFunctionType(fakeCreateInstance);
    }
    if (std::strcmp(name, "vkEnumerateInstanceExtensionProperties") == 0)
    {
        return eraseFunctionType(fakeEnumerateInstanceExtensionProperties);
    }
    if (std::strcmp(name, "vkEnumerateInstanceLayerProperties") == 0)
    {
        return eraseFunctionType(fakeEnumerateInstanceLayerProperties);
    }
    if (std::strcmp(name, "vkEnumerateInstanceVersion") == 0)
    {
        return eraseFunctionType(fakeEnumerateInstanceVersion);
    }
    if (instance != gState->mInstance)
    {
        return nullptr;
    }
    if (std::strcmp(name, "vkDestroyInstance") == 0)
    {
        return eraseFunctionType(fakeDestroyInstance);
    }
    if (std::strcmp(name, "vkCreateDebugUtilsMessengerEXT") == 0)
    {
        return eraseFunctionType(fakeCreateDebugUtilsMessenger);
    }
    if (std::strcmp(name, "vkDestroyDebugUtilsMessengerEXT") == 0)
    {
        return eraseFunctionType(fakeDestroyDebugUtilsMessenger);
    }
    if (std::strcmp(name, "vkDestroySurfaceKHR") == 0)
    {
        return gState->mExposeDestroySurface ? eraseFunctionType(fakeDestroySurface) : nullptr;
    }
    if (std::strcmp(name, "vkEnumeratePhysicalDevices") == 0)
        return eraseFunctionType(fakeEnumeratePhysicalDevices);
    if (std::strcmp(name, "vkGetPhysicalDeviceProperties") == 0)
        return eraseFunctionType(fakeGetPhysicalDeviceProperties);
    if (std::strcmp(name, "vkGetPhysicalDeviceFormatProperties") == 0)
        return eraseFunctionType(fakeGetPhysicalDeviceFormatProperties);
    if (std::strcmp(name, "vkGetPhysicalDeviceImageFormatProperties") == 0)
        return eraseFunctionType(fakeGetPhysicalDeviceImageFormatProperties);
    if (std::strcmp(name, "vkGetPhysicalDeviceMemoryProperties") == 0)
        return eraseFunctionType(fakeGetPhysicalDeviceMemoryProperties);
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
    if (std::strcmp(name, "vkGetDeviceProcAddr") == 0)
        return eraseFunctionType(fakeGetDeviceProcAddr);
    return nullptr;
}

LLWindowVulkanFunction fakeResolver() noexcept
{
    return reinterpret_cast<LLWindowVulkanFunction>(fakeGetInstanceProcAddr);
}

bool isMainThread(void* userdata) noexcept
{
    return static_cast<FakeState*>(userdata)->mMainThread;
}

void* openLoader(void* userdata, const char* path) noexcept
{
    auto& state = *static_cast<FakeState*>(userdata);
    state.record(Event::OpenLoader);
    state.mOpenedPath = path;
    if (!state.mLoaderOpens)
    {
        return nullptr;
    }
    state.mLoaderLive = true;
    return state.mLoader;
}

void closeLoader(void* userdata, void* loader) noexcept
{
    auto& state = *static_cast<FakeState*>(userdata);
    state.record(Event::CloseLoader);
    if (state.mOwnerDuringDestroy)
    {
        state.mAllOwnersGoneBeforeLoaderClose = !state.mOwnerDuringDestroy->hasNativeWindow() &&
                                                !state.mOwnerDuringDestroy->hasRequirements() &&
                                                !state.mOwnerDuringDestroy->instanceGeneration();
    }
    if (loader == state.mLoader)
    {
        state.mLoaderLive = false;
    }
}

LLWindowVulkanFunction getResolver(void* userdata, void* loader) noexcept
{
    auto& state = *static_cast<FakeState*>(userdata);
    state.record(Event::GetResolver);
    state.mResolverLoader = loader;
    return state.mResolverLive ? fakeResolver() : nullptr;
}

LLWindowMacOSXVulkanNativeCreateResult createNativeWindow(void* userdata, const LLWindowMacOSXVulkanCreateInfo& info) noexcept
{
    auto& state = *static_cast<FakeState*>(userdata);
    state.record(Event::CreateNative);
    state.mCreateInfo = &info;
    if (state.mNativeFailure)
    {
        return LLWindowMacOSXVulkanNativeCreateError{ *state.mNativeFailure };
    }
    state.mNativeLive = true;
    return state.mNative;
}

bool refreshNativeWindow(void* userdata, LLWindowMacOSXVulkanNativeWindow& native) noexcept
{
    auto& state = *static_cast<FakeState*>(userdata);
    state.record(Event::RefreshNative);
    ++state.mRefreshCount;
    if (!state.mRefreshSucceeds)
    {
        return false;
    }

    native.mBackingScale   = state.mRefreshScale;
    native.mDrawableWidth  = state.mRefreshWidth;
    native.mDrawableHeight = state.mRefreshHeight;
    switch (state.mRefreshMutation)
    {
        case RefreshMutation::None:
            break;
        case RefreshMutation::Token:
            native.mToken = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x9090));
            break;
        case RefreshMutation::Window:
            native.mWindow = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x9090));
            break;
        case RefreshMutation::View:
            native.mView = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x9090));
            break;
        case RefreshMutation::Layer:
            native.mMetalLayer = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x9090));
            break;
        case RefreshMutation::ZeroScale:
            native.mBackingScale = 0.0;
            break;
        case RefreshMutation::InfiniteScale:
            native.mBackingScale = std::numeric_limits<F64>::infinity();
            break;
        case RefreshMutation::ZeroWidth:
            native.mDrawableWidth = 0;
            break;
        case RefreshMutation::ZeroHeight:
            native.mDrawableHeight = 0;
            break;
    }
    return true;
}

bool resizeNativeWindowForDiagnostic(void*                             userdata,
                                     LLWindowMacOSXVulkanNativeWindow& native,
                                     U32                               backing_width,
                                     U32                               backing_height) noexcept
{
    auto& state = *static_cast<FakeState*>(userdata);
    state.record(Event::ResizeNative);
    ++state.mResizeCount;
    state.mResizeWidth  = backing_width;
    state.mResizeHeight = backing_height;
    if (!state.mResizeSucceeds)
    {
        return false;
    }

    native.mBackingScale   = state.mRefreshScale;
    native.mDrawableWidth  = backing_width;
    native.mDrawableHeight = backing_height;
    switch (state.mResizeMutation)
    {
        case RefreshMutation::None:
            break;
        case RefreshMutation::Token:
            native.mToken = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x9090));
            break;
        case RefreshMutation::Window:
            native.mWindow = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x9090));
            break;
        case RefreshMutation::View:
            native.mView = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x9090));
            break;
        case RefreshMutation::Layer:
            native.mMetalLayer = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x9090));
            break;
        case RefreshMutation::ZeroScale:
            native.mBackingScale = 0.0;
            break;
        case RefreshMutation::InfiniteScale:
            native.mBackingScale = std::numeric_limits<F64>::infinity();
            break;
        case RefreshMutation::ZeroWidth:
            native.mDrawableWidth = 0;
            break;
        case RefreshMutation::ZeroHeight:
            native.mDrawableHeight = 0;
            break;
    }
    return true;
}

void destroyNativeWindow(void* userdata, LLWindowMacOSXVulkanNativeWindow& native) noexcept
{
    auto& state = *static_cast<FakeState*>(userdata);
    state.record(Event::DestroyNative);
    state.mDestroyedNative = native;
    if (state.mOwnerDuringDestroy)
    {
        state.mRequirementsGoneBeforeNativeDestroy = !state.mOwnerDuringDestroy->hasRequirements();
        state.mInstanceGoneBeforeNativeDestroy     = !state.mOwnerDuringDestroy->instanceGeneration();
    }
    state.mLoaderLiveDuringNativeDestroy = state.mLoaderLive;
    state.mNativeLive                    = false;
    native                               = {};
}

LLRenderVulkan::VulkanSurfaceCreateOutcome createSurface(void*                        userdata,
                                                         LLWindowVulkanFunction       resolver,
                                                         void*                        metal_layer,
                                                         VkInstance                   instance,
                                                         const VkAllocationCallbacks* allocator,
                                                         VkSurfaceKHR*                surface) noexcept
{
    auto& state = *static_cast<FakeState*>(userdata);
    state.record(Event::CreateSurface);
    ++state.mCreateSurfaceCount;
    state.mSurfaceResolver   = resolver;
    state.mSurfaceMetalLayer = metal_layer;
    state.mSurfaceInstance   = instance;
    state.mSurfaceAllocator  = allocator;

    if (surface)
    {
        const bool failed = state.mSurfacePlatformFailure || state.mSurfaceResult != VK_SUCCESS;
        *surface          = state.mNullSurface || (failed && !state.mPoisonSurfaceOutput) ? VK_NULL_HANDLE : state.mSurface;
    }
    if (state.mSurfacePlatformFailure)
    {
        return LLRenderVulkan::VulkanSurfacePlatformFailure{};
    }
    return state.mSurfaceResult;
}

LLWindowMacOSXVulkanOperations fakeOperations(FakeState& state) noexcept
{
    return { &state,
             isMainThread,
             openLoader,
             closeLoader,
             getResolver,
             createNativeWindow,
             refreshNativeWindow,
             destroyNativeWindow,
             createSurface,
             resizeNativeWindowForDiagnostic };
}

LLWindowMacOSXVulkanCreateInfo createInfo()
{
    return { "/diagnostic/libvulkan.dylib", 1280, 720 };
}

void ensureEvents(const char* message, const FakeState& state, std::initializer_list<Event> expected)
{
    bool equal = state.mEventCount == expected.size();
    if (equal)
    {
        std::size_t index = 0;
        for (Event event : expected)
        {
            equal = equal && state.mEvents[index++] == event;
        }
    }
    tut::ensure(message, equal);
}

const LLWindowMacOSXVulkanAcquireError* acquireError(const LLWindowMacOSXVulkanAcquireResult& result) noexcept
{
    return std::get_if<LLWindowMacOSXVulkanAcquireError>(&result);
}

LLWindowMacOSXVulkan* acquiredWindow(LLWindowMacOSXVulkanAcquireResult& result) noexcept
{
    return std::get_if<LLWindowMacOSXVulkan>(&result);
}

void ensureAcquireError(const char* message, const LLWindowMacOSXVulkanAcquireResult& result, LLWindowMacOSXVulkanAcquireCode code)
{
    const auto* error = acquireError(result);
    tut::ensure(message, error && error->mCode == code);
}

void ensureSurfaceError(const char*                                       message,
                        const LLRenderVulkan::VulkanSurfaceAcquireResult& result,
                        LLRenderVulkan::VulkanSurfaceAcquireCode          code)
{
    tut::ensure(message, result && result->mCode == code);
}

const LLRenderVulkan::VulkanSwapchainFrameSlotParentOperationError* operationError(
    const LLRenderVulkan::VulkanSwapchainFrameSlotParentOperationResult& result) noexcept
{
    return std::get_if<LLRenderVulkan::VulkanSwapchainFrameSlotParentOperationError>(&result);
}

const LLRenderVulkan::VulkanSwapchainFrameSlotParentOperationError* presentationError(
    const LLRenderVulkan::VulkanSwapchainFrameSlotParentPresentationResult& result) noexcept
{
    return std::get_if<LLRenderVulkan::VulkanSwapchainFrameSlotParentOperationError>(&result);
}

bool operationSucceeded(const LLRenderVulkan::VulkanSwapchainFrameSlotParentOperationResult& result,
                        LLRenderVulkan::VulkanSwapchainFrameSlotDisposition                 disposition) noexcept
{
    const auto* success = std::get_if<LLRenderVulkan::VulkanSwapchainFrameSlotDisposition>(&result);
    return success && *success == disposition;
}

bool presentationSucceeded(const LLRenderVulkan::VulkanSwapchainFrameSlotParentPresentationResult& result,
                           LLRenderVulkan::VulkanSwapchainFrameSlotPresentationOutcome              outcome,
                           std::uint32_t                                                             image_index) noexcept
{
    const auto* success = std::get_if<LLRenderVulkan::VulkanSwapchainFrameSlotPresentationSuccess>(&result);
    return success && success->mOutcome == outcome && success->mImageIndex == image_index;
}

const LLRenderVulkan::VulkanSwapchainChainRebuildError* rebuildError(
    const LLRenderVulkan::VulkanSwapchainChainRebuildResult& result) noexcept
{
    return std::get_if<LLRenderVulkan::VulkanSwapchainChainRebuildError>(&result);
}

bool acquireCompleteSwapchainChain(LLWindowMacOSXVulkan& owner) noexcept
{
    using namespace LLRenderVulkan;
    return !owner.acquireInstanceGeneration(VulkanInstanceValidationMode::Disabled, VulkanInstancePortabilityMode::Disabled) &&
           !owner.acquireSurfaceGeneration() && !owner.acquirePresentationDeviceGeneration() && !owner.acquireLogicalDeviceGeneration() &&
           !owner.acquireSwapchainConfigurationGeneration() && !owner.acquireSwapchainGeneration() &&
           !owner.acquireSwapchainImagesGeneration() && !owner.acquireSwapchainPresentationTargetGeneration() &&
           !owner.acquireSwapchainPresentationPipelineGeneration() && !owner.acquireSwapchainReadbackGeneration() &&
           !owner.acquireSwapchainFrameSlotGeneration();
}

struct UploadOperationContext
{
    const LLWindowMacOSXVulkan*                     mOwner      = nullptr;
    const LLRenderVulkan::VulkanInstanceGeneration* mGeneration = nullptr;
};

bool uploadInstanceOwnerIsCurrent(void* userdata, const LLRenderVulkan::VulkanInstanceGeneration& generation) noexcept
{
    const auto* context = static_cast<const UploadOperationContext*>(userdata);
    return context && context->mOwner && context->mGeneration == &generation && context->mOwner->instanceGeneration() == &generation;
}

bool uploadWindowGenerationIsCurrent(void* userdata, std::uint64_t native_window_generation) noexcept
{
    const auto* context = static_cast<const UploadOperationContext*>(userdata);
    return context && context->mOwner && context->mOwner->isGenerationCurrent(native_window_generation);
}

struct ResidentUploadDestination
{
    LLRenderContract::BufferHandle mHandle;
    std::uint64_t                  mExpectedIdentity = 0;
    std::uint64_t                  mResidentIdentity = 0;
    VkBuffer                       mBuffer           = VK_NULL_HANDLE;
    VkDeviceMemory                 mMemory           = VK_NULL_HANDLE;
};

bool acquireResidentUploadDestination(LLWindowMacOSXVulkan& owner, ResidentUploadDestination& retained) noexcept
{
    using namespace LLRenderVulkan;

    auto* generation = const_cast<VulkanInstanceGeneration*>(owner.instanceGeneration());
    if (!generation)
    {
        return false;
    }

    const LLRenderContract::TextureUploadFixture fixture = LLRenderContract::makeTextureUploadFixture();
    static_assert(sizeof(fixture.mScreenTriangle) == VULKAN_UPLOAD_SOURCE_BYTE_COUNT);
    VulkanUploadSourceDescription description;
    description.mHandle = LLRenderContract::StreamingUploadHandles{}.mScreenTriangle;
    std::memcpy(description.mBytes.data(), fixture.mScreenTriangle.data(), description.mBytes.size());

    UploadOperationContext                     context{ &owner, generation };
    const VulkanUploadSourceRequest            source_request{ generation->nativeWindowGeneration(),
                                                    description,
                                                               { &context, uploadInstanceOwnerIsCurrent },
                                                               { &context, uploadWindowGenerationIsCurrent } };
    const VulkanUploadDestinationRequest       destination_request{ generation->nativeWindowGeneration(),
                                                              description,
                                                                    { &context, uploadInstanceOwnerIsCurrent },
                                                                    { &context, uploadWindowGenerationIsCurrent } };
    const VulkanUploadTransferRequest          transfer_request{ generation->nativeWindowGeneration(),
                                                        description,
                                                                 { &context, uploadInstanceOwnerIsCurrent },
                                                                 { &context, uploadWindowGenerationIsCurrent } };
    const VulkanUploadTransferOperationRequest operation_request{ generation->nativeWindowGeneration(),
                                                                  description,
                                                                  { &context, uploadInstanceOwnerIsCurrent },
                                                                  { &context, uploadWindowGenerationIsCurrent } };
    if (generation->acquireUploadSourceGeneration(source_request) || generation->acquireUploadDestinationGeneration(destination_request) ||
        generation->acquireUploadTransferGeneration(transfer_request))
    {
        return false;
    }

    const auto  result      = generation->executeUploadTransfer(operation_request);
    const auto* disposition = std::get_if<VulkanUploadTransferDisposition>(&result);
    if (!disposition || *disposition != VulkanUploadTransferDisposition::Complete)
    {
        return false;
    }

    retained = { generation->uploadDestinationResourceHandle(), generation->uploadDestinationExpectedContentIdentity(),
                 generation->uploadDestinationResidentContentIdentity(), generation->uploadDestinationBuffer(),
                 generation->uploadDestinationMemory() };
    const bool completed_exactly_once =
        generation->uploadTransferSubmissionCount() == 1 && generation->uploadTransferCompletionWaitCount() == 1 &&
        generation->uploadDestinationIsResident() && retained.mHandle == description.mHandle && retained.mExpectedIdentity != 0 &&
        retained.mExpectedIdentity == retained.mResidentIdentity && retained.mBuffer != VK_NULL_HANDLE &&
        retained.mMemory != VK_NULL_HANDLE && generation->uploadDestinationByteCount() == VULKAN_UPLOAD_SOURCE_BYTE_COUNT &&
        generation->uploadDestinationIsDeviceLocal();
    const bool source_reset = generation->resetUploadSourceGeneration();
    return completed_exactly_once && source_reset && !generation->hasUploadSourceGeneration() &&
           !generation->hasUploadTransferGeneration() && generation->hasUploadDestinationGeneration() &&
           generation->uploadDestinationResourceHandle() == retained.mHandle &&
           generation->uploadDestinationExpectedContentIdentity() == retained.mExpectedIdentity &&
           generation->uploadDestinationResidentContentIdentity() == retained.mResidentIdentity &&
           generation->uploadDestinationBuffer() == retained.mBuffer && generation->uploadDestinationMemory() == retained.mMemory;
}

void failAllocation()
{
    throw std::bad_alloc();
}

} // namespace

namespace tut
{

struct window_macosx_vulkan_test
{
};

using window_macosx_vulkan_group  = test_group<window_macosx_vulkan_test>;
using window_macosx_vulkan_object = window_macosx_vulkan_group::object;
window_macosx_vulkan_group window_macosx_vulkan_tests("window macOS Vulkan ownership");

template<>
template<>
void window_macosx_vulkan_object::test<1>()
{
    static_assert(LLWINDOWMACOSXVULKAN_STATUS_SUCCESS == 0);
    static_assert(LLWINDOWMACOSXVULKAN_STATUS_INVALID_ARGUMENT == 1);
    static_assert(LLWINDOWMACOSXVULKAN_STATUS_MAIN_THREAD_REQUIRED == 2);
    static_assert(LLWINDOWMACOSXVULKAN_STATUS_APPLICATION_FAILED == 3);
    static_assert(LLWINDOWMACOSXVULKAN_STATUS_STORAGE_FAILED == 4);
    static_assert(LLWINDOWMACOSXVULKAN_STATUS_WINDOW_FAILED == 5);
    static_assert(LLWINDOWMACOSXVULKAN_STATUS_VIEW_FAILED == 6);
    static_assert(LLWINDOWMACOSXVULKAN_STATUS_LAYER_FAILED == 7);
    static_assert(LLWINDOWMACOSXVULKAN_STATUS_GEOMETRY_FAILED == 8);
    static_assert(LLWINDOWMACOSXVULKAN_STATUS_DESTROY_FAILED == 9);
    static_assert(LLWINDOWMACOSXVULKAN_STATUS_DRAWABLE_UNAVAILABLE == 10);
    static_assert(!std::is_copy_constructible_v<LLWindowMacOSXVulkan>);
    static_assert(!std::is_copy_assignable_v<LLWindowMacOSXVulkan>);
    static_assert(std::is_nothrow_move_constructible_v<LLWindowMacOSXVulkan>);
    static_assert(std::is_nothrow_move_assignable_v<LLWindowMacOSXVulkan>);
    static_assert(std::is_nothrow_destructible_v<LLWindowMacOSXVulkan>);
    static_assert(std::is_same_v<decltype(std::declval<LLWindowMacOSXVulkan&>().acquireSwapchainGeneration()),
                                 LLRenderVulkan::VulkanSwapchainAcquireResult>);
    static_assert(noexcept(std::declval<LLWindowMacOSXVulkan&>().acquireSwapchainGeneration()));
    static_assert(std::is_same_v<decltype(std::declval<LLWindowMacOSXVulkan&>().acquireSwapchainImagesGeneration()),
                                 LLRenderVulkan::VulkanSwapchainImagesAcquireResult>);
    static_assert(noexcept(std::declval<LLWindowMacOSXVulkan&>().acquireSwapchainImagesGeneration()));
    static_assert(std::is_same_v<decltype(std::declval<LLWindowMacOSXVulkan&>().acquireSwapchainPresentationTargetGeneration()),
                                 LLRenderVulkan::VulkanSwapchainPresentationTargetAcquireResult>);
    static_assert(noexcept(std::declval<LLWindowMacOSXVulkan&>().acquireSwapchainPresentationTargetGeneration()));
    static_assert(std::is_same_v<decltype(std::declval<LLWindowMacOSXVulkan&>().acquireSwapchainReadbackGeneration()),
                                 LLRenderVulkan::VulkanSwapchainReadbackAcquireResult>);
    static_assert(noexcept(std::declval<LLWindowMacOSXVulkan&>().acquireSwapchainReadbackGeneration()));
    static_assert(std::is_same_v<decltype(std::declval<LLWindowMacOSXVulkan&>().resetSwapchainReadbackGeneration()), bool>);
    static_assert(noexcept(std::declval<LLWindowMacOSXVulkan&>().resetSwapchainReadbackGeneration()));
    static_assert(std::is_same_v<decltype(std::declval<LLWindowMacOSXVulkan&>().acquireSwapchainFrameSlotGeneration()),
                                 LLRenderVulkan::VulkanSwapchainFrameSlotAcquireResult>);
    static_assert(noexcept(std::declval<LLWindowMacOSXVulkan&>().acquireSwapchainFrameSlotGeneration()));
    static_assert(std::is_same_v<decltype(std::declval<LLWindowMacOSXVulkan&>().rebuildSwapchainChain()),
                                 LLRenderVulkan::VulkanSwapchainChainRebuildResult>);
    static_assert(noexcept(std::declval<LLWindowMacOSXVulkan&>().rebuildSwapchainChain()));
    static_assert(noexcept(std::declval<LLWindowMacOSXVulkan&>().resizeNativeDrawableForDiagnostic(U32{}, U32{})));
    static_assert(std::is_same_v<decltype(std::declval<LLWindowMacOSXVulkan&>().roundTripEmptySwapchainFrameSlot()),
                                 LLRenderVulkan::VulkanSwapchainFrameSlotParentOperationResult>);
    static_assert(noexcept(std::declval<LLWindowMacOSXVulkan&>().roundTripEmptySwapchainFrameSlot()));
    static_assert(std::is_same_v<decltype(std::declval<LLWindowMacOSXVulkan&>().retryEmptySwapchainFrameSlotCompletion()),
                                 LLRenderVulkan::VulkanSwapchainFrameSlotParentOperationResult>);
    static_assert(noexcept(std::declval<LLWindowMacOSXVulkan&>().retryEmptySwapchainFrameSlotCompletion()));
    static_assert(std::is_same_v<decltype(std::declval<LLWindowMacOSXVulkan&>().acquireToPresentSwapchainFrameSlot()),
                                 LLRenderVulkan::VulkanSwapchainFrameSlotParentPresentationResult>);
    static_assert(noexcept(std::declval<LLWindowMacOSXVulkan&>().acquireToPresentSwapchainFrameSlot()));
    static_assert(std::is_same_v<decltype(std::declval<LLWindowMacOSXVulkan&>().acquireClearToPresentSwapchainFrameSlot(
                                     std::declval<const LLRenderVulkan::VulkanSwapchainFrameClearColor&>())),
                                 LLRenderVulkan::VulkanSwapchainFrameSlotParentPresentationResult>);
    static_assert(noexcept(std::declval<LLWindowMacOSXVulkan&>().acquireClearToPresentSwapchainFrameSlot(
        std::declval<const LLRenderVulkan::VulkanSwapchainFrameClearColor&>())));
    static_assert(std::is_same_v<decltype(std::declval<LLWindowMacOSXVulkan&>().acquireRenderPassClearToPresentSwapchainFrameSlot(
                                     std::declval<const LLRenderVulkan::VulkanSwapchainFrameClearColor&>())),
                                 LLRenderVulkan::VulkanSwapchainFrameSlotParentPresentationResult>);
    static_assert(noexcept(std::declval<LLWindowMacOSXVulkan&>().acquireRenderPassClearToPresentSwapchainFrameSlot(
        std::declval<const LLRenderVulkan::VulkanSwapchainFrameClearColor&>())));
    static_assert(std::is_same_v<decltype(std::declval<LLWindowMacOSXVulkan&>().acquireRenderPassDrawToPresentSwapchainFrameSlot(
                                     std::declval<const LLRenderVulkan::VulkanSwapchainFrameClearColor&>())),
                                 LLRenderVulkan::VulkanSwapchainFrameSlotParentPresentationResult>);
    static_assert(noexcept(std::declval<LLWindowMacOSXVulkan&>().acquireRenderPassDrawToPresentSwapchainFrameSlot(
        std::declval<const LLRenderVulkan::VulkanSwapchainFrameClearColor&>())));
    static_assert(noexcept(std::declval<LLWindowMacOSXVulkan&>().retrySwapchainFrameSlotPresentation()));
    static_assert(noexcept(std::declval<LLWindowMacOSXVulkan&>().retrySwapchainFrameSlotPresentationCompletion()));
    static_assert(noexcept(std::declval<LLWindowMacOSXVulkan&>().cancelSwapchainFrameSlotPresentation()));
    static_assert(noexcept(std::declval<LLWindowMacOSXVulkan&>().retrySwapchainFrameSlotCancellationCompletion()));
    static_assert(noexcept(std::declval<LLWindowMacOSXVulkan&>().resetSwapchainFrameSlotGeneration()));
    static_assert(std::is_same_v<decltype(std::declval<LLWindowMacOSXVulkan&>().resetSwapchainPresentationTargetGeneration()), bool>);
    static_assert(noexcept(std::declval<LLWindowMacOSXVulkan&>().resetSwapchainPresentationTargetGeneration()));
    static_assert(noexcept(std::declval<LLWindowMacOSXVulkan&>().resetSwapchainImagesGeneration()));
    static_assert(noexcept(std::declval<LLWindowMacOSXVulkan&>().resetSwapchainGeneration()));
    static_assert(std::is_same_v<decltype(std::declval<LLWindowMacOSXVulkan&>().reset()), bool>);
    static_assert(noexcept(std::declval<LLWindowMacOSXVulkan&>().reset()));
    static_assert(std::is_same_v<decltype(std::declval<const LLWindowMacOSXVulkan&>().requirements()), const LLWindowVulkanRequirements*>);
    static_assert(noexcept(acquireLLWindowMacOSXVulkan(std::declval<const LLWindowMacOSXVulkanCreateInfo&>(),
                                                       U64{},
                                                       std::declval<const LLWindowMacOSXVulkanOperations&>())));

    const auto& operations = defaultLLWindowMacOSXVulkanOperations();
    ensure("the production operation table is complete",
           operations.mIsMainThread && operations.mOpenLoader && operations.mCloseLoader && operations.mGetResolver &&
               operations.mCreateNativeWindow && operations.mRefreshNativeWindow && operations.mResizeNativeWindowForDiagnostic &&
               operations.mDestroyNativeWindow && operations.mCreateSurface);

    const LLWindowMacOSXVulkanOperations legacy_positional_operations{ nullptr,
                                                                       isMainThread,
                                                                       openLoader,
                                                                       closeLoader,
                                                                       getResolver,
                                                                       createNativeWindow,
                                                                       refreshNativeWindow,
                                                                       destroyNativeWindow,
                                                                       createSurface };
    ensure("the appended diagnostic callback preserves old positional operation initializers",
           legacy_positional_operations.mCreateSurface == createSurface &&
               legacy_positional_operations.mResizeNativeWindowForDiagnostic == nullptr);
}

template<>
template<>
void window_macosx_vulkan_object::test<2>()
{
    FakeState   state;
    ScopedState active(state);
    auto        info   = createInfo();
    auto        result = acquireLLWindowMacOSXVulkan(info, 41, fakeOperations(state));
    auto*       owner  = acquiredWindow(result);

    ensure("acquisition succeeds", owner != nullptr);
    ensureEvents("acquisition opens the loader before resolving and creating native state", state,
                 { Event::OpenLoader, Event::GetResolver, Event::CreateNative });
    ensure("the explicit loader remains live", state.mLoaderLive);
    ensure("resolver lookup receives the exact loader", state.mResolverLoader == state.mLoader);
    ensure("native creation receives the exact description", state.mCreateInfo == &info);
    ensure("the description preserves loader and backing dimensions",
           state.mCreateInfo->mLoaderPath == "/diagnostic/libvulkan.dylib" && state.mCreateInfo->mBackingWidth == 1280 &&
               state.mCreateInfo->mBackingHeight == 720 && std::strcmp(state.mOpenedPath, state.mCreateInfo->mLoaderPath.c_str()) == 0);
    ensure("the owner publishes only scalar native state",
           owner->hasNativeWindow() && owner->backingScale() == 2.0 && owner->drawableWidth() == 1280 && owner->drawableHeight() == 720);
    ensure("the owner publishes requirements for the exact generation",
           owner->hasRequirements() && owner->isGenerationCurrent(41) && !owner->isGenerationCurrent(0) && !owner->isGenerationCurrent(42));
    const auto& extensions = owner->requirements()->requiredInstanceExtensions();
    ensure("the required extension pair is retained in platform order",
           extensions == std::vector<std::string>{ VK_KHR_SURFACE_EXTENSION_NAME, "VK_EXT_metal_surface" } &&
               owner->requirements()->resolver() == fakeResolver());

    state.mRefreshScale  = 1.5;
    state.mRefreshWidth  = 1440;
    state.mRefreshHeight = 900;
    ensure("a valid refresh updates only published geometry", owner->refreshNativeGeometry());
    ensure("refreshed geometry is published",
           owner->backingScale() == 1.5 && owner->drawableWidth() == 1440 && owner->drawableHeight() == 900);

    ensure("the complete Vulkan ownership chain is acquired before the affinity check",
           !owner->acquireInstanceGeneration(LLRenderVulkan::VulkanInstanceValidationMode::Required,
                                             LLRenderVulkan::VulkanInstancePortabilityMode::EnableIfAvailable) &&
               !owner->acquireSurfaceGeneration());
    ensure("the complete chain owns the exact parent and surface handles",
           owner->instanceGeneration() && owner->instanceGeneration()->instance() == state.mInstance &&
               owner->instanceGeneration()->surface() == state.mSurface);

    state.mMainThread = false;
    ensure("off-main reset fails without releasing any owner", !owner->reset());
    ensure("off-main reset preserves the complete ownership chain",
           owner->hasNativeWindow() && owner->hasRequirements() && owner->instanceGeneration() &&
               owner->instanceGeneration()->instance() == state.mInstance && owner->instanceGeneration()->surface() == state.mSurface &&
               state.mNativeLive && state.mLoaderLive);
    ensure("off-main reset destroys no Vulkan owner",
           state.mDestroySurfaceCount == 0 && state.mDestroyDebugCount == 0 && state.mDestroyInstanceCount == 0);
    ensure_equals("off-main reset invokes no teardown operation", state.mEventCount, std::size_t{ 9 });

    state.mMainThread         = true;
    state.mOwnerDuringDestroy = owner;
    ensure("main-thread reset releases the complete owner", owner->reset());
    ensureEvents("reset releases the full Vulkan and native chain before the loader", state,
                 { Event::OpenLoader, Event::GetResolver, Event::CreateNative, Event::RefreshNative, Event::RefreshNative,
                   Event::CreateInstance, Event::CreateDebugMessenger, Event::RefreshNative, Event::CreateSurface, Event::DestroySurface,
                   Event::DestroyDebugMessenger, Event::DestroyInstance, Event::DestroyNative, Event::CloseLoader });
    ensure("requirements and instances are gone before native destruction",
           state.mRequirementsGoneBeforeNativeDestroy && state.mInstanceGoneBeforeNativeDestroy);
    ensure("the loader remains live during native destruction", state.mLoaderLiveDuringNativeDestroy);
    ensure("native destruction receives the exact token, window, view, and Metal layer",
           state.mDestroyedNative.mToken == state.mToken && state.mDestroyedNative.mWindow == state.mWindow &&
               state.mDestroyedNative.mView == state.mView && state.mDestroyedNative.mMetalLayer == state.mMetalLayer);
    ensure("all owned state is gone before loader close", state.mAllOwnersGoneBeforeLoaderClose);
    ensure("reset clears the public state",
           !owner->hasNativeWindow() && !owner->hasRequirements() && !owner->instanceGeneration() && !state.mLoaderLive);

    ensure("reset is idempotent after ownership is empty", owner->reset());
    ensure_equals("a second reset performs no operation", state.mEventCount, std::size_t{ 14 });
}

template<>
template<>
void window_macosx_vulkan_object::test<3>()
{
    const auto info = createInfo();

    FakeState invalid_state;
    auto      operations   = fakeOperations(invalid_state);
    operations.mOpenLoader = nullptr;
    auto invalid           = acquireLLWindowMacOSXVulkan(info, 1, operations);
    ensureAcquireError("an incomplete operation table is rejected", invalid, LLWindowMacOSXVulkanAcquireCode::InvalidOperations);
    ensure_equals("operation validation has no side effect", invalid_state.mEventCount, std::size_t{ 0 });

    operations               = fakeOperations(invalid_state);
    operations.mIsMainThread = nullptr;
    invalid                  = acquireLLWindowMacOSXVulkan(info, 1, operations);
    ensureAcquireError("a missing main-thread operation is rejected", invalid, LLWindowMacOSXVulkanAcquireCode::InvalidOperations);

    operations                      = fakeOperations(invalid_state);
    operations.mRefreshNativeWindow = nullptr;
    invalid                         = acquireLLWindowMacOSXVulkan(info, 1, operations);
    ensureAcquireError("a missing refresh operation is rejected", invalid, LLWindowMacOSXVulkanAcquireCode::InvalidOperations);

    FakeState no_resize_state;
    operations                                  = fakeOperations(no_resize_state);
    operations.mResizeNativeWindowForDiagnostic = nullptr;
    auto no_resize                              = acquireLLWindowMacOSXVulkan(info, 1, operations);
    auto* no_resize_owner                       = acquiredWindow(no_resize);
    ensure("a missing diagnostic resize seam does not invalidate ordinary ownership", no_resize_owner != nullptr);
    ensure("a missing diagnostic resize seam rejects only an explicit diagnostic resize",
           !no_resize_owner->resizeNativeDrawableForDiagnostic(1600, 900));
    ensure("the owner without a diagnostic resize seam tears down normally", no_resize_owner->reset());

    operations                = fakeOperations(invalid_state);
    operations.mCreateSurface = nullptr;
    invalid                   = acquireLLWindowMacOSXVulkan(info, 1, operations);
    ensureAcquireError("a missing surface operation is rejected", invalid, LLWindowMacOSXVulkanAcquireCode::InvalidOperations);

    auto invalid_width          = info;
    invalid_width.mBackingWidth = 0;
    auto width                  = acquireLLWindowMacOSXVulkan(invalid_width, 1, fakeOperations(invalid_state));
    ensureAcquireError("zero backing width is rejected", width, LLWindowMacOSXVulkanAcquireCode::InvalidCreateInfo);

    auto invalid_height           = info;
    invalid_height.mBackingHeight = 0;
    auto height                   = acquireLLWindowMacOSXVulkan(invalid_height, 1, fakeOperations(invalid_state));
    ensureAcquireError("zero backing height is rejected", height, LLWindowMacOSXVulkanAcquireCode::InvalidCreateInfo);
    ensure_equals("invalid dimensions are rejected before opening a loader", invalid_state.mEventCount, std::size_t{ 0 });

    FakeState thread_state;
    thread_state.mMainThread = false;
    auto thread              = acquireLLWindowMacOSXVulkan(info, 1, fakeOperations(thread_state));
    ensureAcquireError("off-main acquisition is rejected", thread, LLWindowMacOSXVulkanAcquireCode::MainThreadRequired);
    ensure_equals("off-main acquisition has no process or loader side effect", thread_state.mEventCount, std::size_t{ 0 });

    FakeState loader_state;
    loader_state.mLoaderOpens = false;
    auto loader               = acquireLLWindowMacOSXVulkan(info, 1, fakeOperations(loader_state));
    ensureAcquireError("loader failure is typed", loader, LLWindowMacOSXVulkanAcquireCode::LoaderFailure);
    ensureEvents("loader failure makes no later call", loader_state, { Event::OpenLoader });

    FakeState resolver_state;
    resolver_state.mResolverLive = false;
    auto resolver                = acquireLLWindowMacOSXVulkan(info, 1, fakeOperations(resolver_state));
    ensureAcquireError("resolver failure is typed", resolver, LLWindowMacOSXVulkanAcquireCode::ResolverFailure);
    ensureEvents("resolver failure closes the loader without creating native state", resolver_state,
                 { Event::OpenLoader, Event::GetResolver, Event::CloseLoader });
    ensure("resolver failure passes the exact loader and releases it",
           resolver_state.mResolverLoader == resolver_state.mLoader && !resolver_state.mLoaderLive);
}

template<>
template<>
void window_macosx_vulkan_object::test<4>()
{
    constexpr std::array failures{
        LLWindowMacOSXVulkanNativeCreateCode::InvalidRequest,     LLWindowMacOSXVulkanNativeCreateCode::MainThreadFailure,
        LLWindowMacOSXVulkanNativeCreateCode::ApplicationFailure, LLWindowMacOSXVulkanNativeCreateCode::StorageFailure,
        LLWindowMacOSXVulkanNativeCreateCode::WindowFailure,      LLWindowMacOSXVulkanNativeCreateCode::ViewFailure,
        LLWindowMacOSXVulkanNativeCreateCode::LayerFailure,       LLWindowMacOSXVulkanNativeCreateCode::GeometryFailure
    };

    for (const auto code : failures)
    {
        FakeState state;
        state.mNativeFailure = code;
        auto result          = acquireLLWindowMacOSXVulkan(createInfo(), 1, fakeOperations(state));
        ensureAcquireError("native creation failure is typed", result, LLWindowMacOSXVulkanAcquireCode::NativeWindowFailure);
        const auto* error = acquireError(result);
        ensure("the exact native failure is retained",
               error && error->mNativeError && error->mNativeError->mCode == code && !error->mRequirementsError);
        ensureEvents("native failure closes only the earned loader", state,
                     { Event::OpenLoader, Event::GetResolver, Event::CreateNative, Event::CloseLoader });
        ensure("native failure does not claim a native object", !state.mNativeLive && !state.mLoaderLive);
    }
}

template<>
template<>
void window_macosx_vulkan_object::test<5>()
{
    const auto info = createInfo();

    for (std::size_t identity = 0; identity < 4; ++identity)
    {
        FakeState state;
        if (identity == 0)
        {
            state.mNative.mToken = nullptr;
        }
        else if (identity == 1)
        {
            state.mNative.mWindow = nullptr;
        }
        else if (identity == 2)
        {
            state.mNative.mView = nullptr;
        }
        else
        {
            state.mNative.mMetalLayer = nullptr;
        }
        auto result = acquireLLWindowMacOSXVulkan(info, 1, fakeOperations(state));
        ensureAcquireError("a poisoned native identity is rejected", result, LLWindowMacOSXVulkanAcquireCode::NativeWindowIdentityFailure);
        ensureEvents("a poisoned success is destroyed before closing its loader", state,
                     { Event::OpenLoader, Event::GetResolver, Event::CreateNative, Event::DestroyNative, Event::CloseLoader });
        ensure("identity rollback releases both earned resources", !state.mNativeLive && !state.mLoaderLive);
    }

    for (std::size_t geometry = 0; geometry < 7; ++geometry)
    {
        FakeState state;
        switch (geometry)
        {
            case 0:
                state.mNative.mBackingScale = 0.0;
                break;
            case 1:
                state.mNative.mBackingScale = -1.0;
                break;
            case 2:
                state.mNative.mBackingScale = std::numeric_limits<F64>::infinity();
                break;
            case 3:
                state.mNative.mBackingScale = std::numeric_limits<F64>::quiet_NaN();
                break;
            case 4:
                state.mNative.mDrawableWidth = 0;
                break;
            case 5:
                state.mNative.mDrawableHeight = 0;
                break;
            default:
                state.mNative.mDrawableWidth = info.mBackingWidth + 1;
                break;
        }
        auto result = acquireLLWindowMacOSXVulkan(info, 1, fakeOperations(state));
        ensureAcquireError("invalid or mismatched native geometry is rejected", result,
                           LLWindowMacOSXVulkanAcquireCode::NativeWindowGeometryFailure);
        ensureEvents("bad geometry rolls back native state before the loader", state,
                     { Event::OpenLoader, Event::GetResolver, Event::CreateNative, Event::DestroyNative, Event::CloseLoader });
    }
}

template<>
template<>
void window_macosx_vulkan_object::test<6>()
{
    const auto info = createInfo();

    FakeState generation_state;
    auto      generation = acquireLLWindowMacOSXVulkan(info, 0, fakeOperations(generation_state));
    ensureAcquireError("zero native generation is rejected before acquisition", generation,
                       LLWindowMacOSXVulkanAcquireCode::InvalidNativeWindowGeneration);
    const auto* generation_error = acquireError(generation);
    ensure("the static generation error carries no nested acquired-state failure",
           generation_error && !generation_error->mNativeError && !generation_error->mRequirementsError);
    ensure_equals("invalid generation creates no loader, NSApplication, or native window", generation_state.mEventCount, std::size_t{ 0 });

    FakeState allocation_state;
    auto      allocation = LLWindowMacOSXVulkanDetail::acquire(info, 1, fakeOperations(allocation_state), failAllocation);
    ensureAcquireError("requirements allocation failure is typed", allocation, LLWindowMacOSXVulkanAcquireCode::RequirementsFailure);
    const auto* allocation_error = acquireError(allocation);
    ensure("the exact allocation error is retained",
           allocation_error && allocation_error->mRequirementsError &&
               allocation_error->mRequirementsError->mCode == LLWindowVulkanRequirementsBuildCode::AllocationFailure);
    ensureEvents("allocation failure rolls back native state before the loader", allocation_state,
                 { Event::OpenLoader, Event::GetResolver, Event::CreateNative, Event::DestroyNative, Event::CloseLoader });
}

template<>
template<>
void window_macosx_vulkan_object::test<7>()
{
    using namespace LLRenderVulkan;

    FakeState   state;
    ScopedState active(state);
    auto        result = acquireLLWindowMacOSXVulkan(createInfo(), 17, fakeOperations(state));
    auto*       owner  = acquiredWindow(result);
    ensure("refresh fixture acquired native state", owner != nullptr);

    state.mRefreshSucceeds = false;
    ensure("platform refresh failure is rejected", !owner->refreshNativeGeometry());
    ensure("failed refresh preserves published geometry",
           owner->backingScale() == 2.0 && owner->drawableWidth() == 1280 && owner->drawableHeight() == 720);

    state.mRefreshSucceeds = true;
    constexpr std::array identity_mutations{ RefreshMutation::Token, RefreshMutation::Window, RefreshMutation::View,
                                             RefreshMutation::Layer };
    for (const auto mutation : identity_mutations)
    {
        state.mRefreshMutation = mutation;
        ensure("changed native identity is rejected", !owner->refreshNativeGeometry());
    }

    constexpr std::array geometry_mutations{ RefreshMutation::ZeroScale, RefreshMutation::InfiniteScale, RefreshMutation::ZeroWidth,
                                             RefreshMutation::ZeroHeight };
    for (const auto mutation : geometry_mutations)
    {
        state.mRefreshMutation = mutation;
        ensure("poisoned refresh geometry is rejected", !owner->refreshNativeGeometry());
        ensure("poisoned geometry does not replace the last valid snapshot",
               owner->backingScale() == 2.0 && owner->drawableWidth() == 1280 && owner->drawableHeight() == 720);
    }

    state.mRefreshMutation = RefreshMutation::Layer;
    const auto stale_instance =
        owner->acquireInstanceGeneration(VulkanInstanceValidationMode::Disabled, VulkanInstancePortabilityMode::Disabled);
    ensure("instance acquisition rejects changed native identity before Vulkan",
           stale_instance && stale_instance->mCode == VulkanInstanceAcquireCode::StaleWindowGeneration &&
               owner->instanceGeneration() == nullptr);

    state.mRefreshMutation = RefreshMutation::None;
    state.mRefreshScale    = 1.25;
    state.mRefreshWidth    = 1600;
    state.mRefreshHeight   = 1000;
    ensure("a valid refresh can recover after every rejected snapshot", owner->refreshNativeGeometry());
    ensure("recovery publishes the new scalar geometry",
           owner->backingScale() == 1.25 && owner->drawableWidth() == 1600 && owner->drawableHeight() == 1000);
    ensure("instance acquisition succeeds after refresh recovery",
           !owner->acquireInstanceGeneration(VulkanInstanceValidationMode::Disabled, VulkanInstancePortabilityMode::Disabled));

    state.mRefreshMutation   = RefreshMutation::Token;
    const auto stale_surface = owner->acquireSurfaceGeneration();
    ensureSurfaceError("surface acquisition rejects changed native identity before its creator", stale_surface,
                       VulkanSurfaceAcquireCode::StaleWindowGeneration);
    ensure_equals("stale surface acquisition never invokes the platform creator", state.mCreateSurfaceCount, std::size_t{ 0 });

    state.mRefreshMutation = RefreshMutation::None;
    ensure("surface acquisition succeeds after identity recovery", !owner->acquireSurfaceGeneration());
    ensure("recovery retains the original private layer",
           state.mSurfaceMetalLayer == state.mMetalLayer && state.mSurfaceResolver == fakeResolver());

    state.mOwnerDuringDestroy = owner;
    ensure("refresh fixture teardown succeeds", owner->reset());
}

template<>
template<>
void window_macosx_vulkan_object::test<8>()
{
    using namespace LLRenderVulkan;

    FakeState   state;
    ScopedState active(state);
    auto        result = acquireLLWindowMacOSXVulkan(createInfo(), 31, fakeOperations(state));
    auto*       owner  = acquiredWindow(result);
    ensure("lifecycle fixture acquired native state", owner != nullptr);

    const auto instance_error =
        owner->acquireInstanceGeneration(VulkanInstanceValidationMode::Required, VulkanInstancePortabilityMode::EnableIfAvailable);
    ensure("a validated portable Vulkan 1.1 instance is acquired", !instance_error);
    ensure("the owner retains the exact instance generation",
           owner->instanceGeneration() && owner->instanceGeneration()->instance() == state.mInstance &&
               owner->instanceGeneration()->apiVersion() == VK_API_VERSION_1_1 &&
               owner->instanceGeneration()->nativeWindowGeneration() == 31 && owner->instanceGeneration()->validationEnabled() &&
               owner->instanceGeneration()->portabilityEnumerationEnabled());
    const auto& enabled_extensions = owner->instanceGeneration()->enabledExtensions();
    ensure("explicit diagnostic instance acquisition adds surface maintenance dependencies in exact order",
           state.mSurfaceCapabilities2Enabled && state.mSurfaceMaintenanceEnabled && enabled_extensions.size() == 6 &&
               enabled_extensions[0] == VK_KHR_SURFACE_EXTENSION_NAME && enabled_extensions[1] == "VK_EXT_metal_surface" &&
               enabled_extensions[2] == SURFACE_CAPABILITIES_2_EXTENSION && enabled_extensions[3] == SURFACE_MAINTENANCE_EXTENSION &&
               enabled_extensions[4] == VK_EXT_DEBUG_UTILS_EXTENSION_NAME && enabled_extensions[5] == "VK_KHR_portability_enumeration" &&
               owner->requirements()->requiredInstanceExtensions() ==
                   std::vector<std::string>{ VK_KHR_SURFACE_EXTENSION_NAME, "VK_EXT_metal_surface" });

    ensure("the Metal surface generation is acquired", !owner->acquireSurfaceGeneration());
    ensure("the parent owns the exact surface generation",
           owner->instanceGeneration()->hasSurfaceGeneration() && owner->instanceGeneration()->surface() == state.mSurface &&
               owner->instanceGeneration()->surfaceNativeWindowGeneration() == 31);
    ensure("surface creation receives exact resolver, layer, instance, and allocator identities",
           state.mSurfaceResolver == fakeResolver() && state.mSurfaceMetalLayer == state.mMetalLayer &&
               state.mSurfaceInstance == state.mInstance && !state.mSurfaceAllocator);

    const auto duplicate_surface = owner->acquireSurfaceGeneration();
    ensureSurfaceError("a second surface is rejected", duplicate_surface, VulkanSurfaceAcquireCode::SurfaceAlreadyOwned);
    ensure_equals("duplicate surface acquisition makes no second platform call", state.mCreateSurfaceCount, std::size_t{ 1 });
    const auto duplicate_instance =
        owner->acquireInstanceGeneration(VulkanInstanceValidationMode::Required, VulkanInstancePortabilityMode::EnableIfAvailable);
    ensure("a second instance is rejected without replacing the parent",
           duplicate_instance && duplicate_instance->mCode == VulkanInstanceAcquireCode::InstanceAlreadyOwned &&
               owner->instanceGeneration()->instance() == state.mInstance);

    LLWindowMacOSXVulkan moved(std::move(*owner));
    ensure("move construction transfers native, requirement, instance, and surface generations",
           moved.hasNativeWindow() && moved.hasRequirements() && moved.isGenerationCurrent(31) && moved.instanceGeneration() &&
               moved.instanceGeneration()->surface() == state.mSurface);
    ensure("the moved source publishes no owned state",
           !owner->hasNativeWindow() && !owner->hasRequirements() && !owner->instanceGeneration());

    state.mOwnerDuringDestroy = &moved;
    ensure("moved owner teardown succeeds", moved.reset());
    ensureEvents("full reset follows loader, native, instance, surface, then exact reverse teardown order", state,
                 { Event::OpenLoader, Event::GetResolver, Event::CreateNative, Event::RefreshNative, Event::CreateInstance,
                   Event::CreateDebugMessenger, Event::RefreshNative, Event::CreateSurface, Event::RefreshNative, Event::DestroySurface,
                   Event::DestroyDebugMessenger, Event::DestroyInstance, Event::DestroyNative, Event::CloseLoader });
    ensure("surface teardown keeps its parent, native state, requirements, and loader live",
           state.mRequirementsLiveDuringSurfaceDestroy && state.mInstanceLiveDuringSurfaceDestroy &&
               state.mNativeLiveDuringSurfaceDestroy && state.mLoaderLiveDuringSurfaceDestroy);
    ensure("messenger teardown follows surface teardown while requirements and loader remain live",
           state.mRequirementsLiveDuringMessengerDestroy && state.mSurfaceAbsentDuringMessengerDestroy &&
               state.mLoaderLiveDuringMessengerDestroy);
    ensure("instance teardown follows messenger teardown while requirements and loader remain live",
           state.mRequirementsLiveDuringInstanceDestroy && state.mSurfaceAbsentDuringInstanceDestroy &&
               state.mLoaderLiveDuringInstanceDestroy);
    ensure("requirements and instance are gone before native destruction",
           state.mRequirementsGoneBeforeNativeDestroy && state.mInstanceGoneBeforeNativeDestroy && state.mLoaderLiveDuringNativeDestroy);
    ensure("native state is gone before loader close", state.mAllOwnersGoneBeforeLoaderClose);
    ensure("each Vulkan object is destroyed once",
           state.mDestroySurfaceCount == 1 && state.mDestroyDebugCount == 1 && state.mDestroyInstanceCount == 1);
}

template<>
template<>
void window_macosx_vulkan_object::test<9>()
{
    using namespace LLRenderVulkan;

    FakeState   state;
    ScopedState active(state);
    auto        result = acquireLLWindowMacOSXVulkan(createInfo(), 51, fakeOperations(state));
    auto*       owner  = acquiredWindow(result);
    ensure("surface failure fixture acquired native state", owner != nullptr);

    const auto missing_parent = owner->acquireSurfaceGeneration();
    ensureSurfaceError("surface acquisition requires a live instance", missing_parent, VulkanSurfaceAcquireCode::InstanceNotLive);
    ensure_equals("missing parent does not refresh or create", state.mRefreshCount, std::size_t{ 0 });

    ensure("surface failure fixture acquired an instance",
           !owner->acquireInstanceGeneration(VulkanInstanceValidationMode::Disabled, VulkanInstancePortabilityMode::Disabled));

    state.mExposeDestroySurface = false;
    const auto missing_destroy  = owner->acquireSurfaceGeneration();
    ensureSurfaceError("missing destroy command fails before platform creation", missing_destroy,
                       VulkanSurfaceAcquireCode::MissingRequiredInstanceCommand);
    ensure("the exact missing command is retained",
           missing_destroy && missing_destroy->mCommand && *missing_destroy->mCommand == VulkanSurfaceCommand::DestroySurface);
    ensure_equals("missing destroy command makes no platform call", state.mCreateSurfaceCount, std::size_t{ 0 });

    state.mExposeDestroySurface   = true;
    state.mSurfacePlatformFailure = true;
    state.mPoisonSurfaceOutput    = true;
    const auto platform_failure   = owner->acquireSurfaceGeneration();
    ensureSurfaceError("platform failure remains distinct from VkResult failure", platform_failure,
                       VulkanSurfaceAcquireCode::PlatformCreationFailure);
    ensure("platform failure carries no invented result and ignores poisoned output",
           platform_failure && !platform_failure->mResult && !owner->instanceGeneration()->hasSurfaceGeneration() &&
               state.mDestroySurfaceCount == 0);

    state.mSurfacePlatformFailure = false;
    state.mSurfaceResult          = VK_ERROR_OUT_OF_HOST_MEMORY;
    const auto vulkan_failure     = owner->acquireSurfaceGeneration();
    ensureSurfaceError("VkResult surface failure is typed", vulkan_failure, VulkanSurfaceAcquireCode::SurfaceCreationFailure);
    ensure("the exact VkResult is retained and poisoned output is ignored",
           vulkan_failure && vulkan_failure->mResult == VK_ERROR_OUT_OF_HOST_MEMORY &&
               !owner->instanceGeneration()->hasSurfaceGeneration() && state.mDestroySurfaceCount == 0);

    state.mSurfaceResult       = VK_SUCCESS;
    state.mPoisonSurfaceOutput = false;
    state.mNullSurface         = true;
    const auto null_success    = owner->acquireSurfaceGeneration();
    ensureSurfaceError("success with a null surface is rejected", null_success, VulkanSurfaceAcquireCode::NullSurfaceOnSuccess);
    ensure("null success publishes and destroys nothing",
           !owner->instanceGeneration()->hasSurfaceGeneration() && state.mDestroySurfaceCount == 0);

    state.mNullSurface = false;
    ensure("later acquisition succeeds after all failed outcomes", !owner->acquireSurfaceGeneration());
    ensure_equals("the four creator outcomes invoke the platform exactly four times", state.mCreateSurfaceCount, std::size_t{ 4 });

    state.mOwnerDuringDestroy = owner;
    ensure("surface failure fixture teardown succeeds", owner->reset());
    ensure_equals("only the earned surface is destroyed", state.mDestroySurfaceCount, std::size_t{ 1 });
    ensure_equals("failure recovery keeps one parent destruction", state.mDestroyInstanceCount, std::size_t{ 1 });
}

template<>
template<>
void window_macosx_vulkan_object::test<10>()
{
    using namespace LLRenderVulkan;

    FakeState   state;
    ScopedState active(state);
    auto        result = acquireLLWindowMacOSXVulkan(createInfo(), 61, fakeOperations(state));
    auto*       owner  = acquiredWindow(result);
    ensure("surface reset fixture acquired native state", owner != nullptr);
    ensure("surface reset fixture acquired an instance",
           !owner->acquireInstanceGeneration(VulkanInstanceValidationMode::Disabled, VulkanInstancePortabilityMode::Disabled));

    const auto allocation_failure = LLWindowMacOSXVulkanDetail::acquireSurfaceGeneration(*owner, failAllocation);
    ensureSurfaceError("surface owner allocation failure is typed", allocation_failure, VulkanSurfaceAcquireCode::AllocationFailure);
    ensure("allocation failure leaves parent, requirements, native state, and loader live",
           owner->instanceGeneration() && owner->hasRequirements() && owner->hasNativeWindow() && state.mLoaderLive);
    ensure("allocation failure invokes no platform creator and publishes no child",
           state.mCreateSurfaceCount == 0 && !owner->instanceGeneration()->hasSurfaceGeneration());

    ensure("normal surface acquisition recovers", !owner->acquireSurfaceGeneration());
    state.mOwnerDuringDestroy = owner;
    ensure("explicit surface reset reports its owned child", owner->resetSurfaceGeneration());
    ensure("explicit reset leaves validation-independent parent state live",
           owner->instanceGeneration() && owner->hasRequirements() && owner->hasNativeWindow() && state.mLoaderLive &&
               !owner->instanceGeneration()->hasSurfaceGeneration());
    ensure("a second surface reset is idempotent", !owner->resetSurfaceGeneration());
    ensure_equals("only the first explicit reset destroys a surface", state.mDestroySurfaceCount, std::size_t{ 1 });

    ensure("the current parent can reacquire a surface", !owner->acquireSurfaceGeneration());
    ensure("surface reset fixture teardown succeeds", owner->reset());
    ensure_equals("both earned surfaces are destroyed once", state.mDestroySurfaceCount, std::size_t{ 2 });
    ensure_equals("the shared instance is destroyed once", state.mDestroyInstanceCount, std::size_t{ 1 });
}

template<>
template<>
void window_macosx_vulkan_object::test<11>()
{
    using namespace LLRenderVulkan;

    FakeState source_state;
    FakeState destination_state;
    source_state.mLoader     = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x11110));
    source_state.mToken      = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x12220));
    source_state.mWindow     = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x13330));
    source_state.mView       = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x14440));
    source_state.mMetalLayer = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x15550));
    source_state.mNative     = { source_state.mToken, source_state.mWindow, source_state.mView, source_state.mMetalLayer, 2.0, 1280, 720 };
    source_state.mInstance   = reinterpret_cast<VkInstance>(static_cast<std::uintptr_t>(0x16660));
    source_state.mDebugMessenger = reinterpret_cast<VkDebugUtilsMessengerEXT>(static_cast<std::uintptr_t>(0x17770));
    source_state.mSurface        = reinterpret_cast<VkSurfaceKHR>(static_cast<std::uintptr_t>(0x18880));

    ScopedState active(source_state);
    auto        source_result = acquireLLWindowMacOSXVulkan(createInfo(), 71, fakeOperations(source_state));
    auto*       source        = acquiredWindow(source_result);
    ensure("move source acquired native state", source != nullptr);
    ensure("move source acquired a validated instance",
           !source->acquireInstanceGeneration(VulkanInstanceValidationMode::Required, VulkanInstancePortabilityMode::EnableIfAvailable));
    ensure("move source acquired a surface", !source->acquireSurfaceGeneration());

    active.use(destination_state);
    auto  destination_result = acquireLLWindowMacOSXVulkan(createInfo(), 72, fakeOperations(destination_state));
    auto* destination        = acquiredWindow(destination_result);
    ensure("move destination acquired native state", destination != nullptr);
    ensure(
        "move destination acquired a validated instance",
        !destination->acquireInstanceGeneration(VulkanInstanceValidationMode::Required, VulkanInstancePortabilityMode::EnableIfAvailable));
    ensure("move destination acquired a surface", !destination->acquireSurfaceGeneration());

    destination_state.mOwnerDuringDestroy = destination;
    *destination                          = std::move(*source);
    ensure("move assignment clears the source owner",
           !source->hasNativeWindow() && !source->hasRequirements() && !source->instanceGeneration());
    ensure("move assignment transfers every source generation",
           destination->hasNativeWindow() && destination->isGenerationCurrent(71) && destination->instanceGeneration() &&
               destination->instanceGeneration()->instance() == source_state.mInstance &&
               destination->instanceGeneration()->surface() == source_state.mSurface);
    ensureEvents("move assignment tears down the replaced destination in strict order", destination_state,
                 { Event::OpenLoader, Event::GetResolver, Event::CreateNative, Event::RefreshNative, Event::CreateInstance,
                   Event::CreateDebugMessenger, Event::RefreshNative, Event::CreateSurface, Event::DestroySurface,
                   Event::DestroyDebugMessenger, Event::DestroyInstance, Event::DestroyNative, Event::CloseLoader });
    ensure("replaced destination teardown preserves all lifetime boundaries",
           destination_state.mRequirementsLiveDuringSurfaceDestroy && destination_state.mInstanceLiveDuringSurfaceDestroy &&
               destination_state.mRequirementsLiveDuringMessengerDestroy && destination_state.mRequirementsLiveDuringInstanceDestroy &&
               destination_state.mRequirementsGoneBeforeNativeDestroy && destination_state.mAllOwnersGoneBeforeLoaderClose);

    active.use(source_state);
    source_state.mOwnerDuringDestroy = destination;
    ensure("transferred owner teardown succeeds", destination->reset());
    ensureEvents("the transferred resources keep source teardown order", source_state,
                 { Event::OpenLoader, Event::GetResolver, Event::CreateNative, Event::RefreshNative, Event::CreateInstance,
                   Event::CreateDebugMessenger, Event::RefreshNative, Event::CreateSurface, Event::DestroySurface,
                   Event::DestroyDebugMessenger, Event::DestroyInstance, Event::DestroyNative, Event::CloseLoader });
    ensure("transferred resources are each released once",
           source_state.mDestroySurfaceCount == 1 && source_state.mDestroyDebugCount == 1 && source_state.mDestroyInstanceCount == 1 &&
               !source_state.mLoaderLive && !source_state.mNativeLive);
}

template<>
template<>
void window_macosx_vulkan_object::test<12>()
{
    using namespace LLRenderVulkan;

    FakeState   state;
    ScopedState active(state);
    auto        result = acquireLLWindowMacOSXVulkan(createInfo(), 81, fakeOperations(state));
    auto*       owner  = acquiredWindow(result);
    ensure("instance failure fixture acquired native state", owner != nullptr);

    state.mInstanceResult = VK_ERROR_INITIALIZATION_FAILED;
    state.mNullInstance   = true;
    const auto failure = owner->acquireInstanceGeneration(VulkanInstanceValidationMode::Disabled, VulkanInstancePortabilityMode::Disabled);
    ensure("failed instance creation returns its VkResult without publishing a parent",
           failure && failure->mCode == VulkanInstanceAcquireCode::InstanceCreationFailure &&
               failure->mResult == VK_ERROR_INITIALIZATION_FAILED && !owner->instanceGeneration());

    state.mInstanceResult = VK_SUCCESS;
    state.mNullInstance   = false;
    ensure("instance acquisition can retry after failure",
           !owner->acquireInstanceGeneration(VulkanInstanceValidationMode::Disabled, VulkanInstancePortabilityMode::Disabled));
    state.mOwnerDuringDestroy = owner;
    ensure("instance retry fixture teardown succeeds", owner->reset());
    ensure_equals("only the earned instance is destroyed", state.mDestroyInstanceCount, std::size_t{ 1 });
    ensure_equals("instance retry never creates a surface", state.mCreateSurfaceCount, std::size_t{ 0 });
}

template<>
template<>
void window_macosx_vulkan_object::test<13>()
{
    using namespace LLRenderVulkan;

    FakeState   state;
    ScopedState active(state);
    const auto  info   = createInfo();
    auto        result = acquireLLWindowMacOSXVulkan(info, 131, fakeOperations(state));
    auto*       owner  = acquiredWindow(result);
    ensure("swapchain-adapter fixture acquired a native owner", owner != nullptr);

    const auto missing_instance = owner->acquireSwapchainConfigurationGeneration();
    ensure("swapchain configuration requires a live instance before refreshing geometry",
           missing_instance && missing_instance->mCode == VulkanSwapchainConfigurationAcquireCode::InstanceNotLive &&
               state.mRefreshCount == 0);

    ensure("swapchain-adapter fixture acquired an instance",
           !owner->acquireInstanceGeneration(VulkanInstanceValidationMode::Disabled, VulkanInstancePortabilityMode::Disabled));
    const std::size_t refreshes_after_instance = state.mRefreshCount;

    state.mRefreshSucceeds    = false;
    const auto failed_refresh = owner->acquireSwapchainConfigurationGeneration();
    ensure("a failed Cocoa geometry refresh maps to a stale window",
           failed_refresh && failed_refresh->mCode == VulkanSwapchainConfigurationAcquireCode::StaleWindowGeneration &&
               state.mRefreshCount == refreshes_after_instance + 1);

    state.mRefreshSucceeds     = true;
    state.mRefreshMutation     = RefreshMutation::ZeroHeight;
    const auto invalid_refresh = owner->acquireSwapchainConfigurationGeneration();
    ensure("an invalid refreshed backing height maps to a stale window",
           invalid_refresh && invalid_refresh->mCode == VulkanSwapchainConfigurationAcquireCode::StaleWindowGeneration &&
               state.mRefreshCount == refreshes_after_instance + 2);

    state.mRefreshMutation     = RefreshMutation::None;
    state.mRefreshScale        = 1.25;
    state.mRefreshWidth        = 1600;
    state.mRefreshHeight       = 1000;
    const auto missing_surface = owner->acquireSwapchainConfigurationGeneration();
    ensure("valid refreshed Cocoa backing pixels are forwarded to the live instance parent",
           missing_surface && missing_surface->mCode == VulkanSwapchainConfigurationAcquireCode::SurfaceNotLive &&
               state.mRefreshCount == refreshes_after_instance + 3 && owner->drawableWidth() == 1600 && owner->drawableHeight() == 1000);

    ensure("swapchain-adapter fixture acquired a surface", !owner->acquireSurfaceGeneration());
    const std::size_t refreshes_after_surface = state.mRefreshCount;
    const auto        missing_selection       = owner->acquireSwapchainConfigurationGeneration();
    ensure("the Cocoa adapter forwards refreshed pixels through the exact surface parent",
           missing_selection && missing_selection->mCode == VulkanSwapchainConfigurationAcquireCode::PresentationDeviceNotLive &&
               state.mRefreshCount == refreshes_after_surface + 1 && owner->drawableWidth() == 1600 && owner->drawableHeight() == 1000);

    state.mOwnerDuringDestroy = owner;
    ensure("swapchain-adapter fixture teardown succeeds", owner->reset());
    ensure_equals("adapter checks preserve one surface destruction", state.mDestroySurfaceCount, std::size_t{ 1 });
    ensure_equals("adapter checks preserve one instance destruction", state.mDestroyInstanceCount, std::size_t{ 1 });
}

template<>
template<>
void window_macosx_vulkan_object::test<14>()
{
    using namespace LLRenderVulkan;

    FakeState   state;
    ScopedState active(state);
    const auto  info   = createInfo();
    auto        result = acquireLLWindowMacOSXVulkan(info, 141, fakeOperations(state));
    auto*       owner  = acquiredWindow(result);
    ensure("swapchain-owner fixture acquired a native owner", owner != nullptr);

    const auto missing_instance = owner->acquireSwapchainGeneration();
    ensure("swapchain acquisition requires a live instance before refreshing geometry",
           missing_instance && missing_instance->mCode == VulkanSwapchainAcquireCode::InstanceNotLive && state.mRefreshCount == 0);

    ensure("swapchain-owner fixture acquired an instance",
           !owner->acquireInstanceGeneration(VulkanInstanceValidationMode::Disabled, VulkanInstancePortabilityMode::Disabled));
    const std::size_t refreshes_after_instance = state.mRefreshCount;

    state.mRefreshSucceeds    = false;
    const auto failed_refresh = owner->acquireSwapchainGeneration();
    ensure("a failed Cocoa geometry refresh maps to a stale swapchain window",
           failed_refresh && failed_refresh->mCode == VulkanSwapchainAcquireCode::StaleWindowGeneration &&
               state.mRefreshCount == refreshes_after_instance + 1);

    state.mRefreshSucceeds     = true;
    state.mRefreshMutation     = RefreshMutation::ZeroWidth;
    const auto invalid_refresh = owner->acquireSwapchainGeneration();
    ensure("an invalid refreshed backing width maps to a stale swapchain window",
           invalid_refresh && invalid_refresh->mCode == VulkanSwapchainAcquireCode::StaleWindowGeneration &&
               state.mRefreshCount == refreshes_after_instance + 2);

    state.mRefreshMutation     = RefreshMutation::None;
    state.mRefreshScale        = 1.5;
    state.mRefreshWidth        = 1920;
    state.mRefreshHeight       = 1080;
    const auto missing_surface = owner->acquireSwapchainGeneration();
    ensure("current Cocoa backing pixels are forwarded to the swapchain parent",
           missing_surface && missing_surface->mCode == VulkanSwapchainAcquireCode::SurfaceNotLive &&
               state.mRefreshCount == refreshes_after_instance + 3 && owner->drawableWidth() == 1920 && owner->drawableHeight() == 1080);

    ensure("swapchain-owner fixture acquired a surface", !owner->acquireSurfaceGeneration());
    const std::size_t refreshes_after_surface = state.mRefreshCount;
    const auto        missing_selection       = owner->acquireSwapchainGeneration();
    ensure("the swapchain adapter refreshes pixels through the exact surface parent",
           missing_selection && missing_selection->mCode == VulkanSwapchainAcquireCode::PresentationDeviceNotLive &&
               state.mRefreshCount == refreshes_after_surface + 1 && owner->drawableWidth() == 1920 && owner->drawableHeight() == 1080);
    ensure("an unowned swapchain reports no explicit reset", !owner->resetSwapchainGeneration());

    state.mOwnerDuringDestroy = owner;
    ensure("swapchain-owner fixture teardown succeeds", owner->reset());
    ensure_equals("swapchain adapter checks preserve one surface destruction", state.mDestroySurfaceCount, std::size_t{ 1 });
    ensure_equals("swapchain adapter checks preserve one instance destruction", state.mDestroyInstanceCount, std::size_t{ 1 });
}

template<>
template<>
void window_macosx_vulkan_object::test<15>()
{
    using namespace LLRenderVulkan;

    FakeState   state;
    ScopedState active(state);
    const auto  info   = createInfo();
    auto        result = acquireLLWindowMacOSXVulkan(info, 151, fakeOperations(state));
    auto*       owner  = acquiredWindow(result);
    ensure("swapchain-image adapter fixture acquired a native owner", owner != nullptr);

    const auto missing_instance = owner->acquireSwapchainImagesGeneration();
    ensure("swapchain-image acquisition requires a live instance before refreshing geometry",
           missing_instance && missing_instance->mCode == VulkanSwapchainImagesAcquireCode::InstanceNotLive && state.mRefreshCount == 0);

    ensure("swapchain-image adapter fixture acquired an instance",
           !owner->acquireInstanceGeneration(VulkanInstanceValidationMode::Disabled, VulkanInstancePortabilityMode::Disabled));
    const std::size_t refreshes_after_instance = state.mRefreshCount;

    state.mRefreshSucceeds    = false;
    const auto failed_refresh = owner->acquireSwapchainImagesGeneration();
    ensure("a failed Cocoa geometry refresh maps to a stale swapchain-image window",
           failed_refresh && failed_refresh->mCode == VulkanSwapchainImagesAcquireCode::StaleWindowGeneration &&
               state.mRefreshCount == refreshes_after_instance + 1);

    state.mRefreshSucceeds     = true;
    state.mRefreshMutation     = RefreshMutation::ZeroHeight;
    const auto invalid_refresh = owner->acquireSwapchainImagesGeneration();
    ensure("an invalid refreshed backing height maps to a stale swapchain-image window",
           invalid_refresh && invalid_refresh->mCode == VulkanSwapchainImagesAcquireCode::StaleWindowGeneration &&
               state.mRefreshCount == refreshes_after_instance + 2);

    state.mRefreshMutation     = RefreshMutation::None;
    state.mRefreshScale        = 2.0;
    state.mRefreshWidth        = 2560;
    state.mRefreshHeight       = 1440;
    const auto missing_surface = owner->acquireSwapchainImagesGeneration();
    ensure("current Cocoa backing pixels are forwarded to the swapchain-image parent",
           missing_surface && missing_surface->mCode == VulkanSwapchainImagesAcquireCode::SurfaceNotLive &&
               state.mRefreshCount == refreshes_after_instance + 3 && owner->drawableWidth() == 2560 && owner->drawableHeight() == 1440);

    ensure("swapchain-image adapter fixture acquired a surface", !owner->acquireSurfaceGeneration());
    const std::size_t refreshes_after_surface = state.mRefreshCount;
    const auto        missing_selection       = owner->acquireSwapchainImagesGeneration();
    ensure("the swapchain-image adapter refreshes pixels through the exact surface parent",
           missing_selection && missing_selection->mCode == VulkanSwapchainImagesAcquireCode::PresentationDeviceNotLive &&
               state.mRefreshCount == refreshes_after_surface + 1 && owner->drawableWidth() == 2560 && owner->drawableHeight() == 1440);
    ensure("an unowned swapchain-image generation reports no explicit reset", !owner->resetSwapchainImagesGeneration());

    state.mOwnerDuringDestroy = owner;
    ensure("swapchain-image adapter fixture teardown succeeds", owner->reset());
    ensure_equals("swapchain-image adapter checks preserve one surface destruction", state.mDestroySurfaceCount, std::size_t{ 1 });
    ensure_equals("swapchain-image adapter checks preserve one instance destruction", state.mDestroyInstanceCount, std::size_t{ 1 });
}

template<>
template<>
void window_macosx_vulkan_object::test<16>()
{
    using namespace LLRenderVulkan;

    FakeState   state;
    ScopedState active(state);
    const auto  info   = createInfo();
    auto        result = acquireLLWindowMacOSXVulkan(info, 161, fakeOperations(state));
    auto*       owner  = acquiredWindow(result);
    ensure("frame-slot adapter fixture acquired a native owner", owner != nullptr);

    const auto missing_instance = owner->acquireSwapchainFrameSlotGeneration();
    ensure("frame-slot acquisition requires a live instance before refreshing geometry",
           missing_instance && missing_instance->mCode == VulkanSwapchainFrameSlotAcquireCode::InstanceNotLive && state.mRefreshCount == 0);
    const auto  missing_operation_result = owner->roundTripEmptySwapchainFrameSlot();
    const auto* missing_operation        = operationError(missing_operation_result);
    ensure("an empty round trip requires a live instance before refreshing geometry",
           missing_operation && missing_operation->mCode == VulkanSwapchainFrameSlotParentOperationCode::InstanceNotLive &&
               state.mRefreshCount == 0);

    ensure("frame-slot adapter fixture acquired an instance",
           !owner->acquireInstanceGeneration(VulkanInstanceValidationMode::Disabled, VulkanInstancePortabilityMode::Disabled));
    const std::size_t refreshes_after_instance = state.mRefreshCount;

    state.mRefreshSucceeds    = false;
    const auto failed_refresh = owner->acquireSwapchainFrameSlotGeneration();
    ensure("a failed Cocoa geometry refresh maps to a stale frame-slot window",
           failed_refresh && failed_refresh->mCode == VulkanSwapchainFrameSlotAcquireCode::StaleWindowGeneration &&
               state.mRefreshCount == refreshes_after_instance + 1);
    const auto  failed_operation_result = owner->roundTripEmptySwapchainFrameSlot();
    const auto* failed_operation        = operationError(failed_operation_result);
    ensure("a failed Cocoa geometry refresh maps an empty round trip to a stale window",
           failed_operation && failed_operation->mCode == VulkanSwapchainFrameSlotParentOperationCode::StaleWindowGeneration &&
               state.mRefreshCount == refreshes_after_instance + 2);

    state.mRefreshSucceeds     = true;
    state.mRefreshMutation     = RefreshMutation::ZeroWidth;
    const auto invalid_refresh = owner->acquireSwapchainFrameSlotGeneration();
    ensure("an invalid refreshed backing width maps to a stale frame-slot window",
           invalid_refresh && invalid_refresh->mCode == VulkanSwapchainFrameSlotAcquireCode::StaleWindowGeneration &&
               state.mRefreshCount == refreshes_after_instance + 3);
    const auto  invalid_operation_result = owner->roundTripEmptySwapchainFrameSlot();
    const auto* invalid_operation        = operationError(invalid_operation_result);
    ensure("an invalid refreshed backing width is a typed empty-round-trip extent failure",
           invalid_operation && invalid_operation->mCode == VulkanSwapchainFrameSlotParentOperationCode::InvalidDrawableExtent &&
               state.mRefreshCount == refreshes_after_instance + 4);

    state.mRefreshMutation     = RefreshMutation::None;
    state.mRefreshScale        = 2.0;
    state.mRefreshWidth        = 3840;
    state.mRefreshHeight       = 2160;
    const auto missing_surface = owner->acquireSwapchainFrameSlotGeneration();
    ensure("current Cocoa backing pixels are forwarded to the frame-slot parent",
           missing_surface && missing_surface->mCode == VulkanSwapchainFrameSlotAcquireCode::SurfaceNotLive &&
               state.mRefreshCount == refreshes_after_instance + 5 && owner->drawableWidth() == 3840 && owner->drawableHeight() == 2160);
    const auto  missing_surface_operation_result = owner->roundTripEmptySwapchainFrameSlot();
    const auto* missing_surface_operation        = operationError(missing_surface_operation_result);
    ensure("current Cocoa backing pixels are forwarded by the empty-round-trip adapter",
           missing_surface_operation && missing_surface_operation->mCode == VulkanSwapchainFrameSlotParentOperationCode::SurfaceNotLive &&
               state.mRefreshCount == refreshes_after_instance + 6 && owner->drawableWidth() == 3840 && owner->drawableHeight() == 2160);

    ensure("frame-slot adapter fixture acquired a surface", !owner->acquireSurfaceGeneration());
    const std::size_t refreshes_after_surface = state.mRefreshCount;
    const auto        missing_selection       = owner->acquireSwapchainFrameSlotGeneration();
    ensure("the frame-slot adapter refreshes pixels through the exact surface parent",
           missing_selection && missing_selection->mCode == VulkanSwapchainFrameSlotAcquireCode::PresentationDeviceNotLive &&
               state.mRefreshCount == refreshes_after_surface + 1 && owner->drawableWidth() == 3840 && owner->drawableHeight() == 2160);
    const auto  missing_selection_operation_result = owner->roundTripEmptySwapchainFrameSlot();
    const auto* missing_selection_operation        = operationError(missing_selection_operation_result);
    ensure("the empty-round-trip adapter forwards the exact live surface parent",
           missing_selection_operation &&
               missing_selection_operation->mCode == VulkanSwapchainFrameSlotParentOperationCode::PresentationDeviceNotLive &&
               state.mRefreshCount == refreshes_after_surface + 2);
    const auto  missing_selection_retry_result = owner->retryEmptySwapchainFrameSlotCompletion();
    const auto* missing_selection_retry        = operationError(missing_selection_retry_result);
    ensure("completion retry does not refresh geometry when no configuration has retained an extent",
           missing_selection_retry &&
               missing_selection_retry->mCode == VulkanSwapchainFrameSlotParentOperationCode::PresentationDeviceNotLive &&
               state.mRefreshCount == refreshes_after_surface + 2);
    const auto  presentation_retry_result = owner->retrySwapchainFrameSlotPresentation();
    const auto* presentation_retry        = std::get_if<VulkanSwapchainFrameSlotParentOperationError>(&presentation_retry_result);
    const auto  presentation_completion_retry_result = owner->retrySwapchainFrameSlotPresentationCompletion();
    const auto* presentation_completion_retry =
        std::get_if<VulkanSwapchainFrameSlotParentOperationError>(&presentation_completion_retry_result);
    const auto  cancellation_result       = owner->cancelSwapchainFrameSlotPresentation();
    const auto* cancellation              = operationError(cancellation_result);
    const auto  cancellation_retry_result = owner->retrySwapchainFrameSlotCancellationCompletion();
    const auto* cancellation_retry        = operationError(cancellation_retry_result);
    ensure("presentation and cancellation retries use retained geometry without refreshing Cocoa state",
           presentation_retry && presentation_retry->mCode == VulkanSwapchainFrameSlotParentOperationCode::PresentationDeviceNotLive &&
               presentation_completion_retry &&
               presentation_completion_retry->mCode == VulkanSwapchainFrameSlotParentOperationCode::PresentationDeviceNotLive &&
               cancellation && cancellation->mCode == VulkanSwapchainFrameSlotParentOperationCode::PresentationDeviceNotLive &&
               cancellation_retry && cancellation_retry->mCode == VulkanSwapchainFrameSlotParentOperationCode::PresentationDeviceNotLive &&
               state.mRefreshCount == refreshes_after_surface + 2);
    ensure("an unowned frame-slot generation reports no explicit reset", !owner->resetSwapchainFrameSlotGeneration());

    state.mOwnerDuringDestroy = owner;
    ensure("frame-slot adapter fixture teardown succeeds", owner->reset());
    ensure_equals("frame-slot adapter checks preserve one surface destruction", state.mDestroySurfaceCount, std::size_t{ 1 });
    ensure_equals("frame-slot adapter checks preserve one instance destruction", state.mDestroyInstanceCount, std::size_t{ 1 });
}

template<>
template<>
void window_macosx_vulkan_object::test<17>()
{
    using namespace LLRenderVulkan;

    FakeState   state;
    ScopedState active(state);
    auto        result = acquireLLWindowMacOSXVulkan(createInfo(), 171, fakeOperations(state));
    auto*       owner  = acquiredWindow(result);
    ensure("swapchain-rebuild adapter fixture acquired a native owner", owner != nullptr);

    const auto  missing_instance_result = owner->rebuildSwapchainChain();
    const auto* missing_instance        = rebuildError(missing_instance_result);
    ensure("swapchain rebuild requires a live instance before observing Cocoa geometry",
           missing_instance && missing_instance->mCode == VulkanSwapchainChainRebuildCode::InstanceNotLive &&
               missing_instance->mPhase == VulkanSwapchainChainRebuildPhase::Preflight && state.mRefreshCount == 0);

    ensure("swapchain-rebuild adapter fixture acquired an instance",
           !owner->acquireInstanceGeneration(VulkanInstanceValidationMode::Disabled, VulkanInstancePortabilityMode::Disabled));
    const std::size_t refreshes_after_instance = state.mRefreshCount;

    state.mRefreshSucceeds        = false;
    const auto  failed_result     = owner->rebuildSwapchainChain();
    const auto* failed            = rebuildError(failed_result);
    ensure("a failed Cocoa observation is a stale rebuild request",
           failed && failed->mCode == VulkanSwapchainChainRebuildCode::StaleWindowGeneration &&
               failed->mPhase == VulkanSwapchainChainRebuildPhase::Preflight &&
               state.mRefreshCount == refreshes_after_instance + 1 && owner->drawableWidth() == 1280 &&
               owner->drawableHeight() == 720);

    state.mRefreshSucceeds = true;
    state.mRefreshMutation = RefreshMutation::View;
    const auto  stale_identity_result = owner->rebuildSwapchainChain();
    const auto* stale_identity        = rebuildError(stale_identity_result);
    ensure("a changed private Cocoa identity is rejected before rebuilding an older parent",
           stale_identity && stale_identity->mCode == VulkanSwapchainChainRebuildCode::StaleWindowGeneration &&
               stale_identity->mPhase == VulkanSwapchainChainRebuildPhase::Preflight &&
               state.mRefreshCount == refreshes_after_instance + 2 && owner->drawableWidth() == 1280 &&
               owner->drawableHeight() == 720);

    state.mRefreshMutation = RefreshMutation::None;
    state.mMainThread      = false;
    const auto  off_main_result = owner->rebuildSwapchainChain();
    const auto* off_main        = rebuildError(off_main_result);
    ensure("an off-main rebuild is rejected without observing or retiring native state",
           off_main && off_main->mCode == VulkanSwapchainChainRebuildCode::StaleWindowGeneration &&
               off_main->mPhase == VulkanSwapchainChainRebuildPhase::Preflight &&
               state.mRefreshCount == refreshes_after_instance + 2 && owner->drawableWidth() == 1280 &&
               owner->drawableHeight() == 720);

    state.mMainThread    = true;
    state.mRefreshWidth  = 0;
    state.mRefreshHeight = 0;
    const auto  zero_result = owner->rebuildSwapchainChain();
    const auto* zero        = rebuildError(zero_result);
    ensure("zero Cocoa pixels are observed as available suspension geometry rather than stale identity",
           zero && zero->mCode == VulkanSwapchainChainRebuildCode::SurfaceNotLive &&
               zero->mPhase == VulkanSwapchainChainRebuildPhase::Preflight &&
               state.mRefreshCount == refreshes_after_instance + 3 && owner->drawableWidth() == 0 && owner->drawableHeight() == 0);

    state.mRefreshScale  = 1.5;
    state.mRefreshWidth  = 1536;
    state.mRefreshHeight = 864;
    const auto  restore_result = owner->rebuildSwapchainChain();
    const auto* restore        = rebuildError(restore_result);
    ensure("restored positive Cocoa pixels are forwarded through the exact live instance parent",
           restore && restore->mCode == VulkanSwapchainChainRebuildCode::SurfaceNotLive &&
               restore->mPhase == VulkanSwapchainChainRebuildPhase::Preflight &&
               state.mRefreshCount == refreshes_after_instance + 4 && owner->backingScale() == 1.5 &&
               owner->drawableWidth() == 1536 && owner->drawableHeight() == 864);

    ensure("the explicit diagnostic resize rejects zero dimensions without native mutation",
           !owner->resizeNativeDrawableForDiagnostic(0, 900) && state.mResizeCount == 0 && owner->drawableWidth() == 1536 &&
               owner->drawableHeight() == 864);
    ensure("the explicit diagnostic resize accepts one exact positive backing extent",
           owner->resizeNativeDrawableForDiagnostic(1600, 900) && state.mResizeCount == 1 && state.mResizeWidth == 1600 &&
               state.mResizeHeight == 900 && owner->drawableWidth() == 1600 && owner->drawableHeight() == 900);

    state.mResizeMutation = RefreshMutation::Layer;
    ensure("the diagnostic seam rejects changed Metal-layer identity",
           !owner->resizeNativeDrawableForDiagnostic(1920, 1080) && state.mResizeCount == 2 && owner->drawableWidth() == 1600 &&
               owner->drawableHeight() == 900);
    state.mResizeMutation = RefreshMutation::None;
    state.mMainThread     = false;
    ensure("the diagnostic resize is main-thread-only and performs no off-main callback",
           !owner->resizeNativeDrawableForDiagnostic(1920, 1080) && state.mResizeCount == 2);

    state.mMainThread         = true;
    state.mOwnerDuringDestroy = owner;
    ensure("swapchain-rebuild adapter fixture teardown succeeds", owner->reset());
    ensure_equals("rebuild adapter checks preserve one instance destruction", state.mDestroyInstanceCount, std::size_t{ 1 });

    FakeState full_state;
    active.use(full_state);
    auto  full_result = acquireLLWindowMacOSXVulkan(createInfo(), 172, fakeOperations(full_state));
    auto* full_owner  = acquiredWindow(full_result);
    ensure("suspend-and-restore fixture acquired a native owner", full_owner != nullptr);
    ensure("suspend-and-restore fixture acquired a complete authenticated swapchain chain",
           acquireCompleteSwapchainChain(*full_owner));

    const VulkanInstanceGeneration* full_generation = full_owner->instanceGeneration();
    ensure("the complete fixture publishes every required generation",
           full_generation && full_generation->hasSurfaceGeneration() && full_generation->hasPresentationDeviceGeneration() &&
               full_generation->hasLogicalDeviceGeneration() && full_generation->hasSwapchainConfigurationGeneration() &&
               full_generation->hasSwapchainGeneration() && full_generation->hasSwapchainImagesGeneration() &&
               full_generation->hasSwapchainPresentationTargetGeneration() &&
               full_generation->hasSwapchainPresentationPipelineGeneration() &&
               full_generation->hasSwapchainFrameSlotGeneration());
    ensure("the complete fixture publishes one render pass and one framebuffer per swapchain image",
           full_generation->swapchainPresentationRenderPass() != VK_NULL_HANDLE &&
               full_generation->swapchainPresentationFramebufferCount() == full_state.mImages.size() &&
               full_generation->swapchainPresentationFramebuffer(0) != VK_NULL_HANDLE &&
               full_generation->swapchainPresentationFramebuffer(1) != VK_NULL_HANDLE &&
               full_generation->swapchainPresentationFramebuffer(2) != VK_NULL_HANDLE &&
               full_generation->swapchainPresentationFramebuffer(static_cast<std::uint32_t>(full_state.mImages.size())) ==
                   VK_NULL_HANDLE &&
               full_state.mCreateRenderPassCount == 1 && full_state.mCreateFramebufferCount == full_state.mImages.size());
    const VkSurfaceKHR    retained_surface         = full_generation->surface();
    const VkPhysicalDevice retained_physical_device = full_generation->physicalDevice();
    const VkDevice         retained_device          = full_generation->logicalDevice();
    const VkQueue          retained_queue           = full_generation->presentationQueue();
    ensure("the initial diagnostic swapchain uses a null old-swapchain handoff",
           full_state.mCreateSwapchainCount == 1 && full_state.mLastOldSwapchain == VK_NULL_HANDLE);

    full_state.mRefreshWidth  = 0;
    full_state.mRefreshHeight = 720;
    const auto  suspended_result  = full_owner->rebuildSwapchainChain();
    const auto* suspended_outcome = std::get_if<VulkanSwapchainChainRebuildOutcome>(&suspended_result);
    ensure("zero backing width produces the typed suspended outcome",
           suspended_outcome && *suspended_outcome == VulkanSwapchainChainRebuildOutcome::Suspended &&
               full_owner->drawableWidth() == 0 && full_owner->drawableHeight() == 720);
    ensure("suspension removes the configuration, swapchain, images, presentation target, pipeline, and frame slot",
           !full_generation->hasSwapchainConfigurationGeneration() && !full_generation->hasSwapchainGeneration() &&
               !full_generation->hasSwapchainImagesGeneration() &&
               !full_generation->hasSwapchainPresentationTargetGeneration() &&
               !full_generation->hasSwapchainPresentationPipelineGeneration() &&
               !full_generation->hasSwapchainFrameSlotGeneration() && full_generation->swapchain() == VK_NULL_HANDLE &&
               full_generation->resolvedSwapchainImageCount() == 0 &&
               full_generation->swapchainPresentationRenderPass() == VK_NULL_HANDLE &&
               full_generation->swapchainPresentationFramebufferCount() == 0 &&
               full_generation->swapchainPresentationFramebuffer(0) == VK_NULL_HANDLE &&
               full_generation->swapchainPresentationPipelineLayout() == VK_NULL_HANDLE &&
               full_generation->swapchainPresentationPipeline() == VK_NULL_HANDLE &&
               full_generation->swapchainFrameCommandPool() == VK_NULL_HANDLE);
    ensure("suspension retains the exact surface, selection, logical device, and queue parents",
           full_generation->surface() == retained_surface && full_generation->physicalDevice() == retained_physical_device &&
               full_generation->logicalDevice() == retained_device && full_generation->presentationQueue() == retained_queue);
    ensure("suspension destroys each youngest owned resource exactly once without creating a replacement",
               full_state.mCreateSwapchainCount == 1 && full_state.mDestroySwapchainCount == 1 &&
               full_state.mCreateImageViewCount == full_state.mImages.size() &&
               full_state.mDestroyImageViewCount == full_state.mImages.size() && full_state.mCreateRenderPassCount == 1 &&
               full_state.mDestroyRenderPassCount == 1 && full_state.mCreateFramebufferCount == full_state.mImages.size() &&
               full_state.mDestroyFramebufferCount == full_state.mImages.size() &&
               full_state.mCreateShaderModuleCount == 2 && full_state.mDestroyShaderModuleCount == 2 &&
               full_state.mCreatePipelineLayoutCount == 1 && full_state.mDestroyPipelineLayoutCount == 1 &&
               full_state.mCreatePipelineCount == 1 && full_state.mDestroyPipelineCount == 1 &&
               full_state.mCreateCommandPoolCount == 1 &&
               full_state.mDestroyCommandPoolCount == 1 && full_state.mCreateSemaphoreCount == 2 &&
               full_state.mDestroySemaphoreCount == 2 && full_state.mCreateFenceCount == 2 && full_state.mDestroyFenceCount == 2);

    const auto  repeated_suspend_result  = full_owner->rebuildSwapchainChain();
    const auto* repeated_suspend_outcome = std::get_if<VulkanSwapchainChainRebuildOutcome>(&repeated_suspend_result);
    ensure("a repeated zero sample remains stably suspended without extra destruction",
           repeated_suspend_outcome && *repeated_suspend_outcome == VulkanSwapchainChainRebuildOutcome::Suspended &&
               full_state.mCreateSwapchainCount == 1 && full_state.mDestroySwapchainCount == 1 &&
               full_state.mDestroyImageViewCount == full_state.mImages.size() && full_state.mDestroyRenderPassCount == 1 &&
               full_state.mDestroyFramebufferCount == full_state.mImages.size() && full_state.mDestroyPipelineCount == 1 &&
               full_state.mDestroyPipelineLayoutCount == 1 && full_state.mDestroyCommandPoolCount == 1);

    full_state.mRefreshScale  = 1.5;
    full_state.mRefreshWidth  = 1600;
    full_state.mRefreshHeight = 900;
    const auto  restored_result  = full_owner->rebuildSwapchainChain();
    const auto* restored_outcome = std::get_if<VulkanSwapchainChainRebuildOutcome>(&restored_result);
    ensure("a positive restored extent rebuilds the complete youngest chain",
           restored_outcome && *restored_outcome == VulkanSwapchainChainRebuildOutcome::Ready &&
               full_generation->hasSwapchainConfigurationGeneration() && full_generation->hasSwapchainGeneration() &&
               full_generation->hasSwapchainImagesGeneration() &&
               full_generation->hasSwapchainPresentationTargetGeneration() &&
               full_generation->hasSwapchainPresentationPipelineGeneration() &&
               full_generation->hasSwapchainFrameSlotGeneration() &&
               full_owner->backingScale() == 1.5 && full_owner->drawableWidth() == 1600 && full_owner->drawableHeight() == 900);
    const VkExtent2D restored_extent = full_generation->swapchainDrawableExtent();
    ensure("restoration authenticates one exact extent and still retains every older parent",
           restored_extent.width == 1600 && restored_extent.height == 900 &&
               full_generation->surface() == retained_surface && full_generation->physicalDevice() == retained_physical_device &&
               full_generation->logicalDevice() == retained_device && full_generation->presentationQueue() == retained_queue);
    ensure("restoration creates an initial-form swapchain and one complete fresh child set",
               full_state.mCreateSwapchainCount == 2 && full_state.mLastOldSwapchain == VK_NULL_HANDLE &&
               full_state.mLastSwapchainExtent.width == 1600 && full_state.mLastSwapchainExtent.height == 900 &&
               full_state.mCreateImageViewCount == full_state.mImages.size() * 2 &&
               full_state.mCreateRenderPassCount == 2 && full_state.mDestroyRenderPassCount == 1 &&
               full_state.mCreateFramebufferCount == full_state.mImages.size() * 2 &&
               full_state.mDestroyFramebufferCount == full_state.mImages.size() &&
               full_generation->swapchainPresentationRenderPass() != VK_NULL_HANDLE &&
               full_generation->swapchainPresentationFramebufferCount() == full_state.mImages.size() &&
               full_generation->swapchainPresentationFramebuffer(0) != VK_NULL_HANDLE &&
               full_generation->swapchainPresentationFramebuffer(1) != VK_NULL_HANDLE &&
               full_generation->swapchainPresentationFramebuffer(2) != VK_NULL_HANDLE &&
               full_generation->swapchainPresentationPipelineLayout() != VK_NULL_HANDLE &&
               full_generation->swapchainPresentationPipeline() != VK_NULL_HANDLE &&
               full_state.mCreateShaderModuleCount == 4 && full_state.mDestroyShaderModuleCount == 4 &&
               full_state.mCreatePipelineLayoutCount == 2 && full_state.mDestroyPipelineLayoutCount == 1 &&
               full_state.mCreatePipelineCount == 2 && full_state.mDestroyPipelineCount == 1 &&
               full_state.mCreateCommandPoolCount == 2 && full_state.mCreateSemaphoreCount == 4 &&
               full_state.mCreateFenceCount == 4);

    full_state.mOwnerDuringDestroy = full_owner;
    ensure("suspend-and-restore fixture teardown succeeds", full_owner->reset());
    ensure("restored teardown releases the second youngest child set exactly once",
           full_state.mDestroySwapchainCount == 2 && full_state.mDestroyImageViewCount == full_state.mImages.size() * 2 &&
               full_state.mDestroyRenderPassCount == 2 &&
               full_state.mDestroyFramebufferCount == full_state.mImages.size() * 2 &&
               full_state.mDestroyPipelineCount == 2 && full_state.mDestroyPipelineLayoutCount == 2 &&
               full_state.mDestroyCommandPoolCount == 2 && full_state.mDestroySemaphoreCount == 4 &&
               full_state.mDestroyFenceCount == 4);
}

template<>
template<>
void window_macosx_vulkan_object::test<18>()
{
    using namespace LLRenderVulkan;

    constexpr VulkanSwapchainFrameClearColor clear_color{ { 0.125f, 0.25f, 0.5f, 1.0f } };

    FakeState   state;
    ScopedState active(state);
    auto        result = acquireLLWindowMacOSXVulkan(createInfo(), 181, fakeOperations(state));
    auto*       owner  = acquiredWindow(result);
    ensure("clear-present adapter fixture acquired a native owner", owner != nullptr);

    const auto  missing_instance_result = owner->acquireClearToPresentSwapchainFrameSlot(clear_color);
    const auto* missing_instance        = presentationError(missing_instance_result);
    ensure("clear-present requires a live instance before observing Cocoa geometry",
           missing_instance && missing_instance->mCode == VulkanSwapchainFrameSlotParentOperationCode::InstanceNotLive &&
               state.mRefreshCount == 0 && state.mClearColorImageCount == 0);

    ensure("clear-present adapter fixture acquired a complete authenticated swapchain chain", acquireCompleteSwapchainChain(*owner));
    ensure("the clear-present fixture creates a transfer-destination-capable swapchain",
           state.mLastSwapchainUsage ==
               (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT));
    const std::size_t refreshes_after_chain = state.mRefreshCount;

    state.mMainThread           = false;
    const auto  off_main_result = owner->acquireClearToPresentSwapchainFrameSlot(clear_color);
    const auto* off_main        = presentationError(off_main_result);
    ensure("off-main clear-present is rejected without observing or dispatching native work",
           off_main && off_main->mCode == VulkanSwapchainFrameSlotParentOperationCode::StaleWindowGeneration &&
               state.mRefreshCount == refreshes_after_chain && state.mClearColorImageCount == 0);

    state.mMainThread      = true;
    state.mRefreshSucceeds = false;
    const auto  failed_refresh_result = owner->acquireClearToPresentSwapchainFrameSlot(clear_color);
    const auto* failed_refresh        = presentationError(failed_refresh_result);
    ensure("a failed Cocoa refresh is a stale clear-present request",
           failed_refresh && failed_refresh->mCode == VulkanSwapchainFrameSlotParentOperationCode::StaleWindowGeneration &&
               state.mRefreshCount == refreshes_after_chain + 1 && state.mClearColorImageCount == 0);

    state.mRefreshSucceeds = true;
    state.mRefreshMutation = RefreshMutation::Layer;
    const auto  stale_identity_result = owner->acquireClearToPresentSwapchainFrameSlot(clear_color);
    const auto* stale_identity        = presentationError(stale_identity_result);
    ensure("changed Metal-layer identity is rejected before clear-present reaches the parent",
           stale_identity && stale_identity->mCode == VulkanSwapchainFrameSlotParentOperationCode::StaleWindowGeneration &&
               state.mRefreshCount == refreshes_after_chain + 2 && state.mClearColorImageCount == 0);

    state.mRefreshMutation = RefreshMutation::None;
    state.mRefreshWidth    = 0;
    const auto  zero_extent_result = owner->acquireClearToPresentSwapchainFrameSlot(clear_color);
    const auto* zero_extent        = presentationError(zero_extent_result);
    ensure("zero Cocoa backing pixels are a typed clear-present extent failure",
           zero_extent && zero_extent->mCode == VulkanSwapchainFrameSlotParentOperationCode::InvalidDrawableExtent &&
               state.mRefreshCount == refreshes_after_chain + 3 && owner->drawableWidth() == 1280 && owner->drawableHeight() == 720 &&
               state.mClearColorImageCount == 0);

    state.mRefreshWidth                  = 1600;
    state.mRefreshHeight                 = 900;
    const auto  mismatched_extent_result = owner->acquireClearToPresentSwapchainFrameSlot(clear_color);
    const auto* mismatched_extent        = presentationError(mismatched_extent_result);
    ensure("clear-present forwards changed positive pixels for exact parent-level extent authentication",
           mismatched_extent && mismatched_extent->mCode == VulkanSwapchainFrameSlotParentOperationCode::DrawableExtentMismatch &&
               state.mRefreshCount == refreshes_after_chain + 4 && owner->drawableWidth() == 1600 && owner->drawableHeight() == 900 &&
               state.mClearColorImageCount == 0);

    state.mRefreshWidth           = 1280;
    state.mRefreshHeight          = 720;
    const auto successful_result = owner->acquireClearToPresentSwapchainFrameSlot(clear_color);
    const auto* success          = std::get_if<VulkanSwapchainFrameSlotPresentationSuccess>(&successful_result);
    ensure("current Cocoa identity and exact backing pixels complete one clear-present cycle",
           success && success->mOutcome == VulkanSwapchainFrameSlotPresentationOutcome::Presented && success->mImageIndex &&
               *success->mImageIndex == state.mAcquiredImageIndex &&
               owner->instanceGeneration()->swapchainFrameSlotDisposition() == VulkanSwapchainFrameSlotDisposition::Reusable &&
               !owner->instanceGeneration()->swapchainFrameAcquiredImageIndex());
    ensure("clear-present records the two transfer transitions and submits and presents once",
           state.mAcquireNextImageCount == 1 && state.mPipelineBarrierCount == 2 && state.mClearColorImageCount == 1 &&
               state.mQueueSubmitCount == 1 && state.mQueuePresentCount == 1 && state.mWaitForFencesCount == 2 &&
               state.mSubmitWaitStage == VK_PIPELINE_STAGE_TRANSFER_BIT && state.mReleaseSwapchainImagesCount == 0);
    ensure("clear-present targets the acquired image in transfer-destination layout with the exact color",
           state.mClearCommandBuffer == state.mCommandBuffer && state.mClearedImage == state.mImages[state.mAcquiredImageIndex] &&
               state.mClearImageLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && state.mClearColor.float32[0] == clear_color.mRgba[0] &&
               state.mClearColor.float32[1] == clear_color.mRgba[1] && state.mClearColor.float32[2] == clear_color.mRgba[2] &&
               state.mClearColor.float32[3] == clear_color.mRgba[3]);
    ensure("clear-present uses one complete color subresource range",
           state.mClearRange.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT && state.mClearRange.baseMipLevel == 0 &&
               state.mClearRange.levelCount == 1 && state.mClearRange.baseArrayLayer == 0 && state.mClearRange.layerCount == 1);
    ensure("successful clear-present refreshes and retains the exact configured backing extent",
           state.mRefreshCount == refreshes_after_chain + 5 && owner->drawableWidth() == 1280 && owner->drawableHeight() == 720);

    state.mOwnerDuringDestroy = owner;
    ensure("clear-present adapter fixture teardown succeeds", owner->reset());
    ensure_equals("clear-present teardown preserves one surface destruction", state.mDestroySurfaceCount, std::size_t{ 1 });
    ensure_equals("clear-present teardown preserves one instance destruction", state.mDestroyInstanceCount, std::size_t{ 1 });
}

template<>
template<>
void window_macosx_vulkan_object::test<19>()
{
    using namespace LLRenderVulkan;

    FakeState   state;
    ScopedState active(state);
    auto        result = acquireLLWindowMacOSXVulkan(createInfo(), 191, fakeOperations(state));
    auto*       owner  = acquiredWindow(result);
    ensure("presentation-target adapter fixture acquires a native owner", owner != nullptr);

    const auto missing_instance = owner->acquireSwapchainPresentationTargetGeneration();
    ensure("presentation-target acquisition requires a live instance before refreshing Cocoa geometry",
           missing_instance && missing_instance->mCode == VulkanSwapchainPresentationTargetAcquireCode::InstanceNotLive &&
               state.mRefreshCount == 0);

    ensure("presentation-target adapter fixture acquires an instance",
           !owner->acquireInstanceGeneration(VulkanInstanceValidationMode::Disabled, VulkanInstancePortabilityMode::Disabled));
    const std::size_t refreshes_after_instance = state.mRefreshCount;

    state.mMainThread = false;
    const auto off_main = owner->acquireSwapchainPresentationTargetGeneration();
    ensure("off-main presentation-target acquisition is stale without refreshing native geometry",
           off_main && off_main->mCode == VulkanSwapchainPresentationTargetAcquireCode::StaleWindowGeneration &&
               state.mRefreshCount == refreshes_after_instance);

    state.mMainThread      = true;
    state.mRefreshSucceeds = false;
    const auto failed_refresh = owner->acquireSwapchainPresentationTargetGeneration();
    ensure("a failed Cocoa geometry refresh maps to a stale presentation-target window",
           failed_refresh && failed_refresh->mCode == VulkanSwapchainPresentationTargetAcquireCode::StaleWindowGeneration &&
               state.mRefreshCount == refreshes_after_instance + 1);

    state.mRefreshSucceeds = true;
    state.mRefreshMutation = RefreshMutation::ZeroHeight;
    const auto invalid_refresh = owner->acquireSwapchainPresentationTargetGeneration();
    ensure("an invalid refreshed backing height maps to a stale presentation-target window",
           invalid_refresh && invalid_refresh->mCode == VulkanSwapchainPresentationTargetAcquireCode::StaleWindowGeneration &&
               state.mRefreshCount == refreshes_after_instance + 2);

    state.mRefreshMutation = RefreshMutation::None;
    state.mRefreshScale    = 1.5;
    state.mRefreshWidth    = 1920;
    state.mRefreshHeight   = 1080;
    const auto missing_surface = owner->acquireSwapchainPresentationTargetGeneration();
    ensure("current Cocoa backing pixels are forwarded to the presentation-target parent",
           missing_surface && missing_surface->mCode == VulkanSwapchainPresentationTargetAcquireCode::SurfaceNotLive &&
               state.mRefreshCount == refreshes_after_instance + 3 && owner->drawableWidth() == 1920 &&
               owner->drawableHeight() == 1080);

    ensure("presentation-target adapter fixture acquires a surface", !owner->acquireSurfaceGeneration());
    const std::size_t refreshes_after_surface = state.mRefreshCount;
    const auto        missing_selection       = owner->acquireSwapchainPresentationTargetGeneration();
    ensure("the presentation-target adapter refreshes pixels through the exact surface parent",
           missing_selection &&
               missing_selection->mCode == VulkanSwapchainPresentationTargetAcquireCode::PresentationDeviceNotLive &&
               state.mRefreshCount == refreshes_after_surface + 1 && owner->drawableWidth() == 1920 &&
               owner->drawableHeight() == 1080);

    ensure("presentation-target adapter fixture acquires the remaining parents through the swapchain",
           !owner->acquirePresentationDeviceGeneration() && !owner->acquireLogicalDeviceGeneration() &&
               !owner->acquireSwapchainConfigurationGeneration() && !owner->acquireSwapchainGeneration());
    const auto missing_images = owner->acquireSwapchainPresentationTargetGeneration();
    ensure("presentation-target acquisition requires the exact swapchain-image parent",
           missing_images && missing_images->mCode == VulkanSwapchainPresentationTargetAcquireCode::SwapchainImagesNotLive &&
               state.mCreateRenderPassCount == 0 && state.mCreateFramebufferCount == 0);
    ensure("presentation-target adapter fixture acquires its swapchain-image parent",
           !owner->acquireSwapchainImagesGeneration());
    ensure("presentation-target acquisition succeeds through the authenticated Cocoa adapter",
           !owner->acquireSwapchainPresentationTargetGeneration());

    const VulkanInstanceGeneration* instance = owner->instanceGeneration();
    const VkRenderPass first_render_pass = instance ? instance->swapchainPresentationRenderPass() : VK_NULL_HANDLE;
    const VkFramebuffer first_framebuffer = instance ? instance->swapchainPresentationFramebuffer(0) : VK_NULL_HANDLE;
    ensure("the Cocoa adapter publishes one exact presentation target per swapchain image",
           instance && instance->hasSwapchainPresentationTargetGeneration() && first_render_pass != VK_NULL_HANDLE &&
               instance->swapchainPresentationFramebufferCount() == state.mImages.size() &&
               first_framebuffer != VK_NULL_HANDLE && instance->swapchainPresentationFramebuffer(1) != VK_NULL_HANDLE &&
               instance->swapchainPresentationFramebuffer(2) != VK_NULL_HANDLE &&
               first_framebuffer != instance->swapchainPresentationFramebuffer(1) &&
               instance->swapchainPresentationFramebuffer(1) != instance->swapchainPresentationFramebuffer(2) &&
               instance->swapchainPresentationFramebuffer(static_cast<std::uint32_t>(state.mImages.size())) == VK_NULL_HANDLE &&
               state.mCreateRenderPassCount == 1 && state.mCreateFramebufferCount == state.mImages.size() &&
               state.mDestroyRenderPassCount == 0 && state.mDestroyFramebufferCount == 0);

    const auto duplicate = owner->acquireSwapchainPresentationTargetGeneration();
    ensure("duplicate presentation-target acquisition is typed and does not create native resources",
           duplicate &&
               duplicate->mCode == VulkanSwapchainPresentationTargetAcquireCode::SwapchainPresentationTargetAlreadyOwned &&
               state.mCreateRenderPassCount == 1 && state.mCreateFramebufferCount == state.mImages.size());

    ensure("the complete-chain order admits a frame slot after the presentation target",
           !owner->acquireSwapchainFrameSlotGeneration() && instance->hasSwapchainFrameSlotGeneration());
    ensure("explicit presentation-target reset retires the younger frame slot and every target handle",
           owner->resetSwapchainPresentationTargetGeneration() &&
               !instance->hasSwapchainPresentationTargetGeneration() && !instance->hasSwapchainFrameSlotGeneration() &&
               instance->swapchainPresentationRenderPass() == VK_NULL_HANDLE &&
               instance->swapchainPresentationFramebufferCount() == 0 &&
               instance->swapchainPresentationFramebuffer(0) == VK_NULL_HANDLE && state.mDestroyRenderPassCount == 1 &&
               state.mDestroyFramebufferCount == state.mImages.size());
    ensure("an unowned presentation target reports no adapter-level reset",
           !owner->resetSwapchainPresentationTargetGeneration());

    ensure("the retained image parents can acquire a fresh presentation target",
           !owner->acquireSwapchainPresentationTargetGeneration() &&
               instance->swapchainPresentationRenderPass() != VK_NULL_HANDLE &&
               instance->swapchainPresentationRenderPass() != first_render_pass &&
               instance->swapchainPresentationFramebufferCount() == state.mImages.size() &&
               instance->swapchainPresentationFramebuffer(0) != VK_NULL_HANDLE &&
               instance->swapchainPresentationFramebuffer(0) != first_framebuffer && state.mCreateRenderPassCount == 2 &&
               state.mCreateFramebufferCount == state.mImages.size() * 2);
    ensure("resetting the image parent retires its presentation target first",
           owner->resetSwapchainImagesGeneration() && !instance->hasSwapchainImagesGeneration() &&
               !instance->hasSwapchainPresentationTargetGeneration() && instance->swapchainPresentationRenderPass() == VK_NULL_HANDLE &&
               instance->swapchainPresentationFramebufferCount() == 0 && state.mDestroyRenderPassCount == 2 &&
               state.mDestroyFramebufferCount == state.mImages.size() * 2);

    state.mOwnerDuringDestroy = owner;
    ensure("presentation-target adapter fixture tears down its retained parents", owner->reset());
    ensure_equals("presentation-target adapter checks preserve one surface destruction", state.mDestroySurfaceCount, std::size_t{ 1 });
    ensure_equals("presentation-target adapter checks preserve one instance destruction", state.mDestroyInstanceCount, std::size_t{ 1 });
}

template<>
template<>
void window_macosx_vulkan_object::test<20>()
{
    using namespace LLRenderVulkan;

    constexpr VulkanSwapchainFrameClearColor render_clear{ { 0.1875f, 0.4375f, 0.8125f, 1.0f } };

    FakeState   missing_state;
    ScopedState active(missing_state);
    auto        missing_result = acquireLLWindowMacOSXVulkan(createInfo(), 201, fakeOperations(missing_state));
    auto*       missing_owner  = acquiredWindow(missing_result);
    ensure("render-pass adapter fixture acquires its native owner", missing_owner != nullptr);

    const auto  missing_instance_result = missing_owner->acquireRenderPassClearToPresentSwapchainFrameSlot(render_clear);
    const auto* missing_instance        = presentationError(missing_instance_result);
    ensure("render-pass clear requires a live instance before observing Cocoa geometry",
           missing_instance && missing_instance->mCode == VulkanSwapchainFrameSlotParentOperationCode::InstanceNotLive &&
               missing_state.mRefreshCount == 0 && missing_state.mBeginRenderPassCount == 0);

    ensure("the missing-target fixture acquires every lower frame-slot parent",
           !missing_owner->acquireInstanceGeneration(VulkanInstanceValidationMode::Disabled,
                                                      VulkanInstancePortabilityMode::Disabled) &&
               !missing_owner->acquireSurfaceGeneration() && !missing_owner->acquirePresentationDeviceGeneration() &&
               !missing_owner->acquireLogicalDeviceGeneration() &&
               !missing_owner->acquireSwapchainConfigurationGeneration() && !missing_owner->acquireSwapchainGeneration() &&
               !missing_owner->acquireSwapchainImagesGeneration() && !missing_owner->acquireSwapchainFrameSlotGeneration());
    const std::size_t missing_target_refreshes = missing_state.mRefreshCount;
    const auto  missing_target_result = missing_owner->acquireRenderPassClearToPresentSwapchainFrameSlot(render_clear);
    const auto* missing_target        = presentationError(missing_target_result);
    ensure("render-pass clear reports the exact missing presentation-target parent after one current identity refresh",
           missing_target && missing_target->mCode == VulkanSwapchainFrameSlotParentOperationCode::SwapchainPresentationTargetNotLive &&
               missing_state.mRefreshCount == missing_target_refreshes + 1 && missing_state.mAcquireNextImageCount == 0 &&
               missing_state.mBeginRenderPassCount == 0 && missing_state.mEndRenderPassCount == 0);
    ensure("the lower frame-slot path remains independently usable without a presentation target",
           operationSucceeded(missing_owner->roundTripEmptySwapchainFrameSlot(), VulkanSwapchainFrameSlotDisposition::Reusable) &&
               missing_state.mBeginRenderPassCount == 0 && missing_state.mEndRenderPassCount == 0);
    missing_state.mOwnerDuringDestroy = missing_owner;
    ensure("the missing-target fixture tears down child-first", missing_owner->reset());

    FakeState state;
    active.use(state);
    auto  result = acquireLLWindowMacOSXVulkan(createInfo(), 202, fakeOperations(state));
    auto* owner  = acquiredWindow(result);
    ensure("render-pass clear fixture acquires a complete authenticated swapchain chain",
           owner && acquireCompleteSwapchainChain(*owner));
    const VulkanInstanceGeneration* instance = owner->instanceGeneration();
    ensure("render-pass clear fixture retains its exact current Cocoa parent",
           instance && owner->isGenerationCurrent(202) && instance->nativeWindowGeneration() == 202);
    const std::size_t refreshes_after_chain = state.mRefreshCount;

    state.mMainThread = false;
    const auto  off_main_result = owner->acquireRenderPassClearToPresentSwapchainFrameSlot(render_clear);
    const auto* off_main        = presentationError(off_main_result);
    ensure("off-main render-pass clear fails before refreshing or dispatching native work",
           off_main && off_main->mCode == VulkanSwapchainFrameSlotParentOperationCode::StaleWindowGeneration &&
               state.mRefreshCount == refreshes_after_chain && state.mAcquireNextImageCount == 0 &&
               state.mBeginRenderPassCount == 0);

    state.mMainThread      = true;
    state.mRefreshSucceeds = false;
    const auto  failed_refresh_result = owner->acquireRenderPassClearToPresentSwapchainFrameSlot(render_clear);
    const auto* failed_refresh        = presentationError(failed_refresh_result);
    ensure("a failed Cocoa refresh is a stale render-pass clear request",
           failed_refresh && failed_refresh->mCode == VulkanSwapchainFrameSlotParentOperationCode::StaleWindowGeneration &&
               state.mRefreshCount == refreshes_after_chain + 1 && state.mAcquireNextImageCount == 0 &&
               state.mBeginRenderPassCount == 0);

    state.mRefreshSucceeds = true;
    state.mRefreshMutation = RefreshMutation::Layer;
    const auto  stale_identity_result = owner->acquireRenderPassClearToPresentSwapchainFrameSlot(render_clear);
    const auto* stale_identity        = presentationError(stale_identity_result);
    ensure("a callback-mutated Metal-layer identity is rejected before render-pass dispatch",
           stale_identity && stale_identity->mCode == VulkanSwapchainFrameSlotParentOperationCode::StaleWindowGeneration &&
               state.mRefreshCount == refreshes_after_chain + 2 && state.mAcquireNextImageCount == 0 &&
               state.mBeginRenderPassCount == 0);

    state.mRefreshMutation = RefreshMutation::None;
    state.mRefreshWidth    = 0;
    const auto  zero_extent_result = owner->acquireRenderPassClearToPresentSwapchainFrameSlot(render_clear);
    const auto* zero_extent        = presentationError(zero_extent_result);
    ensure("zero refreshed Cocoa backing pixels fail without replacing the retained geometry",
           zero_extent && zero_extent->mCode == VulkanSwapchainFrameSlotParentOperationCode::InvalidDrawableExtent &&
               state.mRefreshCount == refreshes_after_chain + 3 && owner->drawableWidth() == 1280 &&
               owner->drawableHeight() == 720 && state.mAcquireNextImageCount == 0 && state.mBeginRenderPassCount == 0);

    state.mRefreshWidth  = 1600;
    state.mRefreshHeight = 900;
    const auto  changed_extent_result = owner->acquireRenderPassClearToPresentSwapchainFrameSlot(render_clear);
    const auto* changed_extent        = presentationError(changed_extent_result);
    ensure("render-pass clear forwards changed positive pixels to exact parent authentication",
           changed_extent && changed_extent->mCode == VulkanSwapchainFrameSlotParentOperationCode::DrawableExtentMismatch &&
               state.mRefreshCount == refreshes_after_chain + 4 && owner->drawableWidth() == 1600 &&
               owner->drawableHeight() == 900 && state.mAcquireNextImageCount == 0 && state.mBeginRenderPassCount == 0);

    VulkanSwapchainFrameClearColor invalid_clear = render_clear;
    invalid_clear.mRgba[0]                       = -0.01f;
    state.mRefreshWidth                          = 1280;
    state.mRefreshHeight                         = 720;
    const auto  invalid_clear_result = owner->acquireRenderPassClearToPresentSwapchainFrameSlot(invalid_clear);
    const auto* invalid_clear_error  = presentationError(invalid_clear_result);
    ensure("render-pass clear preserves the core's typed normalized-color preflight",
           invalid_clear_error && invalid_clear_error->mCode == VulkanSwapchainFrameSlotParentOperationCode::InvalidClearColor &&
               !invalid_clear_error->mOperationError && state.mRefreshCount == refreshes_after_chain + 5 &&
               state.mAcquireNextImageCount == 0 && state.mBeginRenderPassCount == 0);

    state.mAcquiredImageIndex = 2;
    const VkRenderPass  expected_render_pass = instance->swapchainPresentationRenderPass();
    const VkFramebuffer expected_framebuffer = instance->swapchainPresentationFramebuffer(2);
    const VkExtent2D    expected_extent      = instance->swapchainImageExtent();
    const auto          render_result = owner->acquireRenderPassClearToPresentSwapchainFrameSlot(render_clear);
    const VulkanSwapchainFrameSlotPresentationSuccess expected_success{
        VulkanSwapchainFrameSlotPresentationOutcome::Presented, std::uint32_t{ 2 }
    };
    ensure("render-pass clear returns the exact parent success and reusable image disposition",
           render_result == VulkanSwapchainFrameSlotParentPresentationResult{ expected_success } &&
               instance->swapchainFrameSlotDisposition() == VulkanSwapchainFrameSlotDisposition::Reusable &&
               !instance->swapchainFrameAcquiredImageIndex());
    ensure("render-pass clear refreshes once and records one balanced pass without a transfer clear",
           state.mRefreshCount == refreshes_after_chain + 6 && owner->drawableWidth() == 1280 && owner->drawableHeight() == 720 &&
               state.mAcquireNextImageCount == 1 && state.mPipelineBarrierCount == 2 && state.mBeginRenderPassCount == 1 &&
               state.mEndRenderPassCount == 1 && state.mClearColorImageCount == 0 && state.mQueueSubmitCount == 1 &&
               state.mQueuePresentCount == 1 && state.mWaitForFencesCount == 2 &&
               state.mSubmitWaitStage == VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    ensure("the recorded pass uses the exact acquired framebuffer, full image extent, and inline contents",
           state.mRenderPassCommandBuffer == state.mCommandBuffer && state.mRenderPass == expected_render_pass &&
               state.mRenderPassFramebuffer == expected_framebuffer && state.mRenderPassArea.offset.x == 0 &&
               state.mRenderPassArea.offset.y == 0 && state.mRenderPassArea.extent.width == expected_extent.width &&
               state.mRenderPassArea.extent.height == expected_extent.height && state.mRenderPassContents == VK_SUBPASS_CONTENTS_INLINE);
    ensure("the wrapper copies the exact clear value into the render-pass begin record",
           state.mRenderPassClear.color.float32[0] == render_clear.mRgba[0] &&
               state.mRenderPassClear.color.float32[1] == render_clear.mRgba[1] &&
               state.mRenderPassClear.color.float32[2] == render_clear.mRgba[2] &&
               state.mRenderPassClear.color.float32[3] == render_clear.mRgba[3]);

    constexpr VulkanSwapchainFrameClearColor transfer_clear{ { 0.75f, 0.125f, 0.25f, 1.0f } };
    state.mAcquiredImageIndex = 1;
    const auto legacy_result  = owner->acquireClearToPresentSwapchainFrameSlot(transfer_clear);
    ensure("the legacy transfer-clear wrapper remains independently reusable after a render-pass cycle",
           presentationSucceeded(legacy_result, VulkanSwapchainFrameSlotPresentationOutcome::Presented, 1) &&
               state.mRefreshCount == refreshes_after_chain + 7 && state.mBeginRenderPassCount == 1 &&
               state.mEndRenderPassCount == 1 && state.mClearColorImageCount == 1 &&
               state.mClearedImage == state.mImages[1] && state.mClearColor.float32[0] == transfer_clear.mRgba[0] &&
               state.mSubmitWaitStage == VK_PIPELINE_STAGE_TRANSFER_BIT &&
               instance->swapchainFrameSlotDisposition() == VulkanSwapchainFrameSlotDisposition::Reusable &&
               !instance->swapchainFrameAcquiredImageIndex());

    state.mOwnerDuringDestroy = owner;
    ensure("the render-pass clear fixture tears down child-first", owner->reset());
}

template<>
template<>
void window_macosx_vulkan_object::test<21>()
{
    using namespace LLRenderVulkan;

    FakeState   state;
    ScopedState active(state);
    auto        result = acquireLLWindowMacOSXVulkan(createInfo(), 211, fakeOperations(state));
    auto*       owner  = acquiredWindow(result);
    ensure("presentation-pipeline adapter fixture acquires its private Cocoa owner", owner != nullptr);

    const auto missing_instance = owner->acquireSwapchainPresentationPipelineGeneration();
    ensure("presentation-pipeline acquisition requires a live instance before observing Cocoa geometry",
           missing_instance && missing_instance->mCode == VulkanSwapchainPresentationPipelineAcquireCode::InstanceNotLive &&
               state.mRefreshCount == 0 && state.mCreateShaderModuleCount == 0 && state.mCreatePipelineCount == 0);

    ensure("presentation-pipeline adapter fixture acquires an instance",
           !owner->acquireInstanceGeneration(VulkanInstanceValidationMode::Disabled,
                                              VulkanInstancePortabilityMode::Disabled));
    const std::size_t refreshes_after_instance = state.mRefreshCount;

    state.mMainThread = false;
    const auto off_main = owner->acquireSwapchainPresentationPipelineGeneration();
    ensure("off-main presentation-pipeline acquisition is stale without refreshing private Cocoa state",
           off_main && off_main->mCode == VulkanSwapchainPresentationPipelineAcquireCode::StaleWindowGeneration &&
               state.mRefreshCount == refreshes_after_instance && state.mCreateShaderModuleCount == 0);

    state.mMainThread      = true;
    state.mRefreshSucceeds = false;
    const auto failed_refresh = owner->acquireSwapchainPresentationPipelineGeneration();
    ensure("a failed Cocoa refresh is a stale presentation-pipeline request",
           failed_refresh && failed_refresh->mCode == VulkanSwapchainPresentationPipelineAcquireCode::StaleWindowGeneration &&
               state.mRefreshCount == refreshes_after_instance + 1 && state.mCreateShaderModuleCount == 0);

    state.mRefreshSucceeds = true;
    state.mRefreshMutation = RefreshMutation::Layer;
    const auto stale_identity = owner->acquireSwapchainPresentationPipelineGeneration();
    ensure("changed Metal-layer identity is rejected before presentation-pipeline dispatch",
           stale_identity && stale_identity->mCode == VulkanSwapchainPresentationPipelineAcquireCode::StaleWindowGeneration &&
               state.mRefreshCount == refreshes_after_instance + 2 && state.mCreateShaderModuleCount == 0);

    state.mRefreshMutation = RefreshMutation::ZeroWidth;
    const auto zero_extent = owner->acquireSwapchainPresentationPipelineGeneration();
    ensure("zero refreshed Retina backing pixels are rejected before presentation-pipeline dispatch",
           zero_extent && zero_extent->mCode == VulkanSwapchainPresentationPipelineAcquireCode::StaleWindowGeneration &&
               state.mRefreshCount == refreshes_after_instance + 3 && owner->drawableWidth() == 1280 &&
               owner->drawableHeight() == 720 && state.mCreateShaderModuleCount == 0);

    state.mRefreshMutation = RefreshMutation::None;
    state.mRefreshScale    = 1.5;
    state.mRefreshWidth    = 1920;
    state.mRefreshHeight   = 1080;
    const auto missing_surface = owner->acquireSwapchainPresentationPipelineGeneration();
    ensure("current Retina backing pixels are forwarded to presentation-pipeline parent authentication",
           missing_surface && missing_surface->mCode == VulkanSwapchainPresentationPipelineAcquireCode::SurfaceNotLive &&
               state.mRefreshCount == refreshes_after_instance + 4 && owner->backingScale() == 1.5 &&
               owner->drawableWidth() == 1920 && owner->drawableHeight() == 1080 && state.mCreateShaderModuleCount == 0);

    ensure("presentation-pipeline adapter fixture acquires parents through swapchain images",
           !owner->acquireSurfaceGeneration() && !owner->acquirePresentationDeviceGeneration() &&
               !owner->acquireLogicalDeviceGeneration() && !owner->acquireSwapchainConfigurationGeneration() &&
               !owner->acquireSwapchainGeneration() && !owner->acquireSwapchainImagesGeneration());
    const auto missing_target = owner->acquireSwapchainPresentationPipelineGeneration();
    ensure("presentation-pipeline acquisition requires the exact presentation-target parent",
           missing_target &&
               missing_target->mCode == VulkanSwapchainPresentationPipelineAcquireCode::SwapchainPresentationTargetNotLive &&
               state.mCreateShaderModuleCount == 0 && state.mCreatePipelineLayoutCount == 0 && state.mCreatePipelineCount == 0);

    ensure("presentation-pipeline adapter fixture acquires its exact presentation target",
           !owner->acquireSwapchainPresentationTargetGeneration());
    ensure("presentation-pipeline acquisition succeeds through the authenticated Cocoa adapter",
           !owner->acquireSwapchainPresentationPipelineGeneration());

    const VulkanInstanceGeneration* instance = owner->instanceGeneration();
    ensure("the Cocoa adapter publishes one non-null render-pass-compatible presentation pipeline",
           instance && instance->hasSwapchainPresentationPipelineGeneration() &&
               instance->swapchainPresentationPipelineLayout() != VK_NULL_HANDLE &&
               instance->swapchainPresentationPipeline() != VK_NULL_HANDLE && state.mCreateShaderModuleCount == 2 &&
               state.mDestroyShaderModuleCount == 2 && state.mCreatePipelineLayoutCount == 1 &&
               state.mDestroyPipelineLayoutCount == 0 && state.mCreatePipelineCount == 1 &&
               state.mDestroyPipelineCount == 0);

    const auto duplicate = owner->acquireSwapchainPresentationPipelineGeneration();
    ensure("duplicate presentation-pipeline acquisition is typed without creating native resources",
           duplicate &&
               duplicate->mCode == VulkanSwapchainPresentationPipelineAcquireCode::SwapchainPresentationPipelineAlreadyOwned &&
               state.mCreateShaderModuleCount == 2 && state.mCreatePipelineLayoutCount == 1 && state.mCreatePipelineCount == 1);

    ensure("the complete-chain order admits one frame slot after the presentation pipeline",
           !owner->acquireSwapchainFrameSlotGeneration() && instance->hasSwapchainFrameSlotGeneration());
    state.mMainThread = false;
    ensure("off-main presentation-pipeline reset preserves the complete published chain",
           !owner->resetSwapchainPresentationPipelineGeneration() &&
               instance->hasSwapchainPresentationPipelineGeneration() && instance->hasSwapchainFrameSlotGeneration() &&
               state.mDestroyPipelineCount == 0 && state.mDestroyPipelineLayoutCount == 0);
    state.mMainThread = true;
    ensure("explicit presentation-pipeline reset retires the younger frame slot before both pipeline handles",
           owner->resetSwapchainPresentationPipelineGeneration() &&
               !instance->hasSwapchainPresentationPipelineGeneration() && !instance->hasSwapchainFrameSlotGeneration() &&
               instance->swapchainPresentationPipelineLayout() == VK_NULL_HANDLE &&
               instance->swapchainPresentationPipeline() == VK_NULL_HANDLE &&
               instance->hasSwapchainPresentationTargetGeneration() && state.mDestroyPipelineCount == 1 &&
               state.mDestroyPipelineLayoutCount == 1);
    ensure("an unowned presentation pipeline reports no adapter-level reset",
           !owner->resetSwapchainPresentationPipelineGeneration());

    ensure("retained target parents reacquire a fresh pipeline and younger frame slot",
           !owner->acquireSwapchainPresentationPipelineGeneration() && !owner->acquireSwapchainFrameSlotGeneration() &&
               instance->swapchainPresentationPipelineLayout() != VK_NULL_HANDLE &&
               instance->swapchainPresentationPipeline() != VK_NULL_HANDLE &&
               state.mCreateShaderModuleCount == 4 && state.mDestroyShaderModuleCount == 4 &&
               state.mCreatePipelineLayoutCount == 2 && state.mDestroyPipelineLayoutCount == 1 &&
               state.mCreatePipelineCount == 2 && state.mDestroyPipelineCount == 1);

    state.mRefreshScale  = 2.0;
    state.mRefreshWidth  = 1600;
    state.mRefreshHeight = 900;
    const auto  rebuild_result  = owner->rebuildSwapchainChain();
    const auto* rebuild_outcome = std::get_if<VulkanSwapchainChainRebuildOutcome>(&rebuild_result);
    ensure("changed Retina backing pixels rebuild target, presentation pipeline, and frame slot in dependency order",
           rebuild_outcome && *rebuild_outcome == VulkanSwapchainChainRebuildOutcome::Ready &&
               owner->backingScale() == 2.0 && owner->drawableWidth() == 1600 && owner->drawableHeight() == 900 &&
               instance->hasSwapchainPresentationTargetGeneration() &&
               instance->hasSwapchainPresentationPipelineGeneration() && instance->hasSwapchainFrameSlotGeneration() &&
               instance->swapchainPresentationPipelineLayout() != VK_NULL_HANDLE &&
               instance->swapchainPresentationPipeline() != VK_NULL_HANDLE && owner->isGenerationCurrent(211));
    ensure("pipeline rebuild destroys every transient shader and retires only the two replaced owned generations",
           state.mCreateShaderModuleCount == 6 && state.mDestroyShaderModuleCount == 6 &&
               state.mCreatePipelineLayoutCount == 3 && state.mDestroyPipelineLayoutCount == 2 &&
               state.mCreatePipelineCount == 3 && state.mDestroyPipelineCount == 2);

    state.mOwnerDuringDestroy = owner;
    ensure("presentation-pipeline adapter fixture tears down child-first", owner->reset());
    ensure_equals("presentation-pipeline teardown balances pipeline destruction", state.mDestroyPipelineCount,
                  state.mCreatePipelineCount);
    ensure_equals("presentation-pipeline teardown balances layout destruction", state.mDestroyPipelineLayoutCount,
                  state.mCreatePipelineLayoutCount);
    ensure_equals("presentation-pipeline adapter preserves one surface destruction", state.mDestroySurfaceCount,
                  std::size_t{ 1 });
    ensure_equals("presentation-pipeline adapter preserves one instance destruction", state.mDestroyInstanceCount,
                  std::size_t{ 1 });
}

template<>
template<>
void window_macosx_vulkan_object::test<22>()
{
    using namespace LLRenderVulkan;

    constexpr VulkanSwapchainFrameClearColor draw_clear{ { 0.15625f, 0.46875f, 0.78125f, 1.0f } };

    FakeState   missing_state;
    ScopedState active(missing_state);
    auto        missing_result = acquireLLWindowMacOSXVulkan(createInfo(), 221, fakeOperations(missing_state));
    auto*       missing_owner  = acquiredWindow(missing_result);
    ensure("draw adapter fixture acquires its private Cocoa owner", missing_owner != nullptr);

    const auto  missing_instance_result = missing_owner->acquireRenderPassDrawToPresentSwapchainFrameSlot(draw_clear);
    const auto* missing_instance        = presentationError(missing_instance_result);
    ensure("diagnostic draw requires a live instance before observing Cocoa geometry",
           missing_instance && missing_instance->mCode == VulkanSwapchainFrameSlotParentOperationCode::InstanceNotLive &&
               missing_state.mRefreshCount == 0 && missing_state.mAcquireNextImageCount == 0 &&
               missing_state.mBindVertexBuffersCount == 0 && missing_state.mDrawCount == 0);

    ensure("the missing-pipeline fixture acquires the exact target and a younger frame slot",
           !missing_owner->acquireInstanceGeneration(VulkanInstanceValidationMode::Disabled, VulkanInstancePortabilityMode::Disabled) &&
               !missing_owner->acquireSurfaceGeneration() && !missing_owner->acquirePresentationDeviceGeneration() &&
               !missing_owner->acquireLogicalDeviceGeneration() && !missing_owner->acquireSwapchainConfigurationGeneration() &&
               !missing_owner->acquireSwapchainGeneration() && !missing_owner->acquireSwapchainImagesGeneration() &&
               !missing_owner->acquireSwapchainPresentationTargetGeneration() && !missing_owner->acquireSwapchainFrameSlotGeneration());
    const std::size_t missing_pipeline_refreshes = missing_state.mRefreshCount;
    const auto        missing_pipeline_result    = missing_owner->acquireRenderPassDrawToPresentSwapchainFrameSlot(draw_clear);
    const auto*       missing_pipeline           = presentationError(missing_pipeline_result);
    ensure("diagnostic draw reports the typed missing presentation-pipeline parent after one current identity refresh",
           missing_pipeline &&
               missing_pipeline->mCode == VulkanSwapchainFrameSlotParentOperationCode::SwapchainPresentationPipelineNotLive &&
               missing_state.mRefreshCount == missing_pipeline_refreshes + 1 && missing_state.mAcquireNextImageCount == 0 &&
               missing_state.mBindPipelineCount == 0 && missing_state.mBindVertexBuffersCount == 0 && missing_state.mDrawCount == 0);
    missing_state.mAcquiredImageIndex = 1;
    const auto clear_without_pipeline = missing_owner->acquireRenderPassClearToPresentSwapchainFrameSlot(draw_clear);
    ensure("the existing render-pass clear route remains draw-free without a presentation pipeline",
           presentationSucceeded(clear_without_pipeline, VulkanSwapchainFrameSlotPresentationOutcome::Presented, 1) &&
               missing_state.mBeginRenderPassCount == 1 && missing_state.mEndRenderPassCount == 1 &&
               missing_state.mBindPipelineCount == 0 && missing_state.mBindVertexBuffersCount == 0 &&
               missing_state.mSetViewportCount == 0 && missing_state.mSetScissorCount == 0 && missing_state.mDrawCount == 0);
    missing_state.mOwnerDuringDestroy = missing_owner;
    ensure("the missing-pipeline fixture tears down child-first", missing_owner->reset());

    FakeState state;
    active.use(state);
    auto  result = acquireLLWindowMacOSXVulkan(createInfo(), 222, fakeOperations(state));
    auto* owner  = acquiredWindow(result);
    ensure("diagnostic draw fixture acquires target, pipeline, then a fresh frame slot", owner && acquireCompleteSwapchainChain(*owner));
    const VulkanInstanceGeneration* instance = owner->instanceGeneration();
    ResidentUploadDestination       retained_destination;
    ensure("diagnostic draw fixture completes one upload and retires its source and terminal transfer",
           owner && acquireResidentUploadDestination(*owner, retained_destination));
    ensure("the upload copies the exact canonical bytes and destroys only its retired source",
           state.mUploadMappedBytes == LLRenderContract::SCREEN_TRIANGLE_BYTES && state.mCopyBufferCount == 1 &&
               !state.mInvalidCopyBuffer && state.mUploadSourceDestroyCount == 1 && state.mUploadSourceFreeCount == 1 &&
               state.mUploadDestinationDestroyCount == 0 && state.mUploadDestinationFreeCount == 0);
    ensure("complete-chain acquisition has no implicit draw hook",
           instance && owner->isGenerationCurrent(222) && instance->nativeWindowGeneration() == 222 &&
               instance->hasSwapchainPresentationTargetGeneration() && instance->hasSwapchainPresentationPipelineGeneration() &&
               instance->hasSwapchainFrameSlotGeneration() && instance->hasUploadDestinationGeneration() &&
               instance->uploadDestinationIsResident() && !instance->hasUploadSourceGeneration() &&
               !instance->hasUploadTransferGeneration() && state.mBindPipelineCount == 0 && state.mBindVertexBuffersCount == 0 &&
               state.mSetViewportCount == 0 && state.mSetScissorCount == 0 && state.mDrawCount == 0);
    const std::size_t refreshes_after_chain = state.mRefreshCount;

    state.mMainThread           = false;
    const auto  off_main_result = owner->acquireRenderPassDrawToPresentSwapchainFrameSlot(draw_clear);
    const auto* off_main        = presentationError(off_main_result);
    ensure("off-main diagnostic draw fails before refreshing or dispatching native work",
           off_main && off_main->mCode == VulkanSwapchainFrameSlotParentOperationCode::StaleWindowGeneration &&
               state.mRefreshCount == refreshes_after_chain && state.mAcquireNextImageCount == 0 && state.mBindVertexBuffersCount == 0 &&
               state.mDrawCount == 0);

    state.mMainThread                 = true;
    state.mRefreshSucceeds            = false;
    const auto  failed_refresh_result = owner->acquireRenderPassDrawToPresentSwapchainFrameSlot(draw_clear);
    const auto* failed_refresh        = presentationError(failed_refresh_result);
    ensure("a failed Cocoa refresh is a stale diagnostic draw request",
           failed_refresh && failed_refresh->mCode == VulkanSwapchainFrameSlotParentOperationCode::StaleWindowGeneration &&
               state.mRefreshCount == refreshes_after_chain + 1 && state.mAcquireNextImageCount == 0 &&
               state.mBindVertexBuffersCount == 0 && state.mDrawCount == 0);

    state.mRefreshSucceeds            = true;
    state.mRefreshMutation            = RefreshMutation::Layer;
    const auto  stale_identity_result = owner->acquireRenderPassDrawToPresentSwapchainFrameSlot(draw_clear);
    const auto* stale_identity        = presentationError(stale_identity_result);
    ensure("a callback-mutated Metal-layer identity is rejected before diagnostic draw dispatch",
           stale_identity && stale_identity->mCode == VulkanSwapchainFrameSlotParentOperationCode::StaleWindowGeneration &&
               state.mRefreshCount == refreshes_after_chain + 2 && state.mAcquireNextImageCount == 0 &&
               state.mBindVertexBuffersCount == 0 && state.mDrawCount == 0);

    state.mRefreshMutation         = RefreshMutation::None;
    state.mRefreshWidth            = 0;
    const auto  zero_extent_result = owner->acquireRenderPassDrawToPresentSwapchainFrameSlot(draw_clear);
    const auto* zero_extent        = presentationError(zero_extent_result);
    ensure("zero refreshed Cocoa backing pixels fail without replacing the retained geometry",
           zero_extent && zero_extent->mCode == VulkanSwapchainFrameSlotParentOperationCode::InvalidDrawableExtent &&
               state.mRefreshCount == refreshes_after_chain + 3 && owner->drawableWidth() == 1280 && owner->drawableHeight() == 720 &&
               state.mAcquireNextImageCount == 0 && state.mBindVertexBuffersCount == 0 && state.mDrawCount == 0);

    state.mRefreshWidth               = 1600;
    state.mRefreshHeight              = 900;
    const auto  changed_extent_result = owner->acquireRenderPassDrawToPresentSwapchainFrameSlot(draw_clear);
    const auto* changed_extent        = presentationError(changed_extent_result);
    ensure("diagnostic draw forwards changed positive pixels to exact parent authentication",
           changed_extent && changed_extent->mCode == VulkanSwapchainFrameSlotParentOperationCode::DrawableExtentMismatch &&
               state.mRefreshCount == refreshes_after_chain + 4 && owner->drawableWidth() == 1600 && owner->drawableHeight() == 900 &&
               state.mAcquireNextImageCount == 0 && state.mBindVertexBuffersCount == 0 && state.mDrawCount == 0);

    VulkanSwapchainFrameClearColor invalid_clear = draw_clear;
    invalid_clear.mRgba[0]                       = -0.01f;
    state.mRefreshWidth                          = 1280;
    state.mRefreshHeight                         = 720;
    const auto  invalid_clear_result             = owner->acquireRenderPassDrawToPresentSwapchainFrameSlot(invalid_clear);
    const auto* invalid_clear_error              = presentationError(invalid_clear_result);
    ensure("diagnostic draw preserves the core's typed normalized-color preflight",
           invalid_clear_error && invalid_clear_error->mCode == VulkanSwapchainFrameSlotParentOperationCode::InvalidClearColor &&
               !invalid_clear_error->mOperationError && state.mRefreshCount == refreshes_after_chain + 5 &&
               state.mAcquireNextImageCount == 0 && state.mBindVertexBuffersCount == 0 && state.mDrawCount == 0);

    state.mAcquiredImageIndex                                              = 2;
    const VkRenderPass                                expected_render_pass = instance->swapchainPresentationRenderPass();
    const VkFramebuffer                               expected_framebuffer = instance->swapchainPresentationFramebuffer(2);
    const VkPipeline                                  expected_pipeline    = instance->swapchainPresentationPipeline();
    const VkExtent2D                                  expected_extent      = instance->swapchainImageExtent();
    const std::size_t                                 queue_submits_before_draw = state.mQueueSubmitCount;
    const std::size_t                                 waits_before_draw         = state.mWaitForFencesCount;
    const auto                                        draw_result = owner->acquireRenderPassDrawToPresentSwapchainFrameSlot(draw_clear);
    const VulkanSwapchainFrameSlotPresentationSuccess expected_success{ VulkanSwapchainFrameSlotPresentationOutcome::Presented,
                                                                        std::uint32_t{ 2 } };
    ensure("diagnostic draw returns the exact parent success and reusable image disposition",
           draw_result == VulkanSwapchainFrameSlotParentPresentationResult{ expected_success } &&
               instance->swapchainFrameSlotDisposition() == VulkanSwapchainFrameSlotDisposition::Reusable &&
               !instance->swapchainFrameAcquiredImageIndex() && instance->hasUploadDestinationGeneration() &&
               instance->uploadDestinationResourceHandle() == retained_destination.mHandle &&
               instance->uploadDestinationExpectedContentIdentity() == retained_destination.mExpectedIdentity &&
               instance->uploadDestinationResidentContentIdentity() == retained_destination.mResidentIdentity &&
               instance->uploadDestinationBuffer() == retained_destination.mBuffer &&
               instance->uploadDestinationMemory() == retained_destination.mMemory && state.mUploadDestinationDestroyCount == 0 &&
               state.mUploadDestinationFreeCount == 0);
    ensure("diagnostic draw refreshes once and records one balanced submitted pass",
           state.mRefreshCount == refreshes_after_chain + 6 && owner->drawableWidth() == 1280 && owner->drawableHeight() == 720 &&
               state.mAcquireNextImageCount == 1 && state.mPipelineBarrierCount == 2 && state.mBeginRenderPassCount == 1 &&
               state.mEndRenderPassCount == 1 && state.mClearColorImageCount == 0 && state.mBindPipelineCount == 1 &&
               state.mBindVertexBuffersCount == 1 && state.mSetViewportCount == 1 && state.mSetScissorCount == 1 && state.mDrawCount == 1 &&
               state.mQueueSubmitCount == queue_submits_before_draw + 1 && state.mQueuePresentCount == 1 &&
               state.mWaitForFencesCount == waits_before_draw + 2 &&
               state.mSubmitWaitStage == VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    ensure("diagnostic draw forwards the acquired framebuffer, pipeline, and exact clear value",
           state.mRenderPassCommandBuffer == state.mCommandBuffer && state.mRenderPass == expected_render_pass &&
               state.mRenderPassFramebuffer == expected_framebuffer && state.mRenderPassArea.offset.x == 0 &&
               state.mRenderPassArea.offset.y == 0 && state.mRenderPassArea.extent.width == expected_extent.width &&
               state.mRenderPassArea.extent.height == expected_extent.height && state.mRenderPassContents == VK_SUBPASS_CONTENTS_INLINE &&
               state.mDrawCommandBuffer == state.mCommandBuffer && state.mPipelineBindPoint == VK_PIPELINE_BIND_POINT_GRAPHICS &&
               state.mBoundPipeline == expected_pipeline && state.mFirstVertexBinding == 0 && state.mVertexBindingCount == 1 &&
               state.mBoundVertexBuffer == retained_destination.mBuffer && state.mBoundVertexOffset == 0 &&
               state.mBindPipelineOrder < state.mBindVertexBuffersOrder && state.mBindVertexBuffersOrder < state.mDrawOrder &&
               state.mRenderPassClear.color.float32[0] == draw_clear.mRgba[0] &&
               state.mRenderPassClear.color.float32[1] == draw_clear.mRgba[1] &&
               state.mRenderPassClear.color.float32[2] == draw_clear.mRgba[2] &&
               state.mRenderPassClear.color.float32[3] == draw_clear.mRgba[3]);
    ensure("diagnostic draw forwards one full positive-height dynamic viewport and matching scissor",
           state.mFirstViewport == 0 && state.mViewport.x == 0.0f && state.mViewport.y == 0.0f &&
               state.mViewport.width == static_cast<float>(expected_extent.width) &&
               state.mViewport.height == static_cast<float>(expected_extent.height) && state.mViewport.minDepth == 0.0f &&
               state.mViewport.maxDepth == 1.0f && state.mFirstScissor == 0 && state.mScissor.offset.x == 0 &&
               state.mScissor.offset.y == 0 && state.mScissor.extent.width == expected_extent.width &&
               state.mScissor.extent.height == expected_extent.height);
    ensure("diagnostic draw forwards one exact three-vertex, one-instance draw",
           state.mDrawVertexCount == 3 && state.mDrawInstanceCount == 1 && state.mDrawFirstVertex == 0 && state.mDrawFirstInstance == 0);

    state.mAcquiredImageIndex = 1;
    const auto clear_result   = owner->acquireRenderPassClearToPresentSwapchainFrameSlot(draw_clear);
    ensure("the existing render-pass clear wrapper remains independent from the explicit draw route",
           presentationSucceeded(clear_result, VulkanSwapchainFrameSlotPresentationOutcome::Presented, 1) &&
               state.mRefreshCount == refreshes_after_chain + 7 && state.mBeginRenderPassCount == 2 && state.mEndRenderPassCount == 2 &&
               state.mBindPipelineCount == 1 && state.mBindVertexBuffersCount == 1 && state.mSetViewportCount == 1 &&
               state.mSetScissorCount == 1 && state.mDrawCount == 1 && state.mUploadDestinationDestroyCount == 0 &&
               state.mUploadDestinationFreeCount == 0);

    state.mOwnerDuringDestroy = owner;
    ensure("the diagnostic draw fixture tears down child-first and finally releases the resident destination",
           owner->reset() && state.mUploadDestinationDestroyCount == 1 && state.mUploadDestinationFreeCount == 1);
}

template<>
template<>
void window_macosx_vulkan_object::test<23>()
{
    using namespace LLRenderVulkan;

    FakeState   state;
    ScopedState active(state);
    auto        result = acquireLLWindowMacOSXVulkan(createInfo(), 223, fakeOperations(state));
    auto*       owner  = acquiredWindow(result);
    ensure("readback adapter fixture acquires its private Cocoa owner", owner != nullptr);

    const VulkanSwapchainReadbackAcquireResult missing_instance = owner->acquireSwapchainReadbackGeneration();
    ensure("readback requires a live instance before refreshing Cocoa geometry",
           missing_instance && missing_instance->mCode == VulkanSwapchainReadbackAcquireCode::InstanceNotLive && state.mRefreshCount == 0);

    ensure("readback fixture acquires the exact chain through swapchain images",
           !owner->acquireInstanceGeneration(VulkanInstanceValidationMode::Disabled, VulkanInstancePortabilityMode::Disabled) &&
               !owner->acquireSurfaceGeneration() && !owner->acquirePresentationDeviceGeneration() &&
               !owner->acquireLogicalDeviceGeneration() && !owner->acquireSwapchainConfigurationGeneration() &&
               !owner->acquireSwapchainGeneration() && !owner->acquireSwapchainImagesGeneration());
    const VulkanInstanceGeneration* instance = owner->instanceGeneration();
    ensure("the readback owner is absent before explicit acquisition",
           instance && !instance->hasSwapchainReadbackGeneration() && !owner->resetSwapchainReadbackGeneration());

    const std::size_t refreshes                         = state.mRefreshCount;
    state.mMainThread                                   = false;
    const VulkanSwapchainReadbackAcquireResult off_main = owner->acquireSwapchainReadbackGeneration();
    ensure("off-main readback fails before refreshing or mutating native state",
           off_main && off_main->mCode == VulkanSwapchainReadbackAcquireCode::StaleWindowGeneration && state.mRefreshCount == refreshes &&
               state.mCreateBufferCount == 0);
    state.mMainThread = true;

    ensure("readback acquisition depends on images but not presentation target or pipeline",
           !owner->acquireSwapchainReadbackGeneration() && instance->hasSwapchainReadbackGeneration() &&
               !instance->hasSwapchainPresentationTargetGeneration() && !instance->hasSwapchainPresentationPipelineGeneration());
    ensure("the Cocoa adapter publishes exact Retina readback metadata",
           instance->swapchainReadbackBuffer() == state.mReadbackBuffer && instance->swapchainReadbackMemory() == state.mReadbackMemory &&
               instance->swapchainReadbackIsMapped() &&
               instance->swapchainReadbackImageFormat() == VK_FORMAT_B8G8R8A8_UNORM &&
               instance->swapchainReadbackImageExtent().width == 1280 && instance->swapchainReadbackImageExtent().height == 720 &&
               instance->swapchainReadbackImageCount() == 3 && instance->swapchainReadbackRowBytes() == 1280 * 4 &&
               instance->swapchainReadbackByteCount() == 1280 * 720 * 4 &&
               instance->swapchainReadbackAllocationSize() >= instance->swapchainReadbackByteCount() &&
               (instance->swapchainReadbackMemoryPropertyFlags() &
                (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                   (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));

    const VulkanSwapchainReadbackAcquireResult duplicate = owner->acquireSwapchainReadbackGeneration();
    ensure("duplicate readback acquisition is typed and performs no second mutation",
           duplicate && duplicate->mCode == VulkanSwapchainReadbackAcquireCode::SwapchainReadbackAlreadyOwned &&
               state.mCreateBufferCount == 1 && state.mAllocateMemoryCount == 1 && state.mMapMemoryCount == 1);
    ensure("explicit readback reset unmaps, destroys, and frees without disturbing images",
           owner->resetSwapchainReadbackGeneration() && !instance->hasSwapchainReadbackGeneration() &&
               instance->hasSwapchainImagesGeneration() && state.mUnmapMemoryCount == 1 && state.mDestroyBufferCount == 1 &&
               state.mFreeMemoryCount == 1 && !owner->resetSwapchainReadbackGeneration());
    ensure("the independent frame-slot sibling remains legal without readback",
           !owner->acquireSwapchainFrameSlotGeneration() && instance->hasSwapchainFrameSlotGeneration());
    state.mOwnerDuringDestroy = owner;
    ensure("readback adapter fixture tears down its retained chain", owner->reset());
}

template<>
template<>
void window_macosx_vulkan_object::test<24>()
{
    using namespace LLRenderVulkan;

    FakeState   state;
    state.mMemoryProperties.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    ScopedState active(state);
    auto        result = acquireLLWindowMacOSXVulkan(createInfo(), 224, fakeOperations(state));
    auto*       owner  = acquiredWindow(result);
    ensure("texture destination fixture acquires its private Cocoa owner", owner != nullptr);
    ensure("texture destination fixture acquires the exact device parent chain",
           !owner->acquireInstanceGeneration(VulkanInstanceValidationMode::Disabled, VulkanInstancePortabilityMode::Disabled) &&
               !owner->acquireSurfaceGeneration() && !owner->acquirePresentationDeviceGeneration() &&
               !owner->acquireLogicalDeviceGeneration());

    auto*                                 instance = const_cast<VulkanInstanceGeneration*>(owner->instanceGeneration());
    UploadOperationContext                context{ owner, instance };
    VulkanTextureUploadDestinationRequest request;
    request.mNativeWindowGeneration = 224;
    request.mDescription            = vulkanTextureUploadDestinationDescription();
    request.mInstanceOwnerCheck     = { &context, uploadInstanceOwnerIsCurrent };
    request.mWindowGenerationCheck  = { &context, uploadWindowGenerationIsCurrent };

    ensure("the current Cocoa device acquires one texture upload destination",
           !instance->acquireTextureUploadDestinationGeneration(request));
    const VkImageCreateInfo&     create_info = state.mTextureUploadDestinationCreateInfo;
    const VkImageViewCreateInfo& view_info   = state.mTextureUploadDestinationViewCreateInfo;
    const VkMemoryRequirements   memory_requirements = instance->textureUploadDestinationMemoryRequirements();
    const VkImageFormatProperties image_limits = instance->textureUploadDestinationImageFormatProperties();
    const VkImageSubresourceRange view_range   = instance->textureUploadDestinationViewRange();
    constexpr VkFormatFeatureFlags required_features = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
                                                       VK_FORMAT_FEATURE_TRANSFER_DST_BIT | VK_FORMAT_FEATURE_BLIT_SRC_BIT |
                                                       VK_FORMAT_FEATURE_BLIT_DST_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    ensure(
        "texture destination publishes its exact image, allocation, and view metadata",
        instance->hasTextureUploadDestinationGeneration() &&
            instance->textureUploadDestinationResourceHandle() == request.mDescription.mHandle &&
            instance->textureUploadDestinationExpectedRevision() == request.mDescription.mExpectedRevision &&
            instance->textureUploadDestinationResidentExtent().width == request.mDescription.mResidentExtent.mWidth &&
            instance->textureUploadDestinationResidentExtent().height == request.mDescription.mResidentExtent.mHeight &&
            instance->textureUploadDestinationLogicalExtent().mWidth == request.mDescription.mLogicalExtent.mWidth &&
            instance->textureUploadDestinationLogicalExtent().mHeight == request.mDescription.mLogicalExtent.mHeight &&
            instance->textureUploadDestinationResidentDiscard() == request.mDescription.mResidentDiscard &&
            instance->textureUploadDestinationPixelFormat() == request.mDescription.mFormat &&
            instance->textureUploadDestinationInitialState() == request.mDescription.mInitialState &&
            instance->textureUploadDestinationFlags() == 0 && instance->textureUploadDestinationImageType() == VK_IMAGE_TYPE_2D &&
            instance->textureUploadDestinationFormat() == VK_FORMAT_R8G8B8A8_UNORM &&
            instance->textureUploadDestinationMipLevels() == request.mDescription.mMipLevels &&
            instance->textureUploadDestinationArrayLayers() == request.mDescription.mArrayLayers &&
            instance->textureUploadDestinationSamples() == VK_SAMPLE_COUNT_1_BIT &&
            instance->textureUploadDestinationTiling() == VK_IMAGE_TILING_OPTIMAL &&
            instance->textureUploadDestinationUsage() ==
                (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT) &&
            instance->textureUploadDestinationSharingMode() == VK_SHARING_MODE_EXCLUSIVE &&
            instance->textureUploadDestinationInitialLayout() == VK_IMAGE_LAYOUT_UNDEFINED &&
            (instance->textureUploadDestinationFormatFeatures() & required_features) == required_features &&
            image_limits.maxExtent.width >= request.mDescription.mResidentExtent.mWidth &&
            image_limits.maxExtent.height >= request.mDescription.mResidentExtent.mHeight && image_limits.maxExtent.depth >= 1 &&
            image_limits.maxMipLevels >= request.mDescription.mMipLevels &&
            image_limits.maxArrayLayers >= request.mDescription.mArrayLayers &&
            (image_limits.sampleCounts & VK_SAMPLE_COUNT_1_BIT) != 0 &&
            instance->textureUploadDestinationImage() == state.mTextureUploadDestinationImage &&
            instance->textureUploadDestinationMemory() == state.mTextureUploadDestinationMemory &&
            instance->textureUploadDestinationImageView() == state.mTextureUploadDestinationImageView &&
            memory_requirements.size == state.mTextureUploadDestinationMemoryRequirements.size &&
            memory_requirements.alignment == state.mTextureUploadDestinationMemoryRequirements.alignment &&
            memory_requirements.memoryTypeBits == state.mTextureUploadDestinationMemoryRequirements.memoryTypeBits &&
            instance->textureUploadDestinationAllocationSize() == memory_requirements.size &&
            instance->textureUploadDestinationAllocationAlignment() == memory_requirements.alignment &&
            instance->textureUploadDestinationCompatibleMemoryTypeBits() == memory_requirements.memoryTypeBits &&
            instance->textureUploadDestinationMemoryTypeIndex() == 0 &&
            instance->textureUploadDestinationIsDeviceLocal() && instance->textureUploadDestinationPrefersDedicatedAllocation() &&
            !instance->textureUploadDestinationRequiresDedicatedAllocation() &&
            instance->textureUploadDestinationImageViewType() == VK_IMAGE_VIEW_TYPE_2D &&
            view_range.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT && view_range.baseMipLevel == 0 &&
            view_range.levelCount == request.mDescription.mMipLevels && view_range.baseArrayLayer == 0 &&
            view_range.layerCount == request.mDescription.mArrayLayers && create_info.sType == VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO &&
            create_info.pNext == nullptr && create_info.flags == 0 && create_info.imageType == VK_IMAGE_TYPE_2D &&
            create_info.format == VK_FORMAT_R8G8B8A8_UNORM && create_info.extent.width == request.mDescription.mResidentExtent.mWidth &&
            create_info.extent.height == request.mDescription.mResidentExtent.mHeight && create_info.extent.depth == 1 &&
            create_info.mipLevels == request.mDescription.mMipLevels && create_info.arrayLayers == request.mDescription.mArrayLayers &&
            create_info.samples == VK_SAMPLE_COUNT_1_BIT && create_info.tiling == VK_IMAGE_TILING_OPTIMAL &&
            create_info.usage == (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT) &&
            create_info.sharingMode == VK_SHARING_MODE_EXCLUSIVE && create_info.queueFamilyIndexCount == 0 &&
            create_info.pQueueFamilyIndices == nullptr && create_info.initialLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
            view_info.sType == VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO && view_info.pNext == nullptr && view_info.flags == 0 &&
            view_info.image == state.mTextureUploadDestinationImage && view_info.viewType == VK_IMAGE_VIEW_TYPE_2D &&
            view_info.format == VK_FORMAT_R8G8B8A8_UNORM && view_info.components.r == VK_COMPONENT_SWIZZLE_IDENTITY &&
            view_info.components.g == VK_COMPONENT_SWIZZLE_IDENTITY && view_info.components.b == VK_COMPONENT_SWIZZLE_IDENTITY &&
            view_info.components.a == VK_COMPONENT_SWIZZLE_IDENTITY &&
            view_info.subresourceRange.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT &&
            view_info.subresourceRange.baseMipLevel == 0 &&
            view_info.subresourceRange.levelCount == request.mDescription.mMipLevels &&
            view_info.subresourceRange.baseArrayLayer == 0 &&
            view_info.subresourceRange.layerCount == request.mDescription.mArrayLayers &&
            state.mTextureUploadDestinationDedicatedAllocationExact && state.mTextureUploadDestinationCreateCount == 1 &&
            state.mTextureUploadDestinationRequirementsCount == 1 && state.mTextureUploadDestinationAllocateCount == 1 &&
            state.mTextureUploadDestinationBindCount == 1 && state.mTextureUploadDestinationViewCreateCount == 1);

    const VkImage                                retained_texture_image  = instance->textureUploadDestinationImage();
    const VkDeviceMemory                         retained_texture_memory = instance->textureUploadDestinationMemory();
    const VkImageView                            retained_texture_view   = instance->textureUploadDestinationImageView();
    const LLRenderContract::TextureUploadFixture fixture                 = LLRenderContract::makeTextureUploadFixture();
    static_assert(sizeof(fixture.mSourceRGBA8) == VULKAN_TEXTURE_UPLOAD_SOURCE_BYTE_COUNT);
    const LLRenderContract::TextureUploadCase texture_upload_case = LLRenderContract::makeTextureUploadCase();
    const auto decoded_texture_upload = LLRenderContract::decodeStreamingUploadFrame(texture_upload_case.mFrame);
    ensure("the texture source starts from the exact decoded 144-byte diagnostic upload",
           decoded_texture_upload &&
               decoded_texture_upload->mHandles.mReplacementImage == LLRenderContract::StreamingUploadHandles{}.mReplacementImage &&
               decoded_texture_upload->mRevision == LLRenderContract::TEXTURE_UPLOAD_REVISION &&
               decoded_texture_upload->mExtent.mWidth == LLRenderContract::TEXTURE_UPLOAD_RESIDENT_WIDTH &&
               decoded_texture_upload->mExtent.mHeight == LLRenderContract::TEXTURE_UPLOAD_RESIDENT_HEIGHT &&
               decoded_texture_upload->mSourceFormat == LLRenderContract::PixelFormat::RGBA8Unorm &&
               decoded_texture_upload->mRowPitch == LLRenderContract::TEXTURE_UPLOAD_ROW_PITCH &&
               decoded_texture_upload->mRowOrigin == LLRenderContract::RowOrigin::TopLeft &&
               decoded_texture_upload->mPixels.size() == VULKAN_TEXTURE_UPLOAD_SOURCE_BYTE_COUNT &&
               std::equal(decoded_texture_upload->mPixels.begin(), decoded_texture_upload->mPixels.end(), fixture.mSourceRGBA8.begin()));
    VulkanTextureUploadSourceBytes source_bytes{};
    std::copy(decoded_texture_upload->mPixels.begin(), decoded_texture_upload->mPixels.end(), source_bytes.begin());
    const VulkanTextureUploadSourceDescription source_description = vulkanTextureUploadSourceDescription(source_bytes);
    VulkanTextureUploadSourceRequest           source_request;
    source_request.mNativeWindowGeneration = 224;
    source_request.mDescription            = source_description;
    source_request.mInstanceOwnerCheck     = { &context, uploadInstanceOwnerIsCurrent };
    source_request.mWindowGenerationCheck  = { &context, uploadWindowGenerationIsCurrent };

    ensure("the current Cocoa device acquires one distinct 144-byte texture upload source after its image destination",
           !instance->acquireTextureUploadSourceGeneration(source_request));
    const VkBufferCreateInfo& source_create_info = state.mTextureUploadSourceCreateInfo;
    ensure("texture source publishes the exact immutable top-left RGBA8 contract and noncoherent host allocation",
           instance->hasTextureUploadSourceGeneration() && instance->textureUploadSourceResourceHandle() == source_description.mHandle &&
               instance->textureUploadSourceExpectedRevision() == source_description.mExpectedRevision &&
               instance->textureUploadSourceResidentExtent().mWidth == LLRenderContract::TEXTURE_UPLOAD_RESIDENT_WIDTH &&
               instance->textureUploadSourceResidentExtent().mHeight == LLRenderContract::TEXTURE_UPLOAD_RESIDENT_HEIGHT &&
               instance->textureUploadSourcePixelFormat() == LLRenderContract::PixelFormat::RGBA8Unorm &&
               instance->textureUploadSourceRowPitch() == LLRenderContract::TEXTURE_UPLOAD_ROW_PITCH &&
               instance->textureUploadSourceRowOrigin() == LLRenderContract::RowOrigin::TopLeft &&
               instance->textureUploadSourceContentIdentity() == LLRenderContract::stableByteContentIdentity(fixture.mSourceRGBA8) &&
               instance->textureUploadSourceFlags() == 0 && instance->textureUploadSourceUsage() == VK_BUFFER_USAGE_TRANSFER_SRC_BIT &&
               instance->textureUploadSourceSharingMode() == VK_SHARING_MODE_EXCLUSIVE &&
               instance->textureUploadSourceBuffer() == state.mTextureUploadSourceBuffer &&
               instance->textureUploadSourceBuffer() != state.mUploadSourceBuffer &&
               instance->textureUploadSourceMemory() == state.mTextureUploadSourceMemory &&
               instance->textureUploadSourceMemory() != state.mUploadSourceMemory &&
               instance->textureUploadSourceByteCount() == VULKAN_TEXTURE_UPLOAD_SOURCE_BYTE_COUNT &&
               instance->textureUploadSourceAllocationSize() == state.mTextureUploadSourceMemoryRequirements.size &&
               instance->textureUploadSourceMemoryTypeIndex() == 0 &&
               instance->textureUploadSourceMemoryPropertyFlags() ==
                   (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) &&
               !instance->textureUploadSourceIsCoherent());
    ensure("texture source creates the exact exclusive transfer-source buffer and copies every padded source byte",
           source_create_info.sType == VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO && source_create_info.pNext == nullptr &&
               source_create_info.flags == 0 && source_create_info.size == VULKAN_TEXTURE_UPLOAD_SOURCE_BYTE_COUNT &&
               source_create_info.usage == VK_BUFFER_USAGE_TRANSFER_SRC_BIT &&
               source_create_info.sharingMode == VK_SHARING_MODE_EXCLUSIVE && source_create_info.queueFamilyIndexCount == 0 &&
               source_create_info.pQueueFamilyIndices == nullptr && state.mTextureUploadSourceMappedBytes == fixture.mSourceRGBA8);
    ensure("noncoherent texture-source creation maps, flushes, and unmaps exactly once before publication",
           state.mTextureUploadSourceMapCount == 1 && state.mTextureUploadSourceFlushCount == 1 &&
               state.mTextureUploadSourceUnmapCount == 1);
    ensure("texture-source acquisition preserves the exact destination, device, and 2x Cocoa geometry",
           instance->textureUploadDestinationImage() == retained_texture_image &&
               instance->textureUploadDestinationMemory() == retained_texture_memory &&
               instance->textureUploadDestinationImageView() == retained_texture_view && instance->hasLogicalDeviceGeneration() &&
               owner->hasNativeWindow() && owner->backingScale() == 2.0 && owner->drawableWidth() == 1280 &&
               owner->drawableHeight() == 720);

    const VulkanUploadSourceDescription vertex_source_description = vulkanScreenTriangleUploadSourceDescription();
    VulkanUploadSourceRequest           vertex_source_request;
    vertex_source_request.mNativeWindowGeneration = 224;
    vertex_source_request.mDescription            = vertex_source_description;
    vertex_source_request.mInstanceOwnerCheck     = { &context, uploadInstanceOwnerIsCurrent };
    vertex_source_request.mWindowGenerationCheck  = { &context, uploadWindowGenerationIsCurrent };
    ensure("the existing 48-byte upload source coexists with the texture source using distinct native storage",
           !instance->acquireUploadSourceGeneration(vertex_source_request) && instance->hasUploadSourceGeneration() &&
               instance->uploadSourceByteCount() == VULKAN_UPLOAD_SOURCE_BYTE_COUNT &&
               instance->textureUploadSourceByteCount() == VULKAN_TEXTURE_UPLOAD_SOURCE_BYTE_COUNT &&
               instance->uploadSourceBuffer() == state.mUploadSourceBuffer &&
               instance->textureUploadSourceBuffer() != instance->uploadSourceBuffer() &&
               instance->uploadSourceMemory() == state.mUploadSourceMemory &&
               instance->textureUploadSourceMemory() != instance->uploadSourceMemory() &&
               state.mUploadMappedBytes == LLRenderContract::SCREEN_TRIANGLE_BYTES &&
               state.mTextureUploadSourceMappedBytes == fixture.mSourceRGBA8);

    ensure("direct texture-source reset destroys its buffer before memory while preserving image and device parents",
           instance->resetTextureUploadSourceGeneration() && !instance->hasTextureUploadSourceGeneration() &&
               instance->textureUploadSourceResourceHandle() == LLRenderContract::ImageHandle{} &&
               instance->textureUploadSourceExpectedRevision() == 0 && instance->textureUploadSourceContentIdentity() == 0 &&
               instance->textureUploadSourceBuffer() == VK_NULL_HANDLE && instance->textureUploadSourceMemory() == VK_NULL_HANDLE &&
               instance->textureUploadSourceByteCount() == 0 && instance->textureUploadSourceAllocationSize() == 0 &&
               state.mTextureUploadSourceDestroyCount == 1 && state.mTextureUploadSourceFreeCount == 1 &&
               state.mTextureUploadSourceDestroyOrder < state.mTextureUploadSourceMemoryFreeOrder &&
               instance->textureUploadDestinationImage() == retained_texture_image &&
               instance->textureUploadDestinationMemory() == retained_texture_memory &&
               instance->textureUploadDestinationImageView() == retained_texture_view && instance->hasUploadSourceGeneration() &&
               instance->uploadSourceBuffer() == state.mUploadSourceBuffer && instance->uploadSourceMemory() == state.mUploadSourceMemory &&
               instance->hasLogicalDeviceGeneration());
    ensure("direct texture destination reset destroys the view before its image allocation without disturbing device parents",
           instance->resetTextureUploadDestinationGeneration() && !instance->hasTextureUploadDestinationGeneration() &&
               instance->textureUploadDestinationImage() == VK_NULL_HANDLE &&
               instance->textureUploadDestinationMemory() == VK_NULL_HANDLE &&
               instance->textureUploadDestinationImageView() == VK_NULL_HANDLE && state.mTextureUploadDestinationViewDestroyCount == 1 &&
               state.mTextureUploadDestinationDestroyCount == 1 && state.mTextureUploadDestinationFreeCount == 1 &&
               state.mTextureUploadDestinationViewDestroyOrder < state.mTextureUploadDestinationImageDestroyOrder &&
               state.mTextureUploadDestinationImageDestroyOrder < state.mTextureUploadDestinationMemoryFreeOrder &&
               instance->hasLogicalDeviceGeneration() && instance->hasPresentationDeviceGeneration() && instance->hasSurfaceGeneration());
    ensure("the retained device parents reacquire fresh texture destination and source ownership occurrences",
           !instance->acquireTextureUploadDestinationGeneration(request) &&
               !instance->acquireTextureUploadSourceGeneration(source_request) &&
               instance->textureUploadDestinationImage() == retained_texture_image &&
               instance->textureUploadDestinationMemory() == retained_texture_memory &&
               instance->textureUploadDestinationImageView() == retained_texture_view &&
               instance->textureUploadSourceBuffer() == state.mTextureUploadSourceBuffer &&
               instance->textureUploadSourceMemory() == state.mTextureUploadSourceMemory && state.mTextureUploadSourceMapCount == 2 &&
               state.mTextureUploadSourceFlushCount == 2 && state.mTextureUploadSourceUnmapCount == 2);
    ensure("texture destination fixture tears down its remaining live child and parent chain", owner->reset());
    ensure("parent-driven source, image, and logical-device teardown remains strictly child-first and balanced",
           state.mTextureUploadDestinationCreateCount == 2 && state.mTextureUploadDestinationAllocateCount == 2 &&
               state.mTextureUploadDestinationViewCreateCount == 2 && state.mTextureUploadDestinationViewDestroyCount == 2 &&
               state.mTextureUploadDestinationDestroyCount == 2 && state.mTextureUploadDestinationFreeCount == 2 &&
               state.mTextureUploadSourceDestroyCount == 2 && state.mTextureUploadSourceFreeCount == 2 &&
               state.mUploadSourceDestroyCount == 1 && state.mUploadSourceFreeCount == 1 &&
               state.mTextureUploadSourceDestroyOrder < state.mTextureUploadSourceMemoryFreeOrder &&
               state.mTextureUploadSourceMemoryFreeOrder < state.mTextureUploadDestinationViewDestroyOrder &&
               state.mTextureUploadDestinationViewDestroyOrder < state.mTextureUploadDestinationImageDestroyOrder &&
               state.mTextureUploadDestinationImageDestroyOrder < state.mTextureUploadDestinationMemoryFreeOrder &&
               state.mTextureUploadDestinationMemoryFreeOrder < state.mLogicalDeviceDestroyOrder);
}

template<>
template<>
void window_macosx_vulkan_object::test<25>()
{
    using namespace LLRenderVulkan;

    FakeState   state;
    ScopedState active(state);
    auto        result = acquireLLWindowMacOSXVulkan(createInfo(), 225, fakeOperations(state));
    auto*       owner  = acquiredWindow(result);
    ensure("texture transfer fixture acquires the complete Cocoa owner chain", owner && acquireCompleteSwapchainChain(*owner));

    auto* generation = const_cast<VulkanInstanceGeneration*>(owner->instanceGeneration());
    ensure("texture transfer fixture publishes its mutable aggregate", generation != nullptr);
    UploadOperationContext context{ owner, generation };

    const LLRenderContract::TextureUploadFixture      fixture                 = LLRenderContract::makeTextureUploadFixture();
    const VulkanTextureUploadSourceDescription        source_description      = vulkanTextureUploadSourceDescription(fixture.mSourceRGBA8);
    const VulkanTextureUploadDestinationDescription   destination_description = vulkanTextureUploadDestinationDescription();
    const VulkanTextureUploadSourceRequest            source_request{ generation->nativeWindowGeneration(),
                                                           source_description,
                                                                      { &context, uploadInstanceOwnerIsCurrent },
                                                                      { &context, uploadWindowGenerationIsCurrent } };
    const VulkanTextureUploadDestinationRequest       destination_request{ generation->nativeWindowGeneration(),
                                                                     destination_description,
                                                                           { &context, uploadInstanceOwnerIsCurrent },
                                                                           { &context, uploadWindowGenerationIsCurrent } };
    const VulkanTextureUploadTransferRequest          transfer_request{ generation->nativeWindowGeneration(),
                                                               source_description,
                                                               destination_description,
                                                                        { &context, uploadInstanceOwnerIsCurrent },
                                                                        { &context, uploadWindowGenerationIsCurrent } };
    const VulkanTextureUploadTransferOperationRequest operation_request{ generation->nativeWindowGeneration(),
                                                                         source_description,
                                                                         destination_description,
                                                                         1'000'000'000,
                                                                         { &context, uploadInstanceOwnerIsCurrent },
                                                                         { &context, uploadWindowGenerationIsCurrent } };

    state.mMemoryProperties.memoryTypes[0].propertyFlags =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    ensure("texture transfer fixture acquires its exact source and unpublished destination",
           !generation->acquireTextureUploadDestinationGeneration(destination_request) &&
               !generation->acquireTextureUploadSourceGeneration(source_request) && !generation->textureUploadDestinationIsResident());
    const VkBuffer      source_buffer     = generation->textureUploadSourceBuffer();
    const VkImage       destination_image = generation->textureUploadDestinationImage();
    const std::uint64_t content_identity  = generation->textureUploadSourceContentIdentity();

    ensure("the Cocoa aggregate acquires one texture upload transfer over those exact resources",
           !generation->acquireTextureUploadTransferGeneration(transfer_request) && generation->hasTextureUploadTransferGeneration() &&
               generation->textureUploadTransferResourceHandle() == destination_description.mHandle &&
               generation->textureUploadTransferExpectedRevision() == destination_description.mExpectedRevision &&
               generation->textureUploadTransferContentIdentity() == content_identity &&
               generation->textureUploadTransferSourceBuffer() == source_buffer &&
               generation->textureUploadTransferDestinationImage() == destination_image &&
               generation->textureUploadTransferQueue() == generation->presentationQueue() &&
               generation->textureUploadTransferQueueFamilyIndex() == generation->presentationQueueFamilyIndex() &&
               generation->textureUploadTransferQueueIndex() == generation->logicalDeviceQueueIndex() &&
               generation->textureUploadTransferCommandPool() != VK_NULL_HANDLE &&
               generation->textureUploadTransferCommandBuffer() != VK_NULL_HANDLE &&
               generation->textureUploadTransferFence() != VK_NULL_HANDLE &&
               generation->textureUploadTransferDisposition() == VulkanTextureUploadTransferDisposition::Ready);

    const auto  execution   = generation->executeTextureUploadTransfer(operation_request);
    const auto* disposition = std::get_if<VulkanTextureUploadTransferDisposition>(&execution);
    ensure("one completed texture transfer publishes exact shader-readable residency",
           disposition && *disposition == VulkanTextureUploadTransferDisposition::Complete &&
               generation->textureUploadTransferDisposition() == VulkanTextureUploadTransferDisposition::Complete &&
               generation->textureUploadTransferSubmissionCount() == 1 && generation->textureUploadTransferCompletionWaitCount() == 1 &&
               state.mTextureUploadCopyCalls == 1 && state.mTextureUploadBlitCalls == 2 && state.mTextureUploadImageBarrierCount == 5 &&
               generation->textureUploadDestinationIsResident() &&
               generation->textureUploadDestinationResidentRevision() == destination_description.mExpectedRevision &&
               generation->textureUploadDestinationResidentContentIdentity() == content_identity &&
               generation->textureUploadDestinationCurrentState() == LLRenderContract::ImageState::ShaderRead);

    state.mRefreshWidth         = 1600;
    state.mRefreshHeight        = 900;
    const auto  rebuild         = owner->rebuildSwapchainChain();
    const auto* rebuild_outcome = std::get_if<VulkanSwapchainChainRebuildOutcome>(&rebuild);
    ensure("changed-extent rebuild preserves the completed texture transfer, Cocoa geometry, and published destination",
           rebuild_outcome && *rebuild_outcome == VulkanSwapchainChainRebuildOutcome::Ready &&
               generation->hasTextureUploadTransferGeneration() && generation->textureUploadTransferSourceBuffer() == source_buffer &&
               generation->textureUploadTransferDestinationImage() == destination_image &&
               generation->textureUploadTransferDisposition() == VulkanTextureUploadTransferDisposition::Complete &&
               generation->textureUploadDestinationIsResident() &&
               generation->textureUploadDestinationResidentContentIdentity() == content_identity && owner->hasNativeWindow() &&
               owner->backingScale() == 2.0 && owner->drawableWidth() == 1600 && owner->drawableHeight() == 900);

    ensure("direct transfer reset preserves its resident destination, source, and rebuilt presentation chain",
           generation->resetTextureUploadTransferGeneration() && !generation->hasTextureUploadTransferGeneration() &&
               generation->hasTextureUploadSourceGeneration() && generation->textureUploadSourceBuffer() == source_buffer &&
               generation->hasTextureUploadDestinationGeneration() && generation->textureUploadDestinationImage() == destination_image &&
               generation->textureUploadDestinationIsResident() &&
               generation->textureUploadDestinationResidentContentIdentity() == content_identity &&
               generation->hasSwapchainFrameSlotGeneration());
    ensure("texture transfer fixture releases its retained resources and Cocoa owner",
           generation->resetTextureUploadSourceGeneration() && generation->resetTextureUploadDestinationGeneration() && owner->reset());
}

template<>
template<>
void window_macosx_vulkan_object::test<26>()
{
    using namespace LLRenderVulkan;

    FakeState   state;
    ScopedState active(state);
    auto        result = acquireLLWindowMacOSXVulkan(createInfo(), 226, fakeOperations(state));
    auto*       owner  = acquiredWindow(result);
    ensure("sampled texture binding fixture acquires the complete Cocoa owner chain", owner && acquireCompleteSwapchainChain(*owner));

    auto* generation = const_cast<VulkanInstanceGeneration*>(owner->instanceGeneration());
    ensure("sampled texture binding fixture publishes its mutable aggregate", generation != nullptr);
    UploadOperationContext context{ owner, generation };

    const LLRenderContract::TextureUploadFixture      fixture                 = LLRenderContract::makeTextureUploadFixture();
    const VulkanTextureUploadSourceDescription        source_description      = vulkanTextureUploadSourceDescription(fixture.mSourceRGBA8);
    const VulkanTextureUploadDestinationDescription   destination_description = vulkanTextureUploadDestinationDescription();
    const VulkanTextureUploadSampleBindingDescription binding_description     = vulkanTextureUploadSampleBindingDescription();
    const VulkanTextureUploadSamplePipelineDescription pipeline_description    = vulkanTextureUploadSamplePipelineDescription();
    const VulkanTextureUploadSourceRequest            source_request{ generation->nativeWindowGeneration(),
                                                           source_description,
                                                                      { &context, uploadInstanceOwnerIsCurrent },
                                                                      { &context, uploadWindowGenerationIsCurrent } };
    const VulkanTextureUploadDestinationRequest       destination_request{ generation->nativeWindowGeneration(),
                                                                     destination_description,
                                                                           { &context, uploadInstanceOwnerIsCurrent },
                                                                           { &context, uploadWindowGenerationIsCurrent } };
    const VulkanTextureUploadTransferRequest          transfer_request{ generation->nativeWindowGeneration(),
                                                               source_description,
                                                               destination_description,
                                                                        { &context, uploadInstanceOwnerIsCurrent },
                                                                        { &context, uploadWindowGenerationIsCurrent } };
    const VulkanTextureUploadTransferOperationRequest operation_request{ generation->nativeWindowGeneration(),
                                                                         source_description,
                                                                         destination_description,
                                                                         1'000'000'000,
                                                                         { &context, uploadInstanceOwnerIsCurrent },
                                                                         { &context, uploadWindowGenerationIsCurrent } };
    const VulkanTextureUploadSampleBindingRequest     binding_request{ generation->nativeWindowGeneration(),
                                                                   destination_description,
                                                                   binding_description,
                                                                       { &context, uploadInstanceOwnerIsCurrent },
                                                                       { &context, uploadWindowGenerationIsCurrent } };
    VulkanTextureUploadSamplePipelineRequest           pipeline_request;
    pipeline_request.mNativeWindowGeneration   = generation->nativeWindowGeneration();
    pipeline_request.mDrawableExtent           = generation->swapchainDrawableExtent();
    pipeline_request.mDestinationDescription   = destination_description;
    pipeline_request.mSampleBindingDescription = binding_description;
    pipeline_request.mDescription              = pipeline_description;
    pipeline_request.mInstanceOwnerCheck       = { &context, uploadInstanceOwnerIsCurrent };
    pipeline_request.mWindowGenerationCheck    = { &context, uploadWindowGenerationIsCurrent };

    state.mMemoryProperties.memoryTypes[0].propertyFlags =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    ensure("sampled texture binding fixture completes one exact texture upload",
           !generation->acquireTextureUploadDestinationGeneration(destination_request) &&
               !generation->acquireTextureUploadSourceGeneration(source_request) &&
               !generation->acquireTextureUploadTransferGeneration(transfer_request));
    const auto  execution   = generation->executeTextureUploadTransfer(operation_request);
    const auto* disposition = std::get_if<VulkanTextureUploadTransferDisposition>(&execution);
    ensure("sampled texture binding fixture starts from one shader-readable resident destination",
           disposition && *disposition == VulkanTextureUploadTransferDisposition::Complete &&
               generation->textureUploadDestinationIsResident() &&
               generation->textureUploadDestinationResidentRevision() == destination_description.mExpectedRevision &&
               generation->textureUploadDestinationResidentContentIdentity() != 0 &&
               generation->textureUploadDestinationCurrentState() == LLRenderContract::ImageState::ShaderRead &&
               state.mTextureUploadCopyCalls == 1 && state.mTextureUploadBlitCalls == 2 && state.mTextureUploadImageBarrierCount == 5);
    const std::uint64_t resident_identity = generation->textureUploadDestinationResidentContentIdentity();
    ensure("the completed resident destination keeps its terminal transfer and source live for pipeline lifetime checks",
           generation->hasTextureUploadTransferGeneration() && generation->hasTextureUploadSourceGeneration() &&
               generation->hasTextureUploadDestinationGeneration() && generation->textureUploadDestinationIsResident());

    const std::size_t shader_module_creates_before_binding        = state.mCreateShaderModuleCount;
    const std::size_t shader_module_destroys_before_binding       = state.mDestroyShaderModuleCount;
    const std::size_t presentation_layout_creates_before_binding  = state.mCreatePipelineLayoutCount;
    const std::size_t presentation_layout_destroys_before_binding = state.mDestroyPipelineLayoutCount;
    const std::size_t graphics_pipeline_creates_before_binding    = state.mCreatePipelineCount;
    const std::size_t graphics_pipeline_destroys_before_binding   = state.mDestroyPipelineCount;
    const std::size_t draw_count_before_binding                   = state.mDrawCount;
    const std::size_t queue_submits_before_binding                = state.mQueueSubmitCount;

    ensure("the Cocoa aggregate acquires the canonical sampled binding from the resident destination alone",
           !generation->acquireTextureUploadSampleBindingGeneration(binding_request));
    ensure("the sampled binding publishes its exact neutral identities and all five native handles",
           generation->hasTextureUploadSampleBindingGeneration() &&
               generation->textureUploadSampleBindingSamplerResourceHandle() == binding_description.mSampler.mHandle &&
               generation->textureUploadSampleBindingDestinationResourceHandle() == destination_description.mHandle &&
               generation->textureUploadSampleBindingExpectedRevision() == destination_description.mExpectedRevision &&
               generation->textureUploadSampleBindingResidentRevision() == destination_description.mExpectedRevision &&
               generation->textureUploadSampleBindingResidentContentIdentity() == resident_identity &&
               generation->textureUploadSampleBindingDestinationImageView() == state.mTextureUploadDestinationImageView &&
               generation->textureUploadSampleBindingDestinationImageLayout() == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
               generation->textureUploadSampleBindingDescriptorSetIndex() == 0 && generation->textureUploadSampleBindingBinding() == 0 &&
               generation->textureUploadSampleBindingSampler() == state.mTextureUploadSampleBindingSampler &&
               generation->textureUploadSampleBindingDescriptorSetLayout() == state.mTextureUploadSampleBindingDescriptorSetLayout &&
               generation->textureUploadSampleBindingPipelineLayout() == state.mTextureUploadSampleBindingPipelineLayout &&
               generation->textureUploadSampleBindingDescriptorPool() == state.mTextureUploadSampleBindingDescriptorPool &&
               generation->textureUploadSampleBindingDescriptorSet() == state.mTextureUploadSampleBindingDescriptorSet);

    const VkSamplerCreateInfo& sampler_info = state.mTextureUploadSampleBindingSamplerCreateInfo;
    ensure("Cocoa forwards the exact linear three-mip clamp sampler contract",
           sampler_info.sType == VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO && sampler_info.pNext == nullptr && sampler_info.flags == 0 &&
               sampler_info.magFilter == VK_FILTER_LINEAR && sampler_info.minFilter == VK_FILTER_LINEAR &&
               sampler_info.mipmapMode == VK_SAMPLER_MIPMAP_MODE_LINEAR &&
               sampler_info.addressModeU == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE &&
               sampler_info.addressModeV == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE &&
               sampler_info.addressModeW == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE && sampler_info.mipLodBias == 0.f &&
               sampler_info.anisotropyEnable == VK_FALSE && sampler_info.maxAnisotropy == 1.f && sampler_info.compareEnable == VK_FALSE &&
               sampler_info.compareOp == VK_COMPARE_OP_ALWAYS && sampler_info.minLod == 0.f && sampler_info.maxLod == 2.f &&
               sampler_info.borderColor == VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK && sampler_info.unnormalizedCoordinates == VK_FALSE);
    const VkDescriptorSetLayoutCreateInfo& set_layout_info      = state.mTextureUploadSampleBindingDescriptorSetLayoutCreateInfo;
    const VkDescriptorSetLayoutBinding&    set_binding          = state.mTextureUploadSampleBindingDescriptorSetLayoutBinding;
    const VkPipelineLayoutCreateInfo&      pipeline_layout_info = state.mTextureUploadSampleBindingPipelineLayoutCreateInfo;
    ensure("Cocoa forwards one fragment-visible combined image sampler at set zero binding zero",
           set_layout_info.sType == VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO && set_layout_info.pNext == nullptr &&
               set_layout_info.flags == 0 && set_layout_info.bindingCount == 1 && set_layout_info.pBindings == &set_binding &&
               set_binding.binding == 0 && set_binding.descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER &&
               set_binding.descriptorCount == 1 && set_binding.stageFlags == VK_SHADER_STAGE_FRAGMENT_BIT &&
               set_binding.pImmutableSamplers == nullptr && pipeline_layout_info.sType == VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO &&
               pipeline_layout_info.pNext == nullptr && pipeline_layout_info.flags == 0 && pipeline_layout_info.setLayoutCount == 1 &&
               pipeline_layout_info.pSetLayouts == &state.mTextureUploadSampleBindingPipelineSetLayout &&
               state.mTextureUploadSampleBindingPipelineSetLayout == state.mTextureUploadSampleBindingDescriptorSetLayout &&
               pipeline_layout_info.pushConstantRangeCount == 0 && pipeline_layout_info.pPushConstantRanges == nullptr);

    const VkDescriptorPoolCreateInfo&  pool_info        = state.mTextureUploadSampleBindingDescriptorPoolCreateInfo;
    const VkDescriptorSetAllocateInfo& allocate_info    = state.mTextureUploadSampleBindingDescriptorSetAllocateInfo;
    const VkWriteDescriptorSet&        descriptor_write = state.mTextureUploadSampleBindingDescriptorWrite;
    const VkDescriptorImageInfo&       image_info       = state.mTextureUploadSampleBindingDescriptorImageInfo;
    ensure("Cocoa allocates one pool-owned set and writes the exact resident image view once",
           pool_info.sType == VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO && pool_info.pNext == nullptr && pool_info.flags == 0 &&
               pool_info.maxSets == 1 && pool_info.poolSizeCount == 1 &&
               pool_info.pPoolSizes == &state.mTextureUploadSampleBindingDescriptorPoolSize &&
               state.mTextureUploadSampleBindingDescriptorPoolSize.type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER &&
               state.mTextureUploadSampleBindingDescriptorPoolSize.descriptorCount == 1 &&
               allocate_info.sType == VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO && allocate_info.pNext == nullptr &&
               allocate_info.descriptorPool == state.mTextureUploadSampleBindingDescriptorPool && allocate_info.descriptorSetCount == 1 &&
               allocate_info.pSetLayouts == &state.mTextureUploadSampleBindingAllocatedSetLayout &&
               state.mTextureUploadSampleBindingAllocatedSetLayout == state.mTextureUploadSampleBindingDescriptorSetLayout &&
               descriptor_write.sType == VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET && descriptor_write.pNext == nullptr &&
               descriptor_write.dstSet == state.mTextureUploadSampleBindingDescriptorSet && descriptor_write.dstBinding == 0 &&
               descriptor_write.dstArrayElement == 0 && descriptor_write.descriptorCount == 1 &&
               descriptor_write.descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER && descriptor_write.pImageInfo == &image_info &&
               descriptor_write.pBufferInfo == nullptr && descriptor_write.pTexelBufferView == nullptr &&
               image_info.sampler == state.mTextureUploadSampleBindingSampler &&
               image_info.imageView == state.mTextureUploadDestinationImageView &&
               image_info.imageLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    ensure("sampled binding creation performs one native occurrence without shader, graphics, draw, or submission work",
           state.mTextureUploadSampleBindingSamplerCreateCount == 1 &&
               state.mTextureUploadSampleBindingDescriptorSetLayoutCreateCount == 1 &&
               state.mTextureUploadSampleBindingPipelineLayoutCreateCount == 1 &&
               state.mTextureUploadSampleBindingDescriptorPoolCreateCount == 1 &&
               state.mTextureUploadSampleBindingDescriptorSetAllocateCount == 1 &&
               state.mTextureUploadSampleBindingDescriptorUpdateCount == 1 &&
               state.mCreateShaderModuleCount == shader_module_creates_before_binding &&
               state.mDestroyShaderModuleCount == shader_module_destroys_before_binding &&
               state.mCreatePipelineLayoutCount == presentation_layout_creates_before_binding &&
               state.mDestroyPipelineLayoutCount == presentation_layout_destroys_before_binding &&
               state.mCreatePipelineCount == graphics_pipeline_creates_before_binding &&
               state.mDestroyPipelineCount == graphics_pipeline_destroys_before_binding && state.mDrawCount == draw_count_before_binding &&
               state.mQueueSubmitCount == queue_submits_before_binding);

    const VkSampler             retained_sampler         = generation->textureUploadSampleBindingSampler();
    const VkDescriptorSetLayout retained_set_layout      = generation->textureUploadSampleBindingDescriptorSetLayout();
    const VkPipelineLayout      retained_pipeline_layout = generation->textureUploadSampleBindingPipelineLayout();
    const VkDescriptorPool      retained_pool            = generation->textureUploadSampleBindingDescriptorPool();
    const VkDescriptorSet       retained_set             = generation->textureUploadSampleBindingDescriptorSet();

    const VkRenderPass initial_sample_render_pass = generation->swapchainPresentationRenderPass();
    ensure("the Cocoa aggregate acquires one sampled pipeline against the exact borrowed binding layout",
           !generation->acquireTextureUploadSamplePipelineGeneration(pipeline_request) &&
               generation->hasTextureUploadSamplePipelineGeneration() &&
               generation->textureUploadSamplePipelineResourceHandle() == pipeline_description.mHandle &&
               generation->textureUploadSamplePipelineLayout() == retained_pipeline_layout &&
               generation->textureUploadSamplePipeline() == state.mTextureUploadSamplePipeline &&
               state.mTextureUploadSamplePipelineLayout == retained_pipeline_layout &&
               state.mTextureUploadSamplePipelineRenderPass == initial_sample_render_pass);
    ensure("sampled pipeline acquisition creates only two transient shader modules and one pipeline occurrence",
           state.mTextureUploadSamplePipelineCreateCount == 1 && state.mTextureUploadSamplePipelineDestroyCount == 0 &&
               state.mCreateShaderModuleCount == shader_module_creates_before_binding + 2 &&
               state.mDestroyShaderModuleCount == shader_module_destroys_before_binding + 2 &&
               state.mCreatePipelineLayoutCount == presentation_layout_creates_before_binding &&
               state.mDestroyPipelineLayoutCount == presentation_layout_destroys_before_binding &&
               state.mCreatePipelineCount == graphics_pipeline_creates_before_binding + 1 &&
               state.mDestroyPipelineCount == graphics_pipeline_destroys_before_binding && state.mDrawCount == draw_count_before_binding &&
               state.mQueueSubmitCount == queue_submits_before_binding);
    const VkPipeline initial_sample_pipeline = generation->textureUploadSamplePipeline();

    ensure("completed texture-transfer reset preserves the initial sampled pipeline and its borrowed layout",
           generation->resetTextureUploadTransferGeneration() && !generation->hasTextureUploadTransferGeneration() &&
               generation->hasTextureUploadSourceGeneration() && generation->hasTextureUploadSamplePipelineGeneration() &&
               generation->textureUploadSamplePipelineLayout() == retained_pipeline_layout &&
               generation->textureUploadSamplePipeline() == initial_sample_pipeline && state.mTextureUploadSamplePipelineDestroyCount == 0);
    ensure("texture-source reset preserves the initial sampled pipeline and resident destination",
           generation->resetTextureUploadSourceGeneration() && !generation->hasTextureUploadSourceGeneration() &&
               generation->hasTextureUploadSamplePipelineGeneration() &&
               generation->textureUploadSamplePipelineLayout() == retained_pipeline_layout &&
               generation->textureUploadSamplePipeline() == initial_sample_pipeline &&
               generation->hasTextureUploadDestinationGeneration() && generation->textureUploadDestinationIsResident() &&
               state.mTextureUploadSamplePipelineDestroyCount == 0);

    state.mRefreshWidth                                  = 1600;
    state.mRefreshHeight                                 = 900;
    const auto  rebuild                                  = owner->rebuildSwapchainChain();
    const auto* rebuild_outcome                          = std::get_if<VulkanSwapchainChainRebuildOutcome>(&rebuild);
    ensure("changed-extent rebuild retires the target-bound sampled pipeline while preserving the exact sampled binding",
           rebuild_outcome && *rebuild_outcome == VulkanSwapchainChainRebuildOutcome::Ready &&
               !generation->hasTextureUploadSamplePipelineGeneration() &&
               generation->textureUploadSamplePipelineResourceHandle() == LLRenderContract::PipelineHandle{} &&
               generation->textureUploadSamplePipelineLayout() == VK_NULL_HANDLE &&
               generation->textureUploadSamplePipeline() == VK_NULL_HANDLE &&
               generation->textureUploadSampleBindingSamplerResourceHandle() == binding_description.mSampler.mHandle &&
               generation->textureUploadSampleBindingDestinationResourceHandle() == destination_description.mHandle &&
               generation->textureUploadSampleBindingExpectedRevision() == destination_description.mExpectedRevision &&
               generation->textureUploadSampleBindingResidentRevision() == destination_description.mExpectedRevision &&
               generation->textureUploadSampleBindingResidentContentIdentity() == resident_identity &&
               generation->textureUploadSampleBindingDestinationImageView() == state.mTextureUploadDestinationImageView &&
               generation->textureUploadSampleBindingDestinationImageLayout() == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
               generation->textureUploadSampleBindingDescriptorSetIndex() == binding_description.mDescriptorSetIndex &&
               generation->textureUploadSampleBindingBinding() == binding_description.mBinding &&
               generation->textureUploadSampleBindingSampler() == retained_sampler &&
               generation->textureUploadSampleBindingDescriptorSetLayout() == retained_set_layout &&
               generation->textureUploadSampleBindingPipelineLayout() == retained_pipeline_layout &&
               generation->textureUploadSampleBindingDescriptorPool() == retained_pool &&
               generation->textureUploadSampleBindingDescriptorSet() == retained_set &&
               state.mTextureUploadSampleBindingSamplerCreateCount == 1 &&
               state.mTextureUploadSampleBindingDescriptorSetLayoutCreateCount == 1 &&
               state.mTextureUploadSampleBindingPipelineLayoutCreateCount == 1 &&
               state.mTextureUploadSampleBindingDescriptorPoolCreateCount == 1 &&
               state.mTextureUploadSampleBindingDescriptorSetAllocateCount == 1 &&
               state.mTextureUploadSampleBindingDescriptorUpdateCount == 1 && state.mTextureUploadSamplePipelineCreateCount == 1 &&
               state.mTextureUploadSamplePipelineDestroyCount == 1 && !generation->hasTextureUploadTransferGeneration() &&
               !generation->hasTextureUploadSourceGeneration() && owner->backingScale() == 2.0 && owner->drawableWidth() == 1600 &&
               owner->drawableHeight() == 900 &&
               state.mTextureUploadSamplePipelineDestroyOrder < state.mPresentationRenderPassDestroyOrder);

    pipeline_request.mDrawableExtent                      = generation->swapchainDrawableExtent();
    const std::size_t  shader_creates_before_reacquire    = state.mCreateShaderModuleCount;
    const std::size_t  shader_destroys_before_reacquire   = state.mDestroyShaderModuleCount;
    const std::size_t  pipeline_creates_before_reacquire  = state.mCreatePipelineCount;
    const std::size_t  pipeline_destroys_before_reacquire = state.mDestroyPipelineCount;
    const VkRenderPass rebuilt_sample_render_pass         = generation->swapchainPresentationRenderPass();
    ensure("the preserved binding explicitly acquires one sampled pipeline for the rebuilt target",
           !generation->acquireTextureUploadSamplePipelineGeneration(pipeline_request) &&
               generation->hasTextureUploadSamplePipelineGeneration() &&
               generation->textureUploadSamplePipelineResourceHandle() == pipeline_description.mHandle &&
               generation->textureUploadSamplePipelineLayout() == retained_pipeline_layout &&
               generation->textureUploadSamplePipeline() == state.mTextureUploadSamplePipeline &&
               generation->textureUploadSamplePipeline() != initial_sample_pipeline &&
               state.mTextureUploadSamplePipelineLayout == retained_pipeline_layout &&
               state.mTextureUploadSamplePipelineRenderPass == rebuilt_sample_render_pass &&
               state.mTextureUploadSamplePipelineCreateCount == 2 && state.mTextureUploadSamplePipelineDestroyCount == 1 &&
               state.mCreateShaderModuleCount == shader_creates_before_reacquire + 2 &&
               state.mDestroyShaderModuleCount == shader_destroys_before_reacquire + 2 &&
               state.mCreatePipelineCount == pipeline_creates_before_reacquire + 1 &&
               state.mDestroyPipelineCount == pipeline_destroys_before_reacquire && state.mDrawCount == draw_count_before_binding &&
               state.mQueueSubmitCount == queue_submits_before_binding);
    ensure("direct sampled pipeline reset clears only the pipeline before its borrowed binding and presentation target",
           generation->resetTextureUploadSamplePipelineGeneration() && !generation->hasTextureUploadSamplePipelineGeneration() &&
               generation->textureUploadSamplePipelineResourceHandle() == LLRenderContract::PipelineHandle{} &&
               generation->textureUploadSamplePipelineLayout() == VK_NULL_HANDLE &&
               generation->textureUploadSamplePipeline() == VK_NULL_HANDLE && generation->hasTextureUploadSampleBindingGeneration() &&
               generation->textureUploadSampleBindingPipelineLayout() == retained_pipeline_layout &&
               generation->hasSwapchainPresentationTargetGeneration() &&
               generation->swapchainPresentationRenderPass() == rebuilt_sample_render_pass &&
               state.mTextureUploadSamplePipelineDestroyCount == 2 && state.mDrawCount == draw_count_before_binding &&
               state.mQueueSubmitCount == queue_submits_before_binding);

    ensure("direct sampled binding reset clears its complete public state while preserving destination and frame slot",
           generation->resetTextureUploadSampleBindingGeneration() && !generation->hasTextureUploadSampleBindingGeneration() &&
               generation->textureUploadSampleBindingSamplerResourceHandle() == LLRenderContract::SamplerHandle{} &&
               generation->textureUploadSampleBindingDestinationResourceHandle() == LLRenderContract::ImageHandle{} &&
               generation->textureUploadSampleBindingExpectedRevision() == 0 &&
               generation->textureUploadSampleBindingResidentRevision() == 0 &&
               generation->textureUploadSampleBindingResidentContentIdentity() == 0 &&
               generation->textureUploadSampleBindingDestinationImageView() == VK_NULL_HANDLE &&
               generation->textureUploadSampleBindingDestinationImageLayout() == VK_IMAGE_LAYOUT_MAX_ENUM &&
               generation->textureUploadSampleBindingDescriptorSetIndex() == std::numeric_limits<std::uint32_t>::max() &&
               generation->textureUploadSampleBindingBinding() == std::numeric_limits<std::uint32_t>::max() &&
               generation->textureUploadSampleBindingSampler() == VK_NULL_HANDLE &&
               generation->textureUploadSampleBindingDescriptorSetLayout() == VK_NULL_HANDLE &&
               generation->textureUploadSampleBindingPipelineLayout() == VK_NULL_HANDLE &&
               generation->textureUploadSampleBindingDescriptorPool() == VK_NULL_HANDLE &&
               generation->textureUploadSampleBindingDescriptorSet() == VK_NULL_HANDLE &&
               generation->hasTextureUploadDestinationGeneration() && generation->textureUploadDestinationIsResident() &&
               generation->textureUploadDestinationResidentContentIdentity() == resident_identity &&
               generation->hasSwapchainFrameSlotGeneration() && state.mTextureUploadSampleBindingDescriptorPoolDestroyCount == 1 &&
               state.mTextureUploadSampleBindingPipelineLayoutDestroyCount == 1 &&
               state.mTextureUploadSampleBindingDescriptorSetLayoutDestroyCount == 1 &&
               state.mTextureUploadSampleBindingSamplerDestroyCount == 1);
    ensure("a second direct sampled binding reset succeeds without another native destruction",
           generation->resetTextureUploadSampleBindingGeneration() && state.mTextureUploadSampleBindingDescriptorPoolDestroyCount == 1 &&
               state.mTextureUploadSampleBindingPipelineLayoutDestroyCount == 1 &&
               state.mTextureUploadSampleBindingDescriptorSetLayoutDestroyCount == 1 &&
               state.mTextureUploadSampleBindingSamplerDestroyCount == 1);

    ensure("the retained destination reacquires one fresh sampled binding occurrence",
           !generation->acquireTextureUploadSampleBindingGeneration(binding_request) &&
               generation->hasTextureUploadSampleBindingGeneration() &&
               generation->textureUploadSampleBindingResidentContentIdentity() == resident_identity &&
               state.mTextureUploadSampleBindingSamplerCreateCount == 2 &&
               state.mTextureUploadSampleBindingDescriptorSetLayoutCreateCount == 2 &&
               state.mTextureUploadSampleBindingPipelineLayoutCreateCount == 2 &&
               state.mTextureUploadSampleBindingDescriptorPoolCreateCount == 2 &&
               state.mTextureUploadSampleBindingDescriptorSetAllocateCount == 2 &&
               state.mTextureUploadSampleBindingDescriptorUpdateCount == 2);
    ensure("the reacquired binding owns one sampled pipeline for full parent-teardown ordering",
           !generation->acquireTextureUploadSamplePipelineGeneration(pipeline_request) &&
               generation->hasTextureUploadSamplePipelineGeneration() &&
               generation->textureUploadSamplePipelineLayout() == generation->textureUploadSampleBindingPipelineLayout() &&
               generation->textureUploadSamplePipeline() == state.mTextureUploadSamplePipeline &&
               state.mTextureUploadSamplePipelineCreateCount == 3 && state.mTextureUploadSamplePipelineDestroyCount == 2);
    ensure("sampled pipeline fixture tears down its complete Cocoa owner chain", owner->reset());
    ensure("parent teardown retires the sampled pipeline before its binding, destination, and device",
           state.mTextureUploadSamplePipelineDestroyCount == 3 && state.mTextureUploadSampleBindingDescriptorPoolDestroyCount == 2 &&
               state.mTextureUploadSampleBindingPipelineLayoutDestroyCount == 2 &&
               state.mTextureUploadSampleBindingDescriptorSetLayoutDestroyCount == 2 &&
               state.mTextureUploadSampleBindingSamplerDestroyCount == 2 &&
               state.mTextureUploadSamplePipelineDestroyOrder < state.mTextureUploadSampleBindingDescriptorPoolDestroyOrder &&
               state.mTextureUploadSamplePipelineDestroyOrder < state.mPresentationRenderPassDestroyOrder &&
               state.mTextureUploadSampleBindingDescriptorPoolDestroyOrder < state.mTextureUploadSampleBindingPipelineLayoutDestroyOrder &&
               state.mTextureUploadSampleBindingPipelineLayoutDestroyOrder <
                   state.mTextureUploadSampleBindingDescriptorSetLayoutDestroyOrder &&
               state.mTextureUploadSampleBindingDescriptorSetLayoutDestroyOrder < state.mTextureUploadSampleBindingSamplerDestroyOrder &&
               state.mTextureUploadSampleBindingSamplerDestroyOrder < state.mTextureUploadDestinationViewDestroyOrder &&
               state.mTextureUploadDestinationViewDestroyOrder < state.mTextureUploadDestinationImageDestroyOrder &&
               state.mTextureUploadDestinationImageDestroyOrder < state.mTextureUploadDestinationMemoryFreeOrder &&
               state.mTextureUploadDestinationMemoryFreeOrder < state.mLogicalDeviceDestroyOrder);
}

} // namespace tut
