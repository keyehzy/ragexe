# Item and card tables

`LoadItemTables` (`004d48e0`) reads 14 text tables from `data\` into three maps: items,
cards, and metal-processing lists. `ClientState::method_00588a00` calls it (`00588a9f`).

## Fields

`LoadHashSeparatedStrings` (`004d3850`) takes (list, name, line mode) and turns a table
into a list of strings.

1. **Empty the list.**
2. **Open the file** with `DataFile`. If it does not open, return false, leaving the list
   empty.
3. **Read it whole** and add a NUL.
4. **Walk the bytes,** with a field starting at 0:
   - **`#`:** becomes NUL and ends a field, which is added; the next starts after it.
   - **`\r`:** becomes NUL. In line mode it ends a field too. In either mode the next
     field starts after it.
   - **`\n`:** the next field starts after it, so what came since the last field's end is
     dropped.
5. **Each field** is a C string: it runs to the first NUL, the byte that ended it or one
   the file holds.

What follows the last `#` (or `\r`) is dropped. So is a line that ends with a bare `\n`,
in line mode.

## Tables

The id fields go through `atoi`: leading spaces, a sign, digits, wrapping in 32 bits.
Each id is found in its map or inserted with an empty value. In the order they are read:

| Table | Mode | Record | Into the item or card |
|-------|------|--------|-----------------------|
| `Num2ItemDisplayNameTable.txt` | `#` | id, name | name (+0x30), underscores to spaces |
| `Num2ItemResNameTable.txt` | `#` | id, name | resource (+0x40) |
| `Num2itemDescTable.txt` | line | see below | description lines (+0x50) |
| `IdNum2ItemDisplayNameTable.txt` | `#` | id, name | identified name (+0x00), underscores to spaces |
| `IdNum2ItemResNameTable.txt` | `#` | id, name | identified resource (+0x10) |
| `IdNum2itemDescTable.txt` | line | see below | identified description lines (+0x20) |
| `itemSlotTable.txt` | `#` | id, n | +0x60 |
| `itemParamTable.txt` | `#` | id, a, b | a and b appended to +0x68 |
| `itemSlotCountTable.txt` | `#` | id, n | +0x64 |
| `CardItemNameTable.txt` | `#` | id | the card, inserted |
| `Num2CardIllustNameTable.txt` | `#` | id, name | illustration (+0x00) |
| `CardPrefixNameTable.txt` | `#` | id, name | prefix (+0x10) |
| `CardPostfixNameTable.txt` | `#` | id | the prefix moves to the postfix (+0x20) |
| `MetalProcessItemList.txt` | line | see below | the id's lines |

**A record runs past the last field** when the count is not a multiple of the record's
size. Its next field is then the list's head node, which holds no string, and the walk
goes round the list again without end.

**Display names** are copied into a 256-byte stack buffer (`local_2c4`), and
`ClientState::UnderscoresToSpaces` (`00599560`) turns each `_` into a space.
- It steps with `CharNextExA` on the code page (`0067c998`), so the trail byte of a
  double-byte character is never taken for an underscore.
- A name of 256 bytes or more overruns the buffer.

**Description tables,** line by line from id 0 (a line before any id belongs to item 0):
1. **An empty line** is skipped.
2. **Otherwise the line is copied** into a 1024-byte stack buffer (`local_6c4`); a line
   of 1024 bytes or more overruns it.
3. **`ReverseAlphabetInPlace`** (`004d93a0`) runs on it, and the result is stored back.
   It acts only when the service type (`0067c9d4`) is 0, Korea:
   - `a`..`z` become `z`..`a`;
   - `A`..`Z` become `Z`..`A`;
   - bytes `0xb0`..`0xbf` are reversed too.
4. **`IsDigitString`** (`004d3af0`) asks whether every byte is `0`..`9`:
   - **yes:** the line's `atoi` becomes the current id;
   - **no:** the line is appended to the current item's lines.

