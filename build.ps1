[CmdletBinding()]
param(
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release",
    [string]$JuceDir = "",   # a clean git checkout of JUCE 8.0.4; found automatically if omitted
    [string]$BuildDir = "",
    [switch]$Clean,
    [switch]$Test,       # run AnnealTests and the host check after building (and the setup test with -Setup)
    [switch]$Snapshot,   # render editor PNGs into .\snapshots
    [switch]$Setup,      # build dist\Anneal-<version>-Setup.exe (Inno Setup 6)
    [switch]$Install,       # run the setup silently just for you: %LOCALAPPDATA%\Programs\Common\VST3, no admin prompt. Implies -Setup
    [switch]$InstallSystem  # run the setup silently for all users: C:\Program Files\Common Files\VST3 (admin prompt). Implies -Setup
)
if ($Install -or $InstallSystem) { $Setup = $true }

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$logDir = Join-Path $root "build-logs"
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$logFile = Join-Path $logDir ("build-{0}.log" -f (Get-Date -Format "yyyyMMdd-HHmmss"))

function Write-Log([string]$Message, [string]$Level = "INFO") {
    $line = "[{0}] [{1}] {2}" -f (Get-Date -Format "HH:mm:ss"), $Level, $Message
    Write-Host $line
    Add-Content -Path $logFile -Value $line
}

# MSBuild's FileTracker cannot write .tlog files past 260 characters (error FTK1011) and JUCE's
# intermediate paths are deep, so a project in a long folder builds out-of-tree in a short one.
if (-not $BuildDir) {
    $inTree = Join-Path $root "build"
    $BuildDir = if ($inTree.Length -gt 90) { Join-Path $env:LOCALAPPDATA "AnnealBuild\build" } else { $inTree }
}
$buildDir = $BuildDir
Write-Log "Build folder: $buildDir"

# FileTracker also ignores file reads under AppData, so when the project itself lives there it
# never records the project's own headers and incremental builds would link stale objects after a
# header change (observed: heap corruption from a mismatched class layout). In that case delete the
# project's own objects before building; JUCE's objects stay incremental.
$underAppData = $root.StartsWith($env:LOCALAPPDATA, [StringComparison]::OrdinalIgnoreCase) -or
                $root.StartsWith($env:APPDATA, [StringComparison]::OrdinalIgnoreCase)
if ($underAppData -and (Test-Path $buildDir)) {
    $own = Get-ChildItem -Path (Join-Path $root "Source"), (Join-Path $root "tests"), (Join-Path $root "tools") -Recurse -Filter *.cpp |
           ForEach-Object { $_.BaseName }
    $stale = Get-ChildItem $buildDir -Recurse -Filter *.obj -ErrorAction SilentlyContinue | Where-Object { $own -contains $_.BaseName }
    Write-Log "Project is under AppData (header changes are not tracked there); recompiling its $($own.Count) source files."
    $stale | Remove-Item -Force
}

# Native tools write progress to stderr; judge success by exit code only.
function Invoke-Logged([string]$Exe, [string[]]$Arguments) {
    $prev = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    & $Exe @Arguments 2>&1 | Tee-Object -FilePath $logFile -Append | Out-Host
    $code = $LASTEXITCODE
    $ErrorActionPreference = $prev
    return $code
}

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Log "cmake not found (winget install Kitware.CMake)." "ERROR"; exit 1
}

if ($Clean -and (Test-Path $buildDir)) {
    Write-Log "Removing $buildDir"
    Remove-Item -Recurse -Force $buildDir
}

