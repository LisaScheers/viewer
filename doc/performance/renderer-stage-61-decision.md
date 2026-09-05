# Stage 61 Vulkan sampled presentation pipeline decision

## Decision

Own one graphics pipeline that joins the fixed Stage 60 sampled binding to the
current swapchain presentation target. The generation retains the exact
binding and target, borrows the binding's pipeline layout, and owns one
`VkPipeline`. Its vertex and fragment shader modules are transient inputs that
are destroyed after pipeline creation.

This pipeline-only boundary is independently useful. Real driver acceptance
proves that the shader interfaces, descriptor-compatible layout, vertex
declaration, fixed graphics state, and presentation render pass agree. The
stage does not bind the pipeline or descriptor set, retain a vertex buffer in
a frame slot, record a sampled command, draw, submit, present sampled output,
or observe sampled pixels.

The owner remains inside the default-off Vulkan diagnostic graph. It does not
select Vulkan for the production viewer, alter the OpenGL renderer, restart
benchmarking, admit Windows, launch or log into the viewer, package a release,
or require developer signing.

## Canonical pipeline identity

The public description contains only the neutral texture-upload
`PipelineHandle {1, 1}`. Shader and fixed-state identity remain internal
constants. This connects the native owner to the immutable renderer packet
without adding an allocating shader-name field to the `noexcept` acquisition
path.

The published generation freezes both parent branches:

* the exact instance, surface, physical device, logical device, queue, queue
  family, and queue index;
* the exact drawable extent, swapchain, image format and extent, image count,
  presentation render pass, and target generation;
* the exact resident texture destination, view, shader-read layout, revision
  23, and nonzero source-content identity; and
* the exact Stage 60 binding generation, sampler, descriptor-set layout,
  borrowed pipeline layout, descriptor pool, descriptor set, set index zero,
  and binding zero.

Stable identities help diagnostics and ABA detection but never replace pointer,
epoch, description, native-handle, residency, and parent checks. The pipeline
retains neither the texture upload source nor transfer generation. It also has
no dependency on the vertex upload destination, frame slot, or swapchain
readback owner until a later command-execution stage.

## Verified shader artifacts

The pipeline reuses these existing sources unchanged:

* `vulkan/shaders/textureupload.vert.glsl`;
* `vulkan/shaders/textureupload.frag.glsl`;
* `app_settings/shaders/class1/interface/copyV.glsl`; and
* `app_settings/shaders/class1/interface/copyF.glsl`.

The embedded modules are the accepted deterministic Vulkan 1.1 artifacts from
the existing texture-upload shader build:

* vertex: 1,112 bytes, SHA-256
  `139f3d06e998cdd95ad6ae751dd97cf7ecaeb9c210efca379a7b1ee73270789c`;
* fragment: 628 bytes, SHA-256
  `2d07ec80932a25934493be1d4f8bdfb3ca3d2bac0cf3ffa9cbb7d7520bdaafb1`.

An offline verifier reproduces the established glslang invocation, validates
both modules for Vulkan 1.1, reflects and disassembles them, and compares exact
bytes with the embedded arrays. It requires one `main` entry point per stage,
vertex location-zero `vec3` to location-zero `vec2`, fragment location-zero
`vec2` to location-zero `vec4`, exactly one `sampler2D` at set zero binding
zero, no other descriptor, no push constant, no vertex-index builtin, and an
implicit-LOD image sample. Ordinary runtime builds do not need shader tools or
load generated SPIR-V from disk.

## Fixed graphics state

The vertex declaration has one per-vertex binding at binding zero with stride
16 and one position attribute at location zero, binding zero, offset zero,
using `VK_FORMAT_R32G32B32_SFLOAT`. Input assembly is triangle list with
primitive restart disabled.

The pipeline declares one dynamic viewport and one dynamic scissor. It uses
fill rasterization, no culling, counter-clockwise front face, no depth clamp,
no rasterizer discard, no depth bias, and line width one. Multisampling uses
one sample with sample shading disabled. There is no depth-stencil state.
Blending and logic operations are disabled and all RGBA components are
writable.

Pipeline creation has no cache, derivative, extension chain, specialization
constant, tessellation state, push constant, or optional feature. Its layout
is exactly the Stage 60 borrowed layout. Its render pass is exactly the
current presentation target's render pass at subpass zero.

## Transaction and rollback

The complete command set resolves before native mutation:

1. `vkGetDeviceProcAddr`;
2. `vkCreateShaderModule` and `vkDestroyShaderModule`; and
3. `vkCreateGraphicsPipelines` and `vkDestroyPipeline`.

Resolution reauthenticates every parent after each resolver and native
callback. All native outputs start null. A failed single-object create never
donates poisoned output bits. A successful call returning null is a distinct
failure. Graphics-pipeline creation may return a non-null partial pipeline
with an aggregate failure; that pipeline is an owned rollback obligation.
Equal native handle bits from different successful occurrences remain
separate obligations.

Rollback destroys a returned pipeline first, then the fragment and vertex
shader modules. Success destroys the transient fragment and vertex modules
before publication. The persistent owner retains only the pipeline and its
borrowed parent facts. Reset first detaches all ownership and provenance, then
destroys the pipeline once. Move construction transfers complete ownership,
and reset is idempotent.

