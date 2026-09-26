package com.nbagri.flashertester

import android.app.AlertDialog
import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.hardware.usb.UsbDeviceConnection
import android.hardware.usb.UsbManager
import android.os.Build
import android.os.Bundle
import android.provider.Settings as AndroidSettings
import android.view.Gravity
import android.view.View
import android.view.WindowManager
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.Spinner
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.hoho.android.usbserial.driver.UsbSerialDriver
import com.hoho.android.usbserial.driver.UsbSerialProber
import org.json.JSONObject
import java.util.Date

/**
 * NB Agri Flasher/Tester — fully phone-only FG1 production flash +
 * test tool, no laptop anywhere in the loop:
 *
 *   - Firmware comes from the real NB Agri Flasher backend (ApiClient,
 *     same login/product/build picker as flasher-spike-app's actual
 *     flow) -- the app segment is always fetched fresh per build
 *     picked; bootloader/partitions are bundled locally (they barely
 *     ever change, see assets/README.md) so a genuine blank-chip full
 *     flash needs no laptop compile step.
 *   - DUT and jig are BOTH wired to the phone via one USB-OTG hub (a
 *     POWERED hub -- phones are weak USB power sources in host mode).
 *     Which port is which is found automatically: every connected
 *     USB-serial device gets a PING, and whichever replies PONG is the
 *     jig (see JigSerialClient) -- deterministic regardless of hub
 *     port order, and there's no WiFi-based targeting/ambiguity to
 *     solve at all anymore (that problem was specific to the earlier
 *     ESP8266-WiFi jig design, see jig_firmware/esp8266_wifi's header
 *     comment for the history).
 *   - The "test AP" for the WiFi+MQTT step is normally the operator's
 *     own phone hotspot, not a separate router -- requires the phone
 *     to support simultaneous hotspot + WiFi-client; verify on the
 *     actual device this bench uses.
 */
class MainActivity : AppCompatActivity() {

    private lateinit var statusText: TextView
    private lateinit var logText: TextView
    private lateinit var progressBar: ProgressBar
    private lateinit var progressText: TextView
    private lateinit var retryFlashButton: Button
    private lateinit var joinWifiSection: View
    private lateinit var deviceIdText: TextView
    private lateinit var continueButton: Button
    private lateinit var stepsContainer: LinearLayout
    private lateinit var reportSection: View
    private lateinit var reportResultText: TextView
    private lateinit var saveNextButton: Button
    private lateinit var usbManager: UsbManager
    private lateinit var api: ApiClient

    // Login/picker views
    private lateinit var loginSection: View
    private lateinit var pickerSection: View
    private lateinit var emailInput: EditText
    private lateinit var otpInput: EditText
    private lateinit var verifyCodeButton: Button
    private lateinit var grantLabelText: TextView
    private lateinit var productSpinner: Spinner
    private lateinit var buildListContainer: LinearLayout
    private lateinit var hardwareStatusText: TextView

    private var config: BenchConfig = BenchConfig()
    private var currentGrant: ApiClient.Grant? = null

    private var jigClient: JigSerialClient? = null
    private var dutDriver: UsbSerialDriver? = null

    // Guards against a double-tap on a build (or tapping a different one
    // mid-flash) starting two overlapping USB-OTG sessions against the same
    // port -- observed on the bench 2026-09-15: the loser's port.open()
    // failed cleanly ("Failed to open serial port"), but the winner still
    // succeeded only by luck of timing, not by design.
    @Volatile private var flashInProgress = false

    private var dut: DutWsClient? = null
    private var jigAvailable = false
    private var currentDeviceId: String? = null
    private var currentFirmwareVersion: String? = null
    private var currentBuild: ApiClient.Build? = null
    private val steps = LinkedHashMap<String, StepResult>().apply {
        for (name in PRODUCTION_TEST_STEPS) put(name, StepResult(name))
    }

    private var pendingUsbAction: (() -> Unit)? = null

