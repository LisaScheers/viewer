/**
 * @file llwindowsdlvulkan_test.cpp
 * @brief Tests for SDL Vulkan window and loader lifetime ownership.
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

#include "llwindowsdlvulkan.h"
#include "lltut.h"

#include <array>
#include <cstdint>
#include <initializer_list>
#include <type_traits>
#include <utility>

namespace
{

enum class Event
{
    Load,
    Create,
    Flags,
    Resolver,
    Extensions,
    Destroy,
    Unload
};

enum class Failure
{
    None,
    Load,
    Window,
    Resolver,
    Extensions
};

void fakeResolver()
{
}

struct FakeState
{
    std::array<Event, 16>              mEvents{};
    std::size_t                        mEventCount               = 0;
    Failure                            mFailure                  = Failure::None;
    int                                mExplicitLoaderReferences = 0;
    int                                mWindowLoaderReferences   = 0;
    SDL_WindowFlags                    mWindowFlags              = SDL_WINDOW_VULKAN;
    SDL_Window*                        mWindow                   = reinterpret_cast<SDL_Window*>(static_cast<std::uintptr_t>(0x12340));
    const LLWindowSDLVulkanCreateInfo* mCreateInfo               = nullptr;
    const char*                        mExtensionNames[2]        = { "VK_KHR_surface", "VK_KHR_xlib_surface" };
    std::size_t                        mExtensionCount           = 2;
    const LLWindowSDLVulkan*           mOwnerDuringDestroy       = nullptr;
    bool                               mRequirementsInvalidatedBeforeDestroy = false;

    void record(Event event) noexcept { mEvents[mEventCount++] = event; }
};

bool loadLibrary(void* userdata) noexcept
{
    auto& state = *static_cast<FakeState*>(userdata);
    state.record(Event::Load);
    if (state.mFailure == Failure::Load)
    {
        return false;
    }
    ++state.mExplicitLoaderReferences;
    return true;
}

void unloadLibrary(void* userdata) noexcept
{
    auto& state = *static_cast<FakeState*>(userdata);
    state.record(Event::Unload);
    --state.mExplicitLoaderReferences;
}

SDL_Window* createWindow(void* userdata, const LLWindowSDLVulkanCreateInfo& info) noexcept
{
    auto& state = *static_cast<FakeState*>(userdata);
    state.record(Event::Create);
    state.mCreateInfo = &info;
    if (state.mFailure == Failure::Window)
    {
        return nullptr;
    }
    ++state.mWindowLoaderReferences;
    return state.mWindow;
}

void destroyWindow(void* userdata, SDL_Window* window) noexcept
{
    auto& state = *static_cast<FakeState*>(userdata);
    state.record(Event::Destroy);
    if (state.mOwnerDuringDestroy)
    {
        state.mRequirementsInvalidatedBeforeDestroy = !state.mOwnerDuringDestroy->hasRequirements();
    }
    if (window == state.mWindow)
    {
        --state.mWindowLoaderReferences;
    }
}

SDL_WindowFlags getWindowFlags(void* userdata, SDL_Window*) noexcept
{
    auto& state = *static_cast<FakeState*>(userdata);
    state.record(Event::Flags);
    return state.mWindowFlags;
}

LLWindowVulkanFunction getResolver(void* userdata) noexcept
{
    auto& state = *static_cast<FakeState*>(userdata);
    state.record(Event::Resolver);
    return state.mFailure == Failure::Resolver ? nullptr : fakeResolver;
}

const char* const* getInstanceExtensions(void* userdata, std::size_t* count) noexcept
{
    auto& state = *static_cast<FakeState*>(userdata);
    state.record(Event::Extensions);
    if (state.mFailure == Failure::Extensions)
    {
        return nullptr;
    }
    *count = state.mExtensionCount;
    return state.mExtensionNames;
}

LLWindowSDLVulkanOperations fakeOperations(FakeState& state) noexcept
{
    return { &state, loadLibrary, unloadLibrary, createWindow, destroyWindow, getWindowFlags, getResolver, getInstanceExtensions };
}

LLWindowSDLVulkanCreateInfo createInfo()
{
    return { "Vulkan test window", 13, 17, 1280, 720, false, true, false, true };
}

void ensureEvents(const char* message, const FakeState& state, std::initializer_list<Event> expected)
{
    bool equal = state.mEventCount == expected.size();
    if (equal)
    {
        std::size_t index = 0;
        for (Event event : expected)
        {
            equal = equal && state.mEvents[index++] == event;
        }
    }
    tut::ensure(message, equal);
}

const LLWindowSDLVulkanAcquireError* acquireError(const LLWindowSDLVulkanAcquireResult& result) noexcept
{
    return std::get_if<LLWindowSDLVulkanAcquireError>(&result);
}

LLWindowSDLVulkan* acquiredWindow(LLWindowSDLVulkanAcquireResult& result) noexcept
{
    return std::get_if<LLWindowSDLVulkan>(&result);
}

void ensureAcquireError(const char* message, const LLWindowSDLVulkanAcquireResult& result, LLWindowSDLVulkanAcquireCode code)
{
    const auto* error = acquireError(result);
    tut::ensure(message, error && error->mCode == code);
}

} // namespace

namespace tut
{

struct window_sdl_vulkan_test
{
};

using window_sdl_vulkan_group  = test_group<window_sdl_vulkan_test>;
using window_sdl_vulkan_object = window_sdl_vulkan_group::object;
window_sdl_vulkan_group window_sdl_vulkan_tests("window SDL Vulkan ownership");

template<>
template<>
void window_sdl_vulkan_object::test<1>()
{
    static_assert(!std::is_copy_constructible_v<LLWindowSDLVulkan>);
    static_assert(!std::is_copy_assignable_v<LLWindowSDLVulkan>);
    static_assert(std::is_nothrow_move_constructible_v<LLWindowSDLVulkan>);
    static_assert(std::is_nothrow_move_assignable_v<LLWindowSDLVulkan>);
    static_assert(std::is_nothrow_destructible_v<LLWindowSDLVulkan>);
    static_assert(std::is_same_v<decltype(std::declval<const LLWindowSDLVulkan&>().requirements()),
                                 const LLWindowVulkanRequirements*>);
    static_assert(noexcept(acquireLLWindowSDLVulkan(std::declval<const LLWindowSDLVulkanCreateInfo&>(), U64{},
                                                    std::declval<const LLWindowSDLVulkanOperations&>())));

    ensure("the production operation table is complete",
           defaultLLWindowSDLVulkanOperations().mLoadLibrary && defaultLLWindowSDLVulkanOperations().mUnloadLibrary &&
               defaultLLWindowSDLVulkanOperations().mCreateWindow && defaultLLWindowSDLVulkanOperations().mDestroyWindow &&
               defaultLLWindowSDLVulkanOperations().mGetWindowFlags && defaultLLWindowSDLVulkanOperations().mGetResolver &&
               defaultLLWindowSDLVulkanOperations().mGetInstanceExtensions);
}

template<>
template<>
void window_sdl_vulkan_object::test<2>()
{
    FakeState state;
    auto      info   = createInfo();
    auto      result = acquireLLWindowSDLVulkan(info, 41, fakeOperations(state));
    auto*     owner  = acquiredWindow(result);

    ensure("acquisition succeeds", owner != nullptr);
    ensureEvents("acquisition follows the SDL loader and query order", state,
                 { Event::Load, Event::Create, Event::Flags, Event::Resolver, Event::Extensions });
    ensure_equals("the explicit loader reference is held", state.mExplicitLoaderReferences, 1);
    ensure_equals("the Vulkan window loader reference is held", state.mWindowLoaderReferences, 1);
    ensure("the fake receives the exact create description", state.mCreateInfo == &info);
    ensure("the create description preserves title, placement, size, and flags",
           state.mCreateInfo->mTitle == "Vulkan test window" && state.mCreateInfo->mX == 13 && state.mCreateInfo->mY == 17 &&
               state.mCreateInfo->mWidth == 1280 && state.mCreateInfo->mHeight == 720 && !state.mCreateInfo->mResizable &&
               state.mCreateInfo->mFullscreen && !state.mCreateInfo->mHidden && state.mCreateInfo->mHighPixelDensity);
    ensure("the created window is verified as Vulkan-only",
           (state.mWindowFlags & SDL_WINDOW_VULKAN) != 0 && (state.mWindowFlags & SDL_WINDOW_OPENGL) == 0);
    ensure("requirements are published after every native query", owner->hasRequirements());
    ensure("the resolver identity is retained", owner->requirements() && owner->requirements()->resolver() == fakeResolver);
    ensure("the generation is current", owner->isGenerationCurrent(41));
    ensure("zero and another generation are stale", !owner->isGenerationCurrent(0) && !owner->isGenerationCurrent(42));

    state.mExtensionNames[0] = "changed_after_acquire";
    state.mExtensionNames[1] = "changed_too";
    const auto& extensions = owner->requirements()->requiredInstanceExtensions();
    ensure("SDL extension storage is deep-copied in order",
           extensions.size() == 2 && extensions[0] == "VK_KHR_surface" && extensions[1] == "VK_KHR_xlib_surface");

    state.mOwnerDuringDestroy = owner;
    owner->reset();
    ensureEvents("reset destroys the window before releasing the explicit loader reference", state,
                 { Event::Load, Event::Create, Event::Flags, Event::Resolver, Event::Extensions, Event::Destroy, Event::Unload });
    ensure("requirements are invalid before SDL destroys the window", state.mRequirementsInvalidatedBeforeDestroy);
    ensure("reset exposes no requirements pointer", owner->requirements() == nullptr);
    ensure_equals("reset releases the explicit loader reference", state.mExplicitLoaderReferences, 0);
    ensure_equals("reset releases the window loader reference", state.mWindowLoaderReferences, 0);

    owner->reset();
    ensure_equals("a second reset performs no SDL operation", state.mEventCount, std::size_t{ 7 });
}

template<>
template<>
void window_sdl_vulkan_object::test<3>()
{
    const auto info = createInfo();

    LLWindowSDLVulkanOperations invalid_operations;
    auto                        invalid = acquireLLWindowSDLVulkan(info, 1, invalid_operations);
    ensureAcquireError("an incomplete operation table is rejected", invalid, LLWindowSDLVulkanAcquireCode::InvalidOperations);

    FakeState load_state;
    load_state.mFailure = Failure::Load;
    auto load           = acquireLLWindowSDLVulkan(info, 1, fakeOperations(load_state));
    ensureAcquireError("loader failure is typed", load, LLWindowSDLVulkanAcquireCode::LoaderFailure);
    ensureEvents("loader failure makes no later call", load_state, { Event::Load });

    FakeState window_state;
    window_state.mFailure = Failure::Window;
    auto window           = acquireLLWindowSDLVulkan(info, 1, fakeOperations(window_state));
    ensureAcquireError("window failure is typed", window, LLWindowSDLVulkanAcquireCode::WindowFailure);
    ensureEvents("window failure only releases the explicit reference", window_state, { Event::Load, Event::Create, Event::Unload });

    FakeState resolver_state;
    resolver_state.mFailure = Failure::Resolver;
    auto resolver           = acquireLLWindowSDLVulkan(info, 1, fakeOperations(resolver_state));
    ensureAcquireError("resolver failure is typed", resolver, LLWindowSDLVulkanAcquireCode::ResolverFailure);
    ensureEvents("resolver failure stops before extension query and rolls back in order", resolver_state,
                 { Event::Load, Event::Create, Event::Flags, Event::Resolver, Event::Destroy, Event::Unload });

    FakeState extension_state;
    extension_state.mFailure = Failure::Extensions;
    auto extension           = acquireLLWindowSDLVulkan(info, 1, fakeOperations(extension_state));
    ensureAcquireError("extension query failure is typed", extension, LLWindowSDLVulkanAcquireCode::ExtensionQueryFailure);
    ensureEvents("extension query failure rolls back in order", extension_state,
                 { Event::Load, Event::Create, Event::Flags, Event::Resolver, Event::Extensions, Event::Destroy, Event::Unload });

    FakeState requirements_state;
    requirements_state.mExtensionNames[1] = requirements_state.mExtensionNames[0];
    auto requirements                     = acquireLLWindowSDLVulkan(info, 1, fakeOperations(requirements_state));
    ensureAcquireError("requirements failure is typed", requirements, LLWindowSDLVulkanAcquireCode::RequirementsFailure);
    const auto* requirements_error = acquireError(requirements);
    ensure("the exact requirements error is retained",
           requirements_error && requirements_error->mRequirementsError &&
               requirements_error->mRequirementsError->mCode == LLWindowVulkanRequirementsBuildCode::DuplicateExtensionName &&
               requirements_error->mRequirementsError->mIndex == 1);
    ensureEvents("requirements failure rolls back in order", requirements_state,
                 { Event::Load, Event::Create, Event::Flags, Event::Resolver, Event::Extensions, Event::Destroy, Event::Unload });
}

template<>
template<>
void window_sdl_vulkan_object::test<4>()
{
    const auto info = createInfo();

    FakeState missing_vulkan_state;
    missing_vulkan_state.mWindowFlags = 0;
    auto missing_vulkan               = acquireLLWindowSDLVulkan(info, 1, fakeOperations(missing_vulkan_state));
    ensureAcquireError("a window without the Vulkan flag is rejected", missing_vulkan, LLWindowSDLVulkanAcquireCode::WindowFlagsFailure);
    ensureEvents("a missing Vulkan flag stops before resolver lookup", missing_vulkan_state,
                 { Event::Load, Event::Create, Event::Flags, Event::Destroy, Event::Unload });

    FakeState opengl_state;
    opengl_state.mWindowFlags = SDL_WINDOW_VULKAN | SDL_WINDOW_OPENGL;
    auto opengl               = acquireLLWindowSDLVulkan(info, 1, fakeOperations(opengl_state));
    ensureAcquireError("a Vulkan and OpenGL window is rejected", opengl, LLWindowSDLVulkanAcquireCode::WindowFlagsFailure);
    ensureEvents("an OpenGL-marked window stops before resolver lookup", opengl_state,
                 { Event::Load, Event::Create, Event::Flags, Event::Destroy, Event::Unload });
}

template<>
template<>
void window_sdl_vulkan_object::test<5>()
{
    FakeState  state;
    const auto info   = createInfo();
    auto       result = acquireLLWindowSDLVulkan(info, 9, fakeOperations(state));
    auto*      owner  = acquiredWindow(result);
    ensure("move fixture acquired a window", owner != nullptr);

    LLWindowSDLVulkan moved(std::move(*owner));
    ensure("move construction transfers requirements", moved.hasRequirements() && moved.isGenerationCurrent(9));
    ensure("the moved-from owner publishes no requirements", !owner->hasRequirements());
    ensure_equals("move construction performs no SDL cleanup", state.mEventCount, std::size_t{ 5 });

    state.mOwnerDuringDestroy = &moved;
    moved.reset();
    ensure_equals("the moved owner releases each reference once", state.mExplicitLoaderReferences, 0);
    ensure_equals("the moved owner releases the window reference once", state.mWindowLoaderReferences, 0);

    state.mOwnerDuringDestroy = nullptr;
    state.mEventCount         = 0;
    auto  reused              = acquireLLWindowSDLVulkan(info, 10, fakeOperations(state));
    auto* reused_owner        = acquiredWindow(reused);
    ensure("the fake may reuse the same native address", reused_owner != nullptr);
    ensure("the new generation is current at a reused address",
           reused_owner->isGenerationCurrent(10) && !reused_owner->isGenerationCurrent(9));
}

template<>
template<>
void window_sdl_vulkan_object::test<6>()
{
    const auto info = createInfo();

    FakeState source_state;
    auto      source_result = acquireLLWindowSDLVulkan(info, 21, fakeOperations(source_state));
    auto*     source        = acquiredWindow(source_result);
    ensure("move-assignment source acquired a window", source != nullptr);

    FakeState destination_state;
    destination_state.mWindow = reinterpret_cast<SDL_Window*>(static_cast<std::uintptr_t>(0x56780));
    auto destination_result = acquireLLWindowSDLVulkan(info, 22, fakeOperations(destination_state));
    auto* destination        = acquiredWindow(destination_result);
    ensure("move-assignment destination acquired a window", destination != nullptr);

    destination_state.mOwnerDuringDestroy = destination;
    *destination = std::move(*source);

    ensure("move assignment invalidates the source", !source->hasRequirements());
    ensure("move assignment transfers the source generation",
           destination->hasRequirements() && destination->isGenerationCurrent(21) && !destination->isGenerationCurrent(22));
    ensure("move assignment invalidates destination requirements before replacement cleanup",
           destination_state.mRequirementsInvalidatedBeforeDestroy);
    ensure_equals("move assignment releases the replaced explicit reference", destination_state.mExplicitLoaderReferences, 0);
    ensure_equals("move assignment releases the replaced window reference", destination_state.mWindowLoaderReferences, 0);
    ensureEvents("move assignment destroys the replaced window before unloading it", destination_state,
                 { Event::Load, Event::Create, Event::Flags, Event::Resolver, Event::Extensions, Event::Destroy, Event::Unload });

    source_state.mOwnerDuringDestroy = destination;
    destination->reset();
    ensure("transferred requirements are invalid before source-window destruction", source_state.mRequirementsInvalidatedBeforeDestroy);
    ensure_equals("the transferred explicit reference is released once", source_state.mExplicitLoaderReferences, 0);
    ensure_equals("the transferred window reference is released once", source_state.mWindowLoaderReferences, 0);
}

} // namespace tut
