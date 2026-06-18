// ==WindhawkMod==
// @id              logioptionsplus-smooth-scroll
// @name            Logi Options+ Smooth Scroll for All Apps
// @description     Enables high-resolution smooth mouse wheel scrolling in any application, not just browsers. Port of igvk/LogiOptionsPlus-InMemoryPatching.
// @version         2.0.1
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
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <cctype>
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

static ModSettings g_settings;

// ===========================================================================
// Glob matching (direct port from utilities.cpp)
// glob string must already be lowercase; text is compared case-insensitively.
// ===========================================================================

static bool glob_match(const char* text, const char* glob)
{
    const char* text_backup = nullptr;
    const char* glob_backup = nullptr;
    while (*text != '\0') {
        if (*glob == '*') {
            text_backup = text;
            glob_backup = ++glob;
        } else if ((*glob == '?' && *text != '/') ||
                   *glob == static_cast<char>(std::tolower(static_cast<unsigned char>(*text)))) {
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

extern "C" bool patched_switch_foreground_process_handler(
    const char* name, size_t length, bool previous_check)
{
    // Build a lowercase, null-terminated copy for matching.
    std::string lower_name(name, length);
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    // No per-invocation logging here: this function is called on every
    // foreground-window change and during video playback / fullscreen
    // transitions. Logging every call slowed the handler enough to interfere
    // with the window manager's fullscreen detection (taskbar remained
    // visible). Only match outcomes are logged, below.

    // Strip any path component so patterns match just the filename.
    const auto sep = lower_name.rfind('\\');
    const char* basename = (sep != std::string::npos)
                           ? lower_name.c_str() + sep + 1
                           : lower_name.c_str();

    for (const auto& glob : g_settings.disabled) {
        if (glob_match(basename, glob.c_str())) {
            Wh_Log(L"handler: '%S' matched disabled '%S' -> false",
                   basename, glob.c_str());
            return false;
        }
    }
    if (previous_check)
        return true;
    for (const auto& glob : g_settings.enabled) {
        if (glob_match(basename, glob.c_str())) {
            Wh_Log(L"handler: '%S' matched enabled '%S' -> true",
                   basename, glob.c_str());
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
// the agent's mid-function state. The shadow space of the target function may
// be reused by the callee, matching the original project's reasoning.
// ===========================================================================

extern "C" {
    // Set to the return address (instruction after the overwritten hook bytes)
    // before arming each hook. Shared single slot: only one version is ever
    // hooked per process, matching the original code.
    void* original_jump_address = nullptr;

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
    "    lea rcx, [rsp+0xB8-0x40]\n"
    "    cmp rdi, 0x10\n"
    "    cmovnb rcx, rbx\n"            // name
    "    mov rdx, rsi\n"              // length
    "    movzx r8, al\n"             // previous check
    "    call patched_switch_foreground_process_handler\n"
    "    mov [rbp+0x28], al\n"
    "    mov rdi, [rbp+0x8]\n"
    "    jmp [rip + original_jump_address]\n"

    // ---- V146 ----
    ".globl injected_handler_V146\n"
    "injected_handler_V146:\n"
    "    lea rcx, [rbp+0x57-0x58]\n"
    "    cmp r14, 0x10\n"
    "    cmovnb rcx, rsi\n"            // name
    "    mov rdx, rbx\n"              // length
    "    movzx r8, al\n"             // previous check
    "    call patched_switch_foreground_process_handler\n"
    "    mov [r12+0x28], al\n"
    "    mov r12, [r12+0x8]\n"
    "    jmp [rip + original_jump_address]\n"

    // ---- V168 ----
    ".globl injected_handler_V168\n"
    "injected_handler_V168:\n"
    "    lea rcx, [rbp+0x57-0x78]\n"
    "    cmp r14, 0x10\n"
    "    cmovnb rcx, rdi\n"            // name
    "    mov rdx, rbx\n"              // length
    "    movzx r8, al\n"             // previous check
    "    call patched_switch_foreground_process_handler\n"
    "    mov [r12+0x28], al\n"
    "    mov r12, [r12+0x8]\n"
    "    jmp [rip + original_jump_address]\n"

    // ---- V186 ----
    ".globl injected_handler_V186\n"
    "injected_handler_V186:\n"
    "    lea rcx, [rbp+0x40-0x80]\n"
    "    cmp r14, 0x10\n"
    "    cmovnb rcx, rdi\n"            // name
    "    mov rdx, rbx\n"              // length
    "    movzx r8, al\n"             // previous check
    "    call patched_switch_foreground_process_handler\n"
    "    mov [r12+0x28], al\n"
    "    mov r12, [r12+0x8]\n"
    "    jmp [rip + original_jump_address]\n"

    // ---- V194 ----
    ".globl injected_handler_V194\n"
    "injected_handler_V194:\n"
    "    lea rcx, [rbp+0x70-0x70]\n"
    "    cmp rsi, 0xF\n"
    "    cmova rcx, rdi\n"             // name
    "    mov rdx, rbx\n"              // length
    "    movzx r8, al\n"             // previous check
    "    call patched_switch_foreground_process_handler\n"
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
static void WriteAbsoluteJump64(void* absJumpMemory, void* addrToJumpTo)
{
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
    void* relay = AllocatePageNearAddress(func2hook);
    if (!relay) {
        Wh_Log(L"InstallAllocateHook: unable to allocate page near target");
        return false;
    }
    WriteAbsoluteJump64(relay, reinterpret_cast<void*>(payloadFunction));

    constexpr uint8_t jmpInstruction = 0xE9;
    const uint32_t relAddr = static_cast<uint32_t>(
        static_cast<uint8_t*>(relay) -
        (static_cast<uint8_t*>(func2hook) + sizeof jmpInstruction + sizeof(uint32_t)));

    uint8_t* code = static_cast<uint8_t*>(func2hook);
    code[0] = jmpInstruction;
    std::memcpy(code + 1, &relAddr, sizeof relAddr);

    size_t remaining = injectSize - (sizeof jmpInstruction + sizeof relAddr);
    if (remaining > 0)
        std::memset(code + sizeof jmpInstruction + sizeof relAddr, 0x90, remaining);

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
                Wh_Log(L"InstallHook: hook bytes not found after signature (already patched?)");
                return false;
            }
            Wh_Log(L"InstallHook: hook point at %p", hook_addr);

            // Return address = instruction right after the overwritten bytes.
            original_jump_address = hook_addr + ver.hook_len;

            // Make the region writable, arm the hook, restore protection.
            DWORD oldProtect;
            if (!VirtualProtect(mem, size, PAGE_EXECUTE_READWRITE, &oldProtect)) {
                Wh_Log(L"InstallHook: VirtualProtect failed, error = %u", GetLastError());
                return false;
            }

            // Save original bytes for restoration on unload.
            std::memcpy(g_backup.original, hook_addr, ver.hook_len);
            g_backup.address = hook_addr;
            g_backup.length  = ver.hook_len;

            void* relay = nullptr;
            bool ok = InstallAllocateHook(hook_addr, ver.hook_len, ver.handler, &relay);
            VirtualProtect(mem, size, oldProtect, &oldProtect);
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
            entry += static_cast<char>(
                std::tolower(static_cast<unsigned char>(static_cast<char>(*p)))
            );
        }
        Wh_FreeStringSetting(raw);
        if (!entry.empty())
            result.emplace_back(std::move(entry));
    }
    return result;
}

static void LoadSettings()
{
    ModSettings fresh;
    fresh.enabled  = ReadStringArray(L"enabledApps[%d]");
    fresh.disabled = ReadStringArray(L"disabledApps[%d]");
    g_settings = std::move(fresh);
    Wh_Log(L"Settings loaded: %zu enabled, %zu disabled",
           g_settings.enabled.size(),
           g_settings.disabled.size());
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
