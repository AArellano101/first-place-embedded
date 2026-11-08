"""Run native tests against the actual C firmware model, parser and config code."""
import os
from pathlib import Path
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
idf = Path(os.environ.get("IDF_PATH", "C:/Espressif/frameworks/esp-idf-v5.5.2"))
cjson = idf / "components/json/cJSON"
compiler = os.environ.get("CC") or shutil.which("gcc")
if not compiler or not (cjson / "cJSON.c").exists():
    raise SystemExit("Set CC to a host GCC and IDF_PATH to ESP-IDF 5.5.2 (for its cJSON source).")
output = root / "build/host-tests"
output.mkdir(parents=True, exist_ok=True)
common = [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic", "-g",
          "-I", str(root / "main"), "-I", str(cjson)]
suffix = ".exe" if os.name == "nt" else ""
for source, name in (("test_box.c", "test_box"), ("parse_probe.c", "parse_probe")):
    target = output / (name + suffix)
    command = common + [str(root / "tests" / source)]
    command += [str(root / "main" / f) for f in ("box_model.c", "box_protocol.c", "box_config.c")]
    command += [str(cjson / "cJSON.c"), "-o", str(target)]
    subprocess.run(command, check=True)
    if name == "test_box":
        subprocess.run([str(target)], check=True)
subprocess.run([sys.executable, str(root / "tests/test_mock.py"), str(output / ("parse_probe" + suffix))], check=True)
