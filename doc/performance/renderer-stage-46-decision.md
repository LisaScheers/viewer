# Stage 46 same-surface Vulkan swapchain rebuild decision

## Decision

Add one explicit diagnostic transaction that retires and recreates the complete
swapchain-dependent ownership chain for the current surface. A positive drawable
extent produces a fresh configuration, swapchain, image-view collection, and
frame slot. A zero extent retires those children and returns `Suspended` while
the instance, surface, physical-device selection, logical device, and queue stay
live.

This consumes the resize, restore, suboptimal, and out-of-date handoff from
Stage 45. It does not connect the transaction to production window events or
presentation. Surface loss, device loss, and indeterminate frame-slot states
remain broader recovery work. Windows remains excluded by user direction.

## Transaction and error contract

`VulkanSwapchainChainRebuildRequest` carries one sampled optional drawable
extent, the native-window generation, and the existing instance-owner and
window-generation checks. A missing extent means that the platform query
failed. Either zero dimension is a valid suspended-window observation.

Success is typed as `Ready` or `Suspended`. Failure records one of these phases:

- `Preflight`;
- `Retirement`;
- `Configuration`;
- `Swapchain`;
- `Images`;
- `FrameSlot`;
- `FinalFreshness`.

The error also retains the exact nested configuration, swapchain, image, or
frame-slot acquisition failure. Publication and rollback failures stay distinct
from child failures. If reset becomes unsafe during rollback, the result keeps
the blocking frame-slot disposition instead of claiming that the partial chain
was removed.

Preflight authenticates every live parent and the currently published child
chain before invoking callbacks or mutating ownership. Only `Reusable` and
`ResetRequired` frame slots may retire. `DeviceLost` returns
`DeviceRecoveryRequired`; every other disposition returns
`FrameSlotResetRefused`. This includes `PresentationReady` and every acquired,
pending, release, cancellation, or indeterminate obligation, even though the
lower-level reset API can retire a lost device during broader recovery.

The public operation is `noexcept`. A test allocation checkpoint that throws
`std::bad_alloc`, or a real allocation failure, becomes the exact nested child
`AllocationFailure`, followed by the same rollback or refusal rules as any
other child failure.

## Retirement, suspension, and rebuild

An accepted transaction destroys the old frame slot, image views and image
collection, swapchain, and configuration in that child-first order. It does not
wait for device or queue idle and does not treat a submit fence as proof of
presentation completion. Stage 45 completion or cancellation must first leave
the frame slot resettable.

If the sampled width or height is zero, the operation runs final freshness
checks and returns `Suspended`. Repeating the zero-size operation is stable. A
later positive extent rebuilds from the retained parent-only state.

For a positive extent, the transaction resolves a fresh configuration and then
creates the swapchain, image views, and frame slot in parent-to-child order. It
does this even when the extent is unchanged because surface capabilities may
have changed after a suboptimal or out-of-date result. Swapchain creation uses
`oldSwapchain = VK_NULL_HANDLE`; the old generation is already gone and no
overlapping ownership is introduced.

Every new child remains private until its own acquisition has passed allocation,
native-call, owner, window, provenance, and publication checks. If rollback
remains safe, a later failure removes all newly published swapchain children and
leaves the same stable parent-only retry state. If a callback publishes a
non-resettable `Pending` or `DeviceLost` frame slot, that complete chain stays
intact and the result reports the exact refusal. Final success requires a
complete and mutually authentic configuration, swapchain, image-view
collection, and frame slot.

## Reentrancy and native-candidate lifetime

Every ownership transition increments one private monotonic epoch. Each of the
seven native acquisition paths samples that epoch before its first callback and
checks it across the full acquisition. This detects callback-driven replacement
even when an allocator reuses the same address or a driver reuses the same
handle value.

A private depth-based `NativeAcquisitionGuard` starts before a native child can
be resolved or created and remains active until an unpublished candidate has
been destroyed. All direct and transitive reset operations refuse during that
interval, including an otherwise idempotent reset. A reentrant rebuild returns
`NativeAcquisitionInProgress` in `Preflight` before callbacks, mutation, or
native work.

