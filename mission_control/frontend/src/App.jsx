import { useCallback, useEffect, useRef, useState } from "react";

import {
  connectMqtt,
  connectSerial,
  disconnectMqtt,
  disconnectSerial,
  dispatchMission,
  dispatchVoiceCommand,
  getMqttStatus,
  getRoverStatus,
  getSerialPorts,
  planAiMission,
  sendSerialCommand,
} from "./api";
import { missions } from "./data";

const diagnosticCommands = [
  "LINE_TEST",
  "RFID_TEST",
  "SERVO_OPEN",
  "SERVO_CLOSE",
  "SERVO_TEST",
  "SELF_TEST",
];

const missionDetails = {
  1: { label: "Deliver Food", cargo: "Food", pickup: "wh-a", waypoint: "WH-A" },
  2: { label: "Deliver Medicine", cargo: "Medicine", pickup: "wh-b", waypoint: "WH-B" },
  3: { label: "Deliver Oxygen", cargo: "Oxygen", pickup: "wh-c", waypoint: "WH-C" },
  4: { label: "Collect Research Sample", cargo: "Research Sample", pickup: "wh-a", waypoint: "HABITAT" },
  5: { label: "Return to Base", cargo: "None", pickup: null, waypoint: "BASE" },
};

const mapWaypoints = {
  base: { x: 120, y: 355, label: "Base" },
  j1: { x: 500, y: 430, label: "J1" },
  j2: { x: 500, y: 195, label: "J2" },
  "wh-a": { x: 300, y: 195, label: "WH-A" },
  "wh-b": { x: 720, y: 130, label: "WH-B" },
  "wh-c": { x: 500, y: 525, label: "WH-C" },
  habitat: { x: 900, y: 305, label: "Habitat" },
};

const missionPaths = {
  1: ["base", "j1", "j2", "wh-a", "j2", "habitat"],
  2: ["base", "j1", "j2", "wh-b", "j2", "habitat"],
  3: ["base", "j1", "wh-c", "j1", "j2", "habitat"],
  4: ["base", "j1", "j2", "wh-a", "j2", "wh-b", "habitat"],
  5: ["habitat", "j2", "j1", "base"],
};

const waypointAliases = {
  base: "base",
  j1: "j1",
  j2: "j2",
  "wh-a": "wh-a",
  wha: "wh-a",
  "warehouse-a": "wh-a",
  "wh-b": "wh-b",
  whb: "wh-b",
  "warehouse-b": "wh-b",
  "wh-c": "wh-c",
  whc: "wh-c",
  "warehouse-c": "wh-c",
  habitat: "habitat",
};

const waypointLogMessages = {
  base: "Rover departed Base",
  j1: "Reached J1",
  j2: "Reached J2",
  "wh-a": "Reached WH-A",
  "wh-b": "Reached WH-B",
  "wh-c": "Reached WH-C",
  habitat: "Reached Habitat",
};

const missionSegmentDurationMs = 1800;
const aiPromptExamples = [
  "Habitat is low on oxygen",
  "Astronaut needs medicine",
  "Food supplies are running low",
  "Collect rock sample",
  "Return rover to base",
];

function toWaypointKey(location) {
  if (!location) return null;
  return waypointAliases[String(location).trim().toLowerCase()] ?? null;
}

function getPathD(points) {
  if (points.length === 0) return "";
  return points
    .map((point, index) => {
      const waypoint = mapWaypoints[point];
      return `${index === 0 ? "M" : "L"}${waypoint.x} ${waypoint.y}`;
    })
    .join(" ");
}

function getPathDWithPosition(points, completedSegments, position) {
  if (points.length === 0) return "";
  const completedPoints = points.slice(0, Math.min(completedSegments + 1, points.length));
  const d = getPathD(completedPoints);
  if (!position || completedSegments >= points.length - 1) return d;
  return `${d} L${position.x} ${position.y}`;
}

function formatStatusValue(value, fallback = "Unknown") {
  if (value === null || value === undefined || value === "") {
    return fallback;
  }
  if (typeof value === "object") {
    return value.status ?? value.state ?? fallback;
  }
  return String(value);
}

function formatTimestamp(value) {
  if (!value) {
    return "No message yet";
  }

  const timestamp = new Date(value);
  if (Number.isNaN(timestamp.getTime())) {
    return String(value);
  }

  return timestamp.toLocaleTimeString([], { hour12: false });
}

function formatUptime(value) {
  const seconds = Number(value);
  if (!Number.isFinite(seconds)) {
    return "Unknown";
  }

  const minutes = Math.floor(seconds / 60);
  const remainingSeconds = Math.floor(seconds % 60);
  return `${minutes}m ${remainingSeconds}s`;
}

