# clientinfo.xml

`clientinfo.xml` chooses the service (country), the server type, and the connections the
login screen offers. `WinMain` passes its name, or what `/account:` names, to
`LoadClientInfo` (`004ca090`).

## LoadClientInfo

1. **Open the file** with `DataFile`. If it opens, parse it into `g_XmlDocument`
   (`0067c9a0`, [xml.md](xml.md)). A file that does not open leaves the document empty.
2. **`ReadClientInfoSettings`,** below.
3. **`InitDefaultClientInfo`,** below.
4. **Set the names by the service and server type.**
   - **Korea (0):**
     - `0067c9cf` and `0067c9d0` are set to 1 (0 otherwise);
     - `0060bbd8` becomes 13 (12 otherwise).
   - **The licence file** (`0060bbb4`, which `GetLicenceText` reads) is
     `..\slicence.txt` for service 1 with server type 1, and `..\licence.txt` otherwise.
   - **China (3):**

     | Value | Server type 1 | Otherwise |
     |-------|---------------|-----------|
     | registry path (`0060bbd4`) | `Software\Gravity Soft\RoClient` | the same |
     | game name (`0060bbd0`) | the Chinese name (`0060bf18`, code page 936) | the same |
     | executable (`0060bbc4`) | `SakClient.exe` | `RoClient.exe` |
     | patcher (`0060bbc8`) | `Sakup.exe` | `Patchup.exe` |
     | run message (`0060bbcc`) | `Please run SakClient.exe` | `Please run RoClient.exe` |

   - **Any other service:** registry path `Software\Gravity Soft\Ragnarok`, game name
     `Ragnarok`, and by server type:

     | Server type | Executable | Patcher |
     |------------:|------------|---------|
     | 1 | `Sakray.exe` | `Sakup.exe` |
     | 2 | `RagIndoor.exe` | `Indoorup.exe` |
     | any other | `Ragnarok.exe` | `Patchup.exe` |

     The run message is "Please run" and the executable.

The registry path is where the client keeps its settings under `HKEY_LOCAL_MACHINE`.

## ReadClientInfoSettings

`004c9b20`. The settings are children of `<clientinfo>` (`GetClientInfoElement`,
`004ca870`). With no `<clientinfo>`, it shows "No ClientInfo File !!" and returns.

- **`servicetype`:** its text, compared whole, gives the service type (`0067c9d4`):

  | Text | Type |
  |------|-----:|
  | `korea` | 0 |
  | `america` | 1 |
  | `japan` | 2 |
  | `china` | 3 |
  | `taiwan` | 4 |
  | `thai` | 5 |
  | `indonesia` | 6 |
  | `philippine` | 7 |
  | `malaysia` | 8 |
  | `singapore` | 9 |

  - Anything else shows "ServiceType Error !!".
  - No element shows "No ServiceType !!".
  - Either way the type keeps its value, 0.
- **`servertype`:** `primary` 0, `sakray` 1 or `local` 2 (`0067c9dc`). Otherwise "ServerType
  Error !!" or "No ServerType !!".
- **The flags,** each set when its element exists, whatever its text:
  - `hideaccountlist`: `0067c9cc`;
  - `passwordencrypt`: `0067c9cd`;
  - `passwordencrypt2`: `0067c9cd` and `0067c9ce`.

The messages go through `ShowErrorMessage`.

## InitDefaultClientInfo

`004c97c0`.

1. **Fill both admin lists** with the built-in account ids:
   - `0067c988` from 32 at `0060bbdc`;
   - `0067c978` from 39 at `0060bc5c`.
2. **By service type:**
   - **1 to 9:** `LoadConnectionSettings(0)`, then `SelectLanguage`.
   - **Korea (0):** the port is `6900` and the registration page
     `http://www.ragnarok.co.kr`; the address and version come from the server type:

     | Server type | Address | Version |
     |------------:|---------|--------:|
     | 0 | `211.172.247.192` | 19 |
     | 1 | `211.172.247.192` | 29 |
     | 2 | `192.168.1.241` | 14 |
     | 3 | `211.172.247.192` (port `9676`) | 19 |
     | 4 | `211.172.247.110` | 29 |

     Then `SelectLanguage`. Server types 3 and 4 cannot come from the file.
   - **Any other type:** "InitDefaultClientInfo() - Unknown ServerType !!!", then
     `SelectLanguage`.

