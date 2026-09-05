# Stage 43 idle Vulkan frame-slot ownership decision

## Decision

Own one idle frame slot for the exact live Stage 42 swapchain image generation.
The slot contains one reset-capable command pool for the selected graphics and
presentation queue family, one primary command buffer, one default binary
image-available semaphore, and one initially signaled submission-completion
fence.

Publish the slot as the youngest move-only child of
`VulkanInstanceGeneration`. Publication succeeds only while the full instance,
native-window, surface, physical-device, logical-device, swapchain
configuration, swapchain, and image-generation chain remains unchanged.

This is a bounded part of master Stage 3. It does not acquire a swapchain image,
record a command, reset or wait on synchronization, submit work, present a
frame, or change the production OpenGL renderer. Windows execution remains
excluded by user direction. Stage 43 adds no Win32 integration and makes no
Windows build or runtime claim.

## Dispatch and exact resource creation

`resolveVulkanSwapchainFrameSlotGeneration()` accepts only a live Stage 39
logical device, its exact Stage 40 configuration, the Stage 41 swapchain, and
the Stage 42 image generation created for those same parents. It resolves
`vkGetDeviceProcAddr` through the retained `vkGetInstanceProcAddr` and exact
instance. It then resolves every command needed for creation and rollback
through the exact logical device before mutating Vulkan state:

- `vkCreateCommandPool` and `vkDestroyCommandPool`;
- `vkAllocateCommandBuffers`;
- `vkCreateSemaphore` and `vkDestroySemaphore`;
- `vkCreateFence` and `vkDestroyFence`.

No Vulkan command is statically imported. The command pool uses
`VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT` and the exact unified
graphics and presentation queue-family index. The pool allocates one primary
command buffer. The semaphore has no type extension, so it is a default binary
semaphore in its initial unsignaled state. The fence uses
`VK_FENCE_CREATE_SIGNALED_BIT`, which makes the first future completion check
well-defined without a special first-frame branch. Every object create and
destroy call uses null allocation callbacks.

The command buffer is owned by its pool and is freed implicitly when the pool
is destroyed. Stage 43 does not resolve or call `vkFreeCommandBuffers`.

## Failure handling and rollback

All required commands resolve before the first create call. A missing command
therefore leaves Vulkan state untouched.

Failed Vulkan create calls leave their output handles undefined. The resolver
classifies each failed command-pool, semaphore, or fence result before reading
its output. A successful create that returns a null handle is a separate typed
failure.

`vkAllocateCommandBuffers` has a different failure contract. On failure it
writes null to every requested command-buffer output. The resolver still
classifies the `VkResult` before using the output. Its focused fake writes null
on allocation failure, and the test keeps allocation failure distinct from a
successful call that returns a null command buffer.

Ownership begins after each successful non-null output. Any later failure
destroys only valid earlier resources, exactly once, in reverse order: fence,
semaphore, then command pool. Destroying the pool implicitly releases the
command buffer. Allocation failure in pending parent storage also rolls back
the complete unpublished slot.

## Lifetime and synchronization contract

`VulkanSwapchainFrameSlotGeneration` retains the originating instance
resolver, instance, surface, physical device and index, logical device, queue,
queue family and index, drawable extent, swapchain, image format and count,
and the exact Stage 42 image-generation object address. `createdFor()` checks
that full identity before parent publication.

The slot is move-only and has idempotent reset. Reset destroys the fence, then
the semaphore, then the command pool. It clears all retained parent identity
after resource destruction.

Host access to the slot is externally serialized. Before direct reset,
transitive reset, or destruction, no operation may still use the slot's fence
or semaphore and its command buffer must not be pending. The child and parent
public class contracts both state this requirement. Stage 43 never submits the
slot, so its native tests satisfy the requirement without a device or queue
idle call.

The image-available semaphore belongs to the frame slot because queue
submission will consume it after a future image acquisition. A semaphore that
will be waited by presentation is not part of Stage 43. Safe reuse of the
present-wait semaphore depends on the acquired image index, so its ownership
belongs with a later complete presentation transaction.

