// SPDX-License-Identifier: GPL-3.0-only
/**
 * @file    bootstrap_interop.c
 * @author  Qinn
 * @brief   使用真实固件认证代码生成请求并检查配套服务回包。
 * @details request/reply 文件由主机测试编排器生成，密钥仅为公开的合成测试值。
 */
#include "cloud_bootstrap.h"
#include <stdio.h>
#include <string.h>

/** @brief 跨语言核对认证报文，并验证逐字节篡改、挑战重放与长度错误被拒绝。 */
int main(int argc, char **argv)
{
    unsigned char nonce[16], request[88], reply[68], changed[68];
    char secret[65];
    uint64_t utc;
    uint16_t port;
    unsigned int index;
    FILE *file;
    if(argc != 3) return 1;
    memset(secret, 'a', 64U); secret[64] = 0;
    for(index = 0U; index < 16U; ++index) nonce[index] = (unsigned char)index;
    if(!cloudBootstrapRequest("cmu-000000000000000000000001", secret, 1U, nonce, request)) return 2;
    if(strcmp(argv[1], "request") == 0) {
        file = fopen(argv[2], "wb");
        if(file == NULL) return 3;
        index = (unsigned int)fwrite(request, 1U, sizeof(request), file);
        fclose(file);
        return index == sizeof(request) ? 0 : 4;
    }
    file = fopen(argv[2], "rb");
    if(file == NULL) return 5;
    index = (unsigned int)fread(reply, 1U, sizeof(reply), file);
    fclose(file);
    if(index != sizeof(reply)) return 6;
    if(!cloudBootstrapReply(reply, sizeof(reply), nonce, secret, 1U, &utc, &port) ||
       utc != UINT64_C(1793664000) || port != 18080U) return 7;
    for(index = 0U; index < sizeof(reply); ++index) {
        memcpy(changed, reply, sizeof(reply));
        changed[index] ^= 1U;
        utc = 17U; port = 18U;
        if(cloudBootstrapReply(changed, sizeof(changed), nonce, secret, 1U, &utc, &port) ||
           utc != 17U || port != 18U) return 8;
    }
    nonce[0] ^= 1U;
    if(cloudBootstrapReply(reply, sizeof(reply), nonce, secret, 1U, &utc, &port)) return 9;
    nonce[0] ^= 1U;
    if(cloudBootstrapReply(reply, sizeof(reply)-1U, nonce, secret, 1U, &utc, &port)) return 10;
    puts("PASS: C/Python bootstrap interop, 68 tamper cases, replay and length rejection");
    return 0;
}
