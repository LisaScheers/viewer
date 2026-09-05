/**
 * @file llrendervulkanuploadtransfer.cpp
 * @brief Loader-neutral ownership of one Vulkan upload transfer.
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

#include "llrendervulkanuploadtransfer.h"

#include "llrendervulkanuploaddestination.h"

#include <limits>
#include <utility>

namespace LLRenderVulkan
{

namespace
{
    struct UploadTransferDispatch
    {
        PFN_vkGetDeviceProcAddr      mGetDeviceProcAddr      = nullptr;
        PFN_vkCreateCommandPool      mCreateCommandPool      = nullptr;
        PFN_vkDestroyCommandPool     mDestroyCommandPool     = nullptr;
        PFN_vkAllocateCommandBuffers mAllocateCommandBuffers = nullptr;
        PFN_vkCreateFence            mCreateFence            = nullptr;
        PFN_vkDestroyFence           mDestroyFence           = nullptr;
        PFN_vkBeginCommandBuffer     mBeginCommandBuffer     = nullptr;
        PFN_vkCmdPipelineBarrier     mCmdPipelineBarrier     = nullptr;
        PFN_vkCmdCopyBuffer          mCmdCopyBuffer          = nullptr;
        PFN_vkEndCommandBuffer       mEndCommandBuffer       = nullptr;
        PFN_vkQueueSubmit            mQueueSubmit            = nullptr;
        PFN_vkWaitForFences          mWaitForFences          = nullptr;
    };

    VulkanUploadTransferResolutionError failure(VulkanUploadTransferResolutionCode         code,
                                                std::optional<VulkanUploadTransferCommand> command = std::nullopt,
                                                VkResult                                   result  = VK_SUCCESS) noexcept
    {
        return { code, command, result };
    }

    VulkanUploadTransferOperationError operationFailure(VulkanUploadTransferOperationCode          code,
                                                        VulkanUploadTransferDisposition            disposition,
                                                        std::optional<VulkanUploadTransferCommand> command = std::nullopt,
                                                        VkResult                                   result  = VK_SUCCESS) noexcept
    {
        return { code, command, result, disposition };
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

    bool validPhysical(const VulkanPhysicalDeviceGeneration& physical_device_generation) noexcept
    {
        return physical_device_generation.getInstanceProcAddr() != nullptr && physical_device_generation.instance() != VK_NULL_HANDLE &&
               physical_device_generation.surface() != VK_NULL_HANDLE && physical_device_generation.physicalDevice() != VK_NULL_HANDLE &&
               physical_device_generation.queueFamilyIndex() != VK_QUEUE_FAMILY_IGNORED;
    }

    bool validLogical(const VulkanPhysicalDeviceGeneration& physical_device_generation,
                      const VulkanLogicalDeviceGeneration&  logical_device_generation) noexcept
    {
        return logical_device_generation.createdFor(physical_device_generation) && logical_device_generation.device() != VK_NULL_HANDLE &&
               logical_device_generation.queue() != VK_NULL_HANDLE &&
               logical_device_generation.queueFamilyIndex() == physical_device_generation.queueFamilyIndex() &&
               logical_device_generation.queueFamilyIndex() != VK_QUEUE_FAMILY_IGNORED;
    }

    std::optional<VulkanUploadTransferResolutionError> resourceError(
        const VulkanPhysicalDeviceGeneration&    physical_device_generation,
        const VulkanLogicalDeviceGeneration&     logical_device_generation,
        const VulkanUploadSourceDescription&     description,
        const VulkanUploadSourceGeneration&      source_generation,
        const VulkanUploadDestinationGeneration& destination_generation) noexcept
    {
        if (!validPhysical(physical_device_generation))
        {
            return failure(VulkanUploadTransferResolutionCode::InvalidPhysicalDeviceGeneration);
        }
        if (!validLogical(physical_device_generation, logical_device_generation))
        {
            return failure(VulkanUploadTransferResolutionCode::InvalidLogicalDeviceGeneration);
        }
        if (!description.mHandle)
        {
            return failure(VulkanUploadTransferResolutionCode::InvalidDescription);
        }
        if (!source_generation.createdFor(physical_device_generation, logical_device_generation) ||
            !source_generation.matchesDescription(description) || source_generation.byteCount() != VULKAN_UPLOAD_SOURCE_BYTE_COUNT ||
            source_generation.buffer() == VK_NULL_HANDLE || source_generation.contentIdentity() == 0)
        {
            return failure(VulkanUploadTransferResolutionCode::InvalidUploadSourceGeneration);
        }
        if (!destination_generation.createdFor(physical_device_generation, logical_device_generation) ||
            !destination_generation.matchesDescription(description))
        {
            return failure(VulkanUploadTransferResolutionCode::InvalidUploadDestinationGeneration);
        }
        if (source_generation.buffer() == destination_generation.buffer())
        {
            return failure(VulkanUploadTransferResolutionCode::SourceDestinationBufferAlias);
        }
        if (!destination_generation.matchesUploadSource(source_generation) ||
            destination_generation.resourceHandle() != source_generation.resourceHandle() ||
            destination_generation.expectedContentIdentity() != source_generation.contentIdentity() ||
            destination_generation.byteCount() != VULKAN_UPLOAD_SOURCE_BYTE_COUNT || destination_generation.buffer() == VK_NULL_HANDLE ||
            destination_generation.memory() == VK_NULL_HANDLE || !destination_generation.isDeviceLocal())
        {
            return failure(VulkanUploadTransferResolutionCode::InvalidUploadDestinationGeneration);
        }
        if (destination_generation.isResident())
        {
            return failure(VulkanUploadTransferResolutionCode::DestinationAlreadyResident);
        }
        return std::nullopt;
    }

    std::optional<VulkanUploadTransferResolutionError> resolveDispatch(const VulkanPhysicalDeviceGeneration& physical_device_generation,
                                                                       const VulkanLogicalDeviceGeneration&  logical_device_generation,
                                                                       UploadTransferDispatch&               dispatch) noexcept
    {
        const VkDevice device       = logical_device_generation.device();
        dispatch.mGetDeviceProcAddr = resolveInstance<PFN_vkGetDeviceProcAddr>(physical_device_generation.getInstanceProcAddr(),
                                                                               physical_device_generation.instance(),
                                                                               "vkGetDeviceProcAddr");
        if (!dispatch.mGetDeviceProcAddr)
        {
            return failure(VulkanUploadTransferResolutionCode::MissingRequiredCommand, VulkanUploadTransferCommand::GetDeviceProcAddr);
        }

#define LL_RESOLVE_UPLOAD_TRANSFER_DEVICE_COMMAND(member, type, command_name, command_value)       \
    dispatch.member = resolveDevice<type>(dispatch.mGetDeviceProcAddr, device, command_name);      \
    if (!dispatch.member)                                                                          \
    {                                                                                              \
        return failure(VulkanUploadTransferResolutionCode::MissingRequiredCommand, command_value); \
    }

        LL_RESOLVE_UPLOAD_TRANSFER_DEVICE_COMMAND(mCreateCommandPool, PFN_vkCreateCommandPool, "vkCreateCommandPool",
                                                  VulkanUploadTransferCommand::CreateCommandPool)
        LL_RESOLVE_UPLOAD_TRANSFER_DEVICE_COMMAND(mDestroyCommandPool, PFN_vkDestroyCommandPool, "vkDestroyCommandPool",
                                                  VulkanUploadTransferCommand::DestroyCommandPool)
        LL_RESOLVE_UPLOAD_TRANSFER_DEVICE_COMMAND(mAllocateCommandBuffers, PFN_vkAllocateCommandBuffers, "vkAllocateCommandBuffers",
                                                  VulkanUploadTransferCommand::AllocateCommandBuffers)
        LL_RESOLVE_UPLOAD_TRANSFER_DEVICE_COMMAND(mCreateFence, PFN_vkCreateFence, "vkCreateFence",
                                                  VulkanUploadTransferCommand::CreateFence)
        LL_RESOLVE_UPLOAD_TRANSFER_DEVICE_COMMAND(mDestroyFence, PFN_vkDestroyFence, "vkDestroyFence",
                                                  VulkanUploadTransferCommand::DestroyFence)
        LL_RESOLVE_UPLOAD_TRANSFER_DEVICE_COMMAND(mBeginCommandBuffer, PFN_vkBeginCommandBuffer, "vkBeginCommandBuffer",
                                                  VulkanUploadTransferCommand::BeginCommandBuffer)
        LL_RESOLVE_UPLOAD_TRANSFER_DEVICE_COMMAND(mCmdPipelineBarrier, PFN_vkCmdPipelineBarrier, "vkCmdPipelineBarrier",
                                                  VulkanUploadTransferCommand::CmdPipelineBarrier)
        LL_RESOLVE_UPLOAD_TRANSFER_DEVICE_COMMAND(mCmdCopyBuffer, PFN_vkCmdCopyBuffer, "vkCmdCopyBuffer",
                                                  VulkanUploadTransferCommand::CmdCopyBuffer)
        LL_RESOLVE_UPLOAD_TRANSFER_DEVICE_COMMAND(mEndCommandBuffer, PFN_vkEndCommandBuffer, "vkEndCommandBuffer",
                                                  VulkanUploadTransferCommand::EndCommandBuffer)
        LL_RESOLVE_UPLOAD_TRANSFER_DEVICE_COMMAND(mQueueSubmit, PFN_vkQueueSubmit, "vkQueueSubmit",
                                                  VulkanUploadTransferCommand::QueueSubmit)
        LL_RESOLVE_UPLOAD_TRANSFER_DEVICE_COMMAND(mWaitForFences, PFN_vkWaitForFences, "vkWaitForFences",
                                                  VulkanUploadTransferCommand::WaitForFences)

#undef LL_RESOLVE_UPLOAD_TRANSFER_DEVICE_COMMAND

        return std::nullopt;
    }

    void rollBack(const UploadTransferDispatch& dispatch, VkDevice device, VkFence fence, VkCommandPool command_pool) noexcept
    {
        if (fence != VK_NULL_HANDLE)
        {
            dispatch.mDestroyFence(device, fence, nullptr);
        }
        if (command_pool != VK_NULL_HANDLE)
        {
            dispatch.mDestroyCommandPool(device, command_pool, nullptr);
        }
    }

} // namespace

struct VulkanUploadTransferGenerationFactory
{
    static VulkanUploadTransferGeneration create(const VulkanPhysicalDeviceGeneration& physical_device_generation,
                                                 const VulkanLogicalDeviceGeneration&  logical_device_generation,
                                                 const VulkanUploadSourceDescription&  description,
                                                 const VulkanUploadSourceGeneration&   source_generation,
                                                 VulkanUploadDestinationGeneration&    destination_generation,
                                                 VkCommandPool                         command_pool,
                                                 VkCommandBuffer                       command_buffer,
                                                 VkFence                               fence,
                                                 const UploadTransferDispatch&         dispatch) noexcept
    {
        return VulkanUploadTransferGeneration(physical_device_generation,
                                              logical_device_generation,
                                              description,
                                              source_generation,
                                              destination_generation,
                                              command_pool,
                                              command_buffer,
                                              fence,
                                              dispatch.mDestroyCommandPool,
                                              dispatch.mDestroyFence,
                                              dispatch.mBeginCommandBuffer,
                                              dispatch.mCmdPipelineBarrier,
                                              dispatch.mCmdCopyBuffer,
                                              dispatch.mEndCommandBuffer,
                                              dispatch.mQueueSubmit,
                                              dispatch.mWaitForFences);
    }
};

VulkanUploadTransferGeneration::VulkanUploadTransferGeneration(const VulkanPhysicalDeviceGeneration& physical_device_generation,
                                                               const VulkanLogicalDeviceGeneration&  logical_device_generation,
                                                               const VulkanUploadSourceDescription&  description,
                                                               const VulkanUploadSourceGeneration&   source_generation,
                                                               VulkanUploadDestinationGeneration&    destination_generation,
                                                               VkCommandPool                         command_pool,
                                                               VkCommandBuffer                       command_buffer,
                                                               VkFence                               fence,
                                                               PFN_vkDestroyCommandPool              destroy_command_pool,
                                                               PFN_vkDestroyFence                    destroy_fence,
                                                               PFN_vkBeginCommandBuffer              begin_command_buffer,
                                                               PFN_vkCmdPipelineBarrier              cmd_pipeline_barrier,
                                                               PFN_vkCmdCopyBuffer                   cmd_copy_buffer,
                                                               PFN_vkEndCommandBuffer                end_command_buffer,
                                                               PFN_vkQueueSubmit                     queue_submit,
                                                               PFN_vkWaitForFences                   wait_for_fences) noexcept :
    mPhysicalDeviceGeneration(&physical_device_generation),
    mLogicalDeviceGeneration(&logical_device_generation),
    mSourceGeneration(&source_generation),
    mDestinationGeneration(&destination_generation),
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
    mContentIdentity(source_generation.contentIdentity()),
    mSourceBuffer(source_generation.buffer()),
    mDestinationBuffer(destination_generation.buffer()),
    mCommandPool(command_pool),
    mCommandBuffer(command_buffer),
    mFence(fence),
    mDestroyCommandPool(destroy_command_pool),
    mDestroyFence(destroy_fence),
    mBeginCommandBuffer(begin_command_buffer),
    mCmdPipelineBarrier(cmd_pipeline_barrier),
    mCmdCopyBuffer(cmd_copy_buffer),
    mEndCommandBuffer(end_command_buffer),
    mQueueSubmit(queue_submit),
    mWaitForFences(wait_for_fences),
    mDisposition(VulkanUploadTransferDisposition::Ready)
{
}

VulkanUploadTransferGeneration::~VulkanUploadTransferGeneration() noexcept
{
    reset();
}

VulkanUploadTransferGeneration::VulkanUploadTransferGeneration(VulkanUploadTransferGeneration&& other) noexcept :
    mPhysicalDeviceGeneration(std::exchange(other.mPhysicalDeviceGeneration, nullptr)),
    mLogicalDeviceGeneration(std::exchange(other.mLogicalDeviceGeneration, nullptr)),
    mSourceGeneration(std::exchange(other.mSourceGeneration, nullptr)),
    mDestinationGeneration(std::exchange(other.mDestinationGeneration, nullptr)),
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
    mSourceBuffer(std::exchange(other.mSourceBuffer, VK_NULL_HANDLE)),
    mDestinationBuffer(std::exchange(other.mDestinationBuffer, VK_NULL_HANDLE)),
    mCommandPool(std::exchange(other.mCommandPool, VK_NULL_HANDLE)),
    mCommandBuffer(std::exchange(other.mCommandBuffer, VK_NULL_HANDLE)),
    mFence(std::exchange(other.mFence, VK_NULL_HANDLE)),
    mDestroyCommandPool(std::exchange(other.mDestroyCommandPool, nullptr)),
    mDestroyFence(std::exchange(other.mDestroyFence, nullptr)),
    mBeginCommandBuffer(std::exchange(other.mBeginCommandBuffer, nullptr)),
    mCmdPipelineBarrier(std::exchange(other.mCmdPipelineBarrier, nullptr)),
    mCmdCopyBuffer(std::exchange(other.mCmdCopyBuffer, nullptr)),
    mEndCommandBuffer(std::exchange(other.mEndCommandBuffer, nullptr)),
    mQueueSubmit(std::exchange(other.mQueueSubmit, nullptr)),
    mWaitForFences(std::exchange(other.mWaitForFences, nullptr)),
    mDisposition(std::exchange(other.mDisposition, VulkanUploadTransferDisposition::ResetRequired)),
    mSubmissionAttemptCount(std::exchange(other.mSubmissionAttemptCount, 0)),
    mCompletionWaitCount(std::exchange(other.mCompletionWaitCount, 0)),
    mSubmitReportedDeviceLost(std::exchange(other.mSubmitReportedDeviceLost, false))
{
}

bool VulkanUploadTransferGeneration::matchesDescription(const VulkanUploadSourceDescription& description) const noexcept
{
    return mCommandPool != VK_NULL_HANDLE && mCommandBuffer != VK_NULL_HANDLE && mFence != VK_NULL_HANDLE && mDescription == description &&
           mDescription.mHandle && mContentIdentity != 0 && mSourceBuffer != VK_NULL_HANDLE && mDestinationBuffer != VK_NULL_HANDLE &&
           mSourceBuffer != mDestinationBuffer;
}

bool VulkanUploadTransferGeneration::createdFor(const VulkanPhysicalDeviceGeneration& physical_device_generation,
                                                const VulkanLogicalDeviceGeneration&  logical_device_generation) const noexcept
{
    return mCommandPool != VK_NULL_HANDLE && mCommandBuffer != VK_NULL_HANDLE && mFence != VK_NULL_HANDLE && mDescription.mHandle &&
           mContentIdentity != 0 && mSourceBuffer != VK_NULL_HANDLE && mDestinationBuffer != VK_NULL_HANDLE &&
           mSourceBuffer != mDestinationBuffer && mPhysicalDeviceGeneration == &physical_device_generation &&
           mLogicalDeviceGeneration == &logical_device_generation && validPhysical(physical_device_generation) &&
           validLogical(physical_device_generation, logical_device_generation) &&
           mGetInstanceProcAddr == physical_device_generation.getInstanceProcAddr() && mInstance == physical_device_generation.instance() &&
           mSurface == physical_device_generation.surface() && mPhysicalDevice == physical_device_generation.physicalDevice() &&
           mPhysicalDeviceIndex == physical_device_generation.physicalDeviceIndex() && mDevice == logical_device_generation.device() &&
           mQueue == logical_device_generation.queue() && mQueueFamilyIndex == logical_device_generation.queueFamilyIndex() &&
           mQueueIndex == logical_device_generation.queueIndex();
}

bool VulkanUploadTransferGeneration::retainsUploadSourceGeneration(const VulkanUploadSourceGeneration& source_generation) const noexcept
{
    return (mDisposition == VulkanUploadTransferDisposition::Ready || mDisposition == VulkanUploadTransferDisposition::Pending) &&
           mSourceGeneration == &source_generation;
}

bool VulkanUploadTransferGeneration::retainsUploadDestinationGeneration(
    const VulkanUploadDestinationGeneration& destination_generation) const noexcept
{
    return (mDisposition == VulkanUploadTransferDisposition::Ready || mDisposition == VulkanUploadTransferDisposition::Pending) &&
           mDestinationGeneration == &destination_generation;
}

bool VulkanUploadTransferGeneration::retainsUploadResources(const VulkanUploadSourceGeneration&      source_generation,
                                                            const VulkanUploadDestinationGeneration& destination_generation) const noexcept
{
    return retainsUploadSourceGeneration(source_generation) && retainsUploadDestinationGeneration(destination_generation);
}

std::optional<VulkanUploadTransferOperationCode> VulkanUploadTransferGeneration::retainedResourceError() const noexcept
{
    if (!mPhysicalDeviceGeneration || !validPhysical(*mPhysicalDeviceGeneration) ||
        mGetInstanceProcAddr != mPhysicalDeviceGeneration->getInstanceProcAddr() || mInstance != mPhysicalDeviceGeneration->instance() ||
        mSurface != mPhysicalDeviceGeneration->surface() || mPhysicalDevice != mPhysicalDeviceGeneration->physicalDevice() ||
        mPhysicalDeviceIndex != mPhysicalDeviceGeneration->physicalDeviceIndex())
    {
        return VulkanUploadTransferOperationCode::InvalidPhysicalDeviceGeneration;
    }
    if (!mLogicalDeviceGeneration || !validLogical(*mPhysicalDeviceGeneration, *mLogicalDeviceGeneration) ||
        mDevice != mLogicalDeviceGeneration->device() || mQueue != mLogicalDeviceGeneration->queue() ||
        mQueueFamilyIndex != mLogicalDeviceGeneration->queueFamilyIndex() || mQueueIndex != mLogicalDeviceGeneration->queueIndex())
    {
        return VulkanUploadTransferOperationCode::InvalidLogicalDeviceGeneration;
    }
    if (!mSourceGeneration || !mSourceGeneration->createdFor(*mPhysicalDeviceGeneration, *mLogicalDeviceGeneration) ||
        !mSourceGeneration->matchesDescription(mDescription) || mSourceGeneration->resourceHandle() != mDescription.mHandle ||
        mSourceGeneration->contentIdentity() != mContentIdentity || mSourceGeneration->buffer() != mSourceBuffer ||
        mSourceGeneration->byteCount() != VULKAN_UPLOAD_SOURCE_BYTE_COUNT)
    {
        return VulkanUploadTransferOperationCode::InvalidUploadSourceGeneration;
    }
    if (!mDestinationGeneration || !mDestinationGeneration->createdFor(*mPhysicalDeviceGeneration, *mLogicalDeviceGeneration) ||
        !mDestinationGeneration->matchesDescription(mDescription) || !mDestinationGeneration->matchesUploadSource(*mSourceGeneration) ||
        mDestinationGeneration->resourceHandle() != mDescription.mHandle ||
        mDestinationGeneration->expectedContentIdentity() != mContentIdentity || mDestinationGeneration->buffer() != mDestinationBuffer ||
        mDestinationGeneration->byteCount() != VULKAN_UPLOAD_SOURCE_BYTE_COUNT || !mDestinationGeneration->isDeviceLocal() ||
        mDestinationGeneration->isResident() || mDestinationBuffer == mSourceBuffer)
    {
        return VulkanUploadTransferOperationCode::InvalidUploadDestinationGeneration;
    }
    return std::nullopt;
}

void VulkanUploadTransferGeneration::releaseTemporaryResources() noexcept
{
    mSourceGeneration      = nullptr;
    mDestinationGeneration = nullptr;
}

VulkanUploadTransferOperationResult VulkanUploadTransferGeneration::finishCompletionWait(VkResult result) noexcept
{
    if (result == VK_ERROR_DEVICE_LOST)
    {
        mSubmitReportedDeviceLost = false;
        mDisposition              = VulkanUploadTransferDisposition::DeviceLost;
        releaseTemporaryResources();
        return operationFailure(VulkanUploadTransferOperationCode::CommandFailure,
                                mDisposition,
                                VulkanUploadTransferCommand::WaitForFences,
                                result);
    }
    if (result != VK_SUCCESS)
    {
        return operationFailure(VulkanUploadTransferOperationCode::CommandFailure,
                                mDisposition,
                                VulkanUploadTransferCommand::WaitForFences,
                                result);
    }
    if (mSubmitReportedDeviceLost)
    {
        mSubmitReportedDeviceLost = false;
        mDisposition              = VulkanUploadTransferDisposition::DeviceLost;
        releaseTemporaryResources();
        return operationFailure(VulkanUploadTransferOperationCode::CommandFailure,
                                mDisposition,
                                VulkanUploadTransferCommand::QueueSubmit,
                                VK_ERROR_DEVICE_LOST);
    }
    if (const auto error = retainedResourceError())
    {
        mDisposition = VulkanUploadTransferDisposition::ResetRequired;
        return operationFailure(*error, mDisposition);
    }
    if (!mDestinationGeneration->markResident(mContentIdentity))
    {
        mDisposition = VulkanUploadTransferDisposition::ResetRequired;
        return operationFailure(VulkanUploadTransferOperationCode::DestinationPublicationFailure, mDisposition);
    }

    mDisposition = VulkanUploadTransferDisposition::Complete;
    releaseTemporaryResources();
    return mDisposition;
}

VulkanUploadTransferOperationResult VulkanUploadTransferGeneration::execute() noexcept
{
    if (mDisposition != VulkanUploadTransferDisposition::Ready)
    {
        return operationFailure(VulkanUploadTransferOperationCode::InvalidDisposition, mDisposition);
    }
    if (const auto error = retainedResourceError())
    {
        mDisposition = VulkanUploadTransferDisposition::ResetRequired;
        return operationFailure(*error, mDisposition);
    }

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VkResult result  = mBeginCommandBuffer(mCommandBuffer, &begin_info);
    if (result != VK_SUCCESS)
    {
        mDisposition =
            result == VK_ERROR_DEVICE_LOST ? VulkanUploadTransferDisposition::DeviceLost : VulkanUploadTransferDisposition::ResetRequired;
        if (mDisposition == VulkanUploadTransferDisposition::DeviceLost)
        {
            releaseTemporaryResources();
        }
        return operationFailure(VulkanUploadTransferOperationCode::CommandFailure,
                                mDisposition,
                                VulkanUploadTransferCommand::BeginCommandBuffer,
                                result);
    }
    if (const auto error = retainedResourceError())
    {
        mDisposition = VulkanUploadTransferDisposition::ResetRequired;
        return operationFailure(*error, mDisposition);
    }

    VkBufferMemoryBarrier source_barrier{};
    source_barrier.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    source_barrier.srcAccessMask       = VK_ACCESS_HOST_WRITE_BIT;
    source_barrier.dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
    source_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    source_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    source_barrier.buffer              = mSourceBuffer;
    source_barrier.offset              = 0;
    source_barrier.size                = VULKAN_UPLOAD_SOURCE_BYTE_COUNT;
    mCmdPipelineBarrier(mCommandBuffer,
                        VK_PIPELINE_STAGE_HOST_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT,
                        0,
                        0,
                        nullptr,
                        1,
                        &source_barrier,
                        0,
                        nullptr);
    if (const auto error = retainedResourceError())
    {
        mDisposition = VulkanUploadTransferDisposition::ResetRequired;
        return operationFailure(*error, mDisposition);
    }

    VkBufferCopy copy{};
    copy.srcOffset = 0;
    copy.dstOffset = 0;
    copy.size      = VULKAN_UPLOAD_SOURCE_BYTE_COUNT;
    mCmdCopyBuffer(mCommandBuffer, mSourceBuffer, mDestinationBuffer, 1, &copy);
    if (const auto error = retainedResourceError())
    {
        mDisposition = VulkanUploadTransferDisposition::ResetRequired;
        return operationFailure(*error, mDisposition);
    }

    VkBufferMemoryBarrier destination_barrier{};
    destination_barrier.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    destination_barrier.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    destination_barrier.dstAccessMask       = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    destination_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    destination_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    destination_barrier.buffer              = mDestinationBuffer;
    destination_barrier.offset              = 0;
    destination_barrier.size                = VULKAN_UPLOAD_SOURCE_BYTE_COUNT;
    mCmdPipelineBarrier(mCommandBuffer,
                        VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                        0,
                        0,
                        nullptr,
                        1,
                        &destination_barrier,
                        0,
                        nullptr);
    if (const auto error = retainedResourceError())
    {
        mDisposition = VulkanUploadTransferDisposition::ResetRequired;
        return operationFailure(*error, mDisposition);
    }

    result = mEndCommandBuffer(mCommandBuffer);
    if (result != VK_SUCCESS)
    {
        mDisposition =
            result == VK_ERROR_DEVICE_LOST ? VulkanUploadTransferDisposition::DeviceLost : VulkanUploadTransferDisposition::ResetRequired;
        if (mDisposition == VulkanUploadTransferDisposition::DeviceLost)
        {
            releaseTemporaryResources();
        }
        return operationFailure(VulkanUploadTransferOperationCode::CommandFailure,
                                mDisposition,
                                VulkanUploadTransferCommand::EndCommandBuffer,
                                result);
    }
    if (const auto error = retainedResourceError())
    {
        mDisposition = VulkanUploadTransferDisposition::ResetRequired;
        return operationFailure(*error, mDisposition);
    }

    VkSubmitInfo submit_info{};
    submit_info.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers    = &mCommandBuffer;

    ++mSubmissionAttemptCount;
    result = mQueueSubmit(mQueue, 1, &submit_info, mFence);
    if (result != VK_SUCCESS && result != VK_ERROR_DEVICE_LOST)
    {
        mDisposition = VulkanUploadTransferDisposition::ResetRequired;
        return operationFailure(VulkanUploadTransferOperationCode::CommandFailure,
                                mDisposition,
                                VulkanUploadTransferCommand::QueueSubmit,
                                result);
    }

    mSubmitReportedDeviceLost = result == VK_ERROR_DEVICE_LOST;
    mDisposition              = VulkanUploadTransferDisposition::Pending;
    ++mCompletionWaitCount;
    result = mWaitForFences(mDevice, 1, &mFence, VK_TRUE, std::numeric_limits<std::uint64_t>::max());
    return finishCompletionWait(result);
}

VulkanUploadTransferOperationResult VulkanUploadTransferGeneration::retryCompletion() noexcept
{
    if (mDisposition != VulkanUploadTransferDisposition::Pending)
    {
        return operationFailure(VulkanUploadTransferOperationCode::InvalidDisposition, mDisposition);
    }

    ++mCompletionWaitCount;
    const VkResult result = mWaitForFences(mDevice, 1, &mFence, VK_TRUE, std::numeric_limits<std::uint64_t>::max());
    return finishCompletionWait(result);
}

bool VulkanUploadTransferGeneration::reset() noexcept
{
    if (mDisposition == VulkanUploadTransferDisposition::Pending)
    {
        return false;
    }

    if (mFence != VK_NULL_HANDLE && mDestroyFence)
    {
        mDestroyFence(mDevice, mFence, nullptr);
    }
    mFence = VK_NULL_HANDLE;
    if (mCommandPool != VK_NULL_HANDLE && mDestroyCommandPool)
    {
        mDestroyCommandPool(mDevice, mCommandPool, nullptr);
    }
    mCommandPool   = VK_NULL_HANDLE;
    mCommandBuffer = VK_NULL_HANDLE;

    mPhysicalDeviceGeneration = nullptr;
    mLogicalDeviceGeneration  = nullptr;
    releaseTemporaryResources();
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
    mSourceBuffer             = VK_NULL_HANDLE;
    mDestinationBuffer        = VK_NULL_HANDLE;
    mDestroyCommandPool       = nullptr;
    mDestroyFence             = nullptr;
    mBeginCommandBuffer       = nullptr;
    mCmdPipelineBarrier       = nullptr;
    mCmdCopyBuffer            = nullptr;
    mEndCommandBuffer         = nullptr;
    mQueueSubmit              = nullptr;
    mWaitForFences            = nullptr;
    mDisposition              = VulkanUploadTransferDisposition::ResetRequired;
    mSubmissionAttemptCount   = 0;
    mCompletionWaitCount      = 0;
    mSubmitReportedDeviceLost = false;
    return true;
}

VulkanUploadTransferResolutionResult resolveVulkanUploadTransferGeneration(
    const VulkanPhysicalDeviceGeneration& physical_device_generation,
    const VulkanLogicalDeviceGeneration&  logical_device_generation,
    const VulkanUploadSourceDescription&  description,
    const VulkanUploadSourceGeneration&   source_generation,
    VulkanUploadDestinationGeneration&    destination_generation) noexcept
{
    const VulkanUploadSourceDescription owned_description = description;
    if (auto error = resourceError(physical_device_generation,
                                   logical_device_generation,
                                   owned_description,
                                   source_generation,
                                   destination_generation))
    {
        return *error;
    }

    UploadTransferDispatch dispatch;
    if (auto error = resolveDispatch(physical_device_generation, logical_device_generation, dispatch))
    {
        return *error;
    }
    if (auto error = resourceError(physical_device_generation,
                                   logical_device_generation,
                                   owned_description,
                                   source_generation,
                                   destination_generation))
    {
        return *error;
    }

    const VkDevice      device             = logical_device_generation.device();
    const std::uint32_t queue_family_index = logical_device_generation.queueFamilyIndex();

    VkCommandPoolCreateInfo command_pool_info{};
    command_pool_info.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    command_pool_info.flags            = 0;
    command_pool_info.queueFamilyIndex = queue_family_index;

    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkResult      result       = dispatch.mCreateCommandPool(device, &command_pool_info, nullptr, &command_pool);
    if (result != VK_SUCCESS)
    {
        return failure(VulkanUploadTransferResolutionCode::CommandPoolCreationFailure,
                       VulkanUploadTransferCommand::CreateCommandPool,
                       result);
    }
    if (command_pool == VK_NULL_HANDLE)
    {
        return failure(VulkanUploadTransferResolutionCode::NullCommandPoolOnSuccess, VulkanUploadTransferCommand::CreateCommandPool);
    }
    if (auto error = resourceError(physical_device_generation,
                                   logical_device_generation,
                                   owned_description,
                                   source_generation,
                                   destination_generation))
    {
        rollBack(dispatch, device, VK_NULL_HANDLE, command_pool);
        return *error;
    }

    VkCommandBufferAllocateInfo command_buffer_info{};
    command_buffer_info.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command_buffer_info.commandPool        = command_pool;
    command_buffer_info.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_buffer_info.commandBufferCount = 1;

    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    result                         = dispatch.mAllocateCommandBuffers(device, &command_buffer_info, &command_buffer);
    if (result != VK_SUCCESS)
    {
        rollBack(dispatch, device, VK_NULL_HANDLE, command_pool);
        return failure(VulkanUploadTransferResolutionCode::CommandBufferAllocationFailure,
                       VulkanUploadTransferCommand::AllocateCommandBuffers,
                       result);
    }
    if (command_buffer == VK_NULL_HANDLE)
    {
        rollBack(dispatch, device, VK_NULL_HANDLE, command_pool);
        return failure(VulkanUploadTransferResolutionCode::NullCommandBufferOnSuccess, VulkanUploadTransferCommand::AllocateCommandBuffers);
    }
    if (auto error = resourceError(physical_device_generation,
                                   logical_device_generation,
                                   owned_description,
                                   source_generation,
                                   destination_generation))
    {
        rollBack(dispatch, device, VK_NULL_HANDLE, command_pool);
        return *error;
    }

    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = 0;

    VkFence fence = VK_NULL_HANDLE;
    result        = dispatch.mCreateFence(device, &fence_info, nullptr, &fence);
    if (result != VK_SUCCESS)
    {
        rollBack(dispatch, device, VK_NULL_HANDLE, command_pool);
        return failure(VulkanUploadTransferResolutionCode::FenceCreationFailure, VulkanUploadTransferCommand::CreateFence, result);
    }
    if (fence == VK_NULL_HANDLE)
    {
        rollBack(dispatch, device, VK_NULL_HANDLE, command_pool);
        return failure(VulkanUploadTransferResolutionCode::NullFenceOnSuccess, VulkanUploadTransferCommand::CreateFence);
    }
    if (auto error = resourceError(physical_device_generation,
                                   logical_device_generation,
                                   owned_description,
                                   source_generation,
                                   destination_generation))
    {
        rollBack(dispatch, device, fence, command_pool);
        return *error;
    }

    return VulkanUploadTransferGenerationFactory::create(physical_device_generation,
                                                         logical_device_generation,
                                                         owned_description,
                                                         source_generation,
                                                         destination_generation,
                                                         command_pool,
                                                         command_buffer,
                                                         fence,
                                                         dispatch);
}

} // namespace LLRenderVulkan
