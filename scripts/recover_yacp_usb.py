"""Inspect a prepared X3 USB recovery plan. Writing requires --install and typed confirmation."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
from datetime import datetime
import zlib
from verify_yacp_image import verify


def digest(data):
    return hashlib.sha256(data).hexdigest()


def active_partition(data):
    partitions = []
    ota = None
    for offset in range(0x8000, 0x9000, 32):
        magic, kind, subtype, address, size, label, flags = struct.unpack_from('<HBBII16sI', data, offset)
        if magic != 0x50AA:
            break
        if kind == 0 and subtype in (16, 17):
            partitions.append((subtype - 16, address, size))
        if kind == 1 and subtype == 0:
            ota = (address, size)
    if ota != (0xE000, 0x2000) or sorted(p[0] for p in partitions) != [0, 1]:
        raise ValueError('Unexpected partition table; no write allowed')
    valid = []
    for offset in (ota[0], ota[0] + 4096):
        seq = struct.unpack_from('<I', data, offset)[0]
        state, crc = struct.unpack_from('<II', data, offset + 24)
        if seq not in (0, 0xFFFFFFFF) and state in (0, 2, 0xFFFFFFFF) and crc == zlib.crc32(data[offset:offset + 4], 0xFFFFFFFF):
            valid.append(seq)
        elif state == 1:
            raise ValueError('Pending OTA verification; stop recovery')
    if not valid:
        raise ValueError('No valid boot selection')
    selected = (max(valid) - 1) % 2
    return next((address, size) for index, address, size in partitions if index == selected)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('plan', type=Path)
    parser.add_argument('--install', action='store_true')
    args = parser.parse_args()
    plan = json.loads(args.plan.read_text(encoding='utf-8'))
    backup = Path(plan['backup']).read_bytes()
    image = Path(plan['image']).read_bytes()
    if len(backup) != 0x1000000 or digest(backup) != plan['backup_sha256']:
        raise ValueError('Flash backup mismatch')
    identity = verify(image, 'tiny', plan['version'])
    if identity['sha256'] != plan['image_sha256']:
        raise ValueError('Firmware image mismatch')
    address, size = active_partition(backup)
    if address != plan['active_address'] or len(image) > size:
        raise ValueError('Unexpected active application partition')
    old_version = backup[address + 48:address + 80].split(b'\0')[0].decode('ascii')
    if old_version != plan['previous_version']:
        raise ValueError('Previous firmware identity mismatch')
    print(f"X3 Standard: {old_version} -> {identity['embedded_version']}")
    print(f"Application only at {address:#x}; bootloader, partition table and SD files are preserved.")
    if not args.install:
        print('Inspection only. Nothing was written to the device.')
        return
    from serial.tools import list_ports
    def identify_port():
        devices = [p for p in list_ports.comports() if p.vid == 0x303A and p.pid == 0x1001 and
                   (p.serial_number or '').lower().replace(':', '').replace('-', '') == plan['mac']]
        if len(devices) != 1:
            raise ValueError('Expected X3 USB identity not found uniquely')
        return devices[0].device
    port = identify_port()
    output = Path(plan['archive']) / ('manual-' + datetime.now().strftime('%Y%m%d-%H%M%S'))
    output.mkdir(exist_ok=False)
    env = dict(os.environ, PYTHONUTF8='1', PYTHONIOENCODING='utf-8')
    # Use the ROM path: the accelerated stub could not read one region reliably.
    def command(*operation):
        return [sys.executable, '-m', 'esptool', '--chip', 'esp32c3', '--port', port,
                '--baud', '115200', '--no-stub', '--after', 'hard-reset', *operation]
    current = output / 'boot-before.bin'
    subprocess.run(command('read-flash', '0x0', '0x10000', str(current)), check=True, env=env)
    if current.read_bytes() != backup[:0x10000]:
        raise ValueError('Boot configuration changed since backup; no write allowed')
    print('Sauvegarde et identite verifiees. Garder le cable USB branche.')
    if input('Pour installer ce firmware, taper INSTALLER puis Entree : ').strip() != 'INSTALLER':
        print('Installation annulee. Aucun firmware ecrit.')
        return
    if identify_port() != port:
        raise ValueError('USB device changed; no write allowed')
    subprocess.run(command('write-flash', hex(address), plan['image']), check=True, env=env)
    (output / 'installed.json').write_text(json.dumps(identity, indent=2), encoding='utf-8')
    print('Ecriture verifiee par esptool. Le X3 redemarre; attendre la migration.')


if __name__ == '__main__':
    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print('ARRET:', error, file=sys.stderr)
        raise SystemExit(1)
