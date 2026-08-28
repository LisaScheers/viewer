# Stage 35 isolated macOS Vulkan surface producer

## Decision

Add one default-off, test-only macOS producer that owns a hidden programmatic
Cocoa window, a private view backed by `CAMetalLayer`, an explicit Vulkan
loader lease, the existing immutable window requirements, and the existing
instance generation with its private surface child.

Do not route the production viewer through this owner. The existing
`LLWindowMacOSX`, `LLOpenGLView`, XIB, public graphics-API selector, viewer
callers, packaging, and install graph remain unchanged. The legacy Cocoa
window still creates and assumes CGL state throughout construction, input,
sizing, swap, shared-context, and teardown behavior. Splitting those concerns
is a later production-integration decision, not part of the first real Metal
surface proof.

This stage adds no physical-device query, presentation query, logical device,
queue, swapchain, image, command buffer, frame, setting, command-line route,
login, benchmark, timing, or performance claim.

## Test-only build boundary

`LL_VULKAN_MACOS_WSI` is off by default. Configuration rejects it unless the
host is macOS and both tests and the loader-neutral Vulkan runtime contracts
are enabled. It does not require the tonemap experiment, shader tools, a
configure-time Vulkan loader, a configured ICD, or packaging changes.

The option creates a standalone `llwindowmacosxvulkan` static target. That
target depends one way on the existing `llwindow` and
`llrendervulkaninstance` libraries and privately links AppKit, QuartzCore, and
the platform dynamic-loader facility. Nothing adds this target back to
`llwindow` or the viewer. Finding QuartzCore also occurs only inside the
enabled branch.

The ordinary C++ owner compiles with both `VK_NO_PROTOTYPES` and
`VK_USE_PLATFORM_METAL_EXT`; tests use only the definitions needed by their
C++ Vulkan headers. The Objective-C++ bridge includes its opaque C bridge
header but no Vulkan header or Linden C++ header. This avoids the documented
Objective-C `BOOL` conflict in the legacy bridge while keeping every Vulkan
entry point behind the explicitly resolved dispatch.

## Opaque Cocoa and Metal ownership

The Objective-C++ bridge creates a plain borderless `NSWindow`, not the
viewer-specific `LLNSWindow`. The window is hidden and has
`releasedWhenClosed` disabled. It owns one private `NSView` subclass whose
exact layer is one retained `CAMetalLayer`.

All AppKit operations require the main thread. The C++ operation table checks
that affinity before any acquisition side effect and before refresh or
teardown. The bridge does not dispatch a caller synchronously to the main
queue, which avoids a hidden deadlock. An explicit off-main reset returns
false while preserving the complete ownership chain for a later main-thread
retry. Destroying a still-owning value off the main thread is a contract
violation and fails fast instead of leaking Cocoa objects and closing their
loader underneath them. The first valid acquisition may create the
process-global `NSApplication`; the owner does not claim that global action is
reversible.

The view updates the layer's `contentsScale` and `drawableSize` when it joins a
window, when backing properties change, and when its frame changes. Creation
attaches the view first, converts the requested backing-pixel rectangle to
content points, applies that logical content size, then requires the resulting
drawable size to equal the requested pixel extent. The contract therefore
does not assume a 2x display.

The bridge returns an opaque token plus borrowed window, view, and layer
identities to the C++ operation seam. The public owner exposes only whether a
native generation exists and scalar backing geometry. Refresh occurs on a
copy and publishes new scalar geometry only if all four identities remain
exact and the scale and drawable extent remain valid.

Bridge teardown detaches the content view and layer, releases layer and view,
then closes and releases the ordinary window. No XIB close callback, viewer
quit callback, input notification, or OpenGL view participates.

## Explicit loader lease

The production operation opens an explicitly requested loader with
`RTLD_NOW | RTLD_LOCAL`. An empty request uses only the macOS Vulkan ABI name
`libvulkan.1.dylib`. It never uses `RTLD_DEFAULT`, a build-machine SDK path,
direct `Vulkan::Vulkan` linkage, or direct MoltenVK linkage.