Move construction during a guarded acquisition leaves the source intact and
creates an empty destination. This is deliberate: an unpublished native child
may still need the source's exact destruction parent. After the guard releases,
ordinary move and reset behavior resumes.

If a callback publishes the exact requested target, the matching
`AlreadyOwned` child error wins. Other callback-driven ownership changes stop
the transaction before later callbacks, native work, or stale parent access.
These rules cover pointer ABA, handle ABA, parent reset, nested acquisition,
allocation checkpoints, and final-callback mutation.

## SDL and macOS diagnostic owners

The SDL wrapper samples backing pixels once. A failed query or negative result
is invalid; a zero result suspends; a positive result enters the core
transaction. The native X11 proof synchronizes the diagnostic resize with
`SDL_SyncWindow`, then calls the core operation directly. Production
`LLWindowSDL` forwarding and event behavior remain unchanged.

The macOS wrapper runs through the existing main-thread Cocoa owner and checks
the exact token, window, view, Metal layer, and geometry. Its rebuild-only
observation distinguishes a stale native identity from a valid zero-size
drawable. Existing acquisition methods still use the positive-only
`refreshNativeGeometry()` contract and do not reinterpret zero as suspension.
The diagnostic-only resize bridge changes the hidden layer's backing extent for
native proof; that proof separately asserts that CGL and `gGL` remain unchanged.

The C bridge appends `DRAWABLE_UNAVAILABLE` after the existing status values, so
the numeric values 0 through 9 retain their prior meaning. The optional native
resize callback is appended to the C++ operations aggregate and is not required
for ordinary owner validity. Existing member order and positional source
initializers remain compatible, but the larger aggregate is not binary
compatible with a precompiled caller. No production macOS resize path calls it.

## Build graph and production isolation

The implementation stays in the default-off Vulkan diagnostic archives. SDL
ownership still requires `LL_VULKAN_SDL_WSI`; macOS ownership still requires
`LL_VULKAN_MACOS_WSI`; both depend on the runtime test graph. The ordinary
OpenGL window factory, renderer selector, resize handlers, and `swapBuffers()`
path are unchanged.

With all six renderer experiments disabled, fresh Linux and macOS graphs omit
every Stage 46 target, source, object, symbol, string, import, dependency, and
package marker. The existing requirements probe and Chromium Vulkan payload
remain accepted baseline content and do not make the viewer select or link
Vulkan.

## Validation evidence

The SHA-256 digest of the newline-terminated `sha256sum` records for the thirteen
Stage 46 source and test files, after sorting the complete records
lexicographically, is
`d2a44d2c9b2d0ca00a8d6e9ab1c2573c416f6b895c4831356d80b875405433dd`.
This decision note is excluded because including its own digest would be
self-referential. Linux, Vega, and the final local recheck reproduce the same
manifest.

On Linux, all eleven global-dispatch, physical-device, logical-device,
configuration, swapchain, image-view, frame-slot, parent, requirements,
SDL-owner, and SDL-WSI routes pass. Direct totals are 6, 8, 9, 11, 6, 11, 25,
75, 7, 19, and 1, for 178 tests with zero failures. The enabled viewer and
appearance targets compile with warnings as errors. The relevant archives and
focused executables have zero direct undefined Vulkan imports, and the
executables have no Vulkan or MoltenVK dynamic dependency.

The required-validation hidden X11 route uses Mesa lavapipe and SDL 3.2.24. It
requests a resize 32 by 24 pixels larger than the original, synchronizes the
window, and observes a different positive backing extent. It rebuilds a
complete fresh child chain while preserving all older parents and completes two
Stage 45 presentation transactions. It reports zero validation messages and
tears down child first.

The fresh Linux all-six-off graph has 1,849 targets and 1,191 unique compile
entries, all with `-Werror`. It retains 1,173 objects. Stage 46 graph, compile
source, object name, object content, defined symbol, staged name, and staged
content hits are zero.

