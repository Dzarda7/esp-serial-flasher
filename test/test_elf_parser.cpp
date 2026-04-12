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

/* Host-side unit tests for Task 1: ELF32 parser (elf_validate, elf_get_phdr).
 * No hardware needed — pure C logic exercised through Catch2. */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include <cstring>
#include <cstdint>
#include <vector>
#include <fstream>
#include <iterator>

extern "C" {
#include "elf32.h"
#include "esp_loader_error.h"
#include "esp_loader_elf.h"
#include "esp_elf_image_priv.h"
}

/* ---------------------------------------------------------------------------
 * Helpers to build a minimal, valid ELF32 in memory
 * ------------------------------------------------------------------------ */

static void fill_elf_header(Elf32_Ehdr *ehdr, uint16_t phnum = 0)
{
    memset(ehdr, 0, sizeof(*ehdr));
    ehdr->e_ident[EI_MAG0]  = ELF_MAGIC_0;
    ehdr->e_ident[EI_MAG1]  = ELF_MAGIC_1;
    ehdr->e_ident[EI_MAG2]  = ELF_MAGIC_2;
    ehdr->e_ident[EI_MAG3]  = ELF_MAGIC_3;
    ehdr->e_ident[EI_CLASS] = ELFCLASS32;
    ehdr->e_ident[EI_DATA]  = ELFDATA2LSB;
    ehdr->e_type             = ET_EXEC;
    ehdr->e_machine          = EM_XTENSA;
    ehdr->e_version          = 1;
    ehdr->e_entry            = 0x40080000;
    ehdr->e_ehsize           = sizeof(Elf32_Ehdr);
    ehdr->e_phentsize        = sizeof(Elf32_Phdr);
    ehdr->e_phnum            = phnum;
    /* Program header table immediately follows the ELF header. */
    ehdr->e_phoff            = (phnum > 0) ? sizeof(Elf32_Ehdr) : 0;
}

/* Build a self-consistent ELF image with `phnum` PT_LOAD segments.
 * Each segment has 256 bytes of data placed after the program header table
 * so that bounds checks in collect_segs pass correctly. */
static std::vector<uint8_t> make_elf(uint16_t phnum = 0)
{
    const size_t DATA_PER_SEG = 256;
    size_t phdr_table_size = phnum * sizeof(Elf32_Phdr);
    size_t data_base       = sizeof(Elf32_Ehdr) + phdr_table_size;
    size_t total           = data_base + phnum * DATA_PER_SEG;
    std::vector<uint8_t> buf(total, 0);

    Elf32_Ehdr *ehdr = reinterpret_cast<Elf32_Ehdr *>(buf.data());
    fill_elf_header(ehdr, phnum);

    /* Fill program headers with recognisable load addresses. */
    for (uint16_t i = 0; i < phnum; i++) {
        Elf32_Phdr *phdr = reinterpret_cast<Elf32_Phdr *>(
                               buf.data() + sizeof(Elf32_Ehdr) + i * sizeof(Elf32_Phdr));
        phdr->p_type   = PT_LOAD;
        phdr->p_vaddr  = 0x40080000u + i * 0x1000u;
        phdr->p_filesz = DATA_PER_SEG;
        phdr->p_memsz  = DATA_PER_SEG;
        phdr->p_flags  = PF_R | PF_X;
        phdr->p_offset = static_cast<uint32_t>(data_base + i * DATA_PER_SEG);
    }

    return buf;
}

/* ---------------------------------------------------------------------------
 * elf_validate tests
 * ------------------------------------------------------------------------ */

TEST_CASE("elf_validate: minimal valid ELF (no segments)", "[elf_parser]")
{
    auto buf = make_elf(0);
    CHECK(elf_validate(buf.data(), buf.size()) == ESP_LOADER_SUCCESS);
}

TEST_CASE("elf_validate: valid ELF with two PT_LOAD segments", "[elf_parser]")
{
    auto buf = make_elf(2);
    CHECK(elf_validate(buf.data(), buf.size()) == ESP_LOADER_SUCCESS);
}

