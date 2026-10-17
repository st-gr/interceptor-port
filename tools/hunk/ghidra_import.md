# Opening the Interceptor image in Ghidra through rea

> **WARNING: `REA_GHIDRA_LANGUAGE_ID=68000:BE:32:default` must be set in the environment of the rea process.** Without it the wrapped image is analysed as big-endian ARM (`ARM:BE:32:v8:default`) and Ghidra finds one function instead of 636. This applies to the `rea` command line and, equally, to the `rea mcp` server that Claude Code starts.

Inputs: `extracted/interceptor.bin` and `extracted/interceptor.map.json` from `tools/hunk/relocate.py` (flat big-endian image, base `0x10000`, 185 hunks). Both are git-ignored, as is everything below under `extracted/`.

## Why a wrapper is needed (tested)

rea 6.4.0 (`rea-agents`) accepts only ELF, PE/MZ and Mach-O headers. Results of opening each form:

- Raw `extracted/interceptor.bin`: `target_unavailable ... unsupported binary format`.
- ELF with `e_machine` 4 (EM_68K), made with `wrap_elf.py --machine 68k`: `target_unavailable ... unsupported ELF architecture` (tried with `mcp__rea__open_binary` on `extracted/interceptor-68k.elf`). rea's own header check only knows x86, x86_64, MIPS, ARM and ARM64, so Ghidra never gets to see the 68K label.
- ELF with `e_machine` 40 (ARM), big-endian, one `PT_LOAD` at `0x10000`: accepted. Big-endian (`EI_DATA` 2) is used for both variants; Ghidra resolves it to `ARM:BE:32:v8:default` when no override is set.

`open_binary` has no base-address or language parameter, so the base comes from the ELF segment and the language from the environment variable. The ARM label is only there to pass rea's header check.

## Procedure

```
. /sandbox/.tools/env.sh
export REA_GHIDRA_LANGUAGE_ID=68000:BE:32:default
cd /sandbox/interceptor-wasm
python tools/hunk/wrap_elf.py extracted/interceptor.bin extracted/interceptor-arm.elf --machine arm
```

`wrap_elf.py <image.bin> <out.elf> [--base 0x10000] [--machine arm|68k]` writes a 52-byte ELF header, one 32-byte program header (RWX `PT_LOAD`, `p_vaddr` = `p_paddr` = base, `p_filesz` = `p_memsz` = image length, `p_offset` 84) and the image. Tests: `cd tools/hunk && pytest -q`. Image offset N is address `0x10000 + N`, matching `interceptor.map.json`. The first analysis takes about 30 seconds of Ghidra start-up plus about 75 seconds of auto-analysis, and every command-line call repeats it.

### Command line, smoke test

Run each from `/sandbox/interceptor-wasm` with the two exports above. Add `--format json --full-output` and read `data.details.partial_observation.normalized_result` (or `raw_result` for search), because each call ends with `cleanup_incomplete ... kill EPERM` (see caveats) even though the analysis succeeded.

| query | command | expected |
|-------|---------|----------|
| overview | `rea inspect extracted/interceptor-arm.elf --format json --full-output` | `procedure_count` 636, `string_count` 343, segment 0x10000-0x58fec of 298988 bytes; `analysis_profile...language_id` is `68000:BE:32:default` |
| list functions | `rea search extracted/interceptor-arm.elf '.+' --mode regex --kind procedures --format json --full-output` | `raw_result` has 636 entries; the first is `0x10000 entry`, the second `0x10136` |
| strings | `rea search extracted/interceptor-arm.elf 'dos\.library' --mode regex --kind strings --format json --full-output` | one match at `0x1a39c` (use `'.+'` to list all 343) |
| bytes | `rea read-bytes extracted/interceptor-arm.elf 0x10000 --length 16 --format json --full-output` | `returned_bytes` 16, `complete` true |
| instructions | `rea instructions extracted/interceptor-arm.elf 0x10000 --format json --full-output` | function `entry`, body `0x10000`-`0x10135` (310 bytes) |
| function | `rea function extracted/interceptor-arm.elf 0x10136 --format json --full-output` | `FUN_00010136`, body `0x10136`-`0x10183` (78 bytes), with pseudo-code |

