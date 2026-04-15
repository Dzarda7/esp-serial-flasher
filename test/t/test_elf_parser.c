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

/*
 * Host-side unit tests for the ELF32 parser (elf_validate, elf_get_phdr),
 * segment classifier (elf_classify_segment, elf_chip_info), image size
 * calculator, and image write pass.
 *
 * No hardware needed — pure C logic, TAP output.
 *
 * Real-ELF tests look for:
 *   TEST_ELF  — path to an ESP32 hello_world.elf (set by the .t driver)
 * They are skipped when the file is absent.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "elf32.h"
#include "esp_loader.h"
#include "esp_loader_error.h"
#include "esp_loader_elf.h"
#include "esp_elf_image_priv.h"
#include "tap.h"

/* -------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------- */

/* Build a minimal, valid ELF32 executable in memory with `phnum` PT_LOAD
 * segments.  Each segment has 256 bytes of data placed after the program
 * header table so that bounds checks in the converter pass correctly.
 * Caller must free() the returned buffer. */
static uint8_t *make_elf(uint16_t phnum, size_t *out_size)
{
    const size_t DATA_PER_SEG  = 256;
    size_t phdr_table_size     = (size_t)phnum * sizeof(Elf32_Phdr);
    size_t data_base           = sizeof(Elf32_Ehdr) + phdr_table_size;
    size_t total               = data_base + (size_t)phnum * DATA_PER_SEG;

    uint8_t *buf = (uint8_t *)calloc(1, total);
    if (!buf) {
        return NULL;
    }
    *out_size = total;

    Elf32_Ehdr *ehdr          = (Elf32_Ehdr *)buf;
    ehdr->e_ident[EI_MAG0]   = ELF_MAGIC_0;
    ehdr->e_ident[EI_MAG1]   = ELF_MAGIC_1;
    ehdr->e_ident[EI_MAG2]   = ELF_MAGIC_2;
    ehdr->e_ident[EI_MAG3]   = ELF_MAGIC_3;
    ehdr->e_ident[EI_CLASS]  = ELFCLASS32;
    ehdr->e_ident[EI_DATA]   = ELFDATA2LSB;
    ehdr->e_type              = ET_EXEC;
    ehdr->e_machine           = EM_XTENSA;
    ehdr->e_version           = 1;
    ehdr->e_entry             = 0x40080000u;
    ehdr->e_ehsize            = (uint16_t)sizeof(Elf32_Ehdr);
    ehdr->e_phentsize         = (uint16_t)sizeof(Elf32_Phdr);
    ehdr->e_phnum             = phnum;
    ehdr->e_phoff             = (phnum > 0) ? (uint32_t)sizeof(Elf32_Ehdr) : 0u;

    for (uint16_t i = 0; i < phnum; i++) {
        Elf32_Phdr *phdr = (Elf32_Phdr *)(buf + sizeof(Elf32_Ehdr)
                                          + (size_t)i * sizeof(Elf32_Phdr));
        phdr->p_type   = PT_LOAD;
        phdr->p_vaddr  = 0x40080000u + (uint32_t)i * 0x1000u;
        phdr->p_filesz = (uint32_t)DATA_PER_SEG;
        phdr->p_memsz  = (uint32_t)DATA_PER_SEG;
        phdr->p_flags  = PF_R | PF_X;
        phdr->p_offset = (uint32_t)(data_base + (size_t)i * DATA_PER_SEG);
    }

    return buf;
}

/* Load a binary file into a malloc'd buffer.  Returns NULL on failure. */
static uint8_t *load_file(const char *path, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long sz = ftell(f);
    if (sz <= 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        fclose(f);
        free(buf);
        return NULL;
    }
    fclose(f);
    *out_size = (size_t)sz;
    return buf;
}

/* Run the two-call sequence and return a malloc'd image.  Returns NULL on
 * failure.  Caller must free() the buffer. */
static uint8_t *generate_image(const uint8_t *elf, size_t elf_sz,
                               target_chip_t chip, bool sha, size_t *out_sz)
{
    esp_loader_elf_cfg_t cfg = ESP_LOADER_ELF_CFG_DEFAULT();
    cfg.append_sha256 = sha;

    size_t sz = 0;
    if (esp_loader_elf_to_flash_image(elf, elf_sz, chip, &cfg,
                                      NULL, &sz) != ESP_LOADER_SUCCESS) {
        return NULL;
    }

    uint8_t *buf = (uint8_t *)calloc(1, sz);
    if (!buf) {
        return NULL;
    }

    if (esp_loader_elf_to_flash_image(elf, elf_sz, chip, &cfg,
                                      buf, &sz) != ESP_LOADER_SUCCESS) {
        free(buf);
        return NULL;
    }

    *out_sz = sz;
    return buf;
}

