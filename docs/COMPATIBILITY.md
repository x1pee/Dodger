# Compatibility and operation

Version 18 targets Windows 10/11 x64 and .NET Framework 4.8/4.8.1. Local verification was performed on Windows 11 x64. Windows 10, other computers, Wine/Proton, Linux, macOS, Windows ARM, and 32-bit systems have not been independently validated; native Linux/macOS builds are not provided.

## Supported game build

Chained Beasts, Windows IL2CPP, Unity 6000.3.21f1. The required SHA-256 of `GameAssembly.dll` is:

```text
C7437CED53B682904EAF18356AEF16613AFA436141E2C706A7560D3A1A4DACDD
```

Dodger refuses to connect if this does not match. Storefront or installation path does not override this check. Check your installed file with:

```powershell
Get-FileHash 'C:\path\to\Chained Beasts\GameAssembly.dll' -Algorithm SHA256
```

## Installation

Extract the full release ZIP into a writable folder. Keep the EXE, its configuration, and the native DLL together. Launch the game yourself or use the panel. If detection fails, select the complete game folder manually. An archived game must first be extracted.

Automatic defense starts disabled in a normal launch. Enable the desired options in the panel. Minimize it with the normal window control; restore it from the taskbar or tray. F8 is not intercepted. F9 adds a diagnostic marker. Alt+Tab hides the HUD and pauses automation while the game is not active.

After updating Dodger, completely close and reopen both Dodger and the game, so the old module is unloaded. No save files are included or modified by installation. Settings and logs are normally stored beside the EXE, with a per-copy LocalAppData fallback if that location is not writable. The panel opens the actual log folder.

The legacy native log path must fit within 170 characters; use a short installation path. Run Dodger with the same privilege level as the game.

## Limits

Prediction uses game state and collision forecasts. Some attacks cannot be canceled or defended against in a given player state. Extremely close projectiles may leave insufficient time for a dodge. Automated actions and HUD warnings are best effort, not a guarantee against damage.

The demonstration is a staged in-game encounter. It does not prove every enemy, weapon, boss, or timing combination. Compiling or passing host checks does not replace gameplay testing.
