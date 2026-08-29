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

    VulkanSwapchainFrameSlotResolutionError failure(VulkanSwapchainFrameSlotResolutionCode         code,
                                                    std::optional<VulkanSwapchainFrameSlotCommand> command = std::nullopt,
                                                    VkResult                                       result  = VK_SUCCESS) noexcept
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
                  VkFence                           fence,
                  VkSemaphore                       semaphore,
                  VkCommandPool                     command_pool) noexcept
    {
        if (fence != VK_NULL_HANDLE)
        {
            dispatch.mDestroyFence(device, fence, nullptr);
        }
        if (semaphore != VK_NULL_HANDLE)
        {
            dispatch.mDestroySemaphore(device, semaphore, nullptr);
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
                                                     VkFence                                       submission_fence,
                                                     PFN_vkDestroyCommandPool                      destroy_command_pool,
                                                     PFN_vkDestroySemaphore                        destroy_semaphore,
                                                     PFN_vkDestroyFence                            destroy_fence) noexcept
    {
        return VulkanSwapchainFrameSlotGeneration(logical_device_generation, configuration_generation, swapchain_generation,
                                                  images_generation, command_pool, command_buffer, image_available_semaphore,
                                                  submission_fence, destroy_command_pool, destroy_semaphore, destroy_fence);
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
    VkFence                                       submission_fence,
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
    mSubmissionFence(submission_fence),
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
    mSubmissionFence(std::exchange(other.mSubmissionFence, VK_NULL_HANDLE)),
    mDestroyCommandPool(std::exchange(other.mDestroyCommandPool, nullptr)),
    mDestroySemaphore(std::exchange(other.mDestroySemaphore, nullptr)),
    mDestroyFence(std::exchange(other.mDestroyFence, nullptr))
{
}

bool VulkanSwapchainFrameSlotGeneration::createdFor(const VulkanLogicalDeviceGeneration&          logical_device_generation,
                                                    const VulkanSwapchainConfigurationGeneration& configuration_generation,
                                                    const VulkanSwapchainGeneration&              swapchain_generation,
                                                    const VulkanSwapchainImagesGeneration&        images_generation) const noexcept
{
    const VkExtent2D drawable_extent = configuration_generation.drawableExtent();
    return mCommandPool != VK_NULL_HANDLE && mCommandBuffer != VK_NULL_HANDLE && mImageAvailableSemaphore != VK_NULL_HANDLE &&
           mSubmissionFence != VK_NULL_HANDLE && mImagesGeneration == &images_generation &&
           images_generation.createdFor(logical_device_generation, configuration_generation, swapchain_generation) &&
           mGetInstanceProcAddr == logical_device_generation.getInstanceProcAddr() && mInstance == logical_device_generation.instance() &&
           mSurface == logical_device_generation.surface() && mPhysicalDevice == logical_device_generation.physicalDevice() &&
           mPhysicalDeviceIndex == logical_device_generation.physicalDeviceIndex() && mDevice == logical_device_generation.device() &&
           mQueue == logical_device_generation.queue() && mQueueFamilyIndex == logical_device_generation.queueFamilyIndex() &&
           mQueueIndex == logical_device_generation.queueIndex() && mDrawableExtent.width == drawable_extent.width &&
           mDrawableExtent.height == drawable_extent.height && mSwapchain == swapchain_generation.swapchain() &&
           mImageFormat == images_generation.imageFormat() && mImageCount == images_generation.imageCount();
}

void VulkanSwapchainFrameSlotGeneration::reset() noexcept
{
    if (mSubmissionFence != VK_NULL_HANDLE && mDestroyFence)
    {
        mDestroyFence(mDevice, mSubmissionFence, nullptr);
    }
    mSubmissionFence = VK_NULL_HANDLE;
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

    mGetInstanceProcAddr = nullptr;
    mInstance            = VK_NULL_HANDLE;
    mSurface             = VK_NULL_HANDLE;
    mPhysicalDevice      = VK_NULL_HANDLE;
    mPhysicalDeviceIndex = 0;
    mDevice              = VK_NULL_HANDLE;
    mQueue               = VK_NULL_HANDLE;
    mQueueFamilyIndex    = VK_QUEUE_FAMILY_IGNORED;
    mQueueIndex          = 0;
    mDrawableExtent      = {};
    mSwapchain           = VK_NULL_HANDLE;
    mImageFormat         = VK_FORMAT_UNDEFINED;
    mImageCount          = 0;
    mImagesGeneration    = nullptr;
    mDestroyCommandPool  = nullptr;
    mDestroySemaphore    = nullptr;
    mDestroyFence        = nullptr;
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
        rollback(logical_device_generation.device(), dispatch, VK_NULL_HANDLE, VK_NULL_HANDLE, command_pool);
        return failure(VulkanSwapchainFrameSlotResolutionCode::CommandBufferAllocationFailure,
                       VulkanSwapchainFrameSlotCommand::AllocateCommandBuffers, result);
    }
    if (command_buffer == VK_NULL_HANDLE)
    {
        rollback(logical_device_generation.device(), dispatch, VK_NULL_HANDLE, VK_NULL_HANDLE, command_pool);
        return failure(VulkanSwapchainFrameSlotResolutionCode::NullCommandBufferOnSuccess,
                       VulkanSwapchainFrameSlotCommand::AllocateCommandBuffers);
    }

    VkSemaphoreCreateInfo semaphore_info{};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkSemaphore image_available_semaphore;
    result = dispatch.mCreateSemaphore(logical_device_generation.device(), &semaphore_info, nullptr, &image_available_semaphore);
    if (result != VK_SUCCESS)
    {
        rollback(logical_device_generation.device(), dispatch, VK_NULL_HANDLE, VK_NULL_HANDLE, command_pool);
        return failure(VulkanSwapchainFrameSlotResolutionCode::ImageAvailableSemaphoreCreationFailure,
                       VulkanSwapchainFrameSlotCommand::CreateSemaphore, result);
    }
    if (image_available_semaphore == VK_NULL_HANDLE)
    {
        rollback(logical_device_generation.device(), dispatch, VK_NULL_HANDLE, VK_NULL_HANDLE, command_pool);
        return failure(VulkanSwapchainFrameSlotResolutionCode::NullImageAvailableSemaphoreOnSuccess,
                       VulkanSwapchainFrameSlotCommand::CreateSemaphore);
    }

    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    VkFence submission_fence;
    result = dispatch.mCreateFence(logical_device_generation.device(), &fence_info, nullptr, &submission_fence);
    if (result != VK_SUCCESS)
    {
        rollback(logical_device_generation.device(), dispatch, VK_NULL_HANDLE, image_available_semaphore, command_pool);
        return failure(VulkanSwapchainFrameSlotResolutionCode::SubmissionFenceCreationFailure, VulkanSwapchainFrameSlotCommand::CreateFence,
                       result);
    }
    if (submission_fence == VK_NULL_HANDLE)
    {
        rollback(logical_device_generation.device(), dispatch, VK_NULL_HANDLE, image_available_semaphore, command_pool);
        return failure(VulkanSwapchainFrameSlotResolutionCode::NullSubmissionFenceOnSuccess, VulkanSwapchainFrameSlotCommand::CreateFence);
    }

    return VulkanSwapchainFrameSlotGenerationFactory::create(
        logical_device_generation, configuration_generation, swapchain_generation, images_generation, command_pool, command_buffer,
        image_available_semaphore, submission_fence, dispatch.mDestroyCommandPool, dispatch.mDestroySemaphore, dispatch.mDestroyFence);
}

} // namespace LLRenderVulkan
