# Stage 55 Vulkan resident upload transaction decision

## Decision

Add one device-scoped Vulkan owner for a distinct resident vertex buffer and
one device-scoped owner for the transfer that populates it. The destination
accepts the exact immutable 48-byte upload-source description from Stage 54,
allocates compatible device-local memory, and becomes resident only after one
fence-backed copy completes successfully.

This is the second resource-upload ownership step in master Stage 3. It stays
behind the default-off Vulkan diagnostic graph. It does not change the
production OpenGL renderer, renderer selection, ordinary window construction,
or `swapBuffers()` behavior. Windows remains excluded by user direction.

## Destination ownership and memory

`VulkanUploadDestinationGeneration` retains the same nonzero neutral
`BufferHandle`, exact description, and expected content identity as the live
upload source. It owns a different native buffer and a separate allocation
used only by that buffer. Both native aliases are rejected with
typed errors, and an aliased borrowed handle is never destroyed during failure
handling.

The destination buffer has zero flags, size 48, exclusive sharing, and usage
exactly `VK_BUFFER_USAGE_TRANSFER_DST_BIT |
VK_BUFFER_USAGE_VERTEX_BUFFER_BIT`. It is bound at offset zero to an allocation
whose size is the exact buffer memory requirement. Requirement validation
rejects undersized or host-unrepresentable sizes, zero or non-power-of-two
alignment, and empty memory-type masks.

Memory selection takes the lowest compatible type index that is device local
and belongs to a sufficiently large heap. Host-visible or coherent properties
are permitted because unified-memory and MoltenVK implementations can expose
them alongside device-local memory, but the destination is never mapped.
Protected, lazily allocated, AMD device-coherent, and QCOM tile-memory
candidates are excluded because their required feature behavior is outside the
current logical-device contract.

Resolution reauthenticates the source and both parent device generations after
dispatch resolution and after each subsequent native query or mutation.
Failure rollback destroys the newly owned buffer before freeing its bound
allocation. A failed create or allocation does not inspect or destroy a
possibly poisoned output. Normal reset follows the same buffer-before-memory
order and is idempotent. Move construction transfers all native ownership,
metadata, callbacks, parent provenance, and resident state while leaving the
moved-from destination inert.

The resident identity starts at zero. Allocation and binding alone never
publish content. Only the transfer owner can publish the expected nonzero
identity after a completed fence wait.

## One-shot transfer and synchronization

`VulkanUploadTransferGeneration` is a logical-device child, separate from the
swapchain frame slot. It owns one command pool with zero flags for the existing
unified graphics and presentation queue family, one primary command buffer,
and one initially unsignaled fence with zero flags. It owns no semaphore,
swapchain object, reusable frame state, or queue-idle operation.

One execution begins the command buffer with
`VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT` and records exactly:

1. one source buffer barrier from `VK_ACCESS_HOST_WRITE_BIT` at
   `VK_PIPELINE_STAGE_HOST_BIT` to `VK_ACCESS_TRANSFER_READ_BIT` at
   `VK_PIPELINE_STAGE_TRANSFER_BIT`;
2. one `VkBufferCopy` with source offset zero, destination offset zero, and
   size 48;
3. one destination buffer barrier from `VK_ACCESS_TRANSFER_WRITE_BIT` at
   `VK_PIPELINE_STAGE_TRANSFER_BIT` to
   `VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT` at
   `VK_PIPELINE_STAGE_VERTEX_INPUT_BIT`.

Both barriers use ignored queue-family indexes and cover exactly the 48-byte
range. One submit contains one command buffer, the retained fence, and no wait
or signal semaphores. Completion waits for that fence with `waitAll` true and
timeout `UINT64_MAX`. The operation never resets or records the command buffer
again and never calls `vkQueueWaitIdle`.

## Completion, failure, and retry

The transfer has five explicit dispositions: `Ready`, `ResetRequired`,
`Pending`, `Complete`, and `DeviceLost`. It retains the exact source and
destination generations while `Ready` or `Pending` so neither buffer can be
retired while a command can still name it.

Recording failure before submission produces `ResetRequired`, except that
`VK_ERROR_DEVICE_LOST` produces `DeviceLost`. A non-device-loss submit failure
also produces `ResetRequired`. A submit that reports `VK_ERROR_DEVICE_LOST` is
treated conservatively as `Pending` until the retained fence wait retires any
possible queue use. An unsuccessful wait other than device loss remains
`Pending`. A completion retry issues only another wait on the same fence; it
does not record or submit again.

A wait that reports device loss retires temporary resource dependencies and
publishes no content. If a submit reported device loss and the subsequent wait
returns success, the terminal result is still `DeviceLost` and the destination
remains nonresident. Otherwise, a successful wait reauthenticates the exact
resource generations and publishes the expected identity, changes the transfer
to `Complete`, and releases its temporary source and destination dependencies.

`reset()` refuses `Pending` without destroying the fence or command pool. Any
non-`Pending` reset destroys the fence before the command pool, clears the
implicit command-buffer ownership with the pool, and is idempotent.
Submission-attempt and completion-wait counters make a duplicate submit or
accidental retry path observable in tests without changing the production
contract.

