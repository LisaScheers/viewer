# Stage 52 Vulkan presentation-readback ownership decision

## Decision

Add one loader-neutral owner for a single host-visible destination buffer sized
for the current swapchain image. The owner creates, allocates, binds, maps,
authenticates, moves, and destroys that destination. It does not record a copy
or read any mapped byte.

The owner remains behind the default-off Vulkan diagnostic graph. It does not
change the production OpenGL renderer, renderer selection, ordinary window
construction, or `swapBuffers()` behavior. Windows remains excluded by user
direction.

## Swapchain admission

The selected swapchain configuration now requires all three image uses:

- `VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT`;
- `VK_IMAGE_USAGE_TRANSFER_DST_BIT`;
- `VK_IMAGE_USAGE_TRANSFER_SRC_BIT`.

The surface must advertise transfer-source image use, and the selected optimal
tiling format must advertise `VK_FORMAT_FEATURE_TRANSFER_SRC_BIT`. The two new
configuration errors are appended after the Stage 51 values, preserving every
earlier ordinal.

The configuration sets `clipped` to `VK_FALSE`. A diagnostic copy must not rely
on an implementation preserving only the visible portions of a presentable
image.

## Destination ownership

`VulkanSwapchainReadbackGeneration` owns one `VK_BUFFER_USAGE_TRANSFER_DST_BIT`
buffer, one separate memory allocation, and one persistent whole-allocation
mapping. It accepts only `VK_FORMAT_B8G8R8A8_UNORM` and
`VK_FORMAT_R8G8B8A8_UNORM`.

The exact byte layout is tightly packed four-byte pixels. Row bytes are
`width * 4`; total bytes are `rowBytes * height`. Both products reject zero or
overflow against the smaller of `VkDeviceSize` and host `size_t`.

Memory selection follows the buffer's `memoryTypeBits` in index order. A type
must contain both `VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT` and
`VK_MEMORY_PROPERTY_HOST_COHERENT_BIT`, and its heap must cover the allocation.
Types carrying `VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD` and types backed by
`VK_MEMORY_HEAP_TILE_MEMORY_BIT_QCOM` are excluded because this logical-device
path enables neither feature.
There is no non-coherent fallback in this diagnostic path. The retained
allocation size may exceed the logical byte count, but never undersizes it.

The generation authenticates the exact physical-device, logical-device,
configuration, swapchain, and image owners. It also snapshots loader, instance,
surface, physical-device selection, device, queue, drawable extent, swapchain,
format, image extent, and image count provenance.

## Dispatch, publication, and rollback

All required commands resolve under `VK_NO_PROTOTYPES` before the first Vulkan
resource-creation call:

- `vkGetPhysicalDeviceMemoryProperties` and `vkGetDeviceProcAddr`;
- buffer creation, destruction, and memory-requirement queries;
- memory allocation, release, binding, mapping, and unmapping.

A missing command publishes no partial generation. Creation checks returned
handles, memory-property bounds, power-of-two alignment, allocation size,
memory-type compatibility, bind success, map success, and a non-null mapped
address.

Failure after buffer creation rolls back in the exact applicable order: unmap,
destroy buffer, then free memory. Normal reset uses the same order. Move
construction transfers every handle, provenance snapshot, byte-layout field,
and retained cleanup command, then disarms the source.

## Parent and platform lifecycle

`VulkanInstanceGeneration` adds guarded readback acquisition, metadata getters,
an independent ownership epoch, direct reset, move transfer, aggregate rebuild,
and complete-chain rollback.

Aggregate creation is target, pipeline, readback, then frame slot. Complete
image teardown is frame slot, readback, pipeline, target, then images. Direct
readback reset preserves the target, pipeline, and frame slot. Direct pipeline
or target reset preserves the readback. The readback depends on the image
generation, not on those three presentation siblings.

A reentrant or final-freshness transition cannot publish a stale destination.
Callbacks, owner identity, native-window generation, drawable extent, parent
identity, and the ownership epoch are checked around resolution and allocation.
Aggregate rebuild requires the readback to remain live through frame-slot
publication.

