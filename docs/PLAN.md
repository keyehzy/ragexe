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
4. Names come from the binary: RTTI, strings, imports. Anything else is a descriptive
   name we make up, never one remembered from elsewhere.
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
subsystem, status, namespace, name and notes. Status runs
`raw → named → typed → specced → ported → verified`.
It is a sorted text file, so every status change is a readable diff in git.
`tools/ledger.py` has three commands:
- `sync` refreshes names and sizes from the Ghidra export and keeps everything set by hand.
- `set` changes rows by address or address range.
- `report` prints progress.

Kind is one of:
- `code`: a real function.
- `funclet`: an EH `Unwind@`/`Catch@` fragment, ported as part of its parent function.
- `thunk`: an import stub.

Only non-library `code` counts toward progress, which is reported as
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
- CMake + C++20 + vcpkg manifest (SDL3, miniaudio, FreeType, zlib, stb, Catch2).
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

### M1 progress
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

### Next actions (M1)
1. Size the global singletons whose size is only estimated (`ClientState`, `UIWindowMgr`,
   `C3dDevice`): the accesses past their estimated ends need a rule for where an object
   stops before the next global begins.
2. Assign every function to a subsystem in the ledger, and write `docs/architecture.md`
   (main loop, mode switcher, singletons, threads).
3. Extend the oracle with the D3D7/DirectDraw COM fakes that record calls, for M4.
