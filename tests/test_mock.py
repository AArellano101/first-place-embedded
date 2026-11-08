"""Real localhost HTTP fixtures -> native firmware parser, not an ESP radio test."""
import http.client
import importlib.util
from pathlib import Path
import subprocess
import socket
import sys
import threading

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("mock_backend", root / "tools/mock_backend.py")
mock = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mock)
spec = importlib.util.spec_from_file_location("provision", root / "tools/provision.py")
provision = importlib.util.module_from_spec(spec)
spec.loader.exec_module(provision)
probe = sys.argv[1]
server = mock.Fixture(("127.0.0.1", 0))
worker = threading.Thread(target=server.serve_forever, daemon=True)
worker.start()


def request(scenario, expected, key="test-device-key", table_id=mock.TEST_UUID):
    server.scenario = scenario
    connection = http.client.HTTPConnection("127.0.0.1", server.server_port, timeout=1)
    path = f"/hw/tables/{table_id}/state"
    connection.request("GET", path, headers={"X-Device-Key": key, "Accept": "application/json"})
    response = connection.getresponse()
    complete = True
    try:
        body = response.read()
    except http.client.IncompleteRead as exc:
        body, complete = exc.partial, False
    result = subprocess.run([probe, str(response.status), str(int(complete))], input=body,
                            stdout=subprocess.PIPE, check=True).stdout.decode().strip()
    connection.close()
    assert result == expected, (scenario, response.status, result, expected)
    path_seen, accept, content_length = server.requests_seen[-1]
    assert path_seen == path and accept == "application/json" and content_length is None


try:
    for color in ("off", "green", "yellow", "red"):
        request(color, color)
    request("fragmented", "yellow")
    request("chunked", "yellow")
    for scenario in ("malformed", "oversized", "truncated", "unknown", "unauthorized", "bad_request",
                     "not_found", "server_error", "redirect"):
        request(scenario, "REJECT")
    request("green", "REJECT", key="bad-key")
    request("green", "REJECT", table_id="bad-uuid")
    request("green", "off", table_id="aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee")
    request("green", "green")  # Recovery after failures.
    server.scenario = "stalled"
    connection = http.client.HTTPConnection("127.0.0.1", server.server_port, timeout=0.1)
    connection.request("GET", f"/hw/tables/{mock.TEST_UUID}/state", headers={"X-Device-Key": "test-device-key", "Accept": "application/json"})
    try:
        connection.getresponse()
        raise AssertionError("Expected a stalled fixture to time out")
    except (TimeoutError, socket.timeout):
        pass
    finally:
        connection.close()
    request("yellow", "yellow")
    assert provision.table_link(mock.TEST_UUID) == "firstplace://table/" + mock.TEST_UUID
    config = {"ssid": "test-wifi", "password": "test-password", "base_url": "http://192.0.2.1:8787/",
              "table_id": mock.TEST_UUID.upper(), "device_key": "test-device-key", "allow_http": True}
    assert provision.validate(config)["table_id"] == mock.TEST_UUID
    print("PASS: localhost HTTP fixtures -> firmware parser, exact GET contract, invalid/unknown UUID, bad key, recovery, QR link identity")
finally:
    server.shutdown()
    server.server_close()
