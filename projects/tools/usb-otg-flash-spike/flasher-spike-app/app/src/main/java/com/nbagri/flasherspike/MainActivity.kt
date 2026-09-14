package com.nbagri.flasherspike

import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.hardware.usb.UsbManager
import android.os.Build
import android.os.Bundle
import android.view.WindowManager
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.EditText
import android.widget.ProgressBar
import android.widget.Spinner
import android.widget.TextView
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import com.hoho.android.usbserial.driver.UsbSerialDriver
import com.hoho.android.usbserial.driver.UsbSerialProber
import java.net.URL

/**
 * NB Agri Flasher — login (email+OTP against agrisense-webapp's NB Agri
 * Flasher API) → product/build picker → flash, using the USB-OTG flashing
 * engine proven in the USB-OTG spike (NativeFlasher/UsbSerialTransport/
 * android_port.c, see SPIKE_SPEC.md). Every build is fetched fresh per
 * flash, never cached (see ApiClient's doc comment).
 *
 * promptTransportChoice() asks USB vs WiFi *before* touching the cable at
 * all — the WiFi path (downloadAndFlashWifi) needs no USB whatsoever,
 * matching its actual point: flashing a board already deployed in the
 * field, where USB access may not be practical at all. It downloads with
 * no device MAC (the server only allows that for admin accounts — see the
 * download route's comment), and identifies the board from its own
 * /status endpoint (fetchSoftApDeviceId()) while still joined to its
 * SoftAP, right after flashing — not beforehand, and not by asking the
 * user to reconnect to the SoftAP a second time. The USB path
 * (downloadAndFlashUsb) is unchanged: reads the MAC over the cable first,
 * same as always.
 *
 * The earlier "Bench tools" section (flashing local .bin files bundled in
 * the app itself, no backend involved) was removed 2026-09-14 — it predated
 * this real flow, had no build-history/version safeguards, and was already
 * causing mix-ups (e.g. someone hitting "Flash over WiFi" here by mistake
 * instead of picking a build above). A blank/new chip's initial full
 * (bootloader+partitions+app) flash still happens separately during
 * production (products/FG1-flowguard/tools/fg1_production_tester), not
 * from this app.
 */
class MainActivity : AppCompatActivity() {

    private lateinit var statusText: TextView
    private lateinit var logText: TextView
    private lateinit var progressBar: ProgressBar
    private lateinit var progressText: TextView
    private lateinit var usbManager: UsbManager
    private lateinit var api: ApiClient

    // Real flow views
    private lateinit var loginSection: android.view.View
    private lateinit var pickerSection: android.view.View
    private lateinit var emailInput: EditText
    private lateinit var otpInput: EditText
    private lateinit var verifyCodeButton: Button
    private lateinit var grantLabelText: TextView
    private lateinit var productSpinner: Spinner
    private lateinit var buildListContainer: android.widget.LinearLayout

    private var currentGrant: ApiClient.Grant? = null
    private var currentBuilds: List<ApiClient.Build> = emptyList()

    /** Set right before requesting USB permission; runs once permission is granted. */
    private var pendingUsbAction: (() -> Unit)? = null

