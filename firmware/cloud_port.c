// SPDX-License-Identifier: GPL-3.0-only
/**
 ******************************************************************************
 * @file    cloud_port.c
 * @author  Qinn
 * @brief   实现 FreeRTOS、GD32 TRNG、认证 UTC 及 lwIP 上的 Mbed TLS 适配。
 ******************************************************************************
 * @attention
 * 密码库动态分配总量限制为 192 KiB，并至少为公共堆保留 48 KiB；远程服务失败
 * 不得阻塞或拖停设备的正常采集业务。
 ******************************************************************************
 * @details
 * 本文件负责硬件随机数、认证对时、UTC/单调时间、受限内存分配及 TLS Socket 适配。
 * 对时通过设备 HMAC 密钥验证随机挑战响应，证书有效期检查不依赖未认证 NTP。
 ******************************************************************************
 */

#include "cloud_port.h"
#include "cloud_bootstrap.h"
#include "FreeRTOS.h"
#include "task.h"
#include "gd32h7xx.h"
#include "gd32h7xx_trng.h"
#include "lwip/sockets.h"
#include "mbedtls/platform.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/ssl.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/md.h"
#include "mbedtls/x509_crt.h"
#include "cloud_log.h"
#include <string.h>
#include <limits.h>

/* 限制密码库动态分配总量，避免远程 TLS 消耗过多公共堆。 */
#define CLOUD_CRYPTO_LIMIT (192U * 1024U)
/* 为采集及其他业务任务保留最低公共堆空间。 */
#define CLOUD_HEAP_RESERVE (48U * 1024U)

/**
 * @brief 密码库动态分配块的内部管理头。
 * @details 分配头位于用户缓冲区之前，用于记录完整分配长度并在释放时准确计费。
 */
typedef struct {
    size_t length;  		/* 本次分配的总字节数，包含管理头本身。 */
    size_t reserved;		/* 保留字段，属于分配头的固定布局。 */
} cloudAllocation;

/**
 * @brief 单条 TLS 连接的私有上下文。
 * @details 每个工作连接单独持有 Socket、TLS 会话、证书链和随机数上下文。
 */
struct cloudTls {
    int fd;                      		/* 底层 lwIP Socket 描述符。 */
    mbedtls_ssl_context ssl;     		/* TLS 会话状态及收发缓冲。 */
    mbedtls_ssl_config config;   		/* 当前连接使用的 TLS 配置。 */
    mbedtls_x509_crt ca;         		/* 本连接加载的可信 CA 证书链。 */
    mbedtls_ctr_drbg_context rng;		/* 本连接独立的 DRBG 上下文。 */
};

/* 下列状态用于统计密码库内存、维护时间基准和检测 TRNG 健康状态。 */
static size_t sAllocated;              		/* 当前计费中的密码库分配总量。 */
static size_t sPeak;                   		/* 密码库分配总量的历史峰值。 */
static uint64_t sUtcMilliseconds;      		/* 最近一次认证对时建立的 UTC 毫秒基准。 */
static TickType_t sTimeTick;           		/* UTC 基准对应的 FreeRTOS Tick。 */
static uint64_t sMonotonicMilliseconds;		/* 独立单调毫秒计数，不受 UTC 校时影响。 */
static TickType_t sMonotonicTick;      		/* 单调计时上次累计时对应的 Tick。 */
static unsigned int sRandomDiscard;    		/* TRNG 恢复后需要丢弃的初始输出字数。 */
static uint32_t sRandomPrevious;       		/* 上一次接受的 TRNG 32 位输出。 */
static bool sRandomHavePrevious;       		/* 是否已有可用于重复值检查的输出。 */

/**
 * @brief 获取独立的 64 位单调毫秒计数。
 * @details 该计时源仅根据 FreeRTOS Tick 累加，不随认证 UTC 校时而跳变。
 * @return 自本模块开始累计以来的单调毫秒数。
 */
