/**
 * @file llrendervulkanswapchainframeslot.cpp
 * @brief Loader-neutral Vulkan swapchain frame-slot ownership.
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

#include "llrendervulkanswapchainframeslot.h"
#include "llrendervulkanswapchainpresentationpipeline.h"
#include "llrendervulkanswapchainpresentationtarget.h"
#include "lltextureuploadcontract.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace LLRenderVulkan
{

namespace
{
    struct SwapchainFrameSlotDispatch
    {
        PFN_vkGetDeviceProcAddr      mGetDeviceProcAddr      = nullptr;
        PFN_vkCreateCommandPool      mCreateCommandPool      = nullptr;
        PFN_vkDestroyCommandPool     mDestroyCommandPool     = nullptr;
        PFN_vkAllocateCommandBuffers mAllocateCommandBuffers = nullptr;
        PFN_vkCreateSemaphore        mCreateSemaphore        = nullptr;
        PFN_vkDestroySemaphore       mDestroySemaphore       = nullptr;
        PFN_vkCreateFence            mCreateFence            = nullptr;
        PFN_vkDestroyFence           mDestroyFence           = nullptr;
    };

    struct EmptySubmissionDispatch
    {
        PFN_vkWaitForFences      mWaitForFences      = nullptr;
        PFN_vkResetCommandBuffer mResetCommandBuffer = nullptr;
        PFN_vkBeginCommandBuffer mBeginCommandBuffer = nullptr;
        PFN_vkEndCommandBuffer   mEndCommandBuffer   = nullptr;
        PFN_vkResetFences        mResetFences        = nullptr;
        PFN_vkQueueSubmit        mQueueSubmit        = nullptr;
    };

    struct PresentationDispatch
    {
        EmptySubmissionDispatch       mSubmission;
        PFN_vkAcquireNextImageKHR       mAcquireNextImage       = nullptr;
        PFN_vkCmdPipelineBarrier        mCmdPipelineBarrier     = nullptr;
        PFN_vkCmdClearColorImage        mCmdClearColorImage     = nullptr;
        PFN_vkCmdBeginRenderPass        mCmdBeginRenderPass     = nullptr;
        PFN_vkCmdEndRenderPass          mCmdEndRenderPass       = nullptr;
        PFN_vkCmdBindPipeline           mCmdBindPipeline        = nullptr;
        PFN_vkCmdBindVertexBuffers      mCmdBindVertexBuffers   = nullptr;
        PFN_vkCmdSetViewport            mCmdSetViewport         = nullptr;
        PFN_vkCmdSetScissor             mCmdSetScissor          = nullptr;
        PFN_vkCmdDraw                   mCmdDraw                = nullptr;
        PFN_vkCmdCopyImageToBuffer      mCmdCopyImageToBuffer   = nullptr;
        PFN_vkQueuePresentKHR           mQueuePresent           = nullptr;
        PFN_vkReleaseSwapchainImagesKHR mReleaseSwapchainImages = nullptr;
    };

    bool validClearColor(const VulkanSwapchainFrameClearColor& clear_color) noexcept
    {
        return std::all_of(clear_color.mRgba.begin(), clear_color.mRgba.end(),
                           [](float component) { return std::isfinite(component) && component >= 0.0f && component <= 1.0f; });
    }

    bool validUploadDestination(const VulkanUploadDestinationGeneration& destination,
                                const VulkanPhysicalDeviceGeneration&    physical_device,
                                const VulkanLogicalDeviceGeneration&     logical_device) noexcept
    {
        constexpr VulkanUploadSourceDescription screen_triangle = vulkanScreenTriangleUploadSourceDescription();
        return destination.createdFor(physical_device, logical_device) && destination.matchesDescription(screen_triangle) &&
               destination.resourceHandle() == screen_triangle.mHandle &&
               destination.expectedContentIdentity() == LLRenderContract::SCREEN_TRIANGLE_CONTENT_IDENTITY &&
               destination.residentContentIdentity() == destination.expectedContentIdentity() && destination.isResident() &&
               destination.buffer() != VK_NULL_HANDLE && destination.memory() != VK_NULL_HANDLE &&
               destination.byteCount() == VULKAN_UPLOAD_SOURCE_BYTE_COUNT &&
               destination.usage() == (VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) &&
               destination.allocationSize() >= VULKAN_UPLOAD_SOURCE_BYTE_COUNT && destination.isDeviceLocal() && !destination.isMapped();
    }

    VulkanSwapchainFrameSlotResolutionError failure(VulkanSwapchainFrameSlotResolutionCode         code,
                                                    std::optional<VulkanSwapchainFrameSlotCommand> command = std::nullopt,
                                                    VkResult                                       result  = VK_SUCCESS) noexcept
    {
        return { code, command, result };
    }

    VulkanSwapchainFrameSlotOperationError operationFailure(VulkanSwapchainFrameSlotOperationCode          code,
                                                            VulkanSwapchainFrameSlotDisposition            disposition,
                                                            std::optional<VulkanSwapchainFrameSlotCommand> command = std::nullopt,
                                                            VkResult                                       result  = VK_SUCCESS,
                                                            std::optional<std::uint32_t>                   image_index = std::nullopt) noexcept
    {
        return { code, command, result, disposition, image_index };
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

    bool valid(const VulkanLogicalDeviceGeneration& generation) noexcept
    {
        return generation.getInstanceProcAddr() != nullptr && generation.instance() != VK_NULL_HANDLE &&
               generation.surface() != VK_NULL_HANDLE && generation.physicalDevice() != VK_NULL_HANDLE &&
               generation.device() != VK_NULL_HANDLE && generation.queue() != VK_NULL_HANDLE &&
               generation.queueFamilyIndex() != VK_QUEUE_FAMILY_IGNORED;
    }

    bool belongsTo(const VulkanSwapchainConfigurationGeneration& configuration,
                   const VulkanLogicalDeviceGeneration&          logical_device) noexcept
    {
        const VkExtent2D drawable_extent = configuration.drawableExtent();
        return configuration.getInstanceProcAddr() == logical_device.getInstanceProcAddr() &&
               configuration.instance() == logical_device.instance() && configuration.surface() == logical_device.surface() &&
               configuration.physicalDevice() == logical_device.physicalDevice() &&
               configuration.physicalDeviceIndex() == logical_device.physicalDeviceIndex() &&
               configuration.device() == logical_device.device() && configuration.queueFamilyIndex() == logical_device.queueFamilyIndex() &&
               drawable_extent.width != 0 && drawable_extent.height != 0 && configuration.imageCount() != 0 &&
               configuration.surfaceFormat().format != VK_FORMAT_UNDEFINED;
    }

    std::optional<VulkanSwapchainFrameSlotResolutionError> resolveDispatch(const VulkanLogicalDeviceGeneration& logical_device,
                                                                           SwapchainFrameSlotDispatch&          dispatch) noexcept
    {
        dispatch.mGetDeviceProcAddr = resolveInstance<PFN_vkGetDeviceProcAddr>(logical_device.getInstanceProcAddr(),
                                                                               logical_device.instance(), "vkGetDeviceProcAddr");
        if (!dispatch.mGetDeviceProcAddr)
        {
            return failure(VulkanSwapchainFrameSlotResolutionCode::MissingRequiredCommand,
                           VulkanSwapchainFrameSlotCommand::GetDeviceProcAddr);
        }

        dispatch.mCreateCommandPool =
            resolveDevice<PFN_vkCreateCommandPool>(dispatch.mGetDeviceProcAddr, logical_device.device(), "vkCreateCommandPool");
        if (!dispatch.mCreateCommandPool)
        {
            return failure(VulkanSwapchainFrameSlotResolutionCode::MissingRequiredCommand,
                           VulkanSwapchainFrameSlotCommand::CreateCommandPool);
        }
        dispatch.mDestroyCommandPool =
            resolveDevice<PFN_vkDestroyCommandPool>(dispatch.mGetDeviceProcAddr, logical_device.device(), "vkDestroyCommandPool");
        if (!dispatch.mDestroyCommandPool)
        {
            return failure(VulkanSwapchainFrameSlotResolutionCode::MissingRequiredCommand,
                           VulkanSwapchainFrameSlotCommand::DestroyCommandPool);
        }
        dispatch.mAllocateCommandBuffers =
            resolveDevice<PFN_vkAllocateCommandBuffers>(dispatch.mGetDeviceProcAddr, logical_device.device(), "vkAllocateCommandBuffers");
        if (!dispatch.mAllocateCommandBuffers)
        {
            return failure(VulkanSwapchainFrameSlotResolutionCode::MissingRequiredCommand,
                           VulkanSwapchainFrameSlotCommand::AllocateCommandBuffers);
        }
        dispatch.mCreateSemaphore =
            resolveDevice<PFN_vkCreateSemaphore>(dispatch.mGetDeviceProcAddr, logical_device.device(), "vkCreateSemaphore");
        if (!dispatch.mCreateSemaphore)
        {
            return failure(VulkanSwapchainFrameSlotResolutionCode::MissingRequiredCommand,
                           VulkanSwapchainFrameSlotCommand::CreateSemaphore);
        }
        dispatch.mDestroySemaphore =
            resolveDevice<PFN_vkDestroySemaphore>(dispatch.mGetDeviceProcAddr, logical_device.device(), "vkDestroySemaphore");
        if (!dispatch.mDestroySemaphore)
        {
            return failure(VulkanSwapchainFrameSlotResolutionCode::MissingRequiredCommand,
                           VulkanSwapchainFrameSlotCommand::DestroySemaphore);
        }
        dispatch.mCreateFence = resolveDevice<PFN_vkCreateFence>(dispatch.mGetDeviceProcAddr, logical_device.device(), "vkCreateFence");
        if (!dispatch.mCreateFence)
        {
            return failure(VulkanSwapchainFrameSlotResolutionCode::MissingRequiredCommand, VulkanSwapchainFrameSlotCommand::CreateFence);
        }
        dispatch.mDestroyFence = resolveDevice<PFN_vkDestroyFence>(dispatch.mGetDeviceProcAddr, logical_device.device(), "vkDestroyFence");
        if (!dispatch.mDestroyFence)
        {
            return failure(VulkanSwapchainFrameSlotResolutionCode::MissingRequiredCommand, VulkanSwapchainFrameSlotCommand::DestroyFence);
        }

        return std::nullopt;
    }

    void rollback(VkDevice                          device,
                  const SwapchainFrameSlotDispatch& dispatch,
                  VkFence                           present_completion_fence,
                  VkFence                           submission_fence,
                  VkSemaphore                       presentation_ready_semaphore,
                  VkSemaphore                       image_available_semaphore,
                  VkCommandPool                     command_pool) noexcept
    {
        if (present_completion_fence != VK_NULL_HANDLE)
        {
            dispatch.mDestroyFence(device, present_completion_fence, nullptr);
        }
        if (submission_fence != VK_NULL_HANDLE)
        {
            dispatch.mDestroyFence(device, submission_fence, nullptr);
        }
        if (presentation_ready_semaphore != VK_NULL_HANDLE)
        {
            dispatch.mDestroySemaphore(device, presentation_ready_semaphore, nullptr);
        }
        if (image_available_semaphore != VK_NULL_HANDLE)
        {
            dispatch.mDestroySemaphore(device, image_available_semaphore, nullptr);
        }
        if (command_pool != VK_NULL_HANDLE)
        {
            dispatch.mDestroyCommandPool(device, command_pool, nullptr);
        }
    }

} // namespace

struct VulkanSwapchainFrameSlotGenerationFactory
{
    static VulkanSwapchainFrameSlotGeneration create(const VulkanLogicalDeviceGeneration&          logical_device_generation,
                                                     const VulkanSwapchainConfigurationGeneration& configuration_generation,
                                                     const VulkanSwapchainGeneration&              swapchain_generation,
                                                     const VulkanSwapchainImagesGeneration&        images_generation,
                                                     VkCommandPool                                 command_pool,
                                                     VkCommandBuffer                               command_buffer,
                                                     VkSemaphore                                   image_available_semaphore,
                                                     VkSemaphore                                   presentation_ready_semaphore,
                                                     VkFence                                       submission_fence,
                                                     VkFence                                       present_completion_fence,
                                                     PFN_vkDestroyCommandPool                      destroy_command_pool,
                                                     PFN_vkDestroySemaphore                        destroy_semaphore,
                                                     PFN_vkDestroyFence                            destroy_fence) noexcept
    {
        return VulkanSwapchainFrameSlotGeneration(logical_device_generation, configuration_generation, swapchain_generation,
                                                  images_generation, command_pool, command_buffer, image_available_semaphore,
                                                  presentation_ready_semaphore, submission_fence, present_completion_fence,
                                                  destroy_command_pool, destroy_semaphore, destroy_fence);
    }
};

VulkanSwapchainFrameSlotGeneration::VulkanSwapchainFrameSlotGeneration(
    const VulkanLogicalDeviceGeneration&          logical_device_generation,
    const VulkanSwapchainConfigurationGeneration& configuration_generation,
    const VulkanSwapchainGeneration&              swapchain_generation,
    const VulkanSwapchainImagesGeneration&        images_generation,
    VkCommandPool                                 command_pool,
    VkCommandBuffer                               command_buffer,
    VkSemaphore                                   image_available_semaphore,
    VkSemaphore                                   presentation_ready_semaphore,
    VkFence                                       submission_fence,
    VkFence                                       present_completion_fence,
    PFN_vkDestroyCommandPool                      destroy_command_pool,
    PFN_vkDestroySemaphore                        destroy_semaphore,
    PFN_vkDestroyFence                            destroy_fence) noexcept :
    mLogicalDeviceGeneration(&logical_device_generation),
    mConfigurationGeneration(&configuration_generation),
    mSwapchainGeneration(&swapchain_generation),
    mGetInstanceProcAddr(logical_device_generation.getInstanceProcAddr()),
    mInstance(logical_device_generation.instance()),
    mSurface(logical_device_generation.surface()),
    mPhysicalDevice(logical_device_generation.physicalDevice()),
    mPhysicalDeviceIndex(logical_device_generation.physicalDeviceIndex()),
    mDevice(logical_device_generation.device()),
    mQueue(logical_device_generation.queue()),
    mQueueFamilyIndex(logical_device_generation.queueFamilyIndex()),
    mQueueIndex(logical_device_generation.queueIndex()),
    mDrawableExtent(configuration_generation.drawableExtent()),
    mImageExtent(configuration_generation.imageExtent()),
    mSwapchain(swapchain_generation.swapchain()),
    mImageFormat(images_generation.imageFormat()),
    mImageCount(images_generation.imageCount()),
    mImagesGeneration(&images_generation),
    mCommandPool(command_pool),
    mCommandBuffer(command_buffer),
    mImageAvailableSemaphore(image_available_semaphore),
    mPresentationReadySemaphore(presentation_ready_semaphore),
    mSubmissionFence(submission_fence),
    mPresentCompletionFence(present_completion_fence),
    mDestroyCommandPool(destroy_command_pool),
    mDestroySemaphore(destroy_semaphore),
    mDestroyFence(destroy_fence)
{
}

VulkanSwapchainFrameSlotGeneration::~VulkanSwapchainFrameSlotGeneration() noexcept
{
    reset();
}

VulkanSwapchainFrameSlotGeneration::VulkanSwapchainFrameSlotGeneration(VulkanSwapchainFrameSlotGeneration&& other) noexcept :
    mLogicalDeviceGeneration(std::exchange(other.mLogicalDeviceGeneration, nullptr)),
    mConfigurationGeneration(std::exchange(other.mConfigurationGeneration, nullptr)),
    mSwapchainGeneration(std::exchange(other.mSwapchainGeneration, nullptr)),
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
    mImageExtent(std::exchange(other.mImageExtent, {})),
    mSwapchain(std::exchange(other.mSwapchain, VK_NULL_HANDLE)),
    mImageFormat(std::exchange(other.mImageFormat, VK_FORMAT_UNDEFINED)),
    mImageCount(std::exchange(other.mImageCount, 0)),
    mImagesGeneration(std::exchange(other.mImagesGeneration, nullptr)),
    mPresentationTargetGeneration(std::exchange(other.mPresentationTargetGeneration, nullptr)),
    mPresentationPipelineGeneration(std::exchange(other.mPresentationPipelineGeneration, nullptr)),
    mPresentationPipelineLayout(std::exchange(other.mPresentationPipelineLayout, VK_NULL_HANDLE)),
    mPresentationPipeline(std::exchange(other.mPresentationPipeline, VK_NULL_HANDLE)),
    mCommandPool(std::exchange(other.mCommandPool, VK_NULL_HANDLE)),
    mCommandBuffer(std::exchange(other.mCommandBuffer, VK_NULL_HANDLE)),
    mImageAvailableSemaphore(std::exchange(other.mImageAvailableSemaphore, VK_NULL_HANDLE)),
    mPresentationReadySemaphore(std::exchange(other.mPresentationReadySemaphore, VK_NULL_HANDLE)),
    mSubmissionFence(std::exchange(other.mSubmissionFence, VK_NULL_HANDLE)),
    mPresentCompletionFence(std::exchange(other.mPresentCompletionFence, VK_NULL_HANDLE)),
    mDestroyCommandPool(std::exchange(other.mDestroyCommandPool, nullptr)),
    mDestroySemaphore(std::exchange(other.mDestroySemaphore, nullptr)),
    mDestroyFence(std::exchange(other.mDestroyFence, nullptr)),
    mWaitForFences(std::exchange(other.mWaitForFences, nullptr)),
    mResetCommandBuffer(std::exchange(other.mResetCommandBuffer, nullptr)),
    mBeginCommandBuffer(std::exchange(other.mBeginCommandBuffer, nullptr)),
    mEndCommandBuffer(std::exchange(other.mEndCommandBuffer, nullptr)),
    mResetFences(std::exchange(other.mResetFences, nullptr)),
    mQueueSubmit(std::exchange(other.mQueueSubmit, nullptr)),
    mAcquireNextImage(std::exchange(other.mAcquireNextImage, nullptr)),
    mCmdPipelineBarrier(std::exchange(other.mCmdPipelineBarrier, nullptr)),
    mCmdClearColorImage(std::exchange(other.mCmdClearColorImage, nullptr)),
    mCmdBeginRenderPass(std::exchange(other.mCmdBeginRenderPass, nullptr)),
    mCmdEndRenderPass(std::exchange(other.mCmdEndRenderPass, nullptr)),
    mCmdBindPipeline(std::exchange(other.mCmdBindPipeline, nullptr)),
    mCmdBindVertexBuffers(std::exchange(other.mCmdBindVertexBuffers, nullptr)),
    mCmdSetViewport(std::exchange(other.mCmdSetViewport, nullptr)),
    mCmdSetScissor(std::exchange(other.mCmdSetScissor, nullptr)),
    mCmdDraw(std::exchange(other.mCmdDraw, nullptr)),
    mCmdCopyImageToBuffer(std::exchange(other.mCmdCopyImageToBuffer, nullptr)),
    mQueuePresent(std::exchange(other.mQueuePresent, nullptr)),
    mReleaseSwapchainImages(std::exchange(other.mReleaseSwapchainImages, nullptr)),
    mUploadDestinationPhysicalDeviceGeneration(std::exchange(other.mUploadDestinationPhysicalDeviceGeneration, nullptr)),
    mActiveUploadDestinationGeneration(std::exchange(other.mActiveUploadDestinationGeneration, nullptr)),
    mActiveUploadDestinationResourceHandle(std::exchange(other.mActiveUploadDestinationResourceHandle, {})),
    mActiveUploadDestinationExpectedIdentity(std::exchange(other.mActiveUploadDestinationExpectedIdentity, 0)),
    mActiveUploadDestinationResidentIdentity(std::exchange(other.mActiveUploadDestinationResidentIdentity, 0)),
    mActiveUploadDestinationBuffer(std::exchange(other.mActiveUploadDestinationBuffer, VK_NULL_HANDLE)),
    mActiveUploadDestinationMemory(std::exchange(other.mActiveUploadDestinationMemory, VK_NULL_HANDLE)),
    mActiveUploadDestinationByteCount(std::exchange(other.mActiveUploadDestinationByteCount, 0)),
    mActiveUploadDestinationUsage(std::exchange(other.mActiveUploadDestinationUsage, 0)),
    mActiveUploadDestinationAllocationSize(std::exchange(other.mActiveUploadDestinationAllocationSize, 0)),
    mActiveUploadDestinationMemoryTypeIndex(std::exchange(other.mActiveUploadDestinationMemoryTypeIndex, 0)),
    mActiveUploadDestinationMemoryPropertyFlags(std::exchange(other.mActiveUploadDestinationMemoryPropertyFlags, 0)),
    mReadbackPhysicalDeviceGeneration(std::exchange(other.mReadbackPhysicalDeviceGeneration, nullptr)),
    mResolvedReadbackGeneration(std::exchange(other.mResolvedReadbackGeneration, nullptr)),
    mActiveReadbackGeneration(std::exchange(other.mActiveReadbackGeneration, nullptr)),
    mActiveReadbackBuffer(std::exchange(other.mActiveReadbackBuffer, VK_NULL_HANDLE)),
    mActiveReadbackImageFormat(std::exchange(other.mActiveReadbackImageFormat, VK_FORMAT_UNDEFINED)),
    mActiveReadbackImageExtent(std::exchange(other.mActiveReadbackImageExtent, {})),
    mActiveReadbackRowBytes(std::exchange(other.mActiveReadbackRowBytes, 0)),
    mActiveReadbackByteCount(std::exchange(other.mActiveReadbackByteCount, 0)),
    mReadbackClassificationEligible(std::exchange(other.mReadbackClassificationEligible, false)),
    mDisposition(std::exchange(other.mDisposition, VulkanSwapchainFrameSlotDisposition::Reusable)),
    mPendingSubmissionReportedDeviceLost(std::exchange(other.mPendingSubmissionReportedDeviceLost, false)),
    mSubmissionFenceSignaled(std::exchange(other.mSubmissionFenceSignaled, true)),
    mPresentCompletionFenceSignaled(std::exchange(other.mPresentCompletionFenceSignaled, true)),
    mAcquiredImageIndex(std::exchange(other.mAcquiredImageIndex, std::nullopt)),
    mPendingPresentationOutcome(std::exchange(other.mPendingPresentationOutcome, VulkanSwapchainFrameSlotPresentationOutcome::Presented)),
    mPendingPresentResult(std::exchange(other.mPendingPresentResult, VK_SUCCESS)),
    mCancellationPhase(std::exchange(other.mCancellationPhase, CancellationPhase::Idle)),
    mCancellationSubmissionPending(std::exchange(other.mCancellationSubmissionPending, false)),
    mCancellationSubmitReportedDeviceLost(std::exchange(other.mCancellationSubmitReportedDeviceLost, false))
{
}

bool VulkanSwapchainFrameSlotGeneration::createdFor(const VulkanLogicalDeviceGeneration&          logical_device_generation,
                                                    const VulkanSwapchainConfigurationGeneration& configuration_generation,
                                                    const VulkanSwapchainGeneration&              swapchain_generation,
                                                    const VulkanSwapchainImagesGeneration&        images_generation) const noexcept
{
    const VkExtent2D drawable_extent = configuration_generation.drawableExtent();
    const VkExtent2D image_extent    = configuration_generation.imageExtent();
    return mCommandPool != VK_NULL_HANDLE && mCommandBuffer != VK_NULL_HANDLE && mImageAvailableSemaphore != VK_NULL_HANDLE &&
           mPresentationReadySemaphore != VK_NULL_HANDLE && mSubmissionFence != VK_NULL_HANDLE &&
           mPresentCompletionFence != VK_NULL_HANDLE && mLogicalDeviceGeneration == &logical_device_generation &&
           mConfigurationGeneration == &configuration_generation && mSwapchainGeneration == &swapchain_generation &&
           mImagesGeneration == &images_generation &&
           images_generation.createdFor(logical_device_generation, configuration_generation, swapchain_generation) &&
           mGetInstanceProcAddr == logical_device_generation.getInstanceProcAddr() && mInstance == logical_device_generation.instance() &&
           mSurface == logical_device_generation.surface() && mPhysicalDevice == logical_device_generation.physicalDevice() &&
           mPhysicalDeviceIndex == logical_device_generation.physicalDeviceIndex() && mDevice == logical_device_generation.device() &&
           mQueue == logical_device_generation.queue() && mQueueFamilyIndex == logical_device_generation.queueFamilyIndex() &&
           mQueueIndex == logical_device_generation.queueIndex() && mDrawableExtent.width == drawable_extent.width &&
           mDrawableExtent.height == drawable_extent.height && mImageExtent.width == image_extent.width &&
           mImageExtent.height == image_extent.height && mSwapchain == swapchain_generation.swapchain() &&
           mImageFormat == images_generation.imageFormat() && mImageCount == images_generation.imageCount();
}

VulkanSwapchainFrameSlotOperationResult VulkanSwapchainFrameSlotGeneration::resolveEmptySubmissionDispatch(
    const VulkanLogicalDeviceGeneration&          logical_device_generation,
    const VulkanSwapchainConfigurationGeneration& configuration_generation,
    const VulkanSwapchainGeneration&              swapchain_generation,
    const VulkanSwapchainImagesGeneration&        images_generation) noexcept
{
    if (mLogicalDeviceGeneration != &logical_device_generation || !valid(logical_device_generation) ||
        mGetInstanceProcAddr != logical_device_generation.getInstanceProcAddr() ||
        mInstance != logical_device_generation.instance() || mSurface != logical_device_generation.surface() ||
        mPhysicalDevice != logical_device_generation.physicalDevice() ||
        mPhysicalDeviceIndex != logical_device_generation.physicalDeviceIndex() || mDevice != logical_device_generation.device() ||
        mQueue != logical_device_generation.queue() || mQueueFamilyIndex != logical_device_generation.queueFamilyIndex() ||
        mQueueIndex != logical_device_generation.queueIndex())
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::InvalidLogicalDeviceGeneration, mDisposition);
    }

    const VkExtent2D drawable_extent = configuration_generation.drawableExtent();
    const VkExtent2D image_extent    = configuration_generation.imageExtent();
    if (mConfigurationGeneration != &configuration_generation || !belongsTo(configuration_generation, logical_device_generation) ||
        mDrawableExtent.width != drawable_extent.width || mDrawableExtent.height != drawable_extent.height ||
        mImageExtent.width != image_extent.width || mImageExtent.height != image_extent.height)
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::InvalidSwapchainConfigurationGeneration, mDisposition);
    }
    if (mSwapchainGeneration != &swapchain_generation ||
        !swapchain_generation.createdFor(logical_device_generation, configuration_generation) ||
        mSwapchain != swapchain_generation.swapchain())
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::InvalidSwapchainGeneration, mDisposition);
    }
    if (mImagesGeneration != &images_generation ||
        !images_generation.createdFor(logical_device_generation, configuration_generation, swapchain_generation) ||
        mImageFormat != images_generation.imageFormat() || mImageCount != images_generation.imageCount())
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::InvalidSwapchainImagesGeneration, mDisposition);
    }

    const bool dispatch_resolved =
        mWaitForFences && mResetCommandBuffer && mBeginCommandBuffer && mEndCommandBuffer && mResetFences && mQueueSubmit;
    if (dispatch_resolved)
    {
        return mDisposition;
    }
    if (mDisposition != VulkanSwapchainFrameSlotDisposition::Reusable)
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::InvalidDisposition, mDisposition);
    }

    const auto get_device_proc_addr = resolveInstance<PFN_vkGetDeviceProcAddr>(mGetInstanceProcAddr, mInstance, "vkGetDeviceProcAddr");
    if (!get_device_proc_addr)
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::MissingRequiredCommand, mDisposition,
                                VulkanSwapchainFrameSlotCommand::GetDeviceProcAddr);
    }

    EmptySubmissionDispatch dispatch;
    dispatch.mWaitForFences = resolveDevice<PFN_vkWaitForFences>(get_device_proc_addr, mDevice, "vkWaitForFences");
    if (!dispatch.mWaitForFences)
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::MissingRequiredCommand, mDisposition,
                                VulkanSwapchainFrameSlotCommand::WaitForFences);
    }
    dispatch.mResetCommandBuffer = resolveDevice<PFN_vkResetCommandBuffer>(get_device_proc_addr, mDevice, "vkResetCommandBuffer");
    if (!dispatch.mResetCommandBuffer)
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::MissingRequiredCommand, mDisposition,
                                VulkanSwapchainFrameSlotCommand::ResetCommandBuffer);
    }
    dispatch.mBeginCommandBuffer = resolveDevice<PFN_vkBeginCommandBuffer>(get_device_proc_addr, mDevice, "vkBeginCommandBuffer");
    if (!dispatch.mBeginCommandBuffer)
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::MissingRequiredCommand, mDisposition,
                                VulkanSwapchainFrameSlotCommand::BeginCommandBuffer);
    }
    dispatch.mEndCommandBuffer = resolveDevice<PFN_vkEndCommandBuffer>(get_device_proc_addr, mDevice, "vkEndCommandBuffer");
    if (!dispatch.mEndCommandBuffer)
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::MissingRequiredCommand, mDisposition,
                                VulkanSwapchainFrameSlotCommand::EndCommandBuffer);
    }
    dispatch.mResetFences = resolveDevice<PFN_vkResetFences>(get_device_proc_addr, mDevice, "vkResetFences");
    if (!dispatch.mResetFences)
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::MissingRequiredCommand, mDisposition,
                                VulkanSwapchainFrameSlotCommand::ResetFences);
    }
    dispatch.mQueueSubmit = resolveDevice<PFN_vkQueueSubmit>(get_device_proc_addr, mDevice, "vkQueueSubmit");
    if (!dispatch.mQueueSubmit)
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::MissingRequiredCommand, mDisposition,
                                VulkanSwapchainFrameSlotCommand::QueueSubmit);
    }

    mWaitForFences      = dispatch.mWaitForFences;
    mResetCommandBuffer = dispatch.mResetCommandBuffer;
    mBeginCommandBuffer = dispatch.mBeginCommandBuffer;
    mEndCommandBuffer   = dispatch.mEndCommandBuffer;
    mResetFences        = dispatch.mResetFences;
    mQueueSubmit        = dispatch.mQueueSubmit;
    return mDisposition;
}

VulkanSwapchainFrameSlotOperationResult VulkanSwapchainFrameSlotGeneration::resolvePresentationDispatch(
    const VulkanLogicalDeviceGeneration&          logical_device_generation,
    const VulkanSwapchainConfigurationGeneration& configuration_generation,
    const VulkanSwapchainGeneration&              swapchain_generation,
    const VulkanSwapchainImagesGeneration&        images_generation) noexcept
{
    return resolvePresentationDispatch(logical_device_generation, configuration_generation, swapchain_generation, images_generation,
                                       RecordingMode::LayoutOnly, nullptr, nullptr, nullptr, nullptr, nullptr);
}

VulkanSwapchainFrameSlotOperationResult VulkanSwapchainFrameSlotGeneration::resolveClearPresentationDispatch(
    const VulkanLogicalDeviceGeneration&          logical_device_generation,
    const VulkanSwapchainConfigurationGeneration& configuration_generation,
    const VulkanSwapchainGeneration&              swapchain_generation,
    const VulkanSwapchainImagesGeneration&        images_generation) noexcept
{
    return resolvePresentationDispatch(logical_device_generation, configuration_generation, swapchain_generation, images_generation,
                                       RecordingMode::TransferClear, nullptr, nullptr, nullptr, nullptr, nullptr);
}

VulkanSwapchainFrameSlotOperationResult VulkanSwapchainFrameSlotGeneration::resolveRenderPassPresentationDispatch(
    const VulkanLogicalDeviceGeneration&               logical_device_generation,
    const VulkanSwapchainConfigurationGeneration&      configuration_generation,
    const VulkanSwapchainGeneration&                   swapchain_generation,
    const VulkanSwapchainImagesGeneration&             images_generation,
    const VulkanSwapchainPresentationTargetGeneration& presentation_target_generation) noexcept
{
    return resolvePresentationDispatch(logical_device_generation, configuration_generation, swapchain_generation, images_generation,
                                       RecordingMode::RenderPassClear, nullptr, &presentation_target_generation, nullptr, nullptr, nullptr);
}

VulkanSwapchainFrameSlotOperationResult VulkanSwapchainFrameSlotGeneration::resolveRenderPassDrawPresentationDispatch(
    const VulkanPhysicalDeviceGeneration&                physical_device_generation,
    const VulkanLogicalDeviceGeneration&                 logical_device_generation,
    const VulkanSwapchainConfigurationGeneration&        configuration_generation,
    const VulkanSwapchainGeneration&                     swapchain_generation,
    const VulkanSwapchainImagesGeneration&               images_generation,
    const VulkanSwapchainPresentationTargetGeneration&   presentation_target_generation,
    const VulkanSwapchainPresentationPipelineGeneration& presentation_pipeline_generation,
    const VulkanUploadDestinationGeneration&             upload_destination_generation) noexcept
{
    return resolvePresentationDispatch(logical_device_generation, configuration_generation, swapchain_generation, images_generation,
                                       RecordingMode::RenderPassDraw, &physical_device_generation, &presentation_target_generation,
                                       &presentation_pipeline_generation, &upload_destination_generation, nullptr);
}

VulkanSwapchainFrameSlotOperationResult VulkanSwapchainFrameSlotGeneration::resolveRenderPassDrawReadbackPresentationDispatch(
    const VulkanPhysicalDeviceGeneration&                physical_device_generation,
    const VulkanLogicalDeviceGeneration&                 logical_device_generation,
    const VulkanSwapchainConfigurationGeneration&        configuration_generation,
    const VulkanSwapchainGeneration&                     swapchain_generation,
    const VulkanSwapchainImagesGeneration&               images_generation,
    const VulkanSwapchainPresentationTargetGeneration&   presentation_target_generation,
    const VulkanSwapchainPresentationPipelineGeneration& presentation_pipeline_generation,
    const VulkanUploadDestinationGeneration&             upload_destination_generation,
    const VulkanSwapchainReadbackGeneration&             readback_generation) noexcept
{
    return resolvePresentationDispatch(logical_device_generation, configuration_generation, swapchain_generation, images_generation,
                                       RecordingMode::RenderPassDrawReadback, &physical_device_generation, &presentation_target_generation,
                                       &presentation_pipeline_generation, &upload_destination_generation, &readback_generation);
}

VulkanSwapchainFrameSlotOperationResult VulkanSwapchainFrameSlotGeneration::resolvePresentationDispatch(
    const VulkanLogicalDeviceGeneration&                 logical_device_generation,
    const VulkanSwapchainConfigurationGeneration&        configuration_generation,
    const VulkanSwapchainGeneration&                     swapchain_generation,
    const VulkanSwapchainImagesGeneration&               images_generation,
    RecordingMode                                        recording_mode,
    const VulkanPhysicalDeviceGeneration*                physical_device_generation,
    const VulkanSwapchainPresentationTargetGeneration*   presentation_target_generation,
    const VulkanSwapchainPresentationPipelineGeneration* presentation_pipeline_generation,
    const VulkanUploadDestinationGeneration*             upload_destination_generation,
    const VulkanSwapchainReadbackGeneration*             readback_generation) noexcept
{
    if (mLogicalDeviceGeneration != &logical_device_generation || !valid(logical_device_generation) ||
        mGetInstanceProcAddr != logical_device_generation.getInstanceProcAddr() ||
        mInstance != logical_device_generation.instance() || mSurface != logical_device_generation.surface() ||
        mPhysicalDevice != logical_device_generation.physicalDevice() ||
        mPhysicalDeviceIndex != logical_device_generation.physicalDeviceIndex() || mDevice != logical_device_generation.device() ||
        mQueue != logical_device_generation.queue() || mQueueFamilyIndex != logical_device_generation.queueFamilyIndex() ||
        mQueueIndex != logical_device_generation.queueIndex())
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::InvalidLogicalDeviceGeneration, mDisposition);
    }
    if (!logical_device_generation.swapchainMaintenance1Enabled())
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::SwapchainMaintenance1NotEnabled, mDisposition);
    }

    const VkExtent2D drawable_extent = configuration_generation.drawableExtent();
    const VkExtent2D image_extent    = configuration_generation.imageExtent();
    if (mConfigurationGeneration != &configuration_generation || !belongsTo(configuration_generation, logical_device_generation) ||
        mDrawableExtent.width != drawable_extent.width || mDrawableExtent.height != drawable_extent.height ||
        mImageExtent.width != image_extent.width || mImageExtent.height != image_extent.height)
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::InvalidSwapchainConfigurationGeneration, mDisposition);
    }
    if (mSwapchainGeneration != &swapchain_generation ||
        !swapchain_generation.createdFor(logical_device_generation, configuration_generation) ||
        mSwapchain != swapchain_generation.swapchain())
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::InvalidSwapchainGeneration, mDisposition);
    }
    if (mImagesGeneration != &images_generation ||
        !images_generation.createdFor(logical_device_generation, configuration_generation, swapchain_generation) ||
        mImageFormat != images_generation.imageFormat() || mImageCount != images_generation.imageCount())
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::InvalidSwapchainImagesGeneration, mDisposition);
    }

    const bool readback_required = recording_mode == RecordingMode::RenderPassDrawReadback;
    const bool render_pass_required =
        recording_mode == RecordingMode::RenderPassClear || recording_mode == RecordingMode::RenderPassDraw || readback_required;
    const bool draw_required = recording_mode == RecordingMode::RenderPassDraw || readback_required;
    if (render_pass_required && (!presentation_target_generation ||
                                 !presentation_target_generation->createdFor(logical_device_generation, configuration_generation,
                                                                             swapchain_generation, images_generation) ||
                                 (mPresentationTargetGeneration && mPresentationTargetGeneration != presentation_target_generation)))
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::InvalidSwapchainPresentationTargetGeneration, mDisposition);
    }
    if (draw_required &&
        (!presentation_pipeline_generation ||
         !presentation_pipeline_generation->createdFor(logical_device_generation, configuration_generation, swapchain_generation,
                                                       images_generation, *presentation_target_generation) ||
         presentation_pipeline_generation->pipelineLayout() == VK_NULL_HANDLE ||
         presentation_pipeline_generation->pipeline() == VK_NULL_HANDLE ||
         (mPresentationPipelineGeneration && (mPresentationPipelineGeneration != presentation_pipeline_generation ||
                                              mPresentationPipelineLayout != presentation_pipeline_generation->pipelineLayout() ||
                                              mPresentationPipeline != presentation_pipeline_generation->pipeline()))))
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::InvalidSwapchainPresentationPipelineGeneration, mDisposition);
    }
    const VkPipelineLayout presentation_pipeline_layout =
        draw_required ? presentation_pipeline_generation->pipelineLayout() : VK_NULL_HANDLE;
    const VkPipeline presentation_pipeline = draw_required ? presentation_pipeline_generation->pipeline() : VK_NULL_HANDLE;
    if (draw_required && (!physical_device_generation || !upload_destination_generation ||
                          !validUploadDestination(*upload_destination_generation, *physical_device_generation, logical_device_generation)))
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::InvalidUploadDestinationGeneration, mDisposition);
    }
    if (readback_required &&
        (!physical_device_generation || !readback_generation ||
         !readback_generation->createdFor(*physical_device_generation, logical_device_generation, configuration_generation,
                                          swapchain_generation, images_generation) ||
         readback_generation->buffer() == VK_NULL_HANDLE || !readback_generation->isMapped() ||
         readback_generation->imageFormat() != mImageFormat || readback_generation->imageExtent().width != mImageExtent.width ||
         readback_generation->imageExtent().height != mImageExtent.height || readback_generation->rowBytes() == 0 ||
         readback_generation->byteCount() == 0))
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::InvalidSwapchainReadbackGeneration, mDisposition);
    }

    const bool transfer_clear_required = recording_mode == RecordingMode::TransferClear;
    const bool dispatch_resolved =
        mWaitForFences && mResetCommandBuffer && mBeginCommandBuffer && mEndCommandBuffer && mResetFences && mQueueSubmit &&
        mAcquireNextImage && mCmdPipelineBarrier && (!transfer_clear_required || mCmdClearColorImage) &&
        (!render_pass_required ||
         (mCmdBeginRenderPass && mCmdEndRenderPass && mPresentationTargetGeneration == presentation_target_generation)) &&
        (!draw_required || (mCmdBindPipeline && mCmdBindVertexBuffers && mCmdSetViewport && mCmdSetScissor && mCmdDraw &&
                            mPresentationPipelineGeneration == presentation_pipeline_generation &&
                            mPresentationPipelineLayout == presentation_pipeline_generation->pipelineLayout() &&
                            mPresentationPipeline == presentation_pipeline_generation->pipeline() &&
                            mUploadDestinationPhysicalDeviceGeneration == physical_device_generation)) &&
        (!readback_required || (mCmdCopyImageToBuffer && mReadbackPhysicalDeviceGeneration == physical_device_generation &&
                                mResolvedReadbackGeneration == readback_generation)) &&
        mQueuePresent && mReleaseSwapchainImages;
    if (dispatch_resolved)
    {
        return mDisposition;
    }
    if (mDisposition != VulkanSwapchainFrameSlotDisposition::Reusable)
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::InvalidDisposition, mDisposition);
    }

    const auto get_device_proc_addr = resolveInstance<PFN_vkGetDeviceProcAddr>(mGetInstanceProcAddr, mInstance, "vkGetDeviceProcAddr");
    if (!get_device_proc_addr)
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::MissingRequiredCommand, mDisposition,
                                VulkanSwapchainFrameSlotCommand::GetDeviceProcAddr);
    }

    PresentationDispatch dispatch;
    dispatch.mSubmission.mWaitForFences =
        resolveDevice<PFN_vkWaitForFences>(get_device_proc_addr, mDevice, "vkWaitForFences");
    if (!dispatch.mSubmission.mWaitForFences)
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::MissingRequiredCommand, mDisposition,
                                VulkanSwapchainFrameSlotCommand::WaitForFences);
    }
    dispatch.mSubmission.mResetCommandBuffer =
        resolveDevice<PFN_vkResetCommandBuffer>(get_device_proc_addr, mDevice, "vkResetCommandBuffer");
    if (!dispatch.mSubmission.mResetCommandBuffer)
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::MissingRequiredCommand, mDisposition,
                                VulkanSwapchainFrameSlotCommand::ResetCommandBuffer);
    }
    dispatch.mSubmission.mBeginCommandBuffer =
        resolveDevice<PFN_vkBeginCommandBuffer>(get_device_proc_addr, mDevice, "vkBeginCommandBuffer");
    if (!dispatch.mSubmission.mBeginCommandBuffer)
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::MissingRequiredCommand, mDisposition,
                                VulkanSwapchainFrameSlotCommand::BeginCommandBuffer);
    }
    dispatch.mSubmission.mEndCommandBuffer =
        resolveDevice<PFN_vkEndCommandBuffer>(get_device_proc_addr, mDevice, "vkEndCommandBuffer");
    if (!dispatch.mSubmission.mEndCommandBuffer)
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::MissingRequiredCommand, mDisposition,
                                VulkanSwapchainFrameSlotCommand::EndCommandBuffer);
    }
    dispatch.mSubmission.mResetFences = resolveDevice<PFN_vkResetFences>(get_device_proc_addr, mDevice, "vkResetFences");
    if (!dispatch.mSubmission.mResetFences)
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::MissingRequiredCommand, mDisposition,
                                VulkanSwapchainFrameSlotCommand::ResetFences);
    }
    dispatch.mSubmission.mQueueSubmit = resolveDevice<PFN_vkQueueSubmit>(get_device_proc_addr, mDevice, "vkQueueSubmit");
    if (!dispatch.mSubmission.mQueueSubmit)
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::MissingRequiredCommand, mDisposition,
                                VulkanSwapchainFrameSlotCommand::QueueSubmit);
    }
    dispatch.mAcquireNextImage =
        resolveDevice<PFN_vkAcquireNextImageKHR>(get_device_proc_addr, mDevice, "vkAcquireNextImageKHR");
    if (!dispatch.mAcquireNextImage)
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::MissingRequiredCommand, mDisposition,
                                VulkanSwapchainFrameSlotCommand::AcquireNextImage);
    }
    dispatch.mCmdPipelineBarrier =
        resolveDevice<PFN_vkCmdPipelineBarrier>(get_device_proc_addr, mDevice, "vkCmdPipelineBarrier");
    if (!dispatch.mCmdPipelineBarrier)
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::MissingRequiredCommand, mDisposition,
                                VulkanSwapchainFrameSlotCommand::CmdPipelineBarrier);
    }
    if (transfer_clear_required)
    {
        dispatch.mCmdClearColorImage =
            resolveDevice<PFN_vkCmdClearColorImage>(get_device_proc_addr, mDevice, "vkCmdClearColorImage");
        if (!dispatch.mCmdClearColorImage)
        {
            return operationFailure(VulkanSwapchainFrameSlotOperationCode::MissingRequiredCommand, mDisposition,
                                    VulkanSwapchainFrameSlotCommand::CmdClearColorImage);
        }
    }
    if (render_pass_required)
    {
        dispatch.mCmdBeginRenderPass =
            resolveDevice<PFN_vkCmdBeginRenderPass>(get_device_proc_addr, mDevice, "vkCmdBeginRenderPass");
        if (!dispatch.mCmdBeginRenderPass)
        {
            return operationFailure(VulkanSwapchainFrameSlotOperationCode::MissingRequiredCommand, mDisposition,
                                    VulkanSwapchainFrameSlotCommand::CmdBeginRenderPass);
        }
        dispatch.mCmdEndRenderPass =
            resolveDevice<PFN_vkCmdEndRenderPass>(get_device_proc_addr, mDevice, "vkCmdEndRenderPass");
        if (!dispatch.mCmdEndRenderPass)
        {
            return operationFailure(VulkanSwapchainFrameSlotOperationCode::MissingRequiredCommand, mDisposition,
                                    VulkanSwapchainFrameSlotCommand::CmdEndRenderPass);
        }
    }
    if (draw_required)
    {
        dispatch.mCmdBindPipeline = resolveDevice<PFN_vkCmdBindPipeline>(get_device_proc_addr, mDevice, "vkCmdBindPipeline");
        if (!dispatch.mCmdBindPipeline)
        {
            return operationFailure(VulkanSwapchainFrameSlotOperationCode::MissingRequiredCommand, mDisposition,
                                    VulkanSwapchainFrameSlotCommand::CmdBindPipeline);
        }
        dispatch.mCmdBindVertexBuffers = resolveDevice<PFN_vkCmdBindVertexBuffers>(get_device_proc_addr, mDevice, "vkCmdBindVertexBuffers");
        if (!dispatch.mCmdBindVertexBuffers)
        {
            return operationFailure(VulkanSwapchainFrameSlotOperationCode::MissingRequiredCommand, mDisposition,
                                    VulkanSwapchainFrameSlotCommand::CmdBindVertexBuffers);
        }
        dispatch.mCmdSetViewport = resolveDevice<PFN_vkCmdSetViewport>(get_device_proc_addr, mDevice, "vkCmdSetViewport");
        if (!dispatch.mCmdSetViewport)
        {
            return operationFailure(VulkanSwapchainFrameSlotOperationCode::MissingRequiredCommand, mDisposition,
                                    VulkanSwapchainFrameSlotCommand::CmdSetViewport);
        }
        dispatch.mCmdSetScissor = resolveDevice<PFN_vkCmdSetScissor>(get_device_proc_addr, mDevice, "vkCmdSetScissor");
        if (!dispatch.mCmdSetScissor)
        {
            return operationFailure(VulkanSwapchainFrameSlotOperationCode::MissingRequiredCommand, mDisposition,
                                    VulkanSwapchainFrameSlotCommand::CmdSetScissor);
        }
        dispatch.mCmdDraw = resolveDevice<PFN_vkCmdDraw>(get_device_proc_addr, mDevice, "vkCmdDraw");
        if (!dispatch.mCmdDraw)
        {
            return operationFailure(VulkanSwapchainFrameSlotOperationCode::MissingRequiredCommand, mDisposition,
                                    VulkanSwapchainFrameSlotCommand::CmdDraw);
        }
    }
    if (readback_required)
    {
        dispatch.mCmdCopyImageToBuffer = resolveDevice<PFN_vkCmdCopyImageToBuffer>(get_device_proc_addr, mDevice, "vkCmdCopyImageToBuffer");
        if (!dispatch.mCmdCopyImageToBuffer)
        {
            return operationFailure(VulkanSwapchainFrameSlotOperationCode::MissingRequiredCommand, mDisposition,
                                    VulkanSwapchainFrameSlotCommand::CmdCopyImageToBuffer);
        }
    }
    dispatch.mQueuePresent = resolveDevice<PFN_vkQueuePresentKHR>(get_device_proc_addr, mDevice, "vkQueuePresentKHR");
    if (!dispatch.mQueuePresent)
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::MissingRequiredCommand, mDisposition,
                                VulkanSwapchainFrameSlotCommand::QueuePresent);
    }
    dispatch.mReleaseSwapchainImages =
        resolveDevice<PFN_vkReleaseSwapchainImagesKHR>(get_device_proc_addr, mDevice, "vkReleaseSwapchainImagesKHR");
    if (!dispatch.mReleaseSwapchainImages)
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::MissingRequiredCommand, mDisposition,
                                VulkanSwapchainFrameSlotCommand::ReleaseSwapchainImages);
    }

    if (render_pass_required && !presentation_target_generation->createdFor(logical_device_generation, configuration_generation,
                                                                            swapchain_generation, images_generation))
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::InvalidSwapchainPresentationTargetGeneration, mDisposition);
    }
    if (draw_required &&
        (!presentation_pipeline_generation->createdFor(logical_device_generation, configuration_generation, swapchain_generation,
                                                       images_generation, *presentation_target_generation) ||
         presentation_pipeline_generation->pipelineLayout() != presentation_pipeline_layout ||
         presentation_pipeline_generation->pipeline() != presentation_pipeline))
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::InvalidSwapchainPresentationPipelineGeneration, mDisposition);
    }
    if (draw_required && !validUploadDestination(*upload_destination_generation, *physical_device_generation, logical_device_generation))
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::InvalidUploadDestinationGeneration, mDisposition);
    }
    if (readback_required &&
        (!readback_generation->createdFor(*physical_device_generation, logical_device_generation, configuration_generation,
                                          swapchain_generation, images_generation) ||
         readback_generation->buffer() == VK_NULL_HANDLE || !readback_generation->isMapped() ||
         readback_generation->imageFormat() != mImageFormat || readback_generation->imageExtent().width != mImageExtent.width ||
         readback_generation->imageExtent().height != mImageExtent.height || readback_generation->rowBytes() == 0 ||
         readback_generation->byteCount() == 0))
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::InvalidSwapchainReadbackGeneration, mDisposition);
    }

    mWaitForFences          = dispatch.mSubmission.mWaitForFences;
    mResetCommandBuffer     = dispatch.mSubmission.mResetCommandBuffer;
    mBeginCommandBuffer     = dispatch.mSubmission.mBeginCommandBuffer;
    mEndCommandBuffer       = dispatch.mSubmission.mEndCommandBuffer;
    mResetFences            = dispatch.mSubmission.mResetFences;
    mQueueSubmit            = dispatch.mSubmission.mQueueSubmit;
    mAcquireNextImage       = dispatch.mAcquireNextImage;
    mCmdPipelineBarrier     = dispatch.mCmdPipelineBarrier;
    if (transfer_clear_required)
    {
        mCmdClearColorImage = dispatch.mCmdClearColorImage;
    }
    if (render_pass_required)
    {
        mCmdBeginRenderPass             = dispatch.mCmdBeginRenderPass;
        mCmdEndRenderPass               = dispatch.mCmdEndRenderPass;
        mPresentationTargetGeneration   = presentation_target_generation;
    }
    if (draw_required)
    {
        mCmdBindPipeline                = dispatch.mCmdBindPipeline;
        mCmdBindVertexBuffers                      = dispatch.mCmdBindVertexBuffers;
        mCmdSetViewport                 = dispatch.mCmdSetViewport;
        mCmdSetScissor                  = dispatch.mCmdSetScissor;
        mCmdDraw                        = dispatch.mCmdDraw;
        mPresentationPipelineGeneration = presentation_pipeline_generation;
        mPresentationPipelineLayout     = presentation_pipeline_layout;
        mPresentationPipeline           = presentation_pipeline;
        mUploadDestinationPhysicalDeviceGeneration = physical_device_generation;
    }
    if (readback_required)
    {
        mCmdCopyImageToBuffer             = dispatch.mCmdCopyImageToBuffer;
        mReadbackPhysicalDeviceGeneration = physical_device_generation;
        mResolvedReadbackGeneration       = readback_generation;
    }
    mQueuePresent           = dispatch.mQueuePresent;
    mReleaseSwapchainImages = dispatch.mReleaseSwapchainImages;
    return mDisposition;
}

VulkanSwapchainFrameSlotOperationResult VulkanSwapchainFrameSlotGeneration::executeEmptySubmission() noexcept
{
    if (mDisposition != VulkanSwapchainFrameSlotDisposition::Reusable)
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::InvalidDisposition, mDisposition);
    }

    if (!mWaitForFences)
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::MissingRequiredCommand, mDisposition,
                                VulkanSwapchainFrameSlotCommand::WaitForFences);
    }
    if (!mResetCommandBuffer)
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::MissingRequiredCommand, mDisposition,
                                VulkanSwapchainFrameSlotCommand::ResetCommandBuffer);
    }
    if (!mBeginCommandBuffer)
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::MissingRequiredCommand, mDisposition,
                                VulkanSwapchainFrameSlotCommand::BeginCommandBuffer);
    }
    if (!mEndCommandBuffer)
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::MissingRequiredCommand, mDisposition,
                                VulkanSwapchainFrameSlotCommand::EndCommandBuffer);
    }
    if (!mResetFences)
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::MissingRequiredCommand, mDisposition,
                                VulkanSwapchainFrameSlotCommand::ResetFences);
    }
    if (!mQueueSubmit)
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::MissingRequiredCommand, mDisposition,
                                VulkanSwapchainFrameSlotCommand::QueueSubmit);
    }

    VkResult result = mWaitForFences(mDevice, 1, &mSubmissionFence, VK_TRUE, std::numeric_limits<std::uint64_t>::max());
    if (result != VK_SUCCESS)
    {
        mDisposition = result == VK_ERROR_DEVICE_LOST ? VulkanSwapchainFrameSlotDisposition::DeviceLost
                                                      : VulkanSwapchainFrameSlotDisposition::Reusable;
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::CommandFailure, mDisposition,
                                VulkanSwapchainFrameSlotCommand::WaitForFences, result);
    }

    result = mResetCommandBuffer(mCommandBuffer, 0);
    if (result != VK_SUCCESS)
    {
        mDisposition = result == VK_ERROR_DEVICE_LOST ? VulkanSwapchainFrameSlotDisposition::DeviceLost
                                                      : VulkanSwapchainFrameSlotDisposition::ResetRequired;
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::CommandFailure, mDisposition,
                                VulkanSwapchainFrameSlotCommand::ResetCommandBuffer, result);
    }

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    result           = mBeginCommandBuffer(mCommandBuffer, &begin_info);
    if (result != VK_SUCCESS)
    {
        mDisposition = result == VK_ERROR_DEVICE_LOST ? VulkanSwapchainFrameSlotDisposition::DeviceLost
                                                      : VulkanSwapchainFrameSlotDisposition::ResetRequired;
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::CommandFailure, mDisposition,
                                VulkanSwapchainFrameSlotCommand::BeginCommandBuffer, result);
    }

    result = mEndCommandBuffer(mCommandBuffer);
    if (result != VK_SUCCESS)
    {
        mDisposition = result == VK_ERROR_DEVICE_LOST ? VulkanSwapchainFrameSlotDisposition::DeviceLost
                                                      : VulkanSwapchainFrameSlotDisposition::ResetRequired;
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::CommandFailure, mDisposition,
                                VulkanSwapchainFrameSlotCommand::EndCommandBuffer, result);
    }

    result = mResetFences(mDevice, 1, &mSubmissionFence);
    if (result != VK_SUCCESS)
    {
        mDisposition = result == VK_ERROR_DEVICE_LOST ? VulkanSwapchainFrameSlotDisposition::DeviceLost
                                                      : VulkanSwapchainFrameSlotDisposition::ResetRequired;
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::CommandFailure, mDisposition,
                                VulkanSwapchainFrameSlotCommand::ResetFences, result);
    }

    VkSubmitInfo submit_info{};
    submit_info.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers    = &mCommandBuffer;

    result = mQueueSubmit(mQueue, 1, &submit_info, mSubmissionFence);
    if (result != VK_SUCCESS)
    {
        // Device loss is equivalent to submit success for pending and in-use
        // accounting. A completion wait must retire that possible use before
        // teardown can destroy the fence or command pool.
        mPendingSubmissionReportedDeviceLost = result == VK_ERROR_DEVICE_LOST;
        mDisposition                         = mPendingSubmissionReportedDeviceLost ? VulkanSwapchainFrameSlotDisposition::Pending
                                                                                    : VulkanSwapchainFrameSlotDisposition::ResetRequired;
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::CommandFailure, mDisposition,
                                VulkanSwapchainFrameSlotCommand::QueueSubmit, result);
    }

    mPendingSubmissionReportedDeviceLost = false;
    mDisposition                         = VulkanSwapchainFrameSlotDisposition::Pending;
    result = mWaitForFences(mDevice, 1, &mSubmissionFence, VK_TRUE, std::numeric_limits<std::uint64_t>::max());
    if (result == VK_SUCCESS)
    {
        mDisposition                         = mPendingSubmissionReportedDeviceLost ? VulkanSwapchainFrameSlotDisposition::DeviceLost
                                                                                    : VulkanSwapchainFrameSlotDisposition::Reusable;
        mPendingSubmissionReportedDeviceLost = false;
        return mDisposition;
    }
    if (result == VK_ERROR_DEVICE_LOST)
    {
        mPendingSubmissionReportedDeviceLost = false;
        mDisposition                         = VulkanSwapchainFrameSlotDisposition::DeviceLost;
    }
    return operationFailure(VulkanSwapchainFrameSlotOperationCode::CommandFailure, mDisposition,
                            VulkanSwapchainFrameSlotCommand::WaitForFences, result);
}

VulkanSwapchainFrameSlotOperationResult VulkanSwapchainFrameSlotGeneration::retryEmptySubmissionCompletion() noexcept
{
    if (mDisposition != VulkanSwapchainFrameSlotDisposition::Pending)
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::InvalidDisposition, mDisposition);
    }
    if (!mWaitForFences)
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::MissingRequiredCommand, mDisposition,
                                VulkanSwapchainFrameSlotCommand::WaitForFences);
    }

    const VkResult result = mWaitForFences(mDevice, 1, &mSubmissionFence, VK_TRUE, std::numeric_limits<std::uint64_t>::max());
    if (result == VK_SUCCESS)
    {
        mDisposition                         = mPendingSubmissionReportedDeviceLost ? VulkanSwapchainFrameSlotDisposition::DeviceLost
                                                                                    : VulkanSwapchainFrameSlotDisposition::Reusable;
        mPendingSubmissionReportedDeviceLost = false;
        return mDisposition;
    }
    if (result == VK_ERROR_DEVICE_LOST)
    {
        mPendingSubmissionReportedDeviceLost = false;
        mDisposition                         = VulkanSwapchainFrameSlotDisposition::DeviceLost;
    }
    return operationFailure(VulkanSwapchainFrameSlotOperationCode::CommandFailure, mDisposition,
                            VulkanSwapchainFrameSlotCommand::WaitForFences, result);
}

VulkanSwapchainFrameSlotPresentationResult VulkanSwapchainFrameSlotGeneration::executeAcquireToPresent() noexcept
{
    return executeAcquireToPresent(RecordingMode::LayoutOnly, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
}

VulkanSwapchainFrameSlotPresentationResult VulkanSwapchainFrameSlotGeneration::executeAcquireClearToPresent(
    const VulkanSwapchainFrameClearColor& clear_color) noexcept
{
    if (!validClearColor(clear_color))
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::InvalidClearColor, mDisposition);
    }
    return executeAcquireToPresent(RecordingMode::TransferClear, &clear_color, nullptr, nullptr, nullptr, nullptr, nullptr);
}

VulkanSwapchainFrameSlotPresentationResult VulkanSwapchainFrameSlotGeneration::executeAcquireRenderPassClearToPresent(
    const VulkanSwapchainPresentationTargetGeneration& presentation_target_generation,
    const VulkanSwapchainFrameClearColor&               clear_color) noexcept
{
    if (!validClearColor(clear_color))
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::InvalidClearColor, mDisposition);
    }
    return executeAcquireToPresent(RecordingMode::RenderPassClear, &clear_color, nullptr, &presentation_target_generation, nullptr, nullptr,
                                   nullptr);
}

VulkanSwapchainFrameSlotPresentationResult VulkanSwapchainFrameSlotGeneration::executeAcquireRenderPassDrawToPresent(
    const VulkanPhysicalDeviceGeneration&                physical_device_generation,
    const VulkanSwapchainPresentationTargetGeneration&   presentation_target_generation,
    const VulkanSwapchainPresentationPipelineGeneration& presentation_pipeline_generation,
    const VulkanUploadDestinationGeneration&             upload_destination_generation,
    const VulkanSwapchainFrameClearColor&                clear_color) noexcept
{
    if (!validClearColor(clear_color))
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::InvalidClearColor, mDisposition);
    }
    return executeAcquireToPresent(RecordingMode::RenderPassDraw, &clear_color, &physical_device_generation,
                                   &presentation_target_generation, &presentation_pipeline_generation, &upload_destination_generation,
                                   nullptr);
}

VulkanSwapchainFrameSlotPresentationResult VulkanSwapchainFrameSlotGeneration::executeAcquireRenderPassDrawReadbackToPresent(
    const VulkanPhysicalDeviceGeneration&                physical_device_generation,
    const VulkanSwapchainPresentationTargetGeneration&   presentation_target_generation,
    const VulkanSwapchainPresentationPipelineGeneration& presentation_pipeline_generation,
    const VulkanUploadDestinationGeneration&             upload_destination_generation,
    VulkanSwapchainReadbackGeneration&                   readback_generation) noexcept
{
    return executeAcquireToPresent(RecordingMode::RenderPassDrawReadback, nullptr, &physical_device_generation,
                                   &presentation_target_generation, &presentation_pipeline_generation, &upload_destination_generation,
                                   &readback_generation);
}

bool VulkanSwapchainFrameSlotGeneration::activeUploadDestinationMatchesSnapshot() const noexcept
{
    if (!mActiveUploadDestinationGeneration || !mUploadDestinationPhysicalDeviceGeneration || !mLogicalDeviceGeneration)
    {
        return false;
    }
    const auto& destination = *mActiveUploadDestinationGeneration;
    return validUploadDestination(destination, *mUploadDestinationPhysicalDeviceGeneration, *mLogicalDeviceGeneration) &&
           destination.resourceHandle() == mActiveUploadDestinationResourceHandle &&
           destination.expectedContentIdentity() == mActiveUploadDestinationExpectedIdentity &&
           destination.residentContentIdentity() == mActiveUploadDestinationResidentIdentity && destination.isResident() &&
           destination.buffer() == mActiveUploadDestinationBuffer && destination.memory() == mActiveUploadDestinationMemory &&
           destination.byteCount() == mActiveUploadDestinationByteCount && destination.usage() == mActiveUploadDestinationUsage &&
           destination.allocationSize() == mActiveUploadDestinationAllocationSize &&
           destination.memoryTypeIndex() == mActiveUploadDestinationMemoryTypeIndex &&
           destination.memoryPropertyFlags() == mActiveUploadDestinationMemoryPropertyFlags && destination.isDeviceLocal() &&
           !destination.isMapped();
}

void VulkanSwapchainFrameSlotGeneration::clearActiveUploadDestination() noexcept
{
    mActiveUploadDestinationGeneration          = nullptr;
    mActiveUploadDestinationResourceHandle      = {};
    mActiveUploadDestinationExpectedIdentity    = 0;
    mActiveUploadDestinationResidentIdentity    = 0;
    mActiveUploadDestinationBuffer              = VK_NULL_HANDLE;
    mActiveUploadDestinationMemory              = VK_NULL_HANDLE;
    mActiveUploadDestinationByteCount           = 0;
    mActiveUploadDestinationUsage               = 0;
    mActiveUploadDestinationAllocationSize      = 0;
    mActiveUploadDestinationMemoryTypeIndex     = 0;
    mActiveUploadDestinationMemoryPropertyFlags = 0;
}

bool VulkanSwapchainFrameSlotGeneration::activeReadbackMatchesSnapshot() const noexcept
{
    if (!mActiveReadbackGeneration)
    {
        return false;
    }
    const VkExtent2D extent = mActiveReadbackGeneration->imageExtent();
    return mResolvedReadbackGeneration == mActiveReadbackGeneration && mActiveReadbackGeneration->buffer() == mActiveReadbackBuffer &&
           mActiveReadbackGeneration->isMapped() && mActiveReadbackGeneration->imageFormat() == mActiveReadbackImageFormat &&
           extent.width == mActiveReadbackImageExtent.width && extent.height == mActiveReadbackImageExtent.height &&
           mActiveReadbackGeneration->rowBytes() == mActiveReadbackRowBytes &&
           mActiveReadbackGeneration->byteCount() == mActiveReadbackByteCount;
}

void VulkanSwapchainFrameSlotGeneration::clearActiveReadback() noexcept
{
    mActiveReadbackGeneration       = nullptr;
    mActiveReadbackBuffer           = VK_NULL_HANDLE;
    mActiveReadbackImageFormat      = VK_FORMAT_UNDEFINED;
    mActiveReadbackImageExtent      = {};
    mActiveReadbackRowBytes         = 0;
    mActiveReadbackByteCount        = 0;
    mReadbackClassificationEligible = false;
}

VulkanSwapchainFrameSlotPresentationResult VulkanSwapchainFrameSlotGeneration::executeAcquireToPresent(
    RecordingMode                                        recording_mode,
    const VulkanSwapchainFrameClearColor*                clear_color,
    const VulkanPhysicalDeviceGeneration*                physical_device_generation,
    const VulkanSwapchainPresentationTargetGeneration*   presentation_target_generation,
    const VulkanSwapchainPresentationPipelineGeneration* presentation_pipeline_generation,
    const VulkanUploadDestinationGeneration*             upload_destination_generation,
    VulkanSwapchainReadbackGeneration*                   readback_generation) noexcept
{
    if (mDisposition != VulkanSwapchainFrameSlotDisposition::Reusable)
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::InvalidDisposition, mDisposition);
    }

    const bool readback_required = recording_mode == RecordingMode::RenderPassDrawReadback;
    const bool render_pass_required =
        recording_mode == RecordingMode::RenderPassClear || recording_mode == RecordingMode::RenderPassDraw || readback_required;
    const bool draw_required = recording_mode == RecordingMode::RenderPassDraw || readback_required;
    if (render_pass_required && (!presentation_target_generation || mPresentationTargetGeneration != presentation_target_generation ||
                                 !mLogicalDeviceGeneration || !mConfigurationGeneration || !mSwapchainGeneration || !mImagesGeneration ||
                                 !presentation_target_generation->createdFor(*mLogicalDeviceGeneration, *mConfigurationGeneration,
                                                                             *mSwapchainGeneration, *mImagesGeneration)))
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::InvalidSwapchainPresentationTargetGeneration, mDisposition);
    }
    if (draw_required &&
        (!presentation_pipeline_generation || mPresentationPipelineGeneration != presentation_pipeline_generation ||
         mPresentationPipelineLayout == VK_NULL_HANDLE || mPresentationPipeline == VK_NULL_HANDLE ||
         presentation_pipeline_generation->pipelineLayout() != mPresentationPipelineLayout ||
         presentation_pipeline_generation->pipeline() != mPresentationPipeline ||
         !presentation_pipeline_generation->createdFor(*mLogicalDeviceGeneration, *mConfigurationGeneration, *mSwapchainGeneration,
                                                       *mImagesGeneration, *presentation_target_generation)))
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::InvalidSwapchainPresentationPipelineGeneration, mDisposition);
    }
    if (draw_required && (!physical_device_generation || mUploadDestinationPhysicalDeviceGeneration != physical_device_generation ||
                          !upload_destination_generation || !mLogicalDeviceGeneration ||
                          !validUploadDestination(*upload_destination_generation, *physical_device_generation, *mLogicalDeviceGeneration)))
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::InvalidUploadDestinationGeneration, mDisposition);
    }
    if (readback_required &&
        (!physical_device_generation || mReadbackPhysicalDeviceGeneration != physical_device_generation || !readback_generation ||
         mResolvedReadbackGeneration != readback_generation || !mLogicalDeviceGeneration || !mConfigurationGeneration ||
         !mSwapchainGeneration || !mImagesGeneration ||
         !readback_generation->createdFor(*mReadbackPhysicalDeviceGeneration, *mLogicalDeviceGeneration, *mConfigurationGeneration,
                                          *mSwapchainGeneration, *mImagesGeneration) ||
         readback_generation->buffer() == VK_NULL_HANDLE || !readback_generation->isMapped() ||
         readback_generation->imageFormat() != mImageFormat || readback_generation->imageExtent().width != mImageExtent.width ||
         readback_generation->imageExtent().height != mImageExtent.height || readback_generation->rowBytes() == 0 ||
         readback_generation->byteCount() == 0))
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::InvalidSwapchainReadbackGeneration, mDisposition);
    }

    VulkanSwapchainFrameClearColor clear_color_snapshot;
    if (readback_required)
    {
        clear_color_snapshot = VulkanSwapchainFrameClearColor{ { 1.0f, 0.0f, 0.0f, 1.0f } };
    }
    else if (clear_color)
    {
        clear_color_snapshot = *clear_color;
    }

    if (draw_required)
    {
        mActiveUploadDestinationGeneration          = upload_destination_generation;
        mActiveUploadDestinationResourceHandle      = upload_destination_generation->resourceHandle();
        mActiveUploadDestinationExpectedIdentity    = upload_destination_generation->expectedContentIdentity();
        mActiveUploadDestinationResidentIdentity    = upload_destination_generation->residentContentIdentity();
        mActiveUploadDestinationBuffer              = upload_destination_generation->buffer();
        mActiveUploadDestinationMemory              = upload_destination_generation->memory();
        mActiveUploadDestinationByteCount           = upload_destination_generation->byteCount();
        mActiveUploadDestinationUsage               = upload_destination_generation->usage();
        mActiveUploadDestinationAllocationSize      = upload_destination_generation->allocationSize();
        mActiveUploadDestinationMemoryTypeIndex     = upload_destination_generation->memoryTypeIndex();
        mActiveUploadDestinationMemoryPropertyFlags = upload_destination_generation->memoryPropertyFlags();
    }

    if (readback_required)
    {
        mActiveReadbackGeneration       = readback_generation;
        mActiveReadbackBuffer           = readback_generation->buffer();
        mActiveReadbackImageFormat      = readback_generation->imageFormat();
        mActiveReadbackImageExtent      = readback_generation->imageExtent();
        mActiveReadbackRowBytes         = readback_generation->rowBytes();
        mActiveReadbackByteCount        = readback_generation->byteCount();
        mReadbackClassificationEligible = false;
    }

    const auto missing_command = [this, recording_mode, render_pass_required, draw_required,
                                  readback_required]() -> std::optional<VulkanSwapchainFrameSlotCommand>
    {
        if (!mWaitForFences)
            return VulkanSwapchainFrameSlotCommand::WaitForFences;
        if (!mAcquireNextImage)
            return VulkanSwapchainFrameSlotCommand::AcquireNextImage;
        if (!mResetCommandBuffer)
            return VulkanSwapchainFrameSlotCommand::ResetCommandBuffer;
        if (!mBeginCommandBuffer)
            return VulkanSwapchainFrameSlotCommand::BeginCommandBuffer;
        if (!mCmdPipelineBarrier)
            return VulkanSwapchainFrameSlotCommand::CmdPipelineBarrier;
        if (recording_mode == RecordingMode::TransferClear && !mCmdClearColorImage)
            return VulkanSwapchainFrameSlotCommand::CmdClearColorImage;
        if (render_pass_required && !mCmdBeginRenderPass)
            return VulkanSwapchainFrameSlotCommand::CmdBeginRenderPass;
        if (render_pass_required && !mCmdEndRenderPass)
            return VulkanSwapchainFrameSlotCommand::CmdEndRenderPass;
        if (draw_required && !mCmdBindPipeline)
            return VulkanSwapchainFrameSlotCommand::CmdBindPipeline;
        if (draw_required && !mCmdBindVertexBuffers)
            return VulkanSwapchainFrameSlotCommand::CmdBindVertexBuffers;
        if (draw_required && !mCmdSetViewport)
            return VulkanSwapchainFrameSlotCommand::CmdSetViewport;
        if (draw_required && !mCmdSetScissor)
            return VulkanSwapchainFrameSlotCommand::CmdSetScissor;
        if (draw_required && !mCmdDraw)
            return VulkanSwapchainFrameSlotCommand::CmdDraw;
        if (readback_required && !mCmdCopyImageToBuffer)
            return VulkanSwapchainFrameSlotCommand::CmdCopyImageToBuffer;
        if (!mEndCommandBuffer)
            return VulkanSwapchainFrameSlotCommand::EndCommandBuffer;
        if (!mResetFences)
            return VulkanSwapchainFrameSlotCommand::ResetFences;
        if (!mQueueSubmit)
            return VulkanSwapchainFrameSlotCommand::QueueSubmit;
        if (!mQueuePresent)
            return VulkanSwapchainFrameSlotCommand::QueuePresent;
        return std::nullopt;
    };
    if (const auto command = missing_command())
    {
        clearActiveUploadDestination();
        clearActiveReadback();
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::MissingRequiredCommand, mDisposition, command);
    }

    const VkFence prior_fences[] = { mSubmissionFence, mPresentCompletionFence };
    VkResult result = mWaitForFences(mDevice, 2, prior_fences, VK_TRUE, std::numeric_limits<std::uint64_t>::max());
    if (result != VK_SUCCESS)
    {
        mDisposition = result == VK_ERROR_DEVICE_LOST ? VulkanSwapchainFrameSlotDisposition::DeviceLost
                                                      : VulkanSwapchainFrameSlotDisposition::Reusable;
        clearActiveUploadDestination();
        clearActiveReadback();
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::CommandFailure, mDisposition,
                                VulkanSwapchainFrameSlotCommand::WaitForFences, result);
    }
    mSubmissionFenceSignaled        = true;
    mPresentCompletionFenceSignaled = true;

    if (draw_required && !activeUploadDestinationMatchesSnapshot())
    {
        clearActiveUploadDestination();
        clearActiveReadback();
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::InvalidUploadDestinationGeneration, mDisposition);
    }

    if (readback_required &&
        (!activeReadbackMatchesSnapshot() ||
         !mActiveReadbackGeneration->createdFor(*mReadbackPhysicalDeviceGeneration, *mLogicalDeviceGeneration, *mConfigurationGeneration,
                                                *mSwapchainGeneration, *mImagesGeneration) ||
         !mActiveReadbackGeneration->poisonForPresentationObservation()))
    {
        clearActiveUploadDestination();
        clearActiveReadback();
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::InvalidSwapchainReadbackGeneration, mDisposition);
    }

    std::uint32_t image_index = 0;
    result = mAcquireNextImage(mDevice, mSwapchain, VULKAN_SWAPCHAIN_FRAME_ACQUIRE_TIMEOUT_NS, mImageAvailableSemaphore,
                               VK_NULL_HANDLE, &image_index);
    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        clearActiveUploadDestination();
        clearActiveReadback();
        return VulkanSwapchainFrameSlotPresentationSuccess{ VulkanSwapchainFrameSlotPresentationOutcome::SwapchainReplacementRequired,
                                                            std::nullopt };
    }
    if (result == VK_ERROR_SURFACE_LOST_KHR)
    {
        clearActiveUploadDestination();
        clearActiveReadback();
        return VulkanSwapchainFrameSlotPresentationSuccess{ VulkanSwapchainFrameSlotPresentationOutcome::SurfaceLost, std::nullopt };
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
        mDisposition = result == VK_ERROR_DEVICE_LOST ? VulkanSwapchainFrameSlotDisposition::DeviceLost
                                                      : VulkanSwapchainFrameSlotDisposition::Reusable;
        clearActiveUploadDestination();
        clearActiveReadback();
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::CommandFailure, mDisposition,
                                VulkanSwapchainFrameSlotCommand::AcquireNextImage, result);
    }

    mAcquiredImageIndex = image_index;
    mDisposition        = VulkanSwapchainFrameSlotDisposition::ImageAcquired;
    mPendingPresentationOutcome = result == VK_SUBOPTIMAL_KHR ? VulkanSwapchainFrameSlotPresentationOutcome::Suboptimal
                                                              : VulkanSwapchainFrameSlotPresentationOutcome::Presented;
    mPendingPresentResult = VK_SUCCESS;
    if (image_index >= mImageCount)
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::AcquiredImageIndexOutOfRange, mDisposition,
                                VulkanSwapchainFrameSlotCommand::AcquireNextImage, result, image_index);
    }
    if (readback_required && !activeReadbackMatchesSnapshot())
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::InvalidSwapchainReadbackGeneration, mDisposition, std::nullopt,
                                VK_SUCCESS, image_index);
    }
    if (draw_required && !activeUploadDestinationMatchesSnapshot())
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::InvalidUploadDestinationGeneration, mDisposition, std::nullopt,
                                VK_SUCCESS, image_index);
    }
    VkRenderPass  presentation_render_pass = VK_NULL_HANDLE;
    VkFramebuffer presentation_framebuffer = VK_NULL_HANDLE;
    VkExtent2D    presentation_extent{};
    if (render_pass_required)
    {
        if (mPresentationTargetGeneration != presentation_target_generation ||
            !presentation_target_generation->createdFor(*mLogicalDeviceGeneration, *mConfigurationGeneration, *mSwapchainGeneration,
                                                        *mImagesGeneration) ||
            presentation_target_generation->renderPass() == VK_NULL_HANDLE ||
            presentation_target_generation->framebuffer(image_index) == VK_NULL_HANDLE)
        {
            return operationFailure(VulkanSwapchainFrameSlotOperationCode::InvalidSwapchainPresentationTargetGeneration, mDisposition,
                                    std::nullopt, VK_SUCCESS, image_index);
        }
        presentation_render_pass = presentation_target_generation->renderPass();
        presentation_framebuffer = presentation_target_generation->framebuffer(image_index);
        presentation_extent      = presentation_target_generation->imageExtent();
    }
    if (readback_required &&
        (presentation_extent.width != mActiveReadbackImageExtent.width || presentation_extent.height != mActiveReadbackImageExtent.height))
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::InvalidSwapchainReadbackGeneration, mDisposition, std::nullopt,
                                VK_SUCCESS, image_index);
    }
    if (draw_required &&
        (mPresentationPipelineGeneration != presentation_pipeline_generation || mPresentationPipelineLayout == VK_NULL_HANDLE ||
         mPresentationPipeline == VK_NULL_HANDLE || presentation_pipeline_generation->pipelineLayout() != mPresentationPipelineLayout ||
         presentation_pipeline_generation->pipeline() != mPresentationPipeline ||
         !presentation_pipeline_generation->createdFor(*mLogicalDeviceGeneration, *mConfigurationGeneration, *mSwapchainGeneration,
                                                       *mImagesGeneration, *presentation_target_generation)))
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::InvalidSwapchainPresentationPipelineGeneration, mDisposition,
                                std::nullopt, VK_SUCCESS, image_index);
    }
    if (draw_required && !activeUploadDestinationMatchesSnapshot())
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::InvalidUploadDestinationGeneration, mDisposition, std::nullopt,
                                VK_SUCCESS, image_index);
    }

    result = mResetCommandBuffer(mCommandBuffer, 0);
    if (result != VK_SUCCESS)
    {
        if (result == VK_ERROR_DEVICE_LOST)
        {
            mDisposition = VulkanSwapchainFrameSlotDisposition::DeviceLost;
            clearActiveUploadDestination();
            clearActiveReadback();
        }
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::CommandFailure, mDisposition,
                                VulkanSwapchainFrameSlotCommand::ResetCommandBuffer, result, image_index);
    }

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    result           = mBeginCommandBuffer(mCommandBuffer, &begin_info);
    if (result != VK_SUCCESS)
    {
        if (result == VK_ERROR_DEVICE_LOST)
        {
            mDisposition = VulkanSwapchainFrameSlotDisposition::DeviceLost;
            clearActiveUploadDestination();
            clearActiveReadback();
        }
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::CommandFailure, mDisposition,
                                VulkanSwapchainFrameSlotCommand::BeginCommandBuffer, result, image_index);
    }

    VkImageMemoryBarrier barrier{};
    barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask                   = 0;
    barrier.dstAccessMask                   = recording_mode == RecordingMode::TransferClear ? VK_ACCESS_TRANSFER_WRITE_BIT
                                              : render_pass_required                         ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                                                                                             : 0;
    barrier.oldLayout                       = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout                       = recording_mode == RecordingMode::TransferClear ? VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
                                              : render_pass_required                         ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                                                                                             : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier.image                           = mImagesGeneration->image(image_index);
    barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel   = 0;
    barrier.subresourceRange.levelCount     = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount     = 1;
    const VkPipelineStageFlags first_stage  = recording_mode == RecordingMode::TransferClear ? VK_PIPELINE_STAGE_TRANSFER_BIT
                                              : render_pass_required                         ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                                                                                             : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    const VkPipelineStageFlags first_destination_stage = recording_mode == RecordingMode::TransferClear ? VK_PIPELINE_STAGE_TRANSFER_BIT
                                                         : render_pass_required ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                                                                                : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    mCmdPipelineBarrier(mCommandBuffer, first_stage, first_destination_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    if (recording_mode == RecordingMode::TransferClear)
    {
        VkClearColorValue clear_value{};
        std::copy(clear_color_snapshot.mRgba.begin(), clear_color_snapshot.mRgba.end(), clear_value.float32);
        mCmdClearColorImage(mCommandBuffer, barrier.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear_value, 1,
                            &barrier.subresourceRange);

        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = 0;
        barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout     = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        mCmdPipelineBarrier(mCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr,
                            0, nullptr, 1, &barrier);
    }
    else if (render_pass_required)
    {
        VkClearValue clear_value{};
        std::copy(clear_color_snapshot.mRgba.begin(), clear_color_snapshot.mRgba.end(), clear_value.color.float32);

        VkRenderPassBeginInfo render_pass_begin{};
        render_pass_begin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        render_pass_begin.renderPass        = presentation_render_pass;
        render_pass_begin.framebuffer       = presentation_framebuffer;
        render_pass_begin.renderArea.extent = presentation_extent;
        render_pass_begin.clearValueCount   = 1;
        render_pass_begin.pClearValues      = &clear_value;
        mCmdBeginRenderPass(mCommandBuffer, &render_pass_begin, VK_SUBPASS_CONTENTS_INLINE);
        if (draw_required)
        {
            mCmdBindPipeline(mCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mPresentationPipeline);

            const VkDeviceSize vertex_buffer_offset = 0;
            mCmdBindVertexBuffers(mCommandBuffer, 0, 1, &mActiveUploadDestinationBuffer, &vertex_buffer_offset);

            VkViewport viewport{};
            viewport.width    = static_cast<float>(presentation_extent.width);
            viewport.height   = static_cast<float>(presentation_extent.height);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            mCmdSetViewport(mCommandBuffer, 0, 1, &viewport);

            VkRect2D scissor{};
            scissor.extent = presentation_extent;
            mCmdSetScissor(mCommandBuffer, 0, 1, &scissor);
            mCmdDraw(mCommandBuffer, 3, 1, 0, 0);
        }
        mCmdEndRenderPass(mCommandBuffer);

        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = readback_required ? VK_ACCESS_TRANSFER_READ_BIT : 0;
        barrier.oldLayout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.newLayout     = readback_required ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        mCmdPipelineBarrier(mCommandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                            readback_required ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0,
                            nullptr, 1, &barrier);

        if (readback_required)
        {
            VkBufferMemoryBarrier buffer_barrier{};
            buffer_barrier.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            buffer_barrier.srcAccessMask       = VK_ACCESS_HOST_WRITE_BIT;
            buffer_barrier.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
            buffer_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            buffer_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            buffer_barrier.buffer              = mActiveReadbackBuffer;
            buffer_barrier.offset              = 0;
            buffer_barrier.size                = mActiveReadbackByteCount;
            mCmdPipelineBarrier(mCommandBuffer, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1,
                                &buffer_barrier, 0, nullptr);

            VkBufferImageCopy copy_region{};
            copy_region.bufferOffset                    = 0;
            copy_region.bufferRowLength                 = 0;
            copy_region.bufferImageHeight               = 0;
            copy_region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            copy_region.imageSubresource.mipLevel       = 0;
            copy_region.imageSubresource.baseArrayLayer = 0;
            copy_region.imageSubresource.layerCount     = 1;
            copy_region.imageOffset                     = { 0, 0, 0 };
            copy_region.imageExtent                     = { mActiveReadbackImageExtent.width, mActiveReadbackImageExtent.height, 1 };
            mCmdCopyImageToBuffer(mCommandBuffer, barrier.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, mActiveReadbackBuffer, 1,
                                  &copy_region);

            buffer_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            buffer_barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            mCmdPipelineBarrier(mCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1,
                                &buffer_barrier, 0, nullptr);

            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = 0;
            barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.newLayout     = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            mCmdPipelineBarrier(mCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0,
                                nullptr, 1, &barrier);
        }
    }

    result = mEndCommandBuffer(mCommandBuffer);
    if (result != VK_SUCCESS)
    {
        if (result == VK_ERROR_DEVICE_LOST)
        {
            mDisposition = VulkanSwapchainFrameSlotDisposition::DeviceLost;
            clearActiveUploadDestination();
            clearActiveReadback();
        }
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::CommandFailure, mDisposition,
                                VulkanSwapchainFrameSlotCommand::EndCommandBuffer, result, image_index);
    }

    const VkFence fences[] = { mSubmissionFence, mPresentCompletionFence };
    result                 = mResetFences(mDevice, 2, fences);
    if (result != VK_SUCCESS)
    {
        if (result == VK_ERROR_DEVICE_LOST)
        {
            mDisposition = VulkanSwapchainFrameSlotDisposition::DeviceLost;
            clearActiveUploadDestination();
            clearActiveReadback();
        }
        else if (result != VK_ERROR_OUT_OF_HOST_MEMORY && result != VK_ERROR_OUT_OF_DEVICE_MEMORY)
        {
            // An unexpected multi-fence reset failure does not say which
            // fences changed state. Recovery cannot safely choose a fence to
            // reuse, wait, or signal.
            mDisposition = VulkanSwapchainFrameSlotDisposition::FenceResetIndeterminate;
        }
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::CommandFailure, mDisposition,
                                VulkanSwapchainFrameSlotCommand::ResetFences, result, image_index);
    }
    mSubmissionFenceSignaled        = false;
    mPresentCompletionFenceSignaled = false;

    const VkPipelineStageFlags wait_stage = recording_mode == RecordingMode::TransferClear ? VK_PIPELINE_STAGE_TRANSFER_BIT
                                            : render_pass_required                         ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                                                                                           : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    VkSubmitInfo submit_info{};
    submit_info.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.waitSemaphoreCount   = 1;
    submit_info.pWaitSemaphores      = &mImageAvailableSemaphore;
    submit_info.pWaitDstStageMask    = &wait_stage;
    submit_info.commandBufferCount   = 1;
    submit_info.pCommandBuffers      = &mCommandBuffer;
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores    = &mPresentationReadySemaphore;

    result = mQueueSubmit(mQueue, 1, &submit_info, mSubmissionFence);
    if (result != VK_SUCCESS)
    {
        if (result == VK_ERROR_DEVICE_LOST)
        {
            mPendingSubmissionReportedDeviceLost = true;
            mDisposition                         = VulkanSwapchainFrameSlotDisposition::SubmissionPending;
        }
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::CommandFailure, mDisposition,
                                VulkanSwapchainFrameSlotCommand::QueueSubmit, result, image_index);
    }

    mPendingSubmissionReportedDeviceLost = false;
    mReadbackClassificationEligible      = mActiveReadbackGeneration != nullptr;
    mDisposition                         = VulkanSwapchainFrameSlotDisposition::PresentationReady;
    return retryPresentation();
}

VulkanSwapchainFrameSlotPresentationResult VulkanSwapchainFrameSlotGeneration::retryPresentation() noexcept
{
    if (mDisposition != VulkanSwapchainFrameSlotDisposition::PresentationReady || !mAcquiredImageIndex)
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::InvalidDisposition, mDisposition);
    }
    if (!mQueuePresent)
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::MissingRequiredCommand, mDisposition,
                                VulkanSwapchainFrameSlotCommand::QueuePresent, VK_SUCCESS, mAcquiredImageIndex);
    }
    if (mActiveUploadDestinationGeneration && !activeUploadDestinationMatchesSnapshot())
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::InvalidUploadDestinationGeneration, mDisposition, std::nullopt,
                                VK_SUCCESS, mAcquiredImageIndex);
    }

    VkSwapchainPresentFenceInfoKHR fence_info{};
    fence_info.sType          = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_KHR;
    fence_info.swapchainCount = 1;
    fence_info.pFences        = &mPresentCompletionFence;

    VkPresentInfoKHR present_info{};
    present_info.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.pNext              = &fence_info;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores    = &mPresentationReadySemaphore;
    present_info.swapchainCount     = 1;
    present_info.pSwapchains        = &mSwapchain;
    present_info.pImageIndices      = &*mAcquiredImageIndex;

    const VkResult result = mQueuePresent(mQueue, &present_info);
    if (result == VK_ERROR_OUT_OF_HOST_MEMORY || result == VK_ERROR_OUT_OF_DEVICE_MEMORY)
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::CommandFailure, mDisposition,
                                VulkanSwapchainFrameSlotCommand::QueuePresent, result, mAcquiredImageIndex);
    }
    const bool present_enqueued = result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR || result == VK_ERROR_DEVICE_LOST ||
                                  result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_ERROR_SURFACE_LOST_KHR ||
                                  result == VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT ||
                                  result == VK_ERROR_PRESENT_TIMING_QUEUE_FULL_EXT;
    if (!present_enqueued)
    {
        // Unexpected failures do not say whether the presentation semaphore or
        // fence was consumed. Neither retry, cancellation, nor teardown can
        // safely choose one interpretation, so only device retirement may
        // recover this slot.
        mDisposition = VulkanSwapchainFrameSlotDisposition::PresentationIndeterminate;
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::CommandFailure, mDisposition,
                                VulkanSwapchainFrameSlotCommand::QueuePresent, result, mAcquiredImageIndex);
    }

    mPendingPresentResult = result;
    if (result == VK_SUBOPTIMAL_KHR)
    {
        mPendingPresentationOutcome = VulkanSwapchainFrameSlotPresentationOutcome::Suboptimal;
    }
    else if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        mPendingPresentationOutcome = VulkanSwapchainFrameSlotPresentationOutcome::SwapchainReplacementRequired;
    }
    else if (result == VK_ERROR_SURFACE_LOST_KHR)
    {
        mPendingPresentationOutcome = VulkanSwapchainFrameSlotPresentationOutcome::SurfaceLost;
    }
    mDisposition = VulkanSwapchainFrameSlotDisposition::PresentPending;
    return retryPresentationCompletion();
}

VulkanSwapchainFrameSlotPresentationResult VulkanSwapchainFrameSlotGeneration::retryPresentationCompletion() noexcept
{
    if (mDisposition == VulkanSwapchainFrameSlotDisposition::SubmissionPending)
    {
        if (!mWaitForFences)
        {
            return operationFailure(VulkanSwapchainFrameSlotOperationCode::MissingRequiredCommand, mDisposition,
                                    VulkanSwapchainFrameSlotCommand::WaitForFences, VK_SUCCESS, mAcquiredImageIndex);
        }
        const VkResult result =
            mWaitForFences(mDevice, 1, &mSubmissionFence, VK_TRUE, std::numeric_limits<std::uint64_t>::max());
        if (result == VK_SUCCESS)
        {
            mSubmissionFenceSignaled = true;
            if (mPendingSubmissionReportedDeviceLost)
            {
                mPendingSubmissionReportedDeviceLost = false;
                mDisposition                         = VulkanSwapchainFrameSlotDisposition::DeviceLost;
                clearActiveUploadDestination();
                clearActiveReadback();
                return operationFailure(VulkanSwapchainFrameSlotOperationCode::CommandFailure, mDisposition,
                                        VulkanSwapchainFrameSlotCommand::QueueSubmit, VK_ERROR_DEVICE_LOST, mAcquiredImageIndex);
            }
            mDisposition = VulkanSwapchainFrameSlotDisposition::PresentationReady;
            return operationFailure(VulkanSwapchainFrameSlotOperationCode::CommandFailure, mDisposition,
                                    VulkanSwapchainFrameSlotCommand::QueueSubmit, VK_ERROR_DEVICE_LOST, mAcquiredImageIndex);
        }
        if (result == VK_ERROR_DEVICE_LOST)
        {
            mPendingSubmissionReportedDeviceLost = false;
            mDisposition                         = VulkanSwapchainFrameSlotDisposition::DeviceLost;
            clearActiveUploadDestination();
            clearActiveReadback();
        }
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::CommandFailure, mDisposition,
                                VulkanSwapchainFrameSlotCommand::WaitForFences, result, mAcquiredImageIndex);
    }

    if (mDisposition != VulkanSwapchainFrameSlotDisposition::PresentPending || !mAcquiredImageIndex)
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::InvalidDisposition, mDisposition);
    }
    if (!mWaitForFences)
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::MissingRequiredCommand, mDisposition,
                                VulkanSwapchainFrameSlotCommand::WaitForFences, VK_SUCCESS, mAcquiredImageIndex);
    }

    const VkFence fences[] = { mSubmissionFence, mPresentCompletionFence };
    const VkResult result = mWaitForFences(mDevice, 2, fences, VK_TRUE, std::numeric_limits<std::uint64_t>::max());
    if (result != VK_SUCCESS)
    {
        if (result == VK_ERROR_DEVICE_LOST)
        {
            mDisposition = VulkanSwapchainFrameSlotDisposition::DeviceLost;
            clearActiveUploadDestination();
            clearActiveReadback();
        }
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::CommandFailure, mDisposition,
                                VulkanSwapchainFrameSlotCommand::WaitForFences, result, mAcquiredImageIndex);
    }

    mSubmissionFenceSignaled        = true;
    mPresentCompletionFenceSignaled = true;
    const auto image_index          = mAcquiredImageIndex;
    const auto outcome              = mPendingPresentationOutcome;
    const auto present_result       = mPendingPresentResult;
    mAcquiredImageIndex.reset();
    mPendingPresentationOutcome = VulkanSwapchainFrameSlotPresentationOutcome::Presented;
    mPendingPresentResult        = VK_SUCCESS;

    if (present_result == VK_ERROR_DEVICE_LOST)
    {
        mDisposition = VulkanSwapchainFrameSlotDisposition::DeviceLost;
        clearActiveUploadDestination();
        clearActiveReadback();
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::CommandFailure, mDisposition,
                                VulkanSwapchainFrameSlotCommand::QueuePresent, present_result, image_index);
    }

    const bool invalid_upload_destination = mActiveUploadDestinationGeneration && !activeUploadDestinationMatchesSnapshot();
    clearActiveUploadDestination();

    std::optional<VulkanSwapchainReadbackObservation> observation;
    bool                                              invalid_readback = false;
    if (mActiveReadbackGeneration)
    {
        if (mReadbackClassificationEligible && activeReadbackMatchesSnapshot())
        {
            observation = mActiveReadbackGeneration->classifyPresentationObservation();
        }
        invalid_readback = !observation;
        clearActiveReadback();
    }

    mDisposition = VulkanSwapchainFrameSlotDisposition::Reusable;
    if (invalid_upload_destination)
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::InvalidUploadDestinationGeneration, mDisposition, std::nullopt,
                                VK_SUCCESS, image_index);
    }
    if (invalid_readback)
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::InvalidSwapchainReadbackGeneration, mDisposition, std::nullopt,
                                VK_SUCCESS, image_index);
    }
    if (present_result == VK_SUCCESS || present_result == VK_SUBOPTIMAL_KHR || present_result == VK_ERROR_OUT_OF_DATE_KHR ||
        present_result == VK_ERROR_SURFACE_LOST_KHR)
    {
        return VulkanSwapchainFrameSlotPresentationSuccess{ outcome, image_index, observation };
    }
    return operationFailure(VulkanSwapchainFrameSlotOperationCode::CommandFailure, mDisposition,
                            VulkanSwapchainFrameSlotCommand::QueuePresent, present_result, image_index);
}

VulkanSwapchainFrameSlotOperationResult VulkanSwapchainFrameSlotGeneration::cancelAcquireToPresent() noexcept
{
    if (!mAcquiredImageIndex)
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::InvalidDisposition, mDisposition);
    }
    if (*mAcquiredImageIndex >= mImageCount)
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::AcquiredImageIndexOutOfRange, mDisposition,
                                VulkanSwapchainFrameSlotCommand::AcquireNextImage, VK_SUCCESS, mAcquiredImageIndex);
    }
    if (!mReleaseSwapchainImages)
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::MissingRequiredCommand, mDisposition,
                                VulkanSwapchainFrameSlotCommand::ReleaseSwapchainImages, VK_SUCCESS, mAcquiredImageIndex);
    }

    if (mDisposition == VulkanSwapchainFrameSlotDisposition::ReleaseRequired)
    {
        VkReleaseSwapchainImagesInfoKHR release_info{};
        release_info.sType           = VK_STRUCTURE_TYPE_RELEASE_SWAPCHAIN_IMAGES_INFO_KHR;
        release_info.swapchain       = mSwapchain;
        release_info.imageIndexCount = 1;
        release_info.pImageIndices   = &*mAcquiredImageIndex;
        const VkResult result        = mReleaseSwapchainImages(mDevice, &release_info);
        if (result == VK_SUCCESS)
        {
            mAcquiredImageIndex.reset();
            mCancellationPhase = CancellationPhase::Idle;
            mDisposition       = VulkanSwapchainFrameSlotDisposition::Reusable;
            clearActiveUploadDestination();
            clearActiveReadback();
            return mDisposition;
        }
        // A non-success result does not say whether release took effect.
        // Retrying could release an image that is no longer acquired.
        mDisposition = result == VK_ERROR_DEVICE_LOST ? VulkanSwapchainFrameSlotDisposition::DeviceLost
                                                      : VulkanSwapchainFrameSlotDisposition::ReleaseIndeterminate;
        if (mDisposition == VulkanSwapchainFrameSlotDisposition::DeviceLost)
        {
            clearActiveUploadDestination();
            clearActiveReadback();
        }
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::CommandFailure, mDisposition,
                                VulkanSwapchainFrameSlotCommand::ReleaseSwapchainImages, result, mAcquiredImageIndex);
    }

    if (mDisposition != VulkanSwapchainFrameSlotDisposition::ImageAcquired &&
        mDisposition != VulkanSwapchainFrameSlotDisposition::PresentationReady)
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::InvalidDisposition, mDisposition, std::nullopt, VK_SUCCESS,
                                mAcquiredImageIndex);
    }
    if (!mResetFences)
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::MissingRequiredCommand, mDisposition,
                                VulkanSwapchainFrameSlotCommand::ResetFences, VK_SUCCESS, mAcquiredImageIndex);
    }
    if (!mQueueSubmit)
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::MissingRequiredCommand, mDisposition,
                                VulkanSwapchainFrameSlotCommand::QueueSubmit, VK_SUCCESS, mAcquiredImageIndex);
    }

    const VulkanSwapchainFrameSlotDisposition original_disposition = mDisposition;
    VkFence* fence = original_disposition == VulkanSwapchainFrameSlotDisposition::ImageAcquired ? &mSubmissionFence
                                                                                                : &mPresentCompletionFence;
    const VkResult reset_result = mResetFences(mDevice, 1, fence);
    if (reset_result != VK_SUCCESS)
    {
        if (reset_result == VK_ERROR_DEVICE_LOST)
        {
            mDisposition = VulkanSwapchainFrameSlotDisposition::DeviceLost;
            clearActiveUploadDestination();
            clearActiveReadback();
        }
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::CommandFailure, mDisposition,
                                VulkanSwapchainFrameSlotCommand::ResetFences, reset_result, mAcquiredImageIndex);
    }

    VkSemaphore* semaphore;
    if (original_disposition == VulkanSwapchainFrameSlotDisposition::ImageAcquired)
    {
        mSubmissionFenceSignaled = false;
        mCancellationPhase       = CancellationPhase::DrainImageAvailable;
        semaphore                = &mImageAvailableSemaphore;
    }
    else
    {
        mPresentCompletionFenceSignaled = false;
        mCancellationPhase              = CancellationPhase::DrainPresentationReady;
        semaphore                       = &mPresentationReadySemaphore;
    }

    const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    VkSubmitInfo submit_info{};
    submit_info.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores    = semaphore;
    submit_info.pWaitDstStageMask  = &wait_stage;

    const VkResult submit_result = mQueueSubmit(mQueue, 1, &submit_info, *fence);
    if (submit_result != VK_SUCCESS && submit_result != VK_ERROR_DEVICE_LOST)
    {
        mCancellationPhase = CancellationPhase::Idle;
        mDisposition       = original_disposition;
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::CommandFailure, mDisposition,
                                VulkanSwapchainFrameSlotCommand::QueueSubmit, submit_result, mAcquiredImageIndex);
    }

    mCancellationSubmissionPending        = true;
    mCancellationSubmitReportedDeviceLost = submit_result == VK_ERROR_DEVICE_LOST;
    mDisposition                          = VulkanSwapchainFrameSlotDisposition::CancellationPending;
    if (submit_result == VK_ERROR_DEVICE_LOST)
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::CommandFailure, mDisposition,
                                VulkanSwapchainFrameSlotCommand::QueueSubmit, submit_result, mAcquiredImageIndex);
    }
    return retryCancellationCompletion();
}

VulkanSwapchainFrameSlotOperationResult VulkanSwapchainFrameSlotGeneration::retryCancellationCompletion() noexcept
{
    if (mDisposition != VulkanSwapchainFrameSlotDisposition::CancellationPending ||
        mCancellationPhase == CancellationPhase::Idle || !mAcquiredImageIndex)
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::InvalidDisposition, mDisposition);
    }
    if (!mWaitForFences)
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::MissingRequiredCommand, mDisposition,
                                VulkanSwapchainFrameSlotCommand::WaitForFences, VK_SUCCESS, mAcquiredImageIndex);
    }

    if (!mCancellationSubmissionPending)
    {
        if (mCancellationPhase != CancellationPhase::SignalPresentFence || !mQueueSubmit)
        {
            return operationFailure(VulkanSwapchainFrameSlotOperationCode::MissingRequiredCommand, mDisposition,
                                    VulkanSwapchainFrameSlotCommand::QueueSubmit, VK_SUCCESS, mAcquiredImageIndex);
        }
        VkSubmitInfo submit_info{};
        submit_info.sType       = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        const VkResult result   = mQueueSubmit(mQueue, 1, &submit_info, mPresentCompletionFence);
        if (result != VK_SUCCESS && result != VK_ERROR_DEVICE_LOST)
        {
            return operationFailure(VulkanSwapchainFrameSlotOperationCode::CommandFailure, mDisposition,
                                    VulkanSwapchainFrameSlotCommand::QueueSubmit, result, mAcquiredImageIndex);
        }
        mCancellationSubmissionPending        = true;
        mCancellationSubmitReportedDeviceLost = result == VK_ERROR_DEVICE_LOST;
        if (result == VK_ERROR_DEVICE_LOST)
        {
            return operationFailure(VulkanSwapchainFrameSlotOperationCode::CommandFailure, mDisposition,
                                    VulkanSwapchainFrameSlotCommand::QueueSubmit, result, mAcquiredImageIndex);
        }
    }

    VkFence wait_fences[] = { mSubmissionFence, mPresentCompletionFence };
    std::uint32_t wait_count = 1;
    const VkFence* fences = mCancellationPhase == CancellationPhase::SignalPresentFence ? &wait_fences[1] : &wait_fences[0];
    if (mCancellationPhase == CancellationPhase::DrainPresentationReady)
    {
        wait_count = 2;
        fences     = wait_fences;
    }
    const VkResult wait_result =
        mWaitForFences(mDevice, wait_count, fences, VK_TRUE, std::numeric_limits<std::uint64_t>::max());
    if (wait_result != VK_SUCCESS)
    {
        if (wait_result == VK_ERROR_DEVICE_LOST)
        {
            mCancellationSubmissionPending        = false;
            mCancellationSubmitReportedDeviceLost = false;
            mDisposition                          = VulkanSwapchainFrameSlotDisposition::DeviceLost;
            clearActiveUploadDestination();
            clearActiveReadback();
        }
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::CommandFailure, mDisposition,
                                VulkanSwapchainFrameSlotCommand::WaitForFences, wait_result, mAcquiredImageIndex);
    }

    mCancellationSubmissionPending = false;
    if (mCancellationSubmitReportedDeviceLost)
    {
        mCancellationSubmitReportedDeviceLost = false;
        mDisposition                          = VulkanSwapchainFrameSlotDisposition::DeviceLost;
        clearActiveUploadDestination();
        clearActiveReadback();
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::CommandFailure, mDisposition,
                                VulkanSwapchainFrameSlotCommand::QueueSubmit, VK_ERROR_DEVICE_LOST, mAcquiredImageIndex);
    }

    if (mCancellationPhase == CancellationPhase::DrainImageAvailable)
    {
        mSubmissionFenceSignaled = true;
        if (!mPresentCompletionFenceSignaled)
        {
            mCancellationPhase             = CancellationPhase::SignalPresentFence;
            mCancellationSubmissionPending = false;
            return retryCancellationCompletion();
        }
    }
    else if (mCancellationPhase == CancellationPhase::SignalPresentFence)
    {
        mPresentCompletionFenceSignaled = true;
    }
    else
    {
        mSubmissionFenceSignaled        = true;
        mPresentCompletionFenceSignaled = true;
    }

    mCancellationPhase = CancellationPhase::Idle;
    mDisposition       = VulkanSwapchainFrameSlotDisposition::ReleaseRequired;
    return cancelAcquireToPresent();
}

void VulkanSwapchainFrameSlotGeneration::reset() noexcept
{
    if (mActiveUploadDestinationGeneration || mActiveReadbackGeneration || mDisposition == VulkanSwapchainFrameSlotDisposition::Pending ||
        mDisposition == VulkanSwapchainFrameSlotDisposition::ImageAcquired ||
        mDisposition == VulkanSwapchainFrameSlotDisposition::SubmissionPending ||
        mDisposition == VulkanSwapchainFrameSlotDisposition::PresentationReady ||
        mDisposition == VulkanSwapchainFrameSlotDisposition::PresentPending ||
        mDisposition == VulkanSwapchainFrameSlotDisposition::FenceResetIndeterminate ||
        mDisposition == VulkanSwapchainFrameSlotDisposition::PresentationIndeterminate ||
        mDisposition == VulkanSwapchainFrameSlotDisposition::CancellationPending ||
        mDisposition == VulkanSwapchainFrameSlotDisposition::ReleaseRequired ||
        mDisposition == VulkanSwapchainFrameSlotDisposition::ReleaseIndeterminate)
    {
        return;
    }

    if (mPresentCompletionFence != VK_NULL_HANDLE && mDestroyFence)
    {
        mDestroyFence(mDevice, mPresentCompletionFence, nullptr);
    }
    mPresentCompletionFence = VK_NULL_HANDLE;
    if (mSubmissionFence != VK_NULL_HANDLE && mDestroyFence)
    {
        mDestroyFence(mDevice, mSubmissionFence, nullptr);
    }
    mSubmissionFence = VK_NULL_HANDLE;
    if (mPresentationReadySemaphore != VK_NULL_HANDLE && mDestroySemaphore)
    {
        mDestroySemaphore(mDevice, mPresentationReadySemaphore, nullptr);
    }
    mPresentationReadySemaphore = VK_NULL_HANDLE;
    if (mImageAvailableSemaphore != VK_NULL_HANDLE && mDestroySemaphore)
    {
        mDestroySemaphore(mDevice, mImageAvailableSemaphore, nullptr);
    }
    mImageAvailableSemaphore = VK_NULL_HANDLE;
    if (mCommandPool != VK_NULL_HANDLE && mDestroyCommandPool)
    {
        mDestroyCommandPool(mDevice, mCommandPool, nullptr);
    }
    mCommandPool   = VK_NULL_HANDLE;
    mCommandBuffer = VK_NULL_HANDLE;

    mLogicalDeviceGeneration          = nullptr;
    mConfigurationGeneration          = nullptr;
    mSwapchainGeneration              = nullptr;
    mGetInstanceProcAddr              = nullptr;
    mInstance                         = VK_NULL_HANDLE;
    mSurface                          = VK_NULL_HANDLE;
    mPhysicalDevice                   = VK_NULL_HANDLE;
    mPhysicalDeviceIndex              = 0;
    mDevice                           = VK_NULL_HANDLE;
    mQueue                            = VK_NULL_HANDLE;
    mQueueFamilyIndex                 = VK_QUEUE_FAMILY_IGNORED;
    mQueueIndex                       = 0;
    mDrawableExtent                   = {};
    mImageExtent                      = {};
    mSwapchain                        = VK_NULL_HANDLE;
    mImageFormat                      = VK_FORMAT_UNDEFINED;
    mImageCount                       = 0;
    mImagesGeneration                 = nullptr;
    mPresentationTargetGeneration     = nullptr;
    mPresentationPipelineGeneration   = nullptr;
    mPresentationPipelineLayout       = VK_NULL_HANDLE;
    mPresentationPipeline             = VK_NULL_HANDLE;
    mDestroyCommandPool               = nullptr;
    mDestroySemaphore                 = nullptr;
    mDestroyFence                     = nullptr;
    mWaitForFences                    = nullptr;
    mResetCommandBuffer               = nullptr;
    mBeginCommandBuffer               = nullptr;
    mEndCommandBuffer                 = nullptr;
    mResetFences                      = nullptr;
    mQueueSubmit                      = nullptr;
    mAcquireNextImage                 = nullptr;
    mCmdPipelineBarrier               = nullptr;
    mCmdClearColorImage               = nullptr;
    mCmdBeginRenderPass               = nullptr;
    mCmdEndRenderPass                 = nullptr;
    mCmdBindPipeline                  = nullptr;
    mCmdBindVertexBuffers                      = nullptr;
    mCmdSetViewport                   = nullptr;
    mCmdSetScissor                    = nullptr;
    mCmdDraw                          = nullptr;
    mCmdCopyImageToBuffer             = nullptr;
    mQueuePresent                     = nullptr;
    mReleaseSwapchainImages           = nullptr;
    mUploadDestinationPhysicalDeviceGeneration = nullptr;
    clearActiveUploadDestination();
    mReadbackPhysicalDeviceGeneration = nullptr;
    mResolvedReadbackGeneration       = nullptr;
    clearActiveReadback();
    mDisposition                         = VulkanSwapchainFrameSlotDisposition::Reusable;
    mPendingSubmissionReportedDeviceLost = false;
    mSubmissionFenceSignaled              = true;
    mPresentCompletionFenceSignaled       = true;
    mAcquiredImageIndex.reset();
    mPendingPresentationOutcome = VulkanSwapchainFrameSlotPresentationOutcome::Presented;
    mPendingPresentResult        = VK_SUCCESS;
    mCancellationPhase                    = CancellationPhase::Idle;
    mCancellationSubmissionPending        = false;
    mCancellationSubmitReportedDeviceLost = false;
}

VulkanSwapchainFrameSlotResolutionResult resolveVulkanSwapchainFrameSlotGeneration(
    const VulkanLogicalDeviceGeneration&          logical_device_generation,
    const VulkanSwapchainConfigurationGeneration& configuration_generation,
    const VulkanSwapchainGeneration&              swapchain_generation,
    const VulkanSwapchainImagesGeneration&        images_generation) noexcept
{
    if (!valid(logical_device_generation))
    {
        return failure(VulkanSwapchainFrameSlotResolutionCode::InvalidLogicalDeviceGeneration);
    }
    if (!logical_device_generation.swapchainMaintenance1Enabled())
    {
        return failure(VulkanSwapchainFrameSlotResolutionCode::SwapchainMaintenance1NotEnabled);
    }
    if (!belongsTo(configuration_generation, logical_device_generation))
    {
        return failure(VulkanSwapchainFrameSlotResolutionCode::InvalidSwapchainConfigurationGeneration);
    }
    if (!swapchain_generation.createdFor(logical_device_generation, configuration_generation))
    {
        return failure(VulkanSwapchainFrameSlotResolutionCode::InvalidSwapchainGeneration);
    }
    if (!images_generation.createdFor(logical_device_generation, configuration_generation, swapchain_generation) ||
        images_generation.imageCount() == 0 || images_generation.imageFormat() != configuration_generation.surfaceFormat().format)
    {
        return failure(VulkanSwapchainFrameSlotResolutionCode::InvalidSwapchainImagesGeneration);
    }

    SwapchainFrameSlotDispatch dispatch;
    if (auto error = resolveDispatch(logical_device_generation, dispatch))
    {
        return *error;
    }

    VkCommandPoolCreateInfo command_pool_info{};
    command_pool_info.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    command_pool_info.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    command_pool_info.queueFamilyIndex = logical_device_generation.queueFamilyIndex();

    VkCommandPool command_pool;
    VkResult      result = dispatch.mCreateCommandPool(logical_device_generation.device(), &command_pool_info, nullptr, &command_pool);
    if (result != VK_SUCCESS)
    {
        return failure(VulkanSwapchainFrameSlotResolutionCode::CommandPoolCreationFailure,
                       VulkanSwapchainFrameSlotCommand::CreateCommandPool, result);
    }
    if (command_pool == VK_NULL_HANDLE)
    {
        return failure(VulkanSwapchainFrameSlotResolutionCode::NullCommandPoolOnSuccess,
                       VulkanSwapchainFrameSlotCommand::CreateCommandPool);
    }

    VkCommandBufferAllocateInfo command_buffer_info{};
    command_buffer_info.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command_buffer_info.commandPool        = command_pool;
    command_buffer_info.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_buffer_info.commandBufferCount = 1;

    VkCommandBuffer command_buffer;
    result = dispatch.mAllocateCommandBuffers(logical_device_generation.device(), &command_buffer_info, &command_buffer);
    if (result != VK_SUCCESS)
    {
        rollback(logical_device_generation.device(), dispatch, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE,
                 command_pool);
        return failure(VulkanSwapchainFrameSlotResolutionCode::CommandBufferAllocationFailure,
                       VulkanSwapchainFrameSlotCommand::AllocateCommandBuffers, result);
    }
    if (command_buffer == VK_NULL_HANDLE)
    {
        rollback(logical_device_generation.device(), dispatch, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE,
                 command_pool);
        return failure(VulkanSwapchainFrameSlotResolutionCode::NullCommandBufferOnSuccess,
                       VulkanSwapchainFrameSlotCommand::AllocateCommandBuffers);
    }

    VkSemaphoreCreateInfo semaphore_info{};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkSemaphore image_available_semaphore;
    result = dispatch.mCreateSemaphore(logical_device_generation.device(), &semaphore_info, nullptr, &image_available_semaphore);
    if (result != VK_SUCCESS)
    {
        rollback(logical_device_generation.device(), dispatch, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE,
                 command_pool);
        return failure(VulkanSwapchainFrameSlotResolutionCode::ImageAvailableSemaphoreCreationFailure,
                       VulkanSwapchainFrameSlotCommand::CreateSemaphore, result);
    }
    if (image_available_semaphore == VK_NULL_HANDLE)
    {
        rollback(logical_device_generation.device(), dispatch, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE,
                 command_pool);
        return failure(VulkanSwapchainFrameSlotResolutionCode::NullImageAvailableSemaphoreOnSuccess,
                       VulkanSwapchainFrameSlotCommand::CreateSemaphore);
    }

    VkSemaphore presentation_ready_semaphore;
    result = dispatch.mCreateSemaphore(logical_device_generation.device(), &semaphore_info, nullptr, &presentation_ready_semaphore);
    if (result != VK_SUCCESS)
    {
        rollback(logical_device_generation.device(), dispatch, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE,
                 image_available_semaphore, command_pool);
        return failure(VulkanSwapchainFrameSlotResolutionCode::PresentationReadySemaphoreCreationFailure,
                       VulkanSwapchainFrameSlotCommand::CreateSemaphore, result);
    }
    if (presentation_ready_semaphore == VK_NULL_HANDLE)
    {
        rollback(logical_device_generation.device(), dispatch, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE,
                 image_available_semaphore, command_pool);
        return failure(VulkanSwapchainFrameSlotResolutionCode::NullPresentationReadySemaphoreOnSuccess,
                       VulkanSwapchainFrameSlotCommand::CreateSemaphore);
    }

    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    VkFence submission_fence;
    result = dispatch.mCreateFence(logical_device_generation.device(), &fence_info, nullptr, &submission_fence);
    if (result != VK_SUCCESS)
    {
        rollback(logical_device_generation.device(), dispatch, VK_NULL_HANDLE, VK_NULL_HANDLE, presentation_ready_semaphore,
                 image_available_semaphore, command_pool);
        return failure(VulkanSwapchainFrameSlotResolutionCode::SubmissionFenceCreationFailure, VulkanSwapchainFrameSlotCommand::CreateFence,
                       result);
    }
    if (submission_fence == VK_NULL_HANDLE)
    {
        rollback(logical_device_generation.device(), dispatch, VK_NULL_HANDLE, VK_NULL_HANDLE, presentation_ready_semaphore,
                 image_available_semaphore, command_pool);
        return failure(VulkanSwapchainFrameSlotResolutionCode::NullSubmissionFenceOnSuccess, VulkanSwapchainFrameSlotCommand::CreateFence);
    }

    VkFence present_completion_fence;
    result = dispatch.mCreateFence(logical_device_generation.device(), &fence_info, nullptr, &present_completion_fence);
    if (result != VK_SUCCESS)
    {
        rollback(logical_device_generation.device(), dispatch, VK_NULL_HANDLE, submission_fence, presentation_ready_semaphore,
                 image_available_semaphore, command_pool);
        return failure(VulkanSwapchainFrameSlotResolutionCode::PresentCompletionFenceCreationFailure,
                       VulkanSwapchainFrameSlotCommand::CreateFence, result);
    }
    if (present_completion_fence == VK_NULL_HANDLE)
    {
        rollback(logical_device_generation.device(), dispatch, VK_NULL_HANDLE, submission_fence, presentation_ready_semaphore,
                 image_available_semaphore, command_pool);
        return failure(VulkanSwapchainFrameSlotResolutionCode::NullPresentCompletionFenceOnSuccess,
                       VulkanSwapchainFrameSlotCommand::CreateFence);
    }

    return VulkanSwapchainFrameSlotGenerationFactory::create(
        logical_device_generation, configuration_generation, swapchain_generation, images_generation, command_pool, command_buffer,
        image_available_semaphore, presentation_ready_semaphore, submission_fence, present_completion_fence,
        dispatch.mDestroyCommandPool, dispatch.mDestroySemaphore, dispatch.mDestroyFence);
}

} // namespace LLRenderVulkan
