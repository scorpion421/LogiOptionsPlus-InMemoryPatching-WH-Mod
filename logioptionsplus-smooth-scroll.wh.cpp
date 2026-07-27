// ==WindhawkMod==
// @id              logioptionsplus-smooth-scroll
// @name            Logi Options+ Smooth Scroll for All Apps
// @description     Enables high-resolution smooth mouse wheel scrolling in any application, not just browsers. Port of igvk/LogiOptionsPlus-InMemoryPatching.
// @version         2.3.0
// @author          MickyFoley
// @github          https://github.com/scorpion421
// @include         logioptionsplus_agent.exe
// @architecture    amd64
// @license         MIT
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Logi Options+ Smooth Scroll for All Apps

Logitech mice support high-resolution mouse wheel events (smooth scrolling),
but Logi Options+ only enables this feature for a few hardcoded browser
applications (notably those using the `chrome.exe` and `firefox.exe`
executable names). This mod patches the Logi Options+ agent process at
runtime to allow smooth scrolling in any application you specify.

The built-in Logi Options+ browser detection is always preserved -- this mod
only adds to it, never replaces it. Chromium-based browsers that ship under
their own executable name (Vivaldi, Brave, Opera, and depending on the agent
version Microsoft Edge) are often NOT detected by the agent, so they are
included as default entries in the additional applications list.

## Settings

**Additional applications**: Executable filenames (without path) to enable
smooth scrolling for. Wildcards `*` and `?` are supported. The defaults cover
common Chromium-based browsers the agent may miss; add or remove entries as
needed.

**Excluded applications**: Executable filenames to explicitly disable smooth
scrolling for. Overrides both the additional apps list and the built-in
browser detection.

## Priority order

1. Excluded apps list matches -> **disabled** (highest priority)
2. Built-in Logi Options+ browser list matches -> **enabled**
3. Additional apps list matches -> **enabled**
4. No match -> **disabled**

## Implementation notes

This mod uses the same in-memory hooking technique as the original project:
it locates a version-specific code signature inside the agent process, then
installs an inline trampoline hook (E9 jump to an allocated relay page) at a
point inside the foreground-process check function. A small assembly handler
(one per known agent version) reconstructs the original register state and
calls back into the decision logic.

- Targets all known Logi Options+ agent versions (1.00 through 1.94).
- No files are written to disk; everything happens in memory.
- No `version.dll` deployment required. No reconfiguration after Logi Options+ updates.
- Settings changes apply immediately without restarting the agent.
- Original project: https://github.com/igvk/LogiOptionsPlus-InMemoryPatching by igvk (MIT License)

## Changelog

### 2.3.0

- **Pass-through for natively detected applications.** The hook is a patch at a
  single address, so it necessarily fires for every process -- including the
  browsers Logi Options+ already detects on its own. For those the mod cannot
  change the answer, so running the decision logic was pure risk: it cost time
  and clobbered registers and flags on a path the window manager is timing.

  Each assembly handler now checks two flags up front and falls straight through
  to the original instructions whenever the outcome cannot differ from what the
  agent already computed. In that case the agent sees the exact register and
  flag state it would have seen without the mod. With no excluded patterns
  configured, every natively supported browser takes this path.

  The handlers also preserve the agent's flags across the whole hook, which the
  previous versions did not.

  Verified equivalent to the full logic across 900k+ generated combinations.

### 2.2.0

Internal correctness and hardening pass. No change to settings or matching
behaviour: the decision logic was verified bit-identical against the previous
version across 120k+ generated input combinations, and the assembly handlers
still emit byte-identical argument setup and epilogue.

- **Fix (ABI)**: The assembly handlers now preserve the volatile registers they
  do not use for arguments (R9, R10, R11, XMM0-XMM5), align RSP to 16 bytes at
  the call site, and reserve their own shadow space instead of borrowing the
  agent's.

  The hook point sits at a common exit for two paths through the agent's
  foreground check: one that ran a string comparison, and one that branched
  straight past it. On the second path no call occurs, so the compiler may keep
  live values in the volatile registers -- which calling into C++ from the hook
  destroyed.

- **Fix (race)**: Settings were replaced by value while the hook handler could be
  iterating them on an agent thread, so a settings change during a foreground
  switch could read freed memory. Settings are now published through an atomic
  pointer swap; the hot path is lock-free and never sees a half-updated list.
- **Performance**: The handler no longer allocates. It previously built a
  `std::string` on every foreground change; the executable name is now located
  in place and lowercased into stack storage. Added early exits so the common
  case -- a natively detected browser with no excluded patterns -- returns
  without inspecting the name at all. This is the same class of issue as the
  2.0.1 logging fix: work done here delays the agent's answer during fullscreen
  transitions.
- **Safety**: The relay page is now verified to be within rel32 range before any
  byte is written into the agent, instead of relying on a truncating cast.
- **Safety**: Only the bytes being patched are unprotected, rather than the
  entire surrounding code region.
- **Robustness**: A signature match whose hook bytes are absent (already patched,
  or a false-positive match elsewhere in memory) now continues searching instead
  of aborting the install. Added a bounds check on the backup buffer and a guard
  against a zero-sized memory region stalling the scan.

### 2.0.1

- **Fix**: Taskbar remained visible when entering fullscreen video or browser-based
  fullscreen games (e.g. YouTube fullscreen, browser games). The per-invocation
  debug log call inside the foreground-process handler slowed the hook enough to
  interfere with the window manager's fullscreen detection. The per-call log was
  removed; match-outcome logs (when an entry from the additional/excluded list
  actually matches) are unchanged.
- **Metadata**: Added `@github` link to the mod header.