mbedtls_ms_time_t mbedtls_ms_time(void)
{
    uint64_t value;
    TickType_t now;
    taskENTER_CRITICAL();
    now = xTaskGetTickCount();
    sMonotonicMilliseconds += (uint64_t)(TickType_t)(now - sMonotonicTick) * portTICK_PERIOD_MS;		/* 用无符号 Tick 差处理计数器回绕。 */
    sMonotonicTick = now;
    value = sMonotonicMilliseconds;
    taskEXIT_CRITICAL();
    return (mbedtls_ms_time_t)value;
}

/**
 * @brief 为 Mbed TLS 分配受限且清零的内存块。
 * @param count 待分配元素数量。
 * @param size 每个元素的字节数。
 * @return 分配成功返回清零后的用户缓冲区；超限或内存不足时返回 NULL。
 */
static void *cloudCalloc(size_t count, size_t size)
{
    cloudAllocation *memory;
    size_t total;
    if((size != 0U) && (count > (SIZE_MAX - sizeof(*memory)) / size)) return NULL; 		/* 分配前检查乘法与管理头相加是否溢出。 */
    total = count * size + sizeof(*memory);                                        		/* 计费长度包含用户区前方的分配管理头。 */
    taskENTER_CRITICAL();
    if((total > CLOUD_CRYPTO_LIMIT) || (sAllocated > CLOUD_CRYPTO_LIMIT - total) ||
       (xPortGetFreeHeapSize() < total + CLOUD_HEAP_RESERVE)) {
        taskEXIT_CRITICAL();
        return NULL;
    }
    sAllocated += total;                                                           		/* 先预占计费额度，防止并发分配突破总量上限。 */
    if(sAllocated > sPeak) sPeak = sAllocated;
    taskEXIT_CRITICAL();
    memory = pvPortMalloc(total);
    if(memory == NULL) {
        taskENTER_CRITICAL();
        sAllocated -= total;
        taskEXIT_CRITICAL();
        return NULL;
    }
    memset(memory, 0, total);
    memory->length = total;
    return memory + 1;
}

/**
 * @brief 擦除并释放由 cloudCalloc 分配的内存块。
 * @param pointer 指向分配给调用方的缓冲区；允许传入 NULL。
 */
static void cloudFree(void *pointer)
{
    cloudAllocation *memory;
    size_t length;
    if(pointer == NULL) return;
    memory = (cloudAllocation *)pointer - 1;
    length = memory->length;
    mbedtls_platform_zeroize(memory, length);		/* 释放前擦除整个分配块，包括管理头和用户数据。 */
    vPortFree(memory);
    taskENTER_CRITICAL();
    sAllocated -= length;
    taskEXIT_CRITICAL();
}

/**
 * @brief 获取当前认证 UTC 秒数。
 * @details 使用无符号 Tick 差值累计毫秒，支持 Tick 计数回绕；未完成认证对时时返回 0。
 * @return 当前 UTC 秒数；尚未建立有效时间基准时返回 0。
 */
uint64_t cloudUtcNow(void)
{
    uint64_t result;
    TickType_t now;
    taskENTER_CRITICAL();
    now = xTaskGetTickCount();
    if(sUtcMilliseconds != 0U) {
        sUtcMilliseconds += (uint64_t)(TickType_t)(now - sTimeTick) * portTICK_PERIOD_MS;
    }
    sTimeTick = now;
    result = sUtcMilliseconds / 1000U;                                                   		/* 对外接口统一返回整秒 UTC。 */
    taskEXIT_CRITICAL();
    return result;
}

/**
 * @brief 向 Mbed TLS 提供当前 UTC 时间。
 * @param output 非 NULL 时用于接收当前时间；可传入 NULL。
 * @return 当前 UTC 时间秒数。
 */
mbedtls_time_t cloudMbedTime(mbedtls_time_t *output)
{
    mbedtls_time_t value = (mbedtls_time_t)cloudUtcNow();
    if(output != NULL) *output = value;
    return value;
}

