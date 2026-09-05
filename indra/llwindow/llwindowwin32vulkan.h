/**
 * @file llwindowwin32vulkan.h
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

#ifndef LL_LLWINDOWWIN32VULKAN_H
#define LL_LLWINDOWWIN32VULKAN_H

#include "llrendervulkaninstance.h"
#include "llwindowvulkanrequirements.h"

#include <memory>
#include <optional>
#include <string>
#include <variant>

struct LLWindowWin32VulkanCreateInfo
{
    std::wstring mLoaderPath;
    U64          mOwnerThreadId = 0;
    U32          mClientWidth   = 0;
    U32          mClientHeight  = 0;
};

enum class LLWindowWin32VulkanNativeCreateCode : U8
{
    InvalidRequest,
    StorageFailure,
    ModuleFailure,
    ClassRegistrationFailure,
    WindowFailure,
    IdentityFailure,
    GeometryFailure
};

struct LLWindowWin32VulkanNativeCreateError
{
    LLWindowWin32VulkanNativeCreateCode mCode = LLWindowWin32VulkanNativeCreateCode::WindowFailure;

    friend constexpr bool operator==(const LLWindowWin32VulkanNativeCreateError&, const LLWindowWin32VulkanNativeCreateError&) = default;
};

// These identities are borrowed from mToken and are visible only to the
// platform operation seam. The owner publishes scalar observations only.
struct LLWindowWin32VulkanNativeWindow
{
    void* mToken          = nullptr;
    void* mModuleInstance = nullptr;
    void* mWindow         = nullptr;
    U64   mOwnerThreadId  = 0;
    U32   mClientWidth    = 0;
    U32   mClientHeight   = 0;
};

using LLWindowWin32VulkanNativeCreateResult = std::variant<LLWindowWin32VulkanNativeCreateError, LLWindowWin32VulkanNativeWindow>;

struct LLWindowWin32VulkanOperations
{
    void* mUserdata = nullptr;

    U64 (*mCurrentThreadId)(void* userdata) noexcept                                                                 = nullptr;
    void* (*mOpenLoader)(void* userdata, const wchar_t* path) noexcept                                               = nullptr;
    void (*mCloseLoader)(void* userdata, void* loader) noexcept                                                      = nullptr;
    LLWindowVulkanFunction (*mGetResolver)(void* userdata, void* loader) noexcept                                    = nullptr;
    LLWindowWin32VulkanNativeCreateResult (*mCreateNativeWindow)(void*                                userdata,
                                                                 const LLWindowWin32VulkanCreateInfo& info) noexcept = nullptr;
    bool (*mRefreshNativeWindow)(void* userdata, LLWindowWin32VulkanNativeWindow& window) noexcept                   = nullptr;
    void (*mDestroyNativeWindow)(void* userdata, LLWindowWin32VulkanNativeWindow& window) noexcept                   = nullptr;
    LLRenderVulkan::VulkanSurfaceCreateOutcome (*mCreateSurface)(void*                        userdata,
                                                                 LLWindowVulkanFunction       resolver,
                                                                 void*                        module_instance,
                                                                 void*                        window,
                                                                 VkInstance                   instance,
                                                                 const VkAllocationCallbacks* allocator,
                                                                 VkSurfaceKHR*                surface) noexcept                     = nullptr;
};

enum class LLWindowWin32VulkanAcquireCode : U8
{
    InvalidOperations,
    InvalidCreateInfo,
    InvalidNativeWindowGeneration,
    OwnerThreadRequired,
    LoaderFailure,
    ResolverFailure,
    NativeWindowFailure,
    NativeWindowIdentityFailure,
    NativeWindowGeometryFailure,
    RequirementsFailure
};

struct LLWindowWin32VulkanAcquireError
{
    LLWindowWin32VulkanAcquireCode                      mCode = LLWindowWin32VulkanAcquireCode::InvalidOperations;
    std::optional<LLWindowWin32VulkanNativeCreateError> mNativeError;
    std::optional<LLWindowVulkanRequirementsBuildError> mRequirementsError;

    friend constexpr bool operator==(const LLWindowWin32VulkanAcquireError&, const LLWindowWin32VulkanAcquireError&) = default;
};

class LLWindowWin32Vulkan;

namespace LLWindowWin32VulkanDetail
{

LLRenderVulkan::VulkanSurfaceAcquireResult acquireSurfaceGeneration(
    LLWindowWin32Vulkan&                                       owner,
    LLRenderVulkan::VulkanInstanceDetail::AllocationCheckpoint allocation_checkpoint) noexcept;

} // namespace LLWindowWin32VulkanDetail

class LLWindowWin32Vulkan
{
public:
    ~LLWindowWin32Vulkan() noexcept;

    LLWindowWin32Vulkan(const LLWindowWin32Vulkan&)            = delete;
    LLWindowWin32Vulkan& operator=(const LLWindowWin32Vulkan&) = delete;
    LLWindowWin32Vulkan(LLWindowWin32Vulkan&& other) noexcept;
    LLWindowWin32Vulkan& operator=(LLWindowWin32Vulkan&& other) noexcept;

    bool                              hasNativeWindow() const noexcept { return mNativeWindow.mToken != nullptr; }
    U64                               ownerThreadId() const noexcept { return mNativeWindow.mOwnerThreadId; }
    U32                               clientWidth() const noexcept { return mNativeWindow.mClientWidth; }
    U32                               clientHeight() const noexcept { return mNativeWindow.mClientHeight; }
    bool                              refreshNativeGeometry() noexcept;
    bool                              hasRequirements() const noexcept { return mRequirements.has_value(); }
    const LLWindowVulkanRequirements* requirements() const noexcept { return mRequirements ? &*mRequirements : nullptr; }
    bool                              isGenerationCurrent(U64 native_window_generation) const noexcept;
    std::optional<LLRenderVulkan::VulkanInstanceAcquireError> acquireInstanceGeneration(
        LLRenderVulkan::VulkanInstanceValidationMode validation_mode) noexcept;
    std::optional<LLRenderVulkan::VulkanSurfaceAcquireError> acquireSurfaceGeneration() noexcept;
    const LLRenderVulkan::VulkanInstanceGeneration*          instanceGeneration() const noexcept { return mInstanceGeneration.get(); }
    bool                                                     resetSurfaceGeneration() noexcept;

    // HWND ownership is creator-thread-affine. An explicit off-thread reset
    // returns false without releasing any resource. Destruction of a still
    // owning object on another thread is a contract violation.
    bool reset() noexcept;

private:
    friend struct LLWindowWin32VulkanFactory;
    friend LLRenderVulkan::VulkanSurfaceAcquireResult LLWindowWin32VulkanDetail::acquireSurfaceGeneration(
        LLWindowWin32Vulkan&,
        LLRenderVulkan::VulkanInstanceDetail::AllocationCheckpoint) noexcept;

    LLWindowWin32Vulkan(const LLWindowWin32VulkanOperations& operations,
                        void*                                loader,
                        LLWindowWin32VulkanNativeWindow&&    native_window,
                        LLWindowVulkanRequirements&&         requirements) noexcept;
    LLRenderVulkan::VulkanSurfaceAcquireResult acquireSurfaceGeneration(
        LLRenderVulkan::VulkanInstanceDetail::AllocationCheckpoint allocation_checkpoint) noexcept;

    LLWindowWin32VulkanOperations                             mOperations;
    void*                                                     mLoader = nullptr;
    LLWindowWin32VulkanNativeWindow                           mNativeWindow;
    std::optional<LLWindowVulkanRequirements>                 mRequirements;
    std::unique_ptr<LLRenderVulkan::VulkanInstanceGeneration> mInstanceGeneration;
};

using LLWindowWin32VulkanAcquireResult = std::variant<LLWindowWin32VulkanAcquireError, LLWindowWin32Vulkan>;

namespace LLWindowWin32VulkanDetail
{

LLWindowWin32VulkanAcquireResult acquire(const LLWindowWin32VulkanCreateInfo&                   info,
                                         U64                                                    native_window_generation,
                                         const LLWindowWin32VulkanOperations&                   operations,
                                         LLWindowVulkanRequirementsDetail::AllocationCheckpoint allocation_checkpoint) noexcept;

} // namespace LLWindowWin32VulkanDetail

const LLWindowWin32VulkanOperations& defaultLLWindowWin32VulkanOperations() noexcept;

LLWindowWin32VulkanAcquireResult acquireLLWindowWin32Vulkan(
    const LLWindowWin32VulkanCreateInfo& info,
    U64                                  native_window_generation,
    const LLWindowWin32VulkanOperations& operations = defaultLLWindowWin32VulkanOperations()) noexcept;

#endif // LL_LLWINDOWWIN32VULKAN_H