TEST_CASE("elf_validate: NULL pointer returns INVALID_PARAM", "[elf_parser]")
{
    CHECK(elf_validate(nullptr, 64) == ESP_LOADER_ERROR_INVALID_PARAM);
}

TEST_CASE("elf_validate: empty buffer returns INVALID_PARAM", "[elf_parser]")
{
    uint8_t buf[1] = {0};
    CHECK(elf_validate(buf, 0) == ESP_LOADER_ERROR_INVALID_PARAM);
}

TEST_CASE("elf_validate: truncated to 51 bytes returns INVALID_PARAM", "[elf_parser]")
{
    auto buf = make_elf(0);
    CHECK(elf_validate(buf.data(), sizeof(Elf32_Ehdr) - 1)
          == ESP_LOADER_ERROR_INVALID_PARAM);
}

TEST_CASE("elf_validate: wrong magic returns INVALID_PARAM", "[elf_parser]")
{
    auto buf = make_elf(0);
    buf[0] = 0x00; /* corrupt first magic byte */
    CHECK(elf_validate(buf.data(), buf.size()) == ESP_LOADER_ERROR_INVALID_PARAM);
}

TEST_CASE("elf_validate: ELFCLASS64 returns INVALID_PARAM", "[elf_parser]")
{
    auto buf = make_elf(0);
    buf[EI_CLASS] = 2; /* ELFCLASS64 */
    CHECK(elf_validate(buf.data(), buf.size()) == ESP_LOADER_ERROR_INVALID_PARAM);
}

TEST_CASE("elf_validate: big-endian returns INVALID_PARAM", "[elf_parser]")
{
    auto buf = make_elf(0);
    buf[EI_DATA] = 2; /* ELFDATA2MSB */
    CHECK(elf_validate(buf.data(), buf.size()) == ESP_LOADER_ERROR_INVALID_PARAM);
}

TEST_CASE("elf_validate: ET_DYN (non-EXEC) returns INVALID_PARAM", "[elf_parser]")
{
    auto buf = make_elf(0);
    Elf32_Ehdr *ehdr = reinterpret_cast<Elf32_Ehdr *>(buf.data());
    ehdr->e_type = 3; /* ET_DYN */
    CHECK(elf_validate(buf.data(), buf.size()) == ESP_LOADER_ERROR_INVALID_PARAM);
}

TEST_CASE("elf_validate: truncated program header table returns INVALID_PARAM", "[elf_parser]")
{
    /* Build a valid 2-segment ELF then shorten the buffer so the second phdr is cut off. */
    auto buf = make_elf(2);
    size_t truncated = sizeof(Elf32_Ehdr) + sizeof(Elf32_Phdr); /* only room for 1 phdr */
    CHECK(elf_validate(buf.data(), truncated) == ESP_LOADER_ERROR_INVALID_PARAM);
}

/* ---------------------------------------------------------------------------
 * elf_get_phdr tests
 * ------------------------------------------------------------------------ */

TEST_CASE("elf_get_phdr: index 0 returns non-NULL for 1-segment ELF", "[elf_parser]")
{
    auto buf = make_elf(1);
    const Elf32_Phdr *phdr = elf_get_phdr(buf.data(), buf.size(), 0);
    REQUIRE(phdr != nullptr);
    CHECK(phdr->p_type == PT_LOAD);
}

TEST_CASE("elf_get_phdr: indices 0..N-1 all valid for N-segment ELF", "[elf_parser]")
{
    const uint16_t N = 3;
    auto buf = make_elf(N);
    for (uint16_t i = 0; i < N; i++) {
        const Elf32_Phdr *phdr = elf_get_phdr(buf.data(), buf.size(), i);
        REQUIRE(phdr != nullptr);
        CHECK(phdr->p_vaddr == 0x40080000u + i * 0x1000u);
    }
}

TEST_CASE("elf_get_phdr: index == e_phnum returns NULL", "[elf_parser]")
{
    auto buf = make_elf(2);
    CHECK(elf_get_phdr(buf.data(), buf.size(), 2) == nullptr);
}

