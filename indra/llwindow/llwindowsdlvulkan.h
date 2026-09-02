/**
 * @file llwindowsdlvulkan.h
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

#ifndef LL_LLWINDOWSDLVULKAN_H
#define LL_LLWINDOWSDLVULKAN_H

#include "llrendervulkaninstance.h"
#include "llwindowvulkanrequirements.h"

#include "SDL3/SDL.h"

#include <memory>
#include <optional>
#include <string>
#include <variant>

struct LLWindowSDLVulkanCreateInfo
{
    std::string mTitle;
    int         mX                = 0;
    int         mY                = 0;
    int         mWidth            = 0;
    int         mHeight           = 0;
    bool        mResizable        = true;
    bool        mFullscreen       = false;
    bool        mHidden           = true;
    bool        mHighPixelDensity = false;
};

// The create operation receives a Vulkan-only description. The production
// operation sets SDL_PROP_WINDOW_CREATE_VULKAN_BOOLEAN and never requests an
// OpenGL window.
struct LLWindowSDLVulkanOperations
{
    void* mUserdata = nullptr;

    bool (*mLoadLibrary)(void* userdata) noexcept                                                        = nullptr;
    void (*mUnloadLibrary)(void* userdata) noexcept                                                      = nullptr;
    SDL_Window* (*mCreateWindow)(void* userdata, const LLWindowSDLVulkanCreateInfo& info) noexcept       = nullptr;
    void (*mDestroyWindow)(void* userdata, SDL_Window* window) noexcept                                  = nullptr;
    SDL_WindowFlags (*mGetWindowFlags)(void* userdata, SDL_Window* window) noexcept                      = nullptr;
    bool (*mGetWindowSizeInPixels)(void* userdata, SDL_Window* window, int* width, int* height) noexcept = nullptr;
    LLWindowVulkanFunction (*mGetResolver)(void* userdata) noexcept                                      = nullptr;
    const char* const* (*mGetInstanceExtensions)(void* userdata, std::size_t* count) noexcept            = nullptr;
    bool (*mCreateSurface)(void*                        userdata,
                           SDL_Window*                  window,
                           VkInstance                   instance,
                           const VkAllocationCallbacks* allocator,
                           VkSurfaceKHR*                surface) noexcept                                               = nullptr;
};

enum class LLWindowSDLVulkanAcquireCode : U8
{
    InvalidOperations,
    LoaderFailure,
    WindowFailure,
    WindowFlagsFailure,
    ResolverFailure,
    ExtensionQueryFailure,
    RequirementsFailure
};

struct LLWindowSDLVulkanAcquireError
{
    LLWindowSDLVulkanAcquireCode                        mCode = LLWindowSDLVulkanAcquireCode::InvalidOperations;
    std::optional<LLWindowVulkanRequirementsBuildError> mRequirementsError;

    friend constexpr bool operator==(const LLWindowSDLVulkanAcquireError&, const LLWindowSDLVulkanAcquireError&) = default;
};

class LLWindowSDLVulkan;

namespace LLWindowSDLVulkanDetail
{

LLRenderVulkan::VulkanSurfaceAcquireResult acquireSurfaceGeneration(
    LLWindowSDLVulkan&                                         owner,
    LLRenderVulkan::VulkanInstanceDetail::AllocationCheckpoint allocation_checkpoint) noexcept;

} // namespace LLWindowSDLVulkanDetail

// Owns both references involved in an SDL Vulkan window. SDL owns one loader
// reference through the window, while this object owns the explicit reference
// acquired before window creation.
class LLWindowSDLVulkan
{
public:
    ~LLWindowSDLVulkan() noexcept;

    LLWindowSDLVulkan(const LLWindowSDLVulkan&)            = delete;
    LLWindowSDLVulkan& operator=(const LLWindowSDLVulkan&) = delete;
    LLWindowSDLVulkan(LLWindowSDLVulkan&& other) noexcept;
    LLWindowSDLVulkan& operator=(LLWindowSDLVulkan&& other) noexcept;

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
    LLRenderVulkan::VulkanSwapchainPresentationTargetAcquireResult acquireSwapchainPresentationTargetGeneration() noexcept;
    LLRenderVulkan::VulkanSwapchainFrameSlotAcquireResult     acquireSwapchainFrameSlotGeneration() noexcept;
    LLRenderVulkan::VulkanSwapchainChainRebuildResult         rebuildSwapchainChain() noexcept;
    LLRenderVulkan::VulkanSwapchainFrameSlotParentOperationResult roundTripEmptySwapchainFrameSlot() noexcept;
    LLRenderVulkan::VulkanSwapchainFrameSlotParentOperationResult retryEmptySwapchainFrameSlotCompletion() noexcept;
    LLRenderVulkan::VulkanSwapchainFrameSlotParentPresentationResult acquireToPresentSwapchainFrameSlot() noexcept;
    LLRenderVulkan::VulkanSwapchainFrameSlotParentPresentationResult acquireClearToPresentSwapchainFrameSlot(
        const LLRenderVulkan::VulkanSwapchainFrameClearColor& clear_color) noexcept;
    LLRenderVulkan::VulkanSwapchainFrameSlotParentPresentationResult acquireRenderPassClearToPresentSwapchainFrameSlot(
        const LLRenderVulkan::VulkanSwapchainFrameClearColor& clear_color) noexcept;
    LLRenderVulkan::VulkanSwapchainFrameSlotParentPresentationResult retrySwapchainFrameSlotPresentation() noexcept;
    LLRenderVulkan::VulkanSwapchainFrameSlotParentPresentationResult retrySwapchainFrameSlotPresentationCompletion() noexcept;
    LLRenderVulkan::VulkanSwapchainFrameSlotParentOperationResult    cancelSwapchainFrameSlotPresentation() noexcept;
    LLRenderVulkan::VulkanSwapchainFrameSlotParentOperationResult    retrySwapchainFrameSlotCancellationCompletion() noexcept;
    const LLRenderVulkan::VulkanInstanceGeneration*           instanceGeneration() const noexcept { return mInstanceGeneration.get(); }
    bool                                                      resetSwapchainFrameSlotGeneration() noexcept;
    bool                                                      resetSwapchainPresentationTargetGeneration() noexcept;
    bool                                                      resetSwapchainImagesGeneration() noexcept;
    bool                                                      resetSwapchainGeneration() noexcept;
    bool                                                      resetSurfaceGeneration() noexcept;

    // Full reset returns false without releasing ownership while an explicit
    // frame-slot operation retains acquired or pending work. Destroying or
    // move-assigning over such an owner violates the caller contract.
    bool reset() noexcept;

private:
    friend class LLWindowSDL;
    friend std::variant<LLWindowSDLVulkanAcquireError, LLWindowSDLVulkan> acquireLLWindowSDLVulkan(
        const LLWindowSDLVulkanCreateInfo&,
        U64,
        const LLWindowSDLVulkanOperations&) noexcept;
    friend LLRenderVulkan::VulkanSurfaceAcquireResult LLWindowSDLVulkanDetail::acquireSurfaceGeneration(
        LLWindowSDLVulkan&,
        LLRenderVulkan::VulkanInstanceDetail::AllocationCheckpoint) noexcept;

    SDL_Window* window() const noexcept { return mWindow; }

    LLWindowSDLVulkan(const LLWindowSDLVulkanOperations& operations,
                      SDL_Window*                        window,
                      LLWindowVulkanRequirements&&       requirements) noexcept;
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

    LLWindowSDLVulkanOperations                               mOperations;
    SDL_Window*                                               mWindow = nullptr;
    std::optional<LLWindowVulkanRequirements>                 mRequirements;
    std::unique_ptr<LLRenderVulkan::VulkanInstanceGeneration> mInstanceGeneration;
    bool                                                      mExplicitLoaderReference = false;
};

using LLWindowSDLVulkanAcquireResult = std::variant<LLWindowSDLVulkanAcquireError, LLWindowSDLVulkan>;

const LLWindowSDLVulkanOperations& defaultLLWindowSDLVulkanOperations() noexcept;

LLWindowSDLVulkanAcquireResult acquireLLWindowSDLVulkan(
    const LLWindowSDLVulkanCreateInfo& info,
    U64                                native_window_generation,
    const LLWindowSDLVulkanOperations& operations = defaultLLWindowSDLVulkanOperations()) noexcept;

#endif // LL_LLWINDOWSDLVULKAN_H
