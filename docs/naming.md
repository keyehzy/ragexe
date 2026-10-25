# Naming functions

How a function of `Ragexe.exe` gets its name, for the tools and for naming by reading.
It applies PLAN rule 4 (`docs/PLAN.md` §2): names come from the binary or describe what
the code does, never from memory of other Ragnarok Online projects.

## Where names may come from

| Source | Examples |
|--------|----------|
| RTTI | `CGameMode`, `UIWindow`, `std::strstreambuf` |
| Strings in the binary | `CTokenizer::operator[]` ("Error in CTokenizer :: operator []"), `CConnection::OnRecv` ("Socket OnRecv Error"), `InitDefaultClientInfo` |
| Imports | `DirectDrawCreateEx`, `WSAAsyncSelect`, Granny and Miles entry points |
| The public interface of what the code calls or implements | Win32, DirectX 7, Winsock and the C runtime by their documented names; the C++ standard library's public members (`insert`, `erase`, `find`, `clear`, `operator[]`) |
| Standard algorithms recognised from their constants in the binary | `Md5Context` (`0x67452301` at `0x41f840`) |
| A descriptive name made up from what the function does | `CutPacket`, `SetHoldSends`, `construct_g_UIWindowMgr` |

Not allowed:
- **Names from other Ragnarok Online projects:** packet names, function or field names,
  file formats. A packet is named only where the binary names it (`PACKET_ZC_*`
  strings); otherwise by its id.
- **A library's internal names when the binary doesn't contain them.** For example,
  MSVC 6's `_Tree`, `_Lockit`, `_Buynode`, `_Tidy`. They are recalled from library
  sources, not read from the binary. Those internals get descriptive names; see below.

## Game code

- **Classes.**
  - A class with RTTI keeps its RTTI name.
  - A class without RTTI is curated in `analysis/labels/inferred.classes.tsv`. It is
    named from a string when the binary has one (`C3dDevice`, `CTokenizer`), and
    otherwise descriptively, with no `C` prefix (`PacketQueue`, `SceneRenderer`).
- **Functions:** CamelCase, verb first: `GetPacketLength`, `LoadClientInfo`,
  `DrawCursor`.
  - A constructor or destructor is named after its class.
  - A virtual function nobody has named is `vfNN`, its vtable slot.
- **Generated placeholders:** `method_<address>` (the class is known, the purpose is
  not), `HandlePacket_<id>`, `static_init_NNN`.
  - Initializer helpers are lower case: `construct_<global>`,
    `register_destroy_<global>`, `destroy_<global>`, `static_init_NNN_body`.
  - Globals are `g_<Name>`, pointers to them `g_p<Name>`.
- **Prefer an accurate plain name to a guessed specific one.** `ApplyStageMode` is
  better than a game term the code doesn't show.

## Library code (CRT and the MSVC 6 STL)

Library rows only take the code out of the port queue, so their names have one job:
to say what the code is.

- **Namespaces:**
  - RTTI names where the classes have RTTI: the stream, locale and exception classes.
  - Otherwise the standard type the code implements: `std::list`, `std::vector`,
    `std::string`, `std::pair`.
  - The red-black tree behind `std::map` and `std::set` is `std::Tree`; the code can't
    tell a map from a set.
- **Lower-case names are the C++ standard's public names.** Use one only when the
  function does what that member is documented to do, and cite the evidence: `insert`
  for "insert n copies of a value at a position", `pair` for a pair constructor.
- **CamelCase names are our descriptions of internals,** from the table below. Reuse
  them, so that separate naming runs agree.

