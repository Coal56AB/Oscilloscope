import base64
import json
import os
import re
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from PySide2.QtCore import Qt, QPoint, QObject, Signal
from PySide2.QtTest import QTest
from PySide2.QtWidgets import QApplication
from PySide2.QtGui import QCloseEvent
from files_ui import FilesTab
from scope_ui import ScopeTab
from terminal_ui import Console
from session import Session
from test_protocol import response
from protocol import ShellReply

APP = QApplication.instance() or QApplication([])


def spin_until(predicate, timeout=3):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        APP.processEvents()
        if predicate():
            return
        time.sleep(0.005)
    raise AssertionError("Qt operation timed out")


class FakeSerial:
    instances = []

    def __init__(self, **kwargs):
        self.instances.append(self)
        self.buffer = bytearray(b"oscill login: ")
        self.dtr = self.rts = None
        self.closed = False
        self.commands = []

    def open(self):
        assert not self.dtr and not self.rts

    @property
    def in_waiting(self):
        return len(self.buffer)

    def read(self, count):
        data = bytes(self.buffer[:min(count, 7)])
        del self.buffer[:len(data)]
        if not data:
            time.sleep(0.002)
        return data

    def write(self, command):
        self.commands.append(command)
        if command == b"root\n":
            self.buffer.extend(b"root\r\n# ")
        match = re.search(rb"__OSC_BEGIN_([a-f0-9]+)__", command)
        if match:
            reply = ShellReply(match.group(1).decode())
            self.buffer.extend(response(reply, b"hello\x00world"))
        return len(command)

    def close(self):
        self.closed = True


