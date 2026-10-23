import json
import os
import sys

from PySide2.QtCore import QSettings, QTimer, Qt
from PySide2.QtWidgets import QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout, QLabel, QComboBox, QPushButton, QTabWidget
from serial.tools import list_ports

from files_ui import FilesTab
from firmware_ui import FirmwareTab
from linux_ui import LinuxTab
from scope_ui import ScopeTab
from session import Session
from terminal_ui import TerminalTab


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Oscill Terminal")
        self.resize(1360, 900)
        self.settings = QSettings("Oscill", "Terminal")
        self.session = Session(self)
        self.closing = False
        central = QWidget()
        self.setCentralWidget(central)
        layout = QVBoxLayout(central)
        row = QHBoxLayout()
        row.addWidget(QLabel("COM:"))
        self.port = QComboBox()
        self.port.setEditable(True)
        self.port.setMinimumWidth(260)
        row.addWidget(self.port)
        self.scan = QPushButton("Обновить")
        self.scan.clicked.connect(self.scan_ports)
        row.addWidget(self.scan)
        row.addWidget(QLabel("Скорость:"))
        self.baud = QComboBox()
        self.baud.addItems(["9600", "19200", "38400", "57600", "115200", "230400", "460800", "921600", "1000000", "2000000"])
        self.baud.setCurrentText(str(self.settings.value("baud", "1000000")))
        row.addWidget(self.baud)
        self.connect_button = QPushButton("Подключиться")
        self.connect_button.clicked.connect(self.toggle_connection)
        row.addWidget(self.connect_button)
        row.addWidget(QLabel("8N1 · без управления потоком"))
        row.addStretch()
        layout.addLayout(row)
        self.tabs = QTabWidget()
        self.terminal = TerminalTab(self.session)
        self.linux = LinuxTab(self.session)
        self.scope = ScopeTab(self.session)
        self.files = FilesTab(self.session)
        self.firmware = FirmwareTab(self.session)
        self.firmware.active_changed.connect(lambda active: self.connect_button.setEnabled(not active))
        for widget, title in [(self.terminal, "Терминал"), (self.linux, "Linux"), (self.scope, "Осциллограф"), (self.files, "SD-карта")]:
            self.tabs.addTab(widget, title)
        self.tabs.addTab(self.firmware, "Прошивка")
        layout.addWidget(self.tabs, 1)
        self.session.changed.connect(self.connection_changed)
        self.session.baud_changed.connect(lambda baud: self.baud.setCurrentText(str(baud)))
        self.session.login_required.connect(lambda: self.tabs.setCurrentWidget(self.terminal))
        self.session.message.connect(self.statusBar().showMessage)
        self.session.progress.connect(lambda count: self.statusBar().showMessage("Получено по UART: {} байт".format(count)))
        self.session.busy_changed.connect(lambda busy: self.statusBar().showMessage("Выполняется операция…" if busy else "Готово"))
        self.scan_ports()
        previous = str(self.settings.value("port", ""))
        index = self.port.findData(previous)
        if index >= 0:
            self.port.setCurrentIndex(index)
        elif previous:
            self.port.setEditText(previous)
        self.statusBar().showMessage("Выберите COM-порт осциллографа")

    def scan_ports(self):
        current = self.port.currentData() or self.port.currentText()
        self.port.clear()
        for info in list_ports.comports():
            self.port.addItem("{} — {}".format(info.device, info.description), info.device)
        index = self.port.findData(current)
        if index >= 0:
            self.port.setCurrentIndex(index)
        elif current:
            self.port.setEditText(current)

    def toggle_connection(self):
        if self.firmware.active:
            return
        if self.session.worker is not None:
            self.session.disconnect_port()
            self.connect_button.setEnabled(False)
            self.session.worker.finished.connect(lambda: self.connect_button.setEnabled(True))
        else:
            port = (self.port.currentData() if self.port.currentText() == self.port.itemText(self.port.currentIndex()) else None) or self.port.currentText().strip()
            if port:
                self.settings.setValue("port", port)
                self.settings.setValue("baud", self.baud.currentText())
                self.connect_button.setEnabled(False)
                self.session.connect_port(port, int(self.baud.currentText()))
                self.session.worker.finished.connect(lambda: self.connect_button.setEnabled(True))

    def connection_changed(self, connected):
        self.connect_button.setText("Отключиться" if connected else "Подключиться")
        self.connect_button.setEnabled(True)
        self.port.setEnabled(not connected)
        self.baud.setEnabled(not connected)
        self.scan.setEnabled(not connected)
        if self.closing and not connected:
            QTimer.singleShot(0, self.close)

    def closeEvent(self, event):
        if self.firmware.active:
            self.statusBar().showMessage("Идёт обновление прошивки. Дождитесь завершения записи и проверки")
            event.ignore()
            return
        if self.session.worker is not None and self.session.worker.isRunning():
            self.closing = True
            self.scope.auto.setChecked(False)
            self.session.worker.finished.connect(self.close)
            self.session.disconnect_port()
            event.ignore()
        else:
            event.accept()


def main():
    QApplication.setAttribute(Qt.AA_EnableHighDpiScaling)
    app = QApplication(sys.argv)
    app.setStyle("Fusion")
    window = MainWindow()
    window.show()
    # Deterministic startup check for the standalone executable, without hardware.
    if "--smoke-test" in sys.argv:
        def smoke():
            for index in range(window.tabs.count()):
                window.tabs.setCurrentIndex(index)
                app.processEvents()
            window.terminal.console.append_bytes(b"root# echo test\r\ntest\r\nroot# ")
            target = os.environ.get("OSCILL_SMOKE_RESULT")
            if target:
                with open(target, "w", encoding="utf-8") as stream:
                    json.dump({"tabs": [window.tabs.tabText(i) for i in range(window.tabs.count())],
                               "python": sys.version.split()[0], "qt": "PySide2", "ports": window.port.count()}, stream, ensure_ascii=False)
            window.close()
        QTimer.singleShot(200, smoke)
    return app.exec_()


if __name__ == "__main__":
    sys.exit(main())
