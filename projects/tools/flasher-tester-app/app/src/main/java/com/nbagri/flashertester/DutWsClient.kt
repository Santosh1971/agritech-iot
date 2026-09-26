package com.nbagri.flashertester

import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.Response
import okhttp3.WebSocket
import okhttp3.WebSocketListener
import org.json.JSONObject
import java.util.concurrent.CountDownLatch
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit

/**
 * Talks to the DUT's Local WS API -- the exact same protocol
 * mobile-app/flutter_app/lib/services/local_service.dart and
 * tools/fg1_production_tester's DutService use ({"cmd": ...} in,
 * {"ok":..,"cmd":..,"data":..} out). See DutService.kt's Dart
 * equivalent for why sendCommand auto-reconnects: the firmware closes
 * every local WS client the instant a background WiFi retry succeeds
 * (main.cpp's pollBackgroundRetry()), well before it actually drops
 * the SoftAP ~60s later -- a command sent right after `wifi_config`
 * reconnects the DUT would otherwise silently go nowhere on the dead
 * socket. Bench-validated fix, 2026-09-07/08.
 *
 * Assumes the phone is already joined to the DUT's own SoftAP
 * (192.168.4.1 is always its gateway IP in that mode).
 */
class DutWsClient(private val host: String = "192.168.4.1") {

    private val client = OkHttpClient.Builder()
        .readTimeout(0, TimeUnit.MILLISECONDS) // long-lived WS, no per-read timeout
        .build()
    private var webSocket: WebSocket? = null
    @Volatile var connected = false
        private set
    private val incoming = LinkedBlockingQueue<JSONObject>()

    fun connect(timeoutMs: Long = 5000): Boolean {
        close()
        val latch = CountDownLatch(1)
        val request = Request.Builder().url("ws://$host/ws").build()
        webSocket = client.newWebSocket(request, object : WebSocketListener() {
            override fun onOpen(webSocket: WebSocket, response: Response) {
                connected = true
                latch.countDown()
            }

            override fun onMessage(webSocket: WebSocket, text: String) {
                try {
                    val obj = JSONObject(text)
                    if (obj.has("ok") || obj.has("type")) incoming.offer(obj)
                } catch (e: Exception) {
                    // malformed frame, ignore
                }
            }

            override fun onClosed(webSocket: WebSocket, code: Int, reason: String) {
                connected = false
            }

            override fun onFailure(webSocket: WebSocket, t: Throwable, response: Response?) {
                connected = false
                latch.countDown()
            }
        })
        latch.await(timeoutMs, TimeUnit.MILLISECONDS)
        return connected
    }

    fun close() {
        try {
            webSocket?.close(1000, null)
        } catch (e: Exception) {
            // already gone
        }
        webSocket = null
        connected = false
        incoming.clear()
    }

    /**
     * Sends {"cmd": cmd, ...extra} and waits for the next response
     * whose "cmd" field matches. Auto-reconnects first if not
     * currently connected -- see class doc comment.
     */
    fun sendCommand(cmd: String, extra: JSONObject = JSONObject(), timeoutMs: Long = 8000): JSONObject? {
        if (!connected && !connect()) return null

        val payload = JSONObject().put("cmd", cmd)
        extra.keys().forEach { key -> payload.put(key, extra.get(key)) }
        incoming.clear() // drop anything stale from before this call
        webSocket?.send(payload.toString())

        val deadline = System.currentTimeMillis() + timeoutMs
        while (System.currentTimeMillis() < deadline) {
            val remaining = deadline - System.currentTimeMillis()
            val msg = incoming.poll(remaining.coerceAtLeast(0), TimeUnit.MILLISECONDS) ?: return null
            if (msg.optString("cmd") == cmd) {
                return msg.optJSONObject("data") ?: msg
            }
        }
        return null
    }

    fun deviceInfo(timeoutMs: Long = 8000): JSONObject? = sendCommand("device_info", timeoutMs = timeoutMs)

    /** Polls deviceInfo() until predicate is true or timeoutMs elapses. */
    fun waitFor(timeoutMs: Long = 20_000, pollIntervalMs: Long = 1000, predicate: (JSONObject) -> Boolean): JSONObject? {
        val deadline = System.currentTimeMillis() + timeoutMs
        while (System.currentTimeMillis() < deadline) {
            val info = deviceInfo()
            if (info != null && predicate(info)) return info
            Thread.sleep(pollIntervalMs)
        }
        return null
    }
}