The exact `vkGetInstanceProcAddr` symbol comes from that handle and is borrowed
by the immutable requirements, global dispatch, instance, and surface
transactions. The loader handle remains live until all those children and
the Cocoa objects have been released. Closing this owned lease is not used to
claim that another process lease or driver image was physically unloaded.

Native validation supplies the exact loader path, MoltenVK ICD manifest, and
validation-layer discovery environment outside the repository and executable.
No private or machine-specific location is compiled into an artifact.

## Requirements and instance policy

The producer deep-copies exactly two required instance extensions in this
order:

1. `VK_KHR_surface`;
2. `VK_EXT_metal_surface`.

One nonzero native-window generation authenticates the retained Cocoa
identities, requirements, instance parent, and surface child. Instance
acquisition refreshes the exact native identities first, requests Vulkan 1.1,
requires Khronos validation, and enables portability enumeration when the
loader advertises it. The existing instance contract owns and validates all
global commands, extension and layer policy, validation callback state,
debug messenger, rollback, and moves.

This is instance-level portability only. The device-level
`VK_KHR_portability_subset` policy remains deferred with all device work.

## Metal surface transaction

Surface acquisition refreshes the native identities again and binds a
stack-scoped operation context to:

- the exact macOS owner;
- the exact instance-generation object currently stored in that owner;
- the exact retained resolver;
- the exact private `CAMetalLayer`; and
- the exact nonzero native-window generation.

The default operation resolves `vkCreateMetalSurfaceEXT` for the exact parent
instance. It constructs `VkMetalSurfaceCreateInfoEXT` with only the exact
private layer and calls through that resolved pointer with null allocation
callbacks. A missing resolver, layer, instance, output, or creation command is
a platform failure. A called Vulkan command retains its exact `VkResult`.

The existing parent transaction resolves `vkDestroySurfaceKHR` before calling
the platform creator, rejects poisoned failed output and null successful
output, repeats owner and native-generation freshness checks around creation,
and publishes only a valid non-null private child. A failed surface attempt
leaves the instance, validation state, requirements, native objects, and
loader lease live so the exact owner may retry.

## Rollback and teardown order

Initial acquisition earns resources in this order:

1. local Vulkan loader lease;
2. exact resolver;
3. hidden Cocoa window, view, and Metal layer;
4. immutable ordered requirements.

Zero dimensions, a zero generation, and an off-main caller fail before the
loader opens or `NSApplication` can exist. Every later failure closes only
earned resources in reverse. Native bridge failures clean their own partial
AppKit storage. Invalid or poisoned native success is passed back to the
injected native destroy operation before the loader lease closes. Requirements
allocation failure destroys the complete native token and then closes the
loader.

A complete successful owner resets in this order:

1. Vulkan surface;
2. debug-utils messenger;
3. Vulkan instance;
4. authenticated global dispatch and immutable requirements;
5. CAMetalLayer, private view, and hidden window;
6. explicit loader lease.

Explicit surface reset performs only step one. It leaves validation, the
instance parent, requirements, native identities, geometry refresh, and loader
lease live. Move construction transfers the complete ownership chain. Move
assignment first tears down the destination chain, then transfers the source.
Moved-from owners expose no loader, native window, requirements, instance, or
surface.

## Focused evidence boundary

The injected C++ suite calls no AppKit API. Its operation tables cover invalid
requests, operations, thread affinity, loader and resolver failures, each typed native
failure, invalid and poisoned native success, requested-pixel geometry,
zero-generation preflight, requirements allocation rollback, exact extension order,
instance and surface acquisition, exact resolver and layer forwarding,
platform and Vulkan surface failures, poisoned output, null success, retry,
duplicate ownership, forced surface allocation failure, refresh identity and
geometry poisoning, explicit reset, reacquisition, moves, move assignment,
idempotent reset, and complete reverse teardown.

