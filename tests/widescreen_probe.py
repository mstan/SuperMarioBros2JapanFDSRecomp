#!/usr/bin/env python3
"""Widescreen mod probes for Super Mario Bros. 2 (Japan) FDS.

Every check boots the image from power-on in a headless run (the live Fit
check drives a hidden window over TCP with SDL's dummy video driver) and
reads the mod's always-on per-frame log (--widescreen-log): nothing arms a
capture or pauses the machine.

  python tests/widescreen_probe.py --exe <mod build> --baseline <untouched stock build> --out <dir> [checks...]

Checks (default: all):
  stock      mod build, mod off / off override / packages installed but
             disabled: full-machine hash identical to the stock build over
             routes/play.txt (9000 frames)
  native     32:9 with --widescreen-enemies native: the world decode, hooks
             and compositor leave the machine hash identical to stock, and the
             native playfield pixels inside the wide picture equal stock's
  seam       every world-scene frame of the play route and a sweep of level
             starts at 16:9 / 21:9 / 32:9: terrain aligned with the native
             background at both joins, cached columns equal the live parser's
  enemies    1-1: residents drawn beyond the native screen, frozen until
             native activation in classic mode, moving in viewport mode,
             none falling through the floor
  plants     5-1 (upside-down piranhas, id $04) and 1-1 (id $0D): plant
             residents drawn outside the native screen, moving in viewport
             mode, frozen in classic mode
  platforms  4-3 lifts ($28/$29) and B-4 ($25/$28/$29): independent platforms
             drawn outside the native screen, moving in viewport, frozen in
             classic
  wind       7-1: the wind's leaf field repeats into the margins; worlds 1-4
             draw no leaves
  flag       1-1 from page 10: authored flag preview, native takeover with no
             missing frame, lowering, the 1-2 entry without a ghost flag
  world9     world 8's ending (the fixture enters its victory mode) loads
             SM2DATA3 and starts world 9: the ending stays pillarboxed, world
             9's terrain decodes from the new overlay, aligned and verified
  savestate  a state saved mid-level and loaded in a new process continues
             with identical machine hashes, mod state and pictures
  presets    16:9 / 21:9 / 32:9 widths, off = 256, Fit from the drawable size
             (headless --present-size) across its clamp
  mods       a saved Mods selection (21:9, centered status bar and camera,
             classic activation) applies through the package's plugin
  fitlive    (needs --window-exe) a hidden window resized over TCP: Fit
             follows every size
  menu       (needs --window-exe) the runtime menu's Mods rows on a hidden
             window: enabling the package turns the widescreen on at once
             (Fit), its Aspect row changes the width, the selection is saved

Fixtures: --dev-start W:L:A[:PAGE] and --dev-hold ADDR:VALUE (InjuryTimer, so
a route survives enemies) are test-only options of the mod build.
"""
import argparse
import json
import os
import shutil
import socket
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PLAY = ROOT / 'routes' / 'play.txt'
WIDTHS = {'16:9': 426, '21:9': 560, '32:9': 854}
PACKAGE = 'super-mario-bros-2-japan-fds.enhancement.widescreen'
INJURY_TIMER = '0x79E:8'


def geometry_width(w, h):
    """common/nes_video_geometry.h, Fit."""
    aspect = min(max(w / h, 256 / 240), 32 / 9)
    width = 2 * int((240 * aspect) / 2 + 0.5)
    return max(256, min(864, width)) & ~1


