# Mission to Mars 2050 — Local Mission Control

Local operator dashboard for the **Su-Par1** Mars rover. The React frontend
provides mission selection and execution telemetry. The FastAPI backend now
uses MQTT as the primary rover communication channel, keeps USB serial as an
optional development fallback, and falls back to simulation when neither live
transport is available.

Amazon Bedrock is optional and used only by the AI Mission Planner; the rest of
Mission Control continues to work with local rule-based and simulated fallback
behavior when Bedrock is not configured.

## Structure

```text
mission_control/
├── backend/
│   ├── app/
│   │   ├── main.py          # FastAPI application and routes
│   │   ├── models.py        # Validated API data models
│   │   ├── mqtt_service.py  # Thread-safe MQTT rover communication
│   │   └── serial_service.py # Optional ESP32 serial development fallback
│   └── requirements.txt
├── frontend/
│   ├── src/
│   │   ├── App.jsx          # Dashboard and mission workflow
│   │   ├── api.js           # Backend client
│   │   ├── data.js          # Mission and timeline definitions
│   │   └── styles.css       # Mission Control visual system
│   ├── package.json
│   └── vite.config.js
└── README.md
```

## Run the backend

From the project root:

```bash
cd mission_control/backend
python3 -m venv .venv
source .venv/bin/activate
python -m pip install -r requirements.txt
uvicorn app.main:app --reload --port 8000
```

Check the API:

```bash
curl http://localhost:8000/health
```

The expected response is:

```json
{"status":"ok"}
```

For Railway/Vercel deployment, see [DEPLOYMENT.md](./DEPLOYMENT.md).

Submit a mission:

```bash
curl -X POST http://localhost:8000/mission \
  -H "Content-Type: application/json" \
  -d '{"mission_id":1,"mission_name":"Deliver Food to Habitat"}'
```

## MQTT rover communication

MQTT is the primary backend-to-rover transport. Mission Control publishes
mission requests to the rover mission topic and subscribes to rover feedback
topics so the frontend can poll cached state through `GET /rover/status`.
The backend uses a unique MQTT client ID per process and attempts a best-effort
auto-reconnect when `/mqtt/status`, `/rover/status`, or `/mission` is called,
which helps Railway recover after restarts or redeploys.

Required or supported environment variables:

```bash
export MQTT_BROKER_HOST=localhost
export MQTT_BROKER_PORT=1883
export MQTT_USERNAME=
export MQTT_PASSWORD=
export MQTT_TLS_ENABLED=false
export ROVER_ID=supar1
```

Optional Amazon Bedrock AI planner variables:

```bash
export AWS_REGION=us-east-1
export BEDROCK_MODEL_ID=us.anthropic.claude-haiku-4-5-20251001-v1:0
```

The backend uses the standard AWS credential provider chain. Configure
credentials with one of the normal AWS methods before using Bedrock, such as:

- `aws configure`
- `AWS_ACCESS_KEY_ID`, `AWS_SECRET_ACCESS_KEY`, and optional
  `AWS_SESSION_TOKEN`
- an AWS profile selected with `AWS_PROFILE`
- an IAM role if running on AWS infrastructure

The AWS identity must have permission to call Bedrock Runtime for the selected
model or inference profile. Some current Anthropic models require an inference
profile ID such as `us.anthropic.claude-haiku-4-5-20251001-v1:0` instead of the
direct model ID. Use an active profile/model that is enabled in your Bedrock
region; legacy models can fail even with valid AWS credentials. If Bedrock fails
or credentials are unavailable, Mission Control falls back to local rule-based
mission matching.

Default topics for `ROVER_ID=supar1`:

- `mars/supar1/mission`
- `mars/supar1/command`
- `mars/supar1/status`
- `mars/supar1/telemetry`
- `mars/supar1/log`

Connect to the MQTT broker:

```bash
curl -X POST http://localhost:8000/mqtt/connect
```

Check MQTT state:

```bash
curl http://localhost:8000/mqtt/status
```

Publish a manual MQTT payload:

```bash
curl -X POST http://localhost:8000/mqtt/publish \
  -H "Content-Type: application/json" \
  -d '{"topic":"mars/supar1/command","payload":{"command":"STOP"}}'
```

Read the latest cached rover feedback:

```bash
curl http://localhost:8000/rover/status
```

On MQTT connect, the backend subscribes to:

- `mars/supar1/status`
- `mars/supar1/telemetry`
- `mars/supar1/log`

