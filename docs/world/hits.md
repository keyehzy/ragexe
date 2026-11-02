# Hits landing

An attack queues its hits on the target with message 0x1b ([attack.md](attack.md)), each with
the time it lands. Each update, before `ProcessState`, the classes' `vf01` (`CPc`'s `00542e40`,
`CNpc`'s `005404d0`, `CPlayer`'s `0054b770` and others) call **`CGameActor::DeliverDueHits`**
(`00503f80`), which sends the actor each hit whose time has come. The hit is a message to the
actor's own `CGameActor::SendMsg` (`00504460`, `vf02`, from no sender): 0x1a, 0x27, 0x5d, 0x5e,
0x60, 0x61 and 0x6f land (case `00505326`), and 0x6c, which skills queue
(`ProcessIdActionOnTarget`, `0050b83e`), restarts the attack (`0050579a`). `CPc::SendMsg`
(`00543540`) leaves them all to `CGameActor`'s. The port is `src/world/actor_hit`.

## The delivery

`DeliverDueHits` reads `timeGetTime` once, then walks the list at +0x204 in order. A hit whose
time (+8) is not above it is sent as its own message (+0) with the hit as the first argument,
then unlinked and freed (+0x20c less 1). The others stay.

## A hit landing

The hit is message 0x1b's record ([attack.md](attack.md#the-record)); its message (+0) is not
read again. In order:
1. **Where state 4 slides to:** +0x228 and +0x22c take the centre of the hit's cell (+0x10,
   +0x14; `CGameMode::GetCellCentre`), or the actor's position when either is -1.
