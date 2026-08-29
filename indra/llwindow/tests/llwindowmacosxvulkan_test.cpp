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
#include <type_traits>
#include <utility>
#include <vector>

namespace
{

enum class Event
{
    OpenLoader,
    GetResolver,
    CreateNative,
    RefreshNative,
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

    constexpr std::array<const char*, 4> extensions{ VK_KHR_SURFACE_EXTENSION_NAME, "VK_EXT_metal_surface",
                                                     VK_EXT_DEBUG_UTILS_EXTENSION_NAME, "VK_KHR_portability_enumeration" };
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

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateInstance(const VkInstanceCreateInfo*, const VkAllocationCallbacks*, VkInstance* instance) noexcept
{
    if (!gState || !instance)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    gState->record(Event::CreateInstance);
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
    return { &state,       isMainThread, openLoader, closeLoader, getResolver, createNativeWindow, refreshNativeWindow, destroyNativeWindow,
             createSurface };
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
    static_assert(noexcept(std::declval<LLWindowMacOSXVulkan&>().resetSwapchainImagesGeneration()));
    static_assert(noexcept(std::declval<LLWindowMacOSXVulkan&>().resetSwapchainGeneration()));
    static_assert(std::is_same_v<decltype(std::declval<const LLWindowMacOSXVulkan&>().requirements()), const LLWindowVulkanRequirements*>);
    static_assert(noexcept(acquireLLWindowMacOSXVulkan(std::declval<const LLWindowMacOSXVulkanCreateInfo&>(),
                                                       U64{},
                                                       std::declval<const LLWindowMacOSXVulkanOperations&>())));

    const auto& operations = defaultLLWindowMacOSXVulkanOperations();
    ensure("the production operation table is complete",
           operations.mIsMainThread && operations.mOpenLoader && operations.mCloseLoader && operations.mGetResolver &&
               operations.mCreateNativeWindow && operations.mRefreshNativeWindow && operations.mDestroyNativeWindow &&
               operations.mCreateSurface);
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

} // namespace tut
