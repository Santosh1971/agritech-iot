package com.nbagri.flasherspike

import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.hardware.usb.UsbManager
import android.os.Build
import android.os.Bundle
import android.widget.Button
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.hoho.android.usbserial.driver.UsbSerialDriver
import com.hoho.android.usbserial.driver.UsbSerialProber
import java.io.IOException
import java.io.InputStream

/**
 * Single-screen bench harness for the USB-OTG flash spike — see
 * projects/tools/usb-otg-flash-spike/SPIKE_SPEC.md for what this is proving
 * and the go/no-go criteria it feeds into.
 *
 * Flashes whichever of assets/bootloader.bin, assets/partitions.bin,
 * assets/firmware.bin are present (see assets/README.md) to a connected
 * ESP32 over USB-OTG. Not wired to any backend or auth — bin files are
 * bundled locally, per the spike's scope (SPIKE_SPEC.md §2).
 */
class MainActivity : AppCompatActivity() {

    private lateinit var statusText: TextView
    private lateinit var logText: TextView
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

        statusText = findViewById(R.id.statusText)
        logText = findViewById(R.id.logText)
        usbManager = getSystemService(Context.USB_SERVICE) as UsbManager

        val filter = IntentFilter(ACTION_USB_PERMISSION)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            registerReceiver(permissionReceiver, filter, Context.RECEIVER_NOT_EXPORTED)
        } else {
            @Suppress("UnspecifiedRegisterReceiverFlag")
            registerReceiver(permissionReceiver, filter)
        }

        findViewById<Button>(R.id.flashButton).setOnClickListener { requestFlash() }
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
        log("---- flash attempt starting ----")

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
                app, APP_OFFSET
            )
            transport.close()

            runOnUiThread {
                val description = FlashResult.describe(result)
                log("Result: $description ($result)")
                statusText.text = if (result == 0) "Flash succeeded" else "Flash failed: $description"
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

        // Standard ESP32/Arduino-PlatformIO flash layout — matches
        // products/FG1-flowguard/firmware's min_spiffs.csv partition table.
        private const val BOOTLOADER_OFFSET = 0x1000
        private const val PARTITIONS_OFFSET = 0x8000
        private const val APP_OFFSET = 0x10000
    }
}