TEST_CASE("elf_get_phdr: index > e_phnum returns NULL", "[elf_parser]")
{
    auto buf = make_elf(2);
    CHECK(elf_get_phdr(buf.data(), buf.size(), 100) == nullptr);
}

TEST_CASE("elf_get_phdr: NULL data returns NULL", "[elf_parser]")
{
    CHECK(elf_get_phdr(nullptr, 128, 0) == nullptr);
}

/* ---------------------------------------------------------------------------
 * Task 2 — elf_classify_segment and elf_chip_info tests
 * Spot-checks validated against esptool/targets/<chip>.py and loader.py.
 * ------------------------------------------------------------------------ */

/* Helper: classify and optionally check the flash-window offset. */
static esp_seg_type_t classify(target_chip_t chip, uint32_t vaddr,
                               uint32_t *flash_out = nullptr)
{
    return elf_classify_segment(chip, vaddr, flash_out);
}

/* ---- ESP8266 ---- */
TEST_CASE("classify ESP8266: IROM (0x40200000)", "[seg_classifier]")
{
    uint32_t faddr = 0xDEAD;
    CHECK(classify(ESP8266_CHIP, 0x40200000u, &faddr) == ESP_SEG_IROM);
    CHECK(faddr == 0u); /* vaddr - mmu_offset = 0x40200000 - 0x40200000 */
}

TEST_CASE("classify ESP8266: IROM mid-range (0x40250000)", "[seg_classifier]")
{
    uint32_t faddr = 0;
    CHECK(classify(ESP8266_CHIP, 0x40250000u, &faddr) == ESP_SEG_IROM);
    CHECK(faddr == 0x50000u);
}

TEST_CASE("classify ESP8266: DRAM (0x3FFF0000)", "[seg_classifier]")
{
    CHECK(classify(ESP8266_CHIP, 0x3FFF0000u) == ESP_SEG_DRAM);
}

TEST_CASE("classify ESP8266: IRAM (0x40100000)", "[seg_classifier]")
{
    CHECK(classify(ESP8266_CHIP, 0x40100000u) == ESP_SEG_IRAM);
}

TEST_CASE("classify ESP8266: unknown (0x00000000)", "[seg_classifier]")
{
    CHECK(classify(ESP8266_CHIP, 0x00000000u) == ESP_SEG_UNKNOWN);
}

/* flash_addr_out must NOT be written for RAM segments. */
TEST_CASE("classify ESP8266: DRAM does not write flash_addr_out", "[seg_classifier]")
{
    uint32_t sentinel = 0xDEADBEEFu;
    CHECK(classify(ESP8266_CHIP, 0x3FFF0000u, &sentinel) == ESP_SEG_DRAM);
    CHECK(sentinel == 0xDEADBEEFu);
}

/* ---- ESP32 ---- */
TEST_CASE("classify ESP32: DROM (0x3F500000)", "[seg_classifier]")
{
    uint32_t faddr = 0xDEAD;
    CHECK(classify(ESP32_CHIP, 0x3F500000u, &faddr) == ESP_SEG_DROM);
    CHECK(faddr == 0x100000u); /* 0x3F500000 - 0x3F400000 */
}

TEST_CASE("classify ESP32: IROM (0x400E0000)", "[seg_classifier]")
{
    uint32_t faddr = 0xDEAD;
    CHECK(classify(ESP32_CHIP, 0x400E0000u, &faddr) == ESP_SEG_IROM);
    CHECK(faddr == 0x10000u); /* 0x400E0000 - 0x400D0000 */
}

TEST_CASE("classify ESP32: DRAM (0x3FFBE000)", "[seg_classifier]")
{
    CHECK(classify(ESP32_CHIP, 0x3FFBe000u) == ESP_SEG_DRAM);
}

TEST_CASE("classify ESP32: IRAM (0x40090000)", "[seg_classifier]")
{
    CHECK(classify(ESP32_CHIP, 0x40090000u) == ESP_SEG_IRAM);
}

TEST_CASE("classify ESP32: RTC_DATA (0x50000010)", "[seg_classifier]")
{
    CHECK(classify(ESP32_CHIP, 0x50000010u) == ESP_SEG_RTC_DATA);
}