Destruction while `Pending` remains a documented caller-contract violation.
The aggregate must first reach `Complete`, `DeviceLost`, or another terminal
state through the retained completion operation. General recovery from
arbitrary owner destruction requires a broader device-loss lifetime policy and
does not enter this one-shot stage.

## Aggregate ownership and lifetime

`VulkanInstanceGeneration` stores source, destination, and transfer as
logical-device children and swapchain siblings, each with a separate ownership
epoch. Destination acquisition requires the exact live source. Transfer
acquisition requires that exact source and destination, a nonresident
destination, and the live parent chain. Both acquisitions snapshot and recheck
native handles, neutral identity, window generation, resource epochs, and the
aggregate transition epoch around resolution, allocation, and publication.
They rerun the owner and window callbacks and require the expected results at
each freshness boundary.

Execution and completion retry use the same parent operation boundary. They
reject stale requests, reentrant transitions, resource replacement, and
same-looking ABA generations without operating on the replacement.

A successful or failed changed-extent swapchain rebuild leaves all three
upload owners unchanged. Pending transfer work blocks direct transfer,
destination, source, logical-device, and aggregate reset. After completion,
resetting the source first retires the terminal transfer and then the source,
while preserving the resident destination. Full teardown retires swapchain
children, transfer, destination, source, and finally the logical device.

## Focused and native validation

On Linux, seventeen focused global-dispatch, physical-device, logical-device,
upload-source, upload-destination, upload-transfer, configuration, swapchain,
image-view, presentation-target, presentation-pipeline, readback, frame-slot,
parent, requirements, SDL-owner, and SDL-WSI routes pass with warnings as
errors. Direct totals are 6, 8, 9, 9, 9, 10, 12, 6, 11, 7, 5, 12, 52, 119,
7, 25, and 1, for 308 test cases with zero failures.

The hidden SDL/X11 route passes against the pinned Vulkan loader, Mesa
lavapipe, X11, and the Khronos validation layer. It completes the upload,
retires the source, retains the exact resident destination through the existing
initial and changed-extent observed draws, and reports zero validation or VUID
messages.

On macOS, the corresponding seventeen routes pass 306 cases in a universal
Release graph with warnings as errors; the Cocoa-owner route has 23 cases
rather than SDL's 25. The hidden Cocoa route loads the pinned Vulkan loader,
MoltenVK, and Khronos validation layer and passes 251 assertions with zero
validation, VUID, error, or warning messages. It proves the same completed
upload and rebuild lifetime without changing CGL context, global OpenGL
manager, Cocoa owner, 2x backing scale, backing-pixel geometry, or live-window
count. No viewer login, developer signing, or launch of the packaged app is
used.

## Build graph and delivery isolation

The enabled Linux Release graph builds the viewer, appearance utility, default
graph, and serial package successfully under a 7 GiB soft and 10 GiB hard
memory cap. A separate native SDL run from that fresh build passes 1/1. The
validation log contains zero Vulkan validation and VUID messages; five product
warnings are the intentional rejection of OpenGL-only calls on a Vulkan
window.

The corrected release archive is 144,805,424 bytes, has SHA-256
`d3d86ecdd9091a305167420612e41299a3dc78021b53fe5b69ed010ae0792409`,
and passes XZ integrity. Its 6,457 entries comprise 6,336 regular files, 116
directories, and five safe relative links. The archive and staging manifests
are identical with SHA-256
`e5f871ddf305f364045c0fc6931cd91fdf4a024a8207ec63443210a2ce8d1a8f`.
All 17 ELF files are x86-64, contain no debug sections, and include the expected
viewer, plugin host, media plugins, and WebRTC library.

This local Nix package is build-graph evidence, not a redistributable or
privacy-clean delivery. Seven locally built ELF files retain host-specific
absolute source or build paths through compiled file names, and their ELF
interpreter or run path points into the Nix store. No account credential,
network address, private-key marker, or hosted-plan reference was found. The
package and its logs remain disposable evidence and do not enter the commit.
A distribution-oriented build must separately provide prefix mapping and a
relocatable interpreter and run path.

A fresh Linux Release configuration disables the benchmark, Vulkan runtime,
tonemap, SDL WSI, macOS WSI, and Win32 WSI switches while keeping tests and
packaging enabled. Its generated graph has 2,615 targets and 1,387 compile
entries, all with `-Werror`. The forbidden Stage 55 expression has zero hits
across the Ninja graph, compile database, target directories, and 40 CTest
files. A post-build scan finds zero Stage 55 path names or strings across
1,671 eligible project files and zero matching defined symbols across 1,561
ELF or archive inputs. The full default graph and serial package complete
under the same 7 GiB soft and 10 GiB hard memory cap.

The disabled-graph archive is 144,891,880 bytes, has SHA-256
`6a50e5147f6fab4c7cc2f0d23ecef6d5a1438c755842f1cb69730e56b7708231`,
and passes XZ integrity. Its 6,457 entries comprise 6,336 regular files, 116
directories, and five safe relative links. The archive and staging tree match
exactly by normalized path, type, mode, ownership, size, link target, and file
content. There are no duplicate, noncanonical, escaping, special, or set-ID
entries.