The SDL and macOS diagnostic wrappers forward guarded acquisition and reset.
SDL refreshes positive drawable geometry. macOS acquisition keeps its existing
main-thread ownership requirement and refreshes Cocoa backing-pixel geometry.
Native tests compare retained metadata with the current swapchain image extent
and format.

No production `LLWindowSDL` hook, viewer setting, graphics selector, resize
event, OpenGL context owner, or ordinary presentation route changes.

## Host-access boundary

The internal generation retains the mapping only so it can own and unmap it.
For mapping state, the public API exposes only a boolean. It also exposes the
Vulkan buffer and memory handles plus bounded layout metadata, but it does not
expose the raw mapped pointer.

Stage 53 remains the first permitted bounded byte access. It must record the
image-to-buffer copy, apply the required synchronization, wait for completion,
and only then inspect bytes through a deliberately bounded API.

Stage 52 does not link the readback owner into frame-slot recording or
submission. It records no image barrier, buffer barrier, copy, invalidation, or
host read.

## Evidence boundary

Stage 52 proves ownership and lifecycle only. The exercised native drivers may
create, bind, coherently map, authenticate, move, and destroy the exact
destination as part of the diagnostic swapchain chain.

It does not prove that an image was copied, that any mapped byte is readable or
correct, that the Stage 51 shader produced green pixels, or that compositor or
display output is correct. No capture, screenshot comparison, fixed-scene
comparison, upload, descriptor, material, tonemap, UI composition, CPU timing,
GPU timing, or performance claim enters this stage.

## Build graph and production isolation

The readback library and test remain reachable only through
`LL_VULKAN_RUNTIME_TEST` or `LL_VULKAN_TONEMAP_TEST`. The library links the
swapchain-image owner and adds no Vulkan loader link dependency. The instance
diagnostic library links the new owner. SDL ownership additionally requires
`LL_VULKAN_SDL_WSI`; macOS ownership additionally requires
`LL_VULKAN_MACOS_WSI`.

The enabled Linux Release graph keeps the benchmark, runtime, tonemap, and SDL
WSI gates on while the macOS and Windows WSI gates remain off. The viewer,
appearance utility, default graph, and package all complete from the final
reviewed source with warnings as errors under capped build services. The Stage
52 compile rule retains `VK_NO_PROTOTYPES`. As expected for this explicitly
enabled diagnostic build, the viewer contains 17 Stage 52 symbol strings
through the SDL window to instance-owner link. It has no Vulkan or MoltenVK
dynamic dependency and no undefined dynamic `vk*` symbol.

The fresh Linux all-six-off graph keeps tests on and has 2,615 targets and
1,387 compile commands, all with warnings as errors. Its generated graph has no
Stage 52 source or marker. The resulting 1,385 objects, 89 archives, executable
build outputs, staged tree, and archive also have no Stage 52 owner, operation,
or marker. The established window-requirements implementation and its test are
the only Vulkan-named translation units. The full default graph and separate
serial package route complete under hard memory limits without an OOM or kill.
The staged viewer has no Vulkan or MoltenVK dynamic dependency and no undefined
dynamic `vk*` symbol.

The enabled macOS universal Release graph completes the viewer, appearance
library, `ALL_BUILD`, and a separate serial `llpackage` route with warnings as
errors. The staged application executable contains `x86_64` and `arm64` slices,
targets macOS 11.0, and has no direct Vulkan or MoltenVK dependency. Signing is
only the linker's generated ad hoc identity: no team or developer signature is
claimed.

The fresh macOS all-six-off project completes universal viewer, appearance,
`ALL_BUILD`, and serial `llpackage` routes with warnings as errors. That package
target produces an unsigned staged application, not a DMG, PKG, ZIP, or tar
archive. Its 178 Xcode targets, 2,558 objects, 73 project archives, Mach-O
outputs, and staged application contain no Stage 52 source, target, symbol,
class, operation marker, or payload. The only Vulkan-named object is the
established window-requirements check and its test; the only generic Vulkan
application content is Chromium's SwiftShader library and ICD manifest.