class GuiTests(unittest.TestCase):
    def test_update_keeps_connection_until_finished(self):
        from app_ui import MainWindow
        window = MainWindow()
        window.firmware.active = True
        window.firmware.active_changed.emit(True)
        self.assertFalse(window.connect_button.isEnabled())
        event = QCloseEvent()
        window.closeEvent(event)
        self.assertFalse(event.isAccepted())
        with patch.object(window.session, 'disconnect_port') as disconnect:
            window.toggle_connection()
            disconnect.assert_not_called()
        window.firmware.finished('OK')
        self.assertTrue(window.connect_button.isEnabled())
        window.closeEvent(event)
        self.assertTrue(event.isAccepted())
        window.close()

    def test_direct_settings_keep_draft_and_confirm_actual_state(self):
        class FakeSession(QObject):
            changed = Signal(bool)
            message = Signal(str)
            connected = True
            busy = False
            input_pending = False
            def request(self, command, callback, failure=None, timeout=30, defer=False):
                self.command = command
                self.callback = callback
                return True
        session = FakeSession()
        tab = ScopeTab(session)
        state = {key: 0 for key in tab.fields}
        state.update(version=1, running=1, waiting=0, source="demo", zoom=0,
                     ch1_enabled=1, ch2_enabled=1, ch1_scale_index=2, ch2_scale_index=3,
                     time_index=7, trigger_level=0.6, time_position=-101,
                     channel_scales_mv=[100, 200, 500, 1000, 2000, 5000],
                     time_scales_us=[0.5, 1, 2, 5, 10, 20, 50, 100])
        tab.load_state(state, force=True)
        self.assertEqual(tab.changed_values(), {})
        tab.fields["ch1_scale_index"][0].setCurrentIndex(4)
        tab.fields["trigger_level"][0].setValue(0.25)
        actual = dict(state, ch2_coupling=1)
        tab.load_state(actual)
        self.assertEqual(tab.changed_values(), {"ch1_scale_index": 4, "trigger_level": 0.25})
        self.assertEqual(tab.fields["ch2_coupling"][0].currentIndex(), 1)
        tab.apply()
        self.assertIn("oscill-remote set ch1_scale_index 4", session.command)
        self.assertIn("oscill-remote set trigger_level 0.25", session.command)
        self.assertNotIn("rotate", session.command)
        confirmed = dict(actual, ch1_scale_index=4, trigger_level=0.25)
        callback = session.callback
        callback(b"OK\nOK\n" + json.dumps(confirmed).encode() + b"\n")
        self.assertEqual(tab.changed_values(), {})
        self.assertEqual(session.command, "oscill-remote window")

    def test_deferred_apply_has_priority_and_preserves_later_edits(self):
        class Worker:
            def __init__(self): self.commands = []
            def request(self, key, command, timeout): self.commands.append((key, command))
        session = Session()
        session.connected = session.ready = True
        session.worker = Worker()
        tab = ScopeTab(session)
        state = {key: 0 for key in tab.fields}
        state.update(version=1, running=1, waiting=0, source="demo", zoom=0,
                     ch1_enabled=1, ch2_enabled=1, ch1_scale_index=2, ch2_scale_index=3,
                     time_index=7, trigger_level=0.6,
                     channel_scales_mv=[100, 200, 500, 1000, 2000, 5000],
                     time_scales_us=[0.5, 1, 2, 5, 10, 20, 50, 100])
        tab.load_state(state, force=True)
        tab.auto.setChecked(True)
        session.request("oscill-remote window", lambda data: tab.refresh())
        first = session.pending[0]
        tab.fields["ch1_scale_index"][0].setCurrentIndex(4)
        tab.apply()
        self.assertEqual(len(session.worker.commands), 1)
        self.assertEqual(len(session.deferred), 1)
        tab.fields["trigger_level"][0].setValue(0.25)
        session._result(first, b"ignored")
        self.assertIn("set ch1_scale_index 4", session.worker.commands[-1][1])
        self.assertEqual(len(session.worker.commands), 2)
        confirmed = dict(state, ch1_scale_index=4)
        session._result(session.pending[0], b"OK\n" + json.dumps(confirmed).encode() + b"\n")
        self.assertEqual(tab.changed_values(), {"trigger_level": 0.25})
        self.assertEqual(session.worker.commands[-1][1], "oscill-remote window")
        tab.apply()
        session._disconnected("test unplug")
        self.assertFalse(session.deferred)
        self.assertIn("отменена", tab.info.text())
        tab.timer.stop()

    def test_deferred_request_waits_for_terminal_line(self):
        class Worker:
            def __init__(self): self.commands = []
            def send(self, data): self.commands.append(data)
            def request(self, key, command, timeout): self.commands.append(command)
        session = Session()
        session.connected = session.ready = True
        session.worker = Worker()
        session.send(b"echo hello")
        session.request("apply snapshot", lambda data: None, defer=True)
        self.assertEqual(session.worker.commands, [b"echo hello"])
        session.send(b"\n")
        self.assertEqual(session.worker.commands, [b"echo hello", b"\n", "apply snapshot"])

    def test_serial_qt_callback_busy_and_disconnect(self):
        session = Session()
        values, messages = [], []
        session.message.connect(lambda message: messages.append(message))
        with patch("transport.serial.Serial", FakeSerial):
            session.connect_port("TEST", 115200)
            spin_until(lambda: session.ready)
            session.send(b"partial")
            self.assertFalse(session.request("must not append to user input", values.append))
            session.send(b"\x03")
            self.assertTrue(session.request("printf hello", values.append))
            self.assertFalse(session.request("must not enqueue", values.append))
            session.send(b"must not mix terminal into transfer")
            spin_until(lambda: bool(values))
            self.assertEqual(values, [b"hello\x00world"])
            self.assertEqual(len(FakeSerial.instances[-1].commands), 5)
            self.assertFalse(session.busy)
            session.disconnect_port()
            spin_until(lambda: session.worker is None)
            self.assertTrue(FakeSerial.instances[-1].closed)

    def test_single_terminal_keyboard_and_receive(self):
        console = Console()
        values = []
        console.send.connect(lambda data: values.append(data))
        QTest.keyClicks(console, "ls")
        QTest.keyClick(console, Qt.Key_Return)
        QTest.keyClick(console, Qt.Key_C, Qt.ControlModifier)
        self.assertEqual(values, [b"l", b"s", b"\n", b"\x03"])
        console.append_bytes(b"root# ls\r\nfile.csv\r\nroot# ")
        QTest.keyClick(console, Qt.Key_Up)
        self.assertEqual(values[-1], b"\x15ls")
        self.assertEqual(console.toPlainText(), "root# ls\nfile.csv\nroot# ")
        console.append_bytes(b"abc\b \b")
        console.append_bytes(b"d")
        self.assertTrue(console.toPlainText().endswith("root# abd"))
        APP.clipboard().setText("echo hello")
        QTest.keyClick(console, Qt.Key_V, Qt.ControlModifier)
        self.assertEqual(values[-1], b"echo hello")

    def test_download_atomic_commit_and_cancel(self):
        class FakeSession:
            connected = True
            busy = False
            class Events(QObject):
                changed = Signal(bool)
                message = Signal(str)
            events = Events()
            changed = events.changed
            message = events.message

            def request(self, command, callback, failure=None, timeout=30):
                self.callback = callback
                return True

        session = FakeSession()
        tab = FilesTab(session)
        tab.refresh()
        session.callback(b"f\t5\t" + base64.b64encode(b"example.csv"))
        item = tab.tree.topLevelItem(0).child(0)
        tab.tree.setCurrentItem(item)
        with tempfile.TemporaryDirectory() as folder:
            target = Path(folder) / "example.csv"
            target.write_bytes(b"old")
            with patch("files_ui.QFileDialog.getSaveFileName", return_value=(str(target), "")):
                tab.start_download()
                session.callback(b"12345")
                self.assertEqual(target.read_bytes(), b"12345")
                self.assertIsNone(tab.download)
                tab.start_download()
                tab.cancelled = True
                session.callback(b"xxxxx")
                self.assertEqual(target.read_bytes(), b"12345")
                self.assertIsNone(tab.download)
                tab.start_download()
                session.callback(b"x")
                self.assertEqual(target.read_bytes(), b"12345")
                self.assertIsNone(tab.download)


if __name__ == "__main__":
    unittest.main()
