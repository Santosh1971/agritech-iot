package com.nbagri.wm1_mini

import android.content.Context
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.os.Handler
import android.os.Looper
import android.util.Log
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.MethodChannel

/**
 * Binds this app's network traffic to the WiFi network even though it
 * has no internet (the ESP32's SoftAP). Without this, Android often
 * routes an app's traffic over mobile data instead of a no-internet
 * WiFi network — even while the phone shows "connected" to that WiFi
 * in system settings — because Android prefers a network with internet
 * for general traffic. Confirmed as the actual cause here: firmware
 * diagnostics showed the phone joining the WiFi (apClients=1) while the
 * app's WebSocket never arrived at all (wsClients stuck at 0).
 *
 * v2: registers ONE long-lived NetworkCallback for the lifetime of the
 * process, instead of calling requestNetwork()/unregisterNetworkCallback()
 * fresh on every retry. The earlier version did that repeatedly — every
 * auto-retry cycle while offline (every few seconds) — which risks
 * exhausting Android's per-app network-request quota after enough
 * cycles; once exhausted, even a "forced" retry can't get a new
 * registration until the process restarts, which is exactly the
 * behavior reported ("retry doesn't work after switching networks,
 * but closing and reopening the app does"). Registering once and
 * letting onAvailable fire again automatically whenever the matched
 * WiFi network changes (Android's own event, not polled) is both the
 * idiomatic use of this API and immune to that exhaustion entirely.
 */
class MainActivity : FlutterActivity() {
    private val channelName = "wm1/network"
    private val tag = "NetworkBinding"
    private val mainHandler = Handler(Looper.getMainLooper())

    private var networkCallback: ConnectivityManager.NetworkCallback? = null
    private var monitoring = false
    private var boundNetwork: Network? = null

    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)
        MethodChannel(flutterEngine.dartExecutor.binaryMessenger, channelName).setMethodCallHandler { call, result ->
            try {
                when (call.method) {
                    "bindWifi" -> ensureWifiMonitoring(result)
                    "unbind" -> { stopMonitoring(); result.success(true) }
                    else -> result.notImplemented()
                }
            } catch (e: Exception) {
                Log.e(tag, "method call '${call.method}' failed", e)
                result.error("network_binding_error", e.message, null)
            }
        }
    }

    private fun connectivityManager(): ConnectivityManager =
        getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager

    /**
     * Idempotent: the first call actually registers the long-lived
     * callback and waits (up to 5s) for an initial WiFi network so it
     * can report a real yes/no. Every call after that is a cheap,
     * instant check of whatever the callback has already bound to —
     * safe to call on every single connection attempt, including from
     * an automatic retry loop, with zero risk of quota exhaustion.
     */
    private fun ensureWifiMonitoring(result: MethodChannel.Result) {
        if (monitoring) {
            result.success(boundNetwork != null)
            return
        }
        monitoring = true

        val cm = connectivityManager()
        val request = NetworkRequest.Builder()
            .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
            // NetworkRequest.Builder() defaults to REQUIRING internet —
            // explicitly drop that, or this request can never match the
            // SoftAP (which has none) and onAvailable never fires.
            .removeCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
            .build()

        var resultDelivered = false
        fun deliverOnce(success: Boolean) {
            if (resultDelivered) return
            resultDelivered = true
            try { result.success(success) } catch (e: Exception) { Log.e(tag, "result.success failed", e) }
        }

        val timeoutRunnable = Runnable { deliverOnce(false) }
        // If the phone isn't on any WiFi yet, onAvailable never fires —
        // this is only for the FIRST call's response; monitoring stays
        // active and will bind automatically the moment WiFi shows up.
        mainHandler.postDelayed(timeoutRunnable, 5000)

        val callback = object : ConnectivityManager.NetworkCallback() {
            // Runs on a system callback thread, not the thread that
            // called requestNetwork — wrapped in try/catch since an
            // uncaught exception here would crash the whole app with no
            // Dart-side stack trace to explain why (confirmed as the
            // likely cause of an earlier reported crash).
            override fun onAvailable(network: Network) {
                try {
                    cm.bindProcessToNetwork(network)
                    boundNetwork = network
                    Log.i(tag, "Bound/rebound process to WiFi network")
                } catch (e: Exception) {
                    Log.e(tag, "bindProcessToNetwork failed", e)
                }
                mainHandler.post {
                    mainHandler.removeCallbacks(timeoutRunnable)
                    deliverOnce(true)
                }
            }

            override fun onLost(network: Network) {
                Log.i(tag, "WiFi network lost")
                if (network == boundNetwork) boundNetwork = null
            }
        }
        networkCallback = callback
        try {
            cm.requestNetwork(request, callback)
        } catch (e: Exception) {
            Log.e(tag, "requestNetwork failed", e)
            mainHandler.removeCallbacks(timeoutRunnable)
            monitoring = false
            deliverOnce(false)
        }
    }

    /** Cloud mode needs a network that actually has internet — this
     * fully tears down monitoring so the process doesn't keep getting
     * silently re-bound back to a no-internet WiFi the moment
     * onAvailable fires again. */
    private fun stopMonitoring() {
        try {
            val cm = connectivityManager()
            networkCallback?.let {
                try { cm.unregisterNetworkCallback(it) } catch (e: Exception) { /* already unregistered */ }
            }
            networkCallback = null
            monitoring = false
            boundNetwork = null
            cm.bindProcessToNetwork(null)
        } catch (e: Exception) {
            Log.e(tag, "unbind failed", e)
        }
    }

    override fun onDestroy() {
        stopMonitoring()
        super.onDestroy()
    }
}
