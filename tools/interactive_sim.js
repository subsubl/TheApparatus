/**
 * @file interactive_sim.js
 * @brief Interactive Desktop ESP32 Simulator Server for The Apparatus
 *
 * Runs a full HTTP + WebSocket simulator server that serves the real firmware SPA
 * and provides live simulation controls (distance slider, touch plate toggle,
 * breathing waveform generator, relay clicks, vactrol outputs).
 *
 * Usage:
 *   node tools/interactive_sim.js [port]
 *   Default port: 8765
 *   Open http://localhost:8765 in browser.
 */

const http = require("http");
const fs = require("fs");
const path = require("path");
const readline = require("readline");

let WebSocketServer;
try {
  WebSocketServer = require("ws").WebSocketServer;
} catch (e) {
  WebSocketServer = require(path.join(__dirname, "gui-test", "node_modules", "ws")).WebSocketServer;
}


const PORT = process.env.PORT || process.argv[2] || 8765;
const HZ = 20;

// Simulation State
const simState = {
  distance_cm: 200,
  touch_active: false,
  breathing_freq_hz: 0.25,
  breathing_active: true,
  manual_override: false,
  scenario_time_s: 0
};

// Configuration
const config = {
  D_min: 60, D_max: 450, hysteresis: 15, gamma_exponent: 1.8,
  breathing_depth_M: 0.35, slew_rate_limit: 4,
  variance_threshold_cm: 5, breath_threshold: 0.45,
  pi_zone_near_cm: 150, pi_zone_far_cm: 350,
  pwm_min_clamp: 0, pwm_max_clamp: 255,
  ave5_buttons: ["(unassigned)", "STILL (freeze)", "STROBE", "MOSAIC", "PAINT",
    "NEGATIVE", "CUT (bus switch)", "A/B (bus select)", "WIPE (arm)",
    "WIPE PATTERN select", "SUPERIMPOSE (camera key)", "FADE (auto fade)"],
  ave5_pots: ["(free use / unlabeled)", "Mix/T-Bar lever", "Color balance X",
    "Color balance Y", "Wipe speed", "Effect level", "Fade lever", "Audio level"],
  vactrol: Array.from({ length: 6 }, (_, i) => ({
    auto_mode: i === 0, min_clamp: 0, max_clamp: 255,
    slew_per_ms: 2, manual_value: 128, ave5_pot: i + 1 })),
  fx: Array.from({ length: 8 }, (_, i) => ({
    name: "WJ-BTN" + (i + 1), trigger: 0, press_length_ms: 120,
    press_count: 1, press_gap_ms: 150, pin: [4,18,19,21,22,23,32,15][i],
    clock_enable: false, clock_interval_ms: 5000, ave5_button: i + 1 })),
  boot: {
    enabled: true, start_delay_ms: 3000, step_count: 2,
    steps: Array.from({ length: 12 }, () => ({
      relay: 0, presses: 1, length_ms: 120, gap_ms: 150, wait_after_ms: 500 })) }
};

// Locate or generate gui.html
const guiHtmlPath = path.join(__dirname, "gui-test", "gui.html");
if (!fs.existsSync(guiHtmlPath)) {
  try {
    const extractScript = path.join(__dirname, "extract_spa.py");
    require("child_process").execSync(`python3 ${extractScript}`);
  } catch (e) {
    console.error("Could not auto-extract gui.html from WebConsole.cpp:", e.message);
  }
}

