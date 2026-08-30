#pragma once

// ============================================================================
// Water Manager-Mini — Config
//
// Topic scheme and transport pattern mirror FG1's actual, currently-running
// app (products/FG1-flowguard/mobile-app/flutter_app/lib/services/*), not
// the aspirational agrisense-webapp/docs/mqtt-topics.md convention — per
// explicit instruction to match what the FG1 app already does, since
// Kamta's app should feel/behave the same way.
// ============================================================================

#define DEVICE_MODEL_PREFIX "WM1"   // Water Manager - Mini

// Same broker FG1 already points at.
#define MQTT_BROKER_HOST "mqtt.agrisenseandcontrol.in"
#define MQTT_BROKER_PORT 1883
// TODO: FG1 uses a per-PRODUCT credential (fg1-device / asacfg1), not
// per-physical-device — mirroring that here rather than the stronger
// per-device-ACL model docs/mqtt-topics.md describes, so this matches
// what's actually deployed today. Confirm with whoever manages the
// Mosquitto ACLs before shipping more than one WM1 unit this way.
#define MQTT_USERNAME "wm1-device"
#define MQTT_PASSWORD "asacwm1"

// Topics, FG1-style: agrisense/WM1/<deviceId>/<suffix>
//   status          (device -> cloud, retained)      - full status snapshot
//   programs        (device -> cloud)                - current program list, pushed after any change
//   command         (cloud -> device)                - {"cmd": ...}
//   programs_config (cloud -> device, RETAINED)       - {"cmd":"set_programs","programs":[...]}
//   lwt             (device -> cloud, retained)       - {"online": true/false}
#define TOPIC_PREFIX_FMT "agrisense/WM1/%s/"

// SoftAP SSID is MAC-suffixed: WM1_<deviceId>, matching FG1's SWC_001_XXXX
// pattern. Password is a placeholder — confirm/replace before field use.
#define SOFTAP_PASSWORD "12345678"

// Local HTTP+WebSocket server port (matches FG1's port 80 pattern).
#define LOCAL_SERVER_PORT 80

// Status publish interval when idle (event-driven pushes happen
// immediately on state change — this is just the periodic heartbeat).
#define STATUS_PUBLISH_INTERVAL_MS 5000

// NVS namespace for persisted settings (WiFi creds, programs, force_local
// flag). Scheduler's own reboot-survival checkpoint uses a separate
// namespace ("wm1_rt") so a Program-shape change never risks corrupting
// runtime-checkpoint keys or vice versa.
#define NVS_NAMESPACE "wm1"
