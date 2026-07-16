import asyncio
import time
from collections import deque
from typing import Any

from fastapi import WebSocket, WebSocketDisconnect


class LogRegistry:
    def __init__(self, history_line_limit: int = 2000):
        self._history_line_limit = history_line_limit
        self._histories: dict[tuple[str, str], deque[str]] = {}
        self._sockets: dict[tuple[str, str], set[WebSocket]] = {}
        self._lock = asyncio.Lock()

    async def append(
        self,
        robot_id: str,
        source: str,
        lines: list[str],
        reset: bool = False,
    ) -> dict[str, Any]:
        stream_key = (robot_id, source)
        received_at = time.time()

        async with self._lock:
            history = self._histories.setdefault(
                stream_key,
                deque(maxlen=self._history_line_limit),
            )
            if reset:
                history.clear()
            history.extend(lines)
            sockets = list(self._sockets.get(stream_key, set()))

        payload = {
            "type": "append",
            "robot_id": robot_id,
            "source": source,
            "lines": lines,
            "reset": reset,
            "received_at": received_at,
        }
        await self._broadcast(stream_key, sockets, payload)
        return {
            "source": source,
            "accepted_lines": len(lines),
            "reset": reset,
        }

    async def recent(self, robot_id: str, source: str, limit: int = 500) -> dict[str, Any]:
        stream_key = (robot_id, source)
        bounded_limit = max(1, min(limit, self._history_line_limit))

        async with self._lock:
            history = self._histories.get(stream_key)
            lines = list(history)[-bounded_limit:] if history is not None else []

        return {
            "type": "history",
            "robot_id": robot_id,
            "source": source,
            "lines": lines,
            "reset": True,
            "received_at": time.time(),
        }

    async def subscribe(
        self,
        robot_id: str,
        source: str,
        websocket: WebSocket,
        history_limit: int = 500,
    ):
        stream_key = (robot_id, source)
        bounded_limit = max(1, min(history_limit, self._history_line_limit))

        async with self._lock:
            history = self._histories.get(stream_key)
            lines = list(history)[-bounded_limit:] if history is not None else []
            await websocket.send_json(
                {
                    "type": "history",
                    "robot_id": robot_id,
                    "source": source,
                    "lines": lines,
                    "reset": True,
                    "received_at": time.time(),
                }
            )
            self._sockets.setdefault(stream_key, set()).add(websocket)

    async def remove_socket(self, robot_id: str, source: str, websocket: WebSocket):
        stream_key = (robot_id, source)
        async with self._lock:
            sockets = self._sockets.get(stream_key)
            if sockets is None:
                return
            sockets.discard(websocket)
            if not sockets:
                self._sockets.pop(stream_key, None)

    async def _broadcast(
        self,
        stream_key: tuple[str, str],
        sockets: list[WebSocket],
        payload: dict[str, Any],
    ):
        disconnected: list[WebSocket] = []
        for websocket in sockets:
            try:
                await websocket.send_json(payload)
            except (OSError, RuntimeError, WebSocketDisconnect):
                disconnected.append(websocket)

        robot_id, source = stream_key
        for websocket in disconnected:
            await self.remove_socket(robot_id, source, websocket)


log_registry = LogRegistry()
