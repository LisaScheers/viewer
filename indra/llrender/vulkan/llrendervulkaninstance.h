/**
 * @file llrendervulkaninstance.h
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

#ifndef LL_LLRENDERVULKANINSTANCE_H
#define LL_LLRENDERVULKANINSTANCE_H

#include "llrendervulkanglobaldispatch.h"
#include "llrendervulkanlogicaldevice.h"
#include "llrendervulkanphysicaldevice.h"
#include "llrendervulkantextureuploaddestination.h"
#include "llrendervulkantextureuploadsamplebinding.h"
#include "llrendervulkantextureuploadsamplepipeline.h"
#include "llrendervulkantextureuploadsource.h"
#include "llrendervulkantextureuploadtransfer.h"
#include "llrendervulkanuploaddestination.h"
#include "llrendervulkanuploadsource.h"
#include "llrendervulkanuploadtransfer.h"
#include "llrendervulkanswapchain.h"
#include "llrendervulkanswapchainconfiguration.h"
#include "llrendervulkanswapchainframeslot.h"
#include "llrendervulkanswapchainimages.h"
#include "llrendervulkanswapchainpresentationtarget.h"
#include "llrendervulkanswapchainpresentationpipeline.h"
#include "llrendervulkanswapchainreadback.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace LLRenderVulkan
{

inline constexpr std::size_t VULKAN_VALIDATION_MESSAGE_CAPACITY = 1024;

class VulkanInstanceGeneration;

enum class VulkanInstanceValidationMode : std::uint8_t
{
    Disabled,
    Required
};

enum class VulkanInstancePortabilityMode : std::uint8_t
{
    Disabled,
    EnableIfAvailable
};

struct VulkanWindowGenerationCheck
{
    void* mUserdata                                                                     = nullptr;
    bool (*mIsCurrent)(void* userdata, std::uint64_t native_window_generation) noexcept = nullptr;
};

struct VulkanInstanceRequest
{
    PFN_vkGetInstanceProcAddr     mGetInstanceProcAddr = nullptr;
    std::span<const std::string>  mRequiredWindowExtensions;
    std::uint64_t                 mNativeWindowGeneration = 0;
    VulkanWindowGenerationCheck   mGenerationCheck;
    VulkanInstanceValidationMode  mValidationMode  = VulkanInstanceValidationMode::Disabled;
    VulkanInstancePortabilityMode mPortabilityMode = VulkanInstancePortabilityMode::Disabled;
};

enum class VulkanInstanceCommand : std::uint8_t
{
    DestroyInstance,
    CreateDebugUtilsMessenger,
    DestroyDebugUtilsMessenger
};

enum class VulkanInstanceAcquireCode : std::uint8_t
{
    InvalidGenerationCheck,
    InvalidValidationMode,
    InvalidPortabilityMode,
    InvalidRequiredWindowExtensions,
    InstanceAlreadyOwned,
    StaleWindowGeneration,
    GlobalDispatchFailure,
    ExtensionEnumerationFailure,
    LayerEnumerationFailure,
    ExtensionCountExceeded,
    LayerCountExceeded,
    EnumerationRetryLimitExceeded,
    MalformedExtensionProperty,
    MalformedLayerProperty,
    MissingRequiredWindowExtension,
    MissingValidationLayer,
    MissingValidationExtension,
    PortabilityPolicyUnavailable,
    AllocationFailure,
    InstanceCreationFailure,
    NullInstanceOnSuccess,
    MissingRequiredInstanceCommand,
    DebugMessengerCreationFailure,
    NullDebugMessengerOnSuccess
};

struct VulkanInstanceAcquireError
{
    VulkanInstanceAcquireCode                          mCode   = VulkanInstanceAcquireCode::InvalidGenerationCheck;
    VkResult                                           mResult = VK_SUCCESS;
    std::optional<VulkanGlobalDispatchResolutionError> mGlobalDispatchError;
    std::optional<VulkanInstanceCommand>               mCommand;
    std::optional<std::size_t>                         mRequiredExtensionIndex;
    std::optional<std::size_t>                         mPropertyIndex;
    std::uint32_t                                      mObservedCount = 0;

    friend constexpr bool operator==(const VulkanInstanceAcquireError&, const VulkanInstanceAcquireError&) = default;
};

struct VulkanValidationSnapshot
{
    std::uint32_t                                        mMessageCount     = 0;
    std::size_t                                          mFirstMessageSize = 0;
    std::array<char, VULKAN_VALIDATION_MESSAGE_CAPACITY> mFirstMessage{};

    std::string_view firstMessage() const noexcept { return std::string_view(mFirstMessage.data(), mFirstMessageSize); }
};

struct VulkanInstanceOwnerCheck
{
    void* mUserdata                                                                         = nullptr;
    bool (*mIsCurrent)(void* userdata, const VulkanInstanceGeneration& generation) noexcept = nullptr;
};

struct VulkanSurfacePlatformFailure
{
    friend constexpr bool operator==(VulkanSurfacePlatformFailure, VulkanSurfacePlatformFailure) = default;
};

using VulkanSurfaceCreateOutcome = std::variant<VulkanSurfacePlatformFailure, VkResult>;

struct VulkanSurfaceCreateOperation
{
    void* mUserdata                                                       = nullptr;
    VulkanSurfaceCreateOutcome (*mCreate)(void*                        userdata,
                                          VkInstance                   instance,
                                          const VkAllocationCallbacks* allocation_callbacks,
                                          VkSurfaceKHR*                surface) noexcept = nullptr;
};

struct VulkanSurfaceRequest
{
    // All callbacks are synchronous and are not retained by the generation.
    // The caller must serialize parent and native-window lifetime changes.
    std::uint64_t                mNativeWindowGeneration = 0;
    VulkanInstanceOwnerCheck     mInstanceOwnerCheck;
    VulkanWindowGenerationCheck  mWindowGenerationCheck;
    VulkanSurfaceCreateOperation mCreateOperation;
};

enum class VulkanSurfaceCommand : std::uint8_t
{
    DestroySurface
};

enum class VulkanSurfaceAcquireCode : std::uint8_t
{
    InvalidInstanceOwnerCheck,
    InvalidWindowGenerationCheck,
    InvalidCreateOperation,
    InvalidNativeWindowGeneration,
    InstanceNotLive,
    SurfaceAlreadyOwned,
    NativeWindowGenerationMismatch,
    StaleInstanceOwner,
    StaleWindowGeneration,
    MissingSurfaceExtension,
    MissingRequiredInstanceCommand,
    AllocationFailure,
    PlatformCreationFailure,
    SurfaceCreationFailure,
    NullSurfaceOnSuccess
};

struct VulkanSurfaceAcquireError
{
    VulkanSurfaceAcquireCode            mCode = VulkanSurfaceAcquireCode::InvalidCreateOperation;
    std::optional<VkResult>             mResult;
    std::optional<VulkanSurfaceCommand> mCommand;

    friend constexpr bool operator==(const VulkanSurfaceAcquireError&, const VulkanSurfaceAcquireError&) = default;
};

using VulkanSurfaceAcquireResult = std::optional<VulkanSurfaceAcquireError>;

struct VulkanPresentationDeviceRequest
{
    // These callbacks are synchronous and are not retained. The caller must
    // serialize parent and native-window lifetime changes during acquisition.
    std::uint64_t               mNativeWindowGeneration = 0;
    VulkanInstanceOwnerCheck    mInstanceOwnerCheck;
    VulkanWindowGenerationCheck mWindowGenerationCheck;
};

enum class VulkanPresentationDeviceAcquireCode : std::uint8_t
{
    InvalidInstanceOwnerCheck,
    InvalidWindowGenerationCheck,
    InvalidNativeWindowGeneration,
    InstanceNotLive,
    SurfaceNotLive,
    PresentationDeviceAlreadyOwned,
    NativeWindowGenerationMismatch,
    StaleInstanceOwner,
    StaleWindowGeneration,
    ResolutionFailure,
    AllocationFailure
};

struct VulkanPresentationDeviceAcquireError
{
    VulkanPresentationDeviceAcquireCode                mCode = VulkanPresentationDeviceAcquireCode::InvalidInstanceOwnerCheck;
    std::optional<VulkanPhysicalDeviceResolutionError> mResolutionError;

    friend constexpr bool operator==(const VulkanPresentationDeviceAcquireError&, const VulkanPresentationDeviceAcquireError&) = default;
};

using VulkanPresentationDeviceAcquireResult = std::optional<VulkanPresentationDeviceAcquireError>;

struct VulkanLogicalDeviceRequest
{
    // These callbacks are synchronous and are not retained. The caller must
    // serialize parent and native-window lifetime changes during acquisition.
    std::uint64_t               mNativeWindowGeneration = 0;
    VulkanInstanceOwnerCheck    mInstanceOwnerCheck;
    VulkanWindowGenerationCheck mWindowGenerationCheck;
};

enum class VulkanLogicalDeviceAcquireCode : std::uint8_t
{
    InvalidInstanceOwnerCheck,
    InvalidWindowGenerationCheck,
    InvalidNativeWindowGeneration,
    InstanceNotLive,
    SurfaceNotLive,
    PresentationDeviceNotLive,
    LogicalDeviceAlreadyOwned,
    NativeWindowGenerationMismatch,
    StaleInstanceOwner,
    StaleWindowGeneration,
    ResolutionFailure,
    AllocationFailure
};

struct VulkanLogicalDeviceAcquireError
{
    VulkanLogicalDeviceAcquireCode                    mCode = VulkanLogicalDeviceAcquireCode::InvalidInstanceOwnerCheck;
    std::optional<VulkanLogicalDeviceResolutionError> mResolutionError;

    friend constexpr bool operator==(const VulkanLogicalDeviceAcquireError&, const VulkanLogicalDeviceAcquireError&) = default;
};

using VulkanLogicalDeviceAcquireResult = std::optional<VulkanLogicalDeviceAcquireError>;

struct VulkanUploadSourceRequest
{
    // These callbacks are synchronous and are not retained. The caller must
    // serialize parent and native-window lifetime changes during acquisition.
    std::uint64_t                 mNativeWindowGeneration = 0;
    VulkanUploadSourceDescription mDescription;
    VulkanInstanceOwnerCheck      mInstanceOwnerCheck;
    VulkanWindowGenerationCheck   mWindowGenerationCheck;
};

enum class VulkanUploadSourceAcquireCode : std::uint8_t
{
    InvalidInstanceOwnerCheck,
    InvalidWindowGenerationCheck,
    InvalidNativeWindowGeneration,
    InstanceNotLive,
    SurfaceNotLive,
    PresentationDeviceNotLive,
    LogicalDeviceNotLive,
    UploadSourceAlreadyOwned,
    NativeWindowGenerationMismatch,
    StaleInstanceOwner,
    StaleWindowGeneration,
    ResolutionFailure,
    AllocationFailure
};

struct VulkanUploadSourceAcquireError
{
    VulkanUploadSourceAcquireCode                    mCode = VulkanUploadSourceAcquireCode::InvalidInstanceOwnerCheck;
    std::optional<VulkanUploadSourceResolutionError> mResolutionError;

    friend constexpr bool operator==(const VulkanUploadSourceAcquireError&, const VulkanUploadSourceAcquireError&) = default;
};

using VulkanUploadSourceAcquireResult = std::optional<VulkanUploadSourceAcquireError>;

struct VulkanTextureUploadDestinationRequest
{
    // These callbacks are synchronous and are not retained. The caller must
    // serialize parent and native-window lifetime changes during acquisition.
    std::uint64_t                              mNativeWindowGeneration = 0;
    VulkanTextureUploadDestinationDescription mDescription;
    VulkanInstanceOwnerCheck                   mInstanceOwnerCheck;
    VulkanWindowGenerationCheck                mWindowGenerationCheck;
};

enum class VulkanTextureUploadDestinationAcquireCode : std::uint8_t
{
    InvalidInstanceOwnerCheck,
    InvalidWindowGenerationCheck,
    InvalidNativeWindowGeneration,
    NativeTeardownInProgress,
    InstanceNotLive,
    SurfaceNotLive,
    PresentationDeviceNotLive,
    LogicalDeviceNotLive,
    TextureUploadDestinationAlreadyOwned,
    NativeWindowGenerationMismatch,
    StaleInstanceOwner,
    StaleWindowGeneration,
    ResolutionFailure,
    AllocationFailure
};

struct VulkanTextureUploadDestinationAcquireError
{
    VulkanTextureUploadDestinationAcquireCode                    mCode =
        VulkanTextureUploadDestinationAcquireCode::InvalidInstanceOwnerCheck;
    std::optional<VulkanTextureUploadDestinationResolutionError> mResolutionError;

    friend constexpr bool operator==(const VulkanTextureUploadDestinationAcquireError&,
                                     const VulkanTextureUploadDestinationAcquireError&) = default;
};

using VulkanTextureUploadDestinationAcquireResult = std::optional<VulkanTextureUploadDestinationAcquireError>;

struct VulkanTextureUploadSourceRequest
{
    // These callbacks are synchronous and are not retained. The caller must
    // serialize parent and native-window lifetime changes during acquisition.
    std::uint64_t                        mNativeWindowGeneration = 0;
    VulkanTextureUploadSourceDescription mDescription;
    VulkanInstanceOwnerCheck             mInstanceOwnerCheck;
    VulkanWindowGenerationCheck          mWindowGenerationCheck;
};

enum class VulkanTextureUploadSourceAcquireCode : std::uint8_t
{
    InvalidInstanceOwnerCheck,
    InvalidWindowGenerationCheck,
    InvalidNativeWindowGeneration,
    NativeTeardownInProgress,
    InstanceNotLive,
    SurfaceNotLive,
    PresentationDeviceNotLive,
    LogicalDeviceNotLive,
    TextureUploadDestinationNotLive,
    TextureUploadSourceAlreadyOwned,
    NativeWindowGenerationMismatch,
    StaleInstanceOwner,
    StaleWindowGeneration,
    ResolutionFailure,
    AllocationFailure
};

struct VulkanTextureUploadSourceAcquireError
{
    VulkanTextureUploadSourceAcquireCode                    mCode = VulkanTextureUploadSourceAcquireCode::InvalidInstanceOwnerCheck;
    std::optional<VulkanTextureUploadSourceResolutionError> mResolutionError;

    friend constexpr bool operator==(const VulkanTextureUploadSourceAcquireError&, const VulkanTextureUploadSourceAcquireError&) = default;
};

using VulkanTextureUploadSourceAcquireResult = std::optional<VulkanTextureUploadSourceAcquireError>;

struct VulkanTextureUploadTransferRequest
{
    std::uint64_t                             mNativeWindowGeneration = 0;
    VulkanTextureUploadSourceDescription      mSourceDescription;
    VulkanTextureUploadDestinationDescription mDestinationDescription;
    VulkanInstanceOwnerCheck                  mInstanceOwnerCheck;
    VulkanWindowGenerationCheck               mWindowGenerationCheck;
};

enum class VulkanTextureUploadTransferAcquireCode : std::uint8_t
{
    InvalidInstanceOwnerCheck,
    InvalidWindowGenerationCheck,
    InvalidNativeWindowGeneration,
    NativeTeardownInProgress,
    InstanceNotLive,
    SurfaceNotLive,
    PresentationDeviceNotLive,
    LogicalDeviceNotLive,
    TextureUploadSourceNotLive,
    TextureUploadDestinationNotLive,
    TextureUploadTransferAlreadyOwned,
    NativeWindowGenerationMismatch,
    StaleInstanceOwner,
    StaleWindowGeneration,
    ResolutionFailure,
    AllocationFailure
};

struct VulkanTextureUploadTransferAcquireError
{
    VulkanTextureUploadTransferAcquireCode                    mCode = VulkanTextureUploadTransferAcquireCode::InvalidInstanceOwnerCheck;
    std::optional<VulkanTextureUploadTransferResolutionError> mResolutionError;
    friend constexpr bool                                     operator==(const VulkanTextureUploadTransferAcquireError&,
                                     const VulkanTextureUploadTransferAcquireError&) = default;
};
using VulkanTextureUploadTransferAcquireResult = std::optional<VulkanTextureUploadTransferAcquireError>;

struct VulkanTextureUploadTransferOperationRequest
{
    std::uint64_t                             mNativeWindowGeneration = 0;
    VulkanTextureUploadSourceDescription      mSourceDescription;
    VulkanTextureUploadDestinationDescription mDestinationDescription;
    std::uint64_t                             mTimeoutNs = 0;
    VulkanInstanceOwnerCheck                  mInstanceOwnerCheck;
    VulkanWindowGenerationCheck               mWindowGenerationCheck;
};

enum class VulkanTextureUploadTransferParentOperationCode : std::uint8_t
{
    InvalidInstanceOwnerCheck,
    InvalidWindowGenerationCheck,
    InvalidNativeWindowGeneration,
    NativeOperationInProgress,
    InstanceNotLive,
    SurfaceNotLive,
    PresentationDeviceNotLive,
    LogicalDeviceNotLive,
    TextureUploadSourceNotLive,
    TextureUploadDestinationNotLive,
    TextureUploadTransferNotLive,
    NativeWindowGenerationMismatch,
    StaleInstanceOwner,
    StaleWindowGeneration,
    OperationFailure
};

struct VulkanTextureUploadTransferParentOperationError
{
    VulkanTextureUploadTransferParentOperationCode mCode = VulkanTextureUploadTransferParentOperationCode::InvalidInstanceOwnerCheck;
    std::optional<VulkanTextureUploadTransferOperationError> mOperationError;
    friend constexpr bool                                    operator==(const VulkanTextureUploadTransferParentOperationError&,
                                     const VulkanTextureUploadTransferParentOperationError&) = default;
};
using VulkanTextureUploadTransferParentOperationResult =
    std::variant<VulkanTextureUploadTransferParentOperationError, VulkanTextureUploadTransferDisposition>;

struct VulkanTextureUploadSampleBindingRequest
{
    // These callbacks are synchronous and are not retained. The caller must
    // serialize parent and native-window lifetime changes during acquisition.
    std::uint64_t                               mNativeWindowGeneration = 0;
    VulkanTextureUploadDestinationDescription   mDestinationDescription;
    VulkanTextureUploadSampleBindingDescription mDescription;
    VulkanInstanceOwnerCheck                    mInstanceOwnerCheck;
    VulkanWindowGenerationCheck                 mWindowGenerationCheck;
};

enum class VulkanTextureUploadSampleBindingAcquireCode : std::uint8_t
{
    InvalidInstanceOwnerCheck,
    InvalidWindowGenerationCheck,
    InvalidNativeWindowGeneration,
    NativeTeardownInProgress,
    InstanceNotLive,
    SurfaceNotLive,
    PresentationDeviceNotLive,
    LogicalDeviceNotLive,
    TextureUploadDestinationNotLive,
    TextureUploadSampleBindingAlreadyOwned,
    NativeWindowGenerationMismatch,
    StaleInstanceOwner,
    StaleWindowGeneration,
    ResolutionFailure,
    AllocationFailure
};

struct VulkanTextureUploadSampleBindingAcquireError
{
    VulkanTextureUploadSampleBindingAcquireCode mCode = VulkanTextureUploadSampleBindingAcquireCode::InvalidInstanceOwnerCheck;
    std::optional<VulkanTextureUploadSampleBindingResolutionError> mResolutionError;

    friend constexpr bool operator==(const VulkanTextureUploadSampleBindingAcquireError&,
                                     const VulkanTextureUploadSampleBindingAcquireError&) = default;
};

using VulkanTextureUploadSampleBindingAcquireResult = std::optional<VulkanTextureUploadSampleBindingAcquireError>;

struct VulkanTextureUploadSamplePipelineRequest
{
    // These callbacks are synchronous and are not retained. The caller must
    // serialize parent, native-window, and drawable-geometry changes.
    std::uint64_t                                mNativeWindowGeneration = 0;
    VkExtent2D                                   mDrawableExtent{};
    VulkanTextureUploadDestinationDescription    mDestinationDescription;
    VulkanTextureUploadSampleBindingDescription  mSampleBindingDescription;
    VulkanTextureUploadSamplePipelineDescription mDescription;
    VulkanInstanceOwnerCheck                     mInstanceOwnerCheck;
    VulkanWindowGenerationCheck                  mWindowGenerationCheck;
};

enum class VulkanTextureUploadSamplePipelineAcquireCode : std::uint8_t
{
    InvalidInstanceOwnerCheck,
    InvalidWindowGenerationCheck,
    InvalidNativeWindowGeneration,
    InvalidDrawableExtent,
    NativeTeardownInProgress,
    InstanceNotLive,
    SurfaceNotLive,
    PresentationDeviceNotLive,
    LogicalDeviceNotLive,
    TextureUploadDestinationNotLive,
    TextureUploadSampleBindingNotLive,
    SwapchainConfigurationNotLive,
    SwapchainNotLive,
    SwapchainImagesNotLive,
    SwapchainPresentationTargetNotLive,
    TextureUploadSamplePipelineAlreadyOwned,
    NativeWindowGenerationMismatch,
    DrawableExtentMismatch,
    StaleInstanceOwner,
    StaleWindowGeneration,
    ResolutionFailure,
    AllocationFailure
};

struct VulkanTextureUploadSamplePipelineAcquireError
{
    VulkanTextureUploadSamplePipelineAcquireCode mCode = VulkanTextureUploadSamplePipelineAcquireCode::InvalidInstanceOwnerCheck;
    std::optional<VulkanTextureUploadSamplePipelineResolutionError> mResolutionError;

    friend constexpr bool operator==(const VulkanTextureUploadSamplePipelineAcquireError&,
                                     const VulkanTextureUploadSamplePipelineAcquireError&) = default;
};

using VulkanTextureUploadSamplePipelineAcquireResult = std::optional<VulkanTextureUploadSamplePipelineAcquireError>;

struct VulkanUploadDestinationRequest
{
    // These callbacks are synchronous and are not retained. The caller must
    // serialize parent and native-window lifetime changes during acquisition.
    std::uint64_t                 mNativeWindowGeneration = 0;
    VulkanUploadSourceDescription mDescription;
    VulkanInstanceOwnerCheck      mInstanceOwnerCheck;
    VulkanWindowGenerationCheck   mWindowGenerationCheck;
};

enum class VulkanUploadDestinationAcquireCode : std::uint8_t
{
    InvalidInstanceOwnerCheck,
    InvalidWindowGenerationCheck,
    InvalidNativeWindowGeneration,
    InstanceNotLive,
    SurfaceNotLive,
    PresentationDeviceNotLive,
    LogicalDeviceNotLive,
    UploadSourceNotLive,
    UploadDestinationAlreadyOwned,
    NativeWindowGenerationMismatch,
    StaleInstanceOwner,
    StaleWindowGeneration,
    ResolutionFailure,
    AllocationFailure
};

struct VulkanUploadDestinationAcquireError
{
    VulkanUploadDestinationAcquireCode                    mCode = VulkanUploadDestinationAcquireCode::InvalidInstanceOwnerCheck;
    std::optional<VulkanUploadDestinationResolutionError> mResolutionError;

    friend constexpr bool operator==(const VulkanUploadDestinationAcquireError&, const VulkanUploadDestinationAcquireError&) = default;
};

using VulkanUploadDestinationAcquireResult = std::optional<VulkanUploadDestinationAcquireError>;

struct VulkanUploadTransferRequest
{
    // These callbacks are synchronous and are not retained. The caller must
    // serialize parent and native-window lifetime changes during acquisition.
    std::uint64_t                 mNativeWindowGeneration = 0;
    VulkanUploadSourceDescription mDescription;
    VulkanInstanceOwnerCheck      mInstanceOwnerCheck;
    VulkanWindowGenerationCheck   mWindowGenerationCheck;
};

enum class VulkanUploadTransferAcquireCode : std::uint8_t
{
    InvalidInstanceOwnerCheck,
    InvalidWindowGenerationCheck,
    InvalidNativeWindowGeneration,
    InstanceNotLive,
    SurfaceNotLive,
    PresentationDeviceNotLive,
    LogicalDeviceNotLive,
    UploadSourceNotLive,
    UploadDestinationNotLive,
    UploadTransferAlreadyOwned,
    NativeWindowGenerationMismatch,
    StaleInstanceOwner,
    StaleWindowGeneration,
    ResolutionFailure,
    AllocationFailure
};

struct VulkanUploadTransferAcquireError
{
    VulkanUploadTransferAcquireCode                    mCode = VulkanUploadTransferAcquireCode::InvalidInstanceOwnerCheck;
    std::optional<VulkanUploadTransferResolutionError> mResolutionError;

    friend constexpr bool operator==(const VulkanUploadTransferAcquireError&, const VulkanUploadTransferAcquireError&) = default;
};

using VulkanUploadTransferAcquireResult = std::optional<VulkanUploadTransferAcquireError>;

struct VulkanUploadTransferOperationRequest
{
    // Authentication callbacks run before native transfer work. No callback is
    // invoked after command recording or fence waiting begins.
    std::uint64_t                 mNativeWindowGeneration = 0;
    VulkanUploadSourceDescription mDescription;
    VulkanInstanceOwnerCheck      mInstanceOwnerCheck;
    VulkanWindowGenerationCheck   mWindowGenerationCheck;
};

enum class VulkanUploadTransferParentOperationCode : std::uint8_t
{
    InvalidInstanceOwnerCheck,
    InvalidWindowGenerationCheck,
    InvalidNativeWindowGeneration,
    InstanceNotLive,
    SurfaceNotLive,
    PresentationDeviceNotLive,
    LogicalDeviceNotLive,
    UploadSourceNotLive,
    UploadDestinationNotLive,
    UploadTransferNotLive,
    NativeWindowGenerationMismatch,
    StaleInstanceOwner,
    StaleWindowGeneration,
    OperationFailure,
    NativeOperationInProgress
};

struct VulkanUploadTransferParentOperationError
{
    VulkanUploadTransferParentOperationCode           mCode = VulkanUploadTransferParentOperationCode::InvalidInstanceOwnerCheck;
    std::optional<VulkanUploadTransferOperationError> mOperationError;

    friend constexpr bool operator==(const VulkanUploadTransferParentOperationError&,
                                     const VulkanUploadTransferParentOperationError&) = default;
};

using VulkanUploadTransferParentOperationResult = std::variant<VulkanUploadTransferParentOperationError, VulkanUploadTransferDisposition>;

struct VulkanSwapchainConfigurationRequest
{
    // These callbacks are synchronous and are not retained. The caller must
    // serialize parent, native-window, and drawable-geometry changes.
    std::uint64_t               mNativeWindowGeneration = 0;
    VkExtent2D                  mDrawableExtent{};
    VulkanInstanceOwnerCheck    mInstanceOwnerCheck;
    VulkanWindowGenerationCheck mWindowGenerationCheck;
};

enum class VulkanSwapchainConfigurationAcquireCode : std::uint8_t
{
    InvalidInstanceOwnerCheck,
    InvalidWindowGenerationCheck,
    InvalidNativeWindowGeneration,
    InvalidDrawableExtent,
    NativeTeardownInProgress,
    InstanceNotLive,
    SurfaceNotLive,
    PresentationDeviceNotLive,
    LogicalDeviceNotLive,
    SwapchainConfigurationAlreadyOwned,
    NativeWindowGenerationMismatch,
    StaleInstanceOwner,
    StaleWindowGeneration,
    ResolutionFailure,
    AllocationFailure
};

struct VulkanSwapchainConfigurationAcquireError
{
    VulkanSwapchainConfigurationAcquireCode                    mCode = VulkanSwapchainConfigurationAcquireCode::InvalidInstanceOwnerCheck;
    std::optional<VulkanSwapchainConfigurationResolutionError> mResolutionError;

    friend constexpr bool operator==(const VulkanSwapchainConfigurationAcquireError&,
                                     const VulkanSwapchainConfigurationAcquireError&) = default;
};

using VulkanSwapchainConfigurationAcquireResult = std::optional<VulkanSwapchainConfigurationAcquireError>;

struct VulkanSwapchainRequest
{
    // These callbacks are synchronous and are not retained. The caller must
    // serialize parent, native-window, and drawable-geometry changes.
    std::uint64_t               mNativeWindowGeneration = 0;
    VkExtent2D                  mDrawableExtent{};
    VulkanInstanceOwnerCheck    mInstanceOwnerCheck;
    VulkanWindowGenerationCheck mWindowGenerationCheck;
};

enum class VulkanSwapchainAcquireCode : std::uint8_t
{
    InvalidInstanceOwnerCheck,
    InvalidWindowGenerationCheck,
    InvalidNativeWindowGeneration,
    InvalidDrawableExtent,
    InstanceNotLive,
    SurfaceNotLive,
    PresentationDeviceNotLive,
    LogicalDeviceNotLive,
    SwapchainConfigurationNotLive,
    SwapchainAlreadyOwned,
    NativeWindowGenerationMismatch,
    DrawableExtentMismatch,
    StaleInstanceOwner,
    StaleWindowGeneration,
    ResolutionFailure,
    AllocationFailure
};

struct VulkanSwapchainAcquireError
{
    VulkanSwapchainAcquireCode                    mCode = VulkanSwapchainAcquireCode::InvalidInstanceOwnerCheck;
    std::optional<VulkanSwapchainResolutionError> mResolutionError;

    friend constexpr bool operator==(const VulkanSwapchainAcquireError&, const VulkanSwapchainAcquireError&) = default;
};

using VulkanSwapchainAcquireResult = std::optional<VulkanSwapchainAcquireError>;

struct VulkanSwapchainImagesRequest
{
    // These callbacks are synchronous and are not retained. The caller must
    // serialize parent, native-window, and drawable-geometry changes.
    std::uint64_t               mNativeWindowGeneration = 0;
    VkExtent2D                  mDrawableExtent{};
    VulkanInstanceOwnerCheck    mInstanceOwnerCheck;
    VulkanWindowGenerationCheck mWindowGenerationCheck;
};

enum class VulkanSwapchainImagesAcquireCode : std::uint8_t
{
    InvalidInstanceOwnerCheck,
    InvalidWindowGenerationCheck,
    InvalidNativeWindowGeneration,
    InvalidDrawableExtent,
    InstanceNotLive,
    SurfaceNotLive,
    PresentationDeviceNotLive,
    LogicalDeviceNotLive,
    SwapchainConfigurationNotLive,
    SwapchainNotLive,
    SwapchainImagesAlreadyOwned,
    NativeWindowGenerationMismatch,
    DrawableExtentMismatch,
    StaleInstanceOwner,
    StaleWindowGeneration,
    ResolutionFailure,
    AllocationFailure
};

struct VulkanSwapchainImagesAcquireError
{
    VulkanSwapchainImagesAcquireCode                    mCode = VulkanSwapchainImagesAcquireCode::InvalidInstanceOwnerCheck;
    std::optional<VulkanSwapchainImagesResolutionError> mResolutionError;

    friend constexpr bool operator==(const VulkanSwapchainImagesAcquireError&, const VulkanSwapchainImagesAcquireError&) = default;
};

using VulkanSwapchainImagesAcquireResult = std::optional<VulkanSwapchainImagesAcquireError>;

struct VulkanSwapchainPresentationTargetRequest
{
    // These callbacks are synchronous and are not retained. The caller must
    // serialize parent, native-window, and drawable-geometry changes.
    std::uint64_t               mNativeWindowGeneration = 0;
    VkExtent2D                  mDrawableExtent{};
    VulkanInstanceOwnerCheck    mInstanceOwnerCheck;
    VulkanWindowGenerationCheck mWindowGenerationCheck;
};

enum class VulkanSwapchainPresentationTargetAcquireCode : std::uint8_t
{
    InvalidInstanceOwnerCheck,
    InvalidWindowGenerationCheck,
    InvalidNativeWindowGeneration,
    InvalidDrawableExtent,
    NativeTeardownInProgress,
    InstanceNotLive,
    SurfaceNotLive,
    PresentationDeviceNotLive,
    LogicalDeviceNotLive,
    SwapchainConfigurationNotLive,
    SwapchainNotLive,
    SwapchainImagesNotLive,
    SwapchainPresentationTargetAlreadyOwned,
    NativeWindowGenerationMismatch,
    DrawableExtentMismatch,
    StaleInstanceOwner,
    StaleWindowGeneration,
    ResolutionFailure,
    AllocationFailure
};

struct VulkanSwapchainPresentationTargetAcquireError
{
    VulkanSwapchainPresentationTargetAcquireCode                    mCode =
        VulkanSwapchainPresentationTargetAcquireCode::InvalidInstanceOwnerCheck;
    std::optional<VulkanSwapchainPresentationTargetResolutionError> mResolutionError;

    friend constexpr bool operator==(const VulkanSwapchainPresentationTargetAcquireError&,
                                     const VulkanSwapchainPresentationTargetAcquireError&) = default;
};

using VulkanSwapchainPresentationTargetAcquireResult = std::optional<VulkanSwapchainPresentationTargetAcquireError>;

struct VulkanSwapchainPresentationPipelineRequest
{
    // These callbacks are synchronous and are not retained. The caller must
    // serialize parent, native-window, and drawable-geometry changes.
    std::uint64_t               mNativeWindowGeneration = 0;
    VkExtent2D                  mDrawableExtent{};
    VulkanInstanceOwnerCheck    mInstanceOwnerCheck;
    VulkanWindowGenerationCheck mWindowGenerationCheck;
};

enum class VulkanSwapchainPresentationPipelineAcquireCode : std::uint8_t
{
    InvalidInstanceOwnerCheck,
    InvalidWindowGenerationCheck,
    InvalidNativeWindowGeneration,
    InvalidDrawableExtent,
    NativeTeardownInProgress,
    InstanceNotLive,
    SurfaceNotLive,
    PresentationDeviceNotLive,
    LogicalDeviceNotLive,
    SwapchainConfigurationNotLive,
    SwapchainNotLive,
    SwapchainImagesNotLive,
    SwapchainPresentationTargetNotLive,
    SwapchainPresentationPipelineAlreadyOwned,
    NativeWindowGenerationMismatch,
    DrawableExtentMismatch,
    StaleInstanceOwner,
    StaleWindowGeneration,
    ResolutionFailure,
    AllocationFailure
};

struct VulkanSwapchainPresentationPipelineAcquireError
{
    VulkanSwapchainPresentationPipelineAcquireCode mCode = VulkanSwapchainPresentationPipelineAcquireCode::InvalidInstanceOwnerCheck;
    std::optional<VulkanSwapchainPresentationPipelineResolutionError> mResolutionError;

    friend constexpr bool operator==(const VulkanSwapchainPresentationPipelineAcquireError&,
                                     const VulkanSwapchainPresentationPipelineAcquireError&) = default;
};

using VulkanSwapchainPresentationPipelineAcquireResult = std::optional<VulkanSwapchainPresentationPipelineAcquireError>;

struct VulkanSwapchainReadbackRequest
{
    // These callbacks are synchronous and are not retained. The caller must
    // serialize parent, native-window, and drawable-geometry changes.
    std::uint64_t               mNativeWindowGeneration = 0;
    VkExtent2D                  mDrawableExtent{};
    VulkanInstanceOwnerCheck    mInstanceOwnerCheck;
    VulkanWindowGenerationCheck mWindowGenerationCheck;
};

enum class VulkanSwapchainReadbackAcquireCode : std::uint8_t
{
    InvalidInstanceOwnerCheck,
    InvalidWindowGenerationCheck,
    InvalidNativeWindowGeneration,
    InvalidDrawableExtent,
    NativeTeardownInProgress,
    InstanceNotLive,
    SurfaceNotLive,
    PresentationDeviceNotLive,
    LogicalDeviceNotLive,
    SwapchainConfigurationNotLive,
    SwapchainNotLive,
    SwapchainImagesNotLive,
    SwapchainReadbackAlreadyOwned,
    NativeWindowGenerationMismatch,
    DrawableExtentMismatch,
    StaleInstanceOwner,
    StaleWindowGeneration,
    ResolutionFailure,
    AllocationFailure
};

struct VulkanSwapchainReadbackAcquireError
{
    VulkanSwapchainReadbackAcquireCode                    mCode = VulkanSwapchainReadbackAcquireCode::InvalidInstanceOwnerCheck;
    std::optional<VulkanSwapchainReadbackResolutionError> mResolutionError;

    friend constexpr bool operator==(const VulkanSwapchainReadbackAcquireError&, const VulkanSwapchainReadbackAcquireError&) = default;
};

using VulkanSwapchainReadbackAcquireResult = std::optional<VulkanSwapchainReadbackAcquireError>;

struct VulkanSwapchainFrameSlotRequest
{
    // These callbacks are synchronous and are not retained. The caller must
    // serialize parent, native-window, and drawable-geometry changes.
    std::uint64_t               mNativeWindowGeneration = 0;
    VkExtent2D                  mDrawableExtent{};
    VulkanInstanceOwnerCheck    mInstanceOwnerCheck;
    VulkanWindowGenerationCheck mWindowGenerationCheck;
};

enum class VulkanSwapchainFrameSlotAcquireCode : std::uint8_t
{
    InvalidInstanceOwnerCheck,
    InvalidWindowGenerationCheck,
    InvalidNativeWindowGeneration,
    InvalidDrawableExtent,
    NativeTeardownInProgress,
    InstanceNotLive,
    SurfaceNotLive,
    PresentationDeviceNotLive,
    LogicalDeviceNotLive,
    SwapchainConfigurationNotLive,
    SwapchainNotLive,
    SwapchainImagesNotLive,
    SwapchainFrameSlotAlreadyOwned,
    NativeWindowGenerationMismatch,
    DrawableExtentMismatch,
    StaleInstanceOwner,
    StaleWindowGeneration,
    ResolutionFailure,
    AllocationFailure
};

struct VulkanSwapchainFrameSlotAcquireError
{
    VulkanSwapchainFrameSlotAcquireCode                    mCode = VulkanSwapchainFrameSlotAcquireCode::InvalidInstanceOwnerCheck;
    std::optional<VulkanSwapchainFrameSlotResolutionError> mResolutionError;

    friend constexpr bool operator==(const VulkanSwapchainFrameSlotAcquireError&, const VulkanSwapchainFrameSlotAcquireError&) = default;
};

using VulkanSwapchainFrameSlotAcquireResult = std::optional<VulkanSwapchainFrameSlotAcquireError>;

struct VulkanSwapchainFrameSlotOperationRequest
{
    // These callbacks are synchronous and are not retained. The caller must
    // serialize parent, queue, native-window, and drawable-geometry changes.
    std::uint64_t               mNativeWindowGeneration = 0;
    VkExtent2D                  mDrawableExtent{};
    VulkanInstanceOwnerCheck    mInstanceOwnerCheck;
    VulkanWindowGenerationCheck mWindowGenerationCheck;
};

enum class VulkanSwapchainFrameSlotParentOperationCode : std::uint8_t
{
    InvalidInstanceOwnerCheck,
    InvalidWindowGenerationCheck,
    InvalidNativeWindowGeneration,
    InvalidDrawableExtent,
    InstanceNotLive,
    SurfaceNotLive,
    PresentationDeviceNotLive,
    LogicalDeviceNotLive,
    SwapchainConfigurationNotLive,
    SwapchainNotLive,
    SwapchainImagesNotLive,
    SwapchainFrameSlotNotLive,
    NativeWindowGenerationMismatch,
    DrawableExtentMismatch,
    StaleInstanceOwner,
    StaleWindowGeneration,
    OperationFailure,
    InvalidClearColor,
    SwapchainPresentationTargetNotLive,
    SwapchainPresentationPipelineNotLive,
    SwapchainReadbackNotLive,
    UploadDestinationNotLive,
    TextureUploadDestinationNotLive,
    TextureUploadSampleBindingNotLive,
    TextureUploadSamplePipelineNotLive
};

struct VulkanSwapchainFrameSlotParentOperationError
{
    VulkanSwapchainFrameSlotParentOperationCode           mCode = VulkanSwapchainFrameSlotParentOperationCode::InvalidInstanceOwnerCheck;
    std::optional<VulkanSwapchainFrameSlotOperationError> mOperationError;

    friend constexpr bool operator==(const VulkanSwapchainFrameSlotParentOperationError&,
                                     const VulkanSwapchainFrameSlotParentOperationError&) = default;
};

using VulkanSwapchainFrameSlotParentOperationResult =
    std::variant<VulkanSwapchainFrameSlotParentOperationError, VulkanSwapchainFrameSlotDisposition>;
using VulkanSwapchainFrameSlotParentPresentationResult =
    std::variant<VulkanSwapchainFrameSlotParentOperationError, VulkanSwapchainFrameSlotPresentationSuccess>;

struct VulkanSwapchainChainRebuildRequest
{
    // A missing sample reports a platform geometry-query failure. A present
    // extent with either dimension zero is a valid suspended-window sample.
    // These callbacks are synchronous and are not retained. The caller must
    // serialize parent, queue, native-window, and drawable-geometry changes.
    std::uint64_t               mNativeWindowGeneration = 0;
    std::optional<VkExtent2D>   mDrawableExtent;
    VulkanInstanceOwnerCheck    mInstanceOwnerCheck;
    VulkanWindowGenerationCheck mWindowGenerationCheck;
};

enum class VulkanSwapchainChainRebuildOutcome : std::uint8_t
{
    Ready,
    Suspended
};

enum class VulkanSwapchainChainRebuildPhase : std::uint8_t
{
    Preflight,
    Retirement,
    Configuration,
    Swapchain,
    Images,
    FrameSlot,
    FinalFreshness,
    PresentationTarget,
    PresentationPipeline,
    Readback
};

enum class VulkanSwapchainChainRebuildCode : std::uint8_t
{
    InvalidInstanceOwnerCheck,
    InvalidWindowGenerationCheck,
    InvalidNativeWindowGeneration,
    InvalidDrawableExtent,
    InstanceNotLive,
    SurfaceNotLive,
    PresentationDeviceNotLive,
    LogicalDeviceNotLive,
    NativeWindowGenerationMismatch,
    NativeAcquisitionInProgress,
    StaleInstanceOwner,
    StaleWindowGeneration,
    FrameSlotResetRefused,
    DeviceRecoveryRequired,
    ChildFailure,
    PublicationFailure,
    RollbackFailure
};

using VulkanSwapchainChainRebuildChildError = std::variant<std::monostate,
                                                           VulkanSwapchainConfigurationAcquireError,
                                                           VulkanSwapchainAcquireError,
                                                           VulkanSwapchainImagesAcquireError,
                                                           VulkanSwapchainFrameSlotAcquireError,
                                                           VulkanSwapchainPresentationTargetAcquireError,
                                                           VulkanSwapchainPresentationPipelineAcquireError,
                                                           VulkanSwapchainReadbackAcquireError>;

struct VulkanSwapchainChainRebuildError
{
    VulkanSwapchainChainRebuildCode                    mCode  = VulkanSwapchainChainRebuildCode::InvalidInstanceOwnerCheck;
    VulkanSwapchainChainRebuildPhase                   mPhase = VulkanSwapchainChainRebuildPhase::Preflight;
    VulkanSwapchainChainRebuildChildError              mChildError;
    std::optional<VulkanSwapchainFrameSlotDisposition> mFrameSlotDisposition;

    friend constexpr bool operator==(const VulkanSwapchainChainRebuildError&,
                                     const VulkanSwapchainChainRebuildError&) = default;
};

using VulkanSwapchainChainRebuildResult =
    std::variant<VulkanSwapchainChainRebuildError, VulkanSwapchainChainRebuildOutcome>;

// This generation owns the Vulkan objects but borrows the loader behind the
// originating window's resolver. It must be reset before that window destroys
// its requirements generation or releases its loader references. If it owns a
// frame slot, callers must externally serialize host access and ensure no
// operation still uses a slot fence or semaphore, its command buffer is not
// pending, and no swapchain image remains acquired before destruction or any
// reset that can transitively destroy it. Destruction while the frame-slot
// disposition names an acquired or pending obligation violates the caller
// contract. An upload transfer must likewise leave Pending through successful
// completion retry before aggregate destruction; its queue work can still use
// the transfer, destination, and source. The bool reset APIs defensively retain
// the live owner for those obligations and while an unpublished native
// acquisition candidate exists.
class VulkanInstanceGeneration
{
public:
    struct ValidationState;

    ~VulkanInstanceGeneration() noexcept;

    VulkanInstanceGeneration(const VulkanInstanceGeneration&)            = delete;
    VulkanInstanceGeneration& operator=(const VulkanInstanceGeneration&) = delete;
    // A move attempted from a generation guarded by an in-progress native
    // acquisition leaves that source intact and produces an empty destination.
    VulkanInstanceGeneration(VulkanInstanceGeneration&& other) noexcept;
    VulkanInstanceGeneration& operator=(VulkanInstanceGeneration&&) = delete;

    VkInstance                        instance() const noexcept { return mInstance; }
    std::uint32_t                     apiVersion() const noexcept { return RENDERER_VULKAN_API_VERSION; }
    std::uint64_t                     nativeWindowGeneration() const noexcept { return mNativeWindowGeneration; }
    bool                              validationEnabled() const noexcept { return mDebugMessenger != VK_NULL_HANDLE; }
    bool                              portabilityEnumerationEnabled() const noexcept { return mPortabilityEnumeration; }
    const std::vector<std::string>&   enabledExtensions() const noexcept { return mEnabledExtensions; }
    const std::vector<std::string>&   enabledLayers() const noexcept { return mEnabledLayers; }
    bool                              isExtensionEnabled(std::string_view extension_name) const noexcept;
    VulkanValidationSnapshot          validationSnapshot() const noexcept;
    bool                              hasSurfaceGeneration() const noexcept;
    VkSurfaceKHR                      surface() const noexcept;
    std::uint64_t                     surfaceNativeWindowGeneration() const noexcept;
    bool                              hasPresentationDeviceGeneration() const noexcept;
    VkPhysicalDevice                  physicalDevice() const noexcept;
    std::uint32_t                     physicalDeviceIndex() const noexcept;
    VkPhysicalDeviceProperties        physicalDeviceProperties() const noexcept;
    std::uint32_t                     presentationQueueFamilyIndex() const noexcept;
    VkQueueFamilyProperties           presentationQueueFamilyProperties() const noexcept;
    std::span<const std::string_view> requiredDeviceExtensions() const noexcept;
    bool                              swapchainMaintenance1Supported() const noexcept;
    bool                              portabilitySubsetRequired() const noexcept;
    bool                              hasLogicalDeviceGeneration() const noexcept;
    VkDevice                          logicalDevice() const noexcept;
    VkQueue                           presentationQueue() const noexcept;
    VkPhysicalDevice                  logicalDevicePhysicalDevice() const noexcept;
    std::uint32_t                     logicalDeviceQueueFamilyIndex() const noexcept;
    std::uint32_t                     logicalDeviceQueueIndex() const noexcept;
    VkPhysicalDeviceFeatures          logicalDeviceEnabledFeatures() const noexcept;
    std::span<const std::string_view> enabledDeviceExtensions() const noexcept;
    bool                              swapchainMaintenance1Enabled() const noexcept;
    bool                              portabilitySubsetEnabled() const noexcept;
    bool                              hasUploadSourceGeneration() const noexcept;
    LLRenderContract::BufferHandle    uploadSourceResourceHandle() const noexcept;
    std::uint64_t                     uploadSourceContentIdentity() const noexcept;
    VkBuffer                          uploadSourceBuffer() const noexcept;
    VkDeviceMemory                    uploadSourceMemory() const noexcept;
    VkDeviceSize                      uploadSourceByteCount() const noexcept;
    VkDeviceSize                      uploadSourceAllocationSize() const noexcept;
    std::uint32_t                     uploadSourceMemoryTypeIndex() const noexcept;
    VkMemoryPropertyFlags             uploadSourceMemoryPropertyFlags() const noexcept;
    bool                              uploadSourceIsCoherent() const noexcept;
    bool                              hasTextureUploadDestinationGeneration() const noexcept;
    LLRenderContract::ImageHandle     textureUploadDestinationResourceHandle() const noexcept;
    std::uint64_t                     textureUploadDestinationExpectedRevision() const noexcept;
    VkExtent3D                        textureUploadDestinationResidentExtent() const noexcept;
    LLRenderContract::Extent2D        textureUploadDestinationLogicalExtent() const noexcept;
    std::uint32_t                     textureUploadDestinationResidentDiscard() const noexcept;
    LLRenderContract::PixelFormat     textureUploadDestinationPixelFormat() const noexcept;
    LLRenderContract::ImageState      textureUploadDestinationInitialState() const noexcept;
    VkImageCreateFlags                textureUploadDestinationFlags() const noexcept;
    VkImageType                       textureUploadDestinationImageType() const noexcept;
    VkFormat                          textureUploadDestinationFormat() const noexcept;
    std::uint32_t                     textureUploadDestinationMipLevels() const noexcept;
    std::uint32_t                     textureUploadDestinationArrayLayers() const noexcept;
    VkSampleCountFlagBits             textureUploadDestinationSamples() const noexcept;
    VkImageTiling                     textureUploadDestinationTiling() const noexcept;
    VkImageUsageFlags                 textureUploadDestinationUsage() const noexcept;
    VkSharingMode                     textureUploadDestinationSharingMode() const noexcept;
    VkImageLayout                     textureUploadDestinationInitialLayout() const noexcept;
    VkFormatFeatureFlags              textureUploadDestinationFormatFeatures() const noexcept;
    VkImageFormatProperties           textureUploadDestinationImageFormatProperties() const noexcept;
    VkImage                           textureUploadDestinationImage() const noexcept;
    VkDeviceMemory                    textureUploadDestinationMemory() const noexcept;
    VkMemoryRequirements              textureUploadDestinationMemoryRequirements() const noexcept;
    VkDeviceSize                      textureUploadDestinationAllocationSize() const noexcept;
    VkDeviceSize                      textureUploadDestinationAllocationAlignment() const noexcept;
    std::uint32_t                     textureUploadDestinationCompatibleMemoryTypeBits() const noexcept;
    std::uint32_t                     textureUploadDestinationMemoryTypeIndex() const noexcept;
    VkMemoryPropertyFlags             textureUploadDestinationMemoryPropertyFlags() const noexcept;
    bool                              textureUploadDestinationIsDeviceLocal() const noexcept;
    bool                              textureUploadDestinationPrefersDedicatedAllocation() const noexcept;
    bool                              textureUploadDestinationRequiresDedicatedAllocation() const noexcept;
    VkImageView                       textureUploadDestinationImageView() const noexcept;
    VkImageViewType                   textureUploadDestinationImageViewType() const noexcept;
    VkImageSubresourceRange           textureUploadDestinationViewRange() const noexcept;
    bool                                                  textureUploadDestinationIsResident() const noexcept;
    std::uint64_t                                         textureUploadDestinationResidentRevision() const noexcept;
    std::uint64_t                                         textureUploadDestinationResidentContentIdentity() const noexcept;
    LLRenderContract::ImageState                          textureUploadDestinationCurrentState() const noexcept;
    bool                                           hasTextureUploadSourceGeneration() const noexcept;
    LLRenderContract::ImageHandle                  textureUploadSourceResourceHandle() const noexcept;
    std::uint64_t                                  textureUploadSourceExpectedRevision() const noexcept;
    LLRenderContract::Extent2D                     textureUploadSourceResidentExtent() const noexcept;
    LLRenderContract::PixelFormat                  textureUploadSourcePixelFormat() const noexcept;
    std::uint32_t                                  textureUploadSourceRowPitch() const noexcept;
    LLRenderContract::RowOrigin                    textureUploadSourceRowOrigin() const noexcept;
    std::uint64_t                                  textureUploadSourceContentIdentity() const noexcept;
    VkBufferCreateFlags                            textureUploadSourceFlags() const noexcept;
    VkBufferUsageFlags                             textureUploadSourceUsage() const noexcept;
    VkSharingMode                                  textureUploadSourceSharingMode() const noexcept;
    VkBuffer                                       textureUploadSourceBuffer() const noexcept;
    VkDeviceMemory                                 textureUploadSourceMemory() const noexcept;
    VkDeviceSize                                   textureUploadSourceByteCount() const noexcept;
    VkDeviceSize                                   textureUploadSourceAllocationSize() const noexcept;
    std::uint32_t                                  textureUploadSourceMemoryTypeIndex() const noexcept;
    VkMemoryPropertyFlags                          textureUploadSourceMemoryPropertyFlags() const noexcept;
    bool                                           textureUploadSourceIsCoherent() const noexcept;
    bool                                                  hasTextureUploadTransferGeneration() const noexcept;
    LLRenderContract::ImageHandle                         textureUploadTransferResourceHandle() const noexcept;
    std::uint64_t                                         textureUploadTransferExpectedRevision() const noexcept;
    std::uint64_t                                         textureUploadTransferContentIdentity() const noexcept;
    VkBuffer                                              textureUploadTransferSourceBuffer() const noexcept;
    VkImage                                               textureUploadTransferDestinationImage() const noexcept;
    VkQueue                                               textureUploadTransferQueue() const noexcept;
    std::uint32_t                                         textureUploadTransferQueueFamilyIndex() const noexcept;
    std::uint32_t                                         textureUploadTransferQueueIndex() const noexcept;
    VkCommandPool                                         textureUploadTransferCommandPool() const noexcept;
    VkCommandBuffer                                       textureUploadTransferCommandBuffer() const noexcept;
    VkFence                                               textureUploadTransferFence() const noexcept;
    std::uint32_t                                         textureUploadTransferSubmissionCount() const noexcept;
    std::uint32_t                                         textureUploadTransferCompletionWaitCount() const noexcept;
    std::optional<VulkanTextureUploadTransferDisposition> textureUploadTransferDisposition() const noexcept;
    bool                                                  hasTextureUploadSampleBindingGeneration() const noexcept;
    LLRenderContract::SamplerHandle                       textureUploadSampleBindingSamplerResourceHandle() const noexcept;
    LLRenderContract::ImageHandle                         textureUploadSampleBindingDestinationResourceHandle() const noexcept;
    std::uint64_t                                         textureUploadSampleBindingExpectedRevision() const noexcept;
    std::uint64_t                                         textureUploadSampleBindingResidentRevision() const noexcept;
    std::uint64_t                                         textureUploadSampleBindingResidentContentIdentity() const noexcept;
    VkImageView                                           textureUploadSampleBindingDestinationImageView() const noexcept;
    VkImageLayout                                         textureUploadSampleBindingDestinationImageLayout() const noexcept;
    std::uint32_t                                         textureUploadSampleBindingDescriptorSetIndex() const noexcept;
    std::uint32_t                                         textureUploadSampleBindingBinding() const noexcept;
    VkSampler                                             textureUploadSampleBindingSampler() const noexcept;
    VkDescriptorSetLayout                                 textureUploadSampleBindingDescriptorSetLayout() const noexcept;
    VkPipelineLayout                                      textureUploadSampleBindingPipelineLayout() const noexcept;
    VkDescriptorPool                                      textureUploadSampleBindingDescriptorPool() const noexcept;
    VkDescriptorSet                                       textureUploadSampleBindingDescriptorSet() const noexcept;
    bool                                                  hasTextureUploadSamplePipelineGeneration() const noexcept;
    LLRenderContract::PipelineHandle                      textureUploadSamplePipelineResourceHandle() const noexcept;
    VkPipelineLayout                                      textureUploadSamplePipelineLayout() const noexcept;
    VkPipeline                                            textureUploadSamplePipeline() const noexcept;
    bool                                                  hasUploadDestinationGeneration() const noexcept;
    LLRenderContract::BufferHandle                        uploadDestinationResourceHandle() const noexcept;
    std::uint64_t                                         uploadDestinationExpectedContentIdentity() const noexcept;
    std::uint64_t                                         uploadDestinationResidentContentIdentity() const noexcept;
    bool                                                  uploadDestinationIsResident() const noexcept;
    VkBuffer                                              uploadDestinationBuffer() const noexcept;
    VkDeviceMemory                                 uploadDestinationMemory() const noexcept;
    VkDeviceSize                                   uploadDestinationByteCount() const noexcept;
    VkBufferUsageFlags                             uploadDestinationUsage() const noexcept;
    VkDeviceSize                                   uploadDestinationAllocationSize() const noexcept;
    std::uint32_t                                  uploadDestinationMemoryTypeIndex() const noexcept;
    VkMemoryPropertyFlags                          uploadDestinationMemoryPropertyFlags() const noexcept;
    bool                                           uploadDestinationIsDeviceLocal() const noexcept;
    bool                                           uploadDestinationIsMapped() const noexcept;
    bool                                           hasUploadTransferGeneration() const noexcept;
    LLRenderContract::BufferHandle                 uploadTransferResourceHandle() const noexcept;
    std::uint64_t                                  uploadTransferContentIdentity() const noexcept;
    VkBuffer                                       uploadTransferSourceBuffer() const noexcept;
    VkBuffer                                       uploadTransferDestinationBuffer() const noexcept;
    VkQueue                                        uploadTransferQueue() const noexcept;
    std::uint32_t                                  uploadTransferQueueFamilyIndex() const noexcept;
    std::uint32_t                                  uploadTransferQueueIndex() const noexcept;
    VkCommandPool                                  uploadTransferCommandPool() const noexcept;
    VkCommandBuffer                                uploadTransferCommandBuffer() const noexcept;
    VkFence                                        uploadTransferFence() const noexcept;
    std::uint32_t                                  uploadTransferSubmissionCount() const noexcept;
    std::uint32_t                                  uploadTransferCompletionWaitCount() const noexcept;
    std::optional<VulkanUploadTransferDisposition> uploadTransferDisposition() const noexcept;
    bool                              hasSwapchainConfigurationGeneration() const noexcept;
    VkExtent2D                        swapchainDrawableExtent() const noexcept;
    VkSurfaceCapabilitiesKHR          swapchainSurfaceCapabilities() const noexcept;
    VkSurfaceFormatKHR                swapchainSurfaceFormat() const noexcept;
    VkPresentModeKHR                  swapchainPresentMode() const noexcept;
    std::uint32_t                     swapchainImageCount() const noexcept;
    VkExtent2D                        swapchainImageExtent() const noexcept;
    std::uint32_t                     swapchainImageArrayLayers() const noexcept;
    VkImageUsageFlags                 swapchainImageUsage() const noexcept;
    VkSharingMode                     swapchainImageSharingMode() const noexcept;
    VkSurfaceTransformFlagBitsKHR     swapchainPreTransform() const noexcept;
    VkCompositeAlphaFlagBitsKHR       swapchainCompositeAlpha() const noexcept;
    VkBool32                          swapchainClipped() const noexcept;
    bool                              hasSwapchainGeneration() const noexcept;
    VkSwapchainKHR                    swapchain() const noexcept;
    VkDevice                          swapchainDevice() const noexcept;
    VkSurfaceKHR                      swapchainSurface() const noexcept;
    bool                              hasSwapchainImagesGeneration() const noexcept;
    std::uint32_t                     resolvedSwapchainImageCount() const noexcept;
    VkImage                           swapchainImage(std::uint32_t index) const noexcept;
    VkImageView                       swapchainImageView(std::uint32_t index) const noexcept;
    bool                              hasSwapchainPresentationTargetGeneration() const noexcept;
    VkRenderPass                      swapchainPresentationRenderPass() const noexcept;
    std::uint32_t                     swapchainPresentationFramebufferCount() const noexcept;
    VkFramebuffer                     swapchainPresentationFramebuffer(std::uint32_t index) const noexcept;
    bool                              hasSwapchainPresentationPipelineGeneration() const noexcept;
    VkPipelineLayout                  swapchainPresentationPipelineLayout() const noexcept;
    VkPipeline                        swapchainPresentationPipeline() const noexcept;
    bool                              hasSwapchainReadbackGeneration() const noexcept;
    VkBuffer                          swapchainReadbackBuffer() const noexcept;
    VkDeviceMemory                    swapchainReadbackMemory() const noexcept;
    bool                              swapchainReadbackIsMapped() const noexcept;
    VkFormat                          swapchainReadbackImageFormat() const noexcept;
    VkExtent2D                        swapchainReadbackImageExtent() const noexcept;
    std::uint32_t                     swapchainReadbackImageCount() const noexcept;
    VkDeviceSize                      swapchainReadbackRowBytes() const noexcept;
    VkDeviceSize                      swapchainReadbackByteCount() const noexcept;
    VkDeviceSize                      swapchainReadbackAllocationSize() const noexcept;
    std::uint32_t                     swapchainReadbackMemoryTypeIndex() const noexcept;
    VkMemoryPropertyFlags             swapchainReadbackMemoryPropertyFlags() const noexcept;
    bool                              hasSwapchainFrameSlotGeneration() const noexcept;
    bool setRenderPassRecorder(VulkanSwapchainFrameSlotGeneration::RenderPassRecorder recorder) noexcept;
    VkCommandPool                     swapchainFrameCommandPool() const noexcept;
    VkCommandBuffer                   swapchainFrameCommandBuffer() const noexcept;
    VkSemaphore                       swapchainFrameImageAvailableSemaphore() const noexcept;
    VkSemaphore                       swapchainFramePresentationReadySemaphore() const noexcept;
    VkFence                           swapchainFrameSubmissionFence() const noexcept;
    VkFence                           swapchainFramePresentCompletionFence() const noexcept;
    std::optional<std::uint32_t>      swapchainFrameAcquiredImageIndex() const noexcept;

    std::optional<VulkanSwapchainFrameSlotDisposition> swapchainFrameSlotDisposition() const noexcept;

    VulkanSurfaceAcquireResult                acquireSurfaceGeneration(const VulkanSurfaceRequest& request) noexcept;
    VulkanPresentationDeviceAcquireResult     acquirePresentationDeviceGeneration(const VulkanPresentationDeviceRequest& request) noexcept;
    VulkanLogicalDeviceAcquireResult          acquireLogicalDeviceGeneration(const VulkanLogicalDeviceRequest& request) noexcept;
    VulkanUploadSourceAcquireResult           acquireUploadSourceGeneration(const VulkanUploadSourceRequest& request) noexcept;
    VulkanTextureUploadDestinationAcquireResult acquireTextureUploadDestinationGeneration(
        const VulkanTextureUploadDestinationRequest& request) noexcept;
    VulkanTextureUploadSourceAcquireResult acquireTextureUploadSourceGeneration(const VulkanTextureUploadSourceRequest& request) noexcept;
    VulkanTextureUploadTransferAcquireResult acquireTextureUploadTransferGeneration(
        const VulkanTextureUploadTransferRequest& request) noexcept;
    VulkanTextureUploadTransferParentOperationResult executeTextureUploadTransfer(
        const VulkanTextureUploadTransferOperationRequest& request) noexcept;
    VulkanTextureUploadTransferParentOperationResult retryTextureUploadTransferCompletion(
        const VulkanTextureUploadTransferOperationRequest& request) noexcept;
    VulkanTextureUploadSampleBindingAcquireResult acquireTextureUploadSampleBindingGeneration(
        const VulkanTextureUploadSampleBindingRequest& request) noexcept;
    VulkanTextureUploadSamplePipelineAcquireResult acquireTextureUploadSamplePipelineGeneration(
        const VulkanTextureUploadSamplePipelineRequest& request) noexcept;
    VulkanUploadDestinationAcquireResult      acquireUploadDestinationGeneration(const VulkanUploadDestinationRequest& request) noexcept;
    VulkanUploadTransferAcquireResult         acquireUploadTransferGeneration(const VulkanUploadTransferRequest& request) noexcept;
    VulkanUploadTransferParentOperationResult executeUploadTransfer(const VulkanUploadTransferOperationRequest& request) noexcept;
    VulkanUploadTransferParentOperationResult retryUploadTransferCompletion(const VulkanUploadTransferOperationRequest& request) noexcept;
    VulkanSwapchainConfigurationAcquireResult acquireSwapchainConfigurationGeneration(
        const VulkanSwapchainConfigurationRequest& request) noexcept;
    VulkanSwapchainAcquireResult       acquireSwapchainGeneration(const VulkanSwapchainRequest& request) noexcept;
    VulkanSwapchainImagesAcquireResult acquireSwapchainImagesGeneration(const VulkanSwapchainImagesRequest& request) noexcept;
    VulkanSwapchainPresentationTargetAcquireResult acquireSwapchainPresentationTargetGeneration(
        const VulkanSwapchainPresentationTargetRequest& request) noexcept;
    VulkanSwapchainPresentationPipelineAcquireResult acquireSwapchainPresentationPipelineGeneration(
        const VulkanSwapchainPresentationPipelineRequest& request) noexcept;
    VulkanSwapchainReadbackAcquireResult  acquireSwapchainReadbackGeneration(const VulkanSwapchainReadbackRequest& request) noexcept;
    VulkanSwapchainFrameSlotAcquireResult acquireSwapchainFrameSlotGeneration(const VulkanSwapchainFrameSlotRequest& request) noexcept;
    VulkanSwapchainChainRebuildResult     rebuildSwapchainChain(const VulkanSwapchainChainRebuildRequest& request) noexcept;
    VulkanSwapchainFrameSlotParentOperationResult roundTripEmptySwapchainFrameSlot(
        const VulkanSwapchainFrameSlotOperationRequest& request) noexcept;
    VulkanSwapchainFrameSlotParentOperationResult retryEmptySwapchainFrameSlotCompletion(
        const VulkanSwapchainFrameSlotOperationRequest& request) noexcept;
    VulkanSwapchainFrameSlotParentPresentationResult acquireToPresentSwapchainFrameSlot(
        const VulkanSwapchainFrameSlotOperationRequest& request) noexcept;
    VulkanSwapchainFrameSlotParentPresentationResult acquireClearToPresentSwapchainFrameSlot(
        const VulkanSwapchainFrameSlotOperationRequest& request,
        const VulkanSwapchainFrameClearColor&           clear_color) noexcept;
    VulkanSwapchainFrameSlotParentPresentationResult acquireRenderPassClearToPresentSwapchainFrameSlot(
        const VulkanSwapchainFrameSlotOperationRequest& request,
        const VulkanSwapchainFrameClearColor&           clear_color) noexcept;
    VulkanSwapchainFrameSlotParentPresentationResult acquireRenderPassDrawToPresentSwapchainFrameSlot(
        const VulkanSwapchainFrameSlotOperationRequest& request,
        const VulkanSwapchainFrameClearColor&           clear_color) noexcept;
    VulkanSwapchainFrameSlotParentPresentationResult acquireRenderPassDrawReadbackToPresentSwapchainFrameSlot(
        const VulkanSwapchainFrameSlotOperationRequest& request) noexcept;
    VulkanSwapchainFrameSlotParentPresentationResult acquireRenderPassSampleDrawToPresentSwapchainFrameSlot(
        const VulkanSwapchainFrameSlotOperationRequest& request) noexcept;
    VulkanSwapchainFrameSlotParentPresentationResult retrySwapchainFrameSlotPresentation(
        const VulkanSwapchainFrameSlotOperationRequest& request) noexcept;
    VulkanSwapchainFrameSlotParentPresentationResult retrySwapchainFrameSlotPresentationCompletion(
        const VulkanSwapchainFrameSlotOperationRequest& request) noexcept;
    VulkanSwapchainFrameSlotParentOperationResult cancelSwapchainFrameSlotPresentation(
        const VulkanSwapchainFrameSlotOperationRequest& request) noexcept;
    VulkanSwapchainFrameSlotParentOperationResult retrySwapchainFrameSlotCancellationCompletion(
        const VulkanSwapchainFrameSlotOperationRequest& request) noexcept;
    // Callers externally serialize these resets. Upload resources are
    // logical-device children independent of drawable and swapchain lifetime.
    // A pending transfer refuses direct and transitive reset without mutation.
    // Destination and source reset first retire a resettable transfer. Either
    // direct resource reset preserves the other resource and the swapchain.
    // The texture source depends on the texture destination, while the sampled
    // binding retains only a completed destination. Direct source and transfer
    // reset preserve the binding. Destination reset preflights a pending
    // transfer, then retires the binding, terminal transfer, source, and image.
    // None of these texture owners belongs to the swapchain chain.
    // Image teardown retires the
    // frame slot, readback destination, presentation pipeline, and presentation
    // target in that order. Direct readback reset preserves every presentation
    // sibling, but refuses while the frame slot retains it for an observation;
    // direct pipeline or target reset preserves readback. False preserves the
    // requested owner and every parent; it reports an outstanding frame
    // obligation, retained observation, or in-progress native child acquisition.
    // The latter refuses even an otherwise idempotent reset.
    bool resetSwapchainFrameSlotGeneration() noexcept;
    bool resetSwapchainReadbackGeneration() noexcept;
    bool resetSwapchainPresentationPipelineGeneration() noexcept;
    bool resetSwapchainPresentationTargetGeneration() noexcept;
    bool resetSwapchainImagesGeneration() noexcept;
    bool resetSwapchainGeneration() noexcept;
    bool resetSwapchainConfigurationGeneration() noexcept;
    bool resetUploadTransferGeneration() noexcept;
    bool resetUploadDestinationGeneration() noexcept;
    bool resetUploadSourceGeneration() noexcept;
    bool resetTextureUploadSourceGeneration() noexcept;
    bool resetTextureUploadTransferGeneration() noexcept;
    bool resetTextureUploadSamplePipelineGeneration() noexcept;
    bool resetTextureUploadSampleBindingGeneration() noexcept;
    bool resetTextureUploadDestinationGeneration() noexcept;
    bool resetLogicalDeviceGeneration() noexcept;
    bool resetPresentationDeviceGeneration() noexcept;
    bool resetSurfaceGeneration() noexcept;

    bool reset() noexcept;

private:
    friend struct VulkanInstanceGenerationFactory;
    friend struct VulkanInstanceGenerationTestAccess;

    class NativeAcquisitionGuard;
    class TextureUploadDestinationTeardownGuard;
    class TextureUploadSourceTeardownGuard;
    class TextureUploadTransferTeardownGuard;
    class TextureUploadSampleBindingTeardownGuard;
    class TextureUploadSamplePipelineTeardownGuard;
    class SwapchainPresentationTargetTeardownGuard;
    class VulkanSurfaceGeneration;

    VulkanInstanceGeneration(VulkanGlobalDispatchGeneration&&    global_dispatch,
                             std::unique_ptr<ValidationState>&&  validation_state,
                             std::vector<std::string>&&          enabled_extensions,
                             std::vector<std::string>&&          enabled_layers,
                             std::uint64_t                       native_window_generation,
                             bool                                portability_enumeration,
                             VkInstance                          instance,
                             PFN_vkDestroyInstance               destroy_instance,
                             VkDebugUtilsMessengerEXT            debug_messenger,
                             PFN_vkDestroyDebugUtilsMessengerEXT destroy_debug_messenger) noexcept;

    void noteOwnershipTransition() noexcept { ++mOwnershipTransitionEpoch; }
    void noteUploadSourceTransition() noexcept
    {
        ++mUploadSourceEpoch;
        noteOwnershipTransition();
    }
    void noteTextureUploadDestinationTransition() noexcept
    {
        ++mTextureUploadDestinationEpoch;
        noteOwnershipTransition();
    }
    void noteTextureUploadSourceTransition() noexcept
    {
        ++mTextureUploadSourceEpoch;
        noteOwnershipTransition();
    }
    void noteTextureUploadTransferTransition() noexcept
    {
        ++mTextureUploadTransferEpoch;
        noteOwnershipTransition();
    }
    void noteTextureUploadSampleBindingTransition() noexcept
    {
        ++mTextureUploadSampleBindingEpoch;
        noteOwnershipTransition();
    }
    void noteTextureUploadSamplePipelineTransition() noexcept
    {
        ++mTextureUploadSamplePipelineEpoch;
        noteOwnershipTransition();
    }
    void noteUploadDestinationTransition() noexcept
    {
        ++mUploadDestinationEpoch;
        noteOwnershipTransition();
    }
    void noteUploadTransferTransition() noexcept
    {
        ++mUploadTransferEpoch;
        noteOwnershipTransition();
    }
    void noteSwapchainPresentationTargetTransition() noexcept
    {
        ++mSwapchainPresentationTargetEpoch;
        noteOwnershipTransition();
    }
    void noteSwapchainPresentationPipelineTransition() noexcept
    {
        ++mSwapchainPresentationPipelineEpoch;
        noteOwnershipTransition();
    }
    void noteSwapchainReadbackTransition() noexcept
    {
        ++mSwapchainReadbackEpoch;
        noteOwnershipTransition();
    }
    void noteSwapchainFrameSlotTransition() noexcept
    {
        ++mSwapchainFrameSlotEpoch;
        noteOwnershipTransition();
    }

    std::optional<VulkanGlobalDispatchGeneration>           mGlobalDispatch;
    std::unique_ptr<ValidationState>                        mValidationState;
    std::vector<std::string>                                mEnabledExtensions;
    std::vector<std::string>                                mEnabledLayers;
    std::uint64_t                                           mNativeWindowGeneration = 0;
    bool                                                    mPortabilityEnumeration = false;
    VkInstance                                              mInstance               = VK_NULL_HANDLE;
    PFN_vkDestroyInstance                                   mDestroyInstance        = nullptr;
    VkDebugUtilsMessengerEXT                                mDebugMessenger         = VK_NULL_HANDLE;
    PFN_vkDestroyDebugUtilsMessengerEXT                     mDestroyDebugMessenger  = nullptr;
    std::unique_ptr<VulkanSurfaceGeneration>                       mSurfaceGeneration;
    std::unique_ptr<VulkanPhysicalDeviceGeneration>                mPresentationDeviceGeneration;
    std::unique_ptr<VulkanLogicalDeviceGeneration>                 mLogicalDeviceGeneration;
    std::unique_ptr<VulkanTextureUploadDestinationGeneration>      mTextureUploadDestinationGeneration;
    std::unique_ptr<VulkanTextureUploadSourceGeneration>           mTextureUploadSourceGeneration;
    std::unique_ptr<VulkanTextureUploadTransferGeneration>         mTextureUploadTransferGeneration;
    std::unique_ptr<VulkanTextureUploadSampleBindingGeneration>    mTextureUploadSampleBindingGeneration;
    std::unique_ptr<VulkanTextureUploadSamplePipelineGeneration>   mTextureUploadSamplePipelineGeneration;
    std::unique_ptr<VulkanUploadSourceGeneration>                  mUploadSourceGeneration;
    std::unique_ptr<VulkanUploadDestinationGeneration>             mUploadDestinationGeneration;
    std::unique_ptr<VulkanUploadTransferGeneration>                mUploadTransferGeneration;
    std::unique_ptr<VulkanSwapchainConfigurationGeneration>    mSwapchainConfigurationGeneration;
    std::unique_ptr<VulkanSwapchainGeneration>                 mSwapchainGeneration;
    std::unique_ptr<VulkanSwapchainImagesGeneration>               mSwapchainImagesGeneration;
    std::unique_ptr<VulkanSwapchainPresentationTargetGeneration>   mSwapchainPresentationTargetGeneration;
    std::unique_ptr<VulkanSwapchainPresentationPipelineGeneration> mSwapchainPresentationPipelineGeneration;
    std::unique_ptr<VulkanSwapchainReadbackGeneration>             mSwapchainReadbackGeneration;
    std::unique_ptr<VulkanSwapchainFrameSlotGeneration>            mSwapchainFrameSlotGeneration;
    std::uint64_t                                                  mTextureUploadDestinationEpoch           = 0;
    std::uint64_t                                                  mTextureUploadSourceEpoch                = 0;
    std::uint64_t                                                  mTextureUploadTransferEpoch              = 0;
    std::uint64_t                                                  mTextureUploadSampleBindingEpoch         = 0;
    std::uint64_t                                                  mTextureUploadSamplePipelineEpoch         = 0;
    std::uint64_t                                                  mUploadSourceEpoch                       = 0;
    std::uint64_t                                                  mUploadDestinationEpoch                  = 0;
    std::uint64_t                                                  mUploadTransferEpoch                     = 0;
    std::uint64_t                                                  mSwapchainPresentationTargetEpoch        = 0;
    std::uint64_t                                                  mSwapchainPresentationPipelineEpoch      = 0;
    std::uint64_t                                                  mSwapchainReadbackEpoch                  = 0;
    std::uint64_t                                                  mSwapchainFrameSlotEpoch                 = 0;
    std::uint64_t                                                  mOwnershipTransitionEpoch                = 0;
    std::size_t                                                    mNativeAcquisitionDepth                  = 0;
    std::size_t                                                    mTextureUploadDestinationTeardownDepth   = 0;
    std::size_t                                                    mTextureUploadSourceTeardownDepth        = 0;
    std::size_t                                                    mTextureUploadTransferTeardownDepth      = 0;
    std::size_t                                                    mTextureUploadSampleBindingTeardownDepth = 0;
    std::size_t                                                    mTextureUploadSamplePipelineTeardownDepth = 0;
    std::size_t                                                    mSwapchainPresentationTargetTeardownDepth = 0;
};

using VulkanInstanceAcquireResult = std::variant<VulkanInstanceAcquireError, VulkanInstanceGeneration>;

VulkanInstanceAcquireResult acquireVulkanInstanceGeneration(const VulkanInstanceRequest& request) noexcept;

namespace VulkanInstanceDetail
{

    // Production uses acquireVulkanInstanceGeneration(). Tests use this overload
    // to force allocation failure before an owned native object can escape.
    using AllocationCheckpoint = void (*)();

    VulkanInstanceAcquireResult acquire(const VulkanInstanceRequest& request, AllocationCheckpoint allocation_checkpoint) noexcept;

    VulkanSurfaceAcquireResult acquireSurface(VulkanInstanceGeneration&   instance_generation,
                                              const VulkanSurfaceRequest& request,
                                              AllocationCheckpoint        allocation_checkpoint) noexcept;

    VulkanPresentationDeviceAcquireResult acquirePresentationDevice(VulkanInstanceGeneration&              instance_generation,
                                                                    const VulkanPresentationDeviceRequest& request,
                                                                    AllocationCheckpoint                   allocation_checkpoint) noexcept;

    VulkanLogicalDeviceAcquireResult acquireLogicalDevice(VulkanInstanceGeneration&         instance_generation,
                                                          const VulkanLogicalDeviceRequest& request,
                                                          AllocationCheckpoint              allocation_checkpoint) noexcept;

    VulkanUploadSourceAcquireResult acquireUploadSource(VulkanInstanceGeneration&        instance_generation,
                                                        const VulkanUploadSourceRequest& request,
                                                        AllocationCheckpoint             allocation_checkpoint) noexcept;

    VulkanTextureUploadDestinationAcquireResult acquireTextureUploadDestination(
        VulkanInstanceGeneration&                    instance_generation,
        const VulkanTextureUploadDestinationRequest& request,
        AllocationCheckpoint                         allocation_checkpoint) noexcept;

    VulkanTextureUploadSourceAcquireResult acquireTextureUploadSource(VulkanInstanceGeneration&               instance_generation,
                                                                      const VulkanTextureUploadSourceRequest& request,
                                                                      AllocationCheckpoint allocation_checkpoint) noexcept;
    VulkanTextureUploadTransferAcquireResult acquireTextureUploadTransfer(VulkanInstanceGeneration&                 instance_generation,
                                                                          const VulkanTextureUploadTransferRequest& request,
                                                                          AllocationCheckpoint allocation_checkpoint) noexcept;

    VulkanTextureUploadSampleBindingAcquireResult acquireTextureUploadSampleBinding(VulkanInstanceGeneration& instance_generation,
                                                                                    const VulkanTextureUploadSampleBindingRequest& request,
                                                                                    AllocationCheckpoint allocation_checkpoint) noexcept;

    VulkanTextureUploadSamplePipelineAcquireResult acquireTextureUploadSamplePipeline(
        VulkanInstanceGeneration&                       instance_generation,
        const VulkanTextureUploadSamplePipelineRequest& request,
        AllocationCheckpoint                            allocation_checkpoint) noexcept;

    VulkanUploadDestinationAcquireResult acquireUploadDestination(VulkanInstanceGeneration&             instance_generation,
                                                                  const VulkanUploadDestinationRequest& request,
                                                                  AllocationCheckpoint                  allocation_checkpoint) noexcept;

    VulkanUploadTransferAcquireResult acquireUploadTransfer(VulkanInstanceGeneration&          instance_generation,
                                                            const VulkanUploadTransferRequest& request,
                                                            AllocationCheckpoint               allocation_checkpoint) noexcept;

    VulkanSwapchainConfigurationAcquireResult acquireSwapchainConfiguration(VulkanInstanceGeneration&                  instance_generation,
                                                                            const VulkanSwapchainConfigurationRequest& request,
                                                                            AllocationCheckpoint allocation_checkpoint) noexcept;

    VulkanSwapchainAcquireResult acquireSwapchain(VulkanInstanceGeneration&     instance_generation,
                                                  const VulkanSwapchainRequest& request,
                                                  AllocationCheckpoint          allocation_checkpoint) noexcept;

    VulkanSwapchainImagesAcquireResult acquireSwapchainImages(VulkanInstanceGeneration&           instance_generation,
                                                              const VulkanSwapchainImagesRequest& request,
                                                              AllocationCheckpoint                allocation_checkpoint) noexcept;

    VulkanSwapchainPresentationTargetAcquireResult acquireSwapchainPresentationTarget(
        VulkanInstanceGeneration&                         instance_generation,
        const VulkanSwapchainPresentationTargetRequest&   request,
        AllocationCheckpoint                              allocation_checkpoint) noexcept;

    VulkanSwapchainPresentationPipelineAcquireResult acquireSwapchainPresentationPipeline(
        VulkanInstanceGeneration&                         instance_generation,
        const VulkanSwapchainPresentationPipelineRequest& request,
        AllocationCheckpoint                              allocation_checkpoint) noexcept;

    VulkanSwapchainReadbackAcquireResult acquireSwapchainReadback(VulkanInstanceGeneration&             instance_generation,
                                                                  const VulkanSwapchainReadbackRequest& request,
                                                                  AllocationCheckpoint                  allocation_checkpoint) noexcept;

    VulkanSwapchainFrameSlotAcquireResult acquireSwapchainFrameSlot(VulkanInstanceGeneration&              instance_generation,
                                                                    const VulkanSwapchainFrameSlotRequest& request,
                                                                    AllocationCheckpoint                   allocation_checkpoint) noexcept;

    VulkanSwapchainChainRebuildResult rebuildSwapchainChain(VulkanInstanceGeneration&                 instance_generation,
                                                             const VulkanSwapchainChainRebuildRequest& request,
                                                             AllocationCheckpoint                      allocation_checkpoint) noexcept;

} // namespace VulkanInstanceDetail

} // namespace LLRenderVulkan

#endif // LL_LLRENDERVULKANINSTANCE_H
