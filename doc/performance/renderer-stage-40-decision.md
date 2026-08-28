# Stage 40 Vulkan swapchain-configuration decision

## Decision

Retain one immutable, loader-neutral swapchain configuration for the exact live
surface, physical device, logical device, unified queue family, and drawable
pixel extent established by Stages 37 through 39. The configuration owns no
Vulkan object. It records only the bounded support snapshot and selected values
needed to create an initial `VkSwapchainKHR` in the next stage.

This is the next bounded part of master Stage 3. It does not create a
swapchain, retrieve images, create image views, allocate command or
synchronization objects, handle resize, submit work, or change the production
OpenGL renderer.

Windows execution remains excluded by user direction. Stage 40 adds no Win32
integration and makes no Windows build or runtime claim.

## Surface-support transaction

`resolveVulkanSwapchainConfigurationGeneration()` accepts only the exact
Stage 38 physical-device selection, its Stage 39 logical device, and a nonzero
drawable pixel extent. It resolves these instance-level commands through that
selection's retained `vkGetInstanceProcAddr` and `VkInstance`:

- `vkGetPhysicalDeviceSurfaceCapabilitiesKHR`;
- `vkGetPhysicalDeviceSurfaceFormatsKHR`;
- `vkGetPhysicalDeviceSurfacePresentModesKHR`.

Missing commands and failed queries preserve the exact command and
`VkResult`. Format and present-mode enumeration each allow at most four full
attempts across count churn or `VK_INCOMPLETE`. A reported count must remain
nonzero and within its allocated capacity. Formats are capped at 4,096 and
present modes at 256. Scratch buffers use no-throw allocation; allocation
failure and retry exhaustion are typed and publish no generation.

The capability snapshot is rejected when its image-count or extent ranges are
invalid, a fixed extent is zero or out of range, a variable extent has no
nonzero maximum, no image array layer is possible, the current transform is
not one supported single bit, no composite-alpha mode exists, or color
attachment usage is unsupported.

## Selected create contract

The selected contract is deliberately conservative and deterministic:

- use a fixed `currentExtent` when supplied; otherwise clamp the authenticated
  drawable pixel extent independently to the advertised bounds;
- prefer `VK_FORMAT_B8G8R8A8_UNORM`, then
  `VK_FORMAT_R8G8B8A8_UNORM`, both only with
  `VK_COLOR_SPACE_SRGB_NONLINEAR_KHR`;
- require `VK_PRESENT_MODE_FIFO_KHR`;
- request one more than `minImageCount` when the maximum and integer range
  permit it, otherwise request the minimum;
- use one image layer, color-attachment usage, exclusive sharing for the
  already unified family, the current surface transform, and clipped output;
- choose composite alpha in this order: opaque, premultiplied,
  postmultiplied, inherit.

The generation retains the raw capabilities, authenticated drawable extent,
chosen surface format, present mode, image count and extent, transform, and
alpha mode. The existing tonemap shader writes display-encoded values to UNORM
storage, so an SRGB image format would apply an unwanted second transfer
conversion.

## Parent ownership and lifetime

`VulkanInstanceGeneration` owns the move-only configuration as the youngest
private child of the exact logical-device chain. Acquisition requires live and
current instance, native-window, surface, physical-selection, and
logical-device generations. Invalid callbacks or geometry, missing parents,
duplicate ownership, native-generation mismatch, stale ownership, nested
resolution failure, and allocation failure remain distinct typed outcomes.

The parent snapshots the instance, surface, physical device, logical device,
queue family, and drawable extent before resolution. It rechecks callback
freshness, parent identity, and `createdFor()` before publishing the pending
generation. Failed or stale work leaves the parent reusable.

Reset order is configuration, logical device, physical selection, surface,
validation messenger, instance, then loader. Resetting any older parent first
removes the configuration. Move construction transfers the complete chain.
The API requires callers to serialize parent, native-window, and geometry
changes during acquisition.

A changed or unavailable drawable must reset this snapshot and acquire a new
one before a swapchain can be created or recreated. Stage 40 intentionally
adds no resize or out-of-date event handler.

## Window integration

The existing opt-in SDL Vulkan branch injects
`SDL_GetWindowSizeInPixels`, rejects unavailable or nonpositive results, and
passes actual client pixels rather than logical window units. It acquires the
configuration after the Stage 39 logical device and before publishing the
Vulkan window.

The isolated macOS owner refreshes its native geometry and passes the retained
backing-pixel width and height. Its diagnostic invokes acquisition explicitly;
the owner remains outside the production Cocoa window factory. `vega` is the
authoritative macOS host.

## Build graph and production isolation

