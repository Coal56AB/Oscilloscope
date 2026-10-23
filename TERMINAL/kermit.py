"""U-Boot loadb sender: binary quoting, long packets, checksums and retry."""
import time


def check(data):
    total = sum(data)
    return ((total + ((total >> 6) & 3)) & 63) + 32


def quote(data):
    result = bytearray()
    for value in data:
        if value & 127 < 32 or value & 127 == 127:
            result.extend((35, value ^ 64))
        elif value == 35:
            result.extend((35, 35))
        else:
            result.append(value)
    return bytes(result)


def packet(sequence, kind, data):
    if len(data) <= 91:
        body = bytes((len(data) + 35, sequence + 32, ord(kind))) + data
    else:
        length = len(data) + 1
        if length > 9024:
            raise ValueError("Kermit packet too large")
        head = bytes((32, sequence + 32, ord(kind), length // 95 + 32, length % 95 + 32))
        body = head + bytes((check(head),)) + data
    return b"\x01" + body + bytes((check(body), 13))


def send(connection, data, progress):
    sequence = 0

    def exchange(kind, payload):
        nonlocal sequence
        raw = packet(sequence, kind, payload)
        for attempt in range(8):
            connection.write(raw)
            buffer = bytearray()
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline:
                buffer.extend(connection.read(max(1, min(connection.in_waiting, 65536))))
                start = buffer.find(b"\x01")
                if start < 0:
                    continue
                if start:
                    del buffer[:start]
                if len(buffer) < 2:
                    continue
                length = buffer[1] - 32
                if length < 3 or length > 94:
                    del buffer[0]
                    continue
                if len(buffer) < length + 3:
                    continue
                reply = bytes(buffer[:length + 3])
                del buffer[:length + 3]
                if reply[-1] != 13 or reply[-2] != check(reply[1:-2]):
                    continue
                if reply[2] != sequence + 32:
                    continue
                if reply[3] == ord('Y'):
                    sequence = (sequence + 1) % 64
                    return reply[4:-2]
                if reply[3] == ord('N'):
                    break
        raise TimeoutError("U-Boot не подтвердил пакет Kermit")

    parameters = bytes((126, 33, 32, 64, 45, 35, 78, 49, 78, 34, 32, 126, 126))
    response = exchange('S', parameters)
    if len(response) < 10 or not ((response[9] - 32) & 2):
        raise RuntimeError("U-Boot не поддерживает длинные пакеты Kermit")
    exchange('F', b'image.bin')
    for offset in range(0, len(data), 2048):
        chunk = data[offset:offset + 2048]
        exchange('D', quote(chunk))
        progress(min(offset + len(chunk), len(data)), len(data))
    exchange('Z', b'')
    exchange('B', b'')
