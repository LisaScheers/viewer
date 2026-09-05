# Stage 44 reusable empty Vulkan submission decision

## Decision

Run one synchronous empty command-buffer submission through the exact Stage 43
frame slot, then repeat it to prove that the slot is reusable. The operation
waits for the slot fence, resets and records the primary command buffer, resets
the fence, submits exactly one command buffer, and waits for completion.

The submission does not acquire a swapchain image. It does not wait on or
signal a semaphore. The existing image-available semaphore remains untouched.
No image layout, presentation state, or production OpenGL path changes in this
stage.

This is a bounded part of master Stage 3. Windows remains excluded by user
direction. Stage 44 adds no Win32 integration and makes no Windows build or
runtime claim.

## Exact execution dispatch

`VulkanSwapchainFrameSlotGeneration` retains six commands for the exact live
logical device and unified graphics and presentation queue:

- `vkWaitForFences`;
- `vkResetCommandBuffer`;
- `vkBeginCommandBuffer`;
- `vkEndCommandBuffer`;
- `vkResetFences`;
- `vkQueueSubmit`.

The resolver obtains `vkGetDeviceProcAddr` through the retained
`vkGetInstanceProcAddr`, instance, and device. It resolves all six commands into
temporary storage before publishing any of them. A missing command reports its
exact identity and leaves Vulkan object state unchanged. No Vulkan command is
statically imported and no loader link edge enters the core archive.

Resolution is separate from execution. The parent can therefore authenticate
the complete ownership chain again after dispatch resolution and before the
first fence wait or command-buffer mutation.

## Recording and submission order

One normal cycle performs these calls in this exact order:

1. Wait indefinitely for the single submission fence with `waitAll` true.
2. Reset the primary command buffer with flags zero.
3. Begin it with the required structure type, flags zero, and no inheritance
   information.
4. End the empty command buffer.
5. Reset the submission fence.
6. Submit one `VkSubmitInfo` containing exactly one command buffer and no wait
   semaphores, signal semaphores, or stage-mask input.
7. Wait indefinitely for the submission fence again.

The fence remains truthful through every pre-submit failure. It is reset only
after recording succeeds and immediately before queue submission. The normal
native tests complete this cycle twice with the same command buffer and fence.

## State and failure contract

The frame slot has four explicit dispositions:

- `Reusable` permits a new empty submission;
- `ResetRequired` permits teardown but rejects another submission;
- `Pending` prevents direct and transitive teardown because the queue may still
  use the command buffer or fence;
- `DeviceLost` is terminal for submission but permits teardown.

An initial fence-wait failure leaves the slot reusable unless it reports device
loss. Reset, begin, end, and fence-reset failures require reset unless they
report device loss. A non-device-loss queue-submit failure requires reset.

Vulkan permits `vkQueueSubmit` to report device loss while submitted resources
may still count as in use. Stage 44 therefore keeps that case pending. A later
completion wait must return success or device loss before the slot becomes
terminally device-lost. Any other completion-wait error leaves the slot pending
and retryable.

A successful queue submission also remains pending until the completion wait
returns success. Success restores `Reusable`; device loss produces
`DeviceLost`; every other wait result preserves `Pending`.

The explicit completion-retry operation performs only the retained fence wait.
It does not resolve dispatch again, record another command buffer, reset the
fence, or submit more work.

## Parent ownership and reset safety

`VulkanInstanceGeneration` authenticates the exact global dispatch, instance,
native-window generation, surface, physical selection, logical device, queue,
swapchain configuration, swapchain, image generation, frame-slot handles, and
object identities before execution. It checks the instance-owner and window
callbacks before and after dispatch resolution. No callback or allocation runs
after execution starts.

Every direct and broader reset now returns whether teardown was safe. A pending
frame slot stops the complete reset chain before any child or parent is
released. The chain remains intact through image views, swapchain,
configuration, device, physical selection, surface, instance, and global
dispatch. This makes a later completion retry possible without reconstructing
ownership.

The frame-slot reset itself also refuses to destroy a pending fence or command
pool. SDL destruction and move assignment terminate when the caller violates
that precondition. The macOS diagnostic owner keeps the same contract together
with its existing main-thread requirement.

## Window integration

The default-off SDL and macOS diagnostic owners expose two explicit methods:

- `roundTripEmptySwapchainFrameSlot()`;
- `retryEmptySwapchainFrameSlotCompletion()`.

Normal window construction remains inert. Neither platform submits work unless
the diagnostic caller requests it.

A new submission checks current drawable geometry. A completion retry uses the
extent retained by the live swapchain generation and does not query current
geometry. A resize or minimize therefore cannot prevent retirement of already
submitted work. SDL still authenticates the exact window owner. macOS still
requires the main thread and exact Cocoa and Metal-layer identity.

## Build graph and production isolation

The execution code stays inside the existing default-off Vulkan diagnostic
archives. SDL ownership remains behind `LL_VULKAN_SDL_WSI`; macOS ownership
remains behind `LL_VULKAN_MACOS_WSI`. Both still require the corresponding
runtime test graph.

With all six renderer experiments disabled, the production viewer remains
OpenGL. The fresh Linux and macOS graphs contain no Stage 44 target, object,
artifact name, symbol, direct import, dependency, API string, or renderer
payload.

## Validation evidence

The SHA-256 digest of the lexicographically ordered, newline-terminated
`sha256sum` manifest for the fourteen Stage 44 source and test files is
`2fe18bdd2ffbe0a5cf6b7d1c17422ac4cb545a874ef0783fc7c6f1d40614105b`.
This decision note is excluded because including its own digest would be
self-referential.

