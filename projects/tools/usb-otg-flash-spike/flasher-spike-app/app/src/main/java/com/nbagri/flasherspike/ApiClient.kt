package com.nbagri.flasherspike

import android.content.Context
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL

/**
 * Talks to agrisense-webapp's NB Agri Flasher API (see
 * webapp/agrisense-webapp/app/api/flasher and /api/auth) — email+OTP login
 * (shared with the main AgriSense dashboard), then grant/builds/download/
 * report. Session cookie and server URL persist in SharedPreferences so
 * re-launching the app doesn't require logging in again every time.
 *
 * The server URL is hardcoded to the production domain — there's no reason
 * for Kamta/Avinash to ever see or change it in the UI. For local backend
 * testing, temporarily call the baseUrl setter from a debug build instead
 * of exposing a field for it.
 */
class ApiClient(context: Context) {
    private val prefs = context.getSharedPreferences("nbagri_flasher", Context.MODE_PRIVATE)

    var baseUrl: String
        get() = prefs.getString(PREF_BASE_URL, DEFAULT_BASE_URL) ?: DEFAULT_BASE_URL
        set(value) = prefs.edit().putString(PREF_BASE_URL, value.trimEnd('/')).apply()

    private var sessionCookie: String?
        get() = prefs.getString(PREF_SESSION, null)
        set(value) = prefs.edit().putString(PREF_SESSION, value).apply()

    val isLoggedIn: Boolean get() = sessionCookie != null

    fun logout() {
        prefs.edit().remove(PREF_SESSION).apply()
    }

    fun requestOtp(email: String) {
        val conn = openConnection("/api/auth/request-otp", "POST")
        writeJson(conn, mapOf("email" to email))
        readJson(conn)
    }

    fun verifyOtp(email: String, code: String) {
        val conn = openConnection("/api/auth/verify-otp", "POST")
        writeJson(conn, mapOf("email" to email, "code" to code))
        val code200 = conn.responseCode
        val cookie = conn.headerFields["Set-Cookie"]
            ?.firstOrNull { it.startsWith("$SESSION_COOKIE_NAME=") }
            ?.substringBefore(";")
        readJson(conn, code200)
        sessionCookie = cookie ?: error("Login succeeded but no session was returned")
    }

    data class Grant(val label: String, val products: List<String>)

    fun fetchGrant(): Grant {
        val conn = authedConnection("/api/flasher/grant", "GET")
        val json = readJson(conn)
        val productsArr = json.getJSONArray("products")
        val products = (0 until productsArr.length()).map { productsArr.getString(it) }
        return Grant(json.getString("label"), products)
    }

    data class Build(
        val id: String,
        val product: String,
        val version: String,
        val variant: String,
        val sizeBytes: Long,
        val notes: String?,
    )

    fun fetchBuilds(product: String): List<Build> {
        val conn = authedConnection("/api/flasher/builds?product=$product", "GET")
        val json = readJson(conn)
        val arr = json.getJSONArray("builds")
        return (0 until arr.length()).map { i ->
            val b = arr.getJSONObject(i)
            Build(
                id = b.getString("id"),
                product = b.getString("product"),
                version = b.getString("version"),
                variant = b.getString("variant"),
                sizeBytes = b.getLong("sizeBytes"),
                notes = b.optString("notes").takeIf { it.isNotBlank() },
            )
        }
    }

    /** `mac` is the connected chip's raw MAC (hex, no separators) — the server checks it against
     *  provisioned Device rows and refuses unknown/unprovisioned hardware. */
    fun downloadBuild(buildId: String, mac: String): ByteArray {
        val conn = authedConnection("/api/flasher/download/$buildId?mac=$mac", "GET")
        val code = conn.responseCode
        if (code !in 200..299) {
            throw IllegalStateException(errorMessage(conn, code))
        }
        return conn.inputStream.use { it.readBytes() }
    }

    /** Best-effort — a failed report shouldn't itself be treated as a flash failure. */
    fun reportResult(buildId: String, result: String, mac: String? = null, detail: String? = null) {
        runCatching {
            val conn = authedConnection("/api/flasher/report", "POST")
            writeJson(conn, mapOf("buildId" to buildId, "result" to result, "mac" to mac, "detail" to detail))
            readJson(conn)
        }
    }

    // --- internals ---

    private fun openConnection(path: String, method: String): HttpURLConnection {
        check(baseUrl.isNotBlank()) { "Set the server address first." }
        val conn = URL(baseUrl + path).openConnection() as HttpURLConnection
        conn.requestMethod = method
        conn.connectTimeout = 8000
        conn.readTimeout = 15000
        if (method == "POST") {
            conn.doOutput = true
            conn.setRequestProperty("Content-Type", "application/json")
        }
        sessionCookie?.let { conn.setRequestProperty("Cookie", it) }
        return conn
    }

    private fun authedConnection(path: String, method: String): HttpURLConnection {
        check(isLoggedIn) { "Not logged in." }
        return openConnection(path, method)
    }

    private fun writeJson(conn: HttpURLConnection, data: Map<String, Any?>) {
        val obj = JSONObject()
        data.forEach { (k, v) -> if (v != null) obj.put(k, v) }
        conn.outputStream.use { it.write(obj.toString().toByteArray()) }
    }

    /** Reads the response as JSON, throwing with the server's own error message on failure. */
    private fun readJson(conn: HttpURLConnection, code: Int = conn.responseCode): JSONObject {
        if (code !in 200..299) {
            throw IllegalStateException(errorMessage(conn, code))
        }
        val body = conn.inputStream.bufferedReader().use { it.readText() }
        return runCatching { JSONObject(body) }.getOrElse { JSONObject() }
    }

    private fun errorMessage(conn: HttpURLConnection, code: Int): String {
        val body = conn.errorStream?.bufferedReader()?.use { it.readText() } ?: ""
        val fromServer = runCatching { JSONObject(body).optString("error") }.getOrNull()
        return fromServer?.takeIf { it.isNotBlank() } ?: "HTTP $code"
    }

    companion object {
        private const val PREF_BASE_URL = "base_url"
        private const val PREF_SESSION = "session_cookie"
        private const val SESSION_COOKIE_NAME = "agrisense_session"
        private const val DEFAULT_BASE_URL = "https://agrisenseandcontrol.in"
    }
}