## Parent publication

`VulkanInstanceGeneration` owns the frame slot after the image generation.
Acquisition requires current instance-owner and native-window callbacks, the
exact nonzero native-window generation and drawable extent, every live parent,
and no existing slot.

The callbacks are synchronous and are not retained. The caller must serialize
parent, native-window, and drawable-geometry changes for the full acquisition
call.

Before resolution, the parent snapshots the exact instance, native-window
generation, surface-generation object and retained generation, surface handle,
physical selection, logical device and queue, configuration, swapchain, and
image-generation object, count, and format. It allocates pending storage before
the final freshness check. The callbacks then run again, followed by
pointer-identity checks before every raw-parent dereference and a complete
provenance check. Only an unchanged chain receives the pending slot.

Duplicate, stale, malformed, failed, and allocation-failed requests leave the
live chain reusable. Within `VulkanInstanceGeneration`, reset order is frame
slot, image views, swapchain, configuration, logical device, physical
selection, surface, validation messenger, instance, then global dispatch.
Every broader parent reset first removes the slot. Window owners later release
their requirements, native window, and loader in that order.

## Window integration

The existing default-off SDL Vulkan branch refreshes the current client pixel
dimensions after Stage 42 image creation. It then creates the frame slot before
publishing the hidden Vulkan window. Unavailable, nonpositive, or changed
dimensions fail without publishing a partial window. Surface reset removes the
slot before image views and every older parent.

The isolated macOS diagnostic owner exposes explicit frame-slot acquisition
after refreshing Cocoa backing pixels. It remains outside the production Cocoa
factory. Its reset path removes the slot first. `vega` is the authoritative
macOS host, and no other Mac supplies validation evidence.

## Build graph and production isolation

The frame-slot archive and focused executable exist only inside the existing
default-off Vulkan runtime or tonemap diagnostic graph. The instance archive
gains a one-way dependency on the frame-slot archive only inside that graph.
The archive, parent, and focused tests compile with `VK_NO_PROTOTYPES`; no
loader link edge was added.

SDL source and linkage remain behind `LL_VULKAN_SDL_WSI`. That option requires
the Linux SDL window path, `LL_TESTS=ON`, and `LL_VULKAN_RUNTIME_TEST=ON`.
macOS source, linkage, and tests remain behind `LL_VULKAN_MACOS_WSI`. The only
normal viewer source addition is inside the existing default-off SDL Vulkan
preprocessor branch.

With all six renderer experiments disabled, the production viewer remains
OpenGL and contains no Stage 43 target, object, dependency, import, or renderer
payload.

## Validation evidence

The SHA-256 digest of the lexicographically ordered, newline-terminated
`sha256sum` manifest for the sixteen Stage 43 source, build, and test files is
`7fd97dc93d412e62ddda2f9459231b0129d580f8ae60063420d0ebeaabfb9298`.
This decision note is excluded because including its own digest would be
self-referential.

On Linux, the enabled viewer and appearance targets compile with warnings as
errors. All eleven global-dispatch, physical-device, logical-device,
configuration, swapchain, image-view, frame-slot, parent, requirements,
SDL-owner, and SDL-WSI CTest routes pass. Direct focused totals are 6, 8, 9,
11, 6, 11, 8, 50, 7, 16, and 1 with zero failures. The frame-slot archive has
no direct undefined Vulkan command symbol, and the frame-slot and parent test
executables have no Vulkan or MoltenVK dynamic dependency.

The required-validation hidden X11 route creates a real lavapipe swapchain and
Stage 42 image-view generation. It then creates a non-null command pool,
primary command buffer, binary semaphore, and signaled fence for the exact
queue family and parents. Surface reset destroys the slot first, and the test
reports zero validation messages.

