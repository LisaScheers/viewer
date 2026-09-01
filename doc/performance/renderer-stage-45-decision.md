# Stage 45 complete Vulkan presentation transaction decision

## Decision

Complete one acquire-to-present transaction through the exact Stage 44 frame
slot, then repeat it. Each transaction acquires one real swapchain image,
transitions it from undefined layout to presentation layout, submits that
command buffer with binary semaphore synchronization, presents the exact image,
and waits for both submission and presentation-resource completion.

`VK_KHR_swapchain_maintenance1` provides the presentation fence and the
acquired-image release operation needed to make reuse, cancellation, and
teardown explicit. The stage requires this capability. It does not substitute a
submit fence, queue idle, or device idle for presentation completion.

The image contents remain deliberately undefined. This stage adds no shader,
attachment, render pass, dynamic rendering, pipeline, draw, upload, readback, or
production renderer selection. It is a bounded dependency of master Stage 3.
Windows remains excluded by user direction.

## Capability admission

The diagnostic instance request appends `VK_KHR_get_surface_capabilities2` and
`VK_KHR_surface_maintenance1` after the platform-native surface extensions. The
existing validation and portability requests retain their prior ordering.

Physical-device selection now requires
`VK_KHR_swapchain_maintenance1`. It queries
`VkPhysicalDeviceSwapchainMaintenance1FeaturesKHR` through
`vkGetPhysicalDeviceFeatures2` and rejects the device unless
`swapchainMaintenance1` is true. A missing enumeration command, feature-query
command, extension, or feature reports its exact typed cause before WSI state is
mutated.

Logical-device creation enables the exact swapchain and maintenance extensions,
plus the existing optional portability subset when advertised. It chains a
true `VkPhysicalDeviceSwapchainMaintenance1FeaturesKHR` into
`VkDeviceCreateInfo`. The immutable physical and logical generations retain the
admitted capability so later owners can authenticate it.

## Frame-slot resources and dispatch

The frame slot owns six handles for one externally serialized queue:

- one command pool;
- one primary command buffer;
- one image-available binary semaphore;
- one presentation-ready binary semaphore;
- one submission fence;
- one presentation-completion fence.

Both fences are created signaled. Construction creates every object in temporary
storage and rolls back child first on any failure or null handle. Publication
occurs only after the exact logical device, swapchain configuration, swapchain,
and image generations are authenticated.

The presentation operation resolves these device commands through the retained
loader-neutral dispatch:

- `vkWaitForFences`;
- `vkAcquireNextImageKHR`;
- `vkResetCommandBuffer`;
- `vkBeginCommandBuffer`;
- `vkCmdPipelineBarrier`;
- `vkEndCommandBuffer`;
- `vkResetFences`;
- `vkQueueSubmit`;
- `vkQueuePresentKHR`;
- `vkReleaseSwapchainImagesKHR`.

Resolution is all or nothing. It publishes no partial dispatch and changes no
Vulkan object when a command is missing. No Vulkan command is statically
imported and no loader link edge enters a core archive.

## Acquire-to-present order

One normal transaction performs this exact sequence:

1. Wait for both prior fences with `waitAll` true.
2. Acquire one image with the image-available semaphore, no acquire fence, and a
   finite one-second timeout.
3. Accept both `VK_SUCCESS` and `VK_SUBOPTIMAL_KHR`, retain the exact returned
   index, and bounds-check it against the retained image generation.
4. Reset and begin the primary command buffer.
5. Record one full-color-subresource barrier from `VK_IMAGE_LAYOUT_UNDEFINED` to
   `VK_IMAGE_LAYOUT_PRESENT_SRC_KHR` for the exact acquired image.
6. End the command buffer and reset both fences.
7. Submit one command buffer, waiting on image-available and signaling
   presentation-ready, with the submission fence.
8. Present the exact swapchain and image index while waiting on
   presentation-ready. Chain one `VkSwapchainPresentFenceInfoKHR` naming the
   presentation-completion fence.
9. Wait for both fences before releasing the retained image index and making the
   slot reusable.

The queue family remains the previously selected unified graphics and
presentation family. Queue ownership is externally serialized. The barrier uses
ignored queue-family indices, one color aspect, one mip level, and one array
layer. It does not claim useful pixel contents.

An acquire-time out-of-date or surface-lost result returns the corresponding
typed outcome without an acquired image. `VK_SUBOPTIMAL_KHR` retains its outcome
through successful presentation completion. Present-time out-of-date and
surface-lost results are also retained until the presentation fence proves that
the queued wait and presentation resources are retired.

## Completion and failure contract

