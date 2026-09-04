/**
 * @file llviewervulkanruntime.cpp
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

#include "llviewerprecompiledheaders.h"

#include "llviewervulkanruntime.h"

#include "llgl.h"
#include "llwindow.h"
#include "llwindowsdl.h"
#include "llwindowsdlvulkan.h"
#include "llrendervulkaninstance.h"
#include "llrendervulkanui.h"

#include <SDL3/SDL.h>

#include <exception>
#include <variant>

namespace
{
constexpr std::uint32_t SUSPENDED_TICK_DELAY_MS = 16;

using namespace LLRenderVulkan;

} // namespace

LLViewerVulkanFrameController::LLViewerVulkanFrameController(LLViewerVulkanFrameBackend& backend) noexcept : mBackend(backend)
{
}

bool LLViewerVulkanFrameController::start(LLViewerVulkanDrawableExtent extent) noexcept
{
    if (mState != State::Starting)
    {
        return false;
    }
    mRequestedExtent  = extent;
    mRebuildRequested = true;
    return rebuildIfNeeded();
}

bool LLViewerVulkanFrameController::tick() noexcept
{
    if (mState == State::Failed || mState == State::Stopped || !rebuildIfNeeded())
    {
        return false;
    }
    if (mState == State::Suspended)
    {
        return true;
    }

    switch (mBackend.presentFrame())
    {
        case LLViewerVulkanPresentOutcome::Presented:
            ++mPresentedFrameCount;
            if (mSuspendedTransitionCount != 0)
            {
                ++mFramesSinceLastResume;
            }
            return true;
        case LLViewerVulkanPresentOutcome::RebuildRequired:
            mRebuildRequested = true;
            return true;
        case LLViewerVulkanPresentOutcome::SurfaceLost:
        case LLViewerVulkanPresentOutcome::Failed:
            fail();
            return false;
    }
    fail();
    return false;
}

void LLViewerVulkanFrameController::requestDrawableExtent(LLViewerVulkanDrawableExtent extent) noexcept
{
    if (mState == State::Failed || mState == State::Stopped)
    {
        return;
    }
    if (extent == mRequestedExtent && ((extent.mWidth == 0 || extent.mHeight == 0) ? mState == State::Suspended : mState == State::Ready))
    {
        return;
    }
    mRequestedExtent  = extent;
    mRebuildRequested = true;
}

void LLViewerVulkanFrameController::requestSuspend() noexcept
{
    requestDrawableExtent({});
}

bool LLViewerVulkanFrameController::shutdown() noexcept
{
    if (mState == State::Stopped)
    {
        return true;
    }
    const bool settled = mBackend.settleFrame();
    if (!settled)
    {
        fail();
        return false;
    }
    mState = State::Stopped;
    return true;
}

bool LLViewerVulkanFrameController::rebuildIfNeeded() noexcept
{
    if (!mRebuildRequested)
    {
        return mState == State::Ready || mState == State::Suspended;
    }
    if (!mBackend.settleFrame())
    {
        fail();
        return false;
    }

    const State previous = mState;
    ++mRebuildCount;
    switch (mBackend.rebuild(mRequestedExtent))
    {
        case LLViewerVulkanRebuildOutcome::Ready:
            mState            = State::Ready;
            mRebuildRequested = false;
            return true;
        case LLViewerVulkanRebuildOutcome::Suspended:
            mState            = State::Suspended;
            mRebuildRequested = false;
            if (previous != State::Suspended)
            {
                ++mSuspendedTransitionCount;
                mFramesSinceLastResume = 0;
            }
            return true;
        case LLViewerVulkanRebuildOutcome::Failed:
            fail();
            return false;
    }
    fail();
    return false;
}

class LLViewerVulkanRuntime::VulkanBackend final : public LLViewerVulkanFrameBackend
{
public:
    explicit VulkanBackend(LLWindowSDLVulkan& owner) noexcept :
        mOwner(owner),
        mGeneration(owner.instanceGeneration())
    {
    }

    bool initializeResources() noexcept
    {
        if (!mGeneration) return fail("missing-instance-generation");
        mUI = std::make_unique<UIRenderer>();
        const auto resolver = reinterpret_cast<PFN_vkGetInstanceProcAddr>(mOwner.requirements()->resolver());
        if (!mUI->initialize(resolver, mGeneration->instance(), mGeneration->physicalDevice(), mGeneration->logicalDevice(),
                             mGeneration->presentationQueue(), mGeneration->logicalDeviceQueueFamilyIndex()))
            return fail(mUI->error().c_str());
        return validationClean("ui-resource-initialize");
    }

    void setFrame(LLUIRender::Frame frame) { mNextFrame = std::move(frame); }
    void retireUI() noexcept { mUI.reset(); }

    bool settleFrame() noexcept override
    {
        if (!mGeneration || !mGeneration->hasSwapchainFrameSlotGeneration())
        {
            return true;
        }

        for (;;)
        {
            const auto disposition = mGeneration->swapchainFrameSlotDisposition();
            if (!disposition || *disposition == VulkanSwapchainFrameSlotDisposition::Reusable)
            {
                return true;
            }

            switch (*disposition)
            {
                case VulkanSwapchainFrameSlotDisposition::Pending:
                    (void)mOwner.retryEmptySwapchainFrameSlotCompletion();
                    continue;
                case VulkanSwapchainFrameSlotDisposition::PresentationReady:
                    (void)mOwner.cancelSwapchainFrameSlotPresentation();
                    continue;
                case VulkanSwapchainFrameSlotDisposition::SubmissionPending:
                case VulkanSwapchainFrameSlotDisposition::PresentPending:
                    (void)mOwner.retrySwapchainFrameSlotPresentationCompletion();
                    continue;
                case VulkanSwapchainFrameSlotDisposition::ImageAcquired:
                case VulkanSwapchainFrameSlotDisposition::ReleaseRequired:
                    (void)mOwner.cancelSwapchainFrameSlotPresentation();
                    continue;
                case VulkanSwapchainFrameSlotDisposition::CancellationPending:
                    (void)mOwner.retrySwapchainFrameSlotCancellationCompletion();
                    continue;
                case VulkanSwapchainFrameSlotDisposition::Reusable:
                case VulkanSwapchainFrameSlotDisposition::ResetRequired:
                    return true;
                case VulkanSwapchainFrameSlotDisposition::DeviceLost:
                    (void)fail("frame-device-lost");
                    return true;
                case VulkanSwapchainFrameSlotDisposition::FenceResetIndeterminate:
                case VulkanSwapchainFrameSlotDisposition::PresentationIndeterminate:
                case VulkanSwapchainFrameSlotDisposition::ReleaseIndeterminate:
                    return fail("frame-device-retirement-required");
            }
        }
    }

    LLViewerVulkanRebuildOutcome rebuild(LLViewerVulkanDrawableExtent extent) noexcept override
    {
        mUI->retireTarget();
        const auto  result  = mOwner.rebuildSwapchainChain({ extent.mWidth, extent.mHeight });
        const auto* outcome = std::get_if<VulkanSwapchainChainRebuildOutcome>(&result);
        if (!outcome)
        {
            fail("swapchain-rebuild");
            return LLViewerVulkanRebuildOutcome::Failed;
        }
        if (*outcome == VulkanSwapchainChainRebuildOutcome::Suspended)
        {
            return LLViewerVulkanRebuildOutcome::Suspended;
        }
        if (*outcome != VulkanSwapchainChainRebuildOutcome::Ready)
        {
            fail("ui-target-rebuild");
            return LLViewerVulkanRebuildOutcome::Failed;
        }
        return validationClean("swapchain-rebuild") ? LLViewerVulkanRebuildOutcome::Ready : LLViewerVulkanRebuildOutcome::Failed;
    }

    LLViewerVulkanPresentOutcome presentFrame() noexcept override
    {
        if (!settleFrame()) return LLViewerVulkanPresentOutcome::Failed;
        const auto extent = mGeneration->swapchainDrawableExtent();
        if (mNextFrame.width != extent.width || mNextFrame.height != extent.height)
            return LLViewerVulkanPresentOutcome::RebuildRequired;
        const auto image_extent = mGeneration->swapchainImageExtent();
        if (image_extent.width != extent.width || image_extent.height != extent.height)
        {
            fail("ui-target-extent-mismatch");
            return LLViewerVulkanPresentOutcome::Failed;
        }
        if (!mUI->prepare(std::move(mNextFrame), mGeneration->swapchainPresentationRenderPass()))
        {
            fail(mUI->error().c_str());
            return LLViewerVulkanPresentOutcome::Failed;
        }
        if (!mGeneration->setRenderPassRecorder({mUI.get(), [](void* owner, VkCommandBuffer command, VkExtent2D size) noexcept {
            static_cast<UIRenderer*>(owner)->record(command, size);
        }}))
        {
            fail("ui-frame-not-reusable");
            return LLViewerVulkanPresentOutcome::Failed;
        }
        const auto result = mOwner.acquireRenderPassClearToPresentSwapchainFrameSlot({{0.f, 0.f, 0.f, 1.f}});
        if (const auto* success = std::get_if<VulkanSwapchainFrameSlotPresentationSuccess>(&result))
        {
            switch (success->mOutcome)
            {
                case VulkanSwapchainFrameSlotPresentationOutcome::Presented:
                    return validationClean("present") ? LLViewerVulkanPresentOutcome::Presented : LLViewerVulkanPresentOutcome::Failed;
                case VulkanSwapchainFrameSlotPresentationOutcome::Suboptimal:
                case VulkanSwapchainFrameSlotPresentationOutcome::SwapchainReplacementRequired:
                    return LLViewerVulkanPresentOutcome::RebuildRequired;
                case VulkanSwapchainFrameSlotPresentationOutcome::SurfaceLost:
                    fail("surface-lost");
                    return LLViewerVulkanPresentOutcome::SurfaceLost;
            }
        }
        fail("ui-present");
        return LLViewerVulkanPresentOutcome::Failed;
    }

    std::uint32_t validationMessageCount() const noexcept { return mGeneration ? mGeneration->validationSnapshot().mMessageCount : 0; }

    const std::string& failure() const noexcept { return mFailure; }

private:
    bool validationClean(const char* operation) noexcept
    {
        const VulkanValidationSnapshot snapshot = mGeneration->validationSnapshot();
        if (snapshot.mMessageCount == 0)
        {
            return true;
        }
        mFailure = std::string(operation) + ":validation:" + std::string(snapshot.firstMessage());
        LL_WARNS("VulkanViewerSlice") << mFailure << LL_ENDL;
        return false;
    }

    bool fail(const char* operation) noexcept
    {
        if (mFailure.empty())
        {
            mFailure = operation;
        }
        LL_WARNS("VulkanViewerSlice") << "failure=" << mFailure << LL_ENDL;
        return false;
    }

    LLWindowSDLVulkan&                           mOwner;
    VulkanInstanceGeneration*                    mGeneration = nullptr;
    std::unique_ptr<UIRenderer>                   mUI;
    LLUIRender::Frame                            mNextFrame;
    std::string                                  mFailure;
};

LLViewerVulkanRuntime::LLViewerVulkanRuntime() = default;

LLViewerVulkanRuntime::~LLViewerVulkanRuntime()
{
    shutdown();
}

bool LLViewerVulkanRuntime::initialize(LLWindow& window)
{
#if LL_SDL_WINDOW && defined(LL_VULKAN_SDL_WSI)
    if (mWindow || window.getGraphicsAPI() != LLWindow::GraphicsAPI::Vulkan)
    {
        return false;
    }
    mWindow = &window;

    auto&              sdl_window = *static_cast<LLWindowSDL*>(mWindow);
    LLWindowSDLVulkan* owner      = sdl_window.getVulkanOwner();
    if (!owner)
    {
        shutdown();
        return false;
    }
    mBackend    = std::make_unique<VulkanBackend>(*owner);
    mController = std::make_unique<LLViewerVulkanFrameController>(*mBackend);
    if (!mBackend->initializeResources() || !mController->start(currentDrawableExtent()))
    {
        logSummary("initialization-failed");
        shutdown();
        return false;
    }

    mWindow->show();
    LL_INFOS("VulkanViewerSlice") << "status=ready api=vulkan visible=1" << LL_ENDL;
    return true;
#else
    (void)window;
    LL_WARNS("VulkanViewerSlice") << "failure=unsupported-platform" << LL_ENDL;
    return false;
#endif
}

bool LLViewerVulkanRuntime::tick(LLUIRender::Frame frame)
{
    if (mBackend) mBackend->setFrame(std::move(frame));
    const auto previous_state = mController ? mController->state() : LLViewerVulkanFrameController::State::Starting;
    const auto previous_resumed_frames = mController ? mController->framesSinceLastResume() : 0;
    requestCurrentDrawableExtent();
    const bool ticked = mController && mController->tick();
    if (ticked && mController->state() == LLViewerVulkanFrameController::State::Suspended)
    {
        if (previous_state != LLViewerVulkanFrameController::State::Suspended)
        {
            logSummary("suspended");
        }
        SDL_Delay(SUSPENDED_TICK_DELAY_MS);
    }
    else if (ticked && previous_resumed_frames == 0 && mController->framesSinceLastResume() != 0)
    {
        // Count a resume only after a successful presentation, not merely a
        // restored window or a rebuilt swapchain.
        logSummary("resumed");
    }
    return ticked;
}

void LLViewerVulkanRuntime::shutdown() noexcept
{
    if (!mWindow)
    {
        return;
    }

    const bool frame_settled  = !mController || mController->shutdown();
    if (!frame_settled)
    {
        logSummary("device-retirement-required");
        std::terminate();
    }

    if (mBackend) mBackend->retireUI();
    auto*      owner           = static_cast<LLWindowSDL*>(mWindow)->getVulkanOwner();
    const bool surface_retired = !owner || owner->resetSurfaceGeneration();
    if (!surface_retired)
    {
        logSummary("device-retirement-required");
        std::terminate();
    }

    const bool validation_clean = !mBackend || mBackend->validationMessageCount() == 0;
    logSummary(validation_clean && !failed() ? "stopped" : "failed");
    mController.reset();
    mBackend.reset();
    mWindow = nullptr;
}

bool LLViewerVulkanRuntime::isInitialized() const noexcept
{
    return mWindow && mController &&
           (mController->state() == LLViewerVulkanFrameController::State::Ready ||
            mController->state() == LLViewerVulkanFrameController::State::Suspended);
}

bool LLViewerVulkanRuntime::failed() const noexcept
{
    return (mController && mController->state() == LLViewerVulkanFrameController::State::Failed) ||
           (mBackend && !mBackend->failure().empty());
}

LLViewerVulkanDrawableExtent LLViewerVulkanRuntime::currentDrawableExtent() const noexcept
{
    LLCoordWindow size;
    if (!mWindow || mWindow->getMinimized() || !mWindow->getSize(&size) || size.mX <= 0 || size.mY <= 0)
    {
        return {};
    }
    return { static_cast<U32>(size.mX), static_cast<U32>(size.mY) };
}

void LLViewerVulkanRuntime::requestCurrentDrawableExtent() noexcept
{
    if (mController)
    {
        mController->requestDrawableExtent(currentDrawableExtent());
    }
}

void LLViewerVulkanRuntime::logSummary(const char* status) const noexcept
{
    const std::uint64_t frames     = mController ? mController->presentedFrameCount() : 0;
    const std::uint64_t rebuilds   = mController ? mController->rebuildCount() : 0;
    const std::uint64_t suspends   = mController ? mController->suspendedTransitionCount() : 0;
    const std::uint32_t validation = mBackend ? mBackend->validationMessageCount() : 0;
    const bool          gl_context = SDL_GL_GetCurrentContext() != nullptr;
    const LLWindowSDLGLAuditSnapshot gl_audit = getLLWindowSDLGLAuditSnapshot();
    LL_INFOS("VulkanViewerSlice") << "status=" << status << " frames=" << frames << " rebuilds=" << rebuilds << " suspends=" << suspends
                                  << " resumed_frames=" << (mController ? mController->framesSinceLastResume() : 0)
                                  << " validation_messages=" << validation << " gl_context=" << gl_context
                                  << " gl_manager=" << gGLManager.mInited
                                  << " gl_context_create_attempts=" << gl_audit.mContextCreateAttempts
                                  << " gl_swap_attempts=" << gl_audit.mBufferSwapAttempts
                                  << " gl_audit_armed=" << gl_audit.mArmed
                                  << " live_windows=" << LLWindow::instanceCount() << LL_ENDL;
}
