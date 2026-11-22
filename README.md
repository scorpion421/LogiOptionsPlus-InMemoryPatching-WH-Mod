# Logi Options+ Smooth Scroll for All Apps (Structural-Discovery Build)

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Windhawk](https://img.shields.io/badge/Windhawk-mod-blue)](https://windhawk.net/)
[![Platform](https://img.shields.io/badge/platform-Windows%20x64-0078D6?logo=windows&logoColor=white)](#compatibility)
![Architecture](https://img.shields.io/badge/arch-amd64-lightgrey)
![Status](https://img.shields.io/badge/status-experimental%20(v3.0.0--exp9)-orange)
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
| **Self-healing fallback** | None (silent failure if signature misses) | **Multi-link fallback chain** across genuine `.rdata` browser anchors (`firefox.exe`, `chrome.exe`, `msedge.exe`, `iexplore.exe`) |
| **Hook concurrency** | Standard byte-by-byte write | **Single-qword atomic swap** (`InterlockedCompareExchange64`) with safe `> 8`-byte epilogue replay |
| **Memory security** | RWX relay pages | **True split-page W^X compliance** (`PAGE_READWRITE` data page + `PAGE_EXECUTE_READ` code page; zero RWX allocations) |
| **Pointer safety** | Basic null check | **Canonical Win64 pointer validation** (`0x10000` - `0x00007FFFFFFFFFFF` + length bounds, zero syscall overhead, MinGW `clang++` compatible) |
| **Unload safety** | Immediate unhook | **Stub bypass disarm + active in-flight counter** with safe settings retention and thread drain-wait |
| **Memory allocation** | Linear 4 KB probing | **64 KB-aligned `VirtualQuery` allocator** (instant startup, zero lag) |
| **Configuration** | Enabled / Disabled apps | Normalized Enabled / Disabled apps (zero-overhead fast path active by default) + **Anchor string** + **Deduplicated verbose logging** |

---

## The problem

Logitech mice feature high-resolution scroll wheels capable of pixel-smooth scrolling. However, Logi Options+ artificially restricts this behavior to a tiny list of hardcoded browser executables (primarily `msedge.exe`, `chrome.exe`, `iexplore.exe`, and `firefox.exe`). Every other application - including Chromium-based browsers like Vivaldi, Brave, and Opera, as well as editors, PDF viewers, and productivity tools - is restricted to coarse 3-line notches.

---

## How structural discovery works

Where the stable mod matches exact byte sequences, this build inspects instructions across every link of the `||` comparison chain:

```
[ Read-Only Data (.rdata) ]
  "firefox.exe" (or fallback anchor: chrome.exe, msedge.exe, iexplore.exe)
         |
         |  1. Locate anchor literal in .rdata
         v
[ Executable Code (.text) ]
  48 8D 4D 40           ->   lea rcx, [rbp+disp]     \   5. Isolate SSO triple (skipping any
  (optional frame loads)                              >     interleaved non-volatile frame loads)
  48 83 FE 0F           ->   cmp rsi, 15             |
  48 0F 47 CF           ->   cmova rcx, rdi          /
  48 83 FB 0A           ->   cmp rbx, anchor_len         3. Discover length check & non-volatile length register
  75 13                 ->   jne .next_or_mismatch       4a. Terminal link: jne -> convergence tail
  4C 8B C3              ->   mov r8, rbx                    (verified size arg load into R8)
  48 8D 15 [disp32]     ->   lea rdx, [rip+disp32]       2. Reference anchor literal
  E8 [rel32]            ->   call <compare>
  85 C0                 ->   test eax, eax
  74 6F                 ->   je .match_tail              4b. Intermediate link: je/jne -> convergence tail
        ...
.match_tail:
  B0 01                 ->   mov al, 1
  EB 02                 ->   jmp .store
.next_or_mismatch:
  32 C0                 ->   xor al, al              (converges here: store result)
.store:
  41 88 46 28           ->   mov [r14+0x28], al      <- 6. THE PATCH SITE
  49 8B 7E 08           ->   mov rdi, [r14+0x08]
```

1. **Locate anchor string:** Finds the null-terminated string literal in the main module's read-only data sections (`.rdata`).
2. **Find code reference:** Finds a RIP-relative `lea rdx, [rip+disp32]` in `.text` referencing the anchor.
3. **Trace length check:** Scans backward to identify `cmp <reg>, anchor_len` (64-bit or 32-bit) followed by a `jne`, confirming the same non-volatile register (or constant-propagated anchor length) is passed as the comparison size argument in `R8`.
4. **Follow terminal or match target:** Follows the `jne` branch for the terminal link in the chain, or the post-call `je`/`jne` match branch (`test eax, eax` / `test al, al`) for earlier links, landing in the convergence tail.
5. **Extract pointer setup:** Decodes the preceding Small String Optimization (SSO) triple (`lea rcx` / `cmp` / `cmov rcx`) across both register and stack-memory forms, isolating just those three instructions even when the compiler interleaves non-volatile frame register preloads, and verifies all referenced registers are non-volatile.
6. **Patch site validation:** Validates the target store (`mov [base+0x28], al`) and subsequent load (`mov reg, [base+0x08]`), ensuring `base` is a non-volatile register.

---

## Hardened safety and security

Deriving a patch address at runtime requires strict safety rails to ensure stability:

* **True split-page W^X memory protection:** The runtime trampoline uses two adjacent pages: Page 0 (`PAGE_READWRITE`) holds the control flags and target pointers, while Page 1 is written as `PAGE_READWRITE` and transitioned once to `PAGE_EXECUTE_READ` (RX) before hook installation. `PAGE_EXECUTE_READWRITE` (RWX) is never requested at allocation or runtime, satisfying strict Windows Exploit Guard and ACG policies.
* **Single-qword atomic injection with conflict checking:** Hooks are written and restored inside one single atomic 8-byte `InterlockedCompareExchange64` swap, verifying the prior 8-byte word on both install and unhook so concurrent modifications abort cleanly without clobbering third-party hooks. Even on 10-byte (`R12`) instruction pairs, trailing bytes after offset `8` are never modified in place and are replayed cleanly from the stub before jumping past the pair.
* **Strict non-volatile register verification:** All registers replayed at the patch site (`len_reg`, the `lea` frame base, and SSO `cmp`/`cmov` registers) are verified to be non-volatile under the Win64 ABI so they cannot be clobbered by intermediate function calls.
* **Canonical Win64 pointer validation:** The decision handler validates that incoming name pointers reside in canonical Win64 user space (`0x10000` through `0x00007FFFFFFFFFFF`) with bounded path lengths before reading, requiring zero system calls on the hot path and compiling cleanly under Windhawk's MinGW `clang++` toolchain.
* **Stub-resident unload synchronization:** When the mod is disabled, the stub control flags are zeroed first so newly arriving threads take the fast bypass path, and the original instructions are restored atomically. An active invocation counter inside Page 0 of the permanently mapped stub is incremented before entering the mod DLL and decremented after returning to Page 1, paired with a brief post-unpatch grace period so `FreeLibrary` never unmaps code while an agent thread is executing inside it. Retired settings generations remain allocated while handler invocations remain in flight.
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

* **Additional applications (`enabledApps`)**: Executable filenames (without path) to enable smooth scrolling for. Wildcards `*` and `?` are supported. Quotes, whitespace, and accidental directory paths are stripped automatically, `.exe` is appended if omitted, and blank rows left ahead of populated items are tolerated.
* **Excluded applications (`disabledApps`)**: Executable filenames to explicitly disable smooth scrolling for. Overrides both the additional list and Logitech's built-in detection. Empty by default so natively detected browsers run on the zero-overhead pass-through stub path without invoking C++.
* **Anchor string (`anchorString`)**: The literal used on mod startup to locate the patch site (default: `firefox.exe`; `chrome.exe`, `msedge.exe`, and `iexplore.exe` are also supported and tried automatically as fallbacks). Only change this and re-enable the mod if the log reports that discovery failed.
* **Verbose logging (`verboseLogging`)**: When enabled (`true`), logs every distinct window evaluation outcome (disabled match, built-in detection, enabled match, and unmatched) with burst deduplication to the Windhawk log. Keep disabled (`false`) for normal use.

### Priority order

1. **Excluded match** -> Smooth scrolling **OFF** (overrides everything).
2. **Logitech native detection** -> Smooth scrolling **ON** (preserves built-in browser support).
3. **Additional match** -> Smooth scrolling **ON**.
4. **Otherwise** -> Smooth scrolling **OFF**.

---

## Changelog

### Version 3.0.0-exp9
* **Stub-resident unload synchronization:** Moved the active invocation counter into the runtime stub's permanently mapped data page (incremented right before calling into the mod DLL and decremented after returning) and added a post-unpatch grace period, closing the unload race window during `FreeLibrary`.
* **Blank-entry resilience in app lists:** Tolerated consecutive blank rows when reading Windhawk string arrays so leaving the default empty slot in `disabledApps` blank while adding exclusions below it no longer truncates the list.
* **Compare-exchange conflict checks:** Verified `InterlockedCompareExchange64` return values on both hook installation and removal so concurrent changes abort safely without clobbering external hooks.
* **Clean init failure lifecycle:** Freed freshly loaded settings when startup discovery fails and removed the unreachable settings-change retry branch from `Wh_ModSettingsChanged`.

### Version 3.0.0-exp8
* **Multi-link chain discovery:** Resolved both intermediate (`je`/`jne` after `call`) and final (`jne` on length check) browser comparison links across authentic fallback anchors (`firefox.exe`, `chrome.exe`, `msedge.exe`, `iexplore.exe`), cleanly isolating SSO instructions even when frame preloads are interleaved in the first link.
* **Fast-path activation out of the box:** Removed the legacy `iexplore.exe` default from `disabledApps` so natively detected browsers run on the zero-overhead pass-through stub path by default.
* **Single-qword atomic patching:** Kept patch installation and removal strictly inside a single atomic 8-byte `InterlockedCompareExchange64` swap without touching trailing bytes on 10-byte (`R12`) hook sites.
* **Unload hardening:** Avoided deleting retired settings if any handler thread remains active during unload (the unreachable settings-change retry path added here was later cleaned up in `3.0.0-exp9`).
* **Toolchain and documentation cleanup:** Removed the obsolete `-fms-extensions` compiler flag and aligned safety documentation with canonical Win64 pointer validation.

### Version 3.0.0-exp7
* **True W^X split-page stub:** Allocated separate `PAGE_READWRITE` data and `PAGE_EXECUTE_READ` code pages for the runtime stub, completely eliminating `PAGE_EXECUTE_READWRITE` (RWX) allocations and runtime `VirtualProtect` flips during settings reloads.
* **Multi-byte patch buffer fix:** Fixed a stack buffer overflow in `InstallHook` and `RemoveHook` when `site.hook_len > 8`, while preserving atomic 64-bit `InterlockedCompareExchange64` patching for the jump header.
* **Future-proof instruction decoding:** Added support for 32-bit length comparisons, `0x89` register moves, constant-propagated `mov r8d, <anchor_len>` immediates, memory-operand SSO checks (`cmp`/`cmov` on stack frame), and signed `cmov` variants (`CMOVGE`/`CMOVG`).
* **Strict non-volatile register validation:** Verified that `len_reg`, the `lea` frame base, and all SSO `cmp`/`cmov` source registers are non-volatile under the Win64 ABI so they cannot be clobbered before reaching the patch site.
* **Safe settings reclamation and unload bypass:** Reclaimed retired `ModSettings` generations once in-flight calls drain, replaced MSVC-only SEH with canonical Win64 user-space pointer and length validation for MinGW `clang++` compatibility, and zeroed stub control flags prior to unpatching.
* **Input normalization and complete verbose logging:** Normalized user app entries (trimming quotes/whitespace, stripping accidental paths, appending `.exe` when omitted) and logged all foreground evaluation outcomes with deduplication when `verboseLogging` is active.

### Version 3.0.0-exp6
* **W^X memory protection:** Stub page transitions to `PAGE_EXECUTE_READ` (RX) after assembly; prevents persistent RWX pages.
* **Atomic 64-bit hook injection:** Patches applied and reverted atomically via `InterlockedCompareExchange64`, eliminating race conditions during hook installation and unhooking.
* **Auto multi-anchor fallback:** Added automatic discovery fallback across known browser anchors if the primary anchor fails.
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
