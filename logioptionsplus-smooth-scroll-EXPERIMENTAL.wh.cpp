// ==WindhawkMod==
// @id              logioptionsplus-smooth-scroll-experimental
// @name            Logi Options+ Smooth Scroll for All Apps (experimental)
// @description     Structural-discovery build. Locates the patch site by string cross-reference instead of hardcoded signatures, and generates its trampoline at runtime.
// @version         3.0.0-exp9
// @author          MickyFoley
// @github          https://github.com/scorpion421
// @include         logioptionsplus_agent.exe
// @architecture    amd64
// @license         MIT
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Logi Options+ Smooth Scroll for All Apps (experimental)

**This is an experimental build. Use the stable mod unless you are testing this
one deliberately.**

Functionally identical to the stable version: it enables high-resolution smooth
scrolling for applications you list, and never removes what Logi Options+
already detects on its own.

What differs is how it finds the place to patch.

## Why

The stable build carries one hardcoded byte signature per agent version. Every
Logi Options+ release that touches the relevant function needs a new signature,
extracted by hand, plus a new assembly handler. Until then the mod is silently
inert.

This build derives the patch site from the structure of the code instead.

## How it works

1. Locate a known browser literal (default `firefox.exe`, or an automatic
   fallback anchor) in the agent's read-only data.
2. Scan the agent's code for a RIP-relative `lea rdx, [rip+disp32]` that resolves
   to that literal. This is the argument setup for the comparison against the
   browser name.
3. Before it, find the length pre-check the compiler emits ahead of the
   comparison: `cmp <reg>, <anchor_len>` followed by a `jne`, with the same
   register (or constant-propagated immediate) handed to `r8` as the size
   argument. The agent chains several such comparisons (`msedge.exe`,
   `chrome.exe`, `iexplore.exe`, `firefox.exe`); the anchor's own length selects
   the right link.
4. Locate the verdict convergence tail: for the final link in the chain, the
   length-mismatch `jne` jumps straight to the tail; for earlier links, the
   post-call match branch (`je`/`jne` after `test eax, eax` or `test al, al`)
   jumps to the convergence tail where the boolean verdict is stored.
5. Before the length check sits the small-string-optimization triple
   (`lea rcx, [frame+disp]` / `cmp <reg_or_mem>, 15 or 16` / `cmov rcx, <reg_or_mem>`)
   that produces the pointer to the name, cleanly extracted even when the
   compiler interleaves non-volatile frame register preloads in the first link.

Nothing above depends on exact byte values. Displacements, register allocation,
chain position, and the encodings of the SSO check all vary across agent
versions, and all are decoded rather than matched.

## Runtime trampoline

The relay stub is assembled at runtime from what discovery found: the isolated
SSO triple instructions are copied into the stub, the length register is
encoded into a `mov rdx`, and the instructions being overwritten are copied out
before the patch is applied and replayed inside the stub. One generic path
replaces the six hand-written assembly handlers, so a new agent version costs
no code.

## Safety

Deriving an address is more dangerous than matching one: a wrong signature match
simply fails, while a wrong derivation patches into unrelated code. Every
candidate must therefore satisfy all of the following before a single byte is
written:

- the string is in a read-only section of the main module, the code in an
  executable one,
- the length check, the `jne`, and the `r8` size argument decode exactly as
  expected, and both the terminal length-mismatch branch and any intermediate
  post-call match branch are validated inside the same executable section,
- the SSO triple decodes to `lea` into RCX and `cmov` into RCX using only
  non-volatile registers, isolating those three instructions even when frame
  register preloads are interleaved,
- within a short window at the convergence target there is `mov [<base>+0x28], al`
  followed by `mov <reg>, [<base>+0x8]`, both through the same non-volatile base
  register,
- the overwritten range is at least 5 bytes so an `E9` fits, while patching on
  disk stays inside a single atomic 8-byte compare-exchange swap,
- and exactly one candidate survives. Ambiguity aborts.

If anything fails, the mod does nothing and says why in the log. When an anchor
reference cannot be resolved, the bytes around it are dumped so an unfamiliar
layout can be read out of the log rather than guessed at.

## Verification

The discovery and code generation were exercised against reconstructions of all
six known agent layouts (1.00 through 2.06), with no version knowledge supplied:

- discovery recovered the correct patch site, patch length, setup sequence and
  length register in every case,
- the generated stub was executed for real and checked for correct arguments in
  both the small-string and heap cases, correct storage of the result, and
  preservation of R9, R10, R11 and RFLAGS,
- eleven deliberately corrupted variants (wrong compared length, mismatched
  registers, altered branch, wrong store target or offset, and others) were all
  refused rather than patched,
- and against shipping Logi Options+ binaries (2.06 through 2.9.994631), all
  four browser anchors (`firefox.exe`, `chrome.exe`, `msedge.exe`,
  `iexplore.exe`) independently resolve to the exact same patch site and SSO
  setup.

RFLAGS are preserved exactly on the pass-through path. On the deciding path they
are clobbered, as they were in every previous version and upstream: two code
paths converge at the patch site, so nothing downstream can depend on them.

## Changelog

### 3.0.0-exp9
- **Stub-resident unload synchronization:** Moved the active call counter into the stub's permanently mapped data page (incremented before calling into the mod DLL and decremented after returning) and added a brief post-unpatch grace period, closing the unload race window during `FreeLibrary`.
- **Blank-entry resilience in app lists:** Tolerated consecutive blank rows when reading Windhawk string arrays so leaving the default empty slot in `disabledApps` blank and adding entries below it no longer truncates the list.
- **Compare-exchange conflict checks:** Verified the return value of `InterlockedCompareExchange64` during both hook installation and removal so concurrent modifications abort cleanly without clobbering third-party hooks.
- **Clean init failure lifecycle:** Freed freshly allocated settings if startup discovery fails and removed the unreachable settings-change retry branch.

### 3.0.0-exp8
- **Multi-link chain discovery:** Resolved both intermediate and final browser comparison links across multi-anchor fallbacks (`firefox.exe`, `chrome.exe`, `msedge.exe`, `iexplore.exe`), including clean extraction of SSO instructions when the first link interleaves non-volatile frame preloads.
- **Fast-path activation out of the box:** Removed the legacy `iexplore.exe` entry from default excluded applications so natively detected browsers immediately take the zero-overhead pass-through stub path unless exclusions are explicitly configured.
- **Single-qword atomic patching:** Kept patch installation and removal strictly inside a single atomic 8-byte `InterlockedCompareExchange64` swap, eliminating partial-instruction tearing on 10-byte (`R12`) hook sites.
- **Unload hardening:** Kept retired settings alive if a handler invocation is still running at unload (the unreachable settings-change retry path added here was later cleaned up in `3.0.0-exp9`).
- **Toolchain and documentation cleanup:** Removed the obsolete `-fms-extensions` compiler flag and aligned safety documentation with canonical Win64 pointer validation.

### 3.0.0-exp7
- **True W^X split-page stub:** Allocated separate `PAGE_READWRITE` data and `PAGE_EXECUTE_READ` code pages for the runtime stub, completely eliminating `PAGE_EXECUTE_READWRITE` (RWX) allocations and runtime `VirtualProtect` flips during settings reloads.
- **Multi-byte patch buffer fix:** Fixed a stack buffer overflow in `InstallHook` and `RemoveHook` when `site.hook_len > 8`, while preserving atomic 64-bit `InterlockedCompareExchange64` patching for the jump header.
- **Future-proof instruction decoding:** Added support for 32-bit length comparisons, `0x89` register moves, constant-propagated `mov r8d, <anchor_len>` immediates, memory-operand SSO checks (`cmp`/`cmov` on stack frame), and signed `cmov` variants (`CMOVGE`/`CMOVG`).
- **Strict non-volatile register validation:** Verified that `len_reg`, the `lea` frame base, and all SSO `cmp`/`cmov` source registers are non-volatile under the Win64 ABI so they cannot be clobbered before reaching the patch site.
- **Safe settings reclamation and unload bypass:** Reclaimed retired `ModSettings` generations once in-flight calls drain, replaced MSVC-only SEH with canonical Win64 user-space pointer and length validation for MinGW `clang++` compatibility, and zeroed stub control flags prior to unpatching.
- **Input normalization and complete verbose logging:** Normalized user app entries (trimming quotes/whitespace, stripping accidental paths, appending `.exe` when omitted) and logged all foreground evaluation outcomes with deduplication when `verboseLogging` is active.

### 3.0.0-exp6
- **W^X memory protection:** Stub page transitions to `PAGE_EXECUTE_READ` (RX) after assembly; avoids persistent RWX pages.
- **Atomic 64-bit hook injection:** Patches applied and reverted atomically via `InterlockedCompareExchange64`, eliminating race conditions during hook installation and unhooking.
- **Auto multi-anchor fallback:** Added automatic discovery fallback across known browser anchors if the primary anchor fails.
- **Structured exception handling (SEH):** Protected decision handler with `__try / __except` to prevent process crashes if host string pointers are invalid.
- **Configurable verbose logging:** Added `verboseLogging` setting to keep the Windhawk log clean during normal window switching.

