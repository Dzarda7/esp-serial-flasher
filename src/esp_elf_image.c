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
#include <stdbool.h>
#include <string.h>

#include "elf32.h"
#include "esp_loader.h"
#include "esp_loader_error.h"
#include "esp_loader_elf.h"
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

/* -------------------------------------------------------------------------
 * Task 2 — Segment classifier and per-chip address tables
 *
 * Address ranges cross-validated against:
 *   esptool/targets/<chip>.py  (IROM_MAP_START/END, DROM_MAP_START/END, MEMORY_MAP)
 *   esptool/loader.py          (ESP8266 IROM_MAP_START/END)
 * ---------------------------------------------------------------------- */

/* Sentinel that terminates a chip's range list. */
#define SEG_SENTINEL  { 0, 0, ESP_SEG_UNKNOWN, 0 }

/* Shorthand macros for readability. */
#define DROM(lo, end, off)  { (lo), (end), ESP_SEG_DROM,     (off) }
#define IROM(lo, end, off)  { (lo), (end), ESP_SEG_IROM,     (off) }
#define DRAM(lo, end)       { (lo), (end), ESP_SEG_DRAM,     0     }
#define IRAM(lo, end)       { (lo), (end), ESP_SEG_IRAM,     0     }
#define RTC(lo, end)        { (lo), (end), ESP_SEG_RTC_DATA, 0     }

/* ---------------------------------------------------------------------------
 * Per-chip address range tables
 * Ranges validated against esptool source (commit on master, 2026-04).
 *
 * vaddr_end is exclusive (addr < vaddr_end), matching esptool's MAP_END.
 * mmu_offset for DROM/IROM: flash_window_offset = vaddr - mmu_offset.
 *   Set to MAP_START so offset 0 corresponds to the first byte of the window.
 *   Callers add the partition base to get the absolute flash address.
 * ------------------------------------------------------------------------ */

/* ESP8266 — sources: loader.py IROM_MAP, targets/esp8266.py MEMORY_MAP */
static const esp_seg_range_t s_ranges_esp8266[] = {
    IROM(0x40200000u, 0x40300000u, 0x40200000u), /* IROM_MAP (loader.py) */
    DRAM(0x3FFE8000u, 0x40000000u),              /* DRAM */
    IRAM(0x40100000u, 0x40108000u),              /* IRAM */
    SEG_SENTINEL,
};

/* ESP32 — sources: IROM_MAP_START/END, DROM_MAP_START/END, MEMORY_MAP */
static const esp_seg_range_t s_ranges_esp32[] = {
    DROM(0x3F400000u, 0x3F800000u, 0x3F400000u), /* DROM_MAP */
    IROM(0x400D0000u, 0x40400000u, 0x400D0000u), /* IROM_MAP */
    DRAM(0x3FFAE000u, 0x40000000u),              /* DRAM + DIRAM_DRAM */
    IRAM(0x40080000u, 0x400C0000u),              /* IRAM + DIRAM_IRAM + RTC_IRAM */
    RTC(0x50000000u,  0x50002000u),              /* RTC_DATA */
    SEG_SENTINEL,
};

/* ESP32-S2 — sources: IROM_MAP_START/END, DROM_MAP_START/END, MEMORY_MAP */
static const esp_seg_range_t s_ranges_esp32s2[] = {
    DROM(0x3F000000u, 0x3FF80000u, 0x3F000000u), /* DROM (MEMORY_MAP) */
    IROM(0x40080000u, 0x40B80000u, 0x40080000u), /* IROM_MAP */
    DRAM(0x3FFB0000u, 0x40000000u),              /* DRAM */
    IRAM(0x40020000u, 0x40070000u),              /* IRAM */
    RTC(0x50000000u,  0x50002000u),              /* RTC_DATA */
    SEG_SENTINEL,
};