TEST_CASE("classify ESP32: address just below DROM returns UNKNOWN", "[seg_classifier]")
{
    CHECK(classify(ESP32_CHIP, 0x3F3FFFFFu) == ESP_SEG_UNKNOWN);
}

TEST_CASE("classify ESP32: IROM MAP_END is exclusive (0x40400000)", "[seg_classifier]")
{
    CHECK(classify(ESP32_CHIP, 0x40400000u) == ESP_SEG_UNKNOWN);
}

/* ---- ESP32-S2 ---- */
TEST_CASE("classify ESP32-S2: DROM (0x3F100000)", "[seg_classifier]")
{
    CHECK(classify(ESP32S2_CHIP, 0x3F100000u) == ESP_SEG_DROM);
}

TEST_CASE("classify ESP32-S2: IROM (0x40100000)", "[seg_classifier]")
{
    CHECK(classify(ESP32S2_CHIP, 0x40100000u) == ESP_SEG_IROM);
}

TEST_CASE("classify ESP32-S2: DRAM (0x3FFC0000)", "[seg_classifier]")
{
    CHECK(classify(ESP32S2_CHIP, 0x3FFC0000u) == ESP_SEG_DRAM);
}

TEST_CASE("classify ESP32-S2: IRAM (0x40040000)", "[seg_classifier]")
{
    CHECK(classify(ESP32S2_CHIP, 0x40040000u) == ESP_SEG_IRAM);
}

/* ---- ESP32-C3 ---- */
TEST_CASE("classify ESP32-C3: DROM (0x3C400000)", "[seg_classifier]")
{
    CHECK(classify(ESP32C3_CHIP, 0x3C400000u) == ESP_SEG_DROM);
}

TEST_CASE("classify ESP32-C3: IROM (0x42400000)", "[seg_classifier]")
{
    CHECK(classify(ESP32C3_CHIP, 0x42400000u) == ESP_SEG_IROM);
}

TEST_CASE("classify ESP32-C3: DRAM (0x3FC90000)", "[seg_classifier]")
{
    CHECK(classify(ESP32C3_CHIP, 0x3FC90000u) == ESP_SEG_DRAM);
}

TEST_CASE("classify ESP32-C3: IRAM (0x40380000)", "[seg_classifier]")
{
    CHECK(classify(ESP32C3_CHIP, 0x40380000u) == ESP_SEG_IRAM);
}

/* ---- ESP32-S3 ---- */
TEST_CASE("classify ESP32-S3: DROM (0x3C800000)", "[seg_classifier]")
{
    CHECK(classify(ESP32S3_CHIP, 0x3C800000u) == ESP_SEG_DROM);
}

TEST_CASE("classify ESP32-S3: IROM (0x42400000)", "[seg_classifier]")
{
    CHECK(classify(ESP32S3_CHIP, 0x42400000u) == ESP_SEG_IROM);
}

TEST_CASE("classify ESP32-S3: DRAM (0x3FC90000)", "[seg_classifier]")
{
    CHECK(classify(ESP32S3_CHIP, 0x3FC90000u) == ESP_SEG_DRAM);
}

TEST_CASE("classify ESP32-S3: IRAM (0x40380000)", "[seg_classifier]")
{
    CHECK(classify(ESP32S3_CHIP, 0x40380000u) == ESP_SEG_IRAM);
}

/* ---- ESP32-C6 ---- */
TEST_CASE("classify ESP32-C6: IROM (0x42400000)", "[seg_classifier]")
{
    CHECK(classify(ESP32C6_CHIP, 0x42400000u) == ESP_SEG_IROM);
}

TEST_CASE("classify ESP32-C6: DROM (0x42900000)", "[seg_classifier]")
{
    CHECK(classify(ESP32C6_CHIP, 0x42900000u) == ESP_SEG_DROM);
}

TEST_CASE("classify ESP32-C6: DRAM (0x40840000)", "[seg_classifier]")
{
    CHECK(classify(ESP32C6_CHIP, 0x40840000u) == ESP_SEG_DRAM);
}

