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
#include "llrendervulkanswapchain.h"
#include "llrendervulkanswapchainconfiguration.h"
#include "llrendervulkanswapchainframeslot.h"
#include "llrendervulkanswapchainimages.h"
#include "llrendervulkanswapchainpresentationtarget.h"
#include "llrendervulkanswapchainpresentationpipeline.h"

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
    VulkanSwapchainPresentationPipelineAcquireCode                    mCode =
        VulkanSwapchainPresentationPipelineAcquireCode::InvalidInstanceOwnerCheck;
    std::optional<VulkanSwapchainPresentationPipelineResolutionError> mResolutionError;

    friend constexpr bool operator==(const VulkanSwapchainPresentationPipelineAcquireError&,
                                     const VulkanSwapchainPresentationPipelineAcquireError&) = default;
};

using VulkanSwapchainPresentationPipelineAcquireResult =
    std::optional<VulkanSwapchainPresentationPipelineAcquireError>;

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
    SwapchainPresentationPipelineNotLive
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
    PresentationPipeline
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

using VulkanSwapchainChainRebuildChildError =
    std::variant<std::monostate, VulkanSwapchainConfigurationAcquireError, VulkanSwapchainAcquireError,
                 VulkanSwapchainImagesAcquireError, VulkanSwapchainFrameSlotAcquireError,
                 VulkanSwapchainPresentationTargetAcquireError,
                 VulkanSwapchainPresentationPipelineAcquireError>;

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
// contract. The bool reset APIs defensively retain the live owner for those
// obligations and while an unpublished native acquisition candidate exists.
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
    bool                              hasSwapchainFrameSlotGeneration() const noexcept;
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
    VulkanSwapchainConfigurationAcquireResult acquireSwapchainConfigurationGeneration(
        const VulkanSwapchainConfigurationRequest& request) noexcept;
    VulkanSwapchainAcquireResult       acquireSwapchainGeneration(const VulkanSwapchainRequest& request) noexcept;
    VulkanSwapchainImagesAcquireResult acquireSwapchainImagesGeneration(const VulkanSwapchainImagesRequest& request) noexcept;
    VulkanSwapchainPresentationTargetAcquireResult acquireSwapchainPresentationTargetGeneration(
        const VulkanSwapchainPresentationTargetRequest& request) noexcept;
    VulkanSwapchainPresentationPipelineAcquireResult acquireSwapchainPresentationPipelineGeneration(
        const VulkanSwapchainPresentationPipelineRequest& request) noexcept;
    VulkanSwapchainFrameSlotAcquireResult acquireSwapchainFrameSlotGeneration(const VulkanSwapchainFrameSlotRequest& request) noexcept;
    VulkanSwapchainChainRebuildResult      rebuildSwapchainChain(const VulkanSwapchainChainRebuildRequest& request) noexcept;
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
    VulkanSwapchainFrameSlotParentPresentationResult retrySwapchainFrameSlotPresentation(
        const VulkanSwapchainFrameSlotOperationRequest& request) noexcept;
    VulkanSwapchainFrameSlotParentPresentationResult retrySwapchainFrameSlotPresentationCompletion(
        const VulkanSwapchainFrameSlotOperationRequest& request) noexcept;
    VulkanSwapchainFrameSlotParentOperationResult cancelSwapchainFrameSlotPresentation(
        const VulkanSwapchainFrameSlotOperationRequest& request) noexcept;
    VulkanSwapchainFrameSlotParentOperationResult retrySwapchainFrameSlotCancellationCompletion(
        const VulkanSwapchainFrameSlotOperationRequest& request) noexcept;
    // Callers externally serialize these resets. Resetting a presentation target
    // retires its frame slot, then its presentation pipeline, before the target,
    // so no slot command may still use a dependent object. False preserves the
    // requested owner and every parent; it reports either an outstanding frame
    // obligation or an in-progress native child acquisition. The latter refuses
    // even an otherwise idempotent reset.
    bool resetSwapchainFrameSlotGeneration() noexcept;
    bool resetSwapchainPresentationPipelineGeneration() noexcept;
    bool resetSwapchainPresentationTargetGeneration() noexcept;
    bool resetSwapchainImagesGeneration() noexcept;
    bool resetSwapchainGeneration() noexcept;
    bool resetSwapchainConfigurationGeneration() noexcept;
    bool resetLogicalDeviceGeneration() noexcept;
    bool resetPresentationDeviceGeneration() noexcept;
    bool resetSurfaceGeneration() noexcept;

    bool reset() noexcept;

private:
    friend struct VulkanInstanceGenerationFactory;

    class NativeAcquisitionGuard;
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
    std::unique_ptr<VulkanSurfaceGeneration>                mSurfaceGeneration;
    std::unique_ptr<VulkanPhysicalDeviceGeneration>         mPresentationDeviceGeneration;
    std::unique_ptr<VulkanLogicalDeviceGeneration>          mLogicalDeviceGeneration;
    std::unique_ptr<VulkanSwapchainConfigurationGeneration>    mSwapchainConfigurationGeneration;
    std::unique_ptr<VulkanSwapchainGeneration>                 mSwapchainGeneration;
    std::unique_ptr<VulkanSwapchainImagesGeneration>           mSwapchainImagesGeneration;
    std::unique_ptr<VulkanSwapchainPresentationTargetGeneration> mSwapchainPresentationTargetGeneration;
    std::unique_ptr<VulkanSwapchainPresentationPipelineGeneration> mSwapchainPresentationPipelineGeneration;
    std::unique_ptr<VulkanSwapchainFrameSlotGeneration>        mSwapchainFrameSlotGeneration;
    std::uint64_t                                                  mSwapchainPresentationTargetEpoch   = 0;
    std::uint64_t                                                  mSwapchainPresentationPipelineEpoch = 0;
    std::uint64_t                                                  mSwapchainFrameSlotEpoch            = 0;
    std::uint64_t                                              mOwnershipTransitionEpoch = 0;
    std::size_t                                                mNativeAcquisitionDepth   = 0;
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

    VulkanSwapchainFrameSlotAcquireResult acquireSwapchainFrameSlot(VulkanInstanceGeneration&              instance_generation,
                                                                    const VulkanSwapchainFrameSlotRequest& request,
                                                                    AllocationCheckpoint                   allocation_checkpoint) noexcept;

    VulkanSwapchainChainRebuildResult rebuildSwapchainChain(VulkanInstanceGeneration&                 instance_generation,
                                                             const VulkanSwapchainChainRebuildRequest& request,
                                                             AllocationCheckpoint                      allocation_checkpoint) noexcept;

} // namespace VulkanInstanceDetail

} // namespace LLRenderVulkan

#endif // LL_LLRENDERVULKANINSTANCE_H
