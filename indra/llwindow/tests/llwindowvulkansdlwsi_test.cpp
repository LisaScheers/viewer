/**
 * @file llwindowvulkansdlwsi_test.cpp
 * @brief Opt-in native smoke for the SDL Vulkan window requirements path.
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
#include "llwindowsdl.h"
#include "llwindowvulkanrequirements.h"
#include "lltut.h"

#include "SDL3/SDL.h"
#include "SDL3/SDL_vulkan.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string_view>
#include <type_traits>
#include <variant>

namespace
{

constexpr const char* NATIVE_SMOKE_ENVIRONMENT = "LL_RUN_VULKAN_SDL_WSI_NATIVE";

struct SDLState
{
    SDL_LogOutputFunction mLogOutput   = nullptr;
    void*                 mLogUserdata = nullptr;
    SDL_InitFlags         mInitialized = 0;
    SDL_GLContext         mGLContext   = nullptr;
};

SDLState currentSDLState()
{
    SDLState state;
    SDL_GetLogOutputFunction(&state.mLogOutput, &state.mLogUserdata);
    state.mInitialized = SDL_WasInit(0);
    state.mGLContext   = SDL_GL_GetCurrentContext();
    return state;
}

bool nativeSmokeRequested()
{
    const char* value = std::getenv(NATIVE_SMOKE_ENVIRONMENT);
    return value && std::string_view(value) == "1";
}

std::uint32_t expectedImageCount(const VkSurfaceCapabilitiesKHR& capabilities)
{
    std::uint32_t image_count = capabilities.minImageCount;
    if (image_count != std::numeric_limits<std::uint32_t>::max() &&
        (capabilities.maxImageCount == 0 || image_count < capabilities.maxImageCount))
    {
        ++image_count;
    }
    return image_count;
}

VkExtent2D expectedImageExtent(const VkSurfaceCapabilitiesKHR& capabilities, VkExtent2D drawable_extent)
{
    if (capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max())
    {
        return capabilities.currentExtent;
    }
    return { std::clamp(drawable_extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
             std::clamp(drawable_extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height) };
}

VkCompositeAlphaFlagBitsKHR expectedCompositeAlpha(VkCompositeAlphaFlagsKHR supported)
{
    constexpr std::array<VkCompositeAlphaFlagBitsKHR, 4> priorities{ VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
                                                                     VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                                                                     VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
                                                                     VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR };
    for (VkCompositeAlphaFlagBitsKHR priority : priorities)
    {
        if ((supported & priority) != 0)
        {
            return priority;
        }
    }
    return static_cast<VkCompositeAlphaFlagBitsKHR>(0);
}

} // namespace

namespace tut
{

struct window_vulkan_sdl_wsi_test
{
};

using window_vulkan_sdl_wsi_group  = test_group<window_vulkan_sdl_wsi_test>;
using window_vulkan_sdl_wsi_object = window_vulkan_sdl_wsi_group::object;
window_vulkan_sdl_wsi_group window_vulkan_sdl_wsi_tests("window Vulkan SDL WSI native");

template<>
template<>
void window_vulkan_sdl_wsi_object::test<1>()
{
    static_assert(std::is_same_v<decltype(std::declval<const LLWindow&>().getVulkanRequirements()), const LLWindowVulkanRequirements*>);
    static_assert(noexcept(std::declval<const LLWindow&>().getVulkanRequirements()));
    static_assert(noexcept(std::declval<const LLWindow&>().isVulkanWindowGenerationCurrent(U64{})));

    if (!nativeSmokeRequested())
    {
        ensure("the native Vulkan SDL smoke remains explicitly opt-in", true);
        return;
    }

    const std::size_t initial_instance_count = LLWindow::instanceCount();
    const SDLState    initial_sdl_state      = currentSDLState();
    const bool        initial_gl_manager     = gGLManager.mInited;

    LLWindow* window = LLWindowManager::createWindow(nullptr, "SDL Vulkan native smoke", "llwindowvulkansdlwsi", 0, 0, 64, 64,
                                                     LLWindow::GraphicsAPI::Vulkan, LLWindow::FLAG_CREATE_HIDDEN);

    bool created                  = window != nullptr;
    bool tracked                  = created && LLWindowManager::isWindowValid(window);
    bool selected_vulkan          = created && window->getGraphicsAPI() == LLWindow::GraphicsAPI::Vulkan;
    bool x11_driver               = created && SDL_GetCurrentVideoDriver() && std::strcmp(SDL_GetCurrentVideoDriver(), "x11") == 0;
    bool no_gl_context            = created && SDL_GL_GetCurrentContext() == nullptr;
    bool no_gl_manager            = created && !gGLManager.mInited;
    bool requirements_published   = false;
    bool generation_current       = false;
    bool resolver_identity        = false;
    bool extensions_identical     = false;
    bool global_dispatch_resolved = false;

    bool instance_acquired                = false;
    bool instance_nonnull                 = false;
    bool instance_api_1_1                 = false;
    bool instance_generation_exact        = false;
    bool instance_extensions_ordered      = false;
    bool instance_validation_enabled      = false;
    bool instance_validation_clean        = false;
    bool instance_window_owned            = false;
    bool surface_acquired                 = false;
    bool surface_nonnull                  = false;
    bool surface_generation_exact         = false;
    bool surface_window_owned             = false;
    bool presentation_device_acquired     = false;
    bool presentation_device_nonnull      = false;
    bool presentation_device_api_1_1      = false;
    bool presentation_queue_usable        = false;
    bool presentation_extensions_exact    = false;
    bool presentation_device_removed      = false;
    bool logical_device_acquired          = false;
    bool logical_device_nonnull           = false;
    bool logical_queue_nonnull            = false;
    bool logical_provenance_exact         = false;
    bool logical_feature_exact            = false;
    bool logical_extensions_exact         = false;
    bool logical_device_removed           = false;
    bool swapchain_configuration_acquired = false;
    bool swapchain_drawable_extent_exact  = false;
    bool swapchain_format_supported       = false;
    bool swapchain_present_mode_exact     = false;
    bool swapchain_create_policy_exact    = false;
    bool swapchain_acquired               = false;
    bool swapchain_nonnull                = false;
    bool swapchain_provenance_exact       = false;
    bool swapchain_images_acquired         = false;
    bool swapchain_images_nonempty         = false;
    bool swapchain_image_views_complete    = false;
    bool swapchain_image_bounds_exact      = false;
    bool swapchain_images_provenance_exact = false;
    bool frame_slot_acquired               = false;
    bool frame_slot_handles_nonnull        = false;
    bool frame_slot_provenance_exact       = false;
    bool frame_slot_removed                = false;
    bool swapchain_images_removed          = false;
    bool swapchain_removed                = false;
    bool swapchain_configuration_removed  = false;
    bool surface_explicitly_reset         = false;
    bool surface_removed                  = false;
    bool surface_parent_still_live        = false;
    bool surface_validation_still_live    = false;
    bool mixed_opengl_rejected            = false;
    bool vulkan_context_switch_fails      = false;
    bool vulkan_shared_context_fails      = false;

    const LLRenderVulkan::VulkanInstanceGeneration* owned_instance_generation = nullptr;

    if (created)
    {
        const LLWindowVulkanRequirements* requirements = window->getVulkanRequirements();
        requirements_published                         = requirements != nullptr;
        if (requirements)
        {
            generation_current = window->isVulkanWindowGenerationCurrent(requirements->nativeWindowGeneration()) &&
                                 !window->isVulkanWindowGenerationCurrent(0);
            resolver_identity = requirements->resolver() == SDL_Vulkan_GetVkGetInstanceProcAddr();

            Uint32             extension_count = 0;
            const char* const* extension_names = SDL_Vulkan_GetInstanceExtensions(&extension_count);
            extensions_identical               = extension_names && requirements->requiredInstanceExtensions().size() == extension_count;
            for (std::size_t index = 0; extensions_identical && index < extension_count; ++index)
            {
                extensions_identical =
                    extension_names[index] && requirements->requiredInstanceExtensions()[index] == extension_names[index];
            }

            const auto dispatch_result = LLRenderVulkan::resolveVulkanGlobalDispatchGeneration(
                reinterpret_cast<PFN_vkGetInstanceProcAddr>(requirements->resolver()));
            global_dispatch_resolved = std::holds_alternative<LLRenderVulkan::VulkanGlobalDispatchGeneration>(dispatch_result);

            const auto* instance_generation = static_cast<const LLWindowSDL*>(window)->getVulkanInstanceGeneration();
            if (instance_generation)
            {
                owned_instance_generation = instance_generation;
                instance_acquired         = true;
                instance_nonnull          = instance_generation->instance() != VK_NULL_HANDLE;
                instance_api_1_1          = instance_generation->apiVersion() == VK_API_VERSION_1_1;
                instance_generation_exact = instance_generation->nativeWindowGeneration() == requirements->nativeWindowGeneration() &&
                                            window->isVulkanWindowGenerationCurrent(instance_generation->nativeWindowGeneration());
                instance_validation_enabled = instance_generation->validationEnabled();
                surface_acquired            = instance_generation->hasSurfaceGeneration();
                surface_nonnull             = instance_generation->surface() != VK_NULL_HANDLE;
                surface_generation_exact =
                    instance_generation->surfaceNativeWindowGeneration() == requirements->nativeWindowGeneration() &&
                    instance_generation->surfaceNativeWindowGeneration() == instance_generation->nativeWindowGeneration();
                presentation_device_acquired                         = instance_generation->hasPresentationDeviceGeneration();
                presentation_device_nonnull                          = instance_generation->physicalDevice() != VK_NULL_HANDLE;
                const VkPhysicalDeviceProperties physical_properties = instance_generation->physicalDeviceProperties();
                presentation_device_api_1_1 =
                    VK_API_VERSION_VARIANT(physical_properties.apiVersion) == 0 && physical_properties.apiVersion >= VK_API_VERSION_1_1;
                const VkQueueFamilyProperties queue_properties = instance_generation->presentationQueueFamilyProperties();
                presentation_queue_usable = instance_generation->presentationQueueFamilyIndex() != VK_QUEUE_FAMILY_IGNORED &&
                                            queue_properties.queueCount != 0 && (queue_properties.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
                const auto device_extensions = instance_generation->requiredDeviceExtensions();
                presentation_extensions_exact =
                    !device_extensions.empty() && device_extensions.front() == VK_KHR_SWAPCHAIN_EXTENSION_NAME &&
                    device_extensions.size() == (instance_generation->portabilitySubsetRequired() ? std::size_t{ 2 } : std::size_t{ 1 });
                if (presentation_extensions_exact && instance_generation->portabilitySubsetRequired())
                {
                    presentation_extensions_exact = device_extensions[1] == "VK_KHR_portability_subset";
                }
                logical_device_acquired = instance_generation->hasLogicalDeviceGeneration();
                logical_device_nonnull  = instance_generation->logicalDevice() != VK_NULL_HANDLE;
                logical_queue_nonnull   = instance_generation->presentationQueue() != VK_NULL_HANDLE;
                logical_provenance_exact =
                    instance_generation->logicalDevicePhysicalDevice() == instance_generation->physicalDevice() &&
                    instance_generation->logicalDeviceQueueFamilyIndex() == instance_generation->presentationQueueFamilyIndex() &&
                    instance_generation->logicalDeviceQueueIndex() == 0;
                logical_feature_exact                = instance_generation->logicalDeviceEnabledFeatures().independentBlend == VK_TRUE;
                const auto enabled_device_extensions = instance_generation->enabledDeviceExtensions();
                logical_extensions_exact             = enabled_device_extensions.size() == device_extensions.size();
                for (std::size_t index = 0; logical_extensions_exact && index < device_extensions.size(); ++index)
                {
                    logical_extensions_exact = enabled_device_extensions[index] == device_extensions[index];
                }

                swapchain_configuration_acquired = instance_generation->hasSwapchainConfigurationGeneration();
                LLCoordWindow current_drawable;
                const bool    drawable_queried = window->getSize(&current_drawable) && current_drawable.mX > 0 && current_drawable.mY > 0;
                const VkExtent2D retained_drawable = instance_generation->swapchainDrawableExtent();
                swapchain_drawable_extent_exact    = drawable_queried &&
                                                  retained_drawable.width == static_cast<std::uint32_t>(current_drawable.mX) &&
                                                  retained_drawable.height == static_cast<std::uint32_t>(current_drawable.mY);
                const VkSurfaceFormatKHR surface_format = instance_generation->swapchainSurfaceFormat();
                swapchain_format_supported =
                    (surface_format.format == VK_FORMAT_B8G8R8A8_UNORM || surface_format.format == VK_FORMAT_R8G8B8A8_UNORM) &&
                    surface_format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
                swapchain_present_mode_exact                   = instance_generation->swapchainPresentMode() == VK_PRESENT_MODE_FIFO_KHR;
                const VkSurfaceCapabilitiesKHR capabilities    = instance_generation->swapchainSurfaceCapabilities();
                const VkExtent2D               image_extent    = instance_generation->swapchainImageExtent();
                const VkExtent2D               expected_extent = expectedImageExtent(capabilities, retained_drawable);
                swapchain_create_policy_exact =
                    instance_generation->swapchainImageCount() == expectedImageCount(capabilities) &&
                    image_extent.width == expected_extent.width && image_extent.height == expected_extent.height &&
                    instance_generation->swapchainImageArrayLayers() == 1 &&
                    instance_generation->swapchainImageUsage() == VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT &&
                    instance_generation->swapchainImageSharingMode() == VK_SHARING_MODE_EXCLUSIVE &&
                    instance_generation->swapchainPreTransform() == capabilities.currentTransform &&
                    instance_generation->swapchainCompositeAlpha() == expectedCompositeAlpha(capabilities.supportedCompositeAlpha) &&
                    instance_generation->swapchainClipped() == VK_TRUE;
                swapchain_acquired         = instance_generation->hasSwapchainGeneration();
                swapchain_nonnull          = instance_generation->swapchain() != VK_NULL_HANDLE;
                swapchain_provenance_exact = instance_generation->swapchainDevice() != VK_NULL_HANDLE &&
                                             instance_generation->swapchainDevice() == instance_generation->logicalDevice() &&
                                             instance_generation->swapchainSurface() != VK_NULL_HANDLE &&
                                             instance_generation->swapchainSurface() == instance_generation->surface();
                swapchain_images_acquired       = instance_generation->hasSwapchainImagesGeneration();
                const std::uint32_t image_count = instance_generation->resolvedSwapchainImageCount();
                swapchain_images_nonempty       = image_count != 0;
                swapchain_image_views_complete  = swapchain_images_nonempty;
                for (std::uint32_t index = 0; swapchain_image_views_complete && index < image_count; ++index)
                {
                    swapchain_image_views_complete = instance_generation->swapchainImage(index) != VK_NULL_HANDLE &&
                                                     instance_generation->swapchainImageView(index) != VK_NULL_HANDLE;
                }
                swapchain_image_bounds_exact = instance_generation->swapchainImage(image_count) == VK_NULL_HANDLE &&
                                               instance_generation->swapchainImageView(image_count) == VK_NULL_HANDLE;
                swapchain_images_provenance_exact = swapchain_images_acquired && swapchain_acquired && logical_device_acquired &&
                                                    swapchain_configuration_acquired && swapchain_format_supported;
                frame_slot_acquired        = instance_generation->hasSwapchainFrameSlotGeneration();
                frame_slot_handles_nonnull = instance_generation->swapchainFrameCommandPool() != VK_NULL_HANDLE &&
                                             instance_generation->swapchainFrameCommandBuffer() != VK_NULL_HANDLE &&
                                             instance_generation->swapchainFrameImageAvailableSemaphore() != VK_NULL_HANDLE &&
                                             instance_generation->swapchainFrameSubmissionFence() != VK_NULL_HANDLE;
                frame_slot_provenance_exact = frame_slot_acquired && frame_slot_handles_nonnull && swapchain_images_provenance_exact &&
                                              instance_generation->logicalDevice() != VK_NULL_HANDLE &&
                                              instance_generation->presentationQueueFamilyIndex() != VK_QUEUE_FAMILY_IGNORED;

                const auto& required_extensions = requirements->requiredInstanceExtensions();
                const auto& enabled_extensions  = instance_generation->enabledExtensions();
                instance_extensions_ordered     = enabled_extensions.size() >= required_extensions.size();
                for (std::size_t index = 0; instance_extensions_ordered && index < required_extensions.size(); ++index)
                {
                    instance_extensions_ordered = enabled_extensions[index] == required_extensions[index];
                }
            }
        }

        const std::size_t count_before_mixed_request = LLWindow::instanceCount();
        LLWindow* mixed       = LLWindowManager::createWindow(nullptr, "forbidden mixed OpenGL window", "llwindowvulkansdlwsi", 0, 0, 1, 1,
                                                              LLWindow::GraphicsAPI::OpenGL, LLWindow::FLAG_CREATE_HIDDEN);
        mixed_opengl_rejected = mixed == nullptr && LLWindow::instanceCount() == count_before_mixed_request;
        if (mixed && LLWindowManager::isWindowValid(mixed))
        {
            LLWindowManager::destroyWindow(mixed);
        }

        vulkan_context_switch_fails = !window->switchContext(false, LLCoordScreen(64, 64), false);
        vulkan_shared_context_fails = window->createSharedContext() == nullptr;
        window->toggleVSync(false);
        window->swapBuffers();
    }

    if (owned_instance_generation)
    {
        instance_window_owned = static_cast<const LLWindowSDL*>(window)->getVulkanInstanceGeneration() == owned_instance_generation &&
                                owned_instance_generation->instance() != VK_NULL_HANDLE;
        surface_window_owned = owned_instance_generation->hasSurfaceGeneration() && owned_instance_generation->surface() != VK_NULL_HANDLE;
        surface_explicitly_reset = static_cast<LLWindowSDL*>(window)->resetVulkanSurfaceGeneration();
        surface_removed = !owned_instance_generation->hasSurfaceGeneration() && owned_instance_generation->surface() == VK_NULL_HANDLE;
        presentation_device_removed = !owned_instance_generation->hasPresentationDeviceGeneration() &&
                                      owned_instance_generation->physicalDevice() == VK_NULL_HANDLE &&
                                      owned_instance_generation->presentationQueueFamilyIndex() == VK_QUEUE_FAMILY_IGNORED;
        logical_device_removed = !owned_instance_generation->hasLogicalDeviceGeneration() &&
                                 owned_instance_generation->logicalDevice() == VK_NULL_HANDLE &&
                                 owned_instance_generation->presentationQueue() == VK_NULL_HANDLE;
        frame_slot_removed = !owned_instance_generation->hasSwapchainFrameSlotGeneration() &&
                             owned_instance_generation->swapchainFrameCommandPool() == VK_NULL_HANDLE &&
                             owned_instance_generation->swapchainFrameCommandBuffer() == VK_NULL_HANDLE &&
                             owned_instance_generation->swapchainFrameImageAvailableSemaphore() == VK_NULL_HANDLE &&
                             owned_instance_generation->swapchainFrameSubmissionFence() == VK_NULL_HANDLE;
        swapchain_images_removed = !owned_instance_generation->hasSwapchainImagesGeneration() &&
                                   owned_instance_generation->resolvedSwapchainImageCount() == 0 &&
                                   owned_instance_generation->swapchainImage(0) == VK_NULL_HANDLE &&
                                   owned_instance_generation->swapchainImageView(0) == VK_NULL_HANDLE;
        swapchain_removed = !owned_instance_generation->hasSwapchainGeneration() &&
                            owned_instance_generation->swapchain() == VK_NULL_HANDLE &&
                            owned_instance_generation->swapchainDevice() == VK_NULL_HANDLE &&
                            owned_instance_generation->swapchainSurface() == VK_NULL_HANDLE;
        swapchain_configuration_removed = !owned_instance_generation->hasSwapchainConfigurationGeneration();
        surface_parent_still_live = static_cast<const LLWindowSDL*>(window)->getVulkanInstanceGeneration() == owned_instance_generation &&
                                    owned_instance_generation->instance() != VK_NULL_HANDLE;
        surface_validation_still_live = owned_instance_generation->validationEnabled();
        instance_validation_clean     = owned_instance_generation->validationSnapshot().mMessageCount == 0;
    }

    const bool     destroyed       = created && LLWindowManager::destroyWindow(window);
    const SDLState final_sdl_state = currentSDLState();

    ensure("a real SDL Vulkan window is created", created);
    ensure("the real SDL Vulkan window is tracked", tracked);
    ensure("the real SDL window retains the Vulkan selection", selected_vulkan);
    ensure("the native claim runs against SDL's X11 driver", x11_driver);
    ensure("the Vulkan path creates no current OpenGL context", no_gl_context);
    ensure("the Vulkan path does not initialize the OpenGL manager", no_gl_manager);
    ensure("the window publishes immutable Vulkan instance requirements", requirements_published);
    ensure("the window accepts only its current nonzero native generation", generation_current);
    ensure("the requirements retain SDL's exact resolver", resolver_identity);
    ensure("the requirements deep-copy SDL's exact extension sequence", extensions_identical);
    ensure("the SDL resolver satisfies the Stage 30 global-dispatch contract", global_dispatch_resolved);
    ensure("the current SDL requirements acquire a validation-enabled Vulkan instance", instance_acquired);
    ensure("the acquired Vulkan instance handle is non-null", instance_nonnull);
    ensure("the acquired Vulkan instance requests API 1.1", instance_api_1_1);
    ensure("the Vulkan instance retains the exact current native-window generation", instance_generation_exact);
    ensure("the Vulkan instance enables SDL's required extensions in order", instance_extensions_ordered);
    ensure("the Vulkan instance enables required validation", instance_validation_enabled);
    ensure("the current instance owns one SDL-created Vulkan surface", surface_acquired);
    ensure("the SDL-created Vulkan surface handle is non-null", surface_nonnull);
    ensure("the Vulkan surface retains the exact instance and native-window generation", surface_generation_exact);
    ensure("the Vulkan surface is owned by the SDL window's exact instance parent", surface_window_owned);
    ensure("the exact SDL surface selects one presentation-capable physical device", presentation_device_acquired);
    ensure("the selected physical-device handle is non-null", presentation_device_nonnull);
    ensure("the selected physical device supports standard Vulkan 1.1 or newer", presentation_device_api_1_1);
    ensure("the selected queue family is nonempty and graphics-capable", presentation_queue_usable);
    ensure("the selected device retains exact swapchain and portability requirements", presentation_extensions_exact);
    ensure("the SDL Vulkan branch automatically owns one logical device", logical_device_acquired);
    ensure("the automatically created logical device is non-null", logical_device_nonnull);
    ensure("the automatically retrieved presentation queue is non-null", logical_queue_nonnull);
    ensure("the logical device retains exact physical-device, family, and queue provenance", logical_provenance_exact);
    ensure("the logical device enables the required independent-blend capability", logical_feature_exact);
    ensure("the logical device enables the selected extensions in exact order", logical_extensions_exact);
    ensure("the SDL Vulkan branch automatically owns one swapchain configuration", swapchain_configuration_acquired);
    ensure("the configuration retains the exact SDL drawable pixel extent", swapchain_drawable_extent_exact);
    ensure("the selected surface format follows the bounded UNORM nonlinear-sRGB policy", swapchain_format_supported);
    ensure("the selected present mode is FIFO", swapchain_present_mode_exact);
    ensure("the selected image count, extent, transform, alpha, usage, and sharing policy are supported", swapchain_create_policy_exact);
    ensure("the SDL Vulkan branch automatically owns one real swapchain", swapchain_acquired);
    ensure("the automatically created swapchain handle is non-null", swapchain_nonnull);
    ensure("the swapchain retains the exact logical-device and surface provenance", swapchain_provenance_exact);
    ensure("the SDL Vulkan branch automatically owns one swapchain-image generation", swapchain_images_acquired);
    ensure("the resolved swapchain image collection is nonempty", swapchain_images_nonempty);
    ensure("every resolved swapchain image has one non-null matching view", swapchain_image_views_complete);
    ensure("swapchain image and view lookup reject the first out-of-range index", swapchain_image_bounds_exact);
    ensure("the image collection retains its exact swapchain, device, configuration, and format parents",
           swapchain_images_provenance_exact);
    ensure("the SDL Vulkan branch automatically owns one frame-slot generation", frame_slot_acquired);
    ensure("the automatic frame slot owns non-null command-pool, command-buffer, semaphore, and fence handles", frame_slot_handles_nonnull);
    ensure("the frame slot retains the exact live queue-family, device, configuration, swapchain, and image parents",
           frame_slot_provenance_exact);
    ensure("the native smoke explicitly resets the Vulkan surface", surface_explicitly_reset);
    ensure("surface reset first removes the frame-slot generation and all four owned handles", frame_slot_removed);
    ensure("surface reset first removes every swapchain image and view", swapchain_images_removed);
    ensure("surface reset first removes the swapchain generation", swapchain_removed);
    ensure("explicit reset removes only the Vulkan surface child", surface_removed);
    ensure("surface reset first removes the presentation-device child", presentation_device_removed);
    ensure("surface reset first removes the logical-device child and borrowed queue", logical_device_removed);
    ensure("surface reset first removes the swapchain-configuration child", swapchain_configuration_removed);
    ensure("the exact parent instance remains live after explicit surface reset", surface_parent_still_live);
    ensure("required validation remains live while the surface is explicitly destroyed", surface_validation_still_live);
    ensure("surface creation and destruction emit no validation messages", instance_validation_clean);
    ensure("the Vulkan instance is owned by its SDL Vulkan window", instance_window_owned);
    ensure("a live Vulkan window rejects an OpenGL window before construction", mixed_opengl_rejected);
    ensure("Vulkan native-window recreation fails closed", vulkan_context_switch_fails);
    ensure("Vulkan shared OpenGL contexts fail closed", vulkan_shared_context_fails);
    ensure("the real SDL Vulkan window is destroyed", destroyed);
    ensure_equals("native teardown restores the LLWindow instance count", LLWindow::instanceCount(), initial_instance_count);
    ensure("native teardown removes the manager entry", !LLWindowManager::isWindowValid(window));
    ensure("native teardown restores SDL's log callback",
           final_sdl_state.mLogOutput == initial_sdl_state.mLogOutput && final_sdl_state.mLogUserdata == initial_sdl_state.mLogUserdata);
    ensure_equals("native teardown restores SDL subsystem state", final_sdl_state.mInitialized, initial_sdl_state.mInitialized);
    ensure("native teardown leaves no current OpenGL context", final_sdl_state.mGLContext == initial_sdl_state.mGLContext);
    ensure_equals("native teardown leaves the OpenGL manager unchanged", gGLManager.mInited, initial_gl_manager);
}

} // namespace tut
