// SPDX-License-Identifier: GPL-3.0-only
/**
 * @file    cloud_config.h
 * @author  Qinn
 * @brief   声明独立示例所需的运行期配置。
 * @details 设备身份和凭据由使用者自行配置，不依赖原产品的 UID 派生或业务平台。
 */
#ifndef CMU_CLOUD_CONFIG_H
#define CMU_CLOUD_CONFIG_H
#include <stdint.h>
#include <stdbool.h>
/** @brief 保存 FRP、认证对时及本机 Web 服务的运行参数。 */
typedef struct {
    uint16_t serverPort, timePort, proxyPort, localPort;
    uint32_t keyVersion;
    char server[64], device[32], token[80], deviceSecret[80], ca[2048];
} cloudConfig;
/** @brief 从本地配置加载参数，缺失或不合规则返回 false 并禁用隧道。 */
bool cloudConfigLoad(cloudConfig *output);
#endif