The configuration archive and focused executable exist only inside the
existing default-off Vulkan runtime or tonemap diagnostic graph. The instance
archive gains a one-way dependency on the configuration archive only inside
that graph. The loader-neutral configuration and instance archives and tests
compile with `VK_NO_PROTOTYPES`; Stage 40 adds no loader link edge.

The only normal viewer source change is inside the already default-off SDL
Vulkan branch. With all six experimental renderer switches disabled, the
production viewer remains OpenGL and must contain no Stage 40 target, object,
dependency, import, or renderer payload.

## Validation evidence

The SHA-256 digest of the filename-and-hash manifest for the sixteen Stage 40
source, build, and test files, ordered lexicographically by repository path, is
`998301c1b810aaa5d8b216627e50fd906de01f49adcc055d3f909da5f204eb65`.
The local Linux tree and the independently built `vega` snapshot produced the
same digest. The decision note is excluded because including its own digest
would be self-referential.

On Linux, all affected targets compile with warnings as errors. The focused
configuration, parent, and SDL-owner executables independently pass 11 of 11,
38 of 38, and 13 of 13 cases. All eight global-dispatch, physical-device,
logical-device, configuration, parent, requirements, SDL-owner, and SDL-WSI
CTest routes pass. The required-validation hidden X11 route queries one real
lavapipe surface, verifies the selected format, FIFO mode, exact legal pixel
extent, image count, transform, alpha mode, usage, layer count, sharing mode,
and clipping policy, and reports zero validation messages.

On macOS 26.6.2 arm64, Xcode 26.6 produced a universal ReleaseOS viewer and
universal Stage 40 archives for `x86_64` and `arm64`. All eight focused CTest
routes pass. The configuration, parent, and macOS-owner executables
independently pass 11 of 11, 38 of 38, and 13 of 13 cases. The
required-validation hidden Cocoa route on `vega` queries one real MoltenVK
surface from refreshed backing pixels and verifies the same exact policy with
zero validation messages and unchanged CGL and OpenGL-manager state. The
enabled package target passes. The focused executables pass strict signature
verification; the viewer bundle intentionally lacks an application signature
because signing is disabled for this disposable build.

The fresh universal macOS graph with all six experiment switches disabled
contains no Stage 40 target or artifact. Its viewer, appearance, and package
targets pass. The staged app contains 6,707 regular files and 367 Mach-O files,
with zero Stage 40 filename, content, or symbol hit; Vulkan or MoltenVK dynamic
dependency; undefined Vulkan import; Vulkan- or MoltenVK-named file; or SPIR-V
file header or embedded magic hit. The viewer executable is universal for
`x86_64` and `arm64`.

The fresh Linux graph with all six experiment switches disabled likewise
contains no Stage 40 target or artifact. Its viewer, appearance, and package
targets pass. The staged package contains 6,336 regular files and 17 ELF files;
the archive contains 6,457 entries. Both contain zero Stage 40-named payload,
and the staged package has zero Stage 40 content or symbol, Vulkan dynamic
dependency, undefined Vulkan import, or SPIR-V file header. Its four
Vulkan-named files are the existing Chromium `libvulkan.so.1`, SwiftShader
library, and two SwiftShader ICD manifests.

The disposable Linux isolation graph needed Nix development headers added to
its process environment and five build-directory symlinks to immutable Nix
compiler-runtime files so the project's FHS-oriented dependency scanner could
resolve them. That scanner then made nonfatal, permission-denied attempts to
copy those system runtimes into `/lib`; no system file changed, and none of
those symlinks entered the package. The one unrelated GCC 15 array-bounds
diagnostic was narrowly suppressed in the scratch compile flags while
`-Werror` remained active. The FHS environment also lacked `strip`, so the
package archive is unstripped. These limitations do not weaken the graph,
content, symbol, dependency, import, or file-header isolation checks above,
but this archive is not a release-distribution artifact.

The macOS native route used Vulkan SDK 1.4.357 with installer SHA-256
`539433589c83522e6f31b1c7b418a4167e21597a4a361ab119e1dc0760cf3865`.
Linux used Vulkan headers, loader, and validation layers 1.4.357 with GCC
15.3.0, CMake 4.3.4, and Ninja 1.13.2. The macOS graph used CMake 4.3.4 with
the Xcode generator.

No viewer was launched, no login or world connection occurred, and no
benchmark or performance timing was run or retained.

## Explicit deferrals

- Windows build and native execution of the committed Win32 diagnostic;
- device dispatch through `vkGetDeviceProcAddr`;
- swapchain creation, destruction, replacement, and out-of-date handling;
- swapchain images and image views;
- command pools, command buffers, barriers, and recording;
- semaphores, fences, submission, presentation, and frame ownership;
- connection to the production graphics selector or replacement of OpenGL;
- an OpenGL-to-Vulkan compatibility layer;
- benchmark, image-parity, timing, or performance claims.