The frame-slot disposition names every live obligation. In addition to the
Stage 44 states, Stage 45 uses `ImageAcquired`, `SubmissionPending`,
`PresentationReady`, `PresentPending`, `FenceResetIndeterminate`,
`PresentationIndeterminate`, `CancellationPending`, `ReleaseRequired`, and
`ReleaseIndeterminate`.

An acquired index that is outside the retained image count remains explicitly
owned in `ImageAcquired`. Reset is forbidden because neither presentation nor a
safe release can name a valid image. Device retirement is the only safe recovery
for that impossible driver result.

Allocation failures from `vkQueuePresentKHR` leave the request unchanged by
contract, so `PresentationReady` remains retryable. Results that are known to
enqueue the presentation wait move to `PresentPending`, even when they report
suboptimal, out-of-date, surface loss, full-screen loss, timing-queue pressure,
or device loss. Both fences must retire before reuse or teardown.

An unexpected present result does not establish whether the wait semaphore or
presentation fence was consumed. The slot therefore enters
`PresentationIndeterminate`; it does not retry, cancel, or destroy potentially
live resources.

A failed two-fence reset is similarly ambiguous unless the failure is a defined
allocation failure or device loss. The conservative terminal state is
`FenceResetIndeterminate`. A failed acquired-image release enters
`ReleaseIndeterminate`; retrying could release an image that is no longer
acquired, so no duplicate release is issued.

Queue-submit device loss remains pending until a fence wait retires possible
resource use. Every other pending wait preserves enough exact state for an
explicit completion retry. Direct and transitive reset checks refuse every
acquired, pending, or indeterminate state.

## Cancellation

A post-acquire failure cannot abandon the binary semaphore payload or acquired
image. Cancellation handles the two safe pre-present states:

- `ImageAcquired` submits an empty operation that waits on image-available and
  signals the submission fence;
- `PresentationReady` submits an empty operation that waits on
  presentation-ready and signals the presentation-completion fence.

If the original two-fence reset left the other fence unsignaled, cancellation
performs a second empty fence-backed submission to restore that fence as well.
It waits for the exact one- or two-fence set before calling
`vkReleaseSwapchainImagesKHR` for the exact retained index. Only successful
release clears the index and restores `Reusable`.

Cancellation retries only a known pending fence wait or the known follow-up
empty submission. Device loss remains typed. No cancellation path assumes that
a submit fence proves presentation completion.

## Parent and platform ownership

`VulkanInstanceGeneration` authenticates the complete chain from global
dispatch through instance, surface, physical selection, logical device, queue,
swapchain configuration, swapchain, images, and frame slot before every
operation. It rechecks the instance-owner and native-window callbacks around
dispatch resolution. No callback or allocation runs after queue execution
starts.

The default-off SDL and macOS diagnostic owners expose explicit acquire,
presentation retry, presentation-completion retry, cancellation, and
cancellation-completion operations. Normal window construction remains inert.
Neither platform acquires or presents unless a diagnostic caller requests it.

The adapters retain their existing window and drawable checks for a new
transaction. A completion or cancellation retry uses the already retained
generation and does not let resize or minimize strand submitted work. macOS
keeps its main-thread, exact Cocoa-window, exact Metal-layer, CGL, and OpenGL
manager invariants.

## Build graph and production isolation

The implementation remains inside the existing default-off Vulkan diagnostic
archives. SDL ownership requires `LL_VULKAN_SDL_WSI`; macOS ownership requires
`LL_VULKAN_MACOS_WSI`; both require the runtime test graph. The production
OpenGL window path and renderer selector are unchanged.

With all six renderer experiments disabled, the production viewer remains
OpenGL. Fresh Linux and macOS graphs must omit every Stage 45 diagnostic target,
object, symbol, direct import, dependency, artifact name, binary marker, and
renderer payload. The pre-existing requirements probe and Chromium's packaged
Vulkan support remain accepted baseline content, not evidence that the viewer
links or selects Vulkan.

## Validation evidence

The SHA-256 digest of the lexicographically ordered, newline-terminated
`sha256sum` manifest for the twenty-three Stage 45 source and test files is
`ea3ff38e0b78c39d607e816773f0da89d332424ce1b0f15eee596507a35f080e`.
This decision note is excluded because including its own digest would be
self-referential.

On Linux, all eleven focused global-dispatch, physical-device, logical-device,
configuration, swapchain, image-view, frame-slot, parent, requirements,
SDL-owner, and SDL-WSI routes pass. Direct totals are 6, 8, 9, 11, 6, 11, 25,
59, 7, 18, and 1, for 161 tests with zero failures. The enabled viewer and
appearance targets compile with warnings as errors. Required-validation X11
completes two owned acquire-to-present transactions through lavapipe with zero
validation messages.

