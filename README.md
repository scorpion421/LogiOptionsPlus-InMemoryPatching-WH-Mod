# Logi Options+ Smooth Scroll for All Apps (experimental)

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Windhawk](https://img.shields.io/badge/Windhawk-mod-blue)](https://windhawk.net/)
[![Platform](https://img.shields.io/badge/platform-Windows%20x64-0078D6?logo=windows&logoColor=white)](#compatibility)
![Architecture](https://img.shields.io/badge/arch-amd64-lightgrey)
![Status](https://img.shields.io/badge/status-experimental-orange)
[![Stable build](https://img.shields.io/badge/stable%20build-README.md-brightgreen)](README.md)

> **This is an experimental build.** Use the [stable mod](README.md) unless you are testing this one deliberately. It has been validated against one shipping agent version; its whole point is what happens on the *next* one, and that has not happened yet.

Functionally identical to the stable version: it enables high-resolution smooth scrolling for applications you list, and never removes what Logi Options+ already detects on its own.

What differs is how it finds the place to patch.

## Why this exists

The stable build carries one hardcoded byte signature per agent version, plus one hand-written assembly handler each. Every Logi Options+ release that touches the relevant function needs a new signature extracted by hand from a disassembler, a new handler written to match, and a new mod release. Until all that happens, the mod is silently inert -- it loads, finds nothing, and does nothing.

That has already happened once during this project: agent 2.06 shipped, no signature matched, and the mod needed an update to come back to life.

This build derives the patch site from the *structure* of the code instead of from its exact bytes, and generates its trampoline at runtime from what it finds. The goal is that a new agent version costs no code at all.

## How it works

Where the stable build matches bytes, this build reads instructions.

1. **Find the anchor.** Locate the literal `firefox.exe` in the agent's read-only data.
2. **Find its use.** Scan the executable sections for a RIP-relative `lea rdx, [rip+disp32]` that resolves to that literal. This is the argument setup for the comparison against the browser name.
3. **Find the length check.** Before it sits `cmp <reg>, 11` followed by a `jne`, with the same register handed to the comparison as its size argument. Eleven is the length of `firefox.exe`. The agent chains several name comparisons; the anchor's own length selects the right link in that chain.
4. **Follow the branch.** The `jne` lands in the tail that produces the result. The store of that result sits at or just after it, once the matching and non-matching paths converge. That store is the patch site.
5. **Read the pointer setup.** Before the length check sits the small-string-optimization triple (`lea rcx, [frame+disp]` / `cmp <reg>, 15 or 16` / `cmov rcx, <reg>`) that produces the pointer to the name.

Nothing above depends on an exact byte value. Displacements, register allocation, the two encodings of the SSO check and both `jne` forms all vary across the known agent versions, and all are decoded rather than matched.

### Runtime trampoline

The relay stub is assembled at runtime from what discovery found:

- the pointer setup is copied verbatim out of the agent,
- the length register is encoded into a `mov rdx`,
- the agent's own verdict is passed along in `r8b`,
- the instructions being overwritten are copied out before the patch is applied and replayed inside the stub.

One generic path replaces the six hand-written assembly handlers. For the agent version this was validated against, the generated stub came out byte-for-byte identical to the hand-written handler for that version.

### Pass-through

The hook is a patch at a single address, so it necessarily fires for every process -- including the browsers Logi Options+ already detects. For those the mod cannot change the answer, so the stub checks two flags up front and falls straight through to the original instructions whenever the outcome cannot differ from what the agent already computed. RFLAGS are preserved exactly on that path, so the agent sees the state it would have seen without the mod at all.

## Safety

Deriving an address is more dangerous than matching one. A wrong signature simply fails to match; a wrong derivation patches into unrelated code inside a process that is not ours.

Every candidate must therefore satisfy all of the following before a single byte is written:

- the anchor string is in a read-only section of the main module, the code in an executable one,
- the length check, the branch and the size argument all decode exactly as expected, and the length check and the size argument name the same register,
- the pointer setup decodes to `lea` into RCX and `cmov` into RCX,
- the branch target lies inside the same executable section,
- within a short window at that target there is exactly `mov [<base>+0x28], al` followed by `mov <reg>, [<base>+0x8]`, both through the same base register,
- the overwritten range is at least 5 bytes, so an `E9` fits,
- the relay page is within reach of a 32-bit relative jump,
- and **exactly one** candidate survives. Ambiguity aborts rather than picks.

If anything fails, the mod does nothing and says why in the log. When an anchor reference cannot be resolved, the bytes around it are dumped as hex so an unfamiliar layout can be read out of the log rather than guessed at. That dump is what made the difference during development -- twice.

## Verification

Discovery and code generation were exercised:

- against reconstructions of all six known agent layouts (1.00 through 2.06), with no version knowledge supplied -- the correct patch site, patch length, setup sequence and length register were recovered in every case,
- against the **actual instruction bytes captured from a shipping agent (2.06)**, where the anchor is the second link in a chain of name comparisons and the branch after its length check lands two instructions short of the patch site. Discovery selects the correct link and the correct site,
- by **executing the generated stub for real**, checking correct arguments in both the small-string and heap cases, correct storage of the result, and preservation of R9, R10, R11 and RFLAGS,
- against **eleven deliberately corrupted variants** (wrong compared length, mismatched registers, altered branch, wrong store target or offset, and others), all of which were refused rather than patched.

It was then run against a live agent, where it located the patch site, generated its stub and hooked successfully, with the values matching what had been predicted from the captured bytes.

**What this does not prove:** that it survives the next agent release. That is the entire purpose of the build, and it can only be demonstrated when Logitech next ships a change to this function and the mod keeps working without an update. Until then this is a promising first validation, not a guarantee.

## Installation

1. Install [Windhawk](https://windhawk.net/) if you do not already have it.
2. Make sure Logi Options+ is installed and its background agent (`logioptionsplus_agent.exe`) is running.
3. In Windhawk, create a new mod, paste the contents of `logioptionsplus-smooth-scroll-EXPERIMENTAL.wh.cpp`, and compile.
4. Enable the mod **with a debug output viewer running** (DebugView, DbgViewMini, or the Windhawk log pane). On an experimental build the log is the point.

Do not run this alongside the stable mod. Both patch the same address.

## Configuration

Two settings behave exactly as in the stable build:

**Additional applications** -- executable filenames (without a path) for which smooth scrolling should be enabled. Wildcards `*` and `?` are supported.

**Excluded applications** -- executable filenames for which smooth scrolling should be explicitly disabled. This overrides both the additional applications list and the agent's built-in browser detection.

One setting is specific to this build:

**Anchor string** -- the browser name literal used to locate the patch site, `firefox.exe` by default. Only change this if the log reports that the anchor was not found. Note that the length check keyed to it is derived from the string's own length, so a different anchor works as long as the agent compares against it the same way.

### Decision priority

For each foreground application, the outcome is decided in this order:

1. The application matches the **excluded** list -> smooth scrolling **off** (highest priority).
2. The agent's built-in browser detection matches -> smooth scrolling **on**.
3. The application matches the **additional** list -> smooth scrolling **on**.
4. No match -> smooth scrolling **off**.

## Reading the log

A successful start looks like this:

```
discover: anchor 'firefox.exe' found 1 time(s)
discover: patch site 00007FF6..., 8 bytes; setup 00007FF6..., 12 bytes; length register r3
install: stub at 00007FF6..., 188 bytes of code
install: patch applied at 00007FF6...
init: hook active
```

Afterwards, only *matches* are logged, and only for applications from your lists. Natively detected browsers take the pass-through path and never appear -- that is correct behaviour, not a missing hook.

What the failure messages mean:

| Message | Meaning |
| --- | --- |
| `anchor '...' not found in read-only data` | The literal is absent. Either the wrong anchor, or the agent no longer stores it as a plain string. |
| `reference at ... does not match the expected shape` | The anchor was found and used, but the surrounding code is not the shape this build understands. A hex dump follows -- that dump is what is needed to teach it the new layout. |
| `no candidate passed validation` | Every reference was rejected. Nothing was patched. |
| `N distinct candidates, refusing to choose` | Ambiguity. Deliberately refuses rather than guessing, and lists the candidate addresses. |

If the mod goes quiet after a Logi Options+ update, the log will say which of these happened. That is exactly the information needed to fix it -- and if the failure is the second one, the hex dump usually makes the fix a small one.

## Compatibility

- **Architecture:** 64-bit (amd64) only. Every decoder assumes 64-bit encodings.
- **Target process:** `logioptionsplus_agent.exe`.
- **No files written to disk.** Everything happens in memory.
- Validated against agent versions 1.00 through 2.06 in reconstruction, and against 2.06 live.

When the mod is disabled or removed, the original bytes are restored, so the agent keeps running without a restart. The stub page is intentionally leaked rather than freed -- another thread may still be executing inside it at that moment, and one page is the cheap side of that trade.

## Relationship to the stable build

| | Stable | Experimental |
| --- | --- | --- |
| Finding the patch site | Hardcoded byte signatures, one per version | Decoded from the code's structure |
| Trampoline | Six hand-written assembly handlers | Generated at runtime |
| New agent version | Needs a new signature and handler | Costs no code, in theory |
| Failure mode | Silently inert | Silently inert, with a diagnostic hex dump |
| Risk if the assumption breaks | Nothing is patched | More validation gates, but a wider surface |

Both share the same decision logic, the same settings, and the same pass-through behaviour.

## Credits

- Original project and reverse-engineering work: [igvk/LogiOptionsPlus-InMemoryPatching](https://github.com/igvk/LogiOptionsPlus-InMemoryPatching).
- Windhawk modding framework: [ramensoftware/windhawk](https://github.com/ramensoftware/windhawk).

## License

MIT, matching the license of the original project.
