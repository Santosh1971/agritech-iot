package com.nbagri.flasherspike

import android.hardware.usb.UsbDeviceConnection
import android.os.SystemClock
import com.hoho.android.usbserial.driver.UsbSerialPort
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

    /**
     * Called from native: android_read(). android_port.c requires exactly `len` bytes
     * back (or a reported failure) — but UsbSerialPort.read() is a single USB transfer,
     * like a raw read(2): it returns as soon as *some* bytes arrive within the timeout,
     * not necessarily all `len` of them. A CP2102 trickling in a SLIP-framed response a
     * few bytes at a time would otherwise make nearly every read here report short,
     * which android_port.c has no choice but to treat as ESP_LOADER_ERROR_TIMEOUT. Loop
     * until the full amount arrives or the deadline passes, same as esp-serial-flasher's
     * own reference port (port/linux_port.c's read_data()).
     */
    fun read(buffer: ByteArray, len: Int, timeoutMs: Int): Int {
        val deadline = SystemClock.elapsedRealtime() + timeoutMs
        val chunk = ByteArray(len)
        var offset = 0
        while (offset < len) {
            val remaining = (deadline - SystemClock.elapsedRealtime()).toInt()
            if (remaining <= 0) break
            val n = try {
                port.read(chunk, len - offset, remaining)
            } catch (e: IOException) {
                -1
            }
            if (n <= 0) break
            System.arraycopy(chunk, 0, buffer, offset, n)
            offset += n
        }
        return offset
    }

    companion object {
        private const val TAG = "UsbSerialTransport"
    }
}
