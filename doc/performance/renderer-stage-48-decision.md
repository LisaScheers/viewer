# Stage 48 Vulkan swapchain presentation-target ownership decision

## Decision

Add one loader-neutral, move-only presentation-target generation for the exact
live swapchain-image generation. It owns one Vulkan 1.1 classic render pass and
one framebuffer for every resolved image view. This creates the reusable
color-attachment ownership required by a later graphics transaction without
executing a render pass or changing the production OpenGL route.

The generation remains behind the default-off Vulkan diagnostic graph. Windows
remains excluded by user direction.

## Capability admission

Swapchain configuration now requires the selected format to advertise both
`VK_FORMAT_FEATURE_TRANSFER_DST_BIT` and
`VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT` for optimal tiling. It also rejects a
selected image extent above the physical device's maximum framebuffer width or
height. The Stage 47 surface-usage, transfer-destination, format, FIFO, image
count, transform, alpha, and extent rules remain intact.

Unsupported color-attachment format use and oversized framebuffer dimensions
have separate appended failures. Existing enum values remain stable. The
complete configuration still resolves all required commands before its first
native query.

## Dispatch, render-pass, and framebuffer creation

The presentation-target resolver first authenticates the exact logical-device,
configuration, swapchain, and image-view generations. It rejects a null image
view before dispatch lookup, host allocation, or native mutation.

It then resolves `vkGetDeviceProcAddr`, `vkCreateRenderPass`,
`vkDestroyRenderPass`, `vkCreateFramebuffer`, and `vkDestroyFramebuffer` as one
candidate. Missing any command prevents allocation and creation. Calls use only
resolved function pointers, so the implementation remains compatible with
`VK_NO_PROTOTYPES` and has no source-level Vulkan loader edge.

The shared render pass contains exactly one selected-format, single-sample
color attachment. Its load operation is clear, its store operation is store,
its stencil operations are ignored, and its initial, subpass, and final layouts
are `VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL`. One graphics subpass has one
color reference. There are no input, resolve, depth, preserve, multiview,
extension, or dependency records.

Each framebuffer binds one exact image view in image-generation order. Its
width and height equal the admitted image extent, and it has one layer. The
owner exposes the shared render pass, exact framebuffer count, indexed
framebuffer lookup, selected format, and image extent.

## Failure handling and rollback

All dispatch resolves before the first host allocation. The framebuffer handle
array uses nothrow allocation and has a distinct typed allocation failure. The
render-pass and framebuffer output handles are initialized to
`VK_NULL_HANDLE`. A failed native call is classified before its output is read;
a successful call that returns a null handle has its own failure.

If a later framebuffer fails, only earlier successful framebuffers are
destroyed, in reverse order, followed by the render pass. Normal reset uses the
same child-first order and is idempotent. No partial target generation is ever
published.

## Lifetime and parent publication

`createdFor()` authenticates exact parent object identities and retained
instance, surface, physical-device, device, queue, queue-family, queue-index,
swapchain, drawable-extent, format, image-extent, image-count, render-pass, and
framebuffer state.

The instance owner publishes a target only after synchronous, unretained
freshness callbacks, ownership-epoch checks, native-acquisition depth guards,
final parent reauthentication, and final allocation checks. Callback mutation,
ABA replacement, reentrancy, stale requests, and allocation failure cannot
publish an inauthentic child.

The frame slot is the target's younger sibling. A target reset first attempts
to retire the frame slot. A live, non-resettable frame slot preserves the
target and all parents. Successful teardown then destroys the frame slot,
framebuffers, render pass, image views, swapchain, and configuration in that
order.

## Rebuild and platform ownership

The aggregate rebuild order is configuration, swapchain, image views,
presentation target, then frame slot. Target failure is reported through an
appended presentation-target phase and rolls the partial child chain back to
the reusable parent-only state. Zero extent suspends the child chain; a later
positive extent can recreate all five generations.

The lower-level Stage 47 frame-slot API remains independently usable and
unchanged. It does not require a presentation target.

The SDL diagnostic owner exposes explicit target acquisition after a positive
backing-pixel query. The macOS diagnostic owner exposes explicit acquisition
after main-thread identity and native-geometry refresh. Both expose explicit
reset. Ordinary window construction, resize events, `swapBuffers()`, renderer
selection, and production OpenGL behavior are unchanged.

## Evidence boundary