/* ESP32-C3 — sources: IROM_MAP_START/END, DROM_MAP_START/END, MEMORY_MAP */
static const esp_seg_range_t s_ranges_esp32c3[] = {
    DROM(0x3C000000u, 0x3C800000u, 0x3C000000u), /* DROM_MAP */
    IROM(0x42000000u, 0x42800000u, 0x42000000u), /* IROM_MAP */
    DRAM(0x3FC80000u, 0x3FCE0000u),              /* DRAM */
    IRAM(0x4037C000u, 0x403E0000u),              /* IRAM */
    RTC(0x50000000u,  0x50002000u),              /* RTC_DATA/RTC_IRAM (MEMORY_MAP) */
    SEG_SENTINEL,
};

/* ESP32-S3 — sources: IROM_MAP_START/END, DROM_MAP_START/END, MEMORY_MAP */
static const esp_seg_range_t s_ranges_esp32s3[] = {
    DROM(0x3C000000u, 0x3E000000u, 0x3C000000u), /* DROM_MAP */
    IROM(0x42000000u, 0x44000000u, 0x42000000u), /* IROM_MAP */
    DRAM(0x3FC88000u, 0x3FD00000u),              /* DRAM */
    IRAM(0x40370000u, 0x403E0000u),              /* IRAM */
    RTC(0x600FE000u,  0x60100000u),              /* RTC_DRAM/RTC_IRAM */
    SEG_SENTINEL,
};

/* ESP32-C2 — sources: IROM_MAP_START/END, DROM_MAP_START/END, MEMORY_MAP */
static const esp_seg_range_t s_ranges_esp32c2[] = {
    DROM(0x3C000000u, 0x3C400000u, 0x3C000000u), /* DROM_MAP */
    IROM(0x42000000u, 0x42400000u, 0x42000000u), /* IROM_MAP */
    DRAM(0x3FCA0000u, 0x3FCE0000u),              /* DRAM */
    IRAM(0x4037C000u, 0x403C0000u),              /* IRAM */
    SEG_SENTINEL,
};

/* ESP32-C5 — sources: IROM_MAP_START/END, DROM_MAP_START/END, MEMORY_MAP.
 * DROM and IROM share the same VA window; DRAM and IRAM share the same window. */
static const esp_seg_range_t s_ranges_esp32c5[] = {
    DROM(0x42000000u, 0x44000000u, 0x42000000u), /* DROM == IROM window */
    DRAM(0x40800000u, 0x40860000u),              /* DRAM == IRAM window */
    RTC(0x50000000u,  0x50004000u),              /* RTC_IRAM/RTC_DRAM */
    SEG_SENTINEL,
};

/* ESP32-H2 — inherits from ESP32-C6 in esptool; IMAGE_CHIP_ID = 16.
 * IROM and DROM are adjacent, non-overlapping windows. */
static const esp_seg_range_t s_ranges_esp32h2[] = {
    IROM(0x42000000u, 0x42800000u, 0x42000000u), /* IROM_MAP (from C6) */
    DROM(0x42800000u, 0x43000000u, 0x42800000u), /* DROM_MAP (from C6) */
    DRAM(0x40800000u, 0x40880000u),              /* DRAM == IRAM window */
    RTC(0x50000000u,  0x50004000u),              /* RTC_DATA (from C6) */
    SEG_SENTINEL,
};

/* ESP32-C6 — sources: IROM_MAP_START/END, DROM_MAP_START/END, MEMORY_MAP. */
static const esp_seg_range_t s_ranges_esp32c6[] = {
    IROM(0x42000000u, 0x42800000u, 0x42000000u), /* IROM_MAP */
    DROM(0x42800000u, 0x43000000u, 0x42800000u), /* DROM_MAP */
    DRAM(0x40800000u, 0x40880000u),              /* DRAM == IRAM window */
    RTC(0x50000000u,  0x50004000u),              /* RTC_DATA */
    SEG_SENTINEL,
};

