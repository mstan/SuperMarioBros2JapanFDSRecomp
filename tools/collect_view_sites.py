"""Keep only executed PC and rewritten-address metadata from private captures."""
import argparse
import hashlib
import json
from pathlib import Path
from prepare_ram_views import IMAGE_SHA256,disk_files,disk_profiles

ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('--out',required=True,type=Path)
ap.add_argument('--rom',required=True,type=Path,help='Approved disk image; distinguish overlay loads from non-disk rewrites')
ap.add_argument('--merge-sites',type=Path,help='Retain existing qualified address metadata while adding new routes')
ap.add_argument('captures',nargs='+',type=Path)
args=ap.parse_args()
raw=args.rom.read_bytes()
assert hashlib.sha256(raw).hexdigest()==IMAGE_SHA256,'Unqualified FDS image'
programs=[f for f in disk_files(raw) if f['kind']==0]
_,profiles=disk_profiles(disk_files(raw))
held={}
for program in programs:
    for offset,value in enumerate(program['data']):
        held.setdefault(program['address']+offset,set()).add(value)
windows={}
qualified={}
profile_groups={}
entries=[]
writes=set()
evidence=[]
if args.merge_sites:
    previous=json.loads(args.merge_sites.read_text())
    assert previous['version']==4,'Regenerate older metadata from its original captures'
    for window in previous['windows']:
        windows[int(window['base'],16)]={int(pc,16) for pc in window['pcs']}
        qualified[int(window['base'],16)]={name:{int(pc,16) for pc in pcs}
                                          for name,pcs in window['profile_pcs'].items()}
        profile_groups[int(window['base'],16)]={name:{tuple(int(pc,16) for pc in group) for group in groups}
                                              for name,groups in window['profile_groups'].items()}
    writes.update(int(a,16) for a in previous['rewritten_addresses'])
    evidence.extend(previous['evidence_sha256'])
for capture in args.captures:
    source_lines=capture.read_text().splitlines()
    for number,line in enumerate(source_lines):
        fields=line.split()
        if fields and fields[0]=='code':
            base=int(fields[1],16)
            if not 0x6000<=base<0xe000:continue
            pcs={int(pc,16) for pc in fields[4].split(',')}
            assert all(base<=pc<base+1024 for pc in pcs)
            windows.setdefault(base,set()).update(pcs)
            image=bytes.fromhex(source_lines[number+1].removeprefix('data '))
            assert len(image)==1026
            entries.append((base,pcs,image))
        elif fields and fields[0]=='volatile':
            address=int(fields[1],16)
            # A store of an original file byte is an overlay load. The
            # compiler already guards each content-keyed disk view. Only a
            # value no original disk file holds proves a runtime rewrite.
            if 0x6000<=address<0xe000 and int(fields[2],16) not in held.get(address,set()):writes.add(address)
for base,pcs,image in entries:
    for name,memory in profiles:
        group=set()
        for pc in pcs:
            # Bind an observed PC to the disk version actually containing
            # its captured three-byte fingerprint. Adjacent bytes may be
            # part of the next instruction; mismatches simply retain the
            # interpreter fallback. Actual rewritten operands remain live.
            # Broadcasting PCs into every overlay can decode another file's
            # changing data as code and invalidate a useful whole view.
            if all(pc+i>=0xe000 or (i and pc+i in writes) or
                   image[pc-base+i]==memory[pc+i] for i in range(3)):
                qualified.setdefault(base,{}).setdefault(name,set()).add(pc)
                group.add(pc)
        if group:
            profile_groups.setdefault(base,{}).setdefault(name,set()).add(tuple(sorted(group)))
result=dict(version=4,rom_sha256=IMAGE_SHA256,
    evidence_sha256=list(dict.fromkeys(evidence+[hashlib.sha256(f.read_bytes()).hexdigest() for f in args.captures])),
    windows=[dict(base=f'{base:04X}',pcs=[f'{pc:04X}' for pc in sorted(pcs)],
                  profile_pcs={name:[f'{pc:04X}' for pc in sorted(group)]
                               for name,group in sorted(qualified.get(base,{}).items())},
                  profile_groups={name:[[f'{pc:04X}' for pc in group] for group in sorted(groups)]
                                  for name,groups in sorted(profile_groups.get(base,{}).items())})
             for base,pcs in sorted(windows.items())],
    rewritten_addresses=[f'{a:04X}' for a in sorted(writes)])
args.out.write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(dict(windows=len(windows),pcs=sum(map(len,windows.values())),rewritten=len(writes),
                      qualified=sum(len(set().union(*groups.values())) for groups in qualified.values()))))
