# Architecture of Ragexe.exe

How the 2003 client is put together: its subsystems, start-up, the mode state machine, one
frame, input, networking, threads, the global objects and shutdown. Addresses are virtual
addresses in `original/client/Ragexe.exe`. Names come from the label files in
`analysis/labels`: class names are from RTTI or strings in the binary, and the rest are
descriptive names made up for this project (PLAN rule 4). `FUN_`, `method_` and `vfNN`
names are functions that are not named yet.

## 1. Subsystems

Every function of the game's own code has a subsystem in the ledger's `subsystem` column.

| Subsystem | Functions | Bytes | What it covers |
|-----------|----------:|------:|----------------|
| `core` | 210 | 36,897 | `WinMain`, the window and message loop, the mode switcher, the frame timer, client configuration (clientinfo.xml, registry), input devices, language and code-page handling, vector and matrix math, the crash handler |
| `res` | 673 | 128,002 | the GRF archive (`CGPak`), `ResourceManager`, every file-backed resource type (`CRes` and its 15 subclasses: sprites, acts, models, maps, palettes, waves), `DataFile`, `XmlDocument`, JPEG decoding |
| `render` | 401 | 71,000 | DirectDraw and Direct3D 7 (`C3dDevice`, `SceneRenderer`), surfaces and textures, render primitives, GDI drawing onto surfaces, fonts, Bink movies, screenshots |
| `world` | 1,152 | 428,769 | the map (`CWorld`, `C3dGround`, scene nodes), the camera (`CView`), the objects placed in the world (the `CGameObject` hierarchy: actors, items, effects), path finding |
| `net` | 63 | 22,975 | `CConnection` and `CRagConnection`, their byte queues (`PacketQueue`), Winsock start-up and clean-up, the server-sent packet filter code, the MD5 code the login handshake uses (`Md5Context`) |
| `ui` | 1,863 | 499,477 | the 153 `UIWindow` classes, `UIWindowMgr`, skins, window snapping, the IME, Thai line breaking, the cursor object (`CMousePointer`) |
| `audio` | 38 | 4,751 | the Miles Sound System wrapper, DirectSound and mixer set-up, `CSoundMaker` |
| `game` | 1,565 | 469,197 | the login and game modes (`CLoginMode`, `CGameMode`), the packet dispatchers and handlers, `ClientState` (character, inventory, options, message tables), guild state, item and skill tables, the chat filter, the mini-game engine (`mg::`) |

The library code is `crt` (728 functions, the static MSVC 6 C runtime from `0x5b325c`)
and `stl` (422, Dinkumware templates in a `std` namespace). Thunks and exception funclets
take the subsystem of the function they belong to.

### How the subsystems were assigned

`tools/assign_subsystems.py` writes the column from the curated rules in
`analysis/subsystems.tsv`. The report is `analysis/exports/Ragexe.exe.subsystems.tsv`, one
row per function with the rule and evidence that placed it.

1. **Seeds.**
   - 3,148 functions are seeded by their class or namespace. A class rule also covers the
     classes derived from it (RTTI bases): `UIWindow` covers all 153 windows and controls.
   - 80 by an address range: three source files identified by their strings (math, frame
     timer, client configuration).
   - 31 by imports that only one subsystem uses: sockets, Miles, DirectDraw, Bink, the
     JPEG library, the IME. Imports used everywhere seed nothing: the UI, for example,
     saves window positions in the registry just as `WinMain` reads its settings there.
   - 3 by single-function rules.
2. **Propagation**, repeated until nothing changes, strongest rule first:
   - *neighbours* (2,364): the nearest assigned functions before and after it agree.
   - *initializer* (45): a dynamic initializer takes the subsystem of the code after it.
   - *callers* (242): every assigned function that references it agrees.
   - *shared* (7): it is referenced from several subsystems, so it is a `core` helper.
   - *callees* (22): everything it references agrees, leaving out shared helpers.
   - *previous* (23): nothing references it, so it takes the function before it.

