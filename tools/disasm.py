#!/usr/bin/env python3
"""Minimal PE x86 disassembler helper (capstone) for research.

Usage: python disasm.py <exe> <hex-va> [count]
Addresses are virtual addresses matching Ghidra listing (image base 0x400000).
"""
import sys
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    path = sys.argv[1]
    va = int(sys.argv[2], 16)
    count = int(sys.argv[3]) if len(sys.argv) > 3 else 80
    pe = pefile.PE(path, fast_load=True)
    image_base = pe.OPTIONAL_HEADER.ImageBase
    rva = va - image_base
    data = None
    for sec in pe.sections:
        if sec.VirtualAddress <= rva < sec.VirtualAddress + max(sec.Misc_VirtualSize, sec.SizeOfRawData):
            off = rva - sec.VirtualAddress
            data = sec.get_data()[off:]
            break
    if data is None:
        print("VA not found in any section")
        return 2
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = False
    for i, insn in enumerate(md.disasm(data, va)):
        if i >= count:
            break
        print(f"{insn.address:08X}  {insn.bytes.hex():<20} {insn.mnemonic:<8} {insn.op_str}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