**`MetalProcessItemList.txt`** works the same way, without the alphabet. Its lines
collect under each id in their own map.

## Maps

Each map is a VC6 `std::map<int, …>`.

| Map | Node | Value |
|-----|-----:|-------|
| `g_itemInfoById` (`0067cc50`) | 0x8c | `ItemInfo`, 0x78 bytes: three strings or string vectors for the identified item, three for the unidentified one, then +0x60, +0x64 and a vector of ints |
| `g_cardInfoById` (`0067cc38`) | 0x44 | `CardInfo`: three strings |
| `g_metalProcessItems` (`0067cc20`) | 0x24 | a vector of strings |

**After the tables:**
- **Item 0, or the fallback item** (`00689140`, used for any id not in the map) when id 0
  is not in the map:
  - both names: "Unknown Item" (`0060c36c`);
  - both resources: `\xbb\xe7\xb0\xfa` (`0060c364`), "사과" in code page 949;
  - both descriptions: "Unknown Item" appended.
- **Card 0, or the fallback card** (`006891c0`): illustration "sorry" (`0060c35c`), with
  the prefix and postfix emptied.

The maps are never cleared, so a second load would add to the first.

### In data.grf

All 14 tables end their lines with CRLF and hold no NUL.

| Table | Records |
|-------|--------:|
| `Num2ItemDisplayNameTable.txt` | 1,187 |
| `Num2ItemResNameTable.txt` | 1,098 |
| `IdNum2ItemDisplayNameTable.txt` | 1,194 |
| `IdNum2ItemResNameTable.txt` | 1,098 |
| `itemSlotTable.txt` | 900 |
| `itemParamTable.txt` | 900 |
| `itemSlotCountTable.txt` | 472 |
| `Num2CardIllustNameTable.txt` and `CardPrefixNameTable.txt` | 148 each |
| `CardPostfixNameTable.txt` | 35 |

**What the maps end with:** 1,196 items, 148 cards (no card 0), and 58 metal-processing
ids.

**Item 0** is not in the tables, so the defaults go to the fallback.

**The patch archives** carry newer `IdNum2ItemResNameTable.txt` files, among others.

## In the port

`src/res/item_tables.{h,cpp}`:
- `SplitHashSeparated` (`LoadHashSeparatedStrings`);
- `ParseLeadingInt`, `IsDigitString`, `ReverseAlphabet` and `UnderscoresToSpaces`;
- `LoadItemTables` into an `ItemTables`: the three maps and the two fallbacks.

**The settings** are the service and the code page. The client takes both from
`clientinfo.xml` (`ReadClientInfoSettings`, `SelectLanguage`).
- **iRO's file** says `america`: descriptions are not reversed.
- **The code page** is 0, the system's. The port takes 0 as single-byte.

**Where the port differs:**
- **It fails** where the client overruns its buffers (a name of 256 bytes, a description
  line of 1024) or goes round a table without end (a record cut short).
- **Double-byte code pages:** `CharNextExA` is followed from the lead-byte ranges of code
  pages 932, 936, 949 and 950. The oracle cannot check this, since the emulator's
  `CharNextExA` is single-byte.

**The oracle:** `tools/item_tables_oracle.py` runs the original `LoadItemTables` in the
emulation oracle. It constructs the maps with their static initializers
(`static_init_122_body` to `124`) and reads them back.
- **What it compares,** with `ragres decode-item-tables`: every item, card and
  metal-processing entry, and both fallbacks, field by field.
- **Real tables:** those of `data.grf`, for the American and the Korean service, and
  with each of the three `.gpf` patches' tables over them.
- **16 synthetic sets** cover:
  - ids with spaces, signs, junk and 32-bit wrapping, and duplicates;
  - description lines before the first id, empty and digit lines;
  - LF-only and CR-only endings, fields left open, and NULs;
  - cards, postfixes and card 0; item 0;
  - the metal list, and the largest name and line that fit.
- **For the 5 sets the port must refuse,** it checks that the port fails.