    private val permissionReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            if (intent.action != ACTION_USB_PERMISSION) return
            val granted = intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)
            val action = pendingUsbAction
            pendingUsbAction = null
            if (granted) action?.invoke() else log("USB permission denied.")
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        // Apps targeting SDK 35+ (this one does) go edge-to-edge by default
        // on Android 15+ -- content draws underneath the status bar unless
        // the app opts back into the old inset-aware layout. Without this,
        // the top row (title + History + Settings) renders partially behind
        // the status bar icons -- confirmed on a real Android 16 phone
        // 2026-09-15 ("I do not see the settings button").
        androidx.core.view.WindowCompat.setDecorFitsSystemWindows(window, true)
        setContentView(R.layout.activity_main)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        // Belt-and-suspenders on top of setDecorFitsSystemWindows above --
        // this was reported still hidden once even after that fix (either a
        // stale APK, or this device needing the padding applied explicitly
        // too), so apply the status bar inset as top padding on the content
        // root directly rather than relying on only one mechanism.
        val contentRoot = findViewById<View>(android.R.id.content)
        androidx.core.view.ViewCompat.setOnApplyWindowInsetsListener(contentRoot) { view, insets ->
            val bars = insets.getInsets(androidx.core.view.WindowInsetsCompat.Type.systemBars())
            view.setPadding(bars.left, bars.top, bars.right, bars.bottom)
            insets
        }

        statusText = findViewById(R.id.statusText)
        logText = findViewById(R.id.logText)
        progressBar = findViewById(R.id.progressBar)
        progressText = findViewById(R.id.progressText)
        retryFlashButton = findViewById(R.id.retryFlashButton)
        joinWifiSection = findViewById(R.id.joinWifiSection)
        deviceIdText = findViewById(R.id.deviceIdText)
        continueButton = findViewById(R.id.continueButton)
        stepsContainer = findViewById(R.id.stepsContainer)
        reportSection = findViewById(R.id.reportSection)
        reportResultText = findViewById(R.id.reportResultText)
        saveNextButton = findViewById(R.id.saveNextButton)
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
        hardwareStatusText = findViewById(R.id.hardwareStatusText)

        config = BenchConfig.load(this)

        val versionName = runCatching { packageManager.getPackageInfo(packageName, 0).versionName }.getOrNull()
        log("NB Agri Flasher/Tester v$versionName — ${Build.MANUFACTURER} ${Build.MODEL}, Android ${Build.VERSION.RELEASE}, server ${api.baseUrl}")

        val filter = IntentFilter(ACTION_USB_PERMISSION)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            registerReceiver(permissionReceiver, filter, Context.RECEIVER_NOT_EXPORTED)
        } else {
            @Suppress("UnspecifiedRegisterReceiverFlag")
            registerReceiver(permissionReceiver, filter)
        }

        findViewById<Button>(R.id.sendCodeButton).setOnClickListener { sendCode() }
        verifyCodeButton.setOnClickListener { verifyCode() }
        findViewById<Button>(R.id.logoutButton).setOnClickListener { api.logout(); showLoginSection() }
        findViewById<Button>(R.id.checkUpdatesButton).setOnClickListener { checkUpdates() }
        findViewById<Button>(R.id.findHardwareButton).setOnClickListener { findHardware() }
        findViewById<Button>(R.id.diagnosticsButton).setOnClickListener { showDiagnosticsDialog() }
        findViewById<Button>(R.id.runTestsOnlyButton).setOnClickListener { promptRunTestsOnly() }
        findViewById<Button>(R.id.forceLocalModeButton).setOnClickListener { promptForceLocalMode() }
        retryFlashButton.setOnClickListener { currentBuild?.let { startFlashForBuild(it) } }
        findViewById<View>(R.id.openWifiSettingsButton).setOnClickListener {
            startActivity(Intent(AndroidSettings.ACTION_WIFI_SETTINGS))
        }
        continueButton.setOnClickListener { runTests() }
        saveNextButton.setOnClickListener { resetForNextUnit() }
        findViewById<Button>(R.id.shareLogButtonTop).setOnClickListener { shareLog() }
        findViewById<Button>(R.id.settingsButton).setOnClickListener { showSettingsDialog() }
        findViewById<Button>(R.id.historyButton).setOnClickListener { showHistoryDialog() }

        renderSteps()
        if (api.isLoggedIn) showPickerSection() else showLoginSection()
    }

    override fun onDestroy() {
        super.onDestroy()
        unregisterReceiver(permissionReceiver)
        dut?.close()
        jigClient?.close()
    }

    // ==================== Login ====================

    private fun showLoginSection() {
        loginSection.visibility = View.VISIBLE
        pickerSection.visibility = View.GONE
        otpInput.visibility = View.GONE
        verifyCodeButton.visibility = View.GONE
    }

    private fun showPickerSection() {
        loginSection.visibility = View.GONE
        pickerSection.visibility = View.VISIBLE
        loadGrant()
    }

    private fun sendCode() {
        val email = emailInput.text.toString().trim()
        if (email.isEmpty()) { log("Enter an email address first."); return }
        Thread {
            try {
                api.requestOtp(email)
                runOnUiThread {
                    otpInput.visibility = View.VISIBLE
                    verifyCodeButton.visibility = View.VISIBLE
                    log("Code sent to $email.")
                }
            } catch (e: Exception) {
                runOnUiThread { log("Could not send code: ${e.message}") }
            }
        }.start()
    }

    private fun verifyCode() {
        val email = emailInput.text.toString().trim()
        val code = otpInput.text.toString().trim()
        if (code.isEmpty()) { log("Enter the code from the email."); return }
        Thread {
            try {
                api.verifyOtp(email, code)
                runOnUiThread { log("Logged in."); showPickerSection() }
            } catch (e: Exception) {
                runOnUiThread { log("Login failed: ${e.message}") }
            }
        }.start()
    }

    // ==================== Product/build picker ====================

    private fun loadGrant() {
        Thread {
            try {
                val grant = api.fetchGrant()
                runOnUiThread {
                    currentGrant = grant
                    grantLabelText.text = grant.label
                    productSpinner.adapter = ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item, grant.products)
                }
            } catch (e: Exception) {
                runOnUiThread { log("Could not load access: ${e.message}") }
            }
        }.start()
    }

    private fun checkUpdates() {
        val product = productSpinner.selectedItem as? String
        if (product == null) { log("No granted products to check."); return }
        buildListContainer.removeAllViews()
        log("Checking for $product updates…")
        Thread {
            try {
                val builds = api.fetchBuilds(product)
                runOnUiThread { renderBuildList(builds) }
            } catch (e: Exception) {
                runOnUiThread { log("Could not fetch builds: ${e.message}") }
            }
        }.start()
    }

    private fun renderBuildList(builds: List<ApiClient.Build>) {
        buildListContainer.removeAllViews()
        if (builds.isEmpty()) { log("No builds available for this product yet."); return }
        for (build in builds) {
            val button = Button(this)
            val sizeKb = build.sizeBytes / 1024
            button.text = "${build.product} ${build.version} (${build.variant}) — ${sizeKb} KB"
            button.setOnClickListener { startFlashForBuild(build) }
            buildListContainer.addView(button)
        }
    }

    // ==================== USB hub: find DUT + jig ====================

    private fun findHardware() {
        jigClient?.close()
        jigClient = null
        dutDriver = null
        hardwareStatusText.text = "Scanning USB hub…"

        val drivers = UsbSerialProber.getDefaultProber().findAllDrivers(usbManager)
        if (drivers.isEmpty()) {
            hardwareStatusText.text = "No USB devices found — check the hub, cables, and that both boards are powered."
            return
        }
        requestPermissionsThenProbe(drivers, 0)
    }

    private fun requestPermissionsThenProbe(drivers: List<UsbSerialDriver>, index: Int) {
        if (index >= drivers.size) {
            probeAllAndAssignRoles(drivers)
            return
        }
        val driver = drivers[index]
        if (usbManager.hasPermission(driver.device)) {
            requestPermissionsThenProbe(drivers, index + 1)
            return
        }
        pendingUsbAction = { requestPermissionsThenProbe(drivers, index + 1) }
        val flags = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) PendingIntent.FLAG_MUTABLE else 0
        val permissionIntent = PendingIntent.getBroadcast(this, 0, Intent(ACTION_USB_PERMISSION).setPackage(packageName), flags)
        usbManager.requestPermission(driver.device, permissionIntent)
    }

    /** Probes every permitted port with PING -- whichever replies PONG is the jig
     *  (see JigSerialClient); the first other one is treated as the DUT. Runs on
     *  a background thread since probing does blocking I/O. */
    private fun probeAllAndAssignRoles(drivers: List<UsbSerialDriver>) {
        Thread {
            var foundJig: JigSerialClient? = null
            var foundDut: UsbSerialDriver? = null
            for (driver in drivers) {
                if (!usbManager.hasPermission(driver.device)) continue
                val client = JigSerialClient.probe(usbManager, driver)
                if (client != null) {
                    foundJig = client
                } else if (foundDut == null) {
                    foundDut = driver
                }
            }
            jigClient = foundJig
            dutDriver = foundDut
            runOnUiThread {
                val jigStr = if (foundJig != null) "Jig: found ✅" else "Jig: not found ❌"
                val dutStr = if (foundDut != null) "DUT: found ✅" else "DUT: not found ❌"
                hardwareStatusText.text = "$jigStr   $dutStr"
                log("Hardware scan: $jigStr, $dutStr (${drivers.size} USB device(s) total)")
            }
        }.start()
    }

    // ==================== Flash (from a picked build) ====================

    private fun setBuildPickerEnabled(enabled: Boolean) {
        for (i in 0 until buildListContainer.childCount) buildListContainer.getChildAt(i).isEnabled = enabled
        findViewById<Button>(R.id.checkUpdatesButton).isEnabled = enabled
        findViewById<Button>(R.id.findHardwareButton).isEnabled = enabled
    }

    /** Call at every terminal point of a flash attempt (success or failure) --
     *  see flashInProgress's doc comment for why this exists. */
    private fun endFlashAttempt() {
        flashInProgress = false
        setBuildPickerEnabled(true)
    }

    private fun startFlashForBuild(build: ApiClient.Build) {
        if (flashInProgress) {
            log("A flash is already in progress — please wait for it to finish.")
            return
        }
        val driver = dutDriver
        if (driver == null) {
            log("No DUT found on the USB hub — tap 'Find DUT + Jig' first.")
            return
        }
        flashInProgress = true
        setBuildPickerEnabled(false)
        resetForNextUnit(closeJig = false)
        currentBuild = build
        retryFlashButton.visibility = View.GONE
        statusText.text = "Identifying device…"
        log("---- ${build.product} ${build.version} (${build.variant}) ----")

        Thread {
            val mac = readMacHex(driver)
            if (mac == null) {
                runOnUiThread {
                    log("Could not read the chip's MAC — check the OTG connection and try again.")
                    statusText.text = "Identification failed"
                    setStep("flash", false, "could not read chip MAC")
                    retryFlashButton.visibility = View.VISIBLE
                    saveReportSoFar()
                    endFlashAttempt()
                }
                return@Thread
            }
            runOnUiThread { log("Device MAC: ${mac.chunked(2).joinToString(":")}") }

            runOnUiThread { statusText.text = "Downloading firmware…"; progressText.text = "download 0%" }
            val appBytes = try {
                api.downloadBuild(build.id, mac, expectedSize = build.sizeBytes) { percent ->
                    runOnUiThread { progressBar.progress = percent; progressText.text = "download $percent%" }
                }
            } catch (e: Exception) {
                runOnUiThread {
                    log("Download failed: ${e.message}")
                    statusText.text = "Download failed"
                    setStep("flash", false, "download failed")
                    retryFlashButton.visibility = View.VISIBLE
                    saveReportSoFar()
                    endFlashAttempt()
                }
                return@Thread
            }
            runOnUiThread { log("Downloaded ${appBytes.size / 1024} KB.") }

            val bootloader = try { assets.open("bootloader.bin").readBytes() } catch (e: Exception) { null }
            val partitions = try { assets.open("partitions.bin").readBytes() } catch (e: Exception) { null }
            if (bootloader == null || partitions == null) {
                runOnUiThread {
                    log("Missing bundled bootloader.bin/partitions.bin — see assets/README.md.")
                    setStep("flash", false, "missing bundled assets")
                    retryFlashButton.visibility = View.VISIBLE
                    saveReportSoFar()
                    endFlashAttempt()
                }
                return@Thread
            }

            runOnUiThread { flashViaUsb(driver, bootloader, partitions, appBytes) }
        }.start()
    }

    private fun readMacHex(driver: UsbSerialDriver): String? {
        val connection = usbManager.openDevice(driver.device) ?: return null
        val transport = UsbSerialTransport(driver.ports.first())
        if (!transport.open(connection, BAUD_RATE)) return null
        val mac = NativeFlasher.readMac(transport)
        transport.close()
        return mac?.joinToString("") { "%02X".format(it) }
    }

    /** Passively listens on the DUT's serial output for durationMs -- same
     *  short-lived open/use/close pattern as readMacHex(), just reusing
     *  UsbSerialTransport.captureBootLog()'s read loop at an arbitrary
     *  point instead of right after a flash. Returns "" on any failure
     *  (a diagnostic add-on, not something that should fail the actual
     *  wifi_mqtt step if it doesn't work). */
    private fun captureLiveDutSerial(driver: UsbSerialDriver, durationMs: Long): String {
        val connection = usbManager.openDevice(driver.device) ?: return ""
        val transport = UsbSerialTransport(driver.ports.first())
        if (!transport.open(connection, BAUD_RATE)) return ""
        val log = transport.captureBootLog(durationMs)
        transport.close()
        return log
    }

    private fun flashViaUsb(driver: UsbSerialDriver, bootloader: ByteArray, partitions: ByteArray, app: ByteArray) {
        statusText.text = "Flashing…"
        progressBar.progress = 0
        log("---- flashing bootloader+partitions+app over USB-OTG ----")

        var lastLoggedStage: String? = null
        val listener = FlashProgressListener { stage, percent ->
            runOnUiThread {
                progressBar.progress = percent
                progressText.text = "$stage $percent%"
                if (stage != lastLoggedStage || percent == 100) { log("$stage: $percent%"); lastLoggedStage = stage }
            }
        }

        Thread {
            val connection: UsbDeviceConnection? = usbManager.openDevice(driver.device)
            if (connection == null) {
                runOnUiThread {
                    log("openDevice() returned null — permission or driver issue.")
                    setStep("flash", false, "openDevice failed")
                    retryFlashButton.visibility = View.VISIBLE
                    saveReportSoFar()
                    endFlashAttempt()
                }
                return@Thread
            }
            val transport = UsbSerialTransport(driver.ports.first())
            if (!transport.open(connection, BAUD_RATE)) {
                runOnUiThread {
                    log("Failed to open serial port at $BAUD_RATE baud.")
                    setStep("flash", false, "open failed")
                    retryFlashButton.visibility = View.VISIBLE
                    saveReportSoFar()
                    endFlashAttempt()
                }
                return@Thread
            }

            // Full chip erase before every flash, not just the three
            // segments below -- 2026-09-16 bench finding: reflashing a unit
            // for a new round left its old WiFi/MQTT credentials in place,
            // because NVS lives outside the bootloader/partitions/app
            // offsets flash() writes and a partial flash never touches it.
            // A blank-chip guarantee matters more here than the extra
            // couple of seconds this costs, since every unit's NVS
            // otherwise carries whatever the previous test round wrote.
            runOnUiThread { statusText.text = "Erasing chip…"; log("Erasing whole chip (clears NVS/WiFi/MQTT config too)…") }
            val eraseResult = NativeFlasher.eraseChip(transport)
            if (eraseResult != 0) {
                val eraseDescription = FlashResult.describe(eraseResult)
                transport.close()
                runOnUiThread {
                    log("Erase failed: $eraseDescription ($eraseResult)")
                    setStep("flash", false, "erase failed: $eraseDescription")
                    statusText.text = "Erase failed: $eraseDescription"
                    retryFlashButton.visibility = View.VISIBLE
                    saveReportSoFar()
                    endFlashAttempt()
                }
                return@Thread
            }
            runOnUiThread { log("Chip erased — writing fresh firmware…") }

            val result = NativeFlasher.flash(transport, bootloader, BOOTLOADER_OFFSET, partitions, PARTITIONS_OFFSET, app, APP_OFFSET, listener)
            val description = FlashResult.describe(result)
            runOnUiThread { log("Flash result: $description ($result)") }

            if (result != 0) {
                transport.close()
                runOnUiThread {
                    setStep("flash", false, description)
                    statusText.text = "Flash failed: $description"
                    retryFlashButton.visibility = View.VISIBLE
                    saveReportSoFar()
                    endFlashAttempt()
                }
                currentBuild?.let { api.reportResult(it.id, "flash_failed", detail = description) }
                return@Thread
            }

            runOnUiThread { setStep("flash", true); statusText.text = "Capturing boot log…" }
            val bootLog = transport.captureBootLog(BOOT_LOG_DURATION_MS)
            transport.close()

            val parsed = BootLogParser.parse(bootLog)
            currentDeviceId = parsed.deviceId
            currentFirmwareVersion = parsed.firmwareVersion
            parsed.deviceId?.let { BenchConfig.saveLastDeviceId(this, it) }

            runOnUiThread {
                log("---- boot log (${BOOT_LOG_DURATION_MS / 1000}s) ----\n${collapseRepeats(bootLog.trim())}\n---- end boot log ----")
                setStep("boot_log", parsed.passed, parsed.deviceId ?: "no device ID found in boot log")

                if (parsed.deviceId == null) {
                    statusText.text = "Boot log had no device ID"
                    saveReportSoFar()
                    endFlashAttempt()
                    return@runOnUiThread
                }
                endFlashAttempt()
                showJoinWifiPrompt(parsed.deviceId)
            }
        }.start()
    }

    /** Jumps straight to the "join WiFi" step against an already-flashed,
     *  already-running DUT -- skips MAC read / download / USB-OTG flash /
     *  boot-log capture entirely. Added 2026-09-16 for exactly the
     *  situation that prompted it: repeatedly re-flashing the same unit
     *  just to get back to the relay/flow/wifi steps while debugging
     *  wiring, which wastes real time on an unchanged flash step. */
    private fun promptRunTestsOnly() {
        val input = EditText(this).apply {
            setText(currentDeviceId ?: BenchConfig.loadLastDeviceId(this@MainActivity) ?: "")
            hint = "e.g. SWC_001_B468"
        }
        AlertDialog.Builder(this)
            .setTitle("Run tests only")
            .setMessage(
                "Which SoftAP is the DUT already on? Use the FULL name exactly as shown after " +
                    "\"[BOOT] Device ID:\" in a boot log (e.g. \"SWC_001_B468\", not just \"B468\") -- " +
                    "it's only used as a label on the report, but keeping it exact avoids confusion later. " +
                    "(Skips flashing entirely -- use this while iterating on wiring/config against a unit " +
                    "that's already flashed and booted.)"
            )
            .setView(input)
            .setPositiveButton(R.string.continue_button) { _, _ ->
                val id = input.text.toString().trim()
                if (id.isEmpty()) {
                    log("Enter the DUT's SoftAP name first.")
                    return@setPositiveButton
                }
                resetForNextUnit(closeJig = false)
                currentDeviceId = id
                showJoinWifiPrompt(id)
            }
            .setNegativeButton(R.string.cancel_button, null)
            .show()
    }

    /** Recovery tool for exactly the scenario hit 2026-09-16: the DUT
     *  already joined the office WiFi and the app got closed (or the
     *  operator moved on) before the ship-clean reset ran, leaving the
     *  DUT unreachable over its own SoftAP (dropped ~60s after going
     *  stable on STA, per main.cpp) with no USB session active either.
     *  force_local_mode is a normal command routed through the same
     *  handleCommand() as everything else (main.cpp) -- it drops STA and
     *  re-enters SoftAP immediately, no reboot needed, and persists so it
     *  survives a power cycle too. Needs no USB/jig connection at all,
     *  only the device ID and the phone's own cellular data -- this is
     *  deliberately reachable from the main screen, not buried in
     *  Diagnostics, since the whole point is recovering a DUT you can't
     *  currently reach any other way. */
    private fun promptForceLocalMode() {
        val input = EditText(this).apply {
            setText(currentDeviceId ?: BenchConfig.loadLastDeviceId(this@MainActivity) ?: "")
            hint = "e.g. SWC_001_B468"
        }
        AlertDialog.Builder(this)
            .setTitle("Force DUT to local mode")
            .setMessage(
                "For a DUT that's stuck on the office WiFi with no USB session active (e.g. the app " +
                    "was closed mid-test). Sends force_local_mode over MQTT -- the DUT drops its WiFi " +
                    "and re-enters its own SoftAP immediately. Needs only your phone's cellular data, " +
                    "no USB/jig connection."
            )
            .setView(input)
            .setPositiveButton("Send") { _, _ ->
                val id = input.text.toString().trim()
                if (id.isEmpty()) {
                    log("Enter the DUT's device ID first.")
                    return@setPositiveButton
                }
                log("Sending force_local_mode to $id over MQTT (retrying up to 4x)…")
                Thread {
                    val commander = MqttCommander(this@MainActivity, id)
                    if (!commander.connect()) {
                        log("force_local_mode: could not connect to the broker.")
                        return@Thread
                    }
                    // Retried, not one-shot -- a single publish has a real
                    // chance of landing during one of the DUT's own
                    // WiFi/MQTT reconnect flaps and getting silently
                    // dropped (PubSubClient has no persistent session/
                    // QoS>0). See MqttCommander.publishCommandWithRetry's
                    // doc comment.
                    val status = commander.publishCommandWithRetry("force_local_mode", attempts = 4, intervalMs = 3000) {
                        it.optBoolean("wifi_connected") != true
                    }
                    commander.close()
                    val stillOnWifi = status?.optBoolean("wifi_connected") == true
                    log(
                        if (status == null) "force_local_mode: sent (4x), but no status confirmation seen (may need network/ACL review) -- check the DUT's own SoftAP list in a minute."
                        else if (!stillOnWifi) "force_local_mode: confirmed -- DUT reports wifi_connected=false, should be broadcasting its SoftAP now."
                        else "force_local_mode: sent 4x over ~12s, but the DUT's last status still shows wifi_connected=true -- likely needs a reflash to recover."
                    )
                }.start()
            }
            .setNegativeButton(R.string.cancel_button, null)
            .show()
    }

    private fun showJoinWifiPrompt(deviceId: String) {
        deviceIdText.text = deviceId
        joinWifiSection.visibility = View.VISIBLE
        statusText.text = "Join the device's own WiFi network, then tap Continue"
    }

    /** Called from the background test thread (via runOnUiThread) -- blocks
     *  that thread (not the UI thread) on [latch] until the operator
     *  confirms they've rejoined the DUT's own WiFi. This is now the ONLY
     *  WiFi-switch prompt left in the whole run (see
     *  docs/testing/PRODUCTION_TOOL_SPEC_V2.md §4.1/§6.2) -- the earlier
     *  hotspot-on / phone-WiFi-off dialogs this replaced were specific to
     *  the now-dropped phone-hotspot test AP (a hotspot-ON +
     *  WiFi-client-joined-to-a-dead-AP combo made Android route the
     *  hotspot's own internet through the dead link). With the office WiFi
     *  router as the only test AP, the DUT's own STA connection needs
     *  nothing from the phone's WiFi state, and post-handover checks run
     *  over MQTT (MqttCommander) instead of the local WS API -- so this
     *  prompt is only needed once, at the very end, to confirm the
     *  final factory_reset actually landed (SoftAP reappearing with no
     *  saved credentials IS the proof). */
    private fun promptRejoinForFactoryResetConfirm(latch: java.util.concurrent.CountDownLatch, skipped: java.util.concurrent.atomic.AtomicBoolean) {
        statusText.text = "Waiting for phone to rejoin the DUT's WiFi…"
        AlertDialog.Builder(this)
            .setTitle("Rejoin the DUT's WiFi")
            .setMessage(
                "Ship-clean reset was sent. The DUT should be rebooting into its own blank SoftAP " +
                    "again (SSID starting with \"SWC_\") -- rejoin it now so we can confirm the reset " +
                    "actually took. Tap Continue once you're reconnected, or Skip if no such SoftAP " +
                    "ever shows up (the reset commands may not have landed -- the unit likely needs a " +
                    "reflash rather than waiting here indefinitely)."
            )
            .setCancelable(false)
            .setNeutralButton("Open WiFi Settings") { _, _ ->
                startActivity(Intent(AndroidSettings.ACTION_WIFI_SETTINGS))
                promptRejoinForFactoryResetConfirm(latch, skipped)
            }
            .setNegativeButton("Skip") { _, _ -> skipped.set(true); latch.countDown() }
            .setPositiveButton(R.string.continue_button) { _, _ -> latch.countDown() }
            .show()
    }

    // ==================== Functional tests ====================

    private fun runTests() {
        joinWifiSection.visibility = View.GONE
        statusText.text = "Running tests…"

        Thread {
            val client = DutWsClient()
            dut = client
            if (!client.connect()) {
                runOnUiThread {
                    setStep("factory_reset", false, "could not connect to DUT WS API")
                    statusText.text = "Could not connect to the DUT — check you joined its WiFi"
                    showReport()
                }
                return@Thread
            }

            jigAvailable = jigClient?.ping() == true
            runOnUiThread { log(if (jigAvailable) "Jig connected." else "Jig not connected — relay/flow steps will be unverified.") }

            runCatching { runFunctionalTests(client) }.onFailure { e ->
                val firstUnset = steps.values.firstOrNull { it.passed == null } ?: steps.values.last()
                runOnUiThread { setStep(firstUnset.name, false, "unexpected error: ${e.message}") }
            }

            runOnUiThread { showReport() }
        }.start()
    }

    private fun runFunctionalTests(client: DutWsClient) {
        val jig = jigClient

        // SoftAP phase routes through the jig's own network bridge when
        // possible (JIG_NETWORK_BRIDGE_SPEC.md Phase 1) instead of the
        // phone's direct WS link -- the jig joins the DUT's SoftAP itself,
        // so the phone's own WiFi state stops mattering for any of this.
        // Falls back to the phone's WS client if there's no jig connected,
        // or the jig couldn't join (e.g. wrong/changed SOFTAP_PASSWORD) --
        // keeps the flow working for a bench run without the new jig
        // firmware, or while this phase is still being validated.
        val netBridge = jig?.let { JigNetworkBridge(it) }
        val deviceId = currentDeviceId
        val useBridge = netBridge != null && deviceId != null && netBridge.joinDutSoftAp(deviceId)
        runOnUiThread {
            log(
                if (useBridge) "Jig joined the DUT's SoftAP — SoftAP-phase commands routed through it, not the phone's WiFi."
                else "Jig network bridge unavailable — falling back to the phone's direct WS link for SoftAP-phase commands."
            )
        }

        fun softApCommand(cmd: String, extra: JSONObject = JSONObject(), timeoutMs: Int = 8000): JSONObject? =
            if (useBridge) netBridge!!.sendCommand(cmd, extra, timeoutMs) else client.sendCommand(cmd, extra, timeoutMs.toLong())

        fun softApDeviceInfo(): JSONObject? = if (useBridge) netBridge!!.deviceInfo() else client.deviceInfo()

        // 3. Factory reset -- the DUT reboots right after this, dropping
        // whatever's connected to its SoftAP. Over WS that means an
        // explicit close()+connect(), which OkHttp retries adequately on
        // its own. Over the bridge, the jig's own STA link drops and has
        // to notice the SoftAP disappeared and reappeared, then
        // re-associate + DHCP -- that's the ESP32 WiFi driver's own
        // background reconnect, not something directly controllable here.
        // 2026-09-18 bench finding: a single check right after a flat 3s
        // sleep failed this step even on a healthy jig -- not enough
        // margin for the DUT's reboot + the jig's reconnect cycle both to
        // finish. Poll instead of checking once.
        softApCommand("factory_reset", timeoutMs = 3000)
        Thread.sleep(3000)
        val reconnected = if (useBridge) {
            var ok = false
            for (attempt in 1..8) {
                if (softApDeviceInfo() != null) { ok = true; break }
                Thread.sleep(1500)
            }
            ok
        } else {
            client.close(); client.connect()
        }
        runOnUiThread { setStep("factory_reset", reconnected) }
        if (!reconnected) return

        // 4. Calibrate
        val calResp = softApCommand("calibrate", JSONObject().put("ppl", config.expectedCalibrationPpl))
        runOnUiThread { setStep("calibrate", calResp != null) }

        // 5. Relay test
        softApCommand("relay_test", timeoutMs = 3000)
        Thread.sleep(800)
        if (jigAvailable && jig != null) {
            val relayOn = jig.relayState()
            runOnUiThread {
                setStep("relay_test", relayOn == true, if (relayOn == true) "jig sensed relay ON" else "jig did NOT sense relay closing")
            }
        } else {
            runOnUiThread { setStep("relay_test", true, "jig not connected — unverified") }
        }

        // 6. Flow sensor
        if (jigAvailable && jig != null) {
            val before = softApDeviceInfo()?.optDouble("liters_delivered", 0.0) ?: 0.0
            jig.pulse(config.flowTestPulseCount)
            Thread.sleep(500)
            val after = softApDeviceInfo()?.optDouble("liters_delivered", 0.0) ?: 0.0
            val delivered = after - before
            val expected = config.flowTestPulseCount.toDouble() / config.expectedCalibrationPpl
            val within = Math.abs(delivered - expected) <= expected * 0.02
            runOnUiThread { setStep("flow_sensor", within, "expected %.2fL, got %.2fL".format(expected, delivered)) }
        } else {
            runOnUiThread { setStep("flow_sensor", true, "jig not connected — unverified") }
        }

        // 7. RTC sync + check -- done here, over the SoftAP-phase transport
        // (PRODUCTION_TOOL_SPEC_V2.md §5 step 9) -- deliberately BEFORE
        // wifi_config, so it doesn't depend on either transport surviving
        // the handover below.
        softApCommand("rtc_sync", JSONObject().put("unix", System.currentTimeMillis() / 1000), timeoutMs = 3000)
        val rtcInfo = softApDeviceInfo()
        runOnUiThread { setStep("rtc", rtcInfo?.optBoolean("rtc_set") == true, "rtc_time=${rtcInfo?.optString("rtc_time")}") }

        // 8. WiFi + MQTT handover -- office WiFi only (no hotspot, see
        // PRODUCTION_TOOL_SPEC_V2.md §4.1), so unlike the dropped hotspot
        // design, nothing here needs an operator WiFi prompt: the DUT's STA
        // connection to a real router doesn't depend on the phone's WiFi
        // state at all. The phone's link to the DUT's SoftAP will just idle
        // and drop on its own once the DUT tears its SoftAP down ~60s after
        // going stable on STA (main.cpp) -- nothing to wait on here either.
        if (config.testApSsid.isBlank()) {
            runOnUiThread { setStep("wifi_mqtt", false, "no office WiFi SSID set — fill it in under Settings first") }
            for (name in listOf("relay_test_mqtt", "flow_sensor_mqtt", "rtc_mqtt")) {
                runOnUiThread { setStep(name, false, "skipped — WiFi/MQTT never connected") }
            }
            val resetAck = softApCommand("factory_reset", timeoutMs = 5000)
            runOnUiThread { setStep("ship_clean_reset", resetAck != null, if (resetAck != null) "sent via SoftAP-phase transport" else "no ack") }
            runOnUiThread { setStep("factory_reset_confirmed", false, "skipped — see wifi_mqtt") }
            if (useBridge) netBridge!!.leaveWifi()
            return
        }
        softApCommand("wifi_config", JSONObject().put("ssid", config.testApSsid).put("pass", config.testApPassword))
        softApCommand("resume_auto_mode", timeoutMs = 3000)
        // The jig's part in the SoftAP phase is done now -- the DUT is
        // about to tear its own SoftAP down anyway, but leave explicitly
        // rather than letting the jig's STA link just die on its own
        // (matches the spec's "explicit LEAVE_WIFI/JOIN_AP, not implicit
        // state" design choice). Office-WiFi/MQTT steps below are still
        // phone-side (MqttCommander) until that's a later phase.
        if (useBridge) netBridge!!.leaveWifi()

        var liveSerialLog = ""
        val serialThread = dutDriver?.let { driver ->
            Thread { liveSerialLog = captureLiveDutSerial(driver, 32_000L) }.apply { start() }
        }
        serialThread?.join() // no timeout -- bounded to captureLiveDutSerial's own 32s

        // "[WiFi] Connected" (onWiFiConnected(), main.cpp) only prints once
        // WIFI_STABLE_HOLD_MS (60s) of uninterrupted STA connection has
        // passed and the DUT actually leaves local fallback -- structurally
        // impossible to see within this 32s capture when reconnecting FROM
        // fallback (as opposed to a fresh boot-time connect). The real,
        // immediate evidence of a successful association on that path is
        // "[WiFi] Reconnected — confirming stability before leaving
        // fallback" (main.cpp's pollBackgroundRetry()), printed the moment
        // STA re-associates, well before the 60s hold completes. Bench
        // finding 2026-09-16: a run with WiFi + MQTT genuinely both working
        // (confirmed subscribed to the command topic and everything) still
        // failed this step because only the slower line was checked for.
        val wifiOkSerial = liveSerialLog.contains("[WiFi] Connected") || liveSerialLog.contains("[WiFi] Reconnected")
        val mqttOkSerial = liveSerialLog.contains("[MQTT] Connected")
        val passed = wifiOkSerial && mqttOkSerial
        val mqttFailLine = Regex("\\[MQTT] Failed rc=-?\\d+.*").find(liveSerialLog)?.value

        var detail = when {
            passed -> "confirmed via DUT serial (WiFi + MQTT connected lines seen)"
            mqttFailLine != null -> mqttFailLine
            !wifiOkSerial -> "no \"[WiFi] Connected\"/\"[WiFi] Reconnected\" line seen in 32s of DUT serial — check the office router is reachable"
            else -> "WiFi connected but no \"[MQTT] Connected\" line seen — broker unreachable or rejected credentials"
        }

        // 9-12. Once WiFi+MQTT is up, everything else runs over MQTT, not
        // WS -- see PRODUCTION_TOOL_SPEC_V2.md §6.1/§6.2 for why the local
        // WS API can't be reached here at all (DutWsClient's fixed
        // 192.168.4.1 stops answering once the DUT's SoftAP drops, and
        // there's no way for the phone to learn its new DHCP IP on the
        // office network). The firmware's MQTT command topic already
        // routes into the same handleCommand() as WS, so this needs no
        // firmware change -- see MqttCommander's doc comment.
        var commander: MqttCommander? = null
        if (passed) {
            val deviceIdForBroker = currentDeviceId
            if (deviceIdForBroker != null) {
                val c = MqttCommander(this@MainActivity, deviceIdForBroker)
                if (c.connect()) {
                    commander = c
                    val status = c.latestStatus(timeoutMs = 7000)
                    detail += if (status != null) {
                        "; broker confirms status message received"
                    } else {
                        "; broker check: no status message seen (may need network/ACL review)"
                    }
                } else {
                    detail += "; broker check: could not connect to broker from phone"
                }
            }
        }

        runOnUiThread {
            setStep("wifi_mqtt", passed, detail)
            if (liveSerialLog.isNotBlank()) {
                log("---- DUT serial during WiFi/MQTT wait ----\n${truncateLines(collapseRepeats(liveSerialLog.trim()))}\n---- end ----")
            }
        }

        if (commander != null) {
            // 9. Relay test via MQTT -- same jig physical sense as the
            // SoftAP-phase check (§5 step 7); the jig stays reachable over
            // USB the whole time regardless of the phone's WiFi state, so
            // this doesn't need a status-field read at all, just a
            // command to trigger it.
            commander.publishCommand("relay_test")
            Thread.sleep(800)
            if (jigAvailable && jig != null) {
                val relayOn = jig.relayState()
                runOnUiThread {
                    setStep("relay_test_mqtt", relayOn == true, if (relayOn == true) "jig sensed relay ON (via MQTT)" else "jig did NOT sense relay closing")
                }
            } else {
                runOnUiThread { setStep("relay_test_mqtt", true, "jig not connected — unverified") }
            }

            // 10. Flow sensor via MQTT -- jig still injects pulses over
            // USB directly; only the before/after liters_delivered read
            // needs MQTT, since that's DUT-side accumulated state.
            if (jigAvailable && jig != null) {
                val before = commander.latestStatus(timeoutMs = 7000)?.optDouble("liters_delivered", 0.0) ?: 0.0
                jig.pulse(config.flowTestPulseCount)
                val after = commander.latestStatus(timeoutMs = 7000)?.optDouble("liters_delivered", 0.0) ?: 0.0
                val delivered = after - before
                val expected = config.flowTestPulseCount.toDouble() / config.expectedCalibrationPpl
                val within = Math.abs(delivered - expected) <= expected * 0.02
                runOnUiThread { setStep("flow_sensor_mqtt", within, "expected %.2fL, got %.2fL (via MQTT)".format(expected, delivered)) }
            } else {
                runOnUiThread { setStep("flow_sensor_mqtt", true, "jig not connected — unverified") }
            }

            // 11. RTC check via MQTT -- re-verify the SoftAP-phase sync
            // (step 7 above) survived the handover; no need to re-sync.
            val rtcStatus = commander.latestStatus(timeoutMs = 7000)
            runOnUiThread { setStep("rtc_mqtt", rtcStatus?.optBoolean("rtc_set") == true, "rtc_time=${rtcStatus?.optString("rtc_time")}") }

            // 12. Ship-clean reset via MQTT. A single publish has a real
            // chance of being silently dropped (see MqttCommander's doc
            // comment) -- retry factory_reset a few times up front (it
            // reboots the DUT ~500ms after landing, so extra sends after
            // that are harmless no-ops), then ALSO retry force_local_mode
            // as a backup: even if factory_reset's NVS wipe never lands,
            // this guarantees the unit at minimum leaves office WiFi rather
            // than shipping still joined to the bench network. 2026-09-18
            // bench finding: a single-shot attempt at each left a unit
            // stuck on office WiFi, needing a full reflash to recover --
            // this replaces that with a "did it actually leave" check
            // using real status evidence instead of hoping one send landed.
            repeat(3) { commander.publishCommand("factory_reset"); Thread.sleep(1500) }
            val localModeStatus = commander.publishCommandWithRetry("force_local_mode", attempts = 3, intervalMs = 2500) {
                it.optBoolean("wifi_connected") != true
            }
            val offWifi = localModeStatus?.optBoolean("wifi_connected") != true
            runOnUiThread {
                setStep(
                    "ship_clean_reset", offWifi,
                    if (offWifi) "factory_reset + force_local_mode sent (retried); DUT off office WiFi"
                    else "factory_reset + force_local_mode sent (retried); DUT still shows wifi_connected=true — may need manual recovery"
                )
            }
            commander.close()
        } else {
            // Can't reach the DUT over MQTT, for one of two different
            // reasons -- distinguish them, since "the DUT's WiFi/MQTT never
            // came up" and "the DUT is fine but this phone couldn't reach
            // the broker" point at completely different things to check
            // (2026-09-16 bench finding: a run where wifi_mqtt genuinely
            // passed on the DUT side still hit this branch because the
            // phone's own cellular-bound broker connection failed --
            // "WiFi/MQTT never connected" was actively misleading there).
            val skipReason = if (passed) "skipped — DUT's WiFi/MQTT is fine, but this phone couldn't reach the broker (check its cellular data/SIM)"
                              else "skipped — WiFi/MQTT never connected"
            for (name in listOf("relay_test_mqtt", "flow_sensor_mqtt", "rtc_mqtt")) {
                runOnUiThread { setStep(name, false, skipReason) }
            }
            val resetAck = client.sendCommand("factory_reset", timeoutMs = 5000)
            runOnUiThread {
                setStep("ship_clean_reset", resetAck != null, if (resetAck != null) "sent via WS fallback" else "no ack via WS either — unit may still carry bench WiFi/MQTT config")
            }
        }

        // 13. Rejoin the DUT's WiFi one last time to confirm the reset
        // actually took -- the DUT reboots and starts printing its boot
        // sequence over USB serial immediately regardless of the phone's
        // WiFi state, so start capturing that in parallel with the prompt
        // rather than waiting for the operator to finish tapping through
        // Settings first.
        var resetSerialLog = ""
        val resetSerialThread = dutDriver?.let { driver ->
            Thread { resetSerialLog = captureLiveDutSerial(driver, 15_000L) }.apply { start() }
        }
        val rejoinLatch = java.util.concurrent.CountDownLatch(1)
        val rejoinSkipped = java.util.concurrent.atomic.AtomicBoolean(false)
        runOnUiThread { promptRejoinForFactoryResetConfirm(rejoinLatch, rejoinSkipped) }
        rejoinLatch.await()
        resetSerialThread?.join()

        // "wifi_ssid NOT_FOUND"/"starting local fallback (SoftAP)" only ever
        // print on a fresh BOOT after nvs.factoryReset() actually wiped NVS
        // -- real proof, unlike a bare "wifi_connected: false" over WS, which
        // is equally true after a live force_local_mode with WiFi creds
        // still sitting in NVS (now sent as a backup in the step above, see
        // its comment). Only the serial-based reboot evidence marks this
        // step confirmed; the WS-only signal is surfaced but treated as
        // unconfirmed, since it can't tell a genuine reset apart from
        // force_local_mode alone -- 2026-09-18 bench finding: this
        // distinction is exactly what "I want complete flash to zero"
        // was about, so a weak signal shouldn't be allowed to mark PASS.
        val nvsConfirmsBlank = resetSerialLog.contains("wifi_ssid NOT_FOUND")
        val serialConfirmsReset = nvsConfirmsBlank || resetSerialLog.contains("starting local fallback (SoftAP)")
        // Skip the WS attempt entirely when the operator's already told us
        // the DUT never showed up -- there's no SoftAP for client.deviceInfo()
        // to reach, so trying would just be an 8s wait for a foregone timeout.
        val confirmInfo = if (rejoinSkipped.get()) null else client.deviceInfo(timeoutMs = 8000)
        val wsOnlyConfirmsWifiOff = confirmInfo != null && confirmInfo.optBoolean("wifi_connected") != true
        val resetConfirmed = serialConfirmsReset
        val confirmDetail = when {
            rejoinSkipped.get() -> "skipped by operator -- DUT never reachable on its own SoftAP; verify it isn't still joined to office WiFi before shipping"
            nvsConfirmsBlank -> "confirmed genuinely blank -- boot log shows wifi_ssid NOT_FOUND"
            serialConfirmsReset -> "confirmed via DUT serial (\"starting local fallback (SoftAP)\")"
            wsOnlyConfirmsWifiOff -> "WiFi is off (via WS) but no reboot/blank-NVS evidence seen -- may only be force_local_mode, not a real factory reset; treat as unconfirmed"
            else -> "could not confirm -- unit may not have actually reset"
        }
        runOnUiThread {
            setStep("factory_reset_confirmed", resetConfirmed, confirmDetail)
            if (resetSerialLog.isNotBlank()) {
                log("---- DUT serial during reset confirm ----\n${truncateLines(collapseRepeats(resetSerialLog.trim()))}\n---- end ----")
            }
        }
    }

    // ==================== Report / next unit ====================

    private fun buildLabel(): String = currentBuild?.let { "${it.product} ${it.version} (${it.variant})" } ?: "unknown build"

    private fun buildReport(): TestReport = TestReport(
        deviceId = currentDeviceId,
        firmwareVersion = currentFirmwareVersion,
        buildLabel = buildLabel(),
        timestampUtc = Date(),
        operator = config.operatorName,
        station = config.stationName,
        steps = LinkedHashMap(steps),
    )

    private fun saveReportSoFar() {
        val report = buildReport()
        ReportStore.add(this, report)
        currentBuild?.let { build ->
            api.reportResult(build.id, if (report.overallPassed) "flash_ok" else "flash_failed", deviceId = currentDeviceId)
        }
    }

    private fun showReport() {
        val report = buildReport()
        saveReportSoFar()
        reportSection.visibility = View.VISIBLE
        reportResultText.text = if (report.overallPassed) "PASS" else "FAIL"
        reportResultText.setTextColor(if (report.overallPassed) 0xFF1A7F37.toInt() else 0xFFC62828.toInt())
        statusText.text = "${report.deviceId ?: "UNKNOWN"} — ${if (report.overallPassed) "PASS" else "FAIL"}"
    }

    /** closeJig=false when starting a new flash for the SAME session (jig stays wired
     *  the whole time); true only when the operator explicitly wants to rescan hardware. */
    private fun resetForNextUnit(closeJig: Boolean = true) {
        for (name in PRODUCTION_TEST_STEPS) steps[name] = StepResult(name)
        currentDeviceId = null
        currentFirmwareVersion = null
        currentBuild = null
        jigAvailable = false
        dut?.close(); dut = null
        if (closeJig) { jigClient?.close(); jigClient = null; dutDriver = null; hardwareStatusText.text = "" }
        joinWifiSection.visibility = View.GONE
        reportSection.visibility = View.GONE
        retryFlashButton.visibility = View.GONE
        progressBar.progress = 0
        progressText.text = getString(R.string.progress_idle)
        statusText.text = getString(R.string.status_idle)
        renderSteps()
    }

    // ==================== UI helpers ====================

    private fun setStep(name: String, passed: Boolean, detail: String = "") {
        steps[name] = StepResult(name, passed, detail)
        renderSteps()
        // Every step result also goes to the log -- otherwise a plain-text
        // copy/paste of the log (e.g. for remote debugging) shows the flash
        // and boot-log stages but silently omits every functional test
        // result, which only ever updated the on-screen checklist icons.
        val label = STEP_LABELS[name] ?: name
        val marker = if (passed) "✅" else "❌"
        log(if (detail.isNotEmpty()) "$marker $label — $detail" else "$marker $label")
    }

    private fun renderSteps() {
        stepsContainer.removeAllViews()
        for (name in PRODUCTION_TEST_STEPS) {
            val step = steps[name] ?: continue
            val row = TextView(this)
            val label = STEP_LABELS[name] ?: name
            val (marker, color) = when (step.passed) {
                null -> "⏳" to 0xFF888888.toInt()
                true -> "✅" to 0xFF1A7F37.toInt()
                false -> "❌" to 0xFFC62828.toInt()
            }
            row.text = if (step.detail.isNotEmpty()) "$marker $label — ${step.detail}" else "$marker $label"
            row.setTextColor(color)
            row.setPadding(0, 8, 0, 8)
            stepsContainer.addView(row)
        }
    }

    private fun log(message: String) {
        android.util.Log.i("flashertester", message)
        runOnUiThread { logText.append("\n$message") }
    }

    /** Collapses runs of 3+ consecutive identical lines into one line plus
     *  a "(repeated Nx)" note. A persistent USB-serial capture artifact
     *  (investigated at length 2026-09-16/18 -- ruled out a firmware
     *  retry-storm with hard evidence, fixed several real related bugs
     *  along the way, but the exact remaining trigger wasn't pinned down)
     *  occasionally duplicates a short fragment many times in a row,
     *  sometimes even in the very first capture of a run. This doesn't
     *  touch what the pass/fail logic reads (BootLogParser and the wifi_
     *  mqtt step's substring checks run on the raw, uncollapsed text, and
     *  have always found the real content regardless of the duplicates
     *  around it) -- it only keeps what a human looks at readable. */
    private fun collapseRepeats(text: String): String {
        val lines = text.lines()
        val out = StringBuilder()
        var i = 0
        while (i < lines.size) {
            var j = i + 1
            while (j < lines.size && lines[j] == lines[i]) j++
            val count = j - i
            out.append(lines[i])
            if (count >= 3) out.append("  (repeated ${count}x)")
            out.append("\n")
            i = j
        }
        return out.toString().trimEnd('\n')
    }

    /** Keeps only the first/last [headTail] lines of a big serial capture for
     *  the on-screen log and shared text -- a flaky WiFi reconnect can print
     *  the same line dozens of times in a row (seen 2026-09-16/18), and the
     *  meaningful evidence is always at the start (what was attempted) or
     *  the end (the final connected/failed state), never buried in a wall
     *  of identical repeats in the middle. The full, untruncated capture
     *  still goes to Logcat via log()'s own Log.i() call regardless. */
    private fun truncateLines(text: String, headTail: Int = 50): String {
        val lines = text.lines()
        if (lines.size <= headTail * 2) return text
        val omitted = lines.size - headTail * 2
        return (lines.take(headTail) + listOf("… $omitted lines omitted …") + lines.takeLast(headTail)).joinToString("\n")
    }

    private fun shareLog() {
        val intent = Intent(Intent.ACTION_SEND).apply {
            type = "text/plain"
            putExtra(Intent.EXTRA_SUBJECT, "NB Agri Flasher/Tester log")
            putExtra(Intent.EXTRA_TEXT, logText.text.toString())
        }
        startActivity(Intent.createChooser(intent, getString(R.string.share_log_button)))
    }

    // ==================== Jig/DUT diagnostics ====================

    /** Isolated hardware check, outside the full 9-step sequence -- lets
     *  the operator confirm the jig's pulse output actually reaches the
     *  DUT's flow-sensor pin without re-running factory_reset/calibrate/
     *  wifi_mqtt/etc every time while debugging wiring. Added 2026-09-15
     *  after a flow_sensor failure that turned out to need this kind of
     *  step-by-step isolation to track down.
     */
    private fun showDiagnosticsDialog() {
        val jig = jigClient
        if (jig == null) {
            log("No jig connected — tap 'Find DUT + Jig' first.")
            return
        }

        val layout = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(48, 24, 48, 0) }
        val resultText = TextView(this).apply { text = "Ready." }
        val pulseInput = EditText(this).apply {
            setText("450")
            inputType = android.text.InputType.TYPE_CLASS_NUMBER
        }
        layout.addView(resultText)

        // Shared, on-demand -- reused by every button below rather than each
        // opening its own short-lived connection, so relay ON, pulse, and
        // liters-read all talk to the same live DUT session.
        fun connectedDut(onReady: (DutWsClient) -> Unit, onFail: () -> Unit) {
            Thread {
                var client = dut
                if (client == null || !client.connected) {
                    client = DutWsClient()
                    if (!client.connect()) {
                        runOnUiThread { onFail() }
                        return@Thread
                    }
                    dut = client
                }
                onReady(client)
            }.start()
        }

        fun addButton(label: String, onClick: () -> Unit) {
            layout.addView(Button(this).apply {
                text = label
                setOnClickListener { onClick() }
            })
        }

        addButton("Ping jig") {
            resultText.text = "Pinging…"
            Thread {
                val ok = jig.ping()
                runOnUiThread { resultText.text = if (ok) "Jig: PONG ✅" else "Jig: no response ❌" }
            }.start()
        }

        addButton("Read relay state (jig sense)") {
            resultText.text = "Reading…"
            Thread {
                val state = jig.relayState()
                runOnUiThread {
                    resultText.text = "Relay: " + (state?.let { if (it) "ON" else "OFF" } ?: "no response")
                }
            }.start()
        }

        layout.addView(TextView(this).apply { text = "Relay control (DUT)"; setPadding(0, 24, 0, 0) })
        val relayRow = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        relayRow.addView(Button(this).apply {
            text = "Relay ON"
            setOnClickListener {
                resultText.text = "Turning relay ON…"
                connectedDut(
                    onReady = { client ->
                        val ok = client.sendCommand("manual_on") != null
                        runOnUiThread { resultText.text = if (ok) "Relay ON sent ✅" else "No ack from DUT" }
                    },
                    onFail = { resultText.text = "Could not connect to the DUT WS API." },
                )
            }
        })
        relayRow.addView(Button(this).apply {
            text = "Relay OFF"
            setOnClickListener {
                resultText.text = "Turning relay OFF…"
                connectedDut(
                    onReady = { client ->
                        val ok = client.sendCommand("manual_off") != null
                        runOnUiThread { resultText.text = if (ok) "Relay OFF sent ✅" else "No ack from DUT" }
                    },
                    onFail = { resultText.text = "Could not connect to the DUT WS API." },
                )
            }
        })
        layout.addView(relayRow)

        layout.addView(TextView(this).apply { text = "Pulse count (per burst)"; setPadding(0, 24, 0, 0) })
        layout.addView(pulseInput)

        addButton("Send pulses + check DUT") {
            val n = pulseInput.text.toString().toIntOrNull()
            if (n == null || n <= 0) {
                resultText.text = "Enter a valid pulse count first."
                return@addButton
            }
            resultText.text = "Sending $n pulses…"
            connectedDut(
                onReady = { client ->
                    val before = client.deviceInfo()?.optDouble("liters_delivered", -1.0) ?: -1.0
                    val pulseAck = jig.pulse(n)
                    Thread.sleep(500)
                    val after = client.deviceInfo()?.optDouble("liters_delivered", -1.0) ?: -1.0
                    runOnUiThread {
                        resultText.text = "Jig confirmed emitting: $pulseAck\n" +
                            "DUT liters_delivered before: $before, after: $after\n" +
                            if (before >= 0 && after >= 0) "delta: ${"%.3f".format(after - before)}L" else "(DUT unreachable)"
                    }
                },
                onFail = { resultText.text = "Could not connect to the DUT WS API." },
            )
        }

        // Continuous pulsing -- runs in small bursts in a loop rather than
        // one giant PULSE:<n> so the Stop button actually takes effect
        // promptly instead of waiting out a single huge burst.
        var pulsing = false
        val continuousButton = Button(this).apply { text = "Start continuous pulses" }
        continuousButton.setOnClickListener {
            if (!pulsing) {
                pulsing = true
                continuousButton.text = "Stop continuous pulses"
                resultText.text = "Pulsing continuously…"
                Thread {
                    while (pulsing) {
                        jig.pulse(50)
                    }
                }.start()
            } else {
                pulsing = false
                continuousButton.text = "Start continuous pulses"
                resultText.text = "Stopped."
            }
        }
        layout.addView(continuousButton)

        // On-demand liters_delivered read -- check this while continuous
        // pulsing (above) is running, to watch it tick up live.
        var autoReading = false
        val autoReadButton = Button(this).apply { text = "Read DUT liters_delivered" }
        addButton("Read DUT liters_delivered now") {
            connectedDut(
                onReady = { client ->
                    val liters = client.deviceInfo()?.optDouble("liters_delivered")
                    runOnUiThread { resultText.text = "liters_delivered: ${liters ?: "no response"}" }
                },
                onFail = { resultText.text = "Could not connect to the DUT WS API." },
            )
        }
        autoReadButton.setOnClickListener {
            if (!autoReading) {
                autoReading = true
                autoReadButton.text = "Stop auto-read"
                Thread {
                    var client = dut
                    if (client == null || !client.connected) {
                        client = DutWsClient()
                        if (!client.connect()) {
                            runOnUiThread { resultText.text = "Could not connect to the DUT WS API."; autoReading = false; autoReadButton.text = "Start auto-read (every 3s)" }
                            return@Thread
                        }
                        dut = client
                    }
                    while (autoReading) {
                        val liters = client.deviceInfo()?.optDouble("liters_delivered")
                        runOnUiThread { resultText.text = "liters_delivered: ${liters ?: "no response"} (auto-reading…)" }
                        Thread.sleep(3000)
                    }
                }.start()
            } else {
                autoReading = false
                autoReadButton.text = "Start auto-read (every 3s)"
            }
        }
        autoReadButton.text = "Start auto-read (every 3s)"
        layout.addView(autoReadButton)

        layout.addView(TextView(this).apply { text = "MQTT broker (independent of DUT self-report)"; setPadding(0, 24, 0, 0) })
        addButton("Check broker for a status message") {
            val id = currentDeviceId
            if (id == null) {
                resultText.text = "No known device ID yet -- flash or use 'Run Tests Only' first."
                return@addButton
            }
            resultText.text = "Subscribing to agrisense/FG1/$id/status…"
            Thread {
                val payload = MqttChecker.waitForStatusMessage(this@MainActivity, id)
                runOnUiThread {
                    resultText.text = if (payload != null) "Broker message received:\n$payload"
                        else "No message received -- broker unreachable, DUT not publishing, or credentials rejected."
                }
            }.start()
        }

        AlertDialog.Builder(this)
            .setTitle("Jig / DUT Diagnostics")
            .setView(android.widget.ScrollView(this).apply { addView(layout) })
            .setNegativeButton(R.string.cancel_button) { _, _ -> pulsing = false; autoReading = false }
            .setOnDismissListener { pulsing = false; autoReading = false }
            .show()
    }

    // ==================== Settings dialog ====================

    private fun showSettingsDialog() {
        val layout = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(48, 24, 48, 0) }
        fun field(label: String, value: String): EditText {
            layout.addView(TextView(this).apply { text = label })
            val input = EditText(this).apply { setText(value) }
            layout.addView(input)
            return input
        }

        val testApSsidInput = field("Office WiFi SSID (for the WiFi+MQTT test)", config.testApSsid)
        val testApPasswordInput = field("Office WiFi password", config.testApPassword)
        val pplInput = field("Expected pulses/liter", config.expectedCalibrationPpl.toString())
        val pulseCountInput = field("Flow test pulse count", config.flowTestPulseCount.toString())
        val operatorInput = field("Operator name", config.operatorName)
        val stationInput = field("Station name", config.stationName)

        val scroll = android.widget.ScrollView(this).apply { addView(layout) }
        AlertDialog.Builder(this)
            .setTitle(R.string.settings_button)
            .setView(scroll)
            .setPositiveButton(R.string.save_button) { _, _ ->
                config = BenchConfig(
                    testApSsid = testApSsidInput.text.toString().trim(),
                    testApPassword = testApPasswordInput.text.toString(),
                    expectedCalibrationPpl = pplInput.text.toString().toIntOrNull() ?: 450,
                    flowTestPulseCount = pulseCountInput.text.toString().toIntOrNull() ?: 450,
                    operatorName = operatorInput.text.toString().trim(),
                    stationName = stationInput.text.toString().trim(),
                )
                config.save(this)
                log("Settings saved.")
            }
            .setNegativeButton(R.string.cancel_button, null)
            .show()
    }

    // ==================== History dialog ====================

    private fun showHistoryDialog() {
        val reports = ReportStore.loadAll(this).reversed()
        val layout = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(32, 16, 32, 16) }
        val passCount = reports.count { it.optBoolean("overall_passed") }
        layout.addView(TextView(this).apply {
            text = "${reports.size} units tested — $passCount passed, ${reports.size - passCount} failed"
            setPadding(0, 0, 0, 16)
        })
        for (r in reports) {
            layout.addView(TextView(this).apply {
                val ok = r.optBoolean("overall_passed")
                text = "${if (ok) "✅" else "❌"} ${r.optString("device_id", "UNKNOWN")}  ${r.optString("timestamp_utc")}"
                setPadding(0, 8, 0, 8)
                gravity = Gravity.START
            })
        }
        val scroll = android.widget.ScrollView(this).apply { addView(layout) }

        AlertDialog.Builder(this)
            .setTitle(R.string.history_button)
            .setView(scroll)
            .setPositiveButton(R.string.export_csv_button) { _, _ ->
                val file = ReportStore.exportCsv(this)
                val intent = Intent(Intent.ACTION_SEND).apply {
                    type = "text/plain"
                    putExtra(Intent.EXTRA_SUBJECT, "FG1 production test results")
                    putExtra(Intent.EXTRA_TEXT, file.readText())
                }
                startActivity(Intent.createChooser(intent, "Export CSV"))
            }
            .setNegativeButton(R.string.cancel_button, null)
            .show()
    }

    companion object {
        private const val ACTION_USB_PERMISSION = "com.nbagri.flashertester.USB_PERMISSION"
        private const val BAUD_RATE = 115200
        private const val BOOT_LOG_DURATION_MS = 10_000L

        // Standard ESP32/PlatformIO flash layout -- matches
        // products/FG1-flowguard/firmware's min_spiffs.csv partition table.
        private const val BOOTLOADER_OFFSET = 0x1000
        private const val PARTITIONS_OFFSET = 0x8000
        private const val APP_OFFSET = 0x10000
    }
}
