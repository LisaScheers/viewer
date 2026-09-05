/**
 * @file llrendervulkantextureuploadsamplebinding.cpp
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

#include "llrendervulkantextureuploadsamplebinding.h"

#include <utility>

namespace LLRenderVulkan
{
namespace
{
    struct Dispatch
    {
        PFN_vkGetDeviceProcAddr          mGetDeviceProcAddr          = nullptr;
        PFN_vkCreateSampler              mCreateSampler              = nullptr;
        PFN_vkDestroySampler             mDestroySampler             = nullptr;
        PFN_vkCreateDescriptorSetLayout  mCreateDescriptorSetLayout  = nullptr;
        PFN_vkDestroyDescriptorSetLayout mDestroyDescriptorSetLayout = nullptr;
        PFN_vkCreatePipelineLayout       mCreatePipelineLayout       = nullptr;
        PFN_vkDestroyPipelineLayout      mDestroyPipelineLayout      = nullptr;
        PFN_vkCreateDescriptorPool       mCreateDescriptorPool       = nullptr;
        PFN_vkDestroyDescriptorPool      mDestroyDescriptorPool      = nullptr;
        PFN_vkAllocateDescriptorSets     mAllocateDescriptorSets     = nullptr;
        PFN_vkUpdateDescriptorSets       mUpdateDescriptorSets       = nullptr;
    };

    template<typename Function>
    Function resolveInstance(PFN_vkGetInstanceProcAddr resolver, VkInstance instance, const char* name) noexcept
    {
        return reinterpret_cast<Function>(resolver(instance, name));
    }

    template<typename Function>
    Function resolveDevice(PFN_vkGetDeviceProcAddr resolver, VkDevice device, const char* name) noexcept
    {
        return reinterpret_cast<Function>(resolver(device, name));
    }

    VulkanTextureUploadSampleBindingResolutionError failure(VulkanTextureUploadSampleBindingResolutionCode         code,
                                                            std::optional<VulkanTextureUploadSampleBindingCommand> command = std::nullopt,
                                                            VkResult result = VK_SUCCESS) noexcept
    {
        return { code, command, result };
    }

    bool validPhysical(const VulkanPhysicalDeviceGeneration& physical) noexcept
    {
        const VkQueueFamilyProperties& queue_family = physical.queueFamilyProperties();
        return physical.getInstanceProcAddr() != nullptr && physical.instance() != VK_NULL_HANDLE && physical.surface() != VK_NULL_HANDLE &&
               physical.physicalDevice() != VK_NULL_HANDLE && physical.queueFamilyIndex() != VK_QUEUE_FAMILY_IGNORED &&
               queue_family.queueCount != 0 && (queue_family.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
    }

    bool validLogical(const VulkanPhysicalDeviceGeneration& physical, const VulkanLogicalDeviceGeneration& logical) noexcept
    {
        const VkQueueFamilyProperties& queue_family = physical.queueFamilyProperties();
        return logical.createdFor(physical) && logical.device() != VK_NULL_HANDLE && logical.queue() != VK_NULL_HANDLE &&
               logical.queueFamilyIndex() == physical.queueFamilyIndex() && queue_family.queueCount > logical.queueIndex() &&
               (queue_family.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
    }

    bool canonicalDescription(const VulkanTextureUploadSampleBindingDescription& description) noexcept
    {
        return description == vulkanTextureUploadSampleBindingDescription();
    }

    bool validDestinationBase(const VulkanPhysicalDeviceGeneration&            physical,
                              const VulkanLogicalDeviceGeneration&             logical,
                              const VulkanTextureUploadDestinationDescription& description,
                              const VulkanTextureUploadDestinationGeneration&  destination) noexcept
    {
        const auto range = destination.viewRange();
        return description == vulkanTextureUploadDestinationDescription() && destination.createdFor(physical, logical) &&
               destination.matchesDescription(description) && destination.resourceHandle() == description.mHandle &&
               destination.expectedRevision() == description.mExpectedRevision && destination.image() != VK_NULL_HANDLE &&
               destination.imageView() != VK_NULL_HANDLE && destination.format() == VK_FORMAT_R8G8B8A8_UNORM &&
               destination.mipLevels() == LLRenderContract::TEXTURE_UPLOAD_MIP_LEVELS && range.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT &&
               range.baseMipLevel == 0 && range.levelCount == LLRenderContract::TEXTURE_UPLOAD_MIP_LEVELS && range.baseArrayLayer == 0 &&
               range.layerCount == 1;
    }

    bool validResidentDestination(const VulkanPhysicalDeviceGeneration&            physical,
                                  const VulkanLogicalDeviceGeneration&             logical,
                                  const VulkanTextureUploadDestinationDescription& description,
                                  const VulkanTextureUploadDestinationGeneration&  destination) noexcept
    {
        return validDestinationBase(physical, logical, description, destination) && destination.isResident() &&
               destination.residentRevision() == description.mExpectedRevision && destination.residentContentIdentity() != 0 &&
               destination.currentState() == LLRenderContract::ImageState::ShaderRead;
    }

    bool parentsAreCurrent(const VulkanPhysicalDeviceGeneration&              physical,
                           const VulkanLogicalDeviceGeneration&               logical,
                           const VulkanTextureUploadSampleBindingDescription& description,
                           const VulkanTextureUploadSampleBindingDescription& owned_description,
                           const VulkanTextureUploadDestinationDescription&   destination_description,
                           const VulkanTextureUploadDestinationDescription&   owned_destination_description,
                           const VulkanTextureUploadDestinationGeneration&    destination,
                           VkImageView                                        destination_image_view,
                           std::uint64_t                                      resident_revision,
                           std::uint64_t                                      resident_content_identity) noexcept
    {
        return description == owned_description && canonicalDescription(owned_description) &&
               destination_description == owned_destination_description &&
               owned_destination_description == vulkanTextureUploadDestinationDescription() && validPhysical(physical) &&
               validLogical(physical, logical) && validResidentDestination(physical, logical, owned_destination_description, destination) &&
               destination.imageView() == destination_image_view && destination.residentRevision() == resident_revision &&
               destination.residentContentIdentity() == resident_content_identity;
    }

    std::optional<VulkanTextureUploadSampleBindingResolutionError> resolveDispatch(
        const VulkanPhysicalDeviceGeneration&              physical,
        const VulkanLogicalDeviceGeneration&               logical,
        const VulkanTextureUploadSampleBindingDescription& description,
        const VulkanTextureUploadSampleBindingDescription& owned_description,
        const VulkanTextureUploadDestinationDescription&   destination_description,
        const VulkanTextureUploadDestinationDescription&   owned_destination_description,
        const VulkanTextureUploadDestinationGeneration&    destination,
        VkImageView                                        destination_image_view,
        std::uint64_t                                      resident_revision,
        std::uint64_t                                      resident_content_identity,
        Dispatch&                                          dispatch) noexcept
    {
        dispatch.mGetDeviceProcAddr =
            resolveInstance<PFN_vkGetDeviceProcAddr>(physical.getInstanceProcAddr(), physical.instance(), "vkGetDeviceProcAddr");
        if (!parentsAreCurrent(physical,
                               logical,
                               description,
                               owned_description,
                               destination_description,
                               owned_destination_description,
                               destination,
                               destination_image_view,
                               resident_revision,
                               resident_content_identity))
        {
            return failure(VulkanTextureUploadSampleBindingResolutionCode::ParentGenerationChanged);
        }
        if (!dispatch.mGetDeviceProcAddr)
        {
            return failure(VulkanTextureUploadSampleBindingResolutionCode::MissingRequiredCommand,
                           VulkanTextureUploadSampleBindingCommand::GetDeviceProcAddr);
        }

#define LL_RESOLVE_TEXTURE_SAMPLE_BINDING_COMMAND(member, type, name, command)                           \
    dispatch.member = resolveDevice<type>(dispatch.mGetDeviceProcAddr, logical.device(), name);          \
    if (!parentsAreCurrent(physical,                                                                     \
                           logical,                                                                      \
                           description,                                                                  \
                           owned_description,                                                            \
                           destination_description,                                                      \
                           owned_destination_description,                                                \
                           destination,                                                                  \
                           destination_image_view,                                                       \
                           resident_revision,                                                            \
                           resident_content_identity))                                                   \
    {                                                                                                    \
        return failure(VulkanTextureUploadSampleBindingResolutionCode::ParentGenerationChanged);         \
    }                                                                                                    \
    if (!dispatch.member)                                                                                \
    {                                                                                                    \
        return failure(VulkanTextureUploadSampleBindingResolutionCode::MissingRequiredCommand, command); \
    }

        LL_RESOLVE_TEXTURE_SAMPLE_BINDING_COMMAND(mCreateSampler,
                                                  PFN_vkCreateSampler,
                                                  "vkCreateSampler",
                                                  VulkanTextureUploadSampleBindingCommand::CreateSampler)
        LL_RESOLVE_TEXTURE_SAMPLE_BINDING_COMMAND(mDestroySampler,
                                                  PFN_vkDestroySampler,
                                                  "vkDestroySampler",
                                                  VulkanTextureUploadSampleBindingCommand::DestroySampler)
        LL_RESOLVE_TEXTURE_SAMPLE_BINDING_COMMAND(mCreateDescriptorSetLayout,
                                                  PFN_vkCreateDescriptorSetLayout,
                                                  "vkCreateDescriptorSetLayout",
                                                  VulkanTextureUploadSampleBindingCommand::CreateDescriptorSetLayout)
        LL_RESOLVE_TEXTURE_SAMPLE_BINDING_COMMAND(mDestroyDescriptorSetLayout,
                                                  PFN_vkDestroyDescriptorSetLayout,
                                                  "vkDestroyDescriptorSetLayout",
                                                  VulkanTextureUploadSampleBindingCommand::DestroyDescriptorSetLayout)
        LL_RESOLVE_TEXTURE_SAMPLE_BINDING_COMMAND(mCreatePipelineLayout,
                                                  PFN_vkCreatePipelineLayout,
                                                  "vkCreatePipelineLayout",
                                                  VulkanTextureUploadSampleBindingCommand::CreatePipelineLayout)
        LL_RESOLVE_TEXTURE_SAMPLE_BINDING_COMMAND(mDestroyPipelineLayout,
                                                  PFN_vkDestroyPipelineLayout,
                                                  "vkDestroyPipelineLayout",
                                                  VulkanTextureUploadSampleBindingCommand::DestroyPipelineLayout)
        LL_RESOLVE_TEXTURE_SAMPLE_BINDING_COMMAND(mCreateDescriptorPool,
                                                  PFN_vkCreateDescriptorPool,
                                                  "vkCreateDescriptorPool",
                                                  VulkanTextureUploadSampleBindingCommand::CreateDescriptorPool)
        LL_RESOLVE_TEXTURE_SAMPLE_BINDING_COMMAND(mDestroyDescriptorPool,
                                                  PFN_vkDestroyDescriptorPool,
                                                  "vkDestroyDescriptorPool",
                                                  VulkanTextureUploadSampleBindingCommand::DestroyDescriptorPool)
        LL_RESOLVE_TEXTURE_SAMPLE_BINDING_COMMAND(mAllocateDescriptorSets,
                                                  PFN_vkAllocateDescriptorSets,
                                                  "vkAllocateDescriptorSets",
                                                  VulkanTextureUploadSampleBindingCommand::AllocateDescriptorSets)
        LL_RESOLVE_TEXTURE_SAMPLE_BINDING_COMMAND(mUpdateDescriptorSets,
                                                  PFN_vkUpdateDescriptorSets,
                                                  "vkUpdateDescriptorSets",
                                                  VulkanTextureUploadSampleBindingCommand::UpdateDescriptorSets)

#undef LL_RESOLVE_TEXTURE_SAMPLE_BINDING_COMMAND

        return std::nullopt;
    }

    void rollback(const Dispatch&       dispatch,
                  VkDevice              device,
                  VkDescriptorPool      descriptor_pool,
                  VkPipelineLayout      pipeline_layout,
                  VkDescriptorSetLayout descriptor_set_layout,
                  VkSampler             sampler) noexcept
    {
        if (descriptor_pool != VK_NULL_HANDLE)
        {
            dispatch.mDestroyDescriptorPool(device, descriptor_pool, nullptr);
        }
        if (pipeline_layout != VK_NULL_HANDLE)
        {
            dispatch.mDestroyPipelineLayout(device, pipeline_layout, nullptr);
        }
        if (descriptor_set_layout != VK_NULL_HANDLE)
        {
            dispatch.mDestroyDescriptorSetLayout(device, descriptor_set_layout, nullptr);
        }
        if (sampler != VK_NULL_HANDLE)
        {
            dispatch.mDestroySampler(device, sampler, nullptr);
        }
    }
} // namespace

struct VulkanTextureUploadSampleBindingGenerationFactory
{
    static VulkanTextureUploadSampleBindingGeneration create(const VulkanPhysicalDeviceGeneration&              physical,
                                                             const VulkanLogicalDeviceGeneration&               logical,
                                                             const VulkanTextureUploadSampleBindingDescription& description,
                                                             const VulkanTextureUploadDestinationDescription&   destination_description,
                                                             const VulkanTextureUploadDestinationGeneration&    destination,
                                                             std::uint64_t                                      resident_revision,
                                                             std::uint64_t                                      resident_content_identity,
                                                             VkImageView                                        destination_image_view,
                                                             VkSampler                                          sampler,
                                                             VkDescriptorSetLayout                              descriptor_set_layout,
                                                             VkPipelineLayout                                   pipeline_layout,
                                                             VkDescriptorPool                                   descriptor_pool,
                                                             VkDescriptorSet                                    descriptor_set,
                                                             const Dispatch&                                    dispatch) noexcept
    {
        return VulkanTextureUploadSampleBindingGeneration(physical,
                                                          logical,
                                                          description,
                                                          destination_description,
                                                          destination,
                                                          resident_revision,
                                                          resident_content_identity,
                                                          destination_image_view,
                                                          sampler,
                                                          descriptor_set_layout,
                                                          pipeline_layout,
                                                          descriptor_pool,
                                                          descriptor_set,
                                                          dispatch.mDestroySampler,
                                                          dispatch.mDestroyDescriptorSetLayout,
                                                          dispatch.mDestroyPipelineLayout,
                                                          dispatch.mDestroyDescriptorPool);
    }
};

VulkanTextureUploadSampleBindingGeneration::VulkanTextureUploadSampleBindingGeneration(
    const VulkanPhysicalDeviceGeneration&              physical,
    const VulkanLogicalDeviceGeneration&               logical,
    const VulkanTextureUploadSampleBindingDescription& description,
    const VulkanTextureUploadDestinationDescription&   destination_description,
    const VulkanTextureUploadDestinationGeneration&    destination,
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
    PFN_vkDestroyDescriptorPool                        destroy_descriptor_pool) noexcept :
    mPhysicalDeviceGeneration(&physical),
    mLogicalDeviceGeneration(&logical),
    mDestinationGeneration(&destination),
    mGetInstanceProcAddr(physical.getInstanceProcAddr()),
    mInstance(physical.instance()),
    mSurface(physical.surface()),
    mPhysicalDevice(physical.physicalDevice()),
    mPhysicalDeviceIndex(physical.physicalDeviceIndex()),
    mDevice(logical.device()),
    mQueue(logical.queue()),
    mQueueFamilyIndex(logical.queueFamilyIndex()),
    mQueueIndex(logical.queueIndex()),
    mDescription(description),
    mDestinationDescription(destination_description),
    mResidentRevision(resident_revision),
    mResidentContentIdentity(resident_content_identity),
    mDestinationImageView(destination_image_view),
    mSampler(sampler),
    mDescriptorSetLayout(descriptor_set_layout),
    mPipelineLayout(pipeline_layout),
    mDescriptorPool(descriptor_pool),
    mDescriptorSet(descriptor_set),
    mDestroySampler(destroy_sampler),
    mDestroyDescriptorSetLayout(destroy_descriptor_set_layout),
    mDestroyPipelineLayout(destroy_pipeline_layout),
    mDestroyDescriptorPool(destroy_descriptor_pool)
{
}

VulkanTextureUploadSampleBindingGeneration::~VulkanTextureUploadSampleBindingGeneration() noexcept
{
    reset();
}

VulkanTextureUploadSampleBindingGeneration::VulkanTextureUploadSampleBindingGeneration(
    VulkanTextureUploadSampleBindingGeneration&& other) noexcept :
    mPhysicalDeviceGeneration(std::exchange(other.mPhysicalDeviceGeneration, nullptr)),
    mLogicalDeviceGeneration(std::exchange(other.mLogicalDeviceGeneration, nullptr)),
    mDestinationGeneration(std::exchange(other.mDestinationGeneration, nullptr)),
    mGetInstanceProcAddr(std::exchange(other.mGetInstanceProcAddr, nullptr)),
    mInstance(std::exchange(other.mInstance, VK_NULL_HANDLE)),
    mSurface(std::exchange(other.mSurface, VK_NULL_HANDLE)),
    mPhysicalDevice(std::exchange(other.mPhysicalDevice, VK_NULL_HANDLE)),
    mPhysicalDeviceIndex(std::exchange(other.mPhysicalDeviceIndex, 0)),
    mDevice(std::exchange(other.mDevice, VK_NULL_HANDLE)),
    mQueue(std::exchange(other.mQueue, VK_NULL_HANDLE)),
    mQueueFamilyIndex(std::exchange(other.mQueueFamilyIndex, VK_QUEUE_FAMILY_IGNORED)),
    mQueueIndex(std::exchange(other.mQueueIndex, 0)),
    mDescription(std::exchange(other.mDescription, {})),
    mDestinationDescription(std::exchange(other.mDestinationDescription, {})),
    mResidentRevision(std::exchange(other.mResidentRevision, 0)),
    mResidentContentIdentity(std::exchange(other.mResidentContentIdentity, 0)),
    mDestinationImageView(std::exchange(other.mDestinationImageView, VK_NULL_HANDLE)),
    mSampler(std::exchange(other.mSampler, VK_NULL_HANDLE)),
    mDescriptorSetLayout(std::exchange(other.mDescriptorSetLayout, VK_NULL_HANDLE)),
    mPipelineLayout(std::exchange(other.mPipelineLayout, VK_NULL_HANDLE)),
    mDescriptorPool(std::exchange(other.mDescriptorPool, VK_NULL_HANDLE)),
    mDescriptorSet(std::exchange(other.mDescriptorSet, VK_NULL_HANDLE)),
    mDestroySampler(std::exchange(other.mDestroySampler, nullptr)),
    mDestroyDescriptorSetLayout(std::exchange(other.mDestroyDescriptorSetLayout, nullptr)),
    mDestroyPipelineLayout(std::exchange(other.mDestroyPipelineLayout, nullptr)),
    mDestroyDescriptorPool(std::exchange(other.mDestroyDescriptorPool, nullptr))
{
}

bool VulkanTextureUploadSampleBindingGeneration::createdFor(const VulkanPhysicalDeviceGeneration&           physical,
                                                            const VulkanLogicalDeviceGeneration&            logical,
                                                            const VulkanTextureUploadDestinationGeneration& destination) const noexcept
{
    return mSampler != VK_NULL_HANDLE && mDescriptorSetLayout != VK_NULL_HANDLE && mPipelineLayout != VK_NULL_HANDLE &&
           mDescriptorPool != VK_NULL_HANDLE && mDescriptorSet != VK_NULL_HANDLE && mPhysicalDeviceGeneration == &physical &&
           mLogicalDeviceGeneration == &logical && mDestinationGeneration == &destination && validPhysical(physical) &&
           validLogical(physical, logical) && validResidentDestination(physical, logical, mDestinationDescription, destination) &&
           mGetInstanceProcAddr == physical.getInstanceProcAddr() && mInstance == physical.instance() && mSurface == physical.surface() &&
           mPhysicalDevice == physical.physicalDevice() && mPhysicalDeviceIndex == physical.physicalDeviceIndex() &&
           mDevice == logical.device() && mQueue == logical.queue() && mQueueFamilyIndex == logical.queueFamilyIndex() &&
           mQueueIndex == logical.queueIndex() && canonicalDescription(mDescription) &&
           mDestinationDescription == vulkanTextureUploadDestinationDescription() && mDestinationImageView == destination.imageView() &&
           mResidentRevision == destination.residentRevision() && mResidentContentIdentity == destination.residentContentIdentity() &&
           mResidentRevision != 0 && mResidentContentIdentity != 0 && mDestroySampler != nullptr &&
           mDestroyDescriptorSetLayout != nullptr && mDestroyPipelineLayout != nullptr && mDestroyDescriptorPool != nullptr;
}

bool VulkanTextureUploadSampleBindingGeneration::matchesDescription(
    const VulkanTextureUploadSampleBindingDescription& description) const noexcept
{
    return mSampler != VK_NULL_HANDLE && mDescription == description;
}

bool VulkanTextureUploadSampleBindingGeneration::retainsTextureUploadDestinationGeneration(
    const VulkanTextureUploadDestinationGeneration& destination) const noexcept
{
    return mSampler != VK_NULL_HANDLE && mDescriptorSetLayout != VK_NULL_HANDLE && mPipelineLayout != VK_NULL_HANDLE &&
           mDescriptorPool != VK_NULL_HANDLE && mDescriptorSet != VK_NULL_HANDLE && mDestinationGeneration == &destination;
}

void VulkanTextureUploadSampleBindingGeneration::reset() noexcept
{
    const VkDevice                         device                        = std::exchange(mDevice, VK_NULL_HANDLE);
    const VkDescriptorPool                 descriptor_pool               = std::exchange(mDescriptorPool, VK_NULL_HANDLE);
    const VkPipelineLayout                 pipeline_layout               = std::exchange(mPipelineLayout, VK_NULL_HANDLE);
    const VkDescriptorSetLayout            descriptor_set_layout         = std::exchange(mDescriptorSetLayout, VK_NULL_HANDLE);
    const VkSampler                        sampler                       = std::exchange(mSampler, VK_NULL_HANDLE);
    const PFN_vkDestroyDescriptorPool      destroy_descriptor_pool       = std::exchange(mDestroyDescriptorPool, nullptr);
    const PFN_vkDestroyPipelineLayout      destroy_pipeline_layout       = std::exchange(mDestroyPipelineLayout, nullptr);
    const PFN_vkDestroyDescriptorSetLayout destroy_descriptor_set_layout = std::exchange(mDestroyDescriptorSetLayout, nullptr);
    const PFN_vkDestroySampler             destroy_sampler               = std::exchange(mDestroySampler, nullptr);

    mPhysicalDeviceGeneration = nullptr;
    mLogicalDeviceGeneration  = nullptr;
    mDestinationGeneration    = nullptr;
    mGetInstanceProcAddr      = nullptr;
    mInstance                 = VK_NULL_HANDLE;
    mSurface                  = VK_NULL_HANDLE;
    mPhysicalDevice           = VK_NULL_HANDLE;
    mPhysicalDeviceIndex      = 0;
    mQueue                    = VK_NULL_HANDLE;
    mQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
    mQueueIndex               = 0;
    mDescription              = {};
    mDestinationDescription   = {};
    mResidentRevision         = 0;
    mResidentContentIdentity  = 0;
    mDestinationImageView     = VK_NULL_HANDLE;
    mDescriptorSet            = VK_NULL_HANDLE;

    if (descriptor_pool != VK_NULL_HANDLE && destroy_descriptor_pool)
    {
        destroy_descriptor_pool(device, descriptor_pool, nullptr);
    }
    if (pipeline_layout != VK_NULL_HANDLE && destroy_pipeline_layout)
    {
        destroy_pipeline_layout(device, pipeline_layout, nullptr);
    }
    if (descriptor_set_layout != VK_NULL_HANDLE && destroy_descriptor_set_layout)
    {
        destroy_descriptor_set_layout(device, descriptor_set_layout, nullptr);
    }
    if (sampler != VK_NULL_HANDLE && destroy_sampler)
    {
        destroy_sampler(device, sampler, nullptr);
    }
}

VulkanTextureUploadSampleBindingResolutionResult resolveVulkanTextureUploadSampleBindingGeneration(
    const VulkanPhysicalDeviceGeneration&              physical,
    const VulkanLogicalDeviceGeneration&               logical,
    const VulkanTextureUploadDestinationDescription&   destination_description,
    const VulkanTextureUploadSampleBindingDescription& description,
    const VulkanTextureUploadDestinationGeneration&    destination) noexcept
{
    const VulkanTextureUploadDestinationDescription   owned_destination_description = destination_description;
    const VulkanTextureUploadSampleBindingDescription owned_description             = description;
    if (!validPhysical(physical))
    {
        return failure(VulkanTextureUploadSampleBindingResolutionCode::InvalidPhysicalDeviceGeneration);
    }
    if (!validLogical(physical, logical))
    {
        return failure(VulkanTextureUploadSampleBindingResolutionCode::InvalidLogicalDeviceGeneration);
    }
    if (owned_destination_description != vulkanTextureUploadDestinationDescription() || !canonicalDescription(owned_description))
    {
        return failure(VulkanTextureUploadSampleBindingResolutionCode::InvalidDescription);
    }
    if (!validDestinationBase(physical, logical, owned_destination_description, destination))
    {
        return failure(VulkanTextureUploadSampleBindingResolutionCode::InvalidTextureUploadDestinationGeneration);
    }
    if (!destination.isResident())
    {
        if (destination.currentState() == LLRenderContract::ImageState::Undefined && destination.residentRevision() == 0 &&
            destination.residentContentIdentity() == 0)
        {
            return failure(VulkanTextureUploadSampleBindingResolutionCode::DestinationNotResident);
        }
        return failure(VulkanTextureUploadSampleBindingResolutionCode::InvalidTextureUploadDestinationGeneration);
    }
    if (!validResidentDestination(physical, logical, owned_destination_description, destination))
    {
        return failure(VulkanTextureUploadSampleBindingResolutionCode::InvalidTextureUploadDestinationGeneration);
    }

    const VkImageView   destination_image_view    = destination.imageView();
    const std::uint64_t resident_revision         = destination.residentRevision();
    const std::uint64_t resident_content_identity = destination.residentContentIdentity();
    auto                current                   = [&]() noexcept
    {
        return parentsAreCurrent(physical,
                                 logical,
                                 description,
                                 owned_description,
                                 destination_description,
                                 owned_destination_description,
                                 destination,
                                 destination_image_view,
                                 resident_revision,
                                 resident_content_identity);
    };

    Dispatch dispatch;
    if (auto error = resolveDispatch(physical,
                                     logical,
                                     description,
                                     owned_description,
                                     destination_description,
                                     owned_destination_description,
                                     destination,
                                     destination_image_view,
                                     resident_revision,
                                     resident_content_identity,
                                     dispatch))
    {
        return *error;
    }

    const VkDevice device = logical.device();

    VkSamplerCreateInfo sampler_info{};
    sampler_info.sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter               = VK_FILTER_LINEAR;
    sampler_info.minFilter               = VK_FILTER_LINEAR;
    sampler_info.mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_info.addressModeU            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.mipLodBias              = 0.f;
    sampler_info.anisotropyEnable        = VK_FALSE;
    sampler_info.maxAnisotropy           = 1.f;
    sampler_info.compareEnable           = VK_FALSE;
    sampler_info.compareOp               = VK_COMPARE_OP_ALWAYS;
    sampler_info.minLod                  = 0.f;
    sampler_info.maxLod                  = static_cast<float>(LLRenderContract::TEXTURE_UPLOAD_MIP_LEVELS - 1);
    sampler_info.borderColor             = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    sampler_info.unnormalizedCoordinates = VK_FALSE;

    VkSampler      sampler        = VK_NULL_HANDLE;
    const VkResult sampler_result = dispatch.mCreateSampler(device, &sampler_info, nullptr, &sampler);
    const bool     owns_sampler   = sampler_result == VK_SUCCESS && sampler != VK_NULL_HANDLE;
    if (!current())
    {
        rollback(dispatch, device, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, owns_sampler ? sampler : VK_NULL_HANDLE);
        return failure(VulkanTextureUploadSampleBindingResolutionCode::ParentGenerationChanged);
    }
    if (sampler_result != VK_SUCCESS)
    {
        return failure(VulkanTextureUploadSampleBindingResolutionCode::SamplerCreationFailure,
                       VulkanTextureUploadSampleBindingCommand::CreateSampler,
                       sampler_result);
    }
    if (sampler == VK_NULL_HANDLE)
    {
        return failure(VulkanTextureUploadSampleBindingResolutionCode::NullSamplerOnSuccess,
                       VulkanTextureUploadSampleBindingCommand::CreateSampler);
    }

    VkDescriptorSetLayoutBinding layout_binding{};
    layout_binding.binding            = owned_description.mBinding;
    layout_binding.descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    layout_binding.descriptorCount    = 1;
    layout_binding.stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT;
    layout_binding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo descriptor_set_layout_info{};
    descriptor_set_layout_info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptor_set_layout_info.bindingCount = 1;
    descriptor_set_layout_info.pBindings    = &layout_binding;

    VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
    const VkResult        descriptor_set_layout_result =
        dispatch.mCreateDescriptorSetLayout(device, &descriptor_set_layout_info, nullptr, &descriptor_set_layout);
    const bool owns_descriptor_set_layout = descriptor_set_layout_result == VK_SUCCESS && descriptor_set_layout != VK_NULL_HANDLE;
    if (!current())
    {
        rollback(dispatch,
                 device,
                 VK_NULL_HANDLE,
                 VK_NULL_HANDLE,
                 owns_descriptor_set_layout ? descriptor_set_layout : VK_NULL_HANDLE,
                 sampler);
        return failure(VulkanTextureUploadSampleBindingResolutionCode::ParentGenerationChanged);
    }
    if (descriptor_set_layout_result != VK_SUCCESS)
    {
        rollback(dispatch, device, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, sampler);
        return failure(VulkanTextureUploadSampleBindingResolutionCode::DescriptorSetLayoutCreationFailure,
                       VulkanTextureUploadSampleBindingCommand::CreateDescriptorSetLayout,
                       descriptor_set_layout_result);
    }
    if (descriptor_set_layout == VK_NULL_HANDLE)
    {
        rollback(dispatch, device, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, sampler);
        return failure(VulkanTextureUploadSampleBindingResolutionCode::NullDescriptorSetLayoutOnSuccess,
                       VulkanTextureUploadSampleBindingCommand::CreateDescriptorSetLayout);
    }

    VkPipelineLayoutCreateInfo pipeline_layout_info{};
    pipeline_layout_info.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.setLayoutCount = 1;
    pipeline_layout_info.pSetLayouts    = &descriptor_set_layout;

    VkPipelineLayout pipeline_layout        = VK_NULL_HANDLE;
    const VkResult   pipeline_layout_result = dispatch.mCreatePipelineLayout(device, &pipeline_layout_info, nullptr, &pipeline_layout);
    const bool       owns_pipeline_layout   = pipeline_layout_result == VK_SUCCESS && pipeline_layout != VK_NULL_HANDLE;
    if (!current())
    {
        rollback(dispatch, device, VK_NULL_HANDLE, owns_pipeline_layout ? pipeline_layout : VK_NULL_HANDLE, descriptor_set_layout, sampler);
        return failure(VulkanTextureUploadSampleBindingResolutionCode::ParentGenerationChanged);
    }
    if (pipeline_layout_result != VK_SUCCESS)
    {
        rollback(dispatch, device, VK_NULL_HANDLE, VK_NULL_HANDLE, descriptor_set_layout, sampler);
        return failure(VulkanTextureUploadSampleBindingResolutionCode::PipelineLayoutCreationFailure,
                       VulkanTextureUploadSampleBindingCommand::CreatePipelineLayout,
                       pipeline_layout_result);
    }
    if (pipeline_layout == VK_NULL_HANDLE)
    {
        rollback(dispatch, device, VK_NULL_HANDLE, VK_NULL_HANDLE, descriptor_set_layout, sampler);
        return failure(VulkanTextureUploadSampleBindingResolutionCode::NullPipelineLayoutOnSuccess,
                       VulkanTextureUploadSampleBindingCommand::CreatePipelineLayout);
    }

    VkDescriptorPoolSize pool_size{};
    pool_size.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_size.descriptorCount = 1;

    VkDescriptorPoolCreateInfo descriptor_pool_info{};
    descriptor_pool_info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptor_pool_info.maxSets       = 1;
    descriptor_pool_info.poolSizeCount = 1;
    descriptor_pool_info.pPoolSizes    = &pool_size;

    VkDescriptorPool descriptor_pool        = VK_NULL_HANDLE;
    const VkResult   descriptor_pool_result = dispatch.mCreateDescriptorPool(device, &descriptor_pool_info, nullptr, &descriptor_pool);
    const bool       owns_descriptor_pool   = descriptor_pool_result == VK_SUCCESS && descriptor_pool != VK_NULL_HANDLE;
    if (!current())
    {
        rollback(dispatch,
                 device,
                 owns_descriptor_pool ? descriptor_pool : VK_NULL_HANDLE,
                 pipeline_layout,
                 descriptor_set_layout,
                 sampler);
        return failure(VulkanTextureUploadSampleBindingResolutionCode::ParentGenerationChanged);
    }
    if (descriptor_pool_result != VK_SUCCESS)
    {
        rollback(dispatch, device, VK_NULL_HANDLE, pipeline_layout, descriptor_set_layout, sampler);
        return failure(VulkanTextureUploadSampleBindingResolutionCode::DescriptorPoolCreationFailure,
                       VulkanTextureUploadSampleBindingCommand::CreateDescriptorPool,
                       descriptor_pool_result);
    }
    if (descriptor_pool == VK_NULL_HANDLE)
    {
        rollback(dispatch, device, VK_NULL_HANDLE, pipeline_layout, descriptor_set_layout, sampler);
        return failure(VulkanTextureUploadSampleBindingResolutionCode::NullDescriptorPoolOnSuccess,
                       VulkanTextureUploadSampleBindingCommand::CreateDescriptorPool);
    }

    VkDescriptorSetAllocateInfo allocate_info{};
    allocate_info.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocate_info.descriptorPool     = descriptor_pool;
    allocate_info.descriptorSetCount = 1;
    allocate_info.pSetLayouts        = &descriptor_set_layout;

    VkDescriptorSet descriptor_set        = VK_NULL_HANDLE;
    const VkResult  descriptor_set_result = dispatch.mAllocateDescriptorSets(device, &allocate_info, &descriptor_set);
    if (!current())
    {
        rollback(dispatch, device, descriptor_pool, pipeline_layout, descriptor_set_layout, sampler);
        return failure(VulkanTextureUploadSampleBindingResolutionCode::ParentGenerationChanged);
    }
    if (descriptor_set_result != VK_SUCCESS)
    {
        rollback(dispatch, device, descriptor_pool, pipeline_layout, descriptor_set_layout, sampler);
        return failure(VulkanTextureUploadSampleBindingResolutionCode::DescriptorSetAllocationFailure,
                       VulkanTextureUploadSampleBindingCommand::AllocateDescriptorSets,
                       descriptor_set_result);
    }
    if (descriptor_set == VK_NULL_HANDLE)
    {
        rollback(dispatch, device, descriptor_pool, pipeline_layout, descriptor_set_layout, sampler);
        return failure(VulkanTextureUploadSampleBindingResolutionCode::NullDescriptorSetOnSuccess,
                       VulkanTextureUploadSampleBindingCommand::AllocateDescriptorSets);
    }

    VkDescriptorImageInfo image_info{};
    image_info.sampler     = sampler;
    image_info.imageView   = destination_image_view;
    image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet          = descriptor_set;
    write.dstBinding      = owned_description.mBinding;
    write.dstArrayElement = 0;
    write.descriptorCount = 1;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo      = &image_info;

    dispatch.mUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    if (!current())
    {
        rollback(dispatch, device, descriptor_pool, pipeline_layout, descriptor_set_layout, sampler);
        return failure(VulkanTextureUploadSampleBindingResolutionCode::ParentGenerationChanged);
    }

    return VulkanTextureUploadSampleBindingGenerationFactory::create(physical,
                                                                     logical,
                                                                     owned_description,
                                                                     owned_destination_description,
                                                                     destination,
                                                                     resident_revision,
                                                                     resident_content_identity,
                                                                     destination_image_view,
                                                                     sampler,
                                                                     descriptor_set_layout,
                                                                     pipeline_layout,
                                                                     descriptor_pool,
                                                                     descriptor_set,
                                                                     dispatch);
}

} // namespace LLRenderVulkan
