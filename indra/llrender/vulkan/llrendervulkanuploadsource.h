/**
 * @file llrendervulkanuploadsource.h
 * @brief Loader-neutral ownership of one immutable Vulkan upload source.
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

#ifndef LL_LLRENDERVULKANUPLOADSOURCE_H
#define LL_LLRENDERVULKANUPLOADSOURCE_H

#include "lltextureuploadcontract.h"
#include "llrendervulkanlogicaldevice.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>

namespace LLRenderVulkan
{

inline constexpr std::size_t VULKAN_UPLOAD_SOURCE_BYTE_COUNT = LLRenderContract::SCREEN_TRIANGLE_BYTES.size();

using VulkanUploadSourceBytes = std::array<std::uint8_t, VULKAN_UPLOAD_SOURCE_BYTE_COUNT>;

struct VulkanUploadSourceDescription
{
    LLRenderContract::BufferHandle mHandle;
    VulkanUploadSourceBytes        mBytes{};

    friend constexpr bool operator==(const VulkanUploadSourceDescription&, const VulkanUploadSourceDescription&) = default;
};

inline constexpr VulkanUploadSourceDescription vulkanScreenTriangleUploadSourceDescription() noexcept
{
    return { LLRenderContract::StreamingUploadHandles{}.mScreenTriangle, LLRenderContract::SCREEN_TRIANGLE_BYTES };
}

enum class VulkanUploadSourceCommand : std::uint8_t
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

enum class VulkanUploadSourceResolutionCode : std::uint8_t
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

struct VulkanUploadSourceResolutionError
{
    VulkanUploadSourceResolutionCode         mCode = VulkanUploadSourceResolutionCode::InvalidPhysicalDeviceGeneration;
    std::optional<VulkanUploadSourceCommand> mCommand;
    VkResult                                 mResult = VK_SUCCESS;

    friend constexpr bool operator==(const VulkanUploadSourceResolutionError&, const VulkanUploadSourceResolutionError&) = default;
};

// This generation owns one immutable transfer-source buffer and one dedicated
// allocation. Resolution maps the whole allocation, copies exactly 48 bytes,
// flushes a noncoherent allocation, and unmaps before publication. The exact
// physical and logical device generations must outlive it. Host access and
// reset are externally synchronized.
class VulkanUploadSourceGeneration
{
public:
    ~VulkanUploadSourceGeneration() noexcept;

    VulkanUploadSourceGeneration(const VulkanUploadSourceGeneration&)            = delete;
    VulkanUploadSourceGeneration& operator=(const VulkanUploadSourceGeneration&) = delete;
    VulkanUploadSourceGeneration(VulkanUploadSourceGeneration&& other) noexcept;
    VulkanUploadSourceGeneration& operator=(VulkanUploadSourceGeneration&&) = delete;

    LLRenderContract::BufferHandle resourceHandle() const noexcept { return mDescription.mHandle; }
    // Stable nonzero FNV-1a identity of the exact 48 source bytes. An inert
    // generation reports zero; matchesDescription() still compares every byte.
    std::uint64_t         contentIdentity() const noexcept { return mContentIdentity; }
    bool                  matchesDescription(const VulkanUploadSourceDescription& description) const noexcept;
    VkBuffer              buffer() const noexcept { return mBuffer; }
    VkDeviceMemory        memory() const noexcept { return mMemory; }
    VkDeviceSize          byteCount() const noexcept { return mByteCount; }
    VkDeviceSize          allocationSize() const noexcept { return mAllocationSize; }
    std::uint32_t         memoryTypeIndex() const noexcept { return mMemoryTypeIndex; }
    VkMemoryPropertyFlags memoryPropertyFlags() const noexcept { return mMemoryPropertyFlags; }
    bool                  isCoherent() const noexcept { return (mMemoryPropertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0; }

    bool createdFor(const VulkanPhysicalDeviceGeneration& physical_device_generation,
                    const VulkanLogicalDeviceGeneration&  logical_device_generation) const noexcept;

    void reset() noexcept;

private:
    friend struct VulkanUploadSourceGenerationFactory;

    VulkanUploadSourceGeneration(const VulkanPhysicalDeviceGeneration& physical_device_generation,
                                 const VulkanLogicalDeviceGeneration&  logical_device_generation,
                                 const VulkanUploadSourceDescription&  description,
                                 VkBuffer                              buffer,
                                 VkDeviceMemory                        memory,
                                 VkDeviceSize                          allocation_size,
                                 std::uint32_t                         memory_type_index,
                                 VkMemoryPropertyFlags                 memory_property_flags,
                                 PFN_vkDestroyBuffer                   destroy_buffer,
                                 PFN_vkFreeMemory                      free_memory) noexcept;

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
    VulkanUploadSourceDescription         mDescription;
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

using VulkanUploadSourceResolutionResult = std::variant<VulkanUploadSourceResolutionError, VulkanUploadSourceGeneration>;

VulkanUploadSourceResolutionResult resolveVulkanUploadSourceGeneration(const VulkanPhysicalDeviceGeneration& physical_device_generation,
                                                                       const VulkanLogicalDeviceGeneration&  logical_device_generation,
                                                                       const VulkanUploadSourceDescription&  description) noexcept;

} // namespace LLRenderVulkan

#endif // LL_LLRENDERVULKANUPLOADSOURCE_H
