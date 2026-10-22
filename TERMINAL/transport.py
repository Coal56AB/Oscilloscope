"""Serial I/O and command serialization, independent of application widgets."""
import queue
import threading
import time

import serial
from PySide2.QtCore import QThread, Signal

from protocol import ShellReply


class SerialWorker(QThread):
    opened = Signal()
    console = Signal(bytes)
    result = Signal(object, object)
    failed = Signal(object, str)
    disconnected = Signal(str)
    progress = Signal(int)

    def __init__(self, port, baud, parent=None):
        super().__init__(parent)
        self.port = port
        self.baud = baud
        self.tasks = queue.Queue()
        self.stopping = threading.Event()

    def stop(self):
        self.stopping.set()

    def send(self, data):
        self.tasks.put((None, data, 0))

    def request(self, key, command, timeout=30):
        self.tasks.put((key, command, timeout))

    def transact(self, connection, command, timeout):
        reply = ShellReply()
        connection.write(reply.command(command).encode("utf-8", "surrogateescape"))
        deadline = time.monotonic() + timeout
        received = 0
        while not self.stopping.is_set() and time.monotonic() < deadline:
            data = connection.read(max(1, min(connection.in_waiting, 65536)))
            if data:
                received += len(data)
                console = reply.feed(data)
                if console:
                    visible = b"\n".join(line for line in console.split(b"\n") if reply.token.encode() not in line)
                    if visible.strip():
                        self.console.emit(visible)
                self.progress.emit(received)
                if not reply.started and (b"login:" in reply.buffer or b"assword:" in reply.buffer):
                    self.console.emit(bytes(reply.buffer))
                    raise TimeoutError("Linux ожидает входа. Подключитесь заново и войдите в терминале.")
                if reply.done:
                    if reply.buffer:
                        self.console.emit(bytes(reply.buffer))
                    return reply.result()
        if self.stopping.is_set():
            raise InterruptedError("Соединение закрыто")
        raise TimeoutError("Нет полного ответа Linux (получено {} байт по UART). Подключитесь заново.".format(received))

    def run(self):
        reason = "Отключено"
        try:
            # DTR/RTS are not asserted intentionally; no board reset on connect.
            connection = serial.Serial(port=None, baudrate=self.baud, timeout=0.05, write_timeout=3)
            connection.dtr = False
            connection.rts = False
            connection.port = self.port
            connection.open()
            try:
                self.opened.emit()
                while not self.stopping.is_set():
                    try:
                        key, command, timeout = self.tasks.get_nowait()
                    except queue.Empty:
                        data = connection.read(max(1, min(connection.in_waiting, 65536)))
                        if data:
                            self.console.emit(data)
                        continue
                    try:
                        if key is None:
                            connection.write(command)
                        else:
                            self.result.emit(key, self.transact(connection, command, timeout))
                    except (TimeoutError, serial.SerialException) as error:
                        self.failed.emit(key, str(error))
                        raise
                    except InterruptedError:
                        break
                    except Exception as error:
                        self.failed.emit(key, str(error))
            finally:
                connection.close()
        except Exception as error:
            reason = str(error)
        self.disconnected.emit(reason)
