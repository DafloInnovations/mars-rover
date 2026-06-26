"""FastAPI application for the local Su-Par1 Mission Control dashboard."""

import json
import logging
import os
from contextlib import asynccontextmanager
from typing import Any

from dotenv import load_dotenv
from fastapi import FastAPI
from fastapi import HTTPException
from fastapi.middleware.cors import CORSMiddleware
from serial import SerialException

from . import mqtt_service
from . import serial_service
from .models import (
    AiMissionPlannerRequest,
    MissionRequest,
    MissionResponse,
    MqttPublishRequest,
    SerialCommandRequest,
    SerialConnectRequest,
    VoiceCommandRequest,
)

load_dotenv()

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s | %(levelname)s | %(name)s | %(message)s",
)
logger = logging.getLogger(__name__)

MISSION_COMMANDS = {
    1: "MISSION_1",
    2: "MISSION_2",
    3: "MISSION_3",
    4: "MISSION_4",
    5: "MISSION_5",
}

VOICE_INTENTS = (
    ("oxygen", "deliver_oxygen", 3, "Deliver Oxygen to Habitat"),
    ("medicine", "deliver_medicine", 2, "Deliver Medicine to Habitat"),
    ("food", "deliver_food", 1, "Deliver Food to Habitat"),
    ("research", "collect_research_sample", 4, "Collect Research Sample"),
    ("sample", "collect_research_sample", 4, "Collect Research Sample"),
    ("rock", "collect_research_sample", 4, "Collect Research Sample"),
    ("return", "return_to_base", 5, "Return to Base"),
    ("base", "return_to_base", 5, "Return to Base"),
)

MISSION_REASONS = {
    1: "Food supplies are needed, so Su-Par1 should deliver food from WH-A to the Habitat.",
    2: "An astronaut needs medicine, so Su-Par1 should deliver medicine from WH-B to the Habitat.",
    3: "Habitat oxygen is low, so Su-Par1 should deliver oxygen from WH-C.",
    4: "A research sample is needed, so Su-Par1 should collect a research sample.",
    5: "The rover should return safely to Base.",
}

MISSION_NAMES = {
    1: "Deliver Food to Habitat",
    2: "Deliver Medicine to Habitat",
    3: "Deliver Oxygen to Habitat",
    4: "Collect Research Sample",
    5: "Return to Base",
}

DEFAULT_BEDROCK_MODEL_ID = "anthropic.claude-3-haiku-20240307-v1:0"


@asynccontextmanager
async def lifespan(_: FastAPI):
    """Release rover communication resources cleanly when the API shuts down."""

    yield
    mqtt_service.disconnect()
    serial_service.disconnect()


app = FastAPI(
    title="Mission to Mars 2050 Mission Control",
    description="Local mission dispatcher for the Su-Par1 rover using MQTT with serial fallback.",
    version="0.3.0",
    lifespan=lifespan,
)

frontend_origin = os.getenv("FRONTEND_ORIGIN", "").strip()
allowed_origins = ["http://localhost:5173", "http://127.0.0.1:5173"]
if frontend_origin:
    allowed_origins.append(frontend_origin)

# Vite serves the local development UI from port 5173. FRONTEND_ORIGIN allows
# the deployed Vercel frontend to call the Railway backend.
app.add_middleware(
    CORSMiddleware,
    allow_origins=allowed_origins,
    allow_credentials=True,
    allow_methods=["GET", "POST", "OPTIONS"],
    allow_headers=["*"],
)


@app.get("/health")
def health() -> dict[str, str]:
    """Report that the local Mission Control API is available."""

    return {"status": "ok"}


def dispatch_mission(mission: MissionRequest) -> MissionResponse:
    """Dispatch a mission over MQTT, falling back to serial or simulation."""

    command = MISSION_COMMANDS[mission.mission_id]
    logger.info(
        "Mission selected: id=%s name=%s command=%s",
        mission.mission_id,
        mission.mission_name,
        command,
    )

    if mqtt_service.is_connected():
        mission_topic = mqtt_service.default_topics()["mission"]
        mission_payload = {
            "rover_id": mqtt_service.status()["rover_id"],
            "mission_id": mission.mission_id,
            "mission_name": mission.mission_name,
            "command": command,
        }
        try:
            mqtt_service.publish(mission_topic, mission_payload)
        except (ConnectionError, RuntimeError, OSError, ValueError) as error:
            logger.error("Mission MQTT publish failed; checking serial fallback: %s", error)
        else:
            return MissionResponse(
                status="sent",
                command=command,
                mode="mqtt",
                mission_id=mission.mission_id,
                mission_name=mission.mission_name,
                message=f"{command} published to Su-Par1 over MQTT.",
            )

    if serial_service.is_connected():
        try:
            serial_service.send_command(command)
        except (ConnectionError, SerialException, OSError) as error:
            logger.error("Mission serial transmission failed: %s", error)
            raise HTTPException(status_code=503, detail=str(error)) from error

        return MissionResponse(
            status="sent",
            command=command,
            mode="serial",
            mission_id=mission.mission_id,
            mission_name=mission.mission_name,
            message=f"{command} sent to Su-Par1 over serial fallback.",
        )

    logger.info("Simulated rover transmission: %s", command)

    return MissionResponse(
        status="simulated",
        command=command,
        mode="simulated",
        mission_id=mission.mission_id,
        mission_name=mission.mission_name,
        message=f"{command} queued in simulated mode; MQTT and serial are unavailable.",
    )


