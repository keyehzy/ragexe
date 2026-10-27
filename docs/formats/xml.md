# XML: XmlDocument

The client parses two XML files with its own small parser:
- **`clientinfo.xml`:** `LoadClientInfo`, `004ca090`, from `WinMain`, into `g_XmlDocument`
  (`0067c9a0`).
- **`PetTalkTable.xml`:** `ClientState::LoadPetTalkTable`, `005a8aa0`, from `ClientState`'s
  constructor, into the `XmlDocument` at `ClientState+0xc60`.

The pet talk loader does not check that the file opened; a missing file parses as an
empty document.

## Elements

An `XmlElement` (0x28 bytes, constructor `004e61e0`):

| Offset | Field |
|-------:|-------|
| +0x00 | its name, a `std::string` |
| +0x10 | its text: the character data directly inside it, all run together |
| +0x20 | the next sibling |
| +0x24 | the first child |

**The document** is an element too. `XmlDocument`'s constructor (`004c95b0`) names it
"XMLDocument" (`0060bcf8`).

**Children** are added in document order (`AddChild` `004e6420`). `FindChild`
(`004e6480`) returns the first child with a name, and `FindNextSibling` (`004e6500`) the
next.

## Parse

`XmlDocument::Parse` (`004e6590`) takes (begin, end). It parses the bytes as the
document's content, and succeeds when the document then has a first child.

**The content of an element** (`ParseContent`, `004e65c0`), byte by byte until the end:

- **`<` then `/`:** a closing tag.
  1. Read its name (below).
  2. Skip past the next `>`.
  3. If the name is the element's own, the content ends here.
  4. Otherwise the tag is skipped, and the content goes on.

  At the top level, `</XMLDocument>` ends the parse.
- **Any other `<`:** an element (`ParseElement`, `004e6870`):
  - **`<?` or `<!`:** skipped past the next `>`. A comment containing `>` ends there.
  - **Otherwise:**
    1. A new element is added to the parent's children, named by the tag name.
    2. Its attributes are ignored.
    3. If the byte before the tag's `>` is `/`, the element is empty.
    4. Otherwise its content follows the `>`.
- **`\r` or `\n`:** skipped.
- **`&`:** an entity (`DecodeXmlEntity`, `004e6b20`):
  1. The two bytes after it are compared with `lt` and `gt`, then the three with `amp`, up
     to the first byte that differs.
  2. `<`, `>` or `&` is appended to the text. Anything else appends nothing.
  3. The parse skips past the next `;`.
- **Any other byte,** spaces, tabs and NULs included, is appended to the element's text.
  So the indentation between child elements stays in the parent's text.

An element left open at the end just ends with the data.

**A tag name** (`ReadXmlTagName`, `004e69f0`) runs from the byte after `<` (or `</`) to
the first space, tab, `>`, `/` or the end. It may be empty.

**Where it reads past the data:**
- **A missing `>` or `;`:** the skip stops at the end and returns one past it. The parse
  then reads on beyond the data.
- **`<` as the last byte:** the byte after it is read.
- **An `&`** reads two bytes after it, and a third after `am`.

### In data.grf

- **`PetTalkTable.xml`** (184,371 bytes) holds 2,509 elements under
  `<monster_talk_table>`:
  - one element per pet (`<poring>` …);
  - under it, one per mood (`<hungry>` …);
  - under that, the lines (`<feeding>`, `<hunting>`, `<danger>` …).
  - It starts with an XML declaration and is indented with tabs.
  - Some lines run over several lines of the file.
  - It has no entities and no comments.
- **`clientinfo.xml`** holds the service, server type and connection list.
- **The patches** add `monsterskillinfo.xml` files, which the client never loads.

## In the port

`src/res/xml.{h,cpp}`:
- `XmlElement`, a tree with the name, text and children;
- `ParseXml`;
- `LoadPetTalkTable`.

**Where the port differs:**
- **It fails** where the client reads past the data.
- **Nesting:** it stops at 10,000 levels; the client's recursion has no limit.

**The oracle:** `tools/xml_oracle.py` runs `XmlDocument::Parse` in the emulation oracle.
- **Placement:** each document ends at the end of mapped memory, so a read past it
  faults.
- **What it runs on:**
  - `PetTalkTable.xml`, `clientinfo.xml` and the patches' `monsterskillinfo.xml`, plus
    `ClientState`'s pet talk loader on `data.grf`'s file;
  - 20 synthetic documents: nesting, attributes, declarations and comments, every
    entity rule, stray and top-level closing tags, odd names, random tags and random
    bytes.
- **What it compares:** every element in document order (depth, name, text and child
  count) with `ragres decode-xml`.
- **The 8 synthetic documents** that make the original fault are each a read past the
  data, and the port fails on each.
