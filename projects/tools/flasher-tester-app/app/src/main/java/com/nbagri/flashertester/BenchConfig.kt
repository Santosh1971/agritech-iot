package com.nbagri.flashertester

import android.content.Context

/**
 * Bench-station settings, persisted in SharedPreferences. Slimmer than
 * the laptop-based design (fg1_production_tester / this app's earlier
 * revision) -- no Flash Bridge host, no manual firmware-env string:
 * the firmware build comes from ApiClient's product/build picker, and
 * the jig is found automatically by probing the USB hub (see
 * MainActivity.identifyUsbPorts()), not configured by hand.
 */
data class BenchConfig(
    // "Test AP" the DUT's STA side joins for the WiFi+MQTT check --
    // the site's office WiFi router (see
    // docs/testing/PRODUCTION_TOOL_SPEC_V2.md §4.1). A phone-hotspot
    // test AP was tried and dropped 2026-09-16: hotspot-ON +
    // WiFi-client-joined-to-the-DUT's-SoftAP at the same time made
    // Android route the hotspot's own internet through that dead-end
    // WiFi link instead of cellular, breaking DNS for the DUT. The
    // office router has its own internet backhaul, so this whole class
    // of problem doesn't apply -- but it does mean these need to be set
    // explicitly per site in Settings before the first run; left blank
    // here deliberately rather than defaulting to stale hotspot
    // credentials that would silently point at the wrong network.
    val testApSsid: String = "",
    val testApPassword: String = "",
    val expectedCalibrationPpl: Int = 450,
    val flowTestPulseCount: Int = 450,
    val operatorName: String = "",
    val stationName: String = "bench-1",
) {
    fun save(context: Context) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().apply {
            putString("test_ap_ssid", testApSsid)
            putString("test_ap_password", testApPassword)
            putInt("expected_ppl", expectedCalibrationPpl)
            putInt("flow_pulse_count", flowTestPulseCount)
            putString("operator", operatorName)
            putString("station", stationName)
            apply()
        }
    }

    companion object {
        private const val PREFS = "bench_config"

        /** Last device ID seen (from a boot log parse) -- separate from the
         *  rest of this class since it changes per-unit, not per-bench.
         *  Persisted (not just an in-memory field) so "Run Tests Only"
         *  still pre-fills correctly after an app restart -- an in-memory
         *  field alone left the operator guessing whether to type
         *  "SWC_001_B468" or "B468" every time. */
        fun saveLastDeviceId(context: Context, deviceId: String) {
            context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
                .putString("last_device_id", deviceId).apply()
        }

        fun loadLastDeviceId(context: Context): String? =
            context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString("last_device_id", null)

        fun load(context: Context): BenchConfig {
            val p = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            val d = BenchConfig()
            return BenchConfig(
                testApSsid = p.getString("test_ap_ssid", d.testApSsid)!!,
                testApPassword = p.getString("test_ap_password", d.testApPassword)!!,
                expectedCalibrationPpl = p.getInt("expected_ppl", d.expectedCalibrationPpl),
                flowTestPulseCount = p.getInt("flow_pulse_count", d.flowTestPulseCount),
                operatorName = p.getString("operator", d.operatorName)!!,
                stationName = p.getString("station", d.stationName)!!,
            )
        }
    }
}
