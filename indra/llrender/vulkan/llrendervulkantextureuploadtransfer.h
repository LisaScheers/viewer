/**
 * @file llrendervulkantextureuploadtransfer.h
 * @brief Loader-neutral ownership of one Vulkan texture upload transfer.
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

#ifndef LL_LLRENDERVULKANTEXTUREUPLOADTRANSFER_H
#define LL_LLRENDERVULKANTEXTUREUPLOADTRANSFER_H

#include "llrendervulkantextureuploaddestination.h"
#include "llrendervulkantextureuploadsource.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>
#include <variant>

namespace LLRenderVulkan
{

class VulkanTextureUploadDestinationGeneration;

enum class VulkanTextureUploadTransferCommand : std::uint8_t
{
    GetDeviceProcAddr,
    CreateCommandPool,
    DestroyCommandPool,
    AllocateCommandBuffers,
    CreateFence,
    DestroyFence,
    BeginCommandBuffer,
    CmdPipelineBarrier,
    CmdCopyBufferToImage,
    CmdBlitImage,
    EndCommandBuffer,
    QueueSubmit,
    WaitForFences
};

enum class VulkanTextureUploadTransferResolutionCode : std::uint8_t
{
    InvalidPhysicalDeviceGeneration,
    InvalidLogicalDeviceGeneration,
    InvalidDescription,
    InvalidTextureUploadSourceGeneration,
    InvalidTextureUploadDestinationGeneration,
    DestinationAlreadyResident,
    MissingRequiredCommand,
    CommandPoolCreationFailure,
    NullCommandPoolOnSuccess,
    CommandBufferAllocationFailure,
    NullCommandBufferOnSuccess,
    FenceCreationFailure,
    NullFenceOnSuccess
};

struct VulkanTextureUploadTransferResolutionError
{
    VulkanTextureUploadTransferResolutionCode         mCode = VulkanTextureUploadTransferResolutionCode::InvalidPhysicalDeviceGeneration;
    std::optional<VulkanTextureUploadTransferCommand> mCommand;
    VkResult                                          mResult = VK_SUCCESS;

    friend constexpr bool operator==(const VulkanTextureUploadTransferResolutionError&,
                                     const VulkanTextureUploadTransferResolutionError&) = default;
};

enum class VulkanTextureUploadTransferDisposition : std::uint8_t
{
    Ready,
    ResetRequired,
    Pending,
    Complete,
    DeviceLost
};

enum class VulkanTextureUploadTransferOperationCode : std::uint8_t
{
    InvalidPhysicalDeviceGeneration,
    InvalidLogicalDeviceGeneration,
    InvalidTextureUploadSourceGeneration,
    InvalidTextureUploadDestinationGeneration,
    InvalidDisposition,
    InvalidTimeout,
    CommandFailure,
    DestinationPublicationFailure
};

struct VulkanTextureUploadTransferOperationError
{
    VulkanTextureUploadTransferOperationCode          mCode = VulkanTextureUploadTransferOperationCode::InvalidPhysicalDeviceGeneration;
    std::optional<VulkanTextureUploadTransferCommand> mCommand;
    VkResult                                          mResult      = VK_SUCCESS;
    VulkanTextureUploadTransferDisposition            mDisposition = VulkanTextureUploadTransferDisposition::ResetRequired;

    friend constexpr bool operator==(const VulkanTextureUploadTransferOperationError&,
                                     const VulkanTextureUploadTransferOperationError&) = default;
};

using VulkanTextureUploadTransferOperationResult =
    std::variant<VulkanTextureUploadTransferOperationError, VulkanTextureUploadTransferDisposition>;

// A one-shot transfer retains the immutable source and unpublished destination
// while Ready or Pending. The exact physical, logical, source, and destination
// generations must outlive that retention and any call that authenticates them.
// The native VkDevice must outlive this owner's reset or destruction in every
// disposition because it owns device children until then. Direct reset of the
// dependencies, queue access, and host access are externally synchronized.
// Pending reset is refused because the queue may still use the resources.
// Destroying this owner or its aggregate while Pending violates the caller
// precondition; destruction never waits for the fence.
class VulkanTextureUploadTransferGeneration
{
public:
    ~VulkanTextureUploadTransferGeneration() noexcept;

    VulkanTextureUploadTransferGeneration(const VulkanTextureUploadTransferGeneration&)            = delete;
    VulkanTextureUploadTransferGeneration& operator=(const VulkanTextureUploadTransferGeneration&) = delete;
    VulkanTextureUploadTransferGeneration(VulkanTextureUploadTransferGeneration&& other) noexcept;
    VulkanTextureUploadTransferGeneration& operator=(VulkanTextureUploadTransferGeneration&&) = delete;

    LLRenderContract::ImageHandle          resourceHandle() const noexcept { return mSourceDescription.mHandle; }
    std::uint64_t                          expectedRevision() const noexcept { return mSourceDescription.mExpectedRevision; }
    std::uint64_t                          contentIdentity() const noexcept { return mContentIdentity; }
    VkBuffer                               sourceBuffer() const noexcept { return mSourceBuffer; }
    VkImage                                destinationImage() const noexcept { return mDestinationImage; }
    VkQueue                                queue() const noexcept { return mQueue; }
    std::uint32_t                          queueFamilyIndex() const noexcept { return mQueueFamilyIndex; }
    std::uint32_t                          queueIndex() const noexcept { return mQueueIndex; }
    VkCommandPool                          commandPool() const noexcept { return mCommandPool; }
    VkCommandBuffer                        commandBuffer() const noexcept { return mCommandBuffer; }
    VkFence                                fence() const noexcept { return mFence; }
    std::uint32_t                          submissionAttemptCount() const noexcept { return mSubmissionAttemptCount; }
    std::uint32_t                          completionWaitCount() const noexcept { return mCompletionWaitCount; }
    VulkanTextureUploadTransferDisposition disposition() const noexcept { return mDisposition; }

    bool matchesSourceDescription(const VulkanTextureUploadSourceDescription& description) const noexcept;
    bool matchesDestinationDescription(const VulkanTextureUploadDestinationDescription& description) const noexcept;
    bool createdFor(const VulkanPhysicalDeviceGeneration& physical_device_generation,
                    const VulkanLogicalDeviceGeneration&  logical_device_generation) const noexcept;
    bool retainsTextureUploadSourceGeneration(const VulkanTextureUploadSourceGeneration& source_generation) const noexcept;
    bool retainsTextureUploadDestinationGeneration(const VulkanTextureUploadDestinationGeneration& destination_generation) const noexcept;
    bool retainsTextureUploadResources(const VulkanTextureUploadSourceGeneration&      source_generation,
                                       const VulkanTextureUploadDestinationGeneration& destination_generation) const noexcept;

    // Zero polls. UINT64_MAX is rejected so every wait remains bounded.
    VulkanTextureUploadTransferOperationResult execute(std::uint64_t timeout_ns) noexcept;
    VulkanTextureUploadTransferOperationResult retryCompletion(std::uint64_t timeout_ns) noexcept;
    bool                                       reset() noexcept;

private:
    friend struct VulkanTextureUploadTransferGenerationFactory;

    class OperationGuard
    {
    public:
        explicit OperationGuard(VulkanTextureUploadTransferGeneration& owner) noexcept :
            mOwner(owner.mOperationDepth == 0 ? &owner : nullptr)
        {
            if (mOwner)
            {
                ++mOwner->mOperationDepth;
            }
        }
        ~OperationGuard() noexcept
        {
            if (mOwner)
            {
                --mOwner->mOperationDepth;
            }
        }

        OperationGuard(const OperationGuard&)            = delete;
        OperationGuard& operator=(const OperationGuard&) = delete;

        explicit operator bool() const noexcept { return mOwner != nullptr; }

    private:
        VulkanTextureUploadTransferGeneration* mOwner = nullptr;
    };

    VulkanTextureUploadTransferGeneration(
        const VulkanPhysicalDeviceGeneration& physical_device_generation, const VulkanLogicalDeviceGeneration& logical_device_generation,
        const VulkanTextureUploadSourceDescription&      source_description,
        const VulkanTextureUploadDestinationDescription& destination_description,
        const VulkanTextureUploadSourceGeneration& source_generation, VulkanTextureUploadDestinationGeneration& destination_generation,
        VkCommandPool command_pool, VkCommandBuffer command_buffer, VkFence fence, PFN_vkDestroyCommandPool destroy_command_pool,
        PFN_vkDestroyFence destroy_fence, PFN_vkBeginCommandBuffer begin_command_buffer, PFN_vkCmdPipelineBarrier cmd_pipeline_barrier,
        PFN_vkCmdCopyBufferToImage cmd_copy_buffer_to_image, PFN_vkCmdBlitImage cmd_blit_image, PFN_vkEndCommandBuffer end_command_buffer,
        PFN_vkQueueSubmit queue_submit, PFN_vkWaitForFences wait_for_fences) noexcept;

    VulkanTextureUploadTransferOperationResult              finishCompletionWait(VkResult result) noexcept;
    std::optional<VulkanTextureUploadTransferOperationCode> retainedResourceError() const noexcept;
    void                                                    releaseTemporaryResources() noexcept;

    const VulkanPhysicalDeviceGeneration*      mPhysicalDeviceGeneration = nullptr;
    const VulkanLogicalDeviceGeneration*       mLogicalDeviceGeneration  = nullptr;
    const VulkanTextureUploadSourceGeneration* mSourceGeneration         = nullptr;
    VulkanTextureUploadDestinationGeneration*  mDestinationGeneration    = nullptr;
    PFN_vkGetInstanceProcAddr                  mGetInstanceProcAddr      = nullptr;
    VkInstance                                 mInstance                 = VK_NULL_HANDLE;
    VkSurfaceKHR                               mSurface                  = VK_NULL_HANDLE;
    VkPhysicalDevice                           mPhysicalDevice           = VK_NULL_HANDLE;
    std::uint32_t                              mPhysicalDeviceIndex      = 0;
    VkDevice                                   mDevice                   = VK_NULL_HANDLE;
    VkQueue                                    mQueue                    = VK_NULL_HANDLE;
    std::uint32_t                              mQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
    std::uint32_t                              mQueueIndex               = 0;
    VulkanTextureUploadSourceDescription       mSourceDescription;
    VulkanTextureUploadDestinationDescription  mDestinationDescription;
    std::uint64_t                              mContentIdentity          = 0;
    VkBuffer                                   mSourceBuffer             = VK_NULL_HANDLE;
    VkImage                                    mDestinationImage         = VK_NULL_HANDLE;
    VkCommandPool                              mCommandPool              = VK_NULL_HANDLE;
    VkCommandBuffer                            mCommandBuffer            = VK_NULL_HANDLE;
    VkFence                                    mFence                    = VK_NULL_HANDLE;
    PFN_vkDestroyCommandPool                   mDestroyCommandPool       = nullptr;
    PFN_vkDestroyFence                         mDestroyFence             = nullptr;
    PFN_vkBeginCommandBuffer                   mBeginCommandBuffer       = nullptr;
    PFN_vkCmdPipelineBarrier                   mCmdPipelineBarrier       = nullptr;
    PFN_vkCmdCopyBufferToImage                 mCmdCopyBufferToImage     = nullptr;
    PFN_vkCmdBlitImage                         mCmdBlitImage             = nullptr;
    PFN_vkEndCommandBuffer                     mEndCommandBuffer         = nullptr;
    PFN_vkQueueSubmit                          mQueueSubmit              = nullptr;
    PFN_vkWaitForFences                        mWaitForFences            = nullptr;
    VulkanTextureUploadTransferDisposition     mDisposition              = VulkanTextureUploadTransferDisposition::ResetRequired;
    std::uint32_t                              mSubmissionAttemptCount   = 0;
    std::uint32_t                              mCompletionWaitCount      = 0;
    bool                                       mSubmitReportedDeviceLost = false;
    std::uint32_t                              mOperationDepth           = 0;
};

using VulkanTextureUploadTransferResolutionResult =
    std::variant<VulkanTextureUploadTransferResolutionError, VulkanTextureUploadTransferGeneration>;

VulkanTextureUploadTransferResolutionResult resolveVulkanTextureUploadTransferGeneration(
    const VulkanPhysicalDeviceGeneration&            physical_device_generation,
    const VulkanLogicalDeviceGeneration&             logical_device_generation,
    const VulkanTextureUploadSourceDescription&      source_description,
    const VulkanTextureUploadDestinationDescription& destination_description,
    const VulkanTextureUploadSourceGeneration&       source_generation,
    VulkanTextureUploadDestinationGeneration&        destination_generation) noexcept;

} // namespace LLRenderVulkan

#endif // LL_LLRENDERVULKANTEXTUREUPLOADTRANSFER_H