This stage creates, rebuilds, and destroys render passes and framebuffers. It
does not record `vkCmdBeginRenderPass` or `vkCmdEndRenderPass`, bind a graphics
pipeline, draw, submit target use, transition an image for the pass, transition
it back to presentation, or connect the target to the Stage 47 clear path.

Native validation can prove that the object graph is legal. It cannot prove
that the configured clear operation ran, that a pixel changed, that a scene
rendered, or that performance improved.

## Build graph and production isolation

The presentation-target library is reachable only through the default-off
Vulkan runtime-test or tonemap graphs. SDL ownership additionally requires
`LL_VULKAN_SDL_WSI`; macOS ownership additionally requires
`LL_VULKAN_MACOS_WSI`. No production window-creation hook was added. No Windows
source or test changed.

Fresh all-six-off graphs must omit the Stage 48 source, target, object, symbol,
string, import, dynamic dependency, name, and packaged marker. Existing
Chromium Vulkan and SwiftShader payloads are accepted baseline content and do
not select or link the diagnostic renderer.

## Validation evidence

The SHA-256 digest of the newline-terminated `sha256sum` records for the
twenty-one Stage 48 source, build, and test files, after sorting the complete
records lexicographically, is
`2cff6b9a0bb99bec46b6d20fa2b2ed0070bbab989640335dd8eee78d0dbfa005`.
This decision note is excluded because including its own digest would be
self-referential.

On Linux, all twelve global-dispatch, physical-device, logical-device,
configuration, swapchain, image-view, presentation-target, frame-slot, parent,
requirements, SDL-owner, and SDL-WSI routes pass. Direct totals are 6, 8, 9,
11, 6, 11, 7, 28, 80, 7, 21, and 1, for 195 tests with zero failures. The
enabled viewer, appearance utility, full build, and package targets compile
with warnings as errors.

The required-validation hidden X11 route uses Mesa lavapipe. It creates a
non-null render pass and a complete framebuffer set, completes two Stage 47
clear presentations, rebuilds the complete child chain at a different positive
backing extent, verifies a fresh complete target, and completes two more clear
presentations. Explicit target reset succeeds and validation reports zero
messages.

On macOS, the same twelve focused routes pass with direct totals 6, 8, 9,
11, 6, 11, 7, 28, 80, 7, 19, and 1, for 193 tests with zero failures. All
twelve focused binaries have zero Vulkan or MoltenVK dynamic dependency and
zero undefined `vk*` import. The enabled universal viewer, appearance utility,
full build, and explicit serial package target pass with warnings as errors and
developer signing disabled.

The required-validation hidden Cocoa route loads the preserved Vulkan loader,
MoltenVK ICD, and Khronos validation layers. It creates a non-null render pass
and complete framebuffer set, completes two Stage 47 clear presentations,
rebuilds at a different positive backing extent, verifies a fresh complete
target, and completes two more. Explicit target reset succeeds, CGL and the
global OpenGL manager state remain unchanged, and exact validation, VUID,
error, and warning marker counts are zero.

The fresh macOS all-six-off graph has 178 targets and 330 warning-as-error
settings. Its universal viewer, appearance utility, full build, and explicit
serial package target pass. The package manifest retains no local installer
outside the CI runner, so the audited staged app is the delivery payload; no
installer-byte claim is made.

The disabled build contains 3,428 compiled artifacts: 2,560 objects, 134
archives, and 734 dynamic libraries. Generated graph, build-name, compiled-byte,
app-name, and all 6,707 staged-app-file scans have zero Stage 48 hit. The staged
app has 215 directories, 6,707 regular files, and no symlink. The main viewer
and appearance utility are both universal `arm64` and `x86_64`.

All 367 packaged Mach-O files complete `otool`, undefined-symbol, full-symbol,
and strings scans without failure. Vulkan or MoltenVK dependencies, undefined
`vk*` imports, and Stage 48 symbols or strings are absent. All 134 archives
likewise complete undefined and full symbol scans with no failure, Vulkan
import, or Stage 48 symbol. The Mach-O path inventory exactly matches Stage 47.

The 69-file CEF inventory matches its Stage 47 digest. Its only two
Vulkan-named files are the accepted SwiftShader library and ICD manifest, with
their expected hashes. There is no MoltenVK- or SPIR-V-named file. Raw SPIR-V
magic matches the exact three-file dependency baseline, and no standalone
header exists. Enabled and disabled app bundles have only Xcode's automatic ad
hoc linker signatures, with no team identifier.

