/**
 * @file llwindowvulkansdlwsi_test.cpp
 * @brief Opt-in native smoke for the SDL Vulkan window requirements path.
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
#include "llwindow.h"
#include "llwindowvulkanrequirements.h"
#include "lltut.h"

#include "SDL3/SDL.h"
#include "SDL3/SDL_vulkan.h"

#include <cstdlib>
#include <cstring>
#include <string_view>
#include <type_traits>
#include <variant>

namespace
{

constexpr const char* NATIVE_SMOKE_ENVIRONMENT = "LL_RUN_VULKAN_SDL_WSI_NATIVE";

struct SDLState
{
    SDL_LogOutputFunction mLogOutput   = nullptr;
    void*                 mLogUserdata = nullptr;
    SDL_InitFlags         mInitialized = 0;
    SDL_GLContext         mGLContext   = nullptr;
};

SDLState currentSDLState()
{
    SDLState state;
    SDL_GetLogOutputFunction(&state.mLogOutput, &state.mLogUserdata);
    state.mInitialized = SDL_WasInit(0);
    state.mGLContext   = SDL_GL_GetCurrentContext();
    return state;
}

bool nativeSmokeRequested()
{
    const char* value = std::getenv(NATIVE_SMOKE_ENVIRONMENT);
    return value && std::string_view(value) == "1";
}

} // namespace

namespace tut
{

struct window_vulkan_sdl_wsi_test
{
};

using window_vulkan_sdl_wsi_group  = test_group<window_vulkan_sdl_wsi_test>;
using window_vulkan_sdl_wsi_object = window_vulkan_sdl_wsi_group::object;
window_vulkan_sdl_wsi_group window_vulkan_sdl_wsi_tests("window Vulkan SDL WSI native");

template<>
template<>
void window_vulkan_sdl_wsi_object::test<1>()
{
    static_assert(std::is_same_v<decltype(std::declval<const LLWindow&>().getVulkanRequirements()),
                                 const LLWindowVulkanRequirements*>);
    static_assert(noexcept(std::declval<const LLWindow&>().getVulkanRequirements()));
    static_assert(noexcept(std::declval<const LLWindow&>().isVulkanWindowGenerationCurrent(U64{})));

    if (!nativeSmokeRequested())
    {
        ensure("the native Vulkan SDL smoke remains explicitly opt-in", true);
        return;
    }

    const std::size_t initial_instance_count = LLWindow::instanceCount();
    const SDLState    initial_sdl_state       = currentSDLState();
    const bool        initial_gl_manager      = gGLManager.mInited;

    LLWindow* window = LLWindowManager::createWindow(nullptr, "SDL Vulkan native smoke", "llwindowvulkansdlwsi", 0, 0, 64, 64,
                                                     LLWindow::GraphicsAPI::Vulkan, LLWindow::FLAG_CREATE_HIDDEN);

    bool created                     = window != nullptr;
    bool tracked                     = created && LLWindowManager::isWindowValid(window);
    bool selected_vulkan             = created && window->getGraphicsAPI() == LLWindow::GraphicsAPI::Vulkan;
    bool x11_driver                  = created && SDL_GetCurrentVideoDriver() && std::strcmp(SDL_GetCurrentVideoDriver(), "x11") == 0;
    bool no_gl_context               = created && SDL_GL_GetCurrentContext() == nullptr;
    bool no_gl_manager               = created && !gGLManager.mInited;
    bool requirements_published      = false;
    bool generation_current          = false;
    bool resolver_identity           = false;
    bool extensions_identical        = false;
    bool global_dispatch_resolved    = false;
    bool mixed_opengl_rejected       = false;
    bool vulkan_context_switch_fails = false;
    bool vulkan_shared_context_fails = false;

    if (created)
    {
        const LLWindowVulkanRequirements* requirements = window->getVulkanRequirements();
        requirements_published = requirements != nullptr;
        if (requirements)
        {
            generation_current = window->isVulkanWindowGenerationCurrent(requirements->nativeWindowGeneration()) &&
                                 !window->isVulkanWindowGenerationCurrent(0);
            resolver_identity = requirements->resolver() == SDL_Vulkan_GetVkGetInstanceProcAddr();

            Uint32 extension_count = 0;
            const char* const* extension_names = SDL_Vulkan_GetInstanceExtensions(&extension_count);
            extensions_identical = extension_names && requirements->requiredInstanceExtensions().size() == extension_count;
            for (std::size_t index = 0; extensions_identical && index < extension_count; ++index)
            {
                extensions_identical = extension_names[index] && requirements->requiredInstanceExtensions()[index] == extension_names[index];
            }

            const auto dispatch_result = LLRenderVulkan::resolveVulkanGlobalDispatchGeneration(
                reinterpret_cast<PFN_vkGetInstanceProcAddr>(requirements->resolver()));
            global_dispatch_resolved = std::holds_alternative<LLRenderVulkan::VulkanGlobalDispatchGeneration>(dispatch_result);
        }

        const std::size_t count_before_mixed_request = LLWindow::instanceCount();
        LLWindow* mixed = LLWindowManager::createWindow(nullptr, "forbidden mixed OpenGL window", "llwindowvulkansdlwsi", 0, 0, 1, 1,
                                                        LLWindow::GraphicsAPI::OpenGL, LLWindow::FLAG_CREATE_HIDDEN);
        mixed_opengl_rejected = mixed == nullptr && LLWindow::instanceCount() == count_before_mixed_request;
        if (mixed && LLWindowManager::isWindowValid(mixed))
        {
            LLWindowManager::destroyWindow(mixed);
        }

        vulkan_context_switch_fails = !window->switchContext(false, LLCoordScreen(64, 64), false);
        vulkan_shared_context_fails = window->createSharedContext() == nullptr;
        window->toggleVSync(false);
        window->swapBuffers();
    }

    const bool destroyed = created && LLWindowManager::destroyWindow(window);
    const SDLState final_sdl_state = currentSDLState();

    ensure("a real SDL Vulkan window is created", created);
    ensure("the real SDL Vulkan window is tracked", tracked);
    ensure("the real SDL window retains the Vulkan selection", selected_vulkan);
    ensure("the native claim runs against SDL's X11 driver", x11_driver);
    ensure("the Vulkan path creates no current OpenGL context", no_gl_context);
    ensure("the Vulkan path does not initialize the OpenGL manager", no_gl_manager);
    ensure("the window publishes immutable Vulkan instance requirements", requirements_published);
    ensure("the window accepts only its current nonzero native generation", generation_current);
    ensure("the requirements retain SDL's exact resolver", resolver_identity);
    ensure("the requirements deep-copy SDL's exact extension sequence", extensions_identical);
    ensure("the SDL resolver satisfies the Stage 30 global-dispatch contract", global_dispatch_resolved);
    ensure("a live Vulkan window rejects an OpenGL window before construction", mixed_opengl_rejected);
    ensure("Vulkan native-window recreation fails closed", vulkan_context_switch_fails);
    ensure("Vulkan shared OpenGL contexts fail closed", vulkan_shared_context_fails);
    ensure("the real SDL Vulkan window is destroyed", destroyed);
    ensure_equals("native teardown restores the LLWindow instance count", LLWindow::instanceCount(), initial_instance_count);
    ensure("native teardown removes the manager entry", !LLWindowManager::isWindowValid(window));
    ensure("native teardown restores SDL's log callback",
           final_sdl_state.mLogOutput == initial_sdl_state.mLogOutput &&
               final_sdl_state.mLogUserdata == initial_sdl_state.mLogUserdata);
    ensure_equals("native teardown restores SDL subsystem state", final_sdl_state.mInitialized, initial_sdl_state.mInitialized);
    ensure("native teardown leaves no current OpenGL context", final_sdl_state.mGLContext == initial_sdl_state.mGLContext);
    ensure_equals("native teardown leaves the OpenGL manager unchanged", gGLManager.mInited, initial_gl_manager);
}

} // namespace tut