/* Dry-run: return the reported image size, or 0 on error. */
static size_t dry_run_size(const uint8_t *elf, size_t elf_sz,
                           target_chip_t chip, bool sha)
{
    esp_loader_elf_cfg_t cfg = ESP_LOADER_ELF_CFG_DEFAULT();
    cfg.append_sha256 = sha;
    size_t sz = 0;
    if (esp_loader_elf_to_flash_image(elf, elf_sz, chip,
                                      &cfg, NULL, &sz) != ESP_LOADER_SUCCESS) {
        return 0;
    }
    return sz;
}

/* Expected image size for make_elf(n) on ESP32 (all-RAM, no flash segs).
 *   header   = 24 bytes (ESP32 has a 24-byte header)
 *   per seg  = 8-byte descriptor + 256 bytes data
 *   pad      = (15 - (total_before % 16)) % 16 + 1  (16-byte alignment, min 1) */
static size_t expected_all_ram_size(size_t n_segs)
{
    const size_t HDR  = 24;
    const size_t SEGD = 8 + 256;
    size_t before = HDR + n_segs * SEGD;
    size_t pad    = (15u - (before % 16u)) % 16u + 1u;
    return before + pad;
}

/* -------------------------------------------------------------------------
 * elf_validate tests
 * ---------------------------------------------------------------------- */

static void test_elf_validate(void)
{
    size_t sz = 0;
    uint8_t *buf;

    /* Minimal valid ELF with no segments */
    buf = make_elf(0, &sz);
    tap_check(elf_validate(buf, sz) == ESP_LOADER_SUCCESS);
    tap_done("elf_validate: minimal valid ELF (no segments)");
    free(buf);

    /* Valid ELF with two PT_LOAD segments */
    buf = make_elf(2, &sz);
    tap_check(elf_validate(buf, sz) == ESP_LOADER_SUCCESS);
    tap_done("elf_validate: valid ELF with two PT_LOAD segments");
    free(buf);

    /* NULL pointer */
    tap_check(elf_validate(NULL, 64) == ESP_LOADER_ERROR_INVALID_PARAM);
    tap_done("elf_validate: NULL pointer returns INVALID_PARAM");

    /* Empty buffer */
    {
        uint8_t zero[1] = {0};
        tap_check(elf_validate(zero, 0) == ESP_LOADER_ERROR_INVALID_PARAM);
        tap_done("elf_validate: empty buffer returns INVALID_PARAM");
    }

    /* Truncated to 51 bytes (one byte short of ELF header) */
    buf = make_elf(0, &sz);
    tap_check(elf_validate(buf, sizeof(Elf32_Ehdr) - 1)
              == ESP_LOADER_ERROR_INVALID_PARAM);
    tap_done("elf_validate: truncated to 51 bytes returns INVALID_PARAM");
    free(buf);

    /* Wrong magic */
    buf = make_elf(0, &sz);
    buf[0] = 0x00;
    tap_check(elf_validate(buf, sz) == ESP_LOADER_ERROR_INVALID_PARAM);
    tap_done("elf_validate: wrong magic returns INVALID_PARAM");
    free(buf);

    /* ELFCLASS64 */
    buf = make_elf(0, &sz);
    buf[EI_CLASS] = 2;
    tap_check(elf_validate(buf, sz) == ESP_LOADER_ERROR_INVALID_PARAM);
    tap_done("elf_validate: ELFCLASS64 returns INVALID_PARAM");
    free(buf);

    /* Big-endian */
    buf = make_elf(0, &sz);
    buf[EI_DATA] = 2;
    tap_check(elf_validate(buf, sz) == ESP_LOADER_ERROR_INVALID_PARAM);
    tap_done("elf_validate: big-endian returns INVALID_PARAM");
    free(buf);

    /* ET_DYN (non-EXEC) */
    buf = make_elf(0, &sz);
    ((Elf32_Ehdr *)buf)->e_type = 3; /* ET_DYN */
    tap_check(elf_validate(buf, sz) == ESP_LOADER_ERROR_INVALID_PARAM);
    tap_done("elf_validate: ET_DYN (non-EXEC) returns INVALID_PARAM");
    free(buf);

    /* Truncated program header table (room for 1 of 2 phdrs) */
    buf = make_elf(2, &sz);
    tap_check(elf_validate(buf, sizeof(Elf32_Ehdr) + sizeof(Elf32_Phdr))
              == ESP_LOADER_ERROR_INVALID_PARAM);
    tap_done("elf_validate: truncated program header table returns INVALID_PARAM");
    free(buf);
}

