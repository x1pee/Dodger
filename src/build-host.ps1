param([string]$OutputName = 'Dodger.exe')
$ErrorActionPreference = 'Stop'
$guardRoot = Split-Path -Parent $PSScriptRoot
$guardFramework = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
$guardCompiler = Join-Path $guardFramework 'csc.exe'
$guardIcon = Join-Path $guardRoot 'assets\Dodger.ico'
if (-not (Test-Path -LiteralPath $guardIcon -PathType Leaf)) { throw 'Dodger.ico is missing.' }
$guardReferences = @(
    'System.dll', 'System.Core.dll', 'System.Xml.dll', 'System.Xml.Linq.dll',
    'System.Xaml.dll', 'System.Windows.Forms.dll', 'System.Drawing.dll',
    (Join-Path $guardFramework 'WPF\WindowsBase.dll'),
    (Join-Path $guardFramework 'WPF\PresentationCore.dll'),
    (Join-Path $guardFramework 'WPF\PresentationFramework.dll')
)
$guardArgs = @('/nologo', '/target:winexe', '/platform:x64', '/optimize+', ('/out:' + (Join-Path $guardRoot $OutputName)))
$guardArgs += '/win32icon:' + $guardIcon
$guardArgs += '/resource:' + $guardIcon + ',Dodger.Icon'
$guardArgs += '/win32manifest:' + (Join-Path $PSScriptRoot 'Dodger.manifest')
foreach ($guardReference in $guardReferences) { $guardArgs += '/reference:' + $guardReference }
$guardArgs += Join-Path $PSScriptRoot 'Host.cs'
foreach ($guardSource in @('I18n.cs','GameDiscovery.cs','GameRuntime.cs','GameLaunch.cs','HostStorage.cs','NativeFiles.cs','AssemblyInfo.cs')) { $guardArgs += Join-Path $PSScriptRoot $guardSource }
& $guardCompiler $guardArgs
if ($LASTEXITCODE -ne 0) { throw 'C# compilation failed.' }
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Dodger.config') -Destination ((Join-Path $guardRoot $OutputName) + '.config')
Write-Output (Join-Path $guardRoot $OutputName)
