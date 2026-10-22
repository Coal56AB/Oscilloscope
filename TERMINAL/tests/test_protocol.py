import base64
import gzip
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from protocol import ShellReply, parse_listing, list_command, read_chunk_command


def response(reply, body, status=0):
    payload = body + reply.exit + str(status).encode() + b"\n"
    return b"boot\r\n" + reply.begin + b"\r\n" + base64.encodebytes(gzip.compress(payload)).replace(b"\n", b"\r\n") + reply.end + b"\r\n# "


class ProtocolTests(unittest.TestCase):
    def test_binary_fragmentation_echo_and_tail(self):
        reply = ShellReply("abc")
        raw = reply.command("true").encode() + response(reply, bytes(range(256)) * 100)
        console = bytearray()
        for byte in raw:
            console.extend(reply.feed(bytes([byte])))
        self.assertEqual(reply.result(), bytes(range(256)) * 100)
        self.assertIn(b"boot", console)
        self.assertIn(b"printf", console)
        self.assertEqual(bytes(reply.buffer), b"# ")

    def test_command_status_is_preserved(self):
        reply = ShellReply("abc")
        reply.feed(response(reply, b"No SD card", 1))
        with self.assertRaisesRegex(RuntimeError, "No SD card"):
            reply.result()

    def test_kernel_message_does_not_corrupt_shell_reply(self):
        reply = ShellReply("abc")
        raw = response(reply, b"window points")
        raw = raw.replace(reply.begin + b"\r\n", reply.begin + b"\r\n[ 12.345] FAT-fs: mounted\r\n")
        console = reply.feed(raw)
        self.assertEqual(reply.result(), b"window points")
        self.assertIn(b"FAT-fs: mounted", console)

    def test_corrupt_and_incomplete_replies(self):
        for data in [b"broken", b"AAAA"]:
            reply = ShellReply("abc")
            reply.feed(reply.begin + b"\n" + data + b"\n" + reply.end + b"\n")
            with self.assertRaises(Exception):
                reply.result()
        with self.assertRaises(ValueError):
            ShellReply().result()

    def test_listing_special_names(self):
        names = ["проба.csv", "a ' $(touch bad)\n.csv", "directory"]
        payload = b"".join(kind.encode() + b"\t123\t" + base64.b64encode(name.encode()) + b"\n"
                           for kind, name in zip(["f", "f", "d"], names))
        entries = parse_listing(payload)
        self.assertEqual(entries[0][0], "directory")
        self.assertEqual({entry[0] for entry in entries}, set(names))
        with self.assertRaises(ValueError):
            parse_listing(b"f\t0\t" + base64.b64encode(b"../escape"))

    def test_shell_commands_quote_paths(self):
        self.assertIn("'", list_command("/media/scope", "/media/scope/with space"))
        self.assertIn("skip=2 count=1", read_chunk_command("/media/scope", "/media/scope/a'$(x)", 2))


if __name__ == "__main__":
    unittest.main()
