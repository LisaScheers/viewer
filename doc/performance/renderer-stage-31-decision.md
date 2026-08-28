# Stage 31 typed window graphics API selection

## Decision

Replace the public `LLWindowManager::createWindow()` `use_gl` Boolean with a
required scoped `LLWindow::GraphicsAPI` choice: `OpenGL`, `Vulkan`, or
`Headless`. Every in-tree caller now states its creation mode before the
defaulted window flags and options.

Stage 31 supports the two modes that already exist. `OpenGL` follows the
established platform or OSMesa OpenGL path. `Headless` follows the established
no-graphics `LLWindowHeadless` path. `Vulkan` and unknown enum values return
null before SDL initialization or any platform constructor. They cannot fall
back to an OpenGL or headless window.

This is a creation-intent contract, not a working Vulkan window or a
process-wide renderer owner. No viewer setting or command-line route selects
Vulkan in this stage. The default viewer remains OpenGL-only.

## Why the Boolean must go first

The old Boolean combined two decisions:

- `true` selected the platform window, whose SDL, Cocoa, and Win32
  implementations create OpenGL state unconditionally;
- `true` selected OSMesa in a headless build, which is still OpenGL; and
- `false` selected a no-graphics headless window.

It could not express Vulkan independently. Reusing `false` for Vulkan would
collide with the existing headless meaning, while passing a new Vulkan label
through the current platform constructors would still create GL or WGL state.
The typed selection makes future backend work compile-time visible without
claiming that those implementations are ready.

There are exactly two production factory callers. The viewer maps
`gHeadlessClient` to `Headless` and otherwise selects `OpenGL`. The appearance
utility selects `OpenGL`. Hidden parity-test windows, non-interactive mode,
VSync, fullscreen, pixel-depth, antialiasing, core-count, and OpenGL-version
inputs remain unchanged.

## Fail-closed ordering

`LLWindowManager::createWindow()` validates the enum before calling
`init_sdl()` or constructing any platform object. The supported selections
then enter the same branches and receive the same private constructor Boolean
values as before:

- `OpenGL` passes `true` to the existing platform or OSMesa implementation;
- `Headless` passes `false` to `LLWindowHeadless`.

The internal constructor Boolean parameters remain in their existing signatures to keep
this source-contract stage narrow. SDL, macOS, and Windows currently ignore
the value because their constructors enter OpenGL setup unconditionally;
OSMesa still consumes `true`, and `LLWindowHeadless` still receives `false`.
Removing that mechanical baggage belongs with the first real Vulkan-native
window branch, when its lifetime and teardown behavior can be defined rather
than guessed.

The manager does not store the selection in `LLWindow` and does not add a
global backend singleton. Because Vulkan is rejected, no successful Vulkan
window exists to mix with OpenGL. Process-wide ownership must be enforced when
the first Vulkan window can actually succeed and the renderer owns the route.

## Focused evidence

The focused integration test statically proves that `GraphicsAPI` is a scoped
enum, is not implicitly convertible from `bool`, and has three distinct
values. It calls the real factory with both `Vulkan` and an unknown underlying
value. Each request must return null, leave the `LLWindow` instance count
unchanged, and produce no manager-valid window.

On SDL builds the test also captures the initialized-subsystem mask and log
callback before and after each request. Both must remain unchanged, making an
accidental call to the current `init_sdl()` observable even if video
initialization fails. Source-order review proves that rejection also precedes
every platform constructor. The cross-platform test proves null return and no
surviving or tracked `LLWindow`; it does not treat the absence of a display as
the detector.

## WSI boundary

Stage 31 adds no Vulkan header, loader, instance extension, native target,
surface, device, swapchain, or frame path to `llwindow`.

A later SDL producer must create a real Vulkan window, ask SDL for the exact
instance extensions required by its active video backend, deep-copy those
borrowed names, and bind loader lifetime to the request. It must not infer X11
or Wayland extensions from environment variables.

A later macOS producer needs a retained Metal-backed view and
`CAMetalLayer`; the current `NSOpenGLView` and returned `NSWindow` do not form a
valid `VK_EXT_metal_surface` target. A later Windows producer can borrow the
current `HINSTANCE` and `HWND`, but must split native-window construction from
WGL setup and guard against handle recreation.

The native window owns those platform objects. The future WSI request owns
only copied extension names and a tagged borrowed target tied to a native
window generation. Surface destruction must precede destruction or recreation
of that native target.

## Evidence boundary

Stage 31 requires warnings-as-errors viewer and focused-test builds on Linux
and universal macOS, the existing renderer-focused tests, default-off Vulkan
target and package isolation, canonical production shader hashes, and loader
dependency scans. Windows receives source, preprocessing, and project-file
visibility review because no Windows compiler is available.

No viewer, account, region, pixel comparison, benchmark, or timing path is
needed. This stage makes no performance claim and does not close master Stage
2 or master Stage 3.

