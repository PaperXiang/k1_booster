import os
from collections import deque
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class LogTailBatch:
    source: str
    lines: list[str]
    reset: bool


class LogFileTailer:
    def __init__(
        self,
        source: str,
        path: str,
        initial_line_limit: int,
        batch_line_limit: int,
        read_byte_limit: int,
        pending_line_limit: int = 2000,
        line_character_limit: int = 16000,
    ):
        self.source = source
        self.path = Path(path)
        self.initial_line_limit = max(1, initial_line_limit)
        self.batch_line_limit = max(1, batch_line_limit)
        self.read_byte_limit = max(1024, read_byte_limit)
        self.pending_line_limit = max(self.batch_line_limit, pending_line_limit)
        self.line_character_limit = max(256, line_character_limit)
        self.anchor_byte_limit = 128

        self._file_identity: tuple[int, int] | None = None
        self._offset = 0
        self._tail_anchor = b""
        self._partial_line = b""
        self._pending_lines: deque[str] = deque()
        self._initialized = False
        self._reset_pending = False
        self._dropped_line_count = 0

    def read_batch(self) -> LogTailBatch | None:
        try:
            with self.path.open("rb") as log_file:
                file_stat = os.fstat(log_file.fileno())
                file_identity = (file_stat.st_dev, file_stat.st_ino)
                first_observation = not self._initialized
                content_replaced = (
                    self._initialized
                    and self._file_identity == file_identity
                    and file_stat.st_size >= self._offset
                    and not self._anchor_matches(log_file)
                )
                stream_restarted = (
                    self._initialized
                    and (
                        self._file_identity != file_identity
                        or file_stat.st_size < self._offset
                        or content_replaced
                    )
                )

                if first_observation:
                    self._initialize_from_recent_history(log_file, file_stat.st_size)
                    self._file_identity = file_identity
                    self._initialized = True
                    self._reset_pending = True
                elif stream_restarted:
                    self._reset_stream(file_identity)

                if not first_observation:
                    log_file.seek(self._offset)
                    new_content = log_file.read(self.read_byte_limit)
                    self._offset = log_file.tell()
                    self._append_content(new_content)
                self._update_tail_anchor(log_file)
        except (FileNotFoundError, IsADirectoryError, PermissionError, OSError):
            return self._next_batch()

        return self._next_batch()

    def _initialize_from_recent_history(self, log_file, file_size: int):
        initial_byte_limit = max(
            self.read_byte_limit,
            self.initial_line_limit * 512,
        )
        start_offset = max(0, file_size - initial_byte_limit)
        log_file.seek(start_offset)
        content = log_file.read()
        self._offset = log_file.tell()

        if start_offset > 0:
            first_line_end = content.find(b"\n")
            content = content[first_line_end + 1:] if first_line_end >= 0 else b""

        complete_lines, partial_line = self._split_complete_lines(content)
        self._partial_line = partial_line
        for line in complete_lines[-self.initial_line_limit:]:
            self._queue_line(line)

    def _reset_stream(self, file_identity: tuple[int, int]):
        self._file_identity = file_identity
        self._offset = 0
        self._tail_anchor = b""
        self._partial_line = b""
        self._pending_lines.clear()
        self._dropped_line_count = 0
        self._reset_pending = True

    def _anchor_matches(self, log_file) -> bool:
        if self._offset <= 0:
            return True

        expected_anchor_length = min(self.anchor_byte_limit, self._offset)
        if len(self._tail_anchor) != expected_anchor_length:
            return False

        log_file.seek(self._offset - expected_anchor_length)
        return log_file.read(expected_anchor_length) == self._tail_anchor

    def _update_tail_anchor(self, log_file):
        if self._offset <= 0:
            self._tail_anchor = b""
            return

        current_position = log_file.tell()
        anchor_length = min(self.anchor_byte_limit, self._offset)
        log_file.seek(self._offset - anchor_length)
        self._tail_anchor = log_file.read(anchor_length)
        log_file.seek(current_position)

    def _append_content(self, content: bytes):
        if not content:
            return

        complete_lines, self._partial_line = self._split_complete_lines(
            self._partial_line + content
        )
        for line in complete_lines:
            self._queue_line(line)

    def _split_complete_lines(self, content: bytes) -> tuple[list[str], bytes]:
        if not content:
            return [], b""

        chunks = content.split(b"\n")
        if content.endswith(b"\n"):
            partial_line = b""
            complete_chunks = chunks[:-1]
        else:
            partial_line = chunks[-1]
            complete_chunks = chunks[:-1]

        lines = [
            chunk.rstrip(b"\r").decode("utf-8", errors="replace")
            for chunk in complete_chunks
        ]
        return lines, partial_line

    def _queue_line(self, line: str):
        truncation_marker = " ... [truncated by webui]"
        if len(line) > self.line_character_limit:
            retained_character_count = self.line_character_limit - len(truncation_marker)
            line = line[:retained_character_count] + truncation_marker

        self._pending_lines.append(line)
        while len(self._pending_lines) > self.pending_line_limit:
            self._pending_lines.popleft()
            self._dropped_line_count += 1

    def _next_batch(self) -> LogTailBatch | None:
        if not self._pending_lines and not self._reset_pending:
            return None

        lines: list[str] = []
        if self._dropped_line_count > 0:
            lines.append(
                f"[webui] dropped {self._dropped_line_count} lines from the local upload backlog"
            )
            self._dropped_line_count = 0

        remaining_line_count = self.batch_line_limit - len(lines)
        while self._pending_lines and remaining_line_count > 0:
            lines.append(self._pending_lines.popleft())
            remaining_line_count -= 1

        reset = self._reset_pending
        self._reset_pending = False
        return LogTailBatch(source=self.source, lines=lines, reset=reset)
