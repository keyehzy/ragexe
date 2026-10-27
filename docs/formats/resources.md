# Resource lookup and cache

How a resource name becomes bytes, and how loaded resources are kept. The archive itself
is in [grf.md](grf.md).

## From a name to bytes: DataFile

`DataFile` (constructed on the stack by every reader, `004dd2f0`) is 0x110 bytes:

| Offset | Field |
|-------:|-------|
| +0 | file handle, when writing |
| +4 | loaded buffer, when reading |
| +8 | read position |
| +0xc | size |
| +0x10 | the path, 256 bytes |

### Open

`DataFile::Open` (`004dd310`) takes (name, mode).

**Mode 0, reading:**
1. Build the path from the name with `MakeDataPath` into `+0x10`.
2. Set the position to 0.
3. Hand the path, as a hashed name, to the archive's `LoadFile` through `g_pCGPak`
   (`0067cd84`). That tries the archive, then the loose file of that name
   ([grf.md](grf.md#names-and-lookup)).
4. The returned buffer goes to `+4` and its size to `+0xc`. Open succeeds when the buffer
   is not null.

**Mode bit 0, writing:**
- The name is copied to `+0x10` as it is: no `data\`.
- `CreateFileA(name, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, CREATE_ALWAYS)`
  opens it, and its handle goes to `+0`. On failure the client shows an error with the
  name (`ShowErrorMessage` `004cc110`).

### Other methods

- **Read** (`004dd430`) of n bytes:
  - with a handle, `ReadFile`;
  - otherwise it copies n bytes from the buffer at the position and advances the
    position. It succeeds **only if all n bytes are there**: a read past the end copies
    nothing and fails.
- **Seek** (`004dd4e0`) to (offset, origin), where origin is 0 from the start, 1 from the
  position, 2 from the end:
  - with a handle, `SetFilePointer`, then success;
  - otherwise it moves the position. A position past the size is clamped to the size and
    the call fails; any other origin leaves the position alone.
- **Size** (`00523a60`): `+0xc`.
- **Close** (`004dd3f0`) closes the handle, or frees the buffer with `CGPak::FreeFile`.
- **Write** (`004dd4b0`): `WriteFile`.

`DataFileExists` (`004dd550`) builds the path the same way and asks the archive's `Exists`
(the archive, then `FindFirstFileA`).

### MakeDataPath

`004dd5a0`, from the name to the archive key (also in [grf.md](grf.md#names-and-lookup)):

1. `data\` (`0061ebc8`) followed by the name.
2. If the result contains `:` (`006125d8`), keep only the part from the character before
   the first `:`. So `data\C:\x` becomes `C:\x`: absolute paths escape the data folder.
3. Collapse every `//` (`006125d4`) to `/`, repeatedly.

Nothing else changes. Case is folded only later, by the hashed name: the archive key
and loose files are both looked up through it.

## Resources: CRes

`CRes` (RTTI) is the base class of the 15 resource classes:

| Offset | Field |
|-------:|-------|
| +0 | vtable |
| +4 | reference count (`AddRef` `004df950`, `Release` `004df960`) |
| +8 | last use, `timeGetTime` (`Touch` `004df9c0`) |
| +0xc | type index |
| +0x10 | its path as a hashed name (256-byte name) |

`Release` also touches the resource. `SetNameAndType` (`004df990`) sets the path and the
type index, and touches it.

**Virtual functions** (CRes vtable `005e27f4`):

| Slot | Function |
|-----:|----------|
| 0 | scalar deleting destructor |
| 1 | `LoadFromMemory(name, data, size)`; the base returns false (`00420880`). `CBitmapRes` decodes by the name's extension (`004278a0`) |
| 2 | `Clone`: a new, empty object of the same class (pure in `CRes`; `CBitmapRes::vf02` `00427670` is `operator new(0x120)` and the constructor) |
| 3 | `Load(path)` (pure in `CRes`), true on success |
| 4 | `Reset`: frees the loaded data; the base does nothing (`00420890`) |
| 5 | `OnLoadError(name)`: shows "ResourceError : Can't find file %s" (`00612814`, `004df9d0`) |

## The resource manager

`GetResourceManager` (`004dfa10`) returns the function-local static at `0067cd88`,
built on first use.

### Members

| Offset | Member |
|-------:|--------|
| +0 | `map<const char*, int>`: extension → type index, ordered by `strcmp` |
| +0x10 | `vector<const char*>`: the folder of each type |
| +0x20 | `vector<CRes*>`: a prototype object of each type, for `Clone` |
| +0x30 | `vector<map<HashedFileName*, CRes*>>`: the loaded resources of each type, keyed by the resource's own name (`+0x10`) |
| +0x40 | `CRITICAL_SECTION`, held by `GetResource` |
| +0x58 | `map<string, string>`: `ResNameTable.txt` |

### Types

`RegisterType(extension, folder, prototype)` (`004e06c0`) gives the next index, the size
of the extension map, to a type. Each resource class's constructor calls it with the
arguments of its static instance. The static initializers, in the order they run:

| Index | Extension | Folder | Class | Initializer |
|------:|-----------|--------|-------|-------------|
| 0 | act | `sprite\` | CActRes | `004207a0` |
| 1 | rma | `model\` | CRmaRes | `00421b10` |
| 2 | gat | (none) | C3dAttr | `00426a00` |
| 3 | bmp | `texture\` | CBitmapRes | `004275f0` |
| 4 | tga | `texture\` | CBitmapRes | `00427740` |
| 5 | jpg | `texture\` | CBitmapRes | `00427790` |
| 6 | ebm | `..\_tmpEmblem\` | CEmblemRes | `00428340` |
| 7 | str | `texture\effect\` | CEZeffectRes | `00428770` |
| 8 | gnd | (none) | CGndRes | `00428ea0` |
| 9 | imf | `imf\` | CImfRes | `004299a0` |
| 10 | rsm | `model\` | C3dModelRes | `0042b600` |
| 11 | rsx | `model\` | C3dAnimModelRes | `0042da00` |
| 12 | gr2 | `model\3dmob\` | C3dGrannyModelRes | `0042e900` |
| 13 | pal | `palette\` | CPaletteRes | `00431f70` |
| 14 | spr | `sprite\` | CSprRes | `00433ee0` |
| 15 | wav | `wav\` | CWave | `00435300` |
| 16 | rsw | (none) | C3dWorldRes | `00435810` |

The indexes assume the initializers run in address order, as MSVC's static-initializer
table lists them. Nothing outside the manager depends on the index values.

### GetResource

`GetResource(name)` (`004dff60`):

1. **Null name:** return null.
2. **Lower-case a copy** of the name (128 bytes, `LowerCaseName` `004e1040`: the table at
   `00612714` maps only A–Z).
3. **Find the type.** The extension is the part after the last `.` (`GetExtension`
   `004e0e70`), and its type index comes from the extension map (`GetTypeIndex`
   `004e0e90`). An unknown or missing extension gives null; the client formats
   "ResourceError : %s is Undefined Ext" (`00612854`) but never shows it.
4. **Build the path:** the type's folder, then the lower-cased name. `AddTypeFolder`
   (`004e0df0`) leaves out the folder when the name already starts with it.
5. **If the type's map holds the path** (`FindLoadedResource` `004e0d60`), touch the
   resource and return it.
6. **Otherwise load it:**
   1. Clone the type's prototype (slot 2) and `Load` the path (slot 3).
   2. If that fails, look the lower-cased name up in the `ResNameTable` map
      (`MapResourceName` `004e0f30`: the mapped name, or the name itself when it is
      absent). Build the path again from it, and `Load` that.
   3. If that also fails, call `OnLoadError` (slot 5), destroy the clone (slot 0), and
      return null.
7. **Store it:** `SetNameAndType(path, index)`, insert the resource into the type's map,
   and return it. A resource loaded through `ResNameTable` is stored under the mapped
   path.

`GetResource` does not change the reference count; callers `AddRef` and `Release`.

`Exists(name)` (`004e0190`) runs steps 2 to 5. If the path is not loaded, it asks
`DataFileExists` of the path. It does not consult `ResNameTable`.

### ResNameTable.txt

`LoadResNameTable` (`004e0940`) clears the map, reads `ResNameTable.txt` (`00612838`)
through `DataFile`, and scans it once. Starting from an empty key:

- **`\r` or `\n`** ends a token without using it.
- **`#`** ends a token:
  - if the key is empty, the token becomes the key;
  - otherwise the token is the value, the pair is inserted, and key and value are
    cleared.
- **Text after the last `#`** is ignored.

Insertion keeps the first value of a repeated key. Keys and values are compared as
written. Lookups use the lower-cased name, so only lower-case keys can match; all 558
lines of the shipped table are lower-case `key#value#` lines.

### Unloading

| Function | Address | Deletes |
|----------|---------|---------|
| `UnloadExpiredResources` | `004e0450` | each resource with no references, unused for 15,000 ms or more (`00612710`) |
| `UnloadUnusedResources` | `004e0540` | each resource with no references |
| `UnloadUnusedResourcesOfType(extension)` | `004e0620` | the same, for one type |
| `UnloadResource` | `004e0280` | one resource, whatever its count |
| `Clear` | `004e0310` | every resource; `WinMain` calls it at shutdown |

Every deletion is `Reset` (slot 4), then the destructor (slot 0).

`CWorld::UnloadWorld` calls `UnloadExpiredResources`. `004e0c80` writes every loaded
resource's name, one per line ("%s\n", mode "wt"), to a file: a debugging aid with no
callers.

## In the port

`src/res/`:

| File | Holds |
|------|-------|
| `data_path` | `MakeDataPath` |
| `data_files` | the archive plus loose files: `Load`, `Exists` |
| `data_file` | `DataFile` |
| `resource` | `Resource` (`CRes`) and `ResourceManager` |

**Loose files on case-sensitive file systems.** The original passes `data\...` to
`CreateFileA` relative to its current directory, the install directory, so Windows
ignores case and accepts `\`. The port resolves the same path against the install
directory on every system:
- it splits the path at `\` and `/`;
- it drops `.` components and lets `..` climb one level, never above the install
  directory (`data\..\_tmpEmblem\x.ebm` is `_tmpEmblem/x.ebm` there);
- it matches each component without regard to A–Z case, as the hashed name does;
- when a directory holds several names that differ only in case, it tries the exact name
  first, then the others in sorted order, and the first path that leads to the file
  wins.

Archive entries still win over loose files, as in the original.

**Absolute paths.** A name with a `:` becomes an absolute Windows path, which a portable
client cannot open. The port treats it as not found.
