# WebUI 后端

用于接收机器人心跳、遥测数据和有限批量进程日志的 FastAPI 后端。

## 运行

```bash
pip install -r requirements.txt
uvicorn app.main:app --host 0.0.0.0 --port 8000 --reload
```

## 接口

- `GET /api/health`
- `POST /api/robots/{robot_id}/heartbeat`
- `POST /api/robots/{robot_id}/telemetry`
- `POST /api/robots/{robot_id}/logs`
- `GET /api/robots`
- `GET /api/robots/{robot_id}/latest`
- `GET /api/robots/{robot_id}/logs/{source}`
- `WebSocket /ws/robots/{robot_id}`
- `WebSocket /ws/robots/{robot_id}/logs/{source}`

支持的日志来源包括 `brain`、`game_controller` 和 `vision`。机器人状态和日志状态仅存储在内存中；重启后端会清空已缓存的历史记录。
