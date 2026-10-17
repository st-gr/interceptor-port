# Binary layout of F/A-18 Interceptor

Scope: where things are in the executable. Addresses are in the relocated flat image. All observations come from a Ghidra 12.1.4 pass over that image (see `tools/hunk/ghidra_import.md`), from the hunk table, and from simple byte statistics per hunk. No game content is reproduced here.

## How the image was produced

The game binary on the disk is an AmigaDOS hunk executable of 185 hunks (331232 bytes on disk) for the Motorola 68000. `tools/hunk/relocate.py` lays the hunks out end to end from base address `0x10000`, applies the 32-bit relocations (all 8441 entries; the original amitools-based version lost runs when one block listed a target hunk more than once, so images made before 2026-10-13 have unrelocated operands, fixed on 2026-10-13 by parsing the hunk file directly in `relocate.py`), and writes `extracted/interceptor.bin` (298988 bytes of image, ending at `0x58FEC`) and `extracted/interceptor.map.json`. Command:

    python tools/hunk/relocate.py "extracted/disk/F-18 Interceptor" extracted/interceptor.bin extracted/interceptor.map.json --base 0x10000

Image offset N corresponds to address `0x10000 + N`. BSS hunks occupy address space in the map and appear in the image as zero fill; hunks 71 to 73 are CODE-typed but entirely zero (see below). The hunk symbol tables are not carried into the image, but `interceptor.map.json` now has a `symbols` list (104 link-time symbols, each `{name, addr, hunk}` with an absolute address, sorted by address). At the start of a session apply the symbols with one `mcp__rea__set_addresses_names` call. Its argument is `{"names": {"0x<hex address>": "<name>", ...}}` (a dict from address string to name, plus an optional `document`), built from the map's `symbols` list: for `{"name": "MakePort", "addr": 103920, "hunk": 0}` and `{"name": "UnmakePort", "addr": 104004, "hunk": 0}` the call is `{"names": {"0x195f0": "MakePort", "0x19644": "UnmakePort"}}`. Names must be unique, and in the real map they are not: 13 names (the assembler local labels `.L1` to `.L14`) occur 34 times among the 104 entries, and 98 distinct addresses carry the 104 entries. Skip entries whose name starts with `.L`; the remaining 66 have unique names and unique addresses. The entry point is hunk 0 at `0x10000`. Ghidra's detected functions all use these addresses.

## Hunk summary

| type | hunks | total bytes |
|------|------:|------------:|
| CODE | 126 | 285976 |
| DATA | 32 | 1500 |
| BSS | 27 | 11512 |

Many hunks are zero length or only 4 bytes (the link-time library glue and C data segments); the large ones are listed in the regions section. The complete table follows.

