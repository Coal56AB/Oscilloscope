"""Firmware update orchestration; widgets and serial ownership stay outside."""
import base64
import hashlib
import re
import shlex
import struct
import time
import zlib
from pathlib import Path

import kermit


def read_images(directory):
    directory = Path(directory)
    boot = (directory / 'BOOT.BIN').read_bytes()
    fit = (directory / 'image.bin').read_bytes()
    if not 160 <= len(boot) <= 0x500000:
        raise ValueError('Недопустимый размер BOOT.BIN')
    words = struct.unpack_from('<11I', boot, 0x20)
    if words[:2] != (0xaa995566, 0x584c4e58) or sum(words) & 0xffffffff != 0xffffffff:
        raise ValueError('Повреждён заголовок BOOT.BIN')
    offset, length = struct.unpack_from('<II', boot, 0x30)
    if offset + length > len(boot):
        raise ValueError('Неполный FSBL')
    if not 40 <= len(fit) <= 0x9e0000 or struct.unpack_from('>II', fit) != (0xd00dfeed, len(fit)):
        raise ValueError('Повреждён или неполон image.bin')
    # Native images have their real length. Never strip legitimate zero bytes.
    return [('BOOT.BIN', boot), ('image.bin', fit)]


class FirmwareJob:
    def __init__(self, directory):
        self.images = read_images(directory)

    def run(self, connection, transact, report):
        def shell(command, timeout=60, retries=1):
            for attempt in range(retries):
                try:
                    return transact(connection, command, timeout)
                except (ValueError, zlib.error, EOFError, RuntimeError):
                    # Only callers passing retries>1 are repeatable reads or
                    # writes at a fixed offset. UART loss can also corrupt the
                    # decoded shell script, producing a nonzero exit status.
                    if attempt + 1 == retries:
                        raise

        def status(phase, done, total):
            report((phase, done, total))

        shell('stty -F /dev/ttyPS0 -echo -ixon -ixoff')
        try:
            flags = int(shell('cat /sys/class/mtd/mtd0/flags').strip(), 16)
            if not flags & 0x400:
                self.bootstrap(connection, shell, status)
            shell('stty -F /dev/ttyPS0 -echo -ixon -ixoff')
            try:
                shell('/etc/init.d/S99oscill stop')
            except RuntimeError:
                # An old DMA driver can remain in capture close for minutes.
                # Use the same verified RAM boot without writing flash first.
                self.bootstrap(connection, shell, status)
                shell('stty -F /dev/ttyPS0 -echo -ixon -ixoff && /etc/init.d/S99oscill stop')
            shell('mkdir -p /tmp/oscill-update')
            shell('test -x /usr/bin/oscill-update; /usr/bin/oscill-update --status PREPARING 0')
            total = sum(len(data) for _, data in self.images)
            sent = 0
            last_percent = -1
            for name, data in self.images:
                target = '/tmp/oscill-update/' + name
                shell(': > ' + shlex.quote(target))
                for offset in range(0, len(data), 1536):
                    chunk = data[offset:offset + 1536]
                    encoded = base64.b64encode(chunk).decode('ascii')
                    command = "printf %s '{}' | base64 -d | dd of={} bs=1536 seek={} conv=notrunc 2>/dev/null".format(
                        encoded, shlex.quote(target), offset // 1536)
                    shell(command, retries=4)
                    sent += len(chunk)
                    percent = sent * 100 // total
                    status('Передача', sent, total)
                    if percent != last_percent:
                        shell('/usr/bin/oscill-update --status RECEIVING {}'.format(percent))
                        last_percent = percent
                expected = hashlib.sha256(data).hexdigest()
                actual = shell('sha256sum ' + shlex.quote(target)).split()[0].decode()
                if actual != expected:
                    raise ValueError('Контрольная сумма {} не совпала; flash не изменена'.format(name))
            boot_crc, fit_crc = [zlib.crc32(data) & 0xffffffff for _, data in self.images]
            command = ('rm -f /run/oscill-update.result; '
                       '( /usr/bin/oscill-update --flash /tmp/oscill-update/BOOT.BIN '
                       '/tmp/oscill-update/image.bin {:08x} {:08x} > /run/oscill-update.log 2>&1; '
                       'echo $? > /run/oscill-update.result ) </dev/null >/dev/null 2>&1 &').format(boot_crc, fit_crc)
            shell(command)
            deadline = time.monotonic() + 300
            while time.monotonic() < deadline:
                output = shell('tail -n 3 /run/oscill-update.log; '
                               'if test -f /run/oscill-update.result; then '
                               'printf "RESULT "; cat /run/oscill-update.result; fi').decode('utf-8', 'replace')
                matches = re.findall(r'PROGRESS (\d+) ', output)
                if matches:
                    status('Запись и проверка', int(matches[-1]), 100)
                result = re.search(r'RESULT (\d+)', output)
                if result:
                    if result[1] != '0':
                        raise RuntimeError('Ошибка записи: ' + output)
                    break
                time.sleep(0.3)
            else:
                raise TimeoutError('Нет результата записи. Не отключайте питание; проверьте /run/oscill-update.log')
            status('Готово. Перезагрузка', 100, 100)
            shell('sync')
            connection.write(b'reboot\n')
            connection.flush()
            connection.baudrate = 1000000
            self.wait_linux(connection)
            version = shell('cat /etc/oscill-versions').decode().strip()
            return 'Прошивка записана и проверена.\n{}'.format(version)
        except Exception:
            if getattr(self, 'in_linux', True):
                try:
                    shell('/usr/bin/oscill-update --status UPDATE_FAILED 0', timeout=5)
                except Exception:
                    pass
            raise
        finally:
            # Do not inject shell text into a bootloader binary receiver on failure.
            if getattr(self, 'in_linux', True):
                try:
                    shell('stty -F /dev/ttyPS0 echo', timeout=5)
                except Exception:
                    pass

    def wait_linux(self, connection):
        buffer = bytearray()
        login_sent = False
        deadline = time.monotonic() + 90
        while time.monotonic() < deadline:
            buffer.extend(connection.read(max(1, min(connection.in_waiting, 65536))))
            if re.search(rb'login: ?$', buffer):
                connection.write(b'root\n'); buffer.clear(); login_sent = True
            elif login_sent and re.search(rb'(?:^|[\r\n])# $', buffer):
                self.in_linux = True
                return
            buffer = buffer[-4096:]
        raise TimeoutError('Linux не вернулся после загрузки: ' + buffer.decode('utf-8', 'replace'))

    def bootstrap(self, connection, shell, progress):
        # First update of an old read-only MTD image: RAM boot, no flash writes.
        progress('Загрузка режима обновления в RAM', 0, 100)
        bootargs = shell('cat /proc/cmdline').decode('utf-8').strip()
        bootargs = ' '.join(arg for arg in bootargs.split() if arg != 'oscill.update=1')
        bootargs += ' oscill.update=1'
        shell('sync')
        connection.write(b'reboot -f\n')
        connection.flush()
        connection.baudrate = 1000000
        self.in_linux = False
        buffer = bytearray()
        deadline = time.monotonic() + 45
        rebooting = False
        while time.monotonic() < deadline:
            chunk = connection.read(max(1, min(connection.in_waiting, 65536)))
            buffer.extend(chunk)
            if b'Restarting system' in buffer or b'Xilinx First Stage' in buffer:
                rebooting = True
            if rebooting:
                connection.write(b'\x03')
            if re.search(rb'(?:Zynq>|=>) ?', buffer[-100:]):
                break
            buffer = buffer[-8192:]
        else:
            raise TimeoutError('Не удалось остановить U-Boot')
        time.sleep(0.2)
        connection.reset_input_buffer()

        def boot_command(command, expected, timeout=15):
            connection.write(command.encode() + b'\n')
            reply = bytearray()
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                reply.extend(connection.read(max(1, min(connection.in_waiting, 65536))))
                if expected in reply:
                    return bytes(reply)
            raise TimeoutError('U-Boot: ' + command)

        data = self.images[1][1]
        boot_command('loadb 0x10000000', b'...')
        kermit.send(connection, data, lambda done, total: progress('Режим обновления в RAM', done, total))
        time.sleep(0.3)
        boot_command('', b'>')
        expected_crc = '{:08x}'.format(zlib.crc32(data) & 0xffffffff).encode()
        reply = boot_command('crc32 0x10000000 {:x}'.format(len(data)), expected_crc)
        if expected_crc not in reply:
            raise ValueError('Контрольная сумма RAM-образа не совпала')
        boot_command('setenv bootargs ' + shlex.quote(bootargs), b'>')
        connection.write(b'bootm 0x10000000\n')
        self.wait_linux(connection)
