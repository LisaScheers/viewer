# Stage 56 Vulkan resident vertex consumption decision

## Decision

Consume the resident 48-byte upload destination completed in Stage 55 as the
vertex input for the existing Vulkan presentation diagnostic. The vertex
shader no longer synthesizes positions from `gl_VertexIndex`; the pipeline
declares one fixed vertex layout, and each observed draw binds the exact
resident destination before drawing three vertices.

This closes the first buffer upload-to-draw path in master Stage 3. It remains
inside the default-off Vulkan diagnostic graph and does not select Vulkan for
the production viewer, modify the OpenGL renderer, change ordinary window
construction, or alter `swapBuffers()`. Windows remains excluded by user
direction.

## Canonical payload and shader interface

The existing texture-upload fixture is now the single source of truth for the
screen triangle. Its twelve IEC-559 32-bit floats describe three positions
with a fourth padding word per vertex. A compile-time byte view defines the
exact 48-byte description. FNV-1a provides a stable, non-cryptographic checksum
and residency identity. Presentation validation separately requires the
canonical resource handle and a byte-for-byte match of all 48 description
bytes. The generic upload-source owner continues to accept arbitrary 48-byte
descriptions.

The vertex shader declares exactly one `vec3` input at location zero and
writes `vec4(position, 1.0)`. It has no vertex-index built-in, descriptor, or
other reflected input. The embedded artifact generated with glslang 16.4.0 is
636 bytes, or 159 words, with SHA-256
`a2eafa25a5e418187e60e41a8a19a958f9394d83a03506e5d17ec285ba8a7b3f`.
The focused pipeline test's FNV-1a checksum of the 636 embedded bytes is
`0x73a79c93438dbc51`. The fragment artifact remains byte-identical at 304 bytes
with SHA-256
`784c8df9710a1b546fa9873249b463567449f4e48b37c656c6d2ac3584e897cc`.

The offline artifact verifier recompiles and validates both modules, compares
their exact bytes and hashes with the embedded arrays, disassembles the vertex
module to reject `BuiltIn VertexIndex`, and reflects its entry point and input
contract. It also rejects descriptors in either module.

## Pipeline and command recording

The graphics pipeline declares exactly one per-vertex binding at binding zero,
with a 16-byte stride. Location zero reads
`VK_FORMAT_R32G32B32_SFLOAT` from binding zero at offset zero. The fourth word
in each record remains padding. Focused fakes deep-copy the binding and
attribute arrays before checking them, so the assertions do not depend on
temporary create-info storage.

Draw-capable frame-slot resolution transactionally resolves
`vkCmdBindVertexBuffers` with the existing draw dispatch. A recorded draw uses
this order inside the render pass:

1. bind the retained graphics pipeline;
2. bind one exact resident destination buffer at binding zero and byte offset
   zero;
3. set the full-extent viewport and scissor;
4. issue exactly `vkCmdDraw(3, 1, 0, 0)`.

The stage adds no allocation, upload, copy, memory barrier, semaphore, fence,
queue wait, or submission. Stage 55 already records the transfer-write to
vertex-attribute-read dependency on the same validated logical-device queue
before it publishes residency.

## Destination validity and lifetime

A draw accepts only a destination whose immutable description matches the
canonical screen-triangle handle and all 48 payload bytes. It separately
requires the expected and resident FNV identities to equal the canonical
checksum, along with exact live physical/logical-device provenance and a
distinct resident destination with non-null buffer and memory. The destination
must be exactly 48 bytes, unmapped, device-local, and have usage exactly
`VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT`.

The aggregate requires that canonical description during preflight and every
freshness recheck. It also snapshots and rechecks the destination pointer,
ownership epoch, expected and resident identities, native buffer and memory,
byte count, usage, allocation size, memory type, memory properties, and parent
device generation. This rejects stale or same-looking ABA replacements even
when native handles are reused. Missing, nonresident, wrong-payload,
wrong-parent, structurally invalid, or replaced destinations fail with typed
errors before image acquisition. A change observed after acquisition retains
the image for safe cancellation and records no bind, draw, or submission.

The frame slot retains the exact destination generation before its first
native wait or acquisition. Retention spans image-acquired,
submission-pending, presentation-ready, present-pending,
cancellation-pending, and indeterminate states. Aggregate destination and
frame-slot reset refuse teardown while retention is active. Completion,
successful cancellation, or terminal device loss clears retention exactly
once. Presentation retry and completion never bind, record, or submit the draw
again. Cancellation may issue fence-backed empty drain or signal submissions
without recording another draw. Frame-slot and aggregate moves transfer the
retained destination pointer and snapshot, leaving moved-from owners inert.

