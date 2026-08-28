/**
 * @file llwindowsdlvulkan.h
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

#ifndef LL_LLWINDOWSDLVULKAN_H
#define LL_LLWINDOWSDLVULKAN_H

#include "llwindowvulkanrequirements.h"

#include "SDL3/SDL.h"

#include <optional>
#include <string>
#include <variant>

struct LLWindowSDLVulkanCreateInfo
{
    std::string mTitle;
    int         mX                = 0;
    int         mY                = 0;
    int         mWidth            = 0;
    int         mHeight           = 0;
    bool        mResizable        = true;
    bool        mFullscreen       = false;
    bool        mHidden           = true;
    bool        mHighPixelDensity = false;
};

// The create operation receives a Vulkan-only description. The production
// operation sets SDL_PROP_WINDOW_CREATE_VULKAN_BOOLEAN and never requests an
// OpenGL window.
struct LLWindowSDLVulkanOperations
{
    void* mUserdata = nullptr;

    bool (*mLoadLibrary)(void* userdata) noexcept                                                  = nullptr;
    void (*mUnloadLibrary)(void* userdata) noexcept                                                = nullptr;
    SDL_Window* (*mCreateWindow)(void* userdata, const LLWindowSDLVulkanCreateInfo& info) noexcept = nullptr;
    void (*mDestroyWindow)(void* userdata, SDL_Window* window) noexcept                            = nullptr;
    SDL_WindowFlags (*mGetWindowFlags)(void* userdata, SDL_Window* window) noexcept                = nullptr;
    LLWindowVulkanFunction (*mGetResolver)(void* userdata) noexcept                                = nullptr;
    const char* const* (*mGetInstanceExtensions)(void* userdata, std::size_t* count) noexcept      = nullptr;
};

enum class LLWindowSDLVulkanAcquireCode : U8
{
    InvalidOperations,
    LoaderFailure,
    WindowFailure,
    WindowFlagsFailure,
    ResolverFailure,
    ExtensionQueryFailure,
    RequirementsFailure
};

struct LLWindowSDLVulkanAcquireError
{
    LLWindowSDLVulkanAcquireCode                        mCode = LLWindowSDLVulkanAcquireCode::InvalidOperations;
    std::optional<LLWindowVulkanRequirementsBuildError> mRequirementsError;

    friend constexpr bool operator==(const LLWindowSDLVulkanAcquireError&, const LLWindowSDLVulkanAcquireError&) = default;
};

// Owns both references involved in an SDL Vulkan window. SDL owns one loader
// reference through the window, while this object owns the explicit reference
// acquired before window creation.
class LLWindowSDLVulkan
{
public:
    ~LLWindowSDLVulkan() noexcept;

    LLWindowSDLVulkan(const LLWindowSDLVulkan&)            = delete;
    LLWindowSDLVulkan& operator=(const LLWindowSDLVulkan&) = delete;
    LLWindowSDLVulkan(LLWindowSDLVulkan&& other) noexcept;
    LLWindowSDLVulkan& operator=(LLWindowSDLVulkan&& other) noexcept;

    bool hasRequirements() const noexcept { return mRequirements.has_value(); }
    const LLWindowVulkanRequirements* requirements() const noexcept
    {
        return mRequirements ? &*mRequirements : nullptr;
    }
    bool isGenerationCurrent(U64 native_window_generation) const noexcept;

    void reset() noexcept;

private:
    friend class LLWindowSDL;
    friend std::variant<LLWindowSDLVulkanAcquireError, LLWindowSDLVulkan> acquireLLWindowSDLVulkan(
        const LLWindowSDLVulkanCreateInfo&,
        U64,
        const LLWindowSDLVulkanOperations&) noexcept;

    SDL_Window* window() const noexcept { return mWindow; }

    LLWindowSDLVulkan(const LLWindowSDLVulkanOperations& operations,
                      SDL_Window*                        window,
                      LLWindowVulkanRequirements&&       requirements) noexcept;

    LLWindowSDLVulkanOperations               mOperations;
    SDL_Window*                               mWindow = nullptr;
    std::optional<LLWindowVulkanRequirements> mRequirements;
    bool                                      mExplicitLoaderReference = false;
};

using LLWindowSDLVulkanAcquireResult = std::variant<LLWindowSDLVulkanAcquireError, LLWindowSDLVulkan>;

const LLWindowSDLVulkanOperations& defaultLLWindowSDLVulkanOperations() noexcept;

LLWindowSDLVulkanAcquireResult acquireLLWindowSDLVulkan(
    const LLWindowSDLVulkanCreateInfo& info,
    U64                                native_window_generation,
    const LLWindowSDLVulkanOperations& operations = defaultLLWindowSDLVulkanOperations()) noexcept;

#endif // LL_LLWINDOWSDLVULKAN_H
