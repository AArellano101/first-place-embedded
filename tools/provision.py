"""USB provisioning without command-line secrets; static QR from stored identity."""
import argparse
import getpass
import json
from pathlib import Path
import re
import time
from urllib.parse import urlsplit
import uuid


def table_link(table_id):
    if not re.fullmatch(r"[0-9a-fA-F]{8}-(?:[0-9a-fA-F]{4}-){3}[0-9a-fA-F]{12}", table_id):
        raise ValueError("A real database table UUID is required.")
    return f"firstplace://table/{str(uuid.UUID(table_id))}"


def validate(config):
    for key, maximum in (("ssid", 32), ("password", 64), ("base_url", 192),
                         ("table_id", 36), ("device_key", 128)):
        value = config.get(key)
        if not isinstance(value, str) or len(value.encode("utf-8")) > maximum or "\0" in value:
            raise ValueError(f"Invalid {key}; check its type/length.")
    if not config["ssid"] or not config["device_key"]:
        raise ValueError("SSID and device key are required.")
    password = config["password"]
    if password and not (8 <= len(password.encode()) <= 63 or re.fullmatch(r"[0-9a-fA-F]{64}", password)):
        raise ValueError("Wi-Fi password must be empty, 8-63 bytes, or a 64-digit hex PSK.")
    if any(not 33 <= ord(c) <= 126 for c in config["device_key"]):
        raise ValueError("Device key must be a printable ASCII token without whitespace.")
    table_link(config["table_id"])
    config["table_id"] = str(uuid.UUID(config["table_id"]))
    config["base_url"] = config["base_url"].rstrip("/")
    url = urlsplit(config["base_url"])
    if url.scheme not in ("http", "https") or not url.hostname or url.username or url.password or url.query or url.fragment:
        raise ValueError("Use an HTTP(S) base URL without credentials, query, or fragment.")
    if url.hostname.lower() == "localhost" or url.hostname.startswith("127.") or url.hostname == "::1":
        raise ValueError("Use the computer's LAN address, not localhost/loopback.")
    if url.scheme == "http" and config.get("allow_http") is not True:
        raise ValueError("Local HTTP requires --allow-http (credentials are unencrypted on that LAN).")
    return config


def transact(port_name, command, prefix, timeout=10):
    import serial
    port = serial.Serial()
    port.port, port.baudrate, port.timeout = port_name, 115200, 0.2
    port.dtr = port.rts = False
    with port:
        port.reset_input_buffer()
        port.write((command + "\n").encode("utf-8"))
        port.flush()
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            line = port.readline().decode("utf-8", errors="replace").strip()
            if "BOX_ERROR " in line:
                raise RuntimeError("Device rejected the operation; check configuration/storage.")
            offset = line.find(prefix)
            if offset >= 0:
                return line[offset + len(prefix):].strip()
    raise RuntimeError("No reply. Close other serial monitors, verify firmware/port, and retry.")


def info(port):
    response = json.loads(transact(port, "INFO", "BOX_INFO "))
    if response.get("provisioned") is not True:
        raise RuntimeError("Device is unprovisioned. Set a real table UUID first.")
    table_link(response["table_id"])
    return response


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("provision", "info", "qr", "erase"))
    parser.add_argument("--port", default="COM9")
    parser.add_argument("--config", type=Path, help="Optional ignored local JSON file; never committed")
    parser.add_argument("--base-url")
    parser.add_argument("--table-id")
    parser.add_argument("--allow-http", action="store_true", help="Explicit local-development HTTP opt-in")
    parser.add_argument("--output", type=Path, default=Path("artifacts/table-qr.svg"))
    args = parser.parse_args()
    if args.action == "provision":
        if args.config:
            config = json.loads(args.config.read_text(encoding="utf-8-sig"))
        else:
            config = {
                "ssid": input("2.4 GHz Wi-Fi SSID: "),
                "password": getpass.getpass("Wi-Fi password (empty for open network): "),
                "base_url": args.base_url or input("API base URL: ").strip(),
                "table_id": args.table_id or input("Actual database table UUID: ").strip(),
                "device_key": getpass.getpass("Allowlisted device key: "),
                "allow_http": args.allow_http,
            }
        validate(config)
        command = "PROVISION " + json.dumps(config, ensure_ascii=True, separators=(",", ":"))
        if len(command.encode()) > 1536:
            raise ValueError("Encoded provisioning command is too long.")
        transact(args.port, command, "BOX_PROVISION_OK")
        print("Provisioned in NVS; device is restarting. Credentials were not printed.")
        print("Table link:", table_link(config["table_id"]))
    elif args.action == "erase":
        if input("Erase this device's provisioning? Type ERASE: ") != "ERASE":
            return
        transact(args.port, "ERASE", "BOX_ERASE_OK")
        print("Provisioning erased; restarting with lamps off.")
    elif args.action == "info":
        print("Stored table link:", table_link(info(args.port)["table_id"]))
    else:
        # Read the device's stored UUID, never a seed/default or a separate CLI UUID.
        link = table_link(info(args.port)["table_id"])
        import qrcode
        import qrcode.image.svg
        if args.output.suffix.lower() != ".svg":
            raise ValueError("Use an .svg output filename for the printable QR.")
        args.output.parent.mkdir(parents=True, exist_ok=True)
        qr = qrcode.make(link, image_factory=qrcode.image.svg.SvgPathFillImage, border=4)
        qr.save(str(args.output))
        args.output.with_suffix(".txt").write_text(link + "\n", encoding="utf-8")
        print("QR saved:", args.output)
        print("Encode this exact URI on an optional passive NFC tag:", link)


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        # Never interpolate arbitrary exception messages: they may contain secret input.
        if isinstance(error, ModuleNotFoundError):
            print("Missing Python dependency. Run with the same Python: python -m pip install -r tools/requirements.txt")
        elif isinstance(error, json.JSONDecodeError):
            print(f"Invalid config JSON at line {error.lineno}, column {error.colno}. No config contents were printed.")
        elif isinstance(error, FileNotFoundError):
            print("File not found. Check the --config path and run from the firmware project folder.")
        elif isinstance(error, PermissionError):
            print("Access denied. Check file permissions and close programs using the serial port.")
        elif type(error).__module__.startswith("serial"):
            print("Serial port unavailable. Close ESP-IDF/VS Code/Arduino serial monitors and other tools using the port, then retry. Verify --port in Device Manager.")
        elif type(error) in (ValueError, RuntimeError):
            print("Error:", str(error))
        else:
            print("Operation failed. Check dependencies, local config, port access and device connection.")
        raise SystemExit(1)
