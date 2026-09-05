# Stage 34 parent-owned Vulkan surface generation

## Decision

Add one platform-neutral `VkSurfaceKHR` transaction as a private child of the
Stage 33 `VulkanInstanceGeneration`. The child cannot be copied, moved, or
returned independently. Its parent owns its storage, exposes only scalar
surface observations, and always destroys the surface before the validation
messenger and instance.

The Linux SDL adapter supplies the exact private `SDL_Window` only through one
synchronous creation thunk. It never owns a peer surface handle. This keeps
creation in the platform layer while destruction and lifetime remain in the
renderer-side parent that owns the Vulkan instance.

The two production callers still select OpenGL. This stage adds no viewer
setting, command-line route, login, physical device, presentation query,
logical device, queue, swapchain, image, command buffer, frame, benchmark,
timing, or performance claim.

## Parent-contained lifetime

The surface generation is an implementation-private child of
`VulkanInstanceGeneration`. It retains only:

- one non-null `VkSurfaceKHR`;
- the exact parent `VkInstance` used for creation;
- the resolved `vkDestroySurfaceKHR` command; and
- the originating nonzero native-window generation.

The parent exposes whether a child exists, its handle, and its native-window
generation. It provides explicit child reset so validation can observe surface
destruction before parent teardown. No caller receives a movable surface value
or a mutable child pointer.

Moving the instance parent transfers the private child allocation together
with the instance, authenticated global dispatch, validation state, and all
other Stage 33 state. The moved-from parent exposes neither an instance nor a
surface. Full parent reset first resets the surface child, then destroys the
debug messenger and instance.

This design makes a dead-parent surface call impossible through the public
ownership API. As with the Stage 33 synchronous request, callbacks must not
reentrantly reset, destroy, or concurrently mutate the parent during an
acquisition call.

## Surface request and failure model

`VulkanSurfaceRequest` carries only:

- the exact native-window generation expected by the parent;
- an instance-owner freshness callback bound to the exact parent object;
- a native-window freshness callback bound to the platform owner; and
- one opaque platform creation operation.

The creation operation receives the exact parent instance, a null
`VkAllocationCallbacks` pointer, and an output pointer. Its result is either a
platform-failure marker or a `VkResult`. This lets SDL report its boolean
failure without inventing a Vulkan result, while future native creators may
retain an exact Vulkan failure.

Typed acquisition errors distinguish:

- missing owner, window, or creation callbacks;
- a zero or mismatched generation;
- a reset or otherwise non-live instance parent;
- duplicate surface acquisition;
- a stale instance owner or native window;
- a parent that did not enable `VK_KHR_surface`;
- missing `vkDestroySurfaceKHR`;
- allocation failure;
- platform creation failure;
- Vulkan creation failure with its exact `VkResult`; and
- `VK_SUCCESS` with a null surface.

Failed platform or Vulkan results may leave a poisoned output value. Such an
output is ignored and never destroyed. Only `VK_SUCCESS` with a non-null
surface earns an ownership obligation.

## Acquisition and rollback order

The parent performs the transaction in this order:

1. validate all request callbacks and the nonzero requested generation;
2. require a complete live parent and no existing surface child;
3. require the requested generation to equal the parent's native generation;
4. check the exact instance owner, then the native window;
5. require `VK_KHR_surface` in the Stage 33 enabled-extension record;
6. repeat both freshness checks immediately before command resolution;
7. resolve `vkDestroySurfaceKHR` from the retained authenticated resolver and
   exact parent instance;
8. repeat both freshness checks after resolution;
9. allocate empty child storage before any platform object can exist;
10. repeat both freshness checks immediately before creation;
11. invoke the platform creator with null allocation callbacks;
12. classify platform failure, exact Vulkan failure, null success, or valid
    success;
13. arm rollback immediately for a valid non-null surface;
14. repeat both freshness checks after successful creation; and
15. publish the private child only when every check remains current.

