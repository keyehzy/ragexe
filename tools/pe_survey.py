"""Print a triage summary of a PE file: headers, sections, entropy, imports, rich header."""
import sys, math, datetime, collections
import pefile


def entropy(data):
    if not data:
        return 0.0
    counts = collections.Counter(data)
    n = len(data)
    return -sum(c / n * math.log2(c / n) for c in counts.values())


def main(path):
    pe = pefile.PE(path)
    fh, oh = pe.FILE_HEADER, pe.OPTIONAL_HEADER
    print(f"file      {path}")
    print(f"machine   {hex(fh.Machine)}  timestamp {datetime.datetime.utcfromtimestamp(fh.TimeDateStamp)}")
    print(f"imagebase {hex(oh.ImageBase)}  entry {hex(oh.AddressOfEntryPoint)}  linker {oh.MajorLinkerVersion}.{oh.MinorLinkerVersion}")
    print(f"subsystem {oh.Subsystem}  sizeofimage {hex(oh.SizeOfImage)}  overlay {len(pe.get_overlay() or b'')} bytes")
    print("\nsections:")
    for s in pe.sections:
        name = s.Name.rstrip(b"\0").decode(errors="replace")
        print(f"  {name:8} va={hex(s.VirtualAddress):>9} vsz={hex(s.Misc_VirtualSize):>9} "
              f"raw={hex(s.SizeOfRawData):>9} ent={entropy(s.get_data()):.2f} chr={hex(s.Characteristics)}")
    rich = pe.parse_rich_header()
    if rich:
        print("\nrich header (prodid, build, count):")
        vals = rich["values"]
        for i in range(0, len(vals), 2):
            print(f"  {vals[i] >> 16:4} {vals[i] & 0xffff:6} {vals[i + 1]}")
    if hasattr(pe, "DIRECTORY_ENTRY_IMPORT"):
        print("\nimports:")
        for d in pe.DIRECTORY_ENTRY_IMPORT:
            names = [(i.name or f"#{i.ordinal}".encode()).decode() for i in d.imports]
            print(f"  {d.dll.decode()} ({len(names)}): {', '.join(names[:400])}")


if __name__ == "__main__":
    main(sys.argv[1])
