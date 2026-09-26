package com.nbagri.flashertester

/**
 * JNI entry point into app/src/main/cpp/jni_bridge.c, which drives
 * esp-serial-flasher against a [UsbSerialTransport] through the port
 * implementation in app/src/main/cpp/android_port.c.
 */
object NativeFlasher {
    init {
        System.loadLibrary("flashertester")
    }

    /**
     * Flashes up to three segments (bootloader/partitions/app) in one
     * connect session. Pass null for a segment to skip it. Returns an
     * esp_loader_error_t value — see [FlashResult.describe]. `listener`'s
     * onProgress is called from jni_bridge.c's flash_one() write loop —
     * method name/signature must match what Java_..._flash() looks up.
     */
    external fun flash(
        transport: UsbSerialTransport,
        bootloader: ByteArray?, bootloaderOffset: Int,
        partitions: ByteArray?, partitionsOffset: Int,
        app: ByteArray?, appOffset: Int,
        listener: FlashProgressListener
    ): Int

    /**
     * Reads the connected chip's burned-in MAC (6 bytes), or null if the
     * connect/read handshake fails. Works on a blank chip — it's an eFuse
     * value, not something firmware has to report. Opens and closes its
     * own connect session, separate from [flash]'s.
     */
    external fun readMac(transport: UsbSerialTransport): ByteArray?

    /**
     * Erases the whole flash chip, NVS included — a normal [flash] call
     * only writes the bootloader/partitions/app offsets and never touches
     * NVS, so WiFi/MQTT credentials a unit picked up during its own test
     * run otherwise survive a reflash. Returns an esp_loader_error_t
     * value — see [FlashResult.describe]. Opens and closes its own
     * connect session, separate from [flash]'s.
     */
    external fun eraseChip(transport: UsbSerialTransport): Int
}

fun interface FlashProgressListener {
    /** Called from native. `stage` is "bootloader" | "partitions" | "firmware"; percent is 0-100. */
    fun onProgress(stage: String, percent: Int)
}

/** Mirrors esp_loader_error_t in esp-serial-flasher/include/esp_loader_error.h exactly — order matters. */
object FlashResult {
    private val NAMES = arrayOf(
        "SUCCESS", "FAIL", "TIMEOUT", "IMAGE_SIZE", "INVALID_MD5",
        "INVALID_PARAM", "INVALID_TARGET", "UNSUPPORTED_CHIP",
        "UNSUPPORTED_FUNC", "INVALID_RESPONSE", "EFUSE_BLOCK_IN_USE", "EFUSE_BURN_FAILED"
    )

    fun describe(code: Int): String = NAMES.getOrElse(code) { "UNKNOWN($code)" }
}
