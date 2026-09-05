/**
 * @file llrendervulkanuploadtransfer.h
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

#ifndef LL_LLRENDERVULKANUPLOADTRANSFER_H
#define LL_LLRENDERVULKANUPLOADTRANSFER_H

#include "llrendervulkanuploadsource.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>
#include <variant>

namespace LLRenderVulkan
{

class VulkanUploadDestinationGeneration;

enum class VulkanUploadTransferCommand : std::uint8_t
{
    GetDeviceProcAddr,
    CreateCommandPool,
    DestroyCommandPool,
    AllocateCommandBuffers,
    CreateFence,
    DestroyFence,
    BeginCommandBuffer,
    CmdPipelineBarrier,
    CmdCopyBuffer,
    EndCommandBuffer,
    QueueSubmit,
    WaitForFences
};

enum class VulkanUploadTransferResolutionCode : std::uint8_t
{
    InvalidPhysicalDeviceGeneration,
    InvalidLogicalDeviceGeneration,
    InvalidDescription,
    InvalidUploadSourceGeneration,
    InvalidUploadDestinationGeneration,
    SourceDestinationBufferAlias,
    DestinationAlreadyResident,
    MissingRequiredCommand,
    CommandPoolCreationFailure,
    NullCommandPoolOnSuccess,
    CommandBufferAllocationFailure,
    NullCommandBufferOnSuccess,
    FenceCreationFailure,
    NullFenceOnSuccess
};

struct VulkanUploadTransferResolutionError
{
    VulkanUploadTransferResolutionCode         mCode = VulkanUploadTransferResolutionCode::InvalidPhysicalDeviceGeneration;
    std::optional<VulkanUploadTransferCommand> mCommand;
    VkResult                                   mResult = VK_SUCCESS;

    friend constexpr bool operator==(const VulkanUploadTransferResolutionError&, const VulkanUploadTransferResolutionError&) = default;
};

enum class VulkanUploadTransferDisposition : std::uint8_t
{
    Ready,
    ResetRequired,
    Pending,
    Complete,
    DeviceLost
};

enum class VulkanUploadTransferOperationCode : std::uint8_t
{
    InvalidPhysicalDeviceGeneration,
    InvalidLogicalDeviceGeneration,
    InvalidUploadSourceGeneration,
    InvalidUploadDestinationGeneration,
    InvalidDisposition,
    CommandFailure,
    DestinationPublicationFailure
};

struct VulkanUploadTransferOperationError
{
    VulkanUploadTransferOperationCode          mCode = VulkanUploadTransferOperationCode::InvalidPhysicalDeviceGeneration;
    std::optional<VulkanUploadTransferCommand> mCommand;
    VkResult                                   mResult      = VK_SUCCESS;
    VulkanUploadTransferDisposition            mDisposition = VulkanUploadTransferDisposition::ResetRequired;

    friend constexpr bool operator==(const VulkanUploadTransferOperationError&, const VulkanUploadTransferOperationError&) = default;
};

using VulkanUploadTransferOperationResult = std::variant<VulkanUploadTransferOperationError, VulkanUploadTransferDisposition>;

// This one-shot owner records one source-to-destination copy on the retained
// unified graphics and presentation queue. The exact physical and logical
// device generations must outlive it until successful reset or destruction.
// The exact source and destination must outlive it while it reports Ready or
// Pending. Host access and all access to that queue are externally serialized.
// reset() refuses Pending because the command buffer, fence, and both buffers
// may still be in use. Destroying this generation or either retained resource
// while Pending violates the caller contract.
class VulkanUploadTransferGeneration
{
public:
    ~VulkanUploadTransferGeneration() noexcept;

    VulkanUploadTransferGeneration(const VulkanUploadTransferGeneration&)            = delete;
    VulkanUploadTransferGeneration& operator=(const VulkanUploadTransferGeneration&) = delete;
    VulkanUploadTransferGeneration(VulkanUploadTransferGeneration&& other) noexcept;
    VulkanUploadTransferGeneration& operator=(VulkanUploadTransferGeneration&&) = delete;

    LLRenderContract::BufferHandle  resourceHandle() const noexcept { return mDescription.mHandle; }
    std::uint64_t                   contentIdentity() const noexcept { return mContentIdentity; }
    VkBuffer                        sourceBuffer() const noexcept { return mSourceBuffer; }
    VkBuffer                        destinationBuffer() const noexcept { return mDestinationBuffer; }
    VkQueue                         queue() const noexcept { return mQueue; }
    std::uint32_t                   queueFamilyIndex() const noexcept { return mQueueFamilyIndex; }
    std::uint32_t                   queueIndex() const noexcept { return mQueueIndex; }
    VkCommandPool                   commandPool() const noexcept { return mCommandPool; }
    VkCommandBuffer                 commandBuffer() const noexcept { return mCommandBuffer; }
    VkFence                         fence() const noexcept { return mFence; }
    std::uint32_t                   submissionAttemptCount() const noexcept { return mSubmissionAttemptCount; }
    std::uint32_t                   completionWaitCount() const noexcept { return mCompletionWaitCount; }
    VulkanUploadTransferDisposition disposition() const noexcept { return mDisposition; }

    bool matchesDescription(const VulkanUploadSourceDescription& description) const noexcept;
    bool createdFor(const VulkanPhysicalDeviceGeneration& physical_device_generation,
                    const VulkanLogicalDeviceGeneration&  logical_device_generation) const noexcept;
    bool retainsUploadSourceGeneration(const VulkanUploadSourceGeneration& source_generation) const noexcept;
    bool retainsUploadDestinationGeneration(const VulkanUploadDestinationGeneration& destination_generation) const noexcept;
    bool retainsUploadResources(const VulkanUploadSourceGeneration&      source_generation,
                                const VulkanUploadDestinationGeneration& destination_generation) const noexcept;

    VulkanUploadTransferOperationResult execute() noexcept;
    // Pending retries issue only the retained fence wait. They never record or
    // submit again.
    VulkanUploadTransferOperationResult retryCompletion() noexcept;

    // Returns false without changing ownership while queue use may be pending.
    bool reset() noexcept;

private:
    friend struct VulkanUploadTransferGenerationFactory;

    VulkanUploadTransferGeneration(const VulkanPhysicalDeviceGeneration& physical_device_generation,
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
                                   PFN_vkWaitForFences                   wait_for_fences) noexcept;

    VulkanUploadTransferOperationResult              finishCompletionWait(VkResult result) noexcept;
    std::optional<VulkanUploadTransferOperationCode> retainedResourceError() const noexcept;
    void                                             releaseTemporaryResources() noexcept;

    const VulkanPhysicalDeviceGeneration* mPhysicalDeviceGeneration = nullptr;
    const VulkanLogicalDeviceGeneration*  mLogicalDeviceGeneration  = nullptr;
    const VulkanUploadSourceGeneration*   mSourceGeneration         = nullptr;
    VulkanUploadDestinationGeneration*    mDestinationGeneration    = nullptr;
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
    std::uint64_t                         mContentIdentity          = 0;
    VkBuffer                              mSourceBuffer             = VK_NULL_HANDLE;
    VkBuffer                              mDestinationBuffer        = VK_NULL_HANDLE;
    VkCommandPool                         mCommandPool              = VK_NULL_HANDLE;
    VkCommandBuffer                       mCommandBuffer            = VK_NULL_HANDLE;
    VkFence                               mFence                    = VK_NULL_HANDLE;
    PFN_vkDestroyCommandPool              mDestroyCommandPool       = nullptr;
    PFN_vkDestroyFence                    mDestroyFence             = nullptr;
    PFN_vkBeginCommandBuffer              mBeginCommandBuffer       = nullptr;
    PFN_vkCmdPipelineBarrier              mCmdPipelineBarrier       = nullptr;
    PFN_vkCmdCopyBuffer                   mCmdCopyBuffer            = nullptr;
    PFN_vkEndCommandBuffer                mEndCommandBuffer         = nullptr;
    PFN_vkQueueSubmit                     mQueueSubmit              = nullptr;
    PFN_vkWaitForFences                   mWaitForFences            = nullptr;
    VulkanUploadTransferDisposition       mDisposition              = VulkanUploadTransferDisposition::ResetRequired;
    std::uint32_t                         mSubmissionAttemptCount   = 0;
    std::uint32_t                         mCompletionWaitCount      = 0;
    bool                                  mSubmitReportedDeviceLost = false;
};

using VulkanUploadTransferResolutionResult = std::variant<VulkanUploadTransferResolutionError, VulkanUploadTransferGeneration>;

VulkanUploadTransferResolutionResult resolveVulkanUploadTransferGeneration(
    const VulkanPhysicalDeviceGeneration& physical_device_generation,
    const VulkanLogicalDeviceGeneration&  logical_device_generation,
    const VulkanUploadSourceDescription&  description,
    const VulkanUploadSourceGeneration&   source_generation,
    VulkanUploadDestinationGeneration&    destination_generation) noexcept;

} // namespace LLRenderVulkan

#endif // LL_LLRENDERVULKANUPLOADTRANSFER_H
