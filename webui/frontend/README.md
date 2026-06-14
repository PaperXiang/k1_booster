# WebUI Frontend

Vite + React dashboard for robot telemetry.

## Run

```bash
npm install
npm run dev
```

By default the frontend connects to `http://localhost:8000`. To use another backend:

```bash
VITE_API_BASE_URL=http://192.168.1.100:8000 npm run dev
```

On Windows PowerShell:

```powershell
$env:VITE_API_BASE_URL="http://192.168.1.100:8000"
npm run dev
```