function formatCapabilities(value) {
  if (!Array.isArray(value) || value.length === 0) {
    return "Unknown";
  }

  return value.join(", ");
}

function Panel({ title, icon, className = "", action, children }) {
  return (
    <section className={`mc-card ${className}`}>
      <header className="card-header">
        <div>
          <span className="card-icon" aria-hidden="true">{icon}</span>
          <h2>{title}</h2>
        </div>
        {action}
      </header>
      <div className="card-body">{children}</div>
    </section>
  );
}

function MarsStatus() {
  return (
    <Panel className="mars-status-card" icon="◉" title="Mars Status">
      <div className="mars-stage">
        <div className="mars-glow" />
        <div className="mars-planet" aria-label="Animated rotating Mars">
          <div className="mars-surface" />
          <div className="mars-shadow" />
        </div>
        <span className="star star-a" />
        <span className="star star-b" />
        <span className="star star-c" />
      </div>
      <div className="mars-readout">
        <span>Mars Rotation</span>
        <div className="rotation-track"><span /></div>
        <strong><i /> Real Time</strong>
      </div>
    </Panel>
  );
}

function ColonyMap({
  activeMission,
  animationState,
  connected,
}) {
  const {
    cargoLoaded,
    completedSegments,
    currentWaypoint,
    etaSeconds,
    habitatDelivered,
    isMoving,
    nextWaypoint,
    path,
    position,
  } = animationState;
  const plannedPath = getPathD(path);
  const completedPath = getPathDWithPosition(path, completedSegments, position);
  const progressPercent = path.length > 1
    ? Math.min(100, Math.round((completedSegments / (path.length - 1)) * 100))
    : 0;
  const cargoLabel = activeMission ? missionDetails[activeMission.id].cargo : "None";
  const currentLabel = mapWaypoints[currentWaypoint]?.label ?? "Unknown";
  const nextLabel = nextWaypoint ? mapWaypoints[nextWaypoint]?.label ?? "Unknown" : "--";

  return (
    <Panel className="map-card" icon="⌖" title="Mars Colony Map">
      <div className="map-canvas">
        <svg
          aria-label="Mars colony route network"
          className="route-network"
          preserveAspectRatio="none"
          viewBox="0 0 1000 620"
        >
          <path className="route planned" d="M120 355 L250 355 L330 430 L500 430" />
          <path className="route planned" d="M500 430 L500 195 L720 195 L750 305" />
          <path className="route planned" d="M500 430 L500 525" />
          <path className="route planned" d="M720 195 L720 130" />
          <path className="route planned" d="M750 305 L900 305" />
          <path className="route completed" d="M120 355 L250 355 L330 430 L500 430 L500 195 L300 195" />
          {plannedPath && <path className="mission-route planned" d={plannedPath} />}
          {completedPath && <path className="mission-route completed" d={completedPath} />}
          <circle className="junction" cx="500" cy="430" r="7" />
          <circle className="junction" cx="500" cy="195" r="7" />
          <circle className="junction" cx="720" cy="195" r="7" />
          <circle className="junction" cx="750" cy="305" r="7" />
        </svg>

        <div className={`map-node base-node ${currentWaypoint === "base" ? "current" : ""}`}>
          <span>⌂</span><strong>BASE</strong><small>Rover Base</small>
        </div>
        <div className={`map-node wha-node ${currentWaypoint === "wh-a" ? "current" : ""}`}>
          <span>▣</span><strong>WH-A</strong><small>Food Supplies</small>
        </div>
        <div className={`map-node whb-node ${currentWaypoint === "wh-b" ? "current" : ""}`}>
          <span>✚</span><strong>WH-B</strong><small>Medicine</small>
        </div>
        <div className={`map-node whc-node ${currentWaypoint === "wh-c" ? "current" : ""}`}>
          <span>O₂</span><strong>WH-C</strong><small>Oxygen</small>
        </div>
        <div className={`map-node habitat-node ${currentWaypoint === "habitat" ? "current" : ""} ${habitatDelivered ? "success" : ""}`}>
          <span>▥</span><strong>HABITAT</strong><small>Mars Habitat</small>
        </div>

        <div
          className={`rover-marker ${connected || isMoving ? "live" : ""}`}
          style={{
            left: `${(position.x / 1000) * 100}%`,
            top: `${(position.y / 620) * 100}%`,
          }}
        >
          <span>▰</span>
          {cargoLoaded && cargoLabel !== "None" && (
            <em className="cargo-badge">{cargoLabel}</em>
          )}
        </div>

        <div className="map-legend">
          <span><i className="legend-complete" /> Path Completed</span>
          <span><i className="legend-planned" /> Path Planned</span>
          <span><i className="legend-position" /> Current Position</span>
        </div>
        <div className="map-grid-label">SECTOR A-17 · LOCAL NAV GRID</div>
      </div>

      <div className="map-info-strip">
        <div>
          <span>Current Location</span>
          <strong>{isMoving ? `En Route: ${currentLabel} → ${nextLabel}` : currentLabel}</strong>
        </div>
        <div>
          <span>Cargo Status</span>
          <strong>{cargoLoaded ? `${cargoLabel} Loaded` : cargoLabel === "None" ? "None" : "Awaiting Pickup"}</strong>
        </div>
        <div>
          <span>Next Waypoint</span>
          <strong>{nextLabel}</strong>
        </div>
        <div>
          <span>ETA</span>
          <strong>{etaSeconds > 0 ? `00:${String(etaSeconds).padStart(2, "0")}` : "--:--"}</strong>
        </div>
        <div>
          <span>Progress</span>
          <strong>{progressPercent}%</strong>
        </div>
      </div>
    </Panel>
  );
}