If `procedure_count` is 1, the environment variable was not in force.

### MCP route

The MCP server inherits its environment from Claude Code, so the variable must be exported before `claude` starts (as the repository's shell setup already recommends for `env.sh`). Verified state: the server running in this session was started without it, so its overview of the ARM-labelled ELF reports architecture `arm`, `language_id` `auto-from-header` and `procedure_count` 1, which is the failure signature. To use it:

1. Exit Claude Code, run `. /sandbox/.tools/env.sh; export REA_GHIDRA_LANGUAGE_ID=68000:BE:32:default`, start `claude` again.
2. `mcp__rea__open_binary` with `path=/sandbox/interceptor-wasm/extracted/interceptor-arm.elf`.
3. `mcp__rea__binary_overview`. The check that the setting took effect: the profile's `language_id` must read `68000:BE:32:default` (not `auto-from-header`) and `procedure_count` must be 636 with `string_count` 343. If not, close and restart as in step 1.
4. `mcp__rea__list_procedures`, `mcp__rea__list_strings`, `mcp__rea__read_bytes`, `mcp__rea__procedure_assembly`, `mcp__rea__procedure_pseudo_code` as needed.

Verified 2026-10-13 after a Claude Code restart from a shell that had sourced `env.sh`: `mcp__rea__open_binary` on the ARM-labelled ELF followed by `mcp__rea__binary_overview` reported `language_id` `68000:BE:32:default`, `loader` `auto-from-header`, segment 0x10000-0x58fec (298988 bytes), `procedure_count` 636 and `string_count` 343, identical to the command-line route. The MCP route is therefore the primary one for RE slices; the command line remains the fallback.

## Caveats found

- Every command-line and MCP close ends with `cleanup_incomplete`: rea cannot signal its Ghidra process group in this sandbox (`kill EPERM`). The analysis completes, the inventories come back inside `data.details.partial_observation`, and no Ghidra process is left running; delete the leftover `/tmp/rea-ghidra-*` directories.
- rea's Ghidra project is ephemeral (imported read-only, discarded on close). There is no persistent `.gpr`; annotations are session-only, so findings must be recorded in `docs/re/` notes.
- rea lists only Ghidra-defined strings. The hunk symbol names (`_OpenLibrary` and the rest) are not in the flat image, but `relocate.py` now writes the 104 link-time symbols into `interceptor.map.json` as `"symbols": [{"name", "addr", "hunk"}, ...]` (absolute addresses, sorted). At the start of a session apply the symbols with one `mcp__rea__set_addresses_names` call. Its argument is `{"names": {"0x<hex address>": "<name>", ...}}` (a dict from address string to name, plus an optional `document`), built from the map's `symbols` list: for `{"name": "MakePort", "addr": 103920, "hunk": 0}` and `{"name": "UnmakePort", "addr": 104004, "hunk": 0}` the call is `{"names": {"0x195f0": "MakePort", "0x19644": "UnmakePort"}}`. Names must be unique, and in the real map they are not: 13 names (the assembler local labels `.L1` to `.L14`) occur 34 times among the 104 entries, and 98 distinct addresses carry the 104 entries. Skip entries whose name starts with `.L`; the remaining 66 have unique names and unique addresses. `wrap_elf.py --machine` now defaults to `arm`, and `p_align` is 4.

## Result (Ghidra 12.1.4, rea 6.4.0)

636 procedures (133 in hunk 0, the first at `0x10000` named `entry`), 343 defined strings. 77 of the 185 hunks contain at least one detected procedure; hunks 41 to 74 contain none (see `docs/re/00-binary-layout.md`).

Note (2026-10-13): the relocation fix changed `interceptor.bin` and so the wrapped ELF files. Regenerate them with `wrap_elf.py` and re-open the ELF in rea; any session opened on the older file must be closed and reopened.
