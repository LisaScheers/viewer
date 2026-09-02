/**
 * @file llrendervulkanswapchainreadback.h
 * @brief Loader-neutral Vulkan swapchain readback-buffer ownership.
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

#ifndef LL_LLRENDERVULKANSWAPCHAINREADBACK_H
#define LL_LLRENDERVULKANSWAPCHAINREADBACK_H

#include "llrendervulkanswapchainimages.h"

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>

namespace LLRenderVulkan
{

enum class VulkanSwapchainReadbackCommand : std::uint8_t
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
    UnmapMemory
};

enum class VulkanSwapchainReadbackResolutionCode : std::uint8_t
{
    InvalidPhysicalDeviceGeneration,
    InvalidLogicalDeviceGeneration,
    InvalidSwapchainConfigurationGeneration,
    InvalidSwapchainGeneration,
    InvalidSwapchainImagesGeneration,
    UnsupportedImageFormat,
    RowBytesOverflow,
    ByteCountOverflow,
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
    NullMappedDataOnSuccess
};

struct VulkanSwapchainReadbackResolutionError
{
    VulkanSwapchainReadbackResolutionCode         mCode = VulkanSwapchainReadbackResolutionCode::InvalidPhysicalDeviceGeneration;
    std::optional<VulkanSwapchainReadbackCommand> mCommand;
    VkResult                                      mResult = VK_SUCCESS;

    friend constexpr bool operator==(const VulkanSwapchainReadbackResolutionError&,
                                     const VulkanSwapchainReadbackResolutionError&) = default;
};

// This generation owns one transfer-destination buffer, one separate memory
// allocation, and one whole-allocation persistent mapping. Its exact physical
// device, logical device, configuration, swapchain, and image generations must
// outlive it. Host access and submitted use are externally synchronized.
class VulkanSwapchainReadbackGeneration
{
public:
    ~VulkanSwapchainReadbackGeneration() noexcept;

    VulkanSwapchainReadbackGeneration(const VulkanSwapchainReadbackGeneration&)            = delete;
    VulkanSwapchainReadbackGeneration& operator=(const VulkanSwapchainReadbackGeneration&) = delete;
    VulkanSwapchainReadbackGeneration(VulkanSwapchainReadbackGeneration&& other) noexcept;
    VulkanSwapchainReadbackGeneration& operator=(VulkanSwapchainReadbackGeneration&&) = delete;

    VkBuffer              buffer() const noexcept { return mBuffer; }
    VkDeviceMemory        memory() const noexcept { return mMemory; }
    bool                  isMapped() const noexcept { return mMappedData != nullptr; }
    VkFormat              imageFormat() const noexcept { return mImageFormat; }
    VkExtent2D            imageExtent() const noexcept { return mImageExtent; }
    std::uint32_t         imageCount() const noexcept { return mImageCount; }
    VkDeviceSize          rowBytes() const noexcept { return mRowBytes; }
    VkDeviceSize          byteCount() const noexcept { return mByteCount; }
    VkDeviceSize          allocationSize() const noexcept { return mAllocationSize; }
    std::uint32_t         memoryTypeIndex() const noexcept { return mMemoryTypeIndex; }
    VkMemoryPropertyFlags memoryPropertyFlags() const noexcept { return mMemoryPropertyFlags; }

    bool createdFor(const VulkanPhysicalDeviceGeneration&         physical_device_generation,
                    const VulkanLogicalDeviceGeneration&          logical_device_generation,
                    const VulkanSwapchainConfigurationGeneration& configuration_generation,
                    const VulkanSwapchainGeneration&              swapchain_generation,
                    const VulkanSwapchainImagesGeneration&        images_generation) const noexcept;

    void reset() noexcept;

private:
    friend struct VulkanSwapchainReadbackGenerationFactory;

    VulkanSwapchainReadbackGeneration(const VulkanPhysicalDeviceGeneration&         physical_device_generation,
                                      const VulkanLogicalDeviceGeneration&          logical_device_generation,
                                      const VulkanSwapchainConfigurationGeneration& configuration_generation,
                                      const VulkanSwapchainGeneration&              swapchain_generation,
                                      const VulkanSwapchainImagesGeneration&        images_generation,
                                      VkDeviceSize                                  row_bytes,
                                      VkDeviceSize                                  byte_count,
                                      VkBuffer                                      buffer,
                                      VkDeviceMemory                                memory,
                                      void*                                         mapped_data,
                                      VkDeviceSize                                  allocation_size,
                                      std::uint32_t                                 memory_type_index,
                                      VkMemoryPropertyFlags                         memory_property_flags,
                                      PFN_vkUnmapMemory                             unmap_memory,
                                      PFN_vkDestroyBuffer                           destroy_buffer,
                                      PFN_vkFreeMemory                              free_memory) noexcept;

    const VulkanPhysicalDeviceGeneration*         mPhysicalDeviceGeneration = nullptr;
    const VulkanLogicalDeviceGeneration*          mLogicalDeviceGeneration  = nullptr;
    const VulkanSwapchainConfigurationGeneration* mConfigurationGeneration  = nullptr;
    const VulkanSwapchainGeneration*              mSwapchainGeneration      = nullptr;
    const VulkanSwapchainImagesGeneration*        mImagesGeneration         = nullptr;
    PFN_vkGetInstanceProcAddr                     mGetInstanceProcAddr      = nullptr;
    VkInstance                                    mInstance                 = VK_NULL_HANDLE;
    VkSurfaceKHR                                  mSurface                  = VK_NULL_HANDLE;
    VkPhysicalDevice                              mPhysicalDevice           = VK_NULL_HANDLE;
    std::uint32_t                                 mPhysicalDeviceIndex      = 0;
    VkDevice                                      mDevice                   = VK_NULL_HANDLE;
    VkQueue                                       mQueue                    = VK_NULL_HANDLE;
    std::uint32_t                                 mQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
    std::uint32_t                                 mQueueIndex               = 0;
    VkExtent2D                                    mDrawableExtent{};
    VkSwapchainKHR                                mSwapchain   = VK_NULL_HANDLE;
    VkFormat                                      mImageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D                                    mImageExtent{};
    std::uint32_t                                 mImageCount          = 0;
    VkDeviceSize                                  mRowBytes            = 0;
    VkDeviceSize                                  mByteCount           = 0;
    VkBuffer                                      mBuffer              = VK_NULL_HANDLE;
    VkDeviceMemory                                mMemory              = VK_NULL_HANDLE;
    void*                                         mMappedData          = nullptr;
    VkDeviceSize                                  mAllocationSize      = 0;
    std::uint32_t                                 mMemoryTypeIndex     = 0;
    VkMemoryPropertyFlags                         mMemoryPropertyFlags = 0;
    PFN_vkUnmapMemory                             mUnmapMemory         = nullptr;
    PFN_vkDestroyBuffer                           mDestroyBuffer       = nullptr;
    PFN_vkFreeMemory                              mFreeMemory          = nullptr;
};

using VulkanSwapchainReadbackResolutionResult = std::variant<VulkanSwapchainReadbackResolutionError, VulkanSwapchainReadbackGeneration>;

struct VulkanSwapchainReadbackByteLayout
{
    VkDeviceSize mRowBytes  = 0;
    VkDeviceSize mByteCount = 0;

    friend constexpr bool operator==(const VulkanSwapchainReadbackByteLayout&, const VulkanSwapchainReadbackByteLayout&) = default;
};

using VulkanSwapchainReadbackByteLayoutResult = std::variant<VulkanSwapchainReadbackResolutionError, VulkanSwapchainReadbackByteLayout>;

VulkanSwapchainReadbackResolutionResult resolveVulkanSwapchainReadbackGeneration(
    const VulkanPhysicalDeviceGeneration&         physical_device_generation,
    const VulkanLogicalDeviceGeneration&          logical_device_generation,
    const VulkanSwapchainConfigurationGeneration& configuration_generation,
    const VulkanSwapchainGeneration&              swapchain_generation,
    const VulkanSwapchainImagesGeneration&        images_generation) noexcept;

namespace VulkanSwapchainReadbackDetail
{

    VulkanSwapchainReadbackByteLayoutResult checkedByteLayout(VkDeviceSize width, VkDeviceSize height) noexcept;

} // namespace VulkanSwapchainReadbackDetail

} // namespace LLRenderVulkan

#endif // LL_LLRENDERVULKANSWAPCHAINREADBACK_H