The disabled staged application has 6,707 regular files and 214 descendant
directories, with no links or special nodes. All 367 Mach-O files pass load
command validation; the main and project-built outputs are universal. Six
pre-existing VLC SIMD plug-ins remain x86_64-only, so the whole third-party tree
is not claimed universal. Neither architecture has an undefined `vk*` symbol
or a Vulkan or MoltenVK load dependency. The consolidated isolation audit
passes with evidence SHA-256
`41bafff1efa3dbdd657fecc94cd9ada5b8e6b2a4262987dee40dac9e98583847`.

## Validation evidence

On Linux, all fourteen focused global-dispatch, physical-device,
logical-device, configuration, swapchain, image-view, presentation-target,
presentation-pipeline, readback, frame-slot, parent, requirements, SDL-owner,
and SDL-WSI routes build with warnings as errors under capped build services.
Direct totals are 6, 8, 9, 12, 6, 11, 7, 5, 8, 40, 100, 7, 25, and 1, for
245 test cases with zero failures.

The required-validation hidden X11 route uses Mesa lavapipe and the Khronos
validation layer. The exact post-cleanup source completes the native route with
the Stage 52 aggregate owner present, and validation reports zero messages. No
mapped byte is accessed.

On macOS, the same fourteen focused routes pass in a universal Release graph
with warnings as errors. Direct totals are 6, 8, 9, 12, 6, 11, 7, 5, 8, 40,
100, 7, 23, and 1, for 243 test cases with zero failures. The readback,
instance, and Cocoa-wrapper archives each contain both `x86_64` and `arm64`
slices.

The required-validation hidden Cocoa route loads MoltenVK and the Khronos
validation layer. It creates and maps the exact destination with the aggregate
owner present, and validation reports zero messages. No mapped byte is
accessed.

The frozen source snapshot starts from
`a63b081af65daece85705049e4333e0142b8bf1e`. Its full-index binary diff has
SHA-256
`5bc736a9dafdf7cdf3876ce32339dcc06177cd16ae9677dc9bf0518efa0ccaf2`;
its logical tree is `bf1cd006f2464b877ba531e77ebd1f1170a82652`; and its
tracked-entry manifest has SHA-256
`4e849c3a9d253ef8ca107efde0be69282be808b4a206495639919a0be7f76748`.

The deterministic cross-host archive is 23,663,850 bytes, contains 9,600
tracked files plus 224 directory headers, and has SHA-256
`8dae009b132bb73faea89de56612218d676c7eee9addf1b737b90e99dcfa4181`.
Linux and macOS also reproduce
`f5d11e6a0eab1bcf94cd9ccc5cda632713bb5a88dc9857642d9afe14beea130d`
as the digest of the sorted-path SHA-256 records for the 23 Stage 52 source,
build, and test files. This decision note is intentionally excluded because
including its own digest would be self-referential. The two unrelated
untracked documents are also absent.

The final enabled Linux archive is
`Second_Life_Test_26_4_0_54505_x86_64.tar.xz`, 145,932,816 bytes, with
SHA-256
`e38e259b5edd7f790aef44652d2b85bcdea29c43a7813202a4954dad1066c753`.
It has one root and 6,461 unique members: 118 directories, 6,338 regular files,
and five safe symlinks. The all-six-off archive has the same name, 145,865,576
bytes, and SHA-256
`c8160884edb4f87b8541fe190b40d2405e33463acaecc8b112be5755531a9584`.
It has one root and 6,457 unique members: 116 directories, 6,336 regular files,
and five safe symlinks. Both pass XZ integrity, duplicate, path, link,
special-node, and exact staging-content checks.

The enabled archive adds only the two established material-diagnostic SPIR-V
files and their two directories to the generic Vulkan path set. Its expected
Stage 52 symbols occur only in the explicitly enabled diagnostic viewer. The
disabled archive contains no Stage 52 marker, project Vulkan shader directory,
or SPIR-V file. Its Vulkan-named content is exactly the bundled loader,
SwiftShader library, and two SwiftShader ICD manifests.