/* ESP32-P4 — sources: IROM_MAP_START/END, DROM_MAP_START/END, MEMORY_MAP.
 * DROM and IROM share the same huge VA window; DRAM and IRAM share a window. */
static const esp_seg_range_t s_ranges_esp32p4[] = {
    DROM(0x40000000u, 0x4C000000u, 0x40000000u), /* DROM == IROM window */
    DRAM(0x4FF00000u, 0x4FFA0000u),              /* DRAM == IRAM window */
    RTC(0x50108000u,  0x50110000u),              /* RTC_IRAM/RTC_DRAM */
    SEG_SENTINEL,
};

/* ---------------------------------------------------------------------------
 * Top-level table — one entry per target_chip_t value (must equal ESP_MAX_CHIP)
 * ------------------------------------------------------------------------ */
typedef struct {
    const esp_seg_range_t *ranges;
    esp_chip_info_t         info;
} chip_entry_t;

static const chip_entry_t s_chip_table[] = {
    /* [ESP8266_CHIP] */ { s_ranges_esp8266,  { 0xFFFF, false } },
    /* [ESP32_CHIP]   */ { s_ranges_esp32,    { 0x0000, true  } },
    /* [ESP32S2_CHIP] */ { s_ranges_esp32s2,  { 0x0002, true  } },
    /* [ESP32C3_CHIP] */ { s_ranges_esp32c3,  { 0x0005, true  } },
    /* [ESP32S3_CHIP] */ { s_ranges_esp32s3,  { 0x0009, true  } },
    /* [ESP32C2_CHIP] */ { s_ranges_esp32c2,  { 0x000C, true  } },
    /* [ESP32C5_CHIP] */ { s_ranges_esp32c5,  { 0x0017, true  } },
    /* [ESP32H2_CHIP] */ { s_ranges_esp32h2,  { 0x0010, true  } },
    /* [ESP32C6_CHIP] */ { s_ranges_esp32c6,  { 0x000D, true  } },
    /* [ESP32P4_CHIP] */ { s_ranges_esp32p4,  { 0x0012, true  } },
};

_Static_assert(sizeof(s_chip_table) / sizeof(s_chip_table[0]) == ESP_MAX_CHIP,
               "s_chip_table must have exactly ESP_MAX_CHIP entries");

esp_seg_type_t elf_classify_segment(target_chip_t chip, uint32_t vaddr,
                                    uint32_t *flash_addr_out)
{
    if ((unsigned)chip >= ESP_MAX_CHIP) {
        return ESP_SEG_UNKNOWN;
    }

    const esp_seg_range_t *r = s_chip_table[chip].ranges;
    for (; r->type != ESP_SEG_UNKNOWN; r++) {
        if (vaddr >= r->vaddr_lo && vaddr < r->vaddr_end) {
            if (flash_addr_out != NULL &&
                    (r->type == ESP_SEG_DROM || r->type == ESP_SEG_IROM)) {
                *flash_addr_out = vaddr - r->mmu_offset;
            }
            return r->type;
        }
    }
    return ESP_SEG_UNKNOWN;
}

const esp_chip_info_t *elf_chip_info(target_chip_t chip)
{
    if ((unsigned)chip >= ESP_MAX_CHIP) {
        return NULL;
    }
    return &s_chip_table[chip].info;
}

/* -------------------------------------------------------------------------
 * Task 3 — Image size calculator (dry-run pass)
 *
 * Binary format (cross-validated against esptool v5.2 output):
 *
 *   [header: 24 B for ESP32+, 8 B for ESP8266]
 *   [seg descriptor (8 B) + data (padded to 4 B)] × seg_count
 *   [checksum padding: (15 - pos%16)%16+1 raw bytes, last byte = XOR checksum]
 *   [SHA-256 digest: 32 B, when cfg->append_sha256]
 *
 * Flash-mapped segments (DROM/IROM) are placed so that:
 *   file_pos_of_data % IROM_ALIGN == vaddr % IROM_ALIGN
 * RAM segments fill the gaps between flash segments and may be split.
 * ---------------------------------------------------------------------- */

