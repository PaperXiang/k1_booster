import asyncio
import time
from typing import Any

from fastapi import WebSocket


class RobotRegistry:
    def __init__(self):
        self._robots: dict[str, dict[str, Any]] = {}
        self._sockets: dict[str, set[WebSocket]] = {}
        self._lock = asyncio.Lock()

    async def heartbeat(self, robot_id: str, payload: dict[str, Any]) -> dict[str, Any]:
        async with self._lock:
            robot = self._robots.setdefault(robot_id, {"robot_id": robot_id})
            robot["last_heartbeat_at"] = time.time()
            robot["heartbeat"] = payload
            snapshot = self._summary(robot)
        await self.broadcast(robot_id)
        return snapshot

    async def telemetry(self, robot_id: str, payload: dict[str, Any]) -> dict[str, Any]:
        async with self._lock:
            robot = self._robots.setdefault(robot_id, {"robot_id": robot_id})
            robot["last_telemetry_at"] = time.time()
            robot["telemetry"] = payload
            robot["status"] = payload.get("status", {})
            snapshot = self._summary(robot)
        await self.broadcast(robot_id)
        return snapshot

    async def list_robots(self) -> list[dict[str, Any]]:
        async with self._lock:
            return [self._summary(robot) for robot in self._robots.values()]

    async def latest(self, robot_id: str) -> dict[str, Any] | None:
        async with self._lock:
            robot = self._robots.get(robot_id)
            if robot is None:
                return None
            return self._summary(robot)

    async def add_socket(self, robot_id: str, websocket: WebSocket):
        async with self._lock:
            self._sockets.setdefault(robot_id, set()).add(websocket)

    async def remove_socket(self, robot_id: str, websocket: WebSocket):
        async with self._lock:
            sockets = self._sockets.get(robot_id)
            if sockets is None:
                return
            sockets.discard(websocket)
            if not sockets:
                self._sockets.pop(robot_id, None)

    async def broadcast(self, robot_id: str):
        async with self._lock:
            robot = self._robots.get(robot_id)
            payload = self._summary(robot) if robot is not None else None
            sockets = list(self._sockets.get(robot_id, set()))

        if payload is None:
            return

        disconnected: list[WebSocket] = []
        for socket in sockets:
            try:
                await socket.send_json(payload)
            except RuntimeError:
                disconnected.append(socket)

        for socket in disconnected:
            await self.remove_socket(robot_id, socket)

    def _summary(self, robot: dict[str, Any]) -> dict[str, Any]:
        now = time.time()
        last_heartbeat = robot.get("last_heartbeat_at")
        last_telemetry = robot.get("last_telemetry_at")
        last_seen = max([t for t in [last_heartbeat, last_telemetry] if t is not None], default=None)
        return {
            "robot_id": robot["robot_id"],
            "online": last_seen is not None and now - last_seen < 5.0,
            "last_seen_at": last_seen,
            "last_heartbeat_at": last_heartbeat,
            "last_telemetry_at": last_telemetry,
            "heartbeat": robot.get("heartbeat", {}),
            "status": robot.get("status", {}),
        }


registry = RobotRegistry()
