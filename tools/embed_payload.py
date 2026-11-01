#!/usr/bin/env python3
"""Generate a C++ source with embedded installer payload arrays."""
import sys


def emit_array(f, name, data):
    f.write(f"extern const unsigned char {name}[];\n")
    f.write(f"extern const size_t {name}Size;\n")
    f.write(f"const unsigned char {name}[] = {{\n")
    for i in range(0, len(data), 16):
        chunk = data[i:i + 16]
        f.write("    " + ", ".join(f"0x{b:02X}" for b in chunk) + ",\n")
    f.write("};\n")
    f.write(f"const size_t {name}Size = sizeof({name});\n\n")


def main():
    if len(sys.argv) != 5:
        print("usage: embed_payload.py <dll> <launcher> <ini> <out.cpp>")
        return 1
    payloads = [
        ("kPayloadDll", sys.argv[1]),
        ("kPayloadLauncher", sys.argv[2]),
        ("kPayloadIni", sys.argv[3]),
    ]
    with open(sys.argv[4], "w", encoding="ascii", newline="\n") as f:
        f.write("#include <cstddef>\n\n")
        for name, path in payloads:
            with open(path, "rb") as source:
                emit_array(f, name, source.read())
    print(f"wrote {sys.argv[4]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