The exact-source enabled macOS staged application has 6,707 regular files and
215 descendant directories, with no links or special nodes. Its path-and-type
tree is identical across two serial package runs and has SHA-256
`f253c99762fedf496c872c048ee7ad8b6893ac31272a3f2543a4ff38e45a6270`.
The two content records have SHA-256
`d0d0db5b4fcd6321eeaab8731c550d9295be1eb08ab6db89fa92ce417fa20a1e`
and
`b447311906a98ccdcd1d50925da115267f5ae2d540b64c0dd432f036a90d278f`;
the only changed file is the package system's known randomized
`contributors.txt`, while the other 6,706 files are byte-identical.

The enabled universal main executable has SHA-256
`f0b954a1022589e4eedf323e519fd19f8f1f568052692920a197c61792923493`.
Its `Info.plist` and build-data manifest have SHA-256
`88927ee0e3c26a67d1f2f0093c1378a5356fcd8acbc990e5eed48fb9e3552d0c`
and
`569bf49ddaece96cdd5049dbe8711d422bb350dcd5c72f37b56c5c6f62213854`.
The enabled graph has 204 Xcode targets, 2,597 objects, and 170 archives.
Stage 52 markers are absent from all 367 staged-application Mach-O files and
occur only in the three explicitly enabled diagnostic test executables. The
application's only Vulkan-named payload remains Chromium's SwiftShader library
and ICD manifest.

Both Linux package variants contain a pre-existing normalized set of 799 viewer
and 1,013 package-wide absolute build/source-path literals. They are compiled
release payload strings, not dynamic dependencies or symbol-table residue. The
macOS staging trees likewise retain their disposable build root in five
project-built Mach-O files. These artifacts are therefore accepted as
isolation evidence but are not privacy-clean or publishable. Source, committed
evidence, and reported results contain no private path or credential.

No viewer was launched. No login, world connection, benchmark, renderer timing,
timing retention, image readback, mapped-pointer dereference, capture, image
comparison, or developer signing occurred. No credential, private network
address, private result, or machine-specific path enters source or committed
evidence.

## Independent review

Review checked transfer-source admission, `clipped = VK_FALSE`, checked byte
layout, exact format admission, coherent memory selection, complete dispatch
resolution, rollback order, null-success handling, parent provenance,
publication cutoffs, move/reset behavior, aggregate ordering, sibling
independence, wrapper freshness, diagnostic-only build placement, and Windows
exclusion.

Review found and corrected several boundary errors. Reentrant frame-slot
publication now requires the readback owner to remain live. Native macOS checks
use the current swapchain image extent and format rather than pre-rebuild
geometry. Reset and rollback tests now prove unmap, buffer destruction, and
memory release ordering plus the absence of later calls after each failure.
The initially exposed mapped address was removed from the public API; only a
boolean mapping-state query remains in this stage.

Final review also removed an invalid blanket rejection of unused zero-sized
memory heaps. Selection now excludes AMD device-coherent memory types and QCOM
tile-memory heaps because the logical device enables neither feature, while
still falling through to the lowest ordinary compatible type. Focused tests
cover both fallbacks and feature-only rejection without allocation. A direct
same-handle image-parent replacement during the readback callback now proves
the ownership epoch rejects stale publication before any readback-native call.

The main residual risk is the existing borrowed-parent and externally
serialized host-lifetime contract. Devices without a compatible coherent
host-visible memory type reject this diagnostic owner by design. Device-loss
and uncommon allocation failures remain fake-tested where native drivers do
not offer deterministic fault injection. The unsupported-format rejection is
not independently reachable after the current configuration policy admits its
two formats.

## Explicit deferrals

- image-to-buffer copy commands and transfer-to-host synchronization;
- bounded mapped-byte access and pixel classification;
- proof of green output, full-target coverage, visual parity, or scene output;
- capture, screenshot comparison, and fixed-scene comparison;
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
