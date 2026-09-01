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
        PFN_vkAcquireNextImageKHR      mAcquireNextImage       = nullptr;
        PFN_vkCmdPipelineBarrier       mCmdPipelineBarrier     = nullptr;
        PFN_vkQueuePresentKHR          mQueuePresent           = nullptr;
        PFN_vkReleaseSwapchainImagesKHR mReleaseSwapchainImages = nullptr;
    };

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
    mImageCount(std::exchange(other.mImageCount, 0)),
    mImagesGeneration(std::exchange(other.mImagesGeneration, nullptr)),
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
    mQueuePresent(std::exchange(other.mQueuePresent, nullptr)),
    mReleaseSwapchainImages(std::exchange(other.mReleaseSwapchainImages, nullptr)),
    mDisposition(std::exchange(other.mDisposition, VulkanSwapchainFrameSlotDisposition::Reusable)),
    mPendingSubmissionReportedDeviceLost(std::exchange(other.mPendingSubmissionReportedDeviceLost, false)),
    mSubmissionFenceSignaled(std::exchange(other.mSubmissionFenceSignaled, true)),
    mPresentCompletionFenceSignaled(std::exchange(other.mPresentCompletionFenceSignaled, true)),
    mAcquiredImageIndex(std::exchange(other.mAcquiredImageIndex, std::nullopt)),
    mPendingPresentationOutcome(std::exchange(other.mPendingPresentationOutcome,
                                              VulkanSwapchainFrameSlotPresentationOutcome::Presented)),
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
    return mCommandPool != VK_NULL_HANDLE && mCommandBuffer != VK_NULL_HANDLE && mImageAvailableSemaphore != VK_NULL_HANDLE &&
           mPresentationReadySemaphore != VK_NULL_HANDLE && mSubmissionFence != VK_NULL_HANDLE &&
           mPresentCompletionFence != VK_NULL_HANDLE && mImagesGeneration == &images_generation &&
           images_generation.createdFor(logical_device_generation, configuration_generation, swapchain_generation) &&
           mGetInstanceProcAddr == logical_device_generation.getInstanceProcAddr() && mInstance == logical_device_generation.instance() &&
           mSurface == logical_device_generation.surface() && mPhysicalDevice == logical_device_generation.physicalDevice() &&
           mPhysicalDeviceIndex == logical_device_generation.physicalDeviceIndex() && mDevice == logical_device_generation.device() &&
           mQueue == logical_device_generation.queue() && mQueueFamilyIndex == logical_device_generation.queueFamilyIndex() &&
           mQueueIndex == logical_device_generation.queueIndex() && mDrawableExtent.width == drawable_extent.width &&
           mDrawableExtent.height == drawable_extent.height && mSwapchain == swapchain_generation.swapchain() &&
           mImageFormat == images_generation.imageFormat() && mImageCount == images_generation.imageCount();
}