### 3.0.0-exp5
- **Dynamic anchor length:** Fixed an issue where changing `anchorString` to an identifier with a length other than 11 characters (e.g. `chrome.exe`) would fail discovery.
- **Unload thread safety:** Added an in-flight invocation counter and drain-wait loop to prevent access violations during `FreeLibrary` on mod unload.
- **Base register safety:** Added explicit non-volatile register validation for the patch site base register, preventing potential clobbering by the handler calling convention.
- **Extended frame support:** Increased LEA scanning range up to 8 bytes to support large stack frames using 32-bit displacements (`disp32`).
- **SIB decoding correction:** Fixed `REX.X` validation in SIB decoding to prevent misidentifying `R12` as "no index register".
- **Strict section filtering:** Excluded writable data sections (`IMAGE_SCN_MEM_WRITE`) from the anchor string literal search to ensure only genuine `.rdata` is matched.
- **High-performance allocation:** Replaced linear 4 KB allocation probing with a `VirtualQuery`-based allocator adhering to Windows 64 KB allocation granularity (`dwAllocationGranularity`), eliminating startup lag.
- **Cleanup:** Cleaned up filename matching in `glob_match`, marked the foreign process handler `noexcept`, and properly freed settings memory on uninit.

## Credits

Original project and reverse-engineering work:
https://github.com/igvk/LogiOptionsPlus-InMemoryPatching by igvk (MIT License)
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- enabledApps:
    - vivaldi.exe
    - brave.exe
    - opera.exe
    - msedge.exe
    - notepad.exe
  $name: Additional applications
  $description: Executable filenames (without path) to enable smooth scrolling for. Wildcards * and ? are supported.
- disabledApps:
    - ""
  $name: Excluded applications
  $description: Executable filenames to explicitly disable smooth scrolling for. Overrides both the additional apps list and the built-in Logi Options+ browser detection. Wildcards * and ? are supported.
- anchorString: firefox.exe
  $name: Anchor string
  $description: The browser name literal used to locate the patch site (firefox.exe, chrome.exe, msedge.exe, and iexplore.exe are supported as automatic fallbacks). Used on mod startup; only change this if the log reports that discovery failed.
- verboseLogging: false
  $name: Verbose logging
  $description: Log every window match event to the Windhawk log. Useful for debugging pattern rules.
*/
// ==/WindhawkModSettings==

// ===========================================================================
// FILE MAP
//
//   SECTION: settings state      - ModSettings, atomic publication, ascii_lower
//   SECTION: matching            - glob_match
//   SECTION: decision logic      - the handler the generated stub calls
//   SECTION: module layout       - PE section enumeration of the host process
//   SECTION: decoding            - the narrow x86-64 decoders discovery needs
//   SECTION: discovery           - string xref -> patch site
//   SECTION: codegen             - runtime assembly of the relay stub
//   SECTION: install / remove    - applying and reverting the patch
//   SECTION: settings loader     - reading Windhawk settings
//   SECTION: windhawk callbacks
//
// ENTRY POINT: Wh_ModInit -> LoadSettings -> DiscoverSite -> InstallHook
//
// ASSUMPTIONS
//   - x86-64 only. Every decoder assumes 64-bit encodings.
//   - The host is the Logi Options+ agent and the relevant code lives in the
//     main module.
//   - The generated stub is executed on agent threads, reached by a jump from
//     inside a function, not by a call. It is not ABI-callable.
// ===========================================================================

#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <new>
#include <string>
#include <vector>

// ===========================================================================
// SECTION: settings state
// ===========================================================================

struct ModSettings {
    std::vector<std::string> enabled;   // additional apps (lowercase patterns)
    std::vector<std::string> disabled;  // excluded apps  (lowercase patterns)
    bool verbose = false;
};

// Published through an atomic pointer rather than held by value.
//
// The decision handler runs on the agent's threads while Wh_ModSettingsChanged
// runs on a Windhawk thread. Replacing a by-value ModSettings would destroy
// vectors a handler might be iterating at that moment. Swapping a pointer is
// atomic and keeps the hot path lock-free.
static std::atomic<const ModSettings*> g_settings{nullptr};

// Retired settings waiting for in-flight handlers to finish before deletion.
static std::vector<const ModSettings*> g_retired_settings;

// Pointer to the 32-bit in-flight counter inside Page 0 (RW) of the stub.
// Incremented by the stub immediately before calling into this DLL and
// decremented after returning to the permanently mapped code page.
static volatile LONG* g_stub_in_flight = nullptr;

static inline LONG GetInFlightCount() noexcept
{
    return g_stub_in_flight ? InterlockedCompareExchange(g_stub_in_flight, 0, 0) : 0;
}

// Last verbose log signature to avoid flooding the log during fullscreen
// transitions that emit bursts of identical foreground events.
static std::atomic<uint64_t> g_last_verbose_sig{0};

// ASCII lowercase. Deliberately not std::tolower, which consults the current
// locale and is a real call. Executable names are ASCII.
static inline char ascii_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

// ===========================================================================
// SECTION: matching
//
// PRECONDITION: both arguments are already lowercase. The caller lowercases the
// name once into a stack buffer and strips any path separators.
// ===========================================================================

static bool glob_match(const char* text, const char* glob)
{
    const char* text_backup = nullptr;
    const char* glob_backup = nullptr;
    while (*text != '\0') {
        if (*glob == '*') {
            text_backup = text;
            glob_backup = ++glob;
        } else if (*glob == '?' || *glob == *text) {
            text++;
            glob++;
        } else {
            if (!glob_backup)
                return false;
            text = ++text_backup;
            glob = glob_backup;
        }
    }
    while (*glob == '*')
        glob++;
    return *glob == '\0';
}

// ===========================================================================
// SECTION: decision logic
//
// Called from the generated stub with the Win64 argument registers already set
// up by the stub: RCX = name, RDX = length, R8B = the agent's own verdict.
// Returns in AL, which the stub stores where the agent expected its own result.
//
// Allocation-free by design: this runs on every foreground change, including
// the burst Windows emits during a fullscreen transition.
//
// Priority:
//   1. excluded match -> false (overrides everything)
//   2. agent's own verdict -> true (native detection preserved)
//   3. additional match -> true
//   4. otherwise -> false
// ===========================================================================

static constexpr size_t k_name_buf = 260;

static inline bool ShouldLogVerbose(const char* name, uint8_t outcome) noexcept
{
    uint64_t h = 14695981039346656037ULL;
    for (const char* p = name; *p != '\0'; ++p) {
        h ^= static_cast<uint8_t>(*p);
        h *= 1099511628211ULL;
    }
    h ^= outcome;
    h *= 1099511628211ULL;
    const uint64_t prev = g_last_verbose_sig.exchange(h, std::memory_order_relaxed);
    return prev != h;
}

extern "C" bool patched_switch_foreground_process_handler(
    const char* name, size_t length, bool previous_check) noexcept
{
    const ModSettings* s = g_settings.load(std::memory_order_acquire);
    const uintptr_t addr = reinterpret_cast<uintptr_t>(name);
    // Validate canonical Win64 user-space pointer range and sane path length.
    if (!s || addr < 0x10000ULL || addr > 0x00007FFFFFFFFFFFULL || length == 0 || length > 32768)
        return previous_check;   // never worse than the agent's own answer

    const bool have_disabled = !s->disabled.empty();
    const bool have_enabled  = !s->enabled.empty();

    // Mirror of the stub's bypass conditions when verbose logging is off.
    if (!s->verbose && !have_disabled) {
        if (previous_check)
            return true;
        if (!have_enabled)
            return false;
    }

    const char* base     = name;
    size_t      base_len = length;
    for (size_t i = length; i > 0; i--) {
        const char c = name[i - 1];
        if (c == '\\' || c == '/') {
            base     = name + i;
            base_len = length - i;
            break;
        }
    }

    char buf[k_name_buf];
    if (base_len >= sizeof buf)
        base_len = sizeof buf - 1;
    for (size_t i = 0; i < base_len; i++)
        buf[i] = ascii_lower(base[i]);
    buf[base_len] = '\0';

    for (const auto& glob : s->disabled) {
        if (glob_match(buf, glob.c_str())) {
            if (s->verbose && ShouldLogVerbose(buf, 1)) {
                Wh_Log(L"handler: '%S' matched disabled '%S' -> false", buf, glob.c_str());
            }
            return false;
        }
    }
    if (previous_check) {
        if (s->verbose && ShouldLogVerbose(buf, 2)) {
            Wh_Log(L"handler: '%S' matched built-in browser detection -> true", buf);
        }
        return true;
    }
    for (const auto& glob : s->enabled) {
        if (glob_match(buf, glob.c_str())) {
            if (s->verbose && ShouldLogVerbose(buf, 3)) {
                Wh_Log(L"handler: '%S' matched enabled '%S' -> true", buf, glob.c_str());
            }
            return true;
        }
    }
    if (s->verbose && ShouldLogVerbose(buf, 4)) {
        Wh_Log(L"handler: '%S' did not match any rule -> false", buf);
    }
    return false;
}

// ===========================================================================
// SECTION: module layout
//
// Discovery is confined to the main module. Scanning every committed page, as
// the signature build does, would widen the search for no benefit and increase
// the chance of a coincidental match in unrelated code.
// ===========================================================================

struct Section {
    uint8_t* base  = nullptr;
    size_t   size  = 0;
    bool     exec  = false;
    bool     read  = false;
    bool     write = false;
};

