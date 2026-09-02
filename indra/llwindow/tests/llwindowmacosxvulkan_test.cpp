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
        std::memcpy(mPhysicalProperties.deviceName, "macOS adapter fake", sizeof("macOS adapter fake"));
        mSurfaceCapabilities.minImageCount       = 2;
        mSurfaceCapabilities.maxImageCount       = 3;
        mSurfaceCapabilities.currentExtent       = { std::numeric_limits<std::uint32_t>::max(),
                                                      std::numeric_limits<std::uint32_t>::max() };
        mSurfaceCapabilities.minImageExtent      = { 64, 64 };
        mSurfaceCapabilities.maxImageExtent      = { 4096, 2160 };
        mSurfaceCapabilities.maxImageArrayLayers = 1;
        mSurfaceCapabilities.supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        mSurfaceCapabilities.currentTransform    = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        mSurfaceCapabilities.supportedCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        mSurfaceCapabilities.supportedUsageFlags =
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        mFormatProperties.optimalTilingFeatures =
            VK_FORMAT_FEATURE_TRANSFER_DST_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
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
    std::size_t                mCreateFramebufferCount     = 0;
    std::size_t                mDestroyFramebufferCount    = 0;
    std::size_t                mCreateCommandPoolCount     = 0;
    std::size_t                mDestroyCommandPoolCount    = 0;
    std::size_t                mCreateSemaphoreCount       = 0;
    std::size_t                mDestroySemaphoreCount      = 0;
    std::size_t                mCreateFenceCount           = 0;
    std::size_t                mDestroyFenceCount          = 0;
    std::size_t                mWaitForFencesCount         = 0;
    std::size_t                mQueueSubmitCount           = 0;
    VkPipelineStageFlags       mSubmitWaitStage            = 0;
    std::size_t                mAcquireNextImageCount      = 0;
    std::uint32_t              mAcquiredImageIndex         = 0;
    std::size_t                mPipelineBarrierCount       = 0;
    std::size_t                mClearColorImageCount       = 0;
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

