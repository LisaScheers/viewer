# Stage 38 Vulkan presentation-device selection decision

## Decision

Retain one loader-neutral, default-off presentation-device selection
transaction for the exact Vulkan instance and surface generation. The
transaction selects a standard Vulkan 1.1 or newer physical device and one
queue family that supports both graphics and presentation to that surface.

This is the next bounded part of master Stage 3. It does not create a logical
device, retrieve a queue, query swapchain capabilities, or change the
production OpenGL renderer.

Windows execution remains deferred by user direction. Stage 38 therefore has
native admission evidence for Linux and macOS only and makes no new Windows
runtime claim.

## Selection transaction

`resolveVulkanPhysicalDeviceGeneration()` consumes only an authenticated
`vkGetInstanceProcAddr`, its exact live `VkInstance`, and its exact live
`VkSurfaceKHR`. It resolves these commands on demand:

- `vkEnumeratePhysicalDevices`;
- `vkGetPhysicalDeviceProperties`;
- `vkGetPhysicalDeviceQueueFamilyProperties`;
- `vkGetPhysicalDeviceSurfaceSupportKHR`;
- `vkEnumerateDeviceExtensionProperties`.

The resolver rejects null inputs and missing commands before selection. It
bounds physical devices and queue families to 256 entries, device extensions
to 4,096 entries, and changing-count enumerations to four attempts. Vulkan
failures, incomplete retries, excessive counts, malformed names, invalid
enumeration output, and scratch allocation failure remain typed and preserve
the relevant command, `VkResult`, attempt, and candidate indices.

Candidates are evaluated in Vulkan enumeration order. A candidate is eligible
only when all of the following are true:

1. the physical-device handle is non-null;
2. the API variant is the standard variant and the API version is at least
   Vulkan 1.1;
3. `VK_KHR_swapchain` is advertised;
4. at least one nonempty queue family supports both `VK_QUEUE_GRAPHICS_BIT`
   and presentation to the exact supplied surface.

The first eligible physical device is selected. Within that device, the
lowest eligible unified queue-family index is selected. A split graphics and
presentation queue pair is deliberately not admitted in this stage.

The immutable result records the resolver, exact instance and surface,
physical-device handle and enumeration index, device properties, queue-family
index and properties, and required device extensions. The required extension
list is exactly `VK_KHR_swapchain`, followed by
`VK_KHR_portability_subset` only when the selected device advertises that
extension. Selection owns no Vulkan object.

## Parent ownership and lifetime

`VulkanInstanceGeneration` owns the selection as a private child of its exact
surface generation. Acquisition requires live and current instance, surface,
and native-window generations. Duplicate ownership, stale callbacks,
generation mismatches, resolution failure, and allocation failure remain
distinct typed outcomes.

Selection is published only after the resolver succeeds, allocation succeeds,
and the parent and native-window freshness checks pass again. A failed
transaction leaves the live instance and surface available for retry.

Move construction transfers the complete instance, surface, and selection
chain. Explicit selection reset removes only the selection child. Surface
reset first removes the selection child, then destroys the surface. Full reset
continues with the validation messenger and instance after both children are
gone.

The existing opt-in SDL Vulkan branch inside the viewer window factory selects
immediately after acquiring its surface. That branch is available only when
the default-off SDL WSI test gate is compiled and explicitly requested. The
isolated macOS owner remains outside the viewer window factory and its native
test calls the same operation explicitly after acquiring the Metal surface.

## Build graph and production isolation

The selector archive and its focused executable exist only when the existing
default-off Vulkan runtime or tonemap experiment is enabled. The instance
archive gains a one-way dependency on the selector archive only inside that
experimental graph. The selector and instance sources compile with
`VK_NO_PROTOTYPES` and use the retained resolver rather than a Vulkan loader
link edge.

The new selector source enters only the gated experimental archives. The
existing normal `llwindowsdl.cpp` source gains integration inside its
default-off `LL_VULKAN_SDL_WSI` branch. With all experimental switches off,
no Stage 38 target, object, dependency, or payload enters the viewer or
package. The default production viewer remains OpenGL.

## Focused contract evidence

The physical-device fake-dispatch suite passes all eight cases. It covers
invalid requests and missing commands, changing enumerations and retry bounds,
malformed and excessive results, candidate filtering, exact extension policy,
unified queue selection, typed query failures, immutable provenance, and move
behavior. Scratch allocation failure remains a typed selector outcome but is
not forced by this suite.

The parent-instance suite passes all 27 cases. Its Stage 38 additions cover
invalid and stale ownership, absent surface state, native-generation mismatch,
duplicate selection, resolver propagation, failed selection, allocation
failure, atomic publication, retry, move, explicit reset, surface reset, and
child-before-parent teardown.

Independent staged review found no implementation correctness, lifecycle,
scope, CMake, or test-coverage defect and corrected four evidence-note
overstatements before commit. The staged diff also passes whitespace checking.

