# Stage 42 Vulkan swapchain image-view ownership decision

## Decision

Enumerate the borrowed images of the exact live Stage 41 swapchain and own one
format-matched `VkImageView` for each image. Publish the image and view arrays
as one move-only child generation only while the full instance, native-window,
surface, physical-device, logical-device, configuration, and swapchain chain
remains unchanged.

This is a bounded part of master Stage 3. It does not acquire an image, record
commands, establish an image layout, submit work, present a frame, or change
the production OpenGL renderer. Windows execution remains excluded by user
direction. Stage 42 adds no Win32 integration and makes no Windows build or
runtime claim.

## Dispatch and bounded enumeration

`resolveVulkanSwapchainImagesGeneration()` accepts only a live Stage 39
logical device, its exact Stage 40 configuration, and the Stage 41 swapchain
created for those same parents. It resolves `vkGetDeviceProcAddr` through the
retained `vkGetInstanceProcAddr` and exact instance. It then resolves
`vkGetSwapchainImagesKHR`, `vkCreateImageView`, and `vkDestroyImageView`
through the exact logical device before querying or mutating Vulkan state. No
Vulkan command is statically imported.

Image enumeration uses a count query followed by an array query. The complete
count-and-array attempt may run at most four times when the image inventory
grows or the array query returns `VK_INCOMPLETE`. The resolver rejects a zero
count, a final count below the configured minimum, a count above 4,096, a
returned count above the supplied capacity, null images, and duplicate images.
It retains the exact failing command, `VkResult`, observed count, attempt, and
image index where those values are defined.

Vulkan does not define count output after a runtime-error result. The resolver
therefore classifies any count or array runtime error before reading that
output. It treats a count-only `VK_INCOMPLETE`, which conforming implementations
do not return, as a bounded defensive retry. Allocation failure and persistent
inventory churn publish nothing.

The returned `VkImage` handles are borrowed from the swapchain and are never
destroyed by this generation.

## Exact image views and rollback

Every borrowed image receives one `VkImageViewCreateInfo` with this fixed
contract:

- `sType` is `VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO`;
- `pNext` is null and `flags` is zero;
- the image is the same-index borrowed swapchain image;
- the view type is `VK_IMAGE_VIEW_TYPE_2D`;
- the format is the exact Stage 40 swapchain format;
- all component swizzles are identity;
- the aspect is color;
- base mip and array layer are zero;
- mip-level and array-layer counts are one;
- allocation callbacks are null.

A failed `vkCreateImageView` leaves its output undefined, so the resolver never
reads or destroys that output. A successful call returning a null view is a
separate typed failure. Each earlier successful view is owned immediately.
Any later Vulkan, validation, or allocation failure destroys those valid views
exactly once in reverse creation order before reporting failure.

`VulkanSwapchainImagesGeneration` is move-only. It retains the originating
instance resolver, instance, physical device and index, logical device, queue
family, drawable extent, swapchain, format, borrowed image array, owned view
array, and destruction command. Bounds-safe access returns null for an invalid
index. Reset destroys views in reverse order and discards only the borrowed
image records.

## Parent publication and lifetime

`VulkanInstanceGeneration` owns the image-view generation as its youngest
private child. Acquisition requires current instance-owner and native-window
callbacks, the exact nonzero native-window generation and drawable extent,
every live parent, and no existing image-view generation.

Before resolution, the parent snapshots the exact instance, native-window
generation, surface-generation pointer and retained window generation, surface
handle, physical selection, logical device, configuration, swapchain, device,
queue family, drawable extent, and swapchain handle. After every view exists
and pending storage has been allocated, callbacks are rerun and every pointer,
handle, retained generation, and provenance relation is checked again. Only an
unchanged chain may receive the pending generation. Stale or failed pending
state owns its own rollback, so the live parent remains reusable.

Reset order is image views, swapchain, configuration, logical device, physical
selection, surface, validation messenger, instance, then loader. Move
construction transfers the complete chain. Callers must continue to serialize
parent, native-window, and drawable-geometry changes during acquisition.

## Window integration

The existing default-off SDL Vulkan branch refreshes the current client pixel
dimensions after swapchain creation, acquires the image-view generation, and
publishes the Vulkan window only after both operations succeed. Unavailable,
nonpositive, or changed dimensions fail without publishing a partial window.

The isolated macOS diagnostic owner also refreshes backing pixels before an
explicit image-view acquisition. It remains outside the production Cocoa
factory. Its reset path destroys views before the swapchain and all older
parents. `vega` is the authoritative macOS host; no other host supplied
validation evidence.

## Build graph and production isolation

The image-view archive and focused executable exist only inside the existing
default-off Vulkan runtime or tonemap diagnostic graph. The instance archive
gains a one-way dependency on the image-view archive only inside that graph.
The archive, parent, and focused tests compile with `VK_NO_PROTOTYPES`; no
loader link edge was added.

