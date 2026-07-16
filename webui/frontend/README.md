# WebUI 前端

用于展示机器人遥测数据的 Vite + React 仪表盘。

## 运行

```bash
npm install
npm run dev
```

默认情况下，前端会连接到 `http://localhost:8000`。如需使用其他后端地址：

```bash
VITE_API_BASE_URL=http://192.168.1.100:8000 npm run dev
```

在 Windows PowerShell 中：

```powershell
$env:VITE_API_BASE_URL="http://192.168.1.100:8000"
npm run dev
```
