/**
 * @file llwindowvulkanrequirements.h
 * @brief Loader-independent Vulkan requirements supplied by a native window.
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

#ifndef LL_LLWINDOWVULKANREQUIREMENTS_H
#define LL_LLWINDOWVULKANREQUIREMENTS_H

#include "stdtypes.h"

#include <cstddef>
#include <optional>
#include <string>
#include <variant>
#include <vector>

// Deliberately opaque here. The renderer may cast this borrowed resolver to
// Vulkan's PFN_vkGetInstanceProcAddr only in code that includes Vulkan headers.
using LLWindowVulkanFunction = void (*)();

inline constexpr std::size_t LL_WINDOW_VULKAN_MAX_EXTENSION_COUNT     = 64;
inline constexpr std::size_t LL_WINDOW_VULKAN_MAX_EXTENSION_NAME_SIZE = 256;

enum class LLWindowVulkanRequirementsBuildCode : U8
{
    InvalidResolver,
    InvalidExtensionCount,
    InvalidExtensionList,
    NullExtensionName,
    EmptyExtensionName,
    UnterminatedExtensionName,
    DuplicateExtensionName,
    InvalidNativeWindowGeneration,
    AllocationFailure
};

struct LLWindowVulkanRequirementsBuildError
{
    LLWindowVulkanRequirementsBuildCode mCode = LLWindowVulkanRequirementsBuildCode::InvalidResolver;
    std::optional<std::size_t>          mIndex;
    std::optional<std::size_t>          mCount;

    friend constexpr bool operator==(const LLWindowVulkanRequirementsBuildError&, const LLWindowVulkanRequirementsBuildError&) = default;
};

// The resolver is borrowed from the native window's loader lease. Extension
// names are owned so the source window toolkit storage never escapes. The
// generation lets the owner reject requirements from a replaced native window.
class LLWindowVulkanRequirements
{
public:
    LLWindowVulkanRequirements(const LLWindowVulkanRequirements&)                = delete;
    LLWindowVulkanRequirements& operator=(const LLWindowVulkanRequirements&)     = delete;
    LLWindowVulkanRequirements(LLWindowVulkanRequirements&&) noexcept            = default;
    LLWindowVulkanRequirements& operator=(LLWindowVulkanRequirements&&) noexcept = default;

    LLWindowVulkanFunction          resolver() const noexcept { return mResolver; }
    const std::vector<std::string>& requiredInstanceExtensions() const noexcept { return mRequiredInstanceExtensions; }
    U64                             nativeWindowGeneration() const noexcept { return mNativeWindowGeneration; }

private:
    friend struct LLWindowVulkanRequirementsFactory;

    LLWindowVulkanRequirements(LLWindowVulkanFunction     resolver,
                               std::vector<std::string>&& required_instance_extensions,
                               U64                        native_window_generation) noexcept;

    LLWindowVulkanFunction   mResolver = nullptr;
    std::vector<std::string> mRequiredInstanceExtensions;
    U64                      mNativeWindowGeneration = 0;
};

using LLWindowVulkanRequirementsBuildResult = std::variant<LLWindowVulkanRequirementsBuildError, LLWindowVulkanRequirements>;

LLWindowVulkanRequirementsBuildResult buildLLWindowVulkanRequirements(LLWindowVulkanFunction resolver,
                                                                      std::size_t            extension_count,
                                                                      const char* const*     extension_names,
                                                                      U64                    native_window_generation) noexcept;

namespace LLWindowVulkanRequirementsDetail
{

// Keeps allocation-failure handling deterministic in the pure contract test.
// Production callers use buildLLWindowVulkanRequirements() above.
using AllocationCheckpoint = void (*)();

LLWindowVulkanRequirementsBuildResult build(LLWindowVulkanFunction resolver,
                                            std::size_t            extension_count,
                                            const char* const*     extension_names,
                                            U64                    native_window_generation,
                                            AllocationCheckpoint   allocation_checkpoint) noexcept;

} // namespace LLWindowVulkanRequirementsDetail

#endif // LL_LLWINDOWVULKANREQUIREMENTS_H
