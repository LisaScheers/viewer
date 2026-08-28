/**
 * @file llrendervulkanphysicaldevice_test.cpp
 * @brief Tests for loader-neutral Vulkan presentation-device selection.
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

#include "llrendervulkanphysicaldevice.h"
#include "lltut.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace
{
using namespace LLRenderVulkan;

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

enum class MissingCommand : std::uint8_t
{
    None,
    EnumeratePhysicalDevices,
    GetPhysicalDeviceProperties,
    GetPhysicalDeviceQueueFamilyProperties,
    GetPhysicalDeviceSurfaceSupport,
    EnumerateDeviceExtensionProperties
};

enum class MalformedExtension : std::uint8_t
{
    None,
    Empty,
    Unterminated
};

struct DeviceRecord
{
    VkPhysicalDevice                     mHandle = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties           mProperties{};
    std::vector<VkQueueFamilyProperties> mQueueFamilies;
    std::vector<VkBool32>                mPresentSupport;
    std::vector<std::string>             mExtensions;
};

struct FakeState
{
    VkInstance   mInstance = fakeHandle<VkInstance>(0x1000);
    VkSurfaceKHR mSurface  = fakeHandle<VkSurfaceKHR>(0x2000);

    MissingCommand           mMissingCommand = MissingCommand::None;
    std::vector<std::string> mResolvedCommands;

    std::vector<DeviceRecord> mDevices;
    std::vector<std::size_t>  mPropertyCalls;
    std::vector<std::size_t>  mQueueCountCalls;
    std::vector<std::size_t>  mQueueListCalls;
    std::vector<std::size_t>  mExtensionCountCallsByDevice;
    std::vector<std::size_t>  mExtensionListCallsByDevice;

    VkResult      mPhysicalCountResult          = VK_SUCCESS;
    VkResult      mPhysicalListResult           = VK_SUCCESS;
    std::uint32_t mPhysicalCountOverride        = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t mPhysicalWrittenOverride      = std::numeric_limits<std::uint32_t>::max();
    std::size_t   mPhysicalIncompleteCountCalls = 0;
    std::size_t   mPhysicalIncompleteListCalls  = 0;
    bool          mPhysicalAlwaysIncomplete     = false;
    std::size_t   mPhysicalCountCalls           = 0;
    std::size_t   mPhysicalListCalls            = 0;

    VkResult           mExtensionCountResult          = VK_SUCCESS;
    VkResult           mExtensionListResult           = VK_SUCCESS;
    std::uint32_t      mExtensionCountOverride        = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t      mExtensionWrittenOverride      = std::numeric_limits<std::uint32_t>::max();
    std::size_t        mExtensionIncompleteCountCalls = 0;
    std::size_t        mExtensionIncompleteListCalls  = 0;
    bool               mExtensionAlwaysIncomplete     = false;
    MalformedExtension mMalformedExtension            = MalformedExtension::None;

    std::uint32_t mQueueCountOverride   = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t mQueueWrittenOverride = std::numeric_limits<std::uint32_t>::max();

    VkResult         mSurfaceSupportResult = VK_SUCCESS;
    std::size_t      mSurfaceSupportCalls  = 0;
    VkPhysicalDevice mLastSurfaceDevice    = VK_NULL_HANDLE;
    std::uint32_t    mLastSurfaceQueue     = VK_QUEUE_FAMILY_IGNORED;
    VkSurfaceKHR     mLastSurface          = VK_NULL_HANDLE;

    void synchronizeCounters()
    {
        mPropertyCalls.assign(mDevices.size(), 0);
        mQueueCountCalls.assign(mDevices.size(), 0);
        mQueueListCalls.assign(mDevices.size(), 0);
        mExtensionCountCallsByDevice.assign(mDevices.size(), 0);
        mExtensionListCallsByDevice.assign(mDevices.size(), 0);
    }
};

FakeState* gFakeState = nullptr;

class ScopedFakeState
{
public:
    explicit ScopedFakeState(FakeState& state) noexcept : mPrevious(gFakeState) { gFakeState = &state; }
    ~ScopedFakeState() noexcept { gFakeState = mPrevious; }

    ScopedFakeState(const ScopedFakeState&)            = delete;
    ScopedFakeState& operator=(const ScopedFakeState&) = delete;

private:
    FakeState* mPrevious = nullptr;
};

VkQueueFamilyProperties queueFamily(VkQueueFlags flags, std::uint32_t count = 1) noexcept
{
    VkQueueFamilyProperties properties{};
    properties.queueFlags = flags;
    properties.queueCount = count;
    return properties;
}

DeviceRecord deviceRecord(std::uintptr_t                       handle,
                          std::uint32_t                        api_version,
                          std::vector<std::string>             extensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME },
                          std::vector<VkQueueFamilyProperties> queues     = { queueFamily(VK_QUEUE_GRAPHICS_BIT) },
                          std::vector<VkBool32>                present    = { VK_TRUE })
{
    DeviceRecord record;
    record.mHandle                = fakeHandle<VkPhysicalDevice>(handle);
    record.mProperties.apiVersion = api_version;
    record.mProperties.deviceType = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
    std::snprintf(record.mProperties.deviceName, VK_MAX_PHYSICAL_DEVICE_NAME_SIZE, "fake-%zx", static_cast<std::size_t>(handle));
    record.mQueueFamilies  = std::move(queues);
    record.mPresentSupport = std::move(present);
    record.mExtensions     = std::move(extensions);
    return record;
}

DeviceRecord nullDeviceRecord()
{
    DeviceRecord record;
    record.mHandle = VK_NULL_HANDLE;
    return record;
}

std::optional<std::size_t> deviceIndex(VkPhysicalDevice device) noexcept
{
    if (!gFakeState)
    {
        return std::nullopt;
    }
    for (std::size_t index = 0; index < gFakeState->mDevices.size(); ++index)
    {
        if (gFakeState->mDevices[index].mHandle == device)
        {
            return index;
        }
    }
    return std::nullopt;
}

template<typename Function>
PFN_vkVoidFunction eraseFunctionType(Function function) noexcept
{
    return reinterpret_cast<PFN_vkVoidFunction>(function);
}

VKAPI_ATTR VkResult VKAPI_CALL fakeEnumeratePhysicalDevices(VkInstance instance, std::uint32_t* count, VkPhysicalDevice* devices) noexcept
{
    if (!gFakeState || instance != gFakeState->mInstance || !count)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!devices)
    {
        ++gFakeState->mPhysicalCountCalls;
        *count = gFakeState->mPhysicalCountOverride == std::numeric_limits<std::uint32_t>::max()
                     ? static_cast<std::uint32_t>(gFakeState->mDevices.size())
                     : gFakeState->mPhysicalCountOverride;
        if (gFakeState->mPhysicalAlwaysIncomplete || gFakeState->mPhysicalCountCalls <= gFakeState->mPhysicalIncompleteCountCalls)
        {
            return VK_INCOMPLETE;
        }
        return gFakeState->mPhysicalCountResult;
    }

    ++gFakeState->mPhysicalListCalls;
    const std::uint32_t capacity   = *count;
    const std::uint32_t written    = gFakeState->mPhysicalWrittenOverride == std::numeric_limits<std::uint32_t>::max()
                                         ? static_cast<std::uint32_t>(gFakeState->mDevices.size())
                                         : gFakeState->mPhysicalWrittenOverride;
    const std::size_t   fill_count = std::min<std::size_t>(capacity, gFakeState->mDevices.size());
    for (std::size_t index = 0; index < fill_count; ++index)
    {
        devices[index] = gFakeState->mDevices[index].mHandle;
    }
    *count = written;
    if (gFakeState->mPhysicalAlwaysIncomplete || gFakeState->mPhysicalListCalls <= gFakeState->mPhysicalIncompleteListCalls)
    {
        return VK_INCOMPLETE;
    }
    return gFakeState->mPhysicalListResult;
}

VKAPI_ATTR void VKAPI_CALL fakeGetPhysicalDeviceProperties(VkPhysicalDevice device, VkPhysicalDeviceProperties* properties) noexcept
{
    const std::optional<std::size_t> index = deviceIndex(device);
    if (!index || !properties)
    {
        return;
    }
    ++gFakeState->mPropertyCalls[*index];
    *properties = gFakeState->mDevices[*index].mProperties;
}

VKAPI_ATTR void VKAPI_CALL fakeGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice         device,
                                                                      std::uint32_t*           count,
                                                                      VkQueueFamilyProperties* properties) noexcept
{
    const std::optional<std::size_t> index = deviceIndex(device);
    if (!index || !count)
    {
        return;
    }
    const DeviceRecord& record = gFakeState->mDevices[*index];
    if (!properties)
    {
        ++gFakeState->mQueueCountCalls[*index];
        *count = gFakeState->mQueueCountOverride == std::numeric_limits<std::uint32_t>::max()
                     ? static_cast<std::uint32_t>(record.mQueueFamilies.size())
                     : gFakeState->mQueueCountOverride;
        return;
    }

    ++gFakeState->mQueueListCalls[*index];
    const std::uint32_t capacity   = *count;
    const std::uint32_t written    = gFakeState->mQueueWrittenOverride == std::numeric_limits<std::uint32_t>::max()
                                         ? static_cast<std::uint32_t>(record.mQueueFamilies.size())
                                         : gFakeState->mQueueWrittenOverride;
    const std::size_t   fill_count = std::min<std::size_t>(capacity, record.mQueueFamilies.size());
    for (std::size_t queue = 0; queue < fill_count; ++queue)
    {
        properties[queue] = record.mQueueFamilies[queue];
    }
    *count = written;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeGetPhysicalDeviceSurfaceSupport(VkPhysicalDevice device,
                                                                   std::uint32_t    queue_family,
                                                                   VkSurfaceKHR     surface,
                                                                   VkBool32*        supported) noexcept
{
    if (!gFakeState || !supported)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    ++gFakeState->mSurfaceSupportCalls;
    gFakeState->mLastSurfaceDevice = device;
    gFakeState->mLastSurfaceQueue  = queue_family;
    gFakeState->mLastSurface       = surface;
    if (gFakeState->mSurfaceSupportResult != VK_SUCCESS)
    {
        return gFakeState->mSurfaceSupportResult;
    }

    const std::optional<std::size_t> index = deviceIndex(device);
    if (!index || surface != gFakeState->mSurface || queue_family >= gFakeState->mDevices[*index].mPresentSupport.size())
    {
        return VK_ERROR_SURFACE_LOST_KHR;
    }
    *supported = gFakeState->mDevices[*index].mPresentSupport[queue_family];
    return VK_SUCCESS;
}

template<std::size_t Size>
void copyExtensionName(char (&destination)[Size], const std::string& source, MalformedExtension malformed) noexcept
{
    if (malformed == MalformedExtension::Unterminated)
    {
        std::fill(std::begin(destination), std::end(destination), 'x');
        return;
    }
    if (malformed == MalformedExtension::Empty)
    {
        destination[0] = '\0';
        return;
    }
    const std::size_t count = std::min(source.size(), Size - 1);
    std::memcpy(destination, source.data(), count);
    destination[count] = '\0';
}

VKAPI_ATTR VkResult VKAPI_CALL fakeEnumerateDeviceExtensionProperties(VkPhysicalDevice       device,
                                                                      const char*            layer_name,
                                                                      std::uint32_t*         count,
                                                                      VkExtensionProperties* properties) noexcept
{
    const std::optional<std::size_t> index = deviceIndex(device);
    if (!gFakeState || !index || layer_name || !count)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    const DeviceRecord& record = gFakeState->mDevices[*index];
    if (!properties)
    {
        ++gFakeState->mExtensionCountCallsByDevice[*index];
        *count = gFakeState->mExtensionCountOverride == std::numeric_limits<std::uint32_t>::max()
                     ? static_cast<std::uint32_t>(record.mExtensions.size())
                     : gFakeState->mExtensionCountOverride;
        if (gFakeState->mExtensionAlwaysIncomplete ||
            gFakeState->mExtensionCountCallsByDevice[*index] <= gFakeState->mExtensionIncompleteCountCalls)
        {
            return VK_INCOMPLETE;
        }
        return gFakeState->mExtensionCountResult;
    }

    ++gFakeState->mExtensionListCallsByDevice[*index];
    const std::uint32_t capacity   = *count;
    const std::uint32_t written    = gFakeState->mExtensionWrittenOverride == std::numeric_limits<std::uint32_t>::max()
                                         ? static_cast<std::uint32_t>(record.mExtensions.size())
                                         : gFakeState->mExtensionWrittenOverride;
    const std::size_t   fill_count = std::min<std::size_t>(capacity, record.mExtensions.size());
    for (std::size_t extension = 0; extension < fill_count; ++extension)
    {
        copyExtensionName(properties[extension].extensionName, record.mExtensions[extension],
                          extension == 0 ? gFakeState->mMalformedExtension : MalformedExtension::None);
    }
    *count = written;
    if (gFakeState->mExtensionAlwaysIncomplete ||
        gFakeState->mExtensionListCallsByDevice[*index] <= gFakeState->mExtensionIncompleteListCalls)
    {
        return VK_INCOMPLETE;
    }
    return gFakeState->mExtensionListResult;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL fakeGetInstanceProcAddr(VkInstance instance, const char* name) noexcept
{
    if (!gFakeState || instance != gFakeState->mInstance || !name)
    {
        return nullptr;
    }
    gFakeState->mResolvedCommands.emplace_back(name);
    if (std::strcmp(name, "vkEnumeratePhysicalDevices") == 0)
    {
        return gFakeState->mMissingCommand == MissingCommand::EnumeratePhysicalDevices ? nullptr
                                                                                       : eraseFunctionType(fakeEnumeratePhysicalDevices);
    }
    if (std::strcmp(name, "vkGetPhysicalDeviceProperties") == 0)
    {
        return gFakeState->mMissingCommand == MissingCommand::GetPhysicalDeviceProperties
                   ? nullptr
                   : eraseFunctionType(fakeGetPhysicalDeviceProperties);
    }
    if (std::strcmp(name, "vkGetPhysicalDeviceQueueFamilyProperties") == 0)
    {
        return gFakeState->mMissingCommand == MissingCommand::GetPhysicalDeviceQueueFamilyProperties
                   ? nullptr
                   : eraseFunctionType(fakeGetPhysicalDeviceQueueFamilyProperties);
    }
    if (std::strcmp(name, "vkGetPhysicalDeviceSurfaceSupportKHR") == 0)
    {
        return gFakeState->mMissingCommand == MissingCommand::GetPhysicalDeviceSurfaceSupport
                   ? nullptr
                   : eraseFunctionType(fakeGetPhysicalDeviceSurfaceSupport);
    }
    if (std::strcmp(name, "vkEnumerateDeviceExtensionProperties") == 0)
    {
        return gFakeState->mMissingCommand == MissingCommand::EnumerateDeviceExtensionProperties
                   ? nullptr
                   : eraseFunctionType(fakeEnumerateDeviceExtensionProperties);
    }
    return nullptr;
}

VulkanPhysicalDeviceRequest request(const FakeState& state) noexcept
{
    return { fakeGetInstanceProcAddr, state.mInstance, state.mSurface };
}

FakeState canonicalState()
{
    FakeState state;
    state.mDevices.push_back(deviceRecord(0x3000, VK_API_VERSION_1_1));
    state.synchronizeCounters();
    return state;
}

VulkanPhysicalDeviceResolutionError requireError(const VulkanPhysicalDeviceResolutionResult& result)
{
    const auto* error = std::get_if<VulkanPhysicalDeviceResolutionError>(&result);
    tut::ensure("physical-device resolution returns an error", error != nullptr);
    return *error;
}

void ensureCode(const VulkanPhysicalDeviceResolutionResult& result, VulkanPhysicalDeviceResolutionCode code)
{
    tut::ensure("the exact physical-device error is reported", requireError(result).mCode == code);
}

VulkanPhysicalDeviceGeneration takeGeneration(VulkanPhysicalDeviceResolutionResult&& result)
{
    tut::ensure("physical-device resolution returns a generation", std::holds_alternative<VulkanPhysicalDeviceGeneration>(result));
    return std::get<VulkanPhysicalDeviceGeneration>(std::move(result));
}

} // namespace

namespace tut
{

struct render_vulkan_physical_device_test
{
};

using render_vulkan_physical_device_group  = test_group<render_vulkan_physical_device_test>;
using render_vulkan_physical_device_object = render_vulkan_physical_device_group::object;
render_vulkan_physical_device_group render_vulkan_physical_device_tests("render Vulkan physical device");

template<>
template<>
void render_vulkan_physical_device_object::test<1>()
{
    static_assert(!std::is_default_constructible_v<VulkanPhysicalDeviceGeneration>);
    static_assert(!std::is_copy_constructible_v<VulkanPhysicalDeviceGeneration>);
    static_assert(!std::is_copy_assignable_v<VulkanPhysicalDeviceGeneration>);
    static_assert(std::is_nothrow_move_constructible_v<VulkanPhysicalDeviceGeneration>);
    static_assert(!std::is_move_assignable_v<VulkanPhysicalDeviceGeneration>);
    static_assert(std::variant_size_v<VulkanPhysicalDeviceResolutionResult> == 2);
    static_assert(noexcept(resolveVulkanPhysicalDeviceGeneration(std::declval<const VulkanPhysicalDeviceRequest&>())));

    const VulkanPhysicalDeviceResolutionError value{ VulkanPhysicalDeviceResolutionCode::SurfaceSupportQueryFailure,
                                                     VulkanPhysicalDeviceCommand::GetPhysicalDeviceSurfaceSupport,
                                                     std::nullopt,
                                                     VK_ERROR_SURFACE_LOST_KHR,
                                                     2,
                                                     3,
                                                     std::nullopt,
                                                     0,
                                                     0 };
    ensure("identical typed errors compare equal", value == value);

    FakeState       state = canonicalState();
    ScopedFakeState scope(state);

    VulkanPhysicalDeviceRequest invalid = request(state);
    invalid.mGetInstanceProcAddr        = nullptr;
    ensureCode(resolveVulkanPhysicalDeviceGeneration(invalid), VulkanPhysicalDeviceResolutionCode::InvalidGetInstanceProcAddr);
    invalid           = request(state);
    invalid.mInstance = VK_NULL_HANDLE;
    ensureCode(resolveVulkanPhysicalDeviceGeneration(invalid), VulkanPhysicalDeviceResolutionCode::InvalidInstance);
    invalid          = request(state);
    invalid.mSurface = VK_NULL_HANDLE;
    ensureCode(resolveVulkanPhysicalDeviceGeneration(invalid), VulkanPhysicalDeviceResolutionCode::InvalidSurface);
    ensure("request preflight resolves no command and performs no query",
           state.mResolvedCommands.empty() && state.mPhysicalCountCalls == 0);
}

template<>
template<>
void render_vulkan_physical_device_object::test<2>()
{
    constexpr std::array missing{ MissingCommand::EnumeratePhysicalDevices, MissingCommand::GetPhysicalDeviceProperties,
                                  MissingCommand::GetPhysicalDeviceQueueFamilyProperties, MissingCommand::GetPhysicalDeviceSurfaceSupport,
                                  MissingCommand::EnumerateDeviceExtensionProperties };
    constexpr std::array commands{ VulkanPhysicalDeviceCommand::EnumeratePhysicalDevices,
                                   VulkanPhysicalDeviceCommand::GetPhysicalDeviceProperties,
                                   VulkanPhysicalDeviceCommand::GetPhysicalDeviceQueueFamilyProperties,
                                   VulkanPhysicalDeviceCommand::GetPhysicalDeviceSurfaceSupport,
                                   VulkanPhysicalDeviceCommand::EnumerateDeviceExtensionProperties };

    for (std::size_t index = 0; index < missing.size(); ++index)
    {
        FakeState state       = canonicalState();
        state.mMissingCommand = missing[index];
        ScopedFakeState scope(state);
        const auto      result = resolveVulkanPhysicalDeviceGeneration(request(state));
        const auto&     error  = requireError(result);
        ensure("a missing command retains its exact identity",
               error.mCode == VulkanPhysicalDeviceResolutionCode::MissingRequiredCommand && error.mCommand == commands[index]);
        ensure_equals("resolution stops at the missing command", state.mResolvedCommands.size(), index + 1);
        ensure_equals("missing dispatch performs no physical-device query", state.mPhysicalCountCalls, std::size_t{ 0 });
    }
}

template<>
template<>
void render_vulkan_physical_device_object::test<3>()
{
    {
        FakeState state            = canonicalState();
        state.mPhysicalCountResult = VK_ERROR_OUT_OF_HOST_MEMORY;
        ScopedFakeState scope(state);
        const auto&     error = requireError(resolveVulkanPhysicalDeviceGeneration(request(state)));
        ensure("physical count failure preserves its VkResult",
               error.mCode == VulkanPhysicalDeviceResolutionCode::PhysicalDeviceEnumerationFailure &&
                   error.mResult == VK_ERROR_OUT_OF_HOST_MEMORY && error.mEnumerationAttempt == 1);
    }
    {
        FakeState state           = canonicalState();
        state.mPhysicalListResult = VK_ERROR_DEVICE_LOST;
        ScopedFakeState scope(state);
        const auto&     error = requireError(resolveVulkanPhysicalDeviceGeneration(request(state)));
        ensure("physical list failure preserves its VkResult",
               error.mCode == VulkanPhysicalDeviceResolutionCode::PhysicalDeviceEnumerationFailure &&
                   error.mResult == VK_ERROR_DEVICE_LOST && error.mEnumerationAttempt == 1);
    }
    {
        FakeState state              = canonicalState();
        state.mPhysicalCountOverride = VULKAN_PRESENTATION_MAX_PHYSICAL_DEVICES + 1;
        ScopedFakeState scope(state);
        const auto&     error = requireError(resolveVulkanPhysicalDeviceGeneration(request(state)));
        ensure("physical enumeration is bounded before allocation",
               error.mCode == VulkanPhysicalDeviceResolutionCode::PhysicalDeviceCountExceeded &&
                   error.mObservedCount == VULKAN_PRESENTATION_MAX_PHYSICAL_DEVICES + 1);
    }
    {
        FakeState state                = canonicalState();
        state.mPhysicalWrittenOverride = 2;
        ScopedFakeState scope(state);
        const auto&     error = requireError(resolveVulkanPhysicalDeviceGeneration(request(state)));
        ensure("physical enumeration rejects output above capacity",
               error.mCode == VulkanPhysicalDeviceResolutionCode::InvalidPhysicalDeviceEnumerationOutput && error.mObservedCount == 2);
    }
    {
        FakeState state                     = canonicalState();
        state.mPhysicalIncompleteCountCalls = 1;
        state.mPhysicalIncompleteListCalls  = 1;
        ScopedFakeState                scope(state);
        VulkanPhysicalDeviceGeneration generation = takeGeneration(resolveVulkanPhysicalDeviceGeneration(request(state)));
        ensure("bounded physical retries restart the whole transaction",
               generation.physicalDevice() == state.mDevices[0].mHandle && state.mPhysicalCountCalls == 3 && state.mPhysicalListCalls == 2);
    }
    {
        FakeState state                 = canonicalState();
        state.mPhysicalAlwaysIncomplete = true;
        ScopedFakeState scope(state);
        const auto&     error = requireError(resolveVulkanPhysicalDeviceGeneration(request(state)));
        ensure("physical enumeration stops at its retry limit",
               error.mCode == VulkanPhysicalDeviceResolutionCode::PhysicalDeviceEnumerationRetryLimitExceeded &&
                   error.mResult == VK_INCOMPLETE && error.mEnumerationAttempt == 4 && state.mPhysicalCountCalls == 4);
    }
    {
        FakeState state;
        state.synchronizeCounters();
        ScopedFakeState scope(state);
        const auto&     error = requireError(resolveVulkanPhysicalDeviceGeneration(request(state)));
        ensure("an empty physical-device list is unsuitable",
               error.mCode == VulkanPhysicalDeviceResolutionCode::NoSuitablePhysicalDevice && error.mObservedCount == 0 &&
                   !error.mLastRejection);
    }
}

template<>
template<>
void render_vulkan_physical_device_object::test<4>()
{
    FakeState state;
    state.mDevices.push_back(nullDeviceRecord());
    state.mDevices.push_back(deviceRecord(0x3100, VK_MAKE_API_VERSION(1, 1, 1, 0)));
    state.mDevices.push_back(deviceRecord(0x3200, VK_API_VERSION_1_0));
    state.mDevices.push_back(deviceRecord(0x3300, VK_API_VERSION_1_1));
    state.mDevices.push_back(deviceRecord(0x3400, VK_API_VERSION_1_2));
    state.synchronizeCounters();
    ScopedFakeState scope(state);

    VulkanPhysicalDeviceGeneration generation = takeGeneration(resolveVulkanPhysicalDeviceGeneration(request(state)));
    ensure("selection skips invalid APIs and takes the first eligible physical device",
           generation.physicalDevice() == state.mDevices[3].mHandle && generation.physicalDeviceIndex() == 3);
    ensure("the generation binds the exact resolver, instance, and surface",
           generation.getInstanceProcAddr() == fakeGetInstanceProcAddr && generation.instance() == state.mInstance &&
               generation.surface() == state.mSurface && generation.selectedFor(state.mInstance, state.mSurface) &&
               !generation.selectedFor(state.mInstance, fakeHandle<VkSurfaceKHR>(0x5eed)));
    ensure("the exact selected properties and API version are retained",
           generation.apiVersion() == VK_API_VERSION_1_1 && std::string_view(generation.properties().deviceName) == "fake-3300");
    ensure("selection stops before querying the later eligible device",
           state.mPropertyCalls[4] == 0 && state.mExtensionCountCallsByDevice[4] == 0 && state.mQueueCountCalls[4] == 0);

    VulkanPhysicalDeviceGeneration moved(std::move(generation));
    ensure("move construction transfers the selected generation",
           moved.physicalDevice() == state.mDevices[3].mHandle && generation.physicalDevice() == VK_NULL_HANDLE &&
               generation.instance() == VK_NULL_HANDLE && generation.surface() == VK_NULL_HANDLE);
}

template<>
template<>
void render_vulkan_physical_device_object::test<5>()
{
    {
        FakeState state             = canonicalState();
        state.mExtensionCountResult = VK_ERROR_OUT_OF_HOST_MEMORY;
        ScopedFakeState scope(state);
        const auto&     error = requireError(resolveVulkanPhysicalDeviceGeneration(request(state)));
        ensure("extension count failure preserves device, attempt, and VkResult",
               error.mCode == VulkanPhysicalDeviceResolutionCode::DeviceExtensionEnumerationFailure &&
                   error.mCommand == VulkanPhysicalDeviceCommand::EnumerateDeviceExtensionProperties && error.mPhysicalDeviceIndex == 0 &&
                   error.mEnumerationAttempt == 1 && error.mResult == VK_ERROR_OUT_OF_HOST_MEMORY);
    }
    {
        FakeState state            = canonicalState();
        state.mExtensionListResult = VK_ERROR_DEVICE_LOST;
        ScopedFakeState scope(state);
        const auto&     error = requireError(resolveVulkanPhysicalDeviceGeneration(request(state)));
        ensure("extension list failure preserves its VkResult",
               error.mCode == VulkanPhysicalDeviceResolutionCode::DeviceExtensionEnumerationFailure &&
                   error.mResult == VK_ERROR_DEVICE_LOST);
    }
    {
        FakeState state               = canonicalState();
        state.mExtensionCountOverride = VULKAN_PRESENTATION_MAX_DEVICE_EXTENSIONS + 1;
        ScopedFakeState scope(state);
        const auto&     error = requireError(resolveVulkanPhysicalDeviceGeneration(request(state)));
        ensure("extension enumeration is bounded before allocation",
               error.mCode == VulkanPhysicalDeviceResolutionCode::DeviceExtensionCountExceeded &&
                   error.mObservedCount == VULKAN_PRESENTATION_MAX_DEVICE_EXTENSIONS + 1);
    }
    {
        FakeState state                 = canonicalState();
        state.mExtensionWrittenOverride = 2;
        ScopedFakeState scope(state);
        const auto&     error = requireError(resolveVulkanPhysicalDeviceGeneration(request(state)));
        ensure("extension enumeration rejects output above capacity",
               error.mCode == VulkanPhysicalDeviceResolutionCode::InvalidDeviceExtensionEnumerationOutput && error.mObservedCount == 2);
    }
    for (MalformedExtension malformed : { MalformedExtension::Empty, MalformedExtension::Unterminated })
    {
        FakeState state           = canonicalState();
        state.mMalformedExtension = malformed;
        ScopedFakeState scope(state);
        const auto&     error = requireError(resolveVulkanPhysicalDeviceGeneration(request(state)));
        ensure("malformed extension names retain the exact property index",
               error.mCode == VulkanPhysicalDeviceResolutionCode::MalformedDeviceExtensionProperty && error.mPropertyIndex == 0 &&
                   error.mPhysicalDeviceIndex == 0);
    }
    {
        FakeState state                      = canonicalState();
        state.mExtensionIncompleteCountCalls = 1;
        state.mExtensionIncompleteListCalls  = 1;
        ScopedFakeState                scope(state);
        VulkanPhysicalDeviceGeneration generation = takeGeneration(resolveVulkanPhysicalDeviceGeneration(request(state)));
        ensure("extension retries restart their complete count and list transaction",
               generation.physicalDevice() == state.mDevices[0].mHandle && state.mExtensionCountCallsByDevice[0] == 3 &&
                   state.mExtensionListCallsByDevice[0] == 2);
    }
    {
        FakeState state                  = canonicalState();
        state.mExtensionAlwaysIncomplete = true;
        ScopedFakeState scope(state);
        const auto&     error = requireError(resolveVulkanPhysicalDeviceGeneration(request(state)));
        ensure("extension enumeration stops at its retry limit",
               error.mCode == VulkanPhysicalDeviceResolutionCode::DeviceExtensionEnumerationRetryLimitExceeded &&
                   error.mResult == VK_INCOMPLETE && error.mEnumerationAttempt == 4);
    }
    {
        FakeState state;
        state.mDevices.push_back(deviceRecord(0x3500, VK_API_VERSION_1_1, { "VK_KHR_swapchain_extra", "xVK_KHR_swapchain" }));
        state.synchronizeCounters();
        ScopedFakeState scope(state);
        const auto&     error = requireError(resolveVulkanPhysicalDeviceGeneration(request(state)));
        ensure("only the exact swapchain extension admits a candidate",
               error.mCode == VulkanPhysicalDeviceResolutionCode::NoSuitablePhysicalDevice &&
                   error.mLastRejection == VulkanPhysicalDeviceRejection::MissingSwapchainExtension);
    }
}

template<>
template<>
void render_vulkan_physical_device_object::test<6>()
{
    {
        FakeState state;
        state.mDevices.push_back(deviceRecord(0x3600, VK_API_VERSION_1_1, { VK_KHR_SWAPCHAIN_EXTENSION_NAME }, {}, {}));
        state.mDevices.push_back(deviceRecord(0x3700, VK_API_VERSION_1_1, { VK_KHR_SWAPCHAIN_EXTENSION_NAME },
                                              { queueFamily(VK_QUEUE_COMPUTE_BIT), queueFamily(VK_QUEUE_GRAPHICS_BIT),
                                                queueFamily(VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_TRANSFER_BIT) },
                                              { VK_TRUE, VK_FALSE, VK_TRUE }));
        state.synchronizeCounters();
        ScopedFakeState scope(state);

        VulkanPhysicalDeviceGeneration generation = takeGeneration(resolveVulkanPhysicalDeviceGeneration(request(state)));
        ensure("selection requires one queue family that is both graphics and present capable",
               generation.physicalDevice() == state.mDevices[1].mHandle && generation.queueFamilyIndex() == 2 &&
                   generation.queueFamilyProperties().queueCount == 1 &&
                   (generation.queueFamilyProperties().queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_TRANSFER_BIT)) ==
                       (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_TRANSFER_BIT));
        ensure("presentation support is queried only for nonempty graphics families against the exact surface",
               state.mSurfaceSupportCalls == 2 && state.mLastSurfaceDevice == state.mDevices[1].mHandle && state.mLastSurfaceQueue == 2 &&
                   state.mLastSurface == state.mSurface);
    }
    {
        FakeState state             = canonicalState();
        state.mSurfaceSupportResult = VK_ERROR_SURFACE_LOST_KHR;
        ScopedFakeState scope(state);
        const auto&     error = requireError(resolveVulkanPhysicalDeviceGeneration(request(state)));
        ensure("surface-support failure preserves its exact candidate, family, and VkResult",
               error.mCode == VulkanPhysicalDeviceResolutionCode::SurfaceSupportQueryFailure && error.mPhysicalDeviceIndex == 0 &&
                   error.mQueueFamilyIndex == 0 && error.mResult == VK_ERROR_SURFACE_LOST_KHR);
    }
    {
        FakeState state           = canonicalState();
        state.mQueueCountOverride = VULKAN_PRESENTATION_MAX_QUEUE_FAMILIES + 1;
        ScopedFakeState scope(state);
        const auto&     error = requireError(resolveVulkanPhysicalDeviceGeneration(request(state)));
        ensure("queue-family enumeration is bounded before allocation",
               error.mCode == VulkanPhysicalDeviceResolutionCode::QueueFamilyCountExceeded &&
                   error.mObservedCount == VULKAN_PRESENTATION_MAX_QUEUE_FAMILIES + 1);
    }
    {
        FakeState state             = canonicalState();
        state.mQueueWrittenOverride = 2;
        ScopedFakeState scope(state);
        const auto&     error = requireError(resolveVulkanPhysicalDeviceGeneration(request(state)));
        ensure("queue-family enumeration rejects output above capacity",
               error.mCode == VulkanPhysicalDeviceResolutionCode::InvalidQueueFamilyEnumerationOutput && error.mObservedCount == 2);
    }
}

template<>
template<>
void render_vulkan_physical_device_object::test<7>()
{
    FakeState state;
    state.mDevices.push_back(deviceRecord(
        0x3800, VK_API_VERSION_1_2, { "VK_KHR_portability_subset_extra", VK_KHR_SWAPCHAIN_EXTENSION_NAME, "VK_KHR_portability_subset" },
        { queueFamily(VK_QUEUE_GRAPHICS_BIT) }, { VK_TRUE }));
    state.mDevices.push_back(deviceRecord(0x3900, VK_API_VERSION_1_3));
    state.synchronizeCounters();
    ScopedFakeState scope(state);

    VulkanPhysicalDeviceGeneration generation = takeGeneration(resolveVulkanPhysicalDeviceGeneration(request(state)));
    const auto                     required   = generation.requiredDeviceExtensions();
    ensure("the first eligible device is selected without scoring later candidates",
           generation.physicalDeviceIndex() == 0 && state.mPropertyCalls[1] == 0);
    ensure("the exact portability-subset advertisement becomes a device-create obligation",
           generation.portabilitySubsetAdvertised() && generation.portabilitySubsetRequired() && required.size() == 2 &&
               required[0] == VK_KHR_SWAPCHAIN_EXTENSION_NAME && required[1] == "VK_KHR_portability_subset");

    FakeState core_state;
    core_state.mDevices.push_back(
        deviceRecord(0x3a00, VK_API_VERSION_1_1, { VK_KHR_SWAPCHAIN_EXTENSION_NAME, "VK_KHR_portability_subset_extra" }));
    core_state.synchronizeCounters();
    ScopedFakeState                core_scope(core_state);
    VulkanPhysicalDeviceGeneration core_generation = takeGeneration(resolveVulkanPhysicalDeviceGeneration(request(core_state)));
    const auto                     core_required   = core_generation.requiredDeviceExtensions();
    ensure("a portability decoy adds no device-create obligation",
           !core_generation.portabilitySubsetAdvertised() && !core_generation.portabilitySubsetRequired() && core_required.size() == 1 &&
               core_required[0] == VK_KHR_SWAPCHAIN_EXTENSION_NAME);
}

template<>
template<>
void render_vulkan_physical_device_object::test<8>()
{
    FakeState state;
    state.mDevices.push_back(deviceRecord(0x3b00, VK_API_VERSION_1_0));
    state.mDevices.push_back(deviceRecord(0x3c00, VK_API_VERSION_1_1, {}));
    state.mDevices.push_back(deviceRecord(0x3d00, VK_API_VERSION_1_1, { VK_KHR_SWAPCHAIN_EXTENSION_NAME },
                                          { queueFamily(VK_QUEUE_GRAPHICS_BIT, 0), queueFamily(VK_QUEUE_COMPUTE_BIT) },
                                          { VK_TRUE, VK_TRUE }));
    state.synchronizeCounters();
    ScopedFakeState scope(state);

    const auto& error = requireError(resolveVulkanPhysicalDeviceGeneration(request(state)));
    ensure("an exhausted candidate list reports its final typed rejection",
           error.mCode == VulkanPhysicalDeviceResolutionCode::NoSuitablePhysicalDevice &&
               error.mLastRejection == VulkanPhysicalDeviceRejection::MissingUnifiedGraphicsPresentQueueFamily &&
               error.mPhysicalDeviceIndex == 2 && error.mObservedCount == 3 && error.mEnumerationAttempt == 1);
    ensure("candidate filtering avoids irrelevant extension and queue queries",
           state.mExtensionCountCallsByDevice[0] == 0 && state.mQueueCountCalls[0] == 0 && state.mQueueCountCalls[1] == 0 &&
               state.mSurfaceSupportCalls == 0);
}

} // namespace tut
