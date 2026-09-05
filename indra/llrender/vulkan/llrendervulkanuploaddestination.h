/**
 * @file llrendervulkanuploaddestination.h
 * @brief Loader-neutral ownership of one Vulkan upload destination.
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

#ifndef LL_LLRENDERVULKANUPLOADDESTINATION_H
#define LL_LLRENDERVULKANUPLOADDESTINATION_H

#include "llrendervulkanuploadsource.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>
#include <variant>

namespace LLRenderVulkan
{

enum class VulkanUploadDestinationCommand : std::uint8_t
{
    GetPhysicalDeviceMemoryProperties,
    GetDeviceProcAddr,
    CreateBuffer,
    DestroyBuffer,
    GetBufferMemoryRequirements,
    AllocateMemory,
    FreeMemory,
    BindBufferMemory
};

enum class VulkanUploadDestinationResolutionCode : std::uint8_t
{
    InvalidPhysicalDeviceGeneration,
    InvalidLogicalDeviceGeneration,
    InvalidDescription,
    InvalidUploadSourceGeneration,
    UploadSourceDescriptionMismatch,
    MissingRequiredCommand,
    InvalidPhysicalDeviceMemoryProperties,
    BufferCreationFailure,
    NullBufferOnSuccess,
    SourceDestinationBufferAlias,
    InvalidBufferMemoryRequirements,
    NoCompatibleMemoryType,
    MemoryAllocationFailure,
    NullMemoryOnSuccess,
    SourceDestinationMemoryAlias,
    BufferMemoryBindFailure
};

struct VulkanUploadDestinationResolutionError
{
    VulkanUploadDestinationResolutionCode         mCode = VulkanUploadDestinationResolutionCode::InvalidPhysicalDeviceGeneration;
    std::optional<VulkanUploadDestinationCommand> mCommand;
    VkResult                                      mResult = VK_SUCCESS;

    friend constexpr bool operator==(const VulkanUploadDestinationResolutionError&,
                                     const VulkanUploadDestinationResolutionError&) = default;
};

class VulkanUploadTransferGeneration;

// This generation owns one transfer-destination/vertex buffer and one
// dedicated device-local allocation. It retains the expected immutable source
// identity, but does not retain or own the upload-source generation itself.
// The exact physical and logical device generations must outlive it. Reset is
// externally synchronized.
class VulkanUploadDestinationGeneration
{
public:
    ~VulkanUploadDestinationGeneration() noexcept;

    VulkanUploadDestinationGeneration(const VulkanUploadDestinationGeneration&)            = delete;
    VulkanUploadDestinationGeneration& operator=(const VulkanUploadDestinationGeneration&) = delete;
    VulkanUploadDestinationGeneration(VulkanUploadDestinationGeneration&& other) noexcept;
    VulkanUploadDestinationGeneration& operator=(VulkanUploadDestinationGeneration&&) = delete;

    LLRenderContract::BufferHandle resourceHandle() const noexcept { return mDescription.mHandle; }
    std::uint64_t                  expectedContentIdentity() const noexcept { return mExpectedContentIdentity; }
    VkDeviceSize                   byteCount() const noexcept { return mByteCount; }
    VkBufferUsageFlags             usage() const noexcept { return mUsage; }
    VkBuffer                       buffer() const noexcept { return mBuffer; }
    VkDeviceMemory                 memory() const noexcept { return mMemory; }
    VkDeviceSize                   allocationSize() const noexcept { return mAllocationSize; }
    std::uint32_t                  memoryTypeIndex() const noexcept { return mMemoryTypeIndex; }
    VkMemoryPropertyFlags          memoryPropertyFlags() const noexcept { return mMemoryPropertyFlags; }
    bool isDeviceLocal() const noexcept { return (mMemoryPropertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0; }
    bool isMapped() const noexcept { return false; }

    bool createdFor(const VulkanPhysicalDeviceGeneration& physical_device_generation,
                    const VulkanLogicalDeviceGeneration&  logical_device_generation) const noexcept;
    bool matchesDescription(const VulkanUploadSourceDescription& description) const noexcept;
    bool matchesUploadSource(const VulkanUploadSourceGeneration& upload_source_generation) const noexcept;

    bool isResident() const noexcept { return mResidentContentIdentity != 0 && mResidentContentIdentity == mExpectedContentIdentity; }
    std::uint64_t residentContentIdentity() const noexcept { return mResidentContentIdentity; }

    void reset() noexcept;

private:
    friend struct VulkanUploadDestinationGenerationFactory;
    friend struct VulkanUploadDestinationGenerationTestAccess;
    friend class VulkanUploadTransferGeneration;

    VulkanUploadDestinationGeneration(const VulkanPhysicalDeviceGeneration& physical_device_generation,
                                      const VulkanLogicalDeviceGeneration&  logical_device_generation,
                                      const VulkanUploadSourceDescription&  description,
                                      std::uint64_t                         expected_content_identity,
                                      VkBuffer                              buffer,
                                      VkDeviceMemory                        memory,
                                      VkDeviceSize                          allocation_size,
                                      std::uint32_t                         memory_type_index,
                                      VkMemoryPropertyFlags                 memory_property_flags,
                                      PFN_vkDestroyBuffer                   destroy_buffer,
                                      PFN_vkFreeMemory                      free_memory) noexcept;

    bool markResident(std::uint64_t content_identity) noexcept;

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
    std::uint64_t                         mExpectedContentIdentity = 0;
    std::uint64_t                         mResidentContentIdentity = 0;
    VkBuffer                              mBuffer                  = VK_NULL_HANDLE;
    VkDeviceMemory                        mMemory                  = VK_NULL_HANDLE;
    VkDeviceSize                          mByteCount               = 0;
    VkBufferUsageFlags                    mUsage                   = 0;
    VkDeviceSize                          mAllocationSize          = 0;
    std::uint32_t                         mMemoryTypeIndex         = 0;
    VkMemoryPropertyFlags                 mMemoryPropertyFlags     = 0;
    PFN_vkDestroyBuffer                   mDestroyBuffer           = nullptr;
    PFN_vkFreeMemory                      mFreeMemory              = nullptr;
};

using VulkanUploadDestinationResolutionResult = std::variant<VulkanUploadDestinationResolutionError, VulkanUploadDestinationGeneration>;

VulkanUploadDestinationResolutionResult resolveVulkanUploadDestinationGeneration(
    const VulkanPhysicalDeviceGeneration& physical_device_generation,
    const VulkanLogicalDeviceGeneration&  logical_device_generation,
    const VulkanUploadSourceGeneration&   upload_source_generation,
    const VulkanUploadSourceDescription&  description) noexcept;

} // namespace LLRenderVulkan

#endif // LL_LLRENDERVULKANUPLOADDESTINATION_H
