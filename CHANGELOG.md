# Changelog

All notable changes to this project (including the experimental structural-discovery branch) are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [3.0.0-exp7] - 2026-10-19

### Added
- **True W^X split-page stub:** Allocated separate `PAGE_READWRITE` data (Page 0) and `PAGE_EXECUTE_READ` code (Page 1) pages for the runtime stub, completely eliminating `PAGE_EXECUTE_READWRITE` (RWX) allocations and runtime `VirtualProtect` flips during settings reloads.
- **Future-proof x86-64 instruction decoding:** Added support for 32-bit length comparisons (`cmp r32, imm8`), `0x89` register moves (`mov r/m, r`), constant-propagated `mov r8d, <anchor_len>` and `mov r8, <anchor_len>` immediates, stack-frame memory operands in the SSO triple (`cmp`/`cmov`), and signed `cmov` variants (`CMOVGE`/`CMOVG`).
- **Strict non-volatile register validation:** Verified that `len_reg`, the `lea` frame base, and all SSO `cmp`/`cmov` source registers are non-volatile under the Win64 ABI so they cannot be clobbered before reaching the patch site.
- **Input normalization and complete verbose logging:** Normalized user app entries (trimming quotes/whitespace, stripping accidental paths, appending `.exe` when omitted) and logged all four foreground evaluation outcomes with burst deduplication when `verboseLogging` is enabled.

### Fixed
- **Multi-byte patch buffer overflow:** Fixed a stack buffer overflow in `InstallHook` and `RemoveHook` when `site.hook_len > 8`, while preserving atomic 64-bit `InterlockedCompareExchange64` patching for the jump header and ensuring `VirtualProtect` covers at least 8 bytes.
- **Safe settings reclamation and unload bypass:** Reclaimed retired `ModSettings` generations once in-flight calls drain, separated C++ RAII from SEH `__try / __except`, and zeroed stub control flags prior to unpatching.

## [3.0.0-exp6] - 2026-10-18

### Added
- **W^X memory protection:** Transitioned the stub page to `PAGE_EXECUTE_READ` (RX) after assembly to avoid persistent RWX pages.
- **Atomic 64-bit hook injection:** Applied and reverted patch sites atomically via `InterlockedCompareExchange64`, eliminating race conditions during hook installation and unhooking.
- **Auto multi-anchor fallback:** Added automatic discovery fallback across known browser anchors (`chrome.exe`, `msedge.exe`, `brave.exe`, `opera.exe`, `vivaldi.exe`) if the primary anchor fails.
- **Structured exception handling (SEH):** Protected the decision handler with `__try / __except` to prevent process crashes if host string pointers are invalid.
- **Configurable verbose logging:** Added `verboseLogging` setting to keep the Windhawk log clean during routine window switching.

## [3.0.0-exp5] - 2026-10-18

### Added
- **Dynamic anchor length:** Replaced hardcoded 11-byte check in `ResolveFromAnchorRef` with dynamic anchor string lengths, enabling custom anchors such as `chrome.exe`.
- **Unload thread safety:** Added an atomic `g_in_flight` invocation counter and drain-wait loop in `RemoveHook` to prevent access violations during `FreeLibrary` on mod unload.
- **Base register safety:** Added explicit non-volatile register validation for the patch site base register.
- **Extended frame support:** Increased `LEA` scanning range up to 8 bytes to support large stack frames using 32-bit displacements (`disp32`).
- **High-performance allocator:** Replaced linear 4 KB allocation probing with a `VirtualQuery`-based allocator adhering to Windows 64 KB allocation granularity (`dwAllocationGranularity`).

### Fixed
- **SIB decoding correction:** Fixed `REX.X` validation in SIB decoding to prevent misidentifying `R12` as "no index register".
- **Strict section filtering:** Excluded writable data sections (`IMAGE_SCN_MEM_WRITE`) from the anchor string literal search to ensure only genuine `.rdata` is matched.
- **Cleanup:** Cleaned up filename matching in `glob_match`, marked the foreign process handler `noexcept`, and freed settings memory on uninit.

## [3.0.0-exp4] - 2026-10-17

### Added
- Initial structural-discovery build locating the patch site via string cross-references instead of static byte signatures.
- Runtime relay stub generation copying host SSO triples and replaying overwritten instructions.
- Diagnostic hex dumping around anchor references for unfamiliar agent layouts.

## [2.4.0] - 2026-10-18

### Added
- Support for Logi Options+ agent 2.06 (`V206` signature and assembly handler) on the stable branch.

## [2.3.0] - 2026-10-15

### Changed
- Fast pass-through bypass and `RFLAGS` preservation for natively detected browsers.

## [2.2.0] - 2026-10-10

### Fixed
- Win64 ABI volatile register preservation (`R9`-`R11`, `XMM0`-`XMM5`), 16-byte stack alignment, lock-free atomic settings swap, and allocation-free foreground evaluation.

## [2.0.1] - 2026-10-05

### Fixed
- Removed per-invocation debug log in the hot path to fix fullscreen taskbar visibility issues.

## [2.0.0] - 2026-10-01

### Added
- Initial Windhawk mod port of `igvk/LogiOptionsPlus-InMemoryPatching`.