class Probe:
    def __init__(self, args):
        self.a = args
        self.out = Path(args.out).resolve()
        self.out.mkdir(parents=True, exist_ok=True)
        self.results = {}
        self.failures = []

    # ---- running ----
    def run(self, tag, args, exe=None, frames=2400, log=True):
        d = self.out / tag
        d.mkdir(parents=True, exist_ok=True)
        cmd = [str(exe or self.a.exe), '--frames', str(frames)] + [str(x) for x in args]
        if log:
            cmd += ['--widescreen-log', str(d / 'log.jsonl')]
        with open(d / 'out.txt', 'w') as f:
            f.write(' '.join(cmd) + '\n')
            f.flush()
            rc = subprocess.run(cmd, cwd=ROOT, stdout=f, stderr=subprocess.STDOUT).returncode
        if rc:
            raise RuntimeError(f'{tag}: exit {rc} (see {d / "out.txt"})')
        return d

    def runs(self, jobs):
        with ThreadPoolExecutor(self.a.jobs) as ex:
            return list(ex.map(lambda j: self.run(*j), jobs))

    @staticmethod
    def log(d):
        return [json.loads(line) for line in open(d / 'log.jsonl')]

    @staticmethod
    def hashes(path):
        return [line.split(' ', 1)[1] for line in open(path)]

    @staticmethod
    def input_file(name, lines):
        return lines

    def write_input(self, name, lines):
        p = self.out / name
        p.write_text('\n'.join(lines) + '\n')
        return p

    def stand(self):
        return self.write_input('stand.txt', ['900 START', '910 -'])

    def running(self, period=40, start=1150, end=4000):
        lines = ['900 START', '910 -']
        f = start
        while f < end:
            lines += [f'{f} RIGHT+A', f'{f + period // 2} RIGHT']
            f += period
        return self.write_input(f'run{period}.txt', lines)

    def check(self, name, ok, **numbers):
        self.results.setdefault(name, []).append(dict(ok=bool(ok), **numbers))
        print(f"  {'PASS' if ok else 'FAIL'} {name}: " + ', '.join(f'{k}={v}' for k, v in numbers.items()), flush=True)
        if not ok:
            self.failures.append(name)

    # ---- checks ----
    def c_stock(self):
        if not self.a.baseline:
            raise RuntimeError('stock needs --baseline')
        mods = self.out / 'stock_mods'
        shutil.rmtree(mods, ignore_errors=True)
        shutil.copytree(ROOT / 'mods' / 'preloaded', mods)
        jobs = [('stock_base', ['--input', PLAY, '--hash-out', self.out / 'stock_base' / 'h.txt'], self.a.baseline, 9000, False),
                ('stock_mod', ['--input', PLAY, '--hash-out', self.out / 'stock_mod' / 'h.txt'], None, 9000, False),
                ('stock_off', ['--input', PLAY, '--widescreen', 'off', '--hash-out', self.out / 'stock_off' / 'h.txt'], None, 9000, False),
                ('stock_pkg', ['--input', PLAY, '--mods-root', mods, '--hash-out', self.out / 'stock_pkg' / 'h.txt'], None, 9000, False)]
        for t, *_ in jobs:
            (self.out / t).mkdir(exist_ok=True)
        self.runs(jobs)
        base = self.hashes(self.out / 'stock_base' / 'h.txt')
        for t in ('stock_mod', 'stock_off', 'stock_pkg'):
            h = self.hashes(self.out / t / 'h.txt')
            same = sum(1 for x, y in zip(base, h) if x == y)
            self.check('stock', same == len(base) == len(h) == 9000, run=t, identical=f'{same}/{len(base)}')

    def c_native(self):
        if not self.a.baseline:
            raise RuntimeError('native needs --baseline')
        from PIL import Image
        import numpy as np
        for t in ('native_base', 'native_ws'):
            (self.out / t).mkdir(exist_ok=True)
        self.runs([('native_base', ['--input', PLAY, '--hash-out', self.out / 'native_base' / 'h.txt',
                                    '--screenshot', self.out / 'native_base' / 's.png', '--shot-every', 150],
                    self.a.baseline, 9000, False),
                   ('native_ws', ['--input', PLAY, '--widescreen', '32:9', '--widescreen-enemies', 'native',
                                  '--hash-out', self.out / 'native_ws' / 'h.txt',
                                  '--present-out', self.out / 'native_ws' / 'p.png', '--present-every', 150], None, 9000)])
        base = self.hashes(self.out / 'native_base' / 'h.txt')
        ws = self.hashes(self.out / 'native_ws' / 'h.txt')
        same = sum(1 for x, y in zip(base, ws) if x == y)
        self.check('native', same == len(base) == len(ws), what='machine hash', identical=f'{same}/{len(base)}')
        log = self.log(self.out / 'native_ws')
        compared = equal = composed = 0
        for f in range(0, 9000, 150):
            s = self.out / 'native_base' / f's_{f:05d}.png'
            p = self.out / 'native_ws' / f'p_{f:05d}.png'
            if not s.exists() or not p.exists():
                continue
            a = np.asarray(Image.open(s).convert('RGB'))
            b = np.asarray(Image.open(p).convert('RGB'))
            x0 = (b.shape[1] - 256) // 2
            compared += 1
            composed += bool(log[f]['world_scene'] and log[f]['valid'])
            equal += bool((a[32:240] == b[32:240, x0:x0 + 256]).all())
        self.check('native', compared > 50 and equal == compared, what='native playfield pixels',
                   frames=compared, composed=composed, equal=equal)

    def seam_stats(self, rows):
        scenes = [r for r in rows if r['world_scene'] and r['valid'] and r['samples']]
        aligned = sum(1 for r in scenes if r['offset_errors'][4] == min(r['offset_errors']))
        verified = max((r['verified_columns'] for r in rows), default=0)
        mism = max((r['mismatched_columns'] for r in rows), default=0)
        return len(scenes), aligned, verified, mism

    def c_seam(self):
        run = self.running()
        jobs = []
        for m in WIDTHS:
            t = 'seam_play_' + m.replace(':', 'x')
            jobs.append((t, ['--input', PLAY, '--widescreen', m], None, 3000))
        for start in ('0:1:1', '1:0:0', '3:2:2', '4:0:0', '6:0:0', '9:0:0', '12:1:1'):
            for m in ('16:9', '32:9'):
                t = f"seam_{start.replace(':', '_')}_{m.replace(':', 'x')}"
                jobs.append((t, ['--input', run, '--widescreen', m, '--dev-start', start], None, 2400))
        for (t, args, *_), d in zip(jobs, self.runs(jobs)):
            rows = self.log(d)
            scenes, aligned, verified, mism = self.seam_stats(rows)
            width = max(r['render_width'] for r in rows)
            m = args[args.index('--widescreen') + 1]
            self.check('seam', scenes > 300 and aligned == scenes and verified > 0 and mism == 0 and width == WIDTHS[m],
                       run=t, world_frames=scenes, aligned=aligned, columns_verified=verified, mismatched=mism, width=width)

    def residents(self, rows, kinds, native_x0_of):
        """Per frame: residents of these kinds drawn beyond the native screen."""
        out = []
        for r in rows:
            if not r['game_engine']:
                continue
            cam = r['camera_x']
            beyond = [e for e in r['enemies'] if e['kind'] in kinds and e['slot'] < 0 and e['sprites'] and not e['dead']
                      and (e['x'] < cam - 16 or e['x'] >= cam + 256)]
            out.append((r, beyond))
        return out

    def activation_pair(self, name, start, kinds, frames=1500, extra=()):
        stand = self.stand()
        jobs = [(f'{name}_{mode}', ['--input', stand, '--widescreen', '32:9', '--widescreen-enemies', mode,
                                    '--dev-start', start, *extra], None, frames) for mode in ('classic', 'viewport')]
        result = {}
        for (t, *_), d in zip(jobs, self.runs(jobs)):
            rows = self.log(d)
            # An actor: its authored record, group member, spawn X and kind
            # (plants all come from the area parser, record 255).
            key = lambda e: (e['record'], e['member'], e['spawn_x'], e['kind'])
            activated = set()   # ever in a native slot or active: the game started it
            beyond, moved, frozen_moved, fell = set(), set(), set(), 0
            first = {}
            frames = 0
            for r in rows:
                if not r['game_engine']:
                    continue
                cam, any_beyond = r['camera_x'], False
                for e in r['enemies']:
                    if e['kind'] not in kinds or e['dead']:
                        continue
                    k = key(e)
                    out = e['slot'] < 0 and e['sprites'] and (e['x'] < cam - 16 or e['x'] >= cam + 256)
                    if out:
                        any_beyond = True
                        beyond.add(k)
                        pos = first.setdefault(k, (e['x'], e['y']))
                        if pos != (e['x'], e['y']):
                            moved.add(k)
                            if k not in activated:
                                frozen_moved.add(k)
                        if e['y'] > 0xE0 and not 0x25 <= e['kind'] <= 0x2C:
                            fell += 1
                    if e['slot'] >= 0 or e['active']:
                        activated.add(k)
                frames += any_beyond
            result[t] = dict(frames_with_beyond=frames, actors_beyond=len(beyond), moved=len(moved),
                             moved_before_activation=len(frozen_moved), fell=fell)
        c = result[f'{name}_classic']
        v = result[f'{name}_viewport']
        self.check(name, c['actors_beyond'] > 0 and c['moved_before_activation'] == 0, mode='classic', **c)
        self.check(name, v['actors_beyond'] > 0 and v['moved'] > 0 and v['fell'] == 0, mode='viewport', **v)

    def c_enemies(self):
        self.activation_pair('enemies', '0:0:0', {0x00, 0x02, 0x03, 0x06, 0x0E})

    def c_plants(self):
        self.activation_pair('plants', '4:0:0', {0x04, 0x0D})
        self.activation_pair('plants_1_1', '0:0:0', {0x0D})

    def c_platforms(self):
        self.activation_pair('platforms', '3:2:2', set(range(0x25, 0x2D)))
        self.activation_pair('platforms_b4', '10:3:3', set(range(0x25, 0x2D)), frames=1800)

    def c_wind(self):
        run = self.running()
        jobs = [('wind_7_1', ['--input', run, '--widescreen', '32:9', '--dev-start', '6:0:0', '--dev-hold', INJURY_TIMER], None, 2600),
                ('wind_1_1', ['--input', run, '--widescreen', '32:9', '--dev-start', '0:0:0', '--dev-hold', INJURY_TIMER], None, 2000)]
        d71, d11 = self.runs(jobs)
        rows = [r for r in self.log(d71) if r['game_engine'] and r['wide_frames']]
        windy = [r for r in rows if r['wind'] and r['file_list']]
        with_copies = sum(1 for r in windy if r['leaf_copies'])
        self.check('wind', len(windy) > 60 and with_copies >= len(windy) * 0.9,
                   level='7-1', wind_frames=len(windy), frames_with_leaf_copies=with_copies,
                   max_copies=max((r['leaf_copies'] for r in windy), default=0))
        rows = [r for r in self.log(d11) if r['game_engine']]
        self.check('wind', rows and not any(r['leaf_copies'] for r in rows), level='1-1', frames=len(rows),
                   leaf_copies=sum(r['leaf_copies'] for r in rows))

    def c_flag(self):
        goal = self.running(period=48, start=1080, end=4000)
        d = self.run('flag_1_1', ['--input', goal, '--widescreen', '32:9', '--dev-start', '0:0:0:10',
                                  '--dev-hold', INJURY_TIMER, '--present-out', self.out / 'flag_1_1' / 'p.png',
                                  '--present-every', 50], frames=2600)
        rows = self.log(d)
        area = [r for r in rows if r['game_engine'] and r['area_data'] and r['flag']['world_x'] >= 0]
        preview = [r for r in area if r['flag']['sprites'] and not r['flag']['native']]
        native = [r for r in area if r['flag']['sprites'] and r['flag']['native']]
        first_preview = preview[0]['frame'] if preview else None
        last_native = native[-1]['frame'] if native else None
        gaps = [r['frame'] for r in area if first_preview is not None and last_native is not None
                and first_preview <= r['frame'] <= last_native and not r['flag']['drawn_pixels']]
        beyond = sum(1 for r in preview if r['flag']['world_x'] >= r['camera_x'] + 256)
        ys = [r['flag']['y'] for r in native]
        self.check('flag', preview and native and beyond and not gaps and max(ys) - min(ys) > 100,
                   preview_frames=len(preview), preview_beyond_native=beyond, native_frames=len(native),
                   missing_frames=len(gaps), lowered_from=min(ys) if ys else None, to=max(ys) if ys else None)
        next_area = [r for r in rows if r['area_data'] and r['flag']['world_x'] < 0]
        ghost = sum(1 for r in rows if r['flag']['drawn_pixels'] and (r['flag']['world_x'] < 0 or not r['game_engine']))
        self.check('flag', next_area and ghost == 0, what='transition to 1-2', frames_after=len(next_area),
                   ghost_flag_frames=ghost)

    def c_world9(self):
        run = self.running(end=9000)
        d = self.run('world9', ['--input', run, '--widescreen', '32:9', '--dev-start', '8:0:0',
                                '--dev-hold', INJURY_TIMER, '--present-out', self.out / 'world9' / 'p.png',
                                '--present-every', 500], frames=9000)
        rows = self.log(d)
        ending = [r for r in rows if r['oper_mode'] == 2 and r['world_number'] == 7 and r['oper_task'] >= 5]
        composed_in_ending = sum(1 for a, b in zip(rows, rows[1:]) if b in ending and b['wide_frames'] > a['wide_frames'])
        w9 = [r for r in rows if r['game_engine'] and r['world_number'] == 8 and r['file_list'] == 2]
        scenes, aligned, verified, mism = self.seam_stats(w9)
        composed_w9 = sum(1 for a, b in zip(rows, rows[1:]) if b in w9 and b['wide_frames'] > a['wide_frames'])
        self.check('world9', ending and not composed_in_ending, what='world 8 ending pillarboxed',
                   ending_frames=len(ending), composed=composed_in_ending)
        self.check('world9', scenes > 300 and aligned == scenes and verified > 0 and mism == 0 and composed_w9 == scenes,
                   what='world 9 gameplay', area=hex(w9[0]['area_data']) if w9 else None, world_frames=scenes,
                   aligned=aligned, columns_verified=verified, mismatched=mism, composed=composed_w9)

    def c_savestate(self):
        run = self.running()
        st = self.out / 'ss_a' / 'mid.state'
        (self.out / 'ss_a').mkdir(exist_ok=True)
        (self.out / 'ss_b').mkdir(exist_ok=True)
        common = ['--input', run, '--widescreen', '32:9']
        self.run('ss_a', common + ['--dev-start', '0:1:1', '--save-state', f'1700:{st}',
                                   '--hash-out', self.out / 'ss_a' / 'h.txt', '--present-out', self.out / 'ss_a' / 'p.png',
                                   '--present-every', 100], frames=2600)
        self.run('ss_b', common + ['--load-state', st, '--hash-out', self.out / 'ss_b' / 'h.txt',
                                   '--present-out', self.out / 'ss_b' / 'p.png', '--present-every', 100], frames=2600)
        ha = self.hashes(self.out / 'ss_a' / 'h.txt')[1701:]
        hb = self.hashes(self.out / 'ss_b' / 'h.txt')
        same = sum(1 for x, y in zip(ha, hb) if x == y)
        la = self.log(self.out / 'ss_a')[1701:]
        lb = self.log(self.out / 'ss_b')
        skip = {'frame', 'wide_frames', 'native_frames'}
        mod_same = sum(1 for x, y in zip(la, lb) if {k: v for k, v in x.items() if k not in skip} ==
                       {k: v for k, v in y.items() if k not in skip})
        pics = sorted(p.name for p in (self.out / 'ss_b').glob('p_*.png'))
        pic_same = sum(1 for n in pics if (self.out / 'ss_a' / n).read_bytes() == (self.out / 'ss_b' / n).read_bytes())
        area = la[0]['area_data'] if la else 0
        self.check('savestate', ha and same == len(ha) == len(hb) and mod_same == len(la) == len(lb) and pics
                   and pic_same == len(pics) and la[0]['game_engine'],
                   area=hex(area), machine_frames=f'{same}/{len(ha)}', mod_state_frames=f'{mod_same}/{len(la)}',
                   pictures=f'{pic_same}/{len(pics)}', residents=la[0]['loaded'] if la else 0)

    def c_presets(self):
        stand = self.stand()
        jobs = [(f"preset_{m.replace(':', 'x')}", ['--input', stand, '--widescreen', m], None, 1200) for m in WIDTHS]
        jobs.append(('preset_off', ['--input', stand, '--widescreen', 'off'], None, 1200))
        sizes = [(640, 480), (800, 600), (1280, 720), (1920, 1080), (2560, 1080), (3440, 1440), (3840, 1080),
                 (5120, 1440), (7680, 1440), (1024, 1024), (600, 900)]
        for w, h in sizes:
            jobs.append((f'preset_fit_{w}x{h}', ['--input', stand, '--widescreen', 'fit', '--present-size', f'{w}x{h}'], None, 1200))
        for (t, args, *_), d in zip(jobs, self.runs(jobs)):
            rows = self.log(d)
            last = rows[-1]
            if t == 'preset_off':
                self.check('presets', last['render_width'] == 256 and not any(r['wide_frames'] for r in rows),
                           run=t, width=last['render_width'])
                continue
            if 'fit' in t:
                w, h = map(int, t.rsplit('_', 1)[1].split('x'))
                want = geometry_width(w, h)
            else:
                want = WIDTHS[args[args.index('--widescreen') + 1]]
            self.check('presets', last['render_width'] == want and last['wide_frames'] > 0 or (want == 256 and last['render_width'] == 256),
                       run=t, width=last['render_width'], expected=want)

    def c_mods(self):
        root = self.out / 'mods_saved'
        shutil.rmtree(root, ignore_errors=True)
        shutil.copytree(ROOT / 'mods' / 'preloaded', root)
        (root / 'state.toml').write_text(
            'format_version = 1\n\n[[package]]\nid = "%s"\nversion = "1.0.0"\n\n[[feature]]\npackage_id = "%s"\n'
            'id = "widescreen"\nenabled = true\n\n[feature.values]\naspect = "21-9"\ncamera = "centered"\n'
            'enemy_activation = "classic"\nhud = "center"\n' % (PACKAGE, PACKAGE))
        d = self.run('mods_saved', ['--input', self.stand(), '--mods-root', root], frames=1200)
        last = self.log(d)[-1]
        self.check('mods', last['enabled'] and last['render_width'] == 560 and last['hud_edges'] == 0 and
                   last['room_edges'] == 0 and last['enemy_mode'] == 1 and last['wide_frames'] > 0,
                   width=last['render_width'], hud_edges=last['hud_edges'], room_edges=last['room_edges'],
                   enemy_mode=last['enemy_mode'], composed=last['wide_frames'])

    def window(self, tag, exe, extra):
        """A hidden window (SDL dummy video driver) and a TCP client for it."""
        port = self.a.port
        with socket.socket() as s:
            if s.connect_ex(('127.0.0.1', port)) == 0:
                raise RuntimeError(f'port {port} is in use')
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NESRECOMP_NO_LAUNCHER='1')
        d = self.out / tag
        d.mkdir(exist_ok=True)
        cfg = d / 'config.ini'
        cfg.write_text('')
        rom = next(ROOT.glob('*.fds'))
        log = open(d / 'out.txt', 'w')
        proc = subprocess.Popen([str(exe), str(rom), '--hidden', '--tcp', str(port), '--config', str(cfg),
                                 '--fds-bios', str(ROOT / 'bios' / 'disksys.rom')] + extra,
                                cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT)
        return d, proc, log

    def c_menu(self):
        if not self.a.window_exe:
            raise RuntimeError('menu needs --window-exe')
        # A private copy of the window build: the Mods screen saves its
        # selection in mods/ beside the executable.
        app = self.out / 'menu_app'
        shutil.rmtree(app, ignore_errors=True)
        app.mkdir(parents=True)
        src = Path(self.a.window_exe).parent
        for f in src.glob('*.dll'):
            shutil.copy2(f, app)
        shutil.copy2(self.a.window_exe, app)
        if (src / 'assets').exists():
            shutil.copytree(src / 'assets', app / 'assets')
        shutil.copytree(ROOT / 'mods' / 'preloaded', app / 'mods')
        exe = app / Path(self.a.window_exe).name
        d, proc, log = self.window('menu', exe, [])
        try:
            cmd, wait_frames = self.client(proc, d)
            cmd('window_size', w=1920, h=1080)
            wait_frames(3)
            before = cmd('smb_ws_state')
            self.check('menu', not before['enabled'] and cmd('video')['width'] == 256, what='package off at start',
                       enabled=before['enabled'], width=cmd('video')['width'])
            # The game pauses while the menu is open: no frames to wait for.
            cmd('menu', open=True)
            time.sleep(0.2)
            # Sections are listed on the left; the Mods section's first row
            # is the package's feature. Find it: enter each section, change
            # its first row, look, and undo what was not it.
            row = None
            for i in range(12):
                cmd('menu', nav='accept')
                cmd('menu', nav='right')
                time.sleep(0.15)
                if cmd('smb_ws_state')['enabled']:
                    row = i
                    break
                cmd('menu', nav='left')
                cmd('menu', nav='back')
                cmd('menu', nav='down')
                time.sleep(0.1)
            cmd('menu', open=False)
            # Trying the Display section's first row (fullscreen and back)
            # restores the window's scale size: size it again.
            cmd('window_size', w=1920, h=1080)
            wait_frames(3)
            v = cmd('video')
            st = cmd('smb_ws_state')
            self.check('menu', row is not None and st['compositor'] and v['width'] == 426,
                       what='Mods row enables widescreen (Fit, 1920x1080)', section=row, width=v['width'],
                       compositor=st['compositor'])
            cmd('window_size', w=1024, h=768)
            wait_frames(3)
            v = cmd('video')
            self.check('menu', v['width'] == 320, what='Fit follows the window (1024x768)', width=v['width'])
            # The Aspect row, below the feature's: fit -> 16:9.
            cmd('menu', open=True)
            time.sleep(0.2)
            found = None
            for i in range(12):
                cmd('menu', nav='accept')
                cmd('menu', nav='down')
                cmd('menu', nav='right')
                time.sleep(0.15)
                if cmd('video')['width'] == 426:
                    found = i
                    break
                cmd('menu', nav='left')
                cmd('menu', nav='up')
                cmd('menu', nav='back')
                cmd('menu', nav='down')
                time.sleep(0.1)
            cmd('menu', open=False)
            wait_frames(3)
            v = cmd('video')
            self.check('menu', found is not None and v['width'] == 426, what='Aspect row: fit -> 16:9 (window 1024x768)',
                       width=v['width'])
            state = (app / 'mods' / 'state.toml').read_text() if (app / 'mods' / 'state.toml').exists() else ''
            self.check('menu', 'enabled = true' in state and 'aspect = "16-9"' in state,
                       what='selection saved in mods/state.toml', saved=bool(state))
            cmd('quit')
        finally:
            try:
                proc.wait(timeout=20)
            except subprocess.TimeoutExpired:
                proc.kill()
            log.close()

    def client(self, proc, d):
        port = self.a.port
        sock = None
        for _ in range(200):
            if proc.poll() is not None:
                raise RuntimeError(f'the window exited ({proc.returncode}, see {d / "out.txt"})')
            try:
                sock = socket.create_connection(('127.0.0.1', port), timeout=5)
                break
            except OSError:
                time.sleep(0.1)
        if sock is None:
            raise RuntimeError('no TCP server')
        f = sock.makefile('rw')
        ident = [0]

        def cmd(name, **kw):
            ident[0] += 1
            f.write(json.dumps(dict(cmd=name, id=ident[0], **kw)) + '\n')
            f.flush()
            while True:
                r = json.loads(f.readline())
                if r.get('id') == ident[0]:
                    if not r.get('ok', True):
                        raise RuntimeError(f'{name}: {r.get("err")}')
                    return r

        def wait_frames(n):
            start = cmd('ping')['frame']
            while cmd('ping')['frame'] < start + n:
                time.sleep(0.02)

        return cmd, wait_frames

    def c_fitlive(self):
        exe = self.a.window_exe
        if not exe:
            raise RuntimeError('fitlive needs --window-exe')
        d, proc, log = self.window('fitlive', exe, ['--widescreen', 'fit', '--widescreen-log', str(self.out / 'fitlive' / 'log.jsonl')])
        try:
            cmd, wait_frames = self.client(proc, d)
            sizes = [(1280, 720), (1920, 800), (768, 720), (2560, 1080), (3840, 1080), (5120, 1080), (800, 800), (1024, 768)]
            for w, h in sizes:
                cmd('window_size', w=w, h=h)
                wait_frames(3)
                v = cmd('video')
                dw, dh = v['drawable']
                want = geometry_width(dw, dh)
                self.check('fitlive', v['width'] == want, window=f'{w}x{h}', drawable=f'{dw}x{dh}', width=v['width'],
                           expected=want)
            cmd('quit')
        finally:
            try:
                proc.wait(timeout=20)
            except subprocess.TimeoutExpired:
                proc.kill()
            log.close()


