/**
 * @file llwindowmacosxvulkan.h
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

#ifndef LL_LLWINDOWMACOSXVULKAN_H
#define LL_LLWINDOWMACOSXVULKAN_H

#include "llrendervulkaninstance.h"
#include "llwindowvulkanrequirements.h"

#include <memory>
#include <optional>
#include <string>
#include <variant>

struct LLWindowMacOSXVulkanCreateInfo
{
    std::string mLoaderPath;
    U32         mBackingWidth  = 0;
    U32         mBackingHeight = 0;
};

enum class LLWindowMacOSXVulkanNativeCreateCode : U8
{
    InvalidRequest,
    MainThreadFailure,
    ApplicationFailure,
    StorageFailure,
    WindowFailure,
    ViewFailure,
    LayerFailure,
    GeometryFailure
};

struct LLWindowMacOSXVulkanNativeCreateError
{
    LLWindowMacOSXVulkanNativeCreateCode mCode = LLWindowMacOSXVulkanNativeCreateCode::ApplicationFailure;

    friend constexpr bool operator==(const LLWindowMacOSXVulkanNativeCreateError&,
                                     const LLWindowMacOSXVulkanNativeCreateError&) = default;
};

// These references are borrowed from mToken and are visible only to the
// platform operation seam. LLWindowMacOSXVulkan publishes scalar geometry and
// keeps every native reference private.
struct LLWindowMacOSXVulkanNativeWindow
{
    void* mToken          = nullptr;
    void* mWindow         = nullptr;
    void* mView           = nullptr;
    void* mMetalLayer     = nullptr;
    F64   mBackingScale   = 0.0;
    U32   mDrawableWidth  = 0;
    U32   mDrawableHeight = 0;
};

using LLWindowMacOSXVulkanNativeCreateResult =
    std::variant<LLWindowMacOSXVulkanNativeCreateError, LLWindowMacOSXVulkanNativeWindow>;

struct LLWindowMacOSXVulkanOperations
{
    void* mUserdata = nullptr;

    bool (*mIsMainThread)(void* userdata) noexcept = nullptr;
    void* (*mOpenLoader)(void* userdata, const char* path) noexcept = nullptr;
    void (*mCloseLoader)(void* userdata, void* loader) noexcept     = nullptr;
    LLWindowVulkanFunction (*mGetResolver)(void* userdata, void* loader) noexcept = nullptr;
    LLWindowMacOSXVulkanNativeCreateResult (*mCreateNativeWindow)(
        void* userdata,
        const LLWindowMacOSXVulkanCreateInfo& info) noexcept = nullptr;
    bool (*mRefreshNativeWindow)(void* userdata, LLWindowMacOSXVulkanNativeWindow& window) noexcept = nullptr;
    void (*mDestroyNativeWindow)(void* userdata, LLWindowMacOSXVulkanNativeWindow& window) noexcept = nullptr;
    LLRenderVulkan::VulkanSurfaceCreateOutcome (*mCreateSurface)(
        void*                        userdata,
        LLWindowVulkanFunction       resolver,
        void*                        metal_layer,
        VkInstance                   instance,
        const VkAllocationCallbacks* allocator,
        VkSurfaceKHR*                surface) noexcept = nullptr;
};

enum class LLWindowMacOSXVulkanAcquireCode : U8
{
    InvalidOperations,
    InvalidCreateInfo,
    InvalidNativeWindowGeneration,
    MainThreadRequired,
    LoaderFailure,
    ResolverFailure,
    NativeWindowFailure,
    NativeWindowIdentityFailure,
    NativeWindowGeometryFailure,
    RequirementsFailure
};

struct LLWindowMacOSXVulkanAcquireError
{
    LLWindowMacOSXVulkanAcquireCode                        mCode = LLWindowMacOSXVulkanAcquireCode::InvalidOperations;
    std::optional<LLWindowMacOSXVulkanNativeCreateError>   mNativeError;
    std::optional<LLWindowVulkanRequirementsBuildError>    mRequirementsError;

    friend constexpr bool operator==(const LLWindowMacOSXVulkanAcquireError&,
                                     const LLWindowMacOSXVulkanAcquireError&) = default;
};

class LLWindowMacOSXVulkan;

namespace LLWindowMacOSXVulkanDetail
{

LLRenderVulkan::VulkanSurfaceAcquireResult acquireSurfaceGeneration(
    LLWindowMacOSXVulkan&                                      owner,
    LLRenderVulkan::VulkanInstanceDetail::AllocationCheckpoint allocation_checkpoint) noexcept;

} // namespace LLWindowMacOSXVulkanDetail

class LLWindowMacOSXVulkan
{
public:
    ~LLWindowMacOSXVulkan() noexcept;

    LLWindowMacOSXVulkan(const LLWindowMacOSXVulkan&)            = delete;
    LLWindowMacOSXVulkan& operator=(const LLWindowMacOSXVulkan&) = delete;
    LLWindowMacOSXVulkan(LLWindowMacOSXVulkan&& other) noexcept;
    LLWindowMacOSXVulkan& operator=(LLWindowMacOSXVulkan&& other) noexcept;

    bool                              hasNativeWindow() const noexcept { return mNativeWindow.mToken != nullptr; }
    F64                               backingScale() const noexcept { return mNativeWindow.mBackingScale; }
    U32                               drawableWidth() const noexcept { return mNativeWindow.mDrawableWidth; }
    U32                               drawableHeight() const noexcept { return mNativeWindow.mDrawableHeight; }
    bool                              refreshNativeGeometry() noexcept;
    bool                              hasRequirements() const noexcept { return mRequirements.has_value(); }
    const LLWindowVulkanRequirements* requirements() const noexcept { return mRequirements ? &*mRequirements : nullptr; }
    bool                              isGenerationCurrent(U64 native_window_generation) const noexcept;
    std::optional<LLRenderVulkan::VulkanInstanceAcquireError> acquireInstanceGeneration(
        LLRenderVulkan::VulkanInstanceValidationMode  validation_mode,
        LLRenderVulkan::VulkanInstancePortabilityMode portability_mode) noexcept;
    std::optional<LLRenderVulkan::VulkanSurfaceAcquireError>  acquireSurfaceGeneration() noexcept;
    LLRenderVulkan::VulkanPresentationDeviceAcquireResult     acquirePresentationDeviceGeneration() noexcept;
    LLRenderVulkan::VulkanLogicalDeviceAcquireResult          acquireLogicalDeviceGeneration() noexcept;
    LLRenderVulkan::VulkanSwapchainConfigurationAcquireResult acquireSwapchainConfigurationGeneration() noexcept;
    LLRenderVulkan::VulkanSwapchainAcquireResult              acquireSwapchainGeneration() noexcept;
    LLRenderVulkan::VulkanSwapchainImagesAcquireResult        acquireSwapchainImagesGeneration() noexcept;
    LLRenderVulkan::VulkanSwapchainFrameSlotAcquireResult     acquireSwapchainFrameSlotGeneration() noexcept;
    LLRenderVulkan::VulkanSwapchainFrameSlotParentOperationResult roundTripEmptySwapchainFrameSlot() noexcept;
    LLRenderVulkan::VulkanSwapchainFrameSlotParentOperationResult retryEmptySwapchainFrameSlotCompletion() noexcept;
    LLRenderVulkan::VulkanSwapchainFrameSlotParentPresentationResult acquireToPresentSwapchainFrameSlot() noexcept;
    LLRenderVulkan::VulkanSwapchainFrameSlotParentPresentationResult retrySwapchainFrameSlotPresentation() noexcept;
    LLRenderVulkan::VulkanSwapchainFrameSlotParentPresentationResult retrySwapchainFrameSlotPresentationCompletion() noexcept;
    LLRenderVulkan::VulkanSwapchainFrameSlotParentOperationResult    cancelSwapchainFrameSlotPresentation() noexcept;
    LLRenderVulkan::VulkanSwapchainFrameSlotParentOperationResult    retrySwapchainFrameSlotCancellationCompletion() noexcept;
    const LLRenderVulkan::VulkanInstanceGeneration*           instanceGeneration() const noexcept { return mInstanceGeneration.get(); }
    bool                                                      resetSwapchainFrameSlotGeneration() noexcept;
    bool                                                      resetSwapchainImagesGeneration() noexcept;
    bool                                                      resetSwapchainGeneration() noexcept;
    bool                                                      resetSurfaceGeneration() noexcept;

    // AppKit ownership is main-thread-affine. An explicit off-main reset, or a
    // reset while the frame slot retains acquired or pending work, returns
    // false without releasing any resource. Destruction in either state
    // violates the caller contract.
    bool reset() noexcept;

private:
    friend struct LLWindowMacOSXVulkanFactory;
    friend LLRenderVulkan::VulkanSurfaceAcquireResult LLWindowMacOSXVulkanDetail::acquireSurfaceGeneration(
        LLWindowMacOSXVulkan&,
        LLRenderVulkan::VulkanInstanceDetail::AllocationCheckpoint) noexcept;

    LLWindowMacOSXVulkan(const LLWindowMacOSXVulkanOperations& operations,
                         void*                                  loader,
                         LLWindowMacOSXVulkanNativeWindow&&     native_window,
                         LLWindowVulkanRequirements&&           requirements) noexcept;
    LLRenderVulkan::VulkanSurfaceAcquireResult acquireSurfaceGeneration(
        LLRenderVulkan::VulkanInstanceDetail::AllocationCheckpoint allocation_checkpoint) noexcept;
    enum class FrameSlotOperation : U8
    {
        ExecuteEmptySubmission,
        RetryEmptySubmissionCompletion,
        ExecuteAcquireToPresent,
        RetryPresentation,
        RetryPresentationCompletion,
        CancelAcquireToPresent,
        RetryCancellationCompletion
    };
    using FrameSlotResult = std::variant<LLRenderVulkan::VulkanSwapchainFrameSlotParentOperationError,
                                         LLRenderVulkan::VulkanSwapchainFrameSlotDisposition,
                                         LLRenderVulkan::VulkanSwapchainFrameSlotPresentationSuccess>;
    FrameSlotResult operateSwapchainFrameSlot(FrameSlotOperation operation) noexcept;

    LLWindowMacOSXVulkanOperations                            mOperations;
    void*                                                     mLoader = nullptr;
    LLWindowMacOSXVulkanNativeWindow                          mNativeWindow;
    std::optional<LLWindowVulkanRequirements>                 mRequirements;
    std::unique_ptr<LLRenderVulkan::VulkanInstanceGeneration> mInstanceGeneration;
};

using LLWindowMacOSXVulkanAcquireResult = std::variant<LLWindowMacOSXVulkanAcquireError, LLWindowMacOSXVulkan>;

namespace LLWindowMacOSXVulkanDetail
{

LLWindowMacOSXVulkanAcquireResult acquire(
    const LLWindowMacOSXVulkanCreateInfo&                   info,
    U64                                                      native_window_generation,
    const LLWindowMacOSXVulkanOperations&                   operations,
    LLWindowVulkanRequirementsDetail::AllocationCheckpoint allocation_checkpoint) noexcept;

} // namespace LLWindowMacOSXVulkanDetail

const LLWindowMacOSXVulkanOperations& defaultLLWindowMacOSXVulkanOperations() noexcept;

LLWindowMacOSXVulkanAcquireResult acquireLLWindowMacOSXVulkan(
    const LLWindowMacOSXVulkanCreateInfo& info,
    U64                                    native_window_generation,
    const LLWindowMacOSXVulkanOperations& operations = defaultLLWindowMacOSXVulkanOperations()) noexcept;

#endif // LL_LLWINDOWMACOSXVULKAN_H
