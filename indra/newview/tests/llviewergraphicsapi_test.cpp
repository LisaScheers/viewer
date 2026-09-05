/**
 * @file llviewergraphicsapi_test.cpp
 * @brief Tests for viewer graphics API selection.
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

#include "linden_common.h"
#include "../test/lltut.h"

#include "../llviewergraphicsapi.h"

#include <variant>

namespace tut
{
struct viewer_graphics_api_test
{
};

using viewer_graphics_api_test_group  = test_group<viewer_graphics_api_test>;
using viewer_graphics_api_test_object = viewer_graphics_api_test_group::object;
viewer_graphics_api_test_group viewer_graphics_api_tests("viewer graphics API");

template<>
template<>
void viewer_graphics_api_test_object::test<1>()
{
    const auto  selection = selectViewerGraphicsAPI(false, false, false);
    const auto* api       = std::get_if<LLWindow::GraphicsAPI>(&selection);

    ensure("the default selection succeeds", api != nullptr);
    ensure("OpenGL remains the default", api && *api == LLWindow::GraphicsAPI::OpenGL);
}

template<>
template<>
void viewer_graphics_api_test_object::test<2>()
{
    const auto  selection = selectViewerGraphicsAPI(true, false, false);
    const auto* api       = std::get_if<LLWindow::GraphicsAPI>(&selection);

    ensure("headless selection succeeds", api != nullptr);
    ensure("headless mode selects the headless API", api && *api == LLWindow::GraphicsAPI::Headless);
}

template<>
template<>
void viewer_graphics_api_test_object::test<3>()
{
    const auto  selection = selectViewerGraphicsAPI(false, true, true);
    const auto* api       = std::get_if<LLWindow::GraphicsAPI>(&selection);

    ensure("available Vulkan selection succeeds", api != nullptr);
    ensure("an available Vulkan request selects Vulkan", api && *api == LLWindow::GraphicsAPI::Vulkan);
}

template<>
template<>
void viewer_graphics_api_test_object::test<4>()
{
    const auto  selection = selectViewerGraphicsAPI(false, true, false);
    const auto* error     = std::get_if<LLViewerGraphicsAPISelectionError>(&selection);

    ensure("unavailable Vulkan selection fails", error != nullptr);
    ensure("unavailable Vulkan is reported explicitly", error && *error == LLViewerGraphicsAPISelectionError::UnsupportedVulkan);
}

template<>
template<>
void viewer_graphics_api_test_object::test<5>()
{
    const auto  available_conflict   = selectViewerGraphicsAPI(true, true, true);
    const auto  unavailable_conflict = selectViewerGraphicsAPI(true, true, false);
    const auto* available_error      = std::get_if<LLViewerGraphicsAPISelectionError>(&available_conflict);
    const auto* unavailable_error    = std::get_if<LLViewerGraphicsAPISelectionError>(&unavailable_conflict);

    ensure("headless and Vulkan conflict when Vulkan is available",
           available_error && *available_error == LLViewerGraphicsAPISelectionError::HeadlessVulkanConflict);
    ensure("headless and Vulkan conflict before availability is considered",
           unavailable_error && *unavailable_error == LLViewerGraphicsAPISelectionError::HeadlessVulkanConflict);
}
} // namespace tut
