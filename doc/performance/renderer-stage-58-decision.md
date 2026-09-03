# Stage 58 Vulkan texture upload source decision

## Decision

Own the canonical texture upload's 144-byte source packet in one immutable,
device-scoped Vulkan staging generation. The generation creates one
transfer-source buffer, binds one separate host-visible allocation, copies the
complete padded top-left packet, makes noncoherent writes visible when needed,
and closes the mapping before publication.

This is the host side of the first ordinary texture upload in master Stage 3.
It remains inside the default-off Vulkan diagnostic graph and does not select
Vulkan for the production viewer, modify the OpenGL renderer, change ordinary
window construction, or alter `swapBuffers()`. Windows remains excluded by
user direction.

## Immutable description

`VulkanTextureUploadSourceDescription` contains the replacement image handle
`{11, 2}`, expected revision 23, and a value-owned array of exactly 144 bytes.
The fixed shape derives from the neutral contract: 8 by 4 RGBA8 texels, a
36-byte row pitch, top-left row origin, and four padding bytes after every
32-byte texel row. Pixel values remain arbitrary within that exact shape. The
fixed diagnostic fixture is the native proof input, including all 16 distinct
poison padding bytes.

A null or noncanonical replacement-image handle or a revision other than 23
fails before dispatch lookup or allocation. Wrong byte counts and inconsistent
fixed metadata are unrepresentable through the fixed array and derived
accessors.

The description does not invent a backend-neutral handle for the private
staging buffer. The existing neutral buffer handle continues to identify only
the screen-triangle vertex resource. The generation retains a stable nonzero
FNV-1a identity for diagnostic lineage, while `matchesDescription()` compares
the image handle, revision, and every source byte. The checksum is not an exact
discriminator.

## Buffer, memory, and host visibility

The native buffer has flags zero, size 144, usage exactly
`VK_BUFFER_USAGE_TRANSFER_SRC_BIT`, exclusive sharing, and no queue-family
array. Its separate allocation is bound at offset zero and uses the complete
reported memory requirement.

Memory validation rejects malformed tables, invalid heap indexes, an
undersized or host-unrepresentable requirement, zero or non-power-of-two
alignment, and empty compatible-type bits. Selection skips candidates with
forbidden type or heap flags or an undersized heap. It is deterministic:
host-visible coherent memory is preferred, followed by host-visible
noncoherent memory. Device-local flags are permitted alongside host visibility
for unified-memory implementations.

The resolver maps the whole allocation at offset zero, copies all 144 bytes,
and leaves any allocation tail untouched. A coherent allocation needs no
flush. A noncoherent allocation is flushed from offset zero through
`VK_WHOLE_SIZE`, which is valid because the complete allocation is mapped.
The resolver unmaps before it publishes the generation, and no mapped pointer
is retained or exposed.

## Dispatch and ownership

All eleven commands resolve through retained instance and device resolvers
under `VK_NO_PROTOTYPES` before the memory query or first resource mutation.
The resolver revalidates the exact physical and logical parents and frozen
description after every dispatch lookup and forward-path native call before
publication.

Every native output starts null. A failed create, allocation, or map call owns
nothing from its output and never consumes possibly poisoned bits. Every
successful non-null buffer or allocation is tracked independently even when
opaque handle bits repeat. A successful map call creates one mapping occurrence
even if its returned address is null, so that typed failure still unmaps once.
Rollback unmaps an active mapping, destroys the buffer, then frees memory.
Reset first detaches all handles, callbacks, parents, metadata, and identity
before invoking teardown callbacks, making destruction reentry idempotent.
Move construction transfers the complete occurrence set and leaves the source
inert.

## Aggregate lifetime

`VulkanInstanceGeneration` owns the staging generation transactionally with a
dedicated epoch. Acquisition requires the exact live Stage 57 image
destination with matching handle and revision. The core staging owner does not
retain that image pointer.

The aggregate snapshots and rechecks aggregate, source, destination, window,
physical-device, and logical-device identity around callbacks, resolution,
allocation, and publication. Pointer and epoch checks reject same-looking ABA
replacement. A native-acquisition guard refuses move, rebuild, and reset while
a candidate exists. A nested exact acquisition may publish first; freshness
checks then reject and roll back the older outer candidate rather than
overwriting the winner.

The source is device-scoped and swapchain-independent. Successful and failed
swapchain rebuilds preserve the source and destination. Direct source reset
preserves the image and presentation chain, while destination reset first
retires its dependent source. Aggregate move transfers both owners and their
epochs. Logical-device and full teardown retire the staging buffer and memory
before the texture view, image, memory, and device parent.

Source retirement detaches the aggregate slot and advances its epochs before
either native teardown callback. A teardown-specific guard then spans buffer
destruction and memory release. During that interval, source and swapchain-root
publication are rejected, while move, rebuild, and direct or transitive reset
remain blocked. This prevents callbacks from recreating children after a
parent reset has already passed their teardown position.

## Validation evidence

The enabled Linux Release graph passed all 19 focused runners serially, with
351 cases in total. The texture-source owner passed 10 cases, the aggregate
owner passed 136, and the SDL owner passed 27. The opt-in X11 and lavapipe
native route passed its single case under required validation with no VUID,
validation error, or validation warning.

