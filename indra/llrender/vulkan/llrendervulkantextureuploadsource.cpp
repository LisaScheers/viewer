/**
 * @file llrendervulkantextureuploadsource.cpp
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

#include "llrendervulkantextureuploadsource.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace LLRenderVulkan
{

namespace
{
    struct TextureUploadSourceDispatch
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

    VulkanTextureUploadSourceResolutionError failure(VulkanTextureUploadSourceResolutionCode         code,
                                                     std::optional<VulkanTextureUploadSourceCommand> command = std::nullopt,
                                                     VkResult                                        result  = VK_SUCCESS) noexcept
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

    bool canonicalDescription(const VulkanTextureUploadSourceDescription& description) noexcept
    {
        const LLRenderContract::StreamingUploadHandles handles;
        return description.mHandle == handles.mReplacementImage &&
               description.mExpectedRevision == LLRenderContract::TEXTURE_UPLOAD_REVISION;
    }

    std::optional<VulkanTextureUploadSourceResolutionError> validateInputs(const VulkanPhysicalDeviceGeneration& physical_device_generation,
                                                                           const VulkanLogicalDeviceGeneration&  logical_device_generation,
                                                                           const VulkanTextureUploadSourceDescription& description) noexcept
    {
        if (physical_device_generation.getInstanceProcAddr() == nullptr || physical_device_generation.instance() == VK_NULL_HANDLE ||
            physical_device_generation.surface() == VK_NULL_HANDLE || physical_device_generation.physicalDevice() == VK_NULL_HANDLE ||
            physical_device_generation.queueFamilyIndex() == VK_QUEUE_FAMILY_IGNORED)
        {
            return failure(VulkanTextureUploadSourceResolutionCode::InvalidPhysicalDeviceGeneration);
        }
        if (!logical_device_generation.createdFor(physical_device_generation))
        {
            return failure(VulkanTextureUploadSourceResolutionCode::InvalidLogicalDeviceGeneration);
        }
        if (!canonicalDescription(description))
        {
            return failure(VulkanTextureUploadSourceResolutionCode::InvalidDescription);
        }
        return std::nullopt;
    }

    std::optional<VulkanTextureUploadSourceResolutionError> resolveDispatch(
        const VulkanPhysicalDeviceGeneration&       physical_device_generation,
        const VulkanLogicalDeviceGeneration&        logical_device_generation,
        const VulkanTextureUploadSourceDescription& description,
        TextureUploadSourceDispatch&                dispatch) noexcept
    {
        const PFN_vkGetInstanceProcAddr get_instance_proc_addr = physical_device_generation.getInstanceProcAddr();
        const VkInstance                instance               = physical_device_generation.instance();
        const VkDevice                  device                 = logical_device_generation.device();

#define LL_RESOLVE_TEXTURE_UPLOAD_SOURCE_INSTANCE_COMMAND(member, type, command_name, command_value)     \
    dispatch.member = resolveInstance<type>(get_instance_proc_addr, instance, command_name);             \
    if (auto error = validateInputs(physical_device_generation, logical_device_generation, description)) \
    {                                                                                                    \
        return *error;                                                                                   \
    }                                                                                                    \
    if (!dispatch.member)                                                                                \
    {                                                                                                    \
        return failure(VulkanTextureUploadSourceResolutionCode::MissingRequiredCommand, command_value);  \
    }

        LL_RESOLVE_TEXTURE_UPLOAD_SOURCE_INSTANCE_COMMAND(mGetPhysicalDeviceMemoryProperties, PFN_vkGetPhysicalDeviceMemoryProperties,
                                                          "vkGetPhysicalDeviceMemoryProperties",
                                                          VulkanTextureUploadSourceCommand::GetPhysicalDeviceMemoryProperties)
        LL_RESOLVE_TEXTURE_UPLOAD_SOURCE_INSTANCE_COMMAND(mGetDeviceProcAddr, PFN_vkGetDeviceProcAddr, "vkGetDeviceProcAddr",
                                                          VulkanTextureUploadSourceCommand::GetDeviceProcAddr)

#undef LL_RESOLVE_TEXTURE_UPLOAD_SOURCE_INSTANCE_COMMAND

#define LL_RESOLVE_TEXTURE_UPLOAD_SOURCE_DEVICE_COMMAND(member, type, command_name, command_value)       \
    dispatch.member = resolveDevice<type>(dispatch.mGetDeviceProcAddr, device, command_name);            \
    if (auto error = validateInputs(physical_device_generation, logical_device_generation, description)) \
    {                                                                                                    \
        return *error;                                                                                   \
    }                                                                                                    \
    if (!dispatch.member)                                                                                \
    {                                                                                                    \
        return failure(VulkanTextureUploadSourceResolutionCode::MissingRequiredCommand, command_value);  \
    }

        LL_RESOLVE_TEXTURE_UPLOAD_SOURCE_DEVICE_COMMAND(mCreateBuffer, PFN_vkCreateBuffer, "vkCreateBuffer",
                                                        VulkanTextureUploadSourceCommand::CreateBuffer)
        LL_RESOLVE_TEXTURE_UPLOAD_SOURCE_DEVICE_COMMAND(mDestroyBuffer, PFN_vkDestroyBuffer, "vkDestroyBuffer",
                                                        VulkanTextureUploadSourceCommand::DestroyBuffer)
        LL_RESOLVE_TEXTURE_UPLOAD_SOURCE_DEVICE_COMMAND(mGetBufferMemoryRequirements, PFN_vkGetBufferMemoryRequirements,
                                                        "vkGetBufferMemoryRequirements",
                                                        VulkanTextureUploadSourceCommand::GetBufferMemoryRequirements)
        LL_RESOLVE_TEXTURE_UPLOAD_SOURCE_DEVICE_COMMAND(mAllocateMemory, PFN_vkAllocateMemory, "vkAllocateMemory",
                                                        VulkanTextureUploadSourceCommand::AllocateMemory)
        LL_RESOLVE_TEXTURE_UPLOAD_SOURCE_DEVICE_COMMAND(mFreeMemory, PFN_vkFreeMemory, "vkFreeMemory",
                                                        VulkanTextureUploadSourceCommand::FreeMemory)
        LL_RESOLVE_TEXTURE_UPLOAD_SOURCE_DEVICE_COMMAND(mBindBufferMemory, PFN_vkBindBufferMemory, "vkBindBufferMemory",
                                                        VulkanTextureUploadSourceCommand::BindBufferMemory)
        LL_RESOLVE_TEXTURE_UPLOAD_SOURCE_DEVICE_COMMAND(mMapMemory, PFN_vkMapMemory, "vkMapMemory",
                                                        VulkanTextureUploadSourceCommand::MapMemory)
        LL_RESOLVE_TEXTURE_UPLOAD_SOURCE_DEVICE_COMMAND(mUnmapMemory, PFN_vkUnmapMemory, "vkUnmapMemory",
                                                        VulkanTextureUploadSourceCommand::UnmapMemory)
        LL_RESOLVE_TEXTURE_UPLOAD_SOURCE_DEVICE_COMMAND(mFlushMappedMemoryRanges, PFN_vkFlushMappedMemoryRanges,
                                                        "vkFlushMappedMemoryRanges",
                                                        VulkanTextureUploadSourceCommand::FlushMappedMemoryRanges)

#undef LL_RESOLVE_TEXTURE_UPLOAD_SOURCE_DEVICE_COMMAND

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
        return requirements.size >= VULKAN_TEXTURE_UPLOAD_SOURCE_BYTE_COUNT &&
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
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
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

    VkBufferCreateInfo bufferCreateInfo() noexcept
    {
        VkBufferCreateInfo info{};
        info.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.flags       = 0;
        info.size        = VULKAN_TEXTURE_UPLOAD_SOURCE_BYTE_COUNT;
        info.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        return info;
    }

    void rollBack(const TextureUploadSourceDispatch& dispatch,
                  VkDevice                           device,
                  VkBuffer                           buffer,
                  bool                               owns_buffer,
                  VkDeviceMemory                     memory,
                  bool                               owns_memory,
                  bool                               owns_mapping) noexcept
    {
        if (owns_mapping)
        {
            dispatch.mUnmapMemory(device, memory);
        }
        if (owns_buffer)
        {
            dispatch.mDestroyBuffer(device, buffer, nullptr);
        }
        if (owns_memory)
        {
            dispatch.mFreeMemory(device, memory, nullptr);
        }
    }

} // namespace

struct VulkanTextureUploadSourceGenerationFactory
{
    static VulkanTextureUploadSourceGeneration create(const VulkanPhysicalDeviceGeneration&       physical_device_generation,
                                                      const VulkanLogicalDeviceGeneration&        logical_device_generation,
                                                      const VulkanTextureUploadSourceDescription& description,
                                                      VkBuffer                                    buffer,
                                                      VkDeviceMemory                              memory,
                                                      VkDeviceSize                                allocation_size,
                                                      std::uint32_t                               memory_type_index,
                                                      VkMemoryPropertyFlags                       memory_property_flags,
                                                      PFN_vkDestroyBuffer                         destroy_buffer,
                                                      PFN_vkFreeMemory                            free_memory) noexcept
    {
        return VulkanTextureUploadSourceGeneration(physical_device_generation,
                                                   logical_device_generation,
                                                   description,
                                                   buffer,
                                                   memory,
                                                   allocation_size,
                                                   memory_type_index,
                                                   memory_property_flags,
                                                   destroy_buffer,
                                                   free_memory);
    }
};

VulkanTextureUploadSourceGeneration::VulkanTextureUploadSourceGeneration(const VulkanPhysicalDeviceGeneration& physical_device_generation,
                                                                         const VulkanLogicalDeviceGeneration&  logical_device_generation,
                                                                         const VulkanTextureUploadSourceDescription& description,
                                                                         VkBuffer                                    buffer,
                                                                         VkDeviceMemory                              memory,
                                                                         VkDeviceSize                                allocation_size,
                                                                         std::uint32_t                               memory_type_index,
                                                                         VkMemoryPropertyFlags                       memory_property_flags,
                                                                         PFN_vkDestroyBuffer                         destroy_buffer,
                                                                         PFN_vkFreeMemory                            free_memory) noexcept :
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
    mContentIdentity(LLRenderContract::stableByteContentIdentity(description.mBytes)),
    mBuffer(buffer),
    mMemory(memory),
    mByteCount(VULKAN_TEXTURE_UPLOAD_SOURCE_BYTE_COUNT),
    mAllocationSize(allocation_size),
    mMemoryTypeIndex(memory_type_index),
    mMemoryPropertyFlags(memory_property_flags),
    mDestroyBuffer(destroy_buffer),
    mFreeMemory(free_memory)
{
}

VulkanTextureUploadSourceGeneration::~VulkanTextureUploadSourceGeneration() noexcept
{
    reset();
}

VulkanTextureUploadSourceGeneration::VulkanTextureUploadSourceGeneration(VulkanTextureUploadSourceGeneration&& other) noexcept :
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

LLRenderContract::Extent2D VulkanTextureUploadSourceGeneration::residentExtent() const noexcept
{
    return mBuffer != VK_NULL_HANDLE ? LLRenderContract::Extent2D{ LLRenderContract::TEXTURE_UPLOAD_RESIDENT_WIDTH,
                                                                   LLRenderContract::TEXTURE_UPLOAD_RESIDENT_HEIGHT }
                                     : LLRenderContract::Extent2D{};
}

LLRenderContract::PixelFormat VulkanTextureUploadSourceGeneration::pixelFormat() const noexcept
{
    return LLRenderContract::PixelFormat::RGBA8Unorm;
}

std::uint32_t VulkanTextureUploadSourceGeneration::rowPitch() const noexcept
{
    return mBuffer != VK_NULL_HANDLE ? LLRenderContract::TEXTURE_UPLOAD_ROW_PITCH : 0;
}

LLRenderContract::RowOrigin VulkanTextureUploadSourceGeneration::rowOrigin() const noexcept
{
    return LLRenderContract::RowOrigin::TopLeft;
}

bool VulkanTextureUploadSourceGeneration::isCoherent() const noexcept
{
    return (mMemoryPropertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
}

bool VulkanTextureUploadSourceGeneration::matchesDescription(const VulkanTextureUploadSourceDescription& description) const noexcept
{
    return mBuffer != VK_NULL_HANDLE && mMemory != VK_NULL_HANDLE && mByteCount == VULKAN_TEXTURE_UPLOAD_SOURCE_BYTE_COUNT &&
           mContentIdentity != 0 && mContentIdentity == LLRenderContract::stableByteContentIdentity(description.mBytes) &&
           mDescription == description;
}

bool VulkanTextureUploadSourceGeneration::createdFor(const VulkanPhysicalDeviceGeneration& physical_device_generation,
                                                     const VulkanLogicalDeviceGeneration&  logical_device_generation) const noexcept
{
    return mBuffer != VK_NULL_HANDLE && mMemory != VK_NULL_HANDLE && canonicalDescription(mDescription) &&
           mByteCount == VULKAN_TEXTURE_UPLOAD_SOURCE_BYTE_COUNT && mContentIdentity != 0 &&
           mContentIdentity == LLRenderContract::stableByteContentIdentity(mDescription.mBytes) &&
           mPhysicalDeviceGeneration == &physical_device_generation && mLogicalDeviceGeneration == &logical_device_generation &&
           logical_device_generation.createdFor(physical_device_generation) &&
           mGetInstanceProcAddr == physical_device_generation.getInstanceProcAddr() && mInstance == physical_device_generation.instance() &&
           mSurface == physical_device_generation.surface() && mPhysicalDevice == physical_device_generation.physicalDevice() &&
           mPhysicalDeviceIndex == physical_device_generation.physicalDeviceIndex() && mDevice == logical_device_generation.device() &&
           mQueue == logical_device_generation.queue() && mQueueFamilyIndex == logical_device_generation.queueFamilyIndex() &&
           mQueueIndex == logical_device_generation.queueIndex() && mAllocationSize >= VULKAN_TEXTURE_UPLOAD_SOURCE_BYTE_COUNT &&
           (mMemoryPropertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0 && mDestroyBuffer && mFreeMemory;
}

void VulkanTextureUploadSourceGeneration::reset() noexcept
{
    const VkDevice            device         = std::exchange(mDevice, VK_NULL_HANDLE);
    const VkBuffer            buffer         = std::exchange(mBuffer, VK_NULL_HANDLE);
    const VkDeviceMemory      memory         = std::exchange(mMemory, VK_NULL_HANDLE);
    const PFN_vkDestroyBuffer destroy_buffer = std::exchange(mDestroyBuffer, nullptr);
    const PFN_vkFreeMemory    free_memory    = std::exchange(mFreeMemory, nullptr);

    mPhysicalDeviceGeneration = nullptr;
    mLogicalDeviceGeneration  = nullptr;
    mGetInstanceProcAddr      = nullptr;
    mInstance                 = VK_NULL_HANDLE;
    mSurface                  = VK_NULL_HANDLE;
    mPhysicalDevice           = VK_NULL_HANDLE;
    mPhysicalDeviceIndex      = 0;
    mQueue                    = VK_NULL_HANDLE;
    mQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
    mQueueIndex               = 0;
    mDescription              = {};
    mContentIdentity          = 0;
    mByteCount                = 0;
    mAllocationSize           = 0;
    mMemoryTypeIndex          = 0;
    mMemoryPropertyFlags      = 0;

    if (buffer != VK_NULL_HANDLE && destroy_buffer)
    {
        destroy_buffer(device, buffer, nullptr);
    }
    if (memory != VK_NULL_HANDLE && free_memory)
    {
        free_memory(device, memory, nullptr);
    }
}

VulkanTextureUploadSourceResolutionResult resolveVulkanTextureUploadSourceGeneration(
    const VulkanPhysicalDeviceGeneration&       physical_device_generation,
    const VulkanLogicalDeviceGeneration&        logical_device_generation,
    const VulkanTextureUploadSourceDescription& description) noexcept
{
    const VulkanTextureUploadSourceDescription owned_description = description;
    if (auto error = validateInputs(physical_device_generation, logical_device_generation, owned_description))
    {
        return *error;
    }

    TextureUploadSourceDispatch dispatch;
    if (auto error = resolveDispatch(physical_device_generation, logical_device_generation, owned_description, dispatch))
    {
        return *error;
    }

    const VkPhysicalDevice physical_device = physical_device_generation.physicalDevice();
    const VkDevice         device          = logical_device_generation.device();

    VkPhysicalDeviceMemoryProperties memory_properties{};
    dispatch.mGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);
    if (auto error = validateInputs(physical_device_generation, logical_device_generation, owned_description))
    {
        return *error;
    }
    if (!validMemoryProperties(memory_properties))
    {
        return failure(VulkanTextureUploadSourceResolutionCode::InvalidPhysicalDeviceMemoryProperties,
                       VulkanTextureUploadSourceCommand::GetPhysicalDeviceMemoryProperties);
    }

    const VkBufferCreateInfo create_info   = bufferCreateInfo();
    VkBuffer                 buffer        = VK_NULL_HANDLE;
    const VkResult           create_result = dispatch.mCreateBuffer(device, &create_info, nullptr, &buffer);
    if (auto error = validateInputs(physical_device_generation, logical_device_generation, owned_description))
    {
        rollBack(dispatch, device, buffer, create_result == VK_SUCCESS && buffer != VK_NULL_HANDLE, VK_NULL_HANDLE, false, false);
        return *error;
    }
    if (create_result != VK_SUCCESS)
    {
        return failure(VulkanTextureUploadSourceResolutionCode::BufferCreationFailure,
                       VulkanTextureUploadSourceCommand::CreateBuffer,
                       create_result);
    }
    if (buffer == VK_NULL_HANDLE)
    {
        return failure(VulkanTextureUploadSourceResolutionCode::NullBufferOnSuccess, VulkanTextureUploadSourceCommand::CreateBuffer);
    }

    VkMemoryRequirements requirements{};
    dispatch.mGetBufferMemoryRequirements(device, buffer, &requirements);
    if (auto error = validateInputs(physical_device_generation, logical_device_generation, owned_description))
    {
        rollBack(dispatch, device, buffer, true, VK_NULL_HANDLE, false, false);
        return *error;
    }
    if (!validMemoryRequirements(requirements))
    {
        rollBack(dispatch, device, buffer, true, VK_NULL_HANDLE, false, false);
        return failure(VulkanTextureUploadSourceResolutionCode::InvalidBufferMemoryRequirements,
                       VulkanTextureUploadSourceCommand::GetBufferMemoryRequirements);
    }

    const std::optional<std::uint32_t> memory_type_index = selectMemoryType(memory_properties, requirements);
    if (!memory_type_index)
    {
        rollBack(dispatch, device, buffer, true, VK_NULL_HANDLE, false, false);
        return failure(VulkanTextureUploadSourceResolutionCode::NoCompatibleMemoryType);
    }

    VkMemoryAllocateInfo allocate_info{};
    allocate_info.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate_info.allocationSize  = requirements.size;
    allocate_info.memoryTypeIndex = *memory_type_index;

    VkDeviceMemory memory          = VK_NULL_HANDLE;
    const VkResult allocate_result = dispatch.mAllocateMemory(device, &allocate_info, nullptr, &memory);
    if (auto error = validateInputs(physical_device_generation, logical_device_generation, owned_description))
    {
        rollBack(dispatch, device, buffer, true, memory, allocate_result == VK_SUCCESS && memory != VK_NULL_HANDLE, false);
        return *error;
    }
    if (allocate_result != VK_SUCCESS)
    {
        rollBack(dispatch, device, buffer, true, VK_NULL_HANDLE, false, false);
        return failure(VulkanTextureUploadSourceResolutionCode::MemoryAllocationFailure,
                       VulkanTextureUploadSourceCommand::AllocateMemory,
                       allocate_result);
    }
    if (memory == VK_NULL_HANDLE)
    {
        rollBack(dispatch, device, buffer, true, VK_NULL_HANDLE, false, false);
        return failure(VulkanTextureUploadSourceResolutionCode::NullMemoryOnSuccess, VulkanTextureUploadSourceCommand::AllocateMemory);
    }

    const VkResult bind_result = dispatch.mBindBufferMemory(device, buffer, memory, 0);
    if (auto error = validateInputs(physical_device_generation, logical_device_generation, owned_description))
    {
        rollBack(dispatch, device, buffer, true, memory, true, false);
        return *error;
    }
    if (bind_result != VK_SUCCESS)
    {
        rollBack(dispatch, device, buffer, true, memory, true, false);
        return failure(VulkanTextureUploadSourceResolutionCode::BufferMemoryBindFailure,
                       VulkanTextureUploadSourceCommand::BindBufferMemory,
                       bind_result);
    }

    void*          mapped_data = nullptr;
    const VkResult map_result  = dispatch.mMapMemory(device, memory, 0, VK_WHOLE_SIZE, 0, &mapped_data);
    if (auto error = validateInputs(physical_device_generation, logical_device_generation, owned_description))
    {
        rollBack(dispatch, device, buffer, true, memory, true, map_result == VK_SUCCESS);
        return *error;
    }
    if (map_result != VK_SUCCESS)
    {
        rollBack(dispatch, device, buffer, true, memory, true, false);
        return failure(VulkanTextureUploadSourceResolutionCode::MemoryMapFailure, VulkanTextureUploadSourceCommand::MapMemory, map_result);
    }
    if (mapped_data == nullptr)
    {
        rollBack(dispatch, device, buffer, true, memory, true, true);
        return failure(VulkanTextureUploadSourceResolutionCode::NullMappedDataOnSuccess, VulkanTextureUploadSourceCommand::MapMemory);
    }

    std::copy(owned_description.mBytes.begin(), owned_description.mBytes.end(), static_cast<std::uint8_t*>(mapped_data));

    const VkMemoryPropertyFlags memory_property_flags = memory_properties.memoryTypes[*memory_type_index].propertyFlags;
    if ((memory_property_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0)
    {
        VkMappedMemoryRange flush_range{};
        flush_range.sType           = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        flush_range.memory          = memory;
        flush_range.offset          = 0;
        flush_range.size            = VK_WHOLE_SIZE;
        const VkResult flush_result = dispatch.mFlushMappedMemoryRanges(device, 1, &flush_range);
        if (auto error = validateInputs(physical_device_generation, logical_device_generation, owned_description))
        {
            rollBack(dispatch, device, buffer, true, memory, true, true);
            return *error;
        }
        if (flush_result != VK_SUCCESS)
        {
            rollBack(dispatch, device, buffer, true, memory, true, true);
            return failure(VulkanTextureUploadSourceResolutionCode::MemoryFlushFailure,
                           VulkanTextureUploadSourceCommand::FlushMappedMemoryRanges,
                           flush_result);
        }
    }

    dispatch.mUnmapMemory(device, memory);
    if (auto error = validateInputs(physical_device_generation, logical_device_generation, owned_description))
    {
        rollBack(dispatch, device, buffer, true, memory, true, false);
        return *error;
    }

    return VulkanTextureUploadSourceGenerationFactory::create(physical_device_generation,
                                                              logical_device_generation,
                                                              owned_description,
                                                              buffer,
                                                              memory,
                                                              requirements.size,
                                                              *memory_type_index,
                                                              memory_property_flags,
                                                              dispatch.mDestroyBuffer,
                                                              dispatch.mFreeMemory);
}

} // namespace LLRenderVulkan
