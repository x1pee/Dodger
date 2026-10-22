# Building Dodger

## Requirements

- Windows x64.
- Visual Studio 2022 or Build Tools 2022 with **Desktop development with C++**, the MSVC x64 toolchain, and a Windows SDK.
- .NET Framework 4.8 or 4.8.1, including its WPF assemblies. The host build uses the Framework's bundled C# compiler.
- Windows PowerShell 5.1 or later.

The repository includes its MinHook sources; no package restore or game files are needed to compile.

## Compile

From the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
```

`src/build-native.cmd` discovers Visual Studio through `vswhere`, or uses an already configured x64 developer command prompt. It compiles a C++17 DLL with the static MSVC runtime. `src/build-host.ps1` compiles the x64 WPF application and copies its runtime configuration.

The three runtime files appear in the repository root:

```text
Dodger.exe
Dodger.exe.config
ChainedGuard.Native.dll
```

Keep them together. To redistribute a portable build, also include `LICENSE`, `MINHOOK-LICENSE.txt`, and the notices. Generated files are excluded from Git.

## Built-in host checks

```powershell
Start-Process .\Dodger.exe -ArgumentList '--self-test' -Wait -PassThru
Get-Content .\self-test.txt
```

These checks do not start the game. They validate host logic and configuration, not real combat coverage. A successful compile and self-test do not establish compatibility with a new game build.

## Architecture

- `Host.cs` and the other C# files implement the panel, HUD, discovery, settings, process checks, and native-module loading.
- `Native.cpp` and the probe/policy headers implement IL2CPP integration and combat prediction.
- `src/vendor/minhook` supplies the native hooks.

The native integration is specific to the game build documented in [COMPATIBILITY.md](COMPATIBILITY.md). Do not simply remove its hash check to support another build: method locations, layouts, and combat behavior require validation.

The integration headers contain reverse-engineered method locations, layouts, and reflected enum names/IDs, rather than an official game SDK. Game binaries and decompiled game classes are not included.
