# Stage 51 Vulkan presentation-draw decision

## Decision

Add one explicit diagnostic operation that records, submits, presents, and
retires a fixed graphics draw through the exact Stage 48 presentation target
and Stage 50 graphics pipeline.

The frame slot gains a fourth recording mode beside layout-only presentation,
transfer clear, and render-pass clear. Draw mode begins the existing classic
render pass, binds the retained graphics pipeline, sets a full-extent dynamic
viewport and scissor, records one three-vertex and one-instance draw, and ends
the pass.

The operation remains behind the default-off Vulkan diagnostic graph. It does
not change the production OpenGL renderer, renderer selection, ordinary window
construction, or `swapBuffers()` behavior. Windows remains excluded by user
direction.

## Viewport and scissor admission

Swapchain configuration now rejects an extent unless the exact positive-height
viewport and zero-offset scissor used by draw mode are legal.

The appended `SelectedImageExtentExceedsViewportLimits` result retains all
earlier enum ordinals and has ordinal 27. Admission checks:

- width and height against `maxViewportDimensions`;
- width and height against the signed-safe scissor limit;
- finite viewport bounds and finite converted dimensions;
- a bounds range containing zero, width, and height;
- exact unsigned-integer-to-float round trips;
- the converted dimensions against the reported viewport limits.

Focused tests reject `UINT32_MAX`, the first inexact float extent `16,777,217`,
and `INT32_MAX + 1`. They admit ordinary exact boundaries and the signed-safe,
exactly representable extent `2,147,483,520`. Rejection happens before surface
format or present-mode enumeration.

## Dispatch and exact provenance

Draw-mode dispatch resolves every existing acquisition, recording, submission,
presentation, and cancellation command plus:

- `vkCmdBindPipeline`;
- `vkCmdSetViewport`;
- `vkCmdSetScissor`;
- `vkCmdDraw`.

All commands resolve through retained function pointers under
`VK_NO_PROTOTYPES`. No Vulkan loader link dependency is added. The complete
dispatch candidate resolves before image acquisition. A missing command or
provenance change publishes none of it.

Draw mode authenticates the exact target and pipeline owner identities. It also
retains the non-null pipeline-layout and graphics-pipeline handle snapshots.
The instance operation snapshots separate ownership epochs for the target,
pipeline, and frame slot, then repeats exact identity, handle, provenance,
epoch, instance-owner, and window-generation checks after callbacks and
dispatch resolution.

Separate epochs distinguish target, pipeline, and frame-slot ABA replacement
even when an implementation reuses opaque handles. Failure precedence remains
target, then pipeline, then frame slot. A callback that resets the complete
owner also fails safely without dereferencing a cleared global-dispatch
generation.

Layout-only, transfer-clear, and render-pass-clear modes remain usable without
a presentation pipeline. They do not resolve or invoke the four draw commands.

## Draw transaction

The instance operation copies and validates the clear color before freshness
callbacks. After acquisition, the frame slot authenticates the retained target
and pipeline again.

The recorded pass sequence is exactly:

1. begin the exact render pass with the acquired image's same-index framebuffer;
2. bind the exact graphics pipeline at `VK_PIPELINE_BIND_POINT_GRAPHICS`;
3. set viewport `{0, 0, width, height, 0, 1}`;
4. set a zero-offset scissor covering the same full extent;
5. call `vkCmdDraw(command_buffer, 3, 1, 0, 0)`;
6. end the render pass.

The existing acquire-to-color and color-to-present barriers remain unchanged.
Submission waits for image availability at color-attachment output, signals
the presentation-ready semaphore, presents the paired image, and retires the
existing submission and presentation-completion fences.

A target or pipeline invalidation after acquisition leaves the frame slot in
`ImageAcquired` with the exact image index. The existing cancellation route
drains the acquisition semaphore and releases that image. Presentation retry,
completion retry, and cancellation do not consult a target or pipeline after
work has started.

Acquired or pending draw work prevents frame-slot, pipeline, target, image,
swapchain, configuration, logical-device, and transitive parent teardown.
Completed or cancelled work restores child-first teardown in frame-slot,
pipeline, target order.

New public command, operation, and error values are appended, preserving
earlier ordinal values.

## Parent and platform routes

`VulkanInstanceGeneration` exposes one guarded draw operation. Target,
pipeline, and frame-slot publication and reset now update their own ownership
epochs as well as the aggregate transition epoch. Move construction transfers
all three epochs and disarms the source.

The SDL diagnostic wrapper resamples positive backing-pixel geometry before
forwarding the draw request. Its native hidden-X11 proof exercises the same
instance draw operation directly, while fake-platform tests cover the wrapper.

The macOS diagnostic wrapper requires the main thread, refreshes Cocoa
geometry, rejects native identity changes, and forwards the exact backing-pixel
extent. Native execution preserves the current CGL context, global OpenGL
manager state, native-window identity, and window count.

No production `LLWindowSDL` hook, viewer setting, graphics selector, resize
event, OpenGL context owner, or ordinary presentation route changes.

