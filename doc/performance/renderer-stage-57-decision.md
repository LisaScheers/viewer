# Stage 57 Vulkan texture image ownership decision

## Decision

Own one canonical streamed-texture destination as a durable Vulkan image
generation. The generation qualifies the exact RGBA8 format contract, creates
one optimal-tiled image, binds one formally dedicated device-local allocation,
and creates one color view over all three declared mip levels.

This is the first durable image owner intended for later sampling in master
Stage 3. It remains inside the default-off Vulkan diagnostic graph and does
not select Vulkan for the production viewer, modify the OpenGL renderer,
change ordinary window construction, or alter `swapBuffers()`. Windows
remains excluded by user direction.

## Canonical description and capability boundary

The owner derives its immutable description from the neutral texture-upload
contract. It accepts only replacement-image handle `{11, 2}`, revision 23,
resident extent 8 by 4, logical extent 32 by 16, discard level 2, RGBA8 UNORM,
three mip levels, one array layer, one sample, and undefined initial state.
The Vulkan image has flags zero, 2D type, optimal tiling, exclusive sharing,
no queue-family-index array, and usage exactly transfer source, transfer
destination, and sampled image.

Before creating anything, the resolver requires optimal-tiling sampled-image,
transfer-source, transfer-destination, blit-source, blit-destination, and
sampled-image linear-filter features. It queries the exact image tuple and
requires sufficient width, height, depth, mip, layer, and sample-count limits.
Unsupported-feature and insufficient-limit errors record both the required
and available value. The generation retains the complete reported image-format
properties, including `maxResourceSize`, but does not compare that
implementation limit with tight texel bytes or memory requirements.

All twelve Vulkan commands are resolved through the physical and logical
device generations. The library uses `VK_NO_PROTOTYPES`, has no global Vulkan
call, and introduces no Vulkan loader link or import.

## Image, memory, and view ownership

Every native handle output starts at `VK_NULL_HANDLE`, and every queried
structure is zero-initialized. The resolver creates the frozen image, queries
`VkMemoryRequirements2` with chained `VkMemoryDedicatedRequirements`, and
rejects zero size, zero or non-power-of-two alignment, empty compatible-memory
bits, malformed memory tables, or insufficient heap capacity. It selects the
first compatible device-local memory type after excluding protected, lazily
allocated, device-coherent AMD, and QCOM tile-memory cases.

The allocation always chains `VkMemoryDedicatedAllocateInfo` to the image and
uses the implementation's required size. Binding uses offset zero. The owner
retains the exact memory requirements, selected type and flags, and both
dedicated-allocation preference bits.

One identity-swizzled 2D RGBA8 view covers color aspect, base mip zero, all
three mip levels, base array layer zero, and one layer. The owner publishes
only after image creation, allocation, binding, view creation, and parent
revalidation all succeed.

Each successful non-null create or allocation is treated as a separate
ownership occurrence even when non-dispatchable handle bits repeat. Failed
call outputs are never inspected or destroyed. A failed transaction retires
only its successful occurrences, exactly once, in view, image, memory order.
Reset first detaches every handle and callback, so destruction callbacks may
reenter without double retirement. Move construction transfers the complete
occurrence set and leaves the source inert.

## Aggregate lifetime

The instance aggregate owns the generation transactionally with a dedicated
epoch. It snapshots and rechecks aggregate and window provenance around caller
callbacks and the allocation checkpoint. The child resolver revalidates its
physical- and logical-device parents after every dispatch-resolver and native
callback. Before publication the aggregate rechecks the exact neutral
description, retained capability and allocation metadata, native handles,
view type and range, and parent identity. Pointer and epoch checks reject
same-looking ABA replacement.

The image is device-scoped rather than swapchain-scoped. Successful and failed
swapchain rebuilds preserve the same owner and epoch. Direct reset balances the
view, image, and memory; logical-device and full teardown retire this child
before its device parent. Aggregate move construction transfers both the
owner and its epoch.

## Validation and delivery evidence

The final Linux enabled graph passed all 18 focused runners and their 333
cases. The owner-focused runner accounts for 9 cases, and the aggregate runner
accounts for 129. The explicitly enabled SDL/X11 lavapipe route created and
reset the canonical image, dedicated allocation, and three-mip view with
required validation and no reported validation finding.

