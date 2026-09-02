/**
 * @file llwindowsdlvulkan_test.cpp
 * @brief Tests for SDL Vulkan window and loader lifetime ownership.
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

#include "llwindowsdlvulkan.h"
#include "lltut.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <new>
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
    Load,
    Create,
    Flags,
    DrawableSize,
    Resolver,
    Extensions,
    CreateInstance,
    CreateSurface,
    DestroySurface,
    DestroyInstance,
    Destroy,
    Unload
};

enum class Failure
{
    None,
    Load,
    Window,
    Resolver,
    Extensions
};

struct FakeState
{
    FakeState()
    {
        mPhysicalProperties.apiVersion = VK_API_VERSION_1_1;
        mPhysicalProperties.deviceType = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
        mPhysicalProperties.limits.maxFramebufferWidth  = 4096;
        mPhysicalProperties.limits.maxFramebufferHeight = 2160;
        std::memcpy(mPhysicalProperties.deviceName, "SDL adapter fake", sizeof("SDL adapter fake"));
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
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        mFormatProperties.optimalTilingFeatures =
            VK_FORMAT_FEATURE_TRANSFER_DST_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
    }

    std::array<Event, 32>              mEvents{};
    std::size_t                        mEventCount                 = 0;
    Failure                            mFailure                    = Failure::None;
    int                                mExplicitLoaderReferences   = 0;
    int                                mWindowLoaderReferences     = 0;
    SDL_WindowFlags                    mWindowFlags                = SDL_WINDOW_VULKAN;
    SDL_Window*                        mWindow                     = reinterpret_cast<SDL_Window*>(static_cast<std::uintptr_t>(0x12340));
    bool                               mDrawableSizeSucceeds       = true;
    int                                mDrawableWidth              = 1280;
    int                                mDrawableHeight             = 720;
    std::size_t                        mDrawableSizeCalls          = 0;
    const LLWindowSDLVulkanCreateInfo* mCreateInfo                 = nullptr;
    const char*                        mExtensionNames[2]          = { "VK_KHR_surface", "VK_KHR_xlib_surface" };
    std::size_t                        mExtensionCount             = 2;
    const LLWindowSDLVulkan*           mOwnerDuringDestroy         = nullptr;
    const LLWindowSDLVulkan*           mOwnerDuringInstanceDestroy = nullptr;
    bool                               mRequirementsInvalidatedBeforeDestroy  = false;
    bool                               mRequirementsLiveDuringInstanceDestroy = false;
    bool                               mLoaderLiveDuringInstanceDestroy       = false;
    bool                               mSurfaceAbsentDuringInstanceDestroy    = false;
    bool                               mFailInstanceCreation                  = false;
    bool                               mSurfaceCapabilities2Enabled           = false;
    bool                               mSurfaceMaintenanceEnabled             = false;
    bool                               mFailSurfaceCreation                   = false;
    bool                               mNullSurfaceOnSuccess                  = false;
    bool                               mPoisonSurfaceOnFailure                = false;
    bool                               mExposeDestroySurface                  = true;
    std::size_t                        mDestroyInstanceCount                  = 0;
    std::size_t                        mCreateSurfaceCount                    = 0;
    std::size_t                        mDestroySurfaceCount                   = 0;
    std::size_t                        mLiveSurfaceCount                      = 0;
    SDL_Window*                        mSurfaceWindow                         = nullptr;
    VkInstance                         mSurfaceInstance                       = VK_NULL_HANDLE;
    const VkAllocationCallbacks*       mSurfaceAllocator                      = reinterpret_cast<const VkAllocationCallbacks*>(1);
    const LLWindowSDLVulkan*           mOwnerDuringSurfaceDestroy             = nullptr;
    bool                               mRequirementsLiveDuringSurfaceDestroy  = false;
    bool                               mInstanceLiveDuringSurfaceDestroy      = false;
    bool                               mLoaderLiveDuringSurfaceDestroy        = false;

    VkPhysicalDevice           mPhysicalDevice = reinterpret_cast<VkPhysicalDevice>(static_cast<std::uintptr_t>(0x11110));
    VkPhysicalDeviceProperties mPhysicalProperties{};
    VkFormatProperties         mFormatProperties{};
    VkDevice                   mDevice    = reinterpret_cast<VkDevice>(static_cast<std::uintptr_t>(0x22220));
    VkQueue                    mQueue     = reinterpret_cast<VkQueue>(static_cast<std::uintptr_t>(0x33330));
    VkSwapchainKHR             mSwapchain = reinterpret_cast<VkSwapchainKHR>(static_cast<std::uintptr_t>(0x44440));
    std::size_t                mCreateSwapchainCalls  = 0;
    std::size_t                mDestroySwapchainCalls = 0;
    VkSurfaceCapabilitiesKHR   mSurfaceCapabilities{};
    std::array<VkImage, 3>     mImages{ reinterpret_cast<VkImage>(static_cast<std::uintptr_t>(0x51000)),
                                    reinterpret_cast<VkImage>(static_cast<std::uintptr_t>(0x52000)),
                                    reinterpret_cast<VkImage>(static_cast<std::uintptr_t>(0x53000)) };
    std::array<VkImageView, 3> mImageViews{ reinterpret_cast<VkImageView>(static_cast<std::uintptr_t>(0x61000)),
                                            reinterpret_cast<VkImageView>(static_cast<std::uintptr_t>(0x62000)),
                                            reinterpret_cast<VkImageView>(static_cast<std::uintptr_t>(0x63000)) };
    std::size_t                mNextImageView           = 0;
    std::size_t                mCreateRenderPassCalls   = 0;
    std::size_t                mDestroyRenderPassCalls  = 0;
    std::size_t                mCreateFramebufferCalls  = 0;
    std::size_t                mDestroyFramebufferCalls = 0;
    VkCommandPool              mCommandPool             = reinterpret_cast<VkCommandPool>(static_cast<std::uintptr_t>(0x71000));
    VkCommandBuffer            mCommandBuffer           = reinterpret_cast<VkCommandBuffer>(static_cast<std::uintptr_t>(0x72000));
    VkSemaphore                mImageAvailableSemaphore = reinterpret_cast<VkSemaphore>(static_cast<std::uintptr_t>(0x73000));
    VkFence                    mSubmissionFence         = reinterpret_cast<VkFence>(static_cast<std::uintptr_t>(0x74000));
    VkSemaphore                mPresentationReadySemaphore = reinterpret_cast<VkSemaphore>(static_cast<std::uintptr_t>(0x75000));
    VkFence                    mPresentCompletionFence     = reinterpret_cast<VkFence>(static_cast<std::uintptr_t>(0x76000));
    bool                       mExposeWaitForFences     = true;
    std::array<VkResult, 8>    mWaitResults{};
    std::size_t                mWaitResultCount  = 0;
    std::size_t                mWaitResultIndex  = 0;
    std::size_t                mWaitCalls        = 0;
    std::size_t                mQueueSubmitCalls = 0;
    std::size_t                mCreateSemaphoreCalls         = 0;
    std::size_t                mCreateFenceCalls             = 0;
    std::size_t                mAcquireNextImageCalls        = 0;
    std::size_t                mPipelineBarrierCalls         = 0;
    std::size_t                mClearColorImageCalls          = 0;
    VkCommandBuffer            mClearCommandBuffer            = VK_NULL_HANDLE;
    VkImage                    mClearedImage                  = VK_NULL_HANDLE;
    VkImageLayout              mClearImageLayout             = VK_IMAGE_LAYOUT_UNDEFINED;
    VkClearColorValue          mClearColor{};
    VkImageSubresourceRange    mClearRange{};
    std::size_t                mQueuePresentCalls            = 0;
    std::size_t                mReleaseSwapchainImagesCalls  = 0;
    VkResult                   mEndCommandBufferResult       = VK_SUCCESS;
    VkResult                   mAcquireNextImageResult       = VK_SUCCESS;
    std::uint32_t              mAcquiredImageIndex           = 0;
    VkResult                   mQueuePresentResult           = VK_SUCCESS;
    VkResult                   mReleaseSwapchainImagesResult = VK_SUCCESS;

    void record(Event event) noexcept { mEvents[mEventCount++] = event; }
};

FakeState* gVulkanState = nullptr;

class ScopedVulkanState
{
public:
    explicit ScopedVulkanState(FakeState& state) noexcept { gVulkanState = &state; }
    ~ScopedVulkanState() noexcept { gVulkanState = nullptr; }

    ScopedVulkanState(const ScopedVulkanState&)            = delete;
    ScopedVulkanState& operator=(const ScopedVulkanState&) = delete;

    void use(FakeState& state) noexcept { gVulkanState = &state; }
};

template<typename Function>
PFN_vkVoidFunction eraseFunctionType(Function function) noexcept
{
    return reinterpret_cast<PFN_vkVoidFunction>(function);
}

VkInstance fakeInstance() noexcept
{
    return reinterpret_cast<VkInstance>(static_cast<std::uintptr_t>(0x98760));
}

VkSurfaceKHR fakeSurface() noexcept
{
    return reinterpret_cast<VkSurfaceKHR>(static_cast<std::uintptr_t>(0x76540));
}

VKAPI_ATTR VkResult VKAPI_CALL fakeEnumerateInstanceVersion(std::uint32_t* version) noexcept
{
    if (!version)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    *version = VK_API_VERSION_1_1;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeEnumerateInstanceExtensionProperties(const char*, std::uint32_t* count,
                                                                        VkExtensionProperties* properties) noexcept
{
    if (!count)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    constexpr std::array<const char*, 4> extensions{ "VK_KHR_surface", "VK_KHR_xlib_surface", "VK_KHR_get_surface_capabilities2",
                                                     "VK_KHR_surface_maintenance1" };
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

VKAPI_ATTR VkResult VKAPI_CALL fakeEnumerateInstanceLayerProperties(std::uint32_t* count, VkLayerProperties*) noexcept
{
    if (!count)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    *count = 0;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateInstance(const VkInstanceCreateInfo* create_info,
                                                  const VkAllocationCallbacks*,
                                                  VkInstance* instance) noexcept
{
    if (!gVulkanState || !create_info || !instance)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    gVulkanState->record(Event::CreateInstance);
    gVulkanState->mSurfaceCapabilities2Enabled = false;
    gVulkanState->mSurfaceMaintenanceEnabled   = false;
    for (std::uint32_t index = 0; index < create_info->enabledExtensionCount; ++index)
    {
        if (create_info->ppEnabledExtensionNames[index] == SURFACE_CAPABILITIES_2_EXTENSION)
        {
            gVulkanState->mSurfaceCapabilities2Enabled = true;
        }
        if (create_info->ppEnabledExtensionNames[index] == SURFACE_MAINTENANCE_EXTENSION)
        {
            gVulkanState->mSurfaceMaintenanceEnabled = true;
        }
    }
    if (gVulkanState->mFailInstanceCreation)
    {
        *instance = VK_NULL_HANDLE;
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    *instance = fakeInstance();
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyInstance(VkInstance instance, const VkAllocationCallbacks*) noexcept
{
    if (!gVulkanState || instance != fakeInstance())
    {
        return;
    }

    gVulkanState->record(Event::DestroyInstance);
    ++gVulkanState->mDestroyInstanceCount;
    if (gVulkanState->mOwnerDuringInstanceDestroy)
    {
        gVulkanState->mRequirementsLiveDuringInstanceDestroy = gVulkanState->mOwnerDuringInstanceDestroy->hasRequirements();
        gVulkanState->mSurfaceAbsentDuringInstanceDestroy    = gVulkanState->mLiveSurfaceCount == 0;
    }
    gVulkanState->mLoaderLiveDuringInstanceDestroy =
        gVulkanState->mExplicitLoaderReferences == 1 && gVulkanState->mWindowLoaderReferences == 1;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroySurface(VkInstance instance, VkSurfaceKHR surface, const VkAllocationCallbacks* allocator) noexcept
{
    if (!gVulkanState || instance != fakeInstance() || surface != fakeSurface() || allocator)
    {
        return;
    }

    gVulkanState->record(Event::DestroySurface);
    ++gVulkanState->mDestroySurfaceCount;
    if (gVulkanState->mLiveSurfaceCount != 0)
    {
        --gVulkanState->mLiveSurfaceCount;
    }
    if (gVulkanState->mOwnerDuringSurfaceDestroy)
    {
        gVulkanState->mRequirementsLiveDuringSurfaceDestroy = gVulkanState->mOwnerDuringSurfaceDestroy->hasRequirements();
        gVulkanState->mInstanceLiveDuringSurfaceDestroy     = instance == fakeInstance();
    }
    gVulkanState->mLoaderLiveDuringSurfaceDestroy =
        gVulkanState->mExplicitLoaderReferences == 1 && gVulkanState->mWindowLoaderReferences == 1;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeEnumeratePhysicalDevices(VkInstance instance, std::uint32_t* count, VkPhysicalDevice* devices) noexcept
{
    if (!gVulkanState || instance != fakeInstance() || !count)
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
    devices[0] = gVulkanState->mPhysicalDevice;
    *count     = 1;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeGetPhysicalDeviceProperties(VkPhysicalDevice device, VkPhysicalDeviceProperties* properties) noexcept
{
    if (gVulkanState && device == gVulkanState->mPhysicalDevice && properties)
    {
        *properties = gVulkanState->mPhysicalProperties;
    }
}

VKAPI_ATTR void VKAPI_CALL fakeGetPhysicalDeviceFormatProperties(VkPhysicalDevice device,
                                                                  VkFormat         format,
                                                                  VkFormatProperties* properties) noexcept
{
    if (gVulkanState && device == gVulkanState->mPhysicalDevice &&
        (format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_R8G8B8A8_UNORM) && properties)
    {
        *properties = gVulkanState->mFormatProperties;
    }
}

VKAPI_ATTR void VKAPI_CALL fakeGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice         device,
                                                                      std::uint32_t*           count,
                                                                      VkQueueFamilyProperties* properties) noexcept
{
    if (!gVulkanState || device != gVulkanState->mPhysicalDevice || !count)
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
    if (!gVulkanState || device != gVulkanState->mPhysicalDevice || queue_family != 0 || surface != fakeSurface() || !supported)
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
    if (!gVulkanState || device != gVulkanState->mPhysicalDevice || layer || !count)
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

VKAPI_ATTR void VKAPI_CALL fakeGetPhysicalDeviceFeatures2(VkPhysicalDevice physical_device, VkPhysicalDeviceFeatures2* features) noexcept
{
    if (!gVulkanState || physical_device != gVulkanState->mPhysicalDevice || !features)
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
    if (gVulkanState && device == gVulkanState->mPhysicalDevice && features)
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
    if (!gVulkanState || device != gVulkanState->mPhysicalDevice || !logical_device)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    *logical_device = gVulkanState->mDevice;
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
    if (gVulkanState && device == gVulkanState->mDevice && queue_family == 0 && queue_index == 0 && queue)
    {
        *queue = gVulkanState->mQueue;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeGetSurfaceCapabilities(VkPhysicalDevice          device,
                                                          VkSurfaceKHR              surface,
                                                          VkSurfaceCapabilitiesKHR* capabilities) noexcept
{
    if (!gVulkanState || device != gVulkanState->mPhysicalDevice || surface != fakeSurface() || !capabilities)
    {
        return VK_ERROR_SURFACE_LOST_KHR;
    }
    *capabilities = gVulkanState->mSurfaceCapabilities;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeGetSurfaceFormats(VkPhysicalDevice    device,
                                                     VkSurfaceKHR        surface,
                                                     std::uint32_t*      count,
                                                     VkSurfaceFormatKHR* formats) noexcept
{
    if (!gVulkanState || device != gVulkanState->mPhysicalDevice || surface != fakeSurface() || !count)
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
    if (!gVulkanState || device != gVulkanState->mPhysicalDevice || surface != fakeSurface() || !count)
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
                                                   const VkSwapchainCreateInfoKHR*,
                                                   const VkAllocationCallbacks*,
                                                   VkSwapchainKHR* swapchain) noexcept
{
    if (!gVulkanState || device != gVulkanState->mDevice || !swapchain)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    ++gVulkanState->mCreateSwapchainCalls;
    *swapchain = gVulkanState->mSwapchain;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroySwapchain(VkDevice device,
                                                VkSwapchainKHR swapchain,
                                                const VkAllocationCallbacks*) noexcept
{
    if (gVulkanState && device == gVulkanState->mDevice && swapchain == gVulkanState->mSwapchain)
    {
        ++gVulkanState->mDestroySwapchainCalls;
        gVulkanState->mNextImageView = 0;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeGetSwapchainImages(VkDevice       device,
                                                      VkSwapchainKHR swapchain,
                                                      std::uint32_t* count,
                                                      VkImage*       images) noexcept
{
    if (!gVulkanState || device != gVulkanState->mDevice || swapchain != gVulkanState->mSwapchain || !count)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!images)
    {
        *count = static_cast<std::uint32_t>(gVulkanState->mImages.size());
        return VK_SUCCESS;
    }
    const std::size_t written = std::min<std::size_t>(*count, gVulkanState->mImages.size());
    std::copy_n(gVulkanState->mImages.begin(), written, images);
    *count = static_cast<std::uint32_t>(written);
    return written == gVulkanState->mImages.size() ? VK_SUCCESS : VK_INCOMPLETE;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateImageView(VkDevice device,
                                                   const VkImageViewCreateInfo*,
                                                   const VkAllocationCallbacks*,
                                                   VkImageView* image_view) noexcept
{
    if (!gVulkanState || device != gVulkanState->mDevice || !image_view || gVulkanState->mNextImageView >= gVulkanState->mImageViews.size())
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    *image_view = gVulkanState->mImageViews[gVulkanState->mNextImageView++];
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyImageView(VkDevice, VkImageView, const VkAllocationCallbacks*) noexcept
{
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateRenderPass(VkDevice device,
                                                     const VkRenderPassCreateInfo*,
                                                     const VkAllocationCallbacks*,
                                                     VkRenderPass* render_pass) noexcept
{
    if (!gVulkanState || device != gVulkanState->mDevice || !render_pass)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    ++gVulkanState->mCreateRenderPassCalls;
    *render_pass = reinterpret_cast<VkRenderPass>(static_cast<std::uintptr_t>(0x80000 + gVulkanState->mCreateRenderPassCalls));
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyRenderPass(VkDevice device,
                                                  VkRenderPass render_pass,
                                                  const VkAllocationCallbacks*) noexcept
{
    if (gVulkanState && device == gVulkanState->mDevice && render_pass != VK_NULL_HANDLE)
    {
        ++gVulkanState->mDestroyRenderPassCalls;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateFramebuffer(VkDevice device,
                                                      const VkFramebufferCreateInfo*,
                                                      const VkAllocationCallbacks*,
                                                      VkFramebuffer* framebuffer) noexcept
{
    if (!gVulkanState || device != gVulkanState->mDevice || !framebuffer)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    ++gVulkanState->mCreateFramebufferCalls;
    *framebuffer = reinterpret_cast<VkFramebuffer>(static_cast<std::uintptr_t>(0x81000 + gVulkanState->mCreateFramebufferCalls));
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyFramebuffer(VkDevice device,
                                                   VkFramebuffer framebuffer,
                                                   const VkAllocationCallbacks*) noexcept
{
    if (gVulkanState && device == gVulkanState->mDevice && framebuffer != VK_NULL_HANDLE)
    {
        ++gVulkanState->mDestroyFramebufferCalls;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateCommandPool(VkDevice device,
                                                     const VkCommandPoolCreateInfo*,
                                                     const VkAllocationCallbacks*,
                                                     VkCommandPool* command_pool) noexcept
{
    if (!gVulkanState || device != gVulkanState->mDevice || !command_pool)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    *command_pool = gVulkanState->mCommandPool;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyCommandPool(VkDevice, VkCommandPool, const VkAllocationCallbacks*) noexcept
{
}

VKAPI_ATTR VkResult VKAPI_CALL fakeAllocateCommandBuffers(VkDevice device,
                                                          const VkCommandBufferAllocateInfo*,
                                                          VkCommandBuffer* command_buffer) noexcept
{
    if (!gVulkanState || device != gVulkanState->mDevice || !command_buffer)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    *command_buffer = gVulkanState->mCommandBuffer;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateSemaphore(VkDevice device,
                                                   const VkSemaphoreCreateInfo*,
                                                   const VkAllocationCallbacks*,
                                                   VkSemaphore* semaphore) noexcept
{
    if (!gVulkanState || device != gVulkanState->mDevice || !semaphore)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    const std::size_t create_index = gVulkanState->mCreateSemaphoreCalls++;
    *semaphore = create_index % 2 == 0 ? gVulkanState->mImageAvailableSemaphore : gVulkanState->mPresentationReadySemaphore;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroySemaphore(VkDevice, VkSemaphore, const VkAllocationCallbacks*) noexcept
{
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateFence(VkDevice device,
                                               const VkFenceCreateInfo*,
                                               const VkAllocationCallbacks*,
                                               VkFence* fence) noexcept
{
    if (!gVulkanState || device != gVulkanState->mDevice || !fence)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    const std::size_t create_index = gVulkanState->mCreateFenceCalls++;
    *fence                         = create_index % 2 == 0 ? gVulkanState->mSubmissionFence : gVulkanState->mPresentCompletionFence;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyFence(VkDevice, VkFence, const VkAllocationCallbacks*) noexcept
{
}

VKAPI_ATTR VkResult VKAPI_CALL fakeWaitForFences(VkDevice device, std::uint32_t fence_count, const VkFence* fences, VkBool32,
                                                 std::uint64_t) noexcept
{
    if (!gVulkanState || device != gVulkanState->mDevice || fence_count == 0 || fence_count > 2 || !fences)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    for (std::uint32_t index = 0; index < fence_count; ++index)
    {
        if (fences[index] != gVulkanState->mSubmissionFence && fences[index] != gVulkanState->mPresentCompletionFence)
        {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
    }
    ++gVulkanState->mWaitCalls;
    const std::size_t index = gVulkanState->mWaitResultIndex++;
    return index < gVulkanState->mWaitResultCount ? gVulkanState->mWaitResults[index] : VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeResetCommandBuffer(VkCommandBuffer command_buffer, VkCommandBufferResetFlags flags) noexcept
{
    return gVulkanState && command_buffer == gVulkanState->mCommandBuffer && flags == 0 ? VK_SUCCESS : VK_ERROR_INITIALIZATION_FAILED;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeBeginCommandBuffer(VkCommandBuffer command_buffer, const VkCommandBufferBeginInfo* begin_info) noexcept
{
    return gVulkanState && command_buffer == gVulkanState->mCommandBuffer && begin_info ? VK_SUCCESS : VK_ERROR_INITIALIZATION_FAILED;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeEndCommandBuffer(VkCommandBuffer command_buffer) noexcept
{
    return gVulkanState && command_buffer == gVulkanState->mCommandBuffer ? gVulkanState->mEndCommandBufferResult
                                                                          : VK_ERROR_INITIALIZATION_FAILED;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeResetFences(VkDevice device, std::uint32_t fence_count, const VkFence* fences) noexcept
{
    if (!gVulkanState || device != gVulkanState->mDevice || fence_count == 0 || fence_count > 2 || !fences)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    for (std::uint32_t index = 0; index < fence_count; ++index)
    {
        if (fences[index] != gVulkanState->mSubmissionFence && fences[index] != gVulkanState->mPresentCompletionFence)
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
    if (!gVulkanState || queue != gVulkanState->mQueue || submit_count != 1 || !submits ||
        (fence != gVulkanState->mSubmissionFence && fence != gVulkanState->mPresentCompletionFence))
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    ++gVulkanState->mQueueSubmitCalls;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeAcquireNextImage(VkDevice       device,
                                                    VkSwapchainKHR swapchain,
                                                    std::uint64_t  timeout,
                                                    VkSemaphore    semaphore,
                                                    VkFence        fence,
                                                    std::uint32_t* image_index) noexcept
{
    if (!gVulkanState || device != gVulkanState->mDevice || swapchain != gVulkanState->mSwapchain ||
        timeout != LLRenderVulkan::VULKAN_SWAPCHAIN_FRAME_ACQUIRE_TIMEOUT_NS || semaphore != gVulkanState->mImageAvailableSemaphore ||
        fence != VK_NULL_HANDLE || !image_index)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    ++gVulkanState->mAcquireNextImageCalls;
    *image_index = gVulkanState->mAcquiredImageIndex;
    return gVulkanState->mAcquireNextImageResult;
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
    if (gVulkanState && command_buffer == gVulkanState->mCommandBuffer && source_stage == VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT &&
        destination_stage == VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT && image_barrier_count == 1 && image_barriers)
    {
        ++gVulkanState->mPipelineBarrierCalls;
    }
}

VKAPI_ATTR void VKAPI_CALL fakeCmdClearColorImage(VkCommandBuffer               command_buffer,
                                                   VkImage                       image,
                                                   VkImageLayout                 image_layout,
                                                   const VkClearColorValue*      color,
                                                   std::uint32_t                 range_count,
                                                   const VkImageSubresourceRange* ranges) noexcept
{
    if (gVulkanState && color && range_count == 1 && ranges)
    {
        ++gVulkanState->mClearColorImageCalls;
        gVulkanState->mClearCommandBuffer = command_buffer;
        gVulkanState->mClearedImage       = image;
        gVulkanState->mClearImageLayout   = image_layout;
        gVulkanState->mClearColor         = *color;
        gVulkanState->mClearRange         = ranges[0];
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeQueuePresent(VkQueue queue, const VkPresentInfoKHR* present_info) noexcept
{
    const auto* fence_info = present_info ? static_cast<const VkSwapchainPresentFenceInfoKHR*>(present_info->pNext) : nullptr;
    if (!gVulkanState || queue != gVulkanState->mQueue || !present_info || present_info->waitSemaphoreCount != 1 ||
        !present_info->pWaitSemaphores || present_info->pWaitSemaphores[0] != gVulkanState->mPresentationReadySemaphore ||
        present_info->swapchainCount != 1 || !present_info->pSwapchains || present_info->pSwapchains[0] != gVulkanState->mSwapchain ||
        !present_info->pImageIndices || present_info->pImageIndices[0] != gVulkanState->mAcquiredImageIndex || !fence_info ||
        fence_info->sType != VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_KHR || fence_info->swapchainCount != 1 ||
        !fence_info->pFences || fence_info->pFences[0] != gVulkanState->mPresentCompletionFence)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    ++gVulkanState->mQueuePresentCalls;
    return gVulkanState->mQueuePresentResult;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeReleaseSwapchainImages(VkDevice device, const VkReleaseSwapchainImagesInfoKHR* release_info) noexcept
{
    if (!gVulkanState || device != gVulkanState->mDevice || !release_info || release_info->swapchain != gVulkanState->mSwapchain ||
        release_info->imageIndexCount != 1 || !release_info->pImageIndices ||
        release_info->pImageIndices[0] != gVulkanState->mAcquiredImageIndex)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    ++gVulkanState->mReleaseSwapchainImagesCalls;
    return gVulkanState->mReleaseSwapchainImagesResult;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL fakeGetDeviceProcAddr(VkDevice device, const char* name) noexcept
{
    if (!gVulkanState || device != gVulkanState->mDevice || !name)
    {
        return nullptr;
    }
#define LL_SDL_VULKAN_DEVICE_COMMAND(command)  \
    if (std::strcmp(name, "vk" #command) == 0) \
    return eraseFunctionType(fake##command)
    if (std::strcmp(name, "vkCreateSwapchainKHR") == 0)
        return eraseFunctionType(fakeCreateSwapchain);
    if (std::strcmp(name, "vkDestroySwapchainKHR") == 0)
        return eraseFunctionType(fakeDestroySwapchain);
    if (std::strcmp(name, "vkGetSwapchainImagesKHR") == 0)
        return eraseFunctionType(fakeGetSwapchainImages);
    LL_SDL_VULKAN_DEVICE_COMMAND(CreateImageView);
    LL_SDL_VULKAN_DEVICE_COMMAND(DestroyImageView);
    LL_SDL_VULKAN_DEVICE_COMMAND(CreateRenderPass);
    LL_SDL_VULKAN_DEVICE_COMMAND(DestroyRenderPass);
    LL_SDL_VULKAN_DEVICE_COMMAND(CreateFramebuffer);
    LL_SDL_VULKAN_DEVICE_COMMAND(DestroyFramebuffer);
    LL_SDL_VULKAN_DEVICE_COMMAND(CreateCommandPool);
    LL_SDL_VULKAN_DEVICE_COMMAND(DestroyCommandPool);
    LL_SDL_VULKAN_DEVICE_COMMAND(AllocateCommandBuffers);
    LL_SDL_VULKAN_DEVICE_COMMAND(CreateSemaphore);
    LL_SDL_VULKAN_DEVICE_COMMAND(DestroySemaphore);
    LL_SDL_VULKAN_DEVICE_COMMAND(CreateFence);
    LL_SDL_VULKAN_DEVICE_COMMAND(DestroyFence);
    if (std::strcmp(name, "vkWaitForFences") == 0)
        return gVulkanState->mExposeWaitForFences ? eraseFunctionType(fakeWaitForFences) : nullptr;
    LL_SDL_VULKAN_DEVICE_COMMAND(ResetCommandBuffer);
    LL_SDL_VULKAN_DEVICE_COMMAND(BeginCommandBuffer);
    LL_SDL_VULKAN_DEVICE_COMMAND(EndCommandBuffer);
    LL_SDL_VULKAN_DEVICE_COMMAND(ResetFences);
    LL_SDL_VULKAN_DEVICE_COMMAND(QueueSubmit);
    if (std::strcmp(name, "vkAcquireNextImageKHR") == 0)
        return eraseFunctionType(fakeAcquireNextImage);
    LL_SDL_VULKAN_DEVICE_COMMAND(CmdPipelineBarrier);
    LL_SDL_VULKAN_DEVICE_COMMAND(CmdClearColorImage);
    if (std::strcmp(name, "vkQueuePresentKHR") == 0)
        return eraseFunctionType(fakeQueuePresent);
    if (std::strcmp(name, "vkReleaseSwapchainImagesKHR") == 0)
        return eraseFunctionType(fakeReleaseSwapchainImages);
#undef LL_SDL_VULKAN_DEVICE_COMMAND
    return nullptr;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL fakeGetInstanceProcAddr(VkInstance instance, const char* name) noexcept
{
    if (!name)
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
    if (instance == fakeInstance() && std::strcmp(name, "vkDestroyInstance") == 0)
    {
        return eraseFunctionType(fakeDestroyInstance);
    }
    if (instance == fakeInstance() && std::strcmp(name, "vkDestroySurfaceKHR") == 0)
    {
        return gVulkanState && gVulkanState->mExposeDestroySurface ? eraseFunctionType(fakeDestroySurface) : nullptr;
    }
    if (instance == fakeInstance() && std::strcmp(name, "vkEnumeratePhysicalDevices") == 0)
        return eraseFunctionType(fakeEnumeratePhysicalDevices);
    if (instance == fakeInstance() && std::strcmp(name, "vkGetPhysicalDeviceProperties") == 0)
        return eraseFunctionType(fakeGetPhysicalDeviceProperties);
    if (instance == fakeInstance() && std::strcmp(name, "vkGetPhysicalDeviceFormatProperties") == 0)
        return eraseFunctionType(fakeGetPhysicalDeviceFormatProperties);
    if (instance == fakeInstance() && std::strcmp(name, "vkGetPhysicalDeviceQueueFamilyProperties") == 0)
        return eraseFunctionType(fakeGetPhysicalDeviceQueueFamilyProperties);
    if (instance == fakeInstance() && std::strcmp(name, "vkGetPhysicalDeviceSurfaceSupportKHR") == 0)
        return eraseFunctionType(fakeGetPhysicalDeviceSurfaceSupport);
    if (instance == fakeInstance() && std::strcmp(name, "vkEnumerateDeviceExtensionProperties") == 0)
        return eraseFunctionType(fakeEnumerateDeviceExtensionProperties);
    if (instance == fakeInstance() && std::strcmp(name, "vkGetPhysicalDeviceFeatures2") == 0)
        return eraseFunctionType(fakeGetPhysicalDeviceFeatures2);
    if (instance == fakeInstance() && std::strcmp(name, "vkGetPhysicalDeviceFeatures") == 0)
        return eraseFunctionType(fakeGetPhysicalDeviceFeatures);
    if (instance == fakeInstance() && std::strcmp(name, "vkCreateDevice") == 0)
        return eraseFunctionType(fakeCreateDevice);
    if (instance == fakeInstance() && std::strcmp(name, "vkDestroyDevice") == 0)
        return eraseFunctionType(fakeDestroyDevice);
    if (instance == fakeInstance() && std::strcmp(name, "vkGetDeviceQueue") == 0)
        return eraseFunctionType(fakeGetDeviceQueue);
    if (instance == fakeInstance() && std::strcmp(name, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR") == 0)
        return eraseFunctionType(fakeGetSurfaceCapabilities);
    if (instance == fakeInstance() && std::strcmp(name, "vkGetPhysicalDeviceSurfaceFormatsKHR") == 0)
        return eraseFunctionType(fakeGetSurfaceFormats);
    if (instance == fakeInstance() && std::strcmp(name, "vkGetPhysicalDeviceSurfacePresentModesKHR") == 0)
        return eraseFunctionType(fakeGetSurfacePresentModes);
    if (instance == fakeInstance() && std::strcmp(name, "vkGetDeviceProcAddr") == 0)
        return eraseFunctionType(fakeGetDeviceProcAddr);
    return nullptr;
}

LLWindowVulkanFunction fakeResolver() noexcept
{
    return reinterpret_cast<LLWindowVulkanFunction>(fakeGetInstanceProcAddr);
}

bool loadLibrary(void* userdata) noexcept
{
    auto& state = *static_cast<FakeState*>(userdata);
    state.record(Event::Load);
    if (state.mFailure == Failure::Load)
    {
        return false;
    }
    ++state.mExplicitLoaderReferences;
    return true;
}

void unloadLibrary(void* userdata) noexcept
{
    auto& state = *static_cast<FakeState*>(userdata);
    state.record(Event::Unload);
    --state.mExplicitLoaderReferences;
}

SDL_Window* createWindow(void* userdata, const LLWindowSDLVulkanCreateInfo& info) noexcept
{
    auto& state = *static_cast<FakeState*>(userdata);
    state.record(Event::Create);
    state.mCreateInfo = &info;
    if (state.mFailure == Failure::Window)
    {
        return nullptr;
    }
    ++state.mWindowLoaderReferences;
    return state.mWindow;
}

void destroyWindow(void* userdata, SDL_Window* window) noexcept
{
    auto& state = *static_cast<FakeState*>(userdata);
    state.record(Event::Destroy);
    if (state.mOwnerDuringDestroy)
    {
        state.mRequirementsInvalidatedBeforeDestroy = !state.mOwnerDuringDestroy->hasRequirements();
    }
    if (window == state.mWindow)
    {
        --state.mWindowLoaderReferences;
    }
}

SDL_WindowFlags getWindowFlags(void* userdata, SDL_Window*) noexcept
{
    auto& state = *static_cast<FakeState*>(userdata);
    state.record(Event::Flags);
    return state.mWindowFlags;
}

bool getWindowSizeInPixels(void* userdata, SDL_Window* window, int* width, int* height) noexcept
{
    auto& state = *static_cast<FakeState*>(userdata);
    state.record(Event::DrawableSize);
    ++state.mDrawableSizeCalls;
    if (!state.mDrawableSizeSucceeds || window != state.mWindow || !width || !height)
    {
        return false;
    }
    *width  = state.mDrawableWidth;
    *height = state.mDrawableHeight;
    return true;
}

LLWindowVulkanFunction getResolver(void* userdata) noexcept
{
    auto& state = *static_cast<FakeState*>(userdata);
    state.record(Event::Resolver);
    return state.mFailure == Failure::Resolver ? nullptr : fakeResolver();
}

const char* const* getInstanceExtensions(void* userdata, std::size_t* count) noexcept
{
    auto& state = *static_cast<FakeState*>(userdata);
    state.record(Event::Extensions);
    if (state.mFailure == Failure::Extensions)
    {
        return nullptr;
    }
    *count = state.mExtensionCount;
    return state.mExtensionNames;
}

bool createSurface(void*                        userdata,
                   SDL_Window*                  window,
                   VkInstance                   instance,
                   const VkAllocationCallbacks* allocator,
                   VkSurfaceKHR*                surface) noexcept
{
    auto& state = *static_cast<FakeState*>(userdata);
    state.record(Event::CreateSurface);
    ++state.mCreateSurfaceCount;
    state.mSurfaceWindow    = window;
    state.mSurfaceInstance  = instance;
    state.mSurfaceAllocator = allocator;

    if (!surface)
    {
        return false;
    }
    if (state.mFailSurfaceCreation)
    {
        *surface = state.mPoisonSurfaceOnFailure ? fakeSurface() : VK_NULL_HANDLE;
        return false;
    }
    *surface = state.mNullSurfaceOnSuccess ? VK_NULL_HANDLE : fakeSurface();
    if (*surface != VK_NULL_HANDLE)
    {
        ++state.mLiveSurfaceCount;
    }
    return true;
}

LLWindowSDLVulkanOperations fakeOperations(FakeState& state) noexcept
{
    return { &state,         loadLibrary,           unloadLibrary, createWindow,          destroyWindow,
             getWindowFlags, getWindowSizeInPixels, getResolver,   getInstanceExtensions, createSurface };
}

LLWindowSDLVulkanCreateInfo createInfo()
{
    return { "Vulkan test window", 13, 17, 1280, 720, false, true, false, true };
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

const LLWindowSDLVulkanAcquireError* acquireError(const LLWindowSDLVulkanAcquireResult& result) noexcept
{
    return std::get_if<LLWindowSDLVulkanAcquireError>(&result);
}

LLWindowSDLVulkan* acquiredWindow(LLWindowSDLVulkanAcquireResult& result) noexcept
{
    return std::get_if<LLWindowSDLVulkan>(&result);
}

void ensureAcquireError(const char* message, const LLWindowSDLVulkanAcquireResult& result, LLWindowSDLVulkanAcquireCode code)
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

bool operationSucceeded(const LLRenderVulkan::VulkanSwapchainFrameSlotParentOperationResult& result,
                        LLRenderVulkan::VulkanSwapchainFrameSlotDisposition                  disposition) noexcept
{
    const auto* value = std::get_if<LLRenderVulkan::VulkanSwapchainFrameSlotDisposition>(&result);
    return value && *value == disposition;
}

const LLRenderVulkan::VulkanSwapchainFrameSlotParentOperationError* presentationError(
    const LLRenderVulkan::VulkanSwapchainFrameSlotParentPresentationResult& result) noexcept
{
    return std::get_if<LLRenderVulkan::VulkanSwapchainFrameSlotParentOperationError>(&result);
}

bool presentationSucceeded(const LLRenderVulkan::VulkanSwapchainFrameSlotParentPresentationResult& result,
                           LLRenderVulkan::VulkanSwapchainFrameSlotPresentationOutcome             outcome,
                           std::uint32_t                                                           image_index) noexcept
{
    const auto* success = std::get_if<LLRenderVulkan::VulkanSwapchainFrameSlotPresentationSuccess>(&result);
    return success && success->mOutcome == outcome && success->mImageIndex == image_index;
}

bool acquireCompleteFrameSlot(LLWindowSDLVulkan& owner) noexcept
{
    using namespace LLRenderVulkan;
    return !owner.acquireInstanceGeneration(VulkanInstanceValidationMode::Disabled, VulkanInstancePortabilityMode::Disabled) &&
           !owner.acquireSurfaceGeneration() && !owner.acquirePresentationDeviceGeneration() && !owner.acquireLogicalDeviceGeneration() &&
           !owner.acquireSwapchainConfigurationGeneration() && !owner.acquireSwapchainGeneration() &&
           !owner.acquireSwapchainImagesGeneration() && !owner.acquireSwapchainPresentationTargetGeneration() &&
           !owner.acquireSwapchainFrameSlotGeneration();
}

void failAllocation()
{
    throw std::bad_alloc();
}

} // namespace

namespace tut
{

struct window_sdl_vulkan_test
{
};

using window_sdl_vulkan_group  = test_group<window_sdl_vulkan_test>;
using window_sdl_vulkan_object = window_sdl_vulkan_group::object;
window_sdl_vulkan_group window_sdl_vulkan_tests("window SDL Vulkan ownership");

template<>
template<>
void window_sdl_vulkan_object::test<1>()
{
    static_assert(!std::is_copy_constructible_v<LLWindowSDLVulkan>);
    static_assert(!std::is_copy_assignable_v<LLWindowSDLVulkan>);
    static_assert(std::is_nothrow_move_constructible_v<LLWindowSDLVulkan>);
    static_assert(std::is_nothrow_move_assignable_v<LLWindowSDLVulkan>);
    static_assert(std::is_nothrow_destructible_v<LLWindowSDLVulkan>);
    static_assert(std::is_same_v<decltype(std::declval<const LLWindowSDLVulkan&>().requirements()), const LLWindowVulkanRequirements*>);
    static_assert(std::is_same_v<decltype(std::declval<LLWindowSDLVulkan&>().acquireSwapchainGeneration()),
                                 LLRenderVulkan::VulkanSwapchainAcquireResult>);
    static_assert(noexcept(std::declval<LLWindowSDLVulkan&>().acquireSwapchainGeneration()));
    static_assert(std::is_same_v<decltype(std::declval<LLWindowSDLVulkan&>().acquireSwapchainImagesGeneration()),
                                 LLRenderVulkan::VulkanSwapchainImagesAcquireResult>);
    static_assert(noexcept(std::declval<LLWindowSDLVulkan&>().acquireSwapchainImagesGeneration()));
    static_assert(std::is_same_v<decltype(std::declval<LLWindowSDLVulkan&>().acquireSwapchainPresentationTargetGeneration()),
                                 LLRenderVulkan::VulkanSwapchainPresentationTargetAcquireResult>);
    static_assert(noexcept(std::declval<LLWindowSDLVulkan&>().acquireSwapchainPresentationTargetGeneration()));
    static_assert(std::is_same_v<decltype(std::declval<LLWindowSDLVulkan&>().acquireSwapchainFrameSlotGeneration()),
                                 LLRenderVulkan::VulkanSwapchainFrameSlotAcquireResult>);
    static_assert(noexcept(std::declval<LLWindowSDLVulkan&>().acquireSwapchainFrameSlotGeneration()));
    static_assert(std::is_same_v<decltype(std::declval<LLWindowSDLVulkan&>().rebuildSwapchainChain()),
                                 LLRenderVulkan::VulkanSwapchainChainRebuildResult>);
    static_assert(noexcept(std::declval<LLWindowSDLVulkan&>().rebuildSwapchainChain()));
    static_assert(std::is_same_v<decltype(std::declval<LLWindowSDLVulkan&>().roundTripEmptySwapchainFrameSlot()),
                                 LLRenderVulkan::VulkanSwapchainFrameSlotParentOperationResult>);
    static_assert(noexcept(std::declval<LLWindowSDLVulkan&>().roundTripEmptySwapchainFrameSlot()));
    static_assert(std::is_same_v<decltype(std::declval<LLWindowSDLVulkan&>().retryEmptySwapchainFrameSlotCompletion()),
                                 LLRenderVulkan::VulkanSwapchainFrameSlotParentOperationResult>);
    static_assert(noexcept(std::declval<LLWindowSDLVulkan&>().retryEmptySwapchainFrameSlotCompletion()));
    static_assert(std::is_same_v<decltype(std::declval<LLWindowSDLVulkan&>().acquireToPresentSwapchainFrameSlot()),
                                 LLRenderVulkan::VulkanSwapchainFrameSlotParentPresentationResult>);
    static_assert(noexcept(std::declval<LLWindowSDLVulkan&>().acquireToPresentSwapchainFrameSlot()));
    static_assert(std::is_same_v<decltype(std::declval<LLWindowSDLVulkan&>().acquireClearToPresentSwapchainFrameSlot(
                                     std::declval<const LLRenderVulkan::VulkanSwapchainFrameClearColor&>())),
                                 LLRenderVulkan::VulkanSwapchainFrameSlotParentPresentationResult>);
    static_assert(noexcept(std::declval<LLWindowSDLVulkan&>().acquireClearToPresentSwapchainFrameSlot(
        std::declval<const LLRenderVulkan::VulkanSwapchainFrameClearColor&>())));
    static_assert(noexcept(std::declval<LLWindowSDLVulkan&>().retrySwapchainFrameSlotPresentation()));
    static_assert(noexcept(std::declval<LLWindowSDLVulkan&>().retrySwapchainFrameSlotPresentationCompletion()));
    static_assert(noexcept(std::declval<LLWindowSDLVulkan&>().cancelSwapchainFrameSlotPresentation()));
    static_assert(noexcept(std::declval<LLWindowSDLVulkan&>().retrySwapchainFrameSlotCancellationCompletion()));
    static_assert(noexcept(std::declval<LLWindowSDLVulkan&>().resetSwapchainFrameSlotGeneration()));
    static_assert(std::is_same_v<decltype(std::declval<LLWindowSDLVulkan&>().resetSwapchainPresentationTargetGeneration()), bool>);
    static_assert(noexcept(std::declval<LLWindowSDLVulkan&>().resetSwapchainPresentationTargetGeneration()));
    static_assert(noexcept(std::declval<LLWindowSDLVulkan&>().resetSwapchainImagesGeneration()));
    static_assert(noexcept(std::declval<LLWindowSDLVulkan&>().resetSwapchainGeneration()));
    static_assert(std::is_same_v<decltype(std::declval<LLWindowSDLVulkan&>().reset()), bool>);
    static_assert(noexcept(std::declval<LLWindowSDLVulkan&>().reset()));
    static_assert(noexcept(acquireLLWindowSDLVulkan(std::declval<const LLWindowSDLVulkanCreateInfo&>(), U64{},
                                                    std::declval<const LLWindowSDLVulkanOperations&>())));

    ensure("the production operation table is complete",
           defaultLLWindowSDLVulkanOperations().mLoadLibrary && defaultLLWindowSDLVulkanOperations().mUnloadLibrary &&
               defaultLLWindowSDLVulkanOperations().mCreateWindow && defaultLLWindowSDLVulkanOperations().mDestroyWindow &&
               defaultLLWindowSDLVulkanOperations().mGetWindowFlags && defaultLLWindowSDLVulkanOperations().mGetWindowSizeInPixels &&
               defaultLLWindowSDLVulkanOperations().mGetResolver && defaultLLWindowSDLVulkanOperations().mGetInstanceExtensions &&
               defaultLLWindowSDLVulkanOperations().mCreateSurface);
}

template<>
template<>
void window_sdl_vulkan_object::test<2>()
{
    FakeState state;
    auto      info   = createInfo();
    auto      result = acquireLLWindowSDLVulkan(info, 41, fakeOperations(state));
    auto*     owner  = acquiredWindow(result);

    ensure("acquisition succeeds", owner != nullptr);
    ensureEvents("acquisition follows the SDL loader and query order", state,
                 { Event::Load, Event::Create, Event::Flags, Event::Resolver, Event::Extensions });
    ensure_equals("the explicit loader reference is held", state.mExplicitLoaderReferences, 1);
    ensure_equals("the Vulkan window loader reference is held", state.mWindowLoaderReferences, 1);
    ensure("the fake receives the exact create description", state.mCreateInfo == &info);
    ensure("the create description preserves title, placement, size, and flags",
           state.mCreateInfo->mTitle == "Vulkan test window" && state.mCreateInfo->mX == 13 && state.mCreateInfo->mY == 17 &&
               state.mCreateInfo->mWidth == 1280 && state.mCreateInfo->mHeight == 720 && !state.mCreateInfo->mResizable &&
               state.mCreateInfo->mFullscreen && !state.mCreateInfo->mHidden && state.mCreateInfo->mHighPixelDensity);
    ensure("the created window is verified as Vulkan-only",
           (state.mWindowFlags & SDL_WINDOW_VULKAN) != 0 && (state.mWindowFlags & SDL_WINDOW_OPENGL) == 0);
    ensure("requirements are published after every native query", owner->hasRequirements());
    ensure("the resolver identity is retained", owner->requirements() && owner->requirements()->resolver() == fakeResolver());
    ensure("the generation is current", owner->isGenerationCurrent(41));
    ensure("zero and another generation are stale", !owner->isGenerationCurrent(0) && !owner->isGenerationCurrent(42));

    state.mExtensionNames[0] = "changed_after_acquire";
    state.mExtensionNames[1] = "changed_too";
    const auto& extensions   = owner->requirements()->requiredInstanceExtensions();
    ensure("SDL extension storage is deep-copied in order",
           extensions.size() == 2 && extensions[0] == "VK_KHR_surface" && extensions[1] == "VK_KHR_xlib_surface");

    state.mOwnerDuringDestroy = owner;
    owner->reset();
    ensureEvents("reset destroys the window before releasing the explicit loader reference", state,
                 { Event::Load, Event::Create, Event::Flags, Event::Resolver, Event::Extensions, Event::Destroy, Event::Unload });
    ensure("requirements are invalid before SDL destroys the window", state.mRequirementsInvalidatedBeforeDestroy);
    ensure("reset exposes no requirements pointer", owner->requirements() == nullptr);
    ensure_equals("reset releases the explicit loader reference", state.mExplicitLoaderReferences, 0);
    ensure_equals("reset releases the window loader reference", state.mWindowLoaderReferences, 0);

    owner->reset();
    ensure_equals("a second reset performs no SDL operation", state.mEventCount, std::size_t{ 7 });
}

template<>
template<>
void window_sdl_vulkan_object::test<3>()
{
    const auto info = createInfo();

    LLWindowSDLVulkanOperations invalid_operations;
    auto                        invalid = acquireLLWindowSDLVulkan(info, 1, invalid_operations);
    ensureAcquireError("an incomplete operation table is rejected", invalid, LLWindowSDLVulkanAcquireCode::InvalidOperations);

    FakeState surface_operation_state;
    auto      missing_surface_operation      = fakeOperations(surface_operation_state);
    missing_surface_operation.mCreateSurface = nullptr;
    auto missing_surface                     = acquireLLWindowSDLVulkan(info, 1, missing_surface_operation);
    ensureAcquireError("a missing SDL surface operation is rejected", missing_surface, LLWindowSDLVulkanAcquireCode::InvalidOperations);
    ensure_equals("a missing SDL surface operation is rejected before loading Vulkan", surface_operation_state.mEventCount,
                  std::size_t{ 0 });

    FakeState size_operation_state;
    auto      missing_size_operation              = fakeOperations(size_operation_state);
    missing_size_operation.mGetWindowSizeInPixels = nullptr;
    auto missing_size                             = acquireLLWindowSDLVulkan(info, 1, missing_size_operation);
    ensureAcquireError("a missing SDL drawable-size operation is rejected", missing_size, LLWindowSDLVulkanAcquireCode::InvalidOperations);
    ensure_equals("a missing SDL drawable-size operation is rejected before loading Vulkan", size_operation_state.mEventCount,
                  std::size_t{ 0 });

    FakeState load_state;
    load_state.mFailure = Failure::Load;
    auto load           = acquireLLWindowSDLVulkan(info, 1, fakeOperations(load_state));
    ensureAcquireError("loader failure is typed", load, LLWindowSDLVulkanAcquireCode::LoaderFailure);
    ensureEvents("loader failure makes no later call", load_state, { Event::Load });

    FakeState window_state;
    window_state.mFailure = Failure::Window;
    auto window           = acquireLLWindowSDLVulkan(info, 1, fakeOperations(window_state));
    ensureAcquireError("window failure is typed", window, LLWindowSDLVulkanAcquireCode::WindowFailure);
    ensureEvents("window failure only releases the explicit reference", window_state, { Event::Load, Event::Create, Event::Unload });

    FakeState resolver_state;
    resolver_state.mFailure = Failure::Resolver;
    auto resolver           = acquireLLWindowSDLVulkan(info, 1, fakeOperations(resolver_state));
    ensureAcquireError("resolver failure is typed", resolver, LLWindowSDLVulkanAcquireCode::ResolverFailure);
    ensureEvents("resolver failure stops before extension query and rolls back in order", resolver_state,
                 { Event::Load, Event::Create, Event::Flags, Event::Resolver, Event::Destroy, Event::Unload });

    FakeState extension_state;
    extension_state.mFailure = Failure::Extensions;
    auto extension           = acquireLLWindowSDLVulkan(info, 1, fakeOperations(extension_state));
    ensureAcquireError("extension query failure is typed", extension, LLWindowSDLVulkanAcquireCode::ExtensionQueryFailure);
    ensureEvents("extension query failure rolls back in order", extension_state,
                 { Event::Load, Event::Create, Event::Flags, Event::Resolver, Event::Extensions, Event::Destroy, Event::Unload });

    FakeState requirements_state;
    requirements_state.mExtensionNames[1] = requirements_state.mExtensionNames[0];
    auto requirements                     = acquireLLWindowSDLVulkan(info, 1, fakeOperations(requirements_state));
    ensureAcquireError("requirements failure is typed", requirements, LLWindowSDLVulkanAcquireCode::RequirementsFailure);
    const auto* requirements_error = acquireError(requirements);
    ensure("the exact requirements error is retained",
           requirements_error && requirements_error->mRequirementsError &&
               requirements_error->mRequirementsError->mCode == LLWindowVulkanRequirementsBuildCode::DuplicateExtensionName &&
               requirements_error->mRequirementsError->mIndex == 1);
    ensureEvents("requirements failure rolls back in order", requirements_state,
                 { Event::Load, Event::Create, Event::Flags, Event::Resolver, Event::Extensions, Event::Destroy, Event::Unload });
}

template<>
template<>
void window_sdl_vulkan_object::test<4>()
{
    const auto info = createInfo();

    FakeState missing_vulkan_state;
    missing_vulkan_state.mWindowFlags = 0;
    auto missing_vulkan               = acquireLLWindowSDLVulkan(info, 1, fakeOperations(missing_vulkan_state));
    ensureAcquireError("a window without the Vulkan flag is rejected", missing_vulkan, LLWindowSDLVulkanAcquireCode::WindowFlagsFailure);
    ensureEvents("a missing Vulkan flag stops before resolver lookup", missing_vulkan_state,
                 { Event::Load, Event::Create, Event::Flags, Event::Destroy, Event::Unload });

    FakeState opengl_state;
    opengl_state.mWindowFlags = SDL_WINDOW_VULKAN | SDL_WINDOW_OPENGL;
    auto opengl               = acquireLLWindowSDLVulkan(info, 1, fakeOperations(opengl_state));
    ensureAcquireError("a Vulkan and OpenGL window is rejected", opengl, LLWindowSDLVulkanAcquireCode::WindowFlagsFailure);
    ensureEvents("an OpenGL-marked window stops before resolver lookup", opengl_state,
                 { Event::Load, Event::Create, Event::Flags, Event::Destroy, Event::Unload });
}

template<>
template<>
void window_sdl_vulkan_object::test<5>()
{
    FakeState  state;
    const auto info   = createInfo();
    auto       result = acquireLLWindowSDLVulkan(info, 9, fakeOperations(state));
    auto*      owner  = acquiredWindow(result);
    ensure("move fixture acquired a window", owner != nullptr);

    LLWindowSDLVulkan moved(std::move(*owner));
    ensure("move construction transfers requirements", moved.hasRequirements() && moved.isGenerationCurrent(9));
    ensure("the moved-from owner publishes no requirements", !owner->hasRequirements());
    ensure_equals("move construction performs no SDL cleanup", state.mEventCount, std::size_t{ 5 });

    state.mOwnerDuringDestroy = &moved;
    moved.reset();
    ensure_equals("the moved owner releases each reference once", state.mExplicitLoaderReferences, 0);
    ensure_equals("the moved owner releases the window reference once", state.mWindowLoaderReferences, 0);

    state.mOwnerDuringDestroy = nullptr;
    state.mEventCount         = 0;
    auto  reused              = acquireLLWindowSDLVulkan(info, 10, fakeOperations(state));
    auto* reused_owner        = acquiredWindow(reused);
    ensure("the fake may reuse the same native address", reused_owner != nullptr);
    ensure("the new generation is current at a reused address",
           reused_owner->isGenerationCurrent(10) && !reused_owner->isGenerationCurrent(9));
}

template<>
template<>
void window_sdl_vulkan_object::test<6>()
{
    const auto info = createInfo();

    FakeState source_state;
    auto      source_result = acquireLLWindowSDLVulkan(info, 21, fakeOperations(source_state));
    auto*     source        = acquiredWindow(source_result);
    ensure("move-assignment source acquired a window", source != nullptr);

    FakeState destination_state;
    destination_state.mWindow = reinterpret_cast<SDL_Window*>(static_cast<std::uintptr_t>(0x56780));
    auto  destination_result  = acquireLLWindowSDLVulkan(info, 22, fakeOperations(destination_state));
    auto* destination         = acquiredWindow(destination_result);
    ensure("move-assignment destination acquired a window", destination != nullptr);

    destination_state.mOwnerDuringDestroy = destination;
    *destination                          = std::move(*source);

    ensure("move assignment invalidates the source", !source->hasRequirements());
    ensure("move assignment transfers the source generation",
           destination->hasRequirements() && destination->isGenerationCurrent(21) && !destination->isGenerationCurrent(22));
    ensure("move assignment invalidates destination requirements before replacement cleanup",
           destination_state.mRequirementsInvalidatedBeforeDestroy);
    ensure_equals("move assignment releases the replaced explicit reference", destination_state.mExplicitLoaderReferences, 0);
    ensure_equals("move assignment releases the replaced window reference", destination_state.mWindowLoaderReferences, 0);
    ensureEvents("move assignment destroys the replaced window before unloading it", destination_state,
                 { Event::Load, Event::Create, Event::Flags, Event::Resolver, Event::Extensions, Event::Destroy, Event::Unload });

    source_state.mOwnerDuringDestroy = destination;
    destination->reset();
    ensure("transferred requirements are invalid before source-window destruction", source_state.mRequirementsInvalidatedBeforeDestroy);
    ensure_equals("the transferred explicit reference is released once", source_state.mExplicitLoaderReferences, 0);
    ensure_equals("the transferred window reference is released once", source_state.mWindowLoaderReferences, 0);
}

template<>
template<>
void window_sdl_vulkan_object::test<7>()
{
    using namespace LLRenderVulkan;

    FakeState         state;
    ScopedVulkanState vulkan_state(state);
    const auto        info   = createInfo();
    auto              result = acquireLLWindowSDLVulkan(info, 31, fakeOperations(state));
    auto*             owner  = acquiredWindow(result);
    ensure("instance fixture acquired a Vulkan window", owner != nullptr);

    const auto acquire_error =
        owner->acquireInstanceGeneration(VulkanInstanceValidationMode::Disabled, VulkanInstancePortabilityMode::Disabled);
    ensure("a validation-disabled fake Vulkan 1.1 instance is acquired", !acquire_error);
    ensure("the instance generation is owned by the SDL window",
           owner->instanceGeneration() && owner->instanceGeneration()->instance() == fakeInstance() &&
               owner->instanceGeneration()->apiVersion() == VK_API_VERSION_1_1 &&
               owner->instanceGeneration()->nativeWindowGeneration() == 31 && !owner->instanceGeneration()->validationEnabled());
    const auto& enabled_extensions = owner->instanceGeneration()->enabledExtensions();
    ensure("explicit diagnostic instance acquisition adds surface maintenance dependencies in exact order",
           state.mSurfaceCapabilities2Enabled && state.mSurfaceMaintenanceEnabled && enabled_extensions.size() == 4 &&
               enabled_extensions[0] == "VK_KHR_surface" && enabled_extensions[1] == "VK_KHR_xlib_surface" &&
               enabled_extensions[2] == SURFACE_CAPABILITIES_2_EXTENSION && enabled_extensions[3] == SURFACE_MAINTENANCE_EXTENSION &&
               owner->requirements()->requiredInstanceExtensions() == std::vector<std::string>{ "VK_KHR_surface", "VK_KHR_xlib_surface" });

    const auto surface_error = owner->acquireSurfaceGeneration();
    ensure("the fake SDL surface is acquired", !surface_error);
    ensure("the instance parent owns the exact fake surface generation",
           owner->instanceGeneration()->hasSurfaceGeneration() && owner->instanceGeneration()->surface() == fakeSurface() &&
               owner->instanceGeneration()->surfaceNativeWindowGeneration() == 31);
    ensure("SDL receives the exact private window, instance, and null allocator",
           state.mSurfaceWindow == state.mWindow && state.mSurfaceInstance == fakeInstance() && !state.mSurfaceAllocator);

    const auto duplicate_surface = owner->acquireSurfaceGeneration();
    ensureSurfaceError("a duplicate surface acquisition is rejected", duplicate_surface, VulkanSurfaceAcquireCode::SurfaceAlreadyOwned);
    ensure_equals("duplicate acquisition makes no second SDL call", state.mCreateSurfaceCount, std::size_t{ 1 });

    const auto duplicate =
        owner->acquireInstanceGeneration(VulkanInstanceValidationMode::Disabled, VulkanInstancePortabilityMode::Disabled);
    ensure("a duplicate instance acquisition is rejected without replacing the owner",
           duplicate && duplicate->mCode == VulkanInstanceAcquireCode::InstanceAlreadyOwned && owner->instanceGeneration() &&
               owner->instanceGeneration()->instance() == fakeInstance());

    LLWindowSDLVulkan moved(std::move(*owner));
    ensure("move construction transfers the instance and surface generations",
           moved.instanceGeneration() && moved.instanceGeneration()->instance() == fakeInstance() &&
               moved.instanceGeneration()->surface() == fakeSurface());
    ensure("the moved-from SDL owner publishes no instance generation", owner->instanceGeneration() == nullptr);

    state.mOwnerDuringSurfaceDestroy  = &moved;
    state.mOwnerDuringInstanceDestroy = &moved;
    state.mOwnerDuringDestroy         = &moved;
    moved.reset();

    ensureEvents("reset destroys the Vulkan surface and instance before requirements, window, and loader teardown", state,
                 { Event::Load, Event::Create, Event::Flags, Event::Resolver, Event::Extensions, Event::CreateInstance,
                   Event::CreateSurface, Event::DestroySurface, Event::DestroyInstance, Event::Destroy, Event::Unload });
    ensure_equals("the Vulkan surface is destroyed exactly once", state.mDestroySurfaceCount, std::size_t{ 1 });
    ensure_equals("the Vulkan instance is destroyed exactly once", state.mDestroyInstanceCount, std::size_t{ 1 });
    ensure("requirements and the parent instance remain live while Vulkan destroys the surface",
           state.mRequirementsLiveDuringSurfaceDestroy && state.mInstanceLiveDuringSurfaceDestroy);
    ensure("both SDL loader references remain live while Vulkan destroys the surface", state.mLoaderLiveDuringSurfaceDestroy);
    ensure("requirements remain live while Vulkan destroys the instance", state.mRequirementsLiveDuringInstanceDestroy);
    ensure("the surface child is absent before Vulkan destroys the instance", state.mSurfaceAbsentDuringInstanceDestroy);
    ensure("both SDL loader references remain live while Vulkan destroys the instance", state.mLoaderLiveDuringInstanceDestroy);
    ensure("requirements are invalidated before SDL destroys the window", state.mRequirementsInvalidatedBeforeDestroy);
    ensure_equals("instance reset releases the explicit loader reference", state.mExplicitLoaderReferences, 0);
    ensure_equals("instance reset releases the window loader reference", state.mWindowLoaderReferences, 0);

    moved.reset();
    ensure_equals("a second instance-owner reset performs no teardown", state.mEventCount, std::size_t{ 11 });
    ensure_equals("a second instance-owner reset does not destroy the surface again", state.mDestroySurfaceCount, std::size_t{ 1 });
    ensure_equals("a second instance-owner reset does not destroy the instance again", state.mDestroyInstanceCount, std::size_t{ 1 });
}

template<>
template<>
void window_sdl_vulkan_object::test<8>()
{
    using namespace LLRenderVulkan;

    FakeState         source_state;
    FakeState         destination_state;
    ScopedVulkanState vulkan_state(source_state);
    const auto        info = createInfo();

    auto  source_result = acquireLLWindowSDLVulkan(info, 51, fakeOperations(source_state));
    auto* source        = acquiredWindow(source_result);
    ensure("move-assignment source acquired a Vulkan window", source != nullptr);
    ensure("move-assignment source acquired an instance",
           !source->acquireInstanceGeneration(VulkanInstanceValidationMode::Disabled, VulkanInstancePortabilityMode::Disabled));
    ensure("move-assignment source acquired a surface", !source->acquireSurfaceGeneration());

    destination_state.mWindow = reinterpret_cast<SDL_Window*>(static_cast<std::uintptr_t>(0x56780));
    vulkan_state.use(destination_state);
    auto  destination_result = acquireLLWindowSDLVulkan(info, 52, fakeOperations(destination_state));
    auto* destination        = acquiredWindow(destination_result);
    ensure("move-assignment destination acquired a Vulkan window", destination != nullptr);
    ensure("move-assignment destination acquired an instance",
           !destination->acquireInstanceGeneration(VulkanInstanceValidationMode::Disabled, VulkanInstancePortabilityMode::Disabled));
    ensure("move-assignment destination acquired a surface", !destination->acquireSurfaceGeneration());

    destination_state.mOwnerDuringSurfaceDestroy  = destination;
    destination_state.mOwnerDuringInstanceDestroy = destination;
    destination_state.mOwnerDuringDestroy         = destination;
    *destination                                  = std::move(*source);

    ensure("move assignment clears both source generations", !source->hasRequirements() && source->instanceGeneration() == nullptr);
    ensure("move assignment transfers the source window, instance, and surface generations",
           destination->isGenerationCurrent(51) && destination->instanceGeneration() &&
               destination->instanceGeneration()->nativeWindowGeneration() == 51 &&
               destination->instanceGeneration()->surfaceNativeWindowGeneration() == 51);
    ensureEvents("move assignment tears down the replaced surface and instance before its SDL resources", destination_state,
                 { Event::Load, Event::Create, Event::Flags, Event::Resolver, Event::Extensions, Event::CreateInstance,
                   Event::CreateSurface, Event::DestroySurface, Event::DestroyInstance, Event::Destroy, Event::Unload });
    ensure("replaced requirements and instance remain live while the replaced surface is destroyed",
           destination_state.mRequirementsLiveDuringSurfaceDestroy && destination_state.mInstanceLiveDuringSurfaceDestroy);
    ensure("replaced loader references remain live while the replaced surface is destroyed",
           destination_state.mLoaderLiveDuringSurfaceDestroy);
    ensure("replaced requirements remain live while the replaced instance is destroyed",
           destination_state.mRequirementsLiveDuringInstanceDestroy);
    ensure("the replaced surface is absent before its instance is destroyed", destination_state.mSurfaceAbsentDuringInstanceDestroy);
    ensure("replaced loader references remain live while the replaced instance is destroyed",
           destination_state.mLoaderLiveDuringInstanceDestroy);
    ensure("replaced requirements are invalid before the replaced SDL window is destroyed",
           destination_state.mRequirementsInvalidatedBeforeDestroy);
    ensure_equals("move assignment destroys the replaced surface once", destination_state.mDestroySurfaceCount, std::size_t{ 1 });
    ensure_equals("move assignment destroys the replaced instance once", destination_state.mDestroyInstanceCount, std::size_t{ 1 });

    vulkan_state.use(source_state);
    source_state.mOwnerDuringSurfaceDestroy  = destination;
    source_state.mOwnerDuringInstanceDestroy = destination;
    source_state.mOwnerDuringDestroy         = destination;
    destination->reset();
    ensureEvents("the transferred owner preserves surface-first teardown for the source resources", source_state,
                 { Event::Load, Event::Create, Event::Flags, Event::Resolver, Event::Extensions, Event::CreateInstance,
                   Event::CreateSurface, Event::DestroySurface, Event::DestroyInstance, Event::Destroy, Event::Unload });
    ensure("transferred requirements and instance remain live while the transferred surface is destroyed",
           source_state.mRequirementsLiveDuringSurfaceDestroy && source_state.mInstanceLiveDuringSurfaceDestroy);
    ensure("transferred loader references remain live while the transferred surface is destroyed",
           source_state.mLoaderLiveDuringSurfaceDestroy);
    ensure("transferred requirements remain live while the transferred instance is destroyed",
           source_state.mRequirementsLiveDuringInstanceDestroy);
    ensure("the transferred surface is absent before its instance is destroyed", source_state.mSurfaceAbsentDuringInstanceDestroy);
    ensure("transferred loader references remain live while the transferred instance is destroyed",
           source_state.mLoaderLiveDuringInstanceDestroy);
    ensure("transferred requirements are invalid before the source SDL window is destroyed",
           source_state.mRequirementsInvalidatedBeforeDestroy);
    ensure_equals("the transferred surface is destroyed once", source_state.mDestroySurfaceCount, std::size_t{ 1 });
    ensure_equals("the transferred instance is destroyed once", source_state.mDestroyInstanceCount, std::size_t{ 1 });
}

template<>
template<>
void window_sdl_vulkan_object::test<9>()
{
    using namespace LLRenderVulkan;

    FakeState         state;
    ScopedVulkanState vulkan_state(state);
    state.mFailInstanceCreation = true;
    const auto info             = createInfo();
    auto       result           = acquireLLWindowSDLVulkan(info, 61, fakeOperations(state));
    auto*      owner            = acquiredWindow(result);
    ensure("instance-failure fixture acquired a Vulkan window", owner != nullptr);

    const auto error = owner->acquireInstanceGeneration(VulkanInstanceValidationMode::Disabled, VulkanInstancePortabilityMode::Disabled);
    ensure("instance creation failure is returned without publishing an owner",
           error && error->mCode == VulkanInstanceAcquireCode::InstanceCreationFailure &&
               error->mResult == VK_ERROR_INITIALIZATION_FAILED && owner->instanceGeneration() == nullptr);

    state.mOwnerDuringDestroy = owner;
    owner->reset();
    ensureEvents("a failed instance acquisition still releases the SDL window and explicit loader", state,
                 { Event::Load, Event::Create, Event::Flags, Event::Resolver, Event::Extensions, Event::CreateInstance, Event::Destroy,
                   Event::Unload });
    ensure_equals("a failed instance acquisition never destroys an unowned instance", state.mDestroyInstanceCount, std::size_t{ 0 });
    ensure("failed acquisition invalidates requirements before SDL destroys the window", state.mRequirementsInvalidatedBeforeDestroy);
    ensure_equals("failed acquisition releases the explicit loader reference", state.mExplicitLoaderReferences, 0);
    ensure_equals("failed acquisition releases the window loader reference", state.mWindowLoaderReferences, 0);
}

template<>
template<>
void window_sdl_vulkan_object::test<10>()
{
    using namespace LLRenderVulkan;

    FakeState         state;
    ScopedVulkanState vulkan_state(state);
    const auto        info   = createInfo();
    auto              result = acquireLLWindowSDLVulkan(info, 71, fakeOperations(state));
    auto*             owner  = acquiredWindow(result);
    ensure("surface-reset fixture acquired a Vulkan window", owner != nullptr);
    ensure("surface-reset fixture acquired an instance",
           !owner->acquireInstanceGeneration(VulkanInstanceValidationMode::Disabled, VulkanInstancePortabilityMode::Disabled));
    ensure("surface-reset fixture acquired a surface", !owner->acquireSurfaceGeneration());

    state.mOwnerDuringSurfaceDestroy = owner;
    ensure("explicit surface reset reports an owned generation", owner->resetSurfaceGeneration());
    ensureEvents("explicit surface reset destroys only the surface", state,
                 { Event::Load, Event::Create, Event::Flags, Event::Resolver, Event::Extensions, Event::CreateInstance,
                   Event::CreateSurface, Event::DestroySurface });
    ensure("explicit surface reset leaves the parent instance and requirements live",
           owner->instanceGeneration() && owner->instanceGeneration()->instance() == fakeInstance() && owner->hasRequirements());
    ensure("explicit surface reset publishes no child", !owner->instanceGeneration()->hasSurfaceGeneration());
    ensure("a second explicit surface reset is idempotent", !owner->resetSurfaceGeneration());
    ensure_equals("idempotent surface reset performs no Vulkan call", state.mEventCount, std::size_t{ 8 });

    ensure("the same current generations may reacquire a surface", !owner->acquireSurfaceGeneration());
    ensure("the reacquired surface is published", owner->instanceGeneration()->surface() == fakeSurface());

    state.mOwnerDuringInstanceDestroy = owner;
    state.mOwnerDuringDestroy         = owner;
    owner->reset();
    ensureEvents("full reset destroys the reacquired surface before its parent and SDL resources", state,
                 { Event::Load, Event::Create, Event::Flags, Event::Resolver, Event::Extensions, Event::CreateInstance,
                   Event::CreateSurface, Event::DestroySurface, Event::CreateSurface, Event::DestroySurface, Event::DestroyInstance,
                   Event::Destroy, Event::Unload });
    ensure_equals("both earned surface generations are destroyed once", state.mDestroySurfaceCount, std::size_t{ 2 });
    ensure_equals("the shared parent instance is destroyed once", state.mDestroyInstanceCount, std::size_t{ 1 });
}

template<>
template<>
void window_sdl_vulkan_object::test<11>()
{
    using namespace LLRenderVulkan;

    FakeState         state;
    ScopedVulkanState vulkan_state(state);
    const auto        info   = createInfo();
    auto              result = acquireLLWindowSDLVulkan(info, 81, fakeOperations(state));
    auto*             owner  = acquiredWindow(result);
    ensure("surface-failure fixture acquired a Vulkan window", owner != nullptr);

    const auto missing_parent = owner->acquireSurfaceGeneration();
    ensureSurfaceError("surface acquisition rejects a missing instance parent", missing_parent, VulkanSurfaceAcquireCode::InstanceNotLive);
    ensure_equals("a missing instance parent makes no SDL surface call", state.mCreateSurfaceCount, std::size_t{ 0 });

    ensure("surface-failure fixture acquired an instance",
           !owner->acquireInstanceGeneration(VulkanInstanceValidationMode::Disabled, VulkanInstancePortabilityMode::Disabled));

    state.mExposeDestroySurface = false;
    const auto missing_destroy  = owner->acquireSurfaceGeneration();
    ensureSurfaceError("a missing surface destroy command fails before SDL creation", missing_destroy,
                       VulkanSurfaceAcquireCode::MissingRequiredInstanceCommand);
    ensure("the exact missing destroy command is retained",
           missing_destroy && missing_destroy->mCommand && *missing_destroy->mCommand == VulkanSurfaceCommand::DestroySurface);
    ensure_equals("a missing destroy command makes no SDL surface call", state.mCreateSurfaceCount, std::size_t{ 0 });

    state.mExposeDestroySurface   = true;
    state.mFailSurfaceCreation    = true;
    state.mPoisonSurfaceOnFailure = true;
    const auto platform_failure   = owner->acquireSurfaceGeneration();
    ensureSurfaceError("SDL false maps to a platform failure", platform_failure, VulkanSurfaceAcquireCode::PlatformCreationFailure);
    ensure("SDL platform failure carries no invented Vulkan result", platform_failure && !platform_failure->mResult);
    ensure("a poisoned failed output is neither published nor destroyed",
           !owner->instanceGeneration()->hasSurfaceGeneration() && state.mDestroySurfaceCount == 0);

    state.mFailSurfaceCreation  = false;
    state.mNullSurfaceOnSuccess = true;
    const auto null_success     = owner->acquireSurfaceGeneration();
    ensureSurfaceError("SDL success with a null handle is rejected", null_success, VulkanSurfaceAcquireCode::NullSurfaceOnSuccess);
    ensure("a null success publishes no child and destroys no surface",
           !owner->instanceGeneration()->hasSurfaceGeneration() && state.mDestroySurfaceCount == 0);

    state.mNullSurfaceOnSuccess = false;
    ensure("a prior platform or null-output failure does not poison later acquisition", !owner->acquireSurfaceGeneration());
    ensure_equals("the three creator paths call SDL exactly three times", state.mCreateSurfaceCount, std::size_t{ 3 });

    state.mOwnerDuringSurfaceDestroy  = owner;
    state.mOwnerDuringInstanceDestroy = owner;
    state.mOwnerDuringDestroy         = owner;
    owner->reset();
    ensure_equals("only the earned surface is destroyed", state.mDestroySurfaceCount, std::size_t{ 1 });
    ensure_equals("failure recovery retains one parent destruction", state.mDestroyInstanceCount, std::size_t{ 1 });
}

template<>
template<>
void window_sdl_vulkan_object::test<12>()
{
    using namespace LLRenderVulkan;

    FakeState         state;
    ScopedVulkanState vulkan_state(state);
    const auto        info   = createInfo();
    auto              result = acquireLLWindowSDLVulkan(info, 91, fakeOperations(state));
    auto*             owner  = acquiredWindow(result);
    ensure("surface-allocation fixture acquired a Vulkan window", owner != nullptr);
    ensure("surface-allocation fixture acquired an instance",
           !owner->acquireInstanceGeneration(VulkanInstanceValidationMode::Disabled, VulkanInstancePortabilityMode::Disabled));

    const auto allocation_failure = LLWindowSDLVulkanDetail::acquireSurfaceGeneration(*owner, failAllocation);
    ensureSurfaceError("child allocation failure is returned through the SDL adapter", allocation_failure,
                       VulkanSurfaceAcquireCode::AllocationFailure);
    ensure("allocation failure leaves the existing parent and requirements live",
           owner->instanceGeneration() && owner->instanceGeneration()->instance() == fakeInstance() && owner->hasRequirements());
    ensure("allocation failure runs no SDL creator and publishes no child",
           state.mCreateSurfaceCount == 0 && !owner->instanceGeneration()->hasSurfaceGeneration());

    ensure("normal acquisition still succeeds after allocation failure", !owner->acquireSurfaceGeneration());
    state.mOwnerDuringSurfaceDestroy  = owner;
    state.mOwnerDuringInstanceDestroy = owner;
    state.mOwnerDuringDestroy         = owner;
    owner->reset();
    ensure_equals("post-allocation recovery destroys one surface", state.mDestroySurfaceCount, std::size_t{ 1 });
    ensure_equals("post-allocation recovery destroys one instance", state.mDestroyInstanceCount, std::size_t{ 1 });
}

template<>
template<>
void window_sdl_vulkan_object::test<13>()
{
    using namespace LLRenderVulkan;

    FakeState         state;
    ScopedVulkanState vulkan_state(state);
    const auto        info   = createInfo();
    auto              result = acquireLLWindowSDLVulkan(info, 101, fakeOperations(state));
    auto*             owner  = acquiredWindow(result);
    ensure("swapchain-adapter fixture acquired a Vulkan window", owner != nullptr);

    const auto missing_instance = owner->acquireSwapchainConfigurationGeneration();
    ensure("swapchain configuration requires a live instance before querying drawable pixels",
           missing_instance && missing_instance->mCode == VulkanSwapchainConfigurationAcquireCode::InstanceNotLive &&
               state.mDrawableSizeCalls == 0);

    ensure("swapchain-adapter fixture acquired an instance",
           !owner->acquireInstanceGeneration(VulkanInstanceValidationMode::Disabled, VulkanInstancePortabilityMode::Disabled));

    state.mDrawableSizeSucceeds = false;
    const auto failed_size      = owner->acquireSwapchainConfigurationGeneration();
    ensure("an SDL drawable-size failure is mapped before parent acquisition",
           failed_size && failed_size->mCode == VulkanSwapchainConfigurationAcquireCode::InvalidDrawableExtent &&
               state.mDrawableSizeCalls == 1);

    state.mDrawableSizeSucceeds = true;
    state.mDrawableWidth        = 0;
    const auto zero_size        = owner->acquireSwapchainConfigurationGeneration();
    ensure("a zero SDL drawable width is rejected",
           zero_size && zero_size->mCode == VulkanSwapchainConfigurationAcquireCode::InvalidDrawableExtent &&
               state.mDrawableSizeCalls == 2);

    state.mDrawableWidth       = 1600;
    state.mDrawableHeight      = 900;
    const auto missing_surface = owner->acquireSwapchainConfigurationGeneration();
    ensure("valid SDL backing pixels are forwarded to the live instance parent",
           missing_surface && missing_surface->mCode == VulkanSwapchainConfigurationAcquireCode::SurfaceNotLive &&
               state.mDrawableSizeCalls == 3);

    ensure("swapchain-adapter fixture acquired a surface", !owner->acquireSurfaceGeneration());
    const auto missing_selection = owner->acquireSwapchainConfigurationGeneration();
    ensure("the SDL adapter forwards current pixels through the exact surface parent",
           missing_selection && missing_selection->mCode == VulkanSwapchainConfigurationAcquireCode::PresentationDeviceNotLive &&
               state.mDrawableSizeCalls == 4);

    state.mOwnerDuringSurfaceDestroy  = owner;
    state.mOwnerDuringInstanceDestroy = owner;
    state.mOwnerDuringDestroy         = owner;
    owner->reset();
    ensure_equals("adapter checks preserve one surface destruction", state.mDestroySurfaceCount, std::size_t{ 1 });
    ensure_equals("adapter checks preserve one instance destruction", state.mDestroyInstanceCount, std::size_t{ 1 });
}

template<>
template<>
void window_sdl_vulkan_object::test<14>()
{
    using namespace LLRenderVulkan;

    FakeState         state;
    ScopedVulkanState vulkan_state(state);
    const auto        info   = createInfo();
    auto              result = acquireLLWindowSDLVulkan(info, 111, fakeOperations(state));
    auto*             owner  = acquiredWindow(result);
    ensure("swapchain-owner fixture acquired a Vulkan window", owner != nullptr);

    const auto missing_instance = owner->acquireSwapchainGeneration();
    ensure("swapchain acquisition requires a live instance before querying drawable pixels",
           missing_instance && missing_instance->mCode == VulkanSwapchainAcquireCode::InstanceNotLive && state.mDrawableSizeCalls == 0);

    ensure("swapchain-owner fixture acquired an instance",
           !owner->acquireInstanceGeneration(VulkanInstanceValidationMode::Disabled, VulkanInstancePortabilityMode::Disabled));

    state.mDrawableSizeSucceeds = false;
    const auto failed_size      = owner->acquireSwapchainGeneration();
    ensure("an SDL drawable-size failure is mapped before swapchain acquisition",
           failed_size && failed_size->mCode == VulkanSwapchainAcquireCode::InvalidDrawableExtent && state.mDrawableSizeCalls == 1);

    state.mDrawableSizeSucceeds = true;
    state.mDrawableHeight       = 0;
    const auto zero_size        = owner->acquireSwapchainGeneration();
    ensure("a zero SDL drawable height is rejected before swapchain acquisition",
           zero_size && zero_size->mCode == VulkanSwapchainAcquireCode::InvalidDrawableExtent && state.mDrawableSizeCalls == 2);

    state.mDrawableWidth       = 1920;
    state.mDrawableHeight      = 1080;
    const auto missing_surface = owner->acquireSwapchainGeneration();
    ensure("current SDL backing pixels are forwarded to the swapchain parent",
           missing_surface && missing_surface->mCode == VulkanSwapchainAcquireCode::SurfaceNotLive && state.mDrawableSizeCalls == 3);

    ensure("swapchain-owner fixture acquired a surface", !owner->acquireSurfaceGeneration());
    const auto missing_selection = owner->acquireSwapchainGeneration();
    ensure("the swapchain adapter re-queries pixels through the exact surface parent",
           missing_selection && missing_selection->mCode == VulkanSwapchainAcquireCode::PresentationDeviceNotLive &&
               state.mDrawableSizeCalls == 4);
    ensure("an unowned swapchain reports no explicit reset", !owner->resetSwapchainGeneration());

    state.mOwnerDuringSurfaceDestroy  = owner;
    state.mOwnerDuringInstanceDestroy = owner;
    state.mOwnerDuringDestroy         = owner;
    owner->reset();
    ensure_equals("swapchain adapter checks preserve one surface destruction", state.mDestroySurfaceCount, std::size_t{ 1 });
    ensure_equals("swapchain adapter checks preserve one instance destruction", state.mDestroyInstanceCount, std::size_t{ 1 });
}

template<>
template<>
void window_sdl_vulkan_object::test<15>()
{
    using namespace LLRenderVulkan;

    FakeState         state;
    ScopedVulkanState vulkan_state(state);
    const auto        info   = createInfo();
    auto              result = acquireLLWindowSDLVulkan(info, 121, fakeOperations(state));
    auto*             owner  = acquiredWindow(result);
    ensure("swapchain-image adapter fixture acquired a Vulkan window", owner != nullptr);

    const auto missing_instance = owner->acquireSwapchainImagesGeneration();
    ensure("swapchain-image acquisition requires a live instance before querying drawable pixels",
           missing_instance && missing_instance->mCode == VulkanSwapchainImagesAcquireCode::InstanceNotLive &&
               state.mDrawableSizeCalls == 0);

    ensure("swapchain-image adapter fixture acquired an instance",
           !owner->acquireInstanceGeneration(VulkanInstanceValidationMode::Disabled, VulkanInstancePortabilityMode::Disabled));

    state.mDrawableSizeSucceeds = false;
    const auto failed_size      = owner->acquireSwapchainImagesGeneration();
    ensure("an SDL drawable-size failure is mapped before swapchain-image acquisition",
           failed_size && failed_size->mCode == VulkanSwapchainImagesAcquireCode::InvalidDrawableExtent && state.mDrawableSizeCalls == 1);

    state.mDrawableSizeSucceeds = true;
    state.mDrawableWidth        = -1;
    const auto invalid_size     = owner->acquireSwapchainImagesGeneration();
    ensure("a negative SDL drawable width is rejected before swapchain-image acquisition",
           invalid_size && invalid_size->mCode == VulkanSwapchainImagesAcquireCode::InvalidDrawableExtent && state.mDrawableSizeCalls == 2);

    state.mDrawableWidth       = 2560;
    state.mDrawableHeight      = 1440;
    const auto missing_surface = owner->acquireSwapchainImagesGeneration();
    ensure("current SDL backing pixels are forwarded to the swapchain-image parent",
           missing_surface && missing_surface->mCode == VulkanSwapchainImagesAcquireCode::SurfaceNotLive && state.mDrawableSizeCalls == 3);

    ensure("swapchain-image adapter fixture acquired a surface", !owner->acquireSurfaceGeneration());
    const auto missing_selection = owner->acquireSwapchainImagesGeneration();
    ensure("the swapchain-image adapter re-queries pixels through the exact surface parent",
           missing_selection && missing_selection->mCode == VulkanSwapchainImagesAcquireCode::PresentationDeviceNotLive &&
               state.mDrawableSizeCalls == 4);
    ensure("an unowned swapchain-image generation reports no explicit reset", !owner->resetSwapchainImagesGeneration());

    state.mOwnerDuringSurfaceDestroy  = owner;
    state.mOwnerDuringInstanceDestroy = owner;
    state.mOwnerDuringDestroy         = owner;
    owner->reset();
    ensure_equals("swapchain-image adapter checks preserve one surface destruction", state.mDestroySurfaceCount, std::size_t{ 1 });
    ensure_equals("swapchain-image adapter checks preserve one instance destruction", state.mDestroyInstanceCount, std::size_t{ 1 });
}

template<>
template<>
void window_sdl_vulkan_object::test<16>()
{
    using namespace LLRenderVulkan;

    FakeState         state;
    ScopedVulkanState vulkan_state(state);
    const auto        info   = createInfo();
    auto              result = acquireLLWindowSDLVulkan(info, 131, fakeOperations(state));
    auto*             owner  = acquiredWindow(result);
    ensure("frame-slot adapter fixture acquired a Vulkan window", owner != nullptr);

    const auto missing_instance = owner->acquireSwapchainFrameSlotGeneration();
    ensure("frame-slot acquisition requires a live instance before querying drawable pixels",
           missing_instance && missing_instance->mCode == VulkanSwapchainFrameSlotAcquireCode::InstanceNotLive &&
               state.mDrawableSizeCalls == 0);
    const auto  missing_result    = owner->roundTripEmptySwapchainFrameSlot();
    const auto* missing_operation = operationError(missing_result);
    ensure("empty submission requires a live instance before querying drawable pixels",
           missing_operation && missing_operation->mCode == VulkanSwapchainFrameSlotParentOperationCode::InstanceNotLive &&
               state.mDrawableSizeCalls == 0);

    ensure("frame-slot adapter fixture acquired an instance",
           !owner->acquireInstanceGeneration(VulkanInstanceValidationMode::Disabled, VulkanInstancePortabilityMode::Disabled));

    state.mDrawableSizeSucceeds = false;
    const auto failed_size      = owner->acquireSwapchainFrameSlotGeneration();
    ensure("an SDL drawable-size failure is mapped before frame-slot acquisition",
           failed_size && failed_size->mCode == VulkanSwapchainFrameSlotAcquireCode::InvalidDrawableExtent &&
               state.mDrawableSizeCalls == 1);
    const auto  failed_result    = owner->roundTripEmptySwapchainFrameSlot();
    const auto* failed_operation = operationError(failed_result);
    ensure("an SDL drawable-size failure is typed before empty submission",
           failed_operation && failed_operation->mCode == VulkanSwapchainFrameSlotParentOperationCode::InvalidDrawableExtent &&
               state.mDrawableSizeCalls == 2);

    state.mDrawableSizeSucceeds = true;
    state.mDrawableHeight       = -1;
    const auto invalid_size     = owner->acquireSwapchainFrameSlotGeneration();
    ensure("a negative SDL drawable height is rejected before frame-slot acquisition",
           invalid_size && invalid_size->mCode == VulkanSwapchainFrameSlotAcquireCode::InvalidDrawableExtent &&
               state.mDrawableSizeCalls == 3);

    state.mDrawableWidth       = 3840;
    state.mDrawableHeight      = 2160;
    const auto missing_surface = owner->acquireSwapchainFrameSlotGeneration();
    ensure("current SDL backing pixels are forwarded to the frame-slot parent",
           missing_surface && missing_surface->mCode == VulkanSwapchainFrameSlotAcquireCode::SurfaceNotLive &&
               state.mDrawableSizeCalls == 4);
    const auto  missing_surface_result    = owner->roundTripEmptySwapchainFrameSlot();
    const auto* missing_surface_operation = operationError(missing_surface_result);
    ensure("current SDL pixels reach the empty-submission parent before a surface exists",
           missing_surface_operation && missing_surface_operation->mCode == VulkanSwapchainFrameSlotParentOperationCode::SurfaceNotLive &&
               state.mDrawableSizeCalls == 5);

    ensure("frame-slot adapter fixture acquired a surface", !owner->acquireSurfaceGeneration());
    const auto missing_selection = owner->acquireSwapchainFrameSlotGeneration();
    ensure("the frame-slot adapter re-queries pixels through the exact surface parent",
           missing_selection && missing_selection->mCode == VulkanSwapchainFrameSlotAcquireCode::PresentationDeviceNotLive &&
               state.mDrawableSizeCalls == 6);
    const auto  missing_selection_result = owner->retryEmptySwapchainFrameSlotCompletion();
    const auto* missing_selection_retry  = operationError(missing_selection_result);
    ensure("completion retry does not query current pixels when no configuration has retained an extent",
           missing_selection_retry &&
               missing_selection_retry->mCode == VulkanSwapchainFrameSlotParentOperationCode::PresentationDeviceNotLive &&
               state.mDrawableSizeCalls == 6);
    ensure("an unowned frame-slot generation reports no explicit reset", !owner->resetSwapchainFrameSlotGeneration());

    state.mOwnerDuringSurfaceDestroy  = owner;
    state.mOwnerDuringInstanceDestroy = owner;
    state.mOwnerDuringDestroy         = owner;
    owner->reset();
    ensure_equals("frame-slot adapter checks preserve one surface destruction", state.mDestroySurfaceCount, std::size_t{ 1 });
    ensure_equals("frame-slot adapter checks preserve one instance destruction", state.mDestroyInstanceCount, std::size_t{ 1 });
}

template<>
template<>
void window_sdl_vulkan_object::test<17>()
{
    using namespace LLRenderVulkan;

    FakeState         state;
    ScopedVulkanState vulkan_state(state);
    auto              result = acquireLLWindowSDLVulkan(createInfo(), 141, fakeOperations(state));
    auto*             owner  = acquiredWindow(result);
    ensure("empty-submission adapter fixture acquired a Vulkan window", owner != nullptr);
    ensure("the adapter fixture acquires the complete frame-slot parent chain", owner && acquireCompleteFrameSlot(*owner));
    const auto* instance = owner->instanceGeneration();
    ensure("frame-slot acquisition remains inert until the explicit adapter call",
           instance && instance->swapchainFrameSlotDisposition() == VulkanSwapchainFrameSlotDisposition::Reusable &&
               state.mWaitCalls == 0 && state.mQueueSubmitCalls == 0);
    const VkSemaphore image_available = instance ? instance->swapchainFrameImageAvailableSemaphore() : VK_NULL_HANDLE;
    ensure("the adapter fixture owns one exact non-null image-available semaphore", image_available != VK_NULL_HANDLE);

    state.mExposeWaitForFences          = false;
    const auto  missing_dispatch_result = owner->roundTripEmptySwapchainFrameSlot();
    const auto* missing_dispatch        = operationError(missing_dispatch_result);
    ensure("a missing execution command remains a typed nested parent failure",
           missing_dispatch && missing_dispatch->mCode == VulkanSwapchainFrameSlotParentOperationCode::OperationFailure &&
               missing_dispatch->mOperationError &&
               missing_dispatch->mOperationError->mCode == VulkanSwapchainFrameSlotOperationCode::MissingRequiredCommand &&
               missing_dispatch->mOperationError->mCommand == VulkanSwapchainFrameSlotCommand::WaitForFences &&
               missing_dispatch->mOperationError->mDisposition == VulkanSwapchainFrameSlotDisposition::Reusable && state.mWaitCalls == 0 &&
               state.mQueueSubmitCalls == 0);

    state.mExposeWaitForFences = true;
    ensure("the first explicit empty round trip succeeds",
           operationSucceeded(owner->roundTripEmptySwapchainFrameSlot(), VulkanSwapchainFrameSlotDisposition::Reusable));
    ensure("the second explicit empty round trip proves reuse",
           operationSucceeded(owner->roundTripEmptySwapchainFrameSlot(), VulkanSwapchainFrameSlotDisposition::Reusable));
    ensure("two adapter cycles cross the queue exactly twice without replacing the image semaphore",
           state.mWaitCalls == 4 && state.mQueueSubmitCalls == 2 && instance->swapchainFrameImageAvailableSemaphore() == image_available);

    state.mWaitResultIndex     = 0;
    state.mWaitResultCount     = 2;
    state.mWaitResults[0]      = VK_SUCCESS;
    state.mWaitResults[1]      = VK_TIMEOUT;
    const auto  pending_result = owner->roundTripEmptySwapchainFrameSlot();
    const auto* pending        = operationError(pending_result);
    ensure("an unknown completion remains a typed nested Pending failure",
           pending && pending->mCode == VulkanSwapchainFrameSlotParentOperationCode::OperationFailure && pending->mOperationError &&
               pending->mOperationError->mCode == VulkanSwapchainFrameSlotOperationCode::CommandFailure &&
               pending->mOperationError->mCommand == VulkanSwapchainFrameSlotCommand::WaitForFences &&
               pending->mOperationError->mResult == VK_TIMEOUT &&
               pending->mOperationError->mDisposition == VulkanSwapchainFrameSlotDisposition::Pending &&
               instance->swapchainFrameSlotDisposition() == VulkanSwapchainFrameSlotDisposition::Pending);
    ensure("every direct and transitive adapter reset refuses Pending without releasing ownership",
           !owner->resetSwapchainFrameSlotGeneration() && !owner->resetSwapchainImagesGeneration() && !owner->resetSwapchainGeneration() &&
               !owner->resetSurfaceGeneration() && !owner->reset() && owner->hasRequirements() && owner->instanceGeneration() == instance &&
               instance->hasSwapchainFrameSlotGeneration());

    const std::size_t drawable_queries_before_retry = state.mDrawableSizeCalls;
    state.mDrawableWidth                            = 0;
    state.mDrawableHeight                           = 0;
    ensure("the explicit completion retry restores reusable state",
           operationSucceeded(owner->retryEmptySwapchainFrameSlotCompletion(), VulkanSwapchainFrameSlotDisposition::Reusable) &&
               instance->swapchainFrameSlotDisposition() == VulkanSwapchainFrameSlotDisposition::Reusable && state.mWaitCalls == 7 &&
               state.mQueueSubmitCalls == 3 && state.mDrawableSizeCalls == drawable_queries_before_retry &&
               instance->swapchainFrameImageAvailableSemaphore() == image_available);
    ensure("child-first reset succeeds after completion retry", owner->resetSwapchainFrameSlotGeneration());
    ensure("the recovered adapter owner completes full teardown", owner->reset());
}

template<>
template<>
void window_sdl_vulkan_object::test<18>()
{
    using namespace LLRenderVulkan;

    FakeState         state;
    ScopedVulkanState vulkan_state(state);
    auto              result = acquireLLWindowSDLVulkan(createInfo(), 151, fakeOperations(state));
    auto*             owner  = acquiredWindow(result);
    ensure("presentation adapter fixture acquires the complete frame-slot chain", owner && acquireCompleteFrameSlot(*owner));
    const auto* instance = owner->instanceGeneration();
    ensure("the adapter exposes all presentation synchronization handles",
           instance && instance->swapchainFrameImageAvailableSemaphore() == state.mImageAvailableSemaphore &&
               instance->swapchainFramePresentationReadySemaphore() == state.mPresentationReadySemaphore &&
               instance->swapchainFrameSubmissionFence() == state.mSubmissionFence &&
               instance->swapchainFramePresentCompletionFence() == state.mPresentCompletionFence);

    const std::size_t drawable_queries_before_cycles = state.mDrawableSizeCalls;
    state.mAcquiredImageIndex                        = 0;
    ensure("the first acquire-to-present adapter cycle succeeds",
           presentationSucceeded(owner->acquireToPresentSwapchainFrameSlot(), VulkanSwapchainFrameSlotPresentationOutcome::Presented, 0));
    state.mAcquiredImageIndex = 1;
    ensure("the second acquire-to-present adapter cycle proves reuse",
           presentationSucceeded(owner->acquireToPresentSwapchainFrameSlot(), VulkanSwapchainFrameSlotPresentationOutcome::Presented, 1));
    ensure("each new cycle samples current geometry and reuses one frame slot",
           state.mDrawableSizeCalls == drawable_queries_before_cycles + 2 && state.mAcquireNextImageCalls == 2 &&
               state.mPipelineBarrierCalls == 2 && state.mQueuePresentCalls == 2 && state.mQueueSubmitCalls == 2 &&
               instance->swapchainFrameSlotDisposition() == VulkanSwapchainFrameSlotDisposition::Reusable &&
               !instance->swapchainFrameAcquiredImageIndex());

    state.mAcquiredImageIndex   = 2;
    state.mQueuePresentResult   = VK_ERROR_OUT_OF_HOST_MEMORY;
    const auto  present_failure = owner->acquireToPresentSwapchainFrameSlot();
    const auto* present_error   = presentationError(present_failure);
    ensure("a retryable present failure retains exact image ownership",
           present_error && present_error->mOperationError &&
               present_error->mOperationError->mCommand == VulkanSwapchainFrameSlotCommand::QueuePresent &&
               present_error->mOperationError->mResult == VK_ERROR_OUT_OF_HOST_MEMORY &&
               instance->swapchainFrameSlotDisposition() == VulkanSwapchainFrameSlotDisposition::PresentationReady &&
               instance->swapchainFrameAcquiredImageIndex() == 2 && !owner->resetSwapchainFrameSlotGeneration());
    const std::size_t drawable_queries_before_retry = state.mDrawableSizeCalls;
    state.mDrawableWidth                            = 0;
    state.mDrawableHeight                           = 0;
    state.mQueuePresentResult                       = VK_SUCCESS;
    ensure("presentation retry uses the retained identity and extent",
           presentationSucceeded(owner->retrySwapchainFrameSlotPresentation(), VulkanSwapchainFrameSlotPresentationOutcome::Presented, 2) &&
               state.mDrawableSizeCalls == drawable_queries_before_retry &&
               instance->swapchainFrameSlotDisposition() == VulkanSwapchainFrameSlotDisposition::Reusable);

    state.mDrawableWidth          = 1280;
    state.mDrawableHeight         = 720;
    state.mAcquiredImageIndex     = 1;
    state.mEndCommandBufferResult = VK_ERROR_UNKNOWN;
    state.mWaitResultIndex        = 0;
    state.mWaitResultCount        = 2;
    state.mWaitResults[0]         = VK_SUCCESS;
    state.mWaitResults[1]         = VK_TIMEOUT;
    const auto  acquired_failure  = owner->acquireToPresentSwapchainFrameSlot();
    const auto* acquired_error    = presentationError(acquired_failure);
    ensure("a post-acquire recording failure retains the acquired image",
           acquired_error && acquired_error->mOperationError &&
               acquired_error->mOperationError->mCommand == VulkanSwapchainFrameSlotCommand::EndCommandBuffer &&
               instance->swapchainFrameSlotDisposition() == VulkanSwapchainFrameSlotDisposition::ImageAcquired &&
               instance->swapchainFrameAcquiredImageIndex() == 1);
    const std::size_t drawable_queries_before_cancel = state.mDrawableSizeCalls;
    state.mDrawableWidth                             = 0;
    state.mDrawableHeight                            = 0;
    state.mEndCommandBufferResult                    = VK_SUCCESS;
    const auto  cancel_failure                       = owner->cancelSwapchainFrameSlotPresentation();
    const auto* cancel_error                         = operationError(cancel_failure);
    ensure("a cancellation wait timeout retains the exact pending obligation",
           cancel_error && cancel_error->mOperationError &&
               cancel_error->mOperationError->mCommand == VulkanSwapchainFrameSlotCommand::WaitForFences &&
               cancel_error->mOperationError->mResult == VK_TIMEOUT &&
               instance->swapchainFrameSlotDisposition() == VulkanSwapchainFrameSlotDisposition::CancellationPending &&
               state.mDrawableSizeCalls == drawable_queries_before_cancel && !owner->reset());
    ensure("cancellation completion retries without querying changed geometry",
           operationSucceeded(owner->retrySwapchainFrameSlotCancellationCompletion(), VulkanSwapchainFrameSlotDisposition::Reusable) &&
               state.mDrawableSizeCalls == drawable_queries_before_cancel && state.mReleaseSwapchainImagesCalls == 1 &&
               !instance->swapchainFrameAcquiredImageIndex());
    ensure("the recovered presentation owner tears down child-first", owner->resetSwapchainFrameSlotGeneration() && owner->reset());
}

template<>
template<>
void window_sdl_vulkan_object::test<19>()
{
    using namespace LLRenderVulkan;

    FakeState         state;
    ScopedVulkanState vulkan_state(state);
    auto              result = acquireLLWindowSDLVulkan(createInfo(), 161, fakeOperations(state));
    auto*             owner  = acquiredWindow(result);
    ensure("swapchain rebuild adapter fixture acquires a Vulkan window", owner != nullptr);

    const auto missing_instance = owner->rebuildSwapchainChain();
    const auto* missing_error    = std::get_if<VulkanSwapchainChainRebuildError>(&missing_instance);
    ensure("swapchain rebuild requires a live instance before sampling SDL pixels",
           missing_error && missing_error->mCode == VulkanSwapchainChainRebuildCode::InstanceNotLive &&
               missing_error->mPhase == VulkanSwapchainChainRebuildPhase::Preflight && state.mDrawableSizeCalls == 0);

    ensure("the rebuild adapter fixture acquires the complete initial chain", acquireCompleteFrameSlot(*owner));
    const VulkanInstanceGeneration* instance = owner->instanceGeneration();
    ensure("the rebuild adapter fixture retains its exact live parent chain",
           instance && instance->surface() == fakeSurface() && instance->physicalDevice() == state.mPhysicalDevice &&
               instance->logicalDevice() == state.mDevice && instance->presentationQueue() == state.mQueue);
    ensure("the initial chain publishes one render pass and one framebuffer per swapchain image",
           instance && instance->hasSwapchainPresentationTargetGeneration() &&
               instance->swapchainPresentationRenderPass() != VK_NULL_HANDLE &&
               instance->swapchainPresentationFramebufferCount() == state.mImages.size() &&
               instance->swapchainPresentationFramebuffer(0) != VK_NULL_HANDLE &&
               instance->swapchainPresentationFramebuffer(1) != VK_NULL_HANDLE &&
               instance->swapchainPresentationFramebuffer(2) != VK_NULL_HANDLE &&
               instance->swapchainPresentationFramebuffer(static_cast<std::uint32_t>(state.mImages.size())) == VK_NULL_HANDLE &&
               state.mCreateRenderPassCalls == 1 && state.mCreateFramebufferCalls == state.mImages.size());

    const std::size_t queries_before_invalid = state.mDrawableSizeCalls;
    state.mDrawableSizeSucceeds              = false;
    const auto failed_query        = owner->rebuildSwapchainChain();
    const auto* failed_query_error = std::get_if<VulkanSwapchainChainRebuildError>(&failed_query);
    ensure("an SDL pixel-query failure is one typed invalid-extent sample",
           failed_query_error && failed_query_error->mCode == VulkanSwapchainChainRebuildCode::InvalidDrawableExtent &&
               failed_query_error->mPhase == VulkanSwapchainChainRebuildPhase::Preflight &&
               state.mDrawableSizeCalls == queries_before_invalid + 1);
    ensure("an SDL pixel-query failure leaves the complete chain intact",
           instance->hasSwapchainConfigurationGeneration() && instance->hasSwapchainGeneration() &&
               instance->hasSwapchainImagesGeneration() && instance->hasSwapchainPresentationTargetGeneration() &&
               instance->hasSwapchainFrameSlotGeneration() && state.mCreateRenderPassCalls == 1 &&
               state.mDestroyRenderPassCalls == 0 && state.mCreateFramebufferCalls == state.mImages.size() &&
               state.mDestroyFramebufferCalls == 0 &&
               state.mCreateSwapchainCalls == 1 && state.mDestroySwapchainCalls == 0);

    state.mDrawableSizeSucceeds = true;
    state.mDrawableWidth        = -1;
    state.mDrawableHeight       = 900;
    const auto negative_width   = owner->rebuildSwapchainChain();
    const auto* negative_error  = std::get_if<VulkanSwapchainChainRebuildError>(&negative_width);
    ensure("a negative SDL pixel dimension is one typed invalid-extent sample",
           negative_error && negative_error->mCode == VulkanSwapchainChainRebuildCode::InvalidDrawableExtent &&
               negative_error->mPhase == VulkanSwapchainChainRebuildPhase::Preflight &&
               state.mDrawableSizeCalls == queries_before_invalid + 2 &&
               instance->hasSwapchainPresentationTargetGeneration() && instance->hasSwapchainFrameSlotGeneration());

    state.mDrawableWidth  = 0;
    state.mDrawableHeight = 900;
    const auto suspended          = owner->rebuildSwapchainChain();
    const auto* suspended_outcome = std::get_if<VulkanSwapchainChainRebuildOutcome>(&suspended);
    ensure("a zero SDL pixel dimension suspends the swapchain chain",
           suspended_outcome && *suspended_outcome == VulkanSwapchainChainRebuildOutcome::Suspended &&
               state.mDrawableSizeCalls == queries_before_invalid + 3);
    ensure("suspension removes all five swapchain children",
           !instance->hasSwapchainConfigurationGeneration() && !instance->hasSwapchainGeneration() &&
               !instance->hasSwapchainImagesGeneration() && !instance->hasSwapchainPresentationTargetGeneration() &&
               !instance->hasSwapchainFrameSlotGeneration() && instance->swapchainPresentationRenderPass() == VK_NULL_HANDLE &&
               instance->swapchainPresentationFramebufferCount() == 0 &&
               instance->swapchainPresentationFramebuffer(0) == VK_NULL_HANDLE && state.mCreateSwapchainCalls == 1 &&
               state.mDestroySwapchainCalls == 1 && state.mCreateRenderPassCalls == 1 &&
               state.mDestroyRenderPassCalls == 1 && state.mCreateFramebufferCalls == state.mImages.size() &&
               state.mDestroyFramebufferCalls == state.mImages.size());
    ensure("suspension preserves the exact SDL instance, surface, physical device, logical device, and queue",
           owner->instanceGeneration() == instance && instance->surface() == fakeSurface() &&
               instance->physicalDevice() == state.mPhysicalDevice && instance->logicalDevice() == state.mDevice &&
               instance->presentationQueue() == state.mQueue && owner->isGenerationCurrent(161));

    state.mDrawableWidth  = 1600;
    state.mDrawableHeight = 900;
    const auto rebuilt          = owner->rebuildSwapchainChain();
    const auto* rebuilt_outcome = std::get_if<VulkanSwapchainChainRebuildOutcome>(&rebuilt);
    ensure("one later nonzero SDL sample rebuilds the complete chain",
           rebuilt_outcome && *rebuilt_outcome == VulkanSwapchainChainRebuildOutcome::Ready &&
               state.mDrawableSizeCalls == queries_before_invalid + 4);
    ensure("the rebuilt configuration retains the new SDL backing-pixel extent",
           instance->hasSwapchainConfigurationGeneration() && instance->swapchainDrawableExtent().width == 1600 &&
               instance->swapchainDrawableExtent().height == 900);
    ensure("the rebuilt chain owns a swapchain, images, presentation targets, and frame slot",
           instance->hasSwapchainGeneration() && instance->hasSwapchainImagesGeneration() &&
               instance->resolvedSwapchainImageCount() == state.mImages.size() &&
               instance->hasSwapchainPresentationTargetGeneration() &&
               instance->swapchainPresentationRenderPass() != VK_NULL_HANDLE &&
               instance->swapchainPresentationFramebufferCount() == state.mImages.size() &&
               instance->swapchainPresentationFramebuffer(0) != VK_NULL_HANDLE &&
               instance->swapchainPresentationFramebuffer(1) != VK_NULL_HANDLE &&
               instance->swapchainPresentationFramebuffer(2) != VK_NULL_HANDLE &&
               instance->hasSwapchainFrameSlotGeneration() && state.mCreateSwapchainCalls == 2 &&
               state.mDestroySwapchainCalls == 1 && state.mCreateRenderPassCalls == 2 &&
               state.mDestroyRenderPassCalls == 1 && state.mCreateFramebufferCalls == state.mImages.size() * 2 &&
               state.mDestroyFramebufferCalls == state.mImages.size());
    ensure("the rebuilt chain still belongs to the exact SDL owner and native-window generation",
           owner->instanceGeneration() == instance && instance->nativeWindowGeneration() == 161 && owner->isGenerationCurrent(161));

    state.mOwnerDuringSurfaceDestroy  = owner;
    state.mOwnerDuringInstanceDestroy = owner;
    state.mOwnerDuringDestroy         = owner;
    ensure("the rebuilt SDL owner tears down child-first", owner->reset());
    ensure_equals("both earned swapchains are destroyed once", state.mDestroySwapchainCalls, std::size_t{ 2 });
    ensure_equals("both earned presentation render passes are destroyed once", state.mDestroyRenderPassCalls, std::size_t{ 2 });
    ensure_equals("every earned presentation framebuffer is destroyed once",
                  state.mDestroyFramebufferCalls,
                  state.mImages.size() * 2);
}

template<>
template<>
void window_sdl_vulkan_object::test<20>()
{
    using namespace LLRenderVulkan;

    VulkanSwapchainFrameClearColor clear_color;
    clear_color.mRgba = { 0.125f, 0.375f, 0.625f, 1.0f };

    FakeState         state;
    ScopedVulkanState vulkan_state(state);
    auto              result = acquireLLWindowSDLVulkan(createInfo(), 171, fakeOperations(state));
    auto*             owner  = acquiredWindow(result);
    ensure("clear-present adapter fixture acquires a Vulkan window", owner != nullptr);

    const auto  missing_instance_result = owner->acquireClearToPresentSwapchainFrameSlot(clear_color);
    const auto* missing_instance        = presentationError(missing_instance_result);
    ensure("clear-present requires a live instance before sampling SDL pixels",
           missing_instance && missing_instance->mCode == VulkanSwapchainFrameSlotParentOperationCode::InstanceNotLive &&
               state.mDrawableSizeCalls == 0);

    ensure("clear-present adapter fixture acquires the complete frame-slot chain", acquireCompleteFrameSlot(*owner));
    const auto* instance = owner->instanceGeneration();
    ensure("clear-present fixture retains the exact SDL owner and native generation",
           instance && owner->isGenerationCurrent(171) && instance->nativeWindowGeneration() == 171);

    const std::size_t drawable_queries_before_failures = state.mDrawableSizeCalls;
    state.mDrawableSizeSucceeds                        = false;
    const auto  failed_query_result = owner->acquireClearToPresentSwapchainFrameSlot(clear_color);
    const auto* failed_query        = presentationError(failed_query_result);
    ensure("clear-present reports one failed SDL backing-pixel sample as invalid extent",
           failed_query && failed_query->mCode == VulkanSwapchainFrameSlotParentOperationCode::InvalidDrawableExtent &&
               state.mDrawableSizeCalls == drawable_queries_before_failures + 1 && state.mAcquireNextImageCalls == 0 &&
               state.mClearColorImageCalls == 0);

    state.mDrawableSizeSucceeds = true;
    state.mDrawableWidth        = -1;
    state.mDrawableHeight       = 720;
    const auto  negative_extent_result = owner->acquireClearToPresentSwapchainFrameSlot(clear_color);
    const auto* negative_extent        = presentationError(negative_extent_result);
    ensure("clear-present rejects a negative SDL backing-pixel dimension before native work",
           negative_extent && negative_extent->mCode == VulkanSwapchainFrameSlotParentOperationCode::InvalidDrawableExtent &&
               state.mDrawableSizeCalls == drawable_queries_before_failures + 2 && state.mAcquireNextImageCalls == 0 &&
               state.mClearColorImageCalls == 0);

    state.mDrawableWidth  = 1280;
    state.mDrawableHeight = 720;
    VulkanSwapchainFrameClearColor invalid_color = clear_color;
    invalid_color.mRgba[0] = -0.01f;
    const auto  invalid_color_result = owner->acquireClearToPresentSwapchainFrameSlot(invalid_color);
    const auto* invalid_color_error  = presentationError(invalid_color_result);
    ensure("clear-present forwards invalid normalized color to the typed core preflight",
           invalid_color_error && invalid_color_error->mCode == VulkanSwapchainFrameSlotParentOperationCode::InvalidClearColor &&
               !invalid_color_error->mOperationError &&
               state.mDrawableSizeCalls == drawable_queries_before_failures + 3 && state.mAcquireNextImageCalls == 0 &&
               state.mClearColorImageCalls == 0);

    state.mAcquiredImageIndex = 2;
    const auto success_result = owner->acquireClearToPresentSwapchainFrameSlot(clear_color);
    ensure("clear-present forwards the exact current SDL owner, generation, extent, and color",
           presentationSucceeded(success_result, VulkanSwapchainFrameSlotPresentationOutcome::Presented, 2) &&
               state.mDrawableSizeCalls == drawable_queries_before_failures + 4 && state.mAcquireNextImageCalls == 1 &&
               state.mClearColorImageCalls == 1 && state.mClearCommandBuffer == state.mCommandBuffer &&
               state.mClearedImage == state.mImages[2] && state.mClearImageLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
               state.mClearColor.float32[0] == clear_color.mRgba[0] && state.mClearColor.float32[1] == clear_color.mRgba[1] &&
               state.mClearColor.float32[2] == clear_color.mRgba[2] && state.mClearColor.float32[3] == clear_color.mRgba[3] &&
               state.mClearRange.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT && state.mClearRange.baseMipLevel == 0 &&
               state.mClearRange.levelCount == 1 && state.mClearRange.baseArrayLayer == 0 && state.mClearRange.layerCount == 1 &&
               state.mQueueSubmitCalls == 1 && state.mQueuePresentCalls == 1 &&
               instance->swapchainFrameSlotDisposition() == VulkanSwapchainFrameSlotDisposition::Reusable &&
               !instance->swapchainFrameAcquiredImageIndex());

    ensure("the clear-present SDL owner tears down child-first", owner->reset());
}

template<>
template<>
void window_sdl_vulkan_object::test<21>()
{
    using namespace LLRenderVulkan;

    FakeState         state;
    ScopedVulkanState vulkan_state(state);
    auto              result = acquireLLWindowSDLVulkan(createInfo(), 181, fakeOperations(state));
    auto*             owner  = acquiredWindow(result);
    ensure("presentation-target adapter fixture acquires a Vulkan window", owner != nullptr);

    const auto missing_instance = owner->acquireSwapchainPresentationTargetGeneration();
    ensure("presentation-target acquisition requires a live instance before querying SDL pixels",
           missing_instance && missing_instance->mCode == VulkanSwapchainPresentationTargetAcquireCode::InstanceNotLive &&
               state.mDrawableSizeCalls == 0);

    ensure("presentation-target adapter fixture acquires an instance",
           !owner->acquireInstanceGeneration(VulkanInstanceValidationMode::Disabled, VulkanInstancePortabilityMode::Disabled));

    state.mDrawableSizeSucceeds = false;
    const auto failed_size      = owner->acquireSwapchainPresentationTargetGeneration();
    ensure("an SDL drawable-size failure is mapped before presentation-target acquisition",
           failed_size && failed_size->mCode == VulkanSwapchainPresentationTargetAcquireCode::InvalidDrawableExtent &&
               state.mDrawableSizeCalls == 1);

    state.mDrawableSizeSucceeds = true;
    state.mDrawableWidth        = 1920;
    state.mDrawableHeight       = 0;
    const auto zero_size        = owner->acquireSwapchainPresentationTargetGeneration();
    ensure("a zero SDL drawable height is rejected before presentation-target acquisition",
           zero_size && zero_size->mCode == VulkanSwapchainPresentationTargetAcquireCode::InvalidDrawableExtent &&
               state.mDrawableSizeCalls == 2);

    state.mDrawableHeight       = 1080;
    const auto missing_surface = owner->acquireSwapchainPresentationTargetGeneration();
    ensure("current SDL backing pixels are forwarded to the presentation-target parent",
           missing_surface && missing_surface->mCode == VulkanSwapchainPresentationTargetAcquireCode::SurfaceNotLive &&
               state.mDrawableSizeCalls == 3);

    ensure("presentation-target adapter fixture acquires a surface", !owner->acquireSurfaceGeneration());
    const auto missing_selection = owner->acquireSwapchainPresentationTargetGeneration();
    ensure("the presentation-target adapter re-queries pixels through the exact surface parent",
           missing_selection &&
               missing_selection->mCode == VulkanSwapchainPresentationTargetAcquireCode::PresentationDeviceNotLive &&
               state.mDrawableSizeCalls == 4);

    ensure("presentation-target adapter fixture acquires the remaining parents through the swapchain",
           !owner->acquirePresentationDeviceGeneration() && !owner->acquireLogicalDeviceGeneration() &&
               !owner->acquireSwapchainConfigurationGeneration() && !owner->acquireSwapchainGeneration());
    const auto missing_images = owner->acquireSwapchainPresentationTargetGeneration();
    ensure("presentation-target acquisition requires the exact swapchain-image parent",
           missing_images && missing_images->mCode == VulkanSwapchainPresentationTargetAcquireCode::SwapchainImagesNotLive &&
               state.mCreateRenderPassCalls == 0 && state.mCreateFramebufferCalls == 0);
    ensure("presentation-target adapter fixture acquires its swapchain-image parent",
           !owner->acquireSwapchainImagesGeneration());
    ensure("presentation-target acquisition succeeds through the authenticated SDL adapter",
           !owner->acquireSwapchainPresentationTargetGeneration());

    const VulkanInstanceGeneration* instance = owner->instanceGeneration();
    const VkRenderPass first_render_pass = instance ? instance->swapchainPresentationRenderPass() : VK_NULL_HANDLE;
    const VkFramebuffer first_framebuffer = instance ? instance->swapchainPresentationFramebuffer(0) : VK_NULL_HANDLE;
    ensure("the SDL adapter publishes one exact presentation target per swapchain image",
           instance && instance->hasSwapchainPresentationTargetGeneration() && first_render_pass != VK_NULL_HANDLE &&
               instance->swapchainPresentationFramebufferCount() == state.mImages.size() &&
               first_framebuffer != VK_NULL_HANDLE && instance->swapchainPresentationFramebuffer(1) != VK_NULL_HANDLE &&
               instance->swapchainPresentationFramebuffer(2) != VK_NULL_HANDLE &&
               first_framebuffer != instance->swapchainPresentationFramebuffer(1) &&
               instance->swapchainPresentationFramebuffer(1) != instance->swapchainPresentationFramebuffer(2) &&
               instance->swapchainPresentationFramebuffer(static_cast<std::uint32_t>(state.mImages.size())) == VK_NULL_HANDLE &&
               state.mCreateRenderPassCalls == 1 && state.mCreateFramebufferCalls == state.mImages.size() &&
               state.mDestroyRenderPassCalls == 0 && state.mDestroyFramebufferCalls == 0);

    const auto duplicate = owner->acquireSwapchainPresentationTargetGeneration();
    ensure("duplicate presentation-target acquisition is typed and does not create native resources",
           duplicate &&
               duplicate->mCode == VulkanSwapchainPresentationTargetAcquireCode::SwapchainPresentationTargetAlreadyOwned &&
               state.mCreateRenderPassCalls == 1 && state.mCreateFramebufferCalls == state.mImages.size());

    ensure("the complete-chain order admits a frame slot after the presentation target",
           !owner->acquireSwapchainFrameSlotGeneration() && instance->hasSwapchainFrameSlotGeneration());
    ensure("explicit presentation-target reset retires the younger frame slot and every target handle",
           owner->resetSwapchainPresentationTargetGeneration() &&
               !instance->hasSwapchainPresentationTargetGeneration() && !instance->hasSwapchainFrameSlotGeneration() &&
               instance->swapchainPresentationRenderPass() == VK_NULL_HANDLE &&
               instance->swapchainPresentationFramebufferCount() == 0 &&
               instance->swapchainPresentationFramebuffer(0) == VK_NULL_HANDLE && state.mDestroyRenderPassCalls == 1 &&
               state.mDestroyFramebufferCalls == state.mImages.size());
    ensure("an unowned presentation target reports no adapter-level reset",
           !owner->resetSwapchainPresentationTargetGeneration());

    ensure("the retained image parents can acquire a fresh presentation target",
           !owner->acquireSwapchainPresentationTargetGeneration() &&
               instance->swapchainPresentationRenderPass() != VK_NULL_HANDLE &&
               instance->swapchainPresentationRenderPass() != first_render_pass &&
               instance->swapchainPresentationFramebufferCount() == state.mImages.size() &&
               instance->swapchainPresentationFramebuffer(0) != VK_NULL_HANDLE &&
               instance->swapchainPresentationFramebuffer(0) != first_framebuffer && state.mCreateRenderPassCalls == 2 &&
               state.mCreateFramebufferCalls == state.mImages.size() * 2);
    ensure("resetting the image parent retires its presentation target first",
           owner->resetSwapchainImagesGeneration() && !instance->hasSwapchainImagesGeneration() &&
               !instance->hasSwapchainPresentationTargetGeneration() && instance->swapchainPresentationRenderPass() == VK_NULL_HANDLE &&
               instance->swapchainPresentationFramebufferCount() == 0 && state.mDestroyRenderPassCalls == 2 &&
               state.mDestroyFramebufferCalls == state.mImages.size() * 2);

    state.mOwnerDuringSurfaceDestroy  = owner;
    state.mOwnerDuringInstanceDestroy = owner;
    state.mOwnerDuringDestroy         = owner;
    ensure("presentation-target adapter fixture tears down its retained parents", owner->reset());
    ensure_equals("presentation-target adapter checks preserve one surface destruction", state.mDestroySurfaceCount, std::size_t{ 1 });
    ensure_equals("presentation-target adapter checks preserve one instance destruction", state.mDestroyInstanceCount, std::size_t{ 1 });
}

} // namespace tut
