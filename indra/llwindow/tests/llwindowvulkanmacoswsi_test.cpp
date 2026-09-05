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
#include "lltextureuploaddiagnostic.h"
#include "llwindow.h"
#include "llwindowmacosxvulkan.h"
#include "llwindowmacosxvulkan-objc.h"
#include "lltut.h"

#include <OpenGL/OpenGL.h>

#include <algorithm>
#include <array>
#include <cstring>
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
constexpr VkBufferUsageFlags UPLOAD_DESTINATION_USAGE = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

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

struct FrameSlotOperationContext
{
    const LLWindowMacOSXVulkan*                     mWindow     = nullptr;
    const LLRenderVulkan::VulkanInstanceGeneration* mGeneration = nullptr;
};

bool frameSlotInstanceOwnerIsCurrent(void* userdata, const LLRenderVulkan::VulkanInstanceGeneration& generation) noexcept
{
    const auto& context = *static_cast<const FrameSlotOperationContext*>(userdata);
    return context.mWindow && context.mGeneration == &generation && context.mWindow->instanceGeneration() == context.mGeneration;
}

bool frameSlotWindowGenerationIsCurrent(void* userdata, std::uint64_t native_window_generation) noexcept
{
    const auto& context = *static_cast<const FrameSlotOperationContext*>(userdata);
    return context.mWindow && context.mWindow->isGenerationCurrent(native_window_generation);
}

bool presentationCompleted(const LLRenderVulkan::VulkanSwapchainFrameSlotParentPresentationResult& result,
                           std::uint32_t                                                           image_count) noexcept
{
    const auto* success = std::get_if<LLRenderVulkan::VulkanSwapchainFrameSlotPresentationSuccess>(&result);
    return success && success->mImageIndex && *success->mImageIndex < image_count &&
           (success->mOutcome == LLRenderVulkan::VulkanSwapchainFrameSlotPresentationOutcome::Presented ||
            success->mOutcome == LLRenderVulkan::VulkanSwapchainFrameSlotPresentationOutcome::Suboptimal);
}

