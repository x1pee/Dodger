$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'src\build-native.cmd')
if ($LASTEXITCODE -ne 0) { throw 'Native compilation failed.' }
& (Join-Path $PSScriptRoot 'src\build-host.ps1')
Write-Output 'Build complete. Keep Dodger.exe, Dodger.exe.config and ChainedGuard.Native.dll together.'
