"""One owner for connection and outstanding application request."""
import re
from collections import deque
from PySide2.QtCore import QObject, Signal, QTimer
from transport import SerialWorker


class Session(QObject):
    console = Signal(bytes)
    changed = Signal(bool)
    busy_changed = Signal(bool)
    message = Signal(str)
    progress = Signal(int)
    login_required = Signal()
    firmware_progress = Signal(object)
    baud_changed = Signal(int)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.worker = None
        self.connected = False
        self.pending = None
        self.deferred = deque()
        self.sequence = 0
        self.input_pending = False
        self.ready = False
        self.login_buffer = bytearray()
        self.login_sent = False

    @property
    def busy(self):
        return self.pending is not None

    def connect_port(self, port, baud):
        if self.worker is not None:
            return
        self.worker = SerialWorker(port, baud, self)
        self.worker.opened.connect(self._opened)
        self.worker.console.connect(self._console)
        self.worker.result.connect(self._result)
        self.worker.failed.connect(self._failed)
        self.worker.disconnected.connect(self._disconnected)
        self.worker.progress.connect(self.progress)
        self.worker.firmware_progress.connect(self.firmware_progress)
        self.worker.baud_changed.connect(self.baud_changed)
        self.worker.finished.connect(self._finished)
        self.worker.start()

    def _opened(self):
        self.connected = True
        self.changed.emit(True)
        self.ready = False
        self.login_sent = False
        self.login_buffer.clear()
        self.worker.send(b"\n")
        self.message.emit("COM открыт. Проверяю консоль Linux…")
        QTimer.singleShot(5000, self._login_timeout)

    def _console(self, data):
        self.console.emit(data)
        self.login_buffer.extend(data)
        self.login_buffer = self.login_buffer[-512:]
        tail = bytes(self.login_buffer)
        if re.search(rb"(?:^|[\r\n])(?:Zynq>|=>) ?$", tail):
            self.ready = False
            self.login_required.emit()
            self.message.emit("Плата находится в U-Boot. Для чтения настроек нужен запущенный Linux")
            self.login_buffer.clear()
        elif re.search(rb"(?:^|[\r\n])[^\r\n]*login: ?$", tail):
            self.ready = False
            if not self.login_sent:
                self.login_sent = True
                self.worker.send(b"root\n")
                self.message.emit("Вхожу в Linux как root…")
            else:
                self.login_required.emit()
                self.message.emit("Войдите в Linux на вкладке «Терминал»")
            self.login_buffer.clear()
        elif re.search(rb"(?:^|[\r\n])[Pp]assword: ?$", tail):
            self.ready = False
            self.login_required.emit()
            self.message.emit("Linux запросил пароль. Введите его в терминале")
            self.login_buffer.clear()
        elif not self.ready and re.search(rb"(?:^|[\r\n])[^\r\n]*[#$] $", tail):
            self.ready = True
            self.input_pending = False
            self.login_buffer.clear()
            self.message.emit("Linux готов. Можно читать настройки, график и файлы")

    def _login_timeout(self):
        if self.connected and not self.ready:
            self.login_required.emit()
            self.message.emit("Нет приглашения shell. Проверьте ответ Linux в терминале")

    def disconnect_port(self):
        if self.worker:
            self.worker.stop()

    def _finished(self):
        worker = self.worker
        self.worker = None
        if worker:
            worker.deleteLater()

    def _disconnected(self, reason):
        cancelled = list(self.deferred)
        if self.pending:
            cancelled.append((None, None, self.pending[2], None))
        self.deferred.clear()
        self.connected = False
        self.ready = False
        self.pending = None
        self.input_pending = False
        self.busy_changed.emit(False)
        self.changed.emit(False)
        self.message.emit(reason)
        for _, _, failure, _ in cancelled:
            if failure:
                failure("Операция отменена: UART отключён")

    def send(self, data):
        if self.connected and not self.busy:
            for byte in data:
                if byte in (3, 10, 13, 21):
                    self.input_pending = False
                elif byte >= 32 or byte == 27:
                    self.input_pending = True
            self.worker.send(data)
            self._drain()
        elif self.busy:
            self.message.emit("Дождитесь завершения текущей операции")

    def request(self, command, success, failure=None, timeout=30, defer=False):
        if not self.connected:
            self.message.emit("Сначала подключитесь к COM-порту")
            return False
        if not self.ready:
            self.login_required.emit()
            self.message.emit("Linux ещё не готов. Дождитесь входа или введите пароль в терминале")
            return False
        if defer and (self.busy or self.input_pending or self.deferred):
            self.deferred.append((command, success, failure, timeout))
            self.message.emit("Операция запомнена и выполнится после освобождения UART")
            return True
        if self.busy:
            self.message.emit("Дождитесь завершения текущей операции")
            return False
        if self.input_pending:
            self.message.emit("Завершите строку терминала: Enter или Ctrl+C")
            return False
        self.sequence += 1
        self.pending = (self.sequence, success, failure)
        self.busy_changed.emit(True)
        self.worker.request(self.sequence, command, timeout)
        return True

    def _drain(self):
        if self.deferred and self.connected and self.ready and not self.busy and not self.input_pending:
            command, success, failure, timeout = self.deferred.popleft()
            self.request(command, success, failure, timeout)

    def _result(self, key, value):
        if self.pending and self.pending[0] == key:
            _, callback, _ = self.pending
            self.pending = None
            self._drain()
            self.busy_changed.emit(self.busy)
            try:
                callback(value)
            except Exception as error:
                self.message.emit(str(error))

    def _failed(self, key, reason):
        if self.pending and self.pending[0] == key:
            _, _, callback = self.pending
            self.pending = None
            self._drain()
            self.busy_changed.emit(self.busy)
            if callback:
                callback(reason)
        self.message.emit(reason)
