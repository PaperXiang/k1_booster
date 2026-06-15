from fastapi import FastAPI, HTTPException, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware

from .schemas import HeartbeatPayload, TelemetryPayload
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


@app.get("/api/robots")
async def robots():
    return await registry.list_robots()


@app.get("/api/robots/{robot_id}/latest")
async def latest(robot_id: str):
    robot = await registry.latest(robot_id)
    if robot is None:
        raise HTTPException(status_code=404, detail="robot not found")
    return robot


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
