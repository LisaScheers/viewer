/**
 * @file llrendervulkanlogicaldevice_test.cpp
 * @brief Tests for loader-neutral Vulkan logical-device ownership.
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

#include "llrendervulkanlogicaldevice.h"
#include "lltut.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
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
    GetPhysicalDeviceFeatures,
    CreateDevice,
    DestroyDevice,
    GetDeviceQueue
};

enum class DeviceEvent : std::uint8_t
{
    Create,
    GetQueue,
    Destroy
};

struct FakeState
{
    VkInstance       mInstance          = fakeHandle<VkInstance>(0x1000);
    VkSurfaceKHR     mSurface           = fakeHandle<VkSurfaceKHR>(0x2000);
    VkPhysicalDevice mPhysicalDevice    = fakeHandle<VkPhysicalDevice>(0x3000);
    VkDevice         mDeviceOutput      = fakeHandle<VkDevice>(0x4000);
    VkQueue          mQueueOutput       = fakeHandle<VkQueue>(0x5000);
    std::uint32_t    mQueueFamily       = 3;
    bool             mPortabilitySubset = false;

    MissingCommand           mMissingCommand   = MissingCommand::None;
    bool                     mIndependentBlend = true;
    VkResult                 mCreateResult     = VK_SUCCESS;
    std::vector<std::string> mLogicalCommandLookups;
    std::vector<DeviceEvent> mDeviceEvents;

    std::size_t      mFeatureCalls              = 0;
    VkPhysicalDevice mFeatureDevice             = VK_NULL_HANDLE;
    bool             mAllResolvedBeforeFeatures = false;

    std::size_t                mCreateCalls             = 0;
    VkPhysicalDevice           mCreatePhysicalDevice    = VK_NULL_HANDLE;
    bool                       mAllResolvedBeforeCreate = false;
    bool                       mCreateInfoExact         = false;
    std::array<std::string, 2> mCreatedExtensions{};
    std::size_t                mCreatedExtensionCount = 0;

    std::size_t   mQueueCalls       = 0;
    VkDevice      mQueueDevice      = VK_NULL_HANDLE;
    std::uint32_t mQueueFamilyInput = VK_QUEUE_FAMILY_IGNORED;
    std::uint32_t mQueueIndexInput  = ~std::uint32_t{ 0 };

    std::size_t mDestroyCalls         = 0;
    VkDevice    mDestroyedDevice      = VK_NULL_HANDLE;
    bool        mDestroyAllocatorNull = false;
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
        *count = 1;
        return VK_SUCCESS;
    }
    if (*count == 0)
    {
        return VK_INCOMPLETE;
    }
    devices[0] = gFakeState->mPhysicalDevice;
    *count     = 1;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeGetPhysicalDeviceProperties(VkPhysicalDevice            physical_device,
                                                           VkPhysicalDeviceProperties* properties) noexcept
{
    if (!gFakeState || physical_device != gFakeState->mPhysicalDevice || !properties)
    {
        return;
    }
    *properties            = {};
    properties->apiVersion = VK_API_VERSION_1_1;
    properties->deviceType = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
    std::strncpy(properties->deviceName, "logical-device-fake", VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 1);
}

VKAPI_ATTR void VKAPI_CALL fakeGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice         physical_device,
                                                                      std::uint32_t*           count,
                                                                      VkQueueFamilyProperties* properties) noexcept
{
    if (!gFakeState || physical_device != gFakeState->mPhysicalDevice || !count)
    {
        return;
    }
    const std::uint32_t required_count = gFakeState->mQueueFamily + 1;
    if (!properties)
    {
        *count = required_count;
        return;
    }
    const std::uint32_t capacity = *count;
    const std::uint32_t written  = std::min(capacity, required_count);
    for (std::uint32_t index = 0; index < written; ++index)
    {
        properties[index]            = {};
        properties[index].queueCount = 1;
        properties[index].queueFlags = index == gFakeState->mQueueFamily ? VK_QUEUE_GRAPHICS_BIT : VK_QUEUE_COMPUTE_BIT;
    }
    *count = written;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeGetPhysicalDeviceSurfaceSupport(VkPhysicalDevice physical_device,
                                                                   std::uint32_t    queue_family,
                                                                   VkSurfaceKHR     surface,
                                                                   VkBool32*        supported) noexcept
{
    if (!gFakeState || physical_device != gFakeState->mPhysicalDevice || surface != gFakeState->mSurface || !supported)
    {
        return VK_ERROR_SURFACE_LOST_KHR;
    }
    *supported = queue_family == gFakeState->mQueueFamily ? VK_TRUE : VK_FALSE;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeEnumerateDeviceExtensionProperties(VkPhysicalDevice       physical_device,
                                                                      const char*            layer_name,
                                                                      std::uint32_t*         count,
                                                                      VkExtensionProperties* properties) noexcept
{
    if (!gFakeState || physical_device != gFakeState->mPhysicalDevice || layer_name || !count)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    const std::uint32_t required_count = gFakeState->mPortabilitySubset ? 2 : 1;
    if (!properties)
    {
        *count = required_count;
        return VK_SUCCESS;
    }
    if (*count < required_count)
    {
        return VK_INCOMPLETE;
    }
    properties[0] = {};
    std::strncpy(properties[0].extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_MAX_EXTENSION_NAME_SIZE - 1);
    if (gFakeState->mPortabilitySubset)
    {
        properties[1] = {};
        std::strncpy(properties[1].extensionName, "VK_KHR_portability_subset", VK_MAX_EXTENSION_NAME_SIZE - 1);
    }
    *count = required_count;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeGetPhysicalDeviceFeatures(VkPhysicalDevice physical_device, VkPhysicalDeviceFeatures* features) noexcept
{
    if (!gFakeState || !features)
    {
        return;
    }
    ++gFakeState->mFeatureCalls;
    gFakeState->mFeatureDevice             = physical_device;
    gFakeState->mAllResolvedBeforeFeatures = gFakeState->mLogicalCommandLookups.size() == 4;
    *features                  = { VK_TRUE, VK_TRUE, VK_TRUE, VK_TRUE, VK_TRUE, VK_TRUE, VK_TRUE, VK_TRUE, VK_TRUE, VK_TRUE, VK_TRUE,
                                   VK_TRUE, VK_TRUE, VK_TRUE, VK_TRUE, VK_TRUE, VK_TRUE, VK_TRUE, VK_TRUE, VK_TRUE, VK_TRUE, VK_TRUE,
                                   VK_TRUE, VK_TRUE, VK_TRUE, VK_TRUE, VK_TRUE, VK_TRUE, VK_TRUE, VK_TRUE, VK_TRUE, VK_TRUE, VK_TRUE,
                                   VK_TRUE, VK_TRUE, VK_TRUE, VK_TRUE, VK_TRUE, VK_TRUE, VK_TRUE, VK_TRUE, VK_TRUE, VK_TRUE, VK_TRUE,
                                   VK_TRUE, VK_TRUE, VK_TRUE, VK_TRUE, VK_TRUE, VK_TRUE, VK_TRUE, VK_TRUE, VK_TRUE, VK_TRUE, VK_TRUE };
    features->independentBlend = gFakeState->mIndependentBlend ? VK_TRUE : VK_FALSE;
}

bool exactEnabledFeatures(const VkPhysicalDeviceFeatures* features) noexcept
{
    if (!features)
    {
        return false;
    }
    VkPhysicalDeviceFeatures expected{};
    expected.independentBlend = VK_TRUE;
    return std::memcmp(features, &expected, sizeof(expected)) == 0;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateDevice(VkPhysicalDevice             physical_device,
                                                const VkDeviceCreateInfo*    create_info,
                                                const VkAllocationCallbacks* allocator,
                                                VkDevice*                    device) noexcept
{
    if (!gFakeState || !device)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    ++gFakeState->mCreateCalls;
    gFakeState->mDeviceEvents.push_back(DeviceEvent::Create);
    gFakeState->mCreatePhysicalDevice    = physical_device;
    gFakeState->mAllResolvedBeforeCreate = gFakeState->mLogicalCommandLookups.size() == 4;
    gFakeState->mCreatedExtensionCount   = 0;

    bool exact = physical_device == gFakeState->mPhysicalDevice && allocator == nullptr && create_info &&
                 create_info->sType == VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO && create_info->pNext == nullptr && create_info->flags == 0 &&
                 create_info->queueCreateInfoCount == 1 && create_info->pQueueCreateInfos && create_info->enabledLayerCount == 0 &&
                 create_info->ppEnabledLayerNames == nullptr &&
                 create_info->enabledExtensionCount == (gFakeState->mPortabilitySubset ? 2u : 1u) && create_info->ppEnabledExtensionNames &&
                 exactEnabledFeatures(create_info->pEnabledFeatures);
    if (exact)
    {
        const VkDeviceQueueCreateInfo& queue_info = create_info->pQueueCreateInfos[0];
        exact = queue_info.sType == VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO && queue_info.pNext == nullptr && queue_info.flags == 0 &&
                queue_info.queueFamilyIndex == gFakeState->mQueueFamily && queue_info.queueCount == 1 && queue_info.pQueuePriorities &&
                queue_info.pQueuePriorities[0] == 1.0f;
    }

    if (create_info && create_info->ppEnabledExtensionNames)
    {
        gFakeState->mCreatedExtensionCount = std::min<std::size_t>(create_info->enabledExtensionCount, 2);
        for (std::size_t index = 0; index < gFakeState->mCreatedExtensionCount; ++index)
        {
            const char* name = create_info->ppEnabledExtensionNames[index];
            if (!name)
            {
                exact = false;
                break;
            }
            gFakeState->mCreatedExtensions[index] = name;
        }
        exact = exact && gFakeState->mCreatedExtensions[0] == VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        if (gFakeState->mPortabilitySubset)
        {
            exact = exact && gFakeState->mCreatedExtensions[1] == "VK_KHR_portability_subset";
        }
    }
    gFakeState->mCreateInfoExact = exact;
    *device                      = gFakeState->mDeviceOutput;
    return gFakeState->mCreateResult;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyDevice(VkDevice device, const VkAllocationCallbacks* allocator) noexcept
{
    if (!gFakeState)
    {
        return;
    }
    ++gFakeState->mDestroyCalls;
    gFakeState->mDeviceEvents.push_back(DeviceEvent::Destroy);
    gFakeState->mDestroyedDevice      = device;
    gFakeState->mDestroyAllocatorNull = allocator == nullptr;
}

VKAPI_ATTR void VKAPI_CALL fakeGetDeviceQueue(VkDevice      device,
                                              std::uint32_t queue_family,
                                              std::uint32_t queue_index,
                                              VkQueue*      queue) noexcept
{
    if (!gFakeState || !queue)
    {
        return;
    }
    ++gFakeState->mQueueCalls;
    gFakeState->mDeviceEvents.push_back(DeviceEvent::GetQueue);
    gFakeState->mQueueDevice      = device;
    gFakeState->mQueueFamilyInput = queue_family;
    gFakeState->mQueueIndexInput  = queue_index;
    *queue                        = gFakeState->mQueueOutput;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL fakeGetInstanceProcAddr(VkInstance instance, const char* name) noexcept
{
    if (!gFakeState || instance != gFakeState->mInstance || !name)
    {
        return nullptr;
    }
    if (std::strcmp(name, "vkEnumeratePhysicalDevices") == 0)
    {
        return eraseFunctionType(fakeEnumeratePhysicalDevices);
    }
    if (std::strcmp(name, "vkGetPhysicalDeviceProperties") == 0)
    {
        return eraseFunctionType(fakeGetPhysicalDeviceProperties);
    }
    if (std::strcmp(name, "vkGetPhysicalDeviceQueueFamilyProperties") == 0)
    {
        return eraseFunctionType(fakeGetPhysicalDeviceQueueFamilyProperties);
    }
    if (std::strcmp(name, "vkGetPhysicalDeviceSurfaceSupportKHR") == 0)
    {
        return eraseFunctionType(fakeGetPhysicalDeviceSurfaceSupport);
    }
    if (std::strcmp(name, "vkEnumerateDeviceExtensionProperties") == 0)
    {
        return eraseFunctionType(fakeEnumerateDeviceExtensionProperties);
    }

    gFakeState->mLogicalCommandLookups.emplace_back(name);
    if (std::strcmp(name, "vkGetPhysicalDeviceFeatures") == 0)
    {
        return gFakeState->mMissingCommand == MissingCommand::GetPhysicalDeviceFeatures ? nullptr
                                                                                        : eraseFunctionType(fakeGetPhysicalDeviceFeatures);
    }
    if (std::strcmp(name, "vkCreateDevice") == 0)
    {
        return gFakeState->mMissingCommand == MissingCommand::CreateDevice ? nullptr : eraseFunctionType(fakeCreateDevice);
    }
    if (std::strcmp(name, "vkDestroyDevice") == 0)
    {
        return gFakeState->mMissingCommand == MissingCommand::DestroyDevice ? nullptr : eraseFunctionType(fakeDestroyDevice);
    }
    if (std::strcmp(name, "vkGetDeviceQueue") == 0)
    {
        return gFakeState->mMissingCommand == MissingCommand::GetDeviceQueue ? nullptr : eraseFunctionType(fakeGetDeviceQueue);
    }
    return nullptr;
}

VulkanPhysicalDeviceGeneration selectPhysicalDevice(FakeState& state)
{
    const VulkanPhysicalDeviceRequest    request{ fakeGetInstanceProcAddr, state.mInstance, state.mSurface };
    VulkanPhysicalDeviceResolutionResult result = resolveVulkanPhysicalDeviceGeneration(request);
    tut::ensure("the physical-device fixture resolves", std::holds_alternative<VulkanPhysicalDeviceGeneration>(result));
    state.mLogicalCommandLookups.clear();
    return std::get<VulkanPhysicalDeviceGeneration>(std::move(result));
}

const VulkanLogicalDeviceResolutionError& requireError(const VulkanLogicalDeviceResolutionResult& result)
{
    const auto* error = std::get_if<VulkanLogicalDeviceResolutionError>(&result);
    tut::ensure("logical-device resolution returns an error", error != nullptr);
    return *error;
}

void ensureCode(const VulkanLogicalDeviceResolutionResult& result, VulkanLogicalDeviceResolutionCode code)
{
    tut::ensure("the exact logical-device error is reported", requireError(result).mCode == code);
}

VulkanLogicalDeviceGeneration takeGeneration(VulkanLogicalDeviceResolutionResult&& result)
{
    tut::ensure("logical-device resolution returns a generation", std::holds_alternative<VulkanLogicalDeviceGeneration>(result));
    return std::get<VulkanLogicalDeviceGeneration>(std::move(result));
}

void ensureExactCommandOrder(const FakeState& state)
{
    constexpr std::array expected{ std::string_view("vkGetPhysicalDeviceFeatures"), std::string_view("vkCreateDevice"),
                                   std::string_view("vkDestroyDevice"), std::string_view("vkGetDeviceQueue") };
    tut::ensure("all four logical-device commands are resolved", state.mLogicalCommandLookups.size() == expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        tut::ensure("logical-device commands are resolved in exact order", state.mLogicalCommandLookups[index] == expected[index]);
    }
}

} // namespace

namespace tut
{

struct render_vulkan_logical_device_test
{
};

using render_vulkan_logical_device_group  = test_group<render_vulkan_logical_device_test>;
using render_vulkan_logical_device_object = render_vulkan_logical_device_group::object;
render_vulkan_logical_device_group render_vulkan_logical_device_tests("render Vulkan logical device");

template<>
template<>
void render_vulkan_logical_device_object::test<1>()
{
    static_assert(!std::is_default_constructible_v<VulkanLogicalDeviceGeneration>);
    static_assert(!std::is_copy_constructible_v<VulkanLogicalDeviceGeneration>);
    static_assert(!std::is_copy_assignable_v<VulkanLogicalDeviceGeneration>);
    static_assert(std::is_nothrow_move_constructible_v<VulkanLogicalDeviceGeneration>);
    static_assert(!std::is_move_assignable_v<VulkanLogicalDeviceGeneration>);
    static_assert(std::is_nothrow_destructible_v<VulkanLogicalDeviceGeneration>);
    static_assert(std::variant_size_v<VulkanLogicalDeviceResolutionResult> == 2);
    static_assert(noexcept(resolveVulkanLogicalDeviceGeneration(std::declval<const VulkanPhysicalDeviceGeneration&>())));

    const VulkanLogicalDeviceResolutionError value{ VulkanLogicalDeviceResolutionCode::DeviceCreationFailure,
                                                    VulkanLogicalDeviceCommand::CreateDevice, VK_ERROR_DEVICE_LOST };
    ensure("identical typed errors compare equal", value == value);

    FakeState       state;
    ScopedFakeState scope(state);
    auto            moved_from = selectPhysicalDevice(state);
    auto            live       = std::move(moved_from);

    ensureCode(resolveVulkanLogicalDeviceGeneration(moved_from), VulkanLogicalDeviceResolutionCode::InvalidPhysicalDeviceGeneration);
    ensure("invalid provenance resolves no logical-device command", state.mLogicalCommandLookups.empty());
    ensure("moving the parent selection preserves the live provenance", live.physicalDevice() == state.mPhysicalDevice);
}

template<>
template<>
void render_vulkan_logical_device_object::test<2>()
{
    constexpr std::array missing_commands{ std::pair{ MissingCommand::GetPhysicalDeviceFeatures,
                                                      VulkanLogicalDeviceCommand::GetPhysicalDeviceFeatures },
                                           std::pair{ MissingCommand::CreateDevice, VulkanLogicalDeviceCommand::CreateDevice },
                                           std::pair{ MissingCommand::DestroyDevice, VulkanLogicalDeviceCommand::DestroyDevice },
                                           std::pair{ MissingCommand::GetDeviceQueue, VulkanLogicalDeviceCommand::GetDeviceQueue } };

    for (std::size_t index = 0; index < missing_commands.size(); ++index)
    {
        FakeState       state;
        ScopedFakeState scope(state);
        auto            selection = selectPhysicalDevice(state);
        state.mMissingCommand     = missing_commands[index].first;

        const VulkanLogicalDeviceResolutionResult result = resolveVulkanLogicalDeviceGeneration(selection);
        const VulkanLogicalDeviceResolutionError& error  = requireError(result);
        ensure("a missing command reports the required-command code",
               error.mCode == VulkanLogicalDeviceResolutionCode::MissingRequiredCommand);
        ensure("a missing command preserves exact command identity", error.mCommand == missing_commands[index].second);
        ensure("resolution stops at the missing command", state.mLogicalCommandLookups.size() == index + 1);
        ensure("no Vulkan object is touched when dispatch is incomplete",
               state.mFeatureCalls == 0 && state.mCreateCalls == 0 && state.mDestroyCalls == 0);
    }
}

template<>
template<>
void render_vulkan_logical_device_object::test<3>()
{
    FakeState       state;
    ScopedFakeState scope(state);
    auto            selection = selectPhysicalDevice(state);
    state.mIndependentBlend   = false;

    const VulkanLogicalDeviceResolutionResult result = resolveVulkanLogicalDeviceGeneration(selection);
    const VulkanLogicalDeviceResolutionError& error  = requireError(result);
    ensure("unsupported independentBlend is a typed failure",
           error.mCode == VulkanLogicalDeviceResolutionCode::IndependentBlendUnsupported &&
               error.mCommand == VulkanLogicalDeviceCommand::GetPhysicalDeviceFeatures);
    ensureExactCommandOrder(state);
    ensure("feature support is queried only after complete dispatch resolution",
           state.mFeatureCalls == 1 && state.mFeatureDevice == state.mPhysicalDevice && state.mAllResolvedBeforeFeatures);
    ensure("unsupported independentBlend creates nothing", state.mCreateCalls == 0 && state.mDestroyCalls == 0);
}

template<>
template<>
void render_vulkan_logical_device_object::test<4>()
{
    FakeState       state;
    ScopedFakeState scope(state);
    auto            selection  = selectPhysicalDevice(state);
    auto            generation = takeGeneration(resolveVulkanLogicalDeviceGeneration(selection));

    ensureExactCommandOrder(state);
    ensure("all commands are resolved before feature query and creation",
           state.mAllResolvedBeforeFeatures && state.mAllResolvedBeforeCreate);
    ensure("the device create transaction is exact",
           state.mCreateCalls == 1 && state.mCreatePhysicalDevice == state.mPhysicalDevice && state.mCreateInfoExact);
    ensure("only swapchain is enabled when portability subset was absent",
           state.mCreatedExtensionCount == 1 && state.mCreatedExtensions[0] == VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    ensure("queue zero is retrieved from the selected family",
           state.mQueueCalls == 1 && state.mQueueDevice == state.mDeviceOutput && state.mQueueFamilyInput == state.mQueueFamily &&
               state.mQueueIndexInput == 0);
    ensure("the generation retains exact parent and child identity",
           generation.getInstanceProcAddr() == fakeGetInstanceProcAddr && generation.instance() == state.mInstance &&
               generation.surface() == state.mSurface && generation.physicalDevice() == state.mPhysicalDevice &&
               generation.device() == state.mDeviceOutput && generation.queue() == state.mQueueOutput &&
               generation.queueFamilyIndex() == state.mQueueFamily && generation.queueIndex() == 0 && generation.createdFor(selection));
    ensure("only independentBlend is retained as enabled", generation.independentBlendEnabled());
    VkPhysicalDeviceFeatures expected_features{};
    expected_features.independentBlend = VK_TRUE;
    ensure("the retained feature structure contains no extra feature",
           std::memcmp(&generation.enabledFeatures(), &expected_features, sizeof(expected_features)) == 0);
    ensure("the retained extension policy has fixed owned identity",
           generation.enabledDeviceExtensions().size() == 1 && generation.enabledDeviceExtensions()[0] == VK_KHR_SWAPCHAIN_EXTENSION_NAME &&
               !generation.portabilitySubsetEnabled());

    generation.reset();
    ensure("reset destroys the exact device with null callbacks and clears borrowed queue state",
           state.mDestroyCalls == 1 && state.mDestroyedDevice == state.mDeviceOutput && state.mDestroyAllocatorNull &&
               generation.device() == VK_NULL_HANDLE && generation.queue() == VK_NULL_HANDLE && !generation.createdFor(selection));
    ensure("successful ownership retrieves its queue before later destruction",
           state.mDeviceEvents == std::vector<DeviceEvent>{ DeviceEvent::Create, DeviceEvent::GetQueue, DeviceEvent::Destroy });
    generation.reset();
    ensure("reset is idempotent", state.mDestroyCalls == 1);
}

template<>
template<>
void render_vulkan_logical_device_object::test<5>()
{
    FakeState       state;
    ScopedFakeState scope(state);
    state.mPortabilitySubset = true;
    auto selection           = selectPhysicalDevice(state);
    auto generation          = takeGeneration(resolveVulkanLogicalDeviceGeneration(selection));
    auto moved               = std::move(generation);

    ensure("portability subset follows swapchain in exact create order",
           state.mCreateInfoExact && state.mCreatedExtensionCount == 2 && state.mCreatedExtensions[0] == VK_KHR_SWAPCHAIN_EXTENSION_NAME &&
               state.mCreatedExtensions[1] == "VK_KHR_portability_subset");
    ensure("the moved generation retains the exact two-extension policy",
           moved.portabilitySubsetEnabled() && moved.enabledDeviceExtensions().size() == 2 &&
               moved.enabledDeviceExtensions()[0] == VK_KHR_SWAPCHAIN_EXTENSION_NAME &&
               moved.enabledDeviceExtensions()[1] == "VK_KHR_portability_subset" && moved.createdFor(selection));
    ensure("move disarms the source owner", generation.device() == VK_NULL_HANDLE && generation.queue() == VK_NULL_HANDLE);

    moved.reset();
    ensure("the moved owner destroys its device exactly once", state.mDestroyCalls == 1 && state.mDestroyedDevice == state.mDeviceOutput);
}

template<>
template<>
void render_vulkan_logical_device_object::test<6>()
{
    FakeState       state;
    ScopedFakeState scope(state);
    auto            selection = selectPhysicalDevice(state);
    state.mCreateResult       = VK_ERROR_FEATURE_NOT_PRESENT;
    state.mDeviceOutput       = VK_NULL_HANDLE;

    const VulkanLogicalDeviceResolutionResult result = resolveVulkanLogicalDeviceGeneration(selection);
    const VulkanLogicalDeviceResolutionError& error  = requireError(result);
    ensure("creation failure preserves the exact VkResult and command",
           error.mCode == VulkanLogicalDeviceResolutionCode::DeviceCreationFailure &&
               error.mCommand == VulkanLogicalDeviceCommand::CreateDevice && error.mResult == VK_ERROR_FEATURE_NOT_PRESENT);
    ensure("ordinary failed creation retrieves no queue and owns nothing", state.mQueueCalls == 0 && state.mDestroyCalls == 0);
}

template<>
template<>
void render_vulkan_logical_device_object::test<7>()
{
    FakeState       state;
    ScopedFakeState scope(state);
    auto            selection = selectPhysicalDevice(state);
    state.mCreateResult       = VK_ERROR_DEVICE_LOST;

    const VulkanLogicalDeviceResolutionResult result = resolveVulkanLogicalDeviceGeneration(selection);
    const VulkanLogicalDeviceResolutionError& error  = requireError(result);
    ensure("poisoned creation preserves its failure",
           error.mCode == VulkanLogicalDeviceResolutionCode::DeviceCreationFailure && error.mResult == VK_ERROR_DEVICE_LOST);
    ensure("an undefined failed output is neither inspected nor destroyed",
           state.mDestroyCalls == 0 && state.mQueueCalls == 0 && state.mDeviceEvents == std::vector<DeviceEvent>{ DeviceEvent::Create });
}

template<>
template<>
void render_vulkan_logical_device_object::test<8>()
{
    FakeState       state;
    ScopedFakeState scope(state);
    auto            selection = selectPhysicalDevice(state);
    state.mDeviceOutput       = VK_NULL_HANDLE;

    const VulkanLogicalDeviceResolutionResult result = resolveVulkanLogicalDeviceGeneration(selection);
    const VulkanLogicalDeviceResolutionError& error  = requireError(result);
    ensure("success with a null device is rejected with exact command identity",
           error.mCode == VulkanLogicalDeviceResolutionCode::NullDeviceOnSuccess &&
               error.mCommand == VulkanLogicalDeviceCommand::CreateDevice && error.mResult == VK_SUCCESS);
    ensure("a null device output is neither queued nor destroyed", state.mQueueCalls == 0 && state.mDestroyCalls == 0);
}

template<>
template<>
void render_vulkan_logical_device_object::test<9>()
{
    FakeState       state;
    ScopedFakeState scope(state);
    auto            selection   = selectPhysicalDevice(state);
    state.mQueueOutput          = VK_NULL_HANDLE;
    const VkDevice first_device = state.mDeviceOutput;

    const VulkanLogicalDeviceResolutionResult failed = resolveVulkanLogicalDeviceGeneration(selection);
    const VulkanLogicalDeviceResolutionError& error  = requireError(failed);
    ensure("a null queue is rejected with exact command identity",
           error.mCode == VulkanLogicalDeviceResolutionCode::NullQueueOnSuccess &&
               error.mCommand == VulkanLogicalDeviceCommand::GetDeviceQueue);
    ensure("null queue rolls back the pending device",
           state.mQueueCalls == 1 && state.mDestroyCalls == 1 && state.mDestroyedDevice == first_device && state.mDestroyAllocatorNull);
    ensure("null-queue rollback destroys only after the failed retrieval",
           state.mDeviceEvents == std::vector<DeviceEvent>{ DeviceEvent::Create, DeviceEvent::GetQueue, DeviceEvent::Destroy });

    state.mDeviceOutput = fakeHandle<VkDevice>(0x6000);
    state.mQueueOutput  = fakeHandle<VkQueue>(0x7000);
    auto retried        = takeGeneration(resolveVulkanLogicalDeviceGeneration(selection));
    ensure("the unchanged parent selection is reusable after rollback",
           retried.device() == state.mDeviceOutput && retried.queue() == state.mQueueOutput && retried.createdFor(selection));
    retried.reset();
    ensure("retry ownership remains exact", state.mDestroyCalls == 2 && state.mDestroyedDevice == state.mDeviceOutput);
}

} // namespace tut
