// ==WindhawkMod==
// @id              logioptionsplus-smooth-scroll-experimental
// @name            Logi Options+ Smooth Scroll for All Apps (experimental)
// @description     Structural-discovery build. Locates the patch site by string cross-reference instead of hardcoded signatures, and generates its trampoline at runtime.
// @version         3.0.0-exp4
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

1. Locate the literal `firefox.exe` in the agent's read-only data.
2. Scan the agent's code for a RIP-relative `lea rdx, [rip+disp32]` that resolves
   to that literal. This is the argument setup for the comparison against the
   browser name.
3. Before it, find the length pre-check the compiler emits ahead of the
   comparison: `cmp <reg>, 11` followed by a `jne`, with the same register handed
   to the comparison as its size argument. Eleven is the length of
   `firefox.exe`. The agent chains several such comparisons; the anchor's own
   length selects the right link.
4. Follow the `jne`. It lands in the tail that produces the result, and the
   store of that result sits at or just after it, once the matching and
   non-matching paths converge. That store is the patch site.
5. Before the length check sits the small-string-optimization triple
   (`lea rcx, [frame+disp]` / `cmp <reg>, 15 or 16` / `cmov rcx, <reg>`) that
   produces the pointer to the name.

Nothing above depends on exact byte values. Displacements, register allocation
and the two encodings of the SSO check all vary across the known agent versions,
and all are decoded rather than matched.

## Runtime trampoline

The relay stub is assembled at runtime from what discovery found: the SSO triple
is copied verbatim, the length register is encoded into a `mov rdx`, and the
instructions being overwritten are copied out before the patch is applied and
replayed inside the stub. One generic path replaces the six hand-written
assembly handlers, so a new agent version costs no code.

## Safety

Deriving an address is more dangerous than matching one: a wrong signature match
simply fails, while a wrong derivation patches into unrelated code. Every
candidate must therefore satisfy all of the following before a single byte is
written:

- the string is in a read-only section of the main module, the code in an
  executable one,
- the length check, the `jne` and the `mov r8` decode exactly as expected, and
  the length check and the `mov r8` name the same register,
- the SSO triple decodes to `lea` into RCX and `cmov` into RCX,
- the `jne` target lies inside the same executable section,
- within a short window at that target there is exactly `mov [<base>+0x28], al`
  followed by `mov <reg>, [<base>+0x8]`, both through the same base register,
- the overwritten range is at least 5 bytes, so an `E9` fits,
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
- and, most importantly, against the actual instruction bytes captured from a
  shipping agent (2.06). There the anchor is the second link in a chain of name
  comparisons, and the branch after its length check lands two instructions
  short of the patch site. Discovery selects the correct link and the correct
  site, and the stub it generates is identical to the hand-written handler for
  that version.

RFLAGS are preserved exactly on the pass-through path. On the deciding path they
are clobbered, as they were in every previous version and upstream: two code
paths converge at the patch site, so nothing downstream can depend on them.

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
    - iexplore.exe
  $name: Excluded applications
  $description: Executable filenames to explicitly disable smooth scrolling for. Overrides both the additional apps list and the built-in Logi Options+ browser detection. Wildcards * and ? are supported.
- anchorString: firefox.exe
  $name: Anchor string
  $description: The browser name literal used to locate the patch site. Only change this if the log reports that the anchor was not found.
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
};

// Published through an atomic pointer rather than held by value.
//
// The decision handler runs on the agent's threads while Wh_ModSettingsChanged
// runs on a Windhawk thread. Replacing a by-value ModSettings would destroy
// vectors a handler might be iterating at that moment. Swapping a pointer is
// atomic and keeps the hot path lock-free.
//
// Retired generations are intentionally leaked: a handler may still be reading
// one when the swap happens, settings changes are rare and user-driven, and the
// leak is a few hundred bytes.
static std::atomic<const ModSettings*> g_settings{nullptr};

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
// name once into a stack buffer.
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

