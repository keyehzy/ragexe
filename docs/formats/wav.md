# Waves: CWave

`CWave` (RTTI, 0x118 bytes) is a sound sample. It is registered for `wav` under `wav\`
([resources.md](resources.md#types)).
- **Playing one:** `PlayWave` (`004352a0`) fetches it by name when the sound is on, and
  `CWave::Play` (`00435740`) hands its sample to `PlayDigital` or `PlayDigital3D`,
  depending on the sound mode (`00605b10`).
- **The work:** Miles Sound System, `Mss32.dll` 6.1a by its strings, decides what a file
  is and decodes it.

## Members

| Offset | Field |
|-------:|-------|
| +0x110 | the sample: what `Play` passes on |
| +0x114 | 1 when Miles allocated the sample |

| Slot | Method | Address |
|-----:|--------|---------|
| 2 | `Clone`: `operator new(0x118)` and the constructor `00435440` | `00435380` |
| 3 | `Load` | `004354a0` |
| 4 | `Reset` | `00435460` |

**`Reset`** frees the sample:
- with `AIL_mem_free_lock` when +0x114 is set, otherwise with `operator delete`;
- then it clears +0x114. +0x110 keeps the freed pointer.

## Load

`CWave::Load` (slot 3, `004354a0`) takes the path.

1. **Open it.** If it does not open, show "Wave :: Cannot find File : " (`00606524`)
   followed by the path, and fail.
2. **Read it:** `Reset`, then read the whole file into `operator new` memory.
3. **Ask `AIL_file_type`** what it is:
   - **2, IMA ADPCM:**
     1. Read its fields again with `AIL_WAV_info`.
     2. `AIL_decompress_ADPCM` makes a PCM wave into +0x110.
     3. Set +0x114 and free the file.
   - **13, MPEG Layer III:**
     1. `AIL_decompress_ASI` decodes it into +0x110, through the codec Miles finds for
        it (`Mp3dec.asi`, beside the client).
     2. Set +0x114 and free the file.
   - **Anything else:** the file itself goes to +0x110.
4. **Succeed,** whether or not a decoder made a sample.

### AIL_file_type

`AIL_file_type` (`Mss32.dll` `211277a0`, behind a logging wrapper) takes the data and its
size.
- **Under 8 bytes:** 0.
- **Otherwise it asks `AIL_WAV_info`,** below. If that finds a wave, the format tag
  decides:

  | Tag | Answer |
  |----:|--------|
  | 1 | 1, PCM |
  | 0x11 | 2 with 4 bits a sample (IMA ADPCM), otherwise 3 |
  | 0x55 | the checks below, on the data chunk |
  | any other | 14 if an ASI codec takes the tag, otherwise 3 |

  `Mp3dec.asi` takes 0x55.
- **Then, on the whole file** (or the data chunk, for tag 0x55), each check answers at
  once:
  1. `Creative` at the start: 4, VOC.
  2. `FORM` with `XDIR` at 8: XMIDI, 6 to 8.
  3. The bytes `RIFF` with `DLS ` or `MLS ` at 8: 9 or 10.
  4. **MPEG frame syncs** in the first 4 KB (`21127928`):
     - **A sync:** a 0xff byte, then a byte whose top 4 bits are set. The four bytes must
       not all be 0xff, and the third's top 6 bits must not all be set.
     - **The layer bits** (bits 1 and 2 of the second byte):
       - Layer III answers 13 at once;
       - Layer II and I are noted as 12 and 11, and the scan goes on.
     - **After each sync** the scan skips 3 more bytes, while its count still drops by
       one, so it reads past its window, and past a file of 4 KB or less.
     - A noted layer is the answer.
  5. `MThd` anywhere: 5, MIDI.
  6. Otherwise 0.
- **The comparisons** (`2111f180`) ignore A–Z case and stop at the first character that
  differs.

### AIL_WAV_info

`AIL_WAV_info` (`21114ad0`) reads a wave's fields. Nothing is checked against the data's
size.

1. **`WAVE` must be at 8.** `RIFF` itself is never checked.
2. **Find the chunks** `fmt ` and then `data`, each walk starting at 12:
   - each step adds the chunk's size, rounded up to even, plus 8;
   - the walk stops at the RIFF size, counted from the start of the file rather than
     from 8;
   - the first chunk is compared before any check.

   Either one missing means no wave: "Not a WAV file."

   The steps are 32-bit, so a chunk size can bring the walk back where it was:
   0xfffffff8 adds nothing. Then it never ends, and the client hangs.
3. **From `fmt `:** the tag, channels, rate, block size and bits.
4. **From `data`:** where the data starts, and its recorded size.
5. **The sample count:**
   - **IMA ADPCM, 4 bits:** the first value of a `fact` chunk. Without one, it comes from
     the blocks:

     ```
     per block = (block − h) · 8 / h + 1, with h = 4 << (channels / 2)
     samples   = per block · ⌈data size / block⌉
     ```

     All in unsigned 32 bits, and the shift count is taken modulo 32. A block size of 0
     divides by zero, and so do channel counts whose half, modulo 32, is 30 or 31 (60 to
     63 among them), which shift `h` to 0.
   - **Otherwise:** data size · 8 / bits, or 0 with 0 bits.

### AIL_decompress_ADPCM

`AIL_decompress_ADPCM` (`21129480`) does nothing for a sample count of 0. Otherwise:

1. **Allocate** `n = (channels · samples & 0x0fffffff) · 2` bytes plus 44, through
   `AIL_mem_alloc_lock`. That allocator is `GlobalAlloc` with `GMEM_ZEROINIT`
   (`21110720`), so samples the data does not reach stay silent.
2. **Write a PCM wave header:**
   - 16 bits a sample;
   - the channels and rate as read;
   - `(channels · rate & 0x0fffffff) · 2` bytes a second;
   - blocks of channels · 2 bytes;
   - `n` bytes of data.
3. **Decode:** two channels go to the stereo decoder (`2111e395`), any other count to the
   mono one (`2111e288`), which then fills all of `n`.

Both decoders are written in assembly, and both are the standard IMA ADPCM:

- **Each block** starts with a 4-byte header per channel:
  - a 16-bit first sample, written out at once without checking for room;
  - a step index, never range-checked.
- **Then come 4-byte words** of 8 nibbles, low first. In stereo, a word of the left
  channel and a word of the right alternate, and their samples interleave.
- **Each nibble** adds the step's difference.
  - **The tables:** the differences (`2114b840`) and the next indexes (`2114c360`) are
    tables by step index and nibble. Both are the standard step sizes and index
    changes, entry for entry.
  - **The sum** saturates to 16 bits, on the side the nibble's sign points to.
- **A block's bytes** are counted down by the words read and compared with zero. So a
  block whose data is not a whole number of words runs on into the next block without
  reading its header.
- **The checks:**
  - the input is checked before each word, against the end of the data chunk, as
    recorded;
  - the output is checked before each sample.
  - The decoders stop at whichever end comes first.

### In data.grf

All 988 `.wav` files are `RIFF`/`WAVE`, and each ends exactly where its RIFF size says.

| Kind | Files |
|------|------:|
| PCM: 16- and 8-bit, 8,000 to 44,100 Hz, one of them stereo | 982 |
| IMA ADPCM, 4 bits: four mono, one stereo, all with `fact` | 5 |
| Microsoft ADPCM (tag 2): `se_frog02.wav`, kept as it is | 1 |

None is MPEG.

## In the port

`src/res/wav.{h,cpp}`:
- `ClassifyWave` (`AIL_file_type`, as far as `CWave` tells its answers apart);
- `ReadWaveInfo` (`AIL_WAV_info`);
- `DecodeWave` (`Load`, with `AIL_decompress_ADPCM`);
- `WaveRes`, registered by `RegisterWaveTypes`.

**The sample** is the file, or the decoded PCM wave, or nothing when the ADPCM decoder
makes none.

**MPEG Layer III is classified but not decoded.** The port keeps the file and leaves it
to the sound layer.

**Where the port differs** (it fails where `Mss32.dll` reads or writes past its
buffers):
- **Reading past the file:** a chunk walk, a field or a comparison that reaches past
  the file; in particular, the MPEG scan of a file of 4 KB or less that has a sync that
  is not Layer III.
- **A chunk walk that comes back where it was.** Every step that goes on reads inside
  the file, so the port stops after as many steps as the file has bytes.
- **Tag 0x55 files with an XMIDI signature:** the port does not follow the reads that
  Miles' chunk search makes on them.
- **ADPCM decoding:**
  - a step index above 88, which reads past the tables;
  - a first sample written past an empty output (no channels);
  - data that runs past the file;
  - a division by zero.
- **A decoded sample over 64 MB fails.** The client asks `GlobalAlloc` for any size.

**The oracle:** `tools/wav_oracle.py` runs the original `Load` in the emulation oracle,
with the original `Mss32.dll` mapped in (`Emulator.load_mss`). It runs on all 988 files
in `data.grf` and 39 synthetic ones.
- **What it compares:**
  - Miles' answer, read where `AIL_file_type` returns (`004356a8`);
  - the sample, byte for byte. All 5 IMA ADPCM files decode identically.
- **For MPEG Layer III,** only the answer is compared: the emulator does not load
  `Mp3dec.asi`.
- **The synthetic files cover:**
  - mono, stereo and 3-channel IMA ADPCM, with and without `fact`;
  - a `fact` count larger or smaller than the data, or ending inside a block;
  - a block that is not whole words, and an index of 88;
  - tags matched without regard to case, and a file without `RIFF`;
  - each MPEG layer and sync rule, including a sync past 4 KB and an MPEG wave (tag
    0x55);
  - a PCM file cut off by its RIFF size, scanned whole;
  - VOC, XMIDI, DLS and MIDI signatures, short files, and a chunk walk that never
    ends.
- **For the 6 synthetic files** the port must refuse, it checks that the port fails.
  On the walk that never ends, the original does not return either.
