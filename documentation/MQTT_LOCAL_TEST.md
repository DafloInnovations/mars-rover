# Su-Par1 Local MQTT Test Guide

This guide verifies the full local Mission Control → MQTT broker → ESP32 rover
path for **Mission to Mars 2050 — Su-Par1**.

The current firmware uses `WiFi.h` + `PubSubClient` with plain MQTT. It does
not yet create a TLS `WiFiClientSecure` connection, so use a broker/listener
that accepts non-TLS MQTT for this test.

## 1. HiveMQ required values

Use a HiveMQ Cloud cluster or local HiveMQ broker that has a plain MQTT listener
available.

Required values:

| Value | Example | Notes |
|---|---|---|
| Broker host | `your-cluster.s1.eu.hivemq.cloud` or local broker IP | Use the exact hostname/IP from HiveMQ. |
| MQTT port | `1883` | Current firmware supports plain MQTT. |
| Username | `supar1-user` | Required if your broker enforces auth. |
| Password | `your-password` | Do not commit this value. |
| TLS enabled | `false` | Current ESP32 firmware does not yet implement TLS. |
| Rover ID | `supar1` | Must match backend and firmware topics. |

Default Su-Par1 topics:

```text
mars/supar1/mission
mars/supar1/command
mars/supar1/status
mars/supar1/telemetry
mars/supar1/log . . 
```

Expected broker behavior:

- Backend publishes missions to `mars/supar1/mission`.
- Firmware subscribes to `mars/supar1/mission` and `mars/supar1/command`.
- Firmware publishes status to `mars/supar1/status` every 2 seconds.
- Firmware publishes boot, ACK, mission, and error logs to `mars/supar1/log`.

## 2. Firmware `WifiMqttConfig.h` setup

Edit:

```text
/Users/nithin/Documents/mars_rover/firmware/include/WifiMqttConfig.h
```

Example:

```cpp
constexpr const char* WIFI_SSID = "YourWiFiName";
constexpr const char* WIFI_PASSWORD = "YourWiFiPassword";

constexpr const char* MQTT_BROKER_HOST = "your-broker-hostname-or-ip";
constexpr uint16_t MQTT_BROKER_PORT = 1883;
constexpr const char* MQTT_USERNAME = "supar1-user";
constexpr const char* MQTT_PASSWORD = "your-password";

constexpr const char* ROVER_ID = "supar1";
```

Important:

- Do not commit real Wi-Fi or MQTT credentials.
- The ESP32 must be on a network that can reach the MQTT broker.
- If using a local broker on your laptop, use the laptop LAN IP address, not
  `localhost`, because `localhost` on the ESP32 means the ESP32 itself.

## 3. PlatformIO upload steps

From the PlatformIO project folder:

```bash
cd /Users/nithin/Documents/mars_rover/mars_rover
pio run
pio run --target upload
pio device monitor --baud 115200
```

If another serial monitor is open, close it before uploading.

## 4. Backend environment setup

Open a new terminal:

```bash
cd /Users/nithin/Documents/mars_rover/mission_control/backend
source .venv/bin/activate
```

Set MQTT environment variables:

```bash
export MQTT_BROKER_HOST="your-broker-hostname-or-ip"
export MQTT_BROKER_PORT="1883"
export MQTT_USERNAME="supar1-user"
export MQTT_PASSWORD="your-password"
export MQTT_TLS_ENABLED="false"
export ROVER_ID="supar1"
```

For a broker with no username/password, leave them empty:

```bash
export MQTT_USERNAME=""
export MQTT_PASSWORD=""
```

## 5. Backend start command

Start FastAPI:

```bash
uvicorn app.main:app --reload --port 8000
```

In another terminal, verify the backend:

```bash
curl http://localhost:8000/health
curl http://localhost:8000/mqtt/status
```

Connect the backend to MQTT:

```bash
curl -X POST http://localhost:8000/mqtt/connect
```

Expected result:

```json
{
  "status": "connected",
  "connected": true
}
```

The response also includes broker, rover ID, and topic information.

## 6. Frontend start command

Open a second terminal:

```bash
cd /Users/nithin/Documents/mars_rover/mission_control/frontend
npm run dev
```

Open:

```text
http://localhost:5173
```

In the dashboard:

1. Select **Connect MQTT**.
2. Wait for MQTT status to show **Connected**.
3. Select **Refresh Rover Status**.
4. Send a mission button, such as **MISSION 1**.

Mission buttons should continue using `POST /mission`; the backend chooses MQTT
first, then USB serial fallback, then simulation.

## 7. Expected Serial Monitor logs

After boot:

```text
Mission to Mars 2050 - Su-Par1
Firmware v0.9 - MQTT Mission Mode
Awaiting commands...
```

During Wi-Fi/MQTT connection:

```text
WIFI_CONNECTING:<your-wifi-ssid>
WIFI_CONNECTED
MQTT_CONNECTED
```

When a mission is received from MQTT:

```text
ACK:MISSION_1
MISSION_START:MISSION_1
MISSION_COMPLETE:MISSION_1
```

If MQTT disconnects:

```text
ERR:MQTT_DISCONNECTED
```

If Wi-Fi disconnects:

```text
ERR:WIFI_DISCONNECTED
```

## 8. Expected dashboard status

After the ESP32 connects and publishes its birth/status messages, the dashboard
should show:

- Communication Mode: `MQTT`
- MQTT Status: `CONNECTED`
- Rover Status: `ONLINE`
- Firmware: `v0.9`
- Current Mission: `NONE`, or the active mission while moving
- Location: `base`
- Wi-Fi RSSI: a negative dBm value, for example `-54 dBm`
- Uptime: increasing over time
- Capabilities: `motors, rfid, line_sensor, servo, self_test, mqtt`
- Last MQTT Message: recent timestamp

You can also verify with:

```bash
curl http://localhost:8000/rover/status
```

Expected fields include:

```json
{
  "status": "online",
  "rover_id": "supar1",
  "firmware": "v0.9",
  "mission": "none",
  "state": "IDLE",
  "location": "base",
  "battery": 87,
  "wifi_rssi": -54,
  "uptime": 120,
  "line": "ON_TRACK",
  "rfid": "NONE",
  "servo": "CLOSED",
  "capabilities": ["motors", "rfid", "line_sensor", "servo", "self_test", "mqtt"],
  "last_seen": "server timestamp"
}
```

## 9. Troubleshooting

### Wi-Fi failed

Symptoms:

- Serial Monitor repeatedly shows `WIFI_CONNECTING:<ssid>`.
- Dashboard never shows rover online.

Checks:

- Confirm `WIFI_SSID` and `WIFI_PASSWORD` are correct.
- Confirm the ESP32 is in range of the router.
- Use a 2.4 GHz Wi-Fi network. Many ESP32 boards do not support 5 GHz Wi-Fi.
- Reboot the ESP32 after changing `WifiMqttConfig.h`.
- Verify the router allows new devices and does not require a captive portal.

### MQTT auth failed

Symptoms:

- Serial Monitor shows `ERR:MQTT_CONNECT_FAILED:<code>`.
- Backend may connect, but the rover does not.

Checks:

- Confirm `MQTT_USERNAME` and `MQTT_PASSWORD` match the broker.
- Confirm the firmware and backend use the same broker host and port.
- Confirm the broker allows the configured client to publish/subscribe to
  `mars/supar1/#`.
- If using HiveMQ Cloud, confirm the MQTT credentials are active and assigned
  to the correct cluster.

### TLS failed

Symptoms:

- Firmware cannot connect to a HiveMQ Cloud TLS-only port such as `8883`.
- Backend may connect with TLS, but the ESP32 does not.

Current limitation:

- Firmware v0.9 uses plain `WiFiClient` with `PubSubClient`.
- It does not yet use `WiFiClientSecure` or broker CA certificates.

Fix for this test:

- Use a plain MQTT listener on port `1883`, or a local broker that supports
  non-TLS MQTT.
- Keep backend `MQTT_TLS_ENABLED=false`.

Future firmware work can add TLS with `WiFiClientSecure`.

### Backend connected but rover offline

Symptoms:

- Dashboard MQTT Status is `CONNECTED`.
- Rover Status remains `OFFLINE` or `Unknown`.
- `/mqtt/status` shows backend connected, but `/rover/status` has no recent
  `last_seen`.

Checks:

- Confirm the ESP32 Serial Monitor shows `MQTT_CONNECTED`.
- Confirm firmware and backend both use `ROVER_ID=supar1`.
- Confirm topics match exactly:
  - `mars/supar1/status`
  - `mars/supar1/log`
- Confirm broker ACLs allow the rover to publish to `mars/supar1/status` and
  `mars/supar1/log`.
- Confirm backend is subscribed after pressing **Connect MQTT** or calling
  `POST /mqtt/connect`.

### Mission not received

Symptoms:

- Dashboard mission dispatch reports MQTT mode, but the rover does not move.
- Serial Monitor does not show `ACK:MISSION_X`.

Checks:

- Confirm the rover Serial Monitor shows `MQTT_CONNECTED`.
- Confirm backend publishes missions to `mars/supar1/mission`.
- Confirm firmware subscribes to `mars/supar1/mission`.
- Try publishing a plain command directly:

```bash
curl -X POST http://localhost:8000/mqtt/publish \
  -H "Content-Type: application/json" \
  -d '{"topic":"mars/supar1/mission","payload":"MISSION_1"}'
```

Try JSON:

```bash
curl -X POST http://localhost:8000/mqtt/publish \
  -H "Content-Type: application/json" \
  -d '{"topic":"mars/supar1/mission","payload":{"command":"MISSION_1"}}'
```

If direct publish works but dashboard buttons do not, check the browser
Activity Log and backend terminal logs. If neither works, check broker ACLs,
topic spelling, and rover MQTT connection state.
