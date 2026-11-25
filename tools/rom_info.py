#!/usr/bin/env python3
"""Prints identifying facts about a local N64 ROM dump.

The runtime validates ROMs with XXH3-64 over the whole file, so that value is
printed alongside the usual hashes. Nothing is written or uploaded.
"""

import argparse
import hashlib
import struct
import sys


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom", help="path to a .z64 dump")
    args = parser.parse_args()

    data = open(args.rom, "rb").read()
    if len(data) < 0x40:
        print("not an N64 ROM (too small)", file=sys.stderr)
        return 1

    entry, libultra, crc1, crc2 = struct.unpack(">IIII", data[8:24])
    name = data[0x20:0x34].decode("ascii", "replace").rstrip("\0 ")
    manufacturer = data[0x3B]
    cart = struct.unpack(">H", data[0x3C:0x3E])[0]
    country = chr(data[0x3E])
    version = data[0x3F]

    print(f"path         {args.rom}")
    print(f"size         0x{len(data):X} ({len(data)} bytes)")
    print(f"magic        {data[:4].hex()}")
    print(f"entry        0x{entry:08X}")
    print(f"libultra     0x{libultra:08X}")
    print(f"crc1/crc2    0x{crc1:08X} / 0x{crc2:08X}")
    print(f"image name   {name!r}")
    print(f"manufacturer 0x{manufacturer:02X} ({chr(manufacturer)!r})  "
          f"cart 0x{cart:04X} ({struct.pack('>H', cart).decode('ascii', 'replace')!r})  "
          f"country {country!r}  version {version}")
    print(f"sha1         {hashlib.sha1(data).hexdigest()}")
    print(f"sha256       {hashlib.sha256(data).hexdigest()}")

    try:
        import xxhash
    except ImportError:
        print("xxh3_64       (pip install xxhash to compute the runtime ROM hash)")
    else:
        print(f"xxh3_64      0x{xxhash.xxh3_64(data).intdigest():016X}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
