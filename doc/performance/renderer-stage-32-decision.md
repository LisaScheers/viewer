# Stage 32 Linux SDL Vulkan instance requirements

## Decision

Add the first real Vulkan-capable native window behind the default-off
`LL_VULKAN_SDL_WSI` migration option. The supported producer is the Linux SDL
window path. It creates an SDL window with the Vulkan property, retains the
loader needed by that window, and publishes the exact instance prerequisites
reported by SDL.

The two production factory callers remain unchanged after Stage 31: the viewer
selects OpenGL unless it is headless, and the appearance utility selects
OpenGL. Stage 32 adds no setting or command-line route to Vulkan. macOS,
Windows, OSMesa, and builds with the option disabled continue to reject a
Vulkan selection before platform initialization.

This is a native-window and instance-requirements step. It creates no Vulkan
instance, debug messenger, surface, physical or logical device, queue,
swapchain, command buffer, or frame.

## Owned requirements generation

`LLWindowVulkanRequirements` is independent of SDL and Vulkan headers. One
successful value contains:

- the opaque function pointer returned for `vkGetInstanceProcAddr`;
- a deep-copied, order-preserving vector of the required instance-extension
  names; and
- a nonzero native-window generation.

The value is noncopyable and is exposed only through a nullable const pointer
from the originating `LLWindow`. It owns no loader, SDL window, or Vulkan
object. A consumer may use the resolver only while the window-owned generation
remains current.

Construction is atomic and bounded. It rejects a missing resolver, zero or
excessive extension count, missing list, null or empty name, a name without a
terminator inside Vulkan's 256-byte extension-name bound, an exact duplicate,
a zero window generation, and allocation failure. No partial list escapes.
Extension names are neither inferred nor normalized. In particular, build
flags and environment variables do not guess whether X11 or Wayland
extensions are needed.

Typed acquisition errors begin only after the enabled SDL producer is chosen.
The legacy public factory still reports an unsupported platform, disabled
option, or unknown graphics API as a null result plus a diagnostic because its
return type is `LLWindow*`. Replacing that established factory result is not
required to make a successful window's prerequisites lifetime-safe.

## SDL loader and window lifetime

SDL 3.2.24-r1 is pinned by the repository. Its documented Vulkan lifecycle has
two distinct references in this implementation:

1. `SDL_Vulkan_LoadLibrary(nullptr)` acquires one explicit reference after SDL
   video initialization.
2. Creating a window with `SDL_PROP_WINDOW_CREATE_VULKAN_BOOLEAN` acquires the
   window's implicit reference.

The move-only `LLWindowSDLVulkan` owner validates the actual SDL window flags,
then asks SDL for the exact resolver and extension sequence. A successful
owner publishes requirements only after every check passes.

Reset and rollback reverse that order. Requirements are invalidated first,
the SDL window is destroyed next so SDL releases its implicit reference, and
the explicit reference is unloaded last. Only then may the manager call
`SDL_Quit()`. Failed loader, window, flag, resolver, extension-query, or
requirements construction publishes nothing and performs only the cleanup
earned by that path.

The pinned SDL implementation can increment its implicit loader count before
allocating the `SDL_Window`. If that internal allocation fails, the helper
cannot know whether the implicit reference was acquired and therefore must not
guess with an extra unload. The manager's immediate `SDL_Quit()` is the final
cleanup for that failed-construction edge.

The injected operation table used by the focused test exercises these rules
without loading SDL video or a Vulkan loader. Production operations remain the
only code that calls the real SDL Vulkan API.

## Disjoint OpenGL and Vulkan construction

`LLWindowSDL` now retains the selected graphics API. OpenGL follows its
existing context setup. Vulkan enters a separate function that:

- creates a resizable, initially hidden, high-pixel-density-capable SDL Vulkan
  window;
- initializes the backend-neutral SDL keyboard and text-input path only after
  successful acquisition; and
- stores the window-owned requirements generation.

