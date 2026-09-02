/**
 * @file llrendervulkanswapchainpresentationtarget.cpp
 * @brief Loader-neutral Vulkan swapchain presentation-target ownership.
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

#include "llrendervulkanswapchainpresentationtarget.h"

#include <new>
#include <utility>

namespace LLRenderVulkan
{

namespace
{
    struct PresentationTargetDispatch
    {
        PFN_vkGetDeviceProcAddr  mGetDeviceProcAddr  = nullptr;
        PFN_vkCreateRenderPass   mCreateRenderPass   = nullptr;
        PFN_vkDestroyRenderPass  mDestroyRenderPass  = nullptr;
        PFN_vkCreateFramebuffer  mCreateFramebuffer  = nullptr;
        PFN_vkDestroyFramebuffer mDestroyFramebuffer = nullptr;
    };

    VulkanSwapchainPresentationTargetResolutionError failure(
        VulkanSwapchainPresentationTargetResolutionCode         code,
        std::optional<VulkanSwapchainPresentationTargetCommand> command = std::nullopt,
        VkResult                                                result  = VK_SUCCESS) noexcept
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
        const VkExtent2D image_extent    = configuration.imageExtent();
        return configuration.getInstanceProcAddr() == logical_device.getInstanceProcAddr() &&
               configuration.instance() == logical_device.instance() && configuration.surface() == logical_device.surface() &&
               configuration.physicalDevice() == logical_device.physicalDevice() &&
               configuration.physicalDeviceIndex() == logical_device.physicalDeviceIndex() &&
               configuration.device() == logical_device.device() && configuration.queueFamilyIndex() == logical_device.queueFamilyIndex() &&
               drawable_extent.width != 0 && drawable_extent.height != 0 && image_extent.width != 0 && image_extent.height != 0 &&
               configuration.imageCount() != 0 && configuration.surfaceFormat().format != VK_FORMAT_UNDEFINED;
    }

    std::optional<VulkanSwapchainPresentationTargetResolutionError> resolveDispatch(
        const VulkanLogicalDeviceGeneration& logical_device,
        PresentationTargetDispatch&          dispatch) noexcept
    {
        dispatch.mGetDeviceProcAddr = resolveInstance<PFN_vkGetDeviceProcAddr>(logical_device.getInstanceProcAddr(),
                                                                               logical_device.instance(), "vkGetDeviceProcAddr");
        if (!dispatch.mGetDeviceProcAddr)
        {
            return failure(VulkanSwapchainPresentationTargetResolutionCode::MissingRequiredCommand,
                           VulkanSwapchainPresentationTargetCommand::GetDeviceProcAddr);
        }

        dispatch.mCreateRenderPass =
            resolveDevice<PFN_vkCreateRenderPass>(dispatch.mGetDeviceProcAddr, logical_device.device(), "vkCreateRenderPass");
        if (!dispatch.mCreateRenderPass)
        {
            return failure(VulkanSwapchainPresentationTargetResolutionCode::MissingRequiredCommand,
                           VulkanSwapchainPresentationTargetCommand::CreateRenderPass);
        }
        dispatch.mDestroyRenderPass =
            resolveDevice<PFN_vkDestroyRenderPass>(dispatch.mGetDeviceProcAddr, logical_device.device(), "vkDestroyRenderPass");
        if (!dispatch.mDestroyRenderPass)
        {
            return failure(VulkanSwapchainPresentationTargetResolutionCode::MissingRequiredCommand,
                           VulkanSwapchainPresentationTargetCommand::DestroyRenderPass);
        }
        dispatch.mCreateFramebuffer =
            resolveDevice<PFN_vkCreateFramebuffer>(dispatch.mGetDeviceProcAddr, logical_device.device(), "vkCreateFramebuffer");
        if (!dispatch.mCreateFramebuffer)
        {
            return failure(VulkanSwapchainPresentationTargetResolutionCode::MissingRequiredCommand,
                           VulkanSwapchainPresentationTargetCommand::CreateFramebuffer);
        }
        dispatch.mDestroyFramebuffer =
            resolveDevice<PFN_vkDestroyFramebuffer>(dispatch.mGetDeviceProcAddr, logical_device.device(), "vkDestroyFramebuffer");
        if (!dispatch.mDestroyFramebuffer)
        {
            return failure(VulkanSwapchainPresentationTargetResolutionCode::MissingRequiredCommand,
                           VulkanSwapchainPresentationTargetCommand::DestroyFramebuffer);
        }
        return std::nullopt;
    }

    std::optional<VulkanSwapchainPresentationTargetResolutionError> allocationFailure(
        VulkanSwapchainPresentationTargetDetail::AllocationCheckpoint allocation_checkpoint,
        std::uint32_t                                                  image_count) noexcept
    {
        if (!allocation_checkpoint)
        {
            return std::nullopt;
        }
        try
        {
            allocation_checkpoint();
        }
        catch (const std::bad_alloc&)
        {
            auto error = failure(VulkanSwapchainPresentationTargetResolutionCode::ScratchAllocationFailure,
                                 VulkanSwapchainPresentationTargetCommand::CreateFramebuffer);
            error.mImageCount = image_count;
            return error;
        }
        return std::nullopt;
    }

    void destroyFramebuffersReverse(VkDevice                 device,
                                    PFN_vkDestroyFramebuffer destroy_framebuffer,
                                    VkFramebuffer*           framebuffers,
                                    std::uint32_t            count) noexcept
    {
        while (count != 0)
        {
            --count;
            if (framebuffers[count] != VK_NULL_HANDLE)
            {
                destroy_framebuffer(device, framebuffers[count], nullptr);
                framebuffers[count] = VK_NULL_HANDLE;
            }
        }
    }

    void rollback(VkDevice                          device,
                  const PresentationTargetDispatch& dispatch,
                  VkFramebuffer*                    framebuffers,
                  std::uint32_t                     framebuffer_count,
                  VkRenderPass                      render_pass) noexcept
    {
        destroyFramebuffersReverse(device, dispatch.mDestroyFramebuffer, framebuffers, framebuffer_count);
        if (render_pass != VK_NULL_HANDLE)
        {
            dispatch.mDestroyRenderPass(device, render_pass, nullptr);
        }
    }

} // namespace

struct VulkanSwapchainPresentationTargetGenerationFactory
{
    static VulkanSwapchainPresentationTargetGeneration create(
        const VulkanLogicalDeviceGeneration&          logical_device_generation,
        const VulkanSwapchainConfigurationGeneration& configuration_generation,
        const VulkanSwapchainGeneration&              swapchain_generation,
        const VulkanSwapchainImagesGeneration&        images_generation,
        VkRenderPass                                  render_pass,
        std::unique_ptr<VkFramebuffer[]>               framebuffers,
        PFN_vkDestroyRenderPass                       destroy_render_pass,
        PFN_vkDestroyFramebuffer                      destroy_framebuffer) noexcept
    {
        return VulkanSwapchainPresentationTargetGeneration(logical_device_generation, configuration_generation, swapchain_generation,
                                                           images_generation, render_pass, std::move(framebuffers), destroy_render_pass,
                                                           destroy_framebuffer);
    }
};

VulkanSwapchainPresentationTargetGeneration::VulkanSwapchainPresentationTargetGeneration(
    const VulkanLogicalDeviceGeneration&          logical_device_generation,
    const VulkanSwapchainConfigurationGeneration& configuration_generation,
    const VulkanSwapchainGeneration&              swapchain_generation,
    const VulkanSwapchainImagesGeneration&        images_generation,
    VkRenderPass                                  render_pass,
    std::unique_ptr<VkFramebuffer[]>               framebuffers,
    PFN_vkDestroyRenderPass                       destroy_render_pass,
    PFN_vkDestroyFramebuffer                      destroy_framebuffer) noexcept :
    mLogicalDeviceGeneration(&logical_device_generation),
    mConfigurationGeneration(&configuration_generation),
    mSwapchainGeneration(&swapchain_generation),
    mImagesGeneration(&images_generation),
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
    mImageExtent(configuration_generation.imageExtent()),
    mImageCount(images_generation.imageCount()),
    mRenderPass(render_pass),
    mFramebuffers(std::move(framebuffers)),
    mDestroyRenderPass(destroy_render_pass),
    mDestroyFramebuffer(destroy_framebuffer)
{
}

VulkanSwapchainPresentationTargetGeneration::~VulkanSwapchainPresentationTargetGeneration() noexcept
{
    reset();
}

VulkanSwapchainPresentationTargetGeneration::VulkanSwapchainPresentationTargetGeneration(
    VulkanSwapchainPresentationTargetGeneration&& other) noexcept :
    mLogicalDeviceGeneration(std::exchange(other.mLogicalDeviceGeneration, nullptr)),
    mConfigurationGeneration(std::exchange(other.mConfigurationGeneration, nullptr)),
    mSwapchainGeneration(std::exchange(other.mSwapchainGeneration, nullptr)),
    mImagesGeneration(std::exchange(other.mImagesGeneration, nullptr)),
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
    mImageExtent(std::exchange(other.mImageExtent, {})),
    mImageCount(std::exchange(other.mImageCount, 0)),
    mRenderPass(std::exchange(other.mRenderPass, VK_NULL_HANDLE)),
    mFramebuffers(std::move(other.mFramebuffers)),
    mDestroyRenderPass(std::exchange(other.mDestroyRenderPass, nullptr)),
    mDestroyFramebuffer(std::exchange(other.mDestroyFramebuffer, nullptr))
{
}

VkFramebuffer VulkanSwapchainPresentationTargetGeneration::framebuffer(std::uint32_t index) const noexcept
{
    return index < mImageCount && mFramebuffers ? mFramebuffers[index] : VK_NULL_HANDLE;
}

bool VulkanSwapchainPresentationTargetGeneration::createdFor(
    const VulkanLogicalDeviceGeneration&          logical_device_generation,
    const VulkanSwapchainConfigurationGeneration& configuration_generation,
    const VulkanSwapchainGeneration&              swapchain_generation,
    const VulkanSwapchainImagesGeneration&        images_generation) const noexcept
{
    const VkExtent2D drawable_extent = configuration_generation.drawableExtent();
    const VkExtent2D image_extent    = configuration_generation.imageExtent();
    if (mRenderPass == VK_NULL_HANDLE || !mFramebuffers || mImageCount == 0 ||
        mLogicalDeviceGeneration != &logical_device_generation || mConfigurationGeneration != &configuration_generation ||
        mSwapchainGeneration != &swapchain_generation || mImagesGeneration != &images_generation ||
        !images_generation.createdFor(logical_device_generation, configuration_generation, swapchain_generation) ||
        mGetInstanceProcAddr != logical_device_generation.getInstanceProcAddr() || mInstance != logical_device_generation.instance() ||
        mSurface != logical_device_generation.surface() || mPhysicalDevice != logical_device_generation.physicalDevice() ||
        mPhysicalDeviceIndex != logical_device_generation.physicalDeviceIndex() || mDevice != logical_device_generation.device() ||
        mQueue != logical_device_generation.queue() || mQueueFamilyIndex != logical_device_generation.queueFamilyIndex() ||
        mQueueIndex != logical_device_generation.queueIndex() || mDrawableExtent.width != drawable_extent.width ||
        mDrawableExtent.height != drawable_extent.height || mSwapchain != swapchain_generation.swapchain() ||
        mImageFormat != configuration_generation.surfaceFormat().format || mImageFormat != images_generation.imageFormat() ||
        mImageExtent.width != image_extent.width || mImageExtent.height != image_extent.height ||
        mImageCount != images_generation.imageCount())
    {
        return false;
    }

    for (std::uint32_t index = 0; index < mImageCount; ++index)
    {
        if (mFramebuffers[index] == VK_NULL_HANDLE)
        {
            return false;
        }
    }
    return true;
}

void VulkanSwapchainPresentationTargetGeneration::reset() noexcept
{
    if (mFramebuffers && mDestroyFramebuffer)
    {
        destroyFramebuffersReverse(mDevice, mDestroyFramebuffer, mFramebuffers.get(), mImageCount);
    }
    mFramebuffers.reset();
    mImageCount = 0;
    if (mRenderPass != VK_NULL_HANDLE && mDestroyRenderPass)
    {
        mDestroyRenderPass(mDevice, mRenderPass, nullptr);
    }
    mRenderPass       = VK_NULL_HANDLE;
    mImagesGeneration = nullptr;
}

namespace
{
    VulkanSwapchainPresentationTargetResolutionResult resolvePresentationTarget(
        const VulkanLogicalDeviceGeneration&                          logical_device_generation,
        const VulkanSwapchainConfigurationGeneration&                 configuration_generation,
        const VulkanSwapchainGeneration&                              swapchain_generation,
        const VulkanSwapchainImagesGeneration&                        images_generation,
        VulkanSwapchainPresentationTargetDetail::AllocationCheckpoint allocation_checkpoint) noexcept
    {
        if (!valid(logical_device_generation))
        {
            return failure(VulkanSwapchainPresentationTargetResolutionCode::InvalidLogicalDeviceGeneration);
        }
        if (!belongsTo(configuration_generation, logical_device_generation))
        {
            return failure(VulkanSwapchainPresentationTargetResolutionCode::InvalidSwapchainConfigurationGeneration);
        }
        if (!swapchain_generation.createdFor(logical_device_generation, configuration_generation))
        {
            return failure(VulkanSwapchainPresentationTargetResolutionCode::InvalidSwapchainGeneration);
        }
        if (!images_generation.createdFor(logical_device_generation, configuration_generation, swapchain_generation))
        {
            return failure(VulkanSwapchainPresentationTargetResolutionCode::InvalidSwapchainImagesGeneration);
        }

        const std::uint32_t image_count = images_generation.imageCount();
        for (std::uint32_t index = 0; index < image_count; ++index)
        {
            if (images_generation.imageView(index) == VK_NULL_HANDLE)
            {
                auto error        = failure(VulkanSwapchainPresentationTargetResolutionCode::InvalidSwapchainImageView);
                error.mImageCount = image_count;
                error.mImageIndex = index;
                return error;
            }
        }

        PresentationTargetDispatch dispatch;
        if (auto error = resolveDispatch(logical_device_generation, dispatch))
        {
            return *error;
        }

        if (auto error = allocationFailure(allocation_checkpoint, image_count))
        {
            return *error;
        }
        std::unique_ptr<VkFramebuffer[]> framebuffers(new (std::nothrow) VkFramebuffer[image_count]{});
        if (!framebuffers)
        {
            auto error        = failure(VulkanSwapchainPresentationTargetResolutionCode::ScratchAllocationFailure,
                                        VulkanSwapchainPresentationTargetCommand::CreateFramebuffer);
            error.mImageCount = image_count;
            return error;
        }

        VkAttachmentDescription attachment{};
        attachment.format         = images_generation.imageFormat();
        attachment.samples        = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachment.finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference color_attachment{};
        color_attachment.attachment = 0;
        color_attachment.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments    = &color_attachment;

        VkRenderPassCreateInfo render_pass_info{};
        render_pass_info.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        render_pass_info.attachmentCount = 1;
        render_pass_info.pAttachments    = &attachment;
        render_pass_info.subpassCount    = 1;
        render_pass_info.pSubpasses      = &subpass;

        VkRenderPass render_pass = VK_NULL_HANDLE;
        const VkResult render_pass_result =
            dispatch.mCreateRenderPass(logical_device_generation.device(), &render_pass_info, nullptr, &render_pass);
        if (render_pass_result != VK_SUCCESS)
        {
            auto error = failure(VulkanSwapchainPresentationTargetResolutionCode::RenderPassCreationFailure,
                                 VulkanSwapchainPresentationTargetCommand::CreateRenderPass, render_pass_result);
            error.mImageCount = image_count;
            return error;
        }
        if (render_pass == VK_NULL_HANDLE)
        {
            auto error = failure(VulkanSwapchainPresentationTargetResolutionCode::NullRenderPassOnSuccess,
                                 VulkanSwapchainPresentationTargetCommand::CreateRenderPass);
            error.mImageCount = image_count;
            return error;
        }

        const VkExtent2D image_extent = configuration_generation.imageExtent();
        for (std::uint32_t index = 0; index < image_count; ++index)
        {
            const VkImageView image_view = images_generation.imageView(index);
            VkFramebufferCreateInfo framebuffer_info{};
            framebuffer_info.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebuffer_info.renderPass      = render_pass;
            framebuffer_info.attachmentCount = 1;
            framebuffer_info.pAttachments    = &image_view;
            framebuffer_info.width           = image_extent.width;
            framebuffer_info.height          = image_extent.height;
            framebuffer_info.layers          = 1;

            VkFramebuffer framebuffer = VK_NULL_HANDLE;
            const VkResult framebuffer_result =
                dispatch.mCreateFramebuffer(logical_device_generation.device(), &framebuffer_info, nullptr, &framebuffer);
            if (framebuffer_result != VK_SUCCESS)
            {
                rollback(logical_device_generation.device(), dispatch, framebuffers.get(), index, render_pass);
                auto error = failure(VulkanSwapchainPresentationTargetResolutionCode::FramebufferCreationFailure,
                                     VulkanSwapchainPresentationTargetCommand::CreateFramebuffer, framebuffer_result);
                error.mImageCount = image_count;
                error.mImageIndex = index;
                return error;
            }
            if (framebuffer == VK_NULL_HANDLE)
            {
                rollback(logical_device_generation.device(), dispatch, framebuffers.get(), index, render_pass);
                auto error = failure(VulkanSwapchainPresentationTargetResolutionCode::NullFramebufferOnSuccess,
                                     VulkanSwapchainPresentationTargetCommand::CreateFramebuffer);
                error.mImageCount = image_count;
                error.mImageIndex = index;
                return error;
            }
            framebuffers[index] = framebuffer;
        }

        return VulkanSwapchainPresentationTargetGenerationFactory::create(
            logical_device_generation, configuration_generation, swapchain_generation, images_generation, render_pass,
            std::move(framebuffers), dispatch.mDestroyRenderPass, dispatch.mDestroyFramebuffer);
    }

} // namespace

VulkanSwapchainPresentationTargetResolutionResult resolveVulkanSwapchainPresentationTargetGeneration(
    const VulkanLogicalDeviceGeneration&          logical_device_generation,
    const VulkanSwapchainConfigurationGeneration& configuration_generation,
    const VulkanSwapchainGeneration&              swapchain_generation,
    const VulkanSwapchainImagesGeneration&        images_generation) noexcept
{
    return resolvePresentationTarget(logical_device_generation, configuration_generation, swapchain_generation, images_generation,
                                     nullptr);
}

namespace VulkanSwapchainPresentationTargetDetail
{

    VulkanSwapchainPresentationTargetResolutionResult resolve(
        const VulkanLogicalDeviceGeneration&          logical_device_generation,
        const VulkanSwapchainConfigurationGeneration& configuration_generation,
        const VulkanSwapchainGeneration&              swapchain_generation,
        const VulkanSwapchainImagesGeneration&        images_generation,
        AllocationCheckpoint                          allocation_checkpoint) noexcept
    {
        return resolvePresentationTarget(logical_device_generation, configuration_generation, swapchain_generation, images_generation,
                                         allocation_checkpoint);
    }

} // namespace VulkanSwapchainPresentationTargetDetail

} // namespace LLRenderVulkan
