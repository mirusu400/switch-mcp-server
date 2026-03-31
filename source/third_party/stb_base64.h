// stb_base64.h - public domain base64 encode/decode by Jeff Bezanson, 2010
// https://github.com/nothings/stb/blob/master/stb_base64.h
// Only the encode/decode functions, minimal version for easy integration
#ifndef STB_BASE64_H
#define STB_BASE64_H

#include <stddef.h>
#include <stdint.h>

static const char stb_b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int stb_base64_encode(const unsigned char *data, size_t len, char *out)
{
    size_t i, j;
    for (i = 0, j = 0; i + 2 < len; i += 3) {
        out[j++] = stb_b64_table[(data[i] >> 2) & 0x3F];
        out[j++] = stb_b64_table[((data[i] & 0x3) << 4) | ((data[i+1] >> 4) & 0xF)];
        out[j++] = stb_b64_table[((data[i+1] & 0xF) << 2) | ((data[i+2] >> 6) & 0x3)];
        out[j++] = stb_b64_table[data[i+2] & 0x3F];
    }
    if (i < len) {
        out[j++] = stb_b64_table[(data[i] >> 2) & 0x3F];
        if (i + 1 < len) {
            out[j++] = stb_b64_table[((data[i] & 0x3) << 4) | ((data[i+1] >> 4) & 0xF)];
            out[j++] = stb_b64_table[(data[i+1] & 0xF) << 2];
            out[j++] = '=';
        } else {
            out[j++] = stb_b64_table[(data[i] & 0x3) << 4];
            out[j++] = '=';
            out[j++] = '=';
        }
    }
    out[j] = '\0';
    return (int)j;
}


static int stb_base64_decode(const char *src, unsigned char *out, size_t out_max)
{
    static const unsigned char d[] = {
        255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
        255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
        255,255,255,255,255,255,255,255,255,255,255, 62,255,255,255, 63,
         52, 53, 54, 55, 56, 57, 58, 59, 60, 61,255,255,255,  0,255,255,
        255,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
         15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,255,255,255,255,255,
        255, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
         41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51,255,255,255,255,255
    };
    size_t j = 0;
    unsigned char a[4];
    int pad = 0, n = 0;

    while (*src) {
        unsigned char c = (unsigned char)*src++;
        if (c == '=' ) { pad++; a[n++] = 0; }
        else if (c == '\r' || c == '\n' || c == ' ') continue;
        else if (c > 127 || d[c] == 255) return -1;
        else { a[n++] = d[c]; }

        if (n == 4) {
            if (j + 3 - pad > out_max) return -1;
            out[j++] = (a[0] << 2) | (a[1] >> 4);
            if (pad < 2) out[j++] = (a[1] << 4) | (a[2] >> 2);
            if (pad < 1) out[j++] = (a[2] << 6) | a[3];
            n = 0; pad = 0;
        }
    }
    return (int)j;
}

#endif // STB_BASE64_H
