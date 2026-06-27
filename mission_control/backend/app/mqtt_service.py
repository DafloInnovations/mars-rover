"""Thread-safe MQTT communication service for the Su-Par1 rover."""

from __future__ import annotations

import json
import logging
import os
import ssl
import time
import uuid
from datetime import UTC, datetime
from threading import RLock
from typing import Any

import paho.mqtt.client as mqtt

logger = logging.getLogger(__name__)

DEFAULT_BROKER_HOST = "localhost"
DEFAULT_BROKER_PORT = 1883
DEFAULT_ROVER_ID = "supar1"

_client: mqtt.Client | None = None
_connected = False
_subscribed_topics: set[str] = set()
_lock = RLock()
_process_suffix = f"{os.getenv('RAILWAY_REPLICA_ID') or os.getenv('RAILWAY_DEPLOYMENT_ID') or os.getpid()}-{uuid.uuid4().hex[:8]}"

last_status: dict[str, Any] = {
    "connected": False,
    "rover_id": DEFAULT_ROVER_ID,
    "status": "offline",
    "capabilities": [],
    "telemetry": None,
    "log": None,
    "last_seen": None,
}


def _mark_rover_online(timestamp: str) -> None:
    """Update common online/cache fields after rover-originated MQTT traffic."""

    last_status["connected"] = _connected
    last_status["status"] = "online"
    last_status["rover_id"] = _build_config()["rover_id"]
    last_status["last_seen"] = timestamp


def _utc_now() -> str:
    """Return an ISO-8601 timestamp for cache updates."""

    return datetime.now(UTC).isoformat()


def _parse_bool(value: str | None) -> bool:
    """Parse common environment-style boolean values."""

    return value is not None and value.strip().lower() in {"1", "true", "yes", "on"}


def _parse_payload(payload: bytes) -> Any:
    """Decode an MQTT payload, preserving JSON objects when possible."""

    text = payload.decode("utf-8", errors="replace")
    try:
        return json.loads(text)
    except json.JSONDecodeError:
        return text


def _build_config() -> dict[str, Any]:
    """Read MQTT connection settings from environment variables."""

    tls_enabled = _parse_bool(os.getenv("MQTT_TLS_ENABLED"))
    default_port = 8883 if tls_enabled else DEFAULT_BROKER_PORT

    return {
        "broker_host": os.getenv("MQTT_BROKER_HOST", DEFAULT_BROKER_HOST),
        "broker_port": int(os.getenv("MQTT_BROKER_PORT", str(default_port))),
        "username": os.getenv("MQTT_USERNAME"),
        "password": os.getenv("MQTT_PASSWORD"),
        "tls_enabled": tls_enabled,
        "rover_id": os.getenv("ROVER_ID", DEFAULT_ROVER_ID),
    }


def default_topics() -> dict[str, str]:
    """Return the default MQTT topic map for the configured rover."""

    rover_id = _build_config()["rover_id"]
    base_topic = f"mars/{rover_id}"

    return {
        "mission": f"{base_topic}/mission",
        "command": f"{base_topic}/command",
        "status": f"{base_topic}/status",
        "telemetry": f"{base_topic}/telemetry",
        "log": f"{base_topic}/log",
    }


def _new_client(rover_id: str) -> mqtt.Client:
    """Create a Paho MQTT client compatible with Paho 1.x and 2.x."""

    client_id = f"mission-control-{rover_id}-{_process_suffix}"
    try:
        client = mqtt.Client(
            mqtt.CallbackAPIVersion.VERSION2,
            client_id=client_id,
            clean_session=True,
        )
    except (AttributeError, TypeError):
        client = mqtt.Client(client_id=client_id, clean_session=True)

    client.reconnect_delay_set(min_delay=1, max_delay=30)
    return client


def _on_connect(client: mqtt.Client, _: Any, __: Any, reason_code: Any, *___: Any) -> None:
    """Subscribe to rover feedback topics after a successful broker connection."""

    global _connected

    success = int(reason_code) == 0 if isinstance(reason_code, int) else str(reason_code) == "Success"
    with _lock:
        _connected = success
        last_status["connected"] = success
        last_status["last_seen"] = _utc_now()

    if not success:
        logger.warning("MQTT connection rejected: %s", reason_code)
        return

    logger.info("MQTT connected")
    for topic in (
        default_topics()["status"],
        default_topics()["telemetry"],
        default_topics()["log"],
    ):
        client.subscribe(topic)
        with _lock:
            _subscribed_topics.add(topic)


def _on_disconnect(_: mqtt.Client, __: Any, reason_code: Any, *___: Any) -> None:
    """Update connection state when the broker disconnects."""

    global _connected

    with _lock:
        _connected = False
        last_status["connected"] = False
        last_status["status"] = "offline"
        last_status["last_seen"] = _utc_now()

    logger.info("MQTT disconnected: %s", reason_code)


