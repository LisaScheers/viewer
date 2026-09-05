# Stage 41 initial Vulkan swapchain-ownership decision

## Decision

Create and retain one initial `VkSwapchainKHR` for the exact live logical
device and immutable swapchain configuration established by Stages 39 and 40.
The new generation owns only that handle and its destruction command. It does
not retrieve swapchain images, create image views, acquire an image, present a
frame, or alter the production OpenGL renderer.

This is the next bounded part of master Stage 3. Windows execution remains
excluded by user direction. Stage 41 adds no Win32 integration and makes no
Windows build or runtime claim.

## Exact dispatch and create transaction

`resolveVulkanSwapchainGeneration()` accepts only a live Stage 39 logical
device and the Stage 40 configuration created for that same resolver,
instance, surface, physical device, device, unified queue family, and nonzero
drawable extent. A mismatched live chain is rejected before any dispatch
lookup or Vulkan object operation.

The resolver obtains `vkGetDeviceProcAddr` through the exact retained
`vkGetInstanceProcAddr` and `VkInstance`. It then resolves
`vkCreateSwapchainKHR` followed by `vkDestroySwapchainKHR` through the exact
logical device. No Vulkan command is statically imported.

The create information copies the complete Stage 40 selection and fixes the
remaining initial-create fields to this contract:

- `sType` is `VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR`;
- `pNext` is null and `flags` is zero;
- sharing is exclusive with zero queue-family indices and a null index
  pointer;
- presentation is FIFO and clipping is enabled, as required by Stage 40;
- `oldSwapchain` is null;
- the allocation callbacks passed to create and destroy are null.

The surface, image count, format, color space, extent, layer count, usage,
transform, and composite alpha are the exact selected values. Creation begins
with a null local handle. A failed `vkCreateSwapchainKHR` preserves its exact
`VkResult` but never inspects or destroys the undefined output. A successful
call that returns a null handle is a separate typed failure. Missing dispatch
commands are also typed and identify the exact missing command.

## Ownership and rollback

`VulkanSwapchainGeneration` is move-only and owns one non-null swapchain.
`reset()` and destruction call the retained `vkDestroySwapchainKHR` exactly
once through the originating device, then clear the handle. A moved-from or
already-reset generation performs no Vulkan operation.

`VulkanInstanceGeneration` owns the swapchain as the youngest private child of
the configuration and logical-device chain. Acquisition requires current
instance-owner and native-window callbacks, the exact native-window
generation, the exact authenticated drawable extent, every live parent, and
no existing swapchain. It snapshots the complete parent identity before
resolution, rechecks callback freshness and exact provenance after resolution
and allocation, and publishes only a generation whose `createdFor()` contract
still matches. Every failure leaves the parent reusable. A stale or failed
pending generation destroys its swapchain during rollback.

Reset order is swapchain, configuration, logical device, physical selection,
surface, validation messenger, instance, then loader. Resetting any older
parent first removes the swapchain. Move construction transfers the full
chain. Callers must serialize parent, native-window, and drawable-geometry
changes during acquisition.

## Window integration

The existing default-off SDL Vulkan path refreshes client pixel dimensions
after configuration selection and automatically acquires the initial
swapchain before publishing the Vulkan window. Unavailable, nonpositive, or
changed pixels fail without publishing a partial window.

The isolated macOS diagnostic owner likewise refreshes backing pixels, but
keeps swapchain acquisition an explicit diagnostic step. It remains outside
the production Cocoa window factory. Its reset path destroys the swapchain
before the configuration and older Vulkan parents. `vega` is the authoritative
macOS host.

## Build graph and production isolation

The swapchain archive and focused executable exist only inside the existing
default-off Vulkan runtime or tonemap diagnostic graph. The instance archive
gains a one-way dependency on the swapchain archive only inside that graph.
The new archive, parent, and focused tests compile with `VK_NO_PROTOTYPES`; no
loader link edge was added.

The only normal viewer source change is inside the already default-off SDL
Vulkan branch. With all six experimental renderer switches disabled, the
production viewer remains OpenGL and contains no Stage 41 target, object,
dependency, import, or renderer payload.

