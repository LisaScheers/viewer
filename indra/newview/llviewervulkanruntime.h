/**
 * @file llviewervulkanruntime.h
 * @brief Thin Vulkan-only viewer application runtime.
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

#ifndef LL_LLVIEWERVULKANRUNTIME_H
#define LL_LLVIEWERVULKANRUNTIME_H

#include "stdtypes.h"
#include "lluirenderframe.h"

#include <cstdint>
#include <memory>

class LLWindow;

struct LLViewerVulkanDrawableExtent
{
    U32 mWidth  = 0;
    U32 mHeight = 0;

    friend constexpr bool operator==(LLViewerVulkanDrawableExtent, LLViewerVulkanDrawableExtent) = default;
};

enum class LLViewerVulkanRebuildOutcome : U8
{
    Ready,
    Suspended,
    Failed
};

enum class LLViewerVulkanPresentOutcome : U8
{
    Presented,
    RebuildRequired,
    SurfaceLost,
    Failed
};

class LLViewerVulkanFrameBackend
{
public:
    virtual ~LLViewerVulkanFrameBackend() = default;

    virtual bool                         settleFrame() noexcept                                = 0;
    virtual LLViewerVulkanRebuildOutcome rebuild(LLViewerVulkanDrawableExtent extent) noexcept = 0;
    virtual LLViewerVulkanPresentOutcome presentFrame() noexcept                        = 0;
};

// This controller is deliberately independent of LLWindow and Vulkan handles.
// It is the focused test seam for frame/rebuild orchestration.
class LLViewerVulkanFrameController
{
public:
    enum class State : U8
    {
        Starting,
        Ready,
        Suspended,
        Failed,
        Stopped
    };

    explicit LLViewerVulkanFrameController(LLViewerVulkanFrameBackend& backend) noexcept;

    bool start(LLViewerVulkanDrawableExtent extent) noexcept;
    bool tick() noexcept;
    void requestDrawableExtent(LLViewerVulkanDrawableExtent extent) noexcept;
    void requestSuspend() noexcept;
    bool shutdown() noexcept;

    State         state() const noexcept { return mState; }
    std::uint64_t presentedFrameCount() const noexcept { return mPresentedFrameCount; }
    std::uint64_t rebuildCount() const noexcept { return mRebuildCount; }
    std::uint64_t suspendedTransitionCount() const noexcept { return mSuspendedTransitionCount; }
    std::uint64_t framesSinceLastResume() const noexcept { return mFramesSinceLastResume; }

private:
    bool rebuildIfNeeded() noexcept;
    void fail() noexcept { mState = State::Failed; }

    LLViewerVulkanFrameBackend&  mBackend;
    LLViewerVulkanDrawableExtent mRequestedExtent{};
    State                        mState                    = State::Starting;
    bool                         mRebuildRequested         = false;
    std::uint64_t                mPresentedFrameCount      = 0;
    std::uint64_t                mRebuildCount             = 0;
    std::uint64_t                mSuspendedTransitionCount = 0;
    std::uint64_t                mFramesSinceLastResume     = 0;
};

class LLViewerVulkanRuntime final
{
public:
    LLViewerVulkanRuntime();
    ~LLViewerVulkanRuntime();

    LLViewerVulkanRuntime(const LLViewerVulkanRuntime&)            = delete;
    LLViewerVulkanRuntime& operator=(const LLViewerVulkanRuntime&) = delete;

    bool initialize(LLWindow& window);
    bool tick(LLUIRender::Frame frame);
    void shutdown() noexcept;

    bool isInitialized() const noexcept;
    bool failed() const noexcept;

private:
    class VulkanBackend;

    LLViewerVulkanDrawableExtent currentDrawableExtent() const noexcept;
    void                         requestCurrentDrawableExtent() noexcept;
    void                         logSummary(const char* status) const noexcept;

    LLWindow*                                      mWindow = nullptr;
    std::unique_ptr<VulkanBackend>                 mBackend;
    std::unique_ptr<LLViewerVulkanFrameController> mController;
};

#endif // LL_LLVIEWERVULKANRUNTIME_H
