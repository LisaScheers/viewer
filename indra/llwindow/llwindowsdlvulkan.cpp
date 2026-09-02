/**
 * @file llwindowsdlvulkan.cpp
 * @brief SDL Vulkan window and loader lifetime ownership.
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

#include "SDL3/SDL_vulkan.h"

#include <algorithm>
#include <exception>
#include <new>
#include <utility>

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

bool loadLibrary(void*) noexcept
{
    SDL_assert(SDL_IsMainThread());
    return SDL_Vulkan_LoadLibrary(nullptr);
}

void unloadLibrary(void*) noexcept
{
    SDL_assert(SDL_IsMainThread());
    SDL_Vulkan_UnloadLibrary();
}

SDL_Window* createWindow(void*, const LLWindowSDLVulkanCreateInfo& info) noexcept
{
    SDL_assert(SDL_IsMainThread());

    const SDL_PropertiesID properties = SDL_CreateProperties();
    if (!properties)
    {
        return nullptr;
    }

    const bool configured = SDL_SetStringProperty(properties, SDL_PROP_WINDOW_CREATE_TITLE_STRING, info.mTitle.c_str()) &&
                            SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_X_NUMBER, info.mX) &&
                            SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_Y_NUMBER, info.mY) &&
                            SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, info.mWidth) &&
                            SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, info.mHeight) &&
                            SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, info.mResizable) &&
                            SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN, info.mFullscreen) &&
                            SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_HIDDEN_BOOLEAN, info.mHidden) &&
                            SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_HIGH_PIXEL_DENSITY_BOOLEAN, info.mHighPixelDensity) &&
                            SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_VULKAN_BOOLEAN, true);

    SDL_Window* window = configured ? SDL_CreateWindowWithProperties(properties) : nullptr;
    SDL_DestroyProperties(properties);
    return window;
}

void destroyWindow(void*, SDL_Window* window) noexcept
{
    SDL_assert(SDL_IsMainThread());
    SDL_DestroyWindow(window);
}

SDL_WindowFlags getWindowFlags(void*, SDL_Window* window) noexcept
{
    SDL_assert(SDL_IsMainThread());
    return SDL_GetWindowFlags(window);
}

bool getWindowSizeInPixels(void*, SDL_Window* window, int* width, int* height) noexcept
{
    SDL_assert(SDL_IsMainThread());
    return SDL_GetWindowSizeInPixels(window, width, height);
}

LLWindowVulkanFunction getResolver(void*) noexcept
{
    SDL_assert(SDL_IsMainThread());
    return SDL_Vulkan_GetVkGetInstanceProcAddr();
}

const char* const* getInstanceExtensions(void*, std::size_t* count) noexcept
{
    SDL_assert(SDL_IsMainThread());
    Uint32             sdl_count  = 0;
    const char* const* extensions = SDL_Vulkan_GetInstanceExtensions(&sdl_count);
    *count                        = sdl_count;
    return extensions;
}

bool createSurface(void*, SDL_Window* window, VkInstance instance, const VkAllocationCallbacks* allocator, VkSurfaceKHR* surface) noexcept
{
    SDL_assert(SDL_IsMainThread());
    return SDL_Vulkan_CreateSurface(window, instance, allocator, surface);
}

bool validOperations(const LLWindowSDLVulkanOperations& operations) noexcept
{
    return operations.mLoadLibrary && operations.mUnloadLibrary && operations.mCreateWindow && operations.mDestroyWindow &&
           operations.mGetWindowFlags && operations.mGetWindowSizeInPixels && operations.mGetResolver &&
           operations.mGetInstanceExtensions && operations.mCreateSurface;
}

bool isInstanceWindowGenerationCurrent(void* userdata, std::uint64_t native_window_generation) noexcept
{
    const auto* window = static_cast<const LLWindowSDLVulkan*>(userdata);
    return window && window->isGenerationCurrent(native_window_generation);
}

struct SurfaceAcquireContext
{
    const LLWindowSDLVulkan*                        mOwner              = nullptr;
    const LLRenderVulkan::VulkanInstanceGeneration* mInstanceGeneration = nullptr;
    const LLWindowSDLVulkanOperations*              mOperations         = nullptr;
    SDL_Window*                                     mWindow             = nullptr;
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

LLRenderVulkan::VulkanSurfaceCreateOutcome createSurfaceGeneration(void*                        userdata,
                                                                   VkInstance                   instance,
                                                                   const VkAllocationCallbacks* allocator,
                                                                   VkSurfaceKHR*                surface) noexcept
{
    const auto* context = static_cast<const SurfaceAcquireContext*>(userdata);
    if (!context || !context->mOperations || !context->mOperations->mCreateSurface || !context->mWindow)
    {
        return LLRenderVulkan::VulkanSurfacePlatformFailure{};
    }

    if (!context->mOperations->mCreateSurface(context->mOperations->mUserdata, context->mWindow, instance, allocator, surface))
    {
        return LLRenderVulkan::VulkanSurfacePlatformFailure{};
    }
    return VkResult{ VK_SUCCESS };
}

LLWindowSDLVulkanAcquireError failure(LLWindowSDLVulkanAcquireCode code) noexcept
{
    return { code, std::nullopt };
}

class Rollback
{
public:
    explicit Rollback(const LLWindowSDLVulkanOperations& operations) noexcept : mOperations(operations) {}

    ~Rollback() noexcept
    {
        if (mWindow)
        {
            mOperations.mDestroyWindow(mOperations.mUserdata, mWindow);
        }
        if (mLoaded)
        {
            mOperations.mUnloadLibrary(mOperations.mUserdata);
        }
    }

    void loaded() noexcept { mLoaded = true; }
    void window(SDL_Window* window) noexcept { mWindow = window; }
    void release() noexcept
    {
        mLoaded = false;
        mWindow = nullptr;
    }

private:
    LLWindowSDLVulkanOperations mOperations;
    SDL_Window*                 mWindow = nullptr;
    bool                        mLoaded = false;
};

} // namespace

LLWindowSDLVulkan::LLWindowSDLVulkan(const LLWindowSDLVulkanOperations& operations,
                                     SDL_Window*                        window,
                                     LLWindowVulkanRequirements&&       requirements) noexcept :
    mOperations(operations),
    mWindow(window),
    mRequirements(std::move(requirements)),
    mExplicitLoaderReference(true)
{
}

LLWindowSDLVulkan::~LLWindowSDLVulkan() noexcept
{
    if (!reset())
    {
        std::terminate();
    }
}

LLWindowSDLVulkan::LLWindowSDLVulkan(LLWindowSDLVulkan&& other) noexcept :
    mOperations(other.mOperations),
    mWindow(std::exchange(other.mWindow, nullptr)),
    mRequirements(std::move(other.mRequirements)),
    mInstanceGeneration(std::move(other.mInstanceGeneration)),
    mExplicitLoaderReference(std::exchange(other.mExplicitLoaderReference, false))
{
    other.mRequirements.reset();
}

LLWindowSDLVulkan& LLWindowSDLVulkan::operator=(LLWindowSDLVulkan&& other) noexcept
{
    if (this != &other)
    {
        if (!reset())
        {
            std::terminate();
        }
        mOperations              = other.mOperations;
        mWindow                  = std::exchange(other.mWindow, nullptr);
        mRequirements            = std::move(other.mRequirements);
        mInstanceGeneration      = std::move(other.mInstanceGeneration);
        mExplicitLoaderReference = std::exchange(other.mExplicitLoaderReference, false);
        other.mRequirements.reset();
    }
    return *this;
}

bool LLWindowSDLVulkan::isGenerationCurrent(U64 native_window_generation) const noexcept
{
    return native_window_generation != 0 && mRequirements && mRequirements->nativeWindowGeneration() == native_window_generation;
}

std::optional<LLRenderVulkan::VulkanInstanceAcquireError> LLWindowSDLVulkan::acquireInstanceGeneration(
    LLRenderVulkan::VulkanInstanceValidationMode  validation_mode,
    LLRenderVulkan::VulkanInstancePortabilityMode portability_mode) noexcept
{
    if (mInstanceGeneration)
    {
        LLRenderVulkan::VulkanInstanceAcquireError error;
        error.mCode = LLRenderVulkan::VulkanInstanceAcquireCode::InstanceAlreadyOwned;
        return error;
    }
    if (!mRequirements)
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

std::optional<LLRenderVulkan::VulkanSurfaceAcquireError> LLWindowSDLVulkan::acquireSurfaceGeneration() noexcept
{
    return acquireSurfaceGeneration(nullptr);
}

LLRenderVulkan::VulkanSurfaceAcquireResult LLWindowSDLVulkan::acquireSurfaceGeneration(
    LLRenderVulkan::VulkanInstanceDetail::AllocationCheckpoint allocation_checkpoint) noexcept
{
    using namespace LLRenderVulkan;

    if (!mInstanceGeneration)
    {
        return VulkanSurfaceAcquireError{ VulkanSurfaceAcquireCode::InstanceNotLive, std::nullopt, std::nullopt };
    }
    if (!mRequirements || !mWindow)
    {
        return VulkanSurfaceAcquireError{ VulkanSurfaceAcquireCode::StaleWindowGeneration, std::nullopt, std::nullopt };
    }

    SurfaceAcquireContext context{ this, mInstanceGeneration.get(), &mOperations, mWindow };

    VulkanSurfaceRequest request;
    request.mNativeWindowGeneration = mRequirements->nativeWindowGeneration();
    request.mInstanceOwnerCheck     = { &context, isSurfaceInstanceOwnerCurrent };
    request.mWindowGenerationCheck  = { &context, isSurfaceWindowGenerationCurrent };
    request.mCreateOperation        = { &context, createSurfaceGeneration };

    return VulkanInstanceDetail::acquireSurface(*mInstanceGeneration, request, allocation_checkpoint);
}

LLRenderVulkan::VulkanPresentationDeviceAcquireResult LLWindowSDLVulkan::acquirePresentationDeviceGeneration() noexcept
{
    using namespace LLRenderVulkan;

    if (!mInstanceGeneration)
    {
        return VulkanPresentationDeviceAcquireError{ VulkanPresentationDeviceAcquireCode::InstanceNotLive, std::nullopt };
    }
    if (!mRequirements || !mWindow)
    {
        return VulkanPresentationDeviceAcquireError{ VulkanPresentationDeviceAcquireCode::StaleWindowGeneration, std::nullopt };
    }

    SurfaceAcquireContext           context{ this, mInstanceGeneration.get(), &mOperations, mWindow };
    VulkanPresentationDeviceRequest request;
    request.mNativeWindowGeneration = mRequirements->nativeWindowGeneration();
    request.mInstanceOwnerCheck     = { &context, isSurfaceInstanceOwnerCurrent };
    request.mWindowGenerationCheck  = { &context, isSurfaceWindowGenerationCurrent };
    return mInstanceGeneration->acquirePresentationDeviceGeneration(request);
}

LLRenderVulkan::VulkanLogicalDeviceAcquireResult LLWindowSDLVulkan::acquireLogicalDeviceGeneration() noexcept
{
    using namespace LLRenderVulkan;

    if (!mInstanceGeneration)
    {
        return VulkanLogicalDeviceAcquireError{ VulkanLogicalDeviceAcquireCode::InstanceNotLive, std::nullopt };
    }
    if (!mRequirements || !mWindow)
    {
        return VulkanLogicalDeviceAcquireError{ VulkanLogicalDeviceAcquireCode::StaleWindowGeneration, std::nullopt };
    }

    SurfaceAcquireContext      context{ this, mInstanceGeneration.get(), &mOperations, mWindow };
    VulkanLogicalDeviceRequest request;
    request.mNativeWindowGeneration = mRequirements->nativeWindowGeneration();
    request.mInstanceOwnerCheck     = { &context, isSurfaceInstanceOwnerCurrent };
    request.mWindowGenerationCheck  = { &context, isSurfaceWindowGenerationCurrent };
    return mInstanceGeneration->acquireLogicalDeviceGeneration(request);
}

LLRenderVulkan::VulkanSwapchainConfigurationAcquireResult LLWindowSDLVulkan::acquireSwapchainConfigurationGeneration() noexcept
{
    using namespace LLRenderVulkan;

    if (!mInstanceGeneration)
    {
        return VulkanSwapchainConfigurationAcquireError{ VulkanSwapchainConfigurationAcquireCode::InstanceNotLive, std::nullopt };
    }
    if (!mRequirements || !mWindow || !mOperations.mGetWindowSizeInPixels)
    {
        return VulkanSwapchainConfigurationAcquireError{ VulkanSwapchainConfigurationAcquireCode::StaleWindowGeneration, std::nullopt };
    }

    int drawable_width  = 0;
    int drawable_height = 0;
    if (!mOperations.mGetWindowSizeInPixels(mOperations.mUserdata, mWindow, &drawable_width, &drawable_height) || drawable_width <= 0 ||
        drawable_height <= 0)
    {
        return VulkanSwapchainConfigurationAcquireError{ VulkanSwapchainConfigurationAcquireCode::InvalidDrawableExtent, std::nullopt };
    }

    SurfaceAcquireContext               context{ this, mInstanceGeneration.get(), &mOperations, mWindow };
    VulkanSwapchainConfigurationRequest request;
    request.mNativeWindowGeneration = mRequirements->nativeWindowGeneration();
    request.mDrawableExtent         = { static_cast<std::uint32_t>(drawable_width), static_cast<std::uint32_t>(drawable_height) };
    request.mInstanceOwnerCheck     = { &context, isSurfaceInstanceOwnerCurrent };
    request.mWindowGenerationCheck  = { &context, isSurfaceWindowGenerationCurrent };
    return mInstanceGeneration->acquireSwapchainConfigurationGeneration(request);
}

LLRenderVulkan::VulkanSwapchainAcquireResult LLWindowSDLVulkan::acquireSwapchainGeneration() noexcept
{
    using namespace LLRenderVulkan;

    if (!mInstanceGeneration)
    {
        return VulkanSwapchainAcquireError{ VulkanSwapchainAcquireCode::InstanceNotLive, std::nullopt };
    }
    if (!mRequirements || !mWindow || !mOperations.mGetWindowSizeInPixels)
    {
        return VulkanSwapchainAcquireError{ VulkanSwapchainAcquireCode::StaleWindowGeneration, std::nullopt };
    }

    int drawable_width  = 0;
    int drawable_height = 0;
    if (!mOperations.mGetWindowSizeInPixels(mOperations.mUserdata, mWindow, &drawable_width, &drawable_height) || drawable_width <= 0 ||
        drawable_height <= 0)
    {
        return VulkanSwapchainAcquireError{ VulkanSwapchainAcquireCode::InvalidDrawableExtent, std::nullopt };
    }

    SurfaceAcquireContext  context{ this, mInstanceGeneration.get(), &mOperations, mWindow };
    VulkanSwapchainRequest request;
    request.mNativeWindowGeneration = mRequirements->nativeWindowGeneration();
    request.mDrawableExtent         = { static_cast<std::uint32_t>(drawable_width), static_cast<std::uint32_t>(drawable_height) };
    request.mInstanceOwnerCheck     = { &context, isSurfaceInstanceOwnerCurrent };
    request.mWindowGenerationCheck  = { &context, isSurfaceWindowGenerationCurrent };
    return mInstanceGeneration->acquireSwapchainGeneration(request);
}

LLRenderVulkan::VulkanSwapchainImagesAcquireResult LLWindowSDLVulkan::acquireSwapchainImagesGeneration() noexcept
{
    using namespace LLRenderVulkan;

    if (!mInstanceGeneration)
    {
        return VulkanSwapchainImagesAcquireError{ VulkanSwapchainImagesAcquireCode::InstanceNotLive, std::nullopt };
    }
    if (!mRequirements || !mWindow || !mOperations.mGetWindowSizeInPixels)
    {
        return VulkanSwapchainImagesAcquireError{ VulkanSwapchainImagesAcquireCode::StaleWindowGeneration, std::nullopt };
    }

    int drawable_width  = 0;
    int drawable_height = 0;
    if (!mOperations.mGetWindowSizeInPixels(mOperations.mUserdata, mWindow, &drawable_width, &drawable_height) || drawable_width <= 0 ||
        drawable_height <= 0)
    {
        return VulkanSwapchainImagesAcquireError{ VulkanSwapchainImagesAcquireCode::InvalidDrawableExtent, std::nullopt };
    }

    SurfaceAcquireContext        context{ this, mInstanceGeneration.get(), &mOperations, mWindow };
    VulkanSwapchainImagesRequest request;
    request.mNativeWindowGeneration = mRequirements->nativeWindowGeneration();
    request.mDrawableExtent         = { static_cast<std::uint32_t>(drawable_width), static_cast<std::uint32_t>(drawable_height) };
    request.mInstanceOwnerCheck     = { &context, isSurfaceInstanceOwnerCurrent };
    request.mWindowGenerationCheck  = { &context, isSurfaceWindowGenerationCurrent };
    return mInstanceGeneration->acquireSwapchainImagesGeneration(request);
}

LLRenderVulkan::VulkanSwapchainPresentationTargetAcquireResult
LLWindowSDLVulkan::acquireSwapchainPresentationTargetGeneration() noexcept
{
    using namespace LLRenderVulkan;

    if (!mInstanceGeneration)
    {
        return VulkanSwapchainPresentationTargetAcquireError{ VulkanSwapchainPresentationTargetAcquireCode::InstanceNotLive,
                                                              std::nullopt };
    }
    if (!mRequirements || !mWindow || !mOperations.mGetWindowSizeInPixels)
    {
        return VulkanSwapchainPresentationTargetAcquireError{ VulkanSwapchainPresentationTargetAcquireCode::StaleWindowGeneration,
                                                              std::nullopt };
    }

    int drawable_width  = 0;
    int drawable_height = 0;
    if (!mOperations.mGetWindowSizeInPixels(mOperations.mUserdata, mWindow, &drawable_width, &drawable_height) || drawable_width <= 0 ||
        drawable_height <= 0)
    {
        return VulkanSwapchainPresentationTargetAcquireError{ VulkanSwapchainPresentationTargetAcquireCode::InvalidDrawableExtent,
                                                              std::nullopt };
    }

    SurfaceAcquireContext                     context{ this, mInstanceGeneration.get(), &mOperations, mWindow };
    VulkanSwapchainPresentationTargetRequest request;
    request.mNativeWindowGeneration = mRequirements->nativeWindowGeneration();
    request.mDrawableExtent         = { static_cast<std::uint32_t>(drawable_width), static_cast<std::uint32_t>(drawable_height) };
    request.mInstanceOwnerCheck     = { &context, isSurfaceInstanceOwnerCurrent };
    request.mWindowGenerationCheck  = { &context, isSurfaceWindowGenerationCurrent };
    return mInstanceGeneration->acquireSwapchainPresentationTargetGeneration(request);
}

LLRenderVulkan::VulkanSwapchainFrameSlotAcquireResult LLWindowSDLVulkan::acquireSwapchainFrameSlotGeneration() noexcept
{
    using namespace LLRenderVulkan;

    if (!mInstanceGeneration)
    {
        return VulkanSwapchainFrameSlotAcquireError{ VulkanSwapchainFrameSlotAcquireCode::InstanceNotLive, std::nullopt };
    }
    if (!mRequirements || !mWindow || !mOperations.mGetWindowSizeInPixels)
    {
        return VulkanSwapchainFrameSlotAcquireError{ VulkanSwapchainFrameSlotAcquireCode::StaleWindowGeneration, std::nullopt };
    }

    int drawable_width  = 0;
    int drawable_height = 0;
    if (!mOperations.mGetWindowSizeInPixels(mOperations.mUserdata, mWindow, &drawable_width, &drawable_height) || drawable_width <= 0 ||
        drawable_height <= 0)
    {
        return VulkanSwapchainFrameSlotAcquireError{ VulkanSwapchainFrameSlotAcquireCode::InvalidDrawableExtent, std::nullopt };
    }

    SurfaceAcquireContext           context{ this, mInstanceGeneration.get(), &mOperations, mWindow };
    VulkanSwapchainFrameSlotRequest request;
    request.mNativeWindowGeneration = mRequirements->nativeWindowGeneration();
    request.mDrawableExtent         = { static_cast<std::uint32_t>(drawable_width), static_cast<std::uint32_t>(drawable_height) };
    request.mInstanceOwnerCheck     = { &context, isSurfaceInstanceOwnerCurrent };
    request.mWindowGenerationCheck  = { &context, isSurfaceWindowGenerationCurrent };
    return mInstanceGeneration->acquireSwapchainFrameSlotGeneration(request);
}

LLRenderVulkan::VulkanSwapchainChainRebuildResult LLWindowSDLVulkan::rebuildSwapchainChain() noexcept
{
    using namespace LLRenderVulkan;

    if (!mInstanceGeneration)
    {
        return VulkanSwapchainChainRebuildError{ VulkanSwapchainChainRebuildCode::InstanceNotLive,
                                                 VulkanSwapchainChainRebuildPhase::Preflight,
                                                 {},
                                                 std::nullopt };
    }
    if (!mRequirements || !mWindow || !mOperations.mGetWindowSizeInPixels)
    {
        return VulkanSwapchainChainRebuildError{ VulkanSwapchainChainRebuildCode::StaleWindowGeneration,
                                                 VulkanSwapchainChainRebuildPhase::Preflight,
                                                 {},
                                                 std::nullopt };
    }

    int drawable_width  = 0;
    int drawable_height = 0;
    const bool drawable_queried =
        mOperations.mGetWindowSizeInPixels(mOperations.mUserdata, mWindow, &drawable_width, &drawable_height);

    SurfaceAcquireContext             context{ this, mInstanceGeneration.get(), &mOperations, mWindow };
    VulkanSwapchainChainRebuildRequest request;
    request.mNativeWindowGeneration = mRequirements->nativeWindowGeneration();
    if (drawable_queried && drawable_width >= 0 && drawable_height >= 0)
    {
        request.mDrawableExtent = VkExtent2D{ static_cast<std::uint32_t>(drawable_width),
                                              static_cast<std::uint32_t>(drawable_height) };
    }
    request.mInstanceOwnerCheck    = { &context, isSurfaceInstanceOwnerCurrent };
    request.mWindowGenerationCheck = { &context, isSurfaceWindowGenerationCurrent };
    return mInstanceGeneration->rebuildSwapchainChain(request);
}

LLRenderVulkan::VulkanSwapchainFrameSlotParentOperationResult LLWindowSDLVulkan::roundTripEmptySwapchainFrameSlot() noexcept
{
    const FrameSlotResult result = operateSwapchainFrameSlot(FrameSlotOperation::ExecuteEmptySubmission);
    if (const auto* error = std::get_if<LLRenderVulkan::VulkanSwapchainFrameSlotParentOperationError>(&result))
    {
        return *error;
    }
    return std::get<LLRenderVulkan::VulkanSwapchainFrameSlotDisposition>(result);
}

LLRenderVulkan::VulkanSwapchainFrameSlotParentOperationResult LLWindowSDLVulkan::retryEmptySwapchainFrameSlotCompletion() noexcept
{
    const FrameSlotResult result = operateSwapchainFrameSlot(FrameSlotOperation::RetryEmptySubmissionCompletion);
    if (const auto* error = std::get_if<LLRenderVulkan::VulkanSwapchainFrameSlotParentOperationError>(&result))
    {
        return *error;
    }
    return std::get<LLRenderVulkan::VulkanSwapchainFrameSlotDisposition>(result);
}

LLRenderVulkan::VulkanSwapchainFrameSlotParentPresentationResult LLWindowSDLVulkan::acquireToPresentSwapchainFrameSlot() noexcept
{
    const FrameSlotResult result = operateSwapchainFrameSlot(FrameSlotOperation::ExecuteAcquireToPresent);
    if (const auto* error = std::get_if<LLRenderVulkan::VulkanSwapchainFrameSlotParentOperationError>(&result))
    {
        return *error;
    }
    return std::get<LLRenderVulkan::VulkanSwapchainFrameSlotPresentationSuccess>(result);
}

LLRenderVulkan::VulkanSwapchainFrameSlotParentPresentationResult LLWindowSDLVulkan::acquireClearToPresentSwapchainFrameSlot(
    const LLRenderVulkan::VulkanSwapchainFrameClearColor& clear_color) noexcept
{
    using namespace LLRenderVulkan;

    if (!mInstanceGeneration)
    {
        return VulkanSwapchainFrameSlotParentOperationError{ VulkanSwapchainFrameSlotParentOperationCode::InstanceNotLive, std::nullopt };
    }
    if (!mRequirements || !mWindow || !mOperations.mGetWindowSizeInPixels)
    {
        return VulkanSwapchainFrameSlotParentOperationError{ VulkanSwapchainFrameSlotParentOperationCode::StaleWindowGeneration,
                                                             std::nullopt };
    }

    int drawable_width  = 0;
    int drawable_height = 0;
    if (!mOperations.mGetWindowSizeInPixels(mOperations.mUserdata, mWindow, &drawable_width, &drawable_height) || drawable_width <= 0 ||
        drawable_height <= 0)
    {
        return VulkanSwapchainFrameSlotParentOperationError{ VulkanSwapchainFrameSlotParentOperationCode::InvalidDrawableExtent,
                                                             std::nullopt };
    }

    SurfaceAcquireContext                    context{ this, mInstanceGeneration.get(), &mOperations, mWindow };
    VulkanSwapchainFrameSlotOperationRequest request;
    request.mNativeWindowGeneration = mRequirements->nativeWindowGeneration();
    request.mDrawableExtent         = { static_cast<std::uint32_t>(drawable_width), static_cast<std::uint32_t>(drawable_height) };
    request.mInstanceOwnerCheck     = { &context, isSurfaceInstanceOwnerCurrent };
    request.mWindowGenerationCheck  = { &context, isSurfaceWindowGenerationCurrent };
    return mInstanceGeneration->acquireClearToPresentSwapchainFrameSlot(request, clear_color);
}

LLRenderVulkan::VulkanSwapchainFrameSlotParentPresentationResult
LLWindowSDLVulkan::acquireRenderPassClearToPresentSwapchainFrameSlot(
    const LLRenderVulkan::VulkanSwapchainFrameClearColor& clear_color) noexcept
{
    using namespace LLRenderVulkan;

    if (!mInstanceGeneration)
    {
        return VulkanSwapchainFrameSlotParentOperationError{ VulkanSwapchainFrameSlotParentOperationCode::InstanceNotLive, std::nullopt };
    }
    if (!mRequirements || !mWindow || !mOperations.mGetWindowSizeInPixels)
    {
        return VulkanSwapchainFrameSlotParentOperationError{ VulkanSwapchainFrameSlotParentOperationCode::StaleWindowGeneration,
                                                             std::nullopt };
    }

    int drawable_width  = 0;
    int drawable_height = 0;
    if (!mOperations.mGetWindowSizeInPixels(mOperations.mUserdata, mWindow, &drawable_width, &drawable_height) || drawable_width <= 0 ||
        drawable_height <= 0)
    {
        return VulkanSwapchainFrameSlotParentOperationError{ VulkanSwapchainFrameSlotParentOperationCode::InvalidDrawableExtent,
                                                             std::nullopt };
    }

    SurfaceAcquireContext                    context{ this, mInstanceGeneration.get(), &mOperations, mWindow };
    VulkanSwapchainFrameSlotOperationRequest request;
    request.mNativeWindowGeneration = mRequirements->nativeWindowGeneration();
    request.mDrawableExtent         = { static_cast<std::uint32_t>(drawable_width), static_cast<std::uint32_t>(drawable_height) };
    request.mInstanceOwnerCheck     = { &context, isSurfaceInstanceOwnerCurrent };
    request.mWindowGenerationCheck  = { &context, isSurfaceWindowGenerationCurrent };
    return mInstanceGeneration->acquireRenderPassClearToPresentSwapchainFrameSlot(request, clear_color);
}

LLRenderVulkan::VulkanSwapchainFrameSlotParentPresentationResult LLWindowSDLVulkan::retrySwapchainFrameSlotPresentation() noexcept
{
    const FrameSlotResult result = operateSwapchainFrameSlot(FrameSlotOperation::RetryPresentation);
    if (const auto* error = std::get_if<LLRenderVulkan::VulkanSwapchainFrameSlotParentOperationError>(&result))
    {
        return *error;
    }
    return std::get<LLRenderVulkan::VulkanSwapchainFrameSlotPresentationSuccess>(result);
}

LLRenderVulkan::VulkanSwapchainFrameSlotParentPresentationResult LLWindowSDLVulkan::retrySwapchainFrameSlotPresentationCompletion() noexcept
{
    const FrameSlotResult result = operateSwapchainFrameSlot(FrameSlotOperation::RetryPresentationCompletion);
    if (const auto* error = std::get_if<LLRenderVulkan::VulkanSwapchainFrameSlotParentOperationError>(&result))
    {
        return *error;
    }
    return std::get<LLRenderVulkan::VulkanSwapchainFrameSlotPresentationSuccess>(result);
}

LLRenderVulkan::VulkanSwapchainFrameSlotParentOperationResult LLWindowSDLVulkan::cancelSwapchainFrameSlotPresentation() noexcept
{
    const FrameSlotResult result = operateSwapchainFrameSlot(FrameSlotOperation::CancelAcquireToPresent);
    if (const auto* error = std::get_if<LLRenderVulkan::VulkanSwapchainFrameSlotParentOperationError>(&result))
    {
        return *error;
    }
    return std::get<LLRenderVulkan::VulkanSwapchainFrameSlotDisposition>(result);
}

LLRenderVulkan::VulkanSwapchainFrameSlotParentOperationResult LLWindowSDLVulkan::retrySwapchainFrameSlotCancellationCompletion() noexcept
{
    const FrameSlotResult result = operateSwapchainFrameSlot(FrameSlotOperation::RetryCancellationCompletion);
    if (const auto* error = std::get_if<LLRenderVulkan::VulkanSwapchainFrameSlotParentOperationError>(&result))
    {
        return *error;
    }
    return std::get<LLRenderVulkan::VulkanSwapchainFrameSlotDisposition>(result);
}

LLWindowSDLVulkan::FrameSlotResult LLWindowSDLVulkan::operateSwapchainFrameSlot(FrameSlotOperation operation) noexcept
{
    using namespace LLRenderVulkan;

    const bool starts_new_work =
        operation == FrameSlotOperation::ExecuteEmptySubmission || operation == FrameSlotOperation::ExecuteAcquireToPresent;

    if (!mInstanceGeneration)
    {
        return VulkanSwapchainFrameSlotParentOperationError{ VulkanSwapchainFrameSlotParentOperationCode::InstanceNotLive, std::nullopt };
    }
    if (!mRequirements || !mWindow || (starts_new_work && !mOperations.mGetWindowSizeInPixels))
    {
        return VulkanSwapchainFrameSlotParentOperationError{ VulkanSwapchainFrameSlotParentOperationCode::StaleWindowGeneration,
                                                             std::nullopt };
    }

    VkExtent2D drawable_extent = mInstanceGeneration->swapchainDrawableExtent();
    if (starts_new_work)
    {
        int drawable_width  = 0;
        int drawable_height = 0;
        if (!mOperations.mGetWindowSizeInPixels(mOperations.mUserdata, mWindow, &drawable_width, &drawable_height) || drawable_width <= 0 ||
            drawable_height <= 0)
        {
            return VulkanSwapchainFrameSlotParentOperationError{ VulkanSwapchainFrameSlotParentOperationCode::InvalidDrawableExtent,
                                                                 std::nullopt };
        }
        drawable_extent = { static_cast<std::uint32_t>(drawable_width), static_cast<std::uint32_t>(drawable_height) };
    }

    SurfaceAcquireContext                    context{ this, mInstanceGeneration.get(), &mOperations, mWindow };
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

bool LLWindowSDLVulkan::resetSwapchainFrameSlotGeneration() noexcept
{
    if (!mInstanceGeneration || !mInstanceGeneration->hasSwapchainFrameSlotGeneration())
    {
        return false;
    }
    return mInstanceGeneration->resetSwapchainFrameSlotGeneration();
}

bool LLWindowSDLVulkan::resetSwapchainPresentationTargetGeneration() noexcept
{
    if (!mInstanceGeneration || !mInstanceGeneration->hasSwapchainPresentationTargetGeneration())
    {
        return false;
    }
    return mInstanceGeneration->resetSwapchainPresentationTargetGeneration();
}

bool LLWindowSDLVulkan::resetSwapchainImagesGeneration() noexcept
{
    if (!mInstanceGeneration || !mInstanceGeneration->hasSwapchainImagesGeneration())
    {
        return false;
    }
    return mInstanceGeneration->resetSwapchainImagesGeneration();
}

bool LLWindowSDLVulkan::resetSwapchainGeneration() noexcept
{
    if (!mInstanceGeneration || !mInstanceGeneration->hasSwapchainGeneration())
    {
        return false;
    }
    return mInstanceGeneration->resetSwapchainGeneration();
}

bool LLWindowSDLVulkan::resetSurfaceGeneration() noexcept
{
    if (!mInstanceGeneration || !mInstanceGeneration->hasSurfaceGeneration())
    {
        return false;
    }
    return mInstanceGeneration->resetSurfaceGeneration();
}

namespace LLWindowSDLVulkanDetail
{

LLRenderVulkan::VulkanSurfaceAcquireResult acquireSurfaceGeneration(
    LLWindowSDLVulkan&                                         owner,
    LLRenderVulkan::VulkanInstanceDetail::AllocationCheckpoint allocation_checkpoint) noexcept
{
    return owner.acquireSurfaceGeneration(allocation_checkpoint);
}

} // namespace LLWindowSDLVulkanDetail

bool LLWindowSDLVulkan::reset() noexcept
{
    if (mInstanceGeneration && !mInstanceGeneration->reset())
    {
        return false;
    }
    mInstanceGeneration.reset();
    mRequirements.reset();
    if (mWindow)
    {
        mOperations.mDestroyWindow(mOperations.mUserdata, mWindow);
        mWindow = nullptr;
    }
    if (mExplicitLoaderReference)
    {
        mOperations.mUnloadLibrary(mOperations.mUserdata);
        mExplicitLoaderReference = false;
    }
    return true;
}

const LLWindowSDLVulkanOperations& defaultLLWindowSDLVulkanOperations() noexcept
{
    static const LLWindowSDLVulkanOperations operations{
        nullptr,        loadLibrary,           unloadLibrary, createWindow,          destroyWindow,
        getWindowFlags, getWindowSizeInPixels, getResolver,   getInstanceExtensions, createSurface
    };
    return operations;
}

LLWindowSDLVulkanAcquireResult acquireLLWindowSDLVulkan(const LLWindowSDLVulkanCreateInfo& info,
                                                        U64                                native_window_generation,
                                                        const LLWindowSDLVulkanOperations& operations) noexcept
{
    if (!validOperations(operations))
    {
        return failure(LLWindowSDLVulkanAcquireCode::InvalidOperations);
    }

    Rollback rollback(operations);
    if (!operations.mLoadLibrary(operations.mUserdata))
    {
        return failure(LLWindowSDLVulkanAcquireCode::LoaderFailure);
    }
    rollback.loaded();

    SDL_Window* window = operations.mCreateWindow(operations.mUserdata, info);
    if (!window)
    {
        return failure(LLWindowSDLVulkanAcquireCode::WindowFailure);
    }
    rollback.window(window);

    const SDL_WindowFlags window_flags = operations.mGetWindowFlags(operations.mUserdata, window);
    if ((window_flags & SDL_WINDOW_VULKAN) == 0 || (window_flags & SDL_WINDOW_OPENGL) != 0)
    {
        return failure(LLWindowSDLVulkanAcquireCode::WindowFlagsFailure);
    }

    const LLWindowVulkanFunction resolver = operations.mGetResolver(operations.mUserdata);
    if (!resolver)
    {
        return failure(LLWindowSDLVulkanAcquireCode::ResolverFailure);
    }

    std::size_t        extension_count = 0;
    const char* const* extension_names = operations.mGetInstanceExtensions(operations.mUserdata, &extension_count);
    if (!extension_names)
    {
        return failure(LLWindowSDLVulkanAcquireCode::ExtensionQueryFailure);
    }

    LLWindowVulkanRequirementsBuildResult requirements_result =
        buildLLWindowVulkanRequirements(resolver, extension_count, extension_names, native_window_generation);
    if (auto* error = std::get_if<LLWindowVulkanRequirementsBuildError>(&requirements_result))
    {
        return LLWindowSDLVulkanAcquireError{ LLWindowSDLVulkanAcquireCode::RequirementsFailure, *error };
    }

    rollback.release();
    return LLWindowSDLVulkan(operations, window, std::move(std::get<LLWindowVulkanRequirements>(requirements_result)));
}
