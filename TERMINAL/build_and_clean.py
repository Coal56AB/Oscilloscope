"""Nuitka standalone + onefile, following the supplied BusTerminal build."""
import argparse
import shutil
import subprocess
import sys
from pathlib import Path


def build():
    parser = argparse.ArgumentParser()
    parser.add_argument("--keep-build", action="store_true")
    args = parser.parse_args()
    if sys.version_info[:3] != (3, 7, 9):
        raise SystemExit("Для этой сборки требуется Python 3.7.9")
    root = Path(__file__).resolve().parent
    output = (root / "output").resolve()
    output.mkdir(parents=True, exist_ok=True)
    command = [sys.executable, "-m", "nuitka", "--standalone", "--onefile",
               "--enable-plugin=pyside2", "--include-qt-plugins=platforms,styles,imageformats",
               "--windows-console-mode=disable", "--msvc=14.2", "--assume-yes-for-downloads",
               "--output-dir=" + str(output), "--output-filename=OscillTerminal.exe",
               "--report=" + str(output / "compilation-report.xml"), str(root / "mainterm.py")]
    print("Сборка OscillTerminal через Nuitka", flush=True)
    subprocess.run(command, cwd=str(root), check=True)
    executable = output / "OscillTerminal.exe"
    if not executable.is_file():
        raise SystemExit("Nuitka не создала исполняемый файл")
    if not args.keep_build:
        for name in ("mainterm.build", "mainterm.onefile-build", "mainterm.dist"):
            path = (output / name).resolve()
            if path.parent != output:
                raise RuntimeError("Каталог очистки вне output")
            if path.exists():
                shutil.rmtree(str(path))
    print("Готово: " + str(executable), flush=True)


if __name__ == "__main__":
    build()
