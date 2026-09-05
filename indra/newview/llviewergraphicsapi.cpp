/**
 * @file llviewergraphicsapi.cpp
 * @brief Selects the viewer window graphics API.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2026, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"

#include "llviewergraphicsapi.h"

LLViewerGraphicsAPISelection selectViewerGraphicsAPI(bool headless_requested, bool vulkan_requested, bool vulkan_available) noexcept
{
    if (headless_requested && vulkan_requested)
    {
        return LLViewerGraphicsAPISelectionError::HeadlessVulkanConflict;
    }
    if (headless_requested)
    {
        return LLWindow::GraphicsAPI::Headless;
    }
    if (!vulkan_requested)
    {
        return LLWindow::GraphicsAPI::OpenGL;
    }
    if (!vulkan_available)
    {
        return LLViewerGraphicsAPISelectionError::UnsupportedVulkan;
    }
    return LLWindow::GraphicsAPI::Vulkan;
}