The staged Linux package contains 116 directories, 6,336 regular files, five
symlinks, and 17 ELF files. Its archive contains 6,457 entries including the
root. Fresh extraction and staging have identical path, type, mode, link-target,
and file-byte manifests. Their manifest SHA-256 is
`a0a023ab6332d805880b7fcb71c45dbf8c1152d1d792dcafa1ccfed392e43d4f`.
The archive SHA-256 is
`98f3ec255d29ffb22b994b72c584500ea7a1fe759065fa148d55aefc72be6805`.

All 17 ELF files have zero undefined `vk*` import and zero Vulkan or MoltenVK
dynamic dependency. The four Vulkan-named package entries are the accepted
Chromium loader, SwiftShader library, and two ICD manifests. The two libraries
byte-match fresh `strip -S` copies of their package-cache inputs; both JSON files
match their raw cache input. No named or standalone SPIR-V file exists.
Embedded magic is confined to `libGLESv2.so` with one hit and SHA-256
`a4f0ceca8af9c4277d5843416fe4999efabd407b74686ef03f54db776a85ea16`,
SDL with ten hits and SHA-256
`55886a5cfc7c0a4a101bf812d69b19db1c81b3ea3bf8509d3b3859d8806e42b5`,
and CEF with three hits and SHA-256
`ea2826e2f667739791715a1c7b8075d0635e841ca36e3ac20e1f32668c6887f5`.
Those staged values match fresh manifest-style stripped accepted inputs.

GCC 15.3 repeats its optimizer-dependent `-Warray-bounds` false positive in the
unrelated `llpaneloutfitedit.cpp` translation unit. Ninja compiled only that
object with `-fno-devirtualize-speculatively`; `-Werror` remained active and no
warning was disabled. The first package attempt also exposed a Nix-only split
include layout: `gstappsink.h` lives in the GStreamer plugins-base development
output while the upstream media target assumes a merged include tree. Ninja
compiled only that media-plugin object with the missing development include;
the ordinary package run then passed. Neither exception changed source or the
build system.

Two pre-existing manifest-output mismatches keep `ninja -n llpackage` from
becoming clean. The copy action expects a `.copy_touched` marker while the
manifest creates `.touched`. The archive action expects a generic
`SecondLife-x86_64` archive while the channel-specific manifest creates
`Second_Life_Test`. The compiled targets are clean, the serial package target
exits successfully, and staging matches fresh extraction byte for byte.

On macOS, the enabled Xcode graph has 197 targets. The universal viewer and
appearance build, full `ALL_BUILD`, and one-job package target pass with
warnings as errors and developer signing disabled. Twelve required products are
universal `arm64 x86_64`; the eleven focused executables are intentionally
native arm64. All focused routes pass with direct totals 6, 8, 9, 11, 6, 11,
25, 75, 7, 17, and 1, for 176 tests with zero failures.

The required-validation hidden Cocoa route resizes its Metal layer to 1440 by
810 backing pixels, rebuilds the exact child chain through MoltenVK, preserves
the surface, physical device, logical device, queue, CGL context, and `gGL`, and
completes two presentations. It reports zero validation messages. Enabled
archives and executables have zero direct Vulkan import or Vulkan and MoltenVK
dynamic dependency.

The fresh macOS all-six-off graph has 178 targets. The cold viewer and
appearance build emits 2,359 `CompileC` actions; `ALL_BUILD` adds 199, for 2,558
actions. Both passes and the package target succeed with warnings as errors.
An exhaustive scan of 3,428 object, archive, and dynamic-library files finds no
Stage 46 marker.

The staged app exactly matches the immutable Stage 43 and enabled path sets: 215
directories, 6,707 regular files, and no symlinks. The ordered file-path digest
is `c5e5554a926c203c5bb7ae30346ee62b8f2e499f1512bad889dd3d0e8e68105f`;
the directory-path digest is
`76e82bfb4b13137c0c85ad53b29c27fe76395aa21ec6d0c7cb7acb1408ff2d44`.
Stage 46 build-graph, artifact-name, object-content, app-name, app-content,
symbol, and string hits are zero. The viewer and appearance archive are
universal. No DMG or PKG is retained.

