"""Run the onefile EXE alone, with Python absent from PATH."""
import argparse
import json
import os
import shutil
import subprocess
import tempfile
from pathlib import Path


def check(executable):
    with tempfile.TemporaryDirectory(prefix="oscill-standalone-") as folder:
        root = Path(folder)
        copy = root / "OscillTerminal.exe"
        shutil.copy2(str(executable), str(copy))
        result = root / "result.json"
        environment = dict(os.environ)
        environment["PATH"] = str(Path(environment.get("SystemRoot", environment.get("WINDIR", "C:/Windows"))) / "System32")
        environment["PYTHONHOME"] = str(root / "no-python")
        environment["PYTHONPATH"] = str(root / "no-modules")
        environment["QT_QPA_PLATFORM"] = "offscreen"
        environment["OSCILL_SMOKE_RESULT"] = str(result)
        completed = subprocess.run([str(copy), "--smoke-test"], cwd=str(root), env=environment, timeout=60)
        assert completed.returncode == 0, completed.returncode
        assert result.exists(), "No startup confirmation"
        details = json.loads(result.read_text(encoding="utf-8"))
        assert details["python"] == "3.7.9", details
        assert details["tabs"] == ["Терминал", "Linux", "Осциллограф", "SD-карта", "Прошивка"], details
        print(json.dumps(details, ensure_ascii=False))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("executable", type=Path)
    check(parser.parse_args().executable.resolve())
