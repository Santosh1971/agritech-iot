// server.js — mock ESP32 SoftAP server for SmartWaterController app development.
// Run this on your dev machine, point the Flutter app's "SoftAP base URL" at it
// instead of the real ESP32's 192.168.4.1, and develop the SoftAP flow with zero hardware.
//
// Endpoints below are a REASONABLE STARTING GUESS at what an ESP32 SoftAP config
// server usually exposes. Adjust paths/payloads in this file to match your actual
// firmware's SoftAP HTTP routes once you confirm them — the dashboard and state
// machine don't need to change, only the route table.

const express = require('express');
const http = require('http');
const path = require('path');
const WebSocket = require('ws');
const state = require('./state');

const app = express();
app.use(express.json());
app.use(express.static(path.join(__dirname, 'public')));
app.get('/', (req, res) => res.sendFile(path.join(__dirname, 'public', 'dashboard.html')));
app.get('/', (req, res) => res.sendFile(path.join(__dirname, 'public', 'dashboard.html')));

const server = http.createServer(app);
const wss = new WebSocket.Server({ server, path: '/ws' });

function broadcast(payload) {
  const msg = JSON.stringify({ type: 'status', data: payload });
  wss.clients.forEach((client) => {
    if (client.readyState === WebSocket.OPEN) client.send(msg);
  });
}

// ---- SoftAP-style REST endpoints ----

// Device status — what the app polls right after connecting to the AP
app.get('/status', (req, res) => res.json(state.snapshot()));

// WiFi provisioning — hand over home WiFi creds like real SoftAP config pages do
app.post('/wifi/config', (req, res) => {
  const { ssid, password } = req.body || {};
  if (!ssid) return res.status(400).json({ error: 'ssid required' });
  state.setWifi('connecting');
  broadcast(state.snapshot());
  setTimeout(() => {
    state.setWifi('connected');
    broadcast(state.snapshot());
  }, 2000); // simulate the real handshake delay
  res.json({ accepted: true, ssid });
});

// Manual relay control
app.post('/command/relay', (req, res) => {
  const { on } = req.body || {};
  res.json(state.setRelay(!!on));
});

// Cycles CRUD
app.get('/cycles', (req, res) => res.json(state.state.cycles));
app.post('/cycles', (req, res) => res.json(state.addCycle(req.body || {})));
app.delete('/cycles/:id', (req, res) => res.json(state.removeCycle(req.params.id)));

// LED registry — supports adding more virtual outputs without a firmware rebuild
app.get('/leds', (req, res) => res.json(state.state.leds));
app.post('/leds', (req, res) => res.json(state.addLed(req.body || {})));

// ---- Dashboard-only endpoints (not part of the real device API — for manual testing) ----
app.post('/sim/flow-rate', (req, res) => res.json(state.setFlowRate(req.body.literPerMin)));
app.post('/sim/pulse', (req, res) => res.json(state.injectPulse()));
app.post('/sim/reset-totals', (req, res) => res.json(state.resetTotals()));
app.post('/sim/wifi-state', (req, res) => res.json(state.setWifi(req.body.status)));

wss.on('connection', (ws) => {
  ws.send(JSON.stringify({ type: 'status', data: state.snapshot() }));
});

state.startFlowLoop(broadcast);

const PORT = process.env.PORT || 4000;
server.listen(PORT, '0.0.0.0', () => {
  console.log(`SWC simulator running:`);
  console.log(`  Dashboard:  http://localhost:${PORT}`);
  console.log(`  Device API: http://localhost:${PORT}/status  (point Flutter app here)`);
  console.log(`  LAN access: http://<this-machine-LAN-IP>:${PORT}  (use this from a phone)`);
});


