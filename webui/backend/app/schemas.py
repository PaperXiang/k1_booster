from typing import Any, Literal

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


LogSource = Literal["brain", "game_controller", "vision"]


class LogBatchPayload(BaseModel):
    source: LogSource
    lines: list[str] = Field(default_factory=list)
    reset: bool = False


class LogUploadPayload(BaseModel):
    robot_id: str
    client_time: float | None = None
    batches: list[LogBatchPayload] = Field(default_factory=list)
