"""Compares the port's clientinfo.xml reading with the original (docs/formats/clientinfo.md).

usage: .venv/bin/python -I tools/clientinfo_oracle.py [--jobs J]

Runs LoadClientInfo (004ca090) in the emulation oracle, then LoadConnectionSettings
(004ca260) for a chosen connection where asked, and CLoginMode::LoadConnectionList
(0053ecd0). It reads back every global they set (service and server type, flags,
connection, admin ids, language, names) and the messages ShowErrorMessage showed, and
compares them with `ragres decode-clientinfo`. The client's data section is restored
before each run. Inputs: data.grf's clientinfo.xml and synthetic ones; where the original
faults, the port must fail.
"""
import argparse
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import oracle_pool  # noqa: E402
from emu import EmulationError, Emulator  # noqa: E402

LOAD_CLIENT_INFO, LOAD_CONNECTION_SETTINGS, LOAD_CONNECTION_LIST = 0x4CA090, 0x4CA260, 0x53ECD0
XML_DOCUMENT_CTOR, G_XML_DOCUMENT = 0x4C95B0, 0x67C9A0
DATA_SECTION = (0x605000, 0x86000)
LOGIN_MODE_SIZE, CONNECTION_LIST, CONNECTION_ENTRY = 0x1400, 0x1354, 0x48
LANGUAGES = {0x67C8E8: "default", 0x67C448: "traditional-chinese", 0x67C3B8: "thai"}


def quoted(text):
    out = '"'
    for b in text:
        c = chr(b)
        if c in '"\\':
            out += "\\" + c
        elif b < 0x20 or b > 0x7E:
            out += f"\\x{b:02x}"
        else:
            out += c
    return out + '"'


def clientinfo(service=b"america", server=b"primary", flags=b"", connections=(), extra=b""):
    def element(name, text):
        return b"" if text is None else b"\t<" + name + b">" + text + b"</" + name + b">\r\n"
    body = element(b"servicetype", service) + element(b"servertype", server) + flags + extra
    for c in connections:
        body += b"\t<connection>\r\n" + c + b"\t</connection>\r\n"
    return b'<?xml version="1.0" encoding="euc-kr" ?>\r\n<clientinfo>\r\n' + body + b"</clientinfo>\r\n"


def connection(display=b"Server", address=b"10.0.0.1", port=b"6900", version=b"18", langtype=b"1",
               web=b"http://example", aid=None):
    def element(name, text):
        return b"" if text is None else b"\t\t<" + name + b">" + text + b"</" + name + b">\r\n"
    out = (element(b"display", display) + element(b"desc", b"None") + element(b"address", address) +
           element(b"port", port) + element(b"version", version) + element(b"langtype", langtype) +
           element(b"registrationweb", web))
    if aid is not None:
        out += b"\t\t<aid>" + b"".join(b"<admin>" + a + b"</admin>" for a in aid) + b"</aid>\r\n"
    return out


