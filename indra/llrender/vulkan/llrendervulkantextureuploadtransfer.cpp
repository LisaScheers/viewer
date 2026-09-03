/**
 * @file llrendervulkantextureuploadtransfer.cpp
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

#include "llrendervulkantextureuploadtransfer.h"

#include <array>
#include <limits>
#include <utility>

namespace LLRenderVulkan
{
namespace
{
    static_assert(LLRenderContract::TEXTURE_UPLOAD_RESIDENT_WIDTH == 8);
    static_assert(LLRenderContract::TEXTURE_UPLOAD_RESIDENT_HEIGHT == 4);
    static_assert(LLRenderContract::TEXTURE_UPLOAD_ROW_PITCH == 36);
    static_assert(LLRenderContract::TEXTURE_UPLOAD_SOURCE_BYTE_COUNT == 144);
    static_assert(LLRenderContract::TEXTURE_UPLOAD_MIP_LEVELS == 3);

    struct Dispatch
    {
        PFN_vkGetDeviceProcAddr      mGetDeviceProcAddr      = nullptr;
        PFN_vkCreateCommandPool      mCreateCommandPool      = nullptr;
        PFN_vkDestroyCommandPool     mDestroyCommandPool     = nullptr;
        PFN_vkAllocateCommandBuffers mAllocateCommandBuffers = nullptr;
        PFN_vkCreateFence            mCreateFence            = nullptr;
        PFN_vkDestroyFence           mDestroyFence           = nullptr;
        PFN_vkBeginCommandBuffer     mBeginCommandBuffer     = nullptr;
        PFN_vkCmdPipelineBarrier     mCmdPipelineBarrier     = nullptr;
        PFN_vkCmdCopyBufferToImage   mCmdCopyBufferToImage   = nullptr;
        PFN_vkCmdBlitImage           mCmdBlitImage           = nullptr;
        PFN_vkEndCommandBuffer       mEndCommandBuffer       = nullptr;
        PFN_vkQueueSubmit            mQueueSubmit            = nullptr;
        PFN_vkWaitForFences          mWaitForFences          = nullptr;
    };

    template<typename T>
    T resolveInstance(PFN_vkGetInstanceProcAddr get, VkInstance instance, const char* name) noexcept
    {
        return reinterpret_cast<T>(get(instance, name));
    }
    template<typename T>
    T resolveDevice(PFN_vkGetDeviceProcAddr get, VkDevice device, const char* name) noexcept
    {
        return reinterpret_cast<T>(get(device, name));
    }

    VulkanTextureUploadTransferResolutionError resolutionFailure(VulkanTextureUploadTransferResolutionCode         code,
                                                                 std::optional<VulkanTextureUploadTransferCommand> command = std::nullopt,
                                                                 VkResult result = VK_SUCCESS) noexcept
    {
        return { code, command, result };
    }

    VulkanTextureUploadTransferOperationError operationFailure(VulkanTextureUploadTransferOperationCode          code,
                                                               VulkanTextureUploadTransferDisposition            disposition,
                                                               std::optional<VulkanTextureUploadTransferCommand> command = std::nullopt,
                                                               VkResult result = VK_SUCCESS) noexcept
    {
        return { code, command, result, disposition };
    }

    bool validPhysical(const VulkanPhysicalDeviceGeneration& physical) noexcept
    {
        const VkQueueFamilyProperties& queue_family = physical.queueFamilyProperties();
        return physical.getInstanceProcAddr() && physical.instance() != VK_NULL_HANDLE && physical.surface() != VK_NULL_HANDLE &&
               physical.physicalDevice() != VK_NULL_HANDLE && physical.queueFamilyIndex() != VK_QUEUE_FAMILY_IGNORED &&
               queue_family.queueCount != 0 && (queue_family.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
    }
    bool validLogical(const VulkanPhysicalDeviceGeneration& physical, const VulkanLogicalDeviceGeneration& logical) noexcept
    {
        const VkQueueFamilyProperties& queue_family = physical.queueFamilyProperties();
        return logical.createdFor(physical) && logical.device() != VK_NULL_HANDLE && logical.queue() != VK_NULL_HANDLE &&
               logical.queueFamilyIndex() == physical.queueFamilyIndex() && queue_family.queueCount > logical.queueIndex() &&
               (queue_family.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
    }

    bool canonical(const VulkanTextureUploadSourceDescription&      source,
                   const VulkanTextureUploadDestinationDescription& destination) noexcept
    {
        const auto canonical_destination = vulkanTextureUploadDestinationDescription();
        return source.mHandle && source.mHandle == destination.mHandle && source.mExpectedRevision != 0 &&
               source.mExpectedRevision == destination.mExpectedRevision && destination == canonical_destination;
    }

    std::optional<VulkanTextureUploadTransferResolutionError> resourceError(
        const VulkanPhysicalDeviceGeneration& physical, const VulkanLogicalDeviceGeneration& logical,
        const VulkanTextureUploadSourceDescription&      source_description,
        const VulkanTextureUploadDestinationDescription& destination_description, const VulkanTextureUploadSourceGeneration& source,
        const VulkanTextureUploadDestinationGeneration& destination) noexcept
    {
        if (!validPhysical(physical))
            return resolutionFailure(VulkanTextureUploadTransferResolutionCode::InvalidPhysicalDeviceGeneration);
        if (!validLogical(physical, logical))
            return resolutionFailure(VulkanTextureUploadTransferResolutionCode::InvalidLogicalDeviceGeneration);
        if (!canonical(source_description, destination_description))
            return resolutionFailure(VulkanTextureUploadTransferResolutionCode::InvalidDescription);
        if (!source.createdFor(physical, logical) || !source.matchesDescription(source_description) || source.buffer() == VK_NULL_HANDLE ||
            source.byteCount() != VULKAN_TEXTURE_UPLOAD_SOURCE_BYTE_COUNT || source.contentIdentity() == 0)
            return resolutionFailure(VulkanTextureUploadTransferResolutionCode::InvalidTextureUploadSourceGeneration);
        if (!destination.createdFor(physical, logical) || !destination.matchesDescription(destination_description) ||
            destination.image() == VK_NULL_HANDLE || destination.mipLevels() != LLRenderContract::TEXTURE_UPLOAD_MIP_LEVELS ||
            !destination.isDeviceLocal())
            return resolutionFailure(VulkanTextureUploadTransferResolutionCode::InvalidTextureUploadDestinationGeneration);
        if (destination.isResident())
            return resolutionFailure(VulkanTextureUploadTransferResolutionCode::DestinationAlreadyResident);
        if (destination.currentState() != LLRenderContract::ImageState::Undefined || destination.residentRevision() != 0 ||
            destination.residentContentIdentity() != 0)
            return resolutionFailure(VulkanTextureUploadTransferResolutionCode::InvalidTextureUploadDestinationGeneration);
        return std::nullopt;
    }

    std::optional<VulkanTextureUploadTransferResolutionError> resolveDispatch(
        const VulkanPhysicalDeviceGeneration& physical, const VulkanLogicalDeviceGeneration& logical,
        const VulkanTextureUploadSourceDescription&      source_description,
        const VulkanTextureUploadDestinationDescription& destination_description, const VulkanTextureUploadSourceGeneration& source,
        const VulkanTextureUploadDestinationGeneration& destination, Dispatch& d) noexcept
    {
        d.mGetDeviceProcAddr =
            resolveInstance<PFN_vkGetDeviceProcAddr>(physical.getInstanceProcAddr(), physical.instance(), "vkGetDeviceProcAddr");
        if (auto error = resourceError(physical, logical, source_description, destination_description, source, destination))
            return *error;
        if (!d.mGetDeviceProcAddr)
            return resolutionFailure(VulkanTextureUploadTransferResolutionCode::MissingRequiredCommand,
                                     VulkanTextureUploadTransferCommand::GetDeviceProcAddr);
#define RESOLVE(member, type, name, command)                                                                             \
    d.member = resolveDevice<type>(d.mGetDeviceProcAddr, logical.device(), name);                                        \
    if (auto error = resourceError(physical, logical, source_description, destination_description, source, destination)) \
        return *error;                                                                                                   \
    if (!d.member)                                                                                                       \
    return resolutionFailure(VulkanTextureUploadTransferResolutionCode::MissingRequiredCommand, command)
        RESOLVE(mCreateCommandPool, PFN_vkCreateCommandPool, "vkCreateCommandPool", VulkanTextureUploadTransferCommand::CreateCommandPool);
        RESOLVE(mDestroyCommandPool, PFN_vkDestroyCommandPool, "vkDestroyCommandPool",
                VulkanTextureUploadTransferCommand::DestroyCommandPool);
        RESOLVE(mAllocateCommandBuffers, PFN_vkAllocateCommandBuffers, "vkAllocateCommandBuffers",
                VulkanTextureUploadTransferCommand::AllocateCommandBuffers);
        RESOLVE(mCreateFence, PFN_vkCreateFence, "vkCreateFence", VulkanTextureUploadTransferCommand::CreateFence);
        RESOLVE(mDestroyFence, PFN_vkDestroyFence, "vkDestroyFence", VulkanTextureUploadTransferCommand::DestroyFence);
        RESOLVE(mBeginCommandBuffer, PFN_vkBeginCommandBuffer, "vkBeginCommandBuffer",
                VulkanTextureUploadTransferCommand::BeginCommandBuffer);
        RESOLVE(mCmdPipelineBarrier, PFN_vkCmdPipelineBarrier, "vkCmdPipelineBarrier",
                VulkanTextureUploadTransferCommand::CmdPipelineBarrier);
        RESOLVE(mCmdCopyBufferToImage, PFN_vkCmdCopyBufferToImage, "vkCmdCopyBufferToImage",
                VulkanTextureUploadTransferCommand::CmdCopyBufferToImage);
        RESOLVE(mCmdBlitImage, PFN_vkCmdBlitImage, "vkCmdBlitImage", VulkanTextureUploadTransferCommand::CmdBlitImage);
        RESOLVE(mEndCommandBuffer, PFN_vkEndCommandBuffer, "vkEndCommandBuffer", VulkanTextureUploadTransferCommand::EndCommandBuffer);
        RESOLVE(mQueueSubmit, PFN_vkQueueSubmit, "vkQueueSubmit", VulkanTextureUploadTransferCommand::QueueSubmit);
        RESOLVE(mWaitForFences, PFN_vkWaitForFences, "vkWaitForFences", VulkanTextureUploadTransferCommand::WaitForFences);
#undef RESOLVE
        return std::nullopt;
    }

    void rollback(const Dispatch& d, VkDevice device, VkFence fence, VkCommandPool pool) noexcept
    {
        if (fence)
            d.mDestroyFence(device, fence, nullptr);
        if (pool)
            d.mDestroyCommandPool(device, pool, nullptr);
    }

    VkImageMemoryBarrier imageBarrier(VkImage image, VkImageLayout old_layout, VkImageLayout new_layout, VkAccessFlags src,
                                      VkAccessFlags dst, std::uint32_t base, std::uint32_t count) noexcept
    {
        VkImageMemoryBarrier b{};
        b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.srcAccessMask       = src;
        b.dstAccessMask       = dst;
        b.oldLayout           = old_layout;
        b.newLayout           = new_layout;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image               = image;
        b.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, base, count, 0, 1 };
        return b;
    }

    VkImageBlit mipBlit(std::uint32_t mip, std::int32_t sw, std::int32_t sh, std::int32_t dw, std::int32_t dh) noexcept
    {
        VkImageBlit b{};
        b.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, mip, 0, 1 };
        b.srcOffsets[1]  = { sw, sh, 1 };
        b.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, mip + 1, 0, 1 };
        b.dstOffsets[1]  = { dw, dh, 1 };
        return b;
    }
} // namespace

struct VulkanTextureUploadTransferGenerationFactory
{
    static VulkanTextureUploadTransferGeneration create(const VulkanPhysicalDeviceGeneration& p, const VulkanLogicalDeviceGeneration& l,
                                                        const VulkanTextureUploadSourceDescription&      sd,
                                                        const VulkanTextureUploadDestinationDescription& dd,
                                                        const VulkanTextureUploadSourceGeneration&       s,
                                                        VulkanTextureUploadDestinationGeneration& dst, VkCommandPool pool,
                                                        VkCommandBuffer command, VkFence fence, const Dispatch& d) noexcept
    {
        return VulkanTextureUploadTransferGeneration(p, l, sd, dd, s, dst, pool, command, fence, d.mDestroyCommandPool, d.mDestroyFence,
                                                     d.mBeginCommandBuffer, d.mCmdPipelineBarrier, d.mCmdCopyBufferToImage, d.mCmdBlitImage,
                                                     d.mEndCommandBuffer, d.mQueueSubmit, d.mWaitForFences);
    }
};

VulkanTextureUploadTransferGeneration::VulkanTextureUploadTransferGeneration(
    const VulkanPhysicalDeviceGeneration& p, const VulkanLogicalDeviceGeneration& l, const VulkanTextureUploadSourceDescription& sd,
    const VulkanTextureUploadDestinationDescription& dd, const VulkanTextureUploadSourceGeneration& s,
    VulkanTextureUploadDestinationGeneration& dst, VkCommandPool pool, VkCommandBuffer command, VkFence fence,
    PFN_vkDestroyCommandPool destroy_pool, PFN_vkDestroyFence destroy_fence, PFN_vkBeginCommandBuffer begin,
    PFN_vkCmdPipelineBarrier barrier, PFN_vkCmdCopyBufferToImage copy, PFN_vkCmdBlitImage blit, PFN_vkEndCommandBuffer end,
    PFN_vkQueueSubmit submit, PFN_vkWaitForFences wait) noexcept :
    mPhysicalDeviceGeneration(&p),
    mLogicalDeviceGeneration(&l),
    mSourceGeneration(&s),
    mDestinationGeneration(&dst),
    mGetInstanceProcAddr(p.getInstanceProcAddr()),
    mInstance(p.instance()),
    mSurface(p.surface()),
    mPhysicalDevice(p.physicalDevice()),
    mPhysicalDeviceIndex(p.physicalDeviceIndex()),
    mDevice(l.device()),
    mQueue(l.queue()),
    mQueueFamilyIndex(l.queueFamilyIndex()),
    mQueueIndex(l.queueIndex()),
    mSourceDescription(sd),
    mDestinationDescription(dd),
    mContentIdentity(s.contentIdentity()),
    mSourceBuffer(s.buffer()),
    mDestinationImage(dst.image()),
    mCommandPool(pool),
    mCommandBuffer(command),
    mFence(fence),
    mDestroyCommandPool(destroy_pool),
    mDestroyFence(destroy_fence),
    mBeginCommandBuffer(begin),
    mCmdPipelineBarrier(barrier),
    mCmdCopyBufferToImage(copy),
    mCmdBlitImage(blit),
    mEndCommandBuffer(end),
    mQueueSubmit(submit),
    mWaitForFences(wait),
    mDisposition(VulkanTextureUploadTransferDisposition::Ready)
{
}

VulkanTextureUploadTransferGeneration::~VulkanTextureUploadTransferGeneration() noexcept
{
    reset();
}

VulkanTextureUploadTransferGeneration::VulkanTextureUploadTransferGeneration(VulkanTextureUploadTransferGeneration&& o) noexcept
{
    if (o.mOperationDepth != 0)
    {
        return;
    }

    mPhysicalDeviceGeneration = std::exchange(o.mPhysicalDeviceGeneration, nullptr);
    mLogicalDeviceGeneration  = std::exchange(o.mLogicalDeviceGeneration, nullptr);
    mSourceGeneration         = std::exchange(o.mSourceGeneration, nullptr);
    mDestinationGeneration    = std::exchange(o.mDestinationGeneration, nullptr);
    mGetInstanceProcAddr      = std::exchange(o.mGetInstanceProcAddr, nullptr);
    mInstance                 = std::exchange(o.mInstance, VK_NULL_HANDLE);
    mSurface                  = std::exchange(o.mSurface, VK_NULL_HANDLE);
    mPhysicalDevice           = std::exchange(o.mPhysicalDevice, VK_NULL_HANDLE);
    mPhysicalDeviceIndex      = std::exchange(o.mPhysicalDeviceIndex, 0);
    mDevice                   = std::exchange(o.mDevice, VK_NULL_HANDLE);
    mQueue                    = std::exchange(o.mQueue, VK_NULL_HANDLE);
    mQueueFamilyIndex         = std::exchange(o.mQueueFamilyIndex, VK_QUEUE_FAMILY_IGNORED);
    mQueueIndex               = std::exchange(o.mQueueIndex, 0);
    mSourceDescription        = std::exchange(o.mSourceDescription, {});
    mDestinationDescription   = std::exchange(o.mDestinationDescription, {});
    mContentIdentity          = std::exchange(o.mContentIdentity, 0);
    mSourceBuffer             = std::exchange(o.mSourceBuffer, VK_NULL_HANDLE);
    mDestinationImage         = std::exchange(o.mDestinationImage, VK_NULL_HANDLE);
    mCommandPool              = std::exchange(o.mCommandPool, VK_NULL_HANDLE);
    mCommandBuffer            = std::exchange(o.mCommandBuffer, VK_NULL_HANDLE);
    mFence                    = std::exchange(o.mFence, VK_NULL_HANDLE);
    mDestroyCommandPool       = std::exchange(o.mDestroyCommandPool, nullptr);
    mDestroyFence             = std::exchange(o.mDestroyFence, nullptr);
    mBeginCommandBuffer       = std::exchange(o.mBeginCommandBuffer, nullptr);
    mCmdPipelineBarrier       = std::exchange(o.mCmdPipelineBarrier, nullptr);
    mCmdCopyBufferToImage     = std::exchange(o.mCmdCopyBufferToImage, nullptr);
    mCmdBlitImage             = std::exchange(o.mCmdBlitImage, nullptr);
    mEndCommandBuffer         = std::exchange(o.mEndCommandBuffer, nullptr);
    mQueueSubmit              = std::exchange(o.mQueueSubmit, nullptr);
    mWaitForFences            = std::exchange(o.mWaitForFences, nullptr);
    mDisposition              = std::exchange(o.mDisposition, VulkanTextureUploadTransferDisposition::ResetRequired);
    mSubmissionAttemptCount   = std::exchange(o.mSubmissionAttemptCount, 0);
    mCompletionWaitCount      = std::exchange(o.mCompletionWaitCount, 0);
    mSubmitReportedDeviceLost = std::exchange(o.mSubmitReportedDeviceLost, false);
}

bool VulkanTextureUploadTransferGeneration::matchesSourceDescription(const VulkanTextureUploadSourceDescription& d) const noexcept
{
    return mCommandPool && mSourceDescription == d && mContentIdentity && mSourceBuffer;
}
bool VulkanTextureUploadTransferGeneration::matchesDestinationDescription(const VulkanTextureUploadDestinationDescription& d) const noexcept
{
    return mCommandPool && mDestinationDescription == d && mDestinationImage;
}
bool VulkanTextureUploadTransferGeneration::createdFor(const VulkanPhysicalDeviceGeneration& p,
                                                       const VulkanLogicalDeviceGeneration&  l) const noexcept
{
    return mCommandPool && mCommandBuffer && mFence && mPhysicalDeviceGeneration == &p && mLogicalDeviceGeneration == &l &&
           validPhysical(p) && validLogical(p, l) && mGetInstanceProcAddr == p.getInstanceProcAddr() && mInstance == p.instance() &&
           mSurface == p.surface() && mPhysicalDevice == p.physicalDevice() && mPhysicalDeviceIndex == p.physicalDeviceIndex() &&
           mDevice == l.device() && mQueue == l.queue() && mQueueFamilyIndex == l.queueFamilyIndex() && mQueueIndex == l.queueIndex();
}
bool VulkanTextureUploadTransferGeneration::retainsTextureUploadSourceGeneration(
    const VulkanTextureUploadSourceGeneration& s) const noexcept
{
    return (mDisposition == VulkanTextureUploadTransferDisposition::Ready ||
            mDisposition == VulkanTextureUploadTransferDisposition::Pending) &&
           mSourceGeneration == &s;
}
bool VulkanTextureUploadTransferGeneration::retainsTextureUploadDestinationGeneration(
    const VulkanTextureUploadDestinationGeneration& d) const noexcept
{
    return (mDisposition == VulkanTextureUploadTransferDisposition::Ready ||
            mDisposition == VulkanTextureUploadTransferDisposition::Pending) &&
           mDestinationGeneration == &d;
}
bool VulkanTextureUploadTransferGeneration::retainsTextureUploadResources(const VulkanTextureUploadSourceGeneration&      s,
                                                                          const VulkanTextureUploadDestinationGeneration& d) const noexcept
{
    return retainsTextureUploadSourceGeneration(s) && retainsTextureUploadDestinationGeneration(d);
}

std::optional<VulkanTextureUploadTransferOperationCode> VulkanTextureUploadTransferGeneration::retainedResourceError() const noexcept
{
    if (!mPhysicalDeviceGeneration || !validPhysical(*mPhysicalDeviceGeneration) ||
        mGetInstanceProcAddr != mPhysicalDeviceGeneration->getInstanceProcAddr() || mInstance != mPhysicalDeviceGeneration->instance() ||
        mSurface != mPhysicalDeviceGeneration->surface() || mPhysicalDevice != mPhysicalDeviceGeneration->physicalDevice() ||
        mPhysicalDeviceIndex != mPhysicalDeviceGeneration->physicalDeviceIndex())
        return VulkanTextureUploadTransferOperationCode::InvalidPhysicalDeviceGeneration;
    if (!mLogicalDeviceGeneration || !validLogical(*mPhysicalDeviceGeneration, *mLogicalDeviceGeneration) ||
        mDevice != mLogicalDeviceGeneration->device() || mQueue != mLogicalDeviceGeneration->queue() ||
        mQueueFamilyIndex != mLogicalDeviceGeneration->queueFamilyIndex() || mQueueIndex != mLogicalDeviceGeneration->queueIndex())
        return VulkanTextureUploadTransferOperationCode::InvalidLogicalDeviceGeneration;
    if (!mSourceGeneration || !mSourceGeneration->createdFor(*mPhysicalDeviceGeneration, *mLogicalDeviceGeneration) ||
        !mSourceGeneration->matchesDescription(mSourceDescription) || mSourceGeneration->buffer() != mSourceBuffer ||
        mSourceGeneration->contentIdentity() != mContentIdentity ||
        mSourceGeneration->byteCount() != LLRenderContract::TEXTURE_UPLOAD_SOURCE_BYTE_COUNT)
        return VulkanTextureUploadTransferOperationCode::InvalidTextureUploadSourceGeneration;
    if (!mDestinationGeneration || !mDestinationGeneration->createdFor(*mPhysicalDeviceGeneration, *mLogicalDeviceGeneration) ||
        !mDestinationGeneration->matchesDescription(mDestinationDescription) || mDestinationGeneration->image() != mDestinationImage ||
        mDestinationGeneration->isResident() || mDestinationGeneration->currentState() != LLRenderContract::ImageState::Undefined ||
        mDestinationGeneration->residentRevision() != 0 || mDestinationGeneration->residentContentIdentity() != 0)
        return VulkanTextureUploadTransferOperationCode::InvalidTextureUploadDestinationGeneration;
    return std::nullopt;
}

void VulkanTextureUploadTransferGeneration::releaseTemporaryResources() noexcept
{
    mSourceGeneration      = nullptr;
    mDestinationGeneration = nullptr;
}

VulkanTextureUploadTransferOperationResult VulkanTextureUploadTransferGeneration::finishCompletionWait(VkResult result) noexcept
{
    if (result == VK_ERROR_DEVICE_LOST)
    {
        mSubmitReportedDeviceLost = false;
        mDisposition              = VulkanTextureUploadTransferDisposition::DeviceLost;
        releaseTemporaryResources();
        return operationFailure(VulkanTextureUploadTransferOperationCode::CommandFailure, mDisposition,
                                VulkanTextureUploadTransferCommand::WaitForFences, result);
    }
    if (const auto e = retainedResourceError())
    {
        if (result == VK_SUCCESS)
            mDisposition = VulkanTextureUploadTransferDisposition::ResetRequired;
        return operationFailure(*e, mDisposition);
    }
    if (result != VK_SUCCESS)
        return operationFailure(VulkanTextureUploadTransferOperationCode::CommandFailure, mDisposition,
                                VulkanTextureUploadTransferCommand::WaitForFences, result);
    if (mSubmitReportedDeviceLost)
    {
        mSubmitReportedDeviceLost = false;
        mDisposition              = VulkanTextureUploadTransferDisposition::DeviceLost;
        releaseTemporaryResources();
        return operationFailure(VulkanTextureUploadTransferOperationCode::CommandFailure, mDisposition,
                                VulkanTextureUploadTransferCommand::QueueSubmit, VK_ERROR_DEVICE_LOST);
    }
    if (!mDestinationGeneration->markResident(mSourceDescription.mExpectedRevision, mContentIdentity,
                                              LLRenderContract::ImageState::ShaderRead))
    {
        mDisposition = VulkanTextureUploadTransferDisposition::ResetRequired;
        return operationFailure(VulkanTextureUploadTransferOperationCode::DestinationPublicationFailure, mDisposition);
    }
    mDisposition = VulkanTextureUploadTransferDisposition::Complete;
    releaseTemporaryResources();
    return mDisposition;
}

VulkanTextureUploadTransferOperationResult VulkanTextureUploadTransferGeneration::execute(std::uint64_t timeout_ns) noexcept
{
    OperationGuard operation(*this);
    if (!operation)
        return operationFailure(VulkanTextureUploadTransferOperationCode::InvalidDisposition, mDisposition);
    if (timeout_ns == std::numeric_limits<std::uint64_t>::max())
        return operationFailure(VulkanTextureUploadTransferOperationCode::InvalidTimeout, mDisposition);
    if (mDisposition != VulkanTextureUploadTransferDisposition::Ready)
        return operationFailure(VulkanTextureUploadTransferOperationCode::InvalidDisposition, mDisposition);
    auto validate = [this]() -> std::optional<VulkanTextureUploadTransferOperationResult>
    {
        if (const auto e = retainedResourceError())
        {
            mDisposition = VulkanTextureUploadTransferDisposition::ResetRequired;
            return operationFailure(*e, mDisposition);
        }
        return std::nullopt;
    };
    if (auto e = validate())
        return *e;
    VkCommandBufferBeginInfo begin{};
    begin.sType     = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags     = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VkResult result = mBeginCommandBuffer(mCommandBuffer, &begin);
    if (auto e = validate())
        return *e;
    if (result != VK_SUCCESS)
    {
        mDisposition = VulkanTextureUploadTransferDisposition::ResetRequired;
        return operationFailure(VulkanTextureUploadTransferOperationCode::CommandFailure, mDisposition,
                                VulkanTextureUploadTransferCommand::BeginCommandBuffer, result);
    }

    VkBufferMemoryBarrier source{};
    source.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    source.srcAccessMask       = VK_ACCESS_HOST_WRITE_BIT;
    source.dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
    source.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    source.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    source.buffer              = mSourceBuffer;
    source.offset              = 0;
    source.size                = LLRenderContract::TEXTURE_UPLOAD_SOURCE_BYTE_COUNT;
    mCmdPipelineBarrier(mCommandBuffer, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &source, 0, nullptr);
    if (auto e = validate())
        return *e;
    auto all_dst = imageBarrier(mDestinationImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                                VK_ACCESS_TRANSFER_WRITE_BIT, 0, LLRenderContract::TEXTURE_UPLOAD_MIP_LEVELS);
    mCmdPipelineBarrier(mCommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                        &all_dst);
    if (auto e = validate())
        return *e;

    constexpr std::array<VkDeviceSize, LLRenderContract::TEXTURE_UPLOAD_RESIDENT_HEIGHT> offsets{
        3 * LLRenderContract::TEXTURE_UPLOAD_ROW_PITCH, 2 * LLRenderContract::TEXTURE_UPLOAD_ROW_PITCH,
        LLRenderContract::TEXTURE_UPLOAD_ROW_PITCH, 0
    };
    std::array<VkBufferImageCopy, LLRenderContract::TEXTURE_UPLOAD_RESIDENT_HEIGHT> rows{};
    for (std::uint32_t y = 0; y < rows.size(); ++y)
    {
        rows[y].bufferOffset     = offsets[y];
        rows[y].imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        rows[y].imageOffset      = { 0, static_cast<std::int32_t>(y), 0 };
        rows[y].imageExtent      = { LLRenderContract::TEXTURE_UPLOAD_RESIDENT_WIDTH, 1, 1 };
    }
    mCmdCopyBufferToImage(mCommandBuffer, mSourceBuffer, mDestinationImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          static_cast<std::uint32_t>(rows.size()), rows.data());
    if (auto e = validate())
        return *e;
    auto mip0 = imageBarrier(mDestinationImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                             VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, 0, 1);
    mCmdPipelineBarrier(mCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                        &mip0);
    if (auto e = validate())
        return *e;
    auto blit0 = mipBlit(0, LLRenderContract::TEXTURE_UPLOAD_RESIDENT_WIDTH, LLRenderContract::TEXTURE_UPLOAD_RESIDENT_HEIGHT,
                         LLRenderContract::TEXTURE_UPLOAD_RESIDENT_WIDTH / 2, LLRenderContract::TEXTURE_UPLOAD_RESIDENT_HEIGHT / 2);
    mCmdBlitImage(mCommandBuffer, mDestinationImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, mDestinationImage,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit0, VK_FILTER_LINEAR);
    if (auto e = validate())
        return *e;
    auto mip1 = imageBarrier(mDestinationImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                             VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, 1, 1);
    mCmdPipelineBarrier(mCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                        &mip1);
    if (auto e = validate())
        return *e;
    auto blit1 = mipBlit(1, 4, 2, 2, 1);
    mCmdBlitImage(mCommandBuffer, mDestinationImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, mDestinationImage,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit1, VK_FILTER_LINEAR);
    if (auto e = validate())
        return *e;
    std::array<VkImageMemoryBarrier, 2> sampled{
        imageBarrier(mDestinationImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT, 0, 2),
        imageBarrier(mDestinationImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, 2, 1)
    };
    mCmdPipelineBarrier(mCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 2,
                        sampled.data());
    if (auto e = validate())
        return *e;
    result = mEndCommandBuffer(mCommandBuffer);
    if (auto e = validate())
        return *e;
    if (result != VK_SUCCESS)
    {
        mDisposition = VulkanTextureUploadTransferDisposition::ResetRequired;
        return operationFailure(VulkanTextureUploadTransferOperationCode::CommandFailure, mDisposition,
                                VulkanTextureUploadTransferCommand::EndCommandBuffer, result);
    }
    VkSubmitInfo submit{};
    submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &mCommandBuffer;
    ++mSubmissionAttemptCount;
    result                    = mQueueSubmit(mQueue, 1, &submit, mFence);
    const auto callback_error = retainedResourceError();
    if (result != VK_SUCCESS && result != VK_ERROR_DEVICE_LOST)
    {
        mDisposition = VulkanTextureUploadTransferDisposition::ResetRequired;
        if (callback_error)
            return operationFailure(*callback_error, mDisposition);
        return operationFailure(VulkanTextureUploadTransferOperationCode::CommandFailure, mDisposition,
                                VulkanTextureUploadTransferCommand::QueueSubmit, result);
    }
    mSubmitReportedDeviceLost = result == VK_ERROR_DEVICE_LOST;
    mDisposition              = VulkanTextureUploadTransferDisposition::Pending;
    ++mCompletionWaitCount;
    result = mWaitForFences(mDevice, 1, &mFence, VK_TRUE, timeout_ns);
    return finishCompletionWait(result);
}

VulkanTextureUploadTransferOperationResult VulkanTextureUploadTransferGeneration::retryCompletion(std::uint64_t timeout_ns) noexcept
{
    OperationGuard operation(*this);
    if (!operation)
        return operationFailure(VulkanTextureUploadTransferOperationCode::InvalidDisposition, mDisposition);
    if (timeout_ns == std::numeric_limits<std::uint64_t>::max())
        return operationFailure(VulkanTextureUploadTransferOperationCode::InvalidTimeout, mDisposition);
    if (mDisposition != VulkanTextureUploadTransferDisposition::Pending)
        return operationFailure(VulkanTextureUploadTransferOperationCode::InvalidDisposition, mDisposition);
    ++mCompletionWaitCount;
    return finishCompletionWait(mWaitForFences(mDevice, 1, &mFence, VK_TRUE, timeout_ns));
}

bool VulkanTextureUploadTransferGeneration::reset() noexcept
{
    if (mOperationDepth != 0 || mDisposition == VulkanTextureUploadTransferDisposition::Pending)
        return false;
    const VkDevice      device        = std::exchange(mDevice, VK_NULL_HANDLE);
    const VkFence       fence         = std::exchange(mFence, VK_NULL_HANDLE);
    const VkCommandPool pool          = std::exchange(mCommandPool, VK_NULL_HANDLE);
    auto                destroy_fence = std::exchange(mDestroyFence, nullptr);
    auto                destroy_pool  = std::exchange(mDestroyCommandPool, nullptr);
    mCommandBuffer                    = VK_NULL_HANDLE;
    mPhysicalDeviceGeneration         = nullptr;
    mLogicalDeviceGeneration          = nullptr;
    releaseTemporaryResources();
    mGetInstanceProcAddr      = nullptr;
    mInstance                 = VK_NULL_HANDLE;
    mSurface                  = VK_NULL_HANDLE;
    mPhysicalDevice           = VK_NULL_HANDLE;
    mPhysicalDeviceIndex      = 0;
    mQueue                    = VK_NULL_HANDLE;
    mQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
    mQueueIndex               = 0;
    mSourceDescription        = {};
    mDestinationDescription   = {};
    mContentIdentity          = 0;
    mSourceBuffer             = VK_NULL_HANDLE;
    mDestinationImage         = VK_NULL_HANDLE;
    mBeginCommandBuffer       = nullptr;
    mCmdPipelineBarrier       = nullptr;
    mCmdCopyBufferToImage     = nullptr;
    mCmdBlitImage             = nullptr;
    mEndCommandBuffer         = nullptr;
    mQueueSubmit              = nullptr;
    mWaitForFences            = nullptr;
    mDisposition              = VulkanTextureUploadTransferDisposition::ResetRequired;
    mSubmissionAttemptCount   = 0;
    mCompletionWaitCount      = 0;
    mSubmitReportedDeviceLost = false;
    mOperationDepth           = 0;
    if (fence && destroy_fence)
        destroy_fence(device, fence, nullptr);
    if (pool && destroy_pool)
        destroy_pool(device, pool, nullptr);
    return true;
}

VulkanTextureUploadTransferResolutionResult resolveVulkanTextureUploadTransferGeneration(
    const VulkanPhysicalDeviceGeneration& physical, const VulkanLogicalDeviceGeneration& logical,
    const VulkanTextureUploadSourceDescription&      source_description,
    const VulkanTextureUploadDestinationDescription& destination_description, const VulkanTextureUploadSourceGeneration& source,
    VulkanTextureUploadDestinationGeneration& destination) noexcept
{
    const auto sd = source_description;
    const auto dd = destination_description;
    if (auto e = resourceError(physical, logical, sd, dd, source, destination))
        return *e;
    Dispatch d;
    if (auto e = resolveDispatch(physical, logical, sd, dd, source, destination, d))
        return *e;
    if (auto e = resourceError(physical, logical, sd, dd, source, destination))
        return *e;
    const VkDevice          device = logical.device();
    VkCommandPoolCreateInfo pi{};
    pi.sType             = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pi.queueFamilyIndex  = logical.queueFamilyIndex();
    VkCommandPool pool   = VK_NULL_HANDLE;
    VkResult      result = d.mCreateCommandPool(device, &pi, nullptr, &pool);
    if (auto e = resourceError(physical, logical, sd, dd, source, destination))
    {
        if (result == VK_SUCCESS && pool != VK_NULL_HANDLE)
            rollback(d, device, VK_NULL_HANDLE, pool);
        return *e;
    }
    if (result != VK_SUCCESS)
        return resolutionFailure(VulkanTextureUploadTransferResolutionCode::CommandPoolCreationFailure,
                                 VulkanTextureUploadTransferCommand::CreateCommandPool, result);
    if (!pool)
        return resolutionFailure(VulkanTextureUploadTransferResolutionCode::NullCommandPoolOnSuccess,
                                 VulkanTextureUploadTransferCommand::CreateCommandPool);
    VkCommandBufferAllocateInfo ai{};
    ai.sType                = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool          = pool;
    ai.level                = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount   = 1;
    VkCommandBuffer command = VK_NULL_HANDLE;
    result                  = d.mAllocateCommandBuffers(device, &ai, &command);
    if (auto e = resourceError(physical, logical, sd, dd, source, destination))
    {
        rollback(d, device, VK_NULL_HANDLE, pool);
        return *e;
    }
    if (result != VK_SUCCESS)
    {
        rollback(d, device, VK_NULL_HANDLE, pool);
        return resolutionFailure(VulkanTextureUploadTransferResolutionCode::CommandBufferAllocationFailure,
                                 VulkanTextureUploadTransferCommand::AllocateCommandBuffers, result);
    }
    if (!command)
    {
        rollback(d, device, VK_NULL_HANDLE, pool);
        return resolutionFailure(VulkanTextureUploadTransferResolutionCode::NullCommandBufferOnSuccess,
                                 VulkanTextureUploadTransferCommand::AllocateCommandBuffers);
    }
    VkFenceCreateInfo fi{};
    fi.sType      = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = VK_NULL_HANDLE;
    result        = d.mCreateFence(device, &fi, nullptr, &fence);
    if (auto e = resourceError(physical, logical, sd, dd, source, destination))
    {
        rollback(d, device, result == VK_SUCCESS ? fence : VK_NULL_HANDLE, pool);
        return *e;
    }
    if (result != VK_SUCCESS)
    {
        rollback(d, device, VK_NULL_HANDLE, pool);
        return resolutionFailure(VulkanTextureUploadTransferResolutionCode::FenceCreationFailure,
                                 VulkanTextureUploadTransferCommand::CreateFence, result);
    }
    if (!fence)
    {
        rollback(d, device, VK_NULL_HANDLE, pool);
        return resolutionFailure(VulkanTextureUploadTransferResolutionCode::NullFenceOnSuccess,
                                 VulkanTextureUploadTransferCommand::CreateFence);
    }
    return VulkanTextureUploadTransferGenerationFactory::create(physical, logical, sd, dd, source, destination, pool, command, fence, d);
}
} // namespace LLRenderVulkan
