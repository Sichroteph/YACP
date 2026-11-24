"""Download a synthetic EPUB from loopback OPDS, checking each filename choice."""
import argparse
from concurrent.futures import ThreadPoolExecutor
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import subprocess
from threading import Thread
from verify_yacp_simulator_stats import create_fixture


def scenario(program, output, mode):
    output.mkdir(parents=True, exist_ok=False)
    sd = create_fixture(output)
    book = (sd / 'completion.epub').read_bytes()
    requests = []

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def do_GET(self):
            requests.append(self.path)
            if self.path == '/feed':
                body = (f'<feed xmlns="http://www.w3.org/2005/Atom"><id>urn:qa</id><title>QA catalogue</title>'
                        f'<entry><id>urn:qa:book</id><title>Sample Book</title><author><name>QA Author</name></author>'
                        f'<link href="http://127.0.0.1:{server.server_port}/book.epub" rel="http://opds-spec.org/acquisition" '
                        'type="application/epub+zip"/></entry></feed>').encode()
                content_type = 'application/atom+xml'
            elif self.path == '/book.epub':
                body, content_type = book, 'application/epub+zip'
            else:
                self.send_error(404)
                return
            self.send_response(200)
            self.send_header('Content-Type', content_type)
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    worker = Thread(target=server.serve_forever, daemon=True)
    worker.start()
    try:
        config = sd / '.crosspoint/settings.json'
        values = json.loads(config.read_text())
        values.update(opdsFilenameFormat=2, opdsDownloadFolder='/Downloads')
        config.write_text(json.dumps(values))
        (sd / '.crosspoint/opds.json').write_text(json.dumps({'servers': [{
            'name': 'QA catalogue', 'url': f'http://127.0.0.1:{server.server_port}/feed',
            'username': '', 'password_obf': '', 'filenameFormat': mode}]}))
        (sd / '.crosspoint/wifi.json').write_text(json.dumps({'lastConnectedSsid': 'LocalQA', 'credentials': [
            {'ssid': 'LocalQA', 'password_obf': '', 'password_len': 0, 'password_crc32': 0}]}))
        env = dict(os.environ, SDL_VIDEODRIVER='offscreen', CROSSPOINT_SIM_SD=str(sd),
                   CROSSPOINT_SIM_WIFI_NETWORKS='LocalQA:-40:open',
                   CROSSPOINT_SIM_INPUT_SCRIPT='1800:TAP:240,756;2700:UP:100;3600:CONFIRM:100;11500:CONFIRM:100;20500:QUIT')
        with (output / 'run.log').open('x') as log:
            process = subprocess.run([str(program)], cwd=output, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=35)
        assert process.returncode == 0
        expected = {0: 'QA Author - Sample Book.epub', 1: 'Sample Book - QA Author.epub',
                    2: 'Sample Book.epub', 255: 'Sample Book.epub'}[mode]
        files = list((sd / 'Downloads').glob('*'))
        assert [p.name for p in files] == [expected], (mode, [p.name for p in files], requests)
        assert files[0].read_bytes() == book
        assert requests.count('/book.epub') == 1
        return {'filename_format': mode, 'filename': expected, 'download_bytes_identical': True}
    finally:
        server.shutdown()
        server.server_close()
        worker.join(timeout=2)


def run(program, output):
    program = program.resolve(strict=True)
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    with ThreadPoolExecutor(max_workers=2) as pool:
        cases = list(pool.map(lambda mode: scenario(program, output / str(mode), mode), (0, 1, 2, 255)))
    result = {'passed': True, 'cases': cases, 'external_opds_contacted': False, 'hardware_validated': False}
    (output / 'result.json').write_text(json.dumps(result, indent=2))
    print(json.dumps(result))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('program', type=Path)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    run(a.program, a.output)
