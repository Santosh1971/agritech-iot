package com.nbagri.flashertester

import android.hardware.usb.UsbDeviceConnection
import android.hardware.usb.UsbManager
import com.hoho.android.usbserial.driver.UsbSerialDriver
import com.hoho.android.usbserial.driver.UsbSerialPort
import com.hoho.android.usbserial.util.SerialInputOutputManager
import java.io.IOException
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit

/**
 * USB-serial client for the test jig (ESP32) -- see
 * testing/jig_firmware/esp32_usb_serial/src/main.cpp for the firmware
 * this talks to. Wired directly to the same phone as the DUT via a
 * USB-OTG hub -- no WiFi, no targeting, no ambiguity: this exact
 * physical connection *is* the jig, found once at session start by
 * probing every connected USB-serial port with PING and keeping
 * whichever one replies PONG.
 *
 * Held open for the whole app session (not re-opened per unit like the
 * DUT's connection) -- it's a permanent fixture, unlike the DUT which
 * gets swapped out every test run.
 *
 * Reads via SerialInputOutputManager (started once in open(), stopped in
 * close()), not a manual read()-in-a-loop -- see UsbSerialTransport.
 * captureBootLog()'s doc comment for the full investigation. Short version:
 * a manual blocking-read loop at 115200 baud can lose/corrupt data under
 * Android's non-real-time scheduling (documented library limitation, not
 * a driver quirk), and this class's original 64-byte-buffer version hit
 * exactly that once device_info's long JSON reply started flowing through
 * it via the network bridge -- short "{}" replies (calibrate, relay_test)
 * were fine, long ones came back empty. The IoManager is the library's own
 * recommended fix, and lets this class stay open/listening continuously
 * for its whole session instead of opening a fresh read window per command.
 */
class JigSerialClient(private val port: UsbSerialPort) {
    @Volatile private var open = false
    private var ioManager: SerialInputOutputManager? = null
    private val lineQueue = LinkedBlockingQueue<String>()
    private val partialLine = StringBuilder()

    fun open(connection: UsbDeviceConnection, baudRate: Int = 115200): Boolean = try {
        port.open(connection)
        port.setParameters(baudRate, UsbSerialPort.DATABITS_8, UsbSerialPort.STOPBITS_1, UsbSerialPort.PARITY_NONE)
        val manager = SerialInputOutputManager(port, object : SerialInputOutputManager.Listener {
            override fun onNewData(data: ByteArray) {
                synchronized(partialLine) {
                    for (b in data) {
                        val c = b.toInt().toChar()
                        when {
                            c == '\n' -> {
                                lineQueue.offer(partialLine.toString().trim())
                                partialLine.setLength(0)
                            }
                            c != '\r' -> partialLine.append(c)
                        }
                    }
                }
            }

            override fun onRunError(e: Exception) {
                android.util.Log.w(TAG, "SerialInputOutputManager error", e)
            }
        })
        manager.start()
        ioManager = manager
        open = true
        true
    } catch (e: IOException) {
        false
    }

    fun close() {
        ioManager?.stop()
        ioManager = null
        try {
            port.close()
        } catch (e: IOException) {
            // already gone
        }
        open = false
    }

    /** Sends "<command>\n" and waits for the next complete line (up to
     *  timeoutMs), or null on timeout/error. Drops anything already queued
     *  from before this call -- a stale unconsumed reply from a previous,
     *  timed-out exchange shouldn't be handed back as this one's answer. */
    private fun sendAndReadLine(command: String, timeoutMs: Int): String? {
        if (!open) return null
        return try {
            lineQueue.clear()
            port.write((command + "\n").toByteArray(), 1000)
            lineQueue.poll(timeoutMs.toLong(), TimeUnit.MILLISECONDS)
        } catch (e: IOException) {
            null
        }
    }

    fun ping(timeoutMs: Int = 2000): Boolean = sendAndReadLine("PING", timeoutMs) == "PONG"

    /** True if the jig currently senses the DUT's relay output closed. */
    fun relayState(timeoutMs: Int = 2000): Boolean? = when (sendAndReadLine("RELAY?", timeoutMs)) {
        "RELAY:ON" -> true
        "RELAY:OFF" -> false
        else -> null
    }

    /** Blocks until the jig confirms the pulses were actually emitted -- timeout scales with count. */
    fun pulse(count: Int): Boolean {
        val timeoutMs = 3000 + (count / 200) * 1000
        return sendAndReadLine("PULSE:$count", timeoutMs) == "OK:$count"
    }

    // ==================== Network bridge (JIG_NETWORK_BRIDGE_SPEC.md) ====================
    // SoftAP-phase only for now -- see that doc's §4 for the full protocol
    // and §9 for the phased build plan.

    /** Joins [ssid]/[pass] as a WiFi STA. Returns the raw reply
     *  ("JOINED:<ip>" or "JOIN_FAIL:<reason>") or null on timeout -- the
     *  10s WIFI_JOIN_TIMEOUT_MS on the firmware side needs real margin
     *  here since a join can legitimately take that long. */
    fun joinAp(ssid: String, pass: String, timeoutMs: Int = 12000): String? =
        sendAndReadLine("JOIN_AP:$ssid:$pass", timeoutMs)

    fun leaveWifi(timeoutMs: Int = 3000): Boolean = sendAndReadLine("LEAVE_WIFI", timeoutMs) == "OK"

    fun wifiStatus(timeoutMs: Int = 2000): String? = sendAndReadLine("WIFI_STATUS?", timeoutMs)

    /** Relays [json] to the DUT's local /command endpoint through the
     *  jig's own WiFi link. Returns the raw reply ("HTTP_OK:<json>" or
     *  "HTTP_FAIL:<reason>") or null on timeout -- margin above the
     *  firmware's own 8s HTTP_TIMEOUT_MS. */
    fun httpCommand(json: String, timeoutMs: Int = 9000): String? =
        sendAndReadLine("HTTP_CMD:$json", timeoutMs)

    companion object {
        private const val TAG = "JigSerialClient"

        /**
         * Opens [driver]'s port, sends PING, and returns a ready client
         * if (and only if) it replies PONG -- otherwise closes the port
         * back up and returns null. Used to identify which of the
         * (possibly several) USB-serial devices on the hub is the jig,
         * without assuming a fixed port order.
         */
        fun probe(usbManager: UsbManager, driver: UsbSerialDriver, baudRate: Int = 115200): JigSerialClient? {
            val connection = usbManager.openDevice(driver.device) ?: return null
            val client = JigSerialClient(driver.ports.first())
            if (!client.open(connection, baudRate)) return null
            if (client.ping()) return client
            client.close()
            return null
        }
    }
}
