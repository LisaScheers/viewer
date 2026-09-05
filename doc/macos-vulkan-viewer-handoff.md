# macOS Vulkan viewer handoff — Stage 2M

This commit is an explicitly requested work-in-progress checkpoint for transfer
to a local Mac agent. **Stage 2M is not complete.**

## Implemented and verified

The actual viewer's `--vulkan` selection now supports macOS/MoltenVK. The viewer
owns one Cocoa window; its Vulkan owner borrows that window and attaches a
Metal view. The application renderer owns submission, outside
`LLWindow::swapBuffers()`. The real progress XUI uses the existing shared Vulkan
UI renderer. OpenGL remains a temporary fallback, without feature parity.

An isolated arm64 build on Vega passed. The real viewer presented 2,319 frames
with zero Vulkan validation messages and zero GL context/swap attempts, then
exited zero through signal-driven orderly shutdown. GL/shader managers, GL
images and media initialization remained absent. This was not visual proof or
a resize/minimize/restore test.

Checks passed: five runtime-controller tests, 26 ownership tests, two live
Cocoa/MoltenVK tests including borrowed-content restoration; the Linux shared
controller build and five tests; 14 Python smoke-gate tests; exact UI shader
regeneration and SPIR-V validation. The Mac build was arm64 only, despite its
build directory being named `build-darwin-universal`.

## Remaining work, in order

1. Use the actual local agent/terminal host with operator-approved Accessibility
   and Screen Recording permissions. The prior SSH host had neither. Do not
   modify privacy databases or bypass permissions.
2. Launch the freshly built viewer in a new isolated profile, with the explicit
   SDK loader, MoltenVK ICD and validation-layer paths. Keep certificate signing
   disabled; the tested executable was linker-ad-hoc-signed. No account login,
   credentials, installed-viewer replacement or system package changes are needed.
3. Capture actual progress UI at 25%, press Space without modifiers, then capture
   75% at the same extent. Inspect readable text, skin assets and localized fill
   change. Record Cocoa points versus Retina backing pixels explicitly.
4. Resize to a distinct drawable extent, minimize using native desktop controls,
   observe `status=suspended`, restore, and observe `status=resumed` plus repeated
   successful presentation. Capture resized and restored content.
5. Close using the native window action. Require exit zero without a timeout or
   signal fallback, clean validation, and renderer-before-window retirement with
   all negative-GL counters still zero.
6. Fix only failures exposed by these checks. Add the smallest durable Mac GUI
   driver and focused tests for new seams. Update the handoff and complete the
   stage only after the integrated gate passes.

The existing `scripts/vulkan/validate_viewer_vertical_slice.py` supplies reusable
`progress_landmarks`, `validate_progress_change` and `validate_viewer_log`
assertions. Its launcher is Linux/X11-specific: do not run that launcher on Mac.
Account for Retina scaling before adapting pixel thresholds; do not weaken the
oracle to admit blank frames or the old sampled fixture. Log `visible=1` alone
is not visual evidence. Require successful post-restore frames, not just a
restored window or a rebuilt swapchain.

## Build and launch notes

Prefer the existing isolated Vega build and its cached dependencies. After
pulling into a proper Git checkout, reconcile any local edits before copying
source into that build tree. The previous Vega source directory was an archive,
not a Git repository; `git pull` cannot be run there. Do not apply the old
uncommitted Stage 2M patch on top of this commit: it is already included.

Required cached build options: `LL_TESTS=ON`, `LL_VULKAN_RUNTIME_TEST=ON`,
`LL_VULKAN_MACOS_WSI=ON`, the Vulkan SDK headers/ICD/layer paths, and signing
disabled. The successful build used Xcode with macOS deployment target 11.0 and
`ARCHS=arm64 ONLY_ACTIVE_ARCH=YES`. Bundle assembly needs Python with `llsd`;
system Python alone did not provide it. Use isolated dependencies, preferably
temporary Nix tools, rather than global installation.

Focused incremental build with the configured CMake executable:

```sh
cmake --build build-darwin-universal --config Release \
  --target secondlife-bin INTEGRATION_TEST_llviewervulkanruntime \
    INTEGRATION_TEST_llwindowmacosxvulkan INTEGRATION_TEST_llwindowvulkanmacoswsi \
  -j 4 -- ARCHS=arm64 ONLY_ACTIVE_ARCH=YES
```

The app's executable is named by `CFBundleExecutable` in its `Info.plist`
(`Second Life Test` in the tested bundle), but its build target is
`secondlife-bin`. Launch that executable with:

```text
--vulkan --multiple --skipupdatecheck --noaudio --novoice
--nonotifications --noninteractive --disablecrashlogger
--set FullScreen FALSE --set WindowWidth 900 --set WindowHeight 600
```

Set process-local `SECONDLIFE_USER_DIR` to a fresh profile, `LL_VULKAN_LOADER` to
the SDK's `lib/libvulkan.1.dylib`, `VK_ICD_FILENAMES` to its MoltenVK ICD JSON,
and `VK_LAYER_PATH` to its explicit-layer directory. Repeat `--set` for each
pair; combining several pairs after one `--set` fails argument parsing.

The detailed machine-specific handoff, build paths and prior logs remain in
the existing Vega archive's `.audit/macos-local-agent-handoff.md`. Those private
artifacts are deliberately not committed. Its statement that the starting
19-file patch is uncommitted is superseded by this transfer checkpoint.

## Stop boundary

Return screenshot paths, input/resize/restore proof, frame and lifetime counters,
validation results, native close/exit evidence, reproducible commands and any
remaining blockers. Preserve useful evidence locally, not in Git. Leave no
diagnostic processes running.

Do not run benchmarks, Windows work, full packaging/default-off audits, or
another pipeline/descriptor-only stage. Do not start login/world/CEF work in
this handoff. The next original-plan milestone is normal startup/native login:
a prior Linux debugger continuation exposed a duplicate `LLProgressView` /
`LLEventPump::DupPumpName` during `initBase()`. That is diagnostic evidence, not
normal-startup acceptance; address it only in the next authorized stage.
