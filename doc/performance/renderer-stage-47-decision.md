# Stage 47 defined-clear Vulkan presentation decision

## Decision

Add one diagnostic transaction that acquires a swapchain image, gives it a
defined normalized RGBA color with a Vulkan transfer clear, presents it, and
retires the presentation work. This is the first Stage 3 operation with defined
swapchain-image contents. It deliberately proves command recording and
presentation, not observed pixel equality.

The transaction remains behind the default-off Vulkan diagnostic graph. It is
not connected to the production renderer, window event loop, or graphics
selector. Windows remains excluded by user direction.

## Capability admission

Swapchain configuration now requires both color-attachment and transfer-
destination surface usage. After selecting the surface format, resolution also
queries that format's physical-device properties and requires
`VK_FORMAT_FEATURE_TRANSFER_DST_BIT` for optimal tiling. The swapchain image
usage is exactly `VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
VK_IMAGE_USAGE_TRANSFER_DST_BIT`.

Missing format-property dispatch, unsupported surface usage, and unsupported
selected-format usage have separate typed failures. All four configuration
commands resolve before the first native query, preserving atomic dispatch
admission. New enum members are appended, and tests guard the previous numeric
values.

## Clear input and parent ownership

`VulkanSwapchainFrameClearColor` owns four floats and defaults to opaque black.
Every component must be finite and within the closed interval from zero to one.
At the parent-operation boundary, invalid input fails before a freshness
callback, dispatch lookup, image acquire, or Vulkan mutation. A platform wrapper
may first sample or refresh its drawable geometry so it can construct the
authenticated request.

The parent transaction copies the color before it crosses any callback boundary.
Caller mutation during a freshness callback therefore cannot alter the recorded
command. The operation then follows the existing exact-instance, window,
configuration, swapchain, image-collection, frame-slot, and drawable-extent
checks. It performs the existing final freshness recheck before execution.

## Dispatch and command recording

The clear path requires `vkCmdClearColorImage` in addition to the established
acquire, command-buffer, barrier, submit, present, fence, and release commands.
The complete dispatch candidate remains private until every command resolves.
A failed clear-command lookup cannot publish a partial table. Resolved commands
are retained for later frames in the same authentic generation.

For the acquired image, the command buffer records exactly:

1. a full color-subresource transition from `VK_IMAGE_LAYOUT_UNDEFINED` to
   `VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL`, from top of pipe to transfer, with
   transfer-write destination access;
2. one `vkCmdClearColorImage` over color aspect, mip level zero, and array layer
   zero;
3. a transition from `VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL` to
   `VK_IMAGE_LAYOUT_PRESENT_SRC_KHR`, from transfer to bottom of pipe, with
   transfer-write source access.

Queue submission waits for image acquisition at the transfer stage. The
existing undefined-to-present transaction keeps its original bottom-of-pipe
barrier and wait-stage behavior.

## Failure and lifecycle contract

Acquire, record, submit, present, retry, cancellation, fence retirement,
out-of-date, suboptimal, surface-loss, device-loss, and indeterminate-state
behavior remains owned by the Stage 45 frame-slot state machine. The clear path
does not introduce a second recovery policy. Once an image has been acquired,
every failure retains the exact obligation required by the established retry or
cancellation route.

Rebuild still requires a resettable frame slot. The native proof completes two
clear presentations before a changed-extent rebuild and two afterward, so the
rebuild crosses only retired presentation state.

## SDL and macOS diagnostic owners

The SDL and macOS diagnostic wrappers sample the current backing-pixel extent,
authenticate their existing platform identity, construct the established parent
operation request, and forward the typed color. The macOS owner refreshes its
existing native-geometry snapshot as part of that authentication. Neither path
changes production `swapBuffers()` behavior.

The macOS path stays on the existing main-thread Cocoa owner. Its native proof
also verifies that the CGL context and global OpenGL manager state remain
unchanged across all four Vulkan presentations and the rebuild.

## Evidence boundary

The native tests prove that Vulkan accepts and completes the exact clear and
presentation command chain with validation enabled. They do not read the image
back, capture the window, or compare pixels. A clear is useful as the smallest
defined-content step before attachment rendering, but it is not visual parity
and it is not evidence about frame time or throughput.

