"""Derive guarded original FDS code views without distributing RAM/ROM bytes.

Only executed PCs and rewritten addresses are source-controlled. Every code
byte comes from the owner's audited disk image. Runtime-written operands stay
live; opcode guards and interpreter fallback remain in force. Optional private
captures retain additional copied/generated code that is absent from disk.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

IMAGE_SHA256='3200fa7325ff110e03518689245116707952aa8222bc283f94c4e774f4fcadbc'


def disk_files(raw):
    assert raw[:4]==b'FDS\x1a' and raw[4]==1,'Requires the approved one-side FDS image'
    at=16
    assert raw[at]==1
    at+=56
    assert raw[at]==2
    count=raw[at+1]
    at+=2
    files=[]
    for _ in range(count):
        assert raw[at]==3,'Invalid FDS file header'
        name=raw[at+3:at+11].decode('ascii')
        address,size=struct.unpack_from('<HH',raw,at+11)
        kind=raw[at+15]
        at+=16
        assert raw[at]==4 and at+1+size<=len(raw),'Invalid FDS file payload'
        at+=1
        files.append(dict(name=name,address=address,data=raw[at:at+size],kind=kind))
        at+=size
    return files


def disk_profiles(files):
    programs=[f for f in files if f['kind']==0 and 0x6000<=f['address'] and f['address']+len(f['data'])<=0xe000]
    main=next(f for f in programs if f['name']=='SM2MAIN ')
    baseline=bytearray(0x10000)
    baseline[main['address']:main['address']+len(main['data'])]=main['data']
    profiles=[]
    for program in programs:
        if program['name']!='SM2MAIN ' and not program['name'].startswith('SM2DATA'):continue
        memory=bytearray(baseline)
        memory[program['address']:program['address']+len(program['data'])]=program['data']
        profiles.append((program['name'],memory))
        if program['name']=='SM2DATA3':
            # World 8 loads DATA2 before the ending loads DATA3. DATA3 starts
            # at $C5D0, leaving DATA2's $C470-$C5CF code resident. Starting
            # this view from MAIN alone gives the wrong bytes for that code
            # and can invalidate an otherwise useful whole control-flow view.
            prior=next(f for f in programs if f['name']=='SM2DATA2')
            inherited=bytearray(baseline)
            inherited[prior['address']:prior['address']+len(prior['data'])]=prior['data']
            inherited[program['address']:program['address']+len(program['data'])]=program['data']
            profiles.append(('SM2MAIN+SM2DATA2+SM2DATA3',inherited))
    return programs,profiles


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--rom',required=True,type=Path)
    ap.add_argument('--out',required=True,type=Path)
    ap.add_argument('--sites',type=Path,default=Path(__file__).with_name('coverage_sites.json'))
    ap.add_argument('--base-captures',type=Path)
    args=ap.parse_args()
    raw=args.rom.read_bytes()
    if hashlib.sha256(raw).hexdigest()!=IMAGE_SHA256:raise ValueError('Unqualified FDS image')
    programs,profiles=disk_profiles(disk_files(raw))
    sites=json.loads(args.sites.read_text())
    assert sites['version'] in (1,2,3,4)
    if sites['version']>=2:assert sites['rom_sha256']==IMAGE_SHA256
    lines=['# Original disk bytes plus observed PC/rewrite metadata; generated locally.']
    if args.base_captures:
        lines.append(args.base_captures.read_text().rstrip())
    snapshots=0
    for window in sites['windows']:
        base=int(window['base'],16)
        pcs=[int(v,16) for v in window['pcs']]
        assert 0x6000<=base<0xe000 and base%1024==0 and all(base<=pc<base+1024 for pc in pcs)
        images=set()
        for name,memory in profiles:
            image=bytes(memory[base:base+1026])
            groups=(window['profile_groups'].get(name,[]) if sites['version']>=4 else
                    [window['profile_pcs'].get(name,[])] if sites['version']>=3 else [window['pcs']])
            for group in groups:
                qualified=tuple(sorted(int(v,16) for v in group))
                assert all(base<=pc<base+1024 for pc in qualified)
                if qualified:images.add((image,qualified))
        # Keep independently observed instruction groups separate. Unioning
        # their PCs joins unrelated control flow into a larger validity guard;
        # loading an overlay can then disable otherwise matching routines.
        # The compiler deduplicates identical resulting guarded views.
        for image,qualified in sorted(images):
            lines += [f'code {base:04X} 00000000 0 '+','.join(f'{pc:04X}' for pc in sorted(qualified)),
                      'data '+image.hex().upper()]
            snapshots+=1
    for item in sites['rewritten_addresses']:
        address=int(item,16)
        assert 0x6000<=address<0xe000
        held={f['data'][address-f['address']] for f in programs
              if f['address']<=address<f['address']+len(f['data'])}
        # This zero-count record is a conservative compiler hint, not a claim
        # that a fabricated value was observed. A non-disk marker makes the
        # compiler treat written operands as live and written opcodes as
        # isolated guarded instructions. It never supplies executed code.
        marker=next(value for value in range(256) if value not in held)
        lines.append(f'volatile {address:04X} {marker:02X} 00000000 0')
    args.out.parent.mkdir(parents=True,exist_ok=True)
    args.out.write_text('\n'.join(lines)+'\n')
    provenance=dict(rom_sha256=IMAGE_SHA256,sites_sha256=hashlib.sha256(args.sites.read_bytes()).hexdigest(),
        profiles=[name for name,_ in profiles],snapshots=snapshots,
        executed_sites=sum(len(w['pcs']) for w in sites['windows']),
        conservative_rewrite_sites=len(sites['rewritten_addresses']),
        base_capture_sha256=hashlib.sha256(args.base_captures.read_bytes()).hexdigest() if args.base_captures else None)
    args.out.with_suffix('.provenance.json').write_text(json.dumps(provenance,indent=2)+'\n')
    print(json.dumps(provenance))


if __name__=='__main__':main()
