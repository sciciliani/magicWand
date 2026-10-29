#!/usr/bin/env python3
"""Turn the compiled firmware (.hex) into a Gecko bootloader update file (.gbl)
and publish it for the web app:  app/fw/wand-<version>.gbl + app/fw/latest.json

    python3 tools/make_gbl.py build/fw/MagicWand.ino.hex [--commander PATH]

With Simplicity Commander (installed with the Silicon Labs Arduino core) the
.gbl is made by `commander gbl create`, Silicon Labs' own tool. Without it,
this script writes a plain (unsigned, uncompressed) GBL itself:
    header tag, application tag, one program-data tag per flash region, end tag + CRC32.
"""
import argparse, json, os, re, shutil, struct, subprocess, sys, time, zlib, glob

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONFIG = os.path.join(ROOT, "firmware", "MagicWand", "config.h")
OUT = os.path.join(ROOT, "app", "fw")

TAG_HEADER, TAG_APP, TAG_PROG, TAG_END = 0x03A617EB, 0xF40A0AF4, 0xFE0101FE, 0xFC0404FC
APP_TYPE_MCU = 1 << 4


def read_hex(path):
    """Intel HEX -> sorted list of (address, bytes) contiguous regions."""
    mem, base = {}, 0
    for line in open(path):
        line = line.strip()
        if not line.startswith(":"):
            continue
        b = bytes.fromhex(line[1:])
        n, addr, typ, data = b[0], (b[1] << 8) | b[2], b[3], b[4:4 + b[0]]
        if (sum(b) & 0xFF) != 0:
            sys.exit(f"bad checksum in {path}: {line}")
        if typ == 0:
            for i, x in enumerate(data):
                mem[base + addr + i] = x
        elif typ == 2:
            base = int.from_bytes(data, "big") << 4
        elif typ == 4:
            base = int.from_bytes(data, "big") << 16
        elif typ == 1:
            break
    regions, cur, start, last = [], bytearray(), None, None
    for a in sorted(mem):
        if last is not None and a - last > 1:
            gap = a - last - 1
            if gap <= 256:
                cur += b"\xff" * gap  # small hole: fill, keep one region
            else:
                regions.append((start, bytes(cur)))
                cur, start = bytearray(), a
        if start is None:
            start = a
        cur.append(mem[a])
        last = a
    if cur:
        regions.append((start, bytes(cur)))
    return regions


def tag(tid, payload):
    return struct.pack("<II", tid, len(payload)) + payload


def gbl_from_regions(regions, version=0):
    out = tag(TAG_HEADER, struct.pack("<II", 0x03000000, 0))
    out += tag(TAG_APP, struct.pack("<III", APP_TYPE_MCU, version, 0) + b"\x00" * 16)
    for addr, data in regions:
        while len(data) % 4:
            data += b"\xff"
        out += tag(TAG_PROG, struct.pack("<I", addr) + data)
    end_head = struct.pack("<II", TAG_END, 4)
    crc = zlib.crc32(out + end_head) & 0xFFFFFFFF
    return out + end_head + struct.pack("<I", crc)


def find_commander():
    pats = [os.path.expanduser("~/Library/Arduino15/packages/SiliconLabs/tools/simplicitycommander/*/Commander.app/Contents/MacOS/commander"),
            os.path.expanduser("~/Library/Arduino15/packages/SiliconLabs/tools/simplicitycommander/*/commander"),
            os.path.expanduser("~/.arduino15/packages/SiliconLabs/tools/simplicitycommander/*/commander"),
            "/Applications/Commander.app/Contents/MacOS/commander"]
    for p in pats:
        hits = sorted(glob.glob(p))
        if hits:
            return hits[-1]
    return shutil.which("commander")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("hex")
    ap.add_argument("--commander", default=os.environ.get("COMMANDER"))
    ap.add_argument("--no-commander", action="store_true", help="always use the built-in GBL writer")
    a = ap.parse_args()

    ver = re.search(r'#define\s+FW_VERSION\s+"([^"]+)"', open(CONFIG).read()).group(1)
    os.makedirs(OUT, exist_ok=True)
    name = f"wand-{ver}.gbl"
    dst = os.path.join(OUT, name)
    regions = read_hex(a.hex)
    app_bytes = sum(len(d) for _, d in regions)
    print(f"firmware {ver}: {app_bytes} bytes in {len(regions)} region(s), first at 0x{regions[0][0]:08X}")
    if regions[0][0] < 0x08006000:
        sys.exit("the .hex includes the bootloader area (0x08000000): use the plain sketch .hex, not *with_bootloader*")

    cmd = None if a.no_commander else (a.commander or find_commander())
    if cmd:
        print(f"using Simplicity Commander: {cmd}")
        subprocess.run([cmd, "gbl", "create", dst, "--app", a.hex], check=True)
    else:
        print("Simplicity Commander not found: writing the GBL with the built-in writer")
        open(dst, "wb").write(gbl_from_regions(regions))

    data = open(dst, "rb").read()
    if data[:4] != struct.pack("<I", TAG_HEADER):
        sys.exit(f"{dst} doesn't look like a GBL file")
    info = {"version": ver, "file": name, "size": len(data), "crc32": f"{zlib.crc32(data) & 0xFFFFFFFF:08x}",
            "built": time.strftime("%Y-%m-%d %H:%M")}
    json.dump(info, open(os.path.join(OUT, "latest.json"), "w"), indent=1)
    print(f"wrote app/fw/{name} ({len(data)} bytes) and app/fw/latest.json")
    if len(data) > 512 * 1024:
        print("WARNING: bigger than the bootloader's 512 KB slot: the wand will refuse it")


if __name__ == "__main__":
    main()
