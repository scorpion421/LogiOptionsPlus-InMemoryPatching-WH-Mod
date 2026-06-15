# Logi Options+ Smooth Scroll for All Apps

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Windhawk](https://img.shields.io/badge/Windhawk-mod-blue)](https://windhawk.net/)
[![Platform](https://img.shields.io/badge/platform-Windows%20x64-0078D6?logo=windows&logoColor=white)](#compatibility)
![Architecture](https://img.shields.io/badge/arch-amd64-lightgrey)
[![GitHub release](https://img.shields.io/github/v/release/scorpion421/LogiOptionsPlus-InMemoryPatching-WH-Mod?display_name=tag)](https://github.com/scorpion421/LogiOptionsPlus-InMemoryPatching-WH-Mod/releases)
[![GitHub stars](https://img.shields.io/github/stars/scorpion421/LogiOptionsPlus-InMemoryPatching-WH-Mod?style=social)](https://github.com/scorpion421/LogiOptionsPlus-InMemoryPatching-WH-Mod/stargazers)

A [Windhawk](https://windhawk.net/) mod that enables high-resolution smooth mouse wheel scrolling in **any** application, not just the handful of browsers that Logitech hardcodes.

This is a Windhawk port of [igvk/LogiOptionsPlus-InMemoryPatching](https://github.com/igvk/LogiOptionsPlus-InMemoryPatching). It reuses the same proven in-memory patching technique, but delivers it as a single Windhawk mod instead of a `version.dll` that has to be copied into the install directory and re-applied after every update.

## The problem

Logitech mice support high-resolution scroll wheel events, which produce pixel-smooth scrolling instead of the chunky three-line jumps you get otherwise. Logi Options+ only enables this for a few hardcoded browser executables (primarily those named `chrome.exe` and `firefox.exe`). Every other application -- including Chromium forks that ship under their own name, such as Vivaldi, Brave, and Opera -- is left with coarse scrolling, even though the hardware is fully capable of the smooth behavior.

## How it works

The mod is injected into the `logioptionsplus_agent.exe` process by Windhawk. At load time it:

1. Scans the agent's executable memory for a version-specific code signature (five known signatures cover agent versions 1.00 through 1.94).
2. Locates the hook point a few bytes after the signature, inside the foreground-process check function.
3. Allocates a relay page within reach of a 32-bit relative jump, writes a trampoline there, and overwrites the hook point with an `E9` jump to it.
4. A small assembly handler (one per agent version) reconstructs the original register state, calls back into the decision logic, restores the registers the agent expects, and returns to the original code.

The decision logic decides, per foreground application, whether smooth scrolling should be active. The agent's built-in browser detection is always consulted first and preserved -- the mod only ever **adds** applications to the smooth-scrolling set, it never removes what Logitech already handles.

When the mod is disabled or removed, the original bytes are restored cleanly, so the agent keeps running without a restart.

## Installation

1. Install [Windhawk](https://windhawk.net/) if you do not already have it.
2. Make sure Logi Options+ is installed and its background agent (`logioptionsplus_agent.exe`) is running.
3. In Windhawk, create a new mod, paste the contents of `logioptionsplus-smooth-scroll.wh.cpp`, and compile.
4. Enable the mod. The agent is patched immediately -- no restart required.

## Configuration

The mod exposes two settings in the Windhawk UI.

**Additional applications** -- executable filenames (without a path) for which smooth scrolling should be enabled. Wildcards `*` and `?` are supported. The defaults include common Chromium-based browsers that the agent does not detect on its own.

**Excluded applications** -- executable filenames for which smooth scrolling should be explicitly disabled. This overrides both the additional applications list and the agent's built-in browser detection. Wildcards `*` and `?` are supported.

### Decision priority

For each foreground application, the outcome is decided in this order:

1. The application matches the **excluded** list -> smooth scrolling **off** (highest priority).
2. The agent's built-in browser detection matches -> smooth scrolling **on**.
3. The application matches the **additional** list -> smooth scrolling **on**.
4. No match -> smooth scrolling **off**.

### Examples

| Entry | Effect |
| --- | --- |
| `vivaldi.exe` | Enable smooth scrolling for Vivaldi |
| `code.exe` | Enable smooth scrolling for VS Code |
| `*` | Enable smooth scrolling for every application |
| `notepad?.exe` | Match `notepad.exe`, `notepad2.exe`, etc. |

Put a pattern in **Excluded applications** to carve out an exception, for example to keep a specific app on coarse scrolling even though it would otherwise match.

## Compatibility

The mod targets all currently known Logi Options+ agent versions (internal markers V100 through V194). If a future agent release changes the relevant code, none of the signatures will match and the mod will quietly do nothing -- it will not crash the agent. If that happens, the signatures need to be updated against the new build.

- **Architecture:** 64-bit (amd64) only.
- **Target process:** `logioptionsplus_agent.exe`.
- **No files written to disk.** Everything happens in memory.
- **No reconfiguration after Logi Options+ updates.** Windhawk re-injects automatically.

## Troubleshooting

If smooth scrolling does not work after enabling the mod, check the Windhawk log output (or a tool such as DebugView). The mod logs:

- which signature was matched and at what address,
- how many enabled/disabled entries were loaded from settings,
- each time the foreground-check handler fires, with the process name it saw.

If you see `no matching signature found`, the installed agent version is not covered by the current signatures. If the handler never fires, the hook did not take. If settings show `0 enabled`, the application list did not load -- verify the entries in the Windhawk settings UI.

## Credits

- Original project and reverse-engineering work: [igvk/LogiOptionsPlus-InMemoryPatching](https://github.com/igvk/LogiOptionsPlus-InMemoryPatching).
- Windhawk modding framework: [ramensoftware/windhawk](https://github.com/ramensoftware/windhawk).

## License

MIT, matching the license of the original project.
