/**
 * @file llwindowvulkanwin32wsi_test.cpp
 * @brief Opt-in native smoke for the isolated Win32 Vulkan owner.
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

#include "linden_common.h"

#include "llgl.h"
#include "llrendervulkanglobaldispatch.h"
#include "llrendervulkaninstance.h"
#include "llwin32headers.h"
#include "llwindow.h"
#include "llwindowwin32vulkan.h"
#include "lltut.h"

#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <variant>

namespace
{

constexpr char NATIVE_SMOKE_ENVIRONMENT[] = "LL_RUN_VULKAN_WIN32_WSI_NATIVE";
constexpr char LOADER_PATH_ENVIRONMENT[]  = "LL_VULKAN_WIN32_WSI_LOADER";
constexpr U64  NATIVE_WINDOW_GENERATION   = 36;
constexpr U32  CLIENT_WIDTH               = 1280;
constexpr U32  CLIENT_HEIGHT              = 720;

bool nativeSmokeRequested()
{
    const char* value = std::getenv(NATIVE_SMOKE_ENVIRONMENT);
    return value && std::string_view(value) == "1";
}

std::optional<std::wstring> utf8ToWide(const char* value)
{
    if (!value || value[0] == '\0')
    {
        return std::nullopt;
    }

    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, nullptr, 0);
    if (length <= 1)
    {
        return std::nullopt;
    }

    std::wstring converted(static_cast<std::size_t>(length), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, converted.data(), length) != length)
    {
        return std::nullopt;
    }
    converted.pop_back();
    return converted;
}

} // namespace

namespace tut
{

struct window_vulkan_win32_wsi_test
{
};

using window_vulkan_win32_wsi_group  = test_group<window_vulkan_win32_wsi_test>;
using window_vulkan_win32_wsi_object = window_vulkan_win32_wsi_group::object;
window_vulkan_win32_wsi_group window_vulkan_win32_wsi_tests("window Vulkan Win32 WSI native");

template<>
template<>
void window_vulkan_win32_wsi_object::test<1>()
{
    if (!nativeSmokeRequested())
    {
        ensure("the native Vulkan Win32 smoke remains explicitly opt-in", true);
        return;
    }

    const auto loader_path = utf8ToWide(std::getenv(LOADER_PATH_ENVIRONMENT));
    ensure("the native Vulkan Win32 smoke requires an explicit UTF-8 loader path", loader_path && !loader_path->empty());

    const HGLRC       initial_wgl_context  = wglGetCurrentContext();
    const HDC         initial_wgl_dc       = wglGetCurrentDC();
    const HWND        initial_focus        = GetFocus();
    const bool        initial_gl_manager   = gGLManager.mInited;
    const std::size_t initial_window_count = LLWindow::instanceCount();
    const U64         owner_thread_id      = static_cast<U64>(GetCurrentThreadId());

    LLWindowWin32VulkanCreateInfo create_info;
    create_info.mLoaderPath    = *loader_path;
    create_info.mOwnerThreadId = owner_thread_id;
    create_info.mClientWidth   = CLIENT_WIDTH;
    create_info.mClientHeight  = CLIENT_HEIGHT;

    LLWindowWin32VulkanAcquireResult result = acquireLLWindowWin32Vulkan(create_info, NATIVE_WINDOW_GENERATION);
    auto*                            owner  = std::get_if<LLWindowWin32Vulkan>(&result);
    ensure("the default Win32 operations acquire a hidden native owner", owner != nullptr);

    ensure("the native owner retains its hidden Win32 window", owner->hasNativeWindow());
    ensure_equals("the native owner retains the creator thread", owner->ownerThreadId(), owner_thread_id);
    ensure_equals("the native owner reports the exact client width", owner->clientWidth(), CLIENT_WIDTH);
    ensure_equals("the native owner reports the exact client height", owner->clientHeight(), CLIENT_HEIGHT);
    ensure("the native owner refreshes its private HWND geometry", owner->refreshNativeGeometry());
    ensure_equals("the refreshed HWND retains the exact client width", owner->clientWidth(), CLIENT_WIDTH);
    ensure_equals("the refreshed HWND retains the exact client height", owner->clientHeight(), CLIENT_HEIGHT);
    ensure_equals("the isolated owner does not enter LLWindowManager", LLWindow::instanceCount(), initial_window_count);
    ensure("native acquisition changes no current WGL context", wglGetCurrentContext() == initial_wgl_context);
    ensure("native acquisition changes no current WGL device context", wglGetCurrentDC() == initial_wgl_dc);
    ensure("native acquisition does not take keyboard focus", GetFocus() == initial_focus);
    ensure_equals("native acquisition leaves the OpenGL manager unchanged", gGLManager.mInited, initial_gl_manager);

    const LLWindowVulkanRequirements* requirements = owner->requirements();
    ensure("the native owner publishes Vulkan instance requirements", owner->hasRequirements() && requirements != nullptr);
    ensure_equals("the native requirements retain the exact nonzero generation", requirements->nativeWindowGeneration(),
                  NATIVE_WINDOW_GENERATION);
    ensure("the native owner accepts only its current nonzero generation",
           owner->isGenerationCurrent(NATIVE_WINDOW_GENERATION) && !owner->isGenerationCurrent(0));

    const auto& required_extensions = requirements->requiredInstanceExtensions();
    ensure_equals("the Win32 producer publishes exactly two required extensions", required_extensions.size(), std::size_t{ 2 });
    ensure_equals("the base surface extension remains first", required_extensions[0], std::string("VK_KHR_surface"));
    ensure_equals("the Win32 surface extension remains second", required_extensions[1], std::string("VK_KHR_win32_surface"));
    ensure("the requirements retain a non-null loader resolver", requirements->resolver() != nullptr);

    const auto resolver = reinterpret_cast<PFN_vkGetInstanceProcAddr>(requirements->resolver());
    {
        auto        dispatch_result = LLRenderVulkan::resolveVulkanGlobalDispatchGeneration(resolver);
        const auto* global_dispatch = std::get_if<LLRenderVulkan::VulkanGlobalDispatchGeneration>(&dispatch_result);
        ensure("the explicit Win32 loader satisfies the global-dispatch contract", global_dispatch != nullptr);
        ensure("global dispatch retains the exact requirements resolver", global_dispatch->getInstanceProcAddr() == resolver);
        ensure("the explicit Win32 loader supports Vulkan 1.1", global_dispatch->loaderApiVersion() >= VK_API_VERSION_1_1);
    }

    const auto instance_error = owner->acquireInstanceGeneration(LLRenderVulkan::VulkanInstanceValidationMode::Required);
    ensure("the current Win32 requirements acquire a Vulkan instance", !instance_error.has_value());

    const LLRenderVulkan::VulkanInstanceGeneration* instance_generation = owner->instanceGeneration();
    ensure("the native owner publishes its exact Vulkan instance generation", instance_generation != nullptr);
    ensure("the acquired Vulkan instance handle is non-null", instance_generation->instance() != VK_NULL_HANDLE);
    ensure_equals("the acquired Vulkan instance requests API 1.1", instance_generation->apiVersion(), VK_API_VERSION_1_1);
    ensure_equals("the Vulkan instance retains the exact native generation", instance_generation->nativeWindowGeneration(),
                  NATIVE_WINDOW_GENERATION);
    ensure("the Vulkan instance generation remains current for this owner",
           owner->isGenerationCurrent(instance_generation->nativeWindowGeneration()));
    ensure("the Vulkan instance enables required validation", instance_generation->validationEnabled());
    ensure("the Win32 instance keeps portability enumeration disabled", !instance_generation->portabilityEnumerationEnabled());
    ensure("the Win32 instance does not enable the portability extension",
           !instance_generation->isExtensionEnabled("VK_KHR_portability_enumeration"));

    const auto& enabled_extensions = instance_generation->enabledExtensions();
    ensure("the Vulkan instance retains both required window extensions", enabled_extensions.size() >= required_extensions.size());
    ensure_equals("the Vulkan instance keeps the base surface extension first", enabled_extensions[0], required_extensions[0]);
    ensure_equals("the Vulkan instance keeps the Win32 surface extension second", enabled_extensions[1], required_extensions[1]);

    const auto surface_error = owner->acquireSurfaceGeneration();
    ensure("the current owner acquires a real Win32 Vulkan surface", !surface_error.has_value());
    ensure("the exact instance parent owns one surface generation", instance_generation->hasSurfaceGeneration());
    ensure("the real Win32 Vulkan surface handle is non-null", instance_generation->surface() != VK_NULL_HANDLE);
    ensure_equals("the surface retains the exact native generation", instance_generation->surfaceNativeWindowGeneration(),
                  NATIVE_WINDOW_GENERATION);
    ensure("surface acquisition changes no current WGL context", wglGetCurrentContext() == initial_wgl_context);
    ensure("surface acquisition changes no current WGL device context", wglGetCurrentDC() == initial_wgl_dc);
    ensure("surface acquisition does not take keyboard focus", GetFocus() == initial_focus);
    ensure_equals("surface acquisition leaves the OpenGL manager unchanged", gGLManager.mInited, initial_gl_manager);

    bool        off_thread_surface_reset = true;
    bool        off_thread_full_reset    = true;
    std::thread off_thread(
        [&]()
        {
            off_thread_surface_reset = owner->resetSurfaceGeneration();
            off_thread_full_reset    = owner->reset();
        });
    off_thread.join();
    ensure("an actual non-owner thread cannot reset the Vulkan surface", !off_thread_surface_reset);
    ensure("an actual non-owner thread cannot reset the full native owner", !off_thread_full_reset);
    ensure("off-thread rejection preserves the exact Vulkan instance parent", owner->instanceGeneration() == instance_generation);
    ensure("off-thread rejection preserves the exact Vulkan surface child", instance_generation->hasSurfaceGeneration());
    ensure("off-thread rejection preserves the private Win32 window", owner->hasNativeWindow());
    ensure("off-thread rejection preserves the exact requirements generation",
           owner->requirements() == requirements && owner->isGenerationCurrent(NATIVE_WINDOW_GENERATION));

    ensure("the native smoke explicitly resets the Vulkan surface", owner->resetSurfaceGeneration());
    ensure("explicit reset removes only the surface child",
           !instance_generation->hasSurfaceGeneration() && instance_generation->surface() == VK_NULL_HANDLE);
    ensure("the exact instance parent remains live after surface reset",
           owner->instanceGeneration() == instance_generation && instance_generation->instance() != VK_NULL_HANDLE);
    ensure("required validation remains live during explicit surface destruction", instance_generation->validationEnabled());
    ensure("the private Win32 owner remains live after surface reset", owner->hasNativeWindow());
    ensure("the private HWND geometry remains live after surface reset", owner->refreshNativeGeometry());
    ensure_equals("the live HWND retains its client width", owner->clientWidth(), CLIENT_WIDTH);
    ensure_equals("the live HWND retains its client height", owner->clientHeight(), CLIENT_HEIGHT);
    ensure("the exact requirements generation remains live after surface reset",
           owner->requirements() == requirements && owner->isGenerationCurrent(NATIVE_WINDOW_GENERATION));

    {
        auto        dispatch_after_surface_reset = LLRenderVulkan::resolveVulkanGlobalDispatchGeneration(resolver);
        const auto* live_global_dispatch = std::get_if<LLRenderVulkan::VulkanGlobalDispatchGeneration>(&dispatch_after_surface_reset);
        ensure("the loader remains live while the surface child is absent", live_global_dispatch != nullptr);
        ensure("the live loader still resolves through the exact requirements function",
               live_global_dispatch->getInstanceProcAddr() == resolver);
    }
    ensure_equals("surface creation and destruction emit no validation messages", instance_generation->validationSnapshot().mMessageCount,
                  std::uint32_t{ 0 });
    ensure("surface reset changes no current WGL context", wglGetCurrentContext() == initial_wgl_context);
    ensure("surface reset changes no current WGL device context", wglGetCurrentDC() == initial_wgl_dc);
    ensure("surface reset does not change keyboard focus", GetFocus() == initial_focus);
    ensure_equals("surface reset leaves the OpenGL manager unchanged", gGLManager.mInited, initial_gl_manager);

    ensure("creator-thread native teardown succeeds", owner->reset());

    ensure("clean teardown removes the private Win32 owner", !owner->hasNativeWindow());
    ensure("clean teardown removes the Vulkan requirements", !owner->hasRequirements() && owner->requirements() == nullptr);
    ensure("clean teardown removes the Vulkan instance parent", owner->instanceGeneration() == nullptr);
    ensure_equals("clean teardown leaves LLWindowManager unchanged", LLWindow::instanceCount(), initial_window_count);
    ensure("clean teardown leaves the initial WGL context unchanged", wglGetCurrentContext() == initial_wgl_context);
    ensure("clean teardown leaves the initial WGL device context unchanged", wglGetCurrentDC() == initial_wgl_dc);
    ensure("clean teardown leaves keyboard focus unchanged", GetFocus() == initial_focus);
    ensure_equals("clean teardown leaves the OpenGL manager unchanged", gGLManager.mInited, initial_gl_manager);
}

} // namespace tut
