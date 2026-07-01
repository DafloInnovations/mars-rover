# Mission to Mars 2050 — Su-Par1

Professional ESP32 firmware for the **Su-Par1** two-wheel differential-drive
rover. Firmware v0.9 adds Wi-Fi, MQTT mission dispatch, autonomous 3-channel
line following, and RFID checkpoint mission stops while preserving SG90 servo,
RC522 RFID diagnostics, hardware self-test, and direct serial motor commands.

Su-Par1 currently uses a straight-line checkpoint layout:

```text
BASE → FOOD → MEDICINE → OXYGEN → HABITAT
```

Checkpoint order is fixed:

| Checkpoint | Index |
|---|---:|
| `BASE` | 0 |
| `FOOD` | 1 |
| `MEDICINE` | 2 |
| `OXYGEN` | 3 |
| `HABITAT` | 4 |

Navigation direction is selected from the current checkpoint and active target:

- `FORWARD_ROUTE` when target index is greater than current index
- `REVERSE_ROUTE` when target index is less than current index
- immediate stop/completion handling when target index equals current index

`MISSION_5` and `RETURN_BASE` use `REVERSE_ROUTE` when the rover is at FOOD,
MEDICINE, OXYGEN, or HABITAT. In reverse route mode, centered line readings use
`motorController.backward()` and left/right corrections are inverted.

Amazon Bedrock is implemented in Mission Control, not in ESP32 firmware.

## Project structure

```text
mars_rover/
├── firmware/
│   ├── include/
│   │   └── WifiMqttConfig.h     # Placeholder Wi-Fi/MQTT configuration
│   ├── main.cpp                 # PlatformIO application entry point
│   ├── main/
│   │   └── main.ino             # Arduino IDE compatibility wrapper
│   ├── motors/
│   │   ├── MotorController.h     # Motor-controller interface
│   │   └── MotorController.cpp   # H-bridge motor-control implementation
│   ├── mqtt/
│   │   ├── MqttClientManager.h   # Wi-Fi/MQTT connection manager
│   │   └── MqttClientManager.cpp # MQTT reconnect, publish, and subscribe
│   ├── rfid/
│   │   ├── RFIDReader.h          # RC522 reader interface
│   │   └── RFIDReader.cpp        # SPI UID-reading implementation
│   ├── sensors/
│   │   ├── LineSensor.h          # Three-channel line-sensor interface
│   │   └── LineSensor.cpp        # Left/center/right digital input implementation
│   └── servo/
│       ├── CargoServo.h          # SG90 cargo mechanism interface
│       └── CargoServo.cpp        # ESP32 PWM servo implementation
├── documentation/               # Design and hardware documentation
└── README.md
```

## Architecture

`MotorController` encapsulates all L298N direction logic. The Su-Par1
application injects the four GPIO assignments through the constructor, so the
class does not contain board-specific pin numbers and can be reused with
alternate wiring.

The public motor API is:

- `begin()` — configures GPIO outputs and establishes a safe stopped state
- `forward()` — drives both motors forward
- `backward()` — drives both motors in reverse
- `left()` — performs an in-place left pivot
- `right()` — performs an in-place right pivot
- `stop()` — removes the direction command from both motors

## Default GPIO assignment

Firmware v0.9 defines these L298N assignments in `firmware/main.cpp`:

| Signal | ESP32 GPIO |
|---|---:|
| Left motor IN1 | 25 |
| Left motor IN2 | 26 |
| Right motor IN3 | 27 |
| Right motor IN4 | 14 |

These values are application configuration, not part of `MotorController`.
Update them to match the selected ESP32 board and motor-driver wiring before
powering the motors.

## Serial command mode

On boot, Su-Par1 stops both motors and prints:

```text
Mission to Mars 2050 - Su-Par1
Firmware v0.9 - MQTT Mission Mode
Awaiting commands...
```

Commands are newline-delimited and accepted at 115200 baud:

| Command | Behavior |
|---|---|
| `FORWARD` | Drive both motors forward |
| `BACKWARD` | Drive both motors backward |
| `LEFT` | Pivot left |
| `RIGHT` | Pivot right |
| `STOP` | Stop both motors |
| `TEST` | Run the legacy v0.2 movement sequence once |
| `LINE_TEST` | Print left, center, and right line channels every 300 ms for 20 seconds |
| `LINE_FOLLOW_START` | Enable continuous autonomous 3-channel line following |
| `LINE_FOLLOW_STOP` | Disable autonomous line following and stop the motors |
| `LINE_FOLLOW_TEST` | Run autonomous line following for 20 seconds, then stop |
| `RFID_TEST` | Scan for RC522 cards and print detected UIDs for 20 seconds |
| `SERVO_OPEN` | Move the SG90 cargo servo to 90 degrees |
| `SERVO_CLOSE` | Move the SG90 cargo servo to 0 degrees |
| `SERVO_TEST` | Open, wait one second, then close |
| `SELF_TEST` | Run the combined motor, line, RFID, and servo diagnostic |
| `MISSION_1` | Pick up FOOD, then deliver it to HABITAT |
| `MISSION_2` | Pick up MEDICINE, then deliver it to HABITAT |
| `MISSION_3` | Pick up OXYGEN, then deliver it to HABITAT |
| `MISSION_4` | Go directly to HABITAT |
| `MISSION_5` | Return to BASE |

Valid commands return `ACK:<COMMAND>`. Unknown commands return
`ERR:UNKNOWN_COMMAND`. Input is case-insensitive and accepts either LF or CRLF
line endings. Mission commands additionally emit `MISSION_START`, RFID
checkpoint, and `MISSION_COMPLETE` lifecycle events.

Example session:

```text
FORWARD
ACK:FORWARD
STOP
ACK:STOP
TEST
TEST:FORWARD
TEST:STOP
TEST:BACKWARD
TEST:STOP
TEST:LEFT
TEST:STOP
TEST:RIGHT
TEST:STOP
ACK:TEST
```

The rover does not move automatically after boot.

## Mission command examples

Mission commands are acknowledged before line following begins. Delivery
missions stop briefly at the pickup checkpoint, then continue to HABITAT.
Completion is reported only after the final destination is detected and the
rover has stopped:

```text
MISSION_2
ACK:MISSION_2
MISSION_START:MISSION_2
TARGET_CHECKPOINT:MEDICINE
RFID_CHECKPOINT:FOOD
CONTINUE_TO:MEDICINE
RFID_CHECKPOINT:MEDICINE
PICKUP_COMPLETE:MEDICINE
RFID_CHECKPOINT:OXYGEN
CONTINUE_TO:HABITAT
RFID_CHECKPOINT:HABITAT
MISSION_COMPLETE:MISSION_2
```

```text
MISSION_5
ACK:MISSION_5
MISSION_START:MISSION_5
TARGET_CHECKPOINT:BASE
RFID_CHECKPOINT:BASE
MISSION_COMPLETE:MISSION_5
```

These missions use the straight black line and RFID checkpoint matching only.
They do not perform junction routing, obstacle detection, or map-based route
planning.

## Wi-Fi and MQTT configuration

Firmware v0.9 uses `WiFi.h` and `PubSubClient` so Mission Control can dispatch
missions over MQTT. Edit the placeholder file
`firmware/include/WifiMqttConfig.h` before uploading to hardware:

```cpp
constexpr const char* WIFI_SSID = "YOUR_WIFI_SSID";
constexpr const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

constexpr const char* MQTT_BROKER_HOST = "192.168.1.100";
constexpr uint16_t MQTT_BROKER_PORT = 1883;
constexpr const char* MQTT_USERNAME = "";
constexpr const char* MQTT_PASSWORD = "";

constexpr const char* ROVER_ID = "supar1";
```

Do not commit real Wi-Fi or broker credentials. Keep local secrets on the
development machine only.

On boot, Su-Par1 initializes all existing hardware modules, connects to Wi-Fi,
connects to the MQTT broker, and subscribes to the mission and command topics.
If Wi-Fi or MQTT disconnects, the firmware stops the motors, keeps Serial
commands available, and automatically retries the network connection.

## MQTT topics

For `ROVER_ID = "supar1"`, firmware v0.9 uses:

| Topic | Direction | Purpose |
|---|---|---|
| `mars/supar1/mission` | Backend → rover | Mission dispatch such as `MISSION_3` |
| `mars/supar1/command` | Backend → rover | Direct rover command such as `STOP` |
| `mars/supar1/status` | Rover → backend | Online state published every 2 seconds |
| `mars/supar1/telemetry` | Rover → backend | Reserved for future telemetry |
| `mars/supar1/log` | Rover → backend | Boot, ACK, mission, and error logs |

The rover subscribes to:

- `mars/supar1/mission`
- `mars/supar1/command`

The rover publishes status JSON every 2 seconds:

