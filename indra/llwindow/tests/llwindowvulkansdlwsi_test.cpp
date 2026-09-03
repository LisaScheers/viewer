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
#include "lltextureuploaddiagnostic.h"
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
constexpr const char* NATIVE_SMOKE_TITLE       = "SDL Vulkan native smoke";

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

LLRenderVulkan::VulkanUploadSourceDescription fixedUploadSourceDescription()
{
    const LLRenderContract::TextureUploadFixture fixture = LLRenderContract::makeTextureUploadFixture();
    static_assert(sizeof(fixture.mScreenTriangle) == LLRenderVulkan::VULKAN_UPLOAD_SOURCE_BYTE_COUNT);

    LLRenderVulkan::VulkanUploadSourceDescription description;
    description.mHandle = LLRenderContract::StreamingUploadHandles{}.mScreenTriangle;
    std::memcpy(description.mBytes.data(), fixture.mScreenTriangle.data(), description.mBytes.size());
    return description;
}

LLRenderVulkan::VulkanTextureUploadSourceDescription fixedTextureUploadSourceDescription()
{
    const LLRenderContract::TextureUploadFixture fixture = LLRenderContract::makeTextureUploadFixture();
    static_assert(fixture.mSourceRGBA8.size() == LLRenderVulkan::VULKAN_TEXTURE_UPLOAD_SOURCE_BYTE_COUNT);
    return LLRenderVulkan::vulkanTextureUploadSourceDescription(fixture.mSourceRGBA8);
}

SDL_Window* findNativeSmokeWindow()
{
    int          window_count = 0;
    SDL_Window** windows      = SDL_GetWindows(&window_count);
    SDL_Window*  match        = nullptr;
    for (int index = 0; windows && index < window_count; ++index)
    {
        const SDL_WindowFlags flags = SDL_GetWindowFlags(windows[index]);
        const char*           title = SDL_GetWindowTitle(windows[index]);
        if ((flags & SDL_WINDOW_VULKAN) != 0 && (flags & SDL_WINDOW_OPENGL) == 0 && title && std::strcmp(title, NATIVE_SMOKE_TITLE) == 0)
        {
            if (match)
            {
                match = nullptr;
                break;
            }
            match = windows[index];
        }
    }
    SDL_free(windows);
    return match;
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
    const LLWindowSDL*                              mWindow     = nullptr;
    const LLRenderVulkan::VulkanInstanceGeneration* mGeneration = nullptr;
};

bool frameSlotInstanceOwnerIsCurrent(void* userdata, const LLRenderVulkan::VulkanInstanceGeneration& generation) noexcept
{
    const auto& context = *static_cast<const FrameSlotOperationContext*>(userdata);
    return context.mWindow && context.mGeneration == &generation && context.mWindow->getVulkanInstanceGeneration() == context.mGeneration;
}

bool frameSlotWindowGenerationIsCurrent(void* userdata, std::uint64_t native_window_generation) noexcept
{
    const auto& context = *static_cast<const FrameSlotOperationContext*>(userdata);
    return context.mWindow && context.mWindow->isVulkanWindowGenerationCurrent(native_window_generation);
}

struct TextureDestinationSnapshot
{
    VkImage                 mImage                       = VK_NULL_HANDLE;
    VkDeviceMemory          mMemory                      = VK_NULL_HANDLE;
    VkImageView             mImageView                   = VK_NULL_HANDLE;
    VkDeviceSize            mAllocationSize              = 0;
    VkDeviceSize            mAllocationAlignment         = 0;
    std::uint32_t           mCompatibleMemoryTypeBits    = 0;
    std::uint32_t           mMemoryTypeIndex             = 0;
    VkMemoryPropertyFlags   mMemoryPropertyFlags         = 0;
    bool                    mPrefersDedicatedAllocation  = false;
    bool                    mRequiresDedicatedAllocation = false;
    VkFormatFeatureFlags    mFormatFeatures              = 0;
    VkImageFormatProperties mImageFormatProperties{};
    VkImageSubresourceRange mViewRange{};
};

TextureDestinationSnapshot textureDestinationSnapshot(const LLRenderVulkan::VulkanInstanceGeneration& generation) noexcept
{
    return { generation.textureUploadDestinationImage(),
             generation.textureUploadDestinationMemory(),
             generation.textureUploadDestinationImageView(),
             generation.textureUploadDestinationAllocationSize(),
             generation.textureUploadDestinationAllocationAlignment(),
             generation.textureUploadDestinationCompatibleMemoryTypeBits(),
             generation.textureUploadDestinationMemoryTypeIndex(),
             generation.textureUploadDestinationMemoryPropertyFlags(),
             generation.textureUploadDestinationPrefersDedicatedAllocation(),
             generation.textureUploadDestinationRequiresDedicatedAllocation(),
             generation.textureUploadDestinationFormatFeatures(),
             generation.textureUploadDestinationImageFormatProperties(),
             generation.textureUploadDestinationViewRange() };
}

