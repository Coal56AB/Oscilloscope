import codecs
import re

from PySide2.QtCore import Qt, Signal
from PySide2.QtGui import QFont, QTextCursor
from PySide2.QtWidgets import QApplication, QWidget, QVBoxLayout, QHBoxLayout, QPlainTextEdit, QLabel, QComboBox, QPushButton, QMenu


class Console(QPlainTextEdit):
    send = Signal(bytes)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setReadOnly(True)
        self.setMaximumBlockCount(10000)
        self.setFont(QFont("Consolas", 11))
        self.setStyleSheet("QPlainTextEdit { background: #111820; color: #d9e3ef; padding: 8px; }")
        self.setPlaceholderText("Подключитесь к COM-порту. Вводите команды прямо здесь — ответы Linux появятся в этом же окне.")
        self.ending = b"\n"
        self.decoder = codecs.getincrementaldecoder("utf-8")("replace")
        self.escape = ""
        self.output_cursor = QTextCursor(self.document())
        self.input_line = ""
        self.input_position = 0
        self.history = []
        self.history_index = 0
        self.history_draft = ""
        self.pending_history = []

    def recall(self, direction):
        if not self.history:
            return
        if self.history_index == len(self.history):
            self.history_draft = self.input_line
        self.history_index = max(0, min(len(self.history), self.history_index + direction))
        self.input_line = self.history[self.history_index] if self.history_index < len(self.history) else self.history_draft
        self.input_position = len(self.input_line)
        self.send.emit(b"\x15" + self.input_line.encode("utf-8"))

    def keyPressEvent(self, event):
        modifiers = event.modifiers()
        if modifiers & Qt.ControlModifier:
            if event.key() == Qt.Key_V:
                self.paste_to_device()
            elif event.key() == Qt.Key_C and self.textCursor().hasSelection():
                self.copy()
            elif Qt.Key_A <= event.key() <= Qt.Key_Z:
                if event.key() in (Qt.Key_C, Qt.Key_U):
                    self.input_line = ""
                    self.input_position = 0
                self.send.emit(bytes([event.key() - Qt.Key_A + 1]))
            return
        if event.key() in (Qt.Key_Up, Qt.Key_Down):
            self.recall(-1 if event.key() == Qt.Key_Up else 1)
            return
        if event.key() in (Qt.Key_Return, Qt.Key_Enter):
            if self.input_line:
                self.pending_history.append(self.input_line)
            self.input_line = ""
            self.input_position = 0
        elif event.key() == Qt.Key_Backspace and self.input_position:
            self.input_line = self.input_line[:self.input_position - 1] + self.input_line[self.input_position:]
            self.input_position -= 1
        elif event.key() == Qt.Key_Left:
            self.input_position = max(0, self.input_position - 1)
        elif event.key() == Qt.Key_Right:
            self.input_position = min(len(self.input_line), self.input_position + 1)
        elif event.key() == Qt.Key_Home:
            self.input_position = 0
        elif event.key() == Qt.Key_End:
            self.input_position = len(self.input_line)
        elif event.key() == Qt.Key_Delete:
            self.input_line = self.input_line[:self.input_position] + self.input_line[self.input_position + 1:]
        keys = {Qt.Key_Return: self.ending, Qt.Key_Enter: self.ending, Qt.Key_Backspace: b"\x7f", Qt.Key_Tab: b"\t",
                Qt.Key_Up: b"\x1b[A", Qt.Key_Down: b"\x1b[B", Qt.Key_Right: b"\x1b[C", Qt.Key_Left: b"\x1b[D",
                Qt.Key_Home: b"\x1b[H", Qt.Key_End: b"\x1b[F", Qt.Key_Delete: b"\x1b[3~", Qt.Key_Escape: b"\x1b"}
        if event.key() in keys:
            self.send.emit(keys[event.key()])
        elif event.text():
            self.input_line = self.input_line[:self.input_position] + event.text() + self.input_line[self.input_position:]
            self.input_position += len(event.text())
            self.send.emit(event.text().encode("utf-8"))

    def paste_to_device(self):
        text = QApplication.clipboard().text().replace("\r\n", "\n")
        if "\n" in text:
            self.input_line = text.rsplit("\n", 1)[-1]
            self.input_position = len(self.input_line)
        else:
            self.input_line = self.input_line[:self.input_position] + text + self.input_line[self.input_position:]
            self.input_position += len(text)
        self.send.emit(text.encode("utf-8"))

    def contextMenuEvent(self, event):
        menu = QMenu(self)
        menu.addAction("Копировать", self.copy)
        menu.addAction("Вставить в терминал", self.paste_to_device)
        menu.addAction("Выделить всё", self.selectAll)
        menu.addAction("Очистить экран", self.clear)
        menu.exec_(event.globalPos())

    def clear(self):
        super().clear()
        self.output_cursor = QTextCursor(self.document())

    def append_bytes(self, data):
        text = self.decoder.decode(data)
        cursor = self.output_cursor
        for character in text:
            if self.escape or character == "\x1b":
                self.escape += character
                if self.escape == "\x1b" or self.escape == "\x1b[":
                    continue
                if self.escape.startswith("\x1b[") and not ("@" <= character <= "~"):
                    continue
                match = re.fullmatch(r"\x1b\[([0-9;?]*)([@-~])", self.escape)
                if match:
                    parameters, final = match.groups()
                    number = int(parameters or "1") if parameters.isdigit() or not parameters else 1
                    if final == "K":
                        cursor.movePosition(QTextCursor.EndOfBlock, QTextCursor.KeepAnchor)
                        cursor.removeSelectedText()
                    elif final == "D":
                        cursor.movePosition(QTextCursor.Left, QTextCursor.MoveAnchor, max(1, number))
                    elif final == "C":
                        cursor.movePosition(QTextCursor.Right, QTextCursor.MoveAnchor, max(1, number))
                    elif final == "J" and parameters == "2":
                        self.clear()
                        cursor = self.output_cursor
                self.escape = ""
                continue
            if character == "\r":
                cursor.movePosition(QTextCursor.StartOfBlock)
            elif character == "\n":
                # Remember after the device echo arrives, including delayed UART echo.
                if self.pending_history:
                    command = self.pending_history.pop(0)
                    if cursor.block().text().endswith(command):
                        if not self.history or self.history[-1] != command:
                            self.history.append(command)
                            self.history = self.history[-100:]
                        self.history_index = len(self.history)
                cursor.movePosition(QTextCursor.EndOfBlock)
                cursor.insertText("\n")
            elif character == "\b":
                cursor.movePosition(QTextCursor.Left)
            elif character >= " " or character == "\t":
                if not cursor.atBlockEnd():
                    cursor.movePosition(QTextCursor.Right, QTextCursor.KeepAnchor)
                cursor.insertText(character)
        self.output_cursor = cursor
        self.setTextCursor(cursor)
        self.ensureCursorVisible()


class TerminalTab(QWidget):
    def __init__(self, session, parent=None):
        super().__init__(parent)
        layout = QVBoxLayout(self)
        row = QHBoxLayout()
        row.addWidget(QLabel("Ввод прямо здесь. Enter — отправить, Ctrl+C — прервать, ↑/↓ — ваши команды, Ctrl+V — вставить."))
        row.addStretch()
        self.ending = QComboBox()
        self.ending.addItems(["LF", "CR", "CRLF"])
        row.addWidget(self.ending)
        clear = QPushButton("Очистить")
        row.addWidget(clear)
        layout.addLayout(row)
        self.console = Console()
        layout.addWidget(self.console, 1)
        self.console.send.connect(session.send)
        session.console.connect(self.console.append_bytes)
        session.changed.connect(lambda connected: self.console.decoder.reset())
        clear.clicked.connect(self.console.clear)
        self.ending.currentTextChanged.connect(lambda value: setattr(self.console, "ending", {"LF": b"\n", "CR": b"\r", "CRLF": b"\r\n"}[value]))