extern "C" bool patched_switch_foreground_process_handler(
    const char* name, size_t length, bool previous_check)
{
    const ModSettings* s = g_settings.load(std::memory_order_acquire);
    if (!s || !name)
        return previous_check;   // never worse than the agent's own answer

    const bool have_disabled = !s->disabled.empty();
    const bool have_enabled  = !s->enabled.empty();

    // Mirror of the stub's bypass conditions, kept as a safety net in case the
    // stub ever calls through when it did not have to.
    if (!have_disabled) {
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
            Wh_Log(L"handler: '%S' matched disabled '%S' -> false", buf, glob.c_str());
            return false;
        }
    }
    if (previous_check)
        return true;
    for (const auto& glob : s->enabled) {
        if (glob_match(buf, glob.c_str())) {
            Wh_Log(L"handler: '%S' matched enabled '%S' -> true", buf, glob.c_str());
            return true;
        }
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
    uint8_t* base = nullptr;
    size_t   size = 0;
    bool     exec = false;
    bool     read = false;
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
        e.base = base + s.VirtualAddress;
        e.size = s.Misc.VirtualSize;
        e.exec = (s.Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
        e.read = (s.Characteristics & IMAGE_SCN_MEM_READ) != 0;
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

// lea r64, [base + disp] with the destination in the ModRM.reg field.
// Rejects RIP-relative forms: the copy in the stub would resolve elsewhere.
// Sets len and dest.
static bool DecodeLea(const uint8_t* p, size_t avail, size_t& len, unsigned& dest)
{
    if (avail < 3) return false;
    const uint8_t rex = p[0];
    if ((rex & 0xF8) != 0x48) return false;     // REX.W, optional R/X/B
    if (p[1] != 0x8D) return false;             // LEA
    const uint8_t modrm = p[2];
    const unsigned mod = modrm >> 6;
    const unsigned rm  = modrm & 7;
    if (mod == 3) return false;                 // register form is not a memory lea
    if (mod == 0 && rm == 5) return false;      // RIP-relative

    size_t n = 3;
    if (rm == 4) {                              // SIB present
        if (avail < n + 1) return false;
        n += 1;
    }
    if (mod == 1)      n += 1;
    else if (mod == 2) n += 4;
    else if (mod == 0) { /* no displacement */ }

    if (avail < n) return false;
    len  = n;
    dest = reg_of(rex, (modrm >> 3) & 7, 0x04); // REX.R
    return true;
}

// cmp r64, imm8   ->  REX.W [+B] 83 /7 ib
static bool DecodeCmpImm8(const uint8_t* p, size_t avail, unsigned& reg, int& imm)
{
    if (avail < 4) return false;
    const uint8_t rex = p[0];
    if ((rex & 0xFE) != 0x48) return false;     // 48 or 49
    if (p[1] != 0x83) return false;
    const uint8_t modrm = p[2];
    if ((modrm >> 6) != 3) return false;        // register operand
    if (((modrm >> 3) & 7) != 7) return false;  // /7 = CMP
    reg = reg_of(rex, modrm & 7, 0x01);         // REX.B
    imm = static_cast<int8_t>(p[3]);
    return true;
}

// cmov<cc> r64, r64 with the destination in ModRM.reg.
// Accepts the two conditions the agent has used across versions:
//   0F 43 = CMOVAE/NB, 0F 47 = CMOVA/NBE.
static bool DecodeCmovToReg(const uint8_t* p, size_t avail, unsigned& dest)
{
    if (avail < 4) return false;
    const uint8_t rex = p[0];
    if ((rex & 0xF8) != 0x48) return false;
    if (p[1] != 0x0F) return false;
    if (p[2] != 0x43 && p[2] != 0x47) return false;
    const uint8_t modrm = p[3];
    if ((modrm >> 6) != 3) return false;
    dest = reg_of(rex, (modrm >> 3) & 7, 0x04);
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
    const unsigned rm = modrm & 7;

    size_t n = i + 2;
    unsigned b;
    if (rm == 4) {                              // SIB
        if (avail < n + 1) return false;
        const uint8_t sib = p[n];
        if (((sib >> 3) & 7) != 4) return false;  // no index register
        if ((sib >> 6) != 0) return false;        // scale must be 1
        b = reg_of(rex, sib & 7, 0x01);           // REX.B
        n += 1;
    } else {
        b = reg_of(rex, rm, 0x01);
    }
    if (avail < n + 1) return false;
    disp = static_cast<int8_t>(p[n]);
    n += 1;

    len  = n;
    base = b;
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
    const unsigned rm = modrm & 7;

    size_t n = 3;
    unsigned b;
    if (rm == 4) {
        if (avail < n + 1) return false;
        const uint8_t sib = p[n];
        if (((sib >> 3) & 7) != 4) return false;
        if ((sib >> 6) != 0) return false;
        b = reg_of(rex, sib & 7, 0x01);
        n += 1;
    } else {
        b = reg_of(rex, rm, 0x01);
    }
    if (avail < n + 1) return false;
    disp = static_cast<int8_t>(p[n]);
    n += 1;

    len  = n;
    base = b;
    return true;
}

// ===========================================================================
// SECTION: discovery
// ===========================================================================

// Everything the code generator needs, derived from the agent's own code.
struct Site {
    uint8_t* triple      = nullptr;  // start of the lea/cmp/cmov sequence
    size_t   triple_len  = 0;        // bytes to copy verbatim into the stub
    unsigned len_reg     = 0;        // register holding the name length
    uint8_t* hook        = nullptr;  // where the E9 goes
    size_t   hook_len    = 0;        // bytes the E9 overwrites
    uint8_t* resume      = nullptr;  // hook + hook_len
};

// Length of "firefox.exe" and friends, as the agent compares it.
static constexpr int k_anchor_len = 11;

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
    uint8_t* lo = (p - before < sec.base) ? sec.base : p - before;
    uint8_t* hi = (p + after > sec.base + sec.size) ? sec.base + sec.size : p + after;

    for (uint8_t* line = lo; line < hi; line += 16) {
        wchar_t buf[128];
        int n = swprintf(buf, 96, L"  %p%s ", line, (line <= p && p < line + 16) ? L" *" : L"  ");
        for (uint8_t* b = line; b < line + 16 && b < hi; b++)
            n += swprintf(buf + n, 8, L"%02X ", *b);
        Wh_Log(L"%s", buf);
    }
}

// Walks back from the anchor reference to the structure that precedes it and
// fills in 'out'. Returns false if anything does not decode as expected.
//
// The length pre-check is searched for within a window rather than assumed to
// sit at a fixed offset: what the compiler emits between the check and the
// comparison setup is not fixed, and an earlier build of this file assumed an
// order that the shipping agent does not use.
static bool ResolveFromAnchorRef(uint8_t* lea_rdx, const Section& code, Site& out)
{
    // Shape being looked for, reading forward:
    //   <lea rcx,[frame+disp]> <cmp reg,15|16> <cmov rcx,reg>   the SSO triple
    //   <cmp len_reg, 11>                                       the length check
    //   <jne ...>              -> target is the patch site
    //   ... possibly a few instructions ...
    //   <lea rdx, [rip -> anchor]>                              <- entry point
    constexpr size_t k_window = 48;   // how far back the length check may sit

    if (lea_rdx < code.base + k_window + 24)
        return false;

    // Nearest match first: the length check is the last branch before the
    // comparison it guards.
    for (size_t back = 6; back <= k_window; back++) {
        uint8_t* p_cmplen = lea_rdx - back;

        unsigned len_reg = 0;
        int imm = 0;
        if (!DecodeCmpImm8(p_cmplen, 4, len_reg, imm))
            continue;
        if (imm != k_anchor_len)
            continue;

        // The branch that skips the comparison, in either encoding.
        uint8_t* p_j = p_cmplen + 4;
        uint8_t* target = nullptr;
        size_t   j_len = 0;
        if (p_j[0] == 0x75) {                       // jne rel8
            target = p_j + 2 + static_cast<int8_t>(p_j[1]);
            j_len  = 2;
        } else if (p_j[0] == 0x0F && p_j[1] == 0x85) {   // jne rel32
            int32_t rel;
            std::memcpy(&rel, p_j + 2, sizeof rel);
            target = p_j + 6 + rel;
            j_len  = 6;
        } else {
            continue;
        }
        if (p_j + j_len > lea_rdx)                  // the branch must precede us
            continue;

        // The branch target is not the patch site itself.
        //
        // The agent chains several name comparisons. A failed length check
        // jumps to the tail that produces the result -- typically the
        // 'xor al, al' that means "no match" -- and the store follows a couple
        // of instructions later, after the two result paths converge. In some
        // builds the two coincide, in others they do not, so the store is
        // searched for forward from the branch target rather than assumed to
        // be at it.
        if (target < code.base || target >= code.base + code.size)
            continue;

        uint8_t* hook = nullptr;
        size_t   hook_len = 0;
        {
            // Only the small result tail is scanned. Anything longer would
            // risk latching onto an unrelated store.
            constexpr size_t k_tail = 16;
            uint8_t* limit = target + k_tail;
            if (limit > code.base + code.size)
                limit = code.base + code.size;

            for (uint8_t* q = target; q < limit; q++) {
                const size_t avail = static_cast<size_t>((code.base + code.size) - q);
                size_t len = 0;
                if (ValidateHookSite(q, avail, len)) {
                    hook     = q;
                    hook_len = len;
                    break;
                }
            }
        }
        if (!hook)
            continue;

        // The SSO triple ends where the length check begins. Its lea is either
        // four or five bytes depending on whether the frame register needs a
        // SIB byte, so try both rather than assuming.
        uint8_t* triple = nullptr;
        size_t   triple_len = 0;
        for (size_t lea_len = 4; lea_len <= 5; lea_len++) {
            uint8_t* cand = p_cmplen - (lea_len + 8);
            if (cand < code.base)
                continue;

            size_t   got_len = 0;
            unsigned lea_dst = 0;
            if (!DecodeLea(cand, lea_len, got_len, lea_dst))
                continue;
            if (got_len != lea_len || lea_dst != 1)      // must load into RCX
                continue;

            unsigned sso_reg = 0;
            int sso_imm = 0;
            if (!DecodeCmpImm8(cand + lea_len, 4, sso_reg, sso_imm))
                continue;
            if (sso_imm != 15 && sso_imm != 16)          // the encodings seen
                continue;

            unsigned cmov_dst = 0;
            if (!DecodeCmovToReg(cand + lea_len + 4, 4, cmov_dst))
                continue;
            if (cmov_dst != 1)                           // must select into RCX
                continue;

            triple     = cand;
            triple_len = lea_len + 8;
            break;
        }
        if (!triple)
            continue;

        // Required cross-check: between the branch and the anchor reference, the
        // same register that was length-checked must be handed to the
        // comparison as its size argument (R8 under the Win64 convention).
        //
        // This is what separates the real call site from any other comparison
        // against the value 11 that happens to sit near an anchor reference.
        // Its position is not fixed, so it is searched for rather than assumed,
        // but its absence is fatal.
        bool size_arg_ok = false;
        for (uint8_t* q = p_j + j_len; q + 3 <= lea_rdx; q++) {
            const bool w64 = (q[0] == 0x4C || q[0] == 0x4D);   // mov r8, r64
            const bool w32 = (q[0] == 0x44 || q[0] == 0x45);   // mov r8d, r32
            if (!(w64 || w32) || q[1] != 0x8B)
                continue;
            if ((q[2] >> 6) != 3)
                continue;
            if (((q[2] >> 3) & 7) != 0)                        // destination R8
                continue;
            const unsigned src = (q[2] & 7) | ((q[0] & 1) ? 8u : 0u);
            if (src == len_reg) { size_arg_ok = true; break; }
        }
        if (!size_arg_ok)
            continue;

        out.triple     = triple;
        out.triple_len = triple_len;
        out.len_reg    = len_reg;
        out.hook       = hook;
        out.hook_len   = hook_len;
        out.resume     = hook + hook_len;
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
        if (s.exec || !s.read || s.size < alen + 1)
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
            if (!ResolveFromAnchorRef(s.base + i, s, cand)) {
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
           out.hook, out.hook_len, out.triple, out.triple_len, out.len_reg);
    return true;
}

// ===========================================================================
// SECTION: codegen
//
// The stub is written into a page allocated within reach of a 32-bit relative
// jump from the patch site. Layout:
//
//   [ control block ]  two flag bytes, written by LoadSettings
//   [ data slots    ]  absolute addresses of the handler and the resume point
//   [ code          ]  the stub proper
//
// The flags and both addresses live in the same page as the code, so every
// reference is a short RIP-relative displacement. Reaching back into this DLL
// instead could exceed the 2 GB a disp32 can express.
//
// Register discipline, in the order the stub runs:
//   entry      - the agent's mid-function state; AL holds its own verdict
//   bypass     - saves and restores RFLAGS; touches no register
//   full path  - restores RSP first, so a copied RSP-relative lea is correct
//   call       - RCX/RDX/R8/RAX are arguments and return; R9/R10/R11 and
//                XMM0-XMM5 are saved because the agent may still need them
//   epilogue   - replays the overwritten instructions, then resumes
// ===========================================================================

struct StubLayout {
    static constexpr size_t off_veto    = 0;    // 1 byte
    static constexpr size_t off_extra   = 1;    // 1 byte
    static constexpr size_t off_handler = 8;    // 8 bytes, absolute
    static constexpr size_t off_resume  = 16;   // 8 bytes, absolute
    static constexpr size_t off_code    = 32;   // 16-byte aligned
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

// Builds the stub. 'stub' is the page base, 'site' the discovered structure.
// Returns the number of code bytes written, or 0 on failure.
static size_t BuildStub(uint8_t* stub, size_t stub_cap, const Site& site)
{
    if (stub_cap <= StubLayout::off_code)
        return 0;

    // Data slots first.
    stub[StubLayout::off_veto]  = 0;
    stub[StubLayout::off_extra] = 0;
    void* handler = reinterpret_cast<void*>(&patched_switch_foreground_process_handler);
    std::memcpy(stub + StubLayout::off_handler, &handler, sizeof handler);
    std::memcpy(stub + StubLayout::off_resume,  &site.resume, sizeof site.resume);

    uint8_t* code = stub + StubLayout::off_code;
    Emitter e(code, stub_cap - StubLayout::off_code);

    // Displacement from the end of a RIP-relative instruction to a data slot.
    auto rip_to = [&](size_t slot_off, size_t insn_end_from_code) -> uint32_t {
        const intptr_t from = static_cast<intptr_t>(
            reinterpret_cast<uintptr_t>(code) + insn_end_from_code);
        const intptr_t to = static_cast<intptr_t>(
            reinterpret_cast<uintptr_t>(stub) + slot_off);
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

    // The agent's own pointer setup, copied verbatim. RSP is back at its entry
    // value, so an RSP-relative displacement inside it stays correct.
    e.raw(site.triple, site.triple_len);          // -> RCX = name

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

    // call [rip + handler]   -- indirect, so nothing extra is clobbered
    e.u8(0xFF); e.u8(0x15);
    e.u32(rip_to(StubLayout::off_handler, e.n + 4));

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
        std::memcpy(code + slot, &d, sizeof d);
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
    bool     active   = false;
    uint8_t* address  = nullptr;      // patch site inside the agent
    uint8_t  original[16] = {};       // bytes replaced by the jump
    size_t   length   = 0;
    uint8_t* stub     = nullptr;      // allocated page
};

static HookState g_hook;

// Pointers into the stub's control block, so settings changes reach the flags
// the generated code actually reads.
static volatile uint8_t* g_veto_flag  = nullptr;
static volatile uint8_t* g_extra_flag = nullptr;

static inline uintptr_t sat_add(uintptr_t a, uintptr_t b)
{
    const uintptr_t r = a + b;
    return (r < a) ? UINTPTR_MAX : r;
}
static inline uintptr_t sat_sub(uintptr_t a, uintptr_t b)
{
    return (a < b) ? 0 : (a - b);
}

// Allocates an executable page within +/-2GB of target, so an E9 can reach it.
static void* AllocatePageNearAddress(void* target)
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    const size_t page = si.dwPageSize;
    constexpr uintptr_t max_disp = 0x7FFFFF00;

    const uintptr_t start = reinterpret_cast<uintptr_t>(target) & ~(static_cast<uintptr_t>(page) - 1);
    uintptr_t lo = reinterpret_cast<uintptr_t>(si.lpMinimumApplicationAddress);
    uintptr_t hi = reinterpret_cast<uintptr_t>(si.lpMaximumApplicationAddress);
    const uintptr_t a = sat_sub(start, max_disp);
    lo = (a >= lo) ? a : lo;
    const uintptr_t b = sat_add(start, max_disp);
    hi = (b <= hi) ? b : hi;

    uintptr_t up = start, down = start;
    do {
        up   = sat_add(up, page);
        down = sat_sub(down, page);
        if (up < hi) {
            if (void* p = VirtualAlloc(reinterpret_cast<void*>(up), page,
                                       MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE))
                return p;
        }
        if (down > lo) {
            if (void* p = VirtualAlloc(reinterpret_cast<void*>(down), page,
                                       MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE))
                return p;
        }
    } while (up < hi || down > lo);

    return nullptr;
}

// Discovers the site, builds the stub and applies the patch.
static bool InstallHook(const std::string& anchor)
{
    Site site;
    if (!DiscoverSite(anchor, site))
        return false;

    if (site.hook_len > sizeof g_hook.original) {
        Wh_Log(L"install: patch length %zu exceeds the backup buffer", site.hook_len);
        return false;
    }

    SYSTEM_INFO si;
    GetSystemInfo(&si);
    const size_t page = si.dwPageSize;

    auto* stub = static_cast<uint8_t*>(AllocatePageNearAddress(site.hook));
    if (!stub) {
        Wh_Log(L"install: no page available within reach of the patch site");
        return false;
    }

    const size_t code_len = BuildStub(stub, page, site);
    if (code_len == 0) {
        Wh_Log(L"install: could not build the stub");
        VirtualFree(stub, 0, MEM_RELEASE);
        return false;
    }
    Wh_Log(L"install: stub at %p, %zu bytes of code", stub, code_len);

    // Confirm the jump can actually reach before touching the agent.
    const intptr_t delta =
        (stub + StubLayout::off_code) - (site.hook + k_rel_jmp_size);
    if (delta < INT32_MIN || delta > INT32_MAX) {
        Wh_Log(L"install: stub out of range (delta = %lld)", static_cast<long long>(delta));
        VirtualFree(stub, 0, MEM_RELEASE);
        return false;
    }

    // Publish the flag pointers before the patch goes live, so the first call
    // through the stub already sees the current settings.
    g_veto_flag  = stub + StubLayout::off_veto;
    g_extra_flag = stub + StubLayout::off_extra;
    if (const ModSettings* s = g_settings.load(std::memory_order_acquire)) {
        *g_veto_flag  = s->disabled.empty() ? 0 : 1;
        *g_extra_flag = s->enabled.empty()  ? 0 : 1;
    }

    DWORD old = 0;
    if (!VirtualProtect(site.hook, site.hook_len, PAGE_EXECUTE_READWRITE, &old)) {
        Wh_Log(L"install: VirtualProtect failed, error = %u", GetLastError());
        VirtualFree(stub, 0, MEM_RELEASE);
        g_veto_flag = g_extra_flag = nullptr;
        return false;
    }

    std::memcpy(g_hook.original, site.hook, site.hook_len);
    g_hook.address = site.hook;
    g_hook.length  = site.hook_len;
    g_hook.stub    = stub;

    const int32_t rel = static_cast<int32_t>(delta);
    site.hook[0] = 0xE9;
    std::memcpy(site.hook + 1, &rel, sizeof rel);
    if (site.hook_len > k_rel_jmp_size)
        std::memset(site.hook + k_rel_jmp_size, 0x90, site.hook_len - k_rel_jmp_size);

    DWORD tmp = 0;
    VirtualProtect(site.hook, site.hook_len, old, &tmp);
    FlushInstructionCache(GetCurrentProcess(), site.hook, site.hook_len);
    FlushInstructionCache(GetCurrentProcess(), stub, page);

    g_hook.active = true;
    Wh_Log(L"install: patch applied at %p", site.hook);
    return true;
}

// Restores the agent's original bytes. Must run before this DLL is unmapped.
static void RemoveHook()
{
    if (!g_hook.active)
        return;

    DWORD old = 0;
    if (VirtualProtect(g_hook.address, g_hook.length, PAGE_EXECUTE_READWRITE, &old)) {
        std::memcpy(g_hook.address, g_hook.original, g_hook.length);
        DWORD tmp = 0;
        VirtualProtect(g_hook.address, g_hook.length, old, &tmp);
        FlushInstructionCache(GetCurrentProcess(), g_hook.address, g_hook.length);
        Wh_Log(L"remove: original bytes restored at %p", g_hook.address);
    } else {
        Wh_Log(L"remove: VirtualProtect failed, error = %u", GetLastError());
    }

    // The stub page is deliberately not freed: another thread may still be
    // executing inside it at this moment. One leaked page is the safe choice.
    g_veto_flag  = nullptr;
    g_extra_flag = nullptr;
    g_hook.active = false;
}

// ===========================================================================
// SECTION: settings loader
// ===========================================================================

// Reads a Windhawk string array. Windhawk has no length key: iterate until an
// empty string comes back. key_fmt must contain a single %d.
static std::vector<std::string> ReadStringArray(const wchar_t* key_fmt)
{
    std::vector<std::string> out;
    for (int i = 0; ; i++) {
        PCWSTR raw = Wh_GetStringSetting(key_fmt, i);
        if (!raw || raw[0] == L'\0') {
            Wh_FreeStringSetting(raw);
            break;
        }
        std::string entry;
        for (const wchar_t* p = raw; *p; p++) {
            const wchar_t w = *p;
            entry += (w < 0x80) ? ascii_lower(static_cast<char>(w)) : '?';
        }
        Wh_FreeStringSetting(raw);
        if (!entry.empty())
            out.emplace_back(std::move(entry));
    }
    return out;
}

static std::string ReadAnchor()
{
    std::string anchor;
    PCWSTR raw = Wh_GetStringSetting(L"anchorString");
    if (raw) {
        for (const wchar_t* p = raw; *p; p++) {
            const wchar_t w = *p;
            if (w < 0x80)
                anchor += ascii_lower(static_cast<char>(w));
        }
    }
    Wh_FreeStringSetting(raw);
    if (anchor.empty())
        anchor = "firefox.exe";
    return anchor;
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

    const size_t n_enabled  = fresh->enabled.size();
    const size_t n_disabled = fresh->disabled.size();

    // Publish atomically. The retired generation is intentionally leaked; see
    // the note on g_settings.
    g_settings.store(fresh, std::memory_order_release);

    // Update the flags the stub reads, after the settings they describe. In
    // this order the flags can only ever be conservative: the stub may call
    // through when it no longer needs to, which is harmless. Bypassing while an
    // excluded pattern exists cannot happen.
    if (g_veto_flag)  *g_veto_flag  = n_disabled ? 1 : 0;
    if (g_extra_flag) *g_extra_flag = n_enabled  ? 1 : 0;

    Wh_Log(L"settings: %zu enabled, %zu disabled", n_enabled, n_disabled);
}

// ===========================================================================
// SECTION: windhawk callbacks
// ===========================================================================

BOOL Wh_ModInit()
{
    Wh_Log(L"init: experimental structural-discovery build starting");
    LoadSettings();
    return InstallHook(ReadAnchor()) ? TRUE : FALSE;
}

void Wh_ModAfterInit()
{
    Wh_Log(L"init: hook active");
}

void Wh_ModBeforeUninit()
{
    Wh_Log(L"uninit: removing hook");
    RemoveHook();
}

void Wh_ModSettingsChanged()
{
    Wh_Log(L"settings: reloading");
    LoadSettings();
}
