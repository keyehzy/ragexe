# Animated model scripts: CRmaRes

`CRmaRes` (RTTI, 0x158 bytes) reads a text file that ties numbered actions to animated
models ([rsx.md](rsx.md)), with a few interior and shadow settings. It is registered for
`rma` under `model\` ([resources.md](resources.md#types)).

**No file and no reader:**
- **Data:** no `.rma` ships in the game data.
- **The one name:** `캐릭터\칼질몬스터.rma` (`0061d8c8`), returned by a two-instruction
  virtual function at `0059b870`.
- **Readers:** none of `CRmaRes`'s getters has a static caller.

## Members

| Offset | Field |
|-------:|-------|
| +0x110 | `vector<record>`: one 0x1c-byte record per action number |
| +0x120 | `vector<string>`: the `.rsx` names |
| +0x130 | `vector<int>`: per action, the index of the last name before it |
| +0x140 | interior zoom (float), 1.0 by default |
| +0x144 | shadow zoom (float), 1.0 by default |
| +0x148 | interior x offset (int) |
| +0x14c | interior y offset (int) |
| +0x150, +0x154 | interior frames (two ints) |

The defaults come from the constructor `Clone` uses (`00421c90`), which zeroes the ints.

**An action record** is three ints and a `vector<int>`, `(−1, −1, 4, [])` when created:

| Offset | Field |
|-------:|-------|
| +0 | `#action`'s fourth token |
| +4 | its fifth |
| +8 | its third |
| +0xc | its sixth and later tokens |

**The getters**, none called:
- `HasAction` (`00422070`): +0 is not −1;
- `GetActionValue1` to `3`: +8, +0, +4;
- `GetActionFirstListValue`: the first of the list, or −1;
- `ActionListContains`;
- `GetActionRsxName` (`00421f50`): the name at the action's index.

## Text

The file is text. The loader reads it line by line.

### Lines

The text is read whole and NUL-terminated. A `strstreambuf` with a length of 0 then reads
it up to its **first NUL**.

`CTokenizer::ReadLine` (`004e4800`) reads each line with `getline(buffer, 0x400, '\n')`.
For each character it checks, in order:
1. **The end of the text.** If nothing was read, this fails the stream.
2. **A newline,** taken and not stored.
3. **A full buffer** of 1023 characters, which fails the stream.

`Load` processes a line before it checks the stream. So a line of 1023 characters plus
its newline is read whole. A longer one is cut to its first 1023 characters, and becomes
the last line read.

### Tokens

`CTokenizer::Tokenize` (`004e4190`) has three states, starting in "token".

**Token:**
- **`//`** ends the line, and the token it cuts short is dropped (`a b//c` gives `a`).
- **A space** (`isspace`, and never a byte of 0x80 or above) ends the token, then goes to
  "spaces". It ends the token **only when it is not the line's first character**.
- **`"`** drops the characters so far and goes to "quoted".
- **Anything else** is added to the token.

**Quoted:** `"` ends the token, which can be empty, and goes to "spaces". Anything else,
`//` included, is added.

**Spaces:** a non-space goes back to "token" and is read again.

**At the end of the line,** a token in progress is kept, unless the state is "spaces".

### Keywords

Each line with tokens is matched on its first token. An empty first token (`""`) skips
the line.

| Keyword | Tokens | Effect |
|---------|-------:|--------|
| `#rsxname` | 2 | The next name index; the name |
| `#action` | 5 or more | The record at `atoi(token 1)` |
| `#interiorzoom` | 2 | `atof` |
| `#interioroffsetx`, `#interioroffsety` | 2 | `atoi` |
| `#interiorframes` | 3 | Two `atoi`s |
| `#shadowzoom` | 2 | `atof` |
| anything else | | "Syntax Error in Rma" (`00605d30`) |

**`#action`:**
- **Growing the vectors:** the action number is taken as unsigned. The record and index
  vectors grow to hold it, with new records `(−1, −1, 4, [])` and new indices 0.
- **A duplicate:** when the record's first value is not −1, the CP949 message at
  `00605d98` ("there is a duplicate action") is shown, and the record is overwritten.
- **The values:** tokens 2, 3 and 4 go to `+8`, `+0` and `+4`.
- **The list:** six tokens or more replace it. Fewer leave the old one in place.
- **The name index:** the action stores the current name index, which is −1 before the
  first `#rsxname`.

**Too few tokens:** the client shows "Error in CTokenizer :: operator []" (`00605dc4`) and
the tokens, then reads the missing ones past the end of its token vector anyway.

**`atoi` and `atof`** are the C runtime's:
- **`atoi`** wraps at 32 bits.
- **`atof`** reads an optional sign, digits, a decimal point and an exponent after `e`,
  `E`, `d` or `D`. There is no hexadecimal, `inf` or `nan`. The result is stored as a
  float.

## Load

`CRmaRes::Load` (slot 3, `004220d0`) **always succeeds**.
- **It does not check `DataFile::Open`.** With no file, it reads the `DataFile`'s
  uninitialised size, and in practice reads nothing.
- **It does not reset first.**

`Reset` (slot 4, `00421e40`) empties the records, the names and the name indices. The
interior and shadow values stay.

## In the port

`src/res/rma.{h,cpp}`:
- **Parsing:** `TokenizeLine` is the tokenizer, and `DecodeRma` the reader.
- **The resource:** `RmaRes`, registered by `RegisterRmaTypes`. A missing file loads
  empty and succeeds, as in the original.
- **Messages:** the ones the original shows are kept in `Rma::messages`.

**Where the port differs:**
- **Lines with too few tokens fail,** where the original reads past its token vector.
- **So do action numbers below 0 or above 65535.** The original takes a negative one as
  unsigned, and would allocate a record for every number up to it.

**The oracle:** `tools/rma_oracle.py` runs the original `Load` on 14 synthetic files. It
compares the names, every action, the interior and shadow values, and the number of
messages.

What the files cover:
- comments, quotes, tabs and CRLF;
- CP949 bytes and a NUL;
- duplicate actions and an action before any name;
- unknown keywords;
- `atoi` wrapping, and `atof` with `d` exponents, bare exponents and words;
- lines of exactly and over 1023 characters.

**Locale warm-up:** the oracle loads one file before it marks the emulated heap. The
first read through an `istream` builds the C locale's `ctype` facet on the heap and keeps
it in a static, and releasing the heap after a file would free it.