## Validation evidence

The SHA-256 digest of the filename-and-hash manifest for the sixteen Stage 41
source, build, and test files, ordered lexicographically by repository path, is
`c307d5a8cdc7c68ff25df08ce730dd94c04f23d95bb26550e9e2bf0e82eb090f`.
The local Linux tree and the independently built `vega` snapshot produced the
same digest. This decision note is excluded because including its own digest
would be self-referential.

On Linux, all affected targets compile with warnings as errors. The focused
swapchain, parent, and SDL-owner executables independently pass 6 of 6, 42 of
42, and 14 of 14 cases. All nine global-dispatch, physical-device,
logical-device, configuration, swapchain, parent, requirements, SDL-owner,
and SDL-WSI CTest routes pass. The required-validation hidden X11 route creates
one real lavapipe swapchain from the exact selected configuration, verifies
its non-null handle and provenance, destroys it before its parents, and
reports zero validation messages.

On macOS 26.6.2 arm64, Xcode 26.6 produced a universal ReleaseOS viewer and
universal Stage 41 archives for `x86_64` and `arm64`. All nine focused CTest
routes pass. The swapchain, parent, and macOS-owner executables independently
pass 6 of 6, 42 of 42, and 14 of 14 cases. The required-validation hidden Cocoa
route on `vega` creates one real non-null MoltenVK swapchain from refreshed
backing pixels, verifies exact provenance and child-first reset, reports zero
validation messages, and leaves CGL and OpenGL-manager state unchanged. The
enabled package post-build passes.

The fresh universal macOS graph with all six experiment switches disabled
contains no Stage 41 target or artifact. Its viewer and appearance targets and
package post-build pass. The staged app contains 6,707 regular files and 367
Mach-O files, with zero Stage 41 filename, content, or symbol hit; Vulkan or
MoltenVK dynamic dependency; undefined Vulkan import; or SPIR-V file. The
viewer executable is universal for `x86_64` and `arm64`. The bundle's existing
Chromium SwiftShader library and ICD remain baseline browser payload and
contain no Stage 41 content.

The fresh Linux graph with all six experiment switches disabled likewise
contains no Stage 41 target or artifact. Its viewer, appearance, and package
targets pass. The staged package contains 6,336 regular files and 17 ELF files;
the archive contains 6,457 entries. Both contain zero Stage 41-named payload,
and the staged package has zero Stage 41 content or symbol, Vulkan dynamic
dependency, undefined Vulkan import, or SPIR-V file header. Its four
Vulkan-named files are the existing Chromium `libvulkan.so.1`, SwiftShader
library, and two SwiftShader ICD manifests.

The disposable Linux isolation graph needed Nix development headers added to
its process environment and five build-directory symlinks to immutable Nix
compiler-runtime files so the project's FHS-oriented dependency scanner could
resolve them. That scanner made nonfatal, permission-denied attempts to copy
those system runtimes into `/lib`; no system file changed, and none of those
symlinks entered the package. One unrelated GCC 15 array-bounds diagnostic was
narrowly suppressed in the scratch compile flags while `-Werror` remained
active. The environment also lacked `strip`, so the package archive is
unstripped. These limitations do not weaken the graph, content, symbol,
dependency, import, or file-header isolation checks above, but this archive is
not a release-distribution artifact.

The macOS native route used Vulkan SDK 1.4.357. Linux used Vulkan headers,
loader, and validation layers 1.4.357 with GCC 15.3.0, CMake 4.3.4, and Ninja
1.13.2. The macOS graph used CMake 4.3.4 with the Xcode generator.

No viewer was launched, no login or world connection occurred, and no
benchmark or performance timing was run or retained.

## Explicit deferrals

- Windows build and native execution;
- swapchain image enumeration and image-view ownership;
- image acquisition, presentation, and frame ownership;
- command pools, command buffers, barriers, and recording;
- semaphores, fences, submission, and queue synchronization;
- resize, replacement, and out-of-date or suboptimal handling;
- connection to the production graphics selector or replacement of OpenGL;
- an OpenGL-to-Vulkan compatibility layer;
- benchmark, image-parity, timing, or performance claims.
