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

extern "C" {
#include "elf32.h"
#include "esp_loader_error.h"
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

/* Build a self-consistent ELF image with `phnum` PT_LOAD segments. */
static std::vector<uint8_t> make_elf(uint16_t phnum = 0)
{
    size_t total = sizeof(Elf32_Ehdr) + phnum * sizeof(Elf32_Phdr);
    std::vector<uint8_t> buf(total, 0);

    Elf32_Ehdr *ehdr = reinterpret_cast<Elf32_Ehdr *>(buf.data());
    fill_elf_header(ehdr, phnum);

    /* Fill program headers with recognisable load addresses. */
    for (uint16_t i = 0; i < phnum; i++) {
        Elf32_Phdr *phdr = reinterpret_cast<Elf32_Phdr *>(
                               buf.data() + sizeof(Elf32_Ehdr) + i * sizeof(Elf32_Phdr));
        phdr->p_type   = PT_LOAD;
        phdr->p_vaddr  = 0x40080000u + i * 0x1000u;
        phdr->p_filesz = 256;
        phdr->p_memsz  = 256;
        phdr->p_flags  = PF_R | PF_X;
        phdr->p_offset = 0; /* points inside the header area — valid for bounds check */
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