/**
 * @brief 将受支持范围内的 UTC 秒数转换为日历日期。
 * @param seconds 指向 Unix UTC 秒数的指针。
 * @param output 用于接收年月日、时分秒等字段的结构体。
 * @return 转换成功返回 output；参数为空或时间超出支持范围时返回 NULL。
 */
struct tm *mbedtls_platform_gmtime_r(const mbedtls_time_t *seconds, struct tm *output)
{
    static const unsigned int months[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    uint64_t days;
    unsigned int year = 1970U, month = 0U, span;
    bool leap;
    if((seconds == NULL) || (output == NULL) || (*seconds < 0) ||
       ((uint64_t)*seconds > UINT64_C(4102444800))) return NULL;                       		/* 拒绝空参数、负时间及 2100 年后的时间。 */
    memset(output, 0, sizeof(*output));
    days = (uint64_t)*seconds / 86400U;
    output->tm_sec = (int)(*seconds % 60);
    output->tm_min = (int)((*seconds / 60) % 60);
    output->tm_hour = (int)((*seconds / 3600) % 24);
    output->tm_wday = (int)((days + 4U) % 7U);                                         		/* Unix epoch 的起始日为星期四。 */
    for(;;) {
        leap = ((year % 4U) == 0U) && (((year % 100U) != 0U) || ((year % 400U) == 0U));		/* 按公历规则判断闰年。 */
        span = leap ? 366U : 365U;
        if(days < span) break;
        days -= span;
        ++year;
    }
    output->tm_yday = (int)days;
    for(month = 0U; month < 11U; ++month) {
        span = months[month] + (((month == 1U) && leap) ? 1U : 0U);
        if(days < span) break;
        days -= span;
    }
    output->tm_year = (int)year - 1900;
    output->tm_mon = (int)month;
    output->tm_mday = (int)days + 1;
    return output;
}

/**
 * @brief 按 GD32H7 TRNG 的 CONDRST 流程恢复随机数健康检测。
 * @details 本函数由调用方在临界区内调用，并重置初始输出丢弃及重复值检测状态。
 */
static void restartRandom(void)
{
    trng_disable();
    trng_conditioning_reset_enable();
    TRNG_CTL |= TRNG_CTL_TRNGEN;                  		/* 在条件化复位流程中重新使能 TRNG。 */
    trng_conditioning_reset_disable();
    trng_interrupt_flag_clear(TRNG_INT_FLAG_SEIF);
    trng_interrupt_flag_clear(TRNG_INT_FLAG_CEIF);
    sRandomDiscard = 4U;                          		/* 恢复后先丢弃四个输出字，再重新接收随机数据。 */
    sRandomHavePrevious = false;
}

/**
 * @brief 从硬件 TRNG 获取随机字节，并检查硬件健康状态。
 * @param context 兼容 Mbed TLS 熵回调接口的上下文；本实现不使用该参数。
 * @param output 用于接收随机字节的输出缓冲区。
 * @param length 请求的随机字节数。
 * @return 全部随机数据获取成功返回 0；健康检查恢复失败或等待超时返回 -1。
 * @details 检测到错误时清空本次输出，最多恢复并重试两次；不会退回到伪随机数据。
 */
int cloudRandom(void *context, unsigned char *output, size_t length)
{
    TickType_t start = xTaskGetTickCount();
    size_t copied = 0U;
    unsigned int recoveries = 0U;
    (void)context;
    while(copied < length) {
        uint32_t word = 0U;
        bool ready, failed;
        uint32_t status;
        bool repeated = false;
        size_t part = length - copied;
        taskENTER_CRITICAL();
        status = TRNG_STAT;
        failed = (status & (TRNG_STAT_CECS | TRNG_STAT_SECS | TRNG_STAT_ERRSTA |
                              TRNG_STAT_CEIF | TRNG_STAT_SEIF)) != 0U;                                      		/* 同时检查时钟、种子及健康检测错误标志。 */
        ready = !failed && (trng_flag_get(TRNG_FLAG_DRDY) == SET);                                          		/* 只有无错误且数据就绪时才读取硬件输出。 */
        if(ready) {
            word = trng_get_true_random_data();
            if(sRandomDiscard != 0U) { --sRandomDiscard; ready = false; }                                   		/* 跳过恢复后的预热输出。 */
            else if(sRandomHavePrevious && word == sRandomPrevious) { failed = true; repeated = true; }     		/* 连续相同的 32 位输出触发健康检查失败。 */
            sRandomPrevious = word;
            sRandomHavePrevious = true;
        }
        if(failed) restartRandom();                                                                         		/* 在临界区内重置硬件熵源及软件检测状态。 */
        taskEXIT_CRITICAL();
        if(failed) {
            APP_LOG_WARNING("云端隧道", "硬件熵健康检查失败：STAT=0x%08lx，重复=%u，已执行CONDRST恢复",
                (unsigned long)status, repeated ? 1U : 0U);
            mbedtls_platform_zeroize(output, length);                                                       		/* 任何健康错误都清空已生成的部分输出。 */
            copied = 0U;
            if(++recoveries > 2U) return -1;                                                                		/* 超过两次恢复机会后向调用方报告失败。 */
            vTaskDelay(pdMS_TO_TICKS(1U));
            continue;
        }
        if(ready) {
            if(part > sizeof(word)) part = sizeof(word);
            memcpy(output + copied, &word, part);
            copied += part;
        } else {
            if((TickType_t)(xTaskGetTickCount() - start) >= pdMS_TO_TICKS(1000U)) {
                APP_LOG_WARNING("云端隧道", "硬件熵等待超时：STAT=0x%08lx，已完成=%lu/%lu，丢弃剩余=%u",
                    (unsigned long)TRNG_STAT, (unsigned long)copied, (unsigned long)length, sRandomDiscard);
                return -1;
            }
            vTaskDelay(pdMS_TO_TICKS(1U));
        }
    }
    return 0;
}

/**
 * @brief 初始化 GD32H7 TRNG 并注册 Mbed TLS 内存分配回调。
 * @details 配置 NIST 模式及硬件熵处理参数，保留芯片库默认的健康检测阈值。
 */
void cloudPortInit(void)
{
    rcu_periph_clock_enable(RCU_TRNG);
    taskENTER_CRITICAL();
    trng_conditioning_reset_enable();
    trng_disable();
    trng_mode_config(TRNG_MODSEL_NIST);                            		/* 选择芯片 TRNG 的 NIST 熵处理模式。 */
    trng_clockdiv_config(TRNG_CLK_DIV16);
    trng_powermode_config(TRNG_NR_HIGH);
    trng_conditioning_algo_config(TRNG_ALGO_SHA1);
    trng_conditioning_input_bitwidth(TRNG_INMOD_256BIT);
    trng_conditioning_output_bitwidth(TRNG_OUTMOD_128BIT);
    trng_hash_init_disable();
    trng_conditioning_enable();
    trng_replace_test_disable();
    trng_clockerror_detection_enable();
    restartRandom();
    taskEXIT_CRITICAL();
    (void)mbedtls_platform_set_calloc_free(cloudCalloc, cloudFree);		/* 让密码库使用带上限与清零的公共堆分配器。 */
}

/**
 * @brief 获取密码库动态内存的当前计费值和历史峰值。
 * @param current 非 NULL 时接收当前分配总量。
 * @param peak 非 NULL 时接收历史分配峰值。
 */
void cloudMemoryStats(size_t *current, size_t *peak)
{
    taskENTER_CRITICAL();
    if(current != NULL) *current = sAllocated;
    if(peak != NULL) *peak = sPeak;
    taskEXIT_CRITICAL();
}

/**
 * @brief 通过认证引导协议取得 UTC 时间和云端分配的代理端口。
 * @param host 认证引导服务器的 IPv4 地址文本。
 * @param port 认证引导服务器的 UDP 端口。
 * @param device 设备标识。
 * @param secret 与服务器约定的设备 HMAC 密钥文本。
 * @param keyVersion 产品密钥版本。
 * @param proxyPort 用于接收云端分配端口的输出指针。
 * @return 请求、回包认证和字段校验全部成功时返回 true，否则返回 false。
 * @details 只有完整回包通过随机挑战、版本及 HMAC 校验后，才更新 UTC 和代理端口。
 */
bool cloudSyncTime(const char *host, uint16_t port, const char *device, const char *secret,
                   uint32_t keyVersion, uint16_t *proxyPort)
{
    struct sockaddr_in address;
    unsigned char request[CLOUD_BOOTSTRAP_REQUEST_SIZE], reply[CLOUD_BOOTSTRAP_REPLY_SIZE + 1U];
    unsigned char nonce[16];
    struct timeval timeout = {3,0};
    uint64_t seconds;
    uint16_t assigned;
    int fd, length;
    /* 使用硬件随机挑战生成认证请求；挑战生成或请求构造失败时不发送网络数据。 */
    if(proxyPort == NULL || cloudRandom(NULL, nonce, sizeof(nonce)) != 0 ||
       !cloudBootstrapRequest(device, secret, keyVersion, nonce, request)) return false;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = lwip_htons(port);
    if(inet_pton(AF_INET, host, &address.sin_addr) != 1) return false;
    /* 引导阶段使用独立 UDP Socket；该 Socket 只负责本次认证对时事务。 */
    fd = lwip_socket(AF_INET, SOCK_DGRAM, 0);
    if(fd < 0) return false;
    (void)lwip_setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    length = lwip_connect(fd, (struct sockaddr *)&address, sizeof(address));
    if(length == 0) length = lwip_send(fd, request, sizeof(request), 0);
    if(length == sizeof(request)) length = lwip_recv(fd, reply, sizeof(reply), 0);              		/* 仅在请求完整发送后才接收认证响应。 */
    (void)lwip_close(fd);
    /* 必须先通过完整回包认证，才允许更新设备的时间基准和代理端口。 */
    if(length < 0 || !cloudBootstrapReply(reply, (size_t)length, nonce, secret, keyVersion,
                                         &seconds, &assigned)) return false;
    taskENTER_CRITICAL();
    sUtcMilliseconds = seconds * 1000U;                                                         		/* 认证通过后才建立 UTC 毫秒基准。 */
    sTimeTick = xTaskGetTickCount();
    *proxyPort = assigned;                                                                      		/* 仅发布已通过回包认证和范围检查的代理端口。 */
    taskEXIT_CRITICAL();
    return true;
}

/**
 * @brief 在指定时限内等待 Socket 的可读或可写事件。
 * @param fd 待等待的 Socket 描述符。
 * @param writing 为 true 时等待可写事件，否则等待可读事件。
 * @param milliseconds 最大等待时长，单位为毫秒。
 * @return 发生目标事件时返回 true；超时或未检测到事件时返回 false。
 */
static bool waitSocket(int fd, bool writing, uint32_t milliseconds)
{
    fd_set set;
    struct timeval timeout;
    FD_ZERO(&set);
    FD_SET(fd, &set);
    timeout.tv_sec = milliseconds / 1000U;
    timeout.tv_usec = (milliseconds % 1000U) * 1000U;
    return lwip_select(fd + 1, writing ? NULL : &set, writing ? &set : NULL, NULL, &timeout) > 0;
}

/**
 * @brief 建立配置了 TCP 保活和非阻塞模式的 IPv4 TCP 连接。
 * @param host 目标服务器的 IPv4 地址文本。
 * @param port 目标 TCP 端口。
 * @param timeoutMs 非阻塞连接建立的最大等待时间，单位为毫秒。
 * @return 连接成功返回 Socket 描述符；参数、连接或等待失败时返回 -1。
 */
int cloudSocketConnect(const char *host, uint16_t port, uint32_t timeoutMs)
{
    struct sockaddr_in address;
    unsigned long nonblocking = 1UL;
    int fd, result, error = 0;
    int keepalive = 1, noDelay = 1, idleSeconds = 10, intervalSeconds = 3, probeCount = 3;
    socklen_t errorLength = sizeof(error);
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = lwip_htons(port);
    if(inet_pton(AF_INET, host, &address.sin_addr) != 1) return -1;
    fd = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if(fd < 0) return -1;
    /* 备用TLS连接长期等候StartWorkConn，须保活并主动识别静默失效的路径。 */
    /* 禁用Nagle，避免FRP短控制帧和实时Web报文等待后续小包。 */
    if(lwip_setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &noDelay, sizeof(noDelay)) != 0 ||
       lwip_setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive)) != 0 ||
       lwip_setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &idleSeconds, sizeof(idleSeconds)) != 0 ||
       lwip_setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &intervalSeconds, sizeof(intervalSeconds)) != 0 ||
       lwip_setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &probeCount, sizeof(probeCount)) != 0) goto failed;
    if(lwip_ioctl(fd, FIONBIO, &nonblocking) != 0) goto failed;                                            		/* 连接改为非阻塞，后续由 select 进行有界等待。 */
    result = lwip_connect(fd, (struct sockaddr *)&address, sizeof(address));                               		/* 非阻塞连接可能立即成功，也可能进入 EINPROGRESS。 */
    if(result == 0) return fd;
    if((errno != EINPROGRESS) && (errno != EWOULDBLOCK)) goto failed;
    if(!waitSocket(fd, true, timeoutMs) ||
       (lwip_getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &errorLength) != 0) || (error != 0)) goto failed;
    return fd;