```json
{
  "rover_id": "supar1",
  "firmware": "v0.9",
  "mission": "MISSION_3",
  "state": "RUNNING",
  "location": "BASE",
  "battery": 87,
  "wifi_rssi": -45,
  "uptime": 120,
  "line": "ON_TRACK",
  "line_follow": "enabled",
  "line_state": "CENTER",
  "rfid": "NONE",
  "servo": "CLOSED",
  "timestamp": 120
}
```

## MQTT command payload examples

Mission payloads can be plain text:

```text
MISSION_3
```

Or JSON:

```json
{"command":"MISSION_3"}
```

Direct command topic payloads use the same command handler as Serial:

```json
{"command":"STOP"}
```

```text
SELF_TEST
```

When MQTT commands are received, the firmware executes the same normalized
command path used by the Serial Monitor. Supported commands, ACK behavior, and
RFID checkpoint mission behavior remain the same as Serial command mode. MQTT ACK,
mission lifecycle, boot, Wi-Fi, MQTT, and error events are published to
`mars/supar1/log`.

## Mission Control backend communication

The local FastAPI Mission Control backend publishes mission requests to
`mars/supar1/mission`. For mission buttons, it sends JSON such as:

```json
{
  "rover_id": "supar1",
  "mission_id": 3,
  "mission_name": "Deliver Oxygen to Habitat",
  "command": "MISSION_3"
}
```

The backend subscribes to `mars/supar1/status`, `mars/supar1/telemetry`, and
`mars/supar1/log`, then caches the latest rover state for the dashboard. USB
serial remains available as a development fallback and for direct diagnostics.

## Three-channel line sensor wiring

Connect the BFD-1000 middle three digital outputs to the ESP32 as follows:

| BFD-1000 signal | Firmware role | ESP32 GPIO |
|---|---|---:|
| S2 | Left | 32 |
| S3 | Center | 33 |
| S4 | Right | 21 |
| GND | GND | GND |
| VCC | VCC | Module-compatible supply |

The firmware uses plain `INPUT` mode and expects the line sensor module to
drive each digital channel.

The current configuration treats digital `1` (`HIGH`) as black. Verify this
with the actual module before autonomous line-following work; if its comparator
polarity is reversed, change `kLineSensorBlackState` in `firmware/main.cpp` to
`LOW`. `LineSensor::isJunction()` returns true only when all three channels
equal the configured black state.

## Autonomous line-following commands

The line follower samples the left, center, and right digital channels every
40 milliseconds without blocking MQTT or serial command handling.

| Reading | Meaning | Motor action | Serial log on state change |
|---|---|---|---|
| `L=0,C=1,R=0` | Line centered | Forward | `LINE:FOLLOWING_CENTER` |
| `L=1,C=1,R=0` or `L=1,C=0,R=0` | Line is left | Correct left | `LINE:CORRECT_LEFT` |
| `L=0,C=1,R=1` or `L=0,C=0,R=1` | Line is right | Correct right | `LINE:CORRECT_RIGHT` |
| `L=1,C=1,R=1` | Wide line/checkpoint area | Continue forward | `LINE:JUNCTION` |
| `L=0,C=0,R=0` | Line lost | Stop | `LINE:LOST` |

`LINE_FOLLOW_START` keeps autonomous following enabled until
`LINE_FOLLOW_STOP`, `STOP`, a manual motor command, or diagnostics take
control. `LINE_FOLLOW_TEST` runs the same follower for 20 seconds and then
stops automatically. Mission commands automatically enable line following and
use RFID checkpoints to decide when to stop.

MQTT status includes:

- `line_follow`: `enabled` or `disabled`
- `line_state`: `CENTER`, `LEFT`, `RIGHT`, `JUNCTION`, or `LOST`

## LINE_TEST usage

Send `LINE_TEST` with a newline at 115200 baud. Su-Par1 stops its motors,
acknowledges the command, and prints one sample every 300 milliseconds for 20
seconds:

```text
LINE_TEST
ACK:LINE_TEST
LINE:L=0,C=1,R=0
LINE:L=0,C=1,R=1
...
LINE_TEST_COMPLETE
```

This diagnostic does not steer the rover or modify any active mission.

## RC522 wiring

Connect the RC522 to the ESP32 SPI bus as follows:

| RC522 signal | ESP32 connection |
|---|---:|
| SDA / SS | GPIO5 |
| SCK | GPIO18 |
| MOSI | GPIO23 |
| MISO | GPIO19 |
| RST | GPIO22 |
| GND | GND |
| 3.3V | 3.3V |