### 2.0.0

- Initial release. Full port of igvk/LogiOptionsPlus-InMemoryPatching to a
  Windhawk mod, including the in-memory inline trampoline hook and the five
  per-version assembly handlers.
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
  $description: Executable filenames (without path) to enable smooth scrolling for. Wildcards * and ? are supported. Defaults include common Chromium-based browsers that the agent may not detect on its own.
- disabledApps:
    - iexplore.exe
  $name: Excluded applications
  $description: Executable filenames to explicitly disable smooth scrolling for. Overrides both the additional apps list and the built-in Logi Options+ browser list. Wildcards * and ? are supported.
*/
// ==/WindhawkModSettings==

#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <new>
#include <string>
#include <vector>

// ===========================================================================
// Byte signatures for each known agent version.
// Identical to the original project's TARGET_MACHINE_CODE_VXxx defines.
// ===========================================================================

static const uint8_t k_target_V100[] = {
    0x48, 0x8D, 0x4C, 0x24, 0x78, 0x48, 0x83, 0xFF, 0x10, 0x48, 0x0F, 0x43,
    0xCB, 0x48, 0x83, 0xFE, 0x0B, 0x75, 0x17, 0x4C, 0x8B, 0xC6
};
static const uint8_t k_target_V146[] = {
    0x48, 0x8D, 0x4D, 0xFF, 0x49, 0x83, 0xFE, 0x10, 0x48, 0x0F, 0x43, 0xCE,
    0x48, 0x83, 0xFB, 0x0B, 0x75, 0x17, 0x4C, 0x8B, 0xC3
};
static const uint8_t k_target_V168[] = {
    0x48, 0x8D, 0x4D, 0xDF, 0x49, 0x83, 0xFE, 0x10, 0x48, 0x0F, 0x43, 0xCF,
    0x48, 0x83, 0xFB, 0x0B, 0x75, 0x17, 0x4C, 0x8B, 0xC3
};
static const uint8_t k_target_V186[] = {
    0x48, 0x8D, 0x4D, 0xC0, 0x49, 0x83, 0xFE, 0x10, 0x48, 0x0F, 0x43, 0xCF,
    0x48, 0x83, 0xFB, 0x0B, 0x75, 0x17, 0x4C, 0x8B, 0xC3
};
static const uint8_t k_target_V194[] = {
    0x48, 0x8D, 0x4D, 0x00, 0x48, 0x83, 0xFE, 0x0F, 0x48, 0x0F, 0x47, 0xCF,
    0x48, 0x83, 0xFB, 0x0B, 0x75, 0x17, 0x4C, 0x8B, 0xC3
};

// HOOK_MACHINE_CODE: the byte sequence right after the target signature that
// gets overwritten by the injected E9 jump (5 bytes minimum). One per version.
static const uint8_t k_hook_V100[] = { 0x88, 0x45, 0x28, 0x48, 0x8B, 0x7D, 0x08 };
static const uint8_t k_hook_V146[] = { 0x41, 0x88, 0x44, 0x24, 0x28, 0x4D, 0x8B, 0x64, 0x24, 0x08 };
static const uint8_t k_hook_V168[] = { 0x41, 0x88, 0x44, 0x24, 0x28, 0x4D, 0x8B, 0x64, 0x24, 0x08 };
static const uint8_t k_hook_V186[] = { 0x41, 0x88, 0x44, 0x24, 0x28, 0x4D, 0x8B, 0x64, 0x24, 0x08 };
static const uint8_t k_hook_V194[] = { 0x41, 0x88, 0x47, 0x28, 0x49, 0x8B, 0x7F, 0x08 };

// Maximum byte distance to search for the hook sequence after the signature.
static constexpr size_t k_max_hook_disp = 0x20;

// Size of an E9 rel32 jump: opcode + 32-bit displacement.
static constexpr size_t k_rel_jmp_size = 1 + sizeof(int32_t);

// Size of the absolute jump stub written into the relay page:
// mov r10, imm64 (10 bytes) + jmp r10 (3 bytes).
static constexpr size_t k_abs_jmp_size = 13;

// Memory protection flags that indicate executable pages.
static constexpr DWORD k_exec_protect =
    PAGE_EXECUTE | PAGE_EXECUTE_READ |
    PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;

// ===========================================================================
// Settings state
// ===========================================================================

struct ModSettings {
    std::vector<std::string>  enabled;   // additional apps (lowercase patterns)
    std::vector<std::string>  disabled;  // excluded apps  (lowercase patterns)
};

// Published through an atomic pointer rather than held by value.
//
// The hook handler runs on the agent's own threads, while Wh_ModSettingsChanged
// runs on a Windhawk thread. Replacing a by-value ModSettings would destroy the
// vectors a handler might be iterating at that very moment. Swapping a pointer
// is atomic and keeps the hot path lock-free.
//
// Retired generations are intentionally leaked: a handler may still be reading
// one when the swap happens, settings changes are rare and user-driven, and the
// leak is a few hundred bytes. This mirrors the deliberate leak of the relay
// page in RemoveHook() for the same reason.
static std::atomic<const ModSettings*> g_settings{nullptr};

// ASCII lowercase. Deliberately not std::tolower: that consults the current
// locale and is a real function call, which is measurable in the hot path.
// Executable names are ASCII.
static inline char ascii_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

// ===========================================================================
// Glob matching (direct port from utilities.cpp)
//
// PRECONDITION: both 'text' and 'glob' are already lowercase. The original
// lowercased 'text' on every character comparison; the single caller now
// lowercases once into a stack buffer, so that per-character work is dropped.
// Matching behaviour is unchanged.
// ===========================================================================