#define MAX_SEGS    16u
#define IROM_ALIGN  0x10000u  /* 64 KB — same as esptool ESP32FirmwareImage.IROM_ALIGN */
#define SEG_HDR_LEN 8u        /* 4-byte load_addr + 4-byte data_size */
#define SHA256_LEN  32u

/* Round n up to the nearest 4-byte boundary (segment data padding). */
static uint32_t round_up4(uint32_t n)
{
    return (n + 3u) & ~3u;
}

/* Internal segment descriptor collected from PT_LOAD headers. */
typedef struct {
    uint32_t vaddr;         /* virtual address */
    uint32_t filesz_padded; /* p_filesz rounded up to 4 bytes */
    uint32_t fileoff;       /* byte offset of segment data in the ELF */
    bool     is_flash;      /* true → DROM or IROM (flash-mapped) */
} img_seg_t;

/* Collect all PT_LOAD segments with p_filesz > 0 into segs[]. */
static esp_loader_error_t collect_segs(const uint8_t *data, size_t size,
                                       target_chip_t chip,
                                       img_seg_t *segs, size_t *n_out)
{
    const Elf32_Ehdr *ehdr = (const Elf32_Ehdr *)data;
    size_t n = 0;

    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        const Elf32_Phdr *ph = elf_get_phdr(data, size, i);
        if (ph == NULL || ph->p_type != PT_LOAD || ph->p_filesz == 0) {
            continue;
        }
        /* Segment data must lie within the ELF buffer. */
        if ((uint64_t)ph->p_offset + ph->p_filesz > size) {
            return ESP_LOADER_ERROR_INVALID_PARAM;
        }
        if (n >= MAX_SEGS) {
            return ESP_LOADER_ERROR_INVALID_PARAM;
        }
        esp_seg_type_t t = elf_classify_segment(chip, ph->p_vaddr, NULL);
        segs[n].vaddr = ph->p_vaddr;
        segs[n].filesz_padded = round_up4(ph->p_filesz);
        segs[n].fileoff = ph->p_offset;
        segs[n].is_flash = (t == ESP_SEG_DROM || t == ESP_SEG_IROM);
        n++;
    }
    *n_out = n;
    return ESP_LOADER_SUCCESS;
}

/*
 * Simulate the image layout and return the total byte count.
 *
 * Flash segments are placed at IROM_ALIGN-aligned file positions matching
 * their vaddr (same algorithm as esptool ESP32FirmwareImage.save()).
 * RAM segments fill the gaps, split across descriptors as needed.
 */