**Power the RC522 from 3.3 V only. Do not connect its power input to 5 V.**
GPIO5 is also an ESP32 boot-strapping pin; if boot reliability is affected,
disconnect the reader while flashing and verify that the RC522 module is not
forcing SS low during reset.

The firmware uses the PlatformIO dependency
`miguelbalboa/MFRC522@^1.4.12`.

## RFID_TEST usage

Send `RFID_TEST` with a newline at 115200 baud. Su-Par1 stops its motors and
scans for newly presented RC522-compatible cards or tags for 20 seconds:

```text
RFID_TEST
ACK:RFID_TEST
RFID:UID=5371C0FF220001,CHECKPOINT=BASE
RFID:UID=5372C0FF220001,CHECKPOINT=FOOD
RFID:UID=04A1B2C3D4,CHECKPOINT=UNKNOWN
RFID_TEST_COMPLETE
```

UIDs are normalized before comparison by removing spaces, removing colons, and
forcing uppercase. Presenting a tag again after removing it allows it to be
detected again.

## Straight-line RFID checkpoint missions

The physical course is one black line with RFID checkpoints in this order:

```text
BASE → FOOD → MEDICINE → OXYGEN → HABITAT
```

Mission commands map to pickup and delivery checkpoints:

| Mission | Cargo | Pickup checkpoint | Delivery / destination checkpoint | Completion condition |
|---|---|---|---|---|
| `MISSION_1` | `FOOD` | `FOOD` | `HABITAT` | Complete at HABITAT after food pickup |
| `MISSION_2` | `MEDICINE` | `MEDICINE` | `HABITAT` | Complete at HABITAT after medicine pickup |
| `MISSION_3` | `OXYGEN` | `OXYGEN` | `HABITAT` | Complete at HABITAT after oxygen pickup |
| `MISSION_4` | `NONE` | `NONE` | `HABITAT` | Complete at HABITAT |
| `MISSION_5` | `NONE` | `NONE` | `BASE` | Complete at BASE |
| `RETURN_BASE` | `NONE` | `NONE` | `BASE` | Alias for `MISSION_5` |

Mission phases published in MQTT status are:

- `IDLE`
- `GOING_TO_PICKUP`
- `PICKUP_PAUSE`
- `GOING_TO_DELIVERY`
- `DELIVERED`
- `RETURNING_BASE`

When a mission starts, Su-Par1 sets the pickup/delivery checkpoints, enables
autonomous line following, and scans RFID while moving. If a checkpoint is
detected before the active target, the rover logs it and continues:

```text
RFID_CHECKPOINT:FOOD
CONTINUE_TO:OXYGEN
```

When the pickup checkpoint is detected, Su-Par1 stops for a one-second
non-blocking pause, updates `cargo_status`, publishes MQTT status, and resumes
line following toward HABITAT:

```text
RFID_CHECKPOINT:OXYGEN
PICKUP_COMPLETE:OXYGEN
```

When the delivery/destination checkpoint is detected, Su-Par1 stops, disables
line following, publishes an `IDLE` status with `location` set to the checkpoint,
and prints:

```text
RFID_CHECKPOINT:HABITAT
MISSION_COMPLETE:MISSION_3
```

MQTT status includes mission/cargo fields:

- `mission_phase`
- `mission_complete`
- `navigation_direction`
- `cargo`
- `cargo_status`
- `pickup_checkpoint`
- `delivery_checkpoint`
- `target_checkpoint`
- `current_checkpoint`
- `location`

Checkpoint UIDs are configured in `firmware/main.cpp`:

| Checkpoint | UID |
|---|---|
| `BASE` | `5371C0FF220001` |
| `FOOD` | `5372C0FF220001` |
| `MEDICINE` | `536BC0FF220001` |
| `OXYGEN` | `5368C0FF220001` |
| `HABITAT` | `5369C0FF220001` |

Unmapped tags report `CHECKPOINT=UNKNOWN` during `RFID_TEST` and
`RFID_CHECKPOINT:UNKNOWN` during line-following missions. Unknown tags do not
stop the rover unless the active mission target is also `UNKNOWN`, which normal
mission commands never set.

Recommended test sequence:

1. Run `LINE_TEST` and confirm `LINE:L=0,C=1,R=0` when centered over the line.
2. Run `LINE_FOLLOW_TEST` with the wheels lifted or the rover on a safe test
   track.
3. Run `RFID_TEST` and confirm each tag reports the expected checkpoint.
4. Upload firmware and send one mission command, such as `MISSION_3`.

