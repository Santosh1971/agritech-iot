/*
 * See android_port.h for what this implements and why.
 *
 * The enter_bootloader/reset_target DTR/RTS sequences are a direct port of
 * esp-serial-flasher's own port/linux_port.c, LINUX_GPIO_DTR_RTS mode,
 * non-USB-JTAG branch (esptool's "UnixTightReset"/"HardReset") — that file
 * carries the full annotated explanation of each step; FG1's CP2102 uses the
 * same standard inverting auto-reset circuit, so no polarity inversion is
 * needed here (SERIAL_FLASHER_*_INVERT both default to false).
 */
#include <stdarg.h>
#include <time.h>
#include <unistd.h>

#include <android/log.h>

#include "android_port.h"

#define LOG_TAG "flasherspike"

/* esp-serial-flasher/CMakeLists.txt SERIAL_FLASHER_RESET_HOLD_TIME_MS / _BOOT_HOLD_TIME_MS defaults. */
#define RESET_HOLD_MS 100
#define BOOT_HOLD_MS  50

static int64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static esp_loader_error_t android_port_init(esp_loader_port_t *port)
{
    (void)port;
    return ESP_LOADER_SUCCESS; /* Kotlin already opened the port before esp_loader_init_serial() */
}

static void android_port_deinit(esp_loader_port_t *port)
{
    (void)port; /* Kotlin owns close(); nothing to release here */
}

static void android_delay_ms(esp_loader_port_t *port, uint32_t ms)
{
    (void)port;
    usleep((useconds_t)ms * 1000u);
}

static void android_start_timer(esp_loader_port_t *port, uint32_t ms)
{
    android_port_t *p = container_of(port, android_port_t, port);
    p->time_end = now_ms() + (int64_t)ms;
}

static uint32_t android_remaining_time(esp_loader_port_t *port)
{
    android_port_t *p = container_of(port, android_port_t, port);
    int64_t remaining = p->time_end - now_ms();
    return (remaining > 0) ? (uint32_t)remaining : 0;
}

static void set_dtr_rts(android_port_t *p, jboolean dtr, jboolean rts)
{
    (*p->env)->CallVoidMethod(p->env, p->transport, p->mSetDTR, dtr);
    (*p->env)->CallVoidMethod(p->env, p->transport, p->mSetRTS, rts);
}

static void android_enter_bootloader(esp_loader_port_t *port)
{
    android_port_t *p = container_of(port, android_port_t, port);

    /* esptool UnixTightReset: through (1,1) to avoid a (0,0) glitch, then
     * hold BOOT low across the reset pulse, then release BOOT after RESET
     * comes back up. */
    set_dtr_rts(p, JNI_FALSE, JNI_FALSE);
    set_dtr_rts(p, JNI_TRUE,  JNI_TRUE);
    set_dtr_rts(p, JNI_FALSE, JNI_TRUE);   /* BOOT asserted, RESET asserted */
    android_delay_ms(port, RESET_HOLD_MS);
    set_dtr_rts(p, JNI_TRUE,  JNI_FALSE);  /* RESET released while BOOT still low */
    android_delay_ms(port, BOOT_HOLD_MS);
    set_dtr_rts(p, JNI_FALSE, JNI_FALSE);  /* BOOT released — chip is in the ROM bootloader */
}

static void android_reset_target(esp_loader_port_t *port)
{
    android_port_t *p = container_of(port, android_port_t, port);
    (*p->env)->CallVoidMethod(p->env, p->transport, p->mSetRTS, JNI_TRUE);
    android_delay_ms(port, RESET_HOLD_MS);
    (*p->env)->CallVoidMethod(p->env, p->transport, p->mSetRTS, JNI_FALSE);
}

static esp_loader_error_t android_write(esp_loader_port_t *port, const uint8_t *data, uint16_t size, uint32_t timeout)
{
    android_port_t *p = container_of(port, android_port_t, port);
    JNIEnv *env = p->env;

    jbyteArray arr = (*env)->NewByteArray(env, size);
    if (arr == NULL) {
        return ESP_LOADER_ERROR_FAIL;
    }
    (*env)->SetByteArrayRegion(env, arr, 0, size, (const jbyte *)data);

    jboolean ok = (*env)->CallBooleanMethod(env, p->transport, p->mWrite, arr, (jint)size, (jint)timeout);
    (*env)->DeleteLocalRef(env, arr);

    return ok ? ESP_LOADER_SUCCESS : ESP_LOADER_ERROR_TIMEOUT;
}

