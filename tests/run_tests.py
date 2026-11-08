"""Compile/run the actual pure-C logic in production and demo modes (host GCC)."""
import os
from pathlib import Path
import shutil
import subprocess

root = Path(__file__).resolve().parents[1]
compiler = os.environ.get("CC") or shutil.which("gcc")
if not compiler:
    raise SystemExit("Install a host GCC or set CC to its executable path.")
output = root / "build" / "host-tests"
output.mkdir(parents=True, exist_ok=True)
for demo in (False, True):
    binary = output / ("demo" if demo else "production")
    if os.name == "nt":
        binary = binary.with_suffix(".exe")
    command = [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic", "-g",
               "-I", str(root / "main")]
    if demo:
        command += ["-DCONFIG_THIRD_PLACE_DEMO=1"]
    command += [str(root / path) for path in (
        "tests/test_beacon.c", "main/beacon.c", "main/input_logic.c", "main/beacon_ui.c")]
    subprocess.run(command + ["-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
