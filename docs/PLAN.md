# Ragexe recompilation: project plan

Goal: a portable, maintainable C++ reimplementation of the iRO Episode 3 client
(`Ragexe.exe`, 2003), built on modern libraries, matching the original's behavior,
for preservation and study.

## 1. What we are working with (triage, done)

| Item | Finding |
|---|---|
| Source | `iRO_ver4.0.zip` → InstallShield/MSI → `Data1.cab` (MS-CAB, 507 files) |
| Target | `Ragexe.exe`, 2,252,892 bytes, SHA-256 `990892d1…6828bc` |
| Build | PE32 i386, linked 2003-09-15, MSVC 6.0 (SP5-era rich header), static CRT |
| PDB path | `C:\mywork\ragnarok\client\Ragnarok___Win32_Release_Episode_3\Ragexe_ep3.pdb` (PDB itself not shipped) |
| Packing | **None.** Standard `.text/.rdata/.data/.rsrc`, code entropy 6.6, full import table, 92-byte overlay is only the CodeView NB10 record |
| Protection | None found: no GameGuard, nProtect, HackShield or anti-debug imports |
| RTTI | **Intact: 293 types**, including ~170 `UI*` windows and the core engine classes (`CGameMode`, `CRagConnection`, `CGndRes`, `CActRes`, `C3dGround`, `CGameActor`, …). See `analysis/rtti_classes.txt` |
| Debug strings | `PACKET_ZC_*` names, assert and log text, so naming is evidence-based |
| Size | 1.9 MB of code; Ghidra finds **10,138 functions** (1.85 MB) after two analysis passes, plus 329 imported functions, of which an estimated ~20% is CRT/STL |
| Ghidra coverage | FID matches only 52 functions (VC6 runtime is not in Ghidra's databases). The built-in RTTI analyzer makes 298 class namespaces but applies just 1 vtable, although the binary holds 295 complete object locators → 295 vtables for 292 classes. The M1 RTTI script fills this in |

Step 1 of the brief (unpacking) is not needed. Code written at run time was ruled out
statically in M1 (`tools/runtime_code_check.py`), with one exception that is part of the
protocol: the client patches one small function of its own and runs x86 code that the
server can send with packet 0x01C7 (`docs/protocol/code-modules.md`).

### Dependencies to replace

| Original | Role | Replacement |
|---|---|---|
| DDRAW / Direct3D 7 (via `DirectDrawCreateEx`) | rendering, 2D surfaces | SDL3 + SDL_GPU (Metal / Vulkan / D3D12), fixed-function state emulated in shaders |
| GDI32 (`CreateFontA`, `TextOutA` into DIBs) | text | FreeType glyph atlas + a bundled open font |
| DINPUT, USER32 messages, IMM32 | input, IME | SDL3 events and SDL3 text input/IME |
| DSOUND + `mss32.dll` + `Mp3dec.asi` (Miles) | 2D/3D sound, MP3 BGM, ADPCM | miniaudio (mixing, spatial audio, MP3/WAV decoding) |
| `ijl15.dll` (Intel JPEG) | JPEG textures | stb_image |
| `cps.dll` (`compress`/`uncompress`) | GRF/packet compression | zlib, once RE confirms `cps.dll` is plain zlib |
| `granny2.dll` (RAD Granny) | 7 `.gr2` models: guardians, emperium, guild flags | our own `.gr2` reader and skinning, from RE of `granny2.dll` (also in the installer); a placeholder until then |
| `binkw32.dll` | `openning.bik` intro | optional FFmpeg backend, otherwise skipped |
| WS2_32 (`WSAAsyncSelect`) | networking | non-blocking sockets polled per frame (thin POSIX/Winsock shim) |
| ADVAPI32 registry | settings | config file in the user's data directory |
| SHELL32 `ShellExecuteA` | open URLs | `SDL_OpenURL` |
| `dbghelp.dll` | crash dumps | platform crash handler, or dropped |
| CP949 text (Korean GRF paths) | encoding | CP949↔UTF-8 table generated from the Unicode Consortium mapping file |

## 2. Clean-room rules

1. Inputs are only the files in `original/` (the exe, its DLLs, and the data).
   No looking at other RO clients, servers, emulators, format wikis or tools.
2. **Every structure, constant, packet ID and format field cites the address it came from**
   (for example `CGndRes::Load @ 0x004xxxxx`). Ghidra is the source of truth.
   This matters because the assistant's general training includes public RO projects.
   A claim with no address is not accepted, so outside knowledge cannot slip in.
3. Spec first: findings go to `docs/formats/*.md` and `docs/protocol/*.md` with
   addresses. Code in `src/` is written from those specs, with the decompilation as a check.
4. Names come from the binary: RTTI, strings, imports, and the documented interfaces
   of what it calls or implements (Win32, DirectX, the C runtime, the C++ standard
   library's public members). Anything else is a descriptive name we make up, never one
   remembered from elsewhere: not from other RO projects, and not a library's internal
   names. `docs/naming.md` has the conventions and the fixed names for library internals.
5. `original/`, data files, and raw decompiler output stay out of version control.
   The rebuilt source is still derived from Gravity's copyrighted work, so the repository stays private.

## 3. Repository layout

```
original/     gitignored: installer, extracted client, SHA-256 manifest
analysis/     Ghidra project (gitignored), exported reports, function ledger
tools/        pe_survey.py, Ghidra scripts, ledger tooling, emulation oracle, stub server
docs/         this plan, formats/, protocol/, architecture notes
src/          the new client: platform/ core/ res/ render/ audio/ net/ ui/ world/ game/
tests/        unit, emulation-oracle, spec-conformance and screenshot-regression tests
```

## 4. Method

### 4.1 Function ledger
`analysis/ledger.tsv` has one row per function: entry, size, kind, library flag,
subsystem, reach, status, namespace, name and notes. Status runs
`raw → named → typed → specced → ported → verified`.
It is a sorted text file, so every status change is a readable diff in git.
`tools/ledger.py` has three commands:
- `sync` refreshes names and sizes from the Ghidra export and keeps everything set by hand.
- `set` changes rows by address or address range.
- `report` prints progress.

The subsystem of library rows comes from `ledger.py classify` (`crt`, `stl`); that of
game code from `tools/assign_subsystems.py` and the rules in `analysis/subsystems.tsv`
(`docs/architecture.md` §1).

Kind is one of:
- `code`: a real function.
- `funclet`: an EH `Unwind@`/`Catch@` fragment, ported as part of its parent function.
- `thunk`: an import stub.

Reach, written by `tools/find_unreferenced.py`, is empty for code the program can
reach. Otherwise it is `unreferenced` (nothing refers to the function) or `dead`
(only unreferenced or dead code refers to it). Such code is never ported and does not
count toward progress or the naming figure.

Only reachable non-library `code` counts toward progress, which is reported as
**% of those bytes verified**, not file count.

### 4.2 From decompilation to C++
For each class:
1. In Ghidra, fix the class struct, vtable, and the signatures of its methods.
2. Write or update the spec in `docs/`.
3. Write idiomatic C++20: RAII, `std::` containers in place of VC6 Dinkumware ones,
   explicit (de)serializers in place of `#pragma pack` struct casts, `std::span`
   in place of pointer+length, and no global singletons where a reference can be passed.
4. Tag each ported function with its origin, `// Ragexe: 0x004xxxxx`, so the ledger can be built from the source.
5. Keep observable quirks deliberately in a `compat::` namespace: the MSVC `rand()` LCG,
   `__ftol` truncation, case-insensitive backslash paths, integer wraparound.
6. Verify (section 4.3) and mark the function `verified`.

### 4.3 Verification oracles
The original is never run as a program, not under Wine and not in a VM. Verification
is static, plus emulation of single functions.
- **Emulation oracle.** `tools/emu/` maps `Ragexe.exe` into a Unicorn x86 emulator and
  calls one of the original's functions by address, with its inputs set up in emulated
  memory. Imports are stubbed: heap and `operator new` go to a bump allocator, file I/O
  reads from `original/install/`, and the D3D/DirectDraw COM interfaces are fakes that
  record the calls made to them. The port must give the same result on the same input.
  This works best for self-contained code: GRF table and file decoding, resource
  parsers, path finding, packet builders, text layout, and the CPU-side geometry and
  render-state setup for a draw.
- **Spec conformance.** Packets the port sends are checked against the length table and
  layouts recovered from the binary. The stub server is checked against the
  client's own parsers.
- **Draw-call comparison.** Where the emulator can run a scene-building function
  (ground, model, sprite), the recorded D3D7 calls (vertices, texture, blend and alpha
  state) are compared with what the port submits to `RenderDevice`. Final pixels are
  checked by review and then pinned as our own regression baselines.
- **Fuzzing.** libFuzzer/ASan on every resource parser. The 2003 code trusted its input.

Without a live run of the original, three things cannot be checked: frame timing as a
whole, the interplay between subsystems, and final pixel output. These are verified by
reading the code, not by measurement.

### 4.4 Recovering binary structure
- Library code: Ghidra FID first. Where it misses VC6 coverage, label the CRT/STL by
  hand (a bounded set) so it drops out of the port queue.
- RTTI script: walk CompleteObjectLocator → TypeDescriptor → ClassHierarchyDescriptor
  to get classes, bases with offsets, and vtables. Generate Ghidra class structs and
  vtable structs for all 293 types.
- Object layouts: size from the `operator new(n)` call before each constructor; fields
  from constructor and destructor writes and their accessors; Ghidra struct types for
  the VC6 containers (`std::list`, `std::vector`, `std::map`, and the reference-counted
  copy-on-write VC6 `std::string`).
- Calling conventions: `__thiscall` (`ecx`) for methods, `__cdecl` by default,
  `__stdcall` for imports and RAD libraries. VC6 has no link-time code generation, so
  no custom conventions are expected. SEH/C++ EH frames are marked so Ghidra
  separates the unwind funclets.
- Globals: find the manager singletons from their constructor calls in the CRT
  initializer table (`_initterm`) and give them descriptive names.
- Packet dispatch: find the packet-length table and the `switch` in the game and login
  modes. `PACKET_ZC_*` strings name the handlers.

The labels are regenerated in this order, repeated until the label files stop changing
(two `--reanalyze` passes after a change to a rule or a curated file):
1. `tools/ghidra/analyze.sh original/client/Ragexe.exe --reanalyze`: applies RTTI and
   every label file, then exports the inventories.
2. `tools/ledger.py sync`, then `tools/ledger.py classify`, then
   `tools/find_unreferenced.py` (the reach column).
3. `tools/recover_objects.py`: constructors, destructors, sizes, global objects.
4. `tools/recover_packets.py` (run from `tools/`): the packet handlers.
5. `tools/recover_layouts.py`: fields, non-virtual methods, global trees and pointers.
6. `tools/assign_subsystems.py`: the ledger's subsystem column.
7. `tools/class_graph.py`: the class graph (`analysis/exports/Ragexe.exe.class_graph.{tsv,dot}`),
   joined from the steps above; it changes no label file.

## 5. Milestones

Effort is for one engineer full time, with AI assistance. Treat it as rough.

### M0 — Workspace and tooling (1–2 weeks)
- `git init`, `.gitignore` for `original/` and the Ghidra project, SHA-256 manifest.
- Install JDK 21 (Ghidra 12.1.4 is installed but has no Java runtime). Run headless
  auto-analysis and keep the scripts in `tools/ghidra/`.
- Read the MSI `File`/`Directory` tables to get the real file names and install layout
  back. The cab stores files under MSI keys such as `_003_10_30gdata_yuno_new_1.g`.
- **Exit:** analyzed Ghidra project; install tree and manifest; the ledger exists.

### M1 — Static map of the binary (4–6 weeks)
- Static runtime-code check from section 1.
- Emulation oracle harness (`tools/emu/`): load the PE, stub the imports, call a function
  by address, read back the results. First target: a small pure function such as a
  string or path helper, to prove the harness works.
- Library code identified; RTTI classes, hierarchy and vtables generated; constructors,
  destructors and object sizes found for all 293 classes.
- Every function assigned to a subsystem: core, resources, render, world, net, UI,
  audio, game.
- `docs/architecture.md`: the main loop (`WinMain` → mode manager → `CMode` subclasses),
  the singletons, frame timing, threads.
- **Exit:** at least 90% of functions named or library-tagged; class graph exported;
  ledger populated.

### M2 — Portable skeleton (2–3 weeks)
- CMake + C++20, with the libraries (SDL3, miniaudio, FreeType, zlib, stb, Catch2) pinned
  in `cmake/dependencies.cmake` and fetched with `FetchContent`, each added by the
  milestone that first uses it.
- Platform layer: window, events, timing (`timeGetTime` → SDL ticks), files, config.
- Main loop and `CMode` state machine with stubbed modes.
- CI on macOS arm64, Linux x86-64 and Windows x64, with ASan/UBSan.
- **Exit:** an empty window runs the mode loop on all three platforms.

### M3 — Resource system (5–7 weeks): the first real port
- GRF archive reader: the `Master of Magic` header, version `0x103`, the table and
  per-file decoding as the binary implements them, and `cps.dll` (confirmed as or replaced by zlib).
- Lookup order between loose files and GRF, path normalization, and the
  `CRes` resource cache and manager.
- Loaders, in dependency order: `CBitmapRes` (BMP/TGA/JPG), `CPaletteRes`, `CSprRes`,
  `CActRes`, `C3dAttr`, `CGndRes`, `C3dWorldRes`, `C3dModelRes`, `CRmaRes`,
  `CImfRes`, `CEZeffectRes`, `CEmblemRes`, `CWave`, the text tables, `clientinfo.xml`.
- `ragres` command-line tool: list, extract and dump every asset type.
- **Exit:** every entry in `data.grf` and the `.gpf` patches parses; on a sample of
  each asset type, our loaders' output matches the original loaders run in the
  emulation oracle; parsers have run under the fuzzer.

### M4 — Renderer (8–10 weeks)
- Recover how the original drives D3D7 and DirectDraw (`C3dDevice`, `CTexture`,
  `CSurface`, `CDCSurface`, `CView`, the renderer): vertex formats, render states,
  blend and alpha-test modes, colorkey, fog, lightmaps, and how it sorts draws.
- `RenderDevice` interface on SDL_GPU, with shaders that reproduce that fixed-function state.
- Build order: 2D blits and UI surfaces → FreeType text → ground and lightmaps
  (`C3dGround`, `C3dGround15`) → RSM models (`C3dNode`) → water → billboard sprites →
  minimap, fog, lighting.
- `ragview` tool: a map and model viewer.
- **Exit:** for reference maps, the vertices and render states the port submits match the
  D3D7 calls recorded from the original's scene-building functions in the emulator;
  rendered frames pass review and become regression baselines.

### M5 — Actors and animation (6–8 weeks)
- The `CRenderObject → CGameActor → CPc/CNpc/CPlayer/CItem/CCorpse` hierarchy.
- ACT frame timing, eight directions relative to the camera, layering and anchors for
  body, head, weapon, shield and headgear, palette swaps.
- Camera control, picking (`CActorPickNode`), walkability from the attribute map, path finding.
- Granny sub-milestone: reverse `granny2.dll` only as far as these 7 models need
  (mesh, skeleton, one animation track) and write a CPU skinning path. Placeholders until then.
- **Exit:** offline sandbox: click-to-move on a map with every actor type animating;
  path finder output matches the emulation oracle.

### M6 — Network and protocol (6–8 weeks)
- `CConnection`/`CRagConnection`: framing, the packet-length table (generated into
  `packets.def` with addresses), fixed and variable-length packets, the
  login → char → zone handoffs in `CLoginMode`.
- Packet 0x01C7 and the code modules that follow it in the stream
  (`docs/protocol/code-modules.md`): parsed and stored, with an identity `PacketFilter`;
  modules, if a server sends any, run in the emulator.
- `tools/stubserver`: our own minimal server, written from what the client sends and
  parses: login, server list, char select and create, map enter, spawn, walk, chat,
  a few scripted NPCs and monsters.
- Port the packet handlers in `CGameMode` and `CLoginMode`.
- **Exit:** the port plays a scenario against the stub server (login → create character
  → enter map → walk → chat → NPC dialog → logout). Every packet it sends matches the
  recovered length table and layout, and byte-matches the original's packet builder
  for the same input in the emulation oracle.

### M7 — UI framework and windows (10–14 weeks; the largest milestone)
- `UIWindow` base, `UIWindowMgr`, focus, z-order, dragging, modal handling, and the
  bitmap skin lookup (`CSkinMgr`).
- Controls: buttons, edit controls with IME, list and combo boxes, scrollbars, tabs,
  gauges, balloon text.
- Windows in priority order: login, server select, char select and create → HUD
  (basic info, chat, shortcut bar, minimap) → items, equipment, skills, status,
  options → NPC dialogs, shops, trade, merchant shops → party, guild, friends, whisper
  → the rest (pet, mini-games, web browser stub, …).
- **Exit:** all ~170 `UI*` classes ported. Each one's layout constants and bitmap names
  are checked against the spec, and it has a screenshot regression test against a
  reviewed baseline.

### M8 — Audio and video (2–3 weeks)
- The sound manager on miniaudio: streamed BGM, 3D positional effects with the
  original's distance and volume rules, ADPCM WAV, option sliders.
- Optional Bink intro via FFmpeg.
- **Exit:** every sound trigger in the ported handlers maps to a call site in the original
  with the same file, volume and position rules.

### M9 — Effects and remaining gameplay (8–12 weeks)
- The effect system (`CRagEffect`, `CEffectPrim`, `CEffectMaker`, `CMsgEffect`,
  `CNumEffect`, arrow, blow and blur effects), skill visuals (`CSkill`, `CGraffiSkill`),
  emotions, weather.
- The `mg::` mini-game engine classes.
- Language variants (`CLanguage`, `CLanguageTC`, `CLanguageTH`).
- **Exit:** ledger shows 100% of non-library code ported and at least 90% verified.

### M10 — Hardening and preservation release (3–4 weeks)
- Audit 64-bit and endianness assumptions, fuzz everything, profile.
- Docs that map each `src/` file to its address ranges, plus format and protocol specs.
- Builds documented for the three platforms.

**Total: about 14–18 months.** M3 → M4 → M5 is the critical path. M6 and the UI
framework part of M7 can run in parallel with M4 and M5 once M2 is done.

## 6. Risks

| Risk | Mitigation |
|---|---|
| Outside knowledge breaks the clean room | Address-citation rule (section 2); specs reviewed before code |
| No server from 2003 to test against | The stub server comes only from client code; the original exe validates it |
| The server may send x86 code (packet 0x01C7) that filters every packet, which a portable client cannot run natively | Keep the stream format; identity filter by default; run received modules in the emulator. The stub server never sends them |
| The original is never run, so there is no ground truth for whole frames, timing or final pixels | Emulation oracle for single functions and recorded D3D calls; careful review of timing and frame-loop code; reviewed regression baselines |
| Emulation harness becomes expensive for functions with deep state | Limit it to self-contained functions; build state from parsed resources instead of emulating start-up |
| Granny and Bink are proprietary | Limited RE of the shipped DLL for the 7 models; the intro video is optional |
| D3D7 output is not bit-exact on modern GPUs | Compare draw calls, not pixels; reproduce blend and alpha rules exactly |
| Fidelity vs. readable code | Quirks isolated in `compat::`; everything else idiomatic |
| Legal | Private repo; never distribute `original/`, data files or decompiler output |

## 7. Status

### M0: done
- [x] JDK 21: Homebrew `openjdk@21` was already installed. `tools/ghidra/analyze.sh` sets `JAVA_HOME` to it.
- [x] Ghidra headless analysis: project in `analysis/ghidra/` (gitignored), about 1 minute per run.
  `ExportInventory.java` writes `analysis/exports/Ragexe.exe.{functions,classes}.tsv` and `.summary.txt`.
- [x] MSI layout: `tools/msi_layout.py` joins the File, Component and Directory tables and hard-links all
  507 files into `original/install/` under their real names (`data.grf`, 10 `.gpf`/`.rgz` patches, `BGM/`,
  `skin/default`, `skin/Scribbling Kid`). Sizes are checked against the MSI. SHA-256 manifest: `analysis/manifest.sha256`.
- [x] `git init` (branch `main`) with `.gitignore` for original files, game data, the Ghidra project and decompiler output.
- [x] Ledger seeded: `analysis/ledger.tsv`, 10,138 rows. The breakdown:
  - 6,486 `code` functions (1.81 MB), of which 54 are FID-recognized library code.
  - **6,432 functions (1,805,976 bytes) left to port.**
  - 3,603 EH funclets and 49 thunks, excluded from progress.
  - Every function is `raw` except `entry`. The FID matches cluster at `0x005b…–0x005c…`,
    which suggests the static CRT sits at the end of `.text`.
- Dropped: running the original under Wine or a VM. Verification is static plus the
  emulation oracle (section 4.3), and the runtime-code check moved to M1.

### M1: done
- [x] RTTI → Ghidra classes and vtables: `tools/ghidra/scripts/ApplyRtti.java`, run by `analyze.sh`
  after auto-analysis. It is safe to re-run; a second run produces identical exports.
  - Applies typed RTTI structures (object locator, hierarchy descriptor, base class array and
    descriptors) and labels in each class namespace.
  - 295 vtables with 7,157 slots, each typed as a `/Ragexe/vftables` struct with one
    function-definition pointer per slot. Every class struct gets a `vftable` field at its vfptr offset.
  - A vtable ends at the next locator pointer, a non-code value, or a reference whose
    instruction contains the address. Devirtualized `CALL [reg+n]` references point into
    the middle of vtables and are ignored. Check: no derived vtable is shorter than its
    primary base's, and no vtable stops in front of another code pointer.
  - 338 new functions created at slot targets Ghidra had missed (10,138 → 10,476).
  - 1,567 virtual functions moved into the class that introduces them (the most-base class
    whose vtable holds them) and named `vfNN`, or `vfNN_for_<Base>` for secondary vtables,
    with `__thiscall`.
  - Multiple inheritance: `mg::mgType1::CItemObj`, `CMonster` and `CPlayer` each have a second
    vtable at offset 116, 116 and 104 (the game-actor side), each with one adjustor thunk.
    The `std` stream classes use virtual inheritance.
  - The one function shared by unrelated classes, `0x005c195a` (18 slots in 7 classes), is
    `push 0x19; call __amsg_exit`, which is `_purecall`. It shows that `CDC` (slots 1–8) and
    `CRes` (slots 2–3) are abstract interfaces.
  - Exports: `analysis/exports/Ragexe.exe.{rtti,vtables,vtable_refs}.tsv`.
    `vtable_refs` lists 1,013 code references to vtables from 617 functions; these are the
    constructor/destructor candidates.
- [x] Curated labels: `tools/ghidra/scripts/ApplyLabels.java` (run by `analyze.sh` after `ApplyRtti`)
  applies `analysis/labels/*.tsv`. Every row carries the evidence for its name. The label files win
  over analysis names, and they are re-applied on every run.
  - `*.functions.tsv`: functions are created when missing.
  - `*.data.tsv`: data labels.
  - `*.classes.tsv`: class struct sizes. A struct is grown, never shrunk.
  - Class structs are looked up by their exact namespace category (`/mg/mgType1/CPlayer`).
    Ghidra's `findOrCreateClassStruct` matches by name only and had merged the global `CPlayer`
    into the mini-game one.
- [x] VC6 runtime located and labeled:
  - Layout:
    - Game code is `[0x00401000, 0x005b3060)`: 5,881 of its 5,883 functions start on a 16-byte
      boundary. It ends with the start-up module, `WinMain` at `0x005b2280`.
    - Linker import thunks follow, up to `0x005b325c`.
    - From there to the end of `.text` is the static C runtime plus Dinkumware STL
      (8% 16-byte aligned).
    - EH funclets (`Unwind@…`) for game functions are also placed at the end of `.text`. The
      ledger keeps them non-library.
  - `tools/binindex.py` is a capstone index of the binary (instructions, calls, import calls,
    data references) for analysis scripts.
  - Only **90 library functions are called from game code** (VC6 inlines `strcpy`, `memcpy` and
    similar). `analysis/labels/crt.functions.tsv` names them all from their code and call sites,
    plus the start-up chain: `WinMainCRTStartup` → `_heap_init`, `_mtinit`, `_ioinit`,
    `_setargv`, `_setenvp`, `_cinit` → `_initterm`, `WinMain`, `exit`.
  - The game uses: `sprintf`/`vsprintf`/`fprintf`/`sscanf`, `fopen`…`ftell`, `strtok`, `_itoa`,
    `atoi`/`atof`, `rand`/`srand`, `time`/`ctime`, `floor`/`acos`/`fmod`, `_spawnl` (`runzip.exe`,
    `findhack.exe`), `_beginthreadex`, `typeid` and `dynamic_cast`, `std::string`, `strstream`.
  - The game also uses the MSVC array constructor/destructor iterators.
  - Ledger: `[0x005b325c, end)` and the 41 `std::` template functions in the game region are
    `library` (`crt`/`stl`), 985 functions in all.
- [x] Constructors, destructors, object sizes and globals: `tools/recover_objects.py` writes
  `analysis/labels/{objects.functions,objects.classes,initializers.functions,globals.data}.tsv`
  and a per-class report, `analysis/exports/Ragexe.exe.objects.tsv`.
  - Deleting destructors are vtable slots that test bit 0 or 1 of the flags argument and call
    `operator delete` (287 of them). The call before the test is the destructor.
  - The C++ initializer table `[0x00605000, 0x00605340)` has 207 entries, which Ghidra had not
    made into functions. They construct 47 global objects. 24 of those have RTTI classes:
    `g_CRagConnection`, `g_UIWindowMgr`, `g_CSkinMgr`, `g_CSnapMgr`, one prototype object per
    resource type, and others. The other 23 are labeled `g_obj_<address>`.
  - Constructors store their own vtable into `this` last and return `this`; destructors store it
    first. In all: 261 constructors and 286 destructors.
  - Sizes come from `push n; call operator new` before a constructor call or an inlined vtable
    store. When an inlined derived constructor follows a base constructor call, the derived
    vtable decides the class. That gives **sizes for 220 classes**, consistent with their bases.
    For example `CPc` 776 < `CPlayer` 988, and `UIEditCtrl` 144 < `UIBitmapEditCtrl` 160.
  - 74 `new` sites construct classes without RTTI. They are listed in
    `analysis/exports/Ragexe.exe.unknown_new.tsv` for later.
  - Known gap: the virtual-inheritance `std` stream destructors (one note in the report).
- Ledger now: 10,811 rows, **6,128 functions (1.69 MB) to port**, 32.0% of those bytes `named`.
  A further pipeline pass adds nothing.

- [x] Field layouts: `tools/recover_layouts.py` writes `analysis/labels/layouts.fields.tsv` and, for
  classes never created with `new`, estimated sizes in `layouts.classes.tsv`. The report is
  `analysis/exports/Ragexe.exe.layouts.tsv`.
  - Sources: every `[this + n]` operand in a class's constructors, destructors and virtual
    functions. `this` is tracked from ECX through register copies, `lea` and EBP stack slots.
    Reads of the vtable pointer are skipped.
  - Constructors alone write only 2.2% of the bytes. Most fields are first touched in virtual
    methods, so all of them count.
  - Types:
    - x87 operand width → `float`/`double`.
    - A loaded value used as an address → `pointer`; passed as `this` to a class method →
      `ptr:<Class>`.
    - `std::string::_Tidy`/`_Grow`/`_Copy` on `this+n` → `std_string`. Labeled `0x423980`,
      `0x424fa0`, `0x425210` from their copy-on-write reference-count code.
    - A fresh `operator new(8 + elem)` node linked to itself and stored at `this+n+4` →
      `std_list` (the MSVC 6 list constructor).
    - Arrays come from `eh_vector_constructor_iterator`; otherwise the access width.
  - Inheritance: an access belongs to the most-base class whose subobject contains it. Bases
    without a size get an extent estimated from their own functions' accesses. Structs are
    written flattened, so each class keeps its own vtable type.
  - `ApplyLabels` now applies kinds in order: sizes, fields, functions, data. It rebuilds each
    struct from its fields and keeps the vftable fields. Member objects and arrays are byte
    ranges, with the type in the comment, so rebuilding one class can't resize another.
  - Result: 292 classes, 7,912 fields. For the 220 sized classes the median class has 88% of
    its bytes covered (mean 77%); 36 are below 50%. 45 `std::string` members and 41 `std::list`
    members, including `UIWindow`'s at `+0x8`, inherited by every window.
  - Checked in the decompiler: `UIWindow::UIWindow` reads `this->field_0x8._Head = node`,
    `CGameMode::CGameMode` calls `std::string::_Tidy(&this->field_0x104, 0)`.

- [x] Non-virtual methods (in `tools/recover_layouts.py`, written to `analysis/labels/methods.functions.tsv`):
  - Rule: a default-named function that reads ECX before writing it (a `push ecx` that only
    reserves stack space doesn't count), called with `ECX = this+n` from a class's functions.
  - Owner: the class itself at `n = 0`; the most-derived base at `n`; or the `std::string`,
    `std::list` or `std::_Tree` member at `n`. With several calling classes, the owner is the
    most-derived class they all derive from.
  - Every round rescans from scratch and recomputes every owner. An earlier version kept
    first-seen owners, and that put a `UIWindow` method on `UIIllustWnd`.
  - Result: **550 methods** (499 on classes, 51 on containers), named `method_<address>`. No
    assigned method touches memory past its owner's size.
- [x] `std::map`/`std::set` members:
  - MSVC 6 `_Tree::_Init` is recognized by shape: the STL lock, a nil node created once,
    `operator new(node)`, and writes to `this+4` (`_Head`) and `this+0xc` (`_Size`). 20 instances.
  - 9 more functions call an `_Init` on their own `this`; these are the `_Tree` constructors.
  - The tree header is 16 bytes (allocator, comparator, `_Head`, `_Multi`, `_Size`). The node
    is left/parent/right, then the value, then a color word.
  - Result: 12 tree members (9 in `CGameMode`) and 6 global trees, labeled and typed (`g_tree_<address>`).
  - `ApplyLabels` gained a `std_tree` type and an optional type column for data labels.
- Coverage after both: median 92% (quartiles 69%–100%) for the 220 sized classes; 30 are below
  50% (was 36). `float` fields 69 → 127; 66 pointer fields now point to a known class.
- Reproducibility:
  - Regenerating every label file after apply, sync and classify gives identical files.
  - `ledger.py sync` no longer drops rows that carry a subsystem.
  - `ledger.py classify` re-applies the library rules: code from `0x005b325c` on is `crt`, and
    any `std` namespace is `stl`, now including the container methods.
  - A fresh import needs three `analyze.sh` passes before the function set stops growing.

- [x] Classes without RTTI (curated in `analysis/labels/inferred.{classes,functions,data}.tsv`):
  - Started from what `WinMain` touches, then every global left as `g_obj_<address>` and every
    game-code `new()` site whose callee had no RTTI class. Each name cites strings, imports and use.
  - 29 classes and 95 function names. Two class names come from strings in the binary: `C3dDevice`
    (`"HRESULT C3dDevice::RestoreSurfaces()"`) and `SceneGraphNode` (`"SceneGraphNode Load."`).
    The rest are descriptive and carry no `C` prefix, e.g. `ModeSwitcher` (creates
    `CLoginMode`/`CGameMode`), `TextureCache`, `ResourceManager` (`ResNameTable.txt`),
    `ClientState` (registry options, inventory, message tables), `MannerFilter` (`manner.txt`),
    `MoviePlayer` (Bink), `PathFinder`, `DataFile`, `XmlDocument` (`clientinfo.xml`).
  - The global with the `_Tree` at `+0xf8` is `GuildInfo`.
  - All 23 `g_obj_` globals are named. Six were STL maps; five are now named by what they hold,
    from the files that fill them (`g_itemInfoById`, `g_cardInfoById`, `g_skillInfoById`,
    `g_metalProcessItems`, `g_prevFontByDc`). The sixth, `0067cd88`, is the
    `ResourceManager` that `GetResourceManager` constructs on first use: its constructor had
    been taken for a tree constructor. A tree constructor must now write nothing past 16 bytes.
  - Of the 74 unresolved `new()` sites, 24 construct the new classes and 29 go to STL code:
    12 `std::_Construct` instances (placement copies into fresh list and tree nodes), the
    `std::string` copy constructor, and one inlined constructor. The 21 left are in the static
    CRT's locale code.
- Pipeline changes:
  - Curated files win: generators skip curated addresses, and `ApplyLabels` applies curated
    files after generated ones. A curated constructor (a row named after its class) drives
    the generators like a recovered one: global names (`g_<Class>`) and sizes from `new()`.
    8 classes are sized that way; 2 have curated sizes (`RenderTriangle` 156, allocated two at
    a time; `RenderQuad` 196).
  - `ApplyLabels` creates classes listed in a classes file and types every global with its
    class struct (`class:<Class>` in the data type column), skipping any that would cover
    another label. When a labeled function lies inside another (`ModeSwitcher`'s constructor
    is the tail-jump target of its initializer stub), it splits it off.
  - Methods are also assigned from calls made with `ECX = &global + n`, anywhere in the
    program: 3,276 call sites on 49 globals.
  - In a class that starts with a map (`TextureCache`), a function called at offset 0 that
    reads that map type's shared nil-node global is a `std::_Tree` method.
  - Result: **974 methods** (896 on classes, 78 on containers; was 550). 10,112 fields;
    median coverage of sized classes still 92%.
  - Ledger: 58.2% of the bytes to port are named (was 47.3%); 6,003 functions left to port,
    1,112 library.
  - Converges in two `--reanalyze` passes: the second regenerates identical label files.

- [x] Container classes: `std::string`, `std::list` and `std::_Tree` are Ghidra classes whose
  structs (`/std/string`, `/std/list`, `/std/_Tree`) hold the MSVC 6 layouts. Ghidra types
  `this` from the struct at the class's own category path, so all 113 container functions now
  take a typed `this` (8 `string`, 31 `list`, 74 `_Tree`), e.g. `std::string::_Tidy(string *this)`
  reads `this->_Ptr`. Member fields use the same structs (`UIWindow+8` is a `/std/list`), and
  `ApplyLabels` deletes the `/Ragexe/stl` copies that earlier runs made. One class per container
  serves every template instance, since the header layout does not depend on the element type.

- [x] Direct accesses to global objects (in `tools/recover_layouts.py`):
  - An absolute operand `[G + n]`, with `G` a typed global object, is a use of the class's
    field at `n`. It gets the same types as `this`-relative uses: x87 width, pointer when the
    loaded value is dereferenced, `ptr:<Class>` when it becomes `this` for that class.
    A call with `ECX = G + n` to a container method or member constructor places that member.
  - Only offsets inside the class's size, or the extent its own functions establish, count.
    Past that extent come unrelated globals: right after `g_MoviePlayer` lie the window
    handle and other scalars, which nothing ties to it.
  - Result: 2,210 uses on 11 classes, mostly on fields the methods had already found. 46
    fields added (32 in `ClientState`, 8 in `GuildInfo`) and 35 types refined (24 `dword` to
    pointer); two `ClientState` lists at `+0x1bc` and `+0x1c8` are now whole `std::list`
    members. 10,154 fields in all. The decompiler shows `g_ClientState.field_0x1bc._Head`.
  - Labels and ledger are stable after one `--reanalyze` pass.

- [x] Static runtime-code check (`tools/runtime_code_check.py`, report in
  `analysis/exports/Ragexe.exe.runtime_code.txt`):
  - No section is both writable and executable; `.text` is read and execute only.
  - Every call to `VirtualProtect`, `VirtualAlloc`, `VirtualFree`, `CreateFileMappingA`,
    `MapViewOfFile`, `LoadLibraryA`, `GetProcAddress` and `HeapCreate` is listed with its
    constant arguments, following `mov reg, [IAT]; call reg`. No allocation asks for
    executable memory. The libraries loaded by name are `ws2_32.dll` (`send`, `recv`),
    `cjconnector.dll` (`ConnectorKey`), `dbghelp.dll` (stack walking for the crash
    handler) and `user32.dll` (the CRT's message box). File mappings are read-only views of
    the archive (`CGPak`). `WriteProcessMemory` and `FlushInstructionCache` are not imported.
  - No instruction writes to an absolute address in `.text`. 15 immediates point into code
    without being a function entry, and all were reviewed: element constructors passed to
    `eh_vector_constructor_iterator` that Ghidra has not made functions, the `ws2_32`
    import thunks kept as fallbacks, a catch block's continuation, CRT tables, and two
    colour constants (`0x4040e0`, `0x505050`). Exception-handling set-up (`push -1;
    push handler`, `mov eax, handler; call _EH_prolog`) is filtered out.
  - The one `VirtualProtect` call is real self-modification: `PacketCodeModules::PatchStub`
    makes 10 bytes of `PacketFilterStub` writable and writes a `call` and a `jmp` into it.
    The call is later pointed at code that the server sends after packet 0x01C7, held in
    heap memory; every sent and received packet then passes through it. Specified in
    `docs/protocol/code-modules.md`; the functions are labeled.
- [x] Emulation oracle (`tools/emu/`, tests in `tests/emu/test_oracle.py`):
  - Unicorn maps the image with each section's protection, a flat GDT and an FS segment on a
    minimal TEB. `call(address, *args, this=)` returns EAX, EDX and ST(0). A sentinel return
    address ends the run, and there is an instruction limit.
  - Every import goes to a stub (`winapi.py`); calling one without a stub stops the run and
    names it. Stubs are deterministic: fixed tick start and step, fixed IDs, the
    Windows-1252 code page, read-only files resolved case-insensitively in `original/install`.
  - The CRT heap (`malloc`, `__nh_malloc`, `calloc`, `realloc`, `_msize`, `free`, newly
    labeled) is hooked to a zero-filling bump allocator. `init_crt()` runs `_ioinit` and the
    C half of `_cinit`, which stdio and float formatting need; game constructors never run.
  - Checked: `atoi`, `_itoa`, `atof` (result in ST(0)), `_strlwr`, `_stricmp`, `sprintf`,
    `fopen`/`fread` on `data.grf`, and the first game function, `CGPak::GetFileName`.
    `sprintf("%e")` gives `3.141590e+000`: MSVC 6 prints three exponent digits, which the
    port has to reproduce where it formats numbers the same way.
  - CLI: `cd tools && python -m emu [--crt] ADDRESS [ARG ...]` with `s:TEXT` and `buf:N` arguments.
  - Dependencies pinned in `tools/requirements.txt`; 12 tests run in under a second.

- [x] Subsystems (`tools/assign_subsystems.py`, rules in `analysis/subsystems.tsv`, report in
  `analysis/exports/Ragexe.exe.subsystems.tsv`):
  - Every game function has a subsystem. Bytes: `ui` 501 K, `game` 474 K, `world` 432 K,
    `res` 129 K, `render` 66 K, `core` 37 K, `net` 27 K, `audio` 5 K.
  - Seeds: 2,811 functions by class (a class rule covers its RTTI descendants), 80 by
    three address ranges, 34 by imports that one subsystem owns, 3 by single-function rules.
  - The rest propagates over the address order and the reference graph. Measured against
    the class-seeded functions, address neighbours agree 99.9% of the time, callees 96.9%,
    callers 87.5%. Neighbours go first: a helper lives in its own file, not its callers'.
  - Dynamic initializers mark the start of a source file, giving 141 runs of code: 121
    have a single subsystem. Import thunks, tail-jump stubs and all 3,232 game-code EH
    funclets (found through their parents' FuncInfo tables) take their owner's subsystem.
  - `ledger.py set --subsystem` refuses game code, so the rules file stays the only
    source.
- [x] `docs/architecture.md`:
  - **Start-up.** `WinMain`, step by step.
  - **Modes.** `ModeSwitcher` with `RequestMode` and `Quit`, and the `CMode` slots
    `OnInit(map)`, `Run`, `OnExit`, `OnFrame`, `SendMsg`, `OnChangeState`. Every map
    change rebuilds `CGameMode`; login packet 0x73 hands over to the game mode.
  - **One frame.** Messages, then packets, then logic, then `GetFrameStepsDue()` world
    steps, then the scene, UI, cursor, fade and present.
  - **Timing.** A fixed step of `1000 / 60` = 16 ms (62.5 Hz), with catch-up steps. The
    flag `0x61ec58` switches to one step per frame and also gates the windowed
    vertical-blank wait.
  - **Input and networking.** DirectInput for the mouse. Sockets through
    `WSAAsyncSelect` on the window thread.
  - **Threads.** Three helpers, with almost no locking: the lazy model loader at
    `THREAD_PRIORITY_HIGHEST`, the music fade, and a debug effect thread.
  - **Also covered:** the global objects and the shutdown order.
  - 50 new curated names (`inferred.functions.tsv`):
    - `WindowProc`, `PumpMessages`, `SetFrameRate`, `GetFrameStepsDue`;
    - both packet dispatchers and the thread procedures;
    - the six `CMode` slots in all three classes;
    - six `SceneRenderer` methods. Their callers load `ECX` from `g_pSceneRenderer`,
      which the method rule does not follow yet.
  - 59.0% of the bytes to port are named (was 58.3%). Converges in two `--reanalyze` passes.

- [x] Methods called through pointer globals (in `tools/recover_layouts.py`):
  - A call with `ECX = [P]`, `P` a dword in `.data`, is a call on the object `P` points to.
    There are 793 such sites on 89 pointers.
  - `P`'s class is its curated `ptr:<Class>` type, or the common base of what is stored
    into it: the address of a global object, or the result of a constructor. A
    non-virtual call through a pointer of that static type must be a method of that
    class.
  - The methods called through `P` are only a check, since they show just a lower bound.
    `0x686584` is a scratch pointer that CRagEffect code points at whatever primitive it
    last made. Calls through it only show `CRenderObject`, and using them put 153 fields
    of derived classes onto `CRenderObject`.
  - Result: 4 pointers with a class, 3 of them newly typed and named (`g_pCLanguage`,
    `g_pCGPak`, `g_pmg_mgType1_CMiniGame001`), and 55 new methods: 42 on
    `SceneRenderer`, 10 on `CLanguage`, 3 on `CMiniGame001`. None of the named methods
    called through a typed pointer contradicts its type. 1,019 methods in all.
- [x] `tools/recover_objects.py`:
  - A known constructor called with `ECX` = a `.data` address outside the initializers
    builds a function-local static. An `atexit` stub registered there names its
    destructor.
  - This gives 4 more global objects: the three language objects that `g_pCLanguage`
    points to, and `mg::mgType1::CWorld`.
  - Constructors that copy `this` into `EAX` at the start and keep it there until `ret`
    are now recognised: `CLanguage`, `mg::CMGEngine` and a second `CSurface` overload.
  - `CLanguage` now shows three `std::string` members.
  - 60.0% of the bytes to port are named (was 59.0%). Converges in two `--reanalyze`
    passes.

- [x] Packet handlers (`tools/recover_packets.py`, labels in `analysis/labels/packets.functions.tsv`,
  report in `analysis/exports/Ragexe.exe.packets.tsv`):
  - Every id from 0 to 0xffff is run through each dispatcher's switch in the emulator, from
    the id read (`CRagConnection::GetPacketId`) to the first call. This needs no
    decoding of the switch's shape. A direct decode of the game mode's index and jump
    tables gives the same handler for all 366 entries.
  - Result: 216 game-mode and 16 login-mode handlers, each serving one id, all named
    `HandlePacket_<id>`.
  - All of them are methods of their mode. The dispatcher passes the mode in `ECX` and
    the handler pops its own argument, even though 98 handlers never read `this`.
    `DispatchGamePackets` is likewise `CGameMode::DispatchPackets`.
  - Eight ids are skipped on purpose (0x75–0x77, 0x7a, 0x83, 0x84, 0x187 in the game,
    0x187 at login). Any other id goes to `CRagConnection::vf02`, which empties the
    receive buffer.
  - Nine handlers print their packet's name from the binary, e.g. 0x0144
    `PACKET_ZC_COMPASS` and 0x0148 `PACKET_ZC_RESURRECTION`; the report records it.
  - `binindex.reads_ecx_first` now steps over `_chkstk`, which saves and restores `ECX`.
    Large-frame methods, the game dispatcher among them, used to lose `this` at the
    probe. Methods 1,019 → 1,042 (86 on containers); fields 10,149 → 10,170.
  - 65.1% of the bytes to port are named (was 60.0%). Converges in two `--reanalyze`
    passes.

- [x] Packet framing (`docs/protocol/framing.md`; lengths in `analysis/exports/Ragexe.exe.packets.tsv`):
  - `RecvPacket` ends with `CRagConnection::CutPacket`, which peeks at the id and looks it
    up in a `std::map<int, int>` at `CRagConnection+0x7c`.
    - −1 means the total length is the word at offset 2.
    - Otherwise the value is the total length, at least 2.
    - A missing id is inserted as 0 and read as 2 bytes, and the dispatcher's default
      then clears the receive queue.
  - The map is filled by `InitPacketLengths` (`0x419400`). `recover_packets.py` runs it in
    the emulator on an empty map and walks the tree: 371 ids from 0x64 to 0x1e0, 61
    variable, fixed lengths from 2 to 186. Every handled id has an entry. 135 ids have no
    handler, among them those the client only sends.
  - Check: no fixed-length handler reads a constant offset past its length. For 104 of
    the 166 that read the packet at a constant offset, the furthest read ends exactly at
    the length. The check holds a register only within its basic block unless the
    argument load is its only write; a linear sweep had taken actor pointers in `EDI`
    for the packet.
  - New curated class `PacketQueue` (28 bytes), with `Peek`, `Read`, `Skip`, `Append`,
    `Size`, `Clear`. `CConnection` holds three at `+0x24`, `+0x40` and `+0x5c`, and
    `CRagConnection` a fourth at `+0xa8`. Also named:
    - `CConnection::Send` and `SetHoldSends`, the hold flag `+0x22` that parks sends in
      `+0x5c`;
    - `CRagConnection::SendPacket`, `IsFixedLength`, `GetPacketLength`.

- [x] Where global objects end (in `tools/recover_layouts.py`; table in `docs/architecture.md` §8):
  - Nothing gives a size to a class that is never created with `new`. Each global object
    is bracketed instead:
    - **At least:** the end of the last field its class uses.
    - **At most:** the first later address that is a variable of its own. That is either
      another data label, or an address written by a dynamic initializer that names
      nothing inside the object; each initializer sets up one variable.
  - Addresses between the two bounds are reported, not guessed. Two hints come with them:
    - **Probable end:** the first address used only by source files that name nothing else
      of the object.
    - **Named by the class's own methods:** an absolute address that the class's own
      methods use.
  - Signals tried and dropped as bounds:
    - Any absolute address a class method uses. `ClientState`'s methods name three of
      its own fields through `g_ClientState`.
    - Any initializer write. `g_DirectInputMouse`'s inlined constructor clears field 4
      without naming the object.
    - The link order of the files. Only 345 of 531 `.bss` addresses used by a single file
      fall in one increasing run of files.
  - Check, run on every pass: none of the 48 global objects has a label or foreign
    initializer write inside its extent. Of the 342 addresses used inside the extents,
    2 would pass the probable-end test.
  - Results:
    - `ClientState` 6,356–6,360 bytes and `UIWindowMgr` 1,152–1,288. Both stop at a
      0.25 float that each file sets up for itself, and nothing touches the gap before it.
    - `C3dDevice` 572–616, with 9 undecided addresses; it probably ends by `+0x248` (584).
    - `CRagConnection` exactly 204, once the nil node its length map shares with
      `CKeyPad`'s map was labelled (`g_intMapNil`, `g_intMapNilUsers`).
  - The bounds are in the evidence column of `layouts.classes.tsv` and in a new `max_size`
    column of the layouts report. The sizes stay the lower bound.

- [x] Inlined `std::map` constructors (in `tools/recover_layouts.py`):
  - The rule: an `operator new` node whose parent (`node + 4`) is loaded from a `.data`
    address, the instantiation's nil node, and that is then stored at `this + n + 4` is an
    inlined `_Tree::_Init`. That puts a tree at `n`, with value size = node size − 16.
  - The same pattern also matches all 20 out-of-line `_Init`s that were already known.
  - It finds 7 inlined trees:

    | Class | Tree at | Value size |
    |---|---|--:|
    | `CRagConnection` | `+0x7c` (the packet lengths) | 8 |
    | `CKeyPad` | `+0xac` | 8 |
    | `PathFinder` | `+0x1078` | 8 |
    | `ResourceManager` | `+0x58` | 32 |
    | `UIGuildMemberManageWnd` | `+0x120` | 16 |
    | `UIGuildPositionManageWnd` | `+0x1b0` | 40 |
    | `mg::mgType1::CWorld` | `+0x40` | 20 |

  - Where an out-of-line `_Init` uses the same nil node, its node size agrees (4 of the 7).
  - 17 more functions become `std::_Tree` methods (container methods 86 → 103). These
    include the map lookups in `IsFixedLength` and `GetPacketLength`. `ledger.py
    classify` moves 15 of them into the `stl` library code (393 → 408 functions), which
    takes about 3 KB out of `net` and smaller amounts out of `ui`, `game` and `world`.
    Converges in two `--reanalyze` passes.
  - Field types: of one kind, a parameterised type now wins over the bare one, so a tree
    or list keeps its value size even when untyped method calls outnumber the typed use.
    That gives value sizes to 11 existing trees (9 in `CGameMode`, 2 in `UIWindowMgr`)
    and 4 lists.
  - No map is built inline into a global address. `0x4e3ad0` builds a tree inline, but
    into a pointer argument (a copy-construct helper), so it places no member.

- [x] DirectDraw 7 / Direct3D 7 fakes for the emulation oracle (`tools/emu/directx.py`; the
  table in `docs/render/texture-stages.md`):
  - **Interfaces.** `IDirectDraw7`, `IDirectDrawSurface7`, `IDirectDrawClipper`,
    `IDirectDrawPalette`, `IDirectDrawGammaControl`, `IDirect3D7`, `IDirect3DDevice7` and
    `IDirect3DVertexBuffer7`. Their slots follow the DirectX 7 SDK order.
  - **How a call is caught.** Each object is a vtable pointer in emulated memory, and
    each slot points at its own stub. A hook reads `this` and the arguments, records the
    call and runs the handler. A method without a handler stops the run with its name,
    as an unstubbed import does.
  - **Checked against the client.**
    - The interface IDs it passes: `IID_IDirectDraw7`, `IID_IDirect3D7`, and the HAL and
      T&L HAL device IDs.
    - Its vtable offsets and argument counts.
    - The `DDSURFACEDESC2` offsets it fills.
  - **What the fakes model.**
    - Surfaces have pixel memory, so `Lock` returns a real pointer and pitch, and a
      flip chain creates its back buffer.
    - The device keeps its render states, stage states, textures, transforms and
      viewport.
    - Every draw records its vertices (decoded by FVF), its indices and a snapshot of
      that state.
    - Reference counts are kept.
  - **Callbacks.** A stub that has to call back into the client is a Python generator
    that yields the callbacks to run. This covers display-mode, z-buffer and
    texture-format enumeration.
  - **New stubs:** `DirectDrawCreateEx`, plus `GetClientRect`, `ClientToScreen`,
    `SetRect` and `SetWindowLongA` for one window.
  - **Checked by running client code**, in 5 new tests (17 in all):
    - `C3dDevice::Init`, windowed and full-screen, through every step: the z-buffer
      enumeration callback picks the 16-bit format.
    - `C3dDevice::Release`, which leaves no device reference behind.
    - The renderer's list flush (`0x410560`) drawing two quads.
    - Each of the 16 texture-stage modes of `0x4112d0`.
  - **First results for M4:**
    - The table of the 16 combiner modes.
    - The device's initial render state.
    - Two facts about drawing: the flush only re-sets a texture that changed, and
      texture coordinates are scaled in place before the draw.
  - Not faked: `DirectDrawCreate`, which hands out the DirectDraw 1 interface used by
    the display-device enumeration (`0x402da0`, `0x406220`, `0x406960`), and
    `DirectDrawEnumerateA`.

- [x] Toward the naming target (M1 exit: 90% of code functions named or library-tagged):
  62.6% → **65.5%** (3,525 named game functions and 1,136 library ones of 7,115); 2,454
  game functions keep default names. 66.1% of the game-code bytes are named.
  - **Initializer helpers** (`tools/recover_objects.py`, 189 functions), each named after
    the global it serves, using that global's label:
    - `construct_<global>`: loads ECX with the global and calls a constructor.
    - `register_destroy_<global>`: `push D; call atexit`.
    - `destroy_<global>`: the atexit handler.
    - `atexit_no_op`: the lone `ret` registered for globals without a destructor.
    - `static_init_NNN_body`: any other code an initializer reaches.
  - **Class inference for ECX** (`recover_layouts.py`, `typed_calls`). A direct call with
    ECX holding an object of a known class counts as a call with `this`, in any function.
    - Sources: `this`; a global object or typed pointer global; a constructor's result;
      a stack object once a constructor has run on it. Values are followed through
      registers and EBP slots.
    - Checked against the methods already known: 6,527 call sites agree, 18 do not.
      All 18 are one misassignment: `0x4a58e0` was on `UIEmblemWnd`, but it is called on
      `UIBitmapButton` and `UIStaticText` too. It moves to `UIWindow`.
    - Pointer fields are not a source. Their class is only the common one of the calls
      made through them, and code casts them down: as a source they pulled window
      classes' methods into `UIWindow` and grew it from 76 to 120 bytes.
    - 18 more methods.
  - **Names from strings:** `CTokenizer::operator[]` (`0x422b30`, a new curated class)
    and `InitDefaultClientInfo` (`0x4c97c0`). These are the only raw functions whose
    strings name them.
  - **Measured and left:**
    - No raw function uses the STL length errors.
    - No raw function is a pure jump stub.
    - Following pointer fields as well gained only 23 methods in a trial, and it was
      unsound.
    - 1,365 raw functions read ECX. At most of their call sites, ECX is copied from
      another register whose value no rule types.
  - Converges in two `--reanalyze` passes.

- [x] Naming pilot on the `net` subsystem: all 25 of its unnamed functions, 4.7 KB of
  code. Named by reading them, with a reviewed result now in `inferred.functions.tsv`.
  65.5% → **65.9%** of code functions named or library-tagged.
  - **Procedure.** One agent got the clean-room rules, the decompile and disassembly
    helpers and the protocol docs. It wrote one row per function: namespace, name,
    calling convention, confidence (16 high, 9 medium), and evidence citing addresses.
    I then checked every row against the code: callers, including exception-unwind
    funclets, `ret N`, imports and constants. The `OnSocketEvent` jump table confirms
    the four socket handlers: `FD_READ` → `0x418aa0`, `FD_WRITE` → `0x418a30`,
    `FD_CONNECT` → `0x418b40`, `FD_CLOSE` → `0x418b50`.
  - **Quality.** All 25 rows are supported by the code. Two were changed in review:
    - The MD5 functions went under a curated `Md5Context` class rather than `Global`
      `__thiscall`.
    - `0x41d9e0` became `std::_Construct`, like the existing helpers of that shape.
  - **What the names are.**
    - `CConnection::OnRecv` (its name is in the binary's "Socket OnRecv Error"),
      `OnSend`, `OnConnect` and `OnClose`; `CleanupWinsock`.
    - The destructors of the length map, of `PacketCodeModules` and of `std::list`.
      They are reached only from unwind funclets.
    - The map's rotations, iterator steps and node allocation, and list erasure.
    - A second, unused `PacketQueue` constructor that takes a capacity.
    - MD5 (`Md5Context::Init`, `Update`, `Final`, `Transform`), recognised from its
      constants (`0x67452301` at `0x41f840`, `0xd76aa478` in `0x41eea0`).
    - 14 of the 25 are STL code, so `classify` moves them to `stl`.
  - **Finding for M6.** `CLoginMode::HandlePacket_01DC` (`0x53ebc0`) hashes the packet
    body (from `+4`) together with the string at `CLoginMode+0x120`. The flag byte at
    `0x67c9ce` decides which comes first. It replies with the 47-byte packet `0x1dd`:
    | Offset | Size | Contents |
    |---|--:|---|
    | `+2` | 4 | the dword at `0x67c9c8` |
    | `+6` | 24 | the bytes at `CLoginMode+0x160` |
    | `+0x1e` | 16 | the digest |
    | `+0x2e` | 1 | a byte from `0x53f100` |
  - **Cost.**
    - The agent: 139k tokens, 23 tool calls, under 5 minutes. That is 5.6k tokens a
      function, or about 29 a byte.
    - The review: roughly another 1k tokens a function.
    - For the remaining 2,429 functions (560 KB), about 14–16M agent tokens plus
      2–3M for review.
  - **Changes before the full run.**
    - A per-function context script that lists, for each address: callers, including
      unwind funclets and the function whose exception tables own them; callees with
      imports resolved; `ret N`; whether it reads ECX first; strings; and data
      references. The agent had to write its own.
    - A second `__thiscall` test: the caller sets ECX just before the call, even when
      the body never reads it.
    - A fixed set of names for STL internals, to keep runs consistent. 101 container
      helpers are still `method_xxx`.
    - Batches by address range (the source files), not by subsystem tag. `net`
      included MD5 and map code shared with `CKeyPad`.
    - Say explicitly that recognising a standard algorithm from its constants counts as
      reading the binary.

- [x] Tools and conventions for naming by reading (`docs/naming.md`):
  - **`tools/fn_context.py ADDRESS...`** prints, per function:
    - its ledger row and the named neighbours on each side;
    - calling-convention evidence: the bytes `ret` pops, whether the body reads ECX
      first, and at how many call sites the caller sets ECX just before the call;
    - its references, grouped by function. Unwind funclets come with the function
      whose EH tables list them; pointers in `.rdata` come with their vtable slot.
    - its callees, with imports resolved through thunks, and its virtual calls by slot;
    - its strings, data addresses with their labels, and `[ecx+n]` offsets.
  - **`tools/ghidra/decompile.sh OUT ADDRESS...`** (with `Decompile.java`) decompiles a
    batch read-only, in about 4 s.
  - **The funclet-to-parent mapping** moved from `assign_subsystems.py` to
    `binindex.eh_parents()`.
  - **Rule 4 is spelled out** for public interfaces, standard algorithms and library
    internals. The pilot showed the clash: the existing STL labels used MSVC 6's
    internal names (`_Tree`, `_Init`, `_Lockit`, `_Tidy`, `_Construct`), which the
    binary does not contain.
  - **The convention for library code.**
    - Namespaces: RTTI names where they exist, otherwise the standard type
      (`std::list`, `std::vector`, `std::string`, `std::pair`), and `std::Tree` for the
      tree behind map and set.
    - Lower-case names are the standard's public members, used only when the behaviour
      matches.
    - CamelCase names describe internals, from a fixed table in `docs/naming.md`.
  - **Renamed under the convention:**
    - `std::_Tree` → `std::Tree`, with its `_Init` → `InitHead` and constructors `_Tree`
      → `Tree`;
    - `std::_Construct` → `std::ConstructAt`;
    - `std::_Lockit` → `std::StlLock`, `std::_Locinfo` → `std::LocaleInfo`;
    - `_Xlen` / `_Xran` → `ThrowLengthError` / `ThrowOutOfRange`;
    - the string helpers → `FreeBuffer`, `GrowBuffer`, `ReallocateBuffer`;
    - the locale and stream `_Init`s → `InitGlobal`, `InitDefaults`, `InitBuffer`;
    - the pilot's pair and vector helpers → `std::pair::pair` and
      `std::vector::insert`.

    The container structs' fields became `allocator`, `buffer`, `length`, `capacity`,
    `head`, `size` and `multi`. `ApplyLabels` removes the old namespaces and their
    structs once they are empty.
  - Converges in three `--reanalyze` passes: the namespace rename needs one more. Still
    65.9% named or library-tagged; this step names nothing new.

- [x] Naming run 1: the 305 unnamed functions of four areas, split into 15 source-file
  batches:
  - vector and matrix math (`0x409540`–`0x40bf50`);
  - DirectSound (`0x41ff40`–`0x420320`);
  - the resource loaders (`0x420790`–`0x435250`);
  - Miles (`0x5b0600`–`0x5b0fa0`).

  The rows are in `inferred.functions.tsv`.
  - **Procedure.** Two agents in one workflow. One named every function, batch by
    batch, writing its rows to a file after each batch. A second checked every row
    against the code and accepted, revised or rejected it. Each function's context
    and decompiled code were prepared in advance (one `fn_context.py` run, one
    `decompile.sh` run), so the agents never opened the Ghidra project.
  - **Quality.**
    - The namer's confidence: 159 high, 130 medium, 16 low.
    - The reviewer accepted 296 rows, revised 9 and rejected none. The revisions were
      allocator members named as free functions (now `std::allocator::construct` and
      `destroy`), evidence that misread the code (the magenta colour key in
      `CBitmapRes::LoadJpeg`; a float tolerance in `IsIdentityMatrix` that is really
      after integer truncation), off-by-one guard bits, and one unnamed function.
    - My checks: every address has one row, every namespace exists or is a standard
      type, and no name uses a library-internal name. An evenly spaced sample of 8
      accepted rows was all correct against the disassembly.
    - I changed 7 `CImfRes` names: layer, action and frame became entry, record and
      index (`GetEntryCount`, `GetRecordLength`, ...). No string or caller in the
      binary shows those terms, so they could only have come from knowledge of the
      format.
    - The other domain words have sources in the binary: "hair_pivot", "#rsxname",
      "#action", the `AIL_` imports, "Failed PlayDigital".
  - **Result.** 132 of the 305 are STL template code, now library; 173 are named game
    functions. Code functions named or library-tagged: 65.9% → **70.1%**; game-code
    bytes named: 69.5%. Converges in two passes.
  - **Cost.** 797k agent tokens, 195 tool calls, 36 minutes: 2.6k tokens a function
    including the review, less than half the pilot's 5.6k, which had no review.
    Preparing the context in advance and one long-running agent instead of many
    account for the difference. For the remaining 2,124 functions (500 KB): about
    5.5–7M agent tokens with the same setup.
  - **Fed back into `docs/naming.md`.**
    - Fixed names for what the table lacked: `std::allocator` members, vector `size`,
      `capacity` and `operator=`, the sort helpers, `AllocateElements`, the string's
      `ReallocateBufferTail` and `GetRefCountByte`.
    - A function with no arguments whose callers set ECX is `__thiscall`.
    - Classes are proposed, not invented, while naming.
    - A file format's parts are named by their shape unless the binary names them.
  - **Class proposals from the run, not yet decided:**
    - math: `Matrix` (48 bytes; the identity at `0x605598`; 30 of the math functions
      are its `__thiscall` methods, in `Global` for now), `Quaternion`, `Vector3`;
    - sound: `MixerVolumeControl`, `SoundBuffer`, and a Miles `SoundSystem` for the
      globals `0x687fb8`–`0x68851c`;
    - resources: the record structs `ActMotion`, `RmaActionRecord`, `ImfRecord`,
      `GndCell`, `AttrCell`; `LensFlareTable`, `TexturedSkyBox`, `ModelSkyBox`,
      `EffectEntry`;
    - and a remark that `GrannyMotionTable` shares `C3dGrannyModelRes`'s layout, 0x110
      bytes lower.

- [x] Class proposals from naming run 1. A proposal was accepted when its functions are
  `__thiscall` methods that callers apply to one kind of object, with the size shown in
  the code. Six classes are in `inferred.classes.tsv`, with subsystem rules:
  - `Matrix` (48 bytes): 28 methods, at 282 call sites on stack matrices, array elements
    and fields. The `Make*` methods copy the 12-dword identity from `0x605598` first.
    Names drop the redundant word: `MultiplyMatrix` → `Matrix::Multiply`.
  - `RmaActionRecord` (28), `ImfRecord` (48), `ActFrame` (76): elements of the action,
    entry and frame vectors of `CRmaRes`, `CImfRes` and `CActRes`. Each has a
    destructor, `operator=` and a scalar deleting destructor.
  - `GndCell` (28): the cell `CGndRes::GetCell` returns, with `SetFromRecord` for the
    file's two record layouts.
  - `AnimFrameArrays` (8): the per-frame entries at `AnimModelResNode+0x48`, with two
    array allocators, a destructor and a vector deleting destructor.

  `recover_layouts` fills them from their methods: `Matrix` gets 12 floats (all 48
  bytes); `GndCell` and `AnimFrameArrays` are covered completely; the others 75–89%.
  - **Renamed by the shape rule.** `ActMotion`, `CActRes::GetMotion` and
    `GetMotionCount` became `ActFrame`, `GetFrame` and `GetFrameCount`. The binary has
    no "motion" (only "emotion" strings), but the elements are stepped through with a
    per-action delay, which shows they are frames.
  - **Not accepted:**
    - `Quaternion` and the Miles `SoundSystem`: their functions are `__cdecl` on
      pointers or globals, so no methods.
    - `Vector3`, `ModelFace`, `EffectEntry`, `AttrCell`: one function each.
    - `MixerVolumeControl`, `SoundBuffer`, `TexturedSkyBox`, `ModelSkyBox` and the
      lens-flare table: nothing references their functions. The one exception is
      `CView::~CView`, which destroys a model sky box. Unreferenced code needs no
      class and no port.
  - The `GrannyMotionTable` / `C3dGrannyModelRes` layout likeness is noted, not acted
    on.
  - Fields 10,208 → 10,261. Code functions named or library-tagged: **70.2%**, with
    `ActFrame::~ActFrame` newly named. Converges in two passes.

- [x] Code nothing reaches (`tools/find_unreferenced.py`; the new ledger column `reach`; the
  report `analysis/exports/Ragexe.exe.unreferenced.tsv`):
  - **What counts as a reference:**
    - a call or jump from another function, funclets included;
    - an instruction operand holding the function's entry address;
    - a 4-byte value equal to the entry anywhere outside decoded instructions: the data
      sections at every alignment, and the gaps between functions in `.text`;
    - falling through from the function before it, when that one doesn't end in `ret`,
      `jmp` or `int3`.

    A stray match only keeps a function live, so the scan errs on the safe side.
  - **Roots** are the entry point and every data match. Funclet slots in the EH
    FuncInfo tables are the exception: they make a funclet reachable only when its
    parent is.
  - **Result for game code:**
    - 238 functions (81.5 KB) are `unreferenced`: 171 of them still unnamed, 67 named.
    - 109 functions (27.7 KB) are `dead`, referenced only from those.
    - Library code adds 149 functions (21.5 KB).
  - **Examples:** the DirectSound buffer and mixer code, the sky boxes and lens flares,
    `IsBoxInViewFrustum`, the unused `PacketQueue` capacity constructor, and an
    out-of-line copy of `CMasterChaser`'s constructor, which every caller inlined.
  - **Checked:**
    - A sample of 10 unreferenced functions, with a raw search for each entry's 4 bytes
      at any offset (inside instructions too) and for any `rel32` call or jump: no hits.
      The function before each one ends in `ret`.
    - A sample of `dead` rows: each is referenced only by unreferenced or dead code.
    - Known live code is reached: `WinMain`, `WindowProc`, the mode slots, the
      initializer helpers, the packet handlers behind the jump tables.
  - **The report** now leaves this code out of the port queue (5,486 functions,
    1.53 MB) and states the naming figure over reachable code: **71.8% of 6,619**.

- [x] Naming run 2: the 288 unnamed reachable functions of the source files from
  `0x401000` to `0x448800`: rendering (`C3dDevice`, `SceneRenderer`, textures, fonts),
  input, the first resource files and the first UI files (skins, snapping, the basic
  controls). Same procedure as run 1, with one sentence added to the prompt: a game or
  file-format term needs a string, RTTI or import behind it. The rows are in
  `inferred.functions.tsv`.
  - **Quality.**
    - The namer's confidence: 184 high, 103 medium, 1 low.
    - The reviewer accepted 271 rows, revised 17 and rejected none. The revisions:
      - five rows in the wrong class: two `SceneRenderer` stubs were in `Global`, a
        `SnapObject` helper in `Global`, and two bar-graph setters on a subclass instead
        of the class that owns the field;
      - six with evidence that misread the code: a wrong field offset, a colour that
        `vf20` never reads, check-box bitmap labels in the wrong order;
      - names more specific than the code: `DeleteQueuedTextures` for a "recycled"
        pool, `DrawPrimitiveWithTexCoordArray` for a "second texture coordinates" draw
        whose FVF has one set.
    - My checks: every address has one row, every namespace exists, no duplicate names.
      An evenly spaced sample of 8 accepted rows was all correct against the
      disassembly. The domain words have sources in the binary: "<Basic Skin>",
      "\colorchip.bmp", "\ShortCutList", and RTTI names such as `UIEmotionWnd`.
  - **Found in existing labels:**
    - `recover_layouts` kept treating an EBP stack slot as `this` after the slot was
      overwritten (`0040f3cd`), so two render-primitive functions were `SceneRenderer`
      methods. A store to a slot now ends it. The two are named in `Global`
      (`ScaleTexCoordsByTexture`, `DrawRenderPrimitive`).
    - `VertexBuffer::Create`'s evidence had the fields swapped: the count is at `+0`
      and the stride at `+4`.
  - **New labels:** `g_basicSkinName` (a `std::string`) and `g_twoTexCoordVertices` (a
    function-local static vector) in `inferred.data.tsv`; `recover_objects` names the
    function-local static `g_ActFrame` from the new `ActFrame` constructor.
  - **`docs/naming.md`'s library table** gained `PartitionRange`, `MedianOfThree`,
    `UnshareBuffer`, `copy_backward`, `fill`, `begin`, `replace` and list `resize`.
  - **Result.** 152 of the 288 are STL template code, now library; 136 are named game
    functions, plus the two above. Seven UI controls' bitmap and text fields became
    `std::string` members (fields 10,263 → 10,243). Code functions named or
    library-tagged: 71.8% → **76.2%** of 6,619; code to port 5,334 functions, 75.4% of
    its bytes named. Converges in three passes (the second picks up the two rows added
    after the first).
  - **Cost.** 951k agent tokens (namer 524k, reviewer 427k), 212 tool calls, 35
    minutes: 3.3k tokens a function, more than run 1's 2.6k. This batch had more game
    code with long bodies (the renderer, UI controls), and the namer wrote its own data
    cross-reference helper because `fn_context.py` lists references only to functions.
  - **Class proposals from the run, not yet decided:**
    - `TranslationKeys`, `RotationKeys`, `ScaleKeys`: the time-keyed vectors at
      `C3dNode+0xe4`, `+0xf4`, `+0x104` (16-, 20- and 32-byte records), with
      `Add`/`Read`/`Get*MatrixAt` methods (`0x4012c0`–`0x402070`);
    - `FontEntry` (20 bytes), the element of `FontCache`'s vector, with six methods;
    - `RenderPrimitive`, the base of `RenderTriangle` and `RenderQuad` (vertices `+0`,
      count `+8`, texture `+0xc`, primitive type `+0x20`), with about nine methods;
    - `VertexBufferRange` (a `Matrix` then a vertex buffer range, sorted by texture in
      `SceneRenderer+0x138`) and `TexturePoolEntry` (0x2c bytes, in 16 size-binned
      lists at `SceneRenderer+0x194`).

- [x] Class proposals from naming run 2, by run 1's test: `__thiscall` methods that callers
  apply to one kind of object, with the size shown in the code. Five classes are in
  `inferred.classes.tsv`, with subsystem rules, and 22 functions moved into them:
  - `TranslationKeys`, `RotationKeys`, `ScaleKeys` (16 bytes each: a vector of 16-, 20-
    and 32-byte keys). They sit at `C3dNode+0xe4`, `+0xf4` and `+0x104`, and the first
    two also at `ModelResNode+0x14c` and `+0x15c`. Their methods are `Read` (from
    `C3dModelRes::vf03`), `Add` (from `C3dNode::vf01` and `C3dNodeHAL::vf01`) and
    `GetMatrixAt` (from `0x4b6db0`). The element sizes come from how the code counts
    them (division by 20, `sar 5`).
  - `FontEntry` (20 bytes): the element of `FontCache`'s vector, stepped by `0x14` in
    `GetFont`. Six methods, including its constructor (`0x4077d0`), `IsLess` (26 call
    sites, in `GetFont` and the sort) and `CreateFont`.
  - `RenderPrimitive` (36 bytes): the base of `RenderTriangle` and `RenderQuad`, whose
    constructors call its constructor (`0x40c1b0`) on `this` and keep their vertices
    right after it at `+0x24`. `SceneRenderer` builds one on its stack and copies its 9
    dwords into the 0x2c-byte nodes of its list at `+0x260`. Eight methods: the
    constructor, `SetArrays`, `SetVertexPosition`, `ComputeVertexFog`,
    `ScaleTexCoordsByTexture` and three `Draw` variants. The base relation is in the
    evidence only: the tools take bases from RTTI.
  - **Not accepted:**
    - `VertexBufferRange`: two methods, but the objects are only reached through
      the vector of pointers at `SceneRenderer+0x134`, so the size never shows.
    - `TexturePoolEntry`: one function.
  - **`recover_layouts`:** a call with `ECX = this + n` to a curated method of a sized
    curated class now types the member at `n`, as a constructor call already did. This
    adds the three key vectors to `C3dNode` (and so `C3dNodeHAL`), two `Matrix` members
    to `ModelInstance` (`+0xc0`, `+0xf0`, replacing 12 loose floats) and two to `CView`
    (`+0x94`, `+0xc4`). The `PacketQueue` members of `CConnection` now count all their
    uses. Fields 10,243 → 10,235.
  - Still 76.2% named or library-tagged; this step moves names and names nothing new.
    Converges in two passes.

- [x] Naming run 3: the 298 unnamed reachable functions of the UI source files from
  `0x4487e0` to `0x4b6900`: the windows, the window manager, its saved window records
  and their element records. Same procedure; the prompt gained one sentence on class
  hierarchies (a method belongs to the class that owns the fields it uses, often a
  base), after run 2's misplaced bar-graph setters. The rows are in
  `inferred.functions.tsv`.
  - **Quality.**
    - The namer's confidence: 122 high, 172 medium, 4 low.
    - The reviewer accepted 289 rows, revised 9 and rejected none:
      - three functions on 20-byte {string, int} vector elements had been called
        `std::pair` without evidence; they are `AssignStringIntRecord` and its kin in
        `Global`;
      - three names were more specific than the code (`SetLineCountStep` →
        `SetHeightStep`; `ReleasePendingChild` → `DetachEditCtrl`;
        `InvalidateAllWindowsOnce` → `InvalidateAllWindows`, as the guard is cleared
        on every pass);
      - two now follow the binary's "SAYDIALOGWNDINFO" spelling, and one had the tree's
        multi flag at the wrong offset.
    - My checks: every address has one row, every namespace exists, no duplicate or
      clashing names. Seven evenly spaced accepted rows were correct against the
      disassembly.
    - 70 rows are the reset/validate pairs of the window records in `UIWindowMgr`
      (`+0x244`–`+0x4f4`). Their window names come from the "<NAME>WNDINFO.X/Y/W/H"
      registry strings, or, for the Guild, Emotion, EmotionList, PetInfo and MiniGame
      windows, which have no string, from the RTTI window class whose `vf37` calls
      them. One record that only the reset-all routine touches is
      `ResetWindowInfo2D8`.
  - **Tool fixes from the namer's notes:**
    - `or ecx, -1` (the `strlen` idiom) counted as reading ECX, so `reads_ecx_first`
      called `__cdecl` functions `__thiscall`. It is a write now.
    - Without that false read, `recover_layouts` lost methods that never use `this`.
      It now also applies `docs/naming.md`'s callers test: every direct call sets ECX
      just before it, nothing reads ECX after that write (a value loaded into ECX and
      pushed is an argument), and the callee pops its arguments or none are passed.
      96 more methods, e.g. `ClientState` methods every caller reaches with
      `mov ecx, g_ClientState` that never read it; a sample of 8 call sites were all
      real `this` calls. `fn_context.py` counts call sites the same way.
    - `ApplyLabels` reset nothing when a generated `method_` row disappeared, so Ghidra
      kept the old name and the ledger counted it. It now resets such placeholders to
      their default names: 4 functions.
  - **Result.** 120 of the 298 are STL template code, now library; 178 are named game
    functions. Code functions named or library-tagged: 76.2% → **82.0%** of 6,619 (80.7%
    from the rows, the rest from the methods test). Converges in two passes.
  - **Cost.** 920k agent tokens (namer 492k, reviewer 428k), 207 tool calls, 45 minutes:
    3.1k tokens a function.
  - **The namer's notes on what slowed it down:**
    - Ghidra prints `UIWindow` subclass fields as `param_1[1].field_0x..`, counted from
      the 0x4c-byte base, so two offsets had to be redone from the disassembly.
    - The window-manager key handler dispatches through a jump table the tools don't
      resolve.
    - `fn_context.py`'s 12 reference lines fill up with unwind funclets for common
      destructors; byte-identical copies of named bodies (four `std::string::~string`)
      are not flagged; tree instances are told apart only by their nil-node global.
  - **Class proposals from the run, not yet decided:** `ItemRecord` (68 bytes, its
    icon path built from "\\item\\%s.bmp"), `SkillRecord` (44), `GuildMemberRecord` (92),
    `GuildPositionRecord` (0x24), `MessengerRecord` (52), `SelectCharSlot` (0xec),
    `ChatRoomInfo`, `WndInfo` (the window records; the name is in the registry
    strings) and `EdgeRect` (48, weaker).

- [x] Naming context, from the notes of naming runs 2 and 3:
  - **`fn_context.py`:**
    - Each data address a function uses shows how many other functions use it and up
      to four named ones, game classes before `std::`. `--data ADDRESS` lists every
      use, the base of indexed operands included. Run 2's namer had written its own
      helper for this.
    - Unwind funclets are summarised on one line by the functions whose cleanup they
      are and no longer use up `--callers`.
    - Functions with the same code are listed (same instructions; calls and outside
      jumps at the same targets, inside jumps at the same offsets), named ones first.
      In run 3's batch 74 functions have a copy, e.g. the five `std::string::~string`
      bodies and the window-record validators that share code.
    - The nil node of each `std::Tree` instantiation is recognised: after
      `operator new(node size)`, under the STL lock, one `.data` dword is stored from a
      register and another (the user count) is stepped. 24 instantiations, with node
      sizes, created in `InitHead` or in constructors that inline it (`g_intMapNil`,
      `UIChatRoomWnd`, `UIGuildMemberManageWnd`, ...).
  - **`decompile.sh`** marks `p[k].field_0xNN` accesses with their offset from `p`
    (`/*+0x90*/`), computed from the struct's size and the field's offset. Run 3's batch
    had 47 such accesses, mostly on `UIWindow` subclasses, which needed the disassembly.
  - **Tests:** `tests/tools/test_context.py` checks the ECX tests, the nil nodes,
    identical bodies and data uses against the binary (23 tests).
  - A 300-function batch's context takes about a minute instead of 10 s and is 16%
    longer.

- [x] Naming run 4, the first with the richer context: the 321 unnamed reachable
  functions of the source files from `0x4b6900` to `0x4d8d30`. They cover 3D nodes and
  model instances, the ground, `CWorld` and its scene graph, IME and client
  configuration, path finding, the view, the guild, the manner filter and the item
  tables. The rows are in `inferred.functions.tsv`.
  - **Quality.**
    - The namer's confidence: 202 high, 108 medium, 11 low.
    - The reviewer accepted 301 rows, revised 20 and rejected none:
      - nine had words with nothing behind them in the binary. "Account id" was a
        gloss on the XML element "aid"; item fields called rank, owner and kind are
        now `FormatItemByte21Message`, `FormatItemIdName` and `FormatItemByte20Message`;
      - five described behaviour wrongly. Two frustum classifiers had their results
        swapped; `GetLicenceText` doesn't rewrite line breaks; and two `CView` setters
        are `StartShake` and `SetShakeAmplitudes`, after the only code that reads their
        fields;
      - two cited the wrong one of two functions with the same name, and three were
        renamed to follow existing patterns.
    - My checks: every address has one row, every namespace exists, no malformed
      names. Two names repeat existing ones (`IsFloatUintPairLess`,
      `DestroyGuildPosition`); `fn_context.py` shows both are byte-identical copies.
      Eight evenly spaced accepted game rows were correct against the disassembly.
      The domain words have sources: the RTTI names `CSoundMaker`, `CEffectMaker`,
      `CSkill`; the strings "MetalProcessItemList.txt", "ISLIGHTMAP",
      "CardPrefixNameTable.txt" and "admin". "Frustum", "shake" and "translucent"
      describe what the code does.
  - **The new context, per the namer:** the "same code as" lines and the nil-node
    labels settled about 60 duplicated tree, list and vector rows quickly, so most of
    the time went to the game files. Still missing: what ECX is set to at each call
    site (a member at `this+0x14` or the owner), and a reader for strings and floats
    at arbitrary addresses. The namer wrote its own for the second.
  - **`docs/naming.md`'s library table** gained `HeapSiftUp`, `CharTraitsAssign`,
    `DequeIteratorDifference`/`DequeIteratorAdvance` (the manner filter's word store),
    `std::string::GetEmptyCString` and `std::pair`'s destructors.
  - **Result.** 181 of the 321 are STL template code, now library; 140 are named game
    functions. Code functions named or library-tagged: 82.0% → **86.8%** of 6,619.
    Converges in two passes.
  - **Cost.** 926k agent tokens (namer 530k, reviewer 396k), 236 tool calls, 49
    minutes: 2.9k tokens a function, down from run 3's 3.1k on a larger batch.
  - **Class proposals from the run, not yet decided:**
    - `LightmapAtlas` at `C3dGround+0x14` (built only when ISLIGHTMAP is set);
    - `MovePath` at `CGameActor+0x170`, with a constructor that `CGameActor` calls;
    - `ViewFrustum` at `CView+0xf4`, with a constructor and destructor;
    - `SceneRayQuery`, built on the stack for picking;
    - `ItemRecord` (0x44 bytes, also proposed in run 3), `ItemInfo` (0x78, the
      `g_itemInfoById` value) and `CardInfo` (0x30, the `g_cardInfoById` value), each
      with a constructor and destructor.

- [x] Naming run 5: the 293 unnamed reachable functions of the source files from
  `0x4d8d30` to `0x4fcf40`. They cover the skill and item tables, the resource manager
  and the archives, the XML reader, the DES and SEED ciphers and CRC-32 of the archive
  and packet code, the stream and locale internals, and the mini-game engine (`mg::`).
  The rows are in `inferred.functions.tsv`.
  - **Quality.**
    - The namer's confidence: 177 high, 112 medium, 4 low.
    - The reviewer accepted 284 rows, revised 9 and rejected none:
      - one game word without evidence ("skill" in `IsIdNotInFixedList`);
      - six evidence fixes: argument order, CRC-32's non-standard seed `0x12477cdf`,
        the script header's two magic numbers, a call frame's return index;
      - `GetlineTail` became `__thiscall`, like `ReallocateBufferTail`.
    - My checks: every address has one row, every namespace exists. Eight evenly
      spaced accepted rows were correct against the disassembly.
    - The ciphers are named from their constants, as `Md5Context` was: DES from its
      permutation and S-box tables at `0x5e8b00`–`0x5e8c48`, SEED from its key
      constants and SS tables. A few names come from Korean (cp949) strings in the
      binary, decoded by the agents.
  - **New class `XmlElement`** (40 bytes). The run's element functions had gone into
    `XmlDocument`, which gave it two constructors. `XmlDocument`'s constructor builds
    itself as the root with the element constructor, and `ParseElement` allocates each
    element with `operator new(0x28)`. The constructor, destructor, `AddChild`,
    `AppendSibling`, `FindChild` and `FindNextSibling` moved to `XmlElement`; the parser
    stays in `XmlDocument`. `recover_layouts` covers 37 of its 40 bytes.
  - **`docs/naming.md`'s library table** gained the block-list (deque) helpers,
    `std::Tree::CopyNodesFrom`, `std::locale::GetFacetIndex`, the facet's
    `DecrementRefCount`/`RegisterStatic`, and `basic_istream`'s `InputPrefix` and
    `GetlineTail`.
  - **Result.** 146 of the 293 are STL and C++ library code, now library; 147 are named
    game functions. Code functions named or library-tagged: 86.8% → **91.2%** of 6,619
    reachable ones, past M1's 90%. Over all 7,115 code functions, the unreachable 496
    included, it is 88.3%. Game code to port: 4,306 of 4,886 functions named, 87.8% of
    its bytes. Converges in three passes: the second resets two `std::string`
    `method_` placeholders that `recover_layouts` no longer proposes.
  - **Cost.** 992k agent tokens (namer 544k, reviewer 448k), 243 tool calls, 42
    minutes: 3.4k tokens a function.
  - **The namer's notes:** a whole-binary search for a field offset or call target
    (it disassembled all of `.text` into a file and grepped it), a reader for bytes,
    dwords, floats and strings at an address, and a shared validator for the
    `names.tsv` columns. `[ecx+n]` lists table addresses used as displacements.
  - **Class proposals from the run, not yet decided:** `DesCipher` and
    `SeedCipherContext` (stack objects in `CGPak` and the packet code),
    `HashedFileName` (0x100 bytes, the key of the resource trees and of `CGPak`'s
    entries; a djb2 hash of the lower-cased name) and `ScriptRecord` (the 0x28-byte
    element of `mg::CScript`'s vector).

- [x] Class proposals from naming runs 3–5, by the same test as before: `__thiscall`
  functions that callers apply to one kind of object, with the size shown in the code.
  For each candidate, the ECX source at every call site was listed (`lea ecx, [node +
  8]` in list nodes, a stack slot, `this + n` of one owner). 17 classes are in
  `inferred.classes.tsv`, with subsystem rules, and 85 functions moved into them; names
  drop the words the class now carries (`BuildItemDisplayName` →
  `ItemRecord::BuildDisplayName`).
  - **Records:**
    - `ItemRecord` (68 bytes): 25 methods. `ClientState` holds three arrays of ten,
      0x2a8 = 10 × 0x44 apart, built by `eh_vector_constructor_iterator`.
    - `SkillRecord` (44), `GuildMemberRecord` (92), `GuildPositionRecord` (36),
      `MessengerRecord` (52) and `StringIntRecord` (20): list, map and vector values
      with destructors, assignments and deleting destructors. Each size is where the
      last field the code uses ends; `SkillRecord::GetIdName` was confirmed by a
      packet handler's stack record, whose string sits at `+0x1c`.
    - `SelectCharSlot` (236): the vector constructor builds three at
      `UISelectCharWnd+0x64` with size 0xec.
    - `ItemInfo` (120) and `CardInfo` (48): the `g_itemInfoById` and `g_cardInfoById`
      values; `recover_objects` now names their static fallbacks `g_ItemInfo` and
      `g_CardInfo`, and the empty guild position `g_GuildPositionRecord`.
    - `ScriptRecord` (40): the element of `mg::CScript`'s vector.
  - **Members:** `LightmapAtlas` (40, `C3dGround+0x14`), `MovePath` (28,
    `CGameActor+0x170`) and `ViewFrustum` (192, `CView+0xf4`). Each spans the gap
    between the owner's neighbouring fields; the frustum's two list heads are the
    `CView` pointers at `+0x1a0` and `+0x1ac`. The owners now have the typed members,
    with no overlapping fields.
  - **Stack objects:** `SceneRayQuery` (40), `EdgeRect` (48, two of them 0x30 apart
    on `UIWindowMgr`'s stack), `DesCipher` (128, the 16 subkeys `CGPak` decrypts
    with) and `HashedFileName` (256, also `CRes+0x10`).
  - **Not accepted:**
    - `SeedCipherContext`: its functions are `__cdecl` and take the context as an
      argument, like `Quaternion`.
    - `WndInfo`: each reset/validate pair serves one window record, and the records
      differ in size (16 or 20 bytes), so no single class fits; they stay in `Global`.
    - `ChatRoomInfo`, the banished-member record and the int/string record: one
      function each.
  - Fields 10,334 → 10,460. Coverage is low where the code indexes arrays
    (`ViewFrustum`'s planes 15%, `DesCipher`'s subkeys 6%, `HashedFileName`'s name
    2%). Still 91.2% named or library-tagged; this step names nothing new. Converges
    in three passes.

- [x] Naming run 6, to close the gap: the 580 unnamed reachable functions left, from
  `0x4a5120` to the end of the game code. 99% needed 514 of them, so the round took all
  580, split at a source-file boundary into two halves (300 and 280). Each half had its
  own naming and reviewing agent, with the same prompts, and the two halves ran in
  parallel. The halves cover game modes, actors, effects and the ~110 effect-primitive
  update and render cases; then the world, `ClientState` and the main loop.
  - **Quality.**
    - The namers' confidence: 321 high, 233 medium, 26 low.
    - The reviewers accepted 561 rows, revised 19 and rejected none:
      - seven had game words the binary doesn't show ("attack", "skill", "ground",
        "hit", "action", "layer", "dir"); they now have shape names
        (`ProcessIdActionOnTarget`, `PackCoordinatesAndNibble`, ...);
      - five were functions that never read ECX but whose callers pass a class
        object in it; they moved into `CEffectPrim` or `ClientState` as `__thiscall`;
      - the rest fixed argument order, a `ret` size and a table.
    - The reviewers decoded `CEffectPrim::vf01`'s and `vf03`'s jump tables (`0x57e06c`,
      `0x586268`) and confirmed every case number in the `UpdatePrim_<id>` rows; I
      re-checked cases 7, 23 and 32. Those placeholders follow `HandlePacket_<id>`,
      named after the switch's "Illegal Effect PrimitiveId %d" string.
    - My checks: every address has one row, every namespace exists, and no name clashes
      with an existing one. Two names repeat within the run on byte-identical copies
      (`DestroyFourStrings`, `StringLess`). A third (`SetVector3`) was on different
      code: the copy that returns `this` is now `InitVector3`. One review line had its
      columns scrambled at `|` characters; I rebuilt it from its text. Eight evenly
      spaced accepted game rows were correct against the disassembly.
  - **`docs/naming.md`** gained the `stable_sort` internals, `UpdatePrim_<id>`-style
    placeholders for switch cases, `...Tail` for jump-only fragments, and the
    convention of empty functions that constructors and destructors call with ECX
    still `this` (13 `NoOp`s, like `CConnection::OnConnect`).
  - **Result.** 285 of the 580 are library code; 295 are named game functions. Code
    functions named or library-tagged: 91.2% → **100%** of 6,619 reachable ones. Over
    all 7,115 code functions it is 96.4%; what remains raw is unreachable code (left
    out on purpose) and library code. Fields 10,460 → 10,578. Converges in two passes.
  - **Cost.** 1.75M agent tokens (namers 538k and 535k, reviewers 362k and 310k), 392
    tool calls, 30 minutes of wall time with the halves in parallel: 3.0k tokens a
    function.
  - **The namers' notes:** a disassembler option that lists jump-table cases (they
    decoded three tables by hand); decoded CP949 strings in `disasm.py`; a layout map
    for `CEffectPrim`, whose fields reach past `0x15000`; and a check, before an
    agent's rows are merged, for the `review.tsv` columns.
  - **Class proposals from the run, not yet decided:** `FourStringRecord` (64 bytes),
    `ConnectionEntry` (0x48, the `connection` entries of clientinfo.xml),
    `StringIntsRecord` (0x1c), `IntStringRecord` (0x14), `EffectPrimRing` (0xb8, four
    at `CEffectPrim+0x156b0`), `EffectPrimHistoryEntry` (0x8c, fifty at
    `CEffectPrim+0x358`) and `ProhibitionLogRecord` (0x74, written to
    "\ProhibitionLog.gd").

- [x] Class proposals from naming run 6, by the same test. Two classes are in
  `inferred.classes.tsv`, with subsystem rules, and 5 functions moved into them:
  - `FourStringRecord` (64 bytes): four `std::string`s. A constructor, a copy
    constructor and a destructor, applied on `CGameMode`'s stack in `SendMsg` and
    `method_005149c0` and to the global `0x689020`, which `recover_objects` now names
    `g_FourStringRecord`. It is the value of `CGameMode`'s tree at `+0x134`, whose
    68-byte values are an int key and this record.
  - `ConnectionEntry` (72 bytes): the entries `CLoginMode::LoadConnectionList` reads
    from clientinfo.xml's "connection" elements ("display", "desc") into the vector at
    `CLoginMode+0x1354`. Four strings and the ints at `+0x40`/`+0x44`; its assignment
    and destructor run in the vector's `insert`. The destructor has the same code as
    `FourStringRecord`'s, since it destroys the same four strings.
  - **Not accepted:**
    - `StringIntsRecord`, `IntStringRecord`: one method each. Their "constructors"
      are `std::ConstructAt` copies (`__cdecl` library helpers), and the int/string
      destructor in the vector code is `std::pair::~pair`.
    - `EffectPrimRing` and `EffectPrimHistoryEntry`: arrays inside `CEffectPrim`
      that only `CEffectPrim`'s own methods touch; no function takes one as `this`.
    - `ProhibitionLogRecord`: no function of its own besides the library's vector
      code.
    - `TimedTextEntry` and the counter record: one weak function each.
  - Fields 10,578 → 10,604. Still 100% named or library-tagged. Converges in three
    passes.

- [x] M1 closed, item by item against section 5:
  - **Static runtime-code check:** done (`tools/runtime_code_check.py`).
  - **Emulation oracle:** done (`tools/emu/`; string, path, CRT, file and DirectX
    tests in `tests/emu/test_oracle.py`).
  - **Library code, RTTI classes, hierarchy and vtables:** done (`ApplyRtti`, the
    FID and CRT labels, `ledger.py classify`).
  - **Constructors, destructors and sizes for every class.** The new
    `tools/class_graph.py` joins the recovered data per class
    (`analysis/exports/Ragexe.exe.class_graph.{tsv,dot}`), the class graph the exit
    asks for. Of the 297 RTTI classes:
    - all 245 game and CRT classes have a size and a destructor, and all but
      `type_info` (static data) have a constructor: out of line, or for 16 classes
      inlined where they are created (`C3dGround15` in `CWorld::LoadWorld`, five
      windows in `UIWindowMgr::method_004ad670`, ...);
    - of the 52 `std` classes, the five `*_base` tags have no functions at all, and
      `std::ios_base` is a virtual base its streams construct.

    `tests/tools/test_class_graph.py` holds these numbers. The 64 curated classes all
    have sizes; those without a constructor or destructor are plain records with no
    vtable.
  - **Every function in a subsystem.** Done for code (`tools/assign_subsystems.py`), but
    568 ledger rows still had none: the 541 exception funclets of library functions and
    27 import thunks. Funclets now take their library parent's `crt` or `stl`. A thunk
    takes its DLL's import rule; failing that, the subsystem its callers share
    (`connect`, `socket` → `net`, `DirectInputCreateA` → `core`); a thunk to a thunk
    follows it. Every one of the 10,813 rows now has a subsystem.
  - **`docs/architecture.md`:** the main loop, singletons, frame timing and threads
    (§2–§9). The subsystem table is refreshed (STL code moved to library since), and
    §11 describes the class graph.
  - **Exit criteria:**
    - 100% of the 6,619 reachable code functions are named or library-tagged (96.4% of
      all 7,115);
    - the class graph is exported;
    - the ledger has 10,813 rows, each with its kind, library flag and subsystem, and the
      reach of every unreachable one.
  - **What "named" covers.** The figure counts placeholders, as it has since M0. Of the
    4,601 reachable game functions:

    | Kind | Functions | Bytes |
    |------|----------:|------:|
    | descriptive names | 1,771 (38.5%) | 28.0% |
    | `vfNN` (virtual, class known) | 1,102 (24.0%) | 32.0% |
    | `method_<address>` (class known) | 1,042 (22.6%) | 31.7% |
    | `HandlePacket_<id>`, `UpdatePrim_<id>` | 296 (6.4%) | 7.6% |
    | initializer helpers | 390 (8.5%) | 0.7% |

    Every function has its class and subsystem, but the purpose of the `vfNN` and
    `method_` functions, 64% of the game's bytes, is still to be read. That reading
    belongs with porting each class.

### M2: done
- [x] Build: CMake (3.25+), C++20 and Ninja. Presets `dev` (Debug with
  AddressSanitizer and UndefinedBehaviorSanitizer; AddressSanitizer only with MSVC) and
  `release`. How to build: `docs/building.md`.
  - **Libraries.** `cmake/dependencies.cmake` pins each by release archive and SHA-256:
    SDL3 3.4.18 and Catch2 3.16.1. CMake uses an installed package when `find_package`
    finds one (Homebrew's SDL3 here), and otherwise `FetchContent` downloads and builds
    the pinned release, SDL3 as a static library. The other libraries of section 5
    are added by the milestones that use them.
  - **vcpkg, tried first and dropped.** The first version used a vcpkg manifest. Both
    CI attempts on Linux and Windows failed in vcpkg's own setup, never in our code.
    On Linux, SDL3's default features made vcpkg build dbus and libxcrypt, which needed
    more system tools. On Windows, getting vcpkg itself failed both times. For six libraries,
    three of them single headers, pinned archives are simpler: no extra tool, no
    bootstrap, no baseline.
- [x] Code in `src/`, one static library per layer:
  - `core`: `FrameTimer`, the original's fixed step (`ResetFrameTimer`, `SetFrameRate`,
    `GetFrameStepsDue`): a period of `1000 / fps` whole milliseconds; each frame runs
    the whole periods elapsed and advances the base by them. With catch-up off (the flag
    at `0x61ec58`) it returns 1 but still advances the base, as the original does.
    Times are wrapping 32-bit milliseconds, like `timeGetTime`.
  - `platform`: SDL start-up, the wrapping millisecond clock, the user data directory,
    events (quit, focus, keys, text), a window with a clear-and-present stand-in until
    the SDL_GPU renderer, and the video settings. These were registry values
    (`ISFULLSCREENMODE`, `WIDTH`, `HEIGHT`, `BITPERPIXEL`) and are now a
    `settings.txt` of `KEY=value` lines.
  - `game`: `Mode` (`CMode`: `OnInit`, `Run`, `OnExit`, `OnFrame`, `SendMsg`,
    `OnChangeState`, with the pending state applied at the top of each `Run` iteration)
    and `ModeSwitcher` (`Run`, `RequestMode`, `Quit`). Stubbed `LoginMode` and
    `GameMode` keep the original's frame order: events, then the world steps due, then
    drawing. `GameMode::OnInit` resets the timer, as `CGameMode::OnInit` does. A
    `Client` object holds what the original keeps in globals.
  - `app/main.cpp`: settings, platform, window, `SetFrameRate(60)`, then
    `ModeSwitcher::Run(Login, "login.rsw")`. Not ported: the `1rag1` command-line
    check, the instance mutex and its name checksum, and the self-update.
- [x] Tests (`tests/unit`, Catch2, run by `ctest`): the frame timer (16 ms period,
  carried remainder, catch-up, 62.5 steps a second over a minute, the 32-bit wrap,
  catch-up off), the mode switcher (login → game → map change → quit, with each
  slot's order, pending states, quit before run) and the settings file. A smoke test
  runs the client itself on SDL's dummy video driver: login, the move to the game mode
  on `prontera.rsw`, quit after 180 frames. All 15 pass under the sanitizers and in the
  release build.
- [x] On macOS arm64 the client opens its window and runs the mode loop: on a 120 Hz
  display the game mode drew 59 frames in 0.49 s and ran 29 world steps, 62.5 a second.
- [x] CI (`.github/workflows/ci.yml`, on https://github.com/keyehzy/ragexe):
  - macOS arm64 (`macos-14`), Linux x86-64 (`ubuntu-24.04`) and Windows x64
    (`windows-2022`). Each job configures the `dev` preset with
    `FETCHCONTENT_TRY_FIND_PACKAGE_MODE=NEVER`, so every platform builds the pinned
    libraries, then builds and runs `ctest`. Linux first installs the X11, Wayland,
    input and D-Bus headers SDL3 builds against.
  - Run 40285434608, the first with FetchContent, passed on all three in under two
    minutes each. All 15 tests passed on each platform, under AddressSanitizer
    (and UndefinedBehaviorSanitizer on macOS and Linux), including the client's smoke
    run from login to game mode.
- [x] **M2 exit.** The client runs its mode loop on all three platforms: in CI with
  SDL's dummy video driver, which creates its window off screen, and on macOS also
  with a real window on the display. Linux and Windows have not shown a window on a
  real display yet; the first time someone runs it there is the remaining check.

### M3: done
- [x] GRF archive reader. The format, from `CGPak` (`004dd820`–`004decd0`), `DesCipher`
  and `HashedFileName`, is in `docs/formats/grf.md`:
  - **Header:** `Master of Magic`, versions 0x100 to 0x103.
  - **Table:** encoded name records.
  - **Per-file decoding:** a single DES round on the first 20 blocks and every cycle-th
    block, plus a byte shuffle on others; `.gnd`, `.gat`, `.act` and `.str` get their
    first 20 blocks only.
  - **Findings:**
    - `DesCipher::SetKey` ANDs the key bits into zeroed registers, so every subkey is
      zero and the per-name keys have no effect. The emulator shows it on a nonzero key.
    - Ragexe opens only `data.grf`; nothing constructs `CSakGPak`.
    - The archive always wins over a loose file.
  - **Code:** `src/res` (`ragexe_res`): `GrfArchive`, the cipher in `grf_cipher`, and
    `HashedFileName`.
- [x] `cps.dll` is zlib 1.0.4, by its own copyright strings, exporting `compress` and
  `uncompress`. It is replaced by zlib 1.3.2, pinned in `cmake/dependencies.cmake`.
  One entry, `data\wav\worm_tail_damage.wav` (in `data.grf` and two patches), has a
  cut-off stream. The client ignores `uncompress`'s result and keeps the zeroed buffer,
  and the reader does the same.
- [x] `ragres list | extract | check` (`src/ragres`). Names with non-ASCII bytes are
  written as `%XX` until the CP949 table exists.
  - **`ragres check`** on `data.grf` and the nine `.gpf` patches: version 0x103 and
    0x102; 20,508 entries, of which 19,879 are files totalling 2.56 GB. All decode and
    inflate to their recorded sizes (one damaged, as above). The other 629 entries are
    directory entries (flags 0). There are no duplicate names.
  - **`grf_check_original`** runs this as a `ctest` test when `original/install` is
    present.
- [x] Tests: the cipher against outputs of the original's `DecryptBlocksMixed`,
  `DecryptFirstBlocks` and `HashedFileName::Set` in the emulation oracle
  (`tools/grf_vectors.py` → `tests/unit/grf_cipher_vectors.inc`); a synthetic archive
  written to the spec and read back (both decode paths, flags 0, CP949 names,
  case-insensitive lookup); the broken stream; refused archives; version 0x100 tables.
- [x] Labels: the `CGPak` and `CSakGPak` methods are named (`Open`, `LoadFile`,
  `LoadFromArchive`, `MapRange`, …).
- [x] Tooling:
  - **ccache** for every compile when installed, with MSVC's debug information in the
    objects; CI keeps a ccache per platform.
  - **clang-format** (Google style, 100 columns) and **clang-tidy** (bug-finding,
    performance and a few modernize checks, all errors), pinned in
    `tools/requirements-lint.txt`.
  - **In CI:** the format check is its own job; clang-tidy runs in the Linux job.
- [x] Lookup layer and resource cache, specified in `docs/formats/resources.md`:
  - **Lookup:**
    - `MakeDataPath`, checked against the original in the emulation oracle
      (`tools/resource_vectors.py`).
    - `DataFiles`: the archive, then loose files under the install directory, matched
      without regard to A–Z case on every system, with `..` contained.
    - `DataFile`: all-or-nothing reads, and seeks clamped to the end, as the original.
  - **Resources:**
    - `Resource` (`CRes`: reference count, last use, type, hashed path).
    - `ResourceManager`: types by extension with a folder and a prototype to clone;
      `Get` loads once per path, falls back to `ResNameTable.txt`, and reports failures
      with the lower-cased name as `OnLoadError` does. The 17 registrations of the
      original are tabulated for the loaders to use.
    - Unloading: after 15 s unused, unused, unused of one type, one, all.
    - A recursive lock, so a resource's `Load` can `Get` others, as the original's
      critical section allows.
  - **Tests:** Catch2 tests for each piece, and a `res_name_table_fuzz` target.
- [ ] Loaders in dependency order, each compared with the original in the emulation
  oracle.
- [x] Fuzzing the GRF reader (`fuzz/`, `docs/building.md`):
  - **Targets:** libFuzzer targets for `ParseGrfTable` and for a whole archive from
    memory (header, table, lookups, duplicate report, reads), with seeds from
    `make_grf_seeds` and a dictionary.
  - **Run locally** (Homebrew LLVM 22, AddressSanitizer and UndefinedBehaviorSanitizer):
    15 minutes per target, about 22 million inputs each. No crashes and no broken
    invariants; the archive corpus covers every reachable line of the reader and the
    cipher.
  - **In every build,** `ctest` replays the seeds and the saved regressions, and CI
    fuzzes each target for 60 seconds.
  - **One change came out of it:** `Read` now refuses a recorded size more than 1032 times
    the stored size, instead of allocating gigabytes for a corrupt table.
  - Later parsers get their targets as they are written.

- [x] `CBitmapRes` (`docs/formats/bitmap.md`, `src/res/bitmap`):
  - **Formats:** BMP (8- and 24-bit), TGA (types 2 and 10) and JPEG, registered for
    `bmp`, `tga` and `jpg` under `texture\`. JPEG goes through stb_image (v2.30,
    pinned) in place of `ijl15.dll`.
  - **Quirks kept:** the magenta key of JPEGs, the alpha flag of TGAs, run-length
    packets that run past their row, and `ApplyAlphaBitmap`'s odd average.
  - **Bugs not kept:** where the original reads or writes out of bounds, the port stays
    in bounds.
  - **Oracle** (`tools/bitmap_oracle.py`): every BMP and TGA in `data.grf` is
    pixel-identical to the original decoders run in the emulator (5,587 images), and
    the 3 four-bit BMPs are refused by both. JPEGs are within 3 levels of Pillow.
    `ctest` runs a sample of 30 of each when the data is present.
  - **Fuzzing:** `bitmap_fuzz` ran 15 minutes (about 238,000 inputs, slow because
    stb_image is instrumented too) with no crash and no broken invariant. UBSan found
    signed overflow in stb_image's IDCT on corrupt coefficients, which stb then clamps;
    stb_image is now compiled with `-fwrapv`, which makes that overflow defined.
  - **Supporting changes:** a `bitmap_fuzz` target, `ragres decode-images`, and
    `ragres extract` with `*.ext` filters. The emulator gained `heap_mark` and
    `heap_release` for batch runs, and a `MessageBoxA` stub.
  - **Labels:** the `CBitmapRes` and `CRes` slots are named. `recover_objects.py` now
    also finds deleting destructors that test their flag in a register, which renames 4
    functions that had been taken for constructors (`CRes`, `CDC`, `CController`,
    `std::locale::facet`).

- [x] `CPaletteRes` (`docs/formats/palette.md`, `src/res/palette`):
  - **Format:** 256 entries of r, g, b and an unused byte, converted by
    `C3dDevice::ConvertColorsToArgb1555` into ARGB1555 with the alpha bit set. Magenta
    becomes the constant key `0x0421`: transparent, the 16-bit form of the JPEG key.
  - **Oracle** (`tools/palette_oracle.py`, also run by `ctest` when the data is
    present): the original conversion run in the emulator agrees on every value of all
    502 `.pal` files in `data.grf`, and of 24 synthetic palettes covering every colour
    near the key.
  - **Not fuzzed:** a palette is a fixed 1024 bytes with nothing to parse; `CSprRes`
    will reuse the conversion.
  - **Where the port differs:** a file under 1024 bytes, which the original would
    convert from stack garbage, fails in the port.
  - **Also:** `ragres decode-palettes`, and labels for the class's slots.

- [x] `CSprRes` (`docs/formats/sprite.md`, `src/res/sprite`):
  - **Format:** an `SP` header (versions to 0x201), 8-bit images raw or zero-run
    encoded, 32-bit images (A, B, G, R, bottom-up), and the palette in the last 1024
    bytes.
  - **Kept from the original:** 8-bit images stay run-length encoded, as the client
    keeps them, with its `EncodeRle`, `DecodeRle` and `ShrinkImage`, including the
    encoder's 16-bit quirks. The detail level that halves images is a registration
    option.
  - **Oracle** (`tools/sprite_oracle.py`):
    - **The stand-in archive:** the emulator now has one (`serve_data_files`), so the
      original `Load` runs on the file through `DataFile`, with `CTexture` replaced to
      capture the 32-bit images.
    - **Result:** all 2,086 sprites at all four detail levels match: 8,344 loads,
      103,536 8-bit images (record and every pixel, decoded by the original
      `DecodeRle`), 368 32-bit images and every palette.
    - **Cost:** one core takes 25 minutes, so `ctest` runs 20 sprites.
  - **Fuzzing:** `sprite_fuzz` found a zero-width 32-bit image that indexed an empty
    vector, now fixed and kept as a regression. Images that claim more pixels than
    their run-length data can cover are now refused, which also took the fuzzer from
    43 to 15,000 inputs a second. Then 9.3 million inputs ran with no further findings.

- [x] `CActRes` (`docs/formats/act.md`, `src/res/act`):
  - **Format:** actions of frames of items and anchors, versions to 0x205, with event
    names and delays.
  - **Item adjustment:** items of versions 0x200 to 0x204 move from their centre to
    their top-left corner, using the matching sprite's image sizes. That makes them
    depend on the sprite's detail level. The port fetches the sprite through the
    resource manager as the original does.
  - **Field names** come from their consumers (`SceneRenderer::DrawActFrame`,
    `CRenderObject::DrawBody`, `CPc::RenderActFrameItems`).
  - **Oracle** (`tools/act_oracle.py`):
    - **Setup:** the original resource manager runs in the emulator with the sprite type
      registered, so `Load` fetches the real `.spr`.
    - **Result:** all 2,109 actions match at all four detail levels: 8,436 loads and
      3,395,340 items, plus every frame, anchor, event and delay.
    - **Faults:** the 7 actions with no sprite fault in the original (28 loads) and fail
      in the port.
    - **What it found:** anchors keep the last 12 of their 16 bytes, not the first 12.
      `__ftol`'s 64-bit `fistp` turns the NaN of a 0 scale into 0 (2 items).
  - **Fuzzing:** `act_fuzz`, 5.2 million inputs with no findings.
  - **Oracle runs:** `tools/oracle_pool.py` spreads them over cores with progress. The
    full ACT run takes 6.5 minutes, and the sprite oracle uses the pool too.

- [x] `C3dAttr` (`docs/formats/gat.md`, `src/res/gat`):
  - **Format:** `GRAT` 1.0 to 1.2, then width × height cells of four corner heights and
    an attribute; the cell size is 5.
  - **Kept from the original:** an unsupported version still succeeds, with an empty
    map.
  - **Oracle** (`tools/gat_oracle.py`, in `ctest` when the data is present): all 208
    maps in `data.grf` match the original `Load`, cell for cell, in 90 s.
  - **Fuzzing:** `gat_fuzz`, 285 million inputs with no findings.
  - **Queries:** `GetHeight` and the other queries are documented for M5, which ports
    them with the walking code.

- [x] `CGndRes` (`docs/formats/gnd.md`, `src/res/gnd`):
  - **Format:** `GRGN` with a byte minor version, then texture names, 8 × 8 lightmaps,
    40-byte surfaces and the cells. Cells are 0x18 bytes below minor 7 and 0x1c at 7;
    `GetCell` widens both to the same form.
  - **The original's loader** keeps the file open and points into it; it checks no
    sizes and never the major version.
  - **Field meanings** come from the builder, `C3dGround15::vf03`, and from
    `LightmapAtlas::Build`. The lightmap index is unsigned: `gl_cas01.gnd` has 37,365
    lightmaps.
  - **Oracle** (`tools/gnd_oracle.py`, in `ctest` when the data is present):
    - all 208 grounds in `data.grf` (all version 1.7) match the original `Load` in every
      name, lightmap, surface and cell;
    - so do 12 synthetic ones, covering every minor version and the failing minor 8.
  - **Where the port differs:**
    - an unversioned file fails: the client would read its cells through an
      uninitialised version byte;
    - so do truncated files.
  - **Fuzzing:** `gnd_fuzz`, 315 million inputs with no findings.
  - **`ragres` reads files whole.** Its byte-by-byte reads took 2 minutes on the 828 MB
    of grounds in the sanitized build.

- [x] `C3dWorldRes` (`docs/formats/rsw.md`, `src/res/rsw`):
  - **Format:** `GRSW` to 2.1:
    - the ground and attribute map names, the water and the light;
    - objects: models, skipped type-2 records, sounds and effects;
    - at 2.1, the bounds of a 1,365-node scene graph.
  - **Field meanings** come from `CWorld::LoadWorld` and `C3dGround::SetWaterParams`.
    The light colours are the diffuse and ambient of the `D3DLIGHT7` passed to
    `IDirect3DDevice7::SetLight`, and model placement comes from `ModelInstance`.
  - **Kept from the original:**
    - the light vector, computed through the original's rotation helpers, bit for bit;
    - the ISLIGHTMAP option's brighter ambient;
    - models below 1.3 named by the loader's leftover name buffer;
    - the speed clamp that turns NaN into 1.0, found by the oracle;
    - 1.x sound records converted with `strncpy`.
  - **Oracle** (`tools/rsw_oracle.py`, in `ctest` when the data is present):
    - all 208 worlds in `data.grf` match the original `Load` with ISLIGHTMAP on and off;
    - so do 19 synthetic ones, covering every version, odd objects and extreme light
      angles;
    - it runs in 20 s.
  - **Where the port differs:** truncated files fail, and so do unterminated names.
  - **Fuzzing:** `rsw_fuzz`, 78 million inputs with no findings.

- [x] `C3dModelRes` (`docs/formats/rsm.md`, `src/res/rsm`):
  - **Format:** `GRSM` 1.0 to 1.5: textures, then a tree of nodes, each with names,
    textures, a matrix, placement, a mesh and keys; then volumes.
  - **The loader does more than read:**
    - it bakes each node's matrix into its vertices;
    - it sorts each mesh's faces by texture with the client's `std::sort`;
    - it computes face normals and per-smoothing-group corner normals.

    The port does all three as the original does:
    - the sort is not stable;
    - its copies leave bytes 0xe to 0x13 of each face in place;
    - degenerate faces get the x87's negative NaN.
  - **Field meanings** come from `C3dNode::vf01` and `ModelInstance`: position, rotation
    axis and angle, scale, alpha and animation length.
  - **Oracle** (`tools/rsm_oracle.py`, in `ctest` when the data is present):
    - all 1,127 models in `data.grf` (all 1.4) match the original `Load`, bit for bit,
      in every vertex, face and normal;
    - so do 13 synthetic models of every version;
    - it runs in 30 s;
    - a stable sort in place of the client's makes 879 models differ.
  - **Where the port differs:** truncated files, bad indices and missing parents or
    roots fail, where the original reads through bad pointers.
  - **Fuzzing:** `rsm_fuzz`, 82 million inputs with no findings.

- [x] `C3dAnimModelRes` (`docs/formats/rsx.md`, `src/res/rsx`):
  - **Format:** a version and header values, then texture names, the root name, and
    nodes fenced by `0x12345678`. Each node carries:
    - its vertices for every frame;
    - texture coordinates per frame, or one shared set;
    - its faces, in the `.rsm` layout.
  - **The original checks neither the magic nor the version.** The port does not either.
  - **One file:** `data.grf` has a single `.rsx`, so 8 synthetic files carry most of the
    oracle (`tools/rsx_oracle.py`, in `ctest` when the data is present). It covers every
    version, both texture-coordinate modes, zero frames and a bad marker, and all of them
    match the original `Load`.
  - **Shared reader:** the loaders that read field by field (`.rsw`, `.rsm`, `.rsx`) now
    share `RecordReader`.
  - **Fuzzing:** `rsx_fuzz`, 183 million inputs with no findings.

- [x] `CImfRes` (`docs/formats/imf.md`, `src/res/imf`):
  - **Format:** a float version (at least 1.01), then layers of actions of frames, each
    frame holding an order, x and y.
  - **Field meanings:** `CPc::RenderActFrameItems` asks `FindEntryWithValue` which layer
    goes first in each action and frame, and `GetRecordPoint` gives x and y as a point.
  - **The original's tables:** it keeps counts in fixed tables of 15 layers by 104
    actions without checking them. The port refuses files that would overflow them.
  - **Oracle** (`tools/imf_oracle.py`, in `ctest` when the data is present): all 31
    files in `data.grf` and 8 synthetic ones match the original `Load`.
  - **Fuzzing:** `imf_fuzz`, 279 million inputs with no findings.

- [x] `CRmaRes` (`docs/formats/rma.md`, `src/res/rma`):
  - **Format:** text, read line by line through the client's `CTokenizer`. The lines are
    `#rsxname`, `#action` (numbered records tied to the last name) and the interior and
    shadow settings.
  - **No data:** no `.rma` ships, and no getter has a caller. The client names one file,
    `캐릭터\칼질몬스터.rma`, and since `Load` never checks the open, it loads empty and
    succeeds. The port does the same.
  - **The tokenizer** is ported quirk for quirk:
    - `//` drops the token it cuts;
    - a quote drops what came before it;
    - a space ends a token only when it is not the line's first character;
    - lines go into a 1023-character `getline` buffer, and the text stops at the first
      NUL.
  - **Oracle** (`tools/rma_oracle.py`, in `ctest`): 14 synthetic files match the original
    `Load`, including the count of messages it shows. It loads one file before marking
    the heap, because the first `istream` read builds the C locale's `ctype` facet there.
  - **Where the port differs:** lines with too few tokens fail, where the original reads
    past its token vector, and so do action numbers outside 0 to 65535.
  - **Fuzzing:** `rma_fuzz`: its first run stopped on a wrong check of its own (an action number never given keeps name index 0 even with no names), kept as a regression; then 9.6 million inputs with no findings.

- [x] `CEZeffectRes` (`docs/formats/str.md`, `src/res/str`):
  - **Format:** `STRM` version `0x94`, then layers of 128-byte texture names and 0x7c-byte
    keyframes.
  - **The original's loader** keeps the file open and points into it, as `CGndRes`
    does. It points unused texture slots at "1.bmp", and checks neither its 60-layer
    array nor its 110 slots per layer.
  - **The player** is `CRagEffect`: type-0 keys set the values and the others add to
    them. The meanings of the 29 values come with the effect player.
  - **Oracle** (`tools/str_oracle.py`, in `ctest` when the data is present): all 137
    effects in `data.grf` and 6 synthetic ones match the original `Load`, names read
    through its pointers.
  - **Fuzzing:** `str_fuzz`, 123 million inputs with no findings.

- [x] `CEmblemRes` (`docs/formats/ebm.md`, `src/res/ebm`):
  - **Format:** a zlib stream of a BMP, written to `_tmpEmblem` from what the server
    sends (`SaveEmblemFile`, `0059c800`). None ships in `data.grf`.
  - **The original's loader** reads the file into an 8 KB stack buffer without checking
    its size, then inflates it into another with `cps.dll`.
    - **A length left at 8 KB** (an error, or an image that fills the buffer) or a BMP
      that fails gives a white 24 × 24 emblem.
    - **A decoded BMP** has its width and height clamped to 24, without moving its pixels.
  - **`cps.dll` is zlib 1.0.4,** and the port follows what it accepts:
    - a distance before the start of the output copies zeros, through a zeroed preset
      dictionary;
    - fixed-code length symbols 286 and 287, on which 1.0.4 copies without end (the load
      fails through `Uncompress`'s exception handler), fail.
  - **The emulator** gained `load_cps`, which maps the original `cps.dll` and sends the
    client's imports to it, and a `lstrcmpiA` stub.
  - **Oracle** (`tools/ebm_oracle.py`, in `ctest`): 31 synthetic emblems and 400 mutated
    streams match the original `Load`, bitmap and inflated bytes compared separately.
    - **Where 1.0.4 faults or runs away,** the port must not decode an image: at 3,000
      mutations it fails 40 of 41 and gives the blank emblem for 1.
    - **What the original builds from bytes nobody set** is compared only as far as the
      inflated bytes.
  - **Fuzzing:** `ebm_fuzz`, 66 million inputs with no findings.

- [x] `CWave` (`docs/formats/wav.md`, `src/res/wav`):
  - **The loader** keeps the file as it is, unless Miles' `AIL_file_type` says IMA
    ADPCM, which `AIL_decompress_ADPCM` turns into a 16-bit PCM wave, or MPEG Layer III,
    which goes to the ASI codec.
  - **`Mss32.dll` 6.1a** was read for what the client calls:
    - `AIL_file_type`: wave tags, then signatures, then an MPEG frame-sync scan of the
      first 4 KB;
    - `AIL_WAV_info`: a chunk walk that checks only `WAVE` and is bounded by the RIFF
      size;
    - the assembly IMA decoders, whose tables are the standard ones entry for entry.
  - **MPEG Layer III** is classified but left to the sound layer; none ships.
  - **The emulator** gained `load_mss`, which maps the original `Mss32.dll`. Its code
    for these calls needs no stubs.
  - **Oracle** (`tools/wav_oracle.py`, in `ctest` when the data is present): all 988
    files in `data.grf` and 39 synthetic ones match the original `Load`. That covers
    Miles' answer and the sample byte for byte; the 5 IMA ADPCM files decode
    identically.
    - **Where `Mss32.dll` reads or writes past its buffers, or walks its chunks forever,**
      the port fails, and the oracle checks it on 6 synthetic files.
  - **Fuzzing:** `wav_fuzz`.
    - **Its first run hung** on a chunk size of `0xfffffff8`, which keeps the walk in
      place (the client hangs on it too). That input is now a regression; the port fails
      on it, and CI's fuzz step stops any input after 10 seconds.
    - **Then** 1.2 million inputs with no findings. That is slow, since mutated sample
      counts ask for decoded waves of up to the port's 64 MB limit.

- [x] The item and card tables (`docs/formats/item_tables.md`, `src/res/item_tables`):
  - **Format:** 14 text tables, read by `LoadItemTables` through
    `LoadHashSeparatedStrings`, which splits on `#` and, in line mode, on `\r`.
    - Records are pairs, triples or single ids.
    - The description tables and the metal list go line by line under the last id.
  - **What it fills:** the item, card and metal-processing maps.
    - **Display names** get spaces for underscores, stepping by `CharNextExA` on the
      service's code page.
    - **Descriptions** are stored alphabet-reversed for the Korean service.
    - **Item 0 and card 0,** or the fallbacks, get "Unknown Item" and "sorry".
  - **Oracle** (`tools/item_tables_oracle.py`, in `ctest` when the data is present):
    - **The maps:** it builds them with their static initializers and reads them back
      field by field.
    - **What matches:** `data.grf`'s tables for both services, the three patch sets over
      them, and 16 synthetic sets.
    - **The port fails** where the client overruns its 256- and 1024-byte stack buffers,
      or goes round a table whose last record is cut short. The oracle checks those
      too.
  - **Fuzzing:** `item_tables_fuzz`, 6.2 million inputs with no findings.

- [x] `ClientState`'s name-pair tables (`docs/formats/client_tables.md`,
  `src/res/client_tables`):
  - **What they are:** `mapNameTable`, `mapObjLightTable`, `mp3NameTable` and
    `skillNameTable`, read by one loader, copied four times, into vectors of string
    pairs.
  - **The parser:** two character buffers that `#` switches between, where a line break
    empties only the one being filled. The pairs are then put through the library's
    `std::sort`, which is not stable.
  - **That sort** is now `Vc6Sort` (`src/res/vc6_sort.h`), shared with the model loader;
    `rsm_oracle` still matches.
  - **The skill tables:** the lowercase ones that `LoadHashSeparatedStrings` would read
    belong to `LoadSkillTablesByName` (`004d95b0`), which nothing calls.
  - **Oracle** (`tools/name_pairs_oracle.py`, in `ctest` when the data is present): each
    loader on its tables from `data.grf` and the patches, and 8 synthetic tables (ties,
    line breaks, NULs, high bytes); every pair matches in order.
  - **Fuzzing:** `name_pairs_fuzz`, 8.6 million inputs with no findings.

- [x] `ClientState`'s skill tables (`docs/formats/client_tables.md`):
  - **`LevelUseskillSpAmount` and `skillAfterDelay`** are read by one loader into
    records of a skill id and numbers, closed by `@`.
  - **`skillDescTable`** is read into records of a skill id and lines. Every line break
    after the id ends a line, so CRLF adds an empty one.
  - **Both are sorted** with `Vc6Sort`.
  - **The SP table's skill id index** (`0059d1f0`) is left to the skills.
  - **The oracle** became `tools/client_tables_oracle.py`, covering all seven loaders.
    Their real tables and 23 synthetic ones match.
  - **Fuzzing:** `name_pairs_fuzz` became `client_tables_fuzz`, over all three
    parsers: 3.2 million inputs with no findings.

- [x] `msgStringTable`, `indoorRswTable` and `fogParameterTable`
  (`docs/formats/client_tables.md`):
  - **Their loaders** keep the raw `#`-ended fields:
    - the messages, in order;
    - the indoor maps, sorted;
    - the fog records, a name and four values each, sorted by name.
  - **`client_tables_oracle`** now runs all ten loaders. Their real tables and 41
    synthetic ones match.
  - **Fuzzing:** `client_tables_fuzz` now covers these parsers too: 1.6 million inputs
    with no findings.

- [x] `PetTalkTable.xml` and the XML parser (`docs/formats/xml.md`, `src/res/xml`):
  - **The parser** is the client's own `XmlDocument`:
    - elements by tag name, attributes ignored;
    - text kept byte for byte, minus line breaks, tabs included;
    - three entities;
    - closing tags of other elements skipped.
  - **The pet talk loader** parses into a document inside `ClientState` without checking
    the open.
  - **Oracle** (`tools/xml_oracle.py`, in `ctest` when the data is present): each document
    ends at the end of mapped memory, so a read past it faults.
    - **What matches:** the real XML (pet talk, `clientinfo.xml` and the patches'
      `monsterskillinfo.xml`) and 20 synthetic documents, element for element.
    - **What fails:** the 8 that make the client read past the data, in the port as in
      the client.
  - **Fuzzing:** `xml_fuzz`, 48 million inputs with no findings.

- [x] `clientinfo.xml` (`docs/formats/clientinfo.md`, `src/res/client_info`):
  - **`ReadClientInfoSettings`** reads the service and server type and three flags.
  - **`InitDefaultClientInfo`** fills the admin lists and, for the Korean service, the
    built-in servers.
  - **`LoadConnectionSettings`** applies a connection: address, port, version, the admin
    ids, and its `langtype`, which replaces the service type.
  - **`SelectLanguage`** picks the code page and charset.
  - **The rest of `LoadClientInfo`** sets the executable, patcher, licence and registry
    names.
  - **Oracle** (`tools/clientinfo_oracle.py`, in `ctest` when the data is present):
    - it reads back every global the client sets, the messages it shows, and the login
      screen's connection list;
    - `data.grf`'s file (three ways) and 54 synthetic files match;
    - an `<aid>` with no `<admin>` faults in the client and fails in the port.
  - **Fuzzing:** `clientinfo_fuzz`, 17 million inputs with no findings.

- [x] The tip of the day and `manner.txt` (`docs/formats/tips.md`,
  `docs/formats/manner.md`, `src/res/tips`, `src/res/manner`):
  - **Tips** are read with `fopen` in text mode from beside the client, by service. Each
    `\n` ends a line and each `#` a tip.
  - **The manner words** are each `\n`-ended line of `data\manner.txt`, lower-cased with
    double-byte characters skipped. Leading blanks and trailing spaces are kept.
  - **`IsClean`** looks for each word at each character, two bytes at a time over
    double-byte characters.
  - **Oracle** (`tools/tips_manner_oracle.py`, in `ctest` when the data is present):
    - the client's own C library reads the tips, text mode and all;
    - the words come back from the client's deque, and `IsClean` answers 20 probes;
    - the real files and 15 synthetic ones match.
    - **A finding:** `AssignLowercase` skips the byte after a lead byte, as `IsClean` does.
  - **Fuzzing:** `tips_manner_fuzz`, 41 million inputs with no findings.

- [x] **M3 exit** (`docs/formats/resources.md#every-entry-through-its-loader`):
  - **Every entry parses.** `ragres check-loaders` loads every file of `data.grf` and the
    nine patches through the loader its extension selects, a fresh resource of the
    registered type. The client's tables go through their own loaders.
    - **The patches** go over `data.grf` in date order, as the patcher merges them, so
      a patch's actions find their sprites in `data.grf`.
    - **Results:** of 19,879 files, 19,803 load and 11 fail; 22 are `.gr2` and 43 are
      never opened by the client.
    - **The 11 failures** are listed in `tests/unit/loader_failures.txt`, and
      `loader_check_original` in `ctest` requires exactly those:
      - 8 version 0x204 actions with no sprite anywhere, where the original reads
        through a null pointer;
      - the 3 4-bit bitmaps, which the original refuses too.
    - **Not ported:** `.gr2` models (`C3dGrannyModelRes`, through `granny2.dll`), the
      one registered type outside the M3 list.
    - **Never opened by the client:**
      - extensions with no type, and names no string in the binary holds (`.ezv`,
        `.sfk`, `.fna`, `monsterskillinfo.xml`);
      - tool leftovers;
      - `MapInfoTable.scp`, whose only reader (`005aaa90`) nothing calls.
  - **A sample of each type matches the original** in the emulation oracle: every type
    the archives hold, and `.ebm` and `.rma` on synthetic files, since none ships.
  - **Every parser has run under the fuzzer.** The palette conversion was the last one
    without a target. `palette_fuzz` ran 141 million inputs with no findings.
  - **Port changes:**
    - `ResourceManager::Create` makes the resource `GetResource` would clone;
    - `Resource::error()` is virtual;
    - `DataFiles::set_layers` reads archives under the main one.

### M4: in progress
- [x] The device (`docs/render/device.md`, `src/render/device`, a new `ragexe_render`
  library):
  - **`C3dDevice::Init`:**
    - DirectDraw with `FPUSETUP`, so the x87 runs at single precision;
    - a flip chain full screen, or an off-screen target blitted to the window;
    - a 16-bit z-buffer where the driver has one;
    - the render and texture stage states it sets once.
  - **`CreateRenderer`:**
    - the pixel mode from the primary's red mask;
    - texture limits from the caps;
    - two-stage blending, and mipmaps under `TRILINEARFILTER`;
    - `CalibrateTexelOffset`, which draws a checker at 65,536 sub-texel offsets and keeps
      the sharpest as the offset `CTexture` adds to texture coordinates.
  - **Textures:** `FitTextureSize` rounds sizes to the device's limits, and `TEXTUREMODE`
    divides them by 1, 2 or 4.
  - **Port:**
    - `DeviceState` and `InitialDeviceState`;
    - the pixel channels;
    - `FitTextureSize`, the mipmap flags and the pixel mode.
  - **Checks:** `tools/device_vectors.py` writes the original's results in the emulation
    oracle to `tests/unit/device_vectors.inc`:
    - `Init` in 12 configurations;
    - the channel expanders;
    - `FitTextureSize` on 845 inputs;
    - the mipmap flags.

    The unit test replays `Init`'s calls through `DeviceState`.
  - **Not checkable in the emulator:** the calibration's result, which depends on the
    hardware's rasterizer.
  - **Labels:** the device's methods and its globals.

- [x] Textures and surfaces (`docs/render/texture.md`, `src/render/texture`):
  - **`CTexture::UpdatePixels`** samples an image down by whole factors to fit the
    texture, with no filtering, and converts to ARGB 1555, 4444, RGB 565 or 8888.
    - Opaque magenta becomes the transparent texel. With the flag, black does too, and in
      565 the magenta texel is kept as it was.
    - It then blits each mipmap level from the top one.
  - **`DrawRleImage`** draws a sprite's run-length image through its palette.
    - A run carries across rows, and a zero-length run zeroes the rest of the image.
  - **`CSurface::UpdatePixels`** packs pixels in the screen's format and leaves magenta
    out. `FillColor` packs a `COLORREF` in the screen's 16-bit format, whatever the
    target.
  - **Port:**
    - `ConvertTexel`, `UpdateTexturePixels` and `ImageUvScale`;
    - `DrawRleImage`, `UpdateSurfacePixels` and `FillValue`;
    - the surface and mipmap sizes.
  - **Checks:** `tools/texture_vectors.py` writes the original's texels to
    `tests/unit/texture_vectors.inc`:
    - 40 uploads across formats, flags, sampling, offsets and strides;
    - 8 run-length draws;
    - surface uploads and fills on three screen formats.

    The port matches every texel. The fake `Lock` now returns its rectangle's top-left,
    as DirectDraw does.
  - **Left for the water:** the water's bump maps (format 5) are documented but not
    ported.

- [x] The scene lists (`docs/render/scene.md`, `src/render/scene`):
  - **`AddPrimitive`** sorts primitives into lists by flags:
    - opaque, the ground, and two that are never drawn;
    - translucent: alpha or additive, each with z writes, without, or without the
      z-buffer;
    - one list drawn in the order added, with per-primitive stage modes and blends.

    Translucent primitives carry a depth key from the first two vertices' `rhw`.
  - **`DrawScene`** draws them in a fixed order of passes, each with its render and stage
    states.
    - **Sorts:** opaque lists by texture address; translucent ones by key, then primitive
      address, back to front.
    - **The ground** is drawn one of three ways, by `ISLIGHTMAP` and two-stage blending.
  - **The sort is VC6's unstable `std::sort`,** so equal keys follow the heap. The port
    replays it with `Vc6Sort` on ids ordered as the original's pointers.
  - **Port:**
    - `SceneLists` and `SelectSceneList`;
    - `SceneDepthKey`;
    - `AppendStageModeCalls` (`SetStageMode`);
    - `DrawScene`, as the sequence of device calls it makes.
  - **Checks:** `tools/scene_vectors.py` records, in the emulation oracle with the x87 at
    single precision:
    - every stage mode's calls;
    - six scenes of 40 to 200 primitives, added through the original's `AddPrimitive` with
      mixed flags and drawn under each fog, lightmap and two-stage setting.

    The port matches every call in order, including the sorts' quicksort path and ties.
  - **Not ported yet:** the vertex-buffer lists, the projection and fog set-up, and the
    in-place texture coordinate scaling.
  - **Labels:** the passes and adders.

- [x] The ground (`docs/render/ground.md`, `src/render/ground`):
  - **`C3dGround15::Build`** places each cell's top and walls from the `.gnd`, with their
    textures and lightmap coordinates.
  - **It lights every vertex from the `.rsw`'s light:**
    - corner normals average the faces of neighbouring cells whose heights match exactly;
    - the last corner starts from the wrong triangle's normal;
    - colour bytes come from the top of the cell whose corner the point is.
  - **`LightmapAtlas::Build`** packs the 8 × 8 lightmaps 1024 to a 256 × 256 ARGB 4444 page.
    Its page buffer is never cleared, so a partial page keeps the previous one's texels.
  - **Port:** `BuildGround` and `BuildLightmapAtlas`.
    - The arithmetic follows the original's order in single precision.
    - The build now sets `-ffp-contract=off`.
  - **Checks:**
    - **Synthetic grounds:** `tools/ground_vectors.py` records five of them from the
      original builder, with ISLIGHTMAP off and on. The port matches every cell, surface,
      vertex (lit colours included), block and page texel, bit for bit.
    - **Real maps:** `tools/ground_oracle.py` compares hashes of real maps, 12 in `ctest`
      (`ground_oracle_sample`). All 208 maps of `data.grf` match, both ways.
  - **Labels:** `Build`, `Reset` and the world light globals.

- [x] Drawing the ground (`docs/render/ground.md#drawing`, `src/render/ground`,
  `src/core/angles`):
  - **`DrawCells`** draws each cell of a scene-graph node's rectangle once a frame.
    - **Water:** a quad when the top dips below the waves, with heights from
      `SinDegrees` written into the cell, back-face culled.
    - **Surfaces:** `DrawSurface` for the top and walls. It projects them with the view
      matrix, culls those off screen when asked, and scales their colours by
      `g_WorldColorFade`.
  - **The angle tables** carry a quirk: negative angles read one degree off.
  - **Port:**
    - `DrawGroundCells` with `GroundPrimitive` quads;
    - the cells' corner records and frame stamps;
    - `core::SinDegrees` and `CosDegrees`.
  - **Checks:** `tools/ground_draw_vectors.py` records, from the original in the
    emulation oracle, every primitive five draws add, under varied views, culling, fade,
    lightmaps and water. The port matches bit for bit, with the cells and the angle
    tables.
  - **Labels:** `DrawCells`, `DrawSurface`, `GetWaterTexture`, and the fade, texture-scale
    and angle-table globals.

- [x] The first frame on screen (`docs/render/gpu.md`, `src/render/fixed_function`,
  `src/render/gpu_device`, `src/ragview`):
  - **`FixedFunctionState`** replays `DrawScene`'s calls over `Init`'s states and Direct3D
    7's defaults. Each draw becomes a pipeline key, the shader's stage and alpha-test
    uniforms, and its textures and samplers.
  - **`RenderDevice`** draws the records on SDL_GPU, to the window or to an image.
    - The shaders are GLSL, compiled during the build by glslang and SPIRV-Cross (pinned
      and built with the project) to SPIR-V and MSL.
    - The fragment shader emulates the two texture stages, the alpha test and vertex fog.
  - **`ragview MAP`** draws a map's ground through the whole ported chain: builder, cell
    drawing, scene lists, `DrawScene`, the fixed-function records. Its textures go through
    the client's own conversion. Prontera's ground shows textured and lightmapped, on
    Metal.
  - **Stand-ins:** an orbit camera in place of `CView`; a 90° projection in place of
    `SetSize`'s; no water. The camera and the projection have since been ported.
  - **Not yet reproduced:**
    - the texel offset and half-pixel centres;
    - `BORDER` addressing;
    - mipmaps and the texture divisor;
    - the fog set-up.
  - **Checks:** unit tests cover the records and texel widening. CI has no GPU, so it builds
    `ragview` and the shaders; frames are checked by eye.

- [x] The camera and the projection (`docs/render/camera.md`, `src/render/camera`,
  `src/render/matrix`):
  - **`SetSize`** gives the client's projection: a 15° field of view.
  - **`CView`** follows the player: each frame the yaw, pitch, distance and point move a
    tenth of the way to their targets.
    - The eye drops onto the `.gat`'s ground where it would sink below it.
    - The view is a look-at matrix with its inverse.
  - **`CGameMode`:**
    - **Placing:** it places the camera on entering a map, from the registry's indoor or
      outdoor latitude and distance, and saves them on leaving. Indoor maps
      (`indoorRswTable.txt`) turn to 45°, with tighter limits.
    - **The mouse:** it turns, pitches (Shift) and zooms (Ctrl) the camera, or resets it.
  - **Port:**
    - `Matrix`'s rotations, product, look-at and inverse;
    - `SetSize`, `Camera` with its reset, step and view build;
    - `PlaceCamera`, `LeaveMap` and `DragCamera`;
    - `res::AttrHeight` (`C3dAttr::GetHeight`).
  - **Checks:** `tools/camera_vectors.py` records the original in the emulation oracle:
    - `SetSize` at ten sizes;
    - `GetHeight` at 48 points;
    - 80 frames following a player;
    - eight map entries;
    - 60 mouse inputs.

    The port matches bit for bit.
  - **`ragview`** now frames maps as the client does, indoor ones included, with the
    client's mouse controls. It replaced the stand-ins, which had mirrored the view to
    hide a reversed cull mapping in the device; the device now maps `CW` and `CCW`
    straight.
  - **Not ported:** the view's shake, and the actors' sprite scales that `SetSize` and
    `Step` set.
  - **Labels:** `CView`'s methods, the placing, saving and input functions, `IsIndoorMap`,
    and the view setting globals.

- [x] The models (`docs/render/model.md`, `src/render/model`):
  - **`ModelInstance`** places a model as `CWorld::LoadWorld` does:
    - its nodes are set up from the model's first node in file order;
    - it is centred on its box and stood on the box's top;
    - it gets a world matrix, an oriented box and each node's world box.
  - **`C3dNode`'s matrix** is scale · rotation · translation from its keys: the
    translation interpolated linearly, the rotation by the client's slerp. Its `acos`
    keeps the x87's single precision.
  - **Lighting:** each face's colours come from its shade type (white, the face normal,
    or the corner normals). A texture coordinate's colour field halves the light. A
    ground tint adds the cell colour under each corner.
  - **Drawing:** each node's vertices are projected as the ground's are; faces are culled
    by facing (two-sided ones by their `field_10`) and, when clipping, by the screen.
    Runs of one texture go to the opaque or translucent list.
  - **Port:**
    - `CreateModelInstance`, `LightModel`, `DrawModel`, `AdvanceAnimation`;
    - `NodeMatrixAt` and `SlerpRotation`;
    - `GroundCellColor`;
    - the rest of `Matrix` (rotations about x and z, scale, translation, quaternions).
  - **Checks:** `tools/model_vectors.py` records the original in the emulation oracle on
    five synthetic and five real models: placement, boxes, lighting, tint, every drawn
    vertex under four draw settings, three animation steps, and 200 slerps. 19,170
    values match bit for bit.
    - It turned up the loader's precision: the main thread bakes vertices in single
      precision, the lazy thread in double, as the port does.
  - **The device** now samples an unbound stage as opaque black, as Direct3D does. The
    ground's lightmap stage stays set for the models and passes their colour through.
  - **`ragview`** places, lights, animates and draws every model of the map.
  - **Not ported:** the volumes, the scene graph's culling, the ray translucency, and the
    clip rectangle under `00688a74`.
  - **Labels:** `ModelInstance`'s and `C3dNode`'s set-up, lighting, tint, box and draw.

- [x] The scene graph and the view frustum (`docs/render/scene_graph.md`,
  `src/render/scene_graph`):
  - **`BuildSceneGraph`** splits the map into a quadtree to depth 5 and puts each model
    in the leaves its oriented box meets (a 15-axis separating axis test). Each node
    gets the ground's and the attribute map's cells under it.
  - **The bounds** come from a version 2.1 `.rsw` (202 of 208 maps), or from the heights
    under each leaf. The ancestors keep their ±999,999 heights.
  - **`ViewFrustum`** carries six view-space planes (the 7.5° sides, near 10, far 1500)
    into the world, and lists the leaves inside and crossing.
  - **`CView::vf03`'s order:** crossing leaves first, their models classified, stepped
    and clipped; then inside leaves; each model and cell once a frame.
  - **Checks:** `tools/scene_graph_vectors.py` records both ways of building, five views'
    planes, leaves and model classes, and 300 box tests. 60,738 values match.
  - **`ragview`** draws only what the frustum keeps.
  - **Labels:** `CView::vf03` and its cell range globals.

- [x] The water and the background (`docs/render/ground.md#the-water`):
  - **`SetWaterParams`** takes the `.rsw`'s water as `LoadWorld` passes it, two values
    truncated.
  - **Each frame step** (`004bc450`): the frame picks one of 32 textures,
    `워터\water<type><frame>.jpg`, and the waves' phase moves on.
  - **Port:** `WaterOfWorld`, `StepWater` and `WaterTextureName`. Checks:
    `tools/water_vectors.py` records six waters over 200 steps; 3,636 values match.
  - **No sky:** the 2003 client's sky boxes (`InitModelSkyBox`, `DrawTexturedSkyBox`, a
    lens flare) have no callers. The background is the clear colour `CGameMode::OnInit`
    picks: 0xff99ccff for `yuno.rsw`, 0xff6699cc for `gonryun.rsw` and `gon_dun02.rsw`,
    black otherwise.
  - **Models are always tinted:** `OnInit` also sets `g_WorldColorFade`+0xd, so in game
    the models draw with the ground's colour added.
  - **`ragview`** steps the water and draws it textured, tints the models, and clears to
    the map's colour.

- [x] The fog (`docs/render/fog.md`):
  - **The switch:** `ClientState`+0x182c, from the registry's `FOG` (on when missing),
    flipped by `/fog`.
  - **The map's fog:** `CGameMode` `00516570` finds the `.rsw` name in the sorted
    `fogParameterTable.txt` (`005991f0`). It reads start and end (fractions of 10 to
    1500), a colour (`ParseHexDigits`) and a density into `00625ea4` to `00625eb4`, then
    `SetFogEnabled`.
  - **Each vertex:** `ComputeVertexFog` replaces the specular colour with the fog factor
    in its alpha, on every primitive when the device has vertex fog.
  - **`SetupProjection`'s states:** the colour, start and end as depths, density, linear
    vertex fog and range fog. Without vertex fog, linear fog is turned off.
  - **Port:** `FogState`, `ParseHexDigits`, `FindMapFog`, `VertexFog` and `SetupFog`.
    The `atof` is shared with the `.rma` reader (`res/c_runtime`).
  - **Checks:** `tools/fog_vectors.py` records 27 hex strings, a synthetic table looked
    up 19 ways, 394 depths under 9 ranges and 16 set-ups; 3,719 values match.
  - **`ragview`** fogs the maps that have a record, unless `--no-fog`.
  - **Labels:** `SetFogEnabled`, `ApplyMapFog`, `FindFogParameters`, and the fog
    globals.

- [x] The actors' sprites (`docs/render/actor.md`):
  - **The scales:** `SetSize` and `CView::Step` leave five globals the sprites read: the
    size, two depth slopes from the pitch, a layer step and a depth bias from the
    distance.
  - **Animation:** `CGameObject::vf04` turns a facing into one of eight directions against
    the camera; `CRenderObject::vf08` picks the action and the frame from the time, looping
    or once.
  - **`DrawBody`** places each item of the current frame at the projected position, scaled
    by depth, with its rhw from the slopes and the layer steps, tinted, and kept when on
    screen. **`DrawShadow`** puts `shadow.spr` on the ground under the actor.
  - **`AddSpriteQuad`** turns a quad into four vertices, turned by its angle, textured from
    a 32-bit image's texture or the sprite texture cache.
  - **The ground's tint is always white:** the table `C3dGround15::GetCellTint` reads is
    never filled.
  - **Checks:** `tools/actor_vectors.py` records the scales at 8 sizes and 10 cameras, 240
    objects (458 body quads, 72 shadows), 200 quads, 120 directions and 200 frame steps;
    13,744 values match.
  - **`ragview`** shows an actor where the player stands (`--actor`, `npc\4_m_01` by
    default).
  - **Labels:** `DrawBody`, `DrawShadow`, `Render`, `UpdateFrame`, `SetAction`,
    `GetDirection`, `AddSpriteQuad`, the cache's `FindCachedSprite` and
    `CacheSpriteImage`, and the five scales.
  - **Not ported:** `CPc`'s layered characters (body, head, headgear and palettes), the
    texture cache's slots, and the screen bounds kept for picking.

- [x] Player characters (`docs/render/player.md`):
  - **Layers:** `CPc` keeps eight layers of actions and sprites: body, head, three
    headgear, weapon, glow and shield.
  - **Names:** `ClientState::InitNameTables` fills the jobs', sexes' and hair styles'
    names; the body, head and `.imf` paths are built from them.
  - **`RenderActFrameItems`:**
    - eight slots; the `.imf` says whether the body or the head goes first, shifted when
      the actor faces away;
    - the head is seated on the body, and the headgear on the head, by the frames' first
      anchors, the head's offset kept in two globals between frames and players;
    - weapons only in the attack actions; headgear without depth writes; colour channels
      combined without masking.
  - **Checks:** `tools/player_vectors.py` records every name, 300 `.imf` lookups and 320
    players (782 quads); 11,006 values match.
  - **`ragview --player JOB SEX HAIR`** draws a player with its body and head.
  - **Not ported:** the headgear's, weapon's and shield's names, other palettes, and the
    Korean service's special drawing.

- [x] Dressing a player (`docs/render/player.md#the-names`):
  - **Names:** the headgear (`SetHeadgearTop` to `Bottom`), the weapon by its kind
    (`SetWeaponKind`, with `CanShowWeapon`'s per-job switch and the glow layer), the shield
    (`SetShield`, item ids mapped to kinds) and the hair and clothes palettes
    (`GetHeadPaletteName`, `GetBodyPaletteName`), from five more of `InitNameTables`'
    tables.
  - **Port:** `src/render/player_names`, the names moved there from `player`.
  - **Checks:** `CPc`'s own setters in the emulation oracle: 280 headgear, 1,035 weapons,
    945 shields and 639 palettes; with the layers, 18,874 values match.
  - **`ragview`:** `--hair-color`, `--clothes-color`, `--headgear`, `--weapon`, `--shield`
    and `--action` dress and pose the player.
  - **Not ported:** `SetWeapon` by item id.

- [x] The `.str` effects (`docs/render/effect.md`):
  - **`AdvanceStr`:** each world step, every layer's keys move on to the frame; type 0 keys
    set the 29 values, others add to them, and the texture index moves by one of five
    modes (one shares a stack slot with the z offset across layers).
  - **`DrawStrLayer`:** two triangles per moved layer from layer 1 on, the corners turned
    by the angle about the layer's position, centred on the actor's screen point 80 pixels
    up, at the near plane, with the layer's own blends and stage mode.
  - **The end:** when every layer has run out of keys; a layer without keys never does.
  - **Checks:** `tools/effect_vectors.py` plays three synthetic effects for 240 frames;
    66,406 values match.
  - **`ragview --effect NAME`** plays an effect where the actor stands.
  - **Not ported:** the effects' own processing cases, `CEffectPrim`'s particles, the
    sounds.

- [x] Effect ids and the first primitives (`docs/render/effect-prim.md`):
  - **The table:** `InitEffect`'s `.str` for each effect id, the `_min` files of minimal
    effects, the seven ids that pick a numbered variant with `rand`, and the durations at
    `00617148`. The CRT's `rand` is ported (`core::CrtRand`).
  - **`CEffectPrim`, ids 0 to 2:** the screen-space rays and diamonds: flying, turning,
    growing, colour speeds, alpha keys, bounce and fades, including the fade to the
    owner's end.
  - **The hit spark (effect id 1):** eight lens rays from the actor's screen point.
  - **`EffectWorld`:** the world's object list in its order, primitives running the step
    they are made, and an ended effect clearing its primitives' owner.
  - **Checks:** `tools/prim_vectors.py`: every id both ways, 36 synthetic primitives and
    four hit sparks; 98,957 values match.
  - **`ragview --effect-id ID [--minimal-effects]`.**
  - **Not ported:** the other 65 primitive ids and the other effects' processing.

- [x] Particles and cylinders (`docs/render/effect-prim.md#particles-and-cylinders`):
  - **`CEffectPrim` ids 5, 6 and 0xd:** particles flying along their matrix with every flag
    of id 5 (drift, wander, straight steps, ending near a target, facing the camera), thrown
    particles bouncing off the ground, and cylinders of two rings.
  - **The history:** `ResetHistory`, `ShiftHistory` and `RenderActFrameHistory`, a sprite's
    frame drawn at each of a particle's last places.
  - **Effect ids 0 and 2:** `ProcessRingBurst` (`ring_blue.tga` and four particles) and
    `ProcessHit3` (two lens cylinders and eight particles).
  - **`DegreesToRadians`** and its table (`006252fc`).
  - **Checks:** 30 synthetic shapes and five runs of the two effects; 360,539 more values
    match (459,496 in `prim_test`).
  - **`ragview --effect-id 0`** and **`2`** draw them with the `particle1` sprite.

- [x] Ring walls (`docs/render/effect-prim.md#ring-walls`):
  - **`RenderRingWalls` and `DrawPresetColorQuad`:** four rings of 21 walls leaning out by
    their tilt, coloured by eleven presets of the scale, blended by it, with the
    original's mixed projection orders.
  - **`CEffectPrim` ids 0x1d and 0x27:** swelling, turning ring waves and rings that rise
    from age 200, fading after 250 or 800.
  - **Eleven effect ids** (0xe1, 0xe7, 0xec to 0xee, 0xf7, 0xf8, 0x103 to 0x105, 0x107)
    through `AddRingWaves`, `AddSlowRingWaves`, `AddRisingRings` and `AddFadingRings`;
    id 0xf8 ends on its first step, as `AdvanceStr` finds no `.str`.
  - **Primitive triangles carry their flags and blends**, which `ragview` now uses.
  - **Checks:** 12 synthetic ring primitives and the eleven effects; 370,048 more values
    (855,776 in `prim_test`).

- [x] The other ring walls (`docs/render/effect-prim.md#more-rings`):
  - **`CEffectPrim` ids 0x17, 0x18, 0x19, 0x1c, 0x22, 0x30, 0x36, 0x39, 0x3c and 0x3e:**
    rising, turning, spreading, wobbling, lifting, waving and closing rings, with the ring
    records' per-wall flag bytes.
  - **Twelve more effect ids:** 200, 0xde, 0xfc, 0xef to 0xf1, 0x118, 0x11d, 0x130 and
    0x13a to 0x13c, through `AddRingTrio`, `AddTurningRings`, `AddRandomRing`,
    `AddTwirlRing`, `AddTeleportationRings`, `AddTeleportationWalls`, `UpdateGlassWall`
    and `AddReadyPortal`.
  - **Checks:** 24 synthetic ring primitives and 23 ring effects; 1,068,602 values in
    `prim_test`.

- [x] Facing squares (`docs/render/effect-prim.md#facing-squares`):
  - **`RenderFacingSquares`, `RotateQuadPoints` and `DrawQuad`:** squares of four corners
    turned to face the camera by its yaw and pitch, in eleven colours, through the quad
    drawer 38 renderers share, with its four transform orders. The angle tables are read
    as they lie in memory, so a turn past 360 reads on into the next table.
  - **`CEffectPrim` ids 0x1f, 0x28, 0x2b, 0x2d, 0x2f, 0x33, 0x37 and 0x38:** clouds, a
    boomerang, thrown sparks, bobbing, sinking, arcing and pulsing squares, and rising
    sparks, with trails and `VectorToDegrees`.
  - **30 more effect ids** (0xe5, 0xe6, 0xe9, 0xf9, 0xfd, 0x108, 0x109, 0x113, 0x116,
    0x117, 0x119 to 0x11c, 0x11e, 0x120, 0x121, 0x123, 0x125, 0x12a, 299, 0x134 to 0x136,
    0x138, 0x139, 0x140, 0x145, 0x14b and 0x14c) through eleven setups; five of the ids
    add waving walls too. The effect gains
    its target and level, and the world the player's position.
  - **Checks:** 24 synthetic square primitives and the 30 effects; 1,489,215 values in
    `prim_test`.

- [x] Flat squares (`docs/render/effect-prim.md#flat-squares`):
  - **`RenderHorizontalSquares`:** level squares swelling on the sine of their tilt, with
    the original's double 0.8 rounded once (`core::RoundedProduct`, moved out of the
    `.str` player).
  - **`CEffectPrim` ids 0x1a, 0x25 and 0x40**, and effect ids 0xc9 and 0xf2 through
    `AddTwinSquares` and `AddSwellingSquare`. The setup for 0x40 (`00574c40`) is never
    called.
  - **Checks:** 12 synthetic flat squares and the two effects; 1,521,275 values in
    `prim_test`.

- [x] Motes (`docs/render/effect-prim.md#motes`):
  - **`RenderRisingSquares`, `RenderPulsingSquares` and `RenderOrbitingSquares`:** up to
    four small squares a record facing the camera, rising and drifting, circling in two
    colours, or popping up in five images with the corner order turned by the camera.
  - **`CEffectPrim` ids 0x1b, 0x1e and 0x21**, and effect ids 0xca, 0xe4, 0xcb and 0xfe
    through `AddRisingMotes`, `AddPulsingMotes` and `AddPoppingMotes`.
  - **Checks:** 12 synthetic mote primitives and the four effects; 1,591,869 values in
    `prim_test`.

- [x] Single squares and rectangles
  (`docs/render/effect-prim.md#single-squares-and-rectangles`):
  - **`RenderTiltedSquare`, `RenderRotatedRectangle`, `RenderColoredFacingSquares`,
    `RenderFacingSquareLayers`, `RenderFacingRectangles` and `RenderRotatedSquares`:** one
    shape a record: bursting squares, rays, fireworks with a trail and sparkles, beams,
    lines, and ten nested squares.
  - **`CEffectPrim` ids 0x29, 0x2a, 0x32, 0x34, 0x35 and 0x41**, and effect ids 0xe3,
    0x106, 0x10a, 0x114, 0x129, 300, 0x12f and 0x148 through seven setups, with
    `AddSpreadingRings` (id 0x1c) for 0x148. `AddCoinBurst` (`005754a0`) is never called.
  - **Checks:** 24 synthetic primitives and the eight effects; 1,848,563 values in
    `prim_test`.

- [x] Walls and bands (`docs/render/effect-prim.md#walls-and-bands`):
  - **`RenderSlantedRingWalls`, `RenderOffsetRingWalls`, `RenderRaisedRingWalls`,
    `RenderBox`, `RenderBoxWalls`, `RenderPentagonBand`, `RenderArcBands` and
    `RenderBandSegments`:** quarter rings whose walls lean their own way, rings about
    their own centres or lifted, boxes, five-sided tubes between two points, and a
    bouncing trail of three-sided tubes.
  - **`CEffectPrim` ids 0x20, 0x23, 0x3a, 0x3b, 0x3d, 0x3f, 0x42 and 0x43**, and effect
    ids 0xe0, 0xe2, 0x133, 0x13d to 0x13f, 0x142 to 0x144, 0x149 and 0x14a through eight
    setups. Nothing adds 0x3a.
  - **Checks:** 32 synthetic primitives and the eleven effects; 2,584,702 values in
    `prim_test`.

- [x] Sprite trails (`docs/render/effect-prim.md#sprite-trails`):
  - **`CEffectPrim` ids 7, 8, 0x15, 0x26, 0x2c and 0x31**, drawn by
    `RenderActFrameHistory`: particles circling their owner, shots circling an axis
    (`Matrix::MakeAxisRotation`), snow left where it fell, drifting leaves, and sprites
    standing at a point.
  - **Effect ids 7, 0xe, 0xf, 0x47, 0x48, 100, 0x56, 0x75, 0xa2, 0xa3, 0xd8, 0xd9, 0x10b,
    0x12e and 0x14d** through thirteen setups. The effect world loads its sprites by name,
    and ragview draws each quad with its own sprite. The fade to an owner's end rounds its
    quotient once (`core::RoundedQuotient`), as the x87 divides by an exact integer.
  - **Checks:** 24 synthetic trails and 22 effect runs; 2,975,160 values in `prim_test`.

- [x] Screen wedges and rings (`docs/render/effect-prim.md#screen-wedges-and-rings`):
  - **`RenderScreenWedge`, `RenderScreenWedge2`, `RenderScreenRing`, `RenderScreenLine` and
    `RenderHistoryRings`:** wedges and rings on the screen at the owner's point, a hairline,
    and rings in the world at each history record.
  - **`CEffectPrim` ids 3, 4, 0xc, 0x13 and 0x14**, and effect ids 8, 0xc, 0x10, 0x23,
    0x24, 0x28, 0x2a, 0x38, 0x3a, 0x3b, 0x3d, 0x3e, 0x49, 0x62, 0x79, 0x80, 0xd7 and 0x141
    through fourteen setups, each putting its actor on screen its own way. Nothing adds
    0x13; only 0xa1, beside the unported 0x16, adds 0x14. The ring walls read a tilt below 0
    before the tables, as the original does for 0x141's widest closing rings.
  - **Checks:** 20 synthetic primitives and the 18 effects; 3,847,129 values in `prim_test`.

- [x] Flying quads (`docs/render/effect-prim.md#flying-quads`):
  - **`UpdatePrim_15` and `UpdatePrim_22`**, the flight ids 0xf, 0x10 and 0x16 share:
    changes, steps, seeking the target, the second matrix turned to the camera or not,
    scrolling v, leading the target, landing, rocking and cycling colours.
  - **`RenderTransformedQuad` and `RenderBillboard`:** ids 0xf and 0x16, a quad in the
    second matrix's plane and one on the screen.
  - **Effect ids 0xd, 0x30, 0x3c, 0x52, 0x5d, 0x5e, 99, 0x72, 0x74, 0x76, 0x77, 0x89,
    0x9f, 0xa0 and 0xa1** through twelve setups. A primitive's owner can now be written
    to, as kLeadsTarget and kLands do.
  - **Checks:** 20 synthetic primitives and 16 effect runs; 4,097,770 values in `prim_test`.

- [x] Crossed quads (`docs/render/effect-prim.md#crossed-quads`):
  - **`RenderCrossedQuads`:** id 0x10, two quads crossed along its own matrix's z row,
    their corners summed three ways and two of them projected inline.
  - **Effect ids 9, 0x1a, 0x1f, 0x25, 0x26, 0x2b, 0x73 and 0xdc** through five setups:
    rising flecks, agility rays, arrows aimed with `GetAngleBetween` that leave rings
    where they land, blitz beat with its count shared across effects, and Concentration,
    which ends itself at frame 151.
  - **Checks:** 10 synthetic primitives and 12 effect runs; 4,662,264 values in
    `prim_test`. ragview takes `--effect-level`, and now plays all of `--effect-step` before
    its first frame rather than 8.

- [x] Pyramids and spheres (`docs/render/effect-prim.md#pyramids-and-spheres`):
  - **`UpdatePrim_9` and `RenderPyramid`** (id 9), **`UpdatePrim_14` and `RenderSphere`**
    (0xe), and ids 0x11 (aged only) and 0x12 (the cylinder's update without its turn),
    which nothing creates.
  - **Effect ids 0x11, 0x1b, 0x1c, 0x3f, 0x4a, 0x4f, 0x59, 0x7b, 0x84, 0x8e and 0xaa**
    through eleven setups: icicles and stones walked towards the target, bursts, walls
    and fields of spikes, Storm Gust and the blasts.
  - **Checks:** 40 synthetic primitives and 14 effect runs; 5,468,837 values in
    `prim_test`.

- [x] Ribbons, the tube and panels (`docs/render/effect-prim.md#ribbons-the-tube-and-panels`):
  - **`UpdatePrim_10` and `RenderHistoryRibbon`** (ids 0xa and 0xb, which nothing
    creates), **`UpdatePrim_36` and `RenderTubeBetweenPoints`** (0x24) and
    **`UpdatePrim_46` and `RenderVerticalPanels`** (0x2e): every `CEffectPrim` id is now
    ported.
  - **Effect ids 0xe8, 0x115, 0x11f, 0x122, 0x124 and 0x126** through two setups; the tube
    looks its actor up by id (the player's through `g_ClientState+0xcdc`, the rest through
    `CWorld::GetActorById`), which `EffectWorld` now carries.
  - **Checks:** 28 synthetic primitives and 8 effect runs; 5,745,547 values in
    `prim_test`.

- [x] The cases that only play their `.str` (`docs/render/effect.md#which-cases-play-their-str`):
  - **97 cases of `CRagEffect::vf01`:** `ProcessGenericType` and the 40 functions like it
    after their sounds, the potions' and three more of vf01's own, 0xfb 20 up, 0x82 and
    0xfa lasting 9999 steps, 0x85 and 0x86, 0x91 starting its `.str` again, 0x69, 0x12d
    and the jokes with none, and Concentration's other three columns.
  - **`AdvanceStr` and `ResetStr`** through an effect's `EffectStr`, false without one;
    the ten cases ported before that step a `.str` now call it too. ragview plays the
    launched effect's `.str` through it.
  - **Messages to the game mode** (`CGameMode::SendMsg`): the view's shake, its amplitudes
    and the jokes, which `EffectWorld` keeps.
  - **Checks:** each of those cases and the ten twice, `AdvanceStr` answered as a `.str`
    that ends and one that does not; Concentration's columns; 6,802,902 values in
    `prim_test`.

- [x] More cylinders (`docs/render/effect-prim.md#more-cylinders`):
  - **Effect ids 6, 0x22, 0x7a, 0x8a and 0x8c:** cylinders (id 0xd) through five setups,
    0x7a's along the actor's facing.
  - **Effect ids 0x36, 0x37 and 0x39:** `AddSpellCircle` (`005673b0`), three open rings
    and a tall one on id 0x17.
  - **Checks:** nine effect runs; 7,110,580 values in `prim_test`.

- [x] More particles (`docs/render/effect-prim.md#more-particles`):
  - **Effect ids 0x14, 0x27, 0x2c, 0x2f, 0x35, 0x7d, 0x88, 0xa5, 0xab, 0xd4, 0x14e and
    0x151:** particles (id 5) of seven more sprites through eight setups; 0x14e keeps the
    effect on the ground, 0xa5 picks an action, and 0x2f and 0xa5 read the effect's +0xc4
    and +0xc8.
  - **Checks:** sixteen effect runs; 7,188,401 values in `prim_test`.

- [x] The other particles (`docs/render/effect-prim.md#more-particles`):
  - **Effect ids 0x15, 0x16, 0x18, 0x21, 0x2d and 0x50:** the detoxification bubbles,
    fireballs and spears at the target, a sprite and its shadow whirling round the actor
    (two ways), and a wandering, keyed spark.
  - **Checks:** eight effect runs; 7,273,751 values in `prim_test`.

- [x] More rays (`docs/render/effect-prim.md#more-rays`):
  - **Effect ids 0x4, 0x5, 0xb, 0x20, 0x2e and 0x32:** rays and diamonds on screen (id 0),
    each case with its own order for the actor's point on screen.
  - **Checks:** twelve effect runs; 7,420,761 values in `prim_test`.

- [x] Bouncers (`docs/render/effect-prim.md#bouncers`):
  - **Effect ids 0x12 and 0xa:** Steal's sparks and the coins, bouncers (id 6) of two more
    sprites; 0xa steps its `.str` and ends at frame 100.
  - **Checks:** five effect runs (one with minimal effects on) and 0xa's two `.str` runs;
    7,559,067 values in `prim_test`.

- [x] Bursts (`docs/render/effect-prim.md#bursts`):
  - **Effect ids 0x3 and 0x51:** a cylinder along the actor's facing with particles fanned
    round it; 0x51 repeats while the frame is below 20 × +0xb0, which only the constructor
    sets (to 1).
  - **Checks:** four effect runs, one with +0xb0 written to 3; 7,616,781 values in
    `prim_test`.

### Next actions
1. M4, the renderer: the 4 cases of `CRagEffect::vf01` left (0x33, 0x78, 0xa4 and 0xa6):
   0x33 and 0xa4, which make their primitives without `CreatePrimitive`, then 0x78 and
   0xa6, which change the actor.
2. Give `vfNN` and `method_` functions descriptive names as their classes are ported,
   in the same reviewed way.
