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

Step 1 of the brief (unpacking) is not needed. One check remains: compare a memory
dump at the entry point against the file, to rule out code that is written at runtime.

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
tools/        pe_survey.py, Ghidra scripts, ledger tooling, oracle DLL, stub server
docs/         this plan, formats/, protocol/, architecture notes
src/          the new client: platform/ core/ res/ render/ audio/ net/ ui/ world/ game/
tests/        unit, oracle-diff, packet-trace and screenshot tests
```

## 4. Method

### 4.1 Function ledger
`analysis/ledger.sqlite` has one row per function: address, size, class, library flag,
and status `raw → named → typed → specced → ported → verified`.
Progress is reported as **% of non-library code bytes verified**, not file count.

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
- **Oracle DLL.** The original runs under Wine or a Windows VM. A proxy DLL
  (in place of `ijl15.dll` or `dbghelp.dll`) calls the original's own functions by
  address, such as the resource loaders, path finder and text layout, and dumps the
  results as JSON. Our port must produce the same output.
- **Packet traces.** Both clients talk to our stub server (M6). Their request byte
  streams must match for scripted scenarios.
- **Screenshots.** Fixed camera, fixed seed, same map. Compare with a perceptual
  threshold, since GPU output will not be bit-exact against D3D7.
- **Fuzzing.** libFuzzer/ASan on every resource parser. The 2003 code trusted its input.

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
- Dynamic environment: run the original exe under Wine (Rosetta) or a Windows VM until
  it reaches the login screen. Attach a debugger (x32dbg in the VM, or `winedbg`).
- Runtime-code check from section 1.
- **Exit:** analyzed Ghidra project; the original boots to login; the ledger exists.

### M1 — Static map of the binary (4–6 weeks)
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
- **Exit:** every entry in `data.grf` and the `.gpf` patches parses; oracle diffs are
  clean on a sample set; parsers have run under the fuzzer.

### M4 — Renderer (8–10 weeks)
- Recover how the original drives D3D7 and DirectDraw (`C3dDevice`, `CTexture`,
  `CSurface`, `CDCSurface`, `CView`, the renderer): vertex formats, render states,
  blend and alpha-test modes, colorkey, fog, lightmaps, and how it sorts draws.
- `RenderDevice` interface on SDL_GPU, with shaders that reproduce that fixed-function state.
- Build order: 2D blits and UI surfaces → FreeType text → ground and lightmaps
  (`C3dGround`, `C3dGround15`) → RSM models (`C3dNode`) → water → billboard sprites →
  minimap, fog, lighting.
- `ragview` tool: a map and model viewer.
- **Exit:** reference maps match screenshots of the original within the threshold.

### M5 — Actors and animation (6–8 weeks)
- The `CRenderObject → CGameActor → CPc/CNpc/CPlayer/CItem/CCorpse` hierarchy.
- ACT frame timing, eight directions relative to the camera, layering and anchors for
  body, head, weapon, shield and headgear, palette swaps.
- Camera control, picking (`CActorPickNode`), walkability from the attribute map, path finding.
- Granny sub-milestone: reverse `granny2.dll` only as far as these 7 models need
  (mesh, skeleton, one animation track) and write a CPU skinning path. Placeholders until then.
- **Exit:** offline sandbox: click-to-move on a map with every actor type animating;
  path finder output matches the oracle.

### M6 — Network and protocol (6–8 weeks)
- `CConnection`/`CRagConnection`: framing, the packet-length table (generated into
  `packets.def` with addresses), fixed and variable-length packets, the
  login → char → zone handoffs in `CLoginMode`.
- `tools/stubserver`: our own minimal server, written from what the client sends and
  parses: login, server list, char select and create, map enter, spawn, walk, chat,
  a few scripted NPCs and monsters.
- Port the packet handlers in `CGameMode` and `CLoginMode`.
- **Exit:** both the original and the port play the same scenario against the stub
  server (login → create character → enter map → walk → chat → NPC dialog → logout),
  with identical client→server traces.

### M7 — UI framework and windows (10–14 weeks; the largest milestone)
- `UIWindow` base, `UIWindowMgr`, focus, z-order, dragging, modal handling, and the
  bitmap skin lookup (`CSkinMgr`).
- Controls: buttons, edit controls with IME, list and combo boxes, scrollbars, tabs,
  gauges, balloon text.
- Windows in priority order: login, server select, char select and create → HUD
  (basic info, chat, shortcut bar, minimap) → items, equipment, skills, status,
  options → NPC dialogs, shops, trade, merchant shops → party, guild, friends, whisper
  → the rest (pet, mini-games, web browser stub, …).
- **Exit:** all ~170 `UI*` classes ported, each with a screenshot test.

### M8 — Audio and video (2–3 weeks)
- The sound manager on miniaudio: streamed BGM, 3D positional effects with the
  original's distance and volume rules, ADPCM WAV, option sliders.
- Optional Bink intro via FFmpeg.
- **Exit:** sound events in the scenario match the original (logged by the oracle DLL).

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
| Running the original on Apple Silicon | Wine with Rosetta first, x86 Windows VM as fallback; only M0 and the oracles need it |
| Granny and Bink are proprietary | Limited RE of the shipped DLL for the 7 models; the intro video is optional |
| D3D7 output is not bit-exact on modern GPUs | Perceptual screenshot threshold; reproduce blend and alpha rules exactly |
| Fidelity vs. readable code | Quirks isolated in `compat::`; everything else idiomatic |
| Legal | Private repo; never distribute `original/`, data files or decompiler output |

## 7. Status

### M0 progress
- [x] JDK 21: Homebrew `openjdk@21` was already installed. `tools/ghidra/analyze.sh` sets `JAVA_HOME` to it.
- [x] Ghidra headless analysis: project in `analysis/ghidra/` (gitignored), about 1 minute per run.
  `ExportInventory.java` writes `analysis/exports/Ragexe.exe.{functions,classes}.tsv` and `.summary.txt`.
- [x] MSI layout: `tools/msi_layout.py` joins the File, Component and Directory tables and hard-links all
  507 files into `original/install/` under their real names (`data.grf`, 10 `.gpf`/`.rgz` patches, `BGM/`,
  `skin/default`, `skin/Scribbling Kid`). Sizes are checked against the MSI. SHA-256 manifest: `analysis/manifest.sha256`.
- [x] `git init` (branch `main`) with `.gitignore` for original files, game data, the Ghidra project and decompiler output.
- [ ] Run the original under Wine or a Windows VM until it reaches the login screen. Attach a debugger.
- [ ] Runtime-code check (needs the item above).
- [ ] Ledger database seeded from `functions.tsv`.

### Next actions
1. Set up the dynamic environment (Wine through Rosetta, or an x86 Windows VM).
2. M1: the RTTI → Ghidra class and vtable script, using the 295 locators found above.
3. M1: hand-label the VC6 CRT/STL, since FID does not cover it.
