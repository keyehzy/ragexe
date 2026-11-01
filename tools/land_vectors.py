"""Test vectors for hits landing on actors and players (world::LandGameActorHit,
world::RestartGameActorAttack and world::DeliverGameActorHits, src/world/actor_hit.h, with
world::ShowGameActorNumber and message 0x59 of src/world/actor_effects.h), taken from the
original in the emulation oracle.

usage: .venv/bin/python -I tools/land_vectors.py > tests/unit/land_vectors.inc

On tools/walk_vectors.py's three small maps, scripts as tools/walk_oracle.py's make_landings
makes them (L and O), then on the open field hits whose results each hang on one thing: every
message, sent or delivered; the damage by its edges and the numbers' digits; no damage by the
attacker and its job, the player's status 0x13 and the msg sprites; the sounds by message, job
and weapon; the effects launched, on or off, the turn and the attacker's position; the guild
war and the privilege; numbers of type 0x16 dropped from the list and their slot; the flinch
by state, job, message and motion; +0x210 by the queue; 0x6c; the delivery by time and order.
Then ClientState's sounds of hits (00599a00, 00599df0) by job and kind, and which effects
CAbleToMakeEffect::LaunchEffect makes, with effects on and off.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import path_oracle
import path_vectors
import walk_oracle
import walk_vectors

CLIENT_STATE = 0x686670
PLAYER_HIT_SOUND, WEAPON_HIT_SOUND, SRAND = 0x599A00, 0x599DF0, 0x5C2546
FIELDS = path_oracle.LAND_FIELDS
SEER = 0x186B4  # an id ClientState 00597950 gives privilege 7 on the Korean service


def bits(f):
    return struct.unpack("<I", struct.pack("<f", f))[0]


def deciding(script):
    """Hits made from the first L and O lines of a generated script."""
    bases = {k: next(q for q in script if q[0] == k).split() for k in "LO"}
    lines = []

    def hit(message=0x1A, damage=10, time=0, attacker=9000, cell=(-1, -1), first=-1, second=0, total=1,
            motion=0.5):
        return [message, damage, time, attacker, cell[0], cell[1], first, second, total, bits(motion)]

    def land(hits=None, how=0, player=False, job=None, state=0, d0=0, frames=4, facing=None, service=1,
             attacker=1, pcs=0, ajob=1100, aeffect=0, aweapon=-1, own=9001, statuses=0, war=0, effects_on=1,
             msgs=1, seed=7, f210=0, listed=0, kinds=0, slot=11, events=0o1000, play=0, f1a8=0, weapon=0, f1bc=0,
             updates=12, actor_id=5000, step=100):
        tokens = bases["O" if player else "L"]
        n = 44 if player else 39
        f = tokens[1:1 + n]
        f[7] = str(job if job is not None else (7 if player else 1100))
        f[10], f[17], f[19], f[21], f[34] = str(frames), str(state), str(d0), str(service), str(actor_id)
        f[11], f[12], f[14] = "0", "0", "0"
        f[4] = f"0x{bits(4.0):08x}"
        if facing is not None:
            f[2] = f"0x{bits(facing):08x}"
        count = int(tokens[1 + n + len(FIELDS) - 1])
        server, tick = (int(tokens[1 + n + len(FIELDS) + 10 * count + k]) for k in (0, 1))
        x, z = (struct.unpack("<f", struct.pack("<I", int(f[k], 0)))[0] for k in (0, 1))
        hits = hits if hits is not None else [hit()]
        records = []
        for record in hits:
            record = list(record)
            record[2] = (tick + record[2]) & 0xFFFFFFFF
            records += record
        values = [how, attacker, pcs, ajob, aeffect, aweapon, own, statuses, war, effects_on, msgs, seed, f210,
                  listed, kinds, slot, events, play, tick, f1a8, weapon, f1bc]
        values = [str(v & 0xFFFFFFFF) for v in values]
        values += [f"0x{bits(x + 30.0):08x}", f"0x{bits(z - 20.0):08x}", f"0x{bits(x - 40.0):08x}",
                   f"0x{bits(z + 10.0):08x}", str(len(hits))]
        times = [server, tick]
        for k in range(1, updates + 1):
            times += [(server + step * k) & 0xFFFFFFFF, (tick + step * k) & 0xFFFFFFFF]
        lines.append(("O " if player else "L ") + " ".join(
            f + values + [str(r & 0xFFFFFFFF) for r in records] + [str(t) for t in times]))

    # Every message, sent at once and delivered, for actors and players.
    for player in (False, True):
        for message in (0x1A, 0x27, 0x5D, 0x5E, 0x60, 0x61, 0x6F, 0x6C, 0x81):
            land(how=message, player=player)
            land(hits=[hit(message=message)], player=player)
    # The damage by its edges, and the numbers' digits, white and red.
    for job in (1100, 7):
        for damage in (-7, -1, 0, 1, 9, 10, 99, 100, 99999, 100000, 999999, 1000000, 0x7FFFFFFF, -0x80000000):
            land(how=0x1A, hits=[hit(damage=damage)], job=job)
    for total in (0, 1, 10, 1000000, -5):
        land(how=0x6F, hits=[hit(message=0x6F, damage=3, total=total)])
        land(how=0x6F, hits=[hit(message=0x6F, damage=3, total=total)], job=0x2C)
    # No damage: the attacker by its job's edge, none, the player or the actor itself; the
    # player with status 0x13 or not; the msg sprites on or off.
    for ajob in (0x2C, 0x2D, 7, 14):
        for attacker, own in ((0, 9000), (1, 9000), (2, 9001), (3, 9000), (4, 9001)):
            for aeffect in (0, 0x20):
                land(how=0x1A, hits=[hit(damage=0, attacker=9001)], attacker=attacker, own=own, ajob=ajob,
                     aeffect=aeffect)
    for statuses in (0, 1, 2, 3):
        for own in (5000, 9000):
            land(how=0x1A, hits=[hit(damage=0)], ajob=0x2D, own=own, statuses=statuses)
    for msgs in (0, 1):
        land(how=0x1A, hits=[hit(damage=0)], msgs=msgs)
        land(how=0x5E, hits=[hit(damage=5)], msgs=msgs)
    # The sounds: by message, the actor's job and the attacker's weapon, a CPc's or not.
    for message in (0x1A, 0x60, 0x61):
        for job in (-1, 0, 1, 3, 0x15, 0x16, 1100):
            for pcs, aweapon in ((0, 5), (1, -1), (1, 0), (1, 1), (1, 0xB), (1, 0x16), (1, 1001), (1, 1100),
                                 (1, 1705), (1, 1999)):
                land(how=message, job=job, pcs=pcs, aweapon=aweapon, seed=job & 0xFF)
    for player in (False, True):
        for attacker in (2, 3, 4):
            land(how=0x1A, job=1100 if not player else 0x16, attacker=attacker, own=9000 if attacker != 3 else 1,
                 pcs=2, aweapon=1100, weapon=0, player=player)
    land(how=0x1A, hits=[hit(second=-2)])
    # The effects launched, on or off; +0xa8 by 0x7f; the turn and its wrap; the attacker's
    # position to 0xd8 and 0xd9.
    for first in (-1, 0, 1, 0x22, 0x23, 0x24, 0x7F, 0xA4, 0xA5, 60000, 60001):
        for effects_on in (0, 1):
            land(how=0x1A, hits=[hit(first=first)], effects_on=effects_on)
    for second in (-1, 0, 0x7A, 0xD8, 0xD9, 0x24, 0x7F):
        for attacker in (0, 1, 2):
            land(how=0x1A, hits=[hit(second=second)], attacker=attacker, own=9000 if attacker == 2 else 1)
    for facing in (0.0, 269.0, 270.0, 270.5, 359.0, 400.0, -100.0):
        land(how=0x1A, hits=[hit(second=0x7A)], facing=facing)
    # The guild war, with the privilege or not.
    for war, service, own in ((0, 1, 9000), (1, 1, 9000), (1, 0, SEER), (1, 0, 9000), (1, 1, SEER)):
        land(how=0x1A, war=war, service=service, own=own)
        land(how=0x6F, hits=[hit(message=0x6F, total=20)], war=war, service=service, own=own, job=0x2D)
    # Numbers of type 0x16 dropped from the list, the first in its slot or not.
    for listed, kinds, slot in ((1, 1, 11), (1, 0, 11), (3, 5, 0), (3, 2, 4), (3, 7, 10), (2, 3, 1)):
        land(how=0x6F, hits=[hit(message=0x6F, total=20)], job=0x2D, listed=listed, kinds=kinds, slot=slot)
        land(how=0x1A, job=0x2D, listed=listed, kinds=kinds, slot=slot)
    land(hits=[hit(message=0x6F, total=20, time=100 * k) for k in range(4)], job=0x2D)
    # The flinch by the state before, the job's edge, the message and the motion.
    for job in (0x2C, 0x2D):
        for state in (0, 2, 3, 4, 5):
            for message in (0x1A, 0x27):
                land(how=message, job=job, state=state)
        for motion in (0.0, 0.5, 1.0, 1.0000001, 2.0, -1.0):
            land(how=0x1A, hits=[hit(motion=motion)], job=job)
    land(how=0x1A, d0=1)
    # +0x210 by the queue: the last hit, or more to come.
    for f210 in (0, 1):
        for queued in (0, 1, 2):
            land(how=0x1A, hits=[hit()] * (1 + queued), f210=f210, job=0x2D)
            land(how=0x5E, hits=[hit()] * (1 + queued), f210=f210, job=0x2D)
            land(how=0x6C, hits=[hit()] * (1 + queued), f210=f210, job=0x2D)
        land(hits=[hit(time=0), hit(time=0), hit(time=300)], f210=f210, job=0x2D, updates=6)
    # 0x6c by the state before and the frames' events, for actors and players.
    for player in (False, True):
        for state, d0 in ((0, 0), (3, 0), (0, 1), (4, 0)):
            land(how=0x6C, state=state, d0=d0, player=player, f1a8=9000)
        for events in (0, 0o1, 0o100):
            land(how=0x6C, events=events, player=player, frames=6)
    # The delivery: due or not, in order, an unported message, a time far off, all at once.
    land(hits=[hit(time=-50), hit(time=0), hit(time=1), hit(time=250), hit(time=0x7FFFFFFF)], updates=4)
    land(hits=[hit(message=0x81, time=0), hit(time=0, damage=3)], updates=2)
    land(hits=[hit(damage=d, time=0) for d in (1, 0, -4, 7)], updates=1)
    land(hits=[hit(time=50 * k, message=m) for k, m in enumerate((0x5D, 0x6C, 0x27, 0x5E, 0x60))],
         player=True, updates=6, step=60)
    return lines


def tables(oracle):
    """ClientState's sounds of hits by job and kind, after srand of each seed, and the effects
    LaunchEffect makes."""
    emu = oracle.emu
    lines = ["// ClientState 00599a00 (by the job, `player` 1) and 00599df0 (by the weapon's kind)"]
    lines.append("// after srand(seed): {player, value, seed, the sound}.")
    lines.append("static const HitSoundVector kHitSounds[] = {")
    for player, fn, values in ((1, PLAYER_HIT_SOUND, range(-2, 0x18)), (0, WEAPON_HIT_SOUND, range(-1, 0x17))):
        for value in values:
            for seed in (1, 2, 3, 4, 77):
                emu.call(SRAND, seed)
                name = emu.read_cstr(emu.call(fn, value & 0xFFFFFFFF, this=CLIENT_STATE).eax)
                lines.append(f"    {{{player}, {value}, {seed}, \"{name}\"}},")
    lines.append("};")
    lines.append("// CAbleToMakeEffect::LaunchEffect (00501e10) on an actor: {id, effects on, made, +0xa8}.")
    lines.append("static const LaunchVector kLaunches[] = {")
    actor = emu.scratch(size=path_oracle.ACTOR_SIZE)
    oracle.landing = True
    for effect_id in [-1, 0, 1, 0x22, *range(0x23, 0xA6), 0x100, 60000, 60001, 0x7FFFFFFF, -0x80000000]:
        for effects_on in (0, 1):
            emu.write_u32(actor + 0xA8, 0)
            emu.write_u32(path_oracle.EFFECTS_ON, effects_on)
            made = emu.call(path_oracle.LAUNCH_EFFECT, effect_id & 0xFFFFFFFF, 0, 0, 0, 0, this=actor).eax
            lines.append(f"    {{{effect_id}, {effects_on}, {int(made != 0)}, {emu.read_u32(actor + 0xA8)}}},")
    oracle.landing = False
    emu.write_u32(path_oracle.EFFECTS_ON, 0)
    lines.append("};")
    return lines


def main():
    oracle = path_oracle.Oracle()
    maps = [("open", dict(path_vectors.MAPS)["open"], False), ("walls", dict(path_vectors.MAPS)["walls"], False),
            ("hills", walk_vectors.HILLS, True)]
    lines = ["// Generated by tools/land_vectors.py from the original in the emulation oracle."]
    lines.append("// The maps are tools/walk_vectors.py's (walk_vectors.inc's kWalkMaps). Each map's hits in")
    lines.append("// order (tools/walk_oracle.py's script and output), then chosen ones.")
    lines.append("static const StateQueryVector kLandQueries[] = {")
    for m, (name, rows, hilly) in enumerate(maps):
        width, height = len(rows[0]), len(rows)
        attrs = [path_vectors.ATTRIBUTES[c] for row in rows for c in row]
        script = walk_oracle.make_landings(name, width, height, attrs, 12, updates=(4, 12, 30, 80))
        if name == "open":
            script += deciding(script)
        for query, result in zip(script, oracle.run(walk_vectors.gat(rows, hilly), script)):
            lines.append(f"    {{{m}, \"{query}\", \"{result}\"}},")
    lines.append("};")
    lines += tables(oracle)
    print("\n".join(lines))


if __name__ == "__main__":
    main()