## Aggregate lifetime and rebuild

`VulkanInstanceGeneration` owns an optional sampled-pipeline slot with its own
epoch and teardown guard. Acquisition snapshots and rechecks the aggregate
ownership epoch, target epoch, binding epoch, destination epoch, exact parent
pointers, request descriptions, native handles, and mutable residency fields.
A nested exact acquisition may publish the winner. A stale outer candidate
rolls back. Same-looking pointer replacement is rejected by pointer plus epoch
checks.

Direct sampled-pipeline reset preserves the presentation target, Stage 60
binding and destination, base green presentation pipeline, frame slot,
readback, logical device, and swapchain. Source and terminal transfer reset
also preserve it. Binding or destination reset retires the sampled pipeline
first.

Presentation-target teardown preflights any frame-slot obligation, moves the
target slot out of the aggregate, and advances its epoch before retiring the
sampled and base pipelines. It destroys the detached target last. Every
pipeline or target destroy callback therefore sees no aggregate target and
cannot publish a new sampled pipeline against a parent being destroyed.
Logical-device and full teardown retain the established rule that the entire
swapchain chain retires before the Stage 60 binding, destination, and device.

The sampled pipeline is optional during swapchain rebuild. Rebuild freezes the
exact absence or presence and full native identity of the Stage 60 binding and
destination branch. It retires the sampled pipeline before the old target,
preserves that binding branch, and constructs only the established mandatory
replacement chain. Successful positive rebuild and zero-extent suspension
leave the sampled-pipeline slot absent. The caller explicitly reacquires it
against a later live target.

Every child and final freshness check, chain-authenticity check, no-child
check, and rollback includes the sampled slot, its epoch, and the frozen
binding branch. Callback-time pipeline publication or binding addition,
removal, mutation, and same-looking replacement changes an epoch or retained
fact, fails the active step, and is removed by rollback. This policy requires
no new rebuild phase, child-error variant, persistent intent flag, or
rebuild-wide lock.

## Validation boundary

Focused owner tests inspect every shader create input and graphics-pipeline
structure, exact dispatch coverage, invalid parent and description paths,
missing commands, native failures, null successes, poisoned outputs, partial
pipeline returns, equal handle occurrences, callback mutation, rollback,
move, idempotent reset, and detached callback state.

Aggregate tests cover allocation failure, nested publication, stale outer
rollback, same-looking ABA replacement, target and binding branch mutation,
destroy-callback reentry, source and terminal-transfer independence, direct
reset preservation, frame-slot rebuild refusal, changed-extent retirement,
frozen binding preservation, explicit reacquisition, and final teardown
ordering.

SDL and Cocoa fake-owner tests exercise the same adapter-visible lifetime.
Native X11/lavapipe and Cocoa/MoltenVK routes complete the real texture upload
and Stage 60 binding, create the pipeline, preserve it across source and
terminal-transfer reset, retire it for rebuild, explicitly reacquire it, and
reset it before its parents. Required Khronos validation must report no
diagnostic. These native paths do not bind or execute the sampled pipeline.

## Closure evidence

The focused Linux build was current. Its seven non-native runners passed all
235 cases: texture destination, source, transfer, sampled binding, sampled
pipeline, aggregate instance, and SDL fake ownership. The native SDL/X11
smoke passed its single case on lavapipe with required Khronos validation and
zero recorded validation messages. It created the sampled pipeline, rebuilt
the swapchain, explicitly reacquired the pipeline, and tore the graph down in
dependency order.

The offline shader verifier regenerated both Vulkan 1.1 modules and matched
the embedded bytes, declared interfaces, operations, sizes, and SHA-256
digests exactly.

Fresh focused Release builds and tests also passed on macOS for both arm64 and
x86_64. Each architecture passed 233 cases, including the native
Cocoa/MoltenVK smoke. Required validation remained enabled with zero recorded
messages through sampled-pipeline acquisition, swapchain rebuild, explicit
reacquisition, and teardown. The macOS work did not package or sign a viewer.

## Deferred work

The current frame-slot implementation has no descriptor-set bind command and
does not retain the sampled pipeline, binding, texture destination, and vertex
destination as one submitted lease. Its readback classifies a whole
presentation image only as green, red, or unexpected. That cannot prove the
texture contract's exact 4 by 2 sampled output.

Stage 61 is the last isolated pipeline or descriptor milestone. The next
committable milestone is one executable Linux viewer vertical slice: an
explicit nonpersistent developer selection, an application-owned Vulkan
runtime, a sampled draw recorded and submitted through the existing frame
slot, and resize, zero-extent suspension, restore, and orderly shutdown.
`secondlife-bin` must own that loop directly and must bypass `LLViewerWindow`,
the OpenGL shader manager, the GL display path, and buffer swapping. Focused
tests cover only the new selection, runtime lifecycle, and sampled-command
seams; one live viewer smoke supplies the end-to-end proof.

Feature parity is not part of that milestone. Once the visible sampled frame
is stable, the first blocked normal viewer pass will select the next subsystem
to migrate. Current startup ownership points to the login and startup UI
renderer, where native-window creation is still coupled to OpenGL fonts,
images, vertex buffers, shaders, and the legacy display pipeline.
