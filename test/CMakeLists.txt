cmake_minimum_required(VERSION 3.22)
project(serial_flasher_test)

add_executable( ${PROJECT_NAME}
	test_main.cpp
	../src/esp_loader.c
	../src/esp_targets.c
	../src/esp_stubs.c
	../src/esp_sdio_stubs.c
	../src/md5_hash.c
	../src/protocol_serial.c
	../src/protocol_uart.c
	../src/protocol_spi.c
	../src/protocol_sdio.c
	../src/slip.c)

target_include_directories(${PROJECT_NAME} PRIVATE ../include ../private_include ../test)

target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Werror -O3)

set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 14)

target_sources(${PROJECT_NAME} PRIVATE test_tcp_port.cpp qemu_test.cpp)

target_compile_definitions(${PROJECT_NAME} PRIVATE
	SERIAL_FLASHER_DEBUG_TRACE
	SERIAL_FLASHER_WRITE_BLOCK_RETRIES=3
)

# ---------------------------------------------------------------------------
# Standalone host test for the ELF32 parser (Task 1) — no hardware needed.
# ---------------------------------------------------------------------------
add_executable(test_elf_parser
	test_elf_parser.cpp
	../src/esp_elf_image.c)

target_include_directories(test_elf_parser PRIVATE ../include ../private_include ../test)
target_compile_options(test_elf_parser PRIVATE -Wall -Werror -O2)
set_property(TARGET test_elf_parser PROPERTY CXX_STANDARD 14)
