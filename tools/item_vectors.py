"""Test vectors for the items on the ground and the corpses (world::ArriveGroundItem,
UpdateGroundItem, GroundItemShadowScale, MakeCorpse and UpdateCorpse, src/world/ground_item.h:
CItem and CCorpse), taken from the original in the emulation oracle.

usage: .venv/bin/python -I tools/item_vectors.py > tests/unit/item_vectors.inc

On tools/walk_vectors.py's three small maps, scripts as tools/walk_oracle.py's make_items and
make_corpses make them (i and k), then on the open field and the hills queries whose results
each hang on one thing: the blink's ticks 89 to 92 and a period on; the fall's last ticks on
flat ground and on a slope; the sixteenths at a cell's corner, centre and past it; the names
identified or not, known or not, cut at 31 bytes; the tables missing one by one; each format;
the messages other than 4; shown or not; on a fourth map, flat at -1000000 (DEEP), a fall whose
421st millisecond lands exactly on the ground, which keeps it falling. For corpses the ticks 254 to 257 each alone (the alpha
-1, kept), the frame counts 0 to 3, the directions about the camera's yaw.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import path_oracle
import path_vectors
import walk_oracle
import walk_vectors

TABLES = [b"501#Red_Potion#\r\n0#Nothing#\r\n909#\xc1\xa9\xb7\xce\xc7\xc7#\r\n",
          b"501#Red_Potion?#\r\n909#Jellopy?#\r\n",
          b"501#\xbb\xa1\xb0\xa3\xc6\xf7\xbc\xc7#\r\n909#jellopy#\r\n",
          b"501#red_potion#\r\n0#nothing#\r\n"]
FORMAT = b"%s: %d ea"
START = 100000
DEEP = -1000000.0  # the fourth map's height, 4 by 4 cells


def item(name, fields, ticks, tables=TABLES, label=FORMAT):
    """An i query: fields over ITEM_FIELDS' defaults, a message 4 at START."""
    f = dict(message=4, id=77, cell_x=5, cell_y=4, sub_x=8, sub_y=8, dropped=0, count=5, identified=1, shown=1,
             scale=walk_oracle.bits(1.0), made=START - 50, at=START)
    f.update(fields)
    texts = [t.hex() if t else "-" for t in tables] + [label.hex() or "-"]
    return "i " + " ".join([name.hex() or "-"] + [str(f[k] & 0xFFFFFFFF) for k in path_oracle.ITEM_FIELDS] + texts
                           + [str((START + t) & 0xFFFFFFFF) for t in ticks])


def corpse(fields, ticks):
    """A k query: fields over CORPSE_FIELDS' defaults, made at START."""
    f = dict(x=walk_oracle.bits(2.5), y=walk_oracle.bits(-1.0), z=walk_oracle.bits(-2.5), action=8, frame=0,
             facing=walk_oracle.bits(0.0), yaw=walk_oracle.bits(0.0), frames=5, made=START)
    f.update(fields)
    return "k " + b"poring.act".hex() + " - " + " ".join(str(f[k] & 0xFFFFFFFF) for k in path_oracle.CORPSE_FIELDS) + " " + " ".join(
        str((START + t) & 0xFFFFFFFF) for t in ticks)