SDL source, linkage, and tests remain behind `LL_VULKAN_SDL_WSI`, which itself
requires the Linux test/runtime experiment. macOS source, linkage, and tests
remain behind `LL_VULKAN_MACOS_WSI`. The only normal viewer source addition is
inside the existing default-off SDL Vulkan preprocessor branch.

With all six experimental renderer switches disabled, the production viewer
remains OpenGL and contains no Stage 42 target, object, dependency, import, or
renderer payload.

## Validation evidence

The SHA-256 digest of the lexicographically ordered, newline-terminated
`sha256sum` manifest for the sixteen Stage 42 source, build, and test files is
`e6ddb7d1e5bcc727d3946cb8c1e5803ec7c6d8e7c5684f4edd3e533ad58cee7d`.
The Linux tree and independently synchronized `vega` snapshot matched all
sixteen per-file lines and the aggregate digest. This decision note is excluded
because including its own digest would be self-referential.

On Linux, the enabled viewer and appearance graphs compile with warnings as
errors. All ten global-dispatch, physical-device, logical-device,
configuration, swapchain, image-view, parent, requirements, SDL-owner, and
SDL-WSI CTest routes pass. The image-view resolver, parent, and SDL-owner
executables independently pass 11 of 11, 46 of 46, and 15 of 15 cases. The
required-validation hidden X11 route creates a real lavapipe swapchain,
enumerates a nonempty image inventory, creates one non-null view for every
image, verifies bounds and provenance, destroys views before their parents,
and reports zero validation messages.

On macOS, Xcode produced universal `x86_64` and `arm64` Release archives for
the image-view generation, parent, and diagnostic owner. The warnings-as-errors
ten-target graph and all ten focused CTest routes pass. Direct focused totals
are 6, 8, 9, 11, 6, 11, 46, 7, 15, and 1 with zero failures. The final 1 of 1
required-validation Cocoa route on `vega` enumerates real MoltenVK swapchain
images, creates a complete same-index view set, verifies child-first reset, and
leaves CGL and OpenGL-manager state unchanged with zero validation messages.

The genuinely fresh universal macOS all-six-off graph builds the viewer,
appearance archive, and package post-build. The staged app contains 6,707
regular files and 367 Mach-O files; the viewer and appearance archive are
universal. Stage 42 target, Xcode graph, object, filename, unique content,
symbol, undefined Vulkan import, Vulkan or MoltenVK dependency, SPIR-V file,
and SPIR-V header hits are all zero. Generic Vulkan command strings and
embedded SPIR-V magic occur only in three existing Chromium or SwiftShader
binaries whose SHA-256 hashes exactly match the previously accepted all-off
package.

The genuinely fresh Linux all-six-off graph also builds the viewer, appearance
archive, and package. The staged tree contains 6,336 regular files and 17 ELF
files; the archive contains the same 6,336 regular files and 17 ELF files.
Stage 42 graph, target, artifact, object-name, filename, unique-content, and
dynamic-symbol hits are all zero. Undefined Vulkan imports, Vulkan or MoltenVK
dependencies, SPIR-V files, and valid SPIR-V headers are also absent from both
the staged tree and archive. Their inventories match exactly. The only four
Vulkan-named files are the existing Chromium SwiftShader payload.

The enabled Linux build used GCC 15.3.0, CMake 4.3.4, Ninja 1.13.2, Vulkan
headers, loader, and validation layers 1.4.357, and Mesa lavapipe. The macOS
route used the Vulkan SDK 1.4.357. No viewer was launched, no login or world
connection occurred, and no benchmark or performance timing was run or
retained.

## Independent audit corrections

Two adversarial reviews changed the implementation before final validation.
The resolver now rejects runtime errors before inspecting undefined count
output, and its fakes model conforming image-count growth while preserving one
explicitly labeled protocol-violation case. Fake image views are unique, so
reverse teardown cannot pass by destroying one handle twice. The parent now
pins the exact surface-generation pointer and its retained native-window
generation at the final publication boundary. Fresh core, parent, platform,
scope, privacy, and build-graph audits report no remaining issue.

## Explicit deferrals

- Windows build and native execution;
- `vkAcquireNextImageKHR` and acquired-image ownership;
- image-layout transitions and barriers;
- command pools, command buffers, and recording;
- semaphores, fences, queue submission, and synchronization;
- presentation and frame ownership;
- resize, replacement, and out-of-date or suboptimal handling;
- render passes, framebuffers, graphics pipelines, and draws;
- connection to the production graphics selector or replacement of OpenGL;
- an OpenGL-to-Vulkan compatibility layer;
- benchmark, image-parity, timing, or performance claims.
