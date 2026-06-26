const API_BASE_URL =
  import.meta.env.VITE_API_BASE_URL ?? "http://localhost:8000";

async function request(path, options = {}) {
  const response = await fetch(`${API_BASE_URL}${path}`, options);
  const payload = await response.json().catch(() => ({}));

  if (!response.ok) {
    throw new Error(
      payload.detail ?? `Backend request failed with status ${response.status}.`,
    );
  }

  return payload;
}

export async function dispatchMission(mission) {
  return request("/mission", {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
    },
    body: JSON.stringify({
      mission_id: mission.id,
      mission_name: mission.name,
    }),
  });
}

export async function dispatchVoiceCommand(text) {
  return request("/voice-command", {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
    },
    body: JSON.stringify({ text }),
  });
}

export async function planAiMission(text) {
  return request("/ai-mission-planner", {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
    },
    body: JSON.stringify({ text }),
  });
}

export async function getSerialPorts() {
  return request("/serial/ports");
}

export async function getMqttStatus() {
  return request("/mqtt/status");
}

export async function connectMqtt() {
  return request("/mqtt/connect", {
    method: "POST",
  });
}

export async function disconnectMqtt() {
  return request("/mqtt/disconnect", {
    method: "POST",
  });
}

export async function getRoverStatus() {
  return request("/rover/status");
}

export async function connectSerial(port) {
  return request("/serial/connect", {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
    },
    body: JSON.stringify({ port, baud: 115200 }),
  });
}

export async function disconnectSerial() {
  return request("/serial/disconnect", {
    method: "POST",
  });
}

export async function sendSerialCommand(command) {
  return request("/serial/send", {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
    },
    body: JSON.stringify({ command }),
  });
}
