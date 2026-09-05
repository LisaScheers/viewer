# Stage 37 Windows runner

This runner validates the isolated Win32 Vulkan owner on supported Windows. It
does not benchmark, install, launch, or register the viewer. Its default mode
also builds a fresh default-off ReleaseOS viewer and unsigned local package to
check that the diagnostic remains outside the production graph.

The existing CEF media plugin ships its own `vulkan-1.dll`,
`vk_swiftshader.dll`, and `vk_swiftshader_icd.json` below `llplugin`. The
package check records those exact baseline payloads and rejects any other
Vulkan-named payload, MoltenVK file, SPIR-V file, or Vulkan material shader.

## Host dependencies

Use a Windows 10 or Windows 11 x64 machine with a Vulkan-capable GPU. Install:

1. Visual Studio 2022 Community or Build Tools with **Desktop development with
   C++**, the MSVC v143 x64/x86 tools, and Windows SDK 10.0.22000.0 or newer.
2. Git for Windows x64.
3. Python 3.11 x64 with the `py.exe` launcher and `pip`.
4. CMake on `PATH`.
5. The current LunarG Vulkan SDK for Windows x64, including
   `VK_LAYER_KHRONOS_validation` and `vulkaninfoSDK.exe`.
6. The current Vulkan driver from the GPU vendor. The system loader at
   `%WINDIR%\System32\vulkan-1.dll` must expose Vulkan 1.1 or newer.
7. .NET 9 SDK and the Velopack `vpk` global tool for the default full run.

PowerShell 5.1 is part of supported Windows installations. The script creates
its own Python environment and installs `autobuild` and `llsd` there. It also
clones the public Second Life build variables into its disposable work root.
It redirects process-local temporary files into that owned root and restores
the caller's environment afterward. Do not install those Python packages
globally.

The full run downloads the viewer's public Autobuild packages, configures two
isolated build graphs, and compiles the full viewer once. Use a machine with
ample free disk space and an unrestricted connection to GitHub release assets.
Installers may need administrator access, but the validation script itself
should run as the normal desktop user.

## Suggested install commands

Run these from an elevated PowerShell only if the corresponding dependency is
not already installed:

```powershell
winget install --id Git.Git -e --source winget
winget install --id Python.Python.3.11 -e --source winget
winget install --id Kitware.CMake -e --source winget
winget install --id Microsoft.VisualStudio.2022.Community -e --source winget --override "--wait --passive --add Microsoft.VisualStudio.Workload.NativeDesktop --includeRecommended"
winget install --id Microsoft.DotNet.SDK.9 -e --source winget
dotnet tool install --global vpk
```

Install the Windows x64 Vulkan SDK from
[LunarG](https://vulkan.lunarg.com/sdk/home). Install the current driver from
the GPU vendor, then restart PowerShell so `VULKAN_SDK` and the SDK `Bin`
directory are visible.

The repository's supported Windows CI uses Visual Studio 2022, Python 3.11,
Autobuild, LLSD, .NET 9, and Velopack. The relevant upstream references are:

- [Microsoft C++ installation](https://learn.microsoft.com/en-us/cpp/build/vscpp-step-0-installation)
- [Visual Studio command-line installation](https://learn.microsoft.com/en-us/visualstudio/install/use-command-line-parameters-to-install-visual-studio)
- [Python 3.11 on Windows](https://docs.python.org/3.11/using/windows.html)
- [Git for Windows](https://git-scm.com/install/windows)
- [CMake downloads](https://cmake.org/download/)
- [LunarG Windows Vulkan SDK guide](https://vulkan.lunarg.com/doc/view/latest/windows/getting_started.html)
- [.NET installation on Windows](https://learn.microsoft.com/en-us/dotnet/core/install/windows)

## Run

Open a new normal 64-bit PowerShell in a clean checkout of the requested
commit. The agent supplying the commit will provide its full hash.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\perf\validate_win32_vulkan.ps1 `
  -ExpectedCommit <full-40-character-commit>
```

The script uses unique user-temp directories by default. Pass a short new
`-WorkRoot` path if the normal temp directory is unusually long. The path must
not already exist. `-FocusedOnly` skips the default-off viewer and package
proof, so that mode cannot complete Stage 37. `-KeepWorkRoot` retains build
products for debugging, prints their local path, and prevents the cleanup gate
from passing. Remove that directory manually when debugging is complete.

At completion, send `stage37-summary.json` from the printed result directory.
If `FailureLog` is set, also send that sanitized log. Do not send the raw step
logs or disposable build tree. Delete the result directory after the handoff is
accepted.
