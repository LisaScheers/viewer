# Stage 49 Vulkan clear-only presentation-pass decision

## Decision

Add one explicit, loader-neutral operation that records and presents a clear-only
classic render pass through the exact Stage 48 presentation target. The new
operation is a third frame-slot recording mode beside layout-only presentation
and the existing transfer clear. It reuses the established acquire, submit,
present, retry, cancellation, two-fence retirement, rebuild, and teardown state
machine.

The operation remains an explicit diagnostic route behind the default-off
Vulkan graphs. It does not replace or modify the production OpenGL renderer.
Windows remains excluded by user direction.

## Corrected acquire synchronization

The transfer-clear operation waits on the image-acquired semaphore at
`VK_PIPELINE_STAGE_TRANSFER_BIT`. Its first image barrier now also starts at
`VK_PIPELINE_STAGE_TRANSFER_BIT`, instead of top of pipe, and ends at transfer.
This orders the transition after the semaphore wait while preserving the
existing access masks, layouts, clear, submission, presentation, and recovery
API.

The render-pass operation waits at
`VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT`. Its first barrier runs from
color-attachment output to color-attachment output, uses no source access,
enables color-attachment writes, and transitions the acquired image from
undefined to color-attachment-optimal layout.

After the pass, a second barrier runs from color-attachment output to bottom of
pipe, makes color-attachment writes available, and transitions the image from
color-attachment-optimal to presentation layout.

## Dispatch and recording modes

The internal boolean clear selection is replaced by three explicit modes:
layout-only, transfer-clear, and render-pass-clear. Existing public layout-only
and transfer-clear operations remain available and keep their established
state-machine behavior.

Render-pass dispatch resolves `vkCmdBeginRenderPass` and
`vkCmdEndRenderPass` together with the already required acquire, barrier,
command-buffer, submit, fence, present, and cancellation commands. The complete
candidate publishes atomically. Missing either render-pass command fails before
image acquisition and cannot leave a partial mode. The render-pass mode does
not resolve or call `vkCmdClearColorImage`.

All commands continue to be obtained through resolved function pointers under
`VK_NO_PROTOTYPES`. No Vulkan loader link dependency is introduced.

## Exact target provenance and lifetime

The frame slot now retains borrowed identities for the exact logical device,
configuration, swapchain, image generation, and optional presentation target.
Render-pass dispatch authenticates all four parent generations plus the exact
target identity, render pass, format, extent, framebuffer count, and every
non-null same-generation framebuffer before publication.

The instance operation copies and validates normalized RGBA input before
callbacks. It snapshots the ownership epoch and exact target, resolves the
complete candidate, then repeats parent, target, handle, and framebuffer checks
before acquisition. Callback mutation, ABA replacement, stale requests,
invalid input, and incomplete targets cannot start work against the wrong
generation.

After acquisition, the frame slot authenticates the target again and selects
only `target.framebuffer(acquired_image_index)`. A missing or invalid target at
that point leaves the slot in `ImageAcquired` with the exact image index, so the
existing cancellation operation can release or retire the obligation.
Presentation retry, completion, and cancellation do not require a still-valid
target once work has started.

The target is borrowed rather than owned by the frame slot. The caller must
keep it alive and externally serialized until the slot returns to reusable and
is reset. The instance owner enforces that order by refusing target teardown
while the younger slot retains acquired or pending work.

## Clear-only render-pass transaction

The operation acquires one presentable image, records the acquire-to-color
barrier, and begins the exact Stage 48 render pass over the target's full image
extent. The render-pass begin record uses the same-index framebuffer, one clear
value copied from the validated RGBA input, and inline subpass contents.

The pass ends immediately. No pipeline, shader, descriptor, viewport, scissor,
vertex input, geometry, texture, draw, dispatch, query, copy, or readback is
recorded. The color-to-present barrier follows, and submission waits on the
acquire semaphore at color-attachment output before signaling the existing
presentation-ready semaphore. Presentation and retirement then use the
existing state machine.

