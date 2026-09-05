/**
 * @file llrendervulkantextureuploadsource.h
 * @brief Loader-neutral ownership of one immutable Vulkan texture upload source.
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

#ifndef LL_LLRENDERVULKANTEXTUREUPLOADSOURCE_H
#define LL_LLRENDERVULKANTEXTUREUPLOADSOURCE_H

#include "llrendervulkanlogicaldevice.h"
#include "lltextureuploadcontract.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>

namespace LLRenderVulkan
{

inline constexpr std::size_t VULKAN_TEXTURE_UPLOAD_SOURCE_BYTE_COUNT = LLRenderContract::TEXTURE_UPLOAD_SOURCE_BYTE_COUNT;
static_assert(VULKAN_TEXTURE_UPLOAD_SOURCE_BYTE_COUNT == 144);

using VulkanTextureUploadSourceBytes = std::array<std::uint8_t, VULKAN_TEXTURE_UPLOAD_SOURCE_BYTE_COUNT>;

struct VulkanTextureUploadSourceDescription
{
    LLRenderContract::ImageHandle  mHandle;
    std::uint64_t                  mExpectedRevision = 0;
    VulkanTextureUploadSourceBytes mBytes{};

    friend constexpr bool operator==(const VulkanTextureUploadSourceDescription&, const VulkanTextureUploadSourceDescription&) = default;
};

inline constexpr VulkanTextureUploadSourceDescription vulkanTextureUploadSourceDescription(
    const VulkanTextureUploadSourceBytes& bytes) noexcept
{
    return { LLRenderContract::StreamingUploadHandles{}.mReplacementImage, LLRenderContract::TEXTURE_UPLOAD_REVISION, bytes };
}

enum class VulkanTextureUploadSourceCommand : std::uint8_t
{
    GetPhysicalDeviceMemoryProperties,
    GetDeviceProcAddr,
    CreateBuffer,
    DestroyBuffer,
    GetBufferMemoryRequirements,
    AllocateMemory,
    FreeMemory,
    BindBufferMemory,
    MapMemory,
    UnmapMemory,
    FlushMappedMemoryRanges
};

enum class VulkanTextureUploadSourceResolutionCode : std::uint8_t
{
    InvalidPhysicalDeviceGeneration,
    InvalidLogicalDeviceGeneration,
    InvalidDescription,
    MissingRequiredCommand,
    InvalidPhysicalDeviceMemoryProperties,
    BufferCreationFailure,
    NullBufferOnSuccess,
    InvalidBufferMemoryRequirements,
    NoCompatibleMemoryType,
    MemoryAllocationFailure,
    NullMemoryOnSuccess,
    BufferMemoryBindFailure,
    MemoryMapFailure,
    NullMappedDataOnSuccess,
    MemoryFlushFailure
};

struct VulkanTextureUploadSourceResolutionError
{
    VulkanTextureUploadSourceResolutionCode         mCode = VulkanTextureUploadSourceResolutionCode::InvalidPhysicalDeviceGeneration;
    std::optional<VulkanTextureUploadSourceCommand> mCommand;
    VkResult                                        mResult = VK_SUCCESS;

    friend constexpr bool operator==(const VulkanTextureUploadSourceResolutionError&,
                                     const VulkanTextureUploadSourceResolutionError&) = default;
};

// This generation owns one immutable transfer-source buffer and one separate
// host-visible allocation. Resolution maps the whole allocation, copies all
// 144 bytes, flushes noncoherent memory, and unmaps before publication. It
// records no command and owns no image state. The exact physical and logical
// device generations must outlive it. Reset is externally synchronized.
class VulkanTextureUploadSourceGeneration
{
public:
    ~VulkanTextureUploadSourceGeneration() noexcept;

    VulkanTextureUploadSourceGeneration(const VulkanTextureUploadSourceGeneration&)            = delete;
    VulkanTextureUploadSourceGeneration& operator=(const VulkanTextureUploadSourceGeneration&) = delete;
    VulkanTextureUploadSourceGeneration(VulkanTextureUploadSourceGeneration&& other) noexcept;
    VulkanTextureUploadSourceGeneration& operator=(VulkanTextureUploadSourceGeneration&&) = delete;

    LLRenderContract::ImageHandle resourceHandle() const noexcept { return mDescription.mHandle; }
    std::uint64_t                 expectedRevision() const noexcept { return mDescription.mExpectedRevision; }
    LLRenderContract::Extent2D    residentExtent() const noexcept;
    LLRenderContract::PixelFormat pixelFormat() const noexcept;
    std::uint32_t                 rowPitch() const noexcept;
    LLRenderContract::RowOrigin   rowOrigin() const noexcept;
    std::uint64_t                 contentIdentity() const noexcept { return mContentIdentity; }
    bool                          matchesDescription(const VulkanTextureUploadSourceDescription& description) const noexcept;

    VkBufferCreateFlags flags() const noexcept { return 0; }
    VkBufferUsageFlags  usage() const noexcept { return mBuffer != VK_NULL_HANDLE ? VK_BUFFER_USAGE_TRANSFER_SRC_BIT : 0; }
    VkSharingMode  sharingMode() const noexcept { return mBuffer != VK_NULL_HANDLE ? VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_MAX_ENUM; }
    VkBuffer       buffer() const noexcept { return mBuffer; }
    VkDeviceMemory memory() const noexcept { return mMemory; }
    VkDeviceSize   byteCount() const noexcept { return mByteCount; }
    VkDeviceSize   allocationSize() const noexcept { return mAllocationSize; }
    std::uint32_t  memoryTypeIndex() const noexcept { return mMemoryTypeIndex; }
    VkMemoryPropertyFlags memoryPropertyFlags() const noexcept { return mMemoryPropertyFlags; }
    bool                  isCoherent() const noexcept;

    bool createdFor(const VulkanPhysicalDeviceGeneration& physical_device_generation,
                    const VulkanLogicalDeviceGeneration&  logical_device_generation) const noexcept;

    void reset() noexcept;

private:
    friend struct VulkanTextureUploadSourceGenerationFactory;
    friend struct VulkanTextureUploadSourceGenerationTestAccess;

    VulkanTextureUploadSourceGeneration(const VulkanPhysicalDeviceGeneration&       physical_device_generation,
                                        const VulkanLogicalDeviceGeneration&        logical_device_generation,
                                        const VulkanTextureUploadSourceDescription& description,
                                        VkBuffer                                    buffer,
                                        VkDeviceMemory                              memory,
                                        VkDeviceSize                                allocation_size,
                                        std::uint32_t                               memory_type_index,
                                        VkMemoryPropertyFlags                       memory_property_flags,
                                        PFN_vkDestroyBuffer                         destroy_buffer,
                                        PFN_vkFreeMemory                            free_memory) noexcept;

    const VulkanPhysicalDeviceGeneration* mPhysicalDeviceGeneration = nullptr;
    const VulkanLogicalDeviceGeneration*  mLogicalDeviceGeneration  = nullptr;
    PFN_vkGetInstanceProcAddr             mGetInstanceProcAddr      = nullptr;
    VkInstance                            mInstance                 = VK_NULL_HANDLE;
    VkSurfaceKHR                          mSurface                  = VK_NULL_HANDLE;
    VkPhysicalDevice                      mPhysicalDevice           = VK_NULL_HANDLE;
    std::uint32_t                         mPhysicalDeviceIndex      = 0;
    VkDevice                              mDevice                   = VK_NULL_HANDLE;
    VkQueue                               mQueue                    = VK_NULL_HANDLE;
    std::uint32_t                         mQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
    std::uint32_t                         mQueueIndex               = 0;
    VulkanTextureUploadSourceDescription  mDescription;
    std::uint64_t                         mContentIdentity     = 0;
    VkBuffer                              mBuffer              = VK_NULL_HANDLE;
    VkDeviceMemory                        mMemory              = VK_NULL_HANDLE;
    VkDeviceSize                          mByteCount           = 0;
    VkDeviceSize                          mAllocationSize      = 0;
    std::uint32_t                         mMemoryTypeIndex     = 0;
    VkMemoryPropertyFlags                 mMemoryPropertyFlags = 0;
    PFN_vkDestroyBuffer                   mDestroyBuffer       = nullptr;
    PFN_vkFreeMemory                      mFreeMemory          = nullptr;
};

using VulkanTextureUploadSourceResolutionResult =
    std::variant<VulkanTextureUploadSourceResolutionError, VulkanTextureUploadSourceGeneration>;

VulkanTextureUploadSourceResolutionResult resolveVulkanTextureUploadSourceGeneration(
    const VulkanPhysicalDeviceGeneration&       physical_device_generation,
    const VulkanLogicalDeviceGeneration&        logical_device_generation,
    const VulkanTextureUploadSourceDescription& description) noexcept;

} // namespace LLRenderVulkan

#endif // LL_LLRENDERVULKANTEXTUREUPLOADSOURCE_H
