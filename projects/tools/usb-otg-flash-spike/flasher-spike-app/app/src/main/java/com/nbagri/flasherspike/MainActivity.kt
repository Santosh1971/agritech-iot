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
import android.widget.Button
import android.widget.ProgressBar
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.hoho.android.usbserial.driver.UsbSerialDriver
import com.hoho.android.usbserial.driver.UsbSerialProber
import java.io.IOException
import java.io.InputStream

/**
 * Single-screen bench harness for the USB-OTG flash spike — see
 * projects/tools/usb-otg-flash-spike/SPIKE_SPEC.md for what this is proving
 * and the go/no-go criteria it feeds into. Two independent flash paths, both
 * from the same bundled assets bin files (see assets/README.md), not wired
 * to any backend or auth (SPIKE_SPEC.md §2):
 *
 *  - USB-OTG (startFlash/NativeFlasher): the spike's actual subject — talks
 *    to the ROM bootloader directly.
 *  - WiFi/SoftAP (startWifiFlash/WifiOtaFlasher): a bench-convenience
 *    bonus once LocalServer.cpp had ElegantOTA wired up — no bootloader
 *    handshake, no USB, just an HTTP upload to a board already running.
 */
class MainActivity : AppCompatActivity() {

    private lateinit var statusText: TextView
    private lateinit var logText: TextView
    private lateinit var progressBar: ProgressBar
    private lateinit var progressText: TextView
    private lateinit var usbManager: UsbManager

    private val permissionReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            if (intent.action != ACTION_USB_PERMISSION) return
            val granted = intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)
            if (granted) {
                startFlash()
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
        // the on-screen log. This is a bench tool, not something Kamta-facing,
        // so keeping the screen on unconditionally is the right tradeoff here.
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        statusText = findViewById(R.id.statusText)
        logText = findViewById(R.id.logText)
        progressBar = findViewById(R.id.progressBar)
        progressText = findViewById(R.id.progressText)
        usbManager = getSystemService(Context.USB_SERVICE) as UsbManager

        val filter = IntentFilter(ACTION_USB_PERMISSION)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            registerReceiver(permissionReceiver, filter, Context.RECEIVER_NOT_EXPORTED)
        } else {
            @Suppress("UnspecifiedRegisterReceiverFlag")
            registerReceiver(permissionReceiver, filter)
        }

        findViewById<Button>(R.id.flashButton).setOnClickListener { requestFlash() }
        findViewById<Button>(R.id.wifiFlashButton).setOnClickListener { startWifiFlash() }
    }

    override fun onDestroy() {
        super.onDestroy()
        unregisterReceiver(permissionReceiver)
    }

    private fun findDriver(): UsbSerialDriver? =
        UsbSerialProber.getDefaultProber().findAllDrivers(usbManager).firstOrNull()

    private fun requestFlash() {
        val driver = findDriver()
        if (driver == null) {
            log("No USB serial device found. Check the OTG cable and that the board is powered.")
            return
        }
        if (usbManager.hasPermission(driver.device)) {
            startFlash()
            return
        }
        val flags = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) PendingIntent.FLAG_MUTABLE else 0
        // Explicit setPackage() — Android 14+ requires non-exported broadcasts to target a
        // package explicitly; without it delivery to our own RECEIVER_NOT_EXPORTED receiver
        // can silently fail on newer devices.
        val permissionIntent = PendingIntent.getBroadcast(
            this, 0, Intent(ACTION_USB_PERMISSION).setPackage(packageName), flags
        )
        usbManager.requestPermission(driver.device, permissionIntent)
    }

    private fun startFlash() {
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
                // Stage transitions and completions are useful bench context;
                // the other ~900 in-between ticks for a 935KB firmware write
                // would just flood this on-screen log (Logcat still has all of them).
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

            val bootloader = readAsset("bootloader.bin")
            val partitions = readAsset("partitions.bin")
            val app = readAsset("firmware.bin")
            if (bootloader == null && partitions == null && app == null) {
                runOnUiThread {
                    log(
                        "No .bin files in assets/. See assets/README.md — copy a build from " +
                            "products/FG1-flowguard/firmware/.pio/build/esp32dev_ds1307/ before running."
                    )
                }
                transport.close()
                return@Thread
            }

            val result = NativeFlasher.flash(
                transport,
                bootloader, BOOTLOADER_OFFSET,
                partitions, PARTITIONS_OFFSET,
                app, APP_OFFSET,
                listener
            )
            transport.close()

            runOnUiThread {
                val description = FlashResult.describe(result)
                log("Result: $description ($result)")
                statusText.text = if (result == 0) "Flash succeeded" else "Flash failed: $description"
            }
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
