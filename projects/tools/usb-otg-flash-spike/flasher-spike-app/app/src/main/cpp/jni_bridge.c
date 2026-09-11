/*
 * JNI entry point called from NativeFlasher.kt. Drives esp-serial-flasher
 * end to end over one connect session: connect (with the bundled flasher
 * stub, for speed and reliability — see esp_loader_connect_with_stub() in
 * esp-serial-flasher/include/esp_loader.h), then flash whichever of
 * bootloader/partitions/app were passed, in that order, then reset the
 * target to run what was just flashed.
 *
 * The flash_start/write-loop/finish shape below mirrors
 * esp-serial-flasher/examples/common/example_common.c's flash_binary() —
 * see that file for the reference this was adapted from.
 */
#include <jni.h>

#include "android_port.h"
#include "esp_loader.h"

#define BLOCK_SIZE 1024

static esp_loader_error_t flash_one(esp_loader_t *loader, const uint8_t *bin, uint32_t size, uint32_t offset)
{
    if (bin == NULL || size == 0) {
        return ESP_LOADER_SUCCESS; /* nothing to flash for this slot */
    }

    esp_loader_flash_cfg_t cfg = {
        .offset = offset,
        .image_size = size,
        .block_size = BLOCK_SIZE,
    };

    esp_loader_error_t err = esp_loader_flash_start(loader, &cfg);
    if (err != ESP_LOADER_SUCCESS) {
        return err;
    }

    uint32_t remaining = size;
    const uint8_t *cursor = bin;
    while (remaining > 0) {
        uint32_t chunk = (remaining < BLOCK_SIZE) ? remaining : BLOCK_SIZE;
        err = esp_loader_flash_write(loader, &cfg, cursor, chunk);
        if (err != ESP_LOADER_SUCCESS) {
            return err;
        }
        cursor += chunk;
        remaining -= chunk;
    }

    return esp_loader_flash_finish(loader, &cfg);
}

JNIEXPORT jint JNICALL
Java_com_nbagri_flasherspike_NativeFlasher_flash(
    JNIEnv *env, jobject thiz,
    jobject transport,
    jbyteArray bootloader, jint bootloaderOffset,
    jbyteArray partitions, jint partitionsOffset,
    jbyteArray app, jint appOffset)
{
    (void)thiz;

    android_port_t aport;
    esp_loader_error_t err = android_port_bind(&aport, env, transport);
    if (err != ESP_LOADER_SUCCESS) {
        return (jint)err;
    }

    esp_loader_t loader;
    err = esp_loader_init_serial(&loader, &aport.port);
    if (err != ESP_LOADER_SUCCESS) {
        return (jint)err;
    }

    esp_loader_connect_args_t connect_args = ESP_LOADER_CONNECT_DEFAULT();
    err = esp_loader_connect_with_stub(&loader, &connect_args);
    if (err != ESP_LOADER_SUCCESS) {
        esp_loader_deinit(&loader);
        return (jint)err;
    }

    jbyte *bootBytes = bootloader ? (*env)->GetByteArrayElements(env, bootloader, NULL) : NULL;
    jbyte *partBytes = partitions ? (*env)->GetByteArrayElements(env, partitions, NULL) : NULL;
    jbyte *appBytes  = app        ? (*env)->GetByteArrayElements(env, app, NULL)        : NULL;

    do {
        err = flash_one(&loader, (const uint8_t *)bootBytes,
                         bootloader ? (uint32_t)(*env)->GetArrayLength(env, bootloader) : 0,
                         (uint32_t)bootloaderOffset);
        if (err != ESP_LOADER_SUCCESS) break;

        err = flash_one(&loader, (const uint8_t *)partBytes,
                         partitions ? (uint32_t)(*env)->GetArrayLength(env, partitions) : 0,
                         (uint32_t)partitionsOffset);
        if (err != ESP_LOADER_SUCCESS) break;

        err = flash_one(&loader, (const uint8_t *)appBytes,
                         app ? (uint32_t)(*env)->GetArrayLength(env, app) : 0,
                         (uint32_t)appOffset);
    } while (0);

    if (bootBytes) (*env)->ReleaseByteArrayElements(env, bootloader, bootBytes, JNI_ABORT);
    if (partBytes) (*env)->ReleaseByteArrayElements(env, partitions, partBytes, JNI_ABORT);
    if (appBytes)  (*env)->ReleaseByteArrayElements(env, app, appBytes, JNI_ABORT);

    if (err == ESP_LOADER_SUCCESS) {
        esp_loader_reset_target(&loader);
    }
    esp_loader_deinit(&loader);

    return (jint)err;
}