| hunk | type | address | size |
|-----:|------|--------:|-----:|
| 0 | CODE | 0x010000 | 41804 |
| 1 | DATA | 0x01A34C | 876 |
| 2 | BSS | 0x01A6B8 | 10756 |
| 3 | CODE | 0x01D0BC | 148 |
| 4 | CODE | 0x01D150 | 2036 |
| 5 | CODE | 0x01D944 | 5800 |
| 6 | CODE | 0x01EFEC | 472 |
| 7 | CODE | 0x01F1C4 | 20 |
| 8 | CODE | 0x01F1D8 | 6420 |
| 9 | CODE | 0x020AEC | 4420 |
| 10 | CODE | 0x021C30 | 4008 |
| 11 | CODE | 0x022BD8 | 288 |
| 12 | CODE | 0x022CF8 | 1808 |
| 13 | CODE | 0x023408 | 4972 |
| 14 | CODE | 0x024774 | 4060 |
| 15 | CODE | 0x025750 | 1916 |
| 16 | CODE | 0x025ECC | 3116 |
| 17 | CODE | 0x026AF8 | 6656 |
| 18 | CODE | 0x0284F8 | 1628 |
| 19 | CODE | 0x028B54 | 1824 |
| 20 | CODE | 0x029274 | 3248 |
| 21 | CODE | 0x029F24 | 640 |
| 22 | CODE | 0x02A1A4 | 2932 |
| 23 | CODE | 0x02AD18 | 2244 |
| 24 | CODE | 0x02B5DC | 2832 |
| 25 | CODE | 0x02C0EC | 3400 |
| 26 | CODE | 0x02CE34 | 3764 |
| 27 | CODE | 0x02DCE8 | 2328 |
| 28 | CODE | 0x02E600 | 5300 |
| 29 | CODE | 0x02FAB4 | 1840 |
| 30 | CODE | 0x0301E4 | 4512 |
| 31 | CODE | 0x031384 | 1884 |
| 32 | CODE | 0x031AE0 | 4936 |
| 33 | CODE | 0x032E28 | 1060 |
| 34 | CODE | 0x03324C | 1296 |
| 35 | CODE | 0x03375C | 2068 |
| 36 | CODE | 0x033F70 | 4644 |
| 37 | CODE | 0x035194 | 2656 |
| 38 | CODE | 0x035BF4 | 2000 |
| 39 | CODE | 0x0363C4 | 6480 |
| 40 | CODE | 0x037D14 | 6124 |
| 41 | CODE | 0x039500 | 2832 |
| 42 | CODE | 0x03A010 | 3224 |
| 43 | CODE | 0x03ACA8 | 2068 |
| 44 | CODE | 0x03B4BC | 2024 |
| 45 | CODE | 0x03BCA4 | 724 |
| 46 | CODE | 0x03BF78 | 1120 |
| 47 | CODE | 0x03C3D8 | 772 |
| 48 | CODE | 0x03C6DC | 608 |
| 49 | CODE | 0x03C93C | 1512 |
| 50 | CODE | 0x03CF24 | 1124 |
| 51 | CODE | 0x03D388 | 3440 |
| 52 | CODE | 0x03E0F8 | 3284 |
| 53 | CODE | 0x03EDCC | 2568 |
| 54 | CODE | 0x03F7D4 | 2328 |
| 55 | CODE | 0x0400EC | 2012 |
| 56 | CODE | 0x0408C8 | 1736 |
| 57 | CODE | 0x040F90 | 1704 |
| 58 | CODE | 0x041638 | 2488 |
| 59 | CODE | 0x041FF0 | 1512 |
| 60 | CODE | 0x0425D8 | 248 |
| 61 | CODE | 0x0426D0 | 872 |
| 62 | CODE | 0x042A38 | 456 |
| 63 | CODE | 0x042C00 | 4596 |
| 64 | CODE | 0x043DF4 | 9256 |
| 65 | CODE | 0x04621C | 4440 |
| 66 | CODE | 0x047374 | 1848 |
| 67 | CODE | 0x047AAC | 720 |
| 68 | CODE | 0x047D7C | 6224 |
| 69 | CODE | 0x0495CC | 888 |
| 70 | CODE | 0x049944 | 3492 |
| 71 | CODE | 0x04A6E8 | 11604 |
| 72 | CODE | 0x04D43C | 25564 |
| 73 | CODE | 0x053818 | 5800 |
| 74 | CODE | 0x054EC0 | 260 |
| 75 | CODE | 0x054FC4 | 404 |
| 76 | CODE | 0x055158 | 420 |
| 77 | CODE | 0x0552FC | 344 |
| 78 | DATA | 0x055454 | 40 |
| 79 | BSS | 0x05547C | 4 |
| 80 | CODE | 0x055480 | 1180 |
| 81 | DATA | 0x05591C | 164 |
| 82 | BSS | 0x0559C0 | 0 |
| 83 | CODE | 0x0559C0 | 732 |
| 84 | DATA | 0x055C9C | 4 |
| 85 | BSS | 0x055CA0 | 0 |
| 86 | CODE | 0x055CA0 | 344 |
| 87 | CODE | 0x055DF8 | 0 |
| 88 | DATA | 0x055DF8 | 4 |
| 89 | BSS | 0x055DFC | 0 |
| 90 | CODE | 0x055DFC | 0 |
| 91 | DATA | 0x055DFC | 260 |
| 92 | BSS | 0x055F00 | 0 |
| 93 | CODE | 0x055F00 | 0 |
| 94 | DATA | 0x055F00 | 4 |
| 95 | BSS | 0x055F04 | 0 |
| 96 | CODE | 0x055F04 | 104 |
| 97 | DATA | 0x055F6C | 0 |
| 98 | BSS | 0x055F6C | 0 |
| 99 | CODE | 0x055F6C | 160 |
| 100 | DATA | 0x05600C | 4 |
| 101 | BSS | 0x056010 | 0 |
| 102 | CODE | 0x056010 | 0 |
| 103 | DATA | 0x056010 | 4 |
| 104 | BSS | 0x056014 | 0 |
| 105 | CODE | 0x056014 | 712 |
| 106 | DATA | 0x0562DC | 0 |
| 107 | BSS | 0x0562DC | 0 |
| 108 | CODE | 0x0562DC | 332 |
| 109 | DATA | 0x056428 | 0 |
| 110 | BSS | 0x056428 | 0 |
| 111 | CODE | 0x056428 | 124 |
| 112 | DATA | 0x0564A4 | 0 |
| 113 | BSS | 0x0564A4 | 0 |
| 114 | CODE | 0x0564A4 | 0 |
| 115 | DATA | 0x0564A4 | 4 |
| 116 | BSS | 0x0564A8 | 0 |
| 117 | CODE | 0x0564A8 | 1544 |
| 118 | DATA | 0x056AB0 | 16 |
| 119 | BSS | 0x056AC0 | 0 |
| 120 | CODE | 0x056AC0 | 1008 |
| 121 | DATA | 0x056EB0 | 0 |
| 122 | BSS | 0x056EB0 | 0 |
| 123 | CODE | 0x056EB0 | 364 |
| 124 | DATA | 0x05701C | 0 |
| 125 | BSS | 0x05701C | 12 |
| 126 | CODE | 0x057028 | 644 |
| 127 | DATA | 0x0572AC | 0 |
| 128 | BSS | 0x0572AC | 0 |
| 129 | CODE | 0x0572AC | 136 |
| 130 | DATA | 0x057334 | 0 |
| 131 | BSS | 0x057334 | 0 |
| 132 | CODE | 0x057334 | 36 |
| 133 | DATA | 0x057358 | 0 |
| 134 | BSS | 0x057358 | 0 |
| 135 | CODE | 0x057358 | 0 |
| 136 | DATA | 0x057358 | 4 |
| 137 | BSS | 0x05735C | 120 |
| 138 | CODE | 0x0573D4 | 104 |
| 139 | DATA | 0x05743C | 0 |
| 140 | BSS | 0x05743C | 0 |
| 141 | CODE | 0x05743C | 0 |
| 142 | DATA | 0x05743C | 0 |
| 143 | BSS | 0x05743C | 480 |
| 144 | CODE | 0x05761C | 2172 |
| 145 | DATA | 0x057E98 | 20 |
| 146 | BSS | 0x057EAC | 0 |
| 147 | CODE | 0x057EAC | 0 |
| 148 | DATA | 0x057EAC | 16 |
| 149 | BSS | 0x057EBC | 0 |
| 150 | CODE | 0x057EBC | 40 |
| 151 | CODE | 0x057EE4 | 192 |
| 152 | CODE | 0x057FA4 | 68 |
| 153 | CODE | 0x057FE8 | 160 |
| 154 | CODE | 0x058088 | 68 |
| 155 | CODE | 0x0580CC | 16 |
| 156 | DATA | 0x0580DC | 0 |
| 157 | BSS | 0x0580DC | 0 |
| 158 | CODE | 0x0580DC | 168 |
| 159 | CODE | 0x058184 | 216 |
| 160 | CODE | 0x05825C | 20 |
| 161 | CODE | 0x058270 | 28 |
| 162 | CODE | 0x05828C | 20 |
| 163 | CODE | 0x0582A0 | 56 |
| 164 | CODE | 0x0582D8 | 24 |
| 165 | CODE | 0x0582F0 | 24 |
| 166 | CODE | 0x058308 | 916 |
| 167 | CODE | 0x05869C | 620 |
| 168 | DATA | 0x058908 | 80 |
| 169 | BSS | 0x058958 | 140 |
| 170 | CODE | 0x0589E4 | 0 |
| 171 | CODE | 0x0589E4 | 332 |
| 172 | CODE | 0x058B30 | 440 |
| 173 | CODE | 0x058CE8 | 0 |
| 174 | CODE | 0x058CE8 | 20 |
| 175 | CODE | 0x058CFC | 240 |
| 176 | DATA | 0x058DEC | 0 |
| 177 | CODE | 0x058DEC | 44 |
| 178 | DATA | 0x058E18 | 0 |
| 179 | CODE | 0x058E18 | 132 |
| 180 | DATA | 0x058E9C | 0 |
| 181 | CODE | 0x058E9C | 320 |
| 182 | DATA | 0x058FDC | 0 |
| 183 | DATA | 0x058FDC | 0 |
| 184 | CODE | 0x058FDC | 16 |