The order follows the evidence. The tool predicts each class-seeded function from the
other seeds and reports how often each rule agrees with the class:

| Rule | Agrees with the class |
|------|----------------------:|
| neighbours | 99.8% of 3,063 |
| callees | 95.4% of 1,718 |
| callers | 88.9% of 1,623 |

The linker lays out each translation unit's functions together, and a small helper sits
with the file that defines it, whoever calls it. Ordering callers first split the
renderer's file (the fog check, the frame counter, the vertex helpers) four ways.

**Source files.** MSVC 6 emits a translation unit's dynamic initializers ahead of its
functions (`static_init_050` directly precedes `C3dModelRes::C3dModelRes`). Every
initializer in the C++ initializer table therefore starts a file, which gives 141 runs
of code. A run can still hold several files that have no initializer of their own.
- 120 runs have a single subsystem.
- Ten mix in more than 300 bytes of another; most of them hold several classes. The
  tool lists them on each run.
- These runs are the natural first cut of the port's source files.

To change an assignment, add a `function` or `range` row to `analysis/subsystems.tsv`;
`ledger.py set --subsystem` refuses game code.

## 2. Start-up: `WinMain` (`0x5b2280`)

In order:

1. `SetUnhandledExceptionFilter(CrashHandler)`. The crash handler loads `dbghelp.dll` to
   describe the fault.
2. A pending self-update: if the new executable exists, ask to restart into it
   (`0x5b1e80`).
3. The command line must contain `1rag1`; otherwise the client shows an error and exits.
   An optional `/account:<file>` selects the client-info file.
4. `LoadClientInfo` (`0x4ca090`) parses clientinfo.xml into `g_XmlDocument`. It sets:
   - the service type `0x67c9d4`;
   - the server list;
   - the language pointer `g_pCLanguage` (`0x67c99c`). On its first call `0x4ca940`
     builds three function-local statics (`g_CLanguage`, `g_CLanguageTC`,
     `g_CLanguageTH`), then points it at the one for the service type.
5. `LoadVideoSettings` reads `ISFULLSCREENMODE`, `WIDTH`, `HEIGHT` and `BITPERPIXEL` from
   the registry. While that fails, `WinMain` runs `setup.exe` and waits for it.
6. `timeBeginPeriod` with the device's minimum period (at least 1 ms), `ResetFrameTimer`
   and `CoInitialize`.
7. The mutex `Surface`: a second instance exits. The first seven characters of that
   name must add up to 0x2c9, which catches an edited name. For service type 0,
   `0x5b2060` must also succeed: it can show a message and open an FTP download of a
   newer installer with `ShellExecuteA`.
8. `ResourceManager::LoadResNameTable("resNameTable.txt")`, then `CreateMainWindow` and
   `InitAudioAndDevice`. The latter starts Miles when `0x688508` is set, then
   `C3dDevice::Init` (DirectDraw, Direct3D 7, full screen or windowed).
9. `C3dDevice::CreateRenderer` creates the `SceneRenderer` and stores it in
   `g_pSceneRenderer`.
10. `InitWinsock`: `WSAStartup(1.1)`, then `send` and `recv` from `GetProcAddress` into
    function pointers. A `mihosoft <ip> <port>` command line connects at once.
11. `SetFrameRate(60)`, focus, hide the system cursor, then the input devices:
    - `DirectInputMouse::Init`;
    - `KeyPadDispatcher::CreateKeyPad`, with ten keys bound (arrows, Ctrl, Alt, Q, W, A, Z).
12. The UI and data:
    - `UIWindowMgr` is sized to the viewport;
    - `ClientState::method_005889d0` seeds `rand` with `srand(timeGetTime())`, then runs
      three set-up methods;
    - 27 Granny animations go into `g_GrannyMotionTable`;
    - `MannerFilter::Load("manner.txt")`, `TipOfTheDayList::Load`;
    - `CSkinMgr::method_00437150` lists `.\skin\*.*`.
