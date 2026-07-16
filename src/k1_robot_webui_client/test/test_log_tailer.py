import tempfile
import unittest
from pathlib import Path

from k1_robot_webui_client.log_tailer import LogFileTailer


def create_tailer(log_path: Path, **overrides) -> LogFileTailer:
    parameters = {
        "source": "brain",
        "path": str(log_path),
        "initial_line_limit": 3,
        "batch_line_limit": 10,
        "read_byte_limit": 1024,
    }
    parameters.update(overrides)
    return LogFileTailer(**parameters)


class LogFileTailerTest(unittest.TestCase):
    def setUp(self):
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.temporary_path = Path(self.temporary_directory.name)

    def tearDown(self):
        self.temporary_directory.cleanup()

    def test_reads_only_recent_lines_on_first_observation(self):
        log_path = self.temporary_path / "brain.log"
        log_path.write_text(
            "line-1\nline-2\nline-3\nline-4\nline-5\n",
            encoding="utf-8",
        )
        tailer = create_tailer(log_path)

        batch = tailer.read_batch()

        self.assertIsNotNone(batch)
        self.assertTrue(batch.reset)
        self.assertEqual(batch.lines, ["line-3", "line-4", "line-5"])
        self.assertIsNone(tailer.read_batch())

    def test_waits_for_a_complete_appended_line(self):
        log_path = self.temporary_path / "brain.log"
        log_path.write_text("existing\n", encoding="utf-8")
        tailer = create_tailer(log_path)
        tailer.read_batch()

        with log_path.open("ab") as log_file:
            log_file.write(b"partial")
        self.assertIsNone(tailer.read_batch())

        with log_path.open("ab") as log_file:
            log_file.write(b" line\n")
        batch = tailer.read_batch()

        self.assertIsNotNone(batch)
        self.assertFalse(batch.reset)
        self.assertEqual(batch.lines, ["partial line"])

    def test_detects_same_size_file_rewrite_as_stream_reset(self):
        log_path = self.temporary_path / "brain.log"
        log_path.write_text("old\n", encoding="utf-8")
        tailer = create_tailer(log_path)
        tailer.read_batch()

        log_path.write_text("new\n", encoding="utf-8")
        batch = tailer.read_batch()

        self.assertIsNotNone(batch)
        self.assertTrue(batch.reset)
        self.assertEqual(batch.lines, ["new"])

    def test_truncates_oversized_lines_before_upload(self):
        log_path = self.temporary_path / "brain.log"
        log_path.write_text(f"{'x' * 1000}\n", encoding="utf-8")
        tailer = create_tailer(log_path, line_character_limit=256)

        batch = tailer.read_batch()

        self.assertIsNotNone(batch)
        self.assertEqual(len(batch.lines), 1)
        self.assertEqual(len(batch.lines[0]), 256)
        self.assertTrue(batch.lines[0].endswith(" ... [truncated by webui]"))


if __name__ == "__main__":
    unittest.main()
