/**
 * @file llwindowmacosxvulkan-objc.mm
 * @brief Opaque Cocoa window bridge for the opt-in Vulkan WSI diagnostic.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2026, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#import "llwindowmacosxvulkan-objc.h"
#include "llwindowmacosx-objc.h"

#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>

#include <cmath>
#include <limits>
#include <new>

@interface LLVulkanMetalView : NSView
@property(nonatomic) BOOL applicationView;
- (void)updateDrawableGeometry;
@end

@implementation LLVulkanMetalView

- (void)updateDrawableGeometry
{
    CALayer* candidate = [self layer];
    if (![candidate isKindOfClass:[CAMetalLayer class]])
    {
        return;
    }

    CAMetalLayer* metal_layer = (CAMetalLayer*)candidate;
    NSWindow* window = [self window];
    const CGFloat scale = window ? [window backingScaleFactor] : 1.0;
    const NSRect backing_bounds = [self convertRectToBacking:[self bounds]];

    [metal_layer setContentsScale:scale];
    [metal_layer setDrawableSize:backing_bounds.size];
}

- (BOOL)acceptsFirstResponder { return self.applicationView; }

- (void)keyDown:(NSEvent*)event
{
    if (!self.applicationView) return;
    NSString* characters = [event charactersIgnoringModifiers];
    NativeKeyEventData data;
    data.mKeyEvent = NativeKeyEventData::KEYDOWN;
    data.mEventType = [event type];
    data.mEventModifiers = [event modifierFlags];
    data.mEventKeyCode = [event keyCode];
    data.mEventRepeat = [event isARepeat];
    data.mEventUnmodChars = characters.length ? [characters characterAtIndex:0] : 0;
    callKeyDown(&data, data.mEventKeyCode, data.mEventModifiers, data.mEventUnmodChars);
}

- (void)keyUp:(NSEvent*)event
{
    if (!self.applicationView) return;
    NativeKeyEventData data;
    data.mKeyEvent = NativeKeyEventData::KEYUP;
    data.mEventType = [event type];
    data.mEventModifiers = [event modifierFlags];
    data.mEventKeyCode = [event keyCode];
    callKeyUp(&data, data.mEventKeyCode, data.mEventModifiers);
}

- (void)windowHidden:(NSNotification*)notification { callWindowHide(); }
- (void)windowRestored:(NSNotification*)notification { callWindowUnhide(); }
- (void)dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [super dealloc];
}

- (void)viewDidMoveToWindow
{
    [super viewDidMoveToWindow];
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    if (self.applicationView && [self window])
    {
        [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(windowHidden:)
            name:NSWindowDidMiniaturizeNotification object:[self window]];
        [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(windowRestored:)
            name:NSWindowDidDeminiaturizeNotification object:[self window]];
    }
    [self updateDrawableGeometry];
}

- (void)viewDidChangeBackingProperties
{
    [super viewDidChangeBackingProperties];
    [self updateDrawableGeometry];
}

- (void)setFrameSize:(NSSize)new_size
{
    [super setFrameSize:new_size];
    [self updateDrawableGeometry];
    if (self.applicationView && [self window])
    {
        const NSSize backing = [self convertSizeToBacking:new_size];
        callResize(backing.width, backing.height);
    }
}

@end

namespace
{
struct NativeToken
{
    NSWindow* window;
    LLVulkanMetalView* view;
    CAMetalLayer* layer;
    bool borrowedWindow;
    NSView* previousView;
};

void clear_native(LLWindowMacOSXVulkanNative* native) noexcept
{
    if (!native)
    {
        return;
    }

    native->token = nullptr;
    native->window = nullptr;
    native->view = nullptr;
    native->layer = nullptr;
    native->contents_scale = 0.0;
    native->drawable_width = 0;
    native->drawable_height = 0;
}

bool checked_dimension(CGFloat value, uint32_t& dimension) noexcept
{
    if (!std::isfinite(static_cast<double>(value)) || value < 0.0 ||
        value > static_cast<CGFloat>(std::numeric_limits<uint32_t>::max()))
    {
        return false;
    }

    const double rounded = std::floor(static_cast<double>(value) + 0.5);
    if (rounded < 0.0 || rounded > std::numeric_limits<uint32_t>::max())
    {
        return false;
    }

    dimension = static_cast<uint32_t>(rounded);
    return true;
}

LLWindowMacOSXVulkanStatus describe_native(
    NativeToken* token,
    LLWindowMacOSXVulkanNative* native) noexcept
{
    if (!token || !native || !token->window || !token->view || !token->layer)
    {
        return LLWINDOWMACOSXVULKAN_STATUS_INVALID_ARGUMENT;
    }

    @try
    {
        if ([token->window contentView] != token->view || [token->view window] != token->window)
        {
            return LLWINDOWMACOSXVULKAN_STATUS_VIEW_FAILED;
        }
        if ([token->view layer] != token->layer)
        {
            return LLWINDOWMACOSXVULKAN_STATUS_LAYER_FAILED;
        }

        [token->view updateDrawableGeometry];

        const CGFloat scale = [token->layer contentsScale];
        const CGSize drawable_size = [token->window isMiniaturized] ? CGSizeZero : [token->layer drawableSize];
        uint32_t drawable_width = 0;
        uint32_t drawable_height = 0;
        if (!std::isfinite(static_cast<double>(scale)) || scale <= 0.0 ||
            !checked_dimension(drawable_size.width, drawable_width) ||
            !checked_dimension(drawable_size.height, drawable_height))
        {
            return LLWINDOWMACOSXVULKAN_STATUS_GEOMETRY_FAILED;
        }

        native->token = static_cast<void*>(token);
        native->window = (void*)token->window;
        native->view = (void*)token->view;
        native->layer = (void*)token->layer;
        native->contents_scale = static_cast<double>(scale);
        native->drawable_width = drawable_width;
        native->drawable_height = drawable_height;
        return drawable_width == 0 || drawable_height == 0 ? LLWINDOWMACOSXVULKAN_STATUS_DRAWABLE_UNAVAILABLE
                                                           : LLWINDOWMACOSXVULKAN_STATUS_SUCCESS;
    }
    @catch (NSException*)
    {
        return LLWINDOWMACOSXVULKAN_STATUS_GEOMETRY_FAILED;
    }
}

bool destroy_token(NativeToken* token) noexcept
{
    if (!token)
    {
        return true;
    }

    bool clean = true;
    if (token->window)
    {
        @try
        {
            token->view.applicationView = NO;
            [[NSNotificationCenter defaultCenter] removeObserver:token->view];
            if (!token->borrowedWindow) [token->window orderOut:nil];
            if ([token->window contentView] == token->view)
            {
                [token->window setContentView:token->previousView];
            }
        }
        @catch (NSException*)
        {
            clean = false;
        }
    }

    if (token->view)
    {
        @try
        {
            if ([token->view layer] == token->layer)
            {
                [token->view setLayer:nil];
            }
            [token->view setWantsLayer:NO];
        }
        @catch (NSException*)
        {
            clean = false;
        }
    }

    if (token->layer)
    {
        @try
        {
            [token->layer release];
        }
        @catch (NSException*)
        {
            clean = false;
        }
        token->layer = nil;
    }

    if (token->view)
    {
        @try
        {
            [token->view release];
        }
        @catch (NSException*)
        {
            clean = false;
        }
        token->view = nil;
    }

    if (token->window)
    {
        @try
        {
            if (!token->borrowedWindow) [token->window close];
        }
        @catch (NSException*)
        {
            clean = false;
        }
        @try
        {
            if (!token->borrowedWindow) [token->window release];
        }
        @catch (NSException*)
        {
            clean = false;
        }
        token->window = nil;
    }

    [token->previousView release];
    delete token;
    return clean;
}

LLWindowMacOSXVulkanStatus create_native(
    uint32_t backing_width,
    uint32_t backing_height,
    LLWindowMacOSXVulkanNative* out_native, NSWindow* application_window = nil) noexcept
{
    if (!out_native || backing_width == 0 || backing_height == 0)
    {
        return LLWINDOWMACOSXVULKAN_STATUS_INVALID_ARGUMENT;
    }
    clear_native(out_native);

    @try
    {
        if (![NSThread isMainThread])
        {
            return LLWINDOWMACOSXVULKAN_STATUS_MAIN_THREAD_REQUIRED;
        }
    }
    @catch (NSException*)
    {
        return LLWINDOWMACOSXVULKAN_STATUS_APPLICATION_FAILED;
    }

    NativeToken* token = nullptr;
    LLWindowMacOSXVulkanStatus failure = LLWINDOWMACOSXVULKAN_STATUS_APPLICATION_FAILED;

    @autoreleasepool
    {
        @try
        {
            if (![NSApplication sharedApplication])
            {
                return LLWINDOWMACOSXVULKAN_STATUS_APPLICATION_FAILED;
            }

            failure = LLWINDOWMACOSXVULKAN_STATUS_STORAGE_FAILED;
            token = new (std::nothrow) NativeToken{};
            if (!token)
            {
                return failure;
            }
            token->window = nil;
            token->view = nil;
            token->layer = nil;

            failure = LLWINDOWMACOSXVULKAN_STATUS_WINDOW_FAILED;
            token->borrowedWindow = application_window != nil;
            token->previousView = application_window ? [[application_window contentView] retain] : nil;
            token->window = application_window ? application_window : [[NSWindow alloc]
                initWithContentRect:NSMakeRect(0.0, 0.0, 1.0, 1.0)
                          styleMask:NSWindowStyleMaskBorderless
                            backing:NSBackingStoreBuffered
                              defer:NO];
            if (!token->window)
            {
                destroy_token(token);
                return failure;
            }
            if (!token->borrowedWindow) [token->window setReleasedWhenClosed:NO];

            failure = LLWINDOWMACOSXVULKAN_STATUS_VIEW_FAILED;
            token->view = [[LLVulkanMetalView alloc] initWithFrame:NSMakeRect(0.0, 0.0, 1.0, 1.0)];
            if (!token->view)
            {
                destroy_token(token);
                return failure;
            }
            [token->view setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
            token->view.applicationView = token->borrowedWindow;

            failure = LLWINDOWMACOSXVULKAN_STATUS_LAYER_FAILED;
            token->layer = [[CAMetalLayer alloc] init];
            if (!token->layer)
            {
                destroy_token(token);
                return failure;
            }
            [token->view setWantsLayer:YES];
            [token->view setLayer:token->layer];
            if ([token->view layer] != token->layer)
            {
                destroy_token(token);
                return failure;
            }

            failure = LLWINDOWMACOSXVULKAN_STATUS_VIEW_FAILED;
            [token->window setContentView:token->view];
            if ([token->window contentView] != token->view || [token->view window] != token->window)
            {
                destroy_token(token);
                return failure;
            }

            failure = LLWINDOWMACOSXVULKAN_STATUS_GEOMETRY_FAILED;
            const NSRect requested_backing = NSMakeRect(
                0.0,
                0.0,
                static_cast<CGFloat>(backing_width),
                static_cast<CGFloat>(backing_height));
            const NSRect requested_points = [token->view convertRectFromBacking:requested_backing];
            if (!std::isfinite(static_cast<double>(requested_points.size.width)) ||
                !std::isfinite(static_cast<double>(requested_points.size.height)) ||
                requested_points.size.width <= 0.0 || requested_points.size.height <= 0.0)
            {
                destroy_token(token);
                return failure;
            }

            [token->window setContentSize:requested_points.size];
            [token->view setFrame:NSMakeRect(
                0.0,
                0.0,
                requested_points.size.width,
                requested_points.size.height)];

            const LLWindowMacOSXVulkanStatus described = describe_native(token, out_native);
            if (described != LLWINDOWMACOSXVULKAN_STATUS_SUCCESS ||
                out_native->drawable_width != backing_width ||
                out_native->drawable_height != backing_height)
            {
                clear_native(out_native);
                destroy_token(token);
                return described == LLWINDOWMACOSXVULKAN_STATUS_SUCCESS ? failure : described;
            }

            return LLWINDOWMACOSXVULKAN_STATUS_SUCCESS;
        }
        @catch (NSException*)
        {
            clear_native(out_native);
            destroy_token(token);
            return failure;
        }
    }
}

LLWindowMacOSXVulkanStatus resize_native_for_diagnostic(
    uint32_t backing_width,
    uint32_t backing_height,
    LLWindowMacOSXVulkanNative* native) noexcept
{
    if (!native || !native->token)
    {
        return LLWINDOWMACOSXVULKAN_STATUS_INVALID_ARGUMENT;
    }

    @try
    {
        if (![NSThread isMainThread])
        {
            return LLWINDOWMACOSXVULKAN_STATUS_MAIN_THREAD_REQUIRED;
        }

        NativeToken* token = static_cast<NativeToken*>(native->token);
        if (!token || native->window != (void*)token->window || native->view != (void*)token->view ||
            native->layer != (void*)token->layer)
        {
            return LLWINDOWMACOSXVULKAN_STATUS_INVALID_ARGUMENT;
        }

        @autoreleasepool
        {
            const NSRect requested_backing = NSMakeRect(
                0.0,
                0.0,
                static_cast<CGFloat>(backing_width),
                static_cast<CGFloat>(backing_height));
            const NSRect requested_points = [token->view convertRectFromBacking:requested_backing];
            if (!std::isfinite(static_cast<double>(requested_points.size.width)) ||
                !std::isfinite(static_cast<double>(requested_points.size.height)) ||
                requested_points.size.width < 0.0 || requested_points.size.height < 0.0)
            {
                return LLWINDOWMACOSXVULKAN_STATUS_GEOMETRY_FAILED;
            }

            [token->window setContentSize:requested_points.size];
            [token->view setFrame:NSMakeRect(
                0.0,
                0.0,
                requested_points.size.width,
                requested_points.size.height)];

            const LLWindowMacOSXVulkanStatus described = describe_native(token, native);
            if ((described != LLWINDOWMACOSXVULKAN_STATUS_SUCCESS &&
                 described != LLWINDOWMACOSXVULKAN_STATUS_DRAWABLE_UNAVAILABLE) ||
                native->drawable_width != backing_width || native->drawable_height != backing_height)
            {
                return described == LLWINDOWMACOSXVULKAN_STATUS_SUCCESS ||
                               described == LLWINDOWMACOSXVULKAN_STATUS_DRAWABLE_UNAVAILABLE
                           ? LLWINDOWMACOSXVULKAN_STATUS_GEOMETRY_FAILED
                           : described;
            }
            return described;
        }
    }
    @catch (NSException*)
    {
        return LLWINDOWMACOSXVULKAN_STATUS_GEOMETRY_FAILED;
    }
}
} // namespace

LLWindowMacOSXVulkanStatus llwindow_macosx_vulkan_native_create(
    uint32_t backing_width,
    uint32_t backing_height,
    LLWindowMacOSXVulkanNative* out_native) noexcept
{
    try
    {
        return create_native(backing_width, backing_height, out_native);
    }
    catch (...)
    {
        clear_native(out_native);
        return LLWINDOWMACOSXVULKAN_STATUS_APPLICATION_FAILED;
    }
}

LLWindowMacOSXVulkanStatus llwindow_macosx_vulkan_native_attach(
    void* window, uint32_t width, uint32_t height, LLWindowMacOSXVulkanNative* native) noexcept
{
    if (!window) return LLWINDOWMACOSXVULKAN_STATUS_INVALID_ARGUMENT;
    try { return create_native(width, height, native, (NSWindow*)window); }
    catch (...) { clear_native(native); return LLWINDOWMACOSXVULKAN_STATUS_APPLICATION_FAILED; }
}

LLWindowMacOSXVulkanStatus llwindow_macosx_vulkan_native_refresh(
    LLWindowMacOSXVulkanNative* native) noexcept
{
    if (!native || !native->token)
    {
        return LLWINDOWMACOSXVULKAN_STATUS_INVALID_ARGUMENT;
    }
    try
    {
        @try
        {
            if (![NSThread isMainThread])
            {
                return LLWINDOWMACOSXVULKAN_STATUS_MAIN_THREAD_REQUIRED;
            }

            @autoreleasepool
            {
                return describe_native(static_cast<NativeToken*>(native->token), native);
            }
        }
        @catch (NSException*)
        {
            return LLWINDOWMACOSXVULKAN_STATUS_GEOMETRY_FAILED;
        }
    }
    catch (...)
    {
        return LLWINDOWMACOSXVULKAN_STATUS_GEOMETRY_FAILED;
    }
}

LLWindowMacOSXVulkanStatus llwindow_macosx_vulkan_native_resize_for_diagnostic(
    uint32_t backing_width,
    uint32_t backing_height,
    LLWindowMacOSXVulkanNative* native) noexcept
{
    try
    {
        return resize_native_for_diagnostic(backing_width, backing_height, native);
    }
    catch (...)
    {
        return LLWINDOWMACOSXVULKAN_STATUS_GEOMETRY_FAILED;
    }
}

LLWindowMacOSXVulkanStatus llwindow_macosx_vulkan_native_destroy(
    LLWindowMacOSXVulkanNative* native) noexcept
{
    if (!native)
    {
        return LLWINDOWMACOSXVULKAN_STATUS_INVALID_ARGUMENT;
    }
    if (!native->token)
    {
        clear_native(native);
        return LLWINDOWMACOSXVULKAN_STATUS_SUCCESS;
    }
    try
    {
        @try
        {
            if (![NSThread isMainThread])
            {
                return LLWINDOWMACOSXVULKAN_STATUS_MAIN_THREAD_REQUIRED;
            }

            @autoreleasepool
            {
                NativeToken* token = static_cast<NativeToken*>(native->token);
                clear_native(native);
                return destroy_token(token) ? LLWINDOWMACOSXVULKAN_STATUS_SUCCESS
                                            : LLWINDOWMACOSXVULKAN_STATUS_DESTROY_FAILED;
            }
        }
        @catch (NSException*)
        {
            clear_native(native);
            return LLWINDOWMACOSXVULKAN_STATUS_DESTROY_FAILED;
        }
    }
    catch (...)
    {
        clear_native(native);
        return LLWINDOWMACOSXVULKAN_STATUS_DESTROY_FAILED;
    }
}