13. `MoviePlayer::Play`: the opening Bink movie, pumping messages while it plays.
14. `ModeSwitcher::Run(&g_ModeSwitcher, 0, "login.rsw")`. This is the rest of the
    program's life (§3).
15. Shutdown (§9).

## 3. Modes

### `ModeSwitcher` (`g_ModeSwitcher`, `0x67c9e8`)

| Offset | Field |
|-------:|-------|
| `+0x0` | running flag |
| `+0x4` | current `CMode *` |
| `+0x8` | current map name |
| `+0x30` | next map name |
| `+0x58` | current mode number |
| `+0x5c` | next mode number |

`Run(mode, map)` loops until the running flag or the quit flag `0x688a68` is cleared:
1. Copy the next mode and map into the current ones.
2. `new` the mode: mode 0 is `CLoginMode` (0x1364 bytes), mode 1 is `CGameMode`
   (0x3bc bytes).
3. Call its slots in order:
   - `OnInit(map)` (vf01);
   - `Run` (vf02), which returns when the mode ends;
   - `OnExit` (vf03);
   - the deleting destructor.

Two calls end a mode:
- `RequestMode(mode, map)` stores the next mode and map, then clears the current mode's
  run flag (`+0x14`). Its callers:
  - the game packet handler `0x527940`, `RequestMode(1, map)`: a map change;
  - the game packet handler `0x527350`, `RequestMode(1, "")`;
  - the game packet handler `0x52b540`, which closes the connection and returns to
    `RequestMode(0, "login.rsw")` in login state 2;
  - `CGameMode::SendMsg`, back to `login.rsw`;
  - the login handler for packet 0x73 (below).
- `Quit()` clears both the run flag and the running flag.

So every map change destroys the `CGameMode` and builds a new one, along with its
`CWorld` and `CView`.

### `CMode` and its two subclasses

| Slot | Name | `CLoginMode` | `CGameMode` |
|------|------|--------------|-------------|
| vf01 | `OnInit(map)` | resets the login state, plays `bgm_01.mp3` | creates `CWorld` and `CView`, loads the map, picks its clear colour |
| vf02 | `Run` | loop below | the same loop |
| vf03 | `OnExit` | stops the samples, releases resources | deletes world and view, releases the map's resources and unused textures |
| vf04 | `OnFrame` | one frame (§4) | one frame (§4) |
| vf05 | `SendMsg(msg, ...)` | 2,969 bytes | the game's command handler, a 23 KB switch |
| vf06 | `OnChangeState` | opens the windows of the new login state | empty |

`CMode` fields:

| Offset | Field |
|-------:|-------|
| `+0x4` | current state |
| `+0x8` | frames spent in it (counted by `CLoginMode::Run`) |
| `+0xc` | pending state, -1 for none |
| `+0x10` | fade-in pending |
| `+0x14` | run flag |
| `+0x2c` | screenshot requested |

`CMode::SendMsg` handles message 2 (quit) and 0x4b (screenshot).

`Run` (both modes) repeats while the run flag is set and the quit flag is clear:
1. When `UIWindowMgr+0x68` is null and a state is pending, make it current, reset the
   frame count and call `OnChangeState`.
2. Call `OnFrame`.

On the way out it calls `FadeOut`.

**Login to game.** The login mode's states (`+0x4`) select its screens through
`OnChangeState`. Packet 0x73 is handled by `0x53e5c0`, which calls `RequestMode(1, map)`.
`DispatchPackets` then returns at once, so the packets behind it in the buffer are left
for the game mode.

## 4. One frame

### `CGameMode::OnFrame` (`0x50d400`)

1. `PumpMessages`: `PeekMessage`, `TranslateMessage` and `DispatchMessage` until the
   queue is empty. `WindowProc` runs here (§5, §6).