## SG90 servo wiring

Connect the cargo-mechanism SG90 as follows:

| SG90 wire | Connection |
|---|---|
| Signal (orange/yellow) | ESP32 GPIO13 |
| Power (red) | Regulated 5 V servo supply |
| Ground (brown/black) | Servo supply ground and ESP32 GND |

Use a suitable external regulated 5 V supply for the servo rather than drawing
motor current from an ESP32 GPIO or its 3.3 V rail. The external supply and
ESP32 must share a common ground. A bulk capacitor near the servo supply can
help prevent ESP32 resets caused by SG90 current spikes.

The firmware uses `madhephaestus/ESP32Servo@^3.0.7`. On startup the servo is
attached at 50 Hz and commanded to its safe closed position of 0 degrees.

## Servo diagnostic usage

```text
SERVO_OPEN
ACK:SERVO_OPEN
SERVO_CLOSE
ACK:SERVO_CLOSE
SERVO_TEST
ACK:SERVO_TEST
```

`SERVO_OPEN` commands 90 degrees, `SERVO_CLOSE` commands 0 degrees, and
`SERVO_TEST` opens the mechanism, waits one second, and closes it. Servo state
does not change any `MISSION_1` through `MISSION_5` sequence in v0.9.

## SELF_TEST usage

Send `SELF_TEST` at 115200 baud to run the complete hardware sequence:

1. Print `SELF_TEST_START`.
2. Drive forward, backward, left, and right for one second each, stopping
   between movements.
3. Print one BFD-1000 line-sensor reading.
4. Scan for RC522 tags for five seconds and print any detected UIDs.
5. Open the SG90, wait one second, and close it.
6. Print `SELF_TEST_COMPLETE` and `ACK:SELF_TEST`.

Example output:

```text
SELF_TEST
SELF_TEST_START
LINE:L=0,C=1,R=0
RFID:UID=04A1B2C3D4
SELF_TEST_COMPLETE
ACK:SELF_TEST
```

If a module did not initialize, the firmware prints a warning such as
`WARN:RFID_NOT_INITIALIZED` and continues testing the remaining modules.
Possible warnings are:

- `WARN:MOTOR_NOT_INITIALIZED`
- `WARN:LINE_SENSOR_NOT_INITIALIZED`
- `WARN:RFID_NOT_INITIALIZED`
- `WARN:SERVO_NOT_INITIALIZED`

The self-test does not modify mission behavior.

## TEST sequence

The `TEST` command runs the former v0.2 sequence once:

1. Forward for 2 seconds
2. Stop for 1 second
3. Backward for 2 seconds
4. Stop for 1 second
5. Left pivot for 2 seconds
6. Stop for 1 second
7. Right pivot for 2 seconds
8. Stop for 2 seconds

## Hardware assumptions

The implementation targets an L298N dual H-bridge motor driver with two
digital direction inputs per motor. The L298N ENA and ENB inputs must be
enabled using the module jumpers or suitable external control.

Before operation:

- Confirm that every selected GPIO is output-capable on the target ESP32.
- Use a motor power supply suitable for the motors and driver.
- Connect the ESP32 ground and motor-driver logic ground.
- Do not power motors directly from ESP32 GPIO pins.
- Verify motor polarity with the rover lifted so its wheels can rotate safely.

If a motor rotates opposite to the expected direction, swap that motor's two
driver outputs or reverse its logical input mapping in the application wiring.

## Building with Arduino ESP32

The included PlatformIO project is configured for the `esp32dev` board and
uses `firmware/` as its source directory. Firmware v0.9 depends on MFRC522,
ESP32Servo, and PubSubClient as declared in `mars_rover/platformio.ini`:

```bash
cd mars_rover
pio run
pio run --target upload
pio device monitor --baud 115200
```

In the serial monitor, select a newline or CRLF line ending before sending
commands.

`firmware/main.cpp` is the PlatformIO application entry point. The nested
`firmware/main/main.ino` file is retained only as an Arduino IDE compatibility
wrapper.

## Safety behavior

`MotorController::begin()` configures all four control pins and immediately
calls `stop()`. This minimizes unintended motor movement during startup. The
current stop command drives every H-bridge input LOW, which commonly selects
coast mode; verify the exact behavior in the motor-driver data sheet.

## Future extension

Additional capabilities can be introduced as independent modules under
`firmware/` without coupling them to motor direction logic. Shared interfaces
and configuration declarations can be placed in `firmware/include`.