Messages received on those topics are cached in memory as the latest rover
status. This keeps the frontend polling model simple while the rover transport
moves toward wireless operation.

## Run the frontend

Open a second terminal:

```bash
cd mission_control/frontend
npm install
npm run dev
```

Open `http://localhost:5173`. The frontend expects the API at
`http://localhost:8000`. To use a different address, set
`VITE_API_BASE_URL` before starting Vite.

## Frontend rover controls

The frontend uses a large NASA-style mission-control layout with a subtle
starfield, glassmorphism panels, glowing telemetry borders, an animated
CSS-only Mars globe, and an SVG colony route map. No external map or planet
assets are required.

The top header includes compact rover connection controls:

1. Select **Refresh Ports** to query `GET /serial/ports`.
2. Choose the ESP32 serial device from the dropdown.
3. Select **Connect** to open it at 115200 baud.
4. Use **Disconnect** before unplugging the ESP32 or opening another serial
   monitor.

The **Manual Rover Controls** panel sends `FORWARD`, `BACKWARD`, `LEFT`,
`RIGHT`, `STOP`, and `TEST` through `POST /serial/send`. These controls remain
disabled until a serial connection succeeds.

The **Diagnostics Panel** sends the following firmware v0.8 commands through
the same `POST /serial/send` endpoint:

- `LINE_TEST`
- `RFID_TEST`
- `SERVO_OPEN`
- `SERVO_CLOSE`
- `SERVO_TEST`
- `SELF_TEST`

Diagnostic controls remain disabled while disconnected. Backend send
acknowledgements and errors are written to the activity log alongside mission,
connection, and manual-control events.

Mission buttons continue to use `POST /mission`. The backend maps mission IDs
1 through 5 to `MISSION_1` through `MISSION_5`, then tries MQTT first. If MQTT
is unavailable, it falls back to the optional USB serial development
connection. If neither transport is available, missions remain available in
simulated mode.

The **Mission Controls** panel also includes **🎙 Voice Command**. In browsers
with Web Speech API support, a visitor can click the microphone button and say
natural commands such as:

- “Su-Par1 deliver oxygen to the habitat”
- “Bring medicine to habitat”
- “Deliver food supplies”
- “Collect research sample”
- “Return to base”

The frontend sends recognized speech to `POST /voice-command`. The backend
uses simple rule-based matching for now, maps the intent to `MISSION_1` through
`MISSION_5`, and reuses the same mission dispatcher as the mission buttons.

The **AI Mission Planner** card accepts typed natural-language situation
reports such as:

- “The habitat is low on oxygen.”
- “An astronaut needs medicine.”
- “Food supplies are running low.”
- “Collect rock sample.”
- “Return Su-Par1 to base.”

The frontend sends these prompts to `POST /ai-mission-planner`. When AWS
credentials and Bedrock access are configured, the backend asks Bedrock to
return strict JSON selecting exactly one of the five supported missions. The
selected mission then uses the same MQTT → serial → simulated dispatch path as
all other mission controls. If Bedrock is unavailable, the backend falls back
to local rule-based matching.

Backend responses, serial connection activity, manual commands, diagnostics,
and mission results appear in the terminal-style **Activity Log**. The log can
be cleared from the dashboard.

The colony map displays Base, J1, J2, warehouse, oxygen, medicine, and Habitat
nodes with animated planned routes, completed routes, and a pulsing rover
position. When a mission button is selected, the dashboard immediately
animates Su-Par1 along the local mission path, shows progress percentage,
current waypoint, next waypoint, and a simulated ETA countdown. Cargo missions
show a small cargo badge after the rover reaches the pickup warehouse and pulse
the Habitat node after delivery.

The animation remains available in backend simulation mode. When
`GET /rover/status` returns a supported location (`base`, `j1`, `j2`, `wh-a`,
`wh-b`, `wh-c`, or `habitat`), the frontend uses it to correct the rover marker
to the latest live waypoint. Direct manual and diagnostic controls require a
live serial connection.

## API

### `GET /health`

Returns the local API health state.

### `POST /mission`

Accepts:

```json
{
  "mission_id": 1,
  "mission_name": "Deliver Food to Habitat"
}
```

The backend logs the mission and maps its numeric ID to `MISSION_1`,
`MISSION_2`, and so on. It publishes a JSON mission payload to
`mars/supar1/mission` when MQTT is connected. If MQTT is unavailable but a USB
serial development link is connected, the command is sent over serial. If both
are unavailable, the response reports `simulated` mode.