The destroy command is therefore available before creation. A late-stale
success destroys the earned surface once and publishes no child. Unlike the
nonconforming Stage 33 case where a loader creates an instance but withholds
its mandatory destroy command, this surface transaction has no unrecoverable
created-object branch.

Child reset exchanges its state to empty before calling
`vkDestroySurfaceKHR(parent_instance, surface, nullptr)`. Repeated explicit
reset is a no-op. A later full parent reset cannot destroy the same surface
again.

## SDL installation

`LLWindowSDLVulkanOperations` gains one injectable surface callback with the
same instance, allocator, and output shape as SDL3 plus the exact private
window. The production callback asserts the SDL main thread and calls
`SDL_Vulkan_CreateSurface`. SDL `false` maps to the platform-failure marker;
SDL `true` maps to `VK_SUCCESS`.

`LLWindowSDLVulkan::acquireSurfaceGeneration()` requires its private window,
current immutable requirements, and exact Stage 33 instance parent. Its
instance-owner check verifies that the SDL owner still contains that same
parent object. Its window check verifies the exact requirements generation.
The stack-bound creator context forwards the private window and operation only
for the synchronous call.

Missing parents and duplicate acquisition invoke no SDL surface operation. A
failure leaves the live instance and its validation state intact so a caller
may retry. The existing hidden Vulkan diagnostic factory acquires the surface
after the instance and before it publishes the SDL owner or starts text input.
A failure lets the local owner unwind the instance, requirements, window, and
loader references.

The complete successful teardown order becomes:

1. Vulkan surface;
2. debug-utils messenger;
3. Vulkan instance;
4. immutable window requirements;
5. SDL Vulkan window and its implicit loader reference;
6. explicit SDL Vulkan loader reference; and
7. manager-owned SDL video state.

Explicit surface reset stops after step one. It leaves required validation,
the instance, requirements, window, and loader references live. Existing SDL
owner move construction and move assignment transfer the one instance parent,
so the surface child follows without another peer member or move rule.

## Focused evidence boundary

The renderer fake cases cover the private child and parent API, invalid
requests, reset parents, generation mismatch, missing `VK_KHR_surface`,
duplicate acquisition, exact command resolution, missing destroy dispatch,
platform and Vulkan failures, poisoned outputs, null success, exact
`VkResult`, every instance and window freshness boundary, forced allocation
failure, retry, explicit reset, parent move before and after acquisition,
idempotent full reset, and surface-first destruction.

The injected SDL cases cover the complete operation table, acquisition before
an instance, exact private-window and parent forwarding, null allocation
callbacks, missing destroy command before SDL creation, boolean failure with a
poisoned output, null success, duplicate acquisition, retry, explicit reset and
reacquisition, move construction, destination move assignment, failed factory
rollback, and the complete surface-to-loader teardown sequence.

The separately opted-in native route uses a disposable X11 display and a known
software Vulkan implementation. It requires a real hidden SDL Vulkan window,
required-validation instance, and non-null surface with one exact generation.
The test explicitly resets the surface while the validation messenger and
instance remain live, then requires a zero-message snapshot before parent and
window teardown. It creates no physical device and makes no Wayland claim.

## Build and dependency boundary

The generic surface code remains inside the existing optional instance target.
It depends on the Stage 33 retained dispatch and no SDL, native platform API,
or Vulkan loader library. The SDL creation adapter remains in the existing
Linux-only `LL_VULKAN_SDL_WSI` graph.

With every optional renderer migration switch disabled, the global-dispatch,
instance and surface, SDL owner, and native WSI targets remain absent. The
neutral graphics-API and Vulkan requirements contracts remain because typed
default-off selection still uses them.

## Explicit deferrals

- a retained Metal-backed view and `CAMetalLayer` producer on macOS;
- a non-WGL `HINSTANCE` and `HWND` generation on Windows;
- physical-device and presentation-queue selection;
- device-level portability-subset policy;
- logical device, queue, swapchain, synchronization, and complete frame
  execution;
