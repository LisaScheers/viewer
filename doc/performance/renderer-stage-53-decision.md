# Stage 53 Vulkan presentation-observation decision

## Decision

Add one diagnostic frame-slot operation that clears a presentable image to
opaque red, records the existing opaque-green full-screen draw, copies the
result to the existing coherent readback allocation, presents it, waits for
the existing submission and presentation fences, and returns exact pixel
category counts.

The operation remains behind the default-off Vulkan diagnostic graph. It does
not change the production OpenGL renderer, renderer selection, ordinary window
construction, or `swapBuffers()` behavior. Windows remains excluded by user
direction.

## Observation contract

`VulkanSwapchainReadbackObservation` reports only:

- the admitted RGBA8 or BGRA8 image format;
- the exact image extent;
- total, opaque-green, opaque-red, and unexpected pixel counts.

The mapped address remains private. The classifier walks exactly the retained
byte count in four-byte pixels and canonicalizes RGBA8 and BGRA8 channel order
while classifying. Exact `[0, 255, 0, 255]` pixels are green; exact
`[255, 0, 0, 255]` pixels are red; every other value is unexpected. The
observation is returned by value and retains no image bytes or mapping.

Before image acquisition, the coherent mapped range is filled with repeating
`11 22 33 44` bytes. The poison covers the logical copy range and leaves any
allocation tail untouched. A missing or partial copy therefore cannot
silently look like the required output.

The private read and write entry points revalidate the admitted format,
positive extent, checked row and total byte layout, allocation bound, mapping,
and host-visible coherent memory properties. The creation-time overflow checks
make the retained byte count safe to convert to host `size_t`.

## Command and synchronization path

The appended `RenderPassDrawReadback` mode resolves
`vkCmdCopyImageToBuffer` together with every command already required for draw,
submission, presentation, cancellation, and fence retirement. Resolution and
parent freshness checks complete before image acquisition.

The exact command sequence is:

1. discard the acquired image's old contents and transition it to color
   attachment layout;
2. begin the existing render pass with a fixed opaque-red clear;
3. bind the existing presentation pipeline, set the full image viewport and
   scissor, and draw the existing three-vertex opaque-green triangle;
4. end the pass and transition color-attachment writes to transfer-source
   reads;
5. order the poison host writes before transfer writes to the destination;
6. copy mip zero and array layer zero over the full extent to buffer offset
   zero with tightly packed rows;
7. order destination transfer writes before host reads;
8. transition transfer reads to present layout, submit, and present.

The copy uses the retained image extent, buffer, row size, and byte count from
the same authenticated readback generation. The destination allocation is
host coherent, so no invalidate operation is required after the transfer-to-
host dependency and fence wait.

No mapped byte is inspected during recording, submission, presentation, or a
retryable state. Classification occurs only after one wait has signaled both
the submission fence and the presentation-completion fence. A valid
observation may still contain red or unexpected pixels; callers receive those
exact counts so the native proof can reject the mismatch without hiding its
cause.

## Lifetime and failure behavior

The frame slot retains the exact readback owner identity and snapshots its
buffer, format, extent, row size, and byte count before the first native wait.
It keeps that identity through acquired, submitted, presentation-ready,
present-pending, cancellation, and retry states. Move construction transfers
the retained state.

Direct frame-slot or readback reset refuses while the readback is retained.
Transitive teardown already stops at the live frame obligation. The existing
pre-submission cancellation route releases the retained owner without
classifying. Device loss also suppresses host access. An indeterminate state
keeps the resource live until the existing device-retirement contract can make
teardown safe.

The instance owner snapshots the readback epoch, object identity, buffer,
memory, mapping state, format, extent, image count, row and byte sizes,
allocation size, memory type, and memory properties. It repeats those checks
after callbacks and dispatch resolution. This rejects stale publication and
same-handle ABA replacement before activation. After activation, retained
identity and metadata checks prevent a substituted owner from being read.

As in the existing owner graph, callers externally serialize operations and
must not destroy a live aggregate reentrantly from inside a Vulkan callback.
Supporting arbitrary reentrant destruction would require a lease over the
whole parent chain and is outside this diagnostic stage.

## Parent and native routes

`VulkanInstanceGeneration` exposes one guarded draw-readback operation and
returns the typed observation through the existing frame-slot result. The
frame-slot diagnostic library now explicitly links the readback owner in
addition to the presentation pipeline, matching the code dependency without a
cycle or Vulkan loader link.

The hidden SDL/X11 and Cocoa tests replace the unobserved draw with the new
operation before and after a changed-extent complete-chain rebuild. They keep
the transfer-clear and render-pass-clear controls. Both routes require total
and green counts to equal checked `width * height`, with red and unexpected
counts equal to zero. The initial result remains valid by value after the
owner chain is rebuilt.