/* ---- ESP32-H2 (inherits C6 ranges) ---- */
TEST_CASE("classify ESP32-H2: IROM (0x42400000)", "[seg_classifier]")
{
    CHECK(classify(ESP32H2_CHIP, 0x42400000u) == ESP_SEG_IROM);
}

TEST_CASE("classify ESP32-H2: DROM (0x42900000)", "[seg_classifier]")
{
    CHECK(classify(ESP32H2_CHIP, 0x42900000u) == ESP_SEG_DROM);
}

/* ---- ESP32-C5 ---- */
TEST_CASE("classify ESP32-C5: DROM window (0x43000000)", "[seg_classifier]")
{
    CHECK(classify(ESP32C5_CHIP, 0x43000000u) == ESP_SEG_DROM);
}

TEST_CASE("classify ESP32-C5: DRAM window (0x40820000)", "[seg_classifier]")
{
    CHECK(classify(ESP32C5_CHIP, 0x40820000u) == ESP_SEG_DRAM);
}

/* ---- ESP32-P4 ---- */
TEST_CASE("classify ESP32-P4: DROM window (0x44000000)", "[seg_classifier]")
{
    CHECK(classify(ESP32P4_CHIP, 0x44000000u) == ESP_SEG_DROM);
}

TEST_CASE("classify ESP32-P4: DRAM (0x4FF10000)", "[seg_classifier]")
{
    CHECK(classify(ESP32P4_CHIP, 0x4FF10000u) == ESP_SEG_DRAM);
}

/* ---- Generic boundary / edge cases ---- */
TEST_CASE("classify: out-of-range chip returns UNKNOWN", "[seg_classifier]")
{
    CHECK(classify(ESP_MAX_CHIP, 0x42000000u) == ESP_SEG_UNKNOWN);
    CHECK(classify((target_chip_t)99, 0x42000000u) == ESP_SEG_UNKNOWN);
}

TEST_CASE("classify: NULL flash_addr_out is safe for flash segments", "[seg_classifier]")
{
    /* Must not crash when flash_addr_out is NULL. */
    CHECK(classify(ESP32_CHIP, 0x3F500000u, nullptr) == ESP_SEG_DROM);
    CHECK(classify(ESP32_CHIP, 0x400E0000u, nullptr) == ESP_SEG_IROM);
}

/* ---- elf_chip_info ---- */
TEST_CASE("elf_chip_info: ESP8266 has no extended header", "[seg_classifier]")
{
    const esp_chip_info_t *info = elf_chip_info(ESP8266_CHIP);
    REQUIRE(info != nullptr);
    CHECK(info->has_ext_header == false);
}

TEST_CASE("elf_chip_info: ESP32 chip_id == 0x0000, has ext header", "[seg_classifier]")
{
    const esp_chip_info_t *info = elf_chip_info(ESP32_CHIP);
    REQUIRE(info != nullptr);
    CHECK(info->chip_id == 0x0000u);
    CHECK(info->has_ext_header == true);
}

TEST_CASE("elf_chip_info: chip_ids match esptool IMAGE_CHIP_ID", "[seg_classifier]")
{
    /* Spot-check a selection; full table validated above. */
    CHECK(elf_chip_info(ESP32S2_CHIP)->chip_id == 0x0002u);
    CHECK(elf_chip_info(ESP32C3_CHIP)->chip_id == 0x0005u);
    CHECK(elf_chip_info(ESP32S3_CHIP)->chip_id == 0x0009u);
    CHECK(elf_chip_info(ESP32C2_CHIP)->chip_id == 0x000Cu);
    CHECK(elf_chip_info(ESP32C6_CHIP)->chip_id == 0x000Du);
    CHECK(elf_chip_info(ESP32H2_CHIP)->chip_id == 0x0010u);
    CHECK(elf_chip_info(ESP32C5_CHIP)->chip_id == 0x0017u);
    CHECK(elf_chip_info(ESP32P4_CHIP)->chip_id == 0x0012u);
}