The separately opted-in native route runs on the AppKit main thread with an
explicit loader and externally selected MoltenVK ICD and validation layer. It
requires one hidden 1280 by 720 backing-pixel native generation, positive
backing scale, the exact ordered requirements, a validation-enabled portable
Vulkan 1.1 instance, and one real non-null Metal surface. It explicitly resets
the surface while the parent, validation, native objects, and loader remain
live, requires zero validation messages, and then completes owned teardown.
It also requires that no CGL context becomes current and that `gGLManager`
does not change.

Universal library slices are a build property. The native integration
executable follows existing project policy by using the host architecture and
valid ad hoc signing. The stage does not claim universal execution.

## Disabled isolation boundary

With all five renderer experiment options disabled, the standalone macOS
producer, its Objective-C++ object, QuartzCore edge, and both tests are absent.
The option-independent graphics-API and requirements routes remain. A fresh
universal ReleaseOS viewer package must remain free of direct Vulkan or
MoltenVK dependencies, direct Vulkan entry imports, and Vulkan, MoltenVK, or
SPIR-V payload.

Linux keeps its existing SDL producer and reusable parent contracts. Enabling
the macOS option on Linux or Windows fails at configuration. No Win32 or
production Cocoa source changes in this stage.

## Validation results

### macOS enabled path

The option-on universal ReleaseOS configuration builds the helper and both
focused tests with warnings as errors. The static helper contains x86_64 and
arm64 slices. Both test executables follow existing project policy by using
the host arm64 architecture, have valid ad hoc signatures, and have no direct
Vulkan or MoltenVK dependency or direct Vulkan entry-point import.

The injected ownership suite passes all 12 cases. It calls no AppKit API and
now proves that an off-main reset preserves the exact surface, debug
messenger, instance, requirements, native identities, and loader before a
successful main-thread retry. The native route passes its default opt-out
case and a separately opted-in real MoltenVK case. The real case creates a
hidden 1280 by 720 backing-pixel generation, enables required validation and
available portability enumeration, creates one non-null Metal surface,
observes zero validation messages, explicitly resets the surface while its
parent remains live, and completes clean teardown without making a CGL
context current or changing the GL manager.

Adversarial review first found that an off-main reset could clear C++
ownership after the bridge refused Cocoa destruction, and that a zero
generation could create `NSApplication` before rejection. Both defects were
fixed before final validation. Final independent reviews found no ownership,
ordering, build-boundary, production-isolation, or privacy defect.

### macOS disabled isolation

A separate fresh configuration with all five experiment switches disabled
builds the universal ReleaseOS viewer and completes the application package
target with warnings as errors. All 11 option-independent renderer routes and
both remaining window routes pass. The helper, its two tests, and all matching
objects and target names are absent. The universal viewer executable has no
direct Vulkan, MoltenVK, or QuartzCore dependency.

All 367 packaged Mach-O files were inspected. Four Chromium helper paths
required temporary safe aliases for the platform inspection tool. The final
scan found zero Vulkan or MoltenVK dependencies and zero direct Vulkan entry
imports. Across 6,707 regular package files, no Vulkan, MoltenVK, or SPIR-V
filename and no SPIR-V magic payload exists.

The fresh graph initially lacked explicit discovery of the already
provisioned Autobuild executable and Python environment. Supplying those
existing tools let the same graph finish without a source change. These were
disposable host-environment corrections, not Stage 35 failures.
Both exact macOS validation roots, copied dependency state, build output,
package output, and temporary tool environment were removed afterward, and
the transient application registration was undone.

### Linux boundaries

The existing warnings-as-errors Linux build keeps the benchmark, runtime,
tonemap, and SDL WSI switches enabled while the new macOS switch remains off.
Six focused global-dispatch, instance, window-selection, requirements,
injected SDL, and native SDL routes pass. The 12 reflection, eight artifact
delivery, and 57 harness unit tests also pass. The harness tests exercise
Python logic only; no benchmark or timing path runs. No macOS helper target or
artifact appears in that graph.

