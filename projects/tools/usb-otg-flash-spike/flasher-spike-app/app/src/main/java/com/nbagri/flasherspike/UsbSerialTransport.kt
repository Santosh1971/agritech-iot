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

    companion object {
        private const val TAG = "UsbSerialTransport"
    }
}