2. `CGameMode::DispatchPackets`: every complete packet in the receive buffer goes to
   its handler, through the index table `0x526188` (id − 0x73).
3. While the mode is running:
   - the per-frame game logic: six calls, five of them `CGameMode` methods (mouse input
     comes in through `method_00515d10`);
   - then `GetFrameStepsDue()` world steps, each one `0x4c4a10` (the world), the ground
     (`C3dGround15::method_004bc450`) and the view (`CView` vf04).
4. After the window regains focus in full screen (`0x688a70`):
   `C3dDevice::RestoreSurfaces` and `UIWindowMgr::method_004a8130`.
5. When the window is active (`0x61ec54`) or windowed, and `IsDrawFrame()` (always 1):
   1. clear to the map's colour;
   2. `SceneRenderer::BeginScene`, the view's scene (`CView` vf03), `EndScene`;
   3. the UI (`UIWindowMgr::method_004a8740`);
   4. a second scene for the cursor (`CMode::DrawCursor`) and overlays;
   5. `CMode::DrawFadeIn`;
   6. `SceneRenderer::LockBackBuffer` with `DDLOCK_WAIT` and `UnlockBackBuffer` at once,
      with nothing drawn in between;
   7. `SceneRenderer::Present`.
6. `CMode::SaveScreenshot` when one was requested, then `WorldColorFade::Update`.

`CLoginMode::OnFrame` (`0x53b810`) is the same without the world: messages, packets,
mouse, UI, cursor, fade, present.

### Frame timing

- **Fixed step.** `SetFrameRate(60)` sets a 16 ms period (`1000 / 60`, integer division).
  `GetFrameStepsDue()` returns the whole periods elapsed since the timer base and
  advances the base by them, so the world runs at about 62.5 steps a second. A frame
  that comes late runs several steps, and one that comes early runs none.
- **The `0x61ec58` flag** (initially 1, toggled by a chat command in
  `CGameMode::method_00516a80`). When it is clear, every frame runs exactly one step. The
  same flag makes windowed presentation wait for vertical blank.
- **Presentation.** Drawing is not throttled except by presentation:
  - full screen: `IDirectDrawSurface7::Flip` with `DDFLIP_WAIT`;
  - windowed: a `Blt` to the client area.
- **Time.** `timeGetTime` is the clock everywhere: animation, fades, the cursor, timeouts.
- **Fades.** `CMode::DrawFadeIn` and `FadeOut` fade over 255 ms of wall-clock time, not
  steps.
- **Inactive window.** `SetAppActive` lowers the process to `IDLE_PRIORITY_CLASS` and
  stops drawing (in full screen), but the loop keeps pumping messages and packets.

## 5. Input

- **Keyboard.**
  - Every message first goes to the language object `g_pCLanguage` (its fifth slot), which
    takes the IME messages.
  - `WM_KEYDOWN` goes to `MoviePlayer::OnKey`, then to `UIWindowMgr::method_004b25f0`,
    then to `CLanguage::method_004c8120` if the UI did not take it. `WM_SYSKEYDOWN` goes straight to
    `UIWindowMgr::method_004b25f0`. That is a large switch over the key and the state of
    Ctrl, Alt and Shift, read with `GetAsyncKeyState`: window shortcuts, and the debug
    thread in §7.
  - `WM_CHAR` goes to `CLanguage::method_004c82a0`.
  - `PumpMessages` hands `VK_PROCESSKEY` to the language object's IME handler.
- **Mouse.** DirectInput, not window messages. `DirectInputMouse::method_0041e730` polls
  the SysMouse device once per frame: from `CLoginMode::OnFrame`, and from
  `CGameMode::method_00515d10` in the game. `SetAppActive` acquires and unacquires it.
- **Key bindings.** `KeyPadDispatcher` holds `CKeyPad` objects. Each frame it passes the
  current input record to their virtual functions.

## 6. Networking

Everything runs on the main thread.