## Evidence boundary

Stage 51 proves that the exercised Vulkan implementations accept the exact
command recording, submission, presentation, and retirement transaction before
and after a changed-extent rebuild.

It does not prove that a shader produced green pixels, that the full target was
covered, that compositor or display output is correct, or that a scene
rendered. No readback, capture, screenshot comparison, fixed-scene comparison,
upload, descriptor, material, tonemap, UI composition, CPU timing, GPU timing,
or performance claim enters this stage.

## Build graph and production isolation

The implementation remains reachable only through `LL_VULKAN_RUNTIME_TEST` or
`LL_VULKAN_TONEMAP_TEST`. SDL ownership additionally requires
`LL_VULKAN_SDL_WSI`; macOS ownership additionally requires
`LL_VULKAN_MACOS_WSI`.

The frame-slot diagnostic library now links the presentation-pipeline library,
which already links the target and earlier swapchain generations. This retains
an acyclic child-to-parent dependency chain.

Fresh all-six-off Linux and macOS builds omit every Stage 51 project source,
target, object, symbol, operation marker, exact draw-command marker, import,
dependency, and renderer payload. Existing Chromium, SDL, Vulkan loader, GLES,
and SwiftShader content remains accepted third-party baseline and does not
select or link the diagnostic renderer.

## Validation evidence

The SHA-256 digest of the sorted, newline-terminated `sha256sum` records for
the 22 Stage 51 source, build, and test files is
`016baab3b48e8d75c8118badd74167c9bf89dab714839979fc3a85e3fb1b552b`.
Linux and macOS independently reproduced it. This decision note is excluded
because including its own digest would be self-referential.

The frozen code snapshot starts from
`b5a1f207108fcbfc1333da3568fa9dd03bb88a33`. Its full-index binary diff has
SHA-256
`c7c5c0d84fc560ed309a0444ec48b37fcce55ea685071eb7f48295535c957764`;
its logical tree is `957a1382bc0909a4a1f77f273de42339b2cdae03`; and its tracked-entry
manifest has SHA-256
`e74893dfcd60cb2b10539c370aee7d6f20a82b2fd875f56a8900af6b695e2a23`.
The deterministic cross-host archive is 23,493,609 bytes, contains 9,596
tracked entries, and has SHA-256
`b4afe24220d51b8a70296ca9ded6098f3ea79b14d8d8cabdbda12845fc12d2cb`.
Unrelated untracked documents are absent.

On Linux, all thirteen focused global-dispatch, physical-device,
logical-device, configuration, swapchain, image-view, presentation-target,
presentation-pipeline, frame-slot, parent, requirements, SDL-owner, and
SDL-WSI routes pass. Direct totals are 6, 8, 9, 12, 6, 11, 7, 5, 40, 96, 7,
24, and 1, for 231 test cases with zero failures.

The required-validation hidden X11 route uses Mesa lavapipe and the Khronos
validation layer. It completes the legacy transfer-clear and render-pass-clear
controls plus one diagnostic draw, rebuilds the complete child chain at a
changed positive extent, and repeats all three operations. Both draws present
and retire, and validation reports zero messages.

The enabled Linux viewer, appearance utility, default full graph, and explicit
serial package target pass with warnings as errors.

The fresh Linux all-six-off graph has 1,766 Ninja targets and 1,191 compile
entries. It produces 1,189 object files. The only Vulkan-named compile source
is the expected always-built `llwindowvulkanrequirements.cpp`; all diagnostic
instance, configuration, swapchain, image, target, pipeline, frame-slot, and
platform-wrapper sources and targets are absent.

The built viewer, appearance utility, and staged viewer contain no Stage 51
operation or class marker and no exact bind, viewport, scissor, or draw command
marker. The staged viewer has no Vulkan or SwiftShader dependency and no
undefined `vkCmd*` import.

Linux staging contains 116 directories including the root, 6,336 regular
files, and five symbolic links. It contains no standalone SPIR-V file, project
Vulkan shader directory, or Stage 51-named payload. Its only Vulkan-named
members are the established web/media baseline: the Vulkan loader, SwiftShader
library, and two copies of the SwiftShader ICD manifest.

Generic draw-command strings occur only in existing CEF, GLES, SDL,
SwiftShader, and Vulkan-loader inputs. Their immutable package provenance,
Build IDs, and SONAMEs match the accepted baseline. No staged ELF file has a
Vulkan or SwiftShader dynamic dependency.

The accepted Linux archive is
`Second_Life_Test_26_4_0_54504_x86_64.tar.xz`, with SHA-256
`6639bf24bf073c85b744fb34d1b8c5c012d29799480d203b7738bee664951dee`.
Its xz integrity check passes. It contains 6,457 unique members, no duplicate
or unsafe path, and the exact staged path set. Critical viewer, SDL, CEF, GLES,
SwiftShader, loader, and ICD member hashes match staging.

