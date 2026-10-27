# The tip of the day

`TipOfTheDayList` (`0067ccd0`) holds the tips the login screen shows. `WinMain` loads it at
start-up, right after the manner filter (`005b2888`).

## Load

`TipOfTheDayList::Load` (`004db290`).

1. **Pick the file by the service type** (`0067c9d4`, [clientinfo.md](clientinfo.md)):

   | Service type | File |
   |-------------:|------|
   | 1 | `tipOfTheday_e.txt` |
   | 2 | `tipOfTheday_j.txt` |
   | any other | `tipOfTheday.txt` |

2. **Open it with `fopen`**, mode `"r"`, beside the client rather than in `data`. If it
   does not open, return 0.
3. **Measure and read it:**
   1. `fseek` to the end, and `ftell` gives the size.
   2. Allocate the size plus one, with a NUL at the end.
   3. `fread` it whole.

   Text mode turns CR LF into LF and stops at the first Ctrl-Z. So `fread` can return
   less than the size, and the rest of the buffer is uninitialized memory, which the walk
   below still covers.
4. **Walk the bytes** up to the size, with a line starting at 0:
   - **`\n`:** becomes NUL; the line is added to the tip being built.
   - **`#`:** becomes NUL; the line is added, then the tip is appended to the list
     (`InsertTips`, `004db520`) and emptied.

   Each line is read as a C string, so a NUL in the file ends it. Lines after the last
   `#` are dropped. A `\r` that text mode leaves (one not before `\n`) stays in its line.
5. **Return 1.** The list is not emptied first.

The line break after each `#` starts the next tip with an empty line.

`GetTip` (`004db4f0`) returns the tip at an index, or the first when the index is out
of range.

## In the install

`tipOfTheDay_e.txt` (10,186 bytes) holds 63 tips:
- **Each tip** starts with `\xa1\xe6`, a bullet in code page 949, and ends with a `#`
  line.
- **Continuation lines** are indented with spaces.
- **Line ends:** all CRLF.

The other two files are not in the install.

## In the port

`src/res/tips.{h,cpp}`:
- `TipFileName`;
- `ReadAsText`, the C library's text mode;
- `ParseTips`;
- `LoadTips`, which resolves the name beside the client without regard to case;
- `GetTip`.

**Where the port differs:** it takes the uninitialized tail to hold neither `\n` nor `#`.

**The oracle:** `tools/tips_manner_oracle.py` runs the original `Load` in the emulation
oracle. Its `fopen` and `fread` are the client's own C library, over the emulator's file
calls.
- **The inputs:** the install's file, and 9 synthetic ones. They cover lone CRs and CR CR
  LF, Ctrl-Z, NULs, empty tips, a CR LF split across the library's 4 KB reads, and random
  bytes.
- **What it compares:** every tip and line, against `ragres decode-tips`.