1. `CConnection::vf01` registers its socket with
   `WSAAsyncSelect(socket, window, WM_USER+1, FD_READ | FD_WRITE | FD_CONNECT | FD_CLOSE)`,
   so socket events arrive as `0x401` messages.
2. During `PumpMessages`, `WindowProc` passes them to `CRagConnection::OnSocketEvent`:
   - an error is logged by name;
   - `FD_READ` reads with `recv` in 2 KB chunks into the connection's receive buffer;
   - `FD_WRITE` sends the queued data at `+0x24`;
   - `FD_CONNECT` and `FD_CLOSE` are also handled.
   If it fails, `WindowProc` calls the current mode's `SendMsg`.
3. Once per frame each mode's dispatcher calls `CRagConnection::RecvPacket` until no
   complete packet is left:
   - `CGameMode::DispatchPackets`, through the index table `0x526188` (id − 0x73);
   - `CLoginMode::DispatchPackets`: ids 0x69–0x74 through a jump table, 0x81 directly,
     0x187–0x1dc through the index table `0x53d1d8` (id − 0x187).

   Every handler is a method of its mode, `HandlePacket_<id>`, called with the packet:
   - the game mode has 216, the login mode 16, and no handler serves two ids;
   - the game mode skips ids 0x75, 0x76, 0x77, 0x7a, 0x83, 0x84 and 0x187, and the login
     mode skips 0x187;
   - any other id goes to `CRagConnection::vf02`, which empties the receive buffer, so
     an unknown packet also loses the packets queued behind it.

   `CRagConnection::CutPacket` takes one packet at a time off the receive queue. Its
   length comes from a table of 371 ids built by `InitPacketLengths`: fixed, or for 61
   ids the word at offset 2. [protocol/framing.md](protocol/framing.md) describes the
   queues and the table, and `analysis/exports/Ragexe.exe.packets.tsv` lists every id
   with its handler and length.
4. `send` and `recv` are called through the pointers `InitWinsock` fetched. Once the
   server has sent filter code, received and sent data pass through it; see
   [protocol/code-modules.md](protocol/code-modules.md).

## 7. Threads

The client is single-threaded except for three helpers. None of them owns any data.

| Thread procedure | Started by | What it does |
|------------------|-----------|--------------|
| `LazyModelLoadThread` `0x4c33a0` | `CreateThread` in the map loader `0x4c3ab0`, at `THREAD_PRIORITY_HIGHEST` | loads the map's remaining models through `ResourceManager` into `ModelInstance`s and passes each to `0x4c5030`, logging "Load Model : Lazy Model Load Complete". `JoinModelLoadThread` waits for it when the map is torn down. If `CreateThread` fails, the loader runs it inline. |
| `MusicFadeThread` `0x4cb3b0` | `_beginthreadex` in `CMode::DrawFadeIn` and `FadeOut` | steps the music stream's volume by one every 10 ms, up to the volume option (`ClientState+0x648`) or down to 0, then pauses the stream. A newer fade cuts a running one short through a plain counter at `0x67ca4c`, with no atomic operation. |
| `EffectTestThread` `0x4b1460` | `CreateThread` in the key handler, on Ctrl+P when `ClientState::method_00597950(8)` is set | a debug aid that launches effect ids 0–0x9f on the object at `CWorld+0x3c`, sleeping between them. A second sequence of this kind (`0x4b1780`) is never referenced. |

**Synchronisation.** The game code has two critical sections:
- one in `CGPak`, entered by `CGPak::vf01` and `CSakGPak::vf01` (the archive reads);
- one in `ResourceManager`, entered by `GetResource`.

The CRT's `StlLock` covers the STL. Nothing else is locked: the model loader hands its
models over, and the effect thread creates effects, while the main thread updates and
draws the same world.

## 8. Global objects

Each subsystem is reached through global objects; nothing passes them as arguments.

