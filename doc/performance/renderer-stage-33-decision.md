# Stage 33 owned Vulkan instance generation

## Decision

Add one reusable, move-only Vulkan instance transaction behind the existing
default-off runtime diagnostics. The renderer-side owner consumes an exact
window requirements generation, authenticates the supplied resolver through
the Stage 30 global-dispatch contract, applies explicit instance policy, and
owns the resulting `VkInstance` and optional validation messenger.

The Linux SDL adapter builds that request only from its live Stage 32
requirements value and stores a successful instance generation inside the
same SDL Vulkan window owner. The instance therefore resets before the
requirements, native window, explicit loader reference, and SDL video state.

The two production callers still select OpenGL. This stage adds no viewer
setting, command-line route, login, surface, physical device, logical device,
queue, swapchain, command buffer, frame, benchmark, timing, or performance
claim.

## Reusable request and owner

`VulkanInstanceRequest` contains only renderer-facing inputs:

- the exact `vkGetInstanceProcAddr` resolver borrowed from a live window;
- an ordered span of required instance-extension names;
- a nonzero native-window generation plus a freshness callback;
- an explicit validation mode; and
- an explicit portability-enumeration mode.

It contains no SDL type, native window handle, platform-specific WSI guess, or
direct Vulkan loader reference. The request is synchronous. A successful
transaction deep-copies all names needed by its owner before returning.

`VulkanInstanceGeneration` is movable but neither copyable nor move
assignable. One successful value owns:

- the authenticated Stage 30 global dispatch generation;
- one non-null Vulkan 1.1 instance;
- the exact resolved `vkDestroyInstance` command;
- an optional debug-utils messenger and its destroy command;
- stable heap-backed validation callback state;
- the enabled extension and layer names in creation order;
- the originating native-window generation; and
- the explicit portability-enumeration decision.

Reset is idempotent. It destroys the debug messenger first and the instance
second, then releases retained dispatch, names, generation metadata, and
validation state.

## Bounded capability policy

Global commands come only from the Stage 30 authenticated resolver. Extension
and layer enumeration accepts `VK_INCOMPLETE` only through a four-attempt
retry loop, caps either property count at 4,096, and rejects a returned value
count that exceeds the allocated count. Every fixed Vulkan property name must
contain a terminator inside its declared buffer.

The window request accepts at most 64 nonempty, bounded, distinct extension
names. Their exact input order is retained at the front of the enabled list,
and every name must be advertised. The core does not infer X11, Wayland,
Metal, Win32, or another WSI extension.

Validation has two modes:

- `Disabled` adds no validation layer or debug-utils policy.
- `Required` atomically requires `VK_LAYER_KHRONOS_validation` and
  `VK_EXT_debug_utils`, enables each once, chains the callback into instance
  creation, and creates an explicit debug messenger.

There is no silent validation downgrade. Portability enumeration is also
explicit. `EnableIfAvailable` appends
`VK_KHR_portability_enumeration` once and sets its instance flag only when the
loader advertises it. Older headers fail with a typed policy error if that
decision cannot be represented. This does not claim or enable the later
device-level portability-subset extension.

All creation uses `RENDERER_VULKAN_API_VERSION`, null allocation callbacks,
and the authenticated `vkCreateInstance` pointer. The owner and applications
have no direct Vulkan entry-point import.

## Generation freshness and rollback

The native generation is checked before global dispatch resolution, after
capability enumeration, immediately before instance creation, after the
mandatory instance destroy command is resolved, and after validation
messenger creation. A stale result at any boundary publishes no owner.

After a non-null instance is created, `vkDestroyInstance` is resolved first.
Required validation then resolves both debug-utils commands before creating
the messenger. Failed Vulkan results and successful calls that return null
handles are distinct typed errors. Once the mandatory destroy command is
available, every later failure rolls back the objects earned by that path in
reverse order.

A nonconforming loader can create an instance and then withhold the mandatory
`vkDestroyInstance` command. Loader-independent code has no legal recovery
call in that branch. The transaction reports the exact command breach and
publishes no owner, but does not falsely describe the leaked driver object as
ordinary successful rollback.

Typed errors retain the exact `VkResult`, an optional nested Stage 30 dispatch
failure, the missing instance command, the required-extension index, malformed
property index, or observed count where applicable. Allocation failure is
caught at the transaction boundary.

## Coherent validation diagnostics

The callback userdata has a stable heap address independent of owner moves.
The callback is `noexcept`, catches every exception before the C ABI boundary,
saturates its message count, and copies at most the first 1,023 message bytes
into fixed storage.

Review changed the planned split atomic and locked design to one mutex for the
count and first-message fields. That makes each snapshot coherent: a reader
cannot observe a nonzero count with an unpublished first message or the
inverse. The callback still performs no allocation. A concurrent writer and
reader stress case checks the snapshot invariant across 10,000 callbacks.

