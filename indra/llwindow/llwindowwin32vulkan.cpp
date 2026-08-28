/**
 * @file llwindowwin32vulkan.cpp
 * @brief Isolated Win32 Vulkan surface ownership.
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

#include "llwindowwin32vulkan.h"

#include <array>
#include <exception>
#include <limits>
#include <new>
#include <utility>

namespace
{

constexpr std::array<const char*, 2> REQUIRED_EXTENSIONS{ "VK_KHR_surface", "VK_KHR_win32_surface" };

bool validOperations(const LLWindowWin32VulkanOperations& operations) noexcept
{
    return operations.mCurrentThreadId && operations.mOpenLoader && operations.mCloseLoader && operations.mGetResolver &&
           operations.mCreateNativeWindow && operations.mRefreshNativeWindow && operations.mDestroyNativeWindow &&
           operations.mCreateSurface;
}

bool validIdentity(const LLWindowWin32VulkanNativeWindow& native_window) noexcept
{
    return native_window.mToken && native_window.mModuleInstance && native_window.mWindow && native_window.mOwnerThreadId != 0;
}

bool validGeometry(const LLWindowWin32VulkanNativeWindow& native_window) noexcept
{
    return native_window.mClientWidth != 0 && native_window.mClientHeight != 0;
}

bool sameIdentity(const LLWindowWin32VulkanNativeWindow& left, const LLWindowWin32VulkanNativeWindow& right) noexcept
{
    return left.mToken == right.mToken && left.mModuleInstance == right.mModuleInstance && left.mWindow == right.mWindow &&
           left.mOwnerThreadId == right.mOwnerThreadId;
}

bool isOwnerThread(const LLWindowWin32VulkanOperations& operations, const LLWindowWin32VulkanNativeWindow& native_window) noexcept
{
    return native_window.mOwnerThreadId != 0 && operations.mCurrentThreadId &&
           operations.mCurrentThreadId(operations.mUserdata) == native_window.mOwnerThreadId;
}

LLWindowWin32VulkanAcquireError failure(LLWindowWin32VulkanAcquireCode                      code,
                                        std::optional<LLWindowWin32VulkanNativeCreateError> native_error       = std::nullopt,
                                        std::optional<LLWindowVulkanRequirementsBuildError> requirements_error = std::nullopt) noexcept
{
    return { code, native_error, requirements_error };
}

bool isInstanceWindowGenerationCurrent(void* userdata, std::uint64_t native_window_generation) noexcept
{
    const auto* owner = static_cast<const LLWindowWin32Vulkan*>(userdata);
    return owner && owner->isGenerationCurrent(native_window_generation);
}

struct SurfaceAcquireContext
{
    const LLWindowWin32Vulkan*                      mOwner              = nullptr;
    const LLRenderVulkan::VulkanInstanceGeneration* mInstanceGeneration = nullptr;
    const LLWindowWin32VulkanOperations*            mOperations         = nullptr;
    LLWindowVulkanFunction                          mResolver           = nullptr;
    void*                                           mModuleInstance     = nullptr;
    void*                                           mWindow             = nullptr;
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
    if (!context || !context->mOperations || !context->mOperations->mCreateSurface || !context->mResolver || !context->mModuleInstance ||
        !context->mWindow)
    {
        return LLRenderVulkan::VulkanSurfacePlatformFailure{};
    }
    return context->mOperations->mCreateSurface(context->mOperations->mUserdata, context->mResolver, context->mModuleInstance,
                                                context->mWindow, instance, allocator, surface);
}

class Rollback
{
public:
    explicit Rollback(const LLWindowWin32VulkanOperations& operations) noexcept : mOperations(operations) {}

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
    void nativeWindow(const LLWindowWin32VulkanNativeWindow& native_window) noexcept
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
    LLWindowWin32VulkanOperations   mOperations;
    void*                           mLoader = nullptr;
    LLWindowWin32VulkanNativeWindow mNativeWindow;
    bool                            mHasNativeWindow = false;
};

} // namespace

struct LLWindowWin32VulkanFactory
{
    static LLWindowWin32Vulkan create(const LLWindowWin32VulkanOperations& operations,
                                      void*                                loader,
                                      LLWindowWin32VulkanNativeWindow&&    native_window,
                                      LLWindowVulkanRequirements&&         requirements) noexcept
    {
        return LLWindowWin32Vulkan(operations, loader, std::move(native_window), std::move(requirements));
    }
};

LLWindowWin32Vulkan::LLWindowWin32Vulkan(const LLWindowWin32VulkanOperations& operations,
                                         void*                                loader,
                                         LLWindowWin32VulkanNativeWindow&&    native_window,
                                         LLWindowVulkanRequirements&&         requirements) noexcept :
    mOperations(operations),
    mLoader(loader),
    mNativeWindow(std::move(native_window)),
    mRequirements(std::move(requirements))
{
}

LLWindowWin32Vulkan::~LLWindowWin32Vulkan() noexcept
{
    if (!reset())
    {
        std::terminate();
    }
}

LLWindowWin32Vulkan::LLWindowWin32Vulkan(LLWindowWin32Vulkan&& other) noexcept :
    mOperations(other.mOperations),
    mLoader(std::exchange(other.mLoader, nullptr)),
    mNativeWindow(std::exchange(other.mNativeWindow, {})),
    mRequirements(std::move(other.mRequirements)),
    mInstanceGeneration(std::move(other.mInstanceGeneration))
{
    other.mRequirements.reset();
}

LLWindowWin32Vulkan& LLWindowWin32Vulkan::operator=(LLWindowWin32Vulkan&& other) noexcept
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

bool LLWindowWin32Vulkan::refreshNativeGeometry() noexcept
{
    if (!validIdentity(mNativeWindow) || !isOwnerThread(mOperations, mNativeWindow) || !mOperations.mRefreshNativeWindow)
    {
        return false;
    }

    LLWindowWin32VulkanNativeWindow refreshed = mNativeWindow;
    if (!mOperations.mRefreshNativeWindow(mOperations.mUserdata, refreshed) || !sameIdentity(mNativeWindow, refreshed) ||
        !validGeometry(refreshed))
    {
        return false;
    }

    mNativeWindow.mClientWidth  = refreshed.mClientWidth;
    mNativeWindow.mClientHeight = refreshed.mClientHeight;
    return true;
}

bool LLWindowWin32Vulkan::isGenerationCurrent(U64 native_window_generation) const noexcept
{
    return native_window_generation != 0 && mLoader && validIdentity(mNativeWindow) && mRequirements &&
           mRequirements->nativeWindowGeneration() == native_window_generation;
}

std::optional<LLRenderVulkan::VulkanInstanceAcquireError> LLWindowWin32Vulkan::acquireInstanceGeneration(
    LLRenderVulkan::VulkanInstanceValidationMode validation_mode) noexcept
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

    LLRenderVulkan::VulkanInstanceRequest request;
    request.mGetInstanceProcAddr      = reinterpret_cast<PFN_vkGetInstanceProcAddr>(mRequirements->resolver());
    request.mRequiredWindowExtensions = std::span<const std::string>(mRequirements->requiredInstanceExtensions());
    request.mNativeWindowGeneration   = mRequirements->nativeWindowGeneration();
    request.mGenerationCheck          = { this, isInstanceWindowGenerationCurrent };
    request.mValidationMode           = validation_mode;
    request.mPortabilityMode          = LLRenderVulkan::VulkanInstancePortabilityMode::Disabled;

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

std::optional<LLRenderVulkan::VulkanSurfaceAcquireError> LLWindowWin32Vulkan::acquireSurfaceGeneration() noexcept
{
    return acquireSurfaceGeneration(nullptr);
}

LLRenderVulkan::VulkanSurfaceAcquireResult LLWindowWin32Vulkan::acquireSurfaceGeneration(
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

    SurfaceAcquireContext context{
        this, mInstanceGeneration.get(), &mOperations, mRequirements->resolver(), mNativeWindow.mModuleInstance, mNativeWindow.mWindow
    };

    VulkanSurfaceRequest request;
    request.mNativeWindowGeneration = mRequirements->nativeWindowGeneration();
    request.mInstanceOwnerCheck     = { &context, isSurfaceInstanceOwnerCurrent };
    request.mWindowGenerationCheck  = { &context, isSurfaceWindowGenerationCurrent };
    request.mCreateOperation        = { &context, createSurfaceGeneration };

    return VulkanInstanceDetail::acquireSurface(*mInstanceGeneration, request, allocation_checkpoint);
}

bool LLWindowWin32Vulkan::resetSurfaceGeneration() noexcept
{
    if (!mInstanceGeneration || !mInstanceGeneration->hasSurfaceGeneration() || !isOwnerThread(mOperations, mNativeWindow))
    {
        return false;
    }
    mInstanceGeneration->resetSurfaceGeneration();
    return true;
}

bool LLWindowWin32Vulkan::reset() noexcept
{
    const bool owns_resources = mInstanceGeneration || mRequirements || mNativeWindow.mToken || mLoader;
    if (!owns_resources)
    {
        return true;
    }
    if (!isOwnerThread(mOperations, mNativeWindow))
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

namespace LLWindowWin32VulkanDetail
{

LLWindowWin32VulkanAcquireResult acquire(const LLWindowWin32VulkanCreateInfo&                   info,
                                         U64                                                    native_window_generation,
                                         const LLWindowWin32VulkanOperations&                   operations,
                                         LLWindowVulkanRequirementsDetail::AllocationCheckpoint allocation_checkpoint) noexcept
{
    if (!validOperations(operations))
    {
        return failure(LLWindowWin32VulkanAcquireCode::InvalidOperations);
    }
    constexpr U32 MAX_NATIVE_EXTENT = static_cast<U32>(std::numeric_limits<int>::max());
    if (info.mOwnerThreadId == 0 || info.mClientWidth == 0 || info.mClientHeight == 0 || info.mClientWidth > MAX_NATIVE_EXTENT ||
        info.mClientHeight > MAX_NATIVE_EXTENT)
    {
        return failure(LLWindowWin32VulkanAcquireCode::InvalidCreateInfo);
    }
    if (native_window_generation == 0)
    {
        return failure(LLWindowWin32VulkanAcquireCode::InvalidNativeWindowGeneration);
    }
    if (operations.mCurrentThreadId(operations.mUserdata) != info.mOwnerThreadId)
    {
        return failure(LLWindowWin32VulkanAcquireCode::OwnerThreadRequired);
    }

    Rollback    rollback(operations);
    void* const loader = operations.mOpenLoader(operations.mUserdata, info.mLoaderPath.c_str());
    if (!loader)
    {
        return failure(LLWindowWin32VulkanAcquireCode::LoaderFailure);
    }
    rollback.loader(loader);

    const LLWindowVulkanFunction resolver = operations.mGetResolver(operations.mUserdata, loader);
    if (!resolver)
    {
        return failure(LLWindowWin32VulkanAcquireCode::ResolverFailure);
    }

    LLWindowWin32VulkanNativeCreateResult native_result = operations.mCreateNativeWindow(operations.mUserdata, info);
    if (const auto* native_error = std::get_if<LLWindowWin32VulkanNativeCreateError>(&native_result))
    {
        return failure(LLWindowWin32VulkanAcquireCode::NativeWindowFailure, *native_error);
    }

    LLWindowWin32VulkanNativeWindow native_window = std::get<LLWindowWin32VulkanNativeWindow>(native_result);
    rollback.nativeWindow(native_window);
    if (!validIdentity(native_window) || native_window.mOwnerThreadId != info.mOwnerThreadId)
    {
        return failure(LLWindowWin32VulkanAcquireCode::NativeWindowIdentityFailure);
    }
    if (!validGeometry(native_window) || native_window.mClientWidth != info.mClientWidth ||
        native_window.mClientHeight != info.mClientHeight)
    {
        return failure(LLWindowWin32VulkanAcquireCode::NativeWindowGeometryFailure);
    }

    LLWindowVulkanRequirementsBuildResult requirements_result = LLWindowVulkanRequirementsDetail::build(
        resolver, REQUIRED_EXTENSIONS.size(), REQUIRED_EXTENSIONS.data(), native_window_generation, allocation_checkpoint);
    if (const auto* requirements_error = std::get_if<LLWindowVulkanRequirementsBuildError>(&requirements_result))
    {
        return failure(LLWindowWin32VulkanAcquireCode::RequirementsFailure, std::nullopt, *requirements_error);
    }

    rollback.release();
    return LLWindowWin32VulkanFactory::create(operations, loader, std::move(native_window),
                                              std::move(std::get<LLWindowVulkanRequirements>(requirements_result)));
}

LLRenderVulkan::VulkanSurfaceAcquireResult acquireSurfaceGeneration(
    LLWindowWin32Vulkan&                                       owner,
    LLRenderVulkan::VulkanInstanceDetail::AllocationCheckpoint allocation_checkpoint) noexcept
{
    return owner.acquireSurfaceGeneration(allocation_checkpoint);
}

} // namespace LLWindowWin32VulkanDetail

LLWindowWin32VulkanAcquireResult acquireLLWindowWin32Vulkan(const LLWindowWin32VulkanCreateInfo& info,
                                                            U64                                  native_window_generation,
                                                            const LLWindowWin32VulkanOperations& operations) noexcept
{
    return LLWindowWin32VulkanDetail::acquire(info, native_window_generation, operations, nullptr);
}