## Linux evidence

The warnings-as-errors Release build passes for the focused window-selection
test, the viewer, and the appearance utility with both Vulkan diagnostic
options enabled. The 21 existing focused renderer routes and the new window
route pass. The 12 shader-reflection, eight artifact-delivery, and 57 benchmark
harness unit tests also pass; the benchmark executable was not launched.

With both Vulkan options disabled, the generated target graph contains zero
Vulkan or SPIR-V targets. The same three C++ targets and the focused window
route pass, while the viewer and focused executable have no Vulkan loader
dependency. The enabled build was restored after that isolation check.

The changed OSMesa factory branch also preprocesses and compiles under
warnings-as-errors with an opaque temporary OSMesa context declaration. That
checks this stage's selection syntax only: current system Mesa does not provide
OSMesa, so this is not an OSMesa link or runtime claim.

## macOS evidence

A disposable, user-owned universal Release build and package pass under
warnings-as-errors with both Vulkan diagnostic options disabled. The viewer is
arm64 and x86_64, while the focused test follows the project's arm64 test
policy and verifies its code signature. The package contains zero SPIR-V files
and no bundled Vulkan loader. All 367 packaged Mach-O files are readable and
none depends on Vulkan or MoltenVK. Four Chromium helper filenames contain
parentheses and were inspected through temporary parenthesis-free aliases to
avoid `otool` interpreting their names as architecture syntax. The 11
option-independent renderer routes and the window-selection route pass.

The same disposable source was then built and packaged with both diagnostics
enabled against one locally installed LunarG SDK. The generic global-dispatch
archive, generated loader, and viewer are universal; the focused test remains
signed arm64. The same 21 focused renderer routes and the window-selection
route pass. The native arm64 MoltenVK material diagnostic passes all 832
component checks and 39 rejection cases with one recording, one submission,
portability enumeration and portability subset enabled, and zero validation
messages. Its temporary artifact was deleted immediately.

The enabled package contains exactly the two canonical production SPIR-V
files. All 367 packaged Mach-O files remain readable, none depends on Vulkan or
MoltenVK, and no loader is bundled. The 12 shader-reflection, eight
artifact-delivery, and 57 benchmark-harness unit tests pass. The six compiled
and build-wiring inputs match the local candidate byte for byte. Package
signing was deliberately disabled; no signed-app claim is made.

No viewer, account, region, benchmark, or timing path was used. The SDK,
builds, logs, generated artifacts, and disposable source are removed after the
source identities are verified.

## Windows and alternate-path boundary

No Windows compiler is available. Windows evidence is therefore limited to
source-order, constructor-parity, preprocessing, and project-visibility
review. The Win32 path still receives the same OpenGL constructor arguments,
and this stage adds no Vulkan header, type, symbol, library, or surface code to
that implementation. A native Windows build remains a required later gate
before a Win32 Vulkan window can be claimed.

## Production artifact hashes

The enabled Linux and macOS builds reproduce the canonical production shader
artifacts:

- vertex: 8,068 bytes,
  `b5750a179572b2fc3545c7472a28cac963351f7e7bc44df38190bb8961894095`;
- fragment: 7,824 bytes,
  `850d765cb1a6cd31dfbe16f94cedd89ab8306eca771cf833fe47c441b9c743fb`.

## Candidate source hashes

The final compiled and build-wiring inputs have these SHA-256 identities:

- `indra/llwindow/llwindow.h`:
  `a2d3e5e3524a7b3fc36e2557acd94d47009b5f3b5cf15a1518ae6f9d08ec0340`;
- `indra/llwindow/llwindow.cpp`:
  `fd2fca01ad8ddc4e7905b61582a55c2e229bc0b19b5a4881d1f05d9374217201`;
- `indra/llwindow/CMakeLists.txt`:
  `5eae390c502ba06e04125794f35c330b1fe2c398ce61b39446cc8f96b972a3b6`;
- `indra/llwindow/tests/llwindowgraphicsapi_test.cpp`:
  `3a152eeefe275862bbe7a4f8914dcd6365c7e5ec225c7de8470d1eb12e4b6de1`;
- `indra/newview/llviewerwindow.cpp`:
  `8b7be89e01dbccda6a559416596cc679726a8aa6fb4f36f842ea86a1e7788bc2`;
- `indra/llappearanceutility/llbakingwindow.cpp`:
  `a3a1d74e0f20895269af9ada6a608e2869de74aaf18419e1ef6ac3d0f17210bd`.

The decision note is excluded because recording these identities changes its
own hash.

## Outcome

Stage 31 is complete. Window creation now carries an explicit, fail-closed
graphics API selection without changing the default OpenGL viewer or claiming
Vulkan WSI support. The next stage must make one platform produce a real,
lifetime-safe Vulkan WSI request before instance or surface integration moves
into the viewer.