## Parent and platform routes

The instance generation exposes one guarded render-pass-clear operation.
Legacy layout-only and transfer-clear operations do not require presentation
target ownership.

The SDL diagnostic owner resamples positive backing-pixel geometry before the
new operation. The macOS diagnostic owner requires the main thread, refreshes
native geometry, and rejects identity changes before forwarding. Both wrappers
remain explicit test and diagnostic entry points. No production `LLWindowSDL`
hook, `swapBuffers()` route, resize event, renderer-selection path, viewer
setting, or OpenGL ownership changes.

## Failure and recovery

Invalid clear input, missing or stale parents, an incomplete presentation
target, missing dispatch, callback mutation, and pre-acquire generation changes
fail before image acquisition. Command-buffer, submission, presentation, and
fence failures retain their established typed results and disposition rules.

A target failure detected after acquisition retains the acquired image and is
recoverable through cancellation. Once submission or presentation begins,
retry and completion remain independent of target freshness. Move construction
transfers all parent identities, target identity, render-pass commands, and
state. Reset clears them only after all native obligations are retired.

New public command and operation codes are appended, so prior ordinal values
remain stable.

## Evidence boundary

This stage proves one real clear-only render-pass transaction completes through
Vulkan and that its resource and synchronization structures are accepted by
the exercised implementations. Native validation cannot prove that a pixel
changed, that a captured image is correct, or that a scene rendered.

No readback, capture, screenshot comparison, graphics pipeline, shader module,
draw, fixed scene, texture upload, material path, descriptor, production
renderer selection, memory soak, CPU timing, GPU timing, or performance claim
enters this stage.

## Build graph and production isolation

The implementation remains reachable only through the default-off Vulkan
runtime-test or tonemap graphs. SDL ownership additionally requires
`LL_VULKAN_SDL_WSI`; macOS ownership additionally requires
`LL_VULKAN_MACOS_WSI`. No Windows source or test changes.

Fresh all-six-off Linux and macOS builds must omit the modified Vulkan sources,
objects, targets, symbols, strings, imports, dependencies, names, and renderer
payload. Existing Chromium Vulkan and SwiftShader payloads are accepted
baseline content and do not select or link the diagnostic renderer.

## Validation evidence

The SHA-256 digest of the newline-terminated `sha256sum` records for the fifteen
Stage 49 source, build, and test files, after sorting the complete records
lexicographically, is
`db22e0901b2bf46ded276c6cfe48c0f446dea274772fa17c3e87b5c9d5c39627`.
This decision note is excluded because including its own digest would be
self-referential.

On Linux, all twelve focused global-dispatch, physical-device, logical-device,
configuration, swapchain, image-view, presentation-target, frame-slot, parent,
requirements, SDL-owner, and SDL-WSI routes pass. Direct totals are 6, 8, 9,
11, 6, 11, 7, 35, 84, 7, 22, and 1, for 207 tests with zero failures. The
enabled viewer, appearance utility, default full build, and explicit serial
package targets pass with warnings as errors.

The required-validation hidden X11 route uses Mesa lavapipe. It completes one
legacy transfer clear and one render-pass clear, rebuilds the complete child
chain, then completes the same pair against the rebuilt generations. Explicit
teardown succeeds and validation reports zero messages. The viewer is not
launched.

The fresh Linux all-six-off graph has 1,766 Ninja targets and 1,191 compile
entries: 1,189 object files and two precompiled-header entries. Every entry
retains `-Werror`. The viewer, appearance utility, default full build, and
explicit serial package target pass. All Stage 49 source, header, test, target,
object, and unique marker hits are zero. The only Vulkan-named compile entry is
the expected always-built requirements probe.

