/**
 * @file llviewervulkanruntime_test.cpp
 * @brief Focused tests for the Vulkan viewer frame controller.
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
#include "../test/lltut.h"

#include "../llviewervulkanruntime.h"
#include "llwindowcallbacks.h"

#include <deque>
#include <type_traits>
#include <vector>

static_assert(!std::is_base_of_v<LLWindowCallbacks, LLViewerVulkanRuntime>);

namespace tut
{
struct FakeBackend final : LLViewerVulkanFrameBackend
{
    bool settleFrame() noexcept override
    {
        ++mSettles;
        return mSettleSucceeds;
    }

    LLViewerVulkanRebuildOutcome rebuild(LLViewerVulkanDrawableExtent extent) noexcept override
    {
        mRebuilds.push_back(extent);
        if (!mRebuildResults.empty())
        {
            const auto result = mRebuildResults.front();
            mRebuildResults.pop_front();
            return result;
        }
        return extent.mWidth == 0 || extent.mHeight == 0 ? LLViewerVulkanRebuildOutcome::Suspended : LLViewerVulkanRebuildOutcome::Ready;
    }

    LLViewerVulkanPresentOutcome presentFrame() noexcept override
    {
        ++mPresents;
        if (mPresentResults.empty())
        {
            return LLViewerVulkanPresentOutcome::Presented;
        }
        const auto result = mPresentResults.front();
        mPresentResults.pop_front();
        return result;
    }

    bool                                      mSettleSucceeds = true;
    U32                                       mSettles        = 0;
    U32                                       mPresents       = 0;
    std::vector<LLViewerVulkanDrawableExtent> mRebuilds;
    std::deque<LLViewerVulkanRebuildOutcome>  mRebuildResults;
    std::deque<LLViewerVulkanPresentOutcome>  mPresentResults;
};

struct viewer_vulkan_runtime_test
{
};

using viewer_vulkan_runtime_test_group  = test_group<viewer_vulkan_runtime_test>;
using viewer_vulkan_runtime_test_object = viewer_vulkan_runtime_test_group::object;
viewer_vulkan_runtime_test_group viewer_vulkan_runtime_tests("viewer Vulkan runtime");

template<>
template<>
void viewer_vulkan_runtime_test_object::test<1>()
{
    FakeBackend                   backend;
    LLViewerVulkanFrameController controller(backend);

    ensure("initial ready rebuild succeeds", controller.start({ 1280, 720 }));
    ensure("controller becomes ready", controller.state() == LLViewerVulkanFrameController::State::Ready);
    ensure("first sampled frame presents", controller.tick());
    ensure("second sampled frame presents", controller.tick());
    ensure_equals("two sampled frames were requested", backend.mPresents, U32{ 2 });
    ensure_equals("two frames were counted", controller.presentedFrameCount(), std::uint64_t{ 2 });
    ensure_equals("initial frames are not restored frames", controller.framesSinceLastResume(), std::uint64_t{ 0 });
}

template<>
template<>
void viewer_vulkan_runtime_test_object::test<2>()
{
    FakeBackend backend;
    backend.mPresentResults.push_back(LLViewerVulkanPresentOutcome::RebuildRequired);
    LLViewerVulkanFrameController controller(backend);

    ensure("start succeeds", controller.start({ 800, 600 }));
    ensure("suboptimal frame defers rebuild", controller.tick());
    ensure_equals("rebuild is deferred to the next tick", backend.mRebuilds.size(), std::size_t{ 1 });
    ensure("next tick rebuilds and presents", controller.tick());
    ensure_equals("one deferred rebuild occurred", backend.mRebuilds.size(), std::size_t{ 2 });
    ensure("the rebuild used the current extent", backend.mRebuilds.back() == LLViewerVulkanDrawableExtent{ 800, 600 });
}

template<>
template<>
void viewer_vulkan_runtime_test_object::test<3>()
{
    FakeBackend                   backend;
    LLViewerVulkanFrameController controller(backend);

    ensure("start succeeds", controller.start({ 1024, 768 }));
    controller.requestSuspend();
    ensure("suspend rebuild succeeds", controller.tick());
    ensure("zero extent suspends", controller.state() == LLViewerVulkanFrameController::State::Suspended);
    ensure_equals("suspended tick submits nothing", backend.mPresents, U32{ 0 });
    ensure("repeated suspended tick succeeds", controller.tick());
    ensure_equals("repeated suspended tick does not rebuild", backend.mRebuilds.size(), std::size_t{ 2 });

    controller.requestDrawableExtent({ 1280, 720 });
    ensure("restore rebuilds and presents", controller.tick());
    ensure("restore returns to ready", controller.state() == LLViewerVulkanFrameController::State::Ready);
    ensure_equals("restore presents once", backend.mPresents, U32{ 1 });
    ensure_equals("one suspension transition is counted", controller.suspendedTransitionCount(), std::uint64_t{ 1 });
    ensure_equals("a successful restored frame is counted", controller.framesSinceLastResume(), std::uint64_t{ 1 });
    ensure("restored presentation repeats", controller.tick());
    ensure_equals("restored frames continue counting", controller.framesSinceLastResume(), std::uint64_t{ 2 });

    controller.requestSuspend();
    ensure("second minimize suspends", controller.tick());
    ensure_equals("a new minimize invalidates previous resume evidence", controller.framesSinceLastResume(), std::uint64_t{ 0 });
    backend.mPresentResults.push_back(LLViewerVulkanPresentOutcome::RebuildRequired);
    controller.requestDrawableExtent({ 1280, 720 });
    ensure("restore can request another rebuild", controller.tick());
    ensure_equals("rebuild alone is not a presented frame", controller.framesSinceLastResume(), std::uint64_t{ 0 });
    ensure("retry presents", controller.tick());
    ensure_equals("successful retry proves resume", controller.framesSinceLastResume(), std::uint64_t{ 1 });
}

template<>
template<>
void viewer_vulkan_runtime_test_object::test<4>()
{
    FakeBackend                   backend;
    LLViewerVulkanFrameController controller(backend);

    ensure("start succeeds", controller.start({ 640, 480 }));
    backend.mPresentResults.push_back(LLViewerVulkanPresentOutcome::SurfaceLost);
    ensure("surface loss fails the tick", !controller.tick());
    ensure("surface loss is fatal", controller.state() == LLViewerVulkanFrameController::State::Failed);
}

template<>
template<>
void viewer_vulkan_runtime_test_object::test<5>()
{
    FakeBackend                   backend;
    LLViewerVulkanFrameController controller(backend);

    ensure("start succeeds", controller.start({ 640, 480 }));
    ensure("orderly shutdown settles", controller.shutdown());
    ensure("controller is stopped", controller.state() == LLViewerVulkanFrameController::State::Stopped);
    ensure_equals("startup and shutdown each settle", backend.mSettles, U32{ 2 });
    ensure("stopped controller does not tick", !controller.tick());
}
} // namespace tut