failed:
    (void)lwip_close(fd);
    return -1;
}

/**
 * @brief 将 lwIP 非阻塞发送结果转换为 Mbed TLS BIO 回调约定。
 * @param context 指向底层 Socket 描述符的上下文指针。
 * @param data 待发送数据缓冲区。
 * @param length 待发送的字节数。
 * @return 成功时返回发送字节数；可重试状态返回 MBEDTLS_ERR_SSL_WANT_WRITE，其他错误返回 TLS 内部错误。
 */
static int tlsSend(void *context, const unsigned char *data, size_t length)
{
    int result = lwip_send(*(int *)context, data, length, 0);
    /* 非阻塞lwIP API消息/缓冲池不足同样可以重试，由外层30秒时限约束。 */
    if((result < 0) && ((errno == EWOULDBLOCK) || (errno == EAGAIN) ||
        (errno == ENOMEM) || (errno == ENOBUFS) || (errno == EINTR))) return MBEDTLS_ERR_SSL_WANT_WRITE;
    return (result < 0) ? MBEDTLS_ERR_SSL_INTERNAL_ERROR : result;
}

/**
 * @brief 将 lwIP 非阻塞接收结果转换为 Mbed TLS BIO 回调约定。
 * @param context 指向底层 Socket 描述符的上下文指针。
 * @param data 用于接收数据的缓冲区。
 * @param length 缓冲区可接收的最大字节数。
 * @return 成功时返回接收字节数；可重试状态返回 MBEDTLS_ERR_SSL_WANT_READ，连接关闭时保留 EOF 语义。
 */
