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
import androidx.appcompat.app.AppCompatActivity
import com.hoho.android.usbserial.driver.UsbSerialDriver
import com.hoho.android.usbserial.driver.UsbSerialProber
import java.io.IOException
import java.io.InputStream

/**
 * NB Agri Flasher — two independent flows sharing the same USB-OTG flashing
 * engine (NativeFlasher/UsbSerialTransport/android_port.c, proven in the
 * USB-OTG spike, see SPIKE_SPEC.md):
 *
 *  - **Real flow** (login section → picker section): phone+OTP login against
 *    agrisense-webapp's NB Agri Flasher API, fetches the caller's live grant,
 *    lists builds for a granted product, downloads the selected one fresh
 *    (never cached — see ApiClient's doc comment) and flashes it to the app
 *    partition only. This is what ships to Kamta.
 *  - **Bench tools** (bottom section, unchanged from the spike): local
 *    local .bin files under assets/, no backend involved — kept for bench testing without
 *    needing a server running.
 *
 * WiFi/SoftAP flashing (startWifiFlash/WifiOtaFlasher) stays bench-only and
 * isn't wired into the real flow — see the parked status in
 * projects/tools/usb-otg-flash-spike/README.md.
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
    private lateinit var serverUrlInput: EditText
    private lateinit var phoneInput: EditText
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
        serverUrlInput = findViewById(R.id.serverUrlInput)
        phoneInput = findViewById(R.id.phoneInput)
        otpInput = findViewById(R.id.otpInput)
        verifyCodeButton = findViewById(R.id.verifyCodeButton)
        grantLabelText = findViewById(R.id.grantLabelText)
        productSpinner = findViewById(R.id.productSpinner)
        buildListContainer = findViewById(R.id.buildListContainer)

        serverUrlInput.setText(api.baseUrl)

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

        findViewById<Button>(R.id.flashButton).setOnClickListener { ensureUsbPermission { startFlash() } }
        findViewById<Button>(R.id.wifiFlashButton).setOnClickListener { startWifiFlash() }

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
        api.baseUrl = serverUrlInput.text.toString().trim()
        val phone = phoneInput.text.toString().trim()
        if (phone.isEmpty()) {
            log("Enter a phone number first.")
            return
        }
        Thread {
            try {
                api.requestOtp(phone)
                runOnUiThread {
                    otpInput.visibility = android.view.View.VISIBLE
                    verifyCodeButton.visibility = android.view.View.VISIBLE
                    log("Code sent to $phone.")
                }
            } catch (e: Exception) {
                runOnUiThread { log("Could not send code: ${e.message}") }
            }
        }.start()
    }

    private fun verifyCode() {
        val phone = phoneInput.text.toString().trim()
        val code = otpInput.text.toString().trim()
        if (code.isEmpty()) {
            log("Enter the code from the SMS.")
            return
        }
        Thread {
            try {
                api.verifyOtp(phone, code)
                runOnUiThread {
                    log("Logged in.")
                    showPickerSection()
                }
            } catch (e: Exception) {
                runOnUiThread { log("Login failed: ${e.message}") }
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
                runOnUiThread { log("Could not load access: ${e.message}") }
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
                runOnUiThread { log("Could not fetch builds: ${e.message}") }
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
            button.setOnClickListener { ensureUsbPermission { downloadAndFlash(build) } }
            buildListContainer.addView(button)
        }
    }

    private fun downloadAndFlash(build: ApiClient.Build) {
        statusText.text = "Downloading ${build.product} ${build.version}…"
        log("---- downloading ${build.product} ${build.version} (${build.variant}) ----")
        Thread {
            val bytes = try {
                api.downloadBuild(build.id)
            } catch (e: Exception) {
                runOnUiThread { log("Download failed: ${e.message}") }
                return@Thread
            }
            runOnUiThread {
                log("Downloaded ${bytes.size / 1024} KB, flashing…")
                flashOverUsb(bootloader = null, partitions = null, app = bytes, appOffset = APP_OFFSET) { result ->
                    val ok = result == 0
                    api.reportResult(
                        build.id,
                        result = if (ok) "flash_ok" else "flash_failed",
                        detail = if (ok) null else FlashResult.describe(result),
                    )
                }
            }
        }.start()
    }

    // ==================== Shared USB plumbing ====================

    private fun findDriver(): UsbSerialDriver? =
        UsbSerialProber.getDefaultProber().findAllDrivers(usbManager).firstOrNull()

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

    /** Bench button: flashes bootloader+partitions+app from local assets. */
    private fun startFlash() {
        val bootloader = readAsset("bootloader.bin")
        val partitions = readAsset("partitions.bin")
        val app = readAsset("firmware.bin")
        if (bootloader == null && partitions == null && app == null) {
            log(
                "No .bin files in assets/. See assets/README.md — copy a build from " +
                    "products/FG1-flowguard/firmware/.pio/build/esp32dev_ds1307/ before running."
            )
            return
        }
        flashOverUsb(bootloader, BOOTLOADER_OFFSET, partitions, PARTITIONS_OFFSET, app, APP_OFFSET, null)
    }

    /** Real flow: only ever writes the app partition — bootloader/partitions came from Avinash's bench flash. */
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
            transport.close()

            runOnUiThread {
                val description = FlashResult.describe(result)
                log("Result: $description ($result)")
                statusText.text = if (result == 0) "Flash succeeded" else "Flash failed: $description"
            }
            onDone?.invoke(result)
        }.start()
    }

    /**
     * Flashes over the board's SoftAP via ElegantOTA (LocalServer.cpp) — no USB
     * involved. Only touches assets/firmware.bin: OTA replaces just the running
     * app image, not the bootloader/partition table, so there's nothing for the
     * other two assets to do here.
     */
    private fun startWifiFlash() {
        val firmware = readAsset("firmware.bin")
        if (firmware == null) {
            log("No firmware.bin in assets/ — see assets/README.md.")
            return
        }

        statusText.text = "Flashing over WiFi…"
        progressBar.progress = 0
        progressText.text = getString(R.string.progress_idle)
        log("---- WiFi flash attempt starting (host $SOFTAP_HOST) ----")

        Thread {
            val result = WifiOtaFlasher(this).flash(SOFTAP_HOST, firmware) { percent ->
                runOnUiThread {
                    progressBar.progress = percent
                    progressText.text = "firmware $percent%"
                }
            }
            runOnUiThread {
                when (result) {
                    is WifiOtaFlasher.Result.Success -> {
                        log("Result: SUCCESS — board is rebooting into the new firmware")
                        statusText.text = "WiFi flash succeeded"
                    }
                    is WifiOtaFlasher.Result.Failure -> {
                        log("Result: FAILED — ${result.message}")
                        statusText.text = "WiFi flash failed"
                    }
                }
            }
        }.start()
    }

    private fun readAsset(name: String): ByteArray? = try {
        assets.open(name).use(InputStream::readBytes)
    } catch (e: IOException) {
        null
    }

    private fun log(message: String) {
        android.util.Log.i("flasherspike", message)
        logText.append("\n$message")
    }

    companion object {
        private const val ACTION_USB_PERMISSION = "com.nbagri.flasherspike.USB_PERMISSION"
        private const val BAUD_RATE = 115200

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
