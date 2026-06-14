# K1 Robot WebUI

This WebUI shows robot status on a computer in the same LAN. The robot does not run a web server; it publishes ROS status locally and the `k1_robot_webui_client` package actively posts telemetry to this backend.

## Architecture

```text
brain_node -> /brain/status_json -> k1_robot_webui_client -> FastAPI backend -> browser frontend
```

## PC backend

Run on the computer that should receive robot status:

```powershell
cd d:\booster\K1_5v5_Demo_v1.6\webui\backend
python -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -r requirements.txt
uvicorn app.main:app --host 0.0.0.0 --port 8000 --reload
```

Important:

- Use `--host 0.0.0.0`, not `127.0.0.1`, so the robot can reach the backend.
- Allow TCP port `8000` in Windows Firewall.
- Find this computer's LAN IP, for example `192.168.1.100`, and put it in the robot client config.

## PC frontend

```powershell
cd d:\booster\K1_5v5_Demo_v1.6\webui\frontend
npm install
npm run dev
```

Open the URL printed by Vite, usually:

```text
http://localhost:5173
```

If the backend is not on `localhost:8000`, set `VITE_API_BASE_URL` before starting the frontend.

## Robot client

Edit the backend address in:

```text
src/k1_robot_webui_client/config/webui_client.yaml
```

Example:

```yaml
server_base_url: "http://192.168.1.100:8000"
robot_id: "k1-3"
```

Build and run on the robot:

```bash
colcon build --packages-select brain k1_robot_webui_client
source install/setup.bash
ros2 launch k1_robot_webui_client webui_client.launch.py
```

The robot client subscribes to `/brain/status_json`, sends heartbeat messages, and posts the latest status to the PC backend.

## What the first version displays

- Online/offline state and last-seen age.
- Player id, role, game state, and current decision.
- Whether the robot is in `chase`, `adjust`, or `RLVisionKick`.
- Ball detection, range, yaw, confidence, and field position.
- Ball prediction enable/use/valid state, predicted ball, velocity, acceleration, and trajectory.
- Robot pose on a simple 2D field.
- Vision/GameController/localization health lag.

## Safety note

This first version is display-only. If control buttons are added later, keep HTTP commands low-frequency, add a token, add command whitelisting, set timeouts, and require confirmation for dangerous actions.