The Vulkan branch does not call `SDL_GL_LoadLibrary`, set an OpenGL window
property, create or bind an SDL GL context, initialize `gGLManager`, enter GLX
or EGL setup, query GL attributes, initialize the legacy custom cursor set,
toggle an OpenGL swap interval, or swap an OpenGL buffer. Context recreation,
shared OpenGL contexts, GL context binding, VSync mutation, and buffer swap all
fail closed for a Vulkan window.

SDL's window and keyboard implementation are process-global and already state
that only one `LLWindowSDL` object may exist. The manager now enforces one live
window on the SDL path before initialization. Other platform managers retain
their existing behavior, while all platforms still reject simultaneous live
OpenGL and Vulkan rendering windows.

The existing manager also assumes sole ownership of SDL initialization. Its
global `SDL_Quit()` teardown is not a composable lease for unrelated code that
initialized SDL first. Stage 32 verifies exact restoration from the viewer's
supported initially-uninitialized process state; subsystem-level shared
ownership would require a separate lifecycle refactor.

Headless selection does not initialize or quit SDL. `LLWindow` records it as
`GraphicsAPI::Headless`, while platform and OSMesa windows retain
`GraphicsAPI::OpenGL` unless the enabled SDL producer receives Vulkan.

## Manager teardown correction

The former SDL manager order was `close`, erase, `SDL_Quit`, then C++ delete.
The `LLWindowSDL` destructor calls SDL cleanup, so that order allowed SDL calls
after global teardown. Invalid construction also returned without balancing a
successful SDL initialization.

Stage 32 changes the order to:

1. close the window;
2. erase it from manager tracking;
3. delete the C++ window, completing SDL window and loader cleanup; and
4. call `SDL_Quit()` and restore the prior SDL log callback.

Failed construction deletes the candidate before quitting SDL. `init_sdl()`
now reports required-subsystem failure, and the manager rolls back every SDL
subsystem initialized by that attempt. Close and destructor paths are
idempotent.

## Focused and native evidence boundary

Pure tests cover the requirements value's traits, every typed validation
failure, deep-copy behavior, extension order and case, explicit bounds,
allocation failure, and move into window ownership.

Injected SDL tests cover the exact success and rollback order, both loader
references, typed acquisition failures, actual Vulkan-only window flags,
resolver and extension publication, move ownership, idempotent reset,
invalidation before window destruction, and generation freshness even when a
fake native address is reused. They invoke no real SDL function.

The native smoke is separately opt-in. In a disposable X11 display with a
known software Vulkan ICD, it creates the hidden window through the real
manager and requires:

- SDL's X11 video driver;
- a current generation with SDL's exact resolver and extension sequence;
- successful Stage 30 global-dispatch resolution through that resolver;
- no current GL context or initialized `gGLManager`;
- rejection of another live SDL/OpenGL window, native-window recreation,
  shared-context creation, VSync mutation, and buffer swap; and
- restoration of window tracking, SDL subsystems, log callback, GL context,
  and GL-manager state after destruction.

The smoke does not create a Vulkan instance or surface and makes no Wayland
claim. The SDL dummy and offscreen drivers are not treated as Vulkan WSI
evidence.

## Build and dependency boundary

The SDL Vulkan path requires Linux, `USE_SDL_WINDOW`, tests, and the Stage 30
runtime-dispatch target. The option is disabled by default. The window layer
uses SDL's dynamic Vulkan entry points and does not link directly to the Vulkan
loader. The viewer keeps its OpenGL selection and must remain free of a direct
Vulkan-loader dependency.

With the option off, the helper source and native SDL Vulkan tests are absent
from the target graph and Vulkan selection retains Stage 31's pre-initialization
rejection. The platform-neutral requirements contract remains available for
later Cocoa and Win32 producers.

## Explicit deferrals

