// SPDX-License-Identifier: GPL-3.0-only
/**
 * @file    cloud_config.c
 * @author  Qinn
 * @brief   为独立 FRP 示例加载人工配置的设备参数。
 * @details 配置缺失时安全禁用；本文件替代原产品私有的 UID 与根密钥派生流程。
 */
#include "cloud_config.h"
#include "cloud_demo_config.h"
#include "lwip/sockets.h"
#include <string.h>

/** @brief 检查凭据是否由 64 个小写十六进制字符组成。 */
static bool validSecret(const char *text)
{
    size_t index;
    if(strlen(text) != 64U) return false;
    for(index = 0; index < 64U; ++index) {
        if(!((text[index] >= '0' && text[index] <= '9') ||
             (text[index] >= 'a' && text[index] <= 'f'))) return false;
    }
    return true;
}

/** @brief 严格检查配置后复制到运行区；认证引导负责填入代理端口。 */
bool cloudConfigLoad(cloudConfig *output)
{
    struct in_addr address;
    size_t index;
    if(output == NULL) return false;
    memset(output, 0, sizeof(*output));
    if(!CLOUD_DEMO_CONFIGURED) return false;		/* 必须由使用者明确启用本地配置。 */
    if(sizeof(CLOUD_DEMO_SERVER) > sizeof(output->server) ||
       sizeof(CLOUD_DEMO_CA) > sizeof(output->ca) ||
       sizeof(CLOUD_DEMO_DEVICE) != 29U ||
       memcmp(CLOUD_DEMO_DEVICE, "cmu-", 4U) != 0 ||
       !validSecret(CLOUD_DEMO_FRP_TOKEN) || !validSecret(CLOUD_DEMO_DEVICE_SECRET) ||
       inet_pton(AF_INET, CLOUD_DEMO_SERVER, &address) != 1 ||
       CLOUD_DEMO_SERVER_PORT == 0 || CLOUD_DEMO_SERVER_PORT > 65535U ||
       CLOUD_DEMO_TIME_PORT == 0 || CLOUD_DEMO_TIME_PORT > 65535U ||
       CLOUD_DEMO_LOCAL_PORT == 0 || CLOUD_DEMO_LOCAL_PORT > 65535U ||
       strstr(CLOUD_DEMO_CA, "-----BEGIN CERTIFICATE-----") == NULL) return false;
    for(index = 4U; index < 28U; ++index) {
        char value = CLOUD_DEMO_DEVICE[index];
        if(!((value >= '0' && value <= '9') || (value >= 'a' && value <= 'f'))) return false;
    }
    output->serverPort = CLOUD_DEMO_SERVER_PORT;
    output->timePort = CLOUD_DEMO_TIME_PORT;
    output->localPort = CLOUD_DEMO_LOCAL_PORT;
    output->keyVersion = 1U;
    memcpy(output->server, CLOUD_DEMO_SERVER, sizeof(CLOUD_DEMO_SERVER));
    memcpy(output->device, CLOUD_DEMO_DEVICE, sizeof(CLOUD_DEMO_DEVICE));
    memcpy(output->token, CLOUD_DEMO_FRP_TOKEN, sizeof(CLOUD_DEMO_FRP_TOKEN));
    memcpy(output->deviceSecret, CLOUD_DEMO_DEVICE_SECRET, sizeof(CLOUD_DEMO_DEVICE_SECRET));
    memcpy(output->ca, CLOUD_DEMO_CA, sizeof(CLOUD_DEMO_CA));
    return true;
}
