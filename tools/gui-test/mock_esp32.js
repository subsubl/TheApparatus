/**
 * Mock ESP32 web console server.
 * Serves the real SPA (gui.html, extracted from firmware) and emulates:
 *   GET  /            -> SPA
 *     WS /ws          -> telemetry @20Hz + config + command echo/ack
 * Received client->server messages are recorded to received.log for
 * test assertions.
 */
const http = require("http");
const fs = require("fs");
const path = require("path");
const { WebSocketServer } = require("ws");

const PORT = process.env.MOCK_PORT || 8765;
const HZ = 20;

const config = {
  D_min: 60, D_max: 450, hysteresis: 15, gamma_exponent: 1.8,
  breathing_depth_M: 0.35, slew_rate_limit: 4,
  variance_threshold_cm: 5, breath_threshold: 0.45,
  pi_zone_near_cm: 150, pi_zone_far_cm: 350,
  lunge_threshold_cm_s: -35, retreat_threshold_cm_s: 35,
  pwm_min_clamp: 0, pwm_max_clamp: 255,
  gate_sensitivity: [50, 50, 50, 50, 50, 50, 50, 50, 50],
  vactrol_count: 6, relay_count: 8,
  ave5_buttons: ["(unassigned)", "STILL (freeze)", "STROBE", "MOSAIC", "PAINT",
    "NEGATIVE", "CUT (bus switch)", "A/B (bus select)", "WIPE (arm)",
    "WIPE PATTERN select", "SUPERIMPOSE (camera key)", "FADE (auto fade)"],
  ave5_pots: ["(free use / unlabeled)", "Mix/T-Bar lever", "Color balance X",
    "Color balance Y", "Wipe speed", "Effect level", "Fade lever", "Audio level"],
  vactrol: Array.from({ length: 6 }, (_, i) => ({
    source_mode: i === 0 ? 1 : 0, dynamic_slew: false,
    min_clamp: 0, max_clamp: 1023, slew_per_ms: 2.0, gamma: 1.0,
    manual_value: 128, ave5_pot: i + 1, pin: [13, 14, 27, 26, 25, 33][i] })),
  fx: Array.from({ length: 8 }, (_, i) => ({
    name: "WJ-BTN" + (i + 1), trigger: 0, press_length_ms: 120,
    press_count: 1, press_gap_ms: 150, pin: [4,18,19,21,22,23,32,15][i],
    clock_enable: false, clock_interval_ms: 5000, ave5_button: i + 1 })),
  boot: {
    enabled: true, start_delay_ms: 3000, step_count: 2,
    steps: Array.from({ length: 12 }, () => ({
      relay: 0, presses: 1, length_ms: 120, gap_ms: 150, wait_after_ms: 500 })) }
};

const received = [];
fs.writeFileSync(path.join(__dirname, "received.log"), "");

let phaseT = 0;
function telemetry() {
  // Synthetic approach -> stationary-breathing cycle, ~6 s period
  const t = (phaseT = (phaseT + 1 / HZ) % 6);
  let state = 1, dist = 200 + 120 * Math.sin(t);
  if (t > 3 && t < 5) { state = 2; dist = 150 + 1.5 * Math.sin(t * Math.PI * 2 / 1.2); }
  const gates = new Array(9).fill(0).map((_, i) =>
    Math.max(0, Math.round(180 * Math.exp(-Math.pow(i - 2, 2)) *
      (state === 2 ? 1 + 0.35 * Math.sin(t * Math.PI * 2 / 1.2) : 1))));
  return {
    type: "telemetry",
    payload: {
      state, state_name: ["IDLE","MACRO","MICRO","CONTACT"][state],
      distance_raw: Math.round(dist + 3),
      distance_filtered: +(dist.toFixed(1)),
      stationary_energy: gates, peak_gate: 2,
      biquad_raw: +(0.4 * Math.sin(t * Math.PI * 2 / 1.2)).toFixed(4),
      agc_normalized: +(state === 2 ? Math.sin(t * Math.PI * 2 / 1.2) : 0.05).toFixed(3),
      mix_pwm: state === 2 ? 140 : 90, base_pwm_f: 138, gamma_shaped: 0.55,
      pi_trigger: false,
      relay_pressed: [false,false,false,false,false,false,false,false],
      relay_seq: [false,false,false,false,false,false,false,false],
      vactrol_val: [128,64,64,90,10,77]
    }
  };
}

