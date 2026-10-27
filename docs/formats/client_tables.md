# ClientState's tables

`ClientState::method_00588a00` loads the client's text tables from `data\`: the item and
card tables ([item_tables.md](item_tables.md)) and `ClientState`'s own, each with its own
loader. This page covers the ones ported so far.

## Name pairs

Four tables are read with the same code, each into a vector of string pairs on
`ClientState`:

| Table | Loader | Into | Pairs |
|-------|--------|------|-------|
| `mapNameTable.txt` | `005979d0` | +0x1670 | a map file and its display name |
| `mapObjLightTable.txt` | `00597e10` | +0x1718 | a map file and `on`/`off` |
| `mp3NameTable.txt` | `00598290` | +0x1680 | a map file and its music file |
| `skillNameTable.txt` | `00593c10` | +0x400 | a skill id and its name |

`method_00588a00` calls them in that order (`00588a31`–`00588a4d`).

**The loader:**
1. **Empty the vector.**
2. **Open the table** with `DataFile`. If it does not open, return 0.
3. **Read the bytes one by one** into one of two character buffers, the first name's or
   the second's. Reading starts with the first.
   - **`\r` or `\n`** empties the buffer being filled. The other keeps what it has.
   - **`#` while the second buffer is empty:**
     1. a NUL is appended to the buffer being filled;
     2. filling moves to the second buffer.

     If the second buffer was already the one being filled, this NUL is its first byte,
     and it ends the second name.
   - **`#` otherwise:**
     1. a NUL is appended;
     2. the pair (the first buffer up to its first NUL, the second likewise) is added;
     3. both buffers are emptied, and filling goes back to the first.
   - **Any other byte** is appended.

   A record left open at the end is dropped.
4. **Sort the pairs** with `std::sort` (`0059fc20`), by the first name.
   - **The comparison** is `StringLess` (`00599400`), an inline `strcmp`: bytes compared
     unsigned.
   - **The sort** is the library's introsort, which is not stable:
     - for more than 16 elements, quicksort around the median of the first, middle and
       last elements, recursing into the smaller part, then insertion sorts;
     - for 16 or fewer, an insertion sort.

     So equal first names end in the order that algorithm leaves them.
5. **Return 1.**

### In data.grf

All four end their lines with CRLF and hold only ASCII. Each line is `first#second#`.

| Table | Pairs |
|-------|------:|
| `mapNameTable.txt` | 370 |
| `mapObjLightTable.txt` | 309 |
| `mp3NameTable.txt` | 334 |
| `skillNameTable.txt` | 162 |

The music files are written `bgm\\02.mp3`, with the backslash doubled.

## An unused loader

`LoadSkillTablesByName` (`004d95b0`) is not called from anywhere, and its address
appears nowhere in the image. It would read the lowercase `skillnametable.txt`,
`skilldesctable.txt` and `leveluseskillspamount.txt` through `LoadHashSeparatedStrings`,
all in `#` mode, into a map keyed by the skill id (`0067ccb8`).

In that mode the description text, on lines with no `#`, is dropped: each `\n` restarts
the field. The client reads its skill names, descriptions and SP amounts with
`ClientState`'s own loaders instead.

## In the port

`src/res/client_tables.{h,cpp}`: `ParseNamePairs` and `LoadNamePairs`. The sort is
`Vc6Sort` (`src/res/vc6_sort.h`), which the model loader shares.

**The oracle:** `tools/name_pairs_oracle.py` runs each of the four loaders in the
emulation oracle on its table from `data.grf` and from the `.gpf` patches that carry it.
It also runs the `mapNameTable` loader on 8 synthetic tables, which cover:
- line breaks inside a record, empty names and NULs;
- bytes above 0x7f, which sort after ASCII;
- 300 records with many equal first names;
- random bytes.

Every pair matches in order, against `ragres decode-name-pairs`.
