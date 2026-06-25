#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "base64.h"

static const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// 辅助函数：根据字符查找索引
static int base64_char_index(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1; // 无效字符
}

// Base64 编码
// 返回动态分配的字符串，调用者需 free
char *base64_encode_manual(const unsigned char *data, size_t len) {
    if (data == NULL) return NULL;

    // 计算输出长度：每3字节变4字符，加上填充 '='，最后加 '\0'
    size_t out_len = 4 * ((len + 2) / 3);
    char *out = (char *)malloc(out_len + 1);
    if (!out) return NULL;

    size_t i = 0, j = 0;
    while (i < len) {
        uint32_t octet_a = i < len ? data[i++] : 0;
        uint32_t octet_b = i < len ? data[i++] : 0;
        uint32_t octet_c = i < len ? data[i++] : 0;

        uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

        out[j++] = base64_chars[(triple >> 18) & 0x3F];
        out[j++] = base64_chars[(triple >> 12) & 0x3F];
        out[j++] = base64_chars[(triple >> 6) & 0x3F];
        out[j++] = base64_chars[triple & 0x3F];
    }

    // 处理填充
    if (len % 3 == 1) {
        out[out_len - 1] = '=';
        out[out_len - 2] = '=';
    } else if (len % 3 == 2) {
        out[out_len - 1] = '=';
    }
    
    out[out_len] = '\0';
    return out;
}

// Base64 解码
// 返回动态分配的缓冲区，调用者需 free；decoded_len 返回实际字节数
unsigned char *base64_decode_manual(const char *input, size_t *decoded_len) {
    if (input == NULL) return NULL;

    size_t in_len = strlen(input);
    if (in_len % 4 != 0) return NULL; // 长度必须是4的倍数

    // 计算输出最大长度
    size_t out_len = (in_len / 4) * 3;
    unsigned char *out = (unsigned char *)malloc(out_len + 1);
    if (!out) return NULL;

    size_t i = 0, j = 0;
    while (i < in_len) {
        int a = base64_char_index(input[i++]);
        int b = base64_char_index(input[i++]);
        int c = base64_char_index(input[i++]);
        int d = base64_char_index(input[i++]);

        if (a == -1 || b == -1) {
            free(out);
            return NULL;
        }

        uint32_t triple = (a << 18) | (b << 12) | ((c != -1) ? (c << 6) : 0) | ((d != -1) ? d : 0);

        out[j++] = (triple >> 16) & 0xFF;
        if (c != -1) out[j++] = (triple >> 8) & 0xFF;
        if (d != -1) out[j++] = triple & 0xFF;
    }

    *decoded_len = j;
    return out;
}

