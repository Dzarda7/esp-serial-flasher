/* Copyright 2026 Espressif Systems (Shanghai) CO LTD
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_loader_error.h"
#include "esp_loader.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Flash interface mode.
 */
typedef enum {
    ESP_FLASH_MODE_QIO  = 0, /*!< Quad I/O */
    ESP_FLASH_MODE_QOUT = 1, /*!< Quad output */
    ESP_FLASH_MODE_DIO  = 2, /*!< Dual I/O */
    ESP_FLASH_MODE_DOUT = 3, /*!< Dual output */
} esp_flash_mode_t;

/**
 * @brief Flash clock frequency.
 *
 * Not every chip supports every frequency; @ref esp_loader_elf_to_flash_image
 * returns @c ESP_LOADER_ERROR_INVALID_PARAM when the selected frequency is not
 * available on the target chip.
 */
typedef enum {
    ESP_FLASH_FREQ_80M,  /*!< 80 MHz  — ESP8266, ESP32, S2, S3, C3, C5, C6, P4 */
    ESP_FLASH_FREQ_60M,  /*!< 60 MHz  — ESP32-C2 only */
    ESP_FLASH_FREQ_48M,  /*!< 48 MHz  — ESP32-H2 only */
    ESP_FLASH_FREQ_40M,  /*!< 40 MHz  — ESP8266, ESP32, S2, S3, C3, C5, C6 */
    ESP_FLASH_FREQ_30M,  /*!< 30 MHz  — ESP32-C2 only */
    ESP_FLASH_FREQ_26M,  /*!< 26 MHz  — ESP8266, ESP32, S2, S3, P4 */
    ESP_FLASH_FREQ_24M,  /*!< 24 MHz  — ESP32-H2 only */
    ESP_FLASH_FREQ_20M,  /*!< 20 MHz  — ESP8266, ESP32, S2, S3, C2, C3, C5, C6, P4 */
    ESP_FLASH_FREQ_16M,  /*!< 16 MHz  — ESP32-H2 only */
    ESP_FLASH_FREQ_15M,  /*!< 15 MHz  — ESP32-C2 only */
    ESP_FLASH_FREQ_12M,  /*!< 12 MHz  — ESP32-H2 only */
    ESP_FLASH_FREQ_KEEP, /*!< preserve the value already in flash */
} esp_flash_freq_t;

/**
 * @brief Flash chip size.
 */
typedef enum {
    ESP_FLASH_SIZE_256KB,  /*!< 256 KB  — ESP8266 only */
    ESP_FLASH_SIZE_512KB,  /*!< 512 KB  — ESP8266 only */
    ESP_FLASH_SIZE_1MB,    /*!< 1 MB */
    ESP_FLASH_SIZE_2MB,    /*!< 2 MB */
    ESP_FLASH_SIZE_4MB,    /*!< 4 MB */
    ESP_FLASH_SIZE_8MB,    /*!< 8 MB */
    ESP_FLASH_SIZE_16MB,   /*!< 16 MB */
    ESP_FLASH_SIZE_32MB,   /*!< 32 MB  — ESP32-family only */
    ESP_FLASH_SIZE_64MB,   /*!< 64 MB  — ESP32-family only */
    ESP_FLASH_SIZE_128MB,  /*!< 128 MB — ESP32-family only */
    ESP_FLASH_SIZE_DETECT, /*!< auto-detect at boot */
} esp_flash_size_t;

/**
 * @brief Configuration for ELF → flash image conversion.
 *
 * Use @ref ESP_LOADER_ELF_CFG_DEFAULT to initialise this struct, then
 * override individual fields as needed.  All fields use chip-agnostic types;
 * the chip-specific header encoding is handled internally by
 * @ref esp_loader_elf_to_flash_image.
 */
typedef struct {
    esp_flash_mode_t flash_mode;       /*!< flash interface mode */
    esp_flash_freq_t flash_freq;       /*!< flash clock frequency */
    esp_flash_size_t flash_size;       /*!< flash chip size */
    uint16_t         min_chip_rev_full; /*!< minimum silicon revision; 0 = any */
    uint16_t         max_chip_rev_full; /*!< maximum silicon revision; 0xFFFF = any */
    bool             append_sha256;    /*!< append SHA-256 digest over the entire image */
} esp_loader_elf_cfg_t;

#define ESP_LOADER_ELF_CFG_DEFAULT() {          \
    .flash_mode        = ESP_FLASH_MODE_DIO,    \
    .flash_freq        = ESP_FLASH_FREQ_KEEP,   \
    .flash_size        = ESP_FLASH_SIZE_DETECT, \
    .min_chip_rev_full = 0,                     \
    .max_chip_rev_full = 0xFFFF,                \
    .append_sha256     = false,                 \
}

/**
 * @brief Convert an ELF executable to an ESP flash binary image.
 *
 * Produces the same binary as `esptool elf2image`.  All PT_LOAD segments
 * with non-zero file size are included (DROM, DRAM, IRAM, IROM, RTC).
 * Flash-mapped segments (DROM/IROM) are placed at IROM_ALIGN (64 KB)
 * boundaries so the ESP MMU maps them correctly when the image is flashed
 * at any 64 KB-aligned partition offset.
 *
 * The chip-specific encoding of @c flash_freq and @c flash_size is handled
 * internally; the caller sets chip-agnostic enum values and the function
 * returns @c ESP_LOADER_ERROR_INVALID_PARAM if the combination is not
 * supported by the target chip.
 *
 * ### Two-call pattern (no heap required)
 * @code
 *   size_t needed;
 *   esp_loader_elf_to_flash_image(elf, elf_size, chip, cfg, NULL, &needed);
 *   uint8_t *buf = malloc(needed);   // or static/stack buffer
 *   esp_loader_elf_to_flash_image(elf, elf_size, chip, cfg, buf, &needed);
 * @endcode
 *
 * @param elf_data  Pointer to the ELF image in memory.
 * @param elf_size  Size of the ELF image in bytes.
 * @param chip      Target chip (determines header format, address ranges, and
 *                  the chip-specific encoding of flash_freq / flash_size).
 * @param cfg       Image configuration; use @ref ESP_LOADER_ELF_CFG_DEFAULT
 *                  as a starting point.
 * @param out_buf   Destination buffer for the flash image, or NULL for a
 *                  size-query dry run.
 * @param out_size  In: capacity of out_buf (ignored when out_buf is NULL).
 *                  Out: number of bytes written (or required on dry run).
 *
 * @return ESP_LOADER_SUCCESS on success.
 * @return ESP_LOADER_ERROR_INVALID_PARAM if the ELF is malformed, a pointer
 *         argument is NULL, or flash_freq / flash_size is not supported by
 *         the target chip.
 * @return ESP_LOADER_ERROR_UNSUPPORTED_CHIP if chip >= ESP_MAX_CHIP.
 * @return ESP_LOADER_ERROR_IMAGE_SIZE if out_buf != NULL and capacity < required.
 */
esp_loader_error_t esp_loader_elf_to_flash_image(
    const uint8_t              *elf_data,
    size_t                      elf_size,
    target_chip_t               chip,
    const esp_loader_elf_cfg_t *cfg,
    uint8_t                    *out_buf,
    size_t                     *out_size);

#ifdef __cplusplus
}
#endif
