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

#include <stddef.h>
#include <stdint.h>

#include "elf32.h"
#include "esp_loader_error.h"
#include "esp_elf_image_priv.h"

/* -------------------------------------------------------------------------
 * Task 1 — ELF32 parser
 * ---------------------------------------------------------------------- */

esp_loader_error_t elf_validate(const uint8_t *data, size_t size)
{
    /* Need at least the 52-byte ELF header. */
    if (data == NULL || size < sizeof(Elf32_Ehdr)) {
        return ESP_LOADER_ERROR_INVALID_PARAM;
    }

    const Elf32_Ehdr *ehdr = (const Elf32_Ehdr *)data;

    /* Magic bytes. */
    if (ehdr->e_ident[EI_MAG0] != ELF_MAGIC_0 ||
            ehdr->e_ident[EI_MAG1] != ELF_MAGIC_1 ||
            ehdr->e_ident[EI_MAG2] != ELF_MAGIC_2 ||
            ehdr->e_ident[EI_MAG3] != ELF_MAGIC_3) {
        return ESP_LOADER_ERROR_INVALID_PARAM;
    }

    /* Must be 32-bit. */
    if (ehdr->e_ident[EI_CLASS] != ELFCLASS32) {
        return ESP_LOADER_ERROR_INVALID_PARAM;
    }

    /* Must be little-endian. */
    if (ehdr->e_ident[EI_DATA] != ELFDATA2LSB) {
        return ESP_LOADER_ERROR_INVALID_PARAM;
    }

    /* Must be a fully-linked executable. */
    if (ehdr->e_type != ET_EXEC) {
        return ESP_LOADER_ERROR_INVALID_PARAM;
    }

    /* Program header table must fit entirely within the buffer.
     * Check: e_phoff + e_phnum * e_phentsize <= size  (with overflow guard). */
    if (ehdr->e_phentsize < sizeof(Elf32_Phdr)) {
        /* Sanity: entry size must be at least as large as our struct. */
        return ESP_LOADER_ERROR_INVALID_PARAM;
    }

    if (ehdr->e_phnum > 0) {
        /* Overflow-safe: e_phnum and e_phentsize are both uint16_t. */
        uint32_t table_size = (uint32_t)ehdr->e_phnum * (uint32_t)ehdr->e_phentsize;
        if ((uint64_t)ehdr->e_phoff + table_size > size) {
            return ESP_LOADER_ERROR_INVALID_PARAM;
        }
    }

    return ESP_LOADER_SUCCESS;
}

const Elf32_Phdr *elf_get_phdr(const uint8_t *data, size_t size, uint16_t index)
{
    if (data == NULL || size < sizeof(Elf32_Ehdr)) {
        return NULL;
    }

    const Elf32_Ehdr *ehdr = (const Elf32_Ehdr *)data;

    if (index >= ehdr->e_phnum) {
        return NULL;
    }

    /* Bounds check: the entry must lie fully within data[]. */
    uint32_t offset = ehdr->e_phoff + (uint32_t)index * ehdr->e_phentsize;
    if ((uint64_t)offset + sizeof(Elf32_Phdr) > size) {
        return NULL;
    }

    return (const Elf32_Phdr *)(data + offset);
}
