import json
import math
from datetime import datetime

from PySide2.QtCore import Qt, QTimer, QRectF, QPointF
from PySide2.QtGui import QColor, QPainter, QPen, QPainterPath
from PySide2.QtWidgets import QWidget, QVBoxLayout, QHBoxLayout, QFormLayout, QGroupBox, QScrollArea, QLabel, QPushButton, QDoubleSpinBox, QComboBox, QCheckBox, QFileDialog

from capture import parse_window, format_time


class WavePlot(QWidget):
    COLORS = [QColor("#f6c647"), QColor("#4dd5ed")]

    def __init__(self, parent=None):
        super().__init__(parent)
        self.window = None
        self.setMinimumSize(500, 350)

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.fillRect(self.rect(), QColor("#101822"))
        area = QRectF(72, 35, max(1, self.width() - 144), max(1, self.height() - 95))
        painter.setPen(QColor("#29404f"))
        for column in range(11):
            x = area.left() + area.width() * column / 10
            painter.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()))
        for row in range(9):
            y = area.top() + area.height() * row / 8
            painter.drawLine(QPointF(area.left(), y), QPointF(area.right(), y))
        if self.window is None:
            painter.setPen(Qt.white)
            painter.drawText(area, Qt.AlignCenter, "Считайте текущее окно с осциллографа")
            return
        window = self.window
        height = window.state["plot_height"]
        for channel in range(2):
            prefix = "ch{}_".format(channel + 1)
            if not window.metadata[prefix + "enabled"]:
                continue
            painter.setPen(self.COLORS[channel])
            painter.drawText(QRectF(0 if channel == 0 else area.right() + 5, 8, 70, 20), Qt.AlignCenter, "CH{} · В".format(channel + 1))
            zero = window.metadata[prefix + "zero_y"]
            gain = window.metadata[prefix + "mv_per_div"] / 1000 / window.state["pixels_per_div"]
            for row in range(9):
                voltage = (zero - height * row / 8) * gain
                label = QRectF(0 if channel == 0 else area.right() + 5, area.top() + row * area.height() / 8 - 9, 66, 20)
                painter.drawText(label, Qt.AlignRight if channel == 0 else Qt.AlignLeft, "{:.3g}".format(voltage))
            path = QPainterPath()
            for index, sample in enumerate(window.samples[channel]):
                point = QPointF(area.left() + index * area.width() / (len(window.time_us) - 1), area.top() + sample * area.height() / height)
                if index:
                    path.lineTo(point)
                else:
                    path.moveTo(point)
            painter.save()
            painter.setClipRect(area)
            painter.setPen(QPen(self.COLORS[channel], 1.3))
            painter.drawPath(path)
            painter.restore()
        painter.setPen(QColor("#dce7ed"))
        for column in range(6):
            index = round((len(window.time_us) - 1) * column / 5)
            painter.drawText(QRectF(area.left() + area.width() * column / 5 - 45, area.bottom() + 10, 90, 24), Qt.AlignCenter, format_time(window.time_us[index]))
        painter.drawText(QRectF(area.left(), area.bottom() + 35, area.width(), 20), Qt.AlignCenter, "Время относительно триггера")


