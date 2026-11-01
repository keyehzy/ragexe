"""Test vectors for actors acting on a target (world::ActOnTarget, world::QueueGameActorHit and
states 2, 4, 7, 9 and 13 of world::SetGameActorState and world::SetPlayerState,
src/world/actor_attack.h and actor_state.h), taken from the original in the emulation oracle.

usage: .venv/bin/python -I tools/attack_vectors.py > tests/unit/attack_vectors.inc

On tools/walk_vectors.py's three small maps, scripts as tools/walk_oracle.py's make_attacks
makes them (V and J), then on the open field attacks whose results each hang on one thing:
every type; the target found, the actor itself, the player by its id (another or the actor
itself) or none, away from the player; the hits' counts and damages by their edges; the second
damage by the job's edge; +0xcc; the motion by its limit and a player's bow, and none, with an
update at once; the late hits' jobs; the target's motion; the act's frames' events; each state
through vf10, for players by job and weapon, blocked or not; a hit queued with a total or none,
and hits queued before.
Then ClientState's weapon kinds (005aad00) by their ranges' edges, and which action and frame a
player's attack takes (0059a440, 0059a3e0) by job, +0x1bc and weapon.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import path_oracle
import path_vectors
import walk_oracle
import walk_vectors
from unicorn.x86_const import UC_X86_REG_FPSW, UC_X86_REG_FPTAG

CLIENT_STATE = 0x686670
WEAPON_KIND, SECOND_ACTION, ATTACK_FRAME = 0x5AAD00, 0x59A440, 0x59A3E0


def bits(f):
    return struct.unpack("<I", struct.pack("<f", f))[0]


def deciding(script):
    """Attacks made from the first V and J lines of a generated script."""
    v = next(q for q in script if q[0] == "V").split()
    j = next(q for q in script if q[0] == "J").split()
    lines = []

    def act(how=0xB, record=(10, 0, 0, 7000, 0, 1, -1, -1, 300, 200), enter=0, player=False, job=None,
            state=0, d0=0, frames=4, f0cc=0, f1a8=0, found=1, own=9000, player_is=1, f1bc=0, weapon=0,
            events=0o1000, play=0, queued=0, updates=12, actor_id=5000, again=False, window=0):
        tokens = j if player else v
        n = 44 if player else 39
        f = tokens[1:1 + n]
        f[7] = str(job if job is not None else (7 if player else 1100))
        f[10], f[17], f[19], f[34] = str(frames), str(state), str(d0), str(actor_id & 0xFFFFFFFF)
        f[11], f[12], f[14] = "0", "0", "0"
        f[4] = f"0x{bits(4.0):08x}"
        if player:
            f[41] = str(window)
        server, tick = int(tokens[1 + n + 27]), int(tokens[1 + n + 28])
        x, z = (struct.unpack("<f", struct.pack("<I", int(f[k], 0)))[0] for k in (0, 1))
        fields = [how, enter, f0cc, f1a8, found, f"0x{bits(x + 30.0):08x}", f"0x{bits(z - 20.0):08x}", own,
                  player_is, f"0x{bits(x - 40.0):08x}", f"0x{bits(z + 10.0):08x}", f1bc, weapon, events, play,
                  tick, queued]
        times = [server, tick] * (2 if again else 1)
        for k in range(1, updates + 1):
            times += [(server + 100 * k) & 0xFFFFFFFF, (tick + 100 * k) & 0xFFFFFFFF]
        lines.append(("J " if player else "V ") + " ".join(
            f + [str(x) if isinstance(x, str) else str(x & 0xFFFFFFFF) for x in fields]
            + [str(r & 0xFFFFFFFF) for r in record] + [str(t) for t in times]))

    def record(damage=10, second=0, kind=0, target=7000, count=1, cell=(-1, -1), motion=300, target_motion=200):
        return (damage, second, kind, target, 0x5A5A5A5A, count, cell[0], cell[1], motion, target_motion)

    # Every type, on an actor found, for actors and players.
    for player in (False, True):
        for kind in (-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12):
            act(record=record(kind=kind, count=2), player=player)
    # The target: found, the actor itself, none (away from the player), the player by its id,
    # another or the actor itself.
    for kind in (0, 8):
        act(record=record(kind=kind, count=3), found=0)
        act(record=record(kind=kind, target=5000), found=2)
        act(record=record(kind=kind, target=5000), found=1)
        act(record=record(kind=kind, target=9000), player_is=1)
        act(record=record(kind=kind, target=9000), player_is=2, own=9000)
        act(record=record(kind=kind, target=5000), player_is=2, own=5000, found=2)
    # The hits' counts and damages by their edges.
    for kind in (0, 8, 9):
        for count in (-1, 0, 1, 2, 3, 35, 36, 37):
            act(record=record(kind=kind, count=count, damage=7))
        for damage in (-7, 0, 1, 0x7FFFFFFF, -0x80000000):
            act(record=record(kind=kind, count=3, damage=damage))
    # The second damage, by the job's edge (1000), with one hit and with three.
    for job, player in ((999, False), (1000, False), (7, True), (0x15, True)):
        for count, kind in ((1, 0), (3, 8), (3, 0)):
            for damage in (9, 0):
                act(record=record(kind=kind, count=count, damage=damage, second=5), job=job, player=player)
        # The total back to 0 by the second damage: the hit keeps what the others made of it,
        # but its message.
        act(record=record(kind=8, count=3, damage=3, second=-3), job=job, player=player)
    # +0xcc, which launches effect 0x83, with type 11 and the others.
    for kind in (0, 11, 10, 3):
        act(record=record(kind=kind), f0cc=1)
        act(record=record(kind=kind), f0cc=0)
    # The motion by its limit (432); for players with a bow, by kind or id, or not.
    for motion in (-5, 0, 1, 431, 432, 433, 5000):
        act(record=record(motion=motion))
    # No motion makes no delay; an update at once is then 0 / 0 frames, which plays on.
    for player in (False, True):
        act(record=record(motion=0), player=player, again=True)
    for weapon in (0xB, 1705, 0x17, 0x18, 0x10000 + 1705, 0, 1750):
        act(record=record(motion=5000), player=True, weapon=weapon)
    # The late hits' jobs.
    for job in (0x3F7, 0x3F8, 0x3FC, 0x42C, 0x504, 0x505, 0x506, 0x507, 0x508):
        act(record=record(kind=8, count=2), job=job)
    # The target's motion, over 288.
    for target_motion in (0, 1, 288, -7, 1 << 30, 0x7FFFFFFF, 16777217):
        act(record=record(target_motion=target_motion))
    # The cell, to the hits.
    act(record=record(cell=(3, 4)))
    act(record=record(cell=(0x7FFFFFFF, -2)))
    # The act's frames' events, which give the attack's frame and so the first hit's time.
    for frames in (0, 1, 2, 3, 5, 12):
        for events in (0, 0o1, 0o10, 0o100, 0o1432, 0o2340, 0o5671, 0o7777777777, 0o1000000000):
            act(record=record(), frames=frames, events=events, updates=2)
    # Each state through vf10, for actors and players, blocked or not, and from state 4.
    for player in (False, True):
        for enter in (2, 4, 7, 9, 13):
            for state, d0 in ((0, 0), (6, 0), (3, 0), (0, 1), (4, 0), (8, 0)):
                # State 13's update is not ported.
                blocked = state == 3 or d0 or (enter == 7 and state == 6)
                act(how=0, enter=enter, state=state, d0=d0, player=player, f1a8=7000,
                    updates=0 if enter == 13 and not blocked else 20)
        for f1a8, found, player_is in ((7000, 0, 1), (9000, 1, 1), (9000, 1, 2), (0, 1, 1)):
            act(how=0, enter=2, f1a8=f1a8, found=found, player_is=player_is, player=player)
    # An actor of job 0x505, which shoots an arrow as it attacks, at +0x1a8 or none.
    for f1a8 in (7000, 0):
        for enter in (2, 7, 9):
            act(how=0, enter=enter, job=0x505, f1a8=f1a8)
        act(record=record(target=f1a8), job=0x505)
    # Players' 2 and 9 by job and weapon: the second action, the frame, and the arrows of a bow
    # (2) and weapons 0xd and 0xe (9).
    for job, f1bc, weapon in ((7, 0, 0xB), (7, 0, 1705), (11, 0, 0xB), (11, 0, 0x10000 + 1705), (1, 0, 4),
                              (12, 0, 0x10), (12, 0, 0x16), (0, 1, 2), (0, 0, 1), (5, 0, 0), (6, 0, 0),
                              (9, 1, 1), (9, 0, 10), (19, 0, 0xD), (19, 0, 0xE), (20, 0, 0xE), (1, 0, 1905),
                              (8, 0, 0xF), (3, 0, 0xB), (3, 0, 2)):
            for enter in (2, 9):
                act(how=0, enter=enter, player=True, job=job, f1bc=f1bc, weapon=weapon, f1a8=7000, window=77)
    # Players' 7 by job and +0x1bc.
    for job, f1bc in ((0x13, 0), (0x14, 0), (0xE, 0), (0xF, 0), (0xF, 1), (7, 0), (0x15, 1)):
        act(how=0, enter=7, player=True, job=job, f1bc=f1bc)
    # 13 by job (players' below 0x16) and weapon.
    for job, player, weapon, f1bc in ((0x15, False, 0, 0), (0x16, False, 0, 0), (1100, False, 0, 0),
                                      (15, False, 0, 0), (3, False, 0, 0), (0, False, 0, 0),
                                      (1, True, 4, 0), (1, True, 1, 0), (0, True, 2, 1), (14, True, 5, 0)):
        act(how=0, enter=13, player=player, job=job, weapon=weapon, f1bc=f1bc, updates=0)
    # A hit queued with a total or none, onto hits queued before.
    for total in (0, 5, -5, 0x7FFFFFFF):
        for queued in (0, 2):
            act(how=0x1B, enter=total, queued=queued,
                record=(0x1A, 33, 123456, 77, 2, 3, -1, 0, 1, 0x3F800000), updates=1)
    # Hits queued before on all three, and an attack of three hits.
    for queued in (1, 2):
        act(record=record(kind=8, count=3), queued=queued)
        act(record=record(kind=8, count=3, target=9000), queued=queued, player=True)
    # Played to the end and standing again.
    for player in (False, True):
        act(record=record(kind=0), frames=3, updates=40, player=player, window=77)
    return lines


def tables(emu):
    """ClientState's weapon kinds by id and the players' attack choices by job, +0x1bc and
    weapon."""
    lines = ["// ClientState::MapRangedIdToKind (005aad00): {id, kind}."]
    lines.append("static const WeaponKindVector kWeaponKinds[] = {")
    ids = {0, 1, -1, 0x16, 0x17, 0x7FFFFFFF, -0x80000000, 0x10000 + 1705}
    for edge in range(1100, 2001, 50):
        ids |= {edge - 1, edge, edge + 1}
    for i in sorted(ids):
        kind = emu.call(WEAPON_KIND, i & 0xFFFFFFFF, this=CLIENT_STATE).eax
        lines.append(f"    {{{i}, {kind - (kind >> 31 << 32)}}},")
    lines.append("};")
    lines.append("// ClientState 0059a440 and 0059a3e0: {job, +0x1bc, weapon, the second action, the frame")
    lines.append("// by the first and by the second (float bits)}.")
    lines.append("static const AttackChoiceVector kAttackChoices[] = {")
    weapons = [-1, *range(0, 0x19), 1100, 1150, 1200, 1250, 1300, 1350, 1400, 1450, 1500, 1550, 1600, 1700,
               1750, 1800, 1900, 1950, 0x10000 + 1705, 0x10000 + 1100]
    for job in [-1, *range(0, 0x17), 0x7FFFFFFF]:
        for f1bc in (0, 1, 2):
            for weapon in weapons:
                second = emu.call(SECOND_ACTION, job & 0xFFFFFFFF, f1bc, weapon & 0xFFFFFFFF,
                                  this=CLIENT_STATE).eax & 0xFF
                frames = []
                for by in (0, 1):
                    emu.call(ATTACK_FRAME, job & 0xFFFFFFFF, f1bc, weapon & 0xFFFFFFFF, by, this=CLIENT_STATE)
                    frames.append(bits(emu.st0()))
                    emu.uc.reg_write(UC_X86_REG_FPSW, 0)
                    emu.uc.reg_write(UC_X86_REG_FPTAG, 0xFFFF)
                lines.append(f"    {{{job}, {f1bc}, {weapon}, {second}, 0x{frames[0]:08x}, 0x{frames[1]:08x}}},")
    lines.append("};")
    return lines


def main():
    oracle = path_oracle.Oracle()
    maps = [("open", dict(path_vectors.MAPS)["open"], False), ("walls", dict(path_vectors.MAPS)["walls"], False),
            ("hills", walk_vectors.HILLS, True)]
    lines = ["// Generated by tools/attack_vectors.py from the original in the emulation oracle."]
    lines.append("// The maps are tools/walk_vectors.py's (walk_vectors.inc's kWalkMaps). Each map's searches")
    lines.append("// and attacks in order (tools/walk_oracle.py's script and output), then chosen ones.")
    lines.append("static const StateQueryVector kAttackQueries[] = {")
    for m, (name, rows, hilly) in enumerate(maps):
        width, height = len(rows[0]), len(rows)
        attrs = [path_vectors.ATTRIBUTES[c] for row in rows for c in row]
        script = walk_oracle.make_attacks(name, width, height, attrs, 12, updates=(4, 12, 30, 80))
        if name == "open":
            script += deciding(script)
        for query, result in zip(script, oracle.run(walk_vectors.gat(rows, hilly), script)):
            lines.append(f"    {{{m}, \"{query}\", \"{result}\"}},")
    lines.append("};")
    lines += tables(oracle.emu)
    print("\n".join(lines))


if __name__ == "__main__":
    main()
