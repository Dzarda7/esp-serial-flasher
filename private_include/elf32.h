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

/* Minimal ELF32 definitions — only the fields used by esp_elf_image.c.
 * No system <elf.h> dependency; works on bare-metal targets without POSIX libc. */

/* ELF identification magic bytes (e_ident[0..3]) */
#define ELF_MAGIC_0   0x7Fu
#define ELF_MAGIC_1   'E'
#define ELF_MAGIC_2   'L'
#define ELF_MAGIC_3   'F'

/* e_ident indices */
#define EI_MAG0       0
#define EI_MAG1       1
#define EI_MAG2       2
#define EI_MAG3       3
#define EI_CLASS      4
#define EI_DATA       5
#define EI_NIDENT     16

/* EI_CLASS values */
#define ELFCLASS32    1   /*!< 32-bit ELF */

/* EI_DATA values */
#define ELFDATA2LSB   1   /*!< Little-endian */

/* e_type values */
#define ET_EXEC       2   /*!< Executable file */

/* e_machine values */
#define EM_XTENSA     94  /*!< Tensilica Xtensa (ESP32, ESP8266) */
#define EM_RISCV      243 /*!< RISC-V (ESP32-C/H/P series) */

/* p_type values */
#define PT_LOAD       1   /*!< Loadable segment */

/* p_flags bit masks */
#define PF_X          0x1u /*!< Execute */
#define PF_W          0x2u /*!< Write */
#define PF_R          0x4u /*!< Read */

/** ELF32 executable header (52 bytes). */
typedef struct {
    uint8_t  e_ident[EI_NIDENT]; /*!< Magic number and other info */
    uint16_t e_type;             /*!< Object file type */
    uint16_t e_machine;          /*!< Architecture */
    uint32_t e_version;          /*!< Object file version */
    uint32_t e_entry;            /*!< Entry point virtual address */
    uint32_t e_phoff;            /*!< Program header table file offset */
    uint32_t e_shoff;            /*!< Section header table file offset */
    uint32_t e_flags;            /*!< Processor-specific flags */
    uint16_t e_ehsize;           /*!< ELF header size in bytes */
    uint16_t e_phentsize;        /*!< Program header table entry size */
    uint16_t e_phnum;            /*!< Program header table entry count */
    uint16_t e_shentsize;        /*!< Section header table entry size */
    uint16_t e_shnum;            /*!< Section header table entry count */
    uint16_t e_shstrndx;         /*!< Section header string table index */
} Elf32_Ehdr;

/** ELF32 program (segment) header (32 bytes). */
typedef struct {
    uint32_t p_type;   /*!< Segment type */
    uint32_t p_offset; /*!< Segment file offset */
    uint32_t p_vaddr;  /*!< Segment virtual address */
    uint32_t p_paddr;  /*!< Segment physical address */
    uint32_t p_filesz; /*!< Segment size in file */
    uint32_t p_memsz;  /*!< Segment size in memory */
    uint32_t p_flags;  /*!< Segment flags */
    uint32_t p_align;  /*!< Segment alignment */
} Elf32_Phdr;