class ScopeTab(QWidget):
    def __init__(self, session, parent=None):
        super().__init__(parent)
        self.session = session
        self.state = None
        self.window = None
        self.fields = {}
        self.timer = QTimer(self)
        self.timer.setSingleShot(True)
        self.timer.timeout.connect(self.refresh)
        layout = QVBoxLayout(self)
        row = QHBoxLayout()
        for title, callback in [("Считать настройки", self.read_state), ("Считать окно", self.refresh), ("Сохранить CSV", self.save_csv)]:
            button = QPushButton(title)
            button.clicked.connect(callback)
            row.addWidget(button)
        self.apply_button = QPushButton("Применить изменения")
        self.apply_button.clicked.connect(self.apply)
        self.apply_button.setEnabled(False)
        row.addWidget(self.apply_button)
        self.auto = QCheckBox("Обновлять график")
        self.auto.toggled.connect(self.schedule)
        row.addWidget(self.auto)
        self.period = QDoubleSpinBox()
        self.period.setRange(0.5, 3600)
        self.period.setValue(5)
        self.period.setSuffix(" с")
        row.addWidget(self.period)
        row.addStretch()
        layout.addLayout(row)
        row = QHBoxLayout()
        for title, command in [("Запустить", "run"), ("Остановить", "stop"), ("Одиночный захват", "single"), ("Принудительный запуск", "force")]:
            button = QPushButton(title)
            button.clicked.connect(lambda checked=False, command=command: self.execute(command))
            row.addWidget(button)
        self.status = QLabel("Настройки ещё не считаны")
        row.addWidget(self.status, 1)
        layout.addLayout(row)
        body = QHBoxLayout()
        left = QVBoxLayout()
        self.plot = WavePlot()
        left.addWidget(self.plot, 1)
        self.measurements = QLabel("CH1 —     CH2 —")
        left.addWidget(self.measurements)
        self.info = QLabel("Подключитесь к Linux и считайте настройки устройства")
        self.info.setWordWrap(True)
        left.addWidget(self.info)
        body.addLayout(left, 1)
        self.controls = QWidget()
        panels = QVBoxLayout(self.controls)
        panels.setContentsMargins(0, 0, 0, 0)
        channels = QHBoxLayout()
        for channel in (1, 2):
            group = QGroupBox("Канал {}".format(channel))
            form = QFormLayout(group)
            prefix = "ch{}_".format(channel)
            self.add_check(form, "Включён", prefix + "enabled")
            self.add_combo(form, "В/дел", prefix + "scale_index", [])
            self.add_combo(form, "Связь", prefix + "coupling", ["DC", "AC"])
            self.add_combo(form, "Щуп", prefix + "probe_ten", ["×1", "×10"])
            self.fields[prefix + "probe_ten"][0].currentIndexChanged.connect(lambda value, channel=channel: self.update_scales(channel))
            self.add_number(form, "Смещение, дел", prefix + "position", -3, 3, factor=60)
            self.add_combo(form, "Фильтр", prefix + "filter", ["Выключен", "Слабый", "Средний", "Сильный"])
            channels.addWidget(group)
        panels.addLayout(channels)
        settings = QHBoxLayout()
        group = QGroupBox("Развёртка")
        form = QFormLayout(group)
        self.add_combo(form, "Время/дел", "time_index", [])
        self.add_number(form, "Смещение, дел", "time_position", -4.1, 4.1, factor=102.4, decimals=3)
        settings.addWidget(group)
        group = QGroupBox("Триггер")
        form = QFormLayout(group)
        self.add_combo(form, "Режим", "trigger_mode", ["AUTO", "NORMAL", "SINGLE"])
        self.add_combo(form, "Источник", "trigger_source", ["CH1", "CH2"])
        self.add_combo(form, "Фронт", "trigger_edge", ["Нарастающий", "Спадающий", "Оба"])
        self.add_number(form, "Уровень, В", "trigger_level", -100, 100, decimals=4)
        self.add_combo(form, "Holdoff", "trigger_holdoff", ["Выключен", "10 мкс", "50 мкс", "100 мкс"])
        settings.addWidget(group)
        panels.addLayout(settings)
        self.draft = QLabel("Измените нужные параметры и нажмите «Применить»")
        self.draft.setWordWrap(True)
        panels.addWidget(self.draft)
        panels.addStretch()
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setWidget(self.controls)
        scroll.setMinimumWidth(500)
        scroll.setMaximumWidth(560)
        body.addWidget(scroll)
        layout.addLayout(body, 1)
        self.controls.setEnabled(False)
        session.changed.connect(self.connection_changed)

    def add_check(self, form, label, key):
        widget = QCheckBox()
        self.fields[key] = (widget, 1)
        form.addRow(label, widget)

    def add_combo(self, form, label, key, options):
        widget = QComboBox()
        widget.addItems(options)
        self.fields[key] = (widget, 1)
        form.addRow(label, widget)

    def add_number(self, form, label, key, low, high, factor=1, decimals=2):
        widget = QDoubleSpinBox()
        widget.setRange(low, high)
        widget.setDecimals(decimals)
        widget.setSingleStep(0.1 if key != "trigger_level" else 0.01)
        self.fields[key] = (widget, factor)
        form.addRow(label, widget)

    def values(self):
        values = {}
        for key, (widget, factor) in self.fields.items():
            if isinstance(widget, QComboBox):
                values[key] = widget.currentIndex()
            elif isinstance(widget, QCheckBox):
                values[key] = int(widget.isChecked())
            else:
                values[key] = round(widget.value() * factor) if factor != 1 else widget.value()
        return values

    def changed_values(self):
        return {key: value for key, value in self.values().items() if self.state is not None and not math.isclose(value, self.state[key], abs_tol=0.00011)}

    def update_scales(self, channel):
        if self.state is None:
            return
        prefix = "ch{}_".format(channel)
        factor = 10 if self.fields[prefix + "probe_ten"][0].currentIndex() else 1
        widget = self.fields[prefix + "scale_index"][0]
        for index, mv in enumerate(self.state["channel_scales_mv"]):
            widget.setItemText(index, "{:g} В".format(mv * factor / 1000))

    def load_state(self, state, force=False, draft=None):
        if state.get("version") != 1:
            raise ValueError("Несовместимая версия устройства")
        if draft is None:
            draft = self.changed_values() if self.state is not None and not force else {}
        self.state = state
        for channel in (1, 2):
            key = "ch{}_scale_index".format(channel)
            widget = self.fields[key][0]
            if not widget.count():
                widget.addItems(["{:g} В".format(mv / 1000) for mv in state["channel_scales_mv"]])
        widget = self.fields["time_index"][0]
        if not widget.count():
            widget.addItems([format_time(value) for value in state["time_scales_us"]])
        self.fields["time_position"][0].setEnabled(not state["zoom"])
        for key, (widget, factor) in self.fields.items():
            value = draft.get(key, state[key])
            widget.blockSignals(True)
            if isinstance(widget, QComboBox):
                widget.setCurrentIndex(int(value))
            elif isinstance(widget, QCheckBox):
                widget.setChecked(bool(value))
            else:
                widget.setValue(value / factor)
            widget.blockSignals(False)
        self.controls.setEnabled(self.session.connected)
        self.apply_button.setEnabled(self.session.connected)
        for channel in (1, 2):
            self.update_scales(channel)
        self.status.setText(("RUN" if state["running"] else "STOP") + (" · ожидание триггера" if state["waiting"] else "") + " · источник: " + state["source"])

    def connection_changed(self, connected):
        self.controls.setEnabled(connected and self.state is not None)
        self.apply_button.setEnabled(connected and self.state is not None)
        if not connected:
            self.auto.setChecked(False)
            self.timer.stop()
            self.state = None
            self.status.setText("Отключено")

    def schedule(self, checked=None):
        self.timer.stop()
        if self.auto.isChecked() and self.session.connected:
            self.timer.start(int(self.period.value() * 1000))

    def read_state(self):
        self.session.request("oscill-remote state", lambda data: self.load_state(json.loads(data.decode("utf-8")), force=True), self.error)

    def refresh(self):
        if self.session.busy or self.session.input_pending:
            if self.auto.isChecked():
                self.timer.start(250)
            else:
                self.session.message.emit("Дождитесь операции или завершите строку терминала: Enter / Ctrl+C")
            return
        self.timer.stop()
        self.session.request("oscill-remote window", self.received, self.error, timeout=60)

    def received(self, data):
        window = parse_window(data)
        self.window = window
        self.plot.window = window
        self.plot.update()
        self.load_state(window.state)
        values = []
        for channel, volts in enumerate(window.volts, 1):
            rms = math.sqrt(sum(value * value for value in volts) / len(volts))
            values.append("CH{}: Vpp {:.4g} В · RMS {:.4g} В".format(channel, max(volts) - min(volts), rms))
        self.measurements.setText("    |    ".join(values))
        self.info.setText("Текущее окно: {} точек/канал · {}".format(len(window.time_us), datetime.now().strftime("%H:%M:%S")))
        self.schedule()

    def error(self, error):
        self.info.setText(error)
        self.auto.setChecked(False)

    def execute(self, command):
        self.session.request("oscill-remote " + command, lambda data: self.refresh(), self.error, defer=True)

    def apply(self):
        changes = self.changed_values()
        if not changes:
            self.info.setText("Изменений нет")
            return
        ordered = [key for key in changes if key != "trigger_level"] + (["trigger_level"] if "trigger_level" in changes else [])
        snapshot = self.values()
        command = " && ".join("oscill-remote set {} {:.9g}".format(key, changes[key]) for key in ordered)
        command += " && oscill-remote state"
        def complete(data):
            state = json.loads(data.rsplit(b"\n", 2)[-2].decode("utf-8"))
            draft = {key: value for key, value in self.values().items()
                     if not math.isclose(value, snapshot[key], abs_tol=0.00011)}
            self.load_state(state, draft=draft)
            self.refresh()
        if self.session.request(command, complete, lambda error: self.error("Не все изменения применены: " + error), timeout=60, defer=True):
            self.info.setText("Применение запомнено; ожидаю подтверждение устройства")

    def save_csv(self):
        if self.window is None:
            self.info.setText("Сначала считайте окно")
            return
        path, _ = QFileDialog.getSaveFileName(self, "Сохранить текущее окно", "oscill.csv", "CSV (*.csv)")
        if path:
            try:
                with open(path, "wb") as stream:
                    stream.write(self.window.csv)
                self.session.message.emit("CSV сохранён: " + path)
            except OSError as error:
                self.error(str(error))
