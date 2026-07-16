# K1 机器人 WebUI

该 WebUI 用于在与机器人处于同一局域网的计算机上显示机器人状态。机器人本身不运行 Web 服务器；它会在本地发布 ROS 状态，而 `k1_robot_webui_client` 软件包会主动将遥测数据发送到此后端。

## 架构

```text
brain_node -> /brain/status_json -> k1_robot_webui_client -> FastAPI 后端 -> 浏览器前端
brain.log / game_controller.log / vision.log -> k1_robot_webui_client -> FastAPI 后端 -> 浏览器日志栏
```

## PC 后端

在需要接收机器人状态的计算机上运行：

```powershell
cd d:\booster\K1_5v5_Demo_v1.6\webui\backend
python -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -r requirements.txt
uvicorn app.main:app --host 0.0.0.0 --port 8000 --reload
```

注意：

- 请使用 `--host 0.0.0.0`，而不是 `127.0.0.1`，以便机器人能够访问后端。
- 在 Windows 防火墙中允许 TCP `8000` 端口。
- 获取本机的局域网 IP，例如 `192.168.1.100`，并将其填入机器人客户端配置。

## PC 前端

```powershell
cd d:\booster\K1_5v5_Demo_v1.6\webui\frontend
npm install
npm run dev
```

打开 Vite 输出的 URL，通常为：

```text
http://localhost:5173
```

如果后端不在 `localhost:8000`，请在启动前端前设置 `VITE_API_BASE_URL`。

## 机器人客户端

在以下文件中编辑后端地址：

```text
src/k1_robot_webui_client/config/webui_client.yaml
```

示例：

```yaml
server_base_url: "http://192.168.1.100:8000"
robot_id: "k1-3"
```

在机器人上构建并运行：

```bash
colcon build --packages-select brain k1_robot_webui_client
source install/setup.bash
ros2 launch k1_robot_webui_client webui_client.launch.py
```

机器人客户端订阅 `/brain/status_json`，发送心跳消息，将最新状态发送至 PC 后端，并持续读取三个进程日志。单独启动客户端时，可以显式指定日志所在目录：

```bash
ros2 launch k1_robot_webui_client webui_client.launch.py log_directory:=$PWD
```

`scripts/start.sh` 会自动传入工作区根目录。日志栏保留有界的近期历史，实时追加新日志，并支持在 Brain、Game Controller 和 Vision 之间切换。

## 第一版显示的内容

- 在线/离线状态以及距离上次出现的时间。
- 球员 ID、角色、比赛状态和当前决策。
- 机器人是否处于 `chase`、`adjust` 或 `RLVisionKick` 状态。
- 足球检测结果、距离、偏航角、置信度和场地位置。
- 足球预测的启用/使用/有效状态、预测足球、速度、加速度和轨迹。
- 简单二维场地中的机器人位姿。
- 视觉、GameController 和定位的健康状态延迟。
- Brain、Game Controller 和 Vision 实时控制台日志，支持来源切换和跟随末尾滚动。

## 安全说明

第一版仅用于显示。若未来添加控制按钮，请保持 HTTP 命令的低频率，加入令牌认证和命令白名单，设置超时，并要求对危险操作进行确认。