    private val permissionReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            if (intent.action != ACTION_USB_PERMISSION) return
            val granted = intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)
            val action = pendingUsbAction
            pendingUsbAction = null
            if (granted) {
                action?.invoke()
            } else {
                log("USB permission denied.")
            }
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        // Bench testing means many back-to-back flashes while looking at the
        // phone — a screen lock mid-flash means re-authenticating and losing
        // the on-screen log. Kept for the real flow too: a field visit is the
        // same "many attempts, eyes on the phone" situation.
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        statusText = findViewById(R.id.statusText)
        logText = findViewById(R.id.logText)
        progressBar = findViewById(R.id.progressBar)
        progressText = findViewById(R.id.progressText)
        usbManager = getSystemService(Context.USB_SERVICE) as UsbManager
        api = ApiClient(this)

        loginSection = findViewById(R.id.loginSection)
        pickerSection = findViewById(R.id.pickerSection)
        emailInput = findViewById(R.id.emailInput)
        otpInput = findViewById(R.id.otpInput)
        verifyCodeButton = findViewById(R.id.verifyCodeButton)
        grantLabelText = findViewById(R.id.grantLabelText)
        productSpinner = findViewById(R.id.productSpinner)
        buildListContainer = findViewById(R.id.buildListContainer)

        // First line of every session's log — the four things needed to make sense of
        // a field report without asking follow-up questions: app version (did they
        // update?), phone/Android version (OEM WiFi/USB quirks vary a lot), and which
        // server it's actually talking to.
        val versionName = runCatching { packageManager.getPackageInfo(packageName, 0).versionName }.getOrNull()
        log("NB Agri Flasher v$versionName — ${Build.MANUFACTURER} ${Build.MODEL}, Android ${Build.VERSION.RELEASE}, server ${api.baseUrl}")

        val filter = IntentFilter(ACTION_USB_PERMISSION)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            registerReceiver(permissionReceiver, filter, Context.RECEIVER_NOT_EXPORTED)
        } else {
            @Suppress("UnspecifiedRegisterReceiverFlag")
            registerReceiver(permissionReceiver, filter)
        }

        findViewById<Button>(R.id.sendCodeButton).setOnClickListener { sendCode() }
        verifyCodeButton.setOnClickListener { verifyCode() }
        findViewById<Button>(R.id.logoutButton).setOnClickListener {
            api.logout()
            showLoginSection()
        }
        findViewById<Button>(R.id.checkUpdatesButton).setOnClickListener { checkUpdates() }

        findViewById<Button>(R.id.shareLogButton).setOnClickListener { shareLog() }

        if (api.isLoggedIn) showPickerSection() else showLoginSection()
    }

    override fun onDestroy() {
        super.onDestroy()
        unregisterReceiver(permissionReceiver)
    }

    // ==================== Real flow: login ====================

    private fun showLoginSection() {
        loginSection.visibility = android.view.View.VISIBLE
        pickerSection.visibility = android.view.View.GONE
        otpInput.visibility = android.view.View.GONE
        verifyCodeButton.visibility = android.view.View.GONE
    }

    private fun showPickerSection() {
        loginSection.visibility = android.view.View.GONE
        pickerSection.visibility = android.view.View.VISIBLE
        loadGrant()
    }

    private fun sendCode() {
        val email = emailInput.text.toString().trim()
        if (email.isEmpty()) {
            log("Enter an email address first.")
            return
        }
        Thread {
            try {
                api.requestOtp(email)
                runOnUiThread {
                    otpInput.visibility = android.view.View.VISIBLE
                    verifyCodeButton.visibility = android.view.View.VISIBLE
                    log("Code sent to $email.")
                }
            } catch (e: Exception) {
                runOnUiThread { log("Could not send code: ${friendlyErrorMessage(e)}") }
            }
        }.start()
    }

    private fun verifyCode() {
        val email = emailInput.text.toString().trim()
        val code = otpInput.text.toString().trim()
        if (code.isEmpty()) {
            log("Enter the code from the email.")
            return
        }
        Thread {
            try {
                api.verifyOtp(email, code)
                runOnUiThread {
                    log("Logged in.")
                    showPickerSection()
                }
            } catch (e: Exception) {
                runOnUiThread { log("Login failed: ${friendlyErrorMessage(e)}") }
            }
        }.start()
    }

    // ==================== Real flow: product/build picker ====================

    private fun loadGrant() {
        Thread {
            try {
                val grant = api.fetchGrant()
                runOnUiThread {
                    currentGrant = grant
                    grantLabelText.text = grant.label
                    productSpinner.adapter = ArrayAdapter(
                        this, android.R.layout.simple_spinner_dropdown_item, grant.products
                    )
                }
            } catch (e: Exception) {
                runOnUiThread { log("Could not load access: ${friendlyErrorMessage(e)}") }
            }
        }.start()
    }

    private fun checkUpdates() {
        val product = productSpinner.selectedItem as? String
        if (product == null) {
            log("No granted products to check.")
            return
        }
        buildListContainer.removeAllViews()
        log("Checking for $product updates…")
        Thread {
            try {
                val builds = api.fetchBuilds(product)
                runOnUiThread {
                    currentBuilds = builds
                    renderBuildList(builds)
                }
            } catch (e: Exception) {
                runOnUiThread { log("Could not fetch builds: ${friendlyErrorMessage(e)}") }
            }
        }.start()
    }

    private fun renderBuildList(builds: List<ApiClient.Build>) {
        buildListContainer.removeAllViews()
        if (builds.isEmpty()) {
            log("No builds available for this product yet.")
            return
        }
        for (build in builds) {
            val button = Button(this)
            val sizeKb = build.sizeBytes / 1024
            button.text = "${build.product} ${build.version} (${build.variant}) — ${sizeKb} KB"
            button.setOnClickListener { promptTransportChoice(build) }
            buildListContainer.addView(button)
        }
    }

    /** Entry point once a build is tapped — asks USB vs WiFi *before* touching the cable
     *  at all, so the WiFi path genuinely never needs USB (see downloadAndFlashWifi). */
    private fun promptTransportChoice(build: ApiClient.Build) {
        AlertDialog.Builder(this)
            .setTitle(R.string.choose_transport_title)
            .setItems(arrayOf(getString(R.string.choose_transport_usb), getString(R.string.choose_transport_wifi))) { _, which ->
                if (which == 0) {
                    ensureUsbPermission { downloadAndFlashUsb(build) }
                } else {
                    downloadAndFlashWifi(build)
                }
            }
            .setNegativeButton(R.string.cancel_button, null)
            .show()
    }

    /** USB path: read the chip's MAC first (needed for the server's per-device
     *  provisioning check), then download and write straight over the same cable. */
    private fun downloadAndFlashUsb(build: ApiClient.Build) {
        statusText.text = "Identifying device…"
        log("---- identifying device for ${build.product} ${build.version} (${build.variant}) ----")
        Thread {
            val mac = readMacHex()
            if (mac == null) {
                runOnUiThread {
                    log("Could not read the chip's MAC — check the OTG connection and try again.")
                    statusText.text = "Identification failed"
                }
                return@Thread
            }
            runOnUiThread { log("Device MAC: ${mac.chunked(2).joinToString(":")}") }

            val bytes = downloadWithProgress(build, mac) ?: return@Thread
            runOnUiThread {
                flashOverUsb(bootloader = null, partitions = null, app = bytes, appOffset = APP_OFFSET) { result ->
                    val ok = result == 0
                    api.reportResult(
                        build.id,
                        result = if (ok) "flash_ok" else "flash_failed",
                        mac = mac,
                        detail = if (ok) null else FlashResult.describe(result),
                    )
                }
            }
        }.start()
    }

    /** WiFi path: no USB at all. Downloads with no MAC (server only accepts that from
     *  admin accounts — see the download route's comment), then walks through the
     *  SoftAP switch → flash → switch back → report sequence. */
    private fun downloadAndFlashWifi(build: ApiClient.Build) {
        statusText.text = "Downloading build…"
        log("---- downloading ${build.product} ${build.version} (${build.variant}) for WiFi flash — no USB needed ----")
        Thread {
            val bytes = downloadWithProgress(build, mac = null) ?: return@Thread
            runOnUiThread { promptWifiSwitchThenFlash(build, bytes) }
        }.start()
    }

    /** Shared by both paths — off the main thread already when called. Returns null (and
     *  has already logged) on failure. */
    private fun downloadWithProgress(build: ApiClient.Build, mac: String?): ByteArray? {
        runOnUiThread {
            statusText.text = "Downloading build…"
            progressBar.progress = 0
            progressText.text = "download 0%"
        }
        return try {
            val bytes = api.downloadBuild(build.id, mac, expectedSize = build.sizeBytes) { percent ->
                runOnUiThread {
                    progressBar.progress = percent
                    progressText.text = "download $percent%"
                }
            }
            runOnUiThread { log("Downloaded ${bytes.size / 1024} KB.") }
            bytes
        } catch (e: Exception) {
            runOnUiThread {
                log("Download failed: ${friendlyErrorMessage(e)}")
                statusText.text = "Download failed"
                progressText.text = getString(R.string.progress_idle)
            }
            null
        }
    }

    private fun promptWifiSwitchThenFlash(build: ApiClient.Build, bytes: ByteArray) {
        AlertDialog.Builder(this)
            .setTitle(R.string.choose_transport_wifi)
            .setMessage(R.string.wifi_transport_instruction)
            .setPositiveButton(R.string.continue_button) { _, _ -> flashOverWifiReal(build, bytes) }
            .setNegativeButton(R.string.cancel_button, null)
            .show()
    }

    private fun flashOverWifiReal(build: ApiClient.Build, bytes: ByteArray) {
        statusText.text = "Flashing over WiFi…"
        progressBar.progress = 0
        progressText.text = getString(R.string.progress_idle)
        log("---- WiFi flash attempt starting (host $SOFTAP_HOST) for ${build.product} ${build.version} ----")

        Thread {
            val result = WifiOtaFlasher(this).flash(SOFTAP_HOST, bytes) { percent ->
                runOnUiThread {
                    progressBar.progress = percent
                    progressText.text = "firmware $percent%"
                }
            }
            // Still on the SoftAP right now, whether the flash succeeded or not — this is
            // the only moment the board's own /status is reachable, so identify it here
            // rather than asking the user to reconnect to the SoftAP a second time.
            val deviceId = fetchSoftApDeviceId()
            runOnUiThread {
                when (result) {
                    is WifiOtaFlasher.Result.Success -> {
                        log("Result: SUCCESS — board is rebooting into the new firmware")
                        log(if (deviceId != null) "Device ID: $deviceId" else "(could not read device ID from the board's own /status)")
                        statusText.text = "WiFi flash succeeded"
                        promptSwitchBackAndFinish(build, deviceId, ok = true, detail = null)
                    }
                    is WifiOtaFlasher.Result.Failure -> {
                        log("Result: FAILED — ${result.message}")
                        statusText.text = "WiFi flash failed"
                        promptSwitchBackAndFinish(build, deviceId, ok = false, detail = result.message)
                    }
                }
            }
        }.start()
    }

    /** GET http://<SoftAP>/status while still joined to it — see LocalServer.cpp's
     *  handler and main.cpp's doc["device_id"]. Best-effort: null on any failure. */
    private fun fetchSoftApDeviceId(): String? = try {
        val conn = (URL("http://$SOFTAP_HOST/status").openConnection() as java.net.HttpURLConnection).apply {
            connectTimeout = 3000
            readTimeout = 3000
        }
        val body = conn.inputStream.bufferedReader().use { it.readText() }
        org.json.JSONObject(body).optString("device_id").takeIf { it.isNotBlank() }
    } catch (e: Exception) {
        null
    }

    /** Last step of the WiFi flow — the phone needs to be back on real internet for this
     *  report call to reach the server, so ask for that explicitly rather than trying to
     *  guess/detect it. */
    private fun promptSwitchBackAndFinish(build: ApiClient.Build, deviceId: String?, ok: Boolean, detail: String?) {
        AlertDialog.Builder(this)
            .setTitle(R.string.switch_back_title)
            .setMessage(R.string.switch_back_instruction)
            .setPositiveButton(R.string.finish_button) { _, _ ->
                Thread {
                    api.reportResult(
                        build.id,
                        result = if (ok) "flash_ok" else "flash_failed",
                        deviceId = deviceId,
                        detail = detail,
                    )
                    runOnUiThread { log("Reported result to server.") }
                }.start()
            }
            .setNegativeButton(R.string.cancel_button, null)
            .show()
    }

    /** Opens Android's share sheet with the full on-screen log — the fastest way for
     *  Kamta/Avinash to get a field failure in front of us without dictating it over
     *  a call. The startup line (app version, phone model, server) plus every
     *  download/MAC/flash-result line already logged should be enough to diagnose
     *  most issues without a follow-up question. */
    private fun shareLog() {
        val intent = Intent(Intent.ACTION_SEND).apply {
            type = "text/plain"
            putExtra(Intent.EXTRA_SUBJECT, "NB Agri Flasher log")
            putExtra(Intent.EXTRA_TEXT, logText.text.toString())
        }
        startActivity(Intent.createChooser(intent, getString(R.string.share_log_button)))
    }

    // ==================== Shared USB plumbing ====================

    private fun findDriver(): UsbSerialDriver? =
        UsbSerialProber.getDefaultProber().findAllDrivers(usbManager).firstOrNull()

    /** Opens its own short-lived connect session to read the chip's MAC — call off the main thread. */
    private fun readMacHex(): String? {
        val driver = findDriver() ?: return null
        val connection = usbManager.openDevice(driver.device) ?: return null
        val transport = UsbSerialTransport(driver.ports.first())
        if (!transport.open(connection, BAUD_RATE)) return null
        val mac = NativeFlasher.readMac(transport)
        transport.close()
        return mac?.joinToString("") { "%02X".format(it) }
    }

    private fun ensureUsbPermission(onGranted: () -> Unit) {
        val driver = findDriver()
        if (driver == null) {
            log("No USB serial device found. Check the OTG cable and that the board is powered.")
            return
        }
        if (usbManager.hasPermission(driver.device)) {
            onGranted()
            return
        }
        pendingUsbAction = onGranted
        val flags = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) PendingIntent.FLAG_MUTABLE else 0
        // Explicit setPackage() — Android 14+ requires non-exported broadcasts to target a
        // package explicitly; without it delivery to our own RECEIVER_NOT_EXPORTED receiver
        // can silently fail on newer devices.
        val permissionIntent = PendingIntent.getBroadcast(
            this, 0, Intent(ACTION_USB_PERMISSION).setPackage(packageName), flags
        )
        usbManager.requestPermission(driver.device, permissionIntent)
    }

    /** Real flow: only ever writes the app partition — a blank/new chip's initial full
     *  (bootloader+partitions+app) flash is done separately during production, not from
     *  this app (see products/FG1-flowguard/tools/fg1_production_tester). */
    private fun flashOverUsb(
        bootloader: ByteArray?, partitions: ByteArray?, app: ByteArray?, appOffset: Int,
        onDone: ((Int) -> Unit)?,
    ) = flashOverUsb(bootloader, BOOTLOADER_OFFSET, partitions, PARTITIONS_OFFSET, app, appOffset, onDone)

    private fun flashOverUsb(
        bootloader: ByteArray?, bootloaderOffset: Int,
        partitions: ByteArray?, partitionsOffset: Int,
        app: ByteArray?, appOffset: Int,
        onDone: ((Int) -> Unit)?,
    ) {
        val driver = findDriver()
        if (driver == null) {
            log("Device disappeared before flashing could start.")
            return
        }

        statusText.text = "Flashing…"
        progressBar.progress = 0
        progressText.text = getString(R.string.progress_idle)
        log("---- flash attempt starting ----")

        var lastLoggedStage: String? = null
        val listener = FlashProgressListener { stage, percent ->
            runOnUiThread {
                progressBar.progress = percent
                progressText.text = "$stage $percent%"
                if (stage != lastLoggedStage || percent == 100) {
                    log("$stage: $percent%")
                    lastLoggedStage = stage
                }
            }
        }

        Thread {
            val connection = usbManager.openDevice(driver.device)
            if (connection == null) {
                runOnUiThread { log("openDevice() returned null — permission or driver issue.") }
                return@Thread
            }

            val transport = UsbSerialTransport(driver.ports.first())
            if (!transport.open(connection, BAUD_RATE)) {
                runOnUiThread { log("Failed to open serial port at $BAUD_RATE baud.") }
                return@Thread
            }

            val result = NativeFlasher.flash(
                transport,
                bootloader, bootloaderOffset,
                partitions, partitionsOffset,
                app, appOffset,
                listener
            )

            val description = FlashResult.describe(result)
            runOnUiThread {
                log("Result: $description ($result)")
                statusText.text = if (result == 0) {
                    "Flash succeeded — capturing boot log…"
                } else {
                    "Flash failed: $description"
                }
            }
            onDone?.invoke(result)

            // Only on success — on failure the chip isn't reset into run mode (see
            // jni_bridge.c), so there's nothing meaningful to capture here.
            val bootLog = if (result == 0) transport.captureBootLog(BOOT_LOG_DURATION_MS) else ""
            transport.close()

            runOnUiThread {
                if (result == 0) {
                    log(
                        if (bootLog.isNotBlank()) {
                            "---- boot log (${BOOT_LOG_DURATION_MS / 1000}s) ----\n${bootLog.trim()}\n---- end boot log ----"
                        } else {
                            "(no boot log output captured in ${BOOT_LOG_DURATION_MS / 1000}s — board may need a " +
                                "manual reset, or isn't printing at $BAUD_RATE baud)"
                        }
                    )
                    statusText.text = "Flash succeeded"
                }
            }
        }.start()
    }

    private fun log(message: String) {
        android.util.Log.i("flasherspike", message)
        logText.append("\n$message")
    }

    /** Turns a raw network exception into something a field user can act on — the most
     *  common cause by far is still being joined to the device's own SoftAP (which has
     *  no internet route) when the app needs to reach the server, which otherwise
     *  surfaces as a cryptic DNS/connect failure that looks like the app did nothing. */
    private fun friendlyErrorMessage(e: Exception): String {
        val isNetworkError = e is java.net.UnknownHostException ||
            e is java.net.ConnectException ||
            e is java.net.SocketTimeoutException
        return if (isNetworkError) {
            "No internet connection. If this phone is already joined to the device's own " +
                "WiFi network, switch back to your normal WiFi or mobile data first, then " +
                "try again. (${e.javaClass.simpleName})"
        } else {
            e.message ?: e.javaClass.simpleName
        }
    }

    companion object {
        private const val ACTION_USB_PERMISSION = "com.nbagri.flasherspike.USB_PERMISSION"
        private const val BAUD_RATE = 115200

        // How long to listen on the just-flashed port for boot output before giving up —
        // long enough to cover WiFi connect + MQTT connect on a normal boot (see the
        // "Local fallback active" / MQTT connect lines each product's firmware prints).
        private const val BOOT_LOG_DURATION_MS = 30_000L

        // ESP32 SoftAP's default gateway IP — matches LocalServer's boot-log
        // "Local fallback active — SSID: ... IP: 192.168.4.1". Would need to
        // change if targeting a board reachable over STA WiFi instead.
        private const val SOFTAP_HOST = "192.168.4.1"

        // Standard ESP32/Arduino-PlatformIO flash layout — matches
        // products/FG1-flowguard/firmware's min_spiffs.csv partition table.
        private const val BOOTLOADER_OFFSET = 0x1000
        private const val PARTITIONS_OFFSET = 0x8000
        private const val APP_OFFSET = 0x10000
    }
}