## SDL installation and teardown

`LLWindowSDLVulkan::acquireInstanceGeneration()` translates one current
`LLWindowVulkanRequirements` value into the reusable request. It passes SDL's
exact resolver, exact extension order, and exact generation. Production uses
required validation and enables portability enumeration only if advertised.

The window owner rejects duplicate instance acquisition. Move construction
and move assignment transfer the instance together with the requirements,
native window, and explicit loader reference. Move assignment resets the
destination first.

The complete successful teardown order is:

1. debug-utils messenger;
2. Vulkan instance;
3. immutable window requirements;
4. SDL Vulkan window and its implicit loader reference;
5. explicit SDL Vulkan loader reference; and
6. manager-owned SDL video state.

An instance acquisition failure destroys only the objects earned by that
path, then the temporary SDL owner performs the existing Stage 32 window and
loader rollback. No `VkSurfaceKHR` exists in this stage.

## Focused evidence boundary

The renderer test uses a fake Vulkan 1.1 resolver and no loader library. Its 13
cases cover type traits, exact creation policy, validation-disabled and
validation-required success, callback movement and coherent snapshots,
portability behavior with current and older headers, stale checks at each
boundary, count and value `VK_INCOMPLETE` convergence and retry caps,
overreported counts, malformed properties, missing required policy, allocation
failure, failed and poisoned outputs, missing commands, idempotent reset, and
reverse-order rollback.

The injected SDL test uses the same fake resolver. Its nine cases cover
validation-disabled acquisition, duplicate rejection, move construction,
move assignment over an owned destination, exact instance destruction before
requirements, window, and loader teardown, both SDL loader references,
idempotent reset, and failed instance-creation cleanup.

The separately opted-in native route uses a disposable X11 display and a
known software Vulkan implementation. It requires a real hidden SDL Vulkan
window, SDL's exact extension prefix and resolver, one non-null Vulkan 1.1
instance with required validation, zero validation messages immediately
before teardown, no GL context or manager state, and restoration of SDL and
window-manager state. It creates no surface and makes no Wayland claim.

## Build and dependency boundary

The generic instance target exists only when the runtime or tonemap diagnostic
is enabled. It depends on Stage 30 global dispatch but not `llwindow`, SDL, or
a Vulkan loader library. The SDL adapter enters the Linux graph only when the
existing `LL_VULKAN_SDL_WSI` option and its prerequisites are enabled.

With every optional renderer migration switch disabled, the global-dispatch,
instance, SDL owner, and native WSI targets are absent. The neutral graphics
API and requirements contracts remain because they are platform-independent
and default-off selection still needs to fail before platform initialization.

## Explicit deferrals

- SDL `VkSurfaceKHR` creation and generation-checked destruction;
- a retained Metal-backed view and `CAMetalLayer` producer on macOS;
- a non-WGL `HINSTANCE` and `HWND` generation on Windows;
- physical-device and presentation-queue selection;
- logical device, swapchain, synchronization, and complete frame execution;
- a production viewer Vulkan route or replacement of `gGLManager`;
- login, region, image-parity, benchmark, timing, or performance claims.

## Validation results

### Linux enabled path

The warnings-as-errors Release build passes for the viewer, appearance
utility, reusable instance target, injected SDL owner, and native SDL WSI
route with the benchmark, runtime, tonemap, and SDL migration switches
enabled. All 25 renderer routes and four window routes pass. The new focused
groups contain 13 renderer instance cases and nine SDL ownership cases.

The opted-in native route passes against a real hidden X11 SDL window and
software Vulkan driver. The window owns a non-null Vulkan 1.1 instance,
retains SDL's required extensions as the enabled-list prefix, enables required
validation, reports zero validation messages immediately before destruction,
and restores the initial manager, SDL, log, GL-context, and GL-manager state.
No surface is created.

The 12 shader-reflection, eight artifact-delivery, and 57 benchmark-harness
unit tests pass. The harness suite exercises Python logic only; no benchmark
or timing path runs. The viewer, appearance utility, and three focused Vulkan
executables have neither a direct Vulkan-loader dependency nor an undefined
direct Vulkan entry point.

### Linux disabled isolation

A fresh warnings-as-errors Release configuration with the benchmark, runtime,
tonemap, and SDL migration switches disabled builds the viewer, appearance
utility, graphics-API test, and neutral requirements test. The optional
global-dispatch, instance, SDL owner, and native WSI targets are absent. Both
remaining window routes pass.

The two applications and two focused executables have no Vulkan-loader
dependency or direct Vulkan import. Outside the shared dependency cache, the
build contains no Vulkan loader, MoltenVK, or SPIR-V payload. The disposable
build and temporary tool-discovery metadata were removed after verification.

### macOS isolation

