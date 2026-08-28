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

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <new>
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
    CreateInstance,
    CreateSurface,
    DestroySurface,
    DestroyInstance,
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

struct FakeState
{
    std::array<Event, 32>              mEvents{};
    std::size_t                        mEventCount                 = 0;
    Failure                            mFailure                    = Failure::None;
    int                                mExplicitLoaderReferences   = 0;
    int                                mWindowLoaderReferences     = 0;
    SDL_WindowFlags                    mWindowFlags                = SDL_WINDOW_VULKAN;
    SDL_Window*                        mWindow                     = reinterpret_cast<SDL_Window*>(static_cast<std::uintptr_t>(0x12340));
    const LLWindowSDLVulkanCreateInfo* mCreateInfo                 = nullptr;
    const char*                        mExtensionNames[2]          = { "VK_KHR_surface", "VK_KHR_xlib_surface" };
    std::size_t                        mExtensionCount             = 2;
    const LLWindowSDLVulkan*           mOwnerDuringDestroy         = nullptr;
    const LLWindowSDLVulkan*           mOwnerDuringInstanceDestroy = nullptr;
    bool                               mRequirementsInvalidatedBeforeDestroy  = false;
    bool                               mRequirementsLiveDuringInstanceDestroy = false;
    bool                               mLoaderLiveDuringInstanceDestroy       = false;
    bool                               mSurfaceAbsentDuringInstanceDestroy    = false;
    bool                               mFailInstanceCreation                  = false;
    bool                               mFailSurfaceCreation                   = false;
    bool                               mNullSurfaceOnSuccess                  = false;
    bool                               mPoisonSurfaceOnFailure                = false;
    bool                               mExposeDestroySurface                  = true;
    std::size_t                        mDestroyInstanceCount                  = 0;
    std::size_t                        mCreateSurfaceCount                    = 0;
    std::size_t                        mDestroySurfaceCount                   = 0;
    std::size_t                        mLiveSurfaceCount                      = 0;
    SDL_Window*                        mSurfaceWindow                         = nullptr;
    VkInstance                         mSurfaceInstance                       = VK_NULL_HANDLE;
    const VkAllocationCallbacks*       mSurfaceAllocator                      = reinterpret_cast<const VkAllocationCallbacks*>(1);
    const LLWindowSDLVulkan*           mOwnerDuringSurfaceDestroy             = nullptr;
    bool                               mRequirementsLiveDuringSurfaceDestroy  = false;
    bool                               mInstanceLiveDuringSurfaceDestroy      = false;
    bool                               mLoaderLiveDuringSurfaceDestroy        = false;

    void record(Event event) noexcept { mEvents[mEventCount++] = event; }
};

FakeState* gVulkanState = nullptr;

class ScopedVulkanState
{
public:
    explicit ScopedVulkanState(FakeState& state) noexcept { gVulkanState = &state; }
    ~ScopedVulkanState() noexcept { gVulkanState = nullptr; }

    ScopedVulkanState(const ScopedVulkanState&)            = delete;
    ScopedVulkanState& operator=(const ScopedVulkanState&) = delete;

    void use(FakeState& state) noexcept { gVulkanState = &state; }
};

template<typename Function>
PFN_vkVoidFunction eraseFunctionType(Function function) noexcept
{
    return reinterpret_cast<PFN_vkVoidFunction>(function);
}

VkInstance fakeInstance() noexcept
{
    return reinterpret_cast<VkInstance>(static_cast<std::uintptr_t>(0x98760));
}

VkSurfaceKHR fakeSurface() noexcept
{
    return reinterpret_cast<VkSurfaceKHR>(static_cast<std::uintptr_t>(0x76540));
}

