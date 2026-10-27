# manner.txt: MannerFilter

`MannerFilter` (`0067cbe8`) holds the words the chat filter bans. `WinMain` loads
`manner.txt` into it at start-up (`005b2879`).

## Load

`MannerFilter::Load` (`004d25e0`) takes the name, and reads `data\` and the name with
`DataFile`.

- **The file does not open:** show "Cannot find File : " (`00605c3c`) and the name, and
  return 0.
- **The file is empty:** return 0.
- **Otherwise:**
  1. Read it with a NUL added, and empty the word deque (+4 to +0x2c).
  2. Walk the bytes:
     - **`\r`:** becomes NUL.
     - **`\n`:** becomes NUL, and `AddWord` (`004d2450`) takes the line from its start,
       as a C string. So a `\r` inside a line cuts it there.

     A last line with no `\n` is dropped.
  3. Return 1.

**`AddWord`:**
1. **A blank line is skipped:** it skips spaces and tabs only to see whether anything is
   left.
2. **Otherwise the whole line is stored,** leading blanks included, lower-cased.
   - **Trailing spaces stay:** the trim of trailing spaces starts at the terminating NUL,
     so it never moves.
   - **The lower-casing** (`AssignLowercase`, `004d2070`) adds 0x20 to A–Z. A byte of
     0x80 or more is taken as the first of two, and the byte after it is skipped.

## IsClean

`IsClean` (`004d2220`) takes a text; `ContainsBannedWord` (`004d2200`) is its negation.

1. **Lower-case a copy** of the text, as above.
2. **At each character of the copy,** compare each word with `strncmp` over the word's
   length. A match makes the text unclean.
3. **The next character** is two bytes on for a byte from 0x80 to 0xfe, one otherwise.
   So a word never matches from the second byte of a double-byte character.

A text that ends with a lone lead byte steps past its NUL, and the client reads on.

## In data.grf

`manner.txt` holds 112 words, one per CRLF line: spelled-out, spaced and dotted
variants, in ASCII.

## In the port

`src/res/manner.{h,cpp}`: `MannerFilter` with `Load`, `Parse`, `IsClean` and
`ContainsBannedWord`.

**Where the port differs:** `IsClean` stops at a lone lead byte at the end.

**The oracle:** `tools/tips_manner_oracle.py` runs the original `Load` in the emulation
oracle, and reads the words back from the client's deque. It then runs `IsClean` on 20
probe texts: cases, spaces and tabs, double-byte characters, `0xff`, an empty text and an
embedded NUL.
- **The word lists:** `data.grf`'s `manner.txt`, and 6 synthetic ones with:
  - blank and indented lines, trailing spaces;
  - a CR inside a line, no final line break;
  - double-byte words, NULs;
  - an empty file;
  - random bytes.
- **What it compares:** every word and every answer, against `ragres decode-manner`.
