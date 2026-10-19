# Logi Options+ Smooth Scroll for All Apps (Structural-Discovery Build)

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Windhawk](https://img.shields.io/badge/Windhawk-mod-blue)](https://windhawk.net/)
[![Platform](https://img.shields.io/badge/platform-Windows%20x64-0078D6?logo=windows&logoColor=white)](#compatibility)
![Architecture](https://img.shields.io/badge/arch-amd64-lightgrey)
![Status](https://img.shields.io/badge/status-experimental%20(v3.0.0--exp7)-orange)
[![Stable build](https://img.shields.io/badge/stable%20build-main%20branch-brightgreen)](https://github.com/scorpion421/LogiOptionsPlus-InMemoryPatching-WH-Mod/tree/main)

> **About this branch:** This is the next-generation **structural-discovery** build. While the [`main` branch](https://github.com/scorpion421/LogiOptionsPlus-InMemoryPatching-WH-Mod/tree/main) relies on hardcoded byte signatures extracted by hand for each Logi Options+ release, this build dynamically derives the patch site from code structure and generates its relay trampoline at runtime.
>
> Functionally, both builds achieve the same goal: pixel-smooth scrolling in any user-selected application without losing native Logi Options+ browser detection.

---

## Key differences: `main` (stable) vs. `experimental`

| Feature | `main` branch (stable) | `experimental` branch (structural discovery) |
|---|---|---|
| **Patch site detection** | Hardcoded byte signatures per version (1.00 - 1.94, 2.06) | **Dynamic structural discovery** via string cross-references (`firefox.exe`, `chrome.exe`, etc.) |
| **Relay trampoline** | Hand-written assembly handlers per version | **Dynamic runtime codegen** assembled specifically for the host layout |
| **Logi Options+ updates** | Breaks on new agent releases until signatures are hand-extracted | **Resilient to updates:** adapts automatically without code changes |
| **Self-healing fallback** | None (silent failure if signature misses) | **Multi-anchor fallback chain** across 6 known browser signatures |
| **Hook concurrency** | Standard byte-by-byte write | **Atomic 64-bit swap** (`InterlockedCompareExchange64`) with multi-byte tail handling |
| **Memory security** | RWX relay pages | **True split-page W^X compliance** (`PAGE_READWRITE` data page + `PAGE_EXECUTE_READ` code page; zero RWX allocations) |
| **Crash protection** | None | **SEH boundary** (`__try / __except`) isolated from C++ RAII catches invalid host pointers |
| **Unload safety** | Immediate unhook | **Stub bypass disarm + active in-flight counter** with thread drain-wait before `FreeLibrary` |
| **Memory allocation** | Linear 4 KB probing | **64 KB-aligned `VirtualQuery` allocator** (instant startup, zero lag) |
| **Configuration** | Enabled / Disabled apps | Normalized Enabled / Disabled apps + **Anchor string** + **Deduplicated verbose logging** |

---

## The problem

Logitech mice feature high-resolution scroll wheels capable of pixel-smooth scrolling. However, Logi Options+ artificially restricts this behavior to a tiny list of hardcoded browser executables (primarily `chrome.exe` and `firefox.exe`). Every other application - including Chromium-based browsers like Vivaldi, Brave, Opera, Edge, as well as editors, PDF viewers, and productivity tools - is restricted to coarse 3-line notches.

---

## How structural discovery works

Where the stable mod matches exact byte sequences, this build inspects instructions:

```
[ Read-Only Data (.rdata) ]
  "firefox.exe" (or fallback anchor: chrome.exe, msedge.exe, etc.)
         |
         |  1. Locate anchor literal in .rdata
         v
[ Executable Code (.text) ]
  48 8D 15 [disp32]   ->   lea rdx, [rip+disp32]   (arg setup for browser comparison)
         |
         |  2. Scan backwards within a 48-byte window
         v
  48 8D 4C 24 30      ->   lea rcx, [rsp+disp]     \
  48 83 FE 0F         ->   cmp rsi, 15              >  5. Extract Small String Optimization (SSO) triple
  48 0F 43 CE         ->   cmovae rcx, rsi         /      (supports both register and stack-frame operands)
  49 83 FE 0B         ->   cmp r14, 11 (anchor_len)   3. Discover length check & non-volatile length register
  75 18               ->   jne .mismatch              4. Follow branch to result convergence
        ...
.mismatch:
  30 C0               ->   xor al, al              (converges here: store result)
  88 43 28            ->   mov [rbx+0x28], al      <- 6. THE PATCH SITE
  48 8B 5B 08         ->   mov rbx, [rbx+0x08]
```

1. **Locate anchor string:** Finds the null-terminated string literal in the main module's read-only data sections (`.rdata`).
2. **Find code reference:** Finds a RIP-relative `lea rdx, [rip+disp32]` in `.text` referencing the anchor.
3. **Trace length check:** Scans backward to identify `cmp <reg>, anchor_len` (64-bit or 32-bit) followed by a `jne`, confirming the same non-volatile register (or constant-propagated anchor length) is passed as the comparison size argument in `R8`.
4. **Follow branch target:** Follows the branch to the convergence point where the match/mismatch paths store the boolean verdict.
5. **Extract pointer setup:** Decodes the preceding Small String Optimization (SSO) triple (`lea rcx` / `cmp` / `cmov rcx`) across both register and stack-memory forms, verifying all referenced registers are non-volatile.
6. **Patch site validation:** Validates the target store (`mov [base+0x28], al`) and subsequent load (`mov reg, [base+0x08]`), ensuring `base` is a non-volatile register.

---

## Hardened safety and security

Deriving a patch address at runtime requires strict safety rails to ensure stability:

* **True split-page W^X memory protection:** The runtime trampoline uses two adjacent pages: Page 0 (`PAGE_READWRITE`) holds the control flags and target pointers, while Page 1 is written as `PAGE_READWRITE` and transitioned once to `PAGE_EXECUTE_READ` (RX) before hook installation. `PAGE_EXECUTE_READWRITE` (RWX) is never requested at allocation or runtime, satisfying strict Windows Exploit Guard and ACG policies.
* **Atomic 64-bit injection with multi-byte safety:** Hooks are written and removed using `InterlockedCompareExchange64` with explicit bounds handling when the overwritten instruction pair exceeds 8 bytes. Other threads in `logioptionsplus_agent.exe` never encounter partially-written instructions.
* **Strict non-volatile register verification:** All registers replayed at the patch site (`len_reg`, the `lea` frame base, and SSO `cmp`/`cmov` registers) are verified to be non-volatile under the Win64 ABI so they cannot be clobbered by intermediate function calls.
* **Structured exception handling (SEH):** The decision handler is guarded by `__try / __except` in a dedicated wrapper separated from C++ RAII destructors. If Logi Options+ ever passes an invalid string pointer, the exception is caught safely and falls back to the native verdict.
* **Bypass disarm and drain-wait on unload:** When the mod is disabled, `RemoveHook()` first zeroes the stub control flags so any newly arriving thread takes the fast bypass path, restores the original instructions atomically, and waits for all in-flight threads to exit before Windhawk unloads the DLL.
* **Strict section filtering:** Writable sections (`IMAGE_SCN_MEM_WRITE`) are excluded from anchor searches to ensure only genuine `.rdata` string constants are matched.
* **Single candidate rule:** If more than one candidate survives validation, discovery aborts rather than guesses.

---

## Installation

1. Install [Windhawk](https://windhawk.net/) if you have not already done so.
2. Ensure Logi Options+ is installed and its background agent (`logioptionsplus_agent.exe`) is running.
3. In the Windhawk UI, click **Create Mod**, paste the code from [`logioptionsplus-smooth-scroll-EXPERIMENTAL.wh.cpp`](logioptionsplus-smooth-scroll-EXPERIMENTAL.wh.cpp), and compile.
4. Enable the mod. Logi Options+ is patched in memory immediately - no agent or system restart required.

> **Note:** Do not run this mod concurrently with the stable version (`logioptionsplus-smooth-scroll.wh.cpp`), as both target the same logic.

---

## Configuration

Settings can be customized directly in the Windhawk Mod Settings UI:

* **Additional applications (`enabledApps`)**: Executable filenames (without path) to enable smooth scrolling for. Wildcards `*` and `?` are supported. Quotes, whitespace, and accidental directory paths are stripped automatically, and `.exe` is appended if omitted.
* **Excluded applications (`disabledApps`)**: Executable filenames to explicitly disable smooth scrolling for. Overrides both the additional list and Logitech's built-in detection.
* **Anchor string (`anchorString`)**: The literal used to locate the patch site (default: `firefox.exe`). Any valid browser string works.
* **Verbose logging (`verboseLogging`)**: When enabled (`true`), logs every distinct window evaluation outcome (disabled match, built-in detection, enabled match, and unmatched) with burst deduplication to the Windhawk log. Keep disabled (`false`) for normal use.

### Priority order

1. **Excluded match** -> Smooth scrolling **OFF** (overrides everything).
2. **Logitech native detection** -> Smooth scrolling **ON** (preserves built-in browser support).
3. **Additional match** -> Smooth scrolling **ON**.
4. **Otherwise** -> Smooth scrolling **OFF**.

---

## Changelog

### Version 3.0.0-exp7
* **True W^X split-page stub:** Allocated separate `PAGE_READWRITE` data and `PAGE_EXECUTE_READ` code pages for the runtime stub, completely eliminating `PAGE_EXECUTE_READWRITE` (RWX) allocations and runtime `VirtualProtect` flips during settings reloads.
* **Multi-byte patch buffer fix:** Fixed a stack buffer overflow in `InstallHook` and `RemoveHook` when `site.hook_len > 8`, while preserving atomic 64-bit `InterlockedCompareExchange64` patching for the jump header.
* **Future-proof instruction decoding:** Added support for 32-bit length comparisons, `0x89` register moves, constant-propagated `mov r8d, <anchor_len>` immediates, memory-operand SSO checks (`cmp`/`cmov` on stack frame), and signed `cmov` variants (`CMOVGE`/`CMOVG`).
* **Strict non-volatile register validation:** Verified that `len_reg`, the `lea` frame base, and all SSO `cmp`/`cmov` source registers are non-volatile under the Win64 ABI so they cannot be clobbered before reaching the patch site.
* **Safe settings reclamation and unload bypass:** Reclaimed retired `ModSettings` generations once in-flight calls drain, separated C++ RAII from SEH `__try / __except`, and zeroed stub control flags prior to unpatching.
* **Input normalization and complete verbose logging:** Normalized user app entries (trimming quotes/whitespace, stripping accidental paths, appending `.exe` when omitted) and logged all foreground evaluation outcomes with deduplication when `verboseLogging` is active.

### Version 3.0.0-exp6
* **W^X memory protection:** Stub page transitions to `PAGE_EXECUTE_READ` (RX) after assembly; prevents persistent RWX pages.
* **Atomic 64-bit hook injection:** Patches applied and reverted atomically via `InterlockedCompareExchange64`, eliminating race conditions during hook installation and unhooking.
* **Auto multi-anchor fallback:** Added automatic discovery fallback across known browser anchors (`chrome.exe`, `msedge.exe`, `brave.exe`, `opera.exe`, `vivaldi.exe`) if the primary anchor fails.
* **Structured exception handling (SEH):** Wrapped decision handler in `__try / __except` to prevent host crashes if uninitialized string pointers are passed.
* **Configurable verbose logging:** Added `verboseLogging` setting to suppress log spam during routine window switching.

### Version 3.0.0-exp5
* **Dynamic anchor length:** Replaced hardcoded 11-byte check in `ResolveFromAnchorRef` with dynamic anchor string lengths, enabling custom anchors such as `chrome.exe`.
* **Unload thread safety:** Implemented atomic `g_in_flight` counter and drain-wait loop in `RemoveHook` to avoid crashes when Windhawk calls `FreeLibrary`.
* **Base register safety:** Added non-volatile register validation for the patch site base register to protect against register clobbering.
* **Extended frame support:** Increased LEA scanning range up to 8 bytes to handle large stack frames using 32-bit displacements (`disp32`).
* **SIB decoding correction:** Fixed `REX.X` validation in SIB decoding to prevent misidentifying `R12` as "no index register".
* **Strict section filtering:** Excluded writable data sections (`IMAGE_SCN_MEM_WRITE`) during anchor search to guarantee `.rdata` matching.
* **High-performance allocator:** Replaced linear 4 KB allocation loop with a `VirtualQuery`-based allocator adhering to Windows 64 KB allocation granularity (`dwAllocationGranularity`).
* **Cleanup:** Cleaned up filename matching in `glob_match`, marked foreign handler `noexcept`, and freed settings memory on uninit.

### Version 3.0.0-exp4
* Initial structural-discovery build using string cross-references instead of static byte signatures.
* Runtime relay stub generation copying host SSO triples and replaying overwritten instructions.
* Hex dumping around anchor references for unfamiliar agent layouts.

---

## Credits

* Original project and reverse-engineering: [igvk/LogiOptionsPlus-InMemoryPatching](https://github.com/igvk/LogiOptionsPlus-InMemoryPatching) (MIT License).
* Windhawk modding framework: [ramensoftware/windhawk](https://github.com/ramensoftware/windhawk).

## License

[MIT](LICENSE)