static int tlsReceive(void *context, unsigned char *data, size_t length)
{
    int result = lwip_recv(*(int *)context, data, length, 0);
    if((result < 0) && ((errno == EWOULDBLOCK) || (errno == EAGAIN) ||
        (errno == ENOMEM) || (errno == ENOBUFS) || (errno == EINTR))) return MBEDTLS_ERR_SSL_WANT_READ;
    return (result < 0) ? MBEDTLS_ERR_SSL_INTERNAL_ERROR : result;
}

/**
 * @brief 在认证对时完成后建立独立的 TLS 1.2 客户端连接。
 * @param host 目标服务器的 IPv4 地址文本，用于建立连接并设置 TLS 主机名验证目标。
 * @param port 目标服务器的 TCP 端口。
 * @param ca 可信 CA 证书的 PEM 文本。
 * @return TLS 握手及证书验证成功时返回连接对象；任一初始化或握手步骤失败时返回 NULL。
 * @details 要求证书链验证成功，并使用配置的密码套件及随机数回调。
 */
cloudTls *cloudTlsOpen(const char *host, uint16_t port, const char *ca)
{
    static const int suites[] = {MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256, 0};
    cloudTls *connection;
    TickType_t start;
    int result = -1;
    /* 证书有效期验证依赖已认证 UTC；未完成对时时禁止建立 TLS。 */
    if(cloudUtcNow() == 0U) return NULL;
    connection = cloudCalloc(1U, sizeof(*connection));
    if(connection == NULL) return NULL;
    connection->fd = -1;
    /* 各密码上下文均归当前连接独占，初始化失败时统一由 cloudTlsClose 回收。 */
    mbedtls_ssl_init(&connection->ssl);
    mbedtls_ssl_config_init(&connection->config);
    mbedtls_x509_crt_init(&connection->ca);
    mbedtls_ctr_drbg_init(&connection->rng);
    if((result = mbedtls_ctr_drbg_seed(&connection->rng, cloudRandom, NULL,
                                      (const unsigned char *)"CMU-TLS-v1", 10U)) != 0) goto failed;
    if((result = mbedtls_x509_crt_parse(&connection->ca, (const unsigned char *)ca, strlen(ca) + 1U)) != 0) goto failed;
    if((result = mbedtls_ssl_config_defaults(&connection->config, MBEDTLS_SSL_IS_CLIENT,
                                             MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT)) != 0) goto failed;
    mbedtls_ssl_conf_authmode(&connection->config, MBEDTLS_SSL_VERIFY_REQUIRED);                                          		/* 将证书验证设为强制要求，禁止跳过验证。 */
    mbedtls_ssl_conf_ca_chain(&connection->config, &connection->ca, NULL);
    mbedtls_ssl_conf_rng(&connection->config, mbedtls_ctr_drbg_random, &connection->rng);
    mbedtls_ssl_conf_ciphersuites(&connection->config, suites);                                                           		/* 仅允许本模块明确列出的 TLS 密码套件。 */
    /* 完成 TLS 配置后绑定目标地址，后续握手会校验证书身份。 */
    if((result = mbedtls_ssl_setup(&connection->ssl, &connection->config)) != 0) goto failed;
    if((result = mbedtls_ssl_set_hostname(&connection->ssl, host)) != 0) goto failed;                                     		/* 设置 TLS 主机名验证目标。 */
    connection->fd = cloudSocketConnect(host, port, 5000U);
    if(connection->fd < 0) { result = MBEDTLS_ERR_SSL_INTERNAL_ERROR; goto failed; }
    /* 将 lwIP Socket 的收发适配函数注册为 TLS 底层 BIO。 */
    mbedtls_ssl_set_bio(&connection->ssl, &connection->fd, tlsSend, tlsReceive, NULL);
    start = xTaskGetTickCount();
    do {
        result = mbedtls_ssl_handshake(&connection->ssl);
        if(result == 0) return connection;                                                                                		/* 握手完成且验证成功后才返回连接对象。 */
        if((result != MBEDTLS_ERR_SSL_WANT_READ) && (result != MBEDTLS_ERR_SSL_WANT_WRITE)) break;
        (void)waitSocket(connection->fd, result == MBEDTLS_ERR_SSL_WANT_WRITE, 25U);
        vTaskDelay(pdMS_TO_TICKS(1U));
    } while((TickType_t)(xTaskGetTickCount() - start) < pdMS_TO_TICKS(12000U));
failed:
    APP_LOG_WARNING("云端隧道", "TLS失败：错误=%d，证书标志=0x%08lx", result,
                    (unsigned long)mbedtls_ssl_get_verify_result(&connection->ssl));
    cloudTlsClose(connection);
    return NULL;
}

