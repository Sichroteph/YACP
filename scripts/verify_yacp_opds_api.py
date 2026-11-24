"""Check OPDS per-server preference persistence using synthetic loopback HTTP only."""
import argparse
import json
import os
from pathlib import Path
import socket
import subprocess
import time
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen
from verify_yacp_simulator_stats import create_fixture


def run(program, output):
    program = program.resolve(strict=True)
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    sd = create_fixture(output)
    store = sd / '.crosspoint/opds.json'
    store.write_text(json.dumps({'servers': [
        {'name': 'QA one', 'url': 'https://example.invalid/one', 'username': 'qa', 'password_obf': '', 'filenameFormat': 0},
        {'name': 'QA two', 'url': 'https://example.invalid/two', 'username': 'qa', 'password_obf': '', 'filenameFormat': 1},
        {'name': 'QA inherit', 'url': 'https://example.invalid/three', 'username': '', 'password_obf': ''}]}))
    (sd / '.crosspoint/wifi.json').write_text(json.dumps({'lastConnectedSsid': 'LocalQA', 'credentials': [
        {'ssid': 'LocalQA', 'password_obf': '', 'password_len': 0, 'password_crc32': 0}]}))
    calls = 0
    for phase in ('edit', 'reload'):
        with socket.socket() as reservation:
            reservation.bind(('127.0.0.1', 0))
            port = reservation.getsockname()[1]
        base = f'http://127.0.0.1:{port}'
        env = dict(os.environ, SDL_VIDEODRIVER='offscreen', CROSSPOINT_SIM_SD=str(sd),
                   CROSSPOINT_SIM_HTTP_PORT=str(port), CROSSPOINT_SIM_WIFI_NETWORKS='LocalQA:-40:open',
                   CROSSPOINT_SIM_INPUT_SCRIPT='1800:TAP:240,756;2600:DOWN:100;3000:DOWN:100;3400:DOWN:100;4200:CONFIRM:100;6000:CONFIRM:100;22000:QUIT')

        def get():
            with urlopen(base + '/api/opds', timeout=2) as response:
                return json.load(response)

        def post(value, status=200):
            nonlocal calls
            request = Request(base + '/api/opds', data=json.dumps(value).encode(), method='POST', headers={'Content-Type': 'application/json'})
            try:
                result = urlopen(request, timeout=3)
            except HTTPError as error:
                result = error
            with result:
                assert result.status == status, (result.status, result.read())
            calls += 1

        with (output / (phase + '.log')).open('x') as log:
            process = subprocess.Popen([str(program)], cwd=output, env=env, stdout=log, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic() + 18
                while True:
                    try:
                        rows = get()
                        break
                    except (URLError, TimeoutError):
                        if process.poll() is not None or time.monotonic() >= deadline:
                            raise RuntimeError('Simulator server did not start')
                        time.sleep(.25)
                assert [r['filenameFormat'] for r in rows] == ([0, 1, 255] if phase == 'edit' else [255, 1, 255, 2])
                assert all('password' not in r and 'password_obf' not in r for r in rows)
                if phase == 'edit':
                    body = {'index': 0, 'name': 'Renamed QA', 'url': 'https://example.invalid/one', 'username': 'qa'}
                    before = store.read_bytes()
                    for bad in (None, True, -1, 3, 256, '1', [], 1.5):
                        post(dict(body, filenameFormat=bad), 400)
                        assert store.read_bytes() == before
                    post(body)
                    assert get()[0]['filenameFormat'] == 0
                    post(dict(body, filenameFormat=255))
                    post({'name': 'QA new', 'url': 'https://example.invalid/new', 'filenameFormat': 2})
                    assert [r['filenameFormat'] for r in get()] == [255, 1, 255, 2]
                assert process.wait(timeout=30) == 0
            finally:
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=5)
    result = {'passed': True, 'http_cases': calls, 'invalid_requests_unchanged': True,
              'per_server_choices_preserved': True, 'hardware_validated': False, 'external_opds_contacted': False}
    (output / 'result.json').write_text(json.dumps(result, indent=2))
    print(json.dumps(result))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('program', type=Path)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    run(a.program, a.output)
