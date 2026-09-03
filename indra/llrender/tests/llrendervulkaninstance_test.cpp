/**
 * @file llrendervulkaninstance_test.cpp
 * @brief Tests for owned Vulkan instance generations.
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

#include "llrendervulkaninstance.h"
#include "lltut.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace
{
using namespace LLRenderVulkan;

enum class MissingCommand : std::uint8_t
{
    None,
    GlobalCreateInstance,
    GlobalEnumerateExtensions,
    GlobalEnumerateLayers,
    GlobalEnumerateVersion,
    DestroyInstance,
    CreateDebugMessenger,
    DestroyDebugMessenger,
    DestroySurface,
    GetPhysicalDeviceFeatures2,
    GetPhysicalDeviceFeatures,
    CreateDevice,
    DestroyDevice,
    GetDeviceQueue,
    GetSurfaceCapabilities,
    GetSurfaceFormats,
    GetSurfacePresentModes,
    GetDeviceProcAddr,
    CreateSwapchain,
    DestroySwapchain,
    GetSwapchainImages,
    CreateImageView,
    DestroyImageView,
    CreateCommandPool,
    DestroyCommandPool,
    AllocateCommandBuffers,
    CreateSemaphore,
    DestroySemaphore,
    CreateFence,
    DestroyFence,
    WaitForFences,
    ResetCommandBuffer,
    BeginCommandBuffer,
    EndCommandBuffer,
    ResetFences,
    QueueSubmit,
    AcquireNextImage,
    CmdPipelineBarrier,
    QueuePresent,
    ReleaseSwapchainImages,
    GetPhysicalDeviceFormatProperties,
    CmdClearColorImage,
    CreateRenderPass,
    DestroyRenderPass,
    CreateFramebuffer,
    DestroyFramebuffer,
    CmdBeginRenderPass,
    CmdEndRenderPass,
    CreateShaderModule,
    DestroyShaderModule,
    CreatePipelineLayout,
    DestroyPipelineLayout,
    CreateGraphicsPipelines,
    DestroyPipeline,
    CmdBindPipeline,
    CmdSetViewport,
    CmdSetScissor,
    CmdDraw,
    GetPhysicalDeviceMemoryProperties,
    CreateBuffer,
    DestroyBuffer,
    GetBufferMemoryRequirements,
    AllocateMemory,
    FreeMemory,
    BindBufferMemory,
    MapMemory,
    UnmapMemory,
    FlushMappedMemoryRanges,
    CmdCopyImageToBuffer,
    CmdCopyBuffer
};

enum class Event : std::uint8_t
{
    CreateInstance,
    CreateDebugMessenger,
    CreateSurface,
    CreateDevice,
    GetDeviceQueue,
    CreateSwapchain,
    CreateImageView,
    CreateCommandPool,
    AllocateCommandBuffer,
    CreateSemaphore,
    CreateFence,
    WaitForFences,
    ResetCommandBuffer,
    BeginCommandBuffer,
    EndCommandBuffer,
    ResetFences,
    QueueSubmit,
    DestroyFence,
    DestroySemaphore,
    DestroyCommandPool,
    DestroyImageView,
    DestroySwapchain,
    DestroyDevice,
    DestroySurface,
    DestroyDebugMessenger,
    DestroyInstance,
    CreateRenderPass,
    CreateFramebuffer,
    DestroyFramebuffer,
    DestroyRenderPass,
    BeginRenderPass,
    EndRenderPass,
    CreateShaderModule,
    DestroyShaderModule,
    CreatePipelineLayout,
    DestroyPipelineLayout,
    CreateGraphicsPipeline,
    DestroyPipeline,
    BindPipeline,
    SetViewport,
    SetScissor,
    Draw,
    CreateBuffer,
    AllocateMemory,
    MapMemory,
    UnmapMemory,
    DestroyBuffer,
    FreeMemory,
    FlushMappedMemoryRanges,
    DestroyUploadSourceBuffer,
    FreeUploadSourceMemory,
    DestroyUploadDestinationBuffer,
    FreeUploadDestinationMemory,
    DestroyUploadTransferFence,
    DestroyUploadTransferCommandPool,
    CopyBuffer
};

enum class ReadbackResetReentryPoint : std::uint8_t
{
    None,
    WaitForFences,
    AcquireNextImage
};

enum class BufferMemoryKind : std::uint8_t
{
    Readback,
    UploadSource,
    UploadDestination
};

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

struct FakeState
{
    std::vector<std::string> mExtensions{ VK_KHR_SURFACE_EXTENSION_NAME };
    std::vector<std::string> mLayers;

    MissingCommand mMissing       = MissingCommand::None;
    VkResult       mVersionResult = VK_SUCCESS;
    std::uint32_t  mVersion       = VK_API_VERSION_1_2;

    VkResult      mExtensionCountResult          = VK_SUCCESS;
    VkResult      mExtensionValuesResult         = VK_SUCCESS;
    std::uint32_t mExtensionCountOverride        = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t mExtensionWrittenOverride      = std::numeric_limits<std::uint32_t>::max();
    std::size_t   mIncompleteExtensionCountCalls = 0;
    std::size_t   mIncompleteExtensionValueCalls = 0;
    bool          mMalformedExtension            = false;
    bool          mAlwaysIncompleteExtensions    = false;

    VkResult      mLayerCountResult   = VK_SUCCESS;
    VkResult      mLayerValuesResult  = VK_SUCCESS;
    std::uint32_t mLayerCountOverride = std::numeric_limits<std::uint32_t>::max();
    bool          mMalformedLayer     = false;

    VkResult mInstanceResult     = VK_SUCCESS;
    bool     mNullInstance       = false;
    VkResult mDebugResult        = VK_SUCCESS;
    bool     mNullDebugMessenger = false;

    bool     mSurfacePlatformFailure = false;
    VkResult mSurfaceResult          = VK_SUCCESS;
    bool     mNullSurface            = false;
    bool     mPoisonSurfaceOutput    = false;

    VkResult                        mPhysicalEnumerationResult = VK_SUCCESS;
    VkPhysicalDevice                mPhysicalDevice            = fakeHandle<VkPhysicalDevice>(0x4444);
    VkPhysicalDeviceProperties      mPhysicalDeviceProperties{};
    VkQueueFamilyProperties         mQueueFamilyProperties{};
    std::vector<std::string>        mDeviceExtensions{ VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME };
    VkBool32                        mPresentationSupported = VK_TRUE;
    VkPhysicalDeviceFeatures        mSupportedFeatures{};
    VkBool32                        mSwapchainMaintenance1Supported = VK_TRUE;
    VkResult                        mSwapchainCapabilitiesResult = VK_SUCCESS;
    VkSurfaceCapabilitiesKHR        mSwapchainCapabilities{};
    std::vector<VkSurfaceFormatKHR> mSwapchainFormats{ { VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR } };
    std::vector<VkPresentModeKHR>   mSwapchainPresentModes{ VK_PRESENT_MODE_FIFO_KHR };
    VkFormatProperties              mSwapchainFormatProperties{};

    VkInstance               mInstance                = fakeHandle<VkInstance>(0x1111);
    VkDebugUtilsMessengerEXT mDebugMessenger          = fakeHandle<VkDebugUtilsMessengerEXT>(0x2222);
    VkSurfaceKHR             mSurface                 = fakeHandle<VkSurfaceKHR>(0x3333);
    VkDevice                 mDevice                  = fakeHandle<VkDevice>(0x5555);
    VkQueue                  mQueue                   = fakeHandle<VkQueue>(0x6666);
    VkSwapchainKHR           mSwapchain               = fakeHandle<VkSwapchainKHR>(0x7777);
    VkCommandPool            mCommandPool             = fakeHandle<VkCommandPool>(0xa001);
    VkCommandBuffer          mCommandBuffer           = fakeHandle<VkCommandBuffer>(0xa002);
    VkSemaphore              mImageAvailableSemaphore = fakeHandle<VkSemaphore>(0xa003);
    VkFence                  mSubmissionFence         = fakeHandle<VkFence>(0xa004);
    VkSemaphore              mPresentationReadySemaphore = fakeHandle<VkSemaphore>(0xa005);
    VkFence                  mPresentCompletionFence     = fakeHandle<VkFence>(0xa006);
    std::vector<VkImage>     mSwapchainImages{ fakeHandle<VkImage>(0x8001), fakeHandle<VkImage>(0x8002), fakeHandle<VkImage>(0x8003) };
    std::vector<VkImageView>      mSwapchainImageViews{ fakeHandle<VkImageView>(0x9001), fakeHandle<VkImageView>(0x9002),
                                                   fakeHandle<VkImageView>(0x9003) };
    VkRenderPass                  mPresentationRenderPass = fakeHandle<VkRenderPass>(0xb001);
    std::vector<VkFramebuffer>    mPresentationFramebuffers{ fakeHandle<VkFramebuffer>(0xb101), fakeHandle<VkFramebuffer>(0xb102),
                                                          fakeHandle<VkFramebuffer>(0xb103) };
    std::array<VkShaderModule, 2> mPresentationShaderModules{ fakeHandle<VkShaderModule>(0xc001), fakeHandle<VkShaderModule>(0xc002) };
    VkPipelineLayout              mPresentationPipelineLayout = fakeHandle<VkPipelineLayout>(0xc101);
    VkPipeline                    mPresentationPipeline       = fakeHandle<VkPipeline>(0xc201);
    VkBuffer                      mReadbackBuffer             = fakeHandle<VkBuffer>(0xd001);
    VkDeviceMemory                mReadbackMemory             = fakeHandle<VkDeviceMemory>(0xd002);
    VkBuffer                      mUploadSourceBuffer         = fakeHandle<VkBuffer>(0xd101);
    VkDeviceMemory                mUploadSourceMemory         = fakeHandle<VkDeviceMemory>(0xd102);
    VkBuffer                      mUploadDestinationBuffer     = fakeHandle<VkBuffer>(0xd201);
    VkDeviceMemory                mUploadDestinationMemory     = fakeHandle<VkDeviceMemory>(0xd202);
    VkCommandPool                 mUploadTransferCommandPool   = fakeHandle<VkCommandPool>(0xe001);
    VkCommandBuffer               mUploadTransferCommandBuffer = fakeHandle<VkCommandBuffer>(0xe002);
    VkFence                       mUploadTransferFence         = fakeHandle<VkFence>(0xe003);
    std::array<std::byte, 4>      mReadbackMappedStorage{};
    VulkanUploadSourceBytes       mUploadSourceMappedStorage{};
    std::vector<std::byte>        mReadbackObservationStorage;
    std::vector<Event>            mEvents;
    std::vector<std::string>      mEnabledExtensions;
    std::vector<std::string>      mEnabledLayers;
    VkInstanceCreateFlags    mInstanceFlags                  = 0;
    std::uint32_t            mRequestedApiVersion            = 0;
    PFN_vkDebugUtilsMessengerCallbackEXT mValidationCallback = nullptr;
    void*                                mValidationUserdata = nullptr;

    std::size_t mVersionCalls         = 0;
    std::size_t mExtensionCountCalls  = 0;
    std::size_t mExtensionValuesCalls = 0;
    std::size_t mLayerCountCalls      = 0;
    std::size_t mLayerValuesCalls     = 0;
    std::size_t mDestroyInstanceCalls = 0;
    std::size_t mDestroyDebugCalls    = 0;

    std::size_t                     mDestroySurfaceResolutionCalls           = 0;
    VkInstance                      mDestroySurfaceResolutionInstance        = VK_NULL_HANDLE;
    std::size_t                     mCreateSurfaceCalls                      = 0;
    VkInstance                      mCreateSurfaceInstance                   = VK_NULL_HANDLE;
    const VkAllocationCallbacks*    mCreateSurfaceAllocationCallbacks        = nullptr;
    std::size_t                     mDestroySurfaceCalls                     = 0;
    VkInstance                      mDestroySurfaceInstance                  = VK_NULL_HANDLE;
    VkSurfaceKHR                    mDestroyedSurface                        = VK_NULL_HANDLE;
    const VkAllocationCallbacks*    mDestroySurfaceAllocationCallbacks       = nullptr;
    const VulkanInstanceGeneration* mSurfaceDestroyOwner                     = nullptr;
    bool                            mSurfaceDestroyObservationMade           = false;
    bool                            mObservedPresentationAtSurfaceDestroy    = false;
    bool                            mObservedLogicalAtSurfaceDestroy         = false;
    bool                            mObservedConfigurationAtSurfaceDestroy   = false;
    bool                            mObservedSwapchainAtSurfaceDestroy       = false;
    bool                            mObservedSwapchainImagesAtSurfaceDestroy = false;
    bool                            mObservedFrameSlotAtSurfaceDestroy       = false;

    std::size_t      mPhysicalCountCalls         = 0;
    std::size_t      mPhysicalListCalls          = 0;
    std::size_t      mPhysicalPropertiesCalls    = 0;
    std::size_t      mQueueFamilyCountCalls      = 0;
    std::size_t      mQueueFamilyListCalls       = 0;
    std::size_t      mSurfaceSupportCalls        = 0;
    std::size_t      mDeviceExtensionCountCalls  = 0;
    std::size_t      mDeviceExtensionValuesCalls = 0;
    VkPhysicalDevice mLastSurfaceSupportDevice   = VK_NULL_HANDLE;
    std::uint32_t    mLastSurfaceSupportQueue    = VK_QUEUE_FAMILY_IGNORED;
    VkSurfaceKHR     mLastSurfaceSupportSurface  = VK_NULL_HANDLE;

    std::size_t                     mPhysicalFeaturesCalls                  = 0;
    VkPhysicalDevice                mPhysicalFeaturesDevice                 = VK_NULL_HANDLE;
    std::size_t                     mCreateDeviceCalls                      = 0;
    std::size_t                     mDestroyDeviceCalls                     = 0;
    std::size_t                     mGetDeviceQueueCalls                    = 0;
    VkDevice                        mGetDeviceQueueDevice                   = VK_NULL_HANDLE;
    std::uint32_t                   mGetDeviceQueueFamily                   = VK_QUEUE_FAMILY_IGNORED;
    std::uint32_t                   mGetDeviceQueueIndex                    = std::numeric_limits<std::uint32_t>::max();
    const VulkanInstanceGeneration* mDeviceDestroyOwner                     = nullptr;
    bool                            mDeviceDestroyObservationMade           = false;
    bool                            mObservedPresentationAtDeviceDestroy    = false;
    bool                            mObservedSurfaceAtDeviceDestroy         = false;
    bool                            mObservedConfigurationAtDeviceDestroy   = false;
    bool                            mObservedSwapchainAtDeviceDestroy       = false;
    bool                            mObservedSwapchainImagesAtDeviceDestroy = false;
    bool                            mObservedFrameSlotAtDeviceDestroy       = false;
    bool                            mObservedUploadSourceAtDeviceDestroy    = false;
    bool                            mObservedUploadDestinationAtDeviceDestroy = false;
    bool                            mObservedUploadTransferAtDeviceDestroy    = false;
    std::size_t                     mSwapchainCapabilitiesCalls             = 0;
    std::size_t                     mSwapchainFormatCountCalls              = 0;
    std::size_t                     mSwapchainFormatListCalls               = 0;
    std::size_t                     mSwapchainPresentModeCountCalls         = 0;
    std::size_t                     mSwapchainPresentModeListCalls          = 0;
    std::size_t                     mSwapchainFormatPropertiesCalls         = 0;

    VkResult                        mSwapchainCreateResult               = VK_SUCCESS;
    bool                            mNullSwapchain                       = false;
    bool                            mPoisonSwapchainOutput               = false;
    std::size_t                     mGetDeviceProcAddrResolutionCalls    = 0;
    VkInstance                      mGetDeviceProcAddrResolutionInstance = VK_NULL_HANDLE;
    std::size_t                     mDeviceProcAddrCalls                 = 0;
    VkDevice                        mDeviceProcAddrDevice                = VK_NULL_HANDLE;
    std::vector<std::string>        mDeviceCommandLookups;
    std::size_t                     mCreateSwapchainCalls  = 0;
    VkDevice                        mCreateSwapchainDevice = VK_NULL_HANDLE;
    VkSwapchainCreateInfoKHR        mSwapchainCreateInfo{};
    const VkAllocationCallbacks*    mCreateSwapchainAllocationCallbacks        = nullptr;
    std::size_t                     mDestroySwapchainCalls                     = 0;
    VkDevice                        mDestroySwapchainDevice                    = VK_NULL_HANDLE;
    VkSwapchainKHR                  mDestroyedSwapchain                        = VK_NULL_HANDLE;
    std::vector<VkSwapchainKHR>     mDestroyedSwapchains;
    const VkAllocationCallbacks*    mDestroySwapchainAllocationCallbacks       = nullptr;
    const VulkanInstanceGeneration* mSwapchainDestroyOwner                     = nullptr;
    bool                            mSwapchainDestroyObservationMade           = false;
    bool                            mObservedConfigurationAtSwapchainDestroy   = false;
    bool                            mObservedLogicalAtSwapchainDestroy         = false;
    bool                            mObservedSurfaceAtSwapchainDestroy         = false;
    bool                            mObservedSwapchainImagesAtSwapchainDestroy = false;
    bool                            mObservedFrameSlotAtSwapchainDestroy       = false;

    VkResult                                  mSwapchainImageCountResult = VK_SUCCESS;
    VkResult                                  mSwapchainImageListResult  = VK_SUCCESS;
    std::size_t                               mSwapchainImageCountCalls  = 0;
    std::size_t                               mSwapchainImageListCalls   = 0;
    VkDevice                                  mSwapchainImagesDevice     = VK_NULL_HANDLE;
    VkSwapchainKHR                            mEnumeratedSwapchain       = VK_NULL_HANDLE;
    VkResult                                  mImageViewCreateResult     = VK_SUCCESS;
    bool                                      mNullImageView             = false;
    std::size_t                               mCreateImageViewCalls      = 0;
    VkDevice                                  mCreateImageViewDevice     = VK_NULL_HANDLE;
    std::vector<VkImageViewCreateInfo>        mImageViewCreateInfos;
    std::vector<const VkAllocationCallbacks*> mCreateImageViewAllocationCallbacks;
    std::size_t                               mDestroyImageViewCalls  = 0;
    VkDevice                                  mDestroyImageViewDevice = VK_NULL_HANDLE;
    std::vector<VkImageView>                  mDestroyedImageViews;
    std::vector<const VkAllocationCallbacks*> mDestroyImageViewAllocationCallbacks;
    const VulkanInstanceGeneration*           mImageViewDestroyOwner                   = nullptr;
    bool                                      mImageViewDestroyObservationMade         = false;
    bool                                      mObservedSwapchainAtImageViewDestroy     = false;
    bool                                      mObservedConfigurationAtImageViewDestroy = false;
    bool                                      mObservedLogicalAtImageViewDestroy       = false;
    bool                                      mObservedSurfaceAtImageViewDestroy       = false;
    bool                                      mObservedPresentationTargetAtImageViewDestroy = false;
    bool                                      mObservedFrameSlotAtImageViewDestroy          = false;

    VkPhysicalDeviceMemoryProperties mMemoryProperties{};
    VkMemoryRequirements             mReadbackMemoryRequirements{};
    VkMemoryRequirements             mUploadSourceMemoryRequirements{};
    VkMemoryRequirements             mUploadDestinationMemoryRequirements{};
    VkResult                         mBufferCreateResult               = VK_SUCCESS;
    VkResult                         mMemoryAllocateResult             = VK_SUCCESS;
    VkResult                         mBufferBindResult                 = VK_SUCCESS;
    VkResult                         mMemoryMapResult                  = VK_SUCCESS;
    VkResult                         mMemoryFlushResult                = VK_SUCCESS;
    bool                             mNullReadbackBuffer               = false;
    bool                             mNullReadbackMemory               = false;
    bool                             mNullReadbackMapping              = false;
    bool                             mNullUploadSourceBuffer           = false;
    bool                             mNullUploadSourceMemory           = false;
    bool                             mNullUploadSourceMapping          = false;
    bool                             mNullUploadDestinationBuffer      = false;
    bool                             mNullUploadDestinationMemory      = false;
    BufferMemoryKind                 mNextBufferMemoryKind             = BufferMemoryKind::Readback;
    std::size_t                      mMemoryPropertiesCalls            = 0;
    std::size_t                      mCreateBufferCalls                = 0;
    std::size_t                      mDestroyBufferCalls               = 0;
    std::size_t                      mGetBufferMemoryRequirementsCalls = 0;
    std::size_t                      mAllocateMemoryCalls              = 0;
    std::size_t                      mFreeMemoryCalls                  = 0;
    std::size_t                      mBindBufferMemoryCalls            = 0;
    std::size_t                      mMapMemoryCalls                   = 0;
    std::size_t                      mUnmapMemoryCalls                 = 0;
    std::size_t                      mFlushMappedMemoryRangesCalls     = 0;
    VkBufferCreateInfo               mReadbackBufferCreateInfo{};
    VkBufferCreateInfo               mUploadSourceBufferCreateInfo{};
    VkBufferCreateInfo               mUploadDestinationBufferCreateInfo{};
    VkMemoryAllocateInfo             mReadbackMemoryAllocateInfo{};
    VkMemoryAllocateInfo             mUploadSourceMemoryAllocateInfo{};
    VkMemoryAllocateInfo             mUploadDestinationMemoryAllocateInfo{};
    VkBuffer                         mReadbackBoundBuffer = VK_NULL_HANDLE;
    VkDeviceMemory                   mReadbackBoundMemory = VK_NULL_HANDLE;
    VkDeviceSize                     mReadbackBindOffset  = std::numeric_limits<VkDeviceSize>::max();
    VkBuffer                         mUploadSourceBoundBuffer = VK_NULL_HANDLE;
    VkDeviceMemory                   mUploadSourceBoundMemory = VK_NULL_HANDLE;
    VkDeviceSize                     mUploadSourceBindOffset  = std::numeric_limits<VkDeviceSize>::max();
    VkBuffer                         mUploadDestinationBoundBuffer = VK_NULL_HANDLE;
    VkDeviceMemory                   mUploadDestinationBoundMemory = VK_NULL_HANDLE;
    VkDeviceSize                     mUploadDestinationBindOffset  = std::numeric_limits<VkDeviceSize>::max();
    VkDeviceMemory                   mMappedMemory        = VK_NULL_HANDLE;
    VkDeviceSize                     mMappedOffset        = std::numeric_limits<VkDeviceSize>::max();
    VkDeviceSize                     mMappedSize          = 0;
    std::vector<VkBuffer>            mDestroyedBuffers;
    std::vector<VkDeviceMemory>      mFreedMemories;
    std::vector<VkMappedMemoryRange> mFlushedMemoryRanges;
    std::vector<VkBuffer>            mCreatedBuffers;
    std::vector<VkDeviceMemory>      mAllocatedMemories;

    VkResult                        mRenderPassCreateResult           = VK_SUCCESS;
    VkResult                        mFramebufferCreateResult          = VK_SUCCESS;
    bool                            mNullRenderPass                   = false;
    bool                                      mNullFramebuffer = false;
    std::size_t                               mCreateRenderPassCalls = 0;
    std::size_t                               mDestroyRenderPassCalls = 0;
    std::size_t                               mCreateFramebufferCalls = 0;
    std::size_t                               mFramebufferIndexWithinRenderPass = 0;
    std::size_t                               mDestroyFramebufferCalls = 0;
    VkDevice                                  mPresentationTargetDevice = VK_NULL_HANDLE;
    std::vector<VkImageView>                  mFramebufferAttachments;
    std::vector<VkExtent2D>                   mFramebufferExtents;
    std::vector<VkFramebuffer>                mDestroyedFramebuffers;
    VkRenderPass                              mDestroyedRenderPass = VK_NULL_HANDLE;
    const VulkanInstanceGeneration*           mPresentationTargetDestroyOwner = nullptr;
    bool                                      mPresentationTargetDestroyObservationMade = false;
    bool                                      mObservedImagesAtPresentationTargetDestroy = false;
    bool                                      mObservedFrameSlotAtPresentationTargetDestroy = false;

    VkResult                                  mShaderModuleCreateResult = VK_SUCCESS;
    VkResult                                  mPipelineLayoutCreateResult = VK_SUCCESS;
    VkResult                                  mGraphicsPipelineCreateResult = VK_SUCCESS;
    bool                                      mNullShaderModule = false;
    bool                                      mNullPipelineLayout = false;
    bool                                      mNullGraphicsPipeline = false;
    std::size_t                               mCreateShaderModuleCalls = 0;
    std::size_t                               mDestroyShaderModuleCalls = 0;
    std::size_t                               mCreatePipelineLayoutCalls = 0;
    std::size_t                               mDestroyPipelineLayoutCalls = 0;
    std::size_t                               mCreateGraphicsPipelineCalls = 0;
    std::size_t                               mDestroyPipelineCalls = 0;
    std::vector<VkShaderModule>               mDestroyedShaderModules;
    std::vector<VkPipelineLayout>             mDestroyedPipelineLayouts;
    std::vector<VkPipeline>                   mDestroyedPipelines;
    const VulkanInstanceGeneration*           mPipelineDestroyOwner = nullptr;
    bool                                      mPipelineDestroyObservationMade = false;
    bool                                      mObservedTargetAtPipelineDestroy = false;
    bool                                      mObservedFrameSlotAtPipelineDestroy = false;

    VkResult                        mCommandPoolCreateResult     = VK_SUCCESS;
    VkResult                        mCommandBufferAllocateResult = VK_SUCCESS;
    VkResult                        mSemaphoreCreateResult       = VK_SUCCESS;
    VkResult                        mFenceCreateResult           = VK_SUCCESS;
    bool                            mNullCommandPool             = false;
    bool                            mNullCommandBuffer           = false;
    bool                            mNullSemaphore               = false;
    bool                            mNullFence                   = false;
    std::size_t                     mCreateCommandPoolCalls      = 0;
    std::size_t                     mDestroyCommandPoolCalls     = 0;
    std::size_t                     mAllocateCommandBufferCalls  = 0;
    std::size_t                     mCreateSemaphoreCalls        = 0;
    std::size_t                     mDestroySemaphoreCalls       = 0;
    std::size_t                     mCreateFenceCalls            = 0;
    std::size_t                     mCreateFrameSlotFenceCalls           = 0;
    std::size_t                     mDestroyFenceCalls           = 0;
    bool                            mUploadTransferResourcesBeingCreated = false;
    std::vector<VkCommandPool>      mDestroyedCommandPools;
    std::vector<VkSemaphore>        mDestroyedSemaphores;
    std::vector<VkFence>            mDestroyedFences;
    VkDevice                        mFrameSlotDevice             = VK_NULL_HANDLE;
    VkCommandPoolCreateInfo         mCommandPoolCreateInfo{};
    VkCommandBufferAllocateInfo     mCommandBufferAllocateInfo{};
    VkSemaphoreCreateInfo           mSemaphoreCreateInfo{};
    VkFenceCreateInfo               mFenceCreateInfo{};
    VkCommandPoolCreateInfo         mUploadTransferCommandPoolCreateInfo{};
    VkCommandBufferAllocateInfo     mUploadTransferCommandBufferAllocateInfo{};
    VkFenceCreateInfo               mUploadTransferFenceCreateInfo{};
    const VkAllocationCallbacks*    mCreateCommandPoolAllocationCallbacks    = nullptr;
    const VkAllocationCallbacks*    mDestroyCommandPoolAllocationCallbacks   = nullptr;
    const VkAllocationCallbacks*    mCreateSemaphoreAllocationCallbacks      = nullptr;
    const VkAllocationCallbacks*    mDestroySemaphoreAllocationCallbacks     = nullptr;
    const VkAllocationCallbacks*    mCreateFenceAllocationCallbacks          = nullptr;
    const VkAllocationCallbacks*    mDestroyFenceAllocationCallbacks         = nullptr;
    const VulkanInstanceGeneration* mFrameSlotDestroyOwner                   = nullptr;
    bool                            mFrameSlotDestroyObservationMade         = false;
    bool                            mObservedImagesAtFrameSlotDestroy        = false;
    bool                            mObservedSwapchainAtFrameSlotDestroy     = false;
    bool                            mObservedConfigurationAtFrameSlotDestroy = false;
    bool                            mObservedLogicalAtFrameSlotDestroy       = false;
    bool                            mObservedSurfaceAtFrameSlotDestroy       = false;
    bool                            mObservedPresentationTargetAtFrameSlotDestroy = false;

    std::vector<VkResult> mWaitForFencesResults{ VK_SUCCESS, VK_SUCCESS };
    std::size_t           mWaitForFencesResultIndex = 0;
    VkResult              mResetCommandBufferResult = VK_SUCCESS;
    VkResult              mBeginCommandBufferResult = VK_SUCCESS;
    VkResult              mEndCommandBufferResult   = VK_SUCCESS;
    VkResult              mResetFencesResult        = VK_SUCCESS;
    VkResult              mQueueSubmitResult        = VK_SUCCESS;
    VkResult              mAcquireNextImageResult       = VK_SUCCESS;
    std::uint32_t         mAcquiredImageIndex           = 0;
    VkResult              mQueuePresentResult           = VK_SUCCESS;
    VkResult              mReleaseSwapchainImagesResult = VK_SUCCESS;
    std::size_t           mWaitForFencesCalls       = 0;
    std::size_t           mResetCommandBufferCalls  = 0;
    std::size_t           mBeginCommandBufferCalls  = 0;
    std::size_t           mEndCommandBufferCalls    = 0;
    std::size_t           mResetFencesCalls         = 0;
    std::size_t           mQueueSubmitCalls         = 0;
    std::size_t           mAcquireNextImageCalls        = 0;
    std::size_t           mPipelineBarrierCalls         = 0;
    std::size_t                                                        mCopyBufferCalls              = 0;
    std::size_t           mClearColorImageCalls         = 0;
    std::size_t           mBeginRenderPassCalls         = 0;
    std::size_t           mEndRenderPassCalls           = 0;
    std::size_t             mBindPipelineCalls            = 0;
    std::size_t             mSetViewportCalls             = 0;
    std::size_t             mSetScissorCalls              = 0;
    std::size_t             mDrawCalls                    = 0;
    std::size_t           mQueuePresentCalls            = 0;
    std::size_t           mReleaseSwapchainImagesCalls  = 0;
    VkDevice              mOperationDevice          = VK_NULL_HANDLE;
    VkQueue               mSubmittedQueue           = VK_NULL_HANDLE;
    VkFence               mOperationFence           = VK_NULL_HANDLE;
    VkCommandBuffer       mOperationCommandBuffer   = VK_NULL_HANDLE;
    VkBool32              mWaitAll                  = VK_FALSE;
    std::uint64_t         mWaitTimeout              = 0;
    VkImage               mClearedImage             = VK_NULL_HANDLE;
    VkClearColorValue     mClearColorValue{};
    VkImageSubresourceRange mClearSubresourceRange{};
    VkCommandBuffer       mRenderPassCommandBuffer      = VK_NULL_HANDLE;
    VkRenderPass          mOperationRenderPass          = VK_NULL_HANDLE;
    VkFramebuffer         mOperationFramebuffer         = VK_NULL_HANDLE;
    VkRect2D              mOperationRenderArea{};
    VkClearValue          mRenderPassClearValue{};
    VkSubpassContents     mOperationSubpassContents     = VK_SUBPASS_CONTENTS_MAX_ENUM;
    VkPipelineBindPoint     mOperationPipelineBindPoint   = VK_PIPELINE_BIND_POINT_MAX_ENUM;
    VkPipeline              mOperationPipeline            = VK_NULL_HANDLE;
    VkViewport              mOperationViewport{};
    VkRect2D                mOperationScissor{};
    std::uint32_t           mDrawVertexCount   = 0;
    std::uint32_t           mDrawInstanceCount = 0;
    std::uint32_t           mDrawFirstVertex   = 0;
    std::uint32_t           mDrawFirstInstance = 0;
    std::size_t             mCopyImageToBufferCalls = 0;
    std::vector<VkBufferMemoryBarrier>                                 mBufferBarriers;
    std::vector<std::pair<VkPipelineStageFlags, VkPipelineStageFlags>> mBufferBarrierStages;
    VkBuffer                                                           mCopySourceBuffer      = VK_NULL_HANDLE;
    VkBuffer                                                           mCopyDestinationBuffer = VK_NULL_HANDLE;
    VkBufferCopy                                                       mBufferCopyRegion{};
    std::size_t                                                        mInstanceOwnerChecksAtUploadTransferBegin = 0;
    std::size_t                                                        mSurfaceWindowChecksAtUploadTransferBegin = 0;

    ReadbackResetReentryPoint                          mReadbackResetReentryPoint   = ReadbackResetReentryPoint::None;
    VulkanInstanceGeneration*                          mReadbackResetReentryOwner   = nullptr;
    bool                                               mReadbackResetReentryInvoked = false;
    std::optional<VulkanSwapchainFrameSlotDisposition> mReadbackResetReentryDisposition;
    bool                                               mReenteredReadbackResetSucceeded  = false;
    bool                                               mReenteredFrameSlotResetSucceeded = false;
    bool                                               mReenteredImagesResetSucceeded    = false;
    VulkanInstanceGeneration*                          mUploadTransferResetReentryOwner          = nullptr;
    bool                                               mUploadTransferResetReentryInvoked        = false;
    bool                                               mReenteredUploadTransferResetSucceeded    = false;
    bool                                               mReenteredUploadDestinationResetSucceeded = false;
    bool                                               mReenteredUploadSourceResetSucceeded      = false;
    bool                                               mReenteredLogicalResetSucceeded           = false;
    bool                                               mReenteredAggregateResetSucceeded         = false;
    bool                                               mUploadTransferReentryObservedNoTeardown  = false;

    bool        mGenerationCurrent   = true;
    std::size_t mGenerationChecks    = 0;
    std::size_t mFailGenerationCheck = 0;

    const VulkanInstanceGeneration* mExpectedInstanceOwner  = nullptr;
    bool                            mInstanceOwnerCurrent   = true;
    std::size_t                     mInstanceOwnerChecks    = 0;
    std::size_t                     mFailInstanceOwnerCheck = 0;

    VulkanInstanceGeneration* mMutationOwner                      = nullptr;
    std::size_t               mResetFrameSlotOnInstanceOwnerCheck = 0;
    bool                      mSurfaceWindowCurrent               = true;
    std::size_t               mSurfaceWindowChecks                = 0;
    std::size_t               mFailSurfaceWindowCheck             = 0;

    FakeState()
    {
        mPhysicalDeviceProperties.apiVersion = VK_API_VERSION_1_1;
        mPhysicalDeviceProperties.deviceType = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
        mPhysicalDeviceProperties.limits.maxFramebufferWidth  = 4096;
        mPhysicalDeviceProperties.limits.maxFramebufferHeight = 2160;
        mPhysicalDeviceProperties.limits.maxViewportDimensions[0] = 4096;
        mPhysicalDeviceProperties.limits.maxViewportDimensions[1] = 4096;
        mPhysicalDeviceProperties.limits.viewportBoundsRange[0]   = -32768.0f;
        mPhysicalDeviceProperties.limits.viewportBoundsRange[1]   = 32767.0f;
        std::memcpy(mPhysicalDeviceProperties.deviceName, "fake-presentation-device", sizeof("fake-presentation-device"));
        mQueueFamilyProperties.queueFlags     = VK_QUEUE_GRAPHICS_BIT;
        mQueueFamilyProperties.queueCount     = 1;
        mSupportedFeatures.independentBlend   = VK_TRUE;
        mSwapchainCapabilities.minImageCount  = 2;
        mSwapchainCapabilities.maxImageCount  = 3;
        mSwapchainCapabilities.currentExtent  = { std::numeric_limits<std::uint32_t>::max(), std::numeric_limits<std::uint32_t>::max() };
        mSwapchainCapabilities.minImageExtent = { 64, 64 };
        mSwapchainCapabilities.maxImageExtent = { 4096, 2160 };
        mSwapchainCapabilities.maxImageArrayLayers     = 1;
        mSwapchainCapabilities.supportedTransforms     = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        mSwapchainCapabilities.currentTransform        = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        mSwapchainCapabilities.supportedCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        mSwapchainCapabilities.supportedUsageFlags =
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        mSwapchainFormatProperties.optimalTilingFeatures =
            VK_FORMAT_FEATURE_TRANSFER_DST_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
        mMemoryProperties.memoryTypeCount              = 1;
        mMemoryProperties.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        mMemoryProperties.memoryTypes[0].heapIndex     = 0;
        mMemoryProperties.memoryHeapCount              = 1;
        mMemoryProperties.memoryHeaps[0].size          = 64 * 1024 * 1024;
        mReadbackMemoryRequirements.size               = 4 * 1024 * 1024;
        mReadbackMemoryRequirements.alignment          = 256;
        mReadbackMemoryRequirements.memoryTypeBits     = 1;
        mUploadSourceMemoryRequirements.size           = 256;
        mUploadSourceMemoryRequirements.alignment      = 16;
        mUploadSourceMemoryRequirements.memoryTypeBits = 1;
        mUploadDestinationMemoryRequirements.size           = 256;
        mUploadDestinationMemoryRequirements.alignment      = 16;
        mUploadDestinationMemoryRequirements.memoryTypeBits = 1;
    }
};

FakeState* gFakeState = nullptr;

class ScopedFakeState
{
public:
    explicit ScopedFakeState(FakeState& state) noexcept { gFakeState = &state; }
    ~ScopedFakeState() noexcept { gFakeState = nullptr; }

    ScopedFakeState(const ScopedFakeState&)            = delete;
    ScopedFakeState& operator=(const ScopedFakeState&) = delete;

    void use(FakeState& state) noexcept { gFakeState = &state; }
};

void attemptReadbackResetReentry(FakeState& state) noexcept
{
    if (!state.mReadbackResetReentryOwner || state.mReadbackResetReentryInvoked)
    {
        return;
    }
    state.mReadbackResetReentryInvoked      = true;
    state.mReadbackResetReentryDisposition  = state.mReadbackResetReentryOwner->swapchainFrameSlotDisposition();
    state.mReenteredReadbackResetSucceeded  = state.mReadbackResetReentryOwner->resetSwapchainReadbackGeneration();
    state.mReenteredFrameSlotResetSucceeded = state.mReadbackResetReentryOwner->resetSwapchainFrameSlotGeneration();
    state.mReenteredImagesResetSucceeded    = state.mReadbackResetReentryOwner->resetSwapchainImagesGeneration();
}

void attemptUploadTransferResetReentry(FakeState& state) noexcept
{
    if (!state.mUploadTransferResetReentryOwner || state.mUploadTransferResetReentryInvoked)
    {
        return;
    }

    const std::size_t destroyed_buffers_before      = state.mDestroyBufferCalls;
    const std::size_t freed_memory_before           = state.mFreeMemoryCalls;
    const std::size_t destroyed_pools_before        = state.mDestroyCommandPoolCalls;
    const std::size_t destroyed_fences_before       = state.mDestroyFenceCalls;
    state.mUploadTransferResetReentryInvoked        = true;
    state.mReenteredUploadTransferResetSucceeded    = state.mUploadTransferResetReentryOwner->resetUploadTransferGeneration();
    state.mReenteredUploadDestinationResetSucceeded = state.mUploadTransferResetReentryOwner->resetUploadDestinationGeneration();
    state.mReenteredUploadSourceResetSucceeded      = state.mUploadTransferResetReentryOwner->resetUploadSourceGeneration();
    state.mReenteredLogicalResetSucceeded           = state.mUploadTransferResetReentryOwner->resetLogicalDeviceGeneration();
    state.mReenteredAggregateResetSucceeded         = state.mUploadTransferResetReentryOwner->reset();
    state.mUploadTransferReentryObservedNoTeardown =
        state.mDestroyBufferCalls == destroyed_buffers_before && state.mFreeMemoryCalls == freed_memory_before &&
        state.mDestroyCommandPoolCalls == destroyed_pools_before && state.mDestroyFenceCalls == destroyed_fences_before &&
        state.mUploadTransferResetReentryOwner->hasUploadSourceGeneration() &&
        state.mUploadTransferResetReentryOwner->hasUploadDestinationGeneration() &&
        state.mUploadTransferResetReentryOwner->hasUploadTransferGeneration() &&
        state.mUploadTransferResetReentryOwner->hasLogicalDeviceGeneration();
}

template<typename Function>
PFN_vkVoidFunction eraseFunctionType(Function function) noexcept
{
    return reinterpret_cast<PFN_vkVoidFunction>(function);
}

template<std::size_t Size>
void copyPropertyName(char (&destination)[Size], const std::string& source, bool malformed) noexcept
{
    if (malformed)
    {
        std::fill(std::begin(destination), std::end(destination), 'x');
        return;
    }
    const std::size_t count = std::min(source.size(), Size - 1);
    std::memcpy(destination, source.data(), count);
    destination[count] = '\0';
}

VKAPI_ATTR VkResult VKAPI_CALL fakeEnumerateInstanceVersion(std::uint32_t* version) noexcept
{
    if (!gFakeState || !version)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    ++gFakeState->mVersionCalls;
    *version = gFakeState->mVersion;
    return gFakeState->mVersionResult;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeEnumerateInstanceExtensionProperties(const char*, std::uint32_t* count,
                                                                        VkExtensionProperties* properties) noexcept
{
    if (!gFakeState || !count)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!properties)
    {
        ++gFakeState->mExtensionCountCalls;
        *count = gFakeState->mExtensionCountOverride == std::numeric_limits<std::uint32_t>::max()
                     ? static_cast<std::uint32_t>(gFakeState->mExtensions.size())
                     : gFakeState->mExtensionCountOverride;
        if (gFakeState->mExtensionCountCalls <= gFakeState->mIncompleteExtensionCountCalls)
        {
            return VK_INCOMPLETE;
        }
        return gFakeState->mExtensionCountResult;
    }

    ++gFakeState->mExtensionValuesCalls;
    const std::uint32_t capacity   = *count;
    const std::uint32_t written    = gFakeState->mExtensionWrittenOverride == std::numeric_limits<std::uint32_t>::max()
                                         ? static_cast<std::uint32_t>(gFakeState->mExtensions.size())
                                         : gFakeState->mExtensionWrittenOverride;
    const std::size_t   fill_count = std::min<std::size_t>(capacity, gFakeState->mExtensions.size());
    for (std::size_t index = 0; index < fill_count; ++index)
    {
        copyPropertyName(properties[index].extensionName, gFakeState->mExtensions[index], gFakeState->mMalformedExtension && index == 0);
    }
    *count = written;
    if (gFakeState->mAlwaysIncompleteExtensions || gFakeState->mExtensionValuesCalls <= gFakeState->mIncompleteExtensionValueCalls)
    {
        return VK_INCOMPLETE;
    }
    return gFakeState->mExtensionValuesResult;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeEnumerateInstanceLayerProperties(std::uint32_t* count, VkLayerProperties* properties) noexcept
{
    if (!gFakeState || !count)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!properties)
    {
        ++gFakeState->mLayerCountCalls;
        *count = gFakeState->mLayerCountOverride == std::numeric_limits<std::uint32_t>::max()
                     ? static_cast<std::uint32_t>(gFakeState->mLayers.size())
                     : gFakeState->mLayerCountOverride;
        return gFakeState->mLayerCountResult;
    }

    ++gFakeState->mLayerValuesCalls;
    const std::uint32_t capacity   = *count;
    const std::size_t   fill_count = std::min<std::size_t>(capacity, gFakeState->mLayers.size());
    for (std::size_t index = 0; index < fill_count; ++index)
    {
        copyPropertyName(properties[index].layerName, gFakeState->mLayers[index], gFakeState->mMalformedLayer && index == 0);
    }
    *count = static_cast<std::uint32_t>(gFakeState->mLayers.size());
    return gFakeState->mLayerValuesResult;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateInstance(const VkInstanceCreateInfo* create_info, const VkAllocationCallbacks*,
                                                  VkInstance*                 instance) noexcept
{
    if (!gFakeState || !create_info || !instance || !create_info->pApplicationInfo)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    gFakeState->mEvents.push_back(Event::CreateInstance);
    gFakeState->mInstanceFlags       = create_info->flags;
    gFakeState->mRequestedApiVersion = create_info->pApplicationInfo->apiVersion;
    gFakeState->mEnabledExtensions.clear();
    for (std::uint32_t index = 0; index < create_info->enabledExtensionCount; ++index)
    {
        gFakeState->mEnabledExtensions.emplace_back(create_info->ppEnabledExtensionNames[index]);
    }
    gFakeState->mEnabledLayers.clear();
    for (std::uint32_t index = 0; index < create_info->enabledLayerCount; ++index)
    {
        gFakeState->mEnabledLayers.emplace_back(create_info->ppEnabledLayerNames[index]);
    }
    if (create_info->pNext)
    {
        const auto* debug_info          = static_cast<const VkDebugUtilsMessengerCreateInfoEXT*>(create_info->pNext);
        gFakeState->mValidationCallback = debug_info->pfnUserCallback;
        gFakeState->mValidationUserdata = debug_info->pUserData;
    }
    *instance = gFakeState->mNullInstance ? VK_NULL_HANDLE : gFakeState->mInstance;
    return gFakeState->mInstanceResult;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyInstance(VkInstance instance, const VkAllocationCallbacks*) noexcept
{
    if (gFakeState && instance == gFakeState->mInstance)
    {
        ++gFakeState->mDestroyInstanceCalls;
        gFakeState->mEvents.push_back(Event::DestroyInstance);
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateDebugUtilsMessenger(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* create_info,
                                                             const VkAllocationCallbacks*, VkDebugUtilsMessengerEXT* messenger) noexcept
{
    if (!gFakeState || instance != gFakeState->mInstance || !create_info || !messenger)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    gFakeState->mEvents.push_back(Event::CreateDebugMessenger);
    gFakeState->mValidationCallback = create_info->pfnUserCallback;
    gFakeState->mValidationUserdata = create_info->pUserData;
    *messenger                      = gFakeState->mNullDebugMessenger ? VK_NULL_HANDLE : gFakeState->mDebugMessenger;
    return gFakeState->mDebugResult;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyDebugUtilsMessenger(VkInstance instance, VkDebugUtilsMessengerEXT messenger,
                                                          const VkAllocationCallbacks*) noexcept
{
    if (gFakeState && instance == gFakeState->mInstance && messenger == gFakeState->mDebugMessenger)
    {
        ++gFakeState->mDestroyDebugCalls;
        gFakeState->mEvents.push_back(Event::DestroyDebugMessenger);
    }
}

VKAPI_ATTR void VKAPI_CALL fakeDestroySurface(VkInstance                   instance,
                                              VkSurfaceKHR                 surface,
                                              const VkAllocationCallbacks* allocation_callbacks) noexcept
{
    if (!gFakeState)
    {
        return;
    }
    ++gFakeState->mDestroySurfaceCalls;
    gFakeState->mDestroySurfaceInstance            = instance;
    gFakeState->mDestroyedSurface                  = surface;
    gFakeState->mDestroySurfaceAllocationCallbacks = allocation_callbacks;
    if (gFakeState->mSurfaceDestroyOwner)
    {
        gFakeState->mSurfaceDestroyObservationMade           = true;
        gFakeState->mObservedPresentationAtSurfaceDestroy    = gFakeState->mSurfaceDestroyOwner->hasPresentationDeviceGeneration();
        gFakeState->mObservedLogicalAtSurfaceDestroy         = gFakeState->mSurfaceDestroyOwner->hasLogicalDeviceGeneration();
        gFakeState->mObservedConfigurationAtSurfaceDestroy   = gFakeState->mSurfaceDestroyOwner->hasSwapchainConfigurationGeneration();
        gFakeState->mObservedSwapchainAtSurfaceDestroy       = gFakeState->mSurfaceDestroyOwner->hasSwapchainGeneration();
        gFakeState->mObservedSwapchainImagesAtSurfaceDestroy = gFakeState->mSurfaceDestroyOwner->hasSwapchainImagesGeneration();
        gFakeState->mObservedFrameSlotAtSurfaceDestroy       = gFakeState->mSurfaceDestroyOwner->hasSwapchainFrameSlotGeneration();
    }
    gFakeState->mEvents.push_back(Event::DestroySurface);
}

VKAPI_ATTR VkResult VKAPI_CALL fakeEnumeratePhysicalDevices(VkInstance instance, std::uint32_t* count, VkPhysicalDevice* devices) noexcept
{
    if (!gFakeState || instance != gFakeState->mInstance || !count)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!devices)
    {
        ++gFakeState->mPhysicalCountCalls;
        *count = 1;
    }
    else
    {
        ++gFakeState->mPhysicalListCalls;
        if (*count != 0)
        {
            devices[0] = gFakeState->mPhysicalDevice;
            *count     = 1;
        }
    }
    return gFakeState->mPhysicalEnumerationResult;
}

VKAPI_ATTR void VKAPI_CALL fakeGetPhysicalDeviceProperties(VkPhysicalDevice            physical_device,
                                                           VkPhysicalDeviceProperties* properties) noexcept
{
    if (gFakeState && physical_device == gFakeState->mPhysicalDevice && properties)
    {
        ++gFakeState->mPhysicalPropertiesCalls;
        *properties = gFakeState->mPhysicalDeviceProperties;
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
    if (!properties)
    {
        ++gFakeState->mQueueFamilyCountCalls;
        *count = 1;
    }
    else
    {
        ++gFakeState->mQueueFamilyListCalls;
        if (*count != 0)
        {
            properties[0] = gFakeState->mQueueFamilyProperties;
            *count        = 1;
        }
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeGetPhysicalDeviceSurfaceSupport(VkPhysicalDevice physical_device,
                                                                   std::uint32_t    queue_family_index,
                                                                   VkSurfaceKHR     surface,
                                                                   VkBool32*        supported) noexcept
{
    if (!gFakeState || !supported || physical_device != gFakeState->mPhysicalDevice || surface != gFakeState->mSurface ||
        queue_family_index != 0)
    {
        return VK_ERROR_SURFACE_LOST_KHR;
    }
    ++gFakeState->mSurfaceSupportCalls;
    gFakeState->mLastSurfaceSupportDevice  = physical_device;
    gFakeState->mLastSurfaceSupportQueue   = queue_family_index;
    gFakeState->mLastSurfaceSupportSurface = surface;
    *supported                             = gFakeState->mPresentationSupported;
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
        ++gFakeState->mDeviceExtensionCountCalls;
        *count = static_cast<std::uint32_t>(gFakeState->mDeviceExtensions.size());
        return VK_SUCCESS;
    }

    ++gFakeState->mDeviceExtensionValuesCalls;
    const std::size_t fill_count = std::min<std::size_t>(*count, gFakeState->mDeviceExtensions.size());
    for (std::size_t index = 0; index < fill_count; ++index)
    {
        copyPropertyName(properties[index].extensionName, gFakeState->mDeviceExtensions[index], false);
    }
    *count = static_cast<std::uint32_t>(gFakeState->mDeviceExtensions.size());
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeGetPhysicalDeviceFeatures(VkPhysicalDevice physical_device, VkPhysicalDeviceFeatures* features) noexcept
{
    if (!gFakeState || physical_device != gFakeState->mPhysicalDevice || !features)
    {
        return;
    }
    ++gFakeState->mPhysicalFeaturesCalls;
    gFakeState->mPhysicalFeaturesDevice = physical_device;
    *features                           = gFakeState->mSupportedFeatures;
}

VKAPI_ATTR void VKAPI_CALL fakeGetPhysicalDeviceFeatures2(VkPhysicalDevice physical_device, VkPhysicalDeviceFeatures2* features) noexcept
{
    if (!gFakeState || physical_device != gFakeState->mPhysicalDevice || !features)
    {
        return;
    }
    for (VkBaseOutStructure* extension = static_cast<VkBaseOutStructure*>(features->pNext); extension; extension = extension->pNext)
    {
        if (extension->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_KHR)
        {
            auto* maintenance                  = reinterpret_cast<VkPhysicalDeviceSwapchainMaintenance1FeaturesKHR*>(extension);
            maintenance->swapchainMaintenance1 = gFakeState->mSwapchainMaintenance1Supported;
        }
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
    ++gFakeState->mCreateDeviceCalls;
    gFakeState->mEvents.push_back(Event::CreateDevice);
    *device = gFakeState->mDevice;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyDevice(VkDevice device, const VkAllocationCallbacks*) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice)
    {
        return;
    }
    ++gFakeState->mDestroyDeviceCalls;
    if (gFakeState->mDeviceDestroyOwner)
    {
        gFakeState->mDeviceDestroyObservationMade           = true;
        gFakeState->mObservedPresentationAtDeviceDestroy    = gFakeState->mDeviceDestroyOwner->hasPresentationDeviceGeneration();
        gFakeState->mObservedSurfaceAtDeviceDestroy         = gFakeState->mDeviceDestroyOwner->hasSurfaceGeneration();
        gFakeState->mObservedConfigurationAtDeviceDestroy   = gFakeState->mDeviceDestroyOwner->hasSwapchainConfigurationGeneration();
        gFakeState->mObservedSwapchainAtDeviceDestroy       = gFakeState->mDeviceDestroyOwner->hasSwapchainGeneration();
        gFakeState->mObservedSwapchainImagesAtDeviceDestroy = gFakeState->mDeviceDestroyOwner->hasSwapchainImagesGeneration();
        gFakeState->mObservedFrameSlotAtDeviceDestroy       = gFakeState->mDeviceDestroyOwner->hasSwapchainFrameSlotGeneration();
        gFakeState->mObservedUploadSourceAtDeviceDestroy    = gFakeState->mDeviceDestroyOwner->hasUploadSourceGeneration();
        gFakeState->mObservedUploadDestinationAtDeviceDestroy = gFakeState->mDeviceDestroyOwner->hasUploadDestinationGeneration();
        gFakeState->mObservedUploadTransferAtDeviceDestroy    = gFakeState->mDeviceDestroyOwner->hasUploadTransferGeneration();
    }
    gFakeState->mEvents.push_back(Event::DestroyDevice);
}

VKAPI_ATTR void VKAPI_CALL fakeGetDeviceQueue(VkDevice      device,
                                              std::uint32_t queue_family_index,
                                              std::uint32_t queue_index,
                                              VkQueue*      queue) noexcept
{
    if (!gFakeState || !queue)
    {
        return;
    }
    ++gFakeState->mGetDeviceQueueCalls;
    gFakeState->mGetDeviceQueueDevice = device;
    gFakeState->mGetDeviceQueueFamily = queue_family_index;
    gFakeState->mGetDeviceQueueIndex  = queue_index;
    gFakeState->mEvents.push_back(Event::GetDeviceQueue);
    *queue = gFakeState->mQueue;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeGetPhysicalDeviceSurfaceCapabilities(VkPhysicalDevice          physical_device,
                                                                        VkSurfaceKHR              surface,
                                                                        VkSurfaceCapabilitiesKHR* capabilities) noexcept
{
    if (!gFakeState || physical_device != gFakeState->mPhysicalDevice || surface != gFakeState->mSurface || !capabilities)
    {
        return VK_ERROR_SURFACE_LOST_KHR;
    }
    ++gFakeState->mSwapchainCapabilitiesCalls;
    if (gFakeState->mSwapchainCapabilitiesResult == VK_SUCCESS)
    {
        *capabilities = gFakeState->mSwapchainCapabilities;
    }
    return gFakeState->mSwapchainCapabilitiesResult;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeGetPhysicalDeviceSurfaceFormats(VkPhysicalDevice    physical_device,
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
        ++gFakeState->mSwapchainFormatCountCalls;
        *count = static_cast<std::uint32_t>(gFakeState->mSwapchainFormats.size());
        return VK_SUCCESS;
    }
    ++gFakeState->mSwapchainFormatListCalls;
    const std::size_t written = std::min<std::size_t>(*count, gFakeState->mSwapchainFormats.size());
    std::copy_n(gFakeState->mSwapchainFormats.begin(), written, formats);
    *count = static_cast<std::uint32_t>(written);
    return written == gFakeState->mSwapchainFormats.size() ? VK_SUCCESS : VK_INCOMPLETE;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeGetPhysicalDeviceSurfacePresentModes(VkPhysicalDevice  physical_device,
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
        ++gFakeState->mSwapchainPresentModeCountCalls;
        *count = static_cast<std::uint32_t>(gFakeState->mSwapchainPresentModes.size());
        return VK_SUCCESS;
    }
    ++gFakeState->mSwapchainPresentModeListCalls;
    const std::size_t written = std::min<std::size_t>(*count, gFakeState->mSwapchainPresentModes.size());
    std::copy_n(gFakeState->mSwapchainPresentModes.begin(), written, modes);
    *count = static_cast<std::uint32_t>(written);
    return written == gFakeState->mSwapchainPresentModes.size() ? VK_SUCCESS : VK_INCOMPLETE;
}

VKAPI_ATTR void VKAPI_CALL fakeGetPhysicalDeviceFormatProperties(VkPhysicalDevice    physical_device,
                                                                 VkFormat            format,
                                                                 VkFormatProperties* properties) noexcept
{
    if (!gFakeState || physical_device != gFakeState->mPhysicalDevice ||
        format != gFakeState->mSwapchainFormats.front().format || !properties)
    {
        return;
    }
    ++gFakeState->mSwapchainFormatPropertiesCalls;
    *properties = gFakeState->mSwapchainFormatProperties;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateSwapchain(VkDevice                        device,
                                                   const VkSwapchainCreateInfoKHR* create_info,
                                                   const VkAllocationCallbacks*    allocation_callbacks,
                                                   VkSwapchainKHR*                 swapchain) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || !create_info || !swapchain)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    ++gFakeState->mCreateSwapchainCalls;
    gFakeState->mCreateSwapchainDevice              = device;
    gFakeState->mSwapchainCreateInfo                = *create_info;
    gFakeState->mCreateSwapchainAllocationCallbacks = allocation_callbacks;
    gFakeState->mEvents.push_back(Event::CreateSwapchain);

    const bool failed = gFakeState->mSwapchainCreateResult != VK_SUCCESS;
    *swapchain = gFakeState->mNullSwapchain || (failed && !gFakeState->mPoisonSwapchainOutput) ? VK_NULL_HANDLE : gFakeState->mSwapchain;
    return gFakeState->mSwapchainCreateResult;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroySwapchain(VkDevice                     device,
                                                VkSwapchainKHR               swapchain,
                                                const VkAllocationCallbacks* allocation_callbacks) noexcept
{
    if (!gFakeState)
    {
        return;
    }

    ++gFakeState->mDestroySwapchainCalls;
    gFakeState->mDestroySwapchainDevice              = device;
    gFakeState->mDestroyedSwapchain                  = swapchain;
    gFakeState->mDestroyedSwapchains.push_back(swapchain);
    gFakeState->mDestroySwapchainAllocationCallbacks = allocation_callbacks;
    if (gFakeState->mSwapchainDestroyOwner)
    {
        gFakeState->mSwapchainDestroyObservationMade           = true;
        gFakeState->mObservedConfigurationAtSwapchainDestroy   = gFakeState->mSwapchainDestroyOwner->hasSwapchainConfigurationGeneration();
        gFakeState->mObservedLogicalAtSwapchainDestroy         = gFakeState->mSwapchainDestroyOwner->hasLogicalDeviceGeneration();
        gFakeState->mObservedSurfaceAtSwapchainDestroy         = gFakeState->mSwapchainDestroyOwner->hasSurfaceGeneration();
        gFakeState->mObservedSwapchainImagesAtSwapchainDestroy = gFakeState->mSwapchainDestroyOwner->hasSwapchainImagesGeneration();
        gFakeState->mObservedFrameSlotAtSwapchainDestroy       = gFakeState->mSwapchainDestroyOwner->hasSwapchainFrameSlotGeneration();
    }
    gFakeState->mEvents.push_back(Event::DestroySwapchain);
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

    gFakeState->mSwapchainImagesDevice = device;
    gFakeState->mEnumeratedSwapchain   = swapchain;
    if (!images)
    {
        ++gFakeState->mSwapchainImageCountCalls;
        *count = static_cast<std::uint32_t>(gFakeState->mSwapchainImages.size());
        return gFakeState->mSwapchainImageCountResult;
    }

    ++gFakeState->mSwapchainImageListCalls;
    const std::size_t written = std::min<std::size_t>(*count, gFakeState->mSwapchainImages.size());
    std::copy_n(gFakeState->mSwapchainImages.begin(), written, images);
    *count = static_cast<std::uint32_t>(written);
    return gFakeState->mSwapchainImageListResult;
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

    ++gFakeState->mCreateImageViewCalls;
    gFakeState->mCreateImageViewDevice = device;
    gFakeState->mImageViewCreateInfos.push_back(*create_info);
    gFakeState->mCreateImageViewAllocationCallbacks.push_back(allocation_callbacks);
    gFakeState->mEvents.push_back(Event::CreateImageView);
    if (gFakeState->mImageViewCreateResult != VK_SUCCESS)
    {
        return gFakeState->mImageViewCreateResult;
    }

    const auto image = std::find(gFakeState->mSwapchainImages.begin(), gFakeState->mSwapchainImages.end(), create_info->image);
    if (image == gFakeState->mSwapchainImages.end())
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    const std::size_t index = static_cast<std::size_t>(image - gFakeState->mSwapchainImages.begin());
    *image_view             = gFakeState->mNullImageView ? VK_NULL_HANDLE : gFakeState->mSwapchainImageViews[index];
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyImageView(VkDevice                     device,
                                                VkImageView                  image_view,
                                                const VkAllocationCallbacks* allocation_callbacks) noexcept
{
    if (!gFakeState)
    {
        return;
    }

    ++gFakeState->mDestroyImageViewCalls;
    gFakeState->mDestroyImageViewDevice = device;
    gFakeState->mDestroyedImageViews.push_back(image_view);
    gFakeState->mDestroyImageViewAllocationCallbacks.push_back(allocation_callbacks);
    if (gFakeState->mImageViewDestroyOwner)
    {
        gFakeState->mImageViewDestroyObservationMade         = true;
        gFakeState->mObservedSwapchainAtImageViewDestroy     = gFakeState->mImageViewDestroyOwner->hasSwapchainGeneration();
        gFakeState->mObservedConfigurationAtImageViewDestroy = gFakeState->mImageViewDestroyOwner->hasSwapchainConfigurationGeneration();
        gFakeState->mObservedLogicalAtImageViewDestroy       = gFakeState->mImageViewDestroyOwner->hasLogicalDeviceGeneration();
        gFakeState->mObservedSurfaceAtImageViewDestroy       = gFakeState->mImageViewDestroyOwner->hasSurfaceGeneration();
        gFakeState->mObservedPresentationTargetAtImageViewDestroy =
            gFakeState->mImageViewDestroyOwner->hasSwapchainPresentationTargetGeneration();
        gFakeState->mObservedFrameSlotAtImageViewDestroy     = gFakeState->mImageViewDestroyOwner->hasSwapchainFrameSlotGeneration();
    }
    gFakeState->mEvents.push_back(Event::DestroyImageView);
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateRenderPass(VkDevice                       device,
                                                    const VkRenderPassCreateInfo* create_info,
                                                    const VkAllocationCallbacks*,
                                                    VkRenderPass* render_pass) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || !create_info || !render_pass ||
        create_info->sType != VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    ++gFakeState->mCreateRenderPassCalls;
    gFakeState->mFramebufferIndexWithinRenderPass = 0;
    gFakeState->mPresentationTargetDevice = device;
    gFakeState->mEvents.push_back(Event::CreateRenderPass);
    if (gFakeState->mRenderPassCreateResult == VK_SUCCESS)
    {
        *render_pass = gFakeState->mNullRenderPass ? VK_NULL_HANDLE : gFakeState->mPresentationRenderPass;
    }
    return gFakeState->mRenderPassCreateResult;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyRenderPass(VkDevice,
                                                 VkRenderPass render_pass,
                                                 const VkAllocationCallbacks*) noexcept
{
    if (!gFakeState)
    {
        return;
    }
    ++gFakeState->mDestroyRenderPassCalls;
    gFakeState->mDestroyedRenderPass = render_pass;
    if (gFakeState->mPresentationTargetDestroyOwner)
    {
        gFakeState->mPresentationTargetDestroyObservationMade = true;
        gFakeState->mObservedImagesAtPresentationTargetDestroy =
            gFakeState->mPresentationTargetDestroyOwner->hasSwapchainImagesGeneration();
        gFakeState->mObservedFrameSlotAtPresentationTargetDestroy =
            gFakeState->mPresentationTargetDestroyOwner->hasSwapchainFrameSlotGeneration();
    }
    gFakeState->mEvents.push_back(Event::DestroyRenderPass);
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateFramebuffer(VkDevice                        device,
                                                     const VkFramebufferCreateInfo* create_info,
                                                     const VkAllocationCallbacks*,
                                                     VkFramebuffer* framebuffer) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || !create_info || !framebuffer ||
        create_info->sType != VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO || create_info->attachmentCount != 1 ||
        !create_info->pAttachments)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    const std::size_t index = gFakeState->mFramebufferIndexWithinRenderPass++;
    ++gFakeState->mCreateFramebufferCalls;
    gFakeState->mPresentationTargetDevice = device;
    gFakeState->mFramebufferAttachments.push_back(create_info->pAttachments[0]);
    gFakeState->mFramebufferExtents.push_back({ create_info->width, create_info->height });
    gFakeState->mEvents.push_back(Event::CreateFramebuffer);
    if (gFakeState->mFramebufferCreateResult != VK_SUCCESS)
    {
        return gFakeState->mFramebufferCreateResult;
    }
    if (index >= gFakeState->mPresentationFramebuffers.size())
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    *framebuffer = gFakeState->mNullFramebuffer ? VK_NULL_HANDLE : gFakeState->mPresentationFramebuffers[index];
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyFramebuffer(VkDevice,
                                                  VkFramebuffer framebuffer,
                                                  const VkAllocationCallbacks*) noexcept
{
    if (!gFakeState)
    {
        return;
    }
    ++gFakeState->mDestroyFramebufferCalls;
    gFakeState->mDestroyedFramebuffers.push_back(framebuffer);
    if (gFakeState->mPresentationTargetDestroyOwner)
    {
        gFakeState->mPresentationTargetDestroyObservationMade = true;
        gFakeState->mObservedImagesAtPresentationTargetDestroy =
            gFakeState->mPresentationTargetDestroyOwner->hasSwapchainImagesGeneration();
        gFakeState->mObservedFrameSlotAtPresentationTargetDestroy =
            gFakeState->mPresentationTargetDestroyOwner->hasSwapchainFrameSlotGeneration();
    }
    gFakeState->mEvents.push_back(Event::DestroyFramebuffer);
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateShaderModule(VkDevice                        device,
                                                      const VkShaderModuleCreateInfo* create_info,
                                                      const VkAllocationCallbacks*,
                                                      VkShaderModule* shader_module) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || !create_info || !shader_module ||
        create_info->sType != VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO || create_info->codeSize == 0 ||
        !create_info->pCode)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    const std::size_t index = gFakeState->mCreateShaderModuleCalls++;
    gFakeState->mEvents.push_back(Event::CreateShaderModule);
    if (gFakeState->mShaderModuleCreateResult == VK_SUCCESS)
    {
        *shader_module = gFakeState->mNullShaderModule
                             ? VK_NULL_HANDLE
                             : gFakeState->mPresentationShaderModules[index % gFakeState->mPresentationShaderModules.size()];
    }
    return gFakeState->mShaderModuleCreateResult;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyShaderModule(VkDevice,
                                                   VkShaderModule shader_module,
                                                   const VkAllocationCallbacks*) noexcept
{
    if (!gFakeState)
    {
        return;
    }
    ++gFakeState->mDestroyShaderModuleCalls;
    gFakeState->mDestroyedShaderModules.push_back(shader_module);
    gFakeState->mEvents.push_back(Event::DestroyShaderModule);
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreatePipelineLayout(VkDevice                          device,
                                                        const VkPipelineLayoutCreateInfo* create_info,
                                                        const VkAllocationCallbacks*,
                                                        VkPipelineLayout* pipeline_layout) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || !create_info || !pipeline_layout ||
        create_info->sType != VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    ++gFakeState->mCreatePipelineLayoutCalls;
    gFakeState->mEvents.push_back(Event::CreatePipelineLayout);
    if (gFakeState->mPipelineLayoutCreateResult == VK_SUCCESS)
    {
        *pipeline_layout = gFakeState->mNullPipelineLayout ? VK_NULL_HANDLE : gFakeState->mPresentationPipelineLayout;
    }
    return gFakeState->mPipelineLayoutCreateResult;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyPipelineLayout(VkDevice,
                                                     VkPipelineLayout pipeline_layout,
                                                     const VkAllocationCallbacks*) noexcept
{
    if (!gFakeState)
    {
        return;
    }
    ++gFakeState->mDestroyPipelineLayoutCalls;
    gFakeState->mDestroyedPipelineLayouts.push_back(pipeline_layout);
    gFakeState->mEvents.push_back(Event::DestroyPipelineLayout);
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateGraphicsPipelines(VkDevice,
                                                           VkPipelineCache,
                                                           std::uint32_t create_info_count,
                                                           const VkGraphicsPipelineCreateInfo* create_infos,
                                                           const VkAllocationCallbacks*,
                                                           VkPipeline* pipelines) noexcept
{
    if (!gFakeState || create_info_count != 1 || !create_infos || !pipelines ||
        create_infos[0].sType != VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    ++gFakeState->mCreateGraphicsPipelineCalls;
    gFakeState->mEvents.push_back(Event::CreateGraphicsPipeline);
    if (gFakeState->mGraphicsPipelineCreateResult == VK_SUCCESS)
    {
        pipelines[0] = gFakeState->mNullGraphicsPipeline ? VK_NULL_HANDLE : gFakeState->mPresentationPipeline;
    }
    return gFakeState->mGraphicsPipelineCreateResult;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyPipeline(VkDevice,
                                               VkPipeline pipeline,
                                               const VkAllocationCallbacks*) noexcept
{
    if (!gFakeState)
    {
        return;
    }
    ++gFakeState->mDestroyPipelineCalls;
    gFakeState->mDestroyedPipelines.push_back(pipeline);
    if (gFakeState->mPipelineDestroyOwner)
    {
        gFakeState->mPipelineDestroyObservationMade = true;
        gFakeState->mObservedTargetAtPipelineDestroy =
            gFakeState->mPipelineDestroyOwner->hasSwapchainPresentationTargetGeneration();
        gFakeState->mObservedFrameSlotAtPipelineDestroy =
            gFakeState->mPipelineDestroyOwner->hasSwapchainFrameSlotGeneration();
    }
    gFakeState->mEvents.push_back(Event::DestroyPipeline);
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateCommandPool(VkDevice                       device,
                                                     const VkCommandPoolCreateInfo* create_info,
                                                     const VkAllocationCallbacks*   allocation_callbacks,
                                                     VkCommandPool*                 command_pool) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || !create_info || !command_pool)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    ++gFakeState->mCreateCommandPoolCalls;
    gFakeState->mFrameSlotDevice                      = device;
    gFakeState->mCommandPoolCreateInfo                = *create_info;
    gFakeState->mCreateCommandPoolAllocationCallbacks = allocation_callbacks;
    gFakeState->mEvents.push_back(Event::CreateCommandPool);
    const bool upload_transfer                       = create_info->flags == 0;
    gFakeState->mUploadTransferResourcesBeingCreated = upload_transfer;
    if (upload_transfer)
    {
        gFakeState->mUploadTransferCommandPoolCreateInfo = *create_info;
    }
    if (gFakeState->mCommandPoolCreateResult == VK_SUCCESS)
    {
        *command_pool = gFakeState->mNullCommandPool
                            ? VK_NULL_HANDLE
                            : (upload_transfer ? gFakeState->mUploadTransferCommandPool : gFakeState->mCommandPool);
    }
    return gFakeState->mCommandPoolCreateResult;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyCommandPool(VkDevice                     device,
                                                  VkCommandPool                command_pool,
                                                  const VkAllocationCallbacks* allocation_callbacks) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice ||
        (command_pool != gFakeState->mCommandPool && command_pool != gFakeState->mUploadTransferCommandPool))
    {
        return;
    }
    ++gFakeState->mDestroyCommandPoolCalls;
    gFakeState->mDestroyedCommandPools.push_back(command_pool);
    gFakeState->mDestroyCommandPoolAllocationCallbacks = allocation_callbacks;
    if (gFakeState->mFrameSlotDestroyOwner)
    {
        gFakeState->mFrameSlotDestroyObservationMade         = true;
        gFakeState->mObservedImagesAtFrameSlotDestroy        = gFakeState->mFrameSlotDestroyOwner->hasSwapchainImagesGeneration();
        gFakeState->mObservedSwapchainAtFrameSlotDestroy     = gFakeState->mFrameSlotDestroyOwner->hasSwapchainGeneration();
        gFakeState->mObservedConfigurationAtFrameSlotDestroy = gFakeState->mFrameSlotDestroyOwner->hasSwapchainConfigurationGeneration();
        gFakeState->mObservedLogicalAtFrameSlotDestroy       = gFakeState->mFrameSlotDestroyOwner->hasLogicalDeviceGeneration();
        gFakeState->mObservedSurfaceAtFrameSlotDestroy       = gFakeState->mFrameSlotDestroyOwner->hasSurfaceGeneration();
        gFakeState->mObservedPresentationTargetAtFrameSlotDestroy =
            gFakeState->mFrameSlotDestroyOwner->hasSwapchainPresentationTargetGeneration();
    }
    gFakeState->mEvents.push_back(Event::DestroyCommandPool);
    if (command_pool == gFakeState->mUploadTransferCommandPool)
    {
        gFakeState->mEvents.push_back(Event::DestroyUploadTransferCommandPool);
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeAllocateCommandBuffers(VkDevice                           device,
                                                          const VkCommandBufferAllocateInfo* allocate_info,
                                                          VkCommandBuffer*                   command_buffers) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || !allocate_info || !command_buffers)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    ++gFakeState->mAllocateCommandBufferCalls;
    gFakeState->mFrameSlotDevice           = device;
    gFakeState->mCommandBufferAllocateInfo = *allocate_info;
    gFakeState->mEvents.push_back(Event::AllocateCommandBuffer);
    const bool upload_transfer = allocate_info->commandPool == gFakeState->mUploadTransferCommandPool;
    if (upload_transfer)
    {
        gFakeState->mUploadTransferCommandBufferAllocateInfo = *allocate_info;
    }
    if (gFakeState->mCommandBufferAllocateResult == VK_SUCCESS)
    {
        *command_buffers = gFakeState->mNullCommandBuffer
                               ? VK_NULL_HANDLE
                               : (upload_transfer ? gFakeState->mUploadTransferCommandBuffer : gFakeState->mCommandBuffer);
    }
    return gFakeState->mCommandBufferAllocateResult;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateSemaphore(VkDevice                     device,
                                                   const VkSemaphoreCreateInfo* create_info,
                                                   const VkAllocationCallbacks* allocation_callbacks,
                                                   VkSemaphore*                 semaphore) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || !create_info || !semaphore)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    const std::size_t create_index                  = gFakeState->mCreateSemaphoreCalls++;
    gFakeState->mFrameSlotDevice                    = device;
    gFakeState->mSemaphoreCreateInfo                = *create_info;
    gFakeState->mCreateSemaphoreAllocationCallbacks = allocation_callbacks;
    gFakeState->mEvents.push_back(Event::CreateSemaphore);
    if (gFakeState->mSemaphoreCreateResult == VK_SUCCESS)
    {
        *semaphore = gFakeState->mNullSemaphore
                         ? VK_NULL_HANDLE
                         : (create_index % 2 == 0 ? gFakeState->mImageAvailableSemaphore : gFakeState->mPresentationReadySemaphore);
    }
    return gFakeState->mSemaphoreCreateResult;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroySemaphore(VkDevice                     device,
                                                VkSemaphore                  semaphore,
                                                const VkAllocationCallbacks* allocation_callbacks) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice ||
        (semaphore != gFakeState->mImageAvailableSemaphore && semaphore != gFakeState->mPresentationReadySemaphore))
    {
        return;
    }
    ++gFakeState->mDestroySemaphoreCalls;
    gFakeState->mDestroyedSemaphores.push_back(semaphore);
    gFakeState->mDestroySemaphoreAllocationCallbacks = allocation_callbacks;
    gFakeState->mEvents.push_back(Event::DestroySemaphore);
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateFence(VkDevice                     device,
                                               const VkFenceCreateInfo*     create_info,
                                               const VkAllocationCallbacks* allocation_callbacks,
                                               VkFence*                     fence) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || !create_info || !fence)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    ++gFakeState->mCreateFenceCalls;
    gFakeState->mFrameSlotDevice                = device;
    gFakeState->mFenceCreateInfo                = *create_info;
    gFakeState->mCreateFenceAllocationCallbacks = allocation_callbacks;
    gFakeState->mEvents.push_back(Event::CreateFence);
    const bool upload_transfer = gFakeState->mUploadTransferResourcesBeingCreated;
    if (upload_transfer)
    {
        gFakeState->mUploadTransferFenceCreateInfo       = *create_info;
        gFakeState->mUploadTransferResourcesBeingCreated = false;
    }
    const std::size_t frame_slot_create_index = gFakeState->mCreateFrameSlotFenceCalls;
    if (!upload_transfer)
    {
        ++gFakeState->mCreateFrameSlotFenceCalls;
    }
    if (gFakeState->mFenceCreateResult == VK_SUCCESS)
    {
        *fence = gFakeState->mNullFence
                     ? VK_NULL_HANDLE
                     : (upload_transfer
                            ? gFakeState->mUploadTransferFence
                            : (frame_slot_create_index % 2 == 0 ? gFakeState->mSubmissionFence : gFakeState->mPresentCompletionFence));
    }
    return gFakeState->mFenceCreateResult;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyFence(VkDevice device, VkFence fence, const VkAllocationCallbacks* allocation_callbacks) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice ||
        (fence != gFakeState->mSubmissionFence && fence != gFakeState->mPresentCompletionFence &&
         fence != gFakeState->mUploadTransferFence))
    {
        return;
    }
    ++gFakeState->mDestroyFenceCalls;
    gFakeState->mDestroyedFences.push_back(fence);
    gFakeState->mDestroyFenceAllocationCallbacks = allocation_callbacks;
    gFakeState->mEvents.push_back(Event::DestroyFence);
    if (fence == gFakeState->mUploadTransferFence)
    {
        gFakeState->mEvents.push_back(Event::DestroyUploadTransferFence);
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeWaitForFences(VkDevice device, std::uint32_t fence_count, const VkFence* fences, VkBool32 wait_all,
                                                 std::uint64_t timeout) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || fence_count == 0 || fence_count > 2 || !fences)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    for (std::uint32_t index = 0; index < fence_count; ++index)
    {
        if (fences[index] != gFakeState->mSubmissionFence && fences[index] != gFakeState->mPresentCompletionFence &&
            fences[index] != gFakeState->mUploadTransferFence)
        {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
    }
    ++gFakeState->mWaitForFencesCalls;
    gFakeState->mOperationDevice = device;
    gFakeState->mOperationFence  = fences[0];
    gFakeState->mWaitAll         = wait_all;
    gFakeState->mWaitTimeout     = timeout;
    gFakeState->mEvents.push_back(Event::WaitForFences);
    if (gFakeState->mReadbackResetReentryPoint == ReadbackResetReentryPoint::WaitForFences)
    {
        attemptReadbackResetReentry(*gFakeState);
    }
    const std::size_t index = gFakeState->mWaitForFencesResultIndex++;
    return index < gFakeState->mWaitForFencesResults.size() ? gFakeState->mWaitForFencesResults[index] : VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeResetCommandBuffer(VkCommandBuffer command_buffer, VkCommandBufferResetFlags flags) noexcept
{
    if (!gFakeState || command_buffer != gFakeState->mCommandBuffer || flags != 0)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    ++gFakeState->mResetCommandBufferCalls;
    gFakeState->mOperationCommandBuffer = command_buffer;
    gFakeState->mEvents.push_back(Event::ResetCommandBuffer);
    return gFakeState->mResetCommandBufferResult;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeBeginCommandBuffer(VkCommandBuffer command_buffer, const VkCommandBufferBeginInfo* begin_info) noexcept
{
    const bool upload_transfer = gFakeState && command_buffer == gFakeState->mUploadTransferCommandBuffer;
    if (!gFakeState || (command_buffer != gFakeState->mCommandBuffer && command_buffer != gFakeState->mUploadTransferCommandBuffer) ||
        !begin_info || begin_info->sType != VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO || begin_info->pNext ||
        begin_info->flags != (upload_transfer ? VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT : 0) || begin_info->pInheritanceInfo)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (upload_transfer)
    {
        gFakeState->mInstanceOwnerChecksAtUploadTransferBegin = gFakeState->mInstanceOwnerChecks;
        gFakeState->mSurfaceWindowChecksAtUploadTransferBegin = gFakeState->mSurfaceWindowChecks;
        attemptUploadTransferResetReentry(*gFakeState);
    }
    ++gFakeState->mBeginCommandBufferCalls;
    gFakeState->mOperationCommandBuffer = command_buffer;
    gFakeState->mEvents.push_back(Event::BeginCommandBuffer);
    return gFakeState->mBeginCommandBufferResult;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeEndCommandBuffer(VkCommandBuffer command_buffer) noexcept
{
    if (!gFakeState || (command_buffer != gFakeState->mCommandBuffer && command_buffer != gFakeState->mUploadTransferCommandBuffer))
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    ++gFakeState->mEndCommandBufferCalls;
    gFakeState->mOperationCommandBuffer = command_buffer;
    gFakeState->mEvents.push_back(Event::EndCommandBuffer);
    return gFakeState->mEndCommandBufferResult;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeResetFences(VkDevice device, std::uint32_t fence_count, const VkFence* fences) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || fence_count == 0 || fence_count > 2 || !fences)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    for (std::uint32_t index = 0; index < fence_count; ++index)
    {
        if (fences[index] != gFakeState->mSubmissionFence && fences[index] != gFakeState->mPresentCompletionFence)
        {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
    }
    ++gFakeState->mResetFencesCalls;
    gFakeState->mOperationDevice = device;
    gFakeState->mOperationFence  = fences[0];
    gFakeState->mEvents.push_back(Event::ResetFences);
    return gFakeState->mResetFencesResult;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeQueueSubmit(VkQueue             queue,
                                               std::uint32_t       submit_count,
                                               const VkSubmitInfo* submits,
                                               VkFence             fence) noexcept
{
    if (!gFakeState || queue != gFakeState->mQueue || submit_count != 1 || !submits || submits[0].sType != VK_STRUCTURE_TYPE_SUBMIT_INFO ||
        submits[0].pNext ||
        (fence != gFakeState->mSubmissionFence && fence != gFakeState->mPresentCompletionFence &&
         fence != gFakeState->mUploadTransferFence))
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (submits[0].commandBufferCount > 1 ||
        (submits[0].commandBufferCount == 1 &&
         (!submits[0].pCommandBuffers || (submits[0].pCommandBuffers[0] != gFakeState->mCommandBuffer &&
                                          submits[0].pCommandBuffers[0] != gFakeState->mUploadTransferCommandBuffer))))
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    ++gFakeState->mQueueSubmitCalls;
    gFakeState->mSubmittedQueue         = queue;
    gFakeState->mOperationFence         = fence;
    gFakeState->mOperationCommandBuffer = submits[0].commandBufferCount == 1 ? submits[0].pCommandBuffers[0] : VK_NULL_HANDLE;
    gFakeState->mEvents.push_back(Event::QueueSubmit);
    return gFakeState->mQueueSubmitResult;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeAcquireNextImage(VkDevice       device,
                                                    VkSwapchainKHR swapchain,
                                                    std::uint64_t  timeout,
                                                    VkSemaphore    semaphore,
                                                    VkFence        fence,
                                                    std::uint32_t* image_index) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || swapchain != gFakeState->mSwapchain ||
        timeout != VULKAN_SWAPCHAIN_FRAME_ACQUIRE_TIMEOUT_NS || semaphore != gFakeState->mImageAvailableSemaphore ||
        fence != VK_NULL_HANDLE || !image_index)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    ++gFakeState->mAcquireNextImageCalls;
    if (gFakeState->mReadbackResetReentryPoint == ReadbackResetReentryPoint::AcquireNextImage)
    {
        attemptReadbackResetReentry(*gFakeState);
    }
    *image_index = gFakeState->mAcquiredImageIndex;
    return gFakeState->mAcquireNextImageResult;
}

VKAPI_ATTR void VKAPI_CALL fakeCmdPipelineBarrier(VkCommandBuffer      command_buffer,
                                                  VkPipelineStageFlags source_stage,
                                                  VkPipelineStageFlags destination_stage,
                                                  VkDependencyFlags,
                                                  std::uint32_t,
                                                  const VkMemoryBarrier*,
                                                  std::uint32_t                buffer_barrier_count,
                                                  const VkBufferMemoryBarrier* buffer_barriers,
                                                  std::uint32_t                image_barrier_count,
                                                  const VkImageMemoryBarrier*  image_barriers) noexcept
{
    if (!gFakeState || source_stage == 0 || destination_stage == 0)
    {
        return;
    }
    if (command_buffer == gFakeState->mCommandBuffer && image_barrier_count == 1 && image_barriers)
    {
        ++gFakeState->mPipelineBarrierCalls;
    }
    else if (command_buffer == gFakeState->mUploadTransferCommandBuffer && buffer_barrier_count == 1 && buffer_barriers &&
             image_barrier_count == 0)
    {
        ++gFakeState->mPipelineBarrierCalls;
        gFakeState->mBufferBarriers.push_back(buffer_barriers[0]);
        gFakeState->mBufferBarrierStages.emplace_back(source_stage, destination_stage);
    }
}

VKAPI_ATTR void VKAPI_CALL fakeCmdCopyBuffer(VkCommandBuffer     command_buffer,
                                             VkBuffer            source_buffer,
                                             VkBuffer            destination_buffer,
                                             std::uint32_t       region_count,
                                             const VkBufferCopy* regions) noexcept
{
    if (!gFakeState || command_buffer != gFakeState->mUploadTransferCommandBuffer || source_buffer != gFakeState->mUploadSourceBuffer ||
        destination_buffer != gFakeState->mUploadDestinationBuffer || region_count != 1 || !regions)
    {
        return;
    }
    ++gFakeState->mCopyBufferCalls;
    gFakeState->mCopySourceBuffer      = source_buffer;
    gFakeState->mCopyDestinationBuffer = destination_buffer;
    gFakeState->mBufferCopyRegion      = regions[0];
    gFakeState->mEvents.push_back(Event::CopyBuffer);
}

VKAPI_ATTR void VKAPI_CALL fakeCmdClearColorImage(VkCommandBuffer               command_buffer,
                                                  VkImage                       image,
                                                  VkImageLayout                 image_layout,
                                                  const VkClearColorValue*      color,
                                                  std::uint32_t                 range_count,
                                                  const VkImageSubresourceRange* ranges) noexcept
{
    if (!gFakeState || command_buffer != gFakeState->mCommandBuffer || image_layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL ||
        !color || range_count != 1 || !ranges)
    {
        return;
    }
    ++gFakeState->mClearColorImageCalls;
    gFakeState->mClearedImage          = image;
    gFakeState->mClearColorValue       = *color;
    gFakeState->mClearSubresourceRange = ranges[0];
}

VKAPI_ATTR void VKAPI_CALL fakeCmdBeginRenderPass(VkCommandBuffer             command_buffer,
                                                  const VkRenderPassBeginInfo* begin_info,
                                                  VkSubpassContents            contents) noexcept
{
    if (!gFakeState || command_buffer != gFakeState->mCommandBuffer || !begin_info ||
        begin_info->sType != VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO || begin_info->pNext ||
        begin_info->renderPass == VK_NULL_HANDLE || begin_info->framebuffer == VK_NULL_HANDLE ||
        begin_info->renderArea.offset.x != 0 || begin_info->renderArea.offset.y != 0 ||
        begin_info->clearValueCount != 1 || !begin_info->pClearValues || contents != VK_SUBPASS_CONTENTS_INLINE)
    {
        return;
    }
    ++gFakeState->mBeginRenderPassCalls;
    gFakeState->mRenderPassCommandBuffer  = command_buffer;
    gFakeState->mOperationRenderPass      = begin_info->renderPass;
    gFakeState->mOperationFramebuffer     = begin_info->framebuffer;
    gFakeState->mOperationRenderArea      = begin_info->renderArea;
    gFakeState->mRenderPassClearValue     = begin_info->pClearValues[0];
    gFakeState->mOperationSubpassContents = contents;
    gFakeState->mEvents.push_back(Event::BeginRenderPass);
}

VKAPI_ATTR void VKAPI_CALL fakeCmdBindPipeline(VkCommandBuffer     command_buffer,
                                               VkPipelineBindPoint pipeline_bind_point,
                                               VkPipeline          pipeline) noexcept
{
    if (!gFakeState || command_buffer != gFakeState->mCommandBuffer || pipeline == VK_NULL_HANDLE)
    {
        return;
    }
    ++gFakeState->mBindPipelineCalls;
    gFakeState->mOperationCommandBuffer     = command_buffer;
    gFakeState->mOperationPipelineBindPoint = pipeline_bind_point;
    gFakeState->mOperationPipeline          = pipeline;
    gFakeState->mEvents.push_back(Event::BindPipeline);
}

VKAPI_ATTR void VKAPI_CALL fakeCmdSetViewport(VkCommandBuffer   command_buffer,
                                              std::uint32_t     first_viewport,
                                              std::uint32_t     viewport_count,
                                              const VkViewport* viewports) noexcept
{
    if (!gFakeState || command_buffer != gFakeState->mCommandBuffer || first_viewport != 0 || viewport_count != 1 || !viewports)
    {
        return;
    }
    ++gFakeState->mSetViewportCalls;
    gFakeState->mOperationViewport = viewports[0];
    gFakeState->mEvents.push_back(Event::SetViewport);
}

VKAPI_ATTR void VKAPI_CALL fakeCmdSetScissor(VkCommandBuffer command_buffer,
                                             std::uint32_t   first_scissor,
                                             std::uint32_t   scissor_count,
                                             const VkRect2D* scissors) noexcept
{
    if (!gFakeState || command_buffer != gFakeState->mCommandBuffer || first_scissor != 0 || scissor_count != 1 || !scissors)
    {
        return;
    }
    ++gFakeState->mSetScissorCalls;
    gFakeState->mOperationScissor = scissors[0];
    gFakeState->mEvents.push_back(Event::SetScissor);
}

VKAPI_ATTR void VKAPI_CALL fakeCmdDraw(VkCommandBuffer command_buffer,
                                       std::uint32_t   vertex_count,
                                       std::uint32_t   instance_count,
                                       std::uint32_t   first_vertex,
                                       std::uint32_t   first_instance) noexcept
{
    if (!gFakeState || command_buffer != gFakeState->mCommandBuffer)
    {
        return;
    }
    ++gFakeState->mDrawCalls;
    gFakeState->mDrawVertexCount   = vertex_count;
    gFakeState->mDrawInstanceCount = instance_count;
    gFakeState->mDrawFirstVertex   = first_vertex;
    gFakeState->mDrawFirstInstance = first_instance;
    gFakeState->mEvents.push_back(Event::Draw);
}

VKAPI_ATTR void VKAPI_CALL fakeCmdCopyImageToBuffer(VkCommandBuffer          command_buffer,
                                                    VkImage                  source_image,
                                                    VkImageLayout            source_image_layout,
                                                    VkBuffer                 destination_buffer,
                                                    std::uint32_t            region_count,
                                                    const VkBufferImageCopy* regions) noexcept
{
    if (!gFakeState || command_buffer != gFakeState->mCommandBuffer ||
        gFakeState->mAcquiredImageIndex >= gFakeState->mSwapchainImages.size() ||
        source_image != gFakeState->mSwapchainImages[gFakeState->mAcquiredImageIndex] ||
        source_image_layout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL || destination_buffer != gFakeState->mReadbackBuffer ||
        region_count != 1 || !regions)
    {
        return;
    }
    ++gFakeState->mCopyImageToBufferCalls;
}

VKAPI_ATTR void VKAPI_CALL fakeCmdEndRenderPass(VkCommandBuffer command_buffer) noexcept
{
    if (!gFakeState || command_buffer != gFakeState->mCommandBuffer)
    {
        return;
    }
    ++gFakeState->mEndRenderPassCalls;
    gFakeState->mRenderPassCommandBuffer = command_buffer;
    gFakeState->mEvents.push_back(Event::EndRenderPass);
}

VKAPI_ATTR VkResult VKAPI_CALL fakeQueuePresent(VkQueue queue, const VkPresentInfoKHR* present_info) noexcept
{
    if (!gFakeState || queue != gFakeState->mQueue || !present_info || present_info->sType != VK_STRUCTURE_TYPE_PRESENT_INFO_KHR ||
        present_info->waitSemaphoreCount != 1 || !present_info->pWaitSemaphores ||
        present_info->pWaitSemaphores[0] != gFakeState->mPresentationReadySemaphore || present_info->swapchainCount != 1 ||
        !present_info->pSwapchains || present_info->pSwapchains[0] != gFakeState->mSwapchain || !present_info->pImageIndices ||
        present_info->pImageIndices[0] != gFakeState->mAcquiredImageIndex)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    ++gFakeState->mQueuePresentCalls;
    return gFakeState->mQueuePresentResult;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeReleaseSwapchainImages(VkDevice device, const VkReleaseSwapchainImagesInfoKHR* release_info) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || !release_info ||
        release_info->sType != VK_STRUCTURE_TYPE_RELEASE_SWAPCHAIN_IMAGES_INFO_KHR || release_info->swapchain != gFakeState->mSwapchain ||
        release_info->imageIndexCount != 1 || !release_info->pImageIndices ||
        release_info->pImageIndices[0] != gFakeState->mAcquiredImageIndex)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    ++gFakeState->mReleaseSwapchainImagesCalls;
    return gFakeState->mReleaseSwapchainImagesResult;
}

VKAPI_ATTR void VKAPI_CALL fakeGetPhysicalDeviceMemoryProperties(VkPhysicalDevice                  physical_device,
                                                                 VkPhysicalDeviceMemoryProperties* properties) noexcept
{
    if (gFakeState && physical_device == gFakeState->mPhysicalDevice && properties)
    {
        ++gFakeState->mMemoryPropertiesCalls;
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
    ++gFakeState->mCreateBufferCalls;
    gFakeState->mEvents.push_back(Event::CreateBuffer);
    const bool upload_source =
        create_info->size == VULKAN_UPLOAD_SOURCE_BYTE_COUNT && create_info->usage == VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    const bool upload_destination = create_info->size == VULKAN_UPLOAD_SOURCE_BYTE_COUNT &&
                                    create_info->usage == (VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    if (upload_source)
    {
        gFakeState->mUploadSourceBufferCreateInfo = *create_info;
        *buffer                                   = gFakeState->mNullUploadSourceBuffer ? VK_NULL_HANDLE : gFakeState->mUploadSourceBuffer;
    }
    else if (upload_destination)
    {
        gFakeState->mUploadDestinationBufferCreateInfo = *create_info;
        *buffer = gFakeState->mNullUploadDestinationBuffer ? VK_NULL_HANDLE : gFakeState->mUploadDestinationBuffer;
    }
    else
    {
        gFakeState->mReadbackBufferCreateInfo = *create_info;
        *buffer                               = gFakeState->mNullReadbackBuffer ? VK_NULL_HANDLE : gFakeState->mReadbackBuffer;
    }
    if (gFakeState->mBufferCreateResult == VK_SUCCESS && *buffer != VK_NULL_HANDLE)
    {
        gFakeState->mCreatedBuffers.push_back(*buffer);
    }
    return gFakeState->mBufferCreateResult;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks*) noexcept
{
    if (gFakeState && device == gFakeState->mDevice &&
        std::find(gFakeState->mCreatedBuffers.begin(), gFakeState->mCreatedBuffers.end(), buffer) != gFakeState->mCreatedBuffers.end())
    {
        ++gFakeState->mDestroyBufferCalls;
        gFakeState->mDestroyedBuffers.push_back(buffer);
        gFakeState->mEvents.push_back(Event::DestroyBuffer);
        if (buffer == gFakeState->mUploadSourceBuffer)
        {
            gFakeState->mEvents.push_back(Event::DestroyUploadSourceBuffer);
        }
        else if (buffer == gFakeState->mUploadDestinationBuffer)
        {
            gFakeState->mEvents.push_back(Event::DestroyUploadDestinationBuffer);
        }
    }
}

VKAPI_ATTR void VKAPI_CALL fakeGetBufferMemoryRequirements(VkDevice device, VkBuffer buffer, VkMemoryRequirements* requirements) noexcept
{
    if (gFakeState && device == gFakeState->mDevice && requirements &&
        std::find(gFakeState->mCreatedBuffers.begin(), gFakeState->mCreatedBuffers.end(), buffer) != gFakeState->mCreatedBuffers.end())
    {
        ++gFakeState->mGetBufferMemoryRequirementsCalls;
        gFakeState->mNextBufferMemoryKind =
            buffer == gFakeState->mUploadSourceBuffer
                ? BufferMemoryKind::UploadSource
                : (buffer == gFakeState->mUploadDestinationBuffer ? BufferMemoryKind::UploadDestination : BufferMemoryKind::Readback);
        if (gFakeState->mNextBufferMemoryKind == BufferMemoryKind::UploadSource)
        {
            *requirements      = gFakeState->mUploadSourceMemoryRequirements;
            requirements->size = std::max(requirements->size, gFakeState->mUploadSourceBufferCreateInfo.size);
        }
        else if (gFakeState->mNextBufferMemoryKind == BufferMemoryKind::UploadDestination)
        {
            *requirements      = gFakeState->mUploadDestinationMemoryRequirements;
            requirements->size = std::max(requirements->size, gFakeState->mUploadDestinationBufferCreateInfo.size);
        }
        else
        {
            *requirements      = gFakeState->mReadbackMemoryRequirements;
            requirements->size = std::max(requirements->size, gFakeState->mReadbackBufferCreateInfo.size);
        }
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeAllocateMemory(VkDevice                    device,
                                                  const VkMemoryAllocateInfo* allocate_info,
                                                  const VkAllocationCallbacks*,
                                                  VkDeviceMemory* memory) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || !allocate_info || !memory)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    ++gFakeState->mAllocateMemoryCalls;
    gFakeState->mEvents.push_back(Event::AllocateMemory);
    const BufferMemoryKind memory_kind = gFakeState->mNextBufferMemoryKind;
    gFakeState->mNextBufferMemoryKind  = BufferMemoryKind::Readback;
    if (memory_kind == BufferMemoryKind::UploadSource)
    {
        gFakeState->mUploadSourceMemoryAllocateInfo = *allocate_info;
        *memory = gFakeState->mNullUploadSourceMemory ? VK_NULL_HANDLE : gFakeState->mUploadSourceMemory;
    }
    else if (memory_kind == BufferMemoryKind::UploadDestination)
    {
        gFakeState->mUploadDestinationMemoryAllocateInfo = *allocate_info;
        *memory = gFakeState->mNullUploadDestinationMemory ? VK_NULL_HANDLE : gFakeState->mUploadDestinationMemory;
    }
    else
    {
        gFakeState->mReadbackMemoryAllocateInfo = *allocate_info;
        *memory                                 = gFakeState->mNullReadbackMemory ? VK_NULL_HANDLE : gFakeState->mReadbackMemory;
    }
    if (gFakeState->mMemoryAllocateResult == VK_SUCCESS && *memory != VK_NULL_HANDLE)
    {
        gFakeState->mAllocatedMemories.push_back(*memory);
    }
    return gFakeState->mMemoryAllocateResult;
}

VKAPI_ATTR void VKAPI_CALL fakeFreeMemory(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks*) noexcept
{
    if (gFakeState && device == gFakeState->mDevice &&
        std::find(gFakeState->mAllocatedMemories.begin(), gFakeState->mAllocatedMemories.end(), memory) !=
            gFakeState->mAllocatedMemories.end())
    {
        ++gFakeState->mFreeMemoryCalls;
        gFakeState->mFreedMemories.push_back(memory);
        gFakeState->mEvents.push_back(Event::FreeMemory);
        if (memory == gFakeState->mUploadSourceMemory)
        {
            gFakeState->mEvents.push_back(Event::FreeUploadSourceMemory);
        }
        else if (memory == gFakeState->mUploadDestinationMemory)
        {
            gFakeState->mEvents.push_back(Event::FreeUploadDestinationMemory);
        }
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeBindBufferMemory(VkDevice       device,
                                                    VkBuffer       buffer,
                                                    VkDeviceMemory memory,
                                                    VkDeviceSize   memory_offset) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice ||
        !((buffer == gFakeState->mReadbackBuffer && memory == gFakeState->mReadbackMemory) ||
          (buffer == gFakeState->mUploadSourceBuffer && memory == gFakeState->mUploadSourceMemory) ||
          (buffer == gFakeState->mUploadDestinationBuffer && memory == gFakeState->mUploadDestinationMemory)))
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    ++gFakeState->mBindBufferMemoryCalls;
    if (buffer == gFakeState->mUploadSourceBuffer)
    {
        gFakeState->mUploadSourceBoundBuffer = buffer;
        gFakeState->mUploadSourceBoundMemory = memory;
        gFakeState->mUploadSourceBindOffset  = memory_offset;
    }
    else if (buffer == gFakeState->mUploadDestinationBuffer)
    {
        gFakeState->mUploadDestinationBoundBuffer = buffer;
        gFakeState->mUploadDestinationBoundMemory = memory;
        gFakeState->mUploadDestinationBindOffset  = memory_offset;
    }
    else
    {
        gFakeState->mReadbackBoundBuffer = buffer;
        gFakeState->mReadbackBoundMemory = memory;
        gFakeState->mReadbackBindOffset  = memory_offset;
    }
    return gFakeState->mBufferBindResult;
}

VKAPI_ATTR VkResult VKAPI_CALL
    fakeMapMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, VkMemoryMapFlags, void** data) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || !data ||
        std::find(gFakeState->mAllocatedMemories.begin(), gFakeState->mAllocatedMemories.end(), memory) ==
            gFakeState->mAllocatedMemories.end())
    {
        return VK_ERROR_MEMORY_MAP_FAILED;
    }
    ++gFakeState->mMapMemoryCalls;
    gFakeState->mMappedMemory = memory;
    gFakeState->mMappedOffset = offset;
    gFakeState->mMappedSize   = size;
    gFakeState->mEvents.push_back(Event::MapMemory);
    if (memory == gFakeState->mUploadSourceMemory)
    {
        *data = gFakeState->mNullUploadSourceMapping ? nullptr : gFakeState->mUploadSourceMappedStorage.data();
    }
    else
    {
        *data = gFakeState->mNullReadbackMapping
                    ? nullptr
                    : (gFakeState->mReadbackObservationStorage.empty() ? gFakeState->mReadbackMappedStorage.data()
                                                                       : gFakeState->mReadbackObservationStorage.data());
    }
    return gFakeState->mMemoryMapResult;
}

VKAPI_ATTR void VKAPI_CALL fakeUnmapMemory(VkDevice device, VkDeviceMemory memory) noexcept
{
    if (gFakeState && device == gFakeState->mDevice &&
        std::find(gFakeState->mAllocatedMemories.begin(), gFakeState->mAllocatedMemories.end(), memory) !=
            gFakeState->mAllocatedMemories.end())
    {
        ++gFakeState->mUnmapMemoryCalls;
        gFakeState->mEvents.push_back(Event::UnmapMemory);
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeFlushMappedMemoryRanges(VkDevice                   device,
                                                           std::uint32_t              memory_range_count,
                                                           const VkMappedMemoryRange* memory_ranges) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || memory_range_count != 1 || !memory_ranges ||
        std::find(gFakeState->mAllocatedMemories.begin(), gFakeState->mAllocatedMemories.end(), memory_ranges[0].memory) ==
            gFakeState->mAllocatedMemories.end() ||
        memory_ranges[0].memory == gFakeState->mReadbackMemory)
    {
        return VK_ERROR_MEMORY_MAP_FAILED;
    }
    ++gFakeState->mFlushMappedMemoryRangesCalls;
    gFakeState->mFlushedMemoryRanges.push_back(memory_ranges[0]);
    gFakeState->mEvents.push_back(Event::FlushMappedMemoryRanges);
    return gFakeState->mMemoryFlushResult;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL fakeGetDeviceProcAddr(VkDevice device, const char* name) noexcept
{
    if (!gFakeState || device != gFakeState->mDevice || !name)
    {
        return nullptr;
    }

    ++gFakeState->mDeviceProcAddrCalls;
    gFakeState->mDeviceProcAddrDevice = device;
    gFakeState->mDeviceCommandLookups.emplace_back(name);
    if (std::strcmp(name, "vkCreateSwapchainKHR") == 0)
    {
        return gFakeState->mMissing == MissingCommand::CreateSwapchain ? nullptr : eraseFunctionType(fakeCreateSwapchain);
    }
    if (std::strcmp(name, "vkDestroySwapchainKHR") == 0)
    {
        return gFakeState->mMissing == MissingCommand::DestroySwapchain ? nullptr : eraseFunctionType(fakeDestroySwapchain);
    }
    if (std::strcmp(name, "vkGetSwapchainImagesKHR") == 0)
    {
        return gFakeState->mMissing == MissingCommand::GetSwapchainImages ? nullptr : eraseFunctionType(fakeGetSwapchainImages);
    }
    if (std::strcmp(name, "vkCreateImageView") == 0)
    {
        return gFakeState->mMissing == MissingCommand::CreateImageView ? nullptr : eraseFunctionType(fakeCreateImageView);
    }
    if (std::strcmp(name, "vkDestroyImageView") == 0)
    {
        return gFakeState->mMissing == MissingCommand::DestroyImageView ? nullptr : eraseFunctionType(fakeDestroyImageView);
    }
    if (std::strcmp(name, "vkCreateRenderPass") == 0)
    {
        return gFakeState->mMissing == MissingCommand::CreateRenderPass ? nullptr : eraseFunctionType(fakeCreateRenderPass);
    }
    if (std::strcmp(name, "vkDestroyRenderPass") == 0)
    {
        return gFakeState->mMissing == MissingCommand::DestroyRenderPass ? nullptr : eraseFunctionType(fakeDestroyRenderPass);
    }
    if (std::strcmp(name, "vkCreateFramebuffer") == 0)
    {
        return gFakeState->mMissing == MissingCommand::CreateFramebuffer ? nullptr : eraseFunctionType(fakeCreateFramebuffer);
    }
    if (std::strcmp(name, "vkDestroyFramebuffer") == 0)
    {
        return gFakeState->mMissing == MissingCommand::DestroyFramebuffer ? nullptr : eraseFunctionType(fakeDestroyFramebuffer);
    }
    if (std::strcmp(name, "vkCreateShaderModule") == 0)
    {
        return gFakeState->mMissing == MissingCommand::CreateShaderModule ? nullptr : eraseFunctionType(fakeCreateShaderModule);
    }
    if (std::strcmp(name, "vkDestroyShaderModule") == 0)
    {
        return gFakeState->mMissing == MissingCommand::DestroyShaderModule ? nullptr : eraseFunctionType(fakeDestroyShaderModule);
    }
    if (std::strcmp(name, "vkCreatePipelineLayout") == 0)
    {
        return gFakeState->mMissing == MissingCommand::CreatePipelineLayout ? nullptr : eraseFunctionType(fakeCreatePipelineLayout);
    }
    if (std::strcmp(name, "vkDestroyPipelineLayout") == 0)
    {
        return gFakeState->mMissing == MissingCommand::DestroyPipelineLayout ? nullptr : eraseFunctionType(fakeDestroyPipelineLayout);
    }
    if (std::strcmp(name, "vkCreateGraphicsPipelines") == 0)
    {
        return gFakeState->mMissing == MissingCommand::CreateGraphicsPipelines
                   ? nullptr
                   : eraseFunctionType(fakeCreateGraphicsPipelines);
    }
    if (std::strcmp(name, "vkDestroyPipeline") == 0)
    {
        return gFakeState->mMissing == MissingCommand::DestroyPipeline ? nullptr : eraseFunctionType(fakeDestroyPipeline);
    }
    if (std::strcmp(name, "vkCreateCommandPool") == 0)
    {
        return gFakeState->mMissing == MissingCommand::CreateCommandPool ? nullptr : eraseFunctionType(fakeCreateCommandPool);
    }
    if (std::strcmp(name, "vkDestroyCommandPool") == 0)
    {
        return gFakeState->mMissing == MissingCommand::DestroyCommandPool ? nullptr : eraseFunctionType(fakeDestroyCommandPool);
    }
    if (std::strcmp(name, "vkAllocateCommandBuffers") == 0)
    {
        return gFakeState->mMissing == MissingCommand::AllocateCommandBuffers ? nullptr : eraseFunctionType(fakeAllocateCommandBuffers);
    }
    if (std::strcmp(name, "vkCreateSemaphore") == 0)
    {
        return gFakeState->mMissing == MissingCommand::CreateSemaphore ? nullptr : eraseFunctionType(fakeCreateSemaphore);
    }
    if (std::strcmp(name, "vkDestroySemaphore") == 0)
    {
        return gFakeState->mMissing == MissingCommand::DestroySemaphore ? nullptr : eraseFunctionType(fakeDestroySemaphore);
    }
    if (std::strcmp(name, "vkCreateFence") == 0)
    {
        return gFakeState->mMissing == MissingCommand::CreateFence ? nullptr : eraseFunctionType(fakeCreateFence);
    }
    if (std::strcmp(name, "vkDestroyFence") == 0)
    {
        return gFakeState->mMissing == MissingCommand::DestroyFence ? nullptr : eraseFunctionType(fakeDestroyFence);
    }
    if (std::strcmp(name, "vkWaitForFences") == 0)
    {
        return gFakeState->mMissing == MissingCommand::WaitForFences ? nullptr : eraseFunctionType(fakeWaitForFences);
    }
    if (std::strcmp(name, "vkResetCommandBuffer") == 0)
    {
        return gFakeState->mMissing == MissingCommand::ResetCommandBuffer ? nullptr : eraseFunctionType(fakeResetCommandBuffer);
    }
    if (std::strcmp(name, "vkBeginCommandBuffer") == 0)
    {
        return gFakeState->mMissing == MissingCommand::BeginCommandBuffer ? nullptr : eraseFunctionType(fakeBeginCommandBuffer);
    }
    if (std::strcmp(name, "vkEndCommandBuffer") == 0)
    {
        return gFakeState->mMissing == MissingCommand::EndCommandBuffer ? nullptr : eraseFunctionType(fakeEndCommandBuffer);
    }
    if (std::strcmp(name, "vkResetFences") == 0)
    {
        return gFakeState->mMissing == MissingCommand::ResetFences ? nullptr : eraseFunctionType(fakeResetFences);
    }
    if (std::strcmp(name, "vkQueueSubmit") == 0)
    {
        return gFakeState->mMissing == MissingCommand::QueueSubmit ? nullptr : eraseFunctionType(fakeQueueSubmit);
    }
    if (std::strcmp(name, "vkAcquireNextImageKHR") == 0)
    {
        return gFakeState->mMissing == MissingCommand::AcquireNextImage ? nullptr : eraseFunctionType(fakeAcquireNextImage);
    }
    if (std::strcmp(name, "vkCmdPipelineBarrier") == 0)
    {
        return gFakeState->mMissing == MissingCommand::CmdPipelineBarrier ? nullptr : eraseFunctionType(fakeCmdPipelineBarrier);
    }
    if (std::strcmp(name, "vkCmdClearColorImage") == 0)
    {
        return gFakeState->mMissing == MissingCommand::CmdClearColorImage ? nullptr : eraseFunctionType(fakeCmdClearColorImage);
    }
    if (std::strcmp(name, "vkCmdBeginRenderPass") == 0)
    {
        return gFakeState->mMissing == MissingCommand::CmdBeginRenderPass ? nullptr : eraseFunctionType(fakeCmdBeginRenderPass);
    }
    if (std::strcmp(name, "vkCmdBindPipeline") == 0)
    {
        return gFakeState->mMissing == MissingCommand::CmdBindPipeline ? nullptr : eraseFunctionType(fakeCmdBindPipeline);
    }
    if (std::strcmp(name, "vkCmdSetViewport") == 0)
    {
        return gFakeState->mMissing == MissingCommand::CmdSetViewport ? nullptr : eraseFunctionType(fakeCmdSetViewport);
    }
    if (std::strcmp(name, "vkCmdSetScissor") == 0)
    {
        return gFakeState->mMissing == MissingCommand::CmdSetScissor ? nullptr : eraseFunctionType(fakeCmdSetScissor);
    }
    if (std::strcmp(name, "vkCmdDraw") == 0)
    {
        return gFakeState->mMissing == MissingCommand::CmdDraw ? nullptr : eraseFunctionType(fakeCmdDraw);
    }
    if (std::strcmp(name, "vkCmdCopyImageToBuffer") == 0)
    {
        return gFakeState->mMissing == MissingCommand::CmdCopyImageToBuffer ? nullptr : eraseFunctionType(fakeCmdCopyImageToBuffer);
    }
    if (std::strcmp(name, "vkCmdCopyBuffer") == 0)
    {
        return gFakeState->mMissing == MissingCommand::CmdCopyBuffer ? nullptr : eraseFunctionType(fakeCmdCopyBuffer);
    }
    if (std::strcmp(name, "vkCmdEndRenderPass") == 0)
    {
        return gFakeState->mMissing == MissingCommand::CmdEndRenderPass ? nullptr : eraseFunctionType(fakeCmdEndRenderPass);
    }
    if (std::strcmp(name, "vkQueuePresentKHR") == 0)
    {
        return gFakeState->mMissing == MissingCommand::QueuePresent ? nullptr : eraseFunctionType(fakeQueuePresent);
    }
    if (std::strcmp(name, "vkReleaseSwapchainImagesKHR") == 0)
    {
        return gFakeState->mMissing == MissingCommand::ReleaseSwapchainImages ? nullptr : eraseFunctionType(fakeReleaseSwapchainImages);
    }
    if (std::strcmp(name, "vkCreateBuffer") == 0)
    {
        return gFakeState->mMissing == MissingCommand::CreateBuffer ? nullptr : eraseFunctionType(fakeCreateBuffer);
    }
    if (std::strcmp(name, "vkDestroyBuffer") == 0)
    {
        return gFakeState->mMissing == MissingCommand::DestroyBuffer ? nullptr : eraseFunctionType(fakeDestroyBuffer);
    }
    if (std::strcmp(name, "vkGetBufferMemoryRequirements") == 0)
    {
        return gFakeState->mMissing == MissingCommand::GetBufferMemoryRequirements ? nullptr
                                                                                   : eraseFunctionType(fakeGetBufferMemoryRequirements);
    }
    if (std::strcmp(name, "vkAllocateMemory") == 0)
    {
        return gFakeState->mMissing == MissingCommand::AllocateMemory ? nullptr : eraseFunctionType(fakeAllocateMemory);
    }
    if (std::strcmp(name, "vkFreeMemory") == 0)
    {
        return gFakeState->mMissing == MissingCommand::FreeMemory ? nullptr : eraseFunctionType(fakeFreeMemory);
    }
    if (std::strcmp(name, "vkBindBufferMemory") == 0)
    {
        return gFakeState->mMissing == MissingCommand::BindBufferMemory ? nullptr : eraseFunctionType(fakeBindBufferMemory);
    }
    if (std::strcmp(name, "vkMapMemory") == 0)
    {
        return gFakeState->mMissing == MissingCommand::MapMemory ? nullptr : eraseFunctionType(fakeMapMemory);
    }
    if (std::strcmp(name, "vkUnmapMemory") == 0)
    {
        return gFakeState->mMissing == MissingCommand::UnmapMemory ? nullptr : eraseFunctionType(fakeUnmapMemory);
    }
    if (std::strcmp(name, "vkFlushMappedMemoryRanges") == 0)
    {
        return gFakeState->mMissing == MissingCommand::FlushMappedMemoryRanges ? nullptr : eraseFunctionType(fakeFlushMappedMemoryRanges);
    }
    return nullptr;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL fakeGetInstanceProcAddr(VkInstance instance, const char* name) noexcept
{
    if (!gFakeState || !name)
    {
        return nullptr;
    }
    if (std::strcmp(name, "vkCreateInstance") == 0)
    {
        return gFakeState->mMissing == MissingCommand::GlobalCreateInstance ? nullptr : eraseFunctionType(fakeCreateInstance);
    }
    if (std::strcmp(name, "vkEnumerateInstanceExtensionProperties") == 0)
    {
        return gFakeState->mMissing == MissingCommand::GlobalEnumerateExtensions
                   ? nullptr
                   : eraseFunctionType(fakeEnumerateInstanceExtensionProperties);
    }
    if (std::strcmp(name, "vkEnumerateInstanceLayerProperties") == 0)
    {
        return gFakeState->mMissing == MissingCommand::GlobalEnumerateLayers ? nullptr
                                                                             : eraseFunctionType(fakeEnumerateInstanceLayerProperties);
    }
    if (std::strcmp(name, "vkEnumerateInstanceVersion") == 0)
    {
        return gFakeState->mMissing == MissingCommand::GlobalEnumerateVersion ? nullptr : eraseFunctionType(fakeEnumerateInstanceVersion);
    }
    if (instance != gFakeState->mInstance)
    {
        return nullptr;
    }
    if (std::strcmp(name, "vkGetDeviceProcAddr") == 0)
    {
        ++gFakeState->mGetDeviceProcAddrResolutionCalls;
        gFakeState->mGetDeviceProcAddrResolutionInstance = instance;
        return gFakeState->mMissing == MissingCommand::GetDeviceProcAddr ? nullptr : eraseFunctionType(fakeGetDeviceProcAddr);
    }
    if (std::strcmp(name, "vkDestroyInstance") == 0)
    {
        return gFakeState->mMissing == MissingCommand::DestroyInstance ? nullptr : eraseFunctionType(fakeDestroyInstance);
    }
    if (std::strcmp(name, "vkDestroySurfaceKHR") == 0)
    {
        ++gFakeState->mDestroySurfaceResolutionCalls;
        gFakeState->mDestroySurfaceResolutionInstance = instance;
        return gFakeState->mMissing == MissingCommand::DestroySurface ? nullptr : eraseFunctionType(fakeDestroySurface);
    }
    if (std::strcmp(name, "vkEnumeratePhysicalDevices") == 0)
    {
        return eraseFunctionType(fakeEnumeratePhysicalDevices);
    }
    if (std::strcmp(name, "vkGetPhysicalDeviceProperties") == 0)
    {
        return eraseFunctionType(fakeGetPhysicalDeviceProperties);
    }
    if (std::strcmp(name, "vkGetPhysicalDeviceQueueFamilyProperties") == 0)
    {
        return eraseFunctionType(fakeGetPhysicalDeviceQueueFamilyProperties);
    }
    if (std::strcmp(name, "vkGetPhysicalDeviceSurfaceSupportKHR") == 0)
    {
        return eraseFunctionType(fakeGetPhysicalDeviceSurfaceSupport);
    }
    if (std::strcmp(name, "vkEnumerateDeviceExtensionProperties") == 0)
    {
        return eraseFunctionType(fakeEnumerateDeviceExtensionProperties);
    }
    if (std::strcmp(name, "vkGetPhysicalDeviceFeatures2") == 0)
    {
        return gFakeState->mMissing == MissingCommand::GetPhysicalDeviceFeatures2 ? nullptr
                                                                                  : eraseFunctionType(fakeGetPhysicalDeviceFeatures2);
    }
    if (std::strcmp(name, "vkGetPhysicalDeviceFeatures") == 0)
    {
        return gFakeState->mMissing == MissingCommand::GetPhysicalDeviceFeatures ? nullptr
                                                                                 : eraseFunctionType(fakeGetPhysicalDeviceFeatures);
    }
    if (std::strcmp(name, "vkCreateDevice") == 0)
    {
        return gFakeState->mMissing == MissingCommand::CreateDevice ? nullptr : eraseFunctionType(fakeCreateDevice);
    }
    if (std::strcmp(name, "vkDestroyDevice") == 0)
    {
        return gFakeState->mMissing == MissingCommand::DestroyDevice ? nullptr : eraseFunctionType(fakeDestroyDevice);
    }
    if (std::strcmp(name, "vkGetDeviceQueue") == 0)
    {
        return gFakeState->mMissing == MissingCommand::GetDeviceQueue ? nullptr : eraseFunctionType(fakeGetDeviceQueue);
    }
    if (std::strcmp(name, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR") == 0)
    {
        return gFakeState->mMissing == MissingCommand::GetSurfaceCapabilities ? nullptr
                                                                              : eraseFunctionType(fakeGetPhysicalDeviceSurfaceCapabilities);
    }
    if (std::strcmp(name, "vkGetPhysicalDeviceSurfaceFormatsKHR") == 0)
    {
        return gFakeState->mMissing == MissingCommand::GetSurfaceFormats ? nullptr : eraseFunctionType(fakeGetPhysicalDeviceSurfaceFormats);
    }
    if (std::strcmp(name, "vkGetPhysicalDeviceSurfacePresentModesKHR") == 0)
    {
        return gFakeState->mMissing == MissingCommand::GetSurfacePresentModes ? nullptr
                                                                              : eraseFunctionType(fakeGetPhysicalDeviceSurfacePresentModes);
    }
    if (std::strcmp(name, "vkGetPhysicalDeviceFormatProperties") == 0)
    {
        return gFakeState->mMissing == MissingCommand::GetPhysicalDeviceFormatProperties
                   ? nullptr
                   : eraseFunctionType(fakeGetPhysicalDeviceFormatProperties);
    }
    if (std::strcmp(name, "vkGetPhysicalDeviceMemoryProperties") == 0)
    {
        return gFakeState->mMissing == MissingCommand::GetPhysicalDeviceMemoryProperties
                   ? nullptr
                   : eraseFunctionType(fakeGetPhysicalDeviceMemoryProperties);
    }
    if (std::strcmp(name, "vkCreateDebugUtilsMessengerEXT") == 0)
    {
        return gFakeState->mMissing == MissingCommand::CreateDebugMessenger ? nullptr : eraseFunctionType(fakeCreateDebugUtilsMessenger);
    }
    if (std::strcmp(name, "vkDestroyDebugUtilsMessengerEXT") == 0)
    {
        return gFakeState->mMissing == MissingCommand::DestroyDebugMessenger ? nullptr : eraseFunctionType(fakeDestroyDebugUtilsMessenger);
    }
    return nullptr;
}

bool generationIsCurrent(void* userdata, std::uint64_t generation) noexcept
{
    auto& state = *static_cast<FakeState*>(userdata);
    ++state.mGenerationChecks;
    return generation == 42 && state.mGenerationCurrent &&
           (state.mFailGenerationCheck == 0 || state.mGenerationChecks != state.mFailGenerationCheck);
}

VulkanWindowGenerationCheck generationCheck(FakeState& state) noexcept
{
    return { &state, generationIsCurrent };
}

bool instanceOwnerIsCurrent(void* userdata, const VulkanInstanceGeneration& generation) noexcept
{
    auto& state = *static_cast<FakeState*>(userdata);
    ++state.mInstanceOwnerChecks;
    if (state.mMutationOwner && state.mResetFrameSlotOnInstanceOwnerCheck == state.mInstanceOwnerChecks)
    {
        state.mMutationOwner->resetSwapchainFrameSlotGeneration();
    }
    return state.mExpectedInstanceOwner == &generation && state.mInstanceOwnerCurrent &&
           (state.mFailInstanceOwnerCheck == 0 || state.mInstanceOwnerChecks != state.mFailInstanceOwnerCheck);
}

bool surfaceWindowIsCurrent(void* userdata, std::uint64_t generation) noexcept
{
    auto& state = *static_cast<FakeState*>(userdata);
    ++state.mSurfaceWindowChecks;
    return generation == 42 && state.mSurfaceWindowCurrent &&
           (state.mFailSurfaceWindowCheck == 0 || state.mSurfaceWindowChecks != state.mFailSurfaceWindowCheck);
}

VulkanSurfaceCreateOutcome createSurface(void*                        userdata,
                                         VkInstance                   instance,
                                         const VkAllocationCallbacks* allocation_callbacks,
                                         VkSurfaceKHR*                surface) noexcept
{
    auto& state = *static_cast<FakeState*>(userdata);
    ++state.mCreateSurfaceCalls;
    state.mCreateSurfaceInstance            = instance;
    state.mCreateSurfaceAllocationCallbacks = allocation_callbacks;
    state.mEvents.push_back(Event::CreateSurface);

    if (surface)
    {
        const bool failed = state.mSurfacePlatformFailure || state.mSurfaceResult != VK_SUCCESS;
        *surface          = state.mNullSurface || (failed && !state.mPoisonSurfaceOutput) ? VK_NULL_HANDLE : state.mSurface;
    }
    if (state.mSurfacePlatformFailure)
    {
        return VulkanSurfacePlatformFailure{};
    }
    return state.mSurfaceResult;
}

const std::vector<std::string> REQUIRED_SURFACE{ VK_KHR_SURFACE_EXTENSION_NAME };
constexpr char                 PORTABILITY_EXTENSION[] = "VK_KHR_portability_enumeration";

VulkanInstanceRequest makeRequest(FakeState&                    state,
                                  VulkanInstanceValidationMode  validation_mode     = VulkanInstanceValidationMode::Disabled,
                                  VulkanInstancePortabilityMode portability_mode    = VulkanInstancePortabilityMode::Disabled,
                                  std::span<const std::string>  required_extensions = REQUIRED_SURFACE) noexcept
{
    return { fakeGetInstanceProcAddr, required_extensions, 42, generationCheck(state), validation_mode, portability_mode };
}

VulkanSurfaceRequest makeSurfaceRequest(FakeState& state, VulkanInstanceGeneration& owner) noexcept
{
    state.mExpectedInstanceOwner = &owner;
    return { 42, { &state, instanceOwnerIsCurrent }, { &state, surfaceWindowIsCurrent }, { &state, createSurface } };
}

VulkanPresentationDeviceRequest makePresentationDeviceRequest(FakeState& state, VulkanInstanceGeneration& owner) noexcept
{
    state.mExpectedInstanceOwner = &owner;
    return { 42, { &state, instanceOwnerIsCurrent }, { &state, surfaceWindowIsCurrent } };
}

VulkanLogicalDeviceRequest makeLogicalDeviceRequest(FakeState& state, VulkanInstanceGeneration& owner) noexcept
{
    state.mExpectedInstanceOwner = &owner;
    return { 42, { &state, instanceOwnerIsCurrent }, { &state, surfaceWindowIsCurrent } };
}

VulkanUploadSourceDescription makeUploadSourceDescription() noexcept
{
    VulkanUploadSourceDescription description{ { 7, 3 } };
    for (std::size_t index = 0; index < description.mBytes.size(); ++index)
    {
        description.mBytes[index] = static_cast<std::uint8_t>(index * 5 + 1);
    }
    return description;
}

VulkanUploadSourceRequest makeUploadSourceRequest(FakeState&                    state,
                                                  VulkanInstanceGeneration&     owner,
                                                  VulkanUploadSourceDescription description = makeUploadSourceDescription()) noexcept
{
    state.mExpectedInstanceOwner = &owner;
    return { 42, description, { &state, instanceOwnerIsCurrent }, { &state, surfaceWindowIsCurrent } };
}

VulkanUploadDestinationRequest makeUploadDestinationRequest(
    FakeState&                    state,
    VulkanInstanceGeneration&     owner,
    VulkanUploadSourceDescription description = makeUploadSourceDescription()) noexcept
{
    state.mExpectedInstanceOwner = &owner;
    return { 42, description, { &state, instanceOwnerIsCurrent }, { &state, surfaceWindowIsCurrent } };
}

VulkanUploadTransferRequest makeUploadTransferRequest(FakeState&                    state,
                                                      VulkanInstanceGeneration&     owner,
                                                      VulkanUploadSourceDescription description = makeUploadSourceDescription()) noexcept
{
    state.mExpectedInstanceOwner = &owner;
    return { 42, description, { &state, instanceOwnerIsCurrent }, { &state, surfaceWindowIsCurrent } };
}

VulkanUploadTransferOperationRequest makeUploadTransferOperationRequest(
    FakeState&                    state,
    VulkanInstanceGeneration&     owner,
    VulkanUploadSourceDescription description = makeUploadSourceDescription()) noexcept
{
    state.mExpectedInstanceOwner = &owner;
    return { 42, description, { &state, instanceOwnerIsCurrent }, { &state, surfaceWindowIsCurrent } };
}

VulkanSwapchainConfigurationRequest makeSwapchainConfigurationRequest(FakeState&                state,
                                                                      VulkanInstanceGeneration& owner,
                                                                      VkExtent2D                drawable_extent = { 800, 600 }) noexcept
{
    state.mExpectedInstanceOwner = &owner;
    return { 42, drawable_extent, { &state, instanceOwnerIsCurrent }, { &state, surfaceWindowIsCurrent } };
}

VulkanSwapchainRequest makeSwapchainRequest(FakeState&                state,
                                            VulkanInstanceGeneration& owner,
                                            VkExtent2D                drawable_extent = { 800, 600 }) noexcept
{
    state.mExpectedInstanceOwner = &owner;
    return { 42, drawable_extent, { &state, instanceOwnerIsCurrent }, { &state, surfaceWindowIsCurrent } };
}

VulkanSwapchainImagesRequest makeSwapchainImagesRequest(FakeState&                state,
                                                        VulkanInstanceGeneration& owner,
                                                        VkExtent2D                drawable_extent = { 800, 600 }) noexcept
{
    state.mExpectedInstanceOwner = &owner;
    return { 42, drawable_extent, { &state, instanceOwnerIsCurrent }, { &state, surfaceWindowIsCurrent } };
}

VulkanSwapchainPresentationTargetRequest makeSwapchainPresentationTargetRequest(
    FakeState&                state,
    VulkanInstanceGeneration& owner,
    VkExtent2D                drawable_extent = { 800, 600 }) noexcept
{
    state.mExpectedInstanceOwner = &owner;
    return { 42, drawable_extent, { &state, instanceOwnerIsCurrent }, { &state, surfaceWindowIsCurrent } };
}

VulkanSwapchainPresentationPipelineRequest makeSwapchainPresentationPipelineRequest(
    FakeState&                state,
    VulkanInstanceGeneration& owner,
    VkExtent2D                drawable_extent = { 800, 600 }) noexcept
{
    state.mExpectedInstanceOwner = &owner;
    return { 42, drawable_extent, { &state, instanceOwnerIsCurrent }, { &state, surfaceWindowIsCurrent } };
}

VulkanSwapchainReadbackRequest makeSwapchainReadbackRequest(FakeState&                state,
                                                            VulkanInstanceGeneration& owner,
                                                            VkExtent2D                drawable_extent = { 800, 600 }) noexcept
{
    state.mExpectedInstanceOwner = &owner;
    return { 42, drawable_extent, { &state, instanceOwnerIsCurrent }, { &state, surfaceWindowIsCurrent } };
}

VulkanSwapchainFrameSlotRequest makeSwapchainFrameSlotRequest(FakeState&                state,
                                                              VulkanInstanceGeneration& owner,
                                                              VkExtent2D                drawable_extent = { 800, 600 }) noexcept
{
    state.mExpectedInstanceOwner = &owner;
    return { 42, drawable_extent, { &state, instanceOwnerIsCurrent }, { &state, surfaceWindowIsCurrent } };
}

VulkanSwapchainFrameSlotOperationRequest makeSwapchainFrameSlotOperationRequest(FakeState&                state,
                                                                                VulkanInstanceGeneration& owner,
                                                                                VkExtent2D drawable_extent = { 800, 600 }) noexcept
{
    state.mExpectedInstanceOwner = &owner;
    return { 42, drawable_extent, { &state, instanceOwnerIsCurrent }, { &state, surfaceWindowIsCurrent } };
}

VulkanSwapchainChainRebuildRequest makeSwapchainChainRebuildRequest(
    FakeState&                    state,
    VulkanInstanceGeneration&    owner,
    std::optional<VkExtent2D>   drawable_extent = VkExtent2D{ 800, 600 }) noexcept
{
    state.mExpectedInstanceOwner = &owner;
    return { 42, drawable_extent, { &state, instanceOwnerIsCurrent }, { &state, surfaceWindowIsCurrent } };
}

enum class PostPublicationFrameSlotMutation : std::uint8_t
{
    Pending,
    DeviceLost
};

struct PostPublicationMutationContext
{
    FakeState*                                         mState                = nullptr;
    VulkanInstanceGeneration*                          mOwner                = nullptr;
    VkExtent2D                                         mTargetExtent{};
    PostPublicationFrameSlotMutation                   mMutation             = PostPublicationFrameSlotMutation::Pending;
    bool                                               mCurrentAfterMutation = true;
    bool                                               mMutated              = false;
    std::optional<VulkanSwapchainFrameSlotDisposition> mObservedDisposition;
};

bool exactMutationOwnerIsCurrent(void* userdata, const VulkanInstanceGeneration& generation) noexcept
{
    return userdata == &generation;
}

bool exactMutationWindowIsCurrent(void* userdata, std::uint64_t native_window_generation) noexcept
{
    const auto* owner = static_cast<const VulkanInstanceGeneration*>(userdata);
    return owner && native_window_generation != 0 && owner->nativeWindowGeneration() == native_window_generation;
}

VulkanSwapchainFrameSlotOperationRequest makeExactFrameSlotOperationRequest(VulkanInstanceGeneration& owner) noexcept
{
    return { owner.nativeWindowGeneration(),
             owner.swapchainDrawableExtent(),
             { &owner, exactMutationOwnerIsCurrent },
             { &owner, exactMutationWindowIsCurrent } };
}

void prepareReadbackObservationStorage(FakeState& state, VkExtent2D extent, std::byte fill = std::byte{ 0xa5 })
{
    const std::size_t byte_count = static_cast<std::size_t>(extent.width) * static_cast<std::size_t>(extent.height) * 4;
    state.mReadbackObservationStorage.assign(byte_count, fill);
    state.mReadbackMemoryRequirements.size = static_cast<VkDeviceSize>(byte_count);
}

struct ClearColorMutationContext
{
    VulkanInstanceGeneration*        mOwner  = nullptr;
    VulkanSwapchainFrameClearColor*  mSource = nullptr;
    std::size_t                      mOwnerChecks = 0;
};

bool clearColorMutationOwnerIsCurrent(void* userdata, const VulkanInstanceGeneration& generation) noexcept
{
    auto* context = static_cast<ClearColorMutationContext*>(userdata);
    if (!context || context->mOwner != &generation || !context->mSource)
    {
        return false;
    }
    ++context->mOwnerChecks;
    if (context->mOwnerChecks == 1)
    {
        context->mSource->mRgba = { 1.0f, 1.0f, 1.0f, 1.0f };
    }
    return true;
}

bool clearColorMutationWindowIsCurrent(void* userdata, std::uint64_t native_window_generation) noexcept
{
    const auto* context = static_cast<const ClearColorMutationContext*>(userdata);
    return context && context->mOwner && native_window_generation == context->mOwner->nativeWindowGeneration();
}

struct PresentationTargetReplacementContext
{
    FakeState*                        mState = nullptr;
    VulkanInstanceGeneration*         mOwner = nullptr;
    std::size_t                       mOwnerChecks = 0;
    bool                              mResetSucceeded = false;
    bool                              mReplacementSucceeded = false;
};

bool presentationTargetReplacementOwnerIsCurrent(void* userdata, const VulkanInstanceGeneration& generation) noexcept
{
    auto* context = static_cast<PresentationTargetReplacementContext*>(userdata);
    if (!context || !context->mState || !context->mOwner || context->mOwner != &generation)
    {
        return false;
    }
    ++context->mOwnerChecks;
    if (context->mOwnerChecks == 2)
    {
        const VkExtent2D extent = context->mOwner->swapchainDrawableExtent();
        context->mResetSucceeded = context->mOwner->resetSwapchainPresentationTargetGeneration();
        if (context->mResetSucceeded)
        {
            const VulkanSwapchainPresentationTargetRequest target_request{
                context->mOwner->nativeWindowGeneration(),
                extent,
                { context->mOwner, exactMutationOwnerIsCurrent },
                { context->mOwner, exactMutationWindowIsCurrent }
            };
            const VulkanSwapchainFrameSlotRequest frame_slot_request{
                context->mOwner->nativeWindowGeneration(),
                extent,
                { context->mOwner, exactMutationOwnerIsCurrent },
                { context->mOwner, exactMutationWindowIsCurrent }
            };
            context->mReplacementSucceeded =
                !context->mOwner->acquireSwapchainPresentationTargetGeneration(target_request) &&
                !context->mOwner->acquireSwapchainFrameSlotGeneration(frame_slot_request);
        }
    }
    return true;
}

bool presentationTargetReplacementWindowIsCurrent(void* userdata, std::uint64_t native_window_generation) noexcept
{
    const auto* context = static_cast<const PresentationTargetReplacementContext*>(userdata);
    return context && context->mOwner && native_window_generation == context->mOwner->nativeWindowGeneration();
}

struct PresentationPipelineReplacementContext
{
    FakeState*                mState                = nullptr;
    VulkanInstanceGeneration* mOwner                = nullptr;
    std::size_t               mOwnerChecks          = 0;
    bool                      mResetSucceeded       = false;
    bool                      mReplacementSucceeded = false;
};

bool presentationPipelineReplacementOwnerIsCurrent(void* userdata, const VulkanInstanceGeneration& generation) noexcept
{
    auto* context = static_cast<PresentationPipelineReplacementContext*>(userdata);
    if (!context || !context->mState || !context->mOwner || context->mOwner != &generation)
    {
        return false;
    }
    ++context->mOwnerChecks;
    if (context->mOwnerChecks == 2)
    {
        const VkExtent2D extent  = context->mOwner->swapchainDrawableExtent();
        context->mResetSucceeded = context->mOwner->resetSwapchainPresentationPipelineGeneration();
        if (context->mResetSucceeded)
        {
            context->mReplacementSucceeded = !context->mOwner->acquireSwapchainPresentationPipelineGeneration(
                                                 makeSwapchainPresentationPipelineRequest(*context->mState, *context->mOwner, extent)) &&
                                             !context->mOwner->acquireSwapchainFrameSlotGeneration(
                                                 makeSwapchainFrameSlotRequest(*context->mState, *context->mOwner, extent));
        }
    }
    return true;
}

bool presentationPipelineReplacementWindowIsCurrent(void* userdata, std::uint64_t native_window_generation) noexcept
{
    const auto* context = static_cast<const PresentationPipelineReplacementContext*>(userdata);
    return context && context->mOwner && native_window_generation == context->mOwner->nativeWindowGeneration();
}

struct FrameSlotReplacementContext
{
    FakeState*                mState                = nullptr;
    VulkanInstanceGeneration* mOwner                = nullptr;
    std::size_t               mOwnerChecks          = 0;
    bool                      mResetSucceeded       = false;
    bool                      mReplacementSucceeded = false;
};

bool frameSlotReplacementOwnerIsCurrent(void* userdata, const VulkanInstanceGeneration& generation) noexcept
{
    auto* context = static_cast<FrameSlotReplacementContext*>(userdata);
    if (!context || !context->mState || !context->mOwner || context->mOwner != &generation)
    {
        return false;
    }
    ++context->mOwnerChecks;
    if (context->mOwnerChecks == 2)
    {
        const VkExtent2D extent  = context->mOwner->swapchainDrawableExtent();
        context->mResetSucceeded = context->mOwner->resetSwapchainFrameSlotGeneration();
        if (context->mResetSucceeded)
        {
            context->mReplacementSucceeded = !context->mOwner->acquireSwapchainFrameSlotGeneration(
                makeSwapchainFrameSlotRequest(*context->mState, *context->mOwner, extent));
        }
    }
    return true;
}

bool frameSlotReplacementWindowIsCurrent(void* userdata, std::uint64_t native_window_generation) noexcept
{
    const auto* context = static_cast<const FrameSlotReplacementContext*>(userdata);
    return context && context->mOwner && native_window_generation == context->mOwner->nativeWindowGeneration();
}

struct OwnerResetDuringOperationContext
{
    VulkanInstanceGeneration* mOwner            = nullptr;
    std::uint64_t             mWindowGeneration = 0;
    std::size_t               mOwnerChecks      = 0;
    std::size_t               mWindowChecks     = 0;
    bool                      mResetSucceeded   = false;
};

bool ownerResetDuringOperationIsCurrent(void* userdata, const VulkanInstanceGeneration& generation) noexcept
{
    auto* context = static_cast<OwnerResetDuringOperationContext*>(userdata);
    if (!context || context->mOwner != &generation)
    {
        return false;
    }
    ++context->mOwnerChecks;
    if (context->mOwnerChecks == 2)
    {
        context->mResetSucceeded = context->mOwner->reset();
    }
    return true;
}

bool ownerResetDuringOperationWindowIsCurrent(void* userdata, std::uint64_t native_window_generation) noexcept
{
    auto* context = static_cast<OwnerResetDuringOperationContext*>(userdata);
    if (!context)
    {
        return false;
    }
    ++context->mWindowChecks;
    return native_window_generation == context->mWindowGeneration;
}

bool postPublicationMutationOwnerIsCurrent(void* userdata, const VulkanInstanceGeneration& generation) noexcept
{
    auto* context = static_cast<PostPublicationMutationContext*>(userdata);
    if (!context || !context->mState || !context->mOwner || context->mOwner != &generation)
    {
        return false;
    }

    const VkExtent2D extent = generation.swapchainDrawableExtent();
    if (!context->mMutated && generation.hasSwapchainFrameSlotGeneration() &&
        extent.width == context->mTargetExtent.width && extent.height == context->mTargetExtent.height)
    {
        context->mMutated = true;
        context->mState->mWaitForFencesResultIndex = 0;
        context->mState->mWaitForFencesResults[0] =
            context->mMutation == PostPublicationFrameSlotMutation::Pending ? VK_SUCCESS : VK_ERROR_DEVICE_LOST;
        context->mState->mWaitForFencesResults[1] = VK_TIMEOUT;
        (void)context->mOwner->roundTripEmptySwapchainFrameSlot(makeExactFrameSlotOperationRequest(*context->mOwner));
        context->mObservedDisposition = context->mOwner->swapchainFrameSlotDisposition();
        return context->mCurrentAfterMutation;
    }
    return true;
}

bool postPublicationMutationWindowIsCurrent(void* userdata, std::uint64_t native_window_generation) noexcept
{
    const auto* context = static_cast<const PostPublicationMutationContext*>(userdata);
    return context && context->mOwner && native_window_generation != 0 &&
           context->mOwner->nativeWindowGeneration() == native_window_generation;
}

VulkanSwapchainChainRebuildRequest makePostPublicationMutationRequest(PostPublicationMutationContext& context) noexcept
{
    return { context.mOwner ? context.mOwner->nativeWindowGeneration() : 0,
             context.mTargetExtent,
             { &context, postPublicationMutationOwnerIsCurrent },
             { &context, postPublicationMutationWindowIsCurrent } };
}

struct ReentrantSwapchainPublicationContext
{
    FakeState*                         mState               = nullptr;
    VulkanInstanceGeneration*          mOwner               = nullptr;
    VkExtent2D                         mTargetExtent{};
    PostPublicationFrameSlotMutation   mMutation            = PostPublicationFrameSlotMutation::Pending;
    std::size_t                        mPublishOnOwnerCheck  = 0;
    std::size_t                        mOwnerChecks          = 0;
    VkSwapchainKHR                     mReplacementSwapchain = VK_NULL_HANDLE;
    VkSwapchainKHR                     mPublishedSwapchain   = VK_NULL_HANDLE;
    bool                               mAttempted            = false;
    bool                               mPublished            = false;
    std::optional<VulkanSwapchainFrameSlotDisposition> mObservedDisposition;
};

bool reentrantSwapchainPublicationOwnerIsCurrent(void* userdata, const VulkanInstanceGeneration& generation) noexcept
{
    auto* context = static_cast<ReentrantSwapchainPublicationContext*>(userdata);
    if (!context || !context->mState || !context->mOwner || context->mOwner != &generation)
    {
        return false;
    }

    ++context->mOwnerChecks;
    const VkExtent2D extent = generation.swapchainDrawableExtent();
    if (!context->mAttempted && context->mOwnerChecks == context->mPublishOnOwnerCheck &&
        generation.hasSwapchainConfigurationGeneration() && !generation.hasSwapchainGeneration() &&
        extent.width == context->mTargetExtent.width && extent.height == context->mTargetExtent.height)
    {
        context->mAttempted       = true;
        context->mState->mSwapchain = context->mReplacementSwapchain;
        const VulkanInstanceOwnerCheck owner_check{ context->mOwner, exactMutationOwnerIsCurrent };
        const VulkanWindowGenerationCheck window_check{ context->mOwner, exactMutationWindowIsCurrent };
        const std::uint64_t native_window_generation = context->mOwner->nativeWindowGeneration();

        const VulkanSwapchainRequest swapchain_request{
            native_window_generation, context->mTargetExtent, owner_check, window_check
        };
        if (context->mOwner->acquireSwapchainGeneration(swapchain_request))
        {
            return true;
        }

        const VulkanSwapchainImagesRequest images_request{
            native_window_generation, context->mTargetExtent, owner_check, window_check
        };
        if (context->mOwner->acquireSwapchainImagesGeneration(images_request))
        {
            return true;
        }

        const VulkanSwapchainPresentationTargetRequest presentation_target_request{
            native_window_generation, context->mTargetExtent, owner_check, window_check
        };
        if (context->mOwner->acquireSwapchainPresentationTargetGeneration(presentation_target_request))
        {
            return true;
        }

        const VulkanSwapchainFrameSlotRequest frame_slot_request{
            native_window_generation, context->mTargetExtent, owner_check, window_check
        };
        if (context->mOwner->acquireSwapchainFrameSlotGeneration(frame_slot_request))
        {
            return true;
        }

        context->mPublishedSwapchain = context->mOwner->swapchain();
        context->mPublished = context->mPublishedSwapchain == context->mReplacementSwapchain;
        context->mState->mWaitForFencesResultIndex = 0;
        context->mState->mWaitForFencesResults[0] =
            context->mMutation == PostPublicationFrameSlotMutation::Pending ? VK_SUCCESS : VK_ERROR_DEVICE_LOST;
        context->mState->mWaitForFencesResults[1] = VK_TIMEOUT;
        (void)context->mOwner->roundTripEmptySwapchainFrameSlot(makeExactFrameSlotOperationRequest(*context->mOwner));
        context->mObservedDisposition = context->mOwner->swapchainFrameSlotDisposition();
    }
    return true;
}

VulkanSwapchainChainRebuildRequest makeReentrantSwapchainPublicationRequest(
    ReentrantSwapchainPublicationContext& context) noexcept
{
    return { context.mOwner ? context.mOwner->nativeWindowGeneration() : 0,
             context.mTargetExtent,
             { &context, reentrantSwapchainPublicationOwnerIsCurrent },
             { context.mOwner, exactMutationWindowIsCurrent } };
}

struct ReentrantConfigurationPublicationContext
{
    FakeState*                                       mState                = nullptr;
    VulkanInstanceGeneration*                        mOwner                = nullptr;
    VkExtent2D                                       mTargetExtent{};
    PostPublicationFrameSlotMutation                 mMutation             = PostPublicationFrameSlotMutation::Pending;
    std::size_t                                      mPublishOnOwnerCheck  = 0;
    std::size_t                                      mOwnerChecks          = 0;
    VkSwapchainKHR                                   mReplacementSwapchain = VK_NULL_HANDLE;
    VkFormat                                         mPublishedFormat      = VK_FORMAT_UNDEFINED;
    bool                                             mAttempted            = false;
    bool                                             mPublished            = false;
    std::optional<VulkanSwapchainFrameSlotDisposition> mObservedDisposition;
};

bool reentrantConfigurationPublicationOwnerIsCurrent(void* userdata, const VulkanInstanceGeneration& generation) noexcept
{
    auto* context = static_cast<ReentrantConfigurationPublicationContext*>(userdata);
    if (!context || !context->mState || !context->mOwner || context->mOwner != &generation)
    {
        return false;
    }

    ++context->mOwnerChecks;
    if (!context->mAttempted && context->mOwnerChecks == context->mPublishOnOwnerCheck &&
        !generation.hasSwapchainConfigurationGeneration() && !context->mState->mSwapchainFormats.empty())
    {
        context->mAttempted                         = true;
        context->mState->mSwapchain                 = context->mReplacementSwapchain;
        context->mState->mSwapchainFormats.front().format = context->mPublishedFormat;
        const VulkanInstanceOwnerCheck owner_check{ context->mOwner, exactMutationOwnerIsCurrent };
        const VulkanWindowGenerationCheck window_check{ context->mOwner, exactMutationWindowIsCurrent };
        const std::uint64_t native_window_generation = context->mOwner->nativeWindowGeneration();

        const VulkanSwapchainConfigurationRequest configuration_request{
            native_window_generation, context->mTargetExtent, owner_check, window_check
        };
        if (context->mOwner->acquireSwapchainConfigurationGeneration(configuration_request))
        {
            return true;
        }

        const VulkanSwapchainRequest swapchain_request{
            native_window_generation, context->mTargetExtent, owner_check, window_check
        };
        if (context->mOwner->acquireSwapchainGeneration(swapchain_request))
        {
            return true;
        }

        const VulkanSwapchainImagesRequest images_request{
            native_window_generation, context->mTargetExtent, owner_check, window_check
        };
        if (context->mOwner->acquireSwapchainImagesGeneration(images_request))
        {
            return true;
        }

        const VulkanSwapchainPresentationTargetRequest presentation_target_request{
            native_window_generation, context->mTargetExtent, owner_check, window_check
        };
        if (context->mOwner->acquireSwapchainPresentationTargetGeneration(presentation_target_request))
        {
            return true;
        }

        const VulkanSwapchainFrameSlotRequest frame_slot_request{
            native_window_generation, context->mTargetExtent, owner_check, window_check
        };
        if (context->mOwner->acquireSwapchainFrameSlotGeneration(frame_slot_request))
        {
            return true;
        }

        context->mPublished = context->mOwner->swapchain() == context->mReplacementSwapchain &&
                              context->mOwner->swapchainSurfaceFormat().format == context->mPublishedFormat;
        context->mState->mWaitForFencesResultIndex = 0;
        context->mState->mWaitForFencesResults[0] =
            context->mMutation == PostPublicationFrameSlotMutation::Pending ? VK_SUCCESS : VK_ERROR_DEVICE_LOST;
        context->mState->mWaitForFencesResults[1] = VK_TIMEOUT;
        (void)context->mOwner->roundTripEmptySwapchainFrameSlot(makeExactFrameSlotOperationRequest(*context->mOwner));
        context->mObservedDisposition = context->mOwner->swapchainFrameSlotDisposition();
    }
    return true;
}

VulkanSwapchainChainRebuildRequest makeReentrantConfigurationPublicationRequest(
    ReentrantConfigurationPublicationContext& context) noexcept
{
    return { context.mOwner ? context.mOwner->nativeWindowGeneration() : 0,
             context.mTargetExtent,
             { &context, reentrantConfigurationPublicationOwnerIsCurrent },
             { context.mOwner, exactMutationWindowIsCurrent } };
}

enum class ReentrantLeafPublicationPath : std::uint8_t
{
    Images,
    FrameSlot
};

struct ReentrantLeafPublicationContext
{
    FakeState*                                       mState       = nullptr;
    VulkanInstanceGeneration*                        mOwner       = nullptr;
    VkExtent2D                                       mTargetExtent{};
    ReentrantLeafPublicationPath                     mPath        = ReentrantLeafPublicationPath::Images;
    PostPublicationFrameSlotMutation                 mMutation    = PostPublicationFrameSlotMutation::Pending;
    VkCommandPool                                    mCommandPool = VK_NULL_HANDLE;
    VkCommandBuffer                                  mCommandBuffer = VK_NULL_HANDLE;
    VkSemaphore                                      mImageAvailableSemaphore = VK_NULL_HANDLE;
    VkSemaphore                                      mPresentationReadySemaphore = VK_NULL_HANDLE;
    VkFence                                          mSubmissionFence = VK_NULL_HANDLE;
    VkFence                                          mPresentCompletionFence = VK_NULL_HANDLE;
    bool                                             mAttempted = false;
    bool                                             mPublished = false;
    std::optional<VulkanSwapchainFrameSlotDisposition> mObservedDisposition;
};

bool reentrantLeafPublicationOwnerIsCurrent(void* userdata, const VulkanInstanceGeneration& generation) noexcept
{
    auto* context = static_cast<ReentrantLeafPublicationContext*>(userdata);
    if (!context || !context->mState || !context->mOwner || context->mOwner != &generation)
    {
        return false;
    }

    const bool at_images_boundary = context->mPath == ReentrantLeafPublicationPath::Images && generation.hasSwapchainGeneration() &&
                                    !generation.hasSwapchainImagesGeneration();
    const bool at_frame_slot_boundary =
        context->mPath == ReentrantLeafPublicationPath::FrameSlot && generation.hasSwapchainImagesGeneration() &&
        generation.hasSwapchainPresentationTargetGeneration() && generation.hasSwapchainPresentationPipelineGeneration() &&
        generation.hasSwapchainReadbackGeneration() && !generation.hasSwapchainFrameSlotGeneration();
    if (!context->mAttempted && (at_images_boundary || at_frame_slot_boundary))
    {
        context->mAttempted                          = true;
        context->mState->mCommandPool                = context->mCommandPool;
        context->mState->mCommandBuffer              = context->mCommandBuffer;
        context->mState->mImageAvailableSemaphore    = context->mImageAvailableSemaphore;
        context->mState->mPresentationReadySemaphore = context->mPresentationReadySemaphore;
        context->mState->mSubmissionFence            = context->mSubmissionFence;
        context->mState->mPresentCompletionFence     = context->mPresentCompletionFence;
        const VulkanInstanceOwnerCheck owner_check{ context->mOwner, exactMutationOwnerIsCurrent };
        const VulkanWindowGenerationCheck window_check{ context->mOwner, exactMutationWindowIsCurrent };
        const std::uint64_t native_window_generation = context->mOwner->nativeWindowGeneration();

        if (context->mPath == ReentrantLeafPublicationPath::Images)
        {
            const VulkanSwapchainImagesRequest images_request{
                native_window_generation, context->mTargetExtent, owner_check, window_check
            };
            if (context->mOwner->acquireSwapchainImagesGeneration(images_request))
            {
                return true;
            }
        }

        if (!context->mOwner->hasSwapchainPresentationTargetGeneration())
        {
            const VulkanSwapchainPresentationTargetRequest presentation_target_request{
                native_window_generation, context->mTargetExtent, owner_check, window_check
            };
            if (context->mOwner->acquireSwapchainPresentationTargetGeneration(presentation_target_request))
            {
                return true;
            }
        }

        const VulkanSwapchainFrameSlotRequest frame_slot_request{
            native_window_generation, context->mTargetExtent, owner_check, window_check
        };
        if (context->mOwner->acquireSwapchainFrameSlotGeneration(frame_slot_request))
        {
            return true;
        }

        context->mPublished = context->mOwner->resolvedSwapchainImageCount() == context->mState->mSwapchainImages.size() &&
                              context->mOwner->swapchainFrameCommandPool() == context->mCommandPool &&
                              context->mOwner->swapchainFrameCommandBuffer() == context->mCommandBuffer &&
                              context->mOwner->swapchainFrameImageAvailableSemaphore() == context->mImageAvailableSemaphore &&
                              context->mOwner->swapchainFramePresentationReadySemaphore() ==
                                  context->mPresentationReadySemaphore &&
                              context->mOwner->swapchainFrameSubmissionFence() == context->mSubmissionFence &&
                              context->mOwner->swapchainFramePresentCompletionFence() == context->mPresentCompletionFence;
        context->mState->mWaitForFencesResultIndex = 0;
        context->mState->mWaitForFencesResults[0] =
            context->mMutation == PostPublicationFrameSlotMutation::Pending ? VK_SUCCESS : VK_ERROR_DEVICE_LOST;
        context->mState->mWaitForFencesResults[1] = VK_TIMEOUT;
        (void)context->mOwner->roundTripEmptySwapchainFrameSlot(makeExactFrameSlotOperationRequest(*context->mOwner));
        context->mObservedDisposition = context->mOwner->swapchainFrameSlotDisposition();
    }
    return true;
}

VulkanSwapchainChainRebuildRequest makeReentrantLeafPublicationRequest(
    ReentrantLeafPublicationContext& context) noexcept
{
    return { context.mOwner ? context.mOwner->nativeWindowGeneration() : 0,
             context.mTargetExtent,
             { &context, reentrantLeafPublicationOwnerIsCurrent },
             { context.mOwner, exactMutationWindowIsCurrent } };
}

enum class PreflightMutationCallback : std::uint8_t
{
    Owner,
    Window
};

enum class PreflightMutationAction : std::uint8_t
{
    OperateExisting,
    ReplaceChildren
};

struct PreflightMutationContext
{
    FakeState*                                       mState         = nullptr;
    VulkanInstanceGeneration*                        mOwner         = nullptr;
    VkExtent2D                                       mReplacementExtent{};
    PreflightMutationCallback                        mCallback      = PreflightMutationCallback::Owner;
    PreflightMutationAction                          mAction        = PreflightMutationAction::OperateExisting;
    PostPublicationFrameSlotMutation                 mMutation      = PostPublicationFrameSlotMutation::Pending;
    bool                                             mCurrentAfterMutation = false;
    VkSwapchainKHR                                   mReplacementSwapchain = VK_NULL_HANDLE;
    VkCommandPool                                    mReplacementCommandPool = VK_NULL_HANDLE;
    VkCommandBuffer                                  mReplacementCommandBuffer = VK_NULL_HANDLE;
    VkSemaphore                                      mReplacementImageAvailableSemaphore = VK_NULL_HANDLE;
    VkSemaphore                                      mReplacementPresentationReadySemaphore = VK_NULL_HANDLE;
    VkFence                                          mReplacementSubmissionFence = VK_NULL_HANDLE;
    VkFence                                          mReplacementPresentCompletionFence = VK_NULL_HANDLE;
    std::size_t                                      mOwnerChecks  = 0;
    std::size_t                                      mWindowChecks = 0;
    bool                                             mMutated      = false;
    bool                                             mReplaced     = false;
    bool                                             mPublished    = false;
    VkSwapchainKHR                                   mPublishedSwapchain = VK_NULL_HANDLE;
    std::optional<VulkanSwapchainFrameSlotDisposition> mObservedDisposition;
};

bool applyPreflightMutation(PreflightMutationContext& context) noexcept
{
    if (context.mMutated || !context.mState || !context.mOwner)
    {
        return context.mCurrentAfterMutation;
    }
    context.mMutated = true;

    if (context.mAction == PreflightMutationAction::ReplaceChildren)
    {
        if (!context.mOwner->resetSwapchainConfigurationGeneration())
        {
            return context.mCurrentAfterMutation;
        }
        context.mReplaced                                  = true;
        context.mState->mSwapchain                         = context.mReplacementSwapchain;
        context.mState->mCommandPool                       = context.mReplacementCommandPool;
        context.mState->mCommandBuffer                     = context.mReplacementCommandBuffer;
        context.mState->mImageAvailableSemaphore           = context.mReplacementImageAvailableSemaphore;
        context.mState->mPresentationReadySemaphore        = context.mReplacementPresentationReadySemaphore;
        context.mState->mSubmissionFence                   = context.mReplacementSubmissionFence;
        context.mState->mPresentCompletionFence            = context.mReplacementPresentCompletionFence;
        const VulkanInstanceOwnerCheck owner_check{ context.mOwner, exactMutationOwnerIsCurrent };
        const VulkanWindowGenerationCheck window_check{ context.mOwner, exactMutationWindowIsCurrent };
        const std::uint64_t native_window_generation = context.mOwner->nativeWindowGeneration();

        const VulkanSwapchainConfigurationRequest configuration_request{
            native_window_generation, context.mReplacementExtent, owner_check, window_check
        };
        if (context.mOwner->acquireSwapchainConfigurationGeneration(configuration_request))
        {
            return context.mCurrentAfterMutation;
        }
        const VulkanSwapchainRequest swapchain_request{
            native_window_generation, context.mReplacementExtent, owner_check, window_check
        };
        if (context.mOwner->acquireSwapchainGeneration(swapchain_request))
        {
            return context.mCurrentAfterMutation;
        }
        const VulkanSwapchainImagesRequest images_request{
            native_window_generation, context.mReplacementExtent, owner_check, window_check
        };
        if (context.mOwner->acquireSwapchainImagesGeneration(images_request))
        {
            return context.mCurrentAfterMutation;
        }
        const VulkanSwapchainFrameSlotRequest frame_slot_request{
            native_window_generation, context.mReplacementExtent, owner_check, window_check
        };
        if (context.mOwner->acquireSwapchainFrameSlotGeneration(frame_slot_request))
        {
            return context.mCurrentAfterMutation;
        }
    }

    context.mPublishedSwapchain = context.mOwner->swapchain();
    context.mPublished = context.mOwner->hasSwapchainConfigurationGeneration() &&
                         context.mOwner->hasSwapchainGeneration() && context.mOwner->hasSwapchainImagesGeneration() &&
                         context.mOwner->hasSwapchainFrameSlotGeneration();
    context.mState->mWaitForFencesResultIndex = 0;
    context.mState->mWaitForFencesResults[0] =
        context.mMutation == PostPublicationFrameSlotMutation::Pending ? VK_SUCCESS : VK_ERROR_DEVICE_LOST;
    context.mState->mWaitForFencesResults[1] = VK_TIMEOUT;
    (void)context.mOwner->roundTripEmptySwapchainFrameSlot(makeExactFrameSlotOperationRequest(*context.mOwner));
    context.mObservedDisposition = context.mOwner->swapchainFrameSlotDisposition();
    return context.mCurrentAfterMutation;
}

bool preflightMutationOwnerIsCurrent(void* userdata, const VulkanInstanceGeneration& generation) noexcept
{
    auto* context = static_cast<PreflightMutationContext*>(userdata);
    if (!context || context->mOwner != &generation)
    {
        return false;
    }
    ++context->mOwnerChecks;
    return context->mCallback == PreflightMutationCallback::Owner ? applyPreflightMutation(*context) : true;
}

bool preflightMutationWindowIsCurrent(void* userdata, std::uint64_t native_window_generation) noexcept
{
    auto* context = static_cast<PreflightMutationContext*>(userdata);
    if (!context || !context->mOwner || native_window_generation == 0 ||
        context->mOwner->nativeWindowGeneration() != native_window_generation)
    {
        return false;
    }
    ++context->mWindowChecks;
    return context->mCallback == PreflightMutationCallback::Window ? applyPreflightMutation(*context) : true;
}

VulkanSwapchainChainRebuildRequest makePreflightMutationRequest(PreflightMutationContext& context) noexcept
{
    return { context.mOwner ? context.mOwner->nativeWindowGeneration() : 0,
             context.mReplacementExtent,
             { &context, preflightMutationOwnerIsCurrent },
             { &context, preflightMutationWindowIsCurrent } };
}

enum class EpochReplacementScope : std::uint8_t
{
    Children,
    StableParents
};

struct EpochReplacementContext
{
    FakeState*                  mState       = nullptr;
    VulkanInstanceGeneration*   mOwner       = nullptr;
    VkExtent2D                  mTargetExtent{};
    EpochReplacementScope       mScope       = EpochReplacementScope::Children;
    std::size_t                 mOwnerChecks  = 0;
    std::size_t                 mWindowChecks = 0;
    bool                        mReplaced     = false;
    bool                        mPublished    = false;
};

bool replaceEpochTrackedOwnership(EpochReplacementContext& context) noexcept
{
    if (context.mReplaced || !context.mState || !context.mOwner)
    {
        return true;
    }

    const bool reset = context.mScope == EpochReplacementScope::StableParents
                           ? context.mOwner->resetSurfaceGeneration()
                           : context.mOwner->resetSwapchainConfigurationGeneration();
    if (!reset)
    {
        return true;
    }
    context.mReplaced = true;

    const VulkanInstanceOwnerCheck owner_check{ context.mOwner, exactMutationOwnerIsCurrent };
    const VulkanWindowGenerationCheck window_check{ context.mOwner, exactMutationWindowIsCurrent };
    const std::uint64_t native_window_generation = context.mOwner->nativeWindowGeneration();
    if (context.mScope == EpochReplacementScope::StableParents)
    {
        const VulkanSurfaceRequest surface_request{
            native_window_generation,
            owner_check,
            window_check,
            { context.mState, createSurface }
        };
        if (context.mOwner->acquireSurfaceGeneration(surface_request))
        {
            return true;
        }
        const VulkanPresentationDeviceRequest presentation_request{
            native_window_generation, owner_check, window_check
        };
        if (context.mOwner->acquirePresentationDeviceGeneration(presentation_request))
        {
            return true;
        }
        const VulkanLogicalDeviceRequest logical_request{ native_window_generation, owner_check, window_check };
        if (context.mOwner->acquireLogicalDeviceGeneration(logical_request))
        {
            return true;
        }
    }

    const VulkanSwapchainConfigurationRequest configuration_request{
        native_window_generation, context.mTargetExtent, owner_check, window_check
    };
    if (context.mOwner->acquireSwapchainConfigurationGeneration(configuration_request))
    {
        return true;
    }
    const VulkanSwapchainRequest swapchain_request{
        native_window_generation, context.mTargetExtent, owner_check, window_check
    };
    if (context.mOwner->acquireSwapchainGeneration(swapchain_request))
    {
        return true;
    }
    const VulkanSwapchainImagesRequest images_request{
        native_window_generation, context.mTargetExtent, owner_check, window_check
    };
    if (context.mOwner->acquireSwapchainImagesGeneration(images_request))
    {
        return true;
    }
    const VulkanSwapchainPresentationTargetRequest presentation_target_request{
        native_window_generation, context.mTargetExtent, owner_check, window_check
    };
    if (context.mOwner->acquireSwapchainPresentationTargetGeneration(presentation_target_request))
    {
        return true;
    }
    const VulkanSwapchainFrameSlotRequest frame_slot_request{
        native_window_generation, context.mTargetExtent, owner_check, window_check
    };
    if (context.mOwner->acquireSwapchainFrameSlotGeneration(frame_slot_request))
    {
        return true;
    }

    context.mPublished = context.mOwner->hasSurfaceGeneration() && context.mOwner->hasPresentationDeviceGeneration() &&
                         context.mOwner->hasLogicalDeviceGeneration() &&
                         context.mOwner->hasSwapchainConfigurationGeneration() && context.mOwner->hasSwapchainGeneration() &&
                         context.mOwner->hasSwapchainImagesGeneration() &&
                         context.mOwner->hasSwapchainPresentationTargetGeneration() &&
                         context.mOwner->hasSwapchainFrameSlotGeneration() &&
                         context.mOwner->swapchainFrameSlotDisposition() == VulkanSwapchainFrameSlotDisposition::Reusable;
    return true;
}

bool epochReplacementOwnerIsCurrent(void* userdata, const VulkanInstanceGeneration& generation) noexcept
{
    auto* context = static_cast<EpochReplacementContext*>(userdata);
    if (!context || context->mOwner != &generation)
    {
        return false;
    }
    ++context->mOwnerChecks;
    return replaceEpochTrackedOwnership(*context);
}

bool epochReplacementWindowIsCurrent(void* userdata, std::uint64_t native_window_generation) noexcept
{
    auto* context = static_cast<EpochReplacementContext*>(userdata);
    if (!context || !context->mOwner || native_window_generation == 0 ||
        context->mOwner->nativeWindowGeneration() != native_window_generation)
    {
        return false;
    }
    ++context->mWindowChecks;
    return true;
}

VulkanSwapchainChainRebuildRequest makeEpochReplacementRequest(EpochReplacementContext& context) noexcept
{
    return { context.mOwner ? context.mOwner->nativeWindowGeneration() : 0,
             context.mTargetExtent,
             { &context, epochReplacementOwnerIsCurrent },
             { &context, epochReplacementWindowIsCurrent } };
}

struct ChildParentReplacementContext
{
    VulkanInstanceGeneration* mOwner       = nullptr;
    VkExtent2D                 mTargetExtent{};
    std::size_t                mOwnerChecks  = 0;
    std::size_t                mWindowChecks = 0;
    bool                       mReplaced     = false;
    bool                       mReacquired   = false;
};

bool childParentReplacementOwnerIsCurrent(void* userdata, const VulkanInstanceGeneration& generation) noexcept
{
    auto* context = static_cast<ChildParentReplacementContext*>(userdata);
    if (!context || context->mOwner != &generation)
    {
        return false;
    }
    ++context->mOwnerChecks;

    if (!context->mReplaced && generation.hasLogicalDeviceGeneration() &&
        !generation.hasSwapchainConfigurationGeneration())
    {
        context->mReplaced = context->mOwner->resetLogicalDeviceGeneration();
        if (!context->mReplaced)
        {
            return true;
        }

        const VulkanLogicalDeviceRequest logical_request{
            context->mOwner->nativeWindowGeneration(),
            { context->mOwner, exactMutationOwnerIsCurrent },
            { context->mOwner, exactMutationWindowIsCurrent }
        };
        context->mReacquired = !context->mOwner->acquireLogicalDeviceGeneration(logical_request);
    }
    return true;
}

bool childParentReplacementWindowIsCurrent(void* userdata, std::uint64_t native_window_generation) noexcept
{
    auto* context = static_cast<ChildParentReplacementContext*>(userdata);
    if (!context || !context->mOwner || native_window_generation == 0 ||
        context->mOwner->nativeWindowGeneration() != native_window_generation)
    {
        return false;
    }
    ++context->mWindowChecks;
    return true;
}

VulkanSwapchainChainRebuildRequest makeChildParentReplacementRequest(ChildParentReplacementContext& context) noexcept
{
    return { context.mOwner ? context.mOwner->nativeWindowGeneration() : 0,
             context.mTargetExtent,
             { &context, childParentReplacementOwnerIsCurrent },
             { &context, childParentReplacementWindowIsCurrent } };
}

enum class OlderTargetPublicationPath : std::uint8_t
{
    Surface,
    PresentationDevice,
    LogicalDevice
};

struct OlderTargetPublicationContext
{
    FakeState*                    mState       = nullptr;
    VulkanInstanceGeneration*     mOwner       = nullptr;
    OlderTargetPublicationPath    mPath        = OlderTargetPublicationPath::Surface;
    std::size_t                   mOwnerChecks  = 0;
    std::size_t                   mWindowChecks = 0;
    bool                          mAttempted    = false;
    bool                          mPublished    = false;
};

bool olderTargetPublicationOwnerIsCurrent(void* userdata, const VulkanInstanceGeneration& generation) noexcept
{
    auto* context = static_cast<OlderTargetPublicationContext*>(userdata);
    if (!context || !context->mState || !context->mOwner || context->mOwner != &generation)
    {
        return false;
    }
    ++context->mOwnerChecks;
    if (context->mAttempted)
    {
        return true;
    }
    context->mAttempted = true;

    const std::uint64_t native_window_generation = context->mOwner->nativeWindowGeneration();
    const VulkanInstanceOwnerCheck owner_check{ context->mOwner, exactMutationOwnerIsCurrent };
    const VulkanWindowGenerationCheck window_check{ context->mOwner, exactMutationWindowIsCurrent };
    switch (context->mPath)
    {
        case OlderTargetPublicationPath::Surface:
        {
            const VulkanSurfaceRequest request{
                native_window_generation,
                owner_check,
                window_check,
                { context->mState, createSurface }
            };
            context->mPublished = !context->mOwner->acquireSurfaceGeneration(request);
            break;
        }
        case OlderTargetPublicationPath::PresentationDevice:
        {
            const VulkanPresentationDeviceRequest request{ native_window_generation, owner_check, window_check };
            context->mPublished = !context->mOwner->acquirePresentationDeviceGeneration(request);
            break;
        }
        case OlderTargetPublicationPath::LogicalDevice:
        {
            const VulkanLogicalDeviceRequest request{ native_window_generation, owner_check, window_check };
            context->mPublished = !context->mOwner->acquireLogicalDeviceGeneration(request);
            break;
        }
    }
    return true;
}

bool olderTargetPublicationWindowIsCurrent(void* userdata, std::uint64_t native_window_generation) noexcept
{
    auto* context = static_cast<OlderTargetPublicationContext*>(userdata);
    if (!context || !context->mOwner || native_window_generation == 0 ||
        context->mOwner->nativeWindowGeneration() != native_window_generation)
    {
        return false;
    }
    ++context->mWindowChecks;
    return true;
}

const VulkanInstanceAcquireError& requireError(const VulkanInstanceAcquireResult& result)
{
    const auto* error = std::get_if<VulkanInstanceAcquireError>(&result);
    tut::ensure("instance acquisition returns an error", error != nullptr);
    return *error;
}

VulkanInstanceGeneration takeGeneration(VulkanInstanceAcquireResult&& result)
{
    tut::ensure("instance acquisition returns an owner", std::holds_alternative<VulkanInstanceGeneration>(result));
    return std::get<VulkanInstanceGeneration>(std::move(result));
}

void ensureCode(const VulkanInstanceAcquireResult& result, VulkanInstanceAcquireCode code)
{
    tut::ensure("the exact instance error is reported", requireError(result).mCode == code);
}

const VulkanSurfaceAcquireError& requireSurfaceError(const VulkanSurfaceAcquireResult& result)
{
    tut::ensure("surface acquisition returns an error", result.has_value());
    return *result;
}

void ensureSurfaceCode(const VulkanSurfaceAcquireResult& result, VulkanSurfaceAcquireCode code)
{
    tut::ensure("the exact surface error is reported", requireSurfaceError(result).mCode == code);
}

const VulkanPresentationDeviceAcquireError& requirePresentationDeviceError(const VulkanPresentationDeviceAcquireResult& result)
{
    tut::ensure("presentation-device acquisition returns an error", result.has_value());
    return *result;
}

void ensurePresentationDeviceCode(const VulkanPresentationDeviceAcquireResult& result, VulkanPresentationDeviceAcquireCode code)
{
    tut::ensure("the exact presentation-device error is reported", requirePresentationDeviceError(result).mCode == code);
}

const VulkanLogicalDeviceAcquireError& requireLogicalDeviceError(const VulkanLogicalDeviceAcquireResult& result)
{
    tut::ensure("logical-device acquisition returns an error", result.has_value());
    return *result;
}

void ensureLogicalDeviceCode(const VulkanLogicalDeviceAcquireResult& result, VulkanLogicalDeviceAcquireCode code)
{
    tut::ensure("the exact logical-device error is reported", requireLogicalDeviceError(result).mCode == code);
}

const VulkanUploadSourceAcquireError& requireUploadSourceError(const VulkanUploadSourceAcquireResult& result)
{
    tut::ensure("upload-source acquisition returns an error", result.has_value());
    return *result;
}

void ensureUploadSourceCode(const VulkanUploadSourceAcquireResult& result, VulkanUploadSourceAcquireCode code)
{
    tut::ensure("the exact upload-source error is reported", requireUploadSourceError(result).mCode == code);
}

const VulkanUploadDestinationAcquireError& requireUploadDestinationError(const VulkanUploadDestinationAcquireResult& result)
{
    tut::ensure("upload-destination acquisition returns an error", result.has_value());
    return *result;
}

void ensureUploadDestinationCode(const VulkanUploadDestinationAcquireResult& result, VulkanUploadDestinationAcquireCode code)
{
    tut::ensure("the exact upload-destination error is reported", requireUploadDestinationError(result).mCode == code);
}

const VulkanUploadTransferAcquireError& requireUploadTransferError(const VulkanUploadTransferAcquireResult& result)
{
    tut::ensure("upload-transfer acquisition returns an error", result.has_value());
    return *result;
}

void ensureUploadTransferCode(const VulkanUploadTransferAcquireResult& result, VulkanUploadTransferAcquireCode code)
{
    tut::ensure("the exact upload-transfer error is reported", requireUploadTransferError(result).mCode == code);
}

const VulkanUploadTransferParentOperationError& requireUploadTransferOperationError(const VulkanUploadTransferParentOperationResult& result)
{
    const auto* error = std::get_if<VulkanUploadTransferParentOperationError>(&result);
    tut::ensure("upload-transfer operation returns an error", error != nullptr);
    return *error;
}

void ensureUploadTransferDisposition(const VulkanUploadTransferParentOperationResult& result, VulkanUploadTransferDisposition disposition)
{
    const auto* actual = std::get_if<VulkanUploadTransferDisposition>(&result);
    tut::ensure("the exact upload-transfer disposition is reported", actual != nullptr && *actual == disposition);
}

const VulkanSwapchainConfigurationAcquireError& requireSwapchainConfigurationError(const VulkanSwapchainConfigurationAcquireResult& result)
{
    tut::ensure("swapchain-configuration acquisition returns an error", result.has_value());
    return *result;
}

void ensureSwapchainConfigurationCode(const VulkanSwapchainConfigurationAcquireResult& result, VulkanSwapchainConfigurationAcquireCode code)
{
    tut::ensure("the exact swapchain-configuration error is reported", requireSwapchainConfigurationError(result).mCode == code);
}

const VulkanSwapchainAcquireError& requireSwapchainError(const VulkanSwapchainAcquireResult& result)
{
    tut::ensure("swapchain acquisition returns an error", result.has_value());
    return *result;
}

void ensureSwapchainCode(const VulkanSwapchainAcquireResult& result, VulkanSwapchainAcquireCode code)
{
    tut::ensure("the exact swapchain error is reported", requireSwapchainError(result).mCode == code);
}

const VulkanSwapchainImagesAcquireError& requireSwapchainImagesError(const VulkanSwapchainImagesAcquireResult& result)
{
    tut::ensure("swapchain-images acquisition returns an error", result.has_value());
    return *result;
}

void ensureSwapchainImagesCode(const VulkanSwapchainImagesAcquireResult& result, VulkanSwapchainImagesAcquireCode code)
{
    tut::ensure("the exact swapchain-images error is reported", requireSwapchainImagesError(result).mCode == code);
}

const VulkanSwapchainPresentationTargetAcquireError& requireSwapchainPresentationTargetError(
    const VulkanSwapchainPresentationTargetAcquireResult& result)
{
    tut::ensure("presentation-target acquisition returns an error", result.has_value());
    return *result;
}

void ensureSwapchainPresentationTargetCode(const VulkanSwapchainPresentationTargetAcquireResult& result,
                                           VulkanSwapchainPresentationTargetAcquireCode          code)
{
    tut::ensure("the exact presentation-target error is reported",
                requireSwapchainPresentationTargetError(result).mCode == code);
}

const VulkanSwapchainPresentationPipelineAcquireError& requireSwapchainPresentationPipelineError(
    const VulkanSwapchainPresentationPipelineAcquireResult& result)
{
    tut::ensure("presentation-pipeline acquisition returns an error", result.has_value());
    return *result;
}

void ensureSwapchainPresentationPipelineCode(const VulkanSwapchainPresentationPipelineAcquireResult& result,
                                             VulkanSwapchainPresentationPipelineAcquireCode          code)
{
    tut::ensure("the exact presentation-pipeline error is reported", requireSwapchainPresentationPipelineError(result).mCode == code);
}

const VulkanSwapchainReadbackAcquireError& requireSwapchainReadbackError(const VulkanSwapchainReadbackAcquireResult& result)
{
    tut::ensure("readback acquisition returns an error", result.has_value());
    return *result;
}

void ensureSwapchainReadbackCode(const VulkanSwapchainReadbackAcquireResult& result, VulkanSwapchainReadbackAcquireCode code)
{
    tut::ensure("the exact readback error is reported", requireSwapchainReadbackError(result).mCode == code);
}

const VulkanSwapchainFrameSlotAcquireError& requireSwapchainFrameSlotError(const VulkanSwapchainFrameSlotAcquireResult& result)
{
    tut::ensure("frame-slot acquisition returns an error", result.has_value());
    return *result;
}

void ensureSwapchainFrameSlotCode(const VulkanSwapchainFrameSlotAcquireResult& result, VulkanSwapchainFrameSlotAcquireCode code)
{
    tut::ensure("the exact frame-slot error is reported", requireSwapchainFrameSlotError(result).mCode == code);
}

const VulkanSwapchainFrameSlotParentOperationError& requireSwapchainFrameSlotOperationError(
    const VulkanSwapchainFrameSlotParentOperationResult& result)
{
    const auto* error = std::get_if<VulkanSwapchainFrameSlotParentOperationError>(&result);
    tut::ensure("the parent frame-slot operation returns an error", error != nullptr);
    return *error;
}

void ensureSwapchainFrameSlotOperationCode(const VulkanSwapchainFrameSlotParentOperationResult& result,
                                           VulkanSwapchainFrameSlotParentOperationCode          code)
{
    tut::ensure("the exact parent frame-slot operation error is reported", requireSwapchainFrameSlotOperationError(result).mCode == code);
}

void ensureSwapchainFrameSlotOperationSuccess(const VulkanSwapchainFrameSlotParentOperationResult& result,
                                              VulkanSwapchainFrameSlotDisposition                  disposition)
{
    const auto* actual = std::get_if<VulkanSwapchainFrameSlotDisposition>(&result);
    tut::ensure("the parent frame-slot operation returns a disposition", actual != nullptr && *actual == disposition);
}

const VulkanSwapchainFrameSlotParentOperationError& requireSwapchainFrameSlotPresentationError(
    const VulkanSwapchainFrameSlotParentPresentationResult& result)
{
    const auto* error = std::get_if<VulkanSwapchainFrameSlotParentOperationError>(&result);
    tut::ensure("the parent frame-slot presentation returns an error", error != nullptr);
    return *error;
}

const VulkanSwapchainFrameSlotPresentationSuccess& requireSwapchainFrameSlotPresentationSuccess(
    const VulkanSwapchainFrameSlotParentPresentationResult& result)
{
    const auto* success = std::get_if<VulkanSwapchainFrameSlotPresentationSuccess>(&result);
    tut::ensure("the parent frame-slot presentation succeeds", success != nullptr);
    return *success;
}

VulkanSwapchainChainRebuildError requireSwapchainChainRebuildError(const VulkanSwapchainChainRebuildResult& result)
{
    const auto* error = std::get_if<VulkanSwapchainChainRebuildError>(&result);
    tut::ensure("the swapchain-chain rebuild returns an error", error != nullptr);
    return *error;
}

void ensureSwapchainChainRebuildCode(const VulkanSwapchainChainRebuildResult& result,
                                     VulkanSwapchainChainRebuildCode          code,
                                     VulkanSwapchainChainRebuildPhase         phase)
{
    const VulkanSwapchainChainRebuildError& error = requireSwapchainChainRebuildError(result);
    tut::ensure("the exact swapchain-chain rebuild error is reported", error.mCode == code && error.mPhase == phase);
}

void ensureSwapchainChainRebuildOutcome(const VulkanSwapchainChainRebuildResult& result,
                                        VulkanSwapchainChainRebuildOutcome       outcome)
{
    const auto* actual = std::get_if<VulkanSwapchainChainRebuildOutcome>(&result);
    tut::ensure("the exact swapchain-chain rebuild outcome is reported", actual != nullptr && *actual == outcome);
}

void acquireSelectionChain(FakeState& state, VulkanInstanceGeneration& owner)
{
    tut::ensure("the surface fixture succeeds", !owner.acquireSurfaceGeneration(makeSurfaceRequest(state, owner)));
    tut::ensure("the presentation fixture succeeds",
                !owner.acquirePresentationDeviceGeneration(makePresentationDeviceRequest(state, owner)));
}

void acquireLogicalChain(FakeState& state, VulkanInstanceGeneration& owner)
{
    acquireSelectionChain(state, owner);
    tut::ensure("the logical-device fixture succeeds", !owner.acquireLogicalDeviceGeneration(makeLogicalDeviceRequest(state, owner)));
}

void acquireUploadSourceChain(FakeState& state, VulkanInstanceGeneration& owner)
{
    acquireLogicalChain(state, owner);
    tut::ensure("the upload-source fixture succeeds", !owner.acquireUploadSourceGeneration(makeUploadSourceRequest(state, owner)));
}

void acquireUploadDestinationChain(FakeState& state, VulkanInstanceGeneration& owner)
{
    acquireUploadSourceChain(state, owner);
    state.mMemoryProperties.memoryTypes[0].propertyFlags |= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    tut::ensure("the upload-destination fixture succeeds",
                !owner.acquireUploadDestinationGeneration(makeUploadDestinationRequest(state, owner)));
}

void acquireUploadTransferChain(FakeState& state, VulkanInstanceGeneration& owner)
{
    acquireUploadDestinationChain(state, owner);
    tut::ensure("the upload-transfer fixture succeeds", !owner.acquireUploadTransferGeneration(makeUploadTransferRequest(state, owner)));
}

void acquireConfigurationChain(FakeState& state, VulkanInstanceGeneration& owner, VkExtent2D drawable_extent = { 800, 600 })
{
    acquireLogicalChain(state, owner);
    tut::ensure("the swapchain-configuration fixture succeeds",
                !owner.acquireSwapchainConfigurationGeneration(makeSwapchainConfigurationRequest(state, owner, drawable_extent)));
}

void acquireSwapchainChain(FakeState& state, VulkanInstanceGeneration& owner, VkExtent2D drawable_extent = { 800, 600 })
{
    acquireConfigurationChain(state, owner, drawable_extent);
    tut::ensure("the swapchain fixture succeeds", !owner.acquireSwapchainGeneration(makeSwapchainRequest(state, owner, drawable_extent)));
}

void acquireSwapchainImagesChain(FakeState& state, VulkanInstanceGeneration& owner, VkExtent2D drawable_extent = { 800, 600 })
{
    acquireSwapchainChain(state, owner, drawable_extent);
    tut::ensure("the swapchain-images fixture succeeds",
                !owner.acquireSwapchainImagesGeneration(makeSwapchainImagesRequest(state, owner, drawable_extent)));
}

void acquireSwapchainPresentationTargetChain(FakeState& state, VulkanInstanceGeneration& owner, VkExtent2D drawable_extent = { 800, 600 })
{
    acquireSwapchainImagesChain(state, owner, drawable_extent);
    tut::ensure("the presentation-target fixture succeeds",
                !owner.acquireSwapchainPresentationTargetGeneration(makeSwapchainPresentationTargetRequest(state, owner, drawable_extent)));
}

void acquireCompleteSwapchainChain(FakeState& state, VulkanInstanceGeneration& owner, VkExtent2D drawable_extent = { 800, 600 })
{
    acquireSwapchainPresentationTargetChain(state, owner, drawable_extent);
    tut::ensure(
        "the complete-chain presentation-pipeline fixture succeeds",
        !owner.acquireSwapchainPresentationPipelineGeneration(makeSwapchainPresentationPipelineRequest(state, owner, drawable_extent)));
    tut::ensure("the complete-chain readback fixture succeeds",
                !owner.acquireSwapchainReadbackGeneration(makeSwapchainReadbackRequest(state, owner, drawable_extent)));
    tut::ensure("the complete-chain frame-slot fixture succeeds",
                !owner.acquireSwapchainFrameSlotGeneration(makeSwapchainFrameSlotRequest(state, owner, drawable_extent)));
}

void acquireCompleteSwapchainChildren(FakeState& state, VulkanInstanceGeneration& owner, VkExtent2D drawable_extent = { 800, 600 })
{
    tut::ensure("the child-chain configuration fixture succeeds",
                !owner.acquireSwapchainConfigurationGeneration(makeSwapchainConfigurationRequest(state, owner, drawable_extent)));
    tut::ensure("the child-chain swapchain fixture succeeds",
                !owner.acquireSwapchainGeneration(makeSwapchainRequest(state, owner, drawable_extent)));
    tut::ensure("the child-chain images fixture succeeds",
                !owner.acquireSwapchainImagesGeneration(makeSwapchainImagesRequest(state, owner, drawable_extent)));
    tut::ensure("the child-chain presentation-target fixture succeeds",
                !owner.acquireSwapchainPresentationTargetGeneration(makeSwapchainPresentationTargetRequest(state, owner, drawable_extent)));
    tut::ensure(
        "the child-chain presentation-pipeline fixture succeeds",
        !owner.acquireSwapchainPresentationPipelineGeneration(makeSwapchainPresentationPipelineRequest(state, owner, drawable_extent)));
    tut::ensure("the child-chain readback fixture succeeds",
                !owner.acquireSwapchainReadbackGeneration(makeSwapchainReadbackRequest(state, owner, drawable_extent)));
    tut::ensure("the child-chain frame-slot fixture succeeds",
                !owner.acquireSwapchainFrameSlotGeneration(makeSwapchainFrameSlotRequest(state, owner, drawable_extent)));
}

void acquireSwapchainFrameSlotChain(FakeState& state, VulkanInstanceGeneration& owner, VkExtent2D drawable_extent = { 800, 600 })
{
    acquireSwapchainImagesChain(state, owner, drawable_extent);
    tut::ensure("the frame-slot fixture succeeds",
                !owner.acquireSwapchainFrameSlotGeneration(makeSwapchainFrameSlotRequest(state, owner, drawable_extent)));
}

void failAllocation()
{
    throw std::bad_alloc();
}

std::size_t gAllocationCheckpointCalls = 0;
std::size_t gFailAllocationCheckpoint  = 0;

void failSelectedAllocation()
{
    ++gAllocationCheckpointCalls;
    if (gAllocationCheckpointCalls == gFailAllocationCheckpoint)
    {
        throw std::bad_alloc();
    }
}

struct NativeCandidateResetContext
{
    VulkanInstanceGeneration*                         mOwner          = nullptr;
    std::size_t                                        mOwnerChecks    = 0;
    std::size_t                                        mWindowChecks   = 0;
    std::size_t                                        mRebuildOwnerChecks  = 0;
    std::size_t                                        mRebuildWindowChecks = 0;
    bool                                               mResetAttempted = false;
    bool                                               mResetSucceeded = false;
    std::optional<VulkanInstanceGeneration>            mMoveDestination;
    std::optional<VulkanSwapchainChainRebuildError>    mRebuildError;
};

bool guardedRebuildOwnerIsCurrent(void* userdata, const VulkanInstanceGeneration& generation) noexcept
{
    auto* context = static_cast<NativeCandidateResetContext*>(userdata);
    if (!context || context->mOwner != &generation)
    {
        return false;
    }
    ++context->mRebuildOwnerChecks;
    return true;
}

bool guardedRebuildWindowIsCurrent(void* userdata, std::uint64_t native_window_generation) noexcept
{
    auto* context = static_cast<NativeCandidateResetContext*>(userdata);
    if (!context || !context->mOwner || native_window_generation == 0 ||
        context->mOwner->nativeWindowGeneration() != native_window_generation)
    {
        return false;
    }
    ++context->mRebuildWindowChecks;
    return true;
}

bool nativeCandidateResetOwnerIsCurrent(void* userdata, const VulkanInstanceGeneration& generation) noexcept
{
    auto* context = static_cast<NativeCandidateResetContext*>(userdata);
    if (!context || context->mOwner != &generation)
    {
        return false;
    }
    ++context->mOwnerChecks;
    if (context->mOwnerChecks == 2)
    {
        context->mMoveDestination.emplace(std::move(*context->mOwner));
        const VulkanSwapchainChainRebuildRequest rebuild_request{
            context->mOwner->nativeWindowGeneration(),
            context->mOwner->swapchainDrawableExtent(),
            { context, guardedRebuildOwnerIsCurrent },
            { context, guardedRebuildWindowIsCurrent }
        };
        const VulkanSwapchainChainRebuildResult rebuild_result = context->mOwner->rebuildSwapchainChain(rebuild_request);
        if (const auto* error = std::get_if<VulkanSwapchainChainRebuildError>(&rebuild_result))
        {
            context->mRebuildError = *error;
        }
        context->mResetAttempted = true;
        context->mResetSucceeded = context->mOwner->resetLogicalDeviceGeneration();
        return false;
    }
    return true;
}

bool nativeCandidateResetWindowIsCurrent(void* userdata, std::uint64_t native_window_generation) noexcept
{
    auto* context = static_cast<NativeCandidateResetContext*>(userdata);
    if (!context || !context->mOwner || native_window_generation == 0 ||
        context->mOwner->nativeWindowGeneration() != native_window_generation)
    {
        return false;
    }
    ++context->mWindowChecks;
    return true;
}

struct SurfaceAbaAllocationContext
{
    FakeState*                mState      = nullptr;
    VulkanInstanceGeneration* mOwner      = nullptr;
    bool                      mInvoked    = false;
    bool                      mReset      = false;
    bool                      mReacquired = false;
};

SurfaceAbaAllocationContext* gSurfaceAbaAllocationContext = nullptr;

void replaceSurfaceAtAllocationCheckpoint() noexcept
{
    SurfaceAbaAllocationContext* context = gSurfaceAbaAllocationContext;
    if (!context || !context->mState || !context->mOwner || context->mInvoked)
    {
        return;
    }
    context->mInvoked = true;
    context->mReset   = context->mOwner->resetSurfaceGeneration();
    if (!context->mReset)
    {
        return;
    }

    const VulkanSurfaceRequest request{
        context->mOwner->nativeWindowGeneration(),
        { context->mOwner, exactMutationOwnerIsCurrent },
        { context->mOwner, exactMutationWindowIsCurrent },
        { context->mState, createSurface }
    };
    context->mReacquired = !context->mOwner->acquireSurfaceGeneration(request);
}

struct PipelineAbaAllocationContext
{
    FakeState*                mState = nullptr;
    VulkanInstanceGeneration* mOwner = nullptr;
    VkPipelineLayout          mReplacementLayout = VK_NULL_HANDLE;
    VkPipeline                mReplacementPipeline = VK_NULL_HANDLE;
    bool                      mInvoked = false;
    bool                      mPublished = false;
};

PipelineAbaAllocationContext* gPipelineAbaAllocationContext = nullptr;

void publishPipelineAtAllocationCheckpoint() noexcept
{
    PipelineAbaAllocationContext* context = gPipelineAbaAllocationContext;
    if (!context || !context->mState || !context->mOwner || context->mInvoked)
    {
        return;
    }
    context->mInvoked = true;
    context->mState->mPresentationPipelineLayout = context->mReplacementLayout;
    context->mState->mPresentationPipeline       = context->mReplacementPipeline;
    const VulkanSwapchainPresentationPipelineRequest request{
        context->mOwner->nativeWindowGeneration(),
        context->mOwner->swapchainDrawableExtent(),
        { context->mOwner, exactMutationOwnerIsCurrent },
        { context->mOwner, exactMutationWindowIsCurrent }
    };
    context->mPublished =
        !context->mOwner->acquireSwapchainPresentationPipelineGeneration(request) &&
        context->mOwner->swapchainPresentationPipelineLayout() == context->mReplacementLayout &&
        context->mOwner->swapchainPresentationPipeline() == context->mReplacementPipeline;
}

struct PipelineParentAbaContext
{
    FakeState*                mState = nullptr;
    VulkanInstanceGeneration* mOwner = nullptr;
    std::size_t               mOwnerChecks = 0;
    bool                      mReset = false;
    bool                      mReacquired = false;
};

bool pipelineParentAbaOwnerIsCurrent(void* userdata, const VulkanInstanceGeneration& generation) noexcept
{
    auto* context = static_cast<PipelineParentAbaContext*>(userdata);
    if (!context || !context->mState || !context->mOwner || context->mOwner != &generation)
    {
        return false;
    }
    ++context->mOwnerChecks;
    if (context->mOwnerChecks == 1)
    {
        const VkExtent2D extent = context->mOwner->swapchainDrawableExtent();
        context->mReset = context->mOwner->resetSwapchainPresentationTargetGeneration();
        if (context->mReset)
        {
            const VulkanSwapchainPresentationTargetRequest request{
                context->mOwner->nativeWindowGeneration(),
                extent,
                { context->mOwner, exactMutationOwnerIsCurrent },
                { context->mOwner, exactMutationWindowIsCurrent }
            };
            context->mReacquired = !context->mOwner->acquireSwapchainPresentationTargetGeneration(request);
        }
    }
    return true;
}

bool pipelineParentAbaWindowIsCurrent(void* userdata, std::uint64_t native_window_generation) noexcept
{
    const auto* context = static_cast<const PipelineParentAbaContext*>(userdata);
    return context && context->mOwner && native_window_generation == context->mOwner->nativeWindowGeneration();
}

struct ReadbackParentAbaContext
{
    FakeState*                mState        = nullptr;
    VulkanInstanceGeneration* mOwner        = nullptr;
    std::size_t               mOwnerChecks  = 0;
    std::size_t               mWindowChecks = 0;
    bool                      mReset        = false;
    bool                      mReacquired   = false;
};

bool readbackParentAbaOwnerIsCurrent(void* userdata, const VulkanInstanceGeneration& generation) noexcept
{
    auto* context = static_cast<ReadbackParentAbaContext*>(userdata);
    if (!context || !context->mState || !context->mOwner || context->mOwner != &generation)
    {
        return false;
    }
    ++context->mOwnerChecks;
    if (context->mOwnerChecks == 1)
    {
        const VkExtent2D extent = context->mOwner->swapchainDrawableExtent();
        context->mReset         = context->mOwner->resetSwapchainImagesGeneration();
        if (context->mReset)
        {
            context->mReacquired =
                !context->mOwner->acquireSwapchainImagesGeneration(makeSwapchainImagesRequest(*context->mState, *context->mOwner, extent));
        }
    }
    return true;
}

bool readbackParentAbaWindowIsCurrent(void* userdata, std::uint64_t native_window_generation) noexcept
{
    auto* context = static_cast<ReadbackParentAbaContext*>(userdata);
    if (!context || !context->mOwner || native_window_generation != context->mOwner->nativeWindowGeneration())
    {
        return false;
    }
    ++context->mWindowChecks;
    return true;
}

struct ReadbackOperationAbaContext
{
    FakeState*                mState        = nullptr;
    VulkanInstanceGeneration* mOwner        = nullptr;
    std::size_t               mOwnerChecks  = 0;
    std::size_t               mWindowChecks = 0;
    bool                      mReset        = false;
    bool                      mReacquired   = false;
};

bool readbackOperationAbaOwnerIsCurrent(void* userdata, const VulkanInstanceGeneration& generation) noexcept
{
    auto* context = static_cast<ReadbackOperationAbaContext*>(userdata);
    if (!context || !context->mState || !context->mOwner || context->mOwner != &generation)
    {
        return false;
    }
    ++context->mOwnerChecks;
    if (context->mOwnerChecks == 2)
    {
        const VkExtent2D extent = context->mOwner->swapchainDrawableExtent();
        context->mReset         = context->mOwner->resetSwapchainReadbackGeneration();
        if (context->mReset)
        {
            context->mReacquired = !context->mOwner->acquireSwapchainReadbackGeneration(
                makeSwapchainReadbackRequest(*context->mState, *context->mOwner, extent));
        }
    }
    return true;
}

bool readbackOperationAbaWindowIsCurrent(void* userdata, std::uint64_t native_window_generation) noexcept
{
    auto* context = static_cast<ReadbackOperationAbaContext*>(userdata);
    if (!context || !context->mOwner || native_window_generation != context->mOwner->nativeWindowGeneration())
    {
        return false;
    }
    ++context->mWindowChecks;
    return true;
}

struct UploadSourceLogicalAbaContext
{
    VulkanInstanceGeneration* mOwner       = nullptr;
    std::size_t               mOwnerChecks = 0;
    bool                      mReset       = false;
    bool                      mReacquired  = false;
};

bool uploadSourceLogicalAbaOwnerIsCurrent(void* userdata, const VulkanInstanceGeneration& generation) noexcept
{
    auto* context = static_cast<UploadSourceLogicalAbaContext*>(userdata);
    if (!context || !context->mOwner || context->mOwner != &generation)
    {
        return false;
    }
    ++context->mOwnerChecks;
    if (context->mOwnerChecks == 1)
    {
        context->mReset = context->mOwner->resetLogicalDeviceGeneration();
        if (context->mReset)
        {
            const VulkanLogicalDeviceRequest request{ context->mOwner->nativeWindowGeneration(),
                                                      { context->mOwner, exactMutationOwnerIsCurrent },
                                                      { context->mOwner, exactMutationWindowIsCurrent } };
            context->mReacquired = !context->mOwner->acquireLogicalDeviceGeneration(request);
        }
    }
    return true;
}

bool uploadSourceLogicalAbaWindowIsCurrent(void* userdata, std::uint64_t native_window_generation) noexcept
{
    const auto* context = static_cast<const UploadSourceLogicalAbaContext*>(userdata);
    return context && context->mOwner && native_window_generation == context->mOwner->nativeWindowGeneration();
}

struct UploadSourcePublicationContext
{
    FakeState*                    mState = nullptr;
    VulkanInstanceGeneration*     mOwner = nullptr;
    VulkanUploadSourceDescription mReplacementDescription;
    VkBuffer                      mReplacementBuffer = VK_NULL_HANDLE;
    VkDeviceMemory                mReplacementMemory = VK_NULL_HANDLE;
    std::size_t                   mOwnerChecks       = 0;
    std::size_t                   mWindowChecks      = 0;
    bool                          mAttempted         = false;
    bool                          mPublished         = false;
};

bool uploadSourcePublicationOwnerIsCurrent(void* userdata, const VulkanInstanceGeneration& generation) noexcept
{
    auto* context = static_cast<UploadSourcePublicationContext*>(userdata);
    if (!context || !context->mState || !context->mOwner || context->mOwner != &generation)
    {
        return false;
    }
    ++context->mOwnerChecks;
    if (context->mOwnerChecks == 2 && !context->mAttempted)
    {
        context->mAttempted                  = true;
        context->mState->mUploadSourceBuffer = context->mReplacementBuffer;
        context->mState->mUploadSourceMemory = context->mReplacementMemory;
        const VulkanUploadSourceRequest request{ context->mOwner->nativeWindowGeneration(),
                                                 context->mReplacementDescription,
                                                 { context->mOwner, exactMutationOwnerIsCurrent },
                                                 { context->mOwner, exactMutationWindowIsCurrent } };
        context->mPublished = !context->mOwner->acquireUploadSourceGeneration(request);
    }
    return true;
}

bool uploadSourcePublicationWindowIsCurrent(void* userdata, std::uint64_t native_window_generation) noexcept
{
    auto* context = static_cast<UploadSourcePublicationContext*>(userdata);
    if (!context || !context->mOwner || native_window_generation != context->mOwner->nativeWindowGeneration())
    {
        return false;
    }
    ++context->mWindowChecks;
    return true;
}

enum class UploadAbaTarget : std::uint8_t
{
    Source,
    Destination,
    Transfer
};

struct UploadAbaContext
{
    FakeState*                    mState = nullptr;
    VulkanInstanceGeneration*     mOwner = nullptr;
    VulkanUploadSourceDescription mDescription;
    UploadAbaTarget               mTarget       = UploadAbaTarget::Source;
    std::size_t                   mOwnerChecks  = 0;
    std::size_t                   mWindowChecks = 0;
    bool                          mReset        = false;
    bool                          mReacquired   = false;
};

bool uploadAbaOwnerIsCurrent(void* userdata, const VulkanInstanceGeneration& generation) noexcept
{
    auto* context = static_cast<UploadAbaContext*>(userdata);
    if (!context || !context->mState || !context->mOwner || context->mOwner != &generation)
    {
        return false;
    }
    ++context->mOwnerChecks;
    if (context->mOwnerChecks != 1)
    {
        return true;
    }

    if (context->mTarget == UploadAbaTarget::Source)
    {
        context->mReset = context->mOwner->resetUploadSourceGeneration();
        const VulkanUploadSourceRequest request{ context->mOwner->nativeWindowGeneration(),
                                                 context->mDescription,
                                                 { context->mOwner, exactMutationOwnerIsCurrent },
                                                 { context->mOwner, exactMutationWindowIsCurrent } };
        context->mReacquired = context->mReset && !context->mOwner->acquireUploadSourceGeneration(request);
    }
    else if (context->mTarget == UploadAbaTarget::Destination)
    {
        context->mReset = context->mOwner->resetUploadDestinationGeneration();
        const VulkanUploadDestinationRequest request{ context->mOwner->nativeWindowGeneration(),
                                                      context->mDescription,
                                                      { context->mOwner, exactMutationOwnerIsCurrent },
                                                      { context->mOwner, exactMutationWindowIsCurrent } };
        context->mReacquired = context->mReset && !context->mOwner->acquireUploadDestinationGeneration(request);
    }
    else
    {
        context->mReset = context->mOwner->resetUploadTransferGeneration();
        const VulkanUploadTransferRequest request{ context->mOwner->nativeWindowGeneration(),
                                                   context->mDescription,
                                                   { context->mOwner, exactMutationOwnerIsCurrent },
                                                   { context->mOwner, exactMutationWindowIsCurrent } };
        context->mReacquired = context->mReset && !context->mOwner->acquireUploadTransferGeneration(request);
    }
    return true;
}

bool uploadAbaWindowIsCurrent(void* userdata, std::uint64_t native_window_generation) noexcept
{
    auto* context = static_cast<UploadAbaContext*>(userdata);
    if (!context || !context->mOwner || native_window_generation != context->mOwner->nativeWindowGeneration())
    {
        return false;
    }
    ++context->mWindowChecks;
    return true;
}

void emitValidationMessage(FakeState& state, const char* message)
{
    tut::ensure("the validation callback was retained", state.mValidationCallback != nullptr && state.mValidationUserdata != nullptr);
    VkDebugUtilsMessengerCallbackDataEXT callback_data{};
    callback_data.sType    = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CALLBACK_DATA_EXT;
    callback_data.pMessage = message;
    state.mValidationCallback(VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT, VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT,
                              &callback_data, state.mValidationUserdata);
}

} // namespace

namespace tut
{

struct render_vulkan_instance_test
{
};

using render_vulkan_instance_test_group  = test_group<render_vulkan_instance_test, 119>;
using render_vulkan_instance_test_object = render_vulkan_instance_test_group::object;
render_vulkan_instance_test_group render_vulkan_instance_tests("render Vulkan instance");

template<>
template<>
void render_vulkan_instance_test_object::test<1>()
{
    static_assert(!std::is_default_constructible_v<VulkanInstanceGeneration>);
    static_assert(!std::is_copy_constructible_v<VulkanInstanceGeneration>);
    static_assert(!std::is_copy_assignable_v<VulkanInstanceGeneration>);
    static_assert(std::is_nothrow_move_constructible_v<VulkanInstanceGeneration>);
    static_assert(!std::is_move_assignable_v<VulkanInstanceGeneration>);
    static_assert(std::is_nothrow_destructible_v<VulkanInstanceGeneration>);
    static_assert(std::variant_size_v<VulkanInstanceAcquireResult> == 2);
    static_assert(std::is_same_v<std::variant_alternative_t<0, VulkanInstanceAcquireResult>, VulkanInstanceAcquireError>);
    static_assert(std::is_same_v<std::variant_alternative_t<1, VulkanInstanceAcquireResult>, VulkanInstanceGeneration>);
    static_assert(noexcept(acquireVulkanInstanceGeneration(std::declval<const VulkanInstanceRequest&>())));

    const VulkanInstanceAcquireError left{ VulkanInstanceAcquireCode::MissingRequiredInstanceCommand, VK_SUCCESS, std::nullopt,
                                           VulkanInstanceCommand::DestroyInstance };
    ensure("identical errors compare equal", left == left);
}

template<>
template<>
void render_vulkan_instance_test_object::test<2>()
{
    FakeState       state;
    ScopedFakeState scope(state);

    VulkanInstanceRequest request = makeRequest(state);
    request.mGenerationCheck      = {};
    ensureCode(acquireVulkanInstanceGeneration(request), VulkanInstanceAcquireCode::InvalidGenerationCheck);

    state.mGenerationCurrent = false;
    request                  = makeRequest(state);
    ensureCode(acquireVulkanInstanceGeneration(request), VulkanInstanceAcquireCode::StaleWindowGeneration);
    ensure_equals("a stale generation performs no dispatch lookup", state.mVersionCalls, std::size_t{ 0 });

    state.mGenerationCurrent = true;
    request                  = makeRequest(state);
    request.mValidationMode  = static_cast<VulkanInstanceValidationMode>(99);
    ensureCode(acquireVulkanInstanceGeneration(request), VulkanInstanceAcquireCode::InvalidValidationMode);

    request                  = makeRequest(state);
    request.mPortabilityMode = static_cast<VulkanInstancePortabilityMode>(99);
    ensureCode(acquireVulkanInstanceGeneration(request), VulkanInstanceAcquireCode::InvalidPortabilityMode);

    request                           = makeRequest(state);
    request.mRequiredWindowExtensions = {};
    ensureCode(acquireVulkanInstanceGeneration(request), VulkanInstanceAcquireCode::InvalidRequiredWindowExtensions);

    const std::vector<std::string> duplicate_extensions{ VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_SURFACE_EXTENSION_NAME };
    request = makeRequest(state, VulkanInstanceValidationMode::Disabled, VulkanInstancePortabilityMode::Disabled, duplicate_extensions);
    const auto  duplicate_result = acquireVulkanInstanceGeneration(request);
    const auto& duplicate_error  = requireError(duplicate_result);
    ensure("a duplicate request extension reports its exact index",
           duplicate_error.mCode == VulkanInstanceAcquireCode::InvalidRequiredWindowExtensions &&
               duplicate_error.mRequiredExtensionIndex == 1);
}

template<>
template<>
void render_vulkan_instance_test_object::test<3>()
{
    FakeState state;
    state.mMissing = MissingCommand::GlobalEnumerateVersion;
    ScopedFakeState scope(state);

    const auto  result = acquireVulkanInstanceGeneration(makeRequest(state));
    const auto& error  = requireError(result);
    ensure("Stage 30 failure is nested exactly",
           error.mCode == VulkanInstanceAcquireCode::GlobalDispatchFailure && error.mGlobalDispatchError &&
               error.mGlobalDispatchError->mCode == VulkanGlobalDispatchResolutionCode::InsufficientApiVersion &&
               error.mGlobalDispatchError->mCommand == VulkanGlobalCommand::EnumerateInstanceVersion);
    ensure_equals("global failure makes no property query", state.mExtensionCountCalls, std::size_t{ 0 });
}

template<>
template<>
void render_vulkan_instance_test_object::test<4>()
{
    {
        FakeState state;
        state.mExtensionCountResult = VK_ERROR_OUT_OF_HOST_MEMORY;
        ScopedFakeState scope(state);
        const auto      result = acquireVulkanInstanceGeneration(makeRequest(state));
        const auto&     error  = requireError(result);
        ensure("extension query failure preserves VkResult",
               error.mCode == VulkanInstanceAcquireCode::ExtensionEnumerationFailure && error.mResult == VK_ERROR_OUT_OF_HOST_MEMORY);
    }
    {
        FakeState state;
        state.mExtensionCountOverride = 4097;
        ScopedFakeState scope(state);
        const auto      result = acquireVulkanInstanceGeneration(makeRequest(state));
        const auto&     error  = requireError(result);
        ensure("extension count cap reports the observed count",
               error.mCode == VulkanInstanceAcquireCode::ExtensionCountExceeded && error.mObservedCount == 4097);
    }
    {
        FakeState state;
        state.mIncompleteExtensionCountCalls = 1;
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        ensure("a changing count query converges on its second bounded attempt",
               state.mExtensionCountCalls == 2 && state.mExtensionValuesCalls == 1);
    }
    {
        FakeState state;
        state.mIncompleteExtensionValueCalls = 1;
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        ensure("a changing property list converges on its second bounded attempt",
               state.mExtensionCountCalls == 2 && state.mExtensionValuesCalls == 2);
    }
    {
        FakeState state;
        state.mExtensionCountResult = VK_INCOMPLETE;
        ScopedFakeState scope(state);
        const auto      result = acquireVulkanInstanceGeneration(makeRequest(state));
        ensureCode(result, VulkanInstanceAcquireCode::EnumerationRetryLimitExceeded);
        ensure("an incomplete count query is bounded before allocating",
               state.mExtensionCountCalls == 4 && state.mExtensionValuesCalls == 0);
    }
    {
        FakeState state;
        state.mAlwaysIncompleteExtensions = true;
        ScopedFakeState scope(state);
        const auto      result = acquireVulkanInstanceGeneration(makeRequest(state));
        ensureCode(result, VulkanInstanceAcquireCode::EnumerationRetryLimitExceeded);
        ensure_equals("incomplete enumeration is retried four times", state.mExtensionValuesCalls, std::size_t{ 4 });
    }
    {
        FakeState state;
        state.mExtensionValuesResult = VK_ERROR_OUT_OF_HOST_MEMORY;
        ScopedFakeState scope(state);
        const auto      result = acquireVulkanInstanceGeneration(makeRequest(state));
        const auto&     error  = requireError(result);
        ensure("extension value-query failure preserves VkResult",
               error.mCode == VulkanInstanceAcquireCode::ExtensionEnumerationFailure && error.mResult == VK_ERROR_OUT_OF_HOST_MEMORY);
    }
    {
        FakeState state;
        state.mExtensionWrittenOverride = 2;
        ScopedFakeState scope(state);
        const auto      result = acquireVulkanInstanceGeneration(makeRequest(state));
        const auto&     error  = requireError(result);
        ensure("an overreported written count is rejected without reading beyond the allocation",
               error.mCode == VulkanInstanceAcquireCode::ExtensionCountExceeded && error.mObservedCount == 2);
    }
    {
        FakeState state;
        state.mMalformedExtension = true;
        ScopedFakeState scope(state);
        const auto      result = acquireVulkanInstanceGeneration(makeRequest(state));
        const auto&     error  = requireError(result);
        ensure("malformed extension reports its index",
               error.mCode == VulkanInstanceAcquireCode::MalformedExtensionProperty && error.mPropertyIndex == 0);
    }
}

template<>
template<>
void render_vulkan_instance_test_object::test<5>()
{
    {
        FakeState state;
        state.mExtensions       = { VK_KHR_SURFACE_EXTENSION_NAME, VK_EXT_DEBUG_UTILS_EXTENSION_NAME };
        state.mLayerCountResult = VK_ERROR_INITIALIZATION_FAILED;
        ScopedFakeState scope(state);
        const auto      result = acquireVulkanInstanceGeneration(makeRequest(state, VulkanInstanceValidationMode::Required));
        const auto&     error  = requireError(result);
        ensure("layer query failure preserves VkResult",
               error.mCode == VulkanInstanceAcquireCode::LayerEnumerationFailure && error.mResult == VK_ERROR_INITIALIZATION_FAILED);
    }
    {
        FakeState state;
        state.mExtensions         = { VK_KHR_SURFACE_EXTENSION_NAME, VK_EXT_DEBUG_UTILS_EXTENSION_NAME };
        state.mLayerCountOverride = 4097;
        ScopedFakeState scope(state);
        const auto      result = acquireVulkanInstanceGeneration(makeRequest(state, VulkanInstanceValidationMode::Required));
        const auto&     error  = requireError(result);
        ensure("layer count cap reports the observed count",
               error.mCode == VulkanInstanceAcquireCode::LayerCountExceeded && error.mObservedCount == 4097);
    }
    {
        FakeState state;
        state.mExtensions        = { VK_KHR_SURFACE_EXTENSION_NAME, VK_EXT_DEBUG_UTILS_EXTENSION_NAME };
        state.mLayers            = { "VK_LAYER_KHRONOS_validation" };
        state.mLayerValuesResult = VK_ERROR_OUT_OF_HOST_MEMORY;
        ScopedFakeState scope(state);
        const auto      result = acquireVulkanInstanceGeneration(makeRequest(state, VulkanInstanceValidationMode::Required));
        const auto&     error  = requireError(result);
        ensure("layer value-query failure preserves VkResult",
               error.mCode == VulkanInstanceAcquireCode::LayerEnumerationFailure && error.mResult == VK_ERROR_OUT_OF_HOST_MEMORY);
    }
    {
        FakeState state;
        state.mExtensions     = { VK_KHR_SURFACE_EXTENSION_NAME, VK_EXT_DEBUG_UTILS_EXTENSION_NAME };
        state.mLayers         = { "VK_LAYER_KHRONOS_validation" };
        state.mMalformedLayer = true;
        ScopedFakeState scope(state);
        const auto      result = acquireVulkanInstanceGeneration(makeRequest(state, VulkanInstanceValidationMode::Required));
        const auto&     error  = requireError(result);
        ensure("malformed layer reports its index",
               error.mCode == VulkanInstanceAcquireCode::MalformedLayerProperty && error.mPropertyIndex == 0);
    }
}

template<>
template<>
void render_vulkan_instance_test_object::test<6>()
{
    {
        FakeState state;
        state.mExtensions = { VK_EXT_DEBUG_UTILS_EXTENSION_NAME };
        ScopedFakeState scope(state);
        const auto      result = acquireVulkanInstanceGeneration(makeRequest(state));
        const auto&     error  = requireError(result);
        ensure("the missing SDL extension retains its exact index",
               error.mCode == VulkanInstanceAcquireCode::MissingRequiredWindowExtension && error.mRequiredExtensionIndex == 0);
    }
    {
        FakeState                state;
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        ensure("disabled validation creates a live instance without a messenger",
               owner.instance() == state.mInstance && !owner.validationEnabled() && owner.enabledLayers().empty());
        ensure_equals("disabled validation never enumerates layers", state.mLayerCountCalls, std::size_t{ 0 });
        ensure("the exact API floor is requested and retained",
               state.mRequestedApiVersion == RENDERER_VULKAN_API_VERSION && owner.apiVersion() == RENDERER_VULKAN_API_VERSION);
        owner.reset();
        ensure_equals("reset destroys the instance once", state.mDestroyInstanceCalls, std::size_t{ 1 });
    }
}

template<>
template<>
void render_vulkan_instance_test_object::test<7>()
{
    {
        FakeState state;
        state.mExtensions = { VK_KHR_SURFACE_EXTENSION_NAME, PORTABILITY_EXTENSION };
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        ensure("disabled portability neither enables the extension nor sets the flag",
               !owner.portabilityEnumerationEnabled() && !owner.isExtensionEnabled(PORTABILITY_EXTENSION) && state.mInstanceFlags == 0);
    }
    {
        FakeState state;
        state.mExtensions = { VK_KHR_SURFACE_EXTENSION_NAME, VK_EXT_DEBUG_UTILS_EXTENSION_NAME };
        ScopedFakeState scope(state);
        ensureCode(acquireVulkanInstanceGeneration(makeRequest(state, VulkanInstanceValidationMode::Required)),
                   VulkanInstanceAcquireCode::MissingValidationLayer);
    }
    {
        FakeState state;
        state.mLayers = { "VK_LAYER_KHRONOS_validation" };
        ScopedFakeState scope(state);
        ensureCode(acquireVulkanInstanceGeneration(makeRequest(state, VulkanInstanceValidationMode::Required)),
                   VulkanInstanceAcquireCode::MissingValidationExtension);
    }
    {
        FakeState state;
        state.mExtensions = { VK_KHR_SURFACE_EXTENSION_NAME, VK_EXT_DEBUG_UTILS_EXTENSION_NAME };
        state.mLayers     = { "VK_LAYER_KHRONOS_validation" };
        ScopedFakeState                scope(state);
        const std::vector<std::string> required{ VK_KHR_SURFACE_EXTENSION_NAME, VK_EXT_DEBUG_UTILS_EXTENSION_NAME };
        VulkanInstanceGeneration       owner = takeGeneration(acquireVulkanInstanceGeneration(
            makeRequest(state, VulkanInstanceValidationMode::Required, VulkanInstancePortabilityMode::Disabled, required)));
        ensure("required validation owns its messenger", owner.validationEnabled());
        ensure("debug utils is not duplicated",
               std::count(owner.enabledExtensions().begin(), owner.enabledExtensions().end(), VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 1);
        ensure("the exact validation layer is retained",
               owner.enabledLayers() == std::vector<std::string>{ "VK_LAYER_KHRONOS_validation" });
        owner.reset();
        const std::vector<Event> expected{ Event::CreateInstance, Event::CreateDebugMessenger, Event::DestroyDebugMessenger,
                                           Event::DestroyInstance };
        ensure("debug teardown precedes instance teardown", state.mEvents == expected);
    }
    {
        FakeState state;
        state.mExtensions = { VK_KHR_SURFACE_EXTENSION_NAME, PORTABILITY_EXTENSION };
        ScopedFakeState scope(state);
        auto            result = acquireVulkanInstanceGeneration(
            makeRequest(state, VulkanInstanceValidationMode::Disabled, VulkanInstancePortabilityMode::EnableIfAvailable));
#if defined(VK_KHR_portability_enumeration)
        VulkanInstanceGeneration owner = takeGeneration(std::move(result));
        ensure("advertised portability is enabled once with its create flag",
               owner.portabilityEnumerationEnabled() && owner.isExtensionEnabled(PORTABILITY_EXTENSION) &&
                   (state.mInstanceFlags & VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR) != 0);
#else
        ensureCode(result, VulkanInstanceAcquireCode::PortabilityPolicyUnavailable);
        ensure("older headers reject an unsupported portability policy before instance creation", state.mEvents.empty());
#endif
    }
}

template<>
template<>
void render_vulkan_instance_test_object::test<8>()
{
    {
        FakeState state;
        state.mInstanceResult = VK_ERROR_INCOMPATIBLE_DRIVER;
        ScopedFakeState scope(state);
        const auto      result = acquireVulkanInstanceGeneration(makeRequest(state));
        const auto&     error  = requireError(result);
        ensure("instance failure preserves VkResult and owns no poisoned output",
               error.mCode == VulkanInstanceAcquireCode::InstanceCreationFailure && error.mResult == VK_ERROR_INCOMPATIBLE_DRIVER &&
                   state.mDestroyInstanceCalls == 0);
    }
    {
        FakeState state;
        state.mNullInstance = true;
        ScopedFakeState scope(state);
        ensureCode(acquireVulkanInstanceGeneration(makeRequest(state)), VulkanInstanceAcquireCode::NullInstanceOnSuccess);
        ensure_equals("a null instance has no destruction obligation", state.mDestroyInstanceCalls, std::size_t{ 0 });
    }
}

template<>
template<>
void render_vulkan_instance_test_object::test<9>()
{
    {
        FakeState state;
        state.mMissing = MissingCommand::DestroyInstance;
        ScopedFakeState scope(state);
        const auto      result = acquireVulkanInstanceGeneration(makeRequest(state));
        const auto&     error  = requireError(result);
        ensure("missing destroy is reported as the exact nonrecoverable loader breach",
               error.mCode == VulkanInstanceAcquireCode::MissingRequiredInstanceCommand &&
                   error.mCommand == VulkanInstanceCommand::DestroyInstance && state.mDestroyInstanceCalls == 0);
    }

    constexpr std::array missing_debug_commands{ MissingCommand::CreateDebugMessenger, MissingCommand::DestroyDebugMessenger };
    constexpr std::array reported_debug_commands{ VulkanInstanceCommand::CreateDebugUtilsMessenger,
                                                  VulkanInstanceCommand::DestroyDebugUtilsMessenger };
    for (std::size_t index = 0; index < missing_debug_commands.size(); ++index)
    {
        FakeState state;
        state.mMissing    = missing_debug_commands[index];
        state.mExtensions = { VK_KHR_SURFACE_EXTENSION_NAME, VK_EXT_DEBUG_UTILS_EXTENSION_NAME };
        state.mLayers     = { "VK_LAYER_KHRONOS_validation" };
        ScopedFakeState scope(state);
        const auto      result = acquireVulkanInstanceGeneration(makeRequest(state, VulkanInstanceValidationMode::Required));
        const auto&     error  = requireError(result);
        ensure("missing debug command is reported and the instance rolls back",
               error.mCode == VulkanInstanceAcquireCode::MissingRequiredInstanceCommand &&
                   error.mCommand == reported_debug_commands[index] && state.mDestroyInstanceCalls == 1 && state.mDestroyDebugCalls == 0);
    }
}

template<>
template<>
void render_vulkan_instance_test_object::test<10>()
{
    {
        FakeState state;
        state.mExtensions  = { VK_KHR_SURFACE_EXTENSION_NAME, VK_EXT_DEBUG_UTILS_EXTENSION_NAME };
        state.mLayers      = { "VK_LAYER_KHRONOS_validation" };
        state.mDebugResult = VK_ERROR_OUT_OF_HOST_MEMORY;
        ScopedFakeState scope(state);
        const auto      result = acquireVulkanInstanceGeneration(makeRequest(state, VulkanInstanceValidationMode::Required));
        const auto&     error  = requireError(result);
        ensure("debug failure preserves VkResult and rolls back only the instance",
               error.mCode == VulkanInstanceAcquireCode::DebugMessengerCreationFailure && error.mResult == VK_ERROR_OUT_OF_HOST_MEMORY &&
                   state.mDestroyDebugCalls == 0 && state.mDestroyInstanceCalls == 1);
    }
    {
        FakeState state;
        state.mExtensions         = { VK_KHR_SURFACE_EXTENSION_NAME, VK_EXT_DEBUG_UTILS_EXTENSION_NAME };
        state.mLayers             = { "VK_LAYER_KHRONOS_validation" };
        state.mNullDebugMessenger = true;
        ScopedFakeState scope(state);
        ensureCode(acquireVulkanInstanceGeneration(makeRequest(state, VulkanInstanceValidationMode::Required)),
                   VulkanInstanceAcquireCode::NullDebugMessengerOnSuccess);
        ensure("a null messenger rolls back only the instance", state.mDestroyDebugCalls == 0 && state.mDestroyInstanceCalls == 1);
    }
}

template<>
template<>
void render_vulkan_instance_test_object::test<11>()
{
    {
        FakeState state;
        state.mFailGenerationCheck = 2;
        ScopedFakeState scope(state);
        ensureCode(acquireVulkanInstanceGeneration(makeRequest(state)), VulkanInstanceAcquireCode::StaleWindowGeneration);
        ensure("staleness after property enumeration publishes no native object",
               state.mExtensionValuesCalls == 1 && state.mEvents.empty());
    }
    {
        FakeState state;
        state.mFailGenerationCheck = 3;
        ScopedFakeState scope(state);
        ensureCode(acquireVulkanInstanceGeneration(makeRequest(state)), VulkanInstanceAcquireCode::StaleWindowGeneration);
        ensure("staleness immediately before instance creation publishes no native object", state.mEvents.empty());
    }
    {
        FakeState state;
        state.mFailGenerationCheck = 4;
        ScopedFakeState scope(state);
        ensureCode(acquireVulkanInstanceGeneration(makeRequest(state)), VulkanInstanceAcquireCode::StaleWindowGeneration);
        ensure_equals("staleness after instance creation rolls it back", state.mDestroyInstanceCalls, std::size_t{ 1 });
    }
    {
        FakeState state;
        state.mExtensions          = { VK_KHR_SURFACE_EXTENSION_NAME, VK_EXT_DEBUG_UTILS_EXTENSION_NAME };
        state.mLayers              = { "VK_LAYER_KHRONOS_validation" };
        state.mFailGenerationCheck = 5;
        ScopedFakeState scope(state);
        ensureCode(acquireVulkanInstanceGeneration(makeRequest(state, VulkanInstanceValidationMode::Required)),
                   VulkanInstanceAcquireCode::StaleWindowGeneration);
        ensure("final staleness destroys messenger before instance",
               state.mEvents == std::vector<Event>{ Event::CreateInstance, Event::CreateDebugMessenger, Event::DestroyDebugMessenger,
                                                    Event::DestroyInstance });
    }
}

template<>
template<>
void render_vulkan_instance_test_object::test<12>()
{
    FakeState state;
    state.mExtensions = { VK_KHR_SURFACE_EXTENSION_NAME, VK_EXT_DEBUG_UTILS_EXTENSION_NAME };
    state.mLayers     = { "VK_LAYER_KHRONOS_validation" };
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration first =
        takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state, VulkanInstanceValidationMode::Required)));

    emitValidationMessage(state, "before move");
    VulkanInstanceGeneration moved(std::move(first));
    emitValidationMessage(state, "after move");

    ensure("a move transfers the native handle", first.instance() == VK_NULL_HANDLE && moved.instance() == state.mInstance);
    const VulkanValidationSnapshot snapshot = moved.validationSnapshot();
    ensure("callback userdata remains stable across the owner move",
           snapshot.mMessageCount == 2 && snapshot.firstMessage() == "before move");
    moved.reset();
    moved.reset();
    ensure("reset is idempotent and preserves teardown order",
           state.mDestroyDebugCalls == 1 && state.mDestroyInstanceCalls == 1 &&
               state.mEvents == std::vector<Event>{ Event::CreateInstance, Event::CreateDebugMessenger, Event::DestroyDebugMessenger,
                                                    Event::DestroyInstance });

    FakeState concurrent_state;
    concurrent_state.mExtensions = { VK_KHR_SURFACE_EXTENSION_NAME, VK_EXT_DEBUG_UTILS_EXTENSION_NAME };
    concurrent_state.mLayers     = { "VK_LAYER_KHRONOS_validation" };
    scope.use(concurrent_state);
    VulkanInstanceGeneration concurrent =
        takeGeneration(acquireVulkanInstanceGeneration(makeRequest(concurrent_state, VulkanInstanceValidationMode::Required)));

    const auto callback = concurrent_state.mValidationCallback;
    void*      userdata = concurrent_state.mValidationUserdata;
    ensure("the concurrent validation callback fixture is published", callback != nullptr && userdata != nullptr);

    std::atomic<bool> start{ false };
    std::atomic<bool> done{ false };
    std::atomic<bool> coherent{ true };
    std::thread       reader(
        [&]
        {
            while (!start.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
            do
            {
                const VulkanValidationSnapshot current = concurrent.validationSnapshot();
                if ((current.mMessageCount == 0) != current.firstMessage().empty())
                {
                    coherent.store(false, std::memory_order_relaxed);
                }
                std::this_thread::yield();
            } while (!done.load(std::memory_order_acquire));
        });
    std::thread writer(
        [&]
        {
            while (!start.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
            VkDebugUtilsMessengerCallbackDataEXT callback_data{};
            callback_data.sType    = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CALLBACK_DATA_EXT;
            callback_data.pMessage = "concurrent callback";
            for (std::uint32_t index = 0; index < 10000; ++index)
            {
                callback(VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT, VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT, &callback_data,
                         userdata);
            }
            done.store(true, std::memory_order_release);
        });
    start.store(true, std::memory_order_release);
    writer.join();
    reader.join();

    const VulkanValidationSnapshot concurrent_snapshot = concurrent.validationSnapshot();
    ensure("concurrent validation snapshots keep their count and first message coherent",
           coherent.load(std::memory_order_relaxed) && concurrent_snapshot.mMessageCount == 10000 &&
               concurrent_snapshot.firstMessage() == "concurrent callback");
}

template<>
template<>
void render_vulkan_instance_test_object::test<13>()
{
    FakeState       state;
    ScopedFakeState scope(state);
    const auto      result = VulkanInstanceDetail::acquire(makeRequest(state), failAllocation);
    ensureCode(result, VulkanInstanceAcquireCode::AllocationFailure);
    ensure("allocation failure occurs before instance ownership", state.mEvents.empty() && state.mDestroyInstanceCalls == 0);
}

template<>
template<>
void render_vulkan_instance_test_object::test<14>()
{
    static_assert(std::variant_size_v<VulkanSurfaceCreateOutcome> == 2);
    static_assert(std::is_same_v<std::variant_alternative_t<0, VulkanSurfaceCreateOutcome>, VulkanSurfacePlatformFailure>);
    static_assert(std::is_same_v<std::variant_alternative_t<1, VulkanSurfaceCreateOutcome>, VkResult>);
    static_assert(std::is_same_v<VulkanSurfaceAcquireResult, std::optional<VulkanSurfaceAcquireError>>);
    static_assert(
        noexcept(std::declval<VulkanInstanceGeneration&>().acquireSurfaceGeneration(std::declval<const VulkanSurfaceRequest&>())));
    static_assert(noexcept(std::declval<VulkanInstanceGeneration&>().resetSurfaceGeneration()));
    static_assert(noexcept(std::declval<const VulkanInstanceGeneration&>().hasSurfaceGeneration()));
    static_assert(noexcept(std::declval<const VulkanInstanceGeneration&>().surface()));
    static_assert(noexcept(std::declval<const VulkanInstanceGeneration&>().surfaceNativeWindowGeneration()));

    const VulkanSurfaceAcquireError left{ VulkanSurfaceAcquireCode::MissingRequiredInstanceCommand, std::nullopt,
                                          VulkanSurfaceCommand::DestroySurface };
    ensure("identical surface errors compare equal", left == left);
    ensure("the platform marker is a distinct success-free outcome", VulkanSurfacePlatformFailure{} == VulkanSurfacePlatformFailure{});
}

template<>
template<>
void render_vulkan_instance_test_object::test<15>()
{
    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));

    VulkanSurfaceRequest request = makeSurfaceRequest(state, owner);
    request.mInstanceOwnerCheck  = {};
    ensureSurfaceCode(owner.acquireSurfaceGeneration(request), VulkanSurfaceAcquireCode::InvalidInstanceOwnerCheck);

    request                        = makeSurfaceRequest(state, owner);
    request.mWindowGenerationCheck = {};
    ensureSurfaceCode(owner.acquireSurfaceGeneration(request), VulkanSurfaceAcquireCode::InvalidWindowGenerationCheck);

    request                          = makeSurfaceRequest(state, owner);
    request.mCreateOperation.mCreate = nullptr;
    ensureSurfaceCode(owner.acquireSurfaceGeneration(request), VulkanSurfaceAcquireCode::InvalidCreateOperation);

    request                         = makeSurfaceRequest(state, owner);
    request.mNativeWindowGeneration = 0;
    ensureSurfaceCode(owner.acquireSurfaceGeneration(request), VulkanSurfaceAcquireCode::InvalidNativeWindowGeneration);

    request                         = makeSurfaceRequest(state, owner);
    request.mNativeWindowGeneration = 41;
    ensureSurfaceCode(owner.acquireSurfaceGeneration(request), VulkanSurfaceAcquireCode::NativeWindowGenerationMismatch);

    owner.reset();
    request = makeSurfaceRequest(state, owner);
    ensureSurfaceCode(owner.acquireSurfaceGeneration(request), VulkanSurfaceAcquireCode::InstanceNotLive);
    ensure("invalid requests never resolve, create, or destroy a surface",
           state.mDestroySurfaceResolutionCalls == 0 && state.mCreateSurfaceCalls == 0 && state.mDestroySurfaceCalls == 0);
}

template<>
template<>
void render_vulkan_instance_test_object::test<16>()
{
    {
        FakeState state;
        state.mExtensions = { "VK_EXT_stage_34_fake" };
        ScopedFakeState                scope(state);
        const std::vector<std::string> required{ "VK_EXT_stage_34_fake" };
        VulkanInstanceGeneration       owner = takeGeneration(acquireVulkanInstanceGeneration(
            makeRequest(state, VulkanInstanceValidationMode::Disabled, VulkanInstancePortabilityMode::Disabled, required)));
        ensureSurfaceCode(owner.acquireSurfaceGeneration(makeSurfaceRequest(state, owner)),
                          VulkanSurfaceAcquireCode::MissingSurfaceExtension);
        ensure("the extension gate precedes command resolution and platform creation",
               state.mDestroySurfaceResolutionCalls == 0 && state.mCreateSurfaceCalls == 0);
    }
    {
        FakeState                state;
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner          = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        state.mMissing                          = MissingCommand::DestroySurface;
        const VulkanSurfaceAcquireResult result = owner.acquireSurfaceGeneration(makeSurfaceRequest(state, owner));
        const VulkanSurfaceAcquireError& error  = requireSurfaceError(result);
        ensure("a missing destroy command is exact and fails before platform creation",
               error.mCode == VulkanSurfaceAcquireCode::MissingRequiredInstanceCommand &&
                   error.mCommand == VulkanSurfaceCommand::DestroySurface && !error.mResult && state.mDestroySurfaceResolutionCalls == 1 &&
                   state.mCreateSurfaceCalls == 0);
    }
    {
        FakeState                state;
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner   = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        VulkanSurfaceRequest     request = makeSurfaceRequest(state, owner);
        ensure("the first surface acquisition succeeds", !owner.acquireSurfaceGeneration(request));
        ensureSurfaceCode(owner.acquireSurfaceGeneration(request), VulkanSurfaceAcquireCode::SurfaceAlreadyOwned);
        ensure("duplicate acquisition preserves the first child and performs no second create",
               owner.hasSurfaceGeneration() && owner.surface() == state.mSurface && state.mCreateSurfaceCalls == 1);
    }
}

template<>
template<>
void render_vulkan_instance_test_object::test<17>()
{
    for (std::size_t scenario = 0; scenario < 5; ++scenario)
    {
        FakeState                state;
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner   = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        VulkanSurfaceRequest     request = makeSurfaceRequest(state, owner);

        if (scenario <= 1)
        {
            state.mSurfacePlatformFailure = true;
            state.mPoisonSurfaceOutput    = scenario == 1;
        }
        else if (scenario <= 3)
        {
            state.mSurfaceResult       = VK_ERROR_OUT_OF_HOST_MEMORY;
            state.mPoisonSurfaceOutput = scenario == 3;
        }
        else
        {
            state.mNullSurface = true;
        }

        const VulkanSurfaceAcquireResult result = owner.acquireSurfaceGeneration(request);
        const VulkanSurfaceAcquireError& error  = requireSurfaceError(result);
        if (scenario <= 1)
        {
            ensure("a platform failure has no invented Vulkan result",
                   error.mCode == VulkanSurfaceAcquireCode::PlatformCreationFailure && !error.mResult);
        }
        else if (scenario <= 3)
        {
            ensure("a Vulkan surface failure retains its exact result",
                   error.mCode == VulkanSurfaceAcquireCode::SurfaceCreationFailure && error.mResult == VK_ERROR_OUT_OF_HOST_MEMORY);
        }
        else
        {
            ensure("a null success is distinct and retains VK_SUCCESS",
                   error.mCode == VulkanSurfaceAcquireCode::NullSurfaceOnSuccess && error.mResult == VK_SUCCESS);
        }
        ensure("failed and null outputs publish no child and never destroy poisoned handles",
               !owner.hasSurfaceGeneration() && owner.surface() == VK_NULL_HANDLE && state.mDestroySurfaceCalls == 0);

        state.mSurfacePlatformFailure = false;
        state.mSurfaceResult          = VK_SUCCESS;
        state.mNullSurface            = false;
        state.mPoisonSurfaceOutput    = false;
        ensure("the parent remains reusable after a failed platform transaction", !owner.acquireSurfaceGeneration(request));
    }
}

template<>
template<>
void render_vulkan_instance_test_object::test<18>()
{
    for (std::size_t boundary = 1; boundary <= 5; ++boundary)
    {
        for (bool fail_instance_owner : { true, false })
        {
            FakeState                state;
            ScopedFakeState          scope(state);
            VulkanInstanceGeneration owner   = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
            VulkanSurfaceRequest     request = makeSurfaceRequest(state, owner);
            if (fail_instance_owner)
            {
                state.mFailInstanceOwnerCheck = boundary;
            }
            else
            {
                state.mFailSurfaceWindowCheck = boundary;
            }

            const VulkanSurfaceAcquireResult result = owner.acquireSurfaceGeneration(request);
            ensureSurfaceCode(result, fail_instance_owner ? VulkanSurfaceAcquireCode::StaleInstanceOwner
                                                          : VulkanSurfaceAcquireCode::StaleWindowGeneration);
            ensure_equals("the selected freshness boundary is reached exactly", state.mInstanceOwnerChecks, boundary);
            ensure_equals("window freshness short-circuits only after a stale instance owner", state.mSurfaceWindowChecks,
                          fail_instance_owner ? boundary - 1 : boundary);

            const std::size_t expected_resolution_calls = boundary >= 3 ? 1 : 0;
            const std::size_t expected_native_calls     = boundary == 5 ? 1 : 0;
            ensure("freshness stops at the exact native-object boundary",
                   state.mDestroySurfaceResolutionCalls == expected_resolution_calls &&
                       state.mCreateSurfaceCalls == expected_native_calls && state.mDestroySurfaceCalls == expected_native_calls &&
                       !owner.hasSurfaceGeneration());
        }
    }
}

template<>
template<>
void render_vulkan_instance_test_object::test<19>()
{
    FakeState state;
    state.mExtensions = { VK_KHR_SURFACE_EXTENSION_NAME, VK_EXT_DEBUG_UTILS_EXTENSION_NAME };
    state.mLayers     = { "VK_LAYER_KHRONOS_validation" };
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner =
        takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state, VulkanInstanceValidationMode::Required)));

    ensure("validated surface acquisition succeeds", !owner.acquireSurfaceGeneration(makeSurfaceRequest(state, owner)));
    ensure("the child retains exact scalar identity",
           owner.hasSurfaceGeneration() && owner.surface() == state.mSurface && owner.surfaceNativeWindowGeneration() == 42);
    ensure("creation and destroy resolution use the exact parent with null allocation policy",
           state.mDestroySurfaceResolutionInstance == state.mInstance && state.mCreateSurfaceInstance == state.mInstance &&
               state.mCreateSurfaceAllocationCallbacks == nullptr && state.mDestroySurfaceCalls == 0);

    owner.resetSurfaceGeneration();
    ensure("explicit child reset leaves validation and the instance live",
           !owner.hasSurfaceGeneration() && owner.surface() == VK_NULL_HANDLE && owner.surfaceNativeWindowGeneration() == 0 &&
               owner.validationEnabled() && owner.instance() == state.mInstance && state.mDestroySurfaceCalls == 1 &&
               state.mDestroyDebugCalls == 0 && state.mDestroyInstanceCalls == 0);
    emitValidationMessage(state, "validation after surface reset");
    const VulkanValidationSnapshot snapshot = owner.validationSnapshot();
    ensure("validation remains observable after surface destruction",
           snapshot.mMessageCount == 1 && snapshot.firstMessage() == "validation after surface reset");

    owner.resetSurfaceGeneration();
    owner.reset();
    ensure("surface teardown is idempotent and precedes validation and instance teardown",
           state.mDestroySurfaceCalls == 1 && state.mDestroySurfaceInstance == state.mInstance &&
               state.mDestroyedSurface == state.mSurface && state.mDestroySurfaceAllocationCallbacks == nullptr &&
               state.mEvents == std::vector<Event>{ Event::CreateInstance, Event::CreateDebugMessenger, Event::CreateSurface,
                                                    Event::DestroySurface, Event::DestroyDebugMessenger, Event::DestroyInstance });
}

template<>
template<>
void render_vulkan_instance_test_object::test<20>()
{
    {
        FakeState state;
        state.mExtensions = { VK_KHR_SURFACE_EXTENSION_NAME, VK_EXT_DEBUG_UTILS_EXTENSION_NAME };
        state.mLayers     = { "VK_LAYER_KHRONOS_validation" };
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration first =
            takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state, VulkanInstanceValidationMode::Required)));
        ensure("surface acquisition before a parent move succeeds", !first.acquireSurfaceGeneration(makeSurfaceRequest(state, first)));

        VulkanInstanceGeneration moved(std::move(first));
        ensure("the parent move transfers the private child allocation",
               !first.hasSurfaceGeneration() && first.surface() == VK_NULL_HANDLE && moved.hasSurfaceGeneration() &&
                   moved.surface() == state.mSurface && moved.surfaceNativeWindowGeneration() == 42);
        first.reset();
        ensure_equals("resetting the moved-from parent destroys no surface", state.mDestroySurfaceCalls, std::size_t{ 0 });
        moved.reset();
        ensure("the moved-to parent owns the one complete teardown chain",
               state.mEvents == std::vector<Event>{ Event::CreateInstance, Event::CreateDebugMessenger, Event::CreateSurface,
                                                    Event::DestroySurface, Event::DestroyDebugMessenger, Event::DestroyInstance } &&
                   state.mDestroySurfaceCalls == 1 && state.mDestroyDebugCalls == 1 && state.mDestroyInstanceCalls == 1);
    }
    {
        FakeState                state;
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration first = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        VulkanInstanceGeneration moved(std::move(first));
        ensure("a request bound after the parent move succeeds", !moved.acquireSurfaceGeneration(makeSurfaceRequest(state, moved)));
        ensure("the post-move owner check receives the exact moved-to object",
               state.mExpectedInstanceOwner == &moved && moved.hasSurfaceGeneration());
    }
}

template<>
template<>
void render_vulkan_instance_test_object::test<21>()
{
    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner   = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    VulkanSurfaceRequest     request = makeSurfaceRequest(state, owner);

    const VulkanSurfaceAcquireResult result = VulkanInstanceDetail::acquireSurface(owner, request, failAllocation);
    ensureSurfaceCode(result, VulkanSurfaceAcquireCode::AllocationFailure);
    ensure("allocation fails after safe destroy resolution but before platform creation",
           state.mDestroySurfaceResolutionCalls == 1 && state.mCreateSurfaceCalls == 0 && state.mDestroySurfaceCalls == 0 &&
               !owner.hasSurfaceGeneration() && owner.instance() == state.mInstance);
    ensure("the live parent can retry after allocation failure", !owner.acquireSurfaceGeneration(request));
}

template<>
template<>
void render_vulkan_instance_test_object::test<22>()
{
    static_assert(std::is_same_v<VulkanPresentationDeviceAcquireResult, std::optional<VulkanPresentationDeviceAcquireError>>);
    static_assert(noexcept(std::declval<VulkanInstanceGeneration&>().acquirePresentationDeviceGeneration(
        std::declval<const VulkanPresentationDeviceRequest&>())));
    static_assert(noexcept(std::declval<VulkanInstanceGeneration&>().resetPresentationDeviceGeneration()));
    static_assert(noexcept(std::declval<const VulkanInstanceGeneration&>().hasPresentationDeviceGeneration()));

    const VulkanPresentationDeviceAcquireError left{ VulkanPresentationDeviceAcquireCode::ResolutionFailure,
                                                     VulkanPhysicalDeviceResolutionError{
                                                         VulkanPhysicalDeviceResolutionCode::NoSuitablePhysicalDevice } };
    ensure("identical presentation-device errors compare equal", left == left);

    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    ensure("an owner without a selected device exposes neutral observations",
           !owner.hasPresentationDeviceGeneration() && owner.physicalDevice() == VK_NULL_HANDLE &&
               owner.physicalDeviceIndex() == std::numeric_limits<std::uint32_t>::max() &&
               owner.presentationQueueFamilyIndex() == VK_QUEUE_FAMILY_IGNORED && owner.requiredDeviceExtensions().empty() &&
               !owner.portabilitySubsetRequired());

    VulkanPresentationDeviceRequest request = makePresentationDeviceRequest(state, owner);
    request.mInstanceOwnerCheck             = {};
    ensurePresentationDeviceCode(owner.acquirePresentationDeviceGeneration(request),
                                 VulkanPresentationDeviceAcquireCode::InvalidInstanceOwnerCheck);

    request                        = makePresentationDeviceRequest(state, owner);
    request.mWindowGenerationCheck = {};
    ensurePresentationDeviceCode(owner.acquirePresentationDeviceGeneration(request),
                                 VulkanPresentationDeviceAcquireCode::InvalidWindowGenerationCheck);

    request                         = makePresentationDeviceRequest(state, owner);
    request.mNativeWindowGeneration = 0;
    ensurePresentationDeviceCode(owner.acquirePresentationDeviceGeneration(request),
                                 VulkanPresentationDeviceAcquireCode::InvalidNativeWindowGeneration);

    request = makePresentationDeviceRequest(state, owner);
    ensurePresentationDeviceCode(owner.acquirePresentationDeviceGeneration(request), VulkanPresentationDeviceAcquireCode::SurfaceNotLive);

    ensure("surface acquisition succeeds for presentation preflight", !owner.acquireSurfaceGeneration(makeSurfaceRequest(state, owner)));
    request                         = makePresentationDeviceRequest(state, owner);
    request.mNativeWindowGeneration = 41;
    ensurePresentationDeviceCode(owner.acquirePresentationDeviceGeneration(request),
                                 VulkanPresentationDeviceAcquireCode::NativeWindowGenerationMismatch);

    request                     = makePresentationDeviceRequest(state, owner);
    state.mInstanceOwnerCurrent = false;
    ensurePresentationDeviceCode(owner.acquirePresentationDeviceGeneration(request),
                                 VulkanPresentationDeviceAcquireCode::StaleInstanceOwner);
    state.mInstanceOwnerCurrent = true;
    state.mSurfaceWindowCurrent = false;
    ensurePresentationDeviceCode(owner.acquirePresentationDeviceGeneration(request),
                                 VulkanPresentationDeviceAcquireCode::StaleWindowGeneration);
    state.mSurfaceWindowCurrent = true;

    owner.reset();
    request = makePresentationDeviceRequest(state, owner);
    ensurePresentationDeviceCode(owner.acquirePresentationDeviceGeneration(request), VulkanPresentationDeviceAcquireCode::InstanceNotLive);
    ensure_equals("preflight and stale provenance failures make no physical-device query", state.mPhysicalCountCalls, std::size_t{ 0 });
}

template<>
template<>
void render_vulkan_instance_test_object::test<23>()
{
    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    ensure("surface acquisition succeeds before device resolution", !owner.acquireSurfaceGeneration(makeSurfaceRequest(state, owner)));

    state.mPhysicalEnumerationResult = VK_ERROR_DEVICE_LOST;
    const VulkanPresentationDeviceAcquireResult result =
        owner.acquirePresentationDeviceGeneration(makePresentationDeviceRequest(state, owner));
    const VulkanPresentationDeviceAcquireError& error = requirePresentationDeviceError(result);
    ensure("the parent preserves the exact nested resolver failure",
           error.mCode == VulkanPresentationDeviceAcquireCode::ResolutionFailure && error.mResolutionError &&
               error.mResolutionError->mCode == VulkanPhysicalDeviceResolutionCode::PhysicalDeviceEnumerationFailure &&
               error.mResolutionError->mCommand == VulkanPhysicalDeviceCommand::EnumeratePhysicalDevices &&
               error.mResolutionError->mResult == VK_ERROR_DEVICE_LOST && error.mResolutionError->mEnumerationAttempt == 1);
    ensure("resolver failure publishes no child and leaves the exact parent surface reusable",
           !owner.hasPresentationDeviceGeneration() && owner.hasSurfaceGeneration() && owner.surface() == state.mSurface &&
               state.mPhysicalCountCalls == 1 && state.mPhysicalListCalls == 0);

    state.mPhysicalEnumerationResult = VK_SUCCESS;
    ensure("the parent can retry selection after a resolver failure",
           !owner.acquirePresentationDeviceGeneration(makePresentationDeviceRequest(state, owner)) &&
               owner.hasPresentationDeviceGeneration());
}

template<>
template<>
void render_vulkan_instance_test_object::test<24>()
{
    FakeState state;
    state.mDeviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME,
                                "VK_KHR_portability_subset" };
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    ensure("surface acquisition succeeds before the canonical selection",
           !owner.acquireSurfaceGeneration(makeSurfaceRequest(state, owner)));

    const VulkanPresentationDeviceRequest request = makePresentationDeviceRequest(state, owner);
    ensure("presentation-device acquisition succeeds", !owner.acquirePresentationDeviceGeneration(request));
    const std::span<const std::string_view> required = owner.requiredDeviceExtensions();
    ensure("the parent exposes the selected device and exact unified queue",
           owner.hasPresentationDeviceGeneration() && owner.physicalDevice() == state.mPhysicalDevice && owner.physicalDeviceIndex() == 0 &&
               owner.physicalDeviceProperties().apiVersion == VK_API_VERSION_1_1 &&
               std::string_view(owner.physicalDeviceProperties().deviceName) == "fake-presentation-device" &&
               owner.presentationQueueFamilyIndex() == 0 && owner.presentationQueueFamilyProperties().queueCount == 1 &&
               (owner.presentationQueueFamilyProperties().queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0);
    ensure("the exact device-create obligations are retained in deterministic order",
           owner.portabilitySubsetRequired() && owner.swapchainMaintenance1Supported() && required.size() == 3 &&
               required[0] == VK_KHR_SWAPCHAIN_EXTENSION_NAME && required[1] == VK_KHR_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME &&
               required[2] == "VK_KHR_portability_subset");
    ensure("selection uses the exact owned surface",
           state.mLastSurfaceSupportDevice == state.mPhysicalDevice && state.mLastSurfaceSupportQueue == 0 &&
               state.mLastSurfaceSupportSurface == state.mSurface);

    const std::size_t count_calls = state.mPhysicalCountCalls;
    ensurePresentationDeviceCode(owner.acquirePresentationDeviceGeneration(request),
                                 VulkanPresentationDeviceAcquireCode::PresentationDeviceAlreadyOwned);
    ensure("duplicate acquisition preserves the first selection without re-entering the resolver",
           owner.physicalDevice() == state.mPhysicalDevice && state.mPhysicalCountCalls == count_calls);

    owner.resetPresentationDeviceGeneration();
    ensure("explicit selection reset leaves the instance and surface live",
           !owner.hasPresentationDeviceGeneration() && owner.physicalDevice() == VK_NULL_HANDLE &&
               owner.requiredDeviceExtensions().empty() && owner.instance() == state.mInstance && owner.surface() == state.mSurface);
    ensure("selection can be reacquired for the same live parent surface", !owner.acquirePresentationDeviceGeneration(request));
}

template<>
template<>
void render_vulkan_instance_test_object::test<25>()
{
    for (bool fail_instance_owner : { true, false })
    {
        FakeState                state;
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        ensure("surface acquisition succeeds before the stale-selection boundary",
               !owner.acquireSurfaceGeneration(makeSurfaceRequest(state, owner)));

        state.mInstanceOwnerChecks    = 0;
        state.mSurfaceWindowChecks    = 0;
        state.mFailInstanceOwnerCheck = fail_instance_owner ? 2 : 0;
        state.mFailSurfaceWindowCheck = fail_instance_owner ? 0 : 2;
        const VulkanPresentationDeviceAcquireResult result =
            owner.acquirePresentationDeviceGeneration(makePresentationDeviceRequest(state, owner));
        ensurePresentationDeviceCode(result, fail_instance_owner ? VulkanPresentationDeviceAcquireCode::StaleInstanceOwner
                                                                 : VulkanPresentationDeviceAcquireCode::StaleWindowGeneration);
        ensure("freshness is reauthenticated after resolution and before publication",
               state.mPhysicalCountCalls == 1 && state.mPhysicalListCalls == 1 && state.mInstanceOwnerChecks == 2 &&
                   state.mSurfaceWindowChecks == (fail_instance_owner ? 1 : 2));
        ensure("a stale final boundary publishes no selection and preserves its parent surface",
               !owner.hasPresentationDeviceGeneration() && owner.hasSurfaceGeneration() && owner.surface() == state.mSurface);
    }
}

template<>
template<>
void render_vulkan_instance_test_object::test<26>()
{
    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration first = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    ensure("surface acquisition succeeds before parent move", !first.acquireSurfaceGeneration(makeSurfaceRequest(state, first)));
    ensure("selection succeeds before parent move",
           !first.acquirePresentationDeviceGeneration(makePresentationDeviceRequest(state, first)));

    VulkanInstanceGeneration moved(std::move(first));
    ensure("the parent move transfers the surface-bound selection",
           !first.hasPresentationDeviceGeneration() && first.physicalDevice() == VK_NULL_HANDLE &&
               moved.hasPresentationDeviceGeneration() && moved.physicalDevice() == state.mPhysicalDevice &&
               moved.presentationQueueFamilyIndex() == 0 && moved.surface() == state.mSurface);

    state.mSurfaceDestroyOwner = &moved;
    moved.resetSurfaceGeneration();
    ensure("surface reset removes the presentation child before invoking surface destruction",
           state.mSurfaceDestroyObservationMade && !state.mObservedPresentationAtSurfaceDestroy && state.mDestroySurfaceCalls == 1 &&
               !moved.hasPresentationDeviceGeneration() && !moved.hasSurfaceGeneration() && moved.physicalDevice() == VK_NULL_HANDLE &&
               moved.surface() == VK_NULL_HANDLE && moved.instance() == state.mInstance);

    ensure("the moved parent remains reusable after its complete child chain is reset",
           !moved.acquireSurfaceGeneration(makeSurfaceRequest(state, moved)) &&
               !moved.acquirePresentationDeviceGeneration(makePresentationDeviceRequest(state, moved)));
}

template<>
template<>
void render_vulkan_instance_test_object::test<27>()
{
    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    ensure("surface acquisition succeeds before forced selection allocation failure",
           !owner.acquireSurfaceGeneration(makeSurfaceRequest(state, owner)));
    const VulkanPresentationDeviceRequest request = makePresentationDeviceRequest(state, owner);

    const VulkanPresentationDeviceAcquireResult result = VulkanInstanceDetail::acquirePresentationDevice(owner, request, failAllocation);
    ensurePresentationDeviceCode(result, VulkanPresentationDeviceAcquireCode::AllocationFailure);
    ensure("allocation failure occurs after resolution but rolls back without disturbing the parent surface",
           state.mPhysicalCountCalls == 1 && state.mPhysicalListCalls == 1 && !owner.hasPresentationDeviceGeneration() &&
               owner.hasSurfaceGeneration() && owner.surface() == state.mSurface && owner.instance() == state.mInstance);
    ensure("the live parent can retry after presentation-device allocation failure",
           !owner.acquirePresentationDeviceGeneration(request) && owner.hasPresentationDeviceGeneration() &&
               state.mPhysicalCountCalls == 2);
}

template<>
template<>
void render_vulkan_instance_test_object::test<28>()
{
    static_assert(std::is_same_v<VulkanLogicalDeviceAcquireResult, std::optional<VulkanLogicalDeviceAcquireError>>);
    static_assert(noexcept(
        std::declval<VulkanInstanceGeneration&>().acquireLogicalDeviceGeneration(std::declval<const VulkanLogicalDeviceRequest&>())));
    static_assert(noexcept(std::declval<VulkanInstanceGeneration&>().resetLogicalDeviceGeneration()));
    static_assert(noexcept(std::declval<const VulkanInstanceGeneration&>().hasLogicalDeviceGeneration()));

    const VulkanLogicalDeviceAcquireError left{ VulkanLogicalDeviceAcquireCode::ResolutionFailure,
                                                VulkanLogicalDeviceResolutionError{
                                                    VulkanLogicalDeviceResolutionCode::IndependentBlendUnsupported,
                                                    VulkanLogicalDeviceCommand::GetPhysicalDeviceFeatures } };
    ensure("identical logical-device errors compare equal", left == left);

    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    ensure("an owner without a logical device exposes neutral observations",
           !owner.hasLogicalDeviceGeneration() && owner.logicalDevice() == VK_NULL_HANDLE && owner.presentationQueue() == VK_NULL_HANDLE &&
               owner.logicalDevicePhysicalDevice() == VK_NULL_HANDLE && owner.logicalDeviceQueueFamilyIndex() == VK_QUEUE_FAMILY_IGNORED &&
               owner.logicalDeviceQueueIndex() == std::numeric_limits<std::uint32_t>::max() &&
               !owner.logicalDeviceEnabledFeatures().independentBlend && owner.enabledDeviceExtensions().empty() &&
               !owner.portabilitySubsetEnabled());

    VulkanLogicalDeviceRequest request = makeLogicalDeviceRequest(state, owner);
    request.mInstanceOwnerCheck        = {};
    ensureLogicalDeviceCode(owner.acquireLogicalDeviceGeneration(request), VulkanLogicalDeviceAcquireCode::InvalidInstanceOwnerCheck);

    request                        = makeLogicalDeviceRequest(state, owner);
    request.mWindowGenerationCheck = {};
    ensureLogicalDeviceCode(owner.acquireLogicalDeviceGeneration(request), VulkanLogicalDeviceAcquireCode::InvalidWindowGenerationCheck);

    request                         = makeLogicalDeviceRequest(state, owner);
    request.mNativeWindowGeneration = 0;
    ensureLogicalDeviceCode(owner.acquireLogicalDeviceGeneration(request), VulkanLogicalDeviceAcquireCode::InvalidNativeWindowGeneration);

    request = makeLogicalDeviceRequest(state, owner);
    ensureLogicalDeviceCode(owner.acquireLogicalDeviceGeneration(request), VulkanLogicalDeviceAcquireCode::SurfaceNotLive);

    ensure("logical preflight surface acquisition succeeds", !owner.acquireSurfaceGeneration(makeSurfaceRequest(state, owner)));
    ensureLogicalDeviceCode(owner.acquireLogicalDeviceGeneration(makeLogicalDeviceRequest(state, owner)),
                            VulkanLogicalDeviceAcquireCode::PresentationDeviceNotLive);
    ensure("logical preflight selection succeeds", !owner.acquirePresentationDeviceGeneration(makePresentationDeviceRequest(state, owner)));

    request                         = makeLogicalDeviceRequest(state, owner);
    request.mNativeWindowGeneration = 41;
    ensureLogicalDeviceCode(owner.acquireLogicalDeviceGeneration(request), VulkanLogicalDeviceAcquireCode::NativeWindowGenerationMismatch);

    request                     = makeLogicalDeviceRequest(state, owner);
    state.mInstanceOwnerCurrent = false;
    ensureLogicalDeviceCode(owner.acquireLogicalDeviceGeneration(request), VulkanLogicalDeviceAcquireCode::StaleInstanceOwner);
    state.mInstanceOwnerCurrent = true;
    state.mSurfaceWindowCurrent = false;
    ensureLogicalDeviceCode(owner.acquireLogicalDeviceGeneration(request), VulkanLogicalDeviceAcquireCode::StaleWindowGeneration);
    state.mSurfaceWindowCurrent = true;

    owner.reset();
    request = makeLogicalDeviceRequest(state, owner);
    ensureLogicalDeviceCode(owner.acquireLogicalDeviceGeneration(request), VulkanLogicalDeviceAcquireCode::InstanceNotLive);
    ensure("preflight and stale provenance failures create no logical device",
           state.mPhysicalFeaturesCalls == 0 && state.mCreateDeviceCalls == 0 && state.mDestroyDeviceCalls == 0);
}

template<>
template<>
void render_vulkan_instance_test_object::test<29>()
{
    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireSelectionChain(state, owner);

    state.mSupportedFeatures.independentBlend                = VK_FALSE;
    const VulkanLogicalDeviceAcquireResult unsupported       = owner.acquireLogicalDeviceGeneration(makeLogicalDeviceRequest(state, owner));
    const VulkanLogicalDeviceAcquireError& unsupported_error = requireLogicalDeviceError(unsupported);
    ensure("the parent preserves the exact unsupported-feature error",
           unsupported_error.mCode == VulkanLogicalDeviceAcquireCode::ResolutionFailure && unsupported_error.mResolutionError &&
               unsupported_error.mResolutionError->mCode == VulkanLogicalDeviceResolutionCode::IndependentBlendUnsupported &&
               unsupported_error.mResolutionError->mCommand == VulkanLogicalDeviceCommand::GetPhysicalDeviceFeatures);
    ensure("the feature query uses the selected device and publishes nothing on failure",
           state.mPhysicalFeaturesCalls == 1 && state.mPhysicalFeaturesDevice == state.mPhysicalDevice && state.mCreateDeviceCalls == 0 &&
               !owner.hasLogicalDeviceGeneration() && owner.hasPresentationDeviceGeneration());

    state.mSupportedFeatures.independentBlend = VK_TRUE;
    const VulkanLogicalDeviceRequest request  = makeLogicalDeviceRequest(state, owner);
    ensure("the exact selection creates one logical-device generation", !owner.acquireLogicalDeviceGeneration(request));
    const std::span<const std::string_view> extensions = owner.enabledDeviceExtensions();
    ensure("the parent exposes the authenticated logical device, queue, feature, and extension policy",
           owner.hasLogicalDeviceGeneration() && owner.logicalDevice() == state.mDevice && owner.presentationQueue() == state.mQueue &&
               owner.logicalDevicePhysicalDevice() == owner.physicalDevice() &&
               owner.logicalDevicePhysicalDevice() == state.mPhysicalDevice &&
               owner.logicalDeviceQueueFamilyIndex() == owner.presentationQueueFamilyIndex() &&
               owner.logicalDeviceQueueFamilyIndex() == 0 && owner.logicalDeviceQueueIndex() == 0 &&
               owner.logicalDeviceEnabledFeatures().independentBlend == VK_TRUE && extensions.size() == 2 &&
               extensions[0] == VK_KHR_SWAPCHAIN_EXTENSION_NAME && extensions[1] == VK_KHR_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME &&
               owner.swapchainMaintenance1Enabled() && !owner.portabilitySubsetEnabled());
    ensure("queue retrieval uses the exact created device, selected family, and queue zero",
           state.mGetDeviceQueueCalls == 1 && state.mGetDeviceQueueDevice == state.mDevice && state.mGetDeviceQueueFamily == 0 &&
               state.mGetDeviceQueueIndex == 0);

    const std::size_t feature_calls = state.mPhysicalFeaturesCalls;
    ensureLogicalDeviceCode(owner.acquireLogicalDeviceGeneration(request), VulkanLogicalDeviceAcquireCode::LogicalDeviceAlreadyOwned);
    ensure("duplicate acquisition keeps the first device without re-entering creation",
           owner.logicalDevice() == state.mDevice && state.mPhysicalFeaturesCalls == feature_calls && state.mCreateDeviceCalls == 1);

    owner.resetLogicalDeviceGeneration();
    ensure("logical-only reset destroys the device and leaves the complete parent selection reusable",
           state.mDestroyDeviceCalls == 1 && !owner.hasLogicalDeviceGeneration() && owner.logicalDevice() == VK_NULL_HANDLE &&
               owner.presentationQueue() == VK_NULL_HANDLE && owner.hasPresentationDeviceGeneration() && owner.hasSurfaceGeneration() &&
               owner.instance() == state.mInstance);
    owner.resetLogicalDeviceGeneration();
    ensure_equals("logical-only reset is idempotent", state.mDestroyDeviceCalls, std::size_t{ 1 });
    ensure("logical-device acquisition can retry after explicit reset", !owner.acquireLogicalDeviceGeneration(request));
}

template<>
template<>
void render_vulkan_instance_test_object::test<30>()
{
    for (bool fail_instance_owner : { true, false })
    {
        FakeState                state;
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        acquireSelectionChain(state, owner);

        state.mInstanceOwnerChecks                    = 0;
        state.mSurfaceWindowChecks                    = 0;
        state.mFailInstanceOwnerCheck                 = fail_instance_owner ? 2 : 0;
        state.mFailSurfaceWindowCheck                 = fail_instance_owner ? 0 : 2;
        const VulkanLogicalDeviceAcquireResult result = owner.acquireLogicalDeviceGeneration(makeLogicalDeviceRequest(state, owner));
        ensureLogicalDeviceCode(result, fail_instance_owner ? VulkanLogicalDeviceAcquireCode::StaleInstanceOwner
                                                            : VulkanLogicalDeviceAcquireCode::StaleWindowGeneration);
        ensure("post-create freshness failure destroys the pending device before publication",
               state.mCreateDeviceCalls == 1 && state.mGetDeviceQueueCalls == 1 && state.mDestroyDeviceCalls == 1 &&
                   !owner.hasLogicalDeviceGeneration() && owner.hasPresentationDeviceGeneration() && owner.hasSurfaceGeneration());

        state.mFailInstanceOwnerCheck = 0;
        state.mFailSurfaceWindowCheck = 0;
        ensure("the current parent can retry after stale rollback",
               !owner.acquireLogicalDeviceGeneration(makeLogicalDeviceRequest(state, owner)) && owner.hasLogicalDeviceGeneration());
    }
}

template<>
template<>
void render_vulkan_instance_test_object::test<31>()
{
    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireSelectionChain(state, owner);
    const VulkanLogicalDeviceRequest request = makeLogicalDeviceRequest(state, owner);

    const VulkanLogicalDeviceAcquireResult result = VulkanInstanceDetail::acquireLogicalDevice(owner, request, failAllocation);
    ensureLogicalDeviceCode(result, VulkanLogicalDeviceAcquireCode::AllocationFailure);
    ensure("post-create parent allocation failure destroys the result-owned device and preserves the selection",
           state.mCreateDeviceCalls == 1 && state.mGetDeviceQueueCalls == 1 && state.mDestroyDeviceCalls == 1 &&
               !owner.hasLogicalDeviceGeneration() && owner.hasPresentationDeviceGeneration() && owner.hasSurfaceGeneration());
    ensure("the live parent retries after logical-device allocation failure",
           !owner.acquireLogicalDeviceGeneration(request) && owner.hasLogicalDeviceGeneration() && state.mCreateDeviceCalls == 2);
}

template<>
template<>
void render_vulkan_instance_test_object::test<32>()
{
    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration first = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireSelectionChain(state, first);
    ensure("logical-device acquisition succeeds before parent move",
           !first.acquireLogicalDeviceGeneration(makeLogicalDeviceRequest(state, first)));

    VulkanInstanceGeneration moved(std::move(first));
    ensure("the parent move transfers the complete logical-device child chain",
           !first.hasLogicalDeviceGeneration() && first.logicalDevice() == VK_NULL_HANDLE && !first.hasPresentationDeviceGeneration() &&
               !first.hasSurfaceGeneration() && moved.hasLogicalDeviceGeneration() && moved.logicalDevice() == state.mDevice &&
               moved.presentationQueue() == state.mQueue && moved.logicalDevicePhysicalDevice() == moved.physicalDevice() &&
               moved.logicalDeviceQueueFamilyIndex() == 0 && moved.hasPresentationDeviceGeneration() && moved.hasSurfaceGeneration());
    first.reset();
    ensure_equals("resetting the moved-from parent destroys no logical device", state.mDestroyDeviceCalls, std::size_t{ 0 });

    state.mDeviceDestroyOwner  = &moved;
    state.mSurfaceDestroyOwner = &moved;
    moved.reset();
    ensure("device destruction observes its selection and surface parents still live",
           state.mDeviceDestroyObservationMade && state.mObservedPresentationAtDeviceDestroy && state.mObservedSurfaceAtDeviceDestroy);
    ensure("surface destruction observes both younger children already removed",
           state.mSurfaceDestroyObservationMade && !state.mObservedLogicalAtSurfaceDestroy && !state.mObservedPresentationAtSurfaceDestroy);
    ensure("the exact owning teardown order is logical device, surface, then instance",
           state.mEvents == std::vector<Event>{ Event::CreateInstance, Event::CreateSurface, Event::CreateDevice, Event::GetDeviceQueue,
                                                Event::DestroyDevice, Event::DestroySurface, Event::DestroyInstance } &&
               state.mDestroyDeviceCalls == 1 && state.mDestroySurfaceCalls == 1 && state.mDestroyInstanceCalls == 1);
}

template<>
template<>
void render_vulkan_instance_test_object::test<33>()
{
    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireSelectionChain(state, owner);

    state.mMissing                                = MissingCommand::DestroyDevice;
    const VulkanLogicalDeviceAcquireResult result = owner.acquireLogicalDeviceGeneration(makeLogicalDeviceRequest(state, owner));
    const VulkanLogicalDeviceAcquireError& error  = requireLogicalDeviceError(result);
    ensure("the parent preserves exact missing logical-device command identity",
           error.mCode == VulkanLogicalDeviceAcquireCode::ResolutionFailure && error.mResolutionError &&
               error.mResolutionError->mCode == VulkanLogicalDeviceResolutionCode::MissingRequiredCommand &&
               error.mResolutionError->mCommand == VulkanLogicalDeviceCommand::DestroyDevice);
    ensure("missing rollback capability stops before feature query and device creation",
           state.mPhysicalFeaturesCalls == 0 && state.mCreateDeviceCalls == 0 && state.mDestroyDeviceCalls == 0 &&
               !owner.hasLogicalDeviceGeneration());

    state.mMissing = MissingCommand::None;
    ensure("the selection remains retryable after a nested dispatch failure",
           !owner.acquireLogicalDeviceGeneration(makeLogicalDeviceRequest(state, owner)) && owner.hasLogicalDeviceGeneration());
}

template<>
template<>
void render_vulkan_instance_test_object::test<34>()
{
    static_assert(std::is_same_v<VulkanSwapchainConfigurationAcquireResult, std::optional<VulkanSwapchainConfigurationAcquireError>>);
    static_assert(noexcept(std::declval<VulkanInstanceGeneration&>().acquireSwapchainConfigurationGeneration(
        std::declval<const VulkanSwapchainConfigurationRequest&>())));
    static_assert(noexcept(std::declval<VulkanInstanceGeneration&>().resetSwapchainConfigurationGeneration()));

    const VulkanSwapchainConfigurationAcquireError value{ VulkanSwapchainConfigurationAcquireCode::ResolutionFailure,
                                                          VulkanSwapchainConfigurationResolutionError{
                                                              VulkanSwapchainConfigurationResolutionCode::NoCompatibleSurfaceFormat,
                                                              VulkanSwapchainConfigurationCommand::GetPhysicalDeviceSurfaceFormats } };
    ensure("identical swapchain-configuration errors compare equal", value == value);

    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    ensure("an owner without a configuration exposes neutral observations",
           !owner.hasSwapchainConfigurationGeneration() && owner.swapchainDrawableExtent().width == 0 &&
               owner.swapchainDrawableExtent().height == 0 && owner.swapchainImageCount() == 0 && owner.swapchainImageExtent().width == 0 &&
               owner.swapchainImageExtent().height == 0 && owner.swapchainImageArrayLayers() == 0 && owner.swapchainImageUsage() == 0 &&
               owner.swapchainPresentMode() == VK_PRESENT_MODE_MAX_ENUM_KHR &&
               owner.swapchainImageSharingMode() == VK_SHARING_MODE_MAX_ENUM && owner.swapchainPreTransform() == 0 &&
               owner.swapchainCompositeAlpha() == 0 && owner.swapchainClipped() == VK_FALSE);

    VulkanSwapchainConfigurationRequest request = makeSwapchainConfigurationRequest(state, owner);
    request.mInstanceOwnerCheck                 = {};
    ensureSwapchainConfigurationCode(owner.acquireSwapchainConfigurationGeneration(request),
                                     VulkanSwapchainConfigurationAcquireCode::InvalidInstanceOwnerCheck);
    request                        = makeSwapchainConfigurationRequest(state, owner);
    request.mWindowGenerationCheck = {};
    ensureSwapchainConfigurationCode(owner.acquireSwapchainConfigurationGeneration(request),
                                     VulkanSwapchainConfigurationAcquireCode::InvalidWindowGenerationCheck);
    request                         = makeSwapchainConfigurationRequest(state, owner);
    request.mNativeWindowGeneration = 0;
    ensureSwapchainConfigurationCode(owner.acquireSwapchainConfigurationGeneration(request),
                                     VulkanSwapchainConfigurationAcquireCode::InvalidNativeWindowGeneration);
    request                 = makeSwapchainConfigurationRequest(state, owner);
    request.mDrawableExtent = { 0, 600 };
    ensureSwapchainConfigurationCode(owner.acquireSwapchainConfigurationGeneration(request),
                                     VulkanSwapchainConfigurationAcquireCode::InvalidDrawableExtent);

    ensureSwapchainConfigurationCode(owner.acquireSwapchainConfigurationGeneration(makeSwapchainConfigurationRequest(state, owner)),
                                     VulkanSwapchainConfigurationAcquireCode::SurfaceNotLive);
    ensure("surface acquisition succeeds for configuration preflight", !owner.acquireSurfaceGeneration(makeSurfaceRequest(state, owner)));
    ensureSwapchainConfigurationCode(owner.acquireSwapchainConfigurationGeneration(makeSwapchainConfigurationRequest(state, owner)),
                                     VulkanSwapchainConfigurationAcquireCode::PresentationDeviceNotLive);
    ensure("selection succeeds for configuration preflight",
           !owner.acquirePresentationDeviceGeneration(makePresentationDeviceRequest(state, owner)));
    ensureSwapchainConfigurationCode(owner.acquireSwapchainConfigurationGeneration(makeSwapchainConfigurationRequest(state, owner)),
                                     VulkanSwapchainConfigurationAcquireCode::LogicalDeviceNotLive);
    ensure("logical device succeeds for configuration preflight",
           !owner.acquireLogicalDeviceGeneration(makeLogicalDeviceRequest(state, owner)));

    request                         = makeSwapchainConfigurationRequest(state, owner);
    request.mNativeWindowGeneration = 41;
    ensureSwapchainConfigurationCode(owner.acquireSwapchainConfigurationGeneration(request),
                                     VulkanSwapchainConfigurationAcquireCode::NativeWindowGenerationMismatch);
    request                     = makeSwapchainConfigurationRequest(state, owner);
    state.mInstanceOwnerCurrent = false;
    ensureSwapchainConfigurationCode(owner.acquireSwapchainConfigurationGeneration(request),
                                     VulkanSwapchainConfigurationAcquireCode::StaleInstanceOwner);
    state.mInstanceOwnerCurrent = true;
    state.mSurfaceWindowCurrent = false;
    ensureSwapchainConfigurationCode(owner.acquireSwapchainConfigurationGeneration(request),
                                     VulkanSwapchainConfigurationAcquireCode::StaleWindowGeneration);
    state.mSurfaceWindowCurrent = true;

    owner.reset();
    ensureSwapchainConfigurationCode(owner.acquireSwapchainConfigurationGeneration(makeSwapchainConfigurationRequest(state, owner)),
                                     VulkanSwapchainConfigurationAcquireCode::InstanceNotLive);
    ensure_equals("preflight failures run no surface-capability query", state.mSwapchainCapabilitiesCalls, std::size_t{ 0 });
}

template<>
template<>
void render_vulkan_instance_test_object::test<35>()
{
    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireLogicalChain(state, owner);
    const VulkanSwapchainConfigurationRequest request = makeSwapchainConfigurationRequest(state, owner, { 1280, 720 });

    state.mMissing                                                 = MissingCommand::GetSurfaceFormats;
    const VulkanSwapchainConfigurationAcquireResult missing_result = owner.acquireSwapchainConfigurationGeneration(request);
    const VulkanSwapchainConfigurationAcquireError& missing_error  = requireSwapchainConfigurationError(missing_result);
    ensure("the parent preserves nested swapchain command identity",
           missing_error.mCode == VulkanSwapchainConfigurationAcquireCode::ResolutionFailure && missing_error.mResolutionError &&
               missing_error.mResolutionError->mCode == VulkanSwapchainConfigurationResolutionCode::MissingRequiredCommand &&
               missing_error.mResolutionError->mCommand == VulkanSwapchainConfigurationCommand::GetPhysicalDeviceSurfaceFormats);
    ensure("nested dispatch failure publishes no configuration", !owner.hasSwapchainConfigurationGeneration());

    state.mMissing = MissingCommand::None;
    ensure("the exact device chain acquires one swapchain configuration", !owner.acquireSwapchainConfigurationGeneration(request));
    ensure("the parent exposes the exact conservative configuration",
           owner.hasSwapchainConfigurationGeneration() && owner.swapchainDrawableExtent().width == 1280 &&
               owner.swapchainDrawableExtent().height == 720 && owner.swapchainSurfaceFormat().format == VK_FORMAT_B8G8R8A8_UNORM &&
               owner.swapchainSurfaceFormat().colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR &&
               owner.swapchainPresentMode() == VK_PRESENT_MODE_FIFO_KHR && owner.swapchainImageCount() == 3 &&
               owner.swapchainImageExtent().width == 1280 && owner.swapchainImageExtent().height == 720 &&
               owner.swapchainImageArrayLayers() == 1 &&
               owner.swapchainImageUsage() ==
                   (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT) &&
               owner.swapchainImageSharingMode() == VK_SHARING_MODE_EXCLUSIVE &&
               owner.swapchainPreTransform() == VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR &&
               owner.swapchainCompositeAlpha() == VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR && owner.swapchainClipped() == VK_FALSE);
    const std::size_t capability_calls = state.mSwapchainCapabilitiesCalls;
    ensureSwapchainConfigurationCode(owner.acquireSwapchainConfigurationGeneration(request),
                                     VulkanSwapchainConfigurationAcquireCode::SwapchainConfigurationAlreadyOwned);
    ensure_equals("duplicate acquisition does not requery the surface", state.mSwapchainCapabilitiesCalls, capability_calls);

    owner.resetSwapchainConfigurationGeneration();
    ensure("configuration-only reset leaves every Vulkan object parent live",
           !owner.hasSwapchainConfigurationGeneration() && owner.hasLogicalDeviceGeneration() && owner.logicalDevice() == state.mDevice &&
               owner.hasPresentationDeviceGeneration() && owner.hasSurfaceGeneration());
    owner.resetSwapchainConfigurationGeneration();
    ensure("the same parent chain can reacquire after explicit configuration reset",
           !owner.acquireSwapchainConfigurationGeneration(request) && owner.hasSwapchainConfigurationGeneration());
}

template<>
template<>
void render_vulkan_instance_test_object::test<36>()
{
    for (bool fail_instance_owner : { true, false })
    {
        FakeState                state;
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        acquireLogicalChain(state, owner);

        state.mInstanceOwnerChecks    = 0;
        state.mSurfaceWindowChecks    = 0;
        state.mFailInstanceOwnerCheck = fail_instance_owner ? 2 : 0;
        state.mFailSurfaceWindowCheck = fail_instance_owner ? 0 : 2;
        const VulkanSwapchainConfigurationAcquireResult result =
            owner.acquireSwapchainConfigurationGeneration(makeSwapchainConfigurationRequest(state, owner));
        ensureSwapchainConfigurationCode(result, fail_instance_owner ? VulkanSwapchainConfigurationAcquireCode::StaleInstanceOwner
                                                                     : VulkanSwapchainConfigurationAcquireCode::StaleWindowGeneration);
        ensure("configuration freshness is reauthenticated after all queries",
               state.mSwapchainCapabilitiesCalls == 1 && state.mSwapchainFormatCountCalls == 1 && state.mSwapchainFormatListCalls == 1 &&
                   state.mSwapchainPresentModeCountCalls == 1 && state.mSwapchainPresentModeListCalls == 1 &&
                   state.mInstanceOwnerChecks == 2 && state.mSurfaceWindowChecks == (fail_instance_owner ? 1 : 2));
        ensure("stale publication leaves the complete Vulkan object chain reusable",
               !owner.hasSwapchainConfigurationGeneration() && owner.hasLogicalDeviceGeneration() && owner.hasSurfaceGeneration());

        state.mFailInstanceOwnerCheck = 0;
        state.mFailSurfaceWindowCheck = 0;
        ensure("current parents retry after stale configuration publication",
               !owner.acquireSwapchainConfigurationGeneration(makeSwapchainConfigurationRequest(state, owner)) &&
                   owner.hasSwapchainConfigurationGeneration());
    }
}

template<>
template<>
void render_vulkan_instance_test_object::test<37>()
{
    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireLogicalChain(state, owner);
    const VulkanSwapchainConfigurationRequest request = makeSwapchainConfigurationRequest(state, owner);

    const VulkanSwapchainConfigurationAcquireResult result =
        VulkanInstanceDetail::acquireSwapchainConfiguration(owner, request, failAllocation);
    ensureSwapchainConfigurationCode(result, VulkanSwapchainConfigurationAcquireCode::AllocationFailure);
    ensure("parent allocation failure occurs after queries and preserves all Vulkan objects",
           state.mSwapchainCapabilitiesCalls == 1 && !owner.hasSwapchainConfigurationGeneration() && owner.hasLogicalDeviceGeneration() &&
               owner.logicalDevice() == state.mDevice && state.mDestroyDeviceCalls == 0);
    ensure("the live parent retries after configuration allocation failure",
           !owner.acquireSwapchainConfigurationGeneration(request) && owner.hasSwapchainConfigurationGeneration() &&
               state.mSwapchainCapabilitiesCalls == 2);
}

template<>
template<>
void render_vulkan_instance_test_object::test<38>()
{
    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration first = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireLogicalChain(state, first);
    ensure("configuration acquisition succeeds before parent move",
           !first.acquireSwapchainConfigurationGeneration(makeSwapchainConfigurationRequest(state, first)));

    VulkanInstanceGeneration moved(std::move(first));
    ensure("the parent move transfers the complete configuration chain",
           !first.hasSwapchainConfigurationGeneration() && !first.hasLogicalDeviceGeneration() &&
               moved.hasSwapchainConfigurationGeneration() && moved.hasLogicalDeviceGeneration() &&
               moved.swapchainDrawableExtent().width == 800 && moved.swapchainImageExtent().height == 600);
    first.reset();
    ensure_equals("resetting the moved-from parent destroys no device", state.mDestroyDeviceCalls, std::size_t{ 0 });

    state.mDeviceDestroyOwner  = &moved;
    state.mSurfaceDestroyOwner = &moved;
    moved.reset();
    ensure("device destruction observes configuration removed while older parents remain live",
           state.mDeviceDestroyObservationMade && !state.mObservedConfigurationAtDeviceDestroy &&
               state.mObservedPresentationAtDeviceDestroy && state.mObservedSurfaceAtDeviceDestroy);
    ensure("surface destruction observes configuration, logical device, and selection already removed",
           state.mSurfaceDestroyObservationMade && !state.mObservedConfigurationAtSurfaceDestroy &&
               !state.mObservedLogicalAtSurfaceDestroy && !state.mObservedPresentationAtSurfaceDestroy);
    ensure("full reset preserves device-before-surface-before-instance teardown",
           state.mEvents == std::vector<Event>{ Event::CreateInstance, Event::CreateSurface, Event::CreateDevice, Event::GetDeviceQueue,
                                                Event::DestroyDevice, Event::DestroySurface, Event::DestroyInstance });
}

template<>
template<>
void render_vulkan_instance_test_object::test<39>()
{
    static_assert(std::is_same_v<VulkanSwapchainAcquireResult, std::optional<VulkanSwapchainAcquireError>>);
    static_assert(
        noexcept(std::declval<VulkanInstanceGeneration&>().acquireSwapchainGeneration(std::declval<const VulkanSwapchainRequest&>())));
    static_assert(noexcept(std::declval<VulkanInstanceGeneration&>().resetSwapchainGeneration()));

    const VulkanSwapchainAcquireError value{ VulkanSwapchainAcquireCode::ResolutionFailure,
                                             VulkanSwapchainResolutionError{ VulkanSwapchainResolutionCode::MissingRequiredCommand,
                                                                             VulkanSwapchainCommand::DestroySwapchain } };
    ensure("identical swapchain errors compare equal", value == value);

    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    ensure("an owner without a swapchain exposes neutral observations",
           !owner.hasSwapchainGeneration() && owner.swapchain() == VK_NULL_HANDLE && owner.swapchainDevice() == VK_NULL_HANDLE &&
               owner.swapchainSurface() == VK_NULL_HANDLE);

    VulkanSwapchainRequest request = makeSwapchainRequest(state, owner);
    request.mInstanceOwnerCheck    = {};
    ensureSwapchainCode(owner.acquireSwapchainGeneration(request), VulkanSwapchainAcquireCode::InvalidInstanceOwnerCheck);
    request                        = makeSwapchainRequest(state, owner);
    request.mWindowGenerationCheck = {};
    ensureSwapchainCode(owner.acquireSwapchainGeneration(request), VulkanSwapchainAcquireCode::InvalidWindowGenerationCheck);
    request                         = makeSwapchainRequest(state, owner);
    request.mNativeWindowGeneration = 0;
    ensureSwapchainCode(owner.acquireSwapchainGeneration(request), VulkanSwapchainAcquireCode::InvalidNativeWindowGeneration);
    request                 = makeSwapchainRequest(state, owner);
    request.mDrawableExtent = { 800, 0 };
    ensureSwapchainCode(owner.acquireSwapchainGeneration(request), VulkanSwapchainAcquireCode::InvalidDrawableExtent);

    ensureSwapchainCode(owner.acquireSwapchainGeneration(makeSwapchainRequest(state, owner)), VulkanSwapchainAcquireCode::SurfaceNotLive);
    ensure("surface acquisition succeeds for swapchain preflight", !owner.acquireSurfaceGeneration(makeSurfaceRequest(state, owner)));
    ensureSwapchainCode(owner.acquireSwapchainGeneration(makeSwapchainRequest(state, owner)),
                        VulkanSwapchainAcquireCode::PresentationDeviceNotLive);
    ensure("selection succeeds for swapchain preflight",
           !owner.acquirePresentationDeviceGeneration(makePresentationDeviceRequest(state, owner)));
    ensureSwapchainCode(owner.acquireSwapchainGeneration(makeSwapchainRequest(state, owner)),
                        VulkanSwapchainAcquireCode::LogicalDeviceNotLive);
    ensure("logical device succeeds for swapchain preflight",
           !owner.acquireLogicalDeviceGeneration(makeLogicalDeviceRequest(state, owner)));
    ensureSwapchainCode(owner.acquireSwapchainGeneration(makeSwapchainRequest(state, owner)),
                        VulkanSwapchainAcquireCode::SwapchainConfigurationNotLive);
    ensure("configuration succeeds for swapchain preflight",
           !owner.acquireSwapchainConfigurationGeneration(makeSwapchainConfigurationRequest(state, owner)));

    request                         = makeSwapchainRequest(state, owner);
    request.mNativeWindowGeneration = 41;
    ensureSwapchainCode(owner.acquireSwapchainGeneration(request), VulkanSwapchainAcquireCode::NativeWindowGenerationMismatch);
    request                 = makeSwapchainRequest(state, owner);
    request.mDrawableExtent = { 801, 600 };
    ensureSwapchainCode(owner.acquireSwapchainGeneration(request), VulkanSwapchainAcquireCode::DrawableExtentMismatch);

    request                     = makeSwapchainRequest(state, owner);
    state.mInstanceOwnerCurrent = false;
    ensureSwapchainCode(owner.acquireSwapchainGeneration(request), VulkanSwapchainAcquireCode::StaleInstanceOwner);
    state.mInstanceOwnerCurrent = true;
    state.mSurfaceWindowCurrent = false;
    ensureSwapchainCode(owner.acquireSwapchainGeneration(request), VulkanSwapchainAcquireCode::StaleWindowGeneration);
    state.mSurfaceWindowCurrent = true;
    ensure("every stale or malformed preflight fails before device dispatch and creation",
           state.mGetDeviceProcAddrResolutionCalls == 0 && state.mCreateSwapchainCalls == 0 && state.mDestroySwapchainCalls == 0 &&
               !owner.hasSwapchainGeneration());

    owner.reset();
    ensureSwapchainCode(owner.acquireSwapchainGeneration(makeSwapchainRequest(state, owner)), VulkanSwapchainAcquireCode::InstanceNotLive);
}

template<>
template<>
void render_vulkan_instance_test_object::test<40>()
{
    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireConfigurationChain(state, owner, { 1280, 720 });
    const VulkanSwapchainRequest request = makeSwapchainRequest(state, owner, { 1280, 720 });

    constexpr std::array missing_commands{ std::pair{ MissingCommand::GetDeviceProcAddr, VulkanSwapchainCommand::GetDeviceProcAddr },
                                           std::pair{ MissingCommand::CreateSwapchain, VulkanSwapchainCommand::CreateSwapchain },
                                           std::pair{ MissingCommand::DestroySwapchain, VulkanSwapchainCommand::DestroySwapchain } };
    for (const auto& [missing, command] : missing_commands)
    {
        state.mMissing                            = missing;
        const VulkanSwapchainAcquireResult result = owner.acquireSwapchainGeneration(request);
        const VulkanSwapchainAcquireError& error  = requireSwapchainError(result);
        ensure("the parent preserves exact nested swapchain dispatch identity",
               error.mCode == VulkanSwapchainAcquireCode::ResolutionFailure && error.mResolutionError &&
                   error.mResolutionError->mCode == VulkanSwapchainResolutionCode::MissingRequiredCommand &&
                   error.mResolutionError->mCommand == command);
        ensure("a missing swapchain command publishes and creates nothing",
               !owner.hasSwapchainGeneration() && state.mCreateSwapchainCalls == 0 && state.mDestroySwapchainCalls == 0);
    }

    state.mMissing                                   = MissingCommand::None;
    state.mSwapchainCreateResult                     = VK_ERROR_OUT_OF_DEVICE_MEMORY;
    state.mPoisonSwapchainOutput                     = true;
    const VulkanSwapchainAcquireResult failed_create = owner.acquireSwapchainGeneration(request);
    const VulkanSwapchainAcquireError& create_error  = requireSwapchainError(failed_create);
    ensure("the parent preserves exact swapchain creation failure and VkResult",
           create_error.mCode == VulkanSwapchainAcquireCode::ResolutionFailure && create_error.mResolutionError &&
               create_error.mResolutionError->mCode == VulkanSwapchainResolutionCode::SwapchainCreationFailure &&
               create_error.mResolutionError->mCommand == VulkanSwapchainCommand::CreateSwapchain &&
               create_error.mResolutionError->mResult == VK_ERROR_OUT_OF_DEVICE_MEMORY);
    ensure("an undefined failed output is neither published nor destroyed",
           !owner.hasSwapchainGeneration() && state.mCreateSwapchainCalls == 1 && state.mDestroySwapchainCalls == 0);

    state.mSwapchainCreateResult                   = VK_SUCCESS;
    state.mPoisonSwapchainOutput                   = false;
    state.mNullSwapchain                           = true;
    const VulkanSwapchainAcquireResult null_create = owner.acquireSwapchainGeneration(request);
    const VulkanSwapchainAcquireError& null_error  = requireSwapchainError(null_create);
    ensure("a null successful output is propagated exactly without a destruction obligation",
           null_error.mCode == VulkanSwapchainAcquireCode::ResolutionFailure && null_error.mResolutionError &&
               null_error.mResolutionError->mCode == VulkanSwapchainResolutionCode::NullSwapchainOnSuccess &&
               null_error.mResolutionError->mCommand == VulkanSwapchainCommand::CreateSwapchain && state.mDestroySwapchainCalls == 0 &&
               !owner.hasSwapchainGeneration());

    state.mNullSwapchain                       = false;
    state.mGetDeviceProcAddrResolutionCalls    = 0;
    state.mGetDeviceProcAddrResolutionInstance = VK_NULL_HANDLE;
    state.mDeviceProcAddrCalls                 = 0;
    state.mDeviceProcAddrDevice                = VK_NULL_HANDLE;
    state.mDeviceCommandLookups.clear();
    state.mCreateSwapchainCalls = 0;
    ensure("the exact parent chain publishes one swapchain", !owner.acquireSwapchainGeneration(request));
    ensure("the parent exposes the exact swapchain provenance",
           owner.hasSwapchainGeneration() && owner.swapchain() == state.mSwapchain && owner.swapchainDevice() == state.mDevice &&
               owner.swapchainSurface() == state.mSurface);
    ensure("swapchain dispatch resolves through the exact instance and logical device",
           state.mGetDeviceProcAddrResolutionCalls == 1 && state.mGetDeviceProcAddrResolutionInstance == state.mInstance &&
               state.mDeviceProcAddrCalls == 2 && state.mDeviceProcAddrDevice == state.mDevice &&
               state.mDeviceCommandLookups == std::vector<std::string>{ "vkCreateSwapchainKHR", "vkDestroySwapchainKHR" });

    const VkSwapchainCreateInfoKHR& create_info = state.mSwapchainCreateInfo;
    ensure("the published swapchain consumed the exact Stage 40 create contract",
           state.mCreateSwapchainCalls == 1 && state.mCreateSwapchainDevice == state.mDevice &&
               state.mCreateSwapchainAllocationCallbacks == nullptr && create_info.sType == VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR &&
               create_info.pNext == nullptr && create_info.flags == 0 && create_info.surface == state.mSurface &&
               create_info.minImageCount == owner.swapchainImageCount() &&
               create_info.imageFormat == owner.swapchainSurfaceFormat().format &&
               create_info.imageColorSpace == owner.swapchainSurfaceFormat().colorSpace &&
               create_info.imageExtent.width == owner.swapchainImageExtent().width &&
               create_info.imageExtent.height == owner.swapchainImageExtent().height &&
               create_info.imageArrayLayers == owner.swapchainImageArrayLayers() && create_info.imageUsage == owner.swapchainImageUsage() &&
               create_info.imageSharingMode == owner.swapchainImageSharingMode() && create_info.queueFamilyIndexCount == 0 &&
               create_info.pQueueFamilyIndices == nullptr && create_info.preTransform == owner.swapchainPreTransform() &&
               create_info.compositeAlpha == owner.swapchainCompositeAlpha() && create_info.presentMode == owner.swapchainPresentMode() &&
               create_info.clipped == owner.swapchainClipped() && create_info.oldSwapchain == VK_NULL_HANDLE);

    ensureSwapchainCode(owner.acquireSwapchainGeneration(request), VulkanSwapchainAcquireCode::SwapchainAlreadyOwned);
    ensure_equals("duplicate acquisition performs no second create", state.mCreateSwapchainCalls, std::size_t{ 1 });

    state.mSwapchainDestroyOwner = &owner;
    owner.resetSwapchainGeneration();
    ensure("explicit swapchain reset preserves its configuration and all older parents",
           !owner.hasSwapchainGeneration() && owner.swapchain() == VK_NULL_HANDLE && owner.hasSwapchainConfigurationGeneration() &&
               owner.hasLogicalDeviceGeneration() && owner.hasSurfaceGeneration() && state.mDestroySwapchainCalls == 1 &&
               state.mDestroySwapchainDevice == state.mDevice && state.mDestroyedSwapchain == state.mSwapchain &&
               state.mDestroySwapchainAllocationCallbacks == nullptr && state.mSwapchainDestroyObservationMade &&
               state.mObservedConfigurationAtSwapchainDestroy && state.mObservedLogicalAtSwapchainDestroy &&
               state.mObservedSurfaceAtSwapchainDestroy);
    owner.resetSwapchainGeneration();
    ensure_equals("a second explicit swapchain reset is idempotent", state.mDestroySwapchainCalls, std::size_t{ 1 });
    ensure("the same parent chain can reacquire after explicit swapchain reset", !owner.acquireSwapchainGeneration(request));
}

template<>
template<>
void render_vulkan_instance_test_object::test<41>()
{
    for (bool fail_instance_owner : { true, false })
    {
        FakeState                state;
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        acquireConfigurationChain(state, owner);

        state.mInstanceOwnerChecks                = 0;
        state.mSurfaceWindowChecks                = 0;
        state.mFailInstanceOwnerCheck             = fail_instance_owner ? 2 : 0;
        state.mFailSurfaceWindowCheck             = fail_instance_owner ? 0 : 2;
        const VulkanSwapchainAcquireResult result = owner.acquireSwapchainGeneration(makeSwapchainRequest(state, owner));
        ensureSwapchainCode(result, fail_instance_owner ? VulkanSwapchainAcquireCode::StaleInstanceOwner
                                                        : VulkanSwapchainAcquireCode::StaleWindowGeneration);
        ensure("swapchain freshness is reauthenticated after native creation",
               state.mCreateSwapchainCalls == 1 && state.mDestroySwapchainCalls == 1 && state.mInstanceOwnerChecks == 2 &&
                   state.mSurfaceWindowChecks == (fail_instance_owner ? 1 : 2));
        ensure("stale publication rolls back the pending swapchain and preserves every parent",
               !owner.hasSwapchainGeneration() && owner.hasSwapchainConfigurationGeneration() && owner.hasLogicalDeviceGeneration() &&
                   owner.hasSurfaceGeneration() && state.mDestroyDeviceCalls == 0 && state.mDestroySurfaceCalls == 0);

        state.mFailInstanceOwnerCheck = 0;
        state.mFailSurfaceWindowCheck = 0;
        ensure("current parents retry after stale swapchain publication",
               !owner.acquireSwapchainGeneration(makeSwapchainRequest(state, owner)) && owner.hasSwapchainGeneration() &&
                   state.mCreateSwapchainCalls == 2 && state.mDestroySwapchainCalls == 1);
        owner.resetSwapchainGeneration();
        ensure_equals("the retried owner destroys its swapchain exactly once", state.mDestroySwapchainCalls, std::size_t{ 2 });
    }

    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireConfigurationChain(state, owner);
    const VulkanSwapchainRequest request = makeSwapchainRequest(state, owner);

    const VulkanSwapchainAcquireResult result = VulkanInstanceDetail::acquireSwapchain(owner, request, failAllocation);
    ensureSwapchainCode(result, VulkanSwapchainAcquireCode::AllocationFailure);
    ensure("parent allocation failure rolls back the created swapchain and preserves every parent",
           state.mCreateSwapchainCalls == 1 && state.mDestroySwapchainCalls == 1 && !owner.hasSwapchainGeneration() &&
               owner.hasSwapchainConfigurationGeneration() && owner.hasLogicalDeviceGeneration() &&
               owner.logicalDevice() == state.mDevice && state.mDestroyDeviceCalls == 0);
    ensure("the live chain retries after swapchain allocation failure",
           !owner.acquireSwapchainGeneration(request) && owner.hasSwapchainGeneration() && state.mCreateSwapchainCalls == 2 &&
               state.mDestroySwapchainCalls == 1);
    owner.resetSwapchainGeneration();
    ensure_equals("the allocation retry owns one independent destruction", state.mDestroySwapchainCalls, std::size_t{ 2 });
}

template<>
template<>
void render_vulkan_instance_test_object::test<42>()
{
    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration first = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireConfigurationChain(state, first);
    ensure("swapchain acquisition succeeds before parent move", !first.acquireSwapchainGeneration(makeSwapchainRequest(state, first)));

    VulkanInstanceGeneration moved(std::move(first));
    ensure("the parent move transfers the complete swapchain chain",
           !first.hasSwapchainGeneration() && first.swapchain() == VK_NULL_HANDLE && !first.hasSwapchainConfigurationGeneration() &&
               !first.hasLogicalDeviceGeneration() && moved.hasSwapchainGeneration() && moved.swapchain() == state.mSwapchain &&
               moved.swapchainDevice() == state.mDevice && moved.swapchainSurface() == state.mSurface &&
               moved.hasSwapchainConfigurationGeneration() && moved.hasLogicalDeviceGeneration());
    first.reset();
    ensure("resetting the moved-from parent destroys no Vulkan object",
           state.mDestroySwapchainCalls == 0 && state.mDestroyDeviceCalls == 0 && state.mDestroySurfaceCalls == 0 &&
               state.mDestroyInstanceCalls == 0);

    state.mSwapchainDestroyOwner = &moved;
    state.mDeviceDestroyOwner    = &moved;
    state.mSurfaceDestroyOwner   = &moved;
    moved.reset();
    ensure("swapchain destruction observes its configuration, device, and surface parents still live",
           state.mSwapchainDestroyObservationMade && state.mObservedConfigurationAtSwapchainDestroy &&
               state.mObservedLogicalAtSwapchainDestroy && state.mObservedSurfaceAtSwapchainDestroy);
    ensure("device destruction observes swapchain and configuration already removed while older parents remain live",
           state.mDeviceDestroyObservationMade && !state.mObservedSwapchainAtDeviceDestroy &&
               !state.mObservedConfigurationAtDeviceDestroy && state.mObservedPresentationAtDeviceDestroy &&
               state.mObservedSurfaceAtDeviceDestroy);
    ensure("surface destruction observes every younger child already removed",
           state.mSurfaceDestroyObservationMade && !state.mObservedSwapchainAtSurfaceDestroy &&
               !state.mObservedConfigurationAtSurfaceDestroy && !state.mObservedLogicalAtSurfaceDestroy &&
               !state.mObservedPresentationAtSurfaceDestroy);
    ensure("full reset destroys swapchain before device, surface, and instance exactly once",
           state.mEvents == std::vector<Event>{ Event::CreateInstance, Event::CreateSurface, Event::CreateDevice, Event::GetDeviceQueue,
                                                Event::CreateSwapchain, Event::DestroySwapchain, Event::DestroyDevice,
                                                Event::DestroySurface, Event::DestroyInstance } &&
               state.mDestroySwapchainCalls == 1 && state.mDestroyDeviceCalls == 1 && state.mDestroySurfaceCalls == 1 &&
               state.mDestroyInstanceCalls == 1 && state.mDestroySwapchainDevice == state.mDevice &&
               state.mDestroyedSwapchain == state.mSwapchain && state.mDestroySwapchainAllocationCallbacks == nullptr);
}

template<>
template<>
void render_vulkan_instance_test_object::test<43>()
{
    static_assert(std::is_same_v<VulkanSwapchainImagesAcquireResult, std::optional<VulkanSwapchainImagesAcquireError>>);
    static_assert(noexcept(
        std::declval<VulkanInstanceGeneration&>().acquireSwapchainImagesGeneration(std::declval<const VulkanSwapchainImagesRequest&>())));
    static_assert(noexcept(std::declval<VulkanInstanceGeneration&>().resetSwapchainImagesGeneration()));

    const VulkanSwapchainImagesAcquireError value{ VulkanSwapchainImagesAcquireCode::ResolutionFailure,
                                                   VulkanSwapchainImagesResolutionError{
                                                       VulkanSwapchainImagesResolutionCode::MissingRequiredCommand,
                                                       VulkanSwapchainImagesCommand::DestroyImageView } };
    ensure("identical swapchain-images errors compare equal", value == value);

    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    ensure("an owner without swapchain images exposes neutral observations",
           !owner.hasSwapchainImagesGeneration() && owner.resolvedSwapchainImageCount() == 0 && owner.swapchainImage(0) == VK_NULL_HANDLE &&
               owner.swapchainImageView(0) == VK_NULL_HANDLE);

    VulkanSwapchainImagesRequest request = makeSwapchainImagesRequest(state, owner);
    request.mInstanceOwnerCheck          = {};
    ensureSwapchainImagesCode(owner.acquireSwapchainImagesGeneration(request), VulkanSwapchainImagesAcquireCode::InvalidInstanceOwnerCheck);
    request                        = makeSwapchainImagesRequest(state, owner);
    request.mWindowGenerationCheck = {};
    ensureSwapchainImagesCode(owner.acquireSwapchainImagesGeneration(request),
                              VulkanSwapchainImagesAcquireCode::InvalidWindowGenerationCheck);
    request                         = makeSwapchainImagesRequest(state, owner);
    request.mNativeWindowGeneration = 0;
    ensureSwapchainImagesCode(owner.acquireSwapchainImagesGeneration(request),
                              VulkanSwapchainImagesAcquireCode::InvalidNativeWindowGeneration);
    request                 = makeSwapchainImagesRequest(state, owner);
    request.mDrawableExtent = { 800, 0 };
    ensureSwapchainImagesCode(owner.acquireSwapchainImagesGeneration(request), VulkanSwapchainImagesAcquireCode::InvalidDrawableExtent);

    ensureSwapchainImagesCode(owner.acquireSwapchainImagesGeneration(makeSwapchainImagesRequest(state, owner)),
                              VulkanSwapchainImagesAcquireCode::SurfaceNotLive);
    ensure("surface acquisition succeeds for swapchain-images preflight",
           !owner.acquireSurfaceGeneration(makeSurfaceRequest(state, owner)));
    ensureSwapchainImagesCode(owner.acquireSwapchainImagesGeneration(makeSwapchainImagesRequest(state, owner)),
                              VulkanSwapchainImagesAcquireCode::PresentationDeviceNotLive);
    ensure("selection succeeds for swapchain-images preflight",
           !owner.acquirePresentationDeviceGeneration(makePresentationDeviceRequest(state, owner)));
    ensureSwapchainImagesCode(owner.acquireSwapchainImagesGeneration(makeSwapchainImagesRequest(state, owner)),
                              VulkanSwapchainImagesAcquireCode::LogicalDeviceNotLive);
    ensure("logical device succeeds for swapchain-images preflight",
           !owner.acquireLogicalDeviceGeneration(makeLogicalDeviceRequest(state, owner)));
    ensureSwapchainImagesCode(owner.acquireSwapchainImagesGeneration(makeSwapchainImagesRequest(state, owner)),
                              VulkanSwapchainImagesAcquireCode::SwapchainConfigurationNotLive);
    ensure("configuration succeeds for swapchain-images preflight",
           !owner.acquireSwapchainConfigurationGeneration(makeSwapchainConfigurationRequest(state, owner)));
    ensureSwapchainImagesCode(owner.acquireSwapchainImagesGeneration(makeSwapchainImagesRequest(state, owner)),
                              VulkanSwapchainImagesAcquireCode::SwapchainNotLive);
    ensure("swapchain succeeds for swapchain-images preflight", !owner.acquireSwapchainGeneration(makeSwapchainRequest(state, owner)));

    request                         = makeSwapchainImagesRequest(state, owner);
    request.mNativeWindowGeneration = 41;
    ensureSwapchainImagesCode(owner.acquireSwapchainImagesGeneration(request),
                              VulkanSwapchainImagesAcquireCode::NativeWindowGenerationMismatch);
    request                 = makeSwapchainImagesRequest(state, owner);
    request.mDrawableExtent = { 801, 600 };
    ensureSwapchainImagesCode(owner.acquireSwapchainImagesGeneration(request), VulkanSwapchainImagesAcquireCode::DrawableExtentMismatch);

    request                     = makeSwapchainImagesRequest(state, owner);
    state.mInstanceOwnerCurrent = false;
    ensureSwapchainImagesCode(owner.acquireSwapchainImagesGeneration(request), VulkanSwapchainImagesAcquireCode::StaleInstanceOwner);
    state.mInstanceOwnerCurrent = true;
    state.mSurfaceWindowCurrent = false;
    ensureSwapchainImagesCode(owner.acquireSwapchainImagesGeneration(request), VulkanSwapchainImagesAcquireCode::StaleWindowGeneration);
    state.mSurfaceWindowCurrent = true;
    ensure("every malformed or stale preflight fails before image dispatch",
           state.mSwapchainImageCountCalls == 0 && state.mSwapchainImageListCalls == 0 && state.mCreateImageViewCalls == 0 &&
               state.mDestroyImageViewCalls == 0 && !owner.hasSwapchainImagesGeneration());

    owner.reset();
    ensureSwapchainImagesCode(owner.acquireSwapchainImagesGeneration(makeSwapchainImagesRequest(state, owner)),
                              VulkanSwapchainImagesAcquireCode::InstanceNotLive);
}

template<>
template<>
void render_vulkan_instance_test_object::test<44>()
{
    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireSwapchainChain(state, owner, { 1280, 720 });
    const VulkanSwapchainImagesRequest request = makeSwapchainImagesRequest(state, owner, { 1280, 720 });

    constexpr std::array missing_commands{ std::pair{ MissingCommand::GetDeviceProcAddr, VulkanSwapchainImagesCommand::GetDeviceProcAddr },
                                           std::pair{ MissingCommand::GetSwapchainImages,
                                                      VulkanSwapchainImagesCommand::GetSwapchainImages },
                                           std::pair{ MissingCommand::CreateImageView, VulkanSwapchainImagesCommand::CreateImageView },
                                           std::pair{ MissingCommand::DestroyImageView, VulkanSwapchainImagesCommand::DestroyImageView } };
    for (const auto& [missing, command] : missing_commands)
    {
        state.mMissing                                  = missing;
        const VulkanSwapchainImagesAcquireResult result = owner.acquireSwapchainImagesGeneration(request);
        const VulkanSwapchainImagesAcquireError& error  = requireSwapchainImagesError(result);
        ensure("the parent preserves exact nested image-view dispatch identity",
               error.mCode == VulkanSwapchainImagesAcquireCode::ResolutionFailure && error.mResolutionError &&
                   error.mResolutionError->mCode == VulkanSwapchainImagesResolutionCode::MissingRequiredCommand &&
                   error.mResolutionError->mCommand == command);
        ensure("a missing image command publishes and creates nothing",
               !owner.hasSwapchainImagesGeneration() && state.mSwapchainImageCountCalls == 0 && state.mCreateImageViewCalls == 0 &&
                   state.mDestroyImageViewCalls == 0);
    }

    state.mMissing                                              = MissingCommand::None;
    state.mSwapchainImageCountResult                            = VK_ERROR_OUT_OF_DEVICE_MEMORY;
    const VulkanSwapchainImagesAcquireResult failed_enumeration = owner.acquireSwapchainImagesGeneration(request);
    const VulkanSwapchainImagesAcquireError& enumeration_error  = requireSwapchainImagesError(failed_enumeration);
    ensure("the parent preserves an exact image-enumeration failure",
           enumeration_error.mCode == VulkanSwapchainImagesAcquireCode::ResolutionFailure && enumeration_error.mResolutionError &&
               enumeration_error.mResolutionError->mCode == VulkanSwapchainImagesResolutionCode::SwapchainImageEnumerationFailure &&
               enumeration_error.mResolutionError->mCommand == VulkanSwapchainImagesCommand::GetSwapchainImages &&
               enumeration_error.mResolutionError->mResult == VK_ERROR_OUT_OF_DEVICE_MEMORY && !owner.hasSwapchainImagesGeneration());

    state.mSwapchainImageCountResult        = VK_SUCCESS;
    state.mSwapchainImageCountCalls         = 0;
    state.mSwapchainImageListCalls          = 0;
    state.mCreateImageViewCalls             = 0;
    state.mGetDeviceProcAddrResolutionCalls = 0;
    state.mDeviceProcAddrCalls              = 0;
    state.mDeviceCommandLookups.clear();
    state.mImageViewCreateInfos.clear();
    state.mCreateImageViewAllocationCallbacks.clear();
    ensure("the exact live parent chain publishes one image-view generation", !owner.acquireSwapchainImagesGeneration(request));
    ensure("the parent exposes every resolved image and image view",
           owner.hasSwapchainImagesGeneration() && owner.resolvedSwapchainImageCount() == state.mSwapchainImages.size());
    for (std::uint32_t index = 0; index < owner.resolvedSwapchainImageCount(); ++index)
    {
        ensure("the parent preserves image and view index pairing",
               owner.swapchainImage(index) == state.mSwapchainImages[index] &&
                   owner.swapchainImageView(index) == state.mSwapchainImageViews[index]);
        const VkImageViewCreateInfo& create_info = state.mImageViewCreateInfos[index];
        ensure("each view uses the exact Stage 42 color-view contract",
               create_info.sType == VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO && create_info.pNext == nullptr && create_info.flags == 0 &&
                   create_info.image == state.mSwapchainImages[index] && create_info.viewType == VK_IMAGE_VIEW_TYPE_2D &&
                   create_info.format == owner.swapchainSurfaceFormat().format &&
                   create_info.components.r == VK_COMPONENT_SWIZZLE_IDENTITY && create_info.components.g == VK_COMPONENT_SWIZZLE_IDENTITY &&
                   create_info.components.b == VK_COMPONENT_SWIZZLE_IDENTITY && create_info.components.a == VK_COMPONENT_SWIZZLE_IDENTITY &&
                   create_info.subresourceRange.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT && create_info.subresourceRange.baseMipLevel == 0 &&
                   create_info.subresourceRange.levelCount == 1 && create_info.subresourceRange.baseArrayLayer == 0 &&
                   create_info.subresourceRange.layerCount == 1 && state.mCreateImageViewAllocationCallbacks[index] == nullptr);
    }
    ensure("out-of-range image observations are neutral",
           owner.swapchainImage(owner.resolvedSwapchainImageCount()) == VK_NULL_HANDLE &&
               owner.swapchainImageView(owner.resolvedSwapchainImageCount()) == VK_NULL_HANDLE);
    ensure("image dispatch uses the exact instance, device, and swapchain",
           state.mGetDeviceProcAddrResolutionCalls == 1 && state.mGetDeviceProcAddrResolutionInstance == state.mInstance &&
               state.mDeviceProcAddrCalls == 3 && state.mDeviceProcAddrDevice == state.mDevice &&
               state.mDeviceCommandLookups ==
                   std::vector<std::string>{ "vkGetSwapchainImagesKHR", "vkCreateImageView", "vkDestroyImageView" } &&
               state.mSwapchainImageCountCalls == 1 && state.mSwapchainImageListCalls == 1 &&
               state.mSwapchainImagesDevice == state.mDevice && state.mEnumeratedSwapchain == state.mSwapchain &&
               state.mCreateImageViewCalls == state.mSwapchainImages.size() && state.mCreateImageViewDevice == state.mDevice);

    ensureSwapchainImagesCode(owner.acquireSwapchainImagesGeneration(request),
                              VulkanSwapchainImagesAcquireCode::SwapchainImagesAlreadyOwned);
    ensure_equals("duplicate acquisition performs no second image query", state.mSwapchainImageCountCalls, std::size_t{ 1 });

    state.mImageViewDestroyOwner = &owner;
    owner.resetSwapchainImagesGeneration();
    ensure("explicit image-view reset preserves the swapchain and every older parent",
           !owner.hasSwapchainImagesGeneration() && owner.resolvedSwapchainImageCount() == 0 && owner.swapchainImage(0) == VK_NULL_HANDLE &&
               owner.swapchainImageView(0) == VK_NULL_HANDLE && owner.hasSwapchainGeneration() && owner.swapchain() == state.mSwapchain &&
               owner.hasSwapchainConfigurationGeneration() && owner.hasLogicalDeviceGeneration() && owner.hasSurfaceGeneration() &&
               state.mDestroyImageViewCalls == state.mSwapchainImageViews.size() && state.mDestroyImageViewDevice == state.mDevice &&
               state.mImageViewDestroyObservationMade && state.mObservedSwapchainAtImageViewDestroy &&
               state.mObservedConfigurationAtImageViewDestroy && state.mObservedLogicalAtImageViewDestroy &&
               state.mObservedSurfaceAtImageViewDestroy &&
               state.mDestroyedImageViews == std::vector<VkImageView>{ state.mSwapchainImageViews[2], state.mSwapchainImageViews[1],
                                                                       state.mSwapchainImageViews[0] } &&
               std::all_of(state.mDestroyImageViewAllocationCallbacks.begin(),
                           state.mDestroyImageViewAllocationCallbacks.end(),
                           [](const VkAllocationCallbacks* callbacks) { return callbacks == nullptr; }));
    owner.resetSwapchainImagesGeneration();
    ensure_equals("a second explicit image-view reset is idempotent", state.mDestroyImageViewCalls, std::size_t{ 3 });
    ensure("the same parent chain reacquires after explicit image-view reset", !owner.acquireSwapchainImagesGeneration(request));
}

template<>
template<>
void render_vulkan_instance_test_object::test<45>()
{
    for (bool fail_instance_owner : { true, false })
    {
        FakeState                state;
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        acquireSwapchainChain(state, owner);

        state.mInstanceOwnerChecks                      = 0;
        state.mSurfaceWindowChecks                      = 0;
        state.mFailInstanceOwnerCheck                   = fail_instance_owner ? 2 : 0;
        state.mFailSurfaceWindowCheck                   = fail_instance_owner ? 0 : 2;
        const VulkanSwapchainImagesAcquireResult result = owner.acquireSwapchainImagesGeneration(makeSwapchainImagesRequest(state, owner));
        ensureSwapchainImagesCode(result, fail_instance_owner ? VulkanSwapchainImagesAcquireCode::StaleInstanceOwner
                                                              : VulkanSwapchainImagesAcquireCode::StaleWindowGeneration);
        ensure("image freshness is reauthenticated after all native views exist",
               state.mSwapchainImageCountCalls == 1 && state.mSwapchainImageListCalls == 1 &&
                   state.mCreateImageViewCalls == state.mSwapchainImages.size() &&
                   state.mDestroyImageViewCalls == state.mSwapchainImageViews.size() && state.mInstanceOwnerChecks == 2 &&
                   state.mSurfaceWindowChecks == (fail_instance_owner ? 1 : 2));
        ensure("stale publication rolls back every pending view and preserves every parent",
               !owner.hasSwapchainImagesGeneration() && owner.hasSwapchainGeneration() && owner.hasSwapchainConfigurationGeneration() &&
                   owner.hasLogicalDeviceGeneration() && owner.hasSurfaceGeneration() && state.mDestroySwapchainCalls == 0 &&
                   state.mDestroyDeviceCalls == 0 && state.mDestroySurfaceCalls == 0);

        state.mFailInstanceOwnerCheck = 0;
        state.mFailSurfaceWindowCheck = 0;
        ensure("current parents retry after stale image publication",
               !owner.acquireSwapchainImagesGeneration(makeSwapchainImagesRequest(state, owner)) && owner.hasSwapchainImagesGeneration() &&
                   state.mCreateImageViewCalls == 2 * state.mSwapchainImages.size() &&
                   state.mDestroyImageViewCalls == state.mSwapchainImageViews.size());
        owner.resetSwapchainImagesGeneration();
        ensure_equals("the retried image owner destroys its views exactly once",
                      state.mDestroyImageViewCalls,
                      2 * state.mSwapchainImageViews.size());
    }

    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireSwapchainChain(state, owner);
    const VulkanSwapchainImagesRequest request = makeSwapchainImagesRequest(state, owner);

    const VulkanSwapchainImagesAcquireResult result = VulkanInstanceDetail::acquireSwapchainImages(owner, request, failAllocation);
    ensureSwapchainImagesCode(result, VulkanSwapchainImagesAcquireCode::AllocationFailure);
    ensure("parent allocation failure rolls back every resolved view and preserves the full parent chain",
           state.mSwapchainImageCountCalls == 1 && state.mSwapchainImageListCalls == 1 &&
               state.mCreateImageViewCalls == state.mSwapchainImages.size() &&
               state.mDestroyImageViewCalls == state.mSwapchainImageViews.size() && !owner.hasSwapchainImagesGeneration() &&
               owner.hasSwapchainGeneration() && owner.hasSwapchainConfigurationGeneration() && owner.hasLogicalDeviceGeneration() &&
               owner.logicalDevice() == state.mDevice && state.mDestroySwapchainCalls == 0 && state.mDestroyDeviceCalls == 0);
    ensure("the live chain retries after image owner allocation failure",
           !owner.acquireSwapchainImagesGeneration(request) && owner.hasSwapchainImagesGeneration() &&
               state.mCreateImageViewCalls == 2 * state.mSwapchainImages.size() &&
               state.mDestroyImageViewCalls == state.mSwapchainImageViews.size());
    owner.resetSwapchainImagesGeneration();
    ensure_equals("the allocation retry owns one independent view set",
                  state.mDestroyImageViewCalls,
                  2 * state.mSwapchainImageViews.size());
}

template<>
template<>
void render_vulkan_instance_test_object::test<46>()
{
    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration first = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireSwapchainChain(state, first);
    ensure("image-view acquisition succeeds before parent move",
           !first.acquireSwapchainImagesGeneration(makeSwapchainImagesRequest(state, first)));

    VulkanInstanceGeneration moved(std::move(first));
    ensure("the parent move transfers the complete image-view chain",
           !first.hasSwapchainImagesGeneration() && first.resolvedSwapchainImageCount() == 0 && first.swapchainImage(0) == VK_NULL_HANDLE &&
               first.swapchainImageView(0) == VK_NULL_HANDLE && !first.hasSwapchainGeneration() &&
               !first.hasSwapchainConfigurationGeneration() && !first.hasLogicalDeviceGeneration() &&
               moved.hasSwapchainImagesGeneration() && moved.resolvedSwapchainImageCount() == state.mSwapchainImages.size() &&
               moved.swapchainImage(0) == state.mSwapchainImages[0] && moved.swapchainImageView(0) == state.mSwapchainImageViews[0] &&
               moved.hasSwapchainGeneration() && moved.swapchain() == state.mSwapchain && moved.hasLogicalDeviceGeneration());
    first.reset();
    ensure("resetting the moved-from parent destroys no Vulkan object",
           state.mDestroyImageViewCalls == 0 && state.mDestroySwapchainCalls == 0 && state.mDestroyDeviceCalls == 0 &&
               state.mDestroySurfaceCalls == 0 && state.mDestroyInstanceCalls == 0);

    state.mImageViewDestroyOwner = &moved;
    state.mSwapchainDestroyOwner = &moved;
    state.mDeviceDestroyOwner    = &moved;
    state.mSurfaceDestroyOwner   = &moved;
    moved.reset();
    ensure("image-view destruction observes its swapchain and older parents still live",
           state.mImageViewDestroyObservationMade && state.mObservedSwapchainAtImageViewDestroy &&
               state.mObservedConfigurationAtImageViewDestroy && state.mObservedLogicalAtImageViewDestroy &&
               state.mObservedSurfaceAtImageViewDestroy);
    ensure("swapchain destruction observes image views already removed while older parents remain live",
           state.mSwapchainDestroyObservationMade && !state.mObservedSwapchainImagesAtSwapchainDestroy &&
               state.mObservedConfigurationAtSwapchainDestroy && state.mObservedLogicalAtSwapchainDestroy &&
               state.mObservedSurfaceAtSwapchainDestroy);
    ensure("device and surface destruction observe both younger swapchain generations removed",
           state.mDeviceDestroyObservationMade && !state.mObservedSwapchainImagesAtDeviceDestroy &&
               !state.mObservedSwapchainAtDeviceDestroy && state.mSurfaceDestroyObservationMade &&
               !state.mObservedSwapchainImagesAtSurfaceDestroy && !state.mObservedSwapchainAtSurfaceDestroy);
    ensure("full reset destroys image views before the swapchain and all older parents exactly once",
           state.mEvents == std::vector<Event>{ Event::CreateInstance, Event::CreateSurface, Event::CreateDevice, Event::GetDeviceQueue,
                                                Event::CreateSwapchain, Event::CreateImageView, Event::CreateImageView,
                                                Event::CreateImageView, Event::DestroyImageView, Event::DestroyImageView,
                                                Event::DestroyImageView, Event::DestroySwapchain, Event::DestroyDevice,
                                                Event::DestroySurface, Event::DestroyInstance } &&
               state.mDestroyImageViewCalls == state.mSwapchainImageViews.size() && state.mDestroySwapchainCalls == 1 &&
               state.mDestroyDeviceCalls == 1 && state.mDestroySurfaceCalls == 1 && state.mDestroyInstanceCalls == 1 &&
               state.mDestroyedImageViews ==
                   std::vector<VkImageView>{ state.mSwapchainImageViews[2], state.mSwapchainImageViews[1], state.mSwapchainImageViews[0] });
}

template<>
template<>
void render_vulkan_instance_test_object::test<47>()
{
    static_assert(std::is_same_v<VulkanSwapchainFrameSlotAcquireResult, std::optional<VulkanSwapchainFrameSlotAcquireError>>);
    static_assert(noexcept(std::declval<VulkanInstanceGeneration&>().acquireSwapchainFrameSlotGeneration(
        std::declval<const VulkanSwapchainFrameSlotRequest&>())));
    static_assert(noexcept(std::declval<VulkanInstanceGeneration&>().resetSwapchainFrameSlotGeneration()));

    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    ensure("an empty parent exposes neutral frame-slot state",
           !owner.hasSwapchainFrameSlotGeneration() && owner.swapchainFrameCommandPool() == VK_NULL_HANDLE &&
               owner.swapchainFrameCommandBuffer() == VK_NULL_HANDLE && owner.swapchainFrameImageAvailableSemaphore() == VK_NULL_HANDLE &&
               owner.swapchainFrameSubmissionFence() == VK_NULL_HANDLE);

    VulkanSwapchainFrameSlotRequest request = makeSwapchainFrameSlotRequest(state, owner);
    request.mInstanceOwnerCheck.mIsCurrent  = nullptr;
    ensureSwapchainFrameSlotCode(owner.acquireSwapchainFrameSlotGeneration(request),
                                 VulkanSwapchainFrameSlotAcquireCode::InvalidInstanceOwnerCheck);
    request                                   = makeSwapchainFrameSlotRequest(state, owner);
    request.mWindowGenerationCheck.mIsCurrent = nullptr;
    ensureSwapchainFrameSlotCode(owner.acquireSwapchainFrameSlotGeneration(request),
                                 VulkanSwapchainFrameSlotAcquireCode::InvalidWindowGenerationCheck);
    request                         = makeSwapchainFrameSlotRequest(state, owner);
    request.mNativeWindowGeneration = 0;
    ensureSwapchainFrameSlotCode(owner.acquireSwapchainFrameSlotGeneration(request),
                                 VulkanSwapchainFrameSlotAcquireCode::InvalidNativeWindowGeneration);
    request                 = makeSwapchainFrameSlotRequest(state, owner);
    request.mDrawableExtent = { 0, 600 };
    ensureSwapchainFrameSlotCode(owner.acquireSwapchainFrameSlotGeneration(request),
                                 VulkanSwapchainFrameSlotAcquireCode::InvalidDrawableExtent);

    ensureSwapchainFrameSlotCode(owner.acquireSwapchainFrameSlotGeneration(makeSwapchainFrameSlotRequest(state, owner)),
                                 VulkanSwapchainFrameSlotAcquireCode::SurfaceNotLive);
    ensure("surface succeeds for frame-slot preflight", !owner.acquireSurfaceGeneration(makeSurfaceRequest(state, owner)));
    ensureSwapchainFrameSlotCode(owner.acquireSwapchainFrameSlotGeneration(makeSwapchainFrameSlotRequest(state, owner)),
                                 VulkanSwapchainFrameSlotAcquireCode::PresentationDeviceNotLive);
    ensure("selection succeeds for frame-slot preflight",
           !owner.acquirePresentationDeviceGeneration(makePresentationDeviceRequest(state, owner)));
    ensureSwapchainFrameSlotCode(owner.acquireSwapchainFrameSlotGeneration(makeSwapchainFrameSlotRequest(state, owner)),
                                 VulkanSwapchainFrameSlotAcquireCode::LogicalDeviceNotLive);
    ensure("logical device succeeds for frame-slot preflight",
           !owner.acquireLogicalDeviceGeneration(makeLogicalDeviceRequest(state, owner)));
    ensureSwapchainFrameSlotCode(owner.acquireSwapchainFrameSlotGeneration(makeSwapchainFrameSlotRequest(state, owner)),
                                 VulkanSwapchainFrameSlotAcquireCode::SwapchainConfigurationNotLive);
    ensure("configuration succeeds for frame-slot preflight",
           !owner.acquireSwapchainConfigurationGeneration(makeSwapchainConfigurationRequest(state, owner)));
    ensureSwapchainFrameSlotCode(owner.acquireSwapchainFrameSlotGeneration(makeSwapchainFrameSlotRequest(state, owner)),
                                 VulkanSwapchainFrameSlotAcquireCode::SwapchainNotLive);
    ensure("swapchain succeeds for frame-slot preflight", !owner.acquireSwapchainGeneration(makeSwapchainRequest(state, owner)));
    ensureSwapchainFrameSlotCode(owner.acquireSwapchainFrameSlotGeneration(makeSwapchainFrameSlotRequest(state, owner)),
                                 VulkanSwapchainFrameSlotAcquireCode::SwapchainImagesNotLive);
    ensure("swapchain images succeed for frame-slot preflight",
           !owner.acquireSwapchainImagesGeneration(makeSwapchainImagesRequest(state, owner)));

    request                         = makeSwapchainFrameSlotRequest(state, owner);
    request.mNativeWindowGeneration = 41;
    ensureSwapchainFrameSlotCode(owner.acquireSwapchainFrameSlotGeneration(request),
                                 VulkanSwapchainFrameSlotAcquireCode::NativeWindowGenerationMismatch);
    request                 = makeSwapchainFrameSlotRequest(state, owner);
    request.mDrawableExtent = { 801, 600 };
    ensureSwapchainFrameSlotCode(owner.acquireSwapchainFrameSlotGeneration(request),
                                 VulkanSwapchainFrameSlotAcquireCode::DrawableExtentMismatch);
    request                     = makeSwapchainFrameSlotRequest(state, owner);
    state.mInstanceOwnerCurrent = false;
    ensureSwapchainFrameSlotCode(owner.acquireSwapchainFrameSlotGeneration(request),
                                 VulkanSwapchainFrameSlotAcquireCode::StaleInstanceOwner);
    state.mInstanceOwnerCurrent = true;
    state.mSurfaceWindowCurrent = false;
    ensureSwapchainFrameSlotCode(owner.acquireSwapchainFrameSlotGeneration(request),
                                 VulkanSwapchainFrameSlotAcquireCode::StaleWindowGeneration);
    state.mSurfaceWindowCurrent = true;
    ensure("all malformed, incomplete, and stale preflights avoid frame-slot mutation",
           state.mCreateCommandPoolCalls == 0 && state.mAllocateCommandBufferCalls == 0 && state.mCreateSemaphoreCalls == 0 &&
               state.mCreateFenceCalls == 0 && !owner.hasSwapchainFrameSlotGeneration());

    owner.reset();
    ensureSwapchainFrameSlotCode(owner.acquireSwapchainFrameSlotGeneration(makeSwapchainFrameSlotRequest(state, owner)),
                                 VulkanSwapchainFrameSlotAcquireCode::InstanceNotLive);
}

template<>
template<>
void render_vulkan_instance_test_object::test<48>()
{
    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireSwapchainImagesChain(state, owner, { 1280, 720 });
    const VulkanSwapchainFrameSlotRequest request = makeSwapchainFrameSlotRequest(state, owner, { 1280, 720 });

    constexpr std::array missing_commands{
        std::pair{ MissingCommand::GetDeviceProcAddr, VulkanSwapchainFrameSlotCommand::GetDeviceProcAddr },
        std::pair{ MissingCommand::CreateCommandPool, VulkanSwapchainFrameSlotCommand::CreateCommandPool },
        std::pair{ MissingCommand::DestroyCommandPool, VulkanSwapchainFrameSlotCommand::DestroyCommandPool },
        std::pair{ MissingCommand::AllocateCommandBuffers, VulkanSwapchainFrameSlotCommand::AllocateCommandBuffers },
        std::pair{ MissingCommand::CreateSemaphore, VulkanSwapchainFrameSlotCommand::CreateSemaphore },
        std::pair{ MissingCommand::DestroySemaphore, VulkanSwapchainFrameSlotCommand::DestroySemaphore },
        std::pair{ MissingCommand::CreateFence, VulkanSwapchainFrameSlotCommand::CreateFence },
        std::pair{ MissingCommand::DestroyFence, VulkanSwapchainFrameSlotCommand::DestroyFence }
    };
    for (const auto& [missing, command] : missing_commands)
    {
        state.mMissing                                     = missing;
        const VulkanSwapchainFrameSlotAcquireResult result = owner.acquireSwapchainFrameSlotGeneration(request);
        const VulkanSwapchainFrameSlotAcquireError& error  = requireSwapchainFrameSlotError(result);
        ensure("the parent preserves exact nested frame-slot dispatch identity",
               error.mCode == VulkanSwapchainFrameSlotAcquireCode::ResolutionFailure && error.mResolutionError &&
                   error.mResolutionError->mCode == VulkanSwapchainFrameSlotResolutionCode::MissingRequiredCommand &&
                   error.mResolutionError->mCommand == command);
        ensure("a missing frame-slot command creates and publishes nothing",
               !owner.hasSwapchainFrameSlotGeneration() && state.mCreateCommandPoolCalls == 0 && state.mAllocateCommandBufferCalls == 0 &&
                   state.mCreateSemaphoreCalls == 0 && state.mCreateFenceCalls == 0);
    }

    state.mMissing                                              = MissingCommand::None;
    state.mCommandPoolCreateResult                              = VK_ERROR_OUT_OF_DEVICE_MEMORY;
    const VulkanSwapchainFrameSlotAcquireResult creation_result = owner.acquireSwapchainFrameSlotGeneration(request);
    const VulkanSwapchainFrameSlotAcquireError& creation_error  = requireSwapchainFrameSlotError(creation_result);
    ensure("the parent preserves the exact command-pool failure",
           creation_error.mCode == VulkanSwapchainFrameSlotAcquireCode::ResolutionFailure && creation_error.mResolutionError &&
               creation_error.mResolutionError->mCode == VulkanSwapchainFrameSlotResolutionCode::CommandPoolCreationFailure &&
               creation_error.mResolutionError->mCommand == VulkanSwapchainFrameSlotCommand::CreateCommandPool &&
               creation_error.mResolutionError->mResult == VK_ERROR_OUT_OF_DEVICE_MEMORY && !owner.hasSwapchainFrameSlotGeneration());

    state.mCommandPoolCreateResult          = VK_SUCCESS;
    state.mCreateCommandPoolCalls           = 0;
    state.mAllocateCommandBufferCalls       = 0;
    state.mCreateSemaphoreCalls             = 0;
    state.mCreateFenceCalls                 = 0;
    state.mGetDeviceProcAddrResolutionCalls = 0;
    state.mDeviceProcAddrCalls              = 0;
    state.mDeviceCommandLookups.clear();
    ensure("the exact live chain publishes one frame slot", !owner.acquireSwapchainFrameSlotGeneration(request));
    ensure("the parent exposes all six frame-slot handles",
           owner.hasSwapchainFrameSlotGeneration() && owner.swapchainFrameCommandPool() == state.mCommandPool &&
               owner.swapchainFrameCommandBuffer() == state.mCommandBuffer &&
               owner.swapchainFrameImageAvailableSemaphore() == state.mImageAvailableSemaphore &&
               owner.swapchainFramePresentationReadySemaphore() == state.mPresentationReadySemaphore &&
               owner.swapchainFrameSubmissionFence() == state.mSubmissionFence &&
               owner.swapchainFramePresentCompletionFence() == state.mPresentCompletionFence && !owner.swapchainFrameAcquiredImageIndex());
    ensure("frame-slot dispatch resolves through the exact instance and device",
           state.mGetDeviceProcAddrResolutionCalls == 1 && state.mGetDeviceProcAddrResolutionInstance == state.mInstance &&
               state.mDeviceProcAddrCalls == 7 && state.mDeviceProcAddrDevice == state.mDevice &&
               state.mDeviceCommandLookups == std::vector<std::string>{ "vkCreateCommandPool", "vkDestroyCommandPool",
                                                                        "vkAllocateCommandBuffers", "vkCreateSemaphore",
                                                                        "vkDestroySemaphore", "vkCreateFence", "vkDestroyFence" });
    ensure("the frame slot uses one resettable primary buffer, two binary semaphores, and two signaled fences",
           state.mCreateCommandPoolCalls == 1 && state.mFrameSlotDevice == state.mDevice &&
               state.mCommandPoolCreateInfo.sType == VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO &&
               state.mCommandPoolCreateInfo.pNext == nullptr &&
               state.mCommandPoolCreateInfo.flags == VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT &&
               state.mCommandPoolCreateInfo.queueFamilyIndex == 0 && state.mAllocateCommandBufferCalls == 1 &&
               state.mCommandBufferAllocateInfo.sType == VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO &&
               state.mCommandBufferAllocateInfo.pNext == nullptr && state.mCommandBufferAllocateInfo.commandPool == state.mCommandPool &&
               state.mCommandBufferAllocateInfo.level == VK_COMMAND_BUFFER_LEVEL_PRIMARY &&
               state.mCommandBufferAllocateInfo.commandBufferCount == 1 && state.mCreateSemaphoreCalls == 2 &&
               state.mSemaphoreCreateInfo.sType == VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO && state.mSemaphoreCreateInfo.pNext == nullptr &&
               state.mSemaphoreCreateInfo.flags == 0 && state.mCreateFenceCalls == 2 &&
               state.mFenceCreateInfo.sType == VK_STRUCTURE_TYPE_FENCE_CREATE_INFO && state.mFenceCreateInfo.pNext == nullptr &&
               state.mFenceCreateInfo.flags == VK_FENCE_CREATE_SIGNALED_BIT && state.mCreateCommandPoolAllocationCallbacks == nullptr &&
               state.mCreateSemaphoreAllocationCallbacks == nullptr && state.mCreateFenceAllocationCallbacks == nullptr);

    ensureSwapchainFrameSlotCode(owner.acquireSwapchainFrameSlotGeneration(request),
                                 VulkanSwapchainFrameSlotAcquireCode::SwapchainFrameSlotAlreadyOwned);
    ensure("duplicate acquisition leaves the published slot untouched",
           owner.swapchainFrameCommandPool() == state.mCommandPool && state.mCreateCommandPoolCalls == 1);

    state.mFrameSlotDestroyOwner  = &owner;
    const std::size_t reset_event = state.mEvents.size();
    owner.resetSwapchainFrameSlotGeneration();
    ensure("explicit reset destroys the slot in reverse order while every parent remains live",
           !owner.hasSwapchainFrameSlotGeneration() && owner.swapchainFrameCommandPool() == VK_NULL_HANDLE &&
               owner.swapchainFrameCommandBuffer() == VK_NULL_HANDLE && owner.swapchainFrameImageAvailableSemaphore() == VK_NULL_HANDLE &&
               owner.swapchainFramePresentationReadySemaphore() == VK_NULL_HANDLE &&
               owner.swapchainFrameSubmissionFence() == VK_NULL_HANDLE && owner.swapchainFramePresentCompletionFence() == VK_NULL_HANDLE &&
               !owner.swapchainFrameAcquiredImageIndex() && owner.hasSwapchainImagesGeneration() && owner.hasSwapchainGeneration() &&
               owner.hasSwapchainConfigurationGeneration() && owner.hasLogicalDeviceGeneration() && owner.hasSurfaceGeneration() &&
               state.mDestroyFenceCalls == 2 && state.mDestroySemaphoreCalls == 2 && state.mDestroyCommandPoolCalls == 1 &&
               state.mFrameSlotDestroyObservationMade && state.mObservedImagesAtFrameSlotDestroy &&
               state.mObservedSwapchainAtFrameSlotDestroy && state.mObservedConfigurationAtFrameSlotDestroy &&
               state.mObservedLogicalAtFrameSlotDestroy && state.mObservedSurfaceAtFrameSlotDestroy &&
               std::vector<Event>(state.mEvents.begin() + static_cast<std::ptrdiff_t>(reset_event), state.mEvents.end()) ==
                   std::vector<Event>{ Event::DestroyFence, Event::DestroyFence, Event::DestroySemaphore, Event::DestroySemaphore,
                                       Event::DestroyCommandPool } &&
               state.mDestroyCommandPoolAllocationCallbacks == nullptr && state.mDestroySemaphoreAllocationCallbacks == nullptr &&
               state.mDestroyFenceAllocationCallbacks == nullptr);
    owner.resetSwapchainFrameSlotGeneration();
    ensure_equals("a second explicit frame-slot reset is idempotent", state.mDestroyCommandPoolCalls, std::size_t{ 1 });

    ensure("the same images parent reacquires a frame slot", !owner.acquireSwapchainFrameSlotGeneration(request));
    state.mImageViewDestroyOwner = &owner;
    owner.resetSwapchainImagesGeneration();
    ensure("replacing the images generation removes its frame slot first",
           !owner.hasSwapchainFrameSlotGeneration() && !owner.hasSwapchainImagesGeneration() && state.mDestroyCommandPoolCalls == 2 &&
               state.mImageViewDestroyObservationMade && !state.mObservedFrameSlotAtImageViewDestroy);
    ensure("the retained swapchain chain can replace both youngest generations",
           !owner.acquireSwapchainImagesGeneration(makeSwapchainImagesRequest(state, owner, { 1280, 720 })) &&
               !owner.acquireSwapchainFrameSlotGeneration(request));
}

template<>
template<>
void render_vulkan_instance_test_object::test<49>()
{
    for (bool fail_instance_owner : { true, false })
    {
        FakeState                state;
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        acquireSwapchainImagesChain(state, owner);

        state.mInstanceOwnerChecks    = 0;
        state.mSurfaceWindowChecks    = 0;
        state.mFailInstanceOwnerCheck = fail_instance_owner ? 2 : 0;
        state.mFailSurfaceWindowCheck = fail_instance_owner ? 0 : 2;
        const VulkanSwapchainFrameSlotAcquireResult result =
            owner.acquireSwapchainFrameSlotGeneration(makeSwapchainFrameSlotRequest(state, owner));
        ensureSwapchainFrameSlotCode(result, fail_instance_owner ? VulkanSwapchainFrameSlotAcquireCode::StaleInstanceOwner
                                                                 : VulkanSwapchainFrameSlotAcquireCode::StaleWindowGeneration);
        ensure("frame-slot freshness is reauthenticated after all native children exist",
               state.mCreateCommandPoolCalls == 1 && state.mAllocateCommandBufferCalls == 1 && state.mCreateSemaphoreCalls == 2 &&
                   state.mCreateFenceCalls == 2 && state.mDestroyFenceCalls == 2 && state.mDestroySemaphoreCalls == 2 &&
                   state.mDestroyCommandPoolCalls == 1 && state.mInstanceOwnerChecks == 2 &&
                   state.mSurfaceWindowChecks == (fail_instance_owner ? 1 : 2));
        ensure("stale publication rolls back the pending slot and preserves every parent",
               !owner.hasSwapchainFrameSlotGeneration() && owner.hasSwapchainImagesGeneration() && owner.hasSwapchainGeneration() &&
                   owner.hasSwapchainConfigurationGeneration() && owner.hasLogicalDeviceGeneration() && owner.hasSurfaceGeneration() &&
                   state.mDestroyImageViewCalls == 0 && state.mDestroySwapchainCalls == 0 && state.mDestroyDeviceCalls == 0);

        state.mFailInstanceOwnerCheck = 0;
        state.mFailSurfaceWindowCheck = 0;
        ensure("the current chain retries after stale frame-slot publication",
               !owner.acquireSwapchainFrameSlotGeneration(makeSwapchainFrameSlotRequest(state, owner)) &&
                   owner.hasSwapchainFrameSlotGeneration() && state.mCreateCommandPoolCalls == 2 && state.mDestroyCommandPoolCalls == 1);
        owner.resetSwapchainFrameSlotGeneration();
        ensure_equals("the retried frame slot destroys its pool exactly once", state.mDestroyCommandPoolCalls, std::size_t{ 2 });
    }

    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireSwapchainImagesChain(state, owner);
    const VulkanSwapchainFrameSlotRequest       request = makeSwapchainFrameSlotRequest(state, owner);
    const VulkanSwapchainFrameSlotAcquireResult result  = VulkanInstanceDetail::acquireSwapchainFrameSlot(owner, request, failAllocation);
    ensureSwapchainFrameSlotCode(result, VulkanSwapchainFrameSlotAcquireCode::AllocationFailure);
    ensure("parent allocation failure rolls back the fully resolved pending slot and preserves its parents",
           state.mCreateCommandPoolCalls == 1 && state.mAllocateCommandBufferCalls == 1 && state.mCreateSemaphoreCalls == 2 &&
               state.mCreateFenceCalls == 2 && state.mDestroyFenceCalls == 2 && state.mDestroySemaphoreCalls == 2 &&
               state.mDestroyCommandPoolCalls == 1 && !owner.hasSwapchainFrameSlotGeneration() && owner.hasSwapchainImagesGeneration() &&
               owner.hasSwapchainGeneration() && owner.hasLogicalDeviceGeneration() && state.mDestroyImageViewCalls == 0 &&
               state.mDestroySwapchainCalls == 0 && state.mDestroyDeviceCalls == 0);
    ensure("the live chain retries after frame-slot owner allocation failure",
           !owner.acquireSwapchainFrameSlotGeneration(request) && owner.hasSwapchainFrameSlotGeneration() &&
               state.mCreateCommandPoolCalls == 2 && state.mDestroyCommandPoolCalls == 1);
}

template<>
template<>
void render_vulkan_instance_test_object::test<50>()
{
    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration first = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireSwapchainImagesChain(state, first);
    ensure("frame-slot acquisition succeeds before parent move",
           !first.acquireSwapchainFrameSlotGeneration(makeSwapchainFrameSlotRequest(state, first)));

    VulkanInstanceGeneration moved(std::move(first));
    ensure("the parent move transfers the complete frame-slot chain",
           !first.hasSwapchainFrameSlotGeneration() && first.swapchainFrameCommandPool() == VK_NULL_HANDLE &&
               first.swapchainFrameCommandBuffer() == VK_NULL_HANDLE && first.swapchainFrameImageAvailableSemaphore() == VK_NULL_HANDLE &&
               first.swapchainFramePresentationReadySemaphore() == VK_NULL_HANDLE &&
               first.swapchainFrameSubmissionFence() == VK_NULL_HANDLE && first.swapchainFramePresentCompletionFence() == VK_NULL_HANDLE &&
               !first.swapchainFrameAcquiredImageIndex() && !first.hasSwapchainImagesGeneration() &&
               moved.hasSwapchainFrameSlotGeneration() && moved.swapchainFrameCommandPool() == state.mCommandPool &&
               moved.swapchainFrameCommandBuffer() == state.mCommandBuffer &&
               moved.swapchainFrameImageAvailableSemaphore() == state.mImageAvailableSemaphore &&
               moved.swapchainFramePresentationReadySemaphore() == state.mPresentationReadySemaphore &&
               moved.swapchainFrameSubmissionFence() == state.mSubmissionFence &&
               moved.swapchainFramePresentCompletionFence() == state.mPresentCompletionFence && !moved.swapchainFrameAcquiredImageIndex() &&
               moved.hasSwapchainImagesGeneration());
    first.reset();
    ensure("resetting the moved-from parent destroys no frame-slot or older object",
           state.mDestroyFenceCalls == 0 && state.mDestroySemaphoreCalls == 0 && state.mDestroyCommandPoolCalls == 0 &&
               state.mDestroyImageViewCalls == 0 && state.mDestroySwapchainCalls == 0 && state.mDestroyDeviceCalls == 0 &&
               state.mDestroySurfaceCalls == 0 && state.mDestroyInstanceCalls == 0);

    state.mFrameSlotDestroyOwner = &moved;
    state.mImageViewDestroyOwner = &moved;
    state.mSwapchainDestroyOwner = &moved;
    state.mDeviceDestroyOwner    = &moved;
    state.mSurfaceDestroyOwner   = &moved;
    moved.reset();
    ensure("frame-slot destruction observes its complete parent chain still live",
           state.mFrameSlotDestroyObservationMade && state.mObservedImagesAtFrameSlotDestroy &&
               state.mObservedSwapchainAtFrameSlotDestroy && state.mObservedConfigurationAtFrameSlotDestroy &&
               state.mObservedLogicalAtFrameSlotDestroy && state.mObservedSurfaceAtFrameSlotDestroy);
    ensure("all older Vulkan destruction observes the frame slot already removed",
           state.mImageViewDestroyObservationMade && !state.mObservedFrameSlotAtImageViewDestroy &&
               state.mSwapchainDestroyObservationMade && !state.mObservedFrameSlotAtSwapchainDestroy &&
               state.mDeviceDestroyObservationMade && !state.mObservedFrameSlotAtDeviceDestroy && state.mSurfaceDestroyObservationMade &&
               !state.mObservedFrameSlotAtSurfaceDestroy);
    ensure("full reset destroys the frame slot, images, swapchain, and older parents exactly once in child-first order",
           state.mEvents == std::vector<Event>{ Event::CreateInstance,        Event::CreateSurface,    Event::CreateDevice,
                                                Event::GetDeviceQueue,        Event::CreateSwapchain,  Event::CreateImageView,
                                                Event::CreateImageView,       Event::CreateImageView,  Event::CreateCommandPool,
                                                Event::AllocateCommandBuffer, Event::CreateSemaphore,  Event::CreateSemaphore,
                                                Event::CreateFence,           Event::CreateFence,      Event::DestroyFence,
                                                Event::DestroyFence,          Event::DestroySemaphore, Event::DestroySemaphore,
                                                Event::DestroyCommandPool,    Event::DestroyImageView, Event::DestroyImageView,
                                                Event::DestroyImageView,      Event::DestroySwapchain, Event::DestroyDevice,
                                                Event::DestroySurface,        Event::DestroyInstance } &&
               state.mDestroyFenceCalls == 2 && state.mDestroySemaphoreCalls == 2 && state.mDestroyCommandPoolCalls == 1 &&
               state.mDestroyImageViewCalls == state.mSwapchainImageViews.size() && state.mDestroySwapchainCalls == 1 &&
               state.mDestroyDeviceCalls == 1 && state.mDestroySurfaceCalls == 1 && state.mDestroyInstanceCalls == 1);
}

template<>
template<>
void render_vulkan_instance_test_object::test<51>()
{
    static_assert(static_cast<std::uint8_t>(VulkanSwapchainFrameSlotParentOperationCode::OperationFailure) == 16);
    static_assert(static_cast<std::uint8_t>(VulkanSwapchainFrameSlotParentOperationCode::InvalidClearColor) == 17);
    static_assert(std::variant_size_v<VulkanSwapchainFrameSlotParentOperationResult> == 2);
    static_assert(std::is_same_v<std::variant_alternative_t<0, VulkanSwapchainFrameSlotParentOperationResult>,
                                 VulkanSwapchainFrameSlotParentOperationError>);
    static_assert(
        std::is_same_v<std::variant_alternative_t<1, VulkanSwapchainFrameSlotParentOperationResult>, VulkanSwapchainFrameSlotDisposition>);
    static_assert(noexcept(std::declval<VulkanInstanceGeneration&>().roundTripEmptySwapchainFrameSlot(
        std::declval<const VulkanSwapchainFrameSlotOperationRequest&>())));
    static_assert(noexcept(std::declval<VulkanInstanceGeneration&>().retryEmptySwapchainFrameSlotCompletion(
        std::declval<const VulkanSwapchainFrameSlotOperationRequest&>())));
    static_assert(std::variant_size_v<VulkanSwapchainFrameSlotParentPresentationResult> == 2);
    static_assert(std::is_same_v<std::variant_alternative_t<0, VulkanSwapchainFrameSlotParentPresentationResult>,
                                 VulkanSwapchainFrameSlotParentOperationError>);
    static_assert(std::is_same_v<std::variant_alternative_t<1, VulkanSwapchainFrameSlotParentPresentationResult>,
                                 VulkanSwapchainFrameSlotPresentationSuccess>);
    static_assert(noexcept(std::declval<VulkanInstanceGeneration&>().acquireToPresentSwapchainFrameSlot(
        std::declval<const VulkanSwapchainFrameSlotOperationRequest&>())));
    static_assert(noexcept(std::declval<VulkanInstanceGeneration&>().acquireClearToPresentSwapchainFrameSlot(
        std::declval<const VulkanSwapchainFrameSlotOperationRequest&>(),
        std::declval<const VulkanSwapchainFrameClearColor&>())));
    static_assert(noexcept(std::declval<VulkanInstanceGeneration&>().retrySwapchainFrameSlotPresentation(
        std::declval<const VulkanSwapchainFrameSlotOperationRequest&>())));
    static_assert(noexcept(std::declval<VulkanInstanceGeneration&>().retrySwapchainFrameSlotPresentationCompletion(
        std::declval<const VulkanSwapchainFrameSlotOperationRequest&>())));
    static_assert(noexcept(std::declval<VulkanInstanceGeneration&>().cancelSwapchainFrameSlotPresentation(
        std::declval<const VulkanSwapchainFrameSlotOperationRequest&>())));
    static_assert(noexcept(std::declval<VulkanInstanceGeneration&>().retrySwapchainFrameSlotCancellationCompletion(
        std::declval<const VulkanSwapchainFrameSlotOperationRequest&>())));
    static_assert(std::is_same_v<decltype(std::declval<const VulkanInstanceGeneration&>().swapchainFrameSlotDisposition()),
                                 std::optional<VulkanSwapchainFrameSlotDisposition>>);
    static_assert(std::is_same_v<decltype(std::declval<VulkanInstanceGeneration&>().reset()), bool>);

    const VulkanSwapchainFrameSlotParentOperationError left{ VulkanSwapchainFrameSlotParentOperationCode::OperationFailure,
                                                             VulkanSwapchainFrameSlotOperationError{
                                                                 VulkanSwapchainFrameSlotOperationCode::CommandFailure,
                                                                 VulkanSwapchainFrameSlotCommand::QueueSubmit, VK_ERROR_UNKNOWN,
                                                                 VulkanSwapchainFrameSlotDisposition::ResetRequired } };
    ensure("identical parent operation errors compare equal", left == left);

    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    ensure("a parent without a frame slot exposes no disposition", !owner.swapchainFrameSlotDisposition());

    VulkanSwapchainFrameSlotOperationRequest request = makeSwapchainFrameSlotOperationRequest(state, owner);
    request.mInstanceOwnerCheck                      = {};
    ensureSwapchainFrameSlotOperationCode(owner.roundTripEmptySwapchainFrameSlot(request),
                                          VulkanSwapchainFrameSlotParentOperationCode::InvalidInstanceOwnerCheck);
    request                        = makeSwapchainFrameSlotOperationRequest(state, owner);
    request.mWindowGenerationCheck = {};
    ensureSwapchainFrameSlotOperationCode(owner.roundTripEmptySwapchainFrameSlot(request),
                                          VulkanSwapchainFrameSlotParentOperationCode::InvalidWindowGenerationCheck);
    request                         = makeSwapchainFrameSlotOperationRequest(state, owner);
    request.mNativeWindowGeneration = 0;
    ensureSwapchainFrameSlotOperationCode(owner.roundTripEmptySwapchainFrameSlot(request),
                                          VulkanSwapchainFrameSlotParentOperationCode::InvalidNativeWindowGeneration);
    request                 = makeSwapchainFrameSlotOperationRequest(state, owner);
    request.mDrawableExtent = { 0, 600 };
    ensureSwapchainFrameSlotOperationCode(owner.roundTripEmptySwapchainFrameSlot(request),
                                          VulkanSwapchainFrameSlotParentOperationCode::InvalidDrawableExtent);

    request = makeSwapchainFrameSlotOperationRequest(state, owner);
    ensureSwapchainFrameSlotOperationCode(owner.roundTripEmptySwapchainFrameSlot(request),
                                          VulkanSwapchainFrameSlotParentOperationCode::SurfaceNotLive);
    ensure("surface acquisition succeeds for operation preflight", !owner.acquireSurfaceGeneration(makeSurfaceRequest(state, owner)));
    ensureSwapchainFrameSlotOperationCode(owner.roundTripEmptySwapchainFrameSlot(request),
                                          VulkanSwapchainFrameSlotParentOperationCode::PresentationDeviceNotLive);
    ensure("selection acquisition succeeds for operation preflight",
           !owner.acquirePresentationDeviceGeneration(makePresentationDeviceRequest(state, owner)));
    ensureSwapchainFrameSlotOperationCode(owner.roundTripEmptySwapchainFrameSlot(request),
                                          VulkanSwapchainFrameSlotParentOperationCode::LogicalDeviceNotLive);
    ensure("device acquisition succeeds for operation preflight",
           !owner.acquireLogicalDeviceGeneration(makeLogicalDeviceRequest(state, owner)));
    ensureSwapchainFrameSlotOperationCode(owner.roundTripEmptySwapchainFrameSlot(request),
                                          VulkanSwapchainFrameSlotParentOperationCode::SwapchainConfigurationNotLive);
    ensure("configuration acquisition succeeds for operation preflight",
           !owner.acquireSwapchainConfigurationGeneration(makeSwapchainConfigurationRequest(state, owner)));
    ensureSwapchainFrameSlotOperationCode(owner.roundTripEmptySwapchainFrameSlot(request),
                                          VulkanSwapchainFrameSlotParentOperationCode::SwapchainNotLive);
    ensure("swapchain acquisition succeeds for operation preflight", !owner.acquireSwapchainGeneration(makeSwapchainRequest(state, owner)));
    ensureSwapchainFrameSlotOperationCode(owner.roundTripEmptySwapchainFrameSlot(request),
                                          VulkanSwapchainFrameSlotParentOperationCode::SwapchainImagesNotLive);
    ensure("images acquisition succeeds for operation preflight",
           !owner.acquireSwapchainImagesGeneration(makeSwapchainImagesRequest(state, owner)));
    ensureSwapchainFrameSlotOperationCode(owner.roundTripEmptySwapchainFrameSlot(request),
                                          VulkanSwapchainFrameSlotParentOperationCode::SwapchainFrameSlotNotLive);
    ensure("slot acquisition succeeds for operation preflight",
           !owner.acquireSwapchainFrameSlotGeneration(makeSwapchainFrameSlotRequest(state, owner)));

    request                         = makeSwapchainFrameSlotOperationRequest(state, owner);
    request.mNativeWindowGeneration = 41;
    ensureSwapchainFrameSlotOperationCode(owner.roundTripEmptySwapchainFrameSlot(request),
                                          VulkanSwapchainFrameSlotParentOperationCode::NativeWindowGenerationMismatch);
    request                 = makeSwapchainFrameSlotOperationRequest(state, owner);
    request.mDrawableExtent = { 801, 600 };
    ensureSwapchainFrameSlotOperationCode(owner.roundTripEmptySwapchainFrameSlot(request),
                                          VulkanSwapchainFrameSlotParentOperationCode::DrawableExtentMismatch);
    request                     = makeSwapchainFrameSlotOperationRequest(state, owner);
    state.mInstanceOwnerCurrent = false;
    ensureSwapchainFrameSlotOperationCode(owner.roundTripEmptySwapchainFrameSlot(request),
                                          VulkanSwapchainFrameSlotParentOperationCode::StaleInstanceOwner);
    state.mInstanceOwnerCurrent = true;
    state.mSurfaceWindowCurrent = false;
    ensureSwapchainFrameSlotOperationCode(owner.roundTripEmptySwapchainFrameSlot(request),
                                          VulkanSwapchainFrameSlotParentOperationCode::StaleWindowGeneration);
    state.mSurfaceWindowCurrent = true;
    ensure("every rejected preflight avoids command-buffer and queue operations",
           state.mWaitForFencesCalls == 0 && state.mResetCommandBufferCalls == 0 && state.mBeginCommandBufferCalls == 0 &&
               state.mEndCommandBufferCalls == 0 && state.mResetFencesCalls == 0 && state.mQueueSubmitCalls == 0);

    owner.reset();
    ensureSwapchainFrameSlotOperationCode(owner.roundTripEmptySwapchainFrameSlot(makeSwapchainFrameSlotOperationRequest(state, owner)),
                                          VulkanSwapchainFrameSlotParentOperationCode::InstanceNotLive);
}

template<>
template<>
void render_vulkan_instance_test_object::test<52>()
{
    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireSwapchainFrameSlotChain(state, owner);
    const VulkanSwapchainFrameSlotOperationRequest request = makeSwapchainFrameSlotOperationRequest(state, owner);

    constexpr std::array missing_commands{
        std::pair{ MissingCommand::GetDeviceProcAddr, VulkanSwapchainFrameSlotCommand::GetDeviceProcAddr },
        std::pair{ MissingCommand::WaitForFences, VulkanSwapchainFrameSlotCommand::WaitForFences },
        std::pair{ MissingCommand::ResetCommandBuffer, VulkanSwapchainFrameSlotCommand::ResetCommandBuffer },
        std::pair{ MissingCommand::BeginCommandBuffer, VulkanSwapchainFrameSlotCommand::BeginCommandBuffer },
        std::pair{ MissingCommand::EndCommandBuffer, VulkanSwapchainFrameSlotCommand::EndCommandBuffer },
        std::pair{ MissingCommand::ResetFences, VulkanSwapchainFrameSlotCommand::ResetFences },
        std::pair{ MissingCommand::QueueSubmit, VulkanSwapchainFrameSlotCommand::QueueSubmit }
    };
    for (const auto& [missing, command] : missing_commands)
    {
        state.mMissing                                                   = missing;
        const VulkanSwapchainFrameSlotParentOperationResult result       = owner.roundTripEmptySwapchainFrameSlot(request);
        const VulkanSwapchainFrameSlotParentOperationError& parent_error = requireSwapchainFrameSlotOperationError(result);
        ensure("the parent preserves exact missing-operation dispatch identity",
               parent_error.mCode == VulkanSwapchainFrameSlotParentOperationCode::OperationFailure && parent_error.mOperationError &&
                   parent_error.mOperationError->mCode == VulkanSwapchainFrameSlotOperationCode::MissingRequiredCommand &&
                   parent_error.mOperationError->mCommand == command && parent_error.mOperationError->mResult == VK_SUCCESS &&
                   parent_error.mOperationError->mDisposition == VulkanSwapchainFrameSlotDisposition::Reusable);
    }
    ensure("dispatch-resolution failure invokes no Vulkan operation", state.mWaitForFencesCalls == 0 && state.mQueueSubmitCalls == 0);

    state.mMissing                                                  = MissingCommand::None;
    state.mWaitForFencesResults                                     = { VK_TIMEOUT };
    const VulkanSwapchainFrameSlotParentOperationResult wait_result = owner.roundTripEmptySwapchainFrameSlot(request);
    const VulkanSwapchainFrameSlotParentOperationError& wait_error  = requireSwapchainFrameSlotOperationError(wait_result);
    ensure("the parent preserves the exact initial-wait failure",
           wait_error.mCode == VulkanSwapchainFrameSlotParentOperationCode::OperationFailure && wait_error.mOperationError &&
               wait_error.mOperationError->mCode == VulkanSwapchainFrameSlotOperationCode::CommandFailure &&
               wait_error.mOperationError->mCommand == VulkanSwapchainFrameSlotCommand::WaitForFences &&
               wait_error.mOperationError->mResult == VK_TIMEOUT &&
               wait_error.mOperationError->mDisposition == VulkanSwapchainFrameSlotDisposition::Reusable &&
               owner.swapchainFrameSlotDisposition() == VulkanSwapchainFrameSlotDisposition::Reusable);

    state.mResetCommandBufferResult                                           = VK_ERROR_UNKNOWN;
    const VulkanSwapchainFrameSlotParentOperationResult reset_required_result = owner.roundTripEmptySwapchainFrameSlot(request);
    const VulkanSwapchainFrameSlotParentOperationError& reset_required_error =
        requireSwapchainFrameSlotOperationError(reset_required_result);
    ensure("the parent preserves ResetRequired and permits explicit retirement",
           reset_required_error.mCode == VulkanSwapchainFrameSlotParentOperationCode::OperationFailure &&
               reset_required_error.mOperationError &&
               reset_required_error.mOperationError->mCode == VulkanSwapchainFrameSlotOperationCode::CommandFailure &&
               reset_required_error.mOperationError->mCommand == VulkanSwapchainFrameSlotCommand::ResetCommandBuffer &&
               reset_required_error.mOperationError->mResult == VK_ERROR_UNKNOWN &&
               reset_required_error.mOperationError->mDisposition == VulkanSwapchainFrameSlotDisposition::ResetRequired &&
               owner.swapchainFrameSlotDisposition() == VulkanSwapchainFrameSlotDisposition::ResetRequired &&
               owner.resetSwapchainFrameSlotGeneration() && owner.hasSwapchainImagesGeneration());
}

template<>
template<>
void render_vulkan_instance_test_object::test<53>()
{
    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireSwapchainFrameSlotChain(state, owner, { 1280, 720 });
    const VulkanSwapchainFrameSlotOperationRequest request = makeSwapchainFrameSlotOperationRequest(state, owner, { 1280, 720 });

    state.mGetDeviceProcAddrResolutionCalls = 0;
    state.mDeviceProcAddrCalls              = 0;
    state.mDeviceCommandLookups.clear();
    state.mInstanceOwnerChecks        = 0;
    state.mSurfaceWindowChecks        = 0;
    const std::size_t operation_event = state.mEvents.size();

    ensureSwapchainFrameSlotOperationSuccess(owner.roundTripEmptySwapchainFrameSlot(request),
                                             VulkanSwapchainFrameSlotDisposition::Reusable);
    const std::size_t first_lookup_count = state.mDeviceProcAddrCalls;
    ensureSwapchainFrameSlotOperationSuccess(owner.roundTripEmptySwapchainFrameSlot(request),
                                             VulkanSwapchainFrameSlotDisposition::Reusable);
    ensure("both successful cycles expose the exact reusable disposition",
           owner.swapchainFrameSlotDisposition() == VulkanSwapchainFrameSlotDisposition::Reusable);
    ensure("the second cycle reuses retained dispatch while repeating both freshness passes",
           state.mGetDeviceProcAddrResolutionCalls == 1 && first_lookup_count == 6 && state.mDeviceProcAddrCalls == first_lookup_count &&
               state.mDeviceCommandLookups == std::vector<std::string>{ "vkWaitForFences", "vkResetCommandBuffer", "vkBeginCommandBuffer",
                                                                        "vkEndCommandBuffer", "vkResetFences", "vkQueueSubmit" } &&
               state.mInstanceOwnerChecks == 4 && state.mSurfaceWindowChecks == 4);
    ensure("two cycles submit the exact retained queue, command buffer, and fence without allocation callbacks",
           state.mWaitForFencesCalls == 4 && state.mResetCommandBufferCalls == 2 && state.mBeginCommandBufferCalls == 2 &&
               state.mEndCommandBufferCalls == 2 && state.mResetFencesCalls == 2 && state.mQueueSubmitCalls == 2 &&
               state.mOperationDevice == state.mDevice && state.mSubmittedQueue == state.mQueue &&
               state.mOperationCommandBuffer == state.mCommandBuffer && state.mOperationFence == state.mSubmissionFence &&
               state.mWaitAll == VK_TRUE && state.mWaitTimeout == std::numeric_limits<std::uint64_t>::max() &&
               state.mCreateCommandPoolCalls == 1 && state.mAllocateCommandBufferCalls == 1 && state.mCreateSemaphoreCalls == 2 &&
               state.mCreateFenceCalls == 2);
    const std::vector<Event> expected_cycle{ Event::WaitForFences,    Event::ResetCommandBuffer, Event::BeginCommandBuffer,
                                             Event::EndCommandBuffer, Event::ResetFences,        Event::QueueSubmit,
                                             Event::WaitForFences };
    std::vector<Event>       expected_events = expected_cycle;
    expected_events.insert(expected_events.end(), expected_cycle.begin(), expected_cycle.end());
    const std::vector<Event> actual_events(state.mEvents.begin() + static_cast<std::ptrdiff_t>(operation_event), state.mEvents.end());
    ensure("both parent cycles preserve exact core operation order", actual_events == expected_events);
}

template<>
template<>
void render_vulkan_instance_test_object::test<54>()
{
    for (bool fail_instance_owner : { true, false })
    {
        FakeState                state;
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        acquireSwapchainFrameSlotChain(state, owner);
        const VulkanSwapchainFrameSlotOperationRequest request = makeSwapchainFrameSlotOperationRequest(state, owner);

        state.mDeviceProcAddrCalls = 0;
        state.mDeviceCommandLookups.clear();
        state.mInstanceOwnerChecks    = 0;
        state.mSurfaceWindowChecks    = 0;
        state.mFailInstanceOwnerCheck = fail_instance_owner ? 2 : 0;
        state.mFailSurfaceWindowCheck = fail_instance_owner ? 0 : 2;
        ensureSwapchainFrameSlotOperationCode(owner.roundTripEmptySwapchainFrameSlot(request),
                                              fail_instance_owner ? VulkanSwapchainFrameSlotParentOperationCode::StaleInstanceOwner
                                                                  : VulkanSwapchainFrameSlotParentOperationCode::StaleWindowGeneration);
        ensure("post-resolution freshness rejection keeps the slot reusable and performs no Vulkan operation",
               owner.swapchainFrameSlotDisposition() == VulkanSwapchainFrameSlotDisposition::Reusable && state.mDeviceProcAddrCalls == 6 &&
                   state.mWaitForFencesCalls == 0 && state.mQueueSubmitCalls == 0 && state.mInstanceOwnerChecks == 2 &&
                   state.mSurfaceWindowChecks == (fail_instance_owner ? 1 : 2));
    }

    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireSwapchainFrameSlotChain(state, owner);
    const VulkanSwapchainFrameSlotOperationRequest request = makeSwapchainFrameSlotOperationRequest(state, owner);
    state.mInstanceOwnerChecks                             = 0;
    state.mSurfaceWindowChecks                             = 0;
    state.mMutationOwner                                   = &owner;
    state.mResetFrameSlotOnInstanceOwnerCheck              = 2;
    ensureSwapchainFrameSlotOperationCode(owner.roundTripEmptySwapchainFrameSlot(request),
                                          VulkanSwapchainFrameSlotParentOperationCode::SwapchainFrameSlotNotLive);
    ensure("a callback-side post-resolution owner mutation is caught before the retired slot can execute",
           !owner.hasSwapchainFrameSlotGeneration() && !owner.swapchainFrameSlotDisposition() && state.mDestroyFenceCalls == 2 &&
               state.mDestroySemaphoreCalls == 2 && state.mDestroyCommandPoolCalls == 1 && state.mWaitForFencesCalls == 0 &&
               state.mQueueSubmitCalls == 0);
}

template<>
template<>
void render_vulkan_instance_test_object::test<55>()
{
    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireSwapchainFrameSlotChain(state, owner);
    const VulkanSwapchainFrameSlotOperationRequest request = makeSwapchainFrameSlotOperationRequest(state, owner);
    state.mWaitForFencesResults                            = { VK_SUCCESS, VK_TIMEOUT, VK_TIMEOUT, VK_SUCCESS };

    const VulkanSwapchainFrameSlotParentOperationResult completion_result = owner.roundTripEmptySwapchainFrameSlot(request);
    const VulkanSwapchainFrameSlotParentOperationError& completion_error  = requireSwapchainFrameSlotOperationError(completion_result);
    ensure("a failed completion wait exposes a retained Pending obligation",
           completion_error.mCode == VulkanSwapchainFrameSlotParentOperationCode::OperationFailure && completion_error.mOperationError &&
               completion_error.mOperationError->mCode == VulkanSwapchainFrameSlotOperationCode::CommandFailure &&
               completion_error.mOperationError->mCommand == VulkanSwapchainFrameSlotCommand::WaitForFences &&
               completion_error.mOperationError->mResult == VK_TIMEOUT &&
               completion_error.mOperationError->mDisposition == VulkanSwapchainFrameSlotDisposition::Pending &&
               owner.swapchainFrameSlotDisposition() == VulkanSwapchainFrameSlotDisposition::Pending);

    ensure("direct Pending reset is refused", !owner.resetSwapchainFrameSlotGeneration());
    ensure("images reset is refused transitively while Pending", !owner.resetSwapchainImagesGeneration());
    ensure("swapchain reset is refused transitively while Pending", !owner.resetSwapchainGeneration());
    ensure("configuration reset is refused transitively while Pending", !owner.resetSwapchainConfigurationGeneration());
    ensure("logical reset is refused transitively while Pending", !owner.resetLogicalDeviceGeneration());
    ensure("selection reset is refused transitively while Pending", !owner.resetPresentationDeviceGeneration());
    ensure("surface reset is refused transitively while Pending", !owner.resetSurfaceGeneration());
    ensure("full reset is refused transitively while Pending", !owner.reset());
    ensure("every refused reset retains the complete parent chain and invokes no destroy callback",
           owner.instance() == state.mInstance && owner.hasSurfaceGeneration() && owner.hasPresentationDeviceGeneration() &&
               owner.hasLogicalDeviceGeneration() && owner.hasSwapchainConfigurationGeneration() && owner.hasSwapchainGeneration() &&
               owner.hasSwapchainImagesGeneration() && owner.hasSwapchainFrameSlotGeneration() && state.mDestroyFenceCalls == 0 &&
               state.mDestroySemaphoreCalls == 0 && state.mDestroyCommandPoolCalls == 0 && state.mDestroyImageViewCalls == 0 &&
               state.mDestroySwapchainCalls == 0 && state.mDestroyDeviceCalls == 0 && state.mDestroySurfaceCalls == 0 &&
               state.mDestroyInstanceCalls == 0);

    const VulkanSwapchainFrameSlotParentOperationResult retry_result = owner.retryEmptySwapchainFrameSlotCompletion(request);
    const VulkanSwapchainFrameSlotParentOperationError& retry_error  = requireSwapchainFrameSlotOperationError(retry_result);
    ensure("a failed completion retry preserves both the exact error and Pending obligation",
           retry_error.mCode == VulkanSwapchainFrameSlotParentOperationCode::OperationFailure && retry_error.mOperationError &&
               retry_error.mOperationError->mCode == VulkanSwapchainFrameSlotOperationCode::CommandFailure &&
               retry_error.mOperationError->mCommand == VulkanSwapchainFrameSlotCommand::WaitForFences &&
               retry_error.mOperationError->mResult == VK_TIMEOUT &&
               retry_error.mOperationError->mDisposition == VulkanSwapchainFrameSlotDisposition::Pending &&
               owner.swapchainFrameSlotDisposition() == VulkanSwapchainFrameSlotDisposition::Pending);
    ensureSwapchainFrameSlotOperationSuccess(owner.retryEmptySwapchainFrameSlotCompletion(request),
                                             VulkanSwapchainFrameSlotDisposition::Reusable);
    ensure("successful retry unlocks child-first full teardown", owner.reset());
    ensure("unlocked teardown destroys every generation exactly once",
           state.mDestroyFenceCalls == 2 && state.mDestroySemaphoreCalls == 2 && state.mDestroyCommandPoolCalls == 1 &&
               state.mDestroyImageViewCalls == state.mSwapchainImageViews.size() && state.mDestroySwapchainCalls == 1 &&
               state.mDestroyDeviceCalls == 1 && state.mDestroySurfaceCalls == 1 && state.mDestroyInstanceCalls == 1);
}

template<>
template<>
void render_vulkan_instance_test_object::test<56>()
{
    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireSwapchainFrameSlotChain(state, owner);
    const VulkanSwapchainFrameSlotOperationRequest request = makeSwapchainFrameSlotOperationRequest(state, owner);
    state.mQueueSubmitResult                               = VK_ERROR_DEVICE_LOST;

    const VulkanSwapchainFrameSlotParentOperationResult submit_result = owner.roundTripEmptySwapchainFrameSlot(request);
    const VulkanSwapchainFrameSlotParentOperationError& submit_error  = requireSwapchainFrameSlotOperationError(submit_result);
    ensure("queue device loss retains a Pending completion obligation",
           submit_error.mCode == VulkanSwapchainFrameSlotParentOperationCode::OperationFailure && submit_error.mOperationError &&
               submit_error.mOperationError->mCode == VulkanSwapchainFrameSlotOperationCode::CommandFailure &&
               submit_error.mOperationError->mCommand == VulkanSwapchainFrameSlotCommand::QueueSubmit &&
               submit_error.mOperationError->mResult == VK_ERROR_DEVICE_LOST &&
               submit_error.mOperationError->mDisposition == VulkanSwapchainFrameSlotDisposition::Pending &&
               owner.swapchainFrameSlotDisposition() == VulkanSwapchainFrameSlotDisposition::Pending && !owner.resetSwapchainGeneration());

    ensureSwapchainFrameSlotOperationSuccess(owner.retryEmptySwapchainFrameSlotCompletion(request),
                                             VulkanSwapchainFrameSlotDisposition::DeviceLost);
    ensure("retiring queue loss unlocks explicit slot teardown without dropping older parents",
           owner.resetSwapchainFrameSlotGeneration() && !owner.hasSwapchainFrameSlotGeneration() && owner.hasSwapchainImagesGeneration() &&
               owner.hasSwapchainGeneration() && owner.hasLogicalDeviceGeneration() && owner.hasSurfaceGeneration() &&
               state.mDestroyFenceCalls == 2 && state.mDestroySemaphoreCalls == 2 && state.mDestroyCommandPoolCalls == 1);
}

template<>
template<>
void render_vulkan_instance_test_object::test<57>()
{
    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireSwapchainFrameSlotChain(state, owner);
    VulkanSwapchainFrameSlotOperationRequest request = makeSwapchainFrameSlotOperationRequest(state, owner);
    state.mWaitForFencesResults                      = { VK_SUCCESS, VK_TIMEOUT, VK_SUCCESS };
    requireSwapchainFrameSlotOperationError(owner.roundTripEmptySwapchainFrameSlot(request));
    ensure("the retry-authentication fixture is Pending",
           owner.swapchainFrameSlotDisposition() == VulkanSwapchainFrameSlotDisposition::Pending);
    const std::size_t wait_calls = state.mWaitForFencesCalls;

    request.mInstanceOwnerCheck = {};
    ensureSwapchainFrameSlotOperationCode(owner.retryEmptySwapchainFrameSlotCompletion(request),
                                          VulkanSwapchainFrameSlotParentOperationCode::InvalidInstanceOwnerCheck);
    request                        = makeSwapchainFrameSlotOperationRequest(state, owner);
    request.mWindowGenerationCheck = {};
    ensureSwapchainFrameSlotOperationCode(owner.retryEmptySwapchainFrameSlotCompletion(request),
                                          VulkanSwapchainFrameSlotParentOperationCode::InvalidWindowGenerationCheck);
    request                         = makeSwapchainFrameSlotOperationRequest(state, owner);
    request.mNativeWindowGeneration = 41;
    ensureSwapchainFrameSlotOperationCode(owner.retryEmptySwapchainFrameSlotCompletion(request),
                                          VulkanSwapchainFrameSlotParentOperationCode::NativeWindowGenerationMismatch);
    request                     = makeSwapchainFrameSlotOperationRequest(state, owner);
    state.mInstanceOwnerCurrent = false;
    ensureSwapchainFrameSlotOperationCode(owner.retryEmptySwapchainFrameSlotCompletion(request),
                                          VulkanSwapchainFrameSlotParentOperationCode::StaleInstanceOwner);
    state.mInstanceOwnerCurrent = true;
    state.mSurfaceWindowCurrent = false;
    ensureSwapchainFrameSlotOperationCode(owner.retryEmptySwapchainFrameSlotCompletion(request),
                                          VulkanSwapchainFrameSlotParentOperationCode::StaleWindowGeneration);
    state.mSurfaceWindowCurrent = true;
    ensure("every rejected retry authentication leaves Pending intact and invokes no retained completion wait",
           owner.swapchainFrameSlotDisposition() == VulkanSwapchainFrameSlotDisposition::Pending &&
               state.mWaitForFencesCalls == wait_calls);

    request                                                                = makeSwapchainFrameSlotOperationRequest(state, owner);
    const VulkanSwapchainFrameSlotParentOperationResult pending_result     = owner.roundTripEmptySwapchainFrameSlot(request);
    const VulkanSwapchainFrameSlotParentOperationError& pending_round_trip = requireSwapchainFrameSlotOperationError(pending_result);
    ensure("a normal round trip cannot bypass the Pending retry path",
           pending_round_trip.mCode == VulkanSwapchainFrameSlotParentOperationCode::OperationFailure &&
               pending_round_trip.mOperationError &&
               pending_round_trip.mOperationError->mCode == VulkanSwapchainFrameSlotOperationCode::InvalidDisposition &&
               pending_round_trip.mOperationError->mDisposition == VulkanSwapchainFrameSlotDisposition::Pending &&
               state.mWaitForFencesCalls == wait_calls);
    request.mDrawableExtent = { 0, 0 };
    ensureSwapchainFrameSlotOperationSuccess(owner.retryEmptySwapchainFrameSlotCompletion(request),
                                             VulkanSwapchainFrameSlotDisposition::Reusable);
    ensure("completion retry ignores current drawable geometry and consumes only retained state",
           state.mWaitForFencesCalls == wait_calls + 1);
}

template<>
template<>
void render_vulkan_instance_test_object::test<58>()
{
    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration first = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireSwapchainFrameSlotChain(state, first);
    ensureSwapchainFrameSlotOperationSuccess(first.roundTripEmptySwapchainFrameSlot(makeSwapchainFrameSlotOperationRequest(state, first)),
                                             VulkanSwapchainFrameSlotDisposition::Reusable);
    const std::size_t retained_lookup_count = state.mDeviceProcAddrCalls;

    VulkanInstanceGeneration moved(std::move(first));
    ensure("moving the parent transfers the reusable operation disposition",
           !first.swapchainFrameSlotDisposition() &&
               moved.swapchainFrameSlotDisposition() == VulkanSwapchainFrameSlotDisposition::Reusable);
    ensureSwapchainFrameSlotOperationCode(first.roundTripEmptySwapchainFrameSlot(makeSwapchainFrameSlotOperationRequest(state, first)),
                                          VulkanSwapchainFrameSlotParentOperationCode::InstanceNotLive);
    ensureSwapchainFrameSlotOperationSuccess(moved.roundTripEmptySwapchainFrameSlot(makeSwapchainFrameSlotOperationRequest(state, moved)),
                                             VulkanSwapchainFrameSlotDisposition::Reusable);
    ensure("the moved owner preserves retained dispatch without reallocating its frame slot",
           state.mDeviceProcAddrCalls == retained_lookup_count && state.mCreateCommandPoolCalls == 1 &&
               state.mAllocateCommandBufferCalls == 1 && state.mCreateSemaphoreCalls == 2 && state.mCreateFenceCalls == 2 &&
               state.mQueueSubmitCalls == 2);
}

template<>
template<>
void render_vulkan_instance_test_object::test<59>()
{
    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireSwapchainFrameSlotChain(state, owner, { 1280, 720 });
    VulkanSwapchainFrameSlotOperationRequest request = makeSwapchainFrameSlotOperationRequest(state, owner, { 1280, 720 });

    state.mAcquiredImageIndex                                             = 1;
    state.mQueuePresentResult                                             = VK_ERROR_OUT_OF_HOST_MEMORY;
    const VulkanSwapchainFrameSlotParentPresentationResult present_result = owner.acquireToPresentSwapchainFrameSlot(request);
    const VulkanSwapchainFrameSlotParentOperationError&    present_error  = requireSwapchainFrameSlotPresentationError(present_result);
    ensure("the parent retains exact presentation ownership after a retryable present failure",
           present_error.mCode == VulkanSwapchainFrameSlotParentOperationCode::OperationFailure && present_error.mOperationError &&
               present_error.mOperationError->mCommand == VulkanSwapchainFrameSlotCommand::QueuePresent &&
               present_error.mOperationError->mResult == VK_ERROR_OUT_OF_HOST_MEMORY &&
               present_error.mOperationError->mDisposition == VulkanSwapchainFrameSlotDisposition::PresentationReady &&
               present_error.mOperationError->mImageIndex == 1 &&
               owner.swapchainFrameSlotDisposition() == VulkanSwapchainFrameSlotDisposition::PresentationReady &&
               owner.swapchainFrameAcquiredImageIndex() == 1 && !owner.resetSwapchainFrameSlotGeneration());

    state.mQueuePresentResult                                            = VK_SUCCESS;
    request.mDrawableExtent                                              = { 0, 0 };
    const VulkanSwapchainFrameSlotParentPresentationResult retry_result  = owner.retrySwapchainFrameSlotPresentation(request);
    const VulkanSwapchainFrameSlotPresentationSuccess&     retry_success = requireSwapchainFrameSlotPresentationSuccess(retry_result);
    ensure("presentation retry ignores changed drawable geometry and completes retained work",
           retry_success.mOutcome == VulkanSwapchainFrameSlotPresentationOutcome::Presented && retry_success.mImageIndex == 1 &&
               owner.swapchainFrameSlotDisposition() == VulkanSwapchainFrameSlotDisposition::Reusable &&
               !owner.swapchainFrameAcquiredImageIndex());

    request                       = makeSwapchainFrameSlotOperationRequest(state, owner, { 1280, 720 });
    state.mAcquiredImageIndex     = 2;
    state.mEndCommandBufferResult = VK_ERROR_UNKNOWN;
    const VulkanSwapchainFrameSlotParentPresentationResult acquired_result = owner.acquireToPresentSwapchainFrameSlot(request);
    const VulkanSwapchainFrameSlotParentOperationError&    acquired_error  = requireSwapchainFrameSlotPresentationError(acquired_result);
    ensure("a post-acquire failure retains the exact image until cancellation",
           acquired_error.mOperationError &&
               acquired_error.mOperationError->mCommand == VulkanSwapchainFrameSlotCommand::EndCommandBuffer &&
               acquired_error.mOperationError->mDisposition == VulkanSwapchainFrameSlotDisposition::ImageAcquired &&
               acquired_error.mOperationError->mImageIndex == 2 && owner.swapchainFrameAcquiredImageIndex() == 2 && !owner.reset());

    state.mEndCommandBufferResult = VK_SUCCESS;
    request.mDrawableExtent       = { 17, 19 };
    ensureSwapchainFrameSlotOperationSuccess(owner.cancelSwapchainFrameSlotPresentation(request),
                                             VulkanSwapchainFrameSlotDisposition::Reusable);
    ensure("cancellation ignores changed drawable geometry and releases the retained image",
           state.mReleaseSwapchainImagesCalls == 1 && !owner.swapchainFrameAcquiredImageIndex() && owner.reset());
}

template<>
template<>
void render_vulkan_instance_test_object::test<60>()
{
    static_assert(std::variant_size_v<VulkanSwapchainChainRebuildResult> == 2);
    static_assert(std::variant_size_v<VulkanSwapchainChainRebuildChildError> == 8);
    static_assert(noexcept(
        std::declval<VulkanInstanceGeneration&>().rebuildSwapchainChain(std::declval<const VulkanSwapchainChainRebuildRequest&>())));

    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireCompleteSwapchainChain(state, owner);

    VulkanSwapchainChainRebuildRequest request = makeSwapchainChainRebuildRequest(state, owner);
    request.mInstanceOwnerCheck                = {};
    ensureSwapchainChainRebuildCode(owner.rebuildSwapchainChain(request),
                                    VulkanSwapchainChainRebuildCode::InvalidInstanceOwnerCheck,
                                    VulkanSwapchainChainRebuildPhase::Preflight);

    request                              = makeSwapchainChainRebuildRequest(state, owner);
    request.mWindowGenerationCheck       = {};
    ensureSwapchainChainRebuildCode(owner.rebuildSwapchainChain(request),
                                    VulkanSwapchainChainRebuildCode::InvalidWindowGenerationCheck,
                                    VulkanSwapchainChainRebuildPhase::Preflight);

    request                               = makeSwapchainChainRebuildRequest(state, owner);
    request.mNativeWindowGeneration       = 0;
    ensureSwapchainChainRebuildCode(owner.rebuildSwapchainChain(request),
                                    VulkanSwapchainChainRebuildCode::InvalidNativeWindowGeneration,
                                    VulkanSwapchainChainRebuildPhase::Preflight);

    request                 = makeSwapchainChainRebuildRequest(state, owner);
    request.mDrawableExtent = std::nullopt;
    ensureSwapchainChainRebuildCode(owner.rebuildSwapchainChain(request),
                                    VulkanSwapchainChainRebuildCode::InvalidDrawableExtent,
                                    VulkanSwapchainChainRebuildPhase::Preflight);

    request                         = makeSwapchainChainRebuildRequest(state, owner);
    request.mNativeWindowGeneration = 41;
    ensureSwapchainChainRebuildCode(owner.rebuildSwapchainChain(request),
                                    VulkanSwapchainChainRebuildCode::NativeWindowGenerationMismatch,
                                    VulkanSwapchainChainRebuildPhase::Preflight);

    request                     = makeSwapchainChainRebuildRequest(state, owner);
    state.mInstanceOwnerCurrent = false;
    ensureSwapchainChainRebuildCode(owner.rebuildSwapchainChain(request), VulkanSwapchainChainRebuildCode::StaleInstanceOwner,
                                    VulkanSwapchainChainRebuildPhase::Preflight);
    state.mInstanceOwnerCurrent = true;
    state.mSurfaceWindowCurrent = false;
    ensureSwapchainChainRebuildCode(owner.rebuildSwapchainChain(request), VulkanSwapchainChainRebuildCode::StaleWindowGeneration,
                                    VulkanSwapchainChainRebuildPhase::Preflight);
    state.mSurfaceWindowCurrent = true;

    ensure("preflight rejection leaves the complete chain untouched",
           owner.hasSwapchainConfigurationGeneration() && owner.hasSwapchainGeneration() && owner.hasSwapchainImagesGeneration() &&
               owner.hasSwapchainPresentationTargetGeneration() && owner.hasSwapchainPresentationPipelineGeneration() &&
               owner.hasSwapchainReadbackGeneration() && owner.hasSwapchainFrameSlotGeneration() && state.mUnmapMemoryCalls == 0 &&
               state.mDestroyBufferCalls == 0 && state.mFreeMemoryCalls == 0 && state.mDestroyFenceCalls == 0 &&
               state.mDestroySemaphoreCalls == 0 && state.mDestroyCommandPoolCalls == 0 && state.mDestroyImageViewCalls == 0 &&
               state.mDestroySwapchainCalls == 0);
}

template<>
template<>
void render_vulkan_instance_test_object::test<61>()
{
    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireCompleteSwapchainChain(state, owner);

    const VkSurfaceKHR surface = owner.surface();
    const VkDevice     device  = owner.logicalDevice();
    const VkQueue      queue   = owner.presentationQueue();
    state.mEvents.clear();

    ensureSwapchainChainRebuildOutcome(owner.rebuildSwapchainChain(makeSwapchainChainRebuildRequest(state, owner, VkExtent2D{ 1280, 720 })),
                                       VulkanSwapchainChainRebuildOutcome::Ready);
    ensure("changed-extent rebuild publishes a complete fresh chain over unchanged older parents",
           owner.surface() == surface && owner.logicalDevice() == device && owner.presentationQueue() == queue &&
               owner.swapchainDrawableExtent().width == 1280 && owner.swapchainDrawableExtent().height == 720 &&
               owner.hasSwapchainConfigurationGeneration() && owner.hasSwapchainGeneration() && owner.hasSwapchainImagesGeneration() &&
               owner.hasSwapchainPresentationTargetGeneration() && owner.hasSwapchainPresentationPipelineGeneration() &&
               owner.hasSwapchainReadbackGeneration() && owner.hasSwapchainFrameSlotGeneration() &&
               owner.swapchainReadbackBuffer() == state.mReadbackBuffer && owner.swapchainReadbackMemory() == state.mReadbackMemory &&
               owner.swapchainReadbackIsMapped() && owner.swapchainReadbackImageExtent().width == 1280 &&
               owner.swapchainReadbackImageExtent().height == 720 && owner.swapchainReadbackRowBytes() == 1280 * 4 &&
               owner.swapchainReadbackByteCount() == 1280 * 720 * 4);
    ensure("rebuild uses the initial-form null oldSwapchain contract",
           state.mCreateSwapchainCalls == 2 && state.mSwapchainCreateInfo.oldSwapchain == VK_NULL_HANDLE);
    ensure("the first rebuild retires each old child exactly once",
           state.mDestroyFenceCalls == 2 && state.mDestroySemaphoreCalls == 2 && state.mDestroyCommandPoolCalls == 1 &&
               state.mDestroyImageViewCalls == state.mSwapchainImageViews.size() && state.mDestroySwapchainCalls == 1);

    const auto first_command_pool_destroy = std::find(state.mEvents.begin(), state.mEvents.end(), Event::DestroyCommandPool);
    const auto first_unmap                = std::find(state.mEvents.begin(), state.mEvents.end(), Event::UnmapMemory);
    const auto first_buffer_destroy       = std::find(state.mEvents.begin(), state.mEvents.end(), Event::DestroyBuffer);
    const auto first_memory_free          = std::find(state.mEvents.begin(), state.mEvents.end(), Event::FreeMemory);
    const auto first_pipeline_destroy     = std::find(state.mEvents.begin(), state.mEvents.end(), Event::DestroyPipeline);
    const auto first_framebuffer_destroy  = std::find(state.mEvents.begin(), state.mEvents.end(), Event::DestroyFramebuffer);
    const auto first_view_destroy         = std::find(state.mEvents.begin(), state.mEvents.end(), Event::DestroyImageView);
    const auto swapchain_destroy          = std::find(state.mEvents.begin(), state.mEvents.end(), Event::DestroySwapchain);
    ensure("retirement is child-first before any replacement creation",
           first_command_pool_destroy != state.mEvents.end() && first_unmap != state.mEvents.end() &&
               first_buffer_destroy != state.mEvents.end() && first_memory_free != state.mEvents.end() &&
               first_pipeline_destroy != state.mEvents.end() && first_framebuffer_destroy != state.mEvents.end() &&
               first_view_destroy != state.mEvents.end() && swapchain_destroy != state.mEvents.end() &&
               first_command_pool_destroy < first_unmap && first_unmap < first_buffer_destroy && first_buffer_destroy < first_memory_free &&
               first_memory_free < first_pipeline_destroy && first_pipeline_destroy < first_framebuffer_destroy &&
               first_framebuffer_destroy < first_view_destroy && first_view_destroy < swapchain_destroy &&
               swapchain_destroy < std::find(state.mEvents.begin(), state.mEvents.end(), Event::CreateSwapchain));

    ensureSwapchainChainRebuildOutcome(owner.rebuildSwapchainChain(makeSwapchainChainRebuildRequest(state, owner, VkExtent2D{ 1280, 720 })),
                                       VulkanSwapchainChainRebuildOutcome::Ready);
    ensure("same-extent replacement refreshes capability provenance instead of reusing the prior children",
           state.mCreateSwapchainCalls == 3 && state.mDestroySwapchainCalls == 2 &&
               state.mSwapchainCapabilitiesCalls == 3 && state.mSwapchainCreateInfo.oldSwapchain == VK_NULL_HANDLE);
    ensureSwapchainFrameSlotOperationSuccess(
        owner.roundTripEmptySwapchainFrameSlot(makeSwapchainFrameSlotOperationRequest(state, owner, { 1280, 720 })),
        VulkanSwapchainFrameSlotDisposition::Reusable);
}

template<>
template<>
void render_vulkan_instance_test_object::test<62>()
{
    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireCompleteSwapchainChain(state, owner);

    const VkSurfaceKHR surface = owner.surface();
    const VkDevice     device  = owner.logicalDevice();
    const VkQueue      queue   = owner.presentationQueue();
    ensureSwapchainChainRebuildOutcome(owner.rebuildSwapchainChain(makeSwapchainChainRebuildRequest(state, owner, VkExtent2D{ 0, 0 })),
                                       VulkanSwapchainChainRebuildOutcome::Suspended);
    ensure("zero pixels suspend with only the stable older parents retained",
           owner.surface() == surface && owner.logicalDevice() == device && owner.presentationQueue() == queue &&
               !owner.hasSwapchainConfigurationGeneration() && !owner.hasSwapchainGeneration() && !owner.hasSwapchainImagesGeneration() &&
               !owner.hasSwapchainPresentationTargetGeneration() && !owner.hasSwapchainPresentationPipelineGeneration() &&
               !owner.hasSwapchainReadbackGeneration() && !owner.hasSwapchainFrameSlotGeneration());
    const std::size_t destroyed_swapchains = state.mDestroySwapchainCalls;

    ensureSwapchainChainRebuildOutcome(owner.rebuildSwapchainChain(makeSwapchainChainRebuildRequest(state, owner, VkExtent2D{ 0, 720 })),
                                       VulkanSwapchainChainRebuildOutcome::Suspended);
    ensure("repeated suspension is stable and does not destroy absent children",
           state.mDestroySwapchainCalls == destroyed_swapchains && !owner.hasSwapchainConfigurationGeneration());

    ensureSwapchainChainRebuildOutcome(owner.rebuildSwapchainChain(makeSwapchainChainRebuildRequest(state, owner, VkExtent2D{ 1024, 768 })),
                                       VulkanSwapchainChainRebuildOutcome::Ready);
    ensure("a nonzero restore reconstructs all seven children against the retained parents",
           owner.surface() == surface && owner.logicalDevice() == device && owner.presentationQueue() == queue &&
               owner.swapchainDrawableExtent().width == 1024 && owner.swapchainDrawableExtent().height == 768 &&
               owner.hasSwapchainConfigurationGeneration() && owner.hasSwapchainGeneration() && owner.hasSwapchainImagesGeneration() &&
               owner.hasSwapchainPresentationTargetGeneration() && owner.hasSwapchainPresentationPipelineGeneration() &&
               owner.hasSwapchainReadbackGeneration() && owner.hasSwapchainFrameSlotGeneration() &&
               owner.swapchainReadbackRowBytes() == 1024 * 4 && owner.swapchainReadbackByteCount() == 1024 * 768 * 4);
}

template<>
template<>
void render_vulkan_instance_test_object::test<63>()
{
    {
        FakeState                state;
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        acquireSwapchainFrameSlotChain(state, owner);
        const VulkanSwapchainFrameSlotOperationRequest operation = makeSwapchainFrameSlotOperationRequest(state, owner);
        state.mWaitForFencesResults                              = { VK_SUCCESS, VK_TIMEOUT, VK_SUCCESS };
        requireSwapchainFrameSlotOperationError(owner.roundTripEmptySwapchainFrameSlot(operation));

        const VulkanSwapchainChainRebuildError& error = requireSwapchainChainRebuildError(
            owner.rebuildSwapchainChain(makeSwapchainChainRebuildRequest(state, owner)));
        ensure("Pending refuses rebuild before any child mutation",
               error.mCode == VulkanSwapchainChainRebuildCode::FrameSlotResetRefused &&
                   error.mPhase == VulkanSwapchainChainRebuildPhase::Preflight &&
                   error.mFrameSlotDisposition == VulkanSwapchainFrameSlotDisposition::Pending &&
                   owner.hasSwapchainFrameSlotGeneration() && owner.hasSwapchainImagesGeneration() &&
                   owner.hasSwapchainGeneration() && state.mDestroyFenceCalls == 0 &&
                   state.mDestroyImageViewCalls == 0 && state.mDestroySwapchainCalls == 0);
        ensureSwapchainFrameSlotOperationSuccess(owner.retryEmptySwapchainFrameSlotCompletion(operation),
                                                 VulkanSwapchainFrameSlotDisposition::Reusable);
    }

    {
        FakeState                state;
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        acquireSwapchainFrameSlotChain(state, owner);
        const VulkanSwapchainFrameSlotOperationRequest operation = makeSwapchainFrameSlotOperationRequest(state, owner);
        state.mQueueSubmitResult                                = VK_ERROR_DEVICE_LOST;
        requireSwapchainFrameSlotOperationError(owner.roundTripEmptySwapchainFrameSlot(operation));
        ensureSwapchainFrameSlotOperationSuccess(owner.retryEmptySwapchainFrameSlotCompletion(operation),
                                                 VulkanSwapchainFrameSlotDisposition::DeviceLost);

        const VulkanSwapchainChainRebuildError& error = requireSwapchainChainRebuildError(
            owner.rebuildSwapchainChain(makeSwapchainChainRebuildRequest(state, owner)));
        ensure("DeviceLost remains a typed broader-recovery handoff without ordinary rebuild mutation",
               error.mCode == VulkanSwapchainChainRebuildCode::DeviceRecoveryRequired &&
                   error.mPhase == VulkanSwapchainChainRebuildPhase::Preflight &&
                   error.mFrameSlotDisposition == VulkanSwapchainFrameSlotDisposition::DeviceLost &&
                   owner.hasSwapchainFrameSlotGeneration() && state.mDestroyFenceCalls == 0 &&
                   state.mDestroyImageViewCalls == 0 && state.mDestroySwapchainCalls == 0);
        ensure("device-loss fixture can still use explicit broad teardown", owner.reset());
    }

    {
        FakeState                state;
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        acquireSwapchainFrameSlotChain(state, owner);
        state.mResetCommandBufferResult = VK_ERROR_UNKNOWN;
        const VulkanSwapchainFrameSlotParentOperationResult operation_result =
            owner.roundTripEmptySwapchainFrameSlot(makeSwapchainFrameSlotOperationRequest(state, owner));
        const VulkanSwapchainFrameSlotParentOperationError& operation_error = requireSwapchainFrameSlotOperationError(
            operation_result);
        ensure("the resettable recovery fixture reaches ResetRequired",
               operation_error.mOperationError &&
                   operation_error.mOperationError->mDisposition == VulkanSwapchainFrameSlotDisposition::ResetRequired &&
                   owner.swapchainFrameSlotDisposition() == VulkanSwapchainFrameSlotDisposition::ResetRequired);

        state.mResetCommandBufferResult = VK_SUCCESS;
        ensureSwapchainChainRebuildOutcome(
            owner.rebuildSwapchainChain(makeSwapchainChainRebuildRequest(state, owner, VkExtent2D{ 1280, 720 })),
            VulkanSwapchainChainRebuildOutcome::Ready);
        ensure("ResetRequired is safely retired and replaced as an ordinary resettable state",
               owner.hasSwapchainFrameSlotGeneration() &&
                   owner.swapchainFrameSlotDisposition() == VulkanSwapchainFrameSlotDisposition::Reusable &&
                   owner.swapchainDrawableExtent().width == 1280 && owner.swapchainDrawableExtent().height == 720);
    }
}

template<>
template<>
void render_vulkan_instance_test_object::test<64>()
{
    {
        FakeState                state;
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        acquireSwapchainFrameSlotChain(state, owner);
        state.mSwapchainCapabilitiesResult = VK_ERROR_SURFACE_LOST_KHR;
        const VulkanSwapchainChainRebuildError& error = requireSwapchainChainRebuildError(
            owner.rebuildSwapchainChain(makeSwapchainChainRebuildRequest(state, owner, VkExtent2D{ 1280, 720 })));
        const auto* child = std::get_if<VulkanSwapchainConfigurationAcquireError>(&error.mChildError);
        ensure("configuration failure retains its exact phase and child error on the parent-only baseline",
               error.mCode == VulkanSwapchainChainRebuildCode::ChildFailure &&
                   error.mPhase == VulkanSwapchainChainRebuildPhase::Configuration && child &&
                   child->mCode == VulkanSwapchainConfigurationAcquireCode::ResolutionFailure &&
                   !owner.hasSwapchainConfigurationGeneration() && !owner.hasSwapchainGeneration() &&
                   !owner.hasSwapchainImagesGeneration() && !owner.hasSwapchainPresentationTargetGeneration() &&
                   !owner.hasSwapchainPresentationPipelineGeneration() && !owner.hasSwapchainReadbackGeneration() &&
                   !owner.hasSwapchainFrameSlotGeneration() && owner.hasLogicalDeviceGeneration() && owner.hasSurfaceGeneration());
    }

    {
        FakeState                state;
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        acquireSwapchainFrameSlotChain(state, owner);
        state.mSwapchainCreateResult = VK_ERROR_OUT_OF_DATE_KHR;
        const VulkanSwapchainChainRebuildError& error = requireSwapchainChainRebuildError(
            owner.rebuildSwapchainChain(makeSwapchainChainRebuildRequest(state, owner, VkExtent2D{ 1280, 720 })));
        const auto* child = std::get_if<VulkanSwapchainAcquireError>(&error.mChildError);
        ensure("swapchain creation failure rolls the new configuration back with its exact error",
               error.mCode == VulkanSwapchainChainRebuildCode::ChildFailure &&
                   error.mPhase == VulkanSwapchainChainRebuildPhase::Swapchain && child &&
                   child->mCode == VulkanSwapchainAcquireCode::ResolutionFailure && !owner.hasSwapchainConfigurationGeneration() &&
                   !owner.hasSwapchainGeneration() && !owner.hasSwapchainImagesGeneration() &&
                   !owner.hasSwapchainPresentationTargetGeneration() && !owner.hasSwapchainPresentationPipelineGeneration() &&
                   !owner.hasSwapchainReadbackGeneration() && !owner.hasSwapchainFrameSlotGeneration() &&
                   owner.hasLogicalDeviceGeneration() && owner.hasSurfaceGeneration());
    }

    {
        FakeState                state;
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        acquireSwapchainFrameSlotChain(state, owner);
        state.mSwapchainImageCountResult = VK_ERROR_DEVICE_LOST;
        const VulkanSwapchainChainRebuildError& error = requireSwapchainChainRebuildError(
            owner.rebuildSwapchainChain(makeSwapchainChainRebuildRequest(state, owner, VkExtent2D{ 1280, 720 })));
        const auto* child = std::get_if<VulkanSwapchainImagesAcquireError>(&error.mChildError);
        ensure("image enumeration failure rolls every partial child back with its exact error",
               error.mCode == VulkanSwapchainChainRebuildCode::ChildFailure && error.mPhase == VulkanSwapchainChainRebuildPhase::Images &&
                   child && child->mCode == VulkanSwapchainImagesAcquireCode::ResolutionFailure &&
                   !owner.hasSwapchainConfigurationGeneration() && !owner.hasSwapchainGeneration() &&
                   !owner.hasSwapchainImagesGeneration() && !owner.hasSwapchainPresentationTargetGeneration() &&
                   !owner.hasSwapchainPresentationPipelineGeneration() && !owner.hasSwapchainReadbackGeneration() &&
                   !owner.hasSwapchainFrameSlotGeneration() && owner.hasLogicalDeviceGeneration() && owner.hasSurfaceGeneration());
    }

    {
        FakeState                state;
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        acquireSwapchainFrameSlotChain(state, owner);
        state.mFenceCreateResult = VK_ERROR_OUT_OF_HOST_MEMORY;
        const VulkanSwapchainChainRebuildError& error = requireSwapchainChainRebuildError(
            owner.rebuildSwapchainChain(makeSwapchainChainRebuildRequest(state, owner, VkExtent2D{ 1280, 720 })));
        const auto* child = std::get_if<VulkanSwapchainFrameSlotAcquireError>(&error.mChildError);
        ensure("frame-slot resource failure rolls the rebuilt prefix back with its exact error",
               error.mCode == VulkanSwapchainChainRebuildCode::ChildFailure &&
                   error.mPhase == VulkanSwapchainChainRebuildPhase::FrameSlot && child &&
                   child->mCode == VulkanSwapchainFrameSlotAcquireCode::ResolutionFailure && !owner.hasSwapchainConfigurationGeneration() &&
                   !owner.hasSwapchainGeneration() && !owner.hasSwapchainImagesGeneration() &&
                   !owner.hasSwapchainPresentationTargetGeneration() && !owner.hasSwapchainPresentationPipelineGeneration() &&
                   !owner.hasSwapchainReadbackGeneration() && !owner.hasSwapchainFrameSlotGeneration() &&
                   owner.hasLogicalDeviceGeneration() && owner.hasSurfaceGeneration());
    }
}

template<>
template<>
void render_vulkan_instance_test_object::test<65>()
{
    constexpr std::array phases{ VulkanSwapchainChainRebuildPhase::Configuration,
                                 VulkanSwapchainChainRebuildPhase::Swapchain,
                                 VulkanSwapchainChainRebuildPhase::Images,
                                 VulkanSwapchainChainRebuildPhase::PresentationTarget,
                                 VulkanSwapchainChainRebuildPhase::PresentationPipeline,
                                 VulkanSwapchainChainRebuildPhase::Readback,
                                 VulkanSwapchainChainRebuildPhase::FrameSlot };

    for (std::size_t failed_allocation = 1; failed_allocation <= phases.size(); ++failed_allocation)
    {
        FakeState                state;
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        acquireSwapchainFrameSlotChain(state, owner);
        const VkSurfaceKHR surface = owner.surface();
        const VkDevice     device  = owner.logicalDevice();
        const VkQueue      queue   = owner.presentationQueue();

        gAllocationCheckpointCalls = 0;
        gFailAllocationCheckpoint  = failed_allocation;
        const VulkanSwapchainChainRebuildError& error = requireSwapchainChainRebuildError(
            VulkanInstanceDetail::rebuildSwapchainChain(owner, makeSwapchainChainRebuildRequest(state, owner, VkExtent2D{ 1280, 720 }),
                                                        failSelectedAllocation));
        ensure("the selected parent allocation reports its exact child phase",
               error.mCode == VulkanSwapchainChainRebuildCode::ChildFailure &&
                   error.mPhase == phases[failed_allocation - 1]);

        bool exact_allocation_error = false;
        switch (failed_allocation)
        {
            case 1:
                if (const auto* child = std::get_if<VulkanSwapchainConfigurationAcquireError>(&error.mChildError))
                {
                    exact_allocation_error = child->mCode == VulkanSwapchainConfigurationAcquireCode::AllocationFailure;
                }
                break;
            case 2:
                if (const auto* child = std::get_if<VulkanSwapchainAcquireError>(&error.mChildError))
                {
                    exact_allocation_error = child->mCode == VulkanSwapchainAcquireCode::AllocationFailure;
                }
                break;
            case 3:
                if (const auto* child = std::get_if<VulkanSwapchainImagesAcquireError>(&error.mChildError))
                {
                    exact_allocation_error = child->mCode == VulkanSwapchainImagesAcquireCode::AllocationFailure;
                }
                break;
            case 4:
                if (const auto* child = std::get_if<VulkanSwapchainPresentationTargetAcquireError>(&error.mChildError))
                {
                    exact_allocation_error =
                        child->mCode == VulkanSwapchainPresentationTargetAcquireCode::AllocationFailure;
                }
                break;
            case 5:
                if (const auto* child = std::get_if<VulkanSwapchainPresentationPipelineAcquireError>(&error.mChildError))
                {
                    exact_allocation_error =
                        child->mCode == VulkanSwapchainPresentationPipelineAcquireCode::AllocationFailure;
                }
                break;
            case 6:
                if (const auto* child = std::get_if<VulkanSwapchainReadbackAcquireError>(&error.mChildError))
                {
                    exact_allocation_error = child->mCode == VulkanSwapchainReadbackAcquireCode::AllocationFailure;
                }
                break;
            case 7:
                if (const auto* child = std::get_if<VulkanSwapchainFrameSlotAcquireError>(&error.mChildError))
                {
                    exact_allocation_error = child->mCode == VulkanSwapchainFrameSlotAcquireCode::AllocationFailure;
                }
                break;
            default:
                break;
        }
        ensure("the exact nested allocation error is retained", exact_allocation_error);
        ensure("allocation rollback leaves no partial child and preserves every older parent",
               !owner.hasSwapchainConfigurationGeneration() && !owner.hasSwapchainGeneration() && !owner.hasSwapchainImagesGeneration() &&
                   !owner.hasSwapchainPresentationTargetGeneration() && !owner.hasSwapchainPresentationPipelineGeneration() &&
                   !owner.hasSwapchainReadbackGeneration() && !owner.hasSwapchainFrameSlotGeneration() && owner.surface() == surface &&
                   owner.logicalDevice() == device && owner.presentationQueue() == queue);
    }
    gAllocationCheckpointCalls = 0;
    gFailAllocationCheckpoint  = 0;
}

template<>
template<>
void render_vulkan_instance_test_object::test<66>()
{
    {
        FakeState                state;
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        acquireSwapchainFrameSlotChain(state, owner);
        const VkSurfaceKHR surface = owner.surface();
        const VkDevice     device  = owner.logicalDevice();
        const VkQueue      queue   = owner.presentationQueue();

        state.mInstanceOwnerChecks                    = 0;
        state.mSurfaceWindowChecks                    = 0;
        state.mFailInstanceOwnerCheck                 = 16;
        const VulkanSwapchainChainRebuildError& error = requireSwapchainChainRebuildError(
            owner.rebuildSwapchainChain(makeSwapchainChainRebuildRequest(state, owner, VkExtent2D{ 1280, 720 })));
        ensure("final owner staleness rolls a fully rebuilt chain back to the stable baseline",
               error.mCode == VulkanSwapchainChainRebuildCode::StaleInstanceOwner &&
                   error.mPhase == VulkanSwapchainChainRebuildPhase::FinalFreshness && !owner.hasSwapchainConfigurationGeneration() &&
                   !owner.hasSwapchainGeneration() && !owner.hasSwapchainImagesGeneration() &&
                   !owner.hasSwapchainPresentationTargetGeneration() && !owner.hasSwapchainPresentationPipelineGeneration() &&
                   !owner.hasSwapchainReadbackGeneration() && !owner.hasSwapchainFrameSlotGeneration() && owner.surface() == surface &&
                   owner.logicalDevice() == device && owner.presentationQueue() == queue);
    }

    {
        FakeState                state;
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        acquireSwapchainFrameSlotChain(state, owner);
        const VkSurfaceKHR surface = owner.surface();
        const VkDevice     device  = owner.logicalDevice();
        const VkQueue      queue   = owner.presentationQueue();

        state.mInstanceOwnerChecks                    = 0;
        state.mSurfaceWindowChecks                    = 0;
        state.mMutationOwner                          = &owner;
        state.mResetFrameSlotOnInstanceOwnerCheck     = 16;
        const VulkanSwapchainChainRebuildError& error = requireSwapchainChainRebuildError(
            owner.rebuildSwapchainChain(makeSwapchainChainRebuildRequest(state, owner, VkExtent2D{ 1280, 720 })));
        ensure("final publication detects replaced child provenance and removes the remaining prefix",
               error.mCode == VulkanSwapchainChainRebuildCode::PublicationFailure &&
                   error.mPhase == VulkanSwapchainChainRebuildPhase::FinalFreshness && !owner.hasSwapchainConfigurationGeneration() &&
                   !owner.hasSwapchainGeneration() && !owner.hasSwapchainImagesGeneration() &&
                   !owner.hasSwapchainPresentationTargetGeneration() && !owner.hasSwapchainPresentationPipelineGeneration() &&
                   !owner.hasSwapchainReadbackGeneration() && !owner.hasSwapchainFrameSlotGeneration() && owner.surface() == surface &&
                   owner.logicalDevice() == device && owner.presentationQueue() == queue);
    }
}

template<>
template<>
void render_vulkan_instance_test_object::test<67>()
{
    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireSwapchainFrameSlotChain(state, owner);
    const VkSurfaceKHR surface = owner.surface();
    const VkDevice     device  = owner.logicalDevice();
    const VkQueue      queue   = owner.presentationQueue();

    PostPublicationMutationContext mutation{ &state,
                                             &owner,
                                             { 1280, 720 },
                                             PostPublicationFrameSlotMutation::Pending,
                                             false,
                                             false,
                                             std::nullopt };
    const VulkanSwapchainChainRebuildError& error =
        requireSwapchainChainRebuildError(owner.rebuildSwapchainChain(makePostPublicationMutationRequest(mutation)));
    ensure("post-publication Pending replaces stale with the exact rollback refusal",
           mutation.mMutated && mutation.mObservedDisposition == VulkanSwapchainFrameSlotDisposition::Pending &&
               error.mCode == VulkanSwapchainChainRebuildCode::FrameSlotResetRefused &&
               error.mPhase == VulkanSwapchainChainRebuildPhase::FinalFreshness &&
               error.mFrameSlotDisposition == VulkanSwapchainFrameSlotDisposition::Pending &&
               std::holds_alternative<std::monostate>(error.mChildError));
    ensure("a refused final rollback retains one complete authentic replacement chain",
           owner.hasSwapchainConfigurationGeneration() && owner.hasSwapchainGeneration() &&
               owner.hasSwapchainImagesGeneration() && owner.hasSwapchainFrameSlotGeneration() &&
               owner.swapchainDrawableExtent().width == 1280 && owner.swapchainDrawableExtent().height == 720 &&
               owner.swapchainFrameSlotDisposition() == VulkanSwapchainFrameSlotDisposition::Pending &&
               owner.surface() == surface && owner.logicalDevice() == device && owner.presentationQueue() == queue);
    ensure("the rebuild transaction destroys only the retired child set before refusing Pending rollback",
           state.mCreateSwapchainCalls == 2 && state.mDestroySwapchainCalls == 1 &&
               state.mDestroyImageViewCalls == state.mSwapchainImageViews.size() && state.mDestroyCommandPoolCalls == 1 &&
               state.mDestroySemaphoreCalls == 2 && state.mDestroyFenceCalls == 2);

    ensureSwapchainFrameSlotOperationSuccess(
        owner.retryEmptySwapchainFrameSlotCompletion(makeExactFrameSlotOperationRequest(owner)),
        VulkanSwapchainFrameSlotDisposition::Reusable);
    ensure("the retained Pending replacement remains retryable and then permits explicit broad teardown", owner.reset());
}

template<>
template<>
void render_vulkan_instance_test_object::test<68>()
{
    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireSwapchainFrameSlotChain(state, owner);
    const VkSurfaceKHR surface = owner.surface();
    const VkDevice     device  = owner.logicalDevice();
    const VkQueue      queue   = owner.presentationQueue();

    PostPublicationMutationContext mutation{ &state,
                                             &owner,
                                             { 1280, 720 },
                                             PostPublicationFrameSlotMutation::DeviceLost,
                                             true,
                                             false,
                                             std::nullopt };
    const VulkanSwapchainChainRebuildError& error =
        requireSwapchainChainRebuildError(owner.rebuildSwapchainChain(makePostPublicationMutationRequest(mutation)));
    ensure("post-publication DeviceLost is an exact broader-recovery handoff even when freshness remains current",
           mutation.mMutated && mutation.mObservedDisposition == VulkanSwapchainFrameSlotDisposition::DeviceLost &&
               error.mCode == VulkanSwapchainChainRebuildCode::DeviceRecoveryRequired &&
               error.mPhase == VulkanSwapchainChainRebuildPhase::FinalFreshness &&
               error.mFrameSlotDisposition == VulkanSwapchainFrameSlotDisposition::DeviceLost &&
               std::holds_alternative<std::monostate>(error.mChildError));
    ensure("the DeviceLost handoff retains the complete replacement chain and every stable older parent",
           owner.hasSwapchainConfigurationGeneration() && owner.hasSwapchainGeneration() &&
               owner.hasSwapchainImagesGeneration() && owner.hasSwapchainFrameSlotGeneration() &&
               owner.swapchainDrawableExtent().width == 1280 && owner.swapchainDrawableExtent().height == 720 &&
               owner.swapchainFrameSlotDisposition() == VulkanSwapchainFrameSlotDisposition::DeviceLost &&
               owner.surface() == surface && owner.logicalDevice() == device && owner.presentationQueue() == queue);
    ensure("the rebuild transaction never destroys the DeviceLost replacement through its rollback path",
           state.mCreateSwapchainCalls == 2 && state.mDestroySwapchainCalls == 1 &&
               state.mDestroyImageViewCalls == state.mSwapchainImageViews.size() && state.mDestroyCommandPoolCalls == 1 &&
               state.mDestroySemaphoreCalls == 2 && state.mDestroyFenceCalls == 2 && state.mWaitForFencesCalls == 1 &&
               state.mQueueSubmitCalls == 0);

    ensure("the explicit broader-recovery owner path remains available after the typed handoff", owner.reset());
}

template<>
template<>
void render_vulkan_instance_test_object::test<69>()
{
    struct Scenario
    {
        PostPublicationFrameSlotMutation mMutation;
        std::size_t                      mPublishOnOwnerCheck;
        VkSwapchainKHR                   mReplacementSwapchain;
        VulkanSwapchainChainRebuildCode  mExpectedCode;
        VulkanSwapchainFrameSlotDisposition mExpectedDisposition;
        std::size_t                         mExpectedCreateCalls;
        std::size_t                         mExpectedDestroyCalls;
    };

    const std::array scenarios{
        Scenario{ PostPublicationFrameSlotMutation::Pending,
                  4,
                  fakeHandle<VkSwapchainKHR>(0x7788),
                  VulkanSwapchainChainRebuildCode::FrameSlotResetRefused,
                  VulkanSwapchainFrameSlotDisposition::Pending,
                  2,
                  1 },
        Scenario{ PostPublicationFrameSlotMutation::DeviceLost,
                  5,
                  fakeHandle<VkSwapchainKHR>(0x7799),
                  VulkanSwapchainChainRebuildCode::DeviceRecoveryRequired,
                  VulkanSwapchainFrameSlotDisposition::DeviceLost,
                  3,
                  2 }
    };

    for (const Scenario& scenario : scenarios)
    {
        FakeState       state;
        ScopedFakeState scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        acquireSwapchainFrameSlotChain(state, owner);
        const VkSwapchainKHR initial_swapchain = owner.swapchain();
        const VkSurfaceKHR   surface           = owner.surface();
        const VkDevice       device            = owner.logicalDevice();
        const VkQueue        queue             = owner.presentationQueue();

        ReentrantSwapchainPublicationContext publication{ &state,
                                                          &owner,
                                                          { 1280, 720 },
                                                          scenario.mMutation,
                                                          scenario.mPublishOnOwnerCheck,
                                                          0,
                                                          scenario.mReplacementSwapchain };
        const VulkanSwapchainChainRebuildError& error = requireSwapchainChainRebuildError(
            owner.rebuildSwapchainChain(makeReentrantSwapchainPublicationRequest(publication)));
        const auto* child = std::get_if<VulkanSwapchainAcquireError>(&error.mChildError);

        ensure("the guarded callback publishes on the selected child freshness boundary",
               publication.mAttempted && publication.mPublished &&
                   publication.mOwnerChecks == scenario.mPublishOnOwnerCheck &&
                   publication.mPublishedSwapchain == scenario.mReplacementSwapchain);
        ensure("reentrant publication retains exact refusal and nested child provenance",
               error.mCode == scenario.mExpectedCode && error.mPhase == VulkanSwapchainChainRebuildPhase::Swapchain &&
                   error.mFrameSlotDisposition == scenario.mExpectedDisposition && child &&
                   child->mCode == VulkanSwapchainAcquireCode::SwapchainAlreadyOwned);
        ensure("the complete callback-published chain remains authentic and attached to every stable parent",
               owner.hasSwapchainConfigurationGeneration() && owner.hasSwapchainGeneration() &&
                   owner.hasSwapchainImagesGeneration() && owner.hasSwapchainFrameSlotGeneration() &&
                   owner.swapchain() == scenario.mReplacementSwapchain &&
                   owner.swapchainDrawableExtent().width == 1280 && owner.swapchainDrawableExtent().height == 720 &&
                   owner.swapchainFrameSlotDisposition() == scenario.mExpectedDisposition && owner.surface() == surface &&
                   owner.logicalDevice() == device && owner.presentationQueue() == queue);
        ensure("the referenced callback-published swapchain is never overwritten or destroyed",
               initial_swapchain != scenario.mReplacementSwapchain &&
                   state.mCreateSwapchainCalls == scenario.mExpectedCreateCalls &&
                   state.mDestroySwapchainCalls == scenario.mExpectedDestroyCalls &&
                   std::find(state.mDestroyedSwapchains.begin(),
                             state.mDestroyedSwapchains.end(),
                             scenario.mReplacementSwapchain) == state.mDestroyedSwapchains.end());

        if (scenario.mExpectedDisposition == VulkanSwapchainFrameSlotDisposition::Pending)
        {
            ensureSwapchainFrameSlotOperationSuccess(
                owner.retryEmptySwapchainFrameSlotCompletion(makeExactFrameSlotOperationRequest(owner)),
                VulkanSwapchainFrameSlotDisposition::Reusable);
        }
        ensure("the retained callback-published chain permits its explicit recovery path", owner.reset());
    }
}

template<>
template<>
void render_vulkan_instance_test_object::test<70>()
{
    struct Scenario
    {
        PostPublicationFrameSlotMutation    mMutation;
        std::size_t                         mPublishOnOwnerCheck;
        VkSwapchainKHR                      mReplacementSwapchain;
        VulkanSwapchainChainRebuildCode     mExpectedCode;
        VulkanSwapchainFrameSlotDisposition mExpectedDisposition;
        std::size_t                         mExpectedCapabilityCalls;
    };

    const std::array scenarios{
        Scenario{ PostPublicationFrameSlotMutation::Pending,
                  2,
                  fakeHandle<VkSwapchainKHR>(0x77aa),
                  VulkanSwapchainChainRebuildCode::FrameSlotResetRefused,
                  VulkanSwapchainFrameSlotDisposition::Pending,
                  2 },
        Scenario{ PostPublicationFrameSlotMutation::DeviceLost,
                  3,
                  fakeHandle<VkSwapchainKHR>(0x77bb),
                  VulkanSwapchainChainRebuildCode::DeviceRecoveryRequired,
                  VulkanSwapchainFrameSlotDisposition::DeviceLost,
                  3 }
    };

    for (const Scenario& scenario : scenarios)
    {
        FakeState       state;
        ScopedFakeState scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        acquireSwapchainFrameSlotChain(state, owner);
        const VkSwapchainKHR initial_swapchain = owner.swapchain();
        const VkSurfaceKHR   surface           = owner.surface();
        const VkDevice       device            = owner.logicalDevice();
        const VkQueue        queue             = owner.presentationQueue();

        ReentrantConfigurationPublicationContext publication{ &state,
                                                              &owner,
                                                              { 1280, 720 },
                                                              scenario.mMutation,
                                                              scenario.mPublishOnOwnerCheck,
                                                              0,
                                                              scenario.mReplacementSwapchain,
                                                              VK_FORMAT_R8G8B8A8_UNORM };
        const VulkanSwapchainChainRebuildError& error = requireSwapchainChainRebuildError(
            owner.rebuildSwapchainChain(makeReentrantConfigurationPublicationRequest(publication)));
        const auto* child = std::get_if<VulkanSwapchainConfigurationAcquireError>(&error.mChildError);

        ensure("the guarded callback publishes on the selected configuration freshness boundary",
               publication.mAttempted && publication.mPublished &&
                   publication.mOwnerChecks == scenario.mPublishOnOwnerCheck &&
                   publication.mObservedDisposition == scenario.mExpectedDisposition);
        ensure("configuration reentrant publication retains exact refusal and nested child provenance",
               error.mCode == scenario.mExpectedCode &&
                   error.mPhase == VulkanSwapchainChainRebuildPhase::Configuration &&
                   error.mFrameSlotDisposition == scenario.mExpectedDisposition && child &&
                   child->mCode == VulkanSwapchainConfigurationAcquireCode::SwapchainConfigurationAlreadyOwned);
        ensure("the callback-published configuration and complete chain retain one matching identity",
               owner.hasSwapchainConfigurationGeneration() && owner.hasSwapchainGeneration() &&
                   owner.hasSwapchainImagesGeneration() && owner.hasSwapchainFrameSlotGeneration() &&
                   owner.swapchain() == scenario.mReplacementSwapchain &&
                   owner.swapchainSurfaceFormat().format == VK_FORMAT_R8G8B8A8_UNORM &&
                   owner.swapchainDrawableExtent().width == 1280 && owner.swapchainDrawableExtent().height == 720 &&
                   owner.swapchainFrameSlotDisposition() == scenario.mExpectedDisposition && owner.surface() == surface &&
                   owner.logicalDevice() == device && owner.presentationQueue() == queue);
        ensure("no stale configuration resolution overwrites or destroys the callback-published chain",
               initial_swapchain != scenario.mReplacementSwapchain && state.mCreateSwapchainCalls == 2 &&
                   state.mDestroySwapchainCalls == 1 &&
                   state.mSwapchainCapabilitiesCalls == scenario.mExpectedCapabilityCalls &&
                   std::find(state.mDestroyedSwapchains.begin(),
                             state.mDestroyedSwapchains.end(),
                             scenario.mReplacementSwapchain) == state.mDestroyedSwapchains.end());

        if (scenario.mExpectedDisposition == VulkanSwapchainFrameSlotDisposition::Pending)
        {
            ensureSwapchainFrameSlotOperationSuccess(
                owner.retryEmptySwapchainFrameSlotCompletion(makeExactFrameSlotOperationRequest(owner)),
                VulkanSwapchainFrameSlotDisposition::Reusable);
        }
        ensure("the retained configuration publication permits its explicit recovery path", owner.reset());
    }
}

template<>
template<>
void render_vulkan_instance_test_object::test<71>()
{
    struct Scenario
    {
        ReentrantLeafPublicationPath         mPath;
        PostPublicationFrameSlotMutation     mMutation;
        VulkanSwapchainChainRebuildCode      mExpectedCode;
        VulkanSwapchainChainRebuildPhase     mExpectedPhase;
        VulkanSwapchainFrameSlotDisposition  mExpectedDisposition;
        std::uintptr_t                       mHandleBase;
    };

    const std::array scenarios{
        Scenario{ ReentrantLeafPublicationPath::Images,
                  PostPublicationFrameSlotMutation::Pending,
                  VulkanSwapchainChainRebuildCode::FrameSlotResetRefused,
                  VulkanSwapchainChainRebuildPhase::Images,
                  VulkanSwapchainFrameSlotDisposition::Pending,
                  0xb100 },
        Scenario{ ReentrantLeafPublicationPath::FrameSlot,
                  PostPublicationFrameSlotMutation::DeviceLost,
                  VulkanSwapchainChainRebuildCode::DeviceRecoveryRequired,
                  VulkanSwapchainChainRebuildPhase::FrameSlot,
                  VulkanSwapchainFrameSlotDisposition::DeviceLost,
                  0xb200 }
    };

    for (const Scenario& scenario : scenarios)
    {
        FakeState       state;
        ScopedFakeState scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        acquireSwapchainFrameSlotChain(state, owner);
        const VkSurfaceKHR surface = owner.surface();
        const VkDevice     device  = owner.logicalDevice();
        const VkQueue      queue   = owner.presentationQueue();

        const std::array replacement_images{
            fakeHandle<VkImage>(scenario.mHandleBase + 1),
            fakeHandle<VkImage>(scenario.mHandleBase + 2),
            fakeHandle<VkImage>(scenario.mHandleBase + 3)
        };
        const std::array replacement_views{
            fakeHandle<VkImageView>(scenario.mHandleBase + 4),
            fakeHandle<VkImageView>(scenario.mHandleBase + 5),
            fakeHandle<VkImageView>(scenario.mHandleBase + 6)
        };
        state.mSwapchainImages.assign(replacement_images.begin(), replacement_images.end());
        state.mSwapchainImageViews.assign(replacement_views.begin(), replacement_views.end());

        ReentrantLeafPublicationContext publication;
        publication.mState                        = &state;
        publication.mOwner                        = &owner;
        publication.mTargetExtent                 = { 1280, 720 };
        publication.mPath                         = scenario.mPath;
        publication.mMutation                     = scenario.mMutation;
        publication.mCommandPool                  = fakeHandle<VkCommandPool>(scenario.mHandleBase + 7);
        publication.mCommandBuffer                = fakeHandle<VkCommandBuffer>(scenario.mHandleBase + 8);
        publication.mImageAvailableSemaphore      = fakeHandle<VkSemaphore>(scenario.mHandleBase + 9);
        publication.mPresentationReadySemaphore   = fakeHandle<VkSemaphore>(scenario.mHandleBase + 10);
        publication.mSubmissionFence              = fakeHandle<VkFence>(scenario.mHandleBase + 11);
        publication.mPresentCompletionFence       = fakeHandle<VkFence>(scenario.mHandleBase + 12);

        const VulkanSwapchainChainRebuildError& error = requireSwapchainChainRebuildError(
            owner.rebuildSwapchainChain(makeReentrantLeafPublicationRequest(publication)));
        bool exact_child_error = false;
        if (scenario.mPath == ReentrantLeafPublicationPath::Images)
        {
            const auto* child = std::get_if<VulkanSwapchainImagesAcquireError>(&error.mChildError);
            exact_child_error = child && child->mCode == VulkanSwapchainImagesAcquireCode::SwapchainImagesAlreadyOwned;
        }
        else
        {
            const auto* child = std::get_if<VulkanSwapchainFrameSlotAcquireError>(&error.mChildError);
            exact_child_error = child &&
                                child->mCode == VulkanSwapchainFrameSlotAcquireCode::SwapchainFrameSlotAlreadyOwned;
        }

        bool exact_images = owner.resolvedSwapchainImageCount() == replacement_images.size();
        for (std::size_t index = 0; exact_images && index < replacement_images.size(); ++index)
        {
            exact_images = owner.swapchainImage(static_cast<std::uint32_t>(index)) == replacement_images[index] &&
                           owner.swapchainImageView(static_cast<std::uint32_t>(index)) == replacement_views[index];
        }
        const auto was_destroyed = [](const auto& destroyed, auto handle)
        {
            return std::find(destroyed.begin(), destroyed.end(), handle) != destroyed.end();
        };
        const bool published_view_destroyed = std::any_of(
            replacement_views.begin(), replacement_views.end(), [&](VkImageView view)
            {
                return was_destroyed(state.mDestroyedImageViews, view);
            });

        ensure("the selected first leaf freshness callback publishes a complete replacement chain",
               publication.mAttempted && publication.mPublished &&
                   publication.mObservedDisposition == scenario.mExpectedDisposition);
        ensure("leaf reentrant publication retains its causal phase and exact nested AlreadyOwned error",
               error.mCode == scenario.mExpectedCode && error.mPhase == scenario.mExpectedPhase &&
                   error.mFrameSlotDisposition == scenario.mExpectedDisposition && exact_child_error);
        ensure("the callback-published images and frame slot retain one matching identity",
               owner.hasSwapchainConfigurationGeneration() && owner.hasSwapchainGeneration() &&
                   owner.hasSwapchainImagesGeneration() && owner.hasSwapchainFrameSlotGeneration() && exact_images &&
                   owner.swapchainFrameCommandPool() == publication.mCommandPool &&
                   owner.swapchainFrameCommandBuffer() == publication.mCommandBuffer &&
                   owner.swapchainFrameImageAvailableSemaphore() == publication.mImageAvailableSemaphore &&
                   owner.swapchainFramePresentationReadySemaphore() == publication.mPresentationReadySemaphore &&
                   owner.swapchainFrameSubmissionFence() == publication.mSubmissionFence &&
                   owner.swapchainFramePresentCompletionFence() == publication.mPresentCompletionFence &&
                   owner.swapchainFrameSlotDisposition() == scenario.mExpectedDisposition && owner.surface() == surface &&
                   owner.logicalDevice() == device && owner.presentationQueue() == queue);
        ensure("first-boundary ownership refusal performs no redundant native resolution or allocation",
               state.mSwapchainImageCountCalls == 2 && state.mSwapchainImageListCalls == 2 &&
                   state.mCreateImageViewCalls == 2 * replacement_views.size() &&
                   state.mCreateCommandPoolCalls == 2 && state.mAllocateCommandBufferCalls == 2 &&
                   state.mCreateSemaphoreCalls == 4 && state.mCreateFenceCalls == 4);
        ensure("no native object referenced by the callback-published chain is destroyed",
               state.mDestroyImageViewCalls == replacement_views.size() && !published_view_destroyed &&
                   state.mDestroyCommandPoolCalls == 1 &&
                   !was_destroyed(state.mDestroyedCommandPools, publication.mCommandPool) &&
                   state.mDestroySemaphoreCalls == 2 &&
                   !was_destroyed(state.mDestroyedSemaphores, publication.mImageAvailableSemaphore) &&
                   !was_destroyed(state.mDestroyedSemaphores, publication.mPresentationReadySemaphore) &&
                   state.mDestroyFenceCalls == 2 &&
                   !was_destroyed(state.mDestroyedFences, publication.mSubmissionFence) &&
                   !was_destroyed(state.mDestroyedFences, publication.mPresentCompletionFence));

        if (scenario.mExpectedDisposition == VulkanSwapchainFrameSlotDisposition::Pending)
        {
            ensureSwapchainFrameSlotOperationSuccess(
                owner.retryEmptySwapchainFrameSlotCompletion(makeExactFrameSlotOperationRequest(owner)),
                VulkanSwapchainFrameSlotDisposition::Reusable);
        }
        ensure("the retained leaf publication permits its explicit recovery path", owner.reset());
    }
}

template<>
template<>
void render_vulkan_instance_test_object::test<72>()
{
    struct Scenario
    {
        PreflightMutationCallback               mCallback;
        PreflightMutationAction                 mAction;
        PostPublicationFrameSlotMutation        mMutation;
        bool                                    mCurrentAfterMutation;
        VulkanSwapchainChainRebuildCode         mExpectedCode;
        VulkanSwapchainFrameSlotDisposition     mExpectedDisposition;
        std::size_t                             mExpectedWindowChecks;
        std::uintptr_t                          mHandleBase;
    };

    const std::array scenarios{
        Scenario{ PreflightMutationCallback::Owner,
                  PreflightMutationAction::OperateExisting,
                  PostPublicationFrameSlotMutation::Pending,
                  false,
                  VulkanSwapchainChainRebuildCode::FrameSlotResetRefused,
                  VulkanSwapchainFrameSlotDisposition::Pending,
                  0,
                  0xc100 },
        Scenario{ PreflightMutationCallback::Window,
                  PreflightMutationAction::OperateExisting,
                  PostPublicationFrameSlotMutation::DeviceLost,
                  false,
                  VulkanSwapchainChainRebuildCode::DeviceRecoveryRequired,
                  VulkanSwapchainFrameSlotDisposition::DeviceLost,
                  1,
                  0xc200 },
        Scenario{ PreflightMutationCallback::Owner,
                  PreflightMutationAction::ReplaceChildren,
                  PostPublicationFrameSlotMutation::Pending,
                  true,
                  VulkanSwapchainChainRebuildCode::FrameSlotResetRefused,
                  VulkanSwapchainFrameSlotDisposition::Pending,
                  0,
                  0xc300 }
    };

    for (const Scenario& scenario : scenarios)
    {
        FakeState       state;
        ScopedFakeState scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        acquireSwapchainFrameSlotChain(state, owner);
        const VkSurfaceKHR surface = owner.surface();
        const VkDevice     device  = owner.logicalDevice();
        const VkQueue      queue   = owner.presentationQueue();

        std::vector<VkImage> expected_images;
        std::vector<VkImageView> expected_views;
        for (std::uint32_t index = 0; index < owner.resolvedSwapchainImageCount(); ++index)
        {
            expected_images.push_back(owner.swapchainImage(index));
            expected_views.push_back(owner.swapchainImageView(index));
        }
        VkSwapchainKHR expected_swapchain = owner.swapchain();
        VkCommandPool  expected_command_pool = owner.swapchainFrameCommandPool();
        VkCommandBuffer expected_command_buffer = owner.swapchainFrameCommandBuffer();
        VkSemaphore expected_image_available = owner.swapchainFrameImageAvailableSemaphore();
        VkSemaphore expected_presentation_ready = owner.swapchainFramePresentationReadySemaphore();
        VkFence expected_submission_fence = owner.swapchainFrameSubmissionFence();
        VkFence expected_present_completion_fence = owner.swapchainFramePresentCompletionFence();

        PreflightMutationContext mutation;
        mutation.mState                                 = &state;
        mutation.mOwner                                 = &owner;
        mutation.mReplacementExtent                     = { 1280, 720 };
        mutation.mCallback                              = scenario.mCallback;
        mutation.mAction                                = scenario.mAction;
        mutation.mMutation                              = scenario.mMutation;
        mutation.mCurrentAfterMutation                  = scenario.mCurrentAfterMutation;
        mutation.mReplacementSwapchain                  = fakeHandle<VkSwapchainKHR>(scenario.mHandleBase + 1);
        mutation.mReplacementCommandPool                = fakeHandle<VkCommandPool>(scenario.mHandleBase + 2);
        mutation.mReplacementCommandBuffer              = fakeHandle<VkCommandBuffer>(scenario.mHandleBase + 3);
        mutation.mReplacementImageAvailableSemaphore    = fakeHandle<VkSemaphore>(scenario.mHandleBase + 4);
        mutation.mReplacementPresentationReadySemaphore = fakeHandle<VkSemaphore>(scenario.mHandleBase + 5);
        mutation.mReplacementSubmissionFence            = fakeHandle<VkFence>(scenario.mHandleBase + 6);
        mutation.mReplacementPresentCompletionFence     = fakeHandle<VkFence>(scenario.mHandleBase + 7);

        if (scenario.mAction == PreflightMutationAction::ReplaceChildren)
        {
            expected_images = { fakeHandle<VkImage>(scenario.mHandleBase + 8),
                                fakeHandle<VkImage>(scenario.mHandleBase + 9),
                                fakeHandle<VkImage>(scenario.mHandleBase + 10) };
            expected_views = { fakeHandle<VkImageView>(scenario.mHandleBase + 11),
                               fakeHandle<VkImageView>(scenario.mHandleBase + 12),
                               fakeHandle<VkImageView>(scenario.mHandleBase + 13) };
            state.mSwapchainImages     = expected_images;
            state.mSwapchainImageViews = expected_views;
            expected_swapchain             = mutation.mReplacementSwapchain;
            expected_command_pool           = mutation.mReplacementCommandPool;
            expected_command_buffer         = mutation.mReplacementCommandBuffer;
            expected_image_available        = mutation.mReplacementImageAvailableSemaphore;
            expected_presentation_ready     = mutation.mReplacementPresentationReadySemaphore;
            expected_submission_fence       = mutation.mReplacementSubmissionFence;
            expected_present_completion_fence = mutation.mReplacementPresentCompletionFence;
        }

        const VulkanSwapchainChainRebuildError& error = requireSwapchainChainRebuildError(
            owner.rebuildSwapchainChain(makePreflightMutationRequest(mutation)));
        bool exact_images = owner.resolvedSwapchainImageCount() == expected_images.size();
        for (std::size_t index = 0; exact_images && index < expected_images.size(); ++index)
        {
            exact_images = owner.swapchainImage(static_cast<std::uint32_t>(index)) == expected_images[index] &&
                           owner.swapchainImageView(static_cast<std::uint32_t>(index)) == expected_views[index];
        }
        const auto was_destroyed = [](const auto& destroyed, auto handle)
        {
            return std::find(destroyed.begin(), destroyed.end(), handle) != destroyed.end();
        };
        const bool retained_view_destroyed = std::any_of(expected_views.begin(), expected_views.end(), [&](VkImageView view)
        {
            return was_destroyed(state.mDestroyedImageViews, view);
        });
        const bool replacement_expected = scenario.mAction == PreflightMutationAction::ReplaceChildren;

        ensure("the selected preflight callback runs", mutation.mMutated);
        ensure("the selected preflight callback publishes its expected chain", mutation.mPublished);
        ensure("a refused preflight stops after the expected callbacks",
               mutation.mOwnerChecks == 1 && mutation.mWindowChecks == scenario.mExpectedWindowChecks);
        ensure("preflight replacement matches the selected action", mutation.mReplaced == replacement_expected);
        ensure("the preflight callback retains its expected swapchain",
               mutation.mPublishedSwapchain == expected_swapchain);
        ensure("the preflight callback leaves the selected frame-slot disposition",
               mutation.mObservedDisposition == scenario.mExpectedDisposition);
        ensure("preflight refusal takes precedence over stale or child-pointer publication provenance",
               error.mCode == scenario.mExpectedCode && error.mPhase == VulkanSwapchainChainRebuildPhase::Preflight &&
                   error.mFrameSlotDisposition == scenario.mExpectedDisposition &&
                   std::holds_alternative<std::monostate>(error.mChildError));
        ensure("the authentic callback-selected chain remains attached to every stable parent",
               owner.hasSwapchainConfigurationGeneration() && owner.hasSwapchainGeneration() &&
                   owner.hasSwapchainImagesGeneration() && owner.hasSwapchainFrameSlotGeneration() && exact_images &&
                   owner.swapchain() == expected_swapchain && owner.swapchainFrameCommandPool() == expected_command_pool &&
                   owner.swapchainFrameCommandBuffer() == expected_command_buffer &&
                   owner.swapchainFrameImageAvailableSemaphore() == expected_image_available &&
                   owner.swapchainFramePresentationReadySemaphore() == expected_presentation_ready &&
                   owner.swapchainFrameSubmissionFence() == expected_submission_fence &&
                   owner.swapchainFramePresentCompletionFence() == expected_present_completion_fence &&
                   owner.swapchainFrameSlotDisposition() == scenario.mExpectedDisposition && owner.surface() == surface &&
                   owner.logicalDevice() == device && owner.presentationQueue() == queue);
        ensure("preflight classification destroys no object referenced by the retained chain",
               state.mDestroySwapchainCalls == (replacement_expected ? 1 : 0) &&
                   !was_destroyed(state.mDestroyedSwapchains, expected_swapchain) &&
                   state.mDestroyImageViewCalls == (replacement_expected ? expected_views.size() : 0) &&
                   !retained_view_destroyed && state.mDestroyCommandPoolCalls == (replacement_expected ? 1 : 0) &&
                   !was_destroyed(state.mDestroyedCommandPools, expected_command_pool) &&
                   state.mDestroySemaphoreCalls == (replacement_expected ? 2 : 0) &&
                   !was_destroyed(state.mDestroyedSemaphores, expected_image_available) &&
                   !was_destroyed(state.mDestroyedSemaphores, expected_presentation_ready) &&
                   state.mDestroyFenceCalls == (replacement_expected ? 2 : 0) &&
                   !was_destroyed(state.mDestroyedFences, expected_submission_fence) &&
                   !was_destroyed(state.mDestroyedFences, expected_present_completion_fence));

        if (scenario.mExpectedDisposition == VulkanSwapchainFrameSlotDisposition::Pending)
        {
            ensureSwapchainFrameSlotOperationSuccess(
                owner.retryEmptySwapchainFrameSlotCompletion(makeExactFrameSlotOperationRequest(owner)),
                VulkanSwapchainFrameSlotDisposition::Reusable);
        }
        ensure("the retained preflight chain permits its explicit recovery path", owner.reset());
    }
}

template<>
template<>
void render_vulkan_instance_test_object::test<73>()
{
    const std::array scopes{ EpochReplacementScope::Children, EpochReplacementScope::StableParents };
    for (const EpochReplacementScope replacement_scope : scopes)
    {
        FakeState       state;
        ScopedFakeState scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        acquireSwapchainFrameSlotChain(state, owner);

        const VkSurfaceKHR    surface         = owner.surface();
        const VkPhysicalDevice physical_device = owner.physicalDevice();
        const VkDevice         device          = owner.logicalDevice();
        const VkQueue          queue           = owner.presentationQueue();
        const VkSwapchainKHR   swapchain       = owner.swapchain();
        const VkCommandPool    command_pool    = owner.swapchainFrameCommandPool();
        const VkCommandBuffer  command_buffer  = owner.swapchainFrameCommandBuffer();
        const VkSemaphore      image_available = owner.swapchainFrameImageAvailableSemaphore();
        const VkSemaphore      presentation_ready = owner.swapchainFramePresentationReadySemaphore();
        const VkFence          submission_fence = owner.swapchainFrameSubmissionFence();
        const VkFence          present_completion_fence = owner.swapchainFramePresentCompletionFence();
        std::vector<VkImage>    images;
        std::vector<VkImageView> views;
        for (std::uint32_t index = 0; index < owner.resolvedSwapchainImageCount(); ++index)
        {
            images.push_back(owner.swapchainImage(index));
            views.push_back(owner.swapchainImageView(index));
        }

        EpochReplacementContext replacement{ &state, &owner, { 800, 600 }, replacement_scope };
        const VulkanSwapchainChainRebuildError& error = requireSwapchainChainRebuildError(
            owner.rebuildSwapchainChain(makeEpochReplacementRequest(replacement)));
        bool same_images = owner.resolvedSwapchainImageCount() == images.size();
        for (std::size_t index = 0; same_images && index < images.size(); ++index)
        {
            same_images = owner.swapchainImage(static_cast<std::uint32_t>(index)) == images[index] &&
                          owner.swapchainImageView(static_cast<std::uint32_t>(index)) == views[index];
        }
        const bool replaced_stable_parents = replacement_scope == EpochReplacementScope::StableParents;

        ensure("the current callback publishes a complete Reusable replacement before epoch validation",
               replacement.mReplaced && replacement.mPublished && replacement.mOwnerChecks == 1 &&
                   replacement.mWindowChecks == 0);
        ensure("an ownership transition reports preflight publication failure even when pointers and handles compare equal",
               error.mCode == VulkanSwapchainChainRebuildCode::PublicationFailure &&
                   error.mPhase == VulkanSwapchainChainRebuildPhase::Preflight && !error.mFrameSlotDisposition &&
                   std::holds_alternative<std::monostate>(error.mChildError));
        ensure("the callback replacement retains the same complete public identity and stable fake handles",
               owner.hasSurfaceGeneration() && owner.hasPresentationDeviceGeneration() && owner.hasLogicalDeviceGeneration() &&
                   owner.hasSwapchainConfigurationGeneration() && owner.hasSwapchainGeneration() &&
                   owner.hasSwapchainImagesGeneration() && owner.hasSwapchainFrameSlotGeneration() &&
                   owner.surface() == surface && owner.physicalDevice() == physical_device && owner.logicalDevice() == device &&
                   owner.presentationQueue() == queue && owner.swapchain() == swapchain && same_images &&
                   owner.swapchainFrameCommandPool() == command_pool &&
                   owner.swapchainFrameCommandBuffer() == command_buffer &&
                   owner.swapchainFrameImageAvailableSemaphore() == image_available &&
                   owner.swapchainFramePresentationReadySemaphore() == presentation_ready &&
                   owner.swapchainFrameSubmissionFence() == submission_fence &&
                   owner.swapchainFramePresentCompletionFence() == present_completion_fence &&
                   owner.swapchainFrameSlotDisposition() == VulkanSwapchainFrameSlotDisposition::Reusable);
        ensure("only the callback retires the prior ownership; the outer rebuild destroys no replacement object",
               state.mDestroySwapchainCalls == 1 && state.mDestroyImageViewCalls == views.size() &&
                   state.mDestroyCommandPoolCalls == 1 && state.mDestroySemaphoreCalls == 2 &&
                   state.mDestroyFenceCalls == 2 && state.mCreateSwapchainCalls == 2 &&
                   state.mCreateImageViewCalls == 2 * views.size() && state.mCreateCommandPoolCalls == 2 &&
                   state.mCreateSemaphoreCalls == 4 && state.mCreateFenceCalls == 4 &&
                   state.mDestroyDeviceCalls == (replaced_stable_parents ? 1 : 0) &&
                   state.mCreateDeviceCalls == (replaced_stable_parents ? 2 : 1) &&
                   state.mDestroySurfaceCalls == (replaced_stable_parents ? 1 : 0) &&
                   state.mCreateSurfaceCalls == (replaced_stable_parents ? 2 : 1));

        ensureSwapchainFrameSlotOperationSuccess(owner.roundTripEmptySwapchainFrameSlot(makeExactFrameSlotOperationRequest(owner)),
                                                 VulkanSwapchainFrameSlotDisposition::Reusable);
        ensure("the retained epoch-selected replacement permits explicit teardown", owner.reset());
    }
}

template<>
template<>
void render_vulkan_instance_test_object::test<74>()
{
    {
        FakeState                state;
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        acquireSwapchainFrameSlotChain(state, owner);

        const VkSurfaceKHR     surface         = owner.surface();
        const VkPhysicalDevice physical_device = owner.physicalDevice();
        const VkDevice         device          = owner.logicalDevice();
        const VkQueue          queue           = owner.presentationQueue();
        const std::size_t      configuration_queries = state.mSwapchainCapabilitiesCalls;
        ChildParentReplacementContext replacement{ &owner, { 1280, 720 } };
        const VulkanSwapchainChainRebuildError error = requireSwapchainChainRebuildError(
            owner.rebuildSwapchainChain(makeChildParentReplacementRequest(replacement)));
        const auto* child = std::get_if<VulkanSwapchainConfigurationAcquireError>(&error.mChildError);

        ensure("a current configuration-phase callback replaces and reacquires the logical parent with the same native handles",
               replacement.mReplaced && replacement.mReacquired && replacement.mOwnerChecks == 2 &&
                   replacement.mWindowChecks == 1 && owner.hasSurfaceGeneration() &&
                   owner.hasPresentationDeviceGeneration() && owner.hasLogicalDeviceGeneration() && owner.surface() == surface &&
                   owner.physicalDevice() == physical_device && owner.logicalDevice() == device &&
                   owner.presentationQueue() == queue);
        ensure("the configuration child reports its exact parent-mutation provenance before native configuration work",
               error.mCode == VulkanSwapchainChainRebuildCode::ChildFailure &&
                   error.mPhase == VulkanSwapchainChainRebuildPhase::Configuration && child &&
                   child->mCode == VulkanSwapchainConfigurationAcquireCode::LogicalDeviceNotLive &&
                   state.mSwapchainCapabilitiesCalls == configuration_queries &&
                   !owner.hasSwapchainConfigurationGeneration() && !owner.hasSwapchainGeneration() &&
                   !owner.hasSwapchainImagesGeneration() && !owner.hasSwapchainFrameSlotGeneration());
        ensure("the outer rebuild retires the original children but does not destroy the callback's replacement parent",
               state.mDestroyCommandPoolCalls == 1 && state.mDestroyImageViewCalls == state.mSwapchainImageViews.size() &&
                   state.mDestroySwapchainCalls == 1 && state.mDestroyDeviceCalls == 1 && state.mCreateDeviceCalls == 2);
        ensure("the retained replacement parent permits explicit teardown", owner.reset() && state.mDestroyDeviceCalls == 2);
    }

    constexpr std::array paths{ OlderTargetPublicationPath::Surface,
                                OlderTargetPublicationPath::PresentationDevice,
                                OlderTargetPublicationPath::LogicalDevice };
    for (const OlderTargetPublicationPath path : paths)
    {
        FakeState                state;
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        if (path == OlderTargetPublicationPath::PresentationDevice)
        {
            ensure("the surface fixture succeeds", !owner.acquireSurfaceGeneration(makeSurfaceRequest(state, owner)));
        }
        else if (path == OlderTargetPublicationPath::LogicalDevice)
        {
            acquireSelectionChain(state, owner);
        }

        const std::size_t create_surface_calls = state.mCreateSurfaceCalls;
        const std::size_t physical_count_calls = state.mPhysicalCountCalls;
        const std::size_t create_device_calls  = state.mCreateDeviceCalls;
        OlderTargetPublicationContext publication{ &state, &owner, path };
        const VulkanInstanceOwnerCheck owner_check{ &publication, olderTargetPublicationOwnerIsCurrent };
        const VulkanWindowGenerationCheck window_check{ &publication, olderTargetPublicationWindowIsCurrent };
        const std::uint64_t native_window_generation = owner.nativeWindowGeneration();

        switch (path)
        {
            case OlderTargetPublicationPath::Surface:
            {
                const VulkanSurfaceRequest request{
                    native_window_generation,
                    owner_check,
                    window_check,
                    { &state, createSurface }
                };
                ensureSurfaceCode(owner.acquireSurfaceGeneration(request), VulkanSurfaceAcquireCode::SurfaceAlreadyOwned);
                ensure("only the callback resolves and creates the retained surface",
                       owner.hasSurfaceGeneration() && owner.surface() == state.mSurface &&
                           state.mCreateSurfaceCalls == create_surface_calls + 1 && state.mDestroySurfaceCalls == 0 &&
                           state.mDestroySurfaceResolutionCalls == 1);
                break;
            }
            case OlderTargetPublicationPath::PresentationDevice:
            {
                const VulkanPresentationDeviceRequest request{ native_window_generation, owner_check, window_check };
                ensurePresentationDeviceCode(owner.acquirePresentationDeviceGeneration(request),
                                             VulkanPresentationDeviceAcquireCode::PresentationDeviceAlreadyOwned);
                ensure("only the callback resolves and retains the physical-device selection",
                       owner.hasPresentationDeviceGeneration() && owner.physicalDevice() == state.mPhysicalDevice &&
                           state.mPhysicalCountCalls == physical_count_calls + 1 && state.mPhysicalListCalls == 1);
                break;
            }
            case OlderTargetPublicationPath::LogicalDevice:
            {
                const VulkanLogicalDeviceRequest request{ native_window_generation, owner_check, window_check };
                ensureLogicalDeviceCode(owner.acquireLogicalDeviceGeneration(request),
                                        VulkanLogicalDeviceAcquireCode::LogicalDeviceAlreadyOwned);
                ensure("only the callback creates and retains the logical device",
                       owner.hasLogicalDeviceGeneration() && owner.logicalDevice() == state.mDevice &&
                           state.mCreateDeviceCalls == create_device_calls + 1 && state.mDestroyDeviceCalls == 0);
                break;
            }
        }

        ensure("target publication takes precedence over the ownership epoch without invoking the outer window callback",
               publication.mAttempted && publication.mPublished && publication.mOwnerChecks == 1 &&
                   publication.mWindowChecks == 0);
        ensure("the callback-published older target permits explicit teardown", owner.reset());
    }
}

template<>
template<>
void render_vulkan_instance_test_object::test<75>()
{
    {
        FakeState                state;
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        ensure("the surface fixture succeeds", !owner.acquireSurfaceGeneration(makeSurfaceRequest(state, owner)));
        const VkSurfaceKHR surface = owner.surface();

        state.mInstanceOwnerChecks = 0;
        state.mSurfaceWindowChecks = 0;
        SurfaceAbaAllocationContext replacement{ &state, &owner };
        gSurfaceAbaAllocationContext = &replacement;
        const VulkanPresentationDeviceAcquireResult result = VulkanInstanceDetail::acquirePresentationDevice(
            owner, makePresentationDeviceRequest(state, owner), replaceSurfaceAtAllocationCheckpoint);
        gSurfaceAbaAllocationContext = nullptr;

        ensure("the guarded allocation checkpoint cannot establish a same-handle parent ABA transition",
               !result && replacement.mInvoked && !replacement.mReset && !replacement.mReacquired &&
                   owner.hasSurfaceGeneration() && owner.surface() == surface && owner.hasPresentationDeviceGeneration() &&
                   state.mCreateSurfaceCalls == 1 && state.mDestroySurfaceCalls == 0 && state.mInstanceOwnerChecks == 2 &&
                   state.mSurfaceWindowChecks == 2);
        ensure("the checkpoint-guarded acquisition permits explicit teardown", owner.reset());
    }

    {
        FakeState                state;
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        acquireConfigurationChain(state, owner);
        state.mSwapchainDestroyOwner = &owner;

        NativeCandidateResetContext reset{ &owner };
        const VulkanSwapchainRequest request{
            owner.nativeWindowGeneration(),
            owner.swapchainDrawableExtent(),
            { &reset, nativeCandidateResetOwnerIsCurrent },
            { &reset, nativeCandidateResetWindowIsCurrent }
        };
        const VulkanSwapchainAcquireResult result = owner.acquireSwapchainGeneration(request);

        ensureSwapchainCode(result, VulkanSwapchainAcquireCode::StaleInstanceOwner);
        ensure("a guarded move is empty while the source remains available to reject the final callback's parent reset",
               reset.mMoveDestination && reset.mMoveDestination->instance() == VK_NULL_HANDLE &&
                   !reset.mMoveDestination->hasSurfaceGeneration() && reset.mResetAttempted && !reset.mResetSucceeded &&
                   reset.mOwnerChecks == 2 && reset.mWindowChecks == 1 &&
                   owner.hasSurfaceGeneration() && owner.hasPresentationDeviceGeneration() &&
                   owner.hasLogicalDeviceGeneration() && owner.hasSwapchainConfigurationGeneration() &&
                   !owner.hasSwapchainGeneration());
        ensure("a reentrant rebuild rejects the in-progress native acquisition before callbacks or child work",
               reset.mRebuildError &&
                   reset.mRebuildError->mCode == VulkanSwapchainChainRebuildCode::NativeAcquisitionInProgress &&
                   reset.mRebuildError->mPhase == VulkanSwapchainChainRebuildPhase::Preflight &&
                   std::holds_alternative<std::monostate>(reset.mRebuildError->mChildError) &&
                   !reset.mRebuildError->mFrameSlotDisposition && reset.mRebuildOwnerChecks == 0 &&
                   reset.mRebuildWindowChecks == 0);
        ensure("the unpublished swapchain candidate is destroyed while every native parent remains live",
               state.mCreateSwapchainCalls == 1 && state.mDestroySwapchainCalls == 1 &&
                   state.mSwapchainDestroyObservationMade && state.mObservedConfigurationAtSwapchainDestroy &&
                   state.mObservedLogicalAtSwapchainDestroy && state.mObservedSurfaceAtSwapchainDestroy &&
                   !state.mObservedSwapchainImagesAtSwapchainDestroy && !state.mObservedFrameSlotAtSwapchainDestroy &&
                   state.mDestroyDeviceCalls == 0);
        ensure("the same logical-parent reset succeeds after native candidate rollback releases its guard",
               owner.resetLogicalDeviceGeneration() && !owner.hasLogicalDeviceGeneration() &&
                   !owner.hasSwapchainConfigurationGeneration() && state.mDestroyDeviceCalls == 1);
        ensure("the retained stable owner permits explicit teardown", owner.reset());
    }
}

template<>
template<>
void render_vulkan_instance_test_object::test<76>()
{
    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireSwapchainFrameSlotChain(state, owner, { 1280, 720 });

    VulkanSwapchainFrameSlotOperationRequest request = makeSwapchainFrameSlotOperationRequest(state, owner, { 1280, 720 });
    VulkanSwapchainFrameClearColor invalid_color{ { std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f, 1.0f } };
    const std::size_t owner_checks_before_invalid  = state.mInstanceOwnerChecks;
    const std::size_t window_checks_before_invalid = state.mSurfaceWindowChecks;
    const std::size_t lookups_before_invalid       = state.mDeviceProcAddrCalls;
    const VulkanSwapchainFrameSlotParentPresentationResult invalid_result =
        owner.acquireClearToPresentSwapchainFrameSlot(request, invalid_color);
    const VulkanSwapchainFrameSlotParentOperationError& invalid_error = requireSwapchainFrameSlotPresentationError(invalid_result);
    ensure("an invalid clear color is rejected by the parent before callbacks, dispatch, or acquisition",
           invalid_error.mCode == VulkanSwapchainFrameSlotParentOperationCode::InvalidClearColor &&
               !invalid_error.mOperationError && state.mInstanceOwnerChecks == owner_checks_before_invalid &&
               state.mSurfaceWindowChecks == window_checks_before_invalid && state.mDeviceProcAddrCalls == lookups_before_invalid &&
               state.mWaitForFencesCalls == 0 && state.mAcquireNextImageCalls == 0 && state.mClearColorImageCalls == 0);

    const VulkanSwapchainFrameClearColor valid_color{ { 0.125f, 0.25f, 0.5f, 1.0f } };
    state.mMissing = MissingCommand::CmdClearColorImage;
    const VulkanSwapchainFrameSlotParentPresentationResult missing_result =
        owner.acquireClearToPresentSwapchainFrameSlot(request, valid_color);
    const VulkanSwapchainFrameSlotParentOperationError& missing_error = requireSwapchainFrameSlotPresentationError(missing_result);
    ensure("clear dispatch resolves atomically before acquiring an image",
           missing_error.mCode == VulkanSwapchainFrameSlotParentOperationCode::OperationFailure && missing_error.mOperationError &&
               missing_error.mOperationError->mCode == VulkanSwapchainFrameSlotOperationCode::MissingRequiredCommand &&
               missing_error.mOperationError->mCommand == VulkanSwapchainFrameSlotCommand::CmdClearColorImage &&
               state.mWaitForFencesCalls == 0 && state.mAcquireNextImageCalls == 0 && state.mClearColorImageCalls == 0 &&
               owner.swapchainFrameSlotDisposition() == VulkanSwapchainFrameSlotDisposition::Reusable);

    state.mMissing = MissingCommand::None;
    state.mAcquiredImageIndex = 1;
    VulkanSwapchainFrameClearColor mutable_color = valid_color;
    ClearColorMutationContext mutation{ &owner, &mutable_color };
    request = { owner.nativeWindowGeneration(),
                owner.swapchainDrawableExtent(),
                { &mutation, clearColorMutationOwnerIsCurrent },
                { &mutation, clearColorMutationWindowIsCurrent } };
    const VulkanSwapchainFrameSlotParentPresentationResult first_result =
        owner.acquireClearToPresentSwapchainFrameSlot(request, mutable_color);
    const VulkanSwapchainFrameSlotPresentationSuccess& first_success = requireSwapchainFrameSlotPresentationSuccess(first_result);
    ensure("the parent copies the typed color before freshness callbacks and presents the acquired image",
           first_success.mOutcome == VulkanSwapchainFrameSlotPresentationOutcome::Presented && first_success.mImageIndex == 1 &&
               mutation.mOwnerChecks == 2 && mutable_color.mRgba == std::array<float, 4>{ 1.0f, 1.0f, 1.0f, 1.0f } &&
               state.mClearColorImageCalls == 1 && state.mClearedImage == state.mSwapchainImages[1] &&
               state.mClearColorValue.float32[0] == 0.125f && state.mClearColorValue.float32[1] == 0.25f &&
               state.mClearColorValue.float32[2] == 0.5f && state.mClearColorValue.float32[3] == 1.0f &&
               state.mClearSubresourceRange.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT &&
               state.mClearSubresourceRange.baseMipLevel == 0 && state.mClearSubresourceRange.levelCount == 1 &&
               state.mClearSubresourceRange.baseArrayLayer == 0 && state.mClearSubresourceRange.layerCount == 1 &&
               state.mPipelineBarrierCalls == 2 && owner.swapchainFrameSlotDisposition() == VulkanSwapchainFrameSlotDisposition::Reusable);

    const std::size_t retained_lookup_count = state.mDeviceProcAddrCalls;
    state.mAcquiredImageIndex = 2;
    request = makeSwapchainFrameSlotOperationRequest(state, owner, { 1280, 720 });
    const VulkanSwapchainFrameSlotParentPresentationResult second_result =
        owner.acquireClearToPresentSwapchainFrameSlot(request, VulkanSwapchainFrameClearColor{});
    const VulkanSwapchainFrameSlotPresentationSuccess& second_success = requireSwapchainFrameSlotPresentationSuccess(second_result);
    ensure("a retired clear-to-present transaction reuses dispatch for the next exact image",
           second_success.mOutcome == VulkanSwapchainFrameSlotPresentationOutcome::Presented && second_success.mImageIndex == 2 &&
               state.mDeviceProcAddrCalls == retained_lookup_count && state.mClearColorImageCalls == 2 &&
               state.mClearedImage == state.mSwapchainImages[2] && state.mPipelineBarrierCalls == 4 &&
               state.mQueueSubmitCalls == 2 && state.mQueuePresentCalls == 2 &&
               owner.swapchainFrameSlotDisposition() == VulkanSwapchainFrameSlotDisposition::Reusable && owner.reset());
}

template<>
template<>
void render_vulkan_instance_test_object::test<77>()
{
    static_assert(static_cast<std::uint8_t>(VulkanSwapchainChainRebuildPhase::Preflight) == 0);
    static_assert(static_cast<std::uint8_t>(VulkanSwapchainChainRebuildPhase::Retirement) == 1);
    static_assert(static_cast<std::uint8_t>(VulkanSwapchainChainRebuildPhase::Configuration) == 2);
    static_assert(static_cast<std::uint8_t>(VulkanSwapchainChainRebuildPhase::Swapchain) == 3);
    static_assert(static_cast<std::uint8_t>(VulkanSwapchainChainRebuildPhase::Images) == 4);
    static_assert(static_cast<std::uint8_t>(VulkanSwapchainChainRebuildPhase::FrameSlot) == 5);
    static_assert(static_cast<std::uint8_t>(VulkanSwapchainChainRebuildPhase::FinalFreshness) == 6);
    static_assert(static_cast<std::uint8_t>(VulkanSwapchainChainRebuildPhase::PresentationTarget) == 7);
    static_assert(static_cast<std::uint8_t>(VulkanSwapchainChainRebuildPhase::PresentationPipeline) == 8);
    static_assert(static_cast<std::uint8_t>(VulkanSwapchainChainRebuildPhase::Readback) == 9);
    static_assert(
        std::is_same_v<std::variant_alternative_t<4, VulkanSwapchainChainRebuildChildError>, VulkanSwapchainFrameSlotAcquireError>);
    static_assert(std::is_same_v<std::variant_alternative_t<5, VulkanSwapchainChainRebuildChildError>,
                                 VulkanSwapchainPresentationTargetAcquireError>);
    static_assert(std::is_same_v<std::variant_alternative_t<6, VulkanSwapchainChainRebuildChildError>,
                                 VulkanSwapchainPresentationPipelineAcquireError>);
    static_assert(
        std::is_same_v<std::variant_alternative_t<7, VulkanSwapchainChainRebuildChildError>, VulkanSwapchainReadbackAcquireError>);
    static_assert(noexcept(std::declval<const VulkanInstanceGeneration&>().hasSwapchainPresentationTargetGeneration()));
    static_assert(noexcept(std::declval<const VulkanInstanceGeneration&>().swapchainPresentationRenderPass()));
    static_assert(noexcept(std::declval<const VulkanInstanceGeneration&>().swapchainPresentationFramebuffer(0)));
    static_assert(noexcept(std::declval<VulkanInstanceGeneration&>().acquireSwapchainPresentationTargetGeneration(
        std::declval<const VulkanSwapchainPresentationTargetRequest&>())));
    static_assert(noexcept(std::declval<VulkanInstanceGeneration&>().resetSwapchainPresentationTargetGeneration()));

    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));

    VulkanSwapchainPresentationTargetRequest request = makeSwapchainPresentationTargetRequest(state, owner);
    request.mInstanceOwnerCheck                       = {};
    ensureSwapchainPresentationTargetCode(owner.acquireSwapchainPresentationTargetGeneration(request),
                                          VulkanSwapchainPresentationTargetAcquireCode::InvalidInstanceOwnerCheck);
    request                         = makeSwapchainPresentationTargetRequest(state, owner);
    request.mWindowGenerationCheck = {};
    ensureSwapchainPresentationTargetCode(owner.acquireSwapchainPresentationTargetGeneration(request),
                                          VulkanSwapchainPresentationTargetAcquireCode::InvalidWindowGenerationCheck);
    request                           = makeSwapchainPresentationTargetRequest(state, owner);
    request.mNativeWindowGeneration  = 0;
    ensureSwapchainPresentationTargetCode(owner.acquireSwapchainPresentationTargetGeneration(request),
                                          VulkanSwapchainPresentationTargetAcquireCode::InvalidNativeWindowGeneration);
    request                   = makeSwapchainPresentationTargetRequest(state, owner);
    request.mDrawableExtent   = {};
    ensureSwapchainPresentationTargetCode(owner.acquireSwapchainPresentationTargetGeneration(request),
                                          VulkanSwapchainPresentationTargetAcquireCode::InvalidDrawableExtent);
    ensureSwapchainPresentationTargetCode(
        owner.acquireSwapchainPresentationTargetGeneration(makeSwapchainPresentationTargetRequest(state, owner)),
        VulkanSwapchainPresentationTargetAcquireCode::SurfaceNotLive);

    acquireSwapchainChain(state, owner);
    ensureSwapchainPresentationTargetCode(
        owner.acquireSwapchainPresentationTargetGeneration(makeSwapchainPresentationTargetRequest(state, owner)),
        VulkanSwapchainPresentationTargetAcquireCode::SwapchainImagesNotLive);
    ensure("presentation-target preconditions publish nothing",
           !owner.hasSwapchainPresentationTargetGeneration() &&
               owner.swapchainPresentationRenderPass() == VK_NULL_HANDLE &&
               owner.swapchainPresentationFramebufferCount() == 0 &&
               owner.swapchainPresentationFramebuffer(0) == VK_NULL_HANDLE && owner.reset());
}

template<>
template<>
void render_vulkan_instance_test_object::test<78>()
{
    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireCompleteSwapchainChain(state, owner);

    ensure("the aggregate publishes the exact render pass and one framebuffer per exact image",
           owner.hasSwapchainPresentationTargetGeneration() &&
               owner.swapchainPresentationRenderPass() == state.mPresentationRenderPass &&
               owner.swapchainPresentationFramebufferCount() == state.mSwapchainImages.size() &&
               owner.swapchainPresentationFramebuffer(0) == state.mPresentationFramebuffers[0] &&
               owner.swapchainPresentationFramebuffer(2) == state.mPresentationFramebuffers[2] &&
               owner.swapchainPresentationFramebuffer(3) == VK_NULL_HANDLE &&
               state.mFramebufferAttachments == state.mSwapchainImageViews);
    ensureSwapchainPresentationTargetCode(
        owner.acquireSwapchainPresentationTargetGeneration(makeSwapchainPresentationTargetRequest(state, owner)),
        VulkanSwapchainPresentationTargetAcquireCode::SwapchainPresentationTargetAlreadyOwned);

    const VulkanSwapchainFrameSlotOperationRequest operation = makeSwapchainFrameSlotOperationRequest(state, owner);
    state.mWaitForFencesResults = { VK_SUCCESS, VK_TIMEOUT, VK_SUCCESS };
    requireSwapchainFrameSlotOperationError(owner.roundTripEmptySwapchainFrameSlot(operation));
    ensure("a pending sibling refuses target retirement without mutating either child",
           owner.swapchainFrameSlotDisposition() == VulkanSwapchainFrameSlotDisposition::Pending &&
               !owner.resetSwapchainPresentationTargetGeneration() &&
               owner.hasSwapchainPresentationTargetGeneration() && owner.hasSwapchainFrameSlotGeneration());
    ensureSwapchainFrameSlotOperationSuccess(owner.retryEmptySwapchainFrameSlotCompletion(operation),
                                             VulkanSwapchainFrameSlotDisposition::Reusable);

    state.mFrameSlotDestroyOwner          = &owner;
    state.mPresentationTargetDestroyOwner = &owner;
    state.mImageViewDestroyOwner          = &owner;
    state.mEvents.clear();
    ensure("target retirement cascades through the frame slot but preserves its exact images parent",
           owner.resetSwapchainPresentationTargetGeneration() && owner.hasSwapchainImagesGeneration() &&
               !owner.hasSwapchainPresentationTargetGeneration() && !owner.hasSwapchainFrameSlotGeneration() &&
               state.mFrameSlotDestroyObservationMade && state.mObservedPresentationTargetAtFrameSlotDestroy &&
               state.mPresentationTargetDestroyObservationMade && state.mObservedImagesAtPresentationTargetDestroy &&
               !state.mObservedFrameSlotAtPresentationTargetDestroy && !state.mImageViewDestroyObservationMade);
    const auto first_fence_destroy = std::find(state.mEvents.begin(), state.mEvents.end(), Event::DestroyFence);
    const auto first_framebuffer_destroy = std::find(state.mEvents.begin(), state.mEvents.end(), Event::DestroyFramebuffer);
    ensure("retirement destroys the frame-slot sibling before every target framebuffer",
           first_fence_destroy != state.mEvents.end() && first_framebuffer_destroy != state.mEvents.end() &&
               first_fence_destroy < first_framebuffer_destroy);

    ensure("the retained images can publish both siblings again",
           !owner.acquireSwapchainPresentationTargetGeneration(makeSwapchainPresentationTargetRequest(state, owner)) &&
               !owner.acquireSwapchainFrameSlotGeneration(makeSwapchainFrameSlotRequest(state, owner)));
    VulkanInstanceGeneration moved(std::move(owner));
    ensure("moving the aggregate preserves the complete exact child graph",
           owner.instance() == VK_NULL_HANDLE && moved.hasSwapchainImagesGeneration() &&
               moved.hasSwapchainPresentationTargetGeneration() && moved.hasSwapchainFrameSlotGeneration() &&
               moved.swapchainPresentationRenderPass() == state.mPresentationRenderPass &&
               moved.swapchainPresentationFramebufferCount() == state.mSwapchainImages.size());
    state.mFrameSlotDestroyOwner          = &moved;
    state.mPresentationTargetDestroyOwner = &moved;
    state.mImageViewDestroyOwner          = &moved;
    ensure("the moved aggregate retains target-before-images explicit teardown",
           moved.reset() && state.mImageViewDestroyObservationMade &&
               !state.mObservedPresentationTargetAtImageViewDestroy);
}

template<>
template<>
void render_vulkan_instance_test_object::test<79>()
{
    {
        FakeState                state;
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        acquireSwapchainImagesChain(state, owner);
        state.mMissing = MissingCommand::CreateFramebuffer;
        const VulkanSwapchainPresentationTargetAcquireResult result =
            owner.acquireSwapchainPresentationTargetGeneration(makeSwapchainPresentationTargetRequest(state, owner));
        const VulkanSwapchainPresentationTargetAcquireError& error = requireSwapchainPresentationTargetError(
            result);
        ensure("a missing target command preserves its exact nested resolution provenance",
               error.mCode == VulkanSwapchainPresentationTargetAcquireCode::ResolutionFailure && error.mResolutionError &&
                   error.mResolutionError->mCode == VulkanSwapchainPresentationTargetResolutionCode::MissingRequiredCommand &&
                   error.mResolutionError->mCommand == VulkanSwapchainPresentationTargetCommand::CreateFramebuffer &&
                   !owner.hasSwapchainPresentationTargetGeneration() && owner.hasSwapchainImagesGeneration() &&
                   state.mCreateRenderPassCalls == 0 && owner.reset());
    }

    {
        FakeState                state;
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        acquireSwapchainImagesChain(state, owner);
        state.mInstanceOwnerChecks    = 0;
        state.mSurfaceWindowChecks    = 0;
        state.mFailInstanceOwnerCheck = 2;
        ensureSwapchainPresentationTargetCode(
            owner.acquireSwapchainPresentationTargetGeneration(makeSwapchainPresentationTargetRequest(state, owner)),
            VulkanSwapchainPresentationTargetAcquireCode::StaleInstanceOwner);
        ensure("post-resolution staleness destroys the unpublished target while retaining every parent",
               !owner.hasSwapchainPresentationTargetGeneration() && owner.hasSwapchainImagesGeneration() &&
                   state.mCreateRenderPassCalls == 1 && state.mCreateFramebufferCalls == state.mSwapchainImages.size() &&
                   state.mDestroyFramebufferCalls == state.mSwapchainImages.size() && state.mDestroyRenderPassCalls == 1 &&
                   owner.reset());
    }

    {
        FakeState                state;
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        acquireSwapchainFrameSlotChain(state, owner);
        state.mInstanceOwnerChecks                  = 0;
        state.mSurfaceWindowChecks                  = 0;
        state.mMutationOwner                        = &owner;
        state.mResetFrameSlotOnInstanceOwnerCheck   = 1;
        ensureSwapchainPresentationTargetCode(
            owner.acquireSwapchainPresentationTargetGeneration(makeSwapchainPresentationTargetRequest(state, owner)),
            VulkanSwapchainPresentationTargetAcquireCode::SwapchainImagesNotLive);
        ensure("an epoch-changing freshness callback cannot publish over a same-parent legacy sibling graph",
               owner.hasSwapchainImagesGeneration() && !owner.hasSwapchainPresentationTargetGeneration() &&
                   !owner.hasSwapchainFrameSlotGeneration() && state.mCreateRenderPassCalls == 0 && owner.reset());
    }

    {
        FakeState                state;
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        acquireSwapchainImagesChain(state, owner);
        ensureSwapchainPresentationTargetCode(
            VulkanInstanceDetail::acquireSwapchainPresentationTarget(
                owner, makeSwapchainPresentationTargetRequest(state, owner), failAllocation),
            VulkanSwapchainPresentationTargetAcquireCode::AllocationFailure);
        ensure("aggregate allocation failure rolls the lower candidate back without touching its parents",
               !owner.hasSwapchainPresentationTargetGeneration() && owner.hasSwapchainImagesGeneration() &&
                   state.mDestroyFramebufferCalls == state.mSwapchainImages.size() && state.mDestroyRenderPassCalls == 1 &&
                   owner.reset());
    }

    {
        FakeState                state;
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        acquireSwapchainImagesChain(state, owner);
        NativeCandidateResetContext reset{ &owner };
        const VulkanSwapchainPresentationTargetRequest request{
            owner.nativeWindowGeneration(),
            owner.swapchainDrawableExtent(),
            { &reset, nativeCandidateResetOwnerIsCurrent },
            { &reset, nativeCandidateResetWindowIsCurrent }
        };
        ensureSwapchainPresentationTargetCode(owner.acquireSwapchainPresentationTargetGeneration(request),
                                              VulkanSwapchainPresentationTargetAcquireCode::StaleInstanceOwner);
        ensure("native target construction blocks reentrant move, rebuild, and parent reset until rollback",
               reset.mMoveDestination && reset.mMoveDestination->instance() == VK_NULL_HANDLE &&
                   reset.mResetAttempted && !reset.mResetSucceeded && reset.mRebuildError &&
                   reset.mRebuildError->mCode == VulkanSwapchainChainRebuildCode::NativeAcquisitionInProgress &&
                   !owner.hasSwapchainPresentationTargetGeneration() && owner.hasSwapchainImagesGeneration() &&
                   state.mDestroyFramebufferCalls == state.mSwapchainImages.size() && state.mDestroyRenderPassCalls == 1 &&
                   owner.reset());
    }
}

template<>
template<>
void render_vulkan_instance_test_object::test<80>()
{
    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireCompleteSwapchainChain(state, owner);
    const VkSurfaceKHR surface = owner.surface();
    const VkDevice     device  = owner.logicalDevice();
    const VkQueue      queue   = owner.presentationQueue();

    state.mMissing = MissingCommand::CreateFramebuffer;
    const VulkanSwapchainChainRebuildError& error = requireSwapchainChainRebuildError(
        owner.rebuildSwapchainChain(makeSwapchainChainRebuildRequest(state, owner, VkExtent2D{ 1280, 720 })));
    const auto* child = std::get_if<VulkanSwapchainPresentationTargetAcquireError>(&error.mChildError);
    ensure("target rebuild failure retains the appended phase and nested command error",
           error.mCode == VulkanSwapchainChainRebuildCode::ChildFailure &&
               error.mPhase == VulkanSwapchainChainRebuildPhase::PresentationTarget && child &&
               child->mCode == VulkanSwapchainPresentationTargetAcquireCode::ResolutionFailure &&
               child->mResolutionError &&
               child->mResolutionError->mCommand == VulkanSwapchainPresentationTargetCommand::CreateFramebuffer);
    ensure("target failure rolls the rebuilt WSI prefix back to the stable parent-only baseline",
           owner.surface() == surface && owner.logicalDevice() == device && owner.presentationQueue() == queue &&
               owner.hasLogicalDeviceGeneration() && !owner.hasSwapchainConfigurationGeneration() &&
               !owner.hasSwapchainGeneration() && !owner.hasSwapchainImagesGeneration() &&
               !owner.hasSwapchainPresentationTargetGeneration() && !owner.hasSwapchainFrameSlotGeneration());

    state.mMissing = MissingCommand::None;
    ensureSwapchainChainRebuildOutcome(
        owner.rebuildSwapchainChain(makeSwapchainChainRebuildRequest(state, owner, VkExtent2D{ 1280, 720 })),
        VulkanSwapchainChainRebuildOutcome::Ready);
    ensure("restore publishes target before the independently owned frame-slot sibling",
           owner.hasSwapchainConfigurationGeneration() && owner.hasSwapchainGeneration() &&
               owner.hasSwapchainImagesGeneration() && owner.hasSwapchainPresentationTargetGeneration() &&
               owner.hasSwapchainFrameSlotGeneration() &&
               owner.swapchainPresentationFramebufferCount() == owner.resolvedSwapchainImageCount());

    ensureSwapchainChainRebuildOutcome(
        owner.rebuildSwapchainChain(makeSwapchainChainRebuildRequest(state, owner, VkExtent2D{ 0, 0 })),
        VulkanSwapchainChainRebuildOutcome::Suspended);
    ensure("suspension retires frame slot, target, images, swapchain, and configuration in order",
           owner.surface() == surface && owner.logicalDevice() == device && owner.presentationQueue() == queue &&
               !owner.hasSwapchainConfigurationGeneration() && !owner.hasSwapchainGeneration() &&
               !owner.hasSwapchainImagesGeneration() && !owner.hasSwapchainPresentationTargetGeneration() &&
               !owner.hasSwapchainFrameSlotGeneration() && owner.reset());
}

template<>
template<>
void render_vulkan_instance_test_object::test<81>()
{
    static_assert(static_cast<std::uint8_t>(VulkanSwapchainFrameSlotParentOperationCode::OperationFailure) == 16);
    static_assert(static_cast<std::uint8_t>(VulkanSwapchainFrameSlotParentOperationCode::InvalidClearColor) == 17);
    static_assert(
        static_cast<std::uint8_t>(VulkanSwapchainFrameSlotParentOperationCode::SwapchainPresentationTargetNotLive) == 18);
    static_assert(noexcept(std::declval<VulkanInstanceGeneration&>().acquireRenderPassClearToPresentSwapchainFrameSlot(
        std::declval<const VulkanSwapchainFrameSlotOperationRequest&>(),
        std::declval<const VulkanSwapchainFrameClearColor&>())));

    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireSwapchainFrameSlotChain(state, owner, { 1280, 720 });
    VulkanSwapchainFrameSlotOperationRequest request =
        makeSwapchainFrameSlotOperationRequest(state, owner, { 1280, 720 });

    const VulkanSwapchainFrameClearColor invalid_color{
        { std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f, 1.0f }
    };
    const std::size_t owner_checks_before  = state.mInstanceOwnerChecks;
    const std::size_t window_checks_before = state.mSurfaceWindowChecks;
    const std::size_t lookups_before       = state.mDeviceProcAddrCalls;
    const VulkanSwapchainFrameSlotParentPresentationResult invalid_result =
        owner.acquireRenderPassClearToPresentSwapchainFrameSlot(request, invalid_color);
    const VulkanSwapchainFrameSlotParentOperationError& invalid_error =
        requireSwapchainFrameSlotPresentationError(invalid_result);
    ensure("invalid render-pass clear input wins before target validation, callbacks, dispatch, or acquisition",
           invalid_error.mCode == VulkanSwapchainFrameSlotParentOperationCode::InvalidClearColor &&
               !invalid_error.mOperationError && state.mInstanceOwnerChecks == owner_checks_before &&
               state.mSurfaceWindowChecks == window_checks_before && state.mDeviceProcAddrCalls == lookups_before &&
               state.mWaitForFencesCalls == 0 && state.mAcquireNextImageCalls == 0);

    const VulkanSwapchainFrameClearColor valid_color{ { 0.125f, 0.25f, 0.5f, 1.0f } };
    const VulkanSwapchainFrameSlotParentPresentationResult target_result =
        owner.acquireRenderPassClearToPresentSwapchainFrameSlot(request, valid_color);
    const VulkanSwapchainFrameSlotParentOperationError& target_error =
        requireSwapchainFrameSlotPresentationError(target_result);
    ensure("render-pass mode rejects a missing target before callbacks, dispatch, or acquisition",
           target_error.mCode == VulkanSwapchainFrameSlotParentOperationCode::SwapchainPresentationTargetNotLive &&
               !target_error.mOperationError && state.mInstanceOwnerChecks == owner_checks_before &&
               state.mSurfaceWindowChecks == window_checks_before && state.mDeviceProcAddrCalls == lookups_before &&
               state.mWaitForFencesCalls == 0 && state.mAcquireNextImageCalls == 0);

    state.mAcquiredImageIndex = 1;
    state.mQueuePresentResult = VK_ERROR_OUT_OF_HOST_MEMORY;
    const VulkanSwapchainFrameSlotParentPresentationResult retryable_result =
        owner.acquireClearToPresentSwapchainFrameSlot(request, valid_color);
    const VulkanSwapchainFrameSlotParentOperationError& retryable_error =
        requireSwapchainFrameSlotPresentationError(retryable_result);
    ensure("the legacy transfer-clear path remains target-independent and retains its retry obligation",
           retryable_error.mCode == VulkanSwapchainFrameSlotParentOperationCode::OperationFailure &&
               retryable_error.mOperationError &&
               retryable_error.mOperationError->mDisposition == VulkanSwapchainFrameSlotDisposition::PresentationReady &&
               !owner.hasSwapchainPresentationTargetGeneration() && state.mClearColorImageCalls == 1 &&
               state.mBeginRenderPassCalls == 0);

    state.mQueuePresentResult = VK_SUCCESS;
    request.mDrawableExtent   = {};
    const VulkanSwapchainFrameSlotParentPresentationResult retry_result =
        owner.retrySwapchainFrameSlotPresentation(request);
    const VulkanSwapchainFrameSlotPresentationSuccess& retry_success =
        requireSwapchainFrameSlotPresentationSuccess(retry_result);
    ensure("legacy presentation retry ignores target and drawable state while completing retained work",
           retry_success.mOutcome == VulkanSwapchainFrameSlotPresentationOutcome::Presented &&
               retry_success.mImageIndex == 1 &&
               owner.swapchainFrameSlotDisposition() == VulkanSwapchainFrameSlotDisposition::Reusable);

    request.mDrawableExtent          = { 1280, 720 };
    state.mAcquiredImageIndex        = 2;
    state.mEndCommandBufferResult    = VK_ERROR_UNKNOWN;
    const VulkanSwapchainFrameSlotParentPresentationResult acquired_result =
        owner.acquireToPresentSwapchainFrameSlot(request);
    const VulkanSwapchainFrameSlotParentOperationError& acquired_error =
        requireSwapchainFrameSlotPresentationError(acquired_result);
    ensure("the legacy layout-only path remains target-independent after acquisition",
           acquired_error.mOperationError &&
               acquired_error.mOperationError->mDisposition == VulkanSwapchainFrameSlotDisposition::ImageAcquired &&
               acquired_error.mOperationError->mImageIndex == 2 && !owner.hasSwapchainPresentationTargetGeneration());

    state.mEndCommandBufferResult = VK_SUCCESS;
    request.mDrawableExtent       = {};
    ensureSwapchainFrameSlotOperationSuccess(owner.cancelSwapchainFrameSlotPresentation(request),
                                             VulkanSwapchainFrameSlotDisposition::Reusable);
    ensure("legacy cancellation releases its image without a presentation target", state.mReleaseSwapchainImagesCalls == 1 && owner.reset());
}

template<>
template<>
void render_vulkan_instance_test_object::test<82>()
{
    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireCompleteSwapchainChain(state, owner, { 1280, 720 });
    VulkanSwapchainFrameSlotOperationRequest request =
        makeSwapchainFrameSlotOperationRequest(state, owner, { 1280, 720 });
    const VulkanSwapchainFrameClearColor color{ { 0.125f, 0.25f, 0.5f, 1.0f } };

    for (const auto& [missing, command] :
         { std::pair{ MissingCommand::CmdBeginRenderPass, VulkanSwapchainFrameSlotCommand::CmdBeginRenderPass },
           std::pair{ MissingCommand::CmdEndRenderPass, VulkanSwapchainFrameSlotCommand::CmdEndRenderPass } })
    {
        state.mMissing = missing;
        const VulkanSwapchainFrameSlotParentPresentationResult result =
            owner.acquireRenderPassClearToPresentSwapchainFrameSlot(request, color);
        const VulkanSwapchainFrameSlotParentOperationError& error =
            requireSwapchainFrameSlotPresentationError(result);
        ensure("render-pass dispatch resolves both recording commands before acquiring an image",
               error.mCode == VulkanSwapchainFrameSlotParentOperationCode::OperationFailure && error.mOperationError &&
                   error.mOperationError->mCode == VulkanSwapchainFrameSlotOperationCode::MissingRequiredCommand &&
                   error.mOperationError->mCommand == command && state.mWaitForFencesCalls == 0 &&
                   state.mAcquireNextImageCalls == 0 && state.mBeginRenderPassCalls == 0 &&
                   owner.swapchainFrameSlotDisposition() == VulkanSwapchainFrameSlotDisposition::Reusable);
    }

    state.mMissing            = MissingCommand::None;
    state.mAcquiredImageIndex = 2;
    VulkanSwapchainFrameClearColor mutable_color = color;
    ClearColorMutationContext mutation{ &owner, &mutable_color };
    request = { owner.nativeWindowGeneration(),
                owner.swapchainDrawableExtent(),
                { &mutation, clearColorMutationOwnerIsCurrent },
                { &mutation, clearColorMutationWindowIsCurrent } };
    const VulkanSwapchainFrameSlotParentPresentationResult result =
        owner.acquireRenderPassClearToPresentSwapchainFrameSlot(request, mutable_color);
    const VulkanSwapchainFrameSlotPresentationSuccess& success =
        requireSwapchainFrameSlotPresentationSuccess(result);
    const auto begin_event = std::find(state.mEvents.begin(), state.mEvents.end(), Event::BeginRenderPass);
    const auto end_event   = std::find(state.mEvents.begin(), state.mEvents.end(), Event::EndRenderPass);
    ensure("the parent copies the normalized clear before callbacks and executes against the acquired image target",
           success.mOutcome == VulkanSwapchainFrameSlotPresentationOutcome::Presented && success.mImageIndex == 2 &&
               mutation.mOwnerChecks == 2 && mutable_color.mRgba == std::array<float, 4>{ 1.0f, 1.0f, 1.0f, 1.0f } &&
               state.mBeginRenderPassCalls == 1 && state.mEndRenderPassCalls == 1 &&
               state.mRenderPassCommandBuffer == state.mCommandBuffer &&
               state.mOperationRenderPass == state.mPresentationRenderPass &&
               state.mOperationFramebuffer == state.mPresentationFramebuffers[2] &&
               state.mOperationRenderArea.extent.width == 1280 && state.mOperationRenderArea.extent.height == 720 &&
               state.mRenderPassClearValue.color.float32[0] == 0.125f &&
               state.mRenderPassClearValue.color.float32[1] == 0.25f &&
               state.mRenderPassClearValue.color.float32[2] == 0.5f &&
               state.mRenderPassClearValue.color.float32[3] == 1.0f &&
               state.mOperationSubpassContents == VK_SUBPASS_CONTENTS_INLINE && state.mClearColorImageCalls == 0 &&
               state.mPipelineBarrierCalls == 2 && begin_event != state.mEvents.end() && end_event != state.mEvents.end() &&
               begin_event < end_event && owner.reset());
}

template<>
template<>
void render_vulkan_instance_test_object::test<83>()
{
    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireCompleteSwapchainChain(state, owner, { 1280, 720 });

    PresentationTargetReplacementContext replacement{ &state, &owner };
    const VulkanSwapchainFrameSlotOperationRequest request{
        owner.nativeWindowGeneration(),
        owner.swapchainDrawableExtent(),
        { &replacement, presentationTargetReplacementOwnerIsCurrent },
        { &replacement, presentationTargetReplacementWindowIsCurrent }
    };
    const VulkanSwapchainFrameSlotParentPresentationResult result =
        owner.acquireRenderPassClearToPresentSwapchainFrameSlot(request, VulkanSwapchainFrameClearColor{});
    const VulkanSwapchainFrameSlotParentOperationError& error =
        requireSwapchainFrameSlotPresentationError(result);
    ensure("an exact-looking target replacement during final freshness is rejected by identity and epoch before acquisition",
           error.mCode == VulkanSwapchainFrameSlotParentOperationCode::SwapchainPresentationTargetNotLive &&
               !error.mOperationError && replacement.mOwnerChecks == 2 && replacement.mResetSucceeded &&
               replacement.mReplacementSucceeded && owner.hasSwapchainPresentationTargetGeneration() &&
               owner.hasSwapchainFrameSlotGeneration() && state.mCreateRenderPassCalls == 2 &&
               state.mDestroyRenderPassCalls == 1 && state.mCreateCommandPoolCalls == 2 &&
               state.mDestroyCommandPoolCalls == 1 && state.mWaitForFencesCalls == 0 &&
               state.mAcquireNextImageCalls == 0 && state.mBeginRenderPassCalls == 0 && owner.reset());
}

template<>
template<>
void render_vulkan_instance_test_object::test<84>()
{
    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireCompleteSwapchainChain(state, owner, { 1280, 720 });
    VulkanSwapchainFrameSlotOperationRequest request =
        makeSwapchainFrameSlotOperationRequest(state, owner, { 1280, 720 });

    state.mAcquiredImageIndex  = 1;
    state.mWaitForFencesResults = { VK_SUCCESS, VK_TIMEOUT, VK_SUCCESS };
    const VulkanSwapchainFrameSlotParentPresentationResult pending_result =
        owner.acquireRenderPassClearToPresentSwapchainFrameSlot(request, VulkanSwapchainFrameClearColor{});
    const VulkanSwapchainFrameSlotParentOperationError& pending_error =
        requireSwapchainFrameSlotPresentationError(pending_result);
    ensure("a pending render-pass presentation retains its target and refuses transitive reset",
           pending_error.mOperationError &&
               pending_error.mOperationError->mCommand == VulkanSwapchainFrameSlotCommand::WaitForFences &&
               pending_error.mOperationError->mDisposition == VulkanSwapchainFrameSlotDisposition::PresentPending &&
               owner.hasSwapchainPresentationTargetGeneration() && !owner.resetSwapchainPresentationTargetGeneration());

    request.mDrawableExtent = {};
    const VulkanSwapchainFrameSlotParentPresentationResult completion_result =
        owner.retrySwapchainFrameSlotPresentationCompletion(request);
    const VulkanSwapchainFrameSlotPresentationSuccess& completion =
        requireSwapchainFrameSlotPresentationSuccess(completion_result);
    ensure("completion retires the retained render-pass obligation and restores target reset",
           completion.mOutcome == VulkanSwapchainFrameSlotPresentationOutcome::Presented && completion.mImageIndex == 1 &&
               owner.swapchainFrameSlotDisposition() == VulkanSwapchainFrameSlotDisposition::Reusable &&
               owner.resetSwapchainPresentationTargetGeneration() && owner.hasSwapchainImagesGeneration() &&
               !owner.hasSwapchainPresentationTargetGeneration() && !owner.hasSwapchainFrameSlotGeneration());

    ensure("the retained images can republish the render-pass target and frame slot",
           !owner.acquireSwapchainPresentationTargetGeneration(makeSwapchainPresentationTargetRequest(state, owner, { 1280, 720 })) &&
               !owner.acquireSwapchainFrameSlotGeneration(makeSwapchainFrameSlotRequest(state, owner, { 1280, 720 })));
    state.mAcquiredImageIndex     = 2;
    state.mEndCommandBufferResult = VK_ERROR_UNKNOWN;
    request = makeSwapchainFrameSlotOperationRequest(state, owner, { 1280, 720 });
    const VulkanSwapchainFrameSlotParentPresentationResult acquired_result =
        owner.acquireRenderPassClearToPresentSwapchainFrameSlot(request, VulkanSwapchainFrameClearColor{});
    const VulkanSwapchainFrameSlotParentOperationError& acquired_error =
        requireSwapchainFrameSlotPresentationError(acquired_result);
    ensure("a post-acquire render-pass failure retains the target until cancellation",
           acquired_error.mOperationError &&
               acquired_error.mOperationError->mCommand == VulkanSwapchainFrameSlotCommand::EndCommandBuffer &&
               acquired_error.mOperationError->mDisposition == VulkanSwapchainFrameSlotDisposition::ImageAcquired &&
               acquired_error.mOperationError->mImageIndex == 2 && !owner.resetSwapchainPresentationTargetGeneration());

    state.mEndCommandBufferResult = VK_SUCCESS;
    request.mDrawableExtent       = {};
    ensureSwapchainFrameSlotOperationSuccess(owner.cancelSwapchainFrameSlotPresentation(request),
                                             VulkanSwapchainFrameSlotDisposition::Reusable);
    ensure("cancellation releases the acquired image and restores target reset",
           state.mReleaseSwapchainImagesCalls == 1 && owner.resetSwapchainPresentationTargetGeneration() &&
               owner.hasSwapchainImagesGeneration() && !owner.hasSwapchainPresentationTargetGeneration() &&
               !owner.hasSwapchainFrameSlotGeneration() && owner.reset());
}

template<>
template<>
void render_vulkan_instance_test_object::test<85>()
{
    static_assert(noexcept(std::declval<const VulkanInstanceGeneration&>().hasSwapchainPresentationPipelineGeneration()));
    static_assert(noexcept(std::declval<const VulkanInstanceGeneration&>().swapchainPresentationPipelineLayout()));
    static_assert(noexcept(std::declval<const VulkanInstanceGeneration&>().swapchainPresentationPipeline()));
    static_assert(noexcept(std::declval<VulkanInstanceGeneration&>().acquireSwapchainPresentationPipelineGeneration(
        std::declval<const VulkanSwapchainPresentationPipelineRequest&>())));
    static_assert(noexcept(std::declval<VulkanInstanceGeneration&>().resetSwapchainPresentationPipelineGeneration()));

    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));

    VulkanSwapchainPresentationPipelineRequest request = makeSwapchainPresentationPipelineRequest(state, owner);
    request.mInstanceOwnerCheck = {};
    ensureSwapchainPresentationPipelineCode(owner.acquireSwapchainPresentationPipelineGeneration(request),
                                            VulkanSwapchainPresentationPipelineAcquireCode::InvalidInstanceOwnerCheck);
    request = makeSwapchainPresentationPipelineRequest(state, owner);
    request.mWindowGenerationCheck = {};
    ensureSwapchainPresentationPipelineCode(owner.acquireSwapchainPresentationPipelineGeneration(request),
                                            VulkanSwapchainPresentationPipelineAcquireCode::InvalidWindowGenerationCheck);
    request = makeSwapchainPresentationPipelineRequest(state, owner);
    request.mNativeWindowGeneration = 0;
    ensureSwapchainPresentationPipelineCode(owner.acquireSwapchainPresentationPipelineGeneration(request),
                                            VulkanSwapchainPresentationPipelineAcquireCode::InvalidNativeWindowGeneration);
    request = makeSwapchainPresentationPipelineRequest(state, owner);
    request.mDrawableExtent = {};
    ensureSwapchainPresentationPipelineCode(owner.acquireSwapchainPresentationPipelineGeneration(request),
                                            VulkanSwapchainPresentationPipelineAcquireCode::InvalidDrawableExtent);

    acquireSwapchainImagesChain(state, owner);
    ensureSwapchainPresentationPipelineCode(
        owner.acquireSwapchainPresentationPipelineGeneration(makeSwapchainPresentationPipelineRequest(state, owner)),
        VulkanSwapchainPresentationPipelineAcquireCode::SwapchainPresentationTargetNotLive);
    ensure("a rejected acquisition leaves the image owner intact",
           owner.hasSwapchainImagesGeneration() && !owner.hasSwapchainPresentationTargetGeneration() &&
               !owner.hasSwapchainPresentationPipelineGeneration());

    ensure("the exact target can be published before its pipeline",
           !owner.acquireSwapchainPresentationTargetGeneration(makeSwapchainPresentationTargetRequest(state, owner)));
    request = makeSwapchainPresentationPipelineRequest(state, owner);
    request.mNativeWindowGeneration = 41;
    ensureSwapchainPresentationPipelineCode(owner.acquireSwapchainPresentationPipelineGeneration(request),
                                            VulkanSwapchainPresentationPipelineAcquireCode::NativeWindowGenerationMismatch);
    request = makeSwapchainPresentationPipelineRequest(state, owner, { 801, 600 });
    ensureSwapchainPresentationPipelineCode(owner.acquireSwapchainPresentationPipelineGeneration(request),
                                            VulkanSwapchainPresentationPipelineAcquireCode::DrawableExtentMismatch);
    ensure("the exact presentation pipeline publishes its two retained handles",
           !owner.acquireSwapchainPresentationPipelineGeneration(makeSwapchainPresentationPipelineRequest(state, owner)) &&
               owner.hasSwapchainPresentationPipelineGeneration() &&
               owner.swapchainPresentationPipelineLayout() == state.mPresentationPipelineLayout &&
               owner.swapchainPresentationPipeline() == state.mPresentationPipeline &&
               state.mCreateShaderModuleCalls == 2 && state.mDestroyShaderModuleCalls == 2 &&
               state.mCreatePipelineLayoutCalls == 1 && state.mCreateGraphicsPipelineCalls == 1);
    ensureSwapchainPresentationPipelineCode(
        owner.acquireSwapchainPresentationPipelineGeneration(makeSwapchainPresentationPipelineRequest(state, owner)),
        VulkanSwapchainPresentationPipelineAcquireCode::SwapchainPresentationPipelineAlreadyOwned);

    ensure("the pipeline can own a later frame-slot sibling",
           !owner.acquireSwapchainFrameSlotGeneration(makeSwapchainFrameSlotRequest(state, owner)));
    state.mPipelineDestroyOwner = &owner;
    ensure("pipeline reset retires the slot first, then the pipeline, while retaining the exact target",
           owner.resetSwapchainPresentationPipelineGeneration() && owner.hasSwapchainPresentationTargetGeneration() &&
               !owner.hasSwapchainPresentationPipelineGeneration() && !owner.hasSwapchainFrameSlotGeneration() &&
               state.mPipelineDestroyObservationMade && state.mObservedTargetAtPipelineDestroy &&
               !state.mObservedFrameSlotAtPipelineDestroy && state.mDestroyPipelineCalls == 1 &&
               state.mDestroyPipelineLayoutCalls == 1);
    const auto slot_destroy = std::find(state.mEvents.begin(), state.mEvents.end(), Event::DestroyCommandPool);
    const auto pipeline_destroy = std::find(state.mEvents.begin(), state.mEvents.end(), Event::DestroyPipeline);
    ensure("the native destruction trace follows slot before pipeline",
           slot_destroy != state.mEvents.end() && pipeline_destroy != state.mEvents.end() && slot_destroy < pipeline_destroy);

    ensure("the retained target can publish a fresh pipeline and frame slot",
           !owner.acquireSwapchainPresentationPipelineGeneration(makeSwapchainPresentationPipelineRequest(state, owner)) &&
               !owner.acquireSwapchainFrameSlotGeneration(makeSwapchainFrameSlotRequest(state, owner)));
    VulkanInstanceGeneration moved(std::move(owner));
    ensure("move construction transfers the exact pipeline ownership without recreating native objects",
           moved.hasSwapchainPresentationTargetGeneration() && moved.hasSwapchainPresentationPipelineGeneration() &&
               moved.hasSwapchainFrameSlotGeneration() && moved.swapchainPresentationPipeline() == state.mPresentationPipeline &&
               !owner.hasSwapchainPresentationPipelineGeneration() && state.mCreateGraphicsPipelineCalls == 2);
    state.mPipelineDestroyOwner = &moved;
    state.mEvents.clear();
    ensure("target reset retires the complete dependent suffix while retaining the image owner",
           moved.resetSwapchainPresentationTargetGeneration() && moved.hasSwapchainImagesGeneration() &&
               !moved.hasSwapchainPresentationTargetGeneration() &&
               !moved.hasSwapchainPresentationPipelineGeneration() && !moved.hasSwapchainFrameSlotGeneration());
    const auto final_slot_destroy = std::find(state.mEvents.begin(), state.mEvents.end(), Event::DestroyCommandPool);
    const auto final_pipeline_destroy = std::find(state.mEvents.begin(), state.mEvents.end(), Event::DestroyPipeline);
    const auto final_framebuffer_destroy = std::find(state.mEvents.begin(), state.mEvents.end(), Event::DestroyFramebuffer);
    const auto final_render_pass_destroy = std::find(state.mEvents.begin(), state.mEvents.end(), Event::DestroyRenderPass);
    ensure("the full native reset trace is slot, pipeline, framebuffers, then render pass",
           final_slot_destroy != state.mEvents.end() && final_pipeline_destroy != state.mEvents.end() &&
               final_framebuffer_destroy != state.mEvents.end() && final_render_pass_destroy != state.mEvents.end() &&
               final_slot_destroy < final_pipeline_destroy && final_pipeline_destroy < final_framebuffer_destroy &&
               final_framebuffer_destroy < final_render_pass_destroy && moved.reset());
}

template<>
template<>
void render_vulkan_instance_test_object::test<86>()
{
    {
        FakeState       state;
        ScopedFakeState scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        acquireSwapchainPresentationTargetChain(state, owner);
        state.mMissing = MissingCommand::DestroyPipeline;
        const auto pipeline_result =
            owner.acquireSwapchainPresentationPipelineGeneration(makeSwapchainPresentationPipelineRequest(state, owner));
        const VulkanSwapchainPresentationPipelineAcquireError& error =
            requireSwapchainPresentationPipelineError(pipeline_result);
        ensure("a missing pipeline command is nested without native mutation or parent loss",
               error.mCode == VulkanSwapchainPresentationPipelineAcquireCode::ResolutionFailure && error.mResolutionError &&
                   error.mResolutionError->mCode == VulkanSwapchainPresentationPipelineResolutionCode::MissingRequiredCommand &&
                   error.mResolutionError->mCommand == VulkanSwapchainPresentationPipelineCommand::DestroyPipeline &&
                   state.mCreateShaderModuleCalls == 0 && state.mCreatePipelineLayoutCalls == 0 &&
                   state.mCreateGraphicsPipelineCalls == 0 && owner.hasSwapchainPresentationTargetGeneration() &&
                   !owner.hasSwapchainPresentationPipelineGeneration());
    }

    {
        FakeState       state;
        ScopedFakeState scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        acquireSwapchainPresentationTargetChain(state, owner);
        state.mInstanceOwnerCurrent = false;
        ensureSwapchainPresentationPipelineCode(
            owner.acquireSwapchainPresentationPipelineGeneration(makeSwapchainPresentationPipelineRequest(state, owner)),
            VulkanSwapchainPresentationPipelineAcquireCode::StaleInstanceOwner);
        ensure("stale ownership is rejected before any pipeline-native mutation",
               state.mCreateShaderModuleCalls == 0 && state.mCreatePipelineLayoutCalls == 0 &&
                   state.mCreateGraphicsPipelineCalls == 0 && !owner.hasSwapchainPresentationPipelineGeneration());
        state.mInstanceOwnerCurrent = true;
        state.mSurfaceWindowCurrent = false;
        ensureSwapchainPresentationPipelineCode(
            owner.acquireSwapchainPresentationPipelineGeneration(makeSwapchainPresentationPipelineRequest(state, owner)),
            VulkanSwapchainPresentationPipelineAcquireCode::StaleWindowGeneration);
        ensure("stale window ownership is also rejected without native mutation",
               state.mCreateShaderModuleCalls == 0 && state.mCreatePipelineLayoutCalls == 0 &&
                   state.mCreateGraphicsPipelineCalls == 0 && !owner.hasSwapchainPresentationPipelineGeneration());
    }

    {
        FakeState       state;
        ScopedFakeState scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        acquireSwapchainPresentationTargetChain(state, owner);
        ensureSwapchainPresentationPipelineCode(
            VulkanInstanceDetail::acquireSwapchainPresentationPipeline(
                owner, makeSwapchainPresentationPipelineRequest(state, owner), failAllocation),
            VulkanSwapchainPresentationPipelineAcquireCode::AllocationFailure);
        ensure("allocation failure after native resolution rolls the unpublished pipeline back exactly once",
               !owner.hasSwapchainPresentationPipelineGeneration() && owner.hasSwapchainPresentationTargetGeneration() &&
                   state.mCreateShaderModuleCalls == 2 && state.mDestroyShaderModuleCalls == 2 &&
                   state.mCreatePipelineLayoutCalls == 1 && state.mDestroyPipelineLayoutCalls == 1 &&
                   state.mCreateGraphicsPipelineCalls == 1 && state.mDestroyPipelineCalls == 1 && owner.reset());
    }
}

template<>
template<>
void render_vulkan_instance_test_object::test<87>()
{
    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireCompleteSwapchainChain(state, owner);

    state.mMissing = MissingCommand::DestroyPipeline;
    const VulkanSwapchainChainRebuildError error = requireSwapchainChainRebuildError(
        owner.rebuildSwapchainChain(makeSwapchainChainRebuildRequest(state, owner, VkExtent2D{ 1280, 720 })));
    const auto* child = std::get_if<VulkanSwapchainPresentationPipelineAcquireError>(&error.mChildError);
    ensure("aggregate rebuild reports the appended pipeline phase and rolls every new child back",
           error.mCode == VulkanSwapchainChainRebuildCode::ChildFailure &&
               error.mPhase == VulkanSwapchainChainRebuildPhase::PresentationPipeline && child &&
               child->mCode == VulkanSwapchainPresentationPipelineAcquireCode::ResolutionFailure &&
               child->mResolutionError &&
               child->mResolutionError->mCommand == VulkanSwapchainPresentationPipelineCommand::DestroyPipeline &&
               owner.hasLogicalDeviceGeneration() && !owner.hasSwapchainConfigurationGeneration() &&
               !owner.hasSwapchainGeneration() && !owner.hasSwapchainImagesGeneration() &&
               !owner.hasSwapchainPresentationTargetGeneration() &&
               !owner.hasSwapchainPresentationPipelineGeneration() && !owner.hasSwapchainFrameSlotGeneration());

    state.mMissing = MissingCommand::None;
    ensureSwapchainChainRebuildOutcome(
        owner.rebuildSwapchainChain(makeSwapchainChainRebuildRequest(state, owner, VkExtent2D{ 1280, 720 })),
        VulkanSwapchainChainRebuildOutcome::Ready);
    ensure("successful aggregate rebuild publishes the exact pipeline between target and frame slot",
           owner.hasSwapchainPresentationTargetGeneration() && owner.hasSwapchainPresentationPipelineGeneration() &&
               owner.hasSwapchainFrameSlotGeneration() &&
               owner.swapchainPresentationPipelineLayout() == state.mPresentationPipelineLayout &&
               owner.swapchainPresentationPipeline() == state.mPresentationPipeline);

    ensureSwapchainChainRebuildOutcome(
        owner.rebuildSwapchainChain(makeSwapchainChainRebuildRequest(state, owner, VkExtent2D{ 0, 0 })),
        VulkanSwapchainChainRebuildOutcome::Suspended);
    ensure("suspension retires pipeline ownership with every swapchain child",
           owner.hasLogicalDeviceGeneration() && !owner.hasSwapchainConfigurationGeneration() &&
               !owner.hasSwapchainPresentationTargetGeneration() &&
               !owner.hasSwapchainPresentationPipelineGeneration() && !owner.hasSwapchainFrameSlotGeneration() &&
               owner.reset());
}

template<>
template<>
void render_vulkan_instance_test_object::test<88>()
{
    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireSwapchainPresentationTargetChain(state, owner);

    NativeCandidateResetContext guard{ &owner };
    const VulkanSwapchainPresentationPipelineRequest request{
        owner.nativeWindowGeneration(),
        owner.swapchainDrawableExtent(),
        { &guard, nativeCandidateResetOwnerIsCurrent },
        { &guard, nativeCandidateResetWindowIsCurrent }
    };
    const auto pipeline_result = owner.acquireSwapchainPresentationPipelineGeneration(request);
    const VulkanSwapchainPresentationPipelineAcquireError& error =
        requireSwapchainPresentationPipelineError(pipeline_result);
    ensure("post-resolution owner staleness rejects and rolls back the unpublished native pipeline",
           error.mCode == VulkanSwapchainPresentationPipelineAcquireCode::StaleInstanceOwner &&
               guard.mOwnerChecks == 2 && guard.mWindowChecks == 1 &&
               !owner.hasSwapchainPresentationPipelineGeneration() && owner.hasSwapchainPresentationTargetGeneration() &&
               state.mCreateShaderModuleCalls == 2 && state.mDestroyShaderModuleCalls == 2 &&
               state.mCreatePipelineLayoutCalls == 1 && state.mDestroyPipelineLayoutCalls == 1 &&
               state.mCreateGraphicsPipelineCalls == 1 && state.mDestroyPipelineCalls == 1);
    ensure("the live pipeline candidate blocks move, aggregate rebuild, and transitive parent reset",
           guard.mMoveDestination.has_value() && guard.mMoveDestination->instance() == VK_NULL_HANDLE &&
               owner.instance() == state.mInstance && guard.mRebuildError &&
               guard.mRebuildError->mCode == VulkanSwapchainChainRebuildCode::NativeAcquisitionInProgress &&
               guard.mRebuildError->mPhase == VulkanSwapchainChainRebuildPhase::Preflight &&
               guard.mRebuildOwnerChecks == 0 && guard.mRebuildWindowChecks == 0 && guard.mResetAttempted &&
               !guard.mResetSucceeded && owner.hasLogicalDeviceGeneration() && owner.reset());
}

template<>
template<>
void render_vulkan_instance_test_object::test<89>()
{
    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireSwapchainPresentationTargetChain(state, owner);

    const VkPipelineLayout original_layout   = state.mPresentationPipelineLayout;
    const VkPipeline       original_pipeline = state.mPresentationPipeline;
    PipelineAbaAllocationContext replacement{
        &state,
        &owner,
        fakeHandle<VkPipelineLayout>(0xc1aa),
        fakeHandle<VkPipeline>(0xc2aa)
    };
    gPipelineAbaAllocationContext = &replacement;
    const VulkanSwapchainPresentationPipelineAcquireResult result =
        VulkanInstanceDetail::acquireSwapchainPresentationPipeline(
            owner, makeSwapchainPresentationPipelineRequest(state, owner), publishPipelineAtAllocationCheckpoint);
    gPipelineAbaAllocationContext = nullptr;

    ensureSwapchainPresentationPipelineCode(
        result, VulkanSwapchainPresentationPipelineAcquireCode::SwapchainPresentationPipelineAlreadyOwned);
    ensure("allocation-checkpoint ABA publication wins without being overwritten by the older candidate",
           replacement.mInvoked && replacement.mPublished && owner.hasSwapchainPresentationPipelineGeneration() &&
               owner.swapchainPresentationPipelineLayout() == replacement.mReplacementLayout &&
               owner.swapchainPresentationPipeline() == replacement.mReplacementPipeline &&
               state.mCreateShaderModuleCalls == 4 && state.mDestroyShaderModuleCalls == 4 &&
               state.mCreatePipelineLayoutCalls == 2 && state.mCreateGraphicsPipelineCalls == 2 &&
               state.mDestroyPipelineCalls == 1 && state.mDestroyPipelineLayoutCalls == 1 &&
               state.mDestroyedPipelines == std::vector<VkPipeline>{ original_pipeline } &&
               state.mDestroyedPipelineLayouts == std::vector<VkPipelineLayout>{ original_layout });
    ensure("explicit reset destroys only the retained replacement after the stale candidate rollback",
           owner.resetSwapchainPresentationPipelineGeneration() && owner.hasSwapchainPresentationTargetGeneration() &&
               !owner.hasSwapchainPresentationPipelineGeneration() && state.mDestroyPipelineCalls == 2 &&
               state.mDestroyPipelineLayoutCalls == 2 &&
               state.mDestroyedPipelines.back() == replacement.mReplacementPipeline &&
               state.mDestroyedPipelineLayouts.back() == replacement.mReplacementLayout && owner.reset());
}

template<>
template<>
void render_vulkan_instance_test_object::test<90>()
{
    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireSwapchainPresentationTargetChain(state, owner);
    const VkRenderPass original_looking_render_pass = owner.swapchainPresentationRenderPass();

    PipelineParentAbaContext replacement{ &state, &owner };
    const VulkanSwapchainPresentationPipelineRequest request{
        owner.nativeWindowGeneration(),
        owner.swapchainDrawableExtent(),
        { &replacement, pipelineParentAbaOwnerIsCurrent },
        { &replacement, pipelineParentAbaWindowIsCurrent }
    };
    ensureSwapchainPresentationPipelineCode(
        owner.acquireSwapchainPresentationPipelineGeneration(request),
        VulkanSwapchainPresentationPipelineAcquireCode::SwapchainPresentationTargetNotLive);
    ensure("an exact-looking target ABA is rejected by ownership epoch before pipeline-native mutation",
           replacement.mOwnerChecks == 1 && replacement.mReset && replacement.mReacquired &&
               owner.hasSwapchainPresentationTargetGeneration() &&
               owner.swapchainPresentationRenderPass() == original_looking_render_pass &&
               !owner.hasSwapchainPresentationPipelineGeneration() && state.mCreateRenderPassCalls == 2 &&
               state.mDestroyRenderPassCalls == 1 && state.mCreateShaderModuleCalls == 0 &&
               state.mCreatePipelineLayoutCalls == 0 && state.mCreateGraphicsPipelineCalls == 0 && owner.reset());
}

template<>
template<>
void render_vulkan_instance_test_object::test<91>()
{
    static_assert(static_cast<std::uint8_t>(VulkanSwapchainFrameSlotParentOperationCode::OperationFailure) == 16);
    static_assert(static_cast<std::uint8_t>(VulkanSwapchainFrameSlotParentOperationCode::InvalidClearColor) == 17);
    static_assert(static_cast<std::uint8_t>(VulkanSwapchainFrameSlotParentOperationCode::SwapchainPresentationTargetNotLive) == 18);
    static_assert(static_cast<std::uint8_t>(VulkanSwapchainFrameSlotParentOperationCode::SwapchainPresentationPipelineNotLive) == 19);
    static_assert(noexcept(std::declval<VulkanInstanceGeneration&>().acquireRenderPassDrawToPresentSwapchainFrameSlot(
        std::declval<const VulkanSwapchainFrameSlotOperationRequest&>(),
        std::declval<const VulkanSwapchainFrameClearColor&>())));

    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireSwapchainFrameSlotChain(state, owner, { 1280, 720 });
    const VulkanSwapchainFrameSlotOperationRequest request = makeSwapchainFrameSlotOperationRequest(state, owner, { 1280, 720 });

    const std::size_t                                      owner_checks_before  = state.mInstanceOwnerChecks;
    const std::size_t                                      window_checks_before = state.mSurfaceWindowChecks;
    const std::size_t                                      lookups_before       = state.mDeviceProcAddrCalls;
    const VulkanSwapchainFrameClearColor                   invalid_color{ { 0.0f, std::numeric_limits<float>::infinity(), 0.0f, 1.0f } };
    const VulkanSwapchainFrameSlotParentPresentationResult invalid_result =
        owner.acquireRenderPassDrawToPresentSwapchainFrameSlot(request, invalid_color);
    const VulkanSwapchainFrameSlotParentOperationError& invalid_error = requireSwapchainFrameSlotPresentationError(invalid_result);
    ensure("invalid draw clear input wins before render-object validation, callbacks, lookup, or acquisition",
           invalid_error.mCode == VulkanSwapchainFrameSlotParentOperationCode::InvalidClearColor && !invalid_error.mOperationError &&
               state.mInstanceOwnerChecks == owner_checks_before && state.mSurfaceWindowChecks == window_checks_before &&
               state.mDeviceProcAddrCalls == lookups_before && state.mWaitForFencesCalls == 0 && state.mAcquireNextImageCalls == 0);

    const VulkanSwapchainFrameSlotParentPresentationResult target_result =
        owner.acquireRenderPassDrawToPresentSwapchainFrameSlot(request, VulkanSwapchainFrameClearColor{});
    const VulkanSwapchainFrameSlotParentOperationError& target_error = requireSwapchainFrameSlotPresentationError(target_result);
    ensure("draw validation reports the missing target before the missing pipeline and before callbacks",
           target_error.mCode == VulkanSwapchainFrameSlotParentOperationCode::SwapchainPresentationTargetNotLive &&
               !target_error.mOperationError && state.mInstanceOwnerChecks == owner_checks_before &&
               state.mSurfaceWindowChecks == window_checks_before && state.mDeviceProcAddrCalls == lookups_before &&
               state.mWaitForFencesCalls == 0 && state.mAcquireNextImageCalls == 0);

    ensure("the exact target fixture succeeds",
           !owner.acquireSwapchainPresentationTargetGeneration(makeSwapchainPresentationTargetRequest(state, owner, { 1280, 720 })));
    const std::size_t                                      pipeline_owner_checks_before  = state.mInstanceOwnerChecks;
    const std::size_t                                      pipeline_window_checks_before = state.mSurfaceWindowChecks;
    const std::size_t                                      pipeline_lookups_before       = state.mDeviceProcAddrCalls;
    const VulkanSwapchainFrameSlotParentPresentationResult pipeline_result =
        owner.acquireRenderPassDrawToPresentSwapchainFrameSlot(request, VulkanSwapchainFrameClearColor{});
    const VulkanSwapchainFrameSlotParentOperationError& pipeline_error = requireSwapchainFrameSlotPresentationError(pipeline_result);
    ensure("a missing draw pipeline stops before callbacks, dispatch lookup, fence work, or acquisition",
           pipeline_error.mCode == VulkanSwapchainFrameSlotParentOperationCode::SwapchainPresentationPipelineNotLive &&
               !pipeline_error.mOperationError && state.mInstanceOwnerChecks == pipeline_owner_checks_before &&
               state.mSurfaceWindowChecks == pipeline_window_checks_before && state.mDeviceProcAddrCalls == pipeline_lookups_before &&
               state.mWaitForFencesCalls == 0 && state.mAcquireNextImageCalls == 0 && state.mBindPipelineCalls == 0 &&
               state.mDrawCalls == 0);

    ensure("the exact pipeline fixture succeeds",
           !owner.acquireSwapchainPresentationPipelineGeneration(makeSwapchainPresentationPipelineRequest(state, owner, { 1280, 720 })));
    ensure("the frame slot can be removed without disturbing the retained target and pipeline",
           owner.resetSwapchainFrameSlotGeneration() && owner.hasSwapchainPresentationTargetGeneration() &&
               owner.hasSwapchainPresentationPipelineGeneration());
    const VulkanSwapchainFrameSlotParentPresentationResult frame_slot_result =
        owner.acquireRenderPassDrawToPresentSwapchainFrameSlot(request, VulkanSwapchainFrameClearColor{});
    const VulkanSwapchainFrameSlotParentOperationError& frame_slot_error = requireSwapchainFrameSlotPresentationError(frame_slot_result);
    ensure("draw validation reaches the frame slot only after accepting the exact target and pipeline",
           frame_slot_error.mCode == VulkanSwapchainFrameSlotParentOperationCode::SwapchainFrameSlotNotLive &&
               !frame_slot_error.mOperationError && state.mAcquireNextImageCalls == 0 && owner.reset());
}

template<>
template<>
void render_vulkan_instance_test_object::test<92>()
{
    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireCompleteSwapchainChain(state, owner, { 1280, 720 });

    state.mAcquiredImageIndex = 2;
    VulkanSwapchainFrameClearColor                         mutable_color{ { 0.125f, 0.25f, 0.5f, 1.0f } };
    ClearColorMutationContext                              mutation{ &owner, &mutable_color };
    const VulkanSwapchainFrameSlotOperationRequest         request{ owner.nativeWindowGeneration(),
                                                            owner.swapchainDrawableExtent(),
                                                                    { &mutation, clearColorMutationOwnerIsCurrent },
                                                                    { &mutation, clearColorMutationWindowIsCurrent } };
    const VulkanSwapchainFrameSlotParentPresentationResult result =
        owner.acquireRenderPassDrawToPresentSwapchainFrameSlot(request, mutable_color);
    const VulkanSwapchainFrameSlotPresentationSuccess& success = requireSwapchainFrameSlotPresentationSuccess(result);

    const auto begin    = std::find(state.mEvents.begin(), state.mEvents.end(), Event::BeginRenderPass);
    const auto bind     = std::find(state.mEvents.begin(), state.mEvents.end(), Event::BindPipeline);
    const auto viewport = std::find(state.mEvents.begin(), state.mEvents.end(), Event::SetViewport);
    const auto scissor  = std::find(state.mEvents.begin(), state.mEvents.end(), Event::SetScissor);
    const auto draw     = std::find(state.mEvents.begin(), state.mEvents.end(), Event::Draw);
    const auto end      = std::find(state.mEvents.begin(), state.mEvents.end(), Event::EndRenderPass);
    ensure("the instance draw forwards the copied clear, exact render objects, and full-extent draw",
           success.mOutcome == VulkanSwapchainFrameSlotPresentationOutcome::Presented && success.mImageIndex == 2 &&
               mutation.mOwnerChecks == 2 && mutable_color.mRgba == std::array<float, 4>{ 1.0f, 1.0f, 1.0f, 1.0f } &&
               state.mRenderPassClearValue.color.float32[0] == 0.125f && state.mRenderPassClearValue.color.float32[1] == 0.25f &&
               state.mRenderPassClearValue.color.float32[2] == 0.5f && state.mRenderPassClearValue.color.float32[3] == 1.0f &&
               state.mOperationRenderPass == state.mPresentationRenderPass &&
               state.mOperationFramebuffer == state.mPresentationFramebuffers[2] &&
               state.mOperationPipelineBindPoint == VK_PIPELINE_BIND_POINT_GRAPHICS &&
               state.mOperationPipeline == state.mPresentationPipeline && state.mBindPipelineCalls == 1 && state.mSetViewportCalls == 1 &&
               state.mOperationViewport.x == 0.0f && state.mOperationViewport.y == 0.0f && state.mOperationViewport.width == 1280.0f &&
               state.mOperationViewport.height == 720.0f && state.mOperationViewport.minDepth == 0.0f &&
               state.mOperationViewport.maxDepth == 1.0f && state.mSetScissorCalls == 1 && state.mOperationScissor.offset.x == 0 &&
               state.mOperationScissor.offset.y == 0 && state.mOperationScissor.extent.width == 1280 &&
               state.mOperationScissor.extent.height == 720 && state.mDrawCalls == 1 && state.mDrawVertexCount == 3 &&
               state.mDrawInstanceCount == 1 && state.mDrawFirstVertex == 0 && state.mDrawFirstInstance == 0 &&
               begin != state.mEvents.end() && bind != state.mEvents.end() && viewport != state.mEvents.end() &&
               scissor != state.mEvents.end() && draw != state.mEvents.end() && end != state.mEvents.end() && begin < bind &&
               bind < viewport && viewport < scissor && scissor < draw && draw < end);

    state.mEvents.clear();
    state.mFrameSlotDestroyOwner          = &owner;
    state.mPipelineDestroyOwner           = &owner;
    state.mPresentationTargetDestroyOwner = &owner;
    ensure("completed draw work permits target teardown while retaining the image owner",
           owner.resetSwapchainPresentationTargetGeneration() && owner.hasSwapchainImagesGeneration() &&
               !owner.hasSwapchainPresentationTargetGeneration() && !owner.hasSwapchainPresentationPipelineGeneration() &&
               !owner.hasSwapchainFrameSlotGeneration());
    const auto slot_destroy        = std::find(state.mEvents.begin(), state.mEvents.end(), Event::DestroyCommandPool);
    const auto pipeline_destroy    = std::find(state.mEvents.begin(), state.mEvents.end(), Event::DestroyPipeline);
    const auto framebuffer_destroy = std::find(state.mEvents.begin(), state.mEvents.end(), Event::DestroyFramebuffer);
    const auto render_pass_destroy = std::find(state.mEvents.begin(), state.mEvents.end(), Event::DestroyRenderPass);
    ensure("draw-owner destruction remains slot, pipeline, framebuffer, then render pass",
           slot_destroy != state.mEvents.end() && pipeline_destroy != state.mEvents.end() && framebuffer_destroy != state.mEvents.end() &&
               render_pass_destroy != state.mEvents.end() && slot_destroy < pipeline_destroy && pipeline_destroy < framebuffer_destroy &&
               framebuffer_destroy < render_pass_destroy && state.mFrameSlotDestroyObservationMade &&
               state.mObservedPresentationTargetAtFrameSlotDestroy && state.mPipelineDestroyObservationMade &&
               state.mObservedTargetAtPipelineDestroy && !state.mObservedFrameSlotAtPipelineDestroy && owner.reset());
}

template<>
template<>
void render_vulkan_instance_test_object::test<93>()
{
    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireCompleteSwapchainChain(state, owner, { 1280, 720 });

    PresentationPipelineReplacementContext                 replacement{ &state, &owner };
    const VulkanSwapchainFrameSlotOperationRequest         request{ owner.nativeWindowGeneration(),
                                                            owner.swapchainDrawableExtent(),
                                                                    { &replacement, presentationPipelineReplacementOwnerIsCurrent },
                                                                    { &replacement, presentationPipelineReplacementWindowIsCurrent } };
    const VulkanSwapchainFrameSlotParentPresentationResult result =
        owner.acquireRenderPassDrawToPresentSwapchainFrameSlot(request, VulkanSwapchainFrameClearColor{});
    const VulkanSwapchainFrameSlotParentOperationError& error = requireSwapchainFrameSlotPresentationError(result);
    ensure("an exact-looking pipeline and frame-slot ABA is rejected after dispatch resolution and before acquisition",
           error.mCode == VulkanSwapchainFrameSlotParentOperationCode::SwapchainPresentationPipelineNotLive && !error.mOperationError &&
               replacement.mOwnerChecks == 2 && replacement.mResetSucceeded && replacement.mReplacementSucceeded &&
               owner.hasSwapchainPresentationTargetGeneration() && owner.hasSwapchainPresentationPipelineGeneration() &&
               owner.hasSwapchainFrameSlotGeneration() &&
               owner.swapchainPresentationPipelineLayout() == state.mPresentationPipelineLayout &&
               owner.swapchainPresentationPipeline() == state.mPresentationPipeline && state.mCreateGraphicsPipelineCalls == 2 &&
               state.mDestroyPipelineCalls == 1 && state.mCreateCommandPoolCalls == 2 && state.mDestroyCommandPoolCalls == 1 &&
               state.mWaitForFencesCalls == 0 && state.mAcquireNextImageCalls == 0 && state.mDrawCalls == 0 && owner.reset());
}

template<>
template<>
void render_vulkan_instance_test_object::test<94>()
{
    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireCompleteSwapchainChain(state, owner, { 1280, 720 });
    VulkanSwapchainFrameSlotOperationRequest request = makeSwapchainFrameSlotOperationRequest(state, owner, { 1280, 720 });

    state.mAcquiredImageIndex   = 1;
    state.mWaitForFencesResults = { VK_SUCCESS, VK_TIMEOUT, VK_SUCCESS };
    const VulkanSwapchainFrameSlotParentPresentationResult pending_result =
        owner.acquireRenderPassDrawToPresentSwapchainFrameSlot(request, VulkanSwapchainFrameClearColor{});
    const VulkanSwapchainFrameSlotParentOperationError& pending_error = requireSwapchainFrameSlotPresentationError(pending_result);
    ensure("submitted draw work retains every owner and refuses pipeline, target, and transitive parent teardown",
           pending_error.mOperationError && pending_error.mOperationError->mCommand == VulkanSwapchainFrameSlotCommand::WaitForFences &&
               pending_error.mOperationError->mDisposition == VulkanSwapchainFrameSlotDisposition::PresentPending &&
               owner.hasSwapchainFrameSlotGeneration() && owner.hasSwapchainPresentationPipelineGeneration() &&
               owner.hasSwapchainPresentationTargetGeneration() && !owner.resetSwapchainPresentationPipelineGeneration() &&
               !owner.resetSwapchainPresentationTargetGeneration() && !owner.resetSwapchainImagesGeneration() &&
               !owner.resetLogicalDeviceGeneration() && owner.hasLogicalDeviceGeneration() && owner.hasSwapchainConfigurationGeneration() &&
               owner.hasSwapchainGeneration() && owner.hasSwapchainImagesGeneration() && owner.hasSwapchainPresentationTargetGeneration() &&
               owner.hasSwapchainPresentationPipelineGeneration() && owner.hasSwapchainFrameSlotGeneration() &&
               state.mDestroyCommandPoolCalls == 0 && state.mDestroyPipelineCalls == 0 && state.mDestroyFramebufferCalls == 0);

    request.mDrawableExtent                                                  = {};
    const VulkanSwapchainFrameSlotParentPresentationResult completion_result = owner.retrySwapchainFrameSlotPresentationCompletion(request);
    const VulkanSwapchainFrameSlotPresentationSuccess&     completion = requireSwapchainFrameSlotPresentationSuccess(completion_result);
    ensure("presentation completion retires the draw obligation and restores the exact teardown chain",
           completion.mOutcome == VulkanSwapchainFrameSlotPresentationOutcome::Presented && completion.mImageIndex == 1 &&
               owner.swapchainFrameSlotDisposition() == VulkanSwapchainFrameSlotDisposition::Reusable &&
               owner.resetSwapchainPresentationTargetGeneration() && owner.hasSwapchainImagesGeneration() &&
               !owner.hasSwapchainPresentationTargetGeneration() && !owner.hasSwapchainPresentationPipelineGeneration() &&
               !owner.hasSwapchainFrameSlotGeneration());

    ensure(
        "the retained images can republish the draw target, pipeline, and frame slot",
        !owner.acquireSwapchainPresentationTargetGeneration(makeSwapchainPresentationTargetRequest(state, owner, { 1280, 720 })) &&
            !owner.acquireSwapchainPresentationPipelineGeneration(makeSwapchainPresentationPipelineRequest(state, owner, { 1280, 720 })) &&
            !owner.acquireSwapchainFrameSlotGeneration(makeSwapchainFrameSlotRequest(state, owner, { 1280, 720 })));
    state.mAcquiredImageIndex     = 2;
    state.mEndCommandBufferResult = VK_ERROR_UNKNOWN;
    request                       = makeSwapchainFrameSlotOperationRequest(state, owner, { 1280, 720 });
    const VulkanSwapchainFrameSlotParentPresentationResult acquired_result =
        owner.acquireRenderPassDrawToPresentSwapchainFrameSlot(request, VulkanSwapchainFrameClearColor{});
    const VulkanSwapchainFrameSlotParentOperationError& acquired_error = requireSwapchainFrameSlotPresentationError(acquired_result);
    ensure("a post-draw recording failure retains its acquired image and refuses dependent teardown",
           acquired_error.mOperationError &&
               acquired_error.mOperationError->mCommand == VulkanSwapchainFrameSlotCommand::EndCommandBuffer &&
               acquired_error.mOperationError->mDisposition == VulkanSwapchainFrameSlotDisposition::ImageAcquired &&
               acquired_error.mOperationError->mImageIndex == 2 && !owner.resetSwapchainPresentationPipelineGeneration() &&
               !owner.resetSwapchainPresentationTargetGeneration() && !owner.resetSwapchainImagesGeneration() &&
               !owner.resetLogicalDeviceGeneration() && owner.hasLogicalDeviceGeneration() && owner.hasSwapchainImagesGeneration() &&
               owner.hasSwapchainPresentationPipelineGeneration() && owner.hasSwapchainPresentationTargetGeneration() &&
               owner.hasSwapchainFrameSlotGeneration());

    state.mEndCommandBufferResult = VK_SUCCESS;
    request.mDrawableExtent       = {};
    ensureSwapchainFrameSlotOperationSuccess(owner.cancelSwapchainFrameSlotPresentation(request),
                                             VulkanSwapchainFrameSlotDisposition::Reusable);
    ensure("cancellation releases the drawn image and restores dependent teardown",
           state.mReleaseSwapchainImagesCalls == 1 && owner.resetSwapchainPresentationTargetGeneration() &&
               owner.hasSwapchainImagesGeneration() && !owner.hasSwapchainPresentationTargetGeneration() &&
               !owner.hasSwapchainPresentationPipelineGeneration() && !owner.hasSwapchainFrameSlotGeneration() && owner.reset());
}

template<>
template<>
void render_vulkan_instance_test_object::test<95>()
{
    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireSwapchainFrameSlotChain(state, owner, { 1280, 720 });
    VulkanSwapchainFrameSlotOperationRequest request = makeSwapchainFrameSlotOperationRequest(state, owner, { 1280, 720 });
    request.mDrawableExtent                          = {};

    const VulkanSwapchainFrameSlotParentPresentationResult retry_result      = owner.retrySwapchainFrameSlotPresentation(request);
    const VulkanSwapchainFrameSlotParentOperationError&    retry_error       = requireSwapchainFrameSlotPresentationError(retry_result);
    const VulkanSwapchainFrameSlotParentPresentationResult completion_result = owner.retrySwapchainFrameSlotPresentationCompletion(request);
    const VulkanSwapchainFrameSlotParentOperationError&    completion_error = requireSwapchainFrameSlotPresentationError(completion_result);
    const VulkanSwapchainFrameSlotParentOperationResult    cancel_result    = owner.cancelSwapchainFrameSlotPresentation(request);
    const VulkanSwapchainFrameSlotParentOperationError&    cancel_error     = requireSwapchainFrameSlotOperationError(cancel_result);
    ensure("retry, completion, and cancellation stay frame-slot-only when no target or pipeline exists",
           !owner.hasSwapchainPresentationTargetGeneration() && !owner.hasSwapchainPresentationPipelineGeneration() &&
               retry_error.mCode == VulkanSwapchainFrameSlotParentOperationCode::OperationFailure && retry_error.mOperationError &&
               retry_error.mOperationError->mCode == VulkanSwapchainFrameSlotOperationCode::InvalidDisposition &&
               completion_error.mCode == VulkanSwapchainFrameSlotParentOperationCode::OperationFailure &&
               completion_error.mOperationError &&
               completion_error.mOperationError->mCode == VulkanSwapchainFrameSlotOperationCode::InvalidDisposition &&
               cancel_error.mCode == VulkanSwapchainFrameSlotParentOperationCode::OperationFailure && cancel_error.mOperationError &&
               cancel_error.mOperationError->mCode == VulkanSwapchainFrameSlotOperationCode::InvalidDisposition && owner.reset());
}

template<>
template<>
void render_vulkan_instance_test_object::test<96>()
{
    {
        FakeState                state;
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        acquireCompleteSwapchainChain(state, owner, { 1280, 720 });

        PresentationTargetReplacementContext                   replacement{ &state, &owner };
        const VulkanSwapchainFrameSlotOperationRequest         request{ owner.nativeWindowGeneration(),
                                                                owner.swapchainDrawableExtent(),
                                                                        { &replacement, presentationTargetReplacementOwnerIsCurrent },
                                                                        { &replacement, presentationTargetReplacementWindowIsCurrent } };
        const VulkanSwapchainFrameSlotParentPresentationResult result =
            owner.acquireRenderPassDrawToPresentSwapchainFrameSlot(request, VulkanSwapchainFrameClearColor{});
        const VulkanSwapchainFrameSlotParentOperationError& error = requireSwapchainFrameSlotPresentationError(result);
        ensure("draw reports an exact-looking target replacement before the missing replacement pipeline",
               error.mCode == VulkanSwapchainFrameSlotParentOperationCode::SwapchainPresentationTargetNotLive && !error.mOperationError &&
                   replacement.mOwnerChecks == 2 && replacement.mResetSucceeded && replacement.mReplacementSucceeded &&
                   owner.hasSwapchainPresentationTargetGeneration() && !owner.hasSwapchainPresentationPipelineGeneration() &&
                   owner.hasSwapchainFrameSlotGeneration() && state.mWaitForFencesCalls == 0 && state.mAcquireNextImageCalls == 0 &&
                   state.mDrawCalls == 0 && owner.reset());
    }

    {
        FakeState                state;
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        acquireCompleteSwapchainChain(state, owner, { 1280, 720 });

        FrameSlotReplacementContext                            replacement{ &state, &owner };
        const VulkanSwapchainFrameSlotOperationRequest         request{ owner.nativeWindowGeneration(),
                                                                owner.swapchainDrawableExtent(),
                                                                        { &replacement, frameSlotReplacementOwnerIsCurrent },
                                                                        { &replacement, frameSlotReplacementWindowIsCurrent } };
        const VulkanSwapchainFrameSlotParentPresentationResult result =
            owner.acquireRenderPassDrawToPresentSwapchainFrameSlot(request, VulkanSwapchainFrameClearColor{});
        const VulkanSwapchainFrameSlotParentOperationError& error = requireSwapchainFrameSlotPresentationError(result);
        ensure("draw reports an exact-looking frame-slot replacement without blaming the retained pipeline",
               error.mCode == VulkanSwapchainFrameSlotParentOperationCode::SwapchainFrameSlotNotLive && !error.mOperationError &&
                   replacement.mOwnerChecks == 2 && replacement.mResetSucceeded && replacement.mReplacementSucceeded &&
                   owner.hasSwapchainPresentationTargetGeneration() && owner.hasSwapchainPresentationPipelineGeneration() &&
                   owner.hasSwapchainFrameSlotGeneration() && state.mWaitForFencesCalls == 0 && state.mAcquireNextImageCalls == 0 &&
                   state.mDrawCalls == 0 && owner.reset());
    }

    {
        FakeState                state;
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        acquireCompleteSwapchainChain(state, owner, { 1280, 720 });

        OwnerResetDuringOperationContext                       reset{ &owner, owner.nativeWindowGeneration() };
        const VulkanSwapchainFrameSlotOperationRequest         request{ owner.nativeWindowGeneration(),
                                                                owner.swapchainDrawableExtent(),
                                                                        { &reset, ownerResetDuringOperationIsCurrent },
                                                                        { &reset, ownerResetDuringOperationWindowIsCurrent } };
        const VulkanSwapchainFrameSlotParentPresentationResult result =
            owner.acquireRenderPassDrawToPresentSwapchainFrameSlot(request, VulkanSwapchainFrameClearColor{});
        const VulkanSwapchainFrameSlotParentOperationError& error = requireSwapchainFrameSlotPresentationError(result);
        ensure("draw detects a full owner reset after final freshness without dereferencing cleared dispatch",
               error.mCode == VulkanSwapchainFrameSlotParentOperationCode::InstanceNotLive && !error.mOperationError &&
                   reset.mOwnerChecks == 2 && reset.mWindowChecks == 2 && reset.mResetSucceeded && owner.instance() == VK_NULL_HANDLE &&
                   state.mWaitForFencesCalls == 0 && state.mAcquireNextImageCalls == 0 && state.mDrawCalls == 0);
    }
}

template<>
template<>
void render_vulkan_instance_test_object::test<97>()
{
    static_assert(std::is_same_v<decltype(std::declval<VulkanInstanceGeneration&>().acquireSwapchainReadbackGeneration(
                                     std::declval<const VulkanSwapchainReadbackRequest&>())),
                                 VulkanSwapchainReadbackAcquireResult>);
    static_assert(noexcept(std::declval<VulkanInstanceGeneration&>().acquireSwapchainReadbackGeneration(
        std::declval<const VulkanSwapchainReadbackRequest&>())));
    static_assert(noexcept(std::declval<VulkanInstanceGeneration&>().resetSwapchainReadbackGeneration()));

    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireSwapchainImagesChain(state, owner);

    ensure("readback acquisition is independent of presentation target, pipeline, and frame-slot ownership",
           !owner.hasSwapchainPresentationTargetGeneration() && !owner.hasSwapchainPresentationPipelineGeneration() &&
               !owner.hasSwapchainFrameSlotGeneration() &&
               !owner.acquireSwapchainReadbackGeneration(makeSwapchainReadbackRequest(state, owner, { 800, 600 })));
    ensure("the exact mapped destination and byte-layout metadata are published",
           owner.hasSwapchainReadbackGeneration() && owner.swapchainReadbackBuffer() == state.mReadbackBuffer &&
               owner.swapchainReadbackMemory() == state.mReadbackMemory &&
               owner.swapchainReadbackIsMapped() &&
               owner.swapchainReadbackImageFormat() == VK_FORMAT_B8G8R8A8_UNORM && owner.swapchainReadbackImageExtent().width == 800 &&
               owner.swapchainReadbackImageExtent().height == 600 && owner.swapchainReadbackImageCount() == state.mSwapchainImages.size() &&
               owner.swapchainReadbackRowBytes() == 800 * 4 && owner.swapchainReadbackByteCount() == 800 * 600 * 4 &&
               owner.swapchainReadbackAllocationSize() == state.mReadbackMemoryRequirements.size &&
               owner.swapchainReadbackMemoryTypeIndex() == 0 &&
               owner.swapchainReadbackMemoryPropertyFlags() ==
                   (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));
    ensure("the parent creates one exact transfer destination and maps its whole allocation",
           state.mMemoryPropertiesCalls == 1 && state.mCreateBufferCalls == 1 &&
               state.mReadbackBufferCreateInfo.sType == VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO &&
               state.mReadbackBufferCreateInfo.size == 800 * 600 * 4 &&
               state.mReadbackBufferCreateInfo.usage == VK_BUFFER_USAGE_TRANSFER_DST_BIT &&
               state.mReadbackBufferCreateInfo.sharingMode == VK_SHARING_MODE_EXCLUSIVE && state.mAllocateMemoryCalls == 1 &&
               state.mReadbackMemoryAllocateInfo.sType == VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO &&
               state.mReadbackMemoryAllocateInfo.allocationSize == state.mReadbackMemoryRequirements.size &&
               state.mReadbackMemoryAllocateInfo.memoryTypeIndex == 0 && state.mBindBufferMemoryCalls == 1 &&
               state.mReadbackBoundBuffer == state.mReadbackBuffer && state.mReadbackBoundMemory == state.mReadbackMemory &&
               state.mReadbackBindOffset == 0 && state.mMapMemoryCalls == 1 && state.mMappedMemory == state.mReadbackMemory &&
               state.mMappedOffset == 0 && state.mMappedSize == VK_WHOLE_SIZE);
    ensureSwapchainReadbackCode(owner.acquireSwapchainReadbackGeneration(makeSwapchainReadbackRequest(state, owner, { 800, 600 })),
                                VulkanSwapchainReadbackAcquireCode::SwapchainReadbackAlreadyOwned);
    ensure_equals("duplicate acquisition performs no second native creation", state.mCreateBufferCalls, std::size_t{ 1 });

    ensure(
        "the independent presentation siblings can be added around the readback owner",
        !owner.acquireSwapchainPresentationTargetGeneration(makeSwapchainPresentationTargetRequest(state, owner, { 800, 600 })) &&
            !owner.acquireSwapchainPresentationPipelineGeneration(makeSwapchainPresentationPipelineRequest(state, owner, { 800, 600 })) &&
            !owner.acquireSwapchainFrameSlotGeneration(makeSwapchainFrameSlotRequest(state, owner, { 800, 600 })));
    VulkanInstanceGeneration moved(std::move(owner));
    ensure("move construction transfers readback provenance without recreating native resources",
           owner.instance() == VK_NULL_HANDLE && moved.hasSwapchainReadbackGeneration() &&
               moved.swapchainReadbackBuffer() == state.mReadbackBuffer && moved.hasSwapchainPresentationTargetGeneration() &&
               moved.hasSwapchainPresentationPipelineGeneration() && moved.hasSwapchainFrameSlotGeneration() &&
               state.mCreateBufferCalls == 1);

    state.mEvents.clear();
    ensure("explicit readback reset preserves every independent sibling and exact image parent",
           moved.resetSwapchainReadbackGeneration() && !moved.hasSwapchainReadbackGeneration() && moved.hasSwapchainImagesGeneration() &&
               moved.hasSwapchainPresentationTargetGeneration() && moved.hasSwapchainPresentationPipelineGeneration() &&
               moved.hasSwapchainFrameSlotGeneration());
    const auto unmap   = std::find(state.mEvents.begin(), state.mEvents.end(), Event::UnmapMemory);
    const auto destroy = std::find(state.mEvents.begin(), state.mEvents.end(), Event::DestroyBuffer);
    const auto free    = std::find(state.mEvents.begin(), state.mEvents.end(), Event::FreeMemory);
    ensure("readback reset destroys its persistent mapping, buffer, then memory exactly once",
           state.mUnmapMemoryCalls == 1 && state.mDestroyBufferCalls == 1 && state.mFreeMemoryCalls == 1 && unmap != state.mEvents.end() &&
               destroy != state.mEvents.end() && free != state.mEvents.end() && unmap < destroy && destroy < free &&
               moved.resetSwapchainReadbackGeneration() && state.mUnmapMemoryCalls == 1 && state.mDestroyBufferCalls == 1 &&
               state.mFreeMemoryCalls == 1 && moved.reset());
}

template<>
template<>
void render_vulkan_instance_test_object::test<98>()
{
    {
        FakeState                      state;
        ScopedFakeState                scope(state);
        VulkanInstanceGeneration       owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        VulkanSwapchainReadbackRequest request{};
        ensureSwapchainReadbackCode(owner.acquireSwapchainReadbackGeneration(request),
                                    VulkanSwapchainReadbackAcquireCode::InvalidInstanceOwnerCheck);
        request                        = makeSwapchainReadbackRequest(state, owner);
        request.mWindowGenerationCheck = {};
        ensureSwapchainReadbackCode(owner.acquireSwapchainReadbackGeneration(request),
                                    VulkanSwapchainReadbackAcquireCode::InvalidWindowGenerationCheck);
        request                         = makeSwapchainReadbackRequest(state, owner);
        request.mNativeWindowGeneration = 0;
        ensureSwapchainReadbackCode(owner.acquireSwapchainReadbackGeneration(request),
                                    VulkanSwapchainReadbackAcquireCode::InvalidNativeWindowGeneration);
        request                 = makeSwapchainReadbackRequest(state, owner);
        request.mDrawableExtent = {};
        ensureSwapchainReadbackCode(owner.acquireSwapchainReadbackGeneration(request),
                                    VulkanSwapchainReadbackAcquireCode::InvalidDrawableExtent);
        ensure("invalid readback requests perform no native mutation", state.mCreateBufferCalls == 0 && owner.reset());
    }

    {
        FakeState                state;
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        acquireSwapchainImagesChain(state, owner);
        state.mMissing = MissingCommand::UnmapMemory;
        const VulkanSwapchainReadbackAcquireResult result =
            owner.acquireSwapchainReadbackGeneration(makeSwapchainReadbackRequest(state, owner));
        const VulkanSwapchainReadbackAcquireError& error = requireSwapchainReadbackError(result);
        ensure("a missing readback command retains exact nested provenance before native mutation",
               error.mCode == VulkanSwapchainReadbackAcquireCode::ResolutionFailure && error.mResolutionError &&
                   error.mResolutionError->mCode == VulkanSwapchainReadbackResolutionCode::MissingRequiredCommand &&
                   error.mResolutionError->mCommand == VulkanSwapchainReadbackCommand::UnmapMemory && state.mCreateBufferCalls == 0 &&
                   !owner.hasSwapchainReadbackGeneration() && owner.hasSwapchainImagesGeneration() && owner.reset());
    }

    {
        FakeState                state;
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        acquireSwapchainImagesChain(state, owner);
        state.mInstanceOwnerCurrent = false;
        ensureSwapchainReadbackCode(owner.acquireSwapchainReadbackGeneration(makeSwapchainReadbackRequest(state, owner)),
                                    VulkanSwapchainReadbackAcquireCode::StaleInstanceOwner);
        ensure("stale parent ownership is rejected before any readback-native mutation",
               state.mCreateBufferCalls == 0 && !owner.hasSwapchainReadbackGeneration());
        state.mInstanceOwnerCurrent = true;
        ensureSwapchainReadbackCode(
            VulkanInstanceDetail::acquireSwapchainReadback(owner, makeSwapchainReadbackRequest(state, owner), failAllocation),
            VulkanSwapchainReadbackAcquireCode::AllocationFailure);
        ensure("publication allocation failure rolls the mapped candidate back while preserving its image parent",
               !owner.hasSwapchainReadbackGeneration() && owner.hasSwapchainImagesGeneration() && state.mCreateBufferCalls == 1 &&
                   state.mAllocateMemoryCalls == 1 && state.mMapMemoryCalls == 1 && state.mUnmapMemoryCalls == 1 &&
                   state.mDestroyBufferCalls == 1 && state.mFreeMemoryCalls == 1 && owner.reset());
    }
}

template<>
template<>
void render_vulkan_instance_test_object::test<99>()
{
    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireCompleteSwapchainChain(state, owner);
    const VkSurfaceKHR surface                        = owner.surface();
    const VkDevice     device                         = owner.logicalDevice();
    const std::size_t  created_buffers_before_rebuild = state.mCreateBufferCalls;

    state.mMissing                               = MissingCommand::UnmapMemory;
    const VulkanSwapchainChainRebuildError error = requireSwapchainChainRebuildError(
        owner.rebuildSwapchainChain(makeSwapchainChainRebuildRequest(state, owner, VkExtent2D{ 1280, 720 })));
    const auto* child = std::get_if<VulkanSwapchainReadbackAcquireError>(&error.mChildError);
    ensure("aggregate rebuild reports the exact readback phase and nested missing command",
           error.mCode == VulkanSwapchainChainRebuildCode::ChildFailure && error.mPhase == VulkanSwapchainChainRebuildPhase::Readback &&
               child && child->mCode == VulkanSwapchainReadbackAcquireCode::ResolutionFailure && child->mResolutionError &&
               child->mResolutionError->mCode == VulkanSwapchainReadbackResolutionCode::MissingRequiredCommand &&
               child->mResolutionError->mCommand == VulkanSwapchainReadbackCommand::UnmapMemory);
    ensure("readback-phase rollback removes the rebuilt prefix while retaining stable parents",
           owner.surface() == surface && owner.logicalDevice() == device && owner.hasLogicalDeviceGeneration() &&
               !owner.hasSwapchainConfigurationGeneration() && !owner.hasSwapchainGeneration() && !owner.hasSwapchainImagesGeneration() &&
               !owner.hasSwapchainPresentationTargetGeneration() && !owner.hasSwapchainPresentationPipelineGeneration() &&
               !owner.hasSwapchainReadbackGeneration() && !owner.hasSwapchainFrameSlotGeneration() &&
               state.mCreateBufferCalls == created_buffers_before_rebuild);

    state.mMissing = MissingCommand::None;
    ensureSwapchainChainRebuildOutcome(owner.rebuildSwapchainChain(makeSwapchainChainRebuildRequest(state, owner, VkExtent2D{ 1280, 720 })),
                                       VulkanSwapchainChainRebuildOutcome::Ready);
    ensure("a clean retry publishes readback between the independent pipeline and frame slot",
           owner.hasSwapchainPresentationPipelineGeneration() && owner.hasSwapchainReadbackGeneration() &&
               owner.hasSwapchainFrameSlotGeneration() && owner.swapchainReadbackImageExtent().width == 1280 &&
               owner.swapchainReadbackImageExtent().height == 720 && owner.swapchainReadbackRowBytes() == 1280 * 4 &&
               owner.swapchainReadbackByteCount() == 1280 * 720 * 4 && owner.reset());
}

template<>
template<>
void render_vulkan_instance_test_object::test<100>()
{
    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireSwapchainImagesChain(state, owner);

    std::vector<VkImage>     original_images;
    std::vector<VkImageView> original_views;
    for (std::uint32_t index = 0; index < owner.resolvedSwapchainImageCount(); ++index)
    {
        original_images.push_back(owner.swapchainImage(index));
        original_views.push_back(owner.swapchainImageView(index));
    }

    ReadbackParentAbaContext             replacement{ &state, &owner };
    const VulkanSwapchainReadbackRequest request{ owner.nativeWindowGeneration(),
                                                  owner.swapchainDrawableExtent(),
                                                  { &replacement, readbackParentAbaOwnerIsCurrent },
                                                  { &replacement, readbackParentAbaWindowIsCurrent } };
    ensureSwapchainReadbackCode(owner.acquireSwapchainReadbackGeneration(request),
                                VulkanSwapchainReadbackAcquireCode::SwapchainImagesNotLive);

    bool same_looking_images = owner.resolvedSwapchainImageCount() == original_images.size();
    for (std::size_t index = 0; same_looking_images && index < original_images.size(); ++index)
    {
        same_looking_images = owner.swapchainImage(static_cast<std::uint32_t>(index)) == original_images[index] &&
                              owner.swapchainImageView(static_cast<std::uint32_t>(index)) == original_views[index];
    }
    ensure("the readback callback replaces and reacquires its image parent with the same-looking Vulkan handles",
           replacement.mOwnerChecks == 1 && replacement.mWindowChecks == 0 && replacement.mReset && replacement.mReacquired &&
               owner.hasSwapchainImagesGeneration() && same_looking_images && state.mCreateImageViewCalls == 2 * original_views.size() &&
               state.mDestroyImageViewCalls == original_views.size());
    ensure("the image-parent epoch rejects stale readback publication before any readback-native mutation",
           !owner.hasSwapchainReadbackGeneration() && state.mMemoryPropertiesCalls == 0 && state.mCreateBufferCalls == 0 &&
               state.mAllocateMemoryCalls == 0 && state.mBindBufferMemoryCalls == 0 && state.mMapMemoryCalls == 0 &&
               state.mUnmapMemoryCalls == 0 && state.mDestroyBufferCalls == 0 && state.mFreeMemoryCalls == 0 && owner.reset());
}

template<>
template<>
void render_vulkan_instance_test_object::test<101>()
{
    static_assert(static_cast<std::uint8_t>(VulkanSwapchainFrameSlotParentOperationCode::SwapchainReadbackNotLive) == 20);
    static_assert(static_cast<std::uint8_t>(VulkanSwapchainFrameSlotCommand::CmdCopyImageToBuffer) == 25);
    static_assert(noexcept(std::declval<VulkanInstanceGeneration&>().acquireRenderPassDrawReadbackToPresentSwapchainFrameSlot(
        std::declval<const VulkanSwapchainFrameSlotOperationRequest&>())));

    {
        FakeState                state;
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        acquireSwapchainPresentationTargetChain(state, owner, { 64, 64 });
        ensure("the missing-readback fixture publishes its draw pipeline and frame slot",
               !owner.acquireSwapchainPresentationPipelineGeneration(makeSwapchainPresentationPipelineRequest(state, owner, { 64, 64 })) &&
                   !owner.acquireSwapchainFrameSlotGeneration(makeSwapchainFrameSlotRequest(state, owner, { 64, 64 })));

        const std::size_t                                      owner_checks  = state.mInstanceOwnerChecks;
        const std::size_t                                      window_checks = state.mSurfaceWindowChecks;
        const std::size_t                                      lookups       = state.mDeviceProcAddrCalls;
        const VulkanSwapchainFrameSlotParentPresentationResult result = owner.acquireRenderPassDrawReadbackToPresentSwapchainFrameSlot(
            makeSwapchainFrameSlotOperationRequest(state, owner, { 64, 64 }));
        const auto& error = requireSwapchainFrameSlotPresentationError(result);
        ensure("the fixed readback operation rejects a missing owner before callbacks, dispatch, or frame work",
               error.mCode == VulkanSwapchainFrameSlotParentOperationCode::SwapchainReadbackNotLive && !error.mOperationError &&
                   state.mInstanceOwnerChecks == owner_checks && state.mSurfaceWindowChecks == window_checks &&
                   state.mDeviceProcAddrCalls == lookups && state.mWaitForFencesCalls == 0 && state.mAcquireNextImageCalls == 0 &&
                   state.mCopyImageToBufferCalls == 0 && owner.reset());
    }

    {
        constexpr VkExtent2D extent{ 64, 64 };
        FakeState            state;
        prepareReadbackObservationStorage(state, extent);
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        acquireCompleteSwapchainChain(state, owner, extent);
        const auto storage_before = state.mReadbackObservationStorage;

        const VulkanSwapchainFrameSlotParentPresentationResult result = owner.acquireRenderPassDrawToPresentSwapchainFrameSlot(
            makeSwapchainFrameSlotOperationRequest(state, owner, extent), VulkanSwapchainFrameClearColor{});
        const auto& success = requireSwapchainFrameSlotPresentationSuccess(result);
        ensure("the existing draw path returns no observation and never touches readback storage",
               success.mOutcome == VulkanSwapchainFrameSlotPresentationOutcome::Presented && success.mImageIndex == 0 &&
                   !success.mObservation && state.mCopyImageToBufferCalls == 0 && state.mReadbackObservationStorage == storage_before &&
                   owner.reset());
    }
}

template<>
template<>
void render_vulkan_instance_test_object::test<102>()
{
    constexpr VkExtent2D extent{ 64, 64 };
    FakeState            state;
    prepareReadbackObservationStorage(state, extent);
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireCompleteSwapchainChain(state, owner, extent);
    const VkBuffer       original_buffer = owner.swapchainReadbackBuffer();
    const VkDeviceMemory original_memory = owner.swapchainReadbackMemory();

    ReadbackOperationAbaContext replacement{ &state, &owner };
    const VulkanSwapchainFrameSlotOperationRequest request{ owner.nativeWindowGeneration(),
                                                            owner.swapchainDrawableExtent(),
                                                            { &replacement, readbackOperationAbaOwnerIsCurrent },
                                                            { &replacement, readbackOperationAbaWindowIsCurrent } };
    const VulkanSwapchainFrameSlotParentPresentationResult result = owner.acquireRenderPassDrawReadbackToPresentSwapchainFrameSlot(request);
    const auto&                                            error  = requireSwapchainFrameSlotPresentationError(result);

    ensure("post-resolution freshness can replace readback with the same-looking native handles",
           replacement.mOwnerChecks == 2 && replacement.mWindowChecks == 2 && replacement.mReset && replacement.mReacquired &&
               owner.hasSwapchainReadbackGeneration() && owner.swapchainReadbackBuffer() == original_buffer &&
               owner.swapchainReadbackMemory() == original_memory && state.mCreateBufferCalls == 2 && state.mAllocateMemoryCalls == 2 &&
               state.mMapMemoryCalls == 2 && state.mUnmapMemoryCalls == 1 && state.mDestroyBufferCalls == 1 && state.mFreeMemoryCalls == 1);
    ensure("the readback epoch rejects the stale resolved generation before wait, acquire, or copy",
           error.mCode == VulkanSwapchainFrameSlotParentOperationCode::SwapchainReadbackNotLive && !error.mOperationError &&
               state.mWaitForFencesCalls == 0 && state.mAcquireNextImageCalls == 0 && state.mCopyImageToBufferCalls == 0 && owner.reset());
}

template<>
template<>
void render_vulkan_instance_test_object::test<103>()
{
    constexpr VkExtent2D extent{ 64, 64 };
    const std::array     points{ ReadbackResetReentryPoint::WaitForFences, ReadbackResetReentryPoint::AcquireNextImage };
    for (const ReadbackResetReentryPoint point : points)
    {
        FakeState state;
        prepareReadbackObservationStorage(state, extent);
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        acquireCompleteSwapchainChain(state, owner, extent);
        state.mReadbackResetReentryPoint = point;
        state.mReadbackResetReentryOwner = &owner;

        const VulkanSwapchainFrameSlotParentPresentationResult result =
            owner.acquireRenderPassDrawReadbackToPresentSwapchainFrameSlot(makeSwapchainFrameSlotOperationRequest(state, owner, extent));
        const auto& success = requireSwapchainFrameSlotPresentationSuccess(result);
        ensure("the active readback identity refuses every direct and transitive reentrant reset while still Reusable",
               state.mReadbackResetReentryInvoked &&
                   state.mReadbackResetReentryDisposition == VulkanSwapchainFrameSlotDisposition::Reusable &&
                   !state.mReenteredReadbackResetSucceeded && !state.mReenteredFrameSlotResetSucceeded &&
                   !state.mReenteredImagesResetSucceeded && owner.hasSwapchainReadbackGeneration() &&
                   owner.hasSwapchainFrameSlotGeneration() && state.mUnmapMemoryCalls == 0 && state.mDestroyBufferCalls == 0 &&
                   state.mFreeMemoryCalls == 0 && state.mDestroyCommandPoolCalls == 0 && state.mDestroyImageViewCalls == 0);
        ensure("a mismatched sentinel frame is still a successful bounded observation",
               success.mOutcome == VulkanSwapchainFrameSlotPresentationOutcome::Presented && success.mImageIndex == 0 &&
                   success.mObservation && success.mObservation->mImageFormat == VK_FORMAT_B8G8R8A8_UNORM &&
                   success.mObservation->mImageExtent.width == extent.width && success.mObservation->mImageExtent.height == extent.height &&
                   success.mObservation->mTotalPixelCount == 64 * 64 && success.mObservation->mGreenPixelCount == 0 &&
                   success.mObservation->mRedPixelCount == 0 && success.mObservation->mUnexpectedPixelCount == 64 * 64 &&
                   state.mCopyImageToBufferCalls == 1);

        ensure("normal completion releases the readback identity for one exact transitive teardown",
               owner.resetSwapchainImagesGeneration() && !owner.hasSwapchainImagesGeneration() && !owner.hasSwapchainReadbackGeneration() &&
                   !owner.hasSwapchainFrameSlotGeneration() && state.mUnmapMemoryCalls == 1 && state.mDestroyBufferCalls == 1 &&
                   state.mFreeMemoryCalls == 1 && state.mDestroyCommandPoolCalls == 1 &&
                   state.mDestroyImageViewCalls == state.mSwapchainImages.size());
        ensure("repeating the completed teardown destroys no resource twice",
               owner.resetSwapchainImagesGeneration() && state.mUnmapMemoryCalls == 1 && state.mDestroyBufferCalls == 1 &&
                   state.mFreeMemoryCalls == 1 && state.mDestroyCommandPoolCalls == 1 &&
                   state.mDestroyImageViewCalls == state.mSwapchainImages.size() && owner.reset());
    }
}

template<>
template<>
void render_vulkan_instance_test_object::test<104>()
{
    constexpr VkExtent2D extent{ 64, 64 };
    {
        FakeState state;
        prepareReadbackObservationStorage(state, extent);
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        acquireCompleteSwapchainChain(state, owner, extent);
        state.mEndCommandBufferResult = VK_ERROR_UNKNOWN;

        VulkanSwapchainFrameSlotOperationRequest               request = makeSwapchainFrameSlotOperationRequest(state, owner, extent);
        const VulkanSwapchainFrameSlotParentPresentationResult result =
            owner.acquireRenderPassDrawReadbackToPresentSwapchainFrameSlot(request);
        const auto& error = requireSwapchainFrameSlotPresentationError(result);
        ensure("an acquired readback frame retains every dependent owner through a recording failure",
               error.mOperationError && error.mOperationError->mCommand == VulkanSwapchainFrameSlotCommand::EndCommandBuffer &&
                   error.mOperationError->mDisposition == VulkanSwapchainFrameSlotDisposition::ImageAcquired &&
                   !owner.resetSwapchainReadbackGeneration() && !owner.resetSwapchainFrameSlotGeneration() &&
                   !owner.resetSwapchainImagesGeneration() && state.mUnmapMemoryCalls == 0 && state.mDestroyBufferCalls == 0 &&
                   state.mFreeMemoryCalls == 0 && state.mDestroyCommandPoolCalls == 0 && state.mDestroyImageViewCalls == 0);

        state.mEndCommandBufferResult = VK_SUCCESS;
        request.mDrawableExtent       = {};
        ensureSwapchainFrameSlotOperationSuccess(owner.cancelSwapchainFrameSlotPresentation(request),
                                                 VulkanSwapchainFrameSlotDisposition::Reusable);
        ensure("fully completed cancellation releases direct readback and frame-slot resets exactly once",
               owner.resetSwapchainReadbackGeneration() && owner.resetSwapchainReadbackGeneration() &&
                   owner.resetSwapchainFrameSlotGeneration() && owner.resetSwapchainFrameSlotGeneration() && state.mUnmapMemoryCalls == 1 &&
                   state.mDestroyBufferCalls == 1 && state.mFreeMemoryCalls == 1 && state.mDestroyCommandPoolCalls == 1 &&
                   owner.resetSwapchainImagesGeneration() && state.mDestroyImageViewCalls == state.mSwapchainImages.size() &&
                   owner.reset());
    }

    {
        FakeState state;
        prepareReadbackObservationStorage(state, extent);
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        acquireCompleteSwapchainChain(state, owner, extent);
        state.mWaitForFencesResults = { VK_SUCCESS, VK_TIMEOUT, VK_SUCCESS };

        const VulkanSwapchainFrameSlotOperationRequest request = makeSwapchainFrameSlotOperationRequest(state, owner, extent);
        const auto result = owner.acquireRenderPassDrawReadbackToPresentSwapchainFrameSlot(request);
        const auto& pending_error = requireSwapchainFrameSlotPresentationError(result);
        ensure("the readback frame reaches an active PresentPending obligation",
               pending_error.mOperationError && pending_error.mOperationError->mCommand == VulkanSwapchainFrameSlotCommand::WaitForFences &&
                   pending_error.mOperationError->mDisposition == VulkanSwapchainFrameSlotDisposition::PresentPending &&
                   !owner.resetSwapchainReadbackGeneration() && !owner.resetSwapchainFrameSlotGeneration() &&
                   !owner.resetSwapchainImagesGeneration());

        const auto  rebuild_result = owner.rebuildSwapchainChain(makeSwapchainChainRebuildRequest(state, owner, extent));
        const auto& rebuild_error  = requireSwapchainChainRebuildError(rebuild_result);
        ensure("aggregate rebuild reports the active frame-slot reset refusal without destroying readback resources",
               rebuild_error.mCode == VulkanSwapchainChainRebuildCode::FrameSlotResetRefused &&
                   rebuild_error.mPhase == VulkanSwapchainChainRebuildPhase::Preflight &&
                   rebuild_error.mFrameSlotDisposition == VulkanSwapchainFrameSlotDisposition::PresentPending &&
                   state.mUnmapMemoryCalls == 0 && state.mDestroyBufferCalls == 0 && state.mFreeMemoryCalls == 0 &&
                   state.mDestroyCommandPoolCalls == 0 && state.mDestroyImageViewCalls == 0);

        const auto completion_result = owner.retrySwapchainFrameSlotPresentationCompletion(makeExactFrameSlotOperationRequest(owner));
        const auto& completion = requireSwapchainFrameSlotPresentationSuccess(completion_result);
        ensure("completion after the refused rebuild returns the one retained observation and restores teardown",
               completion.mObservation && completion.mObservation->mUnexpectedPixelCount == 64 * 64 &&
                   owner.resetSwapchainImagesGeneration() && state.mUnmapMemoryCalls == 1 && state.mDestroyBufferCalls == 1 &&
                   state.mFreeMemoryCalls == 1 && owner.reset());
    }
}

template<>
template<>
void render_vulkan_instance_test_object::test<105>()
{
    constexpr VkExtent2D extent{ 64, 64 };
    FakeState            state;
    prepareReadbackObservationStorage(state, extent);
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireCompleteSwapchainChain(state, owner, extent);
    state.mWaitForFencesResults = { VK_SUCCESS, VK_TIMEOUT, VK_SUCCESS };

    const auto pending_result = owner.acquireRenderPassDrawReadbackToPresentSwapchainFrameSlot(
        makeSwapchainFrameSlotOperationRequest(state, owner, extent));
    const auto& pending_error = requireSwapchainFrameSlotPresentationError(pending_result);
    ensure("the move fixture holds one submitted readback frame",
           pending_error.mOperationError &&
               pending_error.mOperationError->mDisposition == VulkanSwapchainFrameSlotDisposition::PresentPending);

    const VkCommandPool  command_pool    = owner.swapchainFrameCommandPool();
    const VkBuffer       readback_buffer = owner.swapchainReadbackBuffer();
    const VkDeviceMemory readback_memory = owner.swapchainReadbackMemory();
    VulkanInstanceGeneration moved(std::move(owner));
    ensure("aggregate move preserves the live child objects and empties only the source owner",
           owner.instance() == VK_NULL_HANDLE && !owner.hasSwapchainFrameSlotGeneration() &&
               !owner.hasSwapchainReadbackGeneration() && moved.instance() == state.mInstance &&
               moved.hasSwapchainFrameSlotGeneration() && moved.hasSwapchainReadbackGeneration() &&
               moved.swapchainFrameCommandPool() == command_pool && moved.swapchainReadbackBuffer() == readback_buffer &&
               moved.swapchainReadbackMemory() == readback_memory && state.mDestroyCommandPoolCalls == 0 &&
               state.mUnmapMemoryCalls == 0 && state.mDestroyBufferCalls == 0 && state.mFreeMemoryCalls == 0);

    const auto completion_result = moved.retrySwapchainFrameSlotPresentationCompletion(makeExactFrameSlotOperationRequest(moved));
    const auto& completion = requireSwapchainFrameSlotPresentationSuccess(completion_result);
    ensure("the moved-to owner retires the frame with exactly one successful mismatch observation",
           completion.mOutcome == VulkanSwapchainFrameSlotPresentationOutcome::Presented && completion.mObservation &&
               completion.mObservation->mTotalPixelCount == 64 * 64 && completion.mObservation->mGreenPixelCount == 0 &&
               completion.mObservation->mRedPixelCount == 0 && completion.mObservation->mUnexpectedPixelCount == 64 * 64 &&
               state.mCopyImageToBufferCalls == 1);

    const auto duplicate_result = moved.retrySwapchainFrameSlotPresentationCompletion(makeExactFrameSlotOperationRequest(moved));
    const auto& duplicate_error = requireSwapchainFrameSlotPresentationError(duplicate_result);
    ensure("a second completion returns no observation and performs no second copy or teardown",
           duplicate_error.mCode == VulkanSwapchainFrameSlotParentOperationCode::OperationFailure &&
               duplicate_error.mOperationError &&
               duplicate_error.mOperationError->mCode == VulkanSwapchainFrameSlotOperationCode::InvalidDisposition &&
               state.mCopyImageToBufferCalls == 1 && state.mUnmapMemoryCalls == 0 && state.mDestroyBufferCalls == 0 &&
               state.mFreeMemoryCalls == 0 && moved.resetSwapchainImagesGeneration() && state.mUnmapMemoryCalls == 1 &&
               state.mDestroyBufferCalls == 1 && state.mFreeMemoryCalls == 1 && moved.reset());
}

template<>
template<>
void render_vulkan_instance_test_object::test<106>()
{
    static_assert(noexcept(std::declval<const VulkanInstanceGeneration&>().hasUploadSourceGeneration()));
    static_assert(noexcept(std::declval<const VulkanInstanceGeneration&>().uploadSourceResourceHandle()));
    static_assert(noexcept(std::declval<const VulkanInstanceGeneration&>().uploadSourceContentIdentity()));
    static_assert(noexcept(std::declval<const VulkanInstanceGeneration&>().uploadSourceBuffer()));
    static_assert(noexcept(std::declval<const VulkanInstanceGeneration&>().uploadSourceMemory()));
    static_assert(noexcept(
        std::declval<VulkanInstanceGeneration&>().acquireUploadSourceGeneration(std::declval<const VulkanUploadSourceRequest&>())));
    static_assert(noexcept(std::declval<VulkanInstanceGeneration&>().resetUploadSourceGeneration()));

    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    ensure("an absent upload source exposes only empty metadata",
           !owner.hasUploadSourceGeneration() && !owner.uploadSourceResourceHandle() && owner.uploadSourceContentIdentity() == 0 &&
               owner.uploadSourceBuffer() == VK_NULL_HANDLE && owner.uploadSourceMemory() == VK_NULL_HANDLE &&
               owner.uploadSourceByteCount() == 0 && owner.uploadSourceAllocationSize() == 0 && owner.uploadSourceMemoryTypeIndex() == 0 &&
               owner.uploadSourceMemoryPropertyFlags() == 0 && !owner.uploadSourceIsCoherent());
    ensureUploadSourceCode(owner.acquireUploadSourceGeneration(makeUploadSourceRequest(state, owner)),
                           VulkanUploadSourceAcquireCode::SurfaceNotLive);

    acquireLogicalChain(state, owner);
    VulkanUploadSourceRequest request = makeUploadSourceRequest(state, owner);
    request.mInstanceOwnerCheck       = {};
    ensureUploadSourceCode(owner.acquireUploadSourceGeneration(request), VulkanUploadSourceAcquireCode::InvalidInstanceOwnerCheck);
    request                        = makeUploadSourceRequest(state, owner);
    request.mWindowGenerationCheck = {};
    ensureUploadSourceCode(owner.acquireUploadSourceGeneration(request), VulkanUploadSourceAcquireCode::InvalidWindowGenerationCheck);
    request                         = makeUploadSourceRequest(state, owner);
    request.mNativeWindowGeneration = 0;
    ensureUploadSourceCode(owner.acquireUploadSourceGeneration(request), VulkanUploadSourceAcquireCode::InvalidNativeWindowGeneration);
    request                         = makeUploadSourceRequest(state, owner);
    request.mNativeWindowGeneration = 41;
    ensureUploadSourceCode(owner.acquireUploadSourceGeneration(request), VulkanUploadSourceAcquireCode::NativeWindowGenerationMismatch);

    request                     = makeUploadSourceRequest(state, owner);
    state.mInstanceOwnerCurrent = false;
    ensureUploadSourceCode(owner.acquireUploadSourceGeneration(request), VulkanUploadSourceAcquireCode::StaleInstanceOwner);
    state.mInstanceOwnerCurrent = true;
    state.mSurfaceWindowCurrent = false;
    ensureUploadSourceCode(owner.acquireUploadSourceGeneration(request), VulkanUploadSourceAcquireCode::StaleWindowGeneration);
    state.mSurfaceWindowCurrent = true;

    request                                                          = makeUploadSourceRequest(state, owner);
    request.mDescription.mHandle                                     = {};
    const VulkanUploadSourceAcquireResult invalid_description_result = owner.acquireUploadSourceGeneration(request);
    const VulkanUploadSourceAcquireError& invalid_description        = requireUploadSourceError(invalid_description_result);
    ensure("the core remains the one description-validation authority without native allocation",
           invalid_description.mCode == VulkanUploadSourceAcquireCode::ResolutionFailure && invalid_description.mResolutionError &&
               invalid_description.mResolutionError->mCode == VulkanUploadSourceResolutionCode::InvalidDescription &&
               state.mCreateBufferCalls == 0 && state.mAllocateMemoryCalls == 0);

    const VulkanUploadSourceDescription description = makeUploadSourceDescription();
    request                                         = makeUploadSourceRequest(state, owner, description);
    ensure("one exact immutable source publishes", !owner.acquireUploadSourceGeneration(request));
    ensure(
        "the aggregate exposes the authenticated source identity and allocation metadata",
        owner.hasUploadSourceGeneration() && owner.uploadSourceResourceHandle() == description.mHandle &&
            owner.uploadSourceContentIdentity() != 0 && owner.uploadSourceBuffer() == state.mUploadSourceBuffer &&
            owner.uploadSourceMemory() == state.mUploadSourceMemory && owner.uploadSourceByteCount() == VULKAN_UPLOAD_SOURCE_BYTE_COUNT &&
            owner.uploadSourceAllocationSize() == state.mUploadSourceMemoryRequirements.size && owner.uploadSourceMemoryTypeIndex() == 0 &&
            owner.uploadSourceMemoryPropertyFlags() == (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) &&
            owner.uploadSourceIsCoherent());
    ensure("publication used one exclusive transfer source and copied the exact 48 bytes before unmapping",
           state.mCreateBufferCalls == 1 && state.mUploadSourceBufferCreateInfo.sType == VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO &&
               state.mUploadSourceBufferCreateInfo.size == VULKAN_UPLOAD_SOURCE_BYTE_COUNT &&
               state.mUploadSourceBufferCreateInfo.usage == VK_BUFFER_USAGE_TRANSFER_SRC_BIT &&
               state.mUploadSourceBufferCreateInfo.sharingMode == VK_SHARING_MODE_EXCLUSIVE && state.mAllocateMemoryCalls == 1 &&
               state.mUploadSourceMemoryAllocateInfo.allocationSize == state.mUploadSourceMemoryRequirements.size &&
               state.mUploadSourceBoundBuffer == state.mUploadSourceBuffer && state.mUploadSourceBoundMemory == state.mUploadSourceMemory &&
               state.mUploadSourceBindOffset == 0 && state.mMapMemoryCalls == 1 && state.mUnmapMemoryCalls == 1 &&
               state.mUploadSourceMappedStorage == description.mBytes && state.mFlushMappedMemoryRangesCalls == 0 &&
               state.mQueueSubmitCalls == 0);

    const std::size_t create_calls = state.mCreateBufferCalls;
    ensureUploadSourceCode(owner.acquireUploadSourceGeneration(request), VulkanUploadSourceAcquireCode::UploadSourceAlreadyOwned);
    ensure("duplicate ownership is rejected before callbacks or native creation",
           state.mCreateBufferCalls == create_calls && owner.reset());
}

template<>
template<>
void render_vulkan_instance_test_object::test<107>()
{
    {
        FakeState                state;
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        acquireLogicalChain(state, owner);
        state.mFailInstanceOwnerCheck = state.mInstanceOwnerChecks + 2;

        const VulkanUploadSourceAcquireResult result = owner.acquireUploadSourceGeneration(makeUploadSourceRequest(state, owner));
        ensureUploadSourceCode(result, VulkanUploadSourceAcquireCode::StaleInstanceOwner);
        ensure("post-resolution freshness rolls the complete unpublished candidate back",
               !owner.hasUploadSourceGeneration() && state.mCreateBufferCalls == 1 && state.mAllocateMemoryCalls == 1 &&
                   state.mMapMemoryCalls == 1 && state.mUnmapMemoryCalls == 1 && state.mDestroyBufferCalls == 1 &&
                   state.mFreeMemoryCalls == 1 && state.mQueueSubmitCalls == 0);

        state.mFailInstanceOwnerCheck = 0;
        ensure("the logical-device parent remains reusable after freshness rollback",
               !owner.acquireUploadSourceGeneration(makeUploadSourceRequest(state, owner)) && owner.hasUploadSourceGeneration() &&
                   state.mCreateBufferCalls == 2 && state.mAllocateMemoryCalls == 2 && owner.reset());
    }

    {
        FakeState                state;
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        acquireLogicalChain(state, owner);

        const VulkanUploadSourceAcquireResult result =
            VulkanInstanceDetail::acquireUploadSource(owner, makeUploadSourceRequest(state, owner), failAllocation);
        ensureUploadSourceCode(result, VulkanUploadSourceAcquireCode::AllocationFailure);
        ensure("a parent allocation failure destroys the fully resolved native candidate without publication",
               !owner.hasUploadSourceGeneration() && state.mCreateBufferCalls == 1 && state.mAllocateMemoryCalls == 1 &&
                   state.mMapMemoryCalls == 1 && state.mUnmapMemoryCalls == 1 && state.mDestroyBufferCalls == 1 &&
                   state.mFreeMemoryCalls == 1 && state.mQueueSubmitCalls == 0);
        ensure("allocation rollback leaves the exact parent reusable",
               !owner.acquireUploadSourceGeneration(makeUploadSourceRequest(state, owner)) && owner.hasUploadSourceGeneration() &&
                   owner.reset());
    }
}

template<>
template<>
void render_vulkan_instance_test_object::test<108>()
{
    {
        FakeState                state;
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        acquireLogicalChain(state, owner);

        UploadSourceLogicalAbaContext   context{ &owner };
        const VulkanUploadSourceRequest request{ owner.nativeWindowGeneration(),
                                                 makeUploadSourceDescription(),
                                                 { &context, uploadSourceLogicalAbaOwnerIsCurrent },
                                                 { &context, uploadSourceLogicalAbaWindowIsCurrent } };
        ensureUploadSourceCode(owner.acquireUploadSourceGeneration(request), VulkanUploadSourceAcquireCode::LogicalDeviceNotLive);
        ensure("the ownership epoch rejects a same-handle logical-device ABA before native source creation",
               context.mOwnerChecks == 1 && context.mReset && context.mReacquired && owner.hasLogicalDeviceGeneration() &&
                   owner.logicalDevice() == state.mDevice && !owner.hasUploadSourceGeneration() && state.mCreateBufferCalls == 0);
        ensure("the replacement logical generation accepts a freshly authenticated request",
               !owner.acquireUploadSourceGeneration(makeUploadSourceRequest(state, owner)) && owner.reset());
    }

    {
        FakeState                state;
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        acquireLogicalChain(state, owner);
        const VkBuffer       original_buffer = state.mUploadSourceBuffer;
        const VkDeviceMemory original_memory = state.mUploadSourceMemory;

        VulkanUploadSourceDescription replacement_description = makeUploadSourceDescription();
        replacement_description.mHandle                       = { 9, 4 };
        for (std::uint8_t& byte : replacement_description.mBytes)
        {
            byte ^= 0xa5;
        }
        UploadSourcePublicationContext  context{ &state, &owner, replacement_description, fakeHandle<VkBuffer>(0xd111),
                                                fakeHandle<VkDeviceMemory>(0xd112) };
        const VulkanUploadSourceRequest request{ owner.nativeWindowGeneration(),
                                                 makeUploadSourceDescription(),
                                                 { &context, uploadSourcePublicationOwnerIsCurrent },
                                                 { &context, uploadSourcePublicationWindowIsCurrent } };
        ensureUploadSourceCode(owner.acquireUploadSourceGeneration(request), VulkanUploadSourceAcquireCode::UploadSourceAlreadyOwned);
        ensure("a competing exact publication wins without being overwritten by the older candidate",
               context.mAttempted && context.mPublished && context.mOwnerChecks == 2 && context.mWindowChecks == 1 &&
                   owner.hasUploadSourceGeneration() && owner.uploadSourceResourceHandle() == replacement_description.mHandle &&
                   owner.uploadSourceBuffer() == context.mReplacementBuffer && owner.uploadSourceMemory() == context.mReplacementMemory &&
                   owner.uploadSourceContentIdentity() != 0 && state.mUploadSourceMappedStorage == replacement_description.mBytes);
        ensure("the losing candidate alone is rolled back and no queue work enters publication",
               state.mCreateBufferCalls == 2 && state.mAllocateMemoryCalls == 2 && state.mMapMemoryCalls == 2 &&
                   state.mUnmapMemoryCalls == 2 && state.mDestroyedBuffers.size() == 1 &&
                   state.mDestroyedBuffers.front() == original_buffer && state.mFreedMemories.size() == 1 &&
                   state.mFreedMemories.front() == original_memory && state.mQueueSubmitCalls == 0 && owner.reset());
    }
}

template<>
template<>
void render_vulkan_instance_test_object::test<109>()
{
    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireUploadSourceChain(state, owner);
    acquireCompleteSwapchainChildren(state, owner);

    const LLRenderContract::BufferHandle resource_handle  = owner.uploadSourceResourceHandle();
    const std::uint64_t                  content_identity = owner.uploadSourceContentIdentity();
    const VkBuffer                       buffer           = owner.uploadSourceBuffer();
    const VkDeviceMemory                 memory           = owner.uploadSourceMemory();
    const VkDeviceSize                   allocation_size  = owner.uploadSourceAllocationSize();
    const std::uint32_t                  memory_type      = owner.uploadSourceMemoryTypeIndex();
    const VkMemoryPropertyFlags          memory_flags     = owner.uploadSourceMemoryPropertyFlags();
    const bool                           coherent         = owner.uploadSourceIsCoherent();
    const VulkanUploadSourceBytes        uploaded_bytes   = state.mUploadSourceMappedStorage;
    const std::size_t                    create_calls     = state.mCreateBufferCalls;
    state.mDestroyedBuffers.clear();
    state.mFreedMemories.clear();

    ensureSwapchainChainRebuildOutcome(owner.rebuildSwapchainChain(makeSwapchainChainRebuildRequest(state, owner, VkExtent2D{ 1280, 720 })),
                                       VulkanSwapchainChainRebuildOutcome::Ready);
    ensure("a changed-extent swapchain rebuild preserves the exact device-scoped upload generation",
           owner.hasUploadSourceGeneration() && owner.uploadSourceResourceHandle() == resource_handle &&
               owner.uploadSourceContentIdentity() == content_identity && content_identity != 0 && owner.uploadSourceBuffer() == buffer &&
               owner.uploadSourceMemory() == memory && owner.uploadSourceByteCount() == VULKAN_UPLOAD_SOURCE_BYTE_COUNT &&
               owner.uploadSourceAllocationSize() == allocation_size && owner.uploadSourceMemoryTypeIndex() == memory_type &&
               owner.uploadSourceMemoryPropertyFlags() == memory_flags && state.mUploadSourceMappedStorage == uploaded_bytes);
    ensure("rebuild creates only its replacement readback buffer and never retires or resubmits the upload source",
           state.mCreateBufferCalls == create_calls + 1 &&
               std::find(state.mDestroyedBuffers.begin(), state.mDestroyedBuffers.end(), buffer) == state.mDestroyedBuffers.end() &&
               std::find(state.mFreedMemories.begin(), state.mFreedMemories.end(), memory) == state.mFreedMemories.end() &&
               state.mQueueSubmitCalls == 0);

    state.mDestroyedBuffers.clear();
    state.mFreedMemories.clear();
    state.mSwapchainCreateResult = VK_ERROR_OUT_OF_HOST_MEMORY;
    const VulkanSwapchainChainRebuildResult failed_rebuild_result =
        owner.rebuildSwapchainChain(makeSwapchainChainRebuildRequest(state, owner, VkExtent2D{ 1440, 810 }));
    const VulkanSwapchainChainRebuildError& failed_rebuild = requireSwapchainChainRebuildError(failed_rebuild_result);
    ensure("the forced rebuild failure is retained at the exact swapchain phase",
           failed_rebuild.mCode == VulkanSwapchainChainRebuildCode::ChildFailure &&
               failed_rebuild.mPhase == VulkanSwapchainChainRebuildPhase::Swapchain &&
               std::holds_alternative<VulkanSwapchainAcquireError>(failed_rebuild.mChildError));
    ensure("failed rebuild rollback preserves every exact upload-source identity and allocation field",
           owner.hasUploadSourceGeneration() && owner.uploadSourceResourceHandle() == resource_handle &&
               owner.uploadSourceContentIdentity() == content_identity && owner.uploadSourceBuffer() == buffer &&
               owner.uploadSourceMemory() == memory && owner.uploadSourceByteCount() == VULKAN_UPLOAD_SOURCE_BYTE_COUNT &&
               owner.uploadSourceAllocationSize() == allocation_size && owner.uploadSourceMemoryTypeIndex() == memory_type &&
               owner.uploadSourceMemoryPropertyFlags() == memory_flags && owner.uploadSourceIsCoherent() == coherent &&
               state.mUploadSourceMappedStorage == uploaded_bytes &&
               std::find(state.mDestroyedBuffers.begin(), state.mDestroyedBuffers.end(), buffer) == state.mDestroyedBuffers.end() &&
               std::find(state.mFreedMemories.begin(), state.mFreedMemories.end(), memory) == state.mFreedMemories.end() &&
               state.mQueueSubmitCalls == 0);
    ensure("the failed-rebuild fixture remains ordinarily tear-downable", owner.reset());
}

template<>
template<>
void render_vulkan_instance_test_object::test<110>()
{
    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireUploadSourceChain(state, owner);
    acquireCompleteSwapchainChildren(state, owner);

    ensure("direct upload reset preserves the complete live drawable-dependent sibling chain",
           owner.resetUploadSourceGeneration() && !owner.hasUploadSourceGeneration() && owner.hasLogicalDeviceGeneration() &&
               owner.hasSwapchainConfigurationGeneration() && owner.hasSwapchainGeneration() && owner.hasSwapchainImagesGeneration() &&
               owner.hasSwapchainPresentationTargetGeneration() && owner.hasSwapchainPresentationPipelineGeneration() &&
               owner.hasSwapchainReadbackGeneration() && owner.hasSwapchainFrameSlotGeneration() &&
               owner.swapchainFrameSlotDisposition() == VulkanSwapchainFrameSlotDisposition::Reusable && state.mDestroyBufferCalls == 1 &&
               state.mFreeMemoryCalls == 1);
    ensure("the reset upload slot can publish a fresh epoch",
           !owner.acquireUploadSourceGeneration(makeUploadSourceRequest(state, owner)) && owner.hasUploadSourceGeneration());

    const LLRenderContract::BufferHandle resource_handle  = owner.uploadSourceResourceHandle();
    const std::uint64_t                  content_identity = owner.uploadSourceContentIdentity();
    const VkBuffer                       buffer           = owner.uploadSourceBuffer();
    const VkDeviceMemory                 memory           = owner.uploadSourceMemory();
    VulkanInstanceGeneration             moved(std::move(owner));
    ensure("aggregate move transfers the complete upload owner and empties every source getter",
           owner.instance() == VK_NULL_HANDLE && !owner.hasUploadSourceGeneration() && !owner.uploadSourceResourceHandle() &&
               owner.uploadSourceContentIdentity() == 0 && owner.uploadSourceBuffer() == VK_NULL_HANDLE &&
               owner.uploadSourceMemory() == VK_NULL_HANDLE && moved.hasUploadSourceGeneration() &&
               moved.uploadSourceResourceHandle() == resource_handle && moved.uploadSourceContentIdentity() == content_identity &&
               moved.uploadSourceBuffer() == buffer && moved.uploadSourceMemory() == memory);

    state.mDeviceDestroyOwner = &moved;
    state.mEvents.clear();
    ensure("logical reset retires the whole drawable chain, upload source, and device", moved.resetLogicalDeviceGeneration());
    const auto upload_buffer_destroy = std::find(state.mEvents.begin(), state.mEvents.end(), Event::DestroyUploadSourceBuffer);
    const auto upload_memory_free    = std::find(state.mEvents.begin(), state.mEvents.end(), Event::FreeUploadSourceMemory);
    const auto device_destroy        = std::find(state.mEvents.begin(), state.mEvents.end(), Event::DestroyDevice);
    ensure("the immutable source buffer and memory retire in order before their logical-device parent",
           upload_buffer_destroy != state.mEvents.end() && upload_memory_free != state.mEvents.end() &&
               device_destroy != state.mEvents.end() && upload_buffer_destroy < upload_memory_free && upload_memory_free < device_destroy &&
               state.mDeviceDestroyObservationMade && !state.mObservedUploadSourceAtDeviceDestroy && !moved.hasUploadSourceGeneration() &&
               !moved.hasLogicalDeviceGeneration() && !moved.hasSwapchainConfigurationGeneration() && !moved.hasSwapchainGeneration() &&
               !moved.hasSwapchainImagesGeneration() && moved.hasPresentationDeviceGeneration() && moved.hasSurfaceGeneration() &&
               state.mQueueSubmitCalls == 0 && moved.reset());
}

template<>
template<>
void render_vulkan_instance_test_object::test<111>()
{
    static_assert(noexcept(std::declval<const VulkanInstanceGeneration&>().hasUploadDestinationGeneration()));
    static_assert(noexcept(std::declval<const VulkanInstanceGeneration&>().uploadDestinationIsResident()));
    static_assert(noexcept(std::declval<VulkanInstanceGeneration&>().acquireUploadDestinationGeneration(
        std::declval<const VulkanUploadDestinationRequest&>())));
    static_assert(noexcept(std::declval<VulkanInstanceGeneration&>().resetUploadDestinationGeneration()));

    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    ensure("an absent upload destination exposes only inert metadata",
           !owner.hasUploadDestinationGeneration() && !owner.uploadDestinationResourceHandle() &&
               owner.uploadDestinationExpectedContentIdentity() == 0 && owner.uploadDestinationResidentContentIdentity() == 0 &&
               !owner.uploadDestinationIsResident() && owner.uploadDestinationBuffer() == VK_NULL_HANDLE &&
               owner.uploadDestinationMemory() == VK_NULL_HANDLE && owner.uploadDestinationByteCount() == 0 &&
               owner.uploadDestinationUsage() == 0 && owner.uploadDestinationAllocationSize() == 0 &&
               owner.uploadDestinationMemoryTypeIndex() == 0 && owner.uploadDestinationMemoryPropertyFlags() == 0 &&
               !owner.uploadDestinationIsDeviceLocal() && !owner.uploadDestinationIsMapped());

    acquireLogicalChain(state, owner);
    ensureUploadDestinationCode(owner.acquireUploadDestinationGeneration(makeUploadDestinationRequest(state, owner)),
                                VulkanUploadDestinationAcquireCode::UploadSourceNotLive);
    ensure("destination preflight performs no native work without its exact source", state.mCreateBufferCalls == 0);
    ensure("the source fixture succeeds", !owner.acquireUploadSourceGeneration(makeUploadSourceRequest(state, owner)));
    state.mMemoryProperties.memoryTypes[0].propertyFlags |= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    VulkanUploadDestinationRequest request = makeUploadDestinationRequest(state, owner);
    request.mInstanceOwnerCheck.mIsCurrent = nullptr;
    ensureUploadDestinationCode(owner.acquireUploadDestinationGeneration(request),
                                VulkanUploadDestinationAcquireCode::InvalidInstanceOwnerCheck);
    request                                   = makeUploadDestinationRequest(state, owner);
    request.mWindowGenerationCheck.mIsCurrent = nullptr;
    ensureUploadDestinationCode(owner.acquireUploadDestinationGeneration(request),
                                VulkanUploadDestinationAcquireCode::InvalidWindowGenerationCheck);
    request                         = makeUploadDestinationRequest(state, owner);
    request.mNativeWindowGeneration = 41;
    ensureUploadDestinationCode(owner.acquireUploadDestinationGeneration(request),
                                VulkanUploadDestinationAcquireCode::NativeWindowGenerationMismatch);
    state.mInstanceOwnerCurrent = false;
    ensureUploadDestinationCode(owner.acquireUploadDestinationGeneration(makeUploadDestinationRequest(state, owner)),
                                VulkanUploadDestinationAcquireCode::StaleInstanceOwner);
    state.mInstanceOwnerCurrent = true;
    state.mSurfaceWindowCurrent = false;
    ensureUploadDestinationCode(owner.acquireUploadDestinationGeneration(makeUploadDestinationRequest(state, owner)),
                                VulkanUploadDestinationAcquireCode::StaleWindowGeneration);
    state.mSurfaceWindowCurrent = true;

    VulkanUploadSourceDescription mismatched_description = makeUploadSourceDescription();
    mismatched_description.mHandle                       = { 8, 4 };
    ensureUploadDestinationCode(
        owner.acquireUploadDestinationGeneration(makeUploadDestinationRequest(state, owner, mismatched_description)),
        VulkanUploadDestinationAcquireCode::UploadSourceNotLive);

    const VulkanUploadSourceDescription description = makeUploadSourceDescription();
    ensure("the exact destination publishes", !owner.acquireUploadDestinationGeneration(makeUploadDestinationRequest(state, owner)));
    ensure("the aggregate exposes the exact unpublished device-local destination",
           owner.hasUploadDestinationGeneration() && owner.uploadDestinationResourceHandle() == description.mHandle &&
               owner.uploadDestinationExpectedContentIdentity() == owner.uploadSourceContentIdentity() &&
               owner.uploadDestinationExpectedContentIdentity() != 0 && owner.uploadDestinationResidentContentIdentity() == 0 &&
               !owner.uploadDestinationIsResident() && owner.uploadDestinationBuffer() == state.mUploadDestinationBuffer &&
               owner.uploadDestinationMemory() == state.mUploadDestinationMemory &&
               owner.uploadDestinationByteCount() == VULKAN_UPLOAD_SOURCE_BYTE_COUNT &&
               owner.uploadDestinationUsage() == (VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) &&
               owner.uploadDestinationAllocationSize() == state.mUploadDestinationMemoryRequirements.size &&
               owner.uploadDestinationMemoryTypeIndex() == 0 && owner.uploadDestinationIsDeviceLocal() &&
               !owner.uploadDestinationIsMapped());
    ensure("destination creation uses one exact dedicated buffer without mapping",
           state.mCreateBufferCalls == 2 && state.mUploadDestinationBufferCreateInfo.size == VULKAN_UPLOAD_SOURCE_BYTE_COUNT &&
               state.mUploadDestinationBufferCreateInfo.usage == (VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) &&
               state.mUploadDestinationBufferCreateInfo.sharingMode == VK_SHARING_MODE_EXCLUSIVE &&
               state.mUploadDestinationMemoryAllocateInfo.allocationSize == state.mUploadDestinationMemoryRequirements.size &&
               state.mUploadDestinationBoundBuffer == state.mUploadDestinationBuffer &&
               state.mUploadDestinationBoundMemory == state.mUploadDestinationMemory && state.mUploadDestinationBindOffset == 0 &&
               state.mMapMemoryCalls == 1 && state.mUnmapMemoryCalls == 1);
    const std::size_t create_calls = state.mCreateBufferCalls;
    ensureUploadDestinationCode(owner.acquireUploadDestinationGeneration(makeUploadDestinationRequest(state, owner)),
                                VulkanUploadDestinationAcquireCode::UploadDestinationAlreadyOwned);
    ensure("duplicate destination acquisition performs no native work", state.mCreateBufferCalls == create_calls && owner.reset());
}

template<>
template<>
void render_vulkan_instance_test_object::test<112>()
{
    static_assert(noexcept(std::declval<const VulkanInstanceGeneration&>().hasUploadTransferGeneration()));
    static_assert(noexcept(
        std::declval<VulkanInstanceGeneration&>().acquireUploadTransferGeneration(std::declval<const VulkanUploadTransferRequest&>())));
    static_assert(noexcept(
        std::declval<VulkanInstanceGeneration&>().executeUploadTransfer(std::declval<const VulkanUploadTransferOperationRequest&>())));
    static_assert(noexcept(std::declval<VulkanInstanceGeneration&>().retryUploadTransferCompletion(
        std::declval<const VulkanUploadTransferOperationRequest&>())));
    static_assert(noexcept(std::declval<VulkanInstanceGeneration&>().resetUploadTransferGeneration()));

    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireUploadDestinationChain(state, owner);

    ensure("an absent transfer exposes only inert metadata",
           !owner.hasUploadTransferGeneration() && !owner.uploadTransferResourceHandle() && owner.uploadTransferContentIdentity() == 0 &&
               owner.uploadTransferSourceBuffer() == VK_NULL_HANDLE && owner.uploadTransferDestinationBuffer() == VK_NULL_HANDLE &&
               owner.uploadTransferQueue() == VK_NULL_HANDLE && owner.uploadTransferQueueFamilyIndex() == VK_QUEUE_FAMILY_IGNORED &&
               owner.uploadTransferQueueIndex() == std::numeric_limits<std::uint32_t>::max() &&
               owner.uploadTransferCommandPool() == VK_NULL_HANDLE && owner.uploadTransferCommandBuffer() == VK_NULL_HANDLE &&
               owner.uploadTransferFence() == VK_NULL_HANDLE && owner.uploadTransferSubmissionCount() == 0 &&
               owner.uploadTransferCompletionWaitCount() == 0 && !owner.uploadTransferDisposition());

    VulkanUploadTransferRequest request    = makeUploadTransferRequest(state, owner);
    request.mInstanceOwnerCheck.mIsCurrent = nullptr;
    ensureUploadTransferCode(owner.acquireUploadTransferGeneration(request), VulkanUploadTransferAcquireCode::InvalidInstanceOwnerCheck);
    state.mMissing                                         = MissingCommand::CmdCopyBuffer;
    const VulkanUploadTransferAcquireResult missing_result = owner.acquireUploadTransferGeneration(makeUploadTransferRequest(state, owner));
    const VulkanUploadTransferAcquireError& missing        = requireUploadTransferError(missing_result);
    ensure("a missing copy command is preserved as a typed resolution failure",
           missing.mCode == VulkanUploadTransferAcquireCode::ResolutionFailure && missing.mResolutionError &&
               missing.mResolutionError->mCode == VulkanUploadTransferResolutionCode::MissingRequiredCommand &&
               missing.mResolutionError->mCommand == VulkanUploadTransferCommand::CmdCopyBuffer);
    state.mMissing = MissingCommand::None;

    ensure("the exact transfer slot publishes", !owner.acquireUploadTransferGeneration(makeUploadTransferRequest(state, owner)));
    ensure("the aggregate exposes the exact ready transfer generation",
           owner.hasUploadTransferGeneration() && owner.uploadTransferResourceHandle() == owner.uploadSourceResourceHandle() &&
               owner.uploadTransferContentIdentity() == owner.uploadSourceContentIdentity() &&
               owner.uploadTransferSourceBuffer() == owner.uploadSourceBuffer() &&
               owner.uploadTransferDestinationBuffer() == owner.uploadDestinationBuffer() && owner.uploadTransferQueue() == state.mQueue &&
               owner.uploadTransferQueueFamilyIndex() == 0 && owner.uploadTransferQueueIndex() == 0 &&
               owner.uploadTransferCommandPool() == state.mUploadTransferCommandPool &&
               owner.uploadTransferCommandBuffer() == state.mUploadTransferCommandBuffer &&
               owner.uploadTransferFence() == state.mUploadTransferFence && owner.uploadTransferSubmissionCount() == 0 &&
               owner.uploadTransferCompletionWaitCount() == 0 &&
               owner.uploadTransferDisposition() == VulkanUploadTransferDisposition::Ready);
    ensure("the one-shot owner uses an exclusive queue-family pool and an unsignaled fence",
           state.mUploadTransferCommandPoolCreateInfo.flags == 0 && state.mUploadTransferCommandPoolCreateInfo.queueFamilyIndex == 0 &&
               state.mUploadTransferCommandBufferAllocateInfo.commandPool == state.mUploadTransferCommandPool &&
               state.mUploadTransferCommandBufferAllocateInfo.level == VK_COMMAND_BUFFER_LEVEL_PRIMARY &&
               state.mUploadTransferCommandBufferAllocateInfo.commandBufferCount == 1 && state.mUploadTransferFenceCreateInfo.flags == 0);

    const std::size_t owner_checks_before  = state.mInstanceOwnerChecks;
    const std::size_t window_checks_before = state.mSurfaceWindowChecks;
    state.mUploadTransferResetReentryOwner = &owner;
    ensureUploadTransferDisposition(owner.executeUploadTransfer(makeUploadTransferOperationRequest(state, owner)),
                                    VulkanUploadTransferDisposition::Complete);
    ensure("fence completion alone publishes the expected resident identity",
           owner.uploadDestinationIsResident() &&
               owner.uploadDestinationResidentContentIdentity() == owner.uploadDestinationExpectedContentIdentity() &&
               owner.uploadDestinationResidentContentIdentity() == owner.uploadSourceContentIdentity() &&
               owner.uploadTransferDisposition() == VulkanUploadTransferDisposition::Complete &&
               owner.uploadTransferSubmissionCount() == 1 && owner.uploadTransferCompletionWaitCount() == 1);
    ensure("authenticated callbacks finish before the first native transfer command",
           state.mInstanceOwnerChecksAtUploadTransferBegin == owner_checks_before + 1 &&
               state.mSurfaceWindowChecksAtUploadTransferBegin == window_checks_before + 1 &&
               state.mInstanceOwnerChecks == owner_checks_before + 1 && state.mSurfaceWindowChecks == window_checks_before + 1);
    ensure("the native-operation guard refuses every reentrant aggregate reset without teardown",
           state.mUploadTransferResetReentryInvoked && !state.mReenteredUploadTransferResetSucceeded &&
               !state.mReenteredUploadDestinationResetSucceeded && !state.mReenteredUploadSourceResetSucceeded &&
               !state.mReenteredLogicalResetSucceeded && !state.mReenteredAggregateResetSucceeded &&
               state.mUploadTransferReentryObservedNoTeardown && owner.hasUploadSourceGeneration() &&
               owner.hasUploadDestinationGeneration() && owner.hasUploadTransferGeneration() &&
               owner.uploadTransferDisposition() == VulkanUploadTransferDisposition::Complete);
    ensure("the copy records exact source and destination dependencies around 48 bytes",
           state.mBeginCommandBufferCalls == 1 && state.mEndCommandBufferCalls == 1 && state.mPipelineBarrierCalls == 2 &&
               state.mBufferBarriers.size() == 2 && state.mBufferBarrierStages.size() == 2 && state.mCopyBufferCalls == 1 &&
               state.mCopySourceBuffer == state.mUploadSourceBuffer && state.mCopyDestinationBuffer == state.mUploadDestinationBuffer &&
               state.mBufferCopyRegion.srcOffset == 0 && state.mBufferCopyRegion.dstOffset == 0 &&
               state.mBufferCopyRegion.size == VULKAN_UPLOAD_SOURCE_BYTE_COUNT && state.mQueueSubmitCalls == 1 &&
               state.mWaitForFencesCalls == 1 && state.mOperationFence == state.mUploadTransferFence && state.mWaitAll == VK_TRUE &&
               state.mWaitTimeout == std::numeric_limits<std::uint64_t>::max());
    const VulkanUploadTransferParentOperationResult retry_result =
        owner.retryUploadTransferCompletion(makeUploadTransferOperationRequest(state, owner));
    const VulkanUploadTransferParentOperationError& retry_error = requireUploadTransferOperationError(retry_result);
    ensure("a completed transfer rejects retry without submitting or waiting again",
           retry_error.mCode == VulkanUploadTransferParentOperationCode::OperationFailure && retry_error.mOperationError &&
               retry_error.mOperationError->mCode == VulkanUploadTransferOperationCode::InvalidDisposition &&
               retry_error.mOperationError->mDisposition == VulkanUploadTransferDisposition::Complete && state.mQueueSubmitCalls == 1 &&
               state.mWaitForFencesCalls == 1 && owner.uploadTransferSubmissionCount() == 1 &&
               owner.uploadTransferCompletionWaitCount() == 1);
    ensureUploadTransferCode(owner.acquireUploadTransferGeneration(makeUploadTransferRequest(state, owner)),
                             VulkanUploadTransferAcquireCode::UploadTransferAlreadyOwned);
    ensure("the completed fixture tears down", owner.reset());
}

template<>
template<>
void render_vulkan_instance_test_object::test<113>()
{
    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireUploadTransferChain(state, owner);
    acquireCompleteSwapchainChildren(state, owner);
    state.mWaitForFencesResults = { VK_TIMEOUT, VK_SUCCESS };

    const VulkanUploadTransferParentOperationResult timeout_result =
        owner.executeUploadTransfer(makeUploadTransferOperationRequest(state, owner));
    const VulkanUploadTransferParentOperationError& timeout = requireUploadTransferOperationError(timeout_result);
    ensure("the first completion timeout retains one pending submission",
           timeout.mCode == VulkanUploadTransferParentOperationCode::OperationFailure && timeout.mOperationError &&
               timeout.mOperationError->mCode == VulkanUploadTransferOperationCode::CommandFailure &&
               timeout.mOperationError->mCommand == VulkanUploadTransferCommand::WaitForFences &&
               timeout.mOperationError->mResult == VK_TIMEOUT &&
               timeout.mOperationError->mDisposition == VulkanUploadTransferDisposition::Pending &&
               owner.uploadTransferDisposition() == VulkanUploadTransferDisposition::Pending &&
               owner.uploadTransferSubmissionCount() == 1 && owner.uploadTransferCompletionWaitCount() == 1 &&
               !owner.uploadDestinationIsResident());

    const std::size_t events_before        = state.mEvents.size();
    const std::size_t destroyed_before     = state.mDestroyBufferCalls;
    const std::size_t freed_before         = state.mFreeMemoryCalls;
    const std::size_t command_pools_before = state.mDestroyCommandPoolCalls;
    const std::size_t fences_before        = state.mDestroyFenceCalls;
    ensure("pending transfer reset refuses directly", !owner.resetUploadTransferGeneration());
    ensure("pending destination reset refuses without retiring either resource", !owner.resetUploadDestinationGeneration());
    ensure("pending source reset refuses without retiring either resource", !owner.resetUploadSourceGeneration());
    ensure("pending logical reset refuses before touching its independent swapchain chain", !owner.resetLogicalDeviceGeneration());
    ensure("pending presentation-device reset refuses transitively", !owner.resetPresentationDeviceGeneration());
    ensure("pending surface reset refuses transitively", !owner.resetSurfaceGeneration());
    ensure("pending aggregate reset refuses transitively", !owner.reset());
    ensure("every refused reset leaves all native and aggregate ownership unchanged",
           state.mEvents.size() == events_before && state.mDestroyBufferCalls == destroyed_before &&
               state.mFreeMemoryCalls == freed_before && state.mDestroyCommandPoolCalls == command_pools_before &&
               state.mDestroyFenceCalls == fences_before && owner.hasUploadSourceGeneration() && owner.hasUploadDestinationGeneration() &&
               owner.hasUploadTransferGeneration() && owner.hasSwapchainConfigurationGeneration() && owner.hasSwapchainGeneration() &&
               owner.hasSwapchainImagesGeneration() && owner.hasSwapchainPresentationTargetGeneration() &&
               owner.hasSwapchainPresentationPipelineGeneration() && owner.hasSwapchainReadbackGeneration() &&
               owner.hasSwapchainFrameSlotGeneration());

    ensureUploadTransferDisposition(owner.retryUploadTransferCompletion(makeUploadTransferOperationRequest(state, owner)),
                                    VulkanUploadTransferDisposition::Complete);
    ensure("completion retry waits only and publishes residency without a second submission",
           owner.uploadDestinationIsResident() && owner.uploadTransferSubmissionCount() == 1 &&
               owner.uploadTransferCompletionWaitCount() == 2 && state.mQueueSubmitCalls == 1 && state.mWaitForFencesCalls == 2 &&
               state.mBeginCommandBufferCalls == 1 && state.mCopyBufferCalls == 1);
    ensure("successful staging retirement removes the transfer and source but preserves the resident destination and drawable chain",
           owner.resetUploadSourceGeneration() && !owner.hasUploadSourceGeneration() && !owner.hasUploadTransferGeneration() &&
               owner.hasUploadDestinationGeneration() && owner.uploadDestinationIsResident() &&
               owner.hasSwapchainConfigurationGeneration() && owner.hasSwapchainGeneration() && owner.hasSwapchainImagesGeneration() &&
               owner.hasSwapchainPresentationTargetGeneration() && owner.hasSwapchainPresentationPipelineGeneration() &&
               owner.hasSwapchainReadbackGeneration() && owner.hasSwapchainFrameSlotGeneration());
    ensure("direct destination reset leaves the complete drawable chain live",
           owner.resetUploadDestinationGeneration() && !owner.hasUploadDestinationGeneration() &&
               owner.hasSwapchainConfigurationGeneration() && owner.hasSwapchainGeneration() && owner.hasSwapchainImagesGeneration() &&
               owner.hasSwapchainPresentationTargetGeneration() && owner.hasSwapchainPresentationPipelineGeneration() &&
               owner.hasSwapchainReadbackGeneration() && owner.hasSwapchainFrameSlotGeneration() && owner.reset());
}

template<>
template<>
void render_vulkan_instance_test_object::test<114>()
{
    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireUploadTransferChain(state, owner);
    ensureUploadTransferDisposition(owner.executeUploadTransfer(makeUploadTransferOperationRequest(state, owner)),
                                    VulkanUploadTransferDisposition::Complete);
    acquireCompleteSwapchainChildren(state, owner);

    const VkBuffer        source_buffer      = owner.uploadSourceBuffer();
    const VkBuffer        destination_buffer = owner.uploadDestinationBuffer();
    const VkDeviceMemory  source_memory      = owner.uploadSourceMemory();
    const VkDeviceMemory  destination_memory = owner.uploadDestinationMemory();
    const VkCommandPool   transfer_pool      = owner.uploadTransferCommandPool();
    const VkCommandBuffer transfer_command   = owner.uploadTransferCommandBuffer();
    const VkFence         transfer_fence     = owner.uploadTransferFence();
    const std::uint64_t   resident_identity  = owner.uploadDestinationResidentContentIdentity();
    const std::size_t     submit_calls       = state.mQueueSubmitCalls;
    const std::size_t     wait_calls         = state.mWaitForFencesCalls;

    ensureSwapchainChainRebuildOutcome(owner.rebuildSwapchainChain(makeSwapchainChainRebuildRequest(state, owner, VkExtent2D{ 1280, 720 })),
                                       VulkanSwapchainChainRebuildOutcome::Ready);
    ensure("successful rebuild retains the exact completed device-scoped upload chain",
           owner.uploadSourceBuffer() == source_buffer && owner.uploadSourceMemory() == source_memory &&
               owner.uploadDestinationBuffer() == destination_buffer && owner.uploadDestinationMemory() == destination_memory &&
               owner.uploadDestinationResidentContentIdentity() == resident_identity && owner.uploadDestinationIsResident() &&
               owner.uploadTransferCommandPool() == transfer_pool && owner.uploadTransferCommandBuffer() == transfer_command &&
               owner.uploadTransferFence() == transfer_fence &&
               owner.uploadTransferDisposition() == VulkanUploadTransferDisposition::Complete &&
               owner.uploadTransferSubmissionCount() == 1 && owner.uploadTransferCompletionWaitCount() == 1 &&
               state.mQueueSubmitCalls == submit_calls && state.mWaitForFencesCalls == wait_calls);

    state.mSwapchainCreateResult = VK_ERROR_OUT_OF_HOST_MEMORY;
    const VulkanSwapchainChainRebuildResult failed_result =
        owner.rebuildSwapchainChain(makeSwapchainChainRebuildRequest(state, owner, VkExtent2D{ 1440, 810 }));
    const VulkanSwapchainChainRebuildError& failed = requireSwapchainChainRebuildError(failed_result);
    ensure("failed rebuild stops at the replacement swapchain",
           failed.mCode == VulkanSwapchainChainRebuildCode::ChildFailure && failed.mPhase == VulkanSwapchainChainRebuildPhase::Swapchain);
    ensure("failed rebuild rollback also retains completed residency without resubmission",
           owner.uploadSourceBuffer() == source_buffer && owner.uploadDestinationBuffer() == destination_buffer &&
               owner.uploadDestinationResidentContentIdentity() == resident_identity &&
               owner.uploadTransferCommandPool() == transfer_pool && owner.uploadTransferFence() == transfer_fence &&
               state.mQueueSubmitCalls == submit_calls && state.mWaitForFencesCalls == wait_calls);
    state.mSwapchainCreateResult = VK_SUCCESS;
    acquireCompleteSwapchainChildren(state, owner, VkExtent2D{ 1440, 810 });

    VulkanInstanceGeneration moved(std::move(owner));
    ensure("aggregate move preserves every upload pointee and empties all source getters",
           owner.instance() == VK_NULL_HANDLE && !owner.hasUploadSourceGeneration() && !owner.hasUploadDestinationGeneration() &&
               !owner.hasUploadTransferGeneration() && moved.uploadSourceBuffer() == source_buffer &&
               moved.uploadDestinationBuffer() == destination_buffer &&
               moved.uploadDestinationResidentContentIdentity() == resident_identity &&
               moved.uploadTransferCommandPool() == transfer_pool && moved.uploadTransferCommandBuffer() == transfer_command &&
               moved.uploadTransferFence() == transfer_fence && moved.uploadTransferSubmissionCount() == 1 &&
               moved.uploadTransferCompletionWaitCount() == 1);

    state.mDeviceDestroyOwner = &moved;
    state.mEvents.clear();
    ensure("logical teardown retires all device children and the logical device", moved.resetLogicalDeviceGeneration());
    const auto transfer_fence_destroy = std::find(state.mEvents.begin(), state.mEvents.end(), Event::DestroyUploadTransferFence);
    const auto transfer_pool_destroy  = std::find(state.mEvents.begin(), state.mEvents.end(), Event::DestroyUploadTransferCommandPool);
    const auto swapchain_destroy      = std::find(state.mEvents.begin(), state.mEvents.end(), Event::DestroySwapchain);
    const auto destination_destroy    = std::find(state.mEvents.begin(), state.mEvents.end(), Event::DestroyUploadDestinationBuffer);
    const auto destination_free       = std::find(state.mEvents.begin(), state.mEvents.end(), Event::FreeUploadDestinationMemory);
    const auto source_destroy         = std::find(state.mEvents.begin(), state.mEvents.end(), Event::DestroyUploadSourceBuffer);
    const auto source_free            = std::find(state.mEvents.begin(), state.mEvents.end(), Event::FreeUploadSourceMemory);
    const auto device_destroy         = std::find(state.mEvents.begin(), state.mEvents.end(), Event::DestroyDevice);
    ensure("full teardown orders transfer, destination, source, then device after the swapchain chain",
           swapchain_destroy != state.mEvents.end() && transfer_fence_destroy != state.mEvents.end() &&
               transfer_pool_destroy != state.mEvents.end() && destination_destroy != state.mEvents.end() &&
               destination_free != state.mEvents.end() && source_destroy != state.mEvents.end() && source_free != state.mEvents.end() &&
               device_destroy != state.mEvents.end() && swapchain_destroy < transfer_fence_destroy &&
               transfer_fence_destroy < transfer_pool_destroy && transfer_pool_destroy < destination_destroy &&
               destination_destroy < destination_free && destination_free < source_destroy && source_destroy < source_free &&
               source_free < device_destroy && state.mDeviceDestroyObservationMade && !state.mObservedUploadSourceAtDeviceDestroy &&
               !state.mObservedUploadDestinationAtDeviceDestroy && !state.mObservedUploadTransferAtDeviceDestroy &&
               !moved.hasLogicalDeviceGeneration() && moved.hasPresentationDeviceGeneration() && moved.hasSurfaceGeneration() &&
               moved.reset());
}

template<>
template<>
void render_vulkan_instance_test_object::test<115>()
{
    {
        FakeState                state;
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        acquireUploadSourceChain(state, owner);
        state.mMemoryProperties.memoryTypes[0].propertyFlags |= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        const VulkanUploadDestinationAcquireResult result =
            VulkanInstanceDetail::acquireUploadDestination(owner, makeUploadDestinationRequest(state, owner), failAllocation);
        ensureUploadDestinationCode(result, VulkanUploadDestinationAcquireCode::AllocationFailure);
        ensure("destination wrapper allocation failure rolls back only its native candidate",
               owner.hasUploadSourceGeneration() && !owner.hasUploadDestinationGeneration() &&
                   std::find(state.mDestroyedBuffers.begin(), state.mDestroyedBuffers.end(), state.mUploadDestinationBuffer) !=
                       state.mDestroyedBuffers.end() &&
                   std::find(state.mFreedMemories.begin(), state.mFreedMemories.end(), state.mUploadDestinationMemory) !=
                       state.mFreedMemories.end() &&
                   !owner.acquireUploadDestinationGeneration(makeUploadDestinationRequest(state, owner)) && owner.reset());
    }

    {
        FakeState                state;
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        acquireUploadDestinationChain(state, owner);
        const VulkanUploadTransferAcquireResult result =
            VulkanInstanceDetail::acquireUploadTransfer(owner, makeUploadTransferRequest(state, owner), failAllocation);
        ensureUploadTransferCode(result, VulkanUploadTransferAcquireCode::AllocationFailure);
        ensure("transfer wrapper allocation failure rolls back its fence and pool but leaves both buffers live",
               owner.hasUploadSourceGeneration() && owner.hasUploadDestinationGeneration() && !owner.hasUploadTransferGeneration() &&
                   std::find(state.mDestroyedFences.begin(), state.mDestroyedFences.end(), state.mUploadTransferFence) !=
                       state.mDestroyedFences.end() &&
                   std::find(state.mDestroyedCommandPools.begin(), state.mDestroyedCommandPools.end(), state.mUploadTransferCommandPool) !=
                       state.mDestroyedCommandPools.end() &&
                   !owner.acquireUploadTransferGeneration(makeUploadTransferRequest(state, owner)) && owner.reset());
    }
}

template<>
template<>
void render_vulkan_instance_test_object::test<116>()
{
    {
        FakeState                state;
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        acquireUploadSourceChain(state, owner);
        state.mMemoryProperties.memoryTypes[0].propertyFlags |= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        UploadAbaContext                     context{ &state, &owner, makeUploadSourceDescription(), UploadAbaTarget::Source };
        const VulkanUploadDestinationRequest request{ owner.nativeWindowGeneration(),
                                                      context.mDescription,
                                                      { &context, uploadAbaOwnerIsCurrent },
                                                      { &context, uploadAbaWindowIsCurrent } };
        ensureUploadDestinationCode(owner.acquireUploadDestinationGeneration(request),
                                    VulkanUploadDestinationAcquireCode::LogicalDeviceNotLive);
        ensure("a same-handle source ABA is rejected before destination native work",
               context.mReset && context.mReacquired && owner.hasUploadSourceGeneration() &&
                   owner.uploadSourceBuffer() == state.mUploadSourceBuffer && !owner.hasUploadDestinationGeneration() &&
                   state.mCreateBufferCalls == 2 && owner.reset());
    }

    {
        FakeState                state;
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        acquireUploadDestinationChain(state, owner);
        UploadAbaContext                  context{ &state, &owner, makeUploadSourceDescription(), UploadAbaTarget::Destination };
        const VulkanUploadTransferRequest request{ owner.nativeWindowGeneration(),
                                                   context.mDescription,
                                                   { &context, uploadAbaOwnerIsCurrent },
                                                   { &context, uploadAbaWindowIsCurrent } };
        ensureUploadTransferCode(owner.acquireUploadTransferGeneration(request), VulkanUploadTransferAcquireCode::LogicalDeviceNotLive);
        ensure("a same-handle destination ABA is rejected before transfer native work",
               context.mReset && context.mReacquired && owner.hasUploadDestinationGeneration() &&
                   owner.uploadDestinationBuffer() == state.mUploadDestinationBuffer && !owner.hasUploadTransferGeneration() &&
                   state.mCreateCommandPoolCalls == 0 && owner.reset());
    }

    {
        FakeState                state;
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        acquireUploadTransferChain(state, owner);
        UploadAbaContext                                context{ &state, &owner, makeUploadSourceDescription(), UploadAbaTarget::Transfer };
        const VulkanUploadTransferOperationRequest      request{ owner.nativeWindowGeneration(),
                                                            context.mDescription,
                                                                 { &context, uploadAbaOwnerIsCurrent },
                                                                 { &context, uploadAbaWindowIsCurrent } };
        const VulkanUploadTransferParentOperationResult error_result = owner.executeUploadTransfer(request);
        const VulkanUploadTransferParentOperationError& error        = requireUploadTransferOperationError(error_result);
        ensure("a same-handle transfer ABA is rejected before command recording",
               context.mReset && context.mReacquired && error.mCode == VulkanUploadTransferParentOperationCode::InstanceNotLive &&
                   owner.uploadTransferDisposition() == VulkanUploadTransferDisposition::Ready && state.mQueueSubmitCalls == 0 &&
                   state.mBeginCommandBufferCalls == 0 && owner.reset());
    }
}

template<>
template<>
void render_vulkan_instance_test_object::test<117>()
{
    constexpr std::array loss_commands{ VulkanUploadTransferCommand::BeginCommandBuffer, VulkanUploadTransferCommand::EndCommandBuffer,
                                        VulkanUploadTransferCommand::WaitForFences };
    for (const VulkanUploadTransferCommand loss_command : loss_commands)
    {
        FakeState                state;
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        acquireUploadTransferChain(state, owner);
        if (loss_command == VulkanUploadTransferCommand::BeginCommandBuffer)
        {
            state.mBeginCommandBufferResult = VK_ERROR_DEVICE_LOST;
        }
        else if (loss_command == VulkanUploadTransferCommand::EndCommandBuffer)
        {
            state.mEndCommandBufferResult = VK_ERROR_DEVICE_LOST;
        }
        else
        {
            state.mWaitForFencesResults = { VK_ERROR_DEVICE_LOST };
        }

        const VulkanUploadTransferParentOperationResult error_result =
            owner.executeUploadTransfer(makeUploadTransferOperationRequest(state, owner));
        const VulkanUploadTransferParentOperationError& error     = requireUploadTransferOperationError(error_result);
        const bool                                      submitted = loss_command == VulkanUploadTransferCommand::WaitForFences;
        ensure("every native device-loss boundary becomes an exact resettable terminal transfer",
               error.mCode == VulkanUploadTransferParentOperationCode::OperationFailure && error.mOperationError &&
                   error.mOperationError->mCode == VulkanUploadTransferOperationCode::CommandFailure &&
                   error.mOperationError->mCommand == loss_command && error.mOperationError->mResult == VK_ERROR_DEVICE_LOST &&
                   error.mOperationError->mDisposition == VulkanUploadTransferDisposition::DeviceLost &&
                   owner.uploadTransferDisposition() == VulkanUploadTransferDisposition::DeviceLost &&
                   owner.uploadTransferSubmissionCount() == (submitted ? 1 : 0) &&
                   owner.uploadTransferCompletionWaitCount() == (submitted ? 1 : 0) && state.mQueueSubmitCalls == (submitted ? 1 : 0) &&
                   state.mWaitForFencesCalls == (submitted ? 1 : 0) && !owner.uploadDestinationIsResident() && owner.reset());
    }

    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireUploadTransferChain(state, owner);
    state.mQueueSubmitResult    = VK_ERROR_DEVICE_LOST;
    state.mWaitForFencesResults = { VK_TIMEOUT, VK_SUCCESS };

    const VulkanUploadTransferParentOperationResult timeout_result =
        owner.executeUploadTransfer(makeUploadTransferOperationRequest(state, owner));
    const VulkanUploadTransferParentOperationError& timeout = requireUploadTransferOperationError(timeout_result);
    ensure("submit loss plus an unresolved wait retains the exact pending obligation",
           timeout.mCode == VulkanUploadTransferParentOperationCode::OperationFailure && timeout.mOperationError &&
               timeout.mOperationError->mCode == VulkanUploadTransferOperationCode::CommandFailure &&
               timeout.mOperationError->mCommand == VulkanUploadTransferCommand::WaitForFences &&
               timeout.mOperationError->mResult == VK_TIMEOUT &&
               timeout.mOperationError->mDisposition == VulkanUploadTransferDisposition::Pending &&
               owner.uploadTransferDisposition() == VulkanUploadTransferDisposition::Pending &&
               owner.uploadTransferSubmissionCount() == 1 && owner.uploadTransferCompletionWaitCount() == 1 &&
               !owner.uploadDestinationIsResident());
    const VulkanUploadTransferParentOperationResult retry_result =
        owner.retryUploadTransferCompletion(makeUploadTransferOperationRequest(state, owner));
    const VulkanUploadTransferParentOperationError& retry = requireUploadTransferOperationError(retry_result);
    ensure("retry waits without resubmission, reports the retained submit loss, and unlocks retirement",
           retry.mCode == VulkanUploadTransferParentOperationCode::OperationFailure && retry.mOperationError &&
               retry.mOperationError->mCode == VulkanUploadTransferOperationCode::CommandFailure &&
               retry.mOperationError->mCommand == VulkanUploadTransferCommand::QueueSubmit &&
               retry.mOperationError->mResult == VK_ERROR_DEVICE_LOST &&
               retry.mOperationError->mDisposition == VulkanUploadTransferDisposition::DeviceLost &&
               owner.uploadTransferDisposition() == VulkanUploadTransferDisposition::DeviceLost &&
               owner.uploadTransferSubmissionCount() == 1 && owner.uploadTransferCompletionWaitCount() == 2 &&
               state.mQueueSubmitCalls == 1 && state.mWaitForFencesCalls == 2 && !owner.uploadDestinationIsResident() && owner.reset());
}

template<>
template<>
void render_vulkan_instance_test_object::test<118>()
{
    {
        FakeState                state;
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        acquireUploadTransferChain(state, owner);
        const VkBuffer        source_buffer      = owner.uploadSourceBuffer();
        const VkBuffer        destination_buffer = owner.uploadDestinationBuffer();
        const VkCommandPool   command_pool       = owner.uploadTransferCommandPool();
        const VkCommandBuffer command_buffer     = owner.uploadTransferCommandBuffer();
        const VkFence         fence              = owner.uploadTransferFence();

        VulkanInstanceGeneration moved(std::move(owner));
        ensure("a Ready aggregate move preserves every retained child identity and empties the source",
               owner.instance() == VK_NULL_HANDLE && !owner.hasUploadSourceGeneration() && !owner.hasUploadDestinationGeneration() &&
                   !owner.hasUploadTransferGeneration() && moved.uploadSourceBuffer() == source_buffer &&
                   moved.uploadDestinationBuffer() == destination_buffer && moved.uploadTransferCommandPool() == command_pool &&
                   moved.uploadTransferCommandBuffer() == command_buffer && moved.uploadTransferFence() == fence &&
                   moved.uploadTransferDisposition() == VulkanUploadTransferDisposition::Ready);
        ensureUploadTransferDisposition(moved.executeUploadTransfer(makeUploadTransferOperationRequest(state, moved)),
                                        VulkanUploadTransferDisposition::Complete);
        ensure("the moved Ready transfer authenticates through the new aggregate and completes once",
               moved.uploadDestinationIsResident() && moved.uploadTransferSubmissionCount() == 1 &&
                   moved.uploadTransferCompletionWaitCount() == 1 && state.mQueueSubmitCalls == 1 && state.mWaitForFencesCalls == 1 &&
                   moved.reset());
    }

    {
        FakeState                state;
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        acquireUploadTransferChain(state, owner);
        state.mWaitForFencesResults = { VK_TIMEOUT, VK_SUCCESS };
        const VulkanUploadTransferParentOperationResult pending_result =
            owner.executeUploadTransfer(makeUploadTransferOperationRequest(state, owner));
        requireUploadTransferOperationError(pending_result);
        const VkBuffer      source_buffer      = owner.uploadSourceBuffer();
        const VkBuffer      destination_buffer = owner.uploadDestinationBuffer();
        const VkCommandPool command_pool       = owner.uploadTransferCommandPool();
        const VkFence       fence              = owner.uploadTransferFence();

        VulkanInstanceGeneration moved(std::move(owner));
        ensure("a Pending aggregate move preserves retained source, destination, slot, and counters",
               owner.instance() == VK_NULL_HANDLE && !owner.hasUploadSourceGeneration() && !owner.hasUploadDestinationGeneration() &&
                   !owner.hasUploadTransferGeneration() && moved.uploadSourceBuffer() == source_buffer &&
                   moved.uploadDestinationBuffer() == destination_buffer && moved.uploadTransferCommandPool() == command_pool &&
                   moved.uploadTransferFence() == fence && moved.uploadTransferDisposition() == VulkanUploadTransferDisposition::Pending &&
                   moved.uploadTransferSubmissionCount() == 1 && moved.uploadTransferCompletionWaitCount() == 1);
        ensureUploadTransferDisposition(moved.retryUploadTransferCompletion(makeUploadTransferOperationRequest(state, moved)),
                                        VulkanUploadTransferDisposition::Complete);
        ensure("the moved Pending transfer authenticates and retries only its retained completion wait",
               moved.uploadDestinationIsResident() && moved.uploadTransferSubmissionCount() == 1 &&
                   moved.uploadTransferCompletionWaitCount() == 2 && state.mQueueSubmitCalls == 1 && state.mWaitForFencesCalls == 2 &&
                   moved.reset());
    }
}

template<>
template<>
void render_vulkan_instance_test_object::test<119>()
{
    FakeState                state;
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
    acquireUploadTransferChain(state, owner);
    acquireCompleteSwapchainChildren(state, owner);
    const LLRenderContract::BufferHandle source_handle   = owner.uploadSourceResourceHandle();
    const std::uint64_t                  source_identity = owner.uploadSourceContentIdentity();
    const VkBuffer                       source_buffer   = owner.uploadSourceBuffer();
    const VkDeviceMemory                 source_memory   = owner.uploadSourceMemory();

    ensure("destination reset safely cascades through a Ready transfer",
           owner.resetUploadDestinationGeneration() && !owner.hasUploadDestinationGeneration() && !owner.hasUploadTransferGeneration());
    ensure("destination reset preserves the exact live source and complete swapchain chain",
           owner.hasUploadSourceGeneration() && owner.uploadSourceResourceHandle() == source_handle &&
               owner.uploadSourceContentIdentity() == source_identity && owner.uploadSourceBuffer() == source_buffer &&
               owner.uploadSourceMemory() == source_memory && owner.hasSwapchainConfigurationGeneration() &&
               owner.hasSwapchainGeneration() && owner.hasSwapchainImagesGeneration() && owner.hasSwapchainPresentationTargetGeneration() &&
               owner.hasSwapchainPresentationPipelineGeneration() && owner.hasSwapchainReadbackGeneration() &&
               owner.hasSwapchainFrameSlotGeneration() &&
               std::find(state.mDestroyedBuffers.begin(), state.mDestroyedBuffers.end(), source_buffer) == state.mDestroyedBuffers.end() &&
               std::find(state.mFreedMemories.begin(), state.mFreedMemories.end(), source_memory) == state.mFreedMemories.end() &&
               std::find(state.mDestroyedFences.begin(), state.mDestroyedFences.end(), state.mUploadTransferFence) !=
                   state.mDestroyedFences.end() &&
               std::find(state.mDestroyedCommandPools.begin(), state.mDestroyedCommandPools.end(), state.mUploadTransferCommandPool) !=
                   state.mDestroyedCommandPools.end() &&
               std::find(state.mDestroyedBuffers.begin(), state.mDestroyedBuffers.end(), state.mUploadDestinationBuffer) !=
                   state.mDestroyedBuffers.end() &&
               owner.reset());
}

} // namespace tut