Response mode values:

- `mqtt` — mission was published to the MQTT broker
- `serial` — mission used the development serial fallback
- `simulated` — no live rover transport was available

### `POST /voice-command`

Accepts recognized speech:

```json
{
  "text": "Su-Par1 deliver oxygen to the habitat"
}
```

Rule-based intent examples:

| Spoken phrase | Intent | Mission command |
|---|---|---|
| “Su-Par1 deliver oxygen to the habitat” | `deliver_oxygen` | `MISSION_3` |
| “Bring medicine to habitat” | `deliver_medicine` | `MISSION_2` |
| “Deliver food supplies” | `deliver_food` | `MISSION_1` |
| “Collect research sample” | `collect_research_sample` | `MISSION_4` |
| “Return to base” | `return_to_base` | `MISSION_5` |

Successful response:

```json
{
  "recognized_text": "Su-Par1 deliver oxygen to the habitat",
  "intent": "deliver_oxygen",
  "mission_id": 3,
  "command": "MISSION_3",
  "dispatch_mode": "mqtt"
}
```

Unknown command response:

```json
{
  "recognized_text": "Su-Par1 dance on Mars",
  "error": "UNKNOWN_VOICE_COMMAND"
}
```

### `POST /ai-mission-planner`

Accepts a typed natural-language Mars situation:

```json
{
  "text": "The habitat is low on oxygen"
}
```

Bedrock is instructed to classify the request into only:

- `MISSION_1` — Deliver Food to Habitat
- `MISSION_2` — Deliver Medicine to Habitat
- `MISSION_3` — Deliver Oxygen to Habitat
- `MISSION_4` — Collect Research Sample
- `MISSION_5` — Return to Base

Successful response:

```json
{
  "input_text": "The habitat is low on oxygen",
  "intent": "deliver_oxygen",
  "mission_id": 3,
  "command": "MISSION_3",
  "reason": "Habitat oxygen is low, so Su-Par1 should deliver oxygen from WH-C.",
  "dispatch_mode": "mqtt"
}
```

If Bedrock fails, the backend logs the failure and attempts local rule-based
fallback. If no local intent is detected, the response is:

```json
{
  "input_text": "Paint the rover blue",
  "error": "UNKNOWN_AI_MISSION_REQUEST"
}
```

### MQTT API

- `GET /mqtt/status` — connection state, configured topics, subscriptions, and
  cached rover data
- `POST /mqtt/connect` — connect to the broker configured by environment
  variables
- `POST /mqtt/disconnect` — disconnect from the broker
- `POST /mqtt/publish` — publish a payload to a selected topic
- `GET /rover/status` — latest cached rover status, telemetry, and log payloads

## ESP32 serial setup

USB serial is kept for local development and debugging only. MQTT is the
primary rover communication path.

1. Build and upload the Su-Par1 firmware.
2. Connect the ESP32 to the Mission Control computer over USB.
3. Close PlatformIO's serial monitor so it does not hold the port open.
4. Start the FastAPI backend.
5. List available ports:

```bash
curl http://localhost:8000/serial/ports
```

On macOS, ESP32 devices commonly appear as `/dev/cu.usbserial-*` or
`/dev/cu.SLAB_USBtoUART`. On Linux they commonly appear as `/dev/ttyUSB0` or
`/dev/ttyACM0`. Windows uses names such as `COM3`.

Connect at the firmware baud rate:

```bash
curl -X POST http://localhost:8000/serial/connect \
  -H "Content-Type: application/json" \
  -d '{"port":"/dev/cu.usbserial-0001","baud":115200}'
```

Send a direct firmware command:

```bash
curl -X POST http://localhost:8000/serial/send \
  -H "Content-Type: application/json" \
  -d '{"command":"FORWARD"}'
```

Disconnect:

```bash
curl -X POST http://localhost:8000/serial/disconnect
```

## Serial API

- `GET /serial/ports` — list detected serial devices
- `POST /serial/connect` — connect using `port` and optional `baud`
- `POST /serial/disconnect` — close the active connection
- `POST /serial/send` — send a newline-delimited development `command`

The service defaults to 115200 baud and normalizes direct commands to
uppercase. Only one serial connection is held at a time, and it is closed when
the backend shuts down.

Mission commands map IDs 1 through 5 to `MISSION_1` through `MISSION_5`, which
are supported by Su-Par1 firmware v0.8.