/* -------------------------------------------------------------------------
 * elf_get_phdr tests
 * ---------------------------------------------------------------------- */

static void test_elf_get_phdr(void)
{
    size_t sz = 0;
    uint8_t *buf;

    /* Index 0 of 1-segment ELF */
    buf = make_elf(1, &sz);
    {
        const Elf32_Phdr *phdr = elf_get_phdr(buf, sz, 0);
        tap_check(phdr != NULL);
        tap_check(phdr != NULL && phdr->p_type == PT_LOAD);
        tap_done("elf_get_phdr: index 0 returns non-NULL for 1-segment ELF");
    }
    free(buf);

    /* All indices 0..N-1 valid for 3-segment ELF */
    buf = make_elf(3, &sz);
    {
        bool all_ok = true;
        for (uint16_t i = 0; i < 3; i++) {
            const Elf32_Phdr *phdr = elf_get_phdr(buf, sz, i);
            if (!phdr || phdr->p_vaddr != (0x40080000u + (uint32_t)i * 0x1000u)) {
                all_ok = false;
            }
        }
        tap_check(all_ok);
        tap_done("elf_get_phdr: indices 0..N-1 all valid for N-segment ELF");
    }
    free(buf);

    /* Index == e_phnum returns NULL */
    buf = make_elf(2, &sz);
    tap_check(elf_get_phdr(buf, sz, 2) == NULL);
    tap_done("elf_get_phdr: index == e_phnum returns NULL");
    free(buf);

    /* Index > e_phnum returns NULL */
    buf = make_elf(2, &sz);
    tap_check(elf_get_phdr(buf, sz, 100) == NULL);
    tap_done("elf_get_phdr: index > e_phnum returns NULL");
    free(buf);

    /* NULL data returns NULL */
    tap_check(elf_get_phdr(NULL, 128, 0) == NULL);
    tap_done("elf_get_phdr: NULL data returns NULL");
}

/* -------------------------------------------------------------------------
 * elf_classify_segment and elf_chip_info tests
 * ---------------------------------------------------------------------- */

