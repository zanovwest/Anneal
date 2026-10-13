# Installs a built Anneal with its setup, silently, and checks that the installed binary is the build.
#
#   Install.ps1 -Source <built Anneal.vst3> -Setup <Anneal-x.y.z-Setup.exe> -CurrentUser
#       just for me: %LOCALAPPDATA%\Programs\Common\VST3. No administrator approval, so it works
#       unattended (the daily update uses this).
#   Install.ps1 -Source <built Anneal.vst3> -Setup <Anneal-x.y.z-Setup.exe> [-ApprovalTimeoutSec 300]
#       all users: C:\Program Files\Common Files\VST3. Windows asks for administrator approval; this
#       waits at most ApprovalTimeoutSec for it, then reports instead of hanging.
#
# Works while a DAW has Anneal loaded (the setup moves the loaded binary aside); the DAW picks up the
# new build the next time it loads the plug-in. Exit code 0 only when the installed binary matches.
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$Source,
    [Parameter(Mandatory)] [string]$Setup,
    [switch]$CurrentUser,
    [int]$ApprovalTimeoutSec = 300
)

$ErrorActionPreference = "Stop"
$name = Split-Path -Leaf $Source
$binRel = "Contents\x86_64-win\$name"
$built = Join-Path $Source $binRel
$destination = if ($CurrentUser) { Join-Path $env:LOCALAPPDATA "Programs\Common\VST3" } else { Join-Path $env:CommonProgramFiles "VST3" }
$installed = Join-Path (Join-Path $destination $name) $binRel
$log = Join-Path $env:TEMP "Anneal-setup.log"
if (-not (Test-Path -LiteralPath $built)) { Write-Host "No build at $built"; exit 2 }
if (-not (Test-Path -LiteralPath $Setup)) { Write-Host "No setup at $Setup"; exit 2 }

function Test-Installed {
    (Test-Path -LiteralPath $installed) -and
    ((Get-FileHash -LiteralPath $installed).Hash -eq (Get-FileHash -LiteralPath $built).Hash)
}

$setupArgs = "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /LOG=`"$log`""
if ($CurrentUser) {
    $p = Start-Process -FilePath $Setup -ArgumentList "/CURRENTUSER $setupArgs" -Wait -PassThru
    if ($p.ExitCode -eq 0 -and (Test-Installed)) { Write-Host "Installed $installed"; exit 0 }
    Write-Host "Setup failed (exit $($p.ExitCode), log: $log)."
    exit 4
}

# The approval prompt blocks whoever requests it, so a separate helper requests it and this script
# only waits for the helper. Encoded, so the quoted paths survive both hops.
$request = "Start-Process -FilePath '$($Setup.Replace("'", "''"))' -Verb RunAs -Wait -ArgumentList '$("/ALLUSERS $setupArgs".Replace("'", "''"))'"
$encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($request))
$helper = Start-Process powershell -PassThru -WindowStyle Hidden -ArgumentList "-NoProfile -EncodedCommand $encoded"
if (-not $helper.WaitForExit($ApprovalTimeoutSec * 1000)) {
    Write-Host "Administrator approval still pending after $ApprovalTimeoutSec s; it installs if approved later."
    exit 3
}
if (Test-Installed) { Write-Host "Installed $installed (setup log: $log)"; exit 0 }
Write-Host "Not installed: administrator approval declined, or the setup failed (log: $log)."
exit 4
