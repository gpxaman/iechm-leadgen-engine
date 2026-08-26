#!/usr/bin/env python3
"""
Thin REST + static-file server. Stdlib only (http.server) -- no
flask/fastapi dependency, since this environment has no package installer
available, and because it keeps the boundary honest: this file contains
NO business logic, only (a) routing JSON requests to core.library
functions and (b) serving whatever is in ui/. The dashboard in ui/ talks
to nothing but the /api/* routes below, so it can be deleted and replaced
with a different frontend (a React app, a native client, curl) without
touching core/ at all -- and this server can be swapped for a real
FastAPI/Flask app later by re-implementing the same routes.

Run:
    python3 server.py [port]
Then open http://localhost:8000/
"""
from __future__ import annotations

import json
import sys
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from core import library

UI_DIR = Path(__file__).resolve().parent / "ui"

_STATIC_CONTENT_TYPES = {
    ".html": "text/html; charset=utf-8",
    ".css": "text/css; charset=utf-8",
    ".js": "application/javascript; charset=utf-8",
    ".json": "application/json; charset=utf-8",
}


class Handler(BaseHTTPRequestHandler):
    server_version = "IECHM-LeadGen/1.0"

    def log_message(self, fmt: str, *args) -> None:  # quieter default logging
        sys.stderr.write(f"{self.address_string()} - {fmt % args}\n")

    # -- helpers --------------------------------------------------------

    def _send_json(self, payload, status: int = 200) -> None:
        body = json.dumps(payload, default=str).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)

    def _send_static(self, rel_path: str) -> None:
        if rel_path in ("", "/"):
            rel_path = "index.html"
        rel_path = rel_path.lstrip("/")
        path = (UI_DIR / rel_path).resolve()
        if UI_DIR not in path.parents and path != UI_DIR:
            self.send_error(403)
            return
        if not path.is_file():
            self.send_error(404, f"not found: {rel_path}")
            return
        content_type = _STATIC_CONTENT_TYPES.get(path.suffix, "application/octet-stream")
        body = path.read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _read_json_body(self) -> dict:
        length = int(self.headers.get("Content-Length") or 0)
        if not length:
            return {}
        raw = self.rfile.read(length)
        try:
            return json.loads(raw or b"{}")
        except json.JSONDecodeError:
            return {}

    # -- routing ----------------------------------------------------------

    def do_OPTIONS(self) -> None:  # CORS preflight, harmless to allow
        self.send_response(204)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()

    def do_GET(self) -> None:
        parsed = urllib.parse.urlparse(self.path)
        qs = urllib.parse.parse_qs(parsed.query)

        try:
            if parsed.path == "/api/channels":
                return self._send_json(library.list_channel_types())

            if parsed.path == "/api/leads":
                limit = int(qs.get("limit", ["100"])[0])
                status = qs.get("status", [None])[0]
                channel_type = qs.get("channel_type", [None])[0]
                return self._send_json(library.list_leads(limit=limit, status=status, channel_type=channel_type))

            if parsed.path.startswith("/api/leads/"):
                lead_id = parsed.path.rsplit("/", 1)[-1]
                detail = library.get_lead(lead_id)
                if detail is None:
                    return self._send_json({"error": "not found"}, status=404)
                return self._send_json(detail)

            if parsed.path == "/api/agents":
                return self._send_json(library.list_agents())

            if parsed.path == "/api/sentinel-events":
                limit = int(qs.get("limit", ["50"])[0])
                return self._send_json(library.list_sentinel_events(limit=limit))

            if parsed.path == "/api/strategies":
                return self._send_json(library.list_strategies())

            if parsed.path == "/api/funnel":
                return self._send_json(library.funnel_metrics())

            if parsed.path == "/api/status":
                return self._send_json({"current_day": library.current_day()})

            # anything else is a static asset request
            return self._send_static(parsed.path)
        except Exception as exc:  # keep the server alive, surface the error to the caller
            return self._send_json({"error": str(exc)}, status=500)

    def do_POST(self) -> None:
        parsed = urllib.parse.urlparse(self.path)
        try:
            if parsed.path == "/api/run-cycle":
                body = self._read_json_body()
                return self._send_json(library.run_cycle(day=body.get("day")))

            if parsed.path == "/api/reset":
                library.reset_all()
                return self._send_json({"ok": True})

            return self._send_json({"error": "not found"}, status=404)
        except Exception as exc:
            return self._send_json({"error": str(exc)}, status=500)


def main() -> None:
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8000
    library.init()
    httpd = ThreadingHTTPServer(("0.0.0.0", port), Handler)
    print(f"IECHM lead-gen dashboard: http://localhost:{port}/")
    print("API base: /api/  (channels, leads, agents, sentinel-events, strategies, funnel, status, run-cycle, reset)")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        httpd.server_close()


if __name__ == "__main__":
    main()
