# Changelog

All notable changes to the stable branch of this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [2.4.0] - 2026-10-18

### Added
- **Support for Logi Options+ agent 2.06:** Added the `V206` code signature, hook bytes, and assembly handler, tried ahead of the older versions as upstream does. Ported from `igvk/LogiOptionsPlus-InMemoryPatching` version 2.6.
- Frame pointer handling in `R14` and updated string buffer frame offset for agent 2.06 while preserving full register and flag pass-through guarantees.

## [2.3.0] - 2026-10-15

### Changed
- **Pass-through for natively detected applications:** Each assembly handler checks two control flags up front and falls straight through to the original instructions whenever the outcome cannot differ from what the agent already computed.
- **Flag preservation:** All version handlers now preserve `RFLAGS` across the hook so natively detected browsers see the exact register and flag state they would have seen without the mod.

## [2.2.0] - 2026-10-10

### Fixed
- **Win64 ABI compliance:** Assembly handlers now preserve volatile registers not used for arguments (`R9`, `R10`, `R11`, `XMM0`-`XMM5`), align `RSP` to 16 bytes at the call site, and reserve their own 32-byte shadow space instead of borrowing the agent's stack frame.
- **Lock-free settings publication:** Replaced by-value settings replacement with an atomic pointer swap so concurrent foreground switches never read freed or partially updated vectors.

### Changed
- **Zero-allocation hot path:** Located executable names in place and lowercased them into a stack buffer with early exits for natively detected browsers.
- **Hook safety checks:** Verified that the relay page is within `rel32` reach before patching, restricted `VirtualProtect` to the patched byte range, and continued scanning if a candidate signature's hook bytes do not match.

## [2.0.1] - 2026-10-05

### Fixed
- **Fullscreen taskbar regression:** Removed per-invocation debug logging inside the foreground-process handler that previously delayed window manager fullscreen transitions (e.g. YouTube fullscreen or fullscreen browser games).
- **Metadata:** Added `@github` link to the Windhawk mod header.

## [2.0.0] - 2026-10-01

### Added
- Initial release. Full port of `igvk/LogiOptionsPlus-InMemoryPatching` to a native Windhawk mod, including the in-memory inline trampoline hook and five per-version assembly handlers (`V100` through `V194`).