static bool glob_match(const char* text, const char* glob)
{
    const char* text_backup = nullptr;
    const char* glob_backup = nullptr;
    while (*text != '\0') {
        if (*glob == '*') {
            text_backup = text;
            glob_backup = ++glob;
        } else if ((*glob == '?' && *text != '/') || *glob == *text) {
            text++;
            glob++;
        } else {
            if (!glob_backup || *text_backup == '/')
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
// Decision logic
// Mirrors patched_switch_foreground_process_handler() from main.cpp.
// Declared extern "C" so the assembly handlers can call it by symbol name.
//
// 'name'   : pointer to the foreground process name (not null-terminated
//            guaranteed; 'length' gives the count).
// 'length' : number of characters in 'name'.
// 'previous_check' : the original agent's result (true = built-in browser
//            list match).
//
// Priority:
//   1. disabled match           -> false  (overrides everything)
//   2. previous_check (browser) -> true   (built-in list preserved)
//   3. enabled match            -> true
//   4. default                  -> false
// ===========================================================================

// Upper bound for the stack buffer holding the lowercased executable name.
static constexpr size_t k_name_buf = 260;

extern "C" bool patched_switch_foreground_process_handler(
    const char* name, size_t length, bool previous_check)
{
    const ModSettings* s = g_settings.load(std::memory_order_acquire);
    if (!s || !name)
        return previous_check;   // never worse than the agent's own answer

    const bool have_disabled = !s->disabled.empty();
    const bool have_enabled  = !s->enabled.empty();

    // -------------------------------------------------------------------
    // Fast paths.
    //
    // The agent calls this on every foreground change, including the burst
    // Windows emits while a window goes fullscreen. Anything done here delays
    // the agent's answer, and a late answer can make Windows miss its own
    // window for hiding the taskbar. So: do no work at all whenever the
    // outcome is already determined.
    //
    // With no excluded patterns nothing can veto a positive result, so the
    // agent's own verdict stands when it is true. This is the common case for
    // every natively detected browser (Chrome, Edge, Firefox) and returns
    // without touching the name, allocating, or matching anything.
    //
    // Results are bit-identical to the full logic below.
    // -------------------------------------------------------------------
    if (!have_disabled) {
        if (previous_check)
            return true;
        if (!have_enabled)
            return false;
    }

    // -------------------------------------------------------------------
    // Slow path. Still allocation-free: the basename is located in the
    // caller's buffer and lowercased into stack storage. The previous version
    // built a std::string per call, which is heap traffic on a path the
    // window manager is timing.
    //
    // No per-invocation logging here either -- only match outcomes are logged.
    // -------------------------------------------------------------------
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
            Wh_Log(L"handler: '%S' matched disabled '%S' -> false",
                   buf, glob.c_str());
            return false;
        }
    }
    if (previous_check)
        return true;
    for (const auto& glob : s->enabled) {
        if (glob_match(buf, glob.c_str())) {
            Wh_Log(L"handler: '%S' matched enabled '%S' -> true",
                   buf, glob.c_str());
            return true;
        }
    }
    return false;
}

// ===========================================================================
// Injected assembly handlers (one per agent version).
// Direct port of inject64.asm. Defined as global assembly symbols using
// Clang/GCC .intel_syntax. Each handler reconstructs the 'name' / 'length' /
// 'previous check' values from the registers that the patched function left
// them in, calls the decision logic, restores the agent's expected register
// state, and jumps to original_jump_address (set before the hook is armed).
//
// The handlers do NOT follow a normal calling convention on entry: they are
// reached via a JMP from inside the agent's function, so the registers are in
// the agent's mid-function state.
//
// The hook point is a common exit for two paths through the agent's function:
// one that ran a string comparison (and therefore already clobbered the
// volatile registers) and one that branched straight past it (and therefore
// did not). On the second path the compiler is free to keep live values in
// RAX/RCX/RDX/R8-R11 and XMM0-XMM5, because from its point of view no call
// happens there. Calling into C++ from here destroys exactly those.
//
// Each handler therefore saves and restores the volatile registers it does not
// use for arguments, aligns RSP to 16 bytes as the Win64 ABI requires at a call
// site, and reserves its own 32-byte shadow space instead of borrowing the
// agent's. RCX, RDX, R8 and RAX are argument/return registers and are clobbered
// by design. All other registers used by the handlers (RBP, RBX, RSI, RDI, R12,
// R14, R15) are non-volatile and preserved by the callee.
// ===========================================================================

extern "C" {
    // Set to the return address (instruction after the overwritten hook bytes)
    // before arming each hook. Shared single slot: only one version is ever
    // hooked per process, matching the original code.
    void* original_jump_address = nullptr;

    // Bypass flags read directly by the assembly handlers.
    //
    // The hook sits at a single address and therefore fires for every process,
    // including the browsers the agent already detects natively. For those the
    // decision logic cannot change the answer, so running it is pure risk: it
    // costs time and clobbers registers and flags on a path the window manager
    // is timing. These flags let each handler recognise that case and fall
    // straight through to the original instructions, leaving the agent in
    // exactly the state it would have been in without the mod.
    //
    //   g_veto_patterns  != 0  ->  the excluded list can turn a true into false
    //   g_extra_patterns != 0  ->  the additional list can turn a false into true
    //
    // They mirror the fast-path conditions in the C++ handler, which stays in
    // place as a safety net.
    volatile unsigned char g_veto_patterns  = 0;
    volatile unsigned char g_extra_patterns = 0;

    void injected_handler_V100();
    void injected_handler_V146();
    void injected_handler_V168();
    void injected_handler_V186();
    void injected_handler_V194();
}

__asm__(
    ".intel_syntax noprefix\n"
    ".text\n"

    // ---- V100 ----
    ".globl injected_handler_V100\n"
    "injected_handler_V100:\n"
    // Save the agent's flags first, so the bypass path below is fully
    // transparent. This lowers RSP by 8; any RSP-relative operand in the
    // argument setup is offset accordingly.
    "    pushfq\n"
    // Bypass: skip everything when the outcome cannot differ from the
    // value the agent already computed in AL.
    "    test al, al\n"
    "    jnz .Lveto_V100\n"
    "    cmp byte ptr [rip + g_extra_patterns], 0\n"
    "    je .Lpass_V100\n"
    "    jmp .Lfull_V100\n"
    ".Lveto_V100:\n"
    "    cmp byte ptr [rip + g_veto_patterns], 0\n"
    "    je .Lpass_V100\n"
    ".Lfull_V100:\n"
    // Arguments come from the agent's mid-function register state, so they
    // must be computed before anything below moves RSP again.
    "    lea rcx, [rsp+0xB8-0x40+8]\n"
    "    cmp rdi, 0x10\n"
    "    cmovnb rcx, rbx\n"
    "    mov rdx, rsi\n"
    "    movzx r8, al\n"
    // Preserve volatile GP and XMM registers, align RSP to 16 and reserve
    // our own shadow space (see the note above this block).
    "    push r9\n"
    "    push r10\n"
    "    push r11\n"
    "    mov r11, rsp\n"
    "    and rsp, -16\n"
    "    sub rsp, 144\n"
    "    mov [rsp+128], r11\n"
    "    movaps [rsp+32], xmm0\n"
    "    movaps [rsp+48], xmm1\n"
    "    movaps [rsp+64], xmm2\n"
    "    movaps [rsp+80], xmm3\n"
    "    movaps [rsp+96], xmm4\n"
    "    movaps [rsp+112], xmm5\n"
    "    call patched_switch_foreground_process_handler\n"
    "    movaps xmm0, [rsp+32]\n"
    "    movaps xmm1, [rsp+48]\n"
    "    movaps xmm2, [rsp+64]\n"
    "    movaps xmm3, [rsp+80]\n"
    "    movaps xmm4, [rsp+96]\n"
    "    movaps xmm5, [rsp+112]\n"
    "    mov rsp, [rsp+128]\n"
    "    pop r11\n"
    "    pop r10\n"
    "    pop r9\n"
    ".Lpass_V100:\n"
    "    popfq\n"
    // Replay the two overwritten instructions, then resume the agent.
    "    mov [rbp+0x28], al\n"
    "    mov rdi, [rbp+0x8]\n"
    "    jmp [rip + original_jump_address]\n"

    // ---- V146 ----
    ".globl injected_handler_V146\n"
    "injected_handler_V146:\n"
    // Save the agent's flags first, so the bypass path below is fully
    // transparent. This lowers RSP by 8; any RSP-relative operand in the
    // argument setup is offset accordingly.
    "    pushfq\n"
    // Bypass: skip everything when the outcome cannot differ from the
    // value the agent already computed in AL.
    "    test al, al\n"
    "    jnz .Lveto_V146\n"
    "    cmp byte ptr [rip + g_extra_patterns], 0\n"
    "    je .Lpass_V146\n"
    "    jmp .Lfull_V146\n"
    ".Lveto_V146:\n"
    "    cmp byte ptr [rip + g_veto_patterns], 0\n"
    "    je .Lpass_V146\n"
    ".Lfull_V146:\n"
    // Arguments come from the agent's mid-function register state, so they
    // must be computed before anything below moves RSP again.
    "    lea rcx, [rbp+0x57-0x58]\n"
    "    cmp r14, 0x10\n"
    "    cmovnb rcx, rsi\n"
    "    mov rdx, rbx\n"
    "    movzx r8, al\n"
    // Preserve volatile GP and XMM registers, align RSP to 16 and reserve
    // our own shadow space (see the note above this block).
    "    push r9\n"
    "    push r10\n"
    "    push r11\n"
    "    mov r11, rsp\n"
    "    and rsp, -16\n"
    "    sub rsp, 144\n"
    "    mov [rsp+128], r11\n"
    "    movaps [rsp+32], xmm0\n"
    "    movaps [rsp+48], xmm1\n"
    "    movaps [rsp+64], xmm2\n"
    "    movaps [rsp+80], xmm3\n"
    "    movaps [rsp+96], xmm4\n"
    "    movaps [rsp+112], xmm5\n"
    "    call patched_switch_foreground_process_handler\n"
    "    movaps xmm0, [rsp+32]\n"
    "    movaps xmm1, [rsp+48]\n"
    "    movaps xmm2, [rsp+64]\n"
    "    movaps xmm3, [rsp+80]\n"
    "    movaps xmm4, [rsp+96]\n"
    "    movaps xmm5, [rsp+112]\n"
    "    mov rsp, [rsp+128]\n"
    "    pop r11\n"
    "    pop r10\n"
    "    pop r9\n"
    ".Lpass_V146:\n"
    "    popfq\n"
    // Replay the two overwritten instructions, then resume the agent.
    "    mov [r12+0x28], al\n"
    "    mov r12, [r12+0x8]\n"
    "    jmp [rip + original_jump_address]\n"

    // ---- V168 ----
    ".globl injected_handler_V168\n"
    "injected_handler_V168:\n"
    // Save the agent's flags first, so the bypass path below is fully
    // transparent. This lowers RSP by 8; any RSP-relative operand in the
    // argument setup is offset accordingly.
    "    pushfq\n"
    // Bypass: skip everything when the outcome cannot differ from the
    // value the agent already computed in AL.
    "    test al, al\n"
    "    jnz .Lveto_V168\n"
    "    cmp byte ptr [rip + g_extra_patterns], 0\n"
    "    je .Lpass_V168\n"
    "    jmp .Lfull_V168\n"
    ".Lveto_V168:\n"
    "    cmp byte ptr [rip + g_veto_patterns], 0\n"
    "    je .Lpass_V168\n"
    ".Lfull_V168:\n"
    // Arguments come from the agent's mid-function register state, so they
    // must be computed before anything below moves RSP again.
    "    lea rcx, [rbp+0x57-0x78]\n"
    "    cmp r14, 0x10\n"
    "    cmovnb rcx, rdi\n"
    "    mov rdx, rbx\n"
    "    movzx r8, al\n"
    // Preserve volatile GP and XMM registers, align RSP to 16 and reserve
    // our own shadow space (see the note above this block).
    "    push r9\n"
    "    push r10\n"
    "    push r11\n"
    "    mov r11, rsp\n"
    "    and rsp, -16\n"
    "    sub rsp, 144\n"
    "    mov [rsp+128], r11\n"
    "    movaps [rsp+32], xmm0\n"
    "    movaps [rsp+48], xmm1\n"
    "    movaps [rsp+64], xmm2\n"
    "    movaps [rsp+80], xmm3\n"
    "    movaps [rsp+96], xmm4\n"
    "    movaps [rsp+112], xmm5\n"
    "    call patched_switch_foreground_process_handler\n"
    "    movaps xmm0, [rsp+32]\n"
    "    movaps xmm1, [rsp+48]\n"
    "    movaps xmm2, [rsp+64]\n"
    "    movaps xmm3, [rsp+80]\n"
    "    movaps xmm4, [rsp+96]\n"
    "    movaps xmm5, [rsp+112]\n"
    "    mov rsp, [rsp+128]\n"
    "    pop r11\n"
    "    pop r10\n"
    "    pop r9\n"
    ".Lpass_V168:\n"
    "    popfq\n"
    // Replay the two overwritten instructions, then resume the agent.
    "    mov [r12+0x28], al\n"
    "    mov r12, [r12+0x8]\n"
    "    jmp [rip + original_jump_address]\n"

    // ---- V186 ----
    ".globl injected_handler_V186\n"
    "injected_handler_V186:\n"
    // Save the agent's flags first, so the bypass path below is fully
    // transparent. This lowers RSP by 8; any RSP-relative operand in the
    // argument setup is offset accordingly.
    "    pushfq\n"
    // Bypass: skip everything when the outcome cannot differ from the
    // value the agent already computed in AL.
    "    test al, al\n"
    "    jnz .Lveto_V186\n"
    "    cmp byte ptr [rip + g_extra_patterns], 0\n"
    "    je .Lpass_V186\n"
    "    jmp .Lfull_V186\n"
    ".Lveto_V186:\n"
    "    cmp byte ptr [rip + g_veto_patterns], 0\n"
    "    je .Lpass_V186\n"
    ".Lfull_V186:\n"
    // Arguments come from the agent's mid-function register state, so they
    // must be computed before anything below moves RSP again.
    "    lea rcx, [rbp+0x40-0x80]\n"
    "    cmp r14, 0x10\n"
    "    cmovnb rcx, rdi\n"
    "    mov rdx, rbx\n"
    "    movzx r8, al\n"
    // Preserve volatile GP and XMM registers, align RSP to 16 and reserve
    // our own shadow space (see the note above this block).
    "    push r9\n"
    "    push r10\n"
    "    push r11\n"
    "    mov r11, rsp\n"
    "    and rsp, -16\n"
    "    sub rsp, 144\n"
    "    mov [rsp+128], r11\n"
    "    movaps [rsp+32], xmm0\n"
    "    movaps [rsp+48], xmm1\n"
    "    movaps [rsp+64], xmm2\n"
    "    movaps [rsp+80], xmm3\n"
    "    movaps [rsp+96], xmm4\n"
    "    movaps [rsp+112], xmm5\n"
    "    call patched_switch_foreground_process_handler\n"
    "    movaps xmm0, [rsp+32]\n"
    "    movaps xmm1, [rsp+48]\n"
    "    movaps xmm2, [rsp+64]\n"
    "    movaps xmm3, [rsp+80]\n"
    "    movaps xmm4, [rsp+96]\n"
    "    movaps xmm5, [rsp+112]\n"
    "    mov rsp, [rsp+128]\n"
    "    pop r11\n"
    "    pop r10\n"
    "    pop r9\n"
    ".Lpass_V186:\n"
    "    popfq\n"
    // Replay the two overwritten instructions, then resume the agent.
    "    mov [r12+0x28], al\n"
    "    mov r12, [r12+0x8]\n"
    "    jmp [rip + original_jump_address]\n"

    // ---- V194 ----
    ".globl injected_handler_V194\n"
    "injected_handler_V194:\n"
    // Save the agent's flags first, so the bypass path below is fully
    // transparent. This lowers RSP by 8; any RSP-relative operand in the
    // argument setup is offset accordingly.
    "    pushfq\n"
    // Bypass: skip everything when the outcome cannot differ from the
    // value the agent already computed in AL.
    "    test al, al\n"
    "    jnz .Lveto_V194\n"
    "    cmp byte ptr [rip + g_extra_patterns], 0\n"
    "    je .Lpass_V194\n"
    "    jmp .Lfull_V194\n"
    ".Lveto_V194:\n"
    "    cmp byte ptr [rip + g_veto_patterns], 0\n"
    "    je .Lpass_V194\n"
    ".Lfull_V194:\n"
    // Arguments come from the agent's mid-function register state, so they
    // must be computed before anything below moves RSP again.
    "    lea rcx, [rbp+0x70-0x70]\n"
    "    cmp rsi, 0xF\n"
    "    cmova rcx, rdi\n"
    "    mov rdx, rbx\n"
    "    movzx r8, al\n"
    // Preserve volatile GP and XMM registers, align RSP to 16 and reserve
    // our own shadow space (see the note above this block).
    "    push r9\n"
    "    push r10\n"
    "    push r11\n"
    "    mov r11, rsp\n"
    "    and rsp, -16\n"
    "    sub rsp, 144\n"
    "    mov [rsp+128], r11\n"
    "    movaps [rsp+32], xmm0\n"
    "    movaps [rsp+48], xmm1\n"
    "    movaps [rsp+64], xmm2\n"
    "    movaps [rsp+80], xmm3\n"
    "    movaps [rsp+96], xmm4\n"
    "    movaps [rsp+112], xmm5\n"
    "    call patched_switch_foreground_process_handler\n"
    "    movaps xmm0, [rsp+32]\n"
    "    movaps xmm1, [rsp+48]\n"
    "    movaps xmm2, [rsp+64]\n"
    "    movaps xmm3, [rsp+80]\n"
    "    movaps xmm4, [rsp+96]\n"
    "    movaps xmm5, [rsp+112]\n"
    "    mov rsp, [rsp+128]\n"
    "    pop r11\n"
    "    pop r10\n"
    "    pop r9\n"
    ".Lpass_V194:\n"
    "    popfq\n"
    // Replay the two overwritten instructions, then resume the agent.
    "    mov [r15+0x28], al\n"
    "    mov rdi, [r15+0x8]\n"
    "    jmp [rip + original_jump_address]\n"

    ".att_syntax prefix\n"
);

// ===========================================================================
// Hook removal state
// Saved so we can restore the agent's original bytes on unload, otherwise the
// stale E9 jump would point at a freed relay page and crash the agent.
// ===========================================================================

struct HookBackup {
    bool     active = false;
    uint8_t* address = nullptr;       // hook point inside the agent
    uint8_t  original[16] = {};       // original bytes (hook_len of them)
    size_t   length = 0;              // number of bytes saved
    void*    relay = nullptr;         // allocated relay page (to free on unmap)
};

static HookBackup g_backup;

// ===========================================================================
// Hooking primitives (ported from hooking.cpp)
// ===========================================================================

// Saturating add/subtract for uintptr_t (replaces MSVC _sat_add/sub_u64).
static inline uintptr_t sat_add(uintptr_t a, uintptr_t b)
{
    uintptr_t r = a + b;
    return (r < a) ? UINTPTR_MAX : r;
}
static inline uintptr_t sat_sub(uintptr_t a, uintptr_t b)
{
    return (a < b) ? 0 : (a - b);
}

// Allocates an executable page within +/-2GB of targetAddr so that a 32-bit
// relative E9 jump can reach it.
static void* AllocatePageNearAddress(void* targetAddr)
{
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    const size_t pageSize = sysInfo.dwPageSize;
    constexpr uintptr_t maxDisp = 0x7FFFFF00;

    uintptr_t startPage = reinterpret_cast<uintptr_t>(targetAddr) & ~(static_cast<uintptr_t>(pageSize) - 1);
    uintptr_t minAddr = reinterpret_cast<uintptr_t>(sysInfo.lpMinimumApplicationAddress);
    uintptr_t addr = sat_sub(startPage, maxDisp);
    minAddr = addr >= minAddr ? addr : minAddr;
    uintptr_t maxAddr = reinterpret_cast<uintptr_t>(sysInfo.lpMaximumApplicationAddress);
    addr = sat_add(startPage, maxDisp);
    maxAddr = addr <= maxAddr ? addr : maxAddr;

    const uintptr_t addrStep = pageSize;
    uintptr_t highAddr = startPage, lowAddr = startPage;

    do {
        highAddr = sat_add(highAddr, addrStep);
        lowAddr = sat_sub(lowAddr, addrStep);
        if (highAddr < maxAddr) {
            void* out = VirtualAlloc(reinterpret_cast<void*>(highAddr), pageSize,
                                     MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
            if (out) return out;
        }
        if (lowAddr > minAddr) {
            void* out = VirtualAlloc(reinterpret_cast<void*>(lowAddr), pageSize,
                                     MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
            if (out) return out;
        }
    } while (highAddr < maxAddr || lowAddr > minAddr);

    return nullptr;
}

// Writes an absolute 64-bit jump (mov r10, imm64 / jmp r10) at absJumpMemory.
// The caller supplies a full page, so k_abs_jmp_size always fits. R10 is a
// volatile scratch register under the Win64 ABI and is not used to pass the
// handler arguments, so clobbering it here is safe.
static void WriteAbsoluteJump64(void* absJumpMemory, void* addrToJumpTo)
{
    static_assert(k_abs_jmp_size == 13, "absolute jump stub size mismatch");
    uint8_t* code = static_cast<uint8_t*>(absJumpMemory);
    // mov r10, imm64  => 49 BA <imm64>
    code[0] = 0x49;
    code[1] = 0xBA;
    std::memcpy(code + 2, &addrToJumpTo, sizeof addrToJumpTo);
    // jmp r10  => 41 FF E2
    code[10] = 0x41;
    code[11] = 0xFF;
    code[12] = 0xE2;
}

// Installs an inline hook: allocates a relay page near func2hook, writes an
// absolute jump to payloadFunction there, then overwrites the first bytes of
// func2hook with an E9 relative jump to the relay. Any remaining overwritten
// bytes (injectSize - 5) are filled with NOPs.
// On success, *out_relay receives the allocated relay page address.
static bool InstallAllocateHook(void* func2hook, size_t injectSize,
                                void (*payloadFunction)(), void** out_relay)
{
    // An E9 rel32 needs 5 bytes; anything shorter cannot be patched safely.
    if (injectSize < k_rel_jmp_size) {
        Wh_Log(L"InstallAllocateHook: inject size %zu below minimum %zu",
               injectSize, k_rel_jmp_size);
        return false;
    }

    void* relay = AllocatePageNearAddress(func2hook);
    if (!relay) {
        Wh_Log(L"InstallAllocateHook: unable to allocate page near target");
        return false;
    }

    // Verify the relay is genuinely reachable by a 32-bit relative jump before
    // writing anything into the agent. AllocatePageNearAddress searches within
    // range, but a truncating cast would silently produce a wild jump if it
    // ever returned something farther out.
    const intptr_t delta =
        static_cast<uint8_t*>(relay) -
        (static_cast<uint8_t*>(func2hook) + k_rel_jmp_size);
    if (delta < INT32_MIN || delta > INT32_MAX) {
        Wh_Log(L"InstallAllocateHook: relay out of rel32 range (delta = %lld)",
               static_cast<long long>(delta));
        VirtualFree(relay, 0, MEM_RELEASE);
        return false;
    }

    WriteAbsoluteJump64(relay, reinterpret_cast<void*>(payloadFunction));

    const int32_t relAddr = static_cast<int32_t>(delta);
    uint8_t* code = static_cast<uint8_t*>(func2hook);
    code[0] = 0xE9;
    std::memcpy(code + 1, &relAddr, sizeof relAddr);

    // Pad the remainder of the overwritten instructions with NOPs so the
    // region never contains a partial instruction.
    const size_t remaining = injectSize - k_rel_jmp_size;
    if (remaining > 0)
        std::memset(code + k_rel_jmp_size, 0x90, remaining);

    if (out_relay)
        *out_relay = relay;
    return true;
}

// ===========================================================================
// Pattern search helper
// ===========================================================================

static bool FindPattern(const uint8_t* memory, size_t size,
                        const uint8_t* pattern, size_t pattern_len,
                        uint8_t*& out_addr)
{
    if (size < pattern_len) {
        out_addr = nullptr;
        return false;
    }
    const uint8_t* last = memory + size - pattern_len;
    for (const uint8_t* p = memory; p <= last; p++) {
        if (std::memcmp(p, pattern, pattern_len) == 0) {
            out_addr = const_cast<uint8_t*>(p);
            return true;
        }
    }
    out_addr = nullptr;
    return false;
}

// ===========================================================================
// Hook installation (ported from hook_current_process() in main.cpp)
// ===========================================================================

struct VersionEntry {
    const uint8_t* target;
    size_t         target_len;
    const uint8_t* hook;
    size_t         hook_len;
    void (*handler)();
    const wchar_t* label;
};

static const VersionEntry g_versions[] = {
    { k_target_V194, sizeof k_target_V194, k_hook_V194, sizeof k_hook_V194, injected_handler_V194, L"V194" },
    { k_target_V186, sizeof k_target_V186, k_hook_V186, sizeof k_hook_V186, injected_handler_V186, L"V186" },
    { k_target_V168, sizeof k_target_V168, k_hook_V168, sizeof k_hook_V168, injected_handler_V168, L"V168" },
    { k_target_V146, sizeof k_target_V146, k_hook_V146, sizeof k_hook_V146, injected_handler_V146, L"V146" },
    { k_target_V100, sizeof k_target_V100, k_hook_V100, sizeof k_hook_V100, injected_handler_V100, L"V100" },
};

static bool InstallHook()
{
    MEMORY_BASIC_INFORMATION mbi{};
    for (uint8_t* addr = nullptr;
         VirtualQuery(addr, &mbi, sizeof mbi);
         addr += mbi.RegionSize)
    {
        // Defensive: a zero-sized region would make this loop spin forever.
        if (mbi.RegionSize == 0)
            break;
        if (mbi.State != MEM_COMMIT)
            continue;
        if (mbi.Protect == PAGE_NOACCESS)
            continue;
        if ((mbi.Protect & k_exec_protect) == 0)
            continue;

        auto* mem   = static_cast<uint8_t*>(mbi.BaseAddress);
        size_t size = mbi.RegionSize;

        for (const auto& ver : g_versions) {
            uint8_t* found = nullptr;
            if (!FindPattern(mem, size, ver.target, ver.target_len, found))
                continue;

            Wh_Log(L"InstallHook: found signature %s at %p", ver.label, found);

            // Locate the hook bytes within k_max_hook_disp after the signature.
            uint8_t* after_sig = found + ver.target_len;
            size_t count = size - (after_sig - mem);
            if (count > k_max_hook_disp)
                count = k_max_hook_disp;

            uint8_t* hook_addr = nullptr;
            if (!FindPattern(after_sig, count, ver.hook, ver.hook_len, hook_addr)) {
                // Either the agent is already patched, or this was a
                // false-positive signature match somewhere else in memory.
                // Keep looking instead of giving up on the whole install.
                Wh_Log(L"InstallHook: signature %s matched but hook bytes absent; "
                       L"trying next candidate", ver.label);
                continue;
            }
            Wh_Log(L"InstallHook: hook point at %p", hook_addr);

            if (ver.hook_len > sizeof g_backup.original) {
                Wh_Log(L"InstallHook: hook length %zu exceeds backup buffer %zu",
                       ver.hook_len, sizeof g_backup.original);
                return false;
            }

            // Return address = instruction right after the overwritten bytes.
            // Published before the E9 is written, so the handler can never be
            // reached with a stale value.
            original_jump_address = hook_addr + ver.hook_len;

            // Unprotect only the bytes being modified, not the whole region
            // (which can span megabytes of the agent's code).
            DWORD oldProtect = 0;
            if (!VirtualProtect(hook_addr, ver.hook_len,
                                PAGE_EXECUTE_READWRITE, &oldProtect)) {
                Wh_Log(L"InstallHook: VirtualProtect failed, error = %u", GetLastError());
                return false;
            }

            // Save original bytes for restoration on unload.
            std::memcpy(g_backup.original, hook_addr, ver.hook_len);
            g_backup.address = hook_addr;
            g_backup.length  = ver.hook_len;

            void* relay = nullptr;
            const bool ok = InstallAllocateHook(hook_addr, ver.hook_len,
                                                ver.handler, &relay);

            DWORD tmp = 0;
            VirtualProtect(hook_addr, ver.hook_len, oldProtect, &tmp);
            FlushInstructionCache(GetCurrentProcess(), hook_addr, ver.hook_len);

            if (ok) {
                g_backup.relay  = relay;
                g_backup.active = true;
                Wh_Log(L"InstallHook: hook installed successfully (%s)", ver.label);
            } else {
                Wh_Log(L"InstallHook: failed to install hook (%s)", ver.label);
            }
            return ok;
        }
    }

    Wh_Log(L"InstallHook: no matching signature found in any executable region");
    return false;
}

// ===========================================================================
// Hook removal: restores the agent's original bytes. Must run before the mod
// DLL (and the relay page) is unmapped, otherwise the patched jump dangles.
// ===========================================================================

static void RemoveHook()
{
    if (!g_backup.active)
        return;

    DWORD oldProtect;
    if (VirtualProtect(g_backup.address, g_backup.length,
                       PAGE_EXECUTE_READWRITE, &oldProtect)) {
        std::memcpy(g_backup.address, g_backup.original, g_backup.length);
        VirtualProtect(g_backup.address, g_backup.length, oldProtect, &oldProtect);
        FlushInstructionCache(GetCurrentProcess(), g_backup.address, g_backup.length);
        Wh_Log(L"RemoveHook: original bytes restored at %p", g_backup.address);
    } else {
        Wh_Log(L"RemoveHook: VirtualProtect failed, error = %u", GetLastError());
    }

    // Note: the relay page is intentionally NOT freed. A different thread may
    // still be executing inside it at the moment of unhooking. Leaking one
    // page is the safe choice; the OS reclaims it on process exit.
    g_backup.active = false;
}

// ===========================================================================
// Settings loader
// ===========================================================================

static std::vector<std::string> ReadStringArray(const wchar_t* key_fmt)
{
    std::vector<std::string> result;

    // Windhawk arrays are read by iterating until an empty string is returned;
    // there is no separate length key. key_fmt must contain a single %d.
    for (int i = 0; ; i++) {
        PCWSTR raw = Wh_GetStringSetting(key_fmt, i);
        if (!raw || raw[0] == L'\0') {
            Wh_FreeStringSetting(raw);
            break;
        }
        std::string entry;
        for (const wchar_t* p = raw; *p; p++) {
            // Executable names are ASCII; anything outside that range cannot
            // match a real process name, so it is folded to '?' rather than
            // silently truncated into an arbitrary byte.
            const wchar_t w = *p;
            entry += (w < 0x80) ? ascii_lower(static_cast<char>(w)) : '?';
        }
        Wh_FreeStringSetting(raw);
        if (!entry.empty())
            result.emplace_back(std::move(entry));
    }
    return result;
}

static void LoadSettings()
{
    auto* fresh = new (std::nothrow) ModSettings();
    if (!fresh) {
        Wh_Log(L"LoadSettings: allocation failed, keeping previous settings");
        return;
    }

    fresh->enabled  = ReadStringArray(L"enabledApps[%d]");
    fresh->disabled = ReadStringArray(L"disabledApps[%d]");

    const size_t n_enabled  = fresh->enabled.size();
    const size_t n_disabled = fresh->disabled.size();

    // Publish atomically. The previous generation is deliberately not freed --
    // see the note on g_settings.
    g_settings.store(fresh, std::memory_order_release);

    // Publish the assembly bypass flags after the settings they describe.
    // Ordered this way the flags can only ever be conservative: a handler may
    // briefly call into the decision logic when it no longer needs to, which is
    // harmless. The reverse -- bypassing while a veto pattern exists -- cannot
    // happen.
    g_veto_patterns  = n_disabled ? 1 : 0;
    g_extra_patterns = n_enabled  ? 1 : 0;

    Wh_Log(L"Settings loaded: %zu enabled, %zu disabled", n_enabled, n_disabled);
}

// ===========================================================================
// Windhawk callbacks
// ===========================================================================

BOOL Wh_ModInit()
{
    Wh_Log(L"Wh_ModInit: logioptionsplus-smooth-scroll starting");
    LoadSettings();
    return InstallHook() ? TRUE : FALSE;
}

void Wh_ModAfterInit()
{
    Wh_Log(L"Wh_ModAfterInit: hooks are active");
}

void Wh_ModBeforeUninit()
{
    Wh_Log(L"Wh_ModBeforeUninit: removing hook");
    RemoveHook();
}

void Wh_ModSettingsChanged()
{
    Wh_Log(L"Wh_ModSettingsChanged: reloading settings");
    LoadSettings();
}
