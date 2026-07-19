// state.js — simulated ESP32 device state for SoftAP dev/testing
// Mirrors the fields your firmware's DeviceStatus/Cycle structs expose over MQTT,
// so the same Flutter models (DeviceStatus, Cycle, HistoryEntry) work unchanged.

const PULSES_PER_LITER = 450.0; // matches FarmFlow; adjust if SWC uses a different sensor constant

const state = {
  deviceId: 'SWC_001_SIM',
  relay: { on: false, sinceMs: Date.now() },
  wifi: { status: 'ap_mode', ledPattern: 'blink_slow' }, // ap_mode | connecting | connected
  flow: {
    pulseCount: 0,
    literPerMin: 0,       // operator-set simulated flow rate
    totalLiters: 0,
    ledOn: false,
  },
  cycles: [
    // { id, mode: 'liter'|'time'|'time_window_liter', target, startTime, active }
  ],
  activeCycle: null,
  leds: [
    { id: 'wifi_led', label: 'WiFi', gpio: 26, state: 'blink_slow' },
    { id: 'flow_led', label: 'Flow Pulse', gpio: 14, state: 'off' },
  ],
  clock: new Date(),
  history: [],
};

let flowTimer = null;

function startFlowLoop(broadcast) {
  // simulate pulses arriving at the rate the operator set via the dashboard,
  // only while the relay is on — same as a real flow sensor only pulsing under water flow
  setInterval(() => {
    state.clock = new Date();
    if (state.relay.on && state.flow.literPerMin > 0) {
      const litersPerTick = state.flow.literPerMin / 60; // per second
      state.flow.totalLiters += litersPerTick;
      state.flow.pulseCount += Math.round(litersPerTick * PULSES_PER_LITER);
      state.flow.ledOn = !state.flow.ledOn; // toggle to simulate pulse blink
    } else {
      state.flow.ledOn = false;
    }
    broadcast(snapshot());
  }, 1000);
}

function snapshot() {
  return {
    deviceId: state.deviceId,
    relay: state.relay,
    wifi: state.wifi,
    flow: {
      literPerMin: state.flow.literPerMin,
      totalLiters: Number(state.flow.totalLiters.toFixed(3)),
      pulseCount: state.flow.pulseCount,
      ledOn: state.flow.ledOn,
    },
    activeCycle: state.activeCycle,
    cycles: state.cycles,
    leds: state.leds,
    clock: state.clock.toISOString(),
  };
}

function setRelay(on) {
  state.relay.on = on;
  state.relay.sinceMs = Date.now();
  return snapshot();
}

function setFlowRate(literPerMin) {
  state.flow.literPerMin = Math.max(0, Number(literPerMin) || 0);
  return snapshot();
}

function injectPulse() {
  state.flow.pulseCount += 1;
  state.flow.totalLiters += 1 / PULSES_PER_LITER;
  return snapshot();
}

function resetTotals() {
  state.flow.totalLiters = 0;
  state.flow.pulseCount = 0;
  return snapshot();
}

function setWifi(status) {
  state.wifi.status = status;
  state.wifi.ledPattern =
    status === 'connected' ? 'solid' : status === 'connecting' ? 'blink_fast' : 'blink_slow';
  const led = state.leds.find((l) => l.id === 'wifi_led');
  if (led) led.state = state.wifi.ledPattern;
  return snapshot();
}

function addCycle(cycle) {
  cycle.id = cycle.id || `cyc_${Date.now()}`;
  state.cycles.push(cycle);
  return snapshot();
}

function removeCycle(id) {
  state.cycles = state.cycles.filter((c) => c.id !== id);
  return snapshot();
}

function addLed({ id, label, gpio }) {
  if (state.leds.find((l) => l.id === id)) return snapshot();
  state.leds.push({ id, label, gpio, state: 'off' });
  return snapshot();
}

module.exports = {
  state,
  snapshot,
  startFlowLoop,
  setRelay,
  setFlowRate,
  injectPulse,
  resetTotals,
  setWifi,
  addCycle,
  removeCycle,
  addLed,
};