bool textureDestinationMatches(const LLRenderVulkan::VulkanInstanceGeneration&                  generation,
                               const LLRenderVulkan::VulkanTextureUploadDestinationDescription& description,
                               const TextureDestinationSnapshot&                                retained) noexcept
{
    constexpr VkFormatFeatureFlags required_features = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
                                                       VK_FORMAT_FEATURE_TRANSFER_DST_BIT | VK_FORMAT_FEATURE_BLIT_SRC_BIT |
                                                       VK_FORMAT_FEATURE_BLIT_DST_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    const VkExtent3D                 resident_extent = generation.textureUploadDestinationResidentExtent();
    const LLRenderContract::Extent2D logical_extent  = generation.textureUploadDestinationLogicalExtent();
    const VkImageFormatProperties    limits          = generation.textureUploadDestinationImageFormatProperties();
    const VkImageSubresourceRange    view_range      = generation.textureUploadDestinationViewRange();
    return generation.hasTextureUploadDestinationGeneration() &&
           generation.textureUploadDestinationResourceHandle() == description.mHandle &&
           generation.textureUploadDestinationExpectedRevision() == description.mExpectedRevision &&
           resident_extent.width == description.mResidentExtent.mWidth && resident_extent.height == description.mResidentExtent.mHeight &&
           resident_extent.depth == 1 && logical_extent.mWidth == description.mLogicalExtent.mWidth &&
           logical_extent.mHeight == description.mLogicalExtent.mHeight &&
           generation.textureUploadDestinationResidentDiscard() == description.mResidentDiscard &&
           generation.textureUploadDestinationPixelFormat() == description.mFormat &&
           generation.textureUploadDestinationInitialState() == description.mInitialState &&
           generation.textureUploadDestinationFlags() == 0 && generation.textureUploadDestinationImageType() == VK_IMAGE_TYPE_2D &&
           generation.textureUploadDestinationFormat() == VK_FORMAT_R8G8B8A8_UNORM &&
           generation.textureUploadDestinationMipLevels() == description.mMipLevels &&
           generation.textureUploadDestinationArrayLayers() == description.mArrayLayers &&
           generation.textureUploadDestinationSamples() == VK_SAMPLE_COUNT_1_BIT &&
           generation.textureUploadDestinationTiling() == VK_IMAGE_TILING_OPTIMAL &&
           generation.textureUploadDestinationUsage() ==
               (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT) &&
           generation.textureUploadDestinationSharingMode() == VK_SHARING_MODE_EXCLUSIVE &&
           generation.textureUploadDestinationInitialLayout() == VK_IMAGE_LAYOUT_UNDEFINED &&
           (generation.textureUploadDestinationFormatFeatures() & required_features) == required_features &&
           limits.maxExtent.width >= description.mResidentExtent.mWidth && limits.maxExtent.height >= description.mResidentExtent.mHeight &&
           limits.maxExtent.depth >= 1 && limits.maxMipLevels >= description.mMipLevels &&
           limits.maxArrayLayers >= description.mArrayLayers && (limits.sampleCounts & VK_SAMPLE_COUNT_1_BIT) != 0 &&
           limits.maxResourceSize != 0 && generation.textureUploadDestinationImage() == retained.mImage &&
           retained.mImage != VK_NULL_HANDLE && generation.textureUploadDestinationMemory() == retained.mMemory &&
           retained.mMemory != VK_NULL_HANDLE && generation.textureUploadDestinationImageView() == retained.mImageView &&
           retained.mImageView != VK_NULL_HANDLE && generation.textureUploadDestinationAllocationSize() == retained.mAllocationSize &&
           retained.mAllocationSize != 0 && generation.textureUploadDestinationAllocationAlignment() == retained.mAllocationAlignment &&
           retained.mAllocationAlignment != 0 &&
           generation.textureUploadDestinationCompatibleMemoryTypeBits() == retained.mCompatibleMemoryTypeBits &&
           retained.mCompatibleMemoryTypeBits != 0 && generation.textureUploadDestinationMemoryTypeIndex() == retained.mMemoryTypeIndex &&
           generation.textureUploadDestinationMemoryPropertyFlags() == retained.mMemoryPropertyFlags &&
           generation.textureUploadDestinationIsDeviceLocal() &&
           generation.textureUploadDestinationPrefersDedicatedAllocation() == retained.mPrefersDedicatedAllocation &&
           generation.textureUploadDestinationRequiresDedicatedAllocation() == retained.mRequiresDedicatedAllocation &&
           generation.textureUploadDestinationFormatFeatures() == retained.mFormatFeatures &&
           limits.maxExtent.width == retained.mImageFormatProperties.maxExtent.width &&
           limits.maxExtent.height == retained.mImageFormatProperties.maxExtent.height &&
           limits.maxExtent.depth == retained.mImageFormatProperties.maxExtent.depth &&
           limits.maxMipLevels == retained.mImageFormatProperties.maxMipLevels &&
           limits.maxArrayLayers == retained.mImageFormatProperties.maxArrayLayers &&
           limits.sampleCounts == retained.mImageFormatProperties.sampleCounts &&
           limits.maxResourceSize == retained.mImageFormatProperties.maxResourceSize &&
           view_range.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT && view_range.baseMipLevel == 0 &&
           view_range.levelCount == description.mMipLevels && view_range.baseArrayLayer == 0 &&
           view_range.layerCount == description.mArrayLayers && view_range.aspectMask == retained.mViewRange.aspectMask &&
           view_range.baseMipLevel == retained.mViewRange.baseMipLevel && view_range.levelCount == retained.mViewRange.levelCount &&
           view_range.baseArrayLayer == retained.mViewRange.baseArrayLayer && view_range.layerCount == retained.mViewRange.layerCount;
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

    LLWindow* window = LLWindowManager::createWindow(nullptr, NATIVE_SMOKE_TITLE, "llwindowvulkansdlwsi", 0, 0, 64, 64,
                                                     LLWindow::GraphicsAPI::Vulkan, LLWindow::FLAG_CREATE_HIDDEN);

    bool        created                  = window != nullptr;
    bool        tracked                  = created && LLWindowManager::isWindowValid(window);
    bool        selected_vulkan          = created && window->getGraphicsAPI() == LLWindow::GraphicsAPI::Vulkan;
    bool        x11_driver               = created && SDL_GetCurrentVideoDriver() && std::strcmp(SDL_GetCurrentVideoDriver(), "x11") == 0;
    bool        no_gl_context            = created && SDL_GL_GetCurrentContext() == nullptr;
    bool        no_gl_manager            = created && !gGLManager.mInited;
    SDL_Window* native_sdl_window        = created ? findNativeSmokeWindow() : nullptr;
    bool        native_sdl_window_exact  = native_sdl_window != nullptr;
    bool        requirements_published   = false;
    bool        generation_current       = false;
    bool        resolver_identity        = false;
    bool        extensions_identical     = false;
    bool        global_dispatch_resolved = false;

    bool instance_acquired                                   = false;
    bool instance_nonnull                                    = false;
    bool instance_api_1_1                                    = false;
    bool instance_generation_exact                           = false;
    bool instance_extensions_ordered                         = false;
    bool instance_diagnostic_extensions                      = false;
    bool instance_validation_enabled                         = false;
    bool instance_validation_clean                           = false;
    bool instance_window_owned                               = false;
    bool surface_acquired                                    = false;
    bool surface_nonnull                                     = false;
    bool surface_generation_exact                            = false;
    bool surface_window_owned                                = false;
    bool presentation_device_acquired                        = false;
    bool presentation_device_nonnull                         = false;
    bool presentation_device_api_1_1                         = false;
    bool presentation_queue_usable                           = false;
    bool presentation_extensions_exact                       = false;
    bool presentation_maintenance                            = false;
    bool presentation_device_removed                         = false;
    bool logical_device_acquired                             = false;
    bool logical_device_nonnull                              = false;
    bool logical_queue_nonnull                               = false;
    bool logical_provenance_exact                            = false;
    bool logical_feature_exact                               = false;
    bool logical_extensions_exact                            = false;
    bool logical_maintenance                                 = false;
    bool logical_device_removed                              = false;
    bool texture_destination_acquired                        = false;
    bool texture_destination_metadata_exact                  = false;
    bool texture_destination_rebuild_retained                = false;
    bool texture_destination_explicitly_reset                = false;
    bool texture_destination_removed                         = false;
    bool texture_destination_chain_retained                  = false;
    bool texture_destination_validation_clean                = false;
    bool texture_source_acquired                             = false;
    bool texture_source_metadata_exact                       = false;
    bool texture_source_native_distinct                      = false;
    bool texture_source_rebuild_retained                     = false;
    bool texture_source_explicitly_reset                     = false;
    bool texture_source_removed                              = false;
    bool texture_source_destination_retained                 = false;
    bool texture_source_chain_retained                       = false;
    bool texture_source_validation_clean                     = false;
    bool texture_transfer_acquired                           = false;
    bool texture_transfer_complete_exact                     = false;
    bool texture_destination_resident_exact                  = false;
    bool texture_transfer_rebuild_retained                   = false;
    bool texture_transfer_explicitly_reset                   = false;
    bool texture_transfer_validation_clean                   = false;
    bool texture_sample_binding_acquired                     = false;
    bool texture_sample_binding_metadata_exact               = false;
    bool texture_sample_binding_rebuild_retained             = false;
    bool texture_sample_binding_transfer_reset_retained      = false;
    bool texture_sample_binding_source_reset_retained        = false;
    bool texture_sample_binding_explicitly_reset             = false;
    bool texture_sample_binding_removed                      = false;
    bool texture_sample_binding_destination_retained         = false;
    bool texture_sample_binding_chain_retained               = false;
    bool texture_sample_binding_validation_clean             = false;
    bool upload_source_acquired                              = false;
    bool upload_source_metadata_exact                        = false;
    bool upload_destination_acquired                         = false;
    bool upload_destination_metadata_exact                   = false;
    bool upload_destination_initially_nonresident            = false;
    bool upload_transfer_acquired                            = false;
    bool upload_transfer_ready_exact                         = false;
    bool upload_transfer_complete_exact                      = false;
    bool upload_destination_resident_exact                   = false;
    bool upload_destination_draw_retained                    = false;
    bool upload_destination_rebuild_retained                 = false;
    bool upload_destination_rebuilt_draw_retained            = false;
    bool upload_transfer_complete_retry_rejected             = false;
    bool upload_transfer_not_resubmitted                     = false;
    bool upload_source_explicitly_reset                      = false;
    bool upload_source_removed                               = false;
    bool upload_source_chain_retained                        = false;
    bool upload_transfer_removed_with_source                 = false;
    bool upload_destination_source_retirement_retained       = false;
    bool upload_destination_explicitly_reset                 = false;
    bool upload_destination_removed                          = false;
    bool upload_destination_chain_retained                   = false;
    bool upload_source_validation_clean                      = false;
    bool upload_destination_validation_clean                 = false;
    bool swapchain_configuration_acquired                    = false;
    bool swapchain_drawable_extent_exact                     = false;
    bool swapchain_format_supported                          = false;
    bool swapchain_present_mode_exact                        = false;
    bool swapchain_create_policy_exact                       = false;
    bool swapchain_acquired                                  = false;
    bool swapchain_nonnull                                   = false;
    bool swapchain_provenance_exact                          = false;
    bool swapchain_images_acquired                           = false;
    bool swapchain_images_nonempty                           = false;
    bool swapchain_image_views_complete                      = false;
    bool swapchain_image_bounds_exact                        = false;
    bool swapchain_images_provenance_exact                   = false;
    bool presentation_target_acquired                        = false;
    bool presentation_target_complete                        = false;
    bool presentation_target_provenance_exact                = false;
    bool presentation_pipeline_acquired                      = false;
    bool presentation_pipeline_handles_nonnull               = false;
    bool presentation_pipeline_rebuilt_nonnull               = false;
    bool readback_acquired                                   = false;
    bool readback_metadata_exact                             = false;
    bool readback_rebuilt_exact                              = false;
    bool frame_slot_initially_acquired                       = false;
    bool frame_slot_initially_reset                          = false;
    bool frame_slot_acquired                                 = false;
    bool frame_slot_handles_nonnull                          = false;
    bool frame_slot_provenance_exact                         = false;
    bool swapchain_resize_requested                          = false;
    bool swapchain_resize_synchronized                       = false;
    bool swapchain_resize_observed                           = false;
    bool swapchain_rebuild_ready                             = false;
    bool swapchain_rebuild_parent_exact                      = false;
    bool swapchain_rebuild_chain_complete                    = false;
    bool swapchain_rebuild_extent_exact                      = false;
    bool frame_slot_transfer_clear_before_rebuild            = false;
    bool frame_slot_render_pass_clear_before_rebuild         = false;
    bool frame_slot_render_pass_draw_readback_before_rebuild = false;
    bool frame_slot_initial_observation_detached             = false;
    bool frame_slot_transfer_clear_after_rebuild             = false;
    bool frame_slot_render_pass_clear_after_rebuild          = false;
    bool frame_slot_render_pass_draw_readback_after_rebuild  = false;
    bool frame_slot_handles_untouched                        = false;
    bool frame_slot_presentation_clean                       = false;
    bool frame_slot_explicitly_reset                         = false;
    bool frame_slot_removed                                  = false;
    bool readback_explicitly_reset                           = false;
    bool readback_removed                                    = false;
    bool readback_siblings_retained                          = false;
    bool presentation_pipeline_explicitly_reset              = false;
    bool presentation_pipeline_removed                       = false;
    bool presentation_target_explicitly_reset                = false;
    bool presentation_target_removed                         = false;
    bool swapchain_images_removed                            = false;
    bool swapchain_removed                                   = false;
    bool swapchain_configuration_removed                     = false;
    bool surface_explicitly_reset                            = false;
    bool surface_removed                                     = false;
    bool surface_parent_still_live                           = false;
    bool surface_validation_still_live                       = false;
    bool mixed_opengl_rejected                               = false;
    bool vulkan_context_switch_fails                         = false;
    bool vulkan_shared_context_fails                         = false;

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
                    device_extensions.size() == (instance_generation->portabilitySubsetRequired() ? std::size_t{ 3 } : std::size_t{ 2 }) &&
                    device_extensions[0] == VK_KHR_SWAPCHAIN_EXTENSION_NAME &&
                    device_extensions[1] == VK_KHR_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME;
                if (presentation_extensions_exact && instance_generation->portabilitySubsetRequired())
                {
                    presentation_extensions_exact = device_extensions[2] == "VK_KHR_portability_subset";
                }
                presentation_maintenance = instance_generation->swapchainMaintenance1Supported();
                logical_device_acquired  = instance_generation->hasLogicalDeviceGeneration();
                logical_device_nonnull   = instance_generation->logicalDevice() != VK_NULL_HANDLE;
                logical_queue_nonnull    = instance_generation->presentationQueue() != VK_NULL_HANDLE;
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
                logical_maintenance = instance_generation->swapchainMaintenance1Enabled();

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
                    instance_generation->swapchainImageUsage() ==
                        (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT) &&
                    instance_generation->swapchainImageSharingMode() == VK_SHARING_MODE_EXCLUSIVE &&
                    instance_generation->swapchainPreTransform() == capabilities.currentTransform &&
                    instance_generation->swapchainCompositeAlpha() == expectedCompositeAlpha(capabilities.supportedCompositeAlpha) &&
                    instance_generation->swapchainClipped() == VK_FALSE;
                swapchain_acquired         = instance_generation->hasSwapchainGeneration();
                swapchain_nonnull          = instance_generation->swapchain() != VK_NULL_HANDLE;
                swapchain_provenance_exact = instance_generation->swapchainDevice() != VK_NULL_HANDLE &&
                                             instance_generation->swapchainDevice() == instance_generation->logicalDevice() &&
                                             instance_generation->swapchainSurface() != VK_NULL_HANDLE &&
                                             instance_generation->swapchainSurface() == instance_generation->surface();
                swapchain_images_acquired      = instance_generation->hasSwapchainImagesGeneration();
                std::uint32_t image_count      = instance_generation->resolvedSwapchainImageCount();
                swapchain_images_nonempty      = image_count != 0;
                swapchain_image_views_complete = swapchain_images_nonempty;
                for (std::uint32_t index = 0; swapchain_image_views_complete && index < image_count; ++index)
                {
                    swapchain_image_views_complete = instance_generation->swapchainImage(index) != VK_NULL_HANDLE &&
                                                     instance_generation->swapchainImageView(index) != VK_NULL_HANDLE;
                }
                swapchain_image_bounds_exact = instance_generation->swapchainImage(image_count) == VK_NULL_HANDLE &&
                                               instance_generation->swapchainImageView(image_count) == VK_NULL_HANDLE;
                swapchain_images_provenance_exact = swapchain_images_acquired && swapchain_acquired && logical_device_acquired &&
                                                    swapchain_configuration_acquired && swapchain_format_supported;
                auto*                     mutable_generation = const_cast<LLRenderVulkan::VulkanInstanceGeneration*>(instance_generation);
                FrameSlotOperationContext upload_source_context{ static_cast<const LLWindowSDL*>(window), instance_generation };
                const LLRenderVulkan::VulkanTextureUploadDestinationDescription texture_description =
                    LLRenderVulkan::vulkanTextureUploadDestinationDescription();
                LLRenderVulkan::VulkanTextureUploadDestinationRequest texture_request;
                texture_request.mNativeWindowGeneration = requirements->nativeWindowGeneration();
                texture_request.mDescription            = texture_description;
                texture_request.mInstanceOwnerCheck     = { &upload_source_context, frameSlotInstanceOwnerIsCurrent };
                texture_request.mWindowGenerationCheck  = { &upload_source_context, frameSlotWindowGenerationIsCurrent };
                texture_destination_acquired            = logical_device_acquired &&
                                               !mutable_generation->acquireTextureUploadDestinationGeneration(texture_request) &&
                                               instance_generation->hasTextureUploadDestinationGeneration();
                const TextureDestinationSnapshot retained_texture_destination = textureDestinationSnapshot(*instance_generation);
                texture_destination_metadata_exact =
                    texture_destination_acquired &&
                    textureDestinationMatches(*instance_generation, texture_description, retained_texture_destination);
                const LLRenderVulkan::VulkanTextureUploadSourceDescription texture_source_description =
                    fixedTextureUploadSourceDescription();
                LLRenderVulkan::VulkanTextureUploadSourceRequest texture_source_request;
                texture_source_request.mNativeWindowGeneration = requirements->nativeWindowGeneration();
                texture_source_request.mDescription            = texture_source_description;
                texture_source_request.mInstanceOwnerCheck     = { &upload_source_context, frameSlotInstanceOwnerIsCurrent };
                texture_source_request.mWindowGenerationCheck  = { &upload_source_context, frameSlotWindowGenerationIsCurrent };
                texture_source_acquired                        = texture_destination_metadata_exact &&
                                          !mutable_generation->acquireTextureUploadSourceGeneration(texture_source_request) &&
                                          instance_generation->hasTextureUploadSourceGeneration();
                const LLRenderContract::ImageHandle retained_texture_source_handle =
                    instance_generation->textureUploadSourceResourceHandle();
                const std::uint64_t  retained_texture_source_revision        = instance_generation->textureUploadSourceExpectedRevision();
                const std::uint64_t  retained_texture_source_identity        = instance_generation->textureUploadSourceContentIdentity();
                const VkBuffer       retained_texture_source_buffer          = instance_generation->textureUploadSourceBuffer();
                const VkDeviceMemory retained_texture_source_memory          = instance_generation->textureUploadSourceMemory();
                const VkDeviceSize   retained_texture_source_byte_count      = instance_generation->textureUploadSourceByteCount();
                const VkDeviceSize   retained_texture_source_allocation_size = instance_generation->textureUploadSourceAllocationSize();
                const std::uint32_t  retained_texture_source_memory_type     = instance_generation->textureUploadSourceMemoryTypeIndex();
                const VkMemoryPropertyFlags retained_texture_source_memory_flags =
                    instance_generation->textureUploadSourceMemoryPropertyFlags();
                const bool retained_texture_source_coherent = instance_generation->textureUploadSourceIsCoherent();
                texture_source_metadata_exact =
                    texture_source_acquired && retained_texture_source_handle == texture_source_description.mHandle &&
                    retained_texture_source_revision == texture_source_description.mExpectedRevision &&
                    instance_generation->textureUploadSourceResidentExtent().mWidth == LLRenderContract::TEXTURE_UPLOAD_RESIDENT_WIDTH &&
                    instance_generation->textureUploadSourceResidentExtent().mHeight == LLRenderContract::TEXTURE_UPLOAD_RESIDENT_HEIGHT &&
                    instance_generation->textureUploadSourcePixelFormat() == LLRenderContract::PixelFormat::RGBA8Unorm &&
                    instance_generation->textureUploadSourceRowPitch() == LLRenderContract::TEXTURE_UPLOAD_ROW_PITCH &&
                    instance_generation->textureUploadSourceRowOrigin() == LLRenderContract::RowOrigin::TopLeft &&
                    retained_texture_source_identity == LLRenderContract::stableByteContentIdentity(texture_source_description.mBytes) &&
                    retained_texture_source_identity != 0 && instance_generation->textureUploadSourceFlags() == 0 &&
                    instance_generation->textureUploadSourceUsage() == VK_BUFFER_USAGE_TRANSFER_SRC_BIT &&
                    instance_generation->textureUploadSourceSharingMode() == VK_SHARING_MODE_EXCLUSIVE &&
                    retained_texture_source_buffer != VK_NULL_HANDLE && retained_texture_source_memory != VK_NULL_HANDLE &&
                    retained_texture_source_memory != retained_texture_destination.mMemory &&
                    retained_texture_source_byte_count == LLRenderVulkan::VULKAN_TEXTURE_UPLOAD_SOURCE_BYTE_COUNT &&
                    retained_texture_source_allocation_size >= retained_texture_source_byte_count &&
                    (retained_texture_source_memory_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0 &&
                    retained_texture_source_coherent ==
                        ((retained_texture_source_memory_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0);
                const auto texture_source_retained = [&]() noexcept
                {
                    return instance_generation->hasTextureUploadSourceGeneration() &&
                           instance_generation->textureUploadSourceResourceHandle() == retained_texture_source_handle &&
                           instance_generation->textureUploadSourceExpectedRevision() == retained_texture_source_revision &&
                           instance_generation->textureUploadSourceContentIdentity() == retained_texture_source_identity &&
                           instance_generation->textureUploadSourceFlags() == 0 &&
                           instance_generation->textureUploadSourceUsage() == VK_BUFFER_USAGE_TRANSFER_SRC_BIT &&
                           instance_generation->textureUploadSourceSharingMode() == VK_SHARING_MODE_EXCLUSIVE &&
                           instance_generation->textureUploadSourceBuffer() == retained_texture_source_buffer &&
                           instance_generation->textureUploadSourceMemory() == retained_texture_source_memory &&
                           instance_generation->textureUploadSourceByteCount() == retained_texture_source_byte_count &&
                           instance_generation->textureUploadSourceAllocationSize() == retained_texture_source_allocation_size &&
                           instance_generation->textureUploadSourceMemoryTypeIndex() == retained_texture_source_memory_type &&
                           instance_generation->textureUploadSourceMemoryPropertyFlags() == retained_texture_source_memory_flags &&
                           instance_generation->textureUploadSourceIsCoherent() == retained_texture_source_coherent;
                };
                LLRenderVulkan::VulkanTextureUploadTransferRequest texture_transfer_request;
                texture_transfer_request.mNativeWindowGeneration = requirements->nativeWindowGeneration();
                texture_transfer_request.mSourceDescription      = texture_source_description;
                texture_transfer_request.mDestinationDescription = texture_description;
                texture_transfer_request.mInstanceOwnerCheck     = { &upload_source_context, frameSlotInstanceOwnerIsCurrent };
                texture_transfer_request.mWindowGenerationCheck  = { &upload_source_context, frameSlotWindowGenerationIsCurrent };
                texture_transfer_acquired                        = texture_source_metadata_exact && texture_destination_metadata_exact &&
                                            !mutable_generation->acquireTextureUploadTransferGeneration(texture_transfer_request) &&
                                            instance_generation->hasTextureUploadTransferGeneration();
                const VkCommandPool   retained_texture_transfer_pool    = instance_generation->textureUploadTransferCommandPool();
                const VkCommandBuffer retained_texture_transfer_command = instance_generation->textureUploadTransferCommandBuffer();
                const VkFence         retained_texture_transfer_fence   = instance_generation->textureUploadTransferFence();
                const auto            texture_transfer_retained         = [&]() noexcept
                {
                    return instance_generation->hasTextureUploadTransferGeneration() &&
                           instance_generation->textureUploadTransferResourceHandle() == texture_description.mHandle &&
                           instance_generation->textureUploadTransferExpectedRevision() == texture_description.mExpectedRevision &&
                           instance_generation->textureUploadTransferContentIdentity() == retained_texture_source_identity &&
                           instance_generation->textureUploadTransferSourceBuffer() == retained_texture_source_buffer &&
                           instance_generation->textureUploadTransferDestinationImage() == retained_texture_destination.mImage &&
                           instance_generation->textureUploadTransferQueue() == instance_generation->presentationQueue() &&
                           instance_generation->textureUploadTransferQueueFamilyIndex() ==
                               instance_generation->presentationQueueFamilyIndex() &&
                           instance_generation->textureUploadTransferQueueIndex() == instance_generation->logicalDeviceQueueIndex() &&
                           instance_generation->textureUploadTransferCommandPool() == retained_texture_transfer_pool &&
                           instance_generation->textureUploadTransferCommandBuffer() == retained_texture_transfer_command &&
                           instance_generation->textureUploadTransferFence() == retained_texture_transfer_fence;
                };
                LLRenderVulkan::VulkanTextureUploadTransferOperationRequest texture_transfer_operation_request;
                texture_transfer_operation_request.mNativeWindowGeneration = requirements->nativeWindowGeneration();
                texture_transfer_operation_request.mSourceDescription      = texture_source_description;
                texture_transfer_operation_request.mDestinationDescription = texture_description;
                texture_transfer_operation_request.mTimeoutNs              = 1'000'000'000;
                texture_transfer_operation_request.mInstanceOwnerCheck     = { &upload_source_context, frameSlotInstanceOwnerIsCurrent };
                texture_transfer_operation_request.mWindowGenerationCheck  = { &upload_source_context, frameSlotWindowGenerationIsCurrent };
                auto texture_transfer_result = mutable_generation->executeTextureUploadTransfer(texture_transfer_operation_request);
                while (instance_generation->textureUploadTransferDisposition() ==
                       LLRenderVulkan::VulkanTextureUploadTransferDisposition::Pending)
                {
                    texture_transfer_result = mutable_generation->retryTextureUploadTransferCompletion(texture_transfer_operation_request);
                }
                const auto* texture_transfer_disposition =
                    std::get_if<LLRenderVulkan::VulkanTextureUploadTransferDisposition>(&texture_transfer_result);
                texture_transfer_complete_exact =
                    texture_transfer_acquired && texture_transfer_disposition &&
                    *texture_transfer_disposition == LLRenderVulkan::VulkanTextureUploadTransferDisposition::Complete &&
                    texture_transfer_retained() && instance_generation->textureUploadTransferSubmissionCount() == 1 &&
                    instance_generation->textureUploadTransferCompletionWaitCount() >= 1 &&
                    instance_generation->textureUploadTransferDisposition() ==
                        LLRenderVulkan::VulkanTextureUploadTransferDisposition::Complete;
                texture_destination_resident_exact =
                    texture_transfer_complete_exact && instance_generation->textureUploadDestinationIsResident() &&
                    instance_generation->textureUploadDestinationResidentRevision() == texture_description.mExpectedRevision &&
                    instance_generation->textureUploadDestinationResidentContentIdentity() == retained_texture_source_identity &&
                    instance_generation->textureUploadDestinationCurrentState() == LLRenderContract::ImageState::ShaderRead;
                const LLRenderVulkan::VulkanTextureUploadSampleBindingDescription texture_sample_binding_description =
                    LLRenderVulkan::vulkanTextureUploadSampleBindingDescription();
                LLRenderVulkan::VulkanTextureUploadSampleBindingRequest texture_sample_binding_request;
                texture_sample_binding_request.mNativeWindowGeneration = requirements->nativeWindowGeneration();
                texture_sample_binding_request.mDestinationDescription = texture_description;
                texture_sample_binding_request.mDescription            = texture_sample_binding_description;
                texture_sample_binding_request.mInstanceOwnerCheck     = { &upload_source_context, frameSlotInstanceOwnerIsCurrent };
                texture_sample_binding_request.mWindowGenerationCheck  = { &upload_source_context, frameSlotWindowGenerationIsCurrent };
                texture_sample_binding_acquired =
                    texture_destination_resident_exact &&
                    !mutable_generation->acquireTextureUploadSampleBindingGeneration(texture_sample_binding_request) &&
                    instance_generation->hasTextureUploadSampleBindingGeneration();
                const VkSampler retained_texture_sample_binding_sampler = instance_generation->textureUploadSampleBindingSampler();
                const VkDescriptorSetLayout retained_texture_sample_binding_set_layout =
                    instance_generation->textureUploadSampleBindingDescriptorSetLayout();
                const VkPipelineLayout retained_texture_sample_binding_pipeline_layout =
                    instance_generation->textureUploadSampleBindingPipelineLayout();
                const VkDescriptorPool retained_texture_sample_binding_descriptor_pool =
                    instance_generation->textureUploadSampleBindingDescriptorPool();
                const VkDescriptorSet retained_texture_sample_binding_descriptor_set =
                    instance_generation->textureUploadSampleBindingDescriptorSet();
                const auto texture_sample_binding_retained = [&]() noexcept
                {
                    return instance_generation->hasTextureUploadSampleBindingGeneration() &&
                           instance_generation->textureUploadSampleBindingSamplerResourceHandle() ==
                               texture_sample_binding_description.mSampler.mHandle &&
                           instance_generation->textureUploadSampleBindingDestinationResourceHandle() == texture_description.mHandle &&
                           instance_generation->textureUploadSampleBindingExpectedRevision() == texture_description.mExpectedRevision &&
                           instance_generation->textureUploadSampleBindingResidentRevision() == texture_description.mExpectedRevision &&
                           instance_generation->textureUploadSampleBindingResidentContentIdentity() == retained_texture_source_identity &&
                           instance_generation->textureUploadSampleBindingDestinationImageView() ==
                               retained_texture_destination.mImageView &&
                           instance_generation->textureUploadSampleBindingDestinationImageLayout() ==
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
                           instance_generation->textureUploadSampleBindingDescriptorSetIndex() ==
                               texture_sample_binding_description.mDescriptorSetIndex &&
                           instance_generation->textureUploadSampleBindingBinding() == texture_sample_binding_description.mBinding &&
                           retained_texture_sample_binding_sampler != VK_NULL_HANDLE &&
                           instance_generation->textureUploadSampleBindingSampler() == retained_texture_sample_binding_sampler &&
                           retained_texture_sample_binding_set_layout != VK_NULL_HANDLE &&
                           instance_generation->textureUploadSampleBindingDescriptorSetLayout() ==
                               retained_texture_sample_binding_set_layout &&
                           retained_texture_sample_binding_pipeline_layout != VK_NULL_HANDLE &&
                           instance_generation->textureUploadSampleBindingPipelineLayout() ==
                               retained_texture_sample_binding_pipeline_layout &&
                           retained_texture_sample_binding_descriptor_pool != VK_NULL_HANDLE &&
                           instance_generation->textureUploadSampleBindingDescriptorPool() ==
                               retained_texture_sample_binding_descriptor_pool &&
                           retained_texture_sample_binding_descriptor_set != VK_NULL_HANDLE &&
                           instance_generation->textureUploadSampleBindingDescriptorSet() == retained_texture_sample_binding_descriptor_set;
                };
                texture_sample_binding_metadata_exact = texture_sample_binding_acquired && texture_sample_binding_retained();
                const LLRenderVulkan::VulkanUploadSourceDescription upload_source_description = fixedUploadSourceDescription();
                LLRenderVulkan::VulkanUploadSourceRequest           upload_source_request;
                upload_source_request.mNativeWindowGeneration = requirements->nativeWindowGeneration();
                upload_source_request.mDescription            = upload_source_description;
                upload_source_request.mInstanceOwnerCheck     = { &upload_source_context, frameSlotInstanceOwnerIsCurrent };
                upload_source_request.mWindowGenerationCheck  = { &upload_source_context, frameSlotWindowGenerationIsCurrent };
                upload_source_acquired                        = logical_device_acquired &&
                                         !mutable_generation->acquireUploadSourceGeneration(upload_source_request) &&
                                         instance_generation->hasUploadSourceGeneration();
                const LLRenderContract::BufferHandle retained_upload_source_handle     = instance_generation->uploadSourceResourceHandle();
                const std::uint64_t                  retained_upload_source_identity   = instance_generation->uploadSourceContentIdentity();
                const VkBuffer                       retained_upload_source_buffer     = instance_generation->uploadSourceBuffer();
                const VkDeviceMemory                 retained_upload_source_memory     = instance_generation->uploadSourceMemory();
                const VkDeviceSize                   retained_upload_source_byte_count = instance_generation->uploadSourceByteCount();
                const VkDeviceSize          retained_upload_source_allocation_size     = instance_generation->uploadSourceAllocationSize();
                const VkMemoryPropertyFlags retained_upload_source_memory_flags = instance_generation->uploadSourceMemoryPropertyFlags();
                const bool                  retained_upload_source_coherent     = instance_generation->uploadSourceIsCoherent();
                upload_source_metadata_exact =
                    upload_source_acquired && retained_upload_source_handle == upload_source_description.mHandle &&
                    retained_upload_source_identity != 0 && retained_upload_source_buffer != VK_NULL_HANDLE &&
                    retained_upload_source_memory != VK_NULL_HANDLE &&
                    retained_upload_source_byte_count == LLRenderVulkan::VULKAN_UPLOAD_SOURCE_BYTE_COUNT &&
                    retained_upload_source_allocation_size >= retained_upload_source_byte_count &&
                    (retained_upload_source_memory_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0 &&
                    retained_upload_source_coherent == ((retained_upload_source_memory_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0);
                texture_source_native_distinct = texture_source_metadata_exact && upload_source_metadata_exact &&
                                                 retained_texture_source_buffer != retained_upload_source_buffer &&
                                                 retained_texture_source_memory != retained_upload_source_memory;
                LLRenderVulkan::VulkanUploadDestinationRequest upload_destination_request;
                upload_destination_request.mNativeWindowGeneration = requirements->nativeWindowGeneration();
                upload_destination_request.mDescription            = upload_source_description;
                upload_destination_request.mInstanceOwnerCheck     = { &upload_source_context, frameSlotInstanceOwnerIsCurrent };
                upload_destination_request.mWindowGenerationCheck  = { &upload_source_context, frameSlotWindowGenerationIsCurrent };
                upload_destination_acquired                        = upload_source_metadata_exact &&
                                              !mutable_generation->acquireUploadDestinationGeneration(upload_destination_request) &&
                                              instance_generation->hasUploadDestinationGeneration();
                const LLRenderContract::BufferHandle retained_upload_destination_handle =
                    instance_generation->uploadDestinationResourceHandle();
                const std::uint64_t retained_upload_destination_expected_identity =
                    instance_generation->uploadDestinationExpectedContentIdentity();
                const VkBuffer       retained_upload_destination_buffer          = instance_generation->uploadDestinationBuffer();
                const VkDeviceMemory retained_upload_destination_memory          = instance_generation->uploadDestinationMemory();
                const VkDeviceSize   retained_upload_destination_byte_count      = instance_generation->uploadDestinationByteCount();
                const VkDeviceSize   retained_upload_destination_allocation_size = instance_generation->uploadDestinationAllocationSize();
                const std::uint32_t  retained_upload_destination_memory_type     = instance_generation->uploadDestinationMemoryTypeIndex();
                const VkMemoryPropertyFlags retained_upload_destination_memory_flags =
                    instance_generation->uploadDestinationMemoryPropertyFlags();
                const VkBufferUsageFlags retained_upload_destination_usage = instance_generation->uploadDestinationUsage();
                upload_destination_metadata_exact =
                    upload_destination_acquired && retained_upload_destination_handle == upload_source_description.mHandle &&
                    retained_upload_destination_expected_identity == retained_upload_source_identity &&
                    retained_upload_destination_buffer != VK_NULL_HANDLE && retained_upload_destination_memory != VK_NULL_HANDLE &&
                    retained_upload_destination_buffer != retained_upload_source_buffer &&
                    retained_upload_destination_memory != retained_upload_source_memory &&
                    retained_upload_destination_byte_count == LLRenderVulkan::VULKAN_UPLOAD_SOURCE_BYTE_COUNT &&
                    retained_upload_destination_allocation_size >= retained_upload_destination_byte_count &&
                    retained_upload_destination_usage == (VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) &&
                    (retained_upload_destination_memory_flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0 &&
                    instance_generation->uploadDestinationIsDeviceLocal() && !instance_generation->uploadDestinationIsMapped();
                upload_destination_initially_nonresident = upload_destination_metadata_exact &&
                                                           !instance_generation->uploadDestinationIsResident() &&
                                                           instance_generation->uploadDestinationResidentContentIdentity() == 0;

                LLRenderVulkan::VulkanUploadTransferRequest upload_transfer_request;
                upload_transfer_request.mNativeWindowGeneration = requirements->nativeWindowGeneration();
                upload_transfer_request.mDescription            = upload_source_description;
                upload_transfer_request.mInstanceOwnerCheck     = { &upload_source_context, frameSlotInstanceOwnerIsCurrent };
                upload_transfer_request.mWindowGenerationCheck  = { &upload_source_context, frameSlotWindowGenerationIsCurrent };
                upload_transfer_acquired                        = upload_destination_initially_nonresident &&
                                           !mutable_generation->acquireUploadTransferGeneration(upload_transfer_request) &&
                                           instance_generation->hasUploadTransferGeneration();
                const LLRenderContract::BufferHandle retained_upload_transfer_handle = instance_generation->uploadTransferResourceHandle();
                const std::uint64_t   retained_upload_transfer_identity              = instance_generation->uploadTransferContentIdentity();
                const VkBuffer        retained_upload_transfer_source                = instance_generation->uploadTransferSourceBuffer();
                const VkBuffer        retained_upload_transfer_destination    = instance_generation->uploadTransferDestinationBuffer();
                const VkQueue         retained_upload_transfer_queue          = instance_generation->uploadTransferQueue();
                const std::uint32_t   retained_upload_transfer_queue_family   = instance_generation->uploadTransferQueueFamilyIndex();
                const std::uint32_t   retained_upload_transfer_queue_index    = instance_generation->uploadTransferQueueIndex();
                const VkCommandPool   retained_upload_transfer_command_pool   = instance_generation->uploadTransferCommandPool();
                const VkCommandBuffer retained_upload_transfer_command_buffer = instance_generation->uploadTransferCommandBuffer();
                const VkFence         retained_upload_transfer_fence          = instance_generation->uploadTransferFence();
                upload_transfer_ready_exact =
                    upload_transfer_acquired && retained_upload_transfer_handle == upload_source_description.mHandle &&
                    retained_upload_transfer_identity == retained_upload_source_identity &&
                    retained_upload_transfer_source == retained_upload_source_buffer &&
                    retained_upload_transfer_destination == retained_upload_destination_buffer &&
                    retained_upload_transfer_queue != VK_NULL_HANDLE &&
                    retained_upload_transfer_queue == instance_generation->presentationQueue() &&
                    retained_upload_transfer_queue_family == instance_generation->presentationQueueFamilyIndex() &&
                    retained_upload_transfer_queue_index == 0 && retained_upload_transfer_command_pool != VK_NULL_HANDLE &&
                    retained_upload_transfer_command_buffer != VK_NULL_HANDLE && retained_upload_transfer_fence != VK_NULL_HANDLE &&
                    instance_generation->uploadTransferDisposition() == LLRenderVulkan::VulkanUploadTransferDisposition::Ready &&
                    instance_generation->uploadTransferSubmissionCount() == 0 &&
                    instance_generation->uploadTransferCompletionWaitCount() == 0;
                LLRenderVulkan::VulkanUploadTransferOperationRequest upload_transfer_operation_request;
                upload_transfer_operation_request.mNativeWindowGeneration = requirements->nativeWindowGeneration();
                upload_transfer_operation_request.mDescription            = upload_source_description;
                upload_transfer_operation_request.mInstanceOwnerCheck     = { &upload_source_context, frameSlotInstanceOwnerIsCurrent };
                upload_transfer_operation_request.mWindowGenerationCheck  = { &upload_source_context, frameSlotWindowGenerationIsCurrent };
                if (upload_transfer_ready_exact)
                {
                    const auto  upload_transfer_result = mutable_generation->executeUploadTransfer(upload_transfer_operation_request);
                    const auto* upload_transfer_disposition =
                        std::get_if<LLRenderVulkan::VulkanUploadTransferDisposition>(&upload_transfer_result);
                    upload_transfer_complete_exact =
                        upload_transfer_disposition &&
                        *upload_transfer_disposition == LLRenderVulkan::VulkanUploadTransferDisposition::Complete &&
                        instance_generation->uploadTransferDisposition() == LLRenderVulkan::VulkanUploadTransferDisposition::Complete &&
                        instance_generation->uploadTransferSubmissionCount() == 1 &&
                        instance_generation->uploadTransferCompletionWaitCount() == 1;
                }
                upload_destination_resident_exact =
                    upload_transfer_complete_exact && instance_generation->uploadDestinationIsResident() &&
                    instance_generation->uploadDestinationResidentContentIdentity() == retained_upload_source_identity &&
                    instance_generation->uploadDestinationResidentContentIdentity() == retained_upload_destination_expected_identity;
                const auto upload_destination_retained = [&]() noexcept
                {
                    return instance_generation->hasUploadDestinationGeneration() &&
                           instance_generation->uploadDestinationResourceHandle() == retained_upload_destination_handle &&
                           instance_generation->uploadDestinationExpectedContentIdentity() ==
                               retained_upload_destination_expected_identity &&
                           instance_generation->uploadDestinationResidentContentIdentity() == retained_upload_source_identity &&
                           instance_generation->uploadDestinationIsResident() &&
                           instance_generation->uploadDestinationBuffer() == retained_upload_destination_buffer &&
                           instance_generation->uploadDestinationMemory() == retained_upload_destination_memory &&
                           instance_generation->uploadDestinationByteCount() == retained_upload_destination_byte_count &&
                           instance_generation->uploadDestinationAllocationSize() == retained_upload_destination_allocation_size &&
                           instance_generation->uploadDestinationMemoryTypeIndex() == retained_upload_destination_memory_type &&
                           instance_generation->uploadDestinationMemoryPropertyFlags() == retained_upload_destination_memory_flags &&
                           instance_generation->uploadDestinationUsage() == retained_upload_destination_usage &&
                           instance_generation->uploadDestinationIsDeviceLocal() && !instance_generation->uploadDestinationIsMapped();
                };
                const auto upload_transfer_retained = [&]() noexcept
                {
                    return instance_generation->hasUploadTransferGeneration() &&
                           instance_generation->uploadTransferResourceHandle() == retained_upload_transfer_handle &&
                           instance_generation->uploadTransferContentIdentity() == retained_upload_transfer_identity &&
                           instance_generation->uploadTransferSourceBuffer() == retained_upload_transfer_source &&
                           instance_generation->uploadTransferDestinationBuffer() == retained_upload_transfer_destination &&
                           instance_generation->uploadTransferQueue() == retained_upload_transfer_queue &&
                           instance_generation->uploadTransferQueueFamilyIndex() == retained_upload_transfer_queue_family &&
                           instance_generation->uploadTransferQueueIndex() == retained_upload_transfer_queue_index &&
                           instance_generation->uploadTransferCommandPool() == retained_upload_transfer_command_pool &&
                           instance_generation->uploadTransferCommandBuffer() == retained_upload_transfer_command_buffer &&
                           instance_generation->uploadTransferFence() == retained_upload_transfer_fence &&
                           instance_generation->uploadTransferDisposition() == LLRenderVulkan::VulkanUploadTransferDisposition::Complete &&
                           instance_generation->uploadTransferSubmissionCount() == 1 &&
                           instance_generation->uploadTransferCompletionWaitCount() == 1;
                };
                frame_slot_initially_acquired = instance_generation->hasSwapchainFrameSlotGeneration() &&
                                                instance_generation->swapchainFrameCommandPool() != VK_NULL_HANDLE &&
                                                instance_generation->swapchainFrameCommandBuffer() != VK_NULL_HANDLE &&
                                                instance_generation->swapchainFrameImageAvailableSemaphore() != VK_NULL_HANDLE &&
                                                instance_generation->swapchainFramePresentationReadySemaphore() != VK_NULL_HANDLE &&
                                                instance_generation->swapchainFrameSubmissionFence() != VK_NULL_HANDLE &&
                                                instance_generation->swapchainFramePresentCompletionFence() != VK_NULL_HANDLE;
                frame_slot_initially_reset = frame_slot_initially_acquired && mutable_generation->resetSwapchainFrameSlotGeneration() &&
                                             !instance_generation->hasSwapchainFrameSlotGeneration();
                FrameSlotOperationContext presentation_target_context{ static_cast<const LLWindowSDL*>(window), instance_generation };
                const LLRenderVulkan::VulkanSwapchainPresentationTargetRequest presentation_target_request{
                    instance_generation->nativeWindowGeneration(),
                    retained_drawable,
                    { &presentation_target_context, frameSlotInstanceOwnerIsCurrent },
                    { &presentation_target_context, frameSlotWindowGenerationIsCurrent }
                };
                presentation_target_acquired =
                    frame_slot_initially_reset &&
                    !mutable_generation->acquireSwapchainPresentationTargetGeneration(presentation_target_request);
                presentation_target_complete = presentation_target_acquired &&
                                               instance_generation->swapchainPresentationRenderPass() != VK_NULL_HANDLE &&
                                               instance_generation->swapchainPresentationFramebufferCount() == image_count;
                for (std::uint32_t index = 0; presentation_target_complete && index < image_count; ++index)
                {
                    presentation_target_complete = instance_generation->swapchainPresentationFramebuffer(index) != VK_NULL_HANDLE;
                }
                presentation_target_complete =
                    presentation_target_complete && instance_generation->swapchainPresentationFramebuffer(image_count) == VK_NULL_HANDLE;
                presentation_target_provenance_exact = presentation_target_complete && swapchain_images_provenance_exact &&
                                                       instance_generation->swapchainImageExtent().width != 0 &&
                                                       instance_generation->swapchainImageExtent().height != 0;
                const LLRenderVulkan::VulkanSwapchainPresentationPipelineRequest presentation_pipeline_request{
                    instance_generation->nativeWindowGeneration(),
                    retained_drawable,
                    { &presentation_target_context, frameSlotInstanceOwnerIsCurrent },
                    { &presentation_target_context, frameSlotWindowGenerationIsCurrent }
                };
                presentation_pipeline_acquired =
                    presentation_target_acquired &&
                    !mutable_generation->acquireSwapchainPresentationPipelineGeneration(presentation_pipeline_request);
                presentation_pipeline_handles_nonnull = presentation_pipeline_acquired &&
                                                        instance_generation->swapchainPresentationPipelineLayout() != VK_NULL_HANDLE &&
                                                        instance_generation->swapchainPresentationPipeline() != VK_NULL_HANDLE;
                const LLRenderVulkan::VulkanSwapchainReadbackRequest readback_request{
                    instance_generation->nativeWindowGeneration(),
                    retained_drawable,
                    { &presentation_target_context, frameSlotInstanceOwnerIsCurrent },
                    { &presentation_target_context, frameSlotWindowGenerationIsCurrent }
                };
                readback_acquired =
                    presentation_pipeline_handles_nonnull && !mutable_generation->acquireSwapchainReadbackGeneration(readback_request);
                readback_metadata_exact =
                    readback_acquired && instance_generation->swapchainReadbackBuffer() != VK_NULL_HANDLE &&
                    instance_generation->swapchainReadbackMemory() != VK_NULL_HANDLE && instance_generation->swapchainReadbackIsMapped() &&
                    instance_generation->swapchainReadbackImageFormat() == surface_format.format &&
                    instance_generation->swapchainReadbackImageExtent().width == image_extent.width &&
                    instance_generation->swapchainReadbackImageExtent().height == image_extent.height &&
                    instance_generation->swapchainReadbackImageCount() == image_count &&
                    instance_generation->swapchainReadbackRowBytes() == static_cast<VkDeviceSize>(image_extent.width) * 4 &&
                    instance_generation->swapchainReadbackByteCount() ==
                        static_cast<VkDeviceSize>(image_extent.width) * image_extent.height * 4 &&
                    (instance_generation->swapchainReadbackMemoryPropertyFlags() &
                     (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                        (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
                const LLRenderVulkan::VulkanSwapchainFrameSlotRequest frame_slot_request{
                    instance_generation->nativeWindowGeneration(),
                    retained_drawable,
                    { &presentation_target_context, frameSlotInstanceOwnerIsCurrent },
                    { &presentation_target_context, frameSlotWindowGenerationIsCurrent }
                };
                frame_slot_acquired = readback_metadata_exact &&
                                      !mutable_generation->acquireSwapchainFrameSlotGeneration(frame_slot_request) &&
                                      instance_generation->hasSwapchainFrameSlotGeneration();
                frame_slot_handles_nonnull = instance_generation->swapchainFrameCommandPool() != VK_NULL_HANDLE &&
                                             instance_generation->swapchainFrameCommandBuffer() != VK_NULL_HANDLE &&
                                             instance_generation->swapchainFrameImageAvailableSemaphore() != VK_NULL_HANDLE &&
                                             instance_generation->swapchainFramePresentationReadySemaphore() != VK_NULL_HANDLE &&
                                             instance_generation->swapchainFrameSubmissionFence() != VK_NULL_HANDLE &&
                                             instance_generation->swapchainFramePresentCompletionFence() != VK_NULL_HANDLE;
                frame_slot_provenance_exact = frame_slot_acquired && frame_slot_handles_nonnull && swapchain_images_provenance_exact &&
                                              instance_generation->logicalDevice() != VK_NULL_HANDLE &&
                                              instance_generation->presentationQueueFamilyIndex() != VK_QUEUE_FAMILY_IGNORED;

                const auto presentation_chain_retained = [&]() noexcept
                {
                    return instance_generation->hasSurfaceGeneration() && instance_generation->surface() != VK_NULL_HANDLE &&
                           instance_generation->hasPresentationDeviceGeneration() &&
                           instance_generation->physicalDevice() != VK_NULL_HANDLE && instance_generation->hasLogicalDeviceGeneration() &&
                           instance_generation->logicalDevice() != VK_NULL_HANDLE &&
                           instance_generation->presentationQueue() != VK_NULL_HANDLE &&
                           instance_generation->hasSwapchainConfigurationGeneration() && instance_generation->hasSwapchainGeneration() &&
                           instance_generation->swapchain() != VK_NULL_HANDLE && instance_generation->hasSwapchainImagesGeneration() &&
                           instance_generation->resolvedSwapchainImageCount() != 0 &&
                           instance_generation->hasSwapchainPresentationTargetGeneration() &&
                           instance_generation->swapchainPresentationRenderPass() != VK_NULL_HANDLE &&
                           instance_generation->swapchainPresentationFramebufferCount() ==
                               instance_generation->resolvedSwapchainImageCount() &&
                           instance_generation->swapchainPresentationFramebuffer(0) != VK_NULL_HANDLE &&
                           instance_generation->hasSwapchainPresentationPipelineGeneration() &&
                           instance_generation->swapchainPresentationPipelineLayout() != VK_NULL_HANDLE &&
                           instance_generation->swapchainPresentationPipeline() != VK_NULL_HANDLE &&
                           instance_generation->hasSwapchainReadbackGeneration() &&
                           instance_generation->swapchainReadbackBuffer() != VK_NULL_HANDLE &&
                           instance_generation->swapchainReadbackMemory() != VK_NULL_HANDLE &&
                           instance_generation->swapchainReadbackIsMapped() && instance_generation->hasSwapchainFrameSlotGeneration() &&
                           instance_generation->swapchainFrameCommandPool() != VK_NULL_HANDLE &&
                           instance_generation->swapchainFrameCommandBuffer() != VK_NULL_HANDLE &&
                           instance_generation->swapchainFrameImageAvailableSemaphore() != VK_NULL_HANDLE &&
                           instance_generation->swapchainFramePresentationReadySemaphore() != VK_NULL_HANDLE &&
                           instance_generation->swapchainFrameSubmissionFence() != VK_NULL_HANDLE &&
                           instance_generation->swapchainFramePresentCompletionFence() != VK_NULL_HANDLE &&
                           instance_generation->swapchainFrameSlotDisposition() ==
                               LLRenderVulkan::VulkanSwapchainFrameSlotDisposition::Reusable &&
                           !instance_generation->swapchainFrameAcquiredImageIndex();
                };

                if (frame_slot_provenance_exact && drawable_queried)
                {
                    FrameSlotOperationContext operation_context{ static_cast<const LLWindowSDL*>(window), instance_generation };
                    const VkInstance          retained_instance        = instance_generation->instance();
                    const VkSurfaceKHR        retained_surface         = instance_generation->surface();
                    const VkPhysicalDevice    retained_physical_device = instance_generation->physicalDevice();
                    const VkDevice            retained_logical_device  = instance_generation->logicalDevice();
                    const VkQueue             retained_queue           = instance_generation->presentationQueue();

                    LLRenderVulkan::VulkanSwapchainFrameClearColor clear_color;
                    clear_color.mRgba = { 0.125f, 0.375f, 0.625f, 1.0f };
                    LLRenderVulkan::VulkanSwapchainFrameSlotOperationRequest operation_request;
                    operation_request.mNativeWindowGeneration = requirements->nativeWindowGeneration();
                    operation_request.mDrawableExtent         = retained_drawable;
                    operation_request.mInstanceOwnerCheck     = { &operation_context, frameSlotInstanceOwnerIsCurrent };
                    operation_request.mWindowGenerationCheck  = { &operation_context, frameSlotWindowGenerationIsCurrent };

                    const std::uint32_t initial_image_count     = image_count;
                    const VkFormat      initial_image_format    = instance_generation->swapchainSurfaceFormat().format;
                    const VkExtent2D    initial_image_extent    = instance_generation->swapchainImageExtent();
                    const VkFormat      initial_readback_format = instance_generation->swapchainReadbackImageFormat();
                    const VkExtent2D    initial_readback_extent = instance_generation->swapchainReadbackImageExtent();

                    const auto first_clear_before_rebuild =
                        mutable_generation->acquireClearToPresentSwapchainFrameSlot(operation_request, clear_color);
                    frame_slot_transfer_clear_before_rebuild = presentationCompleted(first_clear_before_rebuild, image_count) &&
                                                               instance_generation->swapchainFrameSlotDisposition() ==
                                                                   LLRenderVulkan::VulkanSwapchainFrameSlotDisposition::Reusable &&
                                                               !instance_generation->swapchainFrameAcquiredImageIndex();
                    const auto second_clear_before_rebuild =
                        mutable_generation->acquireRenderPassClearToPresentSwapchainFrameSlot(operation_request, clear_color);
                    frame_slot_render_pass_clear_before_rebuild = presentationCompleted(second_clear_before_rebuild, image_count) &&
                                                                  instance_generation->swapchainFrameSlotDisposition() ==
                                                                      LLRenderVulkan::VulkanSwapchainFrameSlotDisposition::Reusable &&
                                                                  !instance_generation->swapchainFrameAcquiredImageIndex();
                    const auto upload_transfer_retry_result =
                        mutable_generation->retryUploadTransferCompletion(upload_transfer_operation_request);
                    const auto* upload_transfer_retry_error =
                        std::get_if<LLRenderVulkan::VulkanUploadTransferParentOperationError>(&upload_transfer_retry_result);
                    upload_transfer_complete_retry_rejected =
                        upload_transfer_retry_error &&
                        upload_transfer_retry_error->mCode == LLRenderVulkan::VulkanUploadTransferParentOperationCode::OperationFailure &&
                        upload_transfer_retry_error->mOperationError &&
                        upload_transfer_retry_error->mOperationError->mCode ==
                            LLRenderVulkan::VulkanUploadTransferOperationCode::InvalidDisposition &&
                        upload_transfer_retry_error->mOperationError->mDisposition ==
                            LLRenderVulkan::VulkanUploadTransferDisposition::Complete;
                    upload_transfer_not_resubmitted = upload_transfer_complete_retry_rejected && upload_transfer_retained();
                    upload_source_explicitly_reset  = upload_transfer_not_resubmitted && mutable_generation->resetUploadSourceGeneration();
                    upload_source_removed =
                        upload_source_explicitly_reset && !instance_generation->hasUploadSourceGeneration() &&
                        !instance_generation->uploadSourceResourceHandle() && instance_generation->uploadSourceContentIdentity() == 0 &&
                        instance_generation->uploadSourceBuffer() == VK_NULL_HANDLE &&
                        instance_generation->uploadSourceMemory() == VK_NULL_HANDLE && instance_generation->uploadSourceByteCount() == 0 &&
                        instance_generation->uploadSourceAllocationSize() == 0 && instance_generation->uploadSourceMemoryTypeIndex() == 0 &&
                        instance_generation->uploadSourceMemoryPropertyFlags() == 0 && !instance_generation->uploadSourceIsCoherent();
                    upload_transfer_removed_with_source =
                        upload_source_explicitly_reset && !instance_generation->hasUploadTransferGeneration() &&
                        !instance_generation->uploadTransferResourceHandle() && instance_generation->uploadTransferContentIdentity() == 0 &&
                        instance_generation->uploadTransferSourceBuffer() == VK_NULL_HANDLE &&
                        instance_generation->uploadTransferDestinationBuffer() == VK_NULL_HANDLE &&
                        instance_generation->uploadTransferQueue() == VK_NULL_HANDLE &&
                        instance_generation->uploadTransferQueueFamilyIndex() == VK_QUEUE_FAMILY_IGNORED &&
                        instance_generation->uploadTransferQueueIndex() == std::numeric_limits<std::uint32_t>::max() &&
                        instance_generation->uploadTransferCommandPool() == VK_NULL_HANDLE &&
                        instance_generation->uploadTransferCommandBuffer() == VK_NULL_HANDLE &&
                        instance_generation->uploadTransferFence() == VK_NULL_HANDLE &&
                        instance_generation->uploadTransferSubmissionCount() == 0 &&
                        instance_generation->uploadTransferCompletionWaitCount() == 0 && !instance_generation->uploadTransferDisposition();
                    upload_destination_source_retirement_retained =
                        upload_source_removed && upload_transfer_removed_with_source && upload_destination_retained();
                    upload_source_chain_retained = upload_destination_source_retirement_retained && presentation_chain_retained();
                    upload_source_validation_clean =
                        upload_source_chain_retained && instance_generation->validationSnapshot().mMessageCount == 0;
                    const auto draw_readback_before_rebuild =
                        mutable_generation->acquireRenderPassDrawReadbackToPresentSwapchainFrameSlot(operation_request);
                    frame_slot_render_pass_draw_readback_before_rebuild =
                        presentationObserved(draw_readback_before_rebuild,
                                             initial_image_count,
                                             initial_image_format,
                                             initial_image_extent,
                                             initial_readback_format,
                                             initial_readback_extent) &&
                        instance_generation->swapchainFrameSlotDisposition() ==
                            LLRenderVulkan::VulkanSwapchainFrameSlotDisposition::Reusable &&
                        !instance_generation->swapchainFrameAcquiredImageIndex();
                    upload_destination_draw_retained = frame_slot_render_pass_draw_readback_before_rebuild &&
                                                       upload_destination_resident_exact && upload_destination_source_retirement_retained &&
                                                       upload_destination_retained();

                    const LLCoordScreen requested_size(current_drawable.mX + 32, current_drawable.mY + 24);
                    swapchain_resize_requested    = window->setSize(requested_size);
                    swapchain_resize_synchronized = swapchain_resize_requested && native_sdl_window && SDL_SyncWindow(native_sdl_window);
                    LLCoordWindow resized_drawable;
                    swapchain_resize_observed = window->getSize(&resized_drawable) && resized_drawable.mX > 0 && resized_drawable.mY > 0 &&
                                                resized_drawable.mX != current_drawable.mX && resized_drawable.mY != current_drawable.mY;

                    LLRenderVulkan::VulkanSwapchainChainRebuildRequest rebuild_request;
                    rebuild_request.mNativeWindowGeneration = requirements->nativeWindowGeneration();
                    if (swapchain_resize_observed)
                    {
                        rebuild_request.mDrawableExtent =
                            VkExtent2D{ static_cast<std::uint32_t>(resized_drawable.mX), static_cast<std::uint32_t>(resized_drawable.mY) };
                    }
                    rebuild_request.mInstanceOwnerCheck    = { &operation_context, frameSlotInstanceOwnerIsCurrent };
                    rebuild_request.mWindowGenerationCheck = { &operation_context, frameSlotWindowGenerationIsCurrent };

                    // The native proof calls the diagnostic core operation directly.
                    // LLWindowSDL keeps its production forwarding API unchanged.
                    const auto  rebuild_result  = mutable_generation->rebuildSwapchainChain(rebuild_request);
                    const auto* rebuild_outcome = std::get_if<LLRenderVulkan::VulkanSwapchainChainRebuildOutcome>(&rebuild_result);
                    swapchain_rebuild_ready =
                        rebuild_outcome && *rebuild_outcome == LLRenderVulkan::VulkanSwapchainChainRebuildOutcome::Ready;
                    swapchain_rebuild_parent_exact = instance_generation->instance() == retained_instance &&
                                                     instance_generation->surface() == retained_surface &&
                                                     instance_generation->physicalDevice() == retained_physical_device &&
                                                     instance_generation->logicalDevice() == retained_logical_device &&
                                                     instance_generation->presentationQueue() == retained_queue;
                    image_count                       = instance_generation->resolvedSwapchainImageCount();
                    bool rebuilt_image_views_complete = image_count != 0;
                    for (std::uint32_t index = 0; rebuilt_image_views_complete && index < image_count; ++index)
                    {
                        rebuilt_image_views_complete = instance_generation->swapchainImage(index) != VK_NULL_HANDLE &&
                                                       instance_generation->swapchainImageView(index) != VK_NULL_HANDLE;
                    }
                    swapchain_rebuild_chain_complete =
                        instance_generation->hasSwapchainConfigurationGeneration() && instance_generation->hasSwapchainGeneration() &&
                        instance_generation->swapchain() != VK_NULL_HANDLE && instance_generation->hasSwapchainImagesGeneration() &&
                        rebuilt_image_views_complete && instance_generation->hasSwapchainPresentationTargetGeneration() &&
                        instance_generation->swapchainPresentationRenderPass() != VK_NULL_HANDLE &&
                        instance_generation->swapchainPresentationFramebufferCount() == image_count &&
                        instance_generation->swapchainPresentationFramebuffer(0) != VK_NULL_HANDLE &&
                        instance_generation->hasSwapchainPresentationPipelineGeneration() &&
                        instance_generation->swapchainPresentationPipelineLayout() != VK_NULL_HANDLE &&
                        instance_generation->swapchainPresentationPipeline() != VK_NULL_HANDLE &&
                        instance_generation->hasSwapchainReadbackGeneration() &&
                        instance_generation->swapchainReadbackBuffer() != VK_NULL_HANDLE &&
                        instance_generation->swapchainReadbackMemory() != VK_NULL_HANDLE &&
                        instance_generation->swapchainReadbackIsMapped() && instance_generation->hasSwapchainFrameSlotGeneration() &&
                        instance_generation->swapchainFrameCommandPool() != VK_NULL_HANDLE &&
                        instance_generation->swapchainFrameCommandBuffer() != VK_NULL_HANDLE;
                    presentation_pipeline_rebuilt_nonnull = swapchain_rebuild_chain_complete &&
                                                            instance_generation->swapchainPresentationPipelineLayout() != VK_NULL_HANDLE &&
                                                            instance_generation->swapchainPresentationPipeline() != VK_NULL_HANDLE;
                    const VkExtent2D rebuilt_drawable = instance_generation->swapchainDrawableExtent();
                    swapchain_rebuild_extent_exact    = swapchain_resize_observed &&
                                                     rebuilt_drawable.width == static_cast<std::uint32_t>(resized_drawable.mX) &&
                                                     rebuilt_drawable.height == static_cast<std::uint32_t>(resized_drawable.mY);
                    const VkExtent2D rebuilt_image_extent = instance_generation->swapchainImageExtent();
                    readback_rebuilt_exact =
                        swapchain_rebuild_chain_complete &&
                        instance_generation->swapchainReadbackImageFormat() == instance_generation->swapchainSurfaceFormat().format &&
                        instance_generation->swapchainReadbackImageExtent().width == rebuilt_image_extent.width &&
                        instance_generation->swapchainReadbackImageExtent().height == rebuilt_image_extent.height &&
                        instance_generation->swapchainReadbackImageCount() == image_count &&
                        instance_generation->swapchainReadbackRowBytes() == static_cast<VkDeviceSize>(rebuilt_image_extent.width) * 4 &&
                        instance_generation->swapchainReadbackByteCount() ==
                            static_cast<VkDeviceSize>(rebuilt_image_extent.width) * rebuilt_image_extent.height * 4;
                    upload_destination_rebuild_retained = swapchain_rebuild_ready && swapchain_rebuild_chain_complete &&
                                                          upload_destination_draw_retained && upload_destination_retained();
                    texture_destination_rebuild_retained =
                        swapchain_rebuild_ready && swapchain_rebuild_chain_complete && texture_destination_metadata_exact &&
                        textureDestinationMatches(*instance_generation, texture_description, retained_texture_destination);
                    texture_source_rebuild_retained = swapchain_rebuild_ready && swapchain_rebuild_chain_complete &&
                                                      texture_source_native_distinct && texture_source_retained();
                    texture_transfer_rebuild_retained =
                        swapchain_rebuild_ready && swapchain_rebuild_chain_complete && texture_destination_resident_exact &&
                        texture_transfer_retained() && instance_generation->textureUploadDestinationIsResident() &&
                        instance_generation->textureUploadDestinationResidentContentIdentity() == retained_texture_source_identity;
                    texture_sample_binding_rebuild_retained = swapchain_rebuild_ready && swapchain_rebuild_chain_complete &&
                                                              texture_sample_binding_metadata_exact && texture_sample_binding_retained();
                    frame_slot_initial_observation_detached = presentationObserved(draw_readback_before_rebuild,
                                                                                   initial_image_count,
                                                                                   initial_image_format,
                                                                                   initial_image_extent,
                                                                                   initial_readback_format,
                                                                                   initial_readback_extent);

                    const VkSemaphore image_available          = instance_generation->swapchainFrameImageAvailableSemaphore();
                    const VkSemaphore presentation_ready       = instance_generation->swapchainFramePresentationReadySemaphore();
                    const VkFence     submission_fence         = instance_generation->swapchainFrameSubmissionFence();
                    const VkFence     present_completion_fence = instance_generation->swapchainFramePresentCompletionFence();
                    operation_request.mDrawableExtent          = rebuilt_drawable;

                    if (swapchain_rebuild_ready && swapchain_rebuild_chain_complete && swapchain_rebuild_extent_exact)
                    {
                        const auto first_clear_after_rebuild =
                            mutable_generation->acquireClearToPresentSwapchainFrameSlot(operation_request, clear_color);
                        frame_slot_transfer_clear_after_rebuild = presentationCompleted(first_clear_after_rebuild, image_count) &&
                                                                  instance_generation->swapchainFrameSlotDisposition() ==
                                                                      LLRenderVulkan::VulkanSwapchainFrameSlotDisposition::Reusable &&
                                                                  !instance_generation->swapchainFrameAcquiredImageIndex();
                        const auto second_clear_after_rebuild =
                            mutable_generation->acquireRenderPassClearToPresentSwapchainFrameSlot(operation_request, clear_color);
                        frame_slot_render_pass_clear_after_rebuild = presentationCompleted(second_clear_after_rebuild, image_count) &&
                                                                     instance_generation->swapchainFrameSlotDisposition() ==
                                                                         LLRenderVulkan::VulkanSwapchainFrameSlotDisposition::Reusable &&
                                                                     !instance_generation->swapchainFrameAcquiredImageIndex();
                        const auto draw_readback_after_rebuild =
                            mutable_generation->acquireRenderPassDrawReadbackToPresentSwapchainFrameSlot(operation_request);
                        frame_slot_render_pass_draw_readback_after_rebuild =
                            presentationObserved(draw_readback_after_rebuild,
                                                 image_count,
                                                 instance_generation->swapchainSurfaceFormat().format,
                                                 rebuilt_image_extent,
                                                 instance_generation->swapchainReadbackImageFormat(),
                                                 instance_generation->swapchainReadbackImageExtent()) &&
                            instance_generation->swapchainFrameSlotDisposition() ==
                                LLRenderVulkan::VulkanSwapchainFrameSlotDisposition::Reusable &&
                            !instance_generation->swapchainFrameAcquiredImageIndex();
                    }
                    upload_destination_rebuilt_draw_retained = frame_slot_render_pass_draw_readback_after_rebuild &&
                                                               upload_destination_rebuild_retained && upload_destination_retained();
                    frame_slot_handles_untouched = image_available != VK_NULL_HANDLE && presentation_ready != VK_NULL_HANDLE &&
                                                   submission_fence != VK_NULL_HANDLE && present_completion_fence != VK_NULL_HANDLE &&
                                                   instance_generation->swapchainFrameImageAvailableSemaphore() == image_available &&
                                                   instance_generation->swapchainFramePresentationReadySemaphore() == presentation_ready &&
                                                   instance_generation->swapchainFrameSubmissionFence() == submission_fence &&
                                                   instance_generation->swapchainFramePresentCompletionFence() == present_completion_fence;
                    frame_slot_presentation_clean = instance_generation->validationSnapshot().mMessageCount == 0;
                }

                upload_destination_explicitly_reset =
                    upload_source_validation_clean && mutable_generation->resetUploadDestinationGeneration();
                upload_destination_removed =
                    upload_destination_explicitly_reset && !instance_generation->hasUploadDestinationGeneration() &&
                    !instance_generation->uploadDestinationResourceHandle() &&
                    instance_generation->uploadDestinationExpectedContentIdentity() == 0 &&
                    instance_generation->uploadDestinationResidentContentIdentity() == 0 &&
                    !instance_generation->uploadDestinationIsResident() &&
                    instance_generation->uploadDestinationBuffer() == VK_NULL_HANDLE &&
                    instance_generation->uploadDestinationMemory() == VK_NULL_HANDLE &&
                    instance_generation->uploadDestinationByteCount() == 0 && instance_generation->uploadDestinationUsage() == 0 &&
                    instance_generation->uploadDestinationAllocationSize() == 0 &&
                    instance_generation->uploadDestinationMemoryTypeIndex() == 0 &&
                    instance_generation->uploadDestinationMemoryPropertyFlags() == 0 &&
                    !instance_generation->uploadDestinationIsDeviceLocal() && !instance_generation->uploadDestinationIsMapped();
                upload_destination_chain_retained = upload_destination_removed && presentation_chain_retained();
                upload_destination_validation_clean =
                    upload_destination_chain_retained && instance_generation->validationSnapshot().mMessageCount == 0;
                texture_transfer_explicitly_reset =
                    texture_transfer_rebuild_retained && mutable_generation->resetTextureUploadTransferGeneration() &&
                    !instance_generation->hasTextureUploadTransferGeneration() && instance_generation->hasTextureUploadSourceGeneration() &&
                    texture_source_retained() &&
                    textureDestinationMatches(*instance_generation, texture_description, retained_texture_destination) &&
                    instance_generation->textureUploadDestinationIsResident() &&
                    instance_generation->textureUploadDestinationResidentContentIdentity() == retained_texture_source_identity;
                texture_transfer_validation_clean =
                    texture_transfer_explicitly_reset && instance_generation->validationSnapshot().mMessageCount == 0;
                texture_sample_binding_transfer_reset_retained =
                    texture_transfer_validation_clean && texture_sample_binding_rebuild_retained && texture_sample_binding_retained();
                texture_source_explicitly_reset =
                    texture_sample_binding_transfer_reset_retained && mutable_generation->resetTextureUploadSourceGeneration();
                texture_source_removed = texture_source_explicitly_reset && !instance_generation->hasTextureUploadSourceGeneration() &&
                                         !instance_generation->textureUploadSourceResourceHandle() &&
                                         instance_generation->textureUploadSourceExpectedRevision() == 0 &&
                                         instance_generation->textureUploadSourceResidentExtent().mWidth == 0 &&
                                         instance_generation->textureUploadSourceResidentExtent().mHeight == 0 &&
                                         instance_generation->textureUploadSourceRowPitch() == 0 &&
                                         instance_generation->textureUploadSourceContentIdentity() == 0 &&
                                         instance_generation->textureUploadSourceFlags() == VK_BUFFER_CREATE_FLAG_BITS_MAX_ENUM &&
                                         instance_generation->textureUploadSourceUsage() == 0 &&
                                         instance_generation->textureUploadSourceSharingMode() == VK_SHARING_MODE_MAX_ENUM &&
                                         instance_generation->textureUploadSourceBuffer() == VK_NULL_HANDLE &&
                                         instance_generation->textureUploadSourceMemory() == VK_NULL_HANDLE &&
                                         instance_generation->textureUploadSourceByteCount() == 0 &&
                                         instance_generation->textureUploadSourceAllocationSize() == 0 &&
                                         instance_generation->textureUploadSourceMemoryTypeIndex() == 0 &&
                                         instance_generation->textureUploadSourceMemoryPropertyFlags() == 0 &&
                                         !instance_generation->textureUploadSourceIsCoherent();
                texture_source_destination_retained =
                    texture_source_removed &&
                    textureDestinationMatches(*instance_generation, texture_description, retained_texture_destination);
                texture_source_chain_retained = texture_source_destination_retained && presentation_chain_retained();
                texture_source_validation_clean =
                    texture_source_chain_retained && instance_generation->validationSnapshot().mMessageCount == 0;
                texture_sample_binding_source_reset_retained =
                    texture_source_validation_clean && texture_sample_binding_transfer_reset_retained && texture_sample_binding_retained();
                texture_sample_binding_explicitly_reset =
                    texture_sample_binding_source_reset_retained && mutable_generation->resetTextureUploadSampleBindingGeneration();
                texture_sample_binding_removed =
                    texture_sample_binding_explicitly_reset && !instance_generation->hasTextureUploadSampleBindingGeneration() &&
                    !instance_generation->textureUploadSampleBindingSamplerResourceHandle() &&
                    !instance_generation->textureUploadSampleBindingDestinationResourceHandle() &&
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
                    instance_generation->textureUploadSampleBindingDescriptorSet() == VK_NULL_HANDLE;
                texture_sample_binding_destination_retained =
                    texture_sample_binding_removed &&
                    textureDestinationMatches(*instance_generation, texture_description, retained_texture_destination) &&
                    instance_generation->textureUploadDestinationIsResident() &&
                    instance_generation->textureUploadDestinationResidentRevision() == texture_description.mExpectedRevision &&
                    instance_generation->textureUploadDestinationResidentContentIdentity() == retained_texture_source_identity &&
                    instance_generation->textureUploadDestinationCurrentState() == LLRenderContract::ImageState::ShaderRead;
                texture_sample_binding_chain_retained = texture_sample_binding_destination_retained && presentation_chain_retained();
                texture_sample_binding_validation_clean =
                    texture_sample_binding_chain_retained && instance_generation->validationSnapshot().mMessageCount == 0;
                texture_destination_explicitly_reset =
                    texture_sample_binding_validation_clean && mutable_generation->resetTextureUploadDestinationGeneration();
                texture_destination_removed =
                    texture_destination_explicitly_reset && !instance_generation->hasTextureUploadDestinationGeneration() &&
                    !instance_generation->textureUploadDestinationResourceHandle() &&
                    instance_generation->textureUploadDestinationExpectedRevision() == 0 &&
                    !instance_generation->textureUploadDestinationIsResident() &&
                    instance_generation->textureUploadDestinationResidentRevision() == 0 &&
                    instance_generation->textureUploadDestinationResidentContentIdentity() == 0 &&
                    instance_generation->textureUploadDestinationCurrentState() == LLRenderContract::ImageState::Undefined &&
                    instance_generation->textureUploadDestinationResidentExtent().width == 0 &&
                    instance_generation->textureUploadDestinationResidentExtent().height == 0 &&
                    instance_generation->textureUploadDestinationResidentExtent().depth == 0 &&
                    instance_generation->textureUploadDestinationLogicalExtent().mWidth == 0 &&
                    instance_generation->textureUploadDestinationLogicalExtent().mHeight == 0 &&
                    instance_generation->textureUploadDestinationResidentDiscard() == 0 &&
                    instance_generation->textureUploadDestinationImage() == VK_NULL_HANDLE &&
                    instance_generation->textureUploadDestinationMemory() == VK_NULL_HANDLE &&
                    instance_generation->textureUploadDestinationImageView() == VK_NULL_HANDLE &&
                    instance_generation->textureUploadDestinationAllocationSize() == 0 &&
                    instance_generation->textureUploadDestinationUsage() == 0;
                texture_destination_chain_retained = texture_destination_removed && presentation_chain_retained();
                texture_destination_validation_clean =
                    texture_destination_chain_retained && instance_generation->validationSnapshot().mMessageCount == 0;

                const auto& required_extensions = requirements->requiredInstanceExtensions();
                const auto& enabled_extensions  = instance_generation->enabledExtensions();
                instance_extensions_ordered     = enabled_extensions.size() >= required_extensions.size();
                for (std::size_t index = 0; instance_extensions_ordered && index < required_extensions.size(); ++index)
                {
                    instance_extensions_ordered = enabled_extensions[index] == required_extensions[index];
                }
                instance_diagnostic_extensions =
                    enabled_extensions.size() >= required_extensions.size() + 2 &&
                    enabled_extensions[required_extensions.size()] == VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME &&
                    enabled_extensions[required_extensions.size() + 1] == VK_KHR_SURFACE_MAINTENANCE_1_EXTENSION_NAME;
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
        frame_slot_explicitly_reset =
            const_cast<LLRenderVulkan::VulkanInstanceGeneration*>(owned_instance_generation)->resetSwapchainFrameSlotGeneration();
        frame_slot_removed = !owned_instance_generation->hasSwapchainFrameSlotGeneration() &&
                             owned_instance_generation->swapchainFrameCommandPool() == VK_NULL_HANDLE &&
                             owned_instance_generation->swapchainFrameCommandBuffer() == VK_NULL_HANDLE &&
                             owned_instance_generation->swapchainFrameImageAvailableSemaphore() == VK_NULL_HANDLE &&
                             owned_instance_generation->swapchainFramePresentationReadySemaphore() == VK_NULL_HANDLE &&
                             owned_instance_generation->swapchainFrameSubmissionFence() == VK_NULL_HANDLE &&
                             owned_instance_generation->swapchainFramePresentCompletionFence() == VK_NULL_HANDLE &&
                             !owned_instance_generation->swapchainFrameAcquiredImageIndex();
        readback_explicitly_reset =
            const_cast<LLRenderVulkan::VulkanInstanceGeneration*>(owned_instance_generation)->resetSwapchainReadbackGeneration();
        readback_removed = !owned_instance_generation->hasSwapchainReadbackGeneration() &&
                           owned_instance_generation->swapchainReadbackBuffer() == VK_NULL_HANDLE &&
                           owned_instance_generation->swapchainReadbackMemory() == VK_NULL_HANDLE &&
                           !owned_instance_generation->swapchainReadbackIsMapped();
        readback_siblings_retained =
            owned_instance_generation->hasSwapchainPresentationPipelineGeneration() &&
            owned_instance_generation->hasSwapchainPresentationTargetGeneration() &&
            owned_instance_generation->hasSwapchainImagesGeneration() && owned_instance_generation->hasSwapchainGeneration() &&
            owned_instance_generation->hasSwapchainConfigurationGeneration() && owned_instance_generation->hasLogicalDeviceGeneration() &&
            owned_instance_generation->hasSurfaceGeneration();
        presentation_pipeline_explicitly_reset = const_cast<LLRenderVulkan::VulkanInstanceGeneration*>(owned_instance_generation)
                                                     ->resetSwapchainPresentationPipelineGeneration();
        presentation_target_explicitly_reset =
            const_cast<LLRenderVulkan::VulkanInstanceGeneration*>(owned_instance_generation)->resetSwapchainPresentationTargetGeneration();
        surface_explicitly_reset = static_cast<LLWindowSDL*>(window)->resetVulkanSurfaceGeneration();
        surface_removed = !owned_instance_generation->hasSurfaceGeneration() && owned_instance_generation->surface() == VK_NULL_HANDLE;
        presentation_device_removed = !owned_instance_generation->hasPresentationDeviceGeneration() &&
                                      owned_instance_generation->physicalDevice() == VK_NULL_HANDLE &&
                                      owned_instance_generation->presentationQueueFamilyIndex() == VK_QUEUE_FAMILY_IGNORED;
        logical_device_removed = !owned_instance_generation->hasLogicalDeviceGeneration() &&
                                 owned_instance_generation->logicalDevice() == VK_NULL_HANDLE &&
                                 owned_instance_generation->presentationQueue() == VK_NULL_HANDLE;
        presentation_pipeline_removed = !owned_instance_generation->hasSwapchainPresentationPipelineGeneration() &&
                                        owned_instance_generation->swapchainPresentationPipelineLayout() == VK_NULL_HANDLE &&
                                        owned_instance_generation->swapchainPresentationPipeline() == VK_NULL_HANDLE;
        presentation_target_removed = !owned_instance_generation->hasSwapchainPresentationTargetGeneration() &&
                                      owned_instance_generation->swapchainPresentationRenderPass() == VK_NULL_HANDLE &&
                                      owned_instance_generation->swapchainPresentationFramebufferCount() == 0 &&
                                      owned_instance_generation->swapchainPresentationFramebuffer(0) == VK_NULL_HANDLE;
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
    ensure("the native proof identifies the exact Vulkan-only SDL window", native_sdl_window_exact);
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
    ensure("the explicit diagnostic instance appends surface-capabilities2 before surface-maintenance1", instance_diagnostic_extensions);
    ensure("the Vulkan instance enables required validation", instance_validation_enabled);
    ensure("the current instance owns one SDL-created Vulkan surface", surface_acquired);
    ensure("the SDL-created Vulkan surface handle is non-null", surface_nonnull);
    ensure("the Vulkan surface retains the exact instance and native-window generation", surface_generation_exact);
    ensure("the Vulkan surface is owned by the SDL window's exact instance parent", surface_window_owned);
    ensure("the exact SDL surface selects one presentation-capable physical device", presentation_device_acquired);
    ensure("the selected physical-device handle is non-null", presentation_device_nonnull);
    ensure("the selected physical device supports standard Vulkan 1.1 or newer", presentation_device_api_1_1);
    ensure("the selected queue family is nonempty and graphics-capable", presentation_queue_usable);
    ensure("the selected device retains exact swapchain-maintenance and portability requirements", presentation_extensions_exact);
    ensure("the selected device supports VK_KHR_swapchain_maintenance1", presentation_maintenance);
    ensure("the SDL Vulkan branch automatically owns one logical device", logical_device_acquired);
    ensure("the automatically created logical device is non-null", logical_device_nonnull);
    ensure("the automatically retrieved presentation queue is non-null", logical_queue_nonnull);
    ensure("the logical device retains exact physical-device, family, and queue provenance", logical_provenance_exact);
    ensure("the logical device enables the required independent-blend capability", logical_feature_exact);
    ensure("the logical device enables the selected extensions in exact order", logical_extensions_exact);
    ensure("the logical device enables the swapchain-maintenance feature", logical_maintenance);
    ensure("the native smoke acquires one canonical device-local texture destination", texture_destination_acquired);
    ensure("the texture destination publishes exact image, memory, view, format, extent, mip, and dedicated-allocation metadata",
           texture_destination_metadata_exact);
    ensure("the native smoke acquires the exact 144-byte texture packet as one immutable upload source", texture_source_acquired);
    ensure("the texture source publishes exact handle, revision, shape, row, identity, allocation, and host-visible metadata",
           texture_source_metadata_exact);
    ensure("the native smoke acquires the exact 48-byte fixture as one immutable upload source", upload_source_acquired);
    ensure("the upload source publishes exact typed, identity, allocation, and host-visible metadata", upload_source_metadata_exact);
    ensure("the 144-byte texture source owns native buffer and memory identities distinct from the 48-byte vertex source",
           texture_source_native_distinct);
    ensure("the native smoke acquires one texture upload transfer over the exact source and image", texture_transfer_acquired);
    ensure("one bounded native submission and fence wait complete the texture upload", texture_transfer_complete_exact);
    ensure("texture upload completion publishes the exact revision, content identity, and shader-read state",
           texture_destination_resident_exact);
    ensure("the resident texture acquires one canonical sampled binding", texture_sample_binding_acquired);
    ensure("the sampled binding publishes exact lineage, view, layout, set, binding, and five native handles",
           texture_sample_binding_metadata_exact);
    ensure("the native smoke acquires one device-local destination for the exact upload source", upload_destination_acquired);
    ensure("the upload destination publishes exact identity, allocation, usage, locality, and unmapped metadata",
           upload_destination_metadata_exact);
    ensure("the new upload destination has no resident content before transfer", upload_destination_initially_nonresident);
    ensure("the native smoke acquires one device-scoped upload transfer", upload_transfer_acquired);
    ensure("the new transfer is Ready with exact source, destination, queue, command, fence, and zero-count metadata",
           upload_transfer_ready_exact);
    ensure("one native submission and completion wait move the transfer from Ready through Pending to Complete",
           upload_transfer_complete_exact);
    ensure("successful fence completion publishes the exact source identity as resident destination content",
           upload_destination_resident_exact);
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
    ensure("the native smoke explicitly acquires one presentation-target generation", presentation_target_acquired);
    ensure("the presentation target owns one render pass and one framebuffer per exact image view", presentation_target_complete);
    ensure("the presentation target retains the exact live image, swapchain, configuration, and device parents",
           presentation_target_provenance_exact);
    ensure("the native smoke explicitly acquires one presentation-pipeline generation", presentation_pipeline_acquired);
    ensure("the presentation pipeline owns one non-null layout and graphics pipeline", presentation_pipeline_handles_nonnull);
    ensure("the native smoke owns one coherent mapped readback destination with exact initial metadata",
           readback_acquired && readback_metadata_exact);
    ensure("the SDL Vulkan branch initially owns one automatic frame-slot generation", frame_slot_initially_acquired);
    ensure("the native smoke retires the automatic frame slot before target and pipeline acquisition", frame_slot_initially_reset);
    ensure("the native smoke acquires a fresh frame slot after target and pipeline acquisition", frame_slot_acquired);
    ensure("the fresh frame slot owns all six non-null command and synchronization handles", frame_slot_handles_nonnull);
    ensure("the frame slot retains the exact live queue-family, device, configuration, swapchain, and image parents",
           frame_slot_provenance_exact);
    ensure("the native X11 diagnostic requests a nonzero window resize", swapchain_resize_requested);
    ensure("SDL applies the asynchronous X11 resize before the drawable query", swapchain_resize_synchronized);
    ensure("SDL reports a different nonzero backing-pixel extent after the resize", swapchain_resize_observed);
    ensure("the explicit diagnostic core call rebuilds the swapchain chain", swapchain_rebuild_ready);
    ensure("swapchain rebuild preserves the exact instance, surface, physical device, logical device, and queue",
           swapchain_rebuild_parent_exact);
    ensure("swapchain rebuild publishes a fresh complete configuration, swapchain, image, target, pipeline, readback, and frame-slot chain",
           swapchain_rebuild_chain_complete);
    ensure("swapchain rebuild publishes exact changed-extent readback metadata", readback_rebuilt_exact);
    ensure("swapchain rebuild publishes non-null presentation-pipeline handles", presentation_pipeline_rebuilt_nonnull);
    ensure("the rebuilt configuration retains the resized X11 backing-pixel extent", swapchain_rebuild_extent_exact);
    ensure("the native SDL owner completes one legacy transfer clear before rebuild", frame_slot_transfer_clear_before_rebuild);
    ensure("the native SDL owner completes one render-pass clear before rebuild", frame_slot_render_pass_clear_before_rebuild);
    ensure("the complete initial target-pipeline-slot chain authenticates one detached green draw-readback observation before rebuild",
           frame_slot_render_pass_draw_readback_before_rebuild);
    ensure("the initial observed draw preserves the resident upload destination identity and native allocation",
           upload_destination_draw_retained);
    ensure("the initial draw-readback observation remains detached after changed-extent rebuild", frame_slot_initial_observation_detached);
    ensure("the rebuilt native SDL owner completes one legacy transfer clear", frame_slot_transfer_clear_after_rebuild);
    ensure("the rebuilt native SDL owner completes one render-pass clear", frame_slot_render_pass_clear_after_rebuild);
    ensure("the rebuilt target-pipeline-slot chain authenticates one green draw-readback observation at the changed extent",
           frame_slot_render_pass_draw_readback_after_rebuild);
    ensure("changed-extent rebuild preserves the resident device-local upload destination", upload_destination_rebuild_retained);
    ensure("changed-extent rebuild preserves the exact texture image, memory, view, and metadata", texture_destination_rebuild_retained);
    ensure("changed-extent rebuild preserves the exact immutable texture source", texture_source_rebuild_retained);
    ensure("changed-extent rebuild preserves the completed texture transfer and resident image", texture_transfer_rebuild_retained);
    ensure("changed-extent rebuild preserves the exact sampled binding", texture_sample_binding_rebuild_retained);
    ensure("the rebuilt observed draw preserves the resident destination identity and native allocation",
           upload_destination_rebuilt_draw_retained);
    ensure("a completion retry after Complete is rejected at the transfer state boundary", upload_transfer_complete_retry_rejected);
    ensure("the rejected retry does not resubmit the completed one-shot upload", upload_transfer_not_resubmitted);
    ensure("all post-rebuild presentation cycles retain all four synchronization handles", frame_slot_handles_untouched);
    ensure("six mixed presentation and observation cycles plus rebuild emit no validation messages", frame_slot_presentation_clean);
    ensure("the native smoke retires the upload source before the first observed draw", upload_source_explicitly_reset);
    ensure("pre-draw upload-source retirement removes every published handle and metadata value", upload_source_removed);
    ensure("pre-draw upload-source retirement also removes the completed transfer", upload_transfer_removed_with_source);
    ensure("pre-draw source and transfer retirement leaves the exact destination resident", upload_destination_source_retirement_retained);
    ensure("pre-draw source and transfer retirement preserves the complete swapchain chain", upload_source_chain_retained);
    ensure("upload acquisition, completion, retention, and source retirement emit no validation messages", upload_source_validation_clean);
    ensure("the native smoke explicitly resets the resident upload destination", upload_destination_explicitly_reset);
    ensure("explicit upload-destination reset removes every published handle and metadata value", upload_destination_removed);
    ensure("explicit upload-destination reset preserves the complete swapchain chain", upload_destination_chain_retained);
    ensure("upload-destination retirement emits no validation messages", upload_destination_validation_clean);
    ensure("the native smoke directly resets the completed texture transfer after changed-extent rebuild",
           texture_transfer_explicitly_reset);
    ensure("texture transfer acquisition, execution, residency publication, rebuild, and reset emit no validation messages",
           texture_transfer_validation_clean);
    ensure("direct texture-transfer reset preserves the sampled binding", texture_sample_binding_transfer_reset_retained);
    ensure("the native smoke directly resets the texture source after changed-extent rebuild", texture_source_explicitly_reset);
    ensure("direct texture-source reset removes every owned handle and zeroable metadata value", texture_source_removed);
    ensure("direct texture-source reset preserves the exact texture destination", texture_source_destination_retained);
    ensure("direct texture-source reset preserves the complete swapchain and presentation chain", texture_source_chain_retained);
    ensure("texture-source acquisition, rebuild retention, and retirement emit no validation messages", texture_source_validation_clean);
    ensure("direct texture-source reset preserves the sampled binding", texture_sample_binding_source_reset_retained);
    ensure("the native smoke directly resets the sampled binding before its destination", texture_sample_binding_explicitly_reset);
    ensure("direct sampled-binding reset clears every published identity and native handle", texture_sample_binding_removed);
    ensure("direct sampled-binding reset preserves the resident texture destination", texture_sample_binding_destination_retained);
    ensure("direct sampled-binding reset preserves the complete swapchain and presentation chain", texture_sample_binding_chain_retained);
    ensure("sampled-binding acquisition, rebuild retention, dependency retirement, and reset emit no validation messages",
           texture_sample_binding_validation_clean);
    ensure("the native smoke directly resets the texture destination after changed-extent rebuild", texture_destination_explicitly_reset);
    ensure("direct texture reset removes every published resource handle and metadata value", texture_destination_removed);
    ensure("direct texture reset preserves the complete swapchain and presentation chain", texture_destination_chain_retained);
    ensure("texture acquisition, rebuild retention, and retirement emit no validation messages", texture_destination_validation_clean);
    ensure("the native smoke explicitly resets the frame-slot child before its parents", frame_slot_explicitly_reset);
    ensure("the native smoke explicitly resets readback after the frame slot", readback_explicitly_reset);
    ensure("the native smoke explicitly resets the presentation pipeline after the frame slot", presentation_pipeline_explicitly_reset);
    ensure("the native smoke explicitly resets the presentation target after the presentation pipeline",
           presentation_target_explicitly_reset);
    ensure("the native smoke explicitly resets the Vulkan surface", surface_explicitly_reset);
    ensure("explicit frame-slot reset removes the generation and all six owned handles", frame_slot_removed);
    ensure("explicit readback reset removes its buffer, memory, and mapping", readback_removed);
    ensure("explicit readback reset preserves every independent presentation and swapchain sibling", readback_siblings_retained);
    ensure("explicit pipeline reset removes the layout and graphics pipeline", presentation_pipeline_removed);
    ensure("explicit target reset removes the render pass and every framebuffer", presentation_target_removed);
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
