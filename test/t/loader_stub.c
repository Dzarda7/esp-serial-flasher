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
 * Minimal stubs for the loader functions called by esp_elf_image.c.
 *
 * esp_loader_elf_to_flash_image() is port-free and does not call these
 * symbols, but the loader headers declare them and some build systems pull
 * in the full translation unit.  Providing stubs here keeps the link clean.
 *
 * All stubs return ESP_LOADER_ERROR_FAIL to make unintended calls obvious.
 */

#include "esp_loader.h"
#include "esp_loader_error.h"

target_chip_t esp_loader_get_target(esp_loader_t *loader)
{
    (void)loader;
    return ESP32_CHIP;
}

esp_loader_error_t esp_loader_flash_start(esp_loader_t *loader,
        esp_loader_flash_cfg_t *cfg)
{
    (void)loader;
    (void)cfg;
    return ESP_LOADER_ERROR_FAIL;
}

esp_loader_error_t esp_loader_flash_write(esp_loader_t *loader,
        esp_loader_flash_cfg_t *cfg,
        void *payload, uint32_t size)
{
    (void)loader;
    (void)cfg;
    (void)payload;
    (void)size;
    return ESP_LOADER_ERROR_FAIL;
}

esp_loader_error_t esp_loader_flash_finish(esp_loader_t *loader,
        esp_loader_flash_cfg_t *cfg)
{
    (void)loader;
    (void)cfg;
    return ESP_LOADER_ERROR_FAIL;
}

esp_loader_error_t esp_loader_mem_start(esp_loader_t *loader,
                                        esp_loader_mem_cfg_t *cfg)
{
    (void)loader;
    (void)cfg;
    return ESP_LOADER_ERROR_FAIL;
}

esp_loader_error_t esp_loader_mem_write(esp_loader_t *loader,
                                        esp_loader_mem_cfg_t *cfg,
                                        const void *payload, uint32_t size)
{
    (void)loader;
    (void)cfg;
    (void)payload;
    (void)size;
    return ESP_LOADER_ERROR_FAIL;
}

esp_loader_error_t esp_loader_mem_finish(esp_loader_t *loader,
        esp_loader_mem_cfg_t *cfg,
        uint32_t entrypoint)
{
    (void)loader;
    (void)cfg;
    (void)entrypoint;
    return ESP_LOADER_ERROR_FAIL;
}
