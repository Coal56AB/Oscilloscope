"""Host Linux integration, using the actual SDL frontend and serial shell framing."""
import argparse
import csv
import io
import json
import os
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from protocol import ShellReply, list_command, parse_listing, read_chunk_command
from capture import parse_window

parser = argparse.ArgumentParser()
parser.add_argument("--build-dir", required=True)
parser.add_argument("--window-output", type=Path)
options, rest = parser.parse_known_args()
BUILD = Path(options.build_dir).resolve()


class DeviceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = tempfile.TemporaryDirectory(prefix="oscill-terminal-")
        cls.root = Path(cls.folder.name)
        cls.socket = cls.root / "scope.sock"
        cls.environment = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_RENDER_DRIVER="software")
        cls.gui = subprocess.Popen([str(BUILD / "scope_preview"), "--kiosk", "--remote-socket", str(cls.socket),
                                    "--data-dir", str(cls.root)], env=cls.environment, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        for _ in range(100):
            if cls.socket.exists():
                break
            if cls.gui.poll() is not None:
                raise RuntimeError(cls.gui.stderr.read().decode())
            time.sleep(0.02)
        else:
            raise RuntimeError("GUI did not open its socket")

    @classmethod
    def tearDownClass(cls):
        cls.gui.terminate()
        cls.gui.communicate(timeout=10)
        cls.folder.cleanup()

    def remote(self, *args, check=True):
        return subprocess.run([str(BUILD / "oscill-remote"), "--socket", str(self.socket)] + list(args),
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=check, timeout=15)

    def shell(self, command):
        reply = ShellReply()
        result = subprocess.run(["sh", "-c", reply.command(command)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
        for offset in range(0, len(result.stdout), 17):
            reply.feed(result.stdout[offset:offset + 17])
        return reply.result()

    def test_window_direct_settings_and_csv(self):
        import shlex
        self.assertEqual(self.remote("ping").stdout, b"OSCILL REMOTE 1\n")
        payload = self.shell("{} --socket {} window".format(shlex.quote(str(BUILD / "oscill-remote")), shlex.quote(str(self.socket))))
        window = parse_window(payload)
        self.assertEqual(len(window.samples[0]), 1024)
        self.assertEqual(len(window.volts[1]), 1024)
        (self.root / "window.bin").write_bytes(payload)
        if options.window_output:
            options.window_output.write_bytes(payload)
        self.remote("stop")
        before = self.remote("csv").stdout
        self.remote("set", "time_index", "4")
        after = self.remote("csv").stdout
        self.assertNotEqual(before.split(b"#time_index=")[1].splitlines()[0], after.split(b"#time_index=")[1].splitlines()[0])
        settings = {"ch1_enabled": 0, "ch2_enabled": 1, "ch1_scale_index": 4, "ch2_scale_index": 2,
                    "ch1_coupling": 1, "ch2_coupling": 0, "ch1_probe_ten": 1, "ch2_probe_ten": 0,
                    "ch1_position": 40, "ch2_position": -20, "ch1_filter": 2, "ch2_filter": 1,
                    "time_position": -100, "trigger_mode": 1, "trigger_source": 1,
                    "trigger_edge": 2, "trigger_holdoff": 3, "trigger_level": 0.1}
        for key, value in settings.items():
            self.remote("set", key, str(value))
        state = json.loads(self.remote("state").stdout)
        for key, value in settings.items():
            self.assertAlmostEqual(state[key], value, delta=0.02)
        self.remote("force")
        self.remote("single")
        rows = list(csv.reader(io.StringIO("\n".join(line for line in after.decode().splitlines() if not line.startswith("#")))))
        self.assertEqual(rows[0], ["sample", "ch1_y", "ch2_y"])
        self.assertEqual(len(rows), 1025)
        for args in [("set", "unknown", "1"), ("set", "time_index", "-1"), ("set", "trigger_mode", "5"), ("set", "ch1_scale_index", "1.5"), ("set", "trigger_level", "nan"), ("unknown",)]:
            self.assertNotEqual(self.remote(*args, check=False).returncode, 0)
        self.assertEqual(self.remote("ping").returncode, 0)

    def test_sd_names_chunking_and_escape(self):
        sd = self.root / "sd"
        sd.mkdir(exist_ok=True)
        name = "данные ' $(touch bad)\n.csv"
        data = bytes(range(256)) * 400
        (sd / name).write_bytes(data)
        (sd / "папка").mkdir(exist_ok=True)
        (sd / "escape").symlink_to(self.root, target_is_directory=True)
        entries = parse_listing(self.shell(list_command(str(sd), str(sd))))
        self.assertIn((name, "f", len(data)), entries)
        self.assertIn(("escape", "l", len(str(self.root))), entries)
        content = b"".join(self.shell(read_chunk_command(str(sd), str(sd / name), n)) for n in range(3))
        self.assertEqual(content, data)
        self.assertFalse((sd / "bad").exists())
        with self.assertRaises(RuntimeError):
            self.shell(list_command(str(sd), str(sd / "escape")))
        with self.assertRaises(RuntimeError):
            self.shell(read_chunk_command(str(sd), str(sd / "missing"), 0))


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]] + rest)
