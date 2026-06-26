"""API request and response models."""

from typing import Any

from pydantic import BaseModel, Field


class MissionRequest(BaseModel):
    """Mission selected by an operator in the Mission Control dashboard."""

    mission_id: int = Field(ge=1, le=5)
    mission_name: str = Field(min_length=1, max_length=100)


class MissionResponse(BaseModel):
    """Acknowledgement returned after dispatching a mission."""

    status: str
    command: str
    mode: str
    mission_id: int
    mission_name: str
    message: str


class SerialConnectRequest(BaseModel):
    """Serial port settings selected by the operator."""

    port: str = Field(min_length=1, max_length=255)
    baud: int = Field(default=115200, gt=0, le=4_000_000)


class SerialCommandRequest(BaseModel):
    """One command to send directly to the connected ESP32."""

    command: str = Field(min_length=1, max_length=100)


class MqttPublishRequest(BaseModel):
    """One MQTT message to publish from Mission Control."""

    topic: str = Field(min_length=1, max_length=255)
    payload: Any


class VoiceCommandRequest(BaseModel):
    """Recognized visitor speech sent by the Mission Control frontend."""

    text: str = Field(min_length=1, max_length=500)


class AiMissionPlannerRequest(BaseModel):
    """Natural-language mission situation for optional Bedrock planning."""

    text: str = Field(min_length=1, max_length=1000)