# Use a local JUCE only if it is provably the pinned release: a git checkout, exactly at the
# tag, with no local modifications. (An extracted zip cannot be verified; one found on this
# machine was missing files.) Otherwise CMake fetches the tag itself.
$JuceTag = "8.0.4"
function Test-JuceTree([string]$dir) {
    if (-not $dir -or -not (Test-Path (Join-Path $dir "CMakeLists.txt"))) { return $false }
    if (-not (Test-Path (Join-Path $dir ".git"))) { Write-Log "Skipping $dir (not a git checkout, cannot verify)" "WARN"; return $false }
    $prev = $ErrorActionPreference; $ErrorActionPreference = "Continue"
    $tag = (& git -C $dir describe --tags --exact-match 2>$null)
    $dirty = (& git -C $dir status --porcelain 2>$null)
    $ErrorActionPreference = $prev
    if ($tag -ne $JuceTag) { Write-Log "Skipping $dir (tag '$tag', need $JuceTag)" "WARN"; return $false }
    if ($dirty) { Write-Log "Skipping $dir (has local modifications)" "WARN"; return $false }
    return $true
}

$candidates = @($JuceDir, $env:JUCE_DIR,
                "$env:USERPROFILE\Downloads\HarshVision\HarshVision\build\_deps\juce-src",
                "$env:USERPROFILE\Downloads\Decolor\Decolor\build\_deps\juce-src") | Where-Object { $_ }
$juce = $candidates | Where-Object { Test-JuceTree $_ } | Select-Object -First 1

$configureArgs = @("-S", $root, "-B", $buildDir, "-A", "x64")
if ($juce) {
    Write-Log "Using verified local JUCE $JuceTag at $juce"
    $configureArgs += "-DANNEAL_JUCE_DIR=$juce"
} else {
    Write-Log "No verified local JUCE $JuceTag; CMake will fetch it from GitHub." "WARN"
    $configureArgs += "-DANNEAL_JUCE_DIR="
}
# Installs happen after the tests (tools\Install.ps1), not as a post-build copy.
$configureArgs += "-DANNEAL_COPY_AFTER_BUILD=OFF"

Write-Log "Configuring..."
if ((Invoke-Logged "cmake" $configureArgs) -ne 0) { Write-Log "Configure failed. Log: $logFile" "ERROR"; exit 1 }

Write-Log "Building $Configuration (plugin, tests, snapshot tool)..."
$buildArgs = @("--build", $buildDir, "--config", $Configuration, "--parallel",
               "--target", "Anneal_VST3", "Anneal_Standalone", "AnnealTests", "AnnealSnapshot", "AnnealHostCheck")
if ((Invoke-Logged "cmake" $buildArgs) -ne 0) { Write-Log "Build failed. Log: $logFile" "ERROR"; exit 1 }

$vst3 = Join-Path $buildDir "Anneal_artefacts\$Configuration\VST3\Anneal.vst3"
Write-Log "VST3: $vst3"

# The DLL's Windows file version must be this build's version (3.0.0 shipped saying 0.2.1;
# see the version-resource note in CMakeLists.txt).
$version = ([regex]::Match((Get-Content (Join-Path $root "CMakeLists.txt") -Raw), 'project\(Anneal VERSION ([0-9.]+)')).Groups[1].Value
foreach ($bin in @((Join-Path $vst3 "Contents\x86_64-win\Anneal.vst3"),
                   (Join-Path $buildDir "Anneal_artefacts\$Configuration\Standalone\Anneal.exe"))) {
    $fileVersion = (Get-Item $bin).VersionInfo.FileVersion
    if ($fileVersion -ne $version) {
        Write-Log "$bin has file version '$fileVersion', expected '$version'." "ERROR"; exit 1
    }
}
Write-Log "File version $version on the VST3 and the standalone app."
Write-Log "Standalone: $(Join-Path $buildDir "Anneal_artefacts\$Configuration\Standalone\Anneal.exe")"

if ($Test) {
    $exe = Join-Path $buildDir "AnnealTests_artefacts\$Configuration\AnnealTests.exe"
    Write-Log "Running $exe"
    $code = Invoke-Logged $exe @()
    if ($code -ne 0) { Write-Log "$code test check(s) failed." "ERROR"; exit 1 }

    $hostExe = Join-Path $buildDir "AnnealHostCheck_artefacts\$Configuration\AnnealHostCheck.exe"
    Write-Log "Running $hostExe on the built VST3"
    $code = Invoke-Logged $hostExe @($vst3)
    if ($code -ne 0) { Write-Log "$code host check(s) failed." "ERROR"; exit 1 }
    Write-Log "All tests passed."
}