VKAPI_ATTR VkResult VKAPI_CALL fakeEnumerateInstanceVersion(std::uint32_t* version) noexcept
{
    if (!version)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    *version = VK_API_VERSION_1_1;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeEnumerateInstanceExtensionProperties(const char*, std::uint32_t* count,
                                                                        VkExtensionProperties* properties) noexcept
{
    if (!count)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    constexpr std::array<const char*, 2> extensions{ "VK_KHR_surface", "VK_KHR_xlib_surface" };
    if (!properties)
    {
        *count = static_cast<std::uint32_t>(extensions.size());
        return VK_SUCCESS;
    }

    const std::size_t copied = std::min<std::size_t>(*count, extensions.size());
    for (std::size_t index = 0; index < copied; ++index)
    {
        std::memcpy(properties[index].extensionName, extensions[index], std::strlen(extensions[index]) + 1);
    }
    *count = static_cast<std::uint32_t>(copied);
    return copied == extensions.size() ? VK_SUCCESS : VK_INCOMPLETE;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeEnumerateInstanceLayerProperties(std::uint32_t* count, VkLayerProperties*) noexcept
{
    if (!count)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    *count = 0;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateInstance(const VkInstanceCreateInfo*, const VkAllocationCallbacks*, VkInstance* instance) noexcept
{
    if (!gVulkanState || !instance)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    gVulkanState->record(Event::CreateInstance);
    if (gVulkanState->mFailInstanceCreation)
    {
        *instance = VK_NULL_HANDLE;
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    *instance = fakeInstance();
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyInstance(VkInstance instance, const VkAllocationCallbacks*) noexcept
{
    if (!gVulkanState || instance != fakeInstance())
    {
        return;
    }

    gVulkanState->record(Event::DestroyInstance);
    ++gVulkanState->mDestroyInstanceCount;
    if (gVulkanState->mOwnerDuringInstanceDestroy)
    {
        gVulkanState->mRequirementsLiveDuringInstanceDestroy = gVulkanState->mOwnerDuringInstanceDestroy->hasRequirements();
        gVulkanState->mSurfaceAbsentDuringInstanceDestroy    = gVulkanState->mLiveSurfaceCount == 0;
    }
    gVulkanState->mLoaderLiveDuringInstanceDestroy =
        gVulkanState->mExplicitLoaderReferences == 1 && gVulkanState->mWindowLoaderReferences == 1;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroySurface(VkInstance instance, VkSurfaceKHR surface, const VkAllocationCallbacks* allocator) noexcept
{
    if (!gVulkanState || instance != fakeInstance() || surface != fakeSurface() || allocator)
    {
        return;
    }

    gVulkanState->record(Event::DestroySurface);
    ++gVulkanState->mDestroySurfaceCount;
    if (gVulkanState->mLiveSurfaceCount != 0)
    {
        --gVulkanState->mLiveSurfaceCount;
    }
    if (gVulkanState->mOwnerDuringSurfaceDestroy)
    {
        gVulkanState->mRequirementsLiveDuringSurfaceDestroy = gVulkanState->mOwnerDuringSurfaceDestroy->hasRequirements();
        gVulkanState->mInstanceLiveDuringSurfaceDestroy     = instance == fakeInstance();
    }
    gVulkanState->mLoaderLiveDuringSurfaceDestroy =
        gVulkanState->mExplicitLoaderReferences == 1 && gVulkanState->mWindowLoaderReferences == 1;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL fakeGetInstanceProcAddr(VkInstance instance, const char* name) noexcept
{
    if (!name)
    {
        return nullptr;
    }
    if (std::strcmp(name, "vkCreateInstance") == 0)
    {
        return eraseFunctionType(fakeCreateInstance);
    }
    if (std::strcmp(name, "vkEnumerateInstanceExtensionProperties") == 0)
    {
        return eraseFunctionType(fakeEnumerateInstanceExtensionProperties);
    }
    if (std::strcmp(name, "vkEnumerateInstanceLayerProperties") == 0)
    {
        return eraseFunctionType(fakeEnumerateInstanceLayerProperties);
    }
    if (std::strcmp(name, "vkEnumerateInstanceVersion") == 0)
    {
        return eraseFunctionType(fakeEnumerateInstanceVersion);
    }
    if (instance == fakeInstance() && std::strcmp(name, "vkDestroyInstance") == 0)
    {
        return eraseFunctionType(fakeDestroyInstance);
    }
    if (instance == fakeInstance() && std::strcmp(name, "vkDestroySurfaceKHR") == 0)
    {
        return gVulkanState && gVulkanState->mExposeDestroySurface ? eraseFunctionType(fakeDestroySurface) : nullptr;
    }
    return nullptr;
}

LLWindowVulkanFunction fakeResolver() noexcept
{
    return reinterpret_cast<LLWindowVulkanFunction>(fakeGetInstanceProcAddr);
}

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
    return state.mFailure == Failure::Resolver ? nullptr : fakeResolver();
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

bool createSurface(void*                        userdata,
                   SDL_Window*                  window,
                   VkInstance                   instance,
                   const VkAllocationCallbacks* allocator,
                   VkSurfaceKHR*                surface) noexcept
{
    auto& state = *static_cast<FakeState*>(userdata);
    state.record(Event::CreateSurface);
    ++state.mCreateSurfaceCount;
    state.mSurfaceWindow    = window;
    state.mSurfaceInstance  = instance;
    state.mSurfaceAllocator = allocator;

    if (!surface)
    {
        return false;
    }
    if (state.mFailSurfaceCreation)
    {
        *surface = state.mPoisonSurfaceOnFailure ? fakeSurface() : VK_NULL_HANDLE;
        return false;
    }
    *surface = state.mNullSurfaceOnSuccess ? VK_NULL_HANDLE : fakeSurface();
    if (*surface != VK_NULL_HANDLE)
    {
        ++state.mLiveSurfaceCount;
    }
    return true;
}

LLWindowSDLVulkanOperations fakeOperations(FakeState& state) noexcept
{
    return { &state,         loadLibrary, unloadLibrary,         createWindow, destroyWindow,
             getWindowFlags, getResolver, getInstanceExtensions, createSurface };
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

void ensureSurfaceError(const char*                                       message,
                        const LLRenderVulkan::VulkanSurfaceAcquireResult& result,
                        LLRenderVulkan::VulkanSurfaceAcquireCode          code)
{
    tut::ensure(message, result && result->mCode == code);
}

void failAllocation()
{
    throw std::bad_alloc();
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
    static_assert(std::is_same_v<decltype(std::declval<const LLWindowSDLVulkan&>().requirements()), const LLWindowVulkanRequirements*>);
    static_assert(noexcept(acquireLLWindowSDLVulkan(std::declval<const LLWindowSDLVulkanCreateInfo&>(), U64{},
                                                    std::declval<const LLWindowSDLVulkanOperations&>())));

    ensure("the production operation table is complete",
           defaultLLWindowSDLVulkanOperations().mLoadLibrary && defaultLLWindowSDLVulkanOperations().mUnloadLibrary &&
               defaultLLWindowSDLVulkanOperations().mCreateWindow && defaultLLWindowSDLVulkanOperations().mDestroyWindow &&
               defaultLLWindowSDLVulkanOperations().mGetWindowFlags && defaultLLWindowSDLVulkanOperations().mGetResolver &&
               defaultLLWindowSDLVulkanOperations().mGetInstanceExtensions && defaultLLWindowSDLVulkanOperations().mCreateSurface);
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
    ensure("the resolver identity is retained", owner->requirements() && owner->requirements()->resolver() == fakeResolver());
    ensure("the generation is current", owner->isGenerationCurrent(41));
    ensure("zero and another generation are stale", !owner->isGenerationCurrent(0) && !owner->isGenerationCurrent(42));

    state.mExtensionNames[0] = "changed_after_acquire";
    state.mExtensionNames[1] = "changed_too";
    const auto& extensions   = owner->requirements()->requiredInstanceExtensions();
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

    FakeState surface_operation_state;
    auto      missing_surface_operation      = fakeOperations(surface_operation_state);
    missing_surface_operation.mCreateSurface = nullptr;
    auto missing_surface                     = acquireLLWindowSDLVulkan(info, 1, missing_surface_operation);
    ensureAcquireError("a missing SDL surface operation is rejected", missing_surface, LLWindowSDLVulkanAcquireCode::InvalidOperations);
    ensure_equals("a missing SDL surface operation is rejected before loading Vulkan", surface_operation_state.mEventCount,
                  std::size_t{ 0 });

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
    auto  destination_result  = acquireLLWindowSDLVulkan(info, 22, fakeOperations(destination_state));
    auto* destination         = acquiredWindow(destination_result);
    ensure("move-assignment destination acquired a window", destination != nullptr);

    destination_state.mOwnerDuringDestroy = destination;
    *destination                          = std::move(*source);

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

template<>
template<>
void window_sdl_vulkan_object::test<7>()
{
    using namespace LLRenderVulkan;

    FakeState         state;
    ScopedVulkanState vulkan_state(state);
    const auto        info   = createInfo();
    auto              result = acquireLLWindowSDLVulkan(info, 31, fakeOperations(state));
    auto*             owner  = acquiredWindow(result);
    ensure("instance fixture acquired a Vulkan window", owner != nullptr);

    const auto acquire_error =
        owner->acquireInstanceGeneration(VulkanInstanceValidationMode::Disabled, VulkanInstancePortabilityMode::Disabled);
    ensure("a validation-disabled fake Vulkan 1.1 instance is acquired", !acquire_error);
    ensure("the instance generation is owned by the SDL window",
           owner->instanceGeneration() && owner->instanceGeneration()->instance() == fakeInstance() &&
               owner->instanceGeneration()->apiVersion() == VK_API_VERSION_1_1 &&
               owner->instanceGeneration()->nativeWindowGeneration() == 31 && !owner->instanceGeneration()->validationEnabled());

    const auto surface_error = owner->acquireSurfaceGeneration();
    ensure("the fake SDL surface is acquired", !surface_error);
    ensure("the instance parent owns the exact fake surface generation",
           owner->instanceGeneration()->hasSurfaceGeneration() && owner->instanceGeneration()->surface() == fakeSurface() &&
               owner->instanceGeneration()->surfaceNativeWindowGeneration() == 31);
    ensure("SDL receives the exact private window, instance, and null allocator",
           state.mSurfaceWindow == state.mWindow && state.mSurfaceInstance == fakeInstance() && !state.mSurfaceAllocator);

    const auto duplicate_surface = owner->acquireSurfaceGeneration();
    ensureSurfaceError("a duplicate surface acquisition is rejected", duplicate_surface, VulkanSurfaceAcquireCode::SurfaceAlreadyOwned);
    ensure_equals("duplicate acquisition makes no second SDL call", state.mCreateSurfaceCount, std::size_t{ 1 });

    const auto duplicate =
        owner->acquireInstanceGeneration(VulkanInstanceValidationMode::Disabled, VulkanInstancePortabilityMode::Disabled);
    ensure("a duplicate instance acquisition is rejected without replacing the owner",
           duplicate && duplicate->mCode == VulkanInstanceAcquireCode::InstanceAlreadyOwned && owner->instanceGeneration() &&
               owner->instanceGeneration()->instance() == fakeInstance());

    LLWindowSDLVulkan moved(std::move(*owner));
    ensure("move construction transfers the instance and surface generations",
           moved.instanceGeneration() && moved.instanceGeneration()->instance() == fakeInstance() &&
               moved.instanceGeneration()->surface() == fakeSurface());
    ensure("the moved-from SDL owner publishes no instance generation", owner->instanceGeneration() == nullptr);

    state.mOwnerDuringSurfaceDestroy  = &moved;
    state.mOwnerDuringInstanceDestroy = &moved;
    state.mOwnerDuringDestroy         = &moved;
    moved.reset();

    ensureEvents("reset destroys the Vulkan surface and instance before requirements, window, and loader teardown", state,
                 { Event::Load, Event::Create, Event::Flags, Event::Resolver, Event::Extensions, Event::CreateInstance,
                   Event::CreateSurface, Event::DestroySurface, Event::DestroyInstance, Event::Destroy, Event::Unload });
    ensure_equals("the Vulkan surface is destroyed exactly once", state.mDestroySurfaceCount, std::size_t{ 1 });
    ensure_equals("the Vulkan instance is destroyed exactly once", state.mDestroyInstanceCount, std::size_t{ 1 });
    ensure("requirements and the parent instance remain live while Vulkan destroys the surface",
           state.mRequirementsLiveDuringSurfaceDestroy && state.mInstanceLiveDuringSurfaceDestroy);
    ensure("both SDL loader references remain live while Vulkan destroys the surface", state.mLoaderLiveDuringSurfaceDestroy);
    ensure("requirements remain live while Vulkan destroys the instance", state.mRequirementsLiveDuringInstanceDestroy);
    ensure("the surface child is absent before Vulkan destroys the instance", state.mSurfaceAbsentDuringInstanceDestroy);
    ensure("both SDL loader references remain live while Vulkan destroys the instance", state.mLoaderLiveDuringInstanceDestroy);
    ensure("requirements are invalidated before SDL destroys the window", state.mRequirementsInvalidatedBeforeDestroy);
    ensure_equals("instance reset releases the explicit loader reference", state.mExplicitLoaderReferences, 0);
    ensure_equals("instance reset releases the window loader reference", state.mWindowLoaderReferences, 0);

    moved.reset();
    ensure_equals("a second instance-owner reset performs no teardown", state.mEventCount, std::size_t{ 11 });
    ensure_equals("a second instance-owner reset does not destroy the surface again", state.mDestroySurfaceCount, std::size_t{ 1 });
    ensure_equals("a second instance-owner reset does not destroy the instance again", state.mDestroyInstanceCount, std::size_t{ 1 });
}

template<>
template<>
void window_sdl_vulkan_object::test<8>()
{
    using namespace LLRenderVulkan;

    FakeState         source_state;
    FakeState         destination_state;
    ScopedVulkanState vulkan_state(source_state);
    const auto        info = createInfo();

    auto  source_result = acquireLLWindowSDLVulkan(info, 51, fakeOperations(source_state));
    auto* source        = acquiredWindow(source_result);
    ensure("move-assignment source acquired a Vulkan window", source != nullptr);
    ensure("move-assignment source acquired an instance",
           !source->acquireInstanceGeneration(VulkanInstanceValidationMode::Disabled, VulkanInstancePortabilityMode::Disabled));
    ensure("move-assignment source acquired a surface", !source->acquireSurfaceGeneration());

    destination_state.mWindow = reinterpret_cast<SDL_Window*>(static_cast<std::uintptr_t>(0x56780));
    vulkan_state.use(destination_state);
    auto  destination_result = acquireLLWindowSDLVulkan(info, 52, fakeOperations(destination_state));
    auto* destination        = acquiredWindow(destination_result);
    ensure("move-assignment destination acquired a Vulkan window", destination != nullptr);
    ensure("move-assignment destination acquired an instance",
           !destination->acquireInstanceGeneration(VulkanInstanceValidationMode::Disabled, VulkanInstancePortabilityMode::Disabled));
    ensure("move-assignment destination acquired a surface", !destination->acquireSurfaceGeneration());

    destination_state.mOwnerDuringSurfaceDestroy  = destination;
    destination_state.mOwnerDuringInstanceDestroy = destination;
    destination_state.mOwnerDuringDestroy         = destination;
    *destination                                  = std::move(*source);

    ensure("move assignment clears both source generations", !source->hasRequirements() && source->instanceGeneration() == nullptr);
    ensure("move assignment transfers the source window, instance, and surface generations",
           destination->isGenerationCurrent(51) && destination->instanceGeneration() &&
               destination->instanceGeneration()->nativeWindowGeneration() == 51 &&
               destination->instanceGeneration()->surfaceNativeWindowGeneration() == 51);
    ensureEvents("move assignment tears down the replaced surface and instance before its SDL resources", destination_state,
                 { Event::Load, Event::Create, Event::Flags, Event::Resolver, Event::Extensions, Event::CreateInstance,
                   Event::CreateSurface, Event::DestroySurface, Event::DestroyInstance, Event::Destroy, Event::Unload });
    ensure("replaced requirements and instance remain live while the replaced surface is destroyed",
           destination_state.mRequirementsLiveDuringSurfaceDestroy && destination_state.mInstanceLiveDuringSurfaceDestroy);
    ensure("replaced loader references remain live while the replaced surface is destroyed",
           destination_state.mLoaderLiveDuringSurfaceDestroy);
    ensure("replaced requirements remain live while the replaced instance is destroyed",
           destination_state.mRequirementsLiveDuringInstanceDestroy);
    ensure("the replaced surface is absent before its instance is destroyed", destination_state.mSurfaceAbsentDuringInstanceDestroy);
    ensure("replaced loader references remain live while the replaced instance is destroyed",
           destination_state.mLoaderLiveDuringInstanceDestroy);
    ensure("replaced requirements are invalid before the replaced SDL window is destroyed",
           destination_state.mRequirementsInvalidatedBeforeDestroy);
    ensure_equals("move assignment destroys the replaced surface once", destination_state.mDestroySurfaceCount, std::size_t{ 1 });
    ensure_equals("move assignment destroys the replaced instance once", destination_state.mDestroyInstanceCount, std::size_t{ 1 });

    vulkan_state.use(source_state);
    source_state.mOwnerDuringSurfaceDestroy  = destination;
    source_state.mOwnerDuringInstanceDestroy = destination;
    source_state.mOwnerDuringDestroy         = destination;
    destination->reset();
    ensureEvents("the transferred owner preserves surface-first teardown for the source resources", source_state,
                 { Event::Load, Event::Create, Event::Flags, Event::Resolver, Event::Extensions, Event::CreateInstance,
                   Event::CreateSurface, Event::DestroySurface, Event::DestroyInstance, Event::Destroy, Event::Unload });
    ensure("transferred requirements and instance remain live while the transferred surface is destroyed",
           source_state.mRequirementsLiveDuringSurfaceDestroy && source_state.mInstanceLiveDuringSurfaceDestroy);
    ensure("transferred loader references remain live while the transferred surface is destroyed",
           source_state.mLoaderLiveDuringSurfaceDestroy);
    ensure("transferred requirements remain live while the transferred instance is destroyed",
           source_state.mRequirementsLiveDuringInstanceDestroy);
    ensure("the transferred surface is absent before its instance is destroyed", source_state.mSurfaceAbsentDuringInstanceDestroy);
    ensure("transferred loader references remain live while the transferred instance is destroyed",
           source_state.mLoaderLiveDuringInstanceDestroy);
    ensure("transferred requirements are invalid before the source SDL window is destroyed",
           source_state.mRequirementsInvalidatedBeforeDestroy);
    ensure_equals("the transferred surface is destroyed once", source_state.mDestroySurfaceCount, std::size_t{ 1 });
    ensure_equals("the transferred instance is destroyed once", source_state.mDestroyInstanceCount, std::size_t{ 1 });
}

template<>
template<>
void window_sdl_vulkan_object::test<9>()
{
    using namespace LLRenderVulkan;

    FakeState         state;
    ScopedVulkanState vulkan_state(state);
    state.mFailInstanceCreation = true;
    const auto info             = createInfo();
    auto       result           = acquireLLWindowSDLVulkan(info, 61, fakeOperations(state));
    auto*      owner            = acquiredWindow(result);
    ensure("instance-failure fixture acquired a Vulkan window", owner != nullptr);

    const auto error = owner->acquireInstanceGeneration(VulkanInstanceValidationMode::Disabled, VulkanInstancePortabilityMode::Disabled);
    ensure("instance creation failure is returned without publishing an owner",
           error && error->mCode == VulkanInstanceAcquireCode::InstanceCreationFailure &&
               error->mResult == VK_ERROR_INITIALIZATION_FAILED && owner->instanceGeneration() == nullptr);

    state.mOwnerDuringDestroy = owner;
    owner->reset();
    ensureEvents("a failed instance acquisition still releases the SDL window and explicit loader", state,
                 { Event::Load, Event::Create, Event::Flags, Event::Resolver, Event::Extensions, Event::CreateInstance, Event::Destroy,
                   Event::Unload });
    ensure_equals("a failed instance acquisition never destroys an unowned instance", state.mDestroyInstanceCount, std::size_t{ 0 });
    ensure("failed acquisition invalidates requirements before SDL destroys the window", state.mRequirementsInvalidatedBeforeDestroy);
    ensure_equals("failed acquisition releases the explicit loader reference", state.mExplicitLoaderReferences, 0);
    ensure_equals("failed acquisition releases the window loader reference", state.mWindowLoaderReferences, 0);
}

template<>
template<>
void window_sdl_vulkan_object::test<10>()
{
    using namespace LLRenderVulkan;

    FakeState         state;
    ScopedVulkanState vulkan_state(state);
    const auto        info   = createInfo();
    auto              result = acquireLLWindowSDLVulkan(info, 71, fakeOperations(state));
    auto*             owner  = acquiredWindow(result);
    ensure("surface-reset fixture acquired a Vulkan window", owner != nullptr);
    ensure("surface-reset fixture acquired an instance",
           !owner->acquireInstanceGeneration(VulkanInstanceValidationMode::Disabled, VulkanInstancePortabilityMode::Disabled));
    ensure("surface-reset fixture acquired a surface", !owner->acquireSurfaceGeneration());

    state.mOwnerDuringSurfaceDestroy = owner;
    ensure("explicit surface reset reports an owned generation", owner->resetSurfaceGeneration());
    ensureEvents("explicit surface reset destroys only the surface", state,
                 { Event::Load, Event::Create, Event::Flags, Event::Resolver, Event::Extensions, Event::CreateInstance,
                   Event::CreateSurface, Event::DestroySurface });
    ensure("explicit surface reset leaves the parent instance and requirements live",
           owner->instanceGeneration() && owner->instanceGeneration()->instance() == fakeInstance() && owner->hasRequirements());
    ensure("explicit surface reset publishes no child", !owner->instanceGeneration()->hasSurfaceGeneration());
    ensure("a second explicit surface reset is idempotent", !owner->resetSurfaceGeneration());
    ensure_equals("idempotent surface reset performs no Vulkan call", state.mEventCount, std::size_t{ 8 });

    ensure("the same current generations may reacquire a surface", !owner->acquireSurfaceGeneration());
    ensure("the reacquired surface is published", owner->instanceGeneration()->surface() == fakeSurface());

    state.mOwnerDuringInstanceDestroy = owner;
    state.mOwnerDuringDestroy         = owner;
    owner->reset();
    ensureEvents("full reset destroys the reacquired surface before its parent and SDL resources", state,
                 { Event::Load, Event::Create, Event::Flags, Event::Resolver, Event::Extensions, Event::CreateInstance,
                   Event::CreateSurface, Event::DestroySurface, Event::CreateSurface, Event::DestroySurface, Event::DestroyInstance,
                   Event::Destroy, Event::Unload });
    ensure_equals("both earned surface generations are destroyed once", state.mDestroySurfaceCount, std::size_t{ 2 });
    ensure_equals("the shared parent instance is destroyed once", state.mDestroyInstanceCount, std::size_t{ 1 });
}

template<>
template<>
void window_sdl_vulkan_object::test<11>()
{
    using namespace LLRenderVulkan;

    FakeState         state;
    ScopedVulkanState vulkan_state(state);
    const auto        info   = createInfo();
    auto              result = acquireLLWindowSDLVulkan(info, 81, fakeOperations(state));
    auto*             owner  = acquiredWindow(result);
    ensure("surface-failure fixture acquired a Vulkan window", owner != nullptr);

    const auto missing_parent = owner->acquireSurfaceGeneration();
    ensureSurfaceError("surface acquisition rejects a missing instance parent", missing_parent, VulkanSurfaceAcquireCode::InstanceNotLive);
    ensure_equals("a missing instance parent makes no SDL surface call", state.mCreateSurfaceCount, std::size_t{ 0 });

    ensure("surface-failure fixture acquired an instance",
           !owner->acquireInstanceGeneration(VulkanInstanceValidationMode::Disabled, VulkanInstancePortabilityMode::Disabled));

    state.mExposeDestroySurface = false;
    const auto missing_destroy  = owner->acquireSurfaceGeneration();
    ensureSurfaceError("a missing surface destroy command fails before SDL creation", missing_destroy,
                       VulkanSurfaceAcquireCode::MissingRequiredInstanceCommand);
    ensure("the exact missing destroy command is retained",
           missing_destroy && missing_destroy->mCommand && *missing_destroy->mCommand == VulkanSurfaceCommand::DestroySurface);
    ensure_equals("a missing destroy command makes no SDL surface call", state.mCreateSurfaceCount, std::size_t{ 0 });

    state.mExposeDestroySurface   = true;
    state.mFailSurfaceCreation    = true;
    state.mPoisonSurfaceOnFailure = true;
    const auto platform_failure   = owner->acquireSurfaceGeneration();
    ensureSurfaceError("SDL false maps to a platform failure", platform_failure, VulkanSurfaceAcquireCode::PlatformCreationFailure);
    ensure("SDL platform failure carries no invented Vulkan result", platform_failure && !platform_failure->mResult);
    ensure("a poisoned failed output is neither published nor destroyed",
           !owner->instanceGeneration()->hasSurfaceGeneration() && state.mDestroySurfaceCount == 0);

    state.mFailSurfaceCreation  = false;
    state.mNullSurfaceOnSuccess = true;
    const auto null_success     = owner->acquireSurfaceGeneration();
    ensureSurfaceError("SDL success with a null handle is rejected", null_success, VulkanSurfaceAcquireCode::NullSurfaceOnSuccess);
    ensure("a null success publishes no child and destroys no surface",
           !owner->instanceGeneration()->hasSurfaceGeneration() && state.mDestroySurfaceCount == 0);

    state.mNullSurfaceOnSuccess = false;
    ensure("a prior platform or null-output failure does not poison later acquisition", !owner->acquireSurfaceGeneration());
    ensure_equals("the three creator paths call SDL exactly three times", state.mCreateSurfaceCount, std::size_t{ 3 });

    state.mOwnerDuringSurfaceDestroy  = owner;
    state.mOwnerDuringInstanceDestroy = owner;
    state.mOwnerDuringDestroy         = owner;
    owner->reset();
    ensure_equals("only the earned surface is destroyed", state.mDestroySurfaceCount, std::size_t{ 1 });
    ensure_equals("failure recovery retains one parent destruction", state.mDestroyInstanceCount, std::size_t{ 1 });
}

template<>
template<>
void window_sdl_vulkan_object::test<12>()
{
    using namespace LLRenderVulkan;

    FakeState         state;
    ScopedVulkanState vulkan_state(state);
    const auto        info   = createInfo();
    auto              result = acquireLLWindowSDLVulkan(info, 91, fakeOperations(state));
    auto*             owner  = acquiredWindow(result);
    ensure("surface-allocation fixture acquired a Vulkan window", owner != nullptr);
    ensure("surface-allocation fixture acquired an instance",
           !owner->acquireInstanceGeneration(VulkanInstanceValidationMode::Disabled, VulkanInstancePortabilityMode::Disabled));

    const auto allocation_failure = LLWindowSDLVulkanDetail::acquireSurfaceGeneration(*owner, failAllocation);
    ensureSurfaceError("child allocation failure is returned through the SDL adapter", allocation_failure,
                       VulkanSurfaceAcquireCode::AllocationFailure);
    ensure("allocation failure leaves the existing parent and requirements live",
           owner->instanceGeneration() && owner->instanceGeneration()->instance() == fakeInstance() && owner->hasRequirements());
    ensure("allocation failure runs no SDL creator and publishes no child",
           state.mCreateSurfaceCount == 0 && !owner->instanceGeneration()->hasSurfaceGeneration());

    ensure("normal acquisition still succeeds after allocation failure", !owner->acquireSurfaceGeneration());
    state.mOwnerDuringSurfaceDestroy  = owner;
    state.mOwnerDuringInstanceDestroy = owner;
    state.mOwnerDuringDestroy         = owner;
    owner->reset();
    ensure_equals("post-allocation recovery destroys one surface", state.mDestroySurfaceCount, std::size_t{ 1 });
    ensure_equals("post-allocation recovery destroys one instance", state.mDestroyInstanceCount, std::size_t{ 1 });
}

} // namespace tut
