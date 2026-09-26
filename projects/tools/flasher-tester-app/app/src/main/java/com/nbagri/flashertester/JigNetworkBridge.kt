package com.nbagri.flashertester

import org.json.JSONObject

/**
 * Talks to the DUT's local HTTP /command endpoint through the jig instead
 * of a direct phone-to-DUT WS link -- see
 * docs/testing/JIG_NETWORK_BRIDGE_SPEC.md. The jig joins the DUT's own
 * SoftAP itself ([joinDutSoftAp]), so the phone's own WiFi state stops
 * being relevant to any of this; only the phone-to-jig USB link matters,
 * the same reliable channel already used for PING/RELAY?/PULSE:<n>.
 *
 * SoftAP-phase only (Phase 1 of the spec's build plan) -- the
 * office-WiFi/MQTT phase still goes through MqttCommander on the phone
 * until that's migrated in a later phase.
 */
class JigNetworkBridge(private val jig: JigSerialClient) {

    /** Joins the DUT's own SoftAP -- fixed shared password, same as the
     *  app's other SoftAP-password assumptions (BenchConfig, ApiClient). */
    fun joinDutSoftAp(deviceId: String): Boolean =
        jig.joinAp(deviceId, SOFTAP_PASSWORD)?.startsWith("JOINED:") == true

    fun leaveWifi() {
        jig.leaveWifi()
    }

    /**
     * Sends {"cmd": cmd, ...extra} to the DUT via the jig's HTTP client and
     * returns the "data" field (or the whole response if there's no "data"
     * key) -- same return shape as DutWsClient.sendCommand(), so call
     * sites barely change moving from one to the other.
     */
    fun sendCommand(cmd: String, extra: JSONObject = JSONObject(), timeoutMs: Int = 9000): JSONObject? {
        val payload = JSONObject().put("cmd", cmd)
        extra.keys().forEach { key -> payload.put(key, extra.get(key)) }
        val reply = jig.httpCommand(payload.toString(), timeoutMs) ?: return null
        if (!reply.startsWith("HTTP_OK:")) return null
        val json = runCatching { JSONObject(reply.removePrefix("HTTP_OK:")) }.getOrNull() ?: return null
        return json.optJSONObject("data") ?: json
    }

    fun deviceInfo(timeoutMs: Int = 9000): JSONObject? = sendCommand("device_info", timeoutMs = timeoutMs)

    companion object {
        // Must match firmware/include/Config.h's SOFTAP_PASSWORD exactly --
        // same shared-secret caveat flagged there (TODO: derive per-device)
        // and in PRODUCTION_TOOL_SPEC.md §9.1. Update Config.h, this
        // constant, and the jig firmware together if it ever changes.
        private val SOFTAP_PASSWORD = BuildConfig.SOFTAP_PASSWORD  // from secrets.properties (gitignored)
    }
}
