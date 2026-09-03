# Stage 59 Vulkan texture upload transfer decision

## Decision

Own the first ordinary texture upload as one device-scoped, one-shot Vulkan
transfer generation. The generation retains the exact immutable Stage 58
source and Stage 57 destination while work can still use them. It owns one
command pool, one primary command buffer, and one fence. It records and submits
the fixed upload and mip-generation sequence exactly once, then publishes
destination residency only after conclusive fence completion.

This remains inside the default-off Vulkan diagnostic graph. It does not
select Vulkan for the production viewer, alter the OpenGL renderer, add sampled
binding or drawing, restart benchmarking, admit Windows, launch or log into the
viewer, or require developer signing.

## Frozen resource and queue chain

Resolution requires the exact live physical and logical device generations,
the canonical three-mip destination description, and the exact value-owned
source description. Source and destination must name replacement image
`{11, 2}` at revision 23. The retained source has a non-null 144-byte
transfer-source buffer and a nonzero content identity. The retained destination
has its canonical image, memory, and three-mip view and is still nonresident in
the neutral `Undefined` state.

The transfer uses the logical generation's retained unified queue and family.
The selected family must contain the retained queue index and advertise
graphics capability. It need not advertise the transfer bit separately:
graphics queues support the required transfer commands, and the two image
blits require graphics capability.

Every exact source and destination field is retained by value or provenance.
Stable identities support evidence and ABA detection, but do not replace exact
description, pointer, epoch, native-handle, revision, and byte comparisons.

## Native ownership

All required entry points resolve through the retained instance and device
resolvers under `VK_NO_PROTOTYPES` before native mutation. Before queue
submission, resource and parent provenance is revalidated after every resolver
and native callback. Submission and every fence result then preserve or
terminally resolve the retained lifetime before publication.

The owner creates one flags-zero command pool for the retained queue family,
allocates one primary command buffer from that pool, and creates one unsignaled
fence. Native outputs start null. Failed calls never donate their output bits,
and each successful non-null occurrence remains independently owned even when
fake handles repeat. Rollback destroys a successful fence before a successful
pool; pool destruction owns command-buffer retirement.

Reset refuses while work is pending or while an operation callback is active.
Otherwise it detaches every handle, callback, parent, resource pointer,
description, identity, counter, and state before invoking fence and pool
destruction. Direct move from an active operation yields an inert destination
without modifying the running owner. The source and destination generations
must outlive `Ready` and `Pending`; the native logical device must outlive
transfer reset or destruction in every disposition because the fence and pool
remain device children. Destruction never submits or waits. The caller must
therefore resolve a pending transfer before destroying either the standalone
owner or its aggregate; violating that externally synchronized lifetime
precondition is an API error rather than an implicit blocking wait.

## Exact recording

The one-time command buffer contains this sequence:

1. A whole-144-byte buffer barrier makes host writes available to transfer
   reads, from the host stage to the transfer stage.
2. One image barrier transitions all three color mips from undefined to
   transfer destination, from top-of-pipe with no source access to transfer
   write access.
3. One `vkCmdCopyBufferToImage` contains four tightly packed 8 by 1 regions.
   Source byte offsets are 108, 72, 36, and 0, while destination Y offsets are
   0, 1, 2, and 3. Both packed-row fields are zero. The mapping reverses the
   top-left source into the diagnostic image's bottom-left convention and
   makes the four poison bytes after each row unreachable.
4. Mip zero transitions from transfer destination to transfer source, then a
   linear self-blit generates mip one from 8 by 4 to 4 by 2.
5. Mip one transitions from transfer destination to transfer source, then a
   second linear self-blit generates mip two from 4 by 2 to 2 by 1.
6. One final barrier call transitions mips zero and one from transfer source to
   shader read, and mip two from transfer destination to shader read. Access
   moves from the corresponding transfer reads or writes to shader reads,
   from the transfer stage to the fragment-shader stage.

The command buffer then ends. No sampler, descriptor, pipeline, draw, readback,
queue-idle wait, second submission, or implementation-dependent mip hash is
part of this stage.

## Submission and completion

`Ready` owns resources but has performed no queue operation. `execute()`
accepts a finite timeout, records once, and submits one command buffer without
semaphores to the exact retained queue and fence. A successful submit changes
the disposition to `Pending` before the fence wait is resolved.

Zero is a valid nonblocking poll; `UINT64_MAX` is rejected so the API cannot
silently create an unbounded wait. A timeout or other inconclusive wait retains
the complete pending generation. `retryCompletion()` may only wait on the same
fence with another finite timeout. It never begins, records, ends, resets, or
submits the command buffer again.

Every legal command-recording failure becomes `ResetRequired`. The Vulkan
return contracts for command-buffer begin and end do not include device loss,
so the owner does not invent an impossible device-lost recording branch. An
ordinary submit failure is resettable without a wait. A device-lost submit
follows the conservative retained-fence resolution path. Fence-reported device
loss becomes terminal without publication.

