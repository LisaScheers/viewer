/**
 * @file llrendervulkantextureuploaddestination.cpp
 * @brief Loader-neutral ownership of one Vulkan streamed-texture image.
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

#include "llrendervulkantextureuploaddestination.h"

#include <optional>
#include <utility>

namespace LLRenderVulkan
{

namespace
{
    constexpr VkFormatFeatureFlags REQUIRED_FORMAT_FEATURES =
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT |
        VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;

    constexpr VkImageUsageFlags IMAGE_USAGE =
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

    struct TextureUploadDestinationDispatch
    {
        PFN_vkGetPhysicalDeviceFormatProperties      mGetPhysicalDeviceFormatProperties      = nullptr;
        PFN_vkGetPhysicalDeviceImageFormatProperties mGetPhysicalDeviceImageFormatProperties = nullptr;
        PFN_vkGetPhysicalDeviceMemoryProperties      mGetPhysicalDeviceMemoryProperties      = nullptr;
        PFN_vkGetDeviceProcAddr                      mGetDeviceProcAddr                      = nullptr;
        PFN_vkCreateImage                            mCreateImage                            = nullptr;
        PFN_vkDestroyImage                           mDestroyImage                           = nullptr;
        PFN_vkGetImageMemoryRequirements2            mGetImageMemoryRequirements2            = nullptr;
        PFN_vkAllocateMemory                         mAllocateMemory                         = nullptr;
        PFN_vkFreeMemory                             mFreeMemory                             = nullptr;
        PFN_vkBindImageMemory                        mBindImageMemory                        = nullptr;
        PFN_vkCreateImageView                        mCreateImageView                        = nullptr;
        PFN_vkDestroyImageView                       mDestroyImageView                       = nullptr;
    };

    VulkanTextureUploadDestinationResolutionError failure(VulkanTextureUploadDestinationResolutionCode            code,
                                                          std::optional<VulkanTextureUploadDestinationCommand>    command    = std::nullopt,
                                                          VkResult                                                result     = VK_SUCCESS,
                                                          std::optional<VulkanTextureUploadDestinationCapability> capability = std::nullopt,
                                                          std::uint64_t                                           required_value = 0,
                                                          std::uint64_t available_value = 0) noexcept
    {
        return { code, command, capability, result, required_value, available_value };
    }

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

    bool canonicalDescription(const VulkanTextureUploadDestinationDescription& description) noexcept
    {
        return description == vulkanTextureUploadDestinationDescription();
    }

    std::optional<VulkanTextureUploadDestinationResolutionError> validateInputs(
        const VulkanPhysicalDeviceGeneration&            physical_device_generation,
        const VulkanLogicalDeviceGeneration&             logical_device_generation,
        const VulkanTextureUploadDestinationDescription& description) noexcept
    {
        if (physical_device_generation.getInstanceProcAddr() == nullptr || physical_device_generation.instance() == VK_NULL_HANDLE ||
            physical_device_generation.surface() == VK_NULL_HANDLE || physical_device_generation.physicalDevice() == VK_NULL_HANDLE ||
            physical_device_generation.queueFamilyIndex() == VK_QUEUE_FAMILY_IGNORED)
        {
            return failure(VulkanTextureUploadDestinationResolutionCode::InvalidPhysicalDeviceGeneration);
        }
        if (!logical_device_generation.createdFor(physical_device_generation))
        {
            return failure(VulkanTextureUploadDestinationResolutionCode::InvalidLogicalDeviceGeneration);
        }
        if (!canonicalDescription(description))
        {
            return failure(VulkanTextureUploadDestinationResolutionCode::InvalidDescription);
        }
        return std::nullopt;
    }

    std::optional<VulkanTextureUploadDestinationResolutionError> resolveDispatch(
        const VulkanPhysicalDeviceGeneration&            physical_device_generation,
        const VulkanLogicalDeviceGeneration&             logical_device_generation,
        const VulkanTextureUploadDestinationDescription& description,
        TextureUploadDestinationDispatch&                dispatch) noexcept
    {
        const PFN_vkGetInstanceProcAddr get_instance_proc_addr = physical_device_generation.getInstanceProcAddr();
        const VkInstance                instance               = physical_device_generation.instance();
        const VkDevice                  device                 = logical_device_generation.device();

#define LL_RESOLVE_TEXTURE_UPLOAD_DESTINATION_INSTANCE_COMMAND(member, type, command_name, command_value)    \
    dispatch.member = resolveInstance<type>(get_instance_proc_addr, instance, command_name);                 \
    if (auto error = validateInputs(physical_device_generation, logical_device_generation, description))     \
    {                                                                                                        \
        return *error;                                                                                       \
    }                                                                                                        \
    if (!dispatch.member)                                                                                    \
    {                                                                                                        \
        return failure(VulkanTextureUploadDestinationResolutionCode::MissingRequiredCommand, command_value); \
    }

        LL_RESOLVE_TEXTURE_UPLOAD_DESTINATION_INSTANCE_COMMAND(mGetPhysicalDeviceFormatProperties, PFN_vkGetPhysicalDeviceFormatProperties,
                                                               "vkGetPhysicalDeviceFormatProperties",
                                                               VulkanTextureUploadDestinationCommand::GetPhysicalDeviceFormatProperties)
        LL_RESOLVE_TEXTURE_UPLOAD_DESTINATION_INSTANCE_COMMAND(
            mGetPhysicalDeviceImageFormatProperties, PFN_vkGetPhysicalDeviceImageFormatProperties,
            "vkGetPhysicalDeviceImageFormatProperties", VulkanTextureUploadDestinationCommand::GetPhysicalDeviceImageFormatProperties)
        LL_RESOLVE_TEXTURE_UPLOAD_DESTINATION_INSTANCE_COMMAND(mGetPhysicalDeviceMemoryProperties, PFN_vkGetPhysicalDeviceMemoryProperties,
                                                               "vkGetPhysicalDeviceMemoryProperties",
                                                               VulkanTextureUploadDestinationCommand::GetPhysicalDeviceMemoryProperties)
        LL_RESOLVE_TEXTURE_UPLOAD_DESTINATION_INSTANCE_COMMAND(mGetDeviceProcAddr, PFN_vkGetDeviceProcAddr, "vkGetDeviceProcAddr",
                                                               VulkanTextureUploadDestinationCommand::GetDeviceProcAddr)

#undef LL_RESOLVE_TEXTURE_UPLOAD_DESTINATION_INSTANCE_COMMAND

#define LL_RESOLVE_TEXTURE_UPLOAD_DESTINATION_DEVICE_COMMAND(member, type, command_name, command_value)      \
    dispatch.member = resolveDevice<type>(dispatch.mGetDeviceProcAddr, device, command_name);                \
    if (auto error = validateInputs(physical_device_generation, logical_device_generation, description))     \
    {                                                                                                        \
        return *error;                                                                                       \
    }                                                                                                        \
    if (!dispatch.member)                                                                                    \
    {                                                                                                        \
        return failure(VulkanTextureUploadDestinationResolutionCode::MissingRequiredCommand, command_value); \
    }

        LL_RESOLVE_TEXTURE_UPLOAD_DESTINATION_DEVICE_COMMAND(mCreateImage, PFN_vkCreateImage, "vkCreateImage",
                                                             VulkanTextureUploadDestinationCommand::CreateImage)
        LL_RESOLVE_TEXTURE_UPLOAD_DESTINATION_DEVICE_COMMAND(mDestroyImage, PFN_vkDestroyImage, "vkDestroyImage",
                                                             VulkanTextureUploadDestinationCommand::DestroyImage)
        LL_RESOLVE_TEXTURE_UPLOAD_DESTINATION_DEVICE_COMMAND(mGetImageMemoryRequirements2, PFN_vkGetImageMemoryRequirements2,
                                                             "vkGetImageMemoryRequirements2",
                                                             VulkanTextureUploadDestinationCommand::GetImageMemoryRequirements2)
        LL_RESOLVE_TEXTURE_UPLOAD_DESTINATION_DEVICE_COMMAND(mAllocateMemory, PFN_vkAllocateMemory, "vkAllocateMemory",
                                                             VulkanTextureUploadDestinationCommand::AllocateMemory)
        LL_RESOLVE_TEXTURE_UPLOAD_DESTINATION_DEVICE_COMMAND(mFreeMemory, PFN_vkFreeMemory, "vkFreeMemory",
                                                             VulkanTextureUploadDestinationCommand::FreeMemory)
        LL_RESOLVE_TEXTURE_UPLOAD_DESTINATION_DEVICE_COMMAND(mBindImageMemory, PFN_vkBindImageMemory, "vkBindImageMemory",
                                                             VulkanTextureUploadDestinationCommand::BindImageMemory)
        LL_RESOLVE_TEXTURE_UPLOAD_DESTINATION_DEVICE_COMMAND(mCreateImageView, PFN_vkCreateImageView, "vkCreateImageView",
                                                             VulkanTextureUploadDestinationCommand::CreateImageView)
        LL_RESOLVE_TEXTURE_UPLOAD_DESTINATION_DEVICE_COMMAND(mDestroyImageView, PFN_vkDestroyImageView, "vkDestroyImageView",
                                                             VulkanTextureUploadDestinationCommand::DestroyImageView)

#undef LL_RESOLVE_TEXTURE_UPLOAD_DESTINATION_DEVICE_COMMAND

        return std::nullopt;
    }

    std::optional<VulkanTextureUploadDestinationResolutionError> imageFormatError(
        const VkImageFormatProperties&                   properties,
        const VulkanTextureUploadDestinationDescription& description) noexcept
    {
        const auto insufficient =
            [](VulkanTextureUploadDestinationCapability capability, std::uint64_t required, std::uint64_t available) noexcept
        {
            return failure(VulkanTextureUploadDestinationResolutionCode::UnsupportedImageFormatLimits,
                           VulkanTextureUploadDestinationCommand::GetPhysicalDeviceImageFormatProperties,
                           VK_SUCCESS,
                           capability,
                           required,
                           available);
        };
        if (properties.maxExtent.width < description.mResidentExtent.mWidth)
        {
            return insufficient(VulkanTextureUploadDestinationCapability::ExtentWidth,
                                description.mResidentExtent.mWidth,
                                properties.maxExtent.width);
        }
        if (properties.maxExtent.height < description.mResidentExtent.mHeight)
        {
            return insufficient(VulkanTextureUploadDestinationCapability::ExtentHeight,
                                description.mResidentExtent.mHeight,
                                properties.maxExtent.height);
        }
        if (properties.maxExtent.depth < 1)
        {
            return insufficient(VulkanTextureUploadDestinationCapability::ExtentDepth, 1, properties.maxExtent.depth);
        }
        if (properties.maxMipLevels < description.mMipLevels)
        {
            return insufficient(VulkanTextureUploadDestinationCapability::MipLevels, description.mMipLevels, properties.maxMipLevels);
        }
        if (properties.maxArrayLayers < description.mArrayLayers)
        {
            return insufficient(VulkanTextureUploadDestinationCapability::ArrayLayers, description.mArrayLayers, properties.maxArrayLayers);
        }
        if ((properties.sampleCounts & VK_SAMPLE_COUNT_1_BIT) == 0)
        {
            return insufficient(VulkanTextureUploadDestinationCapability::SampleCountOne, VK_SAMPLE_COUNT_1_BIT, properties.sampleCounts);
        }
        return std::nullopt;
    }

    bool validImageFormatProperties(const VkImageFormatProperties&                   properties,
                                    const VulkanTextureUploadDestinationDescription& description) noexcept
    {
        return !imageFormatError(properties, description);
    }

    bool validMemoryProperties(const VkPhysicalDeviceMemoryProperties& properties) noexcept
    {
        if (properties.memoryTypeCount == 0 || properties.memoryTypeCount > VK_MAX_MEMORY_TYPES || properties.memoryHeapCount == 0 ||
            properties.memoryHeapCount > VK_MAX_MEMORY_HEAPS)
        {
            return false;
        }
        for (std::uint32_t type_index = 0; type_index < properties.memoryTypeCount; ++type_index)
        {
            if (properties.memoryTypes[type_index].heapIndex >= properties.memoryHeapCount)
            {
                return false;
            }
        }
        return true;
    }

    bool validMemoryRequirements(const VkMemoryRequirements& requirements) noexcept
    {
        return requirements.size != 0 && requirements.alignment != 0 && (requirements.alignment & (requirements.alignment - 1)) == 0 &&
               requirements.memoryTypeBits != 0;
    }

    std::optional<std::uint32_t> selectMemoryType(const VkPhysicalDeviceMemoryProperties& properties,
                                                  const VkMemoryRequirements&             requirements) noexcept
    {
        constexpr VkMemoryPropertyFlags forbidden_type_flags =
            VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD | VK_MEMORY_PROPERTY_PROTECTED_BIT | VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT;
        constexpr VkMemoryHeapFlags forbidden_heap_flags = VK_MEMORY_HEAP_TILE_MEMORY_BIT_QCOM;

        for (std::uint32_t index = 0; index < properties.memoryTypeCount; ++index)
        {
            const std::uint32_t bit = std::uint32_t{ 1 } << index;
            if ((requirements.memoryTypeBits & bit) == 0)
            {
                continue;
            }
            const VkMemoryType& type = properties.memoryTypes[index];
            const VkMemoryHeap& heap = properties.memoryHeaps[type.heapIndex];
            if ((type.propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == 0 || (type.propertyFlags & forbidden_type_flags) != 0 ||
                (heap.flags & forbidden_heap_flags) != 0 || heap.size < requirements.size)
            {
                continue;
            }
            return index;
        }
        return std::nullopt;
    }

    VkImageCreateInfo imageCreateInfo(const VulkanTextureUploadDestinationDescription& description) noexcept
    {
        VkImageCreateInfo info{};
        info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.flags         = 0;
        info.imageType     = VK_IMAGE_TYPE_2D;
        info.format        = VK_FORMAT_R8G8B8A8_UNORM;
        info.extent        = { description.mResidentExtent.mWidth, description.mResidentExtent.mHeight, 1 };
        info.mipLevels     = description.mMipLevels;
        info.arrayLayers   = description.mArrayLayers;
        info.samples       = VK_SAMPLE_COUNT_1_BIT;
        info.tiling        = VK_IMAGE_TILING_OPTIMAL;
        info.usage         = IMAGE_USAGE;
        info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        return info;
    }

    VkImageViewCreateInfo imageViewCreateInfo(VkImage image, const VulkanTextureUploadDestinationDescription& description) noexcept
    {
        VkImageViewCreateInfo info{};
        info.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        info.image                           = image;
        info.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        info.format                          = VK_FORMAT_R8G8B8A8_UNORM;
        info.components.r                    = VK_COMPONENT_SWIZZLE_IDENTITY;
        info.components.g                    = VK_COMPONENT_SWIZZLE_IDENTITY;
        info.components.b                    = VK_COMPONENT_SWIZZLE_IDENTITY;
        info.components.a                    = VK_COMPONENT_SWIZZLE_IDENTITY;
        info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        info.subresourceRange.baseMipLevel   = 0;
        info.subresourceRange.levelCount     = description.mMipLevels;
        info.subresourceRange.baseArrayLayer = 0;
        info.subresourceRange.layerCount     = description.mArrayLayers;
        return info;
    }

    void rollBack(const TextureUploadDestinationDispatch& dispatch,
                  VkDevice                                device,
                  VkImageView                             image_view,
                  bool                                    owns_image_view,
                  VkImage                                 image,
                  bool                                    owns_image,
                  VkDeviceMemory                          memory,
                  bool                                    owns_memory) noexcept
    {
        if (owns_image_view)
        {
            dispatch.mDestroyImageView(device, image_view, nullptr);
        }
        if (owns_image)
        {
            dispatch.mDestroyImage(device, image, nullptr);
        }
        if (owns_memory)
        {
            dispatch.mFreeMemory(device, memory, nullptr);
        }
    }

} // namespace

struct VulkanTextureUploadDestinationGenerationFactory
{
    static VulkanTextureUploadDestinationGeneration create(const VulkanPhysicalDeviceGeneration&            physical_device_generation,
                                                           const VulkanLogicalDeviceGeneration&             logical_device_generation,
                                                           const VulkanTextureUploadDestinationDescription& description,
                                                           VkFormatFeatureFlags                             format_features,
                                                           const VkImageFormatProperties&                   image_format_properties,
                                                           VkImage                                          image,
                                                           VkDeviceMemory                                   memory,
                                                           const VkMemoryRequirements&                      memory_requirements,
                                                           std::uint32_t                                    memory_type_index,
                                                           VkMemoryPropertyFlags                            memory_property_flags,
                                                           bool                                             prefers_dedicated_allocation,
                                                           bool                                             requires_dedicated_allocation,
                                                           VkImageView                                      image_view,
                                                           PFN_vkDestroyImageView                           destroy_image_view,
                                                           PFN_vkDestroyImage                               destroy_image,
                                                           PFN_vkFreeMemory                                 free_memory) noexcept
    {
        return VulkanTextureUploadDestinationGeneration(physical_device_generation,
                                                        logical_device_generation,
                                                        description,
                                                        format_features,
                                                        image_format_properties,
                                                        image,
                                                        memory,
                                                        memory_requirements,
                                                        memory_type_index,
                                                        memory_property_flags,
                                                        prefers_dedicated_allocation,
                                                        requires_dedicated_allocation,
                                                        image_view,
                                                        destroy_image_view,
                                                        destroy_image,
                                                        free_memory);
    }
};

VulkanTextureUploadDestinationGeneration::VulkanTextureUploadDestinationGeneration(
    const VulkanPhysicalDeviceGeneration&            physical_device_generation,
    const VulkanLogicalDeviceGeneration&             logical_device_generation,
    const VulkanTextureUploadDestinationDescription& description,
    VkFormatFeatureFlags                             format_features,
    const VkImageFormatProperties&                   image_format_properties,
    VkImage                                          image,
    VkDeviceMemory                                   memory,
    const VkMemoryRequirements&                      memory_requirements,
    std::uint32_t                                    memory_type_index,
    VkMemoryPropertyFlags                            memory_property_flags,
    bool                                             prefers_dedicated_allocation,
    bool                                             requires_dedicated_allocation,
    VkImageView                                      image_view,
    PFN_vkDestroyImageView                           destroy_image_view,
    PFN_vkDestroyImage                               destroy_image,
    PFN_vkFreeMemory                                 free_memory) noexcept :
    mPhysicalDeviceGeneration(&physical_device_generation),
    mLogicalDeviceGeneration(&logical_device_generation),
    mGetInstanceProcAddr(physical_device_generation.getInstanceProcAddr()),
    mInstance(physical_device_generation.instance()),
    mSurface(physical_device_generation.surface()),
    mPhysicalDevice(physical_device_generation.physicalDevice()),
    mPhysicalDeviceIndex(physical_device_generation.physicalDeviceIndex()),
    mDevice(logical_device_generation.device()),
    mQueue(logical_device_generation.queue()),
    mQueueFamilyIndex(logical_device_generation.queueFamilyIndex()),
    mQueueIndex(logical_device_generation.queueIndex()),
    mDescription(description),
    mCurrentState(description.mInitialState),
    mFormatFeatures(format_features),
    mImageFormatProperties(image_format_properties),
    mImage(image),
    mMemory(memory),
    mMemoryRequirements(memory_requirements),
    mMemoryTypeIndex(memory_type_index),
    mMemoryPropertyFlags(memory_property_flags),
    mPrefersDedicatedAllocation(prefers_dedicated_allocation),
    mRequiresDedicatedAllocation(requires_dedicated_allocation),
    mImageView(image_view),
    mDestroyImageView(destroy_image_view),
    mDestroyImage(destroy_image),
    mFreeMemory(free_memory)
{
}

VulkanTextureUploadDestinationGeneration::~VulkanTextureUploadDestinationGeneration() noexcept
{
    reset();
}

VulkanTextureUploadDestinationGeneration::VulkanTextureUploadDestinationGeneration(
    VulkanTextureUploadDestinationGeneration&& other) noexcept :
    mPhysicalDeviceGeneration(std::exchange(other.mPhysicalDeviceGeneration, nullptr)),
    mLogicalDeviceGeneration(std::exchange(other.mLogicalDeviceGeneration, nullptr)),
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
    mResidentRevision(std::exchange(other.mResidentRevision, 0)),
    mResidentContentIdentity(std::exchange(other.mResidentContentIdentity, 0)),
    mCurrentState(std::exchange(other.mCurrentState, LLRenderContract::ImageState::Undefined)),
    mFormatFeatures(std::exchange(other.mFormatFeatures, 0)),
    mImageFormatProperties(std::exchange(other.mImageFormatProperties, {})),
    mImage(std::exchange(other.mImage, VK_NULL_HANDLE)),
    mMemory(std::exchange(other.mMemory, VK_NULL_HANDLE)),
    mMemoryRequirements(std::exchange(other.mMemoryRequirements, {})),
    mMemoryTypeIndex(std::exchange(other.mMemoryTypeIndex, 0)),
    mMemoryPropertyFlags(std::exchange(other.mMemoryPropertyFlags, 0)),
    mPrefersDedicatedAllocation(std::exchange(other.mPrefersDedicatedAllocation, false)),
    mRequiresDedicatedAllocation(std::exchange(other.mRequiresDedicatedAllocation, false)),
    mImageView(std::exchange(other.mImageView, VK_NULL_HANDLE)),
    mDestroyImageView(std::exchange(other.mDestroyImageView, nullptr)),
    mDestroyImage(std::exchange(other.mDestroyImage, nullptr)),
    mFreeMemory(std::exchange(other.mFreeMemory, nullptr))
{
}

VkExtent3D VulkanTextureUploadDestinationGeneration::residentExtent() const noexcept
{
    const bool live_extent = mDescription.mResidentExtent.mWidth != 0 && mDescription.mResidentExtent.mHeight != 0;
    return { mDescription.mResidentExtent.mWidth, mDescription.mResidentExtent.mHeight, live_extent ? 1U : 0U };
}

bool VulkanTextureUploadDestinationGeneration::isDeviceLocal() const noexcept
{
    return (mMemoryPropertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0;
}

bool VulkanTextureUploadDestinationGeneration::isResident() const noexcept
{
    return mImage != VK_NULL_HANDLE && mMemory != VK_NULL_HANDLE && mImageView != VK_NULL_HANDLE && mResidentRevision != 0 &&
           mResidentRevision == mDescription.mExpectedRevision && mResidentContentIdentity != 0 &&
           mCurrentState == LLRenderContract::ImageState::ShaderRead;
}

VkImageSubresourceRange VulkanTextureUploadDestinationGeneration::viewRange() const noexcept
{
    return mImageView != VK_NULL_HANDLE
               ? VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, mDescription.mMipLevels, 0, mDescription.mArrayLayers }
               : VkImageSubresourceRange{};
}

bool VulkanTextureUploadDestinationGeneration::matchesDescription(
    const VulkanTextureUploadDestinationDescription& description) const noexcept
{
    return mImage != VK_NULL_HANDLE && mMemory != VK_NULL_HANDLE && mImageView != VK_NULL_HANDLE && mDescription == description;
}

bool VulkanTextureUploadDestinationGeneration::createdFor(const VulkanPhysicalDeviceGeneration& physical_device_generation,
                                                          const VulkanLogicalDeviceGeneration&  logical_device_generation) const noexcept
{
    return mImage != VK_NULL_HANDLE && mMemory != VK_NULL_HANDLE && mImageView != VK_NULL_HANDLE &&
           mPhysicalDeviceGeneration == &physical_device_generation && mLogicalDeviceGeneration == &logical_device_generation &&
           logical_device_generation.createdFor(physical_device_generation) &&
           mGetInstanceProcAddr == physical_device_generation.getInstanceProcAddr() && mInstance == physical_device_generation.instance() &&
           mSurface == physical_device_generation.surface() && mPhysicalDevice == physical_device_generation.physicalDevice() &&
           mPhysicalDeviceIndex == physical_device_generation.physicalDeviceIndex() && mDevice == logical_device_generation.device() &&
           mQueue == logical_device_generation.queue() && mQueueFamilyIndex == logical_device_generation.queueFamilyIndex() &&
           mQueueIndex == logical_device_generation.queueIndex() && canonicalDescription(mDescription) &&
           (mFormatFeatures & REQUIRED_FORMAT_FEATURES) == REQUIRED_FORMAT_FEATURES &&
           validImageFormatProperties(mImageFormatProperties, mDescription) && validMemoryRequirements(mMemoryRequirements) &&
           mMemoryTypeIndex < VK_MAX_MEMORY_TYPES && (mMemoryRequirements.memoryTypeBits & (std::uint32_t{ 1 } << mMemoryTypeIndex)) != 0 &&
           isDeviceLocal() && mDestroyImageView && mDestroyImage && mFreeMemory;
}

bool VulkanTextureUploadDestinationGeneration::markResident(std::uint64_t                expected_revision,
                                                            std::uint64_t                content_identity,
                                                            LLRenderContract::ImageState state) noexcept
{
    if (mImage == VK_NULL_HANDLE || mMemory == VK_NULL_HANDLE || mImageView == VK_NULL_HANDLE || expected_revision == 0 ||
        expected_revision != mDescription.mExpectedRevision || content_identity == 0 || state != LLRenderContract::ImageState::ShaderRead ||
        mResidentRevision != 0 || mResidentContentIdentity != 0 || mCurrentState != LLRenderContract::ImageState::Undefined)
    {
        return false;
    }

    mResidentRevision        = expected_revision;
    mResidentContentIdentity = content_identity;
    mCurrentState            = state;
    return true;
}

void VulkanTextureUploadDestinationGeneration::reset() noexcept
{
    const VkDevice               device             = std::exchange(mDevice, VK_NULL_HANDLE);
    const VkImageView            image_view         = std::exchange(mImageView, VK_NULL_HANDLE);
    const VkImage                image              = std::exchange(mImage, VK_NULL_HANDLE);
    const VkDeviceMemory         memory             = std::exchange(mMemory, VK_NULL_HANDLE);
    const PFN_vkDestroyImageView destroy_image_view = std::exchange(mDestroyImageView, nullptr);
    const PFN_vkDestroyImage     destroy_image      = std::exchange(mDestroyImage, nullptr);
    const PFN_vkFreeMemory       free_memory        = std::exchange(mFreeMemory, nullptr);

    mPhysicalDeviceGeneration    = nullptr;
    mLogicalDeviceGeneration     = nullptr;
    mGetInstanceProcAddr         = nullptr;
    mInstance                    = VK_NULL_HANDLE;
    mSurface                     = VK_NULL_HANDLE;
    mPhysicalDevice              = VK_NULL_HANDLE;
    mPhysicalDeviceIndex         = 0;
    mQueue                       = VK_NULL_HANDLE;
    mQueueFamilyIndex            = VK_QUEUE_FAMILY_IGNORED;
    mQueueIndex                  = 0;
    mDescription                 = {};
    mResidentRevision            = 0;
    mResidentContentIdentity     = 0;
    mCurrentState                = LLRenderContract::ImageState::Undefined;
    mFormatFeatures              = 0;
    mImageFormatProperties       = {};
    mMemoryRequirements          = {};
    mMemoryTypeIndex             = 0;
    mMemoryPropertyFlags         = 0;
    mPrefersDedicatedAllocation  = false;
    mRequiresDedicatedAllocation = false;

    if (image_view != VK_NULL_HANDLE && destroy_image_view)
    {
        destroy_image_view(device, image_view, nullptr);
    }
    if (image != VK_NULL_HANDLE && destroy_image)
    {
        destroy_image(device, image, nullptr);
    }
    if (memory != VK_NULL_HANDLE && free_memory)
    {
        free_memory(device, memory, nullptr);
    }
}

VulkanTextureUploadDestinationResolutionResult resolveVulkanTextureUploadDestinationGeneration(
    const VulkanPhysicalDeviceGeneration&            physical_device_generation,
    const VulkanLogicalDeviceGeneration&             logical_device_generation,
    const VulkanTextureUploadDestinationDescription& description) noexcept
{
    const VulkanTextureUploadDestinationDescription owned_description = description;
    if (auto error = validateInputs(physical_device_generation, logical_device_generation, owned_description))
    {
        return *error;
    }

    TextureUploadDestinationDispatch dispatch;
    if (auto error = resolveDispatch(physical_device_generation, logical_device_generation, owned_description, dispatch))
    {
        return *error;
    }
    if (auto error = validateInputs(physical_device_generation, logical_device_generation, owned_description))
    {
        return *error;
    }

    const VkPhysicalDevice physical_device = physical_device_generation.physicalDevice();
    const VkDevice         device          = logical_device_generation.device();

    VkFormatProperties format_properties{};
    dispatch.mGetPhysicalDeviceFormatProperties(physical_device, VK_FORMAT_R8G8B8A8_UNORM, &format_properties);
    if (auto error = validateInputs(physical_device_generation, logical_device_generation, owned_description))
    {
        return *error;
    }
    if ((format_properties.optimalTilingFeatures & REQUIRED_FORMAT_FEATURES) != REQUIRED_FORMAT_FEATURES)
    {
        return failure(VulkanTextureUploadDestinationResolutionCode::UnsupportedFormatFeatures,
                       VulkanTextureUploadDestinationCommand::GetPhysicalDeviceFormatProperties,
                       VK_SUCCESS,
                       VulkanTextureUploadDestinationCapability::FormatFeatures,
                       REQUIRED_FORMAT_FEATURES,
                       format_properties.optimalTilingFeatures);
    }

    VkImageFormatProperties image_format_properties{};
    const VkResult          image_format_result = dispatch.mGetPhysicalDeviceImageFormatProperties(
        physical_device, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, IMAGE_USAGE, 0, &image_format_properties);
    if (auto error = validateInputs(physical_device_generation, logical_device_generation, owned_description))
    {
        return *error;
    }
    if (image_format_result != VK_SUCCESS)
    {
        return failure(VulkanTextureUploadDestinationResolutionCode::ImageFormatQueryFailure,
                       VulkanTextureUploadDestinationCommand::GetPhysicalDeviceImageFormatProperties,
                       image_format_result);
    }
    if (auto error = imageFormatError(image_format_properties, owned_description))
    {
        return *error;
    }

    VkPhysicalDeviceMemoryProperties memory_properties{};
    dispatch.mGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);
    if (auto error = validateInputs(physical_device_generation, logical_device_generation, owned_description))
    {
        return *error;
    }
    if (!validMemoryProperties(memory_properties))
    {
        return failure(VulkanTextureUploadDestinationResolutionCode::InvalidPhysicalDeviceMemoryProperties,
                       VulkanTextureUploadDestinationCommand::GetPhysicalDeviceMemoryProperties);
    }

    const VkImageCreateInfo create_info   = imageCreateInfo(owned_description);
    VkImage                 image         = VK_NULL_HANDLE;
    const VkResult          create_result = dispatch.mCreateImage(device, &create_info, nullptr, &image);
    if (auto error = validateInputs(physical_device_generation, logical_device_generation, owned_description))
    {
        if (create_result == VK_SUCCESS && image != VK_NULL_HANDLE)
        {
            rollBack(dispatch, device, VK_NULL_HANDLE, false, image, true, VK_NULL_HANDLE, false);
        }
        return *error;
    }
    if (create_result != VK_SUCCESS)
    {
        return failure(VulkanTextureUploadDestinationResolutionCode::ImageCreationFailure,
                       VulkanTextureUploadDestinationCommand::CreateImage,
                       create_result);
    }
    if (image == VK_NULL_HANDLE)
    {
        return failure(VulkanTextureUploadDestinationResolutionCode::NullImageOnSuccess,
                       VulkanTextureUploadDestinationCommand::CreateImage);
    }

    VkMemoryDedicatedRequirements dedicated_requirements{};
    dedicated_requirements.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS;
    VkMemoryRequirements2 requirements2{};
    requirements2.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
    requirements2.pNext = &dedicated_requirements;
    VkImageMemoryRequirementsInfo2 requirements_info{};
    requirements_info.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2;
    requirements_info.image = image;
    dispatch.mGetImageMemoryRequirements2(device, &requirements_info, &requirements2);
    if (auto error = validateInputs(physical_device_generation, logical_device_generation, owned_description))
    {
        rollBack(dispatch, device, VK_NULL_HANDLE, false, image, true, VK_NULL_HANDLE, false);
        return *error;
    }
    if (!validMemoryRequirements(requirements2.memoryRequirements))
    {
        rollBack(dispatch, device, VK_NULL_HANDLE, false, image, true, VK_NULL_HANDLE, false);
        return failure(VulkanTextureUploadDestinationResolutionCode::InvalidImageMemoryRequirements,
                       VulkanTextureUploadDestinationCommand::GetImageMemoryRequirements2);
    }

    const std::optional<std::uint32_t> memory_type_index = selectMemoryType(memory_properties, requirements2.memoryRequirements);
    if (!memory_type_index)
    {
        rollBack(dispatch, device, VK_NULL_HANDLE, false, image, true, VK_NULL_HANDLE, false);
        return failure(VulkanTextureUploadDestinationResolutionCode::NoCompatibleMemoryType);
    }

    VkMemoryDedicatedAllocateInfo dedicated_allocate_info{};
    dedicated_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
    dedicated_allocate_info.image = image;
    VkMemoryAllocateInfo allocate_info{};
    allocate_info.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate_info.pNext           = &dedicated_allocate_info;
    allocate_info.allocationSize  = requirements2.memoryRequirements.size;
    allocate_info.memoryTypeIndex = *memory_type_index;

    VkDeviceMemory memory          = VK_NULL_HANDLE;
    const VkResult allocate_result = dispatch.mAllocateMemory(device, &allocate_info, nullptr, &memory);
    if (auto error = validateInputs(physical_device_generation, logical_device_generation, owned_description))
    {
        if (allocate_result == VK_SUCCESS)
        {
            rollBack(dispatch, device, VK_NULL_HANDLE, false, image, true, memory, memory != VK_NULL_HANDLE);
        }
        else
        {
            rollBack(dispatch, device, VK_NULL_HANDLE, false, image, true, VK_NULL_HANDLE, false);
        }
        return *error;
    }
    if (allocate_result != VK_SUCCESS)
    {
        rollBack(dispatch, device, VK_NULL_HANDLE, false, image, true, VK_NULL_HANDLE, false);
        return failure(VulkanTextureUploadDestinationResolutionCode::MemoryAllocationFailure,
                       VulkanTextureUploadDestinationCommand::AllocateMemory,
                       allocate_result);
    }
    if (memory == VK_NULL_HANDLE)
    {
        rollBack(dispatch, device, VK_NULL_HANDLE, false, image, true, VK_NULL_HANDLE, false);
        return failure(VulkanTextureUploadDestinationResolutionCode::NullMemoryOnSuccess,
                       VulkanTextureUploadDestinationCommand::AllocateMemory);
    }

    const VkResult bind_result = dispatch.mBindImageMemory(device, image, memory, 0);
    if (auto error = validateInputs(physical_device_generation, logical_device_generation, owned_description))
    {
        rollBack(dispatch, device, VK_NULL_HANDLE, false, image, true, memory, true);
        return *error;
    }
    if (bind_result != VK_SUCCESS)
    {
        rollBack(dispatch, device, VK_NULL_HANDLE, false, image, true, memory, true);
        return failure(VulkanTextureUploadDestinationResolutionCode::ImageMemoryBindFailure,
                       VulkanTextureUploadDestinationCommand::BindImageMemory,
                       bind_result);
    }

    const VkImageViewCreateInfo view_info   = imageViewCreateInfo(image, owned_description);
    VkImageView                 image_view  = VK_NULL_HANDLE;
    const VkResult              view_result = dispatch.mCreateImageView(device, &view_info, nullptr, &image_view);
    if (auto error = validateInputs(physical_device_generation, logical_device_generation, owned_description))
    {
        if (view_result == VK_SUCCESS)
        {
            rollBack(dispatch, device, image_view, image_view != VK_NULL_HANDLE, image, true, memory, true);
        }
        else
        {
            rollBack(dispatch, device, VK_NULL_HANDLE, false, image, true, memory, true);
        }
        return *error;
    }
    if (view_result != VK_SUCCESS)
    {
        rollBack(dispatch, device, VK_NULL_HANDLE, false, image, true, memory, true);
        return failure(VulkanTextureUploadDestinationResolutionCode::ImageViewCreationFailure,
                       VulkanTextureUploadDestinationCommand::CreateImageView,
                       view_result);
    }
    if (image_view == VK_NULL_HANDLE)
    {
        rollBack(dispatch, device, VK_NULL_HANDLE, false, image, true, memory, true);
        return failure(VulkanTextureUploadDestinationResolutionCode::NullImageViewOnSuccess,
                       VulkanTextureUploadDestinationCommand::CreateImageView);
    }

    return VulkanTextureUploadDestinationGenerationFactory::create(physical_device_generation,
                                                                   logical_device_generation,
                                                                   owned_description,
                                                                   format_properties.optimalTilingFeatures,
                                                                   image_format_properties,
                                                                   image,
                                                                   memory,
                                                                   requirements2.memoryRequirements,
                                                                   *memory_type_index,
                                                                   memory_properties.memoryTypes[*memory_type_index].propertyFlags,
                                                                   dedicated_requirements.prefersDedicatedAllocation != VK_FALSE,
                                                                   dedicated_requirements.requiresDedicatedAllocation != VK_FALSE,
                                                                   image_view,
                                                                   dispatch.mDestroyImageView,
                                                                   dispatch.mDestroyImage,
                                                                   dispatch.mFreeMemory);
}

} // namespace LLRenderVulkan
