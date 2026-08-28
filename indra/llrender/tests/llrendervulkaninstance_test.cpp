/**
 * @file llrendervulkaninstance_test.cpp
 * @brief Tests for owned Vulkan instance generations.
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

#include "llrendervulkaninstance.h"
#include "lltut.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace
{
using namespace LLRenderVulkan;

enum class MissingCommand : std::uint8_t
{
    None,
    GlobalCreateInstance,
    GlobalEnumerateExtensions,
    GlobalEnumerateLayers,
    GlobalEnumerateVersion,
    DestroyInstance,
    CreateDebugMessenger,
    DestroyDebugMessenger
};

enum class Event : std::uint8_t
{
    CreateInstance,
    CreateDebugMessenger,
    DestroyDebugMessenger,
    DestroyInstance
};

template<typename Handle>
Handle fakeHandle(std::uintptr_t value) noexcept
{
    if constexpr (std::is_pointer_v<Handle>)
    {
        return reinterpret_cast<Handle>(value);
    }
    else
    {
        return static_cast<Handle>(value);
    }
}

struct FakeState
{
    std::vector<std::string> mExtensions{ VK_KHR_SURFACE_EXTENSION_NAME };
    std::vector<std::string> mLayers;

    MissingCommand mMissing       = MissingCommand::None;
    VkResult       mVersionResult = VK_SUCCESS;
    std::uint32_t  mVersion       = VK_API_VERSION_1_2;

    VkResult      mExtensionCountResult          = VK_SUCCESS;
    VkResult      mExtensionValuesResult         = VK_SUCCESS;
    std::uint32_t mExtensionCountOverride        = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t mExtensionWrittenOverride      = std::numeric_limits<std::uint32_t>::max();
    std::size_t   mIncompleteExtensionCountCalls = 0;
    std::size_t   mIncompleteExtensionValueCalls = 0;
    bool          mMalformedExtension            = false;
    bool          mAlwaysIncompleteExtensions    = false;

    VkResult      mLayerCountResult   = VK_SUCCESS;
    VkResult      mLayerValuesResult  = VK_SUCCESS;
    std::uint32_t mLayerCountOverride = std::numeric_limits<std::uint32_t>::max();
    bool          mMalformedLayer     = false;

    VkResult mInstanceResult     = VK_SUCCESS;
    bool     mNullInstance       = false;
    VkResult mDebugResult        = VK_SUCCESS;
    bool     mNullDebugMessenger = false;

    VkInstance                           mInstance       = fakeHandle<VkInstance>(0x1111);
    VkDebugUtilsMessengerEXT             mDebugMessenger = fakeHandle<VkDebugUtilsMessengerEXT>(0x2222);
    std::vector<Event>                   mEvents;
    std::vector<std::string>             mEnabledExtensions;
    std::vector<std::string>             mEnabledLayers;
    VkInstanceCreateFlags                mInstanceFlags       = 0;
    std::uint32_t                        mRequestedApiVersion = 0;
    PFN_vkDebugUtilsMessengerCallbackEXT mValidationCallback  = nullptr;
    void*                                mValidationUserdata  = nullptr;

    std::size_t mVersionCalls         = 0;
    std::size_t mExtensionCountCalls  = 0;
    std::size_t mExtensionValuesCalls = 0;
    std::size_t mLayerCountCalls      = 0;
    std::size_t mLayerValuesCalls     = 0;
    std::size_t mDestroyInstanceCalls = 0;
    std::size_t mDestroyDebugCalls    = 0;

    bool        mGenerationCurrent   = true;
    std::size_t mGenerationChecks    = 0;
    std::size_t mFailGenerationCheck = 0;
};

FakeState* gFakeState = nullptr;

class ScopedFakeState
{
public:
    explicit ScopedFakeState(FakeState& state) noexcept { gFakeState = &state; }
    ~ScopedFakeState() noexcept { gFakeState = nullptr; }

    ScopedFakeState(const ScopedFakeState&)            = delete;
    ScopedFakeState& operator=(const ScopedFakeState&) = delete;

    void use(FakeState& state) noexcept { gFakeState = &state; }
};

template<typename Function>
PFN_vkVoidFunction eraseFunctionType(Function function) noexcept
{
    return reinterpret_cast<PFN_vkVoidFunction>(function);
}

template<std::size_t Size>
void copyPropertyName(char (&destination)[Size], const std::string& source, bool malformed) noexcept
{
    if (malformed)
    {
        std::fill(std::begin(destination), std::end(destination), 'x');
        return;
    }
    const std::size_t count = std::min(source.size(), Size - 1);
    std::memcpy(destination, source.data(), count);
    destination[count] = '\0';
}

VKAPI_ATTR VkResult VKAPI_CALL fakeEnumerateInstanceVersion(std::uint32_t* version) noexcept
{
    if (!gFakeState || !version)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    ++gFakeState->mVersionCalls;
    *version = gFakeState->mVersion;
    return gFakeState->mVersionResult;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeEnumerateInstanceExtensionProperties(const char*, std::uint32_t* count,
                                                                        VkExtensionProperties* properties) noexcept
{
    if (!gFakeState || !count)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!properties)
    {
        ++gFakeState->mExtensionCountCalls;
        *count = gFakeState->mExtensionCountOverride == std::numeric_limits<std::uint32_t>::max()
                     ? static_cast<std::uint32_t>(gFakeState->mExtensions.size())
                     : gFakeState->mExtensionCountOverride;
        if (gFakeState->mExtensionCountCalls <= gFakeState->mIncompleteExtensionCountCalls)
        {
            return VK_INCOMPLETE;
        }
        return gFakeState->mExtensionCountResult;
    }

    ++gFakeState->mExtensionValuesCalls;
    const std::uint32_t capacity   = *count;
    const std::uint32_t written    = gFakeState->mExtensionWrittenOverride == std::numeric_limits<std::uint32_t>::max()
                                         ? static_cast<std::uint32_t>(gFakeState->mExtensions.size())
                                         : gFakeState->mExtensionWrittenOverride;
    const std::size_t   fill_count = std::min<std::size_t>(capacity, gFakeState->mExtensions.size());
    for (std::size_t index = 0; index < fill_count; ++index)
    {
        copyPropertyName(properties[index].extensionName, gFakeState->mExtensions[index], gFakeState->mMalformedExtension && index == 0);
    }
    *count = written;
    if (gFakeState->mAlwaysIncompleteExtensions || gFakeState->mExtensionValuesCalls <= gFakeState->mIncompleteExtensionValueCalls)
    {
        return VK_INCOMPLETE;
    }
    return gFakeState->mExtensionValuesResult;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeEnumerateInstanceLayerProperties(std::uint32_t* count, VkLayerProperties* properties) noexcept
{
    if (!gFakeState || !count)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!properties)
    {
        ++gFakeState->mLayerCountCalls;
        *count = gFakeState->mLayerCountOverride == std::numeric_limits<std::uint32_t>::max()
                     ? static_cast<std::uint32_t>(gFakeState->mLayers.size())
                     : gFakeState->mLayerCountOverride;
        return gFakeState->mLayerCountResult;
    }

    ++gFakeState->mLayerValuesCalls;
    const std::uint32_t capacity   = *count;
    const std::size_t   fill_count = std::min<std::size_t>(capacity, gFakeState->mLayers.size());
    for (std::size_t index = 0; index < fill_count; ++index)
    {
        copyPropertyName(properties[index].layerName, gFakeState->mLayers[index], gFakeState->mMalformedLayer && index == 0);
    }
    *count = static_cast<std::uint32_t>(gFakeState->mLayers.size());
    return gFakeState->mLayerValuesResult;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateInstance(const VkInstanceCreateInfo* create_info, const VkAllocationCallbacks*,
                                                  VkInstance*                 instance) noexcept
{
    if (!gFakeState || !create_info || !instance || !create_info->pApplicationInfo)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    gFakeState->mEvents.push_back(Event::CreateInstance);
    gFakeState->mInstanceFlags       = create_info->flags;
    gFakeState->mRequestedApiVersion = create_info->pApplicationInfo->apiVersion;
    gFakeState->mEnabledExtensions.clear();
    for (std::uint32_t index = 0; index < create_info->enabledExtensionCount; ++index)
    {
        gFakeState->mEnabledExtensions.emplace_back(create_info->ppEnabledExtensionNames[index]);
    }
    gFakeState->mEnabledLayers.clear();
    for (std::uint32_t index = 0; index < create_info->enabledLayerCount; ++index)
    {
        gFakeState->mEnabledLayers.emplace_back(create_info->ppEnabledLayerNames[index]);
    }
    if (create_info->pNext)
    {
        const auto* debug_info          = static_cast<const VkDebugUtilsMessengerCreateInfoEXT*>(create_info->pNext);
        gFakeState->mValidationCallback = debug_info->pfnUserCallback;
        gFakeState->mValidationUserdata = debug_info->pUserData;
    }
    *instance = gFakeState->mNullInstance ? VK_NULL_HANDLE : gFakeState->mInstance;
    return gFakeState->mInstanceResult;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyInstance(VkInstance instance, const VkAllocationCallbacks*) noexcept
{
    if (gFakeState && instance == gFakeState->mInstance)
    {
        ++gFakeState->mDestroyInstanceCalls;
        gFakeState->mEvents.push_back(Event::DestroyInstance);
    }
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateDebugUtilsMessenger(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* create_info,
                                                             const VkAllocationCallbacks*, VkDebugUtilsMessengerEXT* messenger) noexcept
{
    if (!gFakeState || instance != gFakeState->mInstance || !create_info || !messenger)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    gFakeState->mEvents.push_back(Event::CreateDebugMessenger);
    gFakeState->mValidationCallback = create_info->pfnUserCallback;
    gFakeState->mValidationUserdata = create_info->pUserData;
    *messenger                      = gFakeState->mNullDebugMessenger ? VK_NULL_HANDLE : gFakeState->mDebugMessenger;
    return gFakeState->mDebugResult;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyDebugUtilsMessenger(VkInstance instance, VkDebugUtilsMessengerEXT messenger,
                                                          const VkAllocationCallbacks*) noexcept
{
    if (gFakeState && instance == gFakeState->mInstance && messenger == gFakeState->mDebugMessenger)
    {
        ++gFakeState->mDestroyDebugCalls;
        gFakeState->mEvents.push_back(Event::DestroyDebugMessenger);
    }
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL fakeGetInstanceProcAddr(VkInstance instance, const char* name) noexcept
{
    if (!gFakeState || !name)
    {
        return nullptr;
    }
    if (std::strcmp(name, "vkCreateInstance") == 0)
    {
        return gFakeState->mMissing == MissingCommand::GlobalCreateInstance ? nullptr : eraseFunctionType(fakeCreateInstance);
    }
    if (std::strcmp(name, "vkEnumerateInstanceExtensionProperties") == 0)
    {
        return gFakeState->mMissing == MissingCommand::GlobalEnumerateExtensions
                   ? nullptr
                   : eraseFunctionType(fakeEnumerateInstanceExtensionProperties);
    }
    if (std::strcmp(name, "vkEnumerateInstanceLayerProperties") == 0)
    {
        return gFakeState->mMissing == MissingCommand::GlobalEnumerateLayers ? nullptr
                                                                             : eraseFunctionType(fakeEnumerateInstanceLayerProperties);
    }
    if (std::strcmp(name, "vkEnumerateInstanceVersion") == 0)
    {
        return gFakeState->mMissing == MissingCommand::GlobalEnumerateVersion ? nullptr : eraseFunctionType(fakeEnumerateInstanceVersion);
    }
    if (instance != gFakeState->mInstance)
    {
        return nullptr;
    }
    if (std::strcmp(name, "vkDestroyInstance") == 0)
    {
        return gFakeState->mMissing == MissingCommand::DestroyInstance ? nullptr : eraseFunctionType(fakeDestroyInstance);
    }
    if (std::strcmp(name, "vkCreateDebugUtilsMessengerEXT") == 0)
    {
        return gFakeState->mMissing == MissingCommand::CreateDebugMessenger ? nullptr : eraseFunctionType(fakeCreateDebugUtilsMessenger);
    }
    if (std::strcmp(name, "vkDestroyDebugUtilsMessengerEXT") == 0)
    {
        return gFakeState->mMissing == MissingCommand::DestroyDebugMessenger ? nullptr : eraseFunctionType(fakeDestroyDebugUtilsMessenger);
    }
    return nullptr;
}

bool generationIsCurrent(void* userdata, std::uint64_t generation) noexcept
{
    auto& state = *static_cast<FakeState*>(userdata);
    ++state.mGenerationChecks;
    return generation == 42 && state.mGenerationCurrent &&
           (state.mFailGenerationCheck == 0 || state.mGenerationChecks != state.mFailGenerationCheck);
}

VulkanWindowGenerationCheck generationCheck(FakeState& state) noexcept
{
    return { &state, generationIsCurrent };
}

const std::vector<std::string> REQUIRED_SURFACE{ VK_KHR_SURFACE_EXTENSION_NAME };
constexpr char                 PORTABILITY_EXTENSION[] = "VK_KHR_portability_enumeration";

VulkanInstanceRequest makeRequest(FakeState&                    state,
                                  VulkanInstanceValidationMode  validation_mode     = VulkanInstanceValidationMode::Disabled,
                                  VulkanInstancePortabilityMode portability_mode    = VulkanInstancePortabilityMode::Disabled,
                                  std::span<const std::string>  required_extensions = REQUIRED_SURFACE) noexcept
{
    return { fakeGetInstanceProcAddr, required_extensions, 42, generationCheck(state), validation_mode, portability_mode };
}

const VulkanInstanceAcquireError& requireError(const VulkanInstanceAcquireResult& result)
{
    const auto* error = std::get_if<VulkanInstanceAcquireError>(&result);
    tut::ensure("instance acquisition returns an error", error != nullptr);
    return *error;
}

VulkanInstanceGeneration takeGeneration(VulkanInstanceAcquireResult&& result)
{
    tut::ensure("instance acquisition returns an owner", std::holds_alternative<VulkanInstanceGeneration>(result));
    return std::get<VulkanInstanceGeneration>(std::move(result));
}

void ensureCode(const VulkanInstanceAcquireResult& result, VulkanInstanceAcquireCode code)
{
    tut::ensure("the exact instance error is reported", requireError(result).mCode == code);
}

void failAllocation()
{
    throw std::bad_alloc();
}

void emitValidationMessage(FakeState& state, const char* message)
{
    tut::ensure("the validation callback was retained", state.mValidationCallback != nullptr && state.mValidationUserdata != nullptr);
    VkDebugUtilsMessengerCallbackDataEXT callback_data{};
    callback_data.sType    = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CALLBACK_DATA_EXT;
    callback_data.pMessage = message;
    state.mValidationCallback(VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT, VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT,
                              &callback_data, state.mValidationUserdata);
}

} // namespace

namespace tut
{

struct render_vulkan_instance_test
{
};

using render_vulkan_instance_test_group  = test_group<render_vulkan_instance_test>;
using render_vulkan_instance_test_object = render_vulkan_instance_test_group::object;
render_vulkan_instance_test_group render_vulkan_instance_tests("render Vulkan instance");

template<>
template<>
void render_vulkan_instance_test_object::test<1>()
{
    static_assert(!std::is_default_constructible_v<VulkanInstanceGeneration>);
    static_assert(!std::is_copy_constructible_v<VulkanInstanceGeneration>);
    static_assert(!std::is_copy_assignable_v<VulkanInstanceGeneration>);
    static_assert(std::is_nothrow_move_constructible_v<VulkanInstanceGeneration>);
    static_assert(!std::is_move_assignable_v<VulkanInstanceGeneration>);
    static_assert(std::is_nothrow_destructible_v<VulkanInstanceGeneration>);
    static_assert(std::variant_size_v<VulkanInstanceAcquireResult> == 2);
    static_assert(std::is_same_v<std::variant_alternative_t<0, VulkanInstanceAcquireResult>, VulkanInstanceAcquireError>);
    static_assert(std::is_same_v<std::variant_alternative_t<1, VulkanInstanceAcquireResult>, VulkanInstanceGeneration>);
    static_assert(noexcept(acquireVulkanInstanceGeneration(std::declval<const VulkanInstanceRequest&>())));

    const VulkanInstanceAcquireError left{ VulkanInstanceAcquireCode::MissingRequiredInstanceCommand, VK_SUCCESS, std::nullopt,
                                           VulkanInstanceCommand::DestroyInstance };
    ensure("identical errors compare equal", left == left);
}

template<>
template<>
void render_vulkan_instance_test_object::test<2>()
{
    FakeState       state;
    ScopedFakeState scope(state);

    VulkanInstanceRequest request = makeRequest(state);
    request.mGenerationCheck      = {};
    ensureCode(acquireVulkanInstanceGeneration(request), VulkanInstanceAcquireCode::InvalidGenerationCheck);

    state.mGenerationCurrent = false;
    request                  = makeRequest(state);
    ensureCode(acquireVulkanInstanceGeneration(request), VulkanInstanceAcquireCode::StaleWindowGeneration);
    ensure_equals("a stale generation performs no dispatch lookup", state.mVersionCalls, std::size_t{ 0 });

    state.mGenerationCurrent = true;
    request                  = makeRequest(state);
    request.mValidationMode  = static_cast<VulkanInstanceValidationMode>(99);
    ensureCode(acquireVulkanInstanceGeneration(request), VulkanInstanceAcquireCode::InvalidValidationMode);

    request                  = makeRequest(state);
    request.mPortabilityMode = static_cast<VulkanInstancePortabilityMode>(99);
    ensureCode(acquireVulkanInstanceGeneration(request), VulkanInstanceAcquireCode::InvalidPortabilityMode);

    request                           = makeRequest(state);
    request.mRequiredWindowExtensions = {};
    ensureCode(acquireVulkanInstanceGeneration(request), VulkanInstanceAcquireCode::InvalidRequiredWindowExtensions);

    const std::vector<std::string> duplicate_extensions{ VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_SURFACE_EXTENSION_NAME };
    request = makeRequest(state, VulkanInstanceValidationMode::Disabled, VulkanInstancePortabilityMode::Disabled, duplicate_extensions);
    const auto  duplicate_result = acquireVulkanInstanceGeneration(request);
    const auto& duplicate_error  = requireError(duplicate_result);
    ensure("a duplicate request extension reports its exact index",
           duplicate_error.mCode == VulkanInstanceAcquireCode::InvalidRequiredWindowExtensions &&
               duplicate_error.mRequiredExtensionIndex == 1);
}

template<>
template<>
void render_vulkan_instance_test_object::test<3>()
{
    FakeState state;
    state.mMissing = MissingCommand::GlobalEnumerateVersion;
    ScopedFakeState scope(state);

    const auto  result = acquireVulkanInstanceGeneration(makeRequest(state));
    const auto& error  = requireError(result);
    ensure("Stage 30 failure is nested exactly",
           error.mCode == VulkanInstanceAcquireCode::GlobalDispatchFailure && error.mGlobalDispatchError &&
               error.mGlobalDispatchError->mCode == VulkanGlobalDispatchResolutionCode::InsufficientApiVersion &&
               error.mGlobalDispatchError->mCommand == VulkanGlobalCommand::EnumerateInstanceVersion);
    ensure_equals("global failure makes no property query", state.mExtensionCountCalls, std::size_t{ 0 });
}

template<>
template<>
void render_vulkan_instance_test_object::test<4>()
{
    {
        FakeState state;
        state.mExtensionCountResult = VK_ERROR_OUT_OF_HOST_MEMORY;
        ScopedFakeState scope(state);
        const auto      result = acquireVulkanInstanceGeneration(makeRequest(state));
        const auto&     error  = requireError(result);
        ensure("extension query failure preserves VkResult",
               error.mCode == VulkanInstanceAcquireCode::ExtensionEnumerationFailure && error.mResult == VK_ERROR_OUT_OF_HOST_MEMORY);
    }
    {
        FakeState state;
        state.mExtensionCountOverride = 4097;
        ScopedFakeState scope(state);
        const auto      result = acquireVulkanInstanceGeneration(makeRequest(state));
        const auto&     error  = requireError(result);
        ensure("extension count cap reports the observed count",
               error.mCode == VulkanInstanceAcquireCode::ExtensionCountExceeded && error.mObservedCount == 4097);
    }
    {
        FakeState state;
        state.mIncompleteExtensionCountCalls = 1;
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        ensure("a changing count query converges on its second bounded attempt",
               state.mExtensionCountCalls == 2 && state.mExtensionValuesCalls == 1);
    }
    {
        FakeState state;
        state.mIncompleteExtensionValueCalls = 1;
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        ensure("a changing property list converges on its second bounded attempt",
               state.mExtensionCountCalls == 2 && state.mExtensionValuesCalls == 2);
    }
    {
        FakeState state;
        state.mExtensionCountResult = VK_INCOMPLETE;
        ScopedFakeState scope(state);
        const auto      result = acquireVulkanInstanceGeneration(makeRequest(state));
        ensureCode(result, VulkanInstanceAcquireCode::EnumerationRetryLimitExceeded);
        ensure("an incomplete count query is bounded before allocating",
               state.mExtensionCountCalls == 4 && state.mExtensionValuesCalls == 0);
    }
    {
        FakeState state;
        state.mAlwaysIncompleteExtensions = true;
        ScopedFakeState scope(state);
        const auto      result = acquireVulkanInstanceGeneration(makeRequest(state));
        ensureCode(result, VulkanInstanceAcquireCode::EnumerationRetryLimitExceeded);
        ensure_equals("incomplete enumeration is retried four times", state.mExtensionValuesCalls, std::size_t{ 4 });
    }
    {
        FakeState state;
        state.mExtensionValuesResult = VK_ERROR_OUT_OF_HOST_MEMORY;
        ScopedFakeState scope(state);
        const auto      result = acquireVulkanInstanceGeneration(makeRequest(state));
        const auto&     error  = requireError(result);
        ensure("extension value-query failure preserves VkResult",
               error.mCode == VulkanInstanceAcquireCode::ExtensionEnumerationFailure && error.mResult == VK_ERROR_OUT_OF_HOST_MEMORY);
    }
    {
        FakeState state;
        state.mExtensionWrittenOverride = 2;
        ScopedFakeState scope(state);
        const auto      result = acquireVulkanInstanceGeneration(makeRequest(state));
        const auto&     error  = requireError(result);
        ensure("an overreported written count is rejected without reading beyond the allocation",
               error.mCode == VulkanInstanceAcquireCode::ExtensionCountExceeded && error.mObservedCount == 2);
    }
    {
        FakeState state;
        state.mMalformedExtension = true;
        ScopedFakeState scope(state);
        const auto      result = acquireVulkanInstanceGeneration(makeRequest(state));
        const auto&     error  = requireError(result);
        ensure("malformed extension reports its index",
               error.mCode == VulkanInstanceAcquireCode::MalformedExtensionProperty && error.mPropertyIndex == 0);
    }
}

template<>
template<>
void render_vulkan_instance_test_object::test<5>()
{
    {
        FakeState state;
        state.mExtensions       = { VK_KHR_SURFACE_EXTENSION_NAME, VK_EXT_DEBUG_UTILS_EXTENSION_NAME };
        state.mLayerCountResult = VK_ERROR_INITIALIZATION_FAILED;
        ScopedFakeState scope(state);
        const auto      result = acquireVulkanInstanceGeneration(makeRequest(state, VulkanInstanceValidationMode::Required));
        const auto&     error  = requireError(result);
        ensure("layer query failure preserves VkResult",
               error.mCode == VulkanInstanceAcquireCode::LayerEnumerationFailure && error.mResult == VK_ERROR_INITIALIZATION_FAILED);
    }
    {
        FakeState state;
        state.mExtensions         = { VK_KHR_SURFACE_EXTENSION_NAME, VK_EXT_DEBUG_UTILS_EXTENSION_NAME };
        state.mLayerCountOverride = 4097;
        ScopedFakeState scope(state);
        const auto      result = acquireVulkanInstanceGeneration(makeRequest(state, VulkanInstanceValidationMode::Required));
        const auto&     error  = requireError(result);
        ensure("layer count cap reports the observed count",
               error.mCode == VulkanInstanceAcquireCode::LayerCountExceeded && error.mObservedCount == 4097);
    }
    {
        FakeState state;
        state.mExtensions        = { VK_KHR_SURFACE_EXTENSION_NAME, VK_EXT_DEBUG_UTILS_EXTENSION_NAME };
        state.mLayers            = { "VK_LAYER_KHRONOS_validation" };
        state.mLayerValuesResult = VK_ERROR_OUT_OF_HOST_MEMORY;
        ScopedFakeState scope(state);
        const auto      result = acquireVulkanInstanceGeneration(makeRequest(state, VulkanInstanceValidationMode::Required));
        const auto&     error  = requireError(result);
        ensure("layer value-query failure preserves VkResult",
               error.mCode == VulkanInstanceAcquireCode::LayerEnumerationFailure && error.mResult == VK_ERROR_OUT_OF_HOST_MEMORY);
    }
    {
        FakeState state;
        state.mExtensions     = { VK_KHR_SURFACE_EXTENSION_NAME, VK_EXT_DEBUG_UTILS_EXTENSION_NAME };
        state.mLayers         = { "VK_LAYER_KHRONOS_validation" };
        state.mMalformedLayer = true;
        ScopedFakeState scope(state);
        const auto      result = acquireVulkanInstanceGeneration(makeRequest(state, VulkanInstanceValidationMode::Required));
        const auto&     error  = requireError(result);
        ensure("malformed layer reports its index",
               error.mCode == VulkanInstanceAcquireCode::MalformedLayerProperty && error.mPropertyIndex == 0);
    }
}

template<>
template<>
void render_vulkan_instance_test_object::test<6>()
{
    {
        FakeState state;
        state.mExtensions = { VK_EXT_DEBUG_UTILS_EXTENSION_NAME };
        ScopedFakeState scope(state);
        const auto      result = acquireVulkanInstanceGeneration(makeRequest(state));
        const auto&     error  = requireError(result);
        ensure("the missing SDL extension retains its exact index",
               error.mCode == VulkanInstanceAcquireCode::MissingRequiredWindowExtension && error.mRequiredExtensionIndex == 0);
    }
    {
        FakeState                state;
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        ensure("disabled validation creates a live instance without a messenger",
               owner.instance() == state.mInstance && !owner.validationEnabled() && owner.enabledLayers().empty());
        ensure_equals("disabled validation never enumerates layers", state.mLayerCountCalls, std::size_t{ 0 });
        ensure("the exact API floor is requested and retained",
               state.mRequestedApiVersion == RENDERER_VULKAN_API_VERSION && owner.apiVersion() == RENDERER_VULKAN_API_VERSION);
        owner.reset();
        ensure_equals("reset destroys the instance once", state.mDestroyInstanceCalls, std::size_t{ 1 });
    }
}

template<>
template<>
void render_vulkan_instance_test_object::test<7>()
{
    {
        FakeState state;
        state.mExtensions = { VK_KHR_SURFACE_EXTENSION_NAME, PORTABILITY_EXTENSION };
        ScopedFakeState          scope(state);
        VulkanInstanceGeneration owner = takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state)));
        ensure("disabled portability neither enables the extension nor sets the flag",
               !owner.portabilityEnumerationEnabled() && !owner.isExtensionEnabled(PORTABILITY_EXTENSION) && state.mInstanceFlags == 0);
    }
    {
        FakeState state;
        state.mExtensions = { VK_KHR_SURFACE_EXTENSION_NAME, VK_EXT_DEBUG_UTILS_EXTENSION_NAME };
        ScopedFakeState scope(state);
        ensureCode(acquireVulkanInstanceGeneration(makeRequest(state, VulkanInstanceValidationMode::Required)),
                   VulkanInstanceAcquireCode::MissingValidationLayer);
    }
    {
        FakeState state;
        state.mLayers = { "VK_LAYER_KHRONOS_validation" };
        ScopedFakeState scope(state);
        ensureCode(acquireVulkanInstanceGeneration(makeRequest(state, VulkanInstanceValidationMode::Required)),
                   VulkanInstanceAcquireCode::MissingValidationExtension);
    }
    {
        FakeState state;
        state.mExtensions = { VK_KHR_SURFACE_EXTENSION_NAME, VK_EXT_DEBUG_UTILS_EXTENSION_NAME };
        state.mLayers     = { "VK_LAYER_KHRONOS_validation" };
        ScopedFakeState                scope(state);
        const std::vector<std::string> required{ VK_KHR_SURFACE_EXTENSION_NAME, VK_EXT_DEBUG_UTILS_EXTENSION_NAME };
        VulkanInstanceGeneration       owner = takeGeneration(acquireVulkanInstanceGeneration(
            makeRequest(state, VulkanInstanceValidationMode::Required, VulkanInstancePortabilityMode::Disabled, required)));
        ensure("required validation owns its messenger", owner.validationEnabled());
        ensure("debug utils is not duplicated",
               std::count(owner.enabledExtensions().begin(), owner.enabledExtensions().end(), VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 1);
        ensure("the exact validation layer is retained",
               owner.enabledLayers() == std::vector<std::string>{ "VK_LAYER_KHRONOS_validation" });
        owner.reset();
        const std::vector<Event> expected{ Event::CreateInstance, Event::CreateDebugMessenger, Event::DestroyDebugMessenger,
                                           Event::DestroyInstance };
        ensure("debug teardown precedes instance teardown", state.mEvents == expected);
    }
    {
        FakeState state;
        state.mExtensions = { VK_KHR_SURFACE_EXTENSION_NAME, PORTABILITY_EXTENSION };
        ScopedFakeState scope(state);
        auto            result = acquireVulkanInstanceGeneration(
            makeRequest(state, VulkanInstanceValidationMode::Disabled, VulkanInstancePortabilityMode::EnableIfAvailable));
#if defined(VK_KHR_portability_enumeration)
        VulkanInstanceGeneration owner = takeGeneration(std::move(result));
        ensure("advertised portability is enabled once with its create flag",
               owner.portabilityEnumerationEnabled() && owner.isExtensionEnabled(PORTABILITY_EXTENSION) &&
                   (state.mInstanceFlags & VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR) != 0);
#else
        ensureCode(result, VulkanInstanceAcquireCode::PortabilityPolicyUnavailable);
        ensure("older headers reject an unsupported portability policy before instance creation", state.mEvents.empty());
#endif
    }
}

template<>
template<>
void render_vulkan_instance_test_object::test<8>()
{
    {
        FakeState state;
        state.mInstanceResult = VK_ERROR_INCOMPATIBLE_DRIVER;
        ScopedFakeState scope(state);
        const auto      result = acquireVulkanInstanceGeneration(makeRequest(state));
        const auto&     error  = requireError(result);
        ensure("instance failure preserves VkResult and owns no poisoned output",
               error.mCode == VulkanInstanceAcquireCode::InstanceCreationFailure && error.mResult == VK_ERROR_INCOMPATIBLE_DRIVER &&
                   state.mDestroyInstanceCalls == 0);
    }
    {
        FakeState state;
        state.mNullInstance = true;
        ScopedFakeState scope(state);
        ensureCode(acquireVulkanInstanceGeneration(makeRequest(state)), VulkanInstanceAcquireCode::NullInstanceOnSuccess);
        ensure_equals("a null instance has no destruction obligation", state.mDestroyInstanceCalls, std::size_t{ 0 });
    }
}

template<>
template<>
void render_vulkan_instance_test_object::test<9>()
{
    {
        FakeState state;
        state.mMissing = MissingCommand::DestroyInstance;
        ScopedFakeState scope(state);
        const auto      result = acquireVulkanInstanceGeneration(makeRequest(state));
        const auto&     error  = requireError(result);
        ensure("missing destroy is reported as the exact nonrecoverable loader breach",
               error.mCode == VulkanInstanceAcquireCode::MissingRequiredInstanceCommand &&
                   error.mCommand == VulkanInstanceCommand::DestroyInstance && state.mDestroyInstanceCalls == 0);
    }

    constexpr std::array missing_debug_commands{ MissingCommand::CreateDebugMessenger, MissingCommand::DestroyDebugMessenger };
    constexpr std::array reported_debug_commands{ VulkanInstanceCommand::CreateDebugUtilsMessenger,
                                                  VulkanInstanceCommand::DestroyDebugUtilsMessenger };
    for (std::size_t index = 0; index < missing_debug_commands.size(); ++index)
    {
        FakeState state;
        state.mMissing    = missing_debug_commands[index];
        state.mExtensions = { VK_KHR_SURFACE_EXTENSION_NAME, VK_EXT_DEBUG_UTILS_EXTENSION_NAME };
        state.mLayers     = { "VK_LAYER_KHRONOS_validation" };
        ScopedFakeState scope(state);
        const auto      result = acquireVulkanInstanceGeneration(makeRequest(state, VulkanInstanceValidationMode::Required));
        const auto&     error  = requireError(result);
        ensure("missing debug command is reported and the instance rolls back",
               error.mCode == VulkanInstanceAcquireCode::MissingRequiredInstanceCommand &&
                   error.mCommand == reported_debug_commands[index] && state.mDestroyInstanceCalls == 1 && state.mDestroyDebugCalls == 0);
    }
}

template<>
template<>
void render_vulkan_instance_test_object::test<10>()
{
    {
        FakeState state;
        state.mExtensions  = { VK_KHR_SURFACE_EXTENSION_NAME, VK_EXT_DEBUG_UTILS_EXTENSION_NAME };
        state.mLayers      = { "VK_LAYER_KHRONOS_validation" };
        state.mDebugResult = VK_ERROR_OUT_OF_HOST_MEMORY;
        ScopedFakeState scope(state);
        const auto      result = acquireVulkanInstanceGeneration(makeRequest(state, VulkanInstanceValidationMode::Required));
        const auto&     error  = requireError(result);
        ensure("debug failure preserves VkResult and rolls back only the instance",
               error.mCode == VulkanInstanceAcquireCode::DebugMessengerCreationFailure && error.mResult == VK_ERROR_OUT_OF_HOST_MEMORY &&
                   state.mDestroyDebugCalls == 0 && state.mDestroyInstanceCalls == 1);
    }
    {
        FakeState state;
        state.mExtensions         = { VK_KHR_SURFACE_EXTENSION_NAME, VK_EXT_DEBUG_UTILS_EXTENSION_NAME };
        state.mLayers             = { "VK_LAYER_KHRONOS_validation" };
        state.mNullDebugMessenger = true;
        ScopedFakeState scope(state);
        ensureCode(acquireVulkanInstanceGeneration(makeRequest(state, VulkanInstanceValidationMode::Required)),
                   VulkanInstanceAcquireCode::NullDebugMessengerOnSuccess);
        ensure("a null messenger rolls back only the instance", state.mDestroyDebugCalls == 0 && state.mDestroyInstanceCalls == 1);
    }
}

template<>
template<>
void render_vulkan_instance_test_object::test<11>()
{
    {
        FakeState state;
        state.mFailGenerationCheck = 2;
        ScopedFakeState scope(state);
        ensureCode(acquireVulkanInstanceGeneration(makeRequest(state)), VulkanInstanceAcquireCode::StaleWindowGeneration);
        ensure("staleness after property enumeration publishes no native object",
               state.mExtensionValuesCalls == 1 && state.mEvents.empty());
    }
    {
        FakeState state;
        state.mFailGenerationCheck = 3;
        ScopedFakeState scope(state);
        ensureCode(acquireVulkanInstanceGeneration(makeRequest(state)), VulkanInstanceAcquireCode::StaleWindowGeneration);
        ensure("staleness immediately before instance creation publishes no native object", state.mEvents.empty());
    }
    {
        FakeState state;
        state.mFailGenerationCheck = 4;
        ScopedFakeState scope(state);
        ensureCode(acquireVulkanInstanceGeneration(makeRequest(state)), VulkanInstanceAcquireCode::StaleWindowGeneration);
        ensure_equals("staleness after instance creation rolls it back", state.mDestroyInstanceCalls, std::size_t{ 1 });
    }
    {
        FakeState state;
        state.mExtensions          = { VK_KHR_SURFACE_EXTENSION_NAME, VK_EXT_DEBUG_UTILS_EXTENSION_NAME };
        state.mLayers              = { "VK_LAYER_KHRONOS_validation" };
        state.mFailGenerationCheck = 5;
        ScopedFakeState scope(state);
        ensureCode(acquireVulkanInstanceGeneration(makeRequest(state, VulkanInstanceValidationMode::Required)),
                   VulkanInstanceAcquireCode::StaleWindowGeneration);
        ensure("final staleness destroys messenger before instance",
               state.mEvents == std::vector<Event>{ Event::CreateInstance, Event::CreateDebugMessenger, Event::DestroyDebugMessenger,
                                                    Event::DestroyInstance });
    }
}

template<>
template<>
void render_vulkan_instance_test_object::test<12>()
{
    FakeState state;
    state.mExtensions = { VK_KHR_SURFACE_EXTENSION_NAME, VK_EXT_DEBUG_UTILS_EXTENSION_NAME };
    state.mLayers     = { "VK_LAYER_KHRONOS_validation" };
    ScopedFakeState          scope(state);
    VulkanInstanceGeneration first =
        takeGeneration(acquireVulkanInstanceGeneration(makeRequest(state, VulkanInstanceValidationMode::Required)));

    emitValidationMessage(state, "before move");
    VulkanInstanceGeneration moved(std::move(first));
    emitValidationMessage(state, "after move");

    ensure("a move transfers the native handle", first.instance() == VK_NULL_HANDLE && moved.instance() == state.mInstance);
    const VulkanValidationSnapshot snapshot = moved.validationSnapshot();
    ensure("callback userdata remains stable across the owner move",
           snapshot.mMessageCount == 2 && snapshot.firstMessage() == "before move");
    moved.reset();
    moved.reset();
    ensure("reset is idempotent and preserves teardown order",
           state.mDestroyDebugCalls == 1 && state.mDestroyInstanceCalls == 1 &&
               state.mEvents == std::vector<Event>{ Event::CreateInstance, Event::CreateDebugMessenger, Event::DestroyDebugMessenger,
                                                    Event::DestroyInstance });

    FakeState concurrent_state;
    concurrent_state.mExtensions = { VK_KHR_SURFACE_EXTENSION_NAME, VK_EXT_DEBUG_UTILS_EXTENSION_NAME };
    concurrent_state.mLayers     = { "VK_LAYER_KHRONOS_validation" };
    scope.use(concurrent_state);
    VulkanInstanceGeneration concurrent =
        takeGeneration(acquireVulkanInstanceGeneration(makeRequest(concurrent_state, VulkanInstanceValidationMode::Required)));

    const auto callback = concurrent_state.mValidationCallback;
    void*      userdata = concurrent_state.mValidationUserdata;
    ensure("the concurrent validation callback fixture is published", callback != nullptr && userdata != nullptr);

    std::atomic<bool> start{ false };
    std::atomic<bool> done{ false };
    std::atomic<bool> coherent{ true };
    std::thread       reader(
        [&]
        {
            while (!start.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
            do
            {
                const VulkanValidationSnapshot current = concurrent.validationSnapshot();
                if ((current.mMessageCount == 0) != current.firstMessage().empty())
                {
                    coherent.store(false, std::memory_order_relaxed);
                }
                std::this_thread::yield();
            } while (!done.load(std::memory_order_acquire));
        });
    std::thread writer(
        [&]
        {
            while (!start.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
            VkDebugUtilsMessengerCallbackDataEXT callback_data{};
            callback_data.sType    = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CALLBACK_DATA_EXT;
            callback_data.pMessage = "concurrent callback";
            for (std::uint32_t index = 0; index < 10000; ++index)
            {
                callback(VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT, VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT, &callback_data,
                         userdata);
            }
            done.store(true, std::memory_order_release);
        });
    start.store(true, std::memory_order_release);
    writer.join();
    reader.join();

    const VulkanValidationSnapshot concurrent_snapshot = concurrent.validationSnapshot();
    ensure("concurrent validation snapshots keep their count and first message coherent",
           coherent.load(std::memory_order_relaxed) && concurrent_snapshot.mMessageCount == 10000 &&
               concurrent_snapshot.firstMessage() == "concurrent callback");
}

template<>
template<>
void render_vulkan_instance_test_object::test<13>()
{
    FakeState       state;
    ScopedFakeState scope(state);
    const auto      result = VulkanInstanceDetail::acquire(makeRequest(state), failAllocation);
    ensureCode(result, VulkanInstanceAcquireCode::AllocationFailure);
    ensure("allocation failure occurs before instance ownership", state.mEvents.empty() && state.mDestroyInstanceCalls == 0);
}

} // namespace tut