static size_t layout_size(const img_seg_t *segs, size_t n,
                          size_t header_size, bool append_sha256)
{
    size_t pos = header_size;

    /* RAM cursor: tracks which RAM segment we are currently consuming
     * and how many bytes of its (padded) data have already been placed. */
    size_t ram_idx = 0;
    uint32_t ram_used = 0; /* bytes of segs[ram_idx].filesz_padded consumed */

    /* Advance ram_idx past any leading flash segments. */
    while (ram_idx < n && segs[ram_idx].is_flash) {
        ram_idx++;
    }

    /* Process flash segments in ELF order. */
    for (size_t i = 0; i < n; i++) {
        if (!segs[i].is_flash) {
            continue;
        }

        /*
         * Compute the target descriptor position for this flash segment:
         *   data must satisfy: data_pos % IROM_ALIGN == vaddr % IROM_ALIGN
         *   descriptor is SEG_HDR_LEN bytes before data.
         */
        uint32_t req_data_mod = segs[i].vaddr % IROM_ALIGN;
        uint32_t req_desc_mod = (req_data_mod + IROM_ALIGN - SEG_HDR_LEN) % IROM_ALIGN;

        uint32_t cur_mod = (uint32_t)(pos % IROM_ALIGN);
        size_t gap;
        if (cur_mod <= req_desc_mod) {
            gap = req_desc_mod - cur_mod;
        } else {
            gap = IROM_ALIGN - cur_mod + req_desc_mod;
        }
        size_t target = pos + gap;

        /* Fill the gap with RAM segment descriptors + data, splitting if needed. */
        while (gap > 0 && ram_idx < n) {
            uint32_t remaining = segs[ram_idx].filesz_padded - ram_used;

            if (gap < SEG_HDR_LEN) {
                /* Can't fit a descriptor — gap too small; stop filling. */
                break;
            }
            uint32_t avail_data = (uint32_t)(gap - SEG_HDR_LEN);

            if (remaining <= avail_data) {
                /* Whole segment fits in the gap. */
                pos += SEG_HDR_LEN + remaining;
                gap -= SEG_HDR_LEN + remaining;
                ram_used = 0;
                ram_idx++;
                /* Skip any embedded flash segments in the sequence. */
                while (ram_idx < n && segs[ram_idx].is_flash) {
                    ram_idx++;
                }
            } else {
                /* Split: write avail_data bytes, leave the rest for after IROM. */
                pos += SEG_HDR_LEN + avail_data;
                ram_used += avail_data;
                gap = 0;
            }
        }

        /* Place flash segment. */
        pos = target;
        pos += SEG_HDR_LEN + segs[i].filesz_padded;
    }

    /* Write remaining RAM segments (or the tail of a split one). */
    while (ram_idx < n) {
        if (!segs[ram_idx].is_flash) {
            uint32_t remaining = segs[ram_idx].filesz_padded - ram_used;
            pos += SEG_HDR_LEN + remaining;
            ram_used = 0;
        }
        ram_idx++;
    }

    /* Checksum padding: (15 - pos%16)%16 + 1 bytes (last byte = XOR checksum). */
    pos += (15u - (pos % 16u)) % 16u + 1u;

    /* Optional SHA-256 digest. */
    if (append_sha256) {
        pos += SHA256_LEN;
    }

    return pos;
}

/* -------------------------------------------------------------------------
 * Public API — Task 3: dry-run (out_buf == NULL)
 * ---------------------------------------------------------------------- */

esp_loader_error_t esp_loader_elf_to_flash_image(
    const uint8_t              *elf_data,
    size_t                      elf_size,
    target_chip_t               chip,
    const esp_loader_elf_cfg_t *cfg,
    uint8_t                    *out_buf,
    size_t                     *out_size)
{
    if (elf_data == NULL || cfg == NULL || out_size == NULL) {
        return ESP_LOADER_ERROR_INVALID_PARAM;
    }
    if ((unsigned)chip >= ESP_MAX_CHIP) {
        return ESP_LOADER_ERROR_UNSUPPORTED_CHIP;
    }

    RETURN_ON_ERROR(elf_validate(elf_data, elf_size));

    img_seg_t segs[MAX_SEGS];
    size_t n_segs = 0;
    RETURN_ON_ERROR(collect_segs(elf_data, elf_size, chip, segs, &n_segs));

    const esp_chip_info_t *ci = elf_chip_info(chip);
    size_t header_size = ci->has_ext_header ? 24u : 8u;

    size_t needed = layout_size(segs, n_segs, header_size, cfg->append_sha256);

    if (out_buf == NULL) {
        /* Dry-run: report required size and return. */
        *out_size = needed;
        return ESP_LOADER_SUCCESS;
    }

    /* Write pass (Task 4): check capacity. */
    if (*out_size < needed) {
        return ESP_LOADER_ERROR_IMAGE_SIZE;
    }

    /* Task 4 write pass — not yet implemented. */
    (void)out_buf;
    *out_size = needed;
    return ESP_LOADER_SUCCESS;
}