def match_voice_intent(text: str) -> tuple[str, int, str] | None:
    """Map recognized speech to one of the five current rover missions."""

    normalized_text = text.casefold()
    for keyword, intent, mission_id, mission_name in VOICE_INTENTS:
        if keyword in normalized_text:
            return intent, mission_id, mission_name
    return None


def validate_ai_plan(plan: dict[str, Any]) -> dict[str, Any]:
    """Validate and normalize a Bedrock mission plan."""

    mission_id = int(plan["mission_id"])
    if mission_id not in MISSION_COMMANDS:
        raise ValueError("Bedrock returned an unsupported mission_id.")

    command = str(plan["command"]).strip().upper()
    expected_command = MISSION_COMMANDS[mission_id]
    if command != expected_command:
        raise ValueError("Bedrock returned a command that does not match mission_id.")

    intent = str(plan["intent"]).strip()
    reason = str(plan["reason"]).strip()
    if not intent or not reason:
        raise ValueError("Bedrock returned an incomplete mission plan.")

    return {
        "intent": intent,
        "mission_id": mission_id,
        "command": expected_command,
        "reason": reason,
    }


def plan_with_bedrock(text: str) -> dict[str, Any]:
    """Use Amazon Bedrock to classify a natural-language mission request."""

    import boto3

    region = os.getenv("AWS_REGION", "us-east-1")
    model_id = os.getenv("BEDROCK_MODEL_ID", DEFAULT_BEDROCK_MODEL_ID)
    client = boto3.client("bedrock-runtime", region_name=region)

    prompt = f"""
You are the mission planner for the Su-Par1 Mars rover.
Classify the operator request into exactly one of these missions:

MISSION_1 = Deliver Food to Habitat
MISSION_2 = Deliver Medicine to Habitat
MISSION_3 = Deliver Oxygen to Habitat
MISSION_4 = Collect Research Sample
MISSION_5 = Return to Base

Return strict JSON only. Do not include markdown or extra text.
JSON schema:
{{
  "intent": "deliver_food|deliver_medicine|deliver_oxygen|collect_research_sample|return_to_base",
  "mission_id": 1,
  "command": "MISSION_1",
  "reason": "short explanation"
}}

Operator request: {text}
""".strip()

    body = {
        "anthropic_version": "bedrock-2023-05-31",
        "max_tokens": 250,
        "temperature": 0,
        "messages": [
            {
                "role": "user",
                "content": [{"type": "text", "text": prompt}],
            },
        ],
    }
    response = client.invoke_model(
        modelId=model_id,
        body=json.dumps(body),
        contentType="application/json",
        accept="application/json",
    )
    payload = json.loads(response["body"].read())
    text_response = payload["content"][0]["text"].strip()
    return validate_ai_plan(json.loads(text_response))


def fallback_rule_plan(text: str) -> dict[str, Any] | None:
    """Build an AI-planner-shaped response from rule-based matching."""

    matched_intent = match_voice_intent(text)
    if matched_intent is None:
        return None

    intent, mission_id, _ = matched_intent
    return {
        "intent": intent,
        "mission_id": mission_id,
        "command": MISSION_COMMANDS[mission_id],
        "reason": MISSION_REASONS[mission_id],
    }


@app.get("/mqtt/status")
def get_mqtt_status() -> dict[str, Any]:
    """Return MQTT connection state, topics, subscriptions, and cached rover data."""

    return mqtt_service.status()


@app.post("/mqtt/connect")
def connect_mqtt() -> dict[str, Any]:
    """Connect Mission Control to the configured MQTT broker."""

    try:
        connection = mqtt_service.connect()
    except (OSError, ValueError, RuntimeError) as error:
        logger.warning("MQTT connection failed: %s", error)
        raise HTTPException(status_code=400, detail=str(error)) from error

    logger.info(
        "MQTT connect requested: broker=%s:%s rover_id=%s",
        connection["broker_host"],
        connection["broker_port"],
        connection["rover_id"],
    )
    return {"status": "connected" if connection["connected"] else "connecting", **connection}


