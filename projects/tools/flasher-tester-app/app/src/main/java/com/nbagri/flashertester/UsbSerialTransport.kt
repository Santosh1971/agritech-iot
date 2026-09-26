package com.nbagri.flashertester

import android.hardware.usb.UsbDeviceConnection
import android.os.SystemClock
import com.hoho.android.usbserial.driver.UsbSerialPort
import com.hoho.android.usbserial.util.SerialInputOutputManager
import java.io.IOException

/**
 * Thin wrapper around [UsbSerialPort] — the handful of operations the
 * native esp-serial-flasher port (app/src/main/cpp/android_port.c) calls
 * back into over JNI (method names/signatures must match android_port.c's
 * android_port_bind() exactly). Every method swallows IOException into a
 * sentinel return value: esp-serial-flasher treats any port failure as a
 * timeout and retries or aborts on its own, so there's nothing more useful
 * to do with the exception here than log it.
 */
class UsbSerialTransport(private val port: UsbSerialPort) {

    fun open(connection: UsbDeviceConnection, baudRate: Int): Boolean = try {
        port.open(connection)
        port.setParameters(baudRate, UsbSerialPort.DATABITS_8, UsbSerialPort.STOPBITS_1, UsbSerialPort.PARITY_NONE)
        true
    } catch (e: IOException) {
        android.util.Log.e(TAG, "open() failed", e)
        false
    }

    fun close() {
        try {
            port.close()
        } catch (e: IOException) {
            // already gone — nothing to clean up
        }
        // 2026-09-18: this app's UsbSerialDriver.getPorts() caches its port
        // objects (confirmed by decompiling Cp21xxSerialDriver -- getPorts()
        // just returns a stored field), so every captureBootLog() call
        // across a test run reuses the SAME underlying UsbSerialPort
        // instance, just wrapped in a fresh UsbSerialTransport each time.
        // SerialInputOutputManager.stop() is fire-and-forget (confirmed by
        // reading its source): it flips a state flag and returns
        // immediately, without waiting for its internal read thread to
        // actually exit. That thread can still be blocked inside the
        // port's own blocking read() when this close() call interrupts it
        // (the library's own source comment: "when using readTimeout == 0
        // (default), additionally use usbSerialPort.close() to interrupt
        // blocking read") -- so close() unblocks it, but the thread still
        // needs a moment to notice and unwind. Reopening the same port
        // object for the next capture before that happens risks two
        // read loops briefly overlapping on one port, which matches
        // exactly what was observed: the first capture on a freshly
        // opened port (right after flashing) always came back clean, but
        // a later capture reusing the port (e.g. the WiFi/MQTT wait, which
        // runs after the boot-log capture already used and closed this
        // same port) still occasionally showed one line duplicated
        // hundreds/thousands of times. This margin is a direct mitigation
        // for that specific, now well-understood race -- not a guess.
        try {
            Thread.sleep(200)
        } catch (e: InterruptedException) {
            Thread.currentThread().interrupt()
        }
    }

    /** Called from native: android_change_rate(). */
    fun setBaudRate(baud: Int): Boolean = try {
        port.setParameters(baud, UsbSerialPort.DATABITS_8, UsbSerialPort.STOPBITS_1, UsbSerialPort.PARITY_NONE)
        true
    } catch (e: IOException) {
        false
    }

    /** Called from native: set_dtr_rts(). */
    fun setDTR(value: Boolean) {
        try {
            port.setDTR(value)
        } catch (e: IOException) {
            android.util.Log.w(TAG, "setDTR failed", e)
        }
    }

    /** Called from native: set_dtr_rts() / android_reset_target(). */
    fun setRTS(value: Boolean) {
        try {
            port.setRTS(value)
        } catch (e: IOException) {
            android.util.Log.w(TAG, "setRTS failed", e)
        }
    }

    /** Called from native: android_write(). */
    fun write(data: ByteArray, len: Int, timeoutMs: Int): Boolean = try {
        port.write(data, len, timeoutMs)
        true
    } catch (e: IOException) {
        false
    }