// Enumerates the sections of the main executable.
// Returns false if the PE headers do not look sane.
static bool GetMainModuleSections(std::vector<Section>& out, uint8_t*& mod_base)
{
    out.clear();

    auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    if (!base)
        return false;
    mod_base = base;

    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return false;

    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return false;
    if (nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64)
        return false;

    auto* sec = IMAGE_FIRST_SECTION(nt);
    const unsigned count = nt->FileHeader.NumberOfSections;
    for (unsigned i = 0; i < count; i++) {
        const auto& s = sec[i];
        if (s.VirtualAddress == 0 || s.Misc.VirtualSize == 0)
            continue;
        Section e;
        e.base  = base + s.VirtualAddress;
        e.size  = s.Misc.VirtualSize;
        e.exec  = (s.Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
        e.read  = (s.Characteristics & IMAGE_SCN_MEM_READ) != 0;
        e.write = (s.Characteristics & IMAGE_SCN_MEM_WRITE) != 0;
        out.push_back(e);
    }
    return !out.empty();
}

// ===========================================================================
// SECTION: decoding
//
// Only the handful of encodings discovery actually needs. Each returns false on
// anything unexpected rather than guessing -- a wrong decode here would put the
// patch in the wrong place.
// ===========================================================================

// Extracts a register number from a REX prefix and a ModRM/opcode field.
static inline unsigned reg_of(uint8_t rex, unsigned field3, uint8_t rex_bit)
{
    return field3 | ((rex & rex_bit) ? 8u : 0u);
}

// True if 'reg' is non-volatile under the Win64 ABI (RBX, RSP, RBP, RSI, RDI,
// R12-R15). Any register read by our stub at the patch site must be non-volatile
// so it survives the function calls in the comparison chain.
static inline bool is_nonvolatile_reg(unsigned reg)
{
    return reg == 3 || reg == 4 || reg == 5 || reg == 6 || reg == 7 ||
           (reg >= 12 && reg <= 15);
}

// Decodes a ModRM (+ optional SIB + displacement) memory operand that uses a
// single base register with no index register and is not RIP-relative.
// Returns the total byte length from the ModRM byte onward, or 0 on failure.
static size_t DecodeBaseDispMem(uint8_t rex, const uint8_t* modrm_ptr, size_t avail,
                                unsigned& base, int& disp, bool allow_disp32)
{
    if (avail < 1) return 0;
    const uint8_t modrm = modrm_ptr[0];
    const unsigned mod = modrm >> 6;
    const unsigned rm  = modrm & 7;
    if (mod == 3) return 0;                     // register form, not memory
    if (mod == 0 && rm == 5) return 0;          // RIP-relative

    size_t n = 1;
    unsigned b = 0;
    if (rm == 4) {                              // SIB present
        if (avail < n + 1) return 0;
        const uint8_t sib = modrm_ptr[n++];
        if (((sib >> 3) & 7) != 4 || (rex & 0x02)) return 0; // index must be none
        if ((sib >> 6) != 0) return 0;          // scale must be 1
        if (mod == 0 && (sib & 7) == 5) return 0; // absolute disp32 without base
        b = reg_of(rex, sib & 7, 0x01);
    } else {
        b = reg_of(rex, rm, 0x01);
    }

    int d = 0;
    if (mod == 1) {
        if (avail < n + 1) return 0;
        d = static_cast<int8_t>(modrm_ptr[n++]);
    } else if (mod == 2) {
        if (!allow_disp32 || avail < n + 4) return 0;
        int32_t d32 = 0;
        std::memcpy(&d32, modrm_ptr + n, sizeof d32);
        d = d32;
        n += 4;
    }

    base = b;
    disp = d;
    return n;
}

// lea r64, [base + disp] with destination in ModRM.reg and a non-volatile base.
// Rejects RIP-relative and indexed forms.
static bool DecodeLea(const uint8_t* p, size_t avail, size_t& len, unsigned& dest)
{
    if (avail < 3) return false;
    const uint8_t rex = p[0];
    if ((rex & 0xF8) != 0x48) return false;     // REX.W, optional R/X/B
    if (p[1] != 0x8D) return false;             // LEA

    unsigned base = 0;
    int disp = 0;
    const size_t mem_len = DecodeBaseDispMem(rex, p + 2, avail - 2, base, disp, true);
    if (mem_len == 0 || !is_nonvolatile_reg(base))
        return false;

    len  = 2 + mem_len;
    dest = reg_of(rex, (p[2] >> 3) & 7, 0x04); // REX.R
    return true;
}

// cmp r64/r32, imm8   ->  [REX] 83 /7 ib
// Used for the length pre-check before the comparison.
static bool DecodeCmpRegImm8(const uint8_t* p, size_t avail, size_t& len,
                             unsigned& reg, int& imm)
{
    size_t i = 0;
    uint8_t rex = 0;
    if (avail >= 4 && (p[0] & 0xF0) == 0x40) {
        rex = p[0];
        if ((rex & 0x06) != 0) return false;    // REX.R and REX.X must be 0
        i = 1;
    }
    if (avail < i + 3) return false;
    if (p[i] != 0x83) return false;
    const uint8_t modrm = p[i + 1];
    if ((modrm >> 6) != 3) return false;        // register operand
    if (((modrm >> 3) & 7) != 7) return false;  // /7 = CMP
    len = i + 3;
    reg = reg_of(rex, modrm & 7, 0x01);         // REX.B
    imm = static_cast<int8_t>(p[i + 2]);
    return true;
}

// Decodes the SSO capacity check: cmp <nonvol_reg_or_frame_mem>, imm8 (15 or 16).
static bool DecodeSsoCmpImm8(const uint8_t* p, size_t avail, size_t& len, int& imm)
{
    if (avail < 4) return false;
    const uint8_t rex = p[0];
    if ((rex & 0xF8) != 0x48) return false;     // REX.W required for capacity check
    if (p[1] != 0x83) return false;
    const uint8_t modrm = p[2];
    if (((modrm >> 3) & 7) != 7) return false;  // /7 = CMP

    if ((modrm >> 6) == 3) {
        if ((rex & 0x06) != 0) return false;
        const unsigned reg = reg_of(rex, modrm & 7, 0x01);
        if (!is_nonvolatile_reg(reg)) return false;
        imm = static_cast<int8_t>(p[3]);
        len = 4;
        return true;
    }

    unsigned base = 0;
    int disp = 0;
    const size_t mem_len = DecodeBaseDispMem(rex, p + 2, avail - 2, base, disp, true);
    if (mem_len == 0 || avail < 2 + mem_len + 1 || !is_nonvolatile_reg(base))
        return false;
    imm = static_cast<int8_t>(p[2 + mem_len]);
    len = 2 + mem_len + 1;
    return true;
}

// cmov<cc> r64, <nonvol_reg_or_frame_mem> with destination in ModRM.reg.
// Accepts unsigned and signed greater/equal conditions:
//   0F 43 = CMOVAE/NB, 0F 47 = CMOVA/NBE, 0F 4D = CMOVGE, 0F 4F = CMOVG.
static bool DecodeSsoCmov(const uint8_t* p, size_t avail, size_t& len, unsigned& dest)
{
    if (avail < 4) return false;
    const uint8_t rex = p[0];
    if ((rex & 0xF8) != 0x48) return false;
    if (p[1] != 0x0F) return false;
    const uint8_t cc = p[2];
    if (cc != 0x43 && cc != 0x47 && cc != 0x4D && cc != 0x4F) return false;
    const uint8_t modrm = p[3];
    dest = reg_of(rex, (modrm >> 3) & 7, 0x04);

    if ((modrm >> 6) == 3) {
        const unsigned src = reg_of(rex, modrm & 7, 0x01);
        if (!is_nonvolatile_reg(src)) return false;
        len = 4;
        return true;
    }

    unsigned base = 0;
    int disp = 0;
    const size_t mem_len = DecodeBaseDispMem(rex, p + 3, avail - 3, base, disp, true);
    if (mem_len == 0 || !is_nonvolatile_reg(base))
        return false;
    len = 3 + mem_len;
    return true;
}

// mov byte ptr [base + disp8], al   ->  [REX.B] 88 /0 [SIB] disp8
// The agent stores its verdict this way. Sets len and base.
static bool DecodeStoreAl(const uint8_t* p, size_t avail, size_t& len,
                          unsigned& base, int& disp)
{
    if (avail < 3) return false;
    size_t i = 0;
    uint8_t rex = 0;
    if ((p[0] & 0xF0) == 0x40) {                // optional REX
        rex = p[0];
        if (rex & 0x08) return false;           // REX.W is wrong for a byte store
        i = 1;
    }
    if (avail < i + 3) return false;
    if (p[i] != 0x88) return false;             // MOV r/m8, r8
    const uint8_t modrm = p[i + 1];
    if ((modrm >> 6) != 1) return false;        // disp8 form
    if (((modrm >> 3) & 7) != 0) return false;  // source register must be AL
    if (rex & 0x04) return false;               // REX.R would make it r8b..r15b

    const size_t mem_len = DecodeBaseDispMem(rex, p + i + 1, avail - (i + 1), base, disp, false);
    if (mem_len == 0) return false;

    len = i + 1 + mem_len;
    return true;
}

// mov r64, [base + disp8]  ->  REX.W[+R/B] 8B /r [SIB] disp8
// Sets len and base.
static bool DecodeLoadReg(const uint8_t* p, size_t avail, size_t& len,
                          unsigned& base, int& disp)
{
    if (avail < 4) return false;
    const uint8_t rex = p[0];
    if ((rex & 0xF8) != 0x48) return false;
    if (p[1] != 0x8B) return false;
    const uint8_t modrm = p[2];
    if ((modrm >> 6) != 1) return false;        // disp8 form

    const size_t mem_len = DecodeBaseDispMem(rex, p + 2, avail - 2, base, disp, false);
    if (mem_len == 0) return false;

    len = 2 + mem_len;
    return true;
}

// mov <nonvol_r64>, [<nonvol_base> + disp8/disp32]  ->  REX.W[+R/B] 8B /r ...
// Matches harmless non-volatile register preloads the compiler schedules inside
// the first comparison link's SSO setup without clobbering RCX, RSP, or RFLAGS.
static bool DecodeNonVolFrameLoad(const uint8_t* p, size_t avail, size_t& len)
{
    if (avail < 3) return false;
    const uint8_t rex = p[0];
    if ((rex & 0xF8) != 0x48) return false;
    if (p[1] != 0x8B) return false;
    const unsigned dst = reg_of(rex, (p[2] >> 3) & 7, 0x04);
    if (dst == 1 || dst == 4 || !is_nonvolatile_reg(dst))
        return false; // never clobber RCX (1) or RSP (4)

    unsigned base = 0;
    int disp = 0;
    const size_t mem_len = DecodeBaseDispMem(rex, p + 2, avail - 2, base, disp, true);
    if (mem_len == 0 || !is_nonvolatile_reg(base))
        return false;

    len = 2 + mem_len;
    return true;
}

// ===========================================================================
// SECTION: discovery
// ===========================================================================

// Everything the code generator needs, derived from the agent's own code.
struct Site {
    uint8_t* triple_addr      = nullptr; // start address of the SSO sequence in host
    uint8_t  triple_bytes[24] = {};      // packed lea + cmp + cmov bytes for the stub
    size_t   triple_len       = 0;       // valid byte count in triple_bytes
    unsigned len_reg          = 0;       // non-volatile register holding name length
    uint8_t* hook             = nullptr; // patch site address
    size_t   hook_len         = 0;       // total byte length of the overwritten pair
    uint8_t* resume           = nullptr; // hook + hook_len
};

// Validates the patch site itself: the two instructions being overwritten.
// They must store AL and then reload through the same base register, which is
// what makes them safe to copy into the stub and replay there.
static bool ValidateHookSite(uint8_t* p, size_t avail, size_t& hook_len)
{
    size_t l1 = 0, l2 = 0;
    unsigned b1 = 0, b2 = 0;
    int d1 = 0, d2 = 0;

    if (!DecodeStoreAl(p, avail, l1, b1, d1))
        return false;
    if (!DecodeLoadReg(p + l1, avail - l1, l2, b2, d2))
        return false;
    if (b1 != b2)
        return false;                       // must be the same frame register
    if (d1 != 0x28 || d2 != 0x08)
        return false;                       // the offsets the agent uses

    // Must be a non-volatile register so it is not clobbered before the epilogue.
    if (!is_nonvolatile_reg(b1))
        return false;

    const size_t total = l1 + l2;
    if (total < 5)                          // an E9 rel32 needs five bytes
        return false;

    hook_len = total;
    return true;
}

// Dumps raw bytes around an address, so a layout this build does not yet
// understand can be read out of the log instead of guessed at.
static void LogBytesAround(uint8_t* p, const Section& sec, size_t before, size_t after)
{
    uint8_t* lo = (static_cast<size_t>(p - sec.base) < before) ? sec.base : (p - before);
    uint8_t* hi = (static_cast<size_t>((sec.base + sec.size) - p) < after) ? (sec.base + sec.size) : (p + after);

    for (uint8_t* line = lo; line < hi; line += 16) {
        wchar_t buf[128];
        int n = swprintf(buf, 96, L"  %p%s ", line, (line <= p && p < line + 16) ? L" *" : L"  ");
        for (uint8_t* b = line; b < line + 16 && b < hi; b++)
            n += swprintf(buf + n, 8, L"%02X ", *b);
        Wh_Log(L"%s", buf);
    }
}

// Checks whether an instruction at 'q' loads the verified size argument into R8:
//   1. mov r8/r8d, <len_reg>  (opcode 0x8B, destination in ModRM.reg)
//   2. mov r8/r8d, <len_reg>  (opcode 0x89, destination in ModRM.rm)
//   3. mov r8d, <anchor_len>  (41 B8 imm32, constant-propagated length)
//   4. mov r8,  <anchor_len>  (49 C7 C0 imm32, constant-propagated length)
// Returns the instruction length in bytes, or 0 if not a match.
static size_t MatchSizeArgLoadToR8(const uint8_t* q, size_t avail, unsigned len_reg, size_t anchor_len)
{
    if (avail >= 3) {
        const uint8_t rex = q[0];
        const uint8_t op  = q[1];
        const uint8_t modrm = q[2];
        if ((modrm >> 6) == 3) {
            // Opcode 0x8B: MOV r64/r32, r/m64/32 -> dest in reg, src in rm
            if (op == 0x8B && (rex == 0x4C || rex == 0x4D || rex == 0x44 || rex == 0x45)) {
                const unsigned dst = reg_of(rex, (modrm >> 3) & 7, 0x04);
                const unsigned src = reg_of(rex, modrm & 7, 0x01);
                if (dst == 8 && src == len_reg)
                    return 3;
            }
            // Opcode 0x89: MOV r/m64/32, r64/32 -> dest in rm, src in reg
            if (op == 0x89 && (rex == 0x49 || rex == 0x4D || rex == 0x41 || rex == 0x45)) {
                const unsigned dst = reg_of(rex, modrm & 7, 0x01);
                const unsigned src = reg_of(rex, (modrm >> 3) & 7, 0x04);
                if (dst == 8 && src == len_reg)
                    return 3;
            }
        }
    }
    // Constant-propagated mov r8d, imm32 (41 B8 ib ib ib ib)
    if (avail >= 6 && q[0] == 0x41 && q[1] == 0xB8) {
        uint32_t imm32 = 0;
        std::memcpy(&imm32, q + 2, sizeof imm32);
        if (imm32 == anchor_len)
            return 6;
    }
    // Constant-propagated mov r8, imm32 (49 C7 C0 ib ib ib ib)
    if (avail >= 7 && q[0] == 0x49 && q[1] == 0xC7 && q[2] == 0xC0) {
        uint32_t imm32 = 0;
        std::memcpy(&imm32, q + 3, sizeof imm32);
        if (imm32 == anchor_len)
            return 7;
    }
    return 0;
}

// Scans a short tail window starting at 'target' for a valid patch site.
static bool FindHookInTail(uint8_t* target, const Section& code, uint8_t*& hook, size_t& hook_len)
{
    if (target < code.base || target >= code.base + code.size)
        return false;

    constexpr size_t k_tail = 16;
    uint8_t* sec_end = code.base + code.size;
    uint8_t* limit = (static_cast<size_t>(sec_end - target) < k_tail) ? sec_end : target + k_tail;

    for (uint8_t* q = target; q < limit; q++) {
        const size_t avail = static_cast<size_t>(sec_end - q);
        size_t len = 0;
        if (ValidateHookSite(q, avail, len)) {
            hook     = q;
            hook_len = len;
            return true;
        }
    }
    return false;
}

// Resolves the positive-match branch target after an anchor comparison call.
// Used for intermediate links in an '||' comparison chain where the length-
// mismatch jne advances to the next link while a match jumps to the tail.
static bool FindPostCallMatchTarget(uint8_t* lea_rdx, const Section& code, unsigned len_reg,
                                    size_t anchor_len, uint8_t*& match_target)
{
    uint8_t* cur = lea_rdx + 7;
    uint8_t* sec_end = code.base + code.size;
    if (cur >= sec_end)
        return false;

    const size_t r8_len = MatchSizeArgLoadToR8(cur, static_cast<size_t>(sec_end - cur), len_reg, anchor_len);
    if (r8_len > 0)
        cur += r8_len;

    size_t avail = static_cast<size_t>(sec_end - cur);
    if (avail >= 5 && cur[0] == 0xE8) {                 // call rel32
        cur += 5;
    } else if (avail >= 6 && cur[0] == 0xFF && cur[1] == 0x15) { // call [rip+disp32]
        cur += 6;
    } else {
        return false;
    }

    avail = static_cast<size_t>(sec_end - cur);
    if (avail < 4)
        return false;

    bool match_on_zero = false;
    if (cur[0] == 0x85 && cur[1] == 0xC0) {             // test eax, eax (0 == match)
        match_on_zero = true;
    } else if (cur[0] == 0x84 && cur[1] == 0xC0) {      // test al, al (nonzero == match)
        match_on_zero = false;
    } else {
        return false;
    }
    cur += 2;
    avail = static_cast<size_t>(sec_end - cur);

    uint8_t* target = nullptr;
    uint8_t* branch_end = nullptr;
    const uint8_t rel8_op  = match_on_zero ? 0x74 : 0x75;
    const uint8_t rel32_op = match_on_zero ? 0x84 : 0x85;

    if (avail >= 2 && cur[0] == rel8_op) {
        branch_end = cur + 2;
        target = branch_end + static_cast<int8_t>(cur[1]);
    } else if (avail >= 6 && cur[0] == 0x0F && cur[1] == rel32_op) {
        int32_t rel = 0;
        std::memcpy(&rel, cur + 2, sizeof rel);
        branch_end = cur + 6;
        target = branch_end + rel;
    } else {
        return false;
    }

    if (target <= branch_end || target < code.base || target >= sec_end)
        return false;

    match_target = target;
    return true;
}

// Scans backward from p_cmplen for the SSO triple (lea rcx -> cmp 15|16 -> cmov rcx),
// packing only those three instructions into out.triple_bytes while skipping harmless
// interleaved non-volatile frame loads.
static bool ExtractSsoTriple(uint8_t* p_cmplen, const Section& code, Site& out)
{
    for (size_t total_try = 11; total_try <= 40; total_try++) {
        if (static_cast<size_t>(p_cmplen - code.base) < total_try)
            break;

        uint8_t* cand = p_cmplen - total_try;
        uint8_t* cur  = cand;

        size_t   lea_len = 0;
        unsigned lea_dst = 0;
        if (!DecodeLea(cur, static_cast<size_t>(p_cmplen - cur), lea_len, lea_dst) || lea_dst != 1)
            continue;
        uint8_t* lea_ptr = cur;
        cur += lea_len;

        for (int k = 0; k < 3; k++) {
            size_t fl = 0;
            if (DecodeNonVolFrameLoad(cur, static_cast<size_t>(p_cmplen - cur), fl))
                cur += fl;
            else
                break;
        }

        size_t cmp_sso_len = 0;
        int    sso_imm     = 0;
        if (!DecodeSsoCmpImm8(cur, static_cast<size_t>(p_cmplen - cur), cmp_sso_len, sso_imm))
            continue;
        if (sso_imm != 15 && sso_imm != 16)
            continue;
        uint8_t* cmp_ptr = cur;
        cur += cmp_sso_len;

        for (int k = 0; k < 2; k++) {
            size_t fl = 0;
            if (DecodeNonVolFrameLoad(cur, static_cast<size_t>(p_cmplen - cur), fl))
                cur += fl;
            else
                break;
        }

        size_t   cmov_len = 0;
        unsigned cmov_dst = 0;
        if (!DecodeSsoCmov(cur, static_cast<size_t>(p_cmplen - cur), cmov_len, cmov_dst))
            continue;
        if (cmov_dst != 1)
            continue;
        uint8_t* cmov_ptr = cur;
        cur += cmov_len;

        for (int k = 0; k < 3; k++) {
            size_t fl = 0;
            if (DecodeNonVolFrameLoad(cur, static_cast<size_t>(p_cmplen - cur), fl))
                cur += fl;
            else
                break;
        }

        if (cur != p_cmplen)
            continue;
        const size_t packed_len = lea_len + cmp_sso_len + cmov_len;
        if (packed_len > sizeof(out.triple_bytes))
            continue;

        std::memcpy(out.triple_bytes, lea_ptr, lea_len);
        std::memcpy(out.triple_bytes + lea_len, cmp_ptr, cmp_sso_len);
        std::memcpy(out.triple_bytes + lea_len + cmp_sso_len, cmov_ptr, cmov_len);
        out.triple_addr = cand;
        out.triple_len  = packed_len;
        return true;
    }
    return false;
}

// Walks back from the anchor reference to the structure that precedes it and
// fills in 'out'. Returns false if anything does not decode as expected.
static bool ResolveFromAnchorRef(uint8_t* lea_rdx, const Section& code, size_t anchor_len, Site& out)
{
    // Shape being looked for, reading forward:
    //   <lea rcx,[frame+disp]> <cmp reg/mem,15|16> <cmov rcx,reg/mem>  the SSO triple
    //   <cmp len_reg, anchor_len>                                      the length check
    //   <jne ...>              -> terminal tail OR next link in chain
    //   ... argument setup (including mov r8, len_reg) ...
    //   <lea rdx, [rip -> anchor]>                                     <- entry point
    constexpr size_t k_window = 48;   // how far back the length check may sit

    // Nearest match first: the length check is the last branch before the
    // comparison it guards.
    for (size_t back = 5; back <= k_window; back++) {
        if (static_cast<size_t>(lea_rdx - code.base) < back)
            break;
        uint8_t* p_cmplen = lea_rdx - back;

        size_t   cmp_len = 0;
        unsigned len_reg = 0;
        int      imm     = 0;
        if (!DecodeCmpRegImm8(p_cmplen, back, cmp_len, len_reg, imm))
            continue;
        if (imm != static_cast<int>(anchor_len))
            continue;
        if (!is_nonvolatile_reg(len_reg))
            continue;

        // The branch that skips the comparison, in either encoding.
        uint8_t* p_j = p_cmplen + cmp_len;
        if (p_j + 2 > lea_rdx)
            continue;

        uint8_t* target = nullptr;
        size_t   j_len = 0;
        if (p_j[0] == 0x75) {                       // jne rel8
            target = p_j + 2 + static_cast<int8_t>(p_j[1]);
            j_len  = 2;
        } else if (p_j + 6 <= lea_rdx && p_j[0] == 0x0F && p_j[1] == 0x85) { // jne rel32
            int32_t rel;
            std::memcpy(&rel, p_j + 2, sizeof rel);
            target = p_j + 6 + rel;
            j_len  = 6;
        } else {
            continue;
        }
        if (p_j + j_len > lea_rdx)                  // the branch must precede us
            continue;

        if (target < code.base || target >= code.base + code.size)
            continue;

        // Terminal links reach the convergence tail on jne; intermediate links
        // in an '||' chain reach it via the post-call match branch instead.
        uint8_t* hook = nullptr;
        size_t   hook_len = 0;
        if (!FindHookInTail(target, code, hook, hook_len)) {
            uint8_t* match_target = nullptr;
            if (!FindPostCallMatchTarget(lea_rdx, code, len_reg, anchor_len, match_target) ||
                !FindHookInTail(match_target, code, hook, hook_len)) {
                continue;
            }
        }

        // Required cross-check: between the branch and the comparison call
        // (either just before or immediately after lea_rdx), R8 must receive
        // the verified length.
        uint8_t* scan_end = lea_rdx + 14;
        if (scan_end > code.base + code.size)
            scan_end = code.base + code.size;

        bool size_arg_ok = false;
        for (uint8_t* q = p_j + j_len; q < scan_end; q++) {
            if (q == lea_rdx) {
                q += 6; // skip the 7-byte lea_rdx instruction (loop increments +1)
                continue;
            }
            const size_t avail = static_cast<size_t>(scan_end - q);
            if (MatchSizeArgLoadToR8(q, avail, len_reg, anchor_len) != 0) {
                size_arg_ok = true;
                break;
            }
        }
        if (!size_arg_ok)
            continue;

        Site cand_site;
        if (!ExtractSsoTriple(p_cmplen, code, cand_site))
            continue;

        cand_site.len_reg  = len_reg;
        cand_site.hook     = hook;
        cand_site.hook_len = hook_len;
        cand_site.resume   = hook + hook_len;
        out = cand_site;
        return true;
    }

    return false;
}

// Finds the patch site. Requires exactly one candidate to survive validation.
static bool DiscoverSite(const std::string& anchor, Site& out)
{
    std::vector<Section> sections;
    uint8_t* mod_base = nullptr;
    if (!GetMainModuleSections(sections, mod_base)) {
        Wh_Log(L"discover: could not read the main module's PE headers");
        return false;
    }

    // Step 1: every occurrence of the anchor literal in read-only data.
    std::vector<uint8_t*> literals;
    const size_t alen = anchor.size();
    for (const auto& s : sections) {
        if (s.exec || !s.read || s.write || s.size < alen + 1)
            continue;
        for (size_t i = 0; i + alen < s.size; i++) {
            if (std::memcmp(s.base + i, anchor.data(), alen) != 0)
                continue;
            if (s.base[i + alen] != '\0')      // must be NUL-terminated
                continue;
            literals.push_back(s.base + i);
        }
    }
    if (literals.empty()) {
        Wh_Log(L"discover: anchor '%S' not found in read-only data", anchor.c_str());
        return false;
    }
    Wh_Log(L"discover: anchor '%S' found %zu time(s)", anchor.c_str(), literals.size());

    // Step 2: RIP-relative references to those literals, in executable code.
    // Step 3: for each, try to resolve the surrounding structure.
    std::vector<Site> hits;
    for (const auto& s : sections) {
        if (!s.exec || s.size < 7)
            continue;
        for (size_t i = 0; i + 7 <= s.size; i++) {
            // lea rdx, [rip+disp32]  ->  48 8D 15 disp32
            if (s.base[i] != 0x48 || s.base[i + 1] != 0x8D || s.base[i + 2] != 0x15)
                continue;

            int32_t disp;
            std::memcpy(&disp, s.base + i + 3, sizeof disp);
            uint8_t* target = s.base + i + 7 + disp;

            bool is_anchor = false;
            for (uint8_t* lit : literals) {
                if (lit == target) { is_anchor = true; break; }
            }
            if (!is_anchor)
                continue;

            Site cand;
            if (!ResolveFromAnchorRef(s.base + i, s, alen, cand)) {
                Wh_Log(L"discover: reference at %p does not match the expected shape; "
                       L"surrounding bytes follow", s.base + i);
                LogBytesAround(s.base + i, s, 64, 32);
                continue;
            }

            // Collapse duplicates that resolve to the same patch site.
            bool dup = false;
            for (const auto& h : hits) {
                if (h.hook == cand.hook) { dup = true; break; }
            }
            if (!dup)
                hits.push_back(cand);
        }
    }

    if (hits.empty()) {
        Wh_Log(L"discover: no candidate passed validation");
        return false;
    }
    if (hits.size() > 1) {
        // Ambiguity is not resolvable safely, so refuse rather than guess.
        Wh_Log(L"discover: %zu distinct candidates, refusing to choose", hits.size());
        for (const auto& h : hits)
            Wh_Log(L"discover:   candidate patch site at %p", h.hook);
        return false;
    }

    out = hits[0];
    Wh_Log(L"discover: patch site %p, %zu bytes; setup %p, %zu bytes; length register r%u",
           out.hook, out.hook_len, out.triple_addr, out.triple_len, out.len_reg);
    return true;
}

// ===========================================================================
// SECTION: codegen
//
// The stub is split across two adjacent pages allocated within reach of a
// 32-bit relative jump from the patch site:
//
//   Page 0 (PAGE_READWRITE):
//     [ control block ]  two flag bytes, updated lock-free by LoadSettings
//     [ data slots    ]  absolute addresses of the handler and resume point
//
//   Page 1 (PAGE_EXECUTE_READ):
//     [ code          ]  the generated relay stub
//
// Splitting data and code across adjacent pages achieves strict W^X compliance:
// neither page is ever mapped PAGE_EXECUTE_READWRITE, and LoadSettings can
// update the control flags directly without calling VirtualProtect at runtime.
// ===========================================================================

struct StubLayout {
    static constexpr size_t off_veto      = 0;    // 1 byte  (Page 0, RW)
    static constexpr size_t off_extra     = 1;    // 1 byte  (Page 0, RW)
    static constexpr size_t off_handler   = 8;    // 8 bytes (Page 0, RW)
    static constexpr size_t off_resume    = 16;   // 8 bytes (Page 0, RW)
    static constexpr size_t off_in_flight = 24;   // 4 bytes (Page 0, RW)
};

// Small append-only emitter over a fixed buffer.
namespace {
struct Emitter {
    uint8_t* buf;
    size_t   cap;
    size_t   n = 0;
    bool     ok = true;

    Emitter(uint8_t* b, size_t c) : buf(b), cap(c) {}

    void u8(uint8_t v)  { if (n + 1 > cap) { ok = false; return; } buf[n++] = v; }
    void u32(uint32_t v){ if (n + 4 > cap) { ok = false; return; } std::memcpy(buf + n, &v, 4); n += 4; }
    void raw(const uint8_t* p, size_t len)
    {
        if (n + len > cap) { ok = false; return; }
        std::memcpy(buf + n, p, len);
        n += len;
    }
};
} // namespace

// Builds the stub. 'data_page' is Page 0 (RW), 'code_page' is Page 1 (written
// while RW, then transitioned to RX before patching).
// Returns the number of code bytes written, or 0 on failure.
static size_t BuildStub(uint8_t* data_page, uint8_t* code_page, size_t page_size, const Site& site)
{
    // Data slots in Page 0.
    data_page[StubLayout::off_veto]  = 0;
    data_page[StubLayout::off_extra] = 0;
    void* handler = reinterpret_cast<void*>(&patched_switch_foreground_process_handler);
    std::memcpy(data_page + StubLayout::off_handler, &handler, sizeof handler);
    std::memcpy(data_page + StubLayout::off_resume,  &site.resume, sizeof site.resume);
    const int32_t zero_in_flight = 0;
    std::memcpy(data_page + StubLayout::off_in_flight, &zero_in_flight, sizeof zero_in_flight);

    Emitter e(code_page, page_size);

    // Displacement from the end of a RIP-relative instruction in Page 1 to a
    // data slot in Page 0.
    auto rip_to = [&](size_t slot_off, size_t insn_end_from_code) -> uint32_t {
        const intptr_t from = static_cast<intptr_t>(
            reinterpret_cast<uintptr_t>(code_page) + insn_end_from_code);
        const intptr_t to = static_cast<intptr_t>(
            reinterpret_cast<uintptr_t>(data_page) + slot_off);
        return static_cast<uint32_t>(static_cast<int32_t>(to - from));
    };

    // --- entry: preserve RFLAGS so the bypass path is fully transparent ----
    //
    // NOTE: the branches here use rel32 forms throughout. The full path is well
    // over 127 bytes, so a rel8 jump to .pass or .epilogue does not reach.
    e.u8(0x9C);                                   // pushfq
    e.u8(0x84); e.u8(0xC0);                       // test al, al

    // jnz -> .veto   (patched once the offset is known)
    e.u8(0x0F); e.u8(0x85); const size_t fix_jnz = e.n; e.u32(0);

    // AL == 0: only the additional list could still turn this into a yes.
    //   cmp byte ptr [rip + extra], 0
    e.u8(0x80); e.u8(0x3D);
    e.u32(rip_to(StubLayout::off_extra, e.n + 4 + 1));
    e.u8(0x00);
    e.u8(0x0F); e.u8(0x84); const size_t fix_je_extra = e.n; e.u32(0);  // je .pass
    e.u8(0xE9); const size_t fix_jmp_full = e.n; e.u32(0);              // jmp .full

    // .veto: AL != 0, so only the excluded list could turn this into a no.
    const size_t lbl_veto = e.n;
    e.u8(0x80); e.u8(0x3D);
    e.u32(rip_to(StubLayout::off_veto, e.n + 4 + 1));
    e.u8(0x00);
    e.u8(0x0F); e.u8(0x84); const size_t fix_je_veto = e.n; e.u32(0);   // je .pass

    // --- .full: the decision logic can change the answer -------------------
    const size_t lbl_full = e.n;
    e.u8(0x9D);                                   // popfq, restoring RSP

    // The agent's own pointer setup (isolated SSO triple). RSP is back at its
    // entry value, so an RSP-relative displacement inside it stays correct.
    e.raw(site.triple_bytes, site.triple_len);    // -> RCX = name

    // mov rdx, <len_reg>     REX.W [+R] 89 /r, ModRM.reg = source
    e.u8(static_cast<uint8_t>(0x48 | ((site.len_reg & 8) ? 0x04 : 0)));
    e.u8(0x89);
    e.u8(static_cast<uint8_t>(0xC0 | ((site.len_reg & 7) << 3) | 2));

    // movzx r8, al           -> the agent's verdict as the third argument
    e.u8(0x4C); e.u8(0x0F); e.u8(0xB6); e.u8(0xC0);

    // Save the volatile registers the agent may still be using.
    e.u8(0x41); e.u8(0x51);                       // push r9
    e.u8(0x41); e.u8(0x52);                       // push r10
    e.u8(0x41); e.u8(0x53);                       // push r11
    e.u8(0x49); e.u8(0x89); e.u8(0xE3);           // mov r11, rsp
    e.u8(0x48); e.u8(0x83); e.u8(0xE4); e.u8(0xF0); // and rsp, -16
    e.u8(0x48); e.u8(0x81); e.u8(0xEC); e.u32(144); // sub rsp, 144
    e.u8(0x4C); e.u8(0x89); e.u8(0x9C); e.u8(0x24);
    e.u32(128);                                   // mov [rsp+128], r11
    for (unsigned i = 0; i < 6; i++) {            // movaps [rsp+32+16i], xmm(i)
        e.u8(0x0F); e.u8(0x29);
        e.u8(static_cast<uint8_t>(0x44 | (i << 3)));
        e.u8(0x24);
        e.u8(static_cast<uint8_t>(32 + 16 * i));
    }

    // Track active DLL entry from inside the permanently mapped stub.
    //   lock inc dword ptr [rip + off_in_flight]
    e.u8(0xF0); e.u8(0xFF); e.u8(0x05);
    e.u32(rip_to(StubLayout::off_in_flight, e.n + 4));

    // call [rip + handler]   -- indirect, so nothing extra is clobbered
    e.u8(0xFF); e.u8(0x15);
    e.u32(rip_to(StubLayout::off_handler, e.n + 4));

    // Clean upper bits of RAX so EAX/RAX holds a canonical 0 or 1.
    e.u8(0x0F); e.u8(0xB6); e.u8(0xC0);           // movzx eax, al

    // Thread has returned out of the DLL into code_page; safe to decrement.
    //   lock dec dword ptr [rip + off_in_flight]
    e.u8(0xF0); e.u8(0xFF); e.u8(0x0D);
    e.u32(rip_to(StubLayout::off_in_flight, e.n + 4));

    for (unsigned i = 0; i < 6; i++) {            // movaps xmm(i), [rsp+32+16i]
        e.u8(0x0F); e.u8(0x28);
        e.u8(static_cast<uint8_t>(0x44 | (i << 3)));
        e.u8(0x24);
        e.u8(static_cast<uint8_t>(32 + 16 * i));
    }
    e.u8(0x48); e.u8(0x8B); e.u8(0xA4); e.u8(0x24);
    e.u32(128);                                   // mov rsp, [rsp+128]
    e.u8(0x41); e.u8(0x5B);                       // pop r11
    e.u8(0x41); e.u8(0x5A);                       // pop r10
    e.u8(0x41); e.u8(0x59);                       // pop r9

    e.u8(0xE9); const size_t fix_jmp_epi = e.n; e.u32(0);   // jmp .epilogue

    // --- .pass: nothing to decide, leave the agent exactly as it was -------
    const size_t lbl_pass = e.n;
    e.u8(0x9D);                                   // popfq

    // --- .epilogue: replay what the E9 overwrote, then resume --------------
    const size_t lbl_epi = e.n;
    e.raw(site.hook, site.hook_len);
    e.u8(0xFF); e.u8(0x25);                       // jmp [rip + resume]
    e.u32(rip_to(StubLayout::off_resume, e.n + 4));

    if (!e.ok)
        return 0;

    // Resolve the branches. Each displacement is measured from the byte after
    // the 4-byte operand, which is the fixup slot + 4.
    auto fix32 = [&](size_t slot, size_t target) {
        const int32_t d = static_cast<int32_t>(
            static_cast<intptr_t>(target) - static_cast<intptr_t>(slot + 4));
        std::memcpy(code_page + slot, &d, sizeof d);
    };
    fix32(fix_jnz,      lbl_veto);
    fix32(fix_je_extra, lbl_pass);
    fix32(fix_jmp_full, lbl_full);
    fix32(fix_je_veto,  lbl_pass);
    fix32(fix_jmp_epi,  lbl_epi);

    return e.n;
}

// ===========================================================================
// SECTION: install / remove
// ===========================================================================

static constexpr size_t k_rel_jmp_size = 1 + sizeof(int32_t);

struct HookState {
    bool     active        = false;
    uint8_t* address       = nullptr;      // patch site inside the agent
    uint8_t  original[16]  = {};           // bytes replaced by the jump
    size_t   length        = 0;
    uint64_t patched_qword = 0;            // expected 8-byte header written at InstallHook
    uint8_t* stub          = nullptr;      // allocated 2-page block (Page 0 RW, Page 1 RX)
};

static HookState g_hook;

// Pointers into the stub's RW control block (Page 0), so settings changes
// reach the flags without needing VirtualProtect on the RX code page.
static volatile uint8_t* g_veto_flag  = nullptr;
static volatile uint8_t* g_extra_flag = nullptr;

// Allocates a 2-page block (initially PAGE_READWRITE) within +/-2GB of target.
// Uses VirtualQuery to skip already-allocated blocks in large chunks rather than
// blindly probing every page, and respects the 64 KB allocation granularity.
static void* AllocateStubPagesNearAddress(void* target, size_t alloc_size)
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    const uintptr_t gran = si.dwAllocationGranularity ? si.dwAllocationGranularity : 0x10000;
    constexpr uintptr_t max_disp = 0x7FFF0000; // slightly under 2 GB

    const uintptr_t target_addr = reinterpret_cast<uintptr_t>(target);
    const uintptr_t min_addr = (target_addr > max_disp)
        ? ((target_addr - max_disp > reinterpret_cast<uintptr_t>(si.lpMinimumApplicationAddress))
            ? target_addr - max_disp : reinterpret_cast<uintptr_t>(si.lpMinimumApplicationAddress))
        : reinterpret_cast<uintptr_t>(si.lpMinimumApplicationAddress);
    const uintptr_t max_addr = (UINTPTR_MAX - target_addr > max_disp)
        ? ((target_addr + max_disp < reinterpret_cast<uintptr_t>(si.lpMaximumApplicationAddress))
            ? target_addr + max_disp : reinterpret_cast<uintptr_t>(si.lpMaximumApplicationAddress))
        : reinterpret_cast<uintptr_t>(si.lpMaximumApplicationAddress);

    // Search upwards from target_addr
    uintptr_t addr = (target_addr + gran - 1) & ~(gran - 1);
    while (addr < max_addr) {
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery(reinterpret_cast<void*>(addr), &mbi, sizeof mbi) == 0)
            break;

        if (mbi.State == MEM_FREE) {
            uintptr_t alloc_addr = (reinterpret_cast<uintptr_t>(mbi.BaseAddress) + gran - 1) & ~(gran - 1);
            uintptr_t region_end = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
            if (alloc_addr + alloc_size <= region_end && alloc_addr + alloc_size <= max_addr) {
                if (void* p = VirtualAlloc(reinterpret_cast<void*>(alloc_addr), alloc_size,
                                           MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE))
                    return p;
            }
        }

        addr = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        addr = (addr + gran - 1) & ~(gran - 1);
    }

    // Search downwards from target_addr
    addr = target_addr & ~(gran - 1);
    while (addr > min_addr) {
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery(reinterpret_cast<void*>(addr), &mbi, sizeof mbi) == 0)
            break;

        if (mbi.State == MEM_FREE) {
            uintptr_t region_end = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
            if (region_end >= gran) {
                uintptr_t alloc_addr = (region_end - gran) & ~(gran - 1);
                if (alloc_addr >= reinterpret_cast<uintptr_t>(mbi.BaseAddress) &&
                    alloc_addr >= min_addr &&
                    alloc_addr + alloc_size <= region_end) {
                    if (void* p = VirtualAlloc(reinterpret_cast<void*>(alloc_addr), alloc_size,
                                               MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE))
                        return p;
                }
            }
        }

        if (reinterpret_cast<uintptr_t>(mbi.BaseAddress) <= gran)
            break;
        addr = (reinterpret_cast<uintptr_t>(mbi.BaseAddress) - 1) & ~(gran - 1);
    }

    return nullptr;
}