## Native and isolation evidence

### Linux

The enabled warnings-as-errors graph builds the focused Stage 38 targets, the
viewer, and `llappearance`. Twenty-six renderer CTest routes and four affected
window routes pass. The hidden X11 SDL route passes once against a real
lavapipe surface with required Khronos validation. It selects a non-null
standard Vulkan 1.1 presentation device and unified queue family, and its
creation and teardown assertions observe zero validation messages.

A fresh all-six-off Ninja ReleaseOS graph builds the viewer, package, and
`llappearance`. The Stage 38 targets and artifacts are absent. The package has
6,336 regular files and 17 ELF files. Across those ELF files there are zero
Vulkan or MoltenVK dynamic dependencies and zero direct `vk*` imports. No file
starts with SPIR-V magic. The archive has 6,457 entries and no Stage 38 entry.

The Linux package retains four pre-existing Chromium payload names:
`lib/libvulkan.so.1`, `lib/libvk_swiftshader.so`,
`bin/vk_swiftshader_icd.json`, and `lib/vk_swiftshader_icd.json`. They are not
Stage 38 renderer payload, so this decision does not claim that the package is
free of every Vulkan-named Chromium file.

The enabled viewer and the exact physical-device, instance, and SDL test
executables also have zero Vulkan or MoltenVK dynamic dependencies and zero
direct `vk*` imports.

### macOS

The enabled universal ReleaseOS graph builds and stages the application with
warnings as errors. The four global-dispatch, physical-device, instance, and
macOS-owner archives contain both `arm64` and `x86_64`. The staged viewer
executable also contains both architectures. The five focused executables are
native arm64 Mach-O files and pass code-signature verification.

Seven focused CTest routes pass, including all eight physical-device cases and
all 27 parent-instance cases. The opt-in native route passes once on `vega`
using the Vulkan 1.4.357 loader and validation layer with MoltenVK 1.4.2. It
creates a hidden 1,280 by 720 Metal surface, requires validation, selects a
standard Vulkan 1.1 or newer physical device and unified queue family, requires
exact swapchain and portability-subset device extensions, resets the selection
before the surface, and observes zero validation messages. The current CGL
context, OpenGL manager state, and `LLWindow` instance count remain unchanged.

A separate fresh all-six-off universal ReleaseOS graph builds the viewer and
completes the application package target with warnings as errors. Its 178
generated targets contain none of the nine Stage 38 archives or focused tests,
and no matching Stage 38 artifact exists in the build tree. The exact 11
option-independent renderer routes and two option-independent window routes
all pass.

The packaged viewer executable contains both `arm64` and `x86_64`. Across
6,707 regular package files, there is no Vulkan, MoltenVK, or SPIR-V filename
and no SPIR-V magic payload. All 367 packaged Mach-O files were inspected.
Four Chromium helper executables with parentheses in their paths were checked
through SHA-256-identical safe aliases. The complete scan finds zero Vulkan or
MoltenVK dependencies and zero direct `vk*` imports.

### Tool identities

Linux uses CMake 4.3.4 with Ninja, GCC 15.3.0, Vulkan headers 1.4.357, and the
Vulkan 1.4.357 loader and validation layer. macOS uses macOS 26.6.2 on Apple
Silicon, Xcode 26.6 with the macOS 26.5 SDK, CMake 4.3.4, Python 3.13.15,
Autobuild 3.10.2, Vulkan headers, loader, and validation layer 1.4.357, and
MoltenVK 1.4.2. The macOS ReleaseOS deployment target is 11.

The ordered SHA-256 digest of the 14 Stage 38 build, implementation, and test
files is
`efd40e3faf3f32935d3dd920f9c7ca5fe19949a05822e94b916559efcec6035d`.
The decision note is excluded because including its own digest would be
self-referential. The same source digest is present in the Linux checkout and
both disposable macOS source snapshots.

No benchmark, viewer launch, login, account, or world access occurred. No
benchmark result or performance timing is retained, interpreted, or reported.

## Explicit deferrals

- Windows build and native execution of the already committed Win32 runner;
- logical-device creation and destruction;
- retrieval or ownership of a Vulkan queue handle;
- swapchain capability, format, and present-mode queries;
- swapchain, image, image-view, synchronization, command, submission, and
  frame ownership;
- connection to the production graphics selector or replacement of OpenGL;
- a Vulkan-to-OpenGL compatibility layer;
- benchmark, image-parity, timing, or performance claims.

## Next dependent decision

After this stage is committed and both plans are reanalysed, Stage 39 may own
one logical-device generation for the exact Stage 38 physical device and queue
family. It must authenticate Stage 28's fixed `independentBlend` requirement,
enable exactly the selected device extensions, create one queue, preserve
loader-neutral lifetime and error contracts, and stop before all swapchain
queries or objects.
