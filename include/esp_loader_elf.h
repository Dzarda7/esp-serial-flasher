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
 * @brief Configuration for ELF → flash image conversion.
 */
typedef struct {
    uint8_t  flash_mode;        /*!< 0=QIO 1=QOUT 2=DIO 3=DOUT */
    uint8_t  flash_freq;        /*!< lower nibble of header byte 3; chip-family specific encoding */
    uint8_t  flash_size;        /*!< upper nibble of header byte 3 */
    uint16_t min_chip_rev_full; /*!< minimum silicon revision; 0 = any */
    uint16_t max_chip_rev_full; /*!< maximum silicon revision; 0xFFFF = any */
    bool     append_sha256;     /*!< append SHA-256 digest over the entire image */
} esp_loader_elf_cfg_t;

#define ESP_LOADER_ELF_CFG_DEFAULT() {  \
    .flash_mode        = 2,             \
    .flash_freq        = 0x0F,          \
    .flash_size        = 0x0F,          \
    .min_chip_rev_full = 0,             \
    .max_chip_rev_full = 0xFFFF,        \
    .append_sha256     = false,         \
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
 * @param chip      Target chip (determines header format and address ranges).
 * @param cfg       Image configuration; use ESP_LOADER_ELF_CFG_DEFAULT() as a
 *                  starting point.
 * @param out_buf   Destination buffer for the flash image, or NULL for a
 *                  size-query dry run.
 * @param out_size  In: capacity of out_buf (ignored when out_buf is NULL).
 *                  Out: number of bytes written (or required on dry run).
 *
 * @return ESP_LOADER_SUCCESS on success.
 * @return ESP_LOADER_ERROR_INVALID_PARAM if the ELF is malformed or NULL.
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
