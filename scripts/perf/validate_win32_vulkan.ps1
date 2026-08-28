#requires -Version 5.1

<#
.SYNOPSIS
Runs the supported Windows admission checks for the isolated Win32 Vulkan owner.

.DESCRIPTION
The script validates one exact viewer commit in disposable local Git clones. The
default run builds and executes both focused Win32 Vulkan tests, checks their
imports, then builds a fresh default-off ReleaseOS viewer and package. It does
not install, launch, or register the viewer.

The script creates an isolated Python environment and downloads the public
Autobuild dependencies. Install the host prerequisites documented in
doc/performance/renderer-stage-37-windows-runner.md before running it.

.PARAMETER ExpectedCommit
The full 40-character commit that must be checked out and validated.

.PARAMETER RepositoryRoot
The viewer checkout containing this script. The default is the repository that
contains the script.

.PARAMETER VulkanLoaderPath
The absolute loader DLL passed to the native test. The default is the loader
installed in Windows System32 by the GPU driver.

.PARAMETER VulkanSdkRoot
The installed LunarG Vulkan SDK root. The default is VULKAN_SDK.

.PARAMETER WorkRoot
A new, short-lived directory for source clones and build products. It must not
already exist. The default is a unique directory below the user temp folder.

.PARAMETER ResultRoot
A new directory that retains logs and stage37-summary.json. It must not be
inside WorkRoot. The default is a unique directory below the user temp folder.

.PARAMETER FocusedOnly
Stops after the enabled focused tests and import checks. This is useful for
iteration, but it does not clear the complete admission gate.

.PARAMETER KeepWorkRoot
Keeps disposable sources and build products for debugging. The default removes
them after logs and evidence have been written.

.EXAMPLE
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\perf\validate_win32_vulkan.ps1 -ExpectedCommit 0123456789abcdef0123456789abcdef01234567
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[0-9A-Fa-f]{40}$')]
    [string]$ExpectedCommit,

    [string]$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,

    [string]$VulkanLoaderPath = (Join-Path $env:WINDIR 'System32\vulkan-1.dll'),

    [string]$VulkanSdkRoot = $env:VULKAN_SDK,

    [string]$WorkRoot,

    [string]$ResultRoot,

    [switch]$FocusedOnly,

    [switch]$KeepWorkRoot
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$script:CurrentStep = 'startup'
$script:StepRecords = New-Object System.Collections.Generic.List[object]
$script:TouchedEnvironment = @(
    'AUTOBUILD_ADDRSIZE',
    'AUTOBUILD_BUILD_ID',
    'AUTOBUILD_CONFIGURATION',
    'AUTOBUILD_GITHUB_TOKEN',
    'AUTOBUILD_INSTALLABLE_CACHE',
    'AUTOBUILD_VARIABLES_FILE',
    'AUTOBUILD_VCS_INFO',
    'AUTOBUILD_VSVER',
    'BUGSPLAT_DB',
    'GITHUB_ACTIONS',
    'GITHUB_OUTPUT',
    'GITHUB_TOKEN',
    'LL_RUN_VULKAN_WIN32_WSI_NATIVE',
    'LL_SKIP_REQUIRE_SYSROOT',
    'LL_VULKAN_WIN32_WSI_LOADER',
    'LOGFAIL',
    'LOGTEST',
    'PATH',
    'PYTHON',
    'PYTHONUTF8',
    'TEMP',
    'TMP',
    'TMPDIR',
    'USE_INCREDIBUILD',
    'VULKAN_SDK',
    'VK_ADD_LAYER_PATH',
    'VK_ADD_DRIVER_FILES',
    'VK_ADD_IMPLICIT_LAYER_PATH',
    'VK_DRIVER_FILES',
    'VK_ICD_FILENAMES',
    'VK_IMPLICIT_LAYER_PATH',
    'VK_INSTANCE_LAYERS',
    'VK_LAYER_PATH',
    'VK_LAYER_SETTINGS_PATH',
    'VK_LOADER_DEBUG',
    'VK_LOADER_DRIVERS_DISABLE',
    'VK_LOADER_DRIVERS_SELECT',
    'VK_LOADER_LAYERS_DISABLE',
    'VK_LOADER_LAYERS_ENABLE',
    'additional_packages'
)
$script:SavedEnvironment = @{}
$script:CurrentLog = $null

function Save-ProcessEnvironment {
    foreach ($name in $script:TouchedEnvironment) {
        $item = Get-Item -LiteralPath ("Env:{0}" -f $name) -ErrorAction SilentlyContinue
        $script:SavedEnvironment[$name] = [ordered]@{
            Exists = ($null -ne $item)
            Value = if ($null -ne $item) { $item.Value } else { $null }
        }
    }
}

function Restore-ProcessEnvironment {
    foreach ($name in $script:TouchedEnvironment) {
        $saved = $script:SavedEnvironment[$name]
        if ($saved.Exists) {
            [Environment]::SetEnvironmentVariable($name, [string]$saved.Value, 'Process')
        }
        else {
            [Environment]::SetEnvironmentVariable($name, $null, 'Process')
        }
    }
}

function Clear-ProcessEnvironmentVariable {
    param([Parameter(Mandatory = $true)][string]$Name)
    [Environment]::SetEnvironmentVariable($Name, $null, 'Process')
}

function Get-CanonicalPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    return [IO.Path]::GetFullPath($Path).TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
}

function Test-PathBelow {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Parent
    )
    $prefix = $Parent + [IO.Path]::DirectorySeparatorChar
    return $Path.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)
}

function Add-StepRecord {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Status,
        [string]$Log
    )
    $record = [ordered]@{ Name = $Name; Status = $Status }
    if ($Log) {
        $record.Log = [IO.Path]::GetFileName($Log)
    }
    $script:StepRecords.Add([pscustomobject]$record)
}

function Get-NativeOutput {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$ArgumentList = @(),
        [string]$WorkingDirectory
    )
    $previousPreference = $ErrorActionPreference
    $output = @()
    $exitCode = 1
    try {
        $ErrorActionPreference = 'Continue'
        if ($WorkingDirectory) {
            Push-Location -LiteralPath $WorkingDirectory
        }
        try {
            $output = @(& $FilePath @ArgumentList 2>&1)
            $exitCode = $LASTEXITCODE
        }
        finally {
            if ($WorkingDirectory) {
                Pop-Location
            }
        }
    }
    finally {
        $ErrorActionPreference = $previousPreference
    }
    if ($exitCode -ne 0) {
        throw "Command failed with exit code ${exitCode}: $FilePath $($ArgumentList -join ' ')"
    }
    return (($output | ForEach-Object { $_.ToString() }) -join "`n").Trim()
}

function Invoke-LoggedNative {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$ArgumentList = @(),
        [Parameter(Mandatory = $true)][string]$WorkingDirectory,
        [Parameter(Mandatory = $true)][string]$LogPath
    )
    $script:CurrentStep = $Name
    $script:CurrentLog = $LogPath
    Write-Host "`n[$Name]"
    Write-Host ("{0} {1}" -f $FilePath, ($ArgumentList -join ' '))
    Set-Content -LiteralPath $LogPath -Value '' -Encoding UTF8

    $previousPreference = $ErrorActionPreference
    $exitCode = 1
    try {
        $ErrorActionPreference = 'Continue'
        Push-Location -LiteralPath $WorkingDirectory
        try {
            & $FilePath @ArgumentList 2>&1 |
                Tee-Object -FilePath $LogPath |
                ForEach-Object { Write-Host $_ }
            $exitCode = $LASTEXITCODE
        }
        finally {
            Pop-Location
        }
    }
    finally {
        $ErrorActionPreference = $previousPreference
    }

    if ($exitCode -ne 0) {
        Add-StepRecord -Name $Name -Status 'failed' -Log $LogPath
        throw "Step '$Name' failed with exit code $exitCode."
    }
    Add-StepRecord -Name $Name -Status 'passed' -Log $LogPath
}

