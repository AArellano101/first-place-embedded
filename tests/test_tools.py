"""Exercise provisioning prompts and real SVG creation with a simulated USB reply."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import sys
from unittest.mock import patch
import xml.etree.ElementTree as ET
import qrcode

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("provision", root / "tools/provision.py")
provision = importlib.util.module_from_spec(spec)
spec.loader.exec_module(provision)
table_id = "12345678-1234-5678-9abc-123456789abc"
capture = io.StringIO()
with patch.object(sys, "argv", ["provision.py", "provision", "--base-url", "https://example.com", "--table-id", table_id]), \
     patch("builtins.input", return_value="test-wifi"), \
     patch.object(provision.getpass, "getpass", side_effect=["private-test-password", "private-test-device-key"]), \
     patch.object(provision, "transact", return_value="") as transfer, \
     contextlib.redirect_stdout(capture):
    provision.main()
command = transfer.call_args.args[1]
assert command.startswith("PROVISION ")
payload = json.loads(command[len("PROVISION "):])
assert payload["table_id"] == table_id and payload["device_key"] == "private-test-device-key"
assert "private-test-password" not in capture.getvalue() and "private-test-device-key" not in capture.getvalue()

output = root / "build/host-tests/fixture-only-qr.svg"
with patch.object(sys, "argv", ["provision.py", "qr", "--output", str(output)]), \
     patch.object(provision, "info", return_value={"provisioned": True, "table_id": table_id}), \
     patch.object(qrcode, "make", wraps=qrcode.make) as make, \
     contextlib.redirect_stdout(io.StringIO()):
    provision.main()
assert make.call_args.args[0] == "firstplace://table/" + table_id
assert make.call_args.kwargs["border"] == 4
assert ET.parse(output).getroot().tag.endswith("svg")
assert output.with_suffix(".txt").read_text().strip() == "firstplace://table/" + table_id
print("PASS: hidden provisioning prompts, no printed credentials, real SVG QR encoding stored fixture UUID (USB simulated)")