The compiled-delivery scan covers 1,221 inputs: 1,189 objects, 24 archives, and
eight target ELF files. Stage 49 markers, begin/end-render-pass strings,
Vulkan or MoltenVK dynamic dependencies, and undefined `vk*` symbols are all
absent. Its path-manifest SHA-256 is
`efb6a7c4f8f8c0740f3fd7147042c980dd62f94df5fe1ce86425c63d2f0e1256`,
and its per-file-hash manifest SHA-256 is
`07baa0a8d9b9674e262619be0de4b5fc0dab05d76cabdafaeaabce20f631a483`.

Linux staging contains 116 directories including the root, 6,336 regular
files, five symlinks, and 17 ELF files. Fresh extraction has the same complete
6,457-entry path, type, octal-mode, symlink-target, and file-byte manifests.
The metadata-manifest SHA-256 is
`167e37d59820fb7c6af1f892cfe2e4912e55604c49665c2e64a11b4503e24672`;
the content-manifest SHA-256 is
`7cdaa850568d1de04f81217f7131e695fcaf85a2678a878940f1617d0b032db8`.
The 145,840,028-byte archive SHA-256 is
`b6de06d2ab4ccda015adbc4ad13d9ce8eaaa35d0b2ca63536821bce1e81ef5c1`.

All 17 packaged ELF files have zero Stage 49 marker, undefined `vk*` import,
or Vulkan or MoltenVK dynamic dependency. The four Vulkan- or
SwiftShader-named staged files and the five third-party libraries containing
generic `vkCmd` strings are byte-identical to the Stage 48 all-off baseline.
They remain accepted Chromium, SDL, loader, and SwiftShader content rather
than diagnostic-renderer payload.

On macOS, the same twelve focused routes pass with direct totals 6, 8, 9,
11, 6, 11, 7, 35, 84, 7, 20, and 1, for 205 tests with zero failures. The
required-validation hidden Cocoa route loads MoltenVK and the Khronos
validation layer. It executes one transfer clear and one render-pass clear,
rebuilds at a changed positive backing extent, then executes the same pair
against the rebuilt generations. Validation, VUID, error, and warning marker
counts are zero. CGL current context and the global OpenGL manager remain
unchanged through acquisition, both clear types, rebuild, reset, and teardown.

The enabled universal viewer, appearance utility, full build, and explicit
serial package target pass with warnings as errors and signing disabled. The
viewer and appearance library both contain `arm64` and `x86_64` slices. The
fresh macOS all-six-off graph has 178 targets and 660 `-Werror` tokens. Its
universal viewer, appearance utility, full build, warm full-build confirmation,
and serial package target pass. The package manifest retains no installer on
this host, so the staged app is the audited delivery payload; no installer-byte
claim is made.

The disabled macOS build contains 3,428 compiled artifacts: 2,560 objects, 134
archives, and 734 dynamic libraries. Complete artifact scans have zero Stage
49 or generic render-pass marker. All 73 build-produced archives complete
undefined-symbol, full-symbol, and strings scans without failure, Vulkan
import, or Stage 49 marker. The remaining archives are unchanged package-cache
inputs covered by the complete raw-artifact scan.

The staged app has 215 directories, 6,707 regular files, no symlink, and 367
Mach-O files. All Mach-O files complete file, undefined-symbol, full-symbol,
and strings inspection. `otool` completes directly for 363 files; its classic
parser misreads parentheses in four Dullahan helper filenames, and hash-matched
temporary aliases without parentheses rescan all four successfully. Effective
inspection failures, Vulkan or MoltenVK dependencies, undefined `vk*` imports,
Stage 49 symbols or strings, and generic render-pass symbol hits are zero.

The 69-file CEF inventory has content-manifest SHA-256
`7862d91a8eb92229c21d2e0deca3ad1fb6a0a99288245b698085d75dd1271b5d`,
identical to the preserved Stage 48 baseline. Its only two Vulkan-named files
are the accepted SwiftShader library and ICD manifest, with the same baseline
hashes. There is no MoltenVK- or SPIR-V-named file and no standalone SPIR-V
header. Exact byte-stream inspection finds only the same three embedded-magic
baseline files: CEF and GLESv2 with two little-endian occurrences each, and
one locale payload with one big-endian occurrence.

