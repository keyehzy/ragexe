# Actors' effects

An actor shows some of its statuses with effects that follow it: a cart, a falcon, the ice
of a frozen actor, a curse, sleep or stun over its head. `CGameActor::SendMsg` (`00504460`,
`vf02`) makes and ends them, mostly on the messages that `SetStatus` sends the actor itself
([arrival.md](arrival.md#the-status)); dying sends 0x3b and 0x3e for a frozen body
([states.md](states.md#dying)). The game mode makes each effect, a `CMsgEffect`; the
actor keeps it in a slot for its status and in a list of all its effects. The port is
`src/world/actor_effects`.

The effects themselves, what they draw and when they go, are not ported here.

## Making one

`CGameMode::AddSpriteEffectByType` (`005106a0`) takes a type, the actor's position (+4) and
its id (+0x1b4). The type picks the sprite and the action. Every sprite is in the effect
folder, `이팩트\`:

| Type | Sprite | Made by |
|-----:|--------|---------|
| 1 | `손수레` (`0060908c`), a cart | 0x33, for the effect state 8 and any but the four below |
| 0x10 to 0x13 | `손수레1` to `손수레4` (`00609064` to `00608fec`) | 0x33, for 0x80, 0x100, 0x200 and 0x400 |
| 2 | `emotion` (`006071fc`) | 0x38 |
| 3, 4 | `얼음땡` (`00614b60`), ice | 0x3a, 0x3e |
| 5 | `smoke` (`00614b38`) | 0x3f |
| 6 | `Status-curse` (`00614b0c`) | 0x43 |
| 7 | `Status-sleep` (`00614adc`) | 0x45 |
| 8 | `Status-stun` (`00614aac`) | 0x47 |
| 9 | `emotion` | 0x49 |
| 10, 0xb | `msg` (`00614a84`) | 0x4b, 0x4e |
| 0xc, 0xd, 0x14 | `msg` | 0x5c, 0x5f, 0x7d |
| 0xe | `매` (`00614b98`), a falcon | 0x65 |

`CGameMode::AddEmotionEffect` (`005111b0`) makes an emotion the same way, from
`emotion.spr`, with the position, the emotion and the id.

Then, for every effect made:
1. it joins the end of the actor's list (`std::list` at +0x270, its head at +0x274 and its
   size at +0x278);
2. it is sent message 0x32 from the actor (its `vf02`), to follow it.

## The slots

Eleven slots, +0x244 to +0x26c, hold an effect each. A message that makes one does nothing
while its slot is full. The message that ends one sends the slot's effect 0x35 from no sender
and empties the slot. Nothing happens on an empty slot. The effect stays in the list.

| Slot | Made | Ended | Type | Shows (from `SetStatus`) |
|-----:|-----:|------:|-----:|-------|
| +0x244 | 0x65 | 0x66 | 0xe | bit 4 of the effect state |
| +0x248 | 0x33 | 0x34 | the cart's | bits 3 and 7 to 10 of the effect state |
| +0x24c | 0x38 | 0x39 | 2 | |
| +0x250 | 0x3a | 0x3b, 0x6b | 3 | body state 2 |
| +0x254 | 0x43 | 0x44 | 6 | bit 1 of the health state |
| +0x258 | 0x45 | 0x46 | 7 | body state 4 |
| +0x25c | 0x47 | 0x48 | 8 | body state 3 |
| +0x260 | 0x49 | 0x4a | 9 | bit 2 of the health state |
| +0x264 | 0x4b | 0x4c | 10 | bit 5 of the health state |
| +0x268 | 0x4e | 0x4f | 0xb | bit 6 of the health state |
| +0x26c | 0x15 with 0x22 | | the emotion 0x22 | |

Some of them do more:
- **0x33** (`00504824`) does nothing while bit 1 or 2 of the effect state (+0x21c) is set
  (`IsBit1Set`, `IsBit2Set`). Its type is the cart that the whole effect state names, not
  its bits: 0x88 makes type 1.
- **0x65** (`00504995`) writes 6.0 (`0x40c00000`) to its effect's +0x44, after the 0x32.
- **0x45** (`00504cda`) then sets state 10 through `vf10` (`SetState`, `CPc`'s for a player).

## Without a slot

0x3e (type 4), 0x3f (5), 0x5c (0xc), 0x5f (0xd) and 0x7d (0x14) make an effect each time.
**0x15** makes the emotion of its argument the same way, except 0x22, which goes in the slot
at +0x26c.

## Gone

An effect that goes sends the actor **0x30** from itself. `SendMsg` (`005044e6`) finds the
first node of the list that holds it, then:
1. `ClearSlotHolding` (`00504090`) empties the first slot that holds it, looking at +0x248,
   +0x244, +0x24c to +0x25c, +0x264, +0x268, +0x260 and +0x26c in that order;
2. the node is taken out of the list.

An effect the list doesn't hold changes nothing.

## The rest

A message that does nothing above (an empty slot's end, a full slot's start, an effect not
listed) falls through, as `SendMsg`'s other messages do, to `CAbleToMakeEffect::SendMsg`
(`005020d0`), then `CRenderObject::SendMsg` (`00500e40`) and `CGameObject::SendMsg`
(`005002a0`), which does nothing.

`CAbleToMakeEffect::SendMsg` handles the effects it launched, in its own list at +0xac (its
head at +0xb0):
- **0x6d,** which `SetStatus` sends, ends the launched effect whose id (its +0xac) is the
  argument, with message 0x18;
- 0x17, 0x54 to 0x57 and 0x78 also work on that list.

The port launches no effects (`LaunchEffect` is not ported), so it has none to end.

`CPc::vf02` (`00543540`) leaves all these to `CGameActor`'s.

## In the port

`src/world/actor_effects.{h,cpp}`, in `ragexe_world`:
- `SendGameActorEffectMessage`, for actors and players, takes the message, its argument and
  the effect that sends it;
- `EffectSlot` names the slots.

`StateContext::effects` (`ActorEffects`) holds the effects made, which the actor names by
number, from 1, with what they were sent, in order (`EffectMessage`). Without it nothing is
made or ended. An `ActorEffect` keeps:
- what made it (`emotion`) and its type;
- the actor's id when it was made;
- the position it follows;
- +0x44.

`GameActor` gained the slots (`effect_slots`) and the list (`effects`).

`SetGameActorStatus` sends each of its messages through `SendGameActorEffectMessage` too, so
an arrival ([arrival.md](arrival.md)) makes and ends the actor's effects as it goes. It takes
`timeGetTime` for 0x45's `SetState`.

`ragres find-paths` takes:
- `X` and `K`, a `G` and a `P` sent messages, after the effects before;
- `B`, an `A` with its messages to itself made and ended.

## Checks

`tools/walk_oracle.py` also sends actors and players effect messages on every map's `.gat`
(287), through the original's `vf02`, and brings actors into view with their own `vf02`.
- **The oracle:** `AddSpriteEffectByType` and `AddEmotionEffect` make stand-ins with a `vf02`
  that records, and the actor's lists start empty.
- **What varies:**
  - the slots full or empty before, sometimes one effect in many, and more in the list;
  - every start and end;
  - the effects without a slot, and emotions (0x22 among them);
  - effects gone that the actor has, had or never had;
  - for actors, `SetStatus`'s other messages;
  - the effect state: the carts, hiding and cloaked;
  - the state before, for 0x45's `SetState`.
- **What is hashed:** after each message, every field the states write, the game mode's
  messages, the slots and the list by number, what made each effect, its type, position, id
  and +0x44, and what each was sent and by whom.

The hashes equal `ragres find-paths`:
- **11,480 actors and 11,480 players** (`X`, `K`) sent 332,568 messages;
- **11,480 arrivals** (`B`) over 1,453,200 updates: 2,136 changed state, 632 faded and 8,712
  stayed.

`tools/actor_effect_vectors.py` writes `tests/unit/actor_effect_vectors.inc`, which
`tests/unit/walk_test.cpp` replays. It holds the three maps' scripts, then messages that
each hang on one thing:
- every slot's start and end, empty and full, twice, and 0x6b;
- the carts by the effect state, hiding and cloaked;
- 0x45's state 10, blocked by state 3 or +0xd0 or not, for players too;
- the effects without a slot and the emotions;
- effects gone from each slot, from the list's first, middle and last, twice, never made or
  from no one;
- one effect in two slots, for each step of `ClearSlotHolding`'s order;
- `SetStatus`'s other messages;
- [arrival.md](arrival.md#checks)'s chosen arrivals with the slots empty and full before, and
  message 5 without a path into sleep, which stays in state 10.
