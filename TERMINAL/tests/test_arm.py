"""Run device commands in the project's ARM Linux/BusyBox under QEMU."""
import argparse
import gzip
import os
import selectors
import shutil
import stat
import subprocess
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from protocol import ShellReply, list_command, parse_listing, read_chunk_command
from capture import parse_window


def newc(files):
    archive = bytearray()
    for index, (name, mode, data) in enumerate(files + [("TRAILER!!!", 0, b"")], 1):
        encoded = name.encode() + b"\x00"
        fields = [index, mode, 0, 0, 1, 0, len(data), 0, 0, 0, 0, len(encoded), 0]
        archive.extend(b"070701" + "".join("{:08x}".format(value) for value in fields).encode())
        archive.extend(encoded)
        archive.extend(b"\x00" * (-len(archive) % 4))
        archive.extend(data)
        archive.extend(b"\x00" * (-len(archive) % 4))
    archive.extend(b"\x00" * (-len(archive) % 512))
    return archive


def run_boot(images, build, data_image):
    init = b"""#!/bin/sh
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev
mkdir -p /run /tmp /media/scope/subdir
/etc/init.d/S98oscill-card start
for attempt in 1 2 3 4 5 6 7 8 9 10; do
    grep -q ' /media/scope ' /proc/mounts && break
    sleep 1
done
grep -q ' /media/scope ' /proc/mounts || { echo DATA_MOUNT_FAILED; exec /bin/sh; }
mkdir -p /media/scope/subdir
printf 'ARM SD file\\n' > '/media/scope/name with spaces.csv'
ln -s / /media/escape
export SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software
/usr/bin/scope_preview --kiosk --remote-socket /run/oscill-control.sock --console /dev/ttyPS0 --data-dir /media/scope >/tmp/scope-log 2>&1 &
sleep 1
echo ARM_TERMINAL_READY
exec /bin/sh
"""
    with tempfile.TemporaryDirectory(prefix="oscill-arm-") as folder:
        root = Path(folder)
        files = [("test-init", stat.S_IFREG | 0o755, init)]
        if build is not None:
            files.extend([
                ("usr", stat.S_IFDIR | 0o755, b""), ("usr/bin", stat.S_IFDIR | 0o755, b""),
                ("usr/bin/scope_preview", stat.S_IFREG | 0o755, (build / "scope_preview").read_bytes()),
                ("usr/bin/oscill-remote", stat.S_IFREG | 0o755, (build / "oscill-remote").read_bytes())])
        overlay = newc(files)
        initrd = root / "test.cpio.gz"
        initrd.write_bytes(gzip.compress((images / "rootfs.cpio").read_bytes() + overlay))
        command = ["qemu-system-arm", "-M", "xilinx-zynq-a9", "-m", "512", "-display", "none", "-monitor", "none",
                   "-serial", "null", "-serial", "stdio", "-net", "none", "-kernel", str(images / "zImage"),
                   "-dtb", str(images / "oscill-zynq7020.dtb"), "-initrd", str(initrd),
                   "-drive", "file={},if=sd,index=1,format=raw".format(data_image),
                   "-append", "console=ttyPS0,115200 rdinit=/test-init panic=-1"]
        process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        selector = selectors.DefaultSelector()
        selector.register(process.stdout, selectors.EVENT_READ)

        def read(timeout):
            if not selector.select(timeout):
                raise TimeoutError("QEMU console timeout")
            data = os.read(process.stdout.fileno(), 65536)
            if not data:
                raise RuntimeError("QEMU exited")
            return data

        def request(command):
            reply = ShellReply()
            process.stdin.write(reply.command(command).encode())
            process.stdin.flush()
            deadline = time.monotonic() + 40
            while not reply.done and time.monotonic() < deadline:
                reply.feed(read(5))
            return reply.result()

        try:
            boot = bytearray()
            deadline = time.monotonic() + 60
            while b"ARM_TERMINAL_READY" not in boot and time.monotonic() < deadline:
                boot.extend(read(10))
            if b"ARM_TERMINAL_READY" not in boot:
                raise RuntimeError(boot.decode("utf-8", "replace")[-8000:])
            assert b"--remote-socket /run/oscill-control.sock" in request("cat /etc/init.d/S99oscill")
            assert b"Oscill data" in request("cat /media/scope/.oscill-data")
            request("oscill-card unmount && sleep 3 && ! grep -q ' /media/scope ' /proc/mounts")
            request("rm /run/oscill-card.disabled; oscill-card auto; grep -q ' /media/scope ' /proc/mounts")
            assert request("oscill-remote ping") == b"OSCILL REMOTE 1\n"
            payload = request("oscill-remote window")
            window = parse_window(payload)
            assert len(window.volts[0]) == 1024
            assert window.state["uart_baud"] == 115200
            assert window.state["pc_active"] == 1
            request("oscill-remote stop")
            before = request("oscill-remote csv")
            request("oscill-remote set time_index 4")
            after = request("oscill-remote csv")
            assert before.split(b"#time_index=")[1].splitlines()[0] != after.split(b"#time_index=")[1].splitlines()[0]
            entries = parse_listing(request(list_command("/media/scope", "/media/scope")))
            assert ("name with spaces.csv", "f", 12) in entries, entries
            assert request(read_chunk_command("/media/scope", "/media/scope/name with spaces.csv", 0)) == b"ARM SD file\n"
            try:
                request(list_command("/media/scope", "/media/escape"))
            except RuntimeError:
                pass
            else:
                raise AssertionError("SD root escape allowed")
            assert request("printf 'comments work' # trailing comment") == b"comments work"
            request("oscill-card unmount")
            print("ARM Linux/BusyBox: window, CSV, direct settings, SD listing/read and shell framing passed")
            print("Window {} bytes, gzip {} bytes".format(len(payload), len(gzip.compress(payload))))
        except Exception:
            if 'boot' in locals():
                print(boot.decode("utf-8", "replace")[-8000:], file=sys.stderr)
            raise
        finally:
            process.terminate()
            process.communicate(timeout=10)
            selector.close()


def run(images, build=None):
    with tempfile.TemporaryDirectory(prefix="oscill-card-") as folder:
        card = Path(folder) / "data.img"
        with card.open("wb") as stream:
            stream.truncate(16 * 1024 * 1024)
        subprocess.run(["/usr/sbin/mkdosfs", "-n", "OSCILL", str(card)], check=True)
        run_boot(images, build, card)
        # The second cold boot must use the remembered marker, without the label.
        subprocess.run(["mlabel", "-i", str(card), "-c", "::"], check=True)
        run_boot(images, build, card)
        print("Data card: label, remembered selection after cold reboot, unmount inhibition passed")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--images", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, help="Override GUI binaries; omit to test the built rootfs")
    args = parser.parse_args()
    run(args.images.resolve(), args.build_dir.resolve() if args.build_dir else None)
