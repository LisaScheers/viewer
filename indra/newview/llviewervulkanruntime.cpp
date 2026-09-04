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

#include "llapp.h"
#include "llgl.h"
#include "llkeyboard.h"
#include "lltextureuploaddiagnostic.h"
#include "llwindow.h"
#include "llwindowsdl.h"
#include "llwindowsdlvulkan.h"
#include "llrendervulkaninstance.h"

#include <SDL3/SDL.h>

#include <exception>
#include <variant>

namespace
{
constexpr std::uint64_t UPLOAD_TIMEOUT_NS       = 1'000'000'000;
constexpr std::uint32_t SUSPENDED_TICK_DELAY_MS = 16;

using namespace LLRenderVulkan;

bool completed(VulkanUploadTransferParentOperationResult result) noexcept
{
    const auto* disposition = std::get_if<VulkanUploadTransferDisposition>(&result);
    return disposition && *disposition == VulkanUploadTransferDisposition::Complete;
}

bool completed(VulkanTextureUploadTransferParentOperationResult result) noexcept
{
    const auto* disposition = std::get_if<VulkanTextureUploadTransferDisposition>(&result);
    return disposition && *disposition == VulkanTextureUploadTransferDisposition::Complete;
}
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

    switch (mBackend.presentSampledFrame())
    {
        case LLViewerVulkanPresentOutcome::Presented:
            ++mPresentedFrameCount;
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
    VulkanBackend(LLWindowSDL& window, LLWindowSDLVulkan& owner) noexcept :
        mWindow(window),
        mOwner(owner),
        mGeneration(owner.instanceGeneration())
    {
    }

    bool initializeResidentResources() noexcept
    {
        if (!mGeneration)
        {
            return fail("missing-instance-generation");
        }

        const auto fixture        = LLRenderContract::makeTextureUploadFixture();
        mTextureDescription       = vulkanTextureUploadDestinationDescription();
        mTextureSourceDescription = vulkanTextureUploadSourceDescription(fixture.mSourceRGBA8);
        mBindingDescription       = vulkanTextureUploadSampleBindingDescription();
        mPipelineDescription      = vulkanTextureUploadSamplePipelineDescription();
        mTriangleDescription      = vulkanScreenTriangleUploadSourceDescription();

        VulkanTextureUploadDestinationRequest texture_destination_request;
        fill(texture_destination_request);
        texture_destination_request.mDescription = mTextureDescription;
        if (mGeneration->acquireTextureUploadDestinationGeneration(texture_destination_request))
        {
            return fail("texture-destination-acquire");
        }

        VulkanTextureUploadSourceRequest texture_source_request;
        fill(texture_source_request);
        texture_source_request.mDescription = mTextureSourceDescription;
        if (mGeneration->acquireTextureUploadSourceGeneration(texture_source_request))
        {
            return fail("texture-source-acquire");
        }

        VulkanTextureUploadTransferRequest texture_transfer_request;
        fill(texture_transfer_request);
        texture_transfer_request.mSourceDescription      = mTextureSourceDescription;
        texture_transfer_request.mDestinationDescription = mTextureDescription;
        if (mGeneration->acquireTextureUploadTransferGeneration(texture_transfer_request))
        {
            return fail("texture-transfer-acquire");
        }

        VulkanTextureUploadTransferOperationRequest texture_operation;
        fill(texture_operation);
        texture_operation.mSourceDescription      = mTextureSourceDescription;
        texture_operation.mDestinationDescription = mTextureDescription;
        texture_operation.mTimeoutNs              = UPLOAD_TIMEOUT_NS;
        auto texture_result                       = mGeneration->executeTextureUploadTransfer(texture_operation);
        while (mGeneration->textureUploadTransferDisposition() == VulkanTextureUploadTransferDisposition::Pending)
        {
            texture_result = mGeneration->retryTextureUploadTransferCompletion(texture_operation);
        }
        if (!completed(std::move(texture_result)) || !mGeneration->textureUploadDestinationIsResident())
        {
            return fail("texture-transfer-complete");
        }

        VulkanTextureUploadSampleBindingRequest binding_request;
        fill(binding_request);
        binding_request.mDestinationDescription = mTextureDescription;
        binding_request.mDescription            = mBindingDescription;
        if (mGeneration->acquireTextureUploadSampleBindingGeneration(binding_request))
        {
            return fail("sample-binding-acquire");
        }
        if (!mGeneration->resetTextureUploadTransferGeneration() || !mGeneration->resetTextureUploadSourceGeneration())
        {
            return fail("texture-temporary-retire");
        }

        VulkanUploadSourceRequest triangle_source_request;
        fill(triangle_source_request);
        triangle_source_request.mDescription = mTriangleDescription;
        if (mGeneration->acquireUploadSourceGeneration(triangle_source_request))
        {
            return fail("triangle-source-acquire");
        }

        VulkanUploadDestinationRequest triangle_destination_request;
        fill(triangle_destination_request);
        triangle_destination_request.mDescription = mTriangleDescription;
        if (mGeneration->acquireUploadDestinationGeneration(triangle_destination_request))
        {
            return fail("triangle-destination-acquire");
        }

        VulkanUploadTransferRequest triangle_transfer_request;
        fill(triangle_transfer_request);
        triangle_transfer_request.mDescription = mTriangleDescription;
        if (mGeneration->acquireUploadTransferGeneration(triangle_transfer_request))
        {
            return fail("triangle-transfer-acquire");
        }

        VulkanUploadTransferOperationRequest triangle_operation;
        fill(triangle_operation);
        triangle_operation.mDescription = mTriangleDescription;
        auto triangle_result            = mGeneration->executeUploadTransfer(triangle_operation);
        while (mGeneration->uploadTransferDisposition() == VulkanUploadTransferDisposition::Pending)
        {
            triangle_result = mGeneration->retryUploadTransferCompletion(triangle_operation);
        }
        if (!completed(std::move(triangle_result)) || !mGeneration->uploadDestinationIsResident())
        {
            return fail("triangle-transfer-complete");
        }
        if (!mGeneration->resetUploadTransferGeneration() || !mGeneration->resetUploadSourceGeneration())
        {
            return fail("triangle-temporary-retire");
        }

        return validationClean("resident-resource-initialize");
    }

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

    bool settleUploads() noexcept
    {
        if (!mGeneration)
        {
            return true;
        }

        VulkanTextureUploadTransferOperationRequest texture_request;
        fill(texture_request);
        texture_request.mSourceDescription      = mTextureSourceDescription;
        texture_request.mDestinationDescription = mTextureDescription;
        texture_request.mTimeoutNs              = UPLOAD_TIMEOUT_NS;
        while (mGeneration->textureUploadTransferDisposition() == VulkanTextureUploadTransferDisposition::Pending)
        {
            (void)mGeneration->retryTextureUploadTransferCompletion(texture_request);
        }

        VulkanUploadTransferOperationRequest triangle_request;
        fill(triangle_request);
        triangle_request.mDescription = mTriangleDescription;
        while (mGeneration->uploadTransferDisposition() == VulkanUploadTransferDisposition::Pending)
        {
            (void)mGeneration->retryUploadTransferCompletion(triangle_request);
        }
        return true;
    }

    LLViewerVulkanRebuildOutcome rebuild(LLViewerVulkanDrawableExtent extent) noexcept override
    {
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
        if (*outcome != VulkanSwapchainChainRebuildOutcome::Ready || !acquireSamplePipeline(extent))
        {
            fail("sample-pipeline-rebuild");
            return LLViewerVulkanRebuildOutcome::Failed;
        }
        return validationClean("swapchain-rebuild") ? LLViewerVulkanRebuildOutcome::Ready : LLViewerVulkanRebuildOutcome::Failed;
    }

    LLViewerVulkanPresentOutcome presentSampledFrame() noexcept override
    {
        const auto result = mOwner.acquireRenderPassSampleDrawToPresentSwapchainFrameSlot();
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
        fail("sample-present");
        return LLViewerVulkanPresentOutcome::Failed;
    }

    std::uint32_t validationMessageCount() const noexcept { return mGeneration ? mGeneration->validationSnapshot().mMessageCount : 0; }

    const std::string& failure() const noexcept { return mFailure; }

private:
    template<typename Request>
    void fill(Request& request) noexcept
    {
        request.mNativeWindowGeneration = mGeneration->nativeWindowGeneration();
        request.mInstanceOwnerCheck     = { this, instanceCurrent };
        request.mWindowGenerationCheck  = { this, windowCurrent };
    }

    static bool instanceCurrent(void* userdata, const VulkanInstanceGeneration& generation) noexcept
    {
        const auto& self = *static_cast<const VulkanBackend*>(userdata);
        return self.mGeneration == &generation && self.mOwner.instanceGeneration() == self.mGeneration;
    }

    static bool windowCurrent(void* userdata, std::uint64_t native_window_generation) noexcept
    {
        const auto& self = *static_cast<const VulkanBackend*>(userdata);
        return self.mWindow.isVulkanWindowGenerationCurrent(native_window_generation);
    }

    bool acquireSamplePipeline(LLViewerVulkanDrawableExtent extent) noexcept
    {
        VulkanTextureUploadSamplePipelineRequest request;
        fill(request);
        request.mDrawableExtent           = { extent.mWidth, extent.mHeight };
        request.mDestinationDescription   = mTextureDescription;
        request.mSampleBindingDescription = mBindingDescription;
        request.mDescription              = mPipelineDescription;
        return !mGeneration->acquireTextureUploadSamplePipelineGeneration(request);
    }

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

    LLWindowSDL&                                 mWindow;
    LLWindowSDLVulkan&                           mOwner;
    VulkanInstanceGeneration*                    mGeneration = nullptr;
    VulkanTextureUploadDestinationDescription    mTextureDescription;
    VulkanTextureUploadSourceDescription         mTextureSourceDescription;
    VulkanTextureUploadSampleBindingDescription  mBindingDescription;
    VulkanTextureUploadSamplePipelineDescription mPipelineDescription;
    VulkanUploadSourceDescription                mTriangleDescription;
    std::string                                  mFailure;
};

LLViewerVulkanRuntime::LLViewerVulkanRuntime() = default;

LLViewerVulkanRuntime::~LLViewerVulkanRuntime()
{
    shutdown();
}

bool LLViewerVulkanRuntime::initialize(const Params& params)
{
#if LL_SDL_WINDOW && defined(LL_VULKAN_SDL_WSI)
    if (mWindow)
    {
        return false;
    }
    mWindowSuspended = false;

    mWindow = LLWindowManager::createWindow(this,
                                            params.mTitle,
                                            params.mName,
                                            params.mX,
                                            params.mY,
                                            params.mWidth,
                                            params.mHeight,
                                            LLWindow::GraphicsAPI::Vulkan,
                                            LLWindow::FLAG_CREATE_HIDDEN,
                                            params.mFullscreen,
                                            false,
                                            params.mEnableVsync,
                                            params.mIgnorePixelDepth);
    if (!mWindow)
    {
        LL_WARNS("VulkanViewerSlice") << "failure=window-create" << LL_ENDL;
        return false;
    }

    auto&              sdl_window = *static_cast<LLWindowSDL*>(mWindow);
    LLWindowSDLVulkan* owner      = sdl_window.getVulkanOwner();
    if (!owner)
    {
        shutdown();
        return false;
    }
    mBackend    = std::make_unique<VulkanBackend>(sdl_window, *owner);
    mController = std::make_unique<LLViewerVulkanFrameController>(*mBackend);
    if (!mBackend->initializeResidentResources() || !mController->start(currentDrawableExtent()))
    {
        logSummary("initialization-failed");
        shutdown();
        return false;
    }

    mWindow->show();
    LL_INFOS("VulkanViewerSlice") << "status=ready api=vulkan visible=1" << LL_ENDL;
    return true;
#else
    (void)params;
    LL_WARNS("VulkanViewerSlice") << "failure=unsupported-platform" << LL_ENDL;
    return false;
#endif
}

bool LLViewerVulkanRuntime::tick()
{
    const bool ticked = mController && mController->tick();
    if (ticked && mController->state() == LLViewerVulkanFrameController::State::Suspended)
    {
        SDL_Delay(SUSPENDED_TICK_DELAY_MS);
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
    const bool upload_settled = !mBackend || mBackend->settleUploads();
    const bool settled        = frame_settled && upload_settled;
    if (!settled)
    {
        logSummary("device-retirement-required");
        std::terminate();
    }

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
    LLWindowManager::destroyWindow(mWindow);
    mWindow          = nullptr;
    mWindowSuspended = false;
    delete gKeyboard;
    gKeyboard = nullptr;
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

void LLViewerVulkanRuntime::handleQuit(LLWindow*)
{
    LLApp::setQuitting();
}

bool LLViewerVulkanRuntime::handleActivate(LLWindow*, bool activated)
{
    mWindowSuspended = !activated;
    if (mController)
    {
        if (activated)
        {
            requestCurrentDrawableExtent();
        }
        else
        {
            mController->requestSuspend();
        }
    }
    return true;
}

void LLViewerVulkanRuntime::handleResize(LLWindow*, S32, S32)
{
    requestCurrentDrawableExtent();
}

bool LLViewerVulkanRuntime::handleDPIChanged(LLWindow*, F32, S32, S32)
{
    requestCurrentDrawableExtent();
    return true;
}

LLViewerVulkanDrawableExtent LLViewerVulkanRuntime::currentDrawableExtent() const noexcept
{
    LLCoordWindow size;
    if (mWindowSuspended || !mWindow || !mWindow->getSize(&size) || size.mX <= 0 || size.mY <= 0)
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
    LL_INFOS("VulkanViewerSlice") << "status=" << status << " frames=" << frames << " rebuilds=" << rebuilds << " suspends=" << suspends
                                  << " validation_messages=" << validation << " gl_context=" << gl_context
                                  << " gl_manager=" << gGLManager.mInited << LL_ENDL;
}
