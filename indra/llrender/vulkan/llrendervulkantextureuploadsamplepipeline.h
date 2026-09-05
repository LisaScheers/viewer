/**
 * @file llrendervulkantextureuploadsamplepipeline.h
 * @brief Loader-neutral ownership of one sampled streamed-texture pipeline.
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

#ifndef LL_LLRENDERVULKANTEXTUREUPLOADSAMPLEPIPELINE_H
#define LL_LLRENDERVULKANTEXTUREUPLOADSAMPLEPIPELINE_H

#include "llrendervulkanswapchainpresentationtarget.h"
#include "llrendervulkantextureuploadsamplebinding.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>
#include <variant>

namespace LLRenderVulkan
{

struct VulkanTextureUploadSamplePipelineDescription
{
    LLRenderContract::PipelineHandle mHandle;

    friend constexpr bool operator==(const VulkanTextureUploadSamplePipelineDescription&,
                                     const VulkanTextureUploadSamplePipelineDescription&) = default;
};

inline constexpr VulkanTextureUploadSamplePipelineDescription vulkanTextureUploadSamplePipelineDescription() noexcept
{
    return { LLRenderContract::StreamingUploadHandles{}.mPipeline };
}

enum class VulkanTextureUploadSamplePipelineCommand : std::uint8_t
{
    GetDeviceProcAddr,
    CreateShaderModule,
    DestroyShaderModule,
    CreateGraphicsPipelines,
    DestroyPipeline
};

enum class VulkanTextureUploadSamplePipelineResolutionCode : std::uint8_t
{
    InvalidPhysicalDeviceGeneration,
    InvalidLogicalDeviceGeneration,
    InvalidDescription,
    InvalidTextureUploadDestinationGeneration,
    InvalidTextureUploadSampleBindingGeneration,
    InvalidSwapchainConfigurationGeneration,
    InvalidSwapchainGeneration,
    InvalidSwapchainImagesGeneration,
    InvalidSwapchainPresentationTargetGeneration,
    MissingRequiredCommand,
    VertexShaderModuleCreationFailure,
    NullVertexShaderModuleOnSuccess,
    FragmentShaderModuleCreationFailure,
    NullFragmentShaderModuleOnSuccess,
    GraphicsPipelineCreationFailure,
    NullGraphicsPipelineOnSuccess,
    ParentGenerationChanged
};

struct VulkanTextureUploadSamplePipelineResolutionError
{
    VulkanTextureUploadSamplePipelineResolutionCode mCode =
        VulkanTextureUploadSamplePipelineResolutionCode::InvalidPhysicalDeviceGeneration;
    std::optional<VulkanTextureUploadSamplePipelineCommand> mCommand;
    VkResult                                                mResult = VK_SUCCESS;

    friend constexpr bool operator==(const VulkanTextureUploadSamplePipelineResolutionError&,
                                     const VulkanTextureUploadSamplePipelineResolutionError&) = default;
};

// This immutable generation owns one graphics pipeline and borrows the exact
// Stage 60 sampled-binding pipeline layout. Its exact physical, logical,
// texture, binding, and swapchain parents must outlive it. Submitted users must
// finish before reset, and host access is externally synchronized.
class VulkanTextureUploadSamplePipelineGeneration
{
public:
    ~VulkanTextureUploadSamplePipelineGeneration() noexcept;

    VulkanTextureUploadSamplePipelineGeneration(const VulkanTextureUploadSamplePipelineGeneration&)            = delete;
    VulkanTextureUploadSamplePipelineGeneration& operator=(const VulkanTextureUploadSamplePipelineGeneration&) = delete;
    VulkanTextureUploadSamplePipelineGeneration(VulkanTextureUploadSamplePipelineGeneration&& other) noexcept;
    VulkanTextureUploadSamplePipelineGeneration& operator=(VulkanTextureUploadSamplePipelineGeneration&&) = delete;

    LLRenderContract::PipelineHandle pipelineResourceHandle() const noexcept { return mDescription.mHandle; }
    VkPipelineLayout                 pipelineLayout() const noexcept { return mPipelineLayout; }
    VkPipeline                       pipeline() const noexcept { return mPipeline; }

    bool createdFor(const VulkanPhysicalDeviceGeneration&              physical_device_generation,
                    const VulkanLogicalDeviceGeneration&               logical_device_generation,
                    const VulkanTextureUploadDestinationGeneration&    destination_generation,
                    const VulkanTextureUploadSampleBindingGeneration&  sample_binding_generation,
                    const VulkanSwapchainConfigurationGeneration&      configuration_generation,
                    const VulkanSwapchainGeneration&                   swapchain_generation,
                    const VulkanSwapchainImagesGeneration&             images_generation,
                    const VulkanSwapchainPresentationTargetGeneration& presentation_target_generation) const noexcept;
    bool matchesDescription(const VulkanTextureUploadSamplePipelineDescription& description) const noexcept;
    bool retainsTextureUploadSampleBindingGeneration(
        const VulkanTextureUploadSampleBindingGeneration& sample_binding_generation) const noexcept;

    void reset() noexcept;

private:
    friend struct VulkanTextureUploadSamplePipelineGenerationFactory;

    VulkanTextureUploadSamplePipelineGeneration(const VulkanPhysicalDeviceGeneration&               physical_device_generation,
                                                const VulkanLogicalDeviceGeneration&                logical_device_generation,
                                                const VulkanTextureUploadDestinationDescription&    destination_description,
                                                const VulkanTextureUploadSampleBindingDescription&  sample_binding_description,
                                                const VulkanTextureUploadSamplePipelineDescription& description,
                                                const VulkanTextureUploadDestinationGeneration&     destination_generation,
                                                const VulkanTextureUploadSampleBindingGeneration&   sample_binding_generation,
                                                const VulkanSwapchainConfigurationGeneration&       configuration_generation,
                                                const VulkanSwapchainGeneration&                    swapchain_generation,
                                                const VulkanSwapchainImagesGeneration&              images_generation,
                                                const VulkanSwapchainPresentationTargetGeneration&  presentation_target_generation,
                                                VkPipelineLayout                                    pipeline_layout,
                                                VkPipeline                                          pipeline,
                                                PFN_vkDestroyPipeline                               destroy_pipeline) noexcept;

    const VulkanPhysicalDeviceGeneration*              mPhysicalDeviceGeneration     = nullptr;
    const VulkanLogicalDeviceGeneration*               mLogicalDeviceGeneration      = nullptr;
    const VulkanTextureUploadDestinationGeneration*    mDestinationGeneration        = nullptr;
    const VulkanTextureUploadSampleBindingGeneration*  mSampleBindingGeneration      = nullptr;
    const VulkanSwapchainConfigurationGeneration*      mConfigurationGeneration      = nullptr;
    const VulkanSwapchainGeneration*                   mSwapchainGeneration          = nullptr;
    const VulkanSwapchainImagesGeneration*             mImagesGeneration             = nullptr;
    const VulkanSwapchainPresentationTargetGeneration* mPresentationTargetGeneration = nullptr;
    PFN_vkGetInstanceProcAddr                          mGetInstanceProcAddr          = nullptr;
    VkInstance                                         mInstance                     = VK_NULL_HANDLE;
    VkSurfaceKHR                                       mSurface                      = VK_NULL_HANDLE;
    VkPhysicalDevice                                   mPhysicalDevice               = VK_NULL_HANDLE;
    std::uint32_t                                      mPhysicalDeviceIndex          = 0;
    VkDevice                                           mDevice                       = VK_NULL_HANDLE;
    VkQueue                                            mQueue                        = VK_NULL_HANDLE;
    std::uint32_t                                      mQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    std::uint32_t                                      mQueueIndex                   = 0;
    VulkanTextureUploadDestinationDescription          mDestinationDescription;
    VulkanTextureUploadSampleBindingDescription        mSampleBindingDescription;
    VulkanTextureUploadSamplePipelineDescription       mDescription;
    std::uint64_t                                      mResidentRevision        = 0;
    std::uint64_t                                      mResidentContentIdentity = 0;
    VkImageView                                        mDestinationImageView    = VK_NULL_HANDLE;
    VkDescriptorSet                                    mDescriptorSet           = VK_NULL_HANDLE;
    VkExtent2D                                         mDrawableExtent{};
    VkSwapchainKHR                                     mSwapchain   = VK_NULL_HANDLE;
    VkFormat                                           mImageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D                                         mImageExtent{};
    std::uint32_t                                      mImageCount      = 0;
    VkRenderPass                                       mRenderPass      = VK_NULL_HANDLE;
    VkPipelineLayout                                   mPipelineLayout  = VK_NULL_HANDLE;
    VkPipeline                                         mPipeline        = VK_NULL_HANDLE;
    PFN_vkDestroyPipeline                              mDestroyPipeline = nullptr;
};

using VulkanTextureUploadSamplePipelineResolutionResult =
    std::variant<VulkanTextureUploadSamplePipelineResolutionError, VulkanTextureUploadSamplePipelineGeneration>;

VulkanTextureUploadSamplePipelineResolutionResult resolveVulkanTextureUploadSamplePipelineGeneration(
    const VulkanPhysicalDeviceGeneration&               physical_device_generation,
    const VulkanLogicalDeviceGeneration&                logical_device_generation,
    const VulkanTextureUploadDestinationDescription&    destination_description,
    const VulkanTextureUploadSampleBindingDescription&  sample_binding_description,
    const VulkanTextureUploadSamplePipelineDescription& description,
    const VulkanTextureUploadDestinationGeneration&     destination_generation,
    const VulkanTextureUploadSampleBindingGeneration&   sample_binding_generation,
    const VulkanSwapchainConfigurationGeneration&       configuration_generation,
    const VulkanSwapchainGeneration&                    swapchain_generation,
    const VulkanSwapchainImagesGeneration&              images_generation,
    const VulkanSwapchainPresentationTargetGeneration&  presentation_target_generation) noexcept;

} // namespace LLRenderVulkan

#endif // LL_LLRENDERVULKANTEXTUREUPLOADSAMPLEPIPELINE_H
