import struct
import sys
import tempfile
import unittest
import hashlib
from unittest.mock import Mock
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import firmware
import kermit


class FirmwareTests(unittest.TestCase):
    def test_reboot_wait_ignores_old_shell_prompt(self):
        class Connection:
            def __init__(self):
                self.chunks = [b'# ', b'Booting Linux\r\n', b'oscill login: ', b'root\r\n# ']
                self.writes = []
            @property
            def in_waiting(self):
                return len(self.chunks[0]) if self.chunks else 0
            def read(self, count):
                return self.chunks.pop(0)
            def write(self, data):
                self.writes.append(data)
        job = object.__new__(firmware.FirmwareJob)
        connection = Connection()
        job.wait_linux(connection)
        self.assertFalse(connection.chunks)
        self.assertEqual(connection.writes, [b'root\n'])

    def test_ram_boot_preserves_linux_arguments(self):
        from unittest.mock import patch
        data = b'fit image'
        crc = '{:08x}'.format(firmware.zlib.crc32(data) & 0xffffffff).encode()
        connection = Mock()
        connection.in_waiting = 1
        connection.read.side_effect = [b'Restarting system\nZynq>', b'...', b'Zynq>', crc, b'Zynq>']
        job = object.__new__(firmware.FirmwareJob)
        job.images = [('BOOT.BIN', b'boot'), ('image.bin', data)]
        job.wait_linux = Mock()
        shell = Mock(side_effect=[b'console=ttyPS0,1000000 rdinit=/sbin/init', b''])
        with patch('firmware.kermit.send'), patch('firmware.time.sleep'):
            job.bootstrap(connection, shell, Mock())
        writes = [call[0][0] for call in connection.write.call_args_list]
        self.assertIn(b"setenv bootargs 'console=ttyPS0,1000000 rdinit=/sbin/init oscill.update=1'\n", writes)
        self.assertEqual(writes[-1], b'bootm 0x10000000\n')
        self.assertEqual(connection.baudrate, 1000000)

    def test_flash_requires_complete_verified_uploads(self):
        for corrupt in (False, True):
            job = object.__new__(firmware.FirmwareJob)
            job.images = [('BOOT.BIN', b'boot' * 800), ('image.bin', b'fit\0' * 900)]
            job.wait_linux = Mock()
            commands = []
            def shell(connection, command, timeout):
                commands.append(command)
                if command.startswith('cat /sys/class/mtd'):
                    return b'0xc00'
                if command.startswith('sha256sum '):
                    name = command.rsplit('/', 1)[-1]
                    data = dict(job.images)[name]
                    return ('0' * 64 if corrupt else hashlib.sha256(data).hexdigest()).encode()
                if command.startswith('tail -n 3'):
                    return b'PROGRESS 100 COMPLETE\nRESULT 0\n'
                if command == 'cat /etc/oscill-versions':
                    return b'app=test\nfpga=test\n'
                return b''
            connection = Mock()
            progress = Mock()
            if corrupt:
                with self.assertRaises(ValueError):
                    job.run(connection, shell, progress)
                self.assertFalse(any('--flash ' in command for command in commands))
                self.assertIn('/usr/bin/oscill-update --status UPDATE_FAILED 0', commands)
                connection.write.assert_not_called()
            else:
                self.assertIn('fpga=test', job.run(connection, shell, progress))
                flash = next(i for i, command in enumerate(commands) if '--flash ' in command)
                self.assertEqual(sum(command.startswith('sha256sum ') for command in commands[:flash]), 2)
                self.assertLess(commands.index('/etc/init.d/S99oscill stop'), flash)
                self.assertFalse(any('BACKUP' in command or '/dev/mtd' in command for command in commands))
                self.assertIn(('Передача', 6800, 6800), [call[0][0] for call in progress.call_args_list])
                job.wait_linux.assert_called_once()

    def test_images_preserve_zero_tail_and_reject_bad_fit(self):
        boot = bytearray(256)
        struct.pack_into('<II', boot, 0x20, 0xaa995566, 0x584c4e58)
        struct.pack_into('<I', boot, 0x48, (~sum(struct.unpack_from('<10I', boot, 0x20))) & 0xffffffff)
        fit = struct.pack('>II', 0xd00dfeed, 64) + bytes(56)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / 'BOOT.BIN').write_bytes(boot)
            (path / 'image.bin').write_bytes(fit)
            images = firmware.FirmwareJob(path).images
            self.assertEqual(images[0][1], boot)
            self.assertEqual(images[1][1], fit)
            (path / 'image.bin').write_bytes(fit + b'\xff')
            with self.assertRaises(ValueError):
                firmware.read_images(path)

    def test_upload_shell_error_retries_only_same_block(self):
        for failures in (1, 4):
            job = object.__new__(firmware.FirmwareJob)
            job.images = [('BOOT.BIN', b'boot'), ('image.bin', b'fit')]
            job.wait_linux = Mock()
            writes, commands = [], []
            def transact(connection, command, timeout):
                commands.append(command)
                if command.startswith('cat /sys/class/mtd'):
                    return b'0xc00'
                if command.startswith('printf %s '):
                    writes.append(command)
                    if len(writes) <= failures:
                        raise RuntimeError('sh: syntax error: unterminated quoted string')
                if command.startswith('sha256sum '):
                    return hashlib.sha256(dict(job.images)[command.rsplit('/', 1)[-1]]).hexdigest().encode()
                if command.startswith('tail -n 3'):
                    return b'RESULT 0\n'
                return b''
            if failures == 4:
                with self.assertRaises(RuntimeError):
                    job.run(Mock(), transact, Mock())
                self.assertFalse(any('--flash ' in command for command in commands))
                self.assertEqual(len(writes), 4)
            else:
                job.run(Mock(), transact, Mock())
                self.assertEqual(writes[0], writes[1])
                self.assertEqual(sum('--flash ' in command for command in commands), 1)

    def test_binary_quoting(self):
        source = bytes(range(256))
        quoted = kermit.quote(source)
        recovered = bytearray()
        escape = False
        for value in quoted:
            if escape:
                recovered.append(value ^ 64 if value & 0x60 == 0x40 or value & 0x7f == 0x3f else value)
                escape = False
            elif value == 35:
                escape = True
            else:
                recovered.append(value)
        self.assertEqual(recovered, source)

    def test_kermit_long_packet_retry(self):
        class Receiver:
            def __init__(self):
                self.incoming = bytearray()
                self.failed = False
                self.frames = []
            @property
            def in_waiting(self):
                return len(self.incoming)
            def read(self, count):
                result = bytes(self.incoming[:count]); del self.incoming[:count]; return result
            def write(self, raw):
                self.frames.append(raw)
                self.assert_packet(raw)
                seq, kind = raw[2] - 32, chr(raw[3])
                if kind == 'D' and not self.failed:
                    self.failed = True
                    reply = kermit.packet(seq, 'N', b'')
                else:
                    reply = kermit.packet(seq, 'Y', b'~! @-#N1N"' if kind == 'S' else b'')
                self.incoming.extend(reply)
            def assert_packet(self, raw):
                assert raw[0] == 1 and raw[-1] == 13 and raw[-2] == kermit.check(raw[1:-2])
                if raw[1] == 32:
                    assert raw[6] == kermit.check(raw[1:6])
                    assert (raw[4]-32)*95+raw[5]-32 == len(raw)-8
        receiver = Receiver()
        updates = []
        kermit.send(receiver, bytes(range(256))*20, lambda done,total: updates.append((done,total)))
        self.assertTrue(receiver.failed)
        self.assertEqual(updates[-1], (5120,5120))
        data_frames = [raw for raw in receiver.frames if raw[3] == ord('D')]
        self.assertEqual(data_frames[0], data_frames[1])


if __name__ == '__main__':
    unittest.main()