def _on_message(_: mqtt.Client, __: Any, message: mqtt.MQTTMessage) -> None:
    """Cache the latest rover status, telemetry, and log MQTT messages."""

    topic = message.topic
    decoded_payload = _parse_payload(message.payload)
    topics = default_topics()

    cache_key: str | None = None
    if topic == topics["status"]:
        cache_key = "status"
    elif topic == topics["telemetry"]:
        cache_key = "telemetry"
    elif topic == topics["log"]:
        cache_key = "log"

    with _lock:
        timestamp = _utc_now()
        if topic == topics["status"]:
            if isinstance(decoded_payload, dict):
                last_status.update(decoded_payload)
                last_status["raw_status"] = dict(decoded_payload)
            else:
                last_status["raw_status"] = decoded_payload
            _mark_rover_online(timestamp)
        elif topic == topics["telemetry"]:
            last_status["telemetry"] = decoded_payload
            if isinstance(decoded_payload, dict):
                last_status.update(decoded_payload)
            _mark_rover_online(timestamp)
        elif topic == topics["log"]:
            last_status["log"] = decoded_payload
            if (
                isinstance(decoded_payload, dict)
                and decoded_payload.get("event") == "ROVER_ONLINE"
            ):
                last_status["status"] = "online"
                last_status["rover_id"] = decoded_payload.get("rover_id", _build_config()["rover_id"])
                last_status["firmware"] = decoded_payload.get("firmware", last_status.get("firmware"))
                last_status["capabilities"] = decoded_payload.get("capabilities", [])
                last_status["last_seen"] = timestamp
                last_status["connected"] = _connected
            else:
                _mark_rover_online(timestamp)
        elif cache_key is not None:
            last_status[cache_key] = decoded_payload
            _mark_rover_online(timestamp)

    logger.info("MQTT message received: topic=%s payload=%s", topic, decoded_payload)


def connect() -> dict[str, Any]:
    """Connect to the MQTT broker and subscribe to rover feedback topics."""

    global _client

    config = _build_config()
    with _lock:
        if _client is not None and _connected:
            return {
                "connected": True,
                "broker_host": config["broker_host"],
                "broker_port": config["broker_port"],
                "rover_id": config["rover_id"],
                "topics": default_topics(),
            }

        client = _new_client(config["rover_id"])
        client.on_connect = _on_connect
        client.on_disconnect = _on_disconnect
        client.on_message = _on_message

        if config["username"]:
            client.username_pw_set(config["username"], config["password"])

        if config["tls_enabled"]:
            client.tls_set(cert_reqs=ssl.CERT_REQUIRED)

        _client = client

    client.connect(config["broker_host"], config["broker_port"], keepalive=60)
    client.loop_start()

    deadline = time.monotonic() + 2.0
    while time.monotonic() < deadline:
        if is_connected():
            break
        time.sleep(0.05)

    with _lock:
        last_status["rover_id"] = config["rover_id"]

    return {
        "connected": is_connected(),
        "broker_host": config["broker_host"],
        "broker_port": config["broker_port"],
        "rover_id": config["rover_id"],
        "topics": default_topics(),
    }


def ensure_connected() -> bool:
    """Best-effort reconnect used by read/dispatch endpoints after restarts."""

    if is_connected():
        return True

    try:
        connection = connect()
    except (OSError, ValueError, RuntimeError) as error:
        logger.warning("MQTT auto-reconnect failed: %s", error)
        return False

    return bool(connection["connected"])


def disconnect() -> None:
    """Disconnect from the MQTT broker, if connected."""

    global _client, _connected

    with _lock:
        client = _client
        _client = None
        _connected = False
        _subscribed_topics.clear()
        last_status["connected"] = False
        last_status["status"] = "offline"
        last_status["last_seen"] = _utc_now()

    if client is not None:
        client.loop_stop()
        client.disconnect()


def publish(topic: str, payload: Any) -> dict[str, Any]:
    """Publish a payload to an MQTT topic."""

    normalized_topic = topic.strip()
    if not normalized_topic:
        raise ValueError("An MQTT topic is required.")

    with _lock:
        if _client is None or not _connected:
            raise ConnectionError("MQTT broker connection is not active.")
        client = _client

    encoded_payload = payload if isinstance(payload, str) else json.dumps(payload)
    result = client.publish(normalized_topic, encoded_payload, qos=1)
    result.wait_for_publish(timeout=2)

    if result.rc != mqtt.MQTT_ERR_SUCCESS:
        raise RuntimeError(f"MQTT publish failed with result code {result.rc}.")

    return {
        "status": "published",
        "topic": normalized_topic,
        "payload": payload,
    }


def subscribe(topic: str) -> dict[str, Any]:
    """Subscribe to one MQTT topic."""

    normalized_topic = topic.strip()
    if not normalized_topic:
        raise ValueError("An MQTT topic is required.")

    with _lock:
        if _client is None or not _connected:
            raise ConnectionError("MQTT broker connection is not active.")
        client = _client

    result, mid = client.subscribe(normalized_topic)
    if result != mqtt.MQTT_ERR_SUCCESS:
        raise RuntimeError(f"MQTT subscribe failed with result code {result}.")

    with _lock:
        _subscribed_topics.add(normalized_topic)

    return {
        "status": "subscribed",
        "topic": normalized_topic,
        "message_id": mid,
    }


def is_connected() -> bool:
    """Return whether an MQTT broker connection is active."""

    with _lock:
        return _client is not None and _connected


def status() -> dict[str, Any]:
    """Return MQTT connection metadata without exposing credentials."""

    config = _build_config()
    with _lock:
        return {
            "connected": is_connected(),
            "broker_host": config["broker_host"],
            "broker_port": config["broker_port"],
            "tls_enabled": config["tls_enabled"],
            "rover_id": config["rover_id"],
            "topics": default_topics(),
            "subscriptions": sorted(_subscribed_topics),
            "last_status": dict(last_status),
        }


def rover_status() -> dict[str, Any]:
    """Return the latest cached rover status, telemetry, and log messages."""

    with _lock:
        return dict(last_status)
