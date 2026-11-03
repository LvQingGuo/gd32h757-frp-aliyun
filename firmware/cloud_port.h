// SPDX-License-Identifier: GPL-3.0-only
/**
 ******************************************************************************
 * @file    cloud_port.h
 * @author  Qinn
 * @brief   声明远程调试所需的受限内存、硬件随机数、认证对时和 TLS Socket 适配接口。
 ******************************************************************************
 * @attention
 * TLS 连接必须验证证书链、目标主机及证书有效期；发生错误时只能中止远程服务，
 * 不得影响设备的正常采集与通信业务。
 ******************************************************************************
 * @details
 * 每个 TLS 实例独立持有 CA、随机数生成器和会话上下文，避免不同工作任务共享
 * 可变密码上下文。
 ******************************************************************************
 */

#ifndef CMU_CLOUD_PORT_H
#define CMU_CLOUD_PORT_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief 表示一条独立 TLS 连接的不透明类型。
 * @details 结构体成员仅在 cloud_port.c 内定义和访问。
 */
typedef struct cloudTls cloudTls;
/**
 * @brief 初始化硬件 TRNG 及 Mbed TLS 平台内存分配回调。
 * @details 应在创建云端远程任务前调用一次。
 */
void cloudPortInit(void);
/**
 * @brief 从硬件 TRNG 获取随机字节。
 * @param context 熵回调上下文；当前实现不使用该参数。
 * @param output 用于接收随机字节的缓冲区。
 * @param length 请求的随机字节数。
 * @return 成功返回 0；硬件健康检查失败或等待超时返回 -1。
 */
int cloudRandom(void *context, unsigned char *output, size_t length);
/**
 * @brief 获取当前认证 UTC 时间。
 * @return 当前 UTC 秒数；认证对时尚未成功时返回 0。
 */
uint64_t cloudUtcNow(void);
/**
 * @brief 使用设备密钥和随机挑战执行认证对时，并获取云端分配的代理端口。
 * @param host 认证引导服务器的 IPv4 地址文本。
 * @param port 认证引导服务器的 UDP 端口。
 * @param device 设备标识。
 * @param secret 设备 HMAC 密钥文本。
 * @param keyVersion 产品密钥版本。
 * @param proxyPort 接收云端分配代理端口的输出指针。
 * @return 认证与校验成功返回 true，否则返回 false。
 */
bool cloudSyncTime(const char *host, uint16_t port, const char *device, const char *secret,
                   uint32_t keyVersion, uint16_t *proxyPort);
/**
 * @brief 建立有超时限制的非阻塞 IPv4 TCP 连接。
 * @param host 目标服务器的 IPv4 地址文本。
 * @param port 目标 TCP 端口。
 * @param timeoutMs 建立连接时的最大等待时间，单位为毫秒。
 * @return 成功返回 Socket 描述符；失败返回 -1 并关闭已创建的 Socket。
 */
int cloudSocketConnect(const char *host, uint16_t port, uint32_t timeoutMs);
/**
 * @brief 建立并验证独立的 TLS 1.2 客户端连接。
 * @param host 目标服务器的 IPv4 地址文本。
 * @param port 目标服务器的 TCP 端口。
 * @param ca 可信 CA 证书的 PEM 文本。
 * @return 握手和证书验证成功返回连接对象；失败返回 NULL。
 */
cloudTls *cloudTlsOpen(const char *host, uint16_t port, const char *ca);
/**
 * @brief 从 TLS 连接读取解密后的应用数据。
 * @param connection 已建立的 TLS 连接对象。
 * @param data 接收明文的缓冲区。
 * @param length 最多读取的字节数。
 * @return Mbed TLS 的读取结果；WANT_READ/WANT_WRITE 等状态由调用方继续处理。
 */
int cloudTlsRead(cloudTls *connection, void *data, size_t length);
/**
 * @brief 在有界等待内发送全部 TLS 明文。
 * @param connection 已建立的 TLS 连接对象。
 * @param data 待发送的明文缓冲区。
 * @param length 待发送的字节数。
 * @return 全部发送成功返回 true；TLS 错误或超时返回 false。
 */
bool cloudTlsWrite(cloudTls *connection, const void *data, size_t length);
/**
 * @brief 查询 TLS 连接对应的底层 Socket。
 * @param connection TLS 连接对象。
 * @return 底层 Socket 描述符；不转移其关闭所有权。
 */
int cloudTlsSocket(const cloudTls *connection);
/**
 * @brief 检查 TLS 内部是否已有待读取的明文数据。
 * @param connection TLS 连接对象。
 * @return 存在待处理明文时返回 true，否则返回 false。
 */
bool cloudTlsPending(const cloudTls *connection);
/**
 * @brief 关闭 TLS 连接并释放其全部会话资源。
 * @param connection 待关闭的 TLS 连接；允许传入 NULL。
 */
void cloudTlsClose(cloudTls *connection);
/**
 * @brief 查询密码库动态内存计费信息。
 * @param current 非 NULL 时接收当前分配总量。
 * @param peak 非 NULL 时接收历史分配峰值。
 */
void cloudMemoryStats(size_t *current, size_t *peak);
#endif
