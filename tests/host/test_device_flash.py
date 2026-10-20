"""Offline tests for release segment validation. No serial/device calls execute."""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("device_flash", ROOT / "scripts" / "device_flash.py")
DEVICE_FLASH = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(DEVICE_FLASH)


def partition(kind, subtype, offset, size, label):
    return struct.pack("<HBBII16sI", 0x50AA, kind, subtype, offset, size, label.encode(), 0)


class ReleaseSegmentsTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="esp-hack-release-validation-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.manifest_path = self.root / "release.json"
        self.manifest = {"chip": "esp32", "display": "SH1106", "segments": []}
        self.add_binary(0x1000, "bootloader.bin", b"B" * 17536)
        self.add_binary(0x8000, "partitions.bin", self.partition_table())
        self.add_binary(0xE000, "boot_app0.bin", b"O" * 8192)
        self.add_binary(0x10000, "firmware.bin", b"A" * 65536)

    def partition_table(self, app_size=0x1E0000, subtype=0x10, offset=0x10000):
        # The real project uses OTA0 subtype 0x10, not factory subtype 0.
        table = partition(1, 2, 0x9000, 0x5000, "nvs")
        table += partition(1, 0, 0xE000, 0x2000, "otadata")
        table += partition(0, subtype, offset, app_size, "app0")
        return table.ljust(3072, b"\xff")

    def add_binary(self, offset, filename, data):
        path = self.root / filename
        path.write_bytes(data)
        self.manifest["segments"].append({"offset": hex(offset), "file": filename,
                                         "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()})

    def replace_binary(self, offset, data):
        segment = next(item for item in self.manifest["segments"] if int(item["offset"], 0) == offset)
        (self.root / segment["file"]).write_bytes(data)
        segment["bytes"] = len(data)
        segment["sha256"] = hashlib.sha256(data).hexdigest()

    def validate(self, flash_bytes=4194304):
        self.manifest_path.write_text(json.dumps(self.manifest), encoding="utf-8")
        return DEVICE_FLASH.release_segments(self.manifest_path, flash_bytes)

    def test_valid_ota0_segments_are_canonical_and_sorted(self):
        self.manifest["segments"].reverse()
        actual = self.validate()
        self.assertEqual(actual[0::2], ["0x1000", "0x8000", "0xe000", "0x10000"])
        self.assertTrue(all(Path(path).is_absolute() for path in actual[1::2]))
        self.assertEqual(Path(actual[-1]), self.root / "firmware.bin")

    def test_duplicate_offset_is_rejected(self):
        self.manifest["segments"][3] = self.manifest["segments"][0].copy()
        with self.assertRaises(RuntimeError): self.validate()

    def test_missing_segment_is_rejected(self):
        self.manifest["segments"].pop()
        with self.assertRaises(RuntimeError): self.validate()

    def test_nvs_offset_is_rejected(self):
        self.manifest["segments"][0]["offset"] = "0x9000"
        with self.assertRaises(RuntimeError): self.validate()

    def test_oversized_bootloader_cannot_cross_partition_or_nvs(self):
        self.replace_binary(0x1000, b"B" * 0x7001)
        with self.assertRaises(RuntimeError): self.validate()

    def test_oversized_partition_table_cannot_erase_nvs(self):
        self.replace_binary(0x8000, self.partition_table() + b"P" * 1025)
        with self.assertRaises(RuntimeError): self.validate()

    def test_oversized_ota_data_cannot_cross_application(self):
        self.replace_binary(0xE000, b"O" * 0x2001)
        with self.assertRaises(RuntimeError): self.validate()

    def test_oversized_application_cannot_cross_app0_limit(self):
        self.replace_binary(0x10000, b"A" * 0x1E0001)
        with self.assertRaises(RuntimeError): self.validate()

    def test_empty_artifact_is_rejected(self):
        self.replace_binary(0x1000, b"")
        with self.assertRaises(RuntimeError): self.validate()

    def test_corrupt_artifact_is_rejected(self):
        (self.root / "firmware.bin").write_bytes(b"X" * 65536)
        with self.assertRaises(RuntimeError): self.validate()

    def test_declared_size_mismatch_is_rejected(self):
        self.manifest["segments"][0]["bytes"] -= 1
        with self.assertRaises(RuntimeError): self.validate()

    def test_parent_path_escape_is_rejected_even_when_hash_matches(self):
        # An existing file outside release directory must not become a flash input.
        with tempfile.TemporaryDirectory(prefix="esp-hack-external-") as external:
            path = Path(external) / "outside.bin"
            data = b"B" * 17536
            path.write_bytes(data)
            self.manifest["segments"][0]["file"] = str(path)
            with self.assertRaises(RuntimeError): self.validate()

    def test_dotdot_path_escape_is_rejected(self):
        self.manifest["segments"][0]["file"] = "../outside.bin"
        with self.assertRaises(RuntimeError): self.validate()

    def test_nested_release_path_is_allowed(self):
        nested = self.root / "images"
        nested.mkdir()
        (nested / "boot.bin").write_bytes((self.root / "bootloader.bin").read_bytes())
        self.manifest["segments"][0]["file"] = "images/boot.bin"
        self.assertEqual(Path(self.validate()[1]), nested / "boot.bin")

    def test_firmware_must_fit_actual_partition_capacity(self):
        self.replace_binary(0x8000, self.partition_table(app_size=32768))
        with self.assertRaises(RuntimeError): self.validate()

    def test_absent_app0_partition_is_rejected(self):
        self.replace_binary(0x8000, b"\xff" * 3072)
        with self.assertRaises(RuntimeError): self.validate()

    def test_wrong_app0_offset_is_rejected(self):
        self.replace_binary(0x8000, self.partition_table(offset=0x20000))
        with self.assertRaises(RuntimeError): self.validate()

    def test_impossible_declared_app0_capacity_is_rejected(self):
        self.replace_binary(0x8000, self.partition_table(app_size=0x1E1000))
        with self.assertRaises(RuntimeError): self.validate()

    def test_wrong_flash_capacity_is_rejected(self):
        for size in (0, 2097152, 8388608):
            with self.subTest(size=size), self.assertRaises(RuntimeError): self.validate(size)

    def test_wrong_target_or_display_is_rejected(self):
        self.manifest["chip"] = "esp32s3"
        with self.assertRaises(RuntimeError): self.validate()
        self.manifest["chip"] = "esp32"
        self.manifest["display"] = "SSD1306"
        with self.assertRaises(RuntimeError): self.validate()


if __name__ == "__main__":
    unittest.main()