const server = http.createServer((req, res) => {
  if (req.url === "/") {
    res.writeHead(200, { "Content-Type": "text/html" });
    res.end(fs.readFileSync(path.join(__dirname, "gui.html")));
  } else { res.writeHead(404); res.end(); }
});
const wss = new WebSocketServer({ server });
wss.on("connection", (ws) => {
  const iv = setInterval(() => ws.readyState === 1 && ws.send(JSON.stringify(telemetry())), 1000 / HZ);
  ws.on("message", (raw) => {
    const msg = JSON.parse(raw.toString());
    received.push(msg);
    fs.appendFileSync(path.join(__dirname, "received.log"), JSON.stringify(msg) + "\n");
    if (msg.type === "get_config") ws.send(JSON.stringify({ type: "config", payload: config }));
    else if (msg.type === "save_config") ws.send(JSON.stringify({ type: "saved" }));
    else if (msg.type === "relay_pin") ws.send(JSON.stringify({ type: msg.pin === 99 ? "pin_fail" : "pin_ok" }));
    else if (msg.type === "vactrol_pin") ws.send(JSON.stringify({ type: msg.pin === 99 ? "pin_fail" : "pin_ok" }));
    else if (msg.type === "vactrol_source") {
      if (config.vactrol[msg.ch]) config.vactrol[msg.ch].source_mode = msg.mode;
    }
    else if (msg.type === "vactrol_pot") {
      if (config.vactrol[msg.ch]) config.vactrol[msg.ch].ave5_pot = msg.pot;
    }
    else if (msg.type === "relay_ave5_button") {
      if (config.fx[msg.index]) config.fx[msg.index].ave5_button = msg.button;
    }
    else if (msg.type === "add_channel") {
      if (msg.bank === "vactrol" && config.vactrol.length < 14) {
        config.vactrol.push({ source_mode: 0, dynamic_slew: false, min_clamp: 0, max_clamp: 1023, slew_per_ms: 2.0, gamma: 1.0, manual_value: 0, ave5_pot: 0, pin: 12 });
        config.vactrol_count = config.vactrol.length;
        ws.send(JSON.stringify({ type: "channel_added" }));
        ws.send(JSON.stringify({ type: "config", payload: config }));
      } else if (msg.bank === "relay" && config.fx.length < 14) {
        config.fx.push({ name: "WJ-BTN" + (config.fx.length + 1), trigger: 0, press_length_ms: 120, press_count: 1, press_gap_ms: 150, pin: 16, clock_enable: false, clock_interval_ms: 5000, ave5_button: 0 });
        config.relay_count = config.fx.length;
        ws.send(JSON.stringify({ type: "channel_added" }));
        ws.send(JSON.stringify({ type: "config", payload: config }));
      } else {
        ws.send(JSON.stringify({ type: "channel_failed" }));
      }
    }
    else if (msg.type === "remove_channel") {
      if (msg.bank === "vactrol" && config.vactrol.length > 1) {
        config.vactrol.pop();
        config.vactrol_count = config.vactrol.length;
        ws.send(JSON.stringify({ type: "channel_removed" }));
        ws.send(JSON.stringify({ type: "config", payload: config }));
      } else if (msg.bank === "relay" && config.fx.length > 1) {
        config.fx.pop();
        config.relay_count = config.fx.length;
        ws.send(JSON.stringify({ type: "channel_removed" }));
        ws.send(JSON.stringify({ type: "config", payload: config }));
      } else {
        ws.send(JSON.stringify({ type: "channel_failed" }));
      }
    }
  });
  ws.on("close", () => clearInterval(iv));
});

server.listen(PORT, () => console.log(`MOCK ESP32 READY on :${PORT}`));