The genuinely fresh Linux all-six-off graph builds `secondlife-bin`,
`llappearance`, and `llpackage` with warnings as errors. It contains 1,173
objects. The staged package has 6,336 regular files, 116 directories, five
symlinks, and 17 ELF files. The archive has 6,457 entries, and its extracted
tree has the same file, directory, symlink, and ELF counts as staging. The two
trees have zero differences.

Stage 43 graph names, targets, artifacts, object names, filenames, unique
content, symbols, and dynamic-symbol hits are all zero. Undefined Vulkan
imports, Vulkan or MoltenVK dependencies, actual SPIR-V files, and valid SPIR-V
headers are also absent from the staged tree and archive. The only four
Vulkan-named files are the existing CEF SwiftShader payload. Their inventory
matches the accepted Stage 42 all-off package. A broad embedded-byte heuristic
also matches the same existing SDL, CEF, and GLES binaries seen at Stage 42;
none is a SPIR-V file.

On macOS, a fresh Stage 42 source archive plus the same sixteen Stage 43 files
reproduced the manifest digest above. Xcode produced universal `x86_64` and
`arm64` ReleaseOS archives with warnings as errors. All eleven focused CTest
routes pass. The frame-slot, parent, and macOS-owner executables independently
pass 8 of 8, 50 of 50, and 16 of 16 cases.

The required-validation hidden Cocoa route on `vega` passes 1 of 1 against
Vulkan SDK 1.4.357 and MoltenVK. It creates a real non-null command pool,
primary command buffer, binary semaphore, and signaled fence. Slot reset
precedes image-view and parent reset, validation reports zero messages before
and after destruction, and CGL and OpenGL-manager state remain unchanged. The
eleven test executables and the frame-slot archive have no direct Vulkan
command import, and the executables have no Vulkan or MoltenVK dynamic
dependency.

The genuinely fresh universal macOS all-six-off graph builds the viewer,
appearance archive, and package post-build. The viewer and appearance archive
are universal. The staged app contains 6,707 regular files and 367 Mach-O
files. Stage 43 graph, target, object-name, filename, content, and symbol hits
are all zero. Undefined Vulkan imports, Vulkan or MoltenVK dependencies, named
SPIR-V files, and standalone SPIR-V headers are also absent.

The only Vulkan-named files are Chromium's existing SwiftShader library and
ICD manifest. Embedded SPIR-V magic coincidences occur only in the existing
CEF binary, `libGLESv2`, and one Chromium locale pack, at the same relative
paths and counts accepted for Stage 42. A redundant SHA-256 comparison of
those baseline files was interrupted when `vega` was returned for other work;
none of the Stage 43 absence claims above depends on that comparison.

The enabled Linux build used GCC 15.3.0, CMake 4.3.4, Ninja 1.13.2, Vulkan
headers, loader, and validation layers 1.4.357, and Mesa lavapipe 26.2.1. No
viewer was launched, no login or world connection occurred, and no benchmark
or performance timing was run or retained.

## Independent audit corrections

Adversarial review found two Vulkan contract issues and one documentation
scope issue before final validation. The reset and destructor precondition now
states the required idle and external host-synchronization state. The
command-buffer fake now writes null on a failed allocation, matching Vulkan's
specific output rule. The parent class contract now applies the same teardown
precondition to destruction and every broader reset that can transitively
destroy the slot.

Independent core, parent, platform, scope, privacy, and build-graph reviews
report no remaining issue. Changed-line and complete new-file formatting checks
pass, and `git diff --check` is clean.

## Explicit deferrals

- Windows build and native execution;
- `vkAcquireNextImageKHR` and acquired-image ownership;
- command-buffer reset, begin, recording, and end;
- fence wait or reset;
- queue submission and completion handling;
- image-layout transitions and barriers;
- presentation-wait semaphores and presentation;
- resize, replacement, and out-of-date or suboptimal handling;
- render passes, framebuffers, graphics pipelines, and draws;
- connection to the production graphics selector or replacement of OpenGL;
- an OpenGL-to-Vulkan compatibility layer;
- benchmark, image-parity, timing, or performance claims.