function Assert-LogTestCounts {
    param(
        [Parameter(Mandatory = $true)][string]$LogPath,
        [Parameter(Mandatory = $true)][int]$ExpectedTotal,
        [Parameter(Mandatory = $true)][int]$ExpectedPassed
    )
    $text = Get-Content -LiteralPath $LogPath -Raw
    if ($text -notmatch ("Total Tests:\s+{0}\b" -f $ExpectedTotal)) {
        throw "Expected $ExpectedTotal total tests in $([IO.Path]::GetFileName($LogPath))."
    }
    if ($text -notmatch ("Passed Tests:\s+{0}\b" -f $ExpectedPassed)) {
        throw "Expected $ExpectedPassed passed tests in $([IO.Path]::GetFileName($LogPath))."
    }
    if ($text -match 'Failed Tests:\s+[1-9]') {
        throw "A focused test reported a failure in $([IO.Path]::GetFileName($LogPath))."
    }
}

function Assert-FocusedCTestRegistration {
    param(
        [Parameter(Mandatory = $true)][string]$CTest,
        [Parameter(Mandatory = $true)][string]$BuildDirectory,
        [Parameter(Mandatory = $true)][string]$LogPath
    )
    $script:CurrentStep = 'focused CTest registration'
    $script:CurrentLog = $LogPath
    $json = Get-NativeOutput -FilePath $CTest -ArgumentList @('-C', 'Release', '--show-only=json-v1') -WorkingDirectory $BuildDirectory
    Set-Content -LiteralPath $LogPath -Value $json -Encoding UTF8
    $registration = $json | ConvertFrom-Json
    $focusedNames = @(
        $registration.tests |
            ForEach-Object { $_.name } |
            Where-Object { $_ -match '^INTEGRATION_TEST_RUNNER_llwindow(?:win32vulkan|vulkanwin32wsi)$' } |
            Sort-Object
    )
    $expected = @(
        'INTEGRATION_TEST_RUNNER_llwindowvulkanwin32wsi',
        'INTEGRATION_TEST_RUNNER_llwindowwin32vulkan'
    ) | Sort-Object
    if ($focusedNames.Count -ne 2 -or ($focusedNames -join "`n") -ne ($expected -join "`n")) {
        throw 'CTest did not register both exact focused Win32 Vulkan tests.'
    }
    Add-StepRecord -Name 'focused CTest registration' -Status 'passed' -Log $LogPath
}

function Get-CMakeCache {
    param([Parameter(Mandatory = $true)][string]$BuildDirectory)
    $cachePath = Join-Path $BuildDirectory 'CMakeCache.txt'
    if (-not (Test-Path -LiteralPath $cachePath -PathType Leaf)) {
        throw "CMake cache was not generated."
    }
    $values = @{}
    foreach ($line in Get-Content -LiteralPath $cachePath) {
        if ($line -match '^([^/#][^:]*):[^=]+=(.*)$') {
            $values[$matches[1]] = $matches[2]
        }
    }
    return $values
}

function Assert-CacheValue {
    param(
        [Parameter(Mandatory = $true)][hashtable]$Cache,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Expected
    )
    if (-not $Cache.ContainsKey($Name) -or [string]$Cache[$Name] -ne $Expected) {
        $actual = if ($Cache.ContainsKey($Name)) { [string]$Cache[$Name] } else { '<missing>' }
        throw "CMake cache $Name is '$actual', expected '$Expected'."
    }
}

function Find-SingleBuildFile {
    param(
        [Parameter(Mandatory = $true)][string]$BuildDirectory,
        [Parameter(Mandatory = $true)][string]$FileName
    )
    $all = @(Get-ChildItem -LiteralPath $BuildDirectory -Recurse -File -Filter $FileName)
    $release = @($all | Where-Object { $_.FullName -match '[\\/]Release[\\/]' })
    $selected = if ($release.Count -gt 0) { $release } else { $all }
    if ($selected.Count -ne 1) {
        throw "Expected one $FileName output, found $($selected.Count)."
    }
    return $selected[0].FullName
}

function Inspect-FocusedImports {
    param(
        [Parameter(Mandatory = $true)][string]$Dumpbin,
        [Parameter(Mandatory = $true)][string]$HelperLibrary,
        [Parameter(Mandatory = $true)][string[]]$Executables,
        [Parameter(Mandatory = $true)][string]$LogPath
    )
    $script:CurrentStep = 'focused import inspection'
    $script:CurrentLog = $LogPath
    Set-Content -LiteralPath $LogPath -Value '' -Encoding UTF8
    $librarySymbols = Get-NativeOutput -FilePath $Dumpbin -ArgumentList @('/nologo', '/symbols', $HelperLibrary)
    Add-Content -LiteralPath $LogPath -Value "HELPER LIBRARY SYMBOLS`n$librarySymbols`n"
    if ($librarySymbols -cmatch '\b__imp_vk[A-Z][A-Za-z0-9_]*\b') {
        throw 'The helper static library contains a direct Vulkan import symbol.'
    }

    $records = @()
    foreach ($executable in $Executables) {
        $imports = Get-NativeOutput -FilePath $Dumpbin -ArgumentList @('/nologo', '/imports', $executable)
        Add-Content -LiteralPath $LogPath -Value ("EXECUTABLE IMPORTS: {0}`n{1}`n" -f [IO.Path]::GetFileName($executable), $imports)
        if ($imports -match '(?im)^\s+vulkan-1\.dll\s*$' -or
            $imports -cmatch '\bvk[A-Z][A-Za-z0-9_]*\b') {
            throw "$([IO.Path]::GetFileName($executable)) contains a direct Vulkan loader or entry-point import."
        }
        $dependencies = @(
            [regex]::Matches($imports, '(?im)^\s+([A-Za-z0-9_.+-]+\.dll)\s*$') |
                ForEach-Object { $_.Groups[1].Value.ToLowerInvariant() } |
                Sort-Object -Unique
        )
        $records += [pscustomobject][ordered]@{
            File = [IO.Path]::GetFileName($executable)
            Dependencies = $dependencies
            ObservesOpenGL = ($dependencies -contains 'opengl32.dll')
        }
    }
    Add-StepRecord -Name 'focused import inspection' -Status 'passed' -Log $LogPath
    return $records
}

function Inspect-DefaultOffBinaries {
    param(
        [Parameter(Mandatory = $true)][string]$Dumpbin,
        [Parameter(Mandatory = $true)][string[]]$Binaries,
        [Parameter(Mandatory = $true)][string]$LogPath
    )
    $script:CurrentStep = 'default-off binary inspection'
    $script:CurrentLog = $LogPath
    Set-Content -LiteralPath $LogPath -Value '' -Encoding UTF8
    foreach ($binary in $Binaries) {
        $imports = Get-NativeOutput -FilePath $Dumpbin -ArgumentList @('/nologo', '/imports', $binary)
        Add-Content -LiteralPath $LogPath -Value ("BINARY: {0}`n{1}`n" -f [IO.Path]::GetFileName($binary), $imports)
        if ($imports -match '(?im)^\s+vulkan-1\.dll\s*$' -or
            $imports -cmatch '\bvk[A-Z][A-Za-z0-9_]*\b') {
            throw "$([IO.Path]::GetFileName($binary)) gained a direct Vulkan loader or entry-point import."
        }
    }
    Add-StepRecord -Name 'default-off binary inspection' -Status 'passed' -Log $LogPath
}

