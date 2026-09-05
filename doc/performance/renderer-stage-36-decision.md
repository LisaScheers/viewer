# Stage 36 Win32 Vulkan surface ownership decision

## Decision

Retain an isolated, default-off Win32 Vulkan surface producer as a test-only
diagnostic. Do not connect it to the production viewer window factory and do
not treat it as an admitted Windows runtime path until the focused native test
passes on supported Windows with the Khronos validation layer enabled.

The producer closes the next platform ownership gap after the neutral surface
parent and the Linux and macOS producers. It implements the intended Win32
resource shape without changing the current WGL window, OpenGL manager, device
path, or frame path.

## Owned generation

`LLWindowWin32Vulkan` owns one creator-thread-affine chain:

1. one explicitly loaded Vulkan loader module;
2. one uniquely registered Unicode window class;
3. one hidden non-WGL `HWND` created from the borrowed process `HINSTANCE`;
4. one immutable Vulkan requirements record for the exact native generation;
5. at most one Vulkan 1.1 instance generation with optional validation;
6. at most one Win32 `VkSurfaceKHR` child retained by that exact instance.

The required instance extensions are exactly, and in this order:

1. `VK_KHR_surface`;
2. `VK_KHR_win32_surface`.

Portability enumeration remains disabled on Windows. For native-window state,
the owner exposes only scalar client extent and thread observations. Native
pointer identities stay inside the operation seam and are forwarded to surface
creation only after the retained identity checks succeed.

## Native Win32 boundary

The default operation table:

- obtains the process module with `GetModuleHandleW(nullptr)` and never frees
  that borrowed handle;
- loads an explicit path with `LoadLibraryExW` using only the supplied DLL
  directory and System32, or searches System32 only for `vulkan-1.dll` when
  no path is supplied;
- resolves `vkGetInstanceProcAddr` from that exact module and resolves
  `vkCreateWin32SurfaceKHR` through that exact instance resolver;
- registers a unique Unicode class and retains its returned atom;
- creates one hidden `WS_POPUP` window with `WS_EX_NOACTIVATE` and
  `WS_EX_TOOLWINDOW`;
- never requests a device context, pixel format, WGL context, focus, visible
  window, or DPI-mode change;
- verifies the exact owner thread, process, `HINSTANCE`, `HWND`, class atom,
  and positive client-coordinate extent before publishing or refreshing the
  generation.

The requested extent is rejected before loader acquisition if either
dimension is zero or cannot be represented by the native signed Win32 extent.
Post-create identity and geometry failures remain separately typed.

## Lifetime and failure contract

Acquisition is transactional. A failure closes the loader and destroys any
registered class and native window that already exist. Requirements allocation
failure cannot leak native state.

Full teardown is strictly:

1. surface;
2. validation messenger;
3. instance;
4. requirements;
5. `HWND`;
6. class registration;
7. loader.

The surface child remains owned by the Stage 34 instance contract. External
global-dispatch observations in the native test end before the loader closes.
Explicit surface reset leaves the instance, validation state, requirements,
native identities, and loader live.

An off-thread surface reset or full reset returns false and releases nothing.
The native smoke also performs this check from an actual second operating
system thread. Destroying or replacing a still-owning object from the wrong
thread is a contract violation and terminates instead of leaking a partially
released Win32 generation.

## Build graph and production isolation

`LL_VULKAN_WIN32_WSI` is a default-off cache option. Configuration rejects it
unless the target platform is Windows and both `LL_TESTS` and
`LL_VULKAN_RUNTIME_TEST` are enabled.

When enabled, the standalone `llwindowwin32vulkan` diagnostic links one way to
`llwindow`, `llrendervulkaninstance`, and private `user32`. The generic owner,
native operation file, injected test, and native test all use
`VK_NO_PROTOTYPES`; only the native operation file enables the Win32 Vulkan
platform declarations. There is no Vulkan import-library edge.

No new source enters the production `llwindow` source list, viewer target,
factory, installer, or package dependency graph. Production `LLWindowWin32`,
its WGL and device-context ownership, and all existing callers are unchanged.

## Focused evidence boundary

The injected suite contains 12 cases. It covers invalid operation tables and
requests, owner-thread preflight, loader and resolver failure, every typed
native creation failure, poisoned identities and geometry, generation and
allocation failures, exact extension order, instance and surface acquisition,
exact resolver and native identity forwarding, platform and Vulkan failures,
poisoned and null surface outcomes, duplicate ownership, refresh poisoning and
recovery, surface-only reset and reacquisition, move construction, move
assignment, idempotent reset, off-thread full-chain retention, and exact
reverse teardown.

The separately opted-in native route requires an explicit UTF-8 loader path.
On supported Windows it creates one hidden 1280 by 720 client generation on
the current thread, requires Vulkan 1.1 and validation, creates one real
non-null Win32 surface, performs actual off-thread reset rejection, explicitly
resets the surface on the creator thread, requires zero validation messages,
and completes owned teardown. It also requires unchanged current WGL context,
current WGL device context, keyboard focus, `LLWindow` instance count, and
OpenGL manager state.

The focused cross compilation described below proves header, type, warning,
and owner-core link behavior only. It does not substitute for supported
Windows execution, the native window manager, or the Khronos validation layer.

## Validation results

### Focused Windows cross-build evidence