All 367 Mach-O files match the accepted path manifest with SHA-256
`553ffd4a8fc1fc9d781c2eaeef1f5db208f3fb4c1599b82f24481ce494a8fdc4`.
Every file passes `otool`, undefined-symbol, full-symbol, and strings inspection
with no tool failure or stderr. Vulkan or MoltenVK dynamic dependencies,
undefined `vk*` imports, Stage 46 symbols, and Stage 46 strings are all absent.

The only two Vulkan-named app files are Chromium's existing
`libvk_swiftshader.dylib` and `vk_swiftshader_icd.json`. Their respective
SHA-256 values are
`2d350884001d55c02dfd108c5d0bb47c32c3d13b4e64a7981005dbee4be2d6ad`
and `d717d915e31e7c27948b80b36ab34e2d897888114c5c7d0af835f93eb53e58f5`,
matching the accepted baseline. The 69-file CEF manifest digest is
`32839675c225347e1dfb80e85509678fac979c6c73adbb75d5a4b8731253be19`.
No named MoltenVK or SPIR-V file and no SPIR-V header is present. The three
embedded SPIR-V tuples match Stage 43 exactly: two little-endian hits in CEF,
two in `libGLESv2.dylib`, and one big-endian hit in the Greek locale pack.

The macOS host uses macOS 26.6.2, Xcode 26.6, macOS SDK 26.5, Apple clang
21.0.0, CMake 4.1.2, Python 3.9.6, Vulkan headers and validation API 1.4.357,
and MoltenVK 1.4.1. The preserved provenance record has SHA-256
`7fe1367c1310be5af1bce9e5f3474423e9e03fba6fc630150620cff23035d10d`;
the final compact evidence summary has SHA-256
`652f93dc3a56988926be44955deaf6e1c0cb61b1a42d355658417c4cf1f69c30`.
Developer signing is disabled. The app has only its linker-generated ad hoc
signature and no team identifier.

Linux uses GCC 15.3.0, CMake 4.3.4, Ninja 1.13.2, Vulkan headers, loader, and
validation layers 1.4.357, Mesa lavapipe 26.2.1, and SDL 3.2.24.

No viewer was launched. No login, world connection, benchmark, performance
timing, or timing retention occurred on either platform. No credential or
private result entered source, build, test, package, or committed evidence.

## Independent review corrections

Adversarial review found and corrected publication, allocation, provenance,
and reentrancy gaps before final validation. Pending and device-lost rollback
now retain complete typed state. Callback publication cannot overwrite an
existing configuration, swapchain, image collection, or frame slot. Every
allocation checkpoint and final freshness boundary revalidates exact parents.

The first freshness design resampled its ownership epoch between native
boundaries, which could miss a replace-and-restore ABA sequence. The final
design samples one acquisition-wide epoch. Review then found that a callback
could reset the native destruction parent while an unpublished child was live;
the depth guard now protects all seven paths. A guarded reentrant rebuild gets
its own `NativeAcquisitionInProgress` result, and guarded move construction
cannot steal the source.

Review also preserved existing macOS callback member order and positional source
compatibility, and retained the numeric C status ABI. It synchronized the SDL
native resize, fixed refusal precedence, and kept the native SDL test on the
direct diagnostic core instead of changing production forwarding. Independent
core, lifetime, rollback, SDL, macOS, failure, evidence, scope, privacy, ABI,
and exact-generation reviews report no remaining defect.

## Explicit deferrals

- Windows build and native execution;
- surface reconstruction and complete device-loss recovery;
- reuse through a nonnull `oldSwapchain`;
- production resize, minimize, suboptimal, and out-of-date event wiring;
- defined image contents, clear operations, attachments, rendering, and draws;
- shaders, pipelines, descriptors, uploads, and texture sampling;
- multiple frames in flight;
- readback, capture, fixed-scene parity, and memory-stability soak;
- connection to the production graphics selector or replacement of OpenGL;
- an OpenGL-to-Vulkan compatibility layer;
- benchmark, image-parity, timing, or performance claims.