The final macOS evidence-record manifest contains 75 files and has SHA-256
`9a82881d5ddc531840135cc29f4eff052e68b6f670a8734df6a77c398721396e`.
The source-record manifest independently reproduces the Linux digest above.

The fresh Linux all-six-off graph has 1,766 Ninja targets and 1,191 unique
compile entries, all with `-Werror`. The viewer, appearance utility, default
`all`, and explicit serial package targets pass in capped systemd scopes. Its
1,189 objects, 24 archives, and 1,221 compiled scan inputs have zero Stage 48
source, object, content, or defined-symbol hit. Scanning completes without an
`rg`, `nm`, or binary-inspection failure.

Linux staging contains 116 directories including the root, 6,336 regular
files, five symlinks, and 17 ELF files. Fresh extraction has the same complete
6,456-record path, type, mode, link-target, and file-byte manifest, with
SHA-256
`8e16f1abcce12674b86c42af330bd0525bfe262a1846231554d2b6316dce31bc`.
The Stage 48 archive SHA-256 is
`dc005885a3dc3181c044db66e35b4378a18fb5df56048df51d7c2d431e7be8ac`.
All 17 ELF files have zero undefined `vk*` import, zero Vulkan or MoltenVK
dynamic dependency, and zero scan failure.

The Stage 47 and Stage 48 package path sets are identical. Twelve ordinary
rebuild or version-metadata records differ, so full byte equality is not
claimed. Exactly four Vulkan-named staged entries remain: Chromium's Vulkan
loader, SwiftShader library, and two ICD manifests. Their hashes exactly match
the Stage 47 baseline. No named or standalone SPIR-V file exists. Embedded
magic is exactly fourteen little-endian occurrences in the same three accepted
baseline libraries: one in GLESv2, ten in SDL, and three in CEF. There is no
big-endian occurrence.

GCC 15 repeats its optimizer-dependent `-Warray-bounds` false positive in the
unrelated `llpaneloutfitedit.cpp` translation unit. Only that generated object
edge adds `-fno-devirtualize-speculatively`; `-Werror` remains active. The Nix
GStreamer layout also requires the immutable plugins-base development include
for packaging. These are environment-only build accommodations. Neither
changes source or CMake.

Fresh macOS configure initially selected deployment target 10.13 while the
inherited release flags require 11.0; AppleClang rejected the conflict under
`-Werror`. Reconfiguring the generated graph at 11.0 resolved it. The fresh
graph also selected a system Python without the viewer's `llsd` module, so its
generated Python setting was redirected to the preserved viewer environment.
The only remaining linker diagnostics are the known prebuilt Velopack archive's
newer deployment-target warnings. These host-tool corrections do not change
source, CMake, warnings-as-errors policy, signing policy, or product code.

No viewer was launched. No login, world connection, benchmark, performance
timing, timing retention, readback, capture, image comparison, or developer
signing occurred. No credential, private network address, private result, or
machine-specific path enters source, build, test, package, or committed
evidence.

## Independent review corrections

Review required every Vulkan create output handle to start at null and the fake
physical-device limit to advertise one valid framebuffer layer. It also found
and removed a production SDL acquisition hook, preserving explicit diagnostic
ownership and ordinary window construction.

A test helper no longer retains a reference into a temporary optional value.
The lower-level frame-slot tests remain independent of presentation-target
ownership, and a compound assertion was split so a failure identifies the
exact contract.

Direct test-count auditing found that instance cases 77 through 80 compiled but
were outside the test group's previous cap. The cap is now 80, the exact target
was rebuilt, and all 80 cases execute. Final independent review found no
remaining defect in registration, capability admission, command resolution,
Vulkan structures, ownership, rollback, reset and rebuild order, platform
boundaries, default-off isolation, or Windows exclusion.

## Explicit deferrals

- explicit acquire-to-color and color-to-present synchronization;
- render-pass begin, clear-value recording, end, submission, and presentation;
- graphics pipelines, shaders, viewport, scissor, vertex or generated geometry,
  and draws;
- descriptors, uploads, texture sampling, deferred materials, tonemap, and UI
  composition;
- readback, capture, fixed-scene parity, memory-stability soak, and measured CPU
  or GPU timing;
- surface reconstruction and complete device-loss recovery;
- production resize, minimize, suboptimal, and out-of-date event wiring;
- connection to the production graphics selector or replacement of OpenGL;
- an OpenGL-to-Vulkan compatibility layer;
- Windows build and native execution;
- benchmark, image-parity, timing, or performance claims.