The frozen macOS evidence payload contains 84 files and has sorted-record
SHA-256
`84abd56779f05a2ffcae9cd07022e31a561aa40ccbe55858c0cc17dbb4143d9f`.
Its credential-marker count is zero. The disposable validation tree was
removed after the manifest was frozen, the canonical checkout remained clean,
and no viewer process existed before or after cleanup.

GCC 15 repeats its optimizer-dependent `-Warray-bounds` false positive in the
unrelated `llpaneloutfitedit.cpp` translation unit. Only that generated object
edge adds `-fno-devirtualize-speculatively`; `-Werror` remains active. The
generated edge is not a source or CMake change.

The Nix compiler wrapper needed its target-suffixed include and library search
variables when invoked outside the original development shell. The exact zlib
development include was also supplied. The enabled package completed once
without a `strip` executable in `PATH`; that result was not used for delivery
evidence. The fresh disabled package was rebuilt with the real compiler-wrapper
`strip` and passed. These are environment-only corrections.

macOS reports inherited deployment-target diagnostics from prebuilt WebRTC and
Velopack objects, plus known zero-symbol-object and duplicate-library warnings.
No warning suppression, source change, or generated-graph accommodation was
made. The first disabled full-build wrapper attempted to assign zsh's read-only
`status` variable only after Xcode had reported success; an identical warm
command then returned zero. This is a shell-wrapper error after a successful
build, not a source failure.

No viewer was launched. No login, world connection, benchmark, performance
timing, timing retention, readback, capture, image comparison, or developer
signing occurred. No credential, private network address, private result, or
machine-specific path enters source, build, test, package, or committed
evidence.

## Independent review

Independent synchronization review found and corrected the legacy transfer
barrier's source stage before the stage was frozen. Final synchronization,
state-machine, provenance, API, CMake, platform, test, scope, and privacy review
found no remaining concrete defect.

The macOS gate also exposed a test-fixture defect: its queue-submit fake
accepted only the presentation-shaped semaphore submission, although a lower
frame-slot regression assertion intentionally executes the valid
zero-semaphore empty-command-buffer submit. The fake now validates both exact
shapes and records a wait stage only when one exists. Production code was
unchanged by this correction.

Review verified matching semaphore wait and first-use stages, compatible
render-pass layouts, exact framebuffer selection, target lifetime and reset
order, post-acquire cancellation, target-independent retry, append-only enums,
acyclic static linkage, and diagnostic-only graph placement. It also confirmed
that no Windows, production SDL, viewer, benchmark, launch, or signing route
changed.

Residual risk remains in the deliberately low-level borrowed-lifetime and
external-host-serialization contract. Rare device-loss and presentation-error
branches remain fake-tested because real drivers cannot reliably force them.
The SDL wrapper itself has fake-platform coverage while its native route proves
the same underlying transaction directly.

## Explicit deferrals

- graphics-pipeline layout, shader modules, pipeline creation, cache ownership,
  viewport, scissor, vertex input, generated geometry, and a draw;
- readback, capture, screenshot comparison, and fixed-scene visual parity;
- descriptors, uploads, texture sampling, deferred materials, tonemap, and UI
  composition;
- surface reconstruction and complete device-loss recovery;
- multiple frames in flight and production resize, minimize, suboptimal,
  out-of-date, fullscreen, and display-change event wiring;
- memory-stability soak and measured CPU or GPU timing;
- connection to the production graphics selector or replacement of OpenGL;
- an OpenGL-to-Vulkan compatibility layer;
- Windows build and native execution;
- benchmark, image-parity, timing, or performance claims.
