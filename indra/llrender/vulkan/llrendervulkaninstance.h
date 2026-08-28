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

// This generation owns the Vulkan objects but borrows the loader behind the
// originating window's resolver. It must be reset before that window destroys
// its requirements generation or releases its loader references.
class VulkanInstanceGeneration
{
public:
    struct ValidationState;

    ~VulkanInstanceGeneration() noexcept;

    VulkanInstanceGeneration(const VulkanInstanceGeneration&)            = delete;
    VulkanInstanceGeneration& operator=(const VulkanInstanceGeneration&) = delete;
    VulkanInstanceGeneration(VulkanInstanceGeneration&& other) noexcept;
    VulkanInstanceGeneration& operator=(VulkanInstanceGeneration&&) = delete;

    VkInstance                      instance() const noexcept { return mInstance; }
    std::uint32_t                   apiVersion() const noexcept { return RENDERER_VULKAN_API_VERSION; }
    std::uint64_t                   nativeWindowGeneration() const noexcept { return mNativeWindowGeneration; }
    bool                            validationEnabled() const noexcept { return mDebugMessenger != VK_NULL_HANDLE; }
    bool                            portabilityEnumerationEnabled() const noexcept { return mPortabilityEnumeration; }
    const std::vector<std::string>& enabledExtensions() const noexcept { return mEnabledExtensions; }
    const std::vector<std::string>& enabledLayers() const noexcept { return mEnabledLayers; }
    bool                            isExtensionEnabled(std::string_view extension_name) const noexcept;
    VulkanValidationSnapshot        validationSnapshot() const noexcept;

    void reset() noexcept;

private:
    friend struct VulkanInstanceGenerationFactory;

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

    std::optional<VulkanGlobalDispatchGeneration> mGlobalDispatch;
    std::unique_ptr<ValidationState>              mValidationState;
    std::vector<std::string>                      mEnabledExtensions;
    std::vector<std::string>                      mEnabledLayers;
    std::uint64_t                                 mNativeWindowGeneration = 0;
    bool                                          mPortabilityEnumeration = false;
    VkInstance                                    mInstance               = VK_NULL_HANDLE;
    PFN_vkDestroyInstance                         mDestroyInstance        = nullptr;
    VkDebugUtilsMessengerEXT                      mDebugMessenger         = VK_NULL_HANDLE;
    PFN_vkDestroyDebugUtilsMessengerEXT           mDestroyDebugMessenger  = nullptr;
};

using VulkanInstanceAcquireResult = std::variant<VulkanInstanceAcquireError, VulkanInstanceGeneration>;

VulkanInstanceAcquireResult acquireVulkanInstanceGeneration(const VulkanInstanceRequest& request) noexcept;

namespace VulkanInstanceDetail
{

    // Production uses acquireVulkanInstanceGeneration(). Tests use this overload
    // to force allocation failure before an owned native object can escape.
    using AllocationCheckpoint = void (*)();

    VulkanInstanceAcquireResult acquire(const VulkanInstanceRequest& request, AllocationCheckpoint allocation_checkpoint) noexcept;

} // namespace VulkanInstanceDetail

} // namespace LLRenderVulkan

#endif // LL_LLRENDERVULKANINSTANCE_H