## Build graph and production isolation

The implementation stays in the Vulkan runtime-test archives. SDL ownership
still requires `LL_VULKAN_SDL_WSI`; macOS ownership still requires
`LL_VULKAN_MACOS_WSI`; both remain downstream of the runtime-test option. The
ordinary OpenGL window factory, renderer selector, resize handlers, and
`swapBuffers()` path are unchanged.

With all six renderer experiments disabled, fresh Linux and macOS graphs must
omit every Stage 47 source, object, symbol, string, import, dependency, and
package marker. Existing Chromium Vulkan and SwiftShader payloads are accepted
baseline content and do not select or link the diagnostic renderer.

## Validation evidence

The SHA-256 digest of the newline-terminated `sha256sum` records for the
nineteen Stage 47 source and test files, after sorting the complete records
lexicographically, is
`91c3ac5e0e31d46719a5d9a0c7ebce374e890ab3a3ed3e09eaf950b7f11cc5e9`.
This decision note is excluded because including its own digest would be
self-referential. Linux and macOS reproduce the same frozen manifest.

On Linux, all eleven global-dispatch, physical-device, logical-device,
configuration, swapchain, image-view, frame-slot, parent, requirements,
SDL-owner, and SDL-WSI routes pass. Direct totals are 6, 8, 9, 11, 6, 11, 28,
76, 7, 20, and 1, for 183 tests with zero failures. The enabled viewer and
appearance targets compile with warnings as errors.

The required-validation hidden X11 route uses Mesa lavapipe. It completes two
clear presentations, rebuilds the complete swapchain child chain at a different
positive backing extent, and completes two more. All four operations retire
successfully and produce zero validation messages.

On macOS, the same eleven focused routes pass with direct totals 6, 8, 9, 11,
6, 11, 28, 76, 7, 18, and 1, for 181 tests with zero failures. The universal
viewer and appearance targets, full `ALL_BUILD`, and enabled package target
compile successfully with warnings as errors and developer signing disabled.

The required-validation hidden Cocoa route completes the same four clear
presentations across a changed-extent rebuild through MoltenVK. It produces zero
validation messages and preserves CGL and `gGLManager` state.

The fresh macOS all-six-off graph has 178 targets and 330 warning-as-error
compile settings. The universal viewer and appearance build, full `ALL_BUILD`,
and one-job package target pass. Its 3,428 compiled artifacts contain zero Stage
47 target, build-name, graph-content, object-content, app-name, or app-content
hit. The enabled and disabled packages have the same 215-directory,
6,707-regular-file, zero-symlink shape. Six independently built or generated
files have different bytes, so full package byte equality is not claimed.

All 367 packaged Mach-O files match the accepted Stage 46 relative-path
manifest. Every dependency, undefined-symbol, full-symbol, and strings route
completes without a scan failure. Vulkan or MoltenVK dynamic dependencies,
undefined `vk*` imports, Stage 47 symbols, and Stage 47 strings are absent. All
134 archives likewise have zero scan failure, Vulkan import, or Stage 47 symbol.
The only two Vulkan-named package files are the accepted CEF SwiftShader
payload, with their Stage 46 hashes unchanged. No MoltenVK- or SPIR-V-named
file and no SPIR-V header exists; the three embedded CEF tuples match the Stage
46 baseline. The app is universal. Signing is disabled, and the observed
signature is linker-generated ad hoc with no team identifier.

The final macOS evidence summary has SHA-256
`b07d011e3f6ca9c323afc761702614726659496a14a2ad526e322b37bcc18b13`;
the evidence-record manifest has SHA-256
`23bd321e9f5f65de9c8f9a99aa0356bfeffa0e9f19177d23d86ffb7a3dc622ac`;
the toolchain provenance has SHA-256
`6a33eb2aefb91598bef4032dfbc4ed6e75d3b657898d63d9f513a1471408ca74`.