Warnings-as-errors MinGW syntax checks pass for the generic owner, native Win32
operation implementation, and injected suite. A focused x86_64 PE link of the
exact generic owner, native operations, reusable instance, requirements, and
global-dispatch cores plus a minimal consumer succeeds. Its import table
contains only core Windows libraries and no `vulkan-1.dll`, `opengl32.dll`, direct
`vkGetInstanceProcAddr`, or direct `vkCreateWin32SurfaceKHR` import.

The injected suite also passes all 12 cases as a Linux-host executable. That
executable links the real generic owner and reusable Vulkan cores while a
temporary non-Win32 default-operation shim satisfies the test-only completeness
check; all lifecycle cases use their injected operation table. This is portable
owner-logic evidence, not compilation or execution of the native Win32 route.

The native integration-test translation unit and the two real CMake integration
executables were not cross-built. The available dependency headers are for the
Linux viewer graph, while the complete Windows graph requires the project's
supported MSVC and prebuilt-library environment. Their compile, link, and run
status remains part of the native Windows gate rather than a cross-build claim.

### Native Windows gate

No supported Windows runner with the Khronos validation layer is available in
this stage. Wine is not accepted as authoritative evidence because the
available environment does not expose the required validation layer. No
native result is retained or claimed.

The default-off implementation can therefore be committed, but admission of
the Win32 producer and any dependent physical-device work remain blocked on
one supported Windows run of both focused tests, including the explicit native
route with required validation.

### Linux regression and isolation

The existing warnings-as-errors Linux graph keeps the benchmark, runtime,
tonemap, and SDL WSI options enabled while both platform-specific macOS and
Win32 diagnostics remain off. The six established global-dispatch, instance,
tonemap-registry, neutral requirements, injected SDL owner, and native SDL WSI
routes pass. The Win32 diagnostic targets and object edges are absent from that
graph, and requesting the option on Linux fails with the intended platform
diagnostic.

Linux warnings-as-errors syntax checks also pass for the platform-neutral
Win32 owner and injected suite. The 12 shader-reflection, eight artifact
delivery, and 57 benchmark-harness Python tests pass. The harness tests execute
Python logic only; no benchmark or timing path runs.

The enabled Linux build reproduces the canonical production shaders:

- vertex: 8,068 bytes,
  `b5750a179572b2fc3545c7472a28cac963351f7e7bc44df38190bb8961894095`;
- fragment: 7,824 bytes,
  `850d765cb1a6cd31dfbe16f94cedd89ab8306eca771cf833fe47c441b9c743fb`.

### macOS disabled isolation

A fresh universal ReleaseOS configuration with all six experimental renderer
options disabled contains no Win32 diagnostic target or test. The viewer and
package targets build with warnings as errors for `arm64` and `x86_64`. The
resulting viewer reports both architectures.

The first disposable attempts exposed missing local build-variable inputs,
not source defects. Supplying the established deployment target and platform
defines allowed the same clean source snapshot to build. The final checker then
rejected the valid `x86_64 arm64` architecture order before its dependency and
import scan, so Stage 36 makes no fresh binary-scan claim. The generated graph
still proves that the default-off Win32 target adds no macOS dependency edge.
All disposable source, build, package, tool-environment, and log state was
removed afterward.

## Source identities

The ordered digest of the seven Stage 36 build, implementation, and test
SHA-256 records is below. The decision note is excluded because including its
own hash would be self-referential.
`b982367963eeca0d16e605f9bf41875d87e022eef29cee27ab31e514fc2f72b1`.
The individual identities are:

- `indra/cmake/Variables.cmake`:
  `1d97eda13e8549a2230657b872f36d571e06b1bb50dad908bcbfcbb6971f5e73`;
- `indra/llwindow/CMakeLists.txt`:
  `420991de1e3c17c7894edda5bc59875fd409f8bf43ba8668a283b90f9cc3467d`;
- `indra/llwindow/llwindowwin32vulkan.h`:
  `998a269293db88625c4886ca0d2dbd8bff3d24cc84747cef60d282ba9a370159`;
- `indra/llwindow/llwindowwin32vulkan.cpp`:
  `4c6702fb967fe0b5fe0b98d5fac7b3756062d792bbb5433787b2604a9bab6902`;
- `indra/llwindow/llwindowwin32vulkan-native.cpp`:
  `09955e8fe87cb0834a74c27a18d57057edc878f843a2e9b330babdd14a18cc3e`;
- `indra/llwindow/tests/llwindowwin32vulkan_test.cpp`:
  `d3cb3f37954f22cca39b6455d83d65a8c171c21ce5e3ada98e81bb3a95122d2b`;
- `indra/llwindow/tests/llwindowvulkanwin32wsi_test.cpp`:
  `46d8a68a7b5620e8b685ed9114e06e0b6f296f6b42ddff399cc404ef008c2e31`.

## Explicit deferrals

- supported Windows execution of both focused tests with required Khronos
  validation;
- connection to the production Win32 window factory or existing WGL window;
- physical-device and presentation-queue selection;
- logical device, swapchain, synchronization, and complete frame execution;
- replacement of `gGLManager` or any OpenGL production path;
- login, region, image-parity, benchmark, timing, or performance claims.

## Next dependent decision

Stage 37 must be a native Windows validation and admission gate for this exact
commit. It may correct Stage 36 defects found by that run, but it must not add a
physical device, queue, swapchain, production selector, or frame path. Device
ownership planning resumes only after the native owner, real surface, required
validation, creator-thread teardown, and default-off production isolation all
pass on supported Windows.
