<#
.SYNOPSIS
    Build Ormeq with MSVC.

.DESCRIPTION
    Finds CMake (on PATH, or the copy bundled with Visual Studio), configures the
    build directory if it does not exist yet, and builds.

    Each frontend gets its own build directory and its own executable name, so
    building one never clobbers the other:

        win32 -> out/       -> ormeq.exe        (upstream's Win32 port)
        sdl3  -> out-sdl3/  -> ormeq-sdl3.exe   (ours)

.EXAMPLE
    .\build.ps1                 # builds the SDL3 frontend
    .\build.ps1 -Frontend win32 # builds the win32 baseline
    .\build.ps1 -Run            # build, then launch it
    .\build.ps1 -Fresh          # throw the build directory away first
    .\build.ps1 -NoCheck        # skip the data checks
    .\build.ps1 -Test           # also build headless and run gameplay regressions

.NOTES
    The data checks run before the compile, because a broken reference is
    not a compile error -- it is a quest that hands out nothing, or a
    portrait that silently draws a letter instead. Every one of them
    exists because something failed quietly and nobody noticed for weeks.
    See tools/check_public.py for data checks and tools/test_game.py for gameplay tests.
#>
param(
    [ValidateSet('sdl3', 'win32', 'none')] [string] $Frontend = 'sdl3',
    [ValidateSet('Release', 'Debug')] [string] $Config = 'Release',
    [switch] $Run,
    [switch] $Fresh,
    [switch] $Wizard,
    [switch] $NoCheck,
    [switch] $Test
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot

# ---------------------------------------------------------------- find cmake
$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) {
    $pattern = 'C:\Program Files\Microsoft Visual Studio\*\*\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    $cmake = (Get-Item $pattern -Force -ErrorAction SilentlyContinue | Select-Object -First 1).FullName
}
if (-not $cmake) {
    throw "Could not find cmake.exe, on PATH or inside Visual Studio. Install CMake, or add VS's copy to PATH."
}

# ------------------------------------------------------------------- build it
if ($Frontend -eq 'sdl3') {
    $buildDir = Join-Path $root 'out-sdl3'
    $exe      = Join-Path $root 'ormeq-sdl3.exe'
} elseif ($Frontend -eq 'none') {
    $buildDir = Join-Path $root 'out-none'
    $exe      = Join-Path $root 'ormeq-none.exe'
} else {
    $buildDir = Join-Path $root 'out'
    $exe      = Join-Path $root 'ormeq.exe'
}

# CMake caches absolute source, dependency and output paths. Recreate a moved
# build directory; configuring over its old cache cannot repair those paths.
$cache = Join-Path $buildDir 'CMakeCache.txt'
$recreate = [bool]$Fresh
if (Test-Path -LiteralPath $cache) {
    $sourceLine = Select-String -LiteralPath $cache -Pattern '^CMAKE_HOME_DIRECTORY:' | Select-Object -First 1
    $binaryLine = Select-String -LiteralPath $cache -Pattern '^CMAKE_CACHEFILE_DIR:' | Select-Object -First 1
    $sourcePath = if ($sourceLine) { ($sourceLine.Line -split '=', 2)[1] } else { '' }
    $binaryPath = if ($binaryLine) { ($binaryLine.Line -split '=', 2)[1] } else { '' }
    if (($sourcePath -replace '/', '\') -ine $root -or
        ($binaryPath -replace '/', '\') -ine $buildDir) {
        Write-Host 'Project moved; recreating the CMake build directory.' -ForegroundColor Cyan
        $recreate = $true
    }
}
if ($recreate -and (Test-Path -LiteralPath $buildDir)) {
    # Only the selected, direct child build directory may be removed.
    $resolvedRoot = (Resolve-Path -LiteralPath $root).Path
    $resolvedBuild = (Resolve-Path -LiteralPath $buildDir).Path
    $item = Get-Item -LiteralPath $resolvedBuild
    if ((Split-Path -Parent $resolvedBuild) -ine $resolvedRoot -or
        $item.Name -notin @('out', 'out-sdl3', 'out-none') -or
        ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "Refusing to remove unexpected build directory: $resolvedBuild"
    }
    Remove-Item -LiteralPath $resolvedBuild -Recurse -Force
}

# Configure on every build, so options and source-list changes cannot be stale.
$wizFlag = if ($Wizard) { 'ON' } else { 'OFF' }
Write-Host "Configuring $Frontend build in $buildDir" -ForegroundColor Cyan
& $cmake -S $root -B $buildDir -D "FROG_FRONTEND=$Frontend" -D "FROG_WIZARD=$wizFlag"
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }

# ------------------------------------------------------------- check first
# Before the compile, not after: if the data is broken there is no point
# waiting for a link to find out.
if (-not $NoCheck) {
    $py = (Get-Command python -ErrorAction SilentlyContinue).Source
    if ($py) {
        Write-Host "Checking the data" -ForegroundColor Cyan
        $check = Join-Path $root 'tools\check_all.py'
        if (-not (Test-Path -LiteralPath $check)) {
            $check = Join-Path $root 'tools\check_public.py'
        }
        & $py $check
        if ($LASTEXITCODE -ne 0) {
            throw "Data checks failed. Fix them, or build with -NoCheck."
        }
    } else {
        Write-Host "python not found; skipping the data checks" -ForegroundColor DarkYellow
    }
}

Write-Host "Building $Frontend ($Config)" -ForegroundColor Cyan
& $cmake --build $buildDir --config $Config
if ($LASTEXITCODE -ne 0) { throw "Build failed." }

Write-Host "Built $exe" -ForegroundColor Green

if ($Test) {
    if ($Frontend -ne 'none') {
        & $PSCommandPath -Frontend none -Config $Config -Wizard:$Wizard -NoCheck
        if (-not $?) { throw 'Headless build failed.' }
    }
    $py = (Get-Command python -ErrorAction SilentlyContinue).Source
    if (-not $py) { throw 'Python is required for -Test.' }
    if ($Frontend -eq 'sdl3') {
        & $py (Join-Path $root 'tools\test_sound.py') (Join-Path $buildDir "$Config\ormeq-sound-tests.exe")
        if ($LASTEXITCODE -ne 0) { throw 'Sound regression tests failed.' }
        & $py (Join-Path $root 'tools\test_tiles.py') (Join-Path $buildDir "$Config\ormeq-tile-tests.exe")
        if ($LASTEXITCODE -ne 0) { throw 'Tile regression tests failed.' }
        & $py (Join-Path $root 'tools\test_map_ui.py')
        if ($LASTEXITCODE -ne 0) { throw 'Map rendering regression tests failed.' }
        & $py (Join-Path $root 'tools\test_quest_ui.py')
        if ($LASTEXITCODE -ne 0) { throw 'Quest journal rendering tests failed.' }
        & $py (Join-Path $root 'tools\test_menu_ui.py')
        if ($LASTEXITCODE -ne 0) { throw 'Menu first-input tests failed.' }
    }
    & $py (Join-Path $root 'tools\test_game.py')
    if ($LASTEXITCODE -ne 0) { throw 'Gameplay regression tests failed.' }
}

if ($Run) {
    # The game locates lib/ relative to its own executable, so start it here.
    Start-Process -FilePath $exe -WorkingDirectory $root
}
