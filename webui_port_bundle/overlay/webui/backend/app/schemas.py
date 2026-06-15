from typing import Any

from pydantic import BaseModel, Field


class HeartbeatPayload(BaseModel):
    robot_id: str
    client_time: float | None = None
    has_status: bool = False
    status_received_at: float | None = None


class TelemetryPayload(BaseModel):
    robot_id: str
    client_time: float | None = None
    status_received_at: float | None = None
    status: dict[str, Any] = Field(default_factory=dict)