static constexpr size_t k_atomic_patch_size = sizeof(uint64_t);

static const char* const k_fallback_anchors[] = {
    "firefox.exe",
    "chrome.exe",
    "msedge.exe",
    "iexplore.exe",
};

// Discovers the site, builds the stub and applies the patch.
static bool InstallHook(const std::string& anchor)
{
    Site site;
    bool found = DiscoverSite(anchor, site);
    if (!found) {
        for (const char* fb : k_fallback_anchors) {
            if (anchor == fb)
                continue;
            Wh_Log(L"install: trying fallback anchor '%S'", fb);
            if (DiscoverSite(fb, site)) {
                Wh_Log(L"install: resolved patch site using fallback anchor '%S'", fb);
                found = true;
                break;
            }
        }
    }
    if (!found) {
        Wh_Log(L"install: neither primary anchor nor fallback anchors could be resolved");
        return false;
    }

    if (site.hook_len > sizeof g_hook.original) {
        Wh_Log(L"install: patch length %zu exceeds the backup buffer", site.hook_len);
        return false;
    }

    SYSTEM_INFO si;
    GetSystemInfo(&si);
    const size_t page = si.dwPageSize ? si.dwPageSize : 4096;

    auto* stub = static_cast<uint8_t*>(AllocateStubPagesNearAddress(site.hook, 2 * page));
    if (!stub) {
        Wh_Log(L"install: no page available within reach of the patch site");
        return false;
    }

    uint8_t* data_page = stub;
    uint8_t* code_page = stub + page;

    const size_t code_len = BuildStub(data_page, code_page, page, site);
    if (code_len == 0) {
        Wh_Log(L"install: could not build the stub");
        VirtualFree(stub, 0, MEM_RELEASE);
        return false;
    }
    Wh_Log(L"install: stub data at %p (RW), code at %p (RX, %zu bytes)", data_page, code_page, code_len);

    // Confirm the jump can actually reach before touching the agent.
    const intptr_t delta = code_page - (site.hook + k_rel_jmp_size);
    if (delta < INT32_MIN || delta > INT32_MAX) {
        Wh_Log(L"install: stub out of range (delta = %lld)", static_cast<long long>(delta));
        VirtualFree(stub, 0, MEM_RELEASE);
        return false;
    }

    // Publish the flag and in-flight counter pointers in Page 0 (RW) before
    // the patch goes live, so the first call through the stub sees them.
    g_veto_flag      = data_page + StubLayout::off_veto;
    g_extra_flag     = data_page + StubLayout::off_extra;
    g_stub_in_flight = reinterpret_cast<volatile LONG*>(data_page + StubLayout::off_in_flight);
    if (const ModSettings* s = g_settings.load(std::memory_order_acquire)) {
        *g_veto_flag  = (s->verbose || !s->disabled.empty()) ? 1 : 0;
        *g_extra_flag = (s->verbose || !s->enabled.empty())  ? 1 : 0;
    }

    // Transition Page 1 (code_page) from RW to RX for strict W^X compliance.
    // Page 0 (data_page) remains PAGE_READWRITE.
    DWORD old_stub_protect = 0;
    if (!VirtualProtect(code_page, page, PAGE_EXECUTE_READ, &old_stub_protect)) {
        Wh_Log(L"install: could not protect stub code page as PAGE_EXECUTE_READ, error = %u", GetLastError());
        VirtualFree(stub, 0, MEM_RELEASE);
        g_veto_flag = g_extra_flag = nullptr;
        g_stub_in_flight = nullptr;
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), code_page, page);

    // Patching stays strictly within a single atomic 8-byte compare-exchange
    // swap. Trailing bytes when site.hook_len > 8 are skipped on entry by the
    // E9 jump and skipped on exit because site.resume == site.hook + hook_len.
    DWORD old = 0;
    if (!VirtualProtect(site.hook, k_atomic_patch_size, PAGE_EXECUTE_READWRITE, &old)) {
        Wh_Log(L"install: VirtualProtect failed, error = %u", GetLastError());
        VirtualFree(stub, 0, MEM_RELEASE);
        g_veto_flag = g_extra_flag = nullptr;
        g_stub_in_flight = nullptr;
        return false;
    }

    std::memcpy(g_hook.original, site.hook, site.hook_len);
    g_hook.address = site.hook;
    g_hook.length  = site.hook_len;
    g_hook.stub    = stub;

    const int32_t rel = static_cast<int32_t>(delta);

    // Prepare 8-byte atomic header (5-byte E9 rel32 + up to 3 NOPs).
    uint8_t patch_buf[k_atomic_patch_size];
    std::memcpy(patch_buf, site.hook, sizeof patch_buf);
    patch_buf[0] = 0xE9;
    std::memcpy(patch_buf + 1, &rel, sizeof rel);
    const size_t atomic_nop_end = (site.hook_len < sizeof patch_buf) ? site.hook_len : sizeof patch_buf;
    for (size_t i = k_rel_jmp_size; i < atomic_nop_end; i++) {
        patch_buf[i] = 0x90;
    }

    uint64_t new_val = 0;
    uint64_t old_val = 0;
    std::memcpy(&new_val, patch_buf, sizeof new_val);
    std::memcpy(&old_val, site.hook, sizeof old_val);
    const LONG64 prev = InterlockedCompareExchange64(
        reinterpret_cast<volatile LONG64*>(site.hook),
        static_cast<LONG64>(new_val),
        static_cast<LONG64>(old_val)
    );

    DWORD tmp = 0;
    VirtualProtect(site.hook, k_atomic_patch_size, old, &tmp);
    if (prev != static_cast<LONG64>(old_val)) {
        Wh_Log(L"install: patch site %p changed concurrently (expected 0x%016llX, found 0x%016llX); aborting",
               site.hook,
               static_cast<unsigned long long>(old_val),
               static_cast<unsigned long long>(static_cast<uint64_t>(prev)));
        VirtualFree(stub, 0, MEM_RELEASE);
        g_veto_flag      = nullptr;
        g_extra_flag     = nullptr;
        g_stub_in_flight = nullptr;
        g_hook           = HookState{};
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), site.hook, k_atomic_patch_size);

    g_hook.patched_qword = new_val;
    g_hook.active        = true;
    Wh_Log(L"install: patch applied at %p", site.hook);
    return true;
}

