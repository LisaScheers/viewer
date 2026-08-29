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
    DestroyImageView
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
    DestroyImageView,
    DestroySwapchain,
    DestroyDevice,
    DestroySurface,
    DestroyDebugMessenger,
    DestroyInstance
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
    std::vector<std::string>        mDeviceExtensions{ VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkBool32                        mPresentationSupported = VK_TRUE;
    VkPhysicalDeviceFeatures        mSupportedFeatures{};
    VkResult                        mSwapchainCapabilitiesResult = VK_SUCCESS;
    VkSurfaceCapabilitiesKHR        mSwapchainCapabilities{};
    std::vector<VkSurfaceFormatKHR> mSwapchainFormats{ { VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR } };
    std::vector<VkPresentModeKHR>   mSwapchainPresentModes{ VK_PRESENT_MODE_FIFO_KHR };

    VkInstance                           mInstance       = fakeHandle<VkInstance>(0x1111);
    VkDebugUtilsMessengerEXT             mDebugMessenger = fakeHandle<VkDebugUtilsMessengerEXT>(0x2222);
    VkSurfaceKHR                         mSurface        = fakeHandle<VkSurfaceKHR>(0x3333);
    VkDevice                             mDevice         = fakeHandle<VkDevice>(0x5555);
    VkQueue                              mQueue          = fakeHandle<VkQueue>(0x6666);
    VkSwapchainKHR                       mSwapchain      = fakeHandle<VkSwapchainKHR>(0x7777);
    std::vector<VkImage>     mSwapchainImages{ fakeHandle<VkImage>(0x8001), fakeHandle<VkImage>(0x8002), fakeHandle<VkImage>(0x8003) };
    std::vector<VkImageView> mSwapchainImageViews{ fakeHandle<VkImageView>(0x9001), fakeHandle<VkImageView>(0x9002),
                                                   fakeHandle<VkImageView>(0x9003) };
    std::vector<Event>                   mEvents;
    std::vector<std::string>             mEnabledExtensions;
    std::vector<std::string>             mEnabledLayers;
    VkInstanceCreateFlags                mInstanceFlags       = 0;
    std::uint32_t                        mRequestedApiVersion = 0;
    PFN_vkDebugUtilsMessengerCallbackEXT mValidationCallback  = nullptr;
    void*                                mValidationUserdata  = nullptr;

    std::size_t mVersionCalls         = 0;
    std::size_t mExtensionCountCalls  = 0;
    std::size_t mExtensionValuesCalls = 0;
    std::size_t mLayerCountCalls      = 0;
    std::size_t mLayerValuesCalls     = 0;
    std::size_t mDestroyInstanceCalls = 0;
    std::size_t mDestroyDebugCalls    = 0;

    std::size_t                     mDestroySurfaceResolutionCalls         = 0;
    VkInstance                      mDestroySurfaceResolutionInstance      = VK_NULL_HANDLE;
    std::size_t                     mCreateSurfaceCalls                    = 0;
    VkInstance                      mCreateSurfaceInstance                 = VK_NULL_HANDLE;
    const VkAllocationCallbacks*    mCreateSurfaceAllocationCallbacks      = nullptr;
    std::size_t                     mDestroySurfaceCalls                   = 0;
    VkInstance                      mDestroySurfaceInstance                = VK_NULL_HANDLE;
    VkSurfaceKHR                    mDestroyedSurface                      = VK_NULL_HANDLE;
    const VkAllocationCallbacks*    mDestroySurfaceAllocationCallbacks     = nullptr;
    const VulkanInstanceGeneration* mSurfaceDestroyOwner                   = nullptr;
    bool                            mSurfaceDestroyObservationMade         = false;
    bool                            mObservedPresentationAtSurfaceDestroy  = false;
    bool                            mObservedLogicalAtSurfaceDestroy       = false;
    bool                            mObservedConfigurationAtSurfaceDestroy = false;
    bool                            mObservedSwapchainAtSurfaceDestroy     = false;
    bool                            mObservedSwapchainImagesAtSurfaceDestroy = false;

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

    std::size_t                     mPhysicalFeaturesCalls                = 0;
    VkPhysicalDevice                mPhysicalFeaturesDevice               = VK_NULL_HANDLE;
    std::size_t                     mCreateDeviceCalls                    = 0;
    std::size_t                     mDestroyDeviceCalls                   = 0;
    std::size_t                     mGetDeviceQueueCalls                  = 0;
    VkDevice                        mGetDeviceQueueDevice                 = VK_NULL_HANDLE;
    std::uint32_t                   mGetDeviceQueueFamily                 = VK_QUEUE_FAMILY_IGNORED;
    std::uint32_t                   mGetDeviceQueueIndex                  = std::numeric_limits<std::uint32_t>::max();
    const VulkanInstanceGeneration* mDeviceDestroyOwner                   = nullptr;
    bool                            mDeviceDestroyObservationMade         = false;
    bool                            mObservedPresentationAtDeviceDestroy  = false;
    bool                            mObservedSurfaceAtDeviceDestroy       = false;
    bool                            mObservedConfigurationAtDeviceDestroy = false;
    bool                            mObservedSwapchainAtDeviceDestroy     = false;
    bool                            mObservedSwapchainImagesAtDeviceDestroy = false;
    std::size_t                     mSwapchainCapabilitiesCalls           = 0;
    std::size_t                     mSwapchainFormatCountCalls            = 0;
    std::size_t                     mSwapchainFormatListCalls             = 0;
    std::size_t                     mSwapchainPresentModeCountCalls       = 0;
    std::size_t                     mSwapchainPresentModeListCalls        = 0;

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
    const VkAllocationCallbacks*    mCreateSwapchainAllocationCallbacks      = nullptr;
    std::size_t                     mDestroySwapchainCalls                   = 0;
    VkDevice                        mDestroySwapchainDevice                  = VK_NULL_HANDLE;
    VkSwapchainKHR                  mDestroyedSwapchain                      = VK_NULL_HANDLE;
    const VkAllocationCallbacks*    mDestroySwapchainAllocationCallbacks     = nullptr;
    const VulkanInstanceGeneration* mSwapchainDestroyOwner                   = nullptr;
    bool                            mSwapchainDestroyObservationMade         = false;
    bool                            mObservedConfigurationAtSwapchainDestroy = false;
    bool                            mObservedLogicalAtSwapchainDestroy       = false;
    bool                            mObservedSurfaceAtSwapchainDestroy       = false;
    bool                            mObservedSwapchainImagesAtSwapchainDestroy = false;

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

    bool        mGenerationCurrent   = true;
    std::size_t mGenerationChecks    = 0;
    std::size_t mFailGenerationCheck = 0;

    const VulkanInstanceGeneration* mExpectedInstanceOwner  = nullptr;
    bool                            mInstanceOwnerCurrent   = true;
    std::size_t                     mInstanceOwnerChecks    = 0;
    std::size_t                     mFailInstanceOwnerCheck = 0;
    bool                            mSurfaceWindowCurrent   = true;
    std::size_t                     mSurfaceWindowChecks    = 0;
    std::size_t                     mFailSurfaceWindowCheck = 0;

    FakeState()
    {
        mPhysicalDeviceProperties.apiVersion = VK_API_VERSION_1_1;
        mPhysicalDeviceProperties.deviceType = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
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
        mSwapchainCapabilities.supportedUsageFlags     = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
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
        gFakeState->mSurfaceDestroyObservationMade         = true;
        gFakeState->mObservedPresentationAtSurfaceDestroy  = gFakeState->mSurfaceDestroyOwner->hasPresentationDeviceGeneration();
        gFakeState->mObservedLogicalAtSurfaceDestroy       = gFakeState->mSurfaceDestroyOwner->hasLogicalDeviceGeneration();
        gFakeState->mObservedConfigurationAtSurfaceDestroy = gFakeState->mSurfaceDestroyOwner->hasSwapchainConfigurationGeneration();
        gFakeState->mObservedSwapchainAtSurfaceDestroy     = gFakeState->mSurfaceDestroyOwner->hasSwapchainGeneration();
        gFakeState->mObservedSwapchainImagesAtSurfaceDestroy = gFakeState->mSurfaceDestroyOwner->hasSwapchainImagesGeneration();
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
        gFakeState->mDeviceDestroyObservationMade         = true;
        gFakeState->mObservedPresentationAtDeviceDestroy  = gFakeState->mDeviceDestroyOwner->hasPresentationDeviceGeneration();
        gFakeState->mObservedSurfaceAtDeviceDestroy       = gFakeState->mDeviceDestroyOwner->hasSurfaceGeneration();
        gFakeState->mObservedConfigurationAtDeviceDestroy = gFakeState->mDeviceDestroyOwner->hasSwapchainConfigurationGeneration();
        gFakeState->mObservedSwapchainAtDeviceDestroy     = gFakeState->mDeviceDestroyOwner->hasSwapchainGeneration();
        gFakeState->mObservedSwapchainImagesAtDeviceDestroy = gFakeState->mDeviceDestroyOwner->hasSwapchainImagesGeneration();
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
    gFakeState->mDestroySwapchainAllocationCallbacks = allocation_callbacks;
    if (gFakeState->mSwapchainDestroyOwner)
    {
        gFakeState->mSwapchainDestroyObservationMade         = true;
        gFakeState->mObservedConfigurationAtSwapchainDestroy = gFakeState->mSwapchainDestroyOwner->hasSwapchainConfigurationGeneration();
        gFakeState->mObservedLogicalAtSwapchainDestroy       = gFakeState->mSwapchainDestroyOwner->hasLogicalDeviceGeneration();
        gFakeState->mObservedSurfaceAtSwapchainDestroy       = gFakeState->mSwapchainDestroyOwner->hasSurfaceGeneration();
        gFakeState->mObservedSwapchainImagesAtSwapchainDestroy = gFakeState->mSwapchainDestroyOwner->hasSwapchainImagesGeneration();
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
    }
    gFakeState->mEvents.push_back(Event::DestroyImageView);
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

void failAllocation()
{
    throw std::bad_alloc();
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

using render_vulkan_instance_test_group  = test_group<render_vulkan_instance_test>;
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
    state.mDeviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME, "VK_KHR_portability_subset" };
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
           owner.portabilitySubsetRequired() && required.size() == 2 && required[0] == VK_KHR_SWAPCHAIN_EXTENSION_NAME &&
               required[1] == "VK_KHR_portability_subset");
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
    ensure(
        "the parent exposes the authenticated logical device, queue, feature, and extension policy",
        owner.hasLogicalDeviceGeneration() && owner.logicalDevice() == state.mDevice && owner.presentationQueue() == state.mQueue &&
            owner.logicalDevicePhysicalDevice() == owner.physicalDevice() && owner.logicalDevicePhysicalDevice() == state.mPhysicalDevice &&
            owner.logicalDeviceQueueFamilyIndex() == owner.presentationQueueFamilyIndex() && owner.logicalDeviceQueueFamilyIndex() == 0 &&
            owner.logicalDeviceQueueIndex() == 0 && owner.logicalDeviceEnabledFeatures().independentBlend == VK_TRUE &&
            extensions.size() == 1 && extensions[0] == VK_KHR_SWAPCHAIN_EXTENSION_NAME && !owner.portabilitySubsetEnabled());
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
               owner.swapchainImageArrayLayers() == 1 && owner.swapchainImageUsage() == VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT &&
               owner.swapchainImageSharingMode() == VK_SHARING_MODE_EXCLUSIVE &&
               owner.swapchainPreTransform() == VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR &&
               owner.swapchainCompositeAlpha() == VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR && owner.swapchainClipped() == VK_TRUE);
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

} // namespace tut
