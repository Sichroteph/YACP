"""Start a synthetic simulator SD and check sleep-gallery HTTP contracts before browser QA."""
import argparse
import gzip
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


def run(program, output, hold_seconds=120, board='pro'):
    program = program.resolve(strict=True)
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    sd = create_fixture(output)
    (sd / 'Books').mkdir()
    (sd / 'Books/Other.txt').write_text('Synthetic book for sleep-gallery tests.')
    (sd / 'Directory.epub').mkdir()
    (sd / '.crosspoint/wifi.json').write_text(json.dumps({'lastConnectedSsid': 'LocalQA', 'credentials': [
        {'ssid': 'LocalQA', 'password_obf': '', 'password_len': 0, 'password_crc32': 0}]}))
    originals = {str(p.relative_to(sd)): hashlib.sha256(p.read_bytes()).hexdigest()
                 for p in sd.rglob('*') if p.is_file() and '.crosspoint' not in p.parts}
    with socket.socket() as reservation:
        reservation.bind(('127.0.0.1', 0))
        port = reservation.getsockname()[1]
    base = f'http://127.0.0.1:{port}'
    menu = '1800:BACK:100' if board == 'x3' else '1800:TAP:240,756'
    env = dict(os.environ, SDL_VIDEODRIVER='offscreen', CROSSPOINT_SIM_SD=str(sd),
               CROSSPOINT_SIM_HTTP_PORT=str(port), CROSSPOINT_SIM_WIFI_NETWORKS='LocalQA:-40:open',
               CROSSPOINT_SIM_INPUT_SCRIPT=f'{menu};2600:DOWN:100;3000:DOWN:100;3400:DOWN:100;4200:CONFIRM:100;6000:CONFIRM:100;{(hold_seconds+20)*1000}:QUIT')
    with (output / 'run.log').open('x') as log:
        process = subprocess.Popen([str(program)], cwd=output, env=env, stdout=log, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 20
            while True:
                try:
                    with urlopen(base + '/api/status', timeout=1) as response:
                        status = json.load(response)
                    break
                except (URLError, TimeoutError):
                    if process.poll() is not None or time.monotonic() >= deadline:
                        raise RuntimeError('Simulator HTTP server did not start')
                    time.sleep(.25)
            dimensions = (528,792) if board == 'x3' else (480,800)
            assert (status['imageWidth'], status['imageHeight']) == dimensions, status
            assert 'currentBookPath' in status
            for path in ('/sleep-images', '/js/sleep-image-prep.js'):
                with urlopen(base + path) as response:
                    data = response.read()
                    assert response.headers['Content-Encoding'] == 'gzip'
                    assert len(gzip.decompress(data)) > 1000
                    etag = response.headers['ETag']
                try:
                    urlopen(Request(base + path, headers={'If-None-Match': etag}))
                except HTTPError as error:
                    assert error.code == 304
                else:
                    raise AssertionError('Static page did not return 304')

            def post(path, value, expected):
                try:
                    response = urlopen(Request(base + path, data=urlencode(value).encode(), method='POST'))
                except HTTPError as error:
                    response = error
                with response:
                    body = response.read()
                    assert response.status == expected, (value, response.status, body)
                    return json.loads(body) if expected == 200 else None

            cases = [('',400), ('completion.epub',400), ('/../completion.epub',400), ('//completion.epub',400),
                     ('/.crosspoint/file.epub',400), ('/completion.epub/',400), ('/a\\b.epub',400),
                     ('/a\x00b.epub',400), ('/a?b.epub',400), ('/image.bmp',400), ('/absent.epub',404),
                     ('/Directory.epub',400), ('/' + 'a'*384 + '.epub',400)]
            for value, expected in cases:
                post('/api/book-gallery/prepare', {'bookPath':value}, expected)
            assert not (sd / '.book-galleries').exists()
            assert post('/api/sleep-images/prepare', {}, 200) == {'path':'/.sleep'}
            target = post('/api/book-gallery/prepare', {'bookPath':'/completion.epub'}, 200)['path']
            assert target.startswith('/.book-galleries/') and (sd / target[1:]).is_dir()
            assert post('/api/book-gallery/prepare', {'bookPath':'/completion.epub'}, 200)['path'] == target
            for path, digest in originals.items():
                assert hashlib.sha256((sd / path).read_bytes()).hexdigest() == digest
            report = {'passed':True, 'invalid_paths':len(cases), 'etag_checks':2,
                      'gallery_repeatable':True, 'book_files_preserved':True, 'hardware_validated':False}
            (output / 'api-result.json').write_text(json.dumps(report, indent=2))
            (output / 'ready.json').write_text(json.dumps({'url':base,'sd':str(sd),'gallery':target,'dimensions':dimensions}))
            print(json.dumps({'ready':base, **report}), flush=True)
            process.wait(timeout=hold_seconds+25)
            assert process.returncode == 0
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=5)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('program', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--hold-seconds', type=int, default=120)
    parser.add_argument('--board', choices=('x3','pro'), default='pro')
    args = parser.parse_args()
    run(args.program, args.output, args.hold_seconds, args.board)
