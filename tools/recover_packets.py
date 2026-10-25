"""Name the packet handlers of the two packet dispatchers.

CGameMode::DispatchPackets and CLoginMode::DispatchPackets take each complete packet
from CRagConnection::RecvPacket, read its id with CRagConnection::GetPacketId and switch
on it (index and jump tables). Rather than decoding the switch by shape, every id from 0
to 0xffff is run through it in the emulator (tools/emu): from the instruction after the
id is read, with EAX = id, until the first call. That call is:
  - the handler, when it is passed the packet buffer. Called with ECX = the mode (the
    dispatcher's own this) and popping its argument itself, it is a method of the mode
    class: the compiler loads ECX only for a member call, and many handlers never read
    their this;
  - the default, when it is the call that ids no case lists reach (CRagConnection::vf02,
    which drops the receive buffer);
  - CRagConnection::RecvPacket again, when the case only skips the packet;
  - otherwise the case is handled inline.
A handler is named HandlePacket_<id>, with every id it handles when there are several.
Curated names win (analysis/labels/inferred.functions.tsv). Some handlers print the
packet's own name in a debug message ("PACKET_ZC_COMPASS"); that name goes into the
evidence and the report.

Writes:
  analysis/labels/packets.functions.tsv  the handlers
  analysis/exports/Ragexe.exe.packets.tsv  mode, id, kind, handler, name, packet name, call site
"""
import os
import re
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from binindex import BinIndex, ROOT  # noqa: E402
from capstone import x86  # noqa: E402
from emu import Emulator  # noqa: E402
from emu.emulator import STACK_BASE  # noqa: E402
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_ECX, UC_X86_REG_ESI,  # noqa: E402
                               UC_X86_REG_ESP)

LABELS = os.path.join(ROOT, "analysis", "labels")
EXPORTS = os.path.join(ROOT, "analysis", "exports")
RECV_PACKET = 0x418FC0
GET_PACKET_ID = 0x419240
DISPATCHERS = [(0x524D80, "CGameMode"), (0x53CFA0, "CLoginMode")]
PACKET_NAME = re.compile(r"PACKET_[A-Z0-9_]+")
THIS = 0x0BAD0000              # a marker for the mode object; the switch never dereferences it
MAX_ID = 0xFFFF
STEP_LIMIT = 400


def read_tsv(path):
    with open(path, encoding="utf-8") as f:
        lines = [line for line in f.read().splitlines() if not line.startswith("#")]
    header = lines[0].split("\t")
    return [dict(zip(header, line.split("\t"))) for line in lines[1:]]


class Dispatcher:
    def __init__(self, bx, emu, entry, cls):
        self.bx, self.emu, self.entry, self.cls = bx, emu, entry, cls
        fn = bx.functions[entry]
        ins = bx.insns(fn)
        self.calls = {i.address: bx.call_target(i) for i in ins if i.id == x86.X86_INS_CALL}
        self.rets = {i.address for i in ins if i.id == x86.X86_INS_RET}
        self.lo, self.hi = fn.entry, fn.entry + fn.size
        recv = next(k for k, i in enumerate(ins) if self.calls.get(i.address) == RECV_PACKET)
        lea = next(p for p in reversed(ins[:recv]) if p.id == x86.X86_INS_LEA)
        self.buffer_disp = lea.operands[1].mem.disp
        get_id = next(k for k, i in enumerate(ins) if k > recv and self.calls.get(i.address) == GET_PACKET_ID)
        self.start = ins[get_id + 1].address

    def run(self, pid):
        """(call site, target, first stack argument, ECX) of the first call, or None."""
        uc = self.emu.uc
        ebp = STACK_BASE + 0x80000
        uc.reg_write(UC_X86_REG_EBP, ebp)
        uc.reg_write(UC_X86_REG_ESP, ebp + self.buffer_disp - 0x100)
        uc.reg_write(UC_X86_REG_ESI, THIS)
        uc.reg_write(UC_X86_REG_EAX, pid)
        self.hit = None
        uc.emu_start(self.start, 0, count=STEP_LIMIT)
        return self.hit

    def on_code(self, uc, address, size, _):
        if not self.lo <= address < self.hi:
            uc.emu_stop()
        elif address in self.calls:
            esp = uc.reg_read(UC_X86_REG_ESP)
            self.hit = (address, self.calls[address], self.emu.read_u32(esp), uc.reg_read(UC_X86_REG_ECX))
            uc.emu_stop()
        elif address in self.rets:
            uc.emu_stop()