As with the existing readback contract, directly moving, resetting, or
destroying a standalone destination while a frame slot retains it remains a
caller-contract violation. The aggregate is the supported owner and enforces
the safe order.

## Source and transfer retirement proof

The native routes complete the Stage 55 upload, then retire the host-visible
source and terminal one-shot transfer before the first observed draw. They
retain only the resident destination from the upload chain, including its
canonical handle and 48-byte description, expected and resident FNV
identities, native buffer and memory, usage, and allocation. Initial and
changed-extent draws must both classify every readback pixel as green while
reusing that same destination and without another upload submission.

This proof distinguishes vertex consumption from Stage 55's procedural green
triangle: the SPIR-V has no procedural vertex source, the fake trace binds the
resident destination, and the native output remains green after the only
other upload owners are gone.

## Validation and delivery evidence

The final Linux enabled graph passed all 17 focused runners and their 318
cases. Offline regeneration, SPIR-V validation, disassembly, reflection, and
embedded-byte checks passed. The explicitly enabled SDL/X11 lavapipe test
completed the initial and changed-extent draws from the resident destination,
classified both readbacks as green, and reported no validation finding.

The serial Linux ReleaseOS viewer and package build passed. The final archive
is 145,936,468 bytes with SHA-256
`5d0c8cc103537475a0132877b60d2eed86fb7151b50a5b9e3b09181319b285d0`.
XZ and tar integrity passed, and all 6,460 paths below the archive root match
the staging tree in type, link target, metadata, and content. Both embedded
presentation modules occur exactly once in the packaged viewer. Six
first-party binaries contain no debug sections, direct Vulkan dependency, or
undefined Vulkan import. They retain release symbol tables, so this evidence
describes them as debug-stripped rather than fully stripped. The package has
Nix-store runtime paths and is test evidence, not a generally redistributable
artifact.

The fresh Linux all-six-off graph passed all 132 CTest runners and its serial
ReleaseOS package build. Its 1,387 compile entries, 2,697 targets, and 40 CTest
files contain no Stage 56 target, source, object, symbol, presentation payload,
or project-owned vertex-bind marker. The only Vulkan-named delivery files are
the unchanged four-file CEF/SwiftShader baseline.

The final macOS enabled graph used a universal `arm64;x86_64` Release build
with a minimum target of macOS 11 and signing disabled. The four focused build
targets passed with warnings treated as errors. All 17 focused runners and
their 316 cases passed. The explicitly enabled Cocoa/MoltenVK test completed
the initial and changed-extent draws and readbacks from the same destination,
with the source and transfer retired and no validation finding. The final
viewer-only build passed after restoring the established Python module path to
its post-build environment; no source workaround was made.

The staged macOS app contains 6,707 regular files, valid property lists, and no
broken link. Its viewer and embedded plugin are universal and declare macOS
11.0 as their minimum version. First-party Mach-O files contain no direct
Vulkan or MoltenVK dependency, undefined Vulkan import, or named SPIR-V
payload. The viewer and plugin have only linker-generated ad-hoc signatures,
with no signing authority, team identifier, or user/developer identity.

The fresh macOS all-six-off universal Release graph and package target passed
with warnings treated as errors and signing disabled. Its regenerated project,
CTest metadata, objects, dependency records, binaries, and staged delivery
contain no Stage 56 target, source, object, symbol, presentation payload, or
project-owned vertex-bind marker. Only the unchanged CEF/SwiftShader baseline
contains Vulkan markers. No DMG remained after the target completed.

No Windows work, viewer launch, login, benchmark, or developer signing was
performed. All 21 changed source files were byte-identical across the primary
tree and both final macOS validation trees.

## Independent review

Independent review found no remaining correctness, synchronization, lifetime,
shader, platform, or disabled-graph blocker. It identified and corrected the
32-bit-float ABI assumption, presentation validation that previously treated
FNV identity as an exact payload discriminator, and documentation that omitted
cancellation's empty fence submissions. Collision-forcing tests prove that the
exact comparisons, rather than the checksum alone, reject altered content at
both the frame-slot and aggregate boundaries.

## Deferred work

This stage does not add images, image views, samplers, descriptors, texture
transitions, scene rendering, renderer selection, performance measurement, or
an OpenGL compatibility layer. No viewer login, benchmark, developer signing,
or packaged-app launch is part of its evidence. The next committable resource
slice is selected only after this stage passes, commits, and the master plan is
reanalyzed.
