/**
 * @file llrendervulkanswapchainreadback.cpp
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

#include "llrendervulkanswapchainreadback.h"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace LLRenderVulkan
{

namespace
{
    struct SwapchainReadbackDispatch
    {
        PFN_vkGetPhysicalDeviceMemoryProperties mGetPhysicalDeviceMemoryProperties = nullptr;
        PFN_vkGetDeviceProcAddr                 mGetDeviceProcAddr                 = nullptr;
        PFN_vkCreateBuffer                      mCreateBuffer                      = nullptr;
        PFN_vkDestroyBuffer                     mDestroyBuffer                     = nullptr;
        PFN_vkGetBufferMemoryRequirements       mGetBufferMemoryRequirements       = nullptr;
        PFN_vkAllocateMemory                    mAllocateMemory                    = nullptr;
        PFN_vkFreeMemory                        mFreeMemory                        = nullptr;
        PFN_vkBindBufferMemory                  mBindBufferMemory                  = nullptr;
        PFN_vkMapMemory                         mMapMemory                         = nullptr;
        PFN_vkUnmapMemory                       mUnmapMemory                       = nullptr;
    };

    VulkanSwapchainReadbackResolutionError failure(VulkanSwapchainReadbackResolutionCode         code,
                                                   std::optional<VulkanSwapchainReadbackCommand> command = std::nullopt,
                                                   VkResult                                      result  = VK_SUCCESS) noexcept
    {
        return { code, command, result };
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

    std::optional<VulkanSwapchainReadbackResolutionError> parentError(
        const VulkanPhysicalDeviceGeneration&         physical_device_generation,
        const VulkanLogicalDeviceGeneration&          logical_device_generation,
        const VulkanSwapchainConfigurationGeneration& configuration_generation,
        const VulkanSwapchainGeneration&              swapchain_generation,
        const VulkanSwapchainImagesGeneration&        images_generation) noexcept
    {
        if (physical_device_generation.getInstanceProcAddr() == nullptr || physical_device_generation.instance() == VK_NULL_HANDLE ||
            physical_device_generation.surface() == VK_NULL_HANDLE || physical_device_generation.physicalDevice() == VK_NULL_HANDLE ||
            physical_device_generation.queueFamilyIndex() == VK_QUEUE_FAMILY_IGNORED)
        {
            return failure(VulkanSwapchainReadbackResolutionCode::InvalidPhysicalDeviceGeneration);
        }
        if (!logical_device_generation.createdFor(physical_device_generation))
        {
            return failure(VulkanSwapchainReadbackResolutionCode::InvalidLogicalDeviceGeneration);
        }

        const VkExtent2D drawable_extent = configuration_generation.drawableExtent();
        const VkExtent2D image_extent    = configuration_generation.imageExtent();
        if (drawable_extent.width == 0 || drawable_extent.height == 0 || image_extent.width == 0 || image_extent.height == 0 ||
            configuration_generation.imageCount() == 0 ||
            !configuration_generation.createdFor(physical_device_generation, logical_device_generation, drawable_extent))
        {
            return failure(VulkanSwapchainReadbackResolutionCode::InvalidSwapchainConfigurationGeneration);
        }
        if (!swapchain_generation.createdFor(logical_device_generation, configuration_generation))
        {
            return failure(VulkanSwapchainReadbackResolutionCode::InvalidSwapchainGeneration);
        }
        if (!images_generation.createdFor(logical_device_generation, configuration_generation, swapchain_generation))
        {
            return failure(VulkanSwapchainReadbackResolutionCode::InvalidSwapchainImagesGeneration);
        }
        return std::nullopt;
    }

    std::optional<VulkanSwapchainReadbackResolutionError> resolveDispatch(const VulkanPhysicalDeviceGeneration& physical_device_generation,
                                                                          const VulkanLogicalDeviceGeneration&  logical_device_generation,
                                                                          SwapchainReadbackDispatch&            dispatch) noexcept
    {
        const PFN_vkGetInstanceProcAddr get_instance_proc_addr = physical_device_generation.getInstanceProcAddr();
        const VkInstance                instance               = physical_device_generation.instance();
        const VkDevice                  device                 = logical_device_generation.device();

        dispatch.mGetPhysicalDeviceMemoryProperties = resolveInstance<PFN_vkGetPhysicalDeviceMemoryProperties>(
            get_instance_proc_addr, instance, "vkGetPhysicalDeviceMemoryProperties");
        if (!dispatch.mGetPhysicalDeviceMemoryProperties)
        {
            return failure(VulkanSwapchainReadbackResolutionCode::MissingRequiredCommand,
                           VulkanSwapchainReadbackCommand::GetPhysicalDeviceMemoryProperties);
        }

        dispatch.mGetDeviceProcAddr = resolveInstance<PFN_vkGetDeviceProcAddr>(get_instance_proc_addr, instance, "vkGetDeviceProcAddr");
        if (!dispatch.mGetDeviceProcAddr)
        {
            return failure(VulkanSwapchainReadbackResolutionCode::MissingRequiredCommand,
                           VulkanSwapchainReadbackCommand::GetDeviceProcAddr);
        }

#define LL_RESOLVE_READBACK_DEVICE_COMMAND(member, type, command_name, command_value)                 \
    dispatch.member = resolveDevice<type>(dispatch.mGetDeviceProcAddr, device, command_name);         \
    if (!dispatch.member)                                                                             \
    {                                                                                                 \
        return failure(VulkanSwapchainReadbackResolutionCode::MissingRequiredCommand, command_value); \
    }

        LL_RESOLVE_READBACK_DEVICE_COMMAND(mCreateBuffer, PFN_vkCreateBuffer, "vkCreateBuffer",
                                           VulkanSwapchainReadbackCommand::CreateBuffer)
        LL_RESOLVE_READBACK_DEVICE_COMMAND(mDestroyBuffer, PFN_vkDestroyBuffer, "vkDestroyBuffer",
                                           VulkanSwapchainReadbackCommand::DestroyBuffer)
        LL_RESOLVE_READBACK_DEVICE_COMMAND(mGetBufferMemoryRequirements, PFN_vkGetBufferMemoryRequirements, "vkGetBufferMemoryRequirements",
                                           VulkanSwapchainReadbackCommand::GetBufferMemoryRequirements)
        LL_RESOLVE_READBACK_DEVICE_COMMAND(mAllocateMemory, PFN_vkAllocateMemory, "vkAllocateMemory",
                                           VulkanSwapchainReadbackCommand::AllocateMemory)
        LL_RESOLVE_READBACK_DEVICE_COMMAND(mFreeMemory, PFN_vkFreeMemory, "vkFreeMemory", VulkanSwapchainReadbackCommand::FreeMemory)
        LL_RESOLVE_READBACK_DEVICE_COMMAND(mBindBufferMemory, PFN_vkBindBufferMemory, "vkBindBufferMemory",
                                           VulkanSwapchainReadbackCommand::BindBufferMemory)
        LL_RESOLVE_READBACK_DEVICE_COMMAND(mMapMemory, PFN_vkMapMemory, "vkMapMemory", VulkanSwapchainReadbackCommand::MapMemory)
        LL_RESOLVE_READBACK_DEVICE_COMMAND(mUnmapMemory, PFN_vkUnmapMemory, "vkUnmapMemory", VulkanSwapchainReadbackCommand::UnmapMemory)

#undef LL_RESOLVE_READBACK_DEVICE_COMMAND

        return std::nullopt;
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

    bool checkedProduct(VkDeviceSize left, VkDeviceSize right, VkDeviceSize& product) noexcept
    {
        const VkDeviceSize vk_limit   = std::numeric_limits<VkDeviceSize>::max();
        const VkDeviceSize host_limit = static_cast<VkDeviceSize>(std::numeric_limits<std::size_t>::max());
        const VkDeviceSize limit      = vk_limit < host_limit ? vk_limit : host_limit;
        if (left == 0 || right == 0 || left > limit / right)
        {
            return false;
        }
        product = left * right;
        return true;
    }

    bool admittedObservationFormat(VkFormat format) noexcept
    {
        return format == VK_FORMAT_R8G8B8A8_UNORM || format == VK_FORMAT_B8G8R8A8_UNORM;
    }

    bool validMemoryRequirements(const VkMemoryRequirements& requirements, VkDeviceSize byte_count) noexcept
    {
        return requirements.size != 0 && requirements.size >= byte_count &&
               requirements.size <= static_cast<VkDeviceSize>(std::numeric_limits<std::size_t>::max()) && requirements.alignment != 0 &&
               (requirements.alignment & (requirements.alignment - 1)) == 0 && requirements.memoryTypeBits != 0;
    }

    std::optional<std::uint32_t> selectMemoryType(const VkPhysicalDeviceMemoryProperties& properties,
                                                  const VkMemoryRequirements&             requirements) noexcept
    {
        constexpr VkMemoryPropertyFlags required_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        // Logical-device creation enables neither device-coherent AMD memory nor
        // the QCOM tile-memory heap feature.
        constexpr VkMemoryPropertyFlags forbidden_type_flags = VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD;
        constexpr VkMemoryHeapFlags     forbidden_heap_flags = VK_MEMORY_HEAP_TILE_MEMORY_BIT_QCOM;
        for (std::uint32_t index = 0; index < properties.memoryTypeCount; ++index)
        {
            const std::uint32_t bit = std::uint32_t{ 1 } << index;
            if ((requirements.memoryTypeBits & bit) == 0)
            {
                continue;
            }
            const VkMemoryType& type = properties.memoryTypes[index];
            const VkMemoryHeap& heap = properties.memoryHeaps[type.heapIndex];
            if ((type.propertyFlags & required_flags) != required_flags || (type.propertyFlags & forbidden_type_flags) != 0 ||
                (heap.flags & forbidden_heap_flags) != 0 || heap.size < requirements.size)
            {
                continue;
            }
            return index;
        }
        return std::nullopt;
    }

    VkBufferCreateInfo bufferCreateInfo(VkDeviceSize byte_count) noexcept
    {
        VkBufferCreateInfo info{};
        info.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.flags       = 0;
        info.size        = byte_count;
        info.usage       = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        return info;
    }

    void rollBack(const SwapchainReadbackDispatch& dispatch,
                  VkDevice                         device,
                  VkBuffer&                        buffer,
                  VkDeviceMemory&                  memory,
                  bool                             mapped) noexcept
    {
        if (mapped)
        {
            dispatch.mUnmapMemory(device, memory);
        }
        if (buffer != VK_NULL_HANDLE)
        {
            dispatch.mDestroyBuffer(device, buffer, nullptr);
            buffer = VK_NULL_HANDLE;
        }
        if (memory != VK_NULL_HANDLE)
        {
            dispatch.mFreeMemory(device, memory, nullptr);
            memory = VK_NULL_HANDLE;
        }
    }

} // namespace

struct VulkanSwapchainReadbackGenerationFactory
{
    static VulkanSwapchainReadbackGeneration create(const VulkanPhysicalDeviceGeneration&         physical_device_generation,
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
                                                    PFN_vkFreeMemory                              free_memory) noexcept
    {
        return VulkanSwapchainReadbackGeneration(physical_device_generation, logical_device_generation, configuration_generation,
                                                 swapchain_generation, images_generation, row_bytes, byte_count, buffer, memory,
                                                 mapped_data, allocation_size, memory_type_index, memory_property_flags, unmap_memory,
                                                 destroy_buffer, free_memory);
    }
};

VulkanSwapchainReadbackGeneration::VulkanSwapchainReadbackGeneration(const VulkanPhysicalDeviceGeneration& physical_device_generation,
                                                                     const VulkanLogicalDeviceGeneration&  logical_device_generation,
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
                                                                     PFN_vkFreeMemory                              free_memory) noexcept :
    mPhysicalDeviceGeneration(&physical_device_generation),
    mLogicalDeviceGeneration(&logical_device_generation),
    mConfigurationGeneration(&configuration_generation),
    mSwapchainGeneration(&swapchain_generation),
    mImagesGeneration(&images_generation),
    mGetInstanceProcAddr(physical_device_generation.getInstanceProcAddr()),
    mInstance(physical_device_generation.instance()),
    mSurface(physical_device_generation.surface()),
    mPhysicalDevice(physical_device_generation.physicalDevice()),
    mPhysicalDeviceIndex(physical_device_generation.physicalDeviceIndex()),
    mDevice(logical_device_generation.device()),
    mQueue(logical_device_generation.queue()),
    mQueueFamilyIndex(logical_device_generation.queueFamilyIndex()),
    mQueueIndex(logical_device_generation.queueIndex()),
    mDrawableExtent(configuration_generation.drawableExtent()),
    mSwapchain(swapchain_generation.swapchain()),
    mImageFormat(configuration_generation.surfaceFormat().format),
    mImageExtent(configuration_generation.imageExtent()),
    mImageCount(images_generation.imageCount()),
    mRowBytes(row_bytes),
    mByteCount(byte_count),
    mBuffer(buffer),
    mMemory(memory),
    mMappedData(mapped_data),
    mAllocationSize(allocation_size),
    mMemoryTypeIndex(memory_type_index),
    mMemoryPropertyFlags(memory_property_flags),
    mUnmapMemory(unmap_memory),
    mDestroyBuffer(destroy_buffer),
    mFreeMemory(free_memory)
{
}

VulkanSwapchainReadbackGeneration::~VulkanSwapchainReadbackGeneration() noexcept
{
    reset();
}

VulkanSwapchainReadbackGeneration::VulkanSwapchainReadbackGeneration(VulkanSwapchainReadbackGeneration&& other) noexcept :
    mPhysicalDeviceGeneration(std::exchange(other.mPhysicalDeviceGeneration, nullptr)),
    mLogicalDeviceGeneration(std::exchange(other.mLogicalDeviceGeneration, nullptr)),
    mConfigurationGeneration(std::exchange(other.mConfigurationGeneration, nullptr)),
    mSwapchainGeneration(std::exchange(other.mSwapchainGeneration, nullptr)),
    mImagesGeneration(std::exchange(other.mImagesGeneration, nullptr)),
    mGetInstanceProcAddr(std::exchange(other.mGetInstanceProcAddr, nullptr)),
    mInstance(std::exchange(other.mInstance, VK_NULL_HANDLE)),
    mSurface(std::exchange(other.mSurface, VK_NULL_HANDLE)),
    mPhysicalDevice(std::exchange(other.mPhysicalDevice, VK_NULL_HANDLE)),
    mPhysicalDeviceIndex(std::exchange(other.mPhysicalDeviceIndex, 0)),
    mDevice(std::exchange(other.mDevice, VK_NULL_HANDLE)),
    mQueue(std::exchange(other.mQueue, VK_NULL_HANDLE)),
    mQueueFamilyIndex(std::exchange(other.mQueueFamilyIndex, VK_QUEUE_FAMILY_IGNORED)),
    mQueueIndex(std::exchange(other.mQueueIndex, 0)),
    mDrawableExtent(std::exchange(other.mDrawableExtent, {})),
    mSwapchain(std::exchange(other.mSwapchain, VK_NULL_HANDLE)),
    mImageFormat(std::exchange(other.mImageFormat, VK_FORMAT_UNDEFINED)),
    mImageExtent(std::exchange(other.mImageExtent, {})),
    mImageCount(std::exchange(other.mImageCount, 0)),
    mRowBytes(std::exchange(other.mRowBytes, 0)),
    mByteCount(std::exchange(other.mByteCount, 0)),
    mBuffer(std::exchange(other.mBuffer, VK_NULL_HANDLE)),
    mMemory(std::exchange(other.mMemory, VK_NULL_HANDLE)),
    mMappedData(std::exchange(other.mMappedData, nullptr)),
    mAllocationSize(std::exchange(other.mAllocationSize, 0)),
    mMemoryTypeIndex(std::exchange(other.mMemoryTypeIndex, 0)),
    mMemoryPropertyFlags(std::exchange(other.mMemoryPropertyFlags, 0)),
    mUnmapMemory(std::exchange(other.mUnmapMemory, nullptr)),
    mDestroyBuffer(std::exchange(other.mDestroyBuffer, nullptr)),
    mFreeMemory(std::exchange(other.mFreeMemory, nullptr))
{
}

bool VulkanSwapchainReadbackGeneration::createdFor(const VulkanPhysicalDeviceGeneration&         physical_device_generation,
                                                   const VulkanLogicalDeviceGeneration&          logical_device_generation,
                                                   const VulkanSwapchainConfigurationGeneration& configuration_generation,
                                                   const VulkanSwapchainGeneration&              swapchain_generation,
                                                   const VulkanSwapchainImagesGeneration&        images_generation) const noexcept
{
    const VkExtent2D drawable_extent = configuration_generation.drawableExtent();
    const VkExtent2D image_extent    = configuration_generation.imageExtent();
    return mBuffer != VK_NULL_HANDLE && mMemory != VK_NULL_HANDLE && mMappedData != nullptr &&
           mPhysicalDeviceGeneration == &physical_device_generation && mLogicalDeviceGeneration == &logical_device_generation &&
           mConfigurationGeneration == &configuration_generation && mSwapchainGeneration == &swapchain_generation &&
           mImagesGeneration == &images_generation && logical_device_generation.createdFor(physical_device_generation) &&
           configuration_generation.createdFor(physical_device_generation, logical_device_generation, drawable_extent) &&
           swapchain_generation.createdFor(logical_device_generation, configuration_generation) &&
           images_generation.createdFor(logical_device_generation, configuration_generation, swapchain_generation) &&
           mGetInstanceProcAddr == physical_device_generation.getInstanceProcAddr() && mInstance == physical_device_generation.instance() &&
           mSurface == physical_device_generation.surface() && mPhysicalDevice == physical_device_generation.physicalDevice() &&
           mPhysicalDeviceIndex == physical_device_generation.physicalDeviceIndex() && mDevice == logical_device_generation.device() &&
           mQueue == logical_device_generation.queue() && mQueueFamilyIndex == logical_device_generation.queueFamilyIndex() &&
           mQueueIndex == logical_device_generation.queueIndex() && mDrawableExtent.width == drawable_extent.width &&
           mDrawableExtent.height == drawable_extent.height && mSwapchain == swapchain_generation.swapchain() &&
           mImageFormat == configuration_generation.surfaceFormat().format && mImageExtent.width == image_extent.width &&
           mImageExtent.height == image_extent.height && mImageCount == images_generation.imageCount();
}

bool VulkanSwapchainReadbackGeneration::hasValidPresentationObservationLayout() const noexcept
{
    constexpr VkMemoryPropertyFlags required_memory = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    if (mBuffer == VK_NULL_HANDLE || mMemory == VK_NULL_HANDLE || mMappedData == nullptr || !admittedObservationFormat(mImageFormat) ||
        mImageCount == 0 || (mMemoryPropertyFlags & required_memory) != required_memory)
    {
        return false;
    }

    const auto  layout_result = VulkanSwapchainReadbackDetail::checkedByteLayout(mImageExtent.width, mImageExtent.height);
    const auto* layout        = std::get_if<VulkanSwapchainReadbackByteLayout>(&layout_result);
    return layout && layout->mRowBytes == mRowBytes && layout->mByteCount == mByteCount && mAllocationSize >= mByteCount;
}

bool VulkanSwapchainReadbackGeneration::poisonForPresentationObservation() noexcept
{
    if (!hasValidPresentationObservationLayout())
    {
        return false;
    }

    constexpr std::array<std::uint8_t, 4> sentinel{ 0x11, 0x22, 0x33, 0x44 };
    auto*                                 bytes = static_cast<std::uint8_t*>(mMappedData);
    const std::size_t                     size  = static_cast<std::size_t>(mByteCount);
    for (std::size_t offset = 0; offset < size; offset += sentinel.size())
    {
        std::copy(sentinel.begin(), sentinel.end(), bytes + offset);
    }
    return true;
}

std::optional<VulkanSwapchainReadbackObservation> VulkanSwapchainReadbackGeneration::classifyPresentationObservation() const noexcept
{
    if (!hasValidPresentationObservationLayout())
    {
        return std::nullopt;
    }

    VulkanSwapchainReadbackObservation observation;
    observation.mImageFormat     = mImageFormat;
    observation.mImageExtent     = mImageExtent;
    observation.mTotalPixelCount = static_cast<std::uint64_t>(mByteCount / 4);

    const auto*       bytes = static_cast<const std::uint8_t*>(mMappedData);
    const std::size_t size  = static_cast<std::size_t>(mByteCount);
    for (std::size_t offset = 0; offset < size; offset += 4)
    {
        const std::uint8_t red   = bytes[offset + (mImageFormat == VK_FORMAT_R8G8B8A8_UNORM ? 0 : 2)];
        const std::uint8_t green = bytes[offset + 1];
        const std::uint8_t blue  = bytes[offset + (mImageFormat == VK_FORMAT_R8G8B8A8_UNORM ? 2 : 0)];
        const std::uint8_t alpha = bytes[offset + 3];
        if (red == 0 && green == 255 && blue == 0 && alpha == 255)
        {
            ++observation.mGreenPixelCount;
        }
        else if (red == 255 && green == 0 && blue == 0 && alpha == 255)
        {
            ++observation.mRedPixelCount;
        }
        else
        {
            ++observation.mUnexpectedPixelCount;
        }
    }
    return observation;
}

void VulkanSwapchainReadbackGeneration::reset() noexcept
{
    if (mMappedData != nullptr && mMemory != VK_NULL_HANDLE && mUnmapMemory)
    {
        mUnmapMemory(mDevice, mMemory);
    }
    mMappedData = nullptr;
    if (mBuffer != VK_NULL_HANDLE && mDestroyBuffer)
    {
        mDestroyBuffer(mDevice, mBuffer, nullptr);
    }
    mBuffer = VK_NULL_HANDLE;
    if (mMemory != VK_NULL_HANDLE && mFreeMemory)
    {
        mFreeMemory(mDevice, mMemory, nullptr);
    }
    mMemory                   = VK_NULL_HANDLE;
    mPhysicalDeviceGeneration = nullptr;
    mLogicalDeviceGeneration  = nullptr;
    mConfigurationGeneration  = nullptr;
    mSwapchainGeneration      = nullptr;
    mImagesGeneration         = nullptr;
    mGetInstanceProcAddr      = nullptr;
    mInstance                 = VK_NULL_HANDLE;
    mSurface                  = VK_NULL_HANDLE;
    mPhysicalDevice           = VK_NULL_HANDLE;
    mPhysicalDeviceIndex      = 0;
    mDevice                   = VK_NULL_HANDLE;
    mQueue                    = VK_NULL_HANDLE;
    mQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
    mQueueIndex               = 0;
    mDrawableExtent           = {};
    mSwapchain                = VK_NULL_HANDLE;
    mImageFormat              = VK_FORMAT_UNDEFINED;
    mImageExtent              = {};
    mImageCount               = 0;
    mRowBytes                 = 0;
    mByteCount                = 0;
    mAllocationSize           = 0;
    mMemoryTypeIndex          = 0;
    mMemoryPropertyFlags      = 0;
    mUnmapMemory              = nullptr;
    mDestroyBuffer            = nullptr;
    mFreeMemory               = nullptr;
}

VulkanSwapchainReadbackResolutionResult resolveVulkanSwapchainReadbackGeneration(
    const VulkanPhysicalDeviceGeneration&         physical_device_generation,
    const VulkanLogicalDeviceGeneration&          logical_device_generation,
    const VulkanSwapchainConfigurationGeneration& configuration_generation,
    const VulkanSwapchainGeneration&              swapchain_generation,
    const VulkanSwapchainImagesGeneration&        images_generation) noexcept
{
    if (auto error = parentError(physical_device_generation, logical_device_generation, configuration_generation, swapchain_generation,
                                 images_generation))
    {
        return *error;
    }

    const VkFormat format = configuration_generation.surfaceFormat().format;
    if (!admittedObservationFormat(format))
    {
        return failure(VulkanSwapchainReadbackResolutionCode::UnsupportedImageFormat);
    }

    const VkExtent2D extent        = configuration_generation.imageExtent();
    auto             layout_result = VulkanSwapchainReadbackDetail::checkedByteLayout(extent.width, extent.height);
    if (const auto* error = std::get_if<VulkanSwapchainReadbackResolutionError>(&layout_result))
    {
        return *error;
    }
    const auto layout     = std::get<VulkanSwapchainReadbackByteLayout>(layout_result);
    const auto row_bytes  = layout.mRowBytes;
    const auto byte_count = layout.mByteCount;

    SwapchainReadbackDispatch dispatch;
    if (auto error = resolveDispatch(physical_device_generation, logical_device_generation, dispatch))
    {
        return *error;
    }

    if (auto error = parentError(physical_device_generation, logical_device_generation, configuration_generation, swapchain_generation,
                                 images_generation))
    {
        return *error;
    }

    const VkPhysicalDevice physical_device = physical_device_generation.physicalDevice();
    const VkDevice         device          = logical_device_generation.device();

    VkPhysicalDeviceMemoryProperties memory_properties{};
    dispatch.mGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);
    if (auto error = parentError(physical_device_generation, logical_device_generation, configuration_generation, swapchain_generation,
                                 images_generation))
    {
        return *error;
    }
    if (!validMemoryProperties(memory_properties))
    {
        return failure(VulkanSwapchainReadbackResolutionCode::InvalidPhysicalDeviceMemoryProperties,
                       VulkanSwapchainReadbackCommand::GetPhysicalDeviceMemoryProperties);
    }

    const VkBufferCreateInfo create_info   = bufferCreateInfo(byte_count);
    VkBuffer                 buffer        = VK_NULL_HANDLE;
    const VkResult           create_result = dispatch.mCreateBuffer(device, &create_info, nullptr, &buffer);
    if (create_result != VK_SUCCESS)
    {
        return failure(VulkanSwapchainReadbackResolutionCode::BufferCreationFailure, VulkanSwapchainReadbackCommand::CreateBuffer,
                       create_result);
    }
    if (buffer == VK_NULL_HANDLE)
    {
        return failure(VulkanSwapchainReadbackResolutionCode::NullBufferOnSuccess, VulkanSwapchainReadbackCommand::CreateBuffer);
    }

    VkMemoryRequirements requirements{};
    dispatch.mGetBufferMemoryRequirements(device, buffer, &requirements);
    if (!validMemoryRequirements(requirements, byte_count))
    {
        VkDeviceMemory no_memory = VK_NULL_HANDLE;
        rollBack(dispatch, device, buffer, no_memory, false);
        return failure(VulkanSwapchainReadbackResolutionCode::InvalidBufferMemoryRequirements,
                       VulkanSwapchainReadbackCommand::GetBufferMemoryRequirements);
    }

    const std::optional<std::uint32_t> memory_type_index = selectMemoryType(memory_properties, requirements);
    if (!memory_type_index)
    {
        VkDeviceMemory no_memory = VK_NULL_HANDLE;
        rollBack(dispatch, device, buffer, no_memory, false);
        return failure(VulkanSwapchainReadbackResolutionCode::NoCompatibleMemoryType);
    }

    VkMemoryAllocateInfo allocate_info{};
    allocate_info.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate_info.allocationSize  = requirements.size;
    allocate_info.memoryTypeIndex = *memory_type_index;

    VkDeviceMemory memory          = VK_NULL_HANDLE;
    const VkResult allocate_result = dispatch.mAllocateMemory(device, &allocate_info, nullptr, &memory);
    if (allocate_result != VK_SUCCESS)
    {
        VkDeviceMemory no_memory = VK_NULL_HANDLE;
        rollBack(dispatch, device, buffer, no_memory, false);
        return failure(VulkanSwapchainReadbackResolutionCode::MemoryAllocationFailure, VulkanSwapchainReadbackCommand::AllocateMemory,
                       allocate_result);
    }
    if (memory == VK_NULL_HANDLE)
    {
        rollBack(dispatch, device, buffer, memory, false);
        return failure(VulkanSwapchainReadbackResolutionCode::NullMemoryOnSuccess, VulkanSwapchainReadbackCommand::AllocateMemory);
    }

    const VkResult bind_result = dispatch.mBindBufferMemory(device, buffer, memory, 0);
    if (bind_result != VK_SUCCESS)
    {
        rollBack(dispatch, device, buffer, memory, false);
        return failure(VulkanSwapchainReadbackResolutionCode::BufferMemoryBindFailure, VulkanSwapchainReadbackCommand::BindBufferMemory,
                       bind_result);
    }

    void*          mapped_data = nullptr;
    const VkResult map_result  = dispatch.mMapMemory(device, memory, 0, VK_WHOLE_SIZE, 0, &mapped_data);
    if (map_result != VK_SUCCESS)
    {
        rollBack(dispatch, device, buffer, memory, false);
        return failure(VulkanSwapchainReadbackResolutionCode::MemoryMapFailure, VulkanSwapchainReadbackCommand::MapMemory, map_result);
    }
    if (mapped_data == nullptr)
    {
        rollBack(dispatch, device, buffer, memory, true);
        return failure(VulkanSwapchainReadbackResolutionCode::NullMappedDataOnSuccess, VulkanSwapchainReadbackCommand::MapMemory);
    }

    if (auto error = parentError(physical_device_generation, logical_device_generation, configuration_generation, swapchain_generation,
                                 images_generation))
    {
        rollBack(dispatch, device, buffer, memory, true);
        return *error;
    }

    const VkMemoryPropertyFlags property_flags = memory_properties.memoryTypes[*memory_type_index].propertyFlags;
    return VulkanSwapchainReadbackGenerationFactory::create(physical_device_generation, logical_device_generation, configuration_generation,
                                                            swapchain_generation, images_generation, row_bytes, byte_count, buffer, memory,
                                                            mapped_data, requirements.size, *memory_type_index, property_flags,
                                                            dispatch.mUnmapMemory, dispatch.mDestroyBuffer, dispatch.mFreeMemory);
}

namespace VulkanSwapchainReadbackDetail
{

    VulkanSwapchainReadbackByteLayoutResult checkedByteLayout(VkDeviceSize width, VkDeviceSize height) noexcept
    {
        VkDeviceSize row_bytes;
        if (!checkedProduct(width, 4, row_bytes))
        {
            return failure(VulkanSwapchainReadbackResolutionCode::RowBytesOverflow);
        }
        VkDeviceSize byte_count;
        if (!checkedProduct(row_bytes, height, byte_count))
        {
            return failure(VulkanSwapchainReadbackResolutionCode::ByteCountOverflow);
        }
        return VulkanSwapchainReadbackByteLayout{ row_bytes, byte_count };
    }

} // namespace VulkanSwapchainReadbackDetail

} // namespace LLRenderVulkan
