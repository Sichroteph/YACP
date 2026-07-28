"""Local preview server for the web portal.

Live-renders the pages from web/ (templates + fragments) and serves them with
the shared assets and mock API responses, so you can iterate on the design in a
normal browser without flashing the device.

    python3 scripts/preview_web.py          # http://localhost:8000
    python3 scripts/preview_web.py 9000      # custom port

Edits to web/ show up on refresh (pages are re-rendered per request). The mock
API returns representative data so Settings/Files/Fonts render populated."""
import os
import re
import sys
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WEB = os.path.join(ROOT, "web")
JSZIP = os.path.join(ROOT, "src", "network", "html", "js", "jszip.min.js")

# slug -> (route, title, active, extra <head> markup)
PAGES = {
    "home":     ("/",         "CrossInk",                   "home",     ""),
    "files":    ("/files",    "Files - CrossInk",           "files",    '  <script src="/js/jszip.min.js"></script>'),
    "settings": ("/settings", "Settings - CrossInk Reader", "settings", ""),
    "fonts":    ("/fonts",    "Fonts - CrossInk",           "fonts",    ""),
}
ROUTE_TO_SLUG = {route: slug for slug, (route, *_rest) in PAGES.items()}

def read(*parts):
    with open(os.path.join(*parts), encoding="utf-8") as f:
        return f.read()

def render_page(slug):
    _route, title, active, head_extra = PAGES[slug]
    js = read(WEB, "pages", f"{slug}.js").strip()
    values = {
        "title": title, "v": "dev", "head_extra": head_extra,
        "styles": read(WEB, "pages", f"{slug}.css"),
        "body": read(WEB, "pages", f"{slug}.html"),
        "script": f"<script>\n{js}\n</script>" if js else "",
        "cls_home": "", "cls_files": "", "cls_settings": "", "cls_fonts": "",
    }
    values[f"cls_{active}"] = ' class="active"'
    base = read(WEB, "templates", "base.html")
    return re.sub(r"{{\s*(\w+)\s*}}", lambda m: values[m.group(1)], base)

# Representative mock data so the pages render populated.
MOCK_API = {
    "/api/status": {"version": "dev-preview", "ip": "192.168.4.1", "freeHeap": 142000},
    "/api/files": [
        {"name": "Books", "isDirectory": True, "isEpub": False, "size": 0},
        {"name": "Read", "isDirectory": True, "isEpub": False, "size": 0},
        {"name": "The Great Gatsby.epub", "isDirectory": False, "isEpub": True, "size": 384512},
        {"name": "Moby Dick.epub", "isDirectory": False, "isEpub": True, "size": 612000},
        {"name": "notes.txt", "isDirectory": False, "isEpub": False, "size": 2048},
    ],
    "/api/fonts": {"families": [
        {"name": "Bookerly", "sizes": [10, 12, 14], "files": [{"size": 120000}, {"size": 140000}]},
        {"name": "Literata", "sizes": [12], "files": [{"size": 160000}]},
    ]},
    "/api/wifi": [
        {"ssid": "HomeNetwork", "hasPassword": True, "isLastConnected": True},
        {"ssid": "Library Guest", "hasPassword": False, "isLastConnected": False},
    ],
    "/api/opds": [
        {"name": "Project Gutenberg", "url": "https://m.gutenberg.org/ebooks.opds/",
         "username": "", "hasPassword": False, "filenameFormat": "author_title"},
    ],
    "/api/settings": [
        {"key": "darkMode", "name": "Dark Mode", "category": "Display", "type": "toggle", "value": 1},
        {"key": "fontSize", "name": "Font Size", "category": "Display", "type": "enum",
         "value": 1, "options": ["Small", "Medium", "Large"]},
        {"key": "margin", "name": "Page Margin", "category": "Reading", "type": "value",
         "value": 20, "min": 0, "max": 50, "step": 5},
        {"key": "sleepScreen", "name": "Sleep Screen", "category": "Reading", "type": "enum",
         "value": 0, "options": ["Cover", "Minimal", "Blank"]},
        {"key": "quickResumeSleepScreen", "name": "Quick Resume on Timeout",
         "category": "Reading", "type": "toggle", "value": 0},
        {"key": "deviceName", "name": "Device Name", "category": "Network", "type": "string",
         "value": "CrossInk-01"},
    ],
}

ASSETS = {
    "/style.css": (os.path.join(WEB, "assets", "style.css"), "text/css"),
    "/logo.png": (os.path.join(WEB, "assets", "logo.png"), "image/png"),
    "/js/jszip.min.js": (JSZIP, "application/javascript"),
}

class Handler(BaseHTTPRequestHandler):
    def _send(self, code, body, ctype):
        if isinstance(body, str):
            body = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = urlparse(self.path).path
        if path in ROUTE_TO_SLUG:
            try:
                self._send(200, render_page(ROUTE_TO_SLUG[path]), "text/html; charset=utf-8")
            except Exception as e:  # surface template errors in the browser
                self._send(500, f"render error: {e}", "text/plain")
            return
        if path in ASSETS:
            fpath, ctype = ASSETS[path]
            with open(fpath, "rb") as f:
                self._send(200, f.read(), ctype)
            return
        if path in MOCK_API:
            self._send(200, json.dumps(MOCK_API[path]), "application/json")
            return
        self._send(404, "not found", "text/plain")

    def do_POST(self):
        # Accept saves/uploads/deletes during preview so the JS does not error.
        self._send(200, json.dumps({"ok": True}), "application/json")

    def log_message(self, fmt, *args):
        pass  # keep the console quiet

if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8000
    httpd = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    print(f"CrossInk web preview: http://localhost:{port}  (Ctrl+C to stop)")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nstopped")