function Inspect-PackageNames {
    param(
        [Parameter(Mandatory = $true)][string]$BuildDirectory,
        [Parameter(Mandatory = $true)][string]$LogPath
    )
    $script:CurrentStep = 'default-off package-name inspection'
    $script:CurrentLog = $LogPath
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $rendererMaterial = '(?i)(moltenvk|spirv|\.spv$|app_settings[/\\]shaders[/\\]vulkan)'
    $vulkanName = '(?i)vulkan|vk_swiftshader'
    $knownCefPayload = '(?i)(^|/)llplugin/(vulkan-1\.dll|vk_swiftshader\.dll|vk_swiftshader_icd\.json)$'
    $newviewRoot = Join-Path $BuildDirectory 'newview'
    $packageRoots = @(
        (Join-Path $newviewRoot 'Release'),
        (Join-Path $newviewRoot 'Releases')
    ) | Where-Object { Test-Path -LiteralPath $_ -PathType Container }
    $packageFiles = @($packageRoots | ForEach-Object { Get-ChildItem -LiteralPath $_ -Recurse -File })
    $archives = @($packageFiles | Where-Object { $_.Extension -in @('.nupkg', '.zip') })
    $packages = @($packageFiles | Where-Object { $_.Name -like '*_Setup.exe' })
    if ($archives.Count -eq 0 -or $packages.Count -eq 0) {
        throw 'The local Velopack build did not produce both an archive and a setup executable.'
    }
    Set-Content -LiteralPath $LogPath -Value '' -Encoding UTF8
    $knownCefEntries = New-Object System.Collections.Generic.List[string]
    foreach ($item in $packageFiles) {
        $relative = $item.FullName.Substring($newviewRoot.Length).TrimStart('\', '/')
        $normalized = $relative.Replace('\', '/')
        Add-Content -LiteralPath $LogPath -Value ("STAGED: {0}" -f $normalized)
        if ($normalized -match $rendererMaterial) {
            throw "Staged package path '$normalized' contains deferred renderer material."
        }
        if ($normalized -match $vulkanName) {
            if ($normalized -notmatch $knownCefPayload) {
                throw "Staged package path '$normalized' contains an unexpected Vulkan payload."
            }
            $knownCefEntries.Add($normalized)
        }
    }
    foreach ($archive in $archives) {
        $zip = [IO.Compression.ZipFile]::OpenRead($archive.FullName)
        try {
            foreach ($entry in $zip.Entries) {
                Add-Content -LiteralPath $LogPath -Value ("{0}: {1}" -f $archive.Name, $entry.FullName)
                $normalized = $entry.FullName.Replace('\', '/')
                if ($normalized -match $rendererMaterial) {
                    throw "Package entry '$normalized' contains deferred renderer material."
                }
                if ($normalized -match $vulkanName) {
                    if ($normalized -notmatch $knownCefPayload) {
                        throw "Package entry '$normalized' contains an unexpected Vulkan payload."
                    }
                    $knownCefEntries.Add(("{0}: {1}" -f $archive.Name, $normalized))
                }
            }
        }
        finally {
            $zip.Dispose()
        }
    }
    Add-StepRecord -Name 'default-off package-name inspection' -Status 'passed' -Log $LogPath
    return [pscustomobject][ordered]@{
        Archives = @($archives | ForEach-Object { $_.Name } | Sort-Object -Unique)
        Installers = @($packages | ForEach-Object { $_.Name } | Sort-Object -Unique)
        KnownCEFVulkanRuntime = @($knownCefEntries | Sort-Object -Unique)
        UnexpectedRendererMaterial = 0
    }
}

function Protect-SummaryText {
    param([string]$Text)
    if ($null -eq $Text) {
        return $null
    }
    $protected = $Text
    foreach ($replacement in @(
        [pscustomobject]@{ From = $RepositoryRoot; To = '<repository>' },
        [pscustomobject]@{ From = $WorkRoot; To = '<work>' },
        [pscustomobject]@{ From = $ResultRoot; To = '<results>' },
        [pscustomobject]@{ From = $env:USERPROFILE; To = '<profile>' }
    )) {
        if ($replacement.From) {
            $native = [string]$replacement.From
            $variants = @(
                $native,
                $native.Replace('\', '/'),
                $native.Replace('/', '\'),
                $native.Replace('\', '\\')
            ) | Select-Object -Unique | Sort-Object Length -Descending
            foreach ($variant in $variants) {
                $protected = [regex]::Replace(
                    $protected,
                    [regex]::Escape($variant),
                    [string]$replacement.To,
                    [Text.RegularExpressions.RegexOptions]::IgnoreCase)
            }
        }
    }
    return $protected
}

function Remove-OwnedWorkRoot {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$MarkerValue
    )
    $marker = Join-Path $Root '.viewer-stage37-owned'
    if (-not (Test-Path -LiteralPath $marker -PathType Leaf)) {
        throw 'Refusing cleanup because the Stage 37 ownership marker is absent.'
    }
    if ((Get-Content -LiteralPath $marker -Raw).Trim() -ne $MarkerValue) {
        throw 'Refusing cleanup because the Stage 37 ownership marker does not match.'
    }
    Remove-Item -LiteralPath $Root -Recurse -Force
}

Save-ProcessEnvironment

$runId = (Get-Date -Format 'yyyyMMdd-HHmmss') + "-$PID"
if (-not $WorkRoot) {
    $WorkRoot = Join-Path ([IO.Path]::GetTempPath()) ("slv-s37-work-{0}" -f $runId)
}
if (-not $ResultRoot) {
    $ResultRoot = Join-Path ([IO.Path]::GetTempPath()) ("slv-s37-result-{0}" -f $runId)
}

$RepositoryRoot = Get-CanonicalPath $RepositoryRoot
$WorkRoot = Get-CanonicalPath $WorkRoot
$ResultRoot = Get-CanonicalPath $ResultRoot
if (-not [IO.Path]::IsPathRooted($VulkanLoaderPath)) {
    throw 'VulkanLoaderPath must be absolute.'
}
$VulkanLoaderPath = Get-CanonicalPath $VulkanLoaderPath
if ($VulkanSdkRoot) {
    $VulkanSdkRoot = Get-CanonicalPath $VulkanSdkRoot
}

if ($WorkRoot.Length -gt 90) {
    throw 'WorkRoot is too long for a reliable Windows viewer build. Choose a shorter new path.'
}
if ($WorkRoot -eq $ResultRoot -or (Test-PathBelow -Path $ResultRoot -Parent $WorkRoot)) {
    throw 'ResultRoot must be separate from and outside WorkRoot.'
}
if ($WorkRoot -eq $RepositoryRoot -or (Test-PathBelow -Path $WorkRoot -Parent $RepositoryRoot) -or
    $ResultRoot -eq $RepositoryRoot -or (Test-PathBelow -Path $ResultRoot -Parent $RepositoryRoot)) {
    throw 'WorkRoot and ResultRoot must be outside the source checkout.'
}
if (Test-Path -LiteralPath $WorkRoot) {
    throw 'WorkRoot already exists. Choose a new empty path.'
}
if (Test-Path -LiteralPath $ResultRoot) {
    throw 'ResultRoot already exists. Choose a new path.'
}

$markerValue = "viewer-stage37-$runId"

$summary = [ordered]@{
    SchemaVersion = 1
    Stage = 37
    Status = 'running'
    Mode = if ($FocusedOnly) { 'focused-only' } else { 'full' }
    ExpectedCommit = $ExpectedCommit.ToLowerInvariant()
    ActualCommit = $null
    Platform = [ordered]@{}
    Tools = [ordered]@{}
    Vulkan = [ordered]@{}
    Focused = [ordered]@{}
    DefaultOff = [ordered]@{}
    Steps = $script:StepRecords
    Cleanup = 'not-run'
    FailureStep = $null
    Failure = $null
    FailureLog = $null
}

$git = $null
$enabledSource = $null
$disabledSource = $null
$exitStatus = 1

try {
    New-Item -ItemType Directory -Path $ResultRoot | Out-Null
    New-Item -ItemType Directory -Path $WorkRoot | Out-Null
    Set-Content -LiteralPath (Join-Path $WorkRoot '.viewer-stage37-owned') -Value $markerValue -Encoding ASCII
    $processTemp = Join-Path $WorkRoot 'tmp'
    New-Item -ItemType Directory -Path $processTemp | Out-Null
    $env:TEMP = $processTemp
    $env:TMP = $processTemp
    $env:TMPDIR = $processTemp

    $script:CurrentStep = 'host preflight'
    if ($env:OS -ne 'Windows_NT') {
        throw 'This admission runner must execute on Windows.'
    }
    if (-not [Environment]::Is64BitOperatingSystem -or -not [Environment]::Is64BitProcess) {
        throw 'Use 64-bit Windows and 64-bit PowerShell.'
    }
    if ($env:PROCESSOR_ARCHITECTURE -ne 'AMD64') {
        throw 'This stage validates the supported Windows x64 build only.'
    }

    $gitCommand = Get-Command git.exe -ErrorAction Stop
    $cmakeCommand = Get-Command cmake.exe -ErrorAction Stop
    $ctestCommand = Get-Command ctest.exe -ErrorAction Stop
    $pythonLauncher = Get-Command py.exe -ErrorAction Stop
    $git = $gitCommand.Source
    $cmake = $cmakeCommand.Source
    $ctest = $ctestCommand.Source
    $py = $pythonLauncher.Source

    $head = (Get-NativeOutput -FilePath $git -ArgumentList @('-c', 'core.longpaths=true', '-C', $RepositoryRoot, 'rev-parse', 'HEAD')).Trim().ToLowerInvariant()
    if ($head -ne $ExpectedCommit.ToLowerInvariant()) {
        throw "Checkout HEAD is $head, expected $($ExpectedCommit.ToLowerInvariant())."
    }
    $status = Get-NativeOutput -FilePath $git -ArgumentList @('-c', 'core.longpaths=true', '-C', $RepositoryRoot, 'status', '--porcelain=v1', '--untracked-files=all')
    if ($status) {
        throw 'The source checkout must be clean before creating validation clones.'
    }
    $summary.ActualCommit = $head

    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
        throw 'Visual Studio Installer or vswhere.exe is missing.'
    }
    $vsArgs = @('-latest', '-version', '[17.0,18.0)', '-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64')
    $vsInstall = (Get-NativeOutput -FilePath $vswhere -ArgumentList ($vsArgs + @('-property', 'installationPath'))).Trim()
    $vsVersion = (Get-NativeOutput -FilePath $vswhere -ArgumentList ($vsArgs + @('-property', 'catalog_productDisplayVersion'))).Trim()
    if (-not $vsInstall) {
        throw 'Visual Studio 2022 with the x64/x86 C++ tools is missing.'
    }
    $dumpbins = @(Get-ChildItem -LiteralPath (Join-Path $vsInstall 'VC\Tools\MSVC') -Recurse -File -Filter dumpbin.exe |
        Where-Object { $_.FullName -match '[\\/]bin[\\/]Hostx64[\\/]x64[\\/]dumpbin\.exe$' } |
        Sort-Object FullName -Descending)
    if ($dumpbins.Count -eq 0) {
        throw 'The Visual Studio 2022 x64 dumpbin.exe is missing.'
    }
    $dumpbin = $dumpbins[0].FullName
    $compiler = Join-Path (Split-Path -Parent $dumpbin) 'cl.exe'
    if (-not (Test-Path -LiteralPath $compiler -PathType Leaf)) {
        throw 'The Visual Studio 2022 x64 cl.exe is missing beside dumpbin.exe.'
    }

    $sdkIncludeRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Include'
    $windowsSdks = @(Get-ChildItem -LiteralPath $sdkIncludeRoot -Directory -ErrorAction SilentlyContinue |
        Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'um\Windows.h') } |
        Sort-Object { [Version]$_.Name } -Descending)
    if ($windowsSdks.Count -eq 0 -or [Version]$windowsSdks[0].Name -lt [Version]'10.0.22000.0') {
        throw 'Windows SDK 10.0.22000.0 or newer is required.'
    }

    $gitVersion = Get-NativeOutput -FilePath $git -ArgumentList @('--version')
    $cmakeVersion = (Get-NativeOutput -FilePath $cmake -ArgumentList @('--version')).Split("`n")[0]
    if ($cmakeVersion -notmatch 'cmake version\s+(\d+\.\d+(?:\.\d+)?)' -or [Version]$matches[1] -lt [Version]'3.21') {
        throw 'CMake 3.21 or newer is required for the Visual Studio 2022 generator.'
    }
    $pythonVersion = Get-NativeOutput -FilePath $py -ArgumentList @('-3.11', '--version')
    $displayAdapters = @(
        Get-CimInstance -ClassName Win32_VideoController |
            ForEach-Object {
                [pscustomobject][ordered]@{
                    Name = [string]$_.Name
                    DriverVersion = [string]$_.DriverVersion
                }
            } |
            Sort-Object Name, DriverVersion
    )
    if ($displayAdapters.Count -eq 0) {
        throw 'Windows did not report a display adapter and driver identity.'
    }

    $summary.Platform = [ordered]@{
        OS = [Environment]::OSVersion.VersionString
        Architecture = $env:PROCESSOR_ARCHITECTURE
        PowerShell = $PSVersionTable.PSVersion.ToString()
        HighestInstalledWindowsSDK = $windowsSdks[0].Name
        SelectedWindowsSDK = $null
        DisplayAdapters = $displayAdapters
    }
    $summary.Tools = [ordered]@{
        Git = $gitVersion
        CMake = $cmakeVersion
        Python = $pythonVersion
        VisualStudio = $vsVersion
        Compiler = $null
    }

    if (-not $VulkanSdkRoot -or -not (Test-Path -LiteralPath $VulkanSdkRoot -PathType Container)) {
        throw 'VULKAN_SDK is missing or stale. Restart PowerShell after installing the LunarG x64 SDK, or pass -VulkanSdkRoot.'
    }
    if (-not (Test-Path -LiteralPath $VulkanLoaderPath -PathType Leaf)) {
        throw 'The explicit Vulkan loader DLL does not exist.'
    }
    if ($VulkanLoaderPath -match '[^\x00-\x7f]' -or $VulkanSdkRoot -match '[^\x00-\x7f]') {
        throw 'The current native test requires ASCII-only Vulkan SDK and loader paths because it reads the loader path through getenv.'
    }
    $sdkBin = Join-Path $VulkanSdkRoot 'Bin'
    $vulkanInfo = Join-Path $sdkBin 'vulkaninfoSDK.exe'
    $vulkanHeader = Join-Path $VulkanSdkRoot 'Include\vulkan\vulkan.h'
    $validationManifests = @(Get-ChildItem -LiteralPath $sdkBin -File -Filter 'VkLayer_khronos_validation.json' -ErrorAction SilentlyContinue)
    $validationLibraries = @(Get-ChildItem -LiteralPath $sdkBin -File -Filter 'VkLayer_khronos_validation.dll' -ErrorAction SilentlyContinue)
    if (-not (Test-Path -LiteralPath $vulkanInfo -PathType Leaf) -or
        -not (Test-Path -LiteralPath $vulkanHeader -PathType Leaf) -or
        $validationManifests.Count -ne 1 -or $validationLibraries.Count -ne 1) {
        throw 'The LunarG SDK must contain the Vulkan header, vulkaninfoSDK.exe, and one Khronos validation manifest and DLL.'
    }

    $env:VULKAN_SDK = $VulkanSdkRoot
    $env:PATH = $sdkBin + [IO.Path]::PathSeparator + $env:PATH

    foreach ($name in @(
        'VK_ADD_DRIVER_FILES',
        'VK_ADD_IMPLICIT_LAYER_PATH',
        'VK_ADD_LAYER_PATH',
        'VK_DRIVER_FILES',
        'VK_ICD_FILENAMES',
        'VK_IMPLICIT_LAYER_PATH',
        'VK_INSTANCE_LAYERS',
        'VK_LAYER_SETTINGS_PATH',
        'VK_LOADER_DEBUG',
        'VK_LOADER_DRIVERS_DISABLE',
        'VK_LOADER_DRIVERS_SELECT',
        'VK_LOADER_LAYERS_DISABLE',
        'VK_LOADER_LAYERS_ENABLE'
    )) {
        Clear-ProcessEnvironmentVariable $name
    }
    $env:VK_LAYER_PATH = $sdkBin
    $vulkanInfoLog = Join-Path $ResultRoot '01-vulkan-summary.log'
    Invoke-LoggedNative -Name 'Vulkan runtime preflight' -FilePath $vulkanInfo -ArgumentList @('--summary') -WorkingDirectory $ResultRoot -LogPath $vulkanInfoLog
    $vulkanText = Get-Content -LiteralPath $vulkanInfoLog -Raw
    foreach ($requiredVulkanName in @('VK_LAYER_KHRONOS_validation', 'VK_EXT_debug_utils', 'VK_KHR_surface', 'VK_KHR_win32_surface')) {
        if ($vulkanText -notmatch [regex]::Escape($requiredVulkanName)) {
            throw "vulkaninfoSDK did not enumerate $requiredVulkanName."
        }
    }
    if ($vulkanText -notmatch 'Vulkan Instance Version:\s+(\d+)\.(\d+)') {
        throw 'vulkaninfoSDK did not report a Vulkan instance version.'
    }
    $loaderMajor = [int]$matches[1]
    $loaderMinor = [int]$matches[2]
    if ($loaderMajor -lt 1 -or ($loaderMajor -eq 1 -and $loaderMinor -lt 1)) {
        throw 'The Vulkan loader must support Vulkan 1.1 or newer.'
    }
    $summary.Vulkan = [ordered]@{
        SDK = [IO.Path]::GetFileName($VulkanSdkRoot)
        LoaderFile = [IO.Path]::GetFileName($VulkanLoaderPath)
        LoaderVersion = [Diagnostics.FileVersionInfo]::GetVersionInfo($VulkanLoaderPath).FileVersion
        LoaderSHA256 = (Get-FileHash -LiteralPath $VulkanLoaderPath -Algorithm SHA256).Hash.ToLowerInvariant()
        ValidationLayer = 'VK_LAYER_KHRONOS_validation'
        SummaryLog = [IO.Path]::GetFileName($vulkanInfoLog)
    }
    Add-StepRecord -Name 'host preflight' -Status 'passed'

    foreach ($name in @('AUTOBUILD_GITHUB_TOKEN', 'GITHUB_ACTIONS', 'GITHUB_OUTPUT', 'GITHUB_TOKEN', 'LOGTEST', 'VK_ADD_LAYER_PATH', 'additional_packages')) {
        Clear-ProcessEnvironmentVariable $name
    }
    $env:LOGFAIL = 'DEBUG'
    $env:PYTHONUTF8 = '1'
    $env:AUTOBUILD_ADDRSIZE = '64'
    $env:AUTOBUILD_BUILD_ID = '37'
    $env:AUTOBUILD_CONFIGURATION = 'ReleaseOS'
    $env:AUTOBUILD_VCS_INFO = 'true'
    $env:AUTOBUILD_VSVER = '170'
    $env:LL_SKIP_REQUIRE_SYSROOT = '1'
    $env:USE_INCREDIBUILD = '0'

    $supportRoot = Join-Path $WorkRoot 'support'
    $buildVariables = Join-Path $supportRoot 'build-variables'
    New-Item -ItemType Directory -Path $supportRoot | Out-Null
    $cloneLog = Join-Path $ResultRoot '02-build-variables.log'
    Invoke-LoggedNative -Name 'clone public build variables' -FilePath $git -ArgumentList @('-c', 'core.longpaths=true', 'clone', '--depth', '1', 'https://github.com/secondlife/build-variables.git', $buildVariables) -WorkingDirectory $supportRoot -LogPath $cloneLog
    $variablesFile = Join-Path $buildVariables 'variables'
    if (-not (Test-Path -LiteralPath $variablesFile -PathType Leaf)) {
        throw 'The public build-variables checkout does not contain variables.'
    }
    $summary.Tools.BuildVariablesCommit = (Get-NativeOutput -FilePath $git -ArgumentList @('-C', $buildVariables, 'rev-parse', 'HEAD')).Trim().ToLowerInvariant()
    $env:AUTOBUILD_VARIABLES_FILE = $variablesFile
    $env:AUTOBUILD_INSTALLABLE_CACHE = Join-Path $WorkRoot 'cache'

    $venv = Join-Path $WorkRoot 'py'
    $venvLog = Join-Path $ResultRoot '03-python-environment.log'
    Invoke-LoggedNative -Name 'create Python environment' -FilePath $py -ArgumentList @('-3.11', '-m', 'venv', $venv) -WorkingDirectory $WorkRoot -LogPath $venvLog
    $venvPython = Join-Path $venv 'Scripts\python.exe'
    $autobuild = Join-Path $venv 'Scripts\autobuild.exe'
    $pipLog = Join-Path $ResultRoot '04-python-packages.log'
    Invoke-LoggedNative -Name 'install Autobuild and LLSD' -FilePath $venvPython -ArgumentList @('-m', 'pip', 'install', '--disable-pip-version-check', '--no-cache-dir', 'autobuild', 'llsd') -WorkingDirectory $WorkRoot -LogPath $pipLog
    if (-not (Test-Path -LiteralPath $autobuild -PathType Leaf)) {
        throw 'The isolated Autobuild executable was not installed.'
    }
    $env:PYTHON = $venvPython
    $summary.Tools.Autobuild = Get-NativeOutput -FilePath $autobuild -ArgumentList @('--version')

    $enabledSource = Join-Path $WorkRoot 'on'
    $disabledSource = Join-Path $WorkRoot 'off'
    $sourceCloneLog = Join-Path $ResultRoot '05-enabled-source-clone.log'
    Invoke-LoggedNative -Name 'create enabled source clone' -FilePath $git -ArgumentList @('-c', 'core.longpaths=true', 'clone', '--shared', '--no-tags', $RepositoryRoot, $enabledSource) -WorkingDirectory $WorkRoot -LogPath $sourceCloneLog
    $enabledCloneHead = (Get-NativeOutput -FilePath $git -ArgumentList @('-C', $enabledSource, 'rev-parse', 'HEAD')).Trim().ToLowerInvariant()
    if ($enabledCloneHead -ne $ExpectedCommit.ToLowerInvariant()) {
        throw 'The enabled source clone did not check out the expected commit.'
    }

    $enabledArguments = @(
        '-DLL_TESTS:BOOL=ON',
        '-DLL_RENDER_BENCHMARK:BOOL=OFF',
        '-DLL_VULKAN_RUNTIME_TEST:BOOL=ON',
        '-DLL_VULKAN_TONEMAP_TEST:BOOL=OFF',
        '-DLL_VULKAN_SDL_WSI:BOOL=OFF',
        '-DLL_VULKAN_MACOS_WSI:BOOL=OFF',
        '-DLL_VULKAN_WIN32_WSI:BOOL=ON',
        '-DPACKAGE:BOOL=OFF',
        '-DVS_DISABLE_FATAL_WARNINGS:BOOL=OFF',
        '-DINSTALL_PROPRIETARY:BOOL=FALSE',
        '-DUSE_KDU:BOOL=FALSE',
        '-DUSE_OPENAL:BOOL=ON',
        '-DUSE_VELOPACK:BOOL=OFF',
        '-DUNATTENDED:BOOL=ON',
        '-DENABLE_SIGNING:BOOL=OFF',
        '-DUSE_BUGSPLAT:BOOL=OFF',
        '-DVIEWER_CHANNEL:STRING=Second Life Test'
    )
    $enabledConfigureLog = Join-Path $ResultRoot '06-enabled-configure.log'
    Invoke-LoggedNative -Name 'configure focused Windows graph' -FilePath $autobuild -ArgumentList (@('configure', '-c', 'ReleaseOS', '-A64', '--') + $enabledArguments) -WorkingDirectory $enabledSource -LogPath $enabledConfigureLog
    $enabledBuild = Join-Path $enabledSource 'build-vc170-64'
    $enabledCache = Get-CMakeCache $enabledBuild
    Assert-CacheValue $enabledCache 'CMAKE_GENERATOR' 'Visual Studio 17 2022'
    Assert-CacheValue $enabledCache 'CMAKE_GENERATOR_PLATFORM' 'x64'
    Assert-CacheValue $enabledCache 'LL_TESTS' 'ON'
    Assert-CacheValue $enabledCache 'LL_RENDER_BENCHMARK' 'OFF'
    Assert-CacheValue $enabledCache 'LL_VULKAN_RUNTIME_TEST' 'ON'
    Assert-CacheValue $enabledCache 'LL_VULKAN_TONEMAP_TEST' 'OFF'
    Assert-CacheValue $enabledCache 'LL_VULKAN_SDL_WSI' 'OFF'
    Assert-CacheValue $enabledCache 'LL_VULKAN_MACOS_WSI' 'OFF'
    Assert-CacheValue $enabledCache 'LL_VULKAN_WIN32_WSI' 'ON'
    Assert-CacheValue $enabledCache 'PACKAGE' 'OFF'
    Assert-CacheValue $enabledCache 'VS_DISABLE_FATAL_WARNINGS' 'OFF'

    if (-not $enabledCache.ContainsKey('CMAKE_GENERATOR_INSTANCE') -or
        -not [string]$enabledCache['CMAKE_GENERATOR_INSTANCE']) {
        throw 'CMake did not record the selected Visual Studio instance.'
    }
    $configuredVisualStudio = Get-CanonicalPath ([string]$enabledCache['CMAKE_GENERATOR_INSTANCE'])
    if (-not $configuredVisualStudio.Equals((Get-CanonicalPath $vsInstall), [StringComparison]::OrdinalIgnoreCase)) {
        throw 'CMake selected a different Visual Studio instance than the inspected MSVC tools.'
    }
    $enabledConfigureText = Get-Content -LiteralPath $enabledConfigureLog -Raw
    if ($enabledConfigureText -notmatch 'CXX compiler identification is MSVC\s+([0-9.]+)') {
        throw 'The fresh CMake configure did not report its selected MSVC compiler version.'
    }
    $selectedCompilerVersion = $matches[1]
    $zeroCheckProject = Join-Path $enabledBuild 'ZERO_CHECK.vcxproj'
    if (-not (Test-Path -LiteralPath $zeroCheckProject -PathType Leaf)) {
        throw 'The generated Visual Studio graph does not contain ZERO_CHECK.vcxproj.'
    }
    $zeroCheckText = Get-Content -LiteralPath $zeroCheckProject -Raw
    if ($zeroCheckText -notmatch '<PlatformToolset>([^<]+)</PlatformToolset>') {
        throw 'The generated Visual Studio graph does not identify its platform toolset.'
    }
    $selectedPlatformToolset = $matches[1]
    if ($selectedPlatformToolset -ne 'v143') {
        throw "CMake selected platform toolset '$selectedPlatformToolset', expected 'v143'."
    }
    if ($zeroCheckText -notmatch '<WindowsTargetPlatformVersion>([^<]+)</WindowsTargetPlatformVersion>') {
        throw 'The generated Visual Studio graph does not identify its Windows SDK.'
    }
    $selectedWindowsSdk = $matches[1]
    if ([Version]$selectedWindowsSdk -lt [Version]'10.0.22000.0') {
        throw "CMake selected Windows SDK '$selectedWindowsSdk', which is too old."
    }
    $summary.Platform.SelectedWindowsSDK = $selectedWindowsSdk
    $summary.Tools.Compiler = [ordered]@{
        Family = 'MSVC'
        CMakeVersion = $selectedCompilerVersion
        PlatformToolset = $selectedPlatformToolset
    }

    $configuredCMake = Get-CanonicalPath ([string]$enabledCache['CMAKE_COMMAND'])
    if (-not $configuredCMake.Equals((Get-CanonicalPath $cmake), [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Autobuild configured with a different CMake executable than the preflight command.'
    }
    $ctest = Join-Path (Split-Path -Parent $configuredCMake) 'ctest.exe'
    if (-not (Test-Path -LiteralPath $ctest -PathType Leaf)) {
        throw 'The CTest executable beside the configured CMake is missing.'
    }

    $focusedBuildLog = Join-Path $ResultRoot '07-focused-build.log'
    Invoke-LoggedNative -Name 'build focused Windows targets' -FilePath $cmake -ArgumentList @('--build', $enabledBuild, '--config', 'Release', '--parallel', [Environment]::ProcessorCount, '--target', 'INTEGRATION_TEST_llwindowwin32vulkan', 'INTEGRATION_TEST_llwindowvulkanwin32wsi') -WorkingDirectory $enabledSource -LogPath $focusedBuildLog

    $ctestRegistrationLog = Join-Path $ResultRoot '07-focused-ctest-registration.json'
    Assert-FocusedCTestRegistration -CTest $ctest -BuildDirectory $enabledBuild -LogPath $ctestRegistrationLog

    Clear-ProcessEnvironmentVariable 'LL_RUN_VULKAN_WIN32_WSI_NATIVE'
    Clear-ProcessEnvironmentVariable 'LL_VULKAN_WIN32_WSI_LOADER'
    $injectedLog = Join-Path $ResultRoot '08-injected-test.log'
    Invoke-LoggedNative -Name 'run injected ownership test' -FilePath $ctest -ArgumentList @('-C', 'Release', '--output-on-failure', '-V', '-R', '^INTEGRATION_TEST_RUNNER_llwindowwin32vulkan$') -WorkingDirectory $enabledBuild -LogPath $injectedLog
    Assert-LogTestCounts -LogPath $injectedLog -ExpectedTotal 12 -ExpectedPassed 12

    $nativeOptOutLog = Join-Path $ResultRoot '09-native-opt-out.log'
    Invoke-LoggedNative -Name 'run native test opt-out' -FilePath $ctest -ArgumentList @('-C', 'Release', '--output-on-failure', '-V', '-R', '^INTEGRATION_TEST_RUNNER_llwindowvulkanwin32wsi$') -WorkingDirectory $enabledBuild -LogPath $nativeOptOutLog
    Assert-LogTestCounts -LogPath $nativeOptOutLog -ExpectedTotal 1 -ExpectedPassed 1

    $env:LL_RUN_VULKAN_WIN32_WSI_NATIVE = '1'
    $env:LL_VULKAN_WIN32_WSI_LOADER = $VulkanLoaderPath
    $nativeOptInLog = Join-Path $ResultRoot '10-native-opt-in.log'
    Invoke-LoggedNative -Name 'run native Win32 Vulkan test' -FilePath $ctest -ArgumentList @('-C', 'Release', '--output-on-failure', '-V', '-R', '^INTEGRATION_TEST_RUNNER_llwindowvulkanwin32wsi$') -WorkingDirectory $enabledBuild -LogPath $nativeOptInLog
    Assert-LogTestCounts -LogPath $nativeOptInLog -ExpectedTotal 1 -ExpectedPassed 1
    Clear-ProcessEnvironmentVariable 'LL_RUN_VULKAN_WIN32_WSI_NATIVE'
    Clear-ProcessEnvironmentVariable 'LL_VULKAN_WIN32_WSI_LOADER'

    $injectedExe = Find-SingleBuildFile -BuildDirectory $enabledBuild -FileName 'INTEGRATION_TEST_llwindowwin32vulkan.exe'
    $nativeExe = Find-SingleBuildFile -BuildDirectory $enabledBuild -FileName 'INTEGRATION_TEST_llwindowvulkanwin32wsi.exe'
    $helperLibrary = Find-SingleBuildFile -BuildDirectory $enabledBuild -FileName 'llwindowwin32vulkan.lib'
    $focusedImportsLog = Join-Path $ResultRoot '11-focused-imports.log'
    $focusedImports = Inspect-FocusedImports -Dumpbin $dumpbin -HelperLibrary $helperLibrary -Executables @($injectedExe, $nativeExe) -LogPath $focusedImportsLog
    $summary.Focused = [ordered]@{
        Injected = [ordered]@{ Total = 12; Passed = 12; Log = [IO.Path]::GetFileName($injectedLog) }
        NativeOptOut = [ordered]@{ Total = 1; Passed = 1; Log = [IO.Path]::GetFileName($nativeOptOutLog) }
        NativeOptIn = [ordered]@{ Total = 1; Passed = 1; Log = [IO.Path]::GetFileName($nativeOptInLog) }
        RequiredValidation = 'passed by native test assertion'
        ValidationMessages = 0
        ClientExtent = '1280x720'
        Imports = $focusedImports
    }

    if ($FocusedOnly) {
        $summary.Status = 'focused-pass'
        $exitStatus = 0
    }
    else {
        $script:CurrentStep = 'default-off package preflight'
        $script:CurrentLog = $null
        $dotnetCommand = Get-Command dotnet.exe -ErrorAction Stop
        $dotnet = $dotnetCommand.Source
        $dotnetSdks = Get-NativeOutput -FilePath $dotnet -ArgumentList @('--list-sdks')
        if ($dotnetSdks -notmatch '(?m)^9\.') {
            throw 'The full package proof requires a .NET 9 SDK.'
        }
        $dotnetTools = Join-Path $env:USERPROFILE '.dotnet\tools'
        $env:PATH = $dotnetTools + [IO.Path]::PathSeparator + $env:PATH
        $vpkCommand = Get-Command vpk.exe -ErrorAction Stop
        $vpk = $vpkCommand.Source
        $summary.Tools.DotNetSDKs = @(
            $dotnetSdks -split "`n" |
                ForEach-Object {
                    if ($_ -match '^([^\s]+)\s+\[') {
                        $matches[1]
                    }
                }
        )
        $summary.Tools.Velopack = Get-NativeOutput -FilePath $vpk -ArgumentList @('--version')

        $disabledSourceCloneLog = Join-Path $ResultRoot '12-default-off-source-clone.log'
        Invoke-LoggedNative -Name 'create default-off source clone' -FilePath $git -ArgumentList @('-c', 'core.longpaths=true', 'clone', '--shared', '--no-tags', $RepositoryRoot, $disabledSource) -WorkingDirectory $WorkRoot -LogPath $disabledSourceCloneLog
        $disabledCloneHead = (Get-NativeOutput -FilePath $git -ArgumentList @('-C', $disabledSource, 'rev-parse', 'HEAD')).Trim().ToLowerInvariant()
        if ($disabledCloneHead -ne $ExpectedCommit.ToLowerInvariant()) {
            throw 'The default-off source clone did not check out the expected commit.'
        }

        $disabledArguments = @(
            '-DLL_TESTS:BOOL=ON',
            '-DLL_RENDER_BENCHMARK:BOOL=OFF',
            '-DLL_VULKAN_RUNTIME_TEST:BOOL=OFF',
            '-DLL_VULKAN_TONEMAP_TEST:BOOL=OFF',
            '-DLL_VULKAN_SDL_WSI:BOOL=OFF',
            '-DLL_VULKAN_MACOS_WSI:BOOL=OFF',
            '-DLL_VULKAN_WIN32_WSI:BOOL=OFF',
            '-DPACKAGE:BOOL=ON',
            '-DVS_DISABLE_FATAL_WARNINGS:BOOL=OFF',
            '-DINSTALL_PROPRIETARY:BOOL=FALSE',
            '-DUSE_KDU:BOOL=FALSE',
            '-DUSE_OPENAL:BOOL=ON',
            '-DUSE_VELOPACK:BOOL=ON',
            '-DUNATTENDED:BOOL=ON',
            '-DENABLE_SIGNING:BOOL=OFF',
            '-DUSE_BUGSPLAT:BOOL=OFF',
            '-DVIEWER_CHANNEL:STRING=Second Life Test'
        )
        $disabledConfigureLog = Join-Path $ResultRoot '13-default-off-configure.log'
        Invoke-LoggedNative -Name 'configure default-off Windows graph' -FilePath $autobuild -ArgumentList (@('configure', '-c', 'ReleaseOS', '-A64', '--') + $disabledArguments) -WorkingDirectory $disabledSource -LogPath $disabledConfigureLog
        $disabledBuild = Join-Path $disabledSource 'build-vc170-64'
        $disabledCache = Get-CMakeCache $disabledBuild
        Assert-CacheValue $disabledCache 'CMAKE_GENERATOR' 'Visual Studio 17 2022'
        Assert-CacheValue $disabledCache 'CMAKE_GENERATOR_PLATFORM' 'x64'
        Assert-CacheValue $disabledCache 'LL_TESTS' 'ON'
        foreach ($option in @('LL_RENDER_BENCHMARK', 'LL_VULKAN_RUNTIME_TEST', 'LL_VULKAN_TONEMAP_TEST', 'LL_VULKAN_SDL_WSI', 'LL_VULKAN_MACOS_WSI', 'LL_VULKAN_WIN32_WSI')) {
            Assert-CacheValue $disabledCache $option 'OFF'
        }
        Assert-CacheValue $disabledCache 'PACKAGE' 'ON'
        Assert-CacheValue $disabledCache 'VS_DISABLE_FATAL_WARNINGS' 'OFF'
        Assert-CacheValue $disabledCache 'CMAKE_GENERATOR_INSTANCE' ([string]$enabledCache['CMAKE_GENERATOR_INSTANCE'])
        $disabledZeroCheckProject = Join-Path $disabledBuild 'ZERO_CHECK.vcxproj'
        if (-not (Test-Path -LiteralPath $disabledZeroCheckProject -PathType Leaf)) {
            throw 'The default-off Visual Studio graph does not contain ZERO_CHECK.vcxproj.'
        }
        $disabledZeroCheckText = Get-Content -LiteralPath $disabledZeroCheckProject -Raw
        if ($disabledZeroCheckText -notmatch '<PlatformToolset>([^<]+)</PlatformToolset>' -or
            $matches[1] -ne $selectedPlatformToolset) {
            throw 'The default-off graph selected a different Visual Studio platform toolset.'
        }
        if ($disabledZeroCheckText -notmatch '<WindowsTargetPlatformVersion>([^<]+)</WindowsTargetPlatformVersion>' -or
            $matches[1] -ne $selectedWindowsSdk) {
            throw 'The default-off graph selected a different Windows SDK.'
        }

        $forbiddenGraphFiles = @(Get-ChildItem -LiteralPath $disabledBuild -Recurse -File | Where-Object { $_.Name -match '(?i)llwindowwin32vulkan|llwindowvulkanwin32wsi' })
        if ($forbiddenGraphFiles.Count -ne 0) {
            throw 'The default-off generated graph contains a Win32 Vulkan diagnostic target or object.'
        }

        $neutralBuildLog = Join-Path $ResultRoot '14-default-off-neutral-build.log'
        Invoke-LoggedNative -Name 'build default-off neutral tests' -FilePath $cmake -ArgumentList @('--build', $disabledBuild, '--config', 'Release', '--parallel', [Environment]::ProcessorCount, '--target', 'INTEGRATION_TEST_llwindowgraphicsapi', 'INTEGRATION_TEST_llwindowvulkanrequirements') -WorkingDirectory $disabledSource -LogPath $neutralBuildLog
        $graphicsApiLog = Join-Path $ResultRoot '15-default-off-graphics-api.log'
        Invoke-LoggedNative -Name 'run default-off graphics API test' -FilePath $ctest -ArgumentList @('-C', 'Release', '--output-on-failure', '-V', '-R', '^INTEGRATION_TEST_RUNNER_llwindowgraphicsapi$') -WorkingDirectory $disabledBuild -LogPath $graphicsApiLog
        Assert-LogTestCounts -LogPath $graphicsApiLog -ExpectedTotal 3 -ExpectedPassed 3
        $requirementsLog = Join-Path $ResultRoot '16-default-off-requirements.log'
        Invoke-LoggedNative -Name 'run default-off requirements test' -FilePath $ctest -ArgumentList @('-C', 'Release', '--output-on-failure', '-V', '-R', '^INTEGRATION_TEST_RUNNER_llwindowvulkanrequirements$') -WorkingDirectory $disabledBuild -LogPath $requirementsLog
        Assert-LogTestCounts -LogPath $requirementsLog -ExpectedTotal 7 -ExpectedPassed 7

        $packageBuildLog = Join-Path $ResultRoot '17-default-off-viewer-package.log'
        Invoke-LoggedNative -Name 'build default-off viewer and package' -FilePath $cmake -ArgumentList @('--build', $disabledBuild, '--config', 'Release', '--parallel', [Environment]::ProcessorCount, '--target', 'viewer', 'llpackage') -WorkingDirectory $disabledSource -LogPath $packageBuildLog

        $viewerExe = Find-SingleBuildFile -BuildDirectory $disabledBuild -FileName 'secondlife-bin.exe'
        $stagedViewerExe = Find-SingleBuildFile -BuildDirectory $disabledBuild -FileName 'SecondLifeViewer.exe'
        $binaryRoots = @(
            (Join-Path $disabledBuild 'newview\Release'),
            (Join-Path $disabledBuild 'sharedlibs\Release')
        )
        $stagedBinaries = @($binaryRoots | Where-Object { Test-Path -LiteralPath $_ -PathType Container } |
            ForEach-Object { Get-ChildItem -LiteralPath $_ -Recurse -File } |
            Where-Object { $_.Extension -in @('.exe', '.dll') } |
            Select-Object -ExpandProperty FullName -Unique)
        if ($stagedBinaries.Count -eq 0 -or $stagedBinaries -notcontains $viewerExe -or $stagedBinaries -notcontains $stagedViewerExe) {
            throw 'No default-off packaged Windows binaries were found for import inspection.'
        }
        $defaultImportsLog = Join-Path $ResultRoot '18-default-off-imports.log'
        Inspect-DefaultOffBinaries -Dumpbin $dumpbin -Binaries $stagedBinaries -LogPath $defaultImportsLog
        $packageNamesLog = Join-Path $ResultRoot '19-default-off-package-names.log'
        $packageEvidence = Inspect-PackageNames -BuildDirectory $disabledBuild -LogPath $packageNamesLog

        $script:CurrentStep = 'runner source isolation'
        $script:CurrentLog = $null
        $stageFiles = @(
            (Get-NativeOutput -FilePath $git -ArgumentList @('-C', $RepositoryRoot, 'diff', '--name-only', "$ExpectedCommit^", $ExpectedCommit)) -split "`n" |
                Where-Object { $_ } |
                Sort-Object
        )
        $expectedStageFiles = @(
            'doc/performance/renderer-stage-37-windows-runner.md',
            'scripts/perf/validate_win32_vulkan.ps1'
        ) | Sort-Object
        if (($stageFiles -join "`n") -ne ($expectedStageFiles -join "`n")) {
            throw 'The runner commit must change exactly the Windows runner and its dependency guide.'
        }
        $summary.DefaultOff = [ordered]@{
            ExperimentalOptions = 'all six off'
            Win32DiagnosticGraphFiles = 0
            GraphicsAPI = [ordered]@{ Total = 3; Passed = 3; Log = [IO.Path]::GetFileName($graphicsApiLog) }
            Requirements = [ordered]@{ Total = 7; Passed = 7; Log = [IO.Path]::GetFileName($requirementsLog) }
            InspectedBinaries = $stagedBinaries.Count
            VulkanLoaderDependencies = 0
            DirectVulkanImports = 0
            Packages = $packageEvidence
            RunnerCommitFiles = $stageFiles
            ViewerOrPackageLaunched = $false
        }
        $summary.Status = 'pass'
        $exitStatus = 0
    }
}
catch {
    $summary.Status = 'failed'
    $summary.FailureStep = $script:CurrentStep
    $summary.Failure = Protect-SummaryText $_.Exception.Message
    $recordedFailure = @(
        $script:StepRecords |
            Where-Object { $_.Name -eq $script:CurrentStep -and $_.Status -eq 'failed' }
    )
    if ($recordedFailure.Count -eq 0) {
        Add-StepRecord -Name $script:CurrentStep -Status 'failed' -Log $script:CurrentLog
    }
    Write-Error ("Stage 37 runner failed in '{0}': {1}" -f $script:CurrentStep, $_.Exception.Message) -ErrorAction Continue
}
finally {
    try {
        if ($KeepWorkRoot -and (Test-Path -LiteralPath $WorkRoot -PathType Container)) {
            $summary.Cleanup = 'kept by request'
            Write-Warning "Disposable work root retained at: $WorkRoot"
            Write-Warning 'Remove that directory manually after debugging.'
            if ($exitStatus -eq 0) {
                $summary.Status = 'incomplete'
                $summary.FailureStep = 'cleanup'
                $summary.Failure = 'KeepWorkRoot retained disposable validation state. Remove it before treating the run as complete.'
                $exitStatus = 1
            }
        }
        elseif ($KeepWorkRoot) {
            $summary.Cleanup = 'work root was not created'
        }
        elseif (Test-Path -LiteralPath $WorkRoot -PathType Container) {
            Remove-OwnedWorkRoot -Root $WorkRoot -MarkerValue $markerValue
            $summary.Cleanup = 'disposable clones, builds, packages, dependencies, and Python environment removed'
        }
        else {
            $summary.Cleanup = 'work root was not created'
        }
    }
    catch {
        $summary.Cleanup = Protect-SummaryText ("cleanup failed: {0}" -f $_.Exception.Message)
        if ($exitStatus -eq 0) {
            $script:CurrentStep = 'cleanup'
            $script:CurrentLog = $null
            $summary.Status = 'failed'
            $summary.FailureStep = 'cleanup'
            $summary.Failure = $summary.Cleanup
            Add-StepRecord -Name 'cleanup' -Status 'failed'
            $exitStatus = 1
        }
    }

    Restore-ProcessEnvironment
    $summary.Steps = $script:StepRecords
    if (Test-Path -LiteralPath $ResultRoot -PathType Container) {
        if ($summary.Status -eq 'failed' -and $script:CurrentLog -and
            (Test-Path -LiteralPath $script:CurrentLog -PathType Leaf)) {
            $sanitizedFailurePath = Join-Path $ResultRoot 'stage37-failure.log'
            $sanitizedFailure = Protect-SummaryText (Get-Content -LiteralPath $script:CurrentLog -Raw)
            Set-Content -LiteralPath $sanitizedFailurePath -Value $sanitizedFailure -Encoding UTF8
            $summary.FailureLog = [IO.Path]::GetFileName($sanitizedFailurePath)
        }
        $summaryPath = Join-Path $ResultRoot 'stage37-summary.json'
        $summary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $summaryPath -Encoding UTF8
        Write-Host "`nStage 37 result: $($summary.Status)"
        Write-Host "Summary: $summaryPath"
        Write-Host 'Send stage37-summary.json. If FailureLog is set, also send that sanitized log.'
    }
}

exit $exitStatus
