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
        ExecuteAcquireRenderPassClearToPresent
    };

    using SwapchainFrameSlotParentResult = std::variant<VulkanSwapchainFrameSlotParentOperationError, VulkanSwapchainFrameSlotDisposition,
                                                        VulkanSwapchainFrameSlotPresentationSuccess>;

    bool startsNewSwapchainFrameSlotWork(SwapchainFrameSlotParentOperation operation) noexcept
    {
        return operation == SwapchainFrameSlotParentOperation::ExecuteEmptySubmission ||
               operation == SwapchainFrameSlotParentOperation::ExecuteAcquireToPresent ||
               operation == SwapchainFrameSlotParentOperation::ExecuteAcquireClearToPresent ||
               operation == SwapchainFrameSlotParentOperation::ExecuteAcquireRenderPassClearToPresent;
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
    mSwapchainConfigurationGeneration = std::move(other.mSwapchainConfigurationGeneration);
    mSwapchainGeneration              = std::move(other.mSwapchainGeneration);
    mSwapchainImagesGeneration        = std::move(other.mSwapchainImagesGeneration);
    mSwapchainPresentationTargetGeneration = std::move(other.mSwapchainPresentationTargetGeneration);
    mSwapchainFrameSlotGeneration     = std::move(other.mSwapchainFrameSlotGeneration);
    mOwnershipTransitionEpoch         = std::exchange(other.mOwnershipTransitionEpoch, 0);
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
    if (mSwapchainFrameSlotGeneration && !frameSlotDispositionAllowsReset(mSwapchainFrameSlotGeneration->disposition()))
    {
        return false;
    }
    if (mSwapchainFrameSlotGeneration)
    {
        mSwapchainFrameSlotGeneration.reset();
        noteOwnershipTransition();
    }
    return true;
}

bool VulkanInstanceGeneration::resetSwapchainPresentationTargetGeneration() noexcept
{
    if (mNativeAcquisitionDepth != 0)
    {
        return false;
    }
    if (!resetSwapchainFrameSlotGeneration())
    {
        return false;
    }
    if (mSwapchainPresentationTargetGeneration)
    {
        mSwapchainPresentationTargetGeneration.reset();
        noteOwnershipTransition();
    }
    return true;
}

