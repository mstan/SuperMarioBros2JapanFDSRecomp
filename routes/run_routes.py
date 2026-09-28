#!/usr/bin/env python3
"""Run this game's scripted routes (routes.toml) on a headless build.

  python routes/run_routes.py build/headless/<Game>.exe                 # coverage
  python routes/run_routes.py <exe> --capture-log cyc_captures.txt      # refresh the capture file

Each route boots the image from power-on and replays its --input file.
Routes naming the same `save` share one disk save file, fresh at the start
of this run, in route order (a later route boots from what an earlier one
saved). A route passes when nothing ran on the interpreter: ROM, CPU RAM and
PRG RAM interpreted cycles all 0 (the 7 cycles of the power-on reset
sequence are the only non-native cycles). Exit status 1 if any route fails.
"""
import argparse
import re
import subprocess
import sys
import tomllib
from pathlib import Path

HERE = Path(__file__).resolve().parent


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('exe', type=Path)
    ap.add_argument('--capture-log', type=Path, help='merge RAM code that ran interpreted into this file')
    ap.add_argument('--out', type=Path, default=Path('build/routes'), help='logs, saves, screenshots')
    ap.add_argument('--only', nargs='*', help='route names to run (default: all)')
    ap.add_argument('--shot-every', type=int, default=0, help='also save every Nth frame')
    args = ap.parse_args()
    routes = tomllib.loads((HERE / 'routes.toml').read_text(encoding='utf-8'))['route']
    args.out.mkdir(parents=True, exist_ok=True)
    fresh, failed = set(), 0
    for r in routes:
        name = r['name']
        if args.only and name not in args.only:
            continue
        cmd = [str(args.exe.resolve()), '--frames', str(r['frames'])]
        if 'input' in r:
            cmd += ['--input', str(HERE / r['input'])]
        cmd += [str(a) for a in r.get('args', [])]
        if 'save' in r:
            save = args.out / (r['save'] + '.fdssave')
            if r['save'] not in fresh:
                save.unlink(missing_ok=True)
                fresh.add(r['save'])
            cmd += ['--save-file', str(save.resolve())]
        if args.capture_log:
            cmd += ['--capture-log', str(args.capture_log.resolve())]
        cmd += ['--screenshot', str((args.out / f'{name}.png').resolve())]
        if args.shot_every:
            cmd += ['--shot-every', str(args.shot_every)]
        run = subprocess.run(cmd, capture_output=True, text=True)
        log = run.stdout + run.stderr
        (args.out / f'{name}.log').write_text(log, encoding='utf-8')
        mode = re.search(r'mode=\S+ frames=(\d+) cycles=(\d+) native_cycles=(\d+) \(([\d.]+)%\)', log)
        interp = re.search(r'interpreted: ROM (\d+) .*?RAM (\d+) \(', log)
        prg = re.search(r'PRG RAM (\d+) \(', log)
        views = re.search(r'native: ROM \d+ \(([\d.]+)%\)\s+RAM views \d+ \(([\d.]+)%\)', log)
        ram_insns = re.search(r'(\d+) RAM instructions interpreted', log)
        if run.returncode or not mode or not interp:
            print(f'{name}: FAILED (exit {run.returncode}); see {args.out / (name + ".log")}')
            failed += 1
            continue
        rom_i, ram_i, prg_i = int(interp.group(1)), int(interp.group(2)), int(prg.group(1)) if prg else 0
        ok = rom_i == 0 and ram_i == 0 and prg_i == 0
        failed += not ok
        print(f'{name}: {mode.group(1)} frames, native {mode.group(4)}% '
              f'(ROM {views.group(1)}%, RAM views {views.group(2)}%), '
              f'interpreted ROM {rom_i} / CPU RAM {ram_i} / PRG RAM {prg_i} cycles, '
              f'{ram_insns.group(1) if ram_insns else "?"} RAM insns  {"PASS" if ok else "FAIL"}')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
