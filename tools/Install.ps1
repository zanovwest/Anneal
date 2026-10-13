# Installs a built Anneal.vst3, also while a DAW still has the old one loaded (a loaded DLL cannot be
# overwritten, but it can be moved, so the old binary goes to %TEMP%\AnnealOld first and is deleted
# by a later install once nothing holds it). The DAW picks up the new build the next time it loads
# the plug-in.
#
#   Install.ps1 -Source <built Anneal.vst3> -Destination <folder you can write>
#       plain copy, e.g. into %LOCALAPPDATA%\Programs\Common\VST3
#   Install.ps1 -Source <built Anneal.vst3> -Setup <Anneal-x.y.z-Setup.exe> [-ApprovalTimeoutSec 300]
#       runs the setup silently; Windows asks for administrator approval. Waits at most
#       ApprovalTimeoutSec for it, so an unattended run reports "not approved" instead of hanging.
#
# Exit code 0 only when the installed binary matches the build.
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$Source,
    [string]$Destination = (Join-Path $env:CommonProgramFiles "VST3"),
    [string]$Setup = "",
    [int]$ApprovalTimeoutSec = 300
)

$ErrorActionPreference = "Stop"
$name = Split-Path -Leaf $Source
$binRel = "Contents\x86_64-win\$name"
$built = Join-Path $Source $binRel
$installed = Join-Path (Join-Path $Destination $name) $binRel
$oldDir = Join-Path $env:TEMP "AnnealOld"
if (-not (Test-Path -LiteralPath $built)) { Write-Host "No build at $built"; exit 2 }

function Test-Installed {
    (Test-Path -LiteralPath $installed) -and
    ((Get-FileHash -LiteralPath $installed).Hash -eq (Get-FileHash -LiteralPath $built).Hash)
}

if ($Setup) {
    if (-not (Test-Path -LiteralPath $Setup)) { Write-Host "No setup at $Setup"; exit 2 }
    # The approval prompt blocks whoever requests it, so a separate helper requests it and this
    # script only waits for the helper. Encoded, so the quoted paths survive both hops.
    $log = Join-Path $env:TEMP "Anneal-setup.log"
    $setupArgs = "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /LOG=`"$log`""
    $request = "Start-Process -FilePath '$($Setup.Replace("'", "''"))' -Verb RunAs -Wait -ArgumentList '$($setupArgs.Replace("'", "''"))'"
    $encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($request))
    $helper = Start-Process powershell -PassThru -WindowStyle Hidden -ArgumentList "-NoProfile -EncodedCommand $encoded"
    if (-not $helper.WaitForExit($ApprovalTimeoutSec * 1000)) {
        Write-Host "Administrator approval still pending after $ApprovalTimeoutSec s; it installs if approved later."
        exit 3
    }
    if (Test-Installed) { Write-Host "Installed $installed (setup log: $log)"; exit 0 }
    Write-Host "Not installed: administrator approval declined, or the setup failed (log: $log)."
    exit 4
}

$target = Join-Path $Destination $name
New-Item -ItemType Directory -Force -Path $Destination | Out-Null
Get-ChildItem -LiteralPath $oldDir -File -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
if (Test-Path -LiteralPath $installed) {
    try { Remove-Item -LiteralPath $installed -Force }
    catch {
        New-Item -ItemType Directory -Force -Path $oldDir | Out-Null
        Move-Item -LiteralPath $installed -Destination (Join-Path $oldDir ("Anneal-{0}.vst3" -f (Get-Date -Format "yyyyMMddHHmmssfff")))
    }
}
# /XJ: never follow junctions.
robocopy $Source $target /E /XJ /R:2 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { Write-Host "robocopy failed ($LASTEXITCODE)"; exit 5 }
if (-not (Test-Installed)) { Write-Host "Installed binary does not match the build: $installed"; exit 6 }
Write-Host "Installed $installed"
exit 0