The enabled universal macOS Release graph passed the same 19 focused runners
serially, with 348 platform-appropriate cases in total. The texture-source
owner passed 10 cases, the aggregate owner passed 136, and the Cocoa owner
passed 24. The opt-in loader and MoltenVK native route passed its single case
under the Khronos validation layer with a zero validation snapshot and no
VUID, validation error, or validation warning. The three-case platform
difference belongs to the existing SDL and Cocoa owner coverage; no synthetic
cases were added to force equal totals.

Linux and macOS both proved coherent and noncoherent source-memory behavior in
focused fakes, exact 144-byte publication, rebuild preservation, and
source-before-image-before-device retirement. No benchmark timing was run or
retained.

## Enabled delivery evidence

The stripped Linux archive is 145,975,080 bytes with SHA-256
`238307b5699a3c9c68abfbb82d1d0b387190b4cb9decad2fc3943b0370f3ccf0`.
The packaged macOS main executable is 145,714,096 bytes with SHA-256
`2fa8a3f324bd77af7b75f21f6a38b36af4cb21a4eafa365d5ebdc476644030ec`.
The macOS package and the relevant Stage 58 libraries contain both `arm64` and
`x86_64` slices at minimum macOS 11. Focused macOS runners use the project's
native `arm64` test architecture and also report minimum macOS 11.

The macOS package has no identity signature or TeamIdentifier. No viewer was
launched, no account was used, and no login, benchmark, Windows work, or
developer signing occurred on either platform.

## Linux default-off evidence

A new Release directory with all six benchmark and Vulkan switches disabled
built the complete graph and stripped package. All 132 ordinary tests passed
serially. Generated targets, compilation records, files, objects, libraries,
and binaries contain no Stage 58 source or class marker. Neither the built nor
packaged viewer has a Vulkan dynamic dependency.

The all-off archive is 144,714,784 bytes with SHA-256
`9dea9abc0f790261d6a01140b87ec72b04b7b4446b3d9a145dc010c7d7604845`
and 6,457 unique members. Its only Vulkan-named members are the four recorded
CEF and SwiftShader baseline files: two ICD manifests, `libvulkan.so.1`, and
`libvk_swiftshader.so`. The known Ninja restat behavior leaves only the
manifest-copy and package edges scheduled after a successful build.

The fresh Linux environment needed the `gst-plugins-base` development header
path and five compiler-runtime files in the build's dependency search area.
Those were build-environment corrections only; no repository or generated
graph workaround was made.

## macOS default-off evidence

A new Xcode Release directory with all six switches disabled built both
`arm64` and `x86_64` slices at minimum macOS 11. All 132 ordinary tests passed
serially, and the explicit unsigned package target passed. The packaged main
executable is 145,746,992 bytes with SHA-256
`348820ead609d8e9497dd9a0e5d752e7da016f76bdc045c92531a0535c41b769`.

Generated targets and link records contain no Stage 58 source, compile unit,
object, output archive, symbol, or binary string marker. The packaged main and
the all-off render and window archives contain no runtime Vulkan class symbol.
The package contains no Vulkan-, MoltenVK-, or VkLayer-named file and no SPIR-V
file. Its main executable has no Vulkan or MoltenVK import and no undefined
`vk*` symbol. The only Vulkan-named generated entries are the always-present
inactive artifact-cleanup target and the API-neutral window-requirements test.

The fresh direct CMake environment needed the established autobuild platform,
address size, revision, and Python dependency variables. Those values only
restored the declared build configuration; no source or generated-graph
workaround was made. Signing remained disabled with an empty identity, and the
package has no TeamIdentifier.

## Reproducibility and review

After the final lifetime patch, SHA-256 manifests match for all 11 code, test,
and CMake files across the local tree and both enabled and all-off Vega source
snapshots. Clang-format 18 leaves every changed C++ hunk and each complete new
owner file unchanged, and `git diff --check` passes.

The exact 12-path intended change contains no credential, private endpoint,
host path, result artifact, generated binary, or out-of-scope command,
transfer, residency, rendering, signing, Windows, or production-selection
work. The two unrelated untracked HTML documents remain excluded. Independent
source-owner and cross-slice reviews are clean after adding teardown guards for
source and swapchain-root republication and tests for both destruction
callbacks. The intended commit parent is
`491d0d68773ed5b3ae7d8b0966695602d907f7cb`.

## Deferred work

This stage records no Vulkan command and performs no GPU upload. It does not
reverse rows, exclude padding, copy into the image, generate mips, transition
an image layout, submit queue work, wait on a fence, publish residency, create
a sampler or descriptor, change a shader or pipeline, bind a sampled resource,
draw, or read back pixels. The Stage 57 destination remains nonresident with
only its immutable undefined creation layout published.

The next committable slice is selected only after this stage passes, commits,
and the rolling plan is reanalyzed. Its candidate boundary is a one-shot,
fence-backed transfer that retains this source and the image, reverses four
rows while excluding padding, generates two mips, transitions all three levels,
and publishes shader-readable residency only after conclusive completion.
