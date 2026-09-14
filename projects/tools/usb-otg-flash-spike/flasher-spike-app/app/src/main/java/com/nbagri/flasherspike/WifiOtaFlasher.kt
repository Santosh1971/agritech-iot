package com.nbagri.flasherspike

import android.content.Context
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.util.Base64
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

/**
 * Flashes firmware over WiFi via ElegantOTA's HTTP API — `GET /ota/start?mode=firmware`
 * to open the update, then a multipart `POST /ota/upload` with the bin — to an FG1 board
 * reachable at [host] (the board's default SoftAP gateway IP; see LocalServer.cpp's
 * ElegantOTA wiring and its boot-log "Local fallback active" line for the actual IP if
 * this ever needs to target a board on STA WiFi instead of its own SoftAP).
 *
 * Binds this process to the currently-connected WiFi network for the call's duration.
 * Without that, Android (7+) prefers routing app traffic through a network with
 * validated internet access — e.g. mobile data — over a WiFi network with none, like an
 * ESP32's own SoftAP, which would make every request here silently fail to ever reach
 * the device even while the phone shows "connected" to that SoftAP in system settings.
 */
class WifiOtaFlasher(private val context: Context) {

    sealed class Result {
        data object Success : Result()
        data class Failure(val message: String) : Result()
    }

    fun flash(host: String, firmware: ByteArray, onProgress: (Int) -> Unit): Result {
        val cm = context.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
        val network = bindToWifi(cm)
            ?: return Result.Failure("No WiFi network available to bind to — connect to the board's SoftAP first.")

        return try {
            val startCode = httpGet(network, "http://$host/ota/start?mode=firmware")
            if (startCode != 200) {
                Result.Failure("/ota/start returned HTTP $startCode — is $host reachable and running the OTA portal?")
            } else {
                uploadMultipart(network, "http://$host/ota/upload", firmware, onProgress)
            }
        } catch (e: IOException) {
            Result.Failure("Network error: ${e.message}")
        } finally {
            cm.bindProcessToNetwork(null)
        }
    }

    private fun bindToWifi(cm: ConnectivityManager): Network? {
        val request = NetworkRequest.Builder()
            .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
            .build()
        var result: Network? = null
        val latch = CountDownLatch(1)
        val callback = object : ConnectivityManager.NetworkCallback() {
            override fun onAvailable(network: Network) {
                result = network
                latch.countDown()
            }
        }
        cm.requestNetwork(request, callback)
        latch.await(5, TimeUnit.SECONDS)
        cm.unregisterNetworkCallback(callback)
        result?.let { cm.bindProcessToNetwork(it) }
        return result
    }

    private fun httpGet(network: Network, urlStr: String): Int {
        val conn = network.openConnection(URL(urlStr)) as HttpURLConnection
        conn.connectTimeout = 5000
        conn.readTimeout = 5000
        conn.setRequestProperty("Authorization", basicAuthHeader())
        return try {
            conn.responseCode
        } finally {
            conn.disconnect()
        }
    }

    private fun uploadMultipart(
        network: Network, urlStr: String, firmware: ByteArray, onProgress: (Int) -> Unit
    ): Result {
        val boundary = "----flasherspike${System.currentTimeMillis()}"
        val head = (
            "--$boundary\r\n" +
                "Content-Disposition: form-data; name=\"firmware\"; filename=\"firmware.bin\"\r\n" +
                "Content-Type: application/octet-stream\r\n\r\n"
            ).toByteArray()
        val tail = "\r\n--$boundary--\r\n".toByteArray()

        val conn = network.openConnection(URL(urlStr)) as HttpURLConnection
        conn.doOutput = true
        conn.requestMethod = "POST"
        conn.setRequestProperty("Content-Type", "multipart/form-data; boundary=$boundary")
        conn.setRequestProperty("Authorization", basicAuthHeader())
        conn.connectTimeout = 5000
        // Firmware-side stalls during a flash-erase pause have run past 30s in bench
        // testing (see LocalServer.cpp / platformio.ini's AsyncTCP/RX-timeout fixes) --
        // matching this to the same 30s would silently re-impose the limit those fixes
        // just removed, from the other end of the same connection.
        conn.readTimeout = 120000
        conn.setFixedLengthStreamingMode(head.size + firmware.size + tail.size)

        return try {
            conn.outputStream.use { out ->
                out.write(head)
                var offset = 0
                while (offset < firmware.size) {
                    val len = minOf(CHUNK_SIZE, firmware.size - offset)
                    out.write(firmware, offset, len)
                    out.flush()
                    // Several attempts died mid-transfer with "Broken pipe" or
                    // "unexpected end of stream" well past 50% written — a fast
                    // sender outrunning Update.write()'s NOR flash writes on the
                    // ESP32 side, which can starve its main loop long enough to
                    // trip the task watchdog (worse on SoftAP: the chip is also
                    // running the AP itself). Small chunks + a pause give it room
                    // to keep up instead of relying on TCP backpressure alone.
                    Thread.sleep(CHUNK_DELAY_MS)
                    offset += len
                    onProgress((offset * 100) / firmware.size)
                }
                out.write(tail)
            }
            val code = conn.responseCode
            if (code == 200) {
                Result.Success
            } else {
                // ElegantOTA's response body carries the actual reason (e.g. a specific
                // Update.h error string, "not enough space", a bad-magic-byte complaint) --
                // surfacing only the status code was hiding exactly the detail needed to
                // diagnose a real failure instead of guessing at it.
                val body = conn.errorStream?.bufferedReader()?.use { it.readText() }?.trim()
                val detail = body?.takeIf { it.isNotBlank() } ?: "(no response body)"
                Result.Failure("/ota/upload returned HTTP $code — $detail")
            }
        } finally {
            conn.disconnect()
        }
    }

    private fun basicAuthHeader(): String {
        val creds = Base64.encodeToString("$OTA_USERNAME:$OTA_PASSWORD".toByteArray(), Base64.NO_WRAP)
        return "Basic $creds"
    }

    companion object {
        private const val CHUNK_SIZE = 1024
        private const val CHUNK_DELAY_MS = 15L
        // Must match Config.h's OTA_USERNAME/OTA_PASSWORD on the firmware side.
        private const val OTA_USERNAME = "nbagri"
        private const val OTA_PASSWORD = "flash-nb-2026"
    }
}
