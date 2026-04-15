#!/usr/bin/env bash
#
# Copyright 2026 Espressif Systems (Shanghai) CO LTD
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# Compile and run the C unit tests for the ELF32 parser, segment classifier,
# image size calculator, and image write pass.
#
# Usage (from repo root):
#   bash test/t/0001-elf-parser.t
#
# Environment variables:
#   CC        — C compiler to use (default: cc)
#   SAN_FLAGS — extra compiler flags (default: -fsanitize=address,undefined)
#   T_OUT     — build output directory (default: /tmp/esp-serial-flasher-t)
#   TEST_ELF  — optional path to a real ESP32 hello_world.elf for extra tests

set -e

CC="${CC:-cc}"
SAN_FLAGS="${SAN_FLAGS-}"
T_OUT="${T_OUT:-/tmp/esp-serial-flasher-t}"

# Resolve repo root relative to this script so the driver works regardless of
# the caller's working directory.
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO="${SCRIPT_DIR}/../.."

O="$T_OUT/$(basename "$0" .t)"
rm -rf "$O" && mkdir -p "$O"

# If TEST_ELF is not set, check for the in-repo ESP32 hello_world fixture.
if [ -z "$TEST_ELF" ]; then
    CANDIDATE="$REPO/test/target-example-src/hello-world-ESP32-src/build-flash-esp32/hello_world.elf"
    if [ -f "$CANDIDATE" ]; then
        TEST_ELF="$CANDIDATE"
    fi
fi

$CC -std=c99 $SAN_FLAGS \
    -I"$REPO/include" \
    -I"$REPO/private_include" \
    "$REPO/src/esp_elf_image.c" \
    "$REPO/src/sha256.c" \
    "$REPO/test/t/loader_stub.c" \
    "$REPO/test/t/test_elf_parser.c" \
    -o "$O/test_elf_parser"

TEST_ELF="$TEST_ELF" "$O/test_elf_parser"
