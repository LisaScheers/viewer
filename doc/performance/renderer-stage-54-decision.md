# Stage 54 Vulkan immutable upload-source ownership decision

## Decision

Add one device-scoped Vulkan owner for an immutable 48-byte transfer source.
The owner accepts a nonzero backend-neutral `BufferHandle` and the existing
three-position screen-triangle bytes by value, creates one transfer-source
buffer with one dedicated host-visible allocation, copies the bytes once, and
closes the mapping before publication.

This is the first resource-upload ownership step in master Stage 3. It remains
behind the default-off Vulkan diagnostic graph. It does not change the
production OpenGL renderer, renderer selection, ordinary window construction,
or `swapBuffers()` behavior. Windows remains excluded by user direction.

## Request and identity boundary

`VulkanUploadSourceDescription` contains exactly:

- one nonzero `LLRenderContract::BufferHandle`;
- one value-owned `std::array` of 48 bytes.

The request does not retain caller storage, a span, or a writable pointer. It
also does not add a Vulkan-shaped resource-creation command to
`FrameSnapshot`. That contract remains a backend-neutral description of frame
work and typed resource references.

The generation retains the complete description and a nonzero 64-bit FNV-1a
identity of the bytes. `matchesDescription()` compares the typed handle, the
identity, and every byte. The identity is a stable diagnostic fingerprint, not
a substitute for the exact comparison and not a cryptographic content
address.

## Dispatch and native object

The implementation keeps `VK_NO_PROTOTYPES` and resolves every required entry
point through the retained instance and device resolvers before creating a
buffer:

- physical-device memory-property query and `vkGetDeviceProcAddr`;
- buffer creation, destruction, and memory-requirement query;
- memory allocation, release, and buffer binding;
- memory mapping, unmapping, and mapped-range flushing.

The native buffer has zero flags, size 48, exclusive sharing, and usage exactly
`VK_BUFFER_USAGE_TRANSFER_SRC_BIT`. It is bound at offset zero to one dedicated
allocation whose size is the exact buffer memory requirement. There is no
suballocation, alias, ring, staging pool, or general allocator policy.

Returned buffer and memory handles start null. A failed creation or allocation
does not inspect or destroy a possibly poisoned failure output. Success with a
null buffer, memory, or mapping is a separate typed failure.

## Memory selection and immutable publication

Memory-property and requirement validation rejects empty or out-of-range
tables, invalid heap indexes, an undersized or host-unrepresentable
requirement, zero or non-power-of-two alignment, and empty `memoryTypeBits`.
A candidate must be admitted by `memoryTypeBits`, be host visible, and have a
heap large enough for the requirement.

Selection is deterministic by memory-type index and makes two passes:

1. host-visible and host-coherent;
2. host-visible noncoherent fallback.

Protected, lazily allocated, AMD device-coherent, and QCOM tile-memory
candidates are excluded because the current logical-device path enables none
of their required behavior or features.

The whole allocation is mapped from offset zero. Exactly 48 bytes are copied
at offset zero and allocation padding is left untouched. A noncoherent
selection flushes one mapped range with offset zero and `VK_WHOLE_SIZE`; a
coherent selection performs no flush. The allocation is always unmapped before
the generation is published, so the public owner exposes no host pointer or
writable byte view.

Failure rollback is the exact applicable reverse sequence: unmap, destroy the
buffer, then free memory. Normal reset destroys the buffer before freeing its
bound memory. Reset is idempotent, and move construction transfers every
handle, callback, description, identity, property, and parent-provenance field
while leaving the source inert.

## Aggregate ownership and lifetime

`VulkanInstanceGeneration` stores the upload source as a logical-device child
and a swapchain sibling, with its own ownership epoch. Acquisition requires
the live instance, surface, physical-device selection, logical device, native
window generation, owner callback, and window-generation callback.

The aggregate snapshots and rechecks the exact parents, native handles, queue
selection, description, and ownership epoch around callbacks, native
resolution, allocation, and publication. It rejects duplicate acquisition,
reentrant transition, allocation failure, stale callbacks, and same-looking
ABA replacement without replacing the live owner.

A changed-extent swapchain rebuild neither resets nor republishes the upload
source. Successful and failed rebuilds retain the exact owner, typed handle,
buffer, memory, content identity, metadata, and epoch. Direct upload-source
reset leaves configuration, swapchain, images, presentation target, pipeline,
readback destination, and frame slot live. Complete teardown retires all
swapchain children first, then the upload source, then the logical device.

