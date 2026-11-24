"""Validate automatic and Web moves on synthetic simulator cards only."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import socket
import subprocess
import time
from urllib.error import HTTPError, URLError
from urllib.parse import urlencode
from urllib.request import Request, urlopen

from verify_yacp_simulator_stats import create_fixture
from yacp_book_profile import DEFAULTS, encode
from yacp_data_formats import book_stats, finished_books


def run(program, output, board):
    program = program.resolve(strict=True)
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    root = Path(__file__).resolve().parents[1]
    hasher = output / 'identity'
    subprocess.run(['g++', '-std=c++17', str(root/'test/yacp_statistics/CacheIdentity.cpp'), '-o', str(hasher)], check=True)

    def paths(sd, book):
        cache = subprocess.check_output([str(hasher), book], text=True).strip()
        value = 14695981039346656037
        for c in book.encode():
            value = ((value ^ c) * 1099511628211) & ((1 << 64)-1)
        stem = Path(book).stem
        label = ''.join(c if c.isascii() and (c.isalnum() or c in '-_') else '-' for c in stem)[:48].strip('-')
        return (sd/'.crosspoint'/cache, sd/'.crosspoint/book_profiles'/cache,
                sd/'.book-galleries'/f'{value:016x}-{label}',
                sd/'.crosspoint/bookmarks'/(book[1:].replace('/', '_').rsplit('.',1)[0]+'.json'))

    reports = []
    for mode in (('automatic', 'web', 'recovery') if board == 'x3' else ('web', 'recovery')):
        folder = output/mode
        folder.mkdir()
        sd = create_fixture(folder)
        original = '/completion.epub'
        cache, profile, gallery, bookmarks = paths(sd, original)
        for directory in (cache, profile, gallery, bookmarks.parent, sd/'read'):
            directory.mkdir(parents=True, exist_ok=True)
        (cache/'unknown-user.bin').write_bytes(b'preserve unknown cache state')
        (profile/'reader_profile.0').write_bytes(encode(DEFAULTS, flags=3))
        (gallery/'retained.bmp').write_bytes(b'gallery content is not decoded by this test')
        bookmarks.write_text('[]')
        immutable = [(cache/'unknown-user.bin').read_bytes(), (profile/'reader_profile.0').read_bytes(),
                     (gallery/'retained.bmp').read_bytes(), bookmarks.read_bytes(), (sd/'completion.epub').read_bytes()]
        setting_path = sd/'.crosspoint/settings.json'
        settings = json.loads(setting_path.read_text())
        settings.update(trackReadingStats=0, clockEnabled=0, longPressMenuAction=8,
                        moveFinishedToReadFolder=int(mode == 'automatic'))
        setting_path.write_text(json.dumps(settings))
        (sd/'.crosspoint/wifi.json').write_text(json.dumps({'lastConnectedSsid':'LocalQA', 'credentials':[
            {'ssid':'LocalQA','password_obf':'','password_len':0,'password_crc32':0}]}))
        with socket.socket() as reserve:
            reserve.bind(('127.0.0.1',0)); port = reserve.getsockname()[1]
        base = f'http://127.0.0.1:{port}'
        if mode == 'automatic':
            script = '1800:CONFIRM:150;6500:CONFIRM:1800;11200:BACK:150;13000:BACK:150;15500:CONFIRM:150;22000:QUIT'
        elif mode == 'recovery':
            # Model power loss after one companion rename and a torn JSON write.
            # All paths are inside this newly created synthetic card.
            def fnv(data):
                value = 2166136261
                for byte in data: value = ((value ^ byte) * 16777619) & 0xffffffff
                return value
            record = bytearray(1052)
            record[:8] = b'YBM1' + bytes([1,31,3,0])
            for i, name in enumerate(('recent','state')):
                data = (sd/'.crosspoint'/f'{name}.json').read_bytes()
                (sd/'.crosspoint'/f'book-move-{name}.json').write_bytes(data)
                record[8+i*8:12+i*8] = len(data).to_bytes(4,'little')
                record[12+i*8:16+i*8] = fnv(data).to_bytes(4,'little')
            for i, path in enumerate((original, '/read/completion.epub')):
                value = path.encode()
                record[24+i*512:24+i*512+len(value)] = value
            record[-4:] = fnv(record[:-4]).to_bytes(4,'little')
            (sd/'.crosspoint/book-move.bin').write_bytes(record)
            target_cache = paths(sd,'/read/completion.epub')[0]
            assert cache.resolve().is_relative_to(sd.resolve()) and target_cache.resolve().is_relative_to(sd.resolve())
            cache.rename(target_cache)
            (sd/'.crosspoint/state.json').write_bytes(b'{')
            script = ('1800:CONFIRM:150;6500:BACK:150;10000:QUIT' if board == 'x3' else
                      '1800:TAP:240,350;6500:HOME:100;10000:QUIT')

        else:
            menu = '1800:BACK:100' if board == 'x3' else '1800:TAP:240,756'
            script = f'{menu};2600:DOWN:100;3000:DOWN:100;3400:DOWN:100;4200:CONFIRM:100;6000:CONFIRM:100;28000:QUIT'
        env = dict(os.environ, SDL_VIDEODRIVER='offscreen', SDL_RENDER_DRIVER='software',
                   CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_INPUT_SCRIPT=script,
                   CROSSPOINT_SIM_HTTP_PORT=str(port), CROSSPOINT_SIM_WIFI_NETWORKS='LocalQA:-40:open',
                   CROSSPOINT_SIM_SCREENSHOTS=f'19000:{folder}/result.bmp')
        with (folder/'run.log').open('x') as log:
            process = subprocess.Popen([str(program)], cwd=folder, env=env, stdout=log, stderr=subprocess.STDOUT)
            try:
                final = '/read/completion.epub'
                if mode == 'web':
                    deadline = time.monotonic()+22
                    while True:
                        try:
                            with urlopen(base+'/api/status', timeout=1) as response: response.read()
                            break
                        except (URLError, TimeoutError):
                            if process.poll() is not None or time.monotonic() > deadline: raise
                            time.sleep(.25)
                    def post(route, args, expected=200):
                        try: response = urlopen(Request(base+route, data=urlencode(args).encode()), timeout=5)
                        except HTTPError as error: response = error
                        with response:
                            body = response.read()
                            assert response.status == expected, (response.status, body)
                    post('/move', {'path': original, 'dest':'/read'})
                    post('/rename', {'path':final, 'name':'renamed.epub'})
                    final = '/read/renamed.epub'
                    # Existing metadata without a destination book must also prevent overwrite.
                    collision_cache = paths(sd, '/read/collision.epub')[0]
                    collision_cache.mkdir()
                    (collision_cache/'stats.bin').write_bytes(b'other book statistics')
                    post('/rename', {'path':final, 'name':'collision.epub'}, 500)
                    assert (collision_cache/'stats.bin').read_bytes() == b'other book statistics'
                process.wait(timeout=35)
                assert process.returncode == 0
            finally:
                if process.poll() is None: process.terminate(); process.wait(timeout=5)
        cache, profile, gallery, bookmarks = paths(sd, final)
        actual = [(cache/'unknown-user.bin').read_bytes(), (profile/'reader_profile.0').read_bytes(),
                  (gallery/'retained.bmp').read_bytes(), bookmarks.read_bytes(), (sd/final[1:]).read_bytes()]
        assert actual == immutable
        assert not (sd/original[1:]).exists()
        assert not (sd/'.crosspoint/book-move.bin').exists()
        recent = json.loads((sd/'.crosspoint/recent.json').read_text())
        assert recent['books'][0]['path'] == final
        if mode == 'automatic':
            stats = book_stats((cache/'stats_v6.bin').read_bytes())
            assert stats['completed']
            assert len(finished_books((sd/'.crosspoint/finished_books.bin').read_bytes())['books']) == 1
            assert (cache/'progress.bin').exists()
        assert 'Book and associated data moved successfully' in (folder/'run.log').read_text()
        reports.append(dict(mode=mode, passed=True, unchanged_book_sha256=hashlib.sha256(immutable[-1]).hexdigest()))
    report = dict(board=board, cases=reports, hardware_validated=False)
    (output/'result.json').write_text(json.dumps(report,indent=2))
    print(json.dumps(report))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('program', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--board', choices=('x3','pro'), default='x3')
    args = parser.parse_args()
    run(args.program,args.output,args.board)
