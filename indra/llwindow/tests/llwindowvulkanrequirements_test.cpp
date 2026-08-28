/**
 * @file llwindowvulkanrequirements_test.cpp
 * @brief Tests for loader-independent native-window Vulkan requirements.
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

#include "lltut.h"
#include "llwindowvulkanrequirements.h"

#include <algorithm>
#include <array>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>

namespace
{

void fakeResolver()
{
}

void failAllocation()
{
    throw std::bad_alloc();
}

const LLWindowVulkanRequirementsBuildError& requireError(const LLWindowVulkanRequirementsBuildResult& result)
{
    const auto* error = std::get_if<LLWindowVulkanRequirementsBuildError>(&result);
    tut::ensure("requirements construction returned an error", error != nullptr);
    return *error;
}

const LLWindowVulkanRequirements& requireRequirements(const LLWindowVulkanRequirementsBuildResult& result)
{
    const auto* requirements = std::get_if<LLWindowVulkanRequirements>(&result);
    tut::ensure("requirements construction returned a value", requirements != nullptr);
    return *requirements;
}

void ensureError(const LLWindowVulkanRequirementsBuildResult& result,
                 LLWindowVulkanRequirementsBuildCode          expected_code,
                 std::optional<std::size_t>
                     expected_index,
                 std::optional<std::size_t>
                     expected_count)
{
    const LLWindowVulkanRequirementsBuildError& error = requireError(result);
    tut::ensure("the exact error code is reported", error.mCode == expected_code);
    tut::ensure("the exact failing index is reported", error.mIndex == expected_index);
    tut::ensure("the exact failing count is reported", error.mCount == expected_count);
}

} // namespace

namespace tut
{

struct window_vulkan_requirements_test
{
};

using window_vulkan_requirements_test_group  = test_group<window_vulkan_requirements_test>;
using window_vulkan_requirements_test_object = window_vulkan_requirements_test_group::object;
window_vulkan_requirements_test_group window_vulkan_requirements_tests("window Vulkan requirements");

template<>
template<>
void window_vulkan_requirements_test_object::test<1>()
{
    static_assert(LL_WINDOW_VULKAN_MAX_EXTENSION_COUNT == 64);
    static_assert(LL_WINDOW_VULKAN_MAX_EXTENSION_NAME_SIZE == 256);
    static_assert(std::is_pointer_v<LLWindowVulkanFunction>);
    static_assert(std::is_function_v<std::remove_pointer_t<LLWindowVulkanFunction>>);
    static_assert(!std::is_default_constructible_v<LLWindowVulkanRequirements>);
    static_assert(!std::is_copy_constructible_v<LLWindowVulkanRequirements>);
    static_assert(!std::is_copy_assignable_v<LLWindowVulkanRequirements>);
    static_assert(std::is_nothrow_move_constructible_v<LLWindowVulkanRequirements>);
    static_assert(std::is_nothrow_move_assignable_v<LLWindowVulkanRequirements>);
    static_assert(std::is_nothrow_destructible_v<LLWindowVulkanRequirements>);
    static_assert(std::is_same_v<decltype(std::declval<const LLWindowVulkanRequirements&>().requiredInstanceExtensions()),
                                 const std::vector<std::string>&>);
    static_assert(noexcept(buildLLWindowVulkanRequirements(nullptr, 0, nullptr, 0)));
    static_assert(std::variant_size_v<LLWindowVulkanRequirementsBuildResult> == 2);
    static_assert(
        std::is_same_v<std::variant_alternative_t<0, LLWindowVulkanRequirementsBuildResult>, LLWindowVulkanRequirementsBuildError>);
    static_assert(std::is_same_v<std::variant_alternative_t<1, LLWindowVulkanRequirementsBuildResult>, LLWindowVulkanRequirements>);
    static_assert(!std::is_copy_constructible_v<LLWindowVulkanRequirementsBuildResult>);
    static_assert(std::is_nothrow_move_constructible_v<LLWindowVulkanRequirementsBuildResult>);
    static_assert(std::is_constructible_v<std::optional<LLWindowVulkanRequirements>, LLWindowVulkanRequirements&&>);

    const LLWindowVulkanRequirementsBuildError left{ LLWindowVulkanRequirementsBuildCode::EmptyExtensionName, 2, 3 };
    const LLWindowVulkanRequirementsBuildError same{ LLWindowVulkanRequirementsBuildCode::EmptyExtensionName, 2, 3 };
    const LLWindowVulkanRequirementsBuildError different{ LLWindowVulkanRequirementsBuildCode::NullExtensionName, 2, 3 };
    ensure("identical errors compare equal", left == same);
    ensure("different errors compare unequal", !(left == different));
}

template<>
template<>
void window_vulkan_requirements_test_object::test<2>()
{
    const char* const name = "VK_KHR_surface";
    ensureError(buildLLWindowVulkanRequirements(nullptr, 1, &name, 1), LLWindowVulkanRequirementsBuildCode::InvalidResolver, std::nullopt,
                std::nullopt);
    ensureError(buildLLWindowVulkanRequirements(fakeResolver, 0, nullptr, 1), LLWindowVulkanRequirementsBuildCode::InvalidExtensionCount,
                std::nullopt, 0);
    ensureError(buildLLWindowVulkanRequirements(fakeResolver, LL_WINDOW_VULKAN_MAX_EXTENSION_COUNT + 1, nullptr, 1),
                LLWindowVulkanRequirementsBuildCode::InvalidExtensionCount, std::nullopt, LL_WINDOW_VULKAN_MAX_EXTENSION_COUNT + 1);
    ensureError(buildLLWindowVulkanRequirements(fakeResolver, 1, nullptr, 1), LLWindowVulkanRequirementsBuildCode::InvalidExtensionList,
                std::nullopt, 1);
    ensureError(buildLLWindowVulkanRequirements(fakeResolver, 1, &name, 0),
                LLWindowVulkanRequirementsBuildCode::InvalidNativeWindowGeneration, std::nullopt, std::nullopt);
}

template<>
template<>
void window_vulkan_requirements_test_object::test<3>()
{
    const char* const null_name[] = { "VK_KHR_surface", nullptr };
    ensureError(buildLLWindowVulkanRequirements(fakeResolver, 2, null_name, 1), LLWindowVulkanRequirementsBuildCode::NullExtensionName, 1,
                2);

    const char* const empty_name[] = { "" };
    ensureError(buildLLWindowVulkanRequirements(fakeResolver, 1, empty_name, 1), LLWindowVulkanRequirementsBuildCode::EmptyExtensionName, 0,
                1);

    std::array<char, LL_WINDOW_VULKAN_MAX_EXTENSION_NAME_SIZE> unterminated_name;
    unterminated_name.fill('x');
    const char* const unterminated_names[] = { "VK_KHR_surface", unterminated_name.data() };
    ensureError(buildLLWindowVulkanRequirements(fakeResolver, 2, unterminated_names, 1),
                LLWindowVulkanRequirementsBuildCode::UnterminatedExtensionName, 1, 2);

    std::array<char, LL_WINDOW_VULKAN_MAX_EXTENSION_NAME_SIZE> longest_name;
    longest_name.fill('x');
    longest_name.back()               = '\0';
    const char* const longest_names[] = { longest_name.data() };
    const auto        longest_result  = buildLLWindowVulkanRequirements(fakeResolver, 1, longest_names, 1);
    ensure_equals("a 255-byte extension name is accepted", requireRequirements(longest_result).requiredInstanceExtensions().front().size(),
                  std::size_t{ 255 });
}

template<>
template<>
void window_vulkan_requirements_test_object::test<4>()
{
    const char* const duplicate_names[] = { "VK_KHR_surface", "VK_EXT_debug_utils", "VK_KHR_surface" };
    ensureError(buildLLWindowVulkanRequirements(fakeResolver, 3, duplicate_names, 7),
                LLWindowVulkanRequirementsBuildCode::DuplicateExtensionName, 2, 3);

    const char* const case_distinct_names[] = { "VK_KHR_surface", "vk_khr_surface" };
    const auto        case_result           = buildLLWindowVulkanRequirements(fakeResolver, 2, case_distinct_names, 7);
    const auto&       case_requirements     = requireRequirements(case_result);
    ensure_equals("extension duplicate matching is case-sensitive", case_requirements.requiredInstanceExtensions().size(),
                  std::size_t{ 2 });
}

template<>
template<>
void window_vulkan_requirements_test_object::test<5>()
{
    std::array<char, 32> first_name{};
    std::array<char, 32> second_name{};
    std::copy_n("VK_KHR_surface", 15, first_name.begin());
    std::copy_n("VK_KHR_xcb_surface", 19, second_name.begin());
    const char* names[] = { first_name.data(), second_name.data() };

    auto result = buildLLWindowVulkanRequirements(fakeResolver, 2, names, 42);
    first_name.fill('p');
    second_name.fill('q');
    names[0] = nullptr;
    names[1] = nullptr;

    const LLWindowVulkanRequirements& requirements = requireRequirements(result);
    ensure("the exact opaque resolver is retained", requirements.resolver() == fakeResolver);
    ensure_equals("the native-window generation is retained", requirements.nativeWindowGeneration(), U64{ 42 });
    ensure_equals("all required extension names are retained", requirements.requiredInstanceExtensions().size(), std::size_t{ 2 });
    ensure_equals("extension order and first deep copy are retained", requirements.requiredInstanceExtensions()[0],
                  std::string("VK_KHR_surface"));
    ensure_equals("extension order and second deep copy are retained", requirements.requiredInstanceExtensions()[1],
                  std::string("VK_KHR_xcb_surface"));
}

template<>
template<>
void window_vulkan_requirements_test_object::test<6>()
{
    std::array<std::string, LL_WINDOW_VULKAN_MAX_EXTENSION_COUNT> storage;
    std::array<const char*, LL_WINDOW_VULKAN_MAX_EXTENSION_COUNT> names;
    for (std::size_t index = 0; index < storage.size(); ++index)
    {
        storage[index] = "VK_TEST_extension_" + std::to_string(index);
        names[index]   = storage[index].c_str();
    }

    auto result = buildLLWindowVulkanRequirements(fakeResolver, names.size(), names.data(), U64{ 9 });
    ensure_equals("the bounded maximum extension count is accepted", requireRequirements(result).requiredInstanceExtensions().size(),
                  names.size());

    LLWindowVulkanRequirements                moved = std::move(std::get<LLWindowVulkanRequirements>(result));
    std::optional<LLWindowVulkanRequirements> owned;
    owned.emplace(std::move(moved));
    ensure("a window-owned optional retains the resolver", owned->resolver() == fakeResolver);
    ensure_equals("a window-owned optional retains every name", owned->requiredInstanceExtensions().size(), names.size());
    ensure_equals("a window-owned optional retains the generation", owned->nativeWindowGeneration(), U64{ 9 });
}

template<>
template<>
void window_vulkan_requirements_test_object::test<7>()
{
    const char* const name   = "VK_KHR_surface";
    const auto        result = LLWindowVulkanRequirementsDetail::build(fakeResolver, 1, &name, 1, failAllocation);
    ensureError(result, LLWindowVulkanRequirementsBuildCode::AllocationFailure, std::nullopt, 1);
}

} // namespace tut
