/**
 * @file llwindowvulkanrequirements.cpp
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

#include "linden_common.h"

#include "llwindowvulkanrequirements.h"

#include <algorithm>
#include <new>
#include <utility>

struct LLWindowVulkanRequirementsFactory
{
    static LLWindowVulkanRequirements create(LLWindowVulkanFunction     resolver,
                                             std::vector<std::string>&& required_instance_extensions,
                                             U64                        native_window_generation) noexcept
    {
        return LLWindowVulkanRequirements(resolver, std::move(required_instance_extensions), native_window_generation);
    }
};

LLWindowVulkanRequirements::LLWindowVulkanRequirements(LLWindowVulkanFunction     resolver,
                                                       std::vector<std::string>&& required_instance_extensions,
                                                       U64                        native_window_generation) noexcept :
    mResolver(resolver),
    mRequiredInstanceExtensions(std::move(required_instance_extensions)),
    mNativeWindowGeneration(native_window_generation)
{
}

namespace
{

LLWindowVulkanRequirementsBuildError failure(LLWindowVulkanRequirementsBuildCode code,
                                             std::optional<std::size_t>          index = std::nullopt,
                                             std::optional<std::size_t>          count = std::nullopt) noexcept
{
    return { code, index, count };
}

std::optional<std::size_t> boundedLength(const char* name) noexcept
{
    for (std::size_t length = 0; length < LL_WINDOW_VULKAN_MAX_EXTENSION_NAME_SIZE; ++length)
    {
        if (name[length] == '\0')
        {
            return length;
        }
    }
    return std::nullopt;
}

} // namespace

namespace LLWindowVulkanRequirementsDetail
{

LLWindowVulkanRequirementsBuildResult build(LLWindowVulkanFunction resolver,
                                            std::size_t            extension_count,
                                            const char* const*     extension_names,
                                            U64                    native_window_generation,
                                            AllocationCheckpoint   allocation_checkpoint) noexcept
{
    if (!resolver)
    {
        return failure(LLWindowVulkanRequirementsBuildCode::InvalidResolver);
    }
    if (extension_count == 0 || extension_count > LL_WINDOW_VULKAN_MAX_EXTENSION_COUNT)
    {
        return failure(LLWindowVulkanRequirementsBuildCode::InvalidExtensionCount, std::nullopt, extension_count);
    }
    if (!extension_names)
    {
        return failure(LLWindowVulkanRequirementsBuildCode::InvalidExtensionList, std::nullopt, extension_count);
    }
    if (native_window_generation == 0)
    {
        return failure(LLWindowVulkanRequirementsBuildCode::InvalidNativeWindowGeneration);
    }

    for (std::size_t index = 0; index < extension_count; ++index)
    {
        const char* const name = extension_names[index];
        if (!name)
        {
            return failure(LLWindowVulkanRequirementsBuildCode::NullExtensionName, index, extension_count);
        }

        const std::optional<std::size_t> length = boundedLength(name);
        if (!length)
        {
            return failure(LLWindowVulkanRequirementsBuildCode::UnterminatedExtensionName, index, extension_count);
        }
        if (*length == 0)
        {
            return failure(LLWindowVulkanRequirementsBuildCode::EmptyExtensionName, index, extension_count);
        }

        for (std::size_t previous_index = 0; previous_index < index; ++previous_index)
        {
            const char* const previous_name   = extension_names[previous_index];
            const std::size_t previous_length = *boundedLength(previous_name);
            if (*length == previous_length && std::equal(name, name + *length, previous_name))
            {
                return failure(LLWindowVulkanRequirementsBuildCode::DuplicateExtensionName, index, extension_count);
            }
        }
    }

    try
    {
        if (allocation_checkpoint)
        {
            allocation_checkpoint();
        }
        std::vector<std::string> owned_names;
        owned_names.reserve(extension_count);
        for (std::size_t index = 0; index < extension_count; ++index)
        {
            owned_names.emplace_back(extension_names[index]);
        }
        return LLWindowVulkanRequirementsFactory::create(resolver, std::move(owned_names), native_window_generation);
    }
    catch (const std::bad_alloc&)
    {
        return failure(LLWindowVulkanRequirementsBuildCode::AllocationFailure, std::nullopt, extension_count);
    }
}

} // namespace LLWindowVulkanRequirementsDetail

LLWindowVulkanRequirementsBuildResult buildLLWindowVulkanRequirements(LLWindowVulkanFunction resolver,
                                                                      std::size_t            extension_count,
                                                                      const char* const*     extension_names,
                                                                      U64                    native_window_generation) noexcept
{
    return LLWindowVulkanRequirementsDetail::build(resolver, extension_count, extension_names, native_window_generation, nullptr);
}