static void test_classify(void)
{
    uint32_t faddr;

    /* ESP8266 */
    faddr = 0xDEADu;
    tap_check(elf_classify_segment(ESP8266_CHIP, 0x40200000u, &faddr) == ESP_SEG_IROM);
    tap_check(faddr == 0u);
    tap_done("classify ESP8266: IROM base (0x40200000) maps to flash offset 0");

    faddr = 0u;
    tap_check(elf_classify_segment(ESP8266_CHIP, 0x40250000u, &faddr) == ESP_SEG_IROM);
    tap_check(faddr == 0x50000u);
    tap_done("classify ESP8266: IROM mid-range (0x40250000) maps to flash offset 0x50000");

    tap_check(elf_classify_segment(ESP8266_CHIP, 0x3FFF0000u, NULL) == ESP_SEG_DRAM);
    tap_done("classify ESP8266: DRAM (0x3FFF0000)");

    tap_check(elf_classify_segment(ESP8266_CHIP, 0x40100000u, NULL) == ESP_SEG_IRAM);
    tap_done("classify ESP8266: IRAM (0x40100000)");

    tap_check(elf_classify_segment(ESP8266_CHIP, 0x00000000u, NULL) == ESP_SEG_UNKNOWN);
    tap_done("classify ESP8266: unknown (0x00000000)");

    /* RAM segments must NOT write flash_addr_out */
    faddr = 0xDEADBEEFu;
    tap_check(elf_classify_segment(ESP8266_CHIP, 0x3FFF0000u, &faddr) == ESP_SEG_DRAM);
    tap_check(faddr == 0xDEADBEEFu);
    tap_done("classify ESP8266: DRAM does not overwrite flash_addr_out");

    /* ESP32 */
    faddr = 0xDEADu;
    tap_check(elf_classify_segment(ESP32_CHIP, 0x3F500000u, &faddr) == ESP_SEG_DROM);
    tap_check(faddr == 0x100000u);
    tap_done("classify ESP32: DROM (0x3F500000) maps to flash offset 0x100000");

    faddr = 0xDEADu;
    tap_check(elf_classify_segment(ESP32_CHIP, 0x400E0000u, &faddr) == ESP_SEG_IROM);
    tap_check(faddr == 0x10000u);
    tap_done("classify ESP32: IROM (0x400E0000) maps to flash offset 0x10000");

    tap_check(elf_classify_segment(ESP32_CHIP, 0x3FFBe000u, NULL) == ESP_SEG_DRAM);
    tap_done("classify ESP32: DRAM (0x3FFBE000)");

    tap_check(elf_classify_segment(ESP32_CHIP, 0x40090000u, NULL) == ESP_SEG_IRAM);
    tap_done("classify ESP32: IRAM (0x40090000)");

    tap_check(elf_classify_segment(ESP32_CHIP, 0x50000010u, NULL) == ESP_SEG_RTC_DATA);
    tap_done("classify ESP32: RTC_DATA (0x50000010)");

    tap_check(elf_classify_segment(ESP32_CHIP, 0x3F3FFFFFu, NULL) == ESP_SEG_UNKNOWN);
    tap_done("classify ESP32: address just below DROM returns UNKNOWN");

    tap_check(elf_classify_segment(ESP32_CHIP, 0x40400000u, NULL) == ESP_SEG_UNKNOWN);
    tap_done("classify ESP32: IROM MAP_END is exclusive (0x40400000)");

    /* ESP32-S2 */
    tap_check(elf_classify_segment(ESP32S2_CHIP, 0x3F100000u, NULL) == ESP_SEG_DROM);
    tap_done("classify ESP32-S2: DROM (0x3F100000)");
    tap_check(elf_classify_segment(ESP32S2_CHIP, 0x40100000u, NULL) == ESP_SEG_IROM);
    tap_done("classify ESP32-S2: IROM (0x40100000)");
    tap_check(elf_classify_segment(ESP32S2_CHIP, 0x3FFC0000u, NULL) == ESP_SEG_DRAM);
    tap_done("classify ESP32-S2: DRAM (0x3FFC0000)");
    tap_check(elf_classify_segment(ESP32S2_CHIP, 0x40040000u, NULL) == ESP_SEG_IRAM);
    tap_done("classify ESP32-S2: IRAM (0x40040000)");

    /* ESP32-C3 */
    tap_check(elf_classify_segment(ESP32C3_CHIP, 0x3C400000u, NULL) == ESP_SEG_DROM);
    tap_done("classify ESP32-C3: DROM (0x3C400000)");
    tap_check(elf_classify_segment(ESP32C3_CHIP, 0x42400000u, NULL) == ESP_SEG_IROM);
    tap_done("classify ESP32-C3: IROM (0x42400000)");
    tap_check(elf_classify_segment(ESP32C3_CHIP, 0x3FC90000u, NULL) == ESP_SEG_DRAM);
    tap_done("classify ESP32-C3: DRAM (0x3FC90000)");
    tap_check(elf_classify_segment(ESP32C3_CHIP, 0x40380000u, NULL) == ESP_SEG_IRAM);
    tap_done("classify ESP32-C3: IRAM (0x40380000)");

    /* ESP32-S3 */
    tap_check(elf_classify_segment(ESP32S3_CHIP, 0x3C800000u, NULL) == ESP_SEG_DROM);
    tap_done("classify ESP32-S3: DROM (0x3C800000)");
    tap_check(elf_classify_segment(ESP32S3_CHIP, 0x42400000u, NULL) == ESP_SEG_IROM);
    tap_done("classify ESP32-S3: IROM (0x42400000)");
    tap_check(elf_classify_segment(ESP32S3_CHIP, 0x3FC90000u, NULL) == ESP_SEG_DRAM);
    tap_done("classify ESP32-S3: DRAM (0x3FC90000)");
    tap_check(elf_classify_segment(ESP32S3_CHIP, 0x40380000u, NULL) == ESP_SEG_IRAM);
    tap_done("classify ESP32-S3: IRAM (0x40380000)");

    /* ESP32-C6 */
    tap_check(elf_classify_segment(ESP32C6_CHIP, 0x42400000u, NULL) == ESP_SEG_IROM);
    tap_done("classify ESP32-C6: IROM (0x42400000)");
    tap_check(elf_classify_segment(ESP32C6_CHIP, 0x42900000u, NULL) == ESP_SEG_DROM);
    tap_done("classify ESP32-C6: DROM (0x42900000)");
    tap_check(elf_classify_segment(ESP32C6_CHIP, 0x40840000u, NULL) == ESP_SEG_DRAM);
    tap_done("classify ESP32-C6: DRAM (0x40840000)");

    /* ESP32-H2 */
    tap_check(elf_classify_segment(ESP32H2_CHIP, 0x42400000u, NULL) == ESP_SEG_IROM);
    tap_done("classify ESP32-H2: IROM (0x42400000)");
    tap_check(elf_classify_segment(ESP32H2_CHIP, 0x42900000u, NULL) == ESP_SEG_DROM);
    tap_done("classify ESP32-H2: DROM (0x42900000)");

    /* ESP32-C5 */
    tap_check(elf_classify_segment(ESP32C5_CHIP, 0x43000000u, NULL) == ESP_SEG_DROM);
    tap_done("classify ESP32-C5: DROM window (0x43000000)");
    tap_check(elf_classify_segment(ESP32C5_CHIP, 0x40820000u, NULL) == ESP_SEG_DRAM);
    tap_done("classify ESP32-C5: DRAM window (0x40820000)");

    /* ESP32-P4 */
    tap_check(elf_classify_segment(ESP32P4_CHIP, 0x44000000u, NULL) == ESP_SEG_DROM);
    tap_done("classify ESP32-P4: DROM window (0x44000000)");
    tap_check(elf_classify_segment(ESP32P4_CHIP, 0x4FF10000u, NULL) == ESP_SEG_DRAM);
    tap_done("classify ESP32-P4: DRAM (0x4FF10000)");

    /* Out-of-range chip */
    tap_check(elf_classify_segment(ESP_MAX_CHIP, 0x42000000u, NULL) == ESP_SEG_UNKNOWN);
    tap_check(elf_classify_segment((target_chip_t)99, 0x42000000u, NULL) == ESP_SEG_UNKNOWN);
    tap_done("classify: out-of-range chip returns UNKNOWN");

    /* NULL flash_addr_out is safe for flash segments */
    tap_check(elf_classify_segment(ESP32_CHIP, 0x3F500000u, NULL) == ESP_SEG_DROM);
    tap_check(elf_classify_segment(ESP32_CHIP, 0x400E0000u, NULL) == ESP_SEG_IROM);
    tap_done("classify: NULL flash_addr_out is safe for flash segments");
}