A disposable runtime-enabled universal warnings-as-errors build produced the
generic global-dispatch and instance archives for both x86_64 and arm64. The
focused fake instance test followed project policy by running as a valid
ad-hoc-signed arm64 executable and passed all 13 cases. SDL WSI remained
disabled and absent from the target graph. Neither the focused executable nor
its archives gained a direct Vulkan or MoltenVK dependency or a direct Vulkan
entry-point import.

A separate fresh configuration with the benchmark, runtime, tonemap, and SDL
migration switches disabled built the universal ReleaseOS viewer and packaged
the application. All 11 option-independent renderer routes and both remaining
window routes passed. Every optional Vulkan target was absent. The packaged
application contained no Vulkan, MoltenVK, or SPIR-V payload, and all 367
packaged Mach-O files were checked with zero forbidden dependencies. Four
Chromium helper names required safe aliases because the dependency inspector
interprets parentheses in paths.

The first all-options-off configuration did not discover an already available
universal NDOF library. A fresh disposable configuration given that exact
dependency location passed, so this was an environment-discovery issue rather
than a Stage 33 source failure. All 11 implementation, build, and test inputs
matched byte for byte before and after validation. Disposable sources, builds,
transfers, logs, packages, and environment state were removed. No benchmark or
timing path ran.

### Other platform boundary

No Windows compiler is available. Source and build-graph review confirms that
the reusable instance core has no SDL dependency, the SDL adapter remains
Linux-only, and no Cocoa, Win32, OSMesa, or production factory route changes.
A native Windows producer and build remain required before a Win32 Vulkan
window can be claimed.

### Production artifact hashes

The enabled Linux build reproduces the canonical production shaders:

- vertex: 8,068 bytes,
  `b5750a179572b2fc3545c7472a28cac963351f7e7bc44df38190bb8961894095`;
- fragment: 7,824 bytes,
  `850d765cb1a6cd31dfbe16f94cedd89ab8306eca771cf833fe47c441b9c743fb`.

The ordered digest of the 11 Stage 33 implementation, build, and test
SHA-256 records is
`ee24eb6673c7d00f6846569d6f16c5d0cce7453f2bc00a4c278abfdb2f32450b`.
Their individual identities are:

- `indra/llrender/CMakeLists.txt`:
  `74b3605d595d3c3aa6b4dc800dd7c1d4c8f543c05f1a7e586fd15ad1ae650ed7`;
- `indra/llrender/vulkan/llrendervulkaninstance.cpp`:
  `b40f8e89a022b5161c5e386ab2c8652dfa3567f0588259424d2c6c078a505b0a`;
- `indra/llrender/vulkan/llrendervulkaninstance.h`:
  `b40ee3d4c8b2cee8c60e856d9c6d8f5fd9887314e666b33cecfad3e551c684de`;
- `indra/llrender/tests/llrendervulkaninstance_test.cpp`:
  `f0392c6e11b49307d9b5ce0dde30457f7fd8887de659d829c4b6a894556ded90`;
- `indra/llwindow/CMakeLists.txt`:
  `c2aae7d9bef81e4729931f61209cfbd4b58d53b1ec6c963d69e07416679381c0`;
- `indra/llwindow/llwindowsdl.cpp`:
  `71dfaf8a9465e71269a9ca61311a33a07ebd7174a6b58eef796007e1c07f42ab`;
- `indra/llwindow/llwindowsdl.h`:
  `60f726cbe8fb445d9aca6ff1e3819b6a407486b5f2a5dfd27249fb89de0e30f1`;
- `indra/llwindow/llwindowsdlvulkan.cpp`:
  `19b260f6cf68dda698b521b5c773b2c3dcb7ced06158e916466f8aa6bdf9f1d9`;
- `indra/llwindow/llwindowsdlvulkan.h`:
  `370cad36509c9c3790d11a7d5e2c3a0803761cf4f6b8ec389b0bb6cfa825f4a0`;
- `indra/llwindow/tests/llwindowsdlvulkan_test.cpp`:
  `d23a295d1346d9ee9fee02cfca2c72059764e4029770de118166e86a31fb847c`;
- `indra/llwindow/tests/llwindowvulkansdlwsi_test.cpp`:
  `a234ef894627ef5e02086bab39891d7090442fe9819b9f3a047661996a387213`.

The decision note is excluded because recording these identities changes its
own hash.

### Outcome

Stage 33 passes its technical exit gates. The exact live window request now
feeds one reusable, authenticated Vulkan instance transaction, the Linux SDL
owner preserves its complete lifetime, validation is explicit and coherent,
and enabled and disabled isolation holds on Linux and macOS. Production viewer
routes remain OpenGL, and Windows remains source-only. The next committable
stage may add a generation-checked SDL surface, but it must not claim the
master plan's cross-platform vertical slice before native macOS and Win32
producers exist.
