[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Installer,
    [string]$ObsRuntime = '.deps/obs-runtime',
    [string]$StagePath = 'stage',
    [string]$Launcher = 'build_x64/Release/interactio-update-e2e-launcher.exe'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = [IO.Path]::GetFullPath((Join-Path (Get-Location) 'build-update-e2e'))
if (Test-Path -LiteralPath $root) { throw "Use a clean fixture: $root" }
$fixture = Join-Path $root 'obs-studio'
$staging = Join-Path $root 'update-staging'
New-Item -ItemType Directory -Path $root, $staging -Force | Out-Null
Copy-Item -LiteralPath (Resolve-Path -LiteralPath $ObsRuntime).Path -Destination $fixture -Recurse
New-Item -ItemType File -Path (Join-Path $fixture 'portable_mode.txt') -Force | Out-Null
$setup = Join-Path $staging 'setup.exe'
$helper = Join-Path $staging 'excel-interactio-updater.exe'
Copy-Item -LiteralPath (Resolve-Path -LiteralPath $Installer).Path -Destination $setup
Copy-Item -LiteralPath (Join-Path (Resolve-Path -LiteralPath $StagePath).Path 'data/obs-plugins/excel-interactio/excel-interactio-updater.exe') -Destination $helper
$existingPlugin = Join-Path $fixture 'obs-plugins/64bit/excel-interactio.dll'
Set-Content -LiteralPath $existingPlugin -Value 'previous-version-sentinel' -NoNewline
$originalHash = (Get-FileHash -LiteralPath $existingPlugin -Algorithm SHA256).Hash
$setupHash = (Get-FileHash -LiteralPath $setup -Algorithm SHA256).Hash.ToLowerInvariant()
$exe = Join-Path $fixture 'bin/64bit/obs64.exe'
$obs = Start-Process -FilePath $exe -ArgumentList @('--portable', '--multi', '--disable-updater') -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -PassThru
for ($i = 0; $i -lt 50 -and -not $obs.HasExited -and $obs.MainWindowHandle -eq 0; $i++) { Start-Sleep -Milliseconds 500; $obs.Refresh() }
if ($obs.HasExited -or $obs.MainWindowHandle -eq 0) {
    $logDir = Join-Path $fixture 'config/obs-studio/logs'
    if (Test-Path -LiteralPath $logDir) {
        $recent = Get-ChildItem -LiteralPath $logDir -File | Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if ($recent) { Get-Content -LiteralPath $recent.FullName -Tail 60 | Write-Host }
    }
    throw 'Isolated OBS did not reach its main window.'
}
$launcherPath = (Resolve-Path -LiteralPath $Launcher).Path
$arguments = @(('"' + $helper + '"'), ('"' + $setup + '"'), $setupHash, ('"' + $fixture + '"'), $obs.Id.ToString())
$launch = Start-Process -FilePath $launcherPath -ArgumentList $arguments -WindowStyle Hidden -PassThru -Wait
if ($launch.ExitCode -ne 0) { throw "Could not start update helper: $($launch.ExitCode)" }
Start-Sleep -Seconds 3
if ($obs.HasExited -or (Get-FileHash -LiteralPath $existingPlugin -Algorithm SHA256).Hash -ne $originalHash -or (Test-Path -LiteralPath (Join-Path $staging 'result.txt'))) {
    throw 'Updater did not wait for the running OBS process.'
}
if (-not $obs.CloseMainWindow()) { throw 'Isolated OBS did not accept a normal close request.' }
if (-not $obs.WaitForExit(40000)) { throw 'Isolated OBS did not close normally; it was not force-closed.' }
$resultPath = Join-Path $staging 'result.txt'
for ($i = 0; $i -lt 120 -and -not (Test-Path -LiteralPath $resultPath); $i++) { Start-Sleep -Seconds 1 }
if (-not (Test-Path -LiteralPath $resultPath)) { throw 'Update helper timed out without a result.' }
$result = Get-Content -LiteralPath $resultPath -Raw
if ($result -notmatch 'Update installed successfully') { throw "Update failed: $result" }
foreach ($file in Get-ChildItem -LiteralPath (Resolve-Path -LiteralPath $StagePath).Path -File -Recurse) {
    $relative = [IO.Path]::GetRelativePath((Resolve-Path -LiteralPath $StagePath).Path, $file.FullName)
    $installed = Join-Path $fixture $relative
    if (-not (Test-Path -LiteralPath $installed) -or (Get-FileHash -LiteralPath $file.FullName).Hash -ne (Get-FileHash -LiteralPath $installed).Hash) {
        throw "Updated payload differs: $relative"
    }
}
Write-Host 'Updater waited for isolated OBS to close and installed the verified payload successfully.'

