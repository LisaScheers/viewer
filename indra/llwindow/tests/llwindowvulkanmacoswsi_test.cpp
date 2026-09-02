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

#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>
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
constexpr U32  REBUILT_BACKING_WIDTH      = 1440;
constexpr U32  REBUILT_BACKING_HEIGHT     = 810;
constexpr LLRenderVulkan::VulkanSwapchainFrameClearColor INITIAL_CLEAR_ONE{ { 0.125f, 0.25f, 0.5f, 1.0f } };
constexpr LLRenderVulkan::VulkanSwapchainFrameClearColor INITIAL_CLEAR_TWO{ { 0.75f, 0.125f, 0.375f, 1.0f } };
constexpr LLRenderVulkan::VulkanSwapchainFrameClearColor REBUILT_CLEAR_ONE{ { 0.0625f, 0.625f, 0.25f, 1.0f } };
constexpr LLRenderVulkan::VulkanSwapchainFrameClearColor REBUILT_CLEAR_TWO{ { 0.875f, 0.375f, 0.0625f, 1.0f } };

static_assert(INITIAL_CLEAR_ONE != INITIAL_CLEAR_TWO && INITIAL_CLEAR_ONE != REBUILT_CLEAR_ONE &&
              INITIAL_CLEAR_ONE != REBUILT_CLEAR_TWO && INITIAL_CLEAR_TWO != REBUILT_CLEAR_ONE &&
              INITIAL_CLEAR_TWO != REBUILT_CLEAR_TWO && REBUILT_CLEAR_ONE != REBUILT_CLEAR_TWO);

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