On Linux, the enabled viewer and appearance targets compile with warnings as
errors. All eleven global-dispatch, physical-device, logical-device,
configuration, swapchain, image-view, frame-slot, parent, requirements,
SDL-owner, and SDL-WSI routes pass. Direct totals are 6, 8, 9, 11, 6, 11, 15,
58, 7, 17, and 1 with zero failures.

The required-validation hidden X11 route uses Mesa lavapipe. It performs two
empty submit-and-wait cycles through the owned SDL path, preserves the exact
nonnull image-available semaphore, resets the child first, and reports zero
validation messages. The two core archives have no direct undefined Vulkan
command import. The core, parent, SDL-owner, and native SDL test executables
have no Vulkan or MoltenVK dynamic dependency.

The genuinely fresh Linux all-six-off graph builds `secondlife-bin`,
`llappearance`, and `llpackage`. It contains 1,173 objects. The staged package
has 6,336 regular files, 116 directories, five symlinks, and 17 ELF files. The
archive has 6,457 entries. Its extracted tree has the same counts and zero
differences from staging.

Stage 44 graph, target, artifact-name, packaged-name, content, and symbol hits
are zero in that Linux build. Undefined Vulkan imports, Vulkan or MoltenVK
dependencies, named SPIR-V files, and valid SPIR-V headers are also absent. The
four Vulkan-named files are Chromium's accepted SwiftShader payload. Embedded
SPIR-V magic occurs only in the accepted `libGLESv2`, SDL, and CEF files.

GCC 15.3 emitted an optimizer-dependent `-Warray-bounds` false positive in the
unrelated legacy `llpaneloutfitedit.cpp` translation unit. Its speculative
devirtualization selected the destructor of a larger sibling class for a known
`LLUpdateAppearanceOnDestroy` allocation. That one file compiled with
`-fno-devirtualize-speculatively`; `-Werror` remained active and no warning was
disabled. No source change entered Stage 44 for this host-build exception.

On macOS, two independent source roots reproduce the fourteen-file digest
before and after validation. Xcode builds all 225 enabled targets as universal
`x86_64` and `arm64` ReleaseOS products with warnings as errors. The viewer,
`llappearance`, and all nine relevant Vulkan and macOS-owner archives are
universal. The same eleven focused routes pass with direct totals 6, 8, 9, 11,
6, 11, 15, 58, 7, 16, and 1.

The explicitly enabled hidden Cocoa route runs through MoltenVK with required
validation. It performs two reusable empty submissions, preserves the exact
nonnull image-available semaphore, reports zero validation messages, resets the
frame slot before image and parent ownership, and leaves CGL and `gGL`
unchanged. All nine relevant archives have zero direct undefined Vulkan
imports. All eleven signed focused executables have zero direct Vulkan imports
and zero Vulkan or MoltenVK dynamic dependencies.

The fresh universal macOS all-six-off graph has 178 targets and builds the
viewer, appearance archive, and package post-build with warnings as errors. The
staged app contains 6,707 regular files and 367 Mach-O files. Stage 44 graph,
target, build-path, object, artifact, packaged-name, binary-content, and symbol
hits are all zero. Every Mach-O has zero undefined Vulkan import and zero
Vulkan or MoltenVK dependency. Four Dullahan helper names contain parentheses,
which `otool-classic` rejects as direct paths; SHA-identical hard-link aliases
produce the same zero-hit result.

The macOS package has no named Vulkan, MoltenVK, or SPIR-V addition and no valid
SPIR-V header. Embedded SPIR-V magic appears only in the existing CEF framework
and `libGLESv2`. The complete 69-file CEF framework matches the accepted Stage
41 SHA manifest. Chromium's existing `libvk_swiftshader.dylib` and
`vk_swiftshader_icd.json` also match their accepted Stage 41 hashes. The package
script completed its base-package step but retained no DMG, so the staged app
is the audited payload.

The macOS host used Xcode 26.6, macOS SDK 26.5, Apple clang 21.0.0, CMake 4.3.4,
Python 3.13.15, Autobuild 3.10.2, Vulkan headers and validation API 1.4.357,
MoltenVK headers 1.4.1, glslang 16.4.0, and SPIRV-Tools v2026.3.

No viewer was launched. No login, world connection, benchmark, performance
timing, or timing retention occurred on either platform. Vega's exact isolated
source, build, log, alias, and result tree was removed after the final digest
and configuration recheck. No credential was created or transferred.

## Independent audit corrections

Adversarial review corrected two lifecycle issues before final validation.
First, queue-submit device loss now remains pending until a fence wait retires
possible queue use. Second, a completion retry uses the retained swapchain
extent instead of current window geometry, so resize or minimize cannot block
safe teardown.

Independent core, parent, platform, lifetime, failure, evidence, scope,
privacy, and exact-index reviews report no remaining defect. Focused format
checks and `git diff --check` pass. The exact intended change contains fourteen
source and test files plus this decision note. The two unrelated untracked HTML
documents remain excluded.

## Explicit deferrals

- Windows build and native execution;
- `vkAcquireNextImageKHR` and acquired-image ownership;
- image-available semaphore consumption;
- image layout transitions and barriers;
- presentation-wait semaphore ownership;
- `vkQueuePresentKHR` and presentation completion;
- resize, minimize, out-of-date, suboptimal, and swapchain replacement policy;
- render passes, dynamic rendering, framebuffers, pipelines, and draws;
- multiple frames in flight, upload, readback, and capture;
- connection to the production graphics selector or replacement of OpenGL;
- an OpenGL-to-Vulkan compatibility layer;
- benchmark, image-parity, timing, or performance claims.
