"""Framed shell replies over a shared Linux serial console; no Qt dependency."""
import base64
import gzip
import re
import shlex
import uuid


class ShellReply:
    LIMIT = 8 * 1024 * 1024

    def __init__(self, token=None):
        self.token = token or uuid.uuid4().hex
        self.begin = ("__OSC_BEGIN_" + self.token + "__").encode("ascii")
        self.end = ("__OSC_END_" + self.token + "__").encode("ascii")
        self.exit = ("\n__OSC_EXIT_" + self.token + "__:").encode("ascii")
        self.buffer = bytearray()
        self.lines = []
        self.started = False
        self.done = False
        self.received = 0

    def command(self, command):
        # Each delimiter is a complete line, unlike the echoed shell command.
        script = base64.b64encode(command.encode("utf-8", "surrogateescape")).decode("ascii")
        return ("printf '\\n%s\\n' '{begin}'; "
                "{{ printf %s '{script}' | base64 -d | sh 2>&1; rc=$?; printf '\\n%s:%s\\n' '{exit}' \"$rc\"; }} "
                "| gzip -c | base64; printf '\\n%s\\n' '{end}'\n").format(
                    begin=self.begin.decode(), end=self.end.decode(),
                    exit=self.exit.decode().lstrip("\n").rstrip(":"), script=script)

    def feed(self, data):
        """Return console bytes outside the reply, preserving boot/login output."""
        self.buffer.extend(data)
        if len(self.buffer) > self.LIMIT:
            raise ValueError("Слишком длинная строка ответа")
        console = bytearray()
        while b"\n" in self.buffer:
            line, _, rest = self.buffer.partition(b"\n")
            self.buffer = bytearray(rest)
            clean = bytes(line).rstrip(b"\r")
            if self.done:
                console.extend(line + b"\n")
            elif not self.started:
                if clean == self.begin:
                    self.started = True
                else:
                    console.extend(line + b"\n")
            elif clean == self.end:
                self.done = True
            elif clean:
                if len(clean) % 4 or not re.fullmatch(rb"[A-Za-z0-9+/]*={0,2}", clean):
                    console.extend(line + b"\n")
                    continue
                self.received += len(clean)
                if self.received > self.LIMIT:
                    raise ValueError("Ответ превышает допустимый размер")
                self.lines.append(clean)
        return bytes(console)

    def result(self):
        if not self.done:
            raise ValueError("Неполный ответ консоли")
        import io
        packed = base64.b64decode(b"".join(self.lines), validate=True)
        with gzip.GzipFile(fileobj=io.BytesIO(packed)) as stream:
            payload = stream.read(self.LIMIT + 1)
        if len(payload) > self.LIMIT:
            raise ValueError("Распакованный ответ слишком большой")
        body, marker, status = payload.rpartition(self.exit)
        if not marker or not status.strip().isdigit():
            raise ValueError("Нет статуса выполнения команды")
        if int(status.strip()):
            raise RuntimeError(body.decode("utf-8", "replace").strip() or
                               "Ошибка команды: " + status.decode().strip())
        return body


def list_command(root, path):
    """Names are base64 so spaces, tabs, newlines and shell syntax stay data."""
    return ("root=$(cd {root} && pwd -P) || exit; "
            "cd {path} || exit; here=$(pwd -P); "
            'case "$here/" in "$root/"*) ;; *) echo "Outside SD root"; exit 1;; esac; '
            'for f in .[!.]* ..?* *; do '
            '[ -e "$f" ] || [ -L "$f" ] || continue; '
            't=f; [ ! -d "$f" ] || t=d; [ ! -L "$f" ] || t=l; '
            's=$(ls -ldn -- "$f") || exit; set -f; set -- $s; set +f; s=$5; '
            'printf "%s\\t%s\\t" "$t" "$s"; printf %s "$f" | base64 | tr -d "\\n"; '
            "printf '\\n'; done").format(root=shlex.quote(root), path=shlex.quote(path))


def parse_listing(payload):
    entries = []
    for line in payload.splitlines():
        kind, size, name = line.split(b"\t", 2)
        decoded = base64.b64decode(name, validate=True).decode("utf-8", "surrogateescape")
        if kind not in (b"f", b"d", b"l") or not size.isdigit() or "/" in decoded or decoded in (".", ".."):
            raise ValueError("Некорректный ответ каталога")
        entries.append((decoded, kind.decode(), int(size)))
    return sorted(entries, key=lambda row: (row[1] != "d", row[0].casefold()))


def read_chunk_command(root, path, block):
    import posixpath
    parent, name = posixpath.split(path)
    return ("root=$(cd {root} && pwd -P) || exit; cd {parent} || exit; "
            'here=$(pwd -P); case "$here/" in "$root/"*) ;; *) exit 1;; esac; '
            "[ ! -L {name} ] && [ -f {name} ] || exit 1; "
            "dd if={name} bs=49152 skip={block} count=1").format(
                root=shlex.quote(root), parent=shlex.quote(parent),
                name=shlex.quote("./" + name), block=int(block)) + " 2>/dev/null"
