#!/usr/bin/env python3
"""Content keys for game.toml [[mod_function_hook]] sites, from the disk image.

  python tools/hook_keys.py "Super Mario Bros. 2 (Japan) (Debug Value 0).fds" [--check game.toml]

A hook site is keyed on the code at its address (runner/cyc/cyc_hooks.h): the
CRC-32 of `length` bytes there, so the repository holds no bytes of the
game. This prints the key of every site game.toml declares, read from the PRG
file that loads over that address, and with --check fails when game.toml
disagrees (the recompiler also refuses a key no disk file holds).
"""
import argparse
import sys
import tomllib
import zlib
from pathlib import Path


def files(image):
    d = image[16:] if image[:4] == b'FDS\x1a' else image
    assert d[0] == 1 and d[1:15] == b'*NINTENDO-HVC*', 'not an FDS side'
    p = 56
    assert d[p] == 2
    p += 2
    out = []
    while p < len(d) and d[p] == 3:
        name = d[p + 3:p + 11].decode('ascii', 'replace').strip()
        addr = d[p + 11] | d[p + 12] << 8
        size = d[p + 13] | d[p + 14] << 8
        kind = d[p + 15]
        p += 16
        assert d[p] == 4
        out.append((name, addr, kind, d[p + 1:p + 1 + size]))
        p += 1 + size
    return out


def key_at(image_files, addr, length):
    """The key in every PRG file that covers [addr, addr + length)."""
    keys = {}
    for name, base, kind, data in image_files:
        if kind != 0 or not (base <= addr and addr + length <= base + len(data)):
            continue
        keys[name] = zlib.crc32(data[addr - base:addr - base + length])
    return keys


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('image', type=Path)
    ap.add_argument('--check', type=Path, help='game.toml to verify')
    args = ap.parse_args()
    image_files = files(args.image.read_bytes())
    game = tomllib.loads((args.check or Path(__file__).resolve().parent.parent / 'game.toml').read_text(encoding='utf-8'))
    bad = 0
    for hook in game.get('mod_function_hook', []):
        addr, length = hook['addr'], hook.get('length', 8)
        keys = key_at(image_files, addr, length)
        want = hook.get('crc32')
        holders = [n for n, k in keys.items() if k == want]
        status = 'ok (' + ', '.join(holders) + ')' if holders else 'MISMATCH'
        if not holders:
            bad += 1
        print(f"{hook.get('id', '?'):45s} ${addr:04X} length={length} "
              + ' '.join(f'{n}=0x{k:08X}' for n, k in keys.items()) + f'  game.toml=0x{want or 0:08X} {status}')
    if args.check and bad:
        sys.exit(1)


if __name__ == '__main__':
    main()
