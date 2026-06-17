#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned int uint32_t;
typedef int int32_t;

// Base64 编码
// 返回动态分配的字符串，调用者需 free
extern char *base64_encode_manual(const unsigned char *data, size_t len);

// Base64 解码
// 返回动态分配的缓冲区，调用者需 free；decoded_len 返回实际字节数
extern unsigned char *base64_decode_manual(const char *input, size_t *decoded_len);