function generateTelemetry() {
  simState.scenario_time_s = (simState.scenario_time_s + 1 / HZ) % 60;
  const t = simState.scenario_time_s;

  let state = 0; // IDLE
  let dist = simState.distance_cm;

  if (simState.touch_active) {
    state = 3; // CONTACT
  } else if (!simState.manual_override) {
    // 60s scenario: 0-12s approach (420->140), 12-48s stationary (MICRO), 48-60s retreat
    if (t < 12) {
      state = 1; // MACRO
      dist = 420 - (280 * (t / 12));
    } else if (t < 48) {
      state = 2; // MICRO
      dist = 150 + 2 * Math.sin(2 * Math.PI * 0.13 * t);
    } else {
      state = 1; // MACRO
      dist = 140 + (280 * ((t - 48) / 12));
    }
  } else {
    if (dist <= config.D_max && dist >= config.D_min) {
      state = 1;
    }
  }

  const biquad = +(0.4 * Math.sin(2 * Math.PI * simState.breathing_freq_hz * t)).toFixed(4);
  const agc = +(state === 2 ? Math.sin(2 * Math.PI * simState.breathing_freq_hz * t) : 0.05).toFixed(3);

  const gates = Array.from({ length: 9 }, (_, i) => {
    const center = dist / 75;
    return Math.max(0, Math.round(180 * Math.exp(-Math.pow(i - center, 2)) *
      (state === 2 ? 1 + 0.35 * agc : 1)));
  });

  const peak_gate = Math.min(8, Math.max(0, Math.floor(dist / 75)));

  return {
    type: "telemetry",
    payload: {
      state,
      state_name: ["IDLE", "MACRO", "MICRO", "CONTACT"][state],
      distance_raw: Math.round(dist + 2),
      distance_filtered: +dist.toFixed(1),
      stationary_energy: gates,
      peak_gate,
      biquad_raw: biquad,
      agc_normalized: agc,
      mix_pwm: state === 3 ? 255 : (state === 2 ? 140 : (state === 1 ? 90 : 0)),
      base_pwm_f: 138,
      gamma_shaped: 0.55,
      pi_trigger: state === 3,
      relay_pressed: [false, false, false, false, false, false, false, false],
      relay_seq: [false, false, false, false, false, false, false, false],
      vactrol_val: [
        state === 3 ? 255 : (state === 2 ? 140 : (state === 1 ? 90 : 0)),
        64, 64, 90, 10, 77
      ]
    }
  };
}

const server = http.createServer((req, res) => {
  if (req.url === "/" || req.url === "/index.html") {
    res.writeHead(200, { "Content-Type": "text/html" });
    res.end(fs.existsSync(guiHtmlPath) ? fs.readFileSync(guiHtmlPath) : "<h1>gui.html not found</h1>");
  } else if (req.url === "/sim-api") {
    res.writeHead(200, { "Content-Type": "application/json" });
    res.end(JSON.stringify(simState));
  } else {
    res.writeHead(404);
    res.end();
  }
});

const wss = new WebSocketServer({ server });
wss.on("connection", (ws) => {
  const iv = setInterval(() => ws.readyState === 1 && ws.send(JSON.stringify(generateTelemetry())), 1000 / HZ);
  ws.on("message", (raw) => {
    try {
      const msg = JSON.parse(raw.toString());
      if (msg.type === "get_config") ws.send(JSON.stringify({ type: "config", payload: config }));
      else if (msg.type === "save_config") ws.send(JSON.stringify({ type: "saved" }));
      else if (msg.type === "sim_set_distance") {
        simState.distance_cm = msg.distance;
        simState.manual_override = true;
      } else if (msg.type === "sim_set_touch") {
        simState.touch_active = !!msg.touch;
      }
    } catch (e) {}
  });
  ws.on("close", () => clearInterval(iv));
});

server.listen(PORT, () => {
  console.log("=================================================");
  console.log(`  THE APPARATUS — ESP32 INTERACTIVE SIMULATOR  `);
  console.log(`  Server listening on http://localhost:${PORT}`);
  console.log("=================================================");
  console.log("Commands available in terminal:");
  console.log("  d <cm>   : Set target distance in cm (e.g. d 150)");
  console.log("  t        : Toggle Touch Plate (CONTACT override)");
  console.log("  auto     : Resume automatic 60s scenario");
  console.log("=================================================");
});

// Terminal CLI Input listener
const rl = readline.createInterface({ input: process.stdin, output: process.stdout });
rl.on("line", (line) => {
  const parts = line.trim().split(/\s+/);
  const cmd = parts[0].toLowerCase();
  if (cmd === "d" && parts[1]) {
    simState.distance_cm = parseFloat(parts[1]);
    simState.manual_override = true;
    console.log(`[SIM] Target distance set to ${simState.distance_cm} cm (Manual Override ON)`);
  } else if (cmd === "t") {
    simState.touch_active = !simState.touch_active;
    console.log(`[SIM] Touch Plate state: ${simState.touch_active ? "ACTIVE (CONTACT)" : "RELEASED"}`);
  } else if (cmd === "auto") {
    simState.manual_override = false;
    simState.touch_active = false;
    console.log(`[SIM] Automatic 60s scenario resumed.`);
  }
});
