# ClientState's tables

`ClientState::method_00588a00` loads the client's text tables from `data\`:
- the item and card tables ([item_tables.md](item_tables.md));
- `ClientState`'s own, with three kinds of loader.

This page covers the ones ported so far.

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

## Number records

Two tables are read with the same code, each into a vector of {string, vector of ints}
records on `ClientState`:

| Table | Loader | Into |
|-------|--------|------|
| `LevelUseskillSpAmount.txt` | `0059ca60` | +0x430: a skill id and its SP by level |
| `skillAfterDelay.txt` | `0059cdd0` | +0x440: a skill id and its delay by level |

**The loader** keeps a name, a list of numbers, and where the current field starts:
1. **Empty the vector.** If the file does not open, return 0.
2. **Walk the bytes:**
   - **`\r` or `\n`:** the next field starts after it.
   - **`#`:** the bytes from the field start up to it make the field, and the next field
     starts after it.
     - **No name yet:** the field becomes the name, as a byte range.
     - **Otherwise:** it is passed to `atoi`, and the result appended to the numbers.
   - **`@`:** the record (name, numbers) is added, and both are emptied. The field start
     does not move, so with no line break after `@`, the next field begins with it.
3. **Sort the records** by name, with the same sort and comparison as the name pairs
   (`005a0d40`, `005a0fc0`).

`LevelUseskillSpAmount`'s loader then indexes the records by skill id (`0059d1f0`). The
index is a vector of 158 pointers at +0x460, built from the skill id names
(`GetSkillIdName`) and a lookup in the sorted records (`0059d0d0`). The port leaves it to
the skills.

## Line records

`skillDescTable.txt` (`005a54b0`) goes into a vector of {string, vector of strings}
records at +0x420: a skill id and its description lines.

1. **Read the file** into a copy with a NUL added. If it does not open, return 0.
2. **Walk the bytes,** with a field starting at 0:
   - **`\r` or `\n`:** becomes NUL. Once there is a name, the field is added as a line.
     Either way, the next field starts after it.

     So a CRLF ends a line and then adds an empty one, and the line after a name's `#` is
     empty too.
   - **`#`:** becomes NUL.
     - **No name yet:** the field becomes the name.
     - **Otherwise:** the field is added as a last line, the record is added, and the name
       and lines are emptied.

     The next field starts after it.
   - **Every field** is read as a C string, so a NUL in the file ends it.
3. **Sort the records** by name, as above.

### In data.grf

| Table | Records |
|-------|--------:|
| `LevelUseskillSpAmount.txt` | 49 |
| `skillDescTable.txt` | 162 |

- **`LevelUseskillSpAmount.txt`** ends each record with `@` on a line of its own. One
  patch carries a longer version.
- **`skillAfterDelay.txt`** is in neither `data.grf` nor the patches.

## Fields

Three loaders keep the `#`-ended fields themselves. Each walks the bytes with a field
start:
- **`\r` or `\n`** moves the start after it.
- **`#`** takes the bytes from the start up to it, NULs and all, as the field, and moves
  the start after it.

What follows the last `#` is dropped.

| Table | Loader | Into | Kept as |
|-------|--------|------|---------|
| `msgStringTable.txt` | `00598c20` | +0x16c0, a vector of strings | each field, in order |
| `indoorRswTable.txt` | `005988c0` | +0x16a0, a vector of strings | each field, then sorted |
| `fogParameterTable.txt` | `00598e10` | +0x1690, {string, vector of strings} records | a name and four values a record, then sorted by name |

**The details:**
- **Each loader** empties its vector first. If the file does not open, it returns 0.
- **The sorts** are the library's, as above. The indoor maps are compared by an inline
  `strcmp` (`005a1ef0`); the fog records by name (`005a06d0`).
- **Fog records:**
  - while there is no name, a field becomes the name;
  - the next three are values;
  - the fourth value closes the record, and the name and values are emptied.

  A record cut short at the end is dropped.
- **Messages:** `InitMsgStringTable` (`004d8e40`, from `WinMain`) calls this loader
  unless the service is Korea, which uses 0x2cb built-in strings instead.

### In data.grf

| Table | Fields | Kept |
|-------|-------:|------|
| `msgStringTable.txt` | 703 | 703 messages, one a line, two with bytes above 0x7f |
| `indoorRswTable.txt` | 23 | 23 map files |
| `fogParameterTable.txt` | 690 | 138 maps with four values each: two decimals, a hexadecimal colour such as `0x2275A9FF`, and another decimal |

The last lines of the indoor and fog tables have no line break, which changes nothing.
The patches carry longer versions of both.

## An unused loader

`LoadSkillTablesByName` (`004d95b0`) is not called from anywhere, and its address
appears nowhere in the image. It would read the lowercase `skillnametable.txt`,
`skilldesctable.txt` and `leveluseskillspamount.txt` through `LoadHashSeparatedStrings`,
all in `#` mode, into a map keyed by the skill id (`0067ccb8`).

In that mode the description text, on lines with no `#`, is dropped: each `\n` restarts
the field. The client reads its skill names, descriptions and SP amounts with
`ClientState`'s own loaders instead.

## In the port

`src/res/client_tables.{h,cpp}`:
- `ParseNamePairs`, `ParseNumberRecords`, `ParseLineRecords`, `ParseFields` and
  `ParseFogParameters`;
- their `Load…` counterparts.

The sort is `Vc6Sort` (`src/res/vc6_sort.h`), which the model loader shares.

**The oracle:** `tools/client_tables_oracle.py` runs each of the ten loaders in the
emulation oracle on its table from `data.grf` and from the `.gpf` patches that carry it.
- **Synthetic tables:** it also runs one loader of each kind on synthetic tables: 8 name
  pair tables, 8 number tables, 7 line tables, 6 field tables (through both the message
  and the indoor loaders) and 6 fog tables. They cover:
  - line breaks inside a record, empty names and NULs;
  - `atoi` on signs and junk, and `@` with no line break after it;
  - bytes above 0x7f, which sort after ASCII;
  - hundreds of records with many equal names;
  - random bytes.
- **The SP index:** `0059d1f0` is skipped, since its skill id names come from start-up
  code the emulator does not run.
- **What it compares:** every record, in order, against `ragres decode-name-pairs`,
  `decode-number-records` and `decode-line-records`.