/**
 * @brief 从 TLS 连接读取解密后的应用数据。
 * @param connection 已建立的 TLS 连接。
 * @param data 用于接收明文的缓冲区。
 * @param length 最多读取的字节数。
 * @return 返回 Mbed TLS 的读取结果；WANT_READ/WANT_WRITE 等状态由调用方处理。
 */
int cloudTlsRead(cloudTls *connection, void *data, size_t length)
{
    return mbedtls_ssl_read(&connection->ssl, data, length);
}

/**
 * @brief 在有界时限内发送完整的 TLS 应用数据。
 * @param connection 已建立的 TLS 连接。
 * @param data 待发送的明文缓冲区。
 * @param length 待发送的字节数。
 * @return 全部数据发送成功返回 true；发生 TLS 错误或等待超时返回 false。
 * @details 处理部分写入和 WANT_READ/WANT_WRITE，避免只发送半帧后继续发送后续帧。
 */
bool cloudTlsWrite(cloudTls *connection, const void *data, size_t length)
{
    const unsigned char *bytes = data;
    size_t offset = 0U;
    TickType_t start = xTaskGetTickCount();
    while(offset < length) {
        int result = mbedtls_ssl_write(&connection->ssl, bytes + offset, length - offset);
        if(result > 0) {
            offset += (size_t)result;                                                               		/* 按实际发送字节数推进，兼容 TLS 部分写入。 */
        } else if((result == MBEDTLS_ERR_SSL_WANT_WRITE) || (result == MBEDTLS_ERR_SSL_WANT_READ)) {
            (void)waitSocket(connection->fd, result == MBEDTLS_ERR_SSL_WANT_WRITE, 25U);            		/* 等待对应事件后再重试 TLS 写操作。 */
        } else {
            APP_LOG_WARNING("云端隧道", "TLS写失败：结果=%d，Socket错误=%d", result, errno);
            return false;
        }
        if((TickType_t)(xTaskGetTickCount() - start) > pdMS_TO_TICKS(30000U)) {
            APP_LOG_WARNING("云端隧道", "TLS写等待30秒超时，Socket错误=%d", errno);
            return false;
        }
    }
    return true;
}

