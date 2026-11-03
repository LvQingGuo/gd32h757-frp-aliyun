// SPDX-License-Identifier: GPL-3.0-only
/**
 * @file    cloud_demo_config.example.h
 * @author  Qinn
 * @brief   仅含占位符的配置模板，复制为 cloud_demo_config.h 后自行配置。
 * @details 所有凭据必须重新生成；示例地址属于文档地址段，不是在线演示入口。
 */
#ifndef CLOUD_DEMO_CONFIG_H
#define CLOUD_DEMO_CONFIG_H
#define CLOUD_DEMO_CONFIGURED 0
#define CLOUD_DEMO_SERVER "192.0.2.10"
#define CLOUD_DEMO_SERVER_PORT 7000U
#define CLOUD_DEMO_TIME_PORT 7000U
#define CLOUD_DEMO_LOCAL_PORT 8080U
#define CLOUD_DEMO_DEVICE "cmu-000000000000000000000001"
#define CLOUD_DEMO_FRP_TOKEN "REPLACE_WITH_64_LOWERCASE_HEX_CHARACTERS"
#define CLOUD_DEMO_DEVICE_SECRET "REPLACE_WITH_DIFFERENT_64_LOWERCASE_HEX_CHARACTERS"
#define CLOUD_DEMO_CA "REPLACE_WITH_YOUR_OWN_PEM_CA_CERTIFICATE"
#endif