static void test_chip_info(void)
{
    const esp_chip_info_t *info;

    info = elf_chip_info(ESP8266_CHIP);
    tap_check(info != NULL);
    tap_check(info != NULL && info->has_ext_header == false);
    tap_done("elf_chip_info: ESP8266 has no extended header");

    info = elf_chip_info(ESP32_CHIP);
    tap_check(info != NULL);
    tap_check(info != NULL && info->chip_id == 0x0000u);
    tap_check(info != NULL && info->has_ext_header == true);
    tap_done("elf_chip_info: ESP32 chip_id == 0x0000, has ext header");

    tap_check(elf_chip_info(ESP32S2_CHIP) != NULL &&
              elf_chip_info(ESP32S2_CHIP)->chip_id == 0x0002u);
    tap_check(elf_chip_info(ESP32C3_CHIP) != NULL &&
              elf_chip_info(ESP32C3_CHIP)->chip_id == 0x0005u);
    tap_check(elf_chip_info(ESP32S3_CHIP) != NULL &&
              elf_chip_info(ESP32S3_CHIP)->chip_id == 0x0009u);
    tap_check(elf_chip_info(ESP32C2_CHIP) != NULL &&
              elf_chip_info(ESP32C2_CHIP)->chip_id == 0x000Cu);
    tap_check(elf_chip_info(ESP32C6_CHIP) != NULL &&
              elf_chip_info(ESP32C6_CHIP)->chip_id == 0x000Du);
    tap_check(elf_chip_info(ESP32H2_CHIP) != NULL &&
              elf_chip_info(ESP32H2_CHIP)->chip_id == 0x0010u);
    tap_check(elf_chip_info(ESP32C5_CHIP) != NULL &&
              elf_chip_info(ESP32C5_CHIP)->chip_id == 0x0017u);
    tap_check(elf_chip_info(ESP32P4_CHIP) != NULL &&
              elf_chip_info(ESP32P4_CHIP)->chip_id == 0x0012u);
    tap_done("elf_chip_info: chip_ids match esptool IMAGE_CHIP_ID");

    tap_check(elf_chip_info(ESP_MAX_CHIP) == NULL);
    tap_done("elf_chip_info: out-of-range chip returns NULL");
}

