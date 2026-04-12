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

#ifdef __cplusplus
}
#endif