So a missing or unknown `servicetype` gives Korea's servers, and the connections in
the file are ignored.

## LoadConnectionSettings

`004ca260` takes (index). It is called for connection 0 at start-up, and again when the
login screen's server is chosen.

1. **Find the connection:** the index-th `<connection>` of `<clientinfo>`. With none,
   nothing changes.
2. **Copy its settings,** each only when its element exists:
   - `address` (`0060bbb8`), `port` (`0060bbbc`) and `registrationweb` (`0060bbc0`): the
     globals point at the element's text, so they read it as a C string;
   - `version` (`0067c9c8`): through `atoi`;
   - `langtype`: through `atoi`, **into the service type**, replacing what `servicetype`
     said.
3. **The admin lists:**
   - **With an `<aid>`:** both lists are emptied, and each `<admin>`'s `atoi` is
     appended to both. Then both are sorted as unsigned numbers.
   - **An `<aid>` with no `<admin>`:** the client reads the first `<admin>` without
     checking that there is one. The element pointer is null.
   - **Without one:** both lists go back to the built-in ids, unsorted.
4. **`SelectLanguage`.**

At start-up `SelectLanguage` therefore runs twice. A message it shows appears twice.

## SelectLanguage

`004ca940` sets, by service type, the font charset (`0067c9d8`), the code page
(`0067c998`) and the language object (`g_pCLanguage`, `0067c99c`):

| Service type | Charset | Code page | Language object |
|-------------:|--------:|----------:|-----------------|
| 0 | 0 | 949 | `g_CLanguage` |
| 2 | 2 | 932 | `g_CLanguage` |
| 3 | 3 | 936 | `g_CLanguageTC` |
| 4 | 4 | 950 | `g_CLanguageTC` |
| 5 | 5 | 874 | `g_CLanguageTH` |
| 6 | 6 | 0 | `g_CLanguage` |
| 1, 7, 8, 9 | 1 | 0 | `g_CLanguage` |
| any other | 1 | 0 | `g_CLanguage`, after "no language setting !!!" |

Code page 0 is the system's.

## The connection list

`CLoginMode::LoadConnectionList` (`0053ecd0`) fills the login screen's list once. It
reads each `<connection>`'s `display` and `desc` text. It returns whether
`hideaccountlist` was absent.

## In data.grf

iRO's file:
- **Settings:** `america`, `primary`, an empty `<hideaccountlist>`.
- **One connection:** `iRO Server`, `38.144.194.2:6900`, version 18, `langtype` 1, a
  registration page, and 19 admin ids.

So the client runs as service 1 with code page 0, as `Ragnarok.exe`.

## In the port

`src/res/client_info.{h,cpp}`:
- `ClientInfo`, every global above;
- `ReadClientInfo`, on a parsed document;
- `LoadClientInfo`, on a file;
- `SelectConnection` (`LoadConnectionSettings`);
- `ListConnections`.

**Where the port differs:** an `<aid>` with no `<admin>` fails.

**The oracle:** `tools/clientinfo_oracle.py` runs `LoadClientInfo` in the emulation
oracle, then `LoadConnectionSettings` for a chosen connection, then the login screen's
`LoadConnectionList`.
- **What it reads back:** every global, the messages shown and the list, against
  `ragres decode-clientinfo`.
- **Between runs** the client's data section is restored.
- **Real runs:** `data.grf`'s file, on its own and choosing connections 0 and 1.
- **54 synthetic files:**
  - every service and server type, known and not, and missing;
  - each `langtype`, flags, partial connections and admin lists, including negative and
    out-of-range ids;
  - a NUL in an address, and three connections chosen in turn;
  - an `<aid>` with no `<admin>`, where the client faults and the port fails.
