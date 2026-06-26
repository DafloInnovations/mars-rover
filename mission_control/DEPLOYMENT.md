# Mission Control Deployment Guide

This guide deploys the Su-Par1 Mission Control backend to Railway and the
frontend to Vercel. The ESP32 rover continues to connect directly to HiveMQ
over MQTT; it does not connect to Railway or Vercel.

## 1. Railway backend deployment steps

1. Push the project to a Git provider supported by Railway.
2. In Railway, create a new project from the repository.
3. Set the Railway service root directory to:

```text
mission_control/backend
```

4. Railway should install dependencies from `requirements.txt`.
5. The backend start command is provided by both `Procfile` and `railway.json`:

```bash
uvicorn app.main:app --host 0.0.0.0 --port $PORT
```

6. Deploy the service.
7. After deploy, open the Railway public URL and verify:

```text
https://your-railway-backend-url/health
```

Expected response:

```json
{"status":"ok"}
```

## 2. Railway environment variables

Configure these in Railway:

```bash
MQTT_BROKER_HOST=your-hivemq-cluster.s1.eu.hivemq.cloud
MQTT_BROKER_PORT=8883
MQTT_USERNAME=your-hivemq-username
MQTT_PASSWORD=your-hivemq-password
MQTT_TLS_ENABLED=true
ROVER_ID=supar1
FRONTEND_ORIGIN=https://your-vercel-frontend-url.vercel.app
```

Optional Bedrock AI planner variables:

```bash
AWS_REGION=us-east-1
BEDROCK_MODEL_ID=anthropic.claude-3-haiku-20240307-v1:0
```

If using Bedrock, Railway also needs AWS credentials through the standard AWS
environment variables or another supported credential mechanism:

```bash
AWS_ACCESS_KEY_ID=...
AWS_SECRET_ACCESS_KEY=...
AWS_SESSION_TOKEN=... # only if temporary credentials are used
```

The AWS identity must have permission to call Bedrock Runtime for the selected
model.

## 3. Vercel frontend deployment steps

1. In Vercel, create a new project from the same repository.
2. Set the frontend root directory to:

```text
mission_control/frontend
```

3. Use the default Vite settings:

```text
Build Command: npm run build
Output Directory: dist
Install Command: npm install
```

4. Add the frontend environment variable listed below.
5. Deploy the Vercel project.
6. Copy the final Vercel URL and add it back to Railway as `FRONTEND_ORIGIN`.
7. Redeploy Railway after changing `FRONTEND_ORIGIN`.

## 4. Vercel environment variables

Configure:

```bash
VITE_API_BASE_URL=https://your-railway-backend-url
```

Do not include a trailing slash.

Example:

```bash
VITE_API_BASE_URL=https://supar1-mission-control-production.up.railway.app
```

## 5. HiveMQ environment setup

Mission Control and the ESP32 must use the same HiveMQ broker and credentials.

Railway backend:

```bash
MQTT_BROKER_HOST=your-hivemq-cluster.s1.eu.hivemq.cloud
MQTT_BROKER_PORT=8883
MQTT_USERNAME=your-hivemq-username
MQTT_PASSWORD=your-hivemq-password
MQTT_TLS_ENABLED=true
ROVER_ID=supar1
```

ESP32 firmware `firmware/include/WifiMqttConfig.h`:

```cpp
constexpr const char* MQTT_BROKER_HOST = "your-hivemq-cluster.s1.eu.hivemq.cloud";
constexpr uint16_t MQTT_BROKER_PORT = 8883;
constexpr bool MQTT_TLS_ENABLED = true;
constexpr const char* MQTT_USERNAME = "your-hivemq-username";
constexpr const char* MQTT_PASSWORD = "your-hivemq-password";
constexpr const char* ROVER_ID = "supar1";
```

Topics must remain unchanged:

```text
mars/supar1/mission
mars/supar1/command
mars/supar1/status
mars/supar1/telemetry
mars/supar1/log
```

## 6. ESP32 remains connected to HiveMQ directly

The deployed architecture is:

```text
Vercel Frontend → Railway Backend → HiveMQ Broker ← ESP32 Su-Par1
```

The ESP32 does not need to know about Railway or Vercel. It only needs:

- Wi-Fi internet access
- HiveMQ broker host
- HiveMQ port, usually `8883`
- HiveMQ username/password
- matching `ROVER_ID=supar1`

The backend also connects to HiveMQ and dispatches mission commands through the
same MQTT topics.

## 7. Testing checklist

Backend:

- [ ] Railway `/health` returns `{"status":"ok"}`.
- [ ] Railway environment variables are set.
- [ ] `FRONTEND_ORIGIN` exactly matches the deployed Vercel URL.
- [ ] `POST /mqtt/connect` returns connected or connecting.
- [ ] `GET /mqtt/status` shows the expected `mars/supar1/...` topics.

Frontend:

- [ ] Vercel build succeeds.
- [ ] `VITE_API_BASE_URL` points to the Railway backend URL.
- [ ] Dashboard loads without CORS errors.
- [ ] MQTT Connect button reaches the Railway backend.
- [ ] Mission buttons still call `POST /mission`.
- [ ] Voice Command still calls `POST /voice-command`.
- [ ] AI Mission Planner still calls `POST /ai-mission-planner`.

ESP32:

- [ ] Serial Monitor shows Wi-Fi connected.
- [ ] Serial Monitor shows `MQTT_TLS_ENABLED` when using HiveMQ port `8883`.
- [ ] Serial Monitor shows `MQTT_CONNECTED`.
- [ ] Rover publishes `ROVER_ONLINE` to `mars/supar1/log`.
- [ ] Rover publishes rich status to `mars/supar1/status`.

End-to-end:

- [ ] Dashboard Rover Status becomes `ONLINE`.
- [ ] `GET /rover/status` returns firmware, mission, state, location, RSSI, and uptime.
- [ ] Mission button dispatch mode is `mqtt`.
- [ ] Voice command can dispatch a mission.
- [ ] AI Mission Planner can dispatch a mission or falls back safely when Bedrock is unavailable.
- [ ] Live rover animation starts when a mission is dispatched.