VulkanSwapchainFrameSlotOperationResult VulkanSwapchainFrameSlotGeneration::resolveEmptySubmissionDispatch(
    const VulkanLogicalDeviceGeneration&          logical_device_generation,
    const VulkanSwapchainConfigurationGeneration& configuration_generation,
    const VulkanSwapchainGeneration&              swapchain_generation,
    const VulkanSwapchainImagesGeneration&        images_generation) noexcept
{
    if (!valid(logical_device_generation) || mGetInstanceProcAddr != logical_device_generation.getInstanceProcAddr() ||
        mInstance != logical_device_generation.instance() || mSurface != logical_device_generation.surface() ||
        mPhysicalDevice != logical_device_generation.physicalDevice() ||
        mPhysicalDeviceIndex != logical_device_generation.physicalDeviceIndex() || mDevice != logical_device_generation.device() ||
        mQueue != logical_device_generation.queue() || mQueueFamilyIndex != logical_device_generation.queueFamilyIndex() ||
        mQueueIndex != logical_device_generation.queueIndex())
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::InvalidLogicalDeviceGeneration, mDisposition);
    }

    const VkExtent2D drawable_extent = configuration_generation.drawableExtent();
    if (!belongsTo(configuration_generation, logical_device_generation) || mDrawableExtent.width != drawable_extent.width ||
        mDrawableExtent.height != drawable_extent.height)
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::InvalidSwapchainConfigurationGeneration, mDisposition);
    }
    if (!swapchain_generation.createdFor(logical_device_generation, configuration_generation) ||
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
    if (!valid(logical_device_generation) || mGetInstanceProcAddr != logical_device_generation.getInstanceProcAddr() ||
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
    if (!belongsTo(configuration_generation, logical_device_generation) || mDrawableExtent.width != drawable_extent.width ||
        mDrawableExtent.height != drawable_extent.height)
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::InvalidSwapchainConfigurationGeneration, mDisposition);
    }
    if (!swapchain_generation.createdFor(logical_device_generation, configuration_generation) ||
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
        mWaitForFences && mResetCommandBuffer && mBeginCommandBuffer && mEndCommandBuffer && mResetFences && mQueueSubmit &&
        mAcquireNextImage && mCmdPipelineBarrier && mQueuePresent && mReleaseSwapchainImages;
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

    mWaitForFences          = dispatch.mSubmission.mWaitForFences;
    mResetCommandBuffer     = dispatch.mSubmission.mResetCommandBuffer;
    mBeginCommandBuffer     = dispatch.mSubmission.mBeginCommandBuffer;
    mEndCommandBuffer       = dispatch.mSubmission.mEndCommandBuffer;
    mResetFences            = dispatch.mSubmission.mResetFences;
    mQueueSubmit            = dispatch.mSubmission.mQueueSubmit;
    mAcquireNextImage       = dispatch.mAcquireNextImage;
    mCmdPipelineBarrier     = dispatch.mCmdPipelineBarrier;
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
    if (mDisposition != VulkanSwapchainFrameSlotDisposition::Reusable)
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::InvalidDisposition, mDisposition);
    }

    const auto missing_command = [this]() -> std::optional<VulkanSwapchainFrameSlotCommand>
    {
        if (!mWaitForFences) return VulkanSwapchainFrameSlotCommand::WaitForFences;
        if (!mAcquireNextImage) return VulkanSwapchainFrameSlotCommand::AcquireNextImage;
        if (!mResetCommandBuffer) return VulkanSwapchainFrameSlotCommand::ResetCommandBuffer;
        if (!mBeginCommandBuffer) return VulkanSwapchainFrameSlotCommand::BeginCommandBuffer;
        if (!mCmdPipelineBarrier) return VulkanSwapchainFrameSlotCommand::CmdPipelineBarrier;
        if (!mEndCommandBuffer) return VulkanSwapchainFrameSlotCommand::EndCommandBuffer;
        if (!mResetFences) return VulkanSwapchainFrameSlotCommand::ResetFences;
        if (!mQueueSubmit) return VulkanSwapchainFrameSlotCommand::QueueSubmit;
        if (!mQueuePresent) return VulkanSwapchainFrameSlotCommand::QueuePresent;
        return std::nullopt;
    };
    if (const auto command = missing_command())
    {
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::MissingRequiredCommand, mDisposition, command);
    }

    const VkFence prior_fences[] = { mSubmissionFence, mPresentCompletionFence };
    VkResult result = mWaitForFences(mDevice, 2, prior_fences, VK_TRUE, std::numeric_limits<std::uint64_t>::max());
    if (result != VK_SUCCESS)
    {
        mDisposition = result == VK_ERROR_DEVICE_LOST ? VulkanSwapchainFrameSlotDisposition::DeviceLost
                                                      : VulkanSwapchainFrameSlotDisposition::Reusable;
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::CommandFailure, mDisposition,
                                VulkanSwapchainFrameSlotCommand::WaitForFences, result);
    }
    mSubmissionFenceSignaled        = true;
    mPresentCompletionFenceSignaled = true;

    std::uint32_t image_index = 0;
    result = mAcquireNextImage(mDevice, mSwapchain, VULKAN_SWAPCHAIN_FRAME_ACQUIRE_TIMEOUT_NS, mImageAvailableSemaphore,
                               VK_NULL_HANDLE, &image_index);
    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        return VulkanSwapchainFrameSlotPresentationSuccess{
            VulkanSwapchainFrameSlotPresentationOutcome::SwapchainReplacementRequired, std::nullopt };
    }
    if (result == VK_ERROR_SURFACE_LOST_KHR)
    {
        return VulkanSwapchainFrameSlotPresentationSuccess{ VulkanSwapchainFrameSlotPresentationOutcome::SurfaceLost, std::nullopt };
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
        mDisposition = result == VK_ERROR_DEVICE_LOST ? VulkanSwapchainFrameSlotDisposition::DeviceLost
                                                      : VulkanSwapchainFrameSlotDisposition::Reusable;
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

    result = mResetCommandBuffer(mCommandBuffer, 0);
    if (result != VK_SUCCESS)
    {
        if (result == VK_ERROR_DEVICE_LOST)
        {
            mDisposition = VulkanSwapchainFrameSlotDisposition::DeviceLost;
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
        }
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::CommandFailure, mDisposition,
                                VulkanSwapchainFrameSlotCommand::BeginCommandBuffer, result, image_index);
    }

    VkImageMemoryBarrier barrier{};
    barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask                   = 0;
    barrier.dstAccessMask                   = 0;
    barrier.oldLayout                       = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout                       = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier.image                           = mImagesGeneration->image(image_index);
    barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel   = 0;
    barrier.subresourceRange.levelCount     = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount     = 1;
    mCmdPipelineBarrier(mCommandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr,
                        0, nullptr, 1, &barrier);

    result = mEndCommandBuffer(mCommandBuffer);
    if (result != VK_SUCCESS)
    {
        if (result == VK_ERROR_DEVICE_LOST)
        {
            mDisposition = VulkanSwapchainFrameSlotDisposition::DeviceLost;
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

    const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
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
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::CommandFailure, mDisposition,
                                VulkanSwapchainFrameSlotCommand::QueuePresent, present_result, image_index);
    }

    mDisposition = VulkanSwapchainFrameSlotDisposition::Reusable;
    if (present_result == VK_SUCCESS || present_result == VK_SUBOPTIMAL_KHR || present_result == VK_ERROR_OUT_OF_DATE_KHR ||
        present_result == VK_ERROR_SURFACE_LOST_KHR)
    {
        return VulkanSwapchainFrameSlotPresentationSuccess{ outcome, image_index };
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
            return mDisposition;
        }
        // A non-success result does not say whether release took effect.
        // Retrying could release an image that is no longer acquired.
        mDisposition = VulkanSwapchainFrameSlotDisposition::ReleaseIndeterminate;
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
        }
        return operationFailure(VulkanSwapchainFrameSlotOperationCode::CommandFailure, mDisposition,
                                VulkanSwapchainFrameSlotCommand::WaitForFences, wait_result, mAcquiredImageIndex);
    }

    mCancellationSubmissionPending = false;
    if (mCancellationSubmitReportedDeviceLost)
    {
        mCancellationSubmitReportedDeviceLost = false;
        mDisposition                          = VulkanSwapchainFrameSlotDisposition::DeviceLost;
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
    if (mDisposition == VulkanSwapchainFrameSlotDisposition::Pending ||
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

    mGetInstanceProcAddr                 = nullptr;
    mInstance                            = VK_NULL_HANDLE;
    mSurface                             = VK_NULL_HANDLE;
    mPhysicalDevice                      = VK_NULL_HANDLE;
    mPhysicalDeviceIndex                 = 0;
    mDevice                              = VK_NULL_HANDLE;
    mQueue                               = VK_NULL_HANDLE;
    mQueueFamilyIndex                    = VK_QUEUE_FAMILY_IGNORED;
    mQueueIndex                          = 0;
    mDrawableExtent                      = {};
    mSwapchain                           = VK_NULL_HANDLE;
    mImageFormat                         = VK_FORMAT_UNDEFINED;
    mImageCount                          = 0;
    mImagesGeneration                    = nullptr;
    mDestroyCommandPool                  = nullptr;
    mDestroySemaphore                    = nullptr;
    mDestroyFence                        = nullptr;
    mWaitForFences                       = nullptr;
    mResetCommandBuffer                  = nullptr;
    mBeginCommandBuffer                  = nullptr;
    mEndCommandBuffer                    = nullptr;
    mResetFences                         = nullptr;
    mQueueSubmit                         = nullptr;
    mAcquireNextImage                    = nullptr;
    mCmdPipelineBarrier                  = nullptr;
    mQueuePresent                        = nullptr;
    mReleaseSwapchainImages              = nullptr;
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
