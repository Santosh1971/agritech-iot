package com.nbagri.flashertester

import android.content.Context
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.TimeZone

/** One test step's outcome -- mirrors test_report.dart's StepResult. */
data class StepResult(val name: String, var passed: Boolean? = null, var detail: String = "") {
    fun toJson(): JSONObject = JSONObject().put("passed", passed).put("detail", detail)
}

/** Fixed step order -- see docs/testing/PRODUCTION_TOOL_SPEC_V2.md §5.
 *  The "_mqtt" steps re-verify relay/flow/RTC once the DUT has left the
 *  SoftAP for the office WiFi -- commanded over MQTT, not WS, since the
 *  phone never rejoins the DUT's own network for them (see that doc's
 *  §6.2 for why WS can't reach the DUT at that point at all). */
val PRODUCTION_TEST_STEPS = listOf(
    "flash", "boot_log", "factory_reset", "calibrate",
    "relay_test", "flow_sensor", "rtc", "wifi_mqtt",
    "relay_test_mqtt", "flow_sensor_mqtt", "rtc_mqtt",
    "ship_clean_reset", "factory_reset_confirmed",
)

val STEP_LABELS = mapOf(
    "flash" to "Flash firmware",
    "boot_log" to "Boot log check",
    "factory_reset" to "Factory reset",
    "calibrate" to "Set calibration",
    "relay_test" to "Relay physical test",
    "flow_sensor" to "Flow sensor accuracy",
    "rtc" to "RTC sync (SoftAP)",
    "wifi_mqtt" to "WiFi + MQTT connect",
    "relay_test_mqtt" to "Relay test (via MQTT)",
    "flow_sensor_mqtt" to "Flow sensor (via MQTT)",
    "rtc_mqtt" to "RTC check (via MQTT)",
    "ship_clean_reset" to "Ship-clean reset (via MQTT)",
    "factory_reset_confirmed" to "Factory reset confirmed (SoftAP)",
)

private val ISO = SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss'Z'", Locale.US).apply {
    timeZone = TimeZone.getTimeZone("UTC")
}

data class TestReport(
    val deviceId: String?,
    val firmwareVersion: String?,
    // e.g. "FG1 1.2.0 (esp32dev_ds1307)" -- product/version/variant from
    // the picked ApiClient.Build, replaces the old manual env string now
    // that the build comes from a picker, not a typed environment name.
    val buildLabel: String,
    val timestampUtc: Date,
    val operator: String,
    val station: String,
    val steps: LinkedHashMap<String, StepResult>,
) {
    val overallPassed: Boolean
        get() = steps.isNotEmpty() && steps.values.all { it.passed == true }

    fun toJson(): JSONObject {
        val stepsJson = JSONObject()
        for ((k, v) in steps) stepsJson.put(k, v.toJson())
        return JSONObject()
            .put("device_id", deviceId)
            .put("firmware_version", firmwareVersion)
            .put("build_label", buildLabel)
            .put("timestamp_utc", ISO.format(timestampUtc))
            .put("operator", operator)
            .put("station", station)
            .put("steps", stepsJson)
            .put("overall_passed", overallPassed)
    }

    /** Row shape matching testing/results_logger.py's CSV schema. */
    fun toCsvRow(): String {
        fun esc(s: String) = "\"" + s.replace("\"", "\"\"") + "\""
        val stepsJsonCompact = steps.entries.joinToString(",", "{", "}") { (k, v) -> "\"$k\":${v.passed == true}" }
        return listOf(
            esc(ISO.format(timestampUtc)),
            esc(deviceId ?: "UNKNOWN"),
            esc("production"),
            overallPassed.toString(),
            esc(stepsJsonCompact),
        ).joinToString(",")
    }
}

/** Local, on-device history -- SharedPreferences-backed JSON array, same
 *  "no database needed at this scale" reasoning as the Flutter app's
 *  ReportStore. */
object ReportStore {
    private const val PREFS = "test_reports"
    private const val KEY = "reports_v1"

    fun add(context: Context, report: TestReport) {
        val prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        val arr = JSONArray(prefs.getString(KEY, "[]"))
        arr.put(report.toJson())
        prefs.edit().putString(KEY, arr.toString()).apply()
    }

    fun loadAll(context: Context): List<JSONObject> {
        val prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        val arr = JSONArray(prefs.getString(KEY, "[]"))
        return (0 until arr.length()).map { arr.getJSONObject(it) }
    }

    /** Writes a CSV to the app's cache dir and returns the file, for sharing. */
    fun exportCsv(context: Context): File {
        val reports = loadAll(context)
        val file = File(context.cacheDir, "fg1_test_results_${System.currentTimeMillis()}.csv")
        file.bufferedWriter().use { w ->
            w.write("timestamp_utc,device_id,tier,passed,steps_json\n")
            for (r in reports) {
                val deviceId = r.optString("device_id", "UNKNOWN")
                val timestamp = r.optString("timestamp_utc")
                val passed = r.optBoolean("overall_passed", false)
                val steps = r.optJSONObject("steps") ?: JSONObject()
                val stepsCompact = steps.keys().asSequence().joinToString(",", "{", "}") { k ->
                    "\"$k\":${steps.optJSONObject(k)?.optBoolean("passed", false) == true}"
                }
                fun esc(s: String) = "\"" + s.replace("\"", "\"\"") + "\""
                w.write("${esc(timestamp)},${esc(deviceId)},${esc("production")},$passed,${esc(stepsCompact)}\n")
            }
        }
        return file
    }
}
