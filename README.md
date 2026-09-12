# Logi Options+ Smooth Scroll for All Apps (Structural-Discovery Build)

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Windhawk](https://img.shields.io/badge/Windhawk-mod-blue)](https://windhawk.net/)
[![Platform](https://img.shields.io/badge/platform-Windows%20x64-0078D6?logo=windows&logoColor=white)](#compatibility)
![Architecture](https://img.shields.io/badge/arch-amd64-lightgrey)
![Status](https://img.shields.io/badge/status-experimental%20(v3.0.0--exp6)-orange)
[![Stable build](https://img.shields.io/badge/stable%20build-main%20branch-brightgreen)](https://github.com/scorpion421/LogiOptionsPlus-InMemoryPatching-WH-Mod/tree/main)

> **About this branch:** This is the next-generation **structural-discovery** build. While the [`main` branch](https://github.com/scorpion421/LogiOptionsPlus-InMemoryPatching-WH-Mod/tree/main) relies on hardcoded byte signatures extracted by hand for each Logi Options+ release, this build dynamically derives the patch site from code structure and generates its relay trampoline at runtime.
>
> Functionally, both builds achieve the same goal: pixel-smooth scrolling in any user-selected application without losing native Logi Options+ browser detection.

---

## Key Differences: `main` (Stable) vs. `experimental`

| Feature | `main` Branch (Stable) | `experimental` Branch (Structural Discovery) |
|---|---|---|
| **Patch Site Detection** | Hardcoded byte signatures per version (1.00 – 1.94, 2.06) | **Dynamic structural discovery** via string cross-references (`firefox.exe`, `chrome.exe`, etc.) |
| **Relay Trampoline** | Hand-written assembly handlers per version | **Dynamic runtime codegen** assembled specifically for the host layout |
| **Logi Options+ Updates** | Breaks on new agent releases until signatures are hand-extracted | **Resilient to updates:** adapts automatically without code changes |
| **Self-Healing Fallback** | None (silent failure if signature misses) | **Multi-anchor fallback chain** across 6 known browser signatures |
| **Hook Concurrency** | Standard byte-by-byte write | **Atomic 64-bit swap** (`InterlockedCompareExchange64`) |
| **Memory Security** | RWX relay pages | **Full W^X compliance** (`PAGE_EXECUTE_READ` / RX enforcement) |
| **Crash Protection** | None | **SEH boundary** (`__try / __except`) catches invalid host pointers |
| **Unload Safety** | Immediate unhook | **Active in-flight counter** with thread drain-wait before `FreeLibrary` |
| **Memory Allocation** | Linear 4 KB probing | **64 KB-aligned `VirtualQuery` allocator** (instant startup, zero lag) |
| **Configuration** | Enabled / Disabled apps | Enabled / Disabled apps + **Anchor string** + **Verbose logging toggle** |

---

## The Problem

Logitech mice feature high-resolution scroll wheels capable of pixel-smooth scrolling. However, Logi Options+ artificially restricts this behavior to a tiny list of hardcoded browser executables (primarily `chrome.exe` and `firefox.exe`). Every other application — including Chromium-based browsers like Vivaldi, Brave, Opera, Edge, as well as editors, PDF viewers, and productivity tools — is restricted to coarse 3-line notches.

---

## How Structural Discovery Works

Where the stable mod matches exact byte sequences, this build inspects instructions:

```
[ Read-Only Data (.rdata) ]
  "firefox.exe" (or fallback anchor: chrome.exe, msedge.exe, etc.)
        ▲
        │  1. Locate anchor literal in .rdata
        │
[ Executable Code (.text) ]
  48 8D 15 [disp32]   ->   lea rdx, [rip+disp32]   (arg setup for browser comparison)
        ▲
        │  2. Scan backwards within a 48-byte window
        │
  48 8D 4C 24 30      ->   lea rcx, [rsp+disp]     \
  48 83 FE 0F         ->   cmp rsi, 15              >  5. Extract Small String Optimization (SSO) triple
  48 0F 43 CE         ->   cmovae rcx, rsi         /
  49 83 FE 0B         ->   cmp r14, 11 (anchor_len)   3. Discover length check & length register
  75 18               ->   jne .mismatch              4. Follow branch to result convergence
        ...
.mismatch:
  30 C0               ->   xor al, al              (converges here: store result)
  88 43 28            ->   mov [rbx+0x28], al      <- 6. THE PATCH SITE
  48 8B 5B 08         ->   mov rbx, [rbx+0x08]
```

1. **Locate Anchor String:** Finds the null-terminated string literal in the main module's read-only data sections (`.rdata`).
2. **Find Code Reference:** Finds a RIP-relative `lea rdx, [rip+disp32]` in `.text` referencing the anchor.
3. **Trace Length Check:** Scans backward to identify `cmp <reg>, anchor_len` followed by a `jne`, confirming the same register is passed as the comparison size argument (`mov r8, <reg>`).
4. **Follow Branch Target:** Follows the branch to the convergence point where the match/mismatch paths store the boolean verdict.
5. **Extract Pointer Setup:** Decodes the preceding Small String Optimization (SSO) triple (`lea rcx` / `cmp` / `cmov rcx`) to discover how the host locates string buffers (both inline SSO stack buffers and heap pointers).
6. **Patch Site Validation:** Validates the target store (`mov [base+0x28], al`) and subsequent load (`mov reg, [base+0x08]`), ensuring `base` is a non-volatile register.

---

## Hardened Safety & Security

Deriving a patch address at runtime requires strict safety rails to ensure stability:

* **Atomic 64-Bit Injection:** Hooks are written and removed using `InterlockedCompareExchange64`. Other threads in `logioptionsplus_agent.exe` will never encounter partially-written instructions.
* **W^X Memory Protection:** The dynamically generated trampoline page is transitioned to `PAGE_EXECUTE_READ` (RX). Persistent `PAGE_EXECUTE_READWRITE` (RWX) allocations are prohibited, satisfying Windows Exploit Guard and ACG.
* **Structured Exception Handling (SEH):** The decision handler is guarded by `__try / __except`. If Logi Options+ ever passes an invalid string pointer, the exception is caught safely and falls back to the native verdict rather than crashing the process.
* **Drain-Wait on Unload:** When the mod is disabled, `RemoveHook()` restores the original instructions and waits for all in-flight threads to exit the handler before Windhawk unloads the DLL.
* **Non-Volatile Register Safety:** The patch site base register is verified to be non-volatile (e.g. `RBX`, `RBP`, `RSI`, `RDI`, `R12`–`R15`), preventing ABI register clobbering.
* **Strict Section Filtering:** Writable sections (`IMAGE_SCN_MEM_WRITE`) are excluded from anchor searches to ensure only genuine `.rdata` string constants are matched.
* **Single Candidate Rule:** If more than one candidate survives validation, discovery aborts rather than guesses.

---

## Installation

1. Install [Windhawk](https://windhawk.net/) if you have not already done so.
2. Ensure Logi Options+ is installed and its background agent (`logioptionsplus_agent.exe`) is running.
3. In the Windhawk UI, click **Create Mod**, paste the code from [`logioptionsplus-smooth-scroll-EXPERIMENTAL.wh.cpp`](logioptionsplus-smooth-scroll-EXPERIMENTAL.wh.cpp), and compile.
4. Enable the mod. Logi Options+ is patched in memory immediately — no agent or system restart required.

> **Note:** Do not run this mod concurrently with the stable version (`logioptionsplus-smooth-scroll.wh.cpp`), as both target the same logic.

---

## Configuration

Settings can be customized directly in the Windhawk Mod Settings UI:

* **Additional applications (`enabledApps`)**: Executable filenames (without path) to enable smooth scrolling for. Wildcards `*` and `?` are supported.
* **Excluded applications (`disabledApps`)**: Executable filenames to explicitly disable smooth scrolling for. Overrides both the additional list and Logitech's built-in detection.
* **Anchor string (`anchorString`)**: The literal used to locate the patch site (default: `firefox.exe`). Any valid browser string works.
* **Verbose logging (`verboseLogging`)**: When enabled (`true`), logs every window evaluation to the Windhawk log. Keep disabled (`false`) for optimal performance.

### Priority Order

1. **Excluded Match** -> Smooth scrolling **OFF** (overrides everything).
2. **Logitech Native Detection** -> Smooth scrolling **ON** (preserves built-in browser support).
3. **Additional Match** -> Smooth scrolling **ON**.
4. **Otherwise** -> Smooth scrolling **OFF**.

---

## Changelog

### Version 3.0.0-exp6
* **W^X Memory Protection:** Stub page transitions to `PAGE_EXECUTE_READ` (RX) after assembly; prevents persistent RWX pages.
* **Atomic 64-Bit Hook Injection:** Patches applied and reverted atomically via `InterlockedCompareExchange64`, eliminating race conditions during hook installation and unhooking.
* **Auto Multi-Anchor Fallback:** Added automatic discovery fallback across known browser anchors (`chrome.exe`, `msedge.exe`, `brave.exe`, `opera.exe`, `vivaldi.exe`) if the primary anchor fails.
* **Structured Exception Handling (SEH):** Wrapped decision handler in `__try / __except` to prevent host crashes if uninitialized string pointers are passed.
* **Configurable Verbose Logging:** Added `verboseLogging` setting to suppress log spam during routine window switching.

### Version 3.0.0-exp5
* **Dynamic Anchor Length:** Replaced hardcoded 11-byte check in `ResolveFromAnchorRef` with dynamic anchor string lengths, enabling custom anchors such as `chrome.exe`.
* **Unload Thread Safety:** Implemented atomic `g_in_flight` counter and drain-wait loop in `RemoveHook` to avoid crashes when Windhawk calls `FreeLibrary`.
* **Base Register Safety:** Added non-volatile register validation for the patch site base register to protect against register clobbering.
* **Extended Frame Support:** Increased LEA scanning range up to 8 bytes to handle large stack frames using 32-bit displacements (`disp32`).
* **SIB Decoding Correction:** Fixed `REX.X` validation in SIB decoding to prevent misidentifying `R12` as "no index register".
* **Strict Section Filtering:** Excluded writable data sections (`IMAGE_SCN_MEM_WRITE`) during anchor search to guarantee `.rdata` matching.
* **High-Performance Allocator:** Replaced linear 4 KB allocation loop with a `VirtualQuery`-based allocator adhering to Windows 64 KB allocation granularity (`dwAllocationGranularity`).
* **Cleanup:** Cleaned up filename matching in `glob_match`, marked foreign handler `noexcept`, and freed settings memory on uninit.

### Version 3.0.0-exp4
* Initial structural-discovery build using string cross-references instead of static byte signatures.
* Runtime relay stub generation copying host SSO triples and replaying overwritten instructions.
* Hex dumping around anchor references for unfamiliar agent layouts.

---

## Credits

* Original project & reverse-engineering: [igvk/LogiOptionsPlus-InMemoryPatching](https://github.com/igvk/LogiOptionsPlus-InMemoryPatching) (MIT License).
* Windhawk modding framework: [ramensoftware/windhawk](https://github.com/ramensoftware/windhawk).

## License

[MIT](LICENSE)
