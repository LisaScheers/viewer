/**
 * @file llwindowmacosxvulkan-objc.h
 * @brief Opaque Cocoa window bridge for the opt-in Vulkan WSI diagnostic.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2026, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#ifndef LL_LLWINDOWMACOSXVULKAN_OBJC_H
#define LL_LLWINDOWMACOSXVULKAN_OBJC_H

#include <stdint.h>

#if defined(__cplusplus)
extern "C"
{
#define LLWINDOWMACOSXVULKAN_NOEXCEPT noexcept
#else
#define LLWINDOWMACOSXVULKAN_NOEXCEPT
#endif

typedef enum LLWindowMacOSXVulkanStatus
{
    LLWINDOWMACOSXVULKAN_STATUS_SUCCESS = 0,
    LLWINDOWMACOSXVULKAN_STATUS_INVALID_ARGUMENT,
    LLWINDOWMACOSXVULKAN_STATUS_MAIN_THREAD_REQUIRED,
    LLWINDOWMACOSXVULKAN_STATUS_APPLICATION_FAILED,
    LLWINDOWMACOSXVULKAN_STATUS_STORAGE_FAILED,
    LLWINDOWMACOSXVULKAN_STATUS_WINDOW_FAILED,
    LLWINDOWMACOSXVULKAN_STATUS_VIEW_FAILED,
    LLWINDOWMACOSXVULKAN_STATUS_LAYER_FAILED,
    LLWINDOWMACOSXVULKAN_STATUS_GEOMETRY_FAILED,
    LLWINDOWMACOSXVULKAN_STATUS_DESTROY_FAILED,
    LLWINDOWMACOSXVULKAN_STATUS_DRAWABLE_UNAVAILABLE
} LLWindowMacOSXVulkanStatus;

typedef struct LLWindowMacOSXVulkanNative
{
    void* token;
    void* window;
    void* view;
    void* layer;
    double contents_scale;
    uint32_t drawable_width;
    uint32_t drawable_height;
} LLWindowMacOSXVulkanNative;

LLWindowMacOSXVulkanStatus llwindow_macosx_vulkan_native_create(
    uint32_t backing_width,
    uint32_t backing_height,
    LLWindowMacOSXVulkanNative* out_native) LLWINDOWMACOSXVULKAN_NOEXCEPT;

LLWindowMacOSXVulkanStatus llwindow_macosx_vulkan_native_refresh(
    LLWindowMacOSXVulkanNative* native) LLWINDOWMACOSXVULKAN_NOEXCEPT;

LLWindowMacOSXVulkanStatus llwindow_macosx_vulkan_native_resize_for_diagnostic(
    uint32_t backing_width,
    uint32_t backing_height,
    LLWindowMacOSXVulkanNative* native) LLWINDOWMACOSXVULKAN_NOEXCEPT;

LLWindowMacOSXVulkanStatus llwindow_macosx_vulkan_native_destroy(
    LLWindowMacOSXVulkanNative* native) LLWINDOWMACOSXVULKAN_NOEXCEPT;

#if defined(__cplusplus)
}
#endif

#undef LLWINDOWMACOSXVULKAN_NOEXCEPT

#endif // LL_LLWINDOWMACOSXVULKAN_OBJC_H