TEST_CASE("elf_chip_info: out-of-range chip returns NULL", "[seg_classifier]")
{
    CHECK(elf_chip_info(ESP_MAX_CHIP) == nullptr);
}

/* ---------------------------------------------------------------------------
 * Task 3 — Image size calculator (dry-run pass)
 * ------------------------------------------------------------------------ */

/* Helper: load a file into a vector. Returns empty vector on failure. */
static std::vector<uint8_t> load_file(const char *path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return {};
    }
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

/* Helper: call dry-run and return the reported size (or 0 on error). */
static size_t dry_run_size(const std::vector<uint8_t> &elf,
                           target_chip_t chip,
                           bool append_sha = false)
{
    esp_loader_elf_cfg_t cfg = ESP_LOADER_ELF_CFG_DEFAULT();
    cfg.append_sha256 = append_sha;
    size_t sz = 0;
    esp_loader_error_t err = esp_loader_elf_to_flash_image(
                                 elf.data(), elf.size(), chip, &cfg, nullptr, &sz);
    if (err != ESP_LOADER_SUCCESS) {
        return 0;
    }
    return sz;
}

/* ---- Parameter validation ---- */
TEST_CASE("size_calc: NULL elf_data returns INVALID_PARAM", "[size_calc]")
{
    esp_loader_elf_cfg_t cfg = ESP_LOADER_ELF_CFG_DEFAULT();
    size_t sz = 0;
    CHECK(esp_loader_elf_to_flash_image(nullptr, 64, ESP32_CHIP, &cfg, nullptr, &sz)
          == ESP_LOADER_ERROR_INVALID_PARAM);
}

TEST_CASE("size_calc: NULL cfg returns INVALID_PARAM", "[size_calc]")
{
    auto buf = make_elf(0);
    size_t sz = 0;
    CHECK(esp_loader_elf_to_flash_image(buf.data(), buf.size(),
                                        ESP32_CHIP, nullptr, nullptr, &sz)
          == ESP_LOADER_ERROR_INVALID_PARAM);
}

TEST_CASE("size_calc: NULL out_size returns INVALID_PARAM", "[size_calc]")
{
    esp_loader_elf_cfg_t cfg = ESP_LOADER_ELF_CFG_DEFAULT();
    auto buf = make_elf(0);
    CHECK(esp_loader_elf_to_flash_image(buf.data(), buf.size(),
                                        ESP32_CHIP, &cfg, nullptr, nullptr)
          == ESP_LOADER_ERROR_INVALID_PARAM);
}

TEST_CASE("size_calc: unsupported chip returns UNSUPPORTED_CHIP", "[size_calc]")
{
    esp_loader_elf_cfg_t cfg = ESP_LOADER_ELF_CFG_DEFAULT();
    auto buf = make_elf(0);
    size_t sz = 0;
    CHECK(esp_loader_elf_to_flash_image(buf.data(), buf.size(),
                                        ESP_MAX_CHIP, &cfg, nullptr, &sz)
          == ESP_LOADER_ERROR_UNSUPPORTED_CHIP);
}

TEST_CASE("size_calc: malformed ELF returns INVALID_PARAM", "[size_calc]")
{
    esp_loader_elf_cfg_t cfg = ESP_LOADER_ELF_CFG_DEFAULT();
    uint8_t bad[4] = {0};
    size_t sz = 0;
    CHECK(esp_loader_elf_to_flash_image(bad, sizeof(bad), ESP32_CHIP, &cfg, nullptr, &sz)
          == ESP_LOADER_ERROR_INVALID_PARAM);
}

/* ---- Determinism ---- */
TEST_CASE("size_calc: two calls with the same ELF return the same size", "[size_calc]")
{
    auto buf = make_elf(2);
    size_t sz1 = dry_run_size(buf, ESP32_CHIP);
    size_t sz2 = dry_run_size(buf, ESP32_CHIP);
    CHECK(sz1 == sz2);
    CHECK(sz1 > 0);
}