bool presentationCompleted(const LLRenderVulkan::VulkanSwapchainFrameSlotParentPresentationResult& result,
                           std::uint32_t                                                           image_count) noexcept
{
    const auto* success = std::get_if<LLRenderVulkan::VulkanSwapchainFrameSlotPresentationSuccess>(&result);
    return success && success->mImageIndex && *success->mImageIndex < image_count &&
           (success->mOutcome == LLRenderVulkan::VulkanSwapchainFrameSlotPresentationOutcome::Presented ||
            success->mOutcome == LLRenderVulkan::VulkanSwapchainFrameSlotPresentationOutcome::Suboptimal);
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
    ensure("the explicit diagnostic instance appends surface-capabilities2 before surface-maintenance1",
           enabled_extensions.size() >= required_extensions.size() + 2 &&
               enabled_extensions[required_extensions.size()] == VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME &&
               enabled_extensions[required_extensions.size() + 1] == VK_KHR_SURFACE_MAINTENANCE_1_EXTENSION_NAME);

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
    ensure("MoltenVK requires exact swapchain-maintenance and portability-subset device extensions",
           instance_generation->swapchainMaintenance1Supported() && instance_generation->portabilitySubsetRequired() &&
               device_extensions.size() == 3 && device_extensions[0] == VK_KHR_SWAPCHAIN_EXTENSION_NAME &&
               device_extensions[1] == VK_KHR_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME &&
               device_extensions[2] == "VK_KHR_portability_subset");
    ensure("presentation-device selection creates no current CGL context", CGLGetCurrentContext() == initial_cgl_context);
    ensure_equals("presentation-device selection leaves the OpenGL manager unchanged", gGLManager.mInited, initial_gl_manager);

    const auto logical_device_error = owner->acquireLogicalDeviceGeneration();
    ensure("the exact presentation selection creates one Vulkan logical device", !logical_device_error.has_value());
    ensure("the instance parent owns one logical-device generation", instance_generation->hasLogicalDeviceGeneration());
    ensure("the created logical device and its borrowed queue are non-null",
           instance_generation->logicalDevice() != VK_NULL_HANDLE && instance_generation->presentationQueue() != VK_NULL_HANDLE);
    ensure("the logical-device provenance matches the exact presentation selection",
           instance_generation->logicalDevicePhysicalDevice() == instance_generation->physicalDevice() &&
               instance_generation->logicalDeviceQueueFamilyIndex() == instance_generation->presentationQueueFamilyIndex() &&
               instance_generation->logicalDeviceQueueIndex() == 0);
    const auto enabled_device_extensions = instance_generation->enabledDeviceExtensions();
    ensure("the MoltenVK device enables exact swapchain-maintenance and portability-subset policy",
           instance_generation->logicalDeviceEnabledFeatures().independentBlend == VK_TRUE &&
               instance_generation->swapchainMaintenance1Enabled() && instance_generation->portabilitySubsetEnabled() &&
               enabled_device_extensions.size() == 3 && enabled_device_extensions[0] == VK_KHR_SWAPCHAIN_EXTENSION_NAME &&
               enabled_device_extensions[1] == VK_KHR_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME &&
               enabled_device_extensions[2] == "VK_KHR_portability_subset");
    ensure("logical-device acquisition creates no current CGL context", CGLGetCurrentContext() == initial_cgl_context);
    ensure_equals("logical-device acquisition leaves the OpenGL manager unchanged", gGLManager.mInited, initial_gl_manager);

    const auto swapchain_configuration_error = owner->acquireSwapchainConfigurationGeneration();
    ensure("the exact logical-device chain selects one swapchain configuration", !swapchain_configuration_error.has_value());
    ensure("the instance parent owns one swapchain-configuration generation", instance_generation->hasSwapchainConfigurationGeneration());
    const VkExtent2D drawable_extent = instance_generation->swapchainDrawableExtent();
    ensure("the configuration retains refreshed Cocoa backing pixels",
           drawable_extent.width == owner->drawableWidth() && drawable_extent.height == owner->drawableHeight());
    const VkSurfaceFormatKHR surface_format = instance_generation->swapchainSurfaceFormat();
    ensure("the MoltenVK format follows the bounded UNORM nonlinear-sRGB policy",
           (surface_format.format == VK_FORMAT_B8G8R8A8_UNORM || surface_format.format == VK_FORMAT_R8G8B8A8_UNORM) &&
               surface_format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);
    const VkSurfaceCapabilitiesKHR capabilities    = instance_generation->swapchainSurfaceCapabilities();
    const VkExtent2D               image_extent    = instance_generation->swapchainImageExtent();
    const VkExtent2D               expected_extent = expectedImageExtent(capabilities, drawable_extent);
    constexpr VkImageUsageFlags expected_image_usage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ensure("the MoltenVK surface admits the exact color-attachment and transfer-destination usage",
           (capabilities.supportedUsageFlags & expected_image_usage) == expected_image_usage);
    ensure("the MoltenVK create-ready configuration follows exact clear-capable policy",
           instance_generation->swapchainPresentMode() == VK_PRESENT_MODE_FIFO_KHR &&
               instance_generation->swapchainImageCount() == expectedImageCount(capabilities) &&
               image_extent.width == expected_extent.width && image_extent.height == expected_extent.height &&
               instance_generation->swapchainImageArrayLayers() == 1 &&
               instance_generation->swapchainImageUsage() == expected_image_usage &&
               instance_generation->swapchainImageSharingMode() == VK_SHARING_MODE_EXCLUSIVE &&
               instance_generation->swapchainPreTransform() == capabilities.currentTransform &&
               instance_generation->swapchainCompositeAlpha() == expectedCompositeAlpha(capabilities.supportedCompositeAlpha) &&
               instance_generation->swapchainClipped() == VK_TRUE);
    ensure("swapchain configuration creates no current CGL context", CGLGetCurrentContext() == initial_cgl_context);
    ensure_equals("swapchain configuration leaves the OpenGL manager unchanged", gGLManager.mInited, initial_gl_manager);

    const auto swapchain_error = owner->acquireSwapchainGeneration();
    ensure("the exact create-ready configuration creates one real swapchain", !swapchain_error.has_value());
    ensure("the instance parent owns one swapchain generation", instance_generation->hasSwapchainGeneration());
    ensure("the real MoltenVK swapchain handle is non-null", instance_generation->swapchain() != VK_NULL_HANDLE);
    ensure("the swapchain retains the exact logical-device and surface provenance",
           instance_generation->swapchainDevice() != VK_NULL_HANDLE &&
               instance_generation->swapchainDevice() == instance_generation->logicalDevice() &&
               instance_generation->swapchainSurface() != VK_NULL_HANDLE &&
               instance_generation->swapchainSurface() == instance_generation->surface());
    ensure("swapchain acquisition creates no current CGL context", CGLGetCurrentContext() == initial_cgl_context);
    ensure_equals("swapchain acquisition leaves the OpenGL manager unchanged", gGLManager.mInited, initial_gl_manager);

    const auto swapchain_images_error = owner->acquireSwapchainImagesGeneration();
    ensure("the exact live swapchain resolves its real images and creates matching views", !swapchain_images_error.has_value());
    ensure("the instance parent owns one swapchain-image generation", instance_generation->hasSwapchainImagesGeneration());
    std::uint32_t resolved_image_count = instance_generation->resolvedSwapchainImageCount();
    ensure("the real MoltenVK swapchain image collection is nonempty", resolved_image_count != 0);
    for (std::uint32_t index = 0; index < resolved_image_count; ++index)
    {
        ensure("every real MoltenVK swapchain image handle is non-null", instance_generation->swapchainImage(index) != VK_NULL_HANDLE);
        ensure("every real MoltenVK swapchain image has one non-null matching view",
               instance_generation->swapchainImageView(index) != VK_NULL_HANDLE);
    }
    ensure("swapchain image and view lookup reject the first out-of-range index",
           instance_generation->swapchainImage(resolved_image_count) == VK_NULL_HANDLE &&
               instance_generation->swapchainImageView(resolved_image_count) == VK_NULL_HANDLE);
    ensure("the image collection retains the exact swapchain, device, configuration, and selected-format parents",
           instance_generation->hasSwapchainGeneration() && instance_generation->swapchain() != VK_NULL_HANDLE &&
               instance_generation->swapchainDevice() == instance_generation->logicalDevice() &&
               instance_generation->hasSwapchainConfigurationGeneration() &&
               instance_generation->swapchainSurfaceFormat().format == surface_format.format);
    ensure("swapchain-image acquisition creates no current CGL context", CGLGetCurrentContext() == initial_cgl_context);
    ensure_equals("swapchain-image acquisition leaves the OpenGL manager unchanged", gGLManager.mInited, initial_gl_manager);

    const auto presentation_target_error = owner->acquireSwapchainPresentationTargetGeneration();
    ensure("the exact image-view chain creates one presentation-target generation", !presentation_target_error.has_value());
    ensure("the instance parent owns one presentation-target generation",
           instance_generation->hasSwapchainPresentationTargetGeneration());
    ensure("the real presentation target owns one non-null pass and one framebuffer per image view",
           instance_generation->swapchainPresentationRenderPass() != VK_NULL_HANDLE &&
               instance_generation->swapchainPresentationFramebufferCount() == resolved_image_count);
    for (std::uint32_t index = 0; index < resolved_image_count; ++index)
    {
        ensure("every real MoltenVK image view has one non-null presentation framebuffer",
               instance_generation->swapchainPresentationFramebuffer(index) != VK_NULL_HANDLE);
    }
    ensure("presentation framebuffer lookup rejects the first out-of-range index",
           instance_generation->swapchainPresentationFramebuffer(resolved_image_count) == VK_NULL_HANDLE);
    ensure_equals("presentation-target acquisition emits no validation messages",
                  instance_generation->validationSnapshot().mMessageCount, std::uint32_t{ 0 });
    ensure("presentation-target acquisition creates no current CGL context", CGLGetCurrentContext() == initial_cgl_context);
    ensure_equals("presentation-target acquisition leaves the OpenGL manager unchanged", gGLManager.mInited, initial_gl_manager);

    const auto frame_slot_error = owner->acquireSwapchainFrameSlotGeneration();
    ensure("the exact swapchain-image chain creates one idle frame slot", !frame_slot_error.has_value());
    ensure("the instance parent owns one frame-slot generation", instance_generation->hasSwapchainFrameSlotGeneration());
    ensure("the real frame slot owns all six non-null command and synchronization handles",
           instance_generation->swapchainFrameCommandPool() != VK_NULL_HANDLE &&
               instance_generation->swapchainFrameCommandBuffer() != VK_NULL_HANDLE &&
               instance_generation->swapchainFrameImageAvailableSemaphore() != VK_NULL_HANDLE &&
               instance_generation->swapchainFramePresentationReadySemaphore() != VK_NULL_HANDLE &&
               instance_generation->swapchainFrameSubmissionFence() != VK_NULL_HANDLE &&
               instance_generation->swapchainFramePresentCompletionFence() != VK_NULL_HANDLE);
    ensure("the frame slot retains the exact live queue-family, device, configuration, swapchain, and image parents",
           instance_generation->presentationQueueFamilyIndex() != VK_QUEUE_FAMILY_IGNORED &&
               instance_generation->logicalDevice() != VK_NULL_HANDLE && instance_generation->hasSwapchainConfigurationGeneration() &&
               instance_generation->hasSwapchainGeneration() && instance_generation->hasSwapchainImagesGeneration());
    ensure_equals("frame-slot creation emits no validation messages", instance_generation->validationSnapshot().mMessageCount,
                  std::uint32_t{ 0 });
    ensure("frame-slot acquisition creates no current CGL context", CGLGetCurrentContext() == initial_cgl_context);
    ensure_equals("frame-slot acquisition leaves the OpenGL manager unchanged", gGLManager.mInited, initial_gl_manager);

    const VkSemaphore initial_image_available          = instance_generation->swapchainFrameImageAvailableSemaphore();
    const VkSemaphore initial_presentation_ready       = instance_generation->swapchainFramePresentationReadySemaphore();
    const VkFence     initial_submission_fence         = instance_generation->swapchainFrameSubmissionFence();
    const VkFence     initial_present_completion_fence = instance_generation->swapchainFramePresentCompletionFence();
    const auto initial_first_presentation = owner->acquireClearToPresentSwapchainFrameSlot(INITIAL_CLEAR_ONE);
    ensure("the initial Metal swapchain submits and presents the first explicit clear cycle",
           presentationCompleted(initial_first_presentation, resolved_image_count) &&
               instance_generation->swapchainFrameSlotDisposition() == LLRenderVulkan::VulkanSwapchainFrameSlotDisposition::Reusable &&
               !instance_generation->swapchainFrameAcquiredImageIndex());
    ensure("the first initial clear cycle retains all four synchronization handles",
           initial_image_available != VK_NULL_HANDLE && initial_presentation_ready != VK_NULL_HANDLE &&
               initial_submission_fence != VK_NULL_HANDLE && initial_present_completion_fence != VK_NULL_HANDLE &&
               instance_generation->swapchainFrameImageAvailableSemaphore() == initial_image_available &&
               instance_generation->swapchainFramePresentationReadySemaphore() == initial_presentation_ready &&
               instance_generation->swapchainFrameSubmissionFence() == initial_submission_fence &&
               instance_generation->swapchainFramePresentCompletionFence() == initial_present_completion_fence);
    ensure("the first initial clear cycle creates no current CGL context", CGLGetCurrentContext() == initial_cgl_context);
    ensure_equals("the first initial clear cycle leaves the OpenGL manager unchanged", gGLManager.mInited, initial_gl_manager);

    const auto initial_second_presentation = owner->acquireClearToPresentSwapchainFrameSlot(INITIAL_CLEAR_TWO);
    ensure("the initial Metal swapchain submits and presents the second distinctive clear cycle",
           presentationCompleted(initial_second_presentation, resolved_image_count) &&
               instance_generation->swapchainFrameSlotDisposition() == LLRenderVulkan::VulkanSwapchainFrameSlotDisposition::Reusable &&
               !instance_generation->swapchainFrameAcquiredImageIndex());
    ensure("the second initial clear cycle retains all four synchronization handles",
           instance_generation->swapchainFrameImageAvailableSemaphore() == initial_image_available &&
               instance_generation->swapchainFramePresentationReadySemaphore() == initial_presentation_ready &&
               instance_generation->swapchainFrameSubmissionFence() == initial_submission_fence &&
               instance_generation->swapchainFramePresentCompletionFence() == initial_present_completion_fence);
    ensure_equals("two initial clear-present cycles emit no validation messages",
                  instance_generation->validationSnapshot().mMessageCount, std::uint32_t{ 0 });
    ensure("the second initial clear cycle creates no current CGL context", CGLGetCurrentContext() == initial_cgl_context);
    ensure_equals("the second initial clear cycle leaves the OpenGL manager unchanged", gGLManager.mInited, initial_gl_manager);

    const VkSurfaceKHR    retained_surface         = instance_generation->surface();
    const VkPhysicalDevice retained_physical_device = instance_generation->physicalDevice();
    const VkDevice         retained_logical_device  = instance_generation->logicalDevice();
    const VkQueue          retained_queue           = instance_generation->presentationQueue();
    ensure("the native diagnostic seam changes the hidden Cocoa drawable to one different backing extent",
           owner->resizeNativeDrawableForDiagnostic(REBUILT_BACKING_WIDTH, REBUILT_BACKING_HEIGHT));
    ensure("the resized native owner publishes the exact changed backing pixels",
           owner->drawableWidth() == REBUILT_BACKING_WIDTH && owner->drawableHeight() == REBUILT_BACKING_HEIGHT);

    const auto  rebuild_result  = owner->rebuildSwapchainChain();
    const auto* rebuild_outcome = std::get_if<LLRenderVulkan::VulkanSwapchainChainRebuildOutcome>(&rebuild_result);
    ensure("the current Cocoa owner rebuilds the complete swapchain-dependent chain",
           rebuild_outcome && *rebuild_outcome == LLRenderVulkan::VulkanSwapchainChainRebuildOutcome::Ready &&
               instance_generation->hasSwapchainConfigurationGeneration() && instance_generation->hasSwapchainGeneration() &&
               instance_generation->hasSwapchainImagesGeneration() &&
               instance_generation->hasSwapchainPresentationTargetGeneration() &&
               instance_generation->hasSwapchainFrameSlotGeneration());
    const VkExtent2D rebuilt_drawable_extent = instance_generation->swapchainDrawableExtent();
    ensure("the rebuilt configuration authenticates the changed Cocoa backing extent",
           rebuilt_drawable_extent.width == REBUILT_BACKING_WIDTH &&
               rebuilt_drawable_extent.height == REBUILT_BACKING_HEIGHT &&
               owner->drawableWidth() == rebuilt_drawable_extent.width && owner->drawableHeight() == rebuilt_drawable_extent.height &&
               instance_generation->swapchainImageUsage() == expected_image_usage);
    ensure("same-surface rebuild retains every older Vulkan parent and borrowed queue",
           instance_generation->surface() == retained_surface && instance_generation->physicalDevice() == retained_physical_device &&
               instance_generation->logicalDevice() == retained_logical_device &&
               instance_generation->presentationQueue() == retained_queue);
    resolved_image_count = instance_generation->resolvedSwapchainImageCount();
    ensure("the rebuilt MoltenVK swapchain publishes a complete nonempty image and frame-slot chain",
           resolved_image_count != 0 && instance_generation->swapchain() != VK_NULL_HANDLE &&
               instance_generation->swapchainImage(0) != VK_NULL_HANDLE &&
               instance_generation->swapchainImageView(0) != VK_NULL_HANDLE &&
               instance_generation->swapchainPresentationRenderPass() != VK_NULL_HANDLE &&
               instance_generation->swapchainPresentationFramebufferCount() == resolved_image_count &&
               instance_generation->swapchainPresentationFramebuffer(0) != VK_NULL_HANDLE &&
               instance_generation->swapchainFrameCommandBuffer() != VK_NULL_HANDLE &&
               instance_generation->swapchainFrameImageAvailableSemaphore() != VK_NULL_HANDLE &&
               instance_generation->swapchainFramePresentationReadySemaphore() != VK_NULL_HANDLE &&
               instance_generation->swapchainFrameSubmissionFence() != VK_NULL_HANDLE &&
               instance_generation->swapchainFramePresentCompletionFence() != VK_NULL_HANDLE);
    ensure_equals("changed-extent rebuild emits no validation messages", instance_generation->validationSnapshot().mMessageCount,
                  std::uint32_t{ 0 });
    ensure("changed-extent rebuild creates no current CGL context", CGLGetCurrentContext() == initial_cgl_context);
    ensure_equals("changed-extent rebuild leaves the OpenGL manager unchanged", gGLManager.mInited, initial_gl_manager);

    const VkSemaphore image_available          = instance_generation->swapchainFrameImageAvailableSemaphore();
    const VkSemaphore presentation_ready       = instance_generation->swapchainFramePresentationReadySemaphore();
    const VkFence     submission_fence         = instance_generation->swapchainFrameSubmissionFence();
    const VkFence     present_completion_fence = instance_generation->swapchainFramePresentCompletionFence();
    const auto        first_presentation       = owner->acquireClearToPresentSwapchainFrameSlot(REBUILT_CLEAR_ONE);
    ensure("the rebuilt Metal swapchain submits and presents the first explicit clear cycle",
           presentationCompleted(first_presentation, resolved_image_count) &&
               instance_generation->swapchainFrameSlotDisposition() == LLRenderVulkan::VulkanSwapchainFrameSlotDisposition::Reusable &&
               !instance_generation->swapchainFrameAcquiredImageIndex());
    ensure("the first rebuilt clear cycle retains all four synchronization handles",
           image_available != VK_NULL_HANDLE && presentation_ready != VK_NULL_HANDLE && submission_fence != VK_NULL_HANDLE &&
               present_completion_fence != VK_NULL_HANDLE &&
               instance_generation->swapchainFrameImageAvailableSemaphore() == image_available &&
               instance_generation->swapchainFramePresentationReadySemaphore() == presentation_ready &&
               instance_generation->swapchainFrameSubmissionFence() == submission_fence &&
               instance_generation->swapchainFramePresentCompletionFence() == present_completion_fence);
    ensure("the first rebuilt clear cycle creates no current CGL context", CGLGetCurrentContext() == initial_cgl_context);
    ensure_equals("the first rebuilt clear cycle leaves the OpenGL manager unchanged", gGLManager.mInited, initial_gl_manager);

    const auto second_presentation = owner->acquireClearToPresentSwapchainFrameSlot(REBUILT_CLEAR_TWO);
    ensure("the rebuilt Metal swapchain submits and presents the second distinctive clear cycle",
           presentationCompleted(second_presentation, resolved_image_count) &&
               instance_generation->swapchainFrameSlotDisposition() == LLRenderVulkan::VulkanSwapchainFrameSlotDisposition::Reusable &&
               !instance_generation->swapchainFrameAcquiredImageIndex());
    ensure("the second rebuilt clear cycle retains all four synchronization handles",
           instance_generation->swapchainFrameImageAvailableSemaphore() == image_available &&
               instance_generation->swapchainFramePresentationReadySemaphore() == presentation_ready &&
               instance_generation->swapchainFrameSubmissionFence() == submission_fence &&
               instance_generation->swapchainFramePresentCompletionFence() == present_completion_fence);
    ensure_equals("two rebuilt clear-present cycles emit no validation messages",
                  instance_generation->validationSnapshot().mMessageCount, std::uint32_t{ 0 });
    ensure("the second rebuilt clear cycle creates no current CGL context", CGLGetCurrentContext() == initial_cgl_context);
    ensure_equals("the second rebuilt clear cycle leaves the OpenGL manager unchanged", gGLManager.mInited, initial_gl_manager);

    ensure("the native smoke explicitly resets the frame slot before swapchain images", owner->resetSwapchainFrameSlotGeneration());
    ensure("explicit frame-slot reset removes all six owned handles",
           !instance_generation->hasSwapchainFrameSlotGeneration() && instance_generation->swapchainFrameCommandPool() == VK_NULL_HANDLE &&
               instance_generation->swapchainFrameCommandBuffer() == VK_NULL_HANDLE &&
               instance_generation->swapchainFrameImageAvailableSemaphore() == VK_NULL_HANDLE &&
               instance_generation->swapchainFramePresentationReadySemaphore() == VK_NULL_HANDLE &&
               instance_generation->swapchainFrameSubmissionFence() == VK_NULL_HANDLE &&
               instance_generation->swapchainFramePresentCompletionFence() == VK_NULL_HANDLE &&
               !instance_generation->swapchainFrameAcquiredImageIndex());
    ensure("frame-slot reset leaves its exact image, swapchain, configuration, device, and surface parents live",
           instance_generation->hasSwapchainImagesGeneration() && instance_generation->resolvedSwapchainImageCount() != 0 &&
               instance_generation->hasSwapchainGeneration() && instance_generation->swapchain() != VK_NULL_HANDLE &&
               instance_generation->hasSwapchainConfigurationGeneration() && instance_generation->hasLogicalDeviceGeneration() &&
               instance_generation->logicalDevice() != VK_NULL_HANDLE && instance_generation->hasSurfaceGeneration() &&
               instance_generation->surface() != VK_NULL_HANDLE);
    ensure_equals("frame-slot creation and destruction emit no validation messages",
                  instance_generation->validationSnapshot().mMessageCount, std::uint32_t{ 0 });
    ensure("frame-slot reset creates no current CGL context", CGLGetCurrentContext() == initial_cgl_context);
    ensure_equals("frame-slot reset leaves the OpenGL manager unchanged", gGLManager.mInited, initial_gl_manager);

    ensure("the native smoke explicitly resets the presentation target before swapchain images",
           owner->resetSwapchainPresentationTargetGeneration());
    ensure("explicit presentation-target reset removes its pass and every framebuffer",
           !instance_generation->hasSwapchainPresentationTargetGeneration() &&
               instance_generation->swapchainPresentationRenderPass() == VK_NULL_HANDLE &&
               instance_generation->swapchainPresentationFramebufferCount() == 0 &&
               instance_generation->swapchainPresentationFramebuffer(0) == VK_NULL_HANDLE);
    ensure("presentation-target reset leaves the exact image, swapchain, configuration, device, and surface parents live",
           instance_generation->hasSwapchainImagesGeneration() && instance_generation->resolvedSwapchainImageCount() != 0 &&
               instance_generation->hasSwapchainGeneration() && instance_generation->hasSwapchainConfigurationGeneration() &&
               instance_generation->hasLogicalDeviceGeneration() && instance_generation->hasSurfaceGeneration());
    ensure_equals("presentation-target creation and destruction emit no validation messages",
                  instance_generation->validationSnapshot().mMessageCount, std::uint32_t{ 0 });
    ensure("presentation-target reset creates no current CGL context", CGLGetCurrentContext() == initial_cgl_context);
    ensure_equals("presentation-target reset leaves the OpenGL manager unchanged", gGLManager.mInited, initial_gl_manager);

    ensure("the native smoke explicitly resets the swapchain images before the swapchain", owner->resetSwapchainImagesGeneration());
    ensure("explicit image reset removes every borrowed image and owned image view",
           !instance_generation->hasSwapchainImagesGeneration() && instance_generation->resolvedSwapchainImageCount() == 0 &&
               instance_generation->swapchainImage(0) == VK_NULL_HANDLE && instance_generation->swapchainImageView(0) == VK_NULL_HANDLE);
    ensure("swapchain-image reset leaves its exact swapchain, configuration, device, and surface parents live",
           instance_generation->hasSwapchainGeneration() && instance_generation->swapchain() != VK_NULL_HANDLE &&
               instance_generation->hasSwapchainConfigurationGeneration() && instance_generation->hasLogicalDeviceGeneration() &&
               instance_generation->logicalDevice() != VK_NULL_HANDLE && instance_generation->hasSurfaceGeneration() &&
               instance_generation->surface() != VK_NULL_HANDLE);
    ensure_equals("swapchain-image creation and destruction emit no validation messages",
                  instance_generation->validationSnapshot().mMessageCount, std::uint32_t{ 0 });
    ensure("swapchain-image reset creates no current CGL context", CGLGetCurrentContext() == initial_cgl_context);
    ensure_equals("swapchain-image reset leaves the OpenGL manager unchanged", gGLManager.mInited, initial_gl_manager);

    ensure("the native smoke explicitly resets the swapchain before its older parents", owner->resetSwapchainGeneration());
    ensure("explicit reset removes the swapchain handle and provenance",
           !instance_generation->hasSwapchainGeneration() && instance_generation->swapchain() == VK_NULL_HANDLE &&
               instance_generation->swapchainDevice() == VK_NULL_HANDLE && instance_generation->swapchainSurface() == VK_NULL_HANDLE);
    ensure("swapchain reset leaves its exact configuration, logical device, and surface parents live",
           instance_generation->hasSwapchainConfigurationGeneration() && instance_generation->hasLogicalDeviceGeneration() &&
               instance_generation->logicalDevice() != VK_NULL_HANDLE && instance_generation->hasSurfaceGeneration() &&
               instance_generation->surface() != VK_NULL_HANDLE);
    ensure_equals("swapchain creation and destruction emit no validation messages", instance_generation->validationSnapshot().mMessageCount,
                  std::uint32_t{ 0 });
    ensure("swapchain reset creates no current CGL context", CGLGetCurrentContext() == initial_cgl_context);
    ensure_equals("swapchain reset leaves the OpenGL manager unchanged", gGLManager.mInited, initial_gl_manager);

    ensure("the native smoke explicitly resets the Vulkan surface", owner->resetSurfaceGeneration());
    ensure("explicit reset removes only the surface child",
           !instance_generation->hasSurfaceGeneration() && instance_generation->surface() == VK_NULL_HANDLE);
    ensure("surface reset first removes the presentation-device child",
           !instance_generation->hasPresentationDeviceGeneration() && instance_generation->physicalDevice() == VK_NULL_HANDLE &&
               instance_generation->presentationQueueFamilyIndex() == VK_QUEUE_FAMILY_IGNORED);
    ensure("surface reset first destroys the logical-device generation and clears its borrowed queue",
           !instance_generation->hasLogicalDeviceGeneration() && instance_generation->logicalDevice() == VK_NULL_HANDLE &&
               instance_generation->presentationQueue() == VK_NULL_HANDLE);
    ensure("surface reset first removes the swapchain-configuration generation",
           !instance_generation->hasSwapchainConfigurationGeneration());
    ensure("surface reset leaves no swapchain generation",
           !instance_generation->hasSwapchainGeneration() && instance_generation->swapchain() == VK_NULL_HANDLE);
    ensure("surface reset leaves no swapchain-image generation",
           !instance_generation->hasSwapchainImagesGeneration() && instance_generation->resolvedSwapchainImageCount() == 0 &&
               instance_generation->swapchainImage(0) == VK_NULL_HANDLE && instance_generation->swapchainImageView(0) == VK_NULL_HANDLE);
    ensure("surface reset leaves no presentation-target generation",
           !instance_generation->hasSwapchainPresentationTargetGeneration() &&
               instance_generation->swapchainPresentationRenderPass() == VK_NULL_HANDLE &&
               instance_generation->swapchainPresentationFramebufferCount() == 0);
    ensure("surface reset leaves no frame-slot generation or owned frame handle",
           !instance_generation->hasSwapchainFrameSlotGeneration() && instance_generation->swapchainFrameCommandPool() == VK_NULL_HANDLE &&
               instance_generation->swapchainFrameCommandBuffer() == VK_NULL_HANDLE &&
               instance_generation->swapchainFrameImageAvailableSemaphore() == VK_NULL_HANDLE &&
               instance_generation->swapchainFramePresentationReadySemaphore() == VK_NULL_HANDLE &&
               instance_generation->swapchainFrameSubmissionFence() == VK_NULL_HANDLE &&
               instance_generation->swapchainFramePresentCompletionFence() == VK_NULL_HANDLE &&
               !instance_generation->swapchainFrameAcquiredImageIndex());
    ensure("the exact instance parent remains live after surface reset",
           owner->instanceGeneration() == instance_generation && instance_generation->instance() != VK_NULL_HANDLE);
    ensure("required validation remains live during explicit surface destruction", instance_generation->validationEnabled());
    ensure("the private Cocoa and Metal owner remains live after surface reset", owner->hasNativeWindow());
    ensure("the private Metal geometry remains live after surface reset", owner->refreshNativeGeometry());
    ensure_equals("the live Metal layer retains its rebuilt backing-pixel width", owner->drawableWidth(), REBUILT_BACKING_WIDTH);
    ensure_equals("the live Metal layer retains its rebuilt backing-pixel height", owner->drawableHeight(), REBUILT_BACKING_HEIGHT);
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