function App() {
  const [ports, setPorts] = useState([]);
  const [selectedPort, setSelectedPort] = useState("");
  const [connectedPort, setConnectedPort] = useState("");
  const [connectionState, setConnectionState] = useState("disconnected");
  const [connectionBusy, setConnectionBusy] = useState(false);
  const [mqttConnected, setMqttConnected] = useState(false);
  const [mqttBusy, setMqttBusy] = useState(false);
  const [roverStatus, setRoverStatus] = useState(null);
  const [serialBusy, setSerialBusy] = useState(false);
  const [activeMission, setActiveMission] = useState(null);
  const [missionState, setMissionState] = useState("idle");
  const [lastCommand, setLastCommand] = useState("STANDBY");
  const [servoState, setServoState] = useState("CLOSED");
  const [motorState, setMotorState] = useState("IDLE");
  const [events, setEvents] = useState([]);
  const [clock, setClock] = useState(new Date());
  const [voiceListening, setVoiceListening] = useState(false);
  const [voiceStatus, setVoiceStatus] = useState("Voice control ready");
  const [recognizedText, setRecognizedText] = useState("");
  const [aiPrompt, setAiPrompt] = useState("");
  const [aiPlanning, setAiPlanning] = useState(false);
  const [aiPlan, setAiPlan] = useState(null);
  const [animationState, setAnimationState] = useState({
    cargoLoaded: false,
    completedSegments: 0,
    currentWaypoint: "base",
    etaSeconds: 0,
    habitatDelivered: false,
    isMoving: false,
    nextWaypoint: null,
    path: missionPaths[1],
    position: mapWaypoints.base,
  });
  const animationTimers = useRef([]);

  const addEvent = useCallback((source, message, level = "info") => {
    setEvents((current) =>
      [
        {
          id: `${Date.now()}-${Math.random()}`,
          time: new Date().toLocaleTimeString([], { hour12: false }),
          source,
          message,
          level,
        },
        ...current,
      ].slice(0, 30),
    );
  }, []);

  const clearAnimationTimers = useCallback(() => {
    animationTimers.current.forEach((timer) => window.clearTimeout(timer));
    animationTimers.current = [];
  }, []);

  const syncAnimationFromRoverStatus = useCallback((status) => {
    const waypointKey = toWaypointKey(status?.location);
    if (!waypointKey) return;

    setAnimationState((current) => {
      const waypoint = mapWaypoints[waypointKey];
      const currentIndex = current.path.indexOf(waypointKey);
      const idle = String(status?.state ?? "").toUpperCase() === "IDLE";
      const noMission = ["", "none", "NONE"].includes(String(status?.mission ?? "").trim());

      return {
        ...current,
        completedSegments: currentIndex >= 0 ? Math.max(current.completedSegments, currentIndex) : current.completedSegments,
        currentWaypoint: waypointKey,
        isMoving: idle && noMission ? false : current.isMoving,
        nextWaypoint: idle && noMission ? null : current.nextWaypoint,
        position: waypoint,
      };
    });
  }, []);

  const startMissionAnimation = useCallback((mission) => {
    clearAnimationTimers();

    const path = missionPaths[mission.id];
    const details = missionDetails[mission.id];
    const totalSegments = path.length - 1;
    const startWaypoint = path[0];

    setAnimationState({
      cargoLoaded: false,
      completedSegments: 0,
      currentWaypoint: startWaypoint,
      etaSeconds: Math.ceil((totalSegments * missionSegmentDurationMs) / 1000),
      habitatDelivered: false,
      isMoving: true,
      nextWaypoint: path[1] ?? null,
      path,
      position: mapWaypoints[startWaypoint],
    });
    addEvent("ROVER", mission.id === 5 ? "Rover departed Habitat" : "Rover departed Base", "info");

    path.slice(1).forEach((waypointKey, index) => {
      const timer = window.setTimeout(() => {
        const completedSegments = index + 1;
        const nextWaypoint = path[completedSegments + 1] ?? null;
        const reachedPickup = details.pickup === waypointKey;
        const reachedHabitat = waypointKey === "habitat";
        const reachedEnd = completedSegments === totalSegments;

        setAnimationState((current) => ({
          ...current,
          cargoLoaded: reachedHabitat
            ? false
            : current.cargoLoaded || Boolean(reachedPickup && details.cargo !== "None"),
          completedSegments,
          currentWaypoint: waypointKey,
          etaSeconds: Math.max(0, Math.ceil(((totalSegments - completedSegments) * missionSegmentDurationMs) / 1000)),
          habitatDelivered: current.habitatDelivered || Boolean(reachedHabitat && details.cargo !== "None"),
          isMoving: !reachedEnd,
          nextWaypoint,
          position: mapWaypoints[waypointKey],
        }));

        if (waypointLogMessages[waypointKey]) {
          addEvent("ROVER", waypointLogMessages[waypointKey], waypointKey === "habitat" ? "success" : "info");
        }
        if (reachedPickup && details.cargo !== "None") {
          addEvent("CARGO", "Cargo Loaded", "success");
        }
        if (reachedHabitat && details.cargo !== "None") {
          addEvent("CARGO", "Cargo Delivered", "success");
        }
        if (reachedEnd) {
          addEvent("MISSION", "Mission Complete", "success");
          setMissionState("complete");
        }
      }, missionSegmentDurationMs * completedSegments);

      animationTimers.current.push(timer);
    });
  }, [addEvent, clearAnimationTimers]);

  const refreshPorts = useCallback(async () => {
    setConnectionBusy(true);
    try {
      const response = await getSerialPorts();
      setPorts(response.ports);
      setSelectedPort((current) =>
        response.ports.some((port) => port.device === current)
          ? current
          : response.ports[0]?.device ?? "",
      );
      addEvent("LINK", `${response.ports.length} serial ports detected`);
    } catch (error) {
      addEvent("ERROR", error.message, "error");
    } finally {
      setConnectionBusy(false);
    }
  }, [addEvent]);

  const refreshMqttStatus = useCallback(async (logResult = false) => {
    try {
      const response = await getMqttStatus();
      setMqttConnected(Boolean(response.connected));
      if (response.last_status) {
        setRoverStatus(response.last_status);
        syncAnimationFromRoverStatus(response.last_status);
      }
      if (logResult) {
        addEvent(
          "MQTT",
          `MQTT is ${response.connected ? "connected" : "disconnected"}`,
          response.connected ? "success" : "warning",
        );
      }
    } catch (error) {
      setMqttConnected(false);
      if (logResult) {
        addEvent("ERROR", error.message, "error");
      }
    }
  }, [addEvent, syncAnimationFromRoverStatus]);

  const refreshRoverStatus = useCallback(async (logResult = true) => {
    try {
      const response = await getRoverStatus();
      setRoverStatus(response);
      syncAnimationFromRoverStatus(response);
      if (logResult) {
        addEvent("ROVER", "Rover status refreshed", "success");
      }
    } catch (error) {
      if (logResult) {
        addEvent("ERROR", error.message, "error");
      }
    }
  }, [addEvent, syncAnimationFromRoverStatus]);

  useEffect(() => {
    refreshPorts();
    refreshMqttStatus(false);
    refreshRoverStatus(false);
    const timer = window.setInterval(() => setClock(new Date()), 1000);
    const roverTimer = window.setInterval(() => {
      refreshMqttStatus(false);
      refreshRoverStatus(false);
    }, 5000);
    return () => {
      window.clearInterval(timer);
      window.clearInterval(roverTimer);
      clearAnimationTimers();
    };
  }, [clearAnimationTimers, refreshMqttStatus, refreshPorts, refreshRoverStatus]);

  const handleMqttConnect = async () => {
    setMqttBusy(true);
    try {
      const response = await connectMqtt();
      setMqttConnected(Boolean(response.connected));
      addEvent(
        "MQTT",
        `${response.status.toUpperCase()} · ${response.broker_host}:${response.broker_port}`,
        response.connected ? "success" : "info",
      );
      await refreshRoverStatus(false);
    } catch (error) {
      setMqttConnected(false);
      addEvent("ERROR", error.message, "error");
    } finally {
      setMqttBusy(false);
    }
  };

  const handleMqttDisconnect = async () => {
    setMqttBusy(true);
    try {
      await disconnectMqtt();
      setMqttConnected(false);
      addEvent("MQTT", "MQTT broker disconnected", "warning");
    } catch (error) {
      addEvent("ERROR", error.message, "error");
    } finally {
      setMqttBusy(false);
    }
  };

  const handleConnect = async () => {
    if (!selectedPort) return;
    setConnectionBusy(true);
    setConnectionState("connecting");
    try {
      const response = await connectSerial(selectedPort);
      setConnectedPort(response.port);
      setConnectionState("connected");
      addEvent("LINK", `Connected to ${response.port}`, "success");
    } catch (error) {
      setConnectionState("disconnected");
      addEvent("ERROR", error.message, "error");
    } finally {
      setConnectionBusy(false);
    }
  };

  const handleDisconnect = async () => {
    setConnectionBusy(true);
    try {
      await disconnectSerial();
      setConnectionState("disconnected");
      setConnectedPort("");
      setMotorState("IDLE");
      addEvent("LINK", "Rover serial link disconnected", "warning");
    } catch (error) {
      addEvent("ERROR", error.message, "error");
    } finally {
      setConnectionBusy(false);
    }
  };

  const sendCommand = async (command, source) => {
    setSerialBusy(true);
    setLastCommand(command);
    if (["FORWARD", "BACKWARD", "LEFT", "RIGHT"].includes(command)) {
      setMotorState(command);
    } else if (command === "STOP") {
      setMotorState("IDLE");
    } else if (command === "SERVO_OPEN") {
      setServoState("OPEN");
    } else if (["SERVO_CLOSE", "SERVO_TEST", "SELF_TEST"].includes(command)) {
      setServoState("CLOSED");
    }
    addEvent(source, `${command} command sent`);

    try {
      const response = await sendSerialCommand(command);
      addEvent(
        "ACK",
        `${response.status.toUpperCase()}: ${response.command} → ${response.port}`,
        "success",
      );
    } catch (error) {
      addEvent("ERROR", error.message, "error");
    } finally {
      setSerialBusy(false);
    }
  };

  const selectMission = async (mission) => {
    setActiveMission(mission);
    setMissionState("dispatching");
    setLastCommand(`MISSION_${mission.id}`);
    addEvent("MISSION", `Mission ${mission.id} selected: ${missionDetails[mission.id].label}`);
    startMissionAnimation(mission);

    try {
      const response = await dispatchMission(mission);
      setMissionState(response.status === "sent" ? "active" : "simulated");
      const dispatchMode = response.mode ?? response.status ?? "unknown";
      addEvent(
        "BACKEND",
        `${response.message} Dispatch mode: ${dispatchMode.toUpperCase()}`,
        response.status === "sent" ? "success" : "info",
      );
    } catch (error) {
      setMissionState("error");
      addEvent("ERROR", error.message, "error");
    }
  };

  const startVoiceCommand = () => {
    const SpeechRecognition =
      window.SpeechRecognition ?? window.webkitSpeechRecognition;

    if (!SpeechRecognition) {
      const message = "Speech recognition is not supported in this browser. Try Chrome or Edge.";
      setVoiceStatus(message);
      addEvent("VOICE", message, "warning");
      return;
    }

    const recognition = new SpeechRecognition();
    recognition.lang = "en-US";
    recognition.continuous = false;
    recognition.interimResults = false;

    recognition.onstart = () => {
      setVoiceListening(true);
      setVoiceStatus("Listening...");
      addEvent("VOICE", "Listening...");
    };

    recognition.onerror = (event) => {
      setVoiceListening(false);
      const message = `Voice recognition error: ${event.error}`;
      setVoiceStatus(message);
      addEvent("VOICE", message, "error");
    };

    recognition.onend = () => {
      setVoiceListening(false);
      setVoiceStatus((current) => current === "Listening..." ? "Voice control ready" : current);
    };

    recognition.onresult = async (event) => {
      const transcript = event.results[0]?.[0]?.transcript?.trim() ?? "";
      if (!transcript) {
        setVoiceStatus("No speech recognized");
        addEvent("VOICE", "No speech recognized", "warning");
        return;
      }

      setRecognizedText(transcript);
      setVoiceStatus("Processing voice command...");
      addEvent("VOICE", `Recognized: "${transcript}"`);

      try {
        const response = await dispatchVoiceCommand(transcript);
        if (response.error) {
          setVoiceStatus(response.error);
          addEvent("VOICE", `${response.error}: "${response.recognized_text}"`, "warning");
          return;
        }

        const mission = missions.find((candidate) => candidate.id === response.mission_id);
        if (mission) {
          setActiveMission(mission);
          setMissionState(response.dispatch_mode === "simulated" ? "simulated" : "active");
          setLastCommand(response.command);
          startMissionAnimation(mission);
        }

        setVoiceStatus(`Intent: ${response.intent}`);
        addEvent(
          "VOICE",
          `${response.command} dispatched by voice via ${response.dispatch_mode.toUpperCase()}`,
          response.dispatch_mode === "simulated" ? "info" : "success",
        );
      } catch (error) {
        setVoiceStatus("Voice command failed");
        addEvent("ERROR", error.message, "error");
      }
    };

    recognition.start();
  };

  const submitAiMissionPlan = async () => {
    const prompt = aiPrompt.trim();
    if (!prompt) {
      addEvent("AI", "Describe the Mars situation before planning.", "warning");
      return;
    }

    setAiPlanning(true);
    addEvent("AI", `Planning mission for: "${prompt}"`);

    try {
      const response = await planAiMission(prompt);
      setAiPlan(response);

      if (response.error) {
        addEvent("AI", response.error, "warning");
        return;
      }

      const mission = missions.find((candidate) => candidate.id === response.mission_id);
      if (mission) {
        setActiveMission(mission);
        setMissionState(response.dispatch_mode === "simulated" ? "simulated" : "active");
        setLastCommand(response.command);
        startMissionAnimation(mission);
      }

      addEvent(
        "AI",
        `${response.intent} → ${response.command} via ${response.dispatch_mode.toUpperCase()}`,
        response.dispatch_mode === "simulated" ? "info" : "success",
      );
    } catch (error) {
      addEvent("ERROR", error.message, "error");
    } finally {
      setAiPlanning(false);
    }
  };

  const stopMission = async () => {
    clearAnimationTimers();
    setAnimationState((current) => ({
      ...current,
      etaSeconds: 0,
      isMoving: false,
      nextWaypoint: null,
    }));
    if (isConnected) {
      await sendCommand("STOP", "MISSION");
    } else {
      addEvent("MISSION", "Simulated mission stopped", "warning");
    }
    setActiveMission(null);
    setMissionState("idle");
  };

  const isConnected = connectionState === "connected";
  const isRoverOnline = formatStatusValue(roverStatus?.status, "offline").toLowerCase() === "online";
  const roverStatusLabel = formatStatusValue(roverStatus?.status, "Unknown");
  const roverFirmware = formatStatusValue(roverStatus?.firmware, "Unknown");
  const roverMission = formatStatusValue(roverStatus?.mission ?? roverStatus?.current_mission, "none");
  const roverLocation = formatStatusValue(roverStatus?.location, "unknown");
  const roverRuntimeState = formatStatusValue(roverStatus?.state, "Unknown");
  const roverBattery = Number.isFinite(Number(roverStatus?.battery)) ? `${roverStatus.battery}%` : "87%";
  const roverWifiRssi = Number.isFinite(Number(roverStatus?.wifi_rssi)) ? `${roverStatus.wifi_rssi} dBm` : "Unknown";
  const roverUptime = formatUptime(roverStatus?.uptime);
  const roverCapabilities = formatCapabilities(roverStatus?.capabilities);
  const lastMqttMessage = formatTimestamp(roverStatus?.last_seen);
  const displayPort = connectedPort || selectedPort || "No port selected";

  return (
    <main className="mission-control">
      <header className="top-header">
        <div className="brand-block">
          <div className="rover-logo" aria-hidden="true">
            <span className="antenna" />
            <span className="rover-body">▰</span>
            <i /><i />
          </div>
          <div>
            <h1>Mission to Mars <em>2050</em></h1>
            <p>Su-Par1 Rover Mission Control</p>
            <p className="mission-inspiration">
              <span aria-hidden="true">🚀</span>
              Inspired by ISRO&apos;s Mangalyaan Mission
            </p>
          </div>
        </div>

        <div className="header-connection">
          <div className="connection-row mqtt-row">
            <div className={`connection-status ${mqttConnected ? "connected" : ""}`}>
              <span className="connection-bars">▥</span>
              <strong>MQTT {mqttConnected ? "Connected" : "Disconnected"}</strong>
            </div>
            <div className="port-display">
              <span>Communication Mode</span>
              <strong>MQTT</strong>
            </div>
            <button
              className="connect-button"
              disabled={mqttBusy || mqttConnected}
              onClick={handleMqttConnect}
              type="button"
            >
              Connect MQTT
            </button>
            <button
              className="disconnect-button"
              disabled={mqttBusy || !mqttConnected}
              onClick={handleMqttDisconnect}
              type="button"
            >
              Disconnect MQTT
            </button>
            <button disabled={mqttBusy} onClick={() => refreshRoverStatus(true)} type="button">
              Refresh Rover Status
            </button>
          </div>

          <div className="connection-row serial-row">
            <div className={`connection-status ${isConnected ? "connected" : ""}`}>
              <span className="connection-bars">▦</span>
              <strong>{isConnected ? "USB Connected" : "USB Disconnected"}</strong>
            </div>
            <div className="port-display">
              <span>Development USB Serial</span>
              <strong>{displayPort}</strong>
            </div>
            <select
              aria-label="Serial port"
              disabled={isConnected || connectionBusy}
              onChange={(event) => setSelectedPort(event.target.value)}
              value={selectedPort}
            >
              {ports.length === 0 && <option value="">No ports detected</option>}
              {ports.map((port) => (
                <option key={port.device} value={port.device}>
                  {port.device}
                </option>
              ))}
            </select>
            <button disabled={connectionBusy || isConnected} onClick={refreshPorts} type="button">↻</button>
            <button
              className="connect-button"
              disabled={connectionBusy || isConnected || !selectedPort}
              onClick={handleConnect}
              type="button"
            >
              Connect
            </button>
            <button
              className="disconnect-button"
              disabled={connectionBusy || !isConnected}
              onClick={handleDisconnect}
              type="button"
            >
              Disconnect
            </button>
          </div>
        </div>

        <div className="header-meta">
          <div className="rover-name"><span>Rover:</span> Su-Par1</div>
          <div className="digital-clock">
            <strong>{clock.toLocaleTimeString([], { hour12: false })}</strong>
            <span>{clock.toLocaleDateString([], { month: "short", day: "2-digit", year: "numeric" })}</span>
          </div>
          <button aria-label="Settings" className="settings-button" type="button">⚙</button>
        </div>
      </header>

      <div className="dashboard-grid">
        <aside className="left-column">
          <MarsStatus />

          <Panel icon="♙" title="Rover Status">
            <dl className="rover-status-list">
              <div><dt>Communication Mode</dt><dd className="good">MQTT</dd></div>
              <div><dt>MQTT Status</dt><dd className={mqttConnected ? "good" : ""}>{mqttConnected ? "CONNECTED" : "DISCONNECTED"}</dd></div>
              <div><dt>Rover Status</dt><dd className={isRoverOnline ? "good" : ""}>{roverStatusLabel.toUpperCase()}</dd></div>
              <div><dt>Last MQTT Message</dt><dd>{lastMqttMessage}</dd></div>
              <div><dt>Firmware</dt><dd>{roverFirmware}</dd></div>
              <div><dt>Current Mission</dt><dd>{roverMission.toUpperCase()}</dd></div>
              <div><dt>Location</dt><dd>{roverLocation}</dd></div>
              <div><dt>Runtime State</dt><dd>{roverRuntimeState.toUpperCase()}</dd></div>
              <div><dt>Battery</dt><dd className="good">{roverBattery} <i className="battery-icon" /></dd></div>
              <div><dt>Wi-Fi RSSI</dt><dd>{roverWifiRssi}</dd></div>
              <div><dt>Uptime</dt><dd>{roverUptime}</dd></div>
              <div><dt>Capabilities</dt><dd>{roverCapabilities}</dd></div>
              <div><dt>Motors</dt><dd>{motorState}</dd></div>
              <div><dt>Line Sensors</dt><dd className="good">ACTIVE</dd></div>
              <div><dt>RFID Reader</dt><dd className="good">READY</dd></div>
              <div><dt>Servo</dt><dd>{formatStatusValue(roverStatus?.servo, servoState)}</dd></div>
            </dl>
          </Panel>

          <Panel icon="⊕" title="Mission Status" className="mission-status-card">
            <div className={`mission-status-value ${activeMission ? "active" : ""}`}>
              <strong>{activeMission ? missionDetails[activeMission.id].label : "IDLE"}</strong>
              <span>
                {activeMission
                  ? `${missionState.toUpperCase()} · MISSION_${activeMission.id}`
                  : "No active mission"}
              </span>
            </div>
          </Panel>
        </aside>

        <section className="center-column">
          <ColonyMap
            activeMission={activeMission}
            animationState={animationState}
            connected={mqttConnected || isConnected}
          />

          <Panel
            className="activity-card"
            icon="▣"
            title="Activity Log"
            action={
              <button className="clear-log" onClick={() => setEvents([])} type="button">
                Clear Log
              </button>
            }
          >
            <div className="terminal-log" aria-live="polite">
              {events.length === 0 ? (
                <p className="empty-log">[{clock.toLocaleTimeString([], { hour12: false })}] Awaiting Mission Control activity...</p>
              ) : (
                events.map((event) => (
                  <div className={`log-line ${event.level}`} key={event.id}>
                    <time>[{event.time}]</time>
                    <strong>{event.source}</strong>
                    <span>{event.message}</span>
                  </div>
                ))
              )}
            </div>
          </Panel>
        </section>

        <aside className="right-column">
          <Panel icon="▣" title="Mission Controls">
            <div className="mission-control-list">
              {missions.map((mission) => (
                <button
                  className={`mission-select mission-${mission.id} ${activeMission?.id === mission.id ? "selected" : ""}`}
                  disabled={missionState === "dispatching"}
                  key={mission.id}
                  onClick={() => selectMission(mission)}
                  type="button"
                >
                  <strong>Mission {mission.id}</strong>
                  <span>{missionDetails[mission.id].label}</span>
                </button>
              ))}
              <button className="stop-mission" onClick={stopMission} type="button">
                ■ Stop Mission
              </button>
              <button
                className={`voice-command-button ${voiceListening ? "listening" : ""}`}
                onClick={startVoiceCommand}
                type="button"
              >
                🎙 Voice Command
              </button>
              <div className="voice-command-status">
                <span>{voiceStatus}</span>
                {recognizedText && <strong>{recognizedText}</strong>}
              </div>
            </div>
          </Panel>

          <Panel icon="◇" title="AI Mission Planner">
            <div className="ai-planner">
              <input
                onChange={(event) => setAiPrompt(event.target.value)}
                onKeyDown={(event) => {
                  if (event.key === "Enter") {
                    submitAiMissionPlan();
                  }
                }}
                placeholder="Describe the situation on Mars..."
                type="text"
                value={aiPrompt}
              />
              <div className="ai-example-chips">
                {aiPromptExamples.map((example) => (
                  <button
                    key={example}
                    onClick={() => setAiPrompt(example)}
                    type="button"
                  >
                    {example}
                  </button>
                ))}
              </div>
              <button
                className="ai-plan-button"
                disabled={aiPlanning}
                onClick={submitAiMissionPlan}
                type="button"
              >
                🧠 Plan Mission
              </button>
              <div className="ai-plan-result">
                <span>Intent</span>
                <strong>{aiPlan?.intent ?? "Awaiting prompt"}</strong>
                <span>Selected Mission</span>
                <strong>{aiPlan?.command ?? "--"}</strong>
                <span>Reason</span>
                <p>{aiPlan?.reason ?? "AI planner will explain the selected rover mission."}</p>
                <span>Dispatch Mode</span>
                <strong>{aiPlan?.dispatch_mode?.toUpperCase() ?? "--"}</strong>
              </div>
            </div>
          </Panel>

          <Panel icon="⌁" title="Manual Controls">
            <div className="drive-pad">
              <button
                className="drive-forward"
                disabled={!isConnected || serialBusy}
                onClick={() => sendCommand("FORWARD", "MANUAL")}
                type="button"
              >↑<span>Forward</span></button>
              <button
                className="drive-left"
                disabled={!isConnected || serialBusy}
                onClick={() => sendCommand("LEFT", "MANUAL")}
                type="button"
              >←<span>Left</span></button>
              <button
                className="drive-stop"
                disabled={!isConnected || serialBusy}
                onClick={() => sendCommand("STOP", "MANUAL")}
                type="button"
              >STOP</button>
              <button
                className="drive-right"
                disabled={!isConnected || serialBusy}
                onClick={() => sendCommand("RIGHT", "MANUAL")}
                type="button"
              >→<span>Right</span></button>
              <button
                className="drive-back"
                disabled={!isConnected || serialBusy}
                onClick={() => sendCommand("BACKWARD", "MANUAL")}
                type="button"
              >↓<span>Backward</span></button>
            </div>
            <button
              className="motor-test-button"
              disabled={!isConnected || serialBusy}
              onClick={() => sendCommand("TEST", "MANUAL")}
              type="button"
            >
              Run Motor Test
            </button>
          </Panel>

          <Panel icon="▧" title="Diagnostics">
            <div className="diagnostic-grid">
              {diagnosticCommands.map((diagnostic) => (
                <button
                  className={diagnostic === "SELF_TEST" ? "self-test" : ""}
                  disabled={!isConnected || serialBusy}
                  key={diagnostic}
                  onClick={() => sendCommand(diagnostic, "DIAGNOSTIC")}
                  type="button"
                >
                  {diagnostic}
                </button>
              ))}
            </div>
          </Panel>

          <div className="link-note">
            <span className={mqttConnected ? "online" : ""} />
            {mqttConnected
              ? `MQTT mission link active · last message ${lastMqttMessage}`
              : isConnected
                ? `Development USB Serial · ${connectedPort}`
                : "Simulation mode · backend mission dispatch available"}
          </div>
        </aside>
      </div>

      <footer className="system-footer">
        <span>SU-PAR1 / MISSION CONTROL</span>
        <span>LAST COMMAND: {lastCommand}</span>
        <span>{mqttConnected ? "MQTT LINK ACTIVE" : isConnected ? "USB SERIAL FALLBACK" : "SIMULATION FALLBACK"}</span>
      </footer>

      <aside className="mission-credit" aria-label="Mission concept and development credit">
        <span>Mission Concept &amp; Development</span>
        <strong>Parineeti Gowda</strong>
        <em>Guided by her Lion 🦁</em>
      </aside>
    </main>
  );
}

export default App;