// Restores the agent's original bytes. Must run before this DLL is unmapped.
static void RemoveHook()
{
    if (!g_hook.active)
        return;

    // Disarm the stub control flags first so any thread entering the stub while
    // we unpatch takes the fast .pass bypass instead of calling into this DLL.
    if (g_veto_flag && g_extra_flag) {
        *g_veto_flag  = 0;
        *g_extra_flag = 0;
        MemoryBarrier();
    }

    DWORD old = 0;
    if (VirtualProtect(g_hook.address, k_atomic_patch_size, PAGE_EXECUTE_READWRITE, &old)) {
        uint8_t restore_buf[k_atomic_patch_size];
        std::memcpy(restore_buf, &g_hook.patched_qword, sizeof restore_buf);
        const size_t atomic_restore_len = (g_hook.length < sizeof restore_buf) ? g_hook.length : sizeof restore_buf;
        std::memcpy(restore_buf, g_hook.original, atomic_restore_len);

        uint64_t restore_val = 0;
        std::memcpy(&restore_val, restore_buf, sizeof restore_val);
        const LONG64 prev = InterlockedCompareExchange64(
            reinterpret_cast<volatile LONG64*>(g_hook.address),
            static_cast<LONG64>(restore_val),
            static_cast<LONG64>(g_hook.patched_qword)
        );

        DWORD tmp = 0;
        VirtualProtect(g_hook.address, k_atomic_patch_size, old, &tmp);
        if (prev == static_cast<LONG64>(g_hook.patched_qword)) {
            FlushInstructionCache(GetCurrentProcess(), g_hook.address, k_atomic_patch_size);
            Wh_Log(L"remove: original bytes restored at %p", g_hook.address);
        } else {
            Wh_Log(L"remove: patch site %p was modified externally (expected 0x%016llX, found 0x%016llX); skipped restore",
                   g_hook.address,
                   static_cast<unsigned long long>(g_hook.patched_qword),
                   static_cast<unsigned long long>(static_cast<uint64_t>(prev)));
        }
    } else {
        Wh_Log(L"remove: VirtualProtect failed, error = %u", GetLastError());
    }

    // The stub pages are deliberately not freed: another thread may still be
    // executing the pass-through epilogue inside it at this moment. Keep
    // g_stub_in_flight valid through Wh_ModBeforeUninit for settings reclamation.
    g_veto_flag   = nullptr;
    g_extra_flag  = nullptr;
    g_hook.active = false;

    // Grace period so any thread between reading control flags and executing
    // 'lock inc' either finishes the fast .pass bypass or enters the counter,
    // followed by draining until no thread remains inside this DLL.
    Sleep(20);
    for (int i = 0; i < 100 && GetInFlightCount() > 0; i++) {
        Sleep(1);
    }
}

