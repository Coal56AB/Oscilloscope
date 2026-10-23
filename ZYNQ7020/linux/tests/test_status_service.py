#!/usr/bin/env python3
"""Exercise the real SDL loop and independent status process without hardware."""
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time


def main():
    build = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="oscill-status-service-") as directory:
        socket = str(Path(directory) / "status.sock")
        logfile = Path(directory) / "status.log"
        with logfile.open("w+") as log:
            service = subprocess.Popen([str(build / "oscill-status"), "--text", "--socket", socket], stdout=log)
            gui = None
            try:
                for _ in range(100):
                    if Path(socket).exists():
                        break
                    time.sleep(0.05)
                assert Path(socket).exists(), "status receiver did not start"
                environment = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
                gui = subprocess.Popen([str(build / "scope_preview"), "--windowed", "--status-socket", socket], env=environment)
                time.sleep(2.5)
                log.flush()
                assert "GUI: READY" in logfile.read_text(), "real GUI did not publish heartbeat"
                os.kill(gui.pid, signal.SIGSTOP)
                time.sleep(4.5)
                assert "GUI TIMEOUT" in logfile.read_text(), "monitor stalled with GUI"
                assert service.poll() is None
                os.kill(gui.pid, signal.SIGCONT)
                time.sleep(1.5)
                assert gui.poll() is None
                gui.terminate()
                assert gui.wait(timeout=10) == 0
                time.sleep(1.5)
                assert "ACTIVE" in logfile.read_text() and "LAST ERROR" in logfile.read_text()
                print("SDL heartbeat, independent GUI freeze detection, recovery and clean stop: PASS")
            finally:
                if gui is not None and gui.poll() is None:
                    os.kill(gui.pid, signal.SIGCONT)
                    gui.terminate()
                    gui.wait(timeout=10)
                service.terminate()
                service.wait(timeout=10)
        assert not Path(socket).exists(), "service left its socket behind"


if __name__ == "__main__":
    main()