static esp_loader_error_t android_read(esp_loader_port_t *port, uint8_t *data, uint16_t size, uint32_t timeout)
{
    android_port_t *p = container_of(port, android_port_t, port);
    JNIEnv *env = p->env;

    jbyteArray arr = (*env)->NewByteArray(env, size);
    if (arr == NULL) {
        return ESP_LOADER_ERROR_FAIL;
    }

    jint n = (*env)->CallIntMethod(env, p->transport, p->mRead, arr, (jint)size, (jint)timeout);
    if (n == (jint)size) {
        (*env)->GetByteArrayRegion(env, arr, 0, size, (jbyte *)data);
    }
    (*env)->DeleteLocalRef(env, arr);

    return (n == (jint)size) ? ESP_LOADER_SUCCESS : ESP_LOADER_ERROR_TIMEOUT;
}

static esp_loader_error_t android_change_rate(esp_loader_port_t *port, uint32_t baudrate)
{
    android_port_t *p = container_of(port, android_port_t, port);
    jboolean ok = (*p->env)->CallBooleanMethod(p->env, p->transport, p->mSetBaudRate, (jint)baudrate);
    return ok ? ESP_LOADER_SUCCESS : ESP_LOADER_ERROR_INVALID_PARAM;
}

static void android_log(esp_loader_port_t *port, esp_loader_log_level_t level, const char *fmt, va_list args)
{
    (void)port;
    int prio;
    switch (level) {
        case ESP_LOADER_LOG_ERROR: prio = ANDROID_LOG_ERROR; break;
        case ESP_LOADER_LOG_WARN:  prio = ANDROID_LOG_WARN;  break;
        case ESP_LOADER_LOG_DEBUG: prio = ANDROID_LOG_DEBUG; break;
        default:                   prio = ANDROID_LOG_INFO;  break;
    }
    __android_log_vprint(prio, LOG_TAG, fmt, args);
}

static const esp_loader_port_ops_t android_uart_ops = {
    .init                     = android_port_init,
    .deinit                   = android_port_deinit,
    .enter_bootloader         = android_enter_bootloader,
    .reset_target             = android_reset_target,
    .start_timer              = android_start_timer,
    .remaining_time           = android_remaining_time,
    .delay_ms                 = android_delay_ms,
    .log                      = android_log,
    .log_hex                  = NULL,
    .change_transmission_rate = android_change_rate,
    .write                    = android_write,
    .read                     = android_read,
    .spi_set_cs               = NULL,
    .sdio_write               = NULL,
    .sdio_read                = NULL,
    .sdio_card_init           = NULL,
};

esp_loader_error_t android_port_bind(android_port_t *p, JNIEnv *env, jobject transport)
{
    p->port.ops = &android_uart_ops;
    p->env = env;
    p->transport = transport;
    p->time_end = 0;

    jclass cls = (*env)->GetObjectClass(env, transport);
    p->mWrite       = (*env)->GetMethodID(env, cls, "write", "([BII)Z");
    p->mRead        = (*env)->GetMethodID(env, cls, "read", "([BII)I");
    p->mSetDTR      = (*env)->GetMethodID(env, cls, "setDTR", "(Z)V");
    p->mSetRTS      = (*env)->GetMethodID(env, cls, "setRTS", "(Z)V");
    p->mSetBaudRate = (*env)->GetMethodID(env, cls, "setBaudRate", "(I)Z");
    (*env)->DeleteLocalRef(env, cls);

    if (!p->mWrite || !p->mRead || !p->mSetDTR || !p->mSetRTS || !p->mSetBaudRate) {
        (*env)->ExceptionClear(env); /* GetMethodID throws NoSuchMethodError on failure */
        return ESP_LOADER_ERROR_FAIL;
    }
    return ESP_LOADER_SUCCESS;
}
