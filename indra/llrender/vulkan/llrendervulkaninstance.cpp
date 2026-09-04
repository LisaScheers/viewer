/**
 * @file llrendervulkaninstance.cpp
 * @brief Owned Vulkan instance generation for an authenticated native-window request.
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

#include "llrendervulkaninstance.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <limits>
#include <mutex>
#include <new>
#include <type_traits>
#include <utility>

namespace LLRenderVulkan
{
namespace
{

    constexpr std::uint32_t MAX_PROPERTY_COUNT           = 4096;
    constexpr std::uint32_t MAX_ENUMERATION_ATTEMPTS     = 4;
    constexpr std::size_t   MAX_REQUIRED_EXTENSION_COUNT = 64;
    constexpr char          VALIDATION_LAYER[]           = "VK_LAYER_KHRONOS_validation";
    constexpr char          PORTABILITY_EXTENSION[]      = "VK_KHR_portability_enumeration";
    constexpr VulkanUploadSourceDescription  SCREEN_TRIANGLE_DESCRIPTION  = vulkanScreenTriangleUploadSourceDescription();
    constexpr LLRenderContract::BufferHandle SCREEN_TRIANGLE_HANDLE       = SCREEN_TRIANGLE_DESCRIPTION.mHandle;

    enum class SwapchainFrameSlotParentOperation : std::uint8_t
    {
        ExecuteEmptySubmission,
        RetryEmptySubmissionCompletion,
        ExecuteAcquireToPresent,
        RetryPresentation,
        RetryPresentationCompletion,
        CancelAcquireToPresent,
        RetryCancellationCompletion,
        ExecuteAcquireClearToPresent,
        ExecuteAcquireRenderPassClearToPresent,
        ExecuteAcquireRenderPassDrawToPresent,
        ExecuteAcquireRenderPassDrawReadbackToPresent
    };

    enum class UploadTransferParentOperation : std::uint8_t
    {
        Execute,
        RetryCompletion
    };

    enum class TextureUploadTransferParentOperation : std::uint8_t
    {
        Execute,
        RetryCompletion
    };

    using SwapchainFrameSlotParentResult = std::variant<VulkanSwapchainFrameSlotParentOperationError, VulkanSwapchainFrameSlotDisposition,
                                                        VulkanSwapchainFrameSlotPresentationSuccess>;

    bool startsNewSwapchainFrameSlotWork(SwapchainFrameSlotParentOperation operation) noexcept
    {
        return operation == SwapchainFrameSlotParentOperation::ExecuteEmptySubmission ||
               operation == SwapchainFrameSlotParentOperation::ExecuteAcquireToPresent ||
               operation == SwapchainFrameSlotParentOperation::ExecuteAcquireClearToPresent ||
               operation == SwapchainFrameSlotParentOperation::ExecuteAcquireRenderPassClearToPresent ||
               operation == SwapchainFrameSlotParentOperation::ExecuteAcquireRenderPassDrawToPresent ||
               operation == SwapchainFrameSlotParentOperation::ExecuteAcquireRenderPassDrawReadbackToPresent;
    }

    bool validClearColor(const VulkanSwapchainFrameClearColor& clear_color) noexcept
    {
        return std::all_of(clear_color.mRgba.begin(), clear_color.mRgba.end(),
                           [](float component) { return std::isfinite(component) && component >= 0.0f && component <= 1.0f; });
    }

    bool frameSlotDispositionAllowsReset(VulkanSwapchainFrameSlotDisposition disposition) noexcept
    {
        return disposition == VulkanSwapchainFrameSlotDisposition::Reusable ||
               disposition == VulkanSwapchainFrameSlotDisposition::ResetRequired ||
               disposition == VulkanSwapchainFrameSlotDisposition::DeviceLost;
    }

    VulkanInstanceAcquireError failure(VulkanInstanceAcquireCode                          code,
                                       VkResult                                           result                   = VK_SUCCESS,
                                       std::optional<VulkanGlobalDispatchResolutionError> global_dispatch_error    = std::nullopt,
                                       std::optional<VulkanInstanceCommand>               command                  = std::nullopt,
                                       std::optional<std::size_t>                         required_extension_index = std::nullopt,
                                       std::optional<std::size_t>                         property_index           = std::nullopt,
                                       std::uint32_t                                      observed_count           = 0) noexcept
    {
        return { code, result, global_dispatch_error, command, required_extension_index, property_index, observed_count };
    }

    VulkanSurfaceAcquireError surfaceFailure(VulkanSurfaceAcquireCode            code,
                                             std::optional<VkResult>             result  = std::nullopt,
                                             std::optional<VulkanSurfaceCommand> command = std::nullopt) noexcept
    {
        return { code, result, command };
    }

    VulkanPresentationDeviceAcquireError presentationDeviceFailure(
        VulkanPresentationDeviceAcquireCode                code,
        std::optional<VulkanPhysicalDeviceResolutionError> resolution_error = std::nullopt) noexcept
    {
        return { code, resolution_error };
    }

    VulkanLogicalDeviceAcquireError logicalDeviceFailure(
        VulkanLogicalDeviceAcquireCode                    code,
        std::optional<VulkanLogicalDeviceResolutionError> resolution_error = std::nullopt) noexcept
    {
        return { code, resolution_error };
    }

    VulkanUploadSourceAcquireError uploadSourceFailure(
        VulkanUploadSourceAcquireCode                    code,
        std::optional<VulkanUploadSourceResolutionError> resolution_error = std::nullopt) noexcept
    {
        return { code, resolution_error };
    }

    VulkanTextureUploadDestinationAcquireError textureUploadDestinationFailure(
        VulkanTextureUploadDestinationAcquireCode                    code,
        std::optional<VulkanTextureUploadDestinationResolutionError> resolution_error = std::nullopt) noexcept
    {
        return { code, resolution_error };
    }

    VulkanTextureUploadSourceAcquireError textureUploadSourceFailure(
        VulkanTextureUploadSourceAcquireCode                    code,
        std::optional<VulkanTextureUploadSourceResolutionError> resolution_error = std::nullopt) noexcept
    {
        return { code, resolution_error };
    }

    VulkanTextureUploadTransferAcquireError textureUploadTransferFailure(
        VulkanTextureUploadTransferAcquireCode                    code,
        std::optional<VulkanTextureUploadTransferResolutionError> resolution_error = std::nullopt) noexcept
    {
        return { code, resolution_error };
    }

    VulkanTextureUploadSampleBindingAcquireError textureUploadSampleBindingFailure(
        VulkanTextureUploadSampleBindingAcquireCode                    code,
        std::optional<VulkanTextureUploadSampleBindingResolutionError> resolution_error = std::nullopt) noexcept
    {
        return { code, resolution_error };
    }

    VulkanTextureUploadSamplePipelineAcquireError textureUploadSamplePipelineFailure(
        VulkanTextureUploadSamplePipelineAcquireCode                    code,
        std::optional<VulkanTextureUploadSamplePipelineResolutionError> resolution_error = std::nullopt) noexcept
    {
        return { code, resolution_error };
    }

    VulkanTextureUploadTransferParentOperationError textureUploadTransferOperationFailure(
        VulkanTextureUploadTransferParentOperationCode           code,
        std::optional<VulkanTextureUploadTransferOperationError> operation_error = std::nullopt) noexcept
    {
        return { code, operation_error };
    }

    VulkanUploadDestinationAcquireError uploadDestinationFailure(
        VulkanUploadDestinationAcquireCode                    code,
        std::optional<VulkanUploadDestinationResolutionError> resolution_error = std::nullopt) noexcept
    {
        return { code, resolution_error };
    }

    VulkanUploadTransferAcquireError uploadTransferFailure(
        VulkanUploadTransferAcquireCode                    code,
        std::optional<VulkanUploadTransferResolutionError> resolution_error = std::nullopt) noexcept
    {
        return { code, resolution_error };
    }

    VulkanUploadTransferParentOperationError uploadTransferOperationFailure(
        VulkanUploadTransferParentOperationCode           code,
        std::optional<VulkanUploadTransferOperationError> operation_error = std::nullopt) noexcept
    {
        return { code, operation_error };
    }

    VulkanSwapchainConfigurationAcquireError swapchainConfigurationFailure(
        VulkanSwapchainConfigurationAcquireCode                    code,
        std::optional<VulkanSwapchainConfigurationResolutionError> resolution_error = std::nullopt) noexcept
    {
        return { code, resolution_error };
    }

    VulkanSwapchainAcquireError swapchainFailure(VulkanSwapchainAcquireCode                    code,
                                                 std::optional<VulkanSwapchainResolutionError> resolution_error = std::nullopt) noexcept
    {
        return { code, resolution_error };
    }

    VulkanSwapchainImagesAcquireError swapchainImagesFailure(
        VulkanSwapchainImagesAcquireCode                    code,
        std::optional<VulkanSwapchainImagesResolutionError> resolution_error = std::nullopt) noexcept
    {
        return { code, resolution_error };
    }

    VulkanSwapchainPresentationTargetAcquireError swapchainPresentationTargetFailure(
        VulkanSwapchainPresentationTargetAcquireCode                    code,
        std::optional<VulkanSwapchainPresentationTargetResolutionError> resolution_error = std::nullopt) noexcept
    {
        return { code, resolution_error };
    }

    VulkanSwapchainPresentationPipelineAcquireError swapchainPresentationPipelineFailure(
        VulkanSwapchainPresentationPipelineAcquireCode                    code,
        std::optional<VulkanSwapchainPresentationPipelineResolutionError> resolution_error = std::nullopt) noexcept
    {
        return { code, resolution_error };
    }

    VulkanSwapchainReadbackAcquireError swapchainReadbackFailure(
        VulkanSwapchainReadbackAcquireCode                    code,
        std::optional<VulkanSwapchainReadbackResolutionError> resolution_error = std::nullopt) noexcept
    {
        return { code, resolution_error };
    }

    VulkanSwapchainFrameSlotAcquireError swapchainFrameSlotFailure(
        VulkanSwapchainFrameSlotAcquireCode                    code,
        std::optional<VulkanSwapchainFrameSlotResolutionError> resolution_error = std::nullopt) noexcept
    {
        return { code, resolution_error };
    }

    VulkanSwapchainFrameSlotParentOperationError swapchainFrameSlotOperationFailure(
        VulkanSwapchainFrameSlotParentOperationCode           code,
        std::optional<VulkanSwapchainFrameSlotOperationError> operation_error = std::nullopt) noexcept
    {
        return { code, operation_error };
    }

    VulkanSwapchainChainRebuildError swapchainChainRebuildFailure(
        VulkanSwapchainChainRebuildCode                    code,
        VulkanSwapchainChainRebuildPhase                   phase,
        VulkanSwapchainChainRebuildChildError              child_error            = {},
        std::optional<VulkanSwapchainFrameSlotDisposition> frame_slot_disposition = std::nullopt) noexcept
    {
        return { code, phase, std::move(child_error), frame_slot_disposition };
    }

    bool current(const VulkanWindowGenerationCheck& check, std::uint64_t generation) noexcept
    {
        return generation != 0 && check.mIsCurrent && check.mIsCurrent(check.mUserdata, generation);
    }

    VulkanSurfaceAcquireResult surfaceFreshness(const VulkanSurfaceRequest&      request,
                                                const VulkanInstanceGeneration& generation,
                                                const std::uint64_t*             ownership_epoch,
                                                std::uint64_t                    expected_epoch) noexcept
    {
        const bool owner_current = request.mInstanceOwnerCheck.mIsCurrent(request.mInstanceOwnerCheck.mUserdata, generation);
        if (generation.hasSurfaceGeneration())
        {
            return surfaceFailure(VulkanSurfaceAcquireCode::SurfaceAlreadyOwned);
        }
        if (!ownership_epoch || *ownership_epoch != expected_epoch)
        {
            return surfaceFailure(VulkanSurfaceAcquireCode::InstanceNotLive);
        }
        if (!owner_current)
        {
            return surfaceFailure(VulkanSurfaceAcquireCode::StaleInstanceOwner);
        }
        const bool window_current = current(request.mWindowGenerationCheck, request.mNativeWindowGeneration);
        if (generation.hasSurfaceGeneration())
        {
            return surfaceFailure(VulkanSurfaceAcquireCode::SurfaceAlreadyOwned);
        }
        if (*ownership_epoch != expected_epoch)
        {
            return surfaceFailure(VulkanSurfaceAcquireCode::InstanceNotLive);
        }
        if (!window_current)
        {
            return surfaceFailure(VulkanSurfaceAcquireCode::StaleWindowGeneration);
        }
        return std::nullopt;
    }

    VulkanPresentationDeviceAcquireResult presentationDeviceFreshness(const VulkanPresentationDeviceRequest& request,
                                                                      const VulkanInstanceGeneration&        generation,
                                                                      const std::uint64_t* ownership_epoch,
                                                                      std::uint64_t        expected_epoch) noexcept
    {
        const bool owner_current = request.mInstanceOwnerCheck.mIsCurrent(request.mInstanceOwnerCheck.mUserdata, generation);
        if (generation.hasPresentationDeviceGeneration())
        {
            return presentationDeviceFailure(VulkanPresentationDeviceAcquireCode::PresentationDeviceAlreadyOwned);
        }
        if (!ownership_epoch || *ownership_epoch != expected_epoch)
        {
            return presentationDeviceFailure(VulkanPresentationDeviceAcquireCode::SurfaceNotLive);
        }
        if (!owner_current)
        {
            return presentationDeviceFailure(VulkanPresentationDeviceAcquireCode::StaleInstanceOwner);
        }
        const bool window_current = current(request.mWindowGenerationCheck, request.mNativeWindowGeneration);
        if (generation.hasPresentationDeviceGeneration())
        {
            return presentationDeviceFailure(VulkanPresentationDeviceAcquireCode::PresentationDeviceAlreadyOwned);
        }
        if (*ownership_epoch != expected_epoch)
        {
            return presentationDeviceFailure(VulkanPresentationDeviceAcquireCode::SurfaceNotLive);
        }
        if (!window_current)
        {
            return presentationDeviceFailure(VulkanPresentationDeviceAcquireCode::StaleWindowGeneration);
        }
        return std::nullopt;
    }

    VulkanLogicalDeviceAcquireResult logicalDeviceFreshness(const VulkanLogicalDeviceRequest& request,
                                                            const VulkanInstanceGeneration&   generation,
                                                            const std::uint64_t* ownership_epoch,
                                                            std::uint64_t        expected_epoch) noexcept
    {
        const bool owner_current = request.mInstanceOwnerCheck.mIsCurrent(request.mInstanceOwnerCheck.mUserdata, generation);
        if (generation.hasLogicalDeviceGeneration())
        {
            return logicalDeviceFailure(VulkanLogicalDeviceAcquireCode::LogicalDeviceAlreadyOwned);
        }
        if (!ownership_epoch || *ownership_epoch != expected_epoch)
        {
            return logicalDeviceFailure(VulkanLogicalDeviceAcquireCode::PresentationDeviceNotLive);
        }
        if (!owner_current)
        {
            return logicalDeviceFailure(VulkanLogicalDeviceAcquireCode::StaleInstanceOwner);
        }
        const bool window_current = current(request.mWindowGenerationCheck, request.mNativeWindowGeneration);
        if (generation.hasLogicalDeviceGeneration())
        {
            return logicalDeviceFailure(VulkanLogicalDeviceAcquireCode::LogicalDeviceAlreadyOwned);
        }
        if (*ownership_epoch != expected_epoch)
        {
            return logicalDeviceFailure(VulkanLogicalDeviceAcquireCode::PresentationDeviceNotLive);
        }
        if (!window_current)
        {
            return logicalDeviceFailure(VulkanLogicalDeviceAcquireCode::StaleWindowGeneration);
        }
        return std::nullopt;
    }

    VulkanUploadSourceAcquireResult uploadSourceFreshness(const VulkanUploadSourceRequest& request,
                                                          const VulkanInstanceGeneration&  generation,
                                                          const std::uint64_t*             ownership_epoch,
                                                          std::uint64_t                    expected_epoch) noexcept
    {
        const bool owner_current = request.mInstanceOwnerCheck.mIsCurrent(request.mInstanceOwnerCheck.mUserdata, generation);
        if (generation.hasUploadSourceGeneration())
        {
            return uploadSourceFailure(VulkanUploadSourceAcquireCode::UploadSourceAlreadyOwned);
        }
        if (!ownership_epoch || *ownership_epoch != expected_epoch)
        {
            return uploadSourceFailure(VulkanUploadSourceAcquireCode::LogicalDeviceNotLive);
        }
        if (!owner_current)
        {
            return uploadSourceFailure(VulkanUploadSourceAcquireCode::StaleInstanceOwner);
        }
        const bool window_current = current(request.mWindowGenerationCheck, request.mNativeWindowGeneration);
        if (generation.hasUploadSourceGeneration())
        {
            return uploadSourceFailure(VulkanUploadSourceAcquireCode::UploadSourceAlreadyOwned);
        }
        if (*ownership_epoch != expected_epoch)
        {
            return uploadSourceFailure(VulkanUploadSourceAcquireCode::LogicalDeviceNotLive);
        }
        if (!window_current)
        {
            return uploadSourceFailure(VulkanUploadSourceAcquireCode::StaleWindowGeneration);
        }
        return std::nullopt;
    }

    VulkanTextureUploadDestinationAcquireResult textureUploadDestinationFreshness(const VulkanTextureUploadDestinationRequest& request,
                                                                                  const VulkanInstanceGeneration&              generation,
                                                                                  const std::uint64_t* ownership_epoch,
                                                                                  std::uint64_t        expected_epoch) noexcept
    {
        const bool owner_current = request.mInstanceOwnerCheck.mIsCurrent(request.mInstanceOwnerCheck.mUserdata, generation);
        if (generation.hasTextureUploadDestinationGeneration())
        {
            return textureUploadDestinationFailure(VulkanTextureUploadDestinationAcquireCode::TextureUploadDestinationAlreadyOwned);
        }
        if (!ownership_epoch || *ownership_epoch != expected_epoch)
        {
            return textureUploadDestinationFailure(VulkanTextureUploadDestinationAcquireCode::LogicalDeviceNotLive);
        }
        if (!owner_current)
        {
            return textureUploadDestinationFailure(VulkanTextureUploadDestinationAcquireCode::StaleInstanceOwner);
        }
        const bool window_current = current(request.mWindowGenerationCheck, request.mNativeWindowGeneration);
        if (generation.hasTextureUploadDestinationGeneration())
        {
            return textureUploadDestinationFailure(VulkanTextureUploadDestinationAcquireCode::TextureUploadDestinationAlreadyOwned);
        }
        if (*ownership_epoch != expected_epoch)
        {
            return textureUploadDestinationFailure(VulkanTextureUploadDestinationAcquireCode::LogicalDeviceNotLive);
        }
        if (!window_current)
        {
            return textureUploadDestinationFailure(VulkanTextureUploadDestinationAcquireCode::StaleWindowGeneration);
        }
        return std::nullopt;
    }

    VulkanTextureUploadSourceAcquireResult textureUploadSourceFreshness(const VulkanTextureUploadSourceRequest& request,
                                                                        const VulkanInstanceGeneration&         generation,
                                                                        const std::uint64_t*                    ownership_epoch,
                                                                        std::uint64_t                           expected_epoch) noexcept
    {
        const bool owner_current = request.mInstanceOwnerCheck.mIsCurrent(request.mInstanceOwnerCheck.mUserdata, generation);
        if (generation.hasTextureUploadSourceGeneration())
        {
            return textureUploadSourceFailure(VulkanTextureUploadSourceAcquireCode::TextureUploadSourceAlreadyOwned);
        }
        if (!ownership_epoch || *ownership_epoch != expected_epoch)
        {
            return textureUploadSourceFailure(VulkanTextureUploadSourceAcquireCode::TextureUploadDestinationNotLive);
        }
        if (!owner_current)
        {
            return textureUploadSourceFailure(VulkanTextureUploadSourceAcquireCode::StaleInstanceOwner);
        }
        const bool window_current = current(request.mWindowGenerationCheck, request.mNativeWindowGeneration);
        if (generation.hasTextureUploadSourceGeneration())
        {
            return textureUploadSourceFailure(VulkanTextureUploadSourceAcquireCode::TextureUploadSourceAlreadyOwned);
        }
        if (*ownership_epoch != expected_epoch)
        {
            return textureUploadSourceFailure(VulkanTextureUploadSourceAcquireCode::TextureUploadDestinationNotLive);
        }
        if (!window_current)
        {
            return textureUploadSourceFailure(VulkanTextureUploadSourceAcquireCode::StaleWindowGeneration);
        }
        return std::nullopt;
    }

    VulkanTextureUploadTransferAcquireResult textureUploadTransferFreshness(const VulkanTextureUploadTransferRequest& request,
                                                                            const VulkanInstanceGeneration&           generation,
                                                                            const std::uint64_t*                      epoch,
                                                                            std::uint64_t expected_epoch) noexcept
    {
        if (!request.mInstanceOwnerCheck.mIsCurrent(request.mInstanceOwnerCheck.mUserdata, generation))
        {
            return textureUploadTransferFailure(VulkanTextureUploadTransferAcquireCode::StaleInstanceOwner);
        }
        if (generation.hasTextureUploadTransferGeneration())
        {
            return textureUploadTransferFailure(VulkanTextureUploadTransferAcquireCode::TextureUploadTransferAlreadyOwned);
        }
        if (!epoch || *epoch != expected_epoch)
        {
            return textureUploadTransferFailure(VulkanTextureUploadTransferAcquireCode::TextureUploadDestinationNotLive);
        }
        if (!generation.hasTextureUploadSourceGeneration())
        {
            return textureUploadTransferFailure(VulkanTextureUploadTransferAcquireCode::TextureUploadSourceNotLive);
        }
        if (!generation.hasTextureUploadDestinationGeneration())
        {
            return textureUploadTransferFailure(VulkanTextureUploadTransferAcquireCode::TextureUploadDestinationNotLive);
        }
        if (!current(request.mWindowGenerationCheck, request.mNativeWindowGeneration))
        {
            return textureUploadTransferFailure(VulkanTextureUploadTransferAcquireCode::StaleWindowGeneration);
        }
        if (generation.hasTextureUploadTransferGeneration())
        {
            return textureUploadTransferFailure(VulkanTextureUploadTransferAcquireCode::TextureUploadTransferAlreadyOwned);
        }
        if (*epoch != expected_epoch)
        {
            return textureUploadTransferFailure(VulkanTextureUploadTransferAcquireCode::TextureUploadDestinationNotLive);
        }
        if (!generation.hasTextureUploadSourceGeneration())
        {
            return textureUploadTransferFailure(VulkanTextureUploadTransferAcquireCode::TextureUploadSourceNotLive);
        }
        if (!generation.hasTextureUploadDestinationGeneration())
        {
            return textureUploadTransferFailure(VulkanTextureUploadTransferAcquireCode::TextureUploadDestinationNotLive);
        }
        return std::nullopt;
    }

    VulkanTextureUploadSampleBindingAcquireResult textureUploadSampleBindingFreshness(
        const VulkanTextureUploadSampleBindingRequest& request,
        const VulkanTextureUploadSampleBindingRequest& snapshot,
        const VulkanInstanceGeneration&                generation,
        const std::uint64_t*                           epoch,
        std::uint64_t                                  expected_epoch) noexcept
    {
        const auto request_shape_error = [&]() noexcept -> VulkanTextureUploadSampleBindingAcquireResult
        {
            if (request.mInstanceOwnerCheck.mUserdata != snapshot.mInstanceOwnerCheck.mUserdata ||
                request.mInstanceOwnerCheck.mIsCurrent != snapshot.mInstanceOwnerCheck.mIsCurrent)
            {
                return textureUploadSampleBindingFailure(VulkanTextureUploadSampleBindingAcquireCode::StaleInstanceOwner);
            }
            if (request.mWindowGenerationCheck.mUserdata != snapshot.mWindowGenerationCheck.mUserdata ||
                request.mWindowGenerationCheck.mIsCurrent != snapshot.mWindowGenerationCheck.mIsCurrent ||
                request.mNativeWindowGeneration != snapshot.mNativeWindowGeneration)
            {
                return textureUploadSampleBindingFailure(VulkanTextureUploadSampleBindingAcquireCode::StaleWindowGeneration);
            }
            if (request.mDestinationDescription != snapshot.mDestinationDescription || request.mDescription != snapshot.mDescription)
            {
                return textureUploadSampleBindingFailure(VulkanTextureUploadSampleBindingAcquireCode::TextureUploadDestinationNotLive);
            }
            return std::nullopt;
        };

        if (auto error = request_shape_error())
        {
            return error;
        }
        const bool owner_current = snapshot.mInstanceOwnerCheck.mIsCurrent(snapshot.mInstanceOwnerCheck.mUserdata, generation);
        if (auto error = request_shape_error())
        {
            return error;
        }
        if (!owner_current)
        {
            return textureUploadSampleBindingFailure(VulkanTextureUploadSampleBindingAcquireCode::StaleInstanceOwner);
        }
        if (generation.hasTextureUploadSampleBindingGeneration())
        {
            return textureUploadSampleBindingFailure(VulkanTextureUploadSampleBindingAcquireCode::TextureUploadSampleBindingAlreadyOwned);
        }
        if (!epoch || *epoch != expected_epoch || !generation.hasTextureUploadDestinationGeneration())
        {
            return textureUploadSampleBindingFailure(VulkanTextureUploadSampleBindingAcquireCode::TextureUploadDestinationNotLive);
        }
        const bool window_current = current(snapshot.mWindowGenerationCheck, snapshot.mNativeWindowGeneration);
        if (auto error = request_shape_error())
        {
            return error;
        }
        if (!window_current)
        {
            return textureUploadSampleBindingFailure(VulkanTextureUploadSampleBindingAcquireCode::StaleWindowGeneration);
        }
        if (generation.hasTextureUploadSampleBindingGeneration())
        {
            return textureUploadSampleBindingFailure(VulkanTextureUploadSampleBindingAcquireCode::TextureUploadSampleBindingAlreadyOwned);
        }
        if (*epoch != expected_epoch || !generation.hasTextureUploadDestinationGeneration())
        {
            return textureUploadSampleBindingFailure(VulkanTextureUploadSampleBindingAcquireCode::TextureUploadDestinationNotLive);
        }
        return std::nullopt;
    }

    VulkanTextureUploadSamplePipelineAcquireResult textureUploadSamplePipelineFreshness(
        const VulkanTextureUploadSamplePipelineRequest& request,
        const VulkanTextureUploadSamplePipelineRequest& snapshot,
        const VulkanInstanceGeneration&                 generation,
        const std::uint64_t*                            ownership_epoch,
        std::uint64_t                                   expected_ownership_epoch,
        const std::uint64_t*                            destination_epoch,
        std::uint64_t                                   expected_destination_epoch,
        const std::uint64_t*                            binding_epoch,
        std::uint64_t                                   expected_binding_epoch,
        const std::uint64_t*                            target_epoch,
        std::uint64_t                                   expected_target_epoch,
        const std::uint64_t*                            pipeline_epoch,
        std::uint64_t                                   expected_pipeline_epoch) noexcept
    {
        const auto request_shape_error = [&]() noexcept -> VulkanTextureUploadSamplePipelineAcquireResult
        {
            if (request.mInstanceOwnerCheck.mUserdata != snapshot.mInstanceOwnerCheck.mUserdata ||
                request.mInstanceOwnerCheck.mIsCurrent != snapshot.mInstanceOwnerCheck.mIsCurrent)
            {
                return textureUploadSamplePipelineFailure(VulkanTextureUploadSamplePipelineAcquireCode::StaleInstanceOwner);
            }
            if (request.mWindowGenerationCheck.mUserdata != snapshot.mWindowGenerationCheck.mUserdata ||
                request.mWindowGenerationCheck.mIsCurrent != snapshot.mWindowGenerationCheck.mIsCurrent ||
                request.mNativeWindowGeneration != snapshot.mNativeWindowGeneration ||
                request.mDrawableExtent.width != snapshot.mDrawableExtent.width ||
                request.mDrawableExtent.height != snapshot.mDrawableExtent.height)
            {
                return textureUploadSamplePipelineFailure(VulkanTextureUploadSamplePipelineAcquireCode::StaleWindowGeneration);
            }
            if (request.mDestinationDescription != snapshot.mDestinationDescription)
            {
                return textureUploadSamplePipelineFailure(VulkanTextureUploadSamplePipelineAcquireCode::TextureUploadDestinationNotLive);
            }
            if (request.mSampleBindingDescription != snapshot.mSampleBindingDescription || request.mDescription != snapshot.mDescription)
            {
                return textureUploadSamplePipelineFailure(VulkanTextureUploadSamplePipelineAcquireCode::TextureUploadSampleBindingNotLive);
            }
            return std::nullopt;
        };

        if (auto error = request_shape_error())
        {
            return error;
        }
        const bool owner_current = snapshot.mInstanceOwnerCheck.mIsCurrent(snapshot.mInstanceOwnerCheck.mUserdata, generation);
        if (auto error = request_shape_error())
        {
            return error;
        }
        if (!owner_current)
        {
            return textureUploadSamplePipelineFailure(VulkanTextureUploadSamplePipelineAcquireCode::StaleInstanceOwner);
        }
        if (generation.hasTextureUploadSamplePipelineGeneration())
        {
            return textureUploadSamplePipelineFailure(
                VulkanTextureUploadSamplePipelineAcquireCode::TextureUploadSamplePipelineAlreadyOwned);
        }
        if (!pipeline_epoch || *pipeline_epoch != expected_pipeline_epoch)
        {
            return textureUploadSamplePipelineFailure(
                VulkanTextureUploadSamplePipelineAcquireCode::TextureUploadSamplePipelineAlreadyOwned);
        }
        if (!destination_epoch || *destination_epoch != expected_destination_epoch || !generation.hasTextureUploadDestinationGeneration())
        {
            return textureUploadSamplePipelineFailure(VulkanTextureUploadSamplePipelineAcquireCode::TextureUploadDestinationNotLive);
        }
        if (!binding_epoch || *binding_epoch != expected_binding_epoch || !generation.hasTextureUploadSampleBindingGeneration())
        {
            return textureUploadSamplePipelineFailure(VulkanTextureUploadSamplePipelineAcquireCode::TextureUploadSampleBindingNotLive);
        }
        if (!target_epoch || *target_epoch != expected_target_epoch || !generation.hasSwapchainPresentationTargetGeneration() ||
            !ownership_epoch || *ownership_epoch != expected_ownership_epoch)
        {
            return textureUploadSamplePipelineFailure(VulkanTextureUploadSamplePipelineAcquireCode::SwapchainPresentationTargetNotLive);
        }
        const bool window_current = current(snapshot.mWindowGenerationCheck, snapshot.mNativeWindowGeneration);
        if (auto error = request_shape_error())
        {
            return error;
        }
        if (!window_current)
        {
            return textureUploadSamplePipelineFailure(VulkanTextureUploadSamplePipelineAcquireCode::StaleWindowGeneration);
        }
        if (generation.hasTextureUploadSamplePipelineGeneration())
        {
            return textureUploadSamplePipelineFailure(
                VulkanTextureUploadSamplePipelineAcquireCode::TextureUploadSamplePipelineAlreadyOwned);
        }
        if (*pipeline_epoch != expected_pipeline_epoch)
        {
            return textureUploadSamplePipelineFailure(
                VulkanTextureUploadSamplePipelineAcquireCode::TextureUploadSamplePipelineAlreadyOwned);
        }
        if (*destination_epoch != expected_destination_epoch || !generation.hasTextureUploadDestinationGeneration())
        {
            return textureUploadSamplePipelineFailure(VulkanTextureUploadSamplePipelineAcquireCode::TextureUploadDestinationNotLive);
        }
        if (*binding_epoch != expected_binding_epoch || !generation.hasTextureUploadSampleBindingGeneration())
        {
            return textureUploadSamplePipelineFailure(VulkanTextureUploadSamplePipelineAcquireCode::TextureUploadSampleBindingNotLive);
        }
        if (*target_epoch != expected_target_epoch || !generation.hasSwapchainPresentationTargetGeneration() ||
            *ownership_epoch != expected_ownership_epoch)
        {
            return textureUploadSamplePipelineFailure(VulkanTextureUploadSamplePipelineAcquireCode::SwapchainPresentationTargetNotLive);
        }
        return std::nullopt;
    }

    std::optional<VulkanTextureUploadTransferParentOperationError> textureUploadTransferOperationFreshness(
        const VulkanTextureUploadTransferOperationRequest& request, const VulkanInstanceGeneration& generation) noexcept
    {
        if (!request.mInstanceOwnerCheck.mIsCurrent(request.mInstanceOwnerCheck.mUserdata, generation))
        {
            return textureUploadTransferOperationFailure(VulkanTextureUploadTransferParentOperationCode::StaleInstanceOwner);
        }
        if (!current(request.mWindowGenerationCheck, request.mNativeWindowGeneration))
        {
            return textureUploadTransferOperationFailure(VulkanTextureUploadTransferParentOperationCode::StaleWindowGeneration);
        }
        return std::nullopt;
    }

    VulkanUploadDestinationAcquireResult uploadDestinationFreshness(const VulkanUploadDestinationRequest& request,
                                                                    const VulkanInstanceGeneration&       generation,
                                                                    const std::uint64_t*                  ownership_epoch,
                                                                    std::uint64_t                         expected_epoch) noexcept
    {
        const bool owner_current = request.mInstanceOwnerCheck.mIsCurrent(request.mInstanceOwnerCheck.mUserdata, generation);
        if (generation.hasUploadDestinationGeneration())
        {
            return uploadDestinationFailure(VulkanUploadDestinationAcquireCode::UploadDestinationAlreadyOwned);
        }
        if (!ownership_epoch || *ownership_epoch != expected_epoch)
        {
            return uploadDestinationFailure(VulkanUploadDestinationAcquireCode::LogicalDeviceNotLive);
        }
        if (!owner_current)
        {
            return uploadDestinationFailure(VulkanUploadDestinationAcquireCode::StaleInstanceOwner);
        }
        const bool window_current = current(request.mWindowGenerationCheck, request.mNativeWindowGeneration);
        if (generation.hasUploadDestinationGeneration())
        {
            return uploadDestinationFailure(VulkanUploadDestinationAcquireCode::UploadDestinationAlreadyOwned);
        }
        if (*ownership_epoch != expected_epoch)
        {
            return uploadDestinationFailure(VulkanUploadDestinationAcquireCode::LogicalDeviceNotLive);
        }
        if (!window_current)
        {
            return uploadDestinationFailure(VulkanUploadDestinationAcquireCode::StaleWindowGeneration);
        }
        return std::nullopt;
    }

    VulkanUploadTransferAcquireResult uploadTransferFreshness(const VulkanUploadTransferRequest& request,
                                                              const VulkanInstanceGeneration&    generation,
                                                              const std::uint64_t*               ownership_epoch,
                                                              std::uint64_t                      expected_epoch) noexcept
    {
        const bool owner_current = request.mInstanceOwnerCheck.mIsCurrent(request.mInstanceOwnerCheck.mUserdata, generation);
        if (generation.hasUploadTransferGeneration())
        {
            return uploadTransferFailure(VulkanUploadTransferAcquireCode::UploadTransferAlreadyOwned);
        }
        if (!ownership_epoch || *ownership_epoch != expected_epoch)
        {
            return uploadTransferFailure(VulkanUploadTransferAcquireCode::LogicalDeviceNotLive);
        }
        if (!owner_current)
        {
            return uploadTransferFailure(VulkanUploadTransferAcquireCode::StaleInstanceOwner);
        }
        const bool window_current = current(request.mWindowGenerationCheck, request.mNativeWindowGeneration);
        if (generation.hasUploadTransferGeneration())
        {
            return uploadTransferFailure(VulkanUploadTransferAcquireCode::UploadTransferAlreadyOwned);
        }
        if (*ownership_epoch != expected_epoch)
        {
            return uploadTransferFailure(VulkanUploadTransferAcquireCode::LogicalDeviceNotLive);
        }
        if (!window_current)
        {
            return uploadTransferFailure(VulkanUploadTransferAcquireCode::StaleWindowGeneration);
        }
        return std::nullopt;
    }

    std::optional<VulkanUploadTransferParentOperationError> uploadTransferOperationFreshness(
        const VulkanUploadTransferOperationRequest& request,
        const VulkanInstanceGeneration&             generation) noexcept
    {
        if (!request.mInstanceOwnerCheck.mIsCurrent(request.mInstanceOwnerCheck.mUserdata, generation))
        {
            return uploadTransferOperationFailure(VulkanUploadTransferParentOperationCode::StaleInstanceOwner);
        }
        if (!current(request.mWindowGenerationCheck, request.mNativeWindowGeneration))
        {
            return uploadTransferOperationFailure(VulkanUploadTransferParentOperationCode::StaleWindowGeneration);
        }
        return std::nullopt;
    }

    VulkanSwapchainConfigurationAcquireResult swapchainConfigurationFreshness(const VulkanSwapchainConfigurationRequest& request,
                                                                              const VulkanInstanceGeneration& generation,
                                                                              const std::uint64_t* ownership_epoch,
                                                                              std::uint64_t expected_epoch) noexcept
    {
        const bool owner_current = request.mInstanceOwnerCheck.mIsCurrent(request.mInstanceOwnerCheck.mUserdata, generation);
        if (generation.hasSwapchainConfigurationGeneration())
        {
            return swapchainConfigurationFailure(
                VulkanSwapchainConfigurationAcquireCode::SwapchainConfigurationAlreadyOwned);
        }
        if (!ownership_epoch || *ownership_epoch != expected_epoch)
        {
            return swapchainConfigurationFailure(VulkanSwapchainConfigurationAcquireCode::LogicalDeviceNotLive);
        }
        if (!owner_current)
        {
            return swapchainConfigurationFailure(VulkanSwapchainConfigurationAcquireCode::StaleInstanceOwner);
        }
        const bool window_current = current(request.mWindowGenerationCheck, request.mNativeWindowGeneration);
        if (generation.hasSwapchainConfigurationGeneration())
        {
            return swapchainConfigurationFailure(
                VulkanSwapchainConfigurationAcquireCode::SwapchainConfigurationAlreadyOwned);
        }
        if (*ownership_epoch != expected_epoch)
        {
            return swapchainConfigurationFailure(VulkanSwapchainConfigurationAcquireCode::LogicalDeviceNotLive);
        }
        if (!window_current)
        {
            return swapchainConfigurationFailure(VulkanSwapchainConfigurationAcquireCode::StaleWindowGeneration);
        }
        return std::nullopt;
    }

    VulkanSwapchainAcquireResult swapchainFreshness(const VulkanSwapchainRequest&   request,
                                                    const VulkanInstanceGeneration& generation,
                                                    const std::uint64_t* ownership_epoch,
                                                    std::uint64_t        expected_epoch) noexcept
    {
        const bool owner_current = request.mInstanceOwnerCheck.mIsCurrent(request.mInstanceOwnerCheck.mUserdata, generation);
        if (generation.hasSwapchainGeneration())
        {
            return swapchainFailure(VulkanSwapchainAcquireCode::SwapchainAlreadyOwned);
        }
        if (!ownership_epoch || *ownership_epoch != expected_epoch)
        {
            return swapchainFailure(VulkanSwapchainAcquireCode::SwapchainConfigurationNotLive);
        }
        if (!owner_current)
        {
            return swapchainFailure(VulkanSwapchainAcquireCode::StaleInstanceOwner);
        }
        const bool window_current = current(request.mWindowGenerationCheck, request.mNativeWindowGeneration);
        if (generation.hasSwapchainGeneration())
        {
            return swapchainFailure(VulkanSwapchainAcquireCode::SwapchainAlreadyOwned);
        }
        if (*ownership_epoch != expected_epoch)
        {
            return swapchainFailure(VulkanSwapchainAcquireCode::SwapchainConfigurationNotLive);
        }
        if (!window_current)
        {
            return swapchainFailure(VulkanSwapchainAcquireCode::StaleWindowGeneration);
        }
        return std::nullopt;
    }

    VulkanSwapchainImagesAcquireResult swapchainImagesFreshness(const VulkanSwapchainImagesRequest& request,
                                                                const VulkanInstanceGeneration&     generation,
                                                                const std::uint64_t* ownership_epoch,
                                                                std::uint64_t        expected_epoch) noexcept
    {
        const bool owner_current = request.mInstanceOwnerCheck.mIsCurrent(request.mInstanceOwnerCheck.mUserdata, generation);
        if (generation.hasSwapchainImagesGeneration())
        {
            return swapchainImagesFailure(VulkanSwapchainImagesAcquireCode::SwapchainImagesAlreadyOwned);
        }
        if (!ownership_epoch || *ownership_epoch != expected_epoch)
        {
            return swapchainImagesFailure(VulkanSwapchainImagesAcquireCode::SwapchainNotLive);
        }
        if (!owner_current)
        {
            return swapchainImagesFailure(VulkanSwapchainImagesAcquireCode::StaleInstanceOwner);
        }
        const bool window_current = current(request.mWindowGenerationCheck, request.mNativeWindowGeneration);
        if (generation.hasSwapchainImagesGeneration())
        {
            return swapchainImagesFailure(VulkanSwapchainImagesAcquireCode::SwapchainImagesAlreadyOwned);
        }
        if (*ownership_epoch != expected_epoch)
        {
            return swapchainImagesFailure(VulkanSwapchainImagesAcquireCode::SwapchainNotLive);
        }
        if (!window_current)
        {
            return swapchainImagesFailure(VulkanSwapchainImagesAcquireCode::StaleWindowGeneration);
        }
        return std::nullopt;
    }

    VulkanSwapchainPresentationTargetAcquireResult swapchainPresentationTargetFreshness(
        const VulkanSwapchainPresentationTargetRequest& request,
        const VulkanInstanceGeneration&                generation,
        const std::uint64_t*                            ownership_epoch,
        std::uint64_t                                   expected_epoch) noexcept
    {
        const bool owner_current = request.mInstanceOwnerCheck.mIsCurrent(request.mInstanceOwnerCheck.mUserdata, generation);
        if (generation.hasSwapchainPresentationTargetGeneration())
        {
            return swapchainPresentationTargetFailure(
                VulkanSwapchainPresentationTargetAcquireCode::SwapchainPresentationTargetAlreadyOwned);
        }
        if (!ownership_epoch || *ownership_epoch != expected_epoch)
        {
            return swapchainPresentationTargetFailure(VulkanSwapchainPresentationTargetAcquireCode::SwapchainImagesNotLive);
        }
        if (!owner_current)
        {
            return swapchainPresentationTargetFailure(VulkanSwapchainPresentationTargetAcquireCode::StaleInstanceOwner);
        }
        const bool window_current = current(request.mWindowGenerationCheck, request.mNativeWindowGeneration);
        if (generation.hasSwapchainPresentationTargetGeneration())
        {
            return swapchainPresentationTargetFailure(
                VulkanSwapchainPresentationTargetAcquireCode::SwapchainPresentationTargetAlreadyOwned);
        }
        if (*ownership_epoch != expected_epoch)
        {
            return swapchainPresentationTargetFailure(VulkanSwapchainPresentationTargetAcquireCode::SwapchainImagesNotLive);
        }
        if (!window_current)
        {
            return swapchainPresentationTargetFailure(VulkanSwapchainPresentationTargetAcquireCode::StaleWindowGeneration);
        }
        return std::nullopt;
    }

    VulkanSwapchainPresentationPipelineAcquireResult swapchainPresentationPipelineFreshness(
        const VulkanSwapchainPresentationPipelineRequest& request,
        const VulkanInstanceGeneration&                  generation,
        const std::uint64_t*                              ownership_epoch,
        std::uint64_t                                     expected_epoch) noexcept
    {
        const bool owner_current = request.mInstanceOwnerCheck.mIsCurrent(request.mInstanceOwnerCheck.mUserdata, generation);
        if (generation.hasSwapchainPresentationPipelineGeneration())
        {
            return swapchainPresentationPipelineFailure(
                VulkanSwapchainPresentationPipelineAcquireCode::SwapchainPresentationPipelineAlreadyOwned);
        }
        if (!ownership_epoch || *ownership_epoch != expected_epoch)
        {
            return swapchainPresentationPipelineFailure(
                VulkanSwapchainPresentationPipelineAcquireCode::SwapchainPresentationTargetNotLive);
        }
        if (!owner_current)
        {
            return swapchainPresentationPipelineFailure(
                VulkanSwapchainPresentationPipelineAcquireCode::StaleInstanceOwner);
        }
        const bool window_current = current(request.mWindowGenerationCheck, request.mNativeWindowGeneration);
        if (generation.hasSwapchainPresentationPipelineGeneration())
        {
            return swapchainPresentationPipelineFailure(
                VulkanSwapchainPresentationPipelineAcquireCode::SwapchainPresentationPipelineAlreadyOwned);
        }
        if (*ownership_epoch != expected_epoch)
        {
            return swapchainPresentationPipelineFailure(VulkanSwapchainPresentationPipelineAcquireCode::SwapchainPresentationTargetNotLive);
        }
        if (!window_current)
        {
            return swapchainPresentationPipelineFailure(VulkanSwapchainPresentationPipelineAcquireCode::StaleWindowGeneration);
        }
        return std::nullopt;
    }

    VulkanSwapchainReadbackAcquireResult swapchainReadbackFreshness(const VulkanSwapchainReadbackRequest& request,
                                                                    const VulkanInstanceGeneration&       generation,
                                                                    const std::uint64_t*                  ownership_epoch,
                                                                    std::uint64_t                         expected_epoch) noexcept
    {
        const bool owner_current = request.mInstanceOwnerCheck.mIsCurrent(request.mInstanceOwnerCheck.mUserdata, generation);
        if (generation.hasSwapchainReadbackGeneration())
        {
            return swapchainReadbackFailure(VulkanSwapchainReadbackAcquireCode::SwapchainReadbackAlreadyOwned);
        }
        if (!ownership_epoch || *ownership_epoch != expected_epoch)
        {
            return swapchainReadbackFailure(VulkanSwapchainReadbackAcquireCode::SwapchainImagesNotLive);
        }
        if (!owner_current)
        {
            return swapchainReadbackFailure(VulkanSwapchainReadbackAcquireCode::StaleInstanceOwner);
        }
        const bool window_current = current(request.mWindowGenerationCheck, request.mNativeWindowGeneration);
        if (generation.hasSwapchainReadbackGeneration())
        {
            return swapchainReadbackFailure(VulkanSwapchainReadbackAcquireCode::SwapchainReadbackAlreadyOwned);
        }
        if (*ownership_epoch != expected_epoch)
        {
            return swapchainReadbackFailure(VulkanSwapchainReadbackAcquireCode::SwapchainImagesNotLive);
        }
        if (!window_current)
        {
            return swapchainReadbackFailure(VulkanSwapchainReadbackAcquireCode::StaleWindowGeneration);
        }
        return std::nullopt;
    }

    VulkanSwapchainFrameSlotAcquireResult swapchainFrameSlotFreshness(const VulkanSwapchainFrameSlotRequest& request,
                                                                      const VulkanInstanceGeneration&        generation,
                                                                      const std::uint64_t* ownership_epoch,
                                                                      std::uint64_t        expected_epoch) noexcept
    {
        const bool owner_current = request.mInstanceOwnerCheck.mIsCurrent(request.mInstanceOwnerCheck.mUserdata, generation);
        if (generation.hasSwapchainFrameSlotGeneration())
        {
            return swapchainFrameSlotFailure(VulkanSwapchainFrameSlotAcquireCode::SwapchainFrameSlotAlreadyOwned);
        }
        if (!ownership_epoch || *ownership_epoch != expected_epoch)
        {
            return swapchainFrameSlotFailure(VulkanSwapchainFrameSlotAcquireCode::SwapchainImagesNotLive);
        }
        if (!owner_current)
        {
            return swapchainFrameSlotFailure(VulkanSwapchainFrameSlotAcquireCode::StaleInstanceOwner);
        }
        const bool window_current = current(request.mWindowGenerationCheck, request.mNativeWindowGeneration);
        if (generation.hasSwapchainFrameSlotGeneration())
        {
            return swapchainFrameSlotFailure(VulkanSwapchainFrameSlotAcquireCode::SwapchainFrameSlotAlreadyOwned);
        }
        if (*ownership_epoch != expected_epoch)
        {
            return swapchainFrameSlotFailure(VulkanSwapchainFrameSlotAcquireCode::SwapchainImagesNotLive);
        }
        if (!window_current)
        {
            return swapchainFrameSlotFailure(VulkanSwapchainFrameSlotAcquireCode::StaleWindowGeneration);
        }
        return std::nullopt;
    }

    std::optional<VulkanSwapchainFrameSlotParentOperationError> swapchainFrameSlotOperationFreshness(
        const VulkanSwapchainFrameSlotOperationRequest& request,
        const VulkanInstanceGeneration&                 generation) noexcept
    {
        if (!request.mInstanceOwnerCheck.mIsCurrent(request.mInstanceOwnerCheck.mUserdata, generation))
        {
            return swapchainFrameSlotOperationFailure(VulkanSwapchainFrameSlotParentOperationCode::StaleInstanceOwner);
        }
        if (!current(request.mWindowGenerationCheck, request.mNativeWindowGeneration))
        {
            return swapchainFrameSlotOperationFailure(VulkanSwapchainFrameSlotParentOperationCode::StaleWindowGeneration);
        }
        return std::nullopt;
    }

    template<std::size_t Size>
    std::optional<std::string_view> boundedName(const char (&name)[Size]) noexcept
    {
        const void* terminator = std::memchr(name, '\0', Size);
        if (!terminator)
        {
            return std::nullopt;
        }
        const auto* end = static_cast<const char*>(terminator);
        return std::string_view(name, static_cast<std::size_t>(end - name));
    }

    template<typename Property, typename Query>
    bool enumerateProperties(Query&&                                    query,
                             VulkanInstanceAcquireCode                  failure_code,
                             VulkanInstanceAcquireCode                  count_code,
                             std::vector<Property>&                     properties,
                             VulkanInstanceAcquireError&                error,
                             VulkanInstanceDetail::AllocationCheckpoint allocation_checkpoint)
    {
        for (std::uint32_t attempt = 0; attempt < MAX_ENUMERATION_ATTEMPTS; ++attempt)
        {
            std::uint32_t  count        = 0;
            const VkResult count_result = query(&count, nullptr);
            if (count_result != VK_SUCCESS && count_result != VK_INCOMPLETE)
            {
                error = failure(failure_code, count_result);
                return false;
            }
            if (count_result == VK_INCOMPLETE)
            {
                continue;
            }
            if (count > MAX_PROPERTY_COUNT)
            {
                error = failure(count_code, VK_SUCCESS, std::nullopt, std::nullopt, std::nullopt, std::nullopt, count);
                return false;
            }
            if (count == 0)
            {
                properties.clear();
                return true;
            }

            if (allocation_checkpoint)
            {
                allocation_checkpoint();
            }
            properties.assign(count, Property{});

            std::uint32_t  written       = count;
            const VkResult values_result = query(&written, properties.data());
            if (written > MAX_PROPERTY_COUNT || written > count)
            {
                error = failure(count_code, values_result, std::nullopt, std::nullopt, std::nullopt, std::nullopt, written);
                return false;
            }
            if (values_result == VK_SUCCESS)
            {
                properties.resize(written);
                return true;
            }
            if (values_result != VK_INCOMPLETE)
            {
                error = failure(failure_code, values_result);
                return false;
            }
        }

        error = failure(VulkanInstanceAcquireCode::EnumerationRetryLimitExceeded, VK_INCOMPLETE);
        return false;
    }

    template<typename Property, typename Name>
    bool hasName(const std::vector<Property>& properties, Name&& name_of, std::string_view expected) noexcept
    {
        return std::any_of(properties.begin(), properties.end(),
                           [&](const Property& property)
                           {
                               const std::optional<std::string_view> name = name_of(property);
                               return name && *name == expected;
                           });
    }

    bool contains(const std::vector<std::string>& names, std::string_view expected) noexcept
    {
        return std::any_of(names.begin(), names.end(), [expected](const std::string& name) { return name == expected; });
    }

    template<typename Function>
    Function resolveInstanceCommand(PFN_vkGetInstanceProcAddr resolver, VkInstance instance, const char* name) noexcept
    {
        return reinterpret_cast<Function>(resolver(instance, name));
    }

    class InstanceRollback
    {
    public:
        ~InstanceRollback() noexcept
        {
            if (mDebugMessenger != VK_NULL_HANDLE && mDestroyDebugMessenger)
            {
                mDestroyDebugMessenger(mInstance, mDebugMessenger, nullptr);
            }
            if (mInstance != VK_NULL_HANDLE && mDestroyInstance)
            {
                mDestroyInstance(mInstance, nullptr);
            }
        }

        void release() noexcept
        {
            mDebugMessenger        = VK_NULL_HANDLE;
            mDestroyDebugMessenger = nullptr;
            mInstance              = VK_NULL_HANDLE;
            mDestroyInstance       = nullptr;
        }

        VkInstance                          mInstance              = VK_NULL_HANDLE;
        PFN_vkDestroyInstance               mDestroyInstance       = nullptr;
        VkDebugUtilsMessengerEXT            mDebugMessenger        = VK_NULL_HANDLE;
        PFN_vkDestroyDebugUtilsMessengerEXT mDestroyDebugMessenger = nullptr;
    };

} // namespace

struct VulkanInstanceGeneration::ValidationState
{
    mutable std::mutex                                   mMutex;
    std::uint32_t                                        mMessageCount     = 0;
    bool                                                 mHasFirstMessage  = false;
    std::size_t                                          mFirstMessageSize = 0;
    std::array<char, VULKAN_VALIDATION_MESSAGE_CAPACITY> mFirstMessage{};
};

class VulkanInstanceGeneration::VulkanSurfaceGeneration final
{
public:
    VulkanSurfaceGeneration() noexcept = default;
    ~VulkanSurfaceGeneration() noexcept { reset(); }

    VulkanSurfaceGeneration(const VulkanSurfaceGeneration&)            = delete;
    VulkanSurfaceGeneration& operator=(const VulkanSurfaceGeneration&) = delete;
    VulkanSurfaceGeneration(VulkanSurfaceGeneration&&)                 = delete;
    VulkanSurfaceGeneration& operator=(VulkanSurfaceGeneration&&)      = delete;

    VkSurfaceKHR  surface() const noexcept { return mSurface; }
    std::uint64_t nativeWindowGeneration() const noexcept { return mNativeWindowGeneration; }

    void adopt(VkInstance              instance,
               VkSurfaceKHR            surface,
               PFN_vkDestroySurfaceKHR destroy_surface,
               std::uint64_t           native_window_generation) noexcept
    {
        mInstance               = instance;
        mSurface                = surface;
        mDestroySurface         = destroy_surface;
        mNativeWindowGeneration = native_window_generation;
    }

    void reset() noexcept
    {
        const VkInstance              instance        = std::exchange(mInstance, VK_NULL_HANDLE);
        const VkSurfaceKHR            surface         = std::exchange(mSurface, VK_NULL_HANDLE);
        const PFN_vkDestroySurfaceKHR destroy_surface = std::exchange(mDestroySurface, nullptr);
        mNativeWindowGeneration                       = 0;

        if (surface != VK_NULL_HANDLE && destroy_surface)
        {
            destroy_surface(instance, surface, nullptr);
        }
    }

private:
    VkInstance              mInstance               = VK_NULL_HANDLE;
    VkSurfaceKHR            mSurface                = VK_NULL_HANDLE;
    PFN_vkDestroySurfaceKHR mDestroySurface         = nullptr;
    std::uint64_t           mNativeWindowGeneration = 0;
};

struct VulkanInstanceGenerationFactory
{
    static_assert(!std::is_copy_constructible_v<VulkanInstanceGeneration::VulkanSurfaceGeneration>);
    static_assert(!std::is_copy_assignable_v<VulkanInstanceGeneration::VulkanSurfaceGeneration>);
    static_assert(!std::is_move_constructible_v<VulkanInstanceGeneration::VulkanSurfaceGeneration>);
    static_assert(!std::is_move_assignable_v<VulkanInstanceGeneration::VulkanSurfaceGeneration>);

    static std::unique_ptr<VulkanInstanceGeneration::ValidationState> allocateValidationState()
    {
        return std::make_unique<VulkanInstanceGeneration::ValidationState>();
    }

    static VKAPI_ATTR VkBool32 VKAPI_CALL validationCallback(VkDebugUtilsMessageSeverityFlagBitsEXT,
                                                             VkDebugUtilsMessageTypeFlagsEXT,
                                                             const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
                                                             void*                                       user_data) noexcept
    {
        auto* state = static_cast<VulkanInstanceGeneration::ValidationState*>(user_data);
        if (!state)
        {
            return VK_FALSE;
        }

        try
        {
            std::lock_guard<std::mutex> lock(state->mMutex);
            if (state->mMessageCount != std::numeric_limits<std::uint32_t>::max())
            {
                ++state->mMessageCount;
            }
            if (callback_data && callback_data->pMessage && !state->mHasFirstMessage)
            {
                std::size_t length = 0;
                while (length + 1 < state->mFirstMessage.size() && callback_data->pMessage[length] != '\0')
                {
                    ++length;
                }
                std::memcpy(state->mFirstMessage.data(), callback_data->pMessage, length);
                state->mFirstMessage[length] = '\0';
                state->mFirstMessageSize     = length;
                state->mHasFirstMessage      = true;
            }
        }
        catch (...)
        {
        }
        return VK_FALSE;
    }

    static VulkanInstanceGeneration create(VulkanGlobalDispatchGeneration&&                             global_dispatch,
                                           std::unique_ptr<VulkanInstanceGeneration::ValidationState>&& validation_state,
                                           std::vector<std::string>&&                                   enabled_extensions,
                                           std::vector<std::string>&&                                   enabled_layers,
                                           std::uint64_t                                                native_window_generation,
                                           bool                                                         portability_enumeration,
                                           VkInstance                                                   instance,
                                           PFN_vkDestroyInstance                                        destroy_instance,
                                           VkDebugUtilsMessengerEXT                                     debug_messenger,
                                           PFN_vkDestroyDebugUtilsMessengerEXT                          destroy_debug_messenger) noexcept
    {
        return VulkanInstanceGeneration(std::move(global_dispatch), std::move(validation_state), std::move(enabled_extensions),
                                        std::move(enabled_layers), native_window_generation, portability_enumeration, instance,
                                        destroy_instance, debug_messenger, destroy_debug_messenger);
    }

    static VulkanSurfaceAcquireResult acquireSurface(VulkanInstanceGeneration&                  instance_generation,
                                                     const VulkanSurfaceRequest&                request,
                                                     VulkanInstanceDetail::AllocationCheckpoint allocation_checkpoint) noexcept;

    static VulkanPresentationDeviceAcquireResult acquirePresentationDevice(
        VulkanInstanceGeneration&                  instance_generation,
        const VulkanPresentationDeviceRequest&     request,
        VulkanInstanceDetail::AllocationCheckpoint allocation_checkpoint) noexcept;

    static VulkanLogicalDeviceAcquireResult acquireLogicalDevice(VulkanInstanceGeneration&                  instance_generation,
                                                                 const VulkanLogicalDeviceRequest&          request,
                                                                 VulkanInstanceDetail::AllocationCheckpoint allocation_checkpoint) noexcept;

    static VulkanUploadSourceAcquireResult acquireUploadSource(VulkanInstanceGeneration&                  instance_generation,
                                                               const VulkanUploadSourceRequest&           request,
                                                               VulkanInstanceDetail::AllocationCheckpoint allocation_checkpoint) noexcept;

    static VulkanTextureUploadDestinationAcquireResult acquireTextureUploadDestination(
        VulkanInstanceGeneration&                    instance_generation,
        const VulkanTextureUploadDestinationRequest& request,
        VulkanInstanceDetail::AllocationCheckpoint   allocation_checkpoint) noexcept;

    static VulkanTextureUploadSourceAcquireResult acquireTextureUploadSource(
        VulkanInstanceGeneration&                  instance_generation,
        const VulkanTextureUploadSourceRequest&    request,
        VulkanInstanceDetail::AllocationCheckpoint allocation_checkpoint) noexcept;

    static VulkanTextureUploadTransferAcquireResult acquireTextureUploadTransfer(
        VulkanInstanceGeneration& instance_generation, const VulkanTextureUploadTransferRequest& request,
        VulkanInstanceDetail::AllocationCheckpoint allocation_checkpoint) noexcept;

    static VulkanTextureUploadSampleBindingAcquireResult acquireTextureUploadSampleBinding(
        VulkanInstanceGeneration&                      instance_generation,
        const VulkanTextureUploadSampleBindingRequest& request,
        VulkanInstanceDetail::AllocationCheckpoint     allocation_checkpoint) noexcept;

    static VulkanTextureUploadSamplePipelineAcquireResult acquireTextureUploadSamplePipeline(
        VulkanInstanceGeneration&                       instance_generation,
        const VulkanTextureUploadSamplePipelineRequest& request,
        VulkanInstanceDetail::AllocationCheckpoint      allocation_checkpoint) noexcept;

    static VulkanTextureUploadTransferParentOperationResult operateTextureUploadTransfer(
        VulkanInstanceGeneration& instance_generation, const VulkanTextureUploadTransferOperationRequest& request,
        TextureUploadTransferParentOperation operation) noexcept;

    static VulkanUploadDestinationAcquireResult acquireUploadDestination(
        VulkanInstanceGeneration&                  instance_generation,
        const VulkanUploadDestinationRequest&      request,
        VulkanInstanceDetail::AllocationCheckpoint allocation_checkpoint) noexcept;

    static VulkanUploadTransferAcquireResult acquireUploadTransfer(
        VulkanInstanceGeneration&                  instance_generation,
        const VulkanUploadTransferRequest&         request,
        VulkanInstanceDetail::AllocationCheckpoint allocation_checkpoint) noexcept;

    static VulkanUploadTransferParentOperationResult operateUploadTransfer(VulkanInstanceGeneration&                   instance_generation,
                                                                           const VulkanUploadTransferOperationRequest& request,
                                                                           UploadTransferParentOperation               operation) noexcept;

    static VulkanSwapchainConfigurationAcquireResult acquireSwapchainConfiguration(
        VulkanInstanceGeneration&                  instance_generation,
        const VulkanSwapchainConfigurationRequest& request,
        VulkanInstanceDetail::AllocationCheckpoint allocation_checkpoint) noexcept;

    static VulkanSwapchainAcquireResult acquireSwapchain(VulkanInstanceGeneration&                  instance_generation,
                                                         const VulkanSwapchainRequest&              request,
                                                         VulkanInstanceDetail::AllocationCheckpoint allocation_checkpoint) noexcept;

    static VulkanSwapchainImagesAcquireResult acquireSwapchainImages(
        VulkanInstanceGeneration&                  instance_generation,
        const VulkanSwapchainImagesRequest&        request,
        VulkanInstanceDetail::AllocationCheckpoint allocation_checkpoint) noexcept;

    static VulkanSwapchainPresentationTargetAcquireResult acquireSwapchainPresentationTarget(
        VulkanInstanceGeneration&                         instance_generation,
        const VulkanSwapchainPresentationTargetRequest&   request,
        VulkanInstanceDetail::AllocationCheckpoint        allocation_checkpoint) noexcept;

    static VulkanSwapchainPresentationPipelineAcquireResult acquireSwapchainPresentationPipeline(
        VulkanInstanceGeneration&                         instance_generation,
        const VulkanSwapchainPresentationPipelineRequest& request,
        VulkanInstanceDetail::AllocationCheckpoint        allocation_checkpoint) noexcept;

    static VulkanSwapchainReadbackAcquireResult acquireSwapchainReadback(
        VulkanInstanceGeneration&                  instance_generation,
        const VulkanSwapchainReadbackRequest&      request,
        VulkanInstanceDetail::AllocationCheckpoint allocation_checkpoint) noexcept;

    static VulkanSwapchainFrameSlotAcquireResult acquireSwapchainFrameSlot(
        VulkanInstanceGeneration&                  instance_generation,
        const VulkanSwapchainFrameSlotRequest&     request,
        VulkanInstanceDetail::AllocationCheckpoint allocation_checkpoint) noexcept;

    static VulkanSwapchainChainRebuildResult rebuildSwapchainChain(
        VulkanInstanceGeneration&                  instance_generation,
        const VulkanSwapchainChainRebuildRequest&  request,
        VulkanInstanceDetail::AllocationCheckpoint allocation_checkpoint) noexcept;

    static VulkanSwapchainFrameSlotParentOperationResult roundTripEmptySwapchainFrameSlot(
        VulkanInstanceGeneration&                       instance_generation,
        const VulkanSwapchainFrameSlotOperationRequest& request) noexcept;

    static VulkanSwapchainFrameSlotParentOperationResult retryEmptySwapchainFrameSlotCompletion(
        VulkanInstanceGeneration&                       instance_generation,
        const VulkanSwapchainFrameSlotOperationRequest& request) noexcept;

    static VulkanSwapchainFrameSlotParentPresentationResult acquireToPresentSwapchainFrameSlot(
        VulkanInstanceGeneration&                       instance_generation,
        const VulkanSwapchainFrameSlotOperationRequest& request) noexcept;

    static VulkanSwapchainFrameSlotParentPresentationResult acquireClearToPresentSwapchainFrameSlot(
        VulkanInstanceGeneration&                       instance_generation,
        const VulkanSwapchainFrameSlotOperationRequest& request,
        const VulkanSwapchainFrameClearColor&           clear_color) noexcept;

    static VulkanSwapchainFrameSlotParentPresentationResult acquireRenderPassClearToPresentSwapchainFrameSlot(
        VulkanInstanceGeneration&                       instance_generation,
        const VulkanSwapchainFrameSlotOperationRequest& request,
        const VulkanSwapchainFrameClearColor&           clear_color) noexcept;

    static VulkanSwapchainFrameSlotParentPresentationResult acquireRenderPassDrawToPresentSwapchainFrameSlot(
        VulkanInstanceGeneration&                       instance_generation,
        const VulkanSwapchainFrameSlotOperationRequest& request,
        const VulkanSwapchainFrameClearColor&           clear_color) noexcept;

    static VulkanSwapchainFrameSlotParentPresentationResult acquireRenderPassDrawReadbackToPresentSwapchainFrameSlot(
        VulkanInstanceGeneration&                       instance_generation,
        const VulkanSwapchainFrameSlotOperationRequest& request) noexcept;

    static VulkanSwapchainFrameSlotParentPresentationResult retrySwapchainFrameSlotPresentation(
        VulkanInstanceGeneration&                       instance_generation,
        const VulkanSwapchainFrameSlotOperationRequest& request) noexcept;

    static VulkanSwapchainFrameSlotParentPresentationResult retrySwapchainFrameSlotPresentationCompletion(
        VulkanInstanceGeneration&                       instance_generation,
        const VulkanSwapchainFrameSlotOperationRequest& request) noexcept;

    static VulkanSwapchainFrameSlotParentOperationResult cancelSwapchainFrameSlotPresentation(
        VulkanInstanceGeneration&                       instance_generation,
        const VulkanSwapchainFrameSlotOperationRequest& request) noexcept;

    static VulkanSwapchainFrameSlotParentOperationResult retrySwapchainFrameSlotCancellationCompletion(
        VulkanInstanceGeneration&                       instance_generation,
        const VulkanSwapchainFrameSlotOperationRequest& request) noexcept;

    static SwapchainFrameSlotParentResult operateSwapchainFrameSlot(VulkanInstanceGeneration&                       instance_generation,
                                                                    const VulkanSwapchainFrameSlotOperationRequest& request,
                                                                    SwapchainFrameSlotParentOperation               operation,
                                                                    std::optional<VulkanSwapchainFrameClearColor>   clear_color = std::nullopt) noexcept;
};

class VulkanInstanceGeneration::NativeAcquisitionGuard
{
public:
    explicit NativeAcquisitionGuard(VulkanInstanceGeneration& generation) noexcept :
        mGeneration(generation)
    {
        ++mGeneration.mNativeAcquisitionDepth;
    }

    ~NativeAcquisitionGuard() noexcept
    {
        --mGeneration.mNativeAcquisitionDepth;
    }

    NativeAcquisitionGuard(const NativeAcquisitionGuard&)            = delete;
    NativeAcquisitionGuard& operator=(const NativeAcquisitionGuard&) = delete;
    NativeAcquisitionGuard(NativeAcquisitionGuard&&)                 = delete;
    NativeAcquisitionGuard& operator=(NativeAcquisitionGuard&&)      = delete;

private:
    VulkanInstanceGeneration& mGeneration;
};

class VulkanInstanceGeneration::TextureUploadSourceTeardownGuard
{
public:
    explicit TextureUploadSourceTeardownGuard(VulkanInstanceGeneration& generation) noexcept : mGeneration(generation)
    {
        ++mGeneration.mNativeAcquisitionDepth;
        ++mGeneration.mTextureUploadSourceTeardownDepth;
    }

    ~TextureUploadSourceTeardownGuard() noexcept
    {
        --mGeneration.mTextureUploadSourceTeardownDepth;
        --mGeneration.mNativeAcquisitionDepth;
    }

    TextureUploadSourceTeardownGuard(const TextureUploadSourceTeardownGuard&)            = delete;
    TextureUploadSourceTeardownGuard& operator=(const TextureUploadSourceTeardownGuard&) = delete;
    TextureUploadSourceTeardownGuard(TextureUploadSourceTeardownGuard&&)                 = delete;
    TextureUploadSourceTeardownGuard& operator=(TextureUploadSourceTeardownGuard&&)      = delete;

private:
    VulkanInstanceGeneration& mGeneration;
};

class VulkanInstanceGeneration::TextureUploadDestinationTeardownGuard
{
public:
    explicit TextureUploadDestinationTeardownGuard(VulkanInstanceGeneration& generation) noexcept : mGeneration(generation)
    {
        ++mGeneration.mNativeAcquisitionDepth;
        ++mGeneration.mTextureUploadDestinationTeardownDepth;
    }

    ~TextureUploadDestinationTeardownGuard() noexcept
    {
        --mGeneration.mTextureUploadDestinationTeardownDepth;
        --mGeneration.mNativeAcquisitionDepth;
    }

    TextureUploadDestinationTeardownGuard(const TextureUploadDestinationTeardownGuard&)            = delete;
    TextureUploadDestinationTeardownGuard& operator=(const TextureUploadDestinationTeardownGuard&) = delete;
    TextureUploadDestinationTeardownGuard(TextureUploadDestinationTeardownGuard&&)                 = delete;
    TextureUploadDestinationTeardownGuard& operator=(TextureUploadDestinationTeardownGuard&&)      = delete;

private:
    VulkanInstanceGeneration& mGeneration;
};

class VulkanInstanceGeneration::TextureUploadTransferTeardownGuard
{
public:
    explicit TextureUploadTransferTeardownGuard(VulkanInstanceGeneration& generation) noexcept : mGeneration(generation)
    {
        ++mGeneration.mNativeAcquisitionDepth;
        ++mGeneration.mTextureUploadTransferTeardownDepth;
    }
    ~TextureUploadTransferTeardownGuard() noexcept
    {
        --mGeneration.mTextureUploadTransferTeardownDepth;
        --mGeneration.mNativeAcquisitionDepth;
    }
    TextureUploadTransferTeardownGuard(const TextureUploadTransferTeardownGuard&)            = delete;
    TextureUploadTransferTeardownGuard& operator=(const TextureUploadTransferTeardownGuard&) = delete;
    TextureUploadTransferTeardownGuard(TextureUploadTransferTeardownGuard&&)                 = delete;
    TextureUploadTransferTeardownGuard& operator=(TextureUploadTransferTeardownGuard&&)      = delete;

private:
    VulkanInstanceGeneration& mGeneration;
};

class VulkanInstanceGeneration::TextureUploadSampleBindingTeardownGuard
{
public:
    explicit TextureUploadSampleBindingTeardownGuard(VulkanInstanceGeneration& generation) noexcept : mGeneration(generation)
    {
        ++mGeneration.mNativeAcquisitionDepth;
        ++mGeneration.mTextureUploadSampleBindingTeardownDepth;
    }

    ~TextureUploadSampleBindingTeardownGuard() noexcept
    {
        --mGeneration.mTextureUploadSampleBindingTeardownDepth;
        --mGeneration.mNativeAcquisitionDepth;
    }

    TextureUploadSampleBindingTeardownGuard(const TextureUploadSampleBindingTeardownGuard&)            = delete;
    TextureUploadSampleBindingTeardownGuard& operator=(const TextureUploadSampleBindingTeardownGuard&) = delete;
    TextureUploadSampleBindingTeardownGuard(TextureUploadSampleBindingTeardownGuard&&)                 = delete;
    TextureUploadSampleBindingTeardownGuard& operator=(TextureUploadSampleBindingTeardownGuard&&)      = delete;

private:
    VulkanInstanceGeneration& mGeneration;
};

class VulkanInstanceGeneration::TextureUploadSamplePipelineTeardownGuard
{
public:
    explicit TextureUploadSamplePipelineTeardownGuard(VulkanInstanceGeneration& generation) noexcept : mGeneration(generation)
    {
        ++mGeneration.mNativeAcquisitionDepth;
        ++mGeneration.mTextureUploadSamplePipelineTeardownDepth;
    }

    ~TextureUploadSamplePipelineTeardownGuard() noexcept
    {
        --mGeneration.mTextureUploadSamplePipelineTeardownDepth;
        --mGeneration.mNativeAcquisitionDepth;
    }

    TextureUploadSamplePipelineTeardownGuard(const TextureUploadSamplePipelineTeardownGuard&)            = delete;
    TextureUploadSamplePipelineTeardownGuard& operator=(const TextureUploadSamplePipelineTeardownGuard&) = delete;
    TextureUploadSamplePipelineTeardownGuard(TextureUploadSamplePipelineTeardownGuard&&)                 = delete;
    TextureUploadSamplePipelineTeardownGuard& operator=(TextureUploadSamplePipelineTeardownGuard&&)      = delete;

private:
    VulkanInstanceGeneration& mGeneration;
};

class VulkanInstanceGeneration::SwapchainPresentationTargetTeardownGuard
{
public:
    explicit SwapchainPresentationTargetTeardownGuard(VulkanInstanceGeneration& generation) noexcept : mGeneration(generation)
    {
        ++mGeneration.mNativeAcquisitionDepth;
        ++mGeneration.mSwapchainPresentationTargetTeardownDepth;
    }

    ~SwapchainPresentationTargetTeardownGuard() noexcept
    {
        --mGeneration.mSwapchainPresentationTargetTeardownDepth;
        --mGeneration.mNativeAcquisitionDepth;
    }

    SwapchainPresentationTargetTeardownGuard(const SwapchainPresentationTargetTeardownGuard&)            = delete;
    SwapchainPresentationTargetTeardownGuard& operator=(const SwapchainPresentationTargetTeardownGuard&) = delete;
    SwapchainPresentationTargetTeardownGuard(SwapchainPresentationTargetTeardownGuard&&)                 = delete;
    SwapchainPresentationTargetTeardownGuard& operator=(SwapchainPresentationTargetTeardownGuard&&)      = delete;

private:
    VulkanInstanceGeneration& mGeneration;
};

VulkanInstanceGeneration::VulkanInstanceGeneration(VulkanGlobalDispatchGeneration&&    global_dispatch,
                                                   std::unique_ptr<ValidationState>&&  validation_state,
                                                   std::vector<std::string>&&          enabled_extensions,
                                                   std::vector<std::string>&&          enabled_layers,
                                                   std::uint64_t                       native_window_generation,
                                                   bool                                portability_enumeration,
                                                   VkInstance                          instance,
                                                   PFN_vkDestroyInstance               destroy_instance,
                                                   VkDebugUtilsMessengerEXT            debug_messenger,
                                                   PFN_vkDestroyDebugUtilsMessengerEXT destroy_debug_messenger) noexcept :
    mGlobalDispatch(std::move(global_dispatch)),
    mValidationState(std::move(validation_state)),
    mEnabledExtensions(std::move(enabled_extensions)),
    mEnabledLayers(std::move(enabled_layers)),
    mNativeWindowGeneration(native_window_generation),
    mPortabilityEnumeration(portability_enumeration),
    mInstance(instance),
    mDestroyInstance(destroy_instance),
    mDebugMessenger(debug_messenger),
    mDestroyDebugMessenger(destroy_debug_messenger),
    mOwnershipTransitionEpoch(1)
{
}

VulkanInstanceGeneration::~VulkanInstanceGeneration() noexcept
{
    reset();
}

VulkanInstanceGeneration::VulkanInstanceGeneration(VulkanInstanceGeneration&& other) noexcept
{
    // A live native candidate retains the source generation as its destruction
    // parent. A reentrant move therefore leaves that source intact and produces
    // an empty destination until the acquisition transaction has completed.
    if (other.mNativeAcquisitionDepth != 0)
    {
        return;
    }

    if (other.mGlobalDispatch)
    {
        mGlobalDispatch.emplace(std::move(*other.mGlobalDispatch));
    }
    mValidationState                  = std::move(other.mValidationState);
    mEnabledExtensions                = std::move(other.mEnabledExtensions);
    mEnabledLayers                    = std::move(other.mEnabledLayers);
    mNativeWindowGeneration           = std::exchange(other.mNativeWindowGeneration, 0);
    mPortabilityEnumeration           = std::exchange(other.mPortabilityEnumeration, false);
    mInstance                         = std::exchange(other.mInstance, VK_NULL_HANDLE);
    mDestroyInstance                  = std::exchange(other.mDestroyInstance, nullptr);
    mDebugMessenger                   = std::exchange(other.mDebugMessenger, VK_NULL_HANDLE);
    mDestroyDebugMessenger            = std::exchange(other.mDestroyDebugMessenger, nullptr);
    mSurfaceGeneration                = std::move(other.mSurfaceGeneration);
    mPresentationDeviceGeneration     = std::move(other.mPresentationDeviceGeneration);
    mLogicalDeviceGeneration          = std::move(other.mLogicalDeviceGeneration);
    mTextureUploadDestinationGeneration      = std::move(other.mTextureUploadDestinationGeneration);
    mTextureUploadSourceGeneration           = std::move(other.mTextureUploadSourceGeneration);
    mTextureUploadTransferGeneration         = std::move(other.mTextureUploadTransferGeneration);
    mTextureUploadSampleBindingGeneration    = std::move(other.mTextureUploadSampleBindingGeneration);
    mTextureUploadSamplePipelineGeneration   = std::move(other.mTextureUploadSamplePipelineGeneration);
    mUploadSourceGeneration                  = std::move(other.mUploadSourceGeneration);
    mUploadDestinationGeneration             = std::move(other.mUploadDestinationGeneration);
    mUploadTransferGeneration                = std::move(other.mUploadTransferGeneration);
    mSwapchainConfigurationGeneration = std::move(other.mSwapchainConfigurationGeneration);
    mSwapchainGeneration              = std::move(other.mSwapchainGeneration);
    mSwapchainImagesGeneration               = std::move(other.mSwapchainImagesGeneration);
    mSwapchainPresentationTargetGeneration   = std::move(other.mSwapchainPresentationTargetGeneration);
    mSwapchainPresentationPipelineGeneration = std::move(other.mSwapchainPresentationPipelineGeneration);
    mSwapchainReadbackGeneration             = std::move(other.mSwapchainReadbackGeneration);
    mSwapchainFrameSlotGeneration            = std::move(other.mSwapchainFrameSlotGeneration);
    mTextureUploadDestinationEpoch           = std::exchange(other.mTextureUploadDestinationEpoch, 0);
    mTextureUploadSourceEpoch                = std::exchange(other.mTextureUploadSourceEpoch, 0);
    mTextureUploadTransferEpoch              = std::exchange(other.mTextureUploadTransferEpoch, 0);
    mTextureUploadSampleBindingEpoch         = std::exchange(other.mTextureUploadSampleBindingEpoch, 0);
    mTextureUploadSamplePipelineEpoch        = std::exchange(other.mTextureUploadSamplePipelineEpoch, 0);
    mUploadSourceEpoch                       = std::exchange(other.mUploadSourceEpoch, 0);
    mUploadDestinationEpoch                  = std::exchange(other.mUploadDestinationEpoch, 0);
    mUploadTransferEpoch                     = std::exchange(other.mUploadTransferEpoch, 0);
    mSwapchainPresentationTargetEpoch        = std::exchange(other.mSwapchainPresentationTargetEpoch, 0);
    mSwapchainPresentationPipelineEpoch      = std::exchange(other.mSwapchainPresentationPipelineEpoch, 0);
    mSwapchainReadbackEpoch                  = std::exchange(other.mSwapchainReadbackEpoch, 0);
    mSwapchainFrameSlotEpoch                 = std::exchange(other.mSwapchainFrameSlotEpoch, 0);
    mOwnershipTransitionEpoch                = std::exchange(other.mOwnershipTransitionEpoch, 0);
    other.mGlobalDispatch.reset();
}

bool VulkanInstanceGeneration::isExtensionEnabled(std::string_view extension_name) const noexcept
{
    return contains(mEnabledExtensions, extension_name);
}

VulkanValidationSnapshot VulkanInstanceGeneration::validationSnapshot() const noexcept
{
    VulkanValidationSnapshot snapshot;
    if (!mValidationState)
    {
        return snapshot;
    }

    try
    {
        std::lock_guard<std::mutex> lock(mValidationState->mMutex);
        snapshot.mMessageCount     = mValidationState->mMessageCount;
        snapshot.mFirstMessageSize = mValidationState->mFirstMessageSize;
        snapshot.mFirstMessage     = mValidationState->mFirstMessage;
    }
    catch (...)
    {
        snapshot.mFirstMessageSize = 0;
        snapshot.mFirstMessage.fill('\0');
    }
    return snapshot;
}

bool VulkanInstanceGeneration::hasSurfaceGeneration() const noexcept
{
    return mSurfaceGeneration != nullptr;
}

VkSurfaceKHR VulkanInstanceGeneration::surface() const noexcept
{
    return mSurfaceGeneration ? mSurfaceGeneration->surface() : VK_NULL_HANDLE;
}

std::uint64_t VulkanInstanceGeneration::surfaceNativeWindowGeneration() const noexcept
{
    return mSurfaceGeneration ? mSurfaceGeneration->nativeWindowGeneration() : 0;
}

bool VulkanInstanceGeneration::hasPresentationDeviceGeneration() const noexcept
{
    return mPresentationDeviceGeneration != nullptr;
}

VkPhysicalDevice VulkanInstanceGeneration::physicalDevice() const noexcept
{
    return mPresentationDeviceGeneration ? mPresentationDeviceGeneration->physicalDevice() : VK_NULL_HANDLE;
}

std::uint32_t VulkanInstanceGeneration::physicalDeviceIndex() const noexcept
{
    return mPresentationDeviceGeneration ? mPresentationDeviceGeneration->physicalDeviceIndex() : std::numeric_limits<std::uint32_t>::max();
}

VkPhysicalDeviceProperties VulkanInstanceGeneration::physicalDeviceProperties() const noexcept
{
    return mPresentationDeviceGeneration ? mPresentationDeviceGeneration->properties() : VkPhysicalDeviceProperties{};
}

std::uint32_t VulkanInstanceGeneration::presentationQueueFamilyIndex() const noexcept
{
    return mPresentationDeviceGeneration ? mPresentationDeviceGeneration->queueFamilyIndex() : VK_QUEUE_FAMILY_IGNORED;
}

VkQueueFamilyProperties VulkanInstanceGeneration::presentationQueueFamilyProperties() const noexcept
{
    return mPresentationDeviceGeneration ? mPresentationDeviceGeneration->queueFamilyProperties() : VkQueueFamilyProperties{};
}

std::span<const std::string_view> VulkanInstanceGeneration::requiredDeviceExtensions() const noexcept
{
    return mPresentationDeviceGeneration ? mPresentationDeviceGeneration->requiredDeviceExtensions() : std::span<const std::string_view>{};
}

bool VulkanInstanceGeneration::swapchainMaintenance1Supported() const noexcept
{
    return mPresentationDeviceGeneration && mPresentationDeviceGeneration->swapchainMaintenance1Supported();
}

bool VulkanInstanceGeneration::portabilitySubsetRequired() const noexcept
{
    return mPresentationDeviceGeneration && mPresentationDeviceGeneration->portabilitySubsetRequired();
}

bool VulkanInstanceGeneration::hasLogicalDeviceGeneration() const noexcept
{
    return mLogicalDeviceGeneration != nullptr;
}

VkDevice VulkanInstanceGeneration::logicalDevice() const noexcept
{
    return mLogicalDeviceGeneration ? mLogicalDeviceGeneration->device() : VK_NULL_HANDLE;
}

VkQueue VulkanInstanceGeneration::presentationQueue() const noexcept
{
    return mLogicalDeviceGeneration ? mLogicalDeviceGeneration->queue() : VK_NULL_HANDLE;
}

VkPhysicalDevice VulkanInstanceGeneration::logicalDevicePhysicalDevice() const noexcept
{
    return mLogicalDeviceGeneration ? mLogicalDeviceGeneration->physicalDevice() : VK_NULL_HANDLE;
}

std::uint32_t VulkanInstanceGeneration::logicalDeviceQueueFamilyIndex() const noexcept
{
    return mLogicalDeviceGeneration ? mLogicalDeviceGeneration->queueFamilyIndex() : VK_QUEUE_FAMILY_IGNORED;
}

std::uint32_t VulkanInstanceGeneration::logicalDeviceQueueIndex() const noexcept
{
    return mLogicalDeviceGeneration ? mLogicalDeviceGeneration->queueIndex() : std::numeric_limits<std::uint32_t>::max();
}

VkPhysicalDeviceFeatures VulkanInstanceGeneration::logicalDeviceEnabledFeatures() const noexcept
{
    return mLogicalDeviceGeneration ? mLogicalDeviceGeneration->enabledFeatures() : VkPhysicalDeviceFeatures{};
}

std::span<const std::string_view> VulkanInstanceGeneration::enabledDeviceExtensions() const noexcept
{
    return mLogicalDeviceGeneration ? mLogicalDeviceGeneration->enabledDeviceExtensions() : std::span<const std::string_view>{};
}

bool VulkanInstanceGeneration::swapchainMaintenance1Enabled() const noexcept
{
    return mLogicalDeviceGeneration && mLogicalDeviceGeneration->swapchainMaintenance1Enabled();
}

bool VulkanInstanceGeneration::portabilitySubsetEnabled() const noexcept
{
    return mLogicalDeviceGeneration && mLogicalDeviceGeneration->portabilitySubsetEnabled();
}

bool VulkanInstanceGeneration::hasUploadSourceGeneration() const noexcept
{
    return mUploadSourceGeneration != nullptr;
}

LLRenderContract::BufferHandle VulkanInstanceGeneration::uploadSourceResourceHandle() const noexcept
{
    return mUploadSourceGeneration ? mUploadSourceGeneration->resourceHandle() : LLRenderContract::BufferHandle{};
}

std::uint64_t VulkanInstanceGeneration::uploadSourceContentIdentity() const noexcept
{
    return mUploadSourceGeneration ? mUploadSourceGeneration->contentIdentity() : 0;
}

VkBuffer VulkanInstanceGeneration::uploadSourceBuffer() const noexcept
{
    return mUploadSourceGeneration ? mUploadSourceGeneration->buffer() : VK_NULL_HANDLE;
}

VkDeviceMemory VulkanInstanceGeneration::uploadSourceMemory() const noexcept
{
    return mUploadSourceGeneration ? mUploadSourceGeneration->memory() : VK_NULL_HANDLE;
}

VkDeviceSize VulkanInstanceGeneration::uploadSourceByteCount() const noexcept
{
    return mUploadSourceGeneration ? mUploadSourceGeneration->byteCount() : 0;
}

VkDeviceSize VulkanInstanceGeneration::uploadSourceAllocationSize() const noexcept
{
    return mUploadSourceGeneration ? mUploadSourceGeneration->allocationSize() : 0;
}

std::uint32_t VulkanInstanceGeneration::uploadSourceMemoryTypeIndex() const noexcept
{
    return mUploadSourceGeneration ? mUploadSourceGeneration->memoryTypeIndex() : 0;
}

VkMemoryPropertyFlags VulkanInstanceGeneration::uploadSourceMemoryPropertyFlags() const noexcept
{
    return mUploadSourceGeneration ? mUploadSourceGeneration->memoryPropertyFlags() : 0;
}

bool VulkanInstanceGeneration::uploadSourceIsCoherent() const noexcept
{
    return mUploadSourceGeneration && mUploadSourceGeneration->isCoherent();
}

bool VulkanInstanceGeneration::hasTextureUploadDestinationGeneration() const noexcept
{
    return mTextureUploadDestinationGeneration != nullptr;
}

LLRenderContract::ImageHandle VulkanInstanceGeneration::textureUploadDestinationResourceHandle() const noexcept
{
    return mTextureUploadDestinationGeneration ? mTextureUploadDestinationGeneration->resourceHandle() : LLRenderContract::ImageHandle{};
}

std::uint64_t VulkanInstanceGeneration::textureUploadDestinationExpectedRevision() const noexcept
{
    return mTextureUploadDestinationGeneration ? mTextureUploadDestinationGeneration->expectedRevision() : 0;
}

VkExtent3D VulkanInstanceGeneration::textureUploadDestinationResidentExtent() const noexcept
{
    return mTextureUploadDestinationGeneration ? mTextureUploadDestinationGeneration->residentExtent() : VkExtent3D{};
}

LLRenderContract::Extent2D VulkanInstanceGeneration::textureUploadDestinationLogicalExtent() const noexcept
{
    return mTextureUploadDestinationGeneration ? mTextureUploadDestinationGeneration->logicalExtent() : LLRenderContract::Extent2D{};
}

std::uint32_t VulkanInstanceGeneration::textureUploadDestinationResidentDiscard() const noexcept
{
    return mTextureUploadDestinationGeneration ? mTextureUploadDestinationGeneration->residentDiscard() : 0;
}

LLRenderContract::PixelFormat VulkanInstanceGeneration::textureUploadDestinationPixelFormat() const noexcept
{
    return mTextureUploadDestinationGeneration ? mTextureUploadDestinationGeneration->pixelFormat()
                                               : LLRenderContract::PixelFormat::RGBA8Unorm;
}

LLRenderContract::ImageState VulkanInstanceGeneration::textureUploadDestinationInitialState() const noexcept
{
    return mTextureUploadDestinationGeneration ? mTextureUploadDestinationGeneration->initialState()
                                               : LLRenderContract::ImageState::Undefined;
}

VkImageCreateFlags VulkanInstanceGeneration::textureUploadDestinationFlags() const noexcept
{
    return mTextureUploadDestinationGeneration ? mTextureUploadDestinationGeneration->flags() : 0;
}

VkImageType VulkanInstanceGeneration::textureUploadDestinationImageType() const noexcept
{
    return mTextureUploadDestinationGeneration ? mTextureUploadDestinationGeneration->imageType() : VK_IMAGE_TYPE_MAX_ENUM;
}

VkFormat VulkanInstanceGeneration::textureUploadDestinationFormat() const noexcept
{
    return mTextureUploadDestinationGeneration ? mTextureUploadDestinationGeneration->format() : VK_FORMAT_UNDEFINED;
}

std::uint32_t VulkanInstanceGeneration::textureUploadDestinationMipLevels() const noexcept
{
    return mTextureUploadDestinationGeneration ? mTextureUploadDestinationGeneration->mipLevels() : 0;
}

std::uint32_t VulkanInstanceGeneration::textureUploadDestinationArrayLayers() const noexcept
{
    return mTextureUploadDestinationGeneration ? mTextureUploadDestinationGeneration->arrayLayers() : 0;
}

VkSampleCountFlagBits VulkanInstanceGeneration::textureUploadDestinationSamples() const noexcept
{
    return mTextureUploadDestinationGeneration ? mTextureUploadDestinationGeneration->samples() : VK_SAMPLE_COUNT_FLAG_BITS_MAX_ENUM;
}

VkImageTiling VulkanInstanceGeneration::textureUploadDestinationTiling() const noexcept
{
    return mTextureUploadDestinationGeneration ? mTextureUploadDestinationGeneration->tiling() : VK_IMAGE_TILING_MAX_ENUM;
}

VkImageUsageFlags VulkanInstanceGeneration::textureUploadDestinationUsage() const noexcept
{
    return mTextureUploadDestinationGeneration ? mTextureUploadDestinationGeneration->usage() : 0;
}

VkSharingMode VulkanInstanceGeneration::textureUploadDestinationSharingMode() const noexcept
{
    return mTextureUploadDestinationGeneration ? mTextureUploadDestinationGeneration->sharingMode() : VK_SHARING_MODE_MAX_ENUM;
}

VkImageLayout VulkanInstanceGeneration::textureUploadDestinationInitialLayout() const noexcept
{
    return mTextureUploadDestinationGeneration ? mTextureUploadDestinationGeneration->initialLayout() : VK_IMAGE_LAYOUT_MAX_ENUM;
}

VkFormatFeatureFlags VulkanInstanceGeneration::textureUploadDestinationFormatFeatures() const noexcept
{
    return mTextureUploadDestinationGeneration ? mTextureUploadDestinationGeneration->formatFeatures() : 0;
}

VkImageFormatProperties VulkanInstanceGeneration::textureUploadDestinationImageFormatProperties() const noexcept
{
    return mTextureUploadDestinationGeneration ? mTextureUploadDestinationGeneration->imageFormatProperties() : VkImageFormatProperties{};
}

VkImage VulkanInstanceGeneration::textureUploadDestinationImage() const noexcept
{
    return mTextureUploadDestinationGeneration ? mTextureUploadDestinationGeneration->image() : VK_NULL_HANDLE;
}

VkDeviceMemory VulkanInstanceGeneration::textureUploadDestinationMemory() const noexcept
{
    return mTextureUploadDestinationGeneration ? mTextureUploadDestinationGeneration->memory() : VK_NULL_HANDLE;
}

VkMemoryRequirements VulkanInstanceGeneration::textureUploadDestinationMemoryRequirements() const noexcept
{
    return mTextureUploadDestinationGeneration ? mTextureUploadDestinationGeneration->memoryRequirements() : VkMemoryRequirements{};
}

VkDeviceSize VulkanInstanceGeneration::textureUploadDestinationAllocationSize() const noexcept
{
    return mTextureUploadDestinationGeneration ? mTextureUploadDestinationGeneration->allocationSize() : 0;
}

VkDeviceSize VulkanInstanceGeneration::textureUploadDestinationAllocationAlignment() const noexcept
{
    return mTextureUploadDestinationGeneration ? mTextureUploadDestinationGeneration->allocationAlignment() : 0;
}

std::uint32_t VulkanInstanceGeneration::textureUploadDestinationCompatibleMemoryTypeBits() const noexcept
{
    return mTextureUploadDestinationGeneration ? mTextureUploadDestinationGeneration->compatibleMemoryTypeBits() : 0;
}

std::uint32_t VulkanInstanceGeneration::textureUploadDestinationMemoryTypeIndex() const noexcept
{
    return mTextureUploadDestinationGeneration ? mTextureUploadDestinationGeneration->memoryTypeIndex() : 0;
}

VkMemoryPropertyFlags VulkanInstanceGeneration::textureUploadDestinationMemoryPropertyFlags() const noexcept
{
    return mTextureUploadDestinationGeneration ? mTextureUploadDestinationGeneration->memoryPropertyFlags() : 0;
}

bool VulkanInstanceGeneration::textureUploadDestinationIsDeviceLocal() const noexcept
{
    return mTextureUploadDestinationGeneration && mTextureUploadDestinationGeneration->isDeviceLocal();
}

bool VulkanInstanceGeneration::textureUploadDestinationPrefersDedicatedAllocation() const noexcept
{
    return mTextureUploadDestinationGeneration && mTextureUploadDestinationGeneration->prefersDedicatedAllocation();
}

bool VulkanInstanceGeneration::textureUploadDestinationRequiresDedicatedAllocation() const noexcept
{
    return mTextureUploadDestinationGeneration && mTextureUploadDestinationGeneration->requiresDedicatedAllocation();
}

VkImageView VulkanInstanceGeneration::textureUploadDestinationImageView() const noexcept
{
    return mTextureUploadDestinationGeneration ? mTextureUploadDestinationGeneration->imageView() : VK_NULL_HANDLE;
}

VkImageViewType VulkanInstanceGeneration::textureUploadDestinationImageViewType() const noexcept
{
    return mTextureUploadDestinationGeneration ? mTextureUploadDestinationGeneration->imageViewType() : VK_IMAGE_VIEW_TYPE_MAX_ENUM;
}

VkImageSubresourceRange VulkanInstanceGeneration::textureUploadDestinationViewRange() const noexcept
{
    return mTextureUploadDestinationGeneration ? mTextureUploadDestinationGeneration->viewRange() : VkImageSubresourceRange{};
}

bool VulkanInstanceGeneration::textureUploadDestinationIsResident() const noexcept
{
    return mTextureUploadDestinationGeneration && mTextureUploadDestinationGeneration->isResident();
}
std::uint64_t VulkanInstanceGeneration::textureUploadDestinationResidentRevision() const noexcept
{
    return mTextureUploadDestinationGeneration ? mTextureUploadDestinationGeneration->residentRevision() : 0;
}
std::uint64_t VulkanInstanceGeneration::textureUploadDestinationResidentContentIdentity() const noexcept
{
    return mTextureUploadDestinationGeneration ? mTextureUploadDestinationGeneration->residentContentIdentity() : 0;
}
LLRenderContract::ImageState VulkanInstanceGeneration::textureUploadDestinationCurrentState() const noexcept
{
    return mTextureUploadDestinationGeneration ? mTextureUploadDestinationGeneration->currentState()
                                               : LLRenderContract::ImageState::Undefined;
}

bool VulkanInstanceGeneration::hasTextureUploadSourceGeneration() const noexcept
{
    return mTextureUploadSourceGeneration != nullptr;
}

LLRenderContract::ImageHandle VulkanInstanceGeneration::textureUploadSourceResourceHandle() const noexcept
{
    return mTextureUploadSourceGeneration ? mTextureUploadSourceGeneration->resourceHandle() : LLRenderContract::ImageHandle{};
}

std::uint64_t VulkanInstanceGeneration::textureUploadSourceExpectedRevision() const noexcept
{
    return mTextureUploadSourceGeneration ? mTextureUploadSourceGeneration->expectedRevision() : 0;
}

LLRenderContract::Extent2D VulkanInstanceGeneration::textureUploadSourceResidentExtent() const noexcept
{
    return mTextureUploadSourceGeneration ? mTextureUploadSourceGeneration->residentExtent() : LLRenderContract::Extent2D{};
}

LLRenderContract::PixelFormat VulkanInstanceGeneration::textureUploadSourcePixelFormat() const noexcept
{
    return mTextureUploadSourceGeneration ? mTextureUploadSourceGeneration->pixelFormat() : LLRenderContract::PixelFormat::RGBA8Unorm;
}

std::uint32_t VulkanInstanceGeneration::textureUploadSourceRowPitch() const noexcept
{
    return mTextureUploadSourceGeneration ? mTextureUploadSourceGeneration->rowPitch() : 0;
}

LLRenderContract::RowOrigin VulkanInstanceGeneration::textureUploadSourceRowOrigin() const noexcept
{
    return mTextureUploadSourceGeneration ? mTextureUploadSourceGeneration->rowOrigin() : LLRenderContract::RowOrigin::TopLeft;
}

std::uint64_t VulkanInstanceGeneration::textureUploadSourceContentIdentity() const noexcept
{
    return mTextureUploadSourceGeneration ? mTextureUploadSourceGeneration->contentIdentity() : 0;
}

VkBufferCreateFlags VulkanInstanceGeneration::textureUploadSourceFlags() const noexcept
{
    return mTextureUploadSourceGeneration ? mTextureUploadSourceGeneration->flags() : VK_BUFFER_CREATE_FLAG_BITS_MAX_ENUM;
}

VkBufferUsageFlags VulkanInstanceGeneration::textureUploadSourceUsage() const noexcept
{
    return mTextureUploadSourceGeneration ? mTextureUploadSourceGeneration->usage() : 0;
}

VkSharingMode VulkanInstanceGeneration::textureUploadSourceSharingMode() const noexcept
{
    return mTextureUploadSourceGeneration ? mTextureUploadSourceGeneration->sharingMode() : VK_SHARING_MODE_MAX_ENUM;
}

VkBuffer VulkanInstanceGeneration::textureUploadSourceBuffer() const noexcept
{
    return mTextureUploadSourceGeneration ? mTextureUploadSourceGeneration->buffer() : VK_NULL_HANDLE;
}

VkDeviceMemory VulkanInstanceGeneration::textureUploadSourceMemory() const noexcept
{
    return mTextureUploadSourceGeneration ? mTextureUploadSourceGeneration->memory() : VK_NULL_HANDLE;
}

VkDeviceSize VulkanInstanceGeneration::textureUploadSourceByteCount() const noexcept
{
    return mTextureUploadSourceGeneration ? mTextureUploadSourceGeneration->byteCount() : 0;
}

VkDeviceSize VulkanInstanceGeneration::textureUploadSourceAllocationSize() const noexcept
{
    return mTextureUploadSourceGeneration ? mTextureUploadSourceGeneration->allocationSize() : 0;
}

std::uint32_t VulkanInstanceGeneration::textureUploadSourceMemoryTypeIndex() const noexcept
{
    return mTextureUploadSourceGeneration ? mTextureUploadSourceGeneration->memoryTypeIndex() : 0;
}

VkMemoryPropertyFlags VulkanInstanceGeneration::textureUploadSourceMemoryPropertyFlags() const noexcept
{
    return mTextureUploadSourceGeneration ? mTextureUploadSourceGeneration->memoryPropertyFlags() : 0;
}

bool VulkanInstanceGeneration::textureUploadSourceIsCoherent() const noexcept
{
    return mTextureUploadSourceGeneration && mTextureUploadSourceGeneration->isCoherent();
}

bool VulkanInstanceGeneration::hasTextureUploadTransferGeneration() const noexcept
{
    return mTextureUploadTransferGeneration != nullptr;
}
LLRenderContract::ImageHandle VulkanInstanceGeneration::textureUploadTransferResourceHandle() const noexcept
{
    return mTextureUploadTransferGeneration ? mTextureUploadTransferGeneration->resourceHandle() : LLRenderContract::ImageHandle{};
}
std::uint64_t VulkanInstanceGeneration::textureUploadTransferExpectedRevision() const noexcept
{
    return mTextureUploadTransferGeneration ? mTextureUploadTransferGeneration->expectedRevision() : 0;
}
std::uint64_t VulkanInstanceGeneration::textureUploadTransferContentIdentity() const noexcept
{
    return mTextureUploadTransferGeneration ? mTextureUploadTransferGeneration->contentIdentity() : 0;
}
VkBuffer VulkanInstanceGeneration::textureUploadTransferSourceBuffer() const noexcept
{
    return mTextureUploadTransferGeneration ? mTextureUploadTransferGeneration->sourceBuffer() : VK_NULL_HANDLE;
}
VkImage VulkanInstanceGeneration::textureUploadTransferDestinationImage() const noexcept
{
    return mTextureUploadTransferGeneration ? mTextureUploadTransferGeneration->destinationImage() : VK_NULL_HANDLE;
}
VkQueue VulkanInstanceGeneration::textureUploadTransferQueue() const noexcept
{
    return mTextureUploadTransferGeneration ? mTextureUploadTransferGeneration->queue() : VK_NULL_HANDLE;
}
std::uint32_t VulkanInstanceGeneration::textureUploadTransferQueueFamilyIndex() const noexcept
{
    return mTextureUploadTransferGeneration ? mTextureUploadTransferGeneration->queueFamilyIndex() : VK_QUEUE_FAMILY_IGNORED;
}
std::uint32_t VulkanInstanceGeneration::textureUploadTransferQueueIndex() const noexcept
{
    return mTextureUploadTransferGeneration ? mTextureUploadTransferGeneration->queueIndex() : std::numeric_limits<std::uint32_t>::max();
}
VkCommandPool VulkanInstanceGeneration::textureUploadTransferCommandPool() const noexcept
{
    return mTextureUploadTransferGeneration ? mTextureUploadTransferGeneration->commandPool() : VK_NULL_HANDLE;
}
VkCommandBuffer VulkanInstanceGeneration::textureUploadTransferCommandBuffer() const noexcept
{
    return mTextureUploadTransferGeneration ? mTextureUploadTransferGeneration->commandBuffer() : VK_NULL_HANDLE;
}
VkFence VulkanInstanceGeneration::textureUploadTransferFence() const noexcept
{
    return mTextureUploadTransferGeneration ? mTextureUploadTransferGeneration->fence() : VK_NULL_HANDLE;
}
std::uint32_t VulkanInstanceGeneration::textureUploadTransferSubmissionCount() const noexcept
{
    return mTextureUploadTransferGeneration ? mTextureUploadTransferGeneration->submissionAttemptCount() : 0;
}
std::uint32_t VulkanInstanceGeneration::textureUploadTransferCompletionWaitCount() const noexcept
{
    return mTextureUploadTransferGeneration ? mTextureUploadTransferGeneration->completionWaitCount() : 0;
}
std::optional<VulkanTextureUploadTransferDisposition> VulkanInstanceGeneration::textureUploadTransferDisposition() const noexcept
{
    return mTextureUploadTransferGeneration ? std::optional{ mTextureUploadTransferGeneration->disposition() } : std::nullopt;
}

bool VulkanInstanceGeneration::hasTextureUploadSampleBindingGeneration() const noexcept
{
    return mTextureUploadSampleBindingGeneration != nullptr;
}

LLRenderContract::SamplerHandle VulkanInstanceGeneration::textureUploadSampleBindingSamplerResourceHandle() const noexcept
{
    return mTextureUploadSampleBindingGeneration ? mTextureUploadSampleBindingGeneration->samplerResourceHandle()
                                                 : LLRenderContract::SamplerHandle{};
}

LLRenderContract::ImageHandle VulkanInstanceGeneration::textureUploadSampleBindingDestinationResourceHandle() const noexcept
{
    return mTextureUploadSampleBindingGeneration ? mTextureUploadSampleBindingGeneration->destinationResourceHandle()
                                                 : LLRenderContract::ImageHandle{};
}

std::uint64_t VulkanInstanceGeneration::textureUploadSampleBindingExpectedRevision() const noexcept
{
    return mTextureUploadSampleBindingGeneration ? mTextureUploadSampleBindingGeneration->expectedRevision() : 0;
}

std::uint64_t VulkanInstanceGeneration::textureUploadSampleBindingResidentRevision() const noexcept
{
    return mTextureUploadSampleBindingGeneration ? mTextureUploadSampleBindingGeneration->residentRevision() : 0;
}

std::uint64_t VulkanInstanceGeneration::textureUploadSampleBindingResidentContentIdentity() const noexcept
{
    return mTextureUploadSampleBindingGeneration ? mTextureUploadSampleBindingGeneration->residentContentIdentity() : 0;
}

VkImageView VulkanInstanceGeneration::textureUploadSampleBindingDestinationImageView() const noexcept
{
    return mTextureUploadSampleBindingGeneration ? mTextureUploadSampleBindingGeneration->destinationImageView() : VK_NULL_HANDLE;
}

VkImageLayout VulkanInstanceGeneration::textureUploadSampleBindingDestinationImageLayout() const noexcept
{
    return mTextureUploadSampleBindingGeneration ? mTextureUploadSampleBindingGeneration->destinationImageLayout()
                                                 : VK_IMAGE_LAYOUT_MAX_ENUM;
}

std::uint32_t VulkanInstanceGeneration::textureUploadSampleBindingDescriptorSetIndex() const noexcept
{
    return mTextureUploadSampleBindingGeneration ? mTextureUploadSampleBindingGeneration->descriptorSetIndex()
                                                 : std::numeric_limits<std::uint32_t>::max();
}

std::uint32_t VulkanInstanceGeneration::textureUploadSampleBindingBinding() const noexcept
{
    return mTextureUploadSampleBindingGeneration ? mTextureUploadSampleBindingGeneration->binding()
                                                 : std::numeric_limits<std::uint32_t>::max();
}

VkSampler VulkanInstanceGeneration::textureUploadSampleBindingSampler() const noexcept
{
    return mTextureUploadSampleBindingGeneration ? mTextureUploadSampleBindingGeneration->sampler() : VK_NULL_HANDLE;
}

VkDescriptorSetLayout VulkanInstanceGeneration::textureUploadSampleBindingDescriptorSetLayout() const noexcept
{
    return mTextureUploadSampleBindingGeneration ? mTextureUploadSampleBindingGeneration->descriptorSetLayout() : VK_NULL_HANDLE;
}

VkPipelineLayout VulkanInstanceGeneration::textureUploadSampleBindingPipelineLayout() const noexcept
{
    return mTextureUploadSampleBindingGeneration ? mTextureUploadSampleBindingGeneration->pipelineLayout() : VK_NULL_HANDLE;
}

VkDescriptorPool VulkanInstanceGeneration::textureUploadSampleBindingDescriptorPool() const noexcept
{
    return mTextureUploadSampleBindingGeneration ? mTextureUploadSampleBindingGeneration->descriptorPool() : VK_NULL_HANDLE;
}

VkDescriptorSet VulkanInstanceGeneration::textureUploadSampleBindingDescriptorSet() const noexcept
{
    return mTextureUploadSampleBindingGeneration ? mTextureUploadSampleBindingGeneration->descriptorSet() : VK_NULL_HANDLE;
}

bool VulkanInstanceGeneration::hasTextureUploadSamplePipelineGeneration() const noexcept
{
    return mTextureUploadSamplePipelineGeneration != nullptr;
}

LLRenderContract::PipelineHandle VulkanInstanceGeneration::textureUploadSamplePipelineResourceHandle() const noexcept
{
    return mTextureUploadSamplePipelineGeneration ? mTextureUploadSamplePipelineGeneration->pipelineResourceHandle()
                                                  : LLRenderContract::PipelineHandle{};
}

VkPipelineLayout VulkanInstanceGeneration::textureUploadSamplePipelineLayout() const noexcept
{
    return mTextureUploadSamplePipelineGeneration ? mTextureUploadSamplePipelineGeneration->pipelineLayout() : VK_NULL_HANDLE;
}

VkPipeline VulkanInstanceGeneration::textureUploadSamplePipeline() const noexcept
{
    return mTextureUploadSamplePipelineGeneration ? mTextureUploadSamplePipelineGeneration->pipeline() : VK_NULL_HANDLE;
}

bool VulkanInstanceGeneration::hasUploadDestinationGeneration() const noexcept
{
    return mUploadDestinationGeneration != nullptr;
}

LLRenderContract::BufferHandle VulkanInstanceGeneration::uploadDestinationResourceHandle() const noexcept
{
    return mUploadDestinationGeneration ? mUploadDestinationGeneration->resourceHandle() : LLRenderContract::BufferHandle{};
}

std::uint64_t VulkanInstanceGeneration::uploadDestinationExpectedContentIdentity() const noexcept
{
    return mUploadDestinationGeneration ? mUploadDestinationGeneration->expectedContentIdentity() : 0;
}

std::uint64_t VulkanInstanceGeneration::uploadDestinationResidentContentIdentity() const noexcept
{
    return mUploadDestinationGeneration ? mUploadDestinationGeneration->residentContentIdentity() : 0;
}

bool VulkanInstanceGeneration::uploadDestinationIsResident() const noexcept
{
    return mUploadDestinationGeneration && mUploadDestinationGeneration->isResident();
}

VkBuffer VulkanInstanceGeneration::uploadDestinationBuffer() const noexcept
{
    return mUploadDestinationGeneration ? mUploadDestinationGeneration->buffer() : VK_NULL_HANDLE;
}

VkDeviceMemory VulkanInstanceGeneration::uploadDestinationMemory() const noexcept
{
    return mUploadDestinationGeneration ? mUploadDestinationGeneration->memory() : VK_NULL_HANDLE;
}

VkDeviceSize VulkanInstanceGeneration::uploadDestinationByteCount() const noexcept
{
    return mUploadDestinationGeneration ? mUploadDestinationGeneration->byteCount() : 0;
}

VkBufferUsageFlags VulkanInstanceGeneration::uploadDestinationUsage() const noexcept
{
    return mUploadDestinationGeneration ? mUploadDestinationGeneration->usage() : 0;
}

VkDeviceSize VulkanInstanceGeneration::uploadDestinationAllocationSize() const noexcept
{
    return mUploadDestinationGeneration ? mUploadDestinationGeneration->allocationSize() : 0;
}

std::uint32_t VulkanInstanceGeneration::uploadDestinationMemoryTypeIndex() const noexcept
{
    return mUploadDestinationGeneration ? mUploadDestinationGeneration->memoryTypeIndex() : 0;
}

VkMemoryPropertyFlags VulkanInstanceGeneration::uploadDestinationMemoryPropertyFlags() const noexcept
{
    return mUploadDestinationGeneration ? mUploadDestinationGeneration->memoryPropertyFlags() : 0;
}

bool VulkanInstanceGeneration::uploadDestinationIsDeviceLocal() const noexcept
{
    return mUploadDestinationGeneration && mUploadDestinationGeneration->isDeviceLocal();
}

bool VulkanInstanceGeneration::uploadDestinationIsMapped() const noexcept
{
    return mUploadDestinationGeneration && mUploadDestinationGeneration->isMapped();
}

bool VulkanInstanceGeneration::hasUploadTransferGeneration() const noexcept
{
    return mUploadTransferGeneration != nullptr;
}

LLRenderContract::BufferHandle VulkanInstanceGeneration::uploadTransferResourceHandle() const noexcept
{
    return mUploadTransferGeneration ? mUploadTransferGeneration->resourceHandle() : LLRenderContract::BufferHandle{};
}

std::uint64_t VulkanInstanceGeneration::uploadTransferContentIdentity() const noexcept
{
    return mUploadTransferGeneration ? mUploadTransferGeneration->contentIdentity() : 0;
}

VkBuffer VulkanInstanceGeneration::uploadTransferSourceBuffer() const noexcept
{
    return mUploadTransferGeneration ? mUploadTransferGeneration->sourceBuffer() : VK_NULL_HANDLE;
}

VkBuffer VulkanInstanceGeneration::uploadTransferDestinationBuffer() const noexcept
{
    return mUploadTransferGeneration ? mUploadTransferGeneration->destinationBuffer() : VK_NULL_HANDLE;
}

VkQueue VulkanInstanceGeneration::uploadTransferQueue() const noexcept
{
    return mUploadTransferGeneration ? mUploadTransferGeneration->queue() : VK_NULL_HANDLE;
}

std::uint32_t VulkanInstanceGeneration::uploadTransferQueueFamilyIndex() const noexcept
{
    return mUploadTransferGeneration ? mUploadTransferGeneration->queueFamilyIndex() : VK_QUEUE_FAMILY_IGNORED;
}

std::uint32_t VulkanInstanceGeneration::uploadTransferQueueIndex() const noexcept
{
    return mUploadTransferGeneration ? mUploadTransferGeneration->queueIndex() : std::numeric_limits<std::uint32_t>::max();
}

VkCommandPool VulkanInstanceGeneration::uploadTransferCommandPool() const noexcept
{
    return mUploadTransferGeneration ? mUploadTransferGeneration->commandPool() : VK_NULL_HANDLE;
}

VkCommandBuffer VulkanInstanceGeneration::uploadTransferCommandBuffer() const noexcept
{
    return mUploadTransferGeneration ? mUploadTransferGeneration->commandBuffer() : VK_NULL_HANDLE;
}

VkFence VulkanInstanceGeneration::uploadTransferFence() const noexcept
{
    return mUploadTransferGeneration ? mUploadTransferGeneration->fence() : VK_NULL_HANDLE;
}

std::uint32_t VulkanInstanceGeneration::uploadTransferSubmissionCount() const noexcept
{
    return mUploadTransferGeneration ? mUploadTransferGeneration->submissionAttemptCount() : 0;
}

std::uint32_t VulkanInstanceGeneration::uploadTransferCompletionWaitCount() const noexcept
{
    return mUploadTransferGeneration ? mUploadTransferGeneration->completionWaitCount() : 0;
}

std::optional<VulkanUploadTransferDisposition> VulkanInstanceGeneration::uploadTransferDisposition() const noexcept
{
    return mUploadTransferGeneration ? std::optional{ mUploadTransferGeneration->disposition() } : std::nullopt;
}

bool VulkanInstanceGeneration::hasSwapchainConfigurationGeneration() const noexcept
{
    return mSwapchainConfigurationGeneration != nullptr;
}

VkExtent2D VulkanInstanceGeneration::swapchainDrawableExtent() const noexcept
{
    return mSwapchainConfigurationGeneration ? mSwapchainConfigurationGeneration->drawableExtent() : VkExtent2D{};
}

VkSurfaceCapabilitiesKHR VulkanInstanceGeneration::swapchainSurfaceCapabilities() const noexcept
{
    return mSwapchainConfigurationGeneration ? mSwapchainConfigurationGeneration->surfaceCapabilities() : VkSurfaceCapabilitiesKHR{};
}

VkSurfaceFormatKHR VulkanInstanceGeneration::swapchainSurfaceFormat() const noexcept
{
    return mSwapchainConfigurationGeneration ? mSwapchainConfigurationGeneration->surfaceFormat() : VkSurfaceFormatKHR{};
}

VkPresentModeKHR VulkanInstanceGeneration::swapchainPresentMode() const noexcept
{
    return mSwapchainConfigurationGeneration ? mSwapchainConfigurationGeneration->presentMode()
                                             : static_cast<VkPresentModeKHR>(VK_PRESENT_MODE_MAX_ENUM_KHR);
}

std::uint32_t VulkanInstanceGeneration::swapchainImageCount() const noexcept
{
    return mSwapchainConfigurationGeneration ? mSwapchainConfigurationGeneration->imageCount() : 0;
}

VkExtent2D VulkanInstanceGeneration::swapchainImageExtent() const noexcept
{
    return mSwapchainConfigurationGeneration ? mSwapchainConfigurationGeneration->imageExtent() : VkExtent2D{};
}

std::uint32_t VulkanInstanceGeneration::swapchainImageArrayLayers() const noexcept
{
    return mSwapchainConfigurationGeneration ? mSwapchainConfigurationGeneration->imageArrayLayers() : 0;
}

VkImageUsageFlags VulkanInstanceGeneration::swapchainImageUsage() const noexcept
{
    return mSwapchainConfigurationGeneration ? mSwapchainConfigurationGeneration->imageUsage() : VkImageUsageFlags{};
}

VkSharingMode VulkanInstanceGeneration::swapchainImageSharingMode() const noexcept
{
    return mSwapchainConfigurationGeneration ? mSwapchainConfigurationGeneration->imageSharingMode()
                                             : static_cast<VkSharingMode>(VK_SHARING_MODE_MAX_ENUM);
}

VkSurfaceTransformFlagBitsKHR VulkanInstanceGeneration::swapchainPreTransform() const noexcept
{
    return mSwapchainConfigurationGeneration ? mSwapchainConfigurationGeneration->preTransform()
                                             : static_cast<VkSurfaceTransformFlagBitsKHR>(0);
}

VkCompositeAlphaFlagBitsKHR VulkanInstanceGeneration::swapchainCompositeAlpha() const noexcept
{
    return mSwapchainConfigurationGeneration ? mSwapchainConfigurationGeneration->compositeAlpha()
                                             : static_cast<VkCompositeAlphaFlagBitsKHR>(0);
}

VkBool32 VulkanInstanceGeneration::swapchainClipped() const noexcept
{
    return mSwapchainConfigurationGeneration ? mSwapchainConfigurationGeneration->clipped() : VK_FALSE;
}

bool VulkanInstanceGeneration::hasSwapchainGeneration() const noexcept
{
    return mSwapchainGeneration != nullptr;
}

VkSwapchainKHR VulkanInstanceGeneration::swapchain() const noexcept
{
    return mSwapchainGeneration ? mSwapchainGeneration->swapchain() : VK_NULL_HANDLE;
}

VkDevice VulkanInstanceGeneration::swapchainDevice() const noexcept
{
    return mSwapchainGeneration ? mSwapchainGeneration->device() : VK_NULL_HANDLE;
}

VkSurfaceKHR VulkanInstanceGeneration::swapchainSurface() const noexcept
{
    return mSwapchainGeneration ? mSwapchainGeneration->surface() : VK_NULL_HANDLE;
}

bool VulkanInstanceGeneration::hasSwapchainImagesGeneration() const noexcept
{
    return mSwapchainImagesGeneration != nullptr;
}

std::uint32_t VulkanInstanceGeneration::resolvedSwapchainImageCount() const noexcept
{
    return mSwapchainImagesGeneration ? mSwapchainImagesGeneration->imageCount() : 0;
}

VkImage VulkanInstanceGeneration::swapchainImage(std::uint32_t index) const noexcept
{
    return mSwapchainImagesGeneration ? mSwapchainImagesGeneration->image(index) : VK_NULL_HANDLE;
}

VkImageView VulkanInstanceGeneration::swapchainImageView(std::uint32_t index) const noexcept
{
    return mSwapchainImagesGeneration ? mSwapchainImagesGeneration->imageView(index) : VK_NULL_HANDLE;
}

bool VulkanInstanceGeneration::hasSwapchainPresentationTargetGeneration() const noexcept
{
    return mSwapchainPresentationTargetGeneration != nullptr;
}

VkRenderPass VulkanInstanceGeneration::swapchainPresentationRenderPass() const noexcept
{
    return mSwapchainPresentationTargetGeneration ? mSwapchainPresentationTargetGeneration->renderPass() : VK_NULL_HANDLE;
}

std::uint32_t VulkanInstanceGeneration::swapchainPresentationFramebufferCount() const noexcept
{
    return mSwapchainPresentationTargetGeneration ? mSwapchainPresentationTargetGeneration->framebufferCount() : 0;
}

VkFramebuffer VulkanInstanceGeneration::swapchainPresentationFramebuffer(std::uint32_t index) const noexcept
{
    return mSwapchainPresentationTargetGeneration ? mSwapchainPresentationTargetGeneration->framebuffer(index) : VK_NULL_HANDLE;
}

bool VulkanInstanceGeneration::hasSwapchainPresentationPipelineGeneration() const noexcept
{
    return mSwapchainPresentationPipelineGeneration != nullptr;
}

VkPipelineLayout VulkanInstanceGeneration::swapchainPresentationPipelineLayout() const noexcept
{
    return mSwapchainPresentationPipelineGeneration ? mSwapchainPresentationPipelineGeneration->pipelineLayout() : VK_NULL_HANDLE;
}

VkPipeline VulkanInstanceGeneration::swapchainPresentationPipeline() const noexcept
{
    return mSwapchainPresentationPipelineGeneration ? mSwapchainPresentationPipelineGeneration->pipeline() : VK_NULL_HANDLE;
}

bool VulkanInstanceGeneration::hasSwapchainReadbackGeneration() const noexcept
{
    return mSwapchainReadbackGeneration != nullptr;
}

VkBuffer VulkanInstanceGeneration::swapchainReadbackBuffer() const noexcept
{
    return mSwapchainReadbackGeneration ? mSwapchainReadbackGeneration->buffer() : VK_NULL_HANDLE;
}

VkDeviceMemory VulkanInstanceGeneration::swapchainReadbackMemory() const noexcept
{
    return mSwapchainReadbackGeneration ? mSwapchainReadbackGeneration->memory() : VK_NULL_HANDLE;
}

bool VulkanInstanceGeneration::swapchainReadbackIsMapped() const noexcept
{
    return mSwapchainReadbackGeneration && mSwapchainReadbackGeneration->isMapped();
}

VkFormat VulkanInstanceGeneration::swapchainReadbackImageFormat() const noexcept
{
    return mSwapchainReadbackGeneration ? mSwapchainReadbackGeneration->imageFormat() : VK_FORMAT_UNDEFINED;
}

VkExtent2D VulkanInstanceGeneration::swapchainReadbackImageExtent() const noexcept
{
    return mSwapchainReadbackGeneration ? mSwapchainReadbackGeneration->imageExtent() : VkExtent2D{};
}

std::uint32_t VulkanInstanceGeneration::swapchainReadbackImageCount() const noexcept
{
    return mSwapchainReadbackGeneration ? mSwapchainReadbackGeneration->imageCount() : 0;
}

VkDeviceSize VulkanInstanceGeneration::swapchainReadbackRowBytes() const noexcept
{
    return mSwapchainReadbackGeneration ? mSwapchainReadbackGeneration->rowBytes() : 0;
}

VkDeviceSize VulkanInstanceGeneration::swapchainReadbackByteCount() const noexcept
{
    return mSwapchainReadbackGeneration ? mSwapchainReadbackGeneration->byteCount() : 0;
}

VkDeviceSize VulkanInstanceGeneration::swapchainReadbackAllocationSize() const noexcept
{
    return mSwapchainReadbackGeneration ? mSwapchainReadbackGeneration->allocationSize() : 0;
}

std::uint32_t VulkanInstanceGeneration::swapchainReadbackMemoryTypeIndex() const noexcept
{
    return mSwapchainReadbackGeneration ? mSwapchainReadbackGeneration->memoryTypeIndex() : 0;
}

VkMemoryPropertyFlags VulkanInstanceGeneration::swapchainReadbackMemoryPropertyFlags() const noexcept
{
    return mSwapchainReadbackGeneration ? mSwapchainReadbackGeneration->memoryPropertyFlags() : 0;
}

bool VulkanInstanceGeneration::hasSwapchainFrameSlotGeneration() const noexcept
{
    return mSwapchainFrameSlotGeneration != nullptr;
}

VkCommandPool VulkanInstanceGeneration::swapchainFrameCommandPool() const noexcept
{
    return mSwapchainFrameSlotGeneration ? mSwapchainFrameSlotGeneration->commandPool() : VK_NULL_HANDLE;
}

VkCommandBuffer VulkanInstanceGeneration::swapchainFrameCommandBuffer() const noexcept
{
    return mSwapchainFrameSlotGeneration ? mSwapchainFrameSlotGeneration->commandBuffer() : VK_NULL_HANDLE;
}

VkSemaphore VulkanInstanceGeneration::swapchainFrameImageAvailableSemaphore() const noexcept
{
    return mSwapchainFrameSlotGeneration ? mSwapchainFrameSlotGeneration->imageAvailableSemaphore() : VK_NULL_HANDLE;
}

VkSemaphore VulkanInstanceGeneration::swapchainFramePresentationReadySemaphore() const noexcept
{
    return mSwapchainFrameSlotGeneration ? mSwapchainFrameSlotGeneration->presentationReadySemaphore() : VK_NULL_HANDLE;
}

VkFence VulkanInstanceGeneration::swapchainFrameSubmissionFence() const noexcept
{
    return mSwapchainFrameSlotGeneration ? mSwapchainFrameSlotGeneration->submissionFence() : VK_NULL_HANDLE;
}

VkFence VulkanInstanceGeneration::swapchainFramePresentCompletionFence() const noexcept
{
    return mSwapchainFrameSlotGeneration ? mSwapchainFrameSlotGeneration->presentCompletionFence() : VK_NULL_HANDLE;
}

std::optional<std::uint32_t> VulkanInstanceGeneration::swapchainFrameAcquiredImageIndex() const noexcept
{
    return mSwapchainFrameSlotGeneration ? mSwapchainFrameSlotGeneration->acquiredImageIndex() : std::nullopt;
}

std::optional<VulkanSwapchainFrameSlotDisposition> VulkanInstanceGeneration::swapchainFrameSlotDisposition() const noexcept
{
    return mSwapchainFrameSlotGeneration ? std::optional{ mSwapchainFrameSlotGeneration->disposition() } : std::nullopt;
}

VulkanSurfaceAcquireResult VulkanInstanceGeneration::acquireSurfaceGeneration(const VulkanSurfaceRequest& request) noexcept
{
    return VulkanInstanceDetail::acquireSurface(*this, request, nullptr);
}

VulkanPresentationDeviceAcquireResult VulkanInstanceGeneration::acquirePresentationDeviceGeneration(
    const VulkanPresentationDeviceRequest& request) noexcept
{
    return VulkanInstanceDetail::acquirePresentationDevice(*this, request, nullptr);
}

VulkanLogicalDeviceAcquireResult VulkanInstanceGeneration::acquireLogicalDeviceGeneration(
    const VulkanLogicalDeviceRequest& request) noexcept
{
    return VulkanInstanceDetail::acquireLogicalDevice(*this, request, nullptr);
}

VulkanUploadSourceAcquireResult VulkanInstanceGeneration::acquireUploadSourceGeneration(const VulkanUploadSourceRequest& request) noexcept
{
    return VulkanInstanceDetail::acquireUploadSource(*this, request, nullptr);
}

VulkanTextureUploadDestinationAcquireResult VulkanInstanceGeneration::acquireTextureUploadDestinationGeneration(
    const VulkanTextureUploadDestinationRequest& request) noexcept
{
    return VulkanInstanceDetail::acquireTextureUploadDestination(*this, request, nullptr);
}

VulkanTextureUploadSourceAcquireResult VulkanInstanceGeneration::acquireTextureUploadSourceGeneration(
    const VulkanTextureUploadSourceRequest& request) noexcept
{
    return VulkanInstanceDetail::acquireTextureUploadSource(*this, request, nullptr);
}

VulkanTextureUploadTransferAcquireResult VulkanInstanceGeneration::acquireTextureUploadTransferGeneration(
    const VulkanTextureUploadTransferRequest& request) noexcept
{
    return VulkanInstanceDetail::acquireTextureUploadTransfer(*this, request, nullptr);
}

VulkanTextureUploadTransferParentOperationResult VulkanInstanceGeneration::executeTextureUploadTransfer(
    const VulkanTextureUploadTransferOperationRequest& request) noexcept
{
    return VulkanInstanceGenerationFactory::operateTextureUploadTransfer(*this, request, TextureUploadTransferParentOperation::Execute);
}

VulkanTextureUploadTransferParentOperationResult VulkanInstanceGeneration::retryTextureUploadTransferCompletion(
    const VulkanTextureUploadTransferOperationRequest& request) noexcept
{
    return VulkanInstanceGenerationFactory::operateTextureUploadTransfer(*this, request,
                                                                         TextureUploadTransferParentOperation::RetryCompletion);
}

VulkanTextureUploadSampleBindingAcquireResult VulkanInstanceGeneration::acquireTextureUploadSampleBindingGeneration(
    const VulkanTextureUploadSampleBindingRequest& request) noexcept
{
    return VulkanInstanceDetail::acquireTextureUploadSampleBinding(*this, request, nullptr);
}

VulkanTextureUploadSamplePipelineAcquireResult VulkanInstanceGeneration::acquireTextureUploadSamplePipelineGeneration(
    const VulkanTextureUploadSamplePipelineRequest& request) noexcept
{
    return VulkanInstanceDetail::acquireTextureUploadSamplePipeline(*this, request, nullptr);
}

VulkanUploadDestinationAcquireResult VulkanInstanceGeneration::acquireUploadDestinationGeneration(
    const VulkanUploadDestinationRequest& request) noexcept
{
    return VulkanInstanceDetail::acquireUploadDestination(*this, request, nullptr);
}

VulkanUploadTransferAcquireResult VulkanInstanceGeneration::acquireUploadTransferGeneration(
    const VulkanUploadTransferRequest& request) noexcept
{
    return VulkanInstanceDetail::acquireUploadTransfer(*this, request, nullptr);
}

VulkanUploadTransferParentOperationResult VulkanInstanceGeneration::executeUploadTransfer(
    const VulkanUploadTransferOperationRequest& request) noexcept
{
    return VulkanInstanceGenerationFactory::operateUploadTransfer(*this, request, UploadTransferParentOperation::Execute);
}

VulkanUploadTransferParentOperationResult VulkanInstanceGeneration::retryUploadTransferCompletion(
    const VulkanUploadTransferOperationRequest& request) noexcept
{
    return VulkanInstanceGenerationFactory::operateUploadTransfer(*this, request, UploadTransferParentOperation::RetryCompletion);
}

VulkanSwapchainConfigurationAcquireResult VulkanInstanceGeneration::acquireSwapchainConfigurationGeneration(
    const VulkanSwapchainConfigurationRequest& request) noexcept
{
    return VulkanInstanceDetail::acquireSwapchainConfiguration(*this, request, nullptr);
}

VulkanSwapchainAcquireResult VulkanInstanceGeneration::acquireSwapchainGeneration(const VulkanSwapchainRequest& request) noexcept
{
    return VulkanInstanceDetail::acquireSwapchain(*this, request, nullptr);
}

VulkanSwapchainImagesAcquireResult VulkanInstanceGeneration::acquireSwapchainImagesGeneration(
    const VulkanSwapchainImagesRequest& request) noexcept
{
    return VulkanInstanceDetail::acquireSwapchainImages(*this, request, nullptr);
}

VulkanSwapchainPresentationTargetAcquireResult VulkanInstanceGeneration::acquireSwapchainPresentationTargetGeneration(
    const VulkanSwapchainPresentationTargetRequest& request) noexcept
{
    return VulkanInstanceDetail::acquireSwapchainPresentationTarget(*this, request, nullptr);
}

VulkanSwapchainPresentationPipelineAcquireResult VulkanInstanceGeneration::acquireSwapchainPresentationPipelineGeneration(
    const VulkanSwapchainPresentationPipelineRequest& request) noexcept
{
    return VulkanInstanceDetail::acquireSwapchainPresentationPipeline(*this, request, nullptr);
}

VulkanSwapchainReadbackAcquireResult VulkanInstanceGeneration::acquireSwapchainReadbackGeneration(
    const VulkanSwapchainReadbackRequest& request) noexcept
{
    return VulkanInstanceDetail::acquireSwapchainReadback(*this, request, nullptr);
}

VulkanSwapchainFrameSlotAcquireResult VulkanInstanceGeneration::acquireSwapchainFrameSlotGeneration(
    const VulkanSwapchainFrameSlotRequest& request) noexcept
{
    return VulkanInstanceDetail::acquireSwapchainFrameSlot(*this, request, nullptr);
}

VulkanSwapchainChainRebuildResult VulkanInstanceGeneration::rebuildSwapchainChain(
    const VulkanSwapchainChainRebuildRequest& request) noexcept
{
    return VulkanInstanceDetail::rebuildSwapchainChain(*this, request, nullptr);
}

VulkanSwapchainFrameSlotParentOperationResult VulkanInstanceGeneration::roundTripEmptySwapchainFrameSlot(
    const VulkanSwapchainFrameSlotOperationRequest& request) noexcept
{
    return VulkanInstanceGenerationFactory::roundTripEmptySwapchainFrameSlot(*this, request);
}

VulkanSwapchainFrameSlotParentOperationResult VulkanInstanceGeneration::retryEmptySwapchainFrameSlotCompletion(
    const VulkanSwapchainFrameSlotOperationRequest& request) noexcept
{
    return VulkanInstanceGenerationFactory::retryEmptySwapchainFrameSlotCompletion(*this, request);
}

VulkanSwapchainFrameSlotParentPresentationResult VulkanInstanceGeneration::acquireToPresentSwapchainFrameSlot(
    const VulkanSwapchainFrameSlotOperationRequest& request) noexcept
{
    return VulkanInstanceGenerationFactory::acquireToPresentSwapchainFrameSlot(*this, request);
}

VulkanSwapchainFrameSlotParentPresentationResult VulkanInstanceGeneration::acquireClearToPresentSwapchainFrameSlot(
    const VulkanSwapchainFrameSlotOperationRequest& request,
    const VulkanSwapchainFrameClearColor&           clear_color) noexcept
{
    return VulkanInstanceGenerationFactory::acquireClearToPresentSwapchainFrameSlot(*this, request, clear_color);
}

VulkanSwapchainFrameSlotParentPresentationResult VulkanInstanceGeneration::acquireRenderPassClearToPresentSwapchainFrameSlot(
    const VulkanSwapchainFrameSlotOperationRequest& request,
    const VulkanSwapchainFrameClearColor&           clear_color) noexcept
{
    return VulkanInstanceGenerationFactory::acquireRenderPassClearToPresentSwapchainFrameSlot(*this, request, clear_color);
}

VulkanSwapchainFrameSlotParentPresentationResult VulkanInstanceGeneration::acquireRenderPassDrawToPresentSwapchainFrameSlot(
    const VulkanSwapchainFrameSlotOperationRequest& request,
    const VulkanSwapchainFrameClearColor&           clear_color) noexcept
{
    return VulkanInstanceGenerationFactory::acquireRenderPassDrawToPresentSwapchainFrameSlot(*this, request, clear_color);
}

VulkanSwapchainFrameSlotParentPresentationResult VulkanInstanceGeneration::acquireRenderPassDrawReadbackToPresentSwapchainFrameSlot(
    const VulkanSwapchainFrameSlotOperationRequest& request) noexcept
{
    return VulkanInstanceGenerationFactory::acquireRenderPassDrawReadbackToPresentSwapchainFrameSlot(*this, request);
}

VulkanSwapchainFrameSlotParentPresentationResult VulkanInstanceGeneration::retrySwapchainFrameSlotPresentation(
    const VulkanSwapchainFrameSlotOperationRequest& request) noexcept
{
    return VulkanInstanceGenerationFactory::retrySwapchainFrameSlotPresentation(*this, request);
}

VulkanSwapchainFrameSlotParentPresentationResult VulkanInstanceGeneration::retrySwapchainFrameSlotPresentationCompletion(
    const VulkanSwapchainFrameSlotOperationRequest& request) noexcept
{
    return VulkanInstanceGenerationFactory::retrySwapchainFrameSlotPresentationCompletion(*this, request);
}

VulkanSwapchainFrameSlotParentOperationResult VulkanInstanceGeneration::cancelSwapchainFrameSlotPresentation(
    const VulkanSwapchainFrameSlotOperationRequest& request) noexcept
{
    return VulkanInstanceGenerationFactory::cancelSwapchainFrameSlotPresentation(*this, request);
}

VulkanSwapchainFrameSlotParentOperationResult VulkanInstanceGeneration::retrySwapchainFrameSlotCancellationCompletion(
    const VulkanSwapchainFrameSlotOperationRequest& request) noexcept
{
    return VulkanInstanceGenerationFactory::retrySwapchainFrameSlotCancellationCompletion(*this, request);
}

bool VulkanInstanceGeneration::resetSwapchainFrameSlotGeneration() noexcept
{
    if (mNativeAcquisitionDepth != 0)
    {
        return false;
    }
    if (mSwapchainFrameSlotGeneration && mSwapchainFrameSlotGeneration->hasRetainedUploadDestinationGeneration())
    {
        return false;
    }
    if (mSwapchainFrameSlotGeneration && mSwapchainReadbackGeneration &&
        mSwapchainFrameSlotGeneration->retainsReadbackGeneration(*mSwapchainReadbackGeneration))
    {
        return false;
    }
    if (mSwapchainFrameSlotGeneration && !frameSlotDispositionAllowsReset(mSwapchainFrameSlotGeneration->disposition()))
    {
        return false;
    }
    if (mSwapchainFrameSlotGeneration)
    {
        mSwapchainFrameSlotGeneration.reset();
        noteSwapchainFrameSlotTransition();
    }
    return true;
}

bool VulkanInstanceGeneration::resetSwapchainReadbackGeneration() noexcept
{
    if (mNativeAcquisitionDepth != 0)
    {
        return false;
    }
    if (mSwapchainReadbackGeneration && mSwapchainFrameSlotGeneration &&
        mSwapchainFrameSlotGeneration->retainsReadbackGeneration(*mSwapchainReadbackGeneration))
    {
        return false;
    }
    if (mSwapchainReadbackGeneration)
    {
        mSwapchainReadbackGeneration.reset();
        noteSwapchainReadbackTransition();
    }
    return true;
}

bool VulkanInstanceGeneration::resetSwapchainPresentationTargetGeneration() noexcept
{
    if (mNativeAcquisitionDepth != 0)
    {
        return false;
    }
    if (mSwapchainFrameSlotGeneration && mSwapchainFrameSlotGeneration->hasRetainedUploadDestinationGeneration())
    {
        return false;
    }
    if (mSwapchainFrameSlotGeneration && mSwapchainReadbackGeneration &&
        mSwapchainFrameSlotGeneration->retainsReadbackGeneration(*mSwapchainReadbackGeneration))
    {
        return false;
    }
    if (mSwapchainFrameSlotGeneration && !frameSlotDispositionAllowsReset(mSwapchainFrameSlotGeneration->disposition()))
    {
        return false;
    }

    auto retiring_frame_slot = std::move(mSwapchainFrameSlotGeneration);
    if (retiring_frame_slot)
    {
        noteSwapchainFrameSlotTransition();
    }

    SwapchainPresentationTargetTeardownGuard target_guard(*this);
    retiring_frame_slot.reset();

    auto retiring_target = std::move(mSwapchainPresentationTargetGeneration);
    if (retiring_target)
    {
        noteSwapchainPresentationTargetTransition();
    }

    auto retiring_sample_pipeline = std::move(mTextureUploadSamplePipelineGeneration);
    if (retiring_sample_pipeline)
    {
        noteTextureUploadSamplePipelineTransition();
        TextureUploadSamplePipelineTeardownGuard sample_guard(*this);
        retiring_sample_pipeline.reset();
    }

    auto retiring_presentation_pipeline = std::move(mSwapchainPresentationPipelineGeneration);
    if (retiring_presentation_pipeline)
    {
        noteSwapchainPresentationPipelineTransition();
    }
    retiring_presentation_pipeline.reset();
    retiring_target.reset();
    return true;
}

bool VulkanInstanceGeneration::resetSwapchainPresentationPipelineGeneration() noexcept
{
    if (mNativeAcquisitionDepth != 0)
    {
        return false;
    }
    if (!resetSwapchainFrameSlotGeneration())
    {
        return false;
    }
    if (mSwapchainPresentationPipelineGeneration)
    {
        mSwapchainPresentationPipelineGeneration.reset();
        noteSwapchainPresentationPipelineTransition();
    }
    return true;
}

bool VulkanInstanceGeneration::resetSwapchainImagesGeneration() noexcept
{
    if (mNativeAcquisitionDepth != 0)
    {
        return false;
    }
    if (!resetSwapchainFrameSlotGeneration())
    {
        return false;
    }
    if (!resetSwapchainReadbackGeneration())
    {
        return false;
    }
    if (!resetSwapchainPresentationTargetGeneration())
    {
        return false;
    }
    if (mSwapchainImagesGeneration)
    {
        mSwapchainImagesGeneration.reset();
        noteOwnershipTransition();
    }
    return true;
}

bool VulkanInstanceGeneration::resetSwapchainGeneration() noexcept
{
    if (mNativeAcquisitionDepth != 0)
    {
        return false;
    }
    if (!resetSwapchainImagesGeneration())
    {
        return false;
    }
    if (mSwapchainGeneration)
    {
        mSwapchainGeneration.reset();
        noteOwnershipTransition();
    }
    return true;
}

bool VulkanInstanceGeneration::resetSwapchainConfigurationGeneration() noexcept
{
    if (mNativeAcquisitionDepth != 0)
    {
        return false;
    }
    if (!resetSwapchainGeneration())
    {
        return false;
    }
    if (mSwapchainConfigurationGeneration)
    {
        mSwapchainConfigurationGeneration.reset();
        noteOwnershipTransition();
    }
    return true;
}

bool VulkanInstanceGeneration::resetUploadTransferGeneration() noexcept
{
    if (mNativeAcquisitionDepth != 0)
    {
        return false;
    }
    if (mUploadTransferGeneration && !mUploadTransferGeneration->reset())
    {
        return false;
    }
    if (mUploadTransferGeneration)
    {
        mUploadTransferGeneration.reset();
        noteUploadTransferTransition();
    }
    return true;
}

bool VulkanInstanceGeneration::resetUploadDestinationGeneration() noexcept
{
    if (mNativeAcquisitionDepth != 0)
    {
        return false;
    }
    if (mSwapchainFrameSlotGeneration && mSwapchainFrameSlotGeneration->hasRetainedUploadDestinationGeneration())
    {
        return false;
    }
    if (!resetUploadTransferGeneration())
    {
        return false;
    }
    if (mUploadDestinationGeneration)
    {
        mUploadDestinationGeneration.reset();
        noteUploadDestinationTransition();
    }
    return true;
}

bool VulkanInstanceGeneration::resetUploadSourceGeneration() noexcept
{
    if (mNativeAcquisitionDepth != 0)
    {
        return false;
    }
    if (!resetUploadTransferGeneration())
    {
        return false;
    }
    if (mUploadSourceGeneration)
    {
        mUploadSourceGeneration.reset();
        noteUploadSourceTransition();
    }
    return true;
}

bool VulkanInstanceGeneration::resetTextureUploadDestinationGeneration() noexcept
{
    if (mNativeAcquisitionDepth != 0)
    {
        return false;
    }
    // Refuse a pending transfer before detaching the independent sampled
    // binding or any texture-upload owner.
    if (mTextureUploadTransferGeneration &&
        mTextureUploadTransferGeneration->disposition() == VulkanTextureUploadTransferDisposition::Pending)
    {
        return false;
    }
    if (!resetTextureUploadSampleBindingGeneration() || !resetTextureUploadTransferGeneration() || !resetTextureUploadSourceGeneration())
    {
        return false;
    }
    if (mTextureUploadDestinationGeneration)
    {
        auto retiring = std::move(mTextureUploadDestinationGeneration);
        noteTextureUploadDestinationTransition();
        TextureUploadDestinationTeardownGuard teardown_guard(*this);
        retiring.reset();
    }
    return true;
}

bool VulkanInstanceGeneration::resetTextureUploadSourceGeneration() noexcept
{
    if (mNativeAcquisitionDepth != 0)
    {
        return false;
    }
    if (!resetTextureUploadTransferGeneration())
    {
        return false;
    }
    if (mTextureUploadSourceGeneration)
    {
        auto retiring = std::move(mTextureUploadSourceGeneration);
        noteTextureUploadSourceTransition();
        TextureUploadSourceTeardownGuard teardown_guard(*this);
        retiring.reset();
    }
    return true;
}

bool VulkanInstanceGeneration::resetTextureUploadTransferGeneration() noexcept
{
    if (mNativeAcquisitionDepth != 0)
    {
        return false;
    }
    if (mTextureUploadTransferGeneration &&
        mTextureUploadTransferGeneration->disposition() == VulkanTextureUploadTransferDisposition::Pending)
    {
        return false;
    }
    if (mTextureUploadTransferGeneration)
    {
        auto retiring = std::move(mTextureUploadTransferGeneration);
        noteTextureUploadTransferTransition();
        TextureUploadTransferTeardownGuard teardown_guard(*this);
        retiring->reset();
    }
    return true;
}

bool VulkanInstanceGeneration::resetTextureUploadSampleBindingGeneration() noexcept
{
    if (mNativeAcquisitionDepth != 0)
    {
        return false;
    }
    if (!resetTextureUploadSamplePipelineGeneration())
    {
        return false;
    }
    if (mTextureUploadSampleBindingGeneration)
    {
        auto retiring = std::move(mTextureUploadSampleBindingGeneration);
        noteTextureUploadSampleBindingTransition();
        TextureUploadSampleBindingTeardownGuard teardown_guard(*this);
        retiring->reset();
    }
    return true;
}

bool VulkanInstanceGeneration::resetTextureUploadSamplePipelineGeneration() noexcept
{
    if (mNativeAcquisitionDepth != 0)
    {
        return false;
    }
    if (mTextureUploadSamplePipelineGeneration)
    {
        auto retiring = std::move(mTextureUploadSamplePipelineGeneration);
        noteTextureUploadSamplePipelineTransition();
        TextureUploadSamplePipelineTeardownGuard teardown_guard(*this);
        retiring->reset();
    }
    return true;
}

bool VulkanInstanceGeneration::resetLogicalDeviceGeneration() noexcept
{
    if (mNativeAcquisitionDepth != 0)
    {
        return false;
    }
    // Preflight the device-scoped pending obligation before touching the
    // independent swapchain chain.
    if ((mUploadTransferGeneration && mUploadTransferGeneration->disposition() == VulkanUploadTransferDisposition::Pending) ||
        (mTextureUploadTransferGeneration &&
         mTextureUploadTransferGeneration->disposition() == VulkanTextureUploadTransferDisposition::Pending))
    {
        return false;
    }
    if (!resetSwapchainConfigurationGeneration())
    {
        return false;
    }
    if (!resetTextureUploadSampleBindingGeneration())
    {
        return false;
    }
    if (!resetTextureUploadTransferGeneration() || !resetTextureUploadSourceGeneration())
    {
        return false;
    }
    if (!resetTextureUploadDestinationGeneration())
    {
        return false;
    }
    if (!resetUploadTransferGeneration())
    {
        return false;
    }
    if (!resetUploadDestinationGeneration())
    {
        return false;
    }
    if (!resetUploadSourceGeneration())
    {
        return false;
    }
    if (mLogicalDeviceGeneration)
    {
        mLogicalDeviceGeneration.reset();
        noteOwnershipTransition();
    }
    return true;
}

bool VulkanInstanceGeneration::resetPresentationDeviceGeneration() noexcept
{
    if (mNativeAcquisitionDepth != 0)
    {
        return false;
    }
    if (!resetLogicalDeviceGeneration())
    {
        return false;
    }
    if (mPresentationDeviceGeneration)
    {
        mPresentationDeviceGeneration.reset();
        noteOwnershipTransition();
    }
    return true;
}

bool VulkanInstanceGeneration::resetSurfaceGeneration() noexcept
{
    if (mNativeAcquisitionDepth != 0)
    {
        return false;
    }
    if (!resetPresentationDeviceGeneration())
    {
        return false;
    }
    if (mSurfaceGeneration)
    {
        mSurfaceGeneration.reset();
        noteOwnershipTransition();
    }
    return true;
}

bool VulkanInstanceGeneration::reset() noexcept
{
    if (mNativeAcquisitionDepth != 0)
    {
        return false;
    }
    if (!resetSurfaceGeneration())
    {
        return false;
    }

    const bool had_debug_messenger = mDebugMessenger != VK_NULL_HANDLE;
    if (had_debug_messenger && mDestroyDebugMessenger)
    {
        mDestroyDebugMessenger(mInstance, mDebugMessenger, nullptr);
    }
    mDebugMessenger        = VK_NULL_HANDLE;
    mDestroyDebugMessenger = nullptr;
    if (had_debug_messenger)
    {
        noteOwnershipTransition();
    }

    const bool had_instance = mInstance != VK_NULL_HANDLE;
    if (had_instance && mDestroyInstance)
    {
        mDestroyInstance(mInstance, nullptr);
    }
    mInstance        = VK_NULL_HANDLE;
    mDestroyInstance = nullptr;
    if (had_instance)
    {
        noteOwnershipTransition();
    }

    const bool had_global_dispatch = mGlobalDispatch.has_value();
    mGlobalDispatch.reset();
    if (had_global_dispatch)
    {
        noteOwnershipTransition();
    }
    mEnabledExtensions.clear();
    mEnabledLayers.clear();
    mNativeWindowGeneration = 0;
    mPortabilityEnumeration = false;
    mValidationState.reset();
    return true;
}

VulkanSurfaceAcquireResult VulkanInstanceGenerationFactory::acquireSurface(
    VulkanInstanceGeneration&                  instance_generation,
    const VulkanSurfaceRequest&                request,
    VulkanInstanceDetail::AllocationCheckpoint allocation_checkpoint) noexcept
{
    if (!request.mInstanceOwnerCheck.mIsCurrent)
    {
        return surfaceFailure(VulkanSurfaceAcquireCode::InvalidInstanceOwnerCheck);
    }
    if (!request.mWindowGenerationCheck.mIsCurrent)
    {
        return surfaceFailure(VulkanSurfaceAcquireCode::InvalidWindowGenerationCheck);
    }
    if (!request.mCreateOperation.mCreate)
    {
        return surfaceFailure(VulkanSurfaceAcquireCode::InvalidCreateOperation);
    }
    if (request.mNativeWindowGeneration == 0)
    {
        return surfaceFailure(VulkanSurfaceAcquireCode::InvalidNativeWindowGeneration);
    }
    if (instance_generation.mInstance == VK_NULL_HANDLE || !instance_generation.mDestroyInstance || !instance_generation.mGlobalDispatch ||
        instance_generation.mNativeWindowGeneration == 0)
    {
        return surfaceFailure(VulkanSurfaceAcquireCode::InstanceNotLive);
    }
    if (instance_generation.mSurfaceGeneration)
    {
        return surfaceFailure(VulkanSurfaceAcquireCode::SurfaceAlreadyOwned);
    }
    if (request.mNativeWindowGeneration != instance_generation.mNativeWindowGeneration)
    {
        return surfaceFailure(VulkanSurfaceAcquireCode::NativeWindowGenerationMismatch);
    }

    const std::uint64_t acquisition_epoch = instance_generation.mOwnershipTransitionEpoch;
    const auto freshness_check = [&]() noexcept
    {
        return surfaceFreshness(request,
                                instance_generation,
                                &instance_generation.mOwnershipTransitionEpoch,
                                acquisition_epoch);
    };

    // Boundary 1: the exact instance owner and native window must both still
    // own the generation before its extension policy is consulted.
    if (VulkanSurfaceAcquireResult freshness = freshness_check())
    {
        return freshness;
    }
    if (!instance_generation.isExtensionEnabled(VK_KHR_SURFACE_EXTENSION_NAME))
    {
        return surfaceFailure(VulkanSurfaceAcquireCode::MissingSurfaceExtension);
    }

    // Boundary 2: do not consult the loader after either owner becomes stale.
    if (VulkanSurfaceAcquireResult freshness = freshness_check())
    {
        return freshness;
    }
    VulkanInstanceGeneration::NativeAcquisitionGuard native_guard(instance_generation);
    const PFN_vkDestroySurfaceKHR destroy_surface = resolveInstanceCommand<PFN_vkDestroySurfaceKHR>(
        instance_generation.mGlobalDispatch->getInstanceProcAddr(), instance_generation.mInstance, "vkDestroySurfaceKHR");
    if (!destroy_surface)
    {
        return surfaceFailure(VulkanSurfaceAcquireCode::MissingRequiredInstanceCommand, std::nullopt, VulkanSurfaceCommand::DestroySurface);
    }

    // Boundary 3: destruction capability is available, but no platform object
    // exists yet.
    if (VulkanSurfaceAcquireResult freshness = freshness_check())
    {
        return freshness;
    }

    try
    {
        if (allocation_checkpoint)
        {
            allocation_checkpoint();
            if (instance_generation.mSurfaceGeneration)
            {
                return surfaceFailure(VulkanSurfaceAcquireCode::SurfaceAlreadyOwned);
            }
            if (instance_generation.mOwnershipTransitionEpoch != acquisition_epoch)
            {
                return surfaceFailure(VulkanSurfaceAcquireCode::InstanceNotLive);
            }
        }
        auto pending = std::make_unique<VulkanInstanceGeneration::VulkanSurfaceGeneration>();

        // Boundary 4: allocation cannot leave a created surface without an
        // owner, and freshness is checked once more immediately before create.
        if (VulkanSurfaceAcquireResult freshness = freshness_check())
        {
            return freshness;
        }

        VkSurfaceKHR                     surface = VK_NULL_HANDLE;
        const VulkanSurfaceCreateOutcome outcome =
            request.mCreateOperation.mCreate(request.mCreateOperation.mUserdata, instance_generation.mInstance, nullptr, &surface);
        if (std::holds_alternative<VulkanSurfacePlatformFailure>(outcome))
        {
            return surfaceFailure(VulkanSurfaceAcquireCode::PlatformCreationFailure);
        }

        const VkResult result = std::get<VkResult>(outcome);
        if (result != VK_SUCCESS)
        {
            return surfaceFailure(VulkanSurfaceAcquireCode::SurfaceCreationFailure, result);
        }
        if (surface == VK_NULL_HANDLE)
        {
            return surfaceFailure(VulkanSurfaceAcquireCode::NullSurfaceOnSuccess, VK_SUCCESS);
        }

        pending->adopt(instance_generation.mInstance, surface, destroy_surface, request.mNativeWindowGeneration);

        // Boundary 5: the pending child owns rollback before either freshness
        // callback can reject publication.
        if (VulkanSurfaceAcquireResult freshness = freshness_check())
        {
            return freshness;
        }

        instance_generation.mSurfaceGeneration = std::move(pending);
        instance_generation.noteOwnershipTransition();
        return std::nullopt;
    }
    catch (const std::bad_alloc&)
    {
        return surfaceFailure(VulkanSurfaceAcquireCode::AllocationFailure);
    }
}

VulkanPresentationDeviceAcquireResult VulkanInstanceGenerationFactory::acquirePresentationDevice(
    VulkanInstanceGeneration&                  instance_generation,
    const VulkanPresentationDeviceRequest&     request,
    VulkanInstanceDetail::AllocationCheckpoint allocation_checkpoint) noexcept
{
    if (!request.mInstanceOwnerCheck.mIsCurrent)
    {
        return presentationDeviceFailure(VulkanPresentationDeviceAcquireCode::InvalidInstanceOwnerCheck);
    }
    if (!request.mWindowGenerationCheck.mIsCurrent)
    {
        return presentationDeviceFailure(VulkanPresentationDeviceAcquireCode::InvalidWindowGenerationCheck);
    }
    if (request.mNativeWindowGeneration == 0)
    {
        return presentationDeviceFailure(VulkanPresentationDeviceAcquireCode::InvalidNativeWindowGeneration);
    }
    if (instance_generation.mInstance == VK_NULL_HANDLE || !instance_generation.mDestroyInstance || !instance_generation.mGlobalDispatch ||
        instance_generation.mNativeWindowGeneration == 0)
    {
        return presentationDeviceFailure(VulkanPresentationDeviceAcquireCode::InstanceNotLive);
    }
    if (!instance_generation.mSurfaceGeneration || instance_generation.surface() == VK_NULL_HANDLE)
    {
        return presentationDeviceFailure(VulkanPresentationDeviceAcquireCode::SurfaceNotLive);
    }
    if (instance_generation.mPresentationDeviceGeneration)
    {
        return presentationDeviceFailure(VulkanPresentationDeviceAcquireCode::PresentationDeviceAlreadyOwned);
    }
    if (request.mNativeWindowGeneration != instance_generation.mNativeWindowGeneration ||
        request.mNativeWindowGeneration != instance_generation.surfaceNativeWindowGeneration())
    {
        return presentationDeviceFailure(VulkanPresentationDeviceAcquireCode::NativeWindowGenerationMismatch);
    }
    const std::uint64_t acquisition_epoch = instance_generation.mOwnershipTransitionEpoch;
    const auto freshness_check = [&]() noexcept
    {
        return presentationDeviceFreshness(request,
                                           instance_generation,
                                           &instance_generation.mOwnershipTransitionEpoch,
                                           acquisition_epoch);
    };
    if (VulkanPresentationDeviceAcquireResult freshness = freshness_check())
    {
        return freshness;
    }
    VulkanInstanceGeneration::NativeAcquisitionGuard native_guard(instance_generation);

    const VkInstance            instance = instance_generation.mInstance;
    const VkSurfaceKHR          surface  = instance_generation.surface();
    VulkanPhysicalDeviceRequest selection_request;
    selection_request.mGetInstanceProcAddr = instance_generation.mGlobalDispatch->getInstanceProcAddr();
    selection_request.mInstance            = instance;
    selection_request.mSurface             = surface;

    VulkanPhysicalDeviceResolutionResult selection_result = resolveVulkanPhysicalDeviceGeneration(selection_request);
    if (const auto* error = std::get_if<VulkanPhysicalDeviceResolutionError>(&selection_result))
    {
        return presentationDeviceFailure(VulkanPresentationDeviceAcquireCode::ResolutionFailure, *error);
    }

    try
    {
        if (allocation_checkpoint)
        {
            allocation_checkpoint();
            if (instance_generation.mPresentationDeviceGeneration)
            {
                return presentationDeviceFailure(VulkanPresentationDeviceAcquireCode::PresentationDeviceAlreadyOwned);
            }
            if (instance_generation.mOwnershipTransitionEpoch != acquisition_epoch)
            {
                return presentationDeviceFailure(VulkanPresentationDeviceAcquireCode::SurfaceNotLive);
            }
        }
        auto pending =
            std::make_unique<VulkanPhysicalDeviceGeneration>(std::move(std::get<VulkanPhysicalDeviceGeneration>(selection_result)));

        if (VulkanPresentationDeviceAcquireResult freshness = freshness_check())
        {
            return freshness;
        }
        if (instance_generation.mInstance != instance || !instance_generation.mSurfaceGeneration ||
            instance_generation.surface() != surface || !pending->selectedFor(instance_generation.mInstance, instance_generation.surface()))
        {
            return presentationDeviceFailure(VulkanPresentationDeviceAcquireCode::SurfaceNotLive);
        }

        instance_generation.mPresentationDeviceGeneration = std::move(pending);
        instance_generation.noteOwnershipTransition();
        return std::nullopt;
    }
    catch (const std::bad_alloc&)
    {
        return presentationDeviceFailure(VulkanPresentationDeviceAcquireCode::AllocationFailure);
    }
}

VulkanLogicalDeviceAcquireResult VulkanInstanceGenerationFactory::acquireLogicalDevice(
    VulkanInstanceGeneration&                  instance_generation,
    const VulkanLogicalDeviceRequest&          request,
    VulkanInstanceDetail::AllocationCheckpoint allocation_checkpoint) noexcept
{
    if (!request.mInstanceOwnerCheck.mIsCurrent)
    {
        return logicalDeviceFailure(VulkanLogicalDeviceAcquireCode::InvalidInstanceOwnerCheck);
    }
    if (!request.mWindowGenerationCheck.mIsCurrent)
    {
        return logicalDeviceFailure(VulkanLogicalDeviceAcquireCode::InvalidWindowGenerationCheck);
    }
    if (request.mNativeWindowGeneration == 0)
    {
        return logicalDeviceFailure(VulkanLogicalDeviceAcquireCode::InvalidNativeWindowGeneration);
    }
    if (instance_generation.mInstance == VK_NULL_HANDLE || !instance_generation.mDestroyInstance || !instance_generation.mGlobalDispatch ||
        instance_generation.mNativeWindowGeneration == 0)
    {
        return logicalDeviceFailure(VulkanLogicalDeviceAcquireCode::InstanceNotLive);
    }
    if (!instance_generation.mSurfaceGeneration || instance_generation.surface() == VK_NULL_HANDLE)
    {
        return logicalDeviceFailure(VulkanLogicalDeviceAcquireCode::SurfaceNotLive);
    }
    if (!instance_generation.mPresentationDeviceGeneration || instance_generation.physicalDevice() == VK_NULL_HANDLE)
    {
        return logicalDeviceFailure(VulkanLogicalDeviceAcquireCode::PresentationDeviceNotLive);
    }
    if (instance_generation.mLogicalDeviceGeneration)
    {
        return logicalDeviceFailure(VulkanLogicalDeviceAcquireCode::LogicalDeviceAlreadyOwned);
    }
    if (request.mNativeWindowGeneration != instance_generation.mNativeWindowGeneration ||
        request.mNativeWindowGeneration != instance_generation.surfaceNativeWindowGeneration())
    {
        return logicalDeviceFailure(VulkanLogicalDeviceAcquireCode::NativeWindowGenerationMismatch);
    }
    const std::uint64_t acquisition_epoch = instance_generation.mOwnershipTransitionEpoch;
    const auto freshness_check = [&]() noexcept
    {
        return logicalDeviceFreshness(request,
                                      instance_generation,
                                      &instance_generation.mOwnershipTransitionEpoch,
                                      acquisition_epoch);
    };
    if (VulkanLogicalDeviceAcquireResult freshness = freshness_check())
    {
        return freshness;
    }
    VulkanInstanceGeneration::NativeAcquisitionGuard native_guard(instance_generation);

    const VkInstance                      instance        = instance_generation.mInstance;
    const VkSurfaceKHR                    surface         = instance_generation.surface();
    const VulkanPhysicalDeviceGeneration* selection       = instance_generation.mPresentationDeviceGeneration.get();
    const VkPhysicalDevice                physical_device = selection->physicalDevice();
    const std::uint32_t                   queue_family    = selection->queueFamilyIndex();

    VulkanLogicalDeviceResolutionResult resolution_result = resolveVulkanLogicalDeviceGeneration(*selection);
    if (const auto* error = std::get_if<VulkanLogicalDeviceResolutionError>(&resolution_result))
    {
        return logicalDeviceFailure(VulkanLogicalDeviceAcquireCode::ResolutionFailure, *error);
    }

    try
    {
        if (allocation_checkpoint)
        {
            allocation_checkpoint();
            if (instance_generation.mLogicalDeviceGeneration)
            {
                return logicalDeviceFailure(VulkanLogicalDeviceAcquireCode::LogicalDeviceAlreadyOwned);
            }
            if (instance_generation.mOwnershipTransitionEpoch != acquisition_epoch)
            {
                return logicalDeviceFailure(VulkanLogicalDeviceAcquireCode::PresentationDeviceNotLive);
            }
        }
        auto pending =
            std::make_unique<VulkanLogicalDeviceGeneration>(std::move(std::get<VulkanLogicalDeviceGeneration>(resolution_result)));

        if (VulkanLogicalDeviceAcquireResult freshness = freshness_check())
        {
            return freshness;
        }
        if (instance_generation.mInstance != instance || !instance_generation.mSurfaceGeneration ||
            instance_generation.surface() != surface || instance_generation.mPresentationDeviceGeneration.get() != selection ||
            instance_generation.physicalDevice() != physical_device || instance_generation.presentationQueueFamilyIndex() != queue_family ||
            !pending->createdFor(*selection))
        {
            return logicalDeviceFailure(VulkanLogicalDeviceAcquireCode::PresentationDeviceNotLive);
        }

        instance_generation.mLogicalDeviceGeneration = std::move(pending);
        instance_generation.noteOwnershipTransition();
        return std::nullopt;
    }
    catch (const std::bad_alloc&)
    {
        return logicalDeviceFailure(VulkanLogicalDeviceAcquireCode::AllocationFailure);
    }
}

VulkanUploadSourceAcquireResult VulkanInstanceGenerationFactory::acquireUploadSource(
    VulkanInstanceGeneration&                  instance_generation,
    const VulkanUploadSourceRequest&           request,
    VulkanInstanceDetail::AllocationCheckpoint allocation_checkpoint) noexcept
{
    if (!request.mInstanceOwnerCheck.mIsCurrent)
    {
        return uploadSourceFailure(VulkanUploadSourceAcquireCode::InvalidInstanceOwnerCheck);
    }
    if (!request.mWindowGenerationCheck.mIsCurrent)
    {
        return uploadSourceFailure(VulkanUploadSourceAcquireCode::InvalidWindowGenerationCheck);
    }
    if (request.mNativeWindowGeneration == 0)
    {
        return uploadSourceFailure(VulkanUploadSourceAcquireCode::InvalidNativeWindowGeneration);
    }
    if (instance_generation.mInstance == VK_NULL_HANDLE || !instance_generation.mDestroyInstance || !instance_generation.mGlobalDispatch ||
        instance_generation.mNativeWindowGeneration == 0)
    {
        return uploadSourceFailure(VulkanUploadSourceAcquireCode::InstanceNotLive);
    }
    if (!instance_generation.mSurfaceGeneration || instance_generation.surface() == VK_NULL_HANDLE)
    {
        return uploadSourceFailure(VulkanUploadSourceAcquireCode::SurfaceNotLive);
    }
    if (!instance_generation.mPresentationDeviceGeneration || instance_generation.physicalDevice() == VK_NULL_HANDLE)
    {
        return uploadSourceFailure(VulkanUploadSourceAcquireCode::PresentationDeviceNotLive);
    }
    if (!instance_generation.mLogicalDeviceGeneration || instance_generation.logicalDevice() == VK_NULL_HANDLE ||
        instance_generation.presentationQueue() == VK_NULL_HANDLE)
    {
        return uploadSourceFailure(VulkanUploadSourceAcquireCode::LogicalDeviceNotLive);
    }
    if (instance_generation.mUploadSourceGeneration)
    {
        return uploadSourceFailure(VulkanUploadSourceAcquireCode::UploadSourceAlreadyOwned);
    }
    if (request.mNativeWindowGeneration != instance_generation.mNativeWindowGeneration ||
        request.mNativeWindowGeneration != instance_generation.surfaceNativeWindowGeneration())
    {
        return uploadSourceFailure(VulkanUploadSourceAcquireCode::NativeWindowGenerationMismatch);
    }

    const VulkanUploadSourceDescription description       = request.mDescription;
    const std::uint64_t                 acquisition_epoch = instance_generation.mOwnershipTransitionEpoch;
    const auto                          freshness_check   = [&]() noexcept
    {
        return uploadSourceFreshness(request, instance_generation, &instance_generation.mOwnershipTransitionEpoch, acquisition_epoch);
    };
    if (VulkanUploadSourceAcquireResult freshness = freshness_check())
    {
        return freshness;
    }
    if (instance_generation.mUploadSourceGeneration)
    {
        return uploadSourceFailure(VulkanUploadSourceAcquireCode::UploadSourceAlreadyOwned);
    }
    VulkanInstanceGeneration::NativeAcquisitionGuard native_guard(instance_generation);

    const VkInstance                      instance        = instance_generation.mInstance;
    const VkSurfaceKHR                    surface         = instance_generation.surface();
    const VulkanPhysicalDeviceGeneration* selection       = instance_generation.mPresentationDeviceGeneration.get();
    const VulkanLogicalDeviceGeneration*  logical_device  = instance_generation.mLogicalDeviceGeneration.get();
    const VkPhysicalDevice                physical_device = selection->physicalDevice();
    const std::uint32_t                   physical_index  = selection->physicalDeviceIndex();
    const VkDevice                        device          = logical_device->device();
    const VkQueue                         queue           = logical_device->queue();
    const std::uint32_t                   queue_family    = logical_device->queueFamilyIndex();
    const std::uint32_t                   queue_index     = logical_device->queueIndex();

    VulkanUploadSourceResolutionResult resolution_result = resolveVulkanUploadSourceGeneration(*selection, *logical_device, description);
    if (const auto* error = std::get_if<VulkanUploadSourceResolutionError>(&resolution_result))
    {
        return uploadSourceFailure(VulkanUploadSourceAcquireCode::ResolutionFailure, *error);
    }

    try
    {
        if (allocation_checkpoint)
        {
            allocation_checkpoint();
            if (instance_generation.mUploadSourceGeneration)
            {
                return uploadSourceFailure(VulkanUploadSourceAcquireCode::UploadSourceAlreadyOwned);
            }
            if (instance_generation.mOwnershipTransitionEpoch != acquisition_epoch)
            {
                return uploadSourceFailure(VulkanUploadSourceAcquireCode::LogicalDeviceNotLive);
            }
        }
        auto pending = std::make_unique<VulkanUploadSourceGeneration>(std::move(std::get<VulkanUploadSourceGeneration>(resolution_result)));

        if (VulkanUploadSourceAcquireResult freshness = freshness_check())
        {
            return freshness;
        }
        if (instance_generation.mUploadSourceGeneration)
        {
            return uploadSourceFailure(VulkanUploadSourceAcquireCode::UploadSourceAlreadyOwned);
        }
        if (instance_generation.mInstance != instance || !instance_generation.mSurfaceGeneration ||
            instance_generation.surface() != surface || instance_generation.mPresentationDeviceGeneration.get() != selection ||
            instance_generation.mLogicalDeviceGeneration.get() != logical_device ||
            instance_generation.physicalDevice() != physical_device || instance_generation.physicalDeviceIndex() != physical_index ||
            instance_generation.logicalDevice() != device || instance_generation.presentationQueue() != queue ||
            instance_generation.logicalDeviceQueueFamilyIndex() != queue_family ||
            instance_generation.logicalDeviceQueueIndex() != queue_index || request.mDescription != description ||
            !pending->createdFor(*selection, *logical_device) || !pending->matchesDescription(description))
        {
            return uploadSourceFailure(VulkanUploadSourceAcquireCode::LogicalDeviceNotLive);
        }

        instance_generation.mUploadSourceGeneration = std::move(pending);
        instance_generation.noteUploadSourceTransition();
        return std::nullopt;
    }
    catch (const std::bad_alloc&)
    {
        return uploadSourceFailure(VulkanUploadSourceAcquireCode::AllocationFailure);
    }
}

VulkanTextureUploadDestinationAcquireResult VulkanInstanceGenerationFactory::acquireTextureUploadDestination(
    VulkanInstanceGeneration&                    instance_generation,
    const VulkanTextureUploadDestinationRequest& request,
    VulkanInstanceDetail::AllocationCheckpoint   allocation_checkpoint) noexcept
{
    if (!request.mInstanceOwnerCheck.mIsCurrent)
    {
        return textureUploadDestinationFailure(VulkanTextureUploadDestinationAcquireCode::InvalidInstanceOwnerCheck);
    }
    if (!request.mWindowGenerationCheck.mIsCurrent)
    {
        return textureUploadDestinationFailure(VulkanTextureUploadDestinationAcquireCode::InvalidWindowGenerationCheck);
    }
    if (request.mNativeWindowGeneration == 0)
    {
        return textureUploadDestinationFailure(VulkanTextureUploadDestinationAcquireCode::InvalidNativeWindowGeneration);
    }
    if (instance_generation.mTextureUploadDestinationTeardownDepth != 0 ||
        instance_generation.mTextureUploadSampleBindingTeardownDepth != 0 ||
        instance_generation.mTextureUploadSamplePipelineTeardownDepth != 0)
    {
        return textureUploadDestinationFailure(VulkanTextureUploadDestinationAcquireCode::NativeTeardownInProgress);
    }
    if (instance_generation.mInstance == VK_NULL_HANDLE || !instance_generation.mDestroyInstance || !instance_generation.mGlobalDispatch ||
        !instance_generation.mGlobalDispatch->getInstanceProcAddr() || instance_generation.mNativeWindowGeneration == 0)
    {
        return textureUploadDestinationFailure(VulkanTextureUploadDestinationAcquireCode::InstanceNotLive);
    }
    if (!instance_generation.mSurfaceGeneration || instance_generation.surface() == VK_NULL_HANDLE ||
        instance_generation.surfaceNativeWindowGeneration() == 0)
    {
        return textureUploadDestinationFailure(VulkanTextureUploadDestinationAcquireCode::SurfaceNotLive);
    }
    if (!instance_generation.mPresentationDeviceGeneration || instance_generation.physicalDevice() == VK_NULL_HANDLE)
    {
        return textureUploadDestinationFailure(VulkanTextureUploadDestinationAcquireCode::PresentationDeviceNotLive);
    }
    if (!instance_generation.mLogicalDeviceGeneration || instance_generation.logicalDevice() == VK_NULL_HANDLE ||
        instance_generation.presentationQueue() == VK_NULL_HANDLE)
    {
        return textureUploadDestinationFailure(VulkanTextureUploadDestinationAcquireCode::LogicalDeviceNotLive);
    }
    if (instance_generation.mTextureUploadDestinationGeneration)
    {
        return textureUploadDestinationFailure(VulkanTextureUploadDestinationAcquireCode::TextureUploadDestinationAlreadyOwned);
    }
    if (request.mNativeWindowGeneration != instance_generation.mNativeWindowGeneration ||
        request.mNativeWindowGeneration != instance_generation.surfaceNativeWindowGeneration())
    {
        return textureUploadDestinationFailure(VulkanTextureUploadDestinationAcquireCode::NativeWindowGenerationMismatch);
    }

    const VulkanTextureUploadDestinationDescription description               = request.mDescription;
    const std::uint64_t                             ownership_epoch           = instance_generation.mOwnershipTransitionEpoch;
    const std::uint64_t                             texture_destination_epoch = instance_generation.mTextureUploadDestinationEpoch;
    const VulkanGlobalDispatchGeneration*           global_dispatch           = &*instance_generation.mGlobalDispatch;
    const PFN_vkGetInstanceProcAddr                 get_instance_proc_addr    = global_dispatch->getInstanceProcAddr();
    const VkInstance                                instance                  = instance_generation.mInstance;
    const std::uint64_t                             native_window_generation  = instance_generation.mNativeWindowGeneration;
    const auto*                                     surface_generation        = instance_generation.mSurfaceGeneration.get();
    const std::uint64_t                             surface_window_generation = instance_generation.surfaceNativeWindowGeneration();
    const VkSurfaceKHR                              surface                   = instance_generation.surface();
    const VulkanPhysicalDeviceGeneration*           selection                 = instance_generation.mPresentationDeviceGeneration.get();
    const VulkanLogicalDeviceGeneration*            logical_device            = instance_generation.mLogicalDeviceGeneration.get();
    const VkPhysicalDevice                          physical_device           = selection->physicalDevice();
    const std::uint32_t                             physical_index            = selection->physicalDeviceIndex();
    const VkDevice                                  device                    = logical_device->device();
    const VkQueue                                   queue                     = logical_device->queue();
    const std::uint32_t                             queue_family              = logical_device->queueFamilyIndex();
    const std::uint32_t                             queue_index               = logical_device->queueIndex();

    const auto exact_parent_chain = [&]() noexcept
    {
        return instance_generation.mOwnershipTransitionEpoch == ownership_epoch &&
               instance_generation.mTextureUploadDestinationEpoch == texture_destination_epoch && instance_generation.mGlobalDispatch &&
               &*instance_generation.mGlobalDispatch == global_dispatch &&
               instance_generation.mGlobalDispatch->getInstanceProcAddr() == get_instance_proc_addr &&
               instance_generation.mInstance == instance && instance_generation.mDestroyInstance &&
               instance_generation.mNativeWindowGeneration == native_window_generation &&
               native_window_generation == request.mNativeWindowGeneration &&
               instance_generation.mSurfaceGeneration.get() == surface_generation && surface_generation &&
               surface_generation->nativeWindowGeneration() == surface_window_generation &&
               surface_window_generation == request.mNativeWindowGeneration && surface_generation->surface() == surface &&
               instance_generation.mPresentationDeviceGeneration.get() == selection && selection &&
               selection->getInstanceProcAddr() == get_instance_proc_addr && selection->instance() == instance &&
               selection->surface() == surface && selection->physicalDevice() == physical_device &&
               selection->physicalDeviceIndex() == physical_index && selection->selectedFor(instance, surface) &&
               instance_generation.mLogicalDeviceGeneration.get() == logical_device && logical_device &&
               logical_device->getInstanceProcAddr() == get_instance_proc_addr && logical_device->instance() == instance &&
               logical_device->surface() == surface && logical_device->physicalDevice() == physical_device &&
               logical_device->physicalDeviceIndex() == physical_index && logical_device->device() == device &&
               logical_device->queue() == queue && logical_device->queueFamilyIndex() == queue_family &&
               logical_device->queueIndex() == queue_index && logical_device->createdFor(*selection) && request.mDescription == description;
    };
    const auto freshness_check = [&]() noexcept -> VulkanTextureUploadDestinationAcquireResult
    {
        if (VulkanTextureUploadDestinationAcquireResult freshness = textureUploadDestinationFreshness(
                request, instance_generation, &instance_generation.mOwnershipTransitionEpoch, ownership_epoch))
        {
            return freshness;
        }
        if (!exact_parent_chain())
        {
            return textureUploadDestinationFailure(VulkanTextureUploadDestinationAcquireCode::LogicalDeviceNotLive);
        }
        return std::nullopt;
    };

    if (VulkanTextureUploadDestinationAcquireResult freshness = freshness_check())
    {
        return freshness;
    }
    VulkanInstanceGeneration::NativeAcquisitionGuard native_guard(instance_generation);

    VulkanTextureUploadDestinationResolutionResult resolution_result =
        resolveVulkanTextureUploadDestinationGeneration(*selection, *logical_device, description);
    if (const auto* error = std::get_if<VulkanTextureUploadDestinationResolutionError>(&resolution_result))
    {
        return textureUploadDestinationFailure(VulkanTextureUploadDestinationAcquireCode::ResolutionFailure, *error);
    }

    const auto&                         resolved                = std::get<VulkanTextureUploadDestinationGeneration>(resolution_result);
    const LLRenderContract::ImageHandle resource_handle         = resolved.resourceHandle();
    const std::uint64_t                 expected_revision       = resolved.expectedRevision();
    const VkExtent3D                    resident_extent         = resolved.residentExtent();
    const LLRenderContract::Extent2D    logical_extent          = resolved.logicalExtent();
    const std::uint32_t                 resident_discard        = resolved.residentDiscard();
    const LLRenderContract::PixelFormat pixel_format            = resolved.pixelFormat();
    const LLRenderContract::ImageState  initial_state           = resolved.initialState();
    const VkImageCreateFlags            image_flags             = resolved.flags();
    const VkImageType                   image_type              = resolved.imageType();
    const VkFormat                      image_format            = resolved.format();
    const std::uint32_t                 mip_levels              = resolved.mipLevels();
    const std::uint32_t                 array_layers            = resolved.arrayLayers();
    const VkSampleCountFlagBits         samples                 = resolved.samples();
    const VkImageTiling                 tiling                  = resolved.tiling();
    const VkImageUsageFlags             usage                   = resolved.usage();
    const VkSharingMode                 sharing_mode            = resolved.sharingMode();
    const VkImageLayout                 initial_layout          = resolved.initialLayout();
    const VkFormatFeatureFlags          format_features         = resolved.formatFeatures();
    const VkImageFormatProperties       image_format_properties = resolved.imageFormatProperties();
    const VkImage                       image                   = resolved.image();
    const VkDeviceMemory                memory                  = resolved.memory();
    const VkMemoryRequirements          memory_requirements     = resolved.memoryRequirements();
    const VkDeviceSize                  allocation_size         = resolved.allocationSize();
    const VkDeviceSize                  allocation_alignment    = resolved.allocationAlignment();
    const std::uint32_t                 compatible_memory_bits  = resolved.compatibleMemoryTypeBits();
    const std::uint32_t                 memory_type_index       = resolved.memoryTypeIndex();
    const VkMemoryPropertyFlags         memory_property_flags   = resolved.memoryPropertyFlags();
    const bool                          device_local            = resolved.isDeviceLocal();
    const bool                          prefers_dedicated       = resolved.prefersDedicatedAllocation();
    const bool                          requires_dedicated      = resolved.requiresDedicatedAllocation();
    const VkImageView                   image_view              = resolved.imageView();
    const VkImageViewType               image_view_type         = resolved.imageViewType();
    const VkImageSubresourceRange       view_range              = resolved.viewRange();

    const auto exact_candidate = [&](const VulkanTextureUploadDestinationGeneration& candidate) noexcept
    {
        const VkExtent3D                 candidate_resident_extent     = candidate.residentExtent();
        const LLRenderContract::Extent2D candidate_logical_extent      = candidate.logicalExtent();
        const VkImageFormatProperties&   candidate_format_properties   = candidate.imageFormatProperties();
        const VkMemoryRequirements&      candidate_memory_requirements = candidate.memoryRequirements();
        const VkImageSubresourceRange    candidate_view_range          = candidate.viewRange();

        return candidate.createdFor(*selection, *logical_device) && candidate.matchesDescription(description) &&
               candidate.resourceHandle() == resource_handle && resource_handle == description.mHandle &&
               candidate.expectedRevision() == expected_revision && expected_revision == description.mExpectedRevision &&
               candidate_resident_extent.width == resident_extent.width && candidate_resident_extent.height == resident_extent.height &&
               candidate_resident_extent.depth == resident_extent.depth && resident_extent.width == description.mResidentExtent.mWidth &&
               resident_extent.height == description.mResidentExtent.mHeight && resident_extent.depth == 1 &&
               candidate_logical_extent.mWidth == logical_extent.mWidth && candidate_logical_extent.mHeight == logical_extent.mHeight &&
               logical_extent.mWidth == description.mLogicalExtent.mWidth && logical_extent.mHeight == description.mLogicalExtent.mHeight &&
               candidate.residentDiscard() == resident_discard && resident_discard == description.mResidentDiscard &&
               candidate.pixelFormat() == pixel_format && pixel_format == description.mFormat &&
               candidate.initialState() == initial_state && initial_state == description.mInitialState &&
               candidate.flags() == image_flags && candidate.imageType() == image_type && candidate.format() == image_format &&
               candidate.mipLevels() == mip_levels && mip_levels == description.mMipLevels && candidate.arrayLayers() == array_layers &&
               array_layers == description.mArrayLayers && candidate.samples() == samples && candidate.tiling() == tiling &&
               candidate.usage() == usage && candidate.sharingMode() == sharing_mode && candidate.initialLayout() == initial_layout &&
               candidate.formatFeatures() == format_features &&
               candidate_format_properties.maxExtent.width == image_format_properties.maxExtent.width &&
               candidate_format_properties.maxExtent.height == image_format_properties.maxExtent.height &&
               candidate_format_properties.maxExtent.depth == image_format_properties.maxExtent.depth &&
               candidate_format_properties.maxMipLevels == image_format_properties.maxMipLevels &&
               candidate_format_properties.maxArrayLayers == image_format_properties.maxArrayLayers &&
               candidate_format_properties.sampleCounts == image_format_properties.sampleCounts &&
               candidate_format_properties.maxResourceSize == image_format_properties.maxResourceSize && candidate.image() == image &&
               image != VK_NULL_HANDLE && candidate.memory() == memory && memory != VK_NULL_HANDLE &&
               candidate_memory_requirements.size == memory_requirements.size &&
               candidate_memory_requirements.alignment == memory_requirements.alignment &&
               candidate_memory_requirements.memoryTypeBits == memory_requirements.memoryTypeBits &&
               candidate.allocationSize() == allocation_size && allocation_size != 0 &&
               candidate.allocationAlignment() == allocation_alignment && allocation_alignment != 0 &&
               candidate.compatibleMemoryTypeBits() == compatible_memory_bits && compatible_memory_bits != 0 &&
               candidate.memoryTypeIndex() == memory_type_index && memory_type_index < VK_MAX_MEMORY_TYPES &&
               (compatible_memory_bits & (std::uint32_t{ 1 } << memory_type_index)) != 0 &&
               candidate.memoryPropertyFlags() == memory_property_flags && candidate.isDeviceLocal() == device_local && device_local &&
               candidate.prefersDedicatedAllocation() == prefers_dedicated &&
               candidate.requiresDedicatedAllocation() == requires_dedicated && candidate.imageView() == image_view &&
               image_view != VK_NULL_HANDLE && candidate.imageViewType() == image_view_type && image_view_type == VK_IMAGE_VIEW_TYPE_2D &&
               candidate_view_range.aspectMask == view_range.aspectMask && candidate_view_range.baseMipLevel == view_range.baseMipLevel &&
               candidate_view_range.levelCount == view_range.levelCount &&
               candidate_view_range.baseArrayLayer == view_range.baseArrayLayer &&
               candidate_view_range.layerCount == view_range.layerCount && request.mDescription == description;
    };

    try
    {
        if (allocation_checkpoint)
        {
            allocation_checkpoint();
            if (instance_generation.mTextureUploadDestinationGeneration)
            {
                return textureUploadDestinationFailure(VulkanTextureUploadDestinationAcquireCode::TextureUploadDestinationAlreadyOwned);
            }
            if (!exact_parent_chain())
            {
                return textureUploadDestinationFailure(VulkanTextureUploadDestinationAcquireCode::LogicalDeviceNotLive);
            }
            if (!exact_candidate(resolved))
            {
                return textureUploadDestinationFailure(VulkanTextureUploadDestinationAcquireCode::LogicalDeviceNotLive);
            }
        }
        auto pending = std::make_unique<VulkanTextureUploadDestinationGeneration>(
            std::move(std::get<VulkanTextureUploadDestinationGeneration>(resolution_result)));

        if (VulkanTextureUploadDestinationAcquireResult freshness = freshness_check())
        {
            return freshness;
        }
        if (instance_generation.mTextureUploadDestinationGeneration)
        {
            return textureUploadDestinationFailure(VulkanTextureUploadDestinationAcquireCode::TextureUploadDestinationAlreadyOwned);
        }
        if (!exact_parent_chain() || !exact_candidate(*pending))
        {
            return textureUploadDestinationFailure(VulkanTextureUploadDestinationAcquireCode::LogicalDeviceNotLive);
        }

        instance_generation.mTextureUploadDestinationGeneration = std::move(pending);
        instance_generation.noteTextureUploadDestinationTransition();
        return std::nullopt;
    }
    catch (const std::bad_alloc&)
    {
        return textureUploadDestinationFailure(VulkanTextureUploadDestinationAcquireCode::AllocationFailure);
    }
}

VulkanTextureUploadSourceAcquireResult VulkanInstanceGenerationFactory::acquireTextureUploadSource(
    VulkanInstanceGeneration&                  instance_generation,
    const VulkanTextureUploadSourceRequest&    request,
    VulkanInstanceDetail::AllocationCheckpoint allocation_checkpoint) noexcept
{
    if (!request.mInstanceOwnerCheck.mIsCurrent)
    {
        return textureUploadSourceFailure(VulkanTextureUploadSourceAcquireCode::InvalidInstanceOwnerCheck);
    }
    if (!request.mWindowGenerationCheck.mIsCurrent)
    {
        return textureUploadSourceFailure(VulkanTextureUploadSourceAcquireCode::InvalidWindowGenerationCheck);
    }
    if (request.mNativeWindowGeneration == 0)
    {
        return textureUploadSourceFailure(VulkanTextureUploadSourceAcquireCode::InvalidNativeWindowGeneration);
    }
    if (instance_generation.mTextureUploadSourceTeardownDepth != 0 || instance_generation.mTextureUploadSampleBindingTeardownDepth != 0 ||
        instance_generation.mTextureUploadSamplePipelineTeardownDepth != 0)
    {
        return textureUploadSourceFailure(VulkanTextureUploadSourceAcquireCode::NativeTeardownInProgress);
    }
    if (instance_generation.mInstance == VK_NULL_HANDLE || !instance_generation.mDestroyInstance || !instance_generation.mGlobalDispatch ||
        !instance_generation.mGlobalDispatch->getInstanceProcAddr() || instance_generation.mNativeWindowGeneration == 0)
    {
        return textureUploadSourceFailure(VulkanTextureUploadSourceAcquireCode::InstanceNotLive);
    }
    if (!instance_generation.mSurfaceGeneration || instance_generation.surface() == VK_NULL_HANDLE ||
        instance_generation.surfaceNativeWindowGeneration() == 0)
    {
        return textureUploadSourceFailure(VulkanTextureUploadSourceAcquireCode::SurfaceNotLive);
    }
    if (!instance_generation.mPresentationDeviceGeneration || instance_generation.physicalDevice() == VK_NULL_HANDLE)
    {
        return textureUploadSourceFailure(VulkanTextureUploadSourceAcquireCode::PresentationDeviceNotLive);
    }
    if (!instance_generation.mLogicalDeviceGeneration || instance_generation.logicalDevice() == VK_NULL_HANDLE ||
        instance_generation.presentationQueue() == VK_NULL_HANDLE)
    {
        return textureUploadSourceFailure(VulkanTextureUploadSourceAcquireCode::LogicalDeviceNotLive);
    }
    if (!instance_generation.mTextureUploadDestinationGeneration || instance_generation.textureUploadDestinationImage() == VK_NULL_HANDLE ||
        instance_generation.textureUploadDestinationMemory() == VK_NULL_HANDLE ||
        instance_generation.textureUploadDestinationImageView() == VK_NULL_HANDLE)
    {
        return textureUploadSourceFailure(VulkanTextureUploadSourceAcquireCode::TextureUploadDestinationNotLive);
    }
    if (instance_generation.mTextureUploadSourceGeneration)
    {
        return textureUploadSourceFailure(VulkanTextureUploadSourceAcquireCode::TextureUploadSourceAlreadyOwned);
    }
    if (request.mNativeWindowGeneration != instance_generation.mNativeWindowGeneration ||
        request.mNativeWindowGeneration != instance_generation.surfaceNativeWindowGeneration())
    {
        return textureUploadSourceFailure(VulkanTextureUploadSourceAcquireCode::NativeWindowGenerationMismatch);
    }
    if (request.mDescription.mHandle != instance_generation.textureUploadDestinationResourceHandle() ||
        request.mDescription.mExpectedRevision != instance_generation.textureUploadDestinationExpectedRevision())
    {
        return textureUploadSourceFailure(VulkanTextureUploadSourceAcquireCode::TextureUploadDestinationNotLive);
    }

    const VulkanTextureUploadSourceDescription description               = request.mDescription;
    const std::uint64_t                        ownership_epoch           = instance_generation.mOwnershipTransitionEpoch;
    const std::uint64_t                        texture_destination_epoch = instance_generation.mTextureUploadDestinationEpoch;
    const std::uint64_t                        texture_source_epoch      = instance_generation.mTextureUploadSourceEpoch;
    const VulkanGlobalDispatchGeneration*      global_dispatch           = &*instance_generation.mGlobalDispatch;
    const PFN_vkGetInstanceProcAddr            get_instance_proc_addr    = global_dispatch->getInstanceProcAddr();
    const VkInstance                           instance                  = instance_generation.mInstance;
    const std::uint64_t                        native_window_generation  = instance_generation.mNativeWindowGeneration;
    const auto*                                surface_generation        = instance_generation.mSurfaceGeneration.get();
    const std::uint64_t                        surface_window_generation = instance_generation.surfaceNativeWindowGeneration();
    const VkSurfaceKHR                         surface                   = instance_generation.surface();
    const VulkanPhysicalDeviceGeneration*      selection                 = instance_generation.mPresentationDeviceGeneration.get();
    const VulkanLogicalDeviceGeneration*       logical_device            = instance_generation.mLogicalDeviceGeneration.get();
    const auto*                                texture_destination       = instance_generation.mTextureUploadDestinationGeneration.get();
    const VkPhysicalDevice                     physical_device           = selection->physicalDevice();
    const std::uint32_t                        physical_index            = selection->physicalDeviceIndex();
    const VkDevice                             device                    = logical_device->device();
    const VkQueue                              queue                     = logical_device->queue();
    const std::uint32_t                        queue_family              = logical_device->queueFamilyIndex();
    const std::uint32_t                        queue_index               = logical_device->queueIndex();
    const LLRenderContract::ImageHandle        destination_handle        = texture_destination->resourceHandle();
    const std::uint64_t                        destination_revision      = texture_destination->expectedRevision();
    const VkImage                              destination_image         = texture_destination->image();
    const VkDeviceMemory                       destination_memory        = texture_destination->memory();
    const VkImageView                          destination_view          = texture_destination->imageView();

    const auto exact_destination_chain = [&]() noexcept
    {
        return instance_generation.mOwnershipTransitionEpoch == ownership_epoch &&
               instance_generation.mTextureUploadDestinationEpoch == texture_destination_epoch &&
               instance_generation.mTextureUploadSourceEpoch == texture_source_epoch && instance_generation.mGlobalDispatch &&
               &*instance_generation.mGlobalDispatch == global_dispatch &&
               instance_generation.mGlobalDispatch->getInstanceProcAddr() == get_instance_proc_addr &&
               instance_generation.mInstance == instance && instance_generation.mDestroyInstance &&
               instance_generation.mNativeWindowGeneration == native_window_generation &&
               native_window_generation == request.mNativeWindowGeneration &&
               instance_generation.mSurfaceGeneration.get() == surface_generation && surface_generation &&
               surface_generation->nativeWindowGeneration() == surface_window_generation &&
               surface_window_generation == request.mNativeWindowGeneration && surface_generation->surface() == surface &&
               instance_generation.mPresentationDeviceGeneration.get() == selection && selection &&
               selection->getInstanceProcAddr() == get_instance_proc_addr && selection->instance() == instance &&
               selection->surface() == surface && selection->physicalDevice() == physical_device &&
               selection->physicalDeviceIndex() == physical_index && selection->selectedFor(instance, surface) &&
               instance_generation.mLogicalDeviceGeneration.get() == logical_device && logical_device &&
               logical_device->getInstanceProcAddr() == get_instance_proc_addr && logical_device->instance() == instance &&
               logical_device->surface() == surface && logical_device->physicalDevice() == physical_device &&
               logical_device->physicalDeviceIndex() == physical_index && logical_device->device() == device &&
               logical_device->queue() == queue && logical_device->queueFamilyIndex() == queue_family &&
               logical_device->queueIndex() == queue_index && logical_device->createdFor(*selection) &&
               instance_generation.mTextureUploadDestinationGeneration.get() == texture_destination && texture_destination &&
               texture_destination->createdFor(*selection, *logical_device) &&
               texture_destination->resourceHandle() == destination_handle && destination_handle == description.mHandle &&
               texture_destination->expectedRevision() == destination_revision && destination_revision == description.mExpectedRevision &&
               texture_destination->image() == destination_image && destination_image != VK_NULL_HANDLE &&
               texture_destination->memory() == destination_memory && destination_memory != VK_NULL_HANDLE &&
               texture_destination->imageView() == destination_view && destination_view != VK_NULL_HANDLE &&
               request.mDescription == description;
    };
    const auto freshness_check = [&]() noexcept -> VulkanTextureUploadSourceAcquireResult
    {
        if (VulkanTextureUploadSourceAcquireResult freshness =
                textureUploadSourceFreshness(request, instance_generation, &instance_generation.mOwnershipTransitionEpoch, ownership_epoch))
        {
            return freshness;
        }
        if (!exact_destination_chain())
        {
            return textureUploadSourceFailure(VulkanTextureUploadSourceAcquireCode::TextureUploadDestinationNotLive);
        }
        return std::nullopt;
    };

    if (VulkanTextureUploadSourceAcquireResult freshness = freshness_check())
    {
        return freshness;
    }
    VulkanInstanceGeneration::NativeAcquisitionGuard native_guard(instance_generation);

    VulkanTextureUploadSourceResolutionResult resolution_result =
        resolveVulkanTextureUploadSourceGeneration(*selection, *logical_device, description);
    if (const auto* error = std::get_if<VulkanTextureUploadSourceResolutionError>(&resolution_result))
    {
        return textureUploadSourceFailure(VulkanTextureUploadSourceAcquireCode::ResolutionFailure, *error);
    }

    const auto&                         resolved              = std::get<VulkanTextureUploadSourceGeneration>(resolution_result);
    const LLRenderContract::ImageHandle resource_handle       = resolved.resourceHandle();
    const std::uint64_t                 expected_revision     = resolved.expectedRevision();
    const LLRenderContract::Extent2D    resident_extent       = resolved.residentExtent();
    const LLRenderContract::PixelFormat pixel_format          = resolved.pixelFormat();
    const std::uint32_t                 row_pitch             = resolved.rowPitch();
    const LLRenderContract::RowOrigin   row_origin            = resolved.rowOrigin();
    const std::uint64_t                 content_identity      = resolved.contentIdentity();
    const VkBuffer                      buffer                = resolved.buffer();
    const VkDeviceMemory                memory                = resolved.memory();
    const VkDeviceSize                  byte_count            = resolved.byteCount();
    const VkDeviceSize                  allocation_size       = resolved.allocationSize();
    const std::uint32_t                 memory_type_index     = resolved.memoryTypeIndex();
    const VkMemoryPropertyFlags         memory_property_flags = resolved.memoryPropertyFlags();
    const bool                          coherent              = resolved.isCoherent();

    const auto exact_candidate = [&](const VulkanTextureUploadSourceGeneration& candidate) noexcept
    {
        const LLRenderContract::Extent2D candidate_extent = candidate.residentExtent();
        return candidate.createdFor(*selection, *logical_device) && candidate.matchesDescription(description) &&
               candidate.resourceHandle() == resource_handle && resource_handle == description.mHandle &&
               resource_handle == destination_handle && candidate.expectedRevision() == expected_revision &&
               expected_revision == description.mExpectedRevision && expected_revision == destination_revision &&
               candidate_extent.mWidth == resident_extent.mWidth && candidate_extent.mHeight == resident_extent.mHeight &&
               resident_extent.mWidth == texture_destination->residentExtent().width &&
               resident_extent.mHeight == texture_destination->residentExtent().height && candidate.pixelFormat() == pixel_format &&
               pixel_format == texture_destination->pixelFormat() && candidate.rowPitch() == row_pitch && row_pitch != 0 &&
               candidate.rowOrigin() == row_origin && row_origin == LLRenderContract::RowOrigin::TopLeft &&
               candidate.contentIdentity() == content_identity && content_identity != 0 && candidate.buffer() == buffer &&
               buffer != VK_NULL_HANDLE && candidate.memory() == memory && memory != VK_NULL_HANDLE && candidate.flags() == 0 &&
               candidate.usage() == VK_BUFFER_USAGE_TRANSFER_SRC_BIT && candidate.sharingMode() == VK_SHARING_MODE_EXCLUSIVE &&
               candidate.byteCount() == byte_count && byte_count == VULKAN_TEXTURE_UPLOAD_SOURCE_BYTE_COUNT &&
               candidate.allocationSize() == allocation_size && allocation_size >= byte_count &&
               candidate.memoryTypeIndex() == memory_type_index && memory_type_index < VK_MAX_MEMORY_TYPES &&
               candidate.memoryPropertyFlags() == memory_property_flags &&
               (memory_property_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0 && candidate.isCoherent() == coherent &&
               request.mDescription == description;
    };

    try
    {
        if (allocation_checkpoint)
        {
            allocation_checkpoint();
            if (instance_generation.mTextureUploadSourceGeneration)
            {
                return textureUploadSourceFailure(VulkanTextureUploadSourceAcquireCode::TextureUploadSourceAlreadyOwned);
            }
            if (!exact_destination_chain() || !exact_candidate(resolved))
            {
                return textureUploadSourceFailure(VulkanTextureUploadSourceAcquireCode::TextureUploadDestinationNotLive);
            }
        }
        auto pending = std::make_unique<VulkanTextureUploadSourceGeneration>(
            std::move(std::get<VulkanTextureUploadSourceGeneration>(resolution_result)));

        if (VulkanTextureUploadSourceAcquireResult freshness = freshness_check())
        {
            return freshness;
        }
        if (instance_generation.mTextureUploadSourceGeneration)
        {
            return textureUploadSourceFailure(VulkanTextureUploadSourceAcquireCode::TextureUploadSourceAlreadyOwned);
        }
        if (!exact_destination_chain() || !exact_candidate(*pending))
        {
            return textureUploadSourceFailure(VulkanTextureUploadSourceAcquireCode::TextureUploadDestinationNotLive);
        }

        instance_generation.mTextureUploadSourceGeneration = std::move(pending);
        instance_generation.noteTextureUploadSourceTransition();
        return std::nullopt;
    }
    catch (const std::bad_alloc&)
    {
        return textureUploadSourceFailure(VulkanTextureUploadSourceAcquireCode::AllocationFailure);
    }
}

VulkanUploadDestinationAcquireResult VulkanInstanceGenerationFactory::acquireUploadDestination(
    VulkanInstanceGeneration&                  instance_generation,
    const VulkanUploadDestinationRequest&      request,
    VulkanInstanceDetail::AllocationCheckpoint allocation_checkpoint) noexcept
{
    if (!request.mInstanceOwnerCheck.mIsCurrent)
    {
        return uploadDestinationFailure(VulkanUploadDestinationAcquireCode::InvalidInstanceOwnerCheck);
    }
    if (!request.mWindowGenerationCheck.mIsCurrent)
    {
        return uploadDestinationFailure(VulkanUploadDestinationAcquireCode::InvalidWindowGenerationCheck);
    }
    if (request.mNativeWindowGeneration == 0)
    {
        return uploadDestinationFailure(VulkanUploadDestinationAcquireCode::InvalidNativeWindowGeneration);
    }
    if (instance_generation.mInstance == VK_NULL_HANDLE || !instance_generation.mDestroyInstance || !instance_generation.mGlobalDispatch ||
        !instance_generation.mGlobalDispatch->getInstanceProcAddr() || instance_generation.mNativeWindowGeneration == 0)
    {
        return uploadDestinationFailure(VulkanUploadDestinationAcquireCode::InstanceNotLive);
    }
    if (!instance_generation.mSurfaceGeneration || instance_generation.surface() == VK_NULL_HANDLE)
    {
        return uploadDestinationFailure(VulkanUploadDestinationAcquireCode::SurfaceNotLive);
    }
    if (!instance_generation.mPresentationDeviceGeneration || instance_generation.physicalDevice() == VK_NULL_HANDLE)
    {
        return uploadDestinationFailure(VulkanUploadDestinationAcquireCode::PresentationDeviceNotLive);
    }
    if (!instance_generation.mLogicalDeviceGeneration || instance_generation.logicalDevice() == VK_NULL_HANDLE ||
        instance_generation.presentationQueue() == VK_NULL_HANDLE)
    {
        return uploadDestinationFailure(VulkanUploadDestinationAcquireCode::LogicalDeviceNotLive);
    }
    if (!instance_generation.mUploadSourceGeneration || instance_generation.uploadSourceBuffer() == VK_NULL_HANDLE ||
        instance_generation.uploadSourceMemory() == VK_NULL_HANDLE)
    {
        return uploadDestinationFailure(VulkanUploadDestinationAcquireCode::UploadSourceNotLive);
    }
    if (instance_generation.mUploadDestinationGeneration)
    {
        return uploadDestinationFailure(VulkanUploadDestinationAcquireCode::UploadDestinationAlreadyOwned);
    }
    if (request.mNativeWindowGeneration != instance_generation.mNativeWindowGeneration ||
        request.mNativeWindowGeneration != instance_generation.surfaceNativeWindowGeneration())
    {
        return uploadDestinationFailure(VulkanUploadDestinationAcquireCode::NativeWindowGenerationMismatch);
    }

    const VulkanUploadSourceDescription   description               = request.mDescription;
    const std::uint64_t                   acquisition_epoch         = instance_generation.mOwnershipTransitionEpoch;
    const std::uint64_t                   source_epoch              = instance_generation.mUploadSourceEpoch;
    const std::uint64_t                   destination_epoch         = instance_generation.mUploadDestinationEpoch;
    const VulkanGlobalDispatchGeneration* global_dispatch           = &*instance_generation.mGlobalDispatch;
    const PFN_vkGetInstanceProcAddr       get_instance_proc_addr    = global_dispatch->getInstanceProcAddr();
    const VkInstance                      instance                  = instance_generation.mInstance;
    const auto*                           surface_generation        = instance_generation.mSurfaceGeneration.get();
    const std::uint64_t                   surface_window_generation = instance_generation.surfaceNativeWindowGeneration();
    const VkSurfaceKHR                    surface                   = instance_generation.surface();
    const VulkanPhysicalDeviceGeneration* selection                 = instance_generation.mPresentationDeviceGeneration.get();
    const VulkanLogicalDeviceGeneration*  logical_device            = instance_generation.mLogicalDeviceGeneration.get();
    const VulkanUploadSourceGeneration*   source                    = instance_generation.mUploadSourceGeneration.get();
    const VkPhysicalDevice                physical_device           = selection->physicalDevice();
    const std::uint32_t                   physical_index            = selection->physicalDeviceIndex();
    const VkDevice                        device                    = logical_device->device();
    const VkQueue                         queue                     = logical_device->queue();
    const std::uint32_t                   queue_family              = logical_device->queueFamilyIndex();
    const std::uint32_t                   queue_index               = logical_device->queueIndex();
    const LLRenderContract::BufferHandle  source_handle             = source->resourceHandle();
    const std::uint64_t                   source_identity           = source->contentIdentity();
    const VkBuffer                        source_buffer             = source->buffer();
    const VkDeviceMemory                  source_memory             = source->memory();
    const VkDeviceSize                    source_byte_count         = source->byteCount();

    const auto exact_source_chain = [&]() noexcept
    {
        return instance_generation.mOwnershipTransitionEpoch == acquisition_epoch &&
               instance_generation.mUploadSourceEpoch == source_epoch && instance_generation.mUploadDestinationEpoch == destination_epoch &&
               instance_generation.mGlobalDispatch && &*instance_generation.mGlobalDispatch == global_dispatch &&
               instance_generation.mGlobalDispatch->getInstanceProcAddr() == get_instance_proc_addr &&
               instance_generation.mInstance == instance && instance_generation.mDestroyInstance &&
               instance_generation.mNativeWindowGeneration == request.mNativeWindowGeneration &&
               instance_generation.mSurfaceGeneration.get() == surface_generation &&
               instance_generation.surfaceNativeWindowGeneration() == surface_window_generation &&
               surface_window_generation == request.mNativeWindowGeneration && instance_generation.surface() == surface &&
               instance_generation.mPresentationDeviceGeneration.get() == selection &&
               selection->getInstanceProcAddr() == get_instance_proc_addr && selection->instance() == instance &&
               selection->surface() == surface && selection->physicalDevice() == physical_device &&
               selection->physicalDeviceIndex() == physical_index && selection->selectedFor(instance, surface) &&
               instance_generation.mLogicalDeviceGeneration.get() == logical_device &&
               logical_device->getInstanceProcAddr() == get_instance_proc_addr && logical_device->instance() == instance &&
               logical_device->surface() == surface && logical_device->physicalDevice() == physical_device &&
               logical_device->physicalDeviceIndex() == physical_index && logical_device->device() == device &&
               logical_device->queue() == queue && logical_device->queueFamilyIndex() == queue_family &&
               logical_device->queueIndex() == queue_index && logical_device->createdFor(*selection) &&
               instance_generation.mUploadSourceGeneration.get() == source && source && source->resourceHandle() == source_handle &&
               source_handle == description.mHandle && source->contentIdentity() == source_identity && source_identity != 0 &&
               source->buffer() == source_buffer && source_buffer != VK_NULL_HANDLE && source->memory() == source_memory &&
               source_memory != VK_NULL_HANDLE && source->byteCount() == source_byte_count &&
               source_byte_count == VULKAN_UPLOAD_SOURCE_BYTE_COUNT && source->createdFor(*selection, *logical_device) &&
               source->matchesDescription(description) && request.mDescription == description;
    };
    const auto freshness_check = [&]() noexcept -> VulkanUploadDestinationAcquireResult
    {
        if (VulkanUploadDestinationAcquireResult freshness =
                uploadDestinationFreshness(request, instance_generation, &instance_generation.mOwnershipTransitionEpoch, acquisition_epoch))
        {
            return freshness;
        }
        if (!exact_source_chain())
        {
            return uploadDestinationFailure(VulkanUploadDestinationAcquireCode::UploadSourceNotLive);
        }
        return std::nullopt;
    };

    if (VulkanUploadDestinationAcquireResult freshness = freshness_check())
    {
        return freshness;
    }
    VulkanInstanceGeneration::NativeAcquisitionGuard native_guard(instance_generation);

    VulkanUploadDestinationResolutionResult resolution_result =
        resolveVulkanUploadDestinationGeneration(*selection, *logical_device, *source, description);
    if (const auto* error = std::get_if<VulkanUploadDestinationResolutionError>(&resolution_result))
    {
        return uploadDestinationFailure(VulkanUploadDestinationAcquireCode::ResolutionFailure, *error);
    }

    try
    {
        if (allocation_checkpoint)
        {
            allocation_checkpoint();
            if (instance_generation.mUploadDestinationGeneration)
            {
                return uploadDestinationFailure(VulkanUploadDestinationAcquireCode::UploadDestinationAlreadyOwned);
            }
            if (!exact_source_chain())
            {
                return uploadDestinationFailure(VulkanUploadDestinationAcquireCode::UploadSourceNotLive);
            }
        }
        auto pending =
            std::make_unique<VulkanUploadDestinationGeneration>(std::move(std::get<VulkanUploadDestinationGeneration>(resolution_result)));

        if (VulkanUploadDestinationAcquireResult freshness = freshness_check())
        {
            return freshness;
        }
        if (instance_generation.mUploadDestinationGeneration)
        {
            return uploadDestinationFailure(VulkanUploadDestinationAcquireCode::UploadDestinationAlreadyOwned);
        }
        if (!exact_source_chain() || !pending->createdFor(*selection, *logical_device) || !pending->matchesDescription(description) ||
            !pending->matchesUploadSource(*source) || pending->isResident())
        {
            return uploadDestinationFailure(VulkanUploadDestinationAcquireCode::UploadSourceNotLive);
        }

        instance_generation.mUploadDestinationGeneration = std::move(pending);
        instance_generation.noteUploadDestinationTransition();
        return std::nullopt;
    }
    catch (const std::bad_alloc&)
    {
        return uploadDestinationFailure(VulkanUploadDestinationAcquireCode::AllocationFailure);
    }
}

VulkanTextureUploadTransferAcquireResult VulkanInstanceGenerationFactory::acquireTextureUploadTransfer(
    VulkanInstanceGeneration& instance_generation, const VulkanTextureUploadTransferRequest& request,
    VulkanInstanceDetail::AllocationCheckpoint allocation_checkpoint) noexcept
{
    if (!request.mInstanceOwnerCheck.mIsCurrent)
        return textureUploadTransferFailure(VulkanTextureUploadTransferAcquireCode::InvalidInstanceOwnerCheck);
    if (!request.mWindowGenerationCheck.mIsCurrent)
        return textureUploadTransferFailure(VulkanTextureUploadTransferAcquireCode::InvalidWindowGenerationCheck);
    if (request.mNativeWindowGeneration == 0)
        return textureUploadTransferFailure(VulkanTextureUploadTransferAcquireCode::InvalidNativeWindowGeneration);
    if (instance_generation.mTextureUploadTransferTeardownDepth != 0 || instance_generation.mTextureUploadSampleBindingTeardownDepth != 0 ||
        instance_generation.mTextureUploadSamplePipelineTeardownDepth != 0)
        return textureUploadTransferFailure(VulkanTextureUploadTransferAcquireCode::NativeTeardownInProgress);
    if (instance_generation.mInstance == VK_NULL_HANDLE || !instance_generation.mGlobalDispatch)
        return textureUploadTransferFailure(VulkanTextureUploadTransferAcquireCode::InstanceNotLive);
    if (!instance_generation.mSurfaceGeneration)
        return textureUploadTransferFailure(VulkanTextureUploadTransferAcquireCode::SurfaceNotLive);
    if (!instance_generation.mPresentationDeviceGeneration)
        return textureUploadTransferFailure(VulkanTextureUploadTransferAcquireCode::PresentationDeviceNotLive);
    if (!instance_generation.mLogicalDeviceGeneration)
        return textureUploadTransferFailure(VulkanTextureUploadTransferAcquireCode::LogicalDeviceNotLive);
    if (!instance_generation.mTextureUploadSourceGeneration)
        return textureUploadTransferFailure(VulkanTextureUploadTransferAcquireCode::TextureUploadSourceNotLive);
    if (!instance_generation.mTextureUploadDestinationGeneration)
        return textureUploadTransferFailure(VulkanTextureUploadTransferAcquireCode::TextureUploadDestinationNotLive);
    if (instance_generation.mTextureUploadTransferGeneration)
        return textureUploadTransferFailure(VulkanTextureUploadTransferAcquireCode::TextureUploadTransferAlreadyOwned);
    if (request.mNativeWindowGeneration != instance_generation.mNativeWindowGeneration ||
        request.mNativeWindowGeneration != instance_generation.surfaceNativeWindowGeneration())
        return textureUploadTransferFailure(VulkanTextureUploadTransferAcquireCode::NativeWindowGenerationMismatch);

    const VulkanTextureUploadSourceDescription      source_description      = request.mSourceDescription;
    const VulkanTextureUploadDestinationDescription destination_description = request.mDestinationDescription;
    const auto*                                     global_dispatch         = &*instance_generation.mGlobalDispatch;
    const VkInstance                                instance                = instance_generation.mInstance;
    const auto*                                     surface_generation      = instance_generation.mSurfaceGeneration.get();
    const VkSurfaceKHR                              surface                 = instance_generation.surface();
    const auto*                                     selection               = instance_generation.mPresentationDeviceGeneration.get();
    const auto*                                     logical                 = instance_generation.mLogicalDeviceGeneration.get();
    const auto*                                     source                  = instance_generation.mTextureUploadSourceGeneration.get();
    auto*                                           destination             = instance_generation.mTextureUploadDestinationGeneration.get();
    const auto                                      source_epoch            = instance_generation.mTextureUploadSourceEpoch;
    const auto                                      destination_epoch       = instance_generation.mTextureUploadDestinationEpoch;
    const auto                                      transfer_epoch          = instance_generation.mTextureUploadTransferEpoch;
    const auto                                      ownership_epoch         = instance_generation.mOwnershipTransitionEpoch;
    const VkBuffer                                  source_buffer           = source->buffer();
    const VkImage                                   destination_image       = destination->image();
    const std::uint64_t                             content_identity        = source->contentIdentity();
    const VkPhysicalDevice                          physical_device         = selection->physicalDevice();
    const VkDevice                                  device                  = logical->device();
    const VkQueue                                   queue                   = logical->queue();
    const auto                                      exact_chain             = [&]() noexcept
    {
        return instance_generation.mTextureUploadSourceEpoch == source_epoch &&
               instance_generation.mTextureUploadDestinationEpoch == destination_epoch &&
               instance_generation.mTextureUploadTransferEpoch == transfer_epoch && instance_generation.mGlobalDispatch &&
               &*instance_generation.mGlobalDispatch == global_dispatch && instance_generation.mInstance == instance &&
               instance_generation.mSurfaceGeneration.get() == surface_generation && instance_generation.surface() == surface &&
               selection->physicalDevice() == physical_device && logical->device() == device && logical->queue() == queue &&
               selection->selectedFor(instance, surface) && logical->createdFor(*selection) &&
               instance_generation.mPresentationDeviceGeneration.get() == selection &&
               instance_generation.mLogicalDeviceGeneration.get() == logical &&
               instance_generation.mTextureUploadSourceGeneration.get() == source &&
               instance_generation.mTextureUploadDestinationGeneration.get() == destination &&
               !instance_generation.mTextureUploadTransferGeneration && source && destination && source->buffer() == source_buffer &&
               source_buffer != VK_NULL_HANDLE && source->contentIdentity() == content_identity && content_identity != 0 &&
               source->matchesDescription(source_description) && destination->image() == destination_image &&
               destination_image != VK_NULL_HANDLE && !destination->isResident() &&
               destination->matchesDescription(destination_description) && source->resourceHandle() == destination->resourceHandle() &&
               source->expectedRevision() == destination->expectedRevision() && source->createdFor(*selection, *logical) &&
               destination->createdFor(*selection, *logical) && request.mSourceDescription == source_description &&
               request.mDestinationDescription == destination_description;
    };
    const auto freshness = [&]() noexcept -> VulkanTextureUploadTransferAcquireResult
    {
        if (auto result = textureUploadTransferFreshness(request, instance_generation, &instance_generation.mOwnershipTransitionEpoch,
                                                         ownership_epoch))
            return result;
        if (!exact_chain())
            return textureUploadTransferFailure(VulkanTextureUploadTransferAcquireCode::TextureUploadDestinationNotLive);
        return std::nullopt;
    };
    if (auto result = freshness())
        return result;
    VulkanInstanceGeneration::NativeAcquisitionGuard guard(instance_generation);
    auto resolved = resolveVulkanTextureUploadTransferGeneration(*selection, *logical, source_description, destination_description, *source,
                                                                 *destination);
    if (const auto* error = std::get_if<VulkanTextureUploadTransferResolutionError>(&resolved))
        return textureUploadTransferFailure(VulkanTextureUploadTransferAcquireCode::ResolutionFailure, *error);
    try
    {
        if (allocation_checkpoint)
        {
            allocation_checkpoint();
            if (!exact_chain())
                return textureUploadTransferFailure(VulkanTextureUploadTransferAcquireCode::TextureUploadDestinationNotLive);
        }
        auto pending =
            std::make_unique<VulkanTextureUploadTransferGeneration>(std::move(std::get<VulkanTextureUploadTransferGeneration>(resolved)));
        if (auto result = freshness())
            return result;
        if (!exact_chain() || !pending->createdFor(*selection, *logical) || !pending->matchesSourceDescription(source_description) ||
            !pending->matchesDestinationDescription(destination_description) ||
            !pending->retainsTextureUploadResources(*source, *destination))
            return textureUploadTransferFailure(VulkanTextureUploadTransferAcquireCode::TextureUploadDestinationNotLive);
        instance_generation.mTextureUploadTransferGeneration = std::move(pending);
        instance_generation.noteTextureUploadTransferTransition();
        return std::nullopt;
    }
    catch (const std::bad_alloc&)
    {
        return textureUploadTransferFailure(VulkanTextureUploadTransferAcquireCode::AllocationFailure);
    }
}

VulkanTextureUploadSampleBindingAcquireResult VulkanInstanceGenerationFactory::acquireTextureUploadSampleBinding(
    VulkanInstanceGeneration&                      instance_generation,
    const VulkanTextureUploadSampleBindingRequest& request,
    VulkanInstanceDetail::AllocationCheckpoint     allocation_checkpoint) noexcept
{
    if (!request.mInstanceOwnerCheck.mIsCurrent)
    {
        return textureUploadSampleBindingFailure(VulkanTextureUploadSampleBindingAcquireCode::InvalidInstanceOwnerCheck);
    }
    if (!request.mWindowGenerationCheck.mIsCurrent)
    {
        return textureUploadSampleBindingFailure(VulkanTextureUploadSampleBindingAcquireCode::InvalidWindowGenerationCheck);
    }
    if (request.mNativeWindowGeneration == 0)
    {
        return textureUploadSampleBindingFailure(VulkanTextureUploadSampleBindingAcquireCode::InvalidNativeWindowGeneration);
    }
    if (instance_generation.mTextureUploadDestinationTeardownDepth != 0 || instance_generation.mTextureUploadSourceTeardownDepth != 0 ||
        instance_generation.mTextureUploadTransferTeardownDepth != 0 || instance_generation.mTextureUploadSampleBindingTeardownDepth != 0 ||
        instance_generation.mTextureUploadSamplePipelineTeardownDepth != 0 ||
        instance_generation.mSwapchainPresentationTargetTeardownDepth != 0)
    {
        return textureUploadSampleBindingFailure(VulkanTextureUploadSampleBindingAcquireCode::NativeTeardownInProgress);
    }
    if (instance_generation.mInstance == VK_NULL_HANDLE || !instance_generation.mDestroyInstance || !instance_generation.mGlobalDispatch ||
        !instance_generation.mGlobalDispatch->getInstanceProcAddr() || instance_generation.mNativeWindowGeneration == 0)
    {
        return textureUploadSampleBindingFailure(VulkanTextureUploadSampleBindingAcquireCode::InstanceNotLive);
    }
    if (!instance_generation.mSurfaceGeneration || instance_generation.surface() == VK_NULL_HANDLE ||
        instance_generation.surfaceNativeWindowGeneration() == 0)
    {
        return textureUploadSampleBindingFailure(VulkanTextureUploadSampleBindingAcquireCode::SurfaceNotLive);
    }
    if (!instance_generation.mPresentationDeviceGeneration || instance_generation.physicalDevice() == VK_NULL_HANDLE)
    {
        return textureUploadSampleBindingFailure(VulkanTextureUploadSampleBindingAcquireCode::PresentationDeviceNotLive);
    }
    if (!instance_generation.mLogicalDeviceGeneration || instance_generation.logicalDevice() == VK_NULL_HANDLE ||
        instance_generation.presentationQueue() == VK_NULL_HANDLE)
    {
        return textureUploadSampleBindingFailure(VulkanTextureUploadSampleBindingAcquireCode::LogicalDeviceNotLive);
    }
    if (!instance_generation.mTextureUploadDestinationGeneration || instance_generation.textureUploadDestinationImage() == VK_NULL_HANDLE ||
        instance_generation.textureUploadDestinationMemory() == VK_NULL_HANDLE ||
        instance_generation.textureUploadDestinationImageView() == VK_NULL_HANDLE)
    {
        return textureUploadSampleBindingFailure(VulkanTextureUploadSampleBindingAcquireCode::TextureUploadDestinationNotLive);
    }
    if (instance_generation.mTextureUploadSampleBindingGeneration)
    {
        return textureUploadSampleBindingFailure(VulkanTextureUploadSampleBindingAcquireCode::TextureUploadSampleBindingAlreadyOwned);
    }
    if (request.mNativeWindowGeneration != instance_generation.mNativeWindowGeneration ||
        request.mNativeWindowGeneration != instance_generation.surfaceNativeWindowGeneration())
    {
        return textureUploadSampleBindingFailure(VulkanTextureUploadSampleBindingAcquireCode::NativeWindowGenerationMismatch);
    }

    const VulkanTextureUploadSampleBindingRequest       request_snapshot          = request;
    constexpr VulkanTextureUploadDestinationDescription canonical_destination     = vulkanTextureUploadDestinationDescription();
    const VulkanTextureUploadDestinationDescription     destination_description   = request_snapshot.mDestinationDescription;
    const VulkanTextureUploadSampleBindingDescription   description               = request_snapshot.mDescription;
    const auto*                                         global_dispatch           = &*instance_generation.mGlobalDispatch;
    const PFN_vkGetInstanceProcAddr                     get_instance_proc_addr    = global_dispatch->getInstanceProcAddr();
    const VkInstance                                    instance                  = instance_generation.mInstance;
    const std::uint64_t                                 native_window_generation  = instance_generation.mNativeWindowGeneration;
    const auto*                                         surface_generation        = instance_generation.mSurfaceGeneration.get();
    const std::uint64_t                                 surface_window_generation = instance_generation.surfaceNativeWindowGeneration();
    const VkSurfaceKHR                                  surface                   = instance_generation.surface();
    const auto*                                         selection                 = instance_generation.mPresentationDeviceGeneration.get();
    const auto*                                         logical                   = instance_generation.mLogicalDeviceGeneration.get();
    const auto*                                         destination       = instance_generation.mTextureUploadDestinationGeneration.get();
    const std::uint64_t                                 ownership_epoch   = instance_generation.mOwnershipTransitionEpoch;
    const std::uint64_t                                 destination_epoch = instance_generation.mTextureUploadDestinationEpoch;
    const std::uint64_t                                 binding_epoch     = instance_generation.mTextureUploadSampleBindingEpoch;
    const VkPhysicalDevice                              physical_device   = selection->physicalDevice();
    const std::uint32_t                                 physical_device_index  = selection->physicalDeviceIndex();
    const VkDevice                                      device                 = logical->device();
    const VkQueue                                       queue                  = logical->queue();
    const std::uint32_t                                 queue_family           = logical->queueFamilyIndex();
    const std::uint32_t                                 queue_index            = logical->queueIndex();
    const VkImage                                       destination_image      = destination->image();
    const VkDeviceMemory                                destination_memory     = destination->memory();
    const VkImageView                                   destination_view       = destination->imageView();
    const VkImageSubresourceRange                       destination_view_range = destination->viewRange();
    const bool                                          destination_resident   = destination->isResident();
    const std::uint64_t                                 resident_revision      = destination->residentRevision();
    const std::uint64_t                                 resident_identity      = destination->residentContentIdentity();
    const LLRenderContract::ImageState                  destination_state      = destination->currentState();

    if (!destination->createdFor(*selection, *logical) || !destination->matchesDescription(canonical_destination) ||
        destination->resourceHandle() != canonical_destination.mHandle ||
        destination->expectedRevision() != canonical_destination.mExpectedRevision ||
        destination->mipLevels() != canonical_destination.mMipLevels || destination->arrayLayers() != canonical_destination.mArrayLayers ||
        destination->imageViewType() != VK_IMAGE_VIEW_TYPE_2D || destination_view_range.aspectMask != VK_IMAGE_ASPECT_COLOR_BIT ||
        destination_view_range.baseMipLevel != 0 || destination_view_range.levelCount != canonical_destination.mMipLevels ||
        destination_view_range.baseArrayLayer != 0 || destination_view_range.layerCount != canonical_destination.mArrayLayers)
    {
        return textureUploadSampleBindingFailure(VulkanTextureUploadSampleBindingAcquireCode::TextureUploadDestinationNotLive);
    }

    const auto exact_chain = [&]() noexcept
    {
        return instance_generation.mOwnershipTransitionEpoch == ownership_epoch &&
               instance_generation.mTextureUploadDestinationEpoch == destination_epoch &&
               instance_generation.mTextureUploadSampleBindingEpoch == binding_epoch && instance_generation.mGlobalDispatch &&
               &*instance_generation.mGlobalDispatch == global_dispatch &&
               instance_generation.mGlobalDispatch->getInstanceProcAddr() == get_instance_proc_addr &&
               instance_generation.mInstance == instance && instance_generation.mDestroyInstance &&
               instance_generation.mNativeWindowGeneration == native_window_generation &&
               native_window_generation == request.mNativeWindowGeneration &&
               instance_generation.mSurfaceGeneration.get() == surface_generation && surface_generation &&
               surface_generation->nativeWindowGeneration() == surface_window_generation &&
               surface_window_generation == request.mNativeWindowGeneration && surface_generation->surface() == surface &&
               instance_generation.mPresentationDeviceGeneration.get() == selection && selection &&
               selection->getInstanceProcAddr() == get_instance_proc_addr && selection->instance() == instance &&
               selection->surface() == surface && selection->physicalDevice() == physical_device &&
               selection->physicalDeviceIndex() == physical_device_index && selection->selectedFor(instance, surface) &&
               instance_generation.mLogicalDeviceGeneration.get() == logical && logical &&
               logical->getInstanceProcAddr() == get_instance_proc_addr && logical->instance() == instance &&
               logical->surface() == surface && logical->physicalDevice() == physical_device &&
               logical->physicalDeviceIndex() == physical_device_index && logical->device() == device && logical->queue() == queue &&
               logical->queueFamilyIndex() == queue_family && logical->queueIndex() == queue_index && logical->createdFor(*selection) &&
               instance_generation.mTextureUploadDestinationGeneration.get() == destination && destination &&
               !instance_generation.mTextureUploadSampleBindingGeneration && destination->createdFor(*selection, *logical) &&
               destination->matchesDescription(canonical_destination) && destination->resourceHandle() == canonical_destination.mHandle &&
               destination->expectedRevision() == canonical_destination.mExpectedRevision && destination->image() == destination_image &&
               destination_image != VK_NULL_HANDLE && destination->memory() == destination_memory && destination_memory != VK_NULL_HANDLE &&
               destination->imageView() == destination_view && destination_view != VK_NULL_HANDLE &&
               destination->imageViewType() == VK_IMAGE_VIEW_TYPE_2D &&
               destination->viewRange().aspectMask == destination_view_range.aspectMask &&
               destination->viewRange().baseMipLevel == destination_view_range.baseMipLevel &&
               destination->viewRange().levelCount == destination_view_range.levelCount &&
               destination->viewRange().baseArrayLayer == destination_view_range.baseArrayLayer &&
               destination->viewRange().layerCount == destination_view_range.layerCount &&
               destination->isResident() == destination_resident && destination->residentRevision() == resident_revision &&
               destination->residentContentIdentity() == resident_identity && destination->currentState() == destination_state;
    };
    const auto freshness = [&]() noexcept -> VulkanTextureUploadSampleBindingAcquireResult
    {
        if (auto result = textureUploadSampleBindingFreshness(request,
                                                              request_snapshot,
                                                              instance_generation,
                                                              &instance_generation.mOwnershipTransitionEpoch,
                                                              ownership_epoch))
        {
            return result;
        }
        if (!exact_chain())
        {
            return textureUploadSampleBindingFailure(VulkanTextureUploadSampleBindingAcquireCode::TextureUploadDestinationNotLive);
        }
        return std::nullopt;
    };

    if (auto result = freshness())
    {
        return result;
    }
    VulkanInstanceGeneration::NativeAcquisitionGuard guard(instance_generation);
    VulkanTextureUploadSampleBindingResolutionResult resolved =
        resolveVulkanTextureUploadSampleBindingGeneration(*selection, *logical, destination_description, description, *destination);
    if (const auto* error = std::get_if<VulkanTextureUploadSampleBindingResolutionError>(&resolved))
    {
        return textureUploadSampleBindingFailure(VulkanTextureUploadSampleBindingAcquireCode::ResolutionFailure, *error);
    }

    try
    {
        if (allocation_checkpoint)
        {
            allocation_checkpoint();
            if (auto result = freshness())
            {
                return result;
            }
        }
        auto pending = std::make_unique<VulkanTextureUploadSampleBindingGeneration>(
            std::move(std::get<VulkanTextureUploadSampleBindingGeneration>(resolved)));
        if (auto result = freshness())
        {
            return result;
        }
        if (!exact_chain() || !pending->createdFor(*selection, *logical, *destination) || !pending->matchesDescription(description) ||
            !pending->retainsTextureUploadDestinationGeneration(*destination) ||
            pending->samplerResourceHandle() != description.mSampler.mHandle || destination_description != canonical_destination ||
            pending->destinationResourceHandle() != destination_description.mHandle ||
            pending->expectedRevision() != destination_description.mExpectedRevision || pending->residentRevision() != resident_revision ||
            pending->residentContentIdentity() != resident_identity || pending->destinationImageView() != destination_view ||
            pending->destinationImageLayout() != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ||
            pending->descriptorSetIndex() != description.mDescriptorSetIndex || pending->binding() != description.mBinding ||
            pending->sampler() == VK_NULL_HANDLE || pending->descriptorSetLayout() == VK_NULL_HANDLE ||
            pending->pipelineLayout() == VK_NULL_HANDLE || pending->descriptorPool() == VK_NULL_HANDLE ||
            pending->descriptorSet() == VK_NULL_HANDLE)
        {
            return textureUploadSampleBindingFailure(VulkanTextureUploadSampleBindingAcquireCode::TextureUploadDestinationNotLive);
        }
        instance_generation.mTextureUploadSampleBindingGeneration = std::move(pending);
        instance_generation.noteTextureUploadSampleBindingTransition();
        return std::nullopt;
    }
    catch (const std::bad_alloc&)
    {
        return textureUploadSampleBindingFailure(VulkanTextureUploadSampleBindingAcquireCode::AllocationFailure);
    }
}

VulkanTextureUploadSamplePipelineAcquireResult VulkanInstanceGenerationFactory::acquireTextureUploadSamplePipeline(
    VulkanInstanceGeneration&                       instance_generation,
    const VulkanTextureUploadSamplePipelineRequest& request,
    VulkanInstanceDetail::AllocationCheckpoint      allocation_checkpoint) noexcept
{
    if (!request.mInstanceOwnerCheck.mIsCurrent)
    {
        return textureUploadSamplePipelineFailure(VulkanTextureUploadSamplePipelineAcquireCode::InvalidInstanceOwnerCheck);
    }
    if (!request.mWindowGenerationCheck.mIsCurrent)
    {
        return textureUploadSamplePipelineFailure(VulkanTextureUploadSamplePipelineAcquireCode::InvalidWindowGenerationCheck);
    }
    if (request.mNativeWindowGeneration == 0)
    {
        return textureUploadSamplePipelineFailure(VulkanTextureUploadSamplePipelineAcquireCode::InvalidNativeWindowGeneration);
    }
    if (request.mDrawableExtent.width == 0 || request.mDrawableExtent.height == 0)
    {
        return textureUploadSamplePipelineFailure(VulkanTextureUploadSamplePipelineAcquireCode::InvalidDrawableExtent);
    }
    if (instance_generation.mTextureUploadDestinationTeardownDepth != 0 || instance_generation.mTextureUploadSourceTeardownDepth != 0 ||
        instance_generation.mTextureUploadTransferTeardownDepth != 0 || instance_generation.mTextureUploadSampleBindingTeardownDepth != 0 ||
        instance_generation.mTextureUploadSamplePipelineTeardownDepth != 0 ||
        instance_generation.mSwapchainPresentationTargetTeardownDepth != 0)
    {
        return textureUploadSamplePipelineFailure(VulkanTextureUploadSamplePipelineAcquireCode::NativeTeardownInProgress);
    }
    if (instance_generation.mInstance == VK_NULL_HANDLE || !instance_generation.mDestroyInstance || !instance_generation.mGlobalDispatch ||
        !instance_generation.mGlobalDispatch->getInstanceProcAddr() || instance_generation.mNativeWindowGeneration == 0)
    {
        return textureUploadSamplePipelineFailure(VulkanTextureUploadSamplePipelineAcquireCode::InstanceNotLive);
    }
    if (!instance_generation.mSurfaceGeneration || instance_generation.surface() == VK_NULL_HANDLE ||
        instance_generation.surfaceNativeWindowGeneration() == 0)
    {
        return textureUploadSamplePipelineFailure(VulkanTextureUploadSamplePipelineAcquireCode::SurfaceNotLive);
    }
    if (!instance_generation.mPresentationDeviceGeneration || instance_generation.physicalDevice() == VK_NULL_HANDLE)
    {
        return textureUploadSamplePipelineFailure(VulkanTextureUploadSamplePipelineAcquireCode::PresentationDeviceNotLive);
    }
    if (!instance_generation.mLogicalDeviceGeneration || instance_generation.logicalDevice() == VK_NULL_HANDLE ||
        instance_generation.presentationQueue() == VK_NULL_HANDLE)
    {
        return textureUploadSamplePipelineFailure(VulkanTextureUploadSamplePipelineAcquireCode::LogicalDeviceNotLive);
    }
    if (!instance_generation.mTextureUploadDestinationGeneration || instance_generation.textureUploadDestinationImage() == VK_NULL_HANDLE ||
        instance_generation.textureUploadDestinationMemory() == VK_NULL_HANDLE ||
        instance_generation.textureUploadDestinationImageView() == VK_NULL_HANDLE)
    {
        return textureUploadSamplePipelineFailure(VulkanTextureUploadSamplePipelineAcquireCode::TextureUploadDestinationNotLive);
    }
    if (!instance_generation.mTextureUploadSampleBindingGeneration ||
        instance_generation.textureUploadSampleBindingPipelineLayout() == VK_NULL_HANDLE ||
        instance_generation.textureUploadSampleBindingDescriptorSet() == VK_NULL_HANDLE)
    {
        return textureUploadSamplePipelineFailure(VulkanTextureUploadSamplePipelineAcquireCode::TextureUploadSampleBindingNotLive);
    }
    if (!instance_generation.mSwapchainConfigurationGeneration)
    {
        return textureUploadSamplePipelineFailure(VulkanTextureUploadSamplePipelineAcquireCode::SwapchainConfigurationNotLive);
    }
    if (!instance_generation.mSwapchainGeneration || instance_generation.swapchain() == VK_NULL_HANDLE)
    {
        return textureUploadSamplePipelineFailure(VulkanTextureUploadSamplePipelineAcquireCode::SwapchainNotLive);
    }
    if (!instance_generation.mSwapchainImagesGeneration || instance_generation.resolvedSwapchainImageCount() == 0 ||
        instance_generation.mSwapchainImagesGeneration->imageFormat() == VK_FORMAT_UNDEFINED)
    {
        return textureUploadSamplePipelineFailure(VulkanTextureUploadSamplePipelineAcquireCode::SwapchainImagesNotLive);
    }
    if (!instance_generation.mSwapchainPresentationTargetGeneration ||
        instance_generation.swapchainPresentationRenderPass() == VK_NULL_HANDLE)
    {
        return textureUploadSamplePipelineFailure(VulkanTextureUploadSamplePipelineAcquireCode::SwapchainPresentationTargetNotLive);
    }
    if (instance_generation.mTextureUploadSamplePipelineGeneration)
    {
        return textureUploadSamplePipelineFailure(VulkanTextureUploadSamplePipelineAcquireCode::TextureUploadSamplePipelineAlreadyOwned);
    }
    if (request.mNativeWindowGeneration != instance_generation.mNativeWindowGeneration ||
        request.mNativeWindowGeneration != instance_generation.surfaceNativeWindowGeneration())
    {
        return textureUploadSamplePipelineFailure(VulkanTextureUploadSamplePipelineAcquireCode::NativeWindowGenerationMismatch);
    }
    if (request.mDrawableExtent.width != instance_generation.swapchainDrawableExtent().width ||
        request.mDrawableExtent.height != instance_generation.swapchainDrawableExtent().height)
    {
        return textureUploadSamplePipelineFailure(VulkanTextureUploadSamplePipelineAcquireCode::DrawableExtentMismatch);
    }

    const VulkanTextureUploadSamplePipelineRequest        request_snapshot           = request;
    constexpr VulkanTextureUploadDestinationDescription   canonical_destination      = vulkanTextureUploadDestinationDescription();
    constexpr VulkanTextureUploadSampleBindingDescription canonical_binding          = vulkanTextureUploadSampleBindingDescription();
    const VulkanTextureUploadDestinationDescription       destination_description    = request_snapshot.mDestinationDescription;
    const VulkanTextureUploadSampleBindingDescription     sample_binding_description = request_snapshot.mSampleBindingDescription;
    const VulkanTextureUploadSamplePipelineDescription    description                = request_snapshot.mDescription;

    const auto*                        global_dispatch           = &*instance_generation.mGlobalDispatch;
    const PFN_vkGetInstanceProcAddr    get_instance_proc_addr    = global_dispatch->getInstanceProcAddr();
    const VkInstance                   instance                  = instance_generation.mInstance;
    const std::uint64_t                native_window_generation  = instance_generation.mNativeWindowGeneration;
    const auto*                        surface_generation        = instance_generation.mSurfaceGeneration.get();
    const std::uint64_t                surface_window_generation = instance_generation.surfaceNativeWindowGeneration();
    const VkSurfaceKHR                 surface                   = instance_generation.surface();
    const auto*                        selection                 = instance_generation.mPresentationDeviceGeneration.get();
    const auto*                        logical                   = instance_generation.mLogicalDeviceGeneration.get();
    const auto*                        destination               = instance_generation.mTextureUploadDestinationGeneration.get();
    const auto*                        sample_binding            = instance_generation.mTextureUploadSampleBindingGeneration.get();
    const auto*                        configuration             = instance_generation.mSwapchainConfigurationGeneration.get();
    const auto*                        swapchain_generation      = instance_generation.mSwapchainGeneration.get();
    const auto*                        images                    = instance_generation.mSwapchainImagesGeneration.get();
    const auto*                        presentation_target       = instance_generation.mSwapchainPresentationTargetGeneration.get();
    const std::uint64_t                ownership_epoch           = instance_generation.mOwnershipTransitionEpoch;
    const std::uint64_t                destination_epoch         = instance_generation.mTextureUploadDestinationEpoch;
    const std::uint64_t                binding_epoch             = instance_generation.mTextureUploadSampleBindingEpoch;
    const std::uint64_t                target_epoch              = instance_generation.mSwapchainPresentationTargetEpoch;
    const std::uint64_t                pipeline_epoch            = instance_generation.mTextureUploadSamplePipelineEpoch;
    const VkPhysicalDevice             physical_device           = selection->physicalDevice();
    const std::uint32_t                physical_device_index     = selection->physicalDeviceIndex();
    const VkDevice                     device                    = logical->device();
    const VkQueue                      queue                     = logical->queue();
    const std::uint32_t                queue_family              = logical->queueFamilyIndex();
    const std::uint32_t                queue_index               = logical->queueIndex();
    const VkImage                      destination_image         = destination->image();
    const VkDeviceMemory               destination_memory        = destination->memory();
    const VkImageView                  destination_view          = destination->imageView();
    const VkImageSubresourceRange      destination_view_range    = destination->viewRange();
    const bool                         destination_resident      = destination->isResident();
    const std::uint64_t                resident_revision         = destination->residentRevision();
    const std::uint64_t                resident_identity         = destination->residentContentIdentity();
    const LLRenderContract::ImageState destination_state         = destination->currentState();
    const VkSampler                    sampler                   = sample_binding->sampler();
    const VkDescriptorSetLayout        descriptor_set_layout     = sample_binding->descriptorSetLayout();
    const VkPipelineLayout             pipeline_layout           = sample_binding->pipelineLayout();
    const VkDescriptorPool             descriptor_pool           = sample_binding->descriptorPool();
    const VkDescriptorSet              descriptor_set            = sample_binding->descriptorSet();
    const VkExtent2D                   drawable_extent           = configuration->drawableExtent();
    const VkSwapchainKHR               swapchain                 = swapchain_generation->swapchain();
    const std::uint32_t                image_count               = images->imageCount();
    const VkFormat                     image_format              = images->imageFormat();
    const VkRenderPass                 render_pass               = presentation_target->renderPass();
    const std::uint32_t                framebuffer_count         = presentation_target->framebufferCount();
    const VkExtent2D                   target_extent             = presentation_target->imageExtent();

    if (destination_description != canonical_destination || !destination->createdFor(*selection, *logical) ||
        !destination->matchesDescription(canonical_destination) || destination->resourceHandle() != canonical_destination.mHandle ||
        destination->expectedRevision() != LLRenderContract::TEXTURE_UPLOAD_REVISION ||
        destination->mipLevels() != canonical_destination.mMipLevels || canonical_destination.mMipLevels != 3 ||
        destination->arrayLayers() != canonical_destination.mArrayLayers || destination->imageViewType() != VK_IMAGE_VIEW_TYPE_2D ||
        destination_view_range.aspectMask != VK_IMAGE_ASPECT_COLOR_BIT || destination_view_range.baseMipLevel != 0 ||
        destination_view_range.levelCount != canonical_destination.mMipLevels || destination_view_range.baseArrayLayer != 0 ||
        destination_view_range.layerCount != canonical_destination.mArrayLayers || !destination_resident ||
        resident_revision != LLRenderContract::TEXTURE_UPLOAD_REVISION || resident_identity == 0 ||
        destination_state != LLRenderContract::ImageState::ShaderRead)
    {
        return textureUploadSamplePipelineFailure(VulkanTextureUploadSamplePipelineAcquireCode::TextureUploadDestinationNotLive);
    }
    if (sample_binding_description != canonical_binding || !sample_binding->createdFor(*selection, *logical, *destination) ||
        !sample_binding->matchesDescription(canonical_binding) ||
        !sample_binding->retainsTextureUploadDestinationGeneration(*destination) ||
        sample_binding->destinationResourceHandle() != canonical_destination.mHandle ||
        sample_binding->expectedRevision() != LLRenderContract::TEXTURE_UPLOAD_REVISION ||
        sample_binding->residentRevision() != resident_revision || sample_binding->residentContentIdentity() != resident_identity ||
        sample_binding->destinationImageView() != destination_view ||
        sample_binding->destinationImageLayout() != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ||
        sample_binding->descriptorSetIndex() != canonical_binding.mDescriptorSetIndex ||
        sample_binding->binding() != canonical_binding.mBinding || sampler == VK_NULL_HANDLE || descriptor_set_layout == VK_NULL_HANDLE ||
        pipeline_layout == VK_NULL_HANDLE || descriptor_pool == VK_NULL_HANDLE || descriptor_set == VK_NULL_HANDLE)
    {
        return textureUploadSamplePipelineFailure(VulkanTextureUploadSamplePipelineAcquireCode::TextureUploadSampleBindingNotLive);
    }
    if (!selection->selectedFor(instance, surface))
    {
        return textureUploadSamplePipelineFailure(VulkanTextureUploadSamplePipelineAcquireCode::PresentationDeviceNotLive);
    }
    if (!logical->createdFor(*selection))
    {
        return textureUploadSamplePipelineFailure(VulkanTextureUploadSamplePipelineAcquireCode::LogicalDeviceNotLive);
    }
    if (!configuration->createdFor(*selection, *logical, request_snapshot.mDrawableExtent))
    {
        return textureUploadSamplePipelineFailure(VulkanTextureUploadSamplePipelineAcquireCode::SwapchainConfigurationNotLive);
    }
    if (!swapchain_generation->createdFor(*logical, *configuration))
    {
        return textureUploadSamplePipelineFailure(VulkanTextureUploadSamplePipelineAcquireCode::SwapchainNotLive);
    }
    if (!images->createdFor(*logical, *configuration, *swapchain_generation))
    {
        return textureUploadSamplePipelineFailure(VulkanTextureUploadSamplePipelineAcquireCode::SwapchainImagesNotLive);
    }
    if (!presentation_target->createdFor(*logical, *configuration, *swapchain_generation, *images))
    {
        return textureUploadSamplePipelineFailure(VulkanTextureUploadSamplePipelineAcquireCode::SwapchainPresentationTargetNotLive);
    }

    const auto destination_snapshot_current = [&]() noexcept
    {
        return instance_generation.mTextureUploadDestinationGeneration.get() == destination && destination &&
               destination->createdFor(*selection, *logical) && destination->matchesDescription(canonical_destination) &&
               destination->resourceHandle() == canonical_destination.mHandle &&
               destination->expectedRevision() == LLRenderContract::TEXTURE_UPLOAD_REVISION && destination->image() == destination_image &&
               destination_image != VK_NULL_HANDLE && destination->memory() == destination_memory && destination_memory != VK_NULL_HANDLE &&
               destination->imageView() == destination_view && destination_view != VK_NULL_HANDLE &&
               destination->imageViewType() == VK_IMAGE_VIEW_TYPE_2D &&
               destination->viewRange().aspectMask == destination_view_range.aspectMask &&
               destination->viewRange().baseMipLevel == destination_view_range.baseMipLevel &&
               destination->viewRange().levelCount == destination_view_range.levelCount &&
               destination->viewRange().baseArrayLayer == destination_view_range.baseArrayLayer &&
               destination->viewRange().layerCount == destination_view_range.layerCount &&
               destination->isResident() == destination_resident && destination->residentRevision() == resident_revision &&
               destination->residentContentIdentity() == resident_identity && destination->currentState() == destination_state;
    };
    const auto binding_snapshot_current = [&]() noexcept
    {
        return instance_generation.mTextureUploadSampleBindingGeneration.get() == sample_binding && sample_binding &&
               sample_binding->createdFor(*selection, *logical, *destination) && sample_binding->matchesDescription(canonical_binding) &&
               sample_binding->retainsTextureUploadDestinationGeneration(*destination) && sample_binding->sampler() == sampler &&
               sample_binding->descriptorSetLayout() == descriptor_set_layout && sample_binding->pipelineLayout() == pipeline_layout &&
               sample_binding->descriptorPool() == descriptor_pool && sample_binding->descriptorSet() == descriptor_set &&
               sample_binding->residentRevision() == resident_revision && sample_binding->residentContentIdentity() == resident_identity &&
               sample_binding->destinationImageView() == destination_view;
    };
    const auto target_snapshot_current = [&]() noexcept
    {
        return instance_generation.mSwapchainPresentationTargetGeneration.get() == presentation_target && presentation_target &&
               presentation_target->renderPass() == render_pass && render_pass != VK_NULL_HANDLE &&
               presentation_target->framebufferCount() == framebuffer_count &&
               presentation_target->imageExtent().width == target_extent.width &&
               presentation_target->imageExtent().height == target_extent.height &&
               presentation_target->createdFor(*logical, *configuration, *swapchain_generation, *images);
    };

    const auto exact_chain = [&]() noexcept
    {
        return instance_generation.mOwnershipTransitionEpoch == ownership_epoch &&
               instance_generation.mTextureUploadDestinationEpoch == destination_epoch &&
               instance_generation.mTextureUploadSampleBindingEpoch == binding_epoch &&
               instance_generation.mSwapchainPresentationTargetEpoch == target_epoch &&
               instance_generation.mTextureUploadSamplePipelineEpoch == pipeline_epoch && instance_generation.mGlobalDispatch &&
               &*instance_generation.mGlobalDispatch == global_dispatch &&
               instance_generation.mGlobalDispatch->getInstanceProcAddr() == get_instance_proc_addr &&
               instance_generation.mInstance == instance && instance_generation.mDestroyInstance &&
               instance_generation.mNativeWindowGeneration == native_window_generation &&
               native_window_generation == request.mNativeWindowGeneration &&
               instance_generation.mSurfaceGeneration.get() == surface_generation && surface_generation &&
               surface_generation->nativeWindowGeneration() == surface_window_generation &&
               surface_window_generation == request.mNativeWindowGeneration && surface_generation->surface() == surface &&
               instance_generation.mPresentationDeviceGeneration.get() == selection && selection &&
               selection->getInstanceProcAddr() == get_instance_proc_addr && selection->instance() == instance &&
               selection->surface() == surface && selection->physicalDevice() == physical_device &&
               selection->physicalDeviceIndex() == physical_device_index && selection->selectedFor(instance, surface) &&
               instance_generation.mLogicalDeviceGeneration.get() == logical && logical &&
               logical->getInstanceProcAddr() == get_instance_proc_addr && logical->instance() == instance &&
               logical->surface() == surface && logical->physicalDevice() == physical_device &&
               logical->physicalDeviceIndex() == physical_device_index && logical->device() == device && logical->queue() == queue &&
               logical->queueFamilyIndex() == queue_family && logical->queueIndex() == queue_index && logical->createdFor(*selection) &&
               instance_generation.mTextureUploadDestinationGeneration.get() == destination && destination &&
               destination->createdFor(*selection, *logical) && destination->matchesDescription(canonical_destination) &&
               destination->resourceHandle() == canonical_destination.mHandle &&
               destination->expectedRevision() == LLRenderContract::TEXTURE_UPLOAD_REVISION && destination->image() == destination_image &&
               destination_image != VK_NULL_HANDLE && destination->memory() == destination_memory && destination_memory != VK_NULL_HANDLE &&
               destination->imageView() == destination_view && destination_view != VK_NULL_HANDLE &&
               destination->imageViewType() == VK_IMAGE_VIEW_TYPE_2D &&
               destination->viewRange().aspectMask == destination_view_range.aspectMask &&
               destination->viewRange().baseMipLevel == destination_view_range.baseMipLevel &&
               destination->viewRange().levelCount == destination_view_range.levelCount &&
               destination->viewRange().baseArrayLayer == destination_view_range.baseArrayLayer &&
               destination->viewRange().layerCount == destination_view_range.layerCount &&
               destination->isResident() == destination_resident && destination->residentRevision() == resident_revision &&
               destination->residentContentIdentity() == resident_identity && destination->currentState() == destination_state &&
               instance_generation.mTextureUploadSampleBindingGeneration.get() == sample_binding && sample_binding &&
               sample_binding->createdFor(*selection, *logical, *destination) && sample_binding->matchesDescription(canonical_binding) &&
               sample_binding->retainsTextureUploadDestinationGeneration(*destination) && sample_binding->sampler() == sampler &&
               sample_binding->descriptorSetLayout() == descriptor_set_layout && sample_binding->pipelineLayout() == pipeline_layout &&
               sample_binding->descriptorPool() == descriptor_pool && sample_binding->descriptorSet() == descriptor_set &&
               sample_binding->residentRevision() == resident_revision && sample_binding->residentContentIdentity() == resident_identity &&
               sample_binding->destinationImageView() == destination_view &&
               instance_generation.mSwapchainConfigurationGeneration.get() == configuration && configuration &&
               configuration->drawableExtent().width == drawable_extent.width &&
               configuration->drawableExtent().height == drawable_extent.height &&
               configuration->drawableExtent().width == request.mDrawableExtent.width &&
               configuration->drawableExtent().height == request.mDrawableExtent.height &&
               configuration->createdFor(*selection, *logical, drawable_extent) &&
               instance_generation.mSwapchainGeneration.get() == swapchain_generation && swapchain_generation &&
               swapchain_generation->swapchain() == swapchain && swapchain != VK_NULL_HANDLE &&
               swapchain_generation->createdFor(*logical, *configuration) &&
               instance_generation.mSwapchainImagesGeneration.get() == images && images && images->imageCount() == image_count &&
               images->imageFormat() == image_format && images->createdFor(*logical, *configuration, *swapchain_generation) &&
               instance_generation.mSwapchainPresentationTargetGeneration.get() == presentation_target && presentation_target &&
               presentation_target->renderPass() == render_pass && render_pass != VK_NULL_HANDLE &&
               presentation_target->framebufferCount() == framebuffer_count &&
               presentation_target->imageExtent().width == target_extent.width &&
               presentation_target->imageExtent().height == target_extent.height &&
               presentation_target->createdFor(*logical, *configuration, *swapchain_generation, *images) &&
               !instance_generation.mTextureUploadSamplePipelineGeneration;
    };
    const auto freshness = [&]() noexcept -> VulkanTextureUploadSamplePipelineAcquireResult
    {
        if (auto result = textureUploadSamplePipelineFreshness(request,
                                                               request_snapshot,
                                                               instance_generation,
                                                               &instance_generation.mOwnershipTransitionEpoch,
                                                               ownership_epoch,
                                                               &instance_generation.mTextureUploadDestinationEpoch,
                                                               destination_epoch,
                                                               &instance_generation.mTextureUploadSampleBindingEpoch,
                                                               binding_epoch,
                                                               &instance_generation.mSwapchainPresentationTargetEpoch,
                                                               target_epoch,
                                                               &instance_generation.mTextureUploadSamplePipelineEpoch,
                                                               pipeline_epoch))
        {
            return result;
        }
        if (!destination_snapshot_current())
        {
            return textureUploadSamplePipelineFailure(VulkanTextureUploadSamplePipelineAcquireCode::TextureUploadDestinationNotLive);
        }
        if (!binding_snapshot_current())
        {
            return textureUploadSamplePipelineFailure(VulkanTextureUploadSamplePipelineAcquireCode::TextureUploadSampleBindingNotLive);
        }
        if (!target_snapshot_current())
        {
            return textureUploadSamplePipelineFailure(VulkanTextureUploadSamplePipelineAcquireCode::SwapchainPresentationTargetNotLive);
        }
        if (!exact_chain())
        {
            return textureUploadSamplePipelineFailure(VulkanTextureUploadSamplePipelineAcquireCode::SwapchainPresentationTargetNotLive);
        }
        return std::nullopt;
    };

    if (auto result = freshness())
    {
        return result;
    }
    VulkanInstanceGeneration::NativeAcquisitionGuard  guard(instance_generation);
    VulkanTextureUploadSamplePipelineResolutionResult resolved =
        resolveVulkanTextureUploadSamplePipelineGeneration(*selection,
                                                           *logical,
                                                           destination_description,
                                                           sample_binding_description,
                                                           description,
                                                           *destination,
                                                           *sample_binding,
                                                           *configuration,
                                                           *swapchain_generation,
                                                           *images,
                                                           *presentation_target);
    if (const auto* error = std::get_if<VulkanTextureUploadSamplePipelineResolutionError>(&resolved))
    {
        return textureUploadSamplePipelineFailure(VulkanTextureUploadSamplePipelineAcquireCode::ResolutionFailure, *error);
    }

    try
    {
        if (allocation_checkpoint)
        {
            allocation_checkpoint();
            if (auto result = freshness())
            {
                return result;
            }
        }
        auto pending = std::make_unique<VulkanTextureUploadSamplePipelineGeneration>(
            std::move(std::get<VulkanTextureUploadSamplePipelineGeneration>(resolved)));
        if (auto result = freshness())
        {
            return result;
        }
        if (!exact_chain() ||
            !pending->createdFor(*selection,
                                 *logical,
                                 *destination,
                                 *sample_binding,
                                 *configuration,
                                 *swapchain_generation,
                                 *images,
                                 *presentation_target) ||
            !pending->matchesDescription(description) || !pending->retainsTextureUploadSampleBindingGeneration(*sample_binding) ||
            pending->pipelineResourceHandle() != description.mHandle || pending->pipelineLayout() != pipeline_layout ||
            pending->pipeline() == VK_NULL_HANDLE)
        {
            return textureUploadSamplePipelineFailure(VulkanTextureUploadSamplePipelineAcquireCode::SwapchainPresentationTargetNotLive);
        }
        instance_generation.mTextureUploadSamplePipelineGeneration = std::move(pending);
        instance_generation.noteTextureUploadSamplePipelineTransition();
        return std::nullopt;
    }
    catch (const std::bad_alloc&)
    {
        return textureUploadSamplePipelineFailure(VulkanTextureUploadSamplePipelineAcquireCode::AllocationFailure);
    }
}

VulkanTextureUploadTransferParentOperationResult VulkanInstanceGenerationFactory::operateTextureUploadTransfer(
    VulkanInstanceGeneration& instance_generation, const VulkanTextureUploadTransferOperationRequest& request,
    TextureUploadTransferParentOperation operation) noexcept
{
    if (!request.mInstanceOwnerCheck.mIsCurrent)
        return textureUploadTransferOperationFailure(VulkanTextureUploadTransferParentOperationCode::InvalidInstanceOwnerCheck);
    if (!request.mWindowGenerationCheck.mIsCurrent)
        return textureUploadTransferOperationFailure(VulkanTextureUploadTransferParentOperationCode::InvalidWindowGenerationCheck);
    if (request.mNativeWindowGeneration == 0)
        return textureUploadTransferOperationFailure(VulkanTextureUploadTransferParentOperationCode::InvalidNativeWindowGeneration);
    if (instance_generation.mNativeAcquisitionDepth != 0)
        return textureUploadTransferOperationFailure(VulkanTextureUploadTransferParentOperationCode::NativeOperationInProgress);
    VulkanInstanceGeneration::NativeAcquisitionGuard guard(instance_generation);
    if (instance_generation.mInstance == VK_NULL_HANDLE || !instance_generation.mGlobalDispatch)
        return textureUploadTransferOperationFailure(VulkanTextureUploadTransferParentOperationCode::InstanceNotLive);
    if (!instance_generation.mSurfaceGeneration)
        return textureUploadTransferOperationFailure(VulkanTextureUploadTransferParentOperationCode::SurfaceNotLive);
    if (!instance_generation.mPresentationDeviceGeneration)
        return textureUploadTransferOperationFailure(VulkanTextureUploadTransferParentOperationCode::PresentationDeviceNotLive);
    if (!instance_generation.mLogicalDeviceGeneration)
        return textureUploadTransferOperationFailure(VulkanTextureUploadTransferParentOperationCode::LogicalDeviceNotLive);
    if (!instance_generation.mTextureUploadSourceGeneration)
        return textureUploadTransferOperationFailure(VulkanTextureUploadTransferParentOperationCode::TextureUploadSourceNotLive);
    if (!instance_generation.mTextureUploadDestinationGeneration)
        return textureUploadTransferOperationFailure(VulkanTextureUploadTransferParentOperationCode::TextureUploadDestinationNotLive);
    if (!instance_generation.mTextureUploadTransferGeneration)
        return textureUploadTransferOperationFailure(VulkanTextureUploadTransferParentOperationCode::TextureUploadTransferNotLive);
    if (request.mNativeWindowGeneration != instance_generation.mNativeWindowGeneration ||
        request.mNativeWindowGeneration != instance_generation.surfaceNativeWindowGeneration())
        return textureUploadTransferOperationFailure(VulkanTextureUploadTransferParentOperationCode::NativeWindowGenerationMismatch);
    if (auto error = textureUploadTransferOperationFreshness(request, instance_generation))
        return *error;

    const VulkanTextureUploadSourceDescription      source_description      = request.mSourceDescription;
    const VulkanTextureUploadDestinationDescription destination_description = request.mDestinationDescription;
    const auto*                                     selection               = instance_generation.mPresentationDeviceGeneration.get();
    const auto*                                     logical                 = instance_generation.mLogicalDeviceGeneration.get();
    const auto*                                     source                  = instance_generation.mTextureUploadSourceGeneration.get();
    auto*                                           destination             = instance_generation.mTextureUploadDestinationGeneration.get();
    auto*                                           transfer                = instance_generation.mTextureUploadTransferGeneration.get();
    const auto*                                     global_dispatch         = &*instance_generation.mGlobalDispatch;
    const VkInstance                                instance                = instance_generation.mInstance;
    const auto*                                     surface_generation      = instance_generation.mSurfaceGeneration.get();
    const VkSurfaceKHR                              surface                 = instance_generation.surface();
    const auto                                      source_epoch            = instance_generation.mTextureUploadSourceEpoch;
    const auto                                      destination_epoch       = instance_generation.mTextureUploadDestinationEpoch;
    const auto                                      transfer_epoch          = instance_generation.mTextureUploadTransferEpoch;
    const VkPhysicalDevice                          physical_device         = selection->physicalDevice();
    const VkDevice                                  device                  = logical->device();
    const VkQueue                                   queue                   = logical->queue();
    const VkBuffer                                  source_buffer           = source->buffer();
    const VkImage                                   destination_image       = destination->image();
    const VkCommandPool                             command_pool            = transfer->commandPool();
    const VkCommandBuffer                           command_buffer          = transfer->commandBuffer();
    const VkFence                                   fence                   = transfer->fence();
    const auto                                      exact                   = [&]() noexcept
    {
        const auto disposition = transfer->disposition();
        return instance_generation.mTextureUploadSourceEpoch == source_epoch &&
               instance_generation.mTextureUploadDestinationEpoch == destination_epoch &&
               instance_generation.mTextureUploadTransferEpoch == transfer_epoch && instance_generation.mGlobalDispatch &&
               &*instance_generation.mGlobalDispatch == global_dispatch && instance_generation.mInstance == instance &&
               instance_generation.mSurfaceGeneration.get() == surface_generation && instance_generation.surface() == surface &&
               selection->physicalDevice() == physical_device && logical->device() == device && logical->queue() == queue &&
               selection->selectedFor(instance, surface) && logical->createdFor(*selection) &&
               instance_generation.mPresentationDeviceGeneration.get() == selection &&
               instance_generation.mLogicalDeviceGeneration.get() == logical &&
               instance_generation.mTextureUploadSourceGeneration.get() == source &&
               instance_generation.mTextureUploadDestinationGeneration.get() == destination &&
               instance_generation.mTextureUploadTransferGeneration.get() == transfer && source->matchesDescription(source_description) &&
               destination->matchesDescription(destination_description) && transfer->matchesSourceDescription(source_description) &&
               transfer->matchesDestinationDescription(destination_description) && transfer->createdFor(*selection, *logical) &&
               source->buffer() == source_buffer && transfer->sourceBuffer() == source_buffer &&
               destination->image() == destination_image && transfer->destinationImage() == destination_image &&
               transfer->queue() == queue && transfer->commandPool() == command_pool && command_pool != VK_NULL_HANDLE &&
               transfer->commandBuffer() == command_buffer && command_buffer != VK_NULL_HANDLE && transfer->fence() == fence &&
               fence != VK_NULL_HANDLE &&
               transfer->retainsTextureUploadResources(*source, *destination) ==
                   (disposition == VulkanTextureUploadTransferDisposition::Ready ||
                    disposition == VulkanTextureUploadTransferDisposition::Pending) &&
               (disposition == VulkanTextureUploadTransferDisposition::Complete
                    ? destination->isResident() && destination->residentRevision() == source->expectedRevision() &&
                          destination->residentContentIdentity() == source->contentIdentity() &&
                          destination->currentState() == LLRenderContract::ImageState::ShaderRead
                    : !destination->isResident()) &&
               request.mSourceDescription == source_description && request.mDestinationDescription == destination_description;
    };
    if (!exact())
        return textureUploadTransferOperationFailure(VulkanTextureUploadTransferParentOperationCode::TextureUploadTransferNotLive);
    auto result = operation == TextureUploadTransferParentOperation::Execute ? transfer->execute(request.mTimeoutNs)
                                                                             : transfer->retryCompletion(request.mTimeoutNs);
    if (const auto* error = std::get_if<VulkanTextureUploadTransferOperationError>(&result))
        return textureUploadTransferOperationFailure(VulkanTextureUploadTransferParentOperationCode::OperationFailure, *error);
    return std::get<VulkanTextureUploadTransferDisposition>(result);
}

VulkanUploadTransferAcquireResult VulkanInstanceGenerationFactory::acquireUploadTransfer(
    VulkanInstanceGeneration&                  instance_generation,
    const VulkanUploadTransferRequest&         request,
    VulkanInstanceDetail::AllocationCheckpoint allocation_checkpoint) noexcept
{
    if (!request.mInstanceOwnerCheck.mIsCurrent)
    {
        return uploadTransferFailure(VulkanUploadTransferAcquireCode::InvalidInstanceOwnerCheck);
    }
    if (!request.mWindowGenerationCheck.mIsCurrent)
    {
        return uploadTransferFailure(VulkanUploadTransferAcquireCode::InvalidWindowGenerationCheck);
    }
    if (request.mNativeWindowGeneration == 0)
    {
        return uploadTransferFailure(VulkanUploadTransferAcquireCode::InvalidNativeWindowGeneration);
    }
    if (instance_generation.mInstance == VK_NULL_HANDLE || !instance_generation.mDestroyInstance || !instance_generation.mGlobalDispatch ||
        !instance_generation.mGlobalDispatch->getInstanceProcAddr() || instance_generation.mNativeWindowGeneration == 0)
    {
        return uploadTransferFailure(VulkanUploadTransferAcquireCode::InstanceNotLive);
    }
    if (!instance_generation.mSurfaceGeneration || instance_generation.surface() == VK_NULL_HANDLE)
    {
        return uploadTransferFailure(VulkanUploadTransferAcquireCode::SurfaceNotLive);
    }
    if (!instance_generation.mPresentationDeviceGeneration || instance_generation.physicalDevice() == VK_NULL_HANDLE)
    {
        return uploadTransferFailure(VulkanUploadTransferAcquireCode::PresentationDeviceNotLive);
    }
    if (!instance_generation.mLogicalDeviceGeneration || instance_generation.logicalDevice() == VK_NULL_HANDLE ||
        instance_generation.presentationQueue() == VK_NULL_HANDLE)
    {
        return uploadTransferFailure(VulkanUploadTransferAcquireCode::LogicalDeviceNotLive);
    }
    if (!instance_generation.mUploadSourceGeneration || instance_generation.uploadSourceBuffer() == VK_NULL_HANDLE ||
        instance_generation.uploadSourceMemory() == VK_NULL_HANDLE)
    {
        return uploadTransferFailure(VulkanUploadTransferAcquireCode::UploadSourceNotLive);
    }
    if (!instance_generation.mUploadDestinationGeneration || instance_generation.uploadDestinationBuffer() == VK_NULL_HANDLE ||
        instance_generation.uploadDestinationMemory() == VK_NULL_HANDLE)
    {
        return uploadTransferFailure(VulkanUploadTransferAcquireCode::UploadDestinationNotLive);
    }
    if (instance_generation.mUploadTransferGeneration)
    {
        return uploadTransferFailure(VulkanUploadTransferAcquireCode::UploadTransferAlreadyOwned);
    }
    if (request.mNativeWindowGeneration != instance_generation.mNativeWindowGeneration ||
        request.mNativeWindowGeneration != instance_generation.surfaceNativeWindowGeneration())
    {
        return uploadTransferFailure(VulkanUploadTransferAcquireCode::NativeWindowGenerationMismatch);
    }

    const VulkanUploadSourceDescription   description               = request.mDescription;
    const std::uint64_t                   acquisition_epoch         = instance_generation.mOwnershipTransitionEpoch;
    const std::uint64_t                   source_epoch              = instance_generation.mUploadSourceEpoch;
    const std::uint64_t                   destination_epoch         = instance_generation.mUploadDestinationEpoch;
    const std::uint64_t                   transfer_epoch            = instance_generation.mUploadTransferEpoch;
    const VulkanGlobalDispatchGeneration* global_dispatch           = &*instance_generation.mGlobalDispatch;
    const PFN_vkGetInstanceProcAddr       get_instance_proc_addr    = global_dispatch->getInstanceProcAddr();
    const VkInstance                      instance                  = instance_generation.mInstance;
    const auto*                           surface_generation        = instance_generation.mSurfaceGeneration.get();
    const std::uint64_t                   surface_window_generation = instance_generation.surfaceNativeWindowGeneration();
    const VkSurfaceKHR                    surface                   = instance_generation.surface();
    const VulkanPhysicalDeviceGeneration* selection                 = instance_generation.mPresentationDeviceGeneration.get();
    const VulkanLogicalDeviceGeneration*  logical_device            = instance_generation.mLogicalDeviceGeneration.get();
    const VulkanUploadSourceGeneration*   source                    = instance_generation.mUploadSourceGeneration.get();
    VulkanUploadDestinationGeneration*    destination               = instance_generation.mUploadDestinationGeneration.get();
    const VkPhysicalDevice                physical_device           = selection->physicalDevice();
    const std::uint32_t                   physical_index            = selection->physicalDeviceIndex();
    const VkDevice                        device                    = logical_device->device();
    const VkQueue                         queue                     = logical_device->queue();
    const std::uint32_t                   queue_family              = logical_device->queueFamilyIndex();
    const std::uint32_t                   queue_index               = logical_device->queueIndex();
    const LLRenderContract::BufferHandle  resource_handle           = source->resourceHandle();
    const std::uint64_t                   content_identity          = source->contentIdentity();
    const VkBuffer                        source_buffer             = source->buffer();
    const VkDeviceMemory                  source_memory             = source->memory();
    const VkDeviceSize                    source_byte_count         = source->byteCount();
    const VkBuffer                        destination_buffer        = destination->buffer();
    const VkDeviceMemory                  destination_memory        = destination->memory();
    const VkDeviceSize                    destination_byte_count    = destination->byteCount();
    const std::uint64_t                   resident_identity         = destination->residentContentIdentity();

    const auto exact_upload_chain = [&]() noexcept
    {
        return instance_generation.mOwnershipTransitionEpoch == acquisition_epoch &&
               instance_generation.mUploadSourceEpoch == source_epoch && instance_generation.mUploadDestinationEpoch == destination_epoch &&
               instance_generation.mUploadTransferEpoch == transfer_epoch && instance_generation.mGlobalDispatch &&
               &*instance_generation.mGlobalDispatch == global_dispatch &&
               instance_generation.mGlobalDispatch->getInstanceProcAddr() == get_instance_proc_addr &&
               instance_generation.mInstance == instance && instance_generation.mDestroyInstance &&
               instance_generation.mNativeWindowGeneration == request.mNativeWindowGeneration &&
               instance_generation.mSurfaceGeneration.get() == surface_generation &&
               instance_generation.surfaceNativeWindowGeneration() == surface_window_generation &&
               surface_window_generation == request.mNativeWindowGeneration && instance_generation.surface() == surface &&
               instance_generation.mPresentationDeviceGeneration.get() == selection &&
               selection->getInstanceProcAddr() == get_instance_proc_addr && selection->instance() == instance &&
               selection->surface() == surface && selection->physicalDevice() == physical_device &&
               selection->physicalDeviceIndex() == physical_index && selection->selectedFor(instance, surface) &&
               instance_generation.mLogicalDeviceGeneration.get() == logical_device &&
               logical_device->getInstanceProcAddr() == get_instance_proc_addr && logical_device->instance() == instance &&
               logical_device->surface() == surface && logical_device->physicalDevice() == physical_device &&
               logical_device->physicalDeviceIndex() == physical_index && logical_device->device() == device &&
               logical_device->queue() == queue && logical_device->queueFamilyIndex() == queue_family &&
               logical_device->queueIndex() == queue_index && logical_device->createdFor(*selection) &&
               instance_generation.mUploadSourceGeneration.get() == source && source && source->resourceHandle() == resource_handle &&
               resource_handle == description.mHandle && source->contentIdentity() == content_identity && content_identity != 0 &&
               source->buffer() == source_buffer && source_buffer != VK_NULL_HANDLE && source->memory() == source_memory &&
               source_memory != VK_NULL_HANDLE && source->byteCount() == source_byte_count &&
               source_byte_count == VULKAN_UPLOAD_SOURCE_BYTE_COUNT && source->createdFor(*selection, *logical_device) &&
               source->matchesDescription(description) && instance_generation.mUploadDestinationGeneration.get() == destination &&
               destination && destination->resourceHandle() == resource_handle &&
               destination->expectedContentIdentity() == content_identity && destination->buffer() == destination_buffer &&
               destination_buffer != VK_NULL_HANDLE && destination_buffer != source_buffer && destination->memory() == destination_memory &&
               destination_memory != VK_NULL_HANDLE && destination->byteCount() == destination_byte_count &&
               destination_byte_count == source_byte_count && destination->residentContentIdentity() == resident_identity &&
               !destination->isResident() && destination->createdFor(*selection, *logical_device) &&
               destination->matchesDescription(description) && destination->matchesUploadSource(*source) &&
               request.mDescription == description;
    };
    const auto freshness_check = [&]() noexcept -> VulkanUploadTransferAcquireResult
    {
        if (VulkanUploadTransferAcquireResult freshness =
                uploadTransferFreshness(request, instance_generation, &instance_generation.mOwnershipTransitionEpoch, acquisition_epoch))
        {
            return freshness;
        }
        if (!exact_upload_chain())
        {
            return uploadTransferFailure(VulkanUploadTransferAcquireCode::UploadDestinationNotLive);
        }
        return std::nullopt;
    };

    if (VulkanUploadTransferAcquireResult freshness = freshness_check())
    {
        return freshness;
    }
    VulkanInstanceGeneration::NativeAcquisitionGuard native_guard(instance_generation);

    VulkanUploadTransferResolutionResult resolution_result =
        resolveVulkanUploadTransferGeneration(*selection, *logical_device, description, *source, *destination);
    if (const auto* error = std::get_if<VulkanUploadTransferResolutionError>(&resolution_result))
    {
        return uploadTransferFailure(VulkanUploadTransferAcquireCode::ResolutionFailure, *error);
    }

    try
    {
        if (allocation_checkpoint)
        {
            allocation_checkpoint();
            if (instance_generation.mUploadTransferGeneration)
            {
                return uploadTransferFailure(VulkanUploadTransferAcquireCode::UploadTransferAlreadyOwned);
            }
            if (!exact_upload_chain())
            {
                return uploadTransferFailure(VulkanUploadTransferAcquireCode::UploadDestinationNotLive);
            }
        }
        auto pending =
            std::make_unique<VulkanUploadTransferGeneration>(std::move(std::get<VulkanUploadTransferGeneration>(resolution_result)));

        if (VulkanUploadTransferAcquireResult freshness = freshness_check())
        {
            return freshness;
        }
        if (instance_generation.mUploadTransferGeneration)
        {
            return uploadTransferFailure(VulkanUploadTransferAcquireCode::UploadTransferAlreadyOwned);
        }
        if (!exact_upload_chain() || !pending->createdFor(*selection, *logical_device) || !pending->matchesDescription(description) ||
            !pending->retainsUploadResources(*source, *destination) || pending->disposition() != VulkanUploadTransferDisposition::Ready)
        {
            return uploadTransferFailure(VulkanUploadTransferAcquireCode::UploadDestinationNotLive);
        }

        instance_generation.mUploadTransferGeneration = std::move(pending);
        instance_generation.noteUploadTransferTransition();
        return std::nullopt;
    }
    catch (const std::bad_alloc&)
    {
        return uploadTransferFailure(VulkanUploadTransferAcquireCode::AllocationFailure);
    }
}

VulkanUploadTransferParentOperationResult VulkanInstanceGenerationFactory::operateUploadTransfer(
    VulkanInstanceGeneration&                   instance_generation,
    const VulkanUploadTransferOperationRequest& request,
    UploadTransferParentOperation               operation) noexcept
{
    if (!request.mInstanceOwnerCheck.mIsCurrent)
    {
        return uploadTransferOperationFailure(VulkanUploadTransferParentOperationCode::InvalidInstanceOwnerCheck);
    }
    if (!request.mWindowGenerationCheck.mIsCurrent)
    {
        return uploadTransferOperationFailure(VulkanUploadTransferParentOperationCode::InvalidWindowGenerationCheck);
    }
    if (request.mNativeWindowGeneration == 0)
    {
        return uploadTransferOperationFailure(VulkanUploadTransferParentOperationCode::InvalidNativeWindowGeneration);
    }
    if (instance_generation.mNativeAcquisitionDepth != 0)
    {
        return uploadTransferOperationFailure(VulkanUploadTransferParentOperationCode::NativeOperationInProgress);
    }
    if (instance_generation.mInstance == VK_NULL_HANDLE || !instance_generation.mDestroyInstance || !instance_generation.mGlobalDispatch ||
        !instance_generation.mGlobalDispatch->getInstanceProcAddr() || instance_generation.mNativeWindowGeneration == 0)
    {
        return uploadTransferOperationFailure(VulkanUploadTransferParentOperationCode::InstanceNotLive);
    }
    if (!instance_generation.mSurfaceGeneration || instance_generation.surface() == VK_NULL_HANDLE)
    {
        return uploadTransferOperationFailure(VulkanUploadTransferParentOperationCode::SurfaceNotLive);
    }
    if (!instance_generation.mPresentationDeviceGeneration || instance_generation.physicalDevice() == VK_NULL_HANDLE)
    {
        return uploadTransferOperationFailure(VulkanUploadTransferParentOperationCode::PresentationDeviceNotLive);
    }
    if (!instance_generation.mLogicalDeviceGeneration || instance_generation.logicalDevice() == VK_NULL_HANDLE ||
        instance_generation.presentationQueue() == VK_NULL_HANDLE)
    {
        return uploadTransferOperationFailure(VulkanUploadTransferParentOperationCode::LogicalDeviceNotLive);
    }
    if (!instance_generation.mUploadSourceGeneration)
    {
        return uploadTransferOperationFailure(VulkanUploadTransferParentOperationCode::UploadSourceNotLive);
    }
    if (!instance_generation.mUploadDestinationGeneration)
    {
        return uploadTransferOperationFailure(VulkanUploadTransferParentOperationCode::UploadDestinationNotLive);
    }
    if (!instance_generation.mUploadTransferGeneration)
    {
        return uploadTransferOperationFailure(VulkanUploadTransferParentOperationCode::UploadTransferNotLive);
    }
    if (request.mNativeWindowGeneration != instance_generation.mNativeWindowGeneration ||
        request.mNativeWindowGeneration != instance_generation.surfaceNativeWindowGeneration())
    {
        return uploadTransferOperationFailure(VulkanUploadTransferParentOperationCode::NativeWindowGenerationMismatch);
    }

    const VulkanUploadSourceDescription   description               = request.mDescription;
    const std::uint64_t                   ownership_epoch           = instance_generation.mOwnershipTransitionEpoch;
    const std::uint64_t                   source_epoch              = instance_generation.mUploadSourceEpoch;
    const std::uint64_t                   destination_epoch         = instance_generation.mUploadDestinationEpoch;
    const std::uint64_t                   transfer_epoch            = instance_generation.mUploadTransferEpoch;
    const VulkanGlobalDispatchGeneration* global_dispatch           = &*instance_generation.mGlobalDispatch;
    const PFN_vkGetInstanceProcAddr       get_instance_proc_addr    = global_dispatch->getInstanceProcAddr();
    const VkInstance                      instance                  = instance_generation.mInstance;
    const auto*                           surface_generation        = instance_generation.mSurfaceGeneration.get();
    const std::uint64_t                   surface_window_generation = instance_generation.surfaceNativeWindowGeneration();
    const VkSurfaceKHR                    surface                   = instance_generation.surface();
    const VulkanPhysicalDeviceGeneration* selection                 = instance_generation.mPresentationDeviceGeneration.get();
    const VulkanLogicalDeviceGeneration*  logical_device            = instance_generation.mLogicalDeviceGeneration.get();
    const VulkanUploadSourceGeneration*   source                    = instance_generation.mUploadSourceGeneration.get();
    VulkanUploadDestinationGeneration*    destination               = instance_generation.mUploadDestinationGeneration.get();
    VulkanUploadTransferGeneration*       transfer                  = instance_generation.mUploadTransferGeneration.get();
    const VkPhysicalDevice                physical_device           = selection->physicalDevice();
    const std::uint32_t                   physical_index            = selection->physicalDeviceIndex();
    const VkDevice                        device                    = logical_device->device();
    const VkQueue                         queue                     = logical_device->queue();
    const std::uint32_t                   queue_family              = logical_device->queueFamilyIndex();
    const std::uint32_t                   queue_index               = logical_device->queueIndex();
    const LLRenderContract::BufferHandle  resource_handle           = source->resourceHandle();
    const std::uint64_t                   content_identity          = source->contentIdentity();
    const VkBuffer                        source_buffer             = source->buffer();
    const VkDeviceMemory                  source_memory             = source->memory();
    const VkDeviceSize                    source_byte_count         = source->byteCount();
    const VkBuffer                        destination_buffer        = destination->buffer();
    const VkDeviceMemory                  destination_memory        = destination->memory();
    const VkDeviceSize                    destination_byte_count    = destination->byteCount();
    const std::uint64_t                   resident_identity         = destination->residentContentIdentity();
    const VkCommandPool                   command_pool              = transfer->commandPool();
    const VkCommandBuffer                 command_buffer            = transfer->commandBuffer();
    const VkFence                         fence                     = transfer->fence();
    const VulkanUploadTransferDisposition disposition               = transfer->disposition();
    const std::uint32_t                   submission_count          = transfer->submissionAttemptCount();
    const std::uint32_t                   completion_wait_count     = transfer->completionWaitCount();

    const auto exact_state_error = [&]() noexcept -> std::optional<VulkanUploadTransferParentOperationError>
    {
        if (instance_generation.mOwnershipTransitionEpoch != ownership_epoch || !instance_generation.mGlobalDispatch ||
            &*instance_generation.mGlobalDispatch != global_dispatch ||
            instance_generation.mGlobalDispatch->getInstanceProcAddr() != get_instance_proc_addr ||
            instance_generation.mInstance != instance || !instance_generation.mDestroyInstance ||
            instance_generation.mNativeWindowGeneration != request.mNativeWindowGeneration)
        {
            return uploadTransferOperationFailure(VulkanUploadTransferParentOperationCode::InstanceNotLive);
        }
        if (instance_generation.mSurfaceGeneration.get() != surface_generation ||
            instance_generation.surfaceNativeWindowGeneration() != surface_window_generation ||
            surface_window_generation != request.mNativeWindowGeneration || instance_generation.surface() != surface)
        {
            return uploadTransferOperationFailure(VulkanUploadTransferParentOperationCode::SurfaceNotLive);
        }
        if (instance_generation.mPresentationDeviceGeneration.get() != selection ||
            selection->getInstanceProcAddr() != get_instance_proc_addr || selection->instance() != instance ||
            selection->surface() != surface || selection->physicalDevice() != physical_device ||
            selection->physicalDeviceIndex() != physical_index || !selection->selectedFor(instance, surface))
        {
            return uploadTransferOperationFailure(VulkanUploadTransferParentOperationCode::PresentationDeviceNotLive);
        }
        if (instance_generation.mLogicalDeviceGeneration.get() != logical_device ||
            logical_device->getInstanceProcAddr() != get_instance_proc_addr || logical_device->instance() != instance ||
            logical_device->surface() != surface || logical_device->physicalDevice() != physical_device ||
            logical_device->physicalDeviceIndex() != physical_index || logical_device->device() != device ||
            logical_device->queue() != queue || logical_device->queueFamilyIndex() != queue_family ||
            logical_device->queueIndex() != queue_index || !logical_device->createdFor(*selection))
        {
            return uploadTransferOperationFailure(VulkanUploadTransferParentOperationCode::LogicalDeviceNotLive);
        }
        if (instance_generation.mUploadSourceEpoch != source_epoch || instance_generation.mUploadSourceGeneration.get() != source ||
            !source || source->resourceHandle() != resource_handle || resource_handle != description.mHandle ||
            source->contentIdentity() != content_identity || content_identity == 0 || source->buffer() != source_buffer ||
            source_buffer == VK_NULL_HANDLE || source->memory() != source_memory || source_memory == VK_NULL_HANDLE ||
            source->byteCount() != source_byte_count || source_byte_count != VULKAN_UPLOAD_SOURCE_BYTE_COUNT ||
            !source->createdFor(*selection, *logical_device) || !source->matchesDescription(description))
        {
            return uploadTransferOperationFailure(VulkanUploadTransferParentOperationCode::UploadSourceNotLive);
        }
        if (instance_generation.mUploadDestinationEpoch != destination_epoch ||
            instance_generation.mUploadDestinationGeneration.get() != destination || !destination ||
            destination->resourceHandle() != resource_handle || destination->expectedContentIdentity() != content_identity ||
            destination->buffer() != destination_buffer || destination_buffer == VK_NULL_HANDLE || destination_buffer == source_buffer ||
            destination->memory() != destination_memory || destination_memory == VK_NULL_HANDLE ||
            destination->byteCount() != destination_byte_count || destination_byte_count != source_byte_count ||
            destination->residentContentIdentity() != resident_identity || !destination->isDeviceLocal() ||
            !destination->createdFor(*selection, *logical_device) || !destination->matchesDescription(description) ||
            !destination->matchesUploadSource(*source))
        {
            return uploadTransferOperationFailure(VulkanUploadTransferParentOperationCode::UploadDestinationNotLive);
        }
        if (instance_generation.mUploadTransferEpoch != transfer_epoch || instance_generation.mUploadTransferGeneration.get() != transfer ||
            !transfer || transfer->resourceHandle() != resource_handle || transfer->contentIdentity() != content_identity ||
            transfer->sourceBuffer() != source_buffer || transfer->destinationBuffer() != destination_buffer ||
            transfer->queue() != queue || transfer->queueFamilyIndex() != queue_family || transfer->queueIndex() != queue_index ||
            transfer->commandPool() != command_pool || command_pool == VK_NULL_HANDLE || transfer->commandBuffer() != command_buffer ||
            command_buffer == VK_NULL_HANDLE || transfer->fence() != fence || fence == VK_NULL_HANDLE ||
            transfer->disposition() != disposition || transfer->submissionAttemptCount() != submission_count ||
            transfer->completionWaitCount() != completion_wait_count || !transfer->createdFor(*selection, *logical_device) ||
            !transfer->matchesDescription(description) ||
            transfer->retainsUploadResources(*source, *destination) !=
                (disposition == VulkanUploadTransferDisposition::Ready || disposition == VulkanUploadTransferDisposition::Pending) ||
            (disposition == VulkanUploadTransferDisposition::Complete &&
             (!destination->isResident() || resident_identity != content_identity)) ||
            (disposition != VulkanUploadTransferDisposition::Complete && destination->isResident()) || request.mDescription != description)
        {
            return uploadTransferOperationFailure(VulkanUploadTransferParentOperationCode::UploadTransferNotLive);
        }
        return std::nullopt;
    };

    if (auto error = uploadTransferOperationFreshness(request, instance_generation))
    {
        return *error;
    }
    if (auto error = exact_state_error())
    {
        return *error;
    }

    VulkanInstanceGeneration::NativeAcquisitionGuard native_guard(instance_generation);
    if (auto error = exact_state_error())
    {
        return *error;
    }

    const VulkanUploadTransferOperationResult result =
        operation == UploadTransferParentOperation::Execute ? transfer->execute() : transfer->retryCompletion();
    if (const auto* error = std::get_if<VulkanUploadTransferOperationError>(&result))
    {
        return uploadTransferOperationFailure(VulkanUploadTransferParentOperationCode::OperationFailure, *error);
    }
    return std::get<VulkanUploadTransferDisposition>(result);
}

VulkanSwapchainConfigurationAcquireResult VulkanInstanceGenerationFactory::acquireSwapchainConfiguration(
    VulkanInstanceGeneration&                  instance_generation,
    const VulkanSwapchainConfigurationRequest& request,
    VulkanInstanceDetail::AllocationCheckpoint allocation_checkpoint) noexcept
{
    if (!request.mInstanceOwnerCheck.mIsCurrent)
    {
        return swapchainConfigurationFailure(VulkanSwapchainConfigurationAcquireCode::InvalidInstanceOwnerCheck);
    }
    if (!request.mWindowGenerationCheck.mIsCurrent)
    {
        return swapchainConfigurationFailure(VulkanSwapchainConfigurationAcquireCode::InvalidWindowGenerationCheck);
    }
    if (request.mNativeWindowGeneration == 0)
    {
        return swapchainConfigurationFailure(VulkanSwapchainConfigurationAcquireCode::InvalidNativeWindowGeneration);
    }
    if (request.mDrawableExtent.width == 0 || request.mDrawableExtent.height == 0)
    {
        return swapchainConfigurationFailure(VulkanSwapchainConfigurationAcquireCode::InvalidDrawableExtent);
    }
    if (instance_generation.mTextureUploadDestinationTeardownDepth != 0 || instance_generation.mTextureUploadSourceTeardownDepth != 0 ||
        instance_generation.mTextureUploadTransferTeardownDepth != 0 || instance_generation.mTextureUploadSampleBindingTeardownDepth != 0 ||
        instance_generation.mTextureUploadSamplePipelineTeardownDepth != 0)
    {
        return swapchainConfigurationFailure(VulkanSwapchainConfigurationAcquireCode::NativeTeardownInProgress);
    }
    if (instance_generation.mInstance == VK_NULL_HANDLE || !instance_generation.mDestroyInstance || !instance_generation.mGlobalDispatch ||
        instance_generation.mNativeWindowGeneration == 0)
    {
        return swapchainConfigurationFailure(VulkanSwapchainConfigurationAcquireCode::InstanceNotLive);
    }
    if (!instance_generation.mSurfaceGeneration || instance_generation.surface() == VK_NULL_HANDLE)
    {
        return swapchainConfigurationFailure(VulkanSwapchainConfigurationAcquireCode::SurfaceNotLive);
    }
    if (!instance_generation.mPresentationDeviceGeneration || instance_generation.physicalDevice() == VK_NULL_HANDLE)
    {
        return swapchainConfigurationFailure(VulkanSwapchainConfigurationAcquireCode::PresentationDeviceNotLive);
    }
    if (!instance_generation.mLogicalDeviceGeneration || instance_generation.logicalDevice() == VK_NULL_HANDLE ||
        instance_generation.presentationQueue() == VK_NULL_HANDLE)
    {
        return swapchainConfigurationFailure(VulkanSwapchainConfigurationAcquireCode::LogicalDeviceNotLive);
    }
    if (instance_generation.mSwapchainConfigurationGeneration)
    {
        return swapchainConfigurationFailure(VulkanSwapchainConfigurationAcquireCode::SwapchainConfigurationAlreadyOwned);
    }
    if (request.mNativeWindowGeneration != instance_generation.mNativeWindowGeneration ||
        request.mNativeWindowGeneration != instance_generation.surfaceNativeWindowGeneration())
    {
        return swapchainConfigurationFailure(VulkanSwapchainConfigurationAcquireCode::NativeWindowGenerationMismatch);
    }
    const std::uint64_t acquisition_epoch = instance_generation.mOwnershipTransitionEpoch;
    const auto freshness_check = [&]() noexcept
    {
        return swapchainConfigurationFreshness(request,
                                               instance_generation,
                                               &instance_generation.mOwnershipTransitionEpoch,
                                               acquisition_epoch);
    };
    if (VulkanSwapchainConfigurationAcquireResult freshness = freshness_check())
    {
        return freshness;
    }
    if (instance_generation.mSwapchainConfigurationGeneration)
    {
        return swapchainConfigurationFailure(
            VulkanSwapchainConfigurationAcquireCode::SwapchainConfigurationAlreadyOwned);
    }
    VulkanInstanceGeneration::NativeAcquisitionGuard native_guard(instance_generation);

    const VkInstance                      instance        = instance_generation.mInstance;
    const VkSurfaceKHR                    surface         = instance_generation.surface();
    const VulkanPhysicalDeviceGeneration* selection       = instance_generation.mPresentationDeviceGeneration.get();
    const VulkanLogicalDeviceGeneration*  logical_device  = instance_generation.mLogicalDeviceGeneration.get();
    const VkPhysicalDevice                physical_device = selection->physicalDevice();
    const VkDevice                        device          = logical_device->device();
    const std::uint32_t                   queue_family    = selection->queueFamilyIndex();
    const VkExtent2D                      drawable_extent = request.mDrawableExtent;

    VulkanSwapchainConfigurationResolutionResult resolution_result =
        resolveVulkanSwapchainConfigurationGeneration(*selection, *logical_device, drawable_extent);
    if (const auto* error = std::get_if<VulkanSwapchainConfigurationResolutionError>(&resolution_result))
    {
        return swapchainConfigurationFailure(VulkanSwapchainConfigurationAcquireCode::ResolutionFailure, *error);
    }

    try
    {
        if (allocation_checkpoint)
        {
            allocation_checkpoint();
            if (instance_generation.mSwapchainConfigurationGeneration)
            {
                return swapchainConfigurationFailure(
                    VulkanSwapchainConfigurationAcquireCode::SwapchainConfigurationAlreadyOwned);
            }
            if (instance_generation.mOwnershipTransitionEpoch != acquisition_epoch)
            {
                return swapchainConfigurationFailure(VulkanSwapchainConfigurationAcquireCode::LogicalDeviceNotLive);
            }
        }
        auto pending = std::make_unique<VulkanSwapchainConfigurationGeneration>(
            std::move(std::get<VulkanSwapchainConfigurationGeneration>(resolution_result)));

        if (VulkanSwapchainConfigurationAcquireResult freshness = freshness_check())
        {
            return freshness;
        }
        if (instance_generation.mSwapchainConfigurationGeneration)
        {
            return swapchainConfigurationFailure(
                VulkanSwapchainConfigurationAcquireCode::SwapchainConfigurationAlreadyOwned);
        }
        if (instance_generation.mInstance != instance || !instance_generation.mSurfaceGeneration ||
            instance_generation.surface() != surface || instance_generation.mPresentationDeviceGeneration.get() != selection ||
            instance_generation.mLogicalDeviceGeneration.get() != logical_device ||
            instance_generation.physicalDevice() != physical_device || instance_generation.logicalDevice() != device ||
            instance_generation.presentationQueueFamilyIndex() != queue_family ||
            !pending->createdFor(*selection, *logical_device, drawable_extent))
        {
            return swapchainConfigurationFailure(VulkanSwapchainConfigurationAcquireCode::LogicalDeviceNotLive);
        }

        instance_generation.mSwapchainConfigurationGeneration = std::move(pending);
        instance_generation.noteOwnershipTransition();
        return std::nullopt;
    }
    catch (const std::bad_alloc&)
    {
        return swapchainConfigurationFailure(VulkanSwapchainConfigurationAcquireCode::AllocationFailure);
    }
}

VulkanSwapchainAcquireResult VulkanInstanceGenerationFactory::acquireSwapchain(
    VulkanInstanceGeneration&                  instance_generation,
    const VulkanSwapchainRequest&              request,
    VulkanInstanceDetail::AllocationCheckpoint allocation_checkpoint) noexcept
{
    if (!request.mInstanceOwnerCheck.mIsCurrent)
    {
        return swapchainFailure(VulkanSwapchainAcquireCode::InvalidInstanceOwnerCheck);
    }
    if (!request.mWindowGenerationCheck.mIsCurrent)
    {
        return swapchainFailure(VulkanSwapchainAcquireCode::InvalidWindowGenerationCheck);
    }
    if (request.mNativeWindowGeneration == 0)
    {
        return swapchainFailure(VulkanSwapchainAcquireCode::InvalidNativeWindowGeneration);
    }
    if (request.mDrawableExtent.width == 0 || request.mDrawableExtent.height == 0)
    {
        return swapchainFailure(VulkanSwapchainAcquireCode::InvalidDrawableExtent);
    }
    if (instance_generation.mInstance == VK_NULL_HANDLE || !instance_generation.mDestroyInstance || !instance_generation.mGlobalDispatch ||
        instance_generation.mNativeWindowGeneration == 0)
    {
        return swapchainFailure(VulkanSwapchainAcquireCode::InstanceNotLive);
    }
    if (!instance_generation.mSurfaceGeneration || instance_generation.surface() == VK_NULL_HANDLE)
    {
        return swapchainFailure(VulkanSwapchainAcquireCode::SurfaceNotLive);
    }
    if (!instance_generation.mPresentationDeviceGeneration || instance_generation.physicalDevice() == VK_NULL_HANDLE)
    {
        return swapchainFailure(VulkanSwapchainAcquireCode::PresentationDeviceNotLive);
    }
    if (!instance_generation.mLogicalDeviceGeneration || instance_generation.logicalDevice() == VK_NULL_HANDLE ||
        instance_generation.presentationQueue() == VK_NULL_HANDLE)
    {
        return swapchainFailure(VulkanSwapchainAcquireCode::LogicalDeviceNotLive);
    }
    if (!instance_generation.mSwapchainConfigurationGeneration)
    {
        return swapchainFailure(VulkanSwapchainAcquireCode::SwapchainConfigurationNotLive);
    }
    if (instance_generation.mSwapchainGeneration)
    {
        return swapchainFailure(VulkanSwapchainAcquireCode::SwapchainAlreadyOwned);
    }
    if (request.mNativeWindowGeneration != instance_generation.mNativeWindowGeneration ||
        request.mNativeWindowGeneration != instance_generation.surfaceNativeWindowGeneration())
    {
        return swapchainFailure(VulkanSwapchainAcquireCode::NativeWindowGenerationMismatch);
    }
    if (request.mDrawableExtent.width != instance_generation.swapchainDrawableExtent().width ||
        request.mDrawableExtent.height != instance_generation.swapchainDrawableExtent().height)
    {
        return swapchainFailure(VulkanSwapchainAcquireCode::DrawableExtentMismatch);
    }
    const std::uint64_t acquisition_epoch = instance_generation.mOwnershipTransitionEpoch;
    const auto freshness_check = [&]() noexcept
    {
        return swapchainFreshness(request,
                                  instance_generation,
                                  &instance_generation.mOwnershipTransitionEpoch,
                                  acquisition_epoch);
    };
    if (VulkanSwapchainAcquireResult freshness = freshness_check())
    {
        return freshness;
    }
    if (instance_generation.mSwapchainGeneration)
    {
        return swapchainFailure(VulkanSwapchainAcquireCode::SwapchainAlreadyOwned);
    }
    VulkanInstanceGeneration::NativeAcquisitionGuard native_guard(instance_generation);

    const VkInstance                              instance        = instance_generation.mInstance;
    const VkSurfaceKHR                            surface         = instance_generation.surface();
    const VulkanPhysicalDeviceGeneration*         selection       = instance_generation.mPresentationDeviceGeneration.get();
    const VulkanLogicalDeviceGeneration*          logical_device  = instance_generation.mLogicalDeviceGeneration.get();
    const VulkanSwapchainConfigurationGeneration* configuration   = instance_generation.mSwapchainConfigurationGeneration.get();
    const VkPhysicalDevice                        physical_device = selection->physicalDevice();
    const VkDevice                                device          = logical_device->device();
    const std::uint32_t                           queue_family    = selection->queueFamilyIndex();
    const VkExtent2D                              drawable_extent = request.mDrawableExtent;

    if (!configuration->createdFor(*selection, *logical_device, drawable_extent))
    {
        return swapchainFailure(VulkanSwapchainAcquireCode::SwapchainConfigurationNotLive);
    }

    VulkanSwapchainResolutionResult resolution_result = resolveVulkanSwapchainGeneration(*logical_device, *configuration);
    if (const auto* error = std::get_if<VulkanSwapchainResolutionError>(&resolution_result))
    {
        return swapchainFailure(VulkanSwapchainAcquireCode::ResolutionFailure, *error);
    }

    try
    {
        if (allocation_checkpoint)
        {
            allocation_checkpoint();
            if (instance_generation.mSwapchainGeneration)
            {
                return swapchainFailure(VulkanSwapchainAcquireCode::SwapchainAlreadyOwned);
            }
            if (instance_generation.mOwnershipTransitionEpoch != acquisition_epoch)
            {
                return swapchainFailure(VulkanSwapchainAcquireCode::SwapchainConfigurationNotLive);
            }
        }
        auto pending = std::make_unique<VulkanSwapchainGeneration>(std::move(std::get<VulkanSwapchainGeneration>(resolution_result)));

        if (VulkanSwapchainAcquireResult freshness = freshness_check())
        {
            return freshness;
        }
        if (instance_generation.mSwapchainGeneration)
        {
            return swapchainFailure(VulkanSwapchainAcquireCode::SwapchainAlreadyOwned);
        }
        if (instance_generation.mInstance != instance || !instance_generation.mSurfaceGeneration ||
            instance_generation.surface() != surface || instance_generation.mPresentationDeviceGeneration.get() != selection ||
            instance_generation.mLogicalDeviceGeneration.get() != logical_device ||
            instance_generation.mSwapchainConfigurationGeneration.get() != configuration ||
            instance_generation.physicalDevice() != physical_device || instance_generation.logicalDevice() != device ||
            instance_generation.presentationQueueFamilyIndex() != queue_family ||
            !configuration->createdFor(*selection, *logical_device, drawable_extent) ||
            !pending->createdFor(*logical_device, *configuration))
        {
            return swapchainFailure(VulkanSwapchainAcquireCode::SwapchainConfigurationNotLive);
        }

        instance_generation.mSwapchainGeneration = std::move(pending);
        instance_generation.noteOwnershipTransition();
        return std::nullopt;
    }
    catch (const std::bad_alloc&)
    {
        return swapchainFailure(VulkanSwapchainAcquireCode::AllocationFailure);
    }
}

VulkanSwapchainImagesAcquireResult VulkanInstanceGenerationFactory::acquireSwapchainImages(
    VulkanInstanceGeneration&                  instance_generation,
    const VulkanSwapchainImagesRequest&        request,
    VulkanInstanceDetail::AllocationCheckpoint allocation_checkpoint) noexcept
{
    if (!request.mInstanceOwnerCheck.mIsCurrent)
    {
        return swapchainImagesFailure(VulkanSwapchainImagesAcquireCode::InvalidInstanceOwnerCheck);
    }
    if (!request.mWindowGenerationCheck.mIsCurrent)
    {
        return swapchainImagesFailure(VulkanSwapchainImagesAcquireCode::InvalidWindowGenerationCheck);
    }
    if (request.mNativeWindowGeneration == 0)
    {
        return swapchainImagesFailure(VulkanSwapchainImagesAcquireCode::InvalidNativeWindowGeneration);
    }
    if (request.mDrawableExtent.width == 0 || request.mDrawableExtent.height == 0)
    {
        return swapchainImagesFailure(VulkanSwapchainImagesAcquireCode::InvalidDrawableExtent);
    }
    if (instance_generation.mInstance == VK_NULL_HANDLE || !instance_generation.mDestroyInstance || !instance_generation.mGlobalDispatch ||
        instance_generation.mNativeWindowGeneration == 0)
    {
        return swapchainImagesFailure(VulkanSwapchainImagesAcquireCode::InstanceNotLive);
    }
    if (!instance_generation.mSurfaceGeneration || instance_generation.surface() == VK_NULL_HANDLE)
    {
        return swapchainImagesFailure(VulkanSwapchainImagesAcquireCode::SurfaceNotLive);
    }
    if (!instance_generation.mPresentationDeviceGeneration || instance_generation.physicalDevice() == VK_NULL_HANDLE)
    {
        return swapchainImagesFailure(VulkanSwapchainImagesAcquireCode::PresentationDeviceNotLive);
    }
    if (!instance_generation.mLogicalDeviceGeneration || instance_generation.logicalDevice() == VK_NULL_HANDLE ||
        instance_generation.presentationQueue() == VK_NULL_HANDLE)
    {
        return swapchainImagesFailure(VulkanSwapchainImagesAcquireCode::LogicalDeviceNotLive);
    }
    if (!instance_generation.mSwapchainConfigurationGeneration)
    {
        return swapchainImagesFailure(VulkanSwapchainImagesAcquireCode::SwapchainConfigurationNotLive);
    }
    if (!instance_generation.mSwapchainGeneration || instance_generation.swapchain() == VK_NULL_HANDLE)
    {
        return swapchainImagesFailure(VulkanSwapchainImagesAcquireCode::SwapchainNotLive);
    }
    if (instance_generation.mSwapchainImagesGeneration)
    {
        return swapchainImagesFailure(VulkanSwapchainImagesAcquireCode::SwapchainImagesAlreadyOwned);
    }
    if (request.mNativeWindowGeneration != instance_generation.mNativeWindowGeneration ||
        request.mNativeWindowGeneration != instance_generation.surfaceNativeWindowGeneration())
    {
        return swapchainImagesFailure(VulkanSwapchainImagesAcquireCode::NativeWindowGenerationMismatch);
    }
    if (request.mDrawableExtent.width != instance_generation.swapchainDrawableExtent().width ||
        request.mDrawableExtent.height != instance_generation.swapchainDrawableExtent().height)
    {
        return swapchainImagesFailure(VulkanSwapchainImagesAcquireCode::DrawableExtentMismatch);
    }
    const std::uint64_t acquisition_epoch = instance_generation.mOwnershipTransitionEpoch;
    const auto freshness_check = [&]() noexcept
    {
        return swapchainImagesFreshness(request,
                                        instance_generation,
                                        &instance_generation.mOwnershipTransitionEpoch,
                                        acquisition_epoch);
    };
    if (VulkanSwapchainImagesAcquireResult freshness = freshness_check())
    {
        return freshness;
    }
    if (instance_generation.mSwapchainImagesGeneration)
    {
        return swapchainImagesFailure(VulkanSwapchainImagesAcquireCode::SwapchainImagesAlreadyOwned);
    }
    VulkanInstanceGeneration::NativeAcquisitionGuard native_guard(instance_generation);

    const VkInstance                              instance                         = instance_generation.mInstance;
    const std::uint64_t                           native_window_generation         = instance_generation.mNativeWindowGeneration;
    const auto*                                   surface_generation               = instance_generation.mSurfaceGeneration.get();
    const std::uint64_t                           surface_native_window_generation = instance_generation.surfaceNativeWindowGeneration();
    const VkSurfaceKHR                            surface                          = instance_generation.surface();
    const VulkanPhysicalDeviceGeneration*         selection            = instance_generation.mPresentationDeviceGeneration.get();
    const VulkanLogicalDeviceGeneration*          logical_device       = instance_generation.mLogicalDeviceGeneration.get();
    const VulkanSwapchainConfigurationGeneration* configuration        = instance_generation.mSwapchainConfigurationGeneration.get();
    const VulkanSwapchainGeneration*              swapchain_generation = instance_generation.mSwapchainGeneration.get();
    const VkPhysicalDevice                        physical_device      = selection->physicalDevice();
    const VkDevice                                device               = logical_device->device();
    const std::uint32_t                           queue_family         = selection->queueFamilyIndex();
    const VkExtent2D                              drawable_extent      = request.mDrawableExtent;
    const VkSwapchainKHR                          swapchain            = swapchain_generation->swapchain();

    if (!configuration->createdFor(*selection, *logical_device, drawable_extent))
    {
        return swapchainImagesFailure(VulkanSwapchainImagesAcquireCode::SwapchainConfigurationNotLive);
    }
    if (!swapchain_generation->createdFor(*logical_device, *configuration))
    {
        return swapchainImagesFailure(VulkanSwapchainImagesAcquireCode::SwapchainNotLive);
    }

    VulkanSwapchainImagesResolutionResult resolution_result =
        resolveVulkanSwapchainImagesGeneration(*logical_device, *configuration, *swapchain_generation);
    if (const auto* error = std::get_if<VulkanSwapchainImagesResolutionError>(&resolution_result))
    {
        return swapchainImagesFailure(VulkanSwapchainImagesAcquireCode::ResolutionFailure, *error);
    }

    try
    {
        if (allocation_checkpoint)
        {
            allocation_checkpoint();
            if (instance_generation.mSwapchainImagesGeneration)
            {
                return swapchainImagesFailure(VulkanSwapchainImagesAcquireCode::SwapchainImagesAlreadyOwned);
            }
            if (instance_generation.mOwnershipTransitionEpoch != acquisition_epoch)
            {
                return swapchainImagesFailure(VulkanSwapchainImagesAcquireCode::SwapchainNotLive);
            }
        }
        auto pending =
            std::make_unique<VulkanSwapchainImagesGeneration>(std::move(std::get<VulkanSwapchainImagesGeneration>(resolution_result)));

        if (VulkanSwapchainImagesAcquireResult freshness = freshness_check())
        {
            return freshness;
        }
        if (instance_generation.mSwapchainImagesGeneration)
        {
            return swapchainImagesFailure(VulkanSwapchainImagesAcquireCode::SwapchainImagesAlreadyOwned);
        }
        if (instance_generation.mInstance != instance || instance_generation.mNativeWindowGeneration != native_window_generation ||
            instance_generation.mSurfaceGeneration.get() != surface_generation ||
            instance_generation.surfaceNativeWindowGeneration() != surface_native_window_generation ||
            instance_generation.surface() != surface || instance_generation.mPresentationDeviceGeneration.get() != selection ||
            instance_generation.mLogicalDeviceGeneration.get() != logical_device ||
            instance_generation.mSwapchainConfigurationGeneration.get() != configuration ||
            instance_generation.physicalDevice() != physical_device || instance_generation.logicalDevice() != device ||
            instance_generation.presentationQueueFamilyIndex() != queue_family ||
            !configuration->createdFor(*selection, *logical_device, drawable_extent))
        {
            return swapchainImagesFailure(VulkanSwapchainImagesAcquireCode::SwapchainConfigurationNotLive);
        }
        if (instance_generation.mSwapchainGeneration.get() != swapchain_generation || instance_generation.swapchain() != swapchain ||
            !swapchain_generation->createdFor(*logical_device, *configuration) ||
            !pending->createdFor(*logical_device, *configuration, *swapchain_generation))
        {
            return swapchainImagesFailure(VulkanSwapchainImagesAcquireCode::SwapchainNotLive);
        }

        instance_generation.mSwapchainImagesGeneration = std::move(pending);
        instance_generation.noteOwnershipTransition();
        return std::nullopt;
    }
    catch (const std::bad_alloc&)
    {
        return swapchainImagesFailure(VulkanSwapchainImagesAcquireCode::AllocationFailure);
    }
}

VulkanSwapchainPresentationTargetAcquireResult VulkanInstanceGenerationFactory::acquireSwapchainPresentationTarget(
    VulkanInstanceGeneration&                         instance_generation,
    const VulkanSwapchainPresentationTargetRequest&   request,
    VulkanInstanceDetail::AllocationCheckpoint        allocation_checkpoint) noexcept
{
    if (!request.mInstanceOwnerCheck.mIsCurrent)
    {
        return swapchainPresentationTargetFailure(
            VulkanSwapchainPresentationTargetAcquireCode::InvalidInstanceOwnerCheck);
    }
    if (!request.mWindowGenerationCheck.mIsCurrent)
    {
        return swapchainPresentationTargetFailure(
            VulkanSwapchainPresentationTargetAcquireCode::InvalidWindowGenerationCheck);
    }
    if (request.mNativeWindowGeneration == 0)
    {
        return swapchainPresentationTargetFailure(
            VulkanSwapchainPresentationTargetAcquireCode::InvalidNativeWindowGeneration);
    }
    if (request.mDrawableExtent.width == 0 || request.mDrawableExtent.height == 0)
    {
        return swapchainPresentationTargetFailure(VulkanSwapchainPresentationTargetAcquireCode::InvalidDrawableExtent);
    }
    if (instance_generation.mSwapchainPresentationTargetTeardownDepth != 0)
    {
        return swapchainPresentationTargetFailure(VulkanSwapchainPresentationTargetAcquireCode::NativeTeardownInProgress);
    }
    if (instance_generation.mInstance == VK_NULL_HANDLE || !instance_generation.mDestroyInstance ||
        !instance_generation.mGlobalDispatch || instance_generation.mNativeWindowGeneration == 0)
    {
        return swapchainPresentationTargetFailure(VulkanSwapchainPresentationTargetAcquireCode::InstanceNotLive);
    }
    if (!instance_generation.mSurfaceGeneration || instance_generation.surface() == VK_NULL_HANDLE)
    {
        return swapchainPresentationTargetFailure(VulkanSwapchainPresentationTargetAcquireCode::SurfaceNotLive);
    }
    if (!instance_generation.mPresentationDeviceGeneration || instance_generation.physicalDevice() == VK_NULL_HANDLE)
    {
        return swapchainPresentationTargetFailure(
            VulkanSwapchainPresentationTargetAcquireCode::PresentationDeviceNotLive);
    }
    if (!instance_generation.mLogicalDeviceGeneration || instance_generation.logicalDevice() == VK_NULL_HANDLE ||
        instance_generation.presentationQueue() == VK_NULL_HANDLE)
    {
        return swapchainPresentationTargetFailure(VulkanSwapchainPresentationTargetAcquireCode::LogicalDeviceNotLive);
    }
    if (!instance_generation.mSwapchainConfigurationGeneration)
    {
        return swapchainPresentationTargetFailure(
            VulkanSwapchainPresentationTargetAcquireCode::SwapchainConfigurationNotLive);
    }
    if (!instance_generation.mSwapchainGeneration || instance_generation.swapchain() == VK_NULL_HANDLE)
    {
        return swapchainPresentationTargetFailure(VulkanSwapchainPresentationTargetAcquireCode::SwapchainNotLive);
    }
    if (!instance_generation.mSwapchainImagesGeneration || instance_generation.resolvedSwapchainImageCount() == 0 ||
        instance_generation.mSwapchainImagesGeneration->imageFormat() == VK_FORMAT_UNDEFINED)
    {
        return swapchainPresentationTargetFailure(VulkanSwapchainPresentationTargetAcquireCode::SwapchainImagesNotLive);
    }
    if (instance_generation.mSwapchainPresentationTargetGeneration)
    {
        return swapchainPresentationTargetFailure(
            VulkanSwapchainPresentationTargetAcquireCode::SwapchainPresentationTargetAlreadyOwned);
    }
    if (request.mNativeWindowGeneration != instance_generation.mNativeWindowGeneration ||
        request.mNativeWindowGeneration != instance_generation.surfaceNativeWindowGeneration())
    {
        return swapchainPresentationTargetFailure(
            VulkanSwapchainPresentationTargetAcquireCode::NativeWindowGenerationMismatch);
    }
    if (request.mDrawableExtent.width != instance_generation.swapchainDrawableExtent().width ||
        request.mDrawableExtent.height != instance_generation.swapchainDrawableExtent().height)
    {
        return swapchainPresentationTargetFailure(VulkanSwapchainPresentationTargetAcquireCode::DrawableExtentMismatch);
    }

    const std::uint64_t acquisition_epoch = instance_generation.mOwnershipTransitionEpoch;
    const auto freshness_check = [&]() noexcept
    {
        return swapchainPresentationTargetFreshness(request,
                                                    instance_generation,
                                                    &instance_generation.mOwnershipTransitionEpoch,
                                                    acquisition_epoch);
    };
    if (VulkanSwapchainPresentationTargetAcquireResult freshness = freshness_check())
    {
        return freshness;
    }
    if (instance_generation.mSwapchainPresentationTargetGeneration)
    {
        return swapchainPresentationTargetFailure(
            VulkanSwapchainPresentationTargetAcquireCode::SwapchainPresentationTargetAlreadyOwned);
    }
    VulkanInstanceGeneration::NativeAcquisitionGuard native_guard(instance_generation);

    const VkInstance                              instance                         = instance_generation.mInstance;
    const std::uint64_t                           native_window_generation         = instance_generation.mNativeWindowGeneration;
    const auto*                                   surface_generation               = instance_generation.mSurfaceGeneration.get();
    const std::uint64_t                           surface_native_window_generation = instance_generation.surfaceNativeWindowGeneration();
    const VkSurfaceKHR                            surface                          = instance_generation.surface();
    const VulkanPhysicalDeviceGeneration*         selection = instance_generation.mPresentationDeviceGeneration.get();
    const VulkanLogicalDeviceGeneration*          logical_device = instance_generation.mLogicalDeviceGeneration.get();
    const VulkanSwapchainConfigurationGeneration* configuration =
        instance_generation.mSwapchainConfigurationGeneration.get();
    const VulkanSwapchainGeneration*       swapchain_generation = instance_generation.mSwapchainGeneration.get();
    const VulkanSwapchainImagesGeneration* images_generation = instance_generation.mSwapchainImagesGeneration.get();
    const PFN_vkGetInstanceProcAddr        get_instance_proc_addr = logical_device->getInstanceProcAddr();
    const VkPhysicalDevice                 physical_device        = selection->physicalDevice();
    const std::uint32_t                    physical_device_index  = selection->physicalDeviceIndex();
    const VkDevice                         device                 = logical_device->device();
    const VkQueue                          queue                  = logical_device->queue();
    const std::uint32_t                    queue_family           = logical_device->queueFamilyIndex();
    const std::uint32_t                    queue_index            = logical_device->queueIndex();
    const VkExtent2D                       drawable_extent        = request.mDrawableExtent;
    const VkSwapchainKHR                   swapchain              = swapchain_generation->swapchain();
    const std::uint32_t                    image_count            = images_generation->imageCount();
    const VkFormat                         image_format           = images_generation->imageFormat();

    if (!selection->selectedFor(instance, surface))
    {
        return swapchainPresentationTargetFailure(
            VulkanSwapchainPresentationTargetAcquireCode::PresentationDeviceNotLive);
    }
    if (!logical_device->createdFor(*selection))
    {
        return swapchainPresentationTargetFailure(VulkanSwapchainPresentationTargetAcquireCode::LogicalDeviceNotLive);
    }
    if (!configuration->createdFor(*selection, *logical_device, drawable_extent))
    {
        return swapchainPresentationTargetFailure(
            VulkanSwapchainPresentationTargetAcquireCode::SwapchainConfigurationNotLive);
    }
    if (!swapchain_generation->createdFor(*logical_device, *configuration))
    {
        return swapchainPresentationTargetFailure(VulkanSwapchainPresentationTargetAcquireCode::SwapchainNotLive);
    }
    if (!images_generation->createdFor(*logical_device, *configuration, *swapchain_generation))
    {
        return swapchainPresentationTargetFailure(VulkanSwapchainPresentationTargetAcquireCode::SwapchainImagesNotLive);
    }

    VulkanSwapchainPresentationTargetResolutionResult resolution_result =
        resolveVulkanSwapchainPresentationTargetGeneration(
            *logical_device, *configuration, *swapchain_generation, *images_generation);
    if (const auto* error = std::get_if<VulkanSwapchainPresentationTargetResolutionError>(&resolution_result))
    {
        return swapchainPresentationTargetFailure(VulkanSwapchainPresentationTargetAcquireCode::ResolutionFailure, *error);
    }

    try
    {
        if (allocation_checkpoint)
        {
            allocation_checkpoint();
            if (instance_generation.mSwapchainPresentationTargetGeneration)
            {
                return swapchainPresentationTargetFailure(
                    VulkanSwapchainPresentationTargetAcquireCode::SwapchainPresentationTargetAlreadyOwned);
            }
            if (instance_generation.mOwnershipTransitionEpoch != acquisition_epoch)
            {
                return swapchainPresentationTargetFailure(
                    VulkanSwapchainPresentationTargetAcquireCode::SwapchainImagesNotLive);
            }
        }
        auto pending = std::make_unique<VulkanSwapchainPresentationTargetGeneration>(
            std::move(std::get<VulkanSwapchainPresentationTargetGeneration>(resolution_result)));

        if (VulkanSwapchainPresentationTargetAcquireResult freshness = freshness_check())
        {
            return freshness;
        }
        if (instance_generation.mSwapchainPresentationTargetGeneration)
        {
            return swapchainPresentationTargetFailure(
                VulkanSwapchainPresentationTargetAcquireCode::SwapchainPresentationTargetAlreadyOwned);
        }
        if (instance_generation.mInstance != instance || !instance_generation.mDestroyInstance ||
            !instance_generation.mGlobalDispatch ||
            instance_generation.mNativeWindowGeneration != native_window_generation)
        {
            return swapchainPresentationTargetFailure(VulkanSwapchainPresentationTargetAcquireCode::InstanceNotLive);
        }
        if (instance_generation.mSurfaceGeneration.get() != surface_generation ||
            instance_generation.surfaceNativeWindowGeneration() != surface_native_window_generation ||
            instance_generation.surface() != surface)
        {
            return swapchainPresentationTargetFailure(VulkanSwapchainPresentationTargetAcquireCode::SurfaceNotLive);
        }
        if (instance_generation.mPresentationDeviceGeneration.get() != selection ||
            selection->getInstanceProcAddr() != get_instance_proc_addr || selection->physicalDevice() != physical_device ||
            selection->physicalDeviceIndex() != physical_device_index || !selection->selectedFor(instance, surface))
        {
            return swapchainPresentationTargetFailure(
                VulkanSwapchainPresentationTargetAcquireCode::PresentationDeviceNotLive);
        }
        if (instance_generation.mLogicalDeviceGeneration.get() != logical_device ||
            logical_device->getInstanceProcAddr() != get_instance_proc_addr || logical_device->device() != device ||
            logical_device->queue() != queue || logical_device->queueFamilyIndex() != queue_family ||
            logical_device->queueIndex() != queue_index || !logical_device->createdFor(*selection))
        {
            return swapchainPresentationTargetFailure(VulkanSwapchainPresentationTargetAcquireCode::LogicalDeviceNotLive);
        }
        if (instance_generation.mSwapchainConfigurationGeneration.get() != configuration ||
            !configuration->createdFor(*selection, *logical_device, drawable_extent))
        {
            return swapchainPresentationTargetFailure(
                VulkanSwapchainPresentationTargetAcquireCode::SwapchainConfigurationNotLive);
        }
        if (instance_generation.mSwapchainGeneration.get() != swapchain_generation ||
            swapchain_generation->swapchain() != swapchain ||
            !swapchain_generation->createdFor(*logical_device, *configuration))
        {
            return swapchainPresentationTargetFailure(VulkanSwapchainPresentationTargetAcquireCode::SwapchainNotLive);
        }
        if (instance_generation.mSwapchainImagesGeneration.get() != images_generation ||
            images_generation->imageCount() != image_count || images_generation->imageFormat() != image_format ||
            !images_generation->createdFor(*logical_device, *configuration, *swapchain_generation) ||
            !pending->createdFor(*logical_device, *configuration, *swapchain_generation, *images_generation))
        {
            return swapchainPresentationTargetFailure(VulkanSwapchainPresentationTargetAcquireCode::SwapchainImagesNotLive);
        }

        instance_generation.mSwapchainPresentationTargetGeneration = std::move(pending);
        instance_generation.noteSwapchainPresentationTargetTransition();
        return std::nullopt;
    }
    catch (const std::bad_alloc&)
    {
        return swapchainPresentationTargetFailure(VulkanSwapchainPresentationTargetAcquireCode::AllocationFailure);
    }
}

VulkanSwapchainPresentationPipelineAcquireResult VulkanInstanceGenerationFactory::acquireSwapchainPresentationPipeline(
    VulkanInstanceGeneration&                         instance_generation,
    const VulkanSwapchainPresentationPipelineRequest& request,
    VulkanInstanceDetail::AllocationCheckpoint        allocation_checkpoint) noexcept
{
    if (!request.mInstanceOwnerCheck.mIsCurrent)
    {
        return swapchainPresentationPipelineFailure(
            VulkanSwapchainPresentationPipelineAcquireCode::InvalidInstanceOwnerCheck);
    }
    if (!request.mWindowGenerationCheck.mIsCurrent)
    {
        return swapchainPresentationPipelineFailure(
            VulkanSwapchainPresentationPipelineAcquireCode::InvalidWindowGenerationCheck);
    }
    if (request.mNativeWindowGeneration == 0)
    {
        return swapchainPresentationPipelineFailure(
            VulkanSwapchainPresentationPipelineAcquireCode::InvalidNativeWindowGeneration);
    }
    if (request.mDrawableExtent.width == 0 || request.mDrawableExtent.height == 0)
    {
        return swapchainPresentationPipelineFailure(
            VulkanSwapchainPresentationPipelineAcquireCode::InvalidDrawableExtent);
    }
    if (instance_generation.mSwapchainPresentationTargetTeardownDepth != 0)
    {
        return swapchainPresentationPipelineFailure(VulkanSwapchainPresentationPipelineAcquireCode::NativeTeardownInProgress);
    }
    if (instance_generation.mInstance == VK_NULL_HANDLE || !instance_generation.mDestroyInstance ||
        !instance_generation.mGlobalDispatch || instance_generation.mNativeWindowGeneration == 0)
    {
        return swapchainPresentationPipelineFailure(VulkanSwapchainPresentationPipelineAcquireCode::InstanceNotLive);
    }
    if (!instance_generation.mSurfaceGeneration || instance_generation.surface() == VK_NULL_HANDLE)
    {
        return swapchainPresentationPipelineFailure(VulkanSwapchainPresentationPipelineAcquireCode::SurfaceNotLive);
    }
    if (!instance_generation.mPresentationDeviceGeneration || instance_generation.physicalDevice() == VK_NULL_HANDLE)
    {
        return swapchainPresentationPipelineFailure(
            VulkanSwapchainPresentationPipelineAcquireCode::PresentationDeviceNotLive);
    }
    if (!instance_generation.mLogicalDeviceGeneration || instance_generation.logicalDevice() == VK_NULL_HANDLE ||
        instance_generation.presentationQueue() == VK_NULL_HANDLE)
    {
        return swapchainPresentationPipelineFailure(
            VulkanSwapchainPresentationPipelineAcquireCode::LogicalDeviceNotLive);
    }
    if (!instance_generation.mSwapchainConfigurationGeneration)
    {
        return swapchainPresentationPipelineFailure(
            VulkanSwapchainPresentationPipelineAcquireCode::SwapchainConfigurationNotLive);
    }
    if (!instance_generation.mSwapchainGeneration || instance_generation.swapchain() == VK_NULL_HANDLE)
    {
        return swapchainPresentationPipelineFailure(VulkanSwapchainPresentationPipelineAcquireCode::SwapchainNotLive);
    }
    if (!instance_generation.mSwapchainImagesGeneration || instance_generation.resolvedSwapchainImageCount() == 0 ||
        instance_generation.mSwapchainImagesGeneration->imageFormat() == VK_FORMAT_UNDEFINED)
    {
        return swapchainPresentationPipelineFailure(
            VulkanSwapchainPresentationPipelineAcquireCode::SwapchainImagesNotLive);
    }
    if (!instance_generation.mSwapchainPresentationTargetGeneration ||
        instance_generation.swapchainPresentationRenderPass() == VK_NULL_HANDLE)
    {
        return swapchainPresentationPipelineFailure(
            VulkanSwapchainPresentationPipelineAcquireCode::SwapchainPresentationTargetNotLive);
    }
    if (instance_generation.mSwapchainPresentationPipelineGeneration)
    {
        return swapchainPresentationPipelineFailure(
            VulkanSwapchainPresentationPipelineAcquireCode::SwapchainPresentationPipelineAlreadyOwned);
    }
    if (request.mNativeWindowGeneration != instance_generation.mNativeWindowGeneration ||
        request.mNativeWindowGeneration != instance_generation.surfaceNativeWindowGeneration())
    {
        return swapchainPresentationPipelineFailure(
            VulkanSwapchainPresentationPipelineAcquireCode::NativeWindowGenerationMismatch);
    }
    if (request.mDrawableExtent.width != instance_generation.swapchainDrawableExtent().width ||
        request.mDrawableExtent.height != instance_generation.swapchainDrawableExtent().height)
    {
        return swapchainPresentationPipelineFailure(
            VulkanSwapchainPresentationPipelineAcquireCode::DrawableExtentMismatch);
    }

    const std::uint64_t acquisition_epoch = instance_generation.mOwnershipTransitionEpoch;
    const auto freshness_check = [&]() noexcept
    {
        return swapchainPresentationPipelineFreshness(request,
                                                      instance_generation,
                                                      &instance_generation.mOwnershipTransitionEpoch,
                                                      acquisition_epoch);
    };
    if (VulkanSwapchainPresentationPipelineAcquireResult freshness = freshness_check())
    {
        return freshness;
    }
    if (instance_generation.mSwapchainPresentationPipelineGeneration)
    {
        return swapchainPresentationPipelineFailure(
            VulkanSwapchainPresentationPipelineAcquireCode::SwapchainPresentationPipelineAlreadyOwned);
    }
    VulkanInstanceGeneration::NativeAcquisitionGuard native_guard(instance_generation);

    const VkInstance                              instance = instance_generation.mInstance;
    const std::uint64_t                           native_window_generation = instance_generation.mNativeWindowGeneration;
    const auto*                                   surface_generation = instance_generation.mSurfaceGeneration.get();
    const std::uint64_t                           surface_native_window_generation =
        instance_generation.surfaceNativeWindowGeneration();
    const VkSurfaceKHR                            surface = instance_generation.surface();
    const VulkanPhysicalDeviceGeneration*         selection = instance_generation.mPresentationDeviceGeneration.get();
    const VulkanLogicalDeviceGeneration*          logical_device = instance_generation.mLogicalDeviceGeneration.get();
    const VulkanSwapchainConfigurationGeneration* configuration =
        instance_generation.mSwapchainConfigurationGeneration.get();
    const VulkanSwapchainGeneration* swapchain_generation = instance_generation.mSwapchainGeneration.get();
    const VulkanSwapchainImagesGeneration* images_generation = instance_generation.mSwapchainImagesGeneration.get();
    const VulkanSwapchainPresentationTargetGeneration* presentation_target =
        instance_generation.mSwapchainPresentationTargetGeneration.get();
    const PFN_vkGetInstanceProcAddr get_instance_proc_addr = logical_device->getInstanceProcAddr();
    const VkPhysicalDevice          physical_device = selection->physicalDevice();
    const std::uint32_t             physical_device_index = selection->physicalDeviceIndex();
    const VkDevice                  device = logical_device->device();
    const VkQueue                   queue = logical_device->queue();
    const std::uint32_t             queue_family = logical_device->queueFamilyIndex();
    const std::uint32_t             queue_index = logical_device->queueIndex();
    const VkExtent2D                drawable_extent = request.mDrawableExtent;
    const VkSwapchainKHR            swapchain = swapchain_generation->swapchain();
    const std::uint32_t             image_count = images_generation->imageCount();
    const VkFormat                  image_format = images_generation->imageFormat();
    const VkRenderPass              render_pass = presentation_target->renderPass();
    const std::uint32_t             framebuffer_count = presentation_target->framebufferCount();

    if (!selection->selectedFor(instance, surface))
    {
        return swapchainPresentationPipelineFailure(
            VulkanSwapchainPresentationPipelineAcquireCode::PresentationDeviceNotLive);
    }
    if (!logical_device->createdFor(*selection))
    {
        return swapchainPresentationPipelineFailure(
            VulkanSwapchainPresentationPipelineAcquireCode::LogicalDeviceNotLive);
    }
    if (!configuration->createdFor(*selection, *logical_device, drawable_extent))
    {
        return swapchainPresentationPipelineFailure(
            VulkanSwapchainPresentationPipelineAcquireCode::SwapchainConfigurationNotLive);
    }
    if (!swapchain_generation->createdFor(*logical_device, *configuration))
    {
        return swapchainPresentationPipelineFailure(VulkanSwapchainPresentationPipelineAcquireCode::SwapchainNotLive);
    }
    if (!images_generation->createdFor(*logical_device, *configuration, *swapchain_generation))
    {
        return swapchainPresentationPipelineFailure(
            VulkanSwapchainPresentationPipelineAcquireCode::SwapchainImagesNotLive);
    }
    if (!presentation_target->createdFor(*logical_device, *configuration, *swapchain_generation, *images_generation))
    {
        return swapchainPresentationPipelineFailure(
            VulkanSwapchainPresentationPipelineAcquireCode::SwapchainPresentationTargetNotLive);
    }

    VulkanSwapchainPresentationPipelineResolutionResult resolution_result =
        resolveVulkanSwapchainPresentationPipelineGeneration(
            *logical_device, *configuration, *swapchain_generation, *images_generation, *presentation_target);
    if (const auto* error = std::get_if<VulkanSwapchainPresentationPipelineResolutionError>(&resolution_result))
    {
        return swapchainPresentationPipelineFailure(
            VulkanSwapchainPresentationPipelineAcquireCode::ResolutionFailure, *error);
    }

    try
    {
        if (allocation_checkpoint)
        {
            allocation_checkpoint();
            if (instance_generation.mSwapchainPresentationPipelineGeneration)
            {
                return swapchainPresentationPipelineFailure(
                    VulkanSwapchainPresentationPipelineAcquireCode::SwapchainPresentationPipelineAlreadyOwned);
            }
            if (instance_generation.mOwnershipTransitionEpoch != acquisition_epoch)
            {
                return swapchainPresentationPipelineFailure(
                    VulkanSwapchainPresentationPipelineAcquireCode::SwapchainPresentationTargetNotLive);
            }
        }
        auto pending = std::make_unique<VulkanSwapchainPresentationPipelineGeneration>(
            std::move(std::get<VulkanSwapchainPresentationPipelineGeneration>(resolution_result)));

        if (VulkanSwapchainPresentationPipelineAcquireResult freshness = freshness_check())
        {
            return freshness;
        }
        if (instance_generation.mSwapchainPresentationPipelineGeneration)
        {
            return swapchainPresentationPipelineFailure(
                VulkanSwapchainPresentationPipelineAcquireCode::SwapchainPresentationPipelineAlreadyOwned);
        }
        if (instance_generation.mInstance != instance || !instance_generation.mDestroyInstance ||
            !instance_generation.mGlobalDispatch ||
            instance_generation.mNativeWindowGeneration != native_window_generation)
        {
            return swapchainPresentationPipelineFailure(
                VulkanSwapchainPresentationPipelineAcquireCode::InstanceNotLive);
        }
        if (instance_generation.mSurfaceGeneration.get() != surface_generation ||
            instance_generation.surfaceNativeWindowGeneration() != surface_native_window_generation ||
            instance_generation.surface() != surface)
        {
            return swapchainPresentationPipelineFailure(
                VulkanSwapchainPresentationPipelineAcquireCode::SurfaceNotLive);
        }
        if (instance_generation.mPresentationDeviceGeneration.get() != selection ||
            selection->getInstanceProcAddr() != get_instance_proc_addr || selection->physicalDevice() != physical_device ||
            selection->physicalDeviceIndex() != physical_device_index || !selection->selectedFor(instance, surface))
        {
            return swapchainPresentationPipelineFailure(
                VulkanSwapchainPresentationPipelineAcquireCode::PresentationDeviceNotLive);
        }
        if (instance_generation.mLogicalDeviceGeneration.get() != logical_device ||
            logical_device->getInstanceProcAddr() != get_instance_proc_addr || logical_device->device() != device ||
            logical_device->queue() != queue || logical_device->queueFamilyIndex() != queue_family ||
            logical_device->queueIndex() != queue_index || !logical_device->createdFor(*selection))
        {
            return swapchainPresentationPipelineFailure(
                VulkanSwapchainPresentationPipelineAcquireCode::LogicalDeviceNotLive);
        }
        if (instance_generation.mSwapchainConfigurationGeneration.get() != configuration ||
            !configuration->createdFor(*selection, *logical_device, drawable_extent))
        {
            return swapchainPresentationPipelineFailure(
                VulkanSwapchainPresentationPipelineAcquireCode::SwapchainConfigurationNotLive);
        }
        if (instance_generation.mSwapchainGeneration.get() != swapchain_generation ||
            swapchain_generation->swapchain() != swapchain ||
            !swapchain_generation->createdFor(*logical_device, *configuration))
        {
            return swapchainPresentationPipelineFailure(
                VulkanSwapchainPresentationPipelineAcquireCode::SwapchainNotLive);
        }
        if (instance_generation.mSwapchainImagesGeneration.get() != images_generation ||
            images_generation->imageCount() != image_count || images_generation->imageFormat() != image_format ||
            !images_generation->createdFor(*logical_device, *configuration, *swapchain_generation))
        {
            return swapchainPresentationPipelineFailure(
                VulkanSwapchainPresentationPipelineAcquireCode::SwapchainImagesNotLive);
        }
        if (instance_generation.mSwapchainPresentationTargetGeneration.get() != presentation_target ||
            presentation_target->renderPass() != render_pass ||
            presentation_target->framebufferCount() != framebuffer_count ||
            !presentation_target->createdFor(*logical_device, *configuration, *swapchain_generation, *images_generation) ||
            !pending->createdFor(*logical_device, *configuration, *swapchain_generation, *images_generation,
                                 *presentation_target))
        {
            return swapchainPresentationPipelineFailure(
                VulkanSwapchainPresentationPipelineAcquireCode::SwapchainPresentationTargetNotLive);
        }

        instance_generation.mSwapchainPresentationPipelineGeneration = std::move(pending);
        instance_generation.noteSwapchainPresentationPipelineTransition();
        return std::nullopt;
    }
    catch (const std::bad_alloc&)
    {
        return swapchainPresentationPipelineFailure(
            VulkanSwapchainPresentationPipelineAcquireCode::AllocationFailure);
    }
}

VulkanSwapchainReadbackAcquireResult VulkanInstanceGenerationFactory::acquireSwapchainReadback(
    VulkanInstanceGeneration&                  instance_generation,
    const VulkanSwapchainReadbackRequest&      request,
    VulkanInstanceDetail::AllocationCheckpoint allocation_checkpoint) noexcept
{
    if (!request.mInstanceOwnerCheck.mIsCurrent)
    {
        return swapchainReadbackFailure(VulkanSwapchainReadbackAcquireCode::InvalidInstanceOwnerCheck);
    }
    if (!request.mWindowGenerationCheck.mIsCurrent)
    {
        return swapchainReadbackFailure(VulkanSwapchainReadbackAcquireCode::InvalidWindowGenerationCheck);
    }
    if (request.mNativeWindowGeneration == 0)
    {
        return swapchainReadbackFailure(VulkanSwapchainReadbackAcquireCode::InvalidNativeWindowGeneration);
    }
    if (request.mDrawableExtent.width == 0 || request.mDrawableExtent.height == 0)
    {
        return swapchainReadbackFailure(VulkanSwapchainReadbackAcquireCode::InvalidDrawableExtent);
    }
    if (instance_generation.mSwapchainPresentationTargetTeardownDepth != 0)
    {
        return swapchainReadbackFailure(VulkanSwapchainReadbackAcquireCode::NativeTeardownInProgress);
    }
    if (instance_generation.mInstance == VK_NULL_HANDLE || !instance_generation.mDestroyInstance || !instance_generation.mGlobalDispatch ||
        instance_generation.mNativeWindowGeneration == 0)
    {
        return swapchainReadbackFailure(VulkanSwapchainReadbackAcquireCode::InstanceNotLive);
    }
    if (!instance_generation.mSurfaceGeneration || instance_generation.surface() == VK_NULL_HANDLE)
    {
        return swapchainReadbackFailure(VulkanSwapchainReadbackAcquireCode::SurfaceNotLive);
    }
    if (!instance_generation.mPresentationDeviceGeneration || instance_generation.physicalDevice() == VK_NULL_HANDLE)
    {
        return swapchainReadbackFailure(VulkanSwapchainReadbackAcquireCode::PresentationDeviceNotLive);
    }
    if (!instance_generation.mLogicalDeviceGeneration || instance_generation.logicalDevice() == VK_NULL_HANDLE ||
        instance_generation.presentationQueue() == VK_NULL_HANDLE)
    {
        return swapchainReadbackFailure(VulkanSwapchainReadbackAcquireCode::LogicalDeviceNotLive);
    }
    if (!instance_generation.mSwapchainConfigurationGeneration)
    {
        return swapchainReadbackFailure(VulkanSwapchainReadbackAcquireCode::SwapchainConfigurationNotLive);
    }
    if (!instance_generation.mSwapchainGeneration || instance_generation.swapchain() == VK_NULL_HANDLE)
    {
        return swapchainReadbackFailure(VulkanSwapchainReadbackAcquireCode::SwapchainNotLive);
    }
    if (!instance_generation.mSwapchainImagesGeneration || instance_generation.resolvedSwapchainImageCount() == 0 ||
        instance_generation.mSwapchainImagesGeneration->imageFormat() == VK_FORMAT_UNDEFINED)
    {
        return swapchainReadbackFailure(VulkanSwapchainReadbackAcquireCode::SwapchainImagesNotLive);
    }
    if (instance_generation.mSwapchainReadbackGeneration)
    {
        return swapchainReadbackFailure(VulkanSwapchainReadbackAcquireCode::SwapchainReadbackAlreadyOwned);
    }
    if (request.mNativeWindowGeneration != instance_generation.mNativeWindowGeneration ||
        request.mNativeWindowGeneration != instance_generation.surfaceNativeWindowGeneration())
    {
        return swapchainReadbackFailure(VulkanSwapchainReadbackAcquireCode::NativeWindowGenerationMismatch);
    }
    if (request.mDrawableExtent.width != instance_generation.swapchainDrawableExtent().width ||
        request.mDrawableExtent.height != instance_generation.swapchainDrawableExtent().height)
    {
        return swapchainReadbackFailure(VulkanSwapchainReadbackAcquireCode::DrawableExtentMismatch);
    }

    const std::uint64_t acquisition_epoch = instance_generation.mOwnershipTransitionEpoch;
    const auto          freshness_check   = [&]() noexcept
    {
        return swapchainReadbackFreshness(request, instance_generation, &instance_generation.mOwnershipTransitionEpoch, acquisition_epoch);
    };
    if (VulkanSwapchainReadbackAcquireResult freshness = freshness_check())
    {
        return freshness;
    }
    if (instance_generation.mSwapchainReadbackGeneration)
    {
        return swapchainReadbackFailure(VulkanSwapchainReadbackAcquireCode::SwapchainReadbackAlreadyOwned);
    }
    VulkanInstanceGeneration::NativeAcquisitionGuard native_guard(instance_generation);

    const VkInstance                              instance                         = instance_generation.mInstance;
    const std::uint64_t                           native_window_generation         = instance_generation.mNativeWindowGeneration;
    const auto*                                   surface_generation               = instance_generation.mSurfaceGeneration.get();
    const std::uint64_t                           surface_native_window_generation = instance_generation.surfaceNativeWindowGeneration();
    const VkSurfaceKHR                            surface                          = instance_generation.surface();
    const VulkanPhysicalDeviceGeneration*         selection              = instance_generation.mPresentationDeviceGeneration.get();
    const VulkanLogicalDeviceGeneration*          logical_device         = instance_generation.mLogicalDeviceGeneration.get();
    const VulkanSwapchainConfigurationGeneration* configuration          = instance_generation.mSwapchainConfigurationGeneration.get();
    const VulkanSwapchainGeneration*              swapchain_generation   = instance_generation.mSwapchainGeneration.get();
    const VulkanSwapchainImagesGeneration*        images_generation      = instance_generation.mSwapchainImagesGeneration.get();
    const PFN_vkGetInstanceProcAddr               get_instance_proc_addr = logical_device->getInstanceProcAddr();
    const VkPhysicalDevice                        physical_device        = selection->physicalDevice();
    const std::uint32_t                           physical_device_index  = selection->physicalDeviceIndex();
    const VkDevice                                device                 = logical_device->device();
    const VkQueue                                 queue                  = logical_device->queue();
    const std::uint32_t                           queue_family           = logical_device->queueFamilyIndex();
    const std::uint32_t                           queue_index            = logical_device->queueIndex();
    const VkExtent2D                              drawable_extent        = request.mDrawableExtent;
    const VkSwapchainKHR                          swapchain              = swapchain_generation->swapchain();
    const std::uint32_t                           image_count            = images_generation->imageCount();
    const VkFormat                                image_format           = images_generation->imageFormat();
    const VkExtent2D                              image_extent           = configuration->imageExtent();

    if (!selection->selectedFor(instance, surface))
    {
        return swapchainReadbackFailure(VulkanSwapchainReadbackAcquireCode::PresentationDeviceNotLive);
    }
    if (!logical_device->createdFor(*selection))
    {
        return swapchainReadbackFailure(VulkanSwapchainReadbackAcquireCode::LogicalDeviceNotLive);
    }
    if (!configuration->createdFor(*selection, *logical_device, drawable_extent))
    {
        return swapchainReadbackFailure(VulkanSwapchainReadbackAcquireCode::SwapchainConfigurationNotLive);
    }
    if (!swapchain_generation->createdFor(*logical_device, *configuration))
    {
        return swapchainReadbackFailure(VulkanSwapchainReadbackAcquireCode::SwapchainNotLive);
    }
    if (!images_generation->createdFor(*logical_device, *configuration, *swapchain_generation))
    {
        return swapchainReadbackFailure(VulkanSwapchainReadbackAcquireCode::SwapchainImagesNotLive);
    }

    VulkanSwapchainReadbackResolutionResult resolution_result =
        resolveVulkanSwapchainReadbackGeneration(*selection, *logical_device, *configuration, *swapchain_generation, *images_generation);
    if (const auto* error = std::get_if<VulkanSwapchainReadbackResolutionError>(&resolution_result))
    {
        return swapchainReadbackFailure(VulkanSwapchainReadbackAcquireCode::ResolutionFailure, *error);
    }

    try
    {
        if (allocation_checkpoint)
        {
            allocation_checkpoint();
            if (instance_generation.mSwapchainReadbackGeneration)
            {
                return swapchainReadbackFailure(VulkanSwapchainReadbackAcquireCode::SwapchainReadbackAlreadyOwned);
            }
            if (instance_generation.mOwnershipTransitionEpoch != acquisition_epoch)
            {
                return swapchainReadbackFailure(VulkanSwapchainReadbackAcquireCode::SwapchainImagesNotLive);
            }
        }
        auto pending =
            std::make_unique<VulkanSwapchainReadbackGeneration>(std::move(std::get<VulkanSwapchainReadbackGeneration>(resolution_result)));

        if (VulkanSwapchainReadbackAcquireResult freshness = freshness_check())
        {
            return freshness;
        }
        if (instance_generation.mSwapchainReadbackGeneration)
        {
            return swapchainReadbackFailure(VulkanSwapchainReadbackAcquireCode::SwapchainReadbackAlreadyOwned);
        }
        if (instance_generation.mInstance != instance || !instance_generation.mDestroyInstance || !instance_generation.mGlobalDispatch ||
            instance_generation.mNativeWindowGeneration != native_window_generation)
        {
            return swapchainReadbackFailure(VulkanSwapchainReadbackAcquireCode::InstanceNotLive);
        }
        if (instance_generation.mSurfaceGeneration.get() != surface_generation ||
            instance_generation.surfaceNativeWindowGeneration() != surface_native_window_generation ||
            instance_generation.surface() != surface)
        {
            return swapchainReadbackFailure(VulkanSwapchainReadbackAcquireCode::SurfaceNotLive);
        }
        if (instance_generation.mPresentationDeviceGeneration.get() != selection ||
            selection->getInstanceProcAddr() != get_instance_proc_addr || selection->physicalDevice() != physical_device ||
            selection->physicalDeviceIndex() != physical_device_index || !selection->selectedFor(instance, surface))
        {
            return swapchainReadbackFailure(VulkanSwapchainReadbackAcquireCode::PresentationDeviceNotLive);
        }
        if (instance_generation.mLogicalDeviceGeneration.get() != logical_device ||
            logical_device->getInstanceProcAddr() != get_instance_proc_addr || logical_device->device() != device ||
            logical_device->queue() != queue || logical_device->queueFamilyIndex() != queue_family ||
            logical_device->queueIndex() != queue_index || !logical_device->createdFor(*selection))
        {
            return swapchainReadbackFailure(VulkanSwapchainReadbackAcquireCode::LogicalDeviceNotLive);
        }
        if (instance_generation.mSwapchainConfigurationGeneration.get() != configuration ||
            !configuration->createdFor(*selection, *logical_device, drawable_extent))
        {
            return swapchainReadbackFailure(VulkanSwapchainReadbackAcquireCode::SwapchainConfigurationNotLive);
        }
        if (instance_generation.mSwapchainGeneration.get() != swapchain_generation || swapchain_generation->swapchain() != swapchain ||
            !swapchain_generation->createdFor(*logical_device, *configuration))
        {
            return swapchainReadbackFailure(VulkanSwapchainReadbackAcquireCode::SwapchainNotLive);
        }
        if (instance_generation.mSwapchainImagesGeneration.get() != images_generation || images_generation->imageCount() != image_count ||
            images_generation->imageFormat() != image_format || configuration->imageExtent().width != image_extent.width ||
            configuration->imageExtent().height != image_extent.height ||
            !images_generation->createdFor(*logical_device, *configuration, *swapchain_generation) ||
            !pending->createdFor(*selection, *logical_device, *configuration, *swapchain_generation, *images_generation))
        {
            return swapchainReadbackFailure(VulkanSwapchainReadbackAcquireCode::SwapchainImagesNotLive);
        }

        instance_generation.mSwapchainReadbackGeneration = std::move(pending);
        instance_generation.noteSwapchainReadbackTransition();
        return std::nullopt;
    }
    catch (const std::bad_alloc&)
    {
        return swapchainReadbackFailure(VulkanSwapchainReadbackAcquireCode::AllocationFailure);
    }
}

VulkanSwapchainFrameSlotAcquireResult VulkanInstanceGenerationFactory::acquireSwapchainFrameSlot(
    VulkanInstanceGeneration&                  instance_generation,
    const VulkanSwapchainFrameSlotRequest&     request,
    VulkanInstanceDetail::AllocationCheckpoint allocation_checkpoint) noexcept
{
    if (!request.mInstanceOwnerCheck.mIsCurrent)
    {
        return swapchainFrameSlotFailure(VulkanSwapchainFrameSlotAcquireCode::InvalidInstanceOwnerCheck);
    }
    if (!request.mWindowGenerationCheck.mIsCurrent)
    {
        return swapchainFrameSlotFailure(VulkanSwapchainFrameSlotAcquireCode::InvalidWindowGenerationCheck);
    }
    if (request.mNativeWindowGeneration == 0)
    {
        return swapchainFrameSlotFailure(VulkanSwapchainFrameSlotAcquireCode::InvalidNativeWindowGeneration);
    }
    if (request.mDrawableExtent.width == 0 || request.mDrawableExtent.height == 0)
    {
        return swapchainFrameSlotFailure(VulkanSwapchainFrameSlotAcquireCode::InvalidDrawableExtent);
    }
    if (instance_generation.mSwapchainPresentationTargetTeardownDepth != 0)
    {
        return swapchainFrameSlotFailure(VulkanSwapchainFrameSlotAcquireCode::NativeTeardownInProgress);
    }
    if (instance_generation.mInstance == VK_NULL_HANDLE || !instance_generation.mDestroyInstance || !instance_generation.mGlobalDispatch ||
        instance_generation.mNativeWindowGeneration == 0)
    {
        return swapchainFrameSlotFailure(VulkanSwapchainFrameSlotAcquireCode::InstanceNotLive);
    }
    if (!instance_generation.mSurfaceGeneration || instance_generation.surface() == VK_NULL_HANDLE)
    {
        return swapchainFrameSlotFailure(VulkanSwapchainFrameSlotAcquireCode::SurfaceNotLive);
    }
    if (!instance_generation.mPresentationDeviceGeneration || instance_generation.physicalDevice() == VK_NULL_HANDLE)
    {
        return swapchainFrameSlotFailure(VulkanSwapchainFrameSlotAcquireCode::PresentationDeviceNotLive);
    }
    if (!instance_generation.mLogicalDeviceGeneration || instance_generation.logicalDevice() == VK_NULL_HANDLE ||
        instance_generation.presentationQueue() == VK_NULL_HANDLE)
    {
        return swapchainFrameSlotFailure(VulkanSwapchainFrameSlotAcquireCode::LogicalDeviceNotLive);
    }
    if (!instance_generation.mSwapchainConfigurationGeneration)
    {
        return swapchainFrameSlotFailure(VulkanSwapchainFrameSlotAcquireCode::SwapchainConfigurationNotLive);
    }
    if (!instance_generation.mSwapchainGeneration || instance_generation.swapchain() == VK_NULL_HANDLE)
    {
        return swapchainFrameSlotFailure(VulkanSwapchainFrameSlotAcquireCode::SwapchainNotLive);
    }
    if (!instance_generation.mSwapchainImagesGeneration || instance_generation.resolvedSwapchainImageCount() == 0 ||
        instance_generation.mSwapchainImagesGeneration->imageFormat() == VK_FORMAT_UNDEFINED)
    {
        return swapchainFrameSlotFailure(VulkanSwapchainFrameSlotAcquireCode::SwapchainImagesNotLive);
    }
    if (instance_generation.mSwapchainFrameSlotGeneration)
    {
        return swapchainFrameSlotFailure(VulkanSwapchainFrameSlotAcquireCode::SwapchainFrameSlotAlreadyOwned);
    }
    if (request.mNativeWindowGeneration != instance_generation.mNativeWindowGeneration ||
        request.mNativeWindowGeneration != instance_generation.surfaceNativeWindowGeneration())
    {
        return swapchainFrameSlotFailure(VulkanSwapchainFrameSlotAcquireCode::NativeWindowGenerationMismatch);
    }
    if (request.mDrawableExtent.width != instance_generation.swapchainDrawableExtent().width ||
        request.mDrawableExtent.height != instance_generation.swapchainDrawableExtent().height)
    {
        return swapchainFrameSlotFailure(VulkanSwapchainFrameSlotAcquireCode::DrawableExtentMismatch);
    }
    const std::uint64_t acquisition_epoch = instance_generation.mOwnershipTransitionEpoch;
    const auto freshness_check = [&]() noexcept
    {
        return swapchainFrameSlotFreshness(request,
                                           instance_generation,
                                           &instance_generation.mOwnershipTransitionEpoch,
                                           acquisition_epoch);
    };
    if (VulkanSwapchainFrameSlotAcquireResult freshness = freshness_check())
    {
        return freshness;
    }
    if (instance_generation.mSwapchainFrameSlotGeneration)
    {
        return swapchainFrameSlotFailure(VulkanSwapchainFrameSlotAcquireCode::SwapchainFrameSlotAlreadyOwned);
    }
    VulkanInstanceGeneration::NativeAcquisitionGuard native_guard(instance_generation);

    const VkInstance                              instance                         = instance_generation.mInstance;
    const std::uint64_t                           native_window_generation         = instance_generation.mNativeWindowGeneration;
    const auto*                                   surface_generation               = instance_generation.mSurfaceGeneration.get();
    const std::uint64_t                           surface_native_window_generation = instance_generation.surfaceNativeWindowGeneration();
    const VkSurfaceKHR                            surface                          = instance_generation.surface();
    const VulkanPhysicalDeviceGeneration*         selection              = instance_generation.mPresentationDeviceGeneration.get();
    const VulkanLogicalDeviceGeneration*          logical_device         = instance_generation.mLogicalDeviceGeneration.get();
    const VulkanSwapchainConfigurationGeneration* configuration          = instance_generation.mSwapchainConfigurationGeneration.get();
    const VulkanSwapchainGeneration*              swapchain_generation   = instance_generation.mSwapchainGeneration.get();
    const VulkanSwapchainImagesGeneration*        images_generation      = instance_generation.mSwapchainImagesGeneration.get();
    const PFN_vkGetInstanceProcAddr               get_instance_proc_addr = logical_device->getInstanceProcAddr();
    const VkPhysicalDevice                        physical_device        = selection->physicalDevice();
    const std::uint32_t                           physical_device_index  = selection->physicalDeviceIndex();
    const VkDevice                                device                 = logical_device->device();
    const VkQueue                                 queue                  = logical_device->queue();
    const std::uint32_t                           queue_family           = logical_device->queueFamilyIndex();
    const std::uint32_t                           queue_index            = logical_device->queueIndex();
    const VkExtent2D                              drawable_extent        = request.mDrawableExtent;
    const VkSwapchainKHR                          swapchain              = swapchain_generation->swapchain();
    const std::uint32_t                           image_count            = images_generation->imageCount();
    const VkFormat                                image_format           = images_generation->imageFormat();

    if (!selection->selectedFor(instance, surface))
    {
        return swapchainFrameSlotFailure(VulkanSwapchainFrameSlotAcquireCode::PresentationDeviceNotLive);
    }
    if (!logical_device->createdFor(*selection))
    {
        return swapchainFrameSlotFailure(VulkanSwapchainFrameSlotAcquireCode::LogicalDeviceNotLive);
    }
    if (!configuration->createdFor(*selection, *logical_device, drawable_extent))
    {
        return swapchainFrameSlotFailure(VulkanSwapchainFrameSlotAcquireCode::SwapchainConfigurationNotLive);
    }
    if (!swapchain_generation->createdFor(*logical_device, *configuration))
    {
        return swapchainFrameSlotFailure(VulkanSwapchainFrameSlotAcquireCode::SwapchainNotLive);
    }
    if (!images_generation->createdFor(*logical_device, *configuration, *swapchain_generation))
    {
        return swapchainFrameSlotFailure(VulkanSwapchainFrameSlotAcquireCode::SwapchainImagesNotLive);
    }

    VulkanSwapchainFrameSlotResolutionResult resolution_result =
        resolveVulkanSwapchainFrameSlotGeneration(*logical_device, *configuration, *swapchain_generation, *images_generation);
    if (const auto* error = std::get_if<VulkanSwapchainFrameSlotResolutionError>(&resolution_result))
    {
        return swapchainFrameSlotFailure(VulkanSwapchainFrameSlotAcquireCode::ResolutionFailure, *error);
    }

    try
    {
        if (allocation_checkpoint)
        {
            allocation_checkpoint();
            if (instance_generation.mSwapchainFrameSlotGeneration)
            {
                return swapchainFrameSlotFailure(VulkanSwapchainFrameSlotAcquireCode::SwapchainFrameSlotAlreadyOwned);
            }
            if (instance_generation.mOwnershipTransitionEpoch != acquisition_epoch)
            {
                return swapchainFrameSlotFailure(VulkanSwapchainFrameSlotAcquireCode::SwapchainImagesNotLive);
            }
        }
        auto pending = std::make_unique<VulkanSwapchainFrameSlotGeneration>(
            std::move(std::get<VulkanSwapchainFrameSlotGeneration>(resolution_result)));

        if (VulkanSwapchainFrameSlotAcquireResult freshness = freshness_check())
        {
            return freshness;
        }
        if (instance_generation.mSwapchainFrameSlotGeneration)
        {
            return swapchainFrameSlotFailure(VulkanSwapchainFrameSlotAcquireCode::SwapchainFrameSlotAlreadyOwned);
        }
        if (instance_generation.mInstance != instance || !instance_generation.mDestroyInstance || !instance_generation.mGlobalDispatch ||
            instance_generation.mNativeWindowGeneration != native_window_generation)
        {
            return swapchainFrameSlotFailure(VulkanSwapchainFrameSlotAcquireCode::InstanceNotLive);
        }
        if (instance_generation.mSurfaceGeneration.get() != surface_generation ||
            instance_generation.surfaceNativeWindowGeneration() != surface_native_window_generation ||
            instance_generation.surface() != surface)
        {
            return swapchainFrameSlotFailure(VulkanSwapchainFrameSlotAcquireCode::SurfaceNotLive);
        }
        if (instance_generation.mPresentationDeviceGeneration.get() != selection ||
            selection->getInstanceProcAddr() != get_instance_proc_addr || selection->physicalDevice() != physical_device ||
            selection->physicalDeviceIndex() != physical_device_index || !selection->selectedFor(instance, surface))
        {
            return swapchainFrameSlotFailure(VulkanSwapchainFrameSlotAcquireCode::PresentationDeviceNotLive);
        }
        if (instance_generation.mLogicalDeviceGeneration.get() != logical_device ||
            logical_device->getInstanceProcAddr() != get_instance_proc_addr || logical_device->device() != device ||
            logical_device->queue() != queue || logical_device->queueFamilyIndex() != queue_family ||
            logical_device->queueIndex() != queue_index || !logical_device->createdFor(*selection))
        {
            return swapchainFrameSlotFailure(VulkanSwapchainFrameSlotAcquireCode::LogicalDeviceNotLive);
        }
        if (instance_generation.mSwapchainConfigurationGeneration.get() != configuration ||
            !configuration->createdFor(*selection, *logical_device, drawable_extent))
        {
            return swapchainFrameSlotFailure(VulkanSwapchainFrameSlotAcquireCode::SwapchainConfigurationNotLive);
        }
        if (instance_generation.mSwapchainGeneration.get() != swapchain_generation || swapchain_generation->swapchain() != swapchain ||
            !swapchain_generation->createdFor(*logical_device, *configuration))
        {
            return swapchainFrameSlotFailure(VulkanSwapchainFrameSlotAcquireCode::SwapchainNotLive);
        }
        if (instance_generation.mSwapchainImagesGeneration.get() != images_generation || images_generation->imageCount() != image_count ||
            images_generation->imageFormat() != image_format ||
            !images_generation->createdFor(*logical_device, *configuration, *swapchain_generation) ||
            !pending->createdFor(*logical_device, *configuration, *swapchain_generation, *images_generation))
        {
            return swapchainFrameSlotFailure(VulkanSwapchainFrameSlotAcquireCode::SwapchainImagesNotLive);
        }

        instance_generation.mSwapchainFrameSlotGeneration = std::move(pending);
        instance_generation.noteSwapchainFrameSlotTransition();
        return std::nullopt;
    }
    catch (const std::bad_alloc&)
    {
        return swapchainFrameSlotFailure(VulkanSwapchainFrameSlotAcquireCode::AllocationFailure);
    }
}

VulkanSwapchainChainRebuildResult VulkanInstanceGenerationFactory::rebuildSwapchainChain(
    VulkanInstanceGeneration&                  instance_generation,
    const VulkanSwapchainChainRebuildRequest&  request,
    VulkanInstanceDetail::AllocationCheckpoint allocation_checkpoint) noexcept
{
    constexpr VulkanSwapchainChainRebuildPhase preflight = VulkanSwapchainChainRebuildPhase::Preflight;

    if (!request.mInstanceOwnerCheck.mIsCurrent)
    {
        return swapchainChainRebuildFailure(VulkanSwapchainChainRebuildCode::InvalidInstanceOwnerCheck, preflight);
    }
    if (!request.mWindowGenerationCheck.mIsCurrent)
    {
        return swapchainChainRebuildFailure(VulkanSwapchainChainRebuildCode::InvalidWindowGenerationCheck, preflight);
    }
    if (request.mNativeWindowGeneration == 0)
    {
        return swapchainChainRebuildFailure(VulkanSwapchainChainRebuildCode::InvalidNativeWindowGeneration, preflight);
    }
    if (!request.mDrawableExtent)
    {
        return swapchainChainRebuildFailure(VulkanSwapchainChainRebuildCode::InvalidDrawableExtent, preflight);
    }
    if (instance_generation.mNativeAcquisitionDepth != 0)
    {
        return swapchainChainRebuildFailure(VulkanSwapchainChainRebuildCode::NativeAcquisitionInProgress, preflight);
    }
    if (instance_generation.mInstance == VK_NULL_HANDLE || !instance_generation.mDestroyInstance ||
        !instance_generation.mGlobalDispatch || !instance_generation.mGlobalDispatch->getInstanceProcAddr() ||
        instance_generation.mNativeWindowGeneration == 0)
    {
        return swapchainChainRebuildFailure(VulkanSwapchainChainRebuildCode::InstanceNotLive, preflight);
    }
    if (!instance_generation.mSurfaceGeneration || instance_generation.surface() == VK_NULL_HANDLE ||
        instance_generation.surfaceNativeWindowGeneration() == 0)
    {
        return swapchainChainRebuildFailure(VulkanSwapchainChainRebuildCode::SurfaceNotLive, preflight);
    }
    if (!instance_generation.mPresentationDeviceGeneration || instance_generation.physicalDevice() == VK_NULL_HANDLE)
    {
        return swapchainChainRebuildFailure(VulkanSwapchainChainRebuildCode::PresentationDeviceNotLive, preflight);
    }
    if (!instance_generation.mLogicalDeviceGeneration || instance_generation.logicalDevice() == VK_NULL_HANDLE ||
        instance_generation.presentationQueue() == VK_NULL_HANDLE)
    {
        return swapchainChainRebuildFailure(VulkanSwapchainChainRebuildCode::LogicalDeviceNotLive, preflight);
    }
    if (request.mNativeWindowGeneration != instance_generation.mNativeWindowGeneration ||
        request.mNativeWindowGeneration != instance_generation.surfaceNativeWindowGeneration())
    {
        return swapchainChainRebuildFailure(VulkanSwapchainChainRebuildCode::NativeWindowGenerationMismatch, preflight);
    }

    const auto* global_dispatch        = &*instance_generation.mGlobalDispatch;
    const auto* surface_generation     = instance_generation.mSurfaceGeneration.get();
    const auto* selection              = instance_generation.mPresentationDeviceGeneration.get();
    const auto* logical_device         = instance_generation.mLogicalDeviceGeneration.get();
    const auto* original_configuration = instance_generation.mSwapchainConfigurationGeneration.get();
    const auto* original_swapchain     = instance_generation.mSwapchainGeneration.get();
    const auto*         original_images                = instance_generation.mSwapchainImagesGeneration.get();
    const auto*         original_presentation_target   = instance_generation.mSwapchainPresentationTargetGeneration.get();
    const auto*         original_presentation_pipeline = instance_generation.mSwapchainPresentationPipelineGeneration.get();
    const auto*         original_readback              = instance_generation.mSwapchainReadbackGeneration.get();
    const auto*         original_frame_slot            = instance_generation.mSwapchainFrameSlotGeneration.get();
    const auto*         original_texture_destination           = instance_generation.mTextureUploadDestinationGeneration.get();
    const auto*         original_sample_binding                = instance_generation.mTextureUploadSampleBindingGeneration.get();
    const auto*         original_sample_pipeline               = instance_generation.mTextureUploadSamplePipelineGeneration.get();
    const std::uint64_t original_ownership_epoch       = instance_generation.mOwnershipTransitionEpoch;
    const std::uint64_t original_texture_destination_epoch     = instance_generation.mTextureUploadDestinationEpoch;
    const std::uint64_t original_sample_binding_epoch          = instance_generation.mTextureUploadSampleBindingEpoch;
    const std::uint64_t original_sample_pipeline_epoch         = instance_generation.mTextureUploadSamplePipelineEpoch;
    const std::uint64_t expected_retired_sample_pipeline_epoch = original_sample_pipeline_epoch + (original_sample_pipeline ? 1u : 0u);

    if ((original_sample_binding && !original_texture_destination) ||
        (original_sample_pipeline && (!original_texture_destination || !original_sample_binding || !original_configuration ||
                                      !original_swapchain || !original_images || !original_presentation_target)))
    {
        return swapchainChainRebuildFailure(VulkanSwapchainChainRebuildCode::PublicationFailure, preflight);
    }

    const PFN_vkGetInstanceProcAddr get_instance_proc_addr           = global_dispatch->getInstanceProcAddr();
    const VkInstance                instance                         = instance_generation.mInstance;
    const std::uint64_t             native_window_generation         = instance_generation.mNativeWindowGeneration;
    const VkSurfaceKHR              surface                          = surface_generation->surface();
    const std::uint64_t             surface_native_window_generation = surface_generation->nativeWindowGeneration();
    const VkPhysicalDevice          physical_device                  = selection->physicalDevice();
    const std::uint32_t             physical_device_index            = selection->physicalDeviceIndex();
    const VkDevice                  device                           = logical_device->device();
    const VkQueue                   queue                            = logical_device->queue();
    const std::uint32_t             queue_family                     = logical_device->queueFamilyIndex();
    const std::uint32_t             queue_index                      = logical_device->queueIndex();
    const VkExtent2D                drawable_extent                  = *request.mDrawableExtent;

    constexpr VulkanTextureUploadDestinationDescription    canonical_destination     = vulkanTextureUploadDestinationDescription();
    constexpr VulkanTextureUploadSampleBindingDescription  canonical_binding         = vulkanTextureUploadSampleBindingDescription();
    constexpr VulkanTextureUploadSamplePipelineDescription canonical_sample_pipeline = vulkanTextureUploadSamplePipelineDescription();
    const VkImage        texture_destination_image = original_texture_destination ? original_texture_destination->image() : VK_NULL_HANDLE;
    const VkDeviceMemory texture_destination_memory =
        original_texture_destination ? original_texture_destination->memory() : VK_NULL_HANDLE;
    const VkImageView texture_destination_view = original_texture_destination ? original_texture_destination->imageView() : VK_NULL_HANDLE;
    const VkImageSubresourceRange texture_destination_view_range =
        original_texture_destination ? original_texture_destination->viewRange() : VkImageSubresourceRange{};
    const bool          texture_destination_resident = original_texture_destination && original_texture_destination->isResident();
    const std::uint64_t texture_resident_revision    = original_texture_destination ? original_texture_destination->residentRevision() : 0;
    const std::uint64_t texture_resident_identity =
        original_texture_destination ? original_texture_destination->residentContentIdentity() : 0;
    const LLRenderContract::ImageState texture_destination_state =
        original_texture_destination ? original_texture_destination->currentState() : LLRenderContract::ImageState::Undefined;
    const VkSampler             texture_sampler = original_sample_binding ? original_sample_binding->sampler() : VK_NULL_HANDLE;
    const VkDescriptorSetLayout texture_descriptor_set_layout =
        original_sample_binding ? original_sample_binding->descriptorSetLayout() : VK_NULL_HANDLE;
    const VkPipelineLayout texture_pipeline_layout = original_sample_binding ? original_sample_binding->pipelineLayout() : VK_NULL_HANDLE;
    const VkDescriptorPool texture_descriptor_pool = original_sample_binding ? original_sample_binding->descriptorPool() : VK_NULL_HANDLE;
    const VkDescriptorSet  texture_descriptor_set  = original_sample_binding ? original_sample_binding->descriptorSet() : VK_NULL_HANDLE;

    const auto texture_branch_still_live = [&]() noexcept
    {
        if (!original_sample_binding)
        {
            return instance_generation.mTextureUploadSampleBindingEpoch == original_sample_binding_epoch &&
                   !instance_generation.mTextureUploadSampleBindingGeneration;
        }
        if (instance_generation.mTextureUploadDestinationEpoch != original_texture_destination_epoch ||
            instance_generation.mTextureUploadSampleBindingEpoch != original_sample_binding_epoch ||
            instance_generation.mTextureUploadDestinationGeneration.get() != original_texture_destination ||
            instance_generation.mTextureUploadSampleBindingGeneration.get() != original_sample_binding || !original_texture_destination ||
            !original_sample_binding)
        {
            return false;
        }
        const VkImageSubresourceRange view_range = original_texture_destination->viewRange();
        return original_texture_destination->createdFor(*selection, *logical_device) &&
               original_texture_destination->matchesDescription(canonical_destination) &&
               original_texture_destination->resourceHandle() == canonical_destination.mHandle &&
               original_texture_destination->expectedRevision() == LLRenderContract::TEXTURE_UPLOAD_REVISION &&
               original_texture_destination->mipLevels() == canonical_destination.mMipLevels && canonical_destination.mMipLevels == 3 &&
               original_texture_destination->arrayLayers() == canonical_destination.mArrayLayers &&
               original_texture_destination->imageViewType() == VK_IMAGE_VIEW_TYPE_2D &&
               original_texture_destination->image() == texture_destination_image && texture_destination_image != VK_NULL_HANDLE &&
               original_texture_destination->memory() == texture_destination_memory && texture_destination_memory != VK_NULL_HANDLE &&
               original_texture_destination->imageView() == texture_destination_view && texture_destination_view != VK_NULL_HANDLE &&
               view_range.aspectMask == texture_destination_view_range.aspectMask &&
               view_range.baseMipLevel == texture_destination_view_range.baseMipLevel &&
               view_range.levelCount == texture_destination_view_range.levelCount &&
               view_range.baseArrayLayer == texture_destination_view_range.baseArrayLayer &&
               view_range.layerCount == texture_destination_view_range.layerCount &&
               original_texture_destination->isResident() == texture_destination_resident && texture_destination_resident &&
               original_texture_destination->residentRevision() == texture_resident_revision &&
               texture_resident_revision == LLRenderContract::TEXTURE_UPLOAD_REVISION &&
               original_texture_destination->residentContentIdentity() == texture_resident_identity && texture_resident_identity != 0 &&
               original_texture_destination->currentState() == texture_destination_state &&
               texture_destination_state == LLRenderContract::ImageState::ShaderRead &&
               original_sample_binding->createdFor(*selection, *logical_device, *original_texture_destination) &&
               original_sample_binding->matchesDescription(canonical_binding) &&
               original_sample_binding->retainsTextureUploadDestinationGeneration(*original_texture_destination) &&
               original_sample_binding->residentRevision() == texture_resident_revision &&
               original_sample_binding->residentContentIdentity() == texture_resident_identity &&
               original_sample_binding->destinationImageView() == texture_destination_view &&
               original_sample_binding->destinationImageLayout() == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
               original_sample_binding->sampler() == texture_sampler && texture_sampler != VK_NULL_HANDLE &&
               original_sample_binding->descriptorSetLayout() == texture_descriptor_set_layout &&
               texture_descriptor_set_layout != VK_NULL_HANDLE && original_sample_binding->pipelineLayout() == texture_pipeline_layout &&
               texture_pipeline_layout != VK_NULL_HANDLE && original_sample_binding->descriptorPool() == texture_descriptor_pool &&
               texture_descriptor_pool != VK_NULL_HANDLE && original_sample_binding->descriptorSet() == texture_descriptor_set &&
               texture_descriptor_set != VK_NULL_HANDLE;
    };

    const auto parents_still_live = [&]() noexcept
    {
        return instance_generation.mGlobalDispatch && &*instance_generation.mGlobalDispatch == global_dispatch &&
               instance_generation.mGlobalDispatch->getInstanceProcAddr() == get_instance_proc_addr &&
               instance_generation.mInstance == instance && instance_generation.mDestroyInstance &&
               instance_generation.mNativeWindowGeneration == native_window_generation &&
               instance_generation.mSurfaceGeneration.get() == surface_generation && surface_generation->surface() == surface &&
               surface_generation->nativeWindowGeneration() == surface_native_window_generation &&
               instance_generation.mPresentationDeviceGeneration.get() == selection &&
               selection->getInstanceProcAddr() == get_instance_proc_addr && selection->physicalDevice() == physical_device &&
               selection->physicalDeviceIndex() == physical_device_index && selection->selectedFor(instance, surface) &&
               instance_generation.mLogicalDeviceGeneration.get() == logical_device &&
               logical_device->getInstanceProcAddr() == get_instance_proc_addr && logical_device->device() == device &&
               logical_device->queue() == queue && logical_device->queueFamilyIndex() == queue_family &&
               logical_device->queueIndex() == queue_index && logical_device->createdFor(*selection) && texture_branch_still_live();
    };

    const auto chain_is_authentic = [&]() noexcept
    {
        const auto* configuration = instance_generation.mSwapchainConfigurationGeneration.get();
        const auto* swapchain     = instance_generation.mSwapchainGeneration.get();
        const auto* images                = instance_generation.mSwapchainImagesGeneration.get();
        const auto* presentation_target   = instance_generation.mSwapchainPresentationTargetGeneration.get();
        const auto* presentation_pipeline = instance_generation.mSwapchainPresentationPipelineGeneration.get();
        const auto* sample_pipeline       = instance_generation.mTextureUploadSamplePipelineGeneration.get();
        const auto* readback              = instance_generation.mSwapchainReadbackGeneration.get();
        const auto* frame_slot            = instance_generation.mSwapchainFrameSlotGeneration.get();

        if ((swapchain && !configuration) || (images && !swapchain) || (presentation_target && !images) ||
            (presentation_pipeline && !presentation_target) ||
            (sample_pipeline && (!original_texture_destination || !original_sample_binding || !configuration || !swapchain || !images ||
                                 !presentation_target)) ||
            (readback && !images) || (frame_slot && !images))
        {
            return false;
        }
        if (configuration && !configuration->createdFor(*selection, *logical_device, configuration->drawableExtent()))
        {
            return false;
        }
        if (swapchain && !swapchain->createdFor(*logical_device, *configuration))
        {
            return false;
        }
        if (images && !images->createdFor(*logical_device, *configuration, *swapchain))
        {
            return false;
        }
        if (presentation_target &&
            !presentation_target->createdFor(*logical_device, *configuration, *swapchain, *images))
        {
            return false;
        }
        if (presentation_pipeline &&
            !presentation_pipeline->createdFor(
                *logical_device, *configuration, *swapchain, *images, *presentation_target))
        {
            return false;
        }
        if (sample_pipeline &&
            (!sample_pipeline->createdFor(*selection,
                                          *logical_device,
                                          *original_texture_destination,
                                          *original_sample_binding,
                                          *configuration,
                                          *swapchain,
                                          *images,
                                          *presentation_target) ||
             !sample_pipeline->matchesDescription(canonical_sample_pipeline) ||
             !sample_pipeline->retainsTextureUploadSampleBindingGeneration(*original_sample_binding) ||
             sample_pipeline->pipelineLayout() != texture_pipeline_layout || sample_pipeline->pipeline() == VK_NULL_HANDLE))
        {
            return false;
        }
        if (readback && !readback->createdFor(*selection, *logical_device, *configuration, *swapchain, *images))
        {
            return false;
        }
        return !frame_slot || frame_slot->createdFor(*logical_device, *configuration, *swapchain, *images);
    };

    const auto reset_refusal = [&](VulkanSwapchainChainRebuildPhase      phase,
                                   VulkanSwapchainChainRebuildChildError child_error)
        -> std::optional<VulkanSwapchainChainRebuildError>
    {
        if (!instance_generation.mSwapchainFrameSlotGeneration)
        {
            return std::nullopt;
        }
        const VulkanSwapchainFrameSlotDisposition disposition =
            instance_generation.mSwapchainFrameSlotGeneration->disposition();
        if (disposition == VulkanSwapchainFrameSlotDisposition::DeviceLost)
        {
            return swapchainChainRebuildFailure(
                VulkanSwapchainChainRebuildCode::DeviceRecoveryRequired, phase, std::move(child_error), disposition);
        }
        if (!frameSlotDispositionAllowsReset(disposition))
        {
            return swapchainChainRebuildFailure(
                VulkanSwapchainChainRebuildCode::FrameSlotResetRefused, phase, std::move(child_error), disposition);
        }
        return std::nullopt;
    };

    if (!parents_still_live())
    {
        return swapchainChainRebuildFailure(VulkanSwapchainChainRebuildCode::PublicationFailure, preflight);
    }
    if (!chain_is_authentic())
    {
        return swapchainChainRebuildFailure(VulkanSwapchainChainRebuildCode::PublicationFailure, preflight);
    }
    if (auto error = reset_refusal(preflight, {}))
    {
        return *error;
    }

    const auto original_children_still_published = [&]() noexcept
    {
        return instance_generation.mSwapchainConfigurationGeneration.get() == original_configuration &&
               instance_generation.mSwapchainGeneration.get() == original_swapchain &&
               instance_generation.mSwapchainImagesGeneration.get() == original_images &&
               instance_generation.mSwapchainPresentationTargetGeneration.get() == original_presentation_target &&
               instance_generation.mSwapchainPresentationPipelineGeneration.get() == original_presentation_pipeline &&
               instance_generation.mTextureUploadSamplePipelineGeneration.get() == original_sample_pipeline &&
               instance_generation.mTextureUploadSamplePipelineEpoch == original_sample_pipeline_epoch &&
               instance_generation.mSwapchainReadbackGeneration.get() == original_readback &&
               instance_generation.mSwapchainFrameSlotGeneration.get() == original_frame_slot;
    };

    const bool instance_owner_current =
        request.mInstanceOwnerCheck.mIsCurrent(request.mInstanceOwnerCheck.mUserdata, instance_generation);
    if (auto error = reset_refusal(preflight, {}))
    {
        return *error;
    }
    if (instance_generation.mOwnershipTransitionEpoch != original_ownership_epoch)
    {
        return swapchainChainRebuildFailure(VulkanSwapchainChainRebuildCode::PublicationFailure, preflight);
    }
    if (!instance_owner_current)
    {
        return swapchainChainRebuildFailure(VulkanSwapchainChainRebuildCode::StaleInstanceOwner, preflight);
    }
    if (!parents_still_live() || !chain_is_authentic() || !original_children_still_published())
    {
        return swapchainChainRebuildFailure(VulkanSwapchainChainRebuildCode::PublicationFailure, preflight);
    }

    const bool window_generation_current = current(request.mWindowGenerationCheck, request.mNativeWindowGeneration);
    if (auto error = reset_refusal(preflight, {}))
    {
        return *error;
    }
    if (instance_generation.mOwnershipTransitionEpoch != original_ownership_epoch)
    {
        return swapchainChainRebuildFailure(VulkanSwapchainChainRebuildCode::PublicationFailure, preflight);
    }
    if (!window_generation_current)
    {
        return swapchainChainRebuildFailure(VulkanSwapchainChainRebuildCode::StaleWindowGeneration, preflight);
    }
    if (!parents_still_live() || !chain_is_authentic() || !original_children_still_published())
    {
        return swapchainChainRebuildFailure(VulkanSwapchainChainRebuildCode::PublicationFailure, preflight);
    }
    if (auto error = reset_refusal(preflight, {}))
    {
        return *error;
    }

    if (!instance_generation.resetSwapchainConfigurationGeneration())
    {
        const std::optional<VulkanSwapchainFrameSlotDisposition> disposition = instance_generation.swapchainFrameSlotDisposition();
        const VulkanSwapchainChainRebuildCode                    code = disposition == VulkanSwapchainFrameSlotDisposition::DeviceLost
                                                                            ? VulkanSwapchainChainRebuildCode::DeviceRecoveryRequired
                                                                            : VulkanSwapchainChainRebuildCode::FrameSlotResetRefused;
        return swapchainChainRebuildFailure(code, VulkanSwapchainChainRebuildPhase::Retirement, {}, disposition);
    }

    const std::uint64_t retired_sample_pipeline_epoch  = expected_retired_sample_pipeline_epoch;
    const auto          sample_pipeline_remains_absent = [&]() noexcept
    {
        return !instance_generation.mTextureUploadSamplePipelineGeneration &&
               instance_generation.mTextureUploadSamplePipelineEpoch == retired_sample_pipeline_epoch;
    };

    const auto no_swapchain_children = [&]() noexcept
    {
        return !instance_generation.mSwapchainConfigurationGeneration && !instance_generation.mSwapchainGeneration &&
               !instance_generation.mSwapchainImagesGeneration && !instance_generation.mSwapchainPresentationTargetGeneration &&
               !instance_generation.mSwapchainPresentationPipelineGeneration &&
               !instance_generation.mTextureUploadSamplePipelineGeneration && !instance_generation.mSwapchainReadbackGeneration &&
               !instance_generation.mSwapchainFrameSlotGeneration;
    };
    const auto rollback_children = [&]() noexcept
    {
        const bool          had_sample_pipeline = instance_generation.mTextureUploadSamplePipelineGeneration != nullptr;
        const std::uint64_t sample_epoch        = instance_generation.mTextureUploadSamplePipelineEpoch;
        if (!instance_generation.resetSwapchainConfigurationGeneration())
        {
            return false;
        }
        const std::uint64_t expected_sample_epoch      = sample_epoch + (had_sample_pipeline ? 1u : 0u);
        const bool          sample_transition_is_exact = instance_generation.mTextureUploadSamplePipelineEpoch == expected_sample_epoch;
        return no_swapchain_children() && texture_branch_still_live() && sample_transition_is_exact;
    };
    const auto rollback_or_report = [&](VulkanSwapchainChainRebuildError error)
    {
        if (auto refusal = reset_refusal(error.mPhase, error.mChildError))
        {
            return *refusal;
        }
        if (rollback_children())
        {
            return error;
        }
        if (auto refusal = reset_refusal(error.mPhase, error.mChildError))
        {
            return *refusal;
        }
        return swapchainChainRebuildFailure(VulkanSwapchainChainRebuildCode::RollbackFailure,
                                            error.mPhase,
                                            std::move(error.mChildError),
                                            instance_generation.swapchainFrameSlotDisposition());
    };
    const auto final_freshness = [&]() -> std::optional<VulkanSwapchainChainRebuildError>
    {
        const std::uint64_t owner_callback_epoch = instance_generation.mOwnershipTransitionEpoch;
        const bool owner_current =
            request.mInstanceOwnerCheck.mIsCurrent(request.mInstanceOwnerCheck.mUserdata, instance_generation);
        if (auto error = reset_refusal(VulkanSwapchainChainRebuildPhase::FinalFreshness, {}))
        {
            return *error;
        }
        if (instance_generation.mOwnershipTransitionEpoch != owner_callback_epoch)
        {
            return swapchainChainRebuildFailure(VulkanSwapchainChainRebuildCode::PublicationFailure,
                                                VulkanSwapchainChainRebuildPhase::FinalFreshness);
        }
        if (!owner_current)
        {
            return swapchainChainRebuildFailure(VulkanSwapchainChainRebuildCode::StaleInstanceOwner,
                                                VulkanSwapchainChainRebuildPhase::FinalFreshness);
        }

        const std::uint64_t window_callback_epoch = instance_generation.mOwnershipTransitionEpoch;
        const bool window_current = current(request.mWindowGenerationCheck, request.mNativeWindowGeneration);
        if (auto error = reset_refusal(VulkanSwapchainChainRebuildPhase::FinalFreshness, {}))
        {
            return *error;
        }
        if (instance_generation.mOwnershipTransitionEpoch != window_callback_epoch)
        {
            return swapchainChainRebuildFailure(VulkanSwapchainChainRebuildCode::PublicationFailure,
                                                VulkanSwapchainChainRebuildPhase::FinalFreshness);
        }
        if (!window_current)
        {
            return swapchainChainRebuildFailure(VulkanSwapchainChainRebuildCode::StaleWindowGeneration,
                                                VulkanSwapchainChainRebuildPhase::FinalFreshness);
        }
        return std::nullopt;
    };
    const auto publication_failure = [&](VulkanSwapchainChainRebuildPhase phase)
    {
        return rollback_or_report(
            swapchainChainRebuildFailure(VulkanSwapchainChainRebuildCode::PublicationFailure, phase));
    };

    if (!parents_still_live() || !no_swapchain_children() || !sample_pipeline_remains_absent())
    {
        return publication_failure(VulkanSwapchainChainRebuildPhase::Retirement);
    }

    if (drawable_extent.width == 0 || drawable_extent.height == 0)
    {
        if (auto error = final_freshness())
        {
            return rollback_or_report(*error);
        }
        if (!parents_still_live() || !no_swapchain_children() || !sample_pipeline_remains_absent())
        {
            return publication_failure(VulkanSwapchainChainRebuildPhase::FinalFreshness);
        }
        return VulkanSwapchainChainRebuildOutcome::Suspended;
    }

    const VulkanSwapchainConfigurationRequest configuration_request{ request.mNativeWindowGeneration, drawable_extent,
                                                                     request.mInstanceOwnerCheck,
                                                                     request.mWindowGenerationCheck };
    if (VulkanSwapchainConfigurationAcquireResult error =
            acquireSwapchainConfiguration(instance_generation, configuration_request, allocation_checkpoint))
    {
        return rollback_or_report(swapchainChainRebuildFailure(VulkanSwapchainChainRebuildCode::ChildFailure,
                                                               VulkanSwapchainChainRebuildPhase::Configuration,
                                                               VulkanSwapchainChainRebuildChildError{ *error }));
    }
    if (!parents_still_live() || !chain_is_authentic() || !sample_pipeline_remains_absent())
    {
        return publication_failure(VulkanSwapchainChainRebuildPhase::Configuration);
    }

    const VulkanSwapchainRequest swapchain_request{ request.mNativeWindowGeneration, drawable_extent,
                                                    request.mInstanceOwnerCheck, request.mWindowGenerationCheck };
    if (VulkanSwapchainAcquireResult error = acquireSwapchain(instance_generation, swapchain_request, allocation_checkpoint))
    {
        return rollback_or_report(swapchainChainRebuildFailure(VulkanSwapchainChainRebuildCode::ChildFailure,
                                                               VulkanSwapchainChainRebuildPhase::Swapchain,
                                                               VulkanSwapchainChainRebuildChildError{ *error }));
    }
    if (!parents_still_live() || !chain_is_authentic() || !sample_pipeline_remains_absent())
    {
        return publication_failure(VulkanSwapchainChainRebuildPhase::Swapchain);
    }

    const VulkanSwapchainImagesRequest images_request{ request.mNativeWindowGeneration, drawable_extent,
                                                       request.mInstanceOwnerCheck, request.mWindowGenerationCheck };
    if (VulkanSwapchainImagesAcquireResult error = acquireSwapchainImages(instance_generation, images_request, allocation_checkpoint))
    {
        return rollback_or_report(swapchainChainRebuildFailure(VulkanSwapchainChainRebuildCode::ChildFailure,
                                                               VulkanSwapchainChainRebuildPhase::Images,
                                                               VulkanSwapchainChainRebuildChildError{ *error }));
    }
    if (!parents_still_live() || !chain_is_authentic() || !sample_pipeline_remains_absent())
    {
        return publication_failure(VulkanSwapchainChainRebuildPhase::Images);
    }

    const VulkanSwapchainPresentationTargetRequest presentation_target_request{ request.mNativeWindowGeneration, drawable_extent,
                                                                                request.mInstanceOwnerCheck,
                                                                                request.mWindowGenerationCheck };
    if (VulkanSwapchainPresentationTargetAcquireResult error =
            acquireSwapchainPresentationTarget(instance_generation, presentation_target_request, allocation_checkpoint))
    {
        return rollback_or_report(swapchainChainRebuildFailure(VulkanSwapchainChainRebuildCode::ChildFailure,
                                                               VulkanSwapchainChainRebuildPhase::PresentationTarget,
                                                               VulkanSwapchainChainRebuildChildError{ *error }));
    }
    if (!parents_still_live() || !chain_is_authentic() || !sample_pipeline_remains_absent())
    {
        return publication_failure(VulkanSwapchainChainRebuildPhase::PresentationTarget);
    }

    const VulkanSwapchainPresentationPipelineRequest presentation_pipeline_request{ request.mNativeWindowGeneration, drawable_extent,
                                                                                    request.mInstanceOwnerCheck,
                                                                                    request.mWindowGenerationCheck };
    if (VulkanSwapchainPresentationPipelineAcquireResult error =
            acquireSwapchainPresentationPipeline(instance_generation, presentation_pipeline_request, allocation_checkpoint))
    {
        return rollback_or_report(swapchainChainRebuildFailure(VulkanSwapchainChainRebuildCode::ChildFailure,
                                                               VulkanSwapchainChainRebuildPhase::PresentationPipeline,
                                                               VulkanSwapchainChainRebuildChildError{ *error }));
    }
    if (!parents_still_live() || !chain_is_authentic() || !sample_pipeline_remains_absent())
    {
        return publication_failure(VulkanSwapchainChainRebuildPhase::PresentationPipeline);
    }

    const VulkanSwapchainReadbackRequest readback_request{ request.mNativeWindowGeneration, drawable_extent, request.mInstanceOwnerCheck,
                                                           request.mWindowGenerationCheck };
    if (VulkanSwapchainReadbackAcquireResult error = acquireSwapchainReadback(instance_generation, readback_request, allocation_checkpoint))
    {
        return rollback_or_report(swapchainChainRebuildFailure(VulkanSwapchainChainRebuildCode::ChildFailure,
                                                               VulkanSwapchainChainRebuildPhase::Readback,
                                                               VulkanSwapchainChainRebuildChildError{ *error }));
    }
    if (!parents_still_live() || !chain_is_authentic() || !sample_pipeline_remains_absent())
    {
        return publication_failure(VulkanSwapchainChainRebuildPhase::Readback);
    }

    const VulkanSwapchainFrameSlotRequest frame_slot_request{ request.mNativeWindowGeneration, drawable_extent, request.mInstanceOwnerCheck,
                                                              request.mWindowGenerationCheck };
    if (VulkanSwapchainFrameSlotAcquireResult error =
            acquireSwapchainFrameSlot(instance_generation, frame_slot_request, allocation_checkpoint))
    {
        return rollback_or_report(swapchainChainRebuildFailure(VulkanSwapchainChainRebuildCode::ChildFailure,
                                                               VulkanSwapchainChainRebuildPhase::FrameSlot,
                                                               VulkanSwapchainChainRebuildChildError{ *error }));
    }
    if (!parents_still_live() || !chain_is_authentic() || !sample_pipeline_remains_absent())
    {
        return publication_failure(VulkanSwapchainChainRebuildPhase::FrameSlot);
    }

    if (auto error = final_freshness())
    {
        return rollback_or_report(*error);
    }
    if (auto error = reset_refusal(VulkanSwapchainChainRebuildPhase::FinalFreshness, {}))
    {
        return *error;
    }

    const auto* configuration = instance_generation.mSwapchainConfigurationGeneration.get();
    const auto* swapchain     = instance_generation.mSwapchainGeneration.get();
    const auto* images                = instance_generation.mSwapchainImagesGeneration.get();
    const auto* presentation_target   = instance_generation.mSwapchainPresentationTargetGeneration.get();
    const auto* presentation_pipeline = instance_generation.mSwapchainPresentationPipelineGeneration.get();
    const auto* readback              = instance_generation.mSwapchainReadbackGeneration.get();
    const auto* frame_slot            = instance_generation.mSwapchainFrameSlotGeneration.get();
    if (!parents_still_live() || !sample_pipeline_remains_absent() || !configuration || !swapchain || !images || !presentation_target ||
        !presentation_pipeline || !readback || !frame_slot || !configuration->createdFor(*selection, *logical_device, drawable_extent) ||
        !swapchain->createdFor(*logical_device, *configuration) || !images->createdFor(*logical_device, *configuration, *swapchain) ||
        !presentation_target->createdFor(*logical_device, *configuration, *swapchain, *images) ||
        !presentation_pipeline->createdFor(*logical_device, *configuration, *swapchain, *images, *presentation_target) ||
        !readback->createdFor(*selection, *logical_device, *configuration, *swapchain, *images) ||
        !frame_slot->createdFor(*logical_device, *configuration, *swapchain, *images))
    {
        return publication_failure(VulkanSwapchainChainRebuildPhase::FinalFreshness);
    }

    return VulkanSwapchainChainRebuildOutcome::Ready;
}

SwapchainFrameSlotParentResult VulkanInstanceGenerationFactory::operateSwapchainFrameSlot(
    VulkanInstanceGeneration&                       instance_generation,
    const VulkanSwapchainFrameSlotOperationRequest& request,
    SwapchainFrameSlotParentOperation               operation,
    std::optional<VulkanSwapchainFrameClearColor>   clear_color) noexcept
{
    const bool render_pass_clear         = operation == SwapchainFrameSlotParentOperation::ExecuteAcquireRenderPassClearToPresent;
    const bool render_pass_draw_readback = operation == SwapchainFrameSlotParentOperation::ExecuteAcquireRenderPassDrawReadbackToPresent;
    const bool render_pass_draw =
        operation == SwapchainFrameSlotParentOperation::ExecuteAcquireRenderPassDrawToPresent || render_pass_draw_readback;
    const bool render_pass_operation       = render_pass_clear || render_pass_draw;
    const auto* current_frame_slot          = instance_generation.mSwapchainFrameSlotGeneration.get();
    const bool  upload_destination_required =
        render_pass_draw || (current_frame_slot && current_frame_slot->hasRetainedUploadDestinationGeneration());
    const bool caller_clear_color_required = operation == SwapchainFrameSlotParentOperation::ExecuteAcquireClearToPresent ||
                                             render_pass_clear ||
                                             operation == SwapchainFrameSlotParentOperation::ExecuteAcquireRenderPassDrawToPresent;
    if (caller_clear_color_required && (!clear_color || !validClearColor(*clear_color)))
    {
        return swapchainFrameSlotOperationFailure(VulkanSwapchainFrameSlotParentOperationCode::InvalidClearColor);
    }

    const auto validate_live_chain = [&]() -> std::optional<VulkanSwapchainFrameSlotParentOperationError>
    {
        if (!request.mInstanceOwnerCheck.mIsCurrent)
        {
            return swapchainFrameSlotOperationFailure(VulkanSwapchainFrameSlotParentOperationCode::InvalidInstanceOwnerCheck);
        }
        if (!request.mWindowGenerationCheck.mIsCurrent)
        {
            return swapchainFrameSlotOperationFailure(VulkanSwapchainFrameSlotParentOperationCode::InvalidWindowGenerationCheck);
        }
        if (request.mNativeWindowGeneration == 0)
        {
            return swapchainFrameSlotOperationFailure(VulkanSwapchainFrameSlotParentOperationCode::InvalidNativeWindowGeneration);
        }
        if (startsNewSwapchainFrameSlotWork(operation) && (request.mDrawableExtent.width == 0 || request.mDrawableExtent.height == 0))
        {
            return swapchainFrameSlotOperationFailure(VulkanSwapchainFrameSlotParentOperationCode::InvalidDrawableExtent);
        }
        if (instance_generation.mInstance == VK_NULL_HANDLE || !instance_generation.mDestroyInstance ||
            !instance_generation.mGlobalDispatch || !instance_generation.mGlobalDispatch->getInstanceProcAddr() ||
            instance_generation.mNativeWindowGeneration == 0)
        {
            return swapchainFrameSlotOperationFailure(VulkanSwapchainFrameSlotParentOperationCode::InstanceNotLive);
        }
        if (!instance_generation.mSurfaceGeneration || instance_generation.surface() == VK_NULL_HANDLE ||
            instance_generation.surfaceNativeWindowGeneration() == 0)
        {
            return swapchainFrameSlotOperationFailure(VulkanSwapchainFrameSlotParentOperationCode::SurfaceNotLive);
        }
        if (!instance_generation.mPresentationDeviceGeneration || instance_generation.physicalDevice() == VK_NULL_HANDLE)
        {
            return swapchainFrameSlotOperationFailure(VulkanSwapchainFrameSlotParentOperationCode::PresentationDeviceNotLive);
        }
        if (!instance_generation.mLogicalDeviceGeneration || instance_generation.logicalDevice() == VK_NULL_HANDLE ||
            instance_generation.presentationQueue() == VK_NULL_HANDLE)
        {
            return swapchainFrameSlotOperationFailure(VulkanSwapchainFrameSlotParentOperationCode::LogicalDeviceNotLive);
        }
        if (!instance_generation.mSwapchainConfigurationGeneration)
        {
            return swapchainFrameSlotOperationFailure(VulkanSwapchainFrameSlotParentOperationCode::SwapchainConfigurationNotLive);
        }
        if (!instance_generation.mSwapchainGeneration || instance_generation.swapchain() == VK_NULL_HANDLE)
        {
            return swapchainFrameSlotOperationFailure(VulkanSwapchainFrameSlotParentOperationCode::SwapchainNotLive);
        }
        if (!instance_generation.mSwapchainImagesGeneration || instance_generation.resolvedSwapchainImageCount() == 0 ||
            instance_generation.mSwapchainImagesGeneration->imageFormat() == VK_FORMAT_UNDEFINED)
        {
            return swapchainFrameSlotOperationFailure(VulkanSwapchainFrameSlotParentOperationCode::SwapchainImagesNotLive);
        }
        if (render_pass_draw_readback)
        {
            const auto* readback = instance_generation.mSwapchainReadbackGeneration.get();
            if (!readback || readback->buffer() == VK_NULL_HANDLE || readback->memory() == VK_NULL_HANDLE || !readback->isMapped() ||
                !readback->createdFor(*instance_generation.mPresentationDeviceGeneration,
                                      *instance_generation.mLogicalDeviceGeneration,
                                      *instance_generation.mSwapchainConfigurationGeneration,
                                      *instance_generation.mSwapchainGeneration,
                                      *instance_generation.mSwapchainImagesGeneration))
            {
                return swapchainFrameSlotOperationFailure(VulkanSwapchainFrameSlotParentOperationCode::SwapchainReadbackNotLive);
            }
        }
        if (render_pass_operation)
        {
            const auto* target = instance_generation.mSwapchainPresentationTargetGeneration.get();
            const VkExtent2D image_extent = instance_generation.swapchainImageExtent();
            if (!target || target->renderPass() == VK_NULL_HANDLE ||
                target->framebufferCount() != instance_generation.resolvedSwapchainImageCount() ||
                target->imageFormat() != instance_generation.mSwapchainImagesGeneration->imageFormat() ||
                target->imageExtent().width != image_extent.width || target->imageExtent().height != image_extent.height ||
                image_extent.width == 0 || image_extent.height == 0 ||
                !target->createdFor(*instance_generation.mLogicalDeviceGeneration,
                                    *instance_generation.mSwapchainConfigurationGeneration,
                                    *instance_generation.mSwapchainGeneration,
                                    *instance_generation.mSwapchainImagesGeneration))
            {
                return swapchainFrameSlotOperationFailure(
                    VulkanSwapchainFrameSlotParentOperationCode::SwapchainPresentationTargetNotLive);
            }
            for (std::uint32_t index = 0; index < target->framebufferCount(); ++index)
            {
                if (target->framebuffer(index) == VK_NULL_HANDLE)
                {
                    return swapchainFrameSlotOperationFailure(
                        VulkanSwapchainFrameSlotParentOperationCode::SwapchainPresentationTargetNotLive);
                }
            }
        }
        if (render_pass_draw)
        {
            const auto* pipeline = instance_generation.mSwapchainPresentationPipelineGeneration.get();
            if (!pipeline || pipeline->pipelineLayout() == VK_NULL_HANDLE || pipeline->pipeline() == VK_NULL_HANDLE ||
                !pipeline->createdFor(*instance_generation.mLogicalDeviceGeneration,
                                      *instance_generation.mSwapchainConfigurationGeneration,
                                      *instance_generation.mSwapchainGeneration,
                                      *instance_generation.mSwapchainImagesGeneration,
                                      *instance_generation.mSwapchainPresentationTargetGeneration))
            {
                return swapchainFrameSlotOperationFailure(
                    VulkanSwapchainFrameSlotParentOperationCode::SwapchainPresentationPipelineNotLive);
            }
        }
        if (!instance_generation.mSwapchainFrameSlotGeneration ||
            instance_generation.mSwapchainFrameSlotGeneration->commandPool() == VK_NULL_HANDLE ||
            instance_generation.mSwapchainFrameSlotGeneration->commandBuffer() == VK_NULL_HANDLE ||
            instance_generation.mSwapchainFrameSlotGeneration->imageAvailableSemaphore() == VK_NULL_HANDLE ||
            instance_generation.mSwapchainFrameSlotGeneration->presentationReadySemaphore() == VK_NULL_HANDLE ||
            instance_generation.mSwapchainFrameSlotGeneration->submissionFence() == VK_NULL_HANDLE ||
            instance_generation.mSwapchainFrameSlotGeneration->presentCompletionFence() == VK_NULL_HANDLE)
        {
            return swapchainFrameSlotOperationFailure(VulkanSwapchainFrameSlotParentOperationCode::SwapchainFrameSlotNotLive);
        }
        if (upload_destination_required)
        {
            const auto* destination     = instance_generation.mUploadDestinationGeneration.get();
            const auto* live_frame_slot = instance_generation.mSwapchainFrameSlotGeneration.get();
            if (!destination || !destination->matchesDescription(SCREEN_TRIANGLE_DESCRIPTION) ||
                destination->resourceHandle() != SCREEN_TRIANGLE_HANDLE ||
                destination->expectedContentIdentity() != LLRenderContract::SCREEN_TRIANGLE_CONTENT_IDENTITY ||
                destination->residentContentIdentity() != destination->expectedContentIdentity() || !destination->isResident() ||
                destination->buffer() == VK_NULL_HANDLE || destination->memory() == VK_NULL_HANDLE ||
                destination->byteCount() != VULKAN_UPLOAD_SOURCE_BYTE_COUNT ||
                destination->usage() != (VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) ||
                destination->allocationSize() < destination->byteCount() ||
                (destination->memoryPropertyFlags() & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == 0 || !destination->isDeviceLocal() ||
                destination->isMapped() ||
                !destination->createdFor(*instance_generation.mPresentationDeviceGeneration,
                                         *instance_generation.mLogicalDeviceGeneration) ||
                (live_frame_slot && live_frame_slot->hasRetainedUploadDestinationGeneration() &&
                 (!live_frame_slot->retainsUploadDestinationGeneration(*destination) ||
                  !live_frame_slot->activeUploadDestinationMatchesSnapshot())))
            {
                return swapchainFrameSlotOperationFailure(VulkanSwapchainFrameSlotParentOperationCode::UploadDestinationNotLive);
            }
        }
        if (request.mNativeWindowGeneration != instance_generation.mNativeWindowGeneration ||
            request.mNativeWindowGeneration != instance_generation.surfaceNativeWindowGeneration())
        {
            return swapchainFrameSlotOperationFailure(VulkanSwapchainFrameSlotParentOperationCode::NativeWindowGenerationMismatch);
        }
        const VkExtent2D configured_extent = instance_generation.swapchainDrawableExtent();
        if (startsNewSwapchainFrameSlotWork(operation) &&
            (request.mDrawableExtent.width != configured_extent.width || request.mDrawableExtent.height != configured_extent.height))
        {
            return swapchainFrameSlotOperationFailure(VulkanSwapchainFrameSlotParentOperationCode::DrawableExtentMismatch);
        }
        return std::nullopt;
    };

    if (auto error = validate_live_chain())
    {
        return *error;
    }
    if (auto error = swapchainFrameSlotOperationFreshness(request, instance_generation))
    {
        return *error;
    }
    if (auto error = validate_live_chain())
    {
        return *error;
    }

    const auto* global_dispatch      = &*instance_generation.mGlobalDispatch;
    const auto* surface_generation   = instance_generation.mSurfaceGeneration.get();
    const auto* selection            = instance_generation.mPresentationDeviceGeneration.get();
    const auto* logical_device       = instance_generation.mLogicalDeviceGeneration.get();
    const auto* configuration        = instance_generation.mSwapchainConfigurationGeneration.get();
    const auto* swapchain_generation = instance_generation.mSwapchainGeneration.get();
    const auto* images_generation    = instance_generation.mSwapchainImagesGeneration.get();
    const auto* presentation_target   = render_pass_operation ? instance_generation.mSwapchainPresentationTargetGeneration.get() : nullptr;
    const auto* presentation_pipeline = render_pass_draw ? instance_generation.mSwapchainPresentationPipelineGeneration.get() : nullptr;
    const auto* upload_destination    = upload_destination_required ? instance_generation.mUploadDestinationGeneration.get() : nullptr;
    auto*       readback            = instance_generation.mSwapchainReadbackGeneration.get();
    auto*       frame_slot          = instance_generation.mSwapchainFrameSlotGeneration.get();
    const bool  readback_required   = render_pass_draw_readback || (readback && frame_slot->retainsReadbackGeneration(*readback));

    const PFN_vkGetInstanceProcAddr get_instance_proc_addr           = global_dispatch->getInstanceProcAddr();
    const VkInstance                instance                         = instance_generation.mInstance;
    const std::uint64_t             native_window_generation         = instance_generation.mNativeWindowGeneration;
    const std::uint64_t             surface_native_window_generation = surface_generation->nativeWindowGeneration();
    const VkSurfaceKHR              surface                          = surface_generation->surface();
    const VkPhysicalDevice          physical_device                  = selection->physicalDevice();
    const std::uint32_t             physical_device_index            = selection->physicalDeviceIndex();
    const VkDevice                  device                           = logical_device->device();
    const VkQueue                   queue                            = logical_device->queue();
    const std::uint32_t             queue_family                     = logical_device->queueFamilyIndex();
    const std::uint32_t             queue_index                      = logical_device->queueIndex();
    const VkExtent2D                drawable_extent                  = configuration->drawableExtent();
    const VkSwapchainKHR            swapchain                        = swapchain_generation->swapchain();
    const std::uint32_t             image_count                      = images_generation->imageCount();
    const VkFormat                  image_format                     = images_generation->imageFormat();
    const std::uint64_t             presentation_target_epoch        = instance_generation.mSwapchainPresentationTargetEpoch;
    const std::uint64_t             presentation_pipeline_epoch      = instance_generation.mSwapchainPresentationPipelineEpoch;
    const std::uint64_t                  upload_destination_epoch         = instance_generation.mUploadDestinationEpoch;
    const std::uint64_t             readback_epoch                   = instance_generation.mSwapchainReadbackEpoch;
    const std::uint64_t             frame_slot_epoch                 = instance_generation.mSwapchainFrameSlotEpoch;
    const VkRenderPass              render_pass = presentation_target ? presentation_target->renderPass() : VK_NULL_HANDLE;
    const std::uint32_t             framebuffer_count = presentation_target ? presentation_target->framebufferCount() : 0;
    const VkFormat                  target_image_format = presentation_target ? presentation_target->imageFormat() : VK_FORMAT_UNDEFINED;
    const VkExtent2D                target_image_extent = presentation_target ? presentation_target->imageExtent() : VkExtent2D{};
    const VkPipelineLayout          pipeline_layout     = presentation_pipeline ? presentation_pipeline->pipelineLayout() : VK_NULL_HANDLE;
    const VkPipeline                pipeline            = presentation_pipeline ? presentation_pipeline->pipeline() : VK_NULL_HANDLE;
    const LLRenderContract::BufferHandle upload_destination_handle =
        upload_destination_required ? upload_destination->resourceHandle() : LLRenderContract::BufferHandle{};
    const std::uint64_t upload_destination_expected_identity =
        upload_destination_required ? upload_destination->expectedContentIdentity() : 0;
    const std::uint64_t upload_destination_resident_identity =
        upload_destination_required ? upload_destination->residentContentIdentity() : 0;
    const VkBuffer           upload_destination_buffer     = upload_destination_required ? upload_destination->buffer() : VK_NULL_HANDLE;
    const VkDeviceMemory     upload_destination_memory     = upload_destination_required ? upload_destination->memory() : VK_NULL_HANDLE;
    const VkDeviceSize       upload_destination_byte_count = upload_destination_required ? upload_destination->byteCount() : 0;
    const VkBufferUsageFlags upload_destination_usage      = upload_destination_required ? upload_destination->usage() : 0;
    const VkDeviceSize       upload_destination_allocation_size   = upload_destination_required ? upload_destination->allocationSize() : 0;
    const std::uint32_t      upload_destination_memory_type_index = upload_destination_required ? upload_destination->memoryTypeIndex() : 0;
    const VkMemoryPropertyFlags upload_destination_memory_property_flags =
        upload_destination_required ? upload_destination->memoryPropertyFlags() : 0;
    const VkBuffer                  readback_buffer      = readback_required ? readback->buffer() : VK_NULL_HANDLE;
    const VkDeviceMemory            readback_memory      = readback_required ? readback->memory() : VK_NULL_HANDLE;
    const bool                      readback_mapped      = readback_required && readback->isMapped();
    const VkFormat                  readback_format      = readback_required ? readback->imageFormat() : VK_FORMAT_UNDEFINED;
    const VkExtent2D                readback_extent      = readback_required ? readback->imageExtent() : VkExtent2D{};
    const std::uint32_t             readback_image_count = readback_required ? readback->imageCount() : 0;
    const VkDeviceSize              readback_row_bytes   = readback_required ? readback->rowBytes() : 0;
    const VkDeviceSize              readback_byte_count  = readback_required ? readback->byteCount() : 0;
    const VkDeviceSize              readback_allocation_size       = readback_required ? readback->allocationSize() : 0;
    const std::uint32_t             readback_memory_type_index     = readback_required ? readback->memoryTypeIndex() : 0;
    const VkMemoryPropertyFlags     readback_memory_property_flags = readback_required ? readback->memoryPropertyFlags() : 0;
    const VkCommandPool             command_pool                     = frame_slot->commandPool();
    const VkCommandBuffer           command_buffer                   = frame_slot->commandBuffer();
    const VkSemaphore               image_available_semaphore        = frame_slot->imageAvailableSemaphore();
    const VkSemaphore               presentation_ready_semaphore     = frame_slot->presentationReadySemaphore();
    const VkFence                   submission_fence                 = frame_slot->submissionFence();
    const VkFence                   present_completion_fence         = frame_slot->presentCompletionFence();

    if (selection->getInstanceProcAddr() != get_instance_proc_addr || selection->instance() != instance ||
        selection->surface() != surface || selection->physicalDevice() != physical_device ||
        selection->physicalDeviceIndex() != physical_device_index || !selection->selectedFor(instance, surface))
    {
        return swapchainFrameSlotOperationFailure(VulkanSwapchainFrameSlotParentOperationCode::PresentationDeviceNotLive);
    }
    if (logical_device->getInstanceProcAddr() != get_instance_proc_addr || logical_device->instance() != instance ||
        logical_device->surface() != surface || logical_device->physicalDevice() != physical_device ||
        logical_device->physicalDeviceIndex() != physical_device_index || logical_device->device() != device ||
        logical_device->queue() != queue || logical_device->queueFamilyIndex() != queue_family ||
        logical_device->queueIndex() != queue_index || !logical_device->createdFor(*selection))
    {
        return swapchainFrameSlotOperationFailure(VulkanSwapchainFrameSlotParentOperationCode::LogicalDeviceNotLive);
    }
    if (configuration->getInstanceProcAddr() != get_instance_proc_addr || configuration->instance() != instance ||
        configuration->surface() != surface || configuration->physicalDevice() != physical_device ||
        configuration->physicalDeviceIndex() != physical_device_index || configuration->device() != device ||
        configuration->queueFamilyIndex() != queue_family || configuration->drawableExtent().width != drawable_extent.width ||
        configuration->drawableExtent().height != drawable_extent.height ||
        !configuration->createdFor(*selection, *logical_device, drawable_extent))
    {
        return swapchainFrameSlotOperationFailure(VulkanSwapchainFrameSlotParentOperationCode::SwapchainConfigurationNotLive);
    }
    if (swapchain_generation->getInstanceProcAddr() != get_instance_proc_addr || swapchain_generation->instance() != instance ||
        swapchain_generation->surface() != surface || swapchain_generation->physicalDevice() != physical_device ||
        swapchain_generation->physicalDeviceIndex() != physical_device_index || swapchain_generation->device() != device ||
        swapchain_generation->queueFamilyIndex() != queue_family || swapchain_generation->drawableExtent().width != drawable_extent.width ||
        swapchain_generation->drawableExtent().height != drawable_extent.height || swapchain_generation->swapchain() != swapchain ||
        !swapchain_generation->createdFor(*logical_device, *configuration))
    {
        return swapchainFrameSlotOperationFailure(VulkanSwapchainFrameSlotParentOperationCode::SwapchainNotLive);
    }
    if (images_generation->imageCount() != image_count || images_generation->imageFormat() != image_format ||
        !images_generation->createdFor(*logical_device, *configuration, *swapchain_generation))
    {
        return swapchainFrameSlotOperationFailure(VulkanSwapchainFrameSlotParentOperationCode::SwapchainImagesNotLive);
    }
    const auto exact_presentation_target = [&]() noexcept
    {
        if (!render_pass_operation)
        {
            return true;
        }
        if (instance_generation.mSwapchainPresentationTargetEpoch != presentation_target_epoch ||
            instance_generation.mSwapchainPresentationTargetGeneration.get() != presentation_target || !presentation_target ||
            presentation_target->renderPass() != render_pass || render_pass == VK_NULL_HANDLE ||
            presentation_target->framebufferCount() != framebuffer_count || framebuffer_count != image_count ||
            presentation_target->imageFormat() != target_image_format || target_image_format != image_format ||
            presentation_target->imageExtent().width != target_image_extent.width ||
            presentation_target->imageExtent().height != target_image_extent.height ||
            target_image_extent.width != configuration->imageExtent().width ||
            target_image_extent.height != configuration->imageExtent().height ||
            !presentation_target->createdFor(*logical_device, *configuration, *swapchain_generation, *images_generation))
        {
            return false;
        }
        for (std::uint32_t index = 0; index < framebuffer_count; ++index)
        {
            if (presentation_target->framebuffer(index) == VK_NULL_HANDLE)
            {
                return false;
            }
        }
        return true;
    };
    if (!exact_presentation_target())
    {
        return swapchainFrameSlotOperationFailure(
            VulkanSwapchainFrameSlotParentOperationCode::SwapchainPresentationTargetNotLive);
    }
    const auto exact_presentation_pipeline = [&]() noexcept
    {
        return !render_pass_draw ||
               (instance_generation.mSwapchainPresentationPipelineEpoch == presentation_pipeline_epoch &&
                instance_generation.mSwapchainPresentationPipelineGeneration.get() == presentation_pipeline && presentation_pipeline &&
                presentation_pipeline->pipelineLayout() == pipeline_layout && pipeline_layout != VK_NULL_HANDLE &&
                presentation_pipeline->pipeline() == pipeline && pipeline != VK_NULL_HANDLE &&
                presentation_pipeline->createdFor(*logical_device, *configuration, *swapchain_generation, *images_generation,
                                                  *presentation_target));
    };
    if (!exact_presentation_pipeline())
    {
        return swapchainFrameSlotOperationFailure(VulkanSwapchainFrameSlotParentOperationCode::SwapchainPresentationPipelineNotLive);
    }
    const auto exact_upload_destination = [&]() noexcept
    {
        if (!upload_destination_required)
        {
            return true;
        }
        return instance_generation.mUploadDestinationEpoch == upload_destination_epoch &&
               instance_generation.mUploadDestinationGeneration.get() == upload_destination && upload_destination &&
               upload_destination->matchesDescription(SCREEN_TRIANGLE_DESCRIPTION) &&
               upload_destination->resourceHandle() == upload_destination_handle && upload_destination_handle == SCREEN_TRIANGLE_HANDLE &&
               upload_destination->expectedContentIdentity() == upload_destination_expected_identity &&
               upload_destination_expected_identity == LLRenderContract::SCREEN_TRIANGLE_CONTENT_IDENTITY &&
               upload_destination->residentContentIdentity() == upload_destination_resident_identity &&
               upload_destination_resident_identity == upload_destination_expected_identity && upload_destination->isResident() &&
               upload_destination->buffer() == upload_destination_buffer && upload_destination_buffer != VK_NULL_HANDLE &&
               upload_destination->memory() == upload_destination_memory && upload_destination_memory != VK_NULL_HANDLE &&
               upload_destination->byteCount() == upload_destination_byte_count &&
               upload_destination_byte_count == VULKAN_UPLOAD_SOURCE_BYTE_COUNT &&
               upload_destination->usage() == upload_destination_usage &&
               upload_destination_usage == (VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) &&
               upload_destination->allocationSize() == upload_destination_allocation_size &&
               upload_destination_allocation_size >= upload_destination_byte_count &&
               upload_destination->memoryTypeIndex() == upload_destination_memory_type_index &&
               upload_destination->memoryPropertyFlags() == upload_destination_memory_property_flags &&
               (upload_destination_memory_property_flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0 &&
               upload_destination->isDeviceLocal() && !upload_destination->isMapped() &&
               upload_destination->createdFor(*selection, *logical_device);
    };
    if (!exact_upload_destination())
    {
        return swapchainFrameSlotOperationFailure(VulkanSwapchainFrameSlotParentOperationCode::UploadDestinationNotLive);
    }
    const auto exact_readback = [&]() noexcept
    {
        if (!readback_required)
        {
            return true;
        }
        return instance_generation.mSwapchainReadbackEpoch == readback_epoch &&
               instance_generation.mSwapchainReadbackGeneration.get() == readback && readback && readback->buffer() == readback_buffer &&
               readback_buffer != VK_NULL_HANDLE && readback->memory() == readback_memory && readback_memory != VK_NULL_HANDLE &&
               readback->isMapped() == readback_mapped && readback_mapped && readback->imageFormat() == readback_format &&
               readback_format == image_format && readback->imageExtent().width == readback_extent.width &&
               readback->imageExtent().height == readback_extent.height && readback_extent.width == configuration->imageExtent().width &&
               readback_extent.height == configuration->imageExtent().height && readback->imageCount() == readback_image_count &&
               readback_image_count == image_count && readback->rowBytes() == readback_row_bytes && readback_row_bytes != 0 &&
               readback->byteCount() == readback_byte_count && readback_byte_count != 0 &&
               readback->allocationSize() == readback_allocation_size && readback_allocation_size >= readback_byte_count &&
               readback->memoryTypeIndex() == readback_memory_type_index &&
               readback->memoryPropertyFlags() == readback_memory_property_flags &&
               (readback_memory_property_flags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                   (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) &&
               readback->createdFor(*selection, *logical_device, *configuration, *swapchain_generation, *images_generation);
    };
    if (!exact_readback())
    {
        return swapchainFrameSlotOperationFailure(VulkanSwapchainFrameSlotParentOperationCode::SwapchainReadbackNotLive);
    }
    const auto exact_frame_slot = [&]() noexcept
    {
        return instance_generation.mSwapchainFrameSlotEpoch == frame_slot_epoch &&
               instance_generation.mSwapchainFrameSlotGeneration.get() == frame_slot && frame_slot &&
               frame_slot->commandPool() == command_pool && frame_slot->commandBuffer() == command_buffer &&
               frame_slot->imageAvailableSemaphore() == image_available_semaphore &&
               frame_slot->presentationReadySemaphore() == presentation_ready_semaphore &&
               frame_slot->submissionFence() == submission_fence && frame_slot->presentCompletionFence() == present_completion_fence &&
               frame_slot->createdFor(*logical_device, *configuration, *swapchain_generation, *images_generation);
    };
    if (!exact_frame_slot())
    {
        return swapchainFrameSlotOperationFailure(VulkanSwapchainFrameSlotParentOperationCode::SwapchainFrameSlotNotLive);
    }
    const auto exact_active_upload_destination = [&]() noexcept
    {
        return !upload_destination_required || !frame_slot->hasRetainedUploadDestinationGeneration() ||
               (frame_slot->retainsUploadDestinationGeneration(*upload_destination) &&
                frame_slot->activeUploadDestinationMatchesSnapshot());
    };
    if (!exact_active_upload_destination())
    {
        return swapchainFrameSlotOperationFailure(VulkanSwapchainFrameSlotParentOperationCode::UploadDestinationNotLive);
    }

    if (startsNewSwapchainFrameSlotWork(operation))
    {
        VulkanSwapchainFrameSlotOperationResult resolution;
        if (render_pass_draw_readback)
        {
            resolution = frame_slot->resolveRenderPassDrawReadbackPresentationDispatch(*selection,
                                                                                       *logical_device,
                                                                                       *configuration,
                                                                                       *swapchain_generation,
                                                                                       *images_generation,
                                                                                       *presentation_target,
                                                                                       *presentation_pipeline,
                                                                                       *upload_destination,
                                                                                       *readback);
        }
        else if (render_pass_draw)
        {
            resolution = frame_slot->resolveRenderPassDrawPresentationDispatch(*selection,
                                                                               *logical_device,
                                                                               *configuration,
                                                                               *swapchain_generation,
                                                                               *images_generation,
                                                                               *presentation_target,
                                                                               *presentation_pipeline,
                                                                               *upload_destination);
        }
        else if (render_pass_clear)
        {
            resolution = frame_slot->resolveRenderPassPresentationDispatch(
                *logical_device, *configuration, *swapchain_generation, *images_generation, *presentation_target);
        }
        else if (operation == SwapchainFrameSlotParentOperation::ExecuteAcquireClearToPresent)
        {
            resolution =
                frame_slot->resolveClearPresentationDispatch(*logical_device, *configuration, *swapchain_generation, *images_generation);
        }
        else if (operation == SwapchainFrameSlotParentOperation::ExecuteAcquireToPresent)
        {
            resolution = frame_slot->resolvePresentationDispatch(*logical_device, *configuration, *swapchain_generation, *images_generation);
        }
        else
        {
            resolution =
                frame_slot->resolveEmptySubmissionDispatch(*logical_device, *configuration, *swapchain_generation, *images_generation);
        }
        if (const auto* error = std::get_if<VulkanSwapchainFrameSlotOperationError>(&resolution))
        {
            return swapchainFrameSlotOperationFailure(VulkanSwapchainFrameSlotParentOperationCode::OperationFailure, *error);
        }

        if (auto error = swapchainFrameSlotOperationFreshness(request, instance_generation))
        {
            return *error;
        }

        if (!instance_generation.mGlobalDispatch || &*instance_generation.mGlobalDispatch != global_dispatch ||
            instance_generation.mGlobalDispatch->getInstanceProcAddr() != get_instance_proc_addr ||
            instance_generation.mInstance != instance || !instance_generation.mDestroyInstance ||
            instance_generation.mNativeWindowGeneration != native_window_generation)
        {
            return swapchainFrameSlotOperationFailure(VulkanSwapchainFrameSlotParentOperationCode::InstanceNotLive);
        }
        if (instance_generation.mSurfaceGeneration.get() != surface_generation ||
            instance_generation.surfaceNativeWindowGeneration() != surface_native_window_generation ||
            instance_generation.surface() != surface)
        {
            return swapchainFrameSlotOperationFailure(VulkanSwapchainFrameSlotParentOperationCode::SurfaceNotLive);
        }
        if (instance_generation.mPresentationDeviceGeneration.get() != selection ||
            selection->getInstanceProcAddr() != get_instance_proc_addr || selection->instance() != instance ||
            selection->surface() != surface || selection->physicalDevice() != physical_device ||
            selection->physicalDeviceIndex() != physical_device_index || !selection->selectedFor(instance, surface))
        {
            return swapchainFrameSlotOperationFailure(VulkanSwapchainFrameSlotParentOperationCode::PresentationDeviceNotLive);
        }
        if (instance_generation.mLogicalDeviceGeneration.get() != logical_device ||
            logical_device->getInstanceProcAddr() != get_instance_proc_addr || logical_device->instance() != instance ||
            logical_device->surface() != surface || logical_device->physicalDevice() != physical_device ||
            logical_device->physicalDeviceIndex() != physical_device_index || logical_device->device() != device ||
            logical_device->queue() != queue || logical_device->queueFamilyIndex() != queue_family ||
            logical_device->queueIndex() != queue_index || !logical_device->createdFor(*selection))
        {
            return swapchainFrameSlotOperationFailure(VulkanSwapchainFrameSlotParentOperationCode::LogicalDeviceNotLive);
        }
        if (instance_generation.mSwapchainConfigurationGeneration.get() != configuration ||
            configuration->getInstanceProcAddr() != get_instance_proc_addr || configuration->instance() != instance ||
            configuration->surface() != surface || configuration->physicalDevice() != physical_device ||
            configuration->physicalDeviceIndex() != physical_device_index || configuration->device() != device ||
            configuration->queueFamilyIndex() != queue_family || configuration->drawableExtent().width != drawable_extent.width ||
            configuration->drawableExtent().height != drawable_extent.height ||
            !configuration->createdFor(*selection, *logical_device, drawable_extent))
        {
            return swapchainFrameSlotOperationFailure(VulkanSwapchainFrameSlotParentOperationCode::SwapchainConfigurationNotLive);
        }
        if (instance_generation.mSwapchainGeneration.get() != swapchain_generation ||
            swapchain_generation->getInstanceProcAddr() != get_instance_proc_addr || swapchain_generation->instance() != instance ||
            swapchain_generation->surface() != surface || swapchain_generation->physicalDevice() != physical_device ||
            swapchain_generation->physicalDeviceIndex() != physical_device_index || swapchain_generation->device() != device ||
            swapchain_generation->queueFamilyIndex() != queue_family ||
            swapchain_generation->drawableExtent().width != drawable_extent.width ||
            swapchain_generation->drawableExtent().height != drawable_extent.height || swapchain_generation->swapchain() != swapchain ||
            !swapchain_generation->createdFor(*logical_device, *configuration))
        {
            return swapchainFrameSlotOperationFailure(VulkanSwapchainFrameSlotParentOperationCode::SwapchainNotLive);
        }
        if (instance_generation.mSwapchainImagesGeneration.get() != images_generation || images_generation->imageCount() != image_count ||
            images_generation->imageFormat() != image_format ||
            !images_generation->createdFor(*logical_device, *configuration, *swapchain_generation))
        {
            return swapchainFrameSlotOperationFailure(VulkanSwapchainFrameSlotParentOperationCode::SwapchainImagesNotLive);
        }
        if (!exact_presentation_target())
        {
            return swapchainFrameSlotOperationFailure(VulkanSwapchainFrameSlotParentOperationCode::SwapchainPresentationTargetNotLive);
        }
        if (!exact_presentation_pipeline())
        {
            return swapchainFrameSlotOperationFailure(VulkanSwapchainFrameSlotParentOperationCode::SwapchainPresentationPipelineNotLive);
        }
        if (!exact_upload_destination())
        {
            return swapchainFrameSlotOperationFailure(VulkanSwapchainFrameSlotParentOperationCode::UploadDestinationNotLive);
        }
        if (!exact_readback())
        {
            return swapchainFrameSlotOperationFailure(VulkanSwapchainFrameSlotParentOperationCode::SwapchainReadbackNotLive);
        }
        if (!exact_frame_slot())
        {
            return swapchainFrameSlotOperationFailure(VulkanSwapchainFrameSlotParentOperationCode::SwapchainFrameSlotNotLive);
        }
        if (!exact_active_upload_destination())
        {
            return swapchainFrameSlotOperationFailure(VulkanSwapchainFrameSlotParentOperationCode::UploadDestinationNotLive);
        }
        if (auto error = validate_live_chain())
        {
            return *error;
        }
        if (!exact_upload_destination())
        {
            return swapchainFrameSlotOperationFailure(VulkanSwapchainFrameSlotParentOperationCode::UploadDestinationNotLive);
        }
        if (!exact_active_upload_destination())
        {
            return swapchainFrameSlotOperationFailure(VulkanSwapchainFrameSlotParentOperationCode::UploadDestinationNotLive);
        }
    }

    if (operation == SwapchainFrameSlotParentOperation::ExecuteAcquireToPresent ||
        operation == SwapchainFrameSlotParentOperation::ExecuteAcquireClearToPresent ||
        operation == SwapchainFrameSlotParentOperation::ExecuteAcquireRenderPassClearToPresent ||
        operation == SwapchainFrameSlotParentOperation::ExecuteAcquireRenderPassDrawToPresent ||
        operation == SwapchainFrameSlotParentOperation::ExecuteAcquireRenderPassDrawReadbackToPresent ||
        operation == SwapchainFrameSlotParentOperation::RetryPresentation ||
        operation == SwapchainFrameSlotParentOperation::RetryPresentationCompletion)
    {
        VulkanSwapchainFrameSlotPresentationResult result;
        switch (operation)
        {
            case SwapchainFrameSlotParentOperation::ExecuteAcquireToPresent:
                result = frame_slot->executeAcquireToPresent();
                break;
            case SwapchainFrameSlotParentOperation::ExecuteAcquireClearToPresent:
                result = frame_slot->executeAcquireClearToPresent(*clear_color);
                break;
            case SwapchainFrameSlotParentOperation::ExecuteAcquireRenderPassClearToPresent:
                result = frame_slot->executeAcquireRenderPassClearToPresent(*presentation_target, *clear_color);
                break;
            case SwapchainFrameSlotParentOperation::ExecuteAcquireRenderPassDrawToPresent:
                result = frame_slot->executeAcquireRenderPassDrawToPresent(*selection, *presentation_target, *presentation_pipeline,
                                                                           *upload_destination, *clear_color);
                break;
            case SwapchainFrameSlotParentOperation::ExecuteAcquireRenderPassDrawReadbackToPresent:
                result = frame_slot->executeAcquireRenderPassDrawReadbackToPresent(*selection, *presentation_target, *presentation_pipeline,
                                                                                   *upload_destination, *readback);
                break;
            case SwapchainFrameSlotParentOperation::RetryPresentation:
                result = frame_slot->retryPresentation();
                break;
            case SwapchainFrameSlotParentOperation::RetryPresentationCompletion:
                result = frame_slot->retryPresentationCompletion();
                break;
            default:
                std::terminate();
        }
        if (const auto* error = std::get_if<VulkanSwapchainFrameSlotOperationError>(&result))
        {
            return swapchainFrameSlotOperationFailure(VulkanSwapchainFrameSlotParentOperationCode::OperationFailure, *error);
        }
        return std::get<VulkanSwapchainFrameSlotPresentationSuccess>(result);
    }

    VulkanSwapchainFrameSlotOperationResult result;
    switch (operation)
    {
        case SwapchainFrameSlotParentOperation::ExecuteEmptySubmission:
            result = frame_slot->executeEmptySubmission();
            break;
        case SwapchainFrameSlotParentOperation::RetryEmptySubmissionCompletion:
            result = frame_slot->retryEmptySubmissionCompletion();
            break;
        case SwapchainFrameSlotParentOperation::CancelAcquireToPresent:
            result = frame_slot->cancelAcquireToPresent();
            break;
        case SwapchainFrameSlotParentOperation::RetryCancellationCompletion:
            result = frame_slot->retryCancellationCompletion();
            break;
        default:
            std::terminate();
    }
    if (const auto* error = std::get_if<VulkanSwapchainFrameSlotOperationError>(&result))
    {
        return swapchainFrameSlotOperationFailure(VulkanSwapchainFrameSlotParentOperationCode::OperationFailure, *error);
    }
    return std::get<VulkanSwapchainFrameSlotDisposition>(result);
}

VulkanSwapchainFrameSlotParentOperationResult VulkanInstanceGenerationFactory::roundTripEmptySwapchainFrameSlot(
    VulkanInstanceGeneration&                       instance_generation,
    const VulkanSwapchainFrameSlotOperationRequest& request) noexcept
{
    const SwapchainFrameSlotParentResult result =
        operateSwapchainFrameSlot(instance_generation, request, SwapchainFrameSlotParentOperation::ExecuteEmptySubmission);
    if (const auto* error = std::get_if<VulkanSwapchainFrameSlotParentOperationError>(&result))
    {
        return *error;
    }
    return std::get<VulkanSwapchainFrameSlotDisposition>(result);
}

VulkanSwapchainFrameSlotParentOperationResult VulkanInstanceGenerationFactory::retryEmptySwapchainFrameSlotCompletion(
    VulkanInstanceGeneration&                       instance_generation,
    const VulkanSwapchainFrameSlotOperationRequest& request) noexcept
{
    const SwapchainFrameSlotParentResult result =
        operateSwapchainFrameSlot(instance_generation, request, SwapchainFrameSlotParentOperation::RetryEmptySubmissionCompletion);
    if (const auto* error = std::get_if<VulkanSwapchainFrameSlotParentOperationError>(&result))
    {
        return *error;
    }
    return std::get<VulkanSwapchainFrameSlotDisposition>(result);
}

VulkanSwapchainFrameSlotParentPresentationResult VulkanInstanceGenerationFactory::acquireToPresentSwapchainFrameSlot(
    VulkanInstanceGeneration&                       instance_generation,
    const VulkanSwapchainFrameSlotOperationRequest& request) noexcept
{
    const SwapchainFrameSlotParentResult result =
        operateSwapchainFrameSlot(instance_generation, request, SwapchainFrameSlotParentOperation::ExecuteAcquireToPresent);
    if (const auto* error = std::get_if<VulkanSwapchainFrameSlotParentOperationError>(&result))
    {
        return *error;
    }
    return std::get<VulkanSwapchainFrameSlotPresentationSuccess>(result);
}

VulkanSwapchainFrameSlotParentPresentationResult VulkanInstanceGenerationFactory::acquireClearToPresentSwapchainFrameSlot(
    VulkanInstanceGeneration&                       instance_generation,
    const VulkanSwapchainFrameSlotOperationRequest& request,
    const VulkanSwapchainFrameClearColor&           clear_color) noexcept
{
    const SwapchainFrameSlotParentResult result = operateSwapchainFrameSlot(
        instance_generation, request, SwapchainFrameSlotParentOperation::ExecuteAcquireClearToPresent, clear_color);
    if (const auto* error = std::get_if<VulkanSwapchainFrameSlotParentOperationError>(&result))
    {
        return *error;
    }
    return std::get<VulkanSwapchainFrameSlotPresentationSuccess>(result);
}

VulkanSwapchainFrameSlotParentPresentationResult
VulkanInstanceGenerationFactory::acquireRenderPassClearToPresentSwapchainFrameSlot(
    VulkanInstanceGeneration&                       instance_generation,
    const VulkanSwapchainFrameSlotOperationRequest& request,
    const VulkanSwapchainFrameClearColor&           clear_color) noexcept
{
    const VulkanSwapchainFrameClearColor normalized_clear_color = clear_color;
    if (!validClearColor(normalized_clear_color))
    {
        return swapchainFrameSlotOperationFailure(VulkanSwapchainFrameSlotParentOperationCode::InvalidClearColor);
    }
    const SwapchainFrameSlotParentResult result = operateSwapchainFrameSlot(
        instance_generation,
        request,
        SwapchainFrameSlotParentOperation::ExecuteAcquireRenderPassClearToPresent,
        normalized_clear_color);
    if (const auto* error = std::get_if<VulkanSwapchainFrameSlotParentOperationError>(&result))
    {
        return *error;
    }
    return std::get<VulkanSwapchainFrameSlotPresentationSuccess>(result);
}

VulkanSwapchainFrameSlotParentPresentationResult VulkanInstanceGenerationFactory::acquireRenderPassDrawToPresentSwapchainFrameSlot(
    VulkanInstanceGeneration&                       instance_generation,
    const VulkanSwapchainFrameSlotOperationRequest& request,
    const VulkanSwapchainFrameClearColor&           clear_color) noexcept
{
    const VulkanSwapchainFrameClearColor normalized_clear_color = clear_color;
    if (!validClearColor(normalized_clear_color))
    {
        return swapchainFrameSlotOperationFailure(VulkanSwapchainFrameSlotParentOperationCode::InvalidClearColor);
    }
    const SwapchainFrameSlotParentResult result =
        operateSwapchainFrameSlot(instance_generation,
                                  request,
                                  SwapchainFrameSlotParentOperation::ExecuteAcquireRenderPassDrawToPresent,
                                  normalized_clear_color);
    if (const auto* error = std::get_if<VulkanSwapchainFrameSlotParentOperationError>(&result))
    {
        return *error;
    }
    return std::get<VulkanSwapchainFrameSlotPresentationSuccess>(result);
}

VulkanSwapchainFrameSlotParentPresentationResult VulkanInstanceGenerationFactory::acquireRenderPassDrawReadbackToPresentSwapchainFrameSlot(
    VulkanInstanceGeneration&                       instance_generation,
    const VulkanSwapchainFrameSlotOperationRequest& request) noexcept
{
    const SwapchainFrameSlotParentResult result =
        operateSwapchainFrameSlot(instance_generation,
                                  request,
                                  SwapchainFrameSlotParentOperation::ExecuteAcquireRenderPassDrawReadbackToPresent);
    if (const auto* error = std::get_if<VulkanSwapchainFrameSlotParentOperationError>(&result))
    {
        return *error;
    }
    return std::get<VulkanSwapchainFrameSlotPresentationSuccess>(result);
}

VulkanSwapchainFrameSlotParentPresentationResult VulkanInstanceGenerationFactory::retrySwapchainFrameSlotPresentation(
    VulkanInstanceGeneration&                       instance_generation,
    const VulkanSwapchainFrameSlotOperationRequest& request) noexcept
{
    const SwapchainFrameSlotParentResult result =
        operateSwapchainFrameSlot(instance_generation, request, SwapchainFrameSlotParentOperation::RetryPresentation);
    if (const auto* error = std::get_if<VulkanSwapchainFrameSlotParentOperationError>(&result))
    {
        return *error;
    }
    return std::get<VulkanSwapchainFrameSlotPresentationSuccess>(result);
}

VulkanSwapchainFrameSlotParentPresentationResult VulkanInstanceGenerationFactory::retrySwapchainFrameSlotPresentationCompletion(
    VulkanInstanceGeneration&                       instance_generation,
    const VulkanSwapchainFrameSlotOperationRequest& request) noexcept
{
    const SwapchainFrameSlotParentResult result =
        operateSwapchainFrameSlot(instance_generation, request, SwapchainFrameSlotParentOperation::RetryPresentationCompletion);
    if (const auto* error = std::get_if<VulkanSwapchainFrameSlotParentOperationError>(&result))
    {
        return *error;
    }
    return std::get<VulkanSwapchainFrameSlotPresentationSuccess>(result);
}

VulkanSwapchainFrameSlotParentOperationResult VulkanInstanceGenerationFactory::cancelSwapchainFrameSlotPresentation(
    VulkanInstanceGeneration&                       instance_generation,
    const VulkanSwapchainFrameSlotOperationRequest& request) noexcept
{
    const SwapchainFrameSlotParentResult result =
        operateSwapchainFrameSlot(instance_generation, request, SwapchainFrameSlotParentOperation::CancelAcquireToPresent);
    if (const auto* error = std::get_if<VulkanSwapchainFrameSlotParentOperationError>(&result))
    {
        return *error;
    }
    return std::get<VulkanSwapchainFrameSlotDisposition>(result);
}

VulkanSwapchainFrameSlotParentOperationResult VulkanInstanceGenerationFactory::retrySwapchainFrameSlotCancellationCompletion(
    VulkanInstanceGeneration&                       instance_generation,
    const VulkanSwapchainFrameSlotOperationRequest& request) noexcept
{
    const SwapchainFrameSlotParentResult result =
        operateSwapchainFrameSlot(instance_generation, request, SwapchainFrameSlotParentOperation::RetryCancellationCompletion);
    if (const auto* error = std::get_if<VulkanSwapchainFrameSlotParentOperationError>(&result))
    {
        return *error;
    }
    return std::get<VulkanSwapchainFrameSlotDisposition>(result);
}

namespace VulkanInstanceDetail
{

    VulkanInstanceAcquireResult acquire(const VulkanInstanceRequest& request, AllocationCheckpoint allocation_checkpoint) noexcept
    {
        if (!request.mGenerationCheck.mIsCurrent)
        {
            return failure(VulkanInstanceAcquireCode::InvalidGenerationCheck);
        }
        if (request.mValidationMode != VulkanInstanceValidationMode::Disabled &&
            request.mValidationMode != VulkanInstanceValidationMode::Required)
        {
            return failure(VulkanInstanceAcquireCode::InvalidValidationMode);
        }
        if (request.mPortabilityMode != VulkanInstancePortabilityMode::Disabled &&
            request.mPortabilityMode != VulkanInstancePortabilityMode::EnableIfAvailable)
        {
            return failure(VulkanInstanceAcquireCode::InvalidPortabilityMode);
        }

        if (request.mRequiredWindowExtensions.empty() || request.mRequiredWindowExtensions.size() > MAX_REQUIRED_EXTENSION_COUNT)
        {
            return failure(VulkanInstanceAcquireCode::InvalidRequiredWindowExtensions, VK_SUCCESS, std::nullopt, std::nullopt, std::nullopt,
                           std::nullopt, static_cast<std::uint32_t>(request.mRequiredWindowExtensions.size()));
        }
        for (std::size_t index = 0; index < request.mRequiredWindowExtensions.size(); ++index)
        {
            const std::string& extension = request.mRequiredWindowExtensions[index];
            if (extension.empty() || extension.size() >= VK_MAX_EXTENSION_NAME_SIZE)
            {
                return failure(VulkanInstanceAcquireCode::InvalidRequiredWindowExtensions, VK_SUCCESS, std::nullopt, std::nullopt, index);
            }
            for (std::size_t previous = 0; previous < index; ++previous)
            {
                if (request.mRequiredWindowExtensions[previous] == extension)
                {
                    return failure(VulkanInstanceAcquireCode::InvalidRequiredWindowExtensions, VK_SUCCESS, std::nullopt, std::nullopt,
                                   index);
                }
            }
        }

        const std::uint64_t native_window_generation = request.mNativeWindowGeneration;
        if (!current(request.mGenerationCheck, native_window_generation))
        {
            return failure(VulkanInstanceAcquireCode::StaleWindowGeneration);
        }

        VulkanGlobalDispatchResolutionResult dispatch_result = resolveVulkanGlobalDispatchGeneration(request.mGetInstanceProcAddr);
        if (const auto* dispatch_error = std::get_if<VulkanGlobalDispatchResolutionError>(&dispatch_result))
        {
            return failure(VulkanInstanceAcquireCode::GlobalDispatchFailure, VK_SUCCESS, *dispatch_error);
        }
        VulkanGlobalDispatchGeneration global_dispatch(std::get<VulkanGlobalDispatchGeneration>(std::move(dispatch_result)));

        try
        {
            std::vector<VkExtensionProperties> extension_properties;
            VulkanInstanceAcquireError         enumeration_error;
            if (!enumerateProperties<VkExtensionProperties>(
                    [&global_dispatch](std::uint32_t* count, VkExtensionProperties* properties)
                    { return global_dispatch.enumerateInstanceExtensionProperties()(nullptr, count, properties); },
                    VulkanInstanceAcquireCode::ExtensionEnumerationFailure, VulkanInstanceAcquireCode::ExtensionCountExceeded,
                    extension_properties, enumeration_error, allocation_checkpoint))
            {
                return enumeration_error;
            }

            for (std::size_t index = 0; index < extension_properties.size(); ++index)
            {
                const std::optional<std::string_view> name = boundedName(extension_properties[index].extensionName);
                if (!name || name->empty())
                {
                    return failure(VulkanInstanceAcquireCode::MalformedExtensionProperty, VK_SUCCESS, std::nullopt, std::nullopt,
                                   std::nullopt, index);
                }
            }

            std::vector<VkLayerProperties> layer_properties;
            if (request.mValidationMode == VulkanInstanceValidationMode::Required)
            {
                if (!enumerateProperties<VkLayerProperties>(
                        [&global_dispatch](std::uint32_t* count, VkLayerProperties* properties)
                        { return global_dispatch.enumerateInstanceLayerProperties()(count, properties); },
                        VulkanInstanceAcquireCode::LayerEnumerationFailure, VulkanInstanceAcquireCode::LayerCountExceeded, layer_properties,
                        enumeration_error, allocation_checkpoint))
                {
                    return enumeration_error;
                }

                for (std::size_t index = 0; index < layer_properties.size(); ++index)
                {
                    const std::optional<std::string_view> name = boundedName(layer_properties[index].layerName);
                    if (!name || name->empty())
                    {
                        return failure(VulkanInstanceAcquireCode::MalformedLayerProperty, VK_SUCCESS, std::nullopt, std::nullopt,
                                       std::nullopt, index);
                    }
                }
            }

            if (!current(request.mGenerationCheck, native_window_generation))
            {
                return failure(VulkanInstanceAcquireCode::StaleWindowGeneration);
            }

            const auto extension_name = [](const VkExtensionProperties& property) noexcept
            {
                return boundedName(property.extensionName);
            };
            const auto layer_name = [](const VkLayerProperties& property) noexcept
            {
                return boundedName(property.layerName);
            };

            if (allocation_checkpoint)
            {
                allocation_checkpoint();
            }
            std::vector<std::string> enabled_extensions(request.mRequiredWindowExtensions.begin(), request.mRequiredWindowExtensions.end());
            for (std::size_t index = 0; index < request.mRequiredWindowExtensions.size(); ++index)
            {
                if (!hasName(extension_properties, extension_name, request.mRequiredWindowExtensions[index]))
                {
                    return failure(VulkanInstanceAcquireCode::MissingRequiredWindowExtension, VK_SUCCESS, std::nullopt, std::nullopt,
                                   index);
                }
            }

            std::vector<std::string>                                   enabled_layers;
            std::unique_ptr<VulkanInstanceGeneration::ValidationState> validation_state;
            if (request.mValidationMode == VulkanInstanceValidationMode::Required)
            {
                if (!hasName(layer_properties, layer_name, VALIDATION_LAYER))
                {
                    return failure(VulkanInstanceAcquireCode::MissingValidationLayer);
                }
                if (!hasName(extension_properties, extension_name, VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
                {
                    return failure(VulkanInstanceAcquireCode::MissingValidationExtension);
                }
                if (!contains(enabled_extensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
                {
                    enabled_extensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
                }
                enabled_layers.emplace_back(VALIDATION_LAYER);
                if (allocation_checkpoint)
                {
                    allocation_checkpoint();
                }
                validation_state = VulkanInstanceGenerationFactory::allocateValidationState();
            }

            const bool portability_advertised = hasName(extension_properties, extension_name, PORTABILITY_EXTENSION);
            const bool portability_required   = contains(enabled_extensions, PORTABILITY_EXTENSION);
            const bool portability_enumeration =
                portability_required ||
                (request.mPortabilityMode == VulkanInstancePortabilityMode::EnableIfAvailable && portability_advertised);
#if !defined(VK_KHR_portability_enumeration)
            if (portability_enumeration)
            {
                return failure(VulkanInstanceAcquireCode::PortabilityPolicyUnavailable);
            }
#endif
            if (portability_enumeration && portability_advertised && !contains(enabled_extensions, PORTABILITY_EXTENSION))
            {
                enabled_extensions.emplace_back(PORTABILITY_EXTENSION);
            }

            if (allocation_checkpoint)
            {
                allocation_checkpoint();
            }
            std::vector<const char*> extension_pointers;
            extension_pointers.reserve(enabled_extensions.size());
            for (const std::string& extension : enabled_extensions)
            {
                extension_pointers.push_back(extension.c_str());
            }

            std::vector<const char*> layer_pointers;
            layer_pointers.reserve(enabled_layers.size());
            for (const std::string& layer : enabled_layers)
            {
                layer_pointers.push_back(layer.c_str());
            }

            if (!current(request.mGenerationCheck, native_window_generation))
            {
                return failure(VulkanInstanceAcquireCode::StaleWindowGeneration);
            }

            VkDebugUtilsMessengerCreateInfoEXT debug_info{};
            if (validation_state)
            {
                debug_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
                debug_info.messageSeverity =
                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
                debug_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                         VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
                debug_info.pfnUserCallback = VulkanInstanceGenerationFactory::validationCallback;
                debug_info.pUserData       = validation_state.get();
            }

            VkApplicationInfo application_info{};
            application_info.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
            application_info.pApplicationName   = "Second Life renderer Vulkan instance diagnostic";
            application_info.applicationVersion = 1;
            application_info.pEngineName        = "Second Life Viewer";
            application_info.engineVersion      = 1;
            application_info.apiVersion         = RENDERER_VULKAN_API_VERSION;

            VkInstanceCreateInfo create_info{};
            create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
            create_info.pNext = validation_state ? &debug_info : nullptr;
#if defined(VK_KHR_portability_enumeration)
            create_info.flags = portability_enumeration ? VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR : 0;
#endif
            create_info.pApplicationInfo        = &application_info;
            create_info.enabledLayerCount       = static_cast<std::uint32_t>(layer_pointers.size());
            create_info.ppEnabledLayerNames     = layer_pointers.empty() ? nullptr : layer_pointers.data();
            create_info.enabledExtensionCount   = static_cast<std::uint32_t>(extension_pointers.size());
            create_info.ppEnabledExtensionNames = extension_pointers.data();

            VkInstance     instance      = VK_NULL_HANDLE;
            const VkResult create_result = global_dispatch.createInstance()(&create_info, nullptr, &instance);
            if (create_result != VK_SUCCESS)
            {
                return failure(VulkanInstanceAcquireCode::InstanceCreationFailure, create_result);
            }
            if (instance == VK_NULL_HANDLE)
            {
                return failure(VulkanInstanceAcquireCode::NullInstanceOnSuccess);
            }

            InstanceRollback rollback;
            rollback.mInstance = instance;

            const PFN_vkDestroyInstance destroy_instance =
                resolveInstanceCommand<PFN_vkDestroyInstance>(global_dispatch.getInstanceProcAddr(), instance, "vkDestroyInstance");
            if (!destroy_instance)
            {
                // A conforming loader must expose this core command for a valid
                // instance. Without it, loader-independent code has no legal
                // rollback call. Report the breach and publish no owner.
                return failure(VulkanInstanceAcquireCode::MissingRequiredInstanceCommand, VK_SUCCESS, std::nullopt,
                               VulkanInstanceCommand::DestroyInstance);
            }
            rollback.mDestroyInstance = destroy_instance;

            if (!current(request.mGenerationCheck, native_window_generation))
            {
                return failure(VulkanInstanceAcquireCode::StaleWindowGeneration);
            }

            PFN_vkDestroyDebugUtilsMessengerEXT destroy_debug_messenger = nullptr;
            if (validation_state)
            {
                const PFN_vkCreateDebugUtilsMessengerEXT create_debug_messenger =
                    resolveInstanceCommand<PFN_vkCreateDebugUtilsMessengerEXT>(global_dispatch.getInstanceProcAddr(), instance,
                                                                               "vkCreateDebugUtilsMessengerEXT");
                if (!create_debug_messenger)
                {
                    return failure(VulkanInstanceAcquireCode::MissingRequiredInstanceCommand, VK_SUCCESS, std::nullopt,
                                   VulkanInstanceCommand::CreateDebugUtilsMessenger);
                }
                destroy_debug_messenger = resolveInstanceCommand<PFN_vkDestroyDebugUtilsMessengerEXT>(
                    global_dispatch.getInstanceProcAddr(), instance, "vkDestroyDebugUtilsMessengerEXT");
                if (!destroy_debug_messenger)
                {
                    return failure(VulkanInstanceAcquireCode::MissingRequiredInstanceCommand, VK_SUCCESS, std::nullopt,
                                   VulkanInstanceCommand::DestroyDebugUtilsMessenger);
                }

                VkDebugUtilsMessengerEXT debug_messenger = VK_NULL_HANDLE;
                const VkResult           debug_result    = create_debug_messenger(instance, &debug_info, nullptr, &debug_messenger);
                if (debug_result != VK_SUCCESS)
                {
                    return failure(VulkanInstanceAcquireCode::DebugMessengerCreationFailure, debug_result);
                }
                if (debug_messenger == VK_NULL_HANDLE)
                {
                    return failure(VulkanInstanceAcquireCode::NullDebugMessengerOnSuccess);
                }
                rollback.mDebugMessenger        = debug_messenger;
                rollback.mDestroyDebugMessenger = destroy_debug_messenger;
            }

            if (!current(request.mGenerationCheck, native_window_generation))
            {
                return failure(VulkanInstanceAcquireCode::StaleWindowGeneration);
            }

            VkDebugUtilsMessengerEXT debug_messenger = rollback.mDebugMessenger;
            rollback.release();
            return VulkanInstanceGenerationFactory::create(
                std::move(global_dispatch), std::move(validation_state), std::move(enabled_extensions), std::move(enabled_layers),
                native_window_generation, portability_enumeration, instance, destroy_instance, debug_messenger, destroy_debug_messenger);
        }
        catch (const std::bad_alloc&)
        {
            return failure(VulkanInstanceAcquireCode::AllocationFailure);
        }
    }

    VulkanSurfaceAcquireResult acquireSurface(VulkanInstanceGeneration&   instance_generation,
                                              const VulkanSurfaceRequest& request,
                                              AllocationCheckpoint        allocation_checkpoint) noexcept
    {
        return VulkanInstanceGenerationFactory::acquireSurface(instance_generation, request, allocation_checkpoint);
    }

    VulkanPresentationDeviceAcquireResult acquirePresentationDevice(VulkanInstanceGeneration&              instance_generation,
                                                                    const VulkanPresentationDeviceRequest& request,
                                                                    AllocationCheckpoint                   allocation_checkpoint) noexcept
    {
        return VulkanInstanceGenerationFactory::acquirePresentationDevice(instance_generation, request, allocation_checkpoint);
    }

    VulkanLogicalDeviceAcquireResult acquireLogicalDevice(VulkanInstanceGeneration&         instance_generation,
                                                          const VulkanLogicalDeviceRequest& request,
                                                          AllocationCheckpoint              allocation_checkpoint) noexcept
    {
        return VulkanInstanceGenerationFactory::acquireLogicalDevice(instance_generation, request, allocation_checkpoint);
    }

    VulkanUploadSourceAcquireResult acquireUploadSource(VulkanInstanceGeneration&        instance_generation,
                                                        const VulkanUploadSourceRequest& request,
                                                        AllocationCheckpoint             allocation_checkpoint) noexcept
    {
        return VulkanInstanceGenerationFactory::acquireUploadSource(instance_generation, request, allocation_checkpoint);
    }

    VulkanTextureUploadDestinationAcquireResult acquireTextureUploadDestination(VulkanInstanceGeneration& instance_generation,
                                                                                const VulkanTextureUploadDestinationRequest& request,
                                                                                AllocationCheckpoint allocation_checkpoint) noexcept
    {
        return VulkanInstanceGenerationFactory::acquireTextureUploadDestination(instance_generation, request, allocation_checkpoint);
    }

    VulkanTextureUploadSourceAcquireResult acquireTextureUploadSource(VulkanInstanceGeneration&               instance_generation,
                                                                      const VulkanTextureUploadSourceRequest& request,
                                                                      AllocationCheckpoint allocation_checkpoint) noexcept
    {
        return VulkanInstanceGenerationFactory::acquireTextureUploadSource(instance_generation, request, allocation_checkpoint);
    }

    VulkanTextureUploadTransferAcquireResult acquireTextureUploadTransfer(VulkanInstanceGeneration&                 instance_generation,
                                                                          const VulkanTextureUploadTransferRequest& request,
                                                                          AllocationCheckpoint allocation_checkpoint) noexcept
    {
        return VulkanInstanceGenerationFactory::acquireTextureUploadTransfer(instance_generation, request, allocation_checkpoint);
    }

    VulkanTextureUploadSampleBindingAcquireResult acquireTextureUploadSampleBinding(VulkanInstanceGeneration& instance_generation,
                                                                                    const VulkanTextureUploadSampleBindingRequest& request,
                                                                                    AllocationCheckpoint allocation_checkpoint) noexcept
    {
        return VulkanInstanceGenerationFactory::acquireTextureUploadSampleBinding(instance_generation, request, allocation_checkpoint);
    }

    VulkanTextureUploadSamplePipelineAcquireResult acquireTextureUploadSamplePipeline(
        VulkanInstanceGeneration&                       instance_generation,
        const VulkanTextureUploadSamplePipelineRequest& request,
        AllocationCheckpoint                            allocation_checkpoint) noexcept
    {
        return VulkanInstanceGenerationFactory::acquireTextureUploadSamplePipeline(instance_generation, request, allocation_checkpoint);
    }

    VulkanUploadDestinationAcquireResult acquireUploadDestination(VulkanInstanceGeneration&             instance_generation,
                                                                  const VulkanUploadDestinationRequest& request,
                                                                  AllocationCheckpoint                  allocation_checkpoint) noexcept
    {
        return VulkanInstanceGenerationFactory::acquireUploadDestination(instance_generation, request, allocation_checkpoint);
    }

    VulkanUploadTransferAcquireResult acquireUploadTransfer(VulkanInstanceGeneration&          instance_generation,
                                                            const VulkanUploadTransferRequest& request,
                                                            AllocationCheckpoint               allocation_checkpoint) noexcept
    {
        return VulkanInstanceGenerationFactory::acquireUploadTransfer(instance_generation, request, allocation_checkpoint);
    }

    VulkanSwapchainConfigurationAcquireResult acquireSwapchainConfiguration(VulkanInstanceGeneration&                  instance_generation,
                                                                            const VulkanSwapchainConfigurationRequest& request,
                                                                            AllocationCheckpoint allocation_checkpoint) noexcept
    {
        return VulkanInstanceGenerationFactory::acquireSwapchainConfiguration(instance_generation, request, allocation_checkpoint);
    }

    VulkanSwapchainAcquireResult acquireSwapchain(VulkanInstanceGeneration&     instance_generation,
                                                  const VulkanSwapchainRequest& request,
                                                  AllocationCheckpoint          allocation_checkpoint) noexcept
    {
        return VulkanInstanceGenerationFactory::acquireSwapchain(instance_generation, request, allocation_checkpoint);
    }

    VulkanSwapchainImagesAcquireResult acquireSwapchainImages(VulkanInstanceGeneration&           instance_generation,
                                                              const VulkanSwapchainImagesRequest& request,
                                                              AllocationCheckpoint                allocation_checkpoint) noexcept
    {
        return VulkanInstanceGenerationFactory::acquireSwapchainImages(instance_generation, request, allocation_checkpoint);
    }

    VulkanSwapchainPresentationTargetAcquireResult acquireSwapchainPresentationTarget(
        VulkanInstanceGeneration&                         instance_generation,
        const VulkanSwapchainPresentationTargetRequest&   request,
        AllocationCheckpoint                              allocation_checkpoint) noexcept
    {
        return VulkanInstanceGenerationFactory::acquireSwapchainPresentationTarget(
            instance_generation, request, allocation_checkpoint);
    }

    VulkanSwapchainPresentationPipelineAcquireResult acquireSwapchainPresentationPipeline(
        VulkanInstanceGeneration&                         instance_generation,
        const VulkanSwapchainPresentationPipelineRequest& request,
        AllocationCheckpoint                              allocation_checkpoint) noexcept
    {
        return VulkanInstanceGenerationFactory::acquireSwapchainPresentationPipeline(instance_generation, request, allocation_checkpoint);
    }

    VulkanSwapchainReadbackAcquireResult acquireSwapchainReadback(VulkanInstanceGeneration&             instance_generation,
                                                                  const VulkanSwapchainReadbackRequest& request,
                                                                  AllocationCheckpoint                  allocation_checkpoint) noexcept
    {
        return VulkanInstanceGenerationFactory::acquireSwapchainReadback(instance_generation, request, allocation_checkpoint);
    }

    VulkanSwapchainFrameSlotAcquireResult acquireSwapchainFrameSlot(VulkanInstanceGeneration&              instance_generation,
                                                                    const VulkanSwapchainFrameSlotRequest& request,
                                                                    AllocationCheckpoint                   allocation_checkpoint) noexcept
    {
        return VulkanInstanceGenerationFactory::acquireSwapchainFrameSlot(instance_generation, request, allocation_checkpoint);
    }

    VulkanSwapchainChainRebuildResult rebuildSwapchainChain(VulkanInstanceGeneration&                 instance_generation,
                                                             const VulkanSwapchainChainRebuildRequest& request,
                                                             AllocationCheckpoint                      allocation_checkpoint) noexcept
    {
        return VulkanInstanceGenerationFactory::rebuildSwapchainChain(instance_generation, request, allocation_checkpoint);
    }

} // namespace VulkanInstanceDetail

VulkanInstanceAcquireResult acquireVulkanInstanceGeneration(const VulkanInstanceRequest& request) noexcept
{
    return VulkanInstanceDetail::acquire(request, nullptr);
}

} // namespace LLRenderVulkan
