package com.nbagri.flashertester

import android.content.Context
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import org.eclipse.paho.client.mqttv3.MqttClient
import org.eclipse.paho.client.mqttv3.MqttConnectOptions
import org.eclipse.paho.client.mqttv3.persist.MemoryPersistence
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

/**
 * Independent, broker-side proof that the DUT is actually publishing --
 * subscribes to the DUT's own status topic directly from the phone,
 * rather than trusting the DUT's self-reported device_info.mqtt_connected
 * (or its serial log lines) alone. This is exactly the gap
 * TEST_JIG_SPEC.md's Tier 1 spec (step 8) originally called out: "confirm
 * the broker actually receives the retained status/lwt topics -- don't
 * just trust the DUT's self-report."
 *
 * Broker/credentials match firmware/include/Config.h's MQTT_BROKER/
 * MQTT_USER/MQTT_PASS exactly -- these are a single value shared across
 * every FG1 unit (not per-device), so using them here to subscribe is
 * consistent with how the fleet already operates, not a special case.
 * Update both places together if they ever change.
 */
object MqttChecker {
    private const val BROKER_URI = "tcp://mqtt.agrisenseandcontrol.in:1883"
    private val MQTT_USER = BuildConfig.MQTT_USER  // from secrets.properties (gitignored)
    private val MQTT_PASS = BuildConfig.MQTT_PASS  // from secrets.properties (gitignored)

    /**
     * Subscribes to agrisense/FG1/<deviceId>/status and waits up to
     * timeoutMs for any message to arrive. Returns the payload string on
     * success, or null on timeout/any connection failure -- a failure
     * here (e.g. auth rejected) is itself useful diagnostic information,
     * logged by the caller, not something to throw on.
     *
     * While the phone is WiFi-joined to the DUT's own SoftAP (which has
     * no internet route), a plain connection can end up routed over that
     * dead link instead of the phone's own cellular data -- so this binds
     * the process to a cellular network specifically for the duration of
     * the check, then unbinds immediately after (not left bound, so it
     * doesn't affect the DUT WS calls that run right after this).
     */
    fun waitForStatusMessage(context: Context, deviceId: String, timeoutMs: Long = 15_000): String? {
        val cm = context.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
        val (cellular, callback) = requestCellularNetwork(cm, 8000)
        val bound = cellular?.let { cm.bindProcessToNetwork(it) } ?: false

        val client = MqttClient(BROKER_URI, MqttClient.generateClientId(), MemoryPersistence())
        val latch = CountDownLatch(1)
        var payload: String? = null
        try {
            val options = MqttConnectOptions().apply {
                userName = MQTT_USER
                password = MQTT_PASS.toCharArray()
                connectionTimeout = 10
                isCleanSession = true
            }
            client.connect(options)
            client.subscribe("agrisense/FG1/$deviceId/status") { _, message ->
                payload = String(message.payload)
                latch.countDown()
            }
            latch.await(timeoutMs, TimeUnit.MILLISECONDS)
        } catch (e: Exception) {
            android.util.Log.w("flashertester", "MqttChecker failed: ${e.message}")
            return null
        } finally {
            try {
                client.disconnect()
                client.close()
            } catch (e: Exception) {
                // already gone
            }
            if (bound) cm.bindProcessToNetwork(null)
            callback?.let { runCatching { cm.unregisterNetworkCallback(it) } }
        }
        return payload
    }

    /** Best-effort: returns null (not a failure) if no cellular network is
     *  available within timeoutMs, e.g. a WiFi-only tablet -- caller just
     *  proceeds unbound in that case. */
    private fun requestCellularNetwork(
        cm: ConnectivityManager, timeoutMs: Long
    ): Pair<Network?, ConnectivityManager.NetworkCallback?> {
        val request = NetworkRequest.Builder()
            .addTransportType(NetworkCapabilities.TRANSPORT_CELLULAR)
            .addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
            .build()
        val latch = CountDownLatch(1)
        var result: Network? = null
        val callback = object : ConnectivityManager.NetworkCallback() {
            override fun onAvailable(network: Network) {
                result = network
                latch.countDown()
            }
        }
        return try {
            cm.requestNetwork(request, callback)
            latch.await(timeoutMs, TimeUnit.MILLISECONDS)
            result to callback
        } catch (e: Exception) {
            null to null
        }
    }
}
