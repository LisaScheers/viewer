/**
 * @file llwindowsdlvulkan.cpp
 * @brief SDL Vulkan window and loader lifetime ownership.
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

#include "SDL3/SDL_vulkan.h"

#include <utility>

namespace
{

bool loadLibrary(void*) noexcept
{
    SDL_assert(SDL_IsMainThread());
    return SDL_Vulkan_LoadLibrary(nullptr);
}

void unloadLibrary(void*) noexcept
{
    SDL_assert(SDL_IsMainThread());
    SDL_Vulkan_UnloadLibrary();
}

SDL_Window* createWindow(void*, const LLWindowSDLVulkanCreateInfo& info) noexcept
{
    SDL_assert(SDL_IsMainThread());

    const SDL_PropertiesID properties = SDL_CreateProperties();
    if (!properties)
    {
        return nullptr;
    }

    const bool configured = SDL_SetStringProperty(properties, SDL_PROP_WINDOW_CREATE_TITLE_STRING, info.mTitle.c_str()) &&
                            SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_X_NUMBER, info.mX) &&
                            SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_Y_NUMBER, info.mY) &&
                            SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, info.mWidth) &&
                            SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, info.mHeight) &&
                            SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, info.mResizable) &&
                            SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN, info.mFullscreen) &&
                            SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_HIDDEN_BOOLEAN, info.mHidden) &&
                            SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_HIGH_PIXEL_DENSITY_BOOLEAN, info.mHighPixelDensity) &&
                            SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_VULKAN_BOOLEAN, true);

    SDL_Window* window = configured ? SDL_CreateWindowWithProperties(properties) : nullptr;
    SDL_DestroyProperties(properties);
    return window;
}

void destroyWindow(void*, SDL_Window* window) noexcept
{
    SDL_assert(SDL_IsMainThread());
    SDL_DestroyWindow(window);
}

SDL_WindowFlags getWindowFlags(void*, SDL_Window* window) noexcept
{
    SDL_assert(SDL_IsMainThread());
    return SDL_GetWindowFlags(window);
}

LLWindowVulkanFunction getResolver(void*) noexcept
{
    SDL_assert(SDL_IsMainThread());
    return SDL_Vulkan_GetVkGetInstanceProcAddr();
}

const char* const* getInstanceExtensions(void*, std::size_t* count) noexcept
{
    SDL_assert(SDL_IsMainThread());
    Uint32             sdl_count  = 0;
    const char* const* extensions = SDL_Vulkan_GetInstanceExtensions(&sdl_count);
    *count                        = sdl_count;
    return extensions;
}

bool validOperations(const LLWindowSDLVulkanOperations& operations) noexcept
{
    return operations.mLoadLibrary && operations.mUnloadLibrary && operations.mCreateWindow && operations.mDestroyWindow &&
           operations.mGetWindowFlags && operations.mGetResolver && operations.mGetInstanceExtensions;
}

LLWindowSDLVulkanAcquireError failure(LLWindowSDLVulkanAcquireCode code) noexcept
{
    return { code, std::nullopt };
}

class Rollback
{
public:
    explicit Rollback(const LLWindowSDLVulkanOperations& operations) noexcept : mOperations(operations) {}

    ~Rollback() noexcept
    {
        if (mWindow)
        {
            mOperations.mDestroyWindow(mOperations.mUserdata, mWindow);
        }
        if (mLoaded)
        {
            mOperations.mUnloadLibrary(mOperations.mUserdata);
        }
    }

    void loaded() noexcept { mLoaded = true; }
    void window(SDL_Window* window) noexcept { mWindow = window; }
    void release() noexcept
    {
        mLoaded = false;
        mWindow = nullptr;
    }

private:
    LLWindowSDLVulkanOperations mOperations;
    SDL_Window*                 mWindow = nullptr;
    bool                        mLoaded = false;
};

} // namespace

LLWindowSDLVulkan::LLWindowSDLVulkan(const LLWindowSDLVulkanOperations& operations,
                                     SDL_Window*                        window,
                                     LLWindowVulkanRequirements&&       requirements) noexcept :
    mOperations(operations),
    mWindow(window),
    mRequirements(std::move(requirements)),
    mExplicitLoaderReference(true)
{
}

LLWindowSDLVulkan::~LLWindowSDLVulkan() noexcept
{
    reset();
}

LLWindowSDLVulkan::LLWindowSDLVulkan(LLWindowSDLVulkan&& other) noexcept :
    mOperations(other.mOperations),
    mWindow(std::exchange(other.mWindow, nullptr)),
    mRequirements(std::move(other.mRequirements)),
    mExplicitLoaderReference(std::exchange(other.mExplicitLoaderReference, false))
{
    other.mRequirements.reset();
}

LLWindowSDLVulkan& LLWindowSDLVulkan::operator=(LLWindowSDLVulkan&& other) noexcept
{
    if (this != &other)
    {
        reset();
        mOperations              = other.mOperations;
        mWindow                  = std::exchange(other.mWindow, nullptr);
        mRequirements            = std::move(other.mRequirements);
        mExplicitLoaderReference = std::exchange(other.mExplicitLoaderReference, false);
        other.mRequirements.reset();
    }
    return *this;
}

bool LLWindowSDLVulkan::isGenerationCurrent(U64 native_window_generation) const noexcept
{
    return native_window_generation != 0 && mRequirements && mRequirements->nativeWindowGeneration() == native_window_generation;
}

void LLWindowSDLVulkan::reset() noexcept
{
    mRequirements.reset();
    if (mWindow)
    {
        mOperations.mDestroyWindow(mOperations.mUserdata, mWindow);
        mWindow = nullptr;
    }
    if (mExplicitLoaderReference)
    {
        mOperations.mUnloadLibrary(mOperations.mUserdata);
        mExplicitLoaderReference = false;
    }
}

const LLWindowSDLVulkanOperations& defaultLLWindowSDLVulkanOperations() noexcept
{
    static const LLWindowSDLVulkanOperations operations{ nullptr,       loadLibrary,    unloadLibrary, createWindow,
                                                         destroyWindow, getWindowFlags, getResolver,   getInstanceExtensions };
    return operations;
}

LLWindowSDLVulkanAcquireResult acquireLLWindowSDLVulkan(const LLWindowSDLVulkanCreateInfo& info,
                                                        U64                                native_window_generation,
                                                        const LLWindowSDLVulkanOperations& operations) noexcept
{
    if (!validOperations(operations))
    {
        return failure(LLWindowSDLVulkanAcquireCode::InvalidOperations);
    }

    Rollback rollback(operations);
    if (!operations.mLoadLibrary(operations.mUserdata))
    {
        return failure(LLWindowSDLVulkanAcquireCode::LoaderFailure);
    }
    rollback.loaded();

    SDL_Window* window = operations.mCreateWindow(operations.mUserdata, info);
    if (!window)
    {
        return failure(LLWindowSDLVulkanAcquireCode::WindowFailure);
    }
    rollback.window(window);

    const SDL_WindowFlags window_flags = operations.mGetWindowFlags(operations.mUserdata, window);
    if ((window_flags & SDL_WINDOW_VULKAN) == 0 || (window_flags & SDL_WINDOW_OPENGL) != 0)
    {
        return failure(LLWindowSDLVulkanAcquireCode::WindowFlagsFailure);
    }

    const LLWindowVulkanFunction resolver = operations.mGetResolver(operations.mUserdata);
    if (!resolver)
    {
        return failure(LLWindowSDLVulkanAcquireCode::ResolverFailure);
    }

    std::size_t        extension_count = 0;
    const char* const* extension_names = operations.mGetInstanceExtensions(operations.mUserdata, &extension_count);
    if (!extension_names)
    {
        return failure(LLWindowSDLVulkanAcquireCode::ExtensionQueryFailure);
    }

    LLWindowVulkanRequirementsBuildResult requirements_result =
        buildLLWindowVulkanRequirements(resolver, extension_count, extension_names, native_window_generation);
    if (auto* error = std::get_if<LLWindowVulkanRequirementsBuildError>(&requirements_result))
    {
        return LLWindowSDLVulkanAcquireError{ LLWindowSDLVulkanAcquireCode::RequirementsFailure, *error };
    }

    rollback.release();
    return LLWindowSDLVulkan(operations, window, std::move(std::get<LLWindowVulkanRequirements>(requirements_result)));
}