Fence success alone is not enough. The transfer revalidates the complete
physical, logical, queue, source, destination, description, revision, content,
and native-handle chain. It then publishes revision 23, the exact source
identity, and neutral `ShaderRead` state together on the destination. Only
after that one-shot publication succeeds does the transfer enter `Complete`
and release its borrowed source and destination pointers.

The identity is lineage for the uploaded source bytes. The two linearly
filtered mip levels are valid generated contents, but cross-driver rounding is
implementation-dependent and is deliberately not represented as an exact byte
hash.

## Aggregate lifetime

`VulkanInstanceGeneration` owns the transfer transactionally with a dedicated
epoch. Acquisition freezes and rechecks global, instance, surface, physical,
logical, source, destination, and transfer provenance around user callbacks,
native resolution, allocation, and publication. Pointer plus epoch checks
reject same-looking ABA replacement. A nested exact acquisition may publish
the winning generation; a stale outer candidate rolls back rather than
overwriting it.

Aggregate operations hold a native-operation guard before user callbacks and
through recording, submission, waiting, and publication. Recursive execution
is rejected before it can record or submit again. The same guard blocks move,
rebuild, and direct or transitive resets during callbacks.

A pending transfer atomically blocks transfer, texture-source,
texture-destination, logical-device, surface, and full reset. The logical
device path preflights this obligation before retiring any independent
swapchain object. Changed-extent swapchain rebuild is otherwise independent
and preserves `Ready`, `Pending`, and `Complete` texture-transfer state.
Direct standalone use carries the same explicit requirement that both parent
generations and both retained resources outlive every ready or pending
operation; the aggregate is the production enforcement of that dependency.

A terminal direct transfer reset preserves source, destination, and any
published residency. Source reset retires the transfer first and preserves the
destination. Destination reset retires transfer, then source, then the image.
Full device teardown orders transfer fence and pool, source buffer and memory,
destination view, image and memory, then logical device.

Transfer retirement moves the aggregate slot out and advances its epoch before
either native teardown callback. A dedicated guard spans both callbacks and
rejects transfer or swapchain-root publication while also blocking move,
rebuild, and reset reentry.

Destination retirement likewise moves its aggregate slot out and advances its
epoch before destroying the view, image, or memory. Its dedicated guard blocks
destination and swapchain-root publication plus move, rebuild, and reset
reentry across all three callbacks. These adversarial callback guarantees are
scoped to the new texture transfer, source, and destination chain. Pre-existing
legacy device-child and logical-device callback transactions are not widened
by this stage.

## Validation boundary

Stage 58 already established fresh enabled and all-six-off builds, packages,
132-test baselines, and graph inventories on Linux and macOS. Stage 59 keeps
the same default-off CMake gate and adds no production or package input.
Accordingly, its disabled proof is a fresh configure plus target, compile-unit,
object, symbol, link-record, and class-marker scan. Any marker, gate change, or
ambiguous evidence escalates to a new full disabled build and package.

The final Linux tree passed the destination 10/10, source 10/10, transfer
11/11, aggregate 142/142, SDL owner 28/28, and native WSI 1/1 suites. That is
202/202 cases. Required-validation lavapipe executed the upload and reported no
validation message. The warnings-as-errors build linked the viewer and
`llappearance`; the viewer has no direct Vulkan dependency or undefined Vulkan
symbol.

The final macOS tree passed the same six roles at 10/10, 10/10, 11/11,
142/142, 25/25, and 1/1. That is 199/199 cases. Every focused binary is
universal x86_64 and arm64. Required-validation MoltenVK completed under an
external process bound with no validation diagnostic. The unsigned viewer and
`llappearance` build succeeded for a minimum deployment target of macOS 11.
Both output slices have the expected architecture, and the viewer has no
direct Vulkan or MoltenVK link and no undefined Vulkan symbol. The final 15
source files matched the Linux worktree byte for byte. Existing prebuilt
archive warnings remain dependency debt and did not change the output's macOS
11 minimum.

A fresh Linux configure with every Vulkan option disabled produced 1,387
compile commands and no Stage 59 target, compile unit, object, symbol, link
record, generated file, or class marker. The gate and package inputs did not
change, so the clean Stage 58 disabled delivery baseline did not require
another package build. Changed-hunk and new-file clang-format 18 checks, diff
and privacy scans, source parity, and an independent final review all passed.
No package, signing, viewer launch, login, benchmark, or Windows work ran.

## Deferred work

This stage creates no sampler, descriptor-set layout or pool, descriptor write,
sampled shader, pipeline-layout dependency, frame binding, sampled draw, or
pixel readback. Those form the next boundary only after this stage commits and
the rolling plan is reanalyzed. Fixed-scene parity, lifecycle completion,
performance evidence, production backend selection, and the deferred Windows
admission proof remain later master-plan work.
