// SPDX-License-Identifier: GPL-3.0-only
/**
 ****************************************************************************
 * @file    cmu_mbedtls_config.h
 * @author  Qinn
 * @brief   配置远程调试使用的 TLS 1.2、P-256、ECDHE-ECDSA 和 AES-GCM 算法子集。
 ****************************************************************************
 * @attention
 * 密码库通过 cloud_port 适配堆、时间和硬件熵；不得关闭证书验证。
 ****************************************************************************
 * @details
 * SHA1、PBKDF2 和 AES-CFB 仅用于兼容 FRP v1 控制流，不用于 TLS 套件。
 ****************************************************************************
 */

#ifndef CMU_MBEDTLS_CONFIG_H
#define CMU_MBEDTLS_CONFIG_H
#include <stdint.h>		/* 为时间接口提供 int64_t 等固定宽度整数类型。 */
#define MBEDTLS_PLATFORM_C                       		/* 启用 Mbed TLS 平台适配层。 */
#define MBEDTLS_PLATFORM_MEMORY                  		/* 使用 cloud_port 注册的受限内存分配与释放接口。 */
#define MBEDTLS_PLATFORM_TIME_TYPE_MACRO int64_t 		/* 将平台时间类型指定为 64 位有符号整数。 */
#define MBEDTLS_PLATFORM_TIME_MACRO cloudMbedTime		/* 使用 cloudMbedTime 提供经认证校准的 UTC 时间。 */
/**
 * @brief  为 Mbed TLS 提供经过认证校时的 64 位 UTC 时间。
 * @param  output 非空时用于接收当前时间；允许传入 NULL。
 * @return 当前 UTC 时间值。
 */
int64_t cloudMbedTime(int64_t *output);
#define MBEDTLS_PLATFORM_GMTIME_R_ALT		/* 使用项目提供的可重入 UTC 日期转换实现。 */
#define MBEDTLS_PLATFORM_MS_TIME_ALT 		/* 使用项目提供的毫秒级单调计时实现。 */
#define MBEDTLS_HAVE_TIME            		/* 启用证书验证所需的时间支持。 */
#define MBEDTLS_HAVE_TIME_DATE       		/* 启用证书有效期的日期检查。 */
#define MBEDTLS_AES_C          		/* 启用 AES 分组密码实现。 */
#define MBEDTLS_CIPHER_C       		/* 启用通用密码算法封装层。 */
#define MBEDTLS_CIPHER_MODE_CFB		/* 启用 FRP v1 控制流兼容所需的 CFB 模式。 */
#define MBEDTLS_GCM_C          		/* 启用 TLS 套件使用的 GCM 认证加密模式。 */
#define MBEDTLS_MD_C    		/* 启用消息摘要算法统一接口。 */
#define MBEDTLS_MD5_C   		/* 启用 FRP v1 认证字段所需的 MD5。 */
#define MBEDTLS_SHA1_C  		/* 启用 PBKDF2 派生所需的 SHA-1。 */
#define MBEDTLS_SHA256_C		/* 启用设备身份派生及 HMAC-SHA256。 */
#define MBEDTLS_PKCS5_C   		/* 启用 PBKDF2 密钥派生功能。 */
#define MBEDTLS_CTR_DRBG_C		/* 启用 TLS 随机数生成器上下文。 */
#define MBEDTLS_HAVE_ASM  		/* 允许使用密码库支持的汇编优化实现。 */
#define MBEDTLS_BIGNUM_C                		/* 启用大整数运算。 */
#define MBEDTLS_ECP_C                   		/* 启用椭圆曲线密码基础实现。 */
#define MBEDTLS_ECP_NIST_OPTIM          		/* 启用 NIST 曲线优化。 */
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED		/* 启用 TLS 使用的 secp256r1（P-256）曲线。 */
#define MBEDTLS_ECP_WINDOW_SIZE 2       		/* 设置椭圆曲线运算窗口大小，以控制空间与性能开销。 */
#define MBEDTLS_MPI_WINDOW_SIZE 1       		/* 设置大整数幂运算窗口大小。 */
#define MBEDTLS_MPI_MAX_SIZE 256        		/* 限制大整数最大存储字节数。 */
#define MBEDTLS_ECDH_C 		/* 启用椭圆曲线 Diffie-Hellman 密钥协商。 */
#define MBEDTLS_ECDSA_C		/* 启用椭圆曲线数字签名算法。 */
#define MBEDTLS_ASN1_PARSE_C		/* 启用 ASN.1 数据解析。 */
#define MBEDTLS_ASN1_WRITE_C		/* 启用 ASN.1 数据编码。 */
#define MBEDTLS_BASE64_C    		/* 启用 Base64 编解码。 */
#define MBEDTLS_OID_C       		/* 启用对象标识符处理。 */
#define MBEDTLS_PEM_PARSE_C 		/* 启用 PEM 格式证书解析。 */
#define MBEDTLS_PK_C        		/* 启用通用公钥算法接口。 */
#define MBEDTLS_PK_PARSE_C  		/* 启用公钥数据解析。 */
#define MBEDTLS_X509_USE_C      		/* 启用 X.509 证书基础支持。 */
#define MBEDTLS_X509_CRT_PARSE_C		/* 启用 X.509 证书链解析与验证所需功能。 */
#define MBEDTLS_SSL_TLS_C                       		/* 启用 TLS 协议核心实现。 */
#define MBEDTLS_SSL_CLI_C                       		/* 启用 TLS 客户端功能。 */
#define MBEDTLS_SSL_PROTO_TLS1_2                		/* 仅启用 TLS 1.2 协议版本。 */
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED		/* 启用 ECDHE-ECDSA 密钥协商与认证套件。 */
#define MBEDTLS_SSL_IN_CONTENT_LEN 16384		/* 设置 TLS 输入记录缓冲区长度为 16384 字节。 */
#define MBEDTLS_SSL_OUT_CONTENT_LEN 4096		/* 设置 TLS 输出记录缓冲区长度为 4096 字节。 */
#endif /* CMU_MBEDTLS_CONFIG_H */
