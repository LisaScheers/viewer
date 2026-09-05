/**
 * @file llrendervulkanuploaddestination.cpp
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

#include "llrendervulkanuploaddestination.h"

#include <array>
#include <limits>
#include <utility>

namespace LLRenderVulkan
{

namespace
{
    struct UploadDestinationDispatch
    {
        PFN_vkGetPhysicalDeviceMemoryProperties mGetPhysicalDeviceMemoryProperties = nullptr;
        PFN_vkGetDeviceProcAddr                 mGetDeviceProcAddr                 = nullptr;
        PFN_vkCreateBuffer                      mCreateBuffer                      = nullptr;
        PFN_vkDestroyBuffer                     mDestroyBuffer                     = nullptr;
        PFN_vkGetBufferMemoryRequirements       mGetBufferMemoryRequirements       = nullptr;
        PFN_vkAllocateMemory                    mAllocateMemory                    = nullptr;
        PFN_vkFreeMemory                        mFreeMemory                        = nullptr;
        PFN_vkBindBufferMemory                  mBindBufferMemory                  = nullptr;
    };

    VulkanUploadDestinationResolutionError failure(VulkanUploadDestinationResolutionCode         code,
                                                   std::optional<VulkanUploadDestinationCommand> command = std::nullopt,
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

    std::optional<VulkanUploadDestinationResolutionError> validateInputs(const VulkanPhysicalDeviceGeneration& physical_device_generation,
                                                                         const VulkanLogicalDeviceGeneration&  logical_device_generation,
                                                                         const VulkanUploadSourceGeneration&   upload_source_generation,
                                                                         const VulkanUploadSourceDescription&  description) noexcept
    {
        if (physical_device_generation.getInstanceProcAddr() == nullptr || physical_device_generation.instance() == VK_NULL_HANDLE ||
            physical_device_generation.surface() == VK_NULL_HANDLE || physical_device_generation.physicalDevice() == VK_NULL_HANDLE ||
            physical_device_generation.queueFamilyIndex() == VK_QUEUE_FAMILY_IGNORED)
        {
            return failure(VulkanUploadDestinationResolutionCode::InvalidPhysicalDeviceGeneration);
        }
        if (!logical_device_generation.createdFor(physical_device_generation))
        {
            return failure(VulkanUploadDestinationResolutionCode::InvalidLogicalDeviceGeneration);
        }
        if (!description.mHandle)
        {
            return failure(VulkanUploadDestinationResolutionCode::InvalidDescription);
        }
        if (!upload_source_generation.createdFor(physical_device_generation, logical_device_generation))
        {
            return failure(VulkanUploadDestinationResolutionCode::InvalidUploadSourceGeneration);
        }
        if (!upload_source_generation.matchesDescription(description))
        {
            return failure(VulkanUploadDestinationResolutionCode::UploadSourceDescriptionMismatch);
        }
        return std::nullopt;
    }

    std::optional<VulkanUploadDestinationResolutionError> resolveDispatch(const VulkanPhysicalDeviceGeneration& physical_device_generation,
                                                                          const VulkanLogicalDeviceGeneration&  logical_device_generation,
                                                                          UploadDestinationDispatch&            dispatch) noexcept
    {
        const PFN_vkGetInstanceProcAddr get_instance_proc_addr = physical_device_generation.getInstanceProcAddr();
        const VkInstance                instance               = physical_device_generation.instance();
        const VkDevice                  device                 = logical_device_generation.device();

        dispatch.mGetPhysicalDeviceMemoryProperties = resolveInstance<PFN_vkGetPhysicalDeviceMemoryProperties>(
            get_instance_proc_addr, instance, "vkGetPhysicalDeviceMemoryProperties");
        if (!dispatch.mGetPhysicalDeviceMemoryProperties)
        {
            return failure(VulkanUploadDestinationResolutionCode::MissingRequiredCommand,
                           VulkanUploadDestinationCommand::GetPhysicalDeviceMemoryProperties);
        }

        dispatch.mGetDeviceProcAddr = resolveInstance<PFN_vkGetDeviceProcAddr>(get_instance_proc_addr, instance, "vkGetDeviceProcAddr");
        if (!dispatch.mGetDeviceProcAddr)
        {
            return failure(VulkanUploadDestinationResolutionCode::MissingRequiredCommand,
                           VulkanUploadDestinationCommand::GetDeviceProcAddr);
        }

#define LL_RESOLVE_UPLOAD_DESTINATION_DEVICE_COMMAND(member, type, command_name, command_value)       \
    dispatch.member = resolveDevice<type>(dispatch.mGetDeviceProcAddr, device, command_name);         \
    if (!dispatch.member)                                                                             \
    {                                                                                                 \
        return failure(VulkanUploadDestinationResolutionCode::MissingRequiredCommand, command_value); \
    }

        LL_RESOLVE_UPLOAD_DESTINATION_DEVICE_COMMAND(mCreateBuffer, PFN_vkCreateBuffer, "vkCreateBuffer",
                                                     VulkanUploadDestinationCommand::CreateBuffer)
        LL_RESOLVE_UPLOAD_DESTINATION_DEVICE_COMMAND(mDestroyBuffer, PFN_vkDestroyBuffer, "vkDestroyBuffer",
                                                     VulkanUploadDestinationCommand::DestroyBuffer)
        LL_RESOLVE_UPLOAD_DESTINATION_DEVICE_COMMAND(mGetBufferMemoryRequirements,
                                                     PFN_vkGetBufferMemoryRequirements,
                                                     "vkGetBufferMemoryRequirements",
                                                     VulkanUploadDestinationCommand::GetBufferMemoryRequirements)
        LL_RESOLVE_UPLOAD_DESTINATION_DEVICE_COMMAND(mAllocateMemory, PFN_vkAllocateMemory, "vkAllocateMemory",
                                                     VulkanUploadDestinationCommand::AllocateMemory)
        LL_RESOLVE_UPLOAD_DESTINATION_DEVICE_COMMAND(mFreeMemory, PFN_vkFreeMemory, "vkFreeMemory",
                                                     VulkanUploadDestinationCommand::FreeMemory)
        LL_RESOLVE_UPLOAD_DESTINATION_DEVICE_COMMAND(mBindBufferMemory, PFN_vkBindBufferMemory, "vkBindBufferMemory",
                                                     VulkanUploadDestinationCommand::BindBufferMemory)

#undef LL_RESOLVE_UPLOAD_DESTINATION_DEVICE_COMMAND

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

    VkBufferCreateInfo bufferCreateInfo() noexcept
    {
        VkBufferCreateInfo info{};
        info.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.flags       = 0;
        info.size        = VULKAN_UPLOAD_SOURCE_BYTE_COUNT;
        info.usage       = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        return info;
    }

    void rollBack(const UploadDestinationDispatch& dispatch, VkDevice device, VkBuffer& buffer, VkDeviceMemory& memory) noexcept
    {
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

struct VulkanUploadDestinationGenerationFactory
{
    static VulkanUploadDestinationGeneration create(const VulkanPhysicalDeviceGeneration& physical_device_generation,
                                                    const VulkanLogicalDeviceGeneration&  logical_device_generation,
                                                    const VulkanUploadSourceDescription&  description,
                                                    std::uint64_t                         expected_content_identity,
                                                    VkBuffer                              buffer,
                                                    VkDeviceMemory                        memory,
                                                    VkDeviceSize                          allocation_size,
                                                    std::uint32_t                         memory_type_index,
                                                    VkMemoryPropertyFlags                 memory_property_flags,
                                                    PFN_vkDestroyBuffer                   destroy_buffer,
                                                    PFN_vkFreeMemory                      free_memory) noexcept
    {
        return VulkanUploadDestinationGeneration(physical_device_generation,
                                                 logical_device_generation,
                                                 description,
                                                 expected_content_identity,
                                                 buffer,
                                                 memory,
                                                 allocation_size,
                                                 memory_type_index,
                                                 memory_property_flags,
                                                 destroy_buffer,
                                                 free_memory);
    }
};

VulkanUploadDestinationGeneration::VulkanUploadDestinationGeneration(const VulkanPhysicalDeviceGeneration& physical_device_generation,
                                                                     const VulkanLogicalDeviceGeneration&  logical_device_generation,
                                                                     const VulkanUploadSourceDescription&  description,
                                                                     std::uint64_t                         expected_content_identity,
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
    mExpectedContentIdentity(expected_content_identity),
    mBuffer(buffer),
    mMemory(memory),
    mByteCount(VULKAN_UPLOAD_SOURCE_BYTE_COUNT),
    mUsage(VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT),
    mAllocationSize(allocation_size),
    mMemoryTypeIndex(memory_type_index),
    mMemoryPropertyFlags(memory_property_flags),
    mDestroyBuffer(destroy_buffer),
    mFreeMemory(free_memory)
{
}

VulkanUploadDestinationGeneration::~VulkanUploadDestinationGeneration() noexcept
{
    reset();
}

VulkanUploadDestinationGeneration::VulkanUploadDestinationGeneration(VulkanUploadDestinationGeneration&& other) noexcept :
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
    mExpectedContentIdentity(std::exchange(other.mExpectedContentIdentity, 0)),
    mResidentContentIdentity(std::exchange(other.mResidentContentIdentity, 0)),
    mBuffer(std::exchange(other.mBuffer, VK_NULL_HANDLE)),
    mMemory(std::exchange(other.mMemory, VK_NULL_HANDLE)),
    mByteCount(std::exchange(other.mByteCount, 0)),
    mUsage(std::exchange(other.mUsage, 0)),
    mAllocationSize(std::exchange(other.mAllocationSize, 0)),
    mMemoryTypeIndex(std::exchange(other.mMemoryTypeIndex, 0)),
    mMemoryPropertyFlags(std::exchange(other.mMemoryPropertyFlags, 0)),
    mDestroyBuffer(std::exchange(other.mDestroyBuffer, nullptr)),
    mFreeMemory(std::exchange(other.mFreeMemory, nullptr))
{
}

bool VulkanUploadDestinationGeneration::createdFor(const VulkanPhysicalDeviceGeneration& physical_device_generation,
                                                   const VulkanLogicalDeviceGeneration&  logical_device_generation) const noexcept
{
    return mBuffer != VK_NULL_HANDLE && mMemory != VK_NULL_HANDLE && mDescription.mHandle && mExpectedContentIdentity != 0 &&
           mByteCount == VULKAN_UPLOAD_SOURCE_BYTE_COUNT &&
           mUsage == (VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) &&
           mPhysicalDeviceGeneration == &physical_device_generation && mLogicalDeviceGeneration == &logical_device_generation &&
           logical_device_generation.createdFor(physical_device_generation) &&
           mGetInstanceProcAddr == physical_device_generation.getInstanceProcAddr() && mInstance == physical_device_generation.instance() &&
           mSurface == physical_device_generation.surface() && mPhysicalDevice == physical_device_generation.physicalDevice() &&
           mPhysicalDeviceIndex == physical_device_generation.physicalDeviceIndex() && mDevice == logical_device_generation.device() &&
           mQueue == logical_device_generation.queue() && mQueueFamilyIndex == logical_device_generation.queueFamilyIndex() &&
           mQueueIndex == logical_device_generation.queueIndex() && mAllocationSize >= VULKAN_UPLOAD_SOURCE_BYTE_COUNT && isDeviceLocal();
}

bool VulkanUploadDestinationGeneration::matchesDescription(const VulkanUploadSourceDescription& description) const noexcept
{
    return mBuffer != VK_NULL_HANDLE && mMemory != VK_NULL_HANDLE && mExpectedContentIdentity != 0 &&
           mByteCount == VULKAN_UPLOAD_SOURCE_BYTE_COUNT && mDescription == description;
}

bool VulkanUploadDestinationGeneration::matchesUploadSource(const VulkanUploadSourceGeneration& upload_source_generation) const noexcept
{
    return mPhysicalDeviceGeneration != nullptr && mLogicalDeviceGeneration != nullptr &&
           createdFor(*mPhysicalDeviceGeneration, *mLogicalDeviceGeneration) &&
           upload_source_generation.createdFor(*mPhysicalDeviceGeneration, *mLogicalDeviceGeneration) &&
           upload_source_generation.contentIdentity() == mExpectedContentIdentity &&
           upload_source_generation.matchesDescription(mDescription) && mBuffer != upload_source_generation.buffer() &&
           mMemory != upload_source_generation.memory();
}

bool VulkanUploadDestinationGeneration::markResident(std::uint64_t content_identity) noexcept
{
    if (mBuffer == VK_NULL_HANDLE || mMemory == VK_NULL_HANDLE || content_identity == 0 || content_identity != mExpectedContentIdentity)
    {
        return false;
    }
    mResidentContentIdentity = content_identity;
    return true;
}

void VulkanUploadDestinationGeneration::reset() noexcept
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
    mExpectedContentIdentity  = 0;
    mResidentContentIdentity  = 0;
    mByteCount                = 0;
    mUsage                    = 0;
    mAllocationSize           = 0;
    mMemoryTypeIndex          = 0;
    mMemoryPropertyFlags      = 0;
    mDestroyBuffer            = nullptr;
    mFreeMemory               = nullptr;
}

VulkanUploadDestinationResolutionResult resolveVulkanUploadDestinationGeneration(
    const VulkanPhysicalDeviceGeneration& physical_device_generation,
    const VulkanLogicalDeviceGeneration&  logical_device_generation,
    const VulkanUploadSourceGeneration&   upload_source_generation,
    const VulkanUploadSourceDescription&  description) noexcept
{
    const VulkanUploadSourceDescription owned_description         = description;
    const std::uint64_t                 expected_content_identity = upload_source_generation.contentIdentity();

    if (auto error = validateInputs(physical_device_generation, logical_device_generation, upload_source_generation, owned_description))
    {
        return *error;
    }

    UploadDestinationDispatch dispatch;
    if (auto error = resolveDispatch(physical_device_generation, logical_device_generation, dispatch))
    {
        return *error;
    }
    if (auto error = validateInputs(physical_device_generation, logical_device_generation, upload_source_generation, owned_description))
    {
        return *error;
    }

    const VkPhysicalDevice physical_device = physical_device_generation.physicalDevice();
    const VkDevice         device          = logical_device_generation.device();
    const VkBuffer         source_buffer   = upload_source_generation.buffer();
    const VkDeviceMemory   source_memory   = upload_source_generation.memory();

    VkPhysicalDeviceMemoryProperties memory_properties{};
    dispatch.mGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);
    if (auto error = validateInputs(physical_device_generation, logical_device_generation, upload_source_generation, owned_description))
    {
        return *error;
    }
    if (!validMemoryProperties(memory_properties))
    {
        return failure(VulkanUploadDestinationResolutionCode::InvalidPhysicalDeviceMemoryProperties,
                       VulkanUploadDestinationCommand::GetPhysicalDeviceMemoryProperties);
    }

    const VkBufferCreateInfo create_info   = bufferCreateInfo();
    VkBuffer                 buffer        = VK_NULL_HANDLE;
    const VkResult           create_result = dispatch.mCreateBuffer(device, &create_info, nullptr, &buffer);
    if (create_result != VK_SUCCESS)
    {
        return failure(VulkanUploadDestinationResolutionCode::BufferCreationFailure,
                       VulkanUploadDestinationCommand::CreateBuffer,
                       create_result);
    }
    if (buffer == VK_NULL_HANDLE)
    {
        return failure(VulkanUploadDestinationResolutionCode::NullBufferOnSuccess, VulkanUploadDestinationCommand::CreateBuffer);
    }
    if (buffer == source_buffer)
    {
        return failure(VulkanUploadDestinationResolutionCode::SourceDestinationBufferAlias, VulkanUploadDestinationCommand::CreateBuffer);
    }
    if (auto error = validateInputs(physical_device_generation, logical_device_generation, upload_source_generation, owned_description))
    {
        VkDeviceMemory no_memory = VK_NULL_HANDLE;
        rollBack(dispatch, device, buffer, no_memory);
        return *error;
    }

    VkMemoryRequirements requirements{};
    dispatch.mGetBufferMemoryRequirements(device, buffer, &requirements);
    if (auto error = validateInputs(physical_device_generation, logical_device_generation, upload_source_generation, owned_description))
    {
        VkDeviceMemory no_memory = VK_NULL_HANDLE;
        rollBack(dispatch, device, buffer, no_memory);
        return *error;
    }
    if (!validMemoryRequirements(requirements))
    {
        VkDeviceMemory no_memory = VK_NULL_HANDLE;
        rollBack(dispatch, device, buffer, no_memory);
        return failure(VulkanUploadDestinationResolutionCode::InvalidBufferMemoryRequirements,
                       VulkanUploadDestinationCommand::GetBufferMemoryRequirements);
    }

    const std::optional<std::uint32_t> memory_type_index = selectMemoryType(memory_properties, requirements);
    if (!memory_type_index)
    {
        VkDeviceMemory no_memory = VK_NULL_HANDLE;
        rollBack(dispatch, device, buffer, no_memory);
        return failure(VulkanUploadDestinationResolutionCode::NoCompatibleMemoryType);
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
        rollBack(dispatch, device, buffer, no_memory);
        return failure(VulkanUploadDestinationResolutionCode::MemoryAllocationFailure,
                       VulkanUploadDestinationCommand::AllocateMemory,
                       allocate_result);
    }
    if (memory == VK_NULL_HANDLE)
    {
        rollBack(dispatch, device, buffer, memory);
        return failure(VulkanUploadDestinationResolutionCode::NullMemoryOnSuccess, VulkanUploadDestinationCommand::AllocateMemory);
    }
    if (memory == source_memory)
    {
        VkDeviceMemory no_memory = VK_NULL_HANDLE;
        rollBack(dispatch, device, buffer, no_memory);
        return failure(VulkanUploadDestinationResolutionCode::SourceDestinationMemoryAlias, VulkanUploadDestinationCommand::AllocateMemory);
    }
    if (auto error = validateInputs(physical_device_generation, logical_device_generation, upload_source_generation, owned_description))
    {
        rollBack(dispatch, device, buffer, memory);
        return *error;
    }

    const VkResult bind_result = dispatch.mBindBufferMemory(device, buffer, memory, 0);
    if (bind_result != VK_SUCCESS)
    {
        rollBack(dispatch, device, buffer, memory);
        return failure(VulkanUploadDestinationResolutionCode::BufferMemoryBindFailure,
                       VulkanUploadDestinationCommand::BindBufferMemory,
                       bind_result);
    }
    if (auto error = validateInputs(physical_device_generation, logical_device_generation, upload_source_generation, owned_description))
    {
        rollBack(dispatch, device, buffer, memory);
        return *error;
    }

    return VulkanUploadDestinationGenerationFactory::create(physical_device_generation,
                                                            logical_device_generation,
                                                            owned_description,
                                                            expected_content_identity,
                                                            buffer,
                                                            memory,
                                                            requirements.size,
                                                            *memory_type_index,
                                                            memory_properties.memoryTypes[*memory_type_index].propertyFlags,
                                                            dispatch.mDestroyBuffer,
                                                            dispatch.mFreeMemory);
}

} // namespace LLRenderVulkan