    // esp-serial-flasher's SLIP decoder pulls bytes one at a time (matching its own
    // reference port, port/linux_port.c's read_data()), but a bench terminal app proves
    // this exact phone/cable/board reads cleanly when polling in normal-sized chunks —
    // strongly suggesting the CP210x driver has a quirk specific to being asked for
    // literally 1 byte per underlying read() call. rxQueue decouples the two: pull a
    // real chunk from the driver, hand bytes out of this queue one at a time, refill
    // only when it runs dry.
    private val rxQueue = ArrayDeque<Byte>()
    private val rxChunk = ByteArray(256)

    /** Called from native: android_read(). See rxQueue's comment for why this buffers. */
    fun read(buffer: ByteArray, len: Int, timeoutMs: Int): Int {
        val deadline = SystemClock.elapsedRealtime() + timeoutMs
        var offset = 0
        while (offset < len) {
            if (rxQueue.isEmpty()) {
                val remaining = (deadline - SystemClock.elapsedRealtime()).toInt()
                if (remaining <= 0) break
                val n = try {
                    port.read(rxChunk, rxChunk.size, remaining)
                } catch (e: IOException) {
                    -1
                }
                if (n <= 0) break
                for (i in 0 until n) rxQueue.addLast(rxChunk[i])
            }
            buffer[offset] = rxQueue.removeFirst()
            offset++
        }
        return offset
    }

    /** Kotlin-side only (not called from native) — reads whatever the target prints for
     *  durationMs, on the same port/baud already used for flashing. Used right after a
     *  successful flash, while the port esp_loader_reset_target() just reset into run mode
     *  is still open, to capture the boot log for the field-report share sheet -- and later,
     *  via captureLiveDutSerial(), to passively listen at other points in a test run.
     *
     *  2026-09-18: this used to be a manual read()-in-a-loop implementation
     *  (like android_read() above), and repeatedly produced garbled/
     *  duplicated captured text -- e.g. text from two unrelated,
     *  non-adjacent boot log lines spliced together, and one WiFi-status
     *  line repeated thousands of times in a single 32s capture. Ruled out
     *  a firmware bug rigorously: the one repeating line can only be
     *  printed from a WiFi retry that's rate-limited to once per
     *  WIFI_RETRY_INTERVAL_MS (60s) in main.cpp, so thousands of repeats in
     *  32s were never physically possible from the DUT. Traced instead to
     *  this library's own documented limitation (see its FAQ): "continuous
     *  transfers at high baud rates (115k2+) may experience data loss due
     *  to Android's non-real-time nature and potential buffer overflows"
     *  with a manual blocking read() loop -- exactly this app's baud rate
     *  and exactly the bursty-output pattern a WiFi reconnect cycle
     *  produces. The library's own fix for this is SerialInputOutputManager
     *  (an async reader running its own internal thread/buffering) instead
     *  of hand-rolled polling -- switched to that here. android_read()
     *  above stays on the manual path deliberately: it's proven reliable
     *  (esp-serial-flasher verifies a hash after every flash) and this is
     *  a passive-listening-only concern, not a flashing-protocol one. */
    fun captureBootLog(durationMs: Long): String {
        val sb = StringBuilder()
        val lock = Object()
        val ioManager = SerialInputOutputManager(port, object : SerialInputOutputManager.Listener {
            override fun onNewData(data: ByteArray) {
                synchronized(lock) {
                    for (b in data) {
                        val c = b.toInt().toChar()
                        if (c == '\n' || c == '\r' || c == '\t' || c.code in 0x20..0x7E) sb.append(c)
                    }
                }
            }

            override fun onRunError(e: Exception) {
                android.util.Log.w(TAG, "SerialInputOutputManager error during capture", e)
            }
        })
        ioManager.start()
        try {
            Thread.sleep(durationMs)
        } finally {
            ioManager.stop()
        }
        return synchronized(lock) { sb.toString() }
    }

    companion object {
        private const val TAG = "UsbSerialTransport"
    }
}
