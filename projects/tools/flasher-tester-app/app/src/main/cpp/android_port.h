/*
 * esp-serial-flasher port for Android.
 *
 * The library's port vtable (esp_loader_port_ops_t, see
 * esp-serial-flasher/include/esp_loader_io.h) needs write/read/DTR/RTS/
 * baud-rate/log callbacks. On Android, actual USB I/O only exists on the
 * Kotlin side (UsbSerialTransport, wrapping usb-serial-for-android) — there
 * is no native USB Host API — so those callbacks call back into a bound
 * Kotlin object over JNI. Timers and delays don't need Kotlin and run
 * natively (see android_port.c).
 */
#pragma once

#include <jni.h>
#include <stdint.h>

#include "esp_loader_io.h"

typedef struct {
    esp_loader_port_t port; /* embedded base — pass &this.port to esp_loader_init_serial() */

    JNIEnv    *env;
    jobject    transport;    /* the Kotlin UsbSerialTransport instance */
    jmethodID  mWrite;       /* boolean write(byte[] data, int len, int timeoutMs) */
    jmethodID  mRead;        /* int read(byte[] buffer, int len, int timeoutMs) */
    jmethodID  mSetDTR;      /* void setDTR(boolean value) */
    jmethodID  mSetRTS;      /* void setRTS(boolean value) */
    jmethodID  mSetBaudRate; /* boolean setBaudRate(int baud) */

    int64_t time_end; /* deadline for the current start_timer()/remaining_time() pair */
} android_port_t;

/*
 * Binds `p` to `transport`, caching its method IDs and installing the port
 * ops table. Call once, before esp_loader_init_serial(&loader, &p->port).
 *
 * Returns ESP_LOADER_ERROR_FAIL if any expected method is missing —
 * i.e. UsbSerialTransport.kt's signatures have drifted from this file.
 */
esp_loader_error_t android_port_bind(android_port_t *p, JNIEnv *env, jobject transport);
