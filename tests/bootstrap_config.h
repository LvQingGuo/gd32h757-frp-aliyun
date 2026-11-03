// SPDX-License-Identifier: GPL-3.0-only
/**
 * @file    bootstrap_config.h
 * @author  Qinn
 * @brief   主机认证引导测试所需的 Mbed TLS 最小配置。
 */
#ifndef BOOTSTRAP_TEST_CONFIG_H
#define BOOTSTRAP_TEST_CONFIG_H
#define MBEDTLS_MD_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_PLATFORM_C
#endif