/* -------------------------------------------------------------------------
 * Image size calculator (dry-run pass)
 * ---------------------------------------------------------------------- */

static void test_size_calc(void)
{
    size_t sz = 0;
    uint8_t *buf;

    /* NULL elf_data */
    {
        esp_loader_elf_cfg_t cfg = ESP_LOADER_ELF_CFG_DEFAULT();
        size_t out = 0;
        tap_check(esp_loader_elf_to_flash_image(NULL, 64, ESP32_CHIP,
                                                &cfg, NULL, &out)
                  == ESP_LOADER_ERROR_INVALID_PARAM);
        tap_done("size_calc: NULL elf_data returns INVALID_PARAM");
    }

    /* NULL cfg */
    buf = make_elf(0, &sz);
    {
        size_t out = 0;
        tap_check(esp_loader_elf_to_flash_image(buf, sz, ESP32_CHIP,
                                                NULL, NULL, &out)
                  == ESP_LOADER_ERROR_INVALID_PARAM);
        tap_done("size_calc: NULL cfg returns INVALID_PARAM");
    }
    free(buf);

    /* NULL out_size */
    buf = make_elf(0, &sz);
    {
        esp_loader_elf_cfg_t cfg = ESP_LOADER_ELF_CFG_DEFAULT();
        tap_check(esp_loader_elf_to_flash_image(buf, sz, ESP32_CHIP,
                                                &cfg, NULL, NULL)
                  == ESP_LOADER_ERROR_INVALID_PARAM);
        tap_done("size_calc: NULL out_size returns INVALID_PARAM");
    }
    free(buf);

    /* Unsupported chip */
    buf = make_elf(0, &sz);
    {
        esp_loader_elf_cfg_t cfg = ESP_LOADER_ELF_CFG_DEFAULT();
        size_t out = 0;
        tap_check(esp_loader_elf_to_flash_image(buf, sz, ESP_MAX_CHIP,
                                                &cfg, NULL, &out)
                  == ESP_LOADER_ERROR_UNSUPPORTED_CHIP);
        tap_done("size_calc: unsupported chip returns UNSUPPORTED_CHIP");
    }
    free(buf);

    /* Malformed ELF */
    {
        esp_loader_elf_cfg_t cfg = ESP_LOADER_ELF_CFG_DEFAULT();
        uint8_t bad[4] = {0};
        size_t out = 0;
        tap_check(esp_loader_elf_to_flash_image(bad, sizeof(bad), ESP32_CHIP,
                                                &cfg, NULL, &out)
                  == ESP_LOADER_ERROR_INVALID_PARAM);
        tap_done("size_calc: malformed ELF returns INVALID_PARAM");
    }

    /* Determinism: two calls with same ELF return same size */
    buf = make_elf(2, &sz);
    {
        size_t sz1 = dry_run_size(buf, sz, ESP32_CHIP, false);
        size_t sz2 = dry_run_size(buf, sz, ESP32_CHIP, false);
        tap_check(sz1 == sz2);
        tap_check(sz1 > 0);
        tap_done("size_calc: two calls with the same ELF return the same size");
    }
    free(buf);

    /* 0-segment synthetic ELF */
    buf = make_elf(0, &sz);
    tap_check(dry_run_size(buf, sz, ESP32_CHIP, false) == expected_all_ram_size(0));
    tap_done("size_calc: synthetic 0-segment ESP32 ELF");
    free(buf);

    /* 1-segment synthetic ELF */
    buf = make_elf(1, &sz);
    tap_check(dry_run_size(buf, sz, ESP32_CHIP, false) == expected_all_ram_size(1));
    tap_done("size_calc: synthetic 1-segment ESP32 ELF");
    free(buf);

    /* 3-segment synthetic ELF */
    buf = make_elf(3, &sz);
    tap_check(dry_run_size(buf, sz, ESP32_CHIP, false) == expected_all_ram_size(3));
    tap_done("size_calc: synthetic 3-segment ESP32 ELF");
    free(buf);

    /* append_sha256 adds exactly 32 bytes */
    buf = make_elf(2, &sz);
    {
        size_t without = dry_run_size(buf, sz, ESP32_CHIP, false);
        size_t with    = dry_run_size(buf, sz, ESP32_CHIP, true);
        tap_check(with == without + 32u);
        tap_done("size_calc: append_sha256 adds exactly 32 bytes");
    }
    free(buf);

    /* ELF truncated by 1 byte returns INVALID_PARAM */
    buf = make_elf(0, &sz);
    {
        esp_loader_elf_cfg_t cfg = ESP_LOADER_ELF_CFG_DEFAULT();
        size_t out = 0;
        tap_check(esp_loader_elf_to_flash_image(buf, sz - 1, ESP32_CHIP,
                                                &cfg, NULL, &out)
                  == ESP_LOADER_ERROR_INVALID_PARAM);
        tap_done("size_calc: ELF truncated by 1 byte returns INVALID_PARAM");
    }
    free(buf);
}

