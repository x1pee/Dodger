# Dodger

Automatic blocking, dodging, and a threat HUD for **Chained Beasts** on Windows.

Dodger connects to the running game and reads combat state through its native module. It predicts incoming melee attacks, projectiles, and supported boss attacks, then uses blocking or dodging according to your settings. Defense priority can interrupt attacks when protection is needed.

The compact HUD shows incoming danger and shield durability, with animations for blocks, dodges, and shield breaks. The desktop panel includes automatic connection, game discovery, manual folder selection, and diagnostic logs. Switch between **English and Russian** in the header; your language choice is saved.

**[Download version 19](https://github.com/x1pee/Dodger/releases/latest)** — extract the complete Windows x64 ZIP and run `Dodger.exe`. Keep the EXE, configuration file, and native DLL together. Start Dodger before or during the game, enable the features you want, and minimize the panel.

Requires Windows 10/11 x64, .NET Framework 4.8+, and the [supported game build](docs/COMPATIBILITY.md). Linux/macOS are unsupported. Defense timing is best effort; complete attack coverage is not guaranteed.

![Staged in-game demonstration](https://github.com/x1pee/Dodger/releases/download/v18/Dodger-preview.gif)

[Screenshots](docs/SCREENSHOTS.md) · [Build from source](docs/BUILDING.md) · [MIT license](LICENSE) · [Third-party notices](THIRD_PARTY_NOTICES.md)
