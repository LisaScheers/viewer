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

    bool current(const VulkanWindowGenerationCheck& check, std::uint64_t generation) noexcept
    {
        return generation != 0 && check.mIsCurrent && check.mIsCurrent(check.mUserdata, generation);
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

struct VulkanInstanceGenerationFactory
{
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
    mDestroyDebugMessenger(std::exchange(other.mDestroyDebugMessenger, nullptr))
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

void VulkanInstanceGeneration::reset() noexcept
{
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

} // namespace VulkanInstanceDetail

VulkanInstanceAcquireResult acquireVulkanInstanceGeneration(const VulkanInstanceRequest& request) noexcept
{
    return VulkanInstanceDetail::acquire(request, nullptr);
}

} // namespace LLRenderVulkan