// ===========================================================================
// SECTION: settings loader
// ===========================================================================

// Normalizes a user-configured executable pattern:
//   1. Lowers ASCII characters.
//   2. Trims leading/trailing whitespace and quotes.
//   3. Strips any accidentally pasted directory path prefix.
//   4. Appends ".exe" if neither an extension ('.') nor wildcards ('*', '?') are present.
static std::string NormalizeAppPattern(PCWSTR raw)
{
    std::string s;
    for (const wchar_t* p = raw; *p; p++) {
        const wchar_t w = *p;
        s += (w < 0x80) ? ascii_lower(static_cast<char>(w)) : '?';
    }

    const auto is_trim = [](char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '"' || c == '\'';
    };
    while (!s.empty() && is_trim(s.front()))
        s.erase(s.begin());
    while (!s.empty() && is_trim(s.back()))
        s.pop_back();

    const size_t slash = s.find_last_of("\\/");
    if (slash != std::string::npos)
        s.erase(0, slash + 1);

    if (!s.empty() &&
        s.find('.') == std::string::npos &&
        s.find('*') == std::string::npos &&
        s.find('?') == std::string::npos) {
        s += ".exe";
    }
    return s;
}

// Reads a Windhawk string array. Windhawk has no length key and can persist
// blank items ahead of populated ones, so stop only after several consecutive
// empty entries. key_fmt must contain a single %d.
static std::vector<std::string> ReadStringArray(const wchar_t* key_fmt)
{
    constexpr int k_max_empty_streak = 8;
    std::vector<std::string> out;
    int empty_streak = 0;
    for (int i = 0; ; i++) {
        PCWSTR raw = Wh_GetStringSetting(key_fmt, i);
        if (!raw || raw[0] == L'\0') {
            Wh_FreeStringSetting(raw);
            if (++empty_streak >= k_max_empty_streak)
                break;
            continue;
        }
        empty_streak = 0;
        std::string entry = NormalizeAppPattern(raw);
        Wh_FreeStringSetting(raw);
        if (!entry.empty())
            out.emplace_back(std::move(entry));
    }
    return out;
}