The fresh Linux all-six-off graph builds the viewer, appearance archive, and
package. It executes 1,175 warning-as-error compile commands and retains 1,173
objects. The staged tree contains 116 directories, 6,336 regular files, five
symlinks, and 17 ELF files. Its 6,457-entry archive extracts byte for byte to the
same manifest. The archive SHA-256 is
`5290e22bc10600755f569606a9a16efca128413a9c8ede5b677219788bf362a3`.

Stage 45 target, graph, source, object, symbol, import, dependency, name,
SPIR-V, and payload hits are zero in that Linux delivery. The four
Vulkan-named package entries are Chromium's accepted loader, SwiftShader
library, and two ICD manifests. Their hashes match the package cache. Embedded
SPIR-V magic occurs only in the accepted CEF, GLES, and SDL files.

On macOS, the full enabled Xcode `ALL_BUILD` graph compiles with warnings as
errors after the isolated configure is pointed at the established Python
environment containing `llsd`. The first post-build attempt selected the system
Python and failed only while importing `viewer_manifest.py`; the corrected
incremental build succeeds. No source change or warning relaxation was needed.
The viewer, appearance archive, and ten relevant Vulkan and platform archives
are universal `x86_64 arm64`; focused test executables are intentionally native
arm64.

The same eleven focused routes pass on Vega with direct totals 6, 8, 9, 11, 6,
11, 25, 59, 7, 16, and 1, for 159 tests with zero failures. The explicitly
enabled hidden Cocoa route records its opt-in environment and loader paths in
the same evidence as the test result. Loader diagnostics show that MoltenVK and
`libVkLayer_khronos_validation.dylib` are loaded and that
`VK_LAYER_KHRONOS_validation` is inserted. The non-skip test completes two
presentation transactions, reports zero validation messages, and leaves CGL
and `gGL` unchanged. The relevant archives and focused executables have zero
direct undefined Vulkan imports. The executables have zero Vulkan or MoltenVK
dynamic dependencies.

The fresh macOS all-six-off graph has 178 Xcode targets. The explicit universal
viewer and appearance build, full `ALL_BUILD`, and one-job package target pass
with warnings as errors. Together the two compilation passes emit 2,558
`CompileC` steps and leave 2,558 objects. The staged app contains 215
directories, 6,707 regular files, no symlinks, and 367 Mach-O files.

Stage 45 graph, target, source-path, object-name, object-content, packaged-name,
packaged-content, and symbol hits are zero. Every Mach-O file has zero undefined
Vulkan import and zero Vulkan or MoltenVK dynamic dependency. The two
Vulkan-named files are Chromium's existing `libvk_swiftshader.dylib` and
`vk_swiftshader_icd.json`. Their hashes and the complete 69-file CEF framework
manifest match the accepted Stage 43 package exactly.

No named SPIR-V file is present. The three embedded SPIR-V byte-pattern hits are
the accepted CEF binary, `libGLESv2.dylib`, and Greek locale pack, with exact
counts and hashes matching the prior package. The package script completes its
base-package step but retains no DMG, so the staged app is the audited delivery
payload.

The macOS host uses macOS 26.6.2, Xcode 26.6, macOS SDK 26.5, Apple clang
21.0.0, CMake 4.1.2, Python 3.9.6 from the isolated build environment, Vulkan
headers and validation API 1.4.357, and MoltenVK 1.4.1. Signing is disabled at
both viewer and Xcode levels.

No viewer was launched. No login, world connection, benchmark, performance
timing, or timing retention occurred on either platform. No credential entered
the source, build, test, or evidence trees.

## Independent review corrections

Adversarial review tightened ambiguous failure states before final validation.
An out-of-range acquired index remains non-resettable. An unexpected present
result becomes `PresentationIndeterminate`. A partially failed two-fence reset
becomes `FenceResetIndeterminate`. A failed release becomes
`ReleaseIndeterminate` and cannot be duplicated. Presentation allocation
failure remains safely retryable.

Review also exercised both binary-semaphore cancellation payloads, the repair
of the second unsignaled fence, queue-submit device loss, present completion,
parent reset refusal, exact image indexing, platform ownership, and child-first
teardown. Independent core, lifetime, WSI, failure, platform, evidence, scope,
privacy, and exact-index reviews report no remaining defect.

## Explicit deferrals

- Windows build and native execution;
- swapchain replacement after resize, minimize, suboptimal, or out-of-date;
- surface reconstruction and complete device-loss recovery;
- defined image contents, clear operations, attachments, rendering, and draws;
- shaders, pipelines, descriptors, uploads, and texture sampling;
- multiple frames in flight;
- readback, capture, fixed-scene parity, and memory-stability soak;
- connection to the production graphics selector or replacement of OpenGL;
- an OpenGL-to-Vulkan compatibility layer;
- benchmark, image-parity, timing, or performance claims.
