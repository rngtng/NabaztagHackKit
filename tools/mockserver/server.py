#!/usr/bin/env python3
"""Mock HTTP server for end-to-end simulation (issue #39).

The MTL simulator talks real BSD sockets, so a running app (e.g. piper) makes
genuine outbound HTTP requests for the files its bytecode demands — `bc.jsp`
(the app bytecode), `init.forth`, and whatever that Forth script then pulls
(`*.forth`, `*.mp3`, `*.wav`, `locate.jsp`, `hooks/record.php`, ...). Point the
simulator at this server (`mtl_simu --serverurl <this-host>`) and it logs every
request and serves the matching file from the assets directory, or 404s.

Assets live next to an app's `*.mtl` sources (e.g. `src/app-piper/assets/`) and
are mounted read-only at `/assets`. The URL path maps straight onto that tree; a
leading `/vl` prefix (the historical Violet server path) is stripped so both
`/init.forth` and `/vl/init.forth` resolve to `assets/init.forth`.

Stdlib only — no third-party deps.
"""

import argparse
import mimetypes
import os
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import unquote, urlsplit

mimetypes.add_type("text/plain", ".forth")
mimetypes.add_type("audio/mpeg", ".mp3")
mimetypes.add_type("audio/wav", ".wav")
mimetypes.add_type("application/octet-stream", ".bin")

ASSETS_ROOT = "/assets"


def log(msg):
    # line-buffered so `docker logs -f` / `task` show requests live
    print(msg, flush=True)


def resolve_asset(url_path):
    """Map a request path to a real file under ASSETS_ROOT, or None.

    Strips a leading `/vl` (Violet server path prefix) and blocks traversal
    outside the assets root. Query string must already be removed.
    """
    path = unquote(url_path)
    if path.startswith("/vl/"):
        path = path[3:]
    elif path == "/vl":
        path = "/"
    rel = path.lstrip("/")
    candidate = os.path.normpath(os.path.join(ASSETS_ROOT, rel))
    root = os.path.normpath(ASSETS_ROOT)
    if candidate != root and not candidate.startswith(root + os.sep):
        return None  # path traversal attempt
    if os.path.isfile(candidate):
        return candidate
    return None


class Handler(BaseHTTPRequestHandler):
    server_version = "NabaztagMock/1.0"

    # silence the default per-request stderr logging; we log our own line
    def log_message(self, fmt, *args):
        pass

    def _serve(self, body_len=0):
        split = urlsplit(self.path)
        query = f"?{split.query}" if split.query else ""
        asset = resolve_asset(split.path)
        extra = f" body={body_len}B" if body_len else ""
        if asset is None:
            log(f"{self.command} {split.path}{query}{extra} -> 404")
            self.send_error(404, "Not found")
            return
        with open(asset, "rb") as f:
            data = f.read()
        ctype = mimetypes.guess_type(asset)[0] or "application/octet-stream"
        log(f"{self.command} {split.path}{query}{extra} -> 200 {asset} ({len(data)}B {ctype})")
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(data)

    def do_GET(self):
        self._serve()

    def do_HEAD(self):
        self._serve()

    def do_POST(self):
        # e.g. hooks/record.php — log the upload; serve a file if one matches, else 200 OK
        length = int(self.headers.get("Content-Length", 0) or 0)
        if length:
            self.rfile.read(length)
        split = urlsplit(self.path)
        asset = resolve_asset(split.path)
        if asset is not None:
            self._serve(body_len=length)
            return
        query = f"?{split.query}" if split.query else ""
        log(f"POST {split.path}{query} body={length}B -> 200 (ack)")
        self.send_response(200)
        self.send_header("Content-Length", "0")
        self.end_headers()


def main():
    ap = argparse.ArgumentParser(description="Nabaztag simulator mock file server")
    ap.add_argument("--port", type=int, default=int(os.environ.get("PORT", "80")))
    args = ap.parse_args()

    if not os.path.isdir(ASSETS_ROOT):
        log(f"[mockserver] WARNING: assets dir {ASSETS_ROOT} not mounted — everything 404s")
    else:
        n = sum(len(files) for _, _, files in os.walk(ASSETS_ROOT))
        log(f"[mockserver] serving {n} file(s) from {ASSETS_ROOT}")

    httpd = ThreadingHTTPServer(("0.0.0.0", args.port), Handler)
    log(f"[mockserver] listening on 0.0.0.0:{args.port}")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        httpd.server_close()


if __name__ == "__main__":
    sys.exit(main())
