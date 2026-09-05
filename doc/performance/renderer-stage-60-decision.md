# Stage 60 Vulkan sampled texture binding decision

## Decision

Own the complete fixed sampled-image interface as one move-only Vulkan
generation. The generation retains the exact resident Stage 57 destination
after the Stage 59 transfer completes. It owns one sampler, one descriptor-set
layout, one pipeline layout, one descriptor pool, and the pool's single
descriptor set. Resolution performs one descriptor write and publishes only
after every retained parent fact is still current.

This remains inside the default-off Vulkan diagnostic graph. It does not
select Vulkan for the production viewer, alter the OpenGL renderer, add shader
modules or a graphics pipeline, bind or draw a descriptor, read pixels back,
restart benchmarking, admit Windows, launch or log into the viewer, package a
release, or require developer signing.

## Fixed sampled interface

The neutral contract contributes sampler handle `{1, 1}`. The Vulkan binding
uses descriptor set 0, binding 0, descriptor count 1, and
`VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER` visible only to the fragment
stage. Its pipeline layout contains that one set layout and no push constants.

The sampler has this exact state:

* linear minification and magnification;
* linear mip filtering;
* clamp-to-edge addressing on U, V, and W;
* zero mip LOD bias;
* anisotropy disabled with maximum anisotropy 1;
* comparison disabled with `VK_COMPARE_OP_ALWAYS` retained as the neutral
  comparison value;
* minimum LOD 0 and maximum LOD 2;
* floating-point transparent-black border color; and
* normalized coordinates.

The description is deliberately narrow. It cannot express more sets,
bindings, array elements, samplers, image views, shader stages, immutable
sampler arrays, descriptor indexing, variable descriptor counts, or
update-after-bind behavior.

## Retained resource chain

Resolution requires the exact live physical and logical device generations,
the canonical sampled-binding description, and the exact resident texture
destination generation. The destination must still match its canonical
three-mip RGBA8 description, retain a non-null view over all three color mips,
publish revision 23 with a nonzero source-content lineage identity, and be in
the neutral `ShaderRead` state represented by
`VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`.

The binding freezes physical and logical device provenance, the destination
description and generation, the sampler resource handle, resident revision,
content identity, image view, image layout, descriptor set number, and binding
number. Stable identities assist evidence and ABA detection, but never replace
exact pointer, epoch, description, native-handle, revision, and parent checks.

The source and transfer generation are not dependencies after transfer
completion. They may be reset before binding acquisition or while the binding
is live. The retained destination generation and native logical device must
outlive binding reset or destruction.

## Transaction and native ownership

All required device entry points resolve through the retained instance and
device resolvers under `VK_NO_PROTOTYPES` before native mutation. The complete
set is:

1. `vkGetDeviceProcAddr`;
2. `vkCreateSampler` and `vkDestroySampler`;
3. `vkCreateDescriptorSetLayout` and `vkDestroyDescriptorSetLayout`;
4. `vkCreatePipelineLayout` and `vkDestroyPipelineLayout`;
5. `vkCreateDescriptorPool` and `vkDestroyDescriptorPool`;
6. `vkAllocateDescriptorSets`; and
7. `vkUpdateDescriptorSets`.

The owner creates the sampler, descriptor-set layout, pipeline layout,
descriptor pool, and descriptor set in that order. Native outputs start null.
A failed call never donates poisoned output bits. A successful call that
returns a null handle is a failure. Each successful non-null occurrence is an
independent ownership obligation even when a fake implementation returns equal
handle bits for different object types or repeated creations.

The descriptor pool contains one pool size for one combined image sampler,
allows one set, and does not enable individual descriptor-set freeing. Pool
destruction owns descriptor-set retirement. The one write targets set 0,
binding 0, array element 0, descriptor count 1, and supplies the owned sampler,
exact resident view, and shader-read-only layout.

`vkUpdateDescriptorSets` returns no result. Its return is therefore only a
callback boundary, not proof that parents stayed current. Resolution
revalidates the complete physical, logical, destination, view, revision,
identity, description, and ownership chain immediately after the update and
publishes the owner only when that check succeeds.

Any missing command, invalid input, native failure, null success, allocation
failure, callback mutation, stale generation, or ABA replacement leaves no
published partial binding. Rollback and reset detach every handle, callback,
parent pointer, description, identity, and layout before destroying the pool,
pipeline layout, descriptor-set layout, and sampler in reverse dependency
order. Reset is idempotent. Reentry during native teardown sees the aggregate
slot detached and cannot publish into the active teardown transaction.

