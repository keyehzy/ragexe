# Casting and the windows over actors

An actor carries up to four windows of the UI over its head, each a pointer in the actor that
`CGameActor::SendMsg` (`00504460`, `vf02`) makes and closes:

| Field | Window | Made by | Closed by |
|-------|--------|---------|-----------|
| +0x1c0 | the chat balloon (`UITransBalloonText`) | message 7 (`005061ab`) | its 5 s ending |
| +0x1c4 | its chat room's title (`UIChatRoomTitle`) | 0x20 (`00505fb0`) | 0x21 (`00505e92`) |
| +0x1c8 | its shop's title (`UIMerchantShopTitle`) | 0x37 (`00505ec8`) | 0x36 (`00505e5c`) |
| +0x1cc | the cast's gauge (`UIRechargeGage`) | 0x52 (`0050607e`) | 0x53 (`00506175`) |

Closing is `UIWindowMgr::QueueRemoval` (`004a90c0`) and the field cleared; without a window
the message does nothing. Walking and state 4 close the gauge too ([states.md](states.md)).
`CPc::vf02` (`00543540`) leaves all of these to `CGameActor`'s. The port is
`src/world/actor_window`, with state 13's update in `src/world/actor_state`.

## Where the windows stand

The windows follow the actor's point on screen, which `vf06` keeps at +0x90 and +0x94
([actor.md](../render/actor.md#the-point-on-screen)), less a **rise**: the screen's height
(`g_pSceneRenderer`, `00625e98`, +0x28) × 81 / 480, in integers, as a float, times +0x48, the
sprite's pixel at its depth. Message 0x52 and `00503210` each work the rise out once, on first
use, into function-local statics (`0067cf10`, guarded by bit 0 of `0067cf14`, and `0067cf18`,
by `0067cf1c`); the port works it out each time from the screen as it is. The point is loaded
whole (`fild`) and the product taken from it, rounded once to single precision; positions are
then truncated (`__ftol`).

## Message 0x52: the cast

`0050607e`, which `CGameMode::HandlePacket_013E` (`00530d5e`, the time from the packet) and
`HandlePacket_011A` (`00531410`, 400 × the packet's word at +4, then `SetState(13)`) send:
1. Without a gauge (+0x1cc), one is made (`operator new(0x90)`, `UIRechargeGage::UIRechargeGage`),
   created 0x3c by 6 (`UIWindow::Create`, `004a58e0`), moved (`vf18`) to 0x1e left of the
   point and the rise above it, and added (`UIWindowMgr::AddWindow`, `004a7d60`).
2. **+0x1d8**, the cast's end, is `timeGetTime` plus the message's first value; **+0x1dc**, its
   start, `timeGetTime`. A gauge already there is left where it is.

## Each update: `CGameActor::UpdateWindows`

`00503210`, which the classes' `vf01` call each update ([update.md](update.md): `CPc`'s
`00542e40` after `DeliverDueHits` and before `ProcessState`):
1. **The balloon** while `timeGetTime` is below +0x1a4 (when it last spoke) plus 5000, unsigned:
   x is +0x90 less half its width (+0x1e0, halved toward 0), y the rise less its height (+0x1e4,
   `fisub`). Each is kept from 3 pixels before the screen's edge to 3 pixels past the far edge
   less the window's own size (its +0x1c and +0x20, `g_pSceneRenderer` +0x24 and +0x28): below
   -3 it is -3, then above the far edge it is that. Once the time is up, it is queued for
   removal and +0x1c0 cleared.
2. **The chat room's title** 0x46 left of the point and 17 (`005e9668`) above the rise; **the
   shop's** 0x46 left and 17 above that.
3. **The gauge,** while `timeGetTime` is not past +0x1d8 (unsigned): moved 0x1e left at the rise,
   then filled (`UIRechargeGage::SetAmount`, `00442e40`) to the part done of the whole. The
   whole is +0x1d8 less +0x1dc, 1 when that is negative; the part done is `timeGetTime` less
   +0x1dc as the whole has it (`timeGetTime` + whole − +0x1d8), no more than the whole. Once
   past, the actor sends itself 0x53, which closes it, and in state 8 stands (`vf10`,
   `SetState(0)`).

## State 13: casting

`SetState(13)` (`0050355d`) holds the actor (+0x82) and plays once the action 0x58 or 0x50
(`ClientState::UsesSecondAttack` with the job, +0x1bc and `vf31`) for jobs 0 to 0x15
(`ClientState::IsPlayerJob`), otherwise 0x10 ([states.md](states.md)). `ProcessState`'s state 13
(`00503939`), after the hold and the fade:
1. With +0xcc clear, it is set and `SetState`'s action is played again, held.
2. Once `timeGetTime` reaches +0x1d8 (not below it, unsigned), +0xcc and +0x82 are cleared, the
   actor stands (`vf10`, `SetState(0)`) and sends itself 0x53, closing the gauge.
3. The frame (`vf08`), then the update's times.

`UpdateWindows` runs before `ProcessState`, and closes the gauge only past +0x1d8; state 13 ends
at +0x1d8 itself, so at that update the gauge is filled whole and then closed by state 13.

## In the port

`src/world/actor_window.{h,cpp}`, in `ragexe_world`: `StartGameActorCast` (0x52),
`CloseGameActorWindow` (0x21, 0x36 and 0x53), `StepGameActorWindows` (`00503210`) for actors and
players, and `WindowRise`. `GameActor` gained the four windows, +0x1a4, the cast's times and the
balloon's size, and `RenderObject` +0x48, +0x90 and +0x94. The windows are the port's
`ActorWindows` (`StateContext::windows`): each one's size, by number from 1, and what they were
told (made, moved, added, filled), in order. Messages 7, 0x20 and 0x37, which make the other
three windows with their text, are not ported.

`ragres find-paths` takes `C` and `R`: an actor and a player sent a message, given a state, and
updated by `00503210` then `ProcessState`.

## Checks

`tools/walk_oracle.py` casts on actors and players on every map's `.gat` (287).
- **The oracle:** the windows are stand-ins: `UIRechargeGage`'s constructor, `UIWindow::Create`,
  `UIWindowMgr::AddWindow`, `UIRechargeGage::SetAmount` and the windows' `vf18` are recorded by
  the windows' numbers; `g_pSceneRenderer` is a stand-in with the screen's size, and the two
  statics are worked out again for each query.
- **What varies:** the message (0x52 with durations about their edges, 0x53, 0x21, 0x36 or
  none) and `SetState(13)` or 8 after it; the state before and +0xcc; the windows there, the
  balloon's times and sizes, the cast's times about the updates', the point on screen, +0x48
  (0, negative, NaN, infinite, huge) and the screen's size about their edges; the job, +0x1bc
  and the weapon.
- **What is hashed:** after the message and after each of `00503210` and `ProcessState` in an
  update, the fields the states write, the game mode's messages, the four windows, +0x1a4, the
  cast's times, +0xcc, for a player `CGameMode`+0x2a0 and +0x294, and what each window was told
  and which were queued for removal.

The hashes equal `ragres find-paths`:
- **11,480 actors and 11,480 players** (`C`, `R`) over 3,238,417 updates;
- **actors:** 885 faded, 2,786 settled (out of state 13, no window left) and 7,809 still
  casting or with a window; **players:** 740, 2,800 and 7,940.

The full run, 248,090 queries on all 287 maps, keeps the older counts, and the older vector
files regenerate byte-identical.

`tools/cast_vectors.py` writes `tests/unit/cast_vectors.inc`, which `tests/unit/walk_test.cpp`
replays: the three maps' scripts, then casts that each hang on one thing (every message with
and without its window; the duration's edges and the gauge filling; a negative whole and the
end reached by each of the two; state 13's action by job, +0x1bc and weapon, +0xcc, and the
states the end stands or not; the balloon's 5 s, each screen edge and its width halved; the
titles; the rise by the screen's height and +0x48).

**Not ported:** messages 7, 0x20 and 0x37, which make the balloon and the titles with their
text; the windows themselves (M7).
