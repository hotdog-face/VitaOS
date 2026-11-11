#!/usr/bin/env python3
"""Read a VitaOS crash dump (.psp2dmp) and say where it crashed.

    python3 tools/crashdump.py psp2core-...psp2dmp [build/vitaos]

With the ELF of the same release (build/vitaos, built with -g from the
release tag with the same VitaSDK), code addresses are turned into
function:line with arm-vita-eabi-addr2line. Without it, offsets are printed
as vitaos+0x... . Standard library only.

How 1.8.4 was mapped (2026-11): check out the tag, build with
`cmake -DCMAKE_C_FLAGS=-g ..`, and confirm that sleeping threads land on
sceKernelDelayThreadCB: if they do, the layout matches the release.
"""
import gzip, os, shutil, struct, subprocess, sys

u32 = lambda d, o: struct.unpack_from('<I', d, o)[0]
u16 = lambda d, o: struct.unpack_from('<H', d, o)[0]
STOP = {0x30002: 'undefined instruction', 0x30003: 'prefetch abort (bad jump)',
        0x30004: 'data abort (bad memory access)', 0x60080: 'division by zero'}


def cstr(d, o):
    return d[o:d.index(b'\0', o)].decode('latin1')
def load(path):
    d=gzip.open(path).read()
    phoff=u32(d,0x1c); phent=u16(d,0x2a); phnum=u16(d,0x2c)
    notes={}; segs=[]
    for i in range(phnum):
        p=phoff+i*phent
        typ,off,va,pa,fs,ms=struct.unpack_from('<6I',d,p)
        if typ==4:
            q=off
            while q<off+fs:
                nsz,dsz,nt=struct.unpack_from('<3I',d,q); q+=12
                name=d[q:q+nsz].rstrip(b'\0').decode(); q+=(nsz+3)&~3
                notes[name]=d[q:q+dsz]; q+=(dsz+3)&~3
        elif typ==1: segs.append((va,d[off:off+fs]))
    return notes,segs
def modules(n):
    d=n['MODULE_INFO']; num=u32(d,4); off=8; mods=[]
    for _ in range(num):
        name=cstr(d,off+0x24); ns=u32(d,off+0x4c); off+=0x50; sg=[]
        for k in range(ns):
            sg.append((u32(d,off+8),u32(d,off+12))); off+=0x14
        off+=0x10; mods.append((name,sg))
    return mods
def threads(n):
    d=n['THREAD_INFO']; num=u32(d,4); off=8; th={}
    for _ in range(num):
        sz=u32(d,off); t=d[off:off+sz]
        th[u32(t,4)]=dict(name=cstr(t,8),stop=u32(t,0x74),status=u16(t,0x30),pc=u32(t,0x9c)); off+=sz
    d=n['THREAD_REG_INFO']; num=u32(d,4); off=8
    for _ in range(num):
        sz=u32(d,off); t=d[off:off+sz]; tid=u32(t,4)
        if tid in th: th[tid]['regs']=[u32(t,8+4*x) for x in range(16)]
        off+=sz
    return th
def where(mods,a):
    for name,sg in mods:
        for k,(s,sz) in enumerate(sg):
            if s<=a<s+sz: return name,k,a-s
    return None
def readv(segs,a,n):
    for va,data in segs:
        if va<=a<va+len(data): return data[a-va:a-va+n]


def main():
    if len(sys.argv) < 2:
        print(__doc__); return 1
    elf = sys.argv[2] if len(sys.argv) > 2 else None
    vitasdk = os.environ.get('VITASDK', '/usr/local/vitasdk')
    a2l = shutil.which('arm-vita-eabi-addr2line') or os.path.join(vitasdk, 'bin', 'arm-vita-eabi-addr2line')
    n, segs = load(sys.argv[1])
    mods = modules(n)
    th = threads(n)

    def fmt(a):
        w = where(mods, a)
        if not w:
            return hex(a)
        name, k, off = w
        s = '%s %s@%d+0x%x' % (hex(a), name, k + 1, off)
        if elf and k == 0 and name.startswith('vitaos') and os.path.exists(a2l):
            out = subprocess.run([a2l, '-f', '-i', '-e', elf, hex(0x81000000 + off)], capture_output=True, text=True).stdout
            s += '  => ' + ' | '.join(x.strip() for x in out.splitlines())
        return s

    for tid, t in th.items():
        r = t.get('regs', [0] * 16)
        flag = '   <== CRASHED: %s' % STOP.get(t['stop'], hex(t['stop'])) if t['stop'] else ''
        print('%-26s PC %s%s' % (t['name'], fmt(t['pc']), flag))
        if t['stop']:
            print('    LR', fmt(r[14]))
            print('    R0-R12', ' '.join('%x' % x for x in r[:13]))
            sp = r[13]
            for i in range(96):
                v = readv(segs, sp + 4 * i, 4)
                if not v:
                    break
                v = struct.unpack('<I', v)[0]
                w = where(mods, v)
                if w and w[1] == 0 and w[0].startswith('vitaos'):
                    print('    stack+%03x' % (4 * i), fmt(v))
    return 0


if __name__ == '__main__':
    sys.exit(main())
