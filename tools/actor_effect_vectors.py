"""Test vectors for actors' effects (world::SendGameActorEffectMessage, src/world/actor_effects.h,
and the effects of world::SetGameActorStatus), taken from the original in the emulation oracle.

usage: .venv/bin/python -I tools/actor_effect_vectors.py > tests/unit/actor_effect_vectors.inc

On tools/walk_vectors.py's three small maps, scripts as tools/walk_oracle.py's make_effects makes
them (X, K and B), then on the open field messages whose results each hang on one thing: every
slot's start and end, empty and full, twice; 0x6b; the carts by the effect state, hiding and
cloaked; 0x45's state 10 from states that block it or not, for a player too; the effects
without a slot; the emotions; effects gone from each slot, from the list's first, middle and
last, twice, never made, or from no one; one effect in two slots, for each step of
ClearSlotHolding's order; SetStatus's other messages; then tools/arrival_vectors.py's chosen
arrivals with their effects made and ended, with the slots empty and full before, and those of
message 5 without a path falling asleep.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import arrival_vectors
import path_oracle
import path_vectors
import walk_oracle
import walk_vectors

SLOTTED = [(0x65, 0x66), (0x33, 0x34), (0x38, 0x39), (0x3A, 0x3B), (0x43, 0x44), (0x45, 0x46), (0x47, 0x48),
           (0x49, 0x4A), (0x4B, 0x4C), (0x4E, 0x4F)]
# ClearSlotHolding's order, by slot (+0x244 is 0).
CLEAR_ORDER = [1, 0, 2, 3, 4, 5, 6, 8, 9, 7, 10]


def deciding(script):
    """Messages made from the first X and K lines of a generated script, and B lines from
    tools/arrival_vectors.py's."""
    x = next(q for q in script if q[0] == "X").split()
    k = next(q for q in script if q[0] == "K").split()
    lines = []

    def send(messages, effect=0, slots=0, listed=0, same=0, state=0, d0=0, player=False):
        tokens = k if player else x
        n = 44 if player else 39
        f = tokens[1:1 + n]
        f[8], f[17], f[19], f[34] = str(effect), str(state), str(d0), "5000"
        flat = [str(v) for m in messages for v in (m if isinstance(m, tuple) else (m, 0, 0))]
        lines.append(("K " if player else "X ") + " ".join(
            f + [str(slots), str(listed), str(same), str(len(messages)), *flat] + tokens[-2:]))

    # Every slot's start and end, empty and full, each twice.
    for start, end in SLOTTED:
        send([start, start, end, end, start])
        send([end, start], slots=0x7FF)
    send([0x3A, 0x6B, 0x6B, 0x3A, 0x3B])
    # The carts, by the whole effect state, and not while hiding or cloaked.
    for effect in (8, 0x80, 0x100, 0x200, 0x400, 0x88, 0x408, 0, 0x10, 0x7FF, 2, 4, 6, 0x82, 0x404, 1, 0x40):
        send([0x33, 0x34, 0x33], effect=effect)
    # 0x45's state 10, blocked by state 3 or +0xd0, and for a player through CPc::SetState.
    for state, d0 in ((0, 0), (1, 0), (3, 0), (0, 1), (6, 0), (10, 0), (5, 0)):
        send([0x45], state=state, d0=d0)
    for state in (0, 2, 4, 8, 1, 3):
        send([0x45, 0x46, 0x45], state=state, player=True)
    # The effects without a slot, twice, and the emotions.
    send([0x3E, 0x3F, 0x5C, 0x5F, 0x7D, 0x3E, 0x3F, 0x5C, 0x5F, 0x7D])
    send([(0x15, 0x22, 0), (0x15, 0x22, 0), (0x15, 0, 0), (0x15, 1, 0), (0x15, -1, 0), (0x15, 0x21, 0),
          (0x15, 0x23, 0), (0x15, 0x40, 0)])
    send([(0x15, 0x22, 0)], slots=1 << 10)
    # Effects gone: from each slot, from the list's first, middle and last, twice, one never
    # made, from no one; then a slot made again once its effect has gone.
    for slot in range(11):
        send([(0x30, 0, 1), (0x30, 0, 1)], slots=1 << slot)
    for sender in (1, 2, 3, 4, 5, 0, 999):
        send([(0x30, 0, sender)], slots=0b101, listed=3)
    send([0x65, (0x30, 0, 1), 0x65, (0x30, 0, 2), (0x30, 0, 1)])
    send([0x65, 0x66, (0x30, 0, 1), 0x65])
    send([(0x15, 0x22, 0), (0x30, 0, 1), (0x15, 0x22, 0), 0x3E, (0x30, 0, 3), (0x30, 0, 2)])
    # One effect in two slots: ClearSlotHolding's order, step by step.
    for a, b in zip(CLEAR_ORDER, CLEAR_ORDER[1:]):
        send([(0x30, 0, 1)], slots=1 << a | 1 << b, same=1)
    send([(0x30, 0, 1)], slots=0x7FF, same=1)
    send([(0x30, 0, 1), (0x30, 0, 2)], slots=0x7FF, same=1, listed=1)
    # SetStatus's other messages, which the slots leave alone.
    send([(0x6D, 0xC8, 0), (0x6D, 0x9F, 0), 0x23, 0x69, 0x6A], slots=0x7FF)
    # Arrivals, the slots empty and full before.
    template = next(q for q in script if q[0] == "B").split()
    a = "A " + " ".join(template[1:1 + 39 + 12] + template[1 + 39 + 12 + 3:])
    for line in arrival_vectors.deciding(a):
        tokens = line.split()
        head, tail = tokens[1:1 + 39 + 12], tokens[1 + 39 + 12:]
        for slots in ("0", "2047"):
            lines.append("B " + " ".join(head + [slots, "0", "0"] + tail))
        # Message 5 without a path into sleep, which leaves the actor in its state 10.
        if head[39] == "5" and tail[8] == str(-9 & 0xFFFFFFFF):
            tail = list(tail)
            tail[14] = "4"
            lines.append("B " + " ".join(head + ["0", "0", "0"] + tail))
    return lines


def main():
    oracle = path_oracle.Oracle()
    maps = [("open", dict(path_vectors.MAPS)["open"], False), ("walls", dict(path_vectors.MAPS)["walls"], False),
            ("hills", walk_vectors.HILLS, True)]
    lines = ["// Generated by tools/actor_effect_vectors.py from the original in the emulation oracle."]
    lines.append("// The maps are tools/walk_vectors.py's (walk_vectors.inc's kWalkMaps). Each map's searches,")
    lines.append("// effects and arrivals in order (tools/walk_oracle.py's script and output), then chosen ones.")
    lines.append("static const StateQueryVector kActorEffectQueries[] = {")
    for m, (name, rows, hilly) in enumerate(maps):
        width, height = len(rows[0]), len(rows)
        attrs = [path_vectors.ATTRIBUTES[c] for row in rows for c in row]
        script = walk_oracle.make_effects(name, width, height, attrs, 12, updates=(4, 12, 30, 80))
        if name == "open":
            script += deciding(script)
        for query, result in zip(script, oracle.run(walk_vectors.gat(rows, hilly), script)):
            lines.append(f"    {{{m}, \"{query}\", \"{result}\"}},")
    lines.append("};")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