$version = ([regex]::Match((Get-Content (Join-Path $root "CMakeLists.txt") -Raw), 'project\(Anneal VERSION ([0-9.]+)')).Groups[1].Value
$setupExe = Join-Path $root "dist\Anneal-$version-Setup.exe"
if ($Setup) {
    $iscc = @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
              "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $iscc) { Write-Log "Inno Setup 6 not found (winget install JRSoftware.InnoSetup)." "ERROR"; exit 1 }
    Write-Log "Building setup $version..."
    $isccArgs = @("/Q", "/DAppVersion=$version", "/DBuildDir=$buildDir", "/DOutDir=$(Join-Path $root 'dist')",
                  (Join-Path $root "installer\Anneal.iss"))
    if ((Invoke-Logged $iscc $isccArgs) -ne 0) { Write-Log "Setup build failed. Log: $logFile" "ERROR"; exit 1 }
    Write-Log "Setup: $setupExe"

    if ($Test) {
        # Same script, its own AppId and name, so the sandbox test never touches a real install.
        $testDir = Join-Path $buildDir "setup-test"
        $testArgs = @("/Q", "/DAppVersion=$version", "/DBuildDir=$buildDir", "/DOutDir=$testDir",
                      "/DAppIdValue={{C3A9A011-1FE8-4D62-BC17-61FF1488E1F2}", "/DOutputName=Anneal-test-Setup",
                      (Join-Path $root "installer\Anneal.iss"))
        if ((Invoke-Logged $iscc $testArgs) -ne 0) { Write-Log "Test setup build failed. Log: $logFile" "ERROR"; exit 1 }
        $builtBin = Join-Path $vst3 "Contents\x86_64-win\Anneal.vst3"
        Write-Log "Testing the setup (sandbox install, update while loaded, uninstall)"
        $code = Invoke-Logged "powershell" @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                                             (Join-Path $root "tools\TestSetup.ps1"),
                                             "-Setup", (Join-Path $testDir "Anneal-test-Setup.exe"), "-Built", $builtBin)
        if ($code -ne 0) { Write-Log "$code setup check(s) failed." "ERROR"; exit 1 }
    }
}

$installer = Join-Path $root "tools\Install.ps1"
if ($Install) {
    Write-Log "Running the setup silently just for you (no administrator approval needed)"
    $out = & powershell -NoProfile -ExecutionPolicy Bypass -File $installer -Source $vst3 -Setup $setupExe -CurrentUser
    $code = $LASTEXITCODE
    Write-Log "$out"
    if ($code -ne 0) { Write-Log "Per-user install failed ($code)." "ERROR"; exit 1 }
}

if ($InstallSystem) {
    $sysDir = Join-Path $env:CommonProgramFiles "VST3"
    Write-Log "Running the setup silently into $sysDir (Windows will ask for administrator approval)"
    $out = & powershell -NoProfile -ExecutionPolicy Bypass -File $installer -Source $vst3 -Setup $setupExe
    $code = $LASTEXITCODE
    Write-Log "$out"
    if ($code -ne 0) { Write-Log "System install not done ($code)." "WARN" }
    else { Write-Log "FL Studio and Cubase use the new build the next time they load Anneal." }
}

if ($Snapshot) {
    $exe = Join-Path $buildDir "AnnealSnapshot_artefacts\$Configuration\AnnealSnapshot.exe"
    Push-Location $root
    $code = Invoke-Logged $exe @("snapshots")
    Pop-Location
    if ($code -ne 0) { Write-Log "Snapshot failed." "ERROR"; exit 1 }
    Write-Log "Editor images in $(Join-Path $root 'snapshots')"
}