/* -------------------------------------------------------------------------
 * Image write pass
 * ---------------------------------------------------------------------- */

static void test_write(void)
{
    size_t sz = 0;
    uint8_t *elf;
    uint8_t *img;
    size_t img_sz = 0;

    /* Magic byte is 0xE9 */
    elf = make_elf(1, &sz);
    img = generate_image(elf, sz, ESP32_CHIP, false, &img_sz);
    tap_check(img != NULL);
    tap_check(img != NULL && img[0] == 0xE9u);
    tap_done("write: magic byte is 0xE9");
    free(img);
    free(elf);

    /* Segment count in header matches layout */
    elf = make_elf(1, &sz);
    img = generate_image(elf, sz, ESP32_CHIP, false, &img_sz);
    tap_check(img != NULL && img[1] == 1u);
    free(img);
    free(elf);
    elf = make_elf(3, &sz);
    img = generate_image(elf, sz, ESP32_CHIP, false, &img_sz);
    tap_check(img != NULL && img[1] == 3u);
    tap_done("write: segment count in header matches layout");
    free(img);
    free(elf);

    /* Extended header chip_id for ESP32 */
    elf = make_elf(0, &sz);
    img = generate_image(elf, sz, ESP32_CHIP, false, &img_sz);
    tap_check(img != NULL && img[8] == 0xEEu);
    {
        uint16_t chip_id = (img != NULL)
                           ? ((uint16_t)img[11] | ((uint16_t)img[12] << 8u))
                           : 0xFFFFu;
        tap_check(chip_id == 0x0000u);
    }
    tap_done("write: extended header chip_id for ESP32");
    free(img);
    free(elf);

    /* append_digest byte reflects cfg */
    elf = make_elf(0, &sz);
    {
        uint8_t *img_no  = generate_image(elf, sz, ESP32_CHIP, false, &img_sz);
        size_t   no_sz   = img_sz;
        uint8_t *img_sha = generate_image(elf, sz, ESP32_CHIP, true,  &img_sz);
        /* append_digest is at offset 8+15=23 in the 32-byte extended header */
        tap_check(img_no  != NULL && img_no[23]  == 0u);
        tap_check(img_sha != NULL && img_sha[23] == 1u);
        tap_done("write: append_digest byte reflects cfg");
        (void)no_sz;
        free(img_no);
        free(img_sha);
    }
    free(elf);

    /* Segment descriptor load_addr matches ELF vaddr (1-seg) */
    elf = make_elf(1, &sz);
    img = generate_image(elf, sz, ESP32_CHIP, false, &img_sz);
    {
        /* First descriptor at offset 24 (ESP32 header size). */
        uint32_t load_addr = (img != NULL)
                             ? ((uint32_t)img[24]
                                | ((uint32_t)img[25] << 8u)
                                | ((uint32_t)img[26] << 16u)
                                | ((uint32_t)img[27] << 24u))
                             : 0u;
        tap_check(load_addr == 0x40080000u);
        tap_done("write: segment descriptor load_addr matches ELF vaddr");
    }
    free(img);
    free(elf);

    /* Segment data matches ELF bytes (all zeros from make_elf) */
    elf = make_elf(1, &sz);
    img = generate_image(elf, sz, ESP32_CHIP, false, &img_sz);
    {
        /* Data starts at offset 24 (header) + 8 (descriptor) = 32. */
        bool data_ok = true;
        if (img) {
            for (size_t i = 32; i < 32 + 256; i++) {
                if (img[i] != 0) {
                    data_ok = false;
                    break;
                }
            }
        } else {
            data_ok = false;
        }
        tap_check(data_ok);
        tap_done("write: segment data matches ELF bytes");
    }
    free(img);
    free(elf);

    /* Checksum byte is correct for 0-segment ELF */
    elf = make_elf(0, &sz);
    img = generate_image(elf, sz, ESP32_CHIP, false, &img_sz);
    {
        /* Expected checksum: 0xEF XOR nothing = 0xEF.
         * With 0 segments, pad = 8 bytes, so last byte is img[31]. */
        tap_check(img != NULL && img[img_sz - 1] == 0xEFu);
        tap_done("write: checksum byte is correct (0-seg)");
    }
    free(img);
    free(elf);

    /* SHA-256 appended correctly */
    elf = make_elf(2, &sz);
    {
        size_t sha_sz = 0, no_sz = 0;
        uint8_t *img_sha = generate_image(elf, sz, ESP32_CHIP, true,  &sha_sz);
        uint8_t *img_no  = generate_image(elf, sz, ESP32_CHIP, false, &no_sz);
        tap_check(img_sha != NULL && img_no != NULL);
        tap_check(sha_sz == no_sz + 32u);
        /* SHA tail must not be all-zeros */
        bool has_nonzero = false;
        if (img_sha) {
            for (size_t i = 0; i < 32; i++) {
                if (img_sha[no_sz + i] != 0) {
                    has_nonzero = true;
                    break;
                }
            }
        }
        tap_check(has_nonzero);
        /* Two independent calls must produce identical output */
        size_t sha_sz2 = 0;
        uint8_t *img_sha2 = generate_image(elf, sz, ESP32_CHIP, true, &sha_sz2);
        tap_check(img_sha2 != NULL && sha_sz2 == sha_sz);
        tap_check(img_sha && img_sha2 && memcmp(img_sha, img_sha2, sha_sz) == 0);
        tap_done("write: SHA-256 appended correctly and deterministically");
        free(img_sha);
        free(img_no);
        free(img_sha2);
    }
    free(elf);
}