static std::string ReadAnchor()
{
    PCWSTR raw = Wh_GetStringSetting(L"anchorString");
    std::string anchor = raw ? NormalizeAppPattern(raw) : std::string();
    Wh_FreeStringSetting(raw);
    if (anchor.empty())
        anchor = "firefox.exe";
    return anchor;
}

static void ReclaimRetiredSettings()
{
    if (g_retired_settings.empty())
        return;
    if (GetInFlightCount() > 0) {
        for (int i = 0; i < 20 && GetInFlightCount() > 0; i++) {
            Sleep(1);
        }
        // Never delete retired settings while a handler call is still running.
        if (GetInFlightCount() > 0)
            return;
    }
    for (const ModSettings* old : g_retired_settings) {
        delete old;
    }
    g_retired_settings.clear();
}

static void LoadSettings()
{
    auto* fresh = new (std::nothrow) ModSettings();
    if (!fresh) {
        Wh_Log(L"settings: allocation failed, keeping the previous set");
        return;
    }

    fresh->enabled  = ReadStringArray(L"enabledApps[%d]");
    fresh->disabled = ReadStringArray(L"disabledApps[%d]");
    fresh->verbose  = Wh_GetIntSetting(L"verboseLogging") != 0;

    const size_t n_enabled  = fresh->enabled.size();
    const size_t n_disabled = fresh->disabled.size();
    const bool   verbose    = fresh->verbose;

    g_last_verbose_sig.store(0, std::memory_order_relaxed);

    // Publish atomically and queue the previous generation for safe reclamation.
    if (const ModSettings* old = g_settings.exchange(fresh, std::memory_order_acq_rel)) {
        g_retired_settings.push_back(old);
    }

    // Update the flags in the RW data page (Page 0). When verbose logging is
    // enabled, both flags are set to 1 so the stub routes all evaluations
    // through the handler for diagnostic logging.
    if (g_veto_flag && g_extra_flag) {
        *g_veto_flag  = (verbose || n_disabled > 0) ? 1 : 0;
        *g_extra_flag = (verbose || n_enabled  > 0) ? 1 : 0;
        MemoryBarrier();
    }

    ReclaimRetiredSettings();

    Wh_Log(L"settings: %zu enabled, %zu disabled, verbose = %d", n_enabled, n_disabled, verbose ? 1 : 0);
}

// ===========================================================================
// SECTION: windhawk callbacks
// ===========================================================================

BOOL Wh_ModInit()
{
    Wh_Log(L"init: experimental structural-discovery build starting (v3.0.0-exp9)");
    LoadSettings();
    if (!InstallHook(ReadAnchor())) {
        if (const ModSettings* s = g_settings.exchange(nullptr, std::memory_order_acq_rel)) {
            g_retired_settings.push_back(s);
        }
        ReclaimRetiredSettings();
        return FALSE;
    }
    return TRUE;
}

void Wh_ModAfterInit()
{
    Wh_Log(L"init: hook active");
}

void Wh_ModBeforeUninit()
{
    Wh_Log(L"uninit: removing hook");
    RemoveHook();

    if (const ModSettings* s = g_settings.exchange(nullptr, std::memory_order_acq_rel)) {
        g_retired_settings.push_back(s);
    }
    ReclaimRetiredSettings();
    g_stub_in_flight = nullptr;
}

void Wh_ModSettingsChanged()
{
    Wh_Log(L"settings: reloading");
    LoadSettings();
}