/* ---- Size formula for a synthetic ESP32 ELF (all-RAM, no flash segs) ----
 *
 * make_elf(N) produces an ELF whose N PT_LOAD segments all have vaddr in the
 * IRAM range (0x40080000+), so they are classified as IRAM (RAM segments).
 * No alignment gaps are inserted.
 *
 * Expected size (no SHA, no flash segs):
 *   header_size  = 24
 *   segs_size    = N × (8 + 256)   (filesz_padded = 256 for each)
 *   total_before = 24 + N×264
 *   pad          = (15 - (total_before % 16)) % 16 + 1
 *   total        = total_before + pad
 */
static size_t expected_all_ram_size(size_t n_segs)
{
    const size_t HDR  = 24;
    const size_t SEGD = 8 + 256; /* descriptor + 256-byte data (already 4-byte aligned) */
    size_t before = HDR + n_segs * SEGD;
    size_t pad    = (15u - (before % 16u)) % 16u + 1u;
    return before + pad;
}

TEST_CASE("size_calc: synthetic 0-segment ESP32 ELF", "[size_calc]")
{
    auto buf = make_elf(0);
    size_t got = dry_run_size(buf, ESP32_CHIP);
    CHECK(got == expected_all_ram_size(0));
}

TEST_CASE("size_calc: synthetic 1-segment ESP32 ELF", "[size_calc]")
{
    auto buf = make_elf(1);
    size_t got = dry_run_size(buf, ESP32_CHIP);
    CHECK(got == expected_all_ram_size(1));
}

TEST_CASE("size_calc: synthetic 3-segment ESP32 ELF", "[size_calc]")
{
    auto buf = make_elf(3);
    size_t got = dry_run_size(buf, ESP32_CHIP);
    CHECK(got == expected_all_ram_size(3));
}

/* ---- SHA-256 adds exactly 32 bytes ---- */
TEST_CASE("size_calc: append_sha256 adds exactly 32 bytes", "[size_calc]")
{
    auto buf = make_elf(2);
    size_t without = dry_run_size(buf, ESP32_CHIP, false);
    size_t with    = dry_run_size(buf, ESP32_CHIP, true);
    CHECK(with == without + 32);
}

/* ---- Truncated ELF returns INVALID_PARAM ---- */
TEST_CASE("size_calc: ELF truncated by 1 byte returns INVALID_PARAM", "[size_calc]")
{
    auto buf = make_elf(0);
    buf.resize(buf.size() - 1);
    esp_loader_elf_cfg_t cfg = ESP_LOADER_ELF_CFG_DEFAULT();
    size_t sz = 0;
    CHECK(esp_loader_elf_to_flash_image(buf.data(), buf.size(),
                                        ESP32_CHIP, &cfg, nullptr, &sz)
          == ESP_LOADER_ERROR_INVALID_PARAM);
}

/* ---- Real IDF ELF: hello_world (ESP32 / Xtensa) ---- *
 * Validated against: esptool --chip esp32 elf2image --dont-append-digest
 * Expected sizes: 122480 bytes (no SHA), 122512 bytes (with SHA).
 */
static const char HELLO_WORLD_ELF[] =
    "/home/jarda/esp/master/esp-idf/examples/get-started"
    "/hello_world/build/hello_world.elf";
static const size_t HELLO_WORLD_BIN_NOSHA = 122480;
static const size_t HELLO_WORLD_BIN_SHA   = 122512;

TEST_CASE("size_calc: hello_world.elf size matches esptool (no SHA)", "[size_calc][real_elf]")
{
    auto elf = load_file(HELLO_WORLD_ELF);
    if (elf.empty()) {
        WARN("Skipped: " << HELLO_WORLD_ELF << " not found");
        return;
    }
    CHECK(dry_run_size(elf, ESP32_CHIP, false) == HELLO_WORLD_BIN_NOSHA);
}

TEST_CASE("size_calc: hello_world.elf size matches esptool (with SHA)", "[size_calc][real_elf]")
{
    auto elf = load_file(HELLO_WORLD_ELF);
    if (elf.empty()) {
        WARN("Skipped: " << HELLO_WORLD_ELF << " not found");
        return;
    }
    CHECK(dry_run_size(elf, ESP32_CHIP, true) == HELLO_WORLD_BIN_SHA);
}
