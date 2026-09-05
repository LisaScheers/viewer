/**
 * @file llviewergraphicsapi.h
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

#ifndef LL_LLVIEWERGRAPHICSAPI_H
#define LL_LLVIEWERGRAPHICSAPI_H

#include "llwindow.h"

#include <variant>

enum class LLViewerGraphicsAPISelectionError : U8
{
    UnsupportedVulkan,
    HeadlessVulkanConflict
};

using LLViewerGraphicsAPISelection = std::variant<LLWindow::GraphicsAPI, LLViewerGraphicsAPISelectionError>;

LLViewerGraphicsAPISelection selectViewerGraphicsAPI(bool headless_requested, bool vulkan_requested, bool vulkan_available) noexcept;

#endif // LL_LLVIEWERGRAPHICSAPI_H