| Namespace | Name | What it does |
|-----------|------|--------------|
| `std::Tree` | `Tree`, `~Tree` | constructor (calls `InitHead` on `this`), destructor (erases all nodes, frees the head, releases the nil node) |
| | `InitHead` | under `std::StlLock`, creates the shared nil node on first use, allocates the head node into `+4`, zeroes the size at `+0xc` |
| | `AllocNode`, `FreeNode` | `operator new` of one node and its link set-up, `operator delete` of one node |
| | `RotateLeft`, `RotateRight` | rebalancing rotations around a node |
| | `LeftmostNode`, `RightmostNode` | follow the left or right links down to the nil node |
| | `IncrementIterator`, `DecrementIterator` | step an iterator (a node pointer) to the next or previous node |
| | `InsertAt` | link a new node under a given parent and rebalance (the insertion step `insert` uses) |
| | `EraseSubtree`, `CopySubtree` | recursive free or copy of a subtree |
| | `insert`, `erase`, `find`, `lower_bound`, `upper_bound`, `clear`, `operator[]`, `count` | public members, when the behaviour matches |
| `std::list` | `list`, `~list` | constructor, destructor |
| | `AllocNode`, `EraseNode`, `EraseRange` | allocate a node; unlink and free one node or a range, returning the next position |
| | `IncrementIterator` | step an iterator to the next node |
| | `push_back`, `push_front`, `insert`, `erase`, `clear`, `remove`, `sort`, `splice` | public members |
| `std::vector` | `vector`, `~vector`, `operator=` | constructor, destructor, assignment |
| | `insert`, `push_back`, `erase`, `resize`, `reserve`, `clear`, `size`, `capacity` | public members |
| | `FillUninitialized`, `CopyUninitialized`, `DestroyRange` | construct n copies or a copy of a range in raw memory, destroy a range |
| `std::allocator` | `allocate`, `deallocate`, `construct`, `destroy`, `max_size` | public members: called with ECX = the allocator, the container's first byte |
| `std::string` | `string`, `~string` | constructor, destructor |
| | `FreeBuffer`, `GrowBuffer`, `ReallocateBuffer` | release (or drop a reference to) the copy-on-write buffer; make room for n characters; allocate a larger buffer and copy into it |
| | `ReallocateBufferTail`, `GetRefCountByte` | the part of `ReallocateBuffer` reached only by its jump; the address of the reference count, the byte before the characters |
| | `assign`, `append`, `insert`, `erase`, `compare`, `find`, `substr`, `resize`, `max_size`, `operator+=` | public members |
| `std::pair` | `pair` | the pair constructor |
| `std` | `ConstructAt`, `DestroyAt` | copy-construct a value at a pointer when it is not null; destroy the value at a pointer. `__cdecl`; the same operation called with ECX = an allocator is `std::allocator::construct` / `destroy` |
| | `AllocateElements` | `operator new` of n elements, as the containers' allocation helper |
| | `QuickSortRange`, `InsertionSort`, `ShiftInsertSorted` | the parts of a sort: partition and recurse; sort a short range; move one element back to its place |
| | `copy`, `operator+` | public algorithms and operators, when the behaviour matches |
| | `ThrowLengthError`, `ThrowOutOfRange` | throw `length_error` or `out_of_range` with the string the binary holds |
| `std::StlLock` | `StlLock`, `~StlLock` | enter and leave the global critical section that guards the STL (`0x688a88`) |
| `std::LocaleInfo` | `LocaleInfo`, `~LocaleInfo` | the locale information object built on the stack before facets |
| `std::locale` | `GetFacet`, `IsClassic`, `InitGlobal` | index the facet table; compare with the classic locale; create the global locale on first use |
| `std::ios_base`, `std::strstreambuf` | `InitDefaults`, `InitBuffer` | a new stream's flags, precision and width; a new strstreambuf's buffer pointers |

Struct fields follow the same rule. For example, `std::string` is `allocator`, `buffer`,
`length`, `capacity`.

Template code is instantiated once per element type, so the same name appears many
times in one namespace; the address and the evidence tell the copies apart.

## Naming by reading

For the functions no rule names, one row per function goes into
`analysis/labels/inferred.functions.tsv` with these columns:
- address;
- namespace;
- name;
- calling convention;
- evidence: one or two sentences, every claim tied to an address (callers, callees,
  fields as `+0x..`, strings, imports, constants).

The procedure:
1. **Context.** `.venv/bin/python tools/fn_context.py ADDRESS ...` prints:
   - neighbours, convention evidence, callers and callees;
   - unwind funclets with the function that owns them;
   - strings and data.

   The named neighbours are usually the best clue, since a source file's functions sit
   together.
2. **Code.** `tools/ghidra/decompile.sh OUT ADDRESS ...` decompiles a batch, read-only.
   Batch many addresses per run: start-up is most of its time. The disassembly is the
   final word on calling conventions and constants.
3. **Calling convention.** `__thiscall` when the body reads ECX before writing it.
   - If the body never reads ECX, still `__thiscall` when the callers set ECX just
     before the call (`fn_context.py` counts this) and the function pops its own
     arguments.
   - A function with no arguments whose callers all set ECX is `__thiscall`.
   - Otherwise `ret N` with N > 0 means `__stdcall`, and a plain `ret` means `__cdecl`.
4. **Confidence.** High when the evidence pins the purpose. Medium for a descriptive
   name inferred from use. Low, or no row at all, when the purpose is unclear.
5. **Review.** Every row is checked against the code before it goes in:
   - the cited addresses exist and do what the evidence says;
   - the convention matches `ret` and the callers;
   - library code uses the table above.

Batches follow the source files: runs of functions between dynamic initializers
(`docs/architecture.md` §1). Subsystem tags are not used, because they cut across
files.

Functions with a `reach` value in the ledger (`unreferenced` or `dead`, from
`tools/find_unreferenced.py`) are never reached, so they are left out of the batches.

Classes without RTTI are not invented while naming. Functions that look like methods of
such a class are named in `Global` (as `__thiscall` where the convention says so), and
the class is proposed separately, with evidence, for `inferred.classes.tsv`.

Terms for a file format's parts need the same evidence as names. When only the shape is
visible (a vector of vectors of records), name the shape (`GetEntryCount`,
`GetRecordLength`), not what such a format is known to hold elsewhere.