## Regions

Classification is by instruction style. Compiled C code shows `link`/`unlk` frames around each function; hand-written assembly shows frameless, register-heavy routines. Ghidra detected 636 functions in total.

| address range | hunks | what it is |
|---|---|---|
| `0x10000`-`0x1A34B` | 0 | C-compiled start-up and main program (121 `link` frames for 133 functions); the entry point is `0x10000`, and `MakePort`/`UnmakePort` are named in its symbols. IFF tags (FORM, LIST) appear as defined strings near `0x10EAE`, so the IFF loading code sits here. |
| `0x1A34C`-`0x1A6B7` | 1 | Initialised data: library, device and file-name strings (see below). |
| `0x1A6B8`-`0x1D0BB` | 2 | BSS, 10756 bytes of C global variables. |
| `0x1D0BC`-`0x1D14F` | 3 | Single routine named `_UnPackRow` (a row unpacking helper), with its internal labels. |
| `0x1D150`-`0x394FF` | 4-40 | Mostly hand-written assembly: 388 functions with only a handful of frames. Frameless, register-heavy hunks are 6, 7, 8, 11, 13, 14, 16-18, 26, 32 and 36-39; hunks 9, 10, 12, 19, 20, 23, 24, 31, 35 and 40 mix in framed (C-compiled) routines. This is the bulk of the simulation and rendering code. |
| `0x39500`-`0x41FEF` | 41-58 | CODE-typed hunks with no detected functions and almost no branch, `rts` or `jsr` patterns; roughly a third zero bytes. Almost certainly data (graphics, tables). Hunks 45-50 (`0x3BCA4`-`0x3D387`) show a little more instruction-like content and are the least certain. |
| `0x41FF0`-`0x425D7` | 59 | Cockpit warning and status message table (54 entries of 28 bytes). |
| `0x425D8`-`0x43DF3` | 60-63 | Unclassified data. |
| `0x43DF4`-`0x4621B` | 64 | Menu, briefing and mission text tables (110 ids, 91 records, 337 lines, note 08 section 2); the credits and a press-a-key prompt end the hunk near `0x46015`-`0x46127`. |
| `0x4621C`-`0x4A6E7` | 65-70 | Unclassified data, no detected code. |
| `0x4A6E8`-`0x54EBF` | 71-73 | 42968 bytes of zeros inside CODE-typed hunks; zero-filled working buffers (CHIP or FAST memory reserved by the loader). |
| `0x54EC0`-`0x55453` | 74-77 | Small frameless assembly helpers (hunks 75-77: 19 functions), apparently low-level utility routines. |
| `0x55454`-`0x55F03` | 78-95 | Start of the C runtime: the `audio.device` name string (hunk 78), 1180-byte framed runtime code (hunk 80), and small data cells. |
| `0x55F04`-`0x57E97` | 96-144 | C runtime library code; framed routines (hunks 105, 117, 120, 123, 126 have dense `link`/`unlk` pairs), with hunk 144 (2172 bytes) the largest. |
| `0x57E98`-`0x58907` | 145-167 | Hex-digit table, runtime data and tiny frameless routines (16-916 bytes each) such as arithmetic helpers. |
| `0x58908`-`0x589E3` | 168-170 | Console-window (`con:`) specification and a runtime error message used by the C start-up (hunk 168), followed by 140 bytes of BSS (hunk 169); hunk 170 is empty. |
| `0x589E4`-`0x58FEC` | 171-184 | OS library stub region (the `_Name` stubs): the end of the image. See below. |

