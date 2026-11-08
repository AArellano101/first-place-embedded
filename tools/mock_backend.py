"""Local hardware-endpoint fixture only; never impersonates a real app/database."""
import argparse
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import re
import threading
import time
import uuid

TEST_UUID = "12345678-1234-5678-9abc-123456789abc"
SCENARIOS = {"off", "green", "yellow", "red", "fragmented", "chunked", "malformed", "oversized",
             "truncated", "unknown", "unauthorized", "bad_request", "not_found", "server_error", "stalled", "redirect"}


class Fixture(ThreadingHTTPServer):
    daemon_threads = True
    def __init__(self, address, table_id=TEST_UUID, key="test-device-key", scenario="off"):
        super().__init__(address, Handler)
        self.table_id = str(uuid.UUID(table_id))
        self.key = key
        self.scenario = scenario
        self.lock = threading.Lock()
        self.requests_seen = []


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    def log_message(self, *_):
        pass  # Do not print header values or requests containing configuration.

    def respond(self, status, body=b"{}", headers=None):
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        for key, value in (headers or {}).items():
            self.send_header(key, value)
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def do_POST(self):
        if self.path != "/__scenario":
            return self.respond(404)
        try:
            size = int(self.headers.get("Content-Length", "0"))
            if not 0 < size < 128:
                return self.respond(400)
            scenario = json.loads(self.rfile.read(size))["scenario"]
            if scenario not in SCENARIOS:
                return self.respond(400)
            with self.server.lock:
                self.server.scenario = scenario
            self.respond(200)
        except (ValueError, KeyError):
            self.respond(400)

    def do_GET(self):
        with self.server.lock:
            scenario = self.server.scenario
            # Bounded diagnostics without the actual credential value.
            self.server.requests_seen.append((self.path, self.headers.get("Accept"),
                                              self.headers.get("Content-Length")))
            self.server.requests_seen[:] = self.server.requests_seen[-100:]
        if self.headers.get("X-Device-Key") != self.server.key:
            return self.respond(401)
        match = re.fullmatch(r"/hw/tables/([^/]+)/state", self.path)
        if not match:
            return self.respond(404)
        try:
            table_id = str(uuid.UUID(match[1]))
        except ValueError:
            return self.respond(400)
        if table_id != self.server.table_id:
            scenario = "off"  # Matches current backend's unknown-table contract.
        status = {"unauthorized": 401, "bad_request": 400, "not_found": 404, "server_error": 500}.get(scenario)
        if status:
            return self.respond(status)
        if scenario == "redirect":
            return self.respond(302, headers={"Location": "http://127.0.0.1:1/do-not-follow"})
        if scenario == "stalled":
            time.sleep(15)
            return self.respond(200, b"{}")
        color = scenario if scenario in {"off", "green", "yellow", "red"} else "yellow"
        if scenario == "unknown":
            color = "welcome"
        body = json.dumps({"light": color, "at": datetime.now(timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z"), "pollMs": 2000}).encode()
        if scenario == "malformed":
            body = b'{"light":'
        if scenario == "oversized":
            body += b" " * 3000
        if scenario == "truncated":
            self.send_response(200)
            self.send_header("Content-Length", str(len(body) + 50))
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(body)
            self.close_connection = True
            return
        if scenario in {"fragmented", "chunked"}:
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            if scenario == "chunked":
                self.send_header("Transfer-Encoding", "chunked")
            else:
                self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            for offset in range(0, len(body), 3):
                part = body[offset:offset + 3]
                self.wfile.write(f"{len(part):x}\r\n".encode() + part + b"\r\n" if scenario == "chunked" else part)
                self.wfile.flush()
                time.sleep(0.002)
            if scenario == "chunked":
                self.wfile.write(b"0\r\n\r\n")
            return
        self.respond(200, body)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1", help="Use 0.0.0.0 explicitly for a trusted development LAN")
    parser.add_argument("--port", type=int, default=8787)
    parser.add_argument("--table-id", default=TEST_UUID, help="Synthetic fixture ID only; never a production default")
    parser.add_argument("--scenario", choices=sorted(SCENARIOS), default="off")
    args = parser.parse_args()
    server = Fixture((args.host, args.port), args.table_id, scenario=args.scenario)
    print(f"TEST FIXTURE ONLY on {args.host}:{args.port}; public test key: test-device-key")
    print("Synthetic table UUID:", server.table_id)
    print('Set scenario with POST /__scenario and JSON {"scenario":"yellow"}. No real app/database is connected.')
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
