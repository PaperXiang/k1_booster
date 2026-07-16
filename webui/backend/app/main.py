from fastapi import FastAPI, HTTPException, Query, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware

from .schemas import HeartbeatPayload, LogSource, LogUploadPayload, TelemetryPayload
from .services.log_registry import log_registry
from .services.robot_registry import registry


app = FastAPI(title="K1 Robot WebUI Backend")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)


@app.get("/api/health")
async def health():
    return {"ok": True}


@app.post("/api/robots/{robot_id}/heartbeat")
async def heartbeat(robot_id: str, payload: HeartbeatPayload):
    if payload.robot_id != robot_id:
        raise HTTPException(status_code=400, detail="robot_id mismatch")
    return await registry.heartbeat(robot_id, payload.dict())


@app.post("/api/robots/{robot_id}/telemetry")
async def telemetry(robot_id: str, payload: TelemetryPayload):
    if payload.robot_id != robot_id:
        raise HTTPException(status_code=400, detail="robot_id mismatch")
    return await registry.telemetry(robot_id, payload.dict())


@app.post("/api/robots/{robot_id}/logs")
async def upload_logs(robot_id: str, payload: LogUploadPayload):
    if payload.robot_id != robot_id:
        raise HTTPException(status_code=400, detail="robot_id mismatch")
    if len(payload.batches) > 12:
        raise HTTPException(status_code=413, detail="too many log batches")

    results = []
    for batch in payload.batches:
        if len(batch.lines) > 500:
            raise HTTPException(status_code=413, detail="too many lines in log batch")
        if any(len(line) > 16384 for line in batch.lines):
            raise HTTPException(status_code=413, detail="log line is too long")

        normalized_lines = [line.rstrip("\r\n") for line in batch.lines]
        results.append(
            await log_registry.append(
                robot_id,
                batch.source,
                normalized_lines,
                batch.reset,
            )
        )

    return {"robot_id": robot_id, "batches": results}


@app.get("/api/robots")
async def robots():
    return await registry.list_robots()


@app.get("/api/robots/{robot_id}/latest")
async def latest(robot_id: str):
    robot = await registry.latest(robot_id)
    if robot is None:
        raise HTTPException(status_code=404, detail="robot not found")
    return robot


@app.get("/api/robots/{robot_id}/logs/{source}")
async def recent_logs(
    robot_id: str,
    source: LogSource,
    limit: int = Query(default=500, ge=1, le=2000),
):
    return await log_registry.recent(robot_id, source, limit)


@app.websocket("/ws/robots/{robot_id}")
async def robot_socket(websocket: WebSocket, robot_id: str):
    await websocket.accept()
    await registry.add_socket(robot_id, websocket)
    latest_robot = await registry.latest(robot_id)
    if latest_robot is not None:
        await websocket.send_json(latest_robot)
    try:
        while True:
            await websocket.receive_text()
    except WebSocketDisconnect:
        await registry.remove_socket(robot_id, websocket)


@app.websocket("/ws/robots/{robot_id}/logs/{source}")
async def robot_log_socket(websocket: WebSocket, robot_id: str, source: LogSource):
    await websocket.accept()
    try:
        await log_registry.subscribe(robot_id, source, websocket)
        while True:
            await websocket.receive_text()
    except WebSocketDisconnect:
        pass
    finally:
        await log_registry.remove_socket(robot_id, source, websocket)