### OS stub region

The stub names are link-time symbols read from the executable's symbol tables (they are absent from the flat image). They sit in hunks 171-184:

| hunk | address | stubs |
|-----:|---------|-------|
| 171 | `0x589E4` | dos: `_Open`, `_Close`, `_Read`, `_Write`, `_Input`, `_Output`, `_Seek`, `_DeleteFile`, `_Rename`, `_Lock`, `_UnLock`, `_Examine`, `_Info`, `_IoErr` |
| 172 | `0x58B30` | exec: `_AddIntServer`, `_RemIntServer`, `_AllocMem`, `_FreeMem`, `_AvailMem`, `_FindTask`, `_SetTaskPri`, `_SetSignal`, `_AllocSignal`, `_FreeSignal`, `_AddPort`, `_RemPort`, `_GetMsg`, `_WaitPort`, `_CloseLibrary`, `_OpenDevice`, `_CloseDevice`, `_DoIO`, `_SendIO`, `_OpenLibrary` |
| 174 | `0x58CE8` | `_NewList` |
| 175 | `0x58CFC` | `_CreatePort`, `_DeletePort` |
| 177 | `0x58DEC` | `_CreateStdIO`, `_DeleteStdIO` |
| 179 | `0x58E18` | `_CreateExtIO`, `_DeleteExtIO` |
| 181 | `0x58E9C` | graphics: `_LoadRGB4`, `_InitRastPort`, `_InitVPort`, `_MrgCop`, `_MakeVPort`, `_LoadView`, `_WaitBlit`, `_InitView`, `_InitBitMap`, `_WaitBOVP`, `_FreeSprite`, `_OwnBlitter`, `_DisownBlitter`, `_FreeRaster`, `_GetColorMap` |
| 184 | `0x58FDC` | `_CloseWorkBench` |