class Packets:
    def __init__(self):
        self.bx = BinIndex()
        self.emu = Emulator()
        self.curated = {int(r["address"], 16): r for r in read_tsv(os.path.join(LABELS, "inferred.functions.tsv"))}

    def packet_names(self, fn):
        names = set()
        for i in self.bx.insns(fn):
            for v in self.bx.data_refs(i):
                m = PACKET_NAME.match(self.bx.cstring(v, 80) or "")
                if m:
                    names.add(m.group(0))
        return sorted(names)

    def walk(self, d):
        hook = self.emu.uc.hook_add(UC_HOOK_CODE, d.on_code, begin=d.lo, end=d.hi)
        try:
            results = {pid: d.run(pid) for pid in range(MAX_ID + 1)}
        finally:
            self.emu.uc.hook_del(hook)
        default = results[MAX_ID]
        buffer = STACK_BASE + 0x80000 + d.buffer_disp
        rows = []
        for pid, hit in results.items():
            if hit is not None and default is not None and hit[:2] == default[:2]:
                continue
            if hit is None:
                rows.append((pid, "inline", None, None))
            elif hit[1] == RECV_PACKET:
                rows.append((pid, "ignored", None, hit))
            elif hit[2] == buffer & 0xFFFFFFFF:
                rows.append((pid, "handler", hit[1], hit))
            else:
                rows.append((pid, "inline", hit[1], hit))
        return rows, default

    def run(self):
        labels, report = {}, []
        self.binary_names = {}
        for entry, cls in DISPATCHERS:
            d = Dispatcher(self.bx, self.emu, entry, cls)
            rows, default = self.walk(d)
            by_handler = defaultdict(list)
            for pid, kind, target, hit in rows:
                report.append((cls, pid, kind, target, hit[0] if hit else None))
                if kind == "handler":
                    by_handler[(target, hit[3] == THIS)].append(pid)
            for (target, with_this), ids in by_handler.items():
                fn = self.bx.functions.get(target)
                method = with_this and fn is not None and any(
                    i.id == x86.X86_INS_RET and i.operands for i in self.bx.insns(fn))
                ns = cls if method else "Global"
                name = "HandlePacket_" + "_".join(f"{i:04X}" for i in sorted(ids))
                known = self.packet_names(fn) if fn is not None else []
                ev = (f"case {', '.join(f'0x{i:04x}' for i in sorted(ids))} of {cls}::DispatchPackets "
                      f"{entry:08x}, passed the packet" + ("; ECX = the mode, and it pops its argument" if method else "")
                      + (f"; prints {', '.join(known)}" if known else ""))
                labels[target] = (ns, name, "__thiscall" if method else "", ev)
                self.binary_names[target] = known
            print(f"{cls}::DispatchPackets {entry:08x}: {len(by_handler)} handlers for "
                  f"{sum(len(v) for v in by_handler.values())} ids, "
                  f"{sum(1 for r in rows if r[1] == 'inline')} handled inline, "
                  f"{sum(1 for r in rows if r[1] == 'ignored')} ignored; other ids call "
                  f"{default[1]:08x} at {default[0]:08x}")
        gen = "# generated by tools/recover_packets.py; edit the script, not this file\n"
        with open(os.path.join(LABELS, "packets.functions.tsv"), "w", encoding="utf-8") as f:
            f.write("address\tnamespace\tname\tcalling_convention\tevidence\n" + gen)
            for t, (ns, name, cc, ev) in sorted(labels.items()):
                if t not in self.curated:
                    f.write(f"{t:08x}\t{ns}\t{name}\t{cc}\t{ev}\n")
        with open(os.path.join(EXPORTS, "Ragexe.exe.packets.tsv"), "w", encoding="utf-8") as f:
            f.write("mode\tid\tkind\thandler\tname\tpacket_name\tcall_site\n")
            for cls, pid, kind, target, site in sorted(report):
                name = self.curated[target]["name"] if target in self.curated else labels.get(target, ("", ""))[1]
                f.write(f"{cls}\t0x{pid:04x}\t{kind}\t{f'{target:08x}' if target else ''}\t{name if kind == 'handler' else ''}\t"
                        f"{','.join(self.binary_names.get(target, [])) if kind == 'handler' else ''}\t"
                        f"{f'{site:08x}' if site else ''}\n")
        shared = sum(1 for _, n, _, _ in labels.values() if n.count("_") > 1)
        print(f"handlers named: {len(labels)} ({shared} handle several ids, "
              f"{sum(1 for v in self.binary_names.values() if v)} print their packet's name); "
              f"curated: {sum(1 for t in labels if t in self.curated)}")


if __name__ == "__main__":
    Packets().run()