A fresh all-five-off Linux configuration contains none of the optional
renderer Vulkan, SDL Vulkan, or macOS Vulkan targets. Its warnings-as-errors
build completes all 218 steps, and both option-independent window routes pass.
A separate disposable configure that requests the macOS option on Linux fails
with the intended platform diagnostic. All exact disposable Linux output was
removed after validation.

### Other platform boundary

No Windows compiler is available. The Win32 production window and hardware
sources remain byte-for-byte unchanged, and neither they nor the production
window factory reference this macOS owner. The configuration guard rejects
the option before a non-macOS helper graph can be generated. A real retained
Win32 native-window producer and native compiler evidence remain required.

### Production and source identities

The enabled Linux build reproduces the canonical production shaders:

- vertex: 8,068 bytes,
  `b5750a179572b2fc3545c7472a28cac963351f7e7bc44df38190bb8961894095`;
- fragment: 7,824 bytes,
  `850d765cb1a6cd31dfbe16f94cedd89ab8306eca771cf833fe47c441b9c743fb`.

The ordered digest of the eight Stage 35 build, implementation, and test
SHA-256 records is
`4b851e9a80ce71a94291d4721f935ae09c3958d9c5c3ee4dab4937b94da94945`.
Both macOS source roots matched that digest before and after validation. The
individual identities are:

- `indra/cmake/Variables.cmake`:
  `b160c405899b1a15c005fbf9c2299ca6bedf00e1e32e2a2bb95d434ee4501997`;
- `indra/llwindow/CMakeLists.txt`:
  `a7a6c4a8e64974bcfa8754ea0d0a744fee4407304208329387503fee7f5a45ce`;
- `indra/llwindow/llwindowmacosxvulkan.cpp`:
  `a87b154d3eaa4b4cc6f12e52b9683a973544aa3c07adc8835c7a7d48268b75c5`;
- `indra/llwindow/llwindowmacosxvulkan.h`:
  `087ae6dca4141617035e0f64273ab484002dcbbdc3337d67797ffbe4819b2311`;
- `indra/llwindow/llwindowmacosxvulkan-objc.h`:
  `3c527af5a79e5d7dade88fc7824ffa0f8c4f101eda8b22cc50fd2dc2fbcf414f`;
- `indra/llwindow/llwindowmacosxvulkan-objc.mm`:
  `0c747358ff1241fc99acc7d03ba52d558ae9fed83f9bf276b2eae678628ccdc0`;
- `indra/llwindow/tests/llwindowmacosxvulkan_test.cpp`:
  `548a6e743a1862b4e20af284ec811347597aa5b5090e769e2a9b6be1e26cbc54`;
- `indra/llwindow/tests/llwindowvulkanmacoswsi_test.cpp`:
  `a4d66eda9492099f1003b4a9705941bb7dceb34aade4fbaf0156ffab2c4f14fe`.

The decision note is excluded because recording these identities changes its
own hash. No benchmark or timing path ran, and no performance claim is made.

## Explicit deferrals

- splitting `LLWindowMacOSX`, `LLOpenGLView`, XIB, input, scale, and teardown
  into graphics-API-neutral and OpenGL-specific pieces;
- exposing the macOS Vulkan owner through `LLWindowManager` or production
  viewer selection;
- a native retained `HINSTANCE` and `HWND` producer plus
  `vkCreateWin32SurfaceKHR`;
- physical-device and presentation-queue selection;
- device-level portability-subset policy;
- logical device, queue, swapchain, synchronization, and complete frame
  execution;
- resize, minimize, swapchain recreation, readback, and device-loss behavior;
- login, region, image parity, capture, benchmark, timing, or performance
  claims.

The master Stage 3 gate still requires a complete Vulkan frame on Linux,
Windows, and MoltenVK. This stage proves only the missing native macOS window
and surface seam. The Win32 producer remains the next platform dependency
before device and swapchain work.