The Cocoa route additionally preserves the current CGL context, global OpenGL
manager state, Cocoa owner identity, native-window generation, and live-window
count. It updates the operation request to the rebuilt backing-pixel extent;
the native gate caught and corrected that stale-request defect before this
decision was accepted.

## Evidence boundary

Stage 53 proves that one fixed diagnostic draw reaches host memory exactly on
the exercised Vulkan implementations. It does not prove compositor or display
output, production renderer integration, scene rendering, or performance.

No screenshot, capture, retained pixel buffer, tolerance, benchmark, CPU or
GPU timing, upload, descriptor, texture, material, fixed scene, viewer launch,
login, Windows work, or developer signing enters this stage.

## Focused and native validation

On Linux, all fourteen focused global-dispatch, physical-device,
logical-device, configuration, swapchain, image-view, presentation-target,
presentation-pipeline, readback, frame-slot, parent, requirements, SDL-owner,
and SDL-WSI routes build with warnings as errors. Direct totals are 6, 8, 9,
12, 6, 11, 7, 5, 12, 52, 105, 7, 25, and 1, for 266 test cases with zero
failures.

The required-validation hidden X11 route uses Mesa lavapipe and the Khronos
validation layer. Before and after its changed-extent rebuild, each returned
observation has total and green counts equal to the exact current image area,
with zero red and unexpected pixels. The route passes with zero validation
messages.

On macOS, the same fourteen focused routes pass in a universal Release graph
with warnings as errors. Direct totals are 6, 8, 9, 12, 6, 11, 7, 5, 12, 52,
105, 7, 23, and 1, for 264 test cases with zero failures. The relevant
readback, frame-slot, instance, and Cocoa-owner archives each contain both
`x86_64` and `arm64` slices.

The required-validation hidden Cocoa route loads MoltenVK and the Khronos
validation layer. Its initial 1280 by 720 observation reports 921,600 total
and green pixels; its rebuilt 1440 by 810 observation reports 1,166,400 total
and green pixels. Both report zero red and unexpected pixels, and validation
reports zero messages. No viewer is launched and no signing identity is used.

## Build graph and production isolation

The implementation is reachable only through `LL_VULKAN_RUNTIME_TEST` or
`LL_VULKAN_TONEMAP_TEST`. SDL ownership additionally requires
`LL_VULKAN_SDL_WSI`; Cocoa ownership additionally requires
`LL_VULKAN_MACOS_WSI`.

The enabled Linux Release graph completes the viewer, appearance utility,
default graph, and serial package route under explicit memory limits without
an OOM. Its staged viewer has no Vulkan or MoltenVK dynamic dependency and no
undefined dynamic `vk*` symbol. Stage 53 markers appear only because the
diagnostic route is explicitly enabled.

The enabled Linux archive is
`Second_Life_Test_26_4_0_54506_x86_64.tar.xz`, 145,895,988 bytes, with SHA-256
`3204696419afbaa4365f45604ede8b1a8bb08b3915db000589d6d0a580707f74`.
It passes XZ integrity and contains 6,461 unique safe members. The staged tree
contains 6,338 regular files and five safe relative symbolic links, with no
special nodes.

The exact enabled macOS graph completes the viewer, appearance utility,
`ALL_BUILD`, and serial package route as unsigned universal Release outputs
with warnings as errors. The staged application has 6,707 regular files, 215
descendant directories, no links, and no special nodes. Its 367 unique Mach-O
files all pass load-command validation and contain no Vulkan or MoltenVK load
dependency, undefined `vk*` symbol, or Stage 53 marker. The universal main
executable targets macOS 11.0 and has SHA-256
`6b128ed9aee5e9d3700ace89b694516fd435fd200624f3e05a5473de21d382fe`.
The sorted path-and-content manifest for all application files has SHA-256
`ac1fe2092930135ea6815077f1a426a87798ef61d5c56b1e8c6df0655df7bc76`.

The fresh macOS all-six-off graph keeps the benchmark, runtime, tonemap, SDL
WSI, Cocoa WSI, and Win32 WSI switches off. Its 178-target project contains no
Stage 53 marker or diagnostic target. Universal viewer, appearance,
`ALL_BUILD`, and serial package routes pass with warnings as errors and no
developer signing. The final staged application contains 6,707 regular files,
215 descendant directories, no links, and no special nodes. All 367 unique
Mach-O files pass load-command validation. No file contains a Stage 53 marker,
Vulkan or MoltenVK load dependency, undefined `vk*` symbol, diagnostic
artifact name, or Vulkan/MoltenVK payload name. The universal main executable
targets macOS 11.0 and has SHA-256
`f217c0fb64a4354f676fa6ce506f46cf1908d504c676c0963ebf7f9f6961e0f8`.
The sorted application path-and-content manifest has SHA-256
`2e75508e034154c81e3d977615126046b4b38922d94965715c744edc23374cd0`.

