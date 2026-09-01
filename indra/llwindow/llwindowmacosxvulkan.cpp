/**
 * @file llwindowmacosxvulkan.cpp
 * @brief Isolated Cocoa and Metal Vulkan surface ownership.
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

#include "llwindowmacosxvulkan-objc.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <dlfcn.h>
#include <exception>
#include <new>
#include <pthread.h>
#include <utility>

namespace
{

constexpr char DEFAULT_VULKAN_LOADER[] = "libvulkan.1.dylib";
constexpr std::array<const char*, 2> REQUIRED_EXTENSIONS{ VK_KHR_SURFACE_EXTENSION_NAME, VK_EXT_METAL_SURFACE_EXTENSION_NAME };
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

bool validOperations(const LLWindowMacOSXVulkanOperations& operations) noexcept
{
    return operations.mIsMainThread && operations.mOpenLoader && operations.mCloseLoader && operations.mGetResolver &&
           operations.mCreateNativeWindow && operations.mRefreshNativeWindow && operations.mDestroyNativeWindow &&
           operations.mCreateSurface;
}

bool validIdentity(const LLWindowMacOSXVulkanNativeWindow& native_window) noexcept
{
    return native_window.mToken && native_window.mWindow && native_window.mView && native_window.mMetalLayer;
}

bool validGeometry(const LLWindowMacOSXVulkanNativeWindow& native_window) noexcept
{
    return std::isfinite(native_window.mBackingScale) && native_window.mBackingScale > 0.0 && native_window.mDrawableWidth != 0 &&
           native_window.mDrawableHeight != 0;
}

bool sameIdentity(const LLWindowMacOSXVulkanNativeWindow& left, const LLWindowMacOSXVulkanNativeWindow& right) noexcept
{
    return left.mToken == right.mToken && left.mWindow == right.mWindow && left.mView == right.mView &&
           left.mMetalLayer == right.mMetalLayer;
}

LLWindowMacOSXVulkanAcquireError failure(
    LLWindowMacOSXVulkanAcquireCode                      code,
    std::optional<LLWindowMacOSXVulkanNativeCreateError> native_error       = std::nullopt,
    std::optional<LLWindowVulkanRequirementsBuildError>  requirements_error = std::nullopt) noexcept
{
    return { code, native_error, requirements_error };
}

LLWindowMacOSXVulkanNativeCreateCode nativeFailureCode(LLWindowMacOSXVulkanStatus status) noexcept
{
    switch (status)
    {
        case LLWINDOWMACOSXVULKAN_STATUS_INVALID_ARGUMENT:
            return LLWindowMacOSXVulkanNativeCreateCode::InvalidRequest;
        case LLWINDOWMACOSXVULKAN_STATUS_MAIN_THREAD_REQUIRED:
            return LLWindowMacOSXVulkanNativeCreateCode::MainThreadFailure;
        case LLWINDOWMACOSXVULKAN_STATUS_STORAGE_FAILED:
            return LLWindowMacOSXVulkanNativeCreateCode::StorageFailure;
        case LLWINDOWMACOSXVULKAN_STATUS_WINDOW_FAILED:
            return LLWindowMacOSXVulkanNativeCreateCode::WindowFailure;
        case LLWINDOWMACOSXVULKAN_STATUS_VIEW_FAILED:
            return LLWindowMacOSXVulkanNativeCreateCode::ViewFailure;
        case LLWINDOWMACOSXVULKAN_STATUS_LAYER_FAILED:
            return LLWindowMacOSXVulkanNativeCreateCode::LayerFailure;
        case LLWINDOWMACOSXVULKAN_STATUS_DRAWABLE_UNAVAILABLE:
        case LLWINDOWMACOSXVULKAN_STATUS_GEOMETRY_FAILED:
        case LLWINDOWMACOSXVULKAN_STATUS_DESTROY_FAILED:
            return LLWindowMacOSXVulkanNativeCreateCode::GeometryFailure;
        case LLWINDOWMACOSXVULKAN_STATUS_APPLICATION_FAILED:
        case LLWINDOWMACOSXVULKAN_STATUS_SUCCESS:
            return LLWindowMacOSXVulkanNativeCreateCode::ApplicationFailure;
    }
    return LLWindowMacOSXVulkanNativeCreateCode::ApplicationFailure;
}

LLWindowMacOSXVulkanNative bridgeNative(const LLWindowMacOSXVulkanNativeWindow& native_window) noexcept
{
    return { native_window.mToken,          native_window.mWindow,        native_window.mView,
             native_window.mMetalLayer,     native_window.mBackingScale,  native_window.mDrawableWidth,
             native_window.mDrawableHeight };
}

LLWindowMacOSXVulkanNativeWindow ownedNative(const LLWindowMacOSXVulkanNative& native) noexcept
{
    return { native.token,          native.window,         native.view,           native.layer,
             native.contents_scale, native.drawable_width, native.drawable_height };
}

bool isMainThread(void*) noexcept
{
    return pthread_main_np() != 0;
}

void* openLoader(void*, const char* path) noexcept
{
    const char* selected_path = path && path[0] != '\0' ? path : DEFAULT_VULKAN_LOADER;
    return dlopen(selected_path, RTLD_NOW | RTLD_LOCAL);
}

void closeLoader(void*, void* loader) noexcept
{
    if (loader)
    {
        dlclose(loader);
    }
}

LLWindowVulkanFunction getResolver(void*, void* loader) noexcept
{
    if (!loader)
    {
        return nullptr;
    }

    void* symbol = dlsym(loader, "vkGetInstanceProcAddr");
    static_assert(sizeof(symbol) == sizeof(LLWindowVulkanFunction));
    LLWindowVulkanFunction resolver = nullptr;
    std::memcpy(&resolver, &symbol, sizeof(resolver));
    return resolver;
}

LLWindowMacOSXVulkanNativeCreateResult createNativeWindow(void*, const LLWindowMacOSXVulkanCreateInfo& info) noexcept
{
    LLWindowMacOSXVulkanNative native{};
    const LLWindowMacOSXVulkanStatus status =
        llwindow_macosx_vulkan_native_create(info.mBackingWidth, info.mBackingHeight, &native);
    if (status != LLWINDOWMACOSXVULKAN_STATUS_SUCCESS)
    {
        return LLWindowMacOSXVulkanNativeCreateError{ nativeFailureCode(status) };
    }
    return ownedNative(native);
}

bool refreshNativeWindow(void*, LLWindowMacOSXVulkanNativeWindow& native_window) noexcept
{
    LLWindowMacOSXVulkanNative native = bridgeNative(native_window);
    const LLWindowMacOSXVulkanStatus status = llwindow_macosx_vulkan_native_refresh(&native);
    if (status != LLWINDOWMACOSXVULKAN_STATUS_SUCCESS && status != LLWINDOWMACOSXVULKAN_STATUS_DRAWABLE_UNAVAILABLE)
    {
        return false;
    }
    native_window = ownedNative(native);
    return true;
}

bool resizeNativeWindowForDiagnostic(void*,
                                     LLWindowMacOSXVulkanNativeWindow& native_window,
                                     U32                               backing_width,
                                     U32                               backing_height) noexcept
{
    LLWindowMacOSXVulkanNative native = bridgeNative(native_window);
    const LLWindowMacOSXVulkanStatus status =
        llwindow_macosx_vulkan_native_resize_for_diagnostic(backing_width, backing_height, &native);
    if (status != LLWINDOWMACOSXVULKAN_STATUS_SUCCESS && status != LLWINDOWMACOSXVULKAN_STATUS_DRAWABLE_UNAVAILABLE)
    {
        return false;
    }
    native_window = ownedNative(native);
    return true;
}

void destroyNativeWindow(void*, LLWindowMacOSXVulkanNativeWindow& native_window) noexcept
{
    LLWindowMacOSXVulkanNative native = bridgeNative(native_window);
    if (llwindow_macosx_vulkan_native_destroy(&native) != LLWINDOWMACOSXVULKAN_STATUS_SUCCESS)
    {
        std::terminate();
    }
    native_window = {};
}

LLRenderVulkan::VulkanSurfaceCreateOutcome createMetalSurface(
    void*,
    LLWindowVulkanFunction       resolver,
    void*                        metal_layer,
    VkInstance                   instance,
    const VkAllocationCallbacks* allocator,
    VkSurfaceKHR*                surface) noexcept
{
    if (!resolver || !metal_layer || instance == VK_NULL_HANDLE || !surface)
    {
        return LLRenderVulkan::VulkanSurfacePlatformFailure{};
    }

    const auto get_instance_proc_addr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(resolver);
    const auto create_surface = reinterpret_cast<PFN_vkCreateMetalSurfaceEXT>(
        get_instance_proc_addr(instance, "vkCreateMetalSurfaceEXT"));
    if (!create_surface)
    {
        return LLRenderVulkan::VulkanSurfacePlatformFailure{};
    }

    VkMetalSurfaceCreateInfoEXT create_info{};
    create_info.sType  = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
    create_info.pLayer = static_cast<const CAMetalLayer*>(metal_layer);
    return create_surface(instance, &create_info, allocator, surface);
}

bool isInstanceWindowGenerationCurrent(void* userdata, std::uint64_t native_window_generation) noexcept
{
    const auto* owner = static_cast<const LLWindowMacOSXVulkan*>(userdata);
    return owner && owner->isGenerationCurrent(native_window_generation);
}

struct SurfaceAcquireContext
{
    const LLWindowMacOSXVulkan*                       mOwner              = nullptr;
    const LLRenderVulkan::VulkanInstanceGeneration*   mInstanceGeneration = nullptr;
    const LLWindowMacOSXVulkanOperations*             mOperations         = nullptr;
    LLWindowVulkanFunction                            mResolver           = nullptr;
    void*                                             mMetalLayer         = nullptr;
};

bool isSurfaceInstanceOwnerCurrent(void* userdata, const LLRenderVulkan::VulkanInstanceGeneration& generation) noexcept
{
    const auto* context = static_cast<const SurfaceAcquireContext*>(userdata);
    return context && context->mOwner && context->mInstanceGeneration == &generation &&
           context->mOwner->instanceGeneration() == &generation;
}

bool isSurfaceWindowGenerationCurrent(void* userdata, std::uint64_t native_window_generation) noexcept
{
    const auto* context = static_cast<const SurfaceAcquireContext*>(userdata);
    return context && context->mOwner && context->mOwner->isGenerationCurrent(native_window_generation);
}

LLRenderVulkan::VulkanSurfaceCreateOutcome createSurfaceGeneration(
    void*                        userdata,
    VkInstance                   instance,
    const VkAllocationCallbacks* allocator,
    VkSurfaceKHR*                surface) noexcept
{
    const auto* context = static_cast<const SurfaceAcquireContext*>(userdata);
    if (!context || !context->mOperations || !context->mOperations->mCreateSurface || !context->mResolver ||
        !context->mMetalLayer)
    {
        return LLRenderVulkan::VulkanSurfacePlatformFailure{};
    }
    return context->mOperations->mCreateSurface(context->mOperations->mUserdata, context->mResolver, context->mMetalLayer,
                                                instance, allocator, surface);
}

class Rollback
{
public:
    explicit Rollback(const LLWindowMacOSXVulkanOperations& operations) noexcept : mOperations(operations) {}

    ~Rollback() noexcept
    {
        if (mHasNativeWindow)
        {
            mOperations.mDestroyNativeWindow(mOperations.mUserdata, mNativeWindow);
        }
        if (mLoader)
        {
            mOperations.mCloseLoader(mOperations.mUserdata, mLoader);
        }
    }

    void loader(void* loader) noexcept { mLoader = loader; }
    void nativeWindow(const LLWindowMacOSXVulkanNativeWindow& native_window) noexcept
    {
        mNativeWindow    = native_window;
        mHasNativeWindow = true;
    }
    void release() noexcept
    {
        mHasNativeWindow = false;
        mLoader          = nullptr;
    }

private:
    LLWindowMacOSXVulkanOperations   mOperations;
    void*                            mLoader = nullptr;
    LLWindowMacOSXVulkanNativeWindow mNativeWindow;
    bool                             mHasNativeWindow = false;
};

} // namespace

struct LLWindowMacOSXVulkanFactory
{
    static LLWindowMacOSXVulkan create(const LLWindowMacOSXVulkanOperations& operations,
                                       void*                                  loader,
                                       LLWindowMacOSXVulkanNativeWindow&&     native_window,
                                       LLWindowVulkanRequirements&&           requirements) noexcept
    {
        return LLWindowMacOSXVulkan(operations, loader, std::move(native_window), std::move(requirements));
    }
};

LLWindowMacOSXVulkan::LLWindowMacOSXVulkan(const LLWindowMacOSXVulkanOperations& operations,
                                           void*                                  loader,
                                           LLWindowMacOSXVulkanNativeWindow&&     native_window,
                                           LLWindowVulkanRequirements&&           requirements) noexcept :
    mOperations(operations),
    mLoader(loader),
    mNativeWindow(std::move(native_window)),
    mRequirements(std::move(requirements))
{
}

LLWindowMacOSXVulkan::~LLWindowMacOSXVulkan() noexcept
{
    if (!reset())
    {
        std::terminate();
    }
}

LLWindowMacOSXVulkan::LLWindowMacOSXVulkan(LLWindowMacOSXVulkan&& other) noexcept :
    mOperations(other.mOperations),
    mLoader(std::exchange(other.mLoader, nullptr)),
    mNativeWindow(std::exchange(other.mNativeWindow, {})),
    mRequirements(std::move(other.mRequirements)),
    mInstanceGeneration(std::move(other.mInstanceGeneration))
{
    other.mRequirements.reset();
}

LLWindowMacOSXVulkan& LLWindowMacOSXVulkan::operator=(LLWindowMacOSXVulkan&& other) noexcept
{
    if (this != &other)
    {
        if (!reset())
        {
            std::terminate();
        }
        mOperations         = other.mOperations;
        mLoader             = std::exchange(other.mLoader, nullptr);
        mNativeWindow       = std::exchange(other.mNativeWindow, {});
        mRequirements       = std::move(other.mRequirements);
        mInstanceGeneration = std::move(other.mInstanceGeneration);
        other.mRequirements.reset();
    }
    return *this;
}

bool LLWindowMacOSXVulkan::refreshNativeGeometry() noexcept
{
    LLWindowMacOSXVulkanNativeWindow refreshed;
    if (observeNativeGeometry(refreshed) != NativeGeometryObservation::Ready)
    {
        return false;
    }

    mNativeWindow.mBackingScale   = refreshed.mBackingScale;
    mNativeWindow.mDrawableWidth  = refreshed.mDrawableWidth;
    mNativeWindow.mDrawableHeight = refreshed.mDrawableHeight;
    return true;
}

LLWindowMacOSXVulkan::NativeGeometryObservation LLWindowMacOSXVulkan::observeNativeGeometry(
    LLWindowMacOSXVulkanNativeWindow& refreshed) noexcept
{
    if (!validIdentity(mNativeWindow) || !mOperations.mIsMainThread ||
        !mOperations.mIsMainThread(mOperations.mUserdata) || !mOperations.mRefreshNativeWindow)
    {
        return NativeGeometryObservation::Stale;
    }

    refreshed = mNativeWindow;
    if (!mOperations.mRefreshNativeWindow(mOperations.mUserdata, refreshed) || !sameIdentity(mNativeWindow, refreshed) ||
        !std::isfinite(refreshed.mBackingScale) || refreshed.mBackingScale <= 0.0)
    {
        return NativeGeometryObservation::Stale;
    }
    if (refreshed.mDrawableWidth == 0 || refreshed.mDrawableHeight == 0)
    {
        return NativeGeometryObservation::Suspended;
    }
    return NativeGeometryObservation::Ready;
}

bool LLWindowMacOSXVulkan::resizeNativeDrawableForDiagnostic(U32 backing_width, U32 backing_height) noexcept
{
    if (backing_width == 0 || backing_height == 0 || !validIdentity(mNativeWindow) || !mOperations.mIsMainThread ||
        !mOperations.mIsMainThread(mOperations.mUserdata) || !mOperations.mResizeNativeWindowForDiagnostic)
    {
        return false;
    }

    LLWindowMacOSXVulkanNativeWindow resized = mNativeWindow;
    if (!mOperations.mResizeNativeWindowForDiagnostic(
            mOperations.mUserdata, resized, backing_width, backing_height) ||
        !sameIdentity(mNativeWindow, resized) || !validGeometry(resized) || resized.mDrawableWidth != backing_width ||
        resized.mDrawableHeight != backing_height)
    {
        return false;
    }

    mNativeWindow.mBackingScale   = resized.mBackingScale;
    mNativeWindow.mDrawableWidth  = resized.mDrawableWidth;
    mNativeWindow.mDrawableHeight = resized.mDrawableHeight;
    return true;
}

bool LLWindowMacOSXVulkan::isGenerationCurrent(U64 native_window_generation) const noexcept
{
    return native_window_generation != 0 && mLoader && validIdentity(mNativeWindow) && mRequirements &&
           mRequirements->nativeWindowGeneration() == native_window_generation;
}

std::optional<LLRenderVulkan::VulkanInstanceAcquireError> LLWindowMacOSXVulkan::acquireInstanceGeneration(
    LLRenderVulkan::VulkanInstanceValidationMode  validation_mode,
    LLRenderVulkan::VulkanInstancePortabilityMode portability_mode) noexcept
{
    if (mInstanceGeneration)
    {
        LLRenderVulkan::VulkanInstanceAcquireError error;
        error.mCode = LLRenderVulkan::VulkanInstanceAcquireCode::InstanceAlreadyOwned;
        return error;
    }
    if (!mRequirements || !mLoader || !validIdentity(mNativeWindow) || !refreshNativeGeometry())
    {
        LLRenderVulkan::VulkanInstanceAcquireError error;
        error.mCode = LLRenderVulkan::VulkanInstanceAcquireCode::StaleWindowGeneration;
        return error;
    }

    std::vector<std::string> required_extensions;
    try
    {
        required_extensions = mRequirements->requiredInstanceExtensions();
        if (std::find(required_extensions.begin(), required_extensions.end(), SURFACE_CAPABILITIES_2_EXTENSION) ==
            required_extensions.end())
        {
            required_extensions.emplace_back(SURFACE_CAPABILITIES_2_EXTENSION);
        }
        if (std::find(required_extensions.begin(), required_extensions.end(), SURFACE_MAINTENANCE_EXTENSION) == required_extensions.end())
        {
            required_extensions.emplace_back(SURFACE_MAINTENANCE_EXTENSION);
        }
    }
    catch (const std::bad_alloc&)
    {
        LLRenderVulkan::VulkanInstanceAcquireError error;
        error.mCode = LLRenderVulkan::VulkanInstanceAcquireCode::AllocationFailure;
        return error;
    }

    LLRenderVulkan::VulkanInstanceRequest request;
    request.mGetInstanceProcAddr      = reinterpret_cast<PFN_vkGetInstanceProcAddr>(mRequirements->resolver());
    request.mRequiredWindowExtensions = std::span<const std::string>(required_extensions);
    request.mNativeWindowGeneration   = mRequirements->nativeWindowGeneration();
    request.mGenerationCheck          = { this, isInstanceWindowGenerationCurrent };
    request.mValidationMode           = validation_mode;
    request.mPortabilityMode          = portability_mode;

    LLRenderVulkan::VulkanInstanceAcquireResult result = LLRenderVulkan::acquireVulkanInstanceGeneration(request);
    if (const auto* error = std::get_if<LLRenderVulkan::VulkanInstanceAcquireError>(&result))
    {
        return *error;
    }

    auto* generation =
        new (std::nothrow) LLRenderVulkan::VulkanInstanceGeneration(std::move(std::get<LLRenderVulkan::VulkanInstanceGeneration>(result)));
    if (!generation)
    {
        LLRenderVulkan::VulkanInstanceAcquireError error;
        error.mCode = LLRenderVulkan::VulkanInstanceAcquireCode::AllocationFailure;
        return error;
    }
    mInstanceGeneration.reset(generation);
    return std::nullopt;
}

std::optional<LLRenderVulkan::VulkanSurfaceAcquireError> LLWindowMacOSXVulkan::acquireSurfaceGeneration() noexcept
{
    return acquireSurfaceGeneration(nullptr);
}

LLRenderVulkan::VulkanSurfaceAcquireResult LLWindowMacOSXVulkan::acquireSurfaceGeneration(
    LLRenderVulkan::VulkanInstanceDetail::AllocationCheckpoint allocation_checkpoint) noexcept
{
    using namespace LLRenderVulkan;

    if (!mInstanceGeneration)
    {
        return VulkanSurfaceAcquireError{ VulkanSurfaceAcquireCode::InstanceNotLive, std::nullopt, std::nullopt };
    }
    if (!mRequirements || !mLoader || !validIdentity(mNativeWindow) || !refreshNativeGeometry())
    {
        return VulkanSurfaceAcquireError{ VulkanSurfaceAcquireCode::StaleWindowGeneration, std::nullopt, std::nullopt };
    }

    SurfaceAcquireContext context{ this, mInstanceGeneration.get(), &mOperations, mRequirements->resolver(), mNativeWindow.mMetalLayer };

    VulkanSurfaceRequest request;
    request.mNativeWindowGeneration = mRequirements->nativeWindowGeneration();
    request.mInstanceOwnerCheck     = { &context, isSurfaceInstanceOwnerCurrent };
    request.mWindowGenerationCheck  = { &context, isSurfaceWindowGenerationCurrent };
    request.mCreateOperation        = { &context, createSurfaceGeneration };

    return VulkanInstanceDetail::acquireSurface(*mInstanceGeneration, request, allocation_checkpoint);
}

LLRenderVulkan::VulkanPresentationDeviceAcquireResult LLWindowMacOSXVulkan::acquirePresentationDeviceGeneration() noexcept
{
    using namespace LLRenderVulkan;

    if (!mInstanceGeneration)
    {
        return VulkanPresentationDeviceAcquireError{ VulkanPresentationDeviceAcquireCode::InstanceNotLive, std::nullopt };
    }
    if (!mRequirements || !mLoader || !validIdentity(mNativeWindow) || !refreshNativeGeometry())
    {
        return VulkanPresentationDeviceAcquireError{ VulkanPresentationDeviceAcquireCode::StaleWindowGeneration, std::nullopt };
    }

    SurfaceAcquireContext context{ this, mInstanceGeneration.get(), &mOperations, mRequirements->resolver(), mNativeWindow.mMetalLayer };
    VulkanPresentationDeviceRequest request;
    request.mNativeWindowGeneration = mRequirements->nativeWindowGeneration();
    request.mInstanceOwnerCheck     = { &context, isSurfaceInstanceOwnerCurrent };
    request.mWindowGenerationCheck  = { &context, isSurfaceWindowGenerationCurrent };
    return mInstanceGeneration->acquirePresentationDeviceGeneration(request);
}

LLRenderVulkan::VulkanLogicalDeviceAcquireResult LLWindowMacOSXVulkan::acquireLogicalDeviceGeneration() noexcept
{
    using namespace LLRenderVulkan;

    if (!mInstanceGeneration)
    {
        return VulkanLogicalDeviceAcquireError{ VulkanLogicalDeviceAcquireCode::InstanceNotLive, std::nullopt };
    }
    if (!mRequirements || !mLoader || !validIdentity(mNativeWindow) || !refreshNativeGeometry())
    {
        return VulkanLogicalDeviceAcquireError{ VulkanLogicalDeviceAcquireCode::StaleWindowGeneration, std::nullopt };
    }

    SurfaceAcquireContext context{ this, mInstanceGeneration.get(), &mOperations, mRequirements->resolver(), mNativeWindow.mMetalLayer };
    VulkanLogicalDeviceRequest request;
    request.mNativeWindowGeneration = mRequirements->nativeWindowGeneration();
    request.mInstanceOwnerCheck     = { &context, isSurfaceInstanceOwnerCurrent };
    request.mWindowGenerationCheck  = { &context, isSurfaceWindowGenerationCurrent };
    return mInstanceGeneration->acquireLogicalDeviceGeneration(request);
}

LLRenderVulkan::VulkanSwapchainConfigurationAcquireResult LLWindowMacOSXVulkan::acquireSwapchainConfigurationGeneration() noexcept
{
    using namespace LLRenderVulkan;

    if (!mInstanceGeneration)
    {
        return VulkanSwapchainConfigurationAcquireError{ VulkanSwapchainConfigurationAcquireCode::InstanceNotLive, std::nullopt };
    }
    if (!mRequirements || !mLoader || !validIdentity(mNativeWindow) || !refreshNativeGeometry())
    {
        return VulkanSwapchainConfigurationAcquireError{ VulkanSwapchainConfigurationAcquireCode::StaleWindowGeneration, std::nullopt };
    }

    SurfaceAcquireContext context{ this, mInstanceGeneration.get(), &mOperations, mRequirements->resolver(), mNativeWindow.mMetalLayer };
    VulkanSwapchainConfigurationRequest request;
    request.mNativeWindowGeneration = mRequirements->nativeWindowGeneration();
    request.mDrawableExtent         = { mNativeWindow.mDrawableWidth, mNativeWindow.mDrawableHeight };
    request.mInstanceOwnerCheck     = { &context, isSurfaceInstanceOwnerCurrent };
    request.mWindowGenerationCheck  = { &context, isSurfaceWindowGenerationCurrent };
    return mInstanceGeneration->acquireSwapchainConfigurationGeneration(request);
}

LLRenderVulkan::VulkanSwapchainAcquireResult LLWindowMacOSXVulkan::acquireSwapchainGeneration() noexcept
{
    using namespace LLRenderVulkan;

    if (!mInstanceGeneration)
    {
        return VulkanSwapchainAcquireError{ VulkanSwapchainAcquireCode::InstanceNotLive, std::nullopt };
    }
    if (!mRequirements || !mLoader || !validIdentity(mNativeWindow) || !refreshNativeGeometry())
    {
        return VulkanSwapchainAcquireError{ VulkanSwapchainAcquireCode::StaleWindowGeneration, std::nullopt };
    }

    SurfaceAcquireContext  context{ this, mInstanceGeneration.get(), &mOperations, mRequirements->resolver(), mNativeWindow.mMetalLayer };
    VulkanSwapchainRequest request;
    request.mNativeWindowGeneration = mRequirements->nativeWindowGeneration();
    request.mDrawableExtent         = { mNativeWindow.mDrawableWidth, mNativeWindow.mDrawableHeight };
    request.mInstanceOwnerCheck     = { &context, isSurfaceInstanceOwnerCurrent };
    request.mWindowGenerationCheck  = { &context, isSurfaceWindowGenerationCurrent };
    return mInstanceGeneration->acquireSwapchainGeneration(request);
}

LLRenderVulkan::VulkanSwapchainImagesAcquireResult LLWindowMacOSXVulkan::acquireSwapchainImagesGeneration() noexcept
{
    using namespace LLRenderVulkan;

    if (!mInstanceGeneration)
    {
        return VulkanSwapchainImagesAcquireError{ VulkanSwapchainImagesAcquireCode::InstanceNotLive, std::nullopt };
    }
    if (!mRequirements || !mLoader || !validIdentity(mNativeWindow) || !refreshNativeGeometry())
    {
        return VulkanSwapchainImagesAcquireError{ VulkanSwapchainImagesAcquireCode::StaleWindowGeneration, std::nullopt };
    }

    SurfaceAcquireContext context{ this, mInstanceGeneration.get(), &mOperations, mRequirements->resolver(), mNativeWindow.mMetalLayer };
    VulkanSwapchainImagesRequest request;
    request.mNativeWindowGeneration = mRequirements->nativeWindowGeneration();
    request.mDrawableExtent         = { mNativeWindow.mDrawableWidth, mNativeWindow.mDrawableHeight };
    request.mInstanceOwnerCheck     = { &context, isSurfaceInstanceOwnerCurrent };
    request.mWindowGenerationCheck  = { &context, isSurfaceWindowGenerationCurrent };
    return mInstanceGeneration->acquireSwapchainImagesGeneration(request);
}

LLRenderVulkan::VulkanSwapchainFrameSlotAcquireResult LLWindowMacOSXVulkan::acquireSwapchainFrameSlotGeneration() noexcept
{
    using namespace LLRenderVulkan;

    if (!mInstanceGeneration)
    {
        return VulkanSwapchainFrameSlotAcquireError{ VulkanSwapchainFrameSlotAcquireCode::InstanceNotLive, std::nullopt };
    }
    if (!mRequirements || !mLoader || !validIdentity(mNativeWindow) || !refreshNativeGeometry())
    {
        return VulkanSwapchainFrameSlotAcquireError{ VulkanSwapchainFrameSlotAcquireCode::StaleWindowGeneration, std::nullopt };
    }

    SurfaceAcquireContext context{ this, mInstanceGeneration.get(), &mOperations, mRequirements->resolver(), mNativeWindow.mMetalLayer };
    VulkanSwapchainFrameSlotRequest request;
    request.mNativeWindowGeneration = mRequirements->nativeWindowGeneration();
    request.mDrawableExtent         = { mNativeWindow.mDrawableWidth, mNativeWindow.mDrawableHeight };
    request.mInstanceOwnerCheck     = { &context, isSurfaceInstanceOwnerCurrent };
    request.mWindowGenerationCheck  = { &context, isSurfaceWindowGenerationCurrent };
    return mInstanceGeneration->acquireSwapchainFrameSlotGeneration(request);
}

LLRenderVulkan::VulkanSwapchainChainRebuildResult LLWindowMacOSXVulkan::rebuildSwapchainChain() noexcept
{
    using namespace LLRenderVulkan;

    if (!mInstanceGeneration)
    {
        return VulkanSwapchainChainRebuildError{ VulkanSwapchainChainRebuildCode::InstanceNotLive,
                                                 VulkanSwapchainChainRebuildPhase::Preflight,
                                                 {},
                                                 std::nullopt };
    }
    if (!mRequirements || !mLoader || !validIdentity(mNativeWindow))
    {
        return VulkanSwapchainChainRebuildError{ VulkanSwapchainChainRebuildCode::StaleWindowGeneration,
                                                 VulkanSwapchainChainRebuildPhase::Preflight,
                                                 {},
                                                 std::nullopt };
    }

    LLWindowMacOSXVulkanNativeWindow refreshed;
    const NativeGeometryObservation observation = observeNativeGeometry(refreshed);
    if (observation == NativeGeometryObservation::Stale)
    {
        return VulkanSwapchainChainRebuildError{ VulkanSwapchainChainRebuildCode::StaleWindowGeneration,
                                                 VulkanSwapchainChainRebuildPhase::Preflight,
                                                 {},
                                                 std::nullopt };
    }

    mNativeWindow.mBackingScale   = refreshed.mBackingScale;
    mNativeWindow.mDrawableWidth  = refreshed.mDrawableWidth;
    mNativeWindow.mDrawableHeight = refreshed.mDrawableHeight;

    SurfaceAcquireContext context{ this, mInstanceGeneration.get(), &mOperations, mRequirements->resolver(), mNativeWindow.mMetalLayer };
    VulkanSwapchainChainRebuildRequest request;
    request.mNativeWindowGeneration = mRequirements->nativeWindowGeneration();
    request.mDrawableExtent         = VkExtent2D{ refreshed.mDrawableWidth, refreshed.mDrawableHeight };
    request.mInstanceOwnerCheck     = { &context, isSurfaceInstanceOwnerCurrent };
    request.mWindowGenerationCheck  = { &context, isSurfaceWindowGenerationCurrent };
    return mInstanceGeneration->rebuildSwapchainChain(request);
}

LLRenderVulkan::VulkanSwapchainFrameSlotParentOperationResult LLWindowMacOSXVulkan::roundTripEmptySwapchainFrameSlot() noexcept
{
    const FrameSlotResult result = operateSwapchainFrameSlot(FrameSlotOperation::ExecuteEmptySubmission);
    if (const auto* error = std::get_if<LLRenderVulkan::VulkanSwapchainFrameSlotParentOperationError>(&result))
    {
        return *error;
    }
    return std::get<LLRenderVulkan::VulkanSwapchainFrameSlotDisposition>(result);
}

LLRenderVulkan::VulkanSwapchainFrameSlotParentOperationResult LLWindowMacOSXVulkan::retryEmptySwapchainFrameSlotCompletion() noexcept
{
    const FrameSlotResult result = operateSwapchainFrameSlot(FrameSlotOperation::RetryEmptySubmissionCompletion);
    if (const auto* error = std::get_if<LLRenderVulkan::VulkanSwapchainFrameSlotParentOperationError>(&result))
    {
        return *error;
    }
    return std::get<LLRenderVulkan::VulkanSwapchainFrameSlotDisposition>(result);
}

LLRenderVulkan::VulkanSwapchainFrameSlotParentPresentationResult LLWindowMacOSXVulkan::acquireToPresentSwapchainFrameSlot() noexcept
{
    const FrameSlotResult result = operateSwapchainFrameSlot(FrameSlotOperation::ExecuteAcquireToPresent);
    if (const auto* error = std::get_if<LLRenderVulkan::VulkanSwapchainFrameSlotParentOperationError>(&result))
    {
        return *error;
    }
    return std::get<LLRenderVulkan::VulkanSwapchainFrameSlotPresentationSuccess>(result);
}

LLRenderVulkan::VulkanSwapchainFrameSlotParentPresentationResult LLWindowMacOSXVulkan::acquireClearToPresentSwapchainFrameSlot(
    const LLRenderVulkan::VulkanSwapchainFrameClearColor& clear_color) noexcept
{
    using namespace LLRenderVulkan;

    if (!mInstanceGeneration)
    {
        return VulkanSwapchainFrameSlotParentOperationError{ VulkanSwapchainFrameSlotParentOperationCode::InstanceNotLive, std::nullopt };
    }
    if (!mRequirements || !mLoader || !validIdentity(mNativeWindow) || !mOperations.mIsMainThread ||
        !mOperations.mIsMainThread(mOperations.mUserdata) || !mOperations.mRefreshNativeWindow)
    {
        return VulkanSwapchainFrameSlotParentOperationError{ VulkanSwapchainFrameSlotParentOperationCode::StaleWindowGeneration,
                                                             std::nullopt };
    }

    LLWindowMacOSXVulkanNativeWindow refreshed = mNativeWindow;
    if (!mOperations.mRefreshNativeWindow(mOperations.mUserdata, refreshed) || !sameIdentity(mNativeWindow, refreshed))
    {
        return VulkanSwapchainFrameSlotParentOperationError{ VulkanSwapchainFrameSlotParentOperationCode::StaleWindowGeneration,
                                                             std::nullopt };
    }
    if (!validGeometry(refreshed))
    {
        return VulkanSwapchainFrameSlotParentOperationError{ VulkanSwapchainFrameSlotParentOperationCode::InvalidDrawableExtent,
                                                             std::nullopt };
    }

    mNativeWindow.mBackingScale   = refreshed.mBackingScale;
    mNativeWindow.mDrawableWidth  = refreshed.mDrawableWidth;
    mNativeWindow.mDrawableHeight = refreshed.mDrawableHeight;

    SurfaceAcquireContext context{ this, mInstanceGeneration.get(), &mOperations, mRequirements->resolver(), mNativeWindow.mMetalLayer };
    VulkanSwapchainFrameSlotOperationRequest request;
    request.mNativeWindowGeneration = mRequirements->nativeWindowGeneration();
    request.mDrawableExtent         = { refreshed.mDrawableWidth, refreshed.mDrawableHeight };
    request.mInstanceOwnerCheck     = { &context, isSurfaceInstanceOwnerCurrent };
    request.mWindowGenerationCheck  = { &context, isSurfaceWindowGenerationCurrent };
    return mInstanceGeneration->acquireClearToPresentSwapchainFrameSlot(request, clear_color);
}

LLRenderVulkan::VulkanSwapchainFrameSlotParentPresentationResult LLWindowMacOSXVulkan::retrySwapchainFrameSlotPresentation() noexcept
{
    const FrameSlotResult result = operateSwapchainFrameSlot(FrameSlotOperation::RetryPresentation);
    if (const auto* error = std::get_if<LLRenderVulkan::VulkanSwapchainFrameSlotParentOperationError>(&result))
    {
        return *error;
    }
    return std::get<LLRenderVulkan::VulkanSwapchainFrameSlotPresentationSuccess>(result);
}

LLRenderVulkan::VulkanSwapchainFrameSlotParentPresentationResult LLWindowMacOSXVulkan::
    retrySwapchainFrameSlotPresentationCompletion() noexcept
{
    const FrameSlotResult result = operateSwapchainFrameSlot(FrameSlotOperation::RetryPresentationCompletion);
    if (const auto* error = std::get_if<LLRenderVulkan::VulkanSwapchainFrameSlotParentOperationError>(&result))
    {
        return *error;
    }
    return std::get<LLRenderVulkan::VulkanSwapchainFrameSlotPresentationSuccess>(result);
}

LLRenderVulkan::VulkanSwapchainFrameSlotParentOperationResult LLWindowMacOSXVulkan::cancelSwapchainFrameSlotPresentation() noexcept
{
    const FrameSlotResult result = operateSwapchainFrameSlot(FrameSlotOperation::CancelAcquireToPresent);
    if (const auto* error = std::get_if<LLRenderVulkan::VulkanSwapchainFrameSlotParentOperationError>(&result))
    {
        return *error;
    }
    return std::get<LLRenderVulkan::VulkanSwapchainFrameSlotDisposition>(result);
}

LLRenderVulkan::VulkanSwapchainFrameSlotParentOperationResult LLWindowMacOSXVulkan::retrySwapchainFrameSlotCancellationCompletion() noexcept
{
    const FrameSlotResult result = operateSwapchainFrameSlot(FrameSlotOperation::RetryCancellationCompletion);
    if (const auto* error = std::get_if<LLRenderVulkan::VulkanSwapchainFrameSlotParentOperationError>(&result))
    {
        return *error;
    }
    return std::get<LLRenderVulkan::VulkanSwapchainFrameSlotDisposition>(result);
}

LLWindowMacOSXVulkan::FrameSlotResult LLWindowMacOSXVulkan::operateSwapchainFrameSlot(FrameSlotOperation operation) noexcept
{
    using namespace LLRenderVulkan;

    const bool starts_new_work =
        operation == FrameSlotOperation::ExecuteEmptySubmission || operation == FrameSlotOperation::ExecuteAcquireToPresent;

    if (!mInstanceGeneration)
    {
        return VulkanSwapchainFrameSlotParentOperationError{ VulkanSwapchainFrameSlotParentOperationCode::InstanceNotLive, std::nullopt };
    }
    if (!mRequirements || !mLoader || !validIdentity(mNativeWindow) || !mOperations.mIsMainThread ||
        !mOperations.mIsMainThread(mOperations.mUserdata) || (starts_new_work && !mOperations.mRefreshNativeWindow))
    {
        return VulkanSwapchainFrameSlotParentOperationError{ VulkanSwapchainFrameSlotParentOperationCode::StaleWindowGeneration,
                                                             std::nullopt };
    }

    VkExtent2D drawable_extent = mInstanceGeneration->swapchainDrawableExtent();
    if (starts_new_work)
    {
        LLWindowMacOSXVulkanNativeWindow refreshed = mNativeWindow;
        if (!mOperations.mRefreshNativeWindow(mOperations.mUserdata, refreshed) || !sameIdentity(mNativeWindow, refreshed))
        {
            return VulkanSwapchainFrameSlotParentOperationError{ VulkanSwapchainFrameSlotParentOperationCode::StaleWindowGeneration,
                                                                 std::nullopt };
        }
        if (!validGeometry(refreshed))
        {
            return VulkanSwapchainFrameSlotParentOperationError{ VulkanSwapchainFrameSlotParentOperationCode::InvalidDrawableExtent,
                                                                 std::nullopt };
        }

        mNativeWindow.mBackingScale   = refreshed.mBackingScale;
        mNativeWindow.mDrawableWidth  = refreshed.mDrawableWidth;
        mNativeWindow.mDrawableHeight = refreshed.mDrawableHeight;
        drawable_extent               = { refreshed.mDrawableWidth, refreshed.mDrawableHeight };
    }

    SurfaceAcquireContext context{ this, mInstanceGeneration.get(), &mOperations, mRequirements->resolver(), mNativeWindow.mMetalLayer };
    VulkanSwapchainFrameSlotOperationRequest request;
    request.mNativeWindowGeneration = mRequirements->nativeWindowGeneration();
    request.mDrawableExtent         = drawable_extent;
    request.mInstanceOwnerCheck     = { &context, isSurfaceInstanceOwnerCurrent };
    request.mWindowGenerationCheck  = { &context, isSurfaceWindowGenerationCurrent };
    if (operation == FrameSlotOperation::ExecuteAcquireToPresent || operation == FrameSlotOperation::RetryPresentation ||
        operation == FrameSlotOperation::RetryPresentationCompletion)
    {
        VulkanSwapchainFrameSlotParentPresentationResult result;
        switch (operation)
        {
            case FrameSlotOperation::ExecuteAcquireToPresent:
                result = mInstanceGeneration->acquireToPresentSwapchainFrameSlot(request);
                break;
            case FrameSlotOperation::RetryPresentation:
                result = mInstanceGeneration->retrySwapchainFrameSlotPresentation(request);
                break;
            case FrameSlotOperation::RetryPresentationCompletion:
                result = mInstanceGeneration->retrySwapchainFrameSlotPresentationCompletion(request);
                break;
            default:
                std::terminate();
        }
        if (const auto* error = std::get_if<VulkanSwapchainFrameSlotParentOperationError>(&result))
        {
            return *error;
        }
        return std::get<VulkanSwapchainFrameSlotPresentationSuccess>(result);
    }

    VulkanSwapchainFrameSlotParentOperationResult result;
    switch (operation)
    {
        case FrameSlotOperation::ExecuteEmptySubmission:
            result = mInstanceGeneration->roundTripEmptySwapchainFrameSlot(request);
            break;
        case FrameSlotOperation::RetryEmptySubmissionCompletion:
            result = mInstanceGeneration->retryEmptySwapchainFrameSlotCompletion(request);
            break;
        case FrameSlotOperation::CancelAcquireToPresent:
            result = mInstanceGeneration->cancelSwapchainFrameSlotPresentation(request);
            break;
        case FrameSlotOperation::RetryCancellationCompletion:
            result = mInstanceGeneration->retrySwapchainFrameSlotCancellationCompletion(request);
            break;
        default:
            std::terminate();
    }
    if (const auto* error = std::get_if<VulkanSwapchainFrameSlotParentOperationError>(&result))
    {
        return *error;
    }
    return std::get<VulkanSwapchainFrameSlotDisposition>(result);
}

bool LLWindowMacOSXVulkan::resetSwapchainFrameSlotGeneration() noexcept
{
    if (!mInstanceGeneration || !mInstanceGeneration->hasSwapchainFrameSlotGeneration())
    {
        return false;
    }
    return mInstanceGeneration->resetSwapchainFrameSlotGeneration();
}

bool LLWindowMacOSXVulkan::resetSwapchainImagesGeneration() noexcept
{
    if (!mInstanceGeneration || !mInstanceGeneration->hasSwapchainImagesGeneration())
    {
        return false;
    }
    return mInstanceGeneration->resetSwapchainImagesGeneration();
}

bool LLWindowMacOSXVulkan::resetSwapchainGeneration() noexcept
{
    if (!mInstanceGeneration || !mInstanceGeneration->hasSwapchainGeneration())
    {
        return false;
    }
    return mInstanceGeneration->resetSwapchainGeneration();
}

bool LLWindowMacOSXVulkan::resetSurfaceGeneration() noexcept
{
    if (!mInstanceGeneration || !mInstanceGeneration->hasSurfaceGeneration())
    {
        return false;
    }
    return mInstanceGeneration->resetSurfaceGeneration();
}

bool LLWindowMacOSXVulkan::reset() noexcept
{
    const bool owns_resources = mInstanceGeneration || mRequirements || mNativeWindow.mToken || mLoader;
    if (!owns_resources)
    {
        return true;
    }
    if (!mOperations.mIsMainThread || !mOperations.mIsMainThread(mOperations.mUserdata))
    {
        return false;
    }

    if (mInstanceGeneration && !mInstanceGeneration->reset())
    {
        return false;
    }
    mInstanceGeneration.reset();
    mRequirements.reset();
    if (mNativeWindow.mToken)
    {
        mOperations.mDestroyNativeWindow(mOperations.mUserdata, mNativeWindow);
        mNativeWindow = {};
    }
    if (mLoader)
    {
        mOperations.mCloseLoader(mOperations.mUserdata, mLoader);
        mLoader = nullptr;
    }
    return true;
}

namespace LLWindowMacOSXVulkanDetail
{

LLWindowMacOSXVulkanAcquireResult acquire(
    const LLWindowMacOSXVulkanCreateInfo&                   info,
    U64                                                      native_window_generation,
    const LLWindowMacOSXVulkanOperations&                   operations,
    LLWindowVulkanRequirementsDetail::AllocationCheckpoint allocation_checkpoint) noexcept
{
    if (!validOperations(operations))
    {
        return failure(LLWindowMacOSXVulkanAcquireCode::InvalidOperations);
    }
    if (info.mBackingWidth == 0 || info.mBackingHeight == 0)
    {
        return failure(LLWindowMacOSXVulkanAcquireCode::InvalidCreateInfo);
    }
    if (native_window_generation == 0)
    {
        return failure(LLWindowMacOSXVulkanAcquireCode::InvalidNativeWindowGeneration);
    }
    if (!operations.mIsMainThread(operations.mUserdata))
    {
        return failure(LLWindowMacOSXVulkanAcquireCode::MainThreadRequired);
    }

    Rollback rollback(operations);
    void* const loader = operations.mOpenLoader(operations.mUserdata, info.mLoaderPath.c_str());
    if (!loader)
    {
        return failure(LLWindowMacOSXVulkanAcquireCode::LoaderFailure);
    }
    rollback.loader(loader);

    const LLWindowVulkanFunction resolver = operations.mGetResolver(operations.mUserdata, loader);
    if (!resolver)
    {
        return failure(LLWindowMacOSXVulkanAcquireCode::ResolverFailure);
    }

    LLWindowMacOSXVulkanNativeCreateResult native_result = operations.mCreateNativeWindow(operations.mUserdata, info);
    if (const auto* native_error = std::get_if<LLWindowMacOSXVulkanNativeCreateError>(&native_result))
    {
        return failure(LLWindowMacOSXVulkanAcquireCode::NativeWindowFailure, *native_error);
    }

    LLWindowMacOSXVulkanNativeWindow native_window = std::get<LLWindowMacOSXVulkanNativeWindow>(native_result);
    rollback.nativeWindow(native_window);
    if (!validIdentity(native_window))
    {
        return failure(LLWindowMacOSXVulkanAcquireCode::NativeWindowIdentityFailure);
    }
    if (!validGeometry(native_window) || native_window.mDrawableWidth != info.mBackingWidth ||
        native_window.mDrawableHeight != info.mBackingHeight)
    {
        return failure(LLWindowMacOSXVulkanAcquireCode::NativeWindowGeometryFailure);
    }

    LLWindowVulkanRequirementsBuildResult requirements_result = LLWindowVulkanRequirementsDetail::build(
        resolver, REQUIRED_EXTENSIONS.size(), REQUIRED_EXTENSIONS.data(), native_window_generation, allocation_checkpoint);
    if (const auto* requirements_error = std::get_if<LLWindowVulkanRequirementsBuildError>(&requirements_result))
    {
        return failure(LLWindowMacOSXVulkanAcquireCode::RequirementsFailure, std::nullopt, *requirements_error);
    }

    rollback.release();
    return LLWindowMacOSXVulkanFactory::create(operations, loader, std::move(native_window),
                                               std::move(std::get<LLWindowVulkanRequirements>(requirements_result)));
}

LLRenderVulkan::VulkanSurfaceAcquireResult acquireSurfaceGeneration(
    LLWindowMacOSXVulkan&                                      owner,
    LLRenderVulkan::VulkanInstanceDetail::AllocationCheckpoint allocation_checkpoint) noexcept
{
    return owner.acquireSurfaceGeneration(allocation_checkpoint);
}

} // namespace LLWindowMacOSXVulkanDetail

const LLWindowMacOSXVulkanOperations& defaultLLWindowMacOSXVulkanOperations() noexcept
{
    static const LLWindowMacOSXVulkanOperations operations{ nullptr,
                                                            isMainThread,
                                                            openLoader,
                                                            closeLoader,
                                                            getResolver,
                                                            createNativeWindow,
                                                            refreshNativeWindow,
                                                            destroyNativeWindow,
                                                            createMetalSurface,
                                                            resizeNativeWindowForDiagnostic };
    return operations;
}

LLWindowMacOSXVulkanAcquireResult acquireLLWindowMacOSXVulkan(
    const LLWindowMacOSXVulkanCreateInfo& info,
    U64                                    native_window_generation,
    const LLWindowMacOSXVulkanOperations& operations) noexcept
{
    return LLWindowMacOSXVulkanDetail::acquire(info, native_window_generation, operations, nullptr);
}
