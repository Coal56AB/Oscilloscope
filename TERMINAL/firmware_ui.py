from PySide2.QtCore import Signal

from PySide2.QtWidgets import QWidget, QVBoxLayout, QHBoxLayout, QLabel, QLineEdit, QPushButton, QFileDialog, QProgressBar
from firmware import FirmwareJob, read_images


class FirmwareTab(QWidget):
    active_changed = Signal(bool)

    def __init__(self, session, parent=None):
        super().__init__(parent)
        self.session = session
        self.active = False
        layout = QVBoxLayout(self)
        layout.addWidget(QLabel('Каталог с BOOT.BIN и image.bin'))
        row = QHBoxLayout()
        self.directory = QLineEdit()
        row.addWidget(self.directory, 1)
        self.browse_button = QPushButton('Выбрать…')
        self.browse_button.clicked.connect(self.browse)
        row.addWidget(self.browse_button)
        layout.addLayout(row)
        self.details = QLabel('Передаются два образа без заполнения qspi.bin.')
        self.details.setWordWrap(True)
        layout.addWidget(self.details)
        self.start_button = QPushButton('Перепрошить прибор')
        self.start_button.clicked.connect(self.start)
        layout.addWidget(self.start_button)
        self.bar = QProgressBar()
        layout.addWidget(self.bar)
        self.state = QLabel('Прибор должен быть подключён; Linux должен быть загружен.')
        self.state.setWordWrap(True)
        layout.addWidget(self.state)
        layout.addStretch()
        session.firmware_progress.connect(self.progress)

    def browse(self):
        path = QFileDialog.getExistingDirectory(self, 'Прошивка', self.directory.text())
        if path:
            self.directory.setText(path)
            try:
                images = read_images(path)
                self.details.setText('К передаче: {:,} байт. BOOT.BIN: {:,}; image.bin: {:,}.'.format(
                    sum(len(data) for _, data in images), len(images[0][1]), len(images[1][1])))
            except Exception as error:
                self.state.setText(str(error))

    def start(self):
        if self.active:
            return
        try:
            job = FirmwareJob(self.directory.text())
        except Exception as error:
            self.state.setText(str(error)); return
        if self.session.request(job, self.finished, self.failed):
            self.active = True
            self.active_changed.emit(True)
            self.start_button.setEnabled(False)
            self.directory.setEnabled(False)
            self.browse_button.setEnabled(False)
            self.state.setText('Проверка файлов…')

    def progress(self, value):
        phase, done, total = value
        self.bar.setValue(done * 100 // total if total else 0)
        self.state.setText('{}: {:,} / {:,}'.format(phase, done, total))

    def finished(self, result):
        self.active = False
        self.active_changed.emit(False)
        self.start_button.setEnabled(True)
        self.directory.setEnabled(True)
        self.browse_button.setEnabled(True)
        self.state.setText(str(result))

    def failed(self, reason):
        self.finished(reason)
