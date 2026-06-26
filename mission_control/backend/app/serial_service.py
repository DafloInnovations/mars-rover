"""Thread-safe serial connection service for the Su-Par1 ESP32."""

from threading import RLock
from typing import Any

import serial
from serial.tools import list_ports as serial_list_ports

DEFAULT_BAUD_RATE = 115200
WRITE_TIMEOUT_SECONDS = 2

_connection: serial.Serial | None = None
_lock = RLock()


def list_ports() -> list[dict[str, Any]]:
    """Return serial ports currently detected by the operating system."""

    return [
        {
            "device": port.device,
            "description": port.description,
            "manufacturer": port.manufacturer,
            "serial_number": port.serial_number,
            "vid": port.vid,
            "pid": port.pid,
        }
        for port in serial_list_ports.comports()
    ]


def connect(port: str, baud: int = DEFAULT_BAUD_RATE) -> dict[str, Any]:
    """Open a serial connection to an ESP32.

    An existing connection is closed before a different port is opened.
    PySerial exceptions are allowed to propagate so the API can return a clear
    connection error to the operator.
    """

    global _connection

    normalized_port = port.strip()
    if not normalized_port:
        raise ValueError("A serial port is required.")
    if baud <= 0:
        raise ValueError("Baud rate must be greater than zero.")

    with _lock:
        if _connection is not None and _connection.is_open:
            if _connection.port == normalized_port and _connection.baudrate == baud:
                return {
                    "connected": True,
                    "port": _connection.port,
                    "baud": _connection.baudrate,
                }
            _connection.close()

        _connection = serial.Serial(
            port=normalized_port,
            baudrate=baud,
            timeout=1,
            write_timeout=WRITE_TIMEOUT_SECONDS,
        )

        return {
            "connected": True,
            "port": _connection.port,
            "baud": _connection.baudrate,
        }


def disconnect() -> None:
    """Close the active serial connection, if one exists."""

    global _connection

    with _lock:
        if _connection is not None:
            if _connection.is_open:
                _connection.close()
            _connection = None


def send_command(command: str) -> dict[str, Any]:
    """Send one newline-delimited command over the active serial connection."""

    normalized_command = command.strip().upper()
    if not normalized_command:
        raise ValueError("A command is required.")

    with _lock:
        if _connection is None or not _connection.is_open:
            raise ConnectionError("No ESP32 serial connection is active.")

        payload = f"{normalized_command}\n".encode("utf-8")
        bytes_written = _connection.write(payload)
        _connection.flush()

        return {
            "status": "sent",
            "command": normalized_command,
            "bytes_written": bytes_written,
            "port": _connection.port,
            "baud": _connection.baudrate,
        }


def is_connected() -> bool:
    """Return whether an open serial connection is currently available."""

    with _lock:
        return _connection is not None and _connection.is_open