/**
 * @brief 获取 TLS 连接所使用的底层 Socket 描述符。
 * @param connection TLS 连接对象。
 * @return 底层 Socket 描述符；该接口不转移 Socket 的关闭所有权。
 */
int cloudTlsSocket(const cloudTls *connection) { return connection->fd; }
/**
 * @brief 检查 TLS 内部是否已有可立即读取的明文数据。
 * @param connection TLS 连接对象。
 * @return 内部存在待处理明文时返回 true，否则返回 false。
 */
bool cloudTlsPending(const cloudTls *connection) { return mbedtls_ssl_check_pending(&connection->ssl) != 0; }

/**
 * @brief 关闭 TLS 连接并释放其全部密码库资源。
 * @param connection 待关闭的 TLS 连接；允许传入 NULL。
 * @details 对已完成握手的连接发送 close_notify，然后关闭 Socket 并释放各上下文。
 */
void cloudTlsClose(cloudTls *connection)
{
    if(connection == NULL) return;
    if(connection->fd >= 0) {
        if(mbedtls_ssl_is_handshake_over(&connection->ssl)) (void)mbedtls_ssl_close_notify(&connection->ssl);
        (void)lwip_shutdown(connection->fd, SHUT_RDWR);
        (void)lwip_close(connection->fd);
    }
    mbedtls_ssl_free(&connection->ssl);
    mbedtls_ssl_config_free(&connection->config);
    mbedtls_x509_crt_free(&connection->ca);
    mbedtls_ctr_drbg_free(&connection->rng);
    cloudFree(connection);
}