def synthetic():
    """(name, bytes, connection index or -1, whether the original faults)."""
    yield "an empty file", b"", -1, False
    yield "no clientinfo element", b"<other/>", -1, False
    for service in (b"korea", b"america", b"japan", b"china", b"taiwan", b"thai", b"indonesia",
                    b"philippine", b"malaysia", b"singapore", b"usa", b" america", None):
        yield f"service {service}", clientinfo(service=service, connections=[connection(langtype=None)]), -1, False
    for server in (b"primary", b"sakray", b"local", b"beta", None):
        yield f"korea, server {server}", clientinfo(service=b"korea", server=server), -1, False
        yield f"america, server {server}", clientinfo(server=server, connections=[connection()]), -1, False
        yield f"china, server {server}", clientinfo(service=b"china", server=server,
                                                    connections=[connection(langtype=b"3")]), -1, False
    for lang in (b"0", b"2", b"3", b"4", b"5", b"6", b"9", b"12", b"-1", b"x"):
        yield f"langtype {lang}", clientinfo(connections=[connection(langtype=lang)]), -1, False
    yield "flags", clientinfo(flags=b"<hideaccountlist/><passwordencrypt2></passwordencrypt2>"), -1, False
    yield "passwordencrypt only", clientinfo(flags=b"<passwordencrypt>0</passwordencrypt>"), -1, False
    yield "no connection", clientinfo(), -1, False
    admins = [b"5", b"-3", b"100001", b"2", b" 7x", b"4294967295"]
    yield "admin ids", clientinfo(connections=[connection(aid=admins)]), -1, False
    yield "partial connection", clientinfo(connections=[connection(address=None, port=None, web=None,
                                                                    version=b"0x10")]), -1, False
    yield "NUL in an address", clientinfo(connections=[connection(address=b"1.2\x00.3")]), -1, False
    three = [connection(display=b"A", address=b"1.1.1.1", aid=[b"9"]),
             connection(display=b"B", address=b"2.2.2.2", langtype=b"4"),
             connection(display=b"C\xb0", address=b"3.3.3.3", aid=[b"8", b"1"])]
    for k in (-1, 0, 1, 2, 3, 7):
        yield f"three connections, choose {k}", clientinfo(connections=three), k, False
    yield "an aid with no admin", clientinfo(connections=[connection(aid=[])]), -1, True
    yield "the real file's shape, korea", clientinfo(service=b"korea", connections=three), 1, False


def init():
    emu = Emulator(max_instructions=200_000_000).init_crt()
    emu.serve_data_files({})
    emu.call(XML_DOCUMENT_CTOR, this=G_XML_DOCUMENT)
    snapshot = emu.mem_read(*DATA_SECTION)
    return emu, snapshot, emu.scratch(size=LOGIN_MODE_SIZE)


def cstring(emu, slot):
    p = emu.read_u32(slot)
    return emu.read_cstr(p, 4096, "latin-1").encode("latin-1") if p else b""


def read_string(emu, p):
    data, length = emu.read_u32(p + 4), emu.read_u32(p + 8)
    return emu.mem_read(data, length) if data and length else b""


def signed(v):
    return v - (1 << 32) if v & 0x80000000 else v