The Linux ReleaseOS viewer and package build passed. The final archive is
145,947,256 bytes with SHA-256
`268037a6fdb1dc8ae92bc011891c06e67af35cf52e2216e36799b06461f905e3`.
XZ integrity passed; all 6,461 entries are beneath one archive root, with no
duplicate or unsafe path, and the archived viewer matches the packaged
staging viewer byte for byte. The packaged viewer has no `.debug_*` section,
Vulkan entry in its `DT_NEEDED` table, or shipped Stage 57 test artifact. Its
retained ELF `.symtab` and `.strtab` contain the expected linked owner symbols.
The package has Nix-store runtime paths and is verification evidence, not a
generally redistributable Linux artifact.

The fresh Linux all-six-off Release graph and package build passed, followed
by all 132 CTest runners in 178.26 seconds. Its generated graph, commands,
dependencies, compile database, objects, archives, symbols, and strings contain
no Stage 57 source, target, object, or owner marker. The final archive is
145,761,908 bytes with SHA-256
`a8de64ae9060ad1238bdc4273268f357f61387a749f8f47ecaabaaeae3442502`.
XZ integrity passed; all 6,457 members are unique, beneath one archive root, and
use safe paths. Its only Vulkan-named payload is the existing four-file
CEF/SwiftShader baseline, and the viewer has no Vulkan entry in its `DT_NEEDED`
table. The repository's existing manifest bookkeeping leaves its generic raw
archive and copy-touch outputs nominally pending after packaging, while the
channel-named delivery archive itself is complete and stable.

The final macOS enabled graph and local package app passed in a universal
`arm64;x86_64` Release build with minimum target macOS 11 and Xcode developer
signing disabled. All 18 focused runners passed, including all 9 owner and all
129 aggregate cases. The explicitly enabled Cocoa/MoltenVK route created and
reset the same canonical image, allocation, and view contract with required
validation and no reported validation finding. The owner archive is universal,
has minimum target macOS 11 in both slices, and has SHA-256
`4ee8c6a17011c284e4b58f9da1b8a3780c8bdbf9cd9de0a48daaac6f93954efd`.
The test executable is intentionally host-only under the repository's test
architecture policy and has no Vulkan loader link.

The enabled packaged app's 145,714,096-byte viewer has SHA-256
`2fa8a3f324bd77af7b75f21f6a38b36af4cb21a4eafa365d5ebdc476644030ec`.
The viewer and focused resource libraries are universal and declare macOS 11
as their minimum version. The viewer has no direct Vulkan or MoltenVK
dependency. Its executable carries only an ad-hoc signature, with no team or
developer identity.

The fresh macOS all-six-off universal Release graph and local package app
passed, followed by all 132 CTest runners. Its generated project, build files,
objects, libraries, viewer, and app contain no Stage 57 target, source, object,
symbol, or owner marker. The viewer has no direct Vulkan or MoltenVK dependency.
It is 145,746,992 bytes with SHA-256
`245a7e079ce6e036507ed8426fc0e1546745133380079bf655f42115f9df382b`,
is universal, declares minimum macOS 11, and has only an ad-hoc signature with
no team or developer authority. Local development packaging produces the app
and touch record but no installer file; CI additionally archives the app as
`viewer.tar.xz` when `RUNNER_TEMP` is set.

The final 11-file source manifest used by both macOS graphs is byte-identical
to the primary tree, with aggregate SHA-256
`5c0387987c4612a97c16afa6109c461a8aa99664ca2bc78a7e9fdb72fe9862cc`.

No Windows work, viewer launch, login, benchmark, packaged-app smoke, or
developer signing was performed. The two unrelated untracked HTML documents
remain excluded from this stage.

## Independent review

Independent dispatch, owner, aggregate, platform, scope, formatting, and
privacy reviews found no remaining blocker. Review corrections made before the
final validation included detaching owner state before destruction callbacks,
revalidating parents after every resolver and native callback, preserving typed
capability evidence, reading allocation and view outputs only after successful
calls, unioning coincident swapchain and texture format capabilities in the
aggregate fake, and strengthening macOS structure and destruction-order proof.

## Deferred work

This stage owns no pixel contents and publishes no current-layout or residency
state. It adds no staging source, map, copy, blit, barrier, command buffer,
queue work, semaphore, fence, sampler, descriptor, shader change, descriptor
or draw-time resource binding, or sampled draw. The next committable slice is
selected only after this stage passes, commits, and the rolling plan is
reanalyzed.
