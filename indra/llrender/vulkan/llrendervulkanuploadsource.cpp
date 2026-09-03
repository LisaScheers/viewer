/**
 * @file llrendervulkanuploadsource.cpp
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

#include "llrendervulkanuploadsource.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace LLRenderVulkan
{

namespace
{
    struct UploadSourceDispatch
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
        PFN_vkFlushMappedMemoryRanges           mFlushMappedMemoryRanges           = nullptr;
    };

    VulkanUploadSourceResolutionError failure(VulkanUploadSourceResolutionCode         code,
                                              std::optional<VulkanUploadSourceCommand> command = std::nullopt,
                                              VkResult                                 result  = VK_SUCCESS) noexcept
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

    std::optional<VulkanUploadSourceResolutionError> parentError(const VulkanPhysicalDeviceGeneration& physical_device_generation,
                                                                 const VulkanLogicalDeviceGeneration&  logical_device_generation) noexcept
    {
        if (physical_device_generation.getInstanceProcAddr() == nullptr || physical_device_generation.instance() == VK_NULL_HANDLE ||
            physical_device_generation.surface() == VK_NULL_HANDLE || physical_device_generation.physicalDevice() == VK_NULL_HANDLE ||
            physical_device_generation.queueFamilyIndex() == VK_QUEUE_FAMILY_IGNORED)
        {
            return failure(VulkanUploadSourceResolutionCode::InvalidPhysicalDeviceGeneration);
        }
        if (!logical_device_generation.createdFor(physical_device_generation))
        {
            return failure(VulkanUploadSourceResolutionCode::InvalidLogicalDeviceGeneration);
        }
        return std::nullopt;
    }

    std::optional<VulkanUploadSourceResolutionError> resolveDispatch(const VulkanPhysicalDeviceGeneration& physical_device_generation,
                                                                     const VulkanLogicalDeviceGeneration&  logical_device_generation,
                                                                     UploadSourceDispatch&                 dispatch) noexcept
    {
        const PFN_vkGetInstanceProcAddr get_instance_proc_addr = physical_device_generation.getInstanceProcAddr();
        const VkInstance                instance               = physical_device_generation.instance();
        const VkDevice                  device                 = logical_device_generation.device();

        dispatch.mGetPhysicalDeviceMemoryProperties = resolveInstance<PFN_vkGetPhysicalDeviceMemoryProperties>(
            get_instance_proc_addr, instance, "vkGetPhysicalDeviceMemoryProperties");
        if (!dispatch.mGetPhysicalDeviceMemoryProperties)
        {
            return failure(VulkanUploadSourceResolutionCode::MissingRequiredCommand,
                           VulkanUploadSourceCommand::GetPhysicalDeviceMemoryProperties);
        }

        dispatch.mGetDeviceProcAddr = resolveInstance<PFN_vkGetDeviceProcAddr>(get_instance_proc_addr, instance, "vkGetDeviceProcAddr");
        if (!dispatch.mGetDeviceProcAddr)
        {
            return failure(VulkanUploadSourceResolutionCode::MissingRequiredCommand, VulkanUploadSourceCommand::GetDeviceProcAddr);
        }

#define LL_RESOLVE_UPLOAD_SOURCE_DEVICE_COMMAND(member, type, command_name, command_value)       \
    dispatch.member = resolveDevice<type>(dispatch.mGetDeviceProcAddr, device, command_name);    \
    if (!dispatch.member)                                                                        \
    {                                                                                            \
        return failure(VulkanUploadSourceResolutionCode::MissingRequiredCommand, command_value); \
    }

        LL_RESOLVE_UPLOAD_SOURCE_DEVICE_COMMAND(mCreateBuffer, PFN_vkCreateBuffer, "vkCreateBuffer",
                                                VulkanUploadSourceCommand::CreateBuffer)
        LL_RESOLVE_UPLOAD_SOURCE_DEVICE_COMMAND(mDestroyBuffer, PFN_vkDestroyBuffer, "vkDestroyBuffer",
                                                VulkanUploadSourceCommand::DestroyBuffer)
        LL_RESOLVE_UPLOAD_SOURCE_DEVICE_COMMAND(mGetBufferMemoryRequirements, PFN_vkGetBufferMemoryRequirements,
                                                "vkGetBufferMemoryRequirements", VulkanUploadSourceCommand::GetBufferMemoryRequirements)
        LL_RESOLVE_UPLOAD_SOURCE_DEVICE_COMMAND(mAllocateMemory, PFN_vkAllocateMemory, "vkAllocateMemory",
                                                VulkanUploadSourceCommand::AllocateMemory)
        LL_RESOLVE_UPLOAD_SOURCE_DEVICE_COMMAND(mFreeMemory, PFN_vkFreeMemory, "vkFreeMemory", VulkanUploadSourceCommand::FreeMemory)
        LL_RESOLVE_UPLOAD_SOURCE_DEVICE_COMMAND(mBindBufferMemory, PFN_vkBindBufferMemory, "vkBindBufferMemory",
                                                VulkanUploadSourceCommand::BindBufferMemory)
        LL_RESOLVE_UPLOAD_SOURCE_DEVICE_COMMAND(mMapMemory, PFN_vkMapMemory, "vkMapMemory", VulkanUploadSourceCommand::MapMemory)
        LL_RESOLVE_UPLOAD_SOURCE_DEVICE_COMMAND(mUnmapMemory, PFN_vkUnmapMemory, "vkUnmapMemory", VulkanUploadSourceCommand::UnmapMemory)
        LL_RESOLVE_UPLOAD_SOURCE_DEVICE_COMMAND(mFlushMappedMemoryRanges, PFN_vkFlushMappedMemoryRanges, "vkFlushMappedMemoryRanges",
                                                VulkanUploadSourceCommand::FlushMappedMemoryRanges)

#undef LL_RESOLVE_UPLOAD_SOURCE_DEVICE_COMMAND

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

    bool validMemoryRequirements(const VkMemoryRequirements& requirements) noexcept
    {
        return requirements.size >= VULKAN_UPLOAD_SOURCE_BYTE_COUNT &&
               requirements.size <= static_cast<VkDeviceSize>(std::numeric_limits<std::size_t>::max()) && requirements.alignment != 0 &&
               (requirements.alignment & (requirements.alignment - 1)) == 0 && requirements.memoryTypeBits != 0;
    }

    std::optional<std::uint32_t> selectMemoryType(const VkPhysicalDeviceMemoryProperties& properties,
                                                  const VkMemoryRequirements&             requirements) noexcept
    {
        constexpr VkMemoryPropertyFlags forbidden_type_flags =
            VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD | VK_MEMORY_PROPERTY_PROTECTED_BIT | VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT;
        constexpr VkMemoryHeapFlags                    forbidden_heap_flags = VK_MEMORY_HEAP_TILE_MEMORY_BIT_QCOM;
        constexpr std::array<VkMemoryPropertyFlags, 2> required_flags{
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
        };

        for (VkMemoryPropertyFlags required : required_flags)
        {
            for (std::uint32_t index = 0; index < properties.memoryTypeCount; ++index)
            {
                const std::uint32_t bit = std::uint32_t{ 1 } << index;
                if ((requirements.memoryTypeBits & bit) == 0)
                {
                    continue;
                }
                const VkMemoryType& type = properties.memoryTypes[index];
                const VkMemoryHeap& heap = properties.memoryHeaps[type.heapIndex];
                if ((type.propertyFlags & required) != required || (type.propertyFlags & forbidden_type_flags) != 0 ||
                    (heap.flags & forbidden_heap_flags) != 0 || heap.size < requirements.size)
                {
                    continue;
                }
                return index;
            }
        }
        return std::nullopt;
    }

    std::uint64_t computeContentIdentity(const VulkanUploadSourceBytes& bytes) noexcept
    {
        return LLRenderContract::stableByteContentIdentity(bytes);
    }

    VkBufferCreateInfo bufferCreateInfo() noexcept
    {
        VkBufferCreateInfo info{};
        info.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.flags       = 0;
        info.size        = VULKAN_UPLOAD_SOURCE_BYTE_COUNT;
        info.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        return info;
    }

    void rollBack(const UploadSourceDispatch& dispatch, VkDevice device, VkBuffer& buffer, VkDeviceMemory& memory, bool mapped) noexcept
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

struct VulkanUploadSourceGenerationFactory
{
    static VulkanUploadSourceGeneration create(const VulkanPhysicalDeviceGeneration& physical_device_generation,
                                               const VulkanLogicalDeviceGeneration&  logical_device_generation,
                                               const VulkanUploadSourceDescription&  description,
                                               VkBuffer                              buffer,
                                               VkDeviceMemory                        memory,
                                               VkDeviceSize                          allocation_size,
                                               std::uint32_t                         memory_type_index,
                                               VkMemoryPropertyFlags                 memory_property_flags,
                                               PFN_vkDestroyBuffer                   destroy_buffer,
                                               PFN_vkFreeMemory                      free_memory) noexcept
    {
        return VulkanUploadSourceGeneration(physical_device_generation, logical_device_generation, description, buffer, memory,
                                            allocation_size, memory_type_index, memory_property_flags, destroy_buffer, free_memory);
    }
};

VulkanUploadSourceGeneration::VulkanUploadSourceGeneration(const VulkanPhysicalDeviceGeneration& physical_device_generation,
                                                           const VulkanLogicalDeviceGeneration&  logical_device_generation,
                                                           const VulkanUploadSourceDescription&  description,
                                                           VkBuffer                              buffer,
                                                           VkDeviceMemory                        memory,
                                                           VkDeviceSize                          allocation_size,
                                                           std::uint32_t                         memory_type_index,
                                                           VkMemoryPropertyFlags                 memory_property_flags,
                                                           PFN_vkDestroyBuffer                   destroy_buffer,
                                                           PFN_vkFreeMemory                      free_memory) noexcept :
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
    mContentIdentity(computeContentIdentity(description.mBytes)),
    mBuffer(buffer),
    mMemory(memory),
    mByteCount(VULKAN_UPLOAD_SOURCE_BYTE_COUNT),
    mAllocationSize(allocation_size),
    mMemoryTypeIndex(memory_type_index),
    mMemoryPropertyFlags(memory_property_flags),
    mDestroyBuffer(destroy_buffer),
    mFreeMemory(free_memory)
{
}

VulkanUploadSourceGeneration::~VulkanUploadSourceGeneration() noexcept
{
    reset();
}

VulkanUploadSourceGeneration::VulkanUploadSourceGeneration(VulkanUploadSourceGeneration&& other) noexcept :
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
    mContentIdentity(std::exchange(other.mContentIdentity, 0)),
    mBuffer(std::exchange(other.mBuffer, VK_NULL_HANDLE)),
    mMemory(std::exchange(other.mMemory, VK_NULL_HANDLE)),
    mByteCount(std::exchange(other.mByteCount, 0)),
    mAllocationSize(std::exchange(other.mAllocationSize, 0)),
    mMemoryTypeIndex(std::exchange(other.mMemoryTypeIndex, 0)),
    mMemoryPropertyFlags(std::exchange(other.mMemoryPropertyFlags, 0)),
    mDestroyBuffer(std::exchange(other.mDestroyBuffer, nullptr)),
    mFreeMemory(std::exchange(other.mFreeMemory, nullptr))
{
}

bool VulkanUploadSourceGeneration::matchesDescription(const VulkanUploadSourceDescription& description) const noexcept
{
    return mBuffer != VK_NULL_HANDLE && mMemory != VK_NULL_HANDLE && mByteCount == VULKAN_UPLOAD_SOURCE_BYTE_COUNT &&
           mContentIdentity != 0 && mContentIdentity == computeContentIdentity(description.mBytes) && mDescription == description;
}

bool VulkanUploadSourceGeneration::createdFor(const VulkanPhysicalDeviceGeneration& physical_device_generation,
                                              const VulkanLogicalDeviceGeneration&  logical_device_generation) const noexcept
{
    return mBuffer != VK_NULL_HANDLE && mMemory != VK_NULL_HANDLE && mDescription.mHandle &&
           mByteCount == VULKAN_UPLOAD_SOURCE_BYTE_COUNT && mContentIdentity != 0 &&
           mContentIdentity == computeContentIdentity(mDescription.mBytes) && mPhysicalDeviceGeneration == &physical_device_generation &&
           mLogicalDeviceGeneration == &logical_device_generation && logical_device_generation.createdFor(physical_device_generation) &&
           mGetInstanceProcAddr == physical_device_generation.getInstanceProcAddr() && mInstance == physical_device_generation.instance() &&
           mSurface == physical_device_generation.surface() && mPhysicalDevice == physical_device_generation.physicalDevice() &&
           mPhysicalDeviceIndex == physical_device_generation.physicalDeviceIndex() && mDevice == logical_device_generation.device() &&
           mQueue == logical_device_generation.queue() && mQueueFamilyIndex == logical_device_generation.queueFamilyIndex() &&
           mQueueIndex == logical_device_generation.queueIndex() && mAllocationSize >= VULKAN_UPLOAD_SOURCE_BYTE_COUNT &&
           (mMemoryPropertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
}

void VulkanUploadSourceGeneration::reset() noexcept
{
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
    mGetInstanceProcAddr      = nullptr;
    mInstance                 = VK_NULL_HANDLE;
    mSurface                  = VK_NULL_HANDLE;
    mPhysicalDevice           = VK_NULL_HANDLE;
    mPhysicalDeviceIndex      = 0;
    mDevice                   = VK_NULL_HANDLE;
    mQueue                    = VK_NULL_HANDLE;
    mQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
    mQueueIndex               = 0;
    mDescription              = {};
    mContentIdentity          = 0;
    mByteCount                = 0;
    mAllocationSize           = 0;
    mMemoryTypeIndex          = 0;
    mMemoryPropertyFlags      = 0;
    mDestroyBuffer            = nullptr;
    mFreeMemory               = nullptr;
}

VulkanUploadSourceResolutionResult resolveVulkanUploadSourceGeneration(const VulkanPhysicalDeviceGeneration& physical_device_generation,
                                                                       const VulkanLogicalDeviceGeneration&  logical_device_generation,
                                                                       const VulkanUploadSourceDescription&  description) noexcept
{
    const VulkanUploadSourceDescription owned_description = description;

    if (auto error = parentError(physical_device_generation, logical_device_generation))
    {
        return *error;
    }
    if (!owned_description.mHandle)
    {
        return failure(VulkanUploadSourceResolutionCode::InvalidDescription);
    }

    UploadSourceDispatch dispatch;
    if (auto error = resolveDispatch(physical_device_generation, logical_device_generation, dispatch))
    {
        return *error;
    }
    if (auto error = parentError(physical_device_generation, logical_device_generation))
    {
        return *error;
    }

    const VkPhysicalDevice physical_device = physical_device_generation.physicalDevice();
    const VkDevice         device          = logical_device_generation.device();

    VkPhysicalDeviceMemoryProperties memory_properties{};
    dispatch.mGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);
    if (auto error = parentError(physical_device_generation, logical_device_generation))
    {
        return *error;
    }
    if (!validMemoryProperties(memory_properties))
    {
        return failure(VulkanUploadSourceResolutionCode::InvalidPhysicalDeviceMemoryProperties,
                       VulkanUploadSourceCommand::GetPhysicalDeviceMemoryProperties);
    }

    const VkBufferCreateInfo create_info   = bufferCreateInfo();
    VkBuffer                 buffer        = VK_NULL_HANDLE;
    const VkResult           create_result = dispatch.mCreateBuffer(device, &create_info, nullptr, &buffer);
    if (create_result != VK_SUCCESS)
    {
        return failure(VulkanUploadSourceResolutionCode::BufferCreationFailure, VulkanUploadSourceCommand::CreateBuffer, create_result);
    }
    if (buffer == VK_NULL_HANDLE)
    {
        return failure(VulkanUploadSourceResolutionCode::NullBufferOnSuccess, VulkanUploadSourceCommand::CreateBuffer);
    }
    if (auto error = parentError(physical_device_generation, logical_device_generation))
    {
        VkDeviceMemory no_memory = VK_NULL_HANDLE;
        rollBack(dispatch, device, buffer, no_memory, false);
        return *error;
    }

    VkMemoryRequirements requirements{};
    dispatch.mGetBufferMemoryRequirements(device, buffer, &requirements);
    if (auto error = parentError(physical_device_generation, logical_device_generation))
    {
        VkDeviceMemory no_memory = VK_NULL_HANDLE;
        rollBack(dispatch, device, buffer, no_memory, false);
        return *error;
    }
    if (!validMemoryRequirements(requirements))
    {
        VkDeviceMemory no_memory = VK_NULL_HANDLE;
        rollBack(dispatch, device, buffer, no_memory, false);
        return failure(VulkanUploadSourceResolutionCode::InvalidBufferMemoryRequirements,
                       VulkanUploadSourceCommand::GetBufferMemoryRequirements);
    }

    const std::optional<std::uint32_t> memory_type_index = selectMemoryType(memory_properties, requirements);
    if (!memory_type_index)
    {
        VkDeviceMemory no_memory = VK_NULL_HANDLE;
        rollBack(dispatch, device, buffer, no_memory, false);
        return failure(VulkanUploadSourceResolutionCode::NoCompatibleMemoryType);
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
        return failure(VulkanUploadSourceResolutionCode::MemoryAllocationFailure,
                       VulkanUploadSourceCommand::AllocateMemory,
                       allocate_result);
    }
    if (memory == VK_NULL_HANDLE)
    {
        rollBack(dispatch, device, buffer, memory, false);
        return failure(VulkanUploadSourceResolutionCode::NullMemoryOnSuccess, VulkanUploadSourceCommand::AllocateMemory);
    }
    if (auto error = parentError(physical_device_generation, logical_device_generation))
    {
        rollBack(dispatch, device, buffer, memory, false);
        return *error;
    }

    const VkResult bind_result = dispatch.mBindBufferMemory(device, buffer, memory, 0);
    if (bind_result != VK_SUCCESS)
    {
        rollBack(dispatch, device, buffer, memory, false);
        return failure(VulkanUploadSourceResolutionCode::BufferMemoryBindFailure, VulkanUploadSourceCommand::BindBufferMemory, bind_result);
    }
    if (auto error = parentError(physical_device_generation, logical_device_generation))
    {
        rollBack(dispatch, device, buffer, memory, false);
        return *error;
    }

    void*          mapped_data = nullptr;
    const VkResult map_result  = dispatch.mMapMemory(device, memory, 0, VK_WHOLE_SIZE, 0, &mapped_data);
    if (map_result != VK_SUCCESS)
    {
        rollBack(dispatch, device, buffer, memory, false);
        return failure(VulkanUploadSourceResolutionCode::MemoryMapFailure, VulkanUploadSourceCommand::MapMemory, map_result);
    }
    if (auto error = parentError(physical_device_generation, logical_device_generation))
    {
        rollBack(dispatch, device, buffer, memory, true);
        return *error;
    }
    if (mapped_data == nullptr)
    {
        rollBack(dispatch, device, buffer, memory, true);
        return failure(VulkanUploadSourceResolutionCode::NullMappedDataOnSuccess, VulkanUploadSourceCommand::MapMemory);
    }

    std::copy(owned_description.mBytes.begin(), owned_description.mBytes.end(), static_cast<std::uint8_t*>(mapped_data));

    const VkMemoryPropertyFlags property_flags = memory_properties.memoryTypes[*memory_type_index].propertyFlags;
    if ((property_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0)
    {
        VkMappedMemoryRange flush_range{};
        flush_range.sType           = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        flush_range.memory          = memory;
        flush_range.offset          = 0;
        flush_range.size            = VK_WHOLE_SIZE;
        const VkResult flush_result = dispatch.mFlushMappedMemoryRanges(device, 1, &flush_range);
        if (flush_result != VK_SUCCESS)
        {
            rollBack(dispatch, device, buffer, memory, true);
            return failure(VulkanUploadSourceResolutionCode::MemoryFlushFailure,
                           VulkanUploadSourceCommand::FlushMappedMemoryRanges,
                           flush_result);
        }
        if (auto error = parentError(physical_device_generation, logical_device_generation))
        {
            rollBack(dispatch, device, buffer, memory, true);
            return *error;
        }
    }

    dispatch.mUnmapMemory(device, memory);

    if (auto error = parentError(physical_device_generation, logical_device_generation))
    {
        rollBack(dispatch, device, buffer, memory, false);
        return *error;
    }

    return VulkanUploadSourceGenerationFactory::create(physical_device_generation,
                                                       logical_device_generation,
                                                       owned_description,
                                                       buffer,
                                                       memory,
                                                       requirements.size,
                                                       *memory_type_index,
                                                       property_flags,
                                                       dispatch.mDestroyBuffer,
                                                       dispatch.mFreeMemory);
}

} // namespace LLRenderVulkan
