package com.nbagri.flashertester

import android.content.Context
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import org.eclipse.paho.client.mqttv3.MqttClient
import org.eclipse.paho.client.mqttv3.MqttConnectOptions
import org.eclipse.paho.client.mqttv3.persist.MemoryPersistence
import org.json.JSONObject
import java.util.concurrent.CountDownLatch
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit

/**
 * Drives the DUT over MQTT once it's off the SoftAP and on the office
 * WiFi -- see docs/testing/PRODUCTION_TOOL_SPEC_V2.md §6.2/§7. The
 * firmware's MQTT command topic routes into the exact same
 * handleCommand() dispatcher as the local WS API (main.cpp -- "return
 * value unused on the MQTT path"), so relay_test/calibrate/rtc_sync/
 * factory_reset all already work here with zero firmware changes.
 * There's no per-command reply on this path though -- results only
 * show up on the next periodic status publish (STATUS_PUBLISH_INTERVAL_MS
 * = 5s in Config.h), which is what [latestStatus] waits out.
 *
 * One session per unit's MQTT-phase re-verification (connect once,
 * publish/observe several times, close once) rather than MqttChecker's
 * one-shot connect-wait-disconnect, since this needs several
 * back-to-back round trips instead of a single proof-of-life check.
 */
class MqttCommander(private val context: Context, private val deviceId: String) {
    private val topicCommand = "agrisense/FG1/$deviceId/command"
    private val topicStatus = "agrisense/FG1/$deviceId/status"

    private var client: MqttClient? = null
    private var cellular: Network? = null
    private var networkCallback: ConnectivityManager.NetworkCallback? = null
    private var bound = false
    private val statusQueue = LinkedBlockingQueue<JSONObject>()

    /** Connects and subscribes to the status topic. Same cellular-bind
     *  reasoning as MqttChecker -- the phone's WiFi is irrelevant/idle
     *  during this phase (§6.2), but don't rely on it accidentally
     *  having a route either. */
    fun connect(): Boolean {
        val cm = context.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
        val (net, callback) = requestCellularNetwork(cm, 8000)
        cellular = net
        networkCallback = callback
        bound = net?.let { cm.bindProcessToNetwork(it) } ?: false

        return try {
            val c = MqttClient(BROKER_URI, MqttClient.generateClientId(), MemoryPersistence())
            val options = MqttConnectOptions().apply {
                userName = MQTT_USER
                password = MQTT_PASS.toCharArray()
                connectionTimeout = 10
                isCleanSession = true
            }
            c.connect(options)
            c.subscribe(topicStatus) { _, message ->
                runCatching { JSONObject(String(message.payload)) }.getOrNull()?.let { statusQueue.offer(it) }
            }
            client = c
            true
        } catch (e: Exception) {
            android.util.Log.w("flashertester", "MqttCommander connect failed: ${e.message}")
            false
        }
    }

    /** Fire-and-forget -- same {"cmd": ...} shape the local WS API uses. */
    fun publishCommand(cmd: String, extra: JSONObject = JSONObject()) {
        val payload = JSONObject().put("cmd", cmd)
        extra.keys().forEach { key -> payload.put(key, extra.get(key)) }
        runCatching {
            client?.publish(topicCommand, payload.toString().toByteArray(), 1, false)
        }.onFailure { e -> android.util.Log.w("flashertester", "MqttCommander publish failed: ${e.message}") }
    }

    /**
     * Drains whatever status messages arrive within [timeoutMs] and
     * returns the freshest one (or null if none arrived). Deliberately
     * not "return the first message" -- a message already in flight
     * when this is called could reflect state from *before* the
     * command that prompted the read, so waiting out close to a full
     * publish interval and taking the last one is a better proxy for
     * "current state" than grabbing whatever's first in the queue.
     */
    fun latestStatus(timeoutMs: Long = 7000): JSONObject? {
        statusQueue.clear()
        var last: JSONObject? = null
        val deadline = System.currentTimeMillis() + timeoutMs
        while (true) {
            val remaining = deadline - System.currentTimeMillis()
            if (remaining <= 0) break
            val msg = statusQueue.poll(remaining, TimeUnit.MILLISECONDS) ?: break
            last = msg
        }
        return last
    }

    /**
     * Publishes [cmd] up to [attempts] times, [intervalMs] apart, checking
     * [confirmedBy] against the status seen after each attempt. Returns the
     * status that satisfied [confirmedBy], or the last status seen if none
     * did (or null if the DUT never published one at all).
     *
     * Exists because a single publishCommand() has a real chance of being
     * silently dropped: the DUT's MQTT library (PubSubClient) has no
     * persistent session or QoS>0 support, so a command published while
     * it's mid-reconnect just never arrives -- not queued, not redelivered.
     * 2026-09-18 bench finding: this office network's DUT connection visibly
     * flaps (repeated ASSOC_LEAVE/reconnect in the serial log) within the
     * same window a single force_local_mode publish was sent, and it took
     * two single-shot attempts to still leave the DUT connected -- spreading
     * several attempts across a few seconds gives it more chances to land
     * during one of the DUT's actually-connected moments.
     */
    fun publishCommandWithRetry(
        cmd: String, extra: JSONObject = JSONObject(),
        attempts: Int = 3, intervalMs: Long = 2500,
        confirmedBy: (JSONObject) -> Boolean
    ): JSONObject? {
        var last: JSONObject? = null
        repeat(attempts) {
            publishCommand(cmd, extra)
            val status = latestStatus(timeoutMs = intervalMs)
            if (status != null) last = status
            if (status != null && confirmedBy(status)) return status
        }
        return last
    }

    fun close() {
        val cm = context.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
        runCatching { client?.disconnect(); client?.close() }
        client = null
        if (bound) cm.bindProcessToNetwork(null)
        networkCallback?.let { runCatching { cm.unregisterNetworkCallback(it) } }
    }

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

    companion object {
        // Matches MqttChecker.kt / firmware/include/Config.h exactly --
        // update all three together if they ever change.
        private const val BROKER_URI = "tcp://mqtt.agrisenseandcontrol.in:1883"
        private val MQTT_USER = BuildConfig.MQTT_USER  // from secrets.properties (gitignored)
        private val MQTT_PASS = BuildConfig.MQTT_PASS  // from secrets.properties (gitignored)
    }
}