## Native and evidence boundary

The hidden SDL/X11 and Cocoa tests acquire the exact fixed source before the
existing observed-draw transaction. They retain its handle, identity, buffer,
memory, logical size, allocation size, selected type, and memory properties
through the initial draw and a changed-extent complete-chain rebuild. They then
reset it independently while the reusable swapchain child chain remains live.

The native routes prove that lavapipe and MoltenVK can create, bind, populate,
unmap, retain, and destroy this source with the required validation layer
reporting zero messages. The focused fake-driver route separately proves the
exact copied bytes, untouched padding, coherent and noncoherent behavior, and
failure rollback.

Stage 54 deliberately records no copy from this source and performs no GPU
read of it. Native tests therefore do not infer byte contents from a draw; the
next stage must add a destination and a synchronized transfer before making
that claim.

## Focused and native validation

On Linux, fifteen focused global-dispatch, physical-device, logical-device,
upload-source, configuration, swapchain, image-view, presentation-target,
presentation-pipeline, readback, frame-slot, parent, requirements, SDL-owner,
and SDL-WSI routes pass with warnings as errors. Direct totals are 6, 8, 9, 9,
12, 6, 11, 7, 5, 12, 52, 110, 7, 25, and 1, for 280 test cases with zero
failures.

The final hidden SDL/X11 binary has SHA-256
`bc03c857ff8687785e022c7e0e769fe6f8bde4a5a331ba0fd32a95dfe4217565`.
It passes against the pinned Vulkan loader, Mesa lavapipe, X11, and the
Khronos validation layer. One native run covers both the initial and rebuilt
draw transactions, retains the upload source through both, resets it with the
complete child chain live, and reports zero validation messages.

On macOS, the corresponding fifteen routes pass in a universal Release graph
with warnings as errors. The Cocoa-owner route has 23 cases rather than SDL's
25, for 278 test cases with zero failures. The upload-source, instance, and
Cocoa-owner archives contain both `x86_64` and `arm64` slices. Integration-test
executables remain host-architecture binaries under the established CMake test
policy.

The separate hidden Cocoa route loads the pinned Vulkan loader, MoltenVK, and
the Khronos validation layer. It passes one complete initial and rebuilt
transaction with the upload source retained, zero validation messages, and no
change to CGL context, global OpenGL manager, Cocoa owner, backing scale,
backing-pixel geometry, or live-window count. No developer signing is used.

A final changed-line audit with Clang 21 corrected formatting in the Cocoa
native test only. The affected target rebuilt successfully, its opt-out route
passed 1/1, and its hidden required-validation route passed 1/1 with all 230
assertions and zero validation or VUID reports. The production main executable
remained byte-identical because neither production macOS graph contains that
test source.

## Build graph and production isolation

The feature-on Linux graph builds the viewer, appearance utility, default
graph, and serial package successfully. The largest guarded link uses 7 GiB
with no swap. Its regenerated release archive is 145,929,780 bytes, has
SHA-256 `e991b6fb93d1d00d2e9497eec6236eaeac88902474707f25388ecf3cf6ed61d4`,
passes XZ integrity, and contains 6,461 path-safe entries. The staged tree has
6,338 files, 118 directories, five valid relative links, and the two
expected Vulkan material shader artifacts.

The enabled Linux main executable contains upload-source symbols because
`LL_VULKAN_SDL_WSI` deliberately makes the production `llwindow` archive use
the experimental Vulkan instance aggregate. This is feature-on diagnostic
linkage, not the default build. None of the 17 staged ELF files has a direct
Vulkan or MoltenVK dependency or an undefined Vulkan entry point. The four
Vulkan-named package files are the pre-existing CEF SwiftShader loader,
implementation, and duplicate ICD records.

A separate fresh Linux configuration disables the benchmark, Vulkan runtime,
tonemap, SDL WSI, macOS WSI, and Win32 WSI switches. Its 1,848-target graph and
1,191 compile commands contain no upload-source target, source, class, error,
or constant marker; every compile command retains `-Werror`. It produces 1,189
objects and 88 archives, then builds the viewer, appearance utility, default
graph, and serial package successfully. The archive is 145,731,680 bytes, has
SHA-256 `f579e8620c4917d305cb45a8575d4e18ec4780070555038d7eaad13e9ca2284e`,
passes XZ integrity, and contains 6,457 path-safe entries. Its 6,336-file,
116-directory staged tree has five valid relative links. All 17 staged
ELF files have zero Stage 54 markers, direct Vulkan or MoltenVK dependencies,
and undefined Vulkan symbols.

