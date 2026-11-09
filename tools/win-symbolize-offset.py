#!/usr/bin/env python3
"""把 Windows 崩溃事件里的「模块 + 错误偏移」反查成最近的导出函数。

用法：
    python tools/win-symbolize-offset.py <dll 路径> <偏移> [更多偏移...]

原理：PE 导出表里记录了每个导出函数的 RVA，按地址排序后找 <= 崩溃偏移的最近一个，
即可给出「哪个函数 + 函数内偏移」。Qt/MinGW 构建的 DLL 导出了全部 C++ 符号（mangled），
所以即使没有 PDB 也能定位到函数级，足以对 0xc0000005 这类崩溃做归因。
"""
import bisect
import struct
import sys


def load_exports(path):
    data = open(path, "rb").read()
    if data[:2] != b"MZ":
        raise SystemExit("不是 PE 文件")
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    if data[e_lfanew:e_lfanew + 4] != b"PE\0\0":
        raise SystemExit("PE 签名错误")

    coff = e_lfanew + 4
    _, nsections, _, _, _, optsize, _ = struct.unpack_from("<HHIIIHH", data, coff)
    opt = coff + 20
    is64 = struct.unpack_from("<H", data, opt)[0] == 0x20B
    dd = opt + (112 if is64 else 96)
    export_rva, _ = struct.unpack_from("<II", data, dd)

    sec = opt + optsize
    sections = []
    for i in range(nsections):
        b = sec + i * 40
        vsize, vaddr, rawsize, rawptr = struct.unpack_from("<IIII", data, b + 8)
        sections.append((vaddr, vsize, rawptr, rawsize))

    def off(rva):
        for vaddr, vsize, rawptr, rawsize in sections:
            if vaddr <= rva < vaddr + max(vsize, rawsize):
                return rawptr + (rva - vaddr)
        return None

    if export_rva == 0:
        raise SystemExit("该模块没有导出表")

    eo = off(export_rva)
    nfuncs, nnames = struct.unpack_from("<II", data, eo + 20)
    af, an, ao = struct.unpack_from("<III", data, eo + 28)
    fo, no, oo = off(af), off(an), off(ao)

    funcs = [struct.unpack_from("<I", data, fo + 4 * i)[0] for i in range(nfuncs)]
    out = []
    for i in range(nnames):
        nrva = struct.unpack_from("<I", data, no + 4 * i)[0]
        oi = struct.unpack_from("<H", data, oo + 2 * i)[0]
        n_off = off(nrva)
        end = data.index(b"\0", n_off)
        out.append((funcs[oi], data[n_off:end].decode("latin1", "replace")))
    out.sort()
    return out


def demangle(name):
    """极简 Itanium ABI 反修饰，只处理常见的 _ZN...E 形式，够读就行。"""
    if not name.startswith("_Z"):
        return name
    body = name[2:]
    if body.startswith("N") and body.endswith("E"):
        body = body[1:-1]
        parts, i = [], 0
        while i < len(body):
            j = i
            while j < len(body) and body[j].isdigit():
                j += 1
            if j == i:
                i += 1
                continue
            n = int(body[i:j])
            parts.append(body[j:j + n])
            i = j + n
        if parts:
            cls = "::".join(parts[:-1])
            fn = parts[-1]
            return f"{cls}::{fn}" if cls else fn
    return name


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    dll = sys.argv[1]
    exports = load_exports(dll)
    rvas = [r for r, _ in exports]
    for spec in sys.argv[2:]:
        target = int(spec, 0)
        i = bisect.bisect_right(rvas, target) - 1
        if i < 0:
            print(f"0x{target:x}: 无匹配导出")
            continue
        lo_rva, lo_name = exports[i]
        hi_rva = exports[i + 1][0] if i + 1 < len(exports) else None
        print(f"0x{target:08x}  +0x{target - lo_rva:<5x}  {demangle(lo_name)}")
        print(f"            原始符号: {lo_name}")
        if hi_rva:
            print(f"            下一个导出: 0x{hi_rva:x}（函数体长度上限 0x{hi_rva - lo_rva:x}）")


if __name__ == "__main__":
    main()