bool presentationObserved(const LLRenderVulkan::VulkanSwapchainFrameSlotParentPresentationResult& result,
                          std::uint32_t                                                           image_count,
                          VkFormat                                                                image_format,
                          VkExtent2D                                                              image_extent,
                          VkFormat                                                                readback_format,
                          VkExtent2D                                                              readback_extent) noexcept
{
    const auto* success = std::get_if<LLRenderVulkan::VulkanSwapchainFrameSlotPresentationSuccess>(&result);
    if (!presentationCompleted(result, image_count) || !success->mObservation || image_format != readback_format ||
        image_extent.width != readback_extent.width || image_extent.height != readback_extent.height || image_extent.width == 0 ||
        image_extent.height == 0 ||
        static_cast<std::uint64_t>(image_extent.height) >
            std::numeric_limits<std::uint64_t>::max() / static_cast<std::uint64_t>(image_extent.width))
    {
        return false;
    }

    const std::uint64_t expected_pixel_count =
        static_cast<std::uint64_t>(image_extent.width) * static_cast<std::uint64_t>(image_extent.height);
    const auto& observation = *success->mObservation;
    return observation.mImageFormat == image_format && observation.mImageFormat == readback_format &&
           observation.mImageExtent.width == image_extent.width && observation.mImageExtent.height == image_extent.height &&
           observation.mImageExtent.width == readback_extent.width && observation.mImageExtent.height == readback_extent.height &&
           observation.mTotalPixelCount == expected_pixel_count && observation.mGreenPixelCount == expected_pixel_count &&
           observation.mRedPixelCount == 0 && observation.mUnexpectedPixelCount == 0;
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

    auto*                     mutable_instance_generation = const_cast<LLRenderVulkan::VulkanInstanceGeneration*>(instance_generation);
    FrameSlotOperationContext operation_context{ owner, instance_generation };

    LLRenderVulkan::VulkanTextureUploadDestinationRequest texture_destination_request;
    texture_destination_request.mNativeWindowGeneration = NATIVE_WINDOW_GENERATION;
    texture_destination_request.mDescription            = LLRenderVulkan::vulkanTextureUploadDestinationDescription();
    texture_destination_request.mInstanceOwnerCheck     = { &operation_context, frameSlotInstanceOwnerIsCurrent };
    texture_destination_request.mWindowGenerationCheck  = { &operation_context, frameSlotWindowGenerationIsCurrent };
    const auto texture_destination_error =
        mutable_instance_generation->acquireTextureUploadDestinationGeneration(texture_destination_request);
    ensure("the exact logical-device chain acquires one native texture upload destination", !texture_destination_error.has_value());
    const auto retained_texture_handle             = instance_generation->textureUploadDestinationResourceHandle();
    const auto retained_texture_revision           = instance_generation->textureUploadDestinationExpectedRevision();
    const auto retained_texture_resident_extent    = instance_generation->textureUploadDestinationResidentExtent();
    const auto retained_texture_logical_extent     = instance_generation->textureUploadDestinationLogicalExtent();
    const auto retained_texture_discard            = instance_generation->textureUploadDestinationResidentDiscard();
    const auto retained_texture_format             = instance_generation->textureUploadDestinationFormat();
    const auto retained_texture_format_features    = instance_generation->textureUploadDestinationFormatFeatures();
    const auto retained_texture_format_properties  = instance_generation->textureUploadDestinationImageFormatProperties();
    const auto retained_texture_image              = instance_generation->textureUploadDestinationImage();
    const auto retained_texture_memory             = instance_generation->textureUploadDestinationMemory();
    const auto retained_texture_allocation         = instance_generation->textureUploadDestinationAllocationSize();
    const auto retained_texture_alignment          = instance_generation->textureUploadDestinationAllocationAlignment();
    const auto retained_texture_memory_bits        = instance_generation->textureUploadDestinationCompatibleMemoryTypeBits();
    const auto retained_texture_memory_type        = instance_generation->textureUploadDestinationMemoryTypeIndex();
    const auto retained_texture_memory_flags       = instance_generation->textureUploadDestinationMemoryPropertyFlags();
    const auto retained_texture_view               = instance_generation->textureUploadDestinationImageView();
    const auto retained_texture_view_range         = instance_generation->textureUploadDestinationViewRange();
    const bool retained_texture_prefers_dedicated  = instance_generation->textureUploadDestinationPrefersDedicatedAllocation();
    const bool retained_texture_requires_dedicated = instance_generation->textureUploadDestinationRequiresDedicatedAllocation();
    constexpr VkFormatFeatureFlags required_texture_features =
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT |
        VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    const auto texture_destination_matches_contract = [&]() noexcept
    {
        const auto& description = texture_destination_request.mDescription;
        const auto  resident    = instance_generation->textureUploadDestinationResidentExtent();
        const auto  logical     = instance_generation->textureUploadDestinationLogicalExtent();
        const auto  properties  = instance_generation->textureUploadDestinationImageFormatProperties();
        const auto  requirements = instance_generation->textureUploadDestinationMemoryRequirements();
        const auto  range        = instance_generation->textureUploadDestinationViewRange();
        const auto  memory_type  = instance_generation->textureUploadDestinationMemoryTypeIndex();
        return instance_generation->hasTextureUploadDestinationGeneration() &&
               instance_generation->textureUploadDestinationResourceHandle() == description.mHandle &&
               instance_generation->textureUploadDestinationExpectedRevision() == description.mExpectedRevision &&
               resident.width == description.mResidentExtent.mWidth && resident.height == description.mResidentExtent.mHeight &&
               resident.depth == 1 && logical.mWidth == description.mLogicalExtent.mWidth &&
               logical.mHeight == description.mLogicalExtent.mHeight &&
               instance_generation->textureUploadDestinationResidentDiscard() == description.mResidentDiscard &&
               instance_generation->textureUploadDestinationPixelFormat() == description.mFormat &&
               instance_generation->textureUploadDestinationInitialState() == description.mInitialState &&
               instance_generation->textureUploadDestinationFlags() == 0 &&
               instance_generation->textureUploadDestinationImageType() == VK_IMAGE_TYPE_2D &&
               instance_generation->textureUploadDestinationFormat() == VK_FORMAT_R8G8B8A8_UNORM &&
               instance_generation->textureUploadDestinationMipLevels() == description.mMipLevels &&
               instance_generation->textureUploadDestinationArrayLayers() == description.mArrayLayers &&
               instance_generation->textureUploadDestinationSamples() == VK_SAMPLE_COUNT_1_BIT &&
               instance_generation->textureUploadDestinationTiling() == VK_IMAGE_TILING_OPTIMAL &&
               instance_generation->textureUploadDestinationUsage() ==
                   (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT) &&
               instance_generation->textureUploadDestinationSharingMode() == VK_SHARING_MODE_EXCLUSIVE &&
               instance_generation->textureUploadDestinationInitialLayout() == VK_IMAGE_LAYOUT_UNDEFINED &&
               (instance_generation->textureUploadDestinationFormatFeatures() & required_texture_features) == required_texture_features &&
               properties.maxExtent.width >= description.mResidentExtent.mWidth &&
               properties.maxExtent.height >= description.mResidentExtent.mHeight && properties.maxExtent.depth >= 1 &&
               properties.maxMipLevels >= description.mMipLevels && properties.maxArrayLayers >= description.mArrayLayers &&
               (properties.sampleCounts & VK_SAMPLE_COUNT_1_BIT) != 0 &&
               instance_generation->textureUploadDestinationImage() != VK_NULL_HANDLE &&
               instance_generation->textureUploadDestinationMemory() != VK_NULL_HANDLE && requirements.size != 0 &&
               requirements.alignment != 0 && requirements.memoryTypeBits != 0 &&
               instance_generation->textureUploadDestinationAllocationSize() == requirements.size &&
               instance_generation->textureUploadDestinationAllocationAlignment() == requirements.alignment &&
               instance_generation->textureUploadDestinationCompatibleMemoryTypeBits() == requirements.memoryTypeBits &&
               memory_type < VK_MAX_MEMORY_TYPES && (requirements.memoryTypeBits & (std::uint32_t{ 1 } << memory_type)) != 0 &&
               (instance_generation->textureUploadDestinationMemoryPropertyFlags() & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0 &&
               instance_generation->textureUploadDestinationIsDeviceLocal() &&
               instance_generation->textureUploadDestinationImageView() != VK_NULL_HANDLE &&
               instance_generation->textureUploadDestinationImageViewType() == VK_IMAGE_VIEW_TYPE_2D &&
               range.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT && range.baseMipLevel == 0 &&
               range.levelCount == description.mMipLevels && range.baseArrayLayer == 0 &&
               range.layerCount == description.mArrayLayers;
    };
    const auto texture_destination_retained        = [&]() noexcept
    {
        const auto properties = instance_generation->textureUploadDestinationImageFormatProperties();
        const auto range      = instance_generation->textureUploadDestinationViewRange();
        return texture_destination_matches_contract() &&
               instance_generation->textureUploadDestinationResourceHandle() == retained_texture_handle &&
               instance_generation->textureUploadDestinationExpectedRevision() == retained_texture_revision &&
               instance_generation->textureUploadDestinationResidentExtent().width == retained_texture_resident_extent.width &&
               instance_generation->textureUploadDestinationResidentExtent().height == retained_texture_resident_extent.height &&
               instance_generation->textureUploadDestinationLogicalExtent().mWidth == retained_texture_logical_extent.mWidth &&
               instance_generation->textureUploadDestinationLogicalExtent().mHeight == retained_texture_logical_extent.mHeight &&
               instance_generation->textureUploadDestinationResidentDiscard() == retained_texture_discard &&
               instance_generation->textureUploadDestinationFormat() == retained_texture_format &&
               instance_generation->textureUploadDestinationFormatFeatures() == retained_texture_format_features &&
               properties.maxExtent.width == retained_texture_format_properties.maxExtent.width &&
               properties.maxExtent.height == retained_texture_format_properties.maxExtent.height &&
               properties.maxMipLevels == retained_texture_format_properties.maxMipLevels &&
               properties.maxArrayLayers == retained_texture_format_properties.maxArrayLayers &&
               properties.sampleCounts == retained_texture_format_properties.sampleCounts &&
               properties.maxResourceSize == retained_texture_format_properties.maxResourceSize &&
               instance_generation->textureUploadDestinationImage() == retained_texture_image &&
               instance_generation->textureUploadDestinationMemory() == retained_texture_memory &&
               instance_generation->textureUploadDestinationAllocationSize() == retained_texture_allocation &&
               instance_generation->textureUploadDestinationAllocationAlignment() == retained_texture_alignment &&
               instance_generation->textureUploadDestinationCompatibleMemoryTypeBits() == retained_texture_memory_bits &&
               instance_generation->textureUploadDestinationMemoryTypeIndex() == retained_texture_memory_type &&
               instance_generation->textureUploadDestinationMemoryPropertyFlags() == retained_texture_memory_flags &&
               instance_generation->textureUploadDestinationIsDeviceLocal() &&
               instance_generation->textureUploadDestinationPrefersDedicatedAllocation() == retained_texture_prefers_dedicated &&
               instance_generation->textureUploadDestinationRequiresDedicatedAllocation() == retained_texture_requires_dedicated &&
               instance_generation->textureUploadDestinationImageView() == retained_texture_view &&
               range.aspectMask == retained_texture_view_range.aspectMask &&
               range.baseMipLevel == retained_texture_view_range.baseMipLevel &&
               range.levelCount == retained_texture_view_range.levelCount &&
               range.baseArrayLayer == retained_texture_view_range.baseArrayLayer &&
               range.layerCount == retained_texture_view_range.layerCount;
    };
    ensure("the native texture destination publishes the canonical image, allocation, view, and capability metadata",
           texture_destination_matches_contract() && texture_destination_retained());
    ensure_equals("texture-destination acquisition emits no validation messages", instance_generation->validationSnapshot().mMessageCount,
                  std::uint32_t{ 0 });

    const LLRenderContract::TextureUploadFixture upload_fixture = LLRenderContract::makeTextureUploadFixture();
    const LLRenderContract::TextureUploadCase    texture_upload_case = LLRenderContract::makeTextureUploadCase();
    const auto decoded_texture_upload = LLRenderContract::decodeStreamingUploadFrame(texture_upload_case.mFrame);
    ensure("the native texture source starts from the exact decoded replacement-image upload contract",
           decoded_texture_upload &&
               decoded_texture_upload->mHandles.mReplacementImage == LLRenderContract::StreamingUploadHandles{}.mReplacementImage &&
               decoded_texture_upload->mRevision == LLRenderContract::TEXTURE_UPLOAD_REVISION &&
               decoded_texture_upload->mExtent.mWidth == LLRenderContract::TEXTURE_UPLOAD_RESIDENT_WIDTH &&
               decoded_texture_upload->mExtent.mHeight == LLRenderContract::TEXTURE_UPLOAD_RESIDENT_HEIGHT &&
               decoded_texture_upload->mSourceFormat == LLRenderContract::PixelFormat::RGBA8Unorm &&
               decoded_texture_upload->mRowPitch == LLRenderContract::TEXTURE_UPLOAD_ROW_PITCH &&
               decoded_texture_upload->mRowOrigin == LLRenderContract::RowOrigin::TopLeft &&
               decoded_texture_upload->mPixels.size() == LLRenderVulkan::VULKAN_TEXTURE_UPLOAD_SOURCE_BYTE_COUNT &&
               std::equal(decoded_texture_upload->mPixels.begin(), decoded_texture_upload->mPixels.end(),
                          upload_fixture.mSourceRGBA8.begin()));
    LLRenderVulkan::VulkanTextureUploadSourceBytes texture_source_bytes{};
    std::copy(decoded_texture_upload->mPixels.begin(), decoded_texture_upload->mPixels.end(), texture_source_bytes.begin());
    bool         exact_poison_padding = true;
    std::uint8_t expected_poison      = 0xf0;
    for (std::size_t row = 0; row < LLRenderContract::TEXTURE_UPLOAD_RESIDENT_HEIGHT; ++row)
    {
        for (std::size_t byte = 32; byte < LLRenderContract::TEXTURE_UPLOAD_ROW_PITCH; ++byte)
        {
            exact_poison_padding =
                exact_poison_padding && texture_source_bytes[row * LLRenderContract::TEXTURE_UPLOAD_ROW_PITCH + byte] == expected_poison++;
        }
    }
    ensure("the decoded source bytes contain all sixteen diagnostic row-padding poison bytes", exact_poison_padding);

    const LLRenderVulkan::VulkanTextureUploadSourceDescription texture_source_description =
        LLRenderVulkan::vulkanTextureUploadSourceDescription(texture_source_bytes);
    LLRenderVulkan::VulkanTextureUploadSourceRequest texture_source_request;
    texture_source_request.mNativeWindowGeneration = NATIVE_WINDOW_GENERATION;
    texture_source_request.mDescription            = texture_source_description;
    texture_source_request.mInstanceOwnerCheck     = { &operation_context, frameSlotInstanceOwnerIsCurrent };
    texture_source_request.mWindowGenerationCheck  = { &operation_context, frameSlotWindowGenerationIsCurrent };
    const F64  texture_source_backing_scale        = owner->backingScale();
    const auto texture_source_error = mutable_instance_generation->acquireTextureUploadSourceGeneration(texture_source_request);
    ensure("the exact texture destination acquires one native immutable 144-byte upload source", !texture_source_error.has_value());
    ensure("the native texture source publishes its exact contract and host-visible allocation metadata",
           instance_generation->hasTextureUploadSourceGeneration() &&
               instance_generation->textureUploadSourceResourceHandle() == texture_source_description.mHandle &&
               instance_generation->textureUploadSourceExpectedRevision() == texture_source_description.mExpectedRevision &&
               instance_generation->textureUploadSourceResidentExtent().mWidth == LLRenderContract::TEXTURE_UPLOAD_RESIDENT_WIDTH &&
               instance_generation->textureUploadSourceResidentExtent().mHeight == LLRenderContract::TEXTURE_UPLOAD_RESIDENT_HEIGHT &&
               instance_generation->textureUploadSourcePixelFormat() == LLRenderContract::PixelFormat::RGBA8Unorm &&
               instance_generation->textureUploadSourceRowPitch() == LLRenderContract::TEXTURE_UPLOAD_ROW_PITCH &&
               instance_generation->textureUploadSourceRowOrigin() == LLRenderContract::RowOrigin::TopLeft &&
               instance_generation->textureUploadSourceContentIdentity() ==
                   LLRenderContract::stableByteContentIdentity(texture_source_description.mBytes) &&
               instance_generation->textureUploadSourceFlags() == 0 &&
               instance_generation->textureUploadSourceUsage() == VK_BUFFER_USAGE_TRANSFER_SRC_BIT &&
               instance_generation->textureUploadSourceSharingMode() == VK_SHARING_MODE_EXCLUSIVE &&
               instance_generation->textureUploadSourceBuffer() != VK_NULL_HANDLE &&
               instance_generation->textureUploadSourceMemory() != VK_NULL_HANDLE &&
               instance_generation->textureUploadSourceMemory() != retained_texture_memory &&
               instance_generation->textureUploadSourceByteCount() == LLRenderVulkan::VULKAN_TEXTURE_UPLOAD_SOURCE_BYTE_COUNT &&
               instance_generation->textureUploadSourceAllocationSize() >= instance_generation->textureUploadSourceByteCount() &&
               (instance_generation->textureUploadSourceMemoryPropertyFlags() & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0 &&
               instance_generation->textureUploadSourceIsCoherent() ==
                   ((instance_generation->textureUploadSourceMemoryPropertyFlags() & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0) &&
               texture_destination_retained());
    ensure_equals("texture-source acquisition emits no validation messages", instance_generation->validationSnapshot().mMessageCount,
                  std::uint32_t{ 0 });
    ensure("texture-source acquisition preserves the private Cocoa owner, scale, and exact backing-pixel geometry",
           owner->hasNativeWindow() && owner->requirements() == requirements && owner->instanceGeneration() == instance_generation &&
               owner->isGenerationCurrent(NATIVE_WINDOW_GENERATION) && owner->backingScale() == texture_source_backing_scale &&
               owner->drawableWidth() == BACKING_WIDTH && owner->drawableHeight() == BACKING_HEIGHT &&
               LLWindow::instanceCount() == initial_window_count);
    ensure("texture-source acquisition creates no current CGL context", CGLGetCurrentContext() == initial_cgl_context);
    ensure_equals("texture-source acquisition leaves the OpenGL manager unchanged", gGLManager.mInited, initial_gl_manager);

    const LLRenderContract::ImageHandle retained_texture_source_handle      = instance_generation->textureUploadSourceResourceHandle();
    const std::uint64_t                 retained_texture_source_revision    = instance_generation->textureUploadSourceExpectedRevision();
    const LLRenderContract::Extent2D    retained_texture_source_extent      = instance_generation->textureUploadSourceResidentExtent();
    const LLRenderContract::PixelFormat retained_texture_source_format      = instance_generation->textureUploadSourcePixelFormat();
    const std::uint32_t                 retained_texture_source_row_pitch   = instance_generation->textureUploadSourceRowPitch();
    const LLRenderContract::RowOrigin   retained_texture_source_row_origin  = instance_generation->textureUploadSourceRowOrigin();
    const std::uint64_t                 retained_texture_source_identity    = instance_generation->textureUploadSourceContentIdentity();
    const VkBufferCreateFlags           retained_texture_source_flags       = instance_generation->textureUploadSourceFlags();
    const VkBufferUsageFlags            retained_texture_source_usage       = instance_generation->textureUploadSourceUsage();
    const VkSharingMode                 retained_texture_source_sharing     = instance_generation->textureUploadSourceSharingMode();
    const VkBuffer                      retained_texture_source_buffer      = instance_generation->textureUploadSourceBuffer();
    const VkDeviceMemory                retained_texture_source_memory      = instance_generation->textureUploadSourceMemory();
    const VkDeviceSize                  retained_texture_source_bytes       = instance_generation->textureUploadSourceByteCount();
    const VkDeviceSize                  retained_texture_source_allocation  = instance_generation->textureUploadSourceAllocationSize();
    const std::uint32_t                 retained_texture_source_memory_type = instance_generation->textureUploadSourceMemoryTypeIndex();
    const VkMemoryPropertyFlags retained_texture_source_memory_flags        = instance_generation->textureUploadSourceMemoryPropertyFlags();
    const bool                  retained_texture_source_coherent            = instance_generation->textureUploadSourceIsCoherent();
    const auto                  texture_source_retained                     = [&]() noexcept
    {
        return instance_generation->hasTextureUploadSourceGeneration() && texture_destination_retained() &&
               instance_generation->textureUploadSourceResourceHandle() == retained_texture_source_handle &&
               instance_generation->textureUploadSourceExpectedRevision() == retained_texture_source_revision &&
               instance_generation->textureUploadSourceResidentExtent().mWidth == retained_texture_source_extent.mWidth &&
               instance_generation->textureUploadSourceResidentExtent().mHeight == retained_texture_source_extent.mHeight &&
               instance_generation->textureUploadSourcePixelFormat() == retained_texture_source_format &&
               instance_generation->textureUploadSourceRowPitch() == retained_texture_source_row_pitch &&
               instance_generation->textureUploadSourceRowOrigin() == retained_texture_source_row_origin &&
               instance_generation->textureUploadSourceContentIdentity() == retained_texture_source_identity &&
               instance_generation->textureUploadSourceFlags() == retained_texture_source_flags &&
               instance_generation->textureUploadSourceUsage() == retained_texture_source_usage &&
               instance_generation->textureUploadSourceSharingMode() == retained_texture_source_sharing &&
               instance_generation->textureUploadSourceBuffer() == retained_texture_source_buffer &&
               instance_generation->textureUploadSourceMemory() == retained_texture_source_memory &&
               instance_generation->textureUploadSourceByteCount() == retained_texture_source_bytes &&
               instance_generation->textureUploadSourceAllocationSize() == retained_texture_source_allocation &&
               instance_generation->textureUploadSourceMemoryTypeIndex() == retained_texture_source_memory_type &&
               instance_generation->textureUploadSourceMemoryPropertyFlags() == retained_texture_source_memory_flags &&
               instance_generation->textureUploadSourceIsCoherent() == retained_texture_source_coherent;
    };

    LLRenderVulkan::VulkanTextureUploadTransferRequest texture_transfer_request;
    texture_transfer_request.mNativeWindowGeneration = NATIVE_WINDOW_GENERATION;
    texture_transfer_request.mSourceDescription      = texture_source_description;
    texture_transfer_request.mDestinationDescription = texture_destination_request.mDescription;
    texture_transfer_request.mInstanceOwnerCheck     = { &operation_context, frameSlotInstanceOwnerIsCurrent };
    texture_transfer_request.mWindowGenerationCheck  = { &operation_context, frameSlotWindowGenerationIsCurrent };
    LLRenderVulkan::VulkanTextureUploadTransferOperationRequest texture_transfer_operation;
    texture_transfer_operation.mNativeWindowGeneration = NATIVE_WINDOW_GENERATION;
    texture_transfer_operation.mSourceDescription      = texture_source_description;
    texture_transfer_operation.mDestinationDescription = texture_destination_request.mDescription;
    texture_transfer_operation.mTimeoutNs              = 1'000'000'000;
    texture_transfer_operation.mInstanceOwnerCheck     = { &operation_context, frameSlotInstanceOwnerIsCurrent };
    texture_transfer_operation.mWindowGenerationCheck  = { &operation_context, frameSlotWindowGenerationIsCurrent };

    ensure("the native destination remains unpublished before its texture upload transfer",
           !instance_generation->textureUploadDestinationIsResident());
    const auto texture_transfer_error = mutable_instance_generation->acquireTextureUploadTransferGeneration(texture_transfer_request);
    ensure("the native aggregate acquires one texture transfer over the exact source and destination",
           !texture_transfer_error.has_value() && instance_generation->hasTextureUploadTransferGeneration() &&
               instance_generation->textureUploadTransferResourceHandle() == retained_texture_handle &&
               instance_generation->textureUploadTransferExpectedRevision() == retained_texture_revision &&
               instance_generation->textureUploadTransferContentIdentity() == retained_texture_source_identity &&
               instance_generation->textureUploadTransferSourceBuffer() == retained_texture_source_buffer &&
               instance_generation->textureUploadTransferDestinationImage() == retained_texture_image &&
               instance_generation->textureUploadTransferQueue() == instance_generation->presentationQueue() &&
               instance_generation->textureUploadTransferCommandPool() != VK_NULL_HANDLE &&
               instance_generation->textureUploadTransferCommandBuffer() != VK_NULL_HANDLE &&
               instance_generation->textureUploadTransferFence() != VK_NULL_HANDLE &&
               instance_generation->textureUploadTransferDisposition() == LLRenderVulkan::VulkanTextureUploadTransferDisposition::Ready);
    auto texture_transfer_execution = mutable_instance_generation->executeTextureUploadTransfer(texture_transfer_operation);
    while (instance_generation->textureUploadTransferDisposition() == LLRenderVulkan::VulkanTextureUploadTransferDisposition::Pending)
    {
        texture_transfer_execution = mutable_instance_generation->retryTextureUploadTransferCompletion(texture_transfer_operation);
    }
    const auto* texture_transfer_disposition =
        std::get_if<LLRenderVulkan::VulkanTextureUploadTransferDisposition>(&texture_transfer_execution);
    ensure(
        "one native texture transfer completes and publishes exact shader-readable residency",
        texture_transfer_disposition && *texture_transfer_disposition == LLRenderVulkan::VulkanTextureUploadTransferDisposition::Complete &&
            instance_generation->textureUploadTransferDisposition() == LLRenderVulkan::VulkanTextureUploadTransferDisposition::Complete &&
            instance_generation->textureUploadTransferSubmissionCount() == 1 &&
            instance_generation->textureUploadTransferCompletionWaitCount() >= 1 &&
            instance_generation->textureUploadDestinationIsResident() &&
            instance_generation->textureUploadDestinationResidentRevision() == retained_texture_revision &&
            instance_generation->textureUploadDestinationResidentContentIdentity() == retained_texture_source_identity &&
            instance_generation->textureUploadDestinationCurrentState() == LLRenderContract::ImageState::ShaderRead);
    ensure_equals("native texture-transfer execution emits no validation messages", instance_generation->validationSnapshot().mMessageCount,
                  std::uint32_t{ 0 });
    ensure("texture-transfer execution preserves the Cocoa owner, backing geometry, and OpenGL isolation",
           owner->hasNativeWindow() && owner->backingScale() == texture_source_backing_scale && owner->drawableWidth() == BACKING_WIDTH &&
               owner->drawableHeight() == BACKING_HEIGHT && LLWindow::instanceCount() == initial_window_count &&
               CGLGetCurrentContext() == initial_cgl_context && gGLManager.mInited == initial_gl_manager);

    const VkCommandPool   retained_texture_transfer_pool   = instance_generation->textureUploadTransferCommandPool();
    const VkCommandBuffer retained_texture_transfer_buffer = instance_generation->textureUploadTransferCommandBuffer();
    const VkFence         retained_texture_transfer_fence  = instance_generation->textureUploadTransferFence();
    const auto            texture_transfer_retained        = [&]() noexcept
    {
        return texture_source_retained() && instance_generation->hasTextureUploadTransferGeneration() &&
               instance_generation->textureUploadTransferSourceBuffer() == retained_texture_source_buffer &&
               instance_generation->textureUploadTransferDestinationImage() == retained_texture_image &&
               instance_generation->textureUploadTransferCommandPool() == retained_texture_transfer_pool &&
               instance_generation->textureUploadTransferCommandBuffer() == retained_texture_transfer_buffer &&
               instance_generation->textureUploadTransferFence() == retained_texture_transfer_fence &&
               instance_generation->textureUploadTransferDisposition() ==
                   LLRenderVulkan::VulkanTextureUploadTransferDisposition::Complete &&
               instance_generation->textureUploadDestinationIsResident() &&
               instance_generation->textureUploadDestinationResidentRevision() == retained_texture_revision &&
               instance_generation->textureUploadDestinationResidentContentIdentity() == retained_texture_source_identity &&
               instance_generation->textureUploadDestinationCurrentState() == LLRenderContract::ImageState::ShaderRead;
    };

    const LLRenderVulkan::VulkanTextureUploadSampleBindingDescription texture_sample_binding_description =
        LLRenderVulkan::vulkanTextureUploadSampleBindingDescription();
    const LLRenderVulkan::VulkanTextureUploadSamplePipelineDescription texture_sample_pipeline_description =
        LLRenderVulkan::vulkanTextureUploadSamplePipelineDescription();
    LLRenderVulkan::VulkanTextureUploadSampleBindingRequest texture_sample_binding_request;
    texture_sample_binding_request.mNativeWindowGeneration = NATIVE_WINDOW_GENERATION;
    texture_sample_binding_request.mDestinationDescription = texture_destination_request.mDescription;
    texture_sample_binding_request.mDescription            = texture_sample_binding_description;
    texture_sample_binding_request.mInstanceOwnerCheck     = { &operation_context, frameSlotInstanceOwnerIsCurrent };
    texture_sample_binding_request.mWindowGenerationCheck  = { &operation_context, frameSlotWindowGenerationIsCurrent };
    const auto texture_sample_binding_error =
        mutable_instance_generation->acquireTextureUploadSampleBindingGeneration(texture_sample_binding_request);
    ensure(
        "the completed native texture upload acquires one canonical sampled-image binding",
        !texture_sample_binding_error.has_value() && instance_generation->hasTextureUploadSampleBindingGeneration() &&
            instance_generation->textureUploadSampleBindingSamplerResourceHandle() == texture_sample_binding_description.mSampler.mHandle &&
            instance_generation->textureUploadSampleBindingDestinationResourceHandle() == retained_texture_handle &&
            instance_generation->textureUploadSampleBindingExpectedRevision() == retained_texture_revision &&
            instance_generation->textureUploadSampleBindingResidentRevision() == retained_texture_revision &&
            instance_generation->textureUploadSampleBindingResidentContentIdentity() == retained_texture_source_identity &&
            retained_texture_source_identity != 0 &&
            instance_generation->textureUploadSampleBindingDestinationImageView() == retained_texture_view &&
            instance_generation->textureUploadSampleBindingDestinationImageLayout() == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
            instance_generation->textureUploadSampleBindingDescriptorSetIndex() == texture_sample_binding_description.mDescriptorSetIndex &&
            instance_generation->textureUploadSampleBindingBinding() == texture_sample_binding_description.mBinding &&
            instance_generation->textureUploadSampleBindingSampler() != VK_NULL_HANDLE &&
            instance_generation->textureUploadSampleBindingDescriptorSetLayout() != VK_NULL_HANDLE &&
            instance_generation->textureUploadSampleBindingPipelineLayout() != VK_NULL_HANDLE &&
            instance_generation->textureUploadSampleBindingDescriptorPool() != VK_NULL_HANDLE &&
            instance_generation->textureUploadSampleBindingDescriptorSet() != VK_NULL_HANDLE && texture_transfer_retained());
    ensure_equals("texture sample-binding acquisition emits no validation messages",
                  instance_generation->validationSnapshot().mMessageCount,
                  std::uint32_t{ 0 });
    ensure("texture sample-binding acquisition preserves the Cocoa owner and exact backing-pixel geometry",
           owner->hasNativeWindow() && owner->requirements() == requirements && owner->instanceGeneration() == instance_generation &&
               owner->isGenerationCurrent(NATIVE_WINDOW_GENERATION) && owner->backingScale() == texture_source_backing_scale &&
               owner->drawableWidth() == BACKING_WIDTH && owner->drawableHeight() == BACKING_HEIGHT &&
               LLWindow::instanceCount() == initial_window_count);
    ensure("texture sample-binding acquisition creates no current CGL context", CGLGetCurrentContext() == initial_cgl_context);
    ensure_equals("texture sample-binding acquisition leaves the OpenGL manager unchanged", gGLManager.mInited, initial_gl_manager);

    const LLRenderContract::SamplerHandle retained_texture_sample_sampler_handle =
        instance_generation->textureUploadSampleBindingSamplerResourceHandle();
    const LLRenderContract::ImageHandle retained_texture_sample_destination_handle =
        instance_generation->textureUploadSampleBindingDestinationResourceHandle();
    const std::uint64_t retained_texture_sample_expected_revision = instance_generation->textureUploadSampleBindingExpectedRevision();
    const std::uint64_t retained_texture_sample_resident_revision = instance_generation->textureUploadSampleBindingResidentRevision();
    const std::uint64_t retained_texture_sample_resident_identity =
        instance_generation->textureUploadSampleBindingResidentContentIdentity();
    const VkImageView   retained_texture_sample_destination_view = instance_generation->textureUploadSampleBindingDestinationImageView();
    const VkImageLayout retained_texture_sample_destination_layout =
        instance_generation->textureUploadSampleBindingDestinationImageLayout();
    const std::uint32_t         retained_texture_sample_set        = instance_generation->textureUploadSampleBindingDescriptorSetIndex();
    const std::uint32_t         retained_texture_sample_binding    = instance_generation->textureUploadSampleBindingBinding();
    const VkSampler             retained_texture_sample_sampler    = instance_generation->textureUploadSampleBindingSampler();
    const VkDescriptorSetLayout retained_texture_sample_set_layout = instance_generation->textureUploadSampleBindingDescriptorSetLayout();
    const VkPipelineLayout      retained_texture_sample_pipeline_layout = instance_generation->textureUploadSampleBindingPipelineLayout();
    const VkDescriptorPool      retained_texture_sample_pool            = instance_generation->textureUploadSampleBindingDescriptorPool();
    const VkDescriptorSet       retained_texture_sample_descriptor_set  = instance_generation->textureUploadSampleBindingDescriptorSet();
    const auto                  texture_sample_binding_retained         = [&]() noexcept
    {
        return texture_destination_retained() && instance_generation->hasTextureUploadSampleBindingGeneration() &&
               instance_generation->textureUploadSampleBindingSamplerResourceHandle() == retained_texture_sample_sampler_handle &&
               instance_generation->textureUploadSampleBindingDestinationResourceHandle() == retained_texture_sample_destination_handle &&
               instance_generation->textureUploadSampleBindingExpectedRevision() == retained_texture_sample_expected_revision &&
               instance_generation->textureUploadSampleBindingResidentRevision() == retained_texture_sample_resident_revision &&
               instance_generation->textureUploadSampleBindingResidentContentIdentity() == retained_texture_sample_resident_identity &&
               instance_generation->textureUploadSampleBindingDestinationImageView() == retained_texture_sample_destination_view &&
               instance_generation->textureUploadSampleBindingDestinationImageLayout() == retained_texture_sample_destination_layout &&
               instance_generation->textureUploadSampleBindingDescriptorSetIndex() == retained_texture_sample_set &&
               instance_generation->textureUploadSampleBindingBinding() == retained_texture_sample_binding &&
               instance_generation->textureUploadSampleBindingSampler() == retained_texture_sample_sampler &&
               instance_generation->textureUploadSampleBindingDescriptorSetLayout() == retained_texture_sample_set_layout &&
               instance_generation->textureUploadSampleBindingPipelineLayout() == retained_texture_sample_pipeline_layout &&
               instance_generation->textureUploadSampleBindingDescriptorPool() == retained_texture_sample_pool &&
               instance_generation->textureUploadSampleBindingDescriptorSet() == retained_texture_sample_descriptor_set;
    };

    static_assert(sizeof(upload_fixture.mScreenTriangle) == LLRenderVulkan::VULKAN_UPLOAD_SOURCE_BYTE_COUNT);
    LLRenderVulkan::VulkanUploadSourceDescription upload_source_description;
    upload_source_description.mHandle = LLRenderContract::StreamingUploadHandles{}.mScreenTriangle;
    std::memcpy(upload_source_description.mBytes.data(), upload_fixture.mScreenTriangle.data(), upload_source_description.mBytes.size());

    LLRenderVulkan::VulkanUploadSourceRequest upload_source_request;
    upload_source_request.mNativeWindowGeneration = NATIVE_WINDOW_GENERATION;
    upload_source_request.mDescription            = upload_source_description;
    upload_source_request.mInstanceOwnerCheck     = { &operation_context, frameSlotInstanceOwnerIsCurrent };
    upload_source_request.mWindowGenerationCheck  = { &operation_context, frameSlotWindowGenerationIsCurrent };

    const F64  upload_source_backing_scale = owner->backingScale();
    const auto upload_source_error         = mutable_instance_generation->acquireUploadSourceGeneration(upload_source_request);
    ensure("the exact logical-device chain acquires the 48-byte texture-fixture upload source", !upload_source_error.has_value());
    ensure("the immutable upload source publishes exact resource and allocation metadata",
           instance_generation->hasUploadSourceGeneration() &&
               instance_generation->uploadSourceResourceHandle() == upload_source_description.mHandle &&
               instance_generation->uploadSourceContentIdentity() != 0 && instance_generation->uploadSourceBuffer() != VK_NULL_HANDLE &&
               instance_generation->uploadSourceMemory() != VK_NULL_HANDLE &&
               instance_generation->uploadSourceByteCount() == LLRenderVulkan::VULKAN_UPLOAD_SOURCE_BYTE_COUNT &&
               instance_generation->uploadSourceAllocationSize() >= instance_generation->uploadSourceByteCount() &&
               (instance_generation->uploadSourceMemoryPropertyFlags() & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0 &&
               instance_generation->uploadSourceIsCoherent() ==
                   ((instance_generation->uploadSourceMemoryPropertyFlags() & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0));
    ensure("the 144-byte texture and 48-byte vertex sources coexist with distinct buffers and allocations",
           texture_source_retained() && instance_generation->textureUploadSourceBuffer() != instance_generation->uploadSourceBuffer() &&
               instance_generation->textureUploadSourceMemory() != instance_generation->uploadSourceMemory() &&
               instance_generation->textureUploadSourceByteCount() == LLRenderVulkan::VULKAN_TEXTURE_UPLOAD_SOURCE_BYTE_COUNT &&
               instance_generation->uploadSourceByteCount() == LLRenderVulkan::VULKAN_UPLOAD_SOURCE_BYTE_COUNT);
    ensure_equals("upload-source acquisition emits no validation messages", instance_generation->validationSnapshot().mMessageCount,
                  std::uint32_t{ 0 });
    ensure("upload-source acquisition preserves the private Cocoa owner and exact drawable geometry",
           owner->hasNativeWindow() && owner->requirements() == requirements && owner->instanceGeneration() == instance_generation &&
               owner->isGenerationCurrent(NATIVE_WINDOW_GENERATION) && owner->backingScale() == upload_source_backing_scale &&
               owner->drawableWidth() == BACKING_WIDTH && owner->drawableHeight() == BACKING_HEIGHT &&
               LLWindow::instanceCount() == initial_window_count);
    ensure("upload-source acquisition creates no current CGL context", CGLGetCurrentContext() == initial_cgl_context);
    ensure_equals("upload-source acquisition leaves the OpenGL manager unchanged", gGLManager.mInited, initial_gl_manager);

    const LLRenderContract::BufferHandle retained_upload_source_handle       = instance_generation->uploadSourceResourceHandle();
    const std::uint64_t                  retained_upload_source_identity     = instance_generation->uploadSourceContentIdentity();
    const VkBuffer                       retained_upload_source_buffer       = instance_generation->uploadSourceBuffer();
    const VkDeviceMemory                 retained_upload_source_memory       = instance_generation->uploadSourceMemory();
    const VkDeviceSize                   retained_upload_source_bytes        = instance_generation->uploadSourceByteCount();
    const VkDeviceSize                   retained_upload_source_allocation   = instance_generation->uploadSourceAllocationSize();
    const std::uint32_t                  retained_upload_source_memory_type  = instance_generation->uploadSourceMemoryTypeIndex();
    const VkMemoryPropertyFlags          retained_upload_source_memory_flags = instance_generation->uploadSourceMemoryPropertyFlags();
    const bool                           retained_upload_source_coherent     = instance_generation->uploadSourceIsCoherent();
    const auto                           upload_source_retained              = [&]() noexcept
    {
        return instance_generation->hasUploadSourceGeneration() &&
               instance_generation->uploadSourceResourceHandle() == retained_upload_source_handle &&
               instance_generation->uploadSourceContentIdentity() == retained_upload_source_identity &&
               instance_generation->uploadSourceBuffer() == retained_upload_source_buffer &&
               instance_generation->uploadSourceMemory() == retained_upload_source_memory &&
               instance_generation->uploadSourceByteCount() == retained_upload_source_bytes &&
               instance_generation->uploadSourceAllocationSize() == retained_upload_source_allocation &&
               instance_generation->uploadSourceMemoryTypeIndex() == retained_upload_source_memory_type &&
               instance_generation->uploadSourceMemoryPropertyFlags() == retained_upload_source_memory_flags &&
               instance_generation->uploadSourceIsCoherent() == retained_upload_source_coherent;
    };

    LLRenderVulkan::VulkanUploadDestinationRequest upload_destination_request;
    upload_destination_request.mNativeWindowGeneration = NATIVE_WINDOW_GENERATION;
    upload_destination_request.mDescription            = upload_source_description;
    upload_destination_request.mInstanceOwnerCheck     = { &operation_context, frameSlotInstanceOwnerIsCurrent };
    upload_destination_request.mWindowGenerationCheck  = { &operation_context, frameSlotWindowGenerationIsCurrent };

    const auto upload_destination_error = mutable_instance_generation->acquireUploadDestinationGeneration(upload_destination_request);
    ensure("the exact upload source creates one device-local 48-byte vertex destination", !upload_destination_error.has_value());
    ensure("the unpublished destination owns distinct native storage and exact source identity",
           instance_generation->hasUploadDestinationGeneration() &&
               instance_generation->uploadDestinationResourceHandle() == upload_source_description.mHandle &&
               instance_generation->uploadDestinationExpectedContentIdentity() == retained_upload_source_identity &&
               instance_generation->uploadDestinationResidentContentIdentity() == 0 &&
               !instance_generation->uploadDestinationIsResident() && instance_generation->uploadDestinationBuffer() != VK_NULL_HANDLE &&
               instance_generation->uploadDestinationBuffer() != retained_upload_source_buffer &&
               instance_generation->uploadDestinationMemory() != VK_NULL_HANDLE &&
               instance_generation->uploadDestinationMemory() != retained_upload_source_memory &&
               instance_generation->uploadDestinationByteCount() == LLRenderVulkan::VULKAN_UPLOAD_SOURCE_BYTE_COUNT &&
               instance_generation->uploadDestinationUsage() == UPLOAD_DESTINATION_USAGE &&
               instance_generation->uploadDestinationAllocationSize() >= instance_generation->uploadDestinationByteCount() &&
               (instance_generation->uploadDestinationMemoryPropertyFlags() & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0 &&
               instance_generation->uploadDestinationIsDeviceLocal() && !instance_generation->uploadDestinationIsMapped());
    ensure_equals("upload-destination acquisition emits no validation messages",
                  instance_generation->validationSnapshot().mMessageCount,
                  std::uint32_t{ 0 });
    ensure("upload-destination acquisition creates no current CGL context", CGLGetCurrentContext() == initial_cgl_context);
    ensure_equals("upload-destination acquisition leaves the OpenGL manager unchanged", gGLManager.mInited, initial_gl_manager);

    LLRenderVulkan::VulkanUploadTransferRequest upload_transfer_request;
    upload_transfer_request.mNativeWindowGeneration = NATIVE_WINDOW_GENERATION;
    upload_transfer_request.mDescription            = upload_source_description;
    upload_transfer_request.mInstanceOwnerCheck     = { &operation_context, frameSlotInstanceOwnerIsCurrent };
    upload_transfer_request.mWindowGenerationCheck  = { &operation_context, frameSlotWindowGenerationIsCurrent };

    const auto upload_transfer_error = mutable_instance_generation->acquireUploadTransferGeneration(upload_transfer_request);
    ensure("the exact source and destination acquire one ready device-scoped transfer", !upload_transfer_error.has_value());
    ensure("the ready transfer retains exact resource, queue, and synchronization identity",
           instance_generation->hasUploadTransferGeneration() &&
               instance_generation->uploadTransferResourceHandle() == upload_source_description.mHandle &&
               instance_generation->uploadTransferContentIdentity() == retained_upload_source_identity &&
               instance_generation->uploadTransferSourceBuffer() == retained_upload_source_buffer &&
               instance_generation->uploadTransferDestinationBuffer() == instance_generation->uploadDestinationBuffer() &&
               instance_generation->uploadTransferQueue() == instance_generation->presentationQueue() &&
               instance_generation->uploadTransferQueueFamilyIndex() == instance_generation->logicalDeviceQueueFamilyIndex() &&
               instance_generation->uploadTransferQueueIndex() == instance_generation->logicalDeviceQueueIndex() &&
               instance_generation->uploadTransferCommandPool() != VK_NULL_HANDLE &&
               instance_generation->uploadTransferCommandBuffer() != VK_NULL_HANDLE &&
               instance_generation->uploadTransferFence() != VK_NULL_HANDLE && instance_generation->uploadTransferSubmissionCount() == 0 &&
               instance_generation->uploadTransferCompletionWaitCount() == 0 &&
               instance_generation->uploadTransferDisposition() == LLRenderVulkan::VulkanUploadTransferDisposition::Ready);

    LLRenderVulkan::VulkanUploadTransferOperationRequest upload_transfer_operation;
    upload_transfer_operation.mNativeWindowGeneration = NATIVE_WINDOW_GENERATION;
    upload_transfer_operation.mDescription            = upload_source_description;
    upload_transfer_operation.mInstanceOwnerCheck     = { &operation_context, frameSlotInstanceOwnerIsCurrent };
    upload_transfer_operation.mWindowGenerationCheck  = { &operation_context, frameSlotWindowGenerationIsCurrent };

    const auto  upload_transfer_result      = mutable_instance_generation->executeUploadTransfer(upload_transfer_operation);
    const auto* upload_transfer_disposition = std::get_if<LLRenderVulkan::VulkanUploadTransferDisposition>(&upload_transfer_result);
    ensure("the native copy submits once, waits once, and reaches complete fence retirement",
           upload_transfer_disposition && *upload_transfer_disposition == LLRenderVulkan::VulkanUploadTransferDisposition::Complete &&
               instance_generation->uploadTransferDisposition() == LLRenderVulkan::VulkanUploadTransferDisposition::Complete &&
               instance_generation->uploadTransferSubmissionCount() == 1 && instance_generation->uploadTransferCompletionWaitCount() == 1);
    ensure("completed fence retirement publishes the exact source identity on the device-local destination",
           instance_generation->uploadDestinationIsResident() &&
               instance_generation->uploadDestinationExpectedContentIdentity() == retained_upload_source_identity &&
               instance_generation->uploadDestinationResidentContentIdentity() == retained_upload_source_identity &&
               upload_source_retained() && texture_source_retained());

    const auto  completed_retry       = mutable_instance_generation->retryUploadTransferCompletion(upload_transfer_operation);
    const auto* completed_retry_error = std::get_if<LLRenderVulkan::VulkanUploadTransferParentOperationError>(&completed_retry);
    ensure("the complete transfer rejects a pending-only retry without another submit or fence wait",
           completed_retry_error &&
               completed_retry_error->mCode == LLRenderVulkan::VulkanUploadTransferParentOperationCode::OperationFailure &&
               completed_retry_error->mOperationError &&
               completed_retry_error->mOperationError->mCode == LLRenderVulkan::VulkanUploadTransferOperationCode::InvalidDisposition &&
               completed_retry_error->mOperationError->mDisposition == LLRenderVulkan::VulkanUploadTransferDisposition::Complete &&
               instance_generation->uploadTransferSubmissionCount() == 1 && instance_generation->uploadTransferCompletionWaitCount() == 1);
    ensure_equals("upload completion and rejected retry emit no validation messages",
                  instance_generation->validationSnapshot().mMessageCount,
                  std::uint32_t{ 0 });
    ensure("upload completion preserves the private Cocoa owner and exact drawable geometry",
           owner->hasNativeWindow() && owner->requirements() == requirements && owner->instanceGeneration() == instance_generation &&
               owner->isGenerationCurrent(NATIVE_WINDOW_GENERATION) && owner->backingScale() == upload_source_backing_scale &&
               owner->drawableWidth() == BACKING_WIDTH && owner->drawableHeight() == BACKING_HEIGHT &&
               LLWindow::instanceCount() == initial_window_count);
    ensure("upload completion creates no current CGL context", CGLGetCurrentContext() == initial_cgl_context);
    ensure_equals("upload completion leaves the OpenGL manager unchanged", gGLManager.mInited, initial_gl_manager);

    const LLRenderContract::BufferHandle retained_upload_destination_handle = instance_generation->uploadDestinationResourceHandle();
    const std::uint64_t  retained_upload_destination_expected_identity   = instance_generation->uploadDestinationExpectedContentIdentity();
    const std::uint64_t  retained_upload_destination_resident_identity   = instance_generation->uploadDestinationResidentContentIdentity();
    const VkBuffer       retained_upload_destination_buffer              = instance_generation->uploadDestinationBuffer();
    const VkDeviceMemory retained_upload_destination_memory              = instance_generation->uploadDestinationMemory();
    const VkDeviceSize   retained_upload_destination_bytes               = instance_generation->uploadDestinationByteCount();
    const VkBufferUsageFlags    retained_upload_destination_usage        = instance_generation->uploadDestinationUsage();
    const VkDeviceSize          retained_upload_destination_allocation   = instance_generation->uploadDestinationAllocationSize();
    const std::uint32_t         retained_upload_destination_memory_type  = instance_generation->uploadDestinationMemoryTypeIndex();
    const VkMemoryPropertyFlags retained_upload_destination_memory_flags = instance_generation->uploadDestinationMemoryPropertyFlags();
    const auto                  upload_destination_retained              = [&]() noexcept
    {
        return instance_generation->hasUploadDestinationGeneration() &&
               instance_generation->uploadDestinationResourceHandle() == retained_upload_destination_handle &&
               instance_generation->uploadDestinationExpectedContentIdentity() == retained_upload_destination_expected_identity &&
               instance_generation->uploadDestinationResidentContentIdentity() == retained_upload_destination_resident_identity &&
               retained_upload_destination_expected_identity == retained_upload_source_identity &&
               retained_upload_destination_resident_identity == retained_upload_source_identity &&
               instance_generation->uploadDestinationIsResident() &&
               instance_generation->uploadDestinationBuffer() == retained_upload_destination_buffer &&
               instance_generation->uploadDestinationMemory() == retained_upload_destination_memory &&
               instance_generation->uploadDestinationByteCount() == retained_upload_destination_bytes &&
               instance_generation->uploadDestinationUsage() == retained_upload_destination_usage &&
               retained_upload_destination_usage == UPLOAD_DESTINATION_USAGE &&
               instance_generation->uploadDestinationAllocationSize() == retained_upload_destination_allocation &&
               instance_generation->uploadDestinationMemoryTypeIndex() == retained_upload_destination_memory_type &&
               instance_generation->uploadDestinationMemoryPropertyFlags() == retained_upload_destination_memory_flags &&
               instance_generation->uploadDestinationIsDeviceLocal() && !instance_generation->uploadDestinationIsMapped();
    };

    const auto resident_destination_only_retained = [&]() noexcept
    {
        return upload_destination_retained() && !instance_generation->hasUploadSourceGeneration() &&
               !instance_generation->hasUploadTransferGeneration();
    };

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
    constexpr VkImageUsageFlags    expected_image_usage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ensure("the MoltenVK surface admits the exact color-attachment, transfer-destination, and transfer-source usage",
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
               instance_generation->swapchainClipped() == VK_FALSE);
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

    const auto presentation_pipeline_error = owner->acquireSwapchainPresentationPipelineGeneration();
    ensure("the exact presentation target creates one real render-pass-compatible graphics pipeline",
           !presentation_pipeline_error.has_value());
    ensure("the instance parent owns one presentation-pipeline generation",
           instance_generation->hasSwapchainPresentationPipelineGeneration());
    ensure("the real presentation pipeline owns one non-null layout and graphics pipeline",
           instance_generation->swapchainPresentationPipelineLayout() != VK_NULL_HANDLE &&
               instance_generation->swapchainPresentationPipeline() != VK_NULL_HANDLE);
    ensure("the presentation pipeline retains its current Retina backing-pixel chain",
           owner->refreshNativeGeometry() && owner->drawableWidth() == BACKING_WIDTH && owner->drawableHeight() == BACKING_HEIGHT &&
               owner->isGenerationCurrent(NATIVE_WINDOW_GENERATION));
    ensure_equals("presentation-pipeline acquisition emits no validation messages", instance_generation->validationSnapshot().mMessageCount,
                  std::uint32_t{ 0 });
    ensure("presentation-pipeline acquisition creates no current CGL context", CGLGetCurrentContext() == initial_cgl_context);
    ensure_equals("presentation-pipeline acquisition leaves the OpenGL manager unchanged", gGLManager.mInited, initial_gl_manager);

    LLRenderVulkan::VulkanTextureUploadSamplePipelineRequest texture_sample_pipeline_request;
    texture_sample_pipeline_request.mNativeWindowGeneration   = NATIVE_WINDOW_GENERATION;
    texture_sample_pipeline_request.mDrawableExtent           = drawable_extent;
    texture_sample_pipeline_request.mDestinationDescription   = texture_destination_request.mDescription;
    texture_sample_pipeline_request.mSampleBindingDescription = texture_sample_binding_description;
    texture_sample_pipeline_request.mDescription              = texture_sample_pipeline_description;
    texture_sample_pipeline_request.mInstanceOwnerCheck       = { &operation_context, frameSlotInstanceOwnerIsCurrent };
    texture_sample_pipeline_request.mWindowGenerationCheck    = { &operation_context, frameSlotWindowGenerationIsCurrent };
    const auto texture_sample_pipeline_error =
        mutable_instance_generation->acquireTextureUploadSamplePipelineGeneration(texture_sample_pipeline_request);
    ensure("the completed texture upload and current presentation target acquire one native sampled pipeline",
           !texture_sample_pipeline_error.has_value() && instance_generation->hasTextureUploadSamplePipelineGeneration() &&
               instance_generation->textureUploadSamplePipelineResourceHandle() == texture_sample_pipeline_description.mHandle &&
               instance_generation->textureUploadSamplePipelineLayout() == retained_texture_sample_pipeline_layout &&
               instance_generation->textureUploadSamplePipeline() != VK_NULL_HANDLE && texture_sample_binding_retained() &&
               texture_transfer_retained());
    ensure_equals("texture sample-pipeline acquisition emits no validation messages",
                  instance_generation->validationSnapshot().mMessageCount,
                  std::uint32_t{ 0 });
    ensure("texture sample-pipeline acquisition preserves the Cocoa owner and exact backing-pixel geometry",
           owner->hasNativeWindow() && owner->requirements() == requirements && owner->instanceGeneration() == instance_generation &&
               owner->isGenerationCurrent(NATIVE_WINDOW_GENERATION) && owner->backingScale() == texture_source_backing_scale &&
               owner->drawableWidth() == BACKING_WIDTH && owner->drawableHeight() == BACKING_HEIGHT &&
               LLWindow::instanceCount() == initial_window_count);
    ensure("texture sample-pipeline acquisition creates no current CGL context", CGLGetCurrentContext() == initial_cgl_context);
    ensure_equals("texture sample-pipeline acquisition leaves the OpenGL manager unchanged", gGLManager.mInited, initial_gl_manager);

    const VkPipeline initial_texture_sample_pipeline  = instance_generation->textureUploadSamplePipeline();
    const auto       texture_sample_pipeline_retained = [&](VkPipeline pipeline) noexcept
    {
        return texture_sample_binding_retained() && instance_generation->hasTextureUploadSamplePipelineGeneration() &&
               instance_generation->textureUploadSamplePipelineResourceHandle() == texture_sample_pipeline_description.mHandle &&
               instance_generation->textureUploadSamplePipelineLayout() == retained_texture_sample_pipeline_layout &&
               instance_generation->textureUploadSamplePipeline() == pipeline && pipeline != VK_NULL_HANDLE;
    };
    ensure("the native sampled pipeline retains its exact binding layout before any frame operation",
           texture_sample_pipeline_retained(initial_texture_sample_pipeline));

    const auto readback_error = owner->acquireSwapchainReadbackGeneration();
    ensure("the exact swapchain-image chain creates one coherent mapped readback destination", !readback_error.has_value());
    ensure("the instance parent owns one readback generation with exact swapchain-image metadata",
           instance_generation->hasSwapchainReadbackGeneration() && instance_generation->swapchainReadbackBuffer() != VK_NULL_HANDLE &&
               instance_generation->swapchainReadbackMemory() != VK_NULL_HANDLE &&
               instance_generation->swapchainReadbackIsMapped() &&
               instance_generation->swapchainReadbackImageFormat() == surface_format.format &&
               instance_generation->swapchainReadbackImageExtent().width == image_extent.width &&
               instance_generation->swapchainReadbackImageExtent().height == image_extent.height &&
               instance_generation->swapchainReadbackImageCount() == resolved_image_count &&
               instance_generation->swapchainReadbackRowBytes() == static_cast<VkDeviceSize>(image_extent.width) * 4 &&
               instance_generation->swapchainReadbackByteCount() ==
                   static_cast<VkDeviceSize>(image_extent.width) * image_extent.height * 4 &&
               (instance_generation->swapchainReadbackMemoryPropertyFlags() &
                (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                   (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));
    ensure_equals("readback acquisition emits no validation messages", instance_generation->validationSnapshot().mMessageCount,
                  std::uint32_t{ 0 });
    ensure("readback acquisition creates no current CGL context", CGLGetCurrentContext() == initial_cgl_context);
    ensure_equals("readback acquisition leaves the OpenGL manager unchanged", gGLManager.mInited, initial_gl_manager);

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

    LLRenderVulkan::VulkanSwapchainFrameSlotOperationRequest operation_request;
    operation_request.mNativeWindowGeneration = NATIVE_WINDOW_GENERATION;
    operation_request.mDrawableExtent         = drawable_extent;
    operation_request.mInstanceOwnerCheck     = { &operation_context, frameSlotInstanceOwnerIsCurrent };
    operation_request.mWindowGenerationCheck  = { &operation_context, frameSlotWindowGenerationIsCurrent };

    const std::uint32_t initial_resolved_image_count = resolved_image_count;
    const VkFormat      initial_readback_format      = instance_generation->swapchainReadbackImageFormat();
    const VkExtent2D    initial_readback_extent      = instance_generation->swapchainReadbackImageExtent();

    const VkSemaphore initial_image_available          = instance_generation->swapchainFrameImageAvailableSemaphore();
    const VkSemaphore initial_presentation_ready       = instance_generation->swapchainFramePresentationReadySemaphore();
    const VkFence     initial_submission_fence         = instance_generation->swapchainFrameSubmissionFence();
    const VkFence     initial_present_completion_fence = instance_generation->swapchainFramePresentCompletionFence();
    const auto        initial_first_presentation       = owner->acquireClearToPresentSwapchainFrameSlot(INITIAL_CLEAR_ONE);
    ensure("the initial Metal swapchain submits and presents one legacy transfer-clear cycle",
           presentationCompleted(initial_first_presentation, resolved_image_count) &&
               instance_generation->swapchainFrameSlotDisposition() == LLRenderVulkan::VulkanSwapchainFrameSlotDisposition::Reusable &&
               !instance_generation->swapchainFrameAcquiredImageIndex());
    ensure("the initial transfer-clear cycle retains all four synchronization handles",
           initial_image_available != VK_NULL_HANDLE && initial_presentation_ready != VK_NULL_HANDLE &&
               initial_submission_fence != VK_NULL_HANDLE && initial_present_completion_fence != VK_NULL_HANDLE &&
               instance_generation->swapchainFrameImageAvailableSemaphore() == initial_image_available &&
               instance_generation->swapchainFramePresentationReadySemaphore() == initial_presentation_ready &&
               instance_generation->swapchainFrameSubmissionFence() == initial_submission_fence &&
               instance_generation->swapchainFramePresentCompletionFence() == initial_present_completion_fence);
    ensure("the initial transfer-clear cycle creates no current CGL context", CGLGetCurrentContext() == initial_cgl_context);
    ensure_equals("the initial transfer-clear cycle leaves the OpenGL manager unchanged", gGLManager.mInited, initial_gl_manager);

    const auto initial_second_presentation = owner->acquireRenderPassClearToPresentSwapchainFrameSlot(INITIAL_CLEAR_TWO);
    ensure("the initial Metal swapchain submits and presents one distinctive render-pass clear cycle",
           presentationCompleted(initial_second_presentation, resolved_image_count) &&
               instance_generation->swapchainFrameSlotDisposition() == LLRenderVulkan::VulkanSwapchainFrameSlotDisposition::Reusable &&
               !instance_generation->swapchainFrameAcquiredImageIndex());
    ensure("the initial render-pass clear cycle retains all four synchronization handles",
           instance_generation->swapchainFrameImageAvailableSemaphore() == initial_image_available &&
               instance_generation->swapchainFramePresentationReadySemaphore() == initial_presentation_ready &&
               instance_generation->swapchainFrameSubmissionFence() == initial_submission_fence &&
               instance_generation->swapchainFramePresentCompletionFence() == initial_present_completion_fence);
    ensure_equals("the initial transfer and render-pass clear cycles emit no validation messages",
                  instance_generation->validationSnapshot().mMessageCount, std::uint32_t{ 0 });
    ensure("the initial render-pass clear cycle creates no current CGL context", CGLGetCurrentContext() == initial_cgl_context);
    ensure_equals("the initial render-pass clear cycle leaves the OpenGL manager unchanged", gGLManager.mInited, initial_gl_manager);

    const F64 initial_draw_backing_scale = owner->backingScale();
    ensure("the native smoke retires the upload source and completed transfer before the first observed draw",
           mutable_instance_generation->resetUploadSourceGeneration());
    ensure("pre-draw upload-source retirement removes its owner, identity, buffer, memory, and allocation metadata",
           !instance_generation->hasUploadSourceGeneration() &&
               instance_generation->uploadSourceResourceHandle() == LLRenderContract::BufferHandle{} &&
               instance_generation->uploadSourceContentIdentity() == 0 && instance_generation->uploadSourceBuffer() == VK_NULL_HANDLE &&
               instance_generation->uploadSourceMemory() == VK_NULL_HANDLE && instance_generation->uploadSourceByteCount() == 0 &&
               instance_generation->uploadSourceAllocationSize() == 0 && instance_generation->uploadSourceMemoryTypeIndex() == 0 &&
               instance_generation->uploadSourceMemoryPropertyFlags() == 0 && !instance_generation->uploadSourceIsCoherent());
    ensure("pre-draw source retirement removes the terminal transfer but preserves the exact resident destination",
           !instance_generation->hasUploadTransferGeneration() &&
               instance_generation->uploadTransferResourceHandle() == LLRenderContract::BufferHandle{} &&
               instance_generation->uploadTransferContentIdentity() == 0 &&
               instance_generation->uploadTransferSourceBuffer() == VK_NULL_HANDLE &&
               instance_generation->uploadTransferDestinationBuffer() == VK_NULL_HANDLE &&
               instance_generation->uploadTransferQueue() == VK_NULL_HANDLE &&
               instance_generation->uploadTransferQueueFamilyIndex() == VK_QUEUE_FAMILY_IGNORED &&
               instance_generation->uploadTransferQueueIndex() == std::numeric_limits<std::uint32_t>::max() &&
               instance_generation->uploadTransferCommandPool() == VK_NULL_HANDLE &&
               instance_generation->uploadTransferCommandBuffer() == VK_NULL_HANDLE &&
               instance_generation->uploadTransferFence() == VK_NULL_HANDLE && instance_generation->uploadTransferSubmissionCount() == 0 &&
               instance_generation->uploadTransferCompletionWaitCount() == 0 && !instance_generation->uploadTransferDisposition() &&
               resident_destination_only_retained() && texture_source_retained());
    ensure("pre-draw upload retirement leaves the complete presentation and observation chain live",
           instance_generation->hasSurfaceGeneration() && instance_generation->surface() != VK_NULL_HANDLE &&
               instance_generation->hasPresentationDeviceGeneration() && instance_generation->physicalDevice() != VK_NULL_HANDLE &&
               instance_generation->hasLogicalDeviceGeneration() && instance_generation->logicalDevice() != VK_NULL_HANDLE &&
               instance_generation->hasSwapchainConfigurationGeneration() && instance_generation->hasSwapchainGeneration() &&
               instance_generation->swapchain() != VK_NULL_HANDLE && instance_generation->hasSwapchainImagesGeneration() &&
               instance_generation->resolvedSwapchainImageCount() == resolved_image_count &&
               instance_generation->hasSwapchainPresentationTargetGeneration() &&
               instance_generation->swapchainPresentationRenderPass() != VK_NULL_HANDLE &&
               instance_generation->hasSwapchainPresentationPipelineGeneration() &&
               instance_generation->swapchainPresentationPipeline() != VK_NULL_HANDLE &&
               instance_generation->hasSwapchainReadbackGeneration() && instance_generation->swapchainReadbackBuffer() != VK_NULL_HANDLE &&
               instance_generation->hasSwapchainFrameSlotGeneration() &&
               instance_generation->swapchainFrameSlotDisposition() == LLRenderVulkan::VulkanSwapchainFrameSlotDisposition::Reusable);
    ensure_equals("pre-draw upload-source and terminal-transfer destruction emit no validation messages",
                  instance_generation->validationSnapshot().mMessageCount,
                  std::uint32_t{ 0 });
    ensure("pre-draw upload retirement preserves the private Cocoa owner and exact initial geometry",
           owner->hasNativeWindow() && owner->requirements() == requirements && owner->instanceGeneration() == instance_generation &&
               owner->isGenerationCurrent(NATIVE_WINDOW_GENERATION) && owner->backingScale() == initial_draw_backing_scale &&
               owner->drawableWidth() == BACKING_WIDTH && owner->drawableHeight() == BACKING_HEIGHT &&
               LLWindow::instanceCount() == initial_window_count);
    ensure("pre-draw upload retirement creates no current CGL context", CGLGetCurrentContext() == initial_cgl_context);
    ensure_equals("pre-draw upload retirement leaves the OpenGL manager unchanged", gGLManager.mInited, initial_gl_manager);

    const auto initial_draw_readback_presentation =
        mutable_instance_generation->acquireRenderPassDrawReadbackToPresentSwapchainFrameSlot(operation_request);
    ensure("the complete initial target-pipeline-slot chain authenticates one detached green draw-readback observation",
           image_extent.width == BACKING_WIDTH && image_extent.height == BACKING_HEIGHT &&
               presentationObserved(initial_draw_readback_presentation,
                                    initial_resolved_image_count,
                                    surface_format.format,
                                    image_extent,
                                    initial_readback_format,
                                    initial_readback_extent) &&
               instance_generation->swapchainFrameSlotDisposition() == LLRenderVulkan::VulkanSwapchainFrameSlotDisposition::Reusable &&
               !instance_generation->swapchainFrameAcquiredImageIndex());
    ensure("the initial observed draw retains all four synchronization handles",
           instance_generation->swapchainFrameImageAvailableSemaphore() == initial_image_available &&
               instance_generation->swapchainFramePresentationReadySemaphore() == initial_presentation_ready &&
               instance_generation->swapchainFrameSubmissionFence() == initial_submission_fence &&
               instance_generation->swapchainFramePresentCompletionFence() == initial_present_completion_fence);
    ensure("the initial observed draw preserves the exact resident vertex destination and independent texture source",
           resident_destination_only_retained() && texture_source_retained());
    ensure_equals("the initial observed draw emits no validation messages", instance_generation->validationSnapshot().mMessageCount,
                  std::uint32_t{ 0 });
    ensure("the initial observed draw preserves the private Cocoa owner and exact Vulkan generation",
           owner->hasNativeWindow() && owner->requirements() == requirements && owner->instanceGeneration() == instance_generation &&
               owner->isGenerationCurrent(NATIVE_WINDOW_GENERATION) && owner->backingScale() == initial_draw_backing_scale &&
               owner->drawableWidth() == BACKING_WIDTH && owner->drawableHeight() == BACKING_HEIGHT &&
               LLWindow::instanceCount() == initial_window_count);
    ensure("the initial observed draw creates no current CGL context", CGLGetCurrentContext() == initial_cgl_context);
    ensure_equals("the initial observed draw leaves the OpenGL manager unchanged", gGLManager.mInited, initial_gl_manager);

    ensure("the native smoke directly resets the completed texture transfer while the initial sampled pipeline remains live",
           mutable_instance_generation->resetTextureUploadTransferGeneration() &&
               !instance_generation->hasTextureUploadTransferGeneration() && texture_source_retained() &&
               instance_generation->textureUploadDestinationIsResident() &&
               instance_generation->textureUploadDestinationResidentRevision() == retained_texture_revision &&
               instance_generation->textureUploadDestinationResidentContentIdentity() == retained_texture_source_identity &&
               instance_generation->textureUploadDestinationCurrentState() == LLRenderContract::ImageState::ShaderRead &&
               texture_sample_pipeline_retained(initial_texture_sample_pipeline));
    ensure_equals("texture-transfer reset with a live sampled pipeline emits no validation messages",
                  instance_generation->validationSnapshot().mMessageCount,
                  std::uint32_t{ 0 });
    ensure("the native smoke directly resets the texture source while the initial sampled pipeline remains live",
           mutable_instance_generation->resetTextureUploadSourceGeneration() && !instance_generation->hasTextureUploadSourceGeneration() &&
               instance_generation->textureUploadSourceResourceHandle() == LLRenderContract::ImageHandle{} &&
               instance_generation->textureUploadSourceExpectedRevision() == 0 &&
               instance_generation->textureUploadSourceResidentExtent().mWidth == 0 &&
               instance_generation->textureUploadSourceResidentExtent().mHeight == 0 &&
               instance_generation->textureUploadSourceRowPitch() == 0 && instance_generation->textureUploadSourceContentIdentity() == 0 &&
               instance_generation->textureUploadSourceBuffer() == VK_NULL_HANDLE &&
               instance_generation->textureUploadSourceMemory() == VK_NULL_HANDLE &&
               instance_generation->textureUploadSourceByteCount() == 0 && instance_generation->textureUploadSourceAllocationSize() == 0 &&
               instance_generation->textureUploadSourceMemoryTypeIndex() == 0 &&
               instance_generation->textureUploadSourceMemoryPropertyFlags() == 0 &&
               !instance_generation->textureUploadSourceIsCoherent() && texture_destination_retained() &&
               texture_sample_pipeline_retained(initial_texture_sample_pipeline) &&
               instance_generation->hasSwapchainConfigurationGeneration() && instance_generation->hasSwapchainGeneration() &&
               instance_generation->hasSwapchainImagesGeneration() && instance_generation->hasSwapchainPresentationTargetGeneration() &&
               instance_generation->hasSwapchainPresentationPipelineGeneration() && instance_generation->hasSwapchainReadbackGeneration() &&
               instance_generation->hasSwapchainFrameSlotGeneration());
    ensure_equals("texture-source reset with a live sampled pipeline emits no validation messages",
                  instance_generation->validationSnapshot().mMessageCount,
                  std::uint32_t{ 0 });
    ensure("texture source and transfer resets preserve the private Cocoa owner and exact initial backing-pixel geometry",
           owner->hasNativeWindow() && owner->requirements() == requirements && owner->instanceGeneration() == instance_generation &&
               owner->isGenerationCurrent(NATIVE_WINDOW_GENERATION) && owner->backingScale() == texture_source_backing_scale &&
               owner->drawableWidth() == BACKING_WIDTH && owner->drawableHeight() == BACKING_HEIGHT &&
               LLWindow::instanceCount() == initial_window_count);
    ensure("texture source and transfer resets create no current CGL context", CGLGetCurrentContext() == initial_cgl_context);
    ensure_equals("texture source and transfer resets leave the OpenGL manager unchanged", gGLManager.mInited, initial_gl_manager);

    const VkSurfaceKHR     retained_surface         = instance_generation->surface();
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
               instance_generation->hasSwapchainImagesGeneration() && instance_generation->hasSwapchainPresentationTargetGeneration() &&
               instance_generation->hasSwapchainPresentationPipelineGeneration() && instance_generation->hasSwapchainReadbackGeneration() &&
               instance_generation->hasSwapchainFrameSlotGeneration() && !instance_generation->hasTextureUploadSamplePipelineGeneration());
    const VkExtent2D rebuilt_drawable_extent = instance_generation->swapchainDrawableExtent();
    ensure("the rebuilt configuration authenticates the changed Cocoa backing extent",
           rebuilt_drawable_extent.width == REBUILT_BACKING_WIDTH && rebuilt_drawable_extent.height == REBUILT_BACKING_HEIGHT &&
               owner->drawableWidth() == rebuilt_drawable_extent.width && owner->drawableHeight() == rebuilt_drawable_extent.height &&
               instance_generation->swapchainImageUsage() == expected_image_usage);
    operation_request.mDrawableExtent = rebuilt_drawable_extent;
    ensure("same-surface rebuild retains every older Vulkan parent and borrowed queue",
           instance_generation->surface() == retained_surface && instance_generation->physicalDevice() == retained_physical_device &&
               instance_generation->logicalDevice() == retained_logical_device &&
               instance_generation->presentationQueue() == retained_queue);
    ensure("same-surface rebuild retains the exact resident upload destination after source and transfer retirement",
           resident_destination_only_retained());
    resolved_image_count = instance_generation->resolvedSwapchainImageCount();
    ensure("the rebuilt MoltenVK swapchain publishes a complete nonempty image and frame-slot chain",
           resolved_image_count != 0 && instance_generation->swapchain() != VK_NULL_HANDLE &&
               instance_generation->swapchainImage(0) != VK_NULL_HANDLE &&
               instance_generation->swapchainImageView(0) != VK_NULL_HANDLE &&
               instance_generation->swapchainPresentationRenderPass() != VK_NULL_HANDLE &&
               instance_generation->swapchainPresentationFramebufferCount() == resolved_image_count &&
               instance_generation->swapchainPresentationFramebuffer(0) != VK_NULL_HANDLE &&
               instance_generation->swapchainPresentationPipelineLayout() != VK_NULL_HANDLE &&
               instance_generation->swapchainPresentationPipeline() != VK_NULL_HANDLE &&
               instance_generation->swapchainReadbackBuffer() != VK_NULL_HANDLE &&
               instance_generation->swapchainReadbackMemory() != VK_NULL_HANDLE &&
               instance_generation->swapchainReadbackIsMapped() &&
               instance_generation->swapchainFrameCommandBuffer() != VK_NULL_HANDLE &&
               instance_generation->swapchainFrameImageAvailableSemaphore() != VK_NULL_HANDLE &&
               instance_generation->swapchainFramePresentationReadySemaphore() != VK_NULL_HANDLE &&
               instance_generation->swapchainFrameSubmissionFence() != VK_NULL_HANDLE &&
               instance_generation->swapchainFramePresentCompletionFence() != VK_NULL_HANDLE);
    const VkExtent2D rebuilt_image_extent = instance_generation->swapchainImageExtent();
    ensure("the rebuilt readback destination follows the changed swapchain image extent",
           instance_generation->swapchainReadbackImageFormat() == instance_generation->swapchainSurfaceFormat().format &&
               instance_generation->swapchainReadbackImageExtent().width == rebuilt_image_extent.width &&
               instance_generation->swapchainReadbackImageExtent().height == rebuilt_image_extent.height &&
               instance_generation->swapchainReadbackImageCount() == resolved_image_count &&
               instance_generation->swapchainReadbackRowBytes() == static_cast<VkDeviceSize>(rebuilt_image_extent.width) * 4 &&
               instance_generation->swapchainReadbackByteCount() ==
                   static_cast<VkDeviceSize>(rebuilt_image_extent.width) * rebuilt_image_extent.height * 4 &&
               (instance_generation->swapchainReadbackMemoryPropertyFlags() &
                (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                   (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));
    ensure("the initial draw-readback observation remains detached after changed-extent rebuild",
           presentationObserved(initial_draw_readback_presentation,
                                initial_resolved_image_count,
                                surface_format.format,
                                image_extent,
                                initial_readback_format,
                                initial_readback_extent));
    ensure_equals("changed-extent rebuild emits no validation messages", instance_generation->validationSnapshot().mMessageCount,
                  std::uint32_t{ 0 });
    ensure("same-surface rebuild preserves the published destination after texture source and transfer retirement",
           !instance_generation->hasTextureUploadTransferGeneration() && !instance_generation->hasTextureUploadSourceGeneration() &&
               texture_destination_retained());
    ensure("same-surface rebuild retires the target-bound sampled pipeline but preserves its exact binding",
           !instance_generation->hasTextureUploadSamplePipelineGeneration() &&
               instance_generation->textureUploadSamplePipelineResourceHandle() == LLRenderContract::PipelineHandle{} &&
               instance_generation->textureUploadSamplePipelineLayout() == VK_NULL_HANDLE &&
               instance_generation->textureUploadSamplePipeline() == VK_NULL_HANDLE && texture_sample_binding_retained());

    texture_sample_pipeline_request.mDrawableExtent = rebuilt_drawable_extent;
    const auto rebuilt_texture_sample_pipeline_error =
        mutable_instance_generation->acquireTextureUploadSamplePipelineGeneration(texture_sample_pipeline_request);
    const VkPipeline rebuilt_texture_sample_pipeline = instance_generation->textureUploadSamplePipeline();
    ensure("the preserved sampled binding explicitly acquires a pipeline for the rebuilt presentation target",
           !rebuilt_texture_sample_pipeline_error.has_value() && texture_sample_pipeline_retained(rebuilt_texture_sample_pipeline) &&
               !instance_generation->hasTextureUploadTransferGeneration() && !instance_generation->hasTextureUploadSourceGeneration());
    ensure_equals("rebuilt texture sample-pipeline acquisition emits no validation messages",
                  instance_generation->validationSnapshot().mMessageCount,
                  std::uint32_t{ 0 });
    ensure("rebuilt texture sample-pipeline acquisition preserves the Cocoa owner and changed backing-pixel geometry",
           owner->hasNativeWindow() && owner->requirements() == requirements && owner->instanceGeneration() == instance_generation &&
               owner->isGenerationCurrent(NATIVE_WINDOW_GENERATION) && owner->backingScale() == texture_source_backing_scale &&
               owner->drawableWidth() == REBUILT_BACKING_WIDTH && owner->drawableHeight() == REBUILT_BACKING_HEIGHT &&
               LLWindow::instanceCount() == initial_window_count);
    ensure("rebuilt texture sample-pipeline acquisition creates no current CGL context", CGLGetCurrentContext() == initial_cgl_context);
    ensure_equals("rebuilt texture sample-pipeline acquisition leaves the OpenGL manager unchanged",
                  gGLManager.mInited,
                  initial_gl_manager);
    ensure("the native smoke directly resets the sampled pipeline before its borrowed binding and presentation target",
           mutable_instance_generation->resetTextureUploadSamplePipelineGeneration() &&
               !instance_generation->hasTextureUploadSamplePipelineGeneration() &&
               instance_generation->textureUploadSamplePipelineResourceHandle() == LLRenderContract::PipelineHandle{} &&
               instance_generation->textureUploadSamplePipelineLayout() == VK_NULL_HANDLE &&
               instance_generation->textureUploadSamplePipeline() == VK_NULL_HANDLE && texture_sample_binding_retained() &&
               instance_generation->hasSwapchainPresentationTargetGeneration() &&
               instance_generation->swapchainPresentationRenderPass() != VK_NULL_HANDLE);
    ensure_equals("texture sample-pipeline destruction emits no validation messages",
                  instance_generation->validationSnapshot().mMessageCount,
                  std::uint32_t{ 0 });
    ensure("texture sample-pipeline reset preserves the private Cocoa owner and changed backing-pixel geometry",
           owner->hasNativeWindow() && owner->requirements() == requirements && owner->instanceGeneration() == instance_generation &&
               owner->isGenerationCurrent(NATIVE_WINDOW_GENERATION) && owner->backingScale() == texture_source_backing_scale &&
               owner->drawableWidth() == REBUILT_BACKING_WIDTH && owner->drawableHeight() == REBUILT_BACKING_HEIGHT &&
               LLWindow::instanceCount() == initial_window_count);
    ensure("texture sample-pipeline reset creates no current CGL context", CGLGetCurrentContext() == initial_cgl_context);
    ensure_equals("texture sample-pipeline reset leaves the OpenGL manager unchanged", gGLManager.mInited, initial_gl_manager);
    ensure("the native smoke directly resets the texture sample binding before its resident destination",
           mutable_instance_generation->resetTextureUploadSampleBindingGeneration() &&
               !instance_generation->hasTextureUploadSampleBindingGeneration() &&
               instance_generation->textureUploadSampleBindingSamplerResourceHandle() == LLRenderContract::SamplerHandle{} &&
               instance_generation->textureUploadSampleBindingDestinationResourceHandle() == LLRenderContract::ImageHandle{} &&
               instance_generation->textureUploadSampleBindingExpectedRevision() == 0 &&
               instance_generation->textureUploadSampleBindingResidentRevision() == 0 &&
               instance_generation->textureUploadSampleBindingResidentContentIdentity() == 0 &&
               instance_generation->textureUploadSampleBindingDestinationImageView() == VK_NULL_HANDLE &&
               instance_generation->textureUploadSampleBindingDestinationImageLayout() == VK_IMAGE_LAYOUT_MAX_ENUM &&
               instance_generation->textureUploadSampleBindingDescriptorSetIndex() == std::numeric_limits<std::uint32_t>::max() &&
               instance_generation->textureUploadSampleBindingBinding() == std::numeric_limits<std::uint32_t>::max() &&
               instance_generation->textureUploadSampleBindingSampler() == VK_NULL_HANDLE &&
               instance_generation->textureUploadSampleBindingDescriptorSetLayout() == VK_NULL_HANDLE &&
               instance_generation->textureUploadSampleBindingPipelineLayout() == VK_NULL_HANDLE &&
               instance_generation->textureUploadSampleBindingDescriptorPool() == VK_NULL_HANDLE &&
               instance_generation->textureUploadSampleBindingDescriptorSet() == VK_NULL_HANDLE && texture_destination_retained() &&
               instance_generation->hasSwapchainConfigurationGeneration() && instance_generation->hasSwapchainGeneration() &&
               instance_generation->hasSwapchainImagesGeneration() && instance_generation->hasSwapchainPresentationTargetGeneration() &&
               instance_generation->hasSwapchainPresentationPipelineGeneration() && instance_generation->hasSwapchainReadbackGeneration() &&
               instance_generation->hasSwapchainFrameSlotGeneration());
    ensure_equals("texture sample-binding destruction emits no validation messages",
                  instance_generation->validationSnapshot().mMessageCount,
                  std::uint32_t{ 0 });
    ensure("texture sample-binding reset creates no current CGL context", CGLGetCurrentContext() == initial_cgl_context);
    ensure_equals("texture sample-binding reset leaves the OpenGL manager unchanged", gGLManager.mInited, initial_gl_manager);
    ensure("the native smoke directly resets the texture destination while all swapchain parents remain live",
           mutable_instance_generation->resetTextureUploadDestinationGeneration() &&
               !instance_generation->hasTextureUploadDestinationGeneration() &&
               instance_generation->textureUploadDestinationImage() == VK_NULL_HANDLE &&
               instance_generation->textureUploadDestinationMemory() == VK_NULL_HANDLE &&
               instance_generation->textureUploadDestinationImageView() == VK_NULL_HANDLE &&
               instance_generation->hasSwapchainConfigurationGeneration() && instance_generation->hasSwapchainGeneration() &&
               instance_generation->hasSwapchainImagesGeneration() && instance_generation->hasSwapchainPresentationTargetGeneration() &&
               instance_generation->hasSwapchainPresentationPipelineGeneration() && instance_generation->hasSwapchainReadbackGeneration() &&
               instance_generation->hasSwapchainFrameSlotGeneration());
    ensure_equals("texture-destination rebuild retention and direct reset emit no validation messages",
                  instance_generation->validationSnapshot().mMessageCount, std::uint32_t{ 0 });
    ensure("changed-extent rebuild creates no current CGL context", CGLGetCurrentContext() == initial_cgl_context);
    ensure_equals("changed-extent rebuild leaves the OpenGL manager unchanged", gGLManager.mInited, initial_gl_manager);

    const VkSemaphore image_available          = instance_generation->swapchainFrameImageAvailableSemaphore();
    const VkSemaphore presentation_ready       = instance_generation->swapchainFramePresentationReadySemaphore();
    const VkFence     submission_fence         = instance_generation->swapchainFrameSubmissionFence();
    const VkFence     present_completion_fence = instance_generation->swapchainFramePresentCompletionFence();
    const auto        first_presentation       = owner->acquireClearToPresentSwapchainFrameSlot(REBUILT_CLEAR_ONE);
    ensure("the rebuilt Metal swapchain submits and presents one legacy transfer-clear cycle",
           presentationCompleted(first_presentation, resolved_image_count) &&
               instance_generation->swapchainFrameSlotDisposition() == LLRenderVulkan::VulkanSwapchainFrameSlotDisposition::Reusable &&
               !instance_generation->swapchainFrameAcquiredImageIndex());
    ensure("the rebuilt transfer-clear cycle retains all four synchronization handles",
           image_available != VK_NULL_HANDLE && presentation_ready != VK_NULL_HANDLE && submission_fence != VK_NULL_HANDLE &&
               present_completion_fence != VK_NULL_HANDLE &&
               instance_generation->swapchainFrameImageAvailableSemaphore() == image_available &&
               instance_generation->swapchainFramePresentationReadySemaphore() == presentation_ready &&
               instance_generation->swapchainFrameSubmissionFence() == submission_fence &&
               instance_generation->swapchainFramePresentCompletionFence() == present_completion_fence);
    ensure("the rebuilt transfer-clear cycle creates no current CGL context", CGLGetCurrentContext() == initial_cgl_context);
    ensure_equals("the rebuilt transfer-clear cycle leaves the OpenGL manager unchanged", gGLManager.mInited, initial_gl_manager);

    const auto second_presentation = owner->acquireRenderPassClearToPresentSwapchainFrameSlot(REBUILT_CLEAR_TWO);
    ensure("the rebuilt Metal swapchain submits and presents one distinctive render-pass clear cycle",
           presentationCompleted(second_presentation, resolved_image_count) &&
               instance_generation->swapchainFrameSlotDisposition() == LLRenderVulkan::VulkanSwapchainFrameSlotDisposition::Reusable &&
               !instance_generation->swapchainFrameAcquiredImageIndex());
    ensure("the rebuilt render-pass clear cycle retains all four synchronization handles",
           instance_generation->swapchainFrameImageAvailableSemaphore() == image_available &&
               instance_generation->swapchainFramePresentationReadySemaphore() == presentation_ready &&
               instance_generation->swapchainFrameSubmissionFence() == submission_fence &&
               instance_generation->swapchainFramePresentCompletionFence() == present_completion_fence);
    ensure_equals("the rebuilt transfer and render-pass clear cycles emit no validation messages",
                  instance_generation->validationSnapshot().mMessageCount, std::uint32_t{ 0 });
    ensure("the rebuilt render-pass clear cycle creates no current CGL context", CGLGetCurrentContext() == initial_cgl_context);
    ensure_equals("the rebuilt render-pass clear cycle leaves the OpenGL manager unchanged", gGLManager.mInited, initial_gl_manager);

    const F64  rebuilt_draw_backing_scale = owner->backingScale();
    const auto rebuilt_draw_readback_presentation =
        mutable_instance_generation->acquireRenderPassDrawReadbackToPresentSwapchainFrameSlot(operation_request);
    ensure("the rebuilt target-pipeline-slot chain authenticates one green draw-readback observation at the changed extent",
           rebuilt_image_extent.width == REBUILT_BACKING_WIDTH && rebuilt_image_extent.height == REBUILT_BACKING_HEIGHT &&
               presentationObserved(rebuilt_draw_readback_presentation,
                                    resolved_image_count,
                                    instance_generation->swapchainSurfaceFormat().format,
                                    rebuilt_image_extent,
                                    instance_generation->swapchainReadbackImageFormat(),
                                    instance_generation->swapchainReadbackImageExtent()) &&
               instance_generation->swapchainFrameSlotDisposition() == LLRenderVulkan::VulkanSwapchainFrameSlotDisposition::Reusable &&
               !instance_generation->swapchainFrameAcquiredImageIndex());
    ensure("the rebuilt observed draw retains all four synchronization handles",
           instance_generation->swapchainFrameImageAvailableSemaphore() == image_available &&
               instance_generation->swapchainFramePresentationReadySemaphore() == presentation_ready &&
               instance_generation->swapchainFrameSubmissionFence() == submission_fence &&
               instance_generation->swapchainFramePresentCompletionFence() == present_completion_fence);
    ensure("the rebuilt observed draw uses and preserves the same exact resident upload destination", resident_destination_only_retained());
    ensure_equals("the rebuilt observed draw emits no validation messages", instance_generation->validationSnapshot().mMessageCount,
                  std::uint32_t{ 0 });
    ensure("the rebuilt observed draw preserves the private Cocoa owner and exact changed geometry",
           owner->hasNativeWindow() && owner->requirements() == requirements && owner->instanceGeneration() == instance_generation &&
               owner->isGenerationCurrent(NATIVE_WINDOW_GENERATION) && owner->backingScale() == rebuilt_draw_backing_scale &&
               owner->drawableWidth() == REBUILT_BACKING_WIDTH && owner->drawableHeight() == REBUILT_BACKING_HEIGHT &&
               LLWindow::instanceCount() == initial_window_count);
    ensure("the rebuilt observed draw creates no current CGL context", CGLGetCurrentContext() == initial_cgl_context);
    ensure_equals("the rebuilt observed draw leaves the OpenGL manager unchanged", gGLManager.mInited, initial_gl_manager);

    ensure("the native smoke resets the resident upload destination while the swapchain child chain is live",
           mutable_instance_generation->resetUploadDestinationGeneration());
    ensure("explicit upload-destination reset removes its owner, identities, usage, storage, and allocation metadata",
           !instance_generation->hasUploadDestinationGeneration() &&
               instance_generation->uploadDestinationResourceHandle() == LLRenderContract::BufferHandle{} &&
               instance_generation->uploadDestinationExpectedContentIdentity() == 0 &&
               instance_generation->uploadDestinationResidentContentIdentity() == 0 &&
               !instance_generation->uploadDestinationIsResident() && instance_generation->uploadDestinationBuffer() == VK_NULL_HANDLE &&
               instance_generation->uploadDestinationMemory() == VK_NULL_HANDLE && instance_generation->uploadDestinationByteCount() == 0 &&
               instance_generation->uploadDestinationUsage() == 0 && instance_generation->uploadDestinationAllocationSize() == 0 &&
               instance_generation->uploadDestinationMemoryTypeIndex() == 0 &&
               instance_generation->uploadDestinationMemoryPropertyFlags() == 0 && !instance_generation->uploadDestinationIsDeviceLocal() &&
               !instance_generation->uploadDestinationIsMapped());
    ensure("upload-destination reset leaves the complete presentation and observation chain live",
           instance_generation->hasSurfaceGeneration() && instance_generation->surface() != VK_NULL_HANDLE &&
               instance_generation->hasPresentationDeviceGeneration() && instance_generation->physicalDevice() != VK_NULL_HANDLE &&
               instance_generation->hasLogicalDeviceGeneration() && instance_generation->logicalDevice() != VK_NULL_HANDLE &&
               instance_generation->hasSwapchainConfigurationGeneration() && instance_generation->hasSwapchainGeneration() &&
               instance_generation->swapchain() != VK_NULL_HANDLE && instance_generation->hasSwapchainImagesGeneration() &&
               instance_generation->resolvedSwapchainImageCount() == resolved_image_count &&
               instance_generation->hasSwapchainPresentationTargetGeneration() &&
               instance_generation->swapchainPresentationRenderPass() != VK_NULL_HANDLE &&
               instance_generation->hasSwapchainPresentationPipelineGeneration() &&
               instance_generation->swapchainPresentationPipeline() != VK_NULL_HANDLE &&
               instance_generation->hasSwapchainReadbackGeneration() && instance_generation->swapchainReadbackBuffer() != VK_NULL_HANDLE &&
               instance_generation->hasSwapchainFrameSlotGeneration() &&
               instance_generation->swapchainFrameSlotDisposition() == LLRenderVulkan::VulkanSwapchainFrameSlotDisposition::Reusable);
    ensure_equals("upload-destination destruction emits no validation messages",
                  instance_generation->validationSnapshot().mMessageCount,
                  std::uint32_t{ 0 });
    ensure("upload-destination reset preserves the private Cocoa owner and exact changed geometry",
           owner->hasNativeWindow() && owner->requirements() == requirements && owner->instanceGeneration() == instance_generation &&
               owner->isGenerationCurrent(NATIVE_WINDOW_GENERATION) && owner->backingScale() == rebuilt_draw_backing_scale &&
               owner->drawableWidth() == REBUILT_BACKING_WIDTH && owner->drawableHeight() == REBUILT_BACKING_HEIGHT &&
               LLWindow::instanceCount() == initial_window_count);
    ensure("upload-destination reset creates no current CGL context", CGLGetCurrentContext() == initial_cgl_context);
    ensure_equals("upload-destination reset leaves the OpenGL manager unchanged", gGLManager.mInited, initial_gl_manager);

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
               instance_generation->hasSwapchainReadbackGeneration() && instance_generation->hasSwapchainGeneration() &&
               instance_generation->swapchain() != VK_NULL_HANDLE && instance_generation->hasSwapchainConfigurationGeneration() &&
               instance_generation->hasLogicalDeviceGeneration() && instance_generation->logicalDevice() != VK_NULL_HANDLE &&
               instance_generation->hasSurfaceGeneration() && instance_generation->surface() != VK_NULL_HANDLE);
    ensure_equals("frame-slot creation and destruction emit no validation messages",
                  instance_generation->validationSnapshot().mMessageCount, std::uint32_t{ 0 });
    ensure("frame-slot reset creates no current CGL context", CGLGetCurrentContext() == initial_cgl_context);
    ensure_equals("frame-slot reset leaves the OpenGL manager unchanged", gGLManager.mInited, initial_gl_manager);

    ensure("the native smoke explicitly resets the readback destination before the presentation pipeline",
           owner->resetSwapchainReadbackGeneration());
    ensure("explicit readback reset removes the mapped buffer and memory",
           !instance_generation->hasSwapchainReadbackGeneration() && instance_generation->swapchainReadbackBuffer() == VK_NULL_HANDLE &&
               instance_generation->swapchainReadbackMemory() == VK_NULL_HANDLE &&
               !instance_generation->swapchainReadbackIsMapped());
    ensure("readback reset leaves the independent target, pipeline, and every older swapchain parent live",
           instance_generation->hasSwapchainPresentationPipelineGeneration() &&
               instance_generation->swapchainPresentationPipeline() != VK_NULL_HANDLE &&
               instance_generation->hasSwapchainPresentationTargetGeneration() &&
               instance_generation->swapchainPresentationRenderPass() != VK_NULL_HANDLE &&
               instance_generation->hasSwapchainImagesGeneration() && instance_generation->hasSwapchainGeneration() &&
               instance_generation->hasSwapchainConfigurationGeneration() && instance_generation->hasLogicalDeviceGeneration() &&
               instance_generation->hasSurfaceGeneration());
    ensure_equals("readback creation and destruction emit no validation messages", instance_generation->validationSnapshot().mMessageCount,
                  std::uint32_t{ 0 });
    ensure("readback reset creates no current CGL context", CGLGetCurrentContext() == initial_cgl_context);
    ensure_equals("readback reset leaves the OpenGL manager unchanged", gGLManager.mInited, initial_gl_manager);

    ensure("the native smoke explicitly resets the presentation pipeline before its presentation target",
           owner->resetSwapchainPresentationPipelineGeneration());
    ensure("explicit presentation-pipeline reset removes both owned graphics handles",
           !instance_generation->hasSwapchainPresentationPipelineGeneration() &&
               instance_generation->swapchainPresentationPipelineLayout() == VK_NULL_HANDLE &&
               instance_generation->swapchainPresentationPipeline() == VK_NULL_HANDLE);
    ensure("presentation-pipeline reset leaves its exact target and every older swapchain parent live",
           instance_generation->hasSwapchainPresentationTargetGeneration() &&
               instance_generation->swapchainPresentationRenderPass() != VK_NULL_HANDLE &&
               instance_generation->swapchainPresentationFramebufferCount() == resolved_image_count &&
               instance_generation->hasSwapchainImagesGeneration() && instance_generation->hasSwapchainGeneration() &&
               instance_generation->hasSwapchainConfigurationGeneration() && instance_generation->hasLogicalDeviceGeneration() &&
               instance_generation->hasSurfaceGeneration());
    ensure_equals("presentation-pipeline creation and destruction emit no validation messages",
                  instance_generation->validationSnapshot().mMessageCount, std::uint32_t{ 0 });
    ensure("presentation-pipeline reset creates no current CGL context", CGLGetCurrentContext() == initial_cgl_context);
    ensure_equals("presentation-pipeline reset leaves the OpenGL manager unchanged", gGLManager.mInited, initial_gl_manager);

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
    ensure("surface reset leaves no presentation-pipeline generation",
           !instance_generation->hasSwapchainPresentationPipelineGeneration() &&
               instance_generation->swapchainPresentationPipelineLayout() == VK_NULL_HANDLE &&
               instance_generation->swapchainPresentationPipeline() == VK_NULL_HANDLE);
    ensure("surface reset leaves no readback generation or mapped destination",
           !instance_generation->hasSwapchainReadbackGeneration() && instance_generation->swapchainReadbackBuffer() == VK_NULL_HANDLE &&
               instance_generation->swapchainReadbackMemory() == VK_NULL_HANDLE &&
               !instance_generation->swapchainReadbackIsMapped());
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

template<>
template<>
void window_vulkan_macos_wsi_object::test<2>()
{
    if (!nativeSmokeRequested()) return;

    struct NativeLifetime
    {
        LLWindowMacOSXVulkanNative native{};
        ~NativeLifetime() { (void)llwindow_macosx_vulkan_native_destroy(&native); }
    } original, attachment;
    const auto initial_context = CGLGetCurrentContext();
    ensure_equals("create a native window for the borrowing seam",
                  llwindow_macosx_vulkan_native_create(640, 480, &original.native),
                  LLWINDOWMACOSXVULKAN_STATUS_SUCCESS);
    const auto original_view = original.native.view;
    ensure_equals("attach to the existing window",
                  llwindow_macosx_vulkan_native_attach(original.native.window, 640, 480, &attachment.native),
                  LLWINDOWMACOSXVULKAN_STATUS_SUCCESS);
    ensure("attachment borrows the same window and owns a different view",
           attachment.native.window == original.native.window && attachment.native.view != original_view);
    ensure_equals("retire only the attachment",
                  llwindow_macosx_vulkan_native_destroy(&attachment.native), LLWINDOWMACOSXVULKAN_STATUS_SUCCESS);
    ensure_equals("original window and content view remain usable",
                  llwindow_macosx_vulkan_native_refresh(&original.native), LLWINDOWMACOSXVULKAN_STATUS_SUCCESS);
    ensure("previous content view was restored", original.native.view == original_view);
    ensure_equals("original window remains resizable after attachment retirement",
                  llwindow_macosx_vulkan_native_resize_for_diagnostic(800, 600, &original.native),
                  LLWINDOWMACOSXVULKAN_STATUS_SUCCESS);
    ensure("attachment lifecycle did not create a GL context", CGLGetCurrentContext() == initial_context);
}

} // namespace tut
