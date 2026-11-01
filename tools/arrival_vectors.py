"""Test vectors for actors coming into view (world::ArriveGameActor, world::ArriveWalkingGameActor
and world::SetGameActorStatus, src/world/actor_arrival.h), taken from the original in the emulation
oracle.

usage: .venv/bin/python -I tools/arrival_vectors.py > tests/unit/arrival_vectors.inc

On tools/walk_vectors.py's three small maps, scripts as tools/walk_oracle.py's make_arrivals makes
them (CGameActor::SendMsg's messages 3 to 6, then ProcessState update by update), with short
runs; then on the open field, arrivals whose results each hang on one thing: every bit of the
effect state set and cleared, for others than the player, for the player and for those who see
hidden actors on either service; the grouped bits changed while some stay set; every body state
left for every other; every bit of the health state, the player's own blindness among them; the
emblem by job, guild, version and file; message 4's job 0x2d2, its fade-in and its hidden
effect; the facings of odd directions; and message 6's sixteenths and a cell off the map.
The x87 runs at single precision, as in the client.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import path_oracle
import path_vectors
import walk_oracle
import walk_vectors

N = 39  # GO_FIELDS
SEERS = (0x186B4, 0x2933C9, 0x326B70)


def deciding(template):
    """Arrivals made from a generated A line: standing, not held, no fade, in state 0."""
    tokens = template.split()
    fields = tokens[1:1 + N]
    times = tokens[1 + N + len(path_oracle.ARRIVE_FIELDS) + path_oracle.ARRIVE_RECORD:]
    fields[11], fields[12], fields[14], fields[17], fields[19] = "0", "0", "0", "0", "0"
    fields[21] = "1"
    actor_id = 5000
    fields[34] = str(actor_id)
    fields[7] = "3"
    run = times[:6]
    lines = []

    def line(message=3, how=0, body0=0, body=0, effect0=0, effect=0, health0=0, health=0, own=1,
             service=1, player=1, flags=0b1011, guild=0, emblem=0, exists=0, job=None, record_job=3,
             direction=2, cell=(5, 5), subs=(8, 8), emperium=(0, 0, 0), goal=None, held=0, finished=0, record_id=actor_id):
        f = list(fields)
        f[8], f[21], f[14], f[36] = str(effect0), str(service), str(held), str(finished)
        goal = goal or (cell[0] + 3, cell[1] + 2)
        if job is not None:
            f[7] = str(job)
        arrive = [str(message), str(how), str(body0), str(health0), str(flags), "0", "0", str(own),
                  str(exists), str(player), "0x3fc00000", "0xc0100000"]
        if message in (3, 4):
            record = [record_id, record_job, 0, cell[0], cell[1], direction, 150, 11, 12, 13, 0, 0, 0,
                      body, health, effect, 14, *emperium, 0, 0, 0, guild, emblem, 15, 16, 0]
        else:
            record = [record_id, record_job, 0, int(times[0]), cell[0], cell[1], subs[0], subs[1],
                      goal[0], goal[1], 150, 13, 0, 0, body, health, effect, 14, 0, 0, 0, 0, 0,
                      0, guild, emblem, 15, 16]
        return "A " + " ".join(f + arrive + [str(r & 0xFFFFFFFF) for r in record] + run)

    # Each bit of the effect state set and cleared: by others than the player, by the player,
    # and by those who see hidden actors, on the Korean service (0) and another (1); one who
    # does not (0x2100e).
    for bit in range(11):
        for before, after in ((0, 1 << bit), (1 << bit, 0)):
            lines.append(line(effect0=before, effect=after))
            lines.append(line(effect0=before, effect=after, own=actor_id))
            if bit in (1, 2, 6):
                for seer in SEERS:
                    lines.append(line(effect0=before, effect=after, own=seer, service=0))
                lines.append(line(effect0=before, effect=after, own=SEERS[0], service=1))
                lines.append(line(effect0=before, effect=after, own=0x2100E, service=0))
            if bit == 0:
                lines.append(line(effect0=before, effect=after, player=0))
    # The grouped bits changed while one stays set, and bits 4 and 5 with them.
    for before, after in ((0x8, 0x88), (0x88, 0x8), (0x400, 0x408), (0x788, 0x700), (0x10, 0x30),
                          (0x7FF, 0), (0, 0x7FF), (0x30, 0x7F0)):
        lines.append(line(effect0=before, effect=after))
    # Hidden from all by a seer, the body not drawn before.
    lines.append(line(effect=0x40, own=SEERS[0], service=0, flags=0b1001))
    # Every body state left for every other, then for the same; held before, and through
    # message 5 without a path, which no SetAction follows, finished or not before.
    for before, after in ((2, 3), (2, 0), (1, 3), (0, 1), (0, 2), (3, 0)):
        lines.append(line(body0=before, body=after, held=1))
        for finished in (0, 1):
            lines.append(line(message=5, body0=before, body=after, held=1 - finished, finished=finished,
                              goal=(-9, -9)))
    for before in range(8):
        for after in range(8):
            lines.append(line(body0=before, body=after))
        lines.append(line(body0=before, body=before, health=1))
    # Each bit of the health state, the player's own and another's; and none, after one.
    for bit in range(8):
        lines.append(line(health=1 << bit))
        lines.append(line(health=1 << bit, own=actor_id, body=6))
    lines.append(line(health0=0x10, health=0, own=actor_id))
    # The emblem: by GetJob (7 and 14 mounted are 13 and 21), the guild, the version and the
    # file; message 3's, then message 4's for job 0x2d2 from the record's +0x44 to +0x4c.
    for job, effect in ((0, 0), (0x15, 0), (0x16, 0), (-1, 0), (0x504, 0), (0x505, 0), (0x507, 0),
                        (0x508, 0), (0x2D2, 0), (7, 0x20), (14, 0x20), (0x13, 0x20), (0x15, 0x20)):
        lines.append(line(job=job, effect0=effect, effect=effect, guild=7, emblem=2))
    for guild, emblem, exists, job in ((1, 1, 0, 3), (1, 1, 1, 3), (0, 1, 0, 3), (-1, 1, 0, 3),
                                       (1, 0, 0, 3), (1, -1, 0, 3), (1, 0, 0, 0x2D2), (1, -1, 0, 0x2D2),
                                       (0, 0, 0, 0x2D2)):
        lines.append(line(guild=guild, emblem=emblem, exists=exists, job=job))
    for record_job, emperium in ((0x2D2, (3, 1, 2)), (0x2D2, (0, 0, 0)), (0x2D2, (0, 0x8000, 0xFFFF)),
                                 (0x2D2, (-1, 0, 1)), (0x2D1, (3, 1, 2))):
        lines.append(line(message=4, job=0x2D2, record_job=record_job, emperium=emperium, guild=9,
                          emblem=9))
    # Message 4 fading in, and with its effect only when hidden.
    lines.append(line(message=4, how=1))
    lines.append(line(message=4, how=2, effect=0x40))
    lines.append(line(message=3, how=1))
    # The record's id, which messages 3 and 5 leave alone.
    for message in (3, 4, 5, 6):
        lines.append(line(message=message, record_id=777))
    # Odd directions, and a cell off the map.
    for direction in (-1, 0, 7, 8, 9, -5, 0x7FFFFFFF):
        lines.append(line(direction=direction))
    lines.append(line(cell=(-3, 40)))
    # Message 6's sixteenths, its start off the map, and message 5's.
    for subs in ((0, 0), (15, 15), (8, 0), (-20, 40)):
        lines.append(line(message=6, subs=subs))
        lines.append(line(message=5, subs=subs))
    lines.append(line(message=6, cell=(-3, 40)))
    # The flags before, all of them, as a status change leaves them.
    for flags in range(16):
        lines.append(line(flags=flags, effect0=0x46, effect=0))
        lines.append(line(flags=flags, effect0=0, effect=0x46))
    return lines


def main():
    oracle = path_oracle.Oracle()
    maps = [("open", dict(path_vectors.MAPS)["open"], False), ("walls", dict(path_vectors.MAPS)["walls"], False),
            ("hills", walk_vectors.HILLS, True)]
    lines = ["// Generated by tools/arrival_vectors.py from the original in the emulation oracle."]
    lines.append("// The maps are tools/walk_vectors.py's (walk_vectors.inc's kWalkMaps). Each map's searches")
    lines.append("// and arrivals in order (tools/walk_oracle.py's script and output), then chosen ones.")
    lines.append("static const StateQueryVector kArrivalQueries[] = {")
    for m, (name, rows, hilly) in enumerate(maps):
        width, height = len(rows[0]), len(rows)
        attrs = [path_vectors.ATTRIBUTES[c] for row in rows for c in row]
        script = walk_oracle.make_arrivals(name, width, height, attrs, 40, updates=(4, 12, 30, 80))
        if name == "open":
            script += deciding(next(q for q in script if q[0] == "A"))
        for query, result in zip(script, oracle.run(walk_vectors.gat(rows, hilly), script)):
            lines.append(f"    {{{m}, \"{query}\", \"{result}\"}},")
    lines.append("};")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
