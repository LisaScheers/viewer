/**
 * @file llwindowwin32vulkan-native.cpp
 * @brief Win32 operations for the isolated Vulkan surface owner.
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

#include "llwindowwin32vulkan.h"

#include <array>
#include <cstring>
#include <cwchar>
#include <exception>
#include <memory>

namespace
{

constexpr wchar_t WINDOW_TITLE[] = L"Second Life Vulkan surface diagnostic";

struct NativeWindowStorage
{
    HINSTANCE               mModuleInstance = nullptr;
    HWND                    mWindow         = nullptr;
    DWORD                   mOwnerThreadId  = 0;
    ATOM                    mClassAtom      = 0;
    std::array<wchar_t, 96> mClassName{};
};

enum class NativeWindowRefreshCode
{
    Success,
    IdentityFailure,
    GeometryFailure
};

LRESULT CALLBACK diagnosticWindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    return DefWindowProcW(window, message, wparam, lparam);
}

U64 currentThreadId(void*) noexcept
{
    return static_cast<U64>(GetCurrentThreadId());
}

void* openLoader(void*, const wchar_t* path) noexcept
{
    HMODULE module = nullptr;
    if (path && path[0] != L'\0')
    {
        module = LoadLibraryExW(path, nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    }
    else
    {
        module = LoadLibraryExW(L"vulkan-1.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    }
    return reinterpret_cast<void*>(module);
}

void closeLoader(void*, void* loader) noexcept
{
    if (loader)
    {
        FreeLibrary(reinterpret_cast<HMODULE>(loader));
    }
}

LLWindowVulkanFunction getResolver(void*, void* loader) noexcept
{
    if (!loader)
    {
        return nullptr;
    }

    FARPROC symbol = GetProcAddress(reinterpret_cast<HMODULE>(loader), "vkGetInstanceProcAddr");
    static_assert(sizeof(symbol) == sizeof(LLWindowVulkanFunction));
    LLWindowVulkanFunction resolver = nullptr;
    std::memcpy(&resolver, &symbol, sizeof(resolver));
    return resolver;
}

bool exactNativeIdentity(const LLWindowWin32VulkanNativeWindow& native_window, const NativeWindowStorage& storage) noexcept
{
    return native_window.mToken == &storage && native_window.mModuleInstance == reinterpret_cast<void*>(storage.mModuleInstance) &&
           native_window.mWindow == reinterpret_cast<void*>(storage.mWindow) &&
           native_window.mOwnerThreadId == static_cast<U64>(storage.mOwnerThreadId);
}

NativeWindowRefreshCode refreshStorage(NativeWindowStorage& storage, LLWindowWin32VulkanNativeWindow& native_window) noexcept
{
    if (GetCurrentThreadId() != storage.mOwnerThreadId || !exactNativeIdentity(native_window, storage) || !IsWindow(storage.mWindow))
    {
        return NativeWindowRefreshCode::IdentityFailure;
    }

    DWORD process_id = 0;
    if (GetWindowThreadProcessId(storage.mWindow, &process_id) != storage.mOwnerThreadId || process_id != GetCurrentProcessId() ||
        reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(storage.mWindow, GWLP_HINSTANCE)) != storage.mModuleInstance ||
        static_cast<ATOM>(GetClassLongPtrW(storage.mWindow, GCW_ATOM)) != storage.mClassAtom)
    {
        return NativeWindowRefreshCode::IdentityFailure;
    }

    RECT client{};
    if (!GetClientRect(storage.mWindow, &client) || client.left != 0 || client.top != 0 || client.right <= 0 || client.bottom <= 0)
    {
        return NativeWindowRefreshCode::GeometryFailure;
    }

    native_window.mClientWidth  = static_cast<U32>(client.right);
    native_window.mClientHeight = static_cast<U32>(client.bottom);
    return NativeWindowRefreshCode::Success;
}

void destroyPartialNativeWindow(NativeWindowStorage& storage) noexcept
{
    if (storage.mWindow && !DestroyWindow(storage.mWindow))
    {
        std::terminate();
    }
    storage.mWindow = nullptr;

    if (storage.mClassAtom && !UnregisterClassW(MAKEINTRESOURCEW(storage.mClassAtom), storage.mModuleInstance))
    {
        std::terminate();
    }
    storage.mClassAtom = 0;
}

LLWindowWin32VulkanNativeCreateResult createNativeWindow(void*, const LLWindowWin32VulkanCreateInfo& info) noexcept
{
    if (info.mOwnerThreadId == 0 || info.mClientWidth == 0 || info.mClientHeight == 0 ||
        static_cast<U64>(GetCurrentThreadId()) != info.mOwnerThreadId)
    {
        return LLWindowWin32VulkanNativeCreateError{ LLWindowWin32VulkanNativeCreateCode::InvalidRequest };
    }

    auto storage = std::unique_ptr<NativeWindowStorage>(new (std::nothrow) NativeWindowStorage);
    if (!storage)
    {
        return LLWindowWin32VulkanNativeCreateError{ LLWindowWin32VulkanNativeCreateCode::StorageFailure };
    }

    storage->mModuleInstance = GetModuleHandleW(nullptr);
    storage->mOwnerThreadId  = GetCurrentThreadId();
    if (!storage->mModuleInstance)
    {
        return LLWindowWin32VulkanNativeCreateError{ LLWindowWin32VulkanNativeCreateCode::ModuleFailure };
    }

    const int class_name_length = std::swprintf(storage->mClassName.data(), storage->mClassName.size(), L"SecondLifeVulkan_%lu_%p",
                                                static_cast<unsigned long>(storage->mOwnerThreadId), static_cast<void*>(storage.get()));
    if (class_name_length <= 0 || static_cast<std::size_t>(class_name_length) >= storage->mClassName.size())
    {
        return LLWindowWin32VulkanNativeCreateError{ LLWindowWin32VulkanNativeCreateCode::StorageFailure };
    }

    WNDCLASSEXW window_class{};
    window_class.cbSize        = sizeof(window_class);
    window_class.lpfnWndProc   = diagnosticWindowProc;
    window_class.hInstance     = storage->mModuleInstance;
    window_class.lpszClassName = storage->mClassName.data();
    storage->mClassAtom        = RegisterClassExW(&window_class);
    if (!storage->mClassAtom)
    {
        return LLWindowWin32VulkanNativeCreateError{ LLWindowWin32VulkanNativeCreateCode::ClassRegistrationFailure };
    }

    storage->mWindow = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, MAKEINTRESOURCEW(storage->mClassAtom), WINDOW_TITLE, WS_POPUP,
                                       0, 0, static_cast<int>(info.mClientWidth), static_cast<int>(info.mClientHeight), nullptr, nullptr,
                                       storage->mModuleInstance, nullptr);
    if (!storage->mWindow)
    {
        destroyPartialNativeWindow(*storage);
        return LLWindowWin32VulkanNativeCreateError{ LLWindowWin32VulkanNativeCreateCode::WindowFailure };
    }

    LLWindowWin32VulkanNativeWindow native_window{ storage.get(),
                                                   reinterpret_cast<void*>(storage->mModuleInstance),
                                                   reinterpret_cast<void*>(storage->mWindow),
                                                   static_cast<U64>(storage->mOwnerThreadId),
                                                   0,
                                                   0 };
    const NativeWindowRefreshCode   refresh_code = refreshStorage(*storage, native_window);
    if (refresh_code != NativeWindowRefreshCode::Success)
    {
        destroyPartialNativeWindow(*storage);
        const LLWindowWin32VulkanNativeCreateCode create_code = refresh_code == NativeWindowRefreshCode::IdentityFailure
                                                                    ? LLWindowWin32VulkanNativeCreateCode::IdentityFailure
                                                                    : LLWindowWin32VulkanNativeCreateCode::GeometryFailure;
        return LLWindowWin32VulkanNativeCreateError{ create_code };
    }
    if (native_window.mClientWidth != info.mClientWidth || native_window.mClientHeight != info.mClientHeight)
    {
        destroyPartialNativeWindow(*storage);
        return LLWindowWin32VulkanNativeCreateError{ LLWindowWin32VulkanNativeCreateCode::GeometryFailure };
    }

    storage.release();
    return native_window;
}

bool refreshNativeWindow(void*, LLWindowWin32VulkanNativeWindow& native_window) noexcept
{
    if (!native_window.mToken)
    {
        return false;
    }
    return refreshStorage(*static_cast<NativeWindowStorage*>(native_window.mToken), native_window) == NativeWindowRefreshCode::Success;
}

void destroyNativeWindow(void*, LLWindowWin32VulkanNativeWindow& native_window) noexcept
{
    if (!native_window.mToken)
    {
        std::terminate();
    }

    std::unique_ptr<NativeWindowStorage> storage(static_cast<NativeWindowStorage*>(native_window.mToken));
    if (GetCurrentThreadId() != storage->mOwnerThreadId || !exactNativeIdentity(native_window, *storage) ||
        static_cast<ATOM>(GetClassLongPtrW(storage->mWindow, GCW_ATOM)) != storage->mClassAtom)
    {
        storage.release();
        std::terminate();
    }
    destroyPartialNativeWindow(*storage);
    native_window = {};
}

LLRenderVulkan::VulkanSurfaceCreateOutcome createWin32Surface(void*,
                                                              LLWindowVulkanFunction       resolver,
                                                              void*                        module_instance,
                                                              void*                        window,
                                                              VkInstance                   instance,
                                                              const VkAllocationCallbacks* allocator,
                                                              VkSurfaceKHR*                surface) noexcept
{
    if (!resolver || !module_instance || !window || instance == VK_NULL_HANDLE || !surface)
    {
        return LLRenderVulkan::VulkanSurfacePlatformFailure{};
    }

    const auto get_instance_proc_addr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(resolver);
    const auto create_surface = reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(get_instance_proc_addr(instance, "vkCreateWin32SurfaceKHR"));
    if (!create_surface)
    {
        return LLRenderVulkan::VulkanSurfacePlatformFailure{};
    }

    VkWin32SurfaceCreateInfoKHR create_info{};
    create_info.sType     = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    create_info.hinstance = reinterpret_cast<HINSTANCE>(module_instance);
    create_info.hwnd      = reinterpret_cast<HWND>(window);
    return create_surface(instance, &create_info, allocator, surface);
}

} // namespace

const LLWindowWin32VulkanOperations& defaultLLWindowWin32VulkanOperations() noexcept
{
    static const LLWindowWin32VulkanOperations operations{ nullptr,           currentThreadId,    openLoader,          closeLoader,
                                                           getResolver,       createNativeWindow, refreshNativeWindow, destroyNativeWindow,
                                                           createWin32Surface };
    return operations;
}
