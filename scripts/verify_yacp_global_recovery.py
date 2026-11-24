"""Check exact global-statistics restore and automatic backup recovery using read-only source archives."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess


def run(output, archive=None):
    root = Path(__file__).resolve().parents[1]
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    reader = root / 'src/activities/reader'
    executable = output / 'global-recovery'
    command = ['g++', '-std=c++20', '-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
               '-I'+str(root/'test/yacp_statistics/stubs'), '-I'+str(reader),
               str(root/'test/yacp_statistics/GlobalRecoveryTest.cpp'), str(reader/'GlobalReadingStats.cpp'),
               str(reader/'ReadingStatsUtils.cpp'), str(reader/'DailyReadingHistory.cpp'), '-o', str(executable)]
    subprocess.run(command, check=True)
    files = []
    for version, size in ((1,13),(2,17),(3,159)):
        file = output / f'synthetic-v{version}.bin'
        data = bytearray(size)
        data[0] = version
        struct.pack_into('<3I',data,1,123,9876543,7654321)
        if version >= 2:
            struct.pack_into('<I',data,13,29)
        if version == 3:
            data[17:] = bytes((i*7+13)%256 for i in range(size-17))
        file.write_bytes(data)
        files.append(file)
    archives = sorted(archive.glob('*.bin')) if archive else []
    if archive and not archives:
        raise ValueError('No supplied statistics archives')
    files += archives
    digests = {p:hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
    modes = ('direct','missing','corrupt','truncated','future-primary','future-backup')
    for file in files:
        for mode in modes:
            result = subprocess.run([str(executable),str(file),mode],check=True,capture_output=True,text=True)
            assert result.stdout.strip() == 'PASS'
    assert all(hashlib.sha256(p.read_bytes()).hexdigest() == digest for p,digest in digests.items())
    report = {'passed':True,'synthetic_versions':3,'real_archives':len(archives),'cases':len(files)*len(modes),
              'all_fields_round_trip':True,'sources_unchanged':True,'recovery_has_no_load_writes':True,
              'future_versions_preserved':True,'storage':'in-memory host substitute','hardware_validated':False}
    (output/'result.json').write_text(json.dumps(report,indent=2))
    print(json.dumps(report))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--archive',type=Path)
    args = parser.parse_args()
    run(args.output,args.archive)
