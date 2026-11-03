// SPDX-License-Identifier: GPL-3.0-only
/**
    ******************************************************************************
    * @file    cloud_bootstrap.h
    * @author  Qinn
    * @brief   声明 UID 自动登记使用的固定长度 HMAC 认证引导报文接口。
    ******************************************************************************
    * @attention
    * 回包必须同时通过长度、随机挑战、版本字段和 HMAC 校验。
    ******************************************************************************
    * @details
    * 请求固定为 88 字节，回包固定为 68 字节；UTC、代理端口和版本字段使用网络字节序。
    ******************************************************************************
    */

#ifndef CMU_CLOUD_BOOTSTRAP_H
#define CMU_CLOUD_BOOTSTRAP_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#define CLOUD_BOOTSTRAP_REQUEST_SIZE 88U		/* 请求帧总长度，包含 32 字节 HMAC。 */
#define CLOUD_BOOTSTRAP_REPLY_SIZE 68U  		/* 回包总长度，包含 32 字节 HMAC。 */

/**
 * @brief  生成包含调用方随机挑战的认证登记请求，不执行网络收发。
 * @param  device 设备标识，格式为 cmu- 加 24 个小写十六进制字符。
 * @param  secret 64 字符设备密钥文本，直接作为 HMAC 密钥。
 * @param  version 请求中的版本字段；当前实现要求为 1。
 * @param  nonce 本轮请求使用的 16 字节随机挑战。
 * @param  output 接收 88 字节请求帧的缓冲区。
 * @return 请求生成及 HMAC 计算成功返回 true，否则返回 false。
 */
bool cloudBootstrapRequest(const char *device, const char *secret, uint32_t version,
                           const uint8_t nonce[16], uint8_t output[CLOUD_BOOTSTRAP_REQUEST_SIZE]);

/**
 * @brief  严格验证认证回包；失败时不更改调用方的时间和端口输出。
 * @param  reply 收到的回包数据。
 * @param  length 回包长度，必须等于 CLOUD_BOOTSTRAP_REPLY_SIZE。
 * @param  nonce 发起请求时使用的 16 字节随机挑战。
 * @param  secret 64 字符设备密钥文本，直接作为 HMAC 密钥。
 * @param  version 预期的回包版本字段。
 * @param  utc 接收已验证 UTC 秒数的输出指针。
 * @param  port 接收已验证代理端口的输出指针。
 * @return 所有格式、挑战、版本、HMAC 和字段范围检查通过时返回 true；否则返回 false，且不修改输出参数。
 */
bool cloudBootstrapReply(const uint8_t *reply, size_t length, const uint8_t nonce[16],
                         const char *secret, uint32_t version, uint64_t *utc, uint16_t *port);
#endif
