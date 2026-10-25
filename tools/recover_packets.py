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

Packet lengths: CRagConnection::RecvPacket hands the receive queue to
CRagConnection::CutPacket, which peeks at the id and looks it up in the std::map<int, int>
at CRagConnection + 0x7c. A length of -1 means the packet is variable: its total length is
the word at offset 2. Any other entry is the total length, at least 2, and an id the map
lacks is added with 0 and read as 2 bytes. The map is filled by
CRagConnection::InitPacketLengths, which the constructor calls; the table here comes from
running it in the emulator on an empty map and walking the tree. Each fixed-length
handler's furthest constant read from the packet is checked against its length.

Writes:
  analysis/labels/packets.functions.tsv  the handlers
  analysis/exports/Ragexe.exe.packets.tsv  mode, id, kind, handler, name, packet name,
      length (or variable), furthest read, call site; ids with a length and no handler
      are listed with mode "-"
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
INIT_PACKET_LENGTHS = 0x419400
LENGTH_MAP = 0x7C              # std::map<int, int> in CRagConnection: allocator, head +4, size +0xc
MAP_NIL, MAP_NIL_COUNT = 0x628B3C, 0x628B40   # that map type's shared nil node and its users
VARIABLE = -1
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

    def packet_lengths(self):
        """id -> total length, or VARIABLE, as InitPacketLengths leaves the map."""
        e = Emulator()
        nil = e.alloc(0x18)
        e.write_u32(nil + 0x14, 1)                       # black, no children
        e.write_u32(MAP_NIL, nil)
        e.write_u32(MAP_NIL_COUNT, 1)
        conn = e.alloc(0x100)
        head = e.alloc(0x18)
        e.write_u32(head, head)                          # leftmost
        e.write_u32(head + 4, nil)                       # root
        e.write_u32(head + 8, head)                      # rightmost
        e.write_u32(conn + LENGTH_MAP + 4, head)
        e.call(INIT_PACKET_LENGTHS, this=conn)
        out = {}
        stack, n = [], e.read_u32(head + 4)
        while stack or n != nil:
            while n != nil:
                stack.append(n)
                n = e.read_u32(n)
            n = stack.pop()
            v = e.read_u32(n + 0x10)
            out[e.read_u32(n + 0xc)] = v - (1 << 32) if v & 0x80000000 else v
            n = e.read_u32(n + 8)
        if len(out) != e.read_u32(conn + LENGTH_MAP + 0xc):
            raise SystemExit("the walked length map does not match its size")
        return out

    def furthest_read(self, fn):
        """End of the furthest [packet + n] read, or 0. The packet is the stack argument
        [ebp + 8]. A register that only that load writes holds it for the rest of the
        function; any other copy counts only until the end of its basic block, since the
        sweep is linear and blocks join from other paths."""
        ins = self.bx.insns(fn)

        def is_arg_load(i):
            ops = i.operands
            return i.id == x86.X86_INS_MOV and len(ops) == 2 and ops[0].type == x86.X86_OP_REG \
                and ops[1].type == x86.X86_OP_MEM and ops[1].size == 4 and ops[1].mem.base == x86.X86_REG_EBP \
                and ops[1].mem.index == 0 and ops[1].mem.disp == 8

        writes = defaultdict(int)
        leaders = set()
        for k, i in enumerate(ins):
            try:
                for r in i.regs_access()[1]:
                    writes[r] += 1
            except Exception:
                pass
            if i.group(x86.X86_GRP_JUMP):
                if i.operands and i.operands[0].type == x86.X86_OP_IMM:
                    leaders.add(i.operands[0].imm)
                if k + 1 < len(ins):
                    leaders.add(ins[k + 1].address)
        stable, held, end = set(), set(), 0
        for i in ins:
            if i.address in leaders:
                held.clear()
            ops = i.operands
            if i.id != x86.X86_INS_LEA:
                for op in ops:
                    if op.type == x86.X86_OP_MEM and op.mem.index == 0 and op.mem.disp >= 0 \
                            and (op.mem.base in held or op.mem.base in stable):
                        end = max(end, op.mem.disp + op.size)
            if is_arg_load(i):
                (stable if writes[ops[0].reg] == 1 else held).add(ops[0].reg)
                continue
            if i.id == x86.X86_INS_MOV and len(ops) == 2 and ops[0].type == x86.X86_OP_REG \
                    and ops[1].type == x86.X86_OP_REG and (ops[1].reg in held or ops[1].reg in stable):
                held.add(ops[0].reg)
                continue
            if i.id == x86.X86_INS_CALL:
                held -= {x86.X86_REG_EAX, x86.X86_REG_ECX, x86.X86_REG_EDX}
                continue
            try:
                held -= set(i.regs_access()[1])
            except Exception:
                held.clear()
        return end

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
        lengths = self.packet_lengths()
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
        handled = {pid for _, pid, kind, _, _ in report if kind != "default"}
        for pid in lengths:
            if pid not in handled:
                report.append(("-", pid, "no handler", None, None))
        reads, past, missing = {}, [], []
        with open(os.path.join(EXPORTS, "Ragexe.exe.packets.tsv"), "w", encoding="utf-8") as f:
            f.write("mode\tid\tkind\thandler\tname\tpacket_name\tlength\tfurthest_read\tcall_site\n")
            for cls, pid, kind, target, site in sorted(report, key=lambda r: (r[0] == "-", r[0], r[1])):
                name = self.curated[target]["name"] if target in self.curated else labels.get(target, ("", ""))[1]
                n = lengths.get(pid)
                length = "variable" if n == VARIABLE else "" if n is None else str(max(n, 2))
                far = ""
                if kind == "handler":
                    if target not in reads:
                        reads[target] = self.furthest_read(self.bx.functions[target])
                    far = str(reads[target]) if reads[target] else ""
                    if n is None:
                        missing.append(pid)
                    elif n != VARIABLE and reads[target] > max(n, 2):
                        past.append(f"{cls} 0x{pid:04x} reads {reads[target]} of {n}")
                f.write(f"{cls}\t0x{pid:04x}\t{kind}\t{f'{target:08x}' if target else ''}\t{name if kind == 'handler' else ''}\t"
                        f"{','.join(self.binary_names.get(target, [])) if kind == 'handler' else ''}\t{length}\t{far}\t"
                        f"{f'{site:08x}' if site else ''}\n")
        print(f"packet lengths: {len(lengths)} ids from 0x{min(lengths):04x} to 0x{max(lengths):04x}, "
              f"{sum(1 for v in lengths.values() if v == VARIABLE)} variable; handled ids without a length: "
              f"{', '.join(f'0x{p:04x}' for p in missing) or 'none'}; with a length but no handler: "
              f"{sum(1 for r in report if r[2] == 'no handler')}")
        print(f"fixed-length handlers that read past their length: {len(past)} {past[:6]}")
        shared = sum(1 for _, n, _, _ in labels.values() if n.count("_") > 1)
        print(f"handlers named: {len(labels)} ({shared} handle several ids, "
              f"{sum(1 for v in self.binary_names.values() if v)} print their packet's name); "
              f"curated: {sum(1 for t in labels if t in self.curated)}")


if __name__ == "__main__":
    Packets().run()