The disabled Linux build viewer has SHA-256
`a1af4f73640d0a1e81bf722dd70824cb7fb11cd7a2dd7b84bbee71a812fd36c6`;
the appearance utility has SHA-256
`cf8f26a2e0376bcb5a75a4a5969b872d648c3d9a802b4dc1fcfb4a401607855f`;
and the staged viewer has SHA-256
`c8679cbf70bddbe3196a5584ba44b9163a6303c604be68c30e542bc58cf380df`.

On macOS, the same thirteen focused routes pass in a universal ReleaseOS graph
with warnings as errors. Direct totals are 6, 8, 9, 12, 6, 11, 7, 5, 40, 96,
7, 22, and 1, for 230 test cases with zero failures.

The required-validation hidden Cocoa route loads MoltenVK and the Khronos
validation layer. It completes one draw at 1,280 by 720 backing pixels,
rebuilds the complete same-surface child chain at 1,440 by 810, and completes a
second draw. Validation reports zero messages. The current CGL context, global
OpenGL manager, Cocoa owner identity, and window count remain unchanged.

The enabled universal viewer, appearance library, `ALL_BUILD`, and explicit
package target pass with warnings as errors. Viewer and appearance outputs
contain both `x86_64` and `arm64` slices. Developer signing is disabled. The
viewer carries only a linker ad-hoc signature and has no team identifier.

The fresh macOS all-six-off graph has 178 targets and 660 warnings-as-errors
tokens. Universal viewer, appearance, `ALL_BUILD`, and serial package targets
pass. The final isolation audit reports zero errors and zero warnings, with
evidence digest
`912dc452a5a221da8351990b761a192b3c51d8f59a51d17b09f9e89aef832ebe`.

The audit covers 2,560 objects, 134 archives, 734 dynamic libraries, 73
build-produced archives, 302 build Mach-O files, and 367 packaged Mach-O files.
No Stage 51 target, source, symbol, operation marker, exact draw command, named
shader output, standalone SPIR-V, exact project shader payload, undefined
Vulkan import, or Vulkan or MoltenVK load dependency survives.

The packaged application contains 6,707 files. Its only Vulkan-named files are
the established SwiftShader library and ICD manifest. Generic command strings
occur only in CEF, GLES, and SwiftShader inputs that are byte-identical between
the enabled and disabled builds. The first audit classified the short token
`RenderPassDraw` as project-unique; CEF contains that generic token. Moving it
to strict byte-matched third-party classification and rerunning the complete
audit passes with project-owned and non-CEF output still requiring zero hits.

All 9,596 tracked extracted files remain byte, type, and mode identical after
the macOS builds. Package-created untracked assets remain inside the disposable
tree. The wake guard and disposable source, build, package, and evidence roots
were removed after validation.

The Linux GStreamer edge needed its immutable development include added to the
generated Ninja rule. The enabled Linux SLPlugin deployment edge needed
immutable toolchain-library search paths in its generated rule. The accepted
package used the compiler wrapper's real `strip`. Source and CMake remained
unchanged, and warnings as errors stayed active.

The native Linux environment supplied the dynamically loaded X11 libraries and
the Vulkan loader used by the current lavapipe driver. These were build and
execution environment corrections, not source changes.

No viewer was launched. No login, world connection, benchmark, renderer timing,
timing retention, readback, capture, image comparison, or developer signing
occurred. No credential, private network address, private result, or
machine-specific path enters source or the committed evidence.

## Independent review

Review verified the exact viewport and scissor limits, append-only ordinals,
complete dispatch cutoff order, atomic publication, command order and
arguments, same-index framebuffer selection, target and pipeline identity,
owner-specific ABA detection, post-acquire cancellation, retry independence,
move/reset behavior, child-first teardown, legacy-mode independence, platform
freshness, diagnostic-only build placement, and Windows exclusion.

An independent instance review found a possible dereference of a disengaged
global-dispatch optional after a final freshness callback reset the complete
owner. The guard now checks engagement first. A deterministic regression
resets the full owner during the operation, and re-review found no remaining
instance-path defect.

Final focused source review found no remaining concrete viewport, frame-slot,
instance, platform, CMake, or test defect. Repository formatting and
`git diff --check` pass.

Residual risk remains in the low-level borrowed-parent and externally
serialized host-lifetime contract. Rare device-loss and native failure branches
remain fake-tested because exercised drivers cannot reliably force them. The
SDL wrapper has fake-platform coverage while its native proof calls the same
instance transaction directly.

## Explicit deferrals

- readback, capture, pixel classification, and screenshot comparison;
- proof of green output, full-target coverage, visual parity, or scene output;
- upload buffers, textures, descriptors, materials, tonemap, and UI composition;
- pipeline-cache ownership and persistent shader compilation;
- surface reconstruction and complete device-loss recovery;
- multiple frames in flight and production resize, minimize, suboptimal,
  out-of-date, fullscreen, and display-change event wiring;
- memory-stability soak and measured CPU or GPU timing;
- connection to the production graphics selector or replacement of OpenGL;
- an OpenGL-to-Vulkan compatibility layer;
- Windows build and native execution;
- benchmark, image-parity, timing, or performance claims.