VKAPI_ATTR void VKAPI_CALL fakeDestroyDevice(VkDevice, const VkAllocationCallbacks*) noexcept
{
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
                                                   const VkImageViewCreateInfo*,
                                                   const VkAllocationCallbacks*,
                                                   VkImageView* image_view) noexcept
{
    if (!gState || device != gState->mDevice || !image_view)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    *image_view = gState->mImageViews[gState->mNextImageView++ % gState->mImageViews.size()];
    ++gState->mCreateImageViewCount;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyImageView(VkDevice device,
                                                VkImageView image_view,
                                                const VkAllocationCallbacks*) noexcept
{
    if (gState && device == gState->mDevice && image_view != VK_NULL_HANDLE)
    {
        ++gState->mDestroyImageViewCount;
    }
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
        submits[0].waitSemaphoreCount != 1 || !submits[0].pWaitSemaphores ||
        submits[0].pWaitSemaphores[0] != gState->mImageAvailableSemaphore || !submits[0].pWaitDstStageMask ||
        submits[0].commandBufferCount != 1 || !submits[0].pCommandBuffers ||
        submits[0].pCommandBuffers[0] != gState->mCommandBuffer || submits[0].signalSemaphoreCount != 1 ||
        !submits[0].pSignalSemaphores || submits[0].pSignalSemaphores[0] != gState->mPresentationReadySemaphore)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    ++gState->mQueueSubmitCount;
    gState->mSubmitWaitStage = submits[0].pWaitDstStageMask[0];
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
                                                  VkPipelineStageFlags,
                                                  VkPipelineStageFlags,
                                                  VkDependencyFlags,
                                                  std::uint32_t,
                                                  const VkMemoryBarrier*,
                                                  std::uint32_t,
                                                  const VkBufferMemoryBarrier*,
                                                  std::uint32_t               image_barrier_count,
                                                  const VkImageMemoryBarrier* image_barriers) noexcept
{
    if (gState && command_buffer == gState->mCommandBuffer && image_barrier_count == 1 && image_barriers)
    {
        ++gState->mPipelineBarrierCount;
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
    LL_MACOS_VULKAN_DEVICE_COMMAND(CreateRenderPass);
    LL_MACOS_VULKAN_DEVICE_COMMAND(DestroyRenderPass);
    LL_MACOS_VULKAN_DEVICE_COMMAND(CreateFramebuffer);
    LL_MACOS_VULKAN_DEVICE_COMMAND(DestroyFramebuffer);
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
    LL_MACOS_VULKAN_DEVICE_COMMAND(CmdClearColorImage);
    if (std::strcmp(name, "vkQueuePresentKHR") == 0)
        return eraseFunctionType(fakeQueuePresent);
    if (std::strcmp(name, "vkReleaseSwapchainImagesKHR") == 0)
        return eraseFunctionType(fakeReleaseSwapchainImages);
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

const LLRenderVulkan::VulkanSwapchainChainRebuildError* rebuildError(
    const LLRenderVulkan::VulkanSwapchainChainRebuildResult& result) noexcept
{
    return std::get_if<LLRenderVulkan::VulkanSwapchainChainRebuildError>(&result);
}

bool acquireCompleteSwapchainChain(LLWindowMacOSXVulkan& owner) noexcept
{
    using namespace LLRenderVulkan;
    return !owner.acquireInstanceGeneration(VulkanInstanceValidationMode::Disabled, VulkanInstancePortabilityMode::Disabled) &&
           !owner.acquireSurfaceGeneration() && !owner.acquirePresentationDeviceGeneration() &&
           !owner.acquireLogicalDeviceGeneration() && !owner.acquireSwapchainConfigurationGeneration() &&
           !owner.acquireSwapchainGeneration() && !owner.acquireSwapchainImagesGeneration() &&
           !owner.acquireSwapchainPresentationTargetGeneration() && !owner.acquireSwapchainFrameSlotGeneration();
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
    ensure("suspension removes the configuration, swapchain, images, presentation targets, and frame slot",
           !full_generation->hasSwapchainConfigurationGeneration() && !full_generation->hasSwapchainGeneration() &&
               !full_generation->hasSwapchainImagesGeneration() &&
               !full_generation->hasSwapchainPresentationTargetGeneration() &&
               !full_generation->hasSwapchainFrameSlotGeneration() && full_generation->swapchain() == VK_NULL_HANDLE &&
               full_generation->resolvedSwapchainImageCount() == 0 &&
               full_generation->swapchainPresentationRenderPass() == VK_NULL_HANDLE &&
               full_generation->swapchainPresentationFramebufferCount() == 0 &&
               full_generation->swapchainPresentationFramebuffer(0) == VK_NULL_HANDLE &&
               full_generation->swapchainFrameCommandPool() == VK_NULL_HANDLE);
    ensure("suspension retains the exact surface, selection, logical device, and queue parents",
           full_generation->surface() == retained_surface && full_generation->physicalDevice() == retained_physical_device &&
               full_generation->logicalDevice() == retained_device && full_generation->presentationQueue() == retained_queue);
    ensure("suspension destroys each youngest owned resource exactly once without creating a replacement",
               full_state.mCreateSwapchainCount == 1 && full_state.mDestroySwapchainCount == 1 &&
               full_state.mCreateImageViewCount == full_state.mImages.size() &&
               full_state.mDestroyImageViewCount == full_state.mImages.size() && full_state.mCreateRenderPassCount == 1 &&
               full_state.mDestroyRenderPassCount == 1 && full_state.mCreateFramebufferCount == full_state.mImages.size() &&
               full_state.mDestroyFramebufferCount == full_state.mImages.size() && full_state.mCreateCommandPoolCount == 1 &&
               full_state.mDestroyCommandPoolCount == 1 && full_state.mCreateSemaphoreCount == 2 &&
               full_state.mDestroySemaphoreCount == 2 && full_state.mCreateFenceCount == 2 && full_state.mDestroyFenceCount == 2);

    const auto  repeated_suspend_result  = full_owner->rebuildSwapchainChain();
    const auto* repeated_suspend_outcome = std::get_if<VulkanSwapchainChainRebuildOutcome>(&repeated_suspend_result);
    ensure("a repeated zero sample remains stably suspended without extra destruction",
           repeated_suspend_outcome && *repeated_suspend_outcome == VulkanSwapchainChainRebuildOutcome::Suspended &&
               full_state.mCreateSwapchainCount == 1 && full_state.mDestroySwapchainCount == 1 &&
               full_state.mDestroyImageViewCount == full_state.mImages.size() && full_state.mDestroyRenderPassCount == 1 &&
               full_state.mDestroyFramebufferCount == full_state.mImages.size() && full_state.mDestroyCommandPoolCount == 1);

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
               full_state.mCreateCommandPoolCount == 2 && full_state.mCreateSemaphoreCount == 4 &&
               full_state.mCreateFenceCount == 4);

    full_state.mOwnerDuringDestroy = full_owner;
    ensure("suspend-and-restore fixture teardown succeeds", full_owner->reset());
    ensure("restored teardown releases the second youngest child set exactly once",
           full_state.mDestroySwapchainCount == 2 && full_state.mDestroyImageViewCount == full_state.mImages.size() * 2 &&
               full_state.mDestroyRenderPassCount == 2 &&
               full_state.mDestroyFramebufferCount == full_state.mImages.size() * 2 &&
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
           state.mLastSwapchainUsage == (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT));
    const std::size_t refreshes_after_chain = state.mRefreshCount;

    state.mMainThread = false;
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
               state.mRefreshCount == refreshes_after_chain + 3 && owner->drawableWidth() == 1280 &&
               owner->drawableHeight() == 720 && state.mClearColorImageCount == 0);

    state.mRefreshWidth  = 1600;
    state.mRefreshHeight = 900;
    const auto  mismatched_extent_result = owner->acquireClearToPresentSwapchainFrameSlot(clear_color);
    const auto* mismatched_extent        = presentationError(mismatched_extent_result);
    ensure("clear-present forwards changed positive pixels for exact parent-level extent authentication",
           mismatched_extent && mismatched_extent->mCode == VulkanSwapchainFrameSlotParentOperationCode::DrawableExtentMismatch &&
               state.mRefreshCount == refreshes_after_chain + 4 && owner->drawableWidth() == 1600 &&
               owner->drawableHeight() == 900 && state.mClearColorImageCount == 0);

    state.mRefreshWidth  = 1280;
    state.mRefreshHeight = 720;
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

} // namespace tut
