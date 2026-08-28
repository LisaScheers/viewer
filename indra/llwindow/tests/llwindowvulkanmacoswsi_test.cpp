/**
 * @file llwindowvulkanmacoswsi_test.cpp
 * @brief Opt-in native smoke for the isolated Cocoa and Metal Vulkan owner.
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

#include "llgl.h"
#include "llrendervulkanglobaldispatch.h"
#include "llrendervulkaninstance.h"
#include "llwindow.h"
#include "llwindowmacosxvulkan.h"
#include "lltut.h"

#include <OpenGL/OpenGL.h>

#include <cstdlib>
#include <string>
#include <string_view>
#include <variant>

namespace
{

constexpr char NATIVE_SMOKE_ENVIRONMENT[] = "LL_RUN_VULKAN_MACOS_WSI_NATIVE";
constexpr char LOADER_PATH_ENVIRONMENT[]  = "LL_VULKAN_MACOS_WSI_LOADER";
constexpr U64  NATIVE_WINDOW_GENERATION   = 35;
constexpr U32  BACKING_WIDTH              = 1280;
constexpr U32  BACKING_HEIGHT             = 720;

bool nativeSmokeRequested()
{
    const char* value = std::getenv(NATIVE_SMOKE_ENVIRONMENT);
    return value && std::string_view(value) == "1";
}

} // namespace

namespace tut
{

struct window_vulkan_macos_wsi_test
{
};

using window_vulkan_macos_wsi_group  = test_group<window_vulkan_macos_wsi_test>;
using window_vulkan_macos_wsi_object = window_vulkan_macos_wsi_group::object;
window_vulkan_macos_wsi_group window_vulkan_macos_wsi_tests("window Vulkan macOS WSI native");

template<>
template<>
void window_vulkan_macos_wsi_object::test<1>()
{
    if (!nativeSmokeRequested())
    {
        ensure("the native Vulkan macOS smoke remains explicitly opt-in", true);
        return;
    }

    const char* loader_path = std::getenv(LOADER_PATH_ENVIRONMENT);
    ensure("the native Vulkan macOS smoke requires an explicit loader path", loader_path && loader_path[0] != '\0');

    const CGLContextObj initial_cgl_context  = CGLGetCurrentContext();
    const bool          initial_gl_manager   = gGLManager.mInited;
    const std::size_t   initial_window_count = LLWindow::instanceCount();

    LLWindowMacOSXVulkanCreateInfo create_info;
    create_info.mLoaderPath    = loader_path;
    create_info.mBackingWidth  = BACKING_WIDTH;
    create_info.mBackingHeight = BACKING_HEIGHT;

    LLWindowMacOSXVulkanAcquireResult result = acquireLLWindowMacOSXVulkan(create_info, NATIVE_WINDOW_GENERATION);
    auto*                             owner  = std::get_if<LLWindowMacOSXVulkan>(&result);
    ensure("the default Cocoa bridge acquires a hidden native owner", owner != nullptr);

    ensure("the native owner retains its hidden Cocoa window", owner->hasNativeWindow());
    ensure("the native owner reports a positive backing scale", owner->backingScale() > 0.0);
    ensure_equals("the native owner reports the exact backing-pixel width", owner->drawableWidth(), BACKING_WIDTH);
    ensure_equals("the native owner reports the exact backing-pixel height", owner->drawableHeight(), BACKING_HEIGHT);
    ensure("the native owner refreshes its private Metal geometry", owner->refreshNativeGeometry());
    ensure_equals("the refreshed Metal layer retains the exact backing-pixel width", owner->drawableWidth(), BACKING_WIDTH);
    ensure_equals("the refreshed Metal layer retains the exact backing-pixel height", owner->drawableHeight(), BACKING_HEIGHT);
    ensure_equals("the isolated owner does not enter LLWindowManager", LLWindow::instanceCount(), initial_window_count);
    ensure("native acquisition creates no current CGL context", CGLGetCurrentContext() == initial_cgl_context);
    ensure_equals("native acquisition leaves the OpenGL manager unchanged", gGLManager.mInited, initial_gl_manager);

    const LLWindowVulkanRequirements* requirements = owner->requirements();
    ensure("the native owner publishes Vulkan instance requirements", owner->hasRequirements() && requirements != nullptr);
    ensure_equals("the native requirements retain the exact nonzero generation", requirements->nativeWindowGeneration(),
                  NATIVE_WINDOW_GENERATION);
    ensure("the native owner accepts only its current nonzero generation",
           owner->isGenerationCurrent(NATIVE_WINDOW_GENERATION) && !owner->isGenerationCurrent(0));

    const auto& required_extensions = requirements->requiredInstanceExtensions();
    ensure_equals("the Metal producer publishes exactly two required extensions", required_extensions.size(), std::size_t{ 2 });
    ensure_equals("the base surface extension remains first", required_extensions[0], std::string("VK_KHR_surface"));
    ensure_equals("the Metal surface extension remains second", required_extensions[1], std::string("VK_EXT_metal_surface"));
    ensure("the requirements retain a non-null loader resolver", requirements->resolver() != nullptr);

    const auto  resolver        = reinterpret_cast<PFN_vkGetInstanceProcAddr>(requirements->resolver());
    auto        dispatch_result = LLRenderVulkan::resolveVulkanGlobalDispatchGeneration(resolver);
    const auto* global_dispatch = std::get_if<LLRenderVulkan::VulkanGlobalDispatchGeneration>(&dispatch_result);
    ensure("the explicit macOS loader satisfies the global-dispatch contract", global_dispatch != nullptr);
    ensure("global dispatch retains the exact requirements resolver", global_dispatch->getInstanceProcAddr() == resolver);
    ensure("the explicit macOS loader supports Vulkan 1.1", global_dispatch->loaderApiVersion() >= VK_API_VERSION_1_1);

    const auto instance_error = owner->acquireInstanceGeneration(LLRenderVulkan::VulkanInstanceValidationMode::Required,
                                                                 LLRenderVulkan::VulkanInstancePortabilityMode::EnableIfAvailable);
    ensure("the current Metal requirements acquire a Vulkan instance", !instance_error.has_value());

    const LLRenderVulkan::VulkanInstanceGeneration* instance_generation = owner->instanceGeneration();
    ensure("the native owner publishes its exact Vulkan instance generation", instance_generation != nullptr);
    ensure("the acquired Vulkan instance handle is non-null", instance_generation->instance() != VK_NULL_HANDLE);
    ensure_equals("the acquired Vulkan instance requests API 1.1", instance_generation->apiVersion(), VK_API_VERSION_1_1);
    ensure_equals("the Vulkan instance retains the exact native generation", instance_generation->nativeWindowGeneration(),
                  NATIVE_WINDOW_GENERATION);
    ensure("the Vulkan instance generation remains current for this owner",
           owner->isGenerationCurrent(instance_generation->nativeWindowGeneration()));
    ensure("the Vulkan instance enables required validation", instance_generation->validationEnabled());
    ensure("the MoltenVK instance enables portability enumeration", instance_generation->portabilityEnumerationEnabled());
    ensure("the MoltenVK instance enables the portability extension",
           instance_generation->isExtensionEnabled("VK_KHR_portability_enumeration"));

    const auto& enabled_extensions = instance_generation->enabledExtensions();
    ensure("the Vulkan instance retains both required window extensions", enabled_extensions.size() >= required_extensions.size());
    ensure_equals("the Vulkan instance keeps the base surface extension first", enabled_extensions[0], required_extensions[0]);
    ensure_equals("the Vulkan instance keeps the Metal surface extension second", enabled_extensions[1], required_extensions[1]);

    const auto surface_error = owner->acquireSurfaceGeneration();
    ensure("the current owner acquires a real Metal Vulkan surface", !surface_error.has_value());
    ensure("the exact instance parent owns one surface generation", instance_generation->hasSurfaceGeneration());
    ensure("the real Metal Vulkan surface handle is non-null", instance_generation->surface() != VK_NULL_HANDLE);
    ensure_equals("the surface retains the exact native generation", instance_generation->surfaceNativeWindowGeneration(),
                  NATIVE_WINDOW_GENERATION);
    ensure("surface acquisition creates no current CGL context", CGLGetCurrentContext() == initial_cgl_context);
    ensure_equals("surface acquisition leaves the OpenGL manager unchanged", gGLManager.mInited, initial_gl_manager);

    const auto presentation_device_error = owner->acquirePresentationDeviceGeneration();
    ensure("the exact Metal surface selects a Vulkan presentation device", !presentation_device_error.has_value());
    ensure("the instance parent owns one presentation-device generation", instance_generation->hasPresentationDeviceGeneration());
    ensure("the selected physical-device handle is non-null", instance_generation->physicalDevice() != VK_NULL_HANDLE);
    const VkPhysicalDeviceProperties physical_properties = instance_generation->physicalDeviceProperties();
    ensure("the selected physical device supports standard Vulkan 1.1 or newer",
           VK_API_VERSION_VARIANT(physical_properties.apiVersion) == 0 && physical_properties.apiVersion >= VK_API_VERSION_1_1);
    const VkQueueFamilyProperties queue_properties = instance_generation->presentationQueueFamilyProperties();
    ensure("the selected queue family is nonempty and graphics-capable",
           instance_generation->presentationQueueFamilyIndex() != VK_QUEUE_FAMILY_IGNORED && queue_properties.queueCount != 0 &&
               (queue_properties.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0);
    const auto device_extensions = instance_generation->requiredDeviceExtensions();
    ensure("MoltenVK requires exact swapchain and portability-subset device extensions",
           instance_generation->portabilitySubsetRequired() && device_extensions.size() == 2 &&
               device_extensions[0] == VK_KHR_SWAPCHAIN_EXTENSION_NAME && device_extensions[1] == "VK_KHR_portability_subset");
    ensure("presentation-device selection creates no current CGL context", CGLGetCurrentContext() == initial_cgl_context);
    ensure_equals("presentation-device selection leaves the OpenGL manager unchanged", gGLManager.mInited, initial_gl_manager);

    ensure("the native smoke explicitly resets the Vulkan surface", owner->resetSurfaceGeneration());
    ensure("explicit reset removes only the surface child",
           !instance_generation->hasSurfaceGeneration() && instance_generation->surface() == VK_NULL_HANDLE);
    ensure("surface reset first removes the presentation-device child",
           !instance_generation->hasPresentationDeviceGeneration() && instance_generation->physicalDevice() == VK_NULL_HANDLE &&
               instance_generation->presentationQueueFamilyIndex() == VK_QUEUE_FAMILY_IGNORED);
    ensure("the exact instance parent remains live after surface reset",
           owner->instanceGeneration() == instance_generation && instance_generation->instance() != VK_NULL_HANDLE);
    ensure("required validation remains live during explicit surface destruction", instance_generation->validationEnabled());
    ensure("the private Cocoa and Metal owner remains live after surface reset", owner->hasNativeWindow());
    ensure("the private Metal geometry remains live after surface reset", owner->refreshNativeGeometry());
    ensure_equals("the live Metal layer retains its backing-pixel width", owner->drawableWidth(), BACKING_WIDTH);
    ensure_equals("the live Metal layer retains its backing-pixel height", owner->drawableHeight(), BACKING_HEIGHT);
    ensure("the exact requirements generation remains live after surface reset",
           owner->requirements() == requirements && owner->isGenerationCurrent(NATIVE_WINDOW_GENERATION));

    auto        dispatch_after_surface_reset = LLRenderVulkan::resolveVulkanGlobalDispatchGeneration(resolver);
    const auto* live_global_dispatch         = std::get_if<LLRenderVulkan::VulkanGlobalDispatchGeneration>(&dispatch_after_surface_reset);
    ensure("the loader remains live while the surface child is absent", live_global_dispatch != nullptr);
    ensure("the live loader still resolves through the exact requirements function",
           live_global_dispatch->getInstanceProcAddr() == resolver);
    ensure_equals("surface creation and destruction emit no validation messages", instance_generation->validationSnapshot().mMessageCount,
                  std::uint32_t{ 0 });
    ensure("surface reset creates no current CGL context", CGLGetCurrentContext() == initial_cgl_context);
    ensure_equals("surface reset leaves the OpenGL manager unchanged", gGLManager.mInited, initial_gl_manager);

    ensure("main-thread native teardown succeeds", owner->reset());

    ensure("clean teardown removes the private Cocoa and Metal owner", !owner->hasNativeWindow());
    ensure("clean teardown removes the Vulkan requirements", !owner->hasRequirements() && owner->requirements() == nullptr);
    ensure("clean teardown removes the Vulkan instance parent", owner->instanceGeneration() == nullptr);
    ensure_equals("clean teardown leaves LLWindowManager unchanged", LLWindow::instanceCount(), initial_window_count);
    ensure("clean teardown leaves the initial CGL context unchanged", CGLGetCurrentContext() == initial_cgl_context);
    ensure_equals("clean teardown leaves the OpenGL manager unchanged", gGLManager.mInited, initial_gl_manager);
}

} // namespace tut