/* -------------------------------------------------------------------------
 * Real ELF tests (optional — skipped if TEST_ELF not set or file absent)
 * ---------------------------------------------------------------------- */

static void test_real_elf(void)
{
    const char *elf_path = getenv("TEST_ELF");
    if (!elf_path) {
        tap_skip("real ELF: TEST_ELF not set");
        tap_skip("real ELF: size with SHA == size without SHA + 32");
        tap_skip("real ELF: image starts with 0xE9");
        tap_skip("real ELF: dry-run is deterministic");
        return;
    }

    size_t elf_sz = 0;
    uint8_t *elf = load_file(elf_path, &elf_sz);
    if (!elf) {
        printf("# TEST_ELF=%s not found or unreadable\n", elf_path);
        tap_skip("real ELF: TEST_ELF not set");
        tap_skip("real ELF: size with SHA == size without SHA + 32");
        tap_skip("real ELF: image starts with 0xE9");
        tap_skip("real ELF: dry-run is deterministic");
        return;
    }

    /* Size query succeeds and returns a plausible value */
    size_t sz_no  = dry_run_size(elf, elf_sz, ESP32_CHIP, false);
    size_t sz_sha = dry_run_size(elf, elf_sz, ESP32_CHIP, true);
    tap_check(sz_no > 0);
    tap_done("real ELF: dry-run returns non-zero size");

    tap_check(sz_sha == sz_no + 32u);
    tap_done("real ELF: size with SHA == size without SHA + 32");

    /* Generate image and check magic */
    size_t img_sz = 0;
    uint8_t *img = generate_image(elf, elf_sz, ESP32_CHIP, false, &img_sz);
    tap_check(img != NULL && img_sz == sz_no);
    tap_check(img != NULL && img[0] == 0xE9u);
    tap_done("real ELF: image starts with magic 0xE9");
    free(img);

    /* Determinism: two independent calls produce the same output */
    size_t img_sz2 = 0;
    uint8_t *img1 = generate_image(elf, elf_sz, ESP32_CHIP, false, &img_sz);
    uint8_t *img2 = generate_image(elf, elf_sz, ESP32_CHIP, false, &img_sz2);
    tap_check(img1 && img2 && img_sz == img_sz2
              && memcmp(img1, img2, img_sz) == 0);
    tap_done("real ELF: dry-run is deterministic");
    free(img1);
    free(img2);

    free(elf);
}

/* -------------------------------------------------------------------------
 * Entry point
 * ---------------------------------------------------------------------- */

int main(void)
{
    test_elf_validate();
    test_elf_get_phdr();
    test_classify();
    test_chip_info();
    test_size_calc();
    test_write();
    test_real_elf();
    return tap_result();
}
