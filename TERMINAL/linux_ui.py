import shlex
from PySide2.QtWidgets import QWidget, QVBoxLayout, QHBoxLayout, QGridLayout, QLabel, QPushButton, QPlainTextEdit, QLineEdit
from PySide2.QtCore import Qt


class CommandLine(QLineEdit):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.history = []
        self.index = 0

    def remember(self):
        if self.text() and (not self.history or self.history[-1] != self.text()):
            self.history.append(self.text())
            self.history = self.history[-100:]
        self.index = len(self.history)

    def keyPressEvent(self, event):
        if event.key() in (Qt.Key_Up, Qt.Key_Down):
            self.index = max(0, min(len(self.history), self.index + (-1 if event.key() == Qt.Key_Up else 1)))
            self.setText(self.history[self.index] if self.index < len(self.history) else "")
        else:
            super().keyPressEvent(event)


class LinuxTab(QWidget):
    def __init__(self, session, parent=None):
        super().__init__(parent)
        self.session = session
        layout = QVBoxLayout(self)
        layout.addWidget(QLabel("После входа в shell Linux на вкладке «Терминал» доступны команды и управление SD."))
        grid = QGridLayout()
        commands = [
            ("Проверить консоль", "printf 'Linux console OK\\n'"),
            ("Система", "uname -a; uptime"),
            ("Память", "free -m"),
            ("Файловые системы", "df -h; cat /proc/mounts"),
            ("Процессы", "ps"),
            ("Журнал ядра", "dmesg | tail -n 100"),
            ("Сеть", "ip addr; ip route"),
            ("Настройки осциллографа", "cat /etc/default/oscill"),
            ("Связь с GUI", "oscill-remote ping"),
            ("Носители", "cat /proc/partitions"),
        ]
        for index, (name, command) in enumerate(commands):
            button = QPushButton(name)
            button.clicked.connect(lambda checked=False, command=command: self.run(command))
            grid.addWidget(button, index // 5, index % 5)
        layout.addLayout(grid)
        row = QHBoxLayout()
        row.addWidget(QLabel("Раздел SD:"))
        self.device = QLineEdit()
        self.device.setPlaceholderText("Выберите по списку носителей, например /dev/mmcblk1p1")
        row.addWidget(self.device, 1)
        mount = QPushButton("Монтировать SD")
        mount.clicked.connect(self.mount)
        row.addWidget(mount)
        unmount = QPushButton("Извлечь SD")
        unmount.clicked.connect(lambda: self.run("oscill-card unmount"))
        row.addWidget(unmount)
        layout.addLayout(row)
        self.output = QPlainTextEdit()
        self.output.setReadOnly(True)
        self.output.setMaximumBlockCount(10000)
        layout.addWidget(self.output, 1)
        row = QHBoxLayout()
        self.command = CommandLine()
        self.command.setPlaceholderText("Выполнить команду Linux и показать результат здесь")
        self.command.returnPressed.connect(self.custom_command)
        row.addWidget(self.command, 1)
        button = QPushButton("Выполнить")
        button.clicked.connect(self.custom_command)
        row.addWidget(button)
        layout.addLayout(row)

    def mount(self):
        device = self.device.text().strip()
        if device:
            self.run("oscill-card mount " + shlex.quote(device))
        else:
            self.session.message.emit("Укажите раздел SD из списка носителей")

    def custom_command(self):
        command = self.command.text().strip()
        if command:
            self.command.remember()
            self.run(command)

    def run(self, command):
        def complete(data):
            self.output.appendPlainText("$ " + command + "\n" + data.decode("utf-8", "replace"))
        self.session.request(command, complete, lambda error: self.output.appendPlainText(error), timeout=60)