def deciding(hilly):
    lines = []
    # The blink: ticks 89.96, 90, 90.96, 91, 91.96 and 92, each alone, and a period on.
    for t in (2159, 2160, 2183, 2184, 2207, 2208, 2159 + 2208, 2160 + 2208, 2207 + 2208, 2208 + 2208):
        lines.append(item(b"501", {}, [t]))
    # The fall: from 15 above at -0.6, on every tick about its end, on a cell's corner, centre and edge.
    for sub in ((8, 8), (0, 0), (15, 15), (3, 12)):
        lines.append(item(b"501", dict(dropped=1, sub_x=sub[0], sub_y=sub[1]), [24 * k for k in range(25)]))
    lines.append(item(b"501", dict(dropped=1), [24 * 17, 24 * 18]))
    lines.append(item(b"501", dict(dropped=-1), [5000]))
    if hilly:
        return lines
    # The place: sixteenths past the cell, off the map.
    for sub in ((16, 8), (-1, 8), (8, 40), (-20, -20)):
        lines.append(item(b"501", dict(sub_x=sub[0], sub_y=sub[1]), [0]))
    lines.append(item(b"501", dict(cell_x=-2, cell_y=99), [0]))
    # The names: identified or not, known or not, the fallback, item 0, cut at 31 bytes.
    for name in (b"501", b"909", b"0", b"12", b"", b" 501", b"501abc", b"0" * 31 + b"9", b"0" * 30 + b"909"):
        for identified in (0, 1, 256):
            lines.append(item(name, dict(identified=identified), [0]))
    # Each table missing.
    for k in range(4):
        tables = list(TABLES)
        tables[k] = b""
        for identified in (0, 1):
            lines.append(item(b"909", dict(identified=identified), [0], tables=tables))
    lines.append(item(b"501", {}, [0], tables=[b""] * 4))
    # The formats and counts.
    for label in walk_oracle.ITEM_FORMATS:
        for count in (0, 7, -3, 123456):
            lines.append(item(b"501", dict(count=count), [0], label=label))
    # Messages other than 4, shown or not, the scale.
    for message in (0, 3, 5, 0x4C, -1):
        lines.append(item(b"501", dict(message=message, dropped=1), [0, 100]))
    for shown, scale in ((0, 1.0), (1, 0.0), (1, 1.25), (1, 0.3), (0, 2.0)):
        lines.append(item(b"501", dict(shown=shown, scale=walk_oracle.bits(scale)), [0]))
    # Corpses: ticks 254.96 to 257 each alone, then through the end.
    for t in (6119, 6120, 6143, 6144, 6167, 6168, 6191, 6192, 0, 24, 100000, -24):
        lines.append(corpse({}, [t]))
    lines.append(corpse({}, [24 * k for k in range(0, 260, 4)]))
    # The frame counts and the directions.
    for frames in (0, 1, 2, 3):
        lines.append(corpse(dict(frames=frames, frame=7), [0, 1000]))
    for facing in (0.0, 22.5, 22.6, 45.0, 90.0, 180.0, 315.0, 337.5, 359.9, -45.0, 405.0):
        for yaw in (0.0, 45.0, 200.0):
            lines.append(corpse(dict(facing=walk_oracle.bits(facing), yaw=walk_oracle.bits(yaw), action=16), [0]))
    return lines


def deep_gat():
    return b"GRAT\x01\x02" + struct.pack("<ii", 4, 4) + struct.pack("<4fi", DEEP, DEEP, DEEP, DEEP, 0) * 16


def main():
    oracle = path_oracle.Oracle()
    maps = [("open", dict(path_vectors.MAPS)["open"], False), ("walls", dict(path_vectors.MAPS)["walls"], False),
            ("hills", walk_vectors.HILLS, True)]
    lines = ["// Generated by tools/item_vectors.py from the original in the emulation oracle."]
    lines.append("// The maps are tools/walk_vectors.py's (walk_vectors.inc's kWalkMaps). Each map's items and")
    lines.append("// corpses in order (tools/walk_oracle.py's script and output), then chosen ones; map 3 is 4 by 4")
    lines.append("// cells flat at kDeepGround.")
    lines.append(f"static constexpr float kDeepGround = {DEEP:.1f}f;")
    lines.append("static const StateQueryVector kItemQueries[] = {")
    for m, (name, rows, hilly) in enumerate(maps):
        width, height = len(rows[0]), len(rows)
        attrs = [path_vectors.ATTRIBUTES[c] for row in rows for c in row]
        script = walk_oracle.make_items(name, width, height, attrs, 40) + walk_oracle.make_corpses(name, width,
                                                                                                   height, attrs, 20)
        if name != "walls":
            script += deciding(hilly)
        for query, result in zip(script, oracle.run(walk_vectors.gat(rows, hilly), script)):
            lines.append(f"    {{{m}, \"{query}\", \"{result}\"}},")
    script = [item(b"501", dict(dropped=1, cell_x=1, cell_y=1), [420, 421, 422]),
              item(b"501", dict(dropped=1, cell_x=1, cell_y=1), [421]),
              item(b"501", dict(dropped=1, cell_x=1, cell_y=1), [420, 422])]
    for query, result in zip(script, oracle.run(deep_gat(), script)):
        lines.append(f"    {{3, \"{query}\", \"{result}\"}},")
    lines.append("};")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
