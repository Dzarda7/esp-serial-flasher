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
 * Minimal portable SHA-256 (public domain, Brad Conte algorithm).
 */

#include <string.h>
#include "sha256.h"

#define SHA256_ROTRIGHT(a, b)  (((a) >> (b)) | ((a) << (32u - (b))))
#define SHA256_CH(x, y, z)     (((x) & (y)) ^ (~(x) & (z)))
#define SHA256_MAJ(x, y, z)    (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define SHA256_EP0(x)  (SHA256_ROTRIGHT(x,2u)^SHA256_ROTRIGHT(x,13u)^SHA256_ROTRIGHT(x,22u))
#define SHA256_EP1(x)  (SHA256_ROTRIGHT(x,6u)^SHA256_ROTRIGHT(x,11u)^SHA256_ROTRIGHT(x,25u))
#define SHA256_SIG0(x) (SHA256_ROTRIGHT(x,7u)^SHA256_ROTRIGHT(x,18u)^((x)>>3u))
#define SHA256_SIG1(x) (SHA256_ROTRIGHT(x,17u)^SHA256_ROTRIGHT(x,19u)^((x)>>10u))

static const uint32_t SHA256_K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

static void sha256_transform(sha256_ctx_t *ctx, const uint8_t d[64])
{
    uint32_t a, b, c, e, f, g, h, t1, t2, m[64];
    uint32_t dd;
    uint32_t i, j;

    for (i = 0u, j = 0u; i < 16u; i++, j += 4u) {
        m[i] = ((uint32_t)d[j] << 24u) | ((uint32_t)d[j + 1u] << 16u)
               | ((uint32_t)d[j + 2u] << 8u) | (uint32_t)d[j + 3u];
    }
    for (; i < 64u; i++) {
        m[i] = SHA256_SIG1(m[i - 2u]) + m[i - 7u]
               + SHA256_SIG0(m[i - 15u]) + m[i - 16u];
    }

    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; dd = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h  = ctx->state[7];

    for (i = 0u; i < 64u; i++) {
        t1 = h + SHA256_EP1(e) + SHA256_CH(e, f, g) + SHA256_K[i] + m[i];
        t2 = SHA256_EP0(a) + SHA256_MAJ(a, b, c);
        h = g; g = f; f = e; e = dd + t1;
        dd = c; c = b; b = a; a = t1 + t2;
    }

    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += dd;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

void sha256_init(sha256_ctx_t *ctx)
{
    ctx->datalen  = 0u;
    ctx->bitlen   = 0u;
    ctx->state[0] = 0x6a09e667u;
    ctx->state[1] = 0xbb67ae85u;
    ctx->state[2] = 0x3c6ef372u;
    ctx->state[3] = 0xa54ff53au;
    ctx->state[4] = 0x510e527fu;
    ctx->state[5] = 0x9b05688cu;
    ctx->state[6] = 0x1f83d9abu;
    ctx->state[7] = 0x5be0cd19u;
}

void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, size_t len)
{
    for (size_t i = 0u; i < len; i++) {
        ctx->data[ctx->datalen++] = data[i];
        if (ctx->datalen == 64u) {
            sha256_transform(ctx, ctx->data);
            ctx->bitlen += 512u;
            ctx->datalen = 0u;
        }
    }
}

void sha256_final(sha256_ctx_t *ctx, uint8_t hash[SHA256_LEN])
{
    uint32_t i = ctx->datalen;

    if (ctx->datalen < 56u) {
        ctx->data[i++] = 0x80u;
        while (i < 56u) {
            ctx->data[i++] = 0x00u;
        }
    } else {
        ctx->data[i++] = 0x80u;
        while (i < 64u) {
            ctx->data[i++] = 0x00u;
        }
        sha256_transform(ctx, ctx->data);
        memset(ctx->data, 0, 56u);
    }
    ctx->bitlen += (uint64_t)ctx->datalen * 8u;
    ctx->data[63] = (uint8_t)(ctx->bitlen);
    ctx->data[62] = (uint8_t)(ctx->bitlen >> 8u);
    ctx->data[61] = (uint8_t)(ctx->bitlen >> 16u);
    ctx->data[60] = (uint8_t)(ctx->bitlen >> 24u);
    ctx->data[59] = (uint8_t)(ctx->bitlen >> 32u);
    ctx->data[58] = (uint8_t)(ctx->bitlen >> 40u);
    ctx->data[57] = (uint8_t)(ctx->bitlen >> 48u);
    ctx->data[56] = (uint8_t)(ctx->bitlen >> 56u);
    sha256_transform(ctx, ctx->data);

    for (i = 0u; i < 4u; i++) {
        hash[i]       = (uint8_t)(ctx->state[0] >> (24u - i * 8u));
        hash[i + 4u]  = (uint8_t)(ctx->state[1] >> (24u - i * 8u));
        hash[i + 8u]  = (uint8_t)(ctx->state[2] >> (24u - i * 8u));
        hash[i + 12u] = (uint8_t)(ctx->state[3] >> (24u - i * 8u));
        hash[i + 16u] = (uint8_t)(ctx->state[4] >> (24u - i * 8u));
        hash[i + 20u] = (uint8_t)(ctx->state[5] >> (24u - i * 8u));
        hash[i + 24u] = (uint8_t)(ctx->state[6] >> (24u - i * 8u));
        hash[i + 28u] = (uint8_t)(ctx->state[7] >> (24u - i * 8u));
    }
}