def work(state, item):
    emu, snapshot, login_mode = state
    path, index = item
    with open(path, "rb") as f:
        data = f.read()
    emu.mem_write(DATA_SECTION[0], snapshot)
    mark = emu.heap_mark()
    emu.serve_data_files({"data\\clientinfo.xml": data})
    emu.api.messages.clear()
    try:
        emu.call(LOAD_CLIENT_INFO, emu.cstr("clientinfo.xml"))
        if index >= 0:
            emu.call(LOAD_CONNECTION_SETTINGS, index)
        emu.mem_write(login_mode, bytes(LOGIN_MODE_SIZE))
        emu.call(LOAD_CONNECTION_LIST, this=login_mode)
    except EmulationError:
        emu.heap_release(mark)
        return "faults"
    u = emu.read_u32
    byte = lambda a: emu.mem_read(a, 1)[0]  # noqa: E731

    def ids(vector):
        first, last = u(vector + 4), u(vector + 8)
        values = [signed(u(p)) for p in range(first, last, 4)] if first else []
        return f"{len(values)}" + "".join(f" {v}" for v in values)

    lines = ["ok", f"service_type {signed(u(0x67C9D4))}", f"server_type {signed(u(0x67C9DC))}",
             f"hide_account_list {byte(0x67C9CC)}", f"password_encrypt {byte(0x67C9CD)}",
             f"password_encrypt2 {byte(0x67C9CE)}",
             f"address {quoted(cstring(emu, 0x60BBB8))}", f"port {quoted(cstring(emu, 0x60BBBC))}",
             f"registration_web {quoted(cstring(emu, 0x60BBC0))}", f"version {signed(u(0x67C9C8))}",
             f"admins_a {ids(0x67C988)}", f"admins_b {ids(0x67C978)}",
             f"charset {signed(u(0x67C9D8))}", f"code_page {u(0x67C998)}",
             f"language {LANGUAGES.get(u(0x67C99C), hex(u(0x67C99C)))}",
             f"flag_67c9cf {byte(0x67C9CF)}", f"flag_67c9d0 {byte(0x67C9D0)}",
             f"value_60bbd8 {signed(u(0x60BBD8))}"]
    for name, slot in (("licence", 0x60BBB4), ("registry_path", 0x60BBD4), ("game_name", 0x60BBD0),
                       ("executable", 0x60BBC4), ("patcher", 0x60BBC8), ("run_message", 0x60BBCC)):
        lines.append(f"{name} {quoted(cstring(emu, slot))}")
    for _, text in emu.api.messages:
        lines.append(f"message {quoted(text.encode('latin-1') if isinstance(text, str) else text)}")
    first, last = u(login_mode + CONNECTION_LIST + 4), u(login_mode + CONNECTION_LIST + 8)
    for p in range(first, last, CONNECTION_ENTRY) if first else []:
        lines.append(f"connection {quoted(read_string(emu, p))} {quoted(read_string(emu, p + 16))}")
    emu.heap_release(mark)
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--install", default=os.path.join(ROOT, "original", "install"))
    ap.add_argument("--ragres", default=os.path.join(ROOT, "build", "dev", "src", "ragres"))
    ap.add_argument("--jobs", type=int, default=0, help="worker processes (0: one per core but one)")
    args = ap.parse_args()
    with tempfile.TemporaryDirectory() as tmp:
        cases = []  # (name, path, index, faults)
        out = os.path.join(tmp, "data.grf")
        subprocess.run([args.ragres, "extract", os.path.join(args.install, "data.grf"), out,
                        "data\\clientinfo.xml"], capture_output=True)
        real = os.path.join(out, "data", "clientinfo.xml")
        if os.path.exists(real):
            for index in (-1, 0, 1):
                cases.append((f"data.grf's clientinfo.xml, connection {index}", real, index, False))
        real_count = len(cases)
        for k, (name, data, index, faults) in enumerate(synthetic()):
            path = os.path.join(tmp, f"synthetic{k}.xml")
            with open(path, "wb") as f:
                f.write(data)
            cases.append((name, path, index, faults))
        originals = oracle_pool.run([(p, i) for _, p, i, _ in cases], init, work, args.jobs or None,
                                    label="clientinfo")
        mismatches = []
        for (name, path, index, faults), want in zip(cases, originals):
            command = [args.ragres, "decode-clientinfo"] + (["--connection", str(index)] if index >= 0 else [])
            got = subprocess.run(command + [path], capture_output=True, text=True,
                                 encoding="latin-1").stdout.strip()
            got = "\n".join(["ok" if got.startswith("0 ok") else "failed"] + got.split("\n")[1:]) \
                if got else ""
            if want == "faults" or faults:
                if want != "faults" or not got.startswith("failed"):
                    mismatches.append(f"{name}: expected the original to fault and the port to fail; "
                                      f"original {want[:30]!r}, port {got[:30]!r}")
                continue
            if got != want:
                diff = [f"    port     {a}\n    original {b}" for a, b in zip(got.split("\n"), want.split("\n"))
                        if a != b]
                if got.count("\n") != want.count("\n"):
                    diff.append(f"    {got.count(chr(10)) + 1} lines against {want.count(chr(10)) + 1}")
                mismatches.append(f"{name}:\n" + "\n".join(diff[:6]))
    print(f"{real_count} runs on the real file and {len(cases) - real_count} synthetic ones compared, "
          f"{len(mismatches)} differ")
    for line in mismatches[:20]:
        print("MISMATCH", line)
    print("FAILED" if mismatches else "OK")
    return 1 if mismatches else 0


if __name__ == "__main__":
    sys.exit(main())
