#!/usr/bin/env python3
"""Extract the RT_GROUP_ICON/RT_ICON resources from a PE file into a .ico."""
import struct
import sys

import pefile


def main():
    if len(sys.argv) != 3:
        print("usage: extract_game_icon.py <exe> <out.ico>")
        return 1
    pe = pefile.PE(sys.argv[1], fast_load=False)
    groups = []
    icons = {}
    for entry in pe.DIRECTORY_ENTRY_RESOURCE.entries:
        if entry.id == 14:
            for group in entry.directory.entries:
                data_entry = group.directory.entries[0].data.struct
                raw = pe.get_data(data_entry.OffsetToData, data_entry.Size)
                if len(raw) < 6:
                    continue
                _, _, count = struct.unpack_from("<HHH", raw, 0)
                items = []
                for i in range(count):
                    w, h, cc, res, planes, bpp, size, icon_id = struct.unpack_from("<BBBBHHIH", raw, 6 + i * 14)
                    items.append((w, h, cc, planes, bpp, size, icon_id))
                groups.append(items)
        elif entry.id == 3:
            for icon in entry.directory.entries:
                data_entry = icon.directory.entries[0].data.struct
                icons[icon.id] = pe.get_data(data_entry.OffsetToData, data_entry.Size)

    best = max(groups, key=lambda items: max((i[0] or 256) * (i[1] or 256) for i in items), default=None)
    if not best:
        print("no icon groups found")
        return 2

    images = []
    for (w, h, cc, planes, bpp, size, icon_id) in best:
        data = icons.get(icon_id)
        if data is None:
            continue
        images.append((w, h, cc, planes, bpp, data))

    header = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    directory = bytearray()
    payload = bytearray()
    for (w, h, cc, planes, bpp, data) in images:
        directory += struct.pack("<BBBBHHII", w, h, cc, 0, planes, bpp, len(data), offset)
        payload += data
        offset += len(data)

    with open(sys.argv[2], "wb") as f:
        f.write(header + directory + payload)
    print(f"wrote {sys.argv[2]}: {len(images)} image(s), {sum(len(x[5]) for x in images)} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