All 17 staged ELF files are little-endian x86-64 and contain no debug-related
sections, Vulkan or MoltenVK dependency, undefined Vulkan entry point, or
Stage 55 marker. The only Vulkan-named payload is the existing four-file CEF
SwiftShader set. The five disposable SLPlugin build links do not enter the
package.

Like the enabled baseline, this local Nix package is not reproducible-path or
redistribution evidence. Ten staged files contain local or upstream build
paths, seven project ELF files retain Nix-store run paths, and the main viewer
and SLPlugin use an absolute Nix-store interpreter. These baseline package
properties have no enabled-versus-disabled Stage 55 differential.

The enabled macOS universal Release build completes the viewer, appearance
utility, full graph, and unsigned serial staging package with warnings as
errors. The staged app has a valid property list, 6,707 regular files, and no
broken links. The harness records only the expected disk-image name because
signing is disabled. Its 3,235 deployment-target warnings are longstanding, so
this evidence does not claim macOS 11 runtime compatibility.

A fresh macOS configuration disables the benchmark, Vulkan runtime, tonemap,
SDL WSI, macOS WSI, and Win32 WSI switches. Its generated graph contains zero
Stage 55 markers before and after the build, and its outputs contain zero
Stage 55 named files, symbols, or target-help markers. The universal Release
viewer and serial unsigned staging package both complete successfully with
warnings as errors and minimum deployment target macOS 11 for both `x86_64`
and `arm64` slices.

The staged app is 969 MiB with 6,707 regular files, a valid property list, no
broken links, and no Vulkan-named payload. Across 1,089 Mach-O files it has zero
Vulkan or MoltenVK imports. The app remains unsigned as intended, and the local
unsigned recipe emits no disk image. The build reports 3,249 warnings: 3,203
are the longstanding prebuilt deployment-target mismatch, with the remainder
coming from existing linker and archive diagnostics. It reports no build error
and no privacy finding.

Fresh-host corrections are confined to disposable build state. Linux requires
the selected Xlib and GStreamer plugins-base development include paths, five
immutable runtime-library staging links, the repository autobuild manifest at
its generated lookup path, and a one-edge correction for the known
test-enabled SLPlugin destination bug. Sourcing the captured Nix environment
also requires clearing its purity flag; otherwise the compiler wrapper
silently removes the source and build include paths. None of these corrections
changes repository source or a feature default.

The fresh macOS direct-CMake harness also requires the Autobuild address-size
and platform variables plus `LL_BUILD`; without the latter, CMake generates a
10.13 target even when the release flag strings name macOS 11. The final fresh
graph sets all three in the environment and verifies `minos 11.0` in both main
executable slices.

GCC 15 repeats its known `std::function` array-bounds false positive in one
unchanged outfit-editor translation unit. Only that generated object command
demotes `array-bounds` from an error; the warning remains visible, every other
compile command retains `-Werror`, and repository source is unchanged.

## Source identity and review

The frozen source starts from Stage 54 commit
`e1944c8505e9ddaafc0fa3588ab5ce8a7f8139a4`. Twelve source, build, and test
paths change before this decision note. Their sorted path-and-content record
has SHA-256
`ca88f712556220092a78588e1620dc53c8de4be726fbf73aaaf6f502cb4548d9`.
A synthetic index over the Stage 54 base produces logical tree
`9addd9a989b132fbac871884b6d004b20f083b05`. Its full binary diff has SHA-256
`835d917933f9fe02a541aee29bfccd28c1a26cc0b1ccd355d460116647a078d7`,
its tracked-entry manifest has SHA-256
`d3cf560780d676c8e891b8ff70dc532f83d4e1dae33791c1404a3d95665336dd`,
and its sorted Git tree manifest has SHA-256
`a15bb7e259c27aa6848a1779faf6dc676e8d782174ba9b3c0bda4779c8def713`.
The tree contains 9,612 tracked files. This decision note stays outside those
pre-decision digests to avoid self-reference.

Independent review covers dispatch resolution, memory selection, alias
handling, rollback, synchronization structure, failure transitions, retry,
parent authentication, ABA replacement, moves, reset ordering, platform state,
default-off isolation, formatting, and evidence handling.

## Explicit deferrals

- binding the resident destination as vertex input or changing the
  presentation shader's `gl_VertexIndex` position source;
- reading destination bytes back to the host or claiming byte equality from
  native execution;
- reusable upload queues, suballocation, rings, batching, asynchronous frame
  overlap, multiple transfers, or allocator policy;
- indices, descriptors, images, samplers, textures, materials, scene data, UI
  composition, or general resource ownership;
- capture, screenshot comparison, fixed-scene comparison, or OpenGL parity;
- production renderer selection or replacement of the OpenGL path;
- an OpenGL-to-Vulkan compatibility layer;
- viewer launch, login, world connection, benchmark, retained timing, or any
  CPU, GPU, image-parity, or performance claim;
- Windows build or native execution;
- developer or distribution signing.
