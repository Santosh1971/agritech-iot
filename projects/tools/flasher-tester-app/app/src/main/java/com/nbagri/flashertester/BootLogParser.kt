package com.nbagri.flashertester

/**
 * Checks a captured boot log against the same required markers and
 * benign-error allowlist as testing/serial_monitor.py -- kept in sync
 * with that file by hand since there's no shared source between Python
 * and Kotlin. Update both if firmware's boot log format changes.
 */
object BootLogParser {

    private val REQUIRED_MARKERS = listOf(
        "\\[BOOT\\] SmartWaterController starting" to "Boot",
        "\\[NVS\\] Initialized OK" to "NVS",
        "\\[RTC\\] Time:" to "RTC",
        "\\[RELAY\\] Initialized on pin" to "Relay",
        "\\[FLOW\\] Initialized on pin" to "Flow",
        "\\[BOOT\\] Device ID:" to "Device ID",
    ).map { (pattern, label) -> Regex(pattern) to label }

    // Only ever appear on a genuinely blank NVS partition (a real chip
    // erase, not just an app-partition reflash) -- every actual
    // production unit starts blank, so these are the common case, not
    // an edge case. See serial_monitor.py's BENIGN_ERROR_PATTERNS for
    // the full history of why each one is here.
    private val BENIGN_ERROR_PATTERNS = listOf(
        "nvs_get_str len fail: mqtt_broker NOT_FOUND",
        "nvs_get_str len fail: mqtt_user NOT_FOUND",
        "nvs_get_str len fail: mqtt_pass NOT_FOUND",
        "nvs_get_str len fail: wifi_ssid NOT_FOUND",
        "nvs_get_str len fail: wifi_pass NOT_FOUND",
        "nvs_get_blob len fail: rs_liters NOT_FOUND",
        "nvs_get_str len fail: rs_by NOT_FOUND",
        "Bus already started in Master Mode",
    ).map { Regex(it) }

    private val DEVICE_ID_PATTERN = Regex("\\[BOOT\\] Device ID: (\\S+)")
    // Matches main.cpp's `Serial.printf("[BOOT] Firmware version: %s\n", FIRMWARE_VERSION)`.
    private val FIRMWARE_VERSION_PATTERN = Regex("\\[BOOT\\] Firmware version: (\\S+)")

    data class Result(
        val passed: Boolean,
        val deviceId: String?,
        val firmwareVersion: String?,
        val markersSeen: Map<String, Boolean>,
        val unexpectedErrors: List<String>,
    )

    fun parse(rawLog: String): Result {
        val markersSeen = REQUIRED_MARKERS.associate { (regex, label) -> label to regex.containsMatchIn(rawLog) }
        val deviceId = DEVICE_ID_PATTERN.find(rawLog)?.groupValues?.get(1)
        val firmwareVersion = FIRMWARE_VERSION_PATTERN.find(rawLog)?.groupValues?.get(1)

        val unexpectedErrors = rawLog.lineSequence()
            .filter { it.contains("[E]") }
            .filter { line -> BENIGN_ERROR_PATTERNS.none { it.containsMatchIn(line) } }
            .toList()

        val passed = markersSeen.values.all { it } && unexpectedErrors.isEmpty()
        return Result(passed, deviceId, firmwareVersion, markersSeen, unexpectedErrors)
    }
}