- resize, minimize, swapchain recreation, readback, and device-loss behavior;
- a production viewer Vulkan route or replacement of `gGLManager`;
- login, region, image parity, capture, benchmark, timing, or performance
  claims.

The original master Stage 3 exit gate still requires a fixed scene through a
complete Vulkan frame on Linux, Windows, and MoltenVK. This Linux surface is a
foundation transaction only. Native macOS and Win32 producers must catch up
before device and swapchain work begins.

## Validation results

### Linux enabled path

The warnings-as-errors Release build passes for the viewer, appearance
utility, reusable instance and surface target, injected SDL owner, and native
SDL WSI route with the benchmark, runtime, tonemap, and SDL migration switches
enabled. All 25 renderer routes and four window routes pass. The focused
groups contain 21 renderer instance and surface cases and 12 SDL ownership
cases.

The opted-in native route passes against a real hidden X11 SDL window and
lavapipe. The SDL owner contains a non-null surface with the exact parent and
native-window generation. The test explicitly destroys that surface while
required validation and the parent instance remain live, then observes zero
validation messages before completing parent and window teardown. It restores
the initial manager, SDL, log, GL-context, and GL-manager state.

The 12 shader-reflection, eight artifact-delivery, and 57 benchmark-harness
unit tests pass. The harness suite exercises Python logic only; no benchmark
or timing path runs. The viewer, appearance utility, and three focused Vulkan
executables have neither a direct Vulkan-loader or MoltenVK dependency nor an
undefined direct Vulkan entry point.

The first wider viewer rebuild inherited a shell without the X11 and GL
development headers expected by the configured build. Supplying the exact Nix
development outputs already associated with that graph let the warnings-as-
errors build pass. This was a host tool-environment gap, not a source failure.

### Linux disabled isolation

A fresh warnings-as-errors Release configuration with the benchmark, runtime,
tonemap, and SDL migration switches disabled builds the viewer, appearance
utility, graphics-API test, and neutral Vulkan requirements test. The only two
remaining window routes pass.

All 41 opt-in high-level Vulkan and SDL target names from the enabled graph
are absent, including global dispatch, parent-owned instance and surface,
material, tonemap, texture-upload, injected SDL ownership, and native WSI
targets. The production SDL Vulkan creator object and its two test object
edges are also absent. The four requested end artifacts contain no Vulkan or
MoltenVK dependency and no direct Vulkan entry-point import. A comprehensive
sweep found the same result across 1,198 ELF files outside the shared
dependency cache. A separate sweep of 1,319 regular files found no Vulkan
loader, MoltenVK, or SPIR-V filename or SPIR-V magic payload.

The disposable graph required the established build-local dependency-cache
link, Autobuild executable, and exact Nix X11, GL, and zlib development
closure that the earlier shell had injected implicitly. These were
environment-discovery corrections; no source change was needed. The complete
disposable build was removed while the shared dependency cache remained
untouched. No benchmark or timing path ran.

### macOS isolation

A disposable runtime-enabled universal warnings-as-errors build produced the
generic global-dispatch and parent-owned instance and surface archives for
both x86_64 and arm64. The focused fake instance and surface test followed
project policy by running as a valid ad-hoc-signed arm64 executable and passed
all 21 cases. SDL WSI remained disabled and absent from the target graph.
Neither the focused executable nor its archives gained a direct Vulkan or
MoltenVK dependency or a direct Vulkan entry-point import.

A separate fresh configuration with the benchmark, runtime, tonemap, and SDL
migration switches disabled built the universal ReleaseOS viewer and packaged
the application. All 11 option-independent renderer routes and both remaining
window routes passed. Every optional Vulkan target was absent. The packaged
application contained no Vulkan, MoltenVK, or SPIR-V payload, and all 367
packaged Mach-O files were checked with zero forbidden dependencies. Four
Chromium helper paths required safe inspection aliases.

