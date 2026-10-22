import posixpath

from PySide2.QtCore import Qt, QSaveFile, QIODevice
from PySide2.QtWidgets import QWidget, QVBoxLayout, QHBoxLayout, QLineEdit, QLabel, QPushButton, QTreeWidget, QTreeWidgetItem, QFileDialog

from protocol import list_command, parse_listing, read_chunk_command


class FilesTab(QWidget):
    def __init__(self, session, parent=None):
        super().__init__(parent)
        self.session = session
        self.download = None
        self.cancelled = False
        self.root_path = "/media/scope"
        layout = QVBoxLayout(self)
        row = QHBoxLayout()
        row.addWidget(QLabel("SD в осциллографе:"))
        self.root = QLineEdit(self.root_path)
        row.addWidget(self.root, 1)
        refresh = QPushButton("Прочитать SD")
        refresh.clicked.connect(self.refresh)
        row.addWidget(refresh)
        self.save = QPushButton("Скачать файл")
        self.save.clicked.connect(self.start_download)
        row.addWidget(self.save)
        self.cancel = QPushButton("Отменить скачивание")
        self.cancel.setEnabled(False)
        self.cancel.clicked.connect(lambda: setattr(self, "cancelled", True))
        row.addWidget(self.cancel)
        layout.addLayout(row)
        self.tree = QTreeWidget()
        self.tree.setHeaderLabels(["Имя", "Тип", "Размер, байт"])
        self.tree.setColumnWidth(0, 440)
        self.tree.itemExpanded.connect(self.expanded)
        self.tree.itemDoubleClicked.connect(lambda item, column: self.start_download() if item.data(0, Qt.UserRole + 1) == "f" else None)
        layout.addWidget(self.tree, 1)
        self.info = QLabel("Смонтируйте карту во вкладке «Linux», затем нажмите «Прочитать SD»")
        layout.addWidget(self.info)
        session.changed.connect(lambda connected: self.abort("Соединение закрыто") if not connected and self.download else None)

    def refresh(self):
        if self.download:
            return
        root = posixpath.normpath(self.root.text().strip())
        if not root.startswith("/") or root == "/":
            self.session.message.emit("Укажите каталог SD, например /media/scope")
            return
        if self.session.busy or not self.session.connected:
            self.session.message.emit("Нет свободного соединения")
            return
        self.root_path = root
        self.tree.clear()
        item = QTreeWidgetItem([root, "Каталог", ""])
        item.setData(0, Qt.UserRole, root)
        item.setData(0, Qt.UserRole + 1, "d")
        self.tree.addTopLevelItem(item)
        self.load_directory(item)

    def expanded(self, item):
        if item.childCount() == 1 and item.child(0).data(0, Qt.UserRole) is None:
            self.load_directory(item)

    def load_directory(self, item):
        path = item.data(0, Qt.UserRole)
        def received(data):
            entries = parse_listing(data)
            item.takeChildren()
            for name, kind, size in entries:
                child = QTreeWidgetItem([name, {"d": "Каталог", "f": "Файл", "l": "Ссылка"}[kind], "" if kind == "d" else str(size)])
                child.setData(0, Qt.UserRole, posixpath.join(path, name))
                child.setData(0, Qt.UserRole + 1, kind)
                child.setData(0, Qt.UserRole + 2, size)
                if kind == "d":
                    child.addChild(QTreeWidgetItem(["Загрузить…"]))
                item.addChild(child)
            item.setExpanded(True)
            self.info.setText("{}: {} элементов".format(path, len(entries)))
        def failed(error):
            item.setExpanded(False)
            self.info.setText(error)
        self.session.request(list_command(self.root_path, path), received, failed)

    def start_download(self):
        item = self.tree.currentItem()
        if self.download or not item or item.data(0, Qt.UserRole + 1) != "f":
            return
        if self.session.busy or not self.session.connected:
            self.session.message.emit("Нет свободного соединения")
            return
        local, _ = QFileDialog.getSaveFileName(self, "Скачать файл с SD", item.text(0))
        if not local:
            return
        target = QSaveFile(local)
        if not target.open(QIODevice.WriteOnly):
            self.info.setText(target.errorString())
            return
        self.download = {"file": target, "path": item.data(0, Qt.UserRole),
                         "size": item.data(0, Qt.UserRole + 2), "received": 0, "block": 0, "local": local}
        self.cancelled = False
        self.cancel.setEnabled(True)
        self.save.setEnabled(False)
        self.next_chunk()

    def next_chunk(self):
        if self.cancelled:
            self.abort("Скачивание отменено")
            return
        state = self.download
        command = read_chunk_command(self.root_path, state["path"], state["block"])
        if not self.session.request(command, self.chunk_received, self.abort, timeout=90):
            self.abort("Не удалось начать чтение")

    def chunk_received(self, data):
        if not self.download:
            return
        if self.cancelled:
            self.abort("Скачивание отменено")
            return
        state = self.download
        if state["file"].write(data) != len(data):
            self.abort(state["file"].errorString())
            return
        state["received"] += len(data)
        self.info.setText("Скачано {} / {} байт".format(state["received"], state["size"]))
        if state["received"] > state["size"]:
            self.abort("Размер файла изменился. Обновите каталог и повторите чтение.")
        elif len(data) < 49152:
            if state["received"] != state["size"]:
                self.abort("Размер файла изменился или чтение прервано")
            elif state["file"].commit():
                self.info.setText("Файл сохранён: " + state["local"])
                self.reset_download()
            else:
                self.abort(state["file"].errorString())
        else:
            state["block"] += 1
            self.next_chunk()

    def reset_download(self):
        self.download = None
        self.cancel.setEnabled(False)
        self.save.setEnabled(True)

    def abort(self, reason):
        if self.download:
            self.download["file"].cancelWriting()
            self.reset_download()
        self.info.setText(reason)
