# WebUI Backend

FastAPI backend for receiving robot heartbeat and telemetry.

## Run

```bash
pip install -r requirements.txt
uvicorn app.main:app --host 0.0.0.0 --port 8000 --reload
```

## Endpoints

- `GET /api/health`
- `POST /api/robots/{robot_id}/heartbeat`
- `POST /api/robots/{robot_id}/telemetry`
- `GET /api/robots`
- `GET /api/robots/{robot_id}/latest`
- `WebSocket /ws/robots/{robot_id}`

State is stored in memory only.