The first fresh-disabled staging attempt selected a system Python without the
project's `llsd` module. Selecting the same project interpreter used by the
enabled graph corrected that environment-only omission; the preserved compile
outputs then completed every final gate without a source or diagnostic-gate
change.
After evidence capture, the exact disposable macOS validation root was removed
and about 61.2 GiB recovered. The pinned SDK and tool root remains preserved.

The fresh Linux all-six-off graph keeps the same six switches off. Its final
cache produces 2,697 Ninja targets and 1,387 compile commands, with no Stage 53
source name, target, or marker. Explicit viewer and appearance targets, the
default all-target graph, and a separate serial package route all pass under
7 GiB soft and 10 GiB hard memory limits without an OOM.

The fresh GCC 15.3 build exposed one pre-existing optimized-inlining
`-Warray-bounds` diagnostic in `llpaneloutfitedit.cpp`. To keep that unrelated
source out of Stage 53, only its generated disposable `build.ninja` rule
demoted that warning from an error. The repository, CMake sources, and cache
were not changed; every other compile command retained warnings as errors.
Ninja recorded 1,068 dependencies for the exceptional object and did not
silently rebuild it. The full graph also required the Nix GStreamer app-header
development output and the same five immutable standard-runtime staging links
used by the enabled build. Those corrections affected only the disposable
build environment.

The completed graph has 1,385 compiled objects and 25 project static archives.
Its objects, project archives, shared libraries, executables, staged tree, and
uncompressed archive stream contain no Stage 53 marker. The only
Vulkan-named objects are the established window-requirements implementation
and test. All 17 staged ELF files contain no Vulkan or MoltenVK dynamic
dependency and no undefined `vk*` symbol. Chromium's bundled SwiftShader
remains accepted third-party content.

The fresh-disabled Linux archive is
`Second_Life_Test_26_4_0_54506_x86_64.tar.xz`, 145,988,336 bytes, with SHA-256
`7f5cac08e3b772ebef98a73f01dc97461961f1fb0f711869e2d2dcbc1196f664`.
It passes XZ integrity and contains 6,457 unique members with no duplicate or
unsafe path: 116 directories, 6,336 regular files, and five safe relative
symbolic links, with no hard link or special node. The staged tree has the
same type counts and occupies 535,515,680 bytes. Its regular-file and
link-target manifests have SHA-256
`7338044df1d483dac4f3c79843a82a946b650a54cf5f9febeba22d68614eed52`
and
`e683bb7461c5b54b34a248ec9722dc6df5eb8b02af012b85fe17fddb5046e289`.

The disabled build viewer, appearance utility, and staged viewer have SHA-256
`da78bd8f383a1f60f3a28024891fb0e71f3de10f0f779acd80c4b97c118b2499`,
`3cbb058fab7a3595d6558800c9ababdb3454b3aff155c3a34efba737483e6246`,
and
`056ad3f3c6d125cc05cd375e1390eb5c67352a1ace26413115a48395f09bea04`.
The enabled Linux archive remains byte-identical after the disabled proof.
After evidence capture, the exact disposable disabled build was removed and
about 20.8 GiB recovered.

## Source identity and review

The frozen source starts from
`b5ce131d49a31656ef0f407e358a3a8aa8b2f5e6`. Its pre-decision-note snapshot is
`1822f9833eda65e4dda643460065ba1a11258bf4`; its logical tree is
`72ff57a1d0eba19192c7288d40288c9fbc26ba66`; and its full-index binary diff has
SHA-256
`e78f6efa267488e69136677995e712133ad6f076599cbb3bb30125f6bee6a245`.
The snapshot contains 9,601 tracked files. Its sorted Git tree manifest has
SHA-256
`9f9d5e96e78f1fbe57597152642402e286fd56c905a364a141da755e6ccb271e`.

The deterministic cross-host archive is 23,686,896 bytes, contains 9,601
tracked files plus 225 directory headers, and has SHA-256
`f2649c182336814edb2a41176381576ef9a134299eb98f575278d474edc88300`.
The SHA-256 of sorted path-and-content records for the twelve Stage 53 source,
build, and test files is
`07efe5caa19e8657cafd27fc38b3d5d45ffee760ad8be47573da91374bce1ebc`.
Linux and macOS independently reproduce that digest.
This decision note is excluded because including its own digest would be
self-referential. The two unrelated untracked documents are absent.

Independent reviews found no in-contract synchronization, bounds, format,
lifetime, parent-authentication, platform-state, or default-off isolation
defect. The residual reentrant-destruction restriction is documented above.
All repository diffs pass whitespace validation, and no private address,
credential, account data, temporary path, or private test log is retained.

## Reanalysis

The observation closes the smallest missing truth gap in the fixed
presentation path: the existing shader is now proven to cover the full image
on both required native implementations. Repeating more fixed-color
observations would add confidence but would not advance the master migration.

The next committable dependency should therefore begin the resource path. Its
exact scope is deliberately left to the post-commit Stage 54 reanalysis; no
upload or sampled-image design is smuggled into this commit.
