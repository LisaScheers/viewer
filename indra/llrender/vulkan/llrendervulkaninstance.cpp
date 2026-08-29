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
#include <cstring>
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

    VulkanSwapchainFrameSlotAcquireError swapchainFrameSlotFailure(
        VulkanSwapchainFrameSlotAcquireCode                    code,
        std::optional<VulkanSwapchainFrameSlotResolutionError> resolution_error = std::nullopt) noexcept
    {
        return { code, resolution_error };
    }

    bool current(const VulkanWindowGenerationCheck& check, std::uint64_t generation) noexcept
    {
        return generation != 0 && check.mIsCurrent && check.mIsCurrent(check.mUserdata, generation);
    }

    VulkanSurfaceAcquireResult surfaceFreshness(const VulkanSurfaceRequest& request, const VulkanInstanceGeneration& generation) noexcept
    {
        if (!request.mInstanceOwnerCheck.mIsCurrent(request.mInstanceOwnerCheck.mUserdata, generation))
        {
            return surfaceFailure(VulkanSurfaceAcquireCode::StaleInstanceOwner);
        }
        if (!current(request.mWindowGenerationCheck, request.mNativeWindowGeneration))
        {
            return surfaceFailure(VulkanSurfaceAcquireCode::StaleWindowGeneration);
        }
        return std::nullopt;
    }

    VulkanPresentationDeviceAcquireResult presentationDeviceFreshness(const VulkanPresentationDeviceRequest& request,
                                                                      const VulkanInstanceGeneration&        generation) noexcept
    {
        if (!request.mInstanceOwnerCheck.mIsCurrent(request.mInstanceOwnerCheck.mUserdata, generation))
        {
            return presentationDeviceFailure(VulkanPresentationDeviceAcquireCode::StaleInstanceOwner);
        }
        if (!current(request.mWindowGenerationCheck, request.mNativeWindowGeneration))
        {
            return presentationDeviceFailure(VulkanPresentationDeviceAcquireCode::StaleWindowGeneration);
        }
        return std::nullopt;
    }

    VulkanLogicalDeviceAcquireResult logicalDeviceFreshness(const VulkanLogicalDeviceRequest& request,
                                                            const VulkanInstanceGeneration&   generation) noexcept
    {
        if (!request.mInstanceOwnerCheck.mIsCurrent(request.mInstanceOwnerCheck.mUserdata, generation))
        {
            return logicalDeviceFailure(VulkanLogicalDeviceAcquireCode::StaleInstanceOwner);
        }
        if (!current(request.mWindowGenerationCheck, request.mNativeWindowGeneration))
        {
            return logicalDeviceFailure(VulkanLogicalDeviceAcquireCode::StaleWindowGeneration);
        }
        return std::nullopt;
    }

    VulkanSwapchainConfigurationAcquireResult swapchainConfigurationFreshness(const VulkanSwapchainConfigurationRequest& request,
                                                                              const VulkanInstanceGeneration& generation) noexcept
    {
        if (!request.mInstanceOwnerCheck.mIsCurrent(request.mInstanceOwnerCheck.mUserdata, generation))
        {
            return swapchainConfigurationFailure(VulkanSwapchainConfigurationAcquireCode::StaleInstanceOwner);
        }
        if (!current(request.mWindowGenerationCheck, request.mNativeWindowGeneration))
        {
            return swapchainConfigurationFailure(VulkanSwapchainConfigurationAcquireCode::StaleWindowGeneration);
        }
        return std::nullopt;
    }

    VulkanSwapchainAcquireResult swapchainFreshness(const VulkanSwapchainRequest&   request,
                                                    const VulkanInstanceGeneration& generation) noexcept
    {
        if (!request.mInstanceOwnerCheck.mIsCurrent(request.mInstanceOwnerCheck.mUserdata, generation))
        {
            return swapchainFailure(VulkanSwapchainAcquireCode::StaleInstanceOwner);
        }
        if (!current(request.mWindowGenerationCheck, request.mNativeWindowGeneration))
        {
            return swapchainFailure(VulkanSwapchainAcquireCode::StaleWindowGeneration);
        }
        return std::nullopt;
    }

    VulkanSwapchainImagesAcquireResult swapchainImagesFreshness(const VulkanSwapchainImagesRequest& request,
                                                                const VulkanInstanceGeneration&     generation) noexcept
    {
        if (!request.mInstanceOwnerCheck.mIsCurrent(request.mInstanceOwnerCheck.mUserdata, generation))
        {
            return swapchainImagesFailure(VulkanSwapchainImagesAcquireCode::StaleInstanceOwner);
        }
        if (!current(request.mWindowGenerationCheck, request.mNativeWindowGeneration))
        {
            return swapchainImagesFailure(VulkanSwapchainImagesAcquireCode::StaleWindowGeneration);
        }
        return std::nullopt;
    }

    VulkanSwapchainFrameSlotAcquireResult swapchainFrameSlotFreshness(const VulkanSwapchainFrameSlotRequest& request,
                                                                      const VulkanInstanceGeneration&        generation) noexcept
    {
        if (!request.mInstanceOwnerCheck.mIsCurrent(request.mInstanceOwnerCheck.mUserdata, generation))
        {
            return swapchainFrameSlotFailure(VulkanSwapchainFrameSlotAcquireCode::StaleInstanceOwner);
        }
        if (!current(request.mWindowGenerationCheck, request.mNativeWindowGeneration))
        {
            return swapchainFrameSlotFailure(VulkanSwapchainFrameSlotAcquireCode::StaleWindowGeneration);
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

    static VulkanSwapchainFrameSlotAcquireResult acquireSwapchainFrameSlot(
        VulkanInstanceGeneration&                  instance_generation,
        const VulkanSwapchainFrameSlotRequest&     request,
        VulkanInstanceDetail::AllocationCheckpoint allocation_checkpoint) noexcept;
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
    mDestroyDebugMessenger(destroy_debug_messenger)
{
}

VulkanInstanceGeneration::~VulkanInstanceGeneration() noexcept
{
    reset();
}

VulkanInstanceGeneration::VulkanInstanceGeneration(VulkanInstanceGeneration&& other) noexcept :
    mGlobalDispatch(std::move(other.mGlobalDispatch)),
    mValidationState(std::move(other.mValidationState)),
    mEnabledExtensions(std::move(other.mEnabledExtensions)),
    mEnabledLayers(std::move(other.mEnabledLayers)),
    mNativeWindowGeneration(std::exchange(other.mNativeWindowGeneration, 0)),
    mPortabilityEnumeration(std::exchange(other.mPortabilityEnumeration, false)),
    mInstance(std::exchange(other.mInstance, VK_NULL_HANDLE)),
    mDestroyInstance(std::exchange(other.mDestroyInstance, nullptr)),
    mDebugMessenger(std::exchange(other.mDebugMessenger, VK_NULL_HANDLE)),
    mDestroyDebugMessenger(std::exchange(other.mDestroyDebugMessenger, nullptr)),
    mSurfaceGeneration(std::move(other.mSurfaceGeneration)),
    mPresentationDeviceGeneration(std::move(other.mPresentationDeviceGeneration)),
    mLogicalDeviceGeneration(std::move(other.mLogicalDeviceGeneration)),
    mSwapchainConfigurationGeneration(std::move(other.mSwapchainConfigurationGeneration)),
    mSwapchainGeneration(std::move(other.mSwapchainGeneration)),
    mSwapchainImagesGeneration(std::move(other.mSwapchainImagesGeneration)),
    mSwapchainFrameSlotGeneration(std::move(other.mSwapchainFrameSlotGeneration))
{
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

VkFence VulkanInstanceGeneration::swapchainFrameSubmissionFence() const noexcept
{
    return mSwapchainFrameSlotGeneration ? mSwapchainFrameSlotGeneration->submissionFence() : VK_NULL_HANDLE;
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

VulkanSwapchainFrameSlotAcquireResult VulkanInstanceGeneration::acquireSwapchainFrameSlotGeneration(
    const VulkanSwapchainFrameSlotRequest& request) noexcept
{
    return VulkanInstanceDetail::acquireSwapchainFrameSlot(*this, request, nullptr);
}

void VulkanInstanceGeneration::resetSwapchainFrameSlotGeneration() noexcept
{
    mSwapchainFrameSlotGeneration.reset();
}

void VulkanInstanceGeneration::resetSwapchainImagesGeneration() noexcept
{
    resetSwapchainFrameSlotGeneration();
    mSwapchainImagesGeneration.reset();
}

void VulkanInstanceGeneration::resetSwapchainGeneration() noexcept
{
    resetSwapchainImagesGeneration();
    mSwapchainGeneration.reset();
}

void VulkanInstanceGeneration::resetSwapchainConfigurationGeneration() noexcept
{
    resetSwapchainGeneration();
    mSwapchainConfigurationGeneration.reset();
}

void VulkanInstanceGeneration::resetLogicalDeviceGeneration() noexcept
{
    resetSwapchainConfigurationGeneration();
    mLogicalDeviceGeneration.reset();
}

void VulkanInstanceGeneration::resetPresentationDeviceGeneration() noexcept
{
    resetLogicalDeviceGeneration();
    mPresentationDeviceGeneration.reset();
}

void VulkanInstanceGeneration::resetSurfaceGeneration() noexcept
{
    resetPresentationDeviceGeneration();
    mSurfaceGeneration.reset();
}

void VulkanInstanceGeneration::reset() noexcept
{
    resetSurfaceGeneration();

    if (mDebugMessenger != VK_NULL_HANDLE && mDestroyDebugMessenger)
    {
        mDestroyDebugMessenger(mInstance, mDebugMessenger, nullptr);
    }
    mDebugMessenger        = VK_NULL_HANDLE;
    mDestroyDebugMessenger = nullptr;

    if (mInstance != VK_NULL_HANDLE && mDestroyInstance)
    {
        mDestroyInstance(mInstance, nullptr);
    }
    mInstance        = VK_NULL_HANDLE;
    mDestroyInstance = nullptr;

    mGlobalDispatch.reset();
    mEnabledExtensions.clear();
    mEnabledLayers.clear();
    mNativeWindowGeneration = 0;
    mPortabilityEnumeration = false;
    mValidationState.reset();
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

    // Boundary 1: the exact instance owner and native window must both still
    // own the generation before its extension policy is consulted.
    if (VulkanSurfaceAcquireResult freshness = surfaceFreshness(request, instance_generation))
    {
        return freshness;
    }
    if (!instance_generation.isExtensionEnabled(VK_KHR_SURFACE_EXTENSION_NAME))
    {
        return surfaceFailure(VulkanSurfaceAcquireCode::MissingSurfaceExtension);
    }

    // Boundary 2: do not consult the loader after either owner becomes stale.
    if (VulkanSurfaceAcquireResult freshness = surfaceFreshness(request, instance_generation))
    {
        return freshness;
    }
    const PFN_vkDestroySurfaceKHR destroy_surface = resolveInstanceCommand<PFN_vkDestroySurfaceKHR>(
        instance_generation.mGlobalDispatch->getInstanceProcAddr(), instance_generation.mInstance, "vkDestroySurfaceKHR");
    if (!destroy_surface)
    {
        return surfaceFailure(VulkanSurfaceAcquireCode::MissingRequiredInstanceCommand, std::nullopt, VulkanSurfaceCommand::DestroySurface);
    }

    // Boundary 3: destruction capability is available, but no platform object
    // exists yet.
    if (VulkanSurfaceAcquireResult freshness = surfaceFreshness(request, instance_generation))
    {
        return freshness;
    }

    try
    {
        if (allocation_checkpoint)
        {
            allocation_checkpoint();
        }
        auto pending = std::make_unique<VulkanInstanceGeneration::VulkanSurfaceGeneration>();

        // Boundary 4: allocation cannot leave a created surface without an
        // owner, and freshness is checked once more immediately before create.
        if (VulkanSurfaceAcquireResult freshness = surfaceFreshness(request, instance_generation))
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
        if (VulkanSurfaceAcquireResult freshness = surfaceFreshness(request, instance_generation))
        {
            return freshness;
        }

        instance_generation.mSurfaceGeneration = std::move(pending);
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
    if (VulkanPresentationDeviceAcquireResult freshness = presentationDeviceFreshness(request, instance_generation))
    {
        return freshness;
    }

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
        }
        auto pending =
            std::make_unique<VulkanPhysicalDeviceGeneration>(std::move(std::get<VulkanPhysicalDeviceGeneration>(selection_result)));

        if (VulkanPresentationDeviceAcquireResult freshness = presentationDeviceFreshness(request, instance_generation))
        {
            return freshness;
        }
        if (instance_generation.mInstance != instance || !instance_generation.mSurfaceGeneration ||
            instance_generation.surface() != surface || !pending->selectedFor(instance_generation.mInstance, instance_generation.surface()))
        {
            return presentationDeviceFailure(VulkanPresentationDeviceAcquireCode::SurfaceNotLive);
        }

        instance_generation.mPresentationDeviceGeneration = std::move(pending);
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
    if (VulkanLogicalDeviceAcquireResult freshness = logicalDeviceFreshness(request, instance_generation))
    {
        return freshness;
    }

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
        }
        auto pending =
            std::make_unique<VulkanLogicalDeviceGeneration>(std::move(std::get<VulkanLogicalDeviceGeneration>(resolution_result)));

        if (VulkanLogicalDeviceAcquireResult freshness = logicalDeviceFreshness(request, instance_generation))
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
    if (VulkanSwapchainConfigurationAcquireResult freshness = swapchainConfigurationFreshness(request, instance_generation))
    {
        return freshness;
    }

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
        }
        auto pending = std::make_unique<VulkanSwapchainConfigurationGeneration>(
            std::move(std::get<VulkanSwapchainConfigurationGeneration>(resolution_result)));

        if (VulkanSwapchainConfigurationAcquireResult freshness = swapchainConfigurationFreshness(request, instance_generation))
        {
            return freshness;
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
    if (VulkanSwapchainAcquireResult freshness = swapchainFreshness(request, instance_generation))
    {
        return freshness;
    }

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
        }
        auto pending = std::make_unique<VulkanSwapchainGeneration>(std::move(std::get<VulkanSwapchainGeneration>(resolution_result)));

        if (VulkanSwapchainAcquireResult freshness = swapchainFreshness(request, instance_generation))
        {
            return freshness;
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
    if (VulkanSwapchainImagesAcquireResult freshness = swapchainImagesFreshness(request, instance_generation))
    {
        return freshness;
    }

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
        }
        auto pending =
            std::make_unique<VulkanSwapchainImagesGeneration>(std::move(std::get<VulkanSwapchainImagesGeneration>(resolution_result)));

        if (VulkanSwapchainImagesAcquireResult freshness = swapchainImagesFreshness(request, instance_generation))
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
        return std::nullopt;
    }
    catch (const std::bad_alloc&)
    {
        return swapchainImagesFailure(VulkanSwapchainImagesAcquireCode::AllocationFailure);
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
    if (VulkanSwapchainFrameSlotAcquireResult freshness = swapchainFrameSlotFreshness(request, instance_generation))
    {
        return freshness;
    }

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
        }
        auto pending = std::make_unique<VulkanSwapchainFrameSlotGeneration>(
            std::move(std::get<VulkanSwapchainFrameSlotGeneration>(resolution_result)));

        if (VulkanSwapchainFrameSlotAcquireResult freshness = swapchainFrameSlotFreshness(request, instance_generation))
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
        return std::nullopt;
    }
    catch (const std::bad_alloc&)
    {
        return swapchainFrameSlotFailure(VulkanSwapchainFrameSlotAcquireCode::AllocationFailure);
    }
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

    VulkanSwapchainFrameSlotAcquireResult acquireSwapchainFrameSlot(VulkanInstanceGeneration&              instance_generation,
                                                                    const VulkanSwapchainFrameSlotRequest& request,
                                                                    AllocationCheckpoint                   allocation_checkpoint) noexcept
    {
        return VulkanInstanceGenerationFactory::acquireSwapchainFrameSlot(instance_generation, request, allocation_checkpoint);
    }

} // namespace VulkanInstanceDetail

VulkanInstanceAcquireResult acquireVulkanInstanceGeneration(const VulkanInstanceRequest& request) noexcept
{
    return VulkanInstanceDetail::acquire(request, nullptr);
}

} // namespace LLRenderVulkan