They are the stock register-loading stubs that move C arguments into registers and jump through the library vector table (they contain no frames, and Ghidra detects a handful of short functions in each). `_UnPackRow` and the other C-runtime helpers are separate (hunk 3 and the runtime above).

## String tables

| address | hunk | contents |
|---------|-----:|----------|
| `0x1A39C`-`0x1A6A5` | 1 | Library and device names, and file names of the `config` and `text/` data files read from the boot disk (about 20 entries). |
| `0x36922`-`0x371D7` | 39 | Mostly the text-drawer and message code of hunk 39; the only text is the fire prompts at `0x36923` and `0x36929` and, near `0x3717C`, the target data words and aircraft tags (`06-cockpit-hud.md`). |
| `0x37CA2` | 40 | A keyboard-row string used for key decoding. |
| `0x41FF2`-`0x425BD` | 59 | Cockpit warning, alert and training-manoeuvre messages (54 entries of 28 bytes, 26 characters each). |
| `0x43DF4`-`0x46127` | 64 | Menu items, mission briefings, mission outcome text, free-flight selection and credits (110 ids, 91 records, 337 lines; the 5 by 7 font that draws them is in hunk 61, note 08). |
| `0x55466`, `0x5546E` | 78 | A short identifier and the `audio.device` name. |
| `0x57E98` | 145 | Hexadecimal digit table. |
| `0x58908`-`0x58930` | 168 | Console specification and a runtime error message. |

## Libraries and devices opened

| name | where the name appears |
|------|------------------------|
| exec.library | not a string; reached through the system base at address 4 |
| dos.library | `0x1A39C` |
| graphics.library | `0x1A3A8` and `0x1A410` |
| intuition.library | near `0x1A3E3` |
| input.device | `0x1A400` |
| keyboard.device | near `0x1A573` |
| timer.device | near `0x1A598` |
| gameport.device | `0x1A5AE` |
| audio.device | `0x5546E` |

## Open questions

1. Hunks 41-58, 60-63 and 65-70 (about 59 KB, excluding the zero-filled hunks 71-73) have no detected functions; which parts are graphics, sound samples, lookup tables or undisassembled code (hunks 45-50 especially)?
2. Hunks 71-73 are zero-filled CODE-typed hunks of 42968 bytes: confirm they are runtime buffers and find which code fills them.
3. The ELF-label trick used for the Ghidra import means addresses are right but there is no persistent project; decide whether to build a real Ghidra database later.
4. Which hunks are called through function-pointer tables? Ghidra found 636 functions, but jump-table and indirect call targets inside hunks 4-40 may be missed.
5. Where does the main loop live in hunk 0 versus the assembly hunks, and what is the per-frame call order?
6. Which library vectors are called directly (without stubs) from assembly, and which C functions call the stub region?
7. Are hunks 150-167 (tiny frameless routines) the C compiler's math and block-copy helpers?
