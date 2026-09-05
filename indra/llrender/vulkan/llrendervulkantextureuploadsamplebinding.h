/**
 * @file llrendervulkantextureuploadsamplebinding.h
 * @brief Loader-neutral ownership of one sampled streamed-texture binding.
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

#ifndef LL_LLRENDERVULKANTEXTUREUPLOADSAMPLEBINDING_H
#define LL_LLRENDERVULKANTEXTUREUPLOADSAMPLEBINDING_H

#include "llrendervulkantextureuploaddestination.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>
#include <variant>

namespace LLRenderVulkan
{

struct VulkanTextureUploadSampleBindingDescription
{
    LLRenderContract::SamplerResource mSampler;
    std::uint32_t                     mDescriptorSetIndex = 0;
    std::uint32_t                     mBinding            = 0;

    friend constexpr bool operator==(const VulkanTextureUploadSampleBindingDescription& left,
                                     const VulkanTextureUploadSampleBindingDescription& right) noexcept
    {
        return left.mSampler.mHandle == right.mSampler.mHandle && left.mSampler.mMinFilter == right.mSampler.mMinFilter &&
               left.mSampler.mMagFilter == right.mSampler.mMagFilter && left.mSampler.mMipFilter == right.mSampler.mMipFilter &&
               left.mSampler.mAddressU == right.mSampler.mAddressU && left.mSampler.mAddressV == right.mSampler.mAddressV &&
               left.mSampler.mMaxAnisotropy == right.mSampler.mMaxAnisotropy && left.mSampler.mLifetime == right.mSampler.mLifetime &&
               left.mDescriptorSetIndex == right.mDescriptorSetIndex && left.mBinding == right.mBinding;
    }
};

inline constexpr VulkanTextureUploadSampleBindingDescription vulkanTextureUploadSampleBindingDescription() noexcept
{
    LLRenderContract::SamplerResource sampler;
    sampler.mHandle        = LLRenderContract::StreamingUploadHandles{}.mSampler;
    sampler.mMinFilter     = LLRenderContract::Filter::Linear;
    sampler.mMagFilter     = LLRenderContract::Filter::Linear;
    sampler.mMipFilter     = LLRenderContract::MipFilter::Linear;
    sampler.mAddressU      = LLRenderContract::AddressMode::Clamp;
    sampler.mAddressV      = LLRenderContract::AddressMode::Clamp;
    sampler.mMaxAnisotropy = 1.f;
    sampler.mLifetime      = LLRenderContract::ResourceLifetime::Persistent;
    return { sampler, 0, 0 };
}

enum class VulkanTextureUploadSampleBindingCommand : std::uint8_t
{
    GetDeviceProcAddr,
    CreateSampler,
    DestroySampler,
    CreateDescriptorSetLayout,
    DestroyDescriptorSetLayout,
    CreatePipelineLayout,
    DestroyPipelineLayout,
    CreateDescriptorPool,
    DestroyDescriptorPool,
    AllocateDescriptorSets,
    UpdateDescriptorSets
};

enum class VulkanTextureUploadSampleBindingResolutionCode : std::uint8_t
{
    InvalidPhysicalDeviceGeneration,
    InvalidLogicalDeviceGeneration,
    InvalidDescription,
    InvalidTextureUploadDestinationGeneration,
    DestinationNotResident,
    MissingRequiredCommand,
    SamplerCreationFailure,
    NullSamplerOnSuccess,
    DescriptorSetLayoutCreationFailure,
    NullDescriptorSetLayoutOnSuccess,
    PipelineLayoutCreationFailure,
    NullPipelineLayoutOnSuccess,
    DescriptorPoolCreationFailure,
    NullDescriptorPoolOnSuccess,
    DescriptorSetAllocationFailure,
    NullDescriptorSetOnSuccess,
    ParentGenerationChanged
};

struct VulkanTextureUploadSampleBindingResolutionError
{
    VulkanTextureUploadSampleBindingResolutionCode mCode = VulkanTextureUploadSampleBindingResolutionCode::InvalidPhysicalDeviceGeneration;
    std::optional<VulkanTextureUploadSampleBindingCommand> mCommand;
    VkResult                                               mResult = VK_SUCCESS;

    friend constexpr bool operator==(const VulkanTextureUploadSampleBindingResolutionError&,
                                     const VulkanTextureUploadSampleBindingResolutionError&) = default;
};

// This immutable generation owns the complete set-zero interface for sampling
// one completed streamed-texture destination: a sampler, descriptor-set layout,
// pipeline layout, descriptor pool, pool-owned set, and its completed write. It
// retains only the destination generation. The exact physical, logical, and
// destination generations must outlive it. Submitted users must finish before
// reset, and host access is externally synchronized.
class VulkanTextureUploadSampleBindingGeneration
{
public:
    ~VulkanTextureUploadSampleBindingGeneration() noexcept;

    VulkanTextureUploadSampleBindingGeneration(const VulkanTextureUploadSampleBindingGeneration&)            = delete;
    VulkanTextureUploadSampleBindingGeneration& operator=(const VulkanTextureUploadSampleBindingGeneration&) = delete;
    VulkanTextureUploadSampleBindingGeneration(VulkanTextureUploadSampleBindingGeneration&& other) noexcept;
    VulkanTextureUploadSampleBindingGeneration& operator=(VulkanTextureUploadSampleBindingGeneration&&) = delete;

    LLRenderContract::SamplerHandle samplerResourceHandle() const noexcept { return mDescription.mSampler.mHandle; }
    LLRenderContract::ImageHandle   destinationResourceHandle() const noexcept { return mDestinationDescription.mHandle; }
    std::uint64_t                   expectedRevision() const noexcept { return mDestinationDescription.mExpectedRevision; }
    std::uint64_t                   residentRevision() const noexcept { return mResidentRevision; }
    std::uint64_t                   residentContentIdentity() const noexcept { return mResidentContentIdentity; }
    VkImageView                     destinationImageView() const noexcept { return mDestinationImageView; }
    VkImageLayout                   destinationImageLayout() const noexcept
    {
        return mDestinationImageView != VK_NULL_HANDLE ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_MAX_ENUM;
    }
    std::uint32_t         descriptorSetIndex() const noexcept { return mDescription.mDescriptorSetIndex; }
    std::uint32_t         binding() const noexcept { return mDescription.mBinding; }
    VkSampler             sampler() const noexcept { return mSampler; }
    VkDescriptorSetLayout descriptorSetLayout() const noexcept { return mDescriptorSetLayout; }
    VkPipelineLayout      pipelineLayout() const noexcept { return mPipelineLayout; }
    VkDescriptorPool      descriptorPool() const noexcept { return mDescriptorPool; }
    VkDescriptorSet       descriptorSet() const noexcept { return mDescriptorSet; }

    bool createdFor(const VulkanPhysicalDeviceGeneration&           physical_device_generation,
                    const VulkanLogicalDeviceGeneration&            logical_device_generation,
                    const VulkanTextureUploadDestinationGeneration& destination_generation) const noexcept;
    bool matchesDescription(const VulkanTextureUploadSampleBindingDescription& description) const noexcept;
    bool retainsTextureUploadDestinationGeneration(const VulkanTextureUploadDestinationGeneration& destination_generation) const noexcept;

    void reset() noexcept;

private:
    friend struct VulkanTextureUploadSampleBindingGenerationFactory;

    VulkanTextureUploadSampleBindingGeneration(const VulkanPhysicalDeviceGeneration&              physical_device_generation,
                                               const VulkanLogicalDeviceGeneration&               logical_device_generation,
                                               const VulkanTextureUploadSampleBindingDescription& description,
                                               const VulkanTextureUploadDestinationDescription&   destination_description,
                                               const VulkanTextureUploadDestinationGeneration&    destination_generation,
                                               std::uint64_t                                      resident_revision,
                                               std::uint64_t                                      resident_content_identity,
                                               VkImageView                                        destination_image_view,
                                               VkSampler                                          sampler,
                                               VkDescriptorSetLayout                              descriptor_set_layout,
                                               VkPipelineLayout                                   pipeline_layout,
                                               VkDescriptorPool                                   descriptor_pool,
                                               VkDescriptorSet                                    descriptor_set,
                                               PFN_vkDestroySampler                               destroy_sampler,
                                               PFN_vkDestroyDescriptorSetLayout                   destroy_descriptor_set_layout,
                                               PFN_vkDestroyPipelineLayout                        destroy_pipeline_layout,
                                               PFN_vkDestroyDescriptorPool                        destroy_descriptor_pool) noexcept;

    const VulkanPhysicalDeviceGeneration*           mPhysicalDeviceGeneration = nullptr;
    const VulkanLogicalDeviceGeneration*            mLogicalDeviceGeneration  = nullptr;
    const VulkanTextureUploadDestinationGeneration* mDestinationGeneration    = nullptr;
    PFN_vkGetInstanceProcAddr                       mGetInstanceProcAddr      = nullptr;
    VkInstance                                      mInstance                 = VK_NULL_HANDLE;
    VkSurfaceKHR                                    mSurface                  = VK_NULL_HANDLE;
    VkPhysicalDevice                                mPhysicalDevice           = VK_NULL_HANDLE;
    std::uint32_t                                   mPhysicalDeviceIndex      = 0;
    VkDevice                                        mDevice                   = VK_NULL_HANDLE;
    VkQueue                                         mQueue                    = VK_NULL_HANDLE;
    std::uint32_t                                   mQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
    std::uint32_t                                   mQueueIndex               = 0;
    VulkanTextureUploadSampleBindingDescription     mDescription;
    VulkanTextureUploadDestinationDescription       mDestinationDescription;
    std::uint64_t                                   mResidentRevision           = 0;
    std::uint64_t                                   mResidentContentIdentity    = 0;
    VkImageView                                     mDestinationImageView       = VK_NULL_HANDLE;
    VkSampler                                       mSampler                    = VK_NULL_HANDLE;
    VkDescriptorSetLayout                           mDescriptorSetLayout        = VK_NULL_HANDLE;
    VkPipelineLayout                                mPipelineLayout             = VK_NULL_HANDLE;
    VkDescriptorPool                                mDescriptorPool             = VK_NULL_HANDLE;
    VkDescriptorSet                                 mDescriptorSet              = VK_NULL_HANDLE;
    PFN_vkDestroySampler                            mDestroySampler             = nullptr;
    PFN_vkDestroyDescriptorSetLayout                mDestroyDescriptorSetLayout = nullptr;
    PFN_vkDestroyPipelineLayout                     mDestroyPipelineLayout      = nullptr;
    PFN_vkDestroyDescriptorPool                     mDestroyDescriptorPool      = nullptr;
};

using VulkanTextureUploadSampleBindingResolutionResult =
    std::variant<VulkanTextureUploadSampleBindingResolutionError, VulkanTextureUploadSampleBindingGeneration>;

VulkanTextureUploadSampleBindingResolutionResult resolveVulkanTextureUploadSampleBindingGeneration(
    const VulkanPhysicalDeviceGeneration&              physical_device_generation,
    const VulkanLogicalDeviceGeneration&               logical_device_generation,
    const VulkanTextureUploadDestinationDescription&   destination_description,
    const VulkanTextureUploadSampleBindingDescription& description,
    const VulkanTextureUploadDestinationGeneration&    destination_generation) noexcept;

} // namespace LLRenderVulkan

#endif // LL_LLRENDERVULKANTEXTUREUPLOADSAMPLEBINDING_H
