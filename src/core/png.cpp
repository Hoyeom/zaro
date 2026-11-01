#include "core/png.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace zaro {

namespace {

uint32_t g_crc_table[256];

void init_crc() {
    if (g_crc_table[1]) return;
    for (uint32_t n = 0; n < 256; ++n) {
        uint32_t c = n;
        for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        g_crc_table[n] = c;
    }
}

uint32_t crc(uint32_t c, const uint8_t* p, size_t n) {
    for (size_t i = 0; i < n; ++i) c = g_crc_table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c;
}

void put_u32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

bool write_chunk(FILE* f, const char type[4], const uint8_t* data, uint32_t size) {
    uint8_t hdr[8];
    put_u32(hdr, size);
    memcpy(hdr + 4, type, 4);
    uint32_t c = crc(0xFFFFFFFFu, hdr + 4, 4);
    c = crc(c, data, size) ^ 0xFFFFFFFFu;
    uint8_t tail[4];
    put_u32(tail, c);
    return fwrite(hdr, 1, 8, f) == 8 && (size == 0 || fwrite(data, 1, size, f) == size) && fwrite(tail, 1, 4, f) == 4;
}

}  // namespace

bool write_png_rgba8(const char* path, uint32_t width, uint32_t height, const uint8_t* rgba) {
    init_crc();
    const size_t row = (size_t)width * 4 + 1;  // filter byte + pixels
    const size_t raw_size = row * height;
    const size_t blocks = (raw_size + 65534) / 65535;
    const size_t z_size = 2 + raw_size + blocks * 5 + 4;
    uint8_t* raw = (uint8_t*)malloc(raw_size);
    uint8_t* z = (uint8_t*)malloc(z_size);
    if (!raw || !z) {
        free(raw);
        free(z);
        return false;
    }
    for (uint32_t y = 0; y < height; ++y) {
        raw[y * row] = 0;
        memcpy(raw + y * row + 1, rgba + (size_t)y * width * 4, (size_t)width * 4);
    }
    // zlib stream of stored (uncompressed) deflate blocks.
    size_t o = 0;
    z[o++] = 0x78;
    z[o++] = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t pos = 0; pos < raw_size;) {
        const size_t len = raw_size - pos < 65535 ? raw_size - pos : 65535;
        z[o++] = pos + len == raw_size ? 1 : 0;
        z[o++] = (uint8_t)len;
        z[o++] = (uint8_t)(len >> 8);
        z[o++] = (uint8_t)~len;
        z[o++] = (uint8_t)(~len >> 8);
        memcpy(z + o, raw + pos, len);
        for (size_t i = 0; i < len; ++i) {
            a = (a + raw[pos + i]) % 65521;
            b = (b + a) % 65521;
        }
        o += len;
        pos += len;
    }
    put_u32(z + o, (b << 16) | a);
    o += 4;

    bool ok = false;
    FILE* f = fopen(path, "wb");
    if (f) {
        static const uint8_t kSig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
        uint8_t ihdr[13];
        put_u32(ihdr, width);
        put_u32(ihdr + 4, height);
        ihdr[8] = 8;   // bit depth
        ihdr[9] = 6;   // RGBA
        ihdr[10] = ihdr[11] = ihdr[12] = 0;
        ok = fwrite(kSig, 1, 8, f) == 8 && write_chunk(f, "IHDR", ihdr, 13) &&
             write_chunk(f, "IDAT", z, (uint32_t)o) && write_chunk(f, "IEND", nullptr, 0);
        ok = (fclose(f) == 0) && ok;
    }
    free(raw);
    free(z);
    return ok;
}

}  // namespace zaro
