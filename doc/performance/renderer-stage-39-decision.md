# Stage 39 Vulkan logical-device ownership decision

## Decision

Retain one loader-neutral, default-off logical-device generation for the exact
Stage 38 physical device and unified graphics and presentation queue family.
The generation owns one `VkDevice` and borrows queue index zero from that
device.

This is the next bounded part of master Stage 3. It does not query swapchain
support, create a swapchain, allocate command or synchronization objects,
submit work, or change the production OpenGL renderer.

Windows execution remains excluded by user direction. The shared core can be
compiled by an opt-in Windows diagnostic graph, but Stage 39 adds no Win32
integration and makes no Windows build or runtime claim.

## Logical-device transaction

`resolveVulkanLogicalDeviceGeneration()` accepts only the exact immutable
Stage 38 presentation-device selection. Before touching device state, it
resolves these commands through that selection's non-null
`vkGetInstanceProcAddr` and `VkInstance`:

- `vkGetPhysicalDeviceFeatures`;
- `vkCreateDevice`;
- `vkDestroyDevice`;
- `vkGetDeviceQueue`.

Missing commands are typed and stop the transaction before feature query or
creation. The transaction queries the selected physical device and requires
the `independentBlend` feature fixed by Stage 28. It enables no other core
feature.

Creation uses one queue-create record with the exact selected family, queue
count one, queue index zero, priority `1.0`, zero flags, and null `pNext`.
The device-create record uses zero flags, null `pNext`, no layers, null
allocation callbacks, and exactly the Stage 38 device extensions in their
selected order: `VK_KHR_swapchain`, followed by
`VK_KHR_portability_subset` only when required.

A failed `vkCreateDevice` result preserves the exact `VkResult`. Its output is
undefined by Vulkan and is therefore neither inspected nor destroyed, even if
a broken implementation writes a non-null bit pattern. Success with a null
device is rejected without destruction. Success with a null queue destroys
the valid pending device before returning a typed failure.

The resulting move-only generation retains the exact resolver, instance,
surface, physical-device handle and index, queue family, queue index, enabled
features, enabled extensions, device, queue, and destruction command. Explicit
reset and destruction release the device once with null callbacks and clear
the borrowed queue.

## Parent ownership and lifetime

`VulkanInstanceGeneration` owns the logical-device generation as the youngest
private child of the exact presentation selection. Acquisition requires live
and current instance, surface, selection, and native-window generations.
Invalid callbacks, missing parents, duplicate ownership, native-generation
mismatch, stale ownership, nested resolution failure, and allocation failure
remain distinct typed outcomes.

The parent snapshots the exact instance, surface, physical device, and queue
family before creation. A pending generation owns rollback before the final
freshness and identity checks. Publication occurs only after those checks and
`createdFor()` authenticate the unchanged Stage 38 selection. Allocation or
post-create freshness failure destroys the pending device and leaves the
parent chain reusable.

Move construction transfers the complete chain. Reset order is logical
device, presentation selection, surface, validation messenger, instance, then
loader. Resetting the presentation selection or surface first removes the
logical-device child.

The API requires the caller to serialize parent and native-window lifetime
changes during acquisition. Reentrant or concurrent parent destruction is
outside this synchronous contract.

## Window integration

The existing opt-in SDL Vulkan branch acquires the logical device immediately
after Stage 38 presentation selection and before publishing the Vulkan window.
Any failure tears down the still-private owner in dependency order. This path
remains compiled and reachable only behind `LL_VULKAN_SDL_WSI`.

The isolated macOS owner exposes the same acquisition operation but does not
join the production window factory. Its native diagnostic invokes the
operation explicitly after creating the Metal surface and selecting the
presentation device.

## Build graph and production isolation

The logical-device archive and focused executable exist only inside the
existing default-off Vulkan runtime or tonemap diagnostic graph. The instance
archive gains a one-way dependency on the logical-device archive only inside
that graph. Both compile with `VK_NO_PROTOTYPES`; no Vulkan loader link edge is
introduced.

The only normal viewer source change is inside the already default-off SDL
Vulkan branch. With all six experimental renderer switches disabled, the
production viewer remains OpenGL and must contain no Stage 39 target, object,
dependency, import, or renderer payload.

## Validation evidence

The SHA-256 digest of the filename-and-hash manifest for the fourteen Stage 39
source, build, and test files, ordered lexicographically by repository path, is
`de2d47e63e5842f7123e0ee94e49af9979bc6bc239f1d915868d4e3290e15e18`.
The local Linux tree and both independently configured all-options-off Linux
and `vega` snapshots produced the same digest. The decision note is excluded
because including its own digest would be self-referential.

On Linux, the focused logical-device, parent, SDL owner, SDL WSI, viewer, and
appearance targets compile with warnings as errors. The logical-device fake
suite passes all 9 cases and the parent suite passes all 33 cases. All seven
global-dispatch, physical-device, logical-device, parent, requirements, SDL
owner, and SDL WSI CTest routes pass. The required-validation hidden X11 route
creates and destroys a real device and retrieves a non-null queue through
lavapipe with zero validation messages.

On macOS 26.6.2 arm64, Xcode 26.6 produced a universal ReleaseOS viewer and
universal Stage 39 archives for `x86_64` and `arm64`. All seven focused CTest
routes pass. The logical-device and parent executables independently report
9 of 9 and 33 of 33 passing cases. The required-validation hidden Cocoa route
on `vega` creates and destroys a real MoltenVK device and retrieves a non-null
queue with the exact swapchain and portability-subset policy, zero validation
messages, and unchanged CGL and OpenGL-manager state. The enabled package target
also passes. The focused executables pass strict signature verification; the
viewer bundle intentionally lacks an application signature because signing is
disabled for this disposable build.

The fresh universal macOS graph with all six experiment switches disabled
contains no Stage 39 target or artifact. Its viewer, appearance, and package
targets pass. The staged app contains 6,707 regular files and 367 Mach-O files,
with zero Stage 39 payload, import, Vulkan or MoltenVK dependency, Vulkan-named
file, or SPIR-V file header. Two existing Chromium binaries contain the SPIR-V
magic byte sequence internally; neither is a standalone SPIR-V file or a Stage
39 payload.

The fresh Linux graph with all six experiment switches disabled likewise
contains no Stage 39 target or artifact. Its viewer, appearance, and package
targets pass. The staged package contains 6,336 regular files and 17 ELF files;
the archive contains 6,457 entries. Both contain zero Stage 39-named payload,
and the staged package has zero Stage 39 symbols, Vulkan dynamic dependencies,
undefined Vulkan imports, or SPIR-V file headers. Its four Vulkan-named files
are the existing Chromium `libvulkan.so.1`, SwiftShader library, and two
SwiftShader ICD manifests.

This disposable Linux isolation build disabled fatal warnings only after GCC
15 reported an unrelated existing array-bounds warning in
`llpaneloutfitedit.cpp`; every affected Stage 39 target separately passes with
warnings as errors. The disposable FHS environment also lacked `strip`, so the
package archive is unstripped. These limitations do not weaken the graph,
symbol, dependency, import, or file-header isolation checks above, but this
archive is not a release-distribution artifact.

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
- swapchain capability, format, present-mode, and image-count queries;
- swapchain, image, and image-view ownership;
- command pools, command buffers, barriers, and recording;
- semaphores, fences, submission, presentation, and frame ownership;
- connection to the production graphics selector or replacement of OpenGL;
- an OpenGL-to-Vulkan compatibility layer;
- benchmark, image-parity, timing, or performance claims.