@app.post("/mqtt/disconnect")
def disconnect_mqtt() -> dict[str, str | bool]:
    """Disconnect Mission Control from the MQTT broker."""

    was_connected = mqtt_service.is_connected()
    mqtt_service.disconnect()
    logger.info("MQTT disconnected")
    return {"status": "disconnected", "was_connected": was_connected}


@app.post("/mqtt/publish")
def publish_mqtt(request: MqttPublishRequest) -> dict[str, Any]:
    """Publish an operator-supplied payload to an MQTT topic."""

    try:
        result = mqtt_service.publish(request.topic, request.payload)
    except ConnectionError as error:
        raise HTTPException(status_code=409, detail=str(error)) from error
    except (ValueError, RuntimeError, OSError) as error:
        logger.warning("MQTT publish failed: %s", error)
        raise HTTPException(status_code=400, detail=str(error)) from error

    logger.info("MQTT published: topic=%s", result["topic"])
    return result


@app.get("/rover/status")
def get_rover_status() -> dict[str, Any]:
    """Return the latest rover status cached from MQTT feedback topics."""

    return mqtt_service.rover_status()


@app.get("/serial/ports")
def get_serial_ports() -> dict[str, list[dict[str, Any]]]:
    """List serial devices available for an ESP32 connection."""

    return {"ports": serial_service.list_ports()}


@app.post("/serial/connect")
def connect_serial(request: SerialConnectRequest) -> dict[str, Any]:
    """Connect Mission Control to the selected ESP32 serial port."""

    try:
        connection = serial_service.connect(request.port, request.baud)
    except (SerialException, OSError, ValueError) as error:
        logger.warning("Serial connection failed for %s: %s", request.port, error)
        raise HTTPException(status_code=400, detail=str(error)) from error

    logger.info(
        "Serial connected: port=%s baud=%s",
        connection["port"],
        connection["baud"],
    )
    return {"status": "connected", **connection}


@app.post("/serial/disconnect")
def disconnect_serial() -> dict[str, str | bool]:
    """Disconnect Mission Control from the active ESP32 serial port."""

    was_connected = serial_service.is_connected()
    serial_service.disconnect()
    logger.info("Serial disconnected")
    return {"status": "disconnected", "was_connected": was_connected}


@app.post("/serial/send")
def send_serial_command(request: SerialCommandRequest) -> dict[str, Any]:
    """Send a direct newline-delimited command to the ESP32 for development."""

    try:
        result = serial_service.send_command(request.command)
    except ConnectionError as error:
        raise HTTPException(status_code=409, detail=str(error)) from error
    except (SerialException, OSError, ValueError) as error:
        logger.warning("Serial send failed: %s", error)
        raise HTTPException(status_code=400, detail=str(error)) from error

    logger.info("Serial command sent: %s", result["command"])
    return result


@app.post("/mission", response_model=MissionResponse)
def create_mission(mission: MissionRequest) -> MissionResponse:
    """Dispatch a mission over MQTT, falling back to serial or simulation."""

    return dispatch_mission(mission)


@app.post("/voice-command")
def create_voice_command(request: VoiceCommandRequest) -> dict[str, Any]:
    """Translate recognized visitor speech into a rule-based rover mission."""

    matched_intent = match_voice_intent(request.text)
    if matched_intent is None:
        logger.info("Unknown voice command: %s", request.text)
        return {
            "recognized_text": request.text,
            "error": "UNKNOWN_VOICE_COMMAND",
        }

    intent, mission_id, mission_name = matched_intent
    mission_response = dispatch_mission(
        MissionRequest(mission_id=mission_id, mission_name=mission_name),
    )

    return {
        "recognized_text": request.text,
        "intent": intent,
        "mission_id": mission_id,
        "command": mission_response.command,
        "dispatch_mode": mission_response.mode,
    }


@app.post("/ai-mission-planner")
def create_ai_mission_plan(request: AiMissionPlannerRequest) -> dict[str, Any]:
    """Use optional Amazon Bedrock planning, with rule-based fallback."""

    planner_source = "bedrock"
    try:
        plan = plan_with_bedrock(request.text)
    except Exception as error:  # noqa: BLE001 - fallback must catch AWS/runtime failures.
        logger.warning("Bedrock planner failed; using rule fallback: %s", error)
        planner_source = "rule_fallback"
        plan = fallback_rule_plan(request.text)

    if plan is None:
        return {
            "input_text": request.text,
            "error": "UNKNOWN_AI_MISSION_REQUEST",
        }

    mission_response = dispatch_mission(
        MissionRequest(
            mission_id=plan["mission_id"],
            mission_name=MISSION_NAMES.get(plan["mission_id"], plan["command"]),
        ),
    )

    return {
        "input_text": request.text,
        "intent": plan["intent"],
        "mission_id": plan["mission_id"],
        "command": mission_response.command,
        "reason": plan["reason"],
        "dispatch_mode": mission_response.mode,
        "planner_source": planner_source,
    }