- `VkInstance` creation, extension availability policy, and validation setup;
- `VkSurfaceKHR` creation, destruction, and generation-checked recreation;
- Wayland-native evidence;
- a retained Metal-backed view and `CAMetalLayer` on macOS;
- a non-WGL `HINSTANCE`/`HWND` generation on Windows;
- physical-device and presentation-queue selection;
- logical device, swapchain, synchronization, and frame execution;
- a viewer setting, command-line route, or replacement of `gGLManager`;
- custom SDL cursor population for a future interactive Vulkan viewer route;
- login, region, image parity, benchmark, timing, or performance claims.

## Outcome

Stage 32 is complete only after option-on and option-off Linux gates, the real
X11 software-Vulkan smoke, universal macOS isolation, cross-platform source
review, dependency and privacy scans, canonical shader checks, and exactly one
commit pass. Those results are recorded below before commit.

## Validation results

### Linux enabled path

The warnings-as-errors Release build passes for the viewer, appearance utility,
and all four focused window targets with the SDL WSI, runtime dispatch, and
tonemap diagnostics enabled. All 24 renderer routes and four window routes
pass. The latter cover the graphics-API factory, neutral requirements value,
injected SDL lifetime owner, and native SDL WSI boundary.

The separately opted-in native route passes in a disposable X11 display with
lavapipe. It creates the hidden Vulkan-only SDL window, receives SDL's exact
resolver and instance-extension sequence, resolves the Stage 30 global
dispatch, exercises fail-closed native-window recreation, shared-context
creation, VSync mutation, and buffer swap, and restores the initial manager,
SDL, log, GL-context, and GL-manager state. The guarded context-binding and
shared-context-destruction paths were source-reviewed. SDL's dynamically loaded
X11 dependency closure was supplied explicitly. No Vulkan instance or surface
was created.

The 12 shader-reflection, eight artifact-delivery, and 57 benchmark-harness
unit tests pass. The last suite exercised Python harness logic only; no
benchmark executable or timing path ran. The viewer, appearance utility, and
four focused window executables have neither a direct Vulkan-loader dependency
nor an undefined direct Vulkan entry point.

### Linux disabled isolation

With all three Vulkan diagnostic options disabled, a fresh warnings-as-errors
rebuild passes for the viewer, appearance utility, graphics-API test, and
neutral requirements test. The SDL owner, native WSI route, Stage 30 runtime
dispatch target, and tonemap runtime target are absent from the generated
graph. The platform-neutral requirements implementation deliberately remains.

Both remaining window routes pass. The two applications and two focused
executables have no Vulkan-loader dependency or direct Vulkan import. Vulkan
selection retains Stage 31's pre-initialization rejection.

### macOS isolation

A fresh, disposable macOS 26.6.2 and Xcode 26.6 universal ReleaseOS build and
package pass under warnings-as-errors with the SDL WSI, runtime dispatch,
tonemap, and benchmark options disabled. The generated project contains no SDL
Vulkan owner or native WSI test. The viewer contains arm64 and x86_64 slices;
the two focused tests follow the project's signed arm64 test policy.

Eleven option-independent renderer routes and both window routes pass. The 12
reflection, eight artifact-delivery, and 57 benchmark-harness tests also pass
without launching a benchmark. The application package contains no SPIR-V,
Vulkan loader, or MoltenVK payload. All 367 packaged Mach-O files are readable
and none depends on Vulkan or MoltenVK. The 17 Stage 32 source and build-wiring
inputs match the Linux candidate byte for byte. The disposable clone, build,
package, environment, and transfer archive were deleted after verification.

### Other platform boundary

No Windows compiler is available. Source and build-graph review confirms that
the Win32 and OSMesa factories retain their OpenGL construction, the common
requirements type contains no SDL or Vulkan header, and the SDL owner can enter
only the Linux option-on graph. The successful macOS build covers the Cocoa and
common option-off compilation paths. A native Windows build remains a later
gate before a Win32 Vulkan window can be claimed.

### Production artifact hashes

The enabled Linux build reproduces the canonical production shaders:

- vertex: 8,068 bytes,
  `b5750a179572b2fc3545c7472a28cac963351f7e7bc44df38190bb8961894095`;
- fragment: 7,824 bytes,
  `850d765cb1a6cd31dfbe16f94cedd89ab8306eca771cf833fe47c441b9c743fb`.

The ordered digest of the 17 Stage 32 source and build-wiring SHA-256 records
is `341d72d1c48ed462b9f9b475866322afa72a2ce0c4516aab0e7c2ff4592bddcd`.
Their individual identities are:

- `indra/cmake/Variables.cmake`:
  `fcc0bcdb77a40610cccb7f30f1d425b91796a17bae3fbc2be616f70633acb17f`;
- `indra/llwindow/CMakeLists.txt`:
  `d0ebc8be1715716e685c9369fdd76d8273914c4aef867d207cf9fefe2c18997c`;
- `indra/llwindow/llsdl.cpp`:
  `ec70dae8d0e9c8e72d4075d2855dfb7d3579a1883ab49067808fb03de29ec348`;
- `indra/llwindow/llsdl.h`:
  `cbd5b73e2f2be2515a6ba05fb878e05e1e1f8ba511554f7f4dec8390e8212917`;
- `indra/llwindow/llwindow.cpp`:
  `41f5a779448d305e4fb51676c15042b8d6b06ee31ecbfea7f38c1e3a9f89ce7d`;
- `indra/llwindow/llwindow.h`:
  `f18a4f518b13b59109bfeccb69dfe9ade9009979c3b137f048cac5954982143f`;
- `indra/llwindow/llwindowheadless.cpp`:
  `131e46001d805a7b0e13877c9d9a5f3ed1744e7adac2b198974d2acb4afd6251`;
- `indra/llwindow/llwindowsdl.cpp`:
  `f0c0e4531e83af31aab50e4d5476f1286778d7186b8cd21083520619a4cbffdd`;
- `indra/llwindow/llwindowsdl.h`:
  `d7fb35c71acc07ea94f8710452f072e67977366aa36cd5f06fd6673c1d21b0ec`;
- `indra/llwindow/llwindowsdlvulkan.cpp`:
  `855b58ae455fa646ae3ab54156bfc11cc27d9ffde3a6eee00cd1da461da23fe5`;
- `indra/llwindow/llwindowsdlvulkan.h`:
  `588673bb66af81f4319b082e730e854a2c28f7c4260ea55767b425c766a49605`;
- `indra/llwindow/llwindowvulkanrequirements.cpp`:
  `35afeff4a1c7ffcc88f2e311f3ccfb60ddae302bfc976f3047c9a7dc77efb940`;
- `indra/llwindow/llwindowvulkanrequirements.h`:
  `35372fdbe7cf8b56a359fd4084154c8d27657b8c35560fe0efac537917119eac`;
- `indra/llwindow/tests/llwindowgraphicsapi_test.cpp`:
  `1d2ff558a2e8fec48f055a5cf89c37c33ad161b322484ad7766150815214b279`;
- `indra/llwindow/tests/llwindowsdlvulkan_test.cpp`:
  `3bbecdbdeebc5b4f1deca199ff5a20f57eca85f6ee0a5011fcc0450a925ceeaf`;
- `indra/llwindow/tests/llwindowvulkanrequirements_test.cpp`:
  `1c66de35de46098445be1154411dc48458f548f0b85594b6094fb9bd77309424`;
- `indra/llwindow/tests/llwindowvulkansdlwsi_test.cpp`:
  `87d4d2019633d56c79d18f3786f32a62847430c71d404433deb31728f549a122`.

The decision note is excluded because recording these identities changes its
own hash.

### Outcome

Stage 32 is complete. The opt-in Linux SDL path now owns a real Vulkan-only
native window and its exact, generation-bound instance requirements without
altering either production caller or importing Vulkan into the window
contract. The next stage may consume this boundary to create an owned Vulkan
instance and SDL surface, but it must preserve the same default-off and
no-viewer-route limits.
