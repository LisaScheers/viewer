/**
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

#ifndef LL_LLRENDERVULKANUI_H
#define LL_LLRENDERVULKANUI_H

#include "lluirenderframe.h"
#include <vulkan/vulkan.h>
#include <memory>
#include <string>

namespace LLRenderVulkan
{
// Application renderer resources on the existing logical device. The caller
// settles its frame slot before prepare/retireTarget/destruction.
class UIRenderer
{
public:
    UIRenderer();
    ~UIRenderer();
    UIRenderer(const UIRenderer&) = delete;
    UIRenderer& operator=(const UIRenderer&) = delete;
    bool initialize(PFN_vkGetInstanceProcAddr resolver, VkInstance instance, VkPhysicalDevice physical,
                    VkDevice device, VkQueue queue, std::uint32_t queueFamily);
    bool prepare(LLUIRender::Frame frame, VkRenderPass renderPass);
    void record(VkCommandBuffer commands, VkExtent2D extent) noexcept;
    void retireTarget() noexcept;
    const std::string& error() const;
private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};
}
#endif