Fresh dependency discovery again did not find an already available universal
NDOF library. The failed root was discarded, and a clean configuration given
that exact isolated dependency path passed. This was an environment-discovery
issue rather than a Stage 34 source failure. All nine implementation and test
inputs matched byte for byte before and after both validations, including the
ordered record digest. Disposable sources, builds, transfers, logs, packages,
dependency copies, caches, and transient application registrations were
removed. No benchmark or timing path ran.

### Other platform boundary

No Windows compiler is available. Source and build-graph review confirms that
the reusable instance and surface core has no SDL dependency, the SDL creator
remains guarded to the Linux-only migration graph, and no Win32, Cocoa,
OSMesa, or production factory file changes. A retained native `HINSTANCE` and
`HWND` generation, Win32 surface creator, and native build remain required
before a Windows Vulkan surface can be claimed.

### Production artifact hashes

The enabled Linux build reproduces the canonical production shaders:

- vertex: 8,068 bytes,
  `b5750a179572b2fc3545c7472a28cac963351f7e7bc44df38190bb8961894095`;
- fragment: 7,824 bytes,
  `850d765cb1a6cd31dfbe16f94cedd89ab8306eca771cf833fe47c441b9c743fb`.

The ordered digest of the nine Stage 34 implementation and test SHA-256
records is
`838899435ca77bc2a393867071fa9754f75aad74af6d28ea605c67c55fd1af50`.
Their individual identities are:

- `indra/llrender/vulkan/llrendervulkaninstance.cpp`:
  `1bd1506bf315d5262233cce58d64a2bc7bb4fefc3ad093d12f4ea9dd1d52b1ef`;
- `indra/llrender/vulkan/llrendervulkaninstance.h`:
  `1ad9ff02ac13f964f4925325a77926096d3d3d42dd2952a072fe6126df85ffd7`;
- `indra/llrender/tests/llrendervulkaninstance_test.cpp`:
  `12ff5603107bc71f8cff285c9652381bd6d5442307a2911d2c25d5bce0b1d628`;
- `indra/llwindow/llwindowsdl.cpp`:
  `82204b08cc1274841144c737ca3c152e1ce65a852fba1dcd2cea6435b2e4c053`;
- `indra/llwindow/llwindowsdl.h`:
  `6c003b5a06f820a8a528d0d5702b81818325f124db3ee2f873ec3c93eb264884`;
- `indra/llwindow/llwindowsdlvulkan.cpp`:
  `c753779dcd6fbc8f4c14d7983e5b4d55a3f5a4c5e7efe6ea9b5c9e0eb5ba0327`;
- `indra/llwindow/llwindowsdlvulkan.h`:
  `f37303dbe1a8e172a0d8aafb9a5431c263e19b808ee1d8885904aeca4d59195a`;
- `indra/llwindow/tests/llwindowsdlvulkan_test.cpp`:
  `81d32389cba5c917833930e51dfb3dd20dc84c0f478420f7becea9114cdb9c72`;
- `indra/llwindow/tests/llwindowvulkansdlwsi_test.cpp`:
  `d6504060310c302ccefdf486ca2a80f70d8ad8ac2299ed47c5f45865f0cb7f8b`.

No CMake input changed in this stage. The decision note is excluded because
recording these identities changes its own hash.

### Outcome

Stage 34 passes its technical exit gates. One exact live instance parent now
owns one private generation-checked Vulkan surface, the Linux SDL adapter
supplies only its exact private window and synchronous creator, and every
success, rollback, move, explicit reset, and full teardown path preserves
surface-before-instance destruction. Required validation observes the real
native destruction while both the messenger and parent remain live.

Enabled and disabled isolation holds on Linux and macOS. Production viewer
routes remain OpenGL, no physical device or frame path exists, and Windows
remains source-only. Reanalysis must therefore keep the original three-
platform master Stage 3 open and add the native macOS producer before device
or swapchain work. A matching Win32 producer and native evidence must follow
before the cross-platform vertical slice can advance.