Both macOS configurations build the viewer and appearance deliveries, full
graph, and serial staging package as universal Release artifacts with warnings
as errors and signing disabled. The fresh all-six-off Xcode graph has 178
targets, 2,558 objects, 134 archives, zero upload-source graph or object
matches, and 330 explicit `-Werror` settings. Xcode does not emit the requested
compile-command database for this generator. Each staged app has 6,707 files,
215 directories, no links, and the same tree shape. Across 367 Mach-O files,
both apps have zero direct Vulkan or MoltenVK dependencies and undefined
Vulkan symbols. The enabled macOS app also has zero Stage 54 markers because
its Cocoa Vulkan owner remains in a test-only archive; the fresh all-off graph
contains no such archive. The local harness stages the app and records the
expected DMG name but does not create a DMG.

Fresh-host corrections were limited to the build harness. Linux needed the
already selected development include and link outputs made explicit. GCC 15
also repeated its known `std::function` array-bounds false positive in one
unchanged outfit-editor translation unit; only that generated disposable
object rule demoted that warning from an error. Every other compile command
kept warnings as errors. macOS needed the established immutable release flag
values exported before its final clean configuration. Before the format-only
test refresh, the retained macOS SDK header and JSON trees were found damaged.
An isolated replacement restored only the recorded exact Vulkan-Headers,
MoltenVK, and ValidationLayers revisions from their official Khronos
repositories and reused the already authenticated dylibs. No correction
changed repository source, feature defaults, or test scope. Both final
packages use the frozen implementation. The later format-only macOS test
change is excluded from both package graphs.

## Source identity and review

The frozen source starts from Stage 53 commit
`2bdd96124d29a3450a070d2ee04b829c5455c9b6`. Nine source, build, and test paths
change, including three new upload-source files. Their sorted path-and-content
record has SHA-256
`8eb4026f400d7dca5fe77b0e8b545326262babd366c4c5f3de2779f4d90a4c75`.
The working source trees on Linux and macOS reproduce every record. The final
macOS test refresh uses this record; the format-only native-test change is not
part of either Linux build graph.

A synthetic index over the Stage 53 base produces logical tree
`1deed6a036976364f60f4da6b7a372bdaf33b723`. Its full binary diff has SHA-256
`2e786812c405b20f7d8964243d46aba7e3234ccef182f2ed19e456a754726d7c`,
its tracked-entry manifest has SHA-256
`48dff6c068412ebce6c3d04ea4c3d3f5cd8c875d1898bb5c7b4ab7787924eb87`,
and its sorted Git tree manifest has SHA-256
`0ff44a37826d1d91a8a912d3e449256b2a692cbd7618a1ba71230bf1bda2619b`.
The tree contains 9,605 tracked files. This decision note stays outside those
pre-decision digests to avoid a self-reference.

Independent review found no correctness, memory-selection, bounds, rollback,
parent-authentication, ABA, move, teardown, CMake, platform-state, or
default-off isolation defect. Review first found two missing parent tests.
The final test set now covers failed rebuild survival and independent reset
with the complete swapchain child chain live.

The remaining test-strength note is deliberate. Native routes authenticate a
real allocation and its lifetime but do not read the driver's 48 source bytes
or use them in a command. The fake-driver tests prove the exact bytes and
identity. GPU consumption belongs in the next committable stage.

## Explicit deferrals

- destination-buffer or image ownership;
- command pools, command buffers, copy commands, barriers, queues, submission,
  semaphores, fences, completion, retry, and device-loss recovery for upload;
- vertex binding, indexing, descriptors, images, samplers, textures, materials,
  scene data, UI composition, and general resource allocation;
- capture, screenshot comparison, fixed-scene comparison, or OpenGL parity;
- production renderer selection or replacement of the OpenGL path;
- an OpenGL-to-Vulkan compatibility layer;
- viewer launch, login, world connection, benchmark, retained timing, or any
  CPU, GPU, image-parity, or performance claim;
- Windows build or native execution;
- developer or distribution signing.