bool VulkanInstanceGeneration::resetSwapchainImagesGeneration() noexcept
{
    if (mNativeAcquisitionDepth != 0)
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

bool VulkanInstanceGeneration::resetLogicalDeviceGeneration() noexcept
{
    if (mNativeAcquisitionDepth != 0)
    {
        return false;
    }
    if (!resetSwapchainConfigurationGeneration())
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
        instance_generation.noteOwnershipTransition();
        return std::nullopt;
    }
    catch (const std::bad_alloc&)
    {
        return swapchainPresentationTargetFailure(VulkanSwapchainPresentationTargetAcquireCode::AllocationFailure);
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
        instance_generation.noteOwnershipTransition();
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
    const auto* original_images        = instance_generation.mSwapchainImagesGeneration.get();
    const auto* original_presentation_target = instance_generation.mSwapchainPresentationTargetGeneration.get();
    const auto* original_frame_slot    = instance_generation.mSwapchainFrameSlotGeneration.get();
    const std::uint64_t original_ownership_epoch = instance_generation.mOwnershipTransitionEpoch;

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

    const auto parents_still_live = [&]() noexcept
    {
        return instance_generation.mGlobalDispatch && &*instance_generation.mGlobalDispatch == global_dispatch &&
               instance_generation.mGlobalDispatch->getInstanceProcAddr() == get_instance_proc_addr &&
               instance_generation.mInstance == instance && instance_generation.mDestroyInstance &&
               instance_generation.mNativeWindowGeneration == native_window_generation &&
               instance_generation.mSurfaceGeneration.get() == surface_generation &&
               surface_generation->surface() == surface &&
               surface_generation->nativeWindowGeneration() == surface_native_window_generation &&
               instance_generation.mPresentationDeviceGeneration.get() == selection &&
               selection->getInstanceProcAddr() == get_instance_proc_addr && selection->physicalDevice() == physical_device &&
               selection->physicalDeviceIndex() == physical_device_index && selection->selectedFor(instance, surface) &&
               instance_generation.mLogicalDeviceGeneration.get() == logical_device &&
               logical_device->getInstanceProcAddr() == get_instance_proc_addr && logical_device->device() == device &&
               logical_device->queue() == queue && logical_device->queueFamilyIndex() == queue_family &&
               logical_device->queueIndex() == queue_index && logical_device->createdFor(*selection);
    };

    const auto chain_is_authentic = [&]() noexcept
    {
        const auto* configuration = instance_generation.mSwapchainConfigurationGeneration.get();
        const auto* swapchain     = instance_generation.mSwapchainGeneration.get();
        const auto* images        = instance_generation.mSwapchainImagesGeneration.get();
        const auto* presentation_target = instance_generation.mSwapchainPresentationTargetGeneration.get();
        const auto* frame_slot    = instance_generation.mSwapchainFrameSlotGeneration.get();

        if ((swapchain && !configuration) || (images && !swapchain) || (presentation_target && !images) ||
            (frame_slot && !images))
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
        const std::optional<VulkanSwapchainFrameSlotDisposition> disposition =
            instance_generation.swapchainFrameSlotDisposition();
        const VulkanSwapchainChainRebuildCode code =
            disposition == VulkanSwapchainFrameSlotDisposition::DeviceLost
                ? VulkanSwapchainChainRebuildCode::DeviceRecoveryRequired
                : VulkanSwapchainChainRebuildCode::FrameSlotResetRefused;
        return swapchainChainRebuildFailure(code, VulkanSwapchainChainRebuildPhase::Retirement, {}, disposition);
    }

    const auto no_swapchain_children = [&]() noexcept
    {
        return !instance_generation.mSwapchainConfigurationGeneration && !instance_generation.mSwapchainGeneration &&
               !instance_generation.mSwapchainImagesGeneration &&
               !instance_generation.mSwapchainPresentationTargetGeneration &&
               !instance_generation.mSwapchainFrameSlotGeneration;
    };
    const auto rollback_children = [&]() noexcept
    {
        return instance_generation.resetSwapchainConfigurationGeneration() && no_swapchain_children();
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

    if (!parents_still_live() || !no_swapchain_children())
    {
        return publication_failure(VulkanSwapchainChainRebuildPhase::Retirement);
    }

    if (drawable_extent.width == 0 || drawable_extent.height == 0)
    {
        if (auto error = final_freshness())
        {
            return rollback_or_report(*error);
        }
        if (!parents_still_live() || !no_swapchain_children())
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
    if (!parents_still_live() || !chain_is_authentic())
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
    if (!parents_still_live() || !chain_is_authentic())
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
    if (!parents_still_live() || !chain_is_authentic())
    {
        return publication_failure(VulkanSwapchainChainRebuildPhase::Images);
    }

    const VulkanSwapchainPresentationTargetRequest presentation_target_request{
        request.mNativeWindowGeneration, drawable_extent, request.mInstanceOwnerCheck, request.mWindowGenerationCheck
    };
    if (VulkanSwapchainPresentationTargetAcquireResult error =
            acquireSwapchainPresentationTarget(instance_generation, presentation_target_request, allocation_checkpoint))
    {
        return rollback_or_report(swapchainChainRebuildFailure(
            VulkanSwapchainChainRebuildCode::ChildFailure,
            VulkanSwapchainChainRebuildPhase::PresentationTarget,
            VulkanSwapchainChainRebuildChildError{ *error }));
    }
    if (!parents_still_live() || !chain_is_authentic())
    {
        return publication_failure(VulkanSwapchainChainRebuildPhase::PresentationTarget);
    }

    const VulkanSwapchainFrameSlotRequest frame_slot_request{ request.mNativeWindowGeneration, drawable_extent,
                                                              request.mInstanceOwnerCheck,
                                                              request.mWindowGenerationCheck };
    if (VulkanSwapchainFrameSlotAcquireResult error =
            acquireSwapchainFrameSlot(instance_generation, frame_slot_request, allocation_checkpoint))
    {
        return rollback_or_report(swapchainChainRebuildFailure(VulkanSwapchainChainRebuildCode::ChildFailure,
                                                               VulkanSwapchainChainRebuildPhase::FrameSlot,
                                                               VulkanSwapchainChainRebuildChildError{ *error }));
    }
    if (!parents_still_live() || !chain_is_authentic())
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
    const auto* images        = instance_generation.mSwapchainImagesGeneration.get();
    const auto* presentation_target = instance_generation.mSwapchainPresentationTargetGeneration.get();
    const auto* frame_slot    = instance_generation.mSwapchainFrameSlotGeneration.get();
    if (!parents_still_live() || !configuration || !swapchain || !images || !presentation_target || !frame_slot ||
        !configuration->createdFor(*selection, *logical_device, drawable_extent) ||
        !swapchain->createdFor(*logical_device, *configuration) ||
        !images->createdFor(*logical_device, *configuration, *swapchain) ||
        !presentation_target->createdFor(*logical_device, *configuration, *swapchain, *images) ||
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
    const bool render_pass_clear =
        operation == SwapchainFrameSlotParentOperation::ExecuteAcquireRenderPassClearToPresent;
    if ((operation == SwapchainFrameSlotParentOperation::ExecuteAcquireClearToPresent || render_pass_clear) &&
        (!clear_color || !validClearColor(*clear_color)))
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
        if (render_pass_clear)
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
    const auto* presentation_target  = render_pass_clear ? instance_generation.mSwapchainPresentationTargetGeneration.get() : nullptr;
    auto*       frame_slot           = instance_generation.mSwapchainFrameSlotGeneration.get();

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
    const std::uint64_t             ownership_epoch                  = instance_generation.mOwnershipTransitionEpoch;
    const VkRenderPass              render_pass = presentation_target ? presentation_target->renderPass() : VK_NULL_HANDLE;
    const std::uint32_t             framebuffer_count = presentation_target ? presentation_target->framebufferCount() : 0;
    const VkFormat                  target_image_format = presentation_target ? presentation_target->imageFormat() : VK_FORMAT_UNDEFINED;
    const VkExtent2D                target_image_extent = presentation_target ? presentation_target->imageExtent() : VkExtent2D{};
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
        if (!render_pass_clear)
        {
            return true;
        }
        if (instance_generation.mOwnershipTransitionEpoch != ownership_epoch ||
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
    if (frame_slot->commandPool() != command_pool || frame_slot->commandBuffer() != command_buffer ||
        frame_slot->imageAvailableSemaphore() != image_available_semaphore ||
        frame_slot->presentationReadySemaphore() != presentation_ready_semaphore || frame_slot->submissionFence() != submission_fence ||
        frame_slot->presentCompletionFence() != present_completion_fence ||
        !frame_slot->createdFor(*logical_device, *configuration, *swapchain_generation, *images_generation))
    {
        return swapchainFrameSlotOperationFailure(VulkanSwapchainFrameSlotParentOperationCode::SwapchainFrameSlotNotLive);
    }

    if (startsNewSwapchainFrameSlotWork(operation))
    {
        VulkanSwapchainFrameSlotOperationResult resolution;
        if (render_pass_clear)
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
        if (auto error = validate_live_chain())
        {
            return *error;
        }

        if (&*instance_generation.mGlobalDispatch != global_dispatch ||
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
            return swapchainFrameSlotOperationFailure(
                VulkanSwapchainFrameSlotParentOperationCode::SwapchainPresentationTargetNotLive);
        }
        if (instance_generation.mSwapchainFrameSlotGeneration.get() != frame_slot || frame_slot->commandPool() != command_pool ||
            frame_slot->commandBuffer() != command_buffer || frame_slot->imageAvailableSemaphore() != image_available_semaphore ||
            frame_slot->presentationReadySemaphore() != presentation_ready_semaphore || frame_slot->submissionFence() != submission_fence ||
            frame_slot->presentCompletionFence() != present_completion_fence ||
            !frame_slot->createdFor(*logical_device, *configuration, *swapchain_generation, *images_generation))
        {
            return swapchainFrameSlotOperationFailure(VulkanSwapchainFrameSlotParentOperationCode::SwapchainFrameSlotNotLive);
        }
    }

    if (operation == SwapchainFrameSlotParentOperation::ExecuteAcquireToPresent ||
        operation == SwapchainFrameSlotParentOperation::ExecuteAcquireClearToPresent ||
        operation == SwapchainFrameSlotParentOperation::ExecuteAcquireRenderPassClearToPresent ||
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