The fresh Linux all-six-off graph has 1,849 targets and 1,191 unique compile
entries, all with `-Werror`. It retains 1,189 objects: the Stage 46 set plus the
explicitly requested appearance-utility objects and one CMake dummy object.
The viewer, appearance utility, default `all`, and explicit serial package
targets pass in capped systemd scopes. A dry run of the compiled targets reports
no work to do. Stage 47 graph, compile-source, object-name, artifact-name,
object-content, executable-content, defined-symbol, staged-name,
staged-content, and staged-dynamic-symbol hits are all zero. Symbol scanning has
zero tool failure.

Linux staging contains 116 directories including the root, 6,336 regular
files, five symlinks, and 17 ELF files. The archive has 6,457 members including
the root. Fresh extraction and staging have identical path, type, mode,
link-target, and file-byte manifests; each 6,456-record manifest has SHA-256
`7743ecc3dfdd0708e385b4957b7886886e1c1b7066c5aa6b31ce13f1e4b9a404`.
The archive SHA-256 is
`8ad8337142f919faf179fc8034915527571676302050d78c3b5867a6ccec84aa`.
All 17 ELF files have zero undefined `vk*` import, zero Vulkan or MoltenVK
dynamic dependency, and zero scan failure. The Stage 46 and Stage 47 package
path sets are identical; twelve ordinary rebuild or version-metadata records
differ, with no Stage 47 path or marker.

Exactly four Vulkan-named staged entries remain: Chromium's Vulkan loader,
SwiftShader library, and two ICD manifests. Both libraries byte-match fresh
`strip -S` copies of their package-cache inputs, and both manifests match their
raw input. No named or standalone SPIR-V file exists. Embedded magic is exactly
fourteen little-endian occurrences in the same three accepted baseline
libraries: one in GLESv2, ten in SDL, and three in CEF; all three files
byte-match freshly stripped cache inputs. There is no big-endian occurrence.
The raw `vkCmdClearColorImage` spelling is absent from compiled project objects,
archives, and executables. Its accepted dependency-payload occurrences all
byte-match fresh cache inputs.

GCC 15.3 repeats its optimizer-dependent `-Warray-bounds` false positive in the
unrelated `llpaneloutfitedit.cpp` translation unit. Only that generated object
edge adds `-fno-devirtualize-speculatively`; `-Werror` remains active. The Nix
GStreamer layout also splits `gstappsink.h` into the plugins-base development
output, while the upstream media target assumes a merged include tree. Only
that generated media-plugin compiler rule receives the immutable development
include; `-Werror` remains active. A private empty pkg-config compatibility
record supplies current GLib's optional Sysprof metadata during configure.
None of these environment-only exceptions changes source or CMake.

The two pre-existing manifest-output mismatches remain: the copy action expects
`.copy_touched` while the manifest produces `.touched`, and a dry-run archive
edge expects a generic `SecondLife-x86_64` name while the real manifest produces
the channel-specific archive. The actual one-job package target exits
successfully, and staging matches fresh extraction byte for byte.

No viewer was launched. No login, world connection, benchmark, performance
timing, timing retention, readback, or image comparison occurred. No credential,
private network address, private result, or machine-specific path enters source,
build, test, package, or committed evidence.

## Independent review corrections

The first complete Linux run exposed a stale image-view test fixture whose fake
surface did not advertise the new transfer-destination requirement. The fixture
now models both the surface usage and selected-format feature. Every focused
target was then explicitly rebuilt before the final test run, avoiding stale
binary evidence.

Review also requested an explicit assertion for the appended parent operation
error. Tests now protect `OperationFailure == 16` and
`InvalidClearColor == 17`. Independent review found no remaining correctness
defect in capability admission, atomic dispatch publication, command ordering,
legacy behavior, parent copying and freshness, lifecycle recovery, platform
forwarding, or native coverage.

## Explicit deferrals

- Windows build and native execution;
- surface reconstruction and complete device-loss recovery;
- production resize, minimize, suboptimal, and out-of-date event wiring;
- render-pass, attachment, framebuffer, pipeline, shader, and draw ownership;
- descriptors, uploads, texture sampling, and multiple frames in flight;
- readback, capture, fixed-scene parity, and memory-stability soak;
- connection to the production graphics selector or replacement of OpenGL;
- an OpenGL-to-Vulkan compatibility layer;
- benchmark, image-parity, timing, or performance claims.
