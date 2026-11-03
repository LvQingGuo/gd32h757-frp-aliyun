// SPDX-License-Identifier: GPL-3.0-only
/**
    ******************************************************************************
    * @file    cloud_bootstrap.c
    * @author  Qinn
    * @brief   生成并校验 UID 自动登记所使用的认证引导报文。
    ******************************************************************************
    * @attention
    * 设备密钥以 64 字符十六进制文本形式提供，并直接作为 HMAC 密钥；不得输出密钥等凭据。
    ******************************************************************************
    * @details
    * 本文件仅负责固定格式请求的生成和回包认证，不负责网络收发。
    * 其实现可由主机测试直接编译，用于验证报文篡改和挑战重放的拒绝行为。
    ******************************************************************************
    */

#include "cloud_bootstrap.h"
#include "mbedtls/md.h"
#include "mbedtls/platform_util.h"
#include <string.h>

/**
 * @brief  使用设备密钥对指定数据计算 HMAC-SHA256。
 * @param  secret 64 字符设备密钥文本，直接作为 HMAC 密钥。
 * @param  data 待认证数据的起始地址。
 * @param  length 待认证数据的字节数。
 * @param  output 接收 32 字节 HMAC 结果的缓冲区。
 * @return 计算成功返回 true；输入密钥为空、长度不符合约定或计算失败时返回 false。
 */
static bool bootstrapHmac(const char *secret, const uint8_t *data, size_t length, uint8_t output[32])
{
    if(secret == NULL || strlen(secret) != 64U) return false;                      		/* 密钥文本必须符合约定长度，否则不计算认证码。 */
    return mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                          (const uint8_t *)secret, 64U, data, length, output) == 0;		/* 使用 SHA-256 计算 HMAC，并写入 32 字节结果。 */
}

/**
 * @brief  按网络字节序读取一个 32 位无符号字段。
 * @param  data 指向至少 4 字节字段数据的缓冲区。
 * @return 将 4 个字节按大端网络字节序组合后的 32 位数值。
 */
static uint32_t readU32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24U) | ((uint32_t)data[1] << 16U) |
           ((uint32_t)data[2] << 8U) | data[3];		/* 按大端网络字节序组合四个字节，得到无符号 32 位字段。 */
}

/**
 * @brief  生成固定格式的 UID 登记请求，并使用 HMAC 认证报文内容。
 * @param  device 设备标识，格式为 cmu- 加 24 个小写十六进制字符。
 * @param  secret 64 字符设备密钥文本，直接作为 HMAC 密钥。
 * @param  version 请求中的版本字段；当前实现要求为 1。
 * @param  nonce 本轮请求使用的 16 字节随机挑战。
 * @param  output 接收 88 字节请求帧的缓冲区。
 * @return 请求生成及 HMAC 计算成功返回 true，否则返回 false。
 */
bool cloudBootstrapRequest(const char *device, const char *secret, uint32_t version,
                           const uint8_t nonce[16], uint8_t output[CLOUD_BOOTSTRAP_REQUEST_SIZE])
{
    size_t index;
    if(device == NULL || nonce == NULL || output == NULL || version != 1U ||
       strlen(device) != 28U || memcmp(device, "cmu-", 4U) != 0) return false;               		/* 输入指针、版本号或设备标识格式不符合约定时拒绝生成请求。 */
    for(index = 4U; index < 28U; ++index) {                                                  		/* 前缀已校验为 cmu-，此处逐位检查后续 24 个字符。 */
        char value = device[index];
        if(!((value >= '0' && value <= '9') || (value >= 'a' && value <= 'f'))) return false;		/* 后缀仅允许数字和小写 a 至 f。 */
    }
    memset(output, 0, CLOUD_BOOTSTRAP_REQUEST_SIZE);                                         		/* 先清零整帧，使保留字段保持为零后再填充有效字段。 */
    memcpy(output, "CMU2", 4U);                                                              		/* 写入请求帧标识，供接收端识别报文类型。 */
    output[4] = (uint8_t)(version >> 24U);
    output[5] = (uint8_t)(version >> 16U);
    output[6] = (uint8_t)(version >> 8U);
    output[7] = (uint8_t)version;                                                            		/* 将版本号按网络字节序写入 4 字节字段。 */
    memcpy(output + 8U, device, 28U);                                                        		/* 将通过格式校验的设备标识写入固定字段。 */
    memcpy(output + 40U, nonce, 16U);                                                        		/* 将本轮随机挑战纳入待认证数据，回包需返回相同挑战。 */
    return bootstrapHmac(secret, output, 56U, output + 56U);                                 		/* 认证前 56 字节，并把 32 字节 HMAC 写入请求尾部。 */
}

/**
 * @brief  严格验证认证回包，并在全部校验通过后输出 UTC 时间和代理端口。
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
                         const char *secret, uint32_t version, uint64_t *utc, uint16_t *port)
{
    uint8_t expected[32], difference = 0U;
    uint64_t seconds = 0U;
    uint32_t assigned;
    size_t index;
    if(reply == NULL || nonce == NULL || utc == NULL || port == NULL ||
       length != CLOUD_BOOTSTRAP_REPLY_SIZE || memcmp(reply, "CMR2", 4U) != 0 ||
       memcmp(reply + 4U, nonce, 16U) != 0 || readU32(reply + 32U) != version ||
       !bootstrapHmac(secret, reply, 36U, expected)) return false;                           		/* 先校验输入、帧标识、挑战和版本，再计算期望 HMAC。 */
    for(index = 0U; index < 32U; ++index) difference |= expected[index] ^ reply[index + 36U];		/* 汇总全部 32 字节 HMAC 的差异，避免逐字节提前退出比较。 */
    mbedtls_platform_zeroize(expected, sizeof(expected));                                    		/* 清除临时保存的期望 HMAC，避免认证材料残留在缓冲区中。 */
    if(difference != 0U) return false;                                                       		/* 只要 HMAC 有任意一位不一致，就拒绝该回包。 */
    for(index = 20U; index < 28U; ++index) seconds = (seconds << 8U) | reply[index];         		/* 按网络字节序读取 8 字节 UTC 秒数。 */
    assigned = readU32(reply + 28U);                                                         		/* 读取云端分配的代理端口字段。 */
    if(seconds < UINT64_C(1767225600) || seconds > UINT64_C(4102444800) ||
       assigned < 1024U || assigned > 65535U) return false;                                  		/* 拒绝超出实现设定范围的 UTC 时间戳或代理端口。 */
    *utc = seconds;                                                                          		/* 只有全部认证和范围检查通过后，才更新调用方的输出参数。 */
    *port = (uint16_t)assigned;                                                              		/* 输出已经验证且可由 uint16_t 表示的代理端口。 */
    return true;
}