CHECKS = ['stock', 'native', 'seam', 'enemies', 'plants', 'platforms', 'wind', 'flag', 'world9', 'savestate', 'presets',
          'mods', 'fitlive', 'menu']


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--exe', required=True, help='the mod build (headless or window)')
    ap.add_argument('--baseline', help='an untouched stock build of the same image (stock, native)')
    ap.add_argument('--window-exe', help='a window build of the mod (fitlive)')
    ap.add_argument('--port', type=int, default=4391, help='TCP port for fitlive (not 4370, the shared default)')
    ap.add_argument('--out', required=True)
    ap.add_argument('--jobs', type=int, default=4)
    ap.add_argument('checks', nargs='*', default=None)
    a = ap.parse_args()
    for k in ('exe', 'baseline', 'window_exe'):
        if getattr(a, k):
            setattr(a, k, str(Path(getattr(a, k)).resolve()))
    p = Probe(a)
    checks = a.checks or [c for c in CHECKS if not (c in ('fitlive', 'menu') and not a.window_exe)
                          and not (c in ('stock', 'native') and not a.baseline)]
    for c in checks:
        print(f'[{c}]', flush=True)
        t = time.time()
        try:
            getattr(p, 'c_' + c)()
        except Exception as e:  # a crashed check is a failed check
            p.check(c, False, error=str(e))
        print(f'  ({time.time() - t:.0f} s)', flush=True)
    (p.out / 'summary.json').write_text(json.dumps(p.results, indent=1))
    print('FAILED: ' + ', '.join(sorted(set(p.failures))) if p.failures else 'ALL PASS')
    return 1 if p.failures else 0


if __name__ == '__main__':
    sys.exit(main())
