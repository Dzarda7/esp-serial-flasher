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

/* Internal types and function declarations for esp_elf_image.c.
 * Exposed here so host-side unit tests can link against the internals directly
 * without going through the public API. */

#include <stddef.h>
#include "elf32.h"
#include "esp_loader_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Validate that @p data points to a well-formed ELF32 executable.
 *
 * Checks performed:
 *  - Buffer is large enough to hold the ELF header (52 bytes).
 *  - Magic bytes match 0x7F 'E' 'L' 'F'.
 *  - EI_CLASS == ELFCLASS32 (32-bit).
 *  - EI_DATA  == ELFDATA2LSB (little-endian).
 *  - e_type   == ET_EXEC (fully-linked executable).
 *  - The program header table fits within @p size (bounds check).
 *
 * @param data  Pointer to the ELF image bytes.
 * @param size  Number of bytes in @p data.
 *
 * @return ESP_LOADER_SUCCESS            on a valid ELF32 executable.
 * @return ESP_LOADER_ERROR_INVALID_PARAM on any validation failure.
 */
esp_loader_error_t elf_validate(const uint8_t *data, size_t size);

/**
 * @brief Return a pointer to the program header at @p index.
 *
 * The returned pointer points into @p data — no copy is made.  The caller
 * must not write through it.
 *
 * @param data   Pointer to the ELF image bytes (already validated).
 * @param size   Number of bytes in @p data.
 * @param index  Zero-based index into the program header table.
 *
 * @return Pointer to the @p index-th @c Elf32_Phdr, or @c NULL if the entry
 *         would fall outside @p data.
 */
const Elf32_Phdr *elf_get_phdr(const uint8_t *data, size_t size, uint16_t index);

/* -------------------------------------------------------------------------
 * Task 2 — Segment classifier
 * ---------------------------------------------------------------------- */

#include "esp_loader.h"  /* target_chip_t */
#include <stdbool.h>

/**
 * @brief Memory-region types used during ELF→image conversion.
 */
typedef enum {
    ESP_SEG_DROM,      /*!< Flash-mapped read-only data (MMU-cached) */
    ESP_SEG_IROM,      /*!< Flash-mapped code (MMU-cached) */
    ESP_SEG_DRAM,      /*!< Data RAM — goes into image with segment header */
    ESP_SEG_IRAM,      /*!< Instruction RAM — goes into image with segment header */
    ESP_SEG_RTC_DATA,  /*!< RTC slow/fast memory — goes into image with segment header */
    ESP_SEG_UNKNOWN,   /*!< Not mapped / not classifiable */
} esp_seg_type_t;

/**
 * @brief One contiguous virtual-address range belonging to a memory type.
 *
 * @c vaddr_end is an exclusive upper bound (addr < vaddr_end), matching
 * esptool's MAP_START/MAP_END convention.
 *
 * For DROM/IROM segments: flash_addr = vaddr - mmu_offset gives the byte
 * offset from the start of the flash-mapped window.  Set to 0 for RAM types.
 */
typedef struct {
    uint32_t       vaddr_lo;   /*!< First virtual address in the range (inclusive) */
    uint32_t       vaddr_end;  /*!< One past the last virtual address (exclusive)  */
    esp_seg_type_t type;
    uint32_t       mmu_offset; /*!< Subtract from vaddr to get flash-window offset; 0 for RAM */
} esp_seg_range_t;

/**
 * @brief Per-chip metadata needed for the binary image header.
 */
typedef struct {
    uint16_t chip_id;         /*!< IMAGE_CHIP_ID (0xFFFF = ESP8266, no extended header) */
    bool     has_ext_header;  /*!< true for ESP32 and later (32-byte header) */
} esp_chip_info_t;

/**
 * @brief Classify a virtual address for the given chip.
 *
 * Looks up @p vaddr in the chip's address range table and returns the
 * segment type.  If @p flash_addr_out is non-NULL and the result is
 * @c ESP_SEG_DROM or @c ESP_SEG_IROM, the flash-window offset
 * (vaddr − mmu_offset) is written into @p *flash_addr_out.
 *
 * @param chip           Target chip identifier.
 * @param vaddr          Virtual address to classify.
 * @param flash_addr_out Optional output for the flash-window offset.
 *
 * @return The @c esp_seg_type_t for @p vaddr, or @c ESP_SEG_UNKNOWN.
 */
esp_seg_type_t elf_classify_segment(target_chip_t chip, uint32_t vaddr,
                                    uint32_t *flash_addr_out);

/**
 * @brief Return image-header metadata for the given chip.
 *
 * @param chip  Target chip identifier (must be < ESP_MAX_CHIP).
 * @return Pointer to the chip's @c esp_chip_info_t.
 */
const esp_chip_info_t *elf_chip_info(target_chip_t chip);

#ifdef __cplusplus
}
#endif
