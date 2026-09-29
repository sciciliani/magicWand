#!/usr/bin/env python3
"""wand_log.py — talk to the wand from a terminal over Bluetooth (bleak).

Records the live feature stream to CSV (handy for tuning gestures) and lets you
type raw protocol commands (see docs/PROTOCOL.md).

    pip install bleak
    python tools/wand_log.py                 # connect to the first "Wand-*"
    python tools/wand_log.py --csv run.csv   # also record F lines
"""
import argparse
import asyncio
import csv
import sys
import time

from bleak import BleakClient, BleakScanner

NUS_RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"  # write
NUS_TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"  # notify


async def main(args):
    print("Scanning for a wand…")
    dev = await BleakScanner.find_device_by_filter(
        lambda d, ad: (d.name or ad.local_name or "").startswith(args.name), timeout=15
    )
    if not dev:
        sys.exit("No wand found. Pick it up to wake it and try again.")
    out = open(args.csv, "w", newline="") if args.csv else None
    writer = csv.writer(out) if out else None
    if writer:
        writer.writerow(["t", "right", "up", "twist", "thrust", "energy"])
    buf = ""

    def on_notify(_, data: bytearray):
        nonlocal buf
        buf += data.decode(errors="replace")
        while "\n" in buf:
            line, buf = buf.split("\n", 1)
            if line.startswith("F "):
                if writer:
                    writer.writerow([f"{time.time():.3f}", *line.split()[1:]])
                if not args.quiet_stream:
                    print(line)
            else:
                print("←", line)

    async with BleakClient(dev) as client:
        print(f"Connected to {dev.name}. Type commands (HELLO, SEND 0, …), Ctrl-C to quit.")
        await client.start_notify(NUS_TX, on_notify)

        async def send(line):
            data = (line + "\n").encode()
            for i in range(0, len(data), 180):
                await client.write_gatt_char(NUS_RX, data[i : i + 180], response=True)

        await send("HELLO")
        if args.csv:
            await send("STREAM 1")
        loop = asyncio.get_running_loop()
        while True:
            line = await loop.run_in_executor(None, sys.stdin.readline)
            if not line:
                break
            if line.strip():
                await send(line.strip())


if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("--name", default="Wand", help="advertised name prefix")
    p.add_argument("--csv", help="record the feature stream to this CSV file")
    p.add_argument("--quiet-stream", action="store_true", help="don't print F lines")
    try:
        asyncio.run(main(p.parse_args()))
    except KeyboardInterrupt:
        pass
