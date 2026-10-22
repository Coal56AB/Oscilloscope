"""Current-window CSV decoding and physical coordinates; independent of Qt."""
import csv
import json
import math
from dataclasses import dataclass


@dataclass
class WaveWindow:
    state: dict
    metadata: dict
    samples: list
    time_us: list
    volts: list
    csv: bytes


def parse_window(payload):
    state_bytes, separator, data = payload.partition(b"\n")
    if not separator:
        raise ValueError("Неполное окно осциллограммы")
    state = json.loads(state_bytes.decode("utf-8"))
    if state.get("version") != 1:
        raise ValueError("Несовместимая версия управления")
    metadata, lines = {}, []
    for line in data.decode("ascii").splitlines():
        if line.startswith("#"):
            key, _, value = line[1:].partition("=")
            metadata[key] = float(value)
        else:
            lines.append(line)
    reader = csv.reader(lines)
    if next(reader, None) != ["sample", "ch1_y", "ch2_y"]:
        raise ValueError("Неизвестный формат осциллограммы")
    samples = [[], []]
    for index, row in enumerate(reader):
        if len(row) != 3 or int(row[0]) != index:
            raise ValueError("Нарушен порядок отсчётов")
        for channel in range(2):
            value = int(row[channel + 1])
            if not -32768 <= value <= 32767:
                raise ValueError("Некорректный отсчёт")
            samples[channel].append(value)
    count = len(samples[0])
    if count != 1024:
        raise ValueError("Неполное текущее окно")
    pixels = float(state["pixels_per_div"])
    divisions = float(state["time_divisions"])
    step = metadata["time_us_per_div"] * divisions / count
    if not math.isfinite(step) or step <= 0 or not math.isfinite(pixels) or pixels <= 0:
        raise ValueError("Некорректный масштаб")
    times = [(index - metadata["trigger_marker_x"]) * step for index in range(count)]
    volts = []
    for channel in range(2):
        prefix = "ch{}_".format(channel + 1)
        gain = metadata[prefix + "mv_per_div"] / 1000 / pixels
        zero = metadata[prefix + "zero_y"]
        if not math.isfinite(gain) or gain <= 0 or not math.isfinite(zero):
            raise ValueError("Некорректный масштаб канала")
        volts.append([(zero - value) * gain for value in samples[channel]])
    return WaveWindow(state, metadata, samples, times, volts, data)


def format_time(us):
    if abs(us) < 1 and us:
        return "{:g} нс".format(us * 1000)
    if abs(us) < 1000:
        return "{:g} мкс".format(us)
    if abs(us) < 1000000:
        return "{:g} мс".format(us / 1000)
    return "{:g} с".format(us / 1000000)
