/**
 * @file llrendervulkanswapchainframeslot.h
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

#ifndef LL_LLRENDERVULKANSWAPCHAINFRAMESLOT_H
#define LL_LLRENDERVULKANSWAPCHAINFRAMESLOT_H

#include "llrendervulkanswapchainimages.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>
#include <variant>

namespace LLRenderVulkan
{

enum class VulkanSwapchainFrameSlotCommand : std::uint8_t
{
    GetDeviceProcAddr,
    CreateCommandPool,
    DestroyCommandPool,
    AllocateCommandBuffers,
    CreateSemaphore,
    DestroySemaphore,
    CreateFence,
    DestroyFence,
    WaitForFences,
    ResetCommandBuffer,
    BeginCommandBuffer,
    EndCommandBuffer,
    ResetFences,
    QueueSubmit
};

enum class VulkanSwapchainFrameSlotResolutionCode : std::uint8_t
{
    InvalidLogicalDeviceGeneration,
    InvalidSwapchainConfigurationGeneration,
    InvalidSwapchainGeneration,
    InvalidSwapchainImagesGeneration,
    MissingRequiredCommand,
    CommandPoolCreationFailure,
    NullCommandPoolOnSuccess,
    CommandBufferAllocationFailure,
    NullCommandBufferOnSuccess,
    ImageAvailableSemaphoreCreationFailure,
    NullImageAvailableSemaphoreOnSuccess,
    SubmissionFenceCreationFailure,
    NullSubmissionFenceOnSuccess
};

struct VulkanSwapchainFrameSlotResolutionError
{
    VulkanSwapchainFrameSlotResolutionCode         mCode = VulkanSwapchainFrameSlotResolutionCode::InvalidLogicalDeviceGeneration;
    std::optional<VulkanSwapchainFrameSlotCommand> mCommand;
    VkResult                                       mResult = VK_SUCCESS;

    friend constexpr bool operator==(const VulkanSwapchainFrameSlotResolutionError&,
                                     const VulkanSwapchainFrameSlotResolutionError&) = default;
};

enum class VulkanSwapchainFrameSlotDisposition : std::uint8_t
{
    Reusable,
    ResetRequired,
    Pending,
    DeviceLost
};

enum class VulkanSwapchainFrameSlotOperationCode : std::uint8_t
{
    InvalidLogicalDeviceGeneration,
    InvalidSwapchainConfigurationGeneration,
    InvalidSwapchainGeneration,
    InvalidSwapchainImagesGeneration,
    MissingRequiredCommand,
    InvalidDisposition,
    CommandFailure
};

struct VulkanSwapchainFrameSlotOperationError
{
    VulkanSwapchainFrameSlotOperationCode          mCode = VulkanSwapchainFrameSlotOperationCode::InvalidLogicalDeviceGeneration;
    std::optional<VulkanSwapchainFrameSlotCommand> mCommand;
    VkResult                                       mResult      = VK_SUCCESS;
    VulkanSwapchainFrameSlotDisposition            mDisposition = VulkanSwapchainFrameSlotDisposition::Reusable;

    friend constexpr bool operator==(const VulkanSwapchainFrameSlotOperationError&,
                                     const VulkanSwapchainFrameSlotOperationError&) = default;
};

using VulkanSwapchainFrameSlotOperationResult = std::variant<VulkanSwapchainFrameSlotOperationError, VulkanSwapchainFrameSlotDisposition>;

// The command buffer is freed implicitly with its pool. The exact logical
// device, swapchain configuration, swapchain, and image generation must outlive
// this slot, which must be reset before any of those parents. Host access is
// externally serialized, including access to the retained queue. Calling reset
// or destroying this generation while disposition() is Pending violates the
// caller contract. Before any other reset or destruction, no operation may
// still use the fence or semaphore.
class VulkanSwapchainFrameSlotGeneration
{
public:
    ~VulkanSwapchainFrameSlotGeneration() noexcept;

    VulkanSwapchainFrameSlotGeneration(const VulkanSwapchainFrameSlotGeneration&)            = delete;
    VulkanSwapchainFrameSlotGeneration& operator=(const VulkanSwapchainFrameSlotGeneration&) = delete;
    VulkanSwapchainFrameSlotGeneration(VulkanSwapchainFrameSlotGeneration&& other) noexcept;
    VulkanSwapchainFrameSlotGeneration& operator=(VulkanSwapchainFrameSlotGeneration&&) = delete;

    VkCommandPool                       commandPool() const noexcept { return mCommandPool; }
    VkCommandBuffer                     commandBuffer() const noexcept { return mCommandBuffer; }
    VkSemaphore                         imageAvailableSemaphore() const noexcept { return mImageAvailableSemaphore; }
    VkFence                             submissionFence() const noexcept { return mSubmissionFence; }
    VulkanSwapchainFrameSlotDisposition disposition() const noexcept { return mDisposition; }

    bool createdFor(const VulkanLogicalDeviceGeneration&          logical_device_generation,
                    const VulkanSwapchainConfigurationGeneration& configuration_generation,
                    const VulkanSwapchainGeneration&              swapchain_generation,
                    const VulkanSwapchainImagesGeneration&        images_generation) const noexcept;

    // Resolution publishes all six commands together and changes no Vulkan
    // object. The caller may therefore recheck parent freshness before execute.
    VulkanSwapchainFrameSlotOperationResult resolveEmptySubmissionDispatch(
        const VulkanLogicalDeviceGeneration&          logical_device_generation,
        const VulkanSwapchainConfigurationGeneration& configuration_generation,
        const VulkanSwapchainGeneration&              swapchain_generation,
        const VulkanSwapchainImagesGeneration&        images_generation) noexcept;

    VulkanSwapchainFrameSlotOperationResult executeEmptySubmission() noexcept;
    VulkanSwapchainFrameSlotOperationResult retryEmptySubmissionCompletion() noexcept;

    // The caller must satisfy the external-synchronization precondition and
    // must not call reset while the disposition is Pending. A defensive
    // Pending check leaves ownership untouched, but does not retire the work.
    void reset() noexcept;

private:
    friend struct VulkanSwapchainFrameSlotGenerationFactory;

    VulkanSwapchainFrameSlotGeneration(const VulkanLogicalDeviceGeneration&          logical_device_generation,
                                       const VulkanSwapchainConfigurationGeneration& configuration_generation,
                                       const VulkanSwapchainGeneration&              swapchain_generation,
                                       const VulkanSwapchainImagesGeneration&        images_generation,
                                       VkCommandPool                                 command_pool,
                                       VkCommandBuffer                               command_buffer,
                                       VkSemaphore                                   image_available_semaphore,
                                       VkFence                                       submission_fence,
                                       PFN_vkDestroyCommandPool                      destroy_command_pool,
                                       PFN_vkDestroySemaphore                        destroy_semaphore,
                                       PFN_vkDestroyFence                            destroy_fence) noexcept;

    PFN_vkGetInstanceProcAddr              mGetInstanceProcAddr = nullptr;
    VkInstance                             mInstance            = VK_NULL_HANDLE;
    VkSurfaceKHR                           mSurface             = VK_NULL_HANDLE;
    VkPhysicalDevice                       mPhysicalDevice      = VK_NULL_HANDLE;
    std::uint32_t                          mPhysicalDeviceIndex = 0;
    VkDevice                               mDevice              = VK_NULL_HANDLE;
    VkQueue                                mQueue               = VK_NULL_HANDLE;
    std::uint32_t                          mQueueFamilyIndex    = VK_QUEUE_FAMILY_IGNORED;
    std::uint32_t                          mQueueIndex          = 0;
    VkExtent2D                             mDrawableExtent{};
    VkSwapchainKHR                         mSwapchain                           = VK_NULL_HANDLE;
    VkFormat                               mImageFormat                         = VK_FORMAT_UNDEFINED;
    std::uint32_t                          mImageCount                          = 0;
    const VulkanSwapchainImagesGeneration* mImagesGeneration                    = nullptr;
    VkCommandPool                          mCommandPool                         = VK_NULL_HANDLE;
    VkCommandBuffer                        mCommandBuffer                       = VK_NULL_HANDLE;
    VkSemaphore                            mImageAvailableSemaphore             = VK_NULL_HANDLE;
    VkFence                                mSubmissionFence                     = VK_NULL_HANDLE;
    PFN_vkDestroyCommandPool               mDestroyCommandPool                  = nullptr;
    PFN_vkDestroySemaphore                 mDestroySemaphore                    = nullptr;
    PFN_vkDestroyFence                     mDestroyFence                        = nullptr;
    PFN_vkWaitForFences                    mWaitForFences                       = nullptr;
    PFN_vkResetCommandBuffer               mResetCommandBuffer                  = nullptr;
    PFN_vkBeginCommandBuffer               mBeginCommandBuffer                  = nullptr;
    PFN_vkEndCommandBuffer                 mEndCommandBuffer                    = nullptr;
    PFN_vkResetFences                      mResetFences                         = nullptr;
    PFN_vkQueueSubmit                      mQueueSubmit                         = nullptr;
    VulkanSwapchainFrameSlotDisposition    mDisposition                         = VulkanSwapchainFrameSlotDisposition::Reusable;
    bool                                   mPendingSubmissionReportedDeviceLost = false;
};

using VulkanSwapchainFrameSlotResolutionResult = std::variant<VulkanSwapchainFrameSlotResolutionError, VulkanSwapchainFrameSlotGeneration>;

VulkanSwapchainFrameSlotResolutionResult resolveVulkanSwapchainFrameSlotGeneration(
    const VulkanLogicalDeviceGeneration&          logical_device_generation,
    const VulkanSwapchainConfigurationGeneration& configuration_generation,
    const VulkanSwapchainGeneration&              swapchain_generation,
    const VulkanSwapchainImagesGeneration&        images_generation) noexcept;

} // namespace LLRenderVulkan

#endif // LL_LLRENDERVULKANSWAPCHAINFRAMESLOT_H
