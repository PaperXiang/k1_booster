# WebUI Linux 补丁包

这个包用于把机器人端需要的 WebUI 支持快速打到另一份 K1 demo 上。

约定：

- WebUI 前端和后端继续放在 Windows 上运行，不移动、不覆盖。
- Linux 机器人端只复制 ROS2 bridge 包，必要时覆盖 brain 的 `/brain/status_json` 发布代码。
- Linux 端的 bridge 会把 `/brain/status_json` 上报到 Windows 后端。

## 包内内容

- `overlay/src/k1_robot_webui_client`：Linux 机器人端 ROS2 bridge 包，订阅 `/brain/status_json` 并通过 HTTP 发到 Windows WebUI 后端。
- `overlay/src/brain/src/brain.cpp` 和 `overlay/src/brain/include/brain.h`：可选 brain 补丁文件，用于目标 demo 还没有 `/brain/status_json` 或字段不够的情况。
- `apply_webui.sh`：推荐在 Linux 机器人端使用的补丁脚本。
- `apply_webui.ps1`：同样只做机器人端补丁的 PowerShell 脚本，主要用于在 Windows 上整理/测试目标目录时使用。
- `overlay/webui`：保留一份 WebUI 源码备份，但当前流程不复制它；前后端以 Windows 上现有的为准。

## Linux 机器人端使用

把整个 `webui_port_bundle` 拷到 Linux 机器人或目标 demo 旁边，然后执行：

```bash
cd webui_port_bundle
chmod +x ./apply_webui.sh
./apply_webui.sh
```

脚本会依次询问：

1. 目标 demo 目录，例如 `/home/booster/Workspace/K1_5v5_Demo_1.5`。
2. Windows 后端 IP 或 URL，例如 `192.168.15.21`。只输入 IP 时会自动变成 `http://192.168.15.21:8000`。
3. 是否为原始 demo。选择 `y` 表示原始 demo，脚本会认为没有 `k1_ball_predictor`，因此不会执行完整 brain 覆盖；选择 `n` 表示新版/改过的 demo，脚本会认为有 `k1_ball_predictor`。
4. 是否 patch brain。默认是 `n`，直接回车表示不覆盖 brain。如果上一步选择了原始 demo，即使这里选择 `y`，脚本也会自动跳过完整 brain 覆盖，避免 `k1_ball_predictor` 缺失导致编译失败。

仍然支持非交互参数：

```bash
./apply_webui.sh --target /path/to/other_demo --server-url http://WINDOWS_IP:8000 --no-patch-brain
```

原始 demo / 新版 demo 也可以通过参数指定：

```bash
# 原始 demo：认为没有 k1_ball_predictor
./apply_webui.sh --target /path/to/other_demo --server-url http://WINDOWS_IP:8000 --original-demo

# 新版或自定义 demo：认为有 k1_ball_predictor
./apply_webui.sh --target /path/to/other_demo --server-url http://WINDOWS_IP:8000 --not-original-demo
```

如果目标 demo 没有发布 `/brain/status_json`，或者需要这版更丰富的 field/perception telemetry，再加：

```bash
./apply_webui.sh --target /path/to/other_demo --server-url http://WINDOWS_IP:8000 --not-original-demo --patch-brain
```

注意：`--patch-brain` 会覆盖目标 demo 的 `brain.cpp` 和 `brain.h`，不同版本 demo 之间可能有依赖差异。比如旧 demo 如果没有 `k1_ball_predictor` 包，覆盖新版 brain 后可能出现：

```text
fatal error: k1_ball_predictor/ball_motion_predictor.hpp: No such file or directory
```

遇到这种情况，先恢复脚本生成的 `brain.cpp.backup.*` 和 `brain.h.backup.*`，然后重新运行脚本并在 `Patch brain?` 时选择 `n`。

其中 `WINDOWS_IP` 改成运行 WebUI backend 的 Windows 电脑 IP，例如：

```bash
./apply_webui.sh --target ~/K1_5v5_Demo --server-url http://192.168.15.21:8000 --patch-brain
```

## Windows 上的 WebUI 不动

这个脚本不会复制：

- `webui/frontend`
- `webui/backend`
- `node_modules`
- `dist`
- `.venv`

Windows 上继续按原来的方式启动 backend/frontend 即可。

Linux 端只需要能访问 Windows 后端地址，也就是 `server_base_url`。

## 安全机制

默认会把目标 demo 中已存在的文件/目录备份成：

```text
*.backup.YYYYMMDD_HHMMSS
```

例如：

- `src/k1_robot_webui_client.backup.20260615_120000`
- `src/brain/src/brain.cpp.backup.20260615_120000`

脚本会自动给 `src/*.backup.*` 目录写入 `COLCON_IGNORE`，避免 colcon 把备份目录也当成 ROS package 扫描。如果你之前已经手动产生过备份目录并遇到 duplicate package，可以在目标 demo 中执行：

```bash
touch src/k1_robot_webui_client.backup.*/COLCON_IGNORE
```

只有明确不想备份时才使用：

```bash
./apply_webui.sh --target /path/to/other_demo --no-backup
```

## 应用后步骤

1. 确认 Linux 端配置：
   ```bash
   cat /path/to/other_demo/src/k1_robot_webui_client/config/webui_client.yaml
   ```
   重点看 `server_base_url` 是否是 Windows 后端地址。

2. 在 Linux 端重新编译 ROS workspace：
   ```bash
   cd /path/to/other_demo
   colcon build --symlink-install
   source install/setup.bash
   ```

3. 启动 Windows 上的 WebUI backend/frontend。

4. 在 Linux 端启动 brain 和 `k1_robot_webui_client`。

## 什么时候需要 `--patch-brain`

- 目标 demo 已经有 `/brain/status_json`：通常不需要。
- 目标 demo 没有 `/brain/status_json`：需要。
- WebUI 上 field、队友、障碍物、门柱、标线等信息为空，并且你希望显示这些内容：建议使用。