2. **The attacker** (+0xc): the player (`CWorld`+0x3c) for the player's id (`ClientState`+0xcdc),
   otherwise what `CWorld::GetActorById` finds. Found, its job (`GetJob`) and its weapon (`vf31`:
   -1 for a `CGameActor`, a `CPc`'s +0x2a0) are kept; otherwise the job is the actor's and the
   weapon -1.
3. **The damage** (+4): 0 for 0x5e; a negative one is made positive and marked.

### No damage

- 0x5e sends the actor message 0x5f (an effect without a slot,
  [effects.md](effects.md)).
- Otherwise, with an attacker: of a job below 0x2d, the attacker is sent 0x59 with 0xffff0000;
  from 0x2d, 0x59 with -1, unless the actor is the player and has status 0x13
  (`ClientState::HasOwnStatus`, `005aa750`, the list at `00641f98` that packet 0x0196 keeps),
  when the actor sends itself 0x7d.
- Message 0x59 (`00504618`) makes an effect with msg.spr (`CGameMode::AddMsgSpriteEffect`,
  `00510440`, which sets its +0x7c to the value) at the attacker's position, into its list, told
  to follow (0x32), but only while `00687ec0` is set, which `CGameMode` `00516a80` toggles.

### Damage

1. **+0x6c** takes `timeGetTime`. With +0x1c -2 (an attack on the attacker itself) +0x83 is
   cleared; otherwise it is set and the hit sounds.
2. **The sound** (`PlayWave`, `004352a0`), from the player, the height 0, then 250, 40 and 1:
   - for 0x60 and 0x61, `ClientState::GetWeaponHitSound(-1)` (`00599df0`);
   - for an actor of jobs 0 to 0x15, `GetPlayerHitSound` of its job (`00599a00`);
   - otherwise `GetWeaponHitSound` of the attacker's weapon, an id past 1000 by its kind
     (`MapRangedIdToKind`).
3. **The effects:** `CAbleToMakeEffect::LaunchEffect` (`00501e10`) of +0x18, then of +0x1c; each
   made is sent 0x2c with 1. +0x1c 0x7a first turns the actor 90 degrees (`005e26b8`, 360 less
   past 360, `005e2a5c`) and launches -1 instead. 0xd8 and 0xd9 are also sent the attacker's
   position (0xe), when there is one.
4. **The number:** messages 0x58 to itself, with the value, its colour and its type:
   - negative: the damage, 0x72 and 0; nothing more is done but step 6;
   - 0x5d: yellow (0xffffff00) and 0x10, then message 0x5c (an effect without a slot);
   - an actor of a job below 0x2d: red (0xffff0000) and 0xe;
   - from 0x2d, 0x6f: white (-1) and 0x15, then the total (+0x20) yellow and 0x16;
   - from 0x2d, the rest: white and 0xe.
5. **The flinch:** for jobs below 0x2d, unless in state 3 or 2 or for 0x27, state 4 (`vf10`, the
   slide, [states.md](states.md)); then the hit's +0x24 becomes +0x4c above 1, otherwise +0x44
   times it. From 0x2d only state 3 and 0x27 keep the actor from state 4, and +0x44 becomes
   +0x24 itself. Above 1 is `fcom` against 1 (`005e25bc`); a NaN is not above.
6. **Last,** for any hit: with +0x210 set and at most one hit queued (the one landing, which is
   still in the list), the actor sends itself 0xc and dies ([states.md](states.md#dying)).

### The sound

`ClientState` `00588a00` fills two tables of `std::string` with `FillPlayerHitSounds`
(`00599620`, at +0x16d4) and `FillWeaponHitSounds` (`00599a40`, at +0x16e4):

| Table | Names |
|-------|-------|
| jobs 0 to 0x15 | player_clothes.wav, but player_metal.wav for 1 and 7, player_wooden_male.wav for 3, 6, 0xb and 0xc |
| kinds 0 to 0x16 | _hit_mace.wav, but _hit_sword.wav for 1 to 3, _hit_spear.wav for 4 and 5, _hit_axe.wav for 6 and 7, _hit_rod.wav for 0xa, _hit_arrow.wav for 0xb |

`GetWeaponHitSound` gives -1 one of _enemy_hit_normal1.wav to 4 and kind 0 one of
_hit_fist1.wav to 4, by `rand() % 4`; past the table it reads on into the heap.
`GetPlayerHitSound` gives other jobs `GetWeaponHitSound(-1)`.

### LaunchEffect

`LaunchEffect` returns no effect unless effects are on (`00687ea8`) and the id is 0 to 60000
(0xea60), or the id is one of its own few, which the byte table at `00502008` marks for ids 0x23
to 0xa4: 0x23, 0x24, 0x3c, 0x72, 0x89, 0x8a, 0x9a, 0x9b, 0x9f, 0xa0 and 0xa4. Id 0x7f first sets
+0xa8.

## Message 0x58: the number

`0050456b`: on a guild war's map (`ClientState::IsGuildWarMap`, `CWorld`+0x60) nothing, unless
the player has privilege 7 (`ClientState::HasAccountPrivilege`, `00597950`). Otherwise
**`CGameActor::ShowNumberEffect`** (`0050beb0`):
1. The value is kept to 0 to 999999, of one to six digits.
2. For type 0x16, **`RemoveMsgEffectsOfType16`** (`0050c100`) first takes each effect of the
   actor's list (+0x270) whose +0x10c is 0x16 out of its slot (`ClearSlotHolding`) and the list,
   sends it 0x18 from the actor and deletes it from the world (`CWorld::DeleteGameObject`).
3. Each digit from the last is a new `CMsgEffect` (0x11c bytes): +0x7c the colour; sent 0x16
   with the actor's id and the type (which its `vf02` keeps at +0x10c), 0x40 with the actor's
   position, the digit and its offset, 4 × (digits − 1) − 8 × its place, and 0x32 from the
   actor to follow it; for type 0x16 +0x3c 0.5 (0x3f000000); then into the world's objects
   (`CGameMode::AddToWorldObjects`) and the actor's list.

## Message 0x6c: the attack again

`0050579a`: state 2 (`vf10`); +0x1ac the action's attack frame (`vf33`, as a float); the cell
reported (`vf11`); the action restarted at +0x70 `timeGetTime`, frame 4, +0x44 1, and the
direction (`vf04` of +0x38) added to +0x24. Then 0xc as a hit's step 6.

## In the port

`src/world/actor_hit.{h,cpp}`, in `ragexe_world`:
- `DeliverGameActorHits`, `LandGameActorHit` and `RestartGameActorAttack`, for actors and players;
- `PlayerHitSound`, `WeaponHitSound`, `LaunchesEffect` and `LaunchGameActorEffect`, which the
  attack's effect 0x83 now goes through too.

`src/world/actor_effects` gained 0x59 and `ShowGameActorNumber` (0x58). `StateContext` gained
the switches (`effects_on`, `msg_sprites`, `guild_war_map`), the player's statuses, the found
actors' weapons and what the launched effects are sent; an effect records its maker
(`EffectMaker`), +0x3c and +0x7c, and a message its arguments and sender. The launched effects
and the effects' own work are not made.

`ragres find-paths` takes `L` and `O`: an actor and a player with hits queued and delivered
update by update, or the first sent at once.

## Checks

`tools/walk_oracle.py` lands hits on actors and players on every map's `.gat` (287): the first
through `SendMsg`, or each through `DeliverDueHits`, then `ProcessState` update by update.
- **The oracle:** the attacker is another `CGameActor` or `CPc`, the player or the actor itself;
  `CMsgEffect::CMsgEffect`, `AddMsgSpriteEffect` and the effect `LaunchEffect` makes are
  stand-ins whose `vf02` is recorded (0x16 keeping its second argument at +0x10c);
  `AddToWorldObjects` is skipped and `DeleteGameObject` recorded; the sound tables are filled by
  the original's own two functions, and the C runtime's thread data, which holds `rand`'s state,
  is made before any query's heap.
- **What varies:** every message, sent or delivered, the damage by its edges, the attacker, its
  job and weapon, the jobs by 0x2d and 0x16, the player's statuses, the guild war and the
  privilege, effects on, the msg sprites, the launched ids and the turn, numbers of type 0x16
  listed before and their slot, the motion, +0x210, the state before and the act's events.
- **What is hashed:** after the message and each update, the fields the states write, the game
  mode's messages, +0x6c, +0x83, +0xa8, +0x228, +0x22c, +0x210, the queue, the sounds, what was
  launched and sent to it, the slots and the three lists, every effect (its maker, type,
  position, id, +0x44, +0x3c, +0x7c and +0x10c), what each was sent and by whom, and what was
  deleted.

The hashes equal `ragres find-paths`:
- **11,480 actors and 11,480 players** (`L`, `O`) over 4,195,761 updates: 9,233 with every hit
  delivered from the queue, 13,727 with the first sent at once (about 1,500 each of 0x1a, 0x27,
  0x5d, 0x5e, 0x60, 0x61, 0x6f, 0x6c and 0x81, which nothing handles);
- **actors:** 2,091 faded or gone and 9,389 stayed; **players:** 897 and 10,583.

The full run, 225,130 queries on all 287 maps, keeps the older counts, and the older vector
files regenerate byte-identical.

Two of the oracle's own slips showed on the way: the C runtime's thread data was made on the
first query's heap, which each run releases, so a later run's effects overwrote `rand`'s state;
and +0xcc, which earlier queries leave, was not cleared for an actor's hits.

`tools/land_vectors.py` writes `tests/unit/land_vectors.inc`, which `tests/unit/walk_test.cpp`
replays: the three maps' scripts, then hits that each hang on one thing (every message sent and
delivered; the damages and digits; no damage by the attacker, its job, status 0x13 and the msg
sprites; the sounds by message, job and weapon; the launched ids on and off, the turn's wrap
and 0xd8 and 0xd9; the guild war and privilege; numbers of type 0x16 dropped and their slots;
the flinch by state, job, message and motion; +0x210 by the queue; 0x6c; the delivery's times
and order); then `kHitSounds`, both sound functions by job and kind after five seeds, and
`kLaunches`, `LaunchEffect` for ids -1 to 0xa5 and past, effects on and off.

What runs around the delivery in `CNpc`'s and `CPc`'s updates, the key colour's end 100 ms
after a hit among it, is in [update.md](update.md).

**Not ported:** what the effects do once made; hits of other messages, which are only taken off
the queue.
