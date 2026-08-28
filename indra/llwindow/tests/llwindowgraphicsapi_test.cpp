/**
 * @file llwindowgraphicsapi_test.cpp
 * @brief Tests for typed window graphics API selection.
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

#include "llwindow.h"
#include "lltut.h"

#include <type_traits>

#if LL_SDL_WINDOW
#include "SDL3/SDL.h"
#endif

static_assert(std::is_enum_v<LLWindow::GraphicsAPI>);
static_assert(!std::is_convertible_v<bool, LLWindow::GraphicsAPI>);
static_assert(!std::is_convertible_v<LLWindow::GraphicsAPI, int>);
static_assert(!std::is_convertible_v<LLWindow::GraphicsAPI, std::underlying_type_t<LLWindow::GraphicsAPI>>);
static_assert(LLWindow::GraphicsAPI::OpenGL != LLWindow::GraphicsAPI::Vulkan);
static_assert(LLWindow::GraphicsAPI::OpenGL != LLWindow::GraphicsAPI::Headless);
static_assert(LLWindow::GraphicsAPI::Vulkan != LLWindow::GraphicsAPI::Headless);

namespace
{

#if LL_SDL_WINDOW
struct SDLState
{
    SDL_LogOutputFunction mLogOutput   = nullptr;
    void*                 mLogUserdata = nullptr;
    SDL_InitFlags         mInitialized = 0;
};

SDLState currentSDLState()
{
    SDLState state;
    SDL_GetLogOutputFunction(&state.mLogOutput, &state.mLogUserdata);
    state.mInitialized = SDL_WasInit(0);
    return state;
}
#endif

} // namespace

namespace tut
{

struct window_graphics_api_test
{
};

using window_graphics_api_test_group  = test_group<window_graphics_api_test>;
using window_graphics_api_test_object = window_graphics_api_test_group::object;
window_graphics_api_test_group window_graphics_api_tests("window graphics API");

template<>
template<>
void window_graphics_api_test_object::test<1>()
{
    const std::size_t initial_instance_count = LLWindow::instanceCount();
#if LL_SDL_WINDOW
    const SDLState initial_sdl_state = currentSDLState();
#endif
    LLWindow* window = LLWindowManager::createWindow(nullptr, "unsupported Vulkan window", "llwindowgraphicsapi", 0, 0, 1, 1,
                                                     LLWindow::GraphicsAPI::Vulkan);

    const bool        returned_null         = window == nullptr;
    const bool        tracked_window        = LLWindowManager::isWindowValid(window);
    const std::size_t result_instance_count = LLWindow::instanceCount();
#if LL_SDL_WINDOW
    const SDLState result_sdl_state = currentSDLState();
#endif

    if (tracked_window)
    {
        LLWindowManager::destroyWindow(window);
    }

    ensure("Vulkan window creation fails closed", returned_null);
    ensure_equals("Vulkan rejection constructs no surviving LLWindow", result_instance_count, initial_instance_count);
    ensure("Vulkan rejection tracks no window", !tracked_window);
#if LL_SDL_WINDOW
    ensure("Vulkan rejection does not change SDL's log callback",
           result_sdl_state.mLogOutput == initial_sdl_state.mLogOutput && result_sdl_state.mLogUserdata == initial_sdl_state.mLogUserdata);
    ensure_equals("Vulkan rejection does not initialize an SDL subsystem", result_sdl_state.mInitialized, initial_sdl_state.mInitialized);
#endif
}

template<>
template<>
void window_graphics_api_test_object::test<2>()
{
    const std::size_t initial_instance_count = LLWindow::instanceCount();
#if LL_SDL_WINDOW
    const SDLState initial_sdl_state = currentSDLState();
#endif
    LLWindow* window = LLWindowManager::createWindow(nullptr, "invalid graphics API window", "llwindowgraphicsapi", 0, 0, 1, 1,
                                                     static_cast<LLWindow::GraphicsAPI>(255));

    const bool        returned_null         = window == nullptr;
    const bool        tracked_window        = LLWindowManager::isWindowValid(window);
    const std::size_t result_instance_count = LLWindow::instanceCount();
#if LL_SDL_WINDOW
    const SDLState result_sdl_state = currentSDLState();
#endif

    if (tracked_window)
    {
        LLWindowManager::destroyWindow(window);
    }

    ensure("unknown graphics API selection fails closed", returned_null);
    ensure_equals("unknown selection constructs no surviving LLWindow", result_instance_count, initial_instance_count);
    ensure("unknown selection tracks no window", !tracked_window);
#if LL_SDL_WINDOW
    ensure("unknown selection does not change SDL's log callback",
           result_sdl_state.mLogOutput == initial_sdl_state.mLogOutput && result_sdl_state.mLogUserdata == initial_sdl_state.mLogUserdata);
    ensure_equals("unknown selection does not initialize an SDL subsystem", result_sdl_state.mInitialized, initial_sdl_state.mInitialized);
#endif
}

} // namespace tut
