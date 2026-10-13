# Installs Anneal with its setup, silently, and checks that the installed binary is the build.
#
#   Install.ps1 -Source <built Anneal.vst3> -Setup <dist\Anneal-x.y.z-Setup.exe>
#       All users (C:\Program Files\Common Files\VST3, C:\Program Files\Anneal). Windows asks the
#       user for administrator approval. The approved step installs the NEWEST setup in the same
#       folder, so a prompt answered hours later never installs an older build, and only one
#       prompt is ever waiting: if one is pending, this run just reports it. Waits at most
#       ApprovalTimeoutSec, then returns 3 ("waiting for approval") instead of hanging.
#   Install.ps1 -Source <built Anneal.vst3> -Setup <...> -CurrentUser
#       Just for me (%LOCALAPPDATA%\Programs\Common\VST3), no approval needed.
#   Install.ps1 -NewestIn <dist folder> [-DryRun]
#       The approved (elevated) step: run the newest Anneal-x.y.z-Setup.exe for all users.
#
# Works while a DAW has Anneal loaded (the setup moves the loaded binary aside); the DAW picks up
# the new build the next time it loads the plug-in. Exit 0 only when the installed binary matches.
[CmdletBinding()]
param(
    [string]$Source = "",
    [string]$Setup = "",
    [switch]$CurrentUser,
    [int]$ApprovalTimeoutSec = 300,
    [string]$NewestIn = "",
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"
$log = Join-Path $env:TEMP "Anneal-setup.log"

function Get-NewestSetup([string]$dir) {
    Get-ChildItem -LiteralPath $dir -Filter "Anneal-*-Setup.exe" -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^Anneal-(\d+\.\d+\.\d+)-Setup\.exe$' } |
        Sort-Object { [version]($_.Name -replace '^Anneal-(\d+\.\d+\.\d+)-Setup\.exe$', '$1') } |
        Select-Object -Last 1
}

if ($NewestIn) {
    $newest = Get-NewestSetup $NewestIn
    if (-not $newest) { Write-Host "No setup in $NewestIn"; exit 2 }
    Write-Host "Newest setup: $($newest.Name)"
    if ($DryRun) { exit 0 }
    $p = Start-Process -FilePath $newest.FullName -ArgumentList "/ALLUSERS /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /LOG=`"$log`"" -Wait -PassThru
    exit $p.ExitCode
}

if (-not $Source -or -not $Setup) { Write-Host "Need -Source and -Setup (or -NewestIn)."; exit 2 }
$name = Split-Path -Leaf $Source
$binRel = "Contents\x86_64-win\$name"
$built = Join-Path $Source $binRel
$destination = if ($CurrentUser) { Join-Path $env:LOCALAPPDATA "Programs\Common\VST3" } else { Join-Path $env:CommonProgramFiles "VST3" }
$installed = Join-Path (Join-Path $destination $name) $binRel
if (-not (Test-Path -LiteralPath $built)) { Write-Host "No build at $built"; exit 2 }
if (-not (Test-Path -LiteralPath $Setup)) { Write-Host "No setup at $Setup"; exit 2 }

function Test-Installed {
    (Test-Path -LiteralPath $installed) -and
    ((Get-FileHash -LiteralPath $installed).Hash -eq (Get-FileHash -LiteralPath $built).Hash)
}

if ($CurrentUser) {
    $p = Start-Process -FilePath $Setup -ArgumentList "/CURRENTUSER /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /LOG=`"$log`"" -Wait -PassThru
    if ($p.ExitCode -eq 0 -and (Test-Installed)) { Write-Host "Installed $installed"; exit 0 }
    Write-Host "Setup failed (exit $($p.ExitCode), log: $log)."
    exit 4
}

# All users. One pending approval at most: it will install the newest build when answered.
$marker = Join-Path $env:TEMP "Anneal-install-pending.txt"
if (Test-Path -LiteralPath $marker) {
    $pendingId = (Get-Content -LiteralPath $marker -ErrorAction SilentlyContinue | Select-Object -First 1) -as [int]
    if ($pendingId -and (Get-Process -Id $pendingId -ErrorAction SilentlyContinue)) {
        Write-Host "An administrator approval is already waiting; when approved it installs the newest build."
        exit 3
    }
}

# The approval prompt blocks whoever requests it, so a separate helper requests it and this script
# only waits for the helper. Encoded, so the quoted paths survive both hops.
$dist = Split-Path -Parent (Resolve-Path -LiteralPath $Setup)
$self = "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" -NewestIn `"$dist`""
$request = "Start-Process powershell -Verb RunAs -Wait -WindowStyle Hidden -ArgumentList '$($self.Replace("'", "''"))'"
$encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($request))
$helper = Start-Process powershell -PassThru -WindowStyle Hidden -ArgumentList "-NoProfile -EncodedCommand $encoded"
Set-Content -LiteralPath $marker -Value $helper.Id
if (-not $helper.WaitForExit($ApprovalTimeoutSec * 1000)) {
    Write-Host "Waiting for administrator approval (the prompt stays until answered; it installs the newest build)."
    exit 3
}
Remove-Item -LiteralPath $marker -ErrorAction SilentlyContinue
if (Test-Installed) { Write-Host "Installed $installed (setup log: $log)"; exit 0 }
Write-Host "Not installed: administrator approval declined, or the setup failed (log: $log)."
exit 4