| Object | Address | Class | Role |
|--------|---------|-------|------|
| `g_ModeSwitcher` | `0x67c9e8` | `ModeSwitcher` | current mode, mode requests (§3) |
| `g_ClientState` | `0x686670` | `ClientState` | session, character, inventory, options, message tables; about 260 methods are called on it |
| `g_UIWindowMgr` | `0x641fa8` | `UIWindowMgr` | every window |
| `g_CRagConnection` | `0x628a70` | `CRagConnection` | the server connection |
| `g_C3dDevice` | `0x6239d0` | `C3dDevice` | DirectDraw and Direct3D device |
| `g_pSceneRenderer` | `0x625e98` | `SceneRenderer *` | the renderer `WinMain` creates |
| `g_TextureCache` | `0x625e88` | `TextureCache` | textures by name |
| `g_FontCache` | `0x623c48` | `FontCache` | GDI fonts |
| `GetResourceManager()` | `0x4dfa10`, static `0x67cd88` | `ResourceManager` | resources by name, created on first use |
| `g_CGPak` | `0x67cd00` | `CGPak` | the archive; `g_pCGPak` (`0x67cd84`) points to the one in use |
| `g_DirectInputMouse` | `0x628b48` | `DirectInputMouse` | the mouse |
| `g_KeyPadDispatcher` | `0x628b88` | `KeyPadDispatcher` | key bindings |
| `g_pCLanguage` | `0x67c99c` | `CLanguage *` | code page, IME, text rules; points to `g_CLanguage`, `g_CLanguageTC` or `g_CLanguageTH` (`0x67c8e8`, `0x67c448`, `0x67c3b8`) |
| `g_MoviePlayer` | `0x688480` | `MoviePlayer` | Bink movies |
| `g_GrannyMotionTable` | `0x63edc0` | `GrannyMotionTable` | 3D monster animations |
| `g_CActorPickNode` | `0x67ca60` | `CActorPickNode` | picking under the cursor |
| `g_PathFinder` | `0x684e30` | `PathFinder` | path search |
| `g_WorldColorFade` | `0x686598` | `WorldColorFade` | ambient colour fades |
| `g_GuildInfo` | `0x67cab8` | `GuildInfo` | guild state |
| `g_MannerFilter` | `0x67cbe8` | `MannerFilter` | chat filter |
| `g_TipOfTheDayList` | `0x67ccd0` | `TipOfTheDayList` | tips |
| `g_CSkinMgr`, `g_CSnapMgr` | `0x641cc8`, `0x641d20` | | window skins and snapping |
| `g_XmlDocument` | `0x67c9a0` | `XmlDocument` | clientinfo.xml |
| `g_ThaiWordBreaker` | `0x67bf98` | `ThaiWordBreaker` | Thai line breaking |
| `g_itemInfoById`, `g_cardInfoById`, `g_skillInfoById`, `g_metalProcessItems` | `0x67cc20`–`0x67ccb8` | `std::map` | game data tables |
| resource prototypes | `0x628c00`–`0x641af0` | the 15 `CRes` subclasses | 17 static objects, each registered under a directory and extension (`0x431f70` registers `palette\` and `pal`) |

**How big they are.** None of these classes is ever created with `new`, so no
`operator new` call gives their size. `tools/recover_layouts.py` brackets each one instead:
- **At least:** the end of the last field the class's own functions use.
- **At most:** the start of the next address that is a variable of its own. That is
  either another data label, or an address written by a dynamic initializer that names
  nothing inside the object. Each initializer sets up one variable.

Addresses between the two bounds that only other code uses could be fields or variables.
They are listed in `analysis/exports/Ragexe.exe.layouts.tsv` and left out of the struct.

| Object | At least | At most | What starts at the upper bound |
|--------|--:|--:|--------|
| `g_ClientState` | 6,356 | 6,360 | a float set to 0.25 by `0x586800` |
| `g_UIWindowMgr` | 1,152 | 1,288 | a float set to 0.25 by `0x4a7040` |
| `g_C3dDevice` | 572 | 616 | `g_prevFontByDc` |
| `g_CRagConnection` | 204 | 204 | `g_intMapNil`, the nil node its length map shares with `CKeyPad`'s |

Several files set up their own copy of this float, so it is probably a `static` in a
shared header. In both files, its initializer runs just before the file's object is
constructed. Nothing uses the bytes between `UIWindowMgr`'s last field and its float.

Nine addresses after `C3dDevice` stay undecided:
- `CGameMode::OnInit` sets `+0x23c` to a colour that depends on the map, and
  `OnFrame` passes it to `C3dDevice::method_00405140` every frame.
- `C3dDevice::method_00405f20` writes `+0x240` and `+0x244` by absolute address, which
  hints at variables rather than fields. `ClientState`'s own methods do the same with
  three of its fields, though, so that cannot be a bound.
- From `+0x248` on, only the display-enumeration file (from `0x406100`) uses them, and
  it names nothing else of the object. The tool reports this as the probable end, a test
  that only 2 of the 342 addresses used inside the global objects would pass.

The file's `.data` has no non-zero byte after `0x6239cc`; `g_C3dDevice` (`0x6239d0`) starts
the zero-filled part.

Scalars that the main loop shares:

| Address | Meaning |
|---------|---------|
| `0x688a60` | window handle |
| `0x688a68` | quit requested |
| `0x61ec54` | window active |
| `0x688a70` | surfaces to restore |
| `0x61ec58` | fixed step and windowed vertical-blank wait |
| `0x628bf8`, `0x628bfc` | frame period and timer base |
| `0x67c9d4` | service type |

## 9. Shutdown

When `ModeSwitcher::Run` returns, `WinMain` tears down in this order:
1. Miles (`0x5b09e0`), then `UIWindowMgr::method_004a7ce0`.
2. The connection (`CRagConnection::method_00418670`, `0x4180d0`), then `WSACleanup`
   (`0x418c10`).
3. `UIWindowMgr::method_004a7f80` and `method_004aae90`, then
   `ClientState::SaveOptions`, which writes the options to the registry.
4. `ResourceManager::Clear`, `TextureCache::Clear`.
5. `C3dDevice::DestroyRenderer`, `C3dDevice::Release`.
6. `0x406130`, `0x420150` (in the DirectSound file) and `DirectInputMouse::Release`.
7. `0x5b18f0`: `DestroyWindow` and `CoUninitialize`.
8. `0x5b1de0` deletes `nconfig.ini`, `removenp.exe`, `cfindhack.exe`, `ffindhack.exe`
   and `gfindhack.exe` if they exist.
9. `CoUninitialize` again, and `timeEndPeriod`.

`WinMain` returns 0, and the CRT runs the `atexit` destructors of the global objects.

## 10. Consequences for the port

- **Main loop.** One thread and one loop:
  1. pump events;
  2. drain the socket into the receive buffer and dispatch whole packets;
  3. run game logic, then `GetFrameStepsDue()` fixed 16 ms world steps;
  4. draw.

  Socket readiness can be polled in step 2 instead of arriving as window messages; the
  order in which packets reach the handlers stays the same.
- **Timing quirks.** Keep the integer 16 ms period (62.5 Hz), the catch-up steps after a
  slow frame, and the `0x61ec58` switch. Fades and animations use wall-clock time.
- **Threads.**
  - The model loader can become a background job that hands finished models to the main
    thread, the synchronisation the original lacks.
  - The music fade becomes a per-frame volume ramp.
  - The effect test thread is a debug feature and can be a timed sequence on the main
    thread.
- **Modes.** `ModeSwitcher` and the `CMode` slots map directly onto a state machine in
  `src/game`.
  - A map change rebuilds the game mode, so world state that survives it lives in
    `ClientState`.
  - The singletons become members of an application object passed by reference.
- **Source layout.** The subsystem column and the 141 initializer-bounded runs give a
  first layout for `src/`.
