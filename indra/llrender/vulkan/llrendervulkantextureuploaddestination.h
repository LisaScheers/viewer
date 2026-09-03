/**
 * @file llrendervulkantextureuploaddestination.h
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

#ifndef LL_LLRENDERVULKANTEXTUREUPLOADDESTINATION_H
#define LL_LLRENDERVULKANTEXTUREUPLOADDESTINATION_H

#include "llrendervulkanlogicaldevice.h"
#include "lltextureuploadcontract.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>
#include <variant>

namespace LLRenderVulkan
{

struct VulkanTextureUploadDestinationDescription
{
    LLRenderContract::ImageHandle mHandle;
    std::uint64_t                 mExpectedRevision = 0;
    LLRenderContract::Extent2D    mResidentExtent;
    LLRenderContract::Extent2D    mLogicalExtent;
    std::uint32_t                 mResidentDiscard = 0;
    LLRenderContract::PixelFormat mFormat          = LLRenderContract::PixelFormat::RGBA8Unorm;
    std::uint32_t                 mMipLevels       = 0;
    std::uint32_t                 mArrayLayers     = 0;
    std::uint32_t                 mSamples         = 0;
    LLRenderContract::ImageState  mInitialState    = LLRenderContract::ImageState::Undefined;

    friend constexpr bool operator==(const VulkanTextureUploadDestinationDescription& left,
                                     const VulkanTextureUploadDestinationDescription& right) noexcept
    {
        return left.mHandle == right.mHandle && left.mExpectedRevision == right.mExpectedRevision &&
               left.mResidentExtent.mWidth == right.mResidentExtent.mWidth &&
               left.mResidentExtent.mHeight == right.mResidentExtent.mHeight && left.mLogicalExtent.mWidth == right.mLogicalExtent.mWidth &&
               left.mLogicalExtent.mHeight == right.mLogicalExtent.mHeight && left.mResidentDiscard == right.mResidentDiscard &&
               left.mFormat == right.mFormat && left.mMipLevels == right.mMipLevels && left.mArrayLayers == right.mArrayLayers &&
               left.mSamples == right.mSamples && left.mInitialState == right.mInitialState;
    }
};

inline constexpr VulkanTextureUploadDestinationDescription vulkanTextureUploadDestinationDescription() noexcept
{
    return { LLRenderContract::StreamingUploadHandles{}.mReplacementImage,
             LLRenderContract::TEXTURE_UPLOAD_REVISION,
             { LLRenderContract::TEXTURE_UPLOAD_RESIDENT_WIDTH, LLRenderContract::TEXTURE_UPLOAD_RESIDENT_HEIGHT },
             { LLRenderContract::TEXTURE_UPLOAD_LOGICAL_WIDTH, LLRenderContract::TEXTURE_UPLOAD_LOGICAL_HEIGHT },
             LLRenderContract::TEXTURE_UPLOAD_RESIDENT_DISCARD,
             LLRenderContract::PixelFormat::RGBA8Unorm,
             LLRenderContract::TEXTURE_UPLOAD_MIP_LEVELS,
             1,
             1,
             LLRenderContract::ImageState::Undefined };
}

enum class VulkanTextureUploadDestinationCommand : std::uint8_t
{
    GetPhysicalDeviceFormatProperties,
    GetPhysicalDeviceImageFormatProperties,
    GetPhysicalDeviceMemoryProperties,
    GetDeviceProcAddr,
    CreateImage,
    DestroyImage,
    GetImageMemoryRequirements2,
    AllocateMemory,
    FreeMemory,
    BindImageMemory,
    CreateImageView,
    DestroyImageView
};

enum class VulkanTextureUploadDestinationResolutionCode : std::uint8_t
{
    InvalidPhysicalDeviceGeneration,
    InvalidLogicalDeviceGeneration,
    InvalidDescription,
    MissingRequiredCommand,
    UnsupportedFormatFeatures,
    ImageFormatQueryFailure,
    UnsupportedImageFormatLimits,
    InvalidPhysicalDeviceMemoryProperties,
    ImageCreationFailure,
    NullImageOnSuccess,
    InvalidImageMemoryRequirements,
    NoCompatibleMemoryType,
    MemoryAllocationFailure,
    NullMemoryOnSuccess,
    ImageMemoryBindFailure,
    ImageViewCreationFailure,
    NullImageViewOnSuccess
};

enum class VulkanTextureUploadDestinationCapability : std::uint8_t
{
    FormatFeatures,
    ExtentWidth,
    ExtentHeight,
    ExtentDepth,
    MipLevels,
    ArrayLayers,
    SampleCountOne
};

struct VulkanTextureUploadDestinationResolutionError
{
    VulkanTextureUploadDestinationResolutionCode mCode = VulkanTextureUploadDestinationResolutionCode::InvalidPhysicalDeviceGeneration;
    std::optional<VulkanTextureUploadDestinationCommand>    mCommand;
    std::optional<VulkanTextureUploadDestinationCapability> mCapability;
    VkResult                                                mResult         = VK_SUCCESS;
    std::uint64_t                                           mRequiredValue  = 0;
    std::uint64_t                                           mAvailableValue = 0;

    friend constexpr bool operator==(const VulkanTextureUploadDestinationResolutionError&,
                                     const VulkanTextureUploadDestinationResolutionError&) = default;
};

// This generation owns one optimal-tiled image, one formally dedicated
// device-local allocation, and one view over its declared color mips. It owns
// no pixel contents and publishes no current-layout or residency state. The
// exact physical and logical device generations must outlive it. Reset is
// externally synchronized.
class VulkanTextureUploadDestinationGeneration
{
public:
    ~VulkanTextureUploadDestinationGeneration() noexcept;

    VulkanTextureUploadDestinationGeneration(const VulkanTextureUploadDestinationGeneration&)            = delete;
    VulkanTextureUploadDestinationGeneration& operator=(const VulkanTextureUploadDestinationGeneration&) = delete;
    VulkanTextureUploadDestinationGeneration(VulkanTextureUploadDestinationGeneration&& other) noexcept;
    VulkanTextureUploadDestinationGeneration& operator=(VulkanTextureUploadDestinationGeneration&&) = delete;

    LLRenderContract::ImageHandle resourceHandle() const noexcept { return mDescription.mHandle; }
    std::uint64_t                 expectedRevision() const noexcept { return mDescription.mExpectedRevision; }
    VkExtent3D                    residentExtent() const noexcept;
    LLRenderContract::Extent2D    logicalExtent() const noexcept { return mDescription.mLogicalExtent; }
    std::uint32_t                 residentDiscard() const noexcept { return mDescription.mResidentDiscard; }
    LLRenderContract::PixelFormat pixelFormat() const noexcept { return mDescription.mFormat; }
    LLRenderContract::ImageState  initialState() const noexcept { return mDescription.mInitialState; }
    VkImageCreateFlags            flags() const noexcept { return 0; }
    VkImageType                   imageType() const noexcept { return mDescription.mHandle ? VK_IMAGE_TYPE_2D : VK_IMAGE_TYPE_MAX_ENUM; }
    VkFormat                      format() const noexcept { return mDescription.mHandle ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_UNDEFINED; }
    std::uint32_t                 mipLevels() const noexcept { return mDescription.mMipLevels; }
    std::uint32_t                 arrayLayers() const noexcept { return mDescription.mArrayLayers; }
    VkSampleCountFlagBits         samples() const noexcept
    {
        return mDescription.mHandle ? VK_SAMPLE_COUNT_1_BIT : VK_SAMPLE_COUNT_FLAG_BITS_MAX_ENUM;
    }
    VkImageTiling     tiling() const noexcept { return mDescription.mHandle ? VK_IMAGE_TILING_OPTIMAL : VK_IMAGE_TILING_MAX_ENUM; }
    VkImageUsageFlags usage() const noexcept
    {
        return mDescription.mHandle ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT : 0;
    }
    VkSharingMode sharingMode() const noexcept { return mDescription.mHandle ? VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_MAX_ENUM; }
    VkImageLayout initialLayout() const noexcept { return mDescription.mHandle ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_MAX_ENUM; }
    VkFormatFeatureFlags           formatFeatures() const noexcept { return mFormatFeatures; }
    const VkImageFormatProperties& imageFormatProperties() const noexcept { return mImageFormatProperties; }
    VkImage                        image() const noexcept { return mImage; }
    VkDeviceMemory                 memory() const noexcept { return mMemory; }
    const VkMemoryRequirements&    memoryRequirements() const noexcept { return mMemoryRequirements; }
    VkDeviceSize                   allocationSize() const noexcept { return mMemoryRequirements.size; }
    VkDeviceSize                   allocationAlignment() const noexcept { return mMemoryRequirements.alignment; }
    std::uint32_t                  compatibleMemoryTypeBits() const noexcept { return mMemoryRequirements.memoryTypeBits; }
    std::uint32_t                  memoryTypeIndex() const noexcept { return mMemoryTypeIndex; }
    VkMemoryPropertyFlags          memoryPropertyFlags() const noexcept { return mMemoryPropertyFlags; }
    bool                           isDeviceLocal() const noexcept;
    bool                           prefersDedicatedAllocation() const noexcept { return mPrefersDedicatedAllocation; }
    bool                           requiresDedicatedAllocation() const noexcept { return mRequiresDedicatedAllocation; }
    VkImageView                    imageView() const noexcept { return mImageView; }
    VkImageViewType                imageViewType() const noexcept
    {
        return mImageView != VK_NULL_HANDLE ? VK_IMAGE_VIEW_TYPE_2D : VK_IMAGE_VIEW_TYPE_MAX_ENUM;
    }
    VkImageSubresourceRange viewRange() const noexcept;

    bool createdFor(const VulkanPhysicalDeviceGeneration& physical_device_generation,
                    const VulkanLogicalDeviceGeneration&  logical_device_generation) const noexcept;
    bool matchesDescription(const VulkanTextureUploadDestinationDescription& description) const noexcept;

    void reset() noexcept;

private:
    friend struct VulkanTextureUploadDestinationGenerationFactory;

    VulkanTextureUploadDestinationGeneration(const VulkanPhysicalDeviceGeneration&            physical_device_generation,
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
                                             PFN_vkFreeMemory                                 free_memory) noexcept;

    const VulkanPhysicalDeviceGeneration*     mPhysicalDeviceGeneration = nullptr;
    const VulkanLogicalDeviceGeneration*      mLogicalDeviceGeneration  = nullptr;
    PFN_vkGetInstanceProcAddr                 mGetInstanceProcAddr      = nullptr;
    VkInstance                                mInstance                 = VK_NULL_HANDLE;
    VkSurfaceKHR                              mSurface                  = VK_NULL_HANDLE;
    VkPhysicalDevice                          mPhysicalDevice           = VK_NULL_HANDLE;
    std::uint32_t                             mPhysicalDeviceIndex      = 0;
    VkDevice                                  mDevice                   = VK_NULL_HANDLE;
    VkQueue                                   mQueue                    = VK_NULL_HANDLE;
    std::uint32_t                             mQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
    std::uint32_t                             mQueueIndex               = 0;
    VulkanTextureUploadDestinationDescription mDescription;
    VkFormatFeatureFlags                      mFormatFeatures = 0;
    VkImageFormatProperties                   mImageFormatProperties{};
    VkImage                                   mImage  = VK_NULL_HANDLE;
    VkDeviceMemory                            mMemory = VK_NULL_HANDLE;
    VkMemoryRequirements                      mMemoryRequirements{};
    std::uint32_t                             mMemoryTypeIndex             = 0;
    VkMemoryPropertyFlags                     mMemoryPropertyFlags         = 0;
    bool                                      mPrefersDedicatedAllocation  = false;
    bool                                      mRequiresDedicatedAllocation = false;
    VkImageView                               mImageView                   = VK_NULL_HANDLE;
    PFN_vkDestroyImageView                    mDestroyImageView            = nullptr;
    PFN_vkDestroyImage                        mDestroyImage                = nullptr;
    PFN_vkFreeMemory                          mFreeMemory                  = nullptr;
};

using VulkanTextureUploadDestinationResolutionResult =
    std::variant<VulkanTextureUploadDestinationResolutionError, VulkanTextureUploadDestinationGeneration>;

VulkanTextureUploadDestinationResolutionResult resolveVulkanTextureUploadDestinationGeneration(
    const VulkanPhysicalDeviceGeneration&            physical_device_generation,
    const VulkanLogicalDeviceGeneration&             logical_device_generation,
    const VulkanTextureUploadDestinationDescription& description) noexcept;

} // namespace LLRenderVulkan

#endif // LL_LLRENDERVULKANTEXTUREUPLOADDESTINATION_H
