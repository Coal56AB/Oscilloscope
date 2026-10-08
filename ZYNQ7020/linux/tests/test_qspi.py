"""Exercise flash budget boundaries using temporary image fixtures."""
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

CHECK = Path(__file__).resolve().parents[1] / 'check_qspi.py'


class FlashBudgetTest(unittest.TestCase):
    def run_budget(self, sizes, *options):
        with tempfile.TemporaryDirectory() as directory:
            paths = []
            for index, size in enumerate(sizes):
                path = Path(directory) / str(index)
                with path.open('wb') as image:
                    image.truncate(size)
                paths.append(str(path))
            return subprocess.run([sys.executable, str(CHECK), *options, *paths],
                                  capture_output=True, text=True)

    def test_exact_fit_and_one_byte_over(self):
        sizes = [0x500000, 0x9e0000]
        self.assertEqual(self.run_budget(sizes).returncode, 0)
        sizes[-1] += 1
        self.assertEqual(self.run_budget(sizes).returncode, 1)

    def test_boot_partition_overflow(self):
        self.assertEqual(self.run_budget([0x500001, 1]).returncode, 1)

    def test_empty_image_rejected(self):
        self.assertEqual(self.run_budget([1, 0]).returncode, 1)


if __name__ == '__main__':
    unittest.main()