## Aggregate lifetime

`VulkanInstanceGeneration` owns one sampled-binding generation and a dedicated
epoch. Acquisition validates callback shapes and native-window provenance,
then requires the exact live instance, surface, physical device, logical
device, and resident destination. It freezes and rechecks the entire chain
around resolver callbacks, native creation, descriptor allocation, the void
descriptor update, allocation checkpoints, and publication.

A nested exact acquisition may publish the winning generation. The stale
outer candidate rolls back instead of overwriting it. Pointer plus epoch checks
reject same-looking ABA replacement. The binding slot is moved out and its
epoch advances before any destroy callback. A dedicated teardown guard blocks
binding acquisition, aggregate mutation, move, and swapchain-root publication
until all four destroy callbacks finish.

Direct binding reset preserves the swapchain, transfer, source, destination,
and device. Direct transfer and source reset preserve the binding because it
does not retain either object. Destination reset first preflights any pending
transfer without mutation, then retires binding, terminal transfer, source,
and destination. Changed-extent swapchain rebuild succeeds or fails without
changing the binding.

Logical-device and full aggregate teardown retain the established
swapchain-first ordering because later presentation work may borrow the sampled
pipeline layout. They then retire the binding before the texture destination
and logical device. Callback-time binding teardown extends the existing
swapchain configuration guard so a nested rebuild cannot publish a new chain
while the retained pipeline layout is being destroyed.

## Validation boundary

Focused owner tests inspect every create structure, descriptor allocation and
write, required dispatch entry, invalid input, missing command, native failure,
null success, poisoned output, equal handle occurrence, rollback order, move,
idempotent reset, and detached callback state. Mutation coverage includes every
resolver, creation, allocation, update, and destroy callback.

Aggregate tests cover callback validation, freshness, nested winner and stale
outer rollback, same-looking ABA replacement, allocation failure, reentry,
reset order, pending-transfer preflight, source and transfer independence,
destination retention, and swapchain rebuild preservation. The platform owner
tests and both native WSI routes complete the real Stage 59 upload before
acquiring the binding. Native lavapipe and MoltenVK runs use required Khronos
validation and do not draw or read back.

## Final evidence

Linux passed 220 of 220 affected checks: 10 destination, 10 source, 11
transfer, 10 sampled-binding, 149 aggregate, 29 SDL owner, and one native X11
WSI case. The native lavapipe path ran with Khronos validation required and
reported no diagnostic. The warnings-as-errors viewer and `llappearance`
build passed serially. The final viewer has neither a direct Vulkan dependency
nor an undefined `vk*` entry point.

macOS passed 217 of 217 affected checks independently on both arm64 and
x86_64: the same four focused texture roles, 149 aggregate cases, 26 Cocoa
owner cases, and one required-validation MoltenVK native case. Validation
snapshots and log scans were clean on the native arm64 and Rosetta x86_64
runs. All seven focused executables, their supporting archives,
`libllappearance.a`, and the unsigned viewer are universal x86_64 plus arm64.
Every inspected Mach-O slice retains a macOS 11 minimum and has no direct
Vulkan or MoltenVK link or unresolved `_vk*` entry point.

The macOS viewer compiled and linked both slices before its first post-build
copy attempt found that the system Python lacked `llsd`. An environment-only
retry with the existing isolated Python dependency set reused those outputs
and completed the target. It installed nothing and changed no source. Signing
remained disabled; only Apple linker-generated ad-hoc signatures were present,
with no identity or team identifier.

A fresh Linux configuration with every Vulkan switch disabled produced 1,191
compile commands and no Stage 60 target, compile unit, object, symbol, link
record, generated file, or class marker. The unchanged gate and delivery
inputs did not trigger another full disabled package baseline. Final
clang-format 18 checks cover all changed hunks and complete new C++ files.
Diff, privacy, exact 12-file Linux-to-macOS source parity, and two independent
semantic and lifetime reviews are clean. No package, timing, launch, login,
manual signing, Windows work, or production selection ran.

## Deferred work

This stage creates no shader or SPIR-V module, graphics pipeline or cache,
render pass, framebuffer, output image, command buffer, descriptor bind, draw,
submission, sampled observation, or readback. After this stage commits, the
rolling plan must reanalyze whether those operations form one coherent next
stage or need another ownership boundary. Fixed-scene parity, lifecycle
completion, performance evidence, production backend selection, and the
deferred Windows admission proof remain later master-plan